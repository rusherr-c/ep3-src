//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CSECTION_H
#define BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CSECTION_H

#include "blobulator/light_bucket_implementation/light_details.h"
#include "blobulator/smartarray.h"
#include "blobulator/light_bucket_implementation/light_csoftware_cache.h"

#ifndef SPU
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#else
#include <string.h>		// For memset()
#include "spu/light_threadtools.h"
#endif


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
	namespace LightBucketImplementation
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
		static const int ALIGN16_TOTAL_NUMBER_OF_SECTIONS = ALIGN_VALUE(TOTAL_NUMBER_OF_SECTIONS, 16);

		FORCEINLINE
		static int GetSectionArrayIndex(int nGlobalX, int nGlobalY, int nGlobalZ)
		{
			int nSectionX = nGlobalX >> SECTION_X_POWER_OF_2;
			int nSectionY = nGlobalY >> SECTION_Y_POWER_OF_2;
			int nSectionZ = nGlobalZ >> SECTION_Z_POWER_OF_2;
			Assert((nSectionX >= 0) && (nSectionX < NUMBER_OF_SECTIONS_IN_X));
			Assert((nSectionY >= 0) && (nSectionY < NUMBER_OF_SECTIONS_IN_Y));
			Assert((nSectionZ >= 0) && (nSectionZ < NUMBER_OF_SECTIONS_IN_Z));
			int nIndex = (((nSectionX * NUMBER_OF_SECTIONS_IN_Y) + nSectionY) * NUMBER_OF_SECTIONS_IN_Z) + nSectionZ;
			return nIndex;
		}

		class ALIGN16 CSection
		{
		public:
			CSection()
				:
				m_nSoftLock(0),
				m_nX(0),
				m_nY(0),
				m_nZ(0),
				m_nInit(0),
				m_nOnAStack(false),
				m_nNumberOfParticles(0)
			{
				m_pEaSection = this;

				ClearCrawlingState();
				ClearDoneState();
			}

			FORCEINLINE
			void PushSeed(XYZ xyz)
			{
				CheckInsideXYZ(xyz);
				m_SeedList.pushAutoSize(xyz);
			}

			void ResetXYZ(int x, int y, int z)
			{
				m_nX = x;
				m_nY = y;
				m_nZ = z;
				m_nInit = false;			// Will reset the section next time we successfully lock it
				m_nNumberOfParticles = 0;
				m_SeedList.SetSize(0);
				Assert(m_nOnAStack == false);
			}

			void ClearDoneState()
			{
				// Optimize this once the code is set in stone
				// Partially done... Next step: VMX or pre-zero?
				m_InternalDoneState[0].m_nFlags64 = 0;
				m_InternalDoneState[1].m_nFlags64 = 0;
				m_InternalDoneState[2].m_nFlags64 = 0;
				m_InternalDoneState[3].m_nFlags64 = 0;
				m_InternalDoneState[4].m_nFlags64 = 0;
				m_InternalDoneState[5].m_nFlags64 = 0;
				m_InternalDoneState[6].m_nFlags64 = 0;
				m_InternalDoneState[7].m_nFlags64 = 0;
			}

			void ClearCrawlingState()
			{
				// Optimize this once the code is set in stone
				// Partially done... Next step: VMX or pre-zero?
				m_SectionXM.m_nFlags64 = 0;
				m_SectionXP.m_nFlags64 = 0;
				m_SectionYM.m_nFlags64 = 0;
				m_SectionYP.m_nFlags64 = 0;
				m_SectionZM.m_nFlags64 = 0;
				m_SectionZP.m_nFlags64 = 0;
			}

			void CheckCrawlingInfoIsReset()
			{
		#if _DEBUG
				if ((m_nX != 0) || (m_nY != 0) || (m_nZ != 0))
				{
					while (TryLock() == false)
					{
						ThreadSleep();
					}
				}

				Assert(m_SeedList.GetSize() == 0);

				Assert(m_SectionXM.m_nFlags64 == 0);
				Assert(m_SectionXP.m_nFlags64 == 0);
				Assert(m_SectionYM.m_nFlags64 == 0);
				Assert(m_SectionYP.m_nFlags64 == 0);
				Assert(m_SectionZM.m_nFlags64 == 0);
				Assert(m_SectionZP.m_nFlags64 == 0);

				if ((m_nX != 0) || (m_nY != 0) || (m_nZ != 0))
				{
					Unlock();
				}
		#endif
			}

			void CheckSectionIsReset()
			{
		#if _DEBUG
				//Assert(m_nInit == false);

				CheckCrawlingInfoIsReset();

				// Lets do an exception for ZeroSection as it will be on the LS
				if ((m_nX != 0) || (m_nY != 0) || (m_nZ != 0))
				{
					while (TryLock() == false)
					{
						ThreadSleep();
					}
				}

				Assert(m_InternalDoneState[0].m_nFlags64 == 0);
				Assert(m_InternalDoneState[1].m_nFlags64 == 0);
				Assert(m_InternalDoneState[2].m_nFlags64 == 0);
				Assert(m_InternalDoneState[3].m_nFlags64 == 0);
				Assert(m_InternalDoneState[4].m_nFlags64 == 0);
				Assert(m_InternalDoneState[5].m_nFlags64 == 0);
				Assert(m_InternalDoneState[6].m_nFlags64 == 0);
				Assert(m_InternalDoneState[7].m_nFlags64 == 0);

				Assert(m_nNumberOfParticles == 0);

				if ((m_nX != 0) || (m_nY != 0) || (m_nZ != 0))
				{
					Unlock();
				}
		#endif
			}

		// Use this define if some incorrect XYZ are introduced in the sections.

		#if _DEBUG
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
			Grid2D m_SectionXM, m_SectionXP, m_SectionYM, m_SectionYP, m_SectionZM, m_SectionZP;

			// We are only using a done state.
			//	There are two cases:
			//	We are seeding the cube, and we want to make sure that we are not doing anything.
			//  We are stacking a cube after crawling, in this case the cube is not really done, but it will be done before leaving the function.
			// At the end of the day, both case can be handled with one state only.
			Grid2D m_InternalDoneState[8];

			// Instead of the dynamic allocation, switch to a fixed allocation.
			// 64 seeds should be enough for all cases. i.e. that would mean 64 particles in one section,
			// but in that case, the section would be certainly completely filled, so the surface would be easy to seed.
			// The most tricky part could happen if we had many particles in a section completely unrelated to each other,
			// in that case, we would lose the seeds over 64. This can't happen with the blobulator though.
			FixedArray<XYZ, 64> m_SeedList;

			volatile int m_nSoftLock;

		#if (BLOB_FILL_MODE != BLOB_FILL_MODE_SYNCHRONOUS)
			bool TryLock()
			{
				volatile int * pnEaSoftLock = &m_pEaSection->m_nSoftLock;
				bool bLocked = ThreadInterlockedAssignIf(pnEaSoftLock, 1, 0);
				return bLocked;
			}

			void Unlock()
			{
				bool bAssigned;
				volatile int * pEaSoftLock = &m_pEaSection->m_nSoftLock;
				bAssigned = ThreadInterlockedAssignIf(pEaSoftLock, 0, 1);
				Assert(bAssigned);
			}
		#else
			bool TryLock()
			{
				return true;
			}

			void Unlock()
			{
				// Do nothing...
			}
		#endif

			void Initialize();

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
			static const int NUMBER_OF_BUCKETS_IN_SECTION = BUCKETS_PER_SECTION_X * BUCKETS_PER_SECTION_Y * BUCKETS_PER_SECTION_Z;

			static int GetBucketIndexX(int x)
			{
				BLOB_COMPILE_TIME_ASSERT((BUCKETS_PER_SECTION_Y * BUCKETS_PER_SECTION_Z) == 25);
				int nResult = (x << 4) + (x << 3) + x;			// Do the mult by 5 ourselves as some compilers do not do it.
				Assert(nResult == (x * BUCKETS_PER_SECTION_Y * BUCKETS_PER_SECTION_Z));
				return nResult;
			}

			static int GetBucketIndexY(int y)
			{
				BLOB_COMPILE_TIME_ASSERT(BUCKETS_PER_SECTION_Z == 5);
				int nResult = (y << 2) + y;			// Do the mult by 5 ourselves as some compilers do not do it
				Assert(nResult == (y * BUCKETS_PER_SECTION_Z));
				return nResult;
			}

			static int GetBucketIndexZ(int z)
			{
				return z;
			}

			int8 m_nX, m_nY, m_nZ, m_nInit;
			CSection * m_pEaSection;			// Stores the EA so we can quickly access variables on SPU
			int16 m_nSectionIndex;
			int16 m_nOnAStack;

			// Let's use int8 and not uint8
			// Around 64 corners expected. 125 max anyway for a section.
			// -1 is used for not defined. Can be changed as needed though.
			// On the SPU implementation, there will be only one corner cache.
			// Each SPU will have its own corner cache that will not be shared with other SPUs.
			// The corner indices will be DMA in and out on each SPUs, when loading an index from another SPU,
			// there is check to make sure that the index is correct.
			int8 m_nCornerIndices[NUM_CORNERS_CACHE][NUMBER_OF_BUCKETS_IN_SECTION];

			// These members will not be DMA out from LS to EA on SPU
			// Make sure that there is no consequence, and that if these fields are updated by the SPU they have to stay local to the SPU.
			int GetSizeToDmaOut() const
			{
				int nEnd = (int)&m_nNumberOfParticles;
				int nStart = (int)this;
				int nSize = ALIGN_VALUE(nEnd - nStart, 16);
				return nSize;
			}

			int16 m_nNumberOfParticles;			// Use uint8 if number is MAX_NUMBER_OF_PARTICLES_IN_SECTION is 255 or less :)
			static const int MAX_NUMBER_OF_PARTICLES_IN_SECTION = 256;	// Up to 256 particles on a given section (too much - too little?)
																		// This can happen often when the blob is switching between two horizontal portals
																		// especially when coming from a sprayer.
			uint8 m_nParticleIndices[MAX_NUMBER_OF_PARTICLES_IN_SECTION];	

			void Log() const;
			static void LogAndCheck(const CSection & section, CSection * pEaSection, bool log = true);


			// Size for CSection:
			//	6 * 8 = 48 bytes for crawling seed list.
			//	8 * 8 = 64 bytes for done list.
			//	64 bytes for seed list.
			//	256 bytes for particle indices.
			//	125 bytes for corner indices.
			//  28 bytes for various info (could be more).
			// Total size: ~585 bytes.

		} ALIGN16_POST;

		BLOB_COMPILE_TIME_ASSERT((sizeof(CSection) & 0Xf) == 0);
	}
}

#endif

