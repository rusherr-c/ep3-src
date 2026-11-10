//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_JOBIMPLEMENTATION_CJOB_CACHE_H
#define BLOBULATOR_JOBIMPLEMENTATION_CJOB_CACHE_H

#include "blobulator/job_implementation/job_cbucket.h"
#include "blobulator/job_implementation/job_cfragment_info.h"
#include "blobulator/job_implementation/job_details.h"
#include "blobulator/job_implementation/job_ccorner_info.h"
#include "blobulator/job_implementation/csoftware_cache.h"

#include "blobulator/point3d.h"
#include "blobulator/smartarray.h"

#include "tier0/cache_hints.h"

#ifdef SPU
// For m_ThreadHandle may not be needed anyway 
#include "spu/threadtools.h"
#else
#include "tier0/threadtools.h"
#endif

class ImpParticle;
class ImpTile;

namespace Blobulator
{
	namespace JobImplementation
	{
		class CBucket;
		class CFragmentedVertexBuffer;

		class ALIGN16 CJobCache
		{
		public:
			// Ugly! Everything is public now as the code is changed a lot in few hours
			// TODO: Clean this once the code is in a more stable state.

			fltx4 g_f4Fourpt25s;
			fltx4 g_f4scalerSq;
			fltx4 g_f4cutoffRSq;
			fltx4 g_f4Threshold;

			Point3D m_OuterBBMins;
			ImpParticle * m_pParticles;								// This points to the main memory. An array of NUMBER_OF_MAX_PARTICLES particles.
																	// The SPU will DMA the content in the local storage for once.
																	// On SPU, the pointer actually access a local storage array.
			int m_nNumberOfParticles;

			// This variable is here to help the SPU and avoid one level of indirection (one less DMA access).
			// We use this directly instead of using the SmartArray<>.
			CBucket * * m_pFirstBucket;								// This points to the main memory
																	// The SPU will DMA the content in the local storage.
																	// To reduce unneeded DMA accesses, the buckets will be cached
																	// in the local storage.
			int m_nTotalNumberOfBuckets;

			float m_fCubeWidth;
			float m_fThreshold;
			int m_nCurrentCornerIndex;
			CCornerInfo * m_pAllCorners;							// On SPU this will point to local storage
																	// No allocation needed on PPU
			ThreadHandle_t m_ThreadHandle;
			bool m_bPoligonizationEnabled;
			bool m_bTileMode;
			int m_nMarginNCubes;
			int m_nJobIndex;

			FORCEINLINE
			int GetCacheIndex() const
			{
				if (HasSingleCornerCache())
				{
					return 0;
				}
				else
				{
					return m_nJobIndex;
				}
			}

			int m_nCurTime;
			CJobCache * m_pEaJobCache;								// Use this pointer to keep track of the EA job cache
																	// This is needed for SPU

			CFragmentInfo m_EaFragment;
			CFragmentInfo m_LsFragment;

			CFragmentInfo & GetCurrentFragmentInfo()
			{
#if IsPlatformPS3_SPU()
				return m_LsFragment;
#else
				return m_EaFragment;
#endif
			}

			// These info are pointing to CFragmentedVertexBuffer internals.
			CLockLessStack<NUMBER_OF_FRAGMENTS> m_FragmentsAvailable;
			CLockLessStack<NUMBER_OF_FRAGMENTS> m_FragmentsToDisplay;
			CFragmentInfo * m_pAllFragmentsInfos;
			int m_nCurrentFragmentIndex;

			int m_nMaxNumberOfVerticesDisplayed;
			int m_nMaxNumberOfIndicesDisplayed;

#ifndef SPU
			CJobCache();
			~CJobCache();

			void Initialize(const CLockLessStack<NUMBER_OF_FRAGMENTS> & fragmentsAvailable, CLockLessStack<NUMBER_OF_FRAGMENTS> & fragmentsDisplay, CFragmentInfo * pFragmentInfos);

			// We do not need these for SPU
			void ResetBetweenFrames(Point3D & outerBBMins, float fCubeWidth, float fThreshold, ImpParticle* pParticles, int nNumberOfParticles, CBucket * * pFirstBucket, int nNumberOfBuckets, int nMarginNCubes, bool bPolygonizationEnabled, bool bTileMode);
			void BeginFrame(void * pRenderContext);
			void EndFrame();
			void BeginTile();
			void EndTile();
#endif

			bool RenderSection(CSection & section);

			// For a global cache.
			// 512 should be enough in many cases. Keep the number a power of 2.
			// For a big blob, we usually over around 7000 buckets or more.
			// Out of these, 2000 to 3000 buckets will be scanned (close to the surface).
			// 512 corresponds to 1/6th of a big blob, thus it is enough for many slices.
			// 512 corners mean that we are going to allocate 128 Kb of memory for the corners.
			// On the SPU, we may use one eight of this size.
			// Each corner group here is actually 8 CornerInfos put together.

			// If the cache is defined by job, then the numbers should be different
			// From one section to the other, the cache coherency may not be very good, so we may recalculate more than needed.
			// In many cases, section are 4 together. This is due to the fact that a particle may influence 1 to 4 sections.
			// When inserting a particle the first time with no section around, there is big chance that all 4 sections around
			// are going to be created at the same time.
			//
			// A section has 8 * 8 * 8 cubes. Or said differently 4 * 4 * 4 = 64 buckets. 64 groups of 8 corners.
			// A section will also access other buckets (so these could also be allocated in the current job).
			// So on a given situation, we may need 37 more corners (64 - 27). For a total of 101.
			// Not all buckets will be crawled by the surface (only half?).
			// So if we take in account the 4 sections around, having 256 corners per job may be a good value.
			// Note that the more job, the more memory used and cache trashed (L2 and corner cache).
			// The current memory cost of the corner cache is around 16 Kb per job.
			static const int NUM_CORNERS_GROUP_ALLOCATED = 64;
			static const int NUM_CORNERINFOS_PER_CORNER_GROUP = 8;

			// On the SPU side, we don't need to write anything back to the PPU side.
			// CCornerInfo pointers are written but they are in the local storage and not the main memory.
			CBucket & AcquireBucket(int nIndex) const;
			// Should we consider dropping this one in favor of the faster "index-less" version?
			void ReleaseBucket(int nIndex, CBucket & bucket) const;

			// Index-less version. This version is faster but there is less checks.
			void AcquireBucket(CBucket & bucket) const;
			void ReleaseBucket(CBucket & bucket) const;

			void SetupInternalCache();
			void TeardownInternalCache();

			void PushCurrentFragmentToDraw();
			bool SwapFragments();

		private:
			FORCEINLINE ImpParticle & GetParticle(int nIndex) const;
			void Calculate(CBucket & bucket, CCornerInfo * corner);
			void Calculate(CBucket & bucket);
			FORCEINLINE void OptimizedCalcVertex2(fltx4 xyz, fltx4 edgeMask, const CCornerInfo * corner0, const CCornerInfo * corner1, float * positionBuffer);

			static uintp CalculateSection(void * pParam);

			FORCEINLINE
			void LazyCalculate(CBucket & bucket, int nCacheIndex)
			{
				if (bucket.IsCalculated(nCacheIndex) == false)
				{
					Calculate(bucket);
				}
			}

			CCornerInfo * FillNewCornerInfo(CBucket & bucket);
		} ALIGN16_POST;
	}
}

#endif

