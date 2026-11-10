//========== Copyright (c) Valve Corporation, All rights reserved. ========

#include "job_vjob_instance.h"
#include "vjobs/root.h"
#include "blobulator/implicit/impparticle.h"
#include "blobulator/job_implementation/job_cthread_data.h"
#include "blobulator/job_implementation/job_cjob_cache.h"
#include <ps3/vjobutils.h>

extern IVJobs * g_pVJobs;

namespace Blobulator
{
namespace JobImplementation
{

BlobulatorVJobInstance::BlobulatorVJobInstance()
:
m_pJob(NULL)
{
	// Do nothing...
}

BlobulatorVJobInstance::~BlobulatorVJobInstance()
{
	// Do nothing...
}

void BlobulatorVJobInstance::Init()
{
	g_pVJobs->Register( this );
}

void BlobulatorVJobInstance::Shutdown()
{
	g_pVJobs->Unregister( this );
}

void BlobulatorVJobInstance::OnVjobsInit()
{
	// Do nothing...
}

void BlobulatorVJobInstance::OnVjobsShutdown()
{
	// Do nothing...
}

#ifdef VJOBS_ON_SPURS
CellSpursJob256 * BlobulatorVJobInstance::RunJob(CThreadData & threadData)
{
	CellSpursJob256 * pJob = NewJob256( *m_pRoot->m_pJobBlobulator );
	pJob->header.useInOutBuffer = 1;
	if (IsCert() == false)
	{
		pJob->header.jobType |= CELL_SPURS_JOB_TYPE_MEMORY_CHECK;
	}
	pJob->header.sizeStack = 0;
	
	CDmaListConstructor dmaConstructor( pJob->workArea.dmaList );
	dmaConstructor.AddInputDma(sizeof(threadData), &threadData);
	dmaConstructor.AddInputDma(sizeof(*threadData.m_pJobCache), threadData.m_pJobCache);
	dmaConstructor.AddInputDma(sizeof(int16) * ALIGN16_TOTAL_NUMBER_OF_SECTIONS, threadData.m_pnSectionIndices);
	// Copy just the particles needed - not point copying the max (even if, in the PPU side, we reserve the max anyway).
	dmaConstructor.AddInputDma(sizeof(ImpParticle) * threadData.m_pJobCache->m_nNumberOfParticles, threadData.m_pJobCache->m_pParticles);

	CFragmentInfo & fragmentInfo = threadData.m_pJobCache->m_EaFragment;

	// Size for the index buffer
	int size = (fragmentInfo.m_nIndexBuffer_EndIndex - fragmentInfo.m_nIndexBuffer_StartIndex) * sizeof(unsigned short);
	dmaConstructor.AddSizeInOrInOut(ALIGN_VALUE(size, 16));
	// Size for the vertex buffer
	size = (fragmentInfo.m_nVertexBuffer_EndIndex - fragmentInfo.m_nVertexBuffer_StartIndex) * fragmentInfo.m_nVertexSize;
	dmaConstructor.AddSizeInOrInOut(ALIGN_VALUE(size, 16));

	//	dmaConstructor[9][1] = param;

	dmaConstructor.FinishIoBuffer( &pJob->header );

// No need of scratch-pad memory for the moment
//	pJob->header.sizeScratch = scratchSize / 16;

	int nStatus;
	nStatus = cellSpursCheckJob( pJob, sizeof(*pJob), 256 );
	PrintStatus(nStatus);

	CELL_VERIFY( m_pRoot->m_queuePortBlobulator.pushJob( &pJob->header, sizeof( *pJob ), 0, CELL_SPURS_JOBQUEUE_FLAG_SYNC_JOB ) );
	return pJob;
}

void BlobulatorVJobInstance::WaitEndOfJob(CellSpursJob256 * pJob)
{
	CELL_VERIFY( m_pRoot->m_queuePortBlobulator.sync( 0 ) );
	DeleteJob( pJob );
}
#endif

static bool g_bStopInDebugger = false;

void BlobulatorVJobInstance::RunUnitTest(bool bOnSpu, CSoftwareCacheUnitTest::Stack & stack, CSoftwareCacheUnitTest::UnitTest kind)
{
	// Can't store DMA data on the stack
	CSoftwareCacheUnitTest * pUnitTest = new CSoftwareCacheUnitTest();
	pUnitTest->m_nStopInDebugger = g_bStopInDebugger;
	pUnitTest->SetTestToRun(stack, kind);
	SyncRunUnitTest(bOnSpu, *pUnitTest);
	delete pUnitTest;
}

void BlobulatorVJobInstance::RunUnitTest(bool bOnSpu, CSoftwareCacheUnitTest::Stack & stack, CSoftwareCacheUnitTest::UnitTest kind, int nData)
{
	// Can't store DMA data on the stack
	CSoftwareCacheUnitTest * pUnitTest = new CSoftwareCacheUnitTest();
	pUnitTest->m_nStopInDebugger = g_bStopInDebugger;
	pUnitTest->SetTestToRun(stack, kind, nData);
	SyncRunUnitTest(bOnSpu, *pUnitTest);
	delete pUnitTest;
}

void BlobulatorVJobInstance::RunUnitTest(bool bOnSpu, CSoftwareCacheUnitTest::Stack & stack1, CSoftwareCacheUnitTest::Stack & stack2, CSoftwareCacheUnitTest::UnitTest kind, int nData)
{
	// Can't store DMA data on the stack
	CSoftwareCacheUnitTest * pUnitTest = new CSoftwareCacheUnitTest();
	pUnitTest->m_nStopInDebugger = g_bStopInDebugger;
	pUnitTest->SetTestToRun(stack1, stack2, kind, nData);
	SyncRunUnitTest(bOnSpu, *pUnitTest);
	delete pUnitTest;
}

BlobulatorVJobInstance::AsyncInfo BlobulatorVJobInstance::RunAsyncUnitTest(CSoftwareCacheUnitTest::Stack & stack1, CSoftwareCacheUnitTest::Stack & stack2, CSoftwareCacheUnitTest::UnitTest kind, int nData)
{
	// Can't store DMA data on the stack
	CSoftwareCacheUnitTest * pUnitTest = new CSoftwareCacheUnitTest();
	pUnitTest->m_nStopInDebugger = g_bStopInDebugger;
	pUnitTest->SetTestToRun(stack1, stack2, kind, nData);

	AsyncInfo asyncInfo;
	asyncInfo.m_pUnitTest = pUnitTest;
#ifdef VJOBS_ON_SPURS
	asyncInfo.m_pJob = RunUnitTest(*pUnitTest);
#else
	pUnitTest->RunTest();
#endif

	return asyncInfo;
}

void BlobulatorVJobInstance::WaitAsyncUnitTest(BlobulatorVJobInstance::AsyncInfo & asyncInfo)
{
#ifdef VJOBS_ON_SPURS
	WaitEndOfUnitTest(asyncInfo.m_pJob);
#endif
	delete asyncInfo.m_pUnitTest;
}

void BlobulatorVJobInstance::SyncRunUnitTest(bool bOnSpu, CSoftwareCacheUnitTest & unitTest)
{
#ifdef VJOBS_ON_SPURS
	if (bOnSpu)
	{
		CellSpursJob256 * pJob = RunUnitTest(unitTest);
		WaitEndOfUnitTest(pJob);
	}
	else
#endif
	{
		unitTest.RunTest();
	}
}

#ifdef VJOBS_ON_SPURS
CellSpursJob256 * BlobulatorVJobInstance::RunUnitTest(CSoftwareCacheUnitTest & unitTest)
{
	CellSpursJob256 * pJob = NewJob256( *m_pRoot->m_pJobBlobulator );
	pJob->header.useInOutBuffer = 1;
	if (IsCert() == false)
	{
		pJob->header.jobType |= CELL_SPURS_JOB_TYPE_MEMORY_CHECK;
	}
	pJob->header.sizeStack = 0;

	CDmaListConstructor dmaConstructor( pJob->workArea.dmaList );
	dmaConstructor[9][1] = (uint64)&unitTest;
	dmaConstructor.FinishIoBuffer( &pJob->header );

	int nStatus;
	nStatus = cellSpursCheckJob( pJob, sizeof(*pJob), 256 );
	PrintStatus(nStatus);

	CELL_VERIFY( m_pRoot->m_queuePortBlobulator.pushJob( &pJob->header, sizeof( *pJob ), 0, CELL_SPURS_JOBQUEUE_FLAG_SYNC_JOB ) );
	return pJob;
}

void BlobulatorVJobInstance::WaitEndOfUnitTest(CellSpursJob256 * pJob)
{
	// Wait for the job to complete (to make sure the SPU code is not out of sync).
	CELL_VERIFY( m_pRoot->m_queuePortBlobulator.sync( 0 ) );
	DeleteJob( pJob );
}
#endif

void BlobulatorVJobInstance::PrintStatus(int nStatus)
{
#if _DEBUG
	switch (nStatus)
	{
	case CELL_OK:
		break;
	case CELL_SPURS_JOB_ERROR_INVAL:
		printf("CELL_SPURS_JOB_ERROR_INVAL");
		break;
	case CELL_SPURS_JOB_ERROR_ALIGN:
		printf("CELL_SPURS_JOB_ERROR_ALIGN");
		break;
	case CELL_SPURS_JOB_ERROR_NULL_POINTER:
		printf("CELL_SPURS_JOB_ERROR_NULL_POINTER");
		break;
	case CELL_SPURS_JOB_ERROR_MEMORY_SIZE:
		printf("CELL_SPURS_JOB_ERROR_MEMORY_SIZE");
		break;
	}
#endif
}


}
}

