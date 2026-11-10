//========== Copyright (c) Valve Corporation, All rights reserved. ========

#ifndef BLOBULATOR_JOBIMPLEMENTATION_BLOBULATORVJOBINSTANCE_H
#define BLOBULATOR_JOBIMPLEMENTATION_BLOBULATORVJOBINSTANCE_H

#include "vjobs_interface.h"
#include "blobulator/job_implementation/csoftware_cache.h"
#include <cell/spurs.h>

namespace Blobulator
{
	namespace JobImplementation
	{
		class CThreadData;

		class BlobulatorVJobInstance : public VJobInstance
		{
		public:
			BlobulatorVJobInstance();
			virtual ~BlobulatorVJobInstance();

			void Init();
			void Shutdown();

			virtual void OnVjobsInit();
			virtual void OnVjobsShutdown();

#ifdef VJOBS_ON_SPURS
			CellSpursJob256 * RunJob(CThreadData & threadData);
			void WaitEndOfJob(CellSpursJob256 * pJob);
#endif

			void RunUnitTest(bool bOnSpu, CSoftwareCacheUnitTest::Stack & stack, CSoftwareCacheUnitTest::UnitTest kind);
			void RunUnitTest(bool bOnSpu, CSoftwareCacheUnitTest::Stack & stack, CSoftwareCacheUnitTest::UnitTest kind, int nData);
			void RunUnitTest(bool bOnSpu, CSoftwareCacheUnitTest::Stack & stack1, CSoftwareCacheUnitTest::Stack & stack2, CSoftwareCacheUnitTest::UnitTest kind, int nData);

			struct AsyncInfo
			{
				CSoftwareCacheUnitTest * m_pUnitTest;
#ifdef VJOBS_ON_SPURS
				CellSpursJob256 * m_pJob;
#endif
			};

			AsyncInfo RunAsyncUnitTest(CSoftwareCacheUnitTest::Stack & stack1, CSoftwareCacheUnitTest::Stack & stack2, CSoftwareCacheUnitTest::UnitTest kind, int nData);
			void WaitAsyncUnitTest(AsyncInfo & asyncInfo);

		private:
			void SyncRunUnitTest(bool bOnSpu, CSoftwareCacheUnitTest & unitTest);

#ifdef VJOBS_ON_SPURS
			CellSpursJob256 * m_pJob;

			CellSpursJob256 * RunUnitTest(CSoftwareCacheUnitTest & unitTest);
			void WaitEndOfUnitTest(CellSpursJob256 * pJob);
#endif

			void PrintStatus(int nStatus);
		};
	}
}

#endif

