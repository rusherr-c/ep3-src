//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "blobulator/section_implementation/cthread_data.h"

namespace Blobulator
{
namespace SectionImplementation
{

bool CThreadData::ExecuteOneSection(CThreadData & threadData, CJobCache & jobCache, Section & section)
{
	if (section.TryLock() == false)
	{
		// Already being used, skip it
		return true;
	}

	// Before we render the next section, let's update the neighbors buckets in the section
	// This should be optimized later - Note that the code could be completely un-looped (although a bit long :)).
	// Assuming that an empty section is represented by a zeroSectiom, containing zeroBuckets then no test is necessary.
	// Note that this could be done by the jobs as well (before rendering the cube).
	// This code has to be optimized.

	// We are looking for the neighbor section (+X, +Y and +Z).
	// skip the update if the section is on the border.
	// Note that as the XMAX+1, YMAX+1 and ZMAX+1 are going to contain zeroBucket, it may not be necessary to remove the final bucket when inserting the particles.
	// We still have to skip the buckets for X=0, Y=0 or Z=0 though.

	if (section.m_nInit == 0)
	{
		section.m_nInit = 1;

		// We only need to do this the first time, as by that time all the sections with buckets have been created.
		// If the section doesn't touch an extreme border (xsize, ysize, zsize), we can update the buckets
		// Also update the section whether or not there are some seeds. As a section with no seed now will probably get some later.
		// This could be slightly more reduced (like by updating the first time there is a seed in the section, so we don't update internal sections with never any seed).

		// For the moment, implement a naive algorithm
		// Loop through 5 * 5 * 5 positions (125), get rid of the 4 * 4 * 4 inside positions (64).
		for (int bucketX = 0 ; bucketX < Section::BUCKETS_PER_SECTION_X ; ++bucketX)
		{
			for (int bucketY = 0 ; bucketY < Section::BUCKETS_PER_SECTION_Y ; ++bucketY)
			{
				for (int bucketZ = 0 ; bucketZ < Section::BUCKETS_PER_SECTION_Z ; ++bucketZ)
				{
					bool isInsideBucket = (bucketX < Section::BUCKETS_PER_SECTION_X - 1);
					isInsideBucket &= (bucketY < Section::BUCKETS_PER_SECTION_Y - 1);
					isInsideBucket &= (bucketZ < Section::BUCKETS_PER_SECTION_Z - 1);
					if (isInsideBucket)
					{
						// The bucket is not coming from a neighbor section.
						// Nothing to do here.
						continue;
					}
					int globalX = bucketX * BUCKET_FACTOR;
					int globalY = bucketY * BUCKET_FACTOR;
					int globalZ = bucketZ * BUCKET_FACTOR;
					if ((section.m_nX + globalX >= X_SIZE) || (section.m_nY + globalY >= Y_SIZE) || (section.m_nZ + globalZ >= Z_SIZE))
					{
						// We can't access outside the range, so skip the test in this case
						continue;
					}

					// The bucket has to be copied from a neighbor section
					int bucketIndex = threadData.m_pSweepRenderer->GetBucketIndexFromXYZ(section.m_nX + globalX, section.m_nY + globalY, section.m_nZ + globalZ);
					section.SetInternalBucket(bucketX, bucketY, bucketZ, bucketIndex);
				}
			}
		}
	}

	bool bWorkRemaining = false;
#if (BLOB_FILL_MODE != BLOB_FILL_MODE_SYNCHRONOUS)
	if (ThreadInterlockedCompareExchange(&section.seed_list.size, 0, 0) != 0)
#else
	if (section.seed_list.size != 0)
#endif
	{
		jobCache.RenderSection(section);
		bWorkRemaining = true;
	}
	section.Unlock();
	return bWorkRemaining;
}

void CThreadData::ExecuteAllSections(CThreadData * pThreadData)
{
	pThreadData->m_bStarted = true;

	// First we execute all the sections specific to this job (it is cache coherent and avoid potential collision with other jobs for most cases).
	pThreadData->ExecuteSections(pThreadData->m_nStartSectionIndex, pThreadData->m_nEndSectionIndex);
	// For similar reasons, let's try to crawl the sections internally
	pThreadData->CrawlSections(pThreadData->m_nStartSectionIndex, pThreadData->m_nEndSectionIndex);

	// From now on, we do this for all available sections
	int nNumIterations = 1;
	for (;;)
	{
		bool bWorkRemaining = pThreadData->ExecuteSections(0, pThreadData->m_nTotalNumberOfSections);
		bWorkRemaining |= pThreadData->CrawlSections(0, pThreadData->m_nTotalNumberOfSections);
		// bWorkRemaining indicates that some work was needed or a section was locked (meaning another job was doing something).
		// In either cases, we have to do a full loop again to make sure that we cover everything.

		++nNumIterations;

#if BLOB_RESET_INSIDE_JOBS
		if (bWorkRemaining == false)
		{
			// Indicate that this job seems done, before stopping let's make sure that the other threads say the same.
			ThreadInterlockedCompareExchange(&pThreadData->m_bWorkRemaining, 0, 1);

			bWorkRemaining = false;
			for (int i = 0 ; i < pThreadData->m_nNumberOfJobs ; ++i)
			{
				if (i == pThreadData->m_nCurrentJobIndex)
				{
					// No need to test the current job
					continue;
				}

				CThreadData * pOtherJob = &pThreadData->m_pAllThreadData[i];
				int oldValue = ThreadInterlockedCompareExchange(&pOtherJob->m_bWorkRemaining, 0, 0);
				if (oldValue)
				{
					// There is some work remaining on another thread, so do not consider this job done yet.
					// It could be because the job did not start yet (The ThreadPool implementation sometimes will spawn the jobs on the same thread).
					int bStarted = ThreadInterlockedCompareExchange(&pOtherJob->m_bStarted, 0, 0);
					if (bStarted)
					{
						// The job started, so there is more work left.
						bWorkRemaining = true;
						break;
					}
					else
					{
						static int s_bJobNotStarted = 0;
						static int s_nNumberOfSections = 0;
						static int s_nIncorrectState = 0;

						++s_bJobNotStarted;
						s_nNumberOfSections = pThreadData->m_nTotalNumberOfSections;

						if (s_nNumberOfSections > 0)
						{
							// Make sure that we have at least several sections
							for (int j = 0 ; j < pThreadData->m_nNumberOfJobs ; ++j)
							{
								CThreadData * pOtherJob2 = &pThreadData->m_pAllThreadData[j];
								if (pOtherJob2->m_bStarted == false)
								{
									// We confirmed that the job did not start
									++s_nIncorrectState;
								}
							}
						}
					}
				}
			}
			if (bWorkRemaining == false)
			{
				// All jobs are pretty much completed with their work, we are done
				break;
			}
		}
		else
		{
			// Actually there is more work to be done, so set back the flag if it was potentially reset
			ThreadInterlockedCompareExchange(&pThreadData->m_bWorkRemaining, 1, 0);
		}
#else
		if (bWorkRemaining == false)
		{
			break;
		}
#endif
	}

	// We did all the sections and there is no work left to be done
	// Push the last fragments.

	CJobCache * pJobCache = pThreadData->m_pJobCache;
	pJobCache->m_pFragmentedVertexBuffer->PushFragmentToDraw(pJobCache->m_Fragment);

	// Reset the sections for the next frame.
#if BLOB_RESET_INSIDE_JOBS
	pThreadData->ResetSections(pThreadData->m_nStartSectionIndex, pThreadData->m_nEndSectionIndex);
	pThreadData->CheckSectionsAreReset(pThreadData->m_nStartSectionIndex, pThreadData->m_nEndSectionIndex);
#endif

	ThreadInterlockedIncrement(&pThreadData->m_bDone);
}

}
}