//========== Copyright (c) Valve Corporation, All rights reserved. ========

#include "blobulator_job.h"
#include "blobulator/job_implementation/job_cthread_data.h"
#include "blobulator/job_implementation/job_cjob_cache.h"
#include "blobulator/util/constants.h"
#include "blobulator/implicit/impparticle.h"

namespace Blobulator
{
	namespace JobImplementation
	{
		// Would it be better for the sw cache to allocate this dynamically?
		// i.e. scratch memory?
		CCornerInfo g_AllCorners[CJobCache::NUM_CORNERS_GROUP_ALLOCATED * CJobCache::NUM_CORNERINFOS_PER_CORNER_GROUP];
	}
}

namespace blobulator_job
{
/*
	void TestSoftwareCache(Blobulator::JobImplementation::CThreadData * pThreadData, Blobulator::JobImplementation::CJobCache * pJobCache)
	{
		using namespace Blobulator::JobImplementation;

		for (int j = 0 ; j < 4 ; ++j)
		{
			// Do not acquire section zero as it is going to assert (we may not need to reference it at all).
			for (int i = 1 ; i < pThreadData->m_nTotalNumberOfSections ; ++i)
			{
				CSection & section = pThreadData->AcquireSection(i);
				BLOB_LOG(BLOB_LOG_VERBOSE, CSection::LogAndCheck(section, NULL));
				pThreadData->ReleaseSection(i, section);
			}

			// ZeroBucket is safe to be acquired as it is often referenced (although it is special cased, so good to test here).
			for (int i = 0 ; i < pJobCache->m_nTotalNumberOfBuckets ; ++i)
			{
				CBucket & bucket = pJobCache->AcquireBucket(i);
				BLOB_LOG(BLOB_LOG_VERBOSE, CBucket::LogAndCheck(bucket, NULL));
				pJobCache->ReleaseBucket(i, bucket);
			}
		}
	}
*/

	void Main(CellSpursJobContext2 * pContext, CellSpursJob256 * pJob)
	{
		using namespace Blobulator::JobImplementation;

		(void)pContext;
		(void)pJob;

#if _DEBUG
		uint64 nRunUnitTests = pJob->workArea.userData[9];
		if (nRunUnitTests)
		{
			CSoftwareCacheUnitTest * pEaUnitTestData = (CSoftwareCacheUnitTest *)nRunUnitTests;
			CSoftwareCacheUnitTest unitTests;
			const int DMA_TAG_UNIT_TESTS = 12;
			cellDmaGet(&unitTests, (uint64)pEaUnitTestData, sizeof(CSoftwareCacheUnitTest), DMA_TAG_UNIT_TESTS, 0, 0);
			cellDmaWaitTagStatusAll(1 << DMA_TAG_UNIT_TESTS);
			if (unitTests.m_nStopInDebugger)
			{
				DebuggerBreak();
			}
			unitTests.RunTest();
			return;
		}
#endif

		// Somehow this generates a warning on this file only.
		// After wasting several minutes on this, I'm just going to comment this.
		// Ah, joy of working with the SPU compiler! :(
		//BLOB_LOG(BLOB_LOG_VERBOSE, "BlobulatorJob is running...\n");

		char * lsThreadData = (char *)(pContext->ioBuffer);
		char * lsJobCache = lsThreadData + sizeof(CThreadData);
		char * lsSectionIndices = lsJobCache + sizeof(CJobCache);
		char * lsParticles = lsSectionIndices + (sizeof(int16) * ALIGN16_TOTAL_NUMBER_OF_SECTIONS);
//		char * lsOutputExtra = lsPointer2 + size2;
//		void * scratch = pContext->sBuffer;

		// Initialize the various pointers (i.e. replace EA by LS)
		CThreadData * pThreadData = (CThreadData *)lsThreadData;

		if (pThreadData->m_bForceSpuBreak)
		{
			DebuggerBreak();
		}

		// Before any calculation, initialize the constants
		Blobulator::Constants::InitializeConstants();

		CJobCache * pJobCache = (CJobCache *)lsJobCache;
		Assert(pJobCache->m_pEaJobCache == pThreadData->m_pJobCache);			// Check that the value is set correctly
		pJobCache->m_pEaJobCache = pThreadData->m_pJobCache;					// Set it anyway just in case...
		pThreadData->m_pJobCache = pJobCache;
		pThreadData->m_pnSectionIndices = (int16 *)lsSectionIndices;
		pThreadData->m_pJobCache->m_pParticles = (ImpParticle *)lsParticles;
		// Note that for the moment, m_pFragmentedVertexBuffer is not converted from EA to LS.
		// We are calling the methods but the methods should not access the members.

		// We need to initialize the CornerInfo on SPU (they are stored locally)
		pJobCache->m_pAllCorners = &g_AllCorners[0];		// Set the local storage address

		// Let's get the output addresses and replace EA by LS

		// Most of this code will have to go in SwapFragments() when the SPU will push every vertex buffers...
		CFragmentInfo & eaFragmentInfo = pThreadData->m_pJobCache->m_EaFragment;
		CFragmentInfo & lsFragmentInfo = pThreadData->m_pJobCache->m_LsFragment;
		lsFragmentInfo = eaFragmentInfo;	// For a start, we want to make sure that they are both in sync (EA being the original version)

		char * lsOutputIndexBuffer = lsParticles + (sizeof(ImpParticle) * pThreadData->m_pJobCache->m_nNumberOfParticles);
		int nSizeIndexBuffer = (lsFragmentInfo.m_nIndexBuffer_EndIndex - lsFragmentInfo.m_nIndexBuffer_StartIndex) * sizeof(unsigned short);
		Assert((nSizeIndexBuffer & 0xf) == 0);
		char * lsOutputVertexBuffer = lsOutputIndexBuffer + nSizeIndexBuffer;

		// Replace EA by LS.
		lsFragmentInfo.m_pCachedIndexBuffer = (unsigned short *)lsOutputIndexBuffer;
		lsFragmentInfo.m_pCachedVertexBuffer = lsOutputVertexBuffer;

		// Update the threshold (clamping to some max values)
		if (lsFragmentInfo.m_nThresholdVerticesOutput > pThreadData->m_pJobCache->m_nMaxNumberOfVerticesDisplayed)
		{
			lsFragmentInfo.m_nThresholdVerticesOutput = pThreadData->m_pJobCache->m_nMaxNumberOfVerticesDisplayed;
		}
		// Remember that m_pCachedIndexBuffer is the address of the beginning of the first buffer.
		// We have to add m_nIndexBuffer_StartIndex to have the correct value for this buffer.
		int nThresholdIndexBuffer = eaFragmentInfo.m_pThresholdIndexBuffer - (eaFragmentInfo.m_pCachedIndexBuffer + eaFragmentInfo.m_nIndexBuffer_StartIndex);
		if (nThresholdIndexBuffer > pThreadData->m_pJobCache->m_nMaxNumberOfIndicesDisplayed)
		{
			nThresholdIndexBuffer = pThreadData->m_pJobCache->m_nMaxNumberOfIndicesDisplayed;
		}
		lsFragmentInfo.m_pThresholdIndexBuffer = lsFragmentInfo.m_pCachedIndexBuffer + nThresholdIndexBuffer;

		// At that point, start / current / end indices are not correct as they are pointing to the big buffer
		// and on the SPU side we are only filling as we had only one buffer.
		// So change the indices so start and current are actually zero.
		lsFragmentInfo.m_nIndexBuffer_EndIndex -= lsFragmentInfo.m_nIndexBuffer_StartIndex;
		lsFragmentInfo.m_nIndexBuffer_StartIndex = lsFragmentInfo.m_nIndexBuffer_CurrentIndex = 0;
		lsFragmentInfo.m_nVertexBuffer_EndIndex -= lsFragmentInfo.m_nVertexBuffer_StartIndex;
		lsFragmentInfo.m_nVertexBuffer_StartIndex = lsFragmentInfo.m_nVertexBuffer_CurrentIndex = 0;

		// At that point, all the important EA have been replaced by LS addresses.
		// Setup everything before the calculation
		pThreadData->SetupInternalCache();
		pJobCache->SetupInternalCache();

/*	#if _DEBUG
		// Try to trash the cache as much as possible
		TestSoftwareCache(pThreadData, pJobCache);
	#endif	*/

		// Execute all the sections
		pThreadData->ExecuteAllSections();

		// The calculation is done, we can shutdown everything...
		pThreadData->TeardownInternalCache();
		pJobCache->TeardownInternalCache();

		//BLOB_LOG(BLOB_LOG_VERBOSE, "BlobulatorJob is done...\n");
	}
}

