//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include <math.h>
#include "blobulator/util/constants.h"
#include "blobulator/timer.h"
#include "blobulator/util/fasttrunc.h"
#include "blobulator/smartarray.h"
#include "blobulator/point3d.h"
#include "blobulator/implicit/hashrenderer.h"
#include "blobulator/implicit/particlecache.h"
#include "../vertexbuffers/vertexbuffer_inl.h"

#include "memdbgon.h"


///////////////////////////////
// TODO: MOVE TO VERTEX BUFFER
///////////////////////////////
#if (RENDER_API==OPENGL_API)
	//#include <windows.h>
	//#include <GL/gl.h>
#else
	#include "tier0/vprof.h"
	#include "engine/ivdebugoverlay.h"
	//#include "materialsystem/imesh.h"
	//#include "cdll_client_int.h"
	//#include "enginecallback.h"
	//#include "engine/ivdebugoverlay.h"
#endif

#if (DRAWING_TRIS==1)
	#include "impconsttable_winding_ccw2_tris.h"
#else
	#include "impconsttable_winding_ccw2.h"
#endif

#include "tier0/memdbgon.h"


///////////////////////////////////////////////
// Preprocessor macros
///////////////////////////////////////////////

//#define SPEED_TEST
//#define ERROR_CHECK
//#define OVERFLOW_CHECK


#define USHORT_MAX (0xFFFF)
// A quick way to check if a float is negative.
#define ISNEG(f) (*(int*)(&(f)) & 0x80000000)


///////////////////////////////////////////////////////
// Static factors that control how isosurface is rendered
///////////////////////////////////////////////////////

// Cubewidth is the size of a voxel on a side.
// Increase to speed up rendering, reduce quality
#define DEFAULT_CUBEWIDTH 0.8f
float HashRenderer::cubewidth = DEFAULT_CUBEWIDTH;
float HashRenderer::oneOverCubewidth = 1.0f / DEFAULT_CUBEWIDTH;
int HashRenderer::cornerInfoSize = sizeof(CornerInfo);

#if (DYNAMIC_FACTORS == 0)
	#define BLR_CONST const
#else
	#define BLR_CONST
#endif

// Constants for isosurface generation
BLR_CONST float HashRenderer::cutoffR = 3.3f; // 3.3 min... 3.5 better... 3.0 is much faster but doesn't look as good
BLR_CONST float HashRenderer::renderR = 1.3f; // 1.3... or 1.4

// These are derived from the constants above. Used to accelerate
// drawing code
BLR_CONST float HashRenderer::cutoffRSq = HashRenderer::cutoffR * HashRenderer::cutoffR;
BLR_CONST float HashRenderer::scaler = 1.0f / (sqrtf(2.0f) * HashRenderer::cutoffR);
BLR_CONST float HashRenderer::scalerSq = HashRenderer::scaler*HashRenderer::scaler;
BLR_CONST float HashRenderer::scaledRenderRSq = HashRenderer::renderR * HashRenderer::renderR * HashRenderer::scalerSq;
BLR_CONST float HashRenderer::threshold = 0.25f + HashRenderer::scaledRenderRSq * HashRenderer::scaledRenderRSq - HashRenderer::scaledRenderRSq;
BLR_CONST float HashRenderer::oneOverThreshold = 1.0f / HashRenderer::threshold;
BLR_CONST float HashRenderer::addAmt = 0.25f;
BLR_CONST short HashRenderer::fieldCalcSteps = short(HashRenderer::cutoffR * HashRenderer::oneOverCubewidth);

#undef BLR_CONST

bool HashRenderer::tile_mode = false;
int HashRenderer::marginNCubes = 0;
float HashRenderer::marginWidth = 0;

Point3D HashRenderer::outerDimensions = Point3D();
Point3D HashRenderer::innerDimensions = Point3D();

///////////////////////////////////////////////////
// Declaration of timing variables
///////////////////////////////////////////////////

#if (SPEED_TEST==1)
	long recurse_depth = 0;
	long voxel_count = 0;
	long sample_count = 0;
	long cum_time_diff = 0;

	int no_datapoints = 0;

	int aa = 0;
	int bb = 0;
	int tt = 0;
	int zz = 0;
#endif

///////////////////////////////////////////////
// User Declared Function pointers
///////////////////////////////////////////////

static bool DOFASTCALL (*calc_sign)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, PARTICLE_CACHE_CLASS* pCache) = NULL;
static void DOFASTCALL (*calc_sign2)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache) = NULL;
static void DOFASTCALL (*calc_corner)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache) = NULL;
static void DOFASTCALL (*calc_vertex)(const  float x, const float y, const float z, const int edge, const CornerInfo* corner1, const CornerInfo* corner2, VERTEX_BUFFER_CLASS* vertexBuffer) = NULL;


////////////////////////////////////////////////////////////
// HashRenderer static member functions
////////////////////////////////////////////////////////////

void HashRenderer::setCalcSignFunc(bool DOFASTCALL (*calc_sign_ptr)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, PARTICLE_CACHE_CLASS* pCache))
{
	calc_sign = calc_sign_ptr;
}

void HashRenderer::setCalcSign2Func(void DOFASTCALL (*calc_sign2_ptr)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache))
{
	calc_sign2 = calc_sign2_ptr;
}

void HashRenderer::setCalcCornerFunc(int _cornerInfoSize, void DOFASTCALL (*calc_corner_ptr)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache))
{
	calc_corner = calc_corner_ptr;
	Assert(_cornerInfoSize >= sizeof(CornerInfo));
	// Round up extraCornerDataSize to a multiple of 16 to allow for proper SSE alignment
	// TODO: Ideally I should only require this when compiling with SSE.
	cornerInfoSize = (((_cornerInfoSize + 15) / 16) * 16);
	Assert(cornerInfoSize % 16 == 0);
}

void HashRenderer::setCalcVertexFunc(void DOFASTCALL (*calc_vertex_ptr)(const  float x, const float y, const float z, const int edge, const CornerInfo* corner1, const CornerInfo* corner2, VERTEX_BUFFER_CLASS* vertexBuffer))
{
	calc_vertex = calc_vertex_ptr;
}




void HashRenderer::setCubeWidth(float _cubewidth)
{
	if(_cubewidth == cubewidth) return;
	changeCubeWidth(_cubewidth);
	recalculateDimensions();
	// Note that this will also affect the bounding box of cubes.
}

void HashRenderer::setCutoffR(float _cutoffR)
{
	if(_cutoffR == cutoffR) return;
	changeRadii(_cutoffR, renderR);
	recalculateDimensions();
	// Note that this will also affect the bounding box of cubes.
}

void HashRenderer::setRenderR(float _renderR)
{
	if(_renderR == renderR) return;
	changeRadii(cutoffR, _renderR);
	recalculateDimensions();
	// Note that this will also affect the bounding box of cubes.
}




float HashRenderer::getCubeWidth()
{
	return cubewidth;
}

float HashRenderer::getCutoffR()
{
	return cutoffR;
}

float HashRenderer::getRenderR()
{
	return renderR;
}




int HashRenderer::getMarginNCubes()
{
	return marginNCubes;
}

float HashRenderer::getMarginWidth()
{
	return marginWidth;
}

Point3D& HashRenderer::getInnerDimensions()
{
	return innerDimensions;
}



void HashRenderer::changeCubeWidth(float _cubewidth)
{
	cubewidth = _cubewidth;
	oneOverCubewidth = 1.0f / cubewidth;
}

void HashRenderer::changeRadii(float _cutoffR, float _renderR)
{
	#if (DYNAMIC_FACTORS == 1)
		if(_renderR >= _cutoffR) _renderR = _cutoffR;

		cutoffR = _cutoffR;
		renderR = _renderR;

		// These are derived from the constants above
		cutoffRSq = cutoffR * cutoffR;
		scaler = 1.0f / (sqrtf(2.0f) * cutoffR);
		scalerSq = scaler*scaler;
		scaledRenderRSq = renderR * renderR * scalerSq;
		threshold = 0.25f + scaledRenderRSq * scaledRenderRSq - scaledRenderRSq;
		oneOverThreshold = 1.0f / threshold;
		addAmt = 0.25f;
	#else
		IMP_PRINT("Can't change Radii if factors are not dynamic\n");
	#endif
}

void HashRenderer::recalculateDimensions()
{
	fieldCalcSteps = short(cutoffR * oneOverCubewidth);

	float fudgeFactor = 0.001f; // Just in case, so we don't go too small on the marginNCubes, and get killed by rounding error
	marginNCubes = round_up(cutoffR/cubewidth + fudgeFactor); 
	marginWidth = marginNCubes * cubewidth;

	outerDimensions.set(xsize*cubewidth, ysize*cubewidth, zsize*cubewidth);
	innerDimensions.set((xsize-marginNCubes*2)*cubewidth, (ysize-marginNCubes*2)*cubewidth, (zsize-marginNCubes*2)*cubewidth);
}



///////////////////////////////////////////////////
// Function definitions
///////////////////////////////////////////////////

/*
void debugDrawBounds()
{
	#if (RENDER_API == VALVE_API)
		Vector origin(offset[0], offset[1], offset[2]);
		Vector vMins(innerBB1[0], innerBB1[1], innerBB1[2]);
		Vector vMaxs(innerBB2[0], innerBB2[1], innerBB2[2]);
		debugoverlay->AddBoxOverlay( origin, vMins, vMaxs, QAngle( 0, 0, 0 ), 0, 255, 0, 16, 0 );
	#endif
}
*/

HashRenderer::HashRenderer()
	: offset(0,0,0)
	//curTime(0)
	, n_frames(0)
	, n_tiles(0)
	, memory_total(0)
	, start_time(0)
{

	IMP_PRINT("theshold=%f\n",threshold);

	pCache = new PARTICLE_CACHE_CLASS();
	vertexBuffer = new VERTEX_BUFFER_CLASS();
	vertexBuffer->init();

	//particleCache = new MCParticleCache(1.0, 1.66, 2.0, 3.3);
	//particleCache = new MCParticleCache(1.0, 2.0, 2.0, 3.3);
	//particleCache->setMode(false);

	// NOTE: This is a fudge which is necessary
	// because there are several static variables
	// that don't have defaults
	static bool firstTime = true;
	if(firstTime)
	{
		recalculateDimensions();
		firstTime = false;
	}

	for(int i = 0; i < 1000; i++)
	{
		slots.pushAutoSize(NULL);
	}
	
	corner_info_cache_used = 0;
}

void HashRenderer::recalculateBB()
{
	outerBBMins = offset-(outerDimensions*0.5f);
	outerBBMaxs = offset+(outerDimensions*0.5f);

	innerBBMins = offset-(innerDimensions*0.5f);
	innerBBMaxs = offset+(innerDimensions*0.5f);
}

void HashRenderer::setOffset(Point3D const& _offset)
{
	offset = _offset;
	recalculateBB();
	pCache->setOffset(_offset); // This is used by the regular 3D particle cache
}

// Call this to begin a frame
void HashRenderer::beginFrame(bool _tile_mode, void* pRenderContext)
{
	#if (RENDER_API==VALVE_API)
		VPROF_BUDGET( "IsoSurface", "Blobulator" );
	#endif

	#if (SPEED_TEST==1)
		long start_time = clock();
	#endif

	#if (IMP_PROFILE == 1)
		if(n_frames == 100)
		{
			__int64 end_time = get_time();
			float time_per_frame = float(end_time - start_time) / float(get_freq() * 100);
			IMP_PRINT("Render time per frame = %f ms (%f fps)\n", time_per_frame, 1.0f/time_per_frame);
			n_frames = 0;
		}
		if(n_frames == 0) start_time = get_time();
		n_frames++;
	#endif

	//debugDrawBounds();

	tile_mode = _tile_mode;
	//per_vertex_color = _per_vertex_color;
	
	vertexBuffer->beginFrame(pRenderContext);
}

// Remember to set offset before calling beginTile
void HashRenderer::beginTile(const Point3D & offset)
{
	#if (RENDER_API==VALVE_API)
		VPROF_BUDGET( "IsoSurface", "Blobulator" );
	#endif

	#if (SPEED_TEST==1)
		long start_time = clock();
	#endif

	setOffset(offset);
	//debugDrawBounds();
	pCache->beginTile();
	vertexBuffer->beginTile();

	//IMP_PRINT("Cache Size = %d, Edge Size = %d\n", corner_info_size, edge_info_size);
	//corner_info_size = 0;


	//curTime++; // Curtime will wrap around every 1000 seconds... around every 2 minutes.
	//if(curTime == 0) curTime++; // zero is a bad time since that's what we initialize to. We could also clear the cache at wraparound
}

bool HashRenderer::isParticleWithinBounds(ImpParticle* p)
{
	return (p->center[0] > outerBBMins[0] && p->center[0] < outerBBMaxs[0] &&
			p->center[1] > outerBBMins[1] && p->center[1] < outerBBMaxs[1] &&
			p->center[2] > outerBBMins[2] && p->center[2] < outerBBMaxs[2]);

	/*
	// Note: When I'm not in tile mode, I might want the above behavior (which will
	// cut off the surface rather than discarding particles)
	if(tile_mode)
	{

	}
	else
	{
		return (p->center[0] > innerBBMins[0] && p->center[0] < innerBBMaxs[0] &&
				p->center[1] > innerBBMins[1] && p->center[1] < innerBBMaxs[1] &&
				p->center[2] > innerBBMins[2] && p->center[2] < innerBBMaxs[2]);
	}
	*/
}

// Call this to add particles
void HashRenderer::addParticle(ImpParticle* p, bool skipBoundsCheck)
{
	if(particles.size >= MAX_PARTICLES-1)
	{
		IMP_PRINT("imp_addParticle: Trying to add more than MAX_PARTICLES particles.\n");
		return;
	}

	if(skipBoundsCheck || isParticleWithinBounds(p))
	{
		particles.pushAutoSize(p);
		pCache->addParticle(p);
	}
}

// Call this to draw the particles
void HashRenderer::endTile()
{
	#if (RENDER_API==VALVE_API)
		VPROF_BUDGET( "IsoSurface", "Blobulator" );
	#endif

	pCache->buildCache(cubewidth, cutoffR, outerBBMins, outerBBMaxs);

	// Seed the surface
	for(int i = 0; i < particles.size; i++)
	{
		ImpParticle *b = particles[i];

		// This somehow always ends up missing surfaces, even with a count of 10
		//if(b->neighbor_count < 9)
		//{

		//seed_surface(b->center[0], b->center[1], b->center[2]);
		seed_surface(b->center);
		//}
	}

	
	memory_total += (sizeof(CornerInfo*) * slots.size);
	memory_total += (corner_info_cache_used * cornerInfoSize);

	n_tiles++;
	if(n_tiles == 100)
	{
		IMP_PRINT("HashRenderer avg max memory usage = %f\n", float(memory_total) / (100.0f * 1000000.0f));
		n_tiles = 0;
		memory_total = 0;
	}


	particles.size = 0;
	for(int i = 0; i < slots.size; i++)
	{
		slots[i] = NULL;
	}

	while(slots.size < int(float(corner_info_cache.size) * 1.5f))
	{
		slots.pushAutoSize(NULL);
	}
	
	corner_info_cache_used = 0;

	vertexBuffer->endTile();
	pCache->endTile();

	// Print out stats
	#if (SPEED_TEST==1)
		long time_diff = clock() - start_time;
		cum_time_diff += time_diff;
		no_datapoints ++;
		IMP_PRINT("n=%d t=%f\n", no_datapoints, (float)cum_time_diff / (float)no_datapoints);
		//IMP_PRINT("aa=%d, bb=%d, tt=%d, zz=%d\n", aa, bb, tt, zz);
		//zz++;
	#endif
}

void HashRenderer::endFrame()
{
	#if (RENDER_API==VALVE_API)
		VPROF_BUDGET( "IsoSurface", "Blobulator" );
	#endif

	vertexBuffer->endFrame();
}

DOINLINE void DOFASTCALL HashRenderer::seed_surface(Point3D& _center)
{
	// TODO: Rather than calling calc normal, might be able to write a simplified routene for seeding
	// TODO: Could possibly make this faster by calling just_calc_normal and then jumping by
	// one box in the direction of the normal... not sure if this would help

	// find cube corresponding to crawl point
	Point3D center = _center - outerBBMins;
	center.mult(oneOverCubewidth);

	int x = round(center[0]); if(x < 0) x = 0; if(x >= xsize) x = xsize-1;
	int y = round(center[1]); if(y < 0) y = 0; if(y >= ysize) y = ysize-1;
	int z = round(center[2]); if(z < 0) z = 0; if(z >= zsize) z = zsize-1;

	int max_iters = fieldCalcSteps + 2; // the radius of the field + 2 (one to account for possible rounding error, and one to allow steping beyond the field)

	float fx = x*cubewidth + outerBBMins[0];
	float fy = y*cubewidth + outerBBMins[1];
	float fz = z*cubewidth + outerBBMins[2];

	for(int iter = 0; iter < max_iters; iter++)
	{
		CornerInfo* cornerInfo;
		findCorner(x, y, z, &cornerInfo);
		if(cornerInfo->dones & VAL_DONE) break;

		cornerInfo->dones = cornerInfo->dones | VAL_DONE;
		calc_corner(x, y, z, fx, fy, fz, cornerInfo, pCache);		

		if(ISNEG(cornerInfo->value)
			#if (DO_BOUNDS_CHECK == 1)
				|| (x == 0 || y == 0 || z == 0)
			#endif
			)
		{
			docrawl(x, y, z);
			return;
		}
		else
		{
			// Diagonal step is fastest
			x -= 1; y -= 1; z -= 1; 
			fx -= cubewidth; fy -= cubewidth; fz -= cubewidth;
			Assert(x >= 0 && y >= 0 && z >= 0);
		}
	}


#if 0
	// TODO: Rather than calling calc normal, might be able to write a simplified routene for seeding
	// TODO: Could possibly make this faster by calling just_calc_normal and then jumping by
	// one box in the direction of the normal... not sure if this would help

	// find cube corresponding to crawl point
	/*
	x = round(cpx * oneOverCubewidth + halfxsize); if(x < 0) x = 0; if(x >= xsize) x = xsize-1;
	cpx = xToFx[x];

	y = round(cpy * oneOverCubewidth + halfysize); if(y < 0) y = 0; if(y >= ysize) y = ysize-1;
	cpy = yToFy[y];
		
	z = round(cpz * oneOverCubewidth + halfzsize); if(z < 0) z = 0; if(z >= zsize) z = zsize-1;
	cpz = zToFz[z];
	*/

	Point3D center = _center - outerBBMins;
	center.mult(oneOverCubewidth);


	// TODO: This is not quite right... because if we jump out of the cube on the first step after
	// doing the rounding, we need to have dx set correctly so that we can find the cube that the
	// surface is in. Also, start_x should refer to a corner, not a cube, and it should be allowed
	// to go as high as (xsizeP1-1). Same goes for start_y and start_z;
	int start_x = round(center[0]); if(start_x < 0) start_x = 0; if(start_x >= xsize) start_x = xsize-1;
	int start_y = round(center[1]); if(start_y < 0) start_y = 0; if(start_y >= ysize) start_y = ysize-1;
	int start_z = round(center[2]); if(start_z < 0) start_z = 0; if(start_z >= zsize) start_z = zsize-1;

	// NOTE: I use an int for x, because it might have to go negative. cube_index_t can't go negative.

	int n_dirs_taken = 0;
	bool dirs_taken[8] = {false, false, false, false, false, false, false, false};
	int x = start_x, y = start_y, z = start_z;
	int dx = 0, dy = 0, dz = 0;

	// There are 8 directions, and the max length of every two opposing directions
	// is the max of xsize,ysize and zsize. There are four of these. We should never
	// actually hit max_iters unless there's a bug.
	int max_iters = max(max(xsize,ysize),zsize)*4;

	for(int iter = 0; iter < max_iters; iter++)
	{
		CornerInfo* cornerInfo;
		findCorner(x, y, z, &cornerInfo);
		if(cornerInfo->dones & VAL_DONE) break;

		Assert(x >= 0 && x < xsizeP1 &&
			   y >= 0 && y < ysizeP1 &&
			   z >= 0 && z < zsizeP1);

		// TODO: Typically, the first cubevalue that's calculated here never gets reused...
		// so maybe, I should just make a function that simply calculates the normal here.
		float fx = x*cubewidth + outerBBMins[0];
		float fy = y*cubewidth + outerBBMins[1];
		float fz = z*cubewidth + outerBBMins[2];

		cornerInfo->dones = cornerInfo->dones | VAL_DONE;
		calc_corner(x, y, z, fx, fy, fz, cornerInfo, pCache);		

		if(ISNEG(cornerInfo->value))
		{
			//IMP_PRINT("Iter = %d\n", iter);
			int crawl_x = dx>0 ? x-1 : x;
			int crawl_y = dy>0 ? y-1 : y;
			int crawl_z = dz>0 ? z-1 : z;

			Assert(crawl_x >= 0 && crawl_x < xsize &&
				   crawl_y >= 0 && crawl_y < ysize &&
				   crawl_z >= 0 && crawl_z < zsize);

			docrawl(cube_index_t(crawl_x), cube_index_t(crawl_y), cube_index_t(crawl_z));
			return;
		}
		else
		{
			if(iter == 0)
			{
				// After the initial iteration, go in the direction of decreasing field.
				if(cornerInfo->normal[0] > 0.0f) dx = 1; else dx = -1;
				if(cornerInfo->normal[1] > 0.0f) dy = 1; else dy = -1;
				if(cornerInfo->normal[2] > 0.0f) dz = 1; else dz = -1;

				int dir_id = (dx==1?0x4:0) | (dy==1?0x2:0) | (dz==1?0x1:0);
				dirs_taken[dir_id] = true;
				n_dirs_taken ++;
			}

			x += dx; y += dy; z += dz;

			while(x < 0 || x >= xsizeP1 || y < 0 || y >= ysizeP1 || z < 0 || z >= zsizeP1)
			{
				if(n_dirs_taken >= 8)
				{
					IMP_PRINT("Error in Blobulator: Couldn't seed particle. Continuing.\n");
					return;
				}

				// If you hit an edge, try to reverse direction. If we've already
				// done that direction, try any other direction.
				if(x < 0 || x >= xsizeP1) { dx = -dx; }
				else if(y < 0 || y >= ysizeP1) { dy = -dy; }
				else if(z < 0 || z >= zsizeP1) { dz = -dz; }
			
				// See if we can do the reversed direction, and
				// make sure we don't do a direction we've already done.
				int dir_id = (dx==1?0x4:0) | (dy==1?0x2:0) | (dz==1?0x1:0);
				while(dirs_taken[dir_id])
				{
					dir_id = (dir_id+1)%8;
				}
				dirs_taken[dir_id] = true;
				n_dirs_taken ++;

				dx = (dir_id&0x4 ? 1:-1);
				dy = (dir_id&0x2 ? 1:-1);
				dz = (dir_id&0x1 ? 1:-1);

				// Reset back to start point. And take step
				x=start_x; y=start_y; z=start_z;
				x += dx; y += dy; z += dz;
			}
		}
	}
#endif
}

/*
bool HashRenderer::findCorner(const cube_index_t x, const cube_index_t y, const cube_index_t z, CornerInfo** out_corner)
{
	int index = ((int(x) * P1) ^ (int(y) * P2) ^ (int(z) * P3)) % slots.size;

	CornerInfo** slot = &(slots[index]);

	if(*slot != NULL)
	{
		CornerInfo* corner = *slot;
		
		while(corner != NULL)
		{
			if(corner->x == x && corner->y == y && corner->z == z)
			{
				*out_corner = corner;
				return true;
			}
			corner = corner->next;
		}
	}

	*out_corner = NULL;
	return false;
}
*/

#define P1 (73856093u)
#define P2 (19349663u)
#define P3 (83492791u)
#define MYUINT uint


bool HashRenderer::findCorner(const cube_index_t x, const cube_index_t y, const cube_index_t z, CornerInfo** out_corner)
{
	MYUINT index = MYUINT((MYUINT(x) * P1) ^ (MYUINT(y) * P2) ^ (MYUINT(z) * P3)) % slots.size;

	CornerInfo** slot = &(slots[index]);

	if(*slot != 0)
	{
		CornerInfo* corner = *slot;
		
		while(corner != NULL)
		{
			if(corner->x == x && corner->y == y && corner->z == z)
			{
				*out_corner = corner;
				return true;
			}
			corner = corner->next;
		}
	}

	if(corner_info_cache_used == corner_info_cache.size)
	{
		corner_info_cache.pushAutoSize((CornerInfo*)_aligned_malloc(cornerInfoSize,16));
	}
	
	// Allocate a new corner
	CornerInfo* new_corner = corner_info_cache[corner_info_cache_used++];

	new_corner->next = *slot;
	// y and z are usually set by the corner field calc func... not good, really
	/*
	new_corner->normal[0] = 0.0f;
	new_corner->normal[1] = 0.0f;
	new_corner->normal[2] = 0.0f;
	new_corner->interpolants1.clear();
	new_corner->interpolants2.clear();
	new_corner->interpolants3.clear();
	new_corner->interpolants4.clear();
	*/

	new_corner->x = x;
	new_corner->y = y;
	new_corner->z = z;
	new_corner->dones = 0;
	*slot = new_corner;

	*out_corner = new_corner;
	return false;
}


// This performs a breadth-first traversal of voxels on the isosurface
DOINLINE void DOFASTCALL HashRenderer::docrawl(const cube_index_t x, const cube_index_t y, const cube_index_t z)
{
	todo_next_sa.size = 0;
	todo_current_sa.size = 0; // not really necessary, but doesn't hurt.

	CornerInfo* cornerInfo = NULL;
	findCorner(x, y, z, &cornerInfo);
	if(cornerInfo->dones & CUBE_DONE) return;
	cornerInfo->dones = cornerInfo->dones | CUBE_DONE;

	crawl(x, y, z);
	
	while(todo_next_sa.size > 0)
	{
		/*
		XYZ* temp = todo_current;
		todo_current = todo_next;
		todo_next = temp;

		todo_current_size = todo_next_size;
		todo_next_size = 0;
		*/
		todo_current_sa.swapData(todo_next_sa);
		todo_next_sa.size = 0;

		for(int i = 0; i < todo_current_sa.size; i++)
		{
			XYZ* xyz = &(todo_current_sa[i]);
			crawl(xyz->x, xyz->y, xyz->z);
		}
	}
}


// Note: We only force a bounds check when seeding
// if DO_BOUNDS_CHECK is set to 1. calcSign2 will
// also have a forced bounds check in such a case
// which will do appropriate bounds checking on the borders.
#define BOUNDS_CHECK_OFF(a)


// TODO: The MOD operations in this code might be really slow.
// I should check them with optimize it. I should be able to get
// away with just a single mod (anytime remaining size is less than
// equal to eight, we should wrap)

// This function should only be called on cubes that haven't been done.
DOINLINE void DOFASTCALL HashRenderer::crawl(const cube_index_t x, const cube_index_t y, const cube_index_t z)
{
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////// This code retrieves the field values at the corners from cache //////////////////
	/////////////// or calculates them if they are not in cache. ////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////

	CornerInfo* cubeCorners[8];


	// TODO: If a corner is outside of the area that will get polygonized,
	// then we only need to calculate a sign there, not a field value.
	// I can replace calc_corner with another function if we're in a non-rendered area.
	// (However, I need to be careful with corners that are at the boundary between
	// the rendered and non-rendered areas. I need a clever, fast way to compute all of that.
	// The check can be: a) check if you're outside of cube (using code already written).
	// b) if outside of cube, do a second check to see if you're outside the cube by one element.
	bool polygonize_cube = true;
	void DOFASTCALL (*calc_corner_func)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache) = calc_corner;
	if(tile_mode)
	{
		// When tiling, there's no point drawing cubes in the margin
		if(x < marginNCubes || x >= xsize-marginNCubes ||
		   y < marginNCubes || y >= ysize-marginNCubes ||
		   z < marginNCubes || z >= zsize-marginNCubes)
		{
			polygonize_cube = false;
			if(x < marginNCubes-1 || x > xsize-marginNCubes ||
			   y < marginNCubes-1 || y > ysize-marginNCubes ||
			   z < marginNCubes-1 || z > zsize-marginNCubes)
			{
				calc_corner_func = calc_sign2;
			}
		}
	}


	float fx[2];
	float fy[2];
	float fz[2];

	/*
	fx[0] = (float(x) - halfxsize) * cubewidth;
	fy[0] = (float(y) - halfysize) * cubewidth;
	fz[0] = (float(z) - halfzsize) * cubewidth;
	fx[1] = fx[0] + cubewidth;
	fy[1] = fy[0] + cubewidth;
	fz[1] = fz[0] + cubewidth;
	*/

	// TODO: Using these arrays may be hurting performance... might be better
	// to calculate fx, fy and fz as done above.
	/*
	fx[0] = xToFx[x];
	fx[1] = xToFx[x+1];
	fy[0] = yToFy[y];
	fy[1] = yToFy[y+1];
	fz[0] = zToFz[z];
	fz[1] = zToFz[z+1];
	*/

	fx[0] = (float(x) * cubewidth) + outerBBMins[0];
	fy[0] = (float(y) * cubewidth) + outerBBMins[1];
	fz[0] = (float(z) * cubewidth) + outerBBMins[2];
	fx[1] = fx[0] + cubewidth;
	fy[1] = fy[0] + cubewidth;
	fz[1] = fz[0] + cubewidth;
	
	//corner_info_sa.ensureAdditionalCapacity(8);

	int index = 0;

	// When we access the cubes array in binary order 000,001,010, the compiler optimize the code for us... yay =)
	
	// TODO: Accessing cubes could be much faster if I use offsets wisely
	// TODO: I could store the isneg value as a 0/1 inside cubeCorners->cornerinfo...
	// then I would just have to shift a little in the other direction... no ISNEG necessary
	CornerInfo* cornerInfo;

	findCorner(x,y,z,&cornerInfo);
	if(!(cornerInfo->dones & VAL_DONE))
	{
		cornerInfo->dones = cornerInfo->dones | VAL_DONE;
		calc_corner_func(x,y,z,fx[0], fy[0], fz[0], cornerInfo, pCache);
	}
	cubeCorners[0] = cornerInfo;
	index |= ISNEG(cornerInfo->value) >> 31; // LBF
	
	findCorner(x,y,z+1,&cornerInfo);
	if(!(cornerInfo->dones & VAL_DONE))
	{
		cornerInfo->dones = cornerInfo->dones | VAL_DONE;
		calc_corner_func(x,y,z+1,fx[0], fy[0], fz[1], cornerInfo, pCache);
	}
	cubeCorners[1] = cornerInfo;
	index |= ISNEG(cornerInfo->value) >> 30; // LBN

	findCorner(x,y+1,z,&cornerInfo);
	if(!(cornerInfo->dones & VAL_DONE))
	{
		cornerInfo->dones = cornerInfo->dones | VAL_DONE;
		calc_corner_func(x,y+1,z,fx[0], fy[1], fz[0], cornerInfo, pCache);
	}
	cubeCorners[2] = cornerInfo;
	index |= ISNEG(cornerInfo->value) >> 29; // LTF
	
	findCorner(x,y+1,z+1,&cornerInfo);
	if(!(cornerInfo->dones & VAL_DONE))
	{
		cornerInfo->dones = cornerInfo->dones | VAL_DONE;
		calc_corner_func(x,y+1,z+1,fx[0], fy[1], fz[1], cornerInfo, pCache);
	}
	cubeCorners[3] = cornerInfo;
	index |= ISNEG(cornerInfo->value) >> 28; // * LTN;

	findCorner(x+1,y,z,&cornerInfo);
	if(!(cornerInfo->dones & VAL_DONE))
	{
		cornerInfo->dones = cornerInfo->dones | VAL_DONE;
		calc_corner_func(x+1,y,z,fx[1], fy[0], fz[0], cornerInfo, pCache);
	}
	cubeCorners[4] = cornerInfo;
	index |= ISNEG(cornerInfo->value) >> 27; // RBF;

	findCorner(x+1,y,z+1,&cornerInfo);
	if(!(cornerInfo->dones & VAL_DONE))
	{
		cornerInfo->dones = cornerInfo->dones | VAL_DONE;
		calc_corner_func(x+1,y,z+1,fx[1], fy[0], fz[1], cornerInfo, pCache);
	}
	cubeCorners[5] = cornerInfo;
	index |= ISNEG(cornerInfo->value) >> 26; // RBN;

	findCorner(x+1,y+1,z,&cornerInfo);
	if(!(cornerInfo->dones & VAL_DONE))
	{
		cornerInfo->dones = cornerInfo->dones | VAL_DONE;
		calc_corner_func(x+1,y+1,z,fx[1], fy[1], fz[0], cornerInfo, pCache);
	}
	cubeCorners[6] = cornerInfo;
	index |= ISNEG(cornerInfo->value) >> 25; // RTF;

	findCorner(x+1,y+1,z+1,&cornerInfo);
	if(!(cornerInfo->dones & VAL_DONE))
	{
		cornerInfo->dones = cornerInfo->dones | VAL_DONE;
		calc_corner_func(x+1,y+1,z+1,fx[1], fy[1], fz[1], cornerInfo, pCache);
	}
	cubeCorners[7] = cornerInfo;
	index |= ISNEG(cornerInfo->value) >> 24; // RTN;


	#ifdef ERROR_CHECK
	if(dones[x][y][z])
	{
		IMP_PRINT("MAJOR ERROR... in crawl... already done");
		return;
	}

	// error if this cube does not intersect surface
	if(index == 0 || index == 255)
	{
		IMP_PRINT("ERROR: crawl was called on a cube[%d,%d,%d] with no surface intersection (index=%d)\n", x, y, z, index);
	}
	#endif
	

	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////// This code enqueues the neighboring cubes for rendering //////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////

	// mark this cube as completed and set the index
	//cubes[x][y][z].doneTime = curTime;

	// polygonize adjacent cubes
	// I might not need to check for x > 0, x < xsize - 1, etc if I can ensure that
	// particles will never go outside dimensions of rendering cube. ** Or if I put sentinel done values

	// TODO: Rather than checking if the item below, to the left, above, etc is done...
	// When I do a cube, I could mark the cubes around it with below done, above done, etc...
	// Then, I don't have to look at surrounding cubes to see which ones have been done.

	// TODO: It might be faster to run this code at the end, after the polygonization
	// since this code might throw out cubeCorners from the cache. Also, it writes
	// into the todo_next table and it might make sense to write into that right before
	// we read back from that. However, it might be better to do this here because
	// cubes might still be in the cache. Also, above, I access all the cubes. Maybe
	// it would be faster to store a pointer to those cubes rather than re-addressing
	// them again here.

	// TODO: It might be better to use ImpConstTable2.h... with the crawlTable
	// and drawTables together, since the current approach has less memory locality.

	// NOTE: With tiling enabled, we need to follow the surface all the way to the
	// edge of the cache. In order to do this, we need to put in checks here to
	// make sure we don't read past the end of the cache. This could be avoided by
	// increasing the margin to (2*cutoffRadius/cubewidth) voxels, but it would
	// waste a tremendous amount of cache.

	todo_next_sa.ensureAdditionalCapacity(6);

	char adjacentVoxels = crawlTable[index];

	findCorner(x,y,z-1,&cornerInfo);
	if((adjacentVoxels & FAR_FACE) && BOUNDS_CHECK_OFF(z>0 &&) !(cornerInfo->dones & CUBE_DONE))
	{
		cornerInfo->dones = cornerInfo->dones | CUBE_DONE;
		XYZ* xyz = &(todo_next_sa.push());
		xyz->x = x; xyz->y = y; xyz->z = z-1;
	}

	findCorner(x,y-1,z,&cornerInfo);
	if((adjacentVoxels & BOT_FACE) && BOUNDS_CHECK_OFF(y>0 &&) !(cornerInfo->dones & CUBE_DONE))
	{
		cornerInfo->dones = cornerInfo->dones | CUBE_DONE;
		XYZ* xyz = &(todo_next_sa.push());
		xyz->x = x; xyz->y = y-1; xyz->z = z;
	}

	findCorner(x-1,y,z,&cornerInfo);
	if((adjacentVoxels & LEFT_FACE) && BOUNDS_CHECK_OFF(x>0 &&) !(cornerInfo->dones & CUBE_DONE))
	{
		cornerInfo->dones = cornerInfo->dones | CUBE_DONE;
		XYZ* xyz = &(todo_next_sa.push());
		xyz->x = x-1; xyz->y = y; xyz->z = z;
	}

	if((adjacentVoxels & RIGHT_FACE) && BOUNDS_CHECK_OFF(x<(xsize-1) &&) !(cubeCorners[4]->dones & CUBE_DONE))
	{
		cubeCorners[4]->dones = cubeCorners[4]->dones | CUBE_DONE;
		XYZ* xyz = &(todo_next_sa.push());
		xyz->x = x+1; xyz->y = y; xyz->z = z;
	}

	if((adjacentVoxels & TOP_FACE) && BOUNDS_CHECK_OFF(y<(ysize-1) &&) !(cubeCorners[2]->dones & CUBE_DONE))
	{
		cubeCorners[2]->dones = cubeCorners[2]->dones | CUBE_DONE;
		XYZ* xyz = &(todo_next_sa.push());
		xyz->x = x; xyz->y = y+1; xyz->z = z;
	}

	if((adjacentVoxels & NEAR_FACE) && BOUNDS_CHECK_OFF(z<(zsize-1) &&) !(cubeCorners[1]->dones & CUBE_DONE))
	{
		cubeCorners[1]->dones = cubeCorners[1]->dones | CUBE_DONE;
		XYZ* xyz = &(todo_next_sa.push());
		xyz->x = x; xyz->y = y; xyz->z = z+1;
	}

	//////////////////////////////////////////////////////////////////
	///////////// This is the polygonization code ////////////////////
	//////////////////////////////////////////////////////////////////

	if(polygonize_cube)
	{
		vertexBuffer->beginCube(x);

		static const int edgeOffsets[12][6] = {{0,0,0,2,0,1},{0,0,0,1,0,2},{0,0,1,1,1,3},{0,1,0,2,2,3}, {0,0,0,0,0,4},{0,0,1,0,1,5},{0,1,0,0,2,6},{0,1,1,0,3,7}, {1,0,0,2,4,5},{1,0,0,1,4,6},{1,0,1,1,5,7},{1,1,0,2,6,7}};

		int counter, edgeno;

		counter = 0;
		const char *table = (const char*)&(drawTable[index]);

		edgeno = table[counter];
		// TODO: I can make this loop a little smarter if I have a -2 for
		// end of list, and just exit the loop upon seeing that. Right now,
		// I need two -1s in a row before I exit.
		while(edgeno != -1)
		{
			#if (DRAWING_STRIPS==1)
				vertexBuffer->beginTriStrip();
				do
				{
			#endif
					// generate vertex position and normal data
					const int * edgeOffset = edgeOffsets[edgeno];
					const int edgea = edgeOffset[3];
					
					CornerInfo* const corner1 = cubeCorners[edgeOffset[4]];

					vbId_t* const id = &(corner1->edges[edgea]);

					if(vertexBuffer->isIdValid(id))
					{
						vertexBuffer->drawVertex(id);
					}
					else
					{
						vertexBuffer->beginVertex(id);
						const CornerInfo* const corner2 = cubeCorners[edgeOffset[5]];
						calc_vertex(fx[edgeOffset[0]], fy[edgeOffset[1]], fz[edgeOffset[2]], edgea, corner1, corner2, vertexBuffer);
						vertexBuffer->endVertex(id);
					}

			#if (DRAWING_STRIPS==1)
					counter ++;
					edgeno = table[counter];
				} while(edgeno != -1);

				vertexBuffer->endTriStrip();
			#endif

			counter ++;
			edgeno = table[counter];
		}

		vertexBuffer->endCube();
	}
}
