//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include "blobulator/point3d.h"
#include "blobulator/implicit/impparticle.h"
#include "blobulator/implicit/impdefines.h"
#include "blobulator/implicit/imprenderer.h"
#include "blobulator/implicit/imptiler.h"
#if (MT_ENABLED == 1)
	#include "blobulator/implicit/syncro.h"
#endif
#include "blobulator/util/fasttrunc.h"
#if BLOB_USE_ABSTRACTION
#include "blobulator/iblob_renderer.h"
#endif
#include "../util/rand.h"

#include "memdbgon.h"


Point3D tile_debug_color = Point3D();

#if(MT_ENABLED == 1)
HANDLE hThread[MAX_WORKER_THREADS]; 
DWORD WINAPI ThreadFunc( LPVOID lpParam ); // Forward declaration
DWORD processorPref[4] = {0x01, 0x04, 0x02, 0x08};
DWORD threadNos[4] = {0, 1, 2, 3};
#endif
RENDERER_CLASS* renderers[4];

#if (MT_ENABLED == 1)
void ImpTiler::launchThreads()
{
	renderers[0] = m_pSweepRenderer;

	// Start at 1 because thread 0 is the main thread
	for(int i = 1; i < MAX_WORKER_THREADS; i++)
	{
		renderers[i] = new RENDERER_CLASS();

		DWORD dwThreadId; 

		hThread[i] = CreateThread( 
			NULL,                        // default security attributes 
			0,                           // use default stack size  
			ThreadFunc,                  // thread function 
			&(threadNos[i]),			     // argument to thread function 
			(SUSPEND_BET_FRAMES ? CREATE_SUSPENDED : 0),            // creation flags 
			&dwThreadId);                // returns the thread identifier 

		if (hThread[i] == NULL) 
		{
			IMP_PRINT("CreateThread failed.\n" ); 
		}

		/*
		DWORD dwIdealProcessor = 2;
		DWORD retVal = SetThreadIdealProcessor(hThread, dwIdealProcessor);

		if (retVal == -1) 
		{
			DWORD error = GetLastError();
			IMP_PRINT("SetThreadIdealProcessor failed. RetVal=%d, Error Code=%d\n", retVal, error); 
		}
		else
		{
			IMP_PRINT("SetThreadIdealProcessor succeeded.\n" ); 	
		}
		*/

		DWORD dwThreadAffinityMask = processorPref[i];

		DWORD_PTR amRetVal = SetThreadAffinityMask(hThread[i], dwThreadAffinityMask);
	  
		if(amRetVal == 0)
		{
			IMP_PRINT("SetThreadAffinityMask failed\n");
		}

		/*
		else 
		{
			_getch();
			CloseHandle( hThread );
		}
		*/

		IMP_PRINT("Thread %d launched!!!!\n", i);
	}
}
#endif

// When we create a new tile, make sure we have a new renderer properly initialized.
// Note that this renderer is going to do some real work (unlike the one that is used by ImpTiler).
ImpTile::ImpTile(int _x, int _y, int _z)
	:
	x(_x),
	y(_y),
	z(_z),
	m_pRenderer(ImpRendererFactory::GetInstance()->AcquireTileRenderer())
{
	// Do nothing...
}

ImpTile::~ImpTile()
{
	ImpRendererFactory::GetInstance()->ReleaseTileRenderer(m_pRenderer);
	m_pRenderer = NULL;
}

ImpTiler::ImpTiler()
	:
	m_pRenderer(ImpRendererFactory::GetInstance()->AcquireTilerRenderer()),
	m_nTileIndexToDraw(-1),
	m_pRenderContext(NULL),
	m_pMatrices(NULL),
	m_nNumberOfMatrices(0)
{
#if (MT_ENABLED == 1)
	launchThreads();
#endif
}

ImpTiler::~ImpTiler()
{
	ImpRendererFactory::GetInstance()->ReleaseTilerRenderer(m_pRenderer);
	m_pRenderer = NULL;
}

void ImpTiler::beginFrame(const Point3D & offset, bool bTileMode, bool bUseTwoPasses)
{
	m_pRenderer->SetCubeWidth(m_fCubeWidth);
	m_pRenderer->SetRenderRadius(m_fRenderRadius);
	m_pRenderer->SetCutoffRadius(m_fCuttoffRadius);

	m_Offset = offset;
	m_bTileMode = bTileMode;
	m_RenderDimension = m_pRenderer->GetInnerDimensions();
	m_fRenderMargin = m_pRenderer->GetMarginWidth();
	m_CornerOffset = offset - (m_RenderDimension * 0.5f);

	if (bUseTwoPasses == false)
	{
		m_pRenderer->BeginFrame(bTileMode, m_pRenderContext);
	}
}

void ImpTiler::insertParticle(ImpParticle* p)
{
	#if (RENDER_MODE==RENDER_INF_HASH)
		addParticleToTile(p, 0, 0, 0);
	#else
		Point3D voxel = ((p->center - m_CornerOffset) / m_RenderDimension);
		int x = round_down(voxel[0]);
		int y = round_down(voxel[1]);
		int z = round_down(voxel[2]);

		// Add particle to any neighboring overlapping tiles
		Point3D tileMins = calcTileCorner(x, y, z);
		Point3D tileMaxs = tileMins + m_RenderDimension;

		bool x_tiles[3] = {p->center[0] <= tileMins[0]+m_fRenderMargin, true, p->center[0] >= tileMaxs[0]-m_fRenderMargin};
		bool y_tiles[3] = {p->center[1] <= tileMins[1]+m_fRenderMargin, true, p->center[1] >= tileMaxs[1]-m_fRenderMargin};
		bool z_tiles[3] = {p->center[2] <= tileMins[2]+m_fRenderMargin, true, p->center[2] >= tileMaxs[2]-m_fRenderMargin};

		// TODO: I should replace this triple for loop with either the code
		// that I have in PhysTiler, or a lookup based on the 6 booleans
		// calculated above. I should make sure this improves perf.
		for(int dx=-1; dx <= 1; dx++)
		{
			if(x_tiles[dx+1])
			{
				for(int dy=-1; dy <= 1; dy++)
				{
					if(y_tiles[dy+1])
					{
						for(int dz=-1; dz <= 1; dz++)
						{
							if(z_tiles[dz+1])
							{
								addParticleToTile(p, x+dx, y+dy, z+dz);
							}
						}
					}
				}
			}
		}
	#endif
}

void ImpTiler::addParticleToTile(ImpParticle*p, int x, int y, int z)
{
	ImpTile* tile = createTile(x, y, z);
	tile->particles.pushAutoSize(p);
}

Point3D ImpTiler::calcTileCorner(int x, int y, int z)
{
	Point3D tile_corner = m_CornerOffset;
	tile_corner[0] += m_RenderDimension[0]*x;
	tile_corner[1] += m_RenderDimension[1]*y;
	tile_corner[2] += m_RenderDimension[2]*z;
	return tile_corner;
}

// Calculate the center-point of a tile
Point3D ImpTiler::calcTileOffset(int x, int y, int z)
{
	Point3D tile_offset = m_Offset;
	tile_offset[0] += m_RenderDimension[0]*x;
	tile_offset[1] += m_RenderDimension[1]*y;
	tile_offset[2] += m_RenderDimension[2]*z;
	return tile_offset;
}

Point3D ImpTiler::getTileOffset(int i)
{
	return m_Tiles[i]->center;
}

void ImpTiler::drawTile(ImpTile * pTile, bool bUseTwoPasses)
{
	BLOB_RENDERER_CLASS * pTileRenderer = pTile->m_pRenderer;
#if(COLOR_TILES==1)
	tile_debug_color = pTile->debug_color;
#endif

	// Note that this renderer may not dispatch to the fragmented vertex buffer, so passing a m_pRenderContext may be meaningless here.
	pTileRenderer->SetPortalMatrices(m_pMatrices, m_nNumberOfMatrices);
	pTileRenderer->BeginFrame(m_bTileMode, m_pRenderContext);
	pTileRenderer->BeginTile(pTile->center);

	for(int j = 0; j < pTile->particles.size; j++)
	{
		ImpParticle *p = pTile->particles[j];
		pTileRenderer->AddParticle(*p, false);
	}

	pTileRenderer->DrawTile();

	if (bUseTwoPasses == false)
	{
		pTileRenderer->EndTile();
		pTileRenderer->EndFrame();
	}

	m_LastTileOffset = pTile->center;
}

void ImpTiler::drawTile(int x, int y, int z, bool bUseTwoPasses)
{
	ImpTile* tile = findTile(x, y, z);
	if(tile != NULL)
	{
		drawTile(tile, bUseTwoPasses);
	}
}

void ImpTiler::drawSurface(bool bUseTwoPasses)
{
	#if (MT_ENABLED == 1)
		drawSurfaceSortedMT(false, Point3D());
	#else
		for(int i = 0; i < m_Tiles.size; i++)
		{
			if (m_nTileIndexToDraw != -1)
			{
				if (i != m_nTileIndexToDraw)
				{
					continue;
				}
			}
//			if(maxNoTilesToDraw == i) break;
			drawTile(m_Tiles[i], bUseTwoPasses);
		}
	#endif
}

class C
{
public:
	static bool gt(ImpTile* a, ImpTile* b)
	{
		return a->dist < b->dist;
	}
};

void ImpTiler::drawSurfaceSorted(Point3D const& eye, bool bUseTwoPasses)
{
	#if (MT_ENABLED == 1)
		drawSurfaceSortedMT(true, eye);
	#else
		for(int i = 0; i < m_Tiles.size; i++)
		{
			m_Tiles[i]->dist = (m_Tiles[i]->center - eye).length();
		}

		C sorter;
		m_Tiles.sort(sorter);

		for(int i = 0; i < m_Tiles.size; i++)
		{
			if (m_nTileIndexToDraw != -1)
			{
				if (i != m_nTileIndexToDraw)
				{
					continue;
				}
			}
			//			if(maxNoTilesToDraw == i) break;
			drawTile(m_Tiles[i], bUseTwoPasses);
		}
	#endif
}

int ImpTiler::getNoTiles()
{
	return m_Tiles.size;
}

ImpTile* ImpTiler::getTile(int i)
{
	return m_Tiles[i];
}


#if (MT_ENABLED == 1)
int n_tilesToFlush;
SmartArray<ImpTile*> tilesToRender;
SmartArray<ImpTile*> tilesToFlush;

DWORD WINAPI ThreadFunc( LPVOID lpParam )
{
	DWORD threadNo = *((DWORD*)lpParam);
	RENDERER_CLASS* sweepRenderer = renderers[threadNo];

	while(true)
	{
		while(true)
		{
			if(
				#if (MAIN_THREAD_DOESNT_DRAW == 1)
					(threadNo > 0 && FORCE_READ(tilesToRender.size) > 0) ||
				#else
					FORCE_READ(tilesToRender.size) > 0 ||
				#endif
				#if (SORT_BUFFERS == 1)
					(threadNo == 0 && ((FORCE_READ(tilesToFlush.size) > 0 && FORCE_READ(tilesToFlush.last()->done)) || n_tilesToFlush == 0))
				#else
					(threadNo == 0 && (FORCE_READ(tilesToFlush.size) > 0  || FORCE_READ(n_tilesToFlush) == 0))
				#endif

			  ) break;
			_asm pause;
		}

		BlrGetLock(&taskListLock);

		if(threadNo == 0)
		{
			#if (SORT_BUFFERS == 1)
			if(tilesToFlush.size > 0 && tilesToFlush.last()->done)
			{
			#else
			if(tilesToFlush.size > 0)
			{
			#endif
				ImpTile* tileToFlush = tilesToFlush.pop();
				Assert(n_tilesToFlush > 0);
				n_tilesToFlush --;
				BlrReleaseLock(&taskListLock);

				LocalBuffer::flushTileBuffers(tileToFlush);

				// NOTE: This is locked by code in releaseTileBuffers!
				LocalBuffer::releaseTileBuffers(tileToFlush);
				continue;
			}
			else if (n_tilesToFlush == 0)
			{
				return 0;
			}
			#if (MAIN_THREAD_DOESNT_DRAW == 1)
				BlrReleaseLock(&taskListLock);
				continue;
			#endif
		}

		if(tilesToRender.size == 0)
		{
			BlrReleaseLock(&taskListLock);
			continue;
		}

		ImpTile* tileToRender = tilesToRender.pop();

		BlrReleaseLock(&taskListLock);
		
		drawTile(tileToRender, sweepRenderer);

		BlrGetLock(&taskListLock);
		tileToRender->done = true;
		#if (SORT_BUFFERS == 0)
			tilesToFlush.pushAutoSize(tileToRender);
		#endif
		BlrReleaseLock(&taskListLock);
	}
}


void ImpTiler::drawSurfaceSortedMT(bool depthSort, Point3D const& eye)
{
	if(m_Tiles.size == 0) return;

	#if (SUSPEND_BET_FRAMES == 1)
		for(int i = 1; i < min(m_Tiles.size+1, MAX_WORKER_THREADS); i++)
		{
			DWORD rtRetVal = ResumeThread(hThread[i]);
			if (rtRetVal == -1)
			{
				IMP_PRINT("ResumeThreadFailed\n");
			}
		}
	#endif

	if(depthSort)
	{
		for(int i = 0; i < m_Tiles.size; i++)
		{
			m_Tiles[i]->dist = (m_Tiles[i]->center - eye).length();
		}

		m_Tiles.sort<C>();
	}

	for(int i = m_Tiles.size-1; i >= 0; i--)
	{
		m_Tiles[i]->done = false;
		tilesToRender.pushAutoSize(m_Tiles[i]);
		#if (SORT_BUFFERS == 1)
			tilesToFlush.pushAutoSize(m_Tiles[i]);
		#endif
	}

	#if (SORT_BUFFERS == 0)
		Assert(tilesToFlush.size == 0);
	#endif

	n_tilesToFlush = m_Tiles.size;

	BlrReleaseLock(&taskListLock);

	ThreadFunc((LPVOID)(&threadNos[0]));

	Assert(tilesToRender.size == 0 && tilesToFlush.size == 0);

	#if (SUSPEND_BET_FRAMES == 1)
		for(int i = 1; i < min(m_Tiles.size+1, MAX_WORKER_THREADS); i++)
		{
			DWORD rtRetVal = SuspendThread(hThread[i]);
			if (rtRetVal == -1)
			{
				IMP_PRINT("ResumeThreadFailed\n");
			}
		}
	#endif
}

#endif


void ImpTiler::endFrame(bool bUseTwoPasses)
{
	if (bUseTwoPasses)
	{
		m_pRenderer->BeginFrame(m_bTileMode, m_pRenderContext);

		// EndTile is going to do the loop to display each fragments...
		for (int i = 0 ; i < m_Tiles.size ; ++i)
		{
			if (m_nTileIndexToDraw != -1)
			{
				if (i != m_nTileIndexToDraw)
				{
					continue;
				}
			}
			// We synchronize all the tiles at that point instead of within the draw...
			m_Tiles[i]->m_pRenderer->EndTile();
			m_Tiles[i]->m_pRenderer->EndFrame();
		}

	}

	//ConMsg("%d\n", tiles.size);
	#if(COLOR_TILES==1)
		SmartArray<ImpTile*> tiles2;
		for(int i = 0; i < m_Tiles.size; i++)
		{
			if(m_Tiles[i]->particles.size > 0)
			{
				m_Tiles[i]->old_tile = true;
				m_Tiles[i]->particles.size = 0;
				tiles2.pushAutoSize(m_Tiles[i]);
			}
			else
			{
				// Before we delete, let's recycle the renderer
				ImpTile * pTile = m_Tiles[i];
				ImpRendererFactory::GetInstance()->ReleaseTileRenderer(pTile->m_pRenderer);
				delete(m_Tiles[i]);
			}
		}
		m_Tiles.swapData(tiles2);
		// Tiles 2 and its data will automatically get destructed here
	#else
		for(int i = 0; i < m_Tiles.size; i++)
		{
			delete m_Tiles[i];
		}
		m_Tiles.size = 0;
	#endif
	m_pRenderer->EndFrame();
}

ImpTile* ImpTiler::findTile(int x, int y, int z)
{
	for(int i = 0; i < m_Tiles.size; i++)
	{
		ImpTile* tile = m_Tiles[i];
		if(tile->x == x && tile->y == y && tile->z == z)
		{
			#if(COLOR_TILES==1)
				if(tile->old_tile == true)
				{
					tile->old_tile = false;
					tile->center = calcTileOffset(x, y, z); // Have to recalculate center because cubeWidth might have changed
				}
			#endif
			return tile;
		}
	}
	return NULL;
}

ImpTile * ImpTiler::createTile(int x, int y, int z)
{
	ImpTile * pTile = findTile(x,y,z);
	if(pTile == NULL)
	{
		pTile = new ImpTile(x, y, z);
		pTile->center = calcTileOffset(x, y, z);
		pTile->dist = 0.0f;
		#if(COLOR_TILES==1)
			pTile->old_tile = false;
			pTile->debug_color.set(frand(),frand(),frand());
		#endif

#if BLOB_USE_ABSTRACTION
		pTile->m_pRenderer->SetCubeWidth(m_fCubeWidth);
		pTile->m_pRenderer->SetRenderRadius(m_fRenderRadius);
		pTile->m_pRenderer->SetCutoffRadius(m_fCuttoffRadius);
#endif

		m_Tiles.pushAutoSize(pTile);
	}
	return pTile;
}

void ImpTiler::SetCubeWidth(float fCubeWidth)
{
	m_fCubeWidth = fCubeWidth;
}

void ImpTiler::SetRenderRadius(float fRenderRadius)
{
	m_fRenderRadius = fRenderRadius;
}

void ImpTiler::SetCutoffRadius(float fCuttoffRadius)
{
	m_fCuttoffRadius = fCuttoffRadius;
}

void ImpTiler::SetRenderContext(void * pRenderContext)
{
	m_pRenderContext = pRenderContext;
}

void ImpTiler::SetPortalMatrices(const matrix3x4_t * pMatrices, int nNumberOfMatrices)
{
	m_pMatrices = pMatrices;
	m_nNumberOfMatrices = nNumberOfMatrices;
}

// TODO: Move this into ImpTilerFactory.cpp?
// TODO: I can make an abstract factory class that does all this for any object
// Find the right place to call CreateInstance() and ReleaseInstance(). As this demonstrates, usage of singleton should be avoided.
ImpTilerFactory * ImpTilerFactory::s_pInstance = ImpTilerFactory::CreateInstance();

ImpTilerFactory * ImpTilerFactory::CreateInstance()
{
	Assert(s_pInstance == NULL);
	s_pInstance = new ImpTilerFactory();
	return s_pInstance;
}

void ImpTilerFactory::ReleaseInstance()
{
	Assert(s_pInstance != NULL);
	delete s_pInstance;
	s_pInstance = NULL;
}

ImpTilerFactory::ImpTilerFactory()
:
	m_tilers(),
	m_Mutex(),
	m_bAddedToMaterials(false)
{
	// We can't register here as "materials" may not be set yet (see static construction above :( ).
}

ImpTilerFactory::~ImpTilerFactory()
{
	bool bResult;
	bResult = materials->RemoveOnLevelShutdownFunc( ClearImpTilers, NULL );
	Assert(bResult);
}

ImpTiler * ImpTilerFactory::AcquireTiler()
{
	AUTO_LOCK(m_Mutex);
	if ( m_tilers.size > 0 )
	{
		return m_tilers.pop();
	}
	else
	{
		if (m_bAddedToMaterials == false)
		{
			bool bResult;
			bResult = materials->AddOnLevelShutdownFunc( ClearImpTilers, NULL );
			Assert(bResult);
			m_bAddedToMaterials = true;
		}
		return new ImpTiler();
	}
}

// TODO: Make this thread-safe (in case multiple threads are simultaneously using a tiler)
void ImpTilerFactory::ReleaseTiler(ImpTiler * pTiler)
{
	AUTO_LOCK(m_Mutex);
	// TODO: When a tiler is returned, ideally, I should take out the renderer,
	// and return that too.
	m_tilers.pushAutoSize(pTiler);
}

void ImpTilerFactory::ClearImpTilers( void * pUserData )
{
	GetInstance()->ClearImpTilers();
}

// Will clear the tiles only on the list.
// This is safe to be called at any point as the tilers would not be in use, but it may leave some tilers behind in some cases.
void ImpTilerFactory::ClearImpTilers()
{
	{
		AUTO_LOCK(m_Mutex);

		while (m_tilers.size > 0)
		{
			ImpTiler * pTiler = m_tilers.pop();
			delete pTiler;
		}
	}

	ImpRendererFactory::GetInstance()->ClearRenderers();
}

