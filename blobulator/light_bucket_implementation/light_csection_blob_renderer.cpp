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

#include "blobulator/light_bucket_implementation/light_csection_blob_renderer.h"
#include "blobulator/light_bucket_implementation/light_ccorner_info.h"
#include "blobulator/light_bucket_implementation/light_cthread_data.h"
#include "blobulator/light_bucket_implementation/light_cfragmented_vertex_buffer.h"
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

#if (BLOB_FILL_MODE == BLOB_FILL_MODE_SPU) || (BLOB_FILL_MODE == BLOB_FILL_MODE_EMULATE_SPU)
#include "light_vjob_instance.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern const int32 ALIGN16 g_SIMD_ComponentMask[4][4] ALIGN16_POST;
extern ConVar r_paintblob_max_number_of_threads;

extern ConVar r_paintblob_debug_spu;

ConVar r_paintblob_seeding_with_simd("r_paintblob_seeding_with_simd", "0", FCVAR_NONE, "Set this to 1 to run the seeding in SIMD. Otherwise use 0.");

#if _DEBUG
ConVar r_paintblob_timeout_for_recycling_fragments("r_paintblob_timeout_for_recycling_fragments", "1000", FCVAR_NONE, "Timeout in milliseconds used to recycle the fragments. Default is 64 in release, 1000 in debug.");
#else
// 64 represents two 30 fps frames. So even if we drop at 15 fps we should still not recycle.
ConVar r_paintblob_timeout_for_recycling_fragments("r_paintblob_timeout_for_recycling_fragments", "100", FCVAR_NONE, "Timeout in milliseconds used to recycle the fragments. Default is 64 in release, 1000 in debug.");
#endif


// Convars relating to Blobulator::LightBucketImplementation::CSectionPool
ConVar blob_csection_pool_enable( "blob_csection_pool_enable", "1", FCVAR_DEVELOPMENTONLY, "When enabled, the blob renderer uses a shared pool to recycle CSection objects, which drastically reduces memory usage" );
ConVar blob_csection_pool_size( "blob_csection_pool_size", "4000", FCVAR_DEVELOPMENTONLY, "The maximum number of CSections to be recycled in the shared pool (smaller values save memory but increase CPU cost)" );
ConVar blob_csection_pool_free_per_frame( "blob_csection_pool_free_per_frame", "15", FCVAR_DEVELOPMENTONLY, "The blob renderer will free this many CSections from the shared pool each frame (larger values save memory but increase CPU cost)" );
ConVar blob_csection_pool_block_size( "blob_csection_pool_block_size", "100", FCVAR_DEVELOPMENTONLY, "The blob renderer will allocate/free CSections in blocks of this size (smaller values may save some memory but will definitely increase CPU cost)" );
ConVar blob_csection_pool_spew( "blob_csection_pool_spew", "0", FCVAR_DEVELOPMENTONLY, "Spew usage stats about the blob renderer's shared CSection pool (more data will be available if blobulator is built in DEBUG)" );


namespace Blobulator
{
namespace LightBucketImplementation
{

CSection ALIGN16 CSectionBlobRenderer::s_ZeroSection;

LightweightRenderer::LightweightRenderer(CFragmentedVertexBuffer & fragmentedVertexBuffer, bool bDispatchToVertexBuffer)
	:
	m_bDispatchToVertexBuffer(bDispatchToVertexBuffer),
	m_FragmentedVertexBuffer(fragmentedVertexBuffer),
	m_offset(0,0,0),
	m_bConstantsCalculated(false)
{
	static bool sFirstTime = true;
	if (sFirstTime)
	{
		Blobulator::Constants::InitializeConstants();
		sFirstTime = false;
	}
}

LightweightRenderer::~LightweightRenderer()
{
	// Do nothing...
}

void LightweightRenderer::BeginFrame(bool bTileMode, void * pRenderContext)
{
	m_bTileMode = bTileMode;
	RecalculateConstants();

	if (m_bDispatchToVertexBuffer)
	{
		m_FragmentedVertexBuffer.BeginFrame(pRenderContext);
	}
}

void LightweightRenderer::EndFrame()
{
	if (m_bDispatchToVertexBuffer)
	{
		m_FragmentedVertexBuffer.EndFrame();
	}
}

void LightweightRenderer::BeginTile(const Point3D & offset)
{
	m_offset = offset;
	if (m_bDispatchToVertexBuffer)
	{
		m_FragmentedVertexBuffer.BeginTile();
	}
}

void LightweightRenderer::DrawTile()
{
	Assert(false);		// Not supported by this implementation. You may want to use CSectionBlobRenderer instead.
}

void LightweightRenderer::EndTile()
{
	if (m_bDispatchToVertexBuffer)
	{
		m_FragmentedVertexBuffer.EndTile();
	}
}

void LightweightRenderer::AddParticle(ImpParticle & particle, bool bSkipBoundsCheck)
{
	Assert(false);	// Not supported by this implementation. You may want to use CSectionBlobRenderer instead.
}

void LightweightRenderer::SetCubeWidth(float fCubeWidth)
{
	m_fCubeWidth = fCubeWidth;
	m_bConstantsCalculated = false;
}

float LightweightRenderer::GetCubeWidth() const
{
	return m_fCubeWidth;
}

void LightweightRenderer::SetRenderRadius(float fRenderRadius)
{
	m_RenderRadius = fRenderRadius;
	m_bConstantsCalculated = false;
}

float LightweightRenderer::GetRenderRadius() const
{
	return m_RenderRadius;
}

void LightweightRenderer::SetCutoffRadius(float fCutoffRadius)
{
	m_fCutoffRadius = fCutoffRadius;
	m_bConstantsCalculated = false;
}

float LightweightRenderer::GetCutoffRadius() const
{
	return m_fCutoffRadius;
}

const Point3D & LightweightRenderer::GetInnerDimensions() const
{
	if (m_bConstantsCalculated == false)
	{
		RecalculateConstants();
	}
	return m_InnerDimensions;
}

float LightweightRenderer::GetMarginWidth() const
{
	if (m_bConstantsCalculated == false)
	{
		RecalculateConstants();
	}
	return m_fMarginWidth;
}

void LightweightRenderer::SetPortalMatrices(const matrix3x4_t * pMatrices, int nNumberOfMatrices)
{
	Assert(false);		// Not supported by this implementation. You may want to use CSectionBlobRenderer instead.
}

void LightweightRenderer::SetDebugMode(bool bDebugMode)
{
	m_bDebugMode = bDebugMode;
}

bool LightweightRenderer::GetDebugMode() const
{
	return m_bDebugMode;
}

int LightweightRenderer::GetNumberOfDebugInfo() const
{
	// Not supported yet
	return 0;
}

void LightweightRenderer::DrawDebugInfo(IVDebugOverlay & debugOverlay, int nDebugInfoIndex)
{
	// Do nothing...
}

int LightweightRenderer::GetDebugId() const
{
	return (int)(this) & 0xffff;
}

void LightweightRenderer::RecalculateConstants() const
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
	m_f4WOnly = LoadAlignedSIMD( g_SIMD_ComponentMask[3] );

	m_f4Threshold = ReplicateX4(m_fThreshold);
	m_f4Threshold = AndSIMD( m_f4Threshold, m_f4WOnly );

	m_f4Fourpt25s = ReplicateX4( 0.25f );
	m_f4scalerSq = ReplicateX4( m_fScalerSq );
	m_f4cutoffRSq = ReplicateX4( m_fCutoffRadiusSq);

	m_bConstantsCalculated = true;
}

#if BLOB_CREATE_OWN_THREADPOOL
IThreadPool * CSectionBlobRenderer::s_pThreadPool = NULL;
int CSectionBlobRenderer::s_nCounterInstances = 0;
#endif

CSectionBlobRenderer::CSectionBlobRenderer(CFragmentedVertexBuffer & fragmentedVertexBuffer, CSectionPool &sectionPool)
:
	LightweightRenderer(fragmentedVertexBuffer, false),
	m_SectionPool(sectionPool),
	m_nMaxNoSlicesToDraw(-1),
	m_nCurrentCacheSection(0),
	m_nNumberOfParticles(0),
	m_pPortalMatrices(NULL),
	m_nNumberOfMatrices(0)
{
	m_nStackSize = 0;

	for (int i = 0 ; i < sizeof(m_nSectionIndices) / sizeof(m_nSectionIndices[0]) ; ++i)
	{
		m_nSectionIndices[i] = 0;
	}

	// Initialize indices, so buckets have their corresponding cache, that is vertex buffer friendly.
	CLockLessStack<BLOB_NUMBER_OF_FRAGMENTS> * pOutFragmentsAvailable;
	CLockLessStack<BLOB_NUMBER_OF_FRAGMENTS> * pOutFragmentsToDisplay;
	CFragmentInfo * pOutAllFragments;
	m_nSlotIndexForFragmentVB = m_FragmentedVertexBuffer.RetrievePointers(&pOutFragmentsAvailable, &pOutFragmentsToDisplay, &pOutAllFragments);
	for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
	{
		m_JobCaches[i].Initialize(*pOutFragmentsAvailable, *pOutFragmentsToDisplay, pOutAllFragments);
		m_JobCaches[i].m_nJobIndex = i;

		if (i == 0)
		{
			// Only the first job needs this (can draw fragments while waiting for some available).
			m_JobCaches[i].m_pFragmentedVertexBuffer = &m_FragmentedVertexBuffer;
		}
		else
		{
			m_JobCaches[i].m_pFragmentedVertexBuffer = NULL;
		}
		m_JobCaches[i].m_nSlotIndexForFragmentVB = m_nSlotIndexForFragmentVB;
	}

#if BLOB_CREATE_OWN_THREADPOOL
	if (s_nCounterInstances == 0)
	{
		s_pThreadPool = CreateNewThreadPool();

		ThreadPoolStartParams_t startParams;
		startParams.nThreads = BLOB_NUM_JOBS - 1;
		startParams.iThreadPriority = 0;
		startParams.nStackSize = 32 * 1024;
		startParams.fDistribute = TRS_TRUE;

		if (IsX360())
		{
			startParams.bUseAffinityTable = true;
			startParams.iAffinityTable[0] = XBOX_PROCESSOR_2;
			startParams.iAffinityTable[1] = XBOX_PROCESSOR_3;
			startParams.iAffinityTable[2] = XBOX_PROCESSOR_4;
			startParams.iAffinityTable[3] = XBOX_PROCESSOR_5;
		}

		s_pThreadPool->Start(startParams, "BlobulatorJobs");
	}
	s_nCounterInstances++;
#endif

	// The first bucket is always the zero bucket, so new bucket index starts at 1 and not 0.

	// The first section is always the zero section, so new section index starts at 1 and not 0.
	m_CacheSections.pushAutoSize(&s_ZeroSection);
	++m_nCurrentCacheSection;

	SectionStack::Container * pContainer;
	for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
	{
		pContainer = new SectionStack::Container();
		m_SectionStacks[i].SetContainer(pContainer);
	}

#if (BLOB_FILL_MODE == BLOB_FILL_MODE_SPU) || (BLOB_FILL_MODE == BLOB_FILL_MODE_EMULATE_SPU)
	m_pJobInstances = new BlobulatorVJobInstance[BLOB_NUM_JOBS];
	for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
	{
		m_pJobInstances[i].Init();
	}
#endif

	pContainer = new SectionStack::Container();
	m_GlobalSectionStack.SetContainer(pContainer);
}

CSectionBlobRenderer::~CSectionBlobRenderer()
{
#if (BLOB_FILL_MODE == BLOB_FILL_MODE_SPU) || (BLOB_FILL_MODE == BLOB_FILL_MODE_EMULATE_SPU)
	for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
	{
		m_pJobInstances[i].Shutdown();
	}
	delete[] m_pJobInstances;
	m_pJobInstances = NULL;
#endif

	for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
	{
		Assert(m_SectionStacks[i].IsEmpty());
		delete m_SectionStacks[i].GetContainer();
	}

	Assert(m_GlobalSectionStack.IsEmpty());
	delete m_GlobalSectionStack.GetContainer();

#if BLOB_CREATE_OWN_THREADPOOL
	Assert(s_nCounterInstances > 0);
	--s_nCounterInstances;
	if (s_nCounterInstances == 0)
	{
		s_pThreadPool->Stop();
		DestroyThreadPool(s_pThreadPool);
		s_pThreadPool = NULL;
	}
#endif

	// The first section is always the zero section, so new section index starts at 1 and not 0.
	m_SectionPool.ReleaseSections( m_CacheSections, 1 );
}

void CSectionBlobRenderer::BeginFrame(bool bTileMode, void * pRenderContext)
{
	VPROF_BUDGET( "CSectionBlobRenderer::BeginFrame", "Blobulator" );

	LightweightRenderer::BeginFrame(bTileMode, pRenderContext);

	for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
	{
		m_JobCaches[i].m_f4Fourpt25s = m_f4Fourpt25s;
		m_JobCaches[i].m_f4scalerSq = m_f4scalerSq;
		m_JobCaches[i].m_f4cutoffRSq = m_f4cutoffRSq;
		m_JobCaches[i].m_f4Threshold = m_f4Threshold;

		m_JobCaches[i].SetPortalMatrices(m_pPortalMatrices, m_nNumberOfMatrices);
		m_JobCaches[i].BeginFrame();
	}
}

void CSectionBlobRenderer::EndFrame()
{
	VPROF_BUDGET( "CSectionBlobRenderer::EndFrame", "Blobulator" );

	for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
	{
		m_JobCaches[i].EndFrame();
	}

	LightweightRenderer::EndFrame();

	// Return CSections to a shared pool, to keep memory usage as low as possible (since we have multiple
	// CSectionBlobRenderers per frame, and have multiple frames in flight, all using many CSections!)
	// The first section is always the zero section, so new section index starts at 1 and not 0.
	if ( blob_csection_pool_enable.GetBool() )
		m_SectionPool.ReleaseSections( m_CacheSections, 1 );
}

void CSectionBlobRenderer::BeginTile(const Point3D & offset)
{
	VPROF_BUDGET( "CSectionBlobRenderer::BeginTile", "Blobulator" );

	LightweightRenderer::BeginTile(offset);

	RecalculateBoundingBoxes();

	for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
	{
		m_JobCaches[i].BeginTile();
	}
	// Reset the state for this coming tile...
	m_nNumberOfParticles = 0;
	m_nCurrentCacheSection = 1;
}

void CSectionBlobRenderer::DrawTile()
{
	VPROF_BUDGET( "CSectionBlobRenderer::DrawTile", "Blobulator" );

	// Do not call LightweightRenderer::DrawTile() on purpose.

	BuildCache();
	SeedSurface();
	SortSections();

	for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
	{
		m_JobCaches[i].ResetBetweenFrames(m_OuterBBMins, m_fCubeWidth, m_fThreshold, m_Particles, m_nNumberOfParticles, m_nMarginNCubes, m_bPolygonizationEnabled, m_bTileMode);
	}
	StartRenderSlices();
}

void CSectionBlobRenderer::EndTile()
{
	VPROF_BUDGET( "CSectionBlobRenderer::EndTile", "Blobulator" );

	EndRenderSlices();

	for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
	{
		m_JobCaches[i].EndTile();
	}

	LightweightRenderer::EndTile();
}

void CSectionBlobRenderer::SetPortalMatrices(const matrix3x4_t * pMatrices, int nNumberOfMatrices)
{
	m_pPortalMatrices = pMatrices;
	m_nNumberOfMatrices = nNumberOfMatrices;
}

void CSectionBlobRenderer::AddParticle(ImpParticle & particle, bool bSkipBoundsCheck)
{
	if (bSkipBoundsCheck || IsParticleWithinBounds(particle))
	{
		if (m_nNumberOfParticles < MAX_NUMBER_OF_PARTICLES)
		{
			m_Particles[m_nNumberOfParticles++] = particle;
		}
		else
		{
			Warning("[Blobulator] Too many particles are added to a tile. Some particles will be discarded, this may result in holes between tiles.\n");
		}
	}
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

// Create specialized version so we don't recalculate / test everything every time.
// This implementation could be optimized further if needed.
// I hate to duplicate this code. Figure out something else.
// BEWARE THAT THIS CODE IS VERY TOUCHY. ANY INCORRECT CHANGES COULD CREATE HOLES IN THE BLOBULATOR IN SOME SPECIFIC CASES.
// Cases to test: sp_a4_jump_polarity, Blob as a long bar in X, Y or Z, and sp_a3_speed_paint.
// There are some asserts to check that the calculation stays accurate, so use the debug version to validate any changes here.

// SIMD version

template <int STEP>
bool CSectionBlobRenderer::SeedX(int nStartX, int nStartY, int nStartZ, int nMaxIterations, fltx4 f4Position, fltx4 f4Step)
{
	if ((nStartY < 0) || (nStartY > Y_SIZE) || (nStartZ < 0) || (nStartZ > Z_SIZE))
	{
		// Outside the range anyway...
		return false;
	}

	// We assume that the current position has already been calculated. Skip it.
	f4Position = AddSIMD(f4Position, f4Step);
	int nMinX = nStartX + STEP;
	int nMaxX = nStartX + (STEP * nMaxIterations);
	// nMinX is not necessarily smaller than nMaxX (depends of the step, but we still want to go from min to max).

	if (nMinX < nMaxX)
	{
		if (nMinX > X_SIZE)
		{
			return false;
		}
		if (nMinX < 0)
		{
			fltx4 fMul = ReplicateX4((float)-nMinX);
			f4Position = MaddSIMD(f4Step, fMul, f4Position);
			nMinX = 0;
		}
		if (nMaxX > X_SIZE)
		{
			nMaxX = X_SIZE;
		}
	}
	else
	{
		if (nMinX < 0)
		{
			return false;
		}
		if (nMinX > X_SIZE)
		{
			fltx4 fMul = ReplicateX4((float)nMinX-X_SIZE);
			f4Position = MaddSIMD(f4Step, fMul, f4Position);
			nMinX = X_SIZE;
		}
		if (nMaxX < 0)
		{
			nMaxX = 0;
		}
	}

	// This code allows seeding issue while being as fast as the original code in most cases.
	// CalcSign() is using SIMD to speed things up a bit.
	for(int x = nMinX ; x != nMaxX ; x += STEP)
	{
		CSection * pSection = GetSection(x, nStartY, nStartZ);
		if (pSection != &s_ZeroSection)
		{
			bool bResult = CalcSign(*pSection, f4Position);
			Assert(CalcSign(*pSection, f4Position, x, nStartY, nStartZ) == bResult);
			if (bResult == false)
			{
				// Here it means that we switched from one side of the surface to the other side.
				// Depending of the direction we are pushing, the cube that we place may be completely outside of the surface.
				// As the cubes are calculated with +1, we need to take the min of both.
				CSection * pSeedSection = pSection;

				// We should actually be able to remove this test (after all x is in the range, and the previous value as well - except for the first iteration).
				int nSeedX;
				XYZ seed;
				if (STEP < 0)
				{
					nSeedX = x;		// This is the simpler case that should be done first
				}
				else
				{
					nSeedX = x - STEP;
					if ((nSeedX < 0) || (nSeedX > X_SIZE))		// This test should not be necessary, but we are taking our precautions here
					{
						// The good seed is outside the range, let's push the other value that is inside the range
						// (hopefully we will still have some good crawling values)
						seed = MakeXYZ(x, nStartY, nStartZ);
						pSection->PushSeed(seed);
						return true;
					}

					// Seed is in the range...
					int nMask = x ^ nSeedX;
					nMask &= ~SECTION_X_MASK;		// We don't care about the low part, we just want to check the high-part
					if (nMask != 0)
					{
						// It means that the seed we are interested in is in another section, let's retrieve it
						pSeedSection = GetSection(nSeedX, nStartY, nStartZ);
						if (pSeedSection == &s_ZeroSection)
						{
							// Somehow the seed section does not exist (outside the range of the particle?).
							// Use the other section, hoping that it will reach the section as needed...
							seed = MakeXYZ(x, nStartY, nStartZ);
							pSection->PushSeed(seed);
							return true;
						}
					}
				}

				seed = MakeXYZ(nSeedX, nStartY, nStartZ);
				pSeedSection->PushSeed(seed);
				return true;
			}
		}

		f4Position = AddSIMD(f4Position, f4Step);
	}
	return false;
}

template <int STEP>
bool CSectionBlobRenderer::SeedY(int nStartX, int nStartY, int nStartZ, int nMaxIterations, fltx4 f4Position, fltx4 f4Step)
{
	if ((nStartX < 0) || (nStartX > X_SIZE) || (nStartZ < 0) || (nStartZ > Z_SIZE))
	{
		// Outside the range anyway...
		return false;
	}

	// We assume that the current position has already been calculated. Skip it.
	f4Position = AddSIMD(f4Position, f4Step);
	int nMinY = nStartY + STEP;
	int nMaxY = nStartY + (STEP * nMaxIterations);
	// nMinX is not necessarily smaller than nMaxX (depends of the step, but we still want to go from min to max).

	if (nMinY < nMaxY)
	{
		if (nMinY > Y_SIZE)
		{
			return false;
		}
		if (nMinY < 0)
		{
			fltx4 fMul = ReplicateX4((float)-nMinY);
			f4Position = MaddSIMD(f4Step, fMul, f4Position);
			nMinY = 0;
		}
		if (nMaxY > Y_SIZE)
		{
			nMaxY = Y_SIZE;
		}
	}
	else
	{
		if (nMaxY < 0)
		{
			return false;
		}
		if (nMinY > Y_SIZE)
		{
			fltx4 fMul = ReplicateX4((float)nMinY-Y_SIZE);
			f4Position = MaddSIMD(f4Step, fMul, f4Position);
			nMinY = Y_SIZE;
		}
		if (nMinY < 0)
		{
			nMinY = 0;
		}
	}

	for(int y = nMinY ; y != nMaxY ; y += STEP)
	{
		CSection * pSection = GetSection(nStartX, y, nStartZ);
		if (pSection != &s_ZeroSection)
		{
			bool bResult = CalcSign(*pSection, f4Position);
			Assert(CalcSign(*pSection, f4Position, nStartX, y, nStartZ) == bResult);
			if (bResult == false)
			{
				// Here it means that we switched from one side of the surface to the other side.
				// Depending of the direction we are pushing, the cube that we place may be completely outside of the surface.
				// As the cubes are calculated with +1, we need to take the min of both.
				CSection * pSeedSection = pSection;

				int nSeedY;
				XYZ seed;
				if (STEP < 0)
				{
					nSeedY = y;		// This is the simpler case that should be done first
				}
				else
				{
					nSeedY = y - STEP;
					if ((nSeedY < 0) || (nSeedY > Y_SIZE))		// This test should not be necessary, but we are taking our precautions here
					{
						// The good seed is outside the range, let's push the value that is inside the range (hopefully we will still have some good crawling values)
						seed = MakeXYZ(nStartX, y, nStartZ);
						pSection->PushSeed(seed);
						return true;
					}

					// Seed is in the range...
					int nMask = y ^ nSeedY;
					nMask &= ~SECTION_Y_MASK;		// We don't care about the low part, we just want to check the high-part
					if (nMask != 0)
					{
						// It means that the seed we are interested in is in another section, let's retrieve it
						pSeedSection = GetSection(nStartX, nSeedY, nStartZ);
						if (pSeedSection == &s_ZeroSection)
						{
							// Somehow the seed section does not exist (outside the range of the particle?).
							// Use the other section, hoping that it will reach the section as needed...
							seed = MakeXYZ(nStartX, y, nStartZ);
							pSection->PushSeed(seed);
							return true;
						}
					}
				}

				seed = MakeXYZ(nStartX, nSeedY, nStartZ);
				pSeedSection->PushSeed(seed);
				return true;
			}
		}
		f4Position = AddSIMD(f4Position, f4Step);
	}
	return false;
}

template <int STEP>
bool CSectionBlobRenderer::SeedZ(int nStartX, int nStartY, int nStartZ, int nMaxIterations, fltx4 f4Position, fltx4 f4Step)
{
	if ((nStartX < 0) || (nStartX > X_SIZE) || (nStartY < 0) || (nStartY > Y_SIZE))
	{
		// Outside the range anyway...
		return false;
	}

	// We assume that the current position has already been calculated. Skip it.
	f4Position = AddSIMD(f4Position, f4Step);
	int nMinZ = nStartZ + STEP;
	int nMaxZ = nStartZ + (STEP * nMaxIterations);
	// nMinX is not necessarily smaller than nMaxX (depends of the step, but we still want to go from min to max).

	if (nMinZ < nMaxZ)
	{
		if (nMinZ > Z_SIZE)
		{
			return false;
		}
		if (nMinZ < 0)
		{
			fltx4 fMul = ReplicateX4((float)-nMinZ);
			f4Position = MaddSIMD(f4Step, fMul, f4Position);
			nMinZ = 0;
		}
		if (nMaxZ > Z_SIZE)
		{
			nMaxZ = Z_SIZE;
		}
	}
	else
	{
		if (nMaxZ < 0)
		{
			return false;
		}
		if (nMinZ > Z_SIZE)
		{
			fltx4 fMul = ReplicateX4((float)nMinZ - Z_SIZE);
			f4Position = MaddSIMD(f4Step, fMul, f4Position);
			nMinZ = Z_SIZE;
		}
		if (nMinZ < 0)
		{
			nMinZ = 0;
		}
	}

	for(int z = nMinZ ; z != nMaxZ ; z += STEP)
	{
		CSection * pSection = GetSection(nStartX, nStartY, z);
		if (pSection != &s_ZeroSection)
		{
			bool bResult = CalcSign(*pSection, f4Position);
			Assert(CalcSign(*pSection, f4Position, nStartX, nStartY, z) == bResult);
			if (bResult == false)
			{
				// Here it means that we switched from one side of the surface to the other side.
				// Depending of the direction we are pushing, the cube that we place may be completely outside of the surface.
				// As the cubes are calculated with +1, we need to take the min of both.
				CSection * pSeedSection = pSection;

				int nSeedZ;
				XYZ seed;
				if (STEP < 0)
				{
					nSeedZ = z;			// This is the simpler case that should be done first
				}
				else
				{
					nSeedZ = z - STEP;
					if ((nSeedZ < 0) || (nSeedZ > Z_SIZE))		// This test should not be necessary, but we are taking our precautions here
					{
						// The good seed is outside the range, let's push the value that is inside the range (hopefully we will still have some good crawling values)
						seed = MakeXYZ(nStartX, nStartY, z);
						pSection->PushSeed(seed);
						return true;
					}

					// Seed is in the range...
					int nMask = z ^ nSeedZ;
					nMask &= ~SECTION_Z_MASK;		// We don't care about the low part, we just want to check the high-part
					if (nMask != 0)
					{
						// It means that the seed we are interested in is in another section, let's retrieve it
						pSeedSection = GetSection(nStartX, nStartY, nSeedZ);
						if (pSeedSection == &s_ZeroSection)
						{
							// Somehow the seed section does not exist (outside the range of the particle?).
							// Use the other section, hoping that it will reach the section as needed...
							seed = MakeXYZ(nStartX, nStartY, z);
							pSection->PushSeed(seed);
							return true;
						}
					}
				}

				seed = MakeXYZ(nStartX, nStartY, nSeedZ);
				pSeedSection->PushSeed(seed);
				return true;
			}
		}
		f4Position = AddSIMD(f4Position, f4Step);
	}
	return false;
}

// Non-SIMD version

template <int STEP>
bool CSectionBlobRenderer::SeedX(int nStartX, int nStartY, int nStartZ, int nMaxIterations, float fX, const float fY, const float fZ)
{
	if ((nStartY < 0) || (nStartY > Y_SIZE) || (nStartZ < 0) || (nStartZ > Z_SIZE))
	{
		// Outside the range anyway...
		return false;
	}

	// We assume that the current position has already been calculated. Skip it.
	fX += m_fCubeWidth * (float)STEP;
	int nMinX = nStartX + STEP;
	int nMaxX = nStartX + (STEP * nMaxIterations);
	// nMinX is not necessarily smaller than nMaxX (depends of the step, but we still want to go from min to max).

	if (nMinX < nMaxX)
	{
		if (nMinX > X_SIZE)
		{
			return false;
		}
		if (nMinX < 0)
		{
			fX += m_fCubeWidth * (float)(-nMinX);
			nMinX = 0;
		}
		if (nMaxX > X_SIZE)
		{
			nMaxX = X_SIZE;
		}
	}
	else
	{
		if (nMinX < 0)
		{
			return false;
		}
		if (nMinX > X_SIZE)
		{
			fX -= m_fCubeWidth * (float)(nMinX - X_SIZE);
			nMinX = X_SIZE;
		}
		if (nMaxX < 0)
		{
			nMaxX = 0;
		}
	}

	// This code allows seeding issue while being as fast as the original code in most cases.
	// CalcSign() is using SIMD to speed things up a bit.
	for(int x = nMinX ; x != nMaxX ; x += STEP)
	{
		CSection * pSection = GetSection(x, nStartY, nStartZ);
		if (pSection != &s_ZeroSection)
		{
			bool bResult = CalcSign(*pSection, fX, fY, fZ);
			if (bResult == false)
			{
				// Here it means that we switched from one side of the surface to the other side.
				// Depending of the direction we are pushing, the cube that we place may be completely outside of the surface.
				// As the cubes are calculated with +1, we need to take the min of both.
				CSection * pSeedSection = pSection;

				// We should actually be able to remove this test (after all x is in the range, and the previous value as well - except for the first iteration).
				int nSeedX;
				XYZ seed;
				if (STEP < 0)
				{
					nSeedX = x;		// This is the simpler case that should be done first
				}
				else
				{
					nSeedX = x - STEP;
					if ((nSeedX < 0) || (nSeedX > X_SIZE))		// This test should not be necessary, but we are taking our precautions here
					{
						// The good seed is outside the range, let's push the other value that is inside the range
						// (hopefully we will still have some good crawling values)
						seed = MakeXYZ(x, nStartY, nStartZ);
						pSection->PushSeed(seed);
						return true;
					}

					// Seed is in the range...
					int nMask = x ^ nSeedX;
					nMask &= ~SECTION_X_MASK;		// We don't care about the low part, we just want to check the high-part
					if (nMask != 0)
					{
						// It means that the seed we are interested in is in another section, let's retrieve it
						pSeedSection = GetSection(nSeedX, nStartY, nStartZ);
						if (pSeedSection == &s_ZeroSection)
						{
							// Somehow the seed section does not exist (outside the range of the particle?).
							// Use the other section, hoping that it will reach the section as needed...
							seed = MakeXYZ(x, nStartY, nStartZ);
							pSection->PushSeed(seed);
							return true;
						}
					}
				}

				seed = MakeXYZ(nSeedX, nStartY, nStartZ);
				pSeedSection->PushSeed(seed);
				return true;
			}
		}

		fX += m_fCubeWidth * (float)STEP;
	}
	return false;
}

template <int STEP>
bool CSectionBlobRenderer::SeedY(int nStartX, int nStartY, int nStartZ, int nMaxIterations, const float fX, float fY, const float fZ)
{
	if ((nStartX < 0) || (nStartX > X_SIZE) || (nStartZ < 0) || (nStartZ > Z_SIZE))
	{
		// Outside the range anyway...
		return false;
	}

	// We assume that the current position has already been calculated. Skip it.
	fY += m_fCubeWidth * (float)STEP;
	int nMinY = nStartY + STEP;
	int nMaxY = nStartY + (STEP * nMaxIterations);
	// nMinX is not necessarily smaller than nMaxX (depends of the step, but we still want to go from min to max).

	if (nMinY < nMaxY)
	{
		if (nMinY > Y_SIZE)
		{
			return false;
		}
		if (nMinY < 0)
		{
			fY += m_fCubeWidth * (float)(-nMinY);
			nMinY = 0;
		}
		if (nMaxY > Y_SIZE)
		{
			nMaxY = Y_SIZE;
		}
	}
	else
	{
		if (nMaxY < 0)
		{
			return false;
		}
		if (nMinY > Y_SIZE)
		{
			fY -= 
			fY -= m_fCubeWidth * (float)(nMinY - Y_SIZE);
			nMinY = Y_SIZE;
		}
		if (nMinY < 0)
		{
			nMinY = 0;
		}
	}

	for(int y = nMinY ; y != nMaxY ; y += STEP)
	{
		CSection * pSection = GetSection(nStartX, y, nStartZ);
		if (pSection != &s_ZeroSection)
		{
			bool bResult = CalcSign(*pSection, fX, fY, fZ);
			if (bResult == false)
			{
				// Here it means that we switched from one side of the surface to the other side.
				// Depending of the direction we are pushing, the cube that we place may be completely outside of the surface.
				// As the cubes are calculated with +1, we need to take the min of both.
				CSection * pSeedSection = pSection;

				int nSeedY;
				XYZ seed;
				if (STEP < 0)
				{
					nSeedY = y;		// This is the simpler case that should be done first
				}
				else
				{
					nSeedY = y - STEP;
					if ((nSeedY < 0) || (nSeedY > Y_SIZE))		// This test should not be necessary, but we are taking our precautions here
					{
						// The good seed is outside the range, let's push the value that is inside the range (hopefully we will still have some good crawling values)
						seed = MakeXYZ(nStartX, y, nStartZ);
						pSection->PushSeed(seed);
						return true;
					}

					// Seed is in the range...
					int nMask = y ^ nSeedY;
					nMask &= ~SECTION_Y_MASK;		// We don't care about the low part, we just want to check the high-part
					if (nMask != 0)
					{
						// It means that the seed we are interested in is in another section, let's retrieve it
						pSeedSection = GetSection(nStartX, nSeedY, nStartZ);
						if (pSeedSection == &s_ZeroSection)
						{
							// Somehow the seed section does not exist (outside the range of the particle?).
							// Use the other section, hoping that it will reach the section as needed...
							seed = MakeXYZ(nStartX, y, nStartZ);
							pSection->PushSeed(seed);
							return true;
						}
					}
				}

				seed = MakeXYZ(nStartX, nSeedY, nStartZ);
				pSeedSection->PushSeed(seed);
				return true;
			}
		}
		fY += m_fCubeWidth * (float)STEP;
	}
	return false;
}

template <int STEP>
bool CSectionBlobRenderer::SeedZ(int nStartX, int nStartY, int nStartZ, int nMaxIterations, const float fX, const float fY, float fZ)
{
	if ((nStartX < 0) || (nStartX > X_SIZE) || (nStartY < 0) || (nStartY > Y_SIZE))
	{
		// Outside the range anyway...
		return false;
	}

	// We assume that the current position has already been calculated. Skip it.
	fZ += m_fCubeWidth * (float)STEP;
	int nMinZ = nStartZ + STEP;
	int nMaxZ = nStartZ + (STEP * nMaxIterations);
	// nMinX is not necessarily smaller than nMaxX (depends of the step, but we still want to go from min to max).

	if (nMinZ < nMaxZ)
	{
		if (nMinZ > Z_SIZE)
		{
			return false;
		}
		if (nMinZ < 0)
		{
			fZ += m_fCubeWidth * (float)(-nMinZ);
			nMinZ = 0;
		}
		if (nMaxZ > Z_SIZE)
		{
			nMaxZ = Z_SIZE;
		}
	}
	else
	{
		if (nMaxZ < 0)
		{
			return false;
		}
		if (nMinZ > Z_SIZE)
		{
			fZ -= m_fCubeWidth * (float)(nMinZ - Z_SIZE);
			nMinZ = Z_SIZE;
		}
		if (nMinZ < 0)
		{
			nMinZ = 0;
		}
	}

	for(int z = nMinZ ; z != nMaxZ ; z += STEP)
	{
		CSection * pSection = GetSection(nStartX, nStartY, z);
		if (pSection != &s_ZeroSection)
		{
			bool bResult = CalcSign(*pSection, fX, fY, fZ);
			if (bResult == false)
			{
				// Here it means that we switched from one side of the surface to the other side.
				// Depending of the direction we are pushing, the cube that we place may be completely outside of the surface.
				// As the cubes are calculated with +1, we need to take the min of both.
				CSection * pSeedSection = pSection;

				int nSeedZ;
				XYZ seed;
				if (STEP < 0)
				{
					nSeedZ = z;			// This is the simpler case that should be done first
				}
				else
				{
					nSeedZ = z - STEP;
					if ((nSeedZ < 0) || (nSeedZ > Z_SIZE))		// This test should not be necessary, but we are taking our precautions here
					{
						// The good seed is outside the range, let's push the value that is inside the range (hopefully we will still have some good crawling values)
						seed = MakeXYZ(nStartX, nStartY, z);
						pSection->PushSeed(seed);
						return true;
					}

					// Seed is in the range...
					int nMask = z ^ nSeedZ;
					nMask &= ~SECTION_Z_MASK;		// We don't care about the low part, we just want to check the high-part
					if (nMask != 0)
					{
						// It means that the seed we are interested in is in another section, let's retrieve it
						pSeedSection = GetSection(nStartX, nStartY, nSeedZ);
						if (pSeedSection == &s_ZeroSection)
						{
							// Somehow the seed section does not exist (outside the range of the particle?).
							// Use the other section, hoping that it will reach the section as needed...
							seed = MakeXYZ(nStartX, nStartY, z);
							pSection->PushSeed(seed);
							return true;
						}
					}
				}

				seed = MakeXYZ(nStartX, nStartY, nSeedZ);
				pSeedSection->PushSeed(seed);
				return true;
			}
		}
		fZ += m_fCubeWidth * (float)STEP;
	}
	return false;
}

// Enable 
#define ENABLE_SEED_XP1		1
#define ENABLE_SEED_XM1		1
#define ENABLE_SEED_YP1		1
#define ENABLE_SEED_YM1		1
#define ENABLE_SEED_ZP1		1
#define ENABLE_SEED_ZM1		1
#define ENABLE_LARGE_SEED	1

// See comments in SeedX() - Beware when changing this code.
bool CSectionBlobRenderer::SeedSurface(Point3D & center, bool bLargerSeeding)
{
	// find cube corresponding to crawl point
	Point3D localCenter = center - m_OuterBBMins;
	localCenter.mult(m_fOneOverCubeWidth);

	// NOTE: ints for x,y and z because it might have to go negative. cube_index_t can't go negative.

	int nStartX = round(localCenter[0]);
	int nStartY = round(localCenter[1]);
	int nStartZ = round(localCenter[2]);

	// Because of how the seeding was designed, it was possible to actually miss a seed on a section and end up not displaying
	// the particles at all. For example, we were doing the lookup on +X and -X, but if we had a bunch of particles,
	// aligned on X in a way that we could not find a seed on a given tile, then the tile would not display the particle at all.
	// Until some of them reached the next tile and would be seeded / discovered at that time.

	// The code has been changed so now it goes through all directions, so we are pretty much guaranteed to find a seed
	// (unless there are so many particles in the tile that we have a big blob touching the limits of the tile.
	// In that case, we would still seed the other particles anyway.

	int nMaxIterations = m_nFieldCalcSteps + 2; // the radius of the field + 2 (one to account for possible rounding error, and one to allow stepping beyond the field)
	if (bLargerSeeding)
	{
		// This is used if the normal seeding did not trigger anything...
		nMaxIterations += 16;
#ifndef ENABLE_LARGE_SEED
		return false;
#endif
	}

	if (r_paintblob_seeding_with_simd.GetBool())
	{
		fltx4 f4CubeWidth = ReplicateX4( &m_fCubeWidth );
		fltx4 f4X = ReplicateX4(nStartX);		// Do not use the table as the numbers can be negative...
		fltx4 f4Y = ReplicateX4(nStartY);
		fltx4 f4Z = ReplicateX4(nStartZ);
		fltx4 f4Position = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_100Mask), f4X, f4Y);			// X | Y | Y | Y
		f4Position = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_110Mask), f4Position, f4Z);		// X | Y | Z | Z
		f4Position = MaddSIMD(f4CubeWidth, f4Position, m_OuterBBMins.sse_vec);								// pos * cubewidth + outerBBMins
		const bi32x4 mask1110 = (bi32x4)LoadAlignedSIMD( Constants::s_SIMD_111Mask );
		f4Position = MaskedAssign(mask1110, f4Position, Four_Ones );					// X | Y | Z | 1

		// Don't test the middle points up to 6 times for each direction, test it once at first
		if ((nStartX >= 0) && (nStartX < X_SIZE_P1) && (nStartY >= 0) && (nStartY < Y_SIZE_P1) && (nStartZ >= 0) || (nStartZ < Z_SIZE_P1))
		{
			CSection * pSection = GetSection(nStartX, nStartY, nStartZ);
			if (pSection != NULL)
			{
				bool bResult = CalcSign(*pSection, f4Position);
				Assert(CalcSign(*pSection, f4Position, nStartX, nStartY, nStartZ) == bResult);
				if (bResult == false)
				{
					XYZ seed = MakeXYZ(nStartX, nStartY, nStartZ);
					pSection->PushSeed(seed);
					return true;
				}
			}
		}

	// Do the -1 first (as they are slightly faster and the same probability to seed)
	fltx4 f4StepX = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_100Mask), f4CubeWidth, Four_Zeros);
#if ENABLE_SEED_XM1
		if (SeedX<-1>(nStartX, nStartY, nStartZ, nMaxIterations, f4Position, -f4StepX))
		{
			return true;
		}
#endif
	fltx4 f4StepY = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_010Mask), f4CubeWidth, Four_Zeros);
#if ENABLE_SEED_YM1
		if (SeedY<-1>(nStartX, nStartY, nStartZ, nMaxIterations, f4Position, -f4StepY))
		{
			return true;
		}
#endif
	fltx4 f4StepZ = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_001Mask), f4CubeWidth, Four_Zeros);
#if ENABLE_SEED_ZM1
		if (SeedZ<-1>(nStartX, nStartY, nStartZ, nMaxIterations, f4Position, -f4StepZ))
		{
			return true;
		}
#endif

	// Do the +1 after
#if ENABLE_SEED_XP1
		if (SeedX<+1>(nStartX, nStartY, nStartZ, nMaxIterations, f4Position, f4StepX))
		{
			return true;
		}
#endif
#if ENABLE_SEED_YP1
		if (SeedY<+1>(nStartX, nStartY, nStartZ, nMaxIterations, f4Position, f4StepY))
		{
			return true;
		}
#endif
#if ENABLE_SEED_ZP1
		if (SeedZ<+1>(nStartX, nStartY, nStartZ, nMaxIterations, f4Position, f4StepZ))
		{
			return true;
		}
#endif
	}
	else
	{
		float fX = (nStartX * m_fCubeWidth) + m_OuterBBMins[0];
		float fY = (nStartY * m_fCubeWidth) + m_OuterBBMins[1];
		float fZ = (nStartZ * m_fCubeWidth) + m_OuterBBMins[2];

#if ENABLE_SEED_XM1
		if (SeedX<-1>(nStartX, nStartY, nStartZ, nMaxIterations, fX, fY, fZ))
		{
			return true;
		}
#endif
#if ENABLE_SEED_YM1
		if (SeedY<-1>(nStartX, nStartY, nStartZ, nMaxIterations, fX, fY, fZ))
		{
			return true;
		}
#endif
#if ENABLE_SEED_ZM1
		if (SeedZ<-1>(nStartX, nStartY, nStartZ, nMaxIterations, fX, fY, fZ))
		{
			return true;
		}
#endif

		// Do the +1 after
#if ENABLE_SEED_XP1
		if (SeedX<+1>(nStartX, nStartY, nStartZ, nMaxIterations, fX, fY, fZ))
		{
			return true;
		}
#endif
#if ENABLE_SEED_YP1
		if (SeedY<+1>(nStartX, nStartY, nStartZ, nMaxIterations, fX, fY, fZ))
		{
			return true;
		}
#endif
#if ENABLE_SEED_ZP1
		if (SeedZ<+1>(nStartX, nStartY, nStartZ, nMaxIterations, fX, fY, fZ))
		{
			return true;
		}
#endif
	}
	// We did not find a seed at all. That can happen, but that could be the symptom of a bigger issue.
	return false;
}

void CSectionBlobRenderer::SeedSurface()
{
	bool bSeeded = false;
	for(int i = 0; i < m_nNumberOfParticles; i++)
	{
		ImpParticle & pParticle = m_Particles[i];
		bSeeded |= SeedSurface(pParticle.center, false);
	}
	if (bSeeded == false)
	{
		// Normal seeding did not produce any seed. Try the larger seeding then, hopefully that will be enough.
		// This should pretty much never happen... Note that we would retest the seeds in the center (same as low res).
		// This could be optimized further if needed.
		for(int i = 0; i < m_nNumberOfParticles; i++)
		{
			ImpParticle & pParticle = m_Particles[i];
			SeedSurface(pParticle.center, true);
		}
	}
}

class CBlobJob : public CJob
{
public:
	CThreadData * m_pThreadData;

	virtual JobStatus_t DoExecute()
	{
		m_pThreadData->ExecuteAllSections(true);
		return (0);
	}
};

class CSectionSorter
{
public:
	int m_Axis[3];

	FORCEINLINE
	bool gt(CSection * a, CSection * b)
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

	for (int i = 0 ; i < m_nCurrentCacheSection ; ++i)
	{
		CSection * section = m_CacheSections[i];
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
	if (m_nCurrentCacheSection > 3)
	{
		// Sort only if that's big enough, otherwise sort() will corrupt the memory.
		// Do not sort the first section (as it is the zero section that has to see at index 0).
		m_CacheSections.sort(sorter, 1, m_nCurrentCacheSection - 1);
	}

	// After we sorted the sections, we have to re-index m_nSectionIndices
	for (int i = 1 ; i < m_nCurrentCacheSection ; ++i)
	{
		CSection * pSection = m_CacheSections[i];
		int nArrayIndex = GetSectionArrayIndex(pSection->m_nX, pSection->m_nY, pSection->m_nZ);
		Assert(m_nSectionIndices[nArrayIndex] != 0);		// How can we check that this matches the unsorted index?
		m_nSectionIndices[nArrayIndex] = i;
		pSection->m_nSectionIndex = i;						// Update the section index as well. This will be needed especially for SPU.
	}
}

#if (BLOB_FILL_MODE == BLOB_FILL_MODE_ASYNCHRONOUS)
void CSectionBlobRenderer::StartRenderPass()
{
	// We spawn all the threads that will render all the sections that have been seeded.

	IThreadPool * pThreadPools[2];

#if BLOB_CREATE_OWN_THREADPOOL
	pThreadPools[0] = s_pThreadPool;
	pThreadPools[1] = s_pThreadPool;
#else
#ifdef _X360
	pThreadPools[0] = g_pThreadPool;		// g_pAlternateThreadPool
	pThreadPools[1] = g_pThreadPool;
#else
	pThreadPools[0] = g_pThreadPool;
	pThreadPools[1] = g_pThreadPool;
#endif
#endif

	// We have to recreate the jobs every time.
	// And because they are reference counted, they could be released after they go out of scope.
	m_ppJobs = new CBlobJob *[m_nNumberOfJobsToSpawn];

	for (int i = 1 ; i < m_nNumberOfJobsToSpawn ; ++i)
	{
		m_ppJobs[i] = new CBlobJob;
		m_ppJobs[i]->m_pThreadData = &m_pThreadData[i];
		m_ppJobs[i]->SetPriority(JP_HIGH);
		m_ppJobs[i]->SetFlags(JF_QUEUE);

		int nThreadPoolIndex = (i & 1);
		pThreadPools[nThreadPoolIndex]->AddJob(m_ppJobs[i]);
	}
}

void CSectionBlobRenderer::EndRenderPass()
{
	// Let's draw fragments until we are all done... All the calculation should have been finished earlier.
	// But in case this is not done yet, we'll help a little if needed.
	int nAllDone;
	uint32 nWaitUntil = Plat_MSTime() + r_paintblob_timeout_for_recycling_fragments.GetInt();	// After N milliseconds we assume that the queue is full
																								// This will prevent some deadlocks
	do
	{
		if (m_FragmentedVertexBuffer.DrawFragments(m_nSlotIndexForFragmentVB) == false)
		{
			if (Plat_MSTime() > nWaitUntil)
			{
				m_FragmentedVertexBuffer.ClearFragmentsToDisplay();
			}

			// We are going to loop here anyway until everything is drawn...
			const int EXECUTE_N_SECTIONS_WHILE_WAITING = 4;
			// Even with SPU running, the main thread is calculating...
			m_pThreadData[0].ExecuteAllSections(false, EXECUTE_N_SECTIONS_WHILE_WAITING);
		}

		nAllDone = 1;
		ThreadMemoryBarrier();		// To make sure m_bDone is updated
		for (int i = 0 ; i < m_nNumberOfJobsToSpawn ; ++i)
		{
			nAllDone &= m_pThreadData[i].m_bDone;
		}
	}
	while (nAllDone == 0);

	// Release the jobs now that we don't need them
	for (int i = 1 ; i < m_nNumberOfJobsToSpawn ; ++i)
	{
		m_ppJobs[i]->Release();
	}
	delete[] m_ppJobs;
	m_ppJobs = NULL;

	// Now that all the jobs are over, again let's make sure we sent all the remaining vertex buffer fragments
	m_FragmentedVertexBuffer.DrawFragments(m_nSlotIndexForFragmentVB);
	m_FragmentedVertexBuffer.CheckResetState(m_nSlotIndexForFragmentVB);
}

#elif (BLOB_FILL_MODE == BLOB_FILL_MODE_SYNCHRONOUS)

void CSectionBlobRenderer::StartRenderPass()
{
	// Fill all fragments (hopefully the buffer will be big enough. If IsTwoStepsDrawEnabled() == false, we are going to only draw one tile at a time. So we will have enough memory.
	// If IsTwoStepsDrawEnabled() == true, then one thread is going to empty the list while the other thread is filling it.
	for (int i = 0 ; i < m_nNumberOfJobsToSpawn ; ++i)
	{
		const int EXECUTE_N_SECTIONS_WHILE_WAITING = 16;
		while (m_pThreadData[i].ExecuteAllSections(true, EXECUTE_N_SECTIONS_WHILE_WAITING) == false);
	}
}

void CSectionBlobRenderer::EndRenderPass()
{
	// Even if we are in synchronous mode, the jobs could be run from another thread...
	// Let's draw fragments until we are all done... 
	int nAllDone;
	do
	{
		if (m_FragmentedVertexBuffer.DrawFragments(m_nSlotIndexForFragmentVB) == false)
		{
			ThreadSleep();
		}

		nAllDone = 1;
		ThreadMemoryBarrier();		// To make sure m_bDone is updated
		for (int i = 0 ; i < m_nNumberOfJobsToSpawn ; ++i)
		{
			nAllDone &= m_pThreadData[i].m_bDone;
		}
	}
	while (nAllDone == 0);

	// Now that all the jobs are over, again let's make sure we sent all the vertex buffer fragments.
	ThreadMemoryBarrier();		// To make sure m_bDone is updated
	m_FragmentedVertexBuffer.DrawFragments(m_nSlotIndexForFragmentVB);
	m_FragmentedVertexBuffer.CheckResetState(m_nSlotIndexForFragmentVB);
}

#elif (BLOB_FILL_MODE == BLOB_FILL_MODE_SPU) || (BLOB_FILL_MODE == BLOB_FILL_MODE_EMULATE_SPU)

void CSectionBlobRenderer::StartRenderPass()
{
	// Spawn all the threads to render all the slices that have been seeded.

	for (int i = 0 ; i < m_nNumberOfJobsToSpawn ; ++i)
	{
#if (BLOB_FILL_MODE == BLOB_FILL_MODE_EMULATE_SPU)
		// In emulation, setup the internal cache
		m_pThreadData[i].SetupInternalCache();
		m_pThreadData[i].m_pJobCache->SetupInternalCache();
#else
		// On SPU, spawn the jobs, thread 0 is used for the main thread...
		if (i > 0)
		{
			// Job 0 will be used for the main thread
			m_pJobInstances[i].RunJob(m_pThreadData[i]);
		}
#endif
	}
}

void CSectionBlobRenderer::EndRenderPass()
{
	int nAllDone;
	uint32 nWaitUntil = Plat_MSTime() + r_paintblob_timeout_for_recycling_fragments.GetInt();	// After N milliseconds we assume that the queue is full
																								// This will prevent some deadlocks

	do
	{
		if (m_FragmentedVertexBuffer.DrawFragments(m_nSlotIndexForFragmentVB) == false)
		{
			if (Plat_MSTime() > nWaitUntil)
			{
				m_FragmentedVertexBuffer.ClearFragmentsToDisplay();
			}

			// We are going to loop here anyway until everything is drawn...
			const int EXECUTE_N_SECTIONS_WHILE_WAITING = 4;
			// Even with SPU running, the main thread is calculating...
			m_pThreadData[0].ExecuteAllSections(false, EXECUTE_N_SECTIONS_WHILE_WAITING);
		}

		nAllDone = 1;
		ThreadMemoryBarrier();		// To make sure m_bDone is updated
		for (int i = 0 ; i < m_nNumberOfJobsToSpawn ; ++i)
		{
			nAllDone &= m_pThreadData[i].m_bDone;
		}
	}
	while (nAllDone == 0);

	// Release the jobs now that we don't need them
	for (int i = 0 ; i < m_nNumberOfJobsToSpawn ; ++i)
	{
#if (BLOB_FILL_MODE == BLOB_FILL_MODE_EMULATE_SPU)
		// In emulation, clear the internal cache and make sure everything has been released properly
		m_pThreadData[i].TeardownInternalCache();
		m_pThreadData[i].m_pJobCache->TeardownInternalCache();
#else
		if (i > 0)
		{
			m_pJobInstances[i].WaitEndOfJob();
		}
#endif
	}

	// Now that all the jobs are over, again let's make sure we sent all the remaining vertex buffer fragments
	m_FragmentedVertexBuffer.DrawFragments(m_nSlotIndexForFragmentVB);
	m_FragmentedVertexBuffer.CheckResetState(m_nSlotIndexForFragmentVB);
}

#else
#error	Unsupported BLOB_FILL_MODE
#endif

bool CSectionBlobRenderer::ShouldRender() const
{
	if (m_nCurrentCacheSection <= 1)
	{
		// Nothing to display... ZeroSection is not displayed.
		return false;
	}
	return true;
}

void CSectionBlobRenderer::StartRenderSlices()
{
	if (ShouldRender() == false)
	{
		return;
	}

	// At that point, all sections have been created (from the particles influence).
	// We have some checks to make sure no new sections are being created in this function.

	m_nNumberOfJobsToSpawn = MIN(r_paintblob_max_number_of_threads.GetInt(), BLOB_NUM_JOBS);

	// To reduce potential overhead (i.e. spawn plenty of jobs for one section),
	// we get the calculate the number of jobs to spawn with the number of sections
	// (the number does not have to be exact though). 
	const int GOOD_MIN_NUMBER_OF_SECTIONS_PER_JOB = 4;
	int nGoodNumberOfJobs = m_nCurrentCacheSection / GOOD_MIN_NUMBER_OF_SECTIONS_PER_JOB;
	if (nGoodNumberOfJobs < 2)
	{
		nGoodNumberOfJobs = 2;		// At least 2 jobs (the helper job and the main job)
	}
	m_nNumberOfJobsToSpawn = MIN(m_nNumberOfJobsToSpawn, nGoodNumberOfJobs);

	// Do not use the stack for pThreadData, as it creates some DMA issues on SPU. Instead, do a dynamic allocation.
	m_pThreadData = new CThreadData[m_nNumberOfJobsToSpawn];

	m_bPolygonizationEnabled = true;

	// There is no notion of prev slice / cur slice or next slice here
	// We just draw each section.

	// Note:
	// To avoid the crawling back and forth, we could actually seed the particles in each section
	// (May not be more computing intensive as a particle would influence few sections at a time).
	// It could be done in parallel with filling the particles per bucket.
	// That way, all the seeds are inside, thus it may not be necessary to parse outside.

	// The final job may have less jobs than others (should be spawn slightly later too).
	int nNumberOfSectionsPerJob = (m_nCurrentCacheSection + m_nNumberOfJobsToSpawn - 1) / m_nNumberOfJobsToSpawn;
	int nNumberOfSectionsDispatched = 1;		// Skip the zeroSection

	for (int i = 0 ; i < m_nNumberOfJobsToSpawn ; ++i)
	{
		CThreadData * pCurrentThreadData = &m_pThreadData[i];

		pCurrentThreadData->m_nCurrentJobIndex = i;
		int nNumberOfSectionsInThisJob = nNumberOfSectionsPerJob;
		if (nNumberOfSectionsDispatched + nNumberOfSectionsInThisJob > m_nCurrentCacheSection)
		{
			nNumberOfSectionsInThisJob = m_nCurrentCacheSection - nNumberOfSectionsDispatched;
		}

		// Setup the section stacks (the stacks are pointing to the renderer version).
		for (int j = 0 ; j < m_nNumberOfJobsToSpawn ; ++j)
		{
			pCurrentThreadData->m_SectionStacks[j] = m_SectionStacks[j];
		}
		pCurrentThreadData->m_GlobalSectionStack = m_GlobalSectionStack;

		// This represents the sections that have to be executed and crawled the first time.
		for (int j = nNumberOfSectionsDispatched ; j < nNumberOfSectionsDispatched + nNumberOfSectionsInThisJob ; ++j)
		{
			if (m_CacheSections[j]->m_SeedList.GetSize() != 0)
			{
				// We are only pushing the sections with seeds as that's only these sections that will do real work the first iteration.
				// Otherwise, we are going to pop the section, lock it, initialize, etc... then discover that there is nothing to do...
				// And we will have to wait the next iteration to see the section being pushed again (this time with seeds).
				// Also this removes some sections that are pushed but will never be seeded anyway (inside a group of seeded sections).
				m_CacheSections[j]->m_nOnAStack = true;					// The state of the stack will be incorrect if we push it twice on the same stack
																		// Store a state indicating that the section is already on a stack.
				pCurrentThreadData->m_SectionStacks[i].PushToHead(j);
			}
		}

		pCurrentThreadData->m_pFirstSection = m_CacheSections.a;
		pCurrentThreadData->m_pnSectionIndices = m_nSectionIndices;
		pCurrentThreadData->m_pJobCache = &m_JobCaches[i];

		pCurrentThreadData->m_nNumberOfJobs = m_nNumberOfJobsToSpawn;
		pCurrentThreadData->m_pAllThreadData = m_pThreadData;	// Address of the first job data

		m_pThreadData[i].m_bDone = false;
		m_pThreadData[i].m_bForceSpuBreak = r_paintblob_debug_spu.GetBool();

		// The first fragment will be retrieved in the job (so we avoid doing it in the main thread).
		pCurrentThreadData->m_pJobCache->m_nCurrentFragmentIndex = -1;

		nNumberOfSectionsDispatched += nNumberOfSectionsInThisJob;
	}
	Assert(nNumberOfSectionsDispatched == m_nCurrentCacheSection);

	//DevMsg("StartRenderPass() - Slot=%d - Start.\n", m_nSlotIndexForFragmentVB);
	StartRenderPass();
	//DevMsg("StartRenderPass() - Slot=%d - End.\n", m_nSlotIndexForFragmentVB);
}

void CSectionBlobRenderer::EndRenderSlices()
{
	if (ShouldRender() == false)
	{
		return;
	}

	//DevMsg("EndRenderPass() - Slot=%d - Start.\n", m_nSlotIndexForFragmentVB);
	EndRenderPass();
	//DevMsg("EndRenderPass() - Slot=%d - End.\n", m_nSlotIndexForFragmentVB);

	delete[] m_pThreadData;	// Should we instead re-use this every frame?
	m_pThreadData = NULL;

	// Once we are done, let's clear the sections
	for (int i = 1 ; i < m_nCurrentCacheSection ; ++i)
	{
		CSection * section = m_CacheSections[i];
		int nArrayIndex = GetSectionArrayIndex(section->m_nX, section->m_nY, section->m_nZ);
		m_nSectionIndices[nArrayIndex] = 0;
#if BLOB_RESET_INSIDE_JOBS
		section->CheckSectionIsReset();
#endif
	}
#if _DEBUG
	// Check that all the sections have been cleared
	for (int i = 0 ; i < TOTAL_NUMBER_OF_SECTIONS ; ++i)
	{
		Assert(m_nSectionIndices[i] == 0);
	}
#endif
}

bool CSectionBlobRenderer::CalcSign(CSection & section, const float x, const float y, const float z)
{
	float fTotal = 0;

	int nNumberOfParticles = section.m_nNumberOfParticles;

	// Consider moving this code to VMX. Early version of the blob did not make this faster.
	// But we should be able to take advantage of newer optimizations. (we could calculate several seeds or particles at the same time and use the one that is valid).
	// One way or the other, this implementation seems slow, and is now taking more than half of the setup time.
	// There was a faster implementation doing 4 particles at a time.
	for (int i = 0 ; i < nNumberOfParticles ; ++i)
	{
		ImpParticle & p = m_Particles[section.m_nParticleIndices[i]];
		float *c = p.center.p;

		// Mimic the VMX code to make sure we have exactly the same result, as there could be some differences that would result
		// in slightly incorrect crawling in some cases (i.e. missing hte crawling).
		float fDiffX = x - c[0];
		float fDiffY = y - c[1];
		float fDiffZ = z - c[2];
		float fDiffXSq = fDiffX * fDiffX;
		float fDiffYSq = fDiffY * fDiffY;
		float fDiffZSq = fDiffZ * fDiffZ;

		// TODO: The fieldRScaleSq should incorporate scalerSq (can compare rSq with one over sqrt(2) instead of cutoffRSq below)
		float rSq = (fDiffXSq + fDiffYSq + fDiffZSq) * p.GetFieldRScaleSq();

		if (m_fCutoffRadiusSq >= rSq)
		{	
			rSq *= m_fScalerSq;
			float f = 0.25f + rSq*rSq - rSq;
			fTotal += f;

			if(fTotal > m_fThreshold)
			{
				return true;
			}
		}
	}
	return false;
}

bool CSectionBlobRenderer::CalcSign(CSection & section, fltx4 position)
{
	const fltx4 f4cutoffRSq = m_f4cutoffRSq;
	const fltx4 f4scalerSq = m_f4scalerSq;
	const fltx4 f4Fourpt25s = m_f4Fourpt25s;
	const fltx4 f4Threshold = ReplicateX4(m_fThreshold);		// Do not use m_f4Threshold as only W is set
																// For this code to work we need all members to be set
	const bi32x4 mask1110 = (bi32x4)LoadAlignedSIMD( Constants::s_SIMD_111Mask );

	fltx4 normal = Four_Zeros;

	int nNumberOfParticles = section.m_nNumberOfParticles;
	
	int i = nNumberOfParticles;
	while (i >= 4)
	{
		// This code is pretty much a variation of CJobCache::Calculate()
		// Do 4 particles at a time on the same corner.

		ImpParticle & p0 = m_Particles[section.m_nParticleIndices[i - 1]];
		ImpParticle & p1 = m_Particles[section.m_nParticleIndices[i - 2]];
		ImpParticle & p2 = m_Particles[section.m_nParticleIndices[i - 3]];
		ImpParticle & p3 = m_Particles[section.m_nParticleIndices[i - 4]];

		fltx4 center0 = p0.center.sse_vec;								// X | Y | Z | fieldRScaleSq
		fltx4 center1 = p1.center.sse_vec;								// X | Y | Z | fieldRScaleSq
		fltx4 center2 = p2.center.sse_vec;								// X | Y | Z | fieldRScaleSq
		fltx4 center3 = p3.center.sse_vec;								// X | Y | Z | fieldRScaleSq

		fltx4 f4pRSq0 = SplatWSIMD(center0);
		fltx4 f4pRSq1 = SplatWSIMD(center1);
		fltx4 f4pRSq2 = SplatWSIMD(center2);
		fltx4 f4pRSq3 = SplatWSIMD(center3);

		center0 = MaskedAssign(mask1110, center0, Four_Zeros);			// X | Y | Z | 0
		center1 = MaskedAssign(mask1110, center1, Four_Zeros);			// X | Y | Z | 0
		center2 = MaskedAssign(mask1110, center2, Four_Zeros);			// X | Y | Z | 0
		center3 = MaskedAssign(mask1110, center3, Four_Zeros);			// X | Y | Z | 0

		fltx4 diff0 = SubSIMD( position, center0 );
		fltx4 diff1 = SubSIMD( position, center1 );
		fltx4 diff2 = SubSIMD( position, center2 );
		fltx4 diff3 = SubSIMD( position, center3 );

		fltx4 diffSq0 = MulSIMD( diff0, diff0 );
		fltx4 diffSq1 = MulSIMD( diff1, diff1 );
		fltx4 diffSq2 = MulSIMD( diff2, diff2 );
		fltx4 diffSq3 = MulSIMD( diff3, diff3 );

		// Because we care about only one component, we could optimize further with some shuffling
		// and calculate several particles with the same SIMD instructions.
		// Although we would have to combine the final result in one total.
		// Between the initial shuffling and final combination I don't know how much faster this would be.
		// I don't know how often we have to calculate 16 particles at a time too.
		// TODO: Revisit this
		fltx4 f4rSq0 = Dot3SIMD(f4pRSq0, diffSq0);
		fltx4 f4rSq1 = Dot3SIMD(f4pRSq1, diffSq1);
		fltx4 f4rSq2 = Dot3SIMD(f4pRSq2, diffSq2);
		fltx4 f4rSq3 = Dot3SIMD(f4pRSq3, diffSq3);

		bi32x4 influence0 = CmpGeSIMD(f4cutoffRSq, f4rSq0);
		bi32x4 influence1 = CmpGeSIMD(f4cutoffRSq, f4rSq1);
		bi32x4 influence2 = CmpGeSIMD(f4cutoffRSq, f4rSq2);
		bi32x4 influence3 = CmpGeSIMD(f4cutoffRSq, f4rSq3);

		f4rSq0 = MulSIMD( f4rSq0, f4scalerSq );
		f4rSq1 = MulSIMD( f4rSq1, f4scalerSq );
		f4rSq2 = MulSIMD( f4rSq2, f4scalerSq );
		f4rSq3 = MulSIMD( f4rSq3, f4scalerSq );

		fltx4 f4_0 = SubSIMD( MaddSIMD(f4rSq0, f4rSq0, f4Fourpt25s) , f4rSq0 );
		fltx4 f4_1 = SubSIMD( MaddSIMD(f4rSq1, f4rSq1, f4Fourpt25s) , f4rSq1 );
		fltx4 f4_2 = SubSIMD( MaddSIMD(f4rSq2, f4rSq2, f4Fourpt25s) , f4rSq2 );
		fltx4 f4_3 = SubSIMD( MaddSIMD(f4rSq3, f4rSq3, f4Fourpt25s) , f4rSq3 );

		f4_0 = AndSIMD(influence0, f4_0);
		f4_1 = AndSIMD(influence1, f4_1);
		f4_2 = AndSIMD(influence2, f4_2);
		f4_3 = AndSIMD(influence3, f4_3);

		normal = AddSIMD(f4_0, normal);
		normal = AddSIMD(f4_1, normal);
		normal = AddSIMD(f4_2, normal);
		normal = AddSIMD(f4_3, normal);

		bi32x4 f4Test = CmpGtSIMD( normal, f4Threshold );
		if ( IsAnyTrue( f4Test ) )
		{
			return true;
		}

		i -= 4;
	}

	while (i >= 1)
	{
		// This code is pretty much a variation of CJobCache::Calculate()
		ImpParticle & p0 = m_Particles[section.m_nParticleIndices[i - 1]];

		fltx4 center0 = p0.center.sse_vec;								// X | Y | Z | fieldRScaleSq
		fltx4 f4pRSq0 = SplatWSIMD(center0);
		center0 = MaskedAssign(mask1110, center0, Four_Zeros);			// X | Y | Z | 0

		fltx4 diff0 = SubSIMD( position, center0 );
		fltx4 diffSq0 = MulSIMD( diff0, diff0 );
		fltx4 f4rSq0 = Dot3SIMD(f4pRSq0, diffSq0);
		bi32x4 influence0 = CmpGeSIMD(f4cutoffRSq, f4rSq0);
		f4rSq0 = MulSIMD( f4rSq0, f4scalerSq );
		fltx4 f4_0 = SubSIMD( MaddSIMD(f4rSq0, f4rSq0, f4Fourpt25s) , f4rSq0 );
		f4_0 = AndSIMD(influence0, f4_0);
		normal = AddSIMD(f4_0, normal);
		bi32x4 f4Test = CmpGtSIMD( normal, f4Threshold );
		if ( IsAnyTrue( f4Test ) )
		{
			return true;
		}
		--i;
	}
	return false;
}

bool CSectionBlobRenderer::CalcSign(CSection & section, fltx4 position, const int x, const int y, const int z)
{
	float fx = x*m_fCubeWidth + m_OuterBBMins[0];
	float fy = y*m_fCubeWidth + m_OuterBBMins[1];
	float fz = z*m_fCubeWidth + m_OuterBBMins[2];

	const fltx4 f4cutoffRSq = m_f4cutoffRSq;
	const fltx4 f4scalerSq = m_f4scalerSq;
	const fltx4 f4Fourpt25s = m_f4Fourpt25s;
	const fltx4 f4Threshold = ReplicateX4(m_fThreshold);		// Do not use m_f4Threshold as only W is set
																// For this code to work we need all members to be set
	const bi32x4 mask1110 = (bi32x4)LoadAlignedSIMD( Constants::s_SIMD_111Mask );

	fltx4 normal = Four_Zeros;
	float fTotal = 0;

	int nNumberOfParticles = section.m_nNumberOfParticles;

	for (int i = 0 ; i < nNumberOfParticles ; ++i)
	{
		// This code is pretty much a copy of CJobCache::Calculate()
		ImpParticle & p = m_Particles[section.m_nParticleIndices[i]];

		float *c = p.center.p;

		// Mimic the VMX code to make sure we have exactly the same result, as there could be some differences that would result
		// in slightly incorrect crawling in some cases (i.e. missing hte crawling).
		float fDiffX = fx - c[0];
		float fDiffY = fy - c[1];
		float fDiffZ = fz - c[2];
		float fDiffXSq = fDiffX * fDiffX;
		float fDiffYSq = fDiffY * fDiffY;
		float fDiffZSq = fDiffZ * fDiffZ;

		// TODO: The fieldRScaleSq should incorporate scalerSq (can compare rSq with one over sqrt(2) instead of cutoffRSq below)
		float rSq = (fDiffXSq + fDiffYSq + fDiffZSq) * p.GetFieldRScaleSq();


		fltx4 center0 = p.center.sse_vec;								// X | Y | Z | fieldRScaleSq
		fltx4 f4pRSq = SplatWSIMD(center0);
		center0 = MaskedAssign(mask1110, center0, Four_Zeros);			// X | Y | Z | 0

		fltx4 diff = SubSIMD( position, center0 );
		fltx4 diffSq = MulSIMD( diff, diff );
		fltx4 f4rSq = Dot3SIMD(f4pRSq, diffSq);
		bi32x4 influence = CmpGeSIMD(f4cutoffRSq, f4rSq);
		f4rSq = MulSIMD( f4rSq, f4scalerSq );
		fltx4 f4 = SubSIMD( MaddSIMD(f4rSq, f4rSq, f4Fourpt25s) , f4rSq );
		f4 = AndSIMD(influence, f4);		// If after the cutoff, the influence will be zero
		normal = AddSIMD(f4, normal);
		bi32x4 f4Test = CmpGtSIMD( normal, f4Threshold );

		bool bTest1 = IsAnyTrue( f4Test );
		bool bTest2 = false;

		if (m_fCutoffRadiusSq >= rSq)
		{	
			rSq *= m_fScalerSq;
			float f = 0.25f + rSq*rSq - rSq;
			fTotal += f;

			bTest2 = (fTotal > m_fThreshold);
		}

		if (bTest1 != bTest2)
		{
			// They could be different if the comparison with threshold is almost the same,
			// but they are in both side of the surface...
			Assert(CloseEnough(SubFloat(normal, 0), fTotal));
		}

		Assert(bTest1 == bTest2);
		if (bTest2)
		{
			return true;
		}
	}
	return false;
}

void CSectionBlobRenderer::FillSections(const int nParticleIndex, int nMinX, int nMaxX, int nMinY, int nMaxY, int nMinZ, int nMaxZ)
{
	const int SECTIONS_PREFETCH_AHEAD = 2;

	int nIndexSectionMinX = nMinX / SECTION_X_SIZE;
	int nIndexSectionMinY = nMinY / SECTION_Y_SIZE;
	int nIndexSectionMinZ = nMinZ / SECTION_Z_SIZE;
	int nIndexSectionMaxX = (nMaxX + SECTION_X_SIZE - 1) / SECTION_X_SIZE;
	int nIndexSectionMaxY = (nMaxY + SECTION_Y_SIZE - 1) / SECTION_Y_SIZE;
	int nIndexSectionMaxZ = (nMaxZ + SECTION_Z_SIZE - 1) / SECTION_Z_SIZE;

	// Redo the min / max here that has already be done by the caller (but with a more accurate range).
	// Improve this at a later point.
	nIndexSectionMinX = MAX(nIndexSectionMinX, 0);				// Starts at 0
	nIndexSectionMinY = MAX(nIndexSectionMinY, 0);
	nIndexSectionMinZ = MAX(nIndexSectionMinZ, 0);
	nIndexSectionMaxX = MIN(nIndexSectionMaxX, NUMBER_OF_SECTIONS_IN_X - 1);
	nIndexSectionMaxY = MIN(nIndexSectionMaxY, NUMBER_OF_SECTIONS_IN_Y - 1);
	nIndexSectionMaxZ = MIN(nIndexSectionMaxZ, NUMBER_OF_SECTIONS_IN_Z - 1);

	int nSectionIndex = GetSectionArrayIndex(nIndexSectionMinX * SECTION_X_SIZE, nIndexSectionMinY * SECTION_Y_SIZE, nIndexSectionMinZ * SECTION_Z_SIZE);
	int nOffsetIndexX = GetSectionArrayIndex(SECTION_X_SIZE, 0, 0);
	int nOffsetIndexY = GetSectionArrayIndex(0, SECTION_Y_SIZE, 0);

	int16 * pXSection = &m_nSectionIndices[nSectionIndex];

	for (int x = nIndexSectionMinX ; x <= nIndexSectionMaxX ; ++x)
	{
		int16 * pYSection = pXSection;
		for (int y = nIndexSectionMinY ; y <= nIndexSectionMaxY ; ++y)
		{
			int16 * pZSection = pYSection;
			for (int z = nIndexSectionMinZ ; z <= nIndexSectionMaxZ ; ++z)
			{
#if _DEBUG
				int nSlowSectionIndex = GetSectionArrayIndex(x * SECTION_X_SIZE, y * SECTION_Y_SIZE, z * SECTION_Z_SIZE);
				Assert(&m_nSectionIndices[nSlowSectionIndex] == pZSection);
#endif
				if (*pZSection == 0)
				{
					// No section already allocated, let's create one
					// Prefetch ahead (and make sure we have enough allocated too).

					// No section at that place, grab a new one
					while (m_nCurrentCacheSection >= m_CacheSections.size - SECTIONS_PREFETCH_AHEAD)
					{
						// No available sections, acquire more
						m_SectionPool.AcquireSections( m_CacheSections );
					}

					if (m_nCurrentCacheSection % (CACHE_LINE_SIZE / sizeof(CSection *)) == 0)
					{
						// A section fits in 3 cache lines :( More now... Even worse, but we don't allocate buckets.
						// Given that X360 or PS3 have only 8 slots for memory access, we are using almost half of it with this
						PREFETCH_128(m_CacheSections[m_nCurrentCacheSection + SECTIONS_PREFETCH_AHEAD], 0 * 128);
						PREFETCH_128(m_CacheSections[m_nCurrentCacheSection + SECTIONS_PREFETCH_AHEAD], 1 * 128);
						PREFETCH_128(m_CacheSections[m_nCurrentCacheSection + SECTIONS_PREFETCH_AHEAD], 2 * 128);

						if (m_nCurrentCacheSection % (2 * CACHE_LINE_SIZE / sizeof(CSection *)) == 0)
						{
							// Prefetch cacheBuckets itself. 25% of the cache misses when reading cacheBuckets[curCacheBucket + BUCKETS_PREFETCH_AHEAD]
							// Because cacheBuckets array is not cache line aligned, prefetching the next cache line may not be good enough
							// As it could be only few pointers later. Instead, prefetch 2 cache lines ahead.
							const int NEXT_CACHE_LINE = 2 * CACHE_LINE_SIZE / sizeof(CSection *);
							int index = m_nCurrentCacheSection + NEXT_CACHE_LINE;
							if (index < m_CacheSections.size)
							{
								// Make sure that we only prefetch some memory addressable by the array
								// It should not be a problem to prefetch, except if we were using virtual allocations, etc...
								PREFETCH_128(&m_CacheSections[index], 0);
							}
						}
					}

					CSection * pSection = m_CacheSections[m_nCurrentCacheSection];
					pSection->m_nSectionIndex = m_nCurrentCacheSection;
					*pZSection = m_nCurrentCacheSection++;
					// Let's reset the bare minimum here.
					pSection->ResetXYZ(x * SECTION_X_SIZE, y * SECTION_Y_SIZE, z * SECTION_Z_SIZE);

					pSection->m_nNumberOfParticles = 1;
					pSection->m_nParticleIndices[0] = nParticleIndex;
				}
				else
				{
					CSection * pSection = m_CacheSections[*pZSection];
#if _DEBUG
					Assert(pSection->m_nX == (x * SECTION_X_SIZE));
					Assert(pSection->m_nY == (y * SECTION_Y_SIZE));
					Assert(pSection->m_nZ == (z * SECTION_Z_SIZE));
#endif
					int nNumberOfParticles = pSection->m_nNumberOfParticles;
					if (nNumberOfParticles < CSection::MAX_NUMBER_OF_PARTICLES_IN_SECTION)
					{
						pSection->m_nParticleIndices[nNumberOfParticles] = nParticleIndex;
						pSection->m_nNumberOfParticles = nNumberOfParticles + 1;
					}
				}
				++pZSection;
			}
			pYSection += nOffsetIndexY;
		}
		pXSection += nOffsetIndexX;
	}

	// Return unused sections to the global pool:
	if ( blob_csection_pool_enable.GetBool() && ( nParticleIndex == ( m_nNumberOfParticles - 1 ) ) )
		m_SectionPool.ReleaseSections( m_CacheSections, ( m_nCurrentCacheSection + SECTIONS_PREFETCH_AHEAD ) );
}

void CSectionBlobRenderer::BuildCache()
{
	// Unlike the other implementations, this implementation does not need to sort the particles.
	// The particles are stored in the corresponding sections, there is enough room in the sections to not have to skip some particles.
	// Before, particles were sorted by weight to keep the biggest particles in the list (and removing the particles that were not fitting in the list).

	for(int nParticleIndex = 0; nParticleIndex < m_nNumberOfParticles; ++nParticleIndex)
	{
		ImpParticle & p = m_Particles[nParticleIndex];

		// TODO: use oneOverCw instead of dividing by cw
		Point3D c = (p.center - m_OuterBBMins) / m_fCubeWidth;
		float fr = (m_fCutoffRadius * p.scale) / m_fCubeWidth;	// field radius in terms of cube-widths

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
		int nMinX = MAX(round_up(c[0] - fr), 0 + 1);
		int nMaxX = MIN(round_down(c[0] + fr), X_SIZE - 1);

		int nMinY = MAX(round_up(c[1] - fr), 0 + 1);
		int nMaxY = MIN(round_down(c[1] + fr), Y_SIZE - 1);

		int nMinZ = MAX(round_up(c[2] - fr), 0 + 1);
		int nMaxZ = MIN(round_down(c[2] + fr), Z_SIZE - 1);

		// Note that we are going to fill the space between (nMinX, nMinY, nMinZ) and (nMaxX, nMaxY, nMaxZ).
		// The space will be a little bit conservative as we map a sphere to a cube, so we may fill some buckets that don't need this.
		// If we want to improve performance a bit, we could have a more accurate calculation (which in some cases could free some space for some particles).
		// We could potentially do this on the PPU
		// Have to increase the range a bit due to the buckets that are shared between sections. Otherwise we will see some cracks.
		FillSections(nParticleIndex, nMinX - BUCKET_FACTOR, nMaxX + BUCKET_FACTOR, nMinY - BUCKET_FACTOR, nMaxY + BUCKET_FACTOR, nMinZ - BUCKET_FACTOR, nMaxZ + BUCKET_FACTOR);
	}
}

}
}
