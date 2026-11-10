//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_SECTIONIMPLEMENTATION_CTHREAD_DATA_H
#define BLOBULATOR_SECTIONIMPLEMENTATION_CTHREAD_DATA_H

#include "blobulator/section_implementation/details.h"
#include "blobulator/section_implementation/csection.h"
#include "blobulator/section_implementation/csection_blob_renderer.h"
#include "blobulator/util/constants.h"

namespace Blobulator
{
	namespace SectionImplementation
	{
		class CJobCache;

		class CThreadData
		{
		public:
			int m_nCurrentJobIndex;

			int m_nStartSectionIndex;
			int m_nEndSectionIndex;
			int m_nTotalNumberOfSections;

			int m_nNumberOfJobs;
			CThreadData * m_pAllThreadData;

			SmartArray<Section *> * m_pAllSections;
			CJobCache * m_pJobCache;
			CSectionBlobRenderer * m_pSweepRenderer;
			int m_bWorkRemaining;
			int m_bDone;
			int m_bStarted;

			// static to make sure we explicitly pass the necessary information
			// Once the code is working, we should change the method back to non-static
			static bool ExecuteOneSection(CThreadData & threadData, CJobCache & jobCache, Section & section);
			static void ExecuteAllSections(CThreadData * pThreadData);


			FORCEINLINE
			static void SeedFromFlagsYZ(int x, int y, int z, Section::Grid2D & flags, Section & destinationSection)
			{
#if BLOB_USE_FLAGS_LIST
				for (int nIndexY = 0 ; nIndexY < 8 ; ++nIndexY)
				{
					uint8 mask = flags.m_nFlags8[nIndexY];
					uint32 flagsList = Constants::s_FlagsLists[mask];
					int counter = flagsList & 0xf;
					flagsList >>= 4;
					while (counter-- > 0)
					{
						int nIndexZ = flagsList & 0x7;
						flagsList >>= 3;
						XYZ temp = MakeXYZ(x, y + nIndexY, z + nIndexZ);
						destinationSection.PushSeed(temp);
					}
				}
#else
				uint64 nMask = flags.m_nFlags64;
				do
				{
					int counter = GetAndRemoveLowestBit(nMask);
					int indexY = (counter >> SECTION_Z_POWER_OF_2);
					int indexZ = (counter & SECTION_Z_MASK);
					XYZ temp = MakeXYZ(x, y + indexY, z + indexZ);
					destinationSection.PushSeed(temp);
				}
				while (nMask != 0);

#endif
			}

			FORCEINLINE
			static void SeedFromFlagsXZ(int x, int y, int z, Section::Grid2D & flags, Section & destinationSection)
			{
#if BLOB_USE_FLAGS_LIST
				for (int nIndexX = 0 ; nIndexX < 8 ; ++nIndexX)
				{
					uint8 mask = flags.m_nFlags8[nIndexX];
					uint32 flagsList = Constants::s_FlagsLists[mask];
					int counter = flagsList & 0xf;
					flagsList >>= 4;
					while (counter-- > 0)
					{
						int nIndexZ = flagsList & 0x7;
						flagsList >>= 3;
						XYZ temp = MakeXYZ(x + nIndexX, y, z + nIndexZ);
						destinationSection.PushSeed(temp);
					}
				}
#else
				uint64 mask = flags.m_nFlags64;
				do 
				{
					int counter = GetAndRemoveLowestBit(mask);
					int indexX = (counter >> SECTION_Z_POWER_OF_2);
					int indexZ = (counter & SECTION_Z_MASK);
					XYZ temp = MakeXYZ(x + indexX, y, z + indexZ);
					destinationSection.PushSeed(temp);
				}
				while (mask != 0);

#endif
			}

			FORCEINLINE
			static void SeedFromFlagsXY(int x, int y, int z, Section::Grid2D & flags, Section & destinationSection)
			{
#if BLOB_USE_FLAGS_LIST
				for (int nIndexX = 0 ; nIndexX < 8 ; ++nIndexX)
				{
					uint8 mask = flags.m_nFlags8[nIndexX];
					uint32 flagsList = Constants::s_FlagsLists[mask];
					int counter = flagsList & 0xf;
					flagsList >>= 4;
					while (counter-- > 0)
					{
						int nIndexY = flagsList & 0x7;
						flagsList >>= 3;
						XYZ temp = MakeXYZ(x + nIndexX, y + nIndexY, z);
						destinationSection.PushSeed(temp);
					}
				}
#else
				uint64 mask = flags.m_nFlags64;
				do 
				{
					int counter = GetAndRemoveLowestBit(mask);
					int indexX = (counter >> SECTION_Y_POWER_OF_2);
					int indexY = (counter & SECTION_Y_MASK);
					XYZ temp = MakeXYZ(x + indexX, y + indexY, z);
					destinationSection.PushSeed(temp);
				}
				while (mask != 0);
#endif
			}

			bool CrawlOneSection(Section & section)
			{
				if (section.TryLock() == false)
				{
					return true;
				}

				bool bWorkRemaining = false;
				if (section.sectionXM.m_nFlags64 != 0)
				{
					bWorkRemaining = true;

					int x = section.m_nX - 1;
					int y = section.m_nY;
					int z = section.m_nZ;
					if (x >= 0)		// Note: This check may be needed due to incorrect reset in some case with multi-threading
						// It would be better to fix the core issue though
					{
						Section * pXmSection = m_pSweepRenderer->GetSection(x, y, z); // If NULL, it means there is no particle associated (like a border), no need to push a seed
						if (pXmSection != &Section::s_ZeroSection)
						{
							// For X, this mask directly matches with the last internalDoneState of the other section
							// Let's see if we can avoid some of them (this masking is really cheap and reduce some seeds).
							//				uint64 otherMask = xmSection->internalDoneState[7];
							//				mask &= otherMask;		// Remove any bit that was already done, this saves a bit of reparsing
							// Somehow this creates more holes in X, let's remove it for the moment...

							if (pXmSection->TryLock())
							{
								SeedFromFlagsYZ(x, y, z, section.sectionXM, *pXmSection);
								pXmSection->Unlock();
								section.sectionXM.m_nFlags64 = 0;	// Reset the flags for next time
							}
						}
						else
						{
							section.sectionXM.m_nFlags64 = 0;	// Reset the flags for next time
						}
					}
					else
					{
						section.sectionXM.m_nFlags64 = 0;	// Reset the flags for next time
					}
				}
				if (section.sectionXP.m_nFlags64 != 0)
				{
					bWorkRemaining = true;

					int x = section.m_nX + SECTION_X_SIZE;
					int y = section.m_nY;
					int z = section.m_nZ;
					if (x < X_SIZE)
					{
						Section * pXpSection = m_pSweepRenderer->GetSection(x, y, z); // If NULL, it means there is no particle associated (like a border), no need to push a seed
						if (pXpSection != &Section::s_ZeroSection)
						{
							// For X, this mask directly matches with the last internalDoneState of the other section
							// Let's see if we can avoid some of them
							//					uint64 otherMask = xpSection->internalDoneState[0];
							//					mask &= ~otherMask;		// Remove any bit that was already done, this saves a bit of reparsing
							// Somehow this creates more holes in X, let's remove it for the moment...

							if (pXpSection->TryLock())
							{
								SeedFromFlagsYZ(x, y, z, section.sectionXP, *pXpSection);
								pXpSection->Unlock();
								section.sectionXP.m_nFlags64 = 0;	// Reset the flags for next time
							}
						}
						else
						{
							section.sectionXP.m_nFlags64 = 0;	// Reset the flags for next time
						}
					}
					else
					{
						section.sectionXP.m_nFlags64 = 0;	// Reset the flags for next time
					}
				}

				// We could do similar masking optimizations for Y and Z, but that would be much more costly.
				// Consider doing doing the masking test as well for Y and Z, but the saving may not be good enough.

				if (section.sectionYM.m_nFlags64 != 0)
				{
					bWorkRemaining = true;

					int x = section.m_nX;
					int y = section.m_nY - 1;
					int z = section.m_nZ;
					if (y >= 0)
					{
						Section * pYmSection = m_pSweepRenderer->GetSection(x, y, z);
						if (pYmSection != &Section::s_ZeroSection)
						{
							if (pYmSection->TryLock())
							{
								SeedFromFlagsXZ(x, y, z, section.sectionYM, *pYmSection);
								pYmSection->Unlock();
								section.sectionYM.m_nFlags64 = 0;	// Reset the flags for next time
							}
						}
						else
						{
							section.sectionYM.m_nFlags64 = 0;	// Reset the flags for next time
						}
					}
					else
					{
						section.sectionYM.m_nFlags64 = 0;	// Reset the flags for next time
					}
				}

				if (section.sectionYP.m_nFlags64 != 0)
				{
					bWorkRemaining = true;

					int x = section.m_nX;
					int y = section.m_nY + SECTION_Y_SIZE;
					int z = section.m_nZ;
					if (y < Y_SIZE)
					{
						Section * pYpSection = m_pSweepRenderer->GetSection(x, y, z);
						if (pYpSection != &Section::s_ZeroSection)
						{
							if (pYpSection->TryLock())
							{
								SeedFromFlagsXZ(x, y, z, section.sectionYP, *pYpSection);
								pYpSection->Unlock();
								section.sectionYP.m_nFlags64 = 0;	// Reset the flags for next time
							}
						}
						else
						{
							section.sectionYP.m_nFlags64 = 0;	// Reset the flags for next time
						}
					}
					else
					{
						section.sectionYP.m_nFlags64 = 0;	// Reset the flags for next time
					}
				}

				if (section.sectionZM.m_nFlags64 != 0)
				{
					bWorkRemaining = true;

					int x = section.m_nX;
					int y = section.m_nY;
					int z = section.m_nZ - 1;
					if (z >= 0)
					{
						Section * pZmSection = m_pSweepRenderer->GetSection(x, y, z);
						if (pZmSection != &Section::s_ZeroSection)
						{
							if (pZmSection->TryLock())
							{
								SeedFromFlagsXY(x, y, z, section.sectionZM, *pZmSection);
								pZmSection->Unlock();
								section.sectionZM.m_nFlags64 = 0;	// Reset the flags for next time
							}
						}
						else
						{
							section.sectionZM.m_nFlags64 = 0;	// Reset the flags for next time
						}
					}
					else
					{
						section.sectionZM.m_nFlags64 = 0;	// Reset the flags for next time
					}
				}

				if (section.sectionZP.m_nFlags64 != 0)
				{
					bWorkRemaining = true;

					int x = section.m_nX;
					int y = section.m_nY;
					int z = section.m_nZ + SECTION_Y_SIZE;
					if (z < Z_SIZE)
					{
						Section * pZpSection = m_pSweepRenderer->GetSection(x, y, z);
						if (pZpSection != &Section::s_ZeroSection)
						{
							if (pZpSection->TryLock())
							{
								SeedFromFlagsXY(x, y, z, section.sectionZP, *pZpSection);
								pZpSection->Unlock();
								section.sectionZP.m_nFlags64 = 0;	// Reset the flags for next time
							}
						}
						else
						{
							section.sectionZP.m_nFlags64 = 0;	// Reset the flags for next time
						}
					}
					else
					{
						section.sectionZP.m_nFlags64 = 0;	// Reset the flags for next time
					}
				}

				section.Unlock();
				return bWorkRemaining;
			}

			bool ExecuteSections(int nStartIndex, int nEndIndex)
			{
				bool bWorkRemaining = false;
				for (int i = nStartIndex ; i < nEndIndex ; ++i)
				{
					Section * section = (*m_pAllSections)[i];
					bWorkRemaining |= ExecuteOneSection(*this, *m_pJobCache,  *section);
				}
				return bWorkRemaining;
			}

			bool CrawlSections(int startIndex, int endIndex)
			{
				bool bWorkRemaining = false;
				for (int i = startIndex ; i < endIndex ; ++i)
				{
					Section * section = (*m_pAllSections)[i];
					bWorkRemaining |= CrawlOneSection(*section);
				}
				return bWorkRemaining;
			}

			void ResetSections(int nStartIndex, int nEndIndex)
			{
				for (int i = nStartIndex ; i < nEndIndex ; ++i)
				{
					Section * section = (*m_pAllSections)[i];

					// Consider adding a lock here?
					section->CheckCrawlingInfoIsReset();	// This should have been reset by now
					section->Reset();
					section->CheckSectionIsReset();
				}
				CheckSectionsAreReset(nStartIndex, nEndIndex);
			}

			void CheckSectionsAreReset(int nStartIndex, int nEndIndex)
			{
				for (int i = nStartIndex ; i < nEndIndex ; ++i)
				{
					Section * section = (*m_pAllSections)[i];
					section->CheckSectionIsReset();
				}
			}

			void CheckCrawlingInfoAreReset(int nStartIndex, int nEndIndex)
			{
				for (int i = nStartIndex ; i < nEndIndex ; ++i)
				{
					Section * section = (*m_pAllSections)[i];
					section->CheckCrawlingInfoIsReset();
				}
			}

		};
	}
}

#endif
