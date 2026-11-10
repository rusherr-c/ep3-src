//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Some comments while developing the code (some of these may not be accurate anymore or even completely obsolete).
// But it explains what has been tested. 

// Previous implementation was using a double-linked list to handle particles.
// For each particle, we scan through Y and Z (in a limited range), and update a double-linked list for each particle in the Y/Z partition.
// Because the particles have been sorted, the double-linked list is sorted too.
// When parsing the tiles, for a given Y/Z, we find the corresponding double linked list, and find the particles that are in the X range.
// The double linked list is costly in term of memory access / usage when writing, and costly when reading too.
// On top of that, it has to go through 2 while loops to find the correct lower bound for the particles sorted on X.
// Every access have to go through the linked list, which potentially creates a lot of cache misses.
// On the other hand, the double linked list is written once, and read several times (in some extent, it is efficient).
// The downside is that it is not free to know for a given tile how many particles are influencing a tile, so we can't use the VMX parallelism
// to its full extent.

// Proposed solution, keep the Y/Z partitioning.
// On X, instead of using a double linked-list, we are going to store the information for each tile on 8 bytes.
//	1 byte for the number of particles influencing a tile.
//  7 bytes for the particle index.
// Because we may not have all particles, there may be some incorrect influence in some cases (would that create holes between tiles?).
// Looking at the distribution of the particles, there is rarely occurrences of 8 or 9 particles influencing a tile.
//	(in most cases, it is 1 to 3 particles).
// And when that happen, there is a big potential for the sign to be positive anyway (i.e. inside the blob).
// We will have to see the side-effect.

// A given cache line (128 bytes) will have 16 tiles in memory. No linked-list, no cache misses when writing / reading contiguously tiles.
// The current range is 100 on X, so 7 cache lines will be necessary if there are particles on the full range.
// This solution is not only very compact in term of memory usage, but it also reduces LHS to the bare minimum, it is cache friendly.
// And the reader can read several particles at the same time for VMX.

// There is more potential for improvements at a later point too.

// After comparing several methods on how to triage particles in XYZ for cube comparison:
// 1) Previous double linked list for each sorted particle on X, put on a YZ slice
//		Good performance overall, but high L2 cache misses and not SPU friendly.
// 2) Dictionary based (x,y,z)
//		Bad performance, some related to being too granular and because of not too efficient hash container.
// 3) Store each particle index per cube (up to 7 particles, 8 bytes total). put on a YZ slice.
//		On par with 1, very low L2 cache misses, more SPU friendly.

// When comparing the memory accesses of these 3 methods, we face this conundrum:
//      To have a fast corner calculation, we will have a slower insertToCache
//		If we have a fast insertToCache (more granular), we will have a slower corner calculation
//		Solution 3 was really following this, much faster corner calculation, but saving were lost by higher cost for insertToCache
// There is not much ways around it.

// So the better solution is to find an algorithm that is:
//	- Overall fast
//	- L2 cache friendly
//	- Easily portable to the SPUs
//  - Potentially flexible on speed versus memory usage

// 3 fits the bill but was not much faster than 1.
// Now here comes the buckets algorithm:
// 4) For each particle, put them in a bucket stored by X/Y/Z (instead of just Y/Z as in 1 and 3).
//		Because a bucket can be memory expensive, X/Y/Z and quantized to a bucket factor (2, 3, 4, ...).
//		The bigger the bucket factor, the less memory used, but more particles will have to be calculated per corner.
//		When looking statistical usage per bucket, even with a factor of 4, half of the buckets are filled with 4 or less particles.
//		In some rare cases, there can be up to 61 particles on a given bucket though.
//		For optimization reasons, buckets can contain up to 60 particles.

// Here are the pros of this method:
//	- Filling the bucket is quite fast (faster than 1, 2 and 3 above).
//	- Calculating the corner is relatively fast (227 cycles instead of 311 cycles for 1. Still slower than 134 that could be achieved with 3).
//  - Low L2 usage compared to 1. A bit higher than 3.
//	- Changing the factor, changes the memory usage and the performance.
//		A factor of 2 offers the sweet spot in term of performance (insert and calculation), with a high relatively memory cost.
//	With just changing corner calculation and insertToCache, we have a 15% performance optimization on the whole blobulator code (which is rather small).
// Note that the corner calculation code has to do some clipping for each particles now, as if a particle that is outside the cube is calculated
// it will make the calculated value incorrect (and the blob will be displayed completely wrong). The clipping costs only 2 cycles per particle.

// Why this code seems superior to the previous version 1 regardless of the meager gains?
//	- It is L2 and SPU friendly. Thus it will be easier to make it parallel or convert it to SPU.
//	- Because a bucket covers multiple cubes (for a factor of 2, it is 2 by 2 by 2 cubes, thus 3 * 3 * 3 corners = 27).
//		And for each bucket, we know the exact number of influencing particles, it would be easier to calculate several corners in parallel.
//		For example, almost 28% of the cubes have only 1 particle influencing. In that case, it is hard to take advantage of the VMX.
//		But when calculating, we could calculate the 4 corners at the same time for a small increase in CPU cycles (not much more cost, and it is the same particle anyway).
//		As such, we could get an additional speed up there. 

// For reference, here is the repartition with up to 8 particles per cube (for a factor of 2):
//	Number of particles			Number of		% of buckets		Sum with previous
//  per bucket					iterations		used				particles
//	[1]							3960897			27.82%				27.82%
//	[2]							2266498			15.92%				43.74%
//	[3]							1571732			11.04%				54.78%
//	[4]							1184466			8.32%				63.10%
//	[5]							945547			6.64%				69.74%
//	[6]							783629			5.50%				75.25%
//	[7]							641807			4.51%				79.75%
//	[8]							543724			3.82%				83.57%

// So 83.57% of the buckets have 8 particles or less. And 43.74% are well under optimized (1 and 2 particles).

// The current bucket factor is 2. Structures have been designed to take advantage of this.
// (for example 8 corners in a bucket, even and odd manipulation with bit masking, etc...).
// If the factor has to be changed, remember to change all the places where it is important.




#include "blobulator/bucket_implementation/cbucket_blob_renderer.h"
#include "blobulator/bucket_implementation/ccorner_info.h"
#include "blobulator/implicit/impparticle.h"
#include "blobulator/util/fasttrunc.h"
#include "blobulator/util/constants.h"
#include "../vertexbuffers/vertexbuffer_inl.h"

#include "tier0/cache_hints.h"
#include "tier0/vprof.h"

#include "../implicit/impconsttable_winding_ccw2_tris.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// A quick way to check if a float is negative.
#define ISNEG(f) (*(int*)(&(f)) & 0x80000000)

extern const int32 ALIGN16 g_SIMD_ComponentMask[4][4] ALIGN16_POST;

namespace Blobulator
{
namespace BucketImplementation
{

Blobulator::BucketImplementation::CBucket ALIGN16 CBucketBlobRenderer::zeroBucket;
Blobulator::BucketImplementation::CornerInfo ALIGN16 CBucketBlobRenderer::zeroCorners[NUM_CORNERINFOS_PER_CORNER_GROUP];

CBucketBlobRenderer::CBucketBlobRenderer()
:
	m_offset(0,0,0),
	n_alloced_slice_corners(0),
	n_alloced_slice_corner_infos(0),
	n_alloced_slice_todo_lists(0),
	m_nMaxNoSlicesToDraw(-1),
	curCacheBucket(0),
	maxNumberOfParticlesPerBucket(0),
	curCacheCornerInfo(0),
	currentCornerIndex(0)
{
	Blobulator::Constants::InitializeConstants();

	m_pVertexBuffer = new VERTEX_BUFFER_CLASS();
	m_pVertexBuffer->init();

	m_nStackSize = 0;

	// TODO: We could optimize this if really necessary (I doubt)
	for (int x = 0 ; x < BUCKET_X ; ++x)
	{
		for (int y = 0 ; y < BUCKET_Y ; ++y)
		{
			for (int z = 0 ; z < BUCKET_Z ; ++z)
			{
				int index = GetIndexFromXYZ(x * BUCKET_FACTOR, y * BUCKET_FACTOR, z * BUCKET_FACTOR);
				bucketsByXYZ[index] = &zeroBucket;
			}
		}
	}

	CheckBucketAllocations(curCacheBucket);

	const int nSizeToAllocate = NUM_CORNERS_GROUP_ALLOCATED * sizeof(CornerInfo) * NUM_CORNERINFOS_PER_CORNER_GROUP;
	m_pAllCorners = (CornerInfo *)(MemAlloc_AllocAligned(nSizeToAllocate, CACHE_LINE_SIZE));
	memset(m_pAllCorners, 0, nSizeToAllocate);

	memset(NumberOfParticlesPerBucketOvertime, 0, sizeof(NumberOfParticlesPerBucketOvertime));

	// We consider that these items are already done, so no test is needed when crawling the surface.
	zeroBucket.SetAllAboveBelowFlags();
	for (int i = 0 ; i < NUM_CORNERINFOS_PER_CORNER_GROUP ; ++i)
	{
		zeroCorners[i].value = -m_fThreshold;
	}
	zeroBucket.m_pCorners = &zeroCorners[0];
}

CBucketBlobRenderer::~CBucketBlobRenderer()
{
	delete m_pVertexBuffer;
	MemAlloc_FreeAligned(m_pAllCorners);
}

void CBucketBlobRenderer::BeginFrame(bool bTileMode, void * pRenderContext)
{
	VPROF_BUDGET( "CBucketBlobRenderer::BeginFrame", "Blobulator" );

	RecalculateConstants();

	m_bTileMode = bTileMode;
	m_pVertexBuffer->beginFrame(pRenderContext);
}

void CBucketBlobRenderer::EndFrame()
{
	VPROF_BUDGET( "CBucketBlobRenderer::EndFrame", "Blobulator" );

	m_pVertexBuffer->endFrame();
}

void CBucketBlobRenderer::BeginTile(const Point3D & offset)
{
	VPROF_BUDGET( "CBucketBlobRenderer::BeginTile", "Blobulator" );

	m_offset = offset;
	RecalculateBoundingBoxes();

	m_pVertexBuffer->beginTile();
}

void CBucketBlobRenderer::EndTile()
{
	VPROF_BUDGET( "CBucketBlobRenderer::EndTile", "Blobulator" );

	BuildCache(m_fCubeWidth, m_fCutoffRadius, m_OuterBBMins, m_OuterBBMaxs);

	// Seed the surface
	for(int i = 0; i < particles.size; i++)
	{
		ImpParticle * pParticle = particles[i];
		SeedSurface(pParticle->center);
	}

	RenderSlices();

	particles.size = 0;
	m_pVertexBuffer->endTile();

	// Once we are doing displaying everything, let's clean for next tile
	particles.size = 0;

	// Clear the 3D array of buckets of the previous values
	for (int i = 0 ; i < curCacheBucket ; ++i)
	{
		CBucket * bucket = cacheBuckets[i];
		if (bucket->m_nNumParticles > maxNumberOfParticlesPerBucket)
		{
			// Monitor how many particles we can have on a given bucket
			maxNumberOfParticlesPerBucket = bucket->m_nNumParticles;
		}
		++NumberOfParticlesPerBucketOvertime[bucket->m_nNumParticles];
		int index = GetIndexFromXYZ(bucket->m_nX * BUCKET_FACTOR, bucket->m_nY * BUCKET_FACTOR, bucket->m_nZ * BUCKET_FACTOR);
		bucketsByXYZ[index] = &zeroBucket;
	}

	// Clear the bucket pointers for each corner (so we know they are free)
	// We will have to optimize this at some point.
	// Actually, we may not need to do this. The buckets are always the same anyway (and added at the end).
	for (int i = 0 ; i < NUM_CORNERS_GROUP_ALLOCATED ; ++i)
	{
		CornerInfo * corner = m_pAllCorners + (i * NUM_CORNERINFOS_PER_CORNER_GROUP);
		corner->bucket = NULL;
	}

	curCacheBucket = 0;
	curCacheCornerInfo = 0;
	currentCornerIndex = 0;

	// We need to clear ahead some items are the prefetch / prezero does not happen on the early items when we recycle
	for (int i = 0 ; i < BUCKETS_PREFETCH_AHEAD ; ++i)
	{
		Construct(cacheBuckets[i]);
	}

	currentCornerIndex = 0;
}

void CBucketBlobRenderer::AddParticle(ImpParticle & particle, bool bSkipBoundsCheck)
{
	if (bSkipBoundsCheck || IsParticleWithinBounds(particle))
	{
		particles.pushAutoSize(&particle);
	}
}

void CBucketBlobRenderer::SetCubeWidth(float fCubeWidth)
{
	m_fCubeWidth = fCubeWidth;
}

float CBucketBlobRenderer::GetCubeWidth() const
{
	return m_fCubeWidth;
}

void CBucketBlobRenderer::SetRenderRadius(float fRenderRadius)
{
	m_RenderRadius = fRenderRadius;
}

float CBucketBlobRenderer::GetRenderRadius() const
{
	return m_RenderRadius;
}

void CBucketBlobRenderer::SetCutoffRadius(float fCutoffRadius)
{
	m_fCutoffRadius = fCutoffRadius;
}

float CBucketBlobRenderer::GetCutoffRadius() const
{
	return m_fCutoffRadius;
}
/*
float CBucketBlobRenderer::GetMarginNCubes() const
{
	return marginNCubes;
}
*/
const Point3D & CBucketBlobRenderer::GetInnerDimensions() const
{
	return m_InnerDimensions;
}

float CBucketBlobRenderer::GetMarginWidth() const
{
	return m_fMarginWidth;
}

void CBucketBlobRenderer::SetDebugMode(bool bDebugMode)
{
	m_bDebugMode = bDebugMode;
}

bool CBucketBlobRenderer::GetDebugMode() const
{
	return m_bDebugMode;
}

int CBucketBlobRenderer::GetNumberOfDebugInfo() const
{
	// Not supported yet
	return 0;
}

void CBucketBlobRenderer::DrawDebugInfo(IVDebugOverlay & debugOverlay, int nDebugInfoIndex)
{
	// Do nothing...
}

void CBucketBlobRenderer::RecalculateConstants()
{
	m_fOneOverCubeWidth = 1.0f / m_fCubeWidth;
	if (m_RenderRadius >= m_fCutoffRadius)
	{
		m_RenderRadius = m_fCutoffRadius;
	}

	// These are derived from the constants above
	m_fCutoffRadiusSq = m_fCutoffRadius * m_fCutoffRadius;
	m_fScaler = 1.0f / (sqrtf(2.0f) * m_fCutoffRadius);
	m_fScalerSq = m_fScaler*m_fScaler;
	float scaledRenderRSq = m_RenderRadius * m_RenderRadius * m_fScalerSq;
	m_fThreshold = 0.25f + scaledRenderRSq * scaledRenderRSq - scaledRenderRSq;
	m_fOneOverThreshold = 1.0f / m_fThreshold;

	m_nFieldCalcSteps = int(m_fCutoffRadius * m_fOneOverCubeWidth);

	float fudgeFactor = 0.001f; // Just in case, so we don't go too small on the marginNCubes, and get killed by rounding error
	m_nMarginNCubes = round_up(m_fCutoffRadius/m_fCubeWidth + fudgeFactor); 
	m_fMarginWidth = m_nMarginNCubes * m_fCubeWidth;

	m_OuterDimensions.set(X_SIZE*m_fCubeWidth, Y_SIZE*m_fCubeWidth, Z_SIZE*m_fCubeWidth);
	m_InnerDimensions.set((X_SIZE-m_nMarginNCubes*2)*m_fCubeWidth, (Y_SIZE-m_nMarginNCubes*2)*m_fCubeWidth, (Z_SIZE-m_nMarginNCubes*2)*m_fCubeWidth);

	// For SIMD operations
	g_f4WOnly = LoadAlignedSIMD( g_SIMD_ComponentMask[3] );

	g_f4Threshold = ReplicateX4(m_fThreshold);
	g_f4Threshold = AndSIMD( g_f4Threshold, g_f4WOnly );

	g_f4Fourpt25s = ReplicateX4( 0.25f );
	g_f4scalerSq = ReplicateX4( m_fScalerSq );
	g_f4One = ReplicateX4( 1.0f );
	g_f4cutoffRSq = ReplicateX4( m_fCutoffRadiusSq);
}

void CBucketBlobRenderer::RecalculateBoundingBoxes()
{
	m_OuterBBMins = m_offset - (m_OuterDimensions * 0.5f);
	m_OuterBBMaxs = m_offset + (m_OuterDimensions * 0.5f);

	m_InnerBBMins = m_offset - (m_InnerDimensions * 0.5f);
	m_InnerBBMaxs = m_offset + (m_InnerDimensions * 0.5f);
}

bool CBucketBlobRenderer::IsParticleWithinBounds(ImpParticle & particle) const
{
	return (particle.center[0] > m_OuterBBMins[0] && particle.center[0] < m_OuterBBMaxs[0] &&
		particle.center[1] > m_OuterBBMins[1] && particle.center[1] < m_OuterBBMaxs[1] &&
		particle.center[2] > m_OuterBBMins[2] && particle.center[2] < m_OuterBBMaxs[2]);
}

void CBucketBlobRenderer::SeedSurface(Point3D & center)
{
	// find cube corresponding to crawl point
	Point3D localCenter = center - m_OuterBBMins;
	localCenter.mult(m_fOneOverCubeWidth);

	// NOTE: ints for x,y and z because it might have to go negative. cube_index_t can't go negative.

	int start_x = round(localCenter[0]); if(start_x < 0) start_x = 0; if(start_x >= X_SIZE) start_x = X_SIZE-1;
	int start_y = round(localCenter[1]); if(start_y < 0) start_y = 0; if(start_y >= Y_SIZE) start_y = Y_SIZE-1;
	int start_z = round(localCenter[2]); if(start_z < 0) start_z = 0; if(start_z >= Z_SIZE) start_z = Z_SIZE-1;
	/*
	// With tiling on, we can't have any field at borders, so we have to clamp closer than border
	int start_x = round(center[0]); if(start_x <= 0) start_x = 1; if(start_x >= xsize) start_x = xsize-1;
	int start_y = round(center[1]); if(start_y <= 0) start_y = 1; if(start_y >= ysize) start_y = ysize-1;
	int start_z = round(center[2]); if(start_z <= 0) start_z = 1; if(start_z >= zsize) start_z = zsize-1;
	*/

	int x = start_x, y = start_y, z = start_z;

	// There are 8 directions, and the max length of every two opposing directions
	// is the max of xsize,ysize and zsize. There are four of these. We should never
	// actually hit max_iters unless there's a bug.
	//int max_iters = max(max(xsize,ysize),zsize)*4;

	// TODO: Take the particle's scaling of the radius into account
	int max_iters = m_nFieldCalcSteps + 2; // the radius of the field + 2 (one to account for possible rounding error, and one to allow stepping beyond the field)
	
	int direction = -1;
#if (SEED_HORIZONTALLY)
	if (y < max_iters)
#else
	if (x < max_iters)
#endif
	{
		// Don't want to go in negative numbers if the number is too small
		direction = +1;
	}

	for(int iter = 0; iter < max_iters; iter++)
	{
		Assert(x >= 0 && x < X_SIZE_P1 &&
			   y >= 0 && y < Y_SIZE_P1 &&
			   z >= 0 && z < Z_SIZE_P1);
		
		#if (SEED_MULTISLICE == 1)
			YZ todo = {y, z};
			slices[x+1].try_seed_list.pushAutoSize(todo); // The +1 here is very important... because there is an extra slice below slice 0.
			x -= 1;
			if(x < 0) break;
		#elif (SEED_SIDEWAYS == 1)
			YZ todo = {y, z};
			slices[x+1].try_seed_list.pushAutoSize(todo);
			break;
		#else
			float fx = x*m_fCubeWidth + m_OuterBBMins[0];
			float fy = y*m_fCubeWidth + m_OuterBBMins[1];
			float fz = z*m_fCubeWidth + m_OuterBBMins[2];

			if (!CalcSign(x, y, z, fx, fy, fz)
				#if (DO_BOUNDS_CHECK == 1)
					|| x == 0 || y == 0 || z == 0
				#endif
				)
			{
				YZ todo = YZ_SetYZ(y, z);
				slices[x+1].seed_list.pushAutoSize(todo); // The +1 here is very important... because there is an extra slice below slice 0.
				return;
			}
			else
			{
				#if (SEED_HORIZONTALLY == 1)
					y += direction;
					Assert((y >= 0) && (y < Y_SIZE_P1));
				#else
					x += direction;
					Assert((x >= 0) && (x < X_SIZE_P1));
				#endif
			}
		#endif
	}
}

void CBucketBlobRenderer::AllocSliceTodoList(Slice_t * slice)
{
	if(slice->todo_list.capacity == 0)
	{
		if(unused_todo_lists.size > 0)
		{
			slice->todo_list = unused_todo_lists.pop();
		}
		else
		{
			slice->todo_list.resize(16);
			n_alloced_slice_todo_lists ++;
			IMP_PRINT("Allocated %d todo lists\n", n_alloced_slice_todo_lists);
		}
	}
}

void CBucketBlobRenderer::DeallocSliceTodoList(Slice_t * slice)
{
	Assert(slice->todo_list.capacity >= 0);

	unused_todo_lists.pushAutoSize(slice->todo_list);
	slice->todo_list.size = 0;
	slice->todo_list.capacity = 0;
	slice->todo_list.a = NULL;

	//unused_todo_lists.pushAutoSize(slice->todo_list);
	//slice->todo_list = NULL;
}

void CBucketBlobRenderer::RenderSlices()
{
	int noSlicesDrawn = 0;
	m_bPolygonizationEnabled = true;

	Slice_t* pPrevSlice;
	Slice_t* pCurrentSlice;
	Slice_t* pNextSlice;

	int numIterations = 0;
	for(int x = 0; x < X_SIZE; x++, numIterations++)
	{
		pCurrentSlice = &(slices[x+1]);

		if ( numIterations >= 4*X_SIZE )
		{
			// FIXME: diagnose why this happens! (seems to happen with non-unit ImpParticle::scale/fieldRScaleSq)
			Warning( "ERROR: SweepRenderer::render_slices got into an infinite loop!\n" );
			Assert( 0 );
			break;
		}

		if(pCurrentSlice->todo_list.size > 0 || pCurrentSlice->seed_list.size > 0
			#if (SEED_MULTISLICE == 1 || SEED_SIDEWAYS == 1)
				|| pCurrentSlice->try_seed_list.size > 0
			#endif
			)
		{
			if(noSlicesDrawn == m_nMaxNoSlicesToDraw)
			{
				m_bPolygonizationEnabled = false;
			}
			if(m_bPolygonizationEnabled)
			{
				m_nLastSliceDrawn = x;
			}
			noSlicesDrawn ++;

			// Make sure this and next slice have
			// corner arrays allocated
			pPrevSlice = &(slices[x]);
			pNextSlice = &(slices[x+2]);

			AllocSliceTodoList(pPrevSlice);
			// Wrong: We don't allocate a todo list for the current slice because the current slice uses the recursion stack!
			AllocSliceTodoList(pCurrentSlice);
			AllocSliceTodoList(pNextSlice);

			// Corners for this slice are on pCurrentSlice and pNextSlice
			// TODOs for prev slice are on pPrevSlice
			// TODOs for this slice are on pCurrentSlice
			// TODOs for next slice are on pNextSlice
			RenderSlice(x, pPrevSlice, pCurrentSlice, pNextSlice);

			//Now, everything is done on this slice. If we have no todos above or below, we can delete this slice.
			//If the slice above is done, and has no todos above it, we can delete it's corner data.
			//If the slice below is done, and has no todos below it, we can delete it's corner data.

			bool pPrevSlice_done = (pPrevSlice->todo_list.size==0 && pPrevSlice->seed_list.size==0);
			Assert(pCurrentSlice->todo_list.size==0 && pCurrentSlice->seed_list.size==0); // pCurrentSlice_done
			bool pNextSlice_done = (pNextSlice->todo_list.size==0 && pNextSlice->seed_list.size==0);

			#if (SEED_MULTISLICE == 1 || SEED_SIDEWAYS == 1)
				pPrevSlice_done = pPrevSlice_done && pPrevSlice->try_seed_list.size == 0;
				Assert(pCurrentSlice->try_seed_list.size == 0);
				pNextSlice_done = pNextSlice_done && pNextSlice->try_seed_list.size == 0;
			#endif

			DeallocSliceTodoList(pCurrentSlice);

			if(pPrevSlice_done)
			{
				DeallocSliceTodoList(pPrevSlice);
			}

			if(pNextSlice_done)
			{
				DeallocSliceTodoList(pNextSlice);
			}

			// If on previous slice, there is something to do do that.
			// Otherwise, go to next slice.

			if(!pPrevSlice_done && x>0)
			{
				// Go down if there is work to do on previous slice!
				x -= 2;
			}
		}
	}
}

void CBucketBlobRenderer::RenderSlice(const int x, Slice_t * pPrevSlice, Slice_t * pCurrentSlice, Slice_t * pNextSlice)
{
	// Use a single counter instead of 2 variables
	YZ * pStack = m_nStack + m_nStackSize;

	fltx4 f4CubeWidth = ReplicateX4( &m_fCubeWidth );

	int x1 = x + 1;
	while(true)
	{
		YZ temp;
		if(pCurrentSlice->todo_list.size > 0)
		{
			temp = pCurrentSlice->todo_list.pop();
		}
		else if(pCurrentSlice->seed_list.size > 0)
		{
			temp = pCurrentSlice->seed_list.pop();
		}
		else
		{
			break;
		}

		// Original code:
		//if(pCurrentSlice->corners[temp.y][temp.z].doneAbove)
		//{
		//	continue;
		//}
		//pCurrentSlice->corners[temp.y][temp.z].doneAbove = true;
		//pNextSlice->corners[temp.y][temp.z].doneBelow = true;

		int y = YZ_GetY(temp);
		int z = YZ_GetZ(temp);

		{
			// Make sure that we are in the range for +1 operations.
			Assert((x >= 0) && (x < X_SIZE));
			Assert((y >= 0) && (y < Y_SIZE));
			Assert((z >= 0) && (z < Z_SIZE));

			int indexXYZ = GetIndexFromXYZ(x, y, z);
			CBucket * bucketXYZ = bucketsByXYZ[indexXYZ];
			if (bucketXYZ == &zeroBucket)
			{
				// No bucket, it means no particle. No need to continue further with this cube.
				continue;
			}
			int offsetXYZ = GetCornerIndexFromXYZ(x, y, z);
			if (bucketXYZ->IsDoneAbove(offsetXYZ))
			{
				continue;
			}

			bucketXYZ->SetDoneAbove(offsetXYZ);

			int indexXp1YZ = GetIndexFromXYZ(x + 1, y, z);
			CBucket * bucketXp1YZ = bucketsByXYZ[indexXp1YZ];
			int offsetXp1YZ = offsetXYZ ^ CBucket::X_FLAG;
			bucketXp1YZ->SetDoneBelow(offsetXp1YZ);
		}

		for (;;)
		{		
			/////////////////////////////////////////////////////////////////////////////////////////////////
			/////////////// This code retrieves the field values at the corners from cache //////////////////
			/////////////// or calculates them if they are not in cache. ////////////////////////////////////
			/////////////////////////////////////////////////////////////////////////////////////////////////

			// The code has been changed to remove the mandatory LHS in the first iteration.
			// It was pushing on the stack then retrieving the same value just after.
			// The value is now popped at the end of the loop (when we finished this iteration).
			const int y1 = y + 1;
			const int z1 = z + 1;

			// Make sure that we are in the range for +1 operations.
			Assert((y >= 0) && (y1 <= Y_SIZE));
			Assert((z >= 0) && (z1 <= Z_SIZE));

			// TODO: If a corner is outside of the area that will get polygonized,
			// then we only need to calculate a sign there, not a field value.
			// I can replace calc_corner with another function if we're in a non-rendered area.
			// (However, I need to be careful with corners that are at the boundary between
			// the rendered and non-rendered areas. I need a clever, fast way to compute all of that.
			// The check can be: a) check if you're outside of cube (using code already written).
			// b) if outside of cube, do a second check to see if you're outside the cube by one element.
			bool polygonize_cube = m_bPolygonizationEnabled;
			if (m_bTileMode)
			{
				// When tiling, there's no point drawing cubes in the margin
				if(x < m_nMarginNCubes || x >= X_SIZE-m_nMarginNCubes ||
					y < m_nMarginNCubes || y >= Y_SIZE-m_nMarginNCubes ||
					z < m_nMarginNCubes || z >= Z_SIZE-m_nMarginNCubes)
				{
					polygonize_cube = false;
				}
			}

			// TODO: I can store these in arrays and use lookups (as I did before)
			// Note: The above can be calculated with three mmx instructions:
			// initialize [fy0,fy1,fz0,fz1] = ((y,y+1,z,z+1)*cubewidth + (outerBBMins[1],outerBBMins[1],outerBBMins[2],outerBBMins[2])
			// or         [fy0,fy1,fz0,fz1] = ((y,y,z,z)*cubewidth + (outerBBMins[1],outerBBMins[1]+cw,outerBBMins[2],outerBBMins[2]+cw)

			CornerInfo* cubeCorners[8];

			// With the buckets, we guarantee that any particle influencing a bucket will be attached to the bucket.
			// This is valid for all corners too. Because a bucket is at least a factor of 2, it has 2 consecutive cubes on each directions.
			// Thus x and x + 1 are in the same bucket when x is even, same for y and z.
			// As buckets have the full influence stored, we can calculate the 8 corners with the same bucket.
			// Thus instead of calculating with x and x + 1, y and y + 1, z and z + 1, we only calculate with x, y and z.
			// That way we are using the same bucket in this function, thus reducing the cache misses.

			// To cache or not cache 8 corners at a time?
			//	PROS:
			//		- A corner is calculated only once and potentially reused up to 8 times.
			//			So the calculation is potentially up to 8 times faster than without caching.
			//			We may calculate corners that are not necessary.
			//			Assuming that we have 50% unused corners, it still makes the calculation 4 times faster with cache.
			//		- It avoids to have the full list of particles on each corners.
			//			I.e. each bucket has its set of particles (for 8 corners, still a quick calculation).
			//	CONS:
			//		- Increased memory usage and cache misses.
			//			We use a circular buffer to reduce that.
			//		- Need 8 tests and branches to calculate the corners.
			//			We don't calculate all, only when we crawl the surface.

			// How could we reduce the 8 tests:
			//	There are 8 potential combinations, pretty much mapped with the value of offsetXYZ.
			//	XYZ			(0)	- 1 bucket used
			//	XYZp1		(1)	- 2 buckets used
			//	XYp1Z		(2)	- 2 buckets used
			//	XYp1Zp1		(3)	- 4 buckets used
			//	Xp1YZ		(4)	- 2 buckets used
			//	Xp1YZp1		(5) - 4 buckets used
			//  Xp1Yp1Z		(6)	- 4 buckets used
			//	Xp1Yp1Zp1	(7) - 8 buckets used
			//					  27 buckets used when using each combination once.

			// If we were doing a switch on offsetXYZ, we could have an average of 1+3.375 tests  (27/8 = 3.375)
			// It would pretty much reduce the number of tests by 2.
			// It would also allow to bake in the buckets and offsets (thus making the generated code more optimal).
			// To reduce the code even further, instead of having to read 8 bytes to get the sign of each corners,
			// we could have one byte storing the value of each corner.
			// With the 8 combinations above, this would make the sign calculation more optimal.


			// 4 mults here
			int indexX = GetIndexFromX(x);
			int indexXp1 = GetIndexFromX(x1);
			int indexY = GetIndexFromY(y);
			int indexYp1 = GetIndexFromY(y1);
			int indexZ = GetIndexFromZ(z);
			int indexZp1 = GetIndexFromZ(z1);

			// Then just a bunch of additions
			int indexXYZ = indexX + indexY + indexZ;
			int indexXYZp1 = indexX + indexY + indexZp1;
			int indexXYp1Z = indexX + indexYp1 + indexZ;
			int indexXYp1Zp1 = indexX + indexYp1 + indexZp1;
			int indexXp1YZ = indexXp1 + indexY + indexZ;
			int indexXp1YZp1 = indexXp1 + indexY + indexZp1;
			int indexXp1Yp1Z = indexXp1 + indexYp1 + indexZ;
			int indexXp1Yp1Zp1 = indexXp1 + indexYp1 + indexZp1;

			// Get the corresponding bucket for each corner (may or may not be the same depending of the alignment).
			// We could optimize this with a cache (each bucket being 256 bytes). 
			CBucket * bucketXYZ = bucketsByXYZ[indexXYZ];
			CBucket * bucketXYZp1 = bucketsByXYZ[indexXYZp1];
			CBucket * bucketXYp1Z = bucketsByXYZ[indexXYp1Z];
			CBucket * bucketXYp1Zp1 = bucketsByXYZ[indexXYp1Zp1];
			CBucket * bucketXp1YZ = bucketsByXYZ[indexXp1YZ];
			CBucket * bucketXp1YZp1 = bucketsByXYZ[indexXp1YZp1];
			CBucket * bucketXp1Yp1Z = bucketsByXYZ[indexXp1Yp1Z];
			CBucket * bucketXp1Yp1Zp1 = bucketsByXYZ[indexXp1Yp1Zp1];

			// Get the index in each corner (depending of the odd / even bit of each X/Y/Z).
			// Note that depending if X/Y/Z are even/odd, there will be a corresponding combination for index0...index7
			// Calculate the first index, then all the others are just a simple bit change from the base
			int offsetXYZ = GetCornerIndexFromXYZ(x, y, z);
			int offsetXYZp1 = offsetXYZ ^ CBucket::Z_FLAG;
			int offsetXYp1Z = offsetXYZ ^ CBucket::Y_FLAG;
			int offsetXYp1Zp1 = offsetXYZ ^ (CBucket::Y_FLAG | CBucket::Z_FLAG);
			int offsetXp1YZ = offsetXYZ ^ CBucket::X_FLAG;
			int offsetXp1YZp1 = offsetXYZ ^ (CBucket::X_FLAG | CBucket::Z_FLAG);
			int offsetXp1Yp1Z = offsetXYZ ^ (CBucket::X_FLAG | CBucket::Y_FLAG);
			int offsetXp1Yp1Zp1 = offsetXYZ ^ (CBucket::X_FLAG | CBucket::Y_FLAG | CBucket::Z_FLAG);

			// Original code, for x,y,z|x,y,z1|x,y1,z|x,y1,z1|x1,y,z|x1,y,z1|x1,y1,z|x1,y1,z1
			//	CubeInfo* const cube0 = &(cur_cubes[y][z]);
			//	if(cube0->cornerInfoNo == 0)
			//	{
			//		cube0->cornerInfoNo = cur_corner_info_no;
			//		CornerInfo* cornerInfo = (CornerInfo*)&(cur_corner_info[cur_corner_info_no]);
			//		cur_corner_info_no++;
			//		calc_corner_func( f4Position, x,y,z, cornerInfo, pCache);
			//	}
			//	cubeCorners[0] = (CornerInfo*)&(cur_corner_info[cube0->cornerInfoNo]);
			//	index |= ISNEG(cubeCorners[0]->value) >> 31; // LBF

			// TODO: Change the initialized value for the array of buckets so NULL is not accepted.
			//	This will avoid doing this if test.
			LazyCalculate(bucketXYZ);
			LazyCalculate(bucketXYZp1);
			LazyCalculate(bucketXYp1Z);
			LazyCalculate(bucketXYp1Zp1);
			LazyCalculate(bucketXp1YZ);
			LazyCalculate(bucketXp1YZp1);
			LazyCalculate(bucketXp1Yp1Z);
			LazyCalculate(bucketXp1Yp1Zp1);

			int index;
			{
				// Use {} to try to help the compiler reduce the register pressure, and reduce LHS
				CornerInfo * localCorner;
				localCorner = &bucketXYZ->m_pCorners[offsetXYZ];
				index = ISNEG(localCorner->value) >> 31;
				cubeCorners[0] = localCorner;

				localCorner = &bucketXYZp1->m_pCorners[offsetXYZp1];
				index |= ISNEG(localCorner->value) >> 30;
				cubeCorners[1] = localCorner;

				localCorner = &bucketXYp1Z->m_pCorners[offsetXYp1Z];
				index |= ISNEG(localCorner->value) >> 29;
				cubeCorners[2] = localCorner;

				localCorner = &bucketXYp1Zp1->m_pCorners[offsetXYp1Zp1];
				index |= ISNEG(localCorner->value) >> 28;
				cubeCorners[3] = localCorner;

				localCorner = &bucketXp1YZ->m_pCorners[offsetXp1YZ];
				index |= ISNEG(localCorner->value) >> 27;
				cubeCorners[4] = localCorner;

				localCorner = &bucketXp1YZp1->m_pCorners[offsetXp1YZp1];
				index |= ISNEG(localCorner->value) >> 26;
				cubeCorners[5] = localCorner;

				localCorner = &bucketXp1Yp1Z->m_pCorners[offsetXp1Yp1Z];
				index |= ISNEG(localCorner->value) >> 25;
				cubeCorners[6] = localCorner;

				localCorner = &bucketXp1Yp1Zp1->m_pCorners[offsetXp1Yp1Zp1];
				index |= ISNEG(localCorner->value) >> 24;
				cubeCorners[7] = localCorner;
			}

#ifdef ERROR_CHECK
			if(cur_cubes[y][z].done == false)
			{
				IMP_PRINT("MAJOR ERROR... in render_slice... already done");
				return;
			}

			// error if this cube does not intersect surface
			if(index == 0 || index == 255)
			{
				IMP_PRINT("ERROR: render_slice was called on a cube[%d,%d,%d] with no surface intersection (index=%d)\n", x, y, z, index);
			}
#endif

			/////////////////////////////////////////////////////////////////////////////////////////////////
			/////////////////////// This code enqueues the neighboring cubes for rendering //////////////////
			/////////////////////////////////////////////////////////////////////////////////////////////////

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

			char adjacentVoxels = crawlTable[index];
			// Original code
			//	if((adjacentVoxels & LEFT_FACE) && BOUNDS_CHECK_OFF(x>0 &&)  cur_cubes[y][z].doneBelow == false)
			//	{
			//		YZ yz = {y, z};
			//		pPrevSlice->todo_list.pushAutoSize(yz);
			//	}
			if (adjacentVoxels & LEFT_FACE)
			{
				// There is a bucket, so there is something that we can do
				if (bucketXYZ->IsDoneBelow(offsetXYZ) == false)
				{
					YZ yz = YZ_SetYZ(y, z);
					pPrevSlice->todo_list.pushAutoSize(yz);
				}
			}

			//	if((adjacentVoxels & RIGHT_FACE) && BOUNDS_CHECK_OFF(x<(xsize-1) &&) next_cubes[y][z].doneAbove == false)
			//	{
			//		YZ yz = {y, z};
			//		pNextSlice->todo_list.pushAutoSize(yz);
			//	}
			if (adjacentVoxels & RIGHT_FACE)
			{
				// There is a bucket, so there is something that we can do
				if (bucketXp1YZ->IsDoneAbove(offsetXp1YZ) == false)
				{
					YZ yz = YZ_SetYZ(y, z);
					pNextSlice->todo_list.pushAutoSize(yz);
				}
			}

			// In the bucket 3D array, X is the biggest array offset, then Y, then Z.
			// So make sure that we go forward / backward with Z first, then Y then finally X (which we do already).
			// Thus nStack[nStackSize++] should be pushed last with Z + 1 / Z - 1 operations, with Y first.

			//	if((adjacentVoxels & BOT_FACE) && BOUNDS_CHECK_OFF(y>0 &&) cur_cubes[y-1][z].doneAbove == false)
			//	{
			//		cur_cubes[y-1][z].doneAbove = true;
			//		next_cubes[y-1][z].doneBelow = true;
			//		nStack[nStackSize++] = y-1;
			//		nStack[nStackSize++] = z;
			//	}
			if (adjacentVoxels & BOT_FACE)
			{
				// Make sure we have enough place for -1 operations
				Assert(y > 0);

				// Instead of recalculating the full index, use previously calculated value to reduce multiplications and use one constant instead
				int indexYm1 = indexYp1 - GetIndexFromY(2);
				int indexXYm1Z = indexX + indexYm1 + indexZ;
				CBucket * bucketXYm1Z = bucketsByXYZ[indexXYm1Z];
				// There is a bucket, so there is something that we can do
				int offsetXYm1Z = offsetXYp1Z;	// +1 and -1 have the same offset
				if (bucketXYm1Z->IsDoneAbove(offsetXYm1Z) == false)
				{
					bucketXYm1Z->SetDoneAbove(offsetXYm1Z);

					// Same for this index
					int indexXp1Ym1Z = indexXp1 + indexYm1 + indexZ;
					CBucket * bucketXp1Ym1Z = bucketsByXYZ[indexXp1Ym1Z];
					int offsetXp1Ym1Z = offsetXp1Yp1Z;	// +1 and -1 have the same offset
					bucketXp1Ym1Z->SetDoneBelow(offsetXp1Ym1Z);

					*pStack++ = YZ_SetYZ(y - 1, z);
				}
			}

			//	if((adjacentVoxels & TOP_FACE) && BOUNDS_CHECK_OFF(y<(ysize-1) &&) cur_cubes[y+1][z].doneAbove == false)
			//	{
			//		cur_cubes[y1][z].doneAbove = true;
			//		next_cubes[y1][z].doneBelow = true;
			//		nStack[nStackSize++] = y1;
			//		nStack[nStackSize++] = z;
			//	}
			if (adjacentVoxels & TOP_FACE)
			{
				// There is a bucket, so there is something that we can do
				if (bucketXYp1Z->IsDoneAbove(offsetXYp1Z) == false)
				{
					bucketXYp1Z->SetDoneAbove(offsetXYp1Z);
					bucketXp1Yp1Z->SetDoneBelow(offsetXp1Yp1Z);

					*pStack++ = YZ_SetYZ(y + 1, z);
				}
			}

			//	if((adjacentVoxels & FAR_FACE) && BOUNDS_CHECK_OFF(z>0 &&) cur_cubes[y][z-1].doneAbove == false)
			//	{
			//		cur_cubes[y][z-1].doneAbove = true;
			//		next_cubes[y][z-1].doneBelow = true;
			//		nStack[nStackSize++] = y;
			//		nStack[nStackSize++] = z-1;
			//	}
			if (adjacentVoxels & FAR_FACE)
			{
				// Make sure we have enough place for -1 operations
				Assert(z > 0);

				// Instead of recalculating the full index, use previously calculated value to reduce multiplications and use one constant instead
				int indexZm1 = indexZp1 - GetIndexFromZ(2);
				int indexXYZm1 = indexX + indexY + indexZm1;
				CBucket * bucketXYZm1 = bucketsByXYZ[indexXYZm1];
				// There is a bucket, so there is something that we can do
				int offsetXYZm1 = offsetXYZp1;	// +1 and -1 have the same offset
				if (bucketXYZm1->IsDoneAbove(offsetXYZm1) == false)
				{
					bucketXYZm1->SetDoneAbove(offsetXYZm1);

					// Same for this index
					int indexXp1YZm1 = indexXp1 + indexY + indexZm1;
					CBucket * bucketXp1YZm1 = bucketsByXYZ[indexXp1YZm1];
					int offsetXp1YZm1 = offsetXp1YZp1;	// +1 and -1 have the same offset
					bucketXp1YZm1->SetDoneBelow(offsetXp1YZm1);

					*pStack++ = YZ_SetYZ(y, z - 1);
				}
			}

			//	if((adjacentVoxels & NEAR_FACE) && BOUNDS_CHECK_OFF(z<(zsize-1) &&) cur_cubes[y][z+1].doneAbove == false)
			//	{
			//		cur_cubes[y][z1].doneAbove = true;
			//		next_cubes[y][z1].doneBelow = true;
			//		nStack[nStackSize++] = y;
			//		nStack[nStackSize++] = z1;
			//	}
			if (adjacentVoxels & NEAR_FACE)
			{
				// There is a bucket, so there is something that we can do
				if (bucketXYZp1->IsDoneAbove(offsetXYZp1) == false)
				{
					bucketXYZp1->SetDoneAbove(offsetXYZp1);
					bucketXp1YZp1->SetDoneBelow(offsetXp1YZp1);

					*pStack++ = YZ_SetYZ(y, z + 1);
				}
			}

			//////////////////////////////////////////////////////////////////
			///////////// This is the polygonization code ////////////////////
			//////////////////////////////////////////////////////////////////

			if (polygonize_cube)
			{
				fltx4 f4X = ReplicateX4(&Constants::s_flByteToFloat[x]);
				fltx4 f4Y = ReplicateX4(&Constants::s_flByteToFloat[y]);
				fltx4 f4Z = ReplicateX4(&Constants::s_flByteToFloat[z]);
				fltx4 f4Position = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_100Mask), f4X, f4Y);		// X | Y | Y | Y
				f4Position = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_110Mask), f4Position, f4Z);	// X | Y | Z | Z
				f4Position = MaddSIMD(f4CubeWidth, f4Position, m_OuterBBMins.sse_vec);			// pos * cubewidth + outerBBMins
				fltx4 f4Position1 = AddSIMD( f4Position, f4CubeWidth );

				// This is slow, optimize this...
				float fx[2], fy[2], fz[2];
				fx[0] = SubFloat( f4Position, 0 ); fx[1] = SubFloat( f4Position1, 0 );
				fy[0] = SubFloat( f4Position, 1 ); fy[1] = SubFloat( f4Position1, 1 );
				fz[0] = SubFloat( f4Position, 2 ); fz[1] = SubFloat( f4Position1, 2 );
				m_pVertexBuffer->beginCube(x);

				static const int edgeOffsets[12][6] = {{0,0,0,2,0,1},{0,0,0,1,0,2},{0,0,1,1,1,3},{0,1,0,2,2,3}, {0,0,0,0,0,4},{0,0,1,0,1,5},{0,1,0,0,2,6},{0,1,1,0,3,7}, {1,0,0,2,4,5},{1,0,0,1,4,6},{1,0,1,1,5,7},{1,1,0,2,6,7}};
				const char * const table = (const char*)&(drawTable[index]);

				// TODO: I can make this loop a little smarter if I have a -2 for
				// end of list, and just exit the loop upon seeing that. Right now,
				// I need two -1s in a row before I exit.
				int counter = 0;
				int edgeno = table[counter];
				while(edgeno != -1)
				{
					// generate vertex position and normal data
					const int *edgeOffset = edgeOffsets[edgeno];
					const int edgea = edgeOffset[3];

					CornerInfo* const corner1 = cubeCorners[edgeOffset[4]];

					vbId_t * const id = &(corner1->edges[edgea]);

					if (m_pVertexBuffer->isIdValid(id))
					{
						m_pVertexBuffer->drawVertex(id);
					}
					else
					{
						m_pVertexBuffer->beginVertex(id);
						const CornerInfo* const corner2 = cubeCorners[edgeOffset[5]];
						CalcVertex(fx[edgeOffset[0]], fy[edgeOffset[1]], fz[edgeOffset[2]], edgea, corner1, corner2);
						m_pVertexBuffer->endVertex(id);
					}

					counter ++;
					edgeno = table[counter];
				}

				m_pVertexBuffer->endCube();
			}

			if (pStack - m_nStack <= 0)
			{
				// Nothing in the stack to unpop
				break;
			}
			const YZ yz = *(--pStack);
			y = YZ_GetY(yz);
			z = YZ_GetZ(yz);
		}
	}

	m_nStackSize = pStack - m_nStack;
}

bool CBucketBlobRenderer::CalcSign(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z)
{
	float totalf = 0.0f;

	CBucket * bucket = GetBucketFromXYZ(ix, iy, iz);
	int numParticles = bucket->m_nNumParticles;
	for (int i = 0 ; i < numParticles ; ++i)
	{
		ImpParticle * p = particles[bucket->m_ParticlesIndex[i]];
		float *c = p->center.p;

		// This is the code without assembly
		float diff_x = x - c[0];
		float diff_y = y - c[1];
		float rSq = diff_x * diff_x;
		float diff_z = z - c[2];
		rSq += diff_y * diff_y;
		rSq += diff_z * diff_z;
		rSq *= p->GetFieldRScaleSq();  // TODO: The fieldRScaleSq should incorporate scalerSq (can compare rSq with one over sqrt(2) instead of cutoffRSq below)

		if(rSq <= m_fCutoffRadiusSq)
		{	
			rSq *= m_fScalerSq;
			float f = 0.25f + rSq*rSq - rSq;
			totalf += f;

			if(totalf > m_fThreshold) return true;
		}
	}
	return false;
}

void CBucketBlobRenderer::CheckBucketAllocations(const int localCurCacheBucket)
{
	int freeBuckets = cacheBuckets.size - localCurCacheBucket;
	// Always allocate more than the max, so we can pre-fetch without further testing
	while (freeBuckets < BUCKETS_ALLOCATED_AHEAD)
	{
		// Not enough allocations done ahead of time, we have to create new ones

		// Several allocations at the same time to reduce the overhead of the memory alignment.
		static const int NUMBER_OF_INSTANCES_IN_ONE_ALLOCATION = 16;
		void * allocation = MemAlloc_AllocAligned(NUMBER_OF_INSTANCES_IN_ONE_ALLOCATION * sizeof(CBucket), CACHE_LINE_SIZE);
		CBucket * bucket = static_cast<CBucket *>(allocation);

		for (int i = 0 ; i < NUMBER_OF_INSTANCES_IN_ONE_ALLOCATION ; ++i)
		{
			Construct(bucket);
			cacheBuckets.pushAutoSize(bucket);
			++bucket;
		}
		++freeBuckets;
	}
}

void CBucketBlobRenderer::FillBuckets(int particleIndex, int minX, int maxX, int minY, int maxY, int minZ, int maxZ)
{
	// Use "register" in a desperate attempt to avoid some LHS on X360 because of too many local variables.
	// I don't think the compiler uses it anymore, but never know... :)
	register int localCurCacheBucket = curCacheBucket;

	int indexBucketMinX = minX / BUCKET_FACTOR;
	int indexBucketMinY = minY / BUCKET_FACTOR;
	int indexBucketMinZ = minZ / BUCKET_FACTOR;
	int indexBucketMaxX = (maxX + BUCKET_FACTOR - 1) / BUCKET_FACTOR;
	int indexBucketMaxY = (maxY + BUCKET_FACTOR - 1) / BUCKET_FACTOR;
	int indexBucketMaxZ = (maxZ + BUCKET_FACTOR - 1) / BUCKET_FACTOR;
	// Redo the min / max here that has already be done by the caller (but with a more accurate range).
	// Improve this at a later point.
	indexBucketMinX = MAX(indexBucketMinX, 1);				// Starts at 1
	indexBucketMinY = MAX(indexBucketMinY, 1);
	indexBucketMinZ = MAX(indexBucketMinZ, 1);
	indexBucketMaxX = MIN(indexBucketMaxX, BUCKET_X - 2);	// Finishes at 49 (max possible value is 50, but we want no particles there).
	indexBucketMaxY = MIN(indexBucketMaxY, BUCKET_Y - 2);
	indexBucketMaxZ = MIN(indexBucketMaxZ, BUCKET_Z - 2);

	const int sizeX = indexBucketMaxX - indexBucketMinX;
	const int sizeY = indexBucketMaxY - indexBucketMinY;
	const int sizeZ = indexBucketMaxZ - indexBucketMinZ;

	// Optimize array access for the following tight loops
	// This reduces the number of multiplications and register pressure (thus reducing LHS)
	int index = indexBucketMinX * BUCKET_Y * BUCKET_Z;
	index += indexBucketMinY * BUCKET_Z;
	index += indexBucketMinZ;

	CBucket * * xBucket = &bucketsByXYZ[index];

	// Use sizeX to 0 loop, to reduce memory usage in the loop
	for (int x = sizeX ; x >= 0 ; --x)
	{
		register CBucket * * yBucket = xBucket;
		for (register int y = sizeY ; y >= 0 ; --y)
		{
			if (y != sizeY)
			{
				// We have at least one more line, so we can prefetch it
				// Prefetch yBucket[z + BUCKET_Z] as reading yBucket[z] is 75% of the cache misses in this function.
				PREFETCH_128(yBucket + BUCKET_Z, 0);
			}

			register CBucket * * zBucket = yBucket;
			for (register int z = sizeZ ; z >= 0 ; --z)
			{
				register CBucket * bucket = *zBucket;
				if (bucket == &zeroBucket)
				{
					// Have to get a new Bucket
					CheckBucketAllocations(localCurCacheBucket);
					// We are going to prefetch 8 buckets ahead (clearing a bucket is fast enough, no need to zero in).
					// At least 4 buckets fit in a cache line.
					if (localCurCacheBucket % (CACHE_LINE_SIZE / sizeof(CBucket)) == 0)
					{
						// prefetch only as necessary
						PREFETCH_128(cacheBuckets[localCurCacheBucket + BUCKETS_PREFETCH_AHEAD], 0);

						if (localCurCacheBucket % (2 * CACHE_LINE_SIZE / sizeof(CBucket *)) == 0)
						{
							// Prefetch cacheBuckets itself. 25% of the cache misses when reading cacheBuckets[curCacheBucket + BUCKETS_PREFETCH_AHEAD]
							// Because cacheBuckets array is not cache line aligned, prefetching the next cache line may not be good enough
							// As it could be only few pointers later. Instead, prefetch 2 cache lines ahead.
							const int NEXT_CACHE_LINE = 4 * CACHE_LINE_SIZE / sizeof(CBucket*);
							int index = localCurCacheBucket + NEXT_CACHE_LINE;
							if (index < cacheBuckets.size)
							{
								// Make sure that we only prefetch some memory addressable by the array
								// It should not be a problem to prefetch, except if we were using virtual allocations, etc...
								PREFETCH_128(&cacheBuckets[localCurCacheBucket + NEXT_CACHE_LINE], 0);
							}
						}
					}

					bucket = cacheBuckets[localCurCacheBucket++];
					bucket->SetXYZ(indexBucketMinX + (sizeX - x), indexBucketMinY + (sizeY - y), indexBucketMinZ + (sizeZ - z));

					*zBucket = bucket;

					// Specialize the code for the first particle , to reduce one LHS (when resetting m_nNumParticles and incrementing it below).
					bucket->m_ParticlesIndex[0] = particleIndex;
					bucket->m_nNumParticles = 1;
					bucket->m_pCorners = NULL;
					bucket->ClearAllAboveBelowFlags();
				}
				else
				{
					// Unroll the test to reduce one LHS (after m_nNumParticles has been reset above)
					int numParticles = bucket->m_nNumParticles;
					if (numParticles < CBucket::MAX_PARTICLES_PER_BUCKET)
					{
						bucket->m_ParticlesIndex[numParticles] = particleIndex;
						bucket->m_nNumParticles = numParticles + 1;
					}
				}
				++zBucket;
			}
			yBucket += BUCKET_Z;
		}
		xBucket += BUCKET_Y * BUCKET_Z;
	}

	curCacheBucket = localCurCacheBucket;
}

class CSorter
{
public:
	static bool gt(ImpParticle* a, ImpParticle* b)
	{
		// Instead of sorting by X, we sort by weight
		// Bigger weight first (so they are added in more priority in each bucket).
		return a->scale > b->scale;
	}
};

void CBucketBlobRenderer::BuildCache(float cw, float fieldRadius, Point3D & mins, Point3D & maxs)
{
	CSorter sorter;
	particles.sort(sorter); // from biggest x to smallest x
	if(!particles.testSort(sorter)) {IMP_PRINT("Error: Sort failed\n");};

	m_mins = mins;
	m_cubeWidth = cw;

	for(int nParticleIndex = 0; nParticleIndex < particles.size; ++nParticleIndex)
	{
		ImpParticle * p = particles[nParticleIndex];

		// TODO: use oneOverCw instead of dividing by cw
		Point3D c = (p->center - mins) / cw;
		float fr = (fieldRadius * p->scale) / cw; // field radius in terms of cubewidths

		// Make sure there is enough elements in the cache to fit all the elements of the particle
		// TODO: This might be slower than just doing it in the inner loop the way it was done before. Should check.

		// NOTE: The 0+1 and size-1 max's and min's force the cache
		// to essentially have no entries at the edges. This forces
		// the field function to be 0 there which in turn forces the
		// renderer to go around the edges rendering all surfaces.
		// It also allows us to not check in the renderer that we've
		// run past the edge of a cube because there is no way a surface
		// can continue outside the edge of a cube with this scheme.
		//int min_x = max(round_up(c[0] - fr), 0+1);
		//int max_x = min(round_down(c[0] + fr), xsize-1);

		// Interestingly, there is a similar code within the loop!
		// Except it seems less efficient (not exact same calculation as for Y and Z).
		// And executed for each loop instead of once for all!
		// Investigate this...
		int min_x = MAX(round_up(c[0] - fr), 0 + 1);
		int max_x = MIN(round_down(c[0] + fr), X_SIZE-1);

		int min_y = MAX(round_up(c[1] - fr), 0+1);
		int max_y = MIN(round_down(c[1] + fr), Y_SIZE-1);

		int min_z = MAX(round_up(c[2] - fr), 0+1);
		int max_z = MIN(round_down(c[2] + fr), Z_SIZE-1);

		// Note that we are going to fill the space between (min_x2, min_y, min_z) and (max_x2, max_y, max_z).
		// The space will be a little bit conservative as we map a sphere to a cube, so we may fill some buckets that don't need this.
		// If we want to improve performance a bit, we could have a more accurate calculation (which in some cases could free some space for some particles).
		FillBuckets(nParticleIndex, min_x, max_x, min_y, max_y, min_z, max_z);
	}
}


// This is a more optimized version than calcCornerNormalColorUVTan_SIMD
// The comments have been added for optimization reasons, for details on the original algorithm, have a look at the calcCornerNormalColorUVTan_SIMD version.
// DONE:
//	1. Used MaddSIMD instead of MaddSIMD and MulSIMD separately. This is twice as fast on X360 and PS3.
//		Although in some cases, the X360 compiler will transform one into the other, it is not always the case, and it still reduces the register pressure.
//  2. Remove the interpolants calculation. This does not seem to be used.
//  3. Simplify the equations a bit to make them faster to execute with a slight loss in correctness.
//		Actually currently, there is no loss of accuracy with the current code. And the code is much simpler / faster.
//  4. Change the cache linked list to a concept of Buckets. Each bucket contains particle indices.
//	5. Sprinkle the code with some __dbct.
//  6. Change ClearIds() to write the memory as a vector (1 operation instead of 3).
//  7. cCalculate the 8 corners at a time so VMX can shine.
void CBucketBlobRenderer::Calculate(CBucket * bucket)
{
	Assert(bucket->IsCalculated() == false);

	CornerInfo * corner = FillNewCornerInfo(bucket);

	// We have 27 corners, but by design only calculate 8 corners (2*2*2 lowest corners).
	// We will use the other corners later from the neighbor Buckets.

	int x, y, z;
	x = bucket->m_nX * BUCKET_FACTOR;
	y = bucket->m_nY * BUCKET_FACTOR;
	z = bucket->m_nZ * BUCKET_FACTOR;

	const fltx4 f4scalerSq = g_f4scalerSq;
	const fltx4 f4Fourpt25s = g_f4Fourpt25s;
	const fltx4 f4cutoffRSq = g_f4cutoffRSq;

	// Slow version for the moment
	fltx4 f4Position;
	// Somehow there is no way to set XYZ at once in the SIMD API.
	// Note that this could be a little bit more optimized by doing permutations instead of splatting X, Y, Z to the whole vectors
	// Note that we could do the conversion by the VMX, here is how that would work:
	//	- Read bucket->m_nX, m_nY and m_nZ as unaligned memory.
	//  - Shuffle it, so they are placed in each XYZ words.
	//  - Convert from integer to float the vector
	//  - Multiply it by BUCKET_FACTOR
	// This would help optimize the masking below for f4Position
	const fltx4 f4X = ReplicateX4(&Constants::s_flByteToFloat[x]);
	const fltx4 f4Y = ReplicateX4(&Constants::s_flByteToFloat[y]);
	const fltx4 f4Z = ReplicateX4(&Constants::s_flByteToFloat[z]);

	// While we are doing a bunch of calculation, let's prefetch the corners where we are going to write
	// We did not do that earlier in order to not fill the cache queue with the memory accesses above.
	// Otherwise a L2 cache miss on say g_flByteToFloat would have to wait the 2 corner cache lines to be read
	// before continuing further.
	PREFETCH_128(corner, 0 * CACHE_LINE_SIZE);
	// Because the 2nd half of the corner info group will be mostly zeroed (all the indices)
	// Let's zero it as is, as it should be faster than the prefetch and should save some CPU cycles (see at the end)
	PREZERO_128(corner, 1 * CACHE_LINE_SIZE);

	f4Position = MaskedAssign((bi32x4)LoadAlignedSIMD( Constants::s_SIMD_100Mask ), f4X, f4Y);			// X | Y | Y | Y
	f4Position = MaskedAssign((bi32x4)LoadAlignedSIMD( Constants::s_SIMD_110Mask ), f4Position, f4Z);	// X | Y | Z | Z

	const fltx4 f4CubeWidth = ReplicateX4(&m_cubeWidth);
	f4Position = MaddSIMD(f4CubeWidth, f4Position, m_mins.sse_vec);

	fltx4 f4Position1 = AddSIMD(f4Position, f4CubeWidth);

	const bi32x4 mask1110 = (bi32x4)LoadAlignedSIMD( Constants::s_SIMD_111Mask );
	f4Position = MaskedAssign(mask1110, f4Position, Four_Ones );					// X | Y | Z | 1
	f4Position1 = MaskedAssign(mask1110, f4Position1, Four_Ones );					// X | Y | Z | 1

	// In this calculation, we could actually skip after a given number of particles.
	// We could sort the particles by scale instead of by X (so we have the bigger particles first).
	// No need to calculate the influence of 60 particles. 16 or even should be enough in most cases.
	// Note that I believe that the SIMD calculation was twice as fast when I only 7 particles were used to influence a bucket.
	// So this should provide a good speed up in many cases.
	// 16 seems to be a good value, with minor differences (the differences are nor aesthetically an issue).
	int numParticles = bucket->m_nNumParticles;
	/*
	 * Enable this if we want to skip some particles at rendering. 
	if (numParticles > r_paintblob_max_particles_per_buckets.GetInt())
	{
		numParticles = r_paintblob_max_particles_per_buckets.GetInt();
	}
	*/

	// This code is roughly equivalent to calcCornerNormalColorUVTan_SIMD2 (one corner at a time)
	// Except it is more optimized for 1 and 2 particles.

#if 0
	fltx4 f4SIMD_Mask[8];

	f4SIMD_Mask[0] = LoadAlignedSIMD( g_SIMD_000Mask );
	f4SIMD_Mask[1] = LoadAlignedSIMD( g_SIMD_001Mask );
	f4SIMD_Mask[2] = LoadAlignedSIMD( g_SIMD_010Mask );
	f4SIMD_Mask[3] = LoadAlignedSIMD( g_SIMD_011Mask );
	f4SIMD_Mask[4] = LoadAlignedSIMD( g_SIMD_100Mask );
	f4SIMD_Mask[5] = LoadAlignedSIMD( g_SIMD_101Mask );
	f4SIMD_Mask[6] = LoadAlignedSIMD( g_SIMD_110Mask );
	f4SIMD_Mask[7] = LoadAlignedSIMD( g_SIMD_111Mask );

	// Unoptimized code, disabled for the moment
	for (int j = 0 ; j < 8 ; ++j)
	{
		fltx4 mask = f4SIMD_Mask[j];
		fltx4 currentPosition = MaskedAssign( ( bi32x4 ) mask, f4Position1, f4Position );

		// Now let's loop through each particle (this part will be unrolled later)
		// For code with 1, 2, 3 particles, we may want to calculate the 8 corners at the same time
		// so we can take advantage of the VMX speed up.

		fltx4 normal = Four_Zeros;

		for (int k = 0 ; k < numParticles ; ++k)
		{
			// Only this version is documented, the others are just unrolled / pipelined version
			ImpParticle * p = particles[bucket->m_ParticlesIndex[k]];

			// TODO: Optimize this ReplicateX4!
			fltx4 f4pRSq = ReplicateX4( p->fieldRScaleSq );
			fltx4 diff = SubSIMD( currentPosition, p->center.sse_vec );
			fltx4 diffSq = MulSIMD( diff, diff );

			// scalar code
			//	float rSq = SubFloat(diffSq, 0)+SubFloat(diffSq, 1)+SubFloat(diffSq, 2); //Can this be done in SSE?
			//	rSq *= p->fieldRScaleSq; // TODO: The fieldRScaleSq should incorporate scalerSq (can compare rSq with one over sqrt(2) instead of cutoffRSq below)

			//fltx4 f4rSq = MulSIMD( f4pRSq, AddSIMD( AddSIMD( diffSq, RotateLeft( diffSq ) ), RotateLeft2( diffSq ) ) );
			// The optimized calculation (at least on X360) is a 3D dot product against f4pRSq
			fltx4 f4rSq = Dot3SIMD(f4pRSq, diffSq);

			// With the new triage algorithm, there may be some particles
			// that are calculated even if they should not influence the cube.
			// Remove their influence if their distance is bigger than cuttOffR
			fltx4 influence = CmpGeSIMD(f4cutoffRSq, f4rSq);
			diff = AndSIMD(influence, diff);

			//rSq *= RENDERER_CLASS::scalerSq;
			// This should be moved in ImpParticle::setFieldScale() when returning fieldRScaleSq
			// But somehow this would not compile
			// TODO: get back to it later...
			f4rSq = MulSIMD( f4rSq, f4scalerSq );
			//float f = 0.25f + rSq*rSq - rSq;
			fltx4 f4 = SubSIMD( MaddSIMD(f4rSq, f4rSq, f4Fourpt25s) , f4rSq );

			normal = MaddSIMD(diff, f4, normal);
		}

		normal = SubSIMD(normal, g_f4Threshold );
		StoreAlignedSIMD( bucket->m_pCorners[j].normal, normal );

		// ONA: Can we do this with one vector store?
		VERTEX_BUFFER_CLASS::clear3Ids( bucket->m_pCorners[j].edges );
	}
#else

	// Maybe we should use FourVectors, but actually W is not used in that library (it is 4 vectors of XYZ only)
	// Dot3SIMD is not supported in that library, nor CmpGeSIMD. Test that another day.

	fltx4 normalA = -g_f4Threshold;
	fltx4 normalB = normalA;
	fltx4 normalC = normalA;
	fltx4 normalD = normalA;
	fltx4 normalE = normalA;
	fltx4 normalF = normalA;
	fltx4 normalG = normalA;
	fltx4 normalH = normalA;

	// For a given particle, the center is loaded only once, same for fieldRScaleSq.
	// It is faster than the calcCornerNormalColorUVTan_SIMD2 function (one corner at a time), especially for 1, 2 and 3 particles (parallel SIMD even for these cases).
	// TODO: Optimize the load.
	// Also, when moving to SPU, maybe we should store the particles in one contiguous array with the bare minimum data.
	// 200 particles * 16 bytes = A DMA read of 3200 bytes when starting the job, instead of a scatter DMA access.

	// Hopefully, there won't be too much register pressure with the code below.
	// We could reorganize the code a bit so only 4 corners would be calculated at a time.

	//const fltx4 currentPositionA = f4Position;			// X | Y | Z | 1
	const fltx4 currentPositionB = MaskedAssign((bi32x4)LoadAlignedSIMD( Constants::s_SIMD_001Mask ), f4Position1, f4Position);
	const fltx4 currentPositionC = MaskedAssign((bi32x4)LoadAlignedSIMD( Constants::s_SIMD_010Mask ), f4Position1, f4Position);
	const fltx4 currentPositionD = MaskedAssign((bi32x4)LoadAlignedSIMD( Constants::s_SIMD_011Mask ), f4Position1, f4Position);
	const fltx4 currentPositionE = MaskedAssign((bi32x4)LoadAlignedSIMD( Constants::s_SIMD_100Mask ), f4Position1, f4Position);
	const fltx4 currentPositionF = MaskedAssign((bi32x4)LoadAlignedSIMD( Constants::s_SIMD_101Mask ), f4Position1, f4Position);
	const fltx4 currentPositionG = MaskedAssign((bi32x4)LoadAlignedSIMD( Constants::s_SIMD_110Mask ), f4Position1, f4Position);
	//const fltx4 currentPositionH = f4Position1;

	for (int i = 0 ; i < numParticles ; ++i)
	{
		ImpParticle * p0 = particles[bucket->m_ParticlesIndex[i]];
		fltx4 center0 = p0->center.sse_vec;								// X | Y | Z | fieldRScaleSq

		// Faster version of this: ReplicateX4( p0->fieldRScaleSq );
		//fltx4 f4pRSq0 = ReplicateX4(p0->fieldRScaleSq);
		fltx4 f4pRSq0 = SplatWSIMD(center0);							// fieldRScaleSq | fieldRScaleSq | fieldRScaleSq | fieldRScaleSq
		center0 = MaskedAssign(mask1110, center0, Four_Zeros);			// X | Y | Z | 0

		fltx4 diff0A = SubSIMD( f4Position, center0 );					// Use f4Position as is to reduce register pressure
		fltx4 diff0B = SubSIMD( currentPositionB, center0 );
		fltx4 diff0C = SubSIMD( currentPositionC, center0 );
		fltx4 diff0D = SubSIMD( currentPositionD, center0 );
		fltx4 diff0E = SubSIMD( currentPositionE, center0 );
		fltx4 diff0F = SubSIMD( currentPositionF, center0 );
		fltx4 diff0G = SubSIMD( currentPositionG, center0 );
		fltx4 diff0H = SubSIMD( f4Position1, center0 );					// Use f4Position1 as is to reduce register pressure

		fltx4 diffSq0A = MulSIMD( diff0A, diff0A );
		fltx4 diffSq0B = MulSIMD( diff0B, diff0B );
		fltx4 diffSq0C = MulSIMD( diff0C, diff0C );
		fltx4 diffSq0D = MulSIMD( diff0D, diff0D );
		fltx4 diffSq0E = MulSIMD( diff0E, diff0E );
		fltx4 diffSq0F = MulSIMD( diff0F, diff0F );
		fltx4 diffSq0G = MulSIMD( diff0G, diff0G );
		fltx4 diffSq0H = MulSIMD( diff0H, diff0H );

		fltx4 f4rSq0A = Dot3SIMD(f4pRSq0, diffSq0A);
		fltx4 f4rSq0B = Dot3SIMD(f4pRSq0, diffSq0B);
		fltx4 f4rSq0C = Dot3SIMD(f4pRSq0, diffSq0C);
		fltx4 f4rSq0D = Dot3SIMD(f4pRSq0, diffSq0D);
		fltx4 f4rSq0E = Dot3SIMD(f4pRSq0, diffSq0E);
		fltx4 f4rSq0F = Dot3SIMD(f4pRSq0, diffSq0F);
		fltx4 f4rSq0G = Dot3SIMD(f4pRSq0, diffSq0G);
		fltx4 f4rSq0H = Dot3SIMD(f4pRSq0, diffSq0H);

		bi32x4 influence0A = CmpGeSIMD(f4cutoffRSq, f4rSq0A);
		bi32x4 influence0B = CmpGeSIMD(f4cutoffRSq, f4rSq0B);
		bi32x4 influence0C = CmpGeSIMD(f4cutoffRSq, f4rSq0C);
		bi32x4 influence0D = CmpGeSIMD(f4cutoffRSq, f4rSq0D);
		bi32x4 influence0E = CmpGeSIMD(f4cutoffRSq, f4rSq0E);
		bi32x4 influence0F = CmpGeSIMD(f4cutoffRSq, f4rSq0F);
		bi32x4 influence0G = CmpGeSIMD(f4cutoffRSq, f4rSq0G);
		bi32x4 influence0H = CmpGeSIMD(f4cutoffRSq, f4rSq0H);

		diff0A = AndSIMD(influence0A, diff0A);
		diff0B = AndSIMD(influence0B, diff0B);
		diff0C = AndSIMD(influence0C, diff0C);
		diff0D = AndSIMD(influence0D, diff0D);
		diff0E = AndSIMD(influence0E, diff0E);
		diff0F = AndSIMD(influence0F, diff0F);
		diff0G = AndSIMD(influence0G, diff0G);
		diff0H = AndSIMD(influence0H, diff0H);

		// It seems we could get rid of these 8 instructions if we were able to
		// integrate f4ScalerSq in particle->fieldRScaleSq
		f4rSq0A = MulSIMD( f4rSq0A, f4scalerSq );
		f4rSq0B = MulSIMD( f4rSq0B, f4scalerSq );
		f4rSq0C = MulSIMD( f4rSq0C, f4scalerSq );
		f4rSq0D = MulSIMD( f4rSq0D, f4scalerSq );
		f4rSq0E = MulSIMD( f4rSq0E, f4scalerSq );
		f4rSq0F = MulSIMD( f4rSq0F, f4scalerSq );
		f4rSq0G = MulSIMD( f4rSq0G, f4scalerSq );
		f4rSq0H = MulSIMD( f4rSq0H, f4scalerSq );

		fltx4 f40A = SubSIMD( MaddSIMD(f4rSq0A, f4rSq0A, f4Fourpt25s) , f4rSq0A );
		fltx4 f40B = SubSIMD( MaddSIMD(f4rSq0B, f4rSq0B, f4Fourpt25s) , f4rSq0B );
		fltx4 f40C = SubSIMD( MaddSIMD(f4rSq0C, f4rSq0C, f4Fourpt25s) , f4rSq0C );
		fltx4 f40D = SubSIMD( MaddSIMD(f4rSq0D, f4rSq0D, f4Fourpt25s) , f4rSq0D );
		fltx4 f40E = SubSIMD( MaddSIMD(f4rSq0E, f4rSq0E, f4Fourpt25s) , f4rSq0E );
		fltx4 f40F = SubSIMD( MaddSIMD(f4rSq0F, f4rSq0F, f4Fourpt25s) , f4rSq0F );
		fltx4 f40G = SubSIMD( MaddSIMD(f4rSq0G, f4rSq0G, f4Fourpt25s) , f4rSq0G );
		fltx4 f40H = SubSIMD( MaddSIMD(f4rSq0H, f4rSq0H, f4Fourpt25s) , f4rSq0H );

		normalA = MaddSIMD(diff0A, f40A, normalA);
		normalB = MaddSIMD(diff0B, f40B, normalB);
		normalC = MaddSIMD(diff0C, f40C, normalC);
		normalD = MaddSIMD(diff0D, f40D, normalD);
		normalE = MaddSIMD(diff0E, f40E, normalE);
		normalF = MaddSIMD(diff0F, f40F, normalF);
		normalG = MaddSIMD(diff0G, f40G, normalG);
		normalH = MaddSIMD(diff0H, f40H, normalH);
	}

	StoreAlignedSIMD( corner[0].normal, normalA );
	StoreAlignedSIMD( corner[1].normal, normalB );
	StoreAlignedSIMD( corner[2].normal, normalC );
	StoreAlignedSIMD( corner[3].normal, normalD );
	StoreAlignedSIMD( corner[4].normal, normalE );
	StoreAlignedSIMD( corner[5].normal, normalF );
	StoreAlignedSIMD( corner[6].normal, normalG );
	StoreAlignedSIMD( corner[7].normal, normalH );

	// The first corner has the Bucket *, so we can't reset this one to zero as is
	// (it would be too costly to read it in a VMX, mask out, then store it. Let's write altogether).
	int * bufferToZero = (int *)(corner[0].edges);
	bufferToZero[0] = 0;
	bufferToZero[1] = 0;
	bufferToZero[2] = 0;

	// But we can reset the others directly (on the first cache line)
	fltx4 zero = Four_Zeros;
	StoreAlignedSIMD( (float *)corner[1].edges, zero );
	StoreAlignedSIMD( (float *)corner[2].edges, zero );
	StoreAlignedSIMD( (float *)corner[3].edges, zero );
	// Another possibility would be to zero the second cache line as is.
	// Use the code below if zeroing the cache does not work well.
/*
	StoreAlignedSIMD( (float *)corner[4].edges, zero );
	StoreAlignedSIMD( (float *)corner[5].edges, zero );
	StoreAlignedSIMD( (float *)corner[6].edges, zero );
	StoreAlignedSIMD( (float *)corner[7].edges, zero );
*/
#endif
}

void CBucketBlobRenderer::CalcVertex(const float x, const float y, const float z, const int edge, const CornerInfo * pCorner1, const CornerInfo * pCorner2)
{
	float ALIGN16 flPosition[4];
	flPosition[0] = x;
	flPosition[1] = y;
	flPosition[2] = z;

	fltx4 f4LoVal = ReplicateX4( pCorner1->value );
	fltx4 f4HiVal = ReplicateX4( pCorner2->value );
	fltx4 f4Pct = DivSIMD( f4LoVal, SubSIMD( f4LoVal, f4HiVal ) );
	fltx4 f4OMPct = SubSIMD( Four_Ones, f4Pct );

	fltx4 f4Edge = AndSIMD( LoadAlignedSIMD( g_SIMD_ComponentMask[edge] ), f4Pct );

	const float *loNormal = pCorner1->normal;
	const float *hiNormal = pCorner2->normal;
	fltx4 f4Normal = AddSIMD( MulSIMD( LoadAlignedSIMD(loNormal), f4OMPct ), MulSIMD( LoadAlignedSIMD(hiNormal), f4Pct ) );

#if _X360
	f4Normal = XMVector3Normalize( f4Normal );
#else
	VectorNormalize( (float *)&f4Normal );
#endif

	// This is the optimized version of the code above. The point of normalizing
	// is that when the interpolated values are calculated for the normal and interpolants,
	// and then interpolated to get a value at the vertex, they are all essentially getting
	// scaled to threshold times their value. This is because they are getting scaled by
	// f in the corner calculation and never normalized by totalf. However, that normalization
	// is unnecessary because we know that by the time we get here, the average of both
	// corners is essentially scaled by the threshold.

	// TODO: We can also scale the normal by the normalizer to get it closer to
	//       a unit normal if we want to, but it 's not necessary.
	fltx4 f4Normalizer = ReplicateX4( m_fOneOverThreshold );
	f4Pct = MulSIMD( f4Normalizer, f4Pct );
	f4OMPct = MulSIMD( f4Normalizer, f4OMPct );

	fltx4 f4Position = AddSIMD( LoadAlignedSIMD( flPosition ), MulSIMD( f4Edge, ReplicateX4( m_fCubeWidth ) ) );

	m_pVertexBuffer->setVertexPosition( f4Position );
	m_pVertexBuffer->setVertexNormal( f4Normal );
}

CornerInfo * CBucketBlobRenderer::FillNewCornerInfo(CBucket * bucket)
{
	CornerInfo * corner;
	CBucket * replacedBucket;
	int offset = currentCornerIndex++;		// Only increased once per lookup
	// We do not want the collision detection to increase too quickly the counter
	for ( ; ; )
	{
		offset %= NUM_CORNERS_GROUP_ALLOCATED;

		corner = m_pAllCorners + (offset * NUM_CORNERINFOS_PER_CORNER_GROUP);

		// Prefetch 8 cache lines ahead, which pretty much represent 4 corners group.
		PREFETCH_128(corner, 8 * CACHE_LINE_SIZE);

		replacedBucket = corner->bucket;
		if (replacedBucket == NULL)
		{
			// Not used by another bucket yet
			break;
		}

		// The Bucket is potentially a neighbor that we need for the calculation as well
		// We don't want to remove it from the cache as it would invalidate the calculation, crash

		int diffX = FastAbs(bucket->m_nX - replacedBucket->m_nX);
		int diffY = FastAbs(bucket->m_nY - replacedBucket->m_nY);
		int diffZ = FastAbs(bucket->m_nZ - replacedBucket->m_nZ);

		if ((diffX | diffY | diffZ) >= 2)
		{
			// The bucket is not a neighbor (within -1 / +1 in any axis), we can use it

			// Indicate that the corner is not valid anymore for this bucket
			// (this will force a recalculation at the next usage of that bucket).
			replacedBucket->m_pCorners = NULL;
			break;
		}

		// It is a neighbor, we need to find another corner (with another bucket).
		// We don't want to jump to near as we may encounter the same collision later,
		// or this will remove the calculation on buckets that we need.
		// Jump at the opposite of the cache plus some prime number
		offset += (NUM_CORNERS_GROUP_ALLOCATED / 2) + 17;

		// Don't this will mess the prefetch during collision, but it happens rarely enough (0.1%).
	}
	bucket->m_pCorners = corner;
	corner->bucket = bucket;
	return corner;
}

}
}

