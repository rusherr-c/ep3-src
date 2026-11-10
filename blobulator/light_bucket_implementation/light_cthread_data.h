//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CTHREAD_DATA_H
#define BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CTHREAD_DATA_H

#include "blobulator/light_bucket_implementation/light_csection.h"
#include "blobulator/light_bucket_implementation/light_csoftware_cache.h"
#include "blobulator/smartarray.h"

namespace Blobulator
{
	namespace LightBucketImplementation
	{
		class CJobCache;
		class CSection;
		class CSectionBlobRenderer;

		class ALIGN16 CThreadData
		{
		public:
			CThreadData();
			~CThreadData();

			int m_nCurrentJobIndex;

			int m_nNumberOfJobs;
			CThreadData * m_pAllThreadData;									// This should not be needed with the current SPU implementation.

			// This variable is here to help the SPU and avoid one level of indirection (one less DMA access).
			// We use this directly instead of using the SmartArray<>.
			CSection * * m_pFirstSection;									// This points to the main memory
																			// The SPU will DMA the content in the local storage.
																			// To reduce unneeded DMA accesses, the sections will be cached
																			// in the local storage.

			int16 * m_pnSectionIndices;

			CJobCache * m_pJobCache;										// On SPU, this needs to be updated with the LS address
			int m_bDone;													// This is the value that we need to set to 1 when we are done
																			// The PPU is expecting this value to be set.
			bool m_bForceSpuBreak;

			typedef CLockLessStack<TOTAL_NUMBER_OF_SECTIONS> SectionStack;

			// These will contain the sections that we are going to execute and parse for each job.
			SectionStack m_SectionStacks[BLOB_NUM_JOBS];
			// This contain the sections that we are going to execute and parse once each job is done.
			SectionStack m_GlobalSectionStack;

			int GetBucketIndexFromXYZ(int globalX, int globalY, int globalZ) const;

			FORCEINLINE
			CThreadData * GetEaThreadData() const
			{
				return m_pAllThreadData + m_nCurrentJobIndex;
			}

			// Returns false if there is more work needed. True if the work is completely done.
			// True is returned when not all sections were executed. Call another time ExecuteAllSections() to continue further.
			bool ExecuteAllSections(bool bMandatory, int nUpToNSections = INT_MAX);

			static void SeedFromFlagsYZ(int x, int y, int z, CSection::Grid2D & flags, CSection & destinationSection);
			static void SeedFromFlagsXZ(int x, int y, int z, CSection::Grid2D & flags, CSection & destinationSection);
			static void SeedFromFlagsXY(int x, int y, int z, CSection::Grid2D & flags, CSection & destinationSection);

			// On the SPU side, we are going to write all the Section info back to the PPU side.
			// This may not be necessary though (but may still be good to have the done state).
			CSection & AcquireSection(int x, int y, int z, int & nOutSectionIndex) const;
			CSection & AcquireSection(int nIndex) const;
			// Should we consider dropping this one in favor of the faster "index-less" version?
			void ReleaseSection(int nIndex, CSection & section) const;

			// This is used for SPU so the state of the section can be read after a lock
			// And written before the unlock. It is a no-op on PC, X360 and Mac.
			bool TryLock(int nSectionIndex, CSection & section);
			void Unlock(int nSectionIndex, CSection & section);

			void SetupInternalCache();
			void TeardownInternalCache();

			// The boolean returned here may not be necessary anymore...
			// After all the state of the stack will indicate if there is some work left or not.
			// We could have the case where all jobs think they are done and the last job is still doing something. 
			// It will be its responsibility to complete the final sections. This should not impact too much the load balancing.
			// Usually the destinationStack is the stack related to the job, so it takes over some of the work.
			// That way, if one job is behind, its stack is going to be spread around by other jobs.
			// It is necessary to have the global stack as it prevents case where we pull a section, it's locked, we have to push it back,
			// but we can't push it back on the same stack to avoid stalling.
			bool ExecuteSections(SectionStack & sourceSectionStack, SectionStack & destinationSectionStack, int nUpToNSections, int & nRefNumberOfSectionsExecuted);
			bool CrawlOneSection(int nSectionIndex, CSection & section, SectionStack & destinationSectionStack);

		} ALIGN16_POST;
	}
}

#endif
