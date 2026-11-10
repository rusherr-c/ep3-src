//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_SECTIONIMPLEMENTATION_CJOB_CACHE_H
#define BLOBULATOR_SECTIONIMPLEMENTATION_CJOB_CACHE_H

#include "blobulator/section_implementation/cbucket.h"
#include "blobulator/section_implementation/cfragment_info.h"
#include "blobulator/section_implementation/details.h"
#include "blobulator/section_implementation/ccorner_info.h"

#include "blobulator/point3d.h"
#include "blobulator/smartarray.h"

#include "tier0/cache_hints.h"

class ImpParticle;

namespace Blobulator
{
	namespace SectionImplementation
	{
		class CBucket;
		class CFragmentedVertexBuffer;

		class ALIGN16 CJobCache
		{
		public:
			CJobCache();
			~CJobCache();

			// Ugly! Everything is public now as the code is changed a lot in few hours
			// TODO: Clean this once the code is in a more stable state.
			Point3D m_OuterBBMins;
			SmartArray<ImpParticle*> * m_pParticles;
			SmartArray<CBucket *> * m_pBuckets;
			float m_cubeWidth;
			int m_nCurrentCornerIndex;
			CornerInfo * m_pAllCorners;
			ThreadHandle_t m_ThreadHandle;
			bool m_bPoligonizationEnabled;
			bool m_bTileMode;
			int m_nMarginNCubes;
			int m_nJobIndex;
			int m_nCurTime;

			fltx4 g_f4Fourpt25s;
			fltx4 g_f4scalerSq;
			fltx4 g_f4cutoffRSq;
			fltx4 g_f4Threshold;

		/*
			unsigned short * m_pCachedIndexBuffer;
			int m_nIndexBuffer_CurrentIndex;
			void * m_pCachedVertexBuffer;
			int m_nVertexBuffer_CurrentIndex;
			int m_nVertexSize;

			int m_nVertexBuffer_StartFragmentIndex;
			int m_nIndexBuffer_StartFragmentIndex;

			int m_nIndexBuffer_LastIndex;
			int m_nVertexBuffer_LastIndex;
		*/
			CFragmentInfo m_Fragment;
			CFragmentedVertexBuffer * m_pFragmentedVertexBuffer;

			// 32 to make sure there is no re-sizing (this would not be thread safe).

			void ResetBetweenFrames(Point3D & outerBBMins, float fCubeWidth, SmartArray<ImpParticle*> * pParticles, SmartArray<CBucket *> * pBuckets, int nMarginNCubes, bool bPolygonizationEnabled, bool bTileMode);
			void BeginFrame(void * pRenderContext);
			void EndFrame();
			void BeginTile();
			void EndTile();

			void Calculate(CBucket * bucket, int x, int y, int z, CornerInfo * corner);
			void Calculate(CBucket * bucket);
			bool RenderSection(Section & section);
			FORCEINLINE void OptimizedCalcVertex2(fltx4 xyz, fltx4 edgeMask, const CornerInfo * corner0, const CornerInfo * corner1, float * positionBuffer);
			static uintp CalculateSection(void * pParam);

			FORCEINLINE
			void LazyCalculate(CBucket * bucket)
			{
				if (bucket->IsCalculated(m_nJobIndex) == false)
				{
					Calculate(bucket);
				}
			}

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

			FORCEINLINE
			CornerInfo * FillNewCornerInfo(CBucket * bucket)
			{
				CornerInfo * corner;
				CBucket * replacedBucket;
				int offset = m_nCurrentCornerIndex++;		// Only increased once per lookup
				// We do not want the collision detection to increase too quickly the counter
				for ( ; ; )
				{
					offset %= NUM_CORNERS_GROUP_ALLOCATED;

					corner = m_pAllCorners + (offset * NUM_CORNERINFOS_PER_CORNER_GROUP);

					// Prefetch 8 cache lines ahead, which pretty much represent 4 corners group.
					// And we prefetch a whole corner group (8 corners - 256 bytes).
					PREFETCH_128(corner, 8 * CACHE_LINE_SIZE);
					PREFETCH_128(corner, 9 * CACHE_LINE_SIZE);

					replacedBucket = corner->bucket;
					if (replacedBucket == NULL)
					{
						// Not used by another bucket yet, we can use it directly
						break;
					}

					// The Bucket is potentially a neighbor that we need for the calculation as well
					// We don't want to remove it from the cache as it would invalidate the calculation, then potentially crash

					int diffX = FastAbs(bucket->m_nX - replacedBucket->m_nX);
					int diffY = FastAbs(bucket->m_nY - replacedBucket->m_nY);
					int diffZ = FastAbs(bucket->m_nZ - replacedBucket->m_nZ);

					if ((diffX | diffY | diffZ) >= 2 * BUCKET_FACTOR)
					{
						// The bucket is not a neighbor (within -1 / +1 in any axis), we can use it

						// Indicate that the corner is not valid anymore for this bucket
						// (this will force a recalculation at the next usage of that bucket).
						replacedBucket->m_pCorners[m_nJobIndex] = NULL;
						break;
					}

					// It is a neighbor, we need to find another corner (with another bucket).
					// We don't want to jump to near as we may encounter the same collision later,
					// or this will remove the calculation on buckets that we need.
					// Jump at the opposite of the cache plus some prime number
					offset += (NUM_CORNERS_GROUP_ALLOCATED / 4) + 5;

					// Don't this will mess the prefetch during collision, but it happens rarely enough (0.1%).
				}

				bucket->m_pCorners[m_nJobIndex] = corner;
				corner->bucket = bucket;
				return corner;
			}
		} ALIGN16_POST;
	}
}

#endif

