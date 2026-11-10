//========= Copyright 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "blobulator/light_bucket_implementation/light_cjob_cache.h"
#include "blobulator/light_bucket_implementation/light_csection.h"

#ifndef SPU
#include "blobulator/light_bucket_implementation/light_csection_blob_renderer.h"
#include "blobulator/light_bucket_implementation/light_cfragmented_vertex_buffer.h"
#include "tier1/strtools.h"
#include "tier1/convar.h"
#else
#include "ps3/spu_job_shared.h"
#if !defined(_CERT)
// Remove this code when blob fix on SPU is validated...
#include <spu_printf.h>
#endif
#endif

#include "blobulator/light_bucket_implementation/light_cfragmented_vertex_buffer.h"
#include "blobulator/implicit/impparticle.h"
#include "blobulator/util/constants.h"

#include "../implicit/impconsttable_winding_ccw2_tris.h"

#include "tier0/cache_hints.h"

// A quick way to check if a float is negative. The parameter has to be the float value read as an integer.
#define ISNEG(f) (f & 0x80000000)

#ifndef SPU
extern ConVar r_paintblob_max_number_of_vertices_displayed;
extern ConVar r_paintblob_max_number_of_indices_displayed;
#endif

namespace Blobulator
{

const float g_ExclusionXMin = (-64.0f * 2.0f);
const float g_ExclusionXMax = (0.0f);					// We are expanding to 0.0f, so it is important that this value is 0.0f
const float g_ExclusionYMin = (-32.0f * 2.5f);
const float g_ExclusionYMax = (32.0f * 2.5f);
const float g_ExclusionZMin = (-56.0f * 2.0f);
const float g_ExclusionZMax = (+56.0f * 2.0f);

namespace LightBucketImplementation
{

#ifndef SPU
CJobCache::CJobCache()
	:
	m_nCurrentCornerIndex(1),
	m_nCurTime(1),
	m_nNumberOfMatrices(0)
{
	m_pEaJobCache = this;

	// Even when running on SPU we allocate the corners. The reason is that the main thread is also going to do some calculation.
	// Not a lot but still better than nothing.
//#if (BLOB_FILL_MODE != BLOB_FILL_MODE_SPU)
	const int nSizeToAllocate = NUM_CORNERS_GROUP_ALLOCATED * sizeof(CCornerInfo) * NUM_CORNERINFOS_PER_CORNER_GROUP;
	m_pAllCorners = (CCornerInfo *)(MemAlloc_AllocAligned(nSizeToAllocate, CACHE_LINE_SIZE));
	memset(m_pAllCorners, 0, nSizeToAllocate);

	// The first set of corners in the corner cache is actually a zero corner (used by index and not by address).
	for (int i = 0 ; i < NUM_CORNERINFOS_PER_CORNER_GROUP ; ++i)
	{
		m_pAllCorners[i].value = -m_fThreshold;
	}
/*
#else
	// This will be stored directly in the SPU, no reason to allocate it
	m_pAllCorners = NULL;
#endif
*/
}

CJobCache::~CJobCache()
{
//#if (BLOB_FILL_MODE != BLOB_FILL_MODE_SPU)
	MemAlloc_FreeAligned(m_pAllCorners);
//#endif
}

void CJobCache::Initialize(const CLockLessStack<BLOB_NUMBER_OF_FRAGMENTS> & fragmentsAvailable, CLockLessStack<BLOB_NUMBER_OF_FRAGMENTS> & fragmentsToDisplay, CFragmentInfo * pAllFragmentInfos)
{
	m_FragmentsAvailable = fragmentsAvailable;
	m_FragmentsToDisplay = fragmentsToDisplay;
	m_pAllFragmentsInfos = pAllFragmentInfos;
}

void CJobCache::SetPortalMatrices(const matrix3x4_t * pMatrices, int nNumberOfMatrices)
{
	if (nNumberOfMatrices > MAX_NUMBER_OF_EXCLUSION_MATRICES)
	{
		nNumberOfMatrices = MAX_NUMBER_OF_EXCLUSION_MATRICES;
	}
	m_nNumberOfMatrices = nNumberOfMatrices;

	// Copy internally the non aligned matrix to the aligned matrix.
	// The matrix is in full in CJobCache so there i.e. no need to DMA it in.
	// For the same reason, we calculate the inverted matrix here.
	// Note that we invert the matrix in each job, where we could invert them once for all jobs.
	for (int i = 0 ; i < nNumberOfMatrices ; ++i)
	{
		matrix3x4a_t portalMatrix;
		portalMatrix = pMatrices[i];
		matrix3x4a_t outInvertedMatrix;
		MatrixInvert(portalMatrix, outInvertedMatrix);

		// Same but this time transposed...

		fltx4 x, y, z, f4Translation;
		x = LoadAlignedSIMD(portalMatrix[0]);
		y = LoadAlignedSIMD(portalMatrix[1]);
		z = LoadAlignedSIMD(portalMatrix[2]);
		f4Translation = Four_Zeros;
		TransposeSIMD(x, y, z, f4Translation);
		m_TransposedPortalMatrices[i].m_ColumnX = x;
		m_TransposedPortalMatrices[i].m_ColumnY = y;
		m_TransposedPortalMatrices[i].m_ColumnZ = z;
		m_TransposedPortalMatrices[i].m_Translation = f4Translation;

		x = LoadAlignedSIMD(outInvertedMatrix[0]);
		y = LoadAlignedSIMD(outInvertedMatrix[1]);
		z = LoadAlignedSIMD(outInvertedMatrix[2]);
		f4Translation = Four_Zeros;
		TransposeSIMD(x, y, z, f4Translation);
		m_TransposedInvertedPortalMatrices[i].m_ColumnX = x;
		m_TransposedInvertedPortalMatrices[i].m_ColumnY = y;
		m_TransposedInvertedPortalMatrices[i].m_ColumnZ = z;
		m_TransposedInvertedPortalMatrices[i].m_Translation = f4Translation;
	}
}

void CJobCache::ResetBetweenFrames(Point3D & outerBBMins, float fCubeWidth, float fThreshold, ImpParticle * pParticles, int nNumberOfParticles, int nMarginNCubes, bool bPolygonizationEnabled, bool bTileMode)
{
	// This is needed by Calculate().
	m_OuterBBMins = outerBBMins;
	m_OuterBBMins = outerBBMins;
	m_fCubeWidth = fCubeWidth;
	m_fThreshold = fThreshold;
	m_pParticles = pParticles;
	m_nNumberOfParticles = nNumberOfParticles;
	m_nMarginNCubes = nMarginNCubes;
	m_bPolygonizationEnabled = bPolygonizationEnabled;
	m_bTileMode = bTileMode;

#if (BLOB_FILL_MODE != BLOB_FILL_MODE_EMULATE_SPU)
	// Clear the bucket pointers for each corner (so we know they are free)
	// We will have to optimize this at some point.
	// Actually, we may not need to do this. The buckets are always the same anyway (and added at the end).
	// Multi-threading makes this a little bit more complex, so let's do the safe way :).

	// On emulated SPU, this is done in CJobCache::SetupInternalCache().
	// For the SPU, this is done within the job itself.
	for (int i = 0 ; i < NUM_CORNERS_GROUP_ALLOCATED ; ++i)
	{
		CCornerInfo * pCorner = m_pAllCorners + (i * NUM_CORNERINFOS_PER_CORNER_GROUP);
		pCorner[0].Corner0.m_nSectionIndex = 0;
		pCorner[0].Corner0.m_nBucketIndex = -1;
		pCorner[1].Corner1.m_nX = -1;
		pCorner[1].Corner1.m_nY = -1;
		pCorner[1].Corner1.m_nZ = -1;
	}
#else
	// On SPU, we are going to clear this between each execution.
#endif

	m_nCurrentCornerIndex = 1;
}

void CJobCache::BeginFrame()
{
	m_nCurTime = 1;
	m_nMaxNumberOfVerticesDisplayed = r_paintblob_max_number_of_vertices_displayed.GetInt();
	m_nMaxNumberOfIndicesDisplayed = r_paintblob_max_number_of_indices_displayed.GetInt();
}

void CJobCache::EndFrame()
{
	// Do nothing...
}

void CJobCache::BeginTile()
{
	// Do nothing...
}

void CJobCache::EndTile()
{
	// Do nothing...
}
#endif

ImpParticle & CJobCache::GetParticle(int nIndex) const
{
	return m_pParticles[nIndex];
}

#if IsPlatformPS3_SPU() || (BLOB_FILL_MODE == BLOB_FILL_MODE_EMULATE_SPU)

void CJobCache::SetupInternalCache()
{
	// Instead of clearing the bare minimum, let's clear everything
	// Later we could selectively clear pieces of the array
	V_memset(m_pAllCorners, 0, NUM_CORNERS_GROUP_ALLOCATED * NUM_CORNERINFOS_PER_CORNER_GROUP * sizeof(CCornerInfo));

	for (int i = 0 ; i < NUM_CORNERS_GROUP_ALLOCATED ; ++i)
	{
		CCornerInfo * pCorner = m_pAllCorners + (i * NUM_CORNERINFOS_PER_CORNER_GROUP);
		pCorner[0].Corner0.m_nSectionIndex = 0;
		pCorner[0].Corner0.m_nBucketIndex = -1;
		pCorner[1].Corner1.m_nX = -1;
		pCorner[1].Corner1.m_nY = -1;
		pCorner[1].Corner1.m_nZ = -1;
	}

	for (int i = 0 ; i < NUM_CORNERINFOS_PER_CORNER_GROUP ; ++i)
	{
		m_pAllCorners[i].value = -m_fThreshold;
	}
}

void CJobCache::TeardownInternalCache()
{
	// Do nothing in this implementation...
}

#else

void CJobCache::SetupInternalCache()
{
	// Do nothing in this implementation...
}

void CJobCache::TeardownInternalCache()
{
	// Do nothing in this implementation...
}

#endif

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
//  7. Calculate the 8 corners at a time so VMX can shine.
void CJobCache::Calculate(CSection & section, CCornerInfo * pCorner, int x, int y, int z)
{
	BLOB_LOG(BLOB_LOG_FUNCTIONS, VjobSpuLog("Calculate Bucket(%d, %d, %d)\n", x, y, z));

	const fltx4 f4scalerSq = m_f4scalerSq;
	const fltx4 f4Fourpt25s = m_f4Fourpt25s;
	const fltx4 f4cutoffRSq = m_f4cutoffRSq;

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
	PREFETCH_128(pCorner, 0 * CACHE_LINE_SIZE);
	// Because the 2nd half of the corner info group will be mostly zeroed (all the indices)
	// Let's zero it as is, as it should be faster than the prefetch and should save some CPU cycles (see at the end)
#if IsPlatformX360() || IsPlatformPS3_PPU()
	// Prefetch only on PowerPC platform. On the other platforms (that don't have prefetch), the best is to clear the items directly
	// it will take advantage of the SIMD
	PREZERO_128(pCorner, 1 * CACHE_LINE_SIZE);
#endif

	f4Position = MaskedAssign((bi32x4)LoadAlignedSIMD( Constants::s_SIMD_100Mask ), f4X, f4Y);			// X | Y | Y | Y
	f4Position = MaskedAssign((bi32x4)LoadAlignedSIMD( Constants::s_SIMD_110Mask ), f4Position, f4Z);	// X | Y | Z | Z

	const fltx4 f4CubeWidth = ReplicateX4(&m_fCubeWidth);
	f4Position = MaddSIMD(f4CubeWidth, f4Position,  m_OuterBBMins.sse_vec);

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

	int nNumParticles = section.m_nNumberOfParticles;

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
			ImpParticle & p = GetParticle(section.m_nParticleIndices[k]);

			// TODO: Optimize this ReplicateX4!
			fltx4 f4pRSq = ReplicateX4( p.fieldRScaleSq );
			fltx4 diff = SubSIMD( currentPosition, p.center.sse_vec );
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
		StoreAlignedSIMD( corner[j].normal, normalA );

		int * bufferToZero = corner[j].edgesAsInt;
		bufferToZero[0] = 0;
		bufferToZero[1] = 0;
		bufferToZero[2] = 0;
	}
#else

	// Maybe we should use FourVectors, but actually W is not used in that library (it is 4 vectors of XYZ only)
	// Dot3SIMD is not supported in that library, nor CmpGeSIMD. Test that another day.

	fltx4 normalA = -m_f4Threshold;
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

	for (int i = 0 ; i < nNumParticles ; ++i)
	{
		ImpParticle & p0 = GetParticle(section.m_nParticleIndices[i]);
		fltx4 center0 = p0.center.sse_vec;								// X | Y | Z | fieldRScaleSq

		// Faster version of this: ReplicateX4( p0.fieldRScaleSq );
		//fltx4 f4pRSq0 = ReplicateX4(p0.fieldRScaleSq);
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

	StoreAlignedSIMD( pCorner[0].normal, normalA );
	StoreAlignedSIMD( pCorner[1].normal, normalB );
	StoreAlignedSIMD( pCorner[2].normal, normalC );
	StoreAlignedSIMD( pCorner[3].normal, normalD );
	StoreAlignedSIMD( pCorner[4].normal, normalE );
	StoreAlignedSIMD( pCorner[5].normal, normalF );
	StoreAlignedSIMD( pCorner[6].normal, normalG );
	StoreAlignedSIMD( pCorner[7].normal, normalH );

	// The 2 first corners have info, so we can't reset them to zero as is
	// (it would be too costly to read it in a VMX, mask out, then store it. Let's write altogether).
	int * pBufferToZero;
	pBufferToZero = pCorner[0].edgesAsInt;
	pBufferToZero[0] = 0;
	pBufferToZero[1] = 0;
	pBufferToZero[2] = 0;
	pBufferToZero = pCorner[1].edgesAsInt;
	pBufferToZero[0] = 0;
	pBufferToZero[1] = 0;
	pBufferToZero[2] = 0;
	// But we can reset the others directly (on the first cache line)
	fltx4 zero = Four_Zeros;
	StoreAlignedSIMD( pCorner[2].edgesAsFloat, zero );
	StoreAlignedSIMD( pCorner[3].edgesAsFloat, zero );

#if !IsPlatformX360() && !IsPlatformPS3_PPU()
	// If we are not on X360, and not on PS3 PPU, we did not prefetch earlier.
	// As such, we zero the cache item by item (faster than clearing everything).
	StoreAlignedSIMD( pCorner[4].edgesAsFloat, zero );
	StoreAlignedSIMD( pCorner[5].edgesAsFloat, zero );
	StoreAlignedSIMD( pCorner[6].edgesAsFloat, zero );
	StoreAlignedSIMD( pCorner[7].edgesAsFloat, zero );
#endif

#endif
}

CCornerInfo * CJobCache::FindNewCornerInfo(CSection & section, int x, int y, int z, int nBucketIndex, int nCacheIndex)
{
	// For the moment we keep the body of this function the same, but this could be changed for optimization purpose...

	CCornerInfo * pCorner;
	uint offset = m_nCurrentCornerIndex++;		// Only increased once per lookup
												// We do not want the collision detection to increase too quickly the counter

	BLOB_LOG(BLOB_LOG_FUNCTIONS, VjobSpuLog("FillNewCornerInfo()\n"));

	for ( ; ; )
	{
		offset %= NUM_CORNERS_GROUP_ALLOCATED;
		if (offset == 0)
		{
			offset = 1;	// Skip zero as it is used for the zero corners... TODO: Do a branch-less version of this test.
		}

		pCorner = m_pAllCorners + (offset * NUM_CORNERINFOS_PER_CORNER_GROUP);

		// Prefetch 8 cache lines ahead, which pretty much represent 4 corners group.
		// And we prefetch a whole corner group (8 corners - 256 bytes).
		PREFETCH_128(pCorner, 8 * CACHE_LINE_SIZE);
		PREFETCH_128(pCorner, 9 * CACHE_LINE_SIZE);

		if (section.m_nSectionIndex != pCorner[0].Corner0.m_nSectionIndex)
		{
			// If this is not already used by the same section, assume it is free for use
			// This is true in most cases.
			break;
		}

		// We should very rarely reach here, as it would mean that we filled the 64 slots already for the same section. Look at number of time we reach here.

		// The Bucket is potentially a neighbor that we need for the calculation as well.
		// We don't want to remove it from the cache as it would invalidate the calculation, then potentially crash.
		// If the corner cache was big enough (125), we could clear the cache at every section and would not need that.

		int diffX = FastAbs(x - pCorner[1].Corner1.m_nX);
		int diffY = FastAbs(y - pCorner[1].Corner1.m_nY);
		int diffZ = FastAbs(z - pCorner[1].Corner1.m_nZ);

		if ((diffX | diffY | diffZ) >= 2 * BUCKET_FACTOR)
		{
			// The bucket is not a neighbor (within -1 / +1 in any axis), we can use it

			// In this implementation, we are not indicating to the section that the corner has been invalidated.
			// This will be done with a check on the section index.

			BLOB_LOG(BLOB_LOG_CACHEUSAGE, VjobSpuLog("CJobCache::FillNewCornerInfo() - Bucket replaced."));
			break;
		}

		// It is a neighbor, we need to find another corner (with another bucket).
		// We don't want to jump to near as we may encounter the same collision later,
		// or this will remove the calculation on buckets that we need.
		// Jump forward in the cache with some prime number to avoid repeat.
		offset += 11;

		// Don't this will mess the prefetch during collision, but it happens rarely enough (0.1%).
	}

	section.m_nCornerIndices[nCacheIndex][nBucketIndex] = offset;

	pCorner[0].Corner0.m_nSectionIndex = section.m_nSectionIndex;
	pCorner[0].Corner0.m_nBucketIndex = nBucketIndex;
	pCorner[1].Corner1.m_nX = x;
	pCorner[1].Corner1.m_nY = y;
	pCorner[1].Corner1.m_nZ = z;

	return pCorner;
}

// All the code to clip the blob behind the portal is excluded for the moment as it generates a compile error.
#ifdef OSX
// GCC 4.2 on OSX doesn't like defining a SIMD static const object, it fails to produce correct assembly due to the constraints, so just
// don't const it on OSX
#define PLAT_CONST 
#else
#define PLAT_CONST const
#endif
	
static PLAT_CONST fltx4 s_ExclusionBoxMin = { Blobulator::g_ExclusionXMin, Blobulator::g_ExclusionYMin, Blobulator::g_ExclusionZMin, 0.0f };
static PLAT_CONST fltx4 s_ExclusionBoxMax = { Blobulator::g_ExclusionXMax, Blobulator::g_ExclusionYMax, Blobulator::g_ExclusionZMax, 0.0f };

// From collisionutils.h - Copied here as we don't want to compile collisionutils.h on SPU.
FORCEINLINE	bool IsPointInBox( const fltx4 & position )
{
	Assert(SubFloat(position, 3) == 0.0f);		// Optimized for W == 0.0f

	bi32x4 greater = CmpGtSIMD( position, s_ExclusionBoxMax );
	bi32x4 less = CmpLtSIMD( position, s_ExclusionBoxMin );
	return (IsAllZeros(OrSIMD(greater,less)));
}

FORCEINLINE		// Inline it so it can help the pipelining
void CJobCache::OptimizedCalcVertex2(fltx4 xyz, fltx4 edgeMask, const CCornerInfo * corner0, const CCornerInfo * corner1, float * positionBuffer)
{
	fltx4 normalValue0 = LoadAlignedSIMD(corner0->normal);
	fltx4 normalValue1 = LoadAlignedSIMD(corner1->normal);

	// We use DivEstSIMD here because:
	//	- The SPU does not have a div implementation (in the PPU emulation library).
	//	- An estimated result has a very small impact here (blending between two values).
	//	- It is faster.
	fltx4 f4Pct = DivEstSIMD( normalValue0, SubSIMD( normalValue0, normalValue1) );
	f4Pct = SplatWSIMD(f4Pct);
	fltx4 f4OMPct = SubSIMD( Four_Ones, f4Pct );

	fltx4 f4Normal0 = MulSIMD(normalValue0, f4OMPct);
	fltx4 f4Normal = MaddSIMD(normalValue1, f4Pct, f4Normal0);

	// Normalize the normal
	// It seems that the SSE math library doesn't have this function as is.
	// Use the estimation as we don't need an absolutely accurate number

	fltx4 f4NormalLength = Dot3SIMD(f4Normal, f4Normal);
	fltx4 f4NormalRec = ReciprocalSqrtEstSIMD(f4NormalLength);
	f4Normal = MulSIMD(f4Normal, f4NormalRec);

	fltx4 f4Position = MaddSIMD(edgeMask, f4Pct, xyz);

	// Here we are testing for the exclusion boxes. I.e. OBB where we exclude some polys (like behind a thin portal).
	// Instead of clipping the whole triangle, we are going to project the point within the box.

	// TODO: Test if the section and the box intersect so we are not doing this test every time
	// but just for the necessary sections (or tile).

	fltx4 f4PositionX = SplatXSIMD(f4Position);
	fltx4 f4PositionY = SplatYSIMD(f4Position);
	fltx4 f4PositionZ = SplatZSIMD(f4Position);

	// In most cases, we are comparing two portal at a time
	// And if there is only one portal, the blob is not going to go through one portal
	// to the other, thus we don't have any clipping issues...
	// So let's calculate 2 boxes at a time (that's almost the same cost as one box).
	// Note that the list of portals is already clipped against the blob at a higher level, so there may be an even number of portals.
	int i = 0;
	while (i + 2 <= m_nNumberOfMatrices)
	{
		// At least two matrices available
		const TransposedMatrix & tInvMatrix0 = m_TransposedInvertedPortalMatrices[i + 0];
		const TransposedMatrix & tInvMatrix1 = m_TransposedInvertedPortalMatrices[i + 1];

		// We could use an array here to avoid the branch 0 | 1 when there is a clipping
		// But the compiler may not use registers as much.

		fltx4 f4NewPosition0 = MaddSIMD( f4PositionX, tInvMatrix0.m_ColumnX, tInvMatrix0.m_Translation );
		fltx4 f4NewPosition1 = MaddSIMD( f4PositionX, tInvMatrix1.m_ColumnX, tInvMatrix1.m_Translation );
		f4NewPosition0 = MaddSIMD( f4PositionY, tInvMatrix0.m_ColumnY, f4NewPosition0 );
		f4NewPosition1 = MaddSIMD( f4PositionY, tInvMatrix1.m_ColumnY, f4NewPosition1 );
		f4NewPosition0 = MaddSIMD( f4PositionZ, tInvMatrix0.m_ColumnZ, f4NewPosition0 );
		f4NewPosition1 = MaddSIMD( f4PositionZ, tInvMatrix1.m_ColumnZ, f4NewPosition1 );

		bool bIsInBox0 = IsPointInBox( f4NewPosition0 );
		bool bIsInBox1 = IsPointInBox( f4NewPosition1 );

		if (bIsInBox0 | bIsInBox1)
		{
			// With current code (due to no culling), in most cases there are no collisions
			// so do a single test first for both collisions.

			// I was thinking that we should only clip if the vertex is in only in one box and not both
			// (when portals are back to back). But this may create create more side effects than necessary.

			fltx4 f4ClippedPositionY;
			fltx4 f4ClippedPositionZ;
			int nIndex;

			if (bIsInBox0)
			{
				// Inside the exclusion box, let's put the poly in the exclusion box
				// Now do the inverse transformation. X has been projected to zero.

				f4ClippedPositionY = SplatYSIMD(f4NewPosition0);
				f4ClippedPositionZ = SplatZSIMD(f4NewPosition0);
				nIndex = 0;
			}
			else
			{
				f4ClippedPositionY = SplatYSIMD(f4NewPosition1);
				f4ClippedPositionZ = SplatZSIMD(f4NewPosition1);
				nIndex = 1;
			}

			const TransposedMatrix & tMatrix = m_TransposedPortalMatrices[i + nIndex];
			// Skip X as the result is zero.
			fltx4 f4ProjectedPosition = MaddSIMD(f4ClippedPositionY, tMatrix.m_ColumnY, tMatrix.m_Translation);
			f4ProjectedPosition = MaddSIMD(f4ClippedPositionZ, tMatrix.m_ColumnZ, f4ProjectedPosition);

			f4Position = f4ProjectedPosition;
		}

		i += 2;
	}

	// Do the last portal if there is one
	while (i < m_nNumberOfMatrices)
	{
		// At least two matrices available
		const TransposedMatrix & tInvMatrix0 = m_TransposedInvertedPortalMatrices[i + 0];

		fltx4 f4NewPosition0 = MaddSIMD( f4PositionX, tInvMatrix0.m_ColumnX, tInvMatrix0.m_Translation );
		f4NewPosition0 = MaddSIMD( f4PositionY, tInvMatrix0.m_ColumnY, f4NewPosition0 );
		f4NewPosition0 = MaddSIMD( f4PositionZ, tInvMatrix0.m_ColumnZ, f4NewPosition0 );

		bool bIsInBox0 = IsPointInBox( f4NewPosition0 );

		// I was thinking that we should only clip if the vertex is in only in one box and not both
		// (when portals are back to back). But this may create create more side effects than necessary.
		if (bIsInBox0)
		{
			// Inside the exclusion box, let's put the poly in the exclusion box
			// Now do the inverse transformation. X has been projected to zero.

			fltx4 f4ClippedPositionY0 = SplatYSIMD(f4NewPosition0);
			fltx4 f4ClippedPositionZ0 = SplatZSIMD(f4NewPosition0);

			const TransposedMatrix & tMatrix0 = m_TransposedPortalMatrices[i + 0];
			// Skip X as the result is zero.
			fltx4 f4ProjectedPosition0 = MaddSIMD(f4ClippedPositionY0, tMatrix0.m_ColumnY, tMatrix0.m_Translation);
			f4ProjectedPosition0 = MaddSIMD(f4ClippedPositionZ0, tMatrix0.m_ColumnZ, f4ProjectedPosition0);

			f4Position = f4ProjectedPosition0;
		}

		++i;
	}
	
#if BLOB_ENABLE_BUFFER_FILLING
	// This code assumes that we store 3 floats for the position
	// On PC at least, position is aligned (normal is not, as it is shifted by 12 bytes).
	StoreUnaligned3SIMD(positionBuffer, f4Position);
	// Then 3 floats for the normal
	StoreUnaligned3SIMD(positionBuffer + 3, f4Normal);
#endif

	// Optimized version in only 2 VMX stores instead of 6
	/*
	 * Works on PC, not on X360 yet. Even that could be different if the vertex format was not fixed on PC.
	fltx4 secondVector = RotateLeft(f4Normal);					//	NY | NZ | NW | NX
	fltx4 firstVector = SetWSIMD(f4Position, secondVector);		//  PX | PY | PZ | NX
	StoreAlignedSIMD(positionBuffer, firstVector);
	StoreAlignedSIMD(positionBuffer + 4, secondVector);
	 */
}

FORCEINLINE
CCornerInfo * CJobCache::RetrieveCorner(CSection & section, int x, int y, int z, int nBucketIndex, int nCacheIndex)
{
	Assert(nBucketIndex < CSection::NUMBER_OF_BUCKETS_IN_SECTION);

	CCornerInfo * pCornerInfo;
	int nCornerIndex = section.m_nCornerIndices[nCacheIndex][nBucketIndex];
	if (nCornerIndex != EMPTY_CORNER)
	{
		// Let's make sure that the corner index that we get is in the expected range
#if IsPlatformPS3_SPU() && !defined(_CERT)
		// Remove this when the fix is validated
		if ( ( nCornerIndex < 0) || ( nCornerIndex >= NUM_CORNERS_GROUP_ALLOCATED ) )
		{
			spu_printf( "[CJobCache::RetrieveCorner] Corner index is out of range: %d\n", nCornerIndex );
		}
#endif
		Assert( ( nCornerIndex >= 0) || ( nCornerIndex < NUM_CORNERS_GROUP_ALLOCATED ) );

		// It is not an empty corner, check if it is valid.
		pCornerInfo = m_pAllCorners + (nCornerIndex * NUM_CORNERINFOS_PER_CORNER_GROUP);
		if (pCornerInfo->Corner0.m_nSectionIndex == section.m_nSectionIndex)
		{
			// And the corner still points to the same section, so nothing to calculate, the cache is valid.
			// We make sure that bucket indices are the same though...
			if (pCornerInfo->Corner0.m_nBucketIndex == nBucketIndex)
			{

				// Check if the bucket index has to be compared.
				// This may not be necessary. Investigate if an assert can be used once the code is working again.
				return pCornerInfo;
			}
		}
	}

	// Not cached already (or not anymore), have to calculate again and put it in the cache.
	pCornerInfo = FindNewCornerInfo(section, x, y, z, nBucketIndex, nCacheIndex);

	// We have 27 corners, but by design only calculate 8 corners (2*2*2 lowest corners).
	// We will use the other corners later from the neighbor Buckets.
	Calculate(section, pCornerInfo, x, y, z);
	return pCornerInfo;
}

bool CJobCache::RenderSection(CSection & section)
{
	BLOB_LOG(BLOB_LOG_FUNCTIONS, VjobSpuLog("RenderSection(%d, %d, %d)\n", section.m_nX, section.m_nY, section.m_nZ));

	// In this implementation, we reset the cache at every section.
	//m_nCurrentCornerIndex = 0;

	// On 8 * 8 * 8, there are 512 cubes. Reserve for 384 cubes, although 256 should be already enough.
	// The stack starts empty at the beginning.
	static const int STACK_SIZE_XYZ = 16 * (SECTION_X_SIZE + SECTION_Y_SIZE + SECTION_Z_SIZE);
	XYZ topOfStackXYZ[STACK_SIZE_XYZ];
	// Use volatile so the compiler doesn't try to keep it in registers (and wrongfully creating tons of LHS on X360).
	volatile XYZ * pStackXYZ = topOfStackXYZ;

	fltx4 f4CubeWidth = ReplicateX4( &m_fCubeWidth );

	fltx4 f4CubeWidth100 = AndSIMD(f4CubeWidth, LoadAlignedSIMD(Constants::s_SIMD_100Mask));
	fltx4 f4CubeWidth010 = AndSIMD(f4CubeWidth, LoadAlignedSIMD(Constants::s_SIMD_010Mask));
	fltx4 f4CubeWidth001 = AndSIMD(f4CubeWidth, LoadAlignedSIMD(Constants::s_SIMD_001Mask));

	CFragmentInfo & currentFragment = GetCurrentFragmentInfo();
	int nVerticesOutput = currentFragment.GetVerticesOutput();
	char * pPositionBuffer = currentFragment.GetPositionBuffer();
	const int nVertexSize = currentFragment.GetVertexSize();
	unsigned short * pCurrentIndexBuffer = currentFragment.GetIndexBuffer();

	for (;;)
	{
		XYZ temp;
		if(section.m_SeedList.GetSize() ==  0)
		{
			break;
		}
		temp = section.m_SeedList.pop();

		int x = XYZ_GetX(temp);
		int y = XYZ_GetY(temp);
		int z = XYZ_GetZ(temp);

		BLOB_LOG(BLOB_LOG_STACKOPS, VjobSpuLog("    Seed list pop = %d, %d, %d\n", x, y, z));

		// Make sure that we are in the range for +1 operations.
		// Note that it is expected that we are using the buckets outside of this range (+1).
		// This is necessary as to calculate the last points, I need the corners on the buckets in the other section.
		section.CheckInsideXYZ(x, y, z);

		{
			// Pull from the seed (which is external from the section).
			// Make cube the cube has not been done yet.
			// If the cube has been pushed from the stack, we made sure it was not parsed already.
			// There may be some cases where the cube has been parsed then done (but not marked done),
			// And it is been seeded again. Investigate if that happens often.

			int nMaskedY = y & SECTION_Y_MASK;
			uint8 flag = 1 << (z & SECTION_Z_MASK);

			uint64 parsedState = section.m_InternalDoneState[x & SECTION_X_MASK].m_nFlags8[nMaskedY];
			if ((parsedState & flag) != 0)
			{
				BLOB_LOG(BLOB_LOG_STACKOPS, VjobSpuLog("    Already done\n"));
				continue;
			}

			// If the cube comes from an internal crawling, it has been marked as done already
			// So only cube coming from external crawling (seed) has to be marked as done.
			section.m_InternalDoneState[x & SECTION_X_MASK].m_nFlags8[nMaskedY] = parsedState | flag;
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

			const int x1 = x + 1;
			const int y1 = y + 1;
			const int z1 = z + 1;

			// TODO: If a corner is outside of the area that will get polygonized,
			// then we only need to calculate a sign there, not a field value.
			// I can replace calc_corner with another function if we're in a non-rendered area.
			// (However, I need to be careful with corners that are at the boundary between
			// the rendered and non-rendered areas. I need a clever, fast way to compute all of that.
			// The check can be: a) check if you're outside of cube (using code already written).
			// b) if outside of cube, do a second check to see if you're outside the cube by one element.
			bool bPolygonizeCube = m_bPolygonizationEnabled;
			if (m_bTileMode)
			{
				// When tiling, there's no point drawing cubes in the margin
				// Note that it seems marginNCubes is a bit too big of a value (7).
				// I am not sure the value is as relevant as before (and may clip a bit too short).

				// Interestingly this test only apply for only few sections.
				// TODO: Optimize this test.
				if(x < m_nMarginNCubes || x >= X_SIZE - m_nMarginNCubes ||
					y < m_nMarginNCubes || y >= Y_SIZE - m_nMarginNCubes ||
					z < m_nMarginNCubes || z >= Z_SIZE - m_nMarginNCubes)
				{
					bPolygonizeCube = false;
				}
			}

			// TODO: I can store these in arrays and use lookups (as I did before)
			// Note: The above can be calculated with three mmx instructions:
			// initialize [fy0,fy1,fz0,fz1] = ((y,y+1,z,z+1)*cubewidth + (outerBBMins[1],outerBBMins[1],outerBBMins[2],outerBBMins[2])
			// or         [fy0,fy1,fz0,fz1] = ((y,y,z,z)*cubewidth + (outerBBMins[1],outerBBMins[1]+cw,outerBBMins[2],outerBBMins[2]+cw)

			CCornerInfo * pCubeCorners[8];

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
			//	There are 8 potential combinations, pretty much mapped with the value of nOffsetXYZ.
			//	XYZ			(0)	- 1 bucket used
			//	XYZp1		(1)	- 2 buckets used
			//	XYp1Z		(2)	- 2 buckets used
			//	XYp1Zp1		(3)	- 4 buckets used
			//	Xp1YZ		(4)	- 2 buckets used
			//	Xp1YZp1		(5) - 4 buckets used
			//  Xp1Yp1Z		(6)	- 4 buckets used
			//	Xp1Yp1Zp1	(7) - 8 buckets used
			//					  27 buckets used when using each combination once.

			// If we were doing a switch on nOffsetXYZ, we could have an average of 1+3.375 tests  (27/8 = 3.375)
			// It would pretty much reduce the number of tests by 2.
			// It would also allow to bake in the buckets and offsets (thus making the generated code more optimal).
			// To reduce the code even further, instead of having to read 8 bytes to get the sign of each corners,
			// we could have one byte storing the value of each corner.
			// With the 8 combinations above, this would make the sign calculation more optimal. Is it worthwhile though?

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

			
			int nCubeFlags;
			{
				// Use {} to try to help the compiler reduce the register pressure, and reduce LHS

				int nMaskedX = x & 0xfffffffe;		// Mask to use the first corner of the bucket
				int nMaskedXp1 = x1 & 0xfffffffe;
				int nMaskedY = y & 0xfffffffe;
				int nMaskedYp1 = y1 & 0xfffffffe;
				int nMaskedZ = z & 0xfffffffe;
				int nMaskedZp1 = z1 & 0xfffffffe;

				const int nCacheIndex = GetCacheIndex();		// Will the compiler optimize the constant if it is always zero?

				int nLocalX = (x - section.m_nX) / BUCKET_FACTOR;
				int nLocalY = (y - section.m_nY) / BUCKET_FACTOR;
				int nLocalZ = (z - section.m_nZ) / BUCKET_FACTOR;
				int nLocalX1 = (x1 - section.m_nX) / BUCKET_FACTOR;
				int nLocalY1 = (y1 - section.m_nY) / BUCKET_FACTOR;
				int nLocalZ1 = (z1 - section.m_nZ) / BUCKET_FACTOR;

				Assert((nLocalX >= 0) && (nLocalX < SECTION_X_SIZE));
				Assert((nLocalY >= 0) && (nLocalY < SECTION_X_SIZE));
				Assert((nLocalZ >= 0) && (nLocalZ < SECTION_X_SIZE));

				BLOB_LOG(BLOB_LOG_STACKOPS, VjobSpuLog("    xyz = %d, %d, %d\n", x, y, z));
				BLOB_LOG(BLOB_LOG_STACKOPS, VjobSpuLog("    sectionXYZ = %d, %d, %d\n", section.m_nX, section.m_nY, section.m_nZ));
				BLOB_LOG(BLOB_LOG_STACKOPS, VjobSpuLog("    localXYZ = %d, %d, %d\n", nLocalX, nLocalY, nLocalZ));

				// Version with no mults. (only add and shifts in case some compilers are not doing it).
				int nIndexX = CSection::GetBucketIndexX(nLocalX);
				int nIndexXp1 = CSection::GetBucketIndexX(nLocalX1);
				int nIndexY = CSection::GetBucketIndexY(nLocalY);
				int nIndexYp1 = CSection::GetBucketIndexY(nLocalY1);
				int nIndexZ = CSection::GetBucketIndexZ(nLocalZ);
				int nIndexZp1 = CSection::GetBucketIndexZ(nLocalZ1);

				BLOB_LOG(BLOB_LOG_STACKOPS, VjobSpuLog("    indexXYZ = %d, %d, %d\n", nIndexX, nIndexY, nIndexZ));

				CCornerInfo * pLocalCorner;
				// Get the index in each corner (depending of the odd / even bit of each X/Y/Z).
				// Note that depending if X/Y/Z are even/odd, there will be a corresponding combination for index0...index7
				// Calculate the first index, then all the others are just a simple bit change from the base
				int nOffsetXYZ = GetCornerIndexFromXYZ(x, y, z);
				pLocalCorner = RetrieveCorner(section, nMaskedX, nMaskedY, nMaskedZ, nIndexX + nIndexY + nIndexZ, nCacheIndex) + nOffsetXYZ;
				nCubeFlags = ISNEG(pLocalCorner->valueAsInt) >> 31;
				pCubeCorners[0] = pLocalCorner;

				int nOffsetXYZp1 = nOffsetXYZ ^ Z_FLAG;
				pLocalCorner = RetrieveCorner(section, nMaskedX, nMaskedY, nMaskedZp1, nIndexX + nIndexY + nIndexZp1, nCacheIndex) + nOffsetXYZp1;
				nCubeFlags |= ISNEG(pLocalCorner->valueAsInt) >> 30;
				pCubeCorners[1] = pLocalCorner;

				int nOffsetXYp1Z = nOffsetXYZ ^ Y_FLAG;
				pLocalCorner = RetrieveCorner(section, nMaskedX, nMaskedYp1, nMaskedZ, nIndexX + nIndexYp1 + nIndexZ, nCacheIndex) + nOffsetXYp1Z;
				nCubeFlags |= ISNEG(pLocalCorner->valueAsInt) >> 29;
				pCubeCorners[2] = pLocalCorner;

				int nOffsetXYp1Zp1 = nOffsetXYZ ^ (Y_FLAG | Z_FLAG);
				pLocalCorner = RetrieveCorner(section, nMaskedX, nMaskedYp1, nMaskedZp1, nIndexX + nIndexYp1 + nIndexZp1, nCacheIndex) + nOffsetXYp1Zp1;
				nCubeFlags |= ISNEG(pLocalCorner->valueAsInt) >> 28;
				pCubeCorners[3] = pLocalCorner;

				int nOffsetXp1YZ = nOffsetXYZ ^ X_FLAG;
				pLocalCorner = RetrieveCorner(section, nMaskedXp1, nMaskedY, nMaskedZ, nIndexXp1 + nIndexY + nIndexZ, nCacheIndex) + nOffsetXp1YZ;
				nCubeFlags |= ISNEG(pLocalCorner->valueAsInt) >> 27;
				pCubeCorners[4] = pLocalCorner;

				int nOffsetXp1YZp1 = nOffsetXYZ ^ (X_FLAG | Z_FLAG);
				pLocalCorner = RetrieveCorner(section, nMaskedXp1, nMaskedY, nMaskedZp1, nIndexXp1 + nIndexY + nIndexZp1, nCacheIndex) + nOffsetXp1YZp1;
				nCubeFlags |= ISNEG(pLocalCorner->valueAsInt) >> 26;
				pCubeCorners[5] = pLocalCorner;

				int nOffsetXp1Yp1Z = nOffsetXYZ ^ (X_FLAG | Y_FLAG);
				pLocalCorner = RetrieveCorner(section, nMaskedXp1, nMaskedYp1, nMaskedZ, nIndexXp1 + nIndexYp1 + nIndexZ, nCacheIndex) + nOffsetXp1Yp1Z;
				nCubeFlags |= ISNEG(pLocalCorner->valueAsInt) >> 25;
				pCubeCorners[6] = pLocalCorner;

				int nOffsetXp1Yp1Zp1 = nOffsetXYZ ^ (X_FLAG | Y_FLAG | Z_FLAG);
				pLocalCorner = RetrieveCorner(section, nMaskedXp1, nMaskedYp1, nMaskedZp1, nIndexXp1 + nIndexYp1 + nIndexZp1, nCacheIndex) + nOffsetXp1Yp1Zp1;
				nCubeFlags |= ISNEG(pLocalCorner->valueAsInt) >> 24;
				pCubeCorners[7] = pLocalCorner;
			}

			// Let's make sure that each cube is intersecting a surface (and not below or above). If that happens the seeding may be incorrect.
			// This assert can happen very often, sometimes because we can't have the correct seed in place (so we use a backup seed).
			//Assert((nCubeFlags != 0) && (nCubeFlags != 255));

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

			char adjacentVoxels = crawlTable[nCubeFlags];

			// In the bucket 3D array, X is the biggest array offset, then Y, then Z.
			// So make sure that we go forward / backward with Z first, then Y then finally X (which we do already).
			// Thus nStack[nStackSize++] should be pushed last with Z + 1 / Z - 1 operations, with Y second and X first.

			if (adjacentVoxels & LEFT_FACE)
			{
				if (x > 0)
				{
					// For the -1 operations, make sure we are in the correct range
					int nMaskY = y & SECTION_Y_MASK;
					uint8 flag = DynamicShift(z & SECTION_Z_MASK);
					if ((x & SECTION_X_MASK) == 0)
					{
						// Have to send it to the previous section (done state may not be accurate with multi-threading, so don't use it).
						section.m_SectionXM.m_nFlags8[nMaskY] |= flag;	// Indicates the cube that we have to push
					}
					else
					{
						section.CheckInsideXYZ(x - 1, y, z);
						uint8 * pParsedState = &section.m_InternalDoneState[(x - 1) & SECTION_X_MASK].m_nFlags8[nMaskY];
						uint8 parsedState = *pParsedState;
						if ((parsedState & flag) == 0)
						{
							*pParsedState = parsedState | flag;
							*pStackXYZ++ = MakeXYZ(x - 1, y, z);

							BLOB_LOG(BLOB_LOG_STACKOPS, VjobSpuLog("    Internal stack push(%d, %d, %d)\n", x - 1, y, z));
						}
					}
				}
			}

			if (adjacentVoxels & RIGHT_FACE)
			{
				// No need to test +1 as x1 has already been validated.
				int nMaskY = y & SECTION_Y_MASK;
				uint8 flag = DynamicShift(z & SECTION_Z_MASK);
				if ((x & SECTION_X_MASK) == SECTION_X_MASK)
				{
					section.m_SectionXP.m_nFlags8[nMaskY] |= flag;
				}
				else
				{
					section.CheckInsideXYZ(x1, y, z);
					uint8 * pParsedState = &section.m_InternalDoneState[x1 & SECTION_X_MASK].m_nFlags8[nMaskY];
					uint8 parsedState = *pParsedState;
					if ((parsedState & flag) == 0)
					{
						*pParsedState = parsedState | flag;
						*pStackXYZ++ = MakeXYZ(x1, y, z);

						BLOB_LOG(BLOB_LOG_STACKOPS, VjobSpuLog("    Internal stack push(%d, %d, %d)\n", x1, y, z));
					}
				}
			}

			if (adjacentVoxels & BOT_FACE)
			{
				if (y > 0)
				{
					// For the -1 operations, make sure we are in the correct range
					if ((y & SECTION_Y_MASK) == 0)
					{
						int nMaskX = x & SECTION_X_MASK;
						uint8 flag = DynamicShift(z & SECTION_Z_MASK);

						section.m_SectionYM.m_nFlags8[nMaskX] |= flag;
					}
					else
					{
						int nMaskY = (y - 1) & SECTION_Y_MASK;
						uint8 flag = DynamicShift(z & SECTION_Z_MASK);

						section.CheckInsideXYZ(x, y - 1, z);
						uint8 * pParsedState = &section.m_InternalDoneState[x & SECTION_X_MASK].m_nFlags8[nMaskY];
						uint8 parsedState = *pParsedState;
						if ((parsedState & flag) == 0)
						{
							*pParsedState = parsedState | flag;
							*pStackXYZ++ = MakeXYZ(x, y - 1, z);

							BLOB_LOG(BLOB_LOG_STACKOPS, VjobSpuLog("    Internal stack push(%d, %d, %d)\n", x, y - 1, z));
						}
					}
				}
			}

			if (adjacentVoxels & TOP_FACE)
			{
				// No need to test +1 as y1 has already been validated.
				if ((y & SECTION_Y_MASK) == SECTION_Y_MASK)
				{
					int nMaskX = x & SECTION_X_MASK;
					uint8 flag = DynamicShift(z & SECTION_Z_MASK);

					section.m_SectionYP.m_nFlags8[nMaskX] |= flag;
				}
				else
				{
					int nMaskY = y1 & SECTION_Y_MASK;
					uint8 flag = DynamicShift(z & SECTION_Z_MASK);

					section.CheckInsideXYZ(x, y1, z);
					uint8 * pParsedState = &section.m_InternalDoneState[x & SECTION_X_MASK].m_nFlags8[nMaskY];
					uint8 parsedState = *pParsedState;
					if ((parsedState & flag) == 0)
					{
						*pParsedState = parsedState | flag;
						*pStackXYZ++ = MakeXYZ(x, y1, z);

						BLOB_LOG(BLOB_LOG_STACKOPS, VjobSpuLog("    Internal stack push(%d, %d, %d)\n", x, y1, z));
					}
				}
			}

			if (adjacentVoxels & FAR_FACE)
			{
				if (z > 0)
				{
					// For the -1 operations, make sure we are in the correct range
					if ((z & SECTION_Z_MASK) == 0)
					{
						// Have to send it to the previous section (done state may not be accurate here, so skip it for the moment).
						int nMaskX = x & SECTION_X_MASK;
						uint8 flag = DynamicShift(y & SECTION_Y_MASK);

						section.m_SectionZM.m_nFlags8[nMaskX] |= flag;
					}
					else
					{
						int nMaskY = y & SECTION_Y_MASK;
						uint8 flag = DynamicShift((z - 1) & SECTION_Z_MASK);

						section.CheckInsideXYZ(x, y, z - 1);
						uint8 * pParsedState = &section.m_InternalDoneState[x & SECTION_X_MASK].m_nFlags8[nMaskY];
						uint8 parsedState = *pParsedState;
						if ((parsedState & flag) == 0)
						{
							*pParsedState = parsedState | flag;
							*pStackXYZ++ = MakeXYZ(x, y, z - 1);

							BLOB_LOG(BLOB_LOG_STACKOPS, VjobSpuLog("    Internal stack push(%d, %d, %d)\n", x, y, z - 1));
						}
					}
				}
			}

			if (adjacentVoxels & NEAR_FACE)
			{
				// No need to test +1 as z1 has already been validated.
				if ((z & SECTION_Z_MASK) == SECTION_Z_MASK)
				{
					// Have to send it to the previous section (done state may not be accurate here, so skip it for the moment).
					int nMaskX = x & SECTION_X_MASK;
					uint8 flag = DynamicShift(y & SECTION_Y_MASK);

					section.m_SectionZP.m_nFlags8[nMaskX] |= flag;
				}
				else
				{
					int nMaskY = y & SECTION_Y_MASK;
					uint8 flag = DynamicShift(z1 & SECTION_Z_MASK);

					section.CheckInsideXYZ(x, y, z1);
					uint8 * pParsedState = &section.m_InternalDoneState[x & SECTION_X_MASK].m_nFlags8[nMaskY];
					uint8 parsedState = *pParsedState;
					if ((parsedState & flag) == 0)
					{
						*pParsedState = parsedState | flag;
						*pStackXYZ++ = MakeXYZ(x, y, z1);

						BLOB_LOG(BLOB_LOG_STACKOPS, VjobSpuLog("    Internal stack push(%d, %d, %d)\n", x, y, z1));
					}
				}
			}

			//////////////////////////////////////////////////////////////////
			///////////// This is the polygonization code ////////////////////
			//////////////////////////////////////////////////////////////////

			if (bPolygonizeCube)
			{
				// This should be optimized for SPU.
				fltx4 f4X = ReplicateX4(&Constants::s_flByteToFloat[x]);
				fltx4 f4Y = ReplicateX4(&Constants::s_flByteToFloat[y]);
				fltx4 f4Z = ReplicateX4(&Constants::s_flByteToFloat[z]);
				fltx4 f4Position0 = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_100Mask), f4X, f4Y);			// X | Y | Y | Y
				f4Position0 = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_110Mask), f4Position0, f4Z);		// X | Y | Z | Z
				f4Position0 = MaddSIMD(f4CubeWidth, f4Position0, m_OuterBBMins.sse_vec);								// pos * cubewidth + outerBBMins
				fltx4 f4Position1 = AddSIMD( f4Position0, f4CubeWidth );

				// Instead of sharing the vertex buffer between several jobs (with locking at every vertex and cube),
				// we use a different vertex buffer for each job.
				// This is more efficient and also much more SPU friendly.

				if (currentFragment.IsOverThreshold(nVerticesOutput, pCurrentIndexBuffer))
				{
					BLOB_LOG(BLOB_LOG_VERTEXBUFFER, VjobSpuLog("    m_Fragment.IsOverThreshold() is true.\n"));

					// The fragment is not big enough to contain this cube, it is time to swap it for a new one
					// While we are going to fill the new fragment, the swapped fragment is going to be drawn.

					// Update the fragment state before sending it to be drawn
					currentFragment.UpdateInternals(nVerticesOutput, pCurrentIndexBuffer);

					bool succeeded = SwapFragments();

					if (succeeded == false)
					{
						BLOB_LOG(BLOB_LOG_VERTEXBUFFER, VjobSpuLog("    SwapFragments() failed.\n"));
						return true;
					}
					// Remember that after we swap the fragment, the cache is invalidated, so reset all the various state

					// As we swapped the fragment for a new one, retrieve the variables
					nVerticesOutput = 0;								// After the swap, it should be zero. Check it.
					Assert(currentFragment.GetVerticesOutput() == nVerticesOutput);

					pPositionBuffer = currentFragment.GetPositionBuffer();
					pCurrentIndexBuffer = currentFragment.GetIndexBuffer();

					// Invalidate the cache
					++m_nCurTime;
				}

				// Up to 5 triangles can be output per cube. It means:
				//	3 * 5 indices (* 2 bytes): 30 bytes
				// Up to 9 different edges can be calculated. It means:
				//	9 * 48 bytes (on PC): 432 bytes.
				// Most times though, only 3 vertices are calculated (the others are cached): 144 bytes.
				// Prefetch a little bit ahead accordingly.

				PREFETCH_128(pCurrentIndexBuffer, 1 * CACHE_LINE_SIZE);	// Prefetch the index table ~4 cubes ahead

				// We could prezero the positionBuffer, but the hard part is to determine when we need to prezero it, and when we won't.
				// (i.e. we don't have a simple index to go throught). So we could prezero several times the same cache line.
				// My assumption is that pre-zeroing several times the same cache line will cost more, than prefetching several times.
				// (as the buffer will be reset constantly, instead of just testing if that's already in memory).
				// This may not be true if the PPC has an internal flag representing the zero state per cache line (which I doubt).
				// It is also a bit trickier to handle end of array when pre-zeroing, and on PC it would make the code slower.
				// Prefetch enough to handle 5 vertices for next cube.
				PREFETCH_128(pPositionBuffer, 2 * CACHE_LINE_SIZE);		// Prefetch the vertex buffer ~1 to 2 cubes ahead.
				//PREFETCH_128(positionBuffer, 3 * CACHE_LINE_SIZE);	// Disabled for the moment as it seems it slows down perf a bit

				// Although we calculate one edge at a time, we actually need 3 edges to define a triangle.
				// The table defines a list of 0 to 5 triangles (0 to 5*3 edges).
				BLOB_LOG(BLOB_LOG_VERBOSE, VjobSpuLog("    index: %d\n", nCubeFlags));
				const char * pBeginTable = (const char*)&(drawTable[nCubeFlags]);
				const char * pTable = pBeginTable;
				int nEdgeIndex = *pTable++;

				while(nEdgeIndex != -1)
				{
					BLOB_LOG(BLOB_LOG_POLYS, VjobSpuLog("    nEdgeIndex: %d\n", nEdgeIndex));

					// generate vertex position and normal data

					// For optimization reasons, bake in the various combinations
					// The cost is two branches, but it significantly reduces the number of loads
					// No need to read edgeOffsets. cubeCorners can be accessed directly.
					// fx, fy and fz are not needed anymore as they are selected directly.
					// Also initializing fx, fy, fz is a bit heavy compared to how it is used.
					// The jump is a small cost compared to all these savings.
					// TODO: Verify that's the case :)

					// Equivalent old code:
					//	const int *edgeOffset = edgeOffsets[edgeno];
					//	const int edgea = edgeOffset[3];
					//	CornerInfo* const corner1 = cubeCorners[edgeOffset[4]];
					//	vbId_t * const id = &(corner1->edges[edgea]);
					//	if(vertexBuffer->isIdValid(id))
					//	{
					//		vertexBuffer->drawVertex(id);
					//	}
					//	else
					//	{
					//		vertexBuffer->beginVertex(id);
					//		const CornerInfo* const corner2 = cubeCorners[edgeOffset[5]];
					//		calc_vertex(fx[edgeOffset[0]], fy[edgeOffset[1]], fz[edgeOffset[2]], edgea, corner1, corner2, vertexBuffer);
					//		vertexBuffer->endVertex(id);
					//	}

					fltx4 currentPosition;
					CCornerInfo * pCorner1;
					// Do not use corner's pointers, but double pointer.
					// That way we load the memory only if necessary.
					// And getting the address of a local variable is just a add, making the switch generated code smaller.
					// We can't do it for corner1 as we need the address anyway to get the address of the id.
					CCornerInfo * * ppCorner2;
					fltx4 edgeMask;
					vbId_t * pId;

					switch (nEdgeIndex)
					{
					default:
						Assert(false);	// Should not be here
					case 0:		//	{0,0,0,2,0,1}
						{
							pCorner1 = pCubeCorners[0];
							ppCorner2 = &pCubeCorners[1];
							edgeMask = f4CubeWidth001;
							pId = &(pCorner1->edges[2]);
							currentPosition = f4Position0;
						}
						break;
					case 1:		//	{0,0,0,1,0,2}
						{
							pCorner1 = pCubeCorners[0];
							ppCorner2 = &pCubeCorners[2];
							edgeMask = f4CubeWidth010;
							pId = &(pCorner1->edges[1]);
							currentPosition = f4Position0;
						}
						break;
					case 2:		//	{0,0,1,1,1,3}
						{
							pCorner1 = pCubeCorners[1];
							ppCorner2 = &pCubeCorners[3];
							edgeMask = f4CubeWidth010;
							pId = &(pCorner1->edges[1]);
							currentPosition = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_001Mask), f4Position1, f4Position0);
						}
						break;
					case 3:		//	{0,1,0,2,2,3}
						{
							pCorner1 = pCubeCorners[2];
							ppCorner2 = &pCubeCorners[3];
							edgeMask = f4CubeWidth001;
							pId = &(pCorner1->edges[2]);
							currentPosition = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_010Mask), f4Position1, f4Position0);
						}
						break;
					case 4:		//	{0,0,0,0,0,4}
						{
							pCorner1 = pCubeCorners[0];
							ppCorner2 = &pCubeCorners[4];
							edgeMask = f4CubeWidth100;
							pId = &(pCorner1->edges[0]);
							currentPosition = f4Position0;
						}
						break;
					case 5:		//	{0,0,1,0,1,5}
						{
							pCorner1 = pCubeCorners[1];
							ppCorner2 = &pCubeCorners[5];
							edgeMask = f4CubeWidth100;
							pId = &(pCorner1->edges[0]);
							currentPosition = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_001Mask), f4Position1, f4Position0);
						}
						break;
					case 6:		//	{0,1,0,0,2,6}
						{
							pCorner1 = pCubeCorners[2];
							ppCorner2 = &pCubeCorners[6];
							edgeMask = f4CubeWidth100;
							pId = &(pCorner1->edges[0]);
							currentPosition = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_010Mask), f4Position1, f4Position0);
						}
						break;
					case 7:		//	{0,1,1,0,3,7}
						{
							pCorner1 = pCubeCorners[3];
							ppCorner2 = &pCubeCorners[7];
							edgeMask = f4CubeWidth100;
							pId = &(pCorner1->edges[0]);
							currentPosition = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_011Mask), f4Position1, f4Position0);
						}
						break;
					case 8:		//	{1,0,0,2,4,5}
						{
							pCorner1 = pCubeCorners[4];
							ppCorner2 = &pCubeCorners[5];
							edgeMask = f4CubeWidth001;
							pId = &(pCorner1->edges[2]);
							currentPosition = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_100Mask), f4Position1, f4Position0);
						}
						break;
					case 9:		//	{1,0,0,1,4,6}
						{
							pCorner1 = pCubeCorners[4];
							ppCorner2 = &pCubeCorners[6];
							edgeMask = f4CubeWidth010;
							pId = &(pCorner1->edges[1]);
							currentPosition = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_100Mask), f4Position1, f4Position0);
						}
						break;
					case 10:	//	{1,0,1,1,5,7}
						{
							pCorner1 = pCubeCorners[5];
							ppCorner2 = &pCubeCorners[7];
							edgeMask = f4CubeWidth010;
							pId = &(pCorner1->edges[1]);
							currentPosition = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_101Mask), f4Position1, f4Position0);
						}
						break;
					case 11:	//	{1,1,0,2,6,7}
						{
							pCorner1 = pCubeCorners[6];
							ppCorner2 = &pCubeCorners[7];
							edgeMask = f4CubeWidth001;
							pId = &(pCorner1->edges[2]);
							currentPosition = MaskedAssign((bi32x4)LoadAlignedSIMD(Constants::s_SIMD_110Mask), f4Position1, f4Position0);
						}
						break;
					}

#if IsPlatformPS3_SPU()
					// Remove this when the fix is validated

					// It is possible that pId does not point to g_AllCorners?
					// That could explain some memory trashing that we are seeing.
					// i.e. pId->time and pId->id in some circumstances can match the patterns we see during the memory trashing.
					int npId = ( int )pId;
					int nBeginAllCorners = ( int )m_pAllCorners;		// Local store
					int nEndAllCorners = nBeginAllCorners + sizeof( CCornerInfo ) * NUM_CORNERS_GROUP_ALLOCATED * NUM_CORNERINFOS_PER_CORNER_GROUP;
					if ( ( npId < nBeginAllCorners ) || ( npId >= nEndAllCorners ) )
					{
#if !defined(_CERT)
						// Keep spu_printf() as we want to always report this issue, except in CERT - VjobSpuLog() is disabled for Blob SPU code.
						spu_printf( "[CJobCache::RenderSection] pId (%08X) is outside the corner cache!\n", npId );
#endif
						// Use a presumably safe values... This may create some visual artifacts...
						pId = &m_pAllCorners[0].edges[0];
						pCorner1 = &m_pAllCorners[0];
						ppCorner2 = &m_pAllCorners;
					}
					else
					{
						// Let's make sure that the value that we get corresponds to an edge
						npId -= nBeginAllCorners;
						npId %= sizeof(CCornerInfo);
						if ( (npId < 16 ) && ( npId >= 28 ) )
						{
#if !defined(_CERT)
							spu_printf( "[CJobCache::RenderSection] pId (%08X) does not point to an edge !\n", npId );
#endif
							// Use a presumably safe values... This may create some visual artifacts...
							pId = &m_pAllCorners[0].edges[0];
							pCorner1 = &m_pAllCorners[0];
							ppCorner2 = &m_pAllCorners;
						}
					}
#endif

					if (pId->time != m_nCurTime)
					{
						// We could optimize this by storing the data in one store.
						pId->time = m_nCurTime;
						pId->id = nVerticesOutput;
						// Fill the index, don't use id->id as it would create a LHS
#if BLOB_ENABLE_BUFFER_FILLING
						*pCurrentIndexBuffer++ = nVerticesOutput++;
#else
						nVerticesOutput++;
#endif

						// Dereference corner2 here (only when necessary)
						OptimizedCalcVertex2(currentPosition, edgeMask, pCorner1, *ppCorner2, reinterpret_cast<float *>(pPositionBuffer));

#if BLOB_ENABLE_BUFFER_FILLING
						// We calculated this vertex, go to the next one
						pPositionBuffer += nVertexSize;
#endif
					}
					else
					{
						// This index has already been calculated, let's re-use it
						// Note that for a given polygon, the same vertex can appear up to 3 times.
						// Thus this could create a LHS.
						// (Should we change the table so we calculate first the vertex once, and then we use them?).
						// When storing the result and re-reading it, we may still have the same number of LHS anyway.
#if BLOB_ENABLE_BUFFER_FILLING
						*pCurrentIndexBuffer++ = pId->id;
#endif
					}

					nEdgeIndex = *pTable++;
				}
				int nNumberOfVerticesDrawn;
				nNumberOfVerticesDrawn = pTable - pBeginTable - 1;		// Don't count the trailing -1

				// Make sure we only output triangles (if the number is incorrect, it probably means that the triangle table is incorrect).
				if ((nNumberOfVerticesDrawn % 3) != 0)
				{
					Assert(0);		// Have to use that construct as Assert() generates a warning if '%' is used in the expression.
				}
			}

			// Pop the stack here to avoid a LHS at the top, when the initial value had to be pushed.
			if (pStackXYZ == topOfStackXYZ)
			{
				// Nothing in the stack to pop
				break;
			}
			const XYZ xyz = *(--pStackXYZ);
			x = XYZ_GetX(xyz);
			y = XYZ_GetY(xyz);
			z = XYZ_GetZ(xyz);
			section.CheckInsideXYZ(x, y, z);

			BLOB_LOG(BLOB_LOG_STACKOPS, VjobSpuLog("    Internal stack pop = %d, %d, %d\n", x, y, z));
		}
	}

#if BLOB_ENABLE_BUFFER_FILLING
	currentFragment.UpdateInternals(nVerticesOutput, pCurrentIndexBuffer);
#endif

	// We did some work, so there is certainly more to do
	return true;
}

void CJobCache::PushCurrentFragmentToDraw()
{
	Assert(m_nCurrentFragmentIndex != -1);

#if IsPlatformPS3_SPU()
	m_LsFragment.CheckIndices();

	// Update how far we wrote on the EA fragment, that's what will be written to the stack of fragments.
	// We don't want LS fragments pushed to the EA memory space.
	m_EaFragment.m_nIndexBuffer_CurrentIndex = m_EaFragment.m_nIndexBuffer_StartIndex + m_LsFragment.m_nIndexBuffer_CurrentIndex;
	m_EaFragment.m_nVertexBuffer_CurrentIndex = m_EaFragment.m_nVertexBuffer_StartIndex + m_LsFragment.m_nVertexBuffer_CurrentIndex;

	// We do so many DMA here that being asynchronous is not really going to help much...
	// Make it synchronous so we can detect MFC issues earlier.
	const int DMA_TAG_FRAGMENT = 7;
	const int DMA_TAG_INDICES = 8;
	const int DMA_TAG_VERTICES = 9;

# if BLOB_DMA_DEBUG	// Don't enable it in debug to have same behavior in debug and release
	// Write it back on the EA version of CJobCache (so the PPU view is in sync - this is only for debugging purpose).
	cellDmaPut(&m_EaFragment, (uint64)(&m_pEaJobCache->m_EaFragment), sizeof(CFragmentInfo), DMA_TAG_FRAGMENT, 0, 0);
	cellDmaWaitTagStatusAll(1 << DMA_TAG_FRAGMENT);
# endif
	// Then we write it back in the array of fragments
	void * pEaFragment = &m_pAllFragmentsInfos[m_nCurrentFragmentIndex];
	cellDmaPut(&m_EaFragment, (uint64)pEaFragment, sizeof(CFragmentInfo), DMA_TAG_FRAGMENT, 0, 0);
	cellDmaWaitTagStatusAll(1 << DMA_TAG_FRAGMENT);

	// Let's write back the vertex and index buffers as well before we push the fragment on the stack.

	// Write the full index and vertex buffer (not only the portion we wrote). This can be optimized later.
	unsigned short * pEaIndexBuffer = m_EaFragment.m_pCachedIndexBuffer + m_EaFragment.m_nIndexBuffer_StartIndex;
	void * pEaVertexBuffer = (char *)(m_EaFragment.m_pCachedVertexBuffer) + (m_EaFragment.m_nVertexBuffer_StartIndex * m_EaFragment.m_nVertexSize);
	// Assume that the buffers can be more than 16 Kb.
	int nSizeIndexBuffer = (m_LsFragment.m_nIndexBuffer_EndIndex - m_LsFragment.m_nIndexBuffer_StartIndex) * sizeof(unsigned short);
	Assert((nSizeIndexBuffer & 0xf) == 0);
	cellDmaLargePut(m_LsFragment.m_pCachedIndexBuffer, (uint64)pEaIndexBuffer, nSizeIndexBuffer, DMA_TAG_INDICES, 0, 0);
	cellDmaWaitTagStatusAll(1 << DMA_TAG_INDICES);
	int nSizeVertexBuffer = (m_LsFragment.m_nVertexBuffer_EndIndex - m_LsFragment.m_nVertexBuffer_StartIndex) * m_LsFragment.m_nVertexSize;
	Assert((nSizeVertexBuffer & 0xf) == 0);
	cellDmaLargePut(m_LsFragment.m_pCachedVertexBuffer, (uint64)pEaVertexBuffer, nSizeVertexBuffer, DMA_TAG_VERTICES, 0, 0);
	cellDmaWaitTagStatusAll(1 << DMA_TAG_VERTICES);
#else
	m_EaFragment.CheckIndices();
	m_pAllFragmentsInfos[m_nCurrentFragmentIndex] = m_EaFragment;
#endif

//	DevMsg("CJobCache::PushCurrentFragmentToDraw(%d)\n", m_nCurrentFragmentIndex);

	m_FragmentsToDisplay.PushToHead(m_nCurrentFragmentIndex);
	m_nCurrentFragmentIndex = -1;
}

bool CJobCache::SwapFragments()
{
	Assert(m_nCurrentFragmentIndex != -1);

	CFragmentInfo & fragmentInfo = GetCurrentFragmentInfo();
	if (fragmentInfo.m_nIndexBuffer_CurrentIndex == fragmentInfo.m_nIndexBuffer_StartIndex)
	{
		// No index to display, nothing to push, it is directly considered free.
		Assert(fragmentInfo.m_nVertexBuffer_CurrentIndex == fragmentInfo.m_nVertexBuffer_StartIndex);
		// And just in case we set the vertices to the start too
		fragmentInfo.m_nVertexBuffer_CurrentIndex = fragmentInfo.m_nVertexBuffer_StartIndex;

		fragmentInfo.CheckIndices();
		return true;
	}

	int nNextFragmentToDisplay;
#if !IsPlatformPS3_SPU()
#if _DEBUG
	const int NUMBER_OF_MILLISECONDS_TIMEOUT = 500;
#else
	const int NUMBER_OF_MILLISECONDS_TIMEOUT = 32;		// After 32 milliseconds we assume that the queue is full
														// This should prevent some deadlocks
#endif
	uint32 nWaitUntil = Plat_MSTime() + NUMBER_OF_MILLISECONDS_TIMEOUT;
#else
	int nNumberOfTries = 150000;						// Timeout of around 32 ms (taking in account number of cycles and L2 cache access).
#endif
	for (;;)
	{
		nNextFragmentToDisplay = m_FragmentsAvailable.PopFromHead();
		if (nNextFragmentToDisplay >= 0)
		{
			break;
		}
		// Somehow the available list is empty, we have to busy wait here :(
		// Consider increasing the number of available fragments
		// Or draw faster... This could happen too if we spawn too many jobs at the same time.

#if !IsPlatformPS3_SPU()
		if ( m_pFragmentedVertexBuffer != NULL )
		{
			// This job can actually draw fragments (should be the main job)
			// It will help create available fragments
			if (m_pFragmentedVertexBuffer->DrawFragments(m_nSlotIndexForFragmentVB, &nNextFragmentToDisplay))
			{
				Assert(nNextFragmentToDisplay >= 0);		// If it succeeded, we should have a valid one
				if (nNextFragmentToDisplay >= 0)
				{
					break;
				}
			}
		}
#endif

		// If used incorrectly, there could be an infinite loop here...
		// No need to sleep(0), this should pretty much never happen...
		ThreadSleep(1);

#if IsPlatformPS3_SPU()
		--nNumberOfTries;
		if (nNumberOfTries < 0)
#else
		if (Plat_MSTime() > nWaitUntil)
#endif
		{
			// We assume that we are in a deadlock, let's reset the current fragment and re-use it
			// we'll see a hole in the blob for a frame, but that's better than a deadlock.
			fragmentInfo.m_nIndexBuffer_CurrentIndex = fragmentInfo.m_nIndexBuffer_StartIndex;
			fragmentInfo.m_nVertexBuffer_CurrentIndex = fragmentInfo.m_nVertexBuffer_StartIndex;
			fragmentInfo.CheckIndices();

#if IsPlatformPS3_SPU() == 0
			const int NUMBER_OF_MILLISECONDS_PER_SPEW = 2 * 1000;
			static uint32 sFirstTime = Plat_MSTime();
			uint32 nNewTime = Plat_MSTime();
			if ( nNewTime > sFirstTime + NUMBER_OF_MILLISECONDS_PER_SPEW )
			{
				sFirstTime = nNewTime;
#else
			{
#endif
				Warning("[Blobulator] Preventing deadlock in SwapFragments(). Expect visual corruptions.  Please set your Effect Detail to a lower setting.\n");
			}
			return true;
		}
	}

	// Invalidate it so we can detect if we do something incorrect with it once it has been pushed.
	fragmentInfo.m_bUsable = false;

	// Now that we have a new fragment, we can push the new fragment
	// Otherwise we would have pushed the fragment and end-up re-using it in case of timeout...
	PushCurrentFragmentToDraw();

#if IsPlatformPS3_SPU()
	const int TAG = 10;
	void * pEaFragment = &m_pAllFragmentsInfos[nNextFragmentToDisplay];
	CheckDmaGet( &m_EaFragment, sizeof(CFragmentInfo) );
	cellDmaGet(&m_EaFragment, (uint64)pEaFragment, sizeof(CFragmentInfo), TAG, 0, 0);
	cellDmaWaitTagStatusAll(1 << TAG);
#else
	m_EaFragment = m_pAllFragmentsInfos[nNextFragmentToDisplay];
#endif

	m_nCurrentFragmentIndex = nNextFragmentToDisplay;
	Assert(m_nCurrentFragmentIndex != -1);

	m_EaFragment.Reset();

//	DevMsg("CJobCache::SwapFragments() - Acquire %d.\n", m_nCurrentFragmentIndex);

#if IsPlatformPS3_SPU()
	// On SPU, we need to initialize accordingly the LS fragment info
	// Everything is pretty much setup already (as we are re-using the same buffer), just have to reset it.
	m_LsFragment.Reset();
	m_LsFragment.CheckIndices();
#endif

	return true;
}

bool CJobCache::GetAvailableFragment()
{
	int nNextFragmentToDisplay;
	for (;;)
	{
		nNextFragmentToDisplay = m_FragmentsAvailable.PopFromHead();
		if (nNextFragmentToDisplay >= 0)
		{
			break;
		}
		// Somehow the available list is empty, let's try to recover :(
		// Consider increasing the number of available fragments
		// Or draw faster... This could happen too if we spawn too many jobs at the same time.
		return false;
	}

#if IsPlatformPS3_SPU()
	const int TAG = 11;
	void * pEaFragment = &m_pAllFragmentsInfos[nNextFragmentToDisplay];
	CheckDmaGet( &m_EaFragment, sizeof(CFragmentInfo) );
	cellDmaGet(&m_EaFragment, (uint64)pEaFragment, sizeof(CFragmentInfo), TAG, 0, 0);
	cellDmaWaitTagStatusAll(1 << TAG);
#else
	m_EaFragment = m_pAllFragmentsInfos[nNextFragmentToDisplay];
#endif

	m_nCurrentFragmentIndex = nNextFragmentToDisplay;
	Assert(m_nCurrentFragmentIndex != -1);

	m_EaFragment.Reset();

#if IsPlatformPS3_SPU()
	// Let's get the output addresses and replace EA by LS

	// Most of this code will have to go in SwapFragments() when the SPU will push every vertex buffers...
	CFragmentInfo & eaFragmentInfo = m_EaFragment;
	CFragmentInfo & lsFragmentInfo = m_LsFragment;
	lsFragmentInfo = eaFragmentInfo;	// For a start, we want to make sure that they are both in sync (EA being the original version)

	char * lsOutputIndexBuffer = (char *)m_pLsOutputBuffers;
	int nSizeIndexBuffer = (lsFragmentInfo.m_nIndexBuffer_EndIndex - lsFragmentInfo.m_nIndexBuffer_StartIndex) * sizeof(unsigned short);
	Assert((nSizeIndexBuffer & 0xf) == 0);
	char * lsOutputVertexBuffer = lsOutputIndexBuffer + nSizeIndexBuffer;

	// Replace EA by LS.
	lsFragmentInfo.m_pCachedIndexBuffer = (unsigned short *)lsOutputIndexBuffer;
	lsFragmentInfo.m_pCachedVertexBuffer = lsOutputVertexBuffer;

	// Update the threshold (clamping to some max values)
	if (lsFragmentInfo.m_nThresholdVerticesOutput > m_nMaxNumberOfVerticesDisplayed)
	{
		lsFragmentInfo.m_nThresholdVerticesOutput = m_nMaxNumberOfVerticesDisplayed;
	}
	// Remember that m_pCachedIndexBuffer is the address of the beginning of the first buffer.
	// We have to add m_nIndexBuffer_StartIndex to have the correct value for this buffer.
	int nThresholdIndexBuffer = eaFragmentInfo.m_pThresholdIndexBuffer - (eaFragmentInfo.m_pCachedIndexBuffer + eaFragmentInfo.m_nIndexBuffer_StartIndex);
	if (nThresholdIndexBuffer > m_nMaxNumberOfIndicesDisplayed)
	{
		nThresholdIndexBuffer = m_nMaxNumberOfIndicesDisplayed;
	}
	lsFragmentInfo.m_pThresholdIndexBuffer = lsFragmentInfo.m_pCachedIndexBuffer + nThresholdIndexBuffer;

	// At that point, start / current / end indices are not correct as they are pointing to the big buffer
	// and on the SPU side we are only filling as we had only one buffer.
	// So change the indices so start and current are actually zero.
	lsFragmentInfo.m_nIndexBuffer_EndIndex -= lsFragmentInfo.m_nIndexBuffer_StartIndex;
	lsFragmentInfo.m_nIndexBuffer_StartIndex = lsFragmentInfo.m_nIndexBuffer_CurrentIndex = 0;
	lsFragmentInfo.m_nVertexBuffer_EndIndex -= lsFragmentInfo.m_nVertexBuffer_StartIndex;
	lsFragmentInfo.m_nVertexBuffer_StartIndex = lsFragmentInfo.m_nVertexBuffer_CurrentIndex = 0;
	m_LsFragment.CheckIndices();
#else
	m_EaFragment.CheckIndices();
#endif

	return true;
}

}
}


