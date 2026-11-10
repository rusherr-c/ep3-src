//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_SECTIONIMPLEMENTATION_CSECTION_H
#define BLOBULATOR_SECTIONIMPLEMENTATION_CSECTION_H

#include "blobulator/section_implementation/details.h"
#include "blobulator/smartarray.h"

#include "tier0/dbg.h"
#include "tier0/threadtools.h"

// Instead of doing slices, we are doing sections of X * Y * Z buckets.
// This allows sections to be spawn on various threads (and SPU).

// The code has to be SPU friendly.
// If we use 8 * 8 * 8 buckets in a section, there would be 512 buckets in a section.
// Each bucket costs 32 bytes, thus 16 Kb would be used for the buckets.
// (need to be ready for all buckets as particles could be all stored in one section, thus filling all the buckets).

// Assuming that we keep the corner cache to 1/4 of the number of buckets,
// the cost for the corner cache is around 32 Kb.
// (need less corners than buckets, because we are only crawling the surface.
// if there are too many particles, then the surface is relatively small.
// If there are less particles, then there is not as much surface to cover).

// Each section has a its own todo list, seed list.
// And another todo list for everything outside a given section.

// Question to investigate:
// Instead of having a big array of buckets. should we actually also store the bucket pointers in their respective sections?
// It would save 200 to 300 Kb (thus maybe better cache friendly?).
// When a bucket is created, we could add to the section. When a bucket is associated with multiple section, we'll add it to all necessary.
// We could even store the bucket directly in the section (without having to allocate buckets).
// That way a section would be fully contained.
// It would need 9 corners by 9 corners by 9 corners. It means 5 * 5 * 5 buckets.
// 125 buckets. (if allocated in place, several buckets will be duplicated across multiple sections).
// 32 bytes per bucket, so a section would contain 4000 bytes. But at that point there is no cache miss anymore, and a more streamlined code.
// In big cases, there are 81 sections displayed.
// Cost would be more than 324000 bytes.
// We are using around 700 to 800 Kb for the moment.
// Once a section and the particles are loaded in memory, there is no more external memory accesses (except to write the polys).
// This would make the code very SPU friendly.

namespace Blobulator
{
	namespace SectionImplementation
	{
		static const int SECTION_X_POWER_OF_2 = 3;
		static const int SECTION_Y_POWER_OF_2 = 3;
		static const int SECTION_Z_POWER_OF_2 = 3;

		static const int SECTION_X_SIZE = 1 << SECTION_X_POWER_OF_2;
		static const int SECTION_Y_SIZE = 1 << SECTION_Y_POWER_OF_2;
		static const int SECTION_Z_SIZE = 1 << SECTION_Z_POWER_OF_2;

		static const int SECTION_X_MASK = SECTION_X_SIZE - 1;
		static const int SECTION_Y_MASK = SECTION_Y_SIZE - 1;
		static const int SECTION_Z_MASK = SECTION_Z_SIZE - 1;

		static const int NUMBER_OF_SECTIONS_IN_X = (RENDERER_CACHE_XSIZE + SECTION_X_SIZE - 1) / SECTION_X_SIZE;
		static const int NUMBER_OF_SECTIONS_IN_Y = (RENDERER_CACHE_YSIZE + SECTION_Y_SIZE - 1) / SECTION_Y_SIZE;
		static const int NUMBER_OF_SECTIONS_IN_Z = (RENDERER_CACHE_ZSIZE + SECTION_Z_SIZE - 1) / SECTION_Z_SIZE;

		static const int TOTAL_NUMBER_OF_SECTIONS = NUMBER_OF_SECTIONS_IN_X * NUMBER_OF_SECTIONS_IN_Y * NUMBER_OF_SECTIONS_IN_Z;

		class Section
		{
		public:
			Section()
				:
				m_nX(0),
				m_nY(0),
				m_nZ(0),
				m_nInit(0),
				m_nSoftLock(0)
			{
				sectionXM.m_nFlags64 = 0;
				sectionXP.m_nFlags64 = 0;
				sectionYM.m_nFlags64 = 0;
				sectionYP.m_nFlags64 = 0;
				sectionZM.m_nFlags64 = 0;
				sectionZP.m_nFlags64 = 0;

				Reset();
			}

			FORCEINLINE
			void PushSeed(XYZ xyz)
			{
				CheckInsideXYZ(xyz);
				seed_list.pushAutoSize(xyz);
			}

			void SetXYZ(int x, int y, int z)
			{
				m_nX = x;
				m_nY = y;
				m_nZ = z;
				m_nInit = false;

				CheckSectionIsReset();
			}

			void Reset()
			{
				//m_nInit = 0;

				// Optimize this once the code is set in stone
				// Partially done... Next step: VMX or prezero?
				internalDoneState[0].m_nFlags64 = 0;
				internalDoneState[1].m_nFlags64 = 0;
				internalDoneState[2].m_nFlags64 = 0;
				internalDoneState[3].m_nFlags64 = 0;
				internalDoneState[4].m_nFlags64 = 0;
				internalDoneState[5].m_nFlags64 = 0;
				internalDoneState[6].m_nFlags64 = 0;
				internalDoneState[7].m_nFlags64 = 0;

				memset(mBucketsInSection, 0, sizeof(mBucketsInSection));

		/*
		 * We can't always store a full cache-line in the 250 bytes.
		   So this code is not correct.

				// We know the size ahead of time (125).
				const int TOTAL_SIZE_TO_ERASE = BUCKETS_PER_SECTION_X * BUCKETS_PER_SECTION_Y * BUCKETS_PER_SECTION_Z;

				uint16 * pStartZoneToErase = mBucketsInSection;
				uint16 * pEndZoneToErase = pStartZoneToErase + TOTAL_SIZE_TO_ERASE;

				// We have 250 bytes to erase as fast as possible
				// Within 250 bytes, there is at least a full cache line.
				int tempPointer = reinterpret_cast<int>(pStartZoneToErase);
				int startCacheLine = (tempPointer + CACHE_LINE_SIZE) & -CACHE_LINE_SIZE;
				PREZERO_128(reinterpret_cast<void *>(startCacheLine), 0);
				int endCacheLine = startCacheLine + CACHE_LINE_SIZE;

				// We erased the middle of the buffer (half of it) as quickly as possible
				// Now we need to erase the beginning and the end
				// For the moment, we use memset, we could use VMX
				int beginingSize = reinterpret_cast<char *>(startCacheLine) - reinterpret_cast<char *>(pStartZoneToErase);
				memset(pStartZoneToErase, 0, beginingSize);
				int endSize = reinterpret_cast<char *>(pEndZoneToErase) - reinterpret_cast<char *>(endCacheLine);
				memset(reinterpret_cast<void *>(endCacheLine), 0, endSize);
		*/

				CheckSectionIsReset();
			}

			void CheckCrawlingInfoIsReset()
			{
		#if _DEBUG
				while (TryLock() == false)
				{
					ThreadSleep();
				}
				Assert(ThreadInterlockedCompareExchange(&seed_list.size, 0, 0) == 0);

				Assert(sectionXM.m_nFlags64 == 0);
				Assert(sectionXP.m_nFlags64 == 0);
				Assert(sectionYM.m_nFlags64 == 0);
				Assert(sectionYP.m_nFlags64 == 0);
				Assert(sectionZM.m_nFlags64 == 0);
				Assert(sectionZP.m_nFlags64 == 0);

				Unlock();
		#endif
			}

			void CheckSectionIsReset()
			{
		#if _DEBUG
				//Assert(m_nInit == false);

				CheckCrawlingInfoIsReset();

				while (TryLock() == false)
				{
					ThreadSleep();
				}

				Assert(internalDoneState[0].m_nFlags64 == 0);
				Assert(internalDoneState[1].m_nFlags64 == 0);
				Assert(internalDoneState[2].m_nFlags64 == 0);
				Assert(internalDoneState[3].m_nFlags64 == 0);
				Assert(internalDoneState[4].m_nFlags64 == 0);
				Assert(internalDoneState[5].m_nFlags64 == 0);
				Assert(internalDoneState[6].m_nFlags64 == 0);
				Assert(internalDoneState[7].m_nFlags64 == 0);

				const int TOTAL_SIZE_TO_CHECK = BUCKETS_PER_SECTION_X * BUCKETS_PER_SECTION_Y * BUCKETS_PER_SECTION_Z;
				for (int i = 0 ; i < TOTAL_SIZE_TO_CHECK ; ++i)
				{
					Assert(mBucketsInSection[i] == 0);
				}

				Unlock();
		#endif
			}

		// Use this define if some incorrect XYZ are introduced in the sections.
		//#define BLOB_CHECK_VALUES

		#if BLOB_CHECK_VALUES
			FORCEINLINE
			void CheckInsideXYZ(XYZ xyz)
			{
				int x = XYZ_GetX(xyz);
				int y = XYZ_GetY(xyz);
				int z = XYZ_GetZ(xyz);
				CheckInsideXYZ(x, y, z);
			}

			FORCEINLINE
			bool IsInside(int x, int y, int z)
			{
				bool inside = ((x >= m_nX) && (x <= m_nX + SECTION_X_SIZE - 1));
				inside &= ((y >= m_nY) && (y <= m_nY + SECTION_Y_SIZE - 1));
				inside &= ((z >= m_nZ) && (z <= m_nZ + SECTION_Z_SIZE - 1));
				return (inside);
			}

			FORCEINLINE
			void CheckInsideXYZ(int x, int y, int z)
			{
				Assert(IsInside(x, y, z));
			}

			FORCEINLINE
			void CheckOutsideXYZ(XYZ xyz)
			{
				int x = XYZ_GetX(xyz);
				int y = XYZ_GetY(xyz);
				int z = XYZ_GetZ(xyz);
				CheckOutsideXYZ(x, y, z);
			}

			FORCEINLINE
			void CheckOutsideXYZ(int x, int y, int z)
			{
				Assert(IsInside(x, y, z) == false);
			}

		#else
			FORCEINLINE void CheckInsideXYZ(int, int, int) {}
			FORCEINLINE void CheckInsideXYZ(XYZ) {}
			FORCEINLINE void CheckOutsideXYZ(int, int, int) {}
			FORCEINLINE void CheckOutsideXYZ(XYZ) {}
		#endif

			// The absolute max number of potential outside_todo_list is:
			//	One XYZ for each cube in each face. (8 * 8 * 6 cells). 384 cells = 768 bytes.
			// Although we could reduce this by using bitmasks:
			//	Say each info would be stored in a bit.
			//  For a 2D face, it would take 8 bytes. (8 * 8 bits).
			//  The full memory cost would be: 48 bytes.
			// Then when we finish the section, we could easily detect which section has to be calculated (a single uint64 test).
			// If a given section has to be calculated, only at that time we need to add the XYZ on the todo list.
			// To make the process more efficient, we should do a first pass on all the sections.
			// Once it is done, we need to list the sections that are neighbors to calculate and respawn it as needed.
			// At first, we will have 2197 sections to test. (a bit high BTW. Have to improve the particle testing so we can quickly figure out the sections).
			// After displaying everything, we may have 20 or so sections to display, then 2, 1 and 0.

			union Grid2D
			{
				uint64	m_nFlags64;
				uint32	m_nFlags32[2];
				uint8	m_nFlags8[8];
			};

			// Instead of using a SmartArray to store the list of cubes that have to be parsed in the other section.
			// We store the 2D info on a bit map (per face XM = face X - 1, XP = face X + 1, etc...).
			// With the SmartArray, we often have 126 items allocated, so the cost is 12 + 504 bytes.
			// With this method, we only need 48 bytes. It also helps fill the section later.
			Grid2D sectionXM, sectionXP, sectionYM, sectionYP, sectionZM, sectionZP;

			// We are only using a done state.
			//	There are two cases:
			//	We are seeding the cube, and we want to make sure that we are not doing anything.
			//  We are stacking a cube after crawling, in this case the cube is not really done, but it will be done before leaving the function.
			// At the end of the day, both case can be handled with one state only.
			Grid2D internalDoneState[8];

			// Average capacity is around 62. I.e. 248 bytes (on top of memory used for the SmartArray class).
			SmartArray<XYZ, false, 0> seed_list;
			volatile int m_nSoftLock;

		#if (BLOB_FILL_MODE != BLOB_FILL_MODE_SYNCHRONOUS)
			bool TryLock()
			{
				int oldValue = ThreadInterlockedCompareExchange(&m_nSoftLock, 1, 0);
				return (oldValue == 0);
			}

			void Unlock()
			{
				int oldValue;
				oldValue = ThreadInterlockedCompareExchange(&m_nSoftLock, 0, 1);
				Assert(oldValue == 1);
			}
		#else
			bool TryLock()
			{
				// Do nothing...
				return true;
			}

			void Unlock()
			{
				// Do nothing...
			}
		#endif

			// The idea here is that instead of having a big 3D array with all the buckets
			// 51 * 51 * 51 bucket pointers = 518 Kb that are accessed randomly
			// We want each section (which are already spatial partitioning) to contain buckets.
			// That way, we get rid of 90% of the main bucket array and significantly improve cache coherency.
			// In the current configuration, there are normally 4 * 4 * 4 buckets per section.
			// (i.e. a section is 8 * 8 * 8 cubes, a bucket is 2 * 2 * 2 cubes).
			// But the last cube of a section needs to access the buckets of the next section.
			// Instead of constantly looking for the adjacent sections, we store the neighbor bucket pointers here.
			// This makes the code tight-loop simpler, and the whole section has everything needed.
			static const int BUCKETS_PER_SECTION_X = (SECTION_X_SIZE / BUCKET_FACTOR) + 1;
			static const int BUCKETS_PER_SECTION_Y = (SECTION_Y_SIZE / BUCKET_FACTOR) + 1;
			static const int BUCKETS_PER_SECTION_Z = (SECTION_Z_SIZE / BUCKET_FACTOR) + 1;
			static const int BUCKETS_PER_SECTION_X_MASK = (SECTION_X_SIZE / BUCKET_FACTOR) - 1;
			static const int BUCKETS_PER_SECTION_Y_MASK = (SECTION_Y_SIZE / BUCKET_FACTOR) - 1;
			static const int BUCKETS_PER_SECTION_Z_MASK = (SECTION_Z_SIZE / BUCKET_FACTOR) - 1;
			uint16 mBucketsInSection[BUCKETS_PER_SECTION_X * BUCKETS_PER_SECTION_Y * BUCKETS_PER_SECTION_Z];

			int GetInternalBucket(int bucketX, int bucketY, int bucketZ) const
			{
				Assert((bucketX >= 0) && (bucketX < BUCKETS_PER_SECTION_X));
				Assert((bucketY >= 0) && (bucketY < BUCKETS_PER_SECTION_Y));
				Assert((bucketZ >= 0) && (bucketZ < BUCKETS_PER_SECTION_Z));
				int index = (((bucketX * BUCKETS_PER_SECTION_Y) + bucketY) * BUCKETS_PER_SECTION_Z) + bucketZ;
				return mBucketsInSection[index];
			}

			int GetInternalBucket(int index) const
			{
				Assert((index >= 0) && (index < BUCKETS_PER_SECTION_X * BUCKETS_PER_SECTION_Y * BUCKETS_PER_SECTION_Z));
				return mBucketsInSection[index];
			}

			void SetInternalBucket(int bucketX, int bucketY, int bucketZ, int bucketIndex)
			{
				Assert((bucketX >= 0) && (bucketX < BUCKETS_PER_SECTION_X));
				Assert((bucketY >= 0) && (bucketY < BUCKETS_PER_SECTION_Y));
				Assert((bucketZ >= 0) && (bucketZ < BUCKETS_PER_SECTION_Z));
				int index = (((bucketX * BUCKETS_PER_SECTION_Y) + bucketY) * BUCKETS_PER_SECTION_Z) + bucketZ;
				mBucketsInSection[index] = bucketIndex;
			}

			static int GetBucketIndexX(int x)
			{
				return x * BUCKETS_PER_SECTION_Y * BUCKETS_PER_SECTION_Z;
			}

			static int GetBucketIndexY(int y)
			{
				return y * BUCKETS_PER_SECTION_Z;
			}

			static int GetBucketIndexZ(int z)
			{
				return z;
			}

			int8 m_nX, m_nY, m_nZ, m_nInit;

			static Section s_ZeroSection;
		};
	}
}

#endif

