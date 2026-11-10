//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CJOB_CACHE_H
#define BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CJOB_CACHE_H

#include "blobulator/light_bucket_implementation/light_cfragment_info.h"
#include "blobulator/light_bucket_implementation/light_details.h"
#include "blobulator/light_bucket_implementation/light_ccorner_info.h"
#include "blobulator/light_bucket_implementation/light_csoftware_cache.h"
#include "blobulator/light_bucket_implementation/light_csection.h"

#include "blobulator/point3d.h"
#include "blobulator/smartarray.h"

#include "tier0/cache_hints.h"

#ifdef SPU
// For m_ThreadHandle may not be needed anyway 
#include "spu/light_threadtools.h"
#else
#include "tier0/threadtools.h"
#endif

class ImpParticle;
class ImpTile;

namespace Blobulator
{
	namespace LightBucketImplementation
	{
		class CFragmentedVertexBuffer;

		// This whole class will be stored on the SPU, so careful what you put there.
		class ALIGN16 CJobCache
		{
		public:
			// Ugly! Everything is public now as the code is changed a lot in few hours
			// TODO: Clean this once the code is in a more stable state.

			fltx4 m_f4Fourpt25s;
			fltx4 m_f4scalerSq;
			fltx4 m_f4cutoffRSq;
			fltx4 m_f4Threshold;

			Point3D m_OuterBBMins;
			ImpParticle * m_pParticles;								// This points to the main memory. An array of NUMBER_OF_MAX_PARTICLES particles.
																	// The SPU will DMA the content in the local storage for once.
																	// On SPU, the pointer actually access a local storage array.
			int m_nNumberOfParticles;

			float m_fCubeWidth;
			float m_fThreshold;
			uint m_nCurrentCornerIndex;
			CCornerInfo * m_pAllCorners;							// On SPU this will point to local storage
																	// No allocation needed on PPU
			ThreadHandle_t m_ThreadHandle;
			bool m_bPolygonizationEnabled;
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

			void * m_pLsOutputBuffers;								// Needed for SPU (so we know where to put index and vertex buffer)
			CFragmentInfo m_EaFragment;
			CFragmentInfo m_LsFragment;
			CFragmentedVertexBuffer * m_pFragmentedVertexBuffer;	// Will not be used by SPU, just by the main job
																	// Will be set only for the first job (all the other will have this NULL and -1 for m_nSlotIndexForFragmentVB)
			int m_nSlotIndexForFragmentVB;

			// Use transposed matrices to optimize SIMD calculations
			struct TransposedMatrix
			{
				fltx4	m_Translation;
				fltx4	m_ColumnX;
				fltx4	m_ColumnY;
				fltx4	m_ColumnZ;
			};

			TransposedMatrix m_TransposedPortalMatrices[MAX_NUMBER_OF_EXCLUSION_MATRICES];
			TransposedMatrix m_TransposedInvertedPortalMatrices[MAX_NUMBER_OF_EXCLUSION_MATRICES];

			int m_nNumberOfMatrices;

			CFragmentInfo & GetCurrentFragmentInfo()
			{
#if IsPlatformPS3_SPU()
				return m_LsFragment;
#else
				return m_EaFragment;
#endif
			}

			// These info are pointing to CFragmentedVertexBuffer internals.
			CLockLessStack<BLOB_NUMBER_OF_FRAGMENTS> m_FragmentsAvailable;
			CLockLessStack<BLOB_NUMBER_OF_FRAGMENTS> m_FragmentsToDisplay;
			CFragmentInfo * m_pAllFragmentsInfos;
			int m_nCurrentFragmentIndex;

			int m_nMaxNumberOfVerticesDisplayed;
			int m_nMaxNumberOfIndicesDisplayed;

#ifndef SPU
			CJobCache();
			~CJobCache();

			void Initialize(const CLockLessStack<BLOB_NUMBER_OF_FRAGMENTS> & fragmentsAvailable, CLockLessStack<BLOB_NUMBER_OF_FRAGMENTS> & fragmentsDisplay, CFragmentInfo * pFragmentInfos);

			// We do not need these for SPU
			void SetPortalMatrices(const matrix3x4_t * pMatrices, int nNumberOfMatrices);
			void ResetBetweenFrames(Point3D & outerBBMins, float fCubeWidth, float fThreshold, ImpParticle* pParticles, int nNumberOfParticles, int nMarginNCubes, bool bPolygonizationEnabled, bool bTileMode);
			void BeginFrame();
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

			void SetupInternalCache();
			void TeardownInternalCache();

			bool GetAvailableFragment();
			void PushCurrentFragmentToDraw();
			bool SwapFragments();

		private:
			FORCEINLINE ImpParticle & GetParticle(int nIndex) const;
			void Calculate(CSection & section, CCornerInfo * corner, int x, int y, int z);
			FORCEINLINE void OptimizedCalcVertex2(fltx4 xyz, fltx4 edgeMask, const CCornerInfo * corner0, const CCornerInfo * corner1, float * positionBuffer);

			CCornerInfo * RetrieveCorner(CSection & section, int x, int y, int z, int nBucketIndex, int nCacheIndex);

			CCornerInfo * FindNewCornerInfo(CSection & section, int x, int y, int z, int nBucketIndex, int nCacheIndex);
		} ALIGN16_POST;
	}
}

#endif

