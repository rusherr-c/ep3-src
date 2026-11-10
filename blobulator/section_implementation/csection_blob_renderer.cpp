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

#include "blobulator/section_implementation/csection_blob_renderer.h"
#include "blobulator/section_implementation/ccorner_info.h"
#include "blobulator/section_implementation/cthread_data.h"
#include "blobulator/implicit/impparticle.h"
#include "blobulator/util/fasttrunc.h"
#include "blobulator/util/constants.h"
#include "tier0/cache_hints.h"
#include "tier0/vprof.h"
#include "tier1/convar.h"
#include "vstdlib/jobthread.h"

#if PLATFORM_WINDOWS_PC
#include <intrin.h>
#pragma intrinsic(_BitScanForward)
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern const int32 ALIGN16 g_SIMD_ComponentMask[4][4] ALIGN16_POST;

#if _PS3
const char g_DefaultMaxNumThreads[] = "7";		// We are using up to 6 SPUs + 1 main job
#else
const char g_DefaultMaxNumThreads[] = "4";		// Revisit this number on X360 and PC once we actually run on the main thread as well.
#endif
ConVar r_paintblob_max_number_of_threads("r_paintblob_max_number_of_threads", g_DefaultMaxNumThreads, FCVAR_NONE, "Indicates the maximum number of threads that will be spawn for the blob.");

namespace Blobulator
{
namespace SectionImplementation
{

Blobulator::SectionImplementation::CBucket ALIGN16 CSectionBlobRenderer::s_ZeroBucket;
Blobulator::SectionImplementation::CornerInfo ALIGN16 CSectionBlobRenderer::s_ZeroCorners[NUM_CORNERINFOS_PER_CORNER_GROUP];

Section Section::s_ZeroSection;

CSectionBlobRenderer::CSectionBlobRenderer()
:
	m_offset(0,0,0),
	m_nMaxNoSlicesToDraw(-1),
	curCacheBucket(0),
	maxNumberOfParticlesPerBucket(0),
	curCacheCornerInfo(0),
	currentCornerIndex(0),
	currentSection(0)
{
	Blobulator::Constants::InitializeConstants();

	m_nStackSize = 0;

	for (int i = 0 ; i < sizeof(m_Sections) / sizeof(m_Sections[0]) ; ++i)
	{
		m_Sections[i] = &Section::s_ZeroSection;
	}

	// Initialize index, so buckets have their corresponding cache, that is vertex buffer friendly.
	for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
	{
		m_JobCaches[i].m_pFragmentedVertexBuffer = &m_FragmentedVertexBuffer;
		m_JobCaches[i].m_nJobIndex = i;
	}

#if BLOB_CREATE_OWN_THREADPOOL
	m_pThreadPool = CreateNewThreadPool();

	ThreadPoolStartParams_t startParams;
	startParams.nThreads = BLOB_NUM_JOBS;
	startParams.iThreadPriority = SHRT_MIN;
	startParams.nStackSize = 32 * 1024;
	startParams.fDistribute = TRS_TRUE;
	startParams.bUseAffinityTable = false;
/*
	if (IsX360())
	{
		startParams.iAffinityTable[0] = XBOX_PROCESSOR_2;
		startParams.iAffinityTable[1] = XBOX_PROCESSOR_3;
		startParams.iAffinityTable[2] = XBOX_PROCESSOR_4;
		startParams.iAffinityTable[3] = XBOX_PROCESSOR_5;
	}
*/
	m_pThreadPool->Start(startParams, "BlobulatorJobs");
#endif

	const int nSizeToAllocate = NUM_CORNERS_GROUP_ALLOCATED * sizeof(CornerInfo) * NUM_CORNERINFOS_PER_CORNER_GROUP;
	m_pAllCorners = (CornerInfo *)(MemAlloc_AllocAligned(nSizeToAllocate, CACHE_LINE_SIZE));
	memset(m_pAllCorners, 0, nSizeToAllocate);

	memset(NumberOfParticlesPerBucketOvertime, 0, sizeof(NumberOfParticlesPerBucketOvertime));

	// We consider that these items are already done, so no test is needed when crawling the surface.
	s_ZeroBucket.ClearAllCubeFlags();
	for (int i = 0 ; i < NUM_CORNERINFOS_PER_CORNER_GROUP ; ++i)
	{
		s_ZeroCorners[i].value = -m_fThreshold;
	}
	for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
	{
		s_ZeroBucket.m_pCorners[i] = &s_ZeroCorners[0];
	}

	// The first bucket is always the zero bucket, si new bucket index starts at 1 and not 0.
	cacheBuckets.pushAutoSize(&s_ZeroBucket);
	++curCacheBucket;
	CheckBucketAllocations(curCacheBucket);
}

CSectionBlobRenderer::~CSectionBlobRenderer()
{
#if BLOB_CREATE_OWN_THREADPOOL
	m_pThreadPool->Stop();
	DestroyThreadPool(m_pThreadPool);
	m_pThreadPool = NULL;
#endif

	MemAlloc_FreeAligned(m_pAllCorners);
}

void CSectionBlobRenderer::BeginFrame(bool bTileMode, void * pRenderContext)
{
	VPROF_BUDGET( "CSectionBlobRenderer::BeginFrame", "Blobulator" );

	RecalculateConstants();

	m_bTileMode = bTileMode;

	for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
	{
		m_JobCaches[i].BeginFrame(pRenderContext);
	}
	m_FragmentedVertexBuffer.BeginFrame(pRenderContext);
}

void CSectionBlobRenderer::EndFrame()
{
	VPROF_BUDGET( "CSectionBlobRenderer::EndFrame", "Blobulator" );

	for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
	{
		m_JobCaches[i].EndFrame();
	}
	m_FragmentedVertexBuffer.EndFrame();
}

void CSectionBlobRenderer::BeginTile(ImpTile * pTile)
{
	VPROF_BUDGET( "CSectionBlobRenderer::BeginTile", "Blobulator" );

	RecalculateBoundingBoxes();

	for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
	{
		m_JobCaches[i].g_f4Fourpt25s = g_f4Fourpt25s;
		m_JobCaches[i].g_f4scalerSq = g_f4scalerSq;
		m_JobCaches[i].g_f4cutoffRSq = g_f4cutoffRSq;
		m_JobCaches[i].g_f4Threshold = g_f4Threshold;

		m_JobCaches[i].BeginTile();
	}
	m_FragmentedVertexBuffer.BeginTile();
}

void CSectionBlobRenderer::EndTile()
{
	VPROF_BUDGET( "CSectionBlobRenderer::EndTile", "Blobulator" );

	BuildCache(m_fCubeWidth, m_fCutoffRadius, m_OuterBBMins, m_OuterBBMaxs);
	for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
	{
		m_JobCaches[i].ResetBetweenFrames(m_OuterBBMins, m_fCubeWidth, &particles, &cacheBuckets, m_nMarginNCubes, m_bPolygonizationEnabled, m_bTileMode);
	}

	// Seed the surface
	for(int i = 0; i < particles.size; i++)
	{
		ImpParticle * pParticle = particles[i];
		SeedSurface(pParticle->center);
	}

	RenderSlices();

	particles.size = 0;

	// Once we are doing displaying everything, let's clean for next tile
	particles.size = 0;
	for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
	{
		m_JobCaches[i].EndTile();
	}
	m_FragmentedVertexBuffer.EndTile();

	// Clear the 3D array of buckets of the previous values
/*
	for (int i = 0 ; i < curCacheBucket ; ++i)
	{
		CBucket * bucket = cacheBuckets[i];
		if (bucket->m_nNumParticles > maxNumberOfParticlesPerBucket)
		{
			// Monitor how many particles we can have on a given bucket
			maxNumberOfParticlesPerBucket = bucket->m_nNumParticles;
		}
		++NumberOfParticlesPerBucketOvertime[bucket->m_nNumParticles];
		int index = GetIndexFromXYZ(bucket->m_nX * BUCKET_SIZE, bucket->m_nY * BUCKET_SIZE, bucket->m_nZ * BUCKET_SIZE);
		bucketsByXYZ[index] = &zeroBucket;
	}
*/

	// Clear the bucket pointers for each corner (so we know they are free)
	// We will have to optimize this at some point.
	// Actually, we may not need to do this. The buckets are always the same anyway (and added at the end).
/*
	for (int i = 0 ; i < NUM_CORNERS_GROUP_ALLOCATED ; ++i)
	{
		CornerInfo * corner = m_pAllCorners + (i * NUM_CORNERINFOS_PER_CORNER_GROUP);
		corner->bucket = NULL;
	}
*/
	curCacheBucket = 1;
//	curCacheCornerInfo = 0;
//	currentCornerIndex = 0;

	// We need to clear ahead some items are the prefetch / prezero does not happen on the early items when we recycle
	for (int i = 1 ; i < BUCKETS_PREFETCH_AHEAD ; ++i)
	{
		Construct(cacheBuckets[i]);
	}

	currentCornerIndex = 0;
}

void CSectionBlobRenderer::AddParticle(ImpParticle & particle, bool bSkipBoundsCheck)
{
	if (bSkipBoundsCheck || IsParticleWithinBounds(particle))
	{
		particles.pushAutoSize(&particle);
	}
}

void CSectionBlobRenderer::SetCubeWidth(float fCubeWidth)
{
	m_fCubeWidth = fCubeWidth;
}

float CSectionBlobRenderer::GetCubeWidth() const
{
	return m_fCubeWidth;
}

void CSectionBlobRenderer::SetRenderRadius(float fRenderRadius)
{
	m_RenderRadius = fRenderRadius;
}

float CSectionBlobRenderer::GetRenderRadius() const
{
	return m_RenderRadius;
}

void CSectionBlobRenderer::SetCutoffRadius(float fCutoffRadius)
{
	m_fCutoffRadius = fCutoffRadius;
}

float CSectionBlobRenderer::GetCutoffRadius() const
{
	return m_fCutoffRadius;
}
/*
float CSectionBlobRenderer::GetMarginNCubes() const
{
	return marginNCubes;
}
*/
const Point3D & CSectionBlobRenderer::GetInnerDimensions() const
{
	return m_InnerDimensions;
}

float CSectionBlobRenderer::GetMarginWidth() const
{
	return m_fMarginWidth;
}

void CSectionBlobRenderer::SetDebugMode(bool bDebugMode)
{
	m_bDebugMode = bDebugMode;
}

bool CSectionBlobRenderer::GetDebugMode() const
{
	return m_bDebugMode;
}

int CSectionBlobRenderer::GetNumberOfDebugInfo() const
{
	// Not supported yet
	return 0;
}

void CSectionBlobRenderer::DrawDebugInfo(IVDebugOverlay & debugOverlay, int nDebugInfoIndex)
{
	// Do nothing...
}

void CSectionBlobRenderer::RecalculateConstants()
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

void CSectionBlobRenderer::RecalculateBoundingBoxes()
{
	m_OuterBBMins = m_offset - (m_OuterDimensions * 0.5f);
	m_OuterBBMaxs = m_offset + (m_OuterDimensions * 0.5f);

	m_InnerBBMins = m_offset - (m_InnerDimensions * 0.5f);
	m_InnerBBMaxs = m_offset + (m_InnerDimensions * 0.5f);
}

bool CSectionBlobRenderer::IsParticleWithinBounds(ImpParticle & particle) const
{
	return (particle.center[0] > m_OuterBBMins[0] && particle.center[0] < m_OuterBBMaxs[0] &&
		particle.center[1] > m_OuterBBMins[1] && particle.center[1] < m_OuterBBMaxs[1] &&
		particle.center[2] > m_OuterBBMins[2] && particle.center[2] < m_OuterBBMaxs[2]);
}

void CSectionBlobRenderer::SeedSurface(Point3D & center)
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
				XYZ seed = MakeXYZ(x, y, z);
				Section * section = GetOrAllocateSection(x, y, z);
				section->PushSeed(seed);
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

Section * CSectionBlobRenderer::GetOrAllocateSection(int globalX, int globalY, int globalZ)
{
	int sectionX = globalX >> SECTION_X_POWER_OF_2;
	int sectionY = globalY >> SECTION_Y_POWER_OF_2;
	int sectionZ = globalZ >> SECTION_Z_POWER_OF_2;
	Assert((sectionX >= 0) && (sectionX < NUMBER_OF_SECTIONS_IN_X));
	Assert((sectionY >= 0) && (sectionY < NUMBER_OF_SECTIONS_IN_Y));
	Assert((sectionZ >= 0) && (sectionZ < NUMBER_OF_SECTIONS_IN_Z));
	int index = (((sectionX * NUMBER_OF_SECTIONS_IN_Y) + sectionY) * NUMBER_OF_SECTIONS_IN_Z) + sectionZ;
	Section * section = m_Sections[index];
	if (section == &Section::s_ZeroSection)
	{
		// Prefetch ahead (and make sure we have enough allocated too).
		const int SECTIONS_PREFETCH_AHEAD = 2;

		// No section at that place, grab a new one
		while (currentSection >= allSections.size - SECTIONS_PREFETCH_AHEAD)
		{
			// No available section, allocate a new one
			section = new Section();
			allSections.pushAutoSize(section);
		}

		if (currentSection % (CACHE_LINE_SIZE / sizeof(Section *)) == 0)
		{
			// A section fits in 3 cache lines :(
			// Given that X360 or PS3 have only 8 slots for memory access, we are using almost half of it with this
			PREFETCH_128(allSections[currentSection + SECTIONS_PREFETCH_AHEAD], 0 * 128);
			PREFETCH_128(allSections[currentSection + SECTIONS_PREFETCH_AHEAD], 1 * 128);
			PREFETCH_128(allSections[currentSection + SECTIONS_PREFETCH_AHEAD], 2 * 128);

			if (currentSection % (2 * CACHE_LINE_SIZE / sizeof(Section *)) == 0)
			{
				// Prefetch cacheBuckets itself. 25% of the cache misses when reading cacheBuckets[curCacheBucket + BUCKETS_PREFETCH_AHEAD]
				// Because cacheBuckets array is not cache line aligned, prefetching the next cache line may not be good enough
				// As it could be only few pointers later. Instead, prefetch 2 cache lines ahead.
				const int NEXT_CACHE_LINE = 2 * CACHE_LINE_SIZE / sizeof(Section *);
				int index = currentSection + NEXT_CACHE_LINE;
				if (index < allSections.size)
				{
					// Make sure that we only prefetch some memory addressable by the array
					// It should not be a problem to prefetch, except if we were using virtual allocations, etc...
					PREFETCH_128(&allSections[index], 0);
				}
			}
		}

		section = allSections[currentSection++];
		m_Sections[index] = section;
#if BLOB_RESET_INSIDE_JOBS == 0
		section->Reset();		// Currently we are incorrectly resetting the sections, let's do it here.
#endif
		section->SetXYZ(globalX & ~SECTION_X_MASK, globalY & ~SECTION_Y_MASK, globalZ & ~SECTION_Z_MASK);

		Assert(section->seed_list.size == 0);
		// To avoid cascading issues, make sure that the size is empty at first
		// The asserts above has to be fixed though, otherwise the blob will have some holes.
		section->seed_list.size = 0;
	}
	Assert(section->m_nX == (globalX & ~SECTION_X_MASK));
	Assert(section->m_nY == (globalY & ~SECTION_Y_MASK));
	Assert(section->m_nZ == (globalZ & ~SECTION_Z_MASK));
	return section;
}

FORCEINLINE
int GetAndRemoveLowestBit(uint64 & bits)
{
#if PLATFORM_WINDOWS_PC
	unsigned long index;
	// _BitScanForward64 is not accessible on 32 bits architecture
	if (_BitScanForward(&index, (unsigned long)bits))
	{
		bits &= ~(1 << index);
		return (index);
	}
	else
	{
		_BitScanForward(&index, (unsigned long)(bits >> 32));
		index += 32;
		bits &= ~(1ULL << index);
		return (index);
	}
#else
	// Verify that there is no equivalent on X360, PS3 and SPU
	if ((bits & 0x8000000000000000ULL) != 0)
	{
		// Neg does not work with the upper bit set, so special case here
		// Check this again at a later point
		bits &= 0x7fffffffffffffffULL;
		return (63);
	}
	else
	{
		// Branchless way to get the lowest bit set
		// all bits left of lsb become 1, lsb & right become 0 
		int64 bits2 = bits ^ (-(int64)bits); 

		// XOR a shifted copy recovers a single 1 in the lsb's location 
		uint64 bitsu = bits2 ^ (bits2 >> 1); 
		bits &= ~bitsu;

		// .. and isolate the bit in log2 of number of bits 
		int i0 = (bitsu & 0xAAAAAAAAAAAAAAAAULL) ?  1 : 0;		// This generates a bunch of branches. Improve this.
		int i1 = (bitsu & 0xCCCCCCCCCCCCCCCCULL) ?  2 : 0;		// Also ULL constants are not X360 / PS3 friendly (have to load in several operations)
		int i2 = (bitsu & 0xF0F0F0F0F0F0F0F0ULL) ?  4 : 0;		// It seems there is room for improvement :)
		int i3 = (bitsu & 0xFF00FF00FF00FF00ULL) ?  8 : 0; 
		int i4 = (bitsu & 0xFFFF0000FFFF0000ULL) ? 16 : 0; 
		int i5 = (bitsu & 0xFFFFFFFF00000000ULL) ? 32 : 0; 
		return i5 | i4 | i3 | i2 | i1 | i0; 
	}
#endif
}

class CBlobJob : public CJob
{
public:
	CThreadData * m_pThreadData;

	virtual JobStatus_t DoExecute()
	{
		CThreadData::ExecuteAllSections(m_pThreadData);
		return (0);
	}
};

class CSectionSorter
{
public:
	int m_Axis[3];

	FORCEINLINE
	bool gt(Section * a, Section * b)
	{
		// Would be good if we could figure out a way to do this in a more efficient way
		// TODO: Revisit this with bit masking, etc...
		int8 * pA = &a->m_nX;
		int8 * pB = &b->m_nX;

		int diff;
		for (int i = 0 ; i < 3 ; ++i)
		{
			int nFirstAxisA = *(pA + m_Axis[i]);
			int nFirstAxisB = *(pB + m_Axis[i]);
			diff = nFirstAxisA - nFirstAxisB;
			if (diff < 0)
			{
				return (false);
			}
			else if (diff > 0)
			{
				return (true);
			}
			// This axis is the same, go to the next
		}
		// Exactly same values (that should not happen with the sections)
		return (false);
	}
};

void CSectionBlobRenderer::SortSections()
{
	int minX = INT_MAX;
	int maxX = INT_MIN;
	int minY = INT_MAX;
	int maxY = INT_MIN;
	int minZ = INT_MAX;
	int maxZ = INT_MIN;

	// First we determine min and max for each axis so we can determine in which order we should sort them
	// We want to sort by the biggest axis first to the smallest.
	// That way when we split the sections by jobs, the sections are close to each others.
	// It does not matter if we have the smallest to the largest, or the other way around, as long as it is coherent.

	for (int i = 0 ; i < currentSection ; ++i)
	{
		Section * section = allSections[i];
		// TODO: Make a branch-less version of MIN and MAX
		minX = MIN(minX, section->m_nX);
		maxX = MAX(maxX, section->m_nX);
		minY = MIN(minY, section->m_nY);
		maxY = MAX(maxY, section->m_nY);
		minZ = MIN(minZ, section->m_nZ);
		maxZ = MAX(maxZ, section->m_nZ);
	}

	int diffX = maxX - minX;
	int diffY = maxY - minY;
	int diffZ = maxZ - minY;

	// I wonder is there is a more straightforward way to do this... It's a very small one time a frame cost though.
	int index = diffX > diffY ? 1 : 0;
	index |= diffX > diffZ ? 2 : 0;
	index |= diffY > diffZ ? 4 : 0;

	static const char SortCombinations[8][3]=
	{
		{	2,	1,	0	},		// 0 =>	Z > Y > X
		{	2,	0,	1	},		// 1 =>	Z > X > Y
		{	0,	1,	2	},		// 2 => X > Z, and Y > X, but Z > Y - Impossible - use XYZ in case
		{	0,	2,	1	},		// 3 => X > Z > Y
		{	1,	2,	0	},		// 4 => Y > Z > X
		{	0,	1,	2	},		// 5 => X > Y, and Y > Z, but Z > X - Impossible - use XYZ in case
		{	1,	0,	2	},		// 6 => Y > X > Z
		{	0,	1,	2,	}		// 7 => X > Y > Z
	};

	CSectionSorter sorter;
	sorter.m_Axis[0] = SortCombinations[index][0];
	sorter.m_Axis[1] = SortCombinations[index][1];
	sorter.m_Axis[2] = SortCombinations[index][2];

	// Then we sort. And sort only the current active sections (not the one cached).
	if (currentSection > 3)
	{
		// Sort only if that's big enough, otherwise sort() will corrupt the memory.
		allSections.sort(sorter, 0, currentSection - 1);
	}
}

#if (BLOB_FILL_MODE != BLOB_FILL_MODE_SYNCHRONOUS)
void CSectionBlobRenderer::RenderPass(CThreadData * pThreadData, int nNumberOfJobsToSpawn)
{
	// Render all the slices that have been seeded
	// We spawn all the threads

	IThreadPool * pThreadPools[2];

#if BLOB_CREATE_OWN_THREADPOOL
	pThreadPools[0] = m_pThreadPool;
	pThreadPools[1] = m_pThreadPool;
#else
#ifdef _X360
	pThreadPools[0] = g_pThreadPool;
	pThreadPools[1] = g_pAlternateThreadPool;
#else
	pThreadPools[0] = g_pThreadPool;
	pThreadPools[1] = g_pThreadPool;
#endif
#endif

	pThreadPools[0]->ResumeExecution();
	pThreadPools[1]->ResumeExecution();

	// We have to recreate the jobs every time.
	// And because they are reference counted, they could be released after they go out of scope.
	CBlobJob * * jobs = new CBlobJob *[nNumberOfJobsToSpawn];

	for (int i = 0 ; i < nNumberOfJobsToSpawn ; ++i)
	{
		jobs[i] = new CBlobJob;
		jobs[i]->m_pThreadData = &pThreadData[i];
		jobs[i]->SetPriority(JP_HIGH);
		jobs[i]->SetFlags(JF_QUEUE);

		int nThreadPoolIndex = (i & 1);
		pThreadPools[nThreadPoolIndex]->AddJob(jobs[i]);
	}

	// While we are waiting for the fragment buffer to be filled, let's calculate a bit.
	// On PC it should not matter much, but on X360 this should make a difference.
	// On PS3, the SPU are so fast that it won't matter anyway.

	bool allDone;

	bool bIsDoneTheFirstTime = false;

	do
	{
		allDone = true;

		bool fragmentsDrawn = m_FragmentedVertexBuffer.DrawFragments();
		if (fragmentsDrawn == false)
		{
			// Wait between drawing fragments
			ThreadPause();
		}

		for (int i = 0 ; i < nNumberOfJobsToSpawn ; ++i)
		{
			int isDone = ThreadInterlockedCompareExchange(&pThreadData[i].m_bDone, 0, 0);
			if (isDone == false)
				//	if (threadData[i].m_bDone == false)
				//if (jobs[i]->IsFinished() == false)
			{
				allDone = false;
			}
			else
			{
				if (bIsDoneTheFirstTime == false)
				{
					bIsDoneTheFirstTime = true;
					//	g_VProfCurrentProfile.EnterScope("BlobWastedTime", 0, "BlobWastedTime", false, BUDGETFLAG_OTHER);
				}
			}
		}
	}
	while (allDone == false);

	// Measure the time between done the first time and done last
	if (bIsDoneTheFirstTime)
	{
		//				g_VProfCurrentProfile.ExitScope(); 
	}

	// Release the jobs now that we don't need them
	for (int i = 0 ; i < nNumberOfJobsToSpawn ; ++i)
	{
		jobs[i]->Release();

#if BLOB_RESET_INSIDE_JOBS
		// And reset the sections for next time
		pThreadData[i].CheckSectionsAreReset(pThreadData[i].m_nStartSectionIndex, pThreadData[i].m_nEndSectionIndex);
#endif
	}

	// Now that we rendered all the sections once,
	// Let's see if there are some outside seeds to handle.
	// We are going to push this on the seed list again

	// Now that all the jobs are over, again let's make sure we sent all the vertex buffer fragments
	// This is tricky here. If we draw the fragments, it will reset the corresponding vertex buffer indices.
	// But because the cache is reset only once a frame (and not every iterations), redoing further calculation
	// will use the wrong vertex id. It is still better to keep the cache around between iterations
	// as these are small crawling with often the exact same corners involved (don't want to recalculate them again).
	m_FragmentedVertexBuffer.DrawFragments();
	m_FragmentedVertexBuffer.CheckResetState();

	pThreadPools[0]->SuspendExecution();
	pThreadPools[1]->SuspendExecution();
}

#else

void CSectionBlobRenderer::RenderPass(CThreadData * pThreadData, int nNumberOfJobsToSpawn)
{
	for (int i = 0 ; i < nNumberOfJobsToSpawn ; ++i)
	{
		// First we execute all the sections specific to this job (it is cache coherent and avoid potential collision with other jobs for most cases).
		pThreadData[i].ExecuteSections(pThreadData[i].m_nStartSectionIndex, pThreadData[i].m_nEndSectionIndex);
		// For similar reasons, let's try to crawl the sections internally
		pThreadData[i].CrawlSections(pThreadData[i].m_nStartSectionIndex, pThreadData[i].m_nEndSectionIndex);
	}

	// From now on, we do this for all available sections
	bool bWorkRemaining;
	// Although the job index doesn't matter at that point, still simulate it to discover potential issues
	int nJobIndex = 0;
	int nNumberOfIteration = 1;		// We did "1" iteration already
	do
	{
		bWorkRemaining = pThreadData[nJobIndex].ExecuteSections(0, pThreadData[nJobIndex].m_nTotalNumberOfSections);
		bWorkRemaining |= pThreadData[nJobIndex].CrawlSections(0, pThreadData[nJobIndex].m_nTotalNumberOfSections);
		// bWorkRemaining indicates that some work was needed or a section was locked (meaning another job was doing something).
		// In either cases, we have to do a full loop again to make sure that we cover everything.
		m_FragmentedVertexBuffer.DrawFragments();
		++nJobIndex;
		nJobIndex %= nNumberOfJobsToSpawn;
		++nNumberOfIteration;
	}
	while (bWorkRemaining);

	// Now that all the iterations are over, let's push the final fragments. We don't need the cache here anymore.
	for (int i = 0 ; i < nNumberOfJobsToSpawn ; ++i)
	{
		m_JobCaches[i].m_pFragmentedVertexBuffer->PushFragmentToDraw(m_JobCaches[i].m_Fragment);
		// And reset the sections for next time
		pThreadData[i].ResetSections(pThreadData[i].m_nStartSectionIndex, pThreadData[i].m_nEndSectionIndex);
	}

	// Now that all the jobs are over, again let's make sure we sent all the vertex buffer fragments
	// This is tricky here. If we draw the fragments, it will reset the corresponding vertex buffer indices.
	// But because the cache is reset only once a frame (and not every iterations), redoing further calculation
	// will use the wrong vertex id. It is still better to keep the cache around between iterations
	// as these are small crawling with often the exact same corners involved (don't want to recalculate them again).
	m_FragmentedVertexBuffer.DrawFragments();
	m_FragmentedVertexBuffer.CheckResetState();
}

#endif

void CSectionBlobRenderer::RenderSlices()
{
	if (currentSection == 0)
	{
		// Nothing to display...
		return;
	}
	SortSections();

	// At that point, all sections have been created (from the particles influence).
	// We have some checks to make sure no new sections are being created in this function.

	const int nNumberOfJobsToSpawn = MIN(r_paintblob_max_number_of_threads.GetInt(), BLOB_NUM_JOBS);
	// Use a static array (initially I used alloca here and in RenderSection() and that created random RTC / runtime issues  :( ).
	CThreadData threadData[BLOB_NUM_JOBS];

	m_bPolygonizationEnabled = true;

	// There is no notion of prev slice / cur slice or next slice here
	// We just draw each section.

	// Note:
	// To avoid the crawling back and forth, we could actually seed the particles in each section
	// (May not be more computing intensive as a particle would influence few sections at a time).
	// It could be done in parallel with filling the particles per bucket.
	// That way, all the seeds are inside, thus it may not be necessary to parse outside.

	// The final job may have less jobs than others (should be spawn slightly later too).
	int nNumberOfSectionsPerJob = (currentSection + nNumberOfJobsToSpawn - 1) / nNumberOfJobsToSpawn;
	int nNumberOfSectionsDispatched = 0;

	for (int i = 0 ; i < nNumberOfJobsToSpawn ; ++i)
	{
		CThreadData * currentThreadData = &threadData[i];

		currentThreadData->m_nCurrentJobIndex = i;
		currentThreadData->m_nStartSectionIndex = nNumberOfSectionsDispatched;
		int nNumberOfSectionsInThisJob = nNumberOfSectionsPerJob;
		if (nNumberOfSectionsDispatched + nNumberOfSectionsInThisJob > currentSection)
		{
			nNumberOfSectionsInThisJob = currentSection - nNumberOfSectionsDispatched;
		}
		currentThreadData->m_nEndSectionIndex = currentThreadData->m_nStartSectionIndex + nNumberOfSectionsInThisJob;

		currentThreadData->m_pAllSections = &allSections;
		currentThreadData->m_pJobCache = &m_JobCaches[i];
		currentThreadData->m_pSweepRenderer = this;
		currentThreadData->m_nTotalNumberOfSections = currentSection;

		currentThreadData->m_nNumberOfJobs = nNumberOfJobsToSpawn;
		currentThreadData->m_pAllThreadData = threadData;	// Address of the first job data

		threadData[i].m_bDone = false;
		threadData[i].m_bWorkRemaining = true;
		threadData[i].m_bStarted = false;

		// Get an available fragment.
		currentThreadData->m_pJobCache->m_Fragment = m_FragmentedVertexBuffer.GetAvailableFragment();

		nNumberOfSectionsDispatched += nNumberOfSectionsInThisJob;
	}
	Assert(nNumberOfSectionsDispatched == currentSection);

	RenderPass(threadData, nNumberOfJobsToSpawn);

	// Once we are done, let's clear the sections
	for (int i = 0 ; i < currentSection ; ++i)
	{
		Section * section = allSections[i];
		int sectionX = section->m_nX >> SECTION_X_POWER_OF_2;
		int sectionY = section->m_nY >> SECTION_Y_POWER_OF_2;
		int sectionZ = section->m_nZ >> SECTION_Z_POWER_OF_2;
		Assert((sectionX >= 0) && (sectionX < NUMBER_OF_SECTIONS_IN_X));
		Assert((sectionY >= 0) && (sectionY < NUMBER_OF_SECTIONS_IN_Y));
		Assert((sectionZ >= 0) && (sectionZ < NUMBER_OF_SECTIONS_IN_Z));
		m_Sections[(((sectionX * NUMBER_OF_SECTIONS_IN_Y) + sectionY) * NUMBER_OF_SECTIONS_IN_Z) + sectionZ] = &Section::s_ZeroSection;
#if BLOB_RESET_INSIDE_JOBS
		section->CheckSectionIsReset();
#endif
	}
#if _DEBUG
	// Check that all the sections have been cleared
	for (int i = 0 ; i < TOTAL_NUMBER_OF_SECTIONS ; ++i)
	{
		Assert(m_Sections[i] == &Section::s_ZeroSection);
	}
#endif
	// Reset for next frame
	currentSection = 0;
}

// Use these methods to track why some buckets seem not updated correctly from the neighbors sections.

void CSectionBlobRenderer::CheckBucketsInSection(Section & section)
{
#if _DEBUG
	// Let's check that what was calculated is correct
	// Go through the full range with the section (testing the overlap with the next sections, X+1, Y+1, Z+1).
	for (int globalX = 0 ; globalX < SECTION_X_SIZE + 1 ; ++globalX)
	{
		for (int globalY = 0 ; globalY < SECTION_Y_SIZE + 1 ; ++globalY)
		{
			for (int globalZ = 0 ; globalZ < SECTION_Z_SIZE + 1 ; ++globalZ)
			{
				if ((section.m_nX + globalX >= X_SIZE) || (section.m_nY + globalY >= Y_SIZE) || (section.m_nZ + globalZ >= Z_SIZE))
				{
					// We can't access outside the range, so skip the test in this case
					continue;
				}
				int officialBucketIndex = GetBucketIndexFromXYZ(section.m_nX + globalX, section.m_nY + globalY, section.m_nZ + globalZ);

				int bucketX = globalX / BUCKET_FACTOR;
				int bucketY = globalY / BUCKET_FACTOR;
				int bucketZ = globalZ / BUCKET_FACTOR;
				Assert(section.GetInternalBucket(bucketX, bucketY, bucketZ) == officialBucketIndex);
			}
		}
	}
#endif
}

void CSectionBlobRenderer::CheckBucketsInSectionUpTo(int index)
{
#if _DEBUG
	for (int i = 0 ; i < index ; ++i)
	{
		Section * previousSection = allSections[i];
		CheckBucketsInSection(*previousSection);
	}
#endif
}

bool CSectionBlobRenderer::CalcSign(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z)
{
	float totalf = 0.0f;

	int bucketIndex = GetBucketIndexFromXYZ(ix, iy, iz);
	CBucket * bucket = cacheBuckets[bucketIndex];

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

void CSectionBlobRenderer::CheckBucketAllocations(const int localCurCacheBucket)
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

// For each particle that we receive, we are going to:
//   Get or create the corresponding sections and buckets that are influenced by the particle.
//   Add the particle index to the bucket (if there is enough room).
void CSectionBlobRenderer::FillBuckets(int particleIndex, int minX, int maxX, int minY, int maxY, int minZ, int maxZ)
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

	// Get the section the first time, afterward it will be calculated as needed
	Section * section = NULL;
	bool getSection = true;

	// Set the flag to true, if xmin and xmax will not end up in the same section
	bool differentSectionInX = (((indexBucketMinX ^ indexBucketMaxX) & ~Section::BUCKETS_PER_SECTION_X_MASK) != 0);
	bool differentSectionInY = (((indexBucketMinY ^ indexBucketMaxY) & ~Section::BUCKETS_PER_SECTION_Y_MASK) != 0);
	bool differentSectionInZ = (((indexBucketMinZ ^ indexBucketMaxZ) & ~Section::BUCKETS_PER_SECTION_X_MASK) != 0);
	bool differentSectionInXYZ = differentSectionInX | differentSectionInY | differentSectionInZ;

	for (int x = indexBucketMinX ; x <= indexBucketMaxX ; ++x)
	{
		int offsetInSectionX = x & Section::BUCKETS_PER_SECTION_X_MASK;
		getSection |= (offsetInSectionX == 0);	// New section
		int indexX = offsetInSectionX * Section::BUCKETS_PER_SECTION_Y;

		for (int y = indexBucketMinY ; y <= indexBucketMaxY ; ++y)
		{
			int offsetInSectionY = y & Section::BUCKETS_PER_SECTION_Y_MASK;
			getSection |= (offsetInSectionY == 0);
			int indexY = (indexX + offsetInSectionY) * Section::BUCKETS_PER_SECTION_Z;

			for (int z = indexBucketMinZ ; z <= indexBucketMaxZ ; ++z)
			{
				int offsetInSectionZ = z & Section::BUCKETS_PER_SECTION_Z_MASK;
				getSection |= (offsetInSectionZ == 0);

				if (getSection)
				{
					// We calculate the section only when we switched section (i.e. we started with X = Y = Z = 0 in the section).
					// Because we increment X, Y and Z 1 by 1
					// We are still going to calculate sections much more than we would like (if a particle spawns over several sections).
					section = GetOrAllocateSection(x * BUCKET_FACTOR, y * BUCKET_FACTOR, z * BUCKET_FACTOR);;
					getSection = false;
				}

				int index = indexY + offsetInSectionZ;
				uint16 * ppBucket = &section->mBucketsInSection[index];
				uint16 bucketIndex = *ppBucket;

				if (bucketIndex == 0)
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
							const int NEXT_CACHE_LINE = 2 * CACHE_LINE_SIZE / sizeof(CBucket*);
							int index = localCurCacheBucket + NEXT_CACHE_LINE;
							if (index < cacheBuckets.size)
							{
								// Make sure that we only prefetch some memory addressable by the array
								// It should not be a problem to prefetch, except if we were using virtual allocations, etc...
								PREFETCH_128(&cacheBuckets[index], 0);
							}
						}
					}

					bucketIndex = localCurCacheBucket++;
					CBucket * bucket = cacheBuckets[bucketIndex];
					bucket->SetXYZ(x * BUCKET_FACTOR, y * BUCKET_FACTOR, z * BUCKET_FACTOR);

					*ppBucket = bucketIndex;

					// Specialize the code for the first particle , to reduce one LHS (when resetting m_nNumParticles and incrementing it below).
					bucket->m_ParticlesIndex[0] = particleIndex;
					bucket->m_nNumParticles = 1;
					for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
					{
						bucket->m_pCorners[i] = NULL;
					}
				}
				else
				{
					CBucket * bucket = cacheBuckets[bucketIndex];

					Assert((x * BUCKET_FACTOR) == bucket->m_nX);
					Assert((y * BUCKET_FACTOR) == bucket->m_nY);
					Assert((z * BUCKET_FACTOR) == bucket->m_nZ);

					// Unroll the test to reduce one LHS (after m_nNumParticles has been reset above)
					int numParticles = bucket->m_nNumParticles;
					if (numParticles < CBucket::MAX_PARTICLES_PER_BUCKET)
					{
						bucket->m_ParticlesIndex[numParticles] = particleIndex;
						bucket->m_nNumParticles = numParticles + 1;
					}
				}
				getSection |= differentSectionInXYZ;
			}
		}
	}
	curCacheBucket = localCurCacheBucket;
}

class CSorter
{
public:
	FORCEINLINE
	bool gt(ImpParticle* a, ImpParticle* b)
	{
		// Instead of sorting by X, we sort by weight
		// Bigger weight first (so they are added in more priority in each bucket).
		return a->scale > b->scale;
	}
};

void CSectionBlobRenderer::BuildCache(float cw, float fieldRadius, Point3D & mins, Point3D & maxs)
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

}
}

