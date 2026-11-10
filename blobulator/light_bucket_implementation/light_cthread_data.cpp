//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "blobulator/light_bucket_implementation/light_cthread_data.h"
#include "blobulator/light_bucket_implementation/light_cjob_cache.h"
#include "blobulator/light_bucket_implementation/light_cfragmented_vertex_buffer.h"

#ifndef SPU
#include "blobulator/light_bucket_implementation/light_csection_blob_renderer.h"
#include "tier1/strtools.h"
#else
#include <spu_mfcio.h>
#endif

namespace Blobulator
{
namespace LightBucketImplementation
{

CThreadData::CThreadData()
	:
	m_nCurrentJobIndex(0),
	m_nNumberOfJobs(0),
	m_pAllThreadData(NULL),
	m_pFirstSection(NULL),
	m_pnSectionIndices(NULL),
	m_pJobCache(NULL),
	m_bDone(false)
{
	// Do nothing...
}

CThreadData::~CThreadData()
{
	// Do nothing...
}

	// There are two models regarding the locks:
	//	If only one SPU is rendering the sections:
	//		- We do not need to lock
	//		- We can DMA in only when we acquire the section
	//		- We can DMA out only when we release the section
	//	If more than one SPU is rendering the sections:
	//		- We need to lock the section
	//		- We have to DMA in only after the lock (as another SPU may have changed the content of the cached section).
	//		- We have to DMA out just before releasing the lock (for the next SPU).

#if (BLOB_NUM_JOBS != 1) && ((BLOB_FILL_MODE != BLOB_FILL_MODE_SYNCHRONOUS))
# define ShouldLockSections()		1
#else
# define ShouldLockSections()		0		// This can be set to 1 if we want to execute the section locks even with only one SPU.
#endif

#if !IsPlatformPS3_SPU() && (BLOB_FILL_MODE != BLOB_FILL_MODE_EMULATE_SPU)

FORCEINLINE
CSection & CThreadData::AcquireSection(int nGlobalX, int nGlobalY, int nGlobalZ, int & nOutSectionIndex) const
{
	int nArrayIndex = GetSectionArrayIndex(nGlobalX, nGlobalY, nGlobalZ);
	nOutSectionIndex = m_pnSectionIndices[nArrayIndex];
	return *m_pFirstSection[nOutSectionIndex];
}

FORCEINLINE
CSection & CThreadData::AcquireSection(int nIndex) const
{
	return *m_pFirstSection[nIndex];
}

FORCEINLINE
void CThreadData::ReleaseSection(int nIndex, CSection & section) const
{
	// Do nothing in this implementation...
}

FORCEINLINE
bool CThreadData::TryLock(int nSectionIndex, CSection & section)
{
	bool bLocked;
	if (ShouldLockSections())
	{
		bLocked = section.TryLock();
	}
	else
	{
		bLocked = true;
	}
	if (bLocked && (section.m_nInit == false))
	{
		// We do it the first time we successfully lock, that way we make sure it is initialized by any thread using the section. 
		section.Initialize();
	}
	return bLocked;

}

FORCEINLINE
void CThreadData::Unlock(int nSectionIndex, CSection & section)
{
	if (ShouldLockSections())
	{
		section.Unlock();
	}
}

void CThreadData::SetupInternalCache()
{
	// Do nothing in this implementation...
}

void CThreadData::TeardownInternalCache()
{
	// Do nothing in this implementation...
}

#else

CSection ALIGN16 g_LocalZeroSection ALIGN16_POST;

void DmaSectionFromLsToEa(int nSectionIndex, CSection * pLsSection)
{
	Assert(nSectionIndex == pLsSection->m_nSectionIndex);
	CSection * pEaSection = pLsSection->m_pEaSection;

	// Note that when we copy back from LS to EA we don't really need to copy everything, only what changed
	// Which is mostly the flags. This code MUST not copy all the memory (but only a sub-portion of the CSection).

	int nSize = pLsSection->GetSizeToDmaOut();

#if IsPlatformPS3_SPU()
	const int TAG = 13;
	BLOB_LOG(BLOB_LOG_DMAS, VjobSpuLog("DmaSectionFromLsToEa(%d) - DMA put.\n", nSectionIndex));

	cellDmaPut(pLsSection, (uint64)pEaSection, nSize, TAG, 0, 0);
	cellDmaWaitTagStatusAll(1 << TAG);		// Synchronous
#else
	// Emulate the DMA
	V_memcpy(pEaSection, pLsSection, nSize);
#endif
}

CSection * GetSectionEaPointer(CSection * * ppFirstSection, int nSectionIndex)
{
	CSection * pEaSection;
#if IsPlatformPS3_SPU()
	// Get the address
	void * addressToRead = &ppFirstSection[nSectionIndex];
	const int TAG = 14;
	pEaSection = (CSection *)cellDmaGetUint32(reinterpret_cast<uint64>(addressToRead), TAG, 0, 0);
#else
	pEaSection = ppFirstSection[nSectionIndex];
#endif
	return pEaSection;
}

void DmaSectionFromEaToLs(int nSectionIndex, CSection * pEaSection, CSection * pLsSection)
{
#if IsPlatformPS3_SPU()
	const int TAG = 15;
	// We have to DMA the pointer "&m_pSweepRenderer->m_pSections[index]" so we can read the bucket.
	// The bucket can be handled by the software cache, but not the array of bucket pointers. 
	BLOB_LOG(BLOB_LOG_DMAS, VjobSpuLog("DmaSectionFromEaToLs(%d) - DMA get - EA Address: %08X\n", nSectionIndex, (unsigned int)pEaSection));

	CheckDmaGet( pLsSection, sizeof(CSection) );
	cellDmaGet(pLsSection, (uint64)pEaSection, sizeof(CSection), TAG, 0, 0);
	cellDmaWaitTagStatusAll(1 << TAG);		// Synchronous
#else
	// Emulate the DMA
	V_memcpy(pLsSection, pEaSection, sizeof(CSection));
#endif
	Assert( nSectionIndex == pLsSection->m_nSectionIndex );
	Assert( pLsSection->m_pEaSection == pEaSection );

	BLOB_LOG(BLOB_LOG_VERBOSE, CSection::LogAndCheck(*pLsSection, pEaSection));
}

// Simple functor implementation, not the best way to put them together though.
class CSectionFunctor
{
public:
	// This code is acquiring from the free list, doing the DMA etc...
	CSection * AcquireInstance(int nSectionIndex)
	{
		Assert(nSectionIndex != 0);		// No zero section should reach here

		if (m_SectionsFreeList.HasAvailableItem() == false)
		{
			bool success;
			success = m_SectionsHashTable.FreeLeastRecentlyUsed(*this);
			Assert(success);
		}

		CSection * pLsSection = m_SectionsFreeList.Allocate();
		Assert(pLsSection != NULL);

		// When we acquire a section, we either DMA it completely
		// or we only retrieve AND store its EA address.
		// That way, when TryLock() is executed it has the proper EA to lock on it.
		CSection * pEaSection = GetSectionEaPointer(m_pFirstSection, nSectionIndex);
		if (ShouldLockSections() == false)
		{
			DmaSectionFromEaToLs(nSectionIndex, pEaSection, pLsSection);
		}
		else
		{
			// Instead of storing only the EA address during acquisition:
			// pLsSection->m_pEaSection = pEaSection;
			// We are DMAing completely the section. The reason is that in some cases, like InitializeSection()
			// we don't need locking, we need the bucket info. The Lock() will re-DMA the content anyway.
			// This comment may not be accurate anymore (but the code does not have side effects per say. A bit slower than necessary though).
			DmaSectionFromEaToLs(nSectionIndex, pEaSection, pLsSection);
		}

		return pLsSection;
	}

	// This code is releasing, doing the DMA back if needed, etc...
	void ReleaseInstance(int nSectionIndex, CSection * pLsSection)
	{
		Assert(nSectionIndex != 0);		// No zero section should reach here

		if (ShouldLockSections() == false)
		{
			// Before freeing the section, write back the section to main memory
			// (as we may have changed some of its internal state that may be important later).
			DmaSectionFromLsToEa(nSectionIndex, pLsSection);
		}

		m_SectionsFreeList.Release(pLsSection);
	}

	int GetUserData(CSection * section)
	{
		return m_SectionsFreeList.GetUserData(section);
	}

	void SetUserData(CSection * section, int nUserData)
	{
		m_SectionsFreeList.SetUserData(section, nUserData);
	}

	// In average, 7 sections are necessary (current + 6 around)
	// With 16 we should have a good amount of cache hit.
#if _DEBUG
	// In debug, use a smaller cache as we have less memory...
	typedef CFreeList<CSection, 10> CSectionFreeList;
#else
	typedef CFreeList<CSection, 16> CSectionFreeList;
#endif
	CSectionFreeList m_SectionsFreeList;
	CHashTable<CSection, 16, 4> m_SectionsHashTable;
	CSection * * m_pFirstSection;
};

CSectionFunctor g_SectionFunctor;

FORCEINLINE
CSection & CThreadData::AcquireSection(int x, int y, int z, int & nOutSectionIndex) const
{
	int nArrayIndex = GetSectionArrayIndex(x, y, z);
	nOutSectionIndex = m_pnSectionIndices[nArrayIndex];
	if (nOutSectionIndex == 0)
	{
		// We don't "acquire" g_LocalZeroSection. It is stored locally.
		// This case can happen with InitializeSection()
		return g_LocalZeroSection;
	}
	else
	{
		CSection * pSection = g_SectionFunctor.m_SectionsHashTable.AddKeyValue(nOutSectionIndex, g_SectionFunctor);
		// Make sure that both lists are in sync...
		Assert(g_SectionFunctor.m_SectionsHashTable.GetCounter() == g_SectionFunctor.m_SectionsFreeList.GetCounter());
		return *pSection;
	}
}

FORCEINLINE
CSection & CThreadData::AcquireSection(int nIndex) const
{
	if (nIndex == 0)
	{
		// We don't "acquire" g_LocalZeroBucket. It is stored locally.
		// This case can happen with InitializeSection()
		return g_LocalZeroSection;
	}
	else
	{
		CSection * pSection = g_SectionFunctor.m_SectionsHashTable.AddKeyValue(nIndex, g_SectionFunctor);
		// Make sure that both lists are in sync...
		Assert(g_SectionFunctor.m_SectionsHashTable.GetCounter() == g_SectionFunctor.m_SectionsFreeList.GetCounter());
		return *pSection;
	}
}

FORCEINLINE
void CThreadData::ReleaseSection(int nIndex, CSection & section) const
{
	if (nIndex == 0)
	{
		// Do nothing in this case, we don't release zero section.
		Assert(&section == &g_LocalZeroSection);
		// This case can happen with InitializeSection()
	}
	else
	{
		g_SectionFunctor.m_SectionsHashTable.ReleaseKeyValue(nIndex, &section, g_SectionFunctor);
		// Make sure that both lists are in sync...
		Assert(g_SectionFunctor.m_SectionsHashTable.GetCounter() == g_SectionFunctor.m_SectionsFreeList.GetCounter());
	}
}

// Cost of TryLock() on SPU:
//	If unsuccessful, there is 1 DMA
//							1 DMA for the atomic operation
//	If successful, there are 2 DMA - ~6 cache lines:
//							1 DMA for the atomic operation
//							1 DMA to load the section (~5 cache lines).
FORCEINLINE
bool CThreadData::TryLock(int nSectionIndex, CSection & section)
{
	bool bLocked;
	if (ShouldLockSections() && (nSectionIndex != 0))
	{
		bLocked = section.TryLock();						// TryLock() will need section.m_pEaSection set correctly earlier.
		if (bLocked)
		{
			// Read the section from EA to LS
			CSection * pEaSection = section.m_pEaSection;		// This has been stored when acquiring the section
			DmaSectionFromEaToLs(nSectionIndex, pEaSection, &section);
		}
	}
	else
	{
		bLocked = true;
	}
	if (bLocked && (section.m_nInit == 0))
	{
		section.Initialize();
	}
	return bLocked;
}

// Cost of Unlock() on SPU:
//	2 DMA and ~6 cache lines
//		1 DMA for the atomic operation
//		1 DMA to store the section (~5 cache lines)
FORCEINLINE
void CThreadData::Unlock(int nSectionIndex, CSection & section)
{
	if (ShouldLockSections() && (nSectionIndex != 0))
	{
		// Write the section from LS to EA before unlocking
		// Take advantage of the fact the EA is stored in section.m_pEaSection.
		DmaSectionFromLsToEa(nSectionIndex, &section);
		section.Unlock();
	}
}

void CThreadData::SetupInternalCache()
{
	// g_LocalZeroSection is setup at construction time...
	g_SectionFunctor.m_pFirstSection = m_pFirstSection;
}

void CThreadData::TeardownInternalCache()
{
	// Make sure that both lists are in sync...
	Assert(g_SectionFunctor.m_SectionsHashTable.GetCounter() == g_SectionFunctor.m_SectionsFreeList.GetCounter());
	// Remove items that stayed in the cache in case they would be used later
	g_SectionFunctor.m_SectionsHashTable.RemoveAllUnused(g_SectionFunctor);
	// Make sure that both lists are in sync...
	Assert(g_SectionFunctor.m_SectionsHashTable.GetCounter() == g_SectionFunctor.m_SectionsFreeList.GetCounter());

	// At that point, the BucketHashTable and the free list should be empty.
	// Nobody should have reference to them anymore
	Assert(g_SectionFunctor.m_SectionsFreeList.IsEmpty());
	Assert(g_SectionFunctor.m_SectionsHashTable.IsEmpty());
}

#endif

bool CThreadData::ExecuteAllSections(bool bMandatory, int nUpToNSections)
{
	if (m_bDone)
	{
		// Already finished, no more to do...
		// This could be called if we continuously call this function, while waiting for the other jobs to finish.
		// But we already pushed the fragment to draw, so we don't want to restart the calculation.
		Assert(IsPlatformPS3_SPU() == false);	// We should not reach here for SPU
		return true;
	}

	//DevMsg("ExecuteAllSections() - Slot=%d - Start.\n", m_pJobCache->m_nSlotIndexForFragmentVB);

	if (m_pJobCache->m_nCurrentFragmentIndex == -1)
	{

#if !IsPlatformPS3_SPU()
#if _DEBUG
		const int NUMBER_OF_MILLISECONDS_TIMEOUT = 500;
#else
		const int NUMBER_OF_MILLISECONDS_TIMEOUT = 32;		// After 32 milliseconds we assume that the queue is full
		// This should prevent some deadlocks
#endif
		uint32 nWaitUntil = Plat_MSTime() + NUMBER_OF_MILLISECONDS_TIMEOUT;
#else
		int nNumberOfTries = 150000;						// Timeout of around 32 ms (taking in account number of cycles and L2 cache access).
#endif

		for ( ; ; )
		{
			bool bSucceeded = m_pJobCache->GetAvailableFragment();
			if (bSucceeded == true)
			{
				break;
			}
			if (bMandatory == false)
			{
				// Was not mandatory, let's try again later if possible...
				//DevMsg("    ExecuteAllSections() - Slot=%d - More work needed.\n", m_pJobCache->m_nSlotIndexForFragmentVB);
				return false;
			}

#if IsPlatformPS3_SPU()
			--nNumberOfTries;
			if (nNumberOfTries < 0)
#else
			if (Plat_MSTime() > nWaitUntil)
#endif
			{
#if IsPlatformPS3_SPU() == 0
				const int NUMBER_OF_MILLISECONDS_PER_SPEW = 2 * 1000;
				static uint32 sFirstTime = Plat_MSTime();
				uint32 nNewTime = Plat_MSTime();
				if ( nNewTime > sFirstTime + NUMBER_OF_MILLISECONDS_PER_SPEW )
				{
					sFirstTime = nNewTime;
#else
				{
#endif
					Warning("[Blobulator] Can't get fragment, canceling job. Report this issue.\n");
				}
				int * pEaDone = &(GetEaThreadData()->m_bDone);
				ThreadInterlockedIncrement(pEaDone);
				return true;
			}

			// Otherwise do a busy loop. We could end up in deadlock here... :(
			// Should we skip the job after some time out?
			ThreadSleep(1);
		}
	}

	int nRefSectionsRendered = 0;
	BLOB_LOG(BLOB_LOG_FUNCTIONS, VjobSpuLog("CThreadData::ExecuteAllSections()\n"));

	// First we execute all the sections specific to this job (it is cache coherent and avoid potential collision with other jobs for most cases).
	// Switch between the job section stacks and the global section stack to avoid stalling (see below as well).
	// This helps the load balancing but kill  the cache a bit. We will have to figure out something better at a later point.
	ExecuteSections(m_SectionStacks[m_nCurrentJobIndex], m_GlobalSectionStack, nUpToNSections, nRefSectionsRendered);
	if (nRefSectionsRendered >= nUpToNSections)
	{
		//DevMsg("    ExecuteAllSections() - Slot=%d - More work needed.\n", m_pJobCache->m_nSlotIndexForFragmentVB);
		return false;	// More work to do. Call ExecuteAllSections() again...
	}

	// From now on, we do this for all available sections
	int nNumIterations = 1;
	for (;;)
	{
		bool bWorkRemaining;
		bWorkRemaining = ExecuteSections(m_GlobalSectionStack, m_SectionStacks[m_nCurrentJobIndex], nUpToNSections, nRefSectionsRendered);
		if (nRefSectionsRendered >= nUpToNSections)
		{
			//DevMsg("    ExecuteAllSections() - Slot=%d - More work needed.\n", m_pJobCache->m_nSlotIndexForFragmentVB);
			return false;	// More work to do. Call ExecuteAllSections() again...
		}

		for (int i = 0 ; i < m_nNumberOfJobs ; ++i)
		{
			bWorkRemaining |= ExecuteSections(m_SectionStacks[i], m_GlobalSectionStack, nUpToNSections, nRefSectionsRendered);
			if (nRefSectionsRendered >= nUpToNSections)
			{
				//DevMsg("    ExecuteAllSections() - Slot=%d - More work needed.\n", m_pJobCache->m_nSlotIndexForFragmentVB);
				return false;	// More work to do. Call ExecuteAllSections() again...
			}
		}

		++nNumIterations;
		BLOB_LOG(BLOB_LOG_ITERATIONS, VjobSpuLog("    ++nNumIterations;  nNumIterations = %d\n", nNumIterations));

		if (bWorkRemaining == false)
		{
			break;
		}
	}

	// We did all the sections and there is no work left to be done
	// (except if another job is doing something and is going to push more, but that's its responsibility).
	// Push the last fragments.
	m_pJobCache->PushCurrentFragmentToDraw();

	int * pEaDone = &(GetEaThreadData()->m_bDone);
	ThreadInterlockedIncrement(pEaDone);

	//DevMsg("ExecuteAllSections() - Slot=%d - End.\n", m_pJobCache->m_nSlotIndexForFragmentVB);

	return true;
}

FORCEINLINE
void CThreadData::SeedFromFlagsYZ(int x, int y, int z, CSection::Grid2D & flags, CSection & destinationSection)
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
void CThreadData::SeedFromFlagsXZ(int x, int y, int z, CSection::Grid2D & flags, CSection & destinationSection)
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
void CThreadData::SeedFromFlagsXY(int x, int y, int z, CSection::Grid2D & flags, CSection & destinationSection)
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

// TODO: Change this code so there is less copy and paste :(
// Make the crawling here more accurate:
//	- bWorkRemaining should be set if there is really some work to be done (i.e. a new section pushed or some flags that could not be reset due to locks).
//  - Make xmSection and XpSection use the mask.
//  - Don't seed if the corresponding cubes have already been done.
bool CThreadData::CrawlOneSection(int nSectionIndex, CSection & section, SectionStack & destinationSectionStack)
{
	BLOB_LOG(BLOB_LOG_FUNCTIONS, VjobSpuLog("CrawlOneSection(%d, %d, %d) - Start\n", section.m_nX, section.m_nY, section.m_nZ));
	BLOB_LOG(BLOB_LOG_VERBOSE, CSection::LogAndCheck(section, NULL));

	// It seems we are spending quite some time pushing seeds with verifying that they have been done first.
	// We may be able to save some time by testing done first instead of pushing / pulling and testing.
	// The issue in this case is that we would test the seed twice. Find a fast way to test the seed only once, and before we push.

	bool bWorkRemaining = false;
	bool bPushAgain = false;
	if (section.m_SectionXM.m_nFlags64 != 0)
	{
		bWorkRemaining = true;

		int x = section.m_nX - 1;
		int y = section.m_nY;
		int z = section.m_nZ;
		if (x >= 0)		// Note: This check may be needed due to incorrect reset in some case with multi-threading
			// It would be better to fix the core issue though
		{
			int nOutSectionIndex;
			CSection & xmSection = AcquireSection(x, y, z, nOutSectionIndex); // If NULL, it means there is no particle associated (like a border), no need to push a seed
			if (nOutSectionIndex != 0)
			{
				// For X, this mask directly matches with the last internalDoneState of the other section
				// Let's see if we can avoid some of them (this masking is really cheap and reduce some seeds).
				//				uint64 otherMask = xmSection->internalDoneState[7];
				//				mask &= otherMask;		// Remove any bit that was already done, this saves a bit of reparsing
				// Somehow this creates more holes in X, let's remove it for the moment...
				if (TryLock(nOutSectionIndex, xmSection))	// We are locking after acquiring the section,
					// i.e. we potentially pay the DMA cost even if we can't lock it
				{
					SeedFromFlagsYZ(x, y, z, section.m_SectionXM, xmSection);
					if (xmSection.m_nOnAStack == false)
					{
						// We are seeding it, and it is not already on a stack, let's push it
						xmSection.m_nOnAStack = true;
						destinationSectionStack.PushToHead(nOutSectionIndex);
					}
					Unlock(nOutSectionIndex, xmSection);
					section.m_SectionXM.m_nFlags64 = 0;	// Reset the flags for next time
				}
				else
				{
					bPushAgain = true;				// the neighbor section is locked, we have to keep trying...
				}
			}
			else
			{
				section.m_SectionXM.m_nFlags64 = 0;	// Reset the flags for next time
			}
			ReleaseSection(nOutSectionIndex, xmSection);
		}
		else
		{
			section.m_SectionXM.m_nFlags64 = 0;	// Reset the flags for next time
		}
	}
	if (section.m_SectionXP.m_nFlags64 != 0)
	{
		bWorkRemaining = true;

		int x = section.m_nX + SECTION_X_SIZE;
		int y = section.m_nY;
		int z = section.m_nZ;
		if (x < X_SIZE)
		{
			int nOutSectionIndex;
			CSection & xpSection = AcquireSection(x, y, z, nOutSectionIndex); // If NULL, it means there is no particle associated (like a border), no need to push a seed
			if (nOutSectionIndex != 0)
			{
				// For X, this mask directly matches with the last internalDoneState of the other section
				// Let's see if we can avoid some of them
				//					uint64 otherMask = xpSection->internalDoneState[0];
				//					mask &= ~otherMask;		// Remove any bit that was already done, this saves a bit of reparsing
				// Somehow this creates more holes in X, let's remove it for the moment...
				if (TryLock(nOutSectionIndex, xpSection))
				{
					SeedFromFlagsYZ(x, y, z, section.m_SectionXP, xpSection);
					if (xpSection.m_nOnAStack == false)
					{
						xpSection.m_nOnAStack = true;
						destinationSectionStack.PushToHead(nOutSectionIndex);
					}
					Unlock(nOutSectionIndex, xpSection);
					section.m_SectionXP.m_nFlags64 = 0;	// Reset the flags for next time
				}
				else
				{
					bPushAgain = true;
				}
			}
			else
			{
				section.m_SectionXP.m_nFlags64 = 0;	// Reset the flags for next time
			}
			ReleaseSection(nOutSectionIndex, xpSection);
		}
		else
		{
			section.m_SectionXP.m_nFlags64 = 0;	// Reset the flags for next time
		}
	}

	// We could do similar masking optimizations for Y and Z, but that would be much more costly.
	// Consider doing doing the masking test as well for Y and Z, but the saving may not be good enough.

	if (section.m_SectionYM.m_nFlags64 != 0)
	{
		bWorkRemaining = true;

		int x = section.m_nX;
		int y = section.m_nY - 1;
		int z = section.m_nZ;
		if (y >= 0)
		{
			int nOutSectionIndex;
			CSection & ymSection = AcquireSection(x, y, z, nOutSectionIndex);
			if (nOutSectionIndex != 0)
			{
				if (TryLock(nOutSectionIndex, ymSection))
				{
					SeedFromFlagsXZ(x, y, z, section.m_SectionYM, ymSection);
					if (ymSection.m_nOnAStack == false)
					{
						ymSection.m_nOnAStack = true;
						destinationSectionStack.PushToHead(nOutSectionIndex);
					}
					Unlock(nOutSectionIndex, ymSection);
					section.m_SectionYM.m_nFlags64 = 0;	// Reset the flags for next time
				}
				else
				{
					bPushAgain = true;
				}
			}
			else
			{
				section.m_SectionYM.m_nFlags64 = 0;	// Reset the flags for next time
			}
			ReleaseSection(nOutSectionIndex, ymSection);
		}
		else
		{
			section.m_SectionYM.m_nFlags64 = 0;	// Reset the flags for next time
		}
	}

	if (section.m_SectionYP.m_nFlags64 != 0)
	{
		bWorkRemaining = true;

		int x = section.m_nX;
		int y = section.m_nY + SECTION_Y_SIZE;
		int z = section.m_nZ;
		if (y < Y_SIZE)
		{
			int nOutSectionIndex;
			CSection & ypSection = AcquireSection(x, y, z, nOutSectionIndex);
			if (nOutSectionIndex != 0)
			{
				if (TryLock(nOutSectionIndex, ypSection))
				{
					SeedFromFlagsXZ(x, y, z, section.m_SectionYP, ypSection);
					if (ypSection.m_nOnAStack == false)
					{
						ypSection.m_nOnAStack = true;
						destinationSectionStack.PushToHead(nOutSectionIndex);
					}
					Unlock(nOutSectionIndex, ypSection);
					section.m_SectionYP.m_nFlags64 = 0;	// Reset the flags for next time
				}
				else
				{
					bPushAgain = true;
				}
			}
			else
			{
				section.m_SectionYP.m_nFlags64 = 0;	// Reset the flags for next time
			}
			ReleaseSection(nOutSectionIndex, ypSection);
		}
		else
		{
			section.m_SectionYP.m_nFlags64 = 0;	// Reset the flags for next time
		}
	}

	if (section.m_SectionZM.m_nFlags64 != 0)
	{
		bWorkRemaining = true;

		int x = section.m_nX;
		int y = section.m_nY;
		int z = section.m_nZ - 1;
		if (z >= 0)
		{
			int nOutSectionIndex;
			CSection & zmSection = AcquireSection(x, y, z, nOutSectionIndex);
			if (nOutSectionIndex != 0)
			{
				if (TryLock(nOutSectionIndex, zmSection))
				{
					SeedFromFlagsXY(x, y, z, section.m_SectionZM, zmSection);
					if (zmSection.m_nOnAStack == false)
					{
						zmSection.m_nOnAStack = true;
						destinationSectionStack.PushToHead(nOutSectionIndex);
					}
					Unlock(nOutSectionIndex, zmSection);
					section.m_SectionZM.m_nFlags64 = 0;	// Reset the flags for next time
				}
				else
				{
					bPushAgain = true;
				}
			}
			else
			{
				section.m_SectionZM.m_nFlags64 = 0;	// Reset the flags for next time
			}
			ReleaseSection(nOutSectionIndex, zmSection);
		}
		else
		{
			section.m_SectionZM.m_nFlags64 = 0;	// Reset the flags for next time
		}
	}

	if (section.m_SectionZP.m_nFlags64 != 0)
	{
		bWorkRemaining = true;

		int x = section.m_nX;
		int y = section.m_nY;
		int z = section.m_nZ + SECTION_Y_SIZE;
		if (z < Z_SIZE)
		{
			int nOutSectionIndex;
			CSection & zpSection = AcquireSection(x, y, z, nOutSectionIndex);
			if (nOutSectionIndex != 0)
			{
				if (TryLock(nOutSectionIndex, zpSection))
				{
					SeedFromFlagsXY(x, y, z, section.m_SectionZP, zpSection);
					if (zpSection.m_nOnAStack == false)
					{
						zpSection.m_nOnAStack = true;
						destinationSectionStack.PushToHead(nOutSectionIndex);
					}
					Unlock(nOutSectionIndex, zpSection);
					section.m_SectionZP.m_nFlags64 = 0;	// Reset the flags for next time
				}
				else
				{
					bPushAgain = true;
				}
			}
			else
			{
				section.m_SectionZP.m_nFlags64 = 0;	// Reset the flags for next time
			}
			ReleaseSection(nOutSectionIndex, zpSection);
		}
		else
		{
			section.m_SectionZP.m_nFlags64 = 0;	// Reset the flags for next time
		}
	}

	if (bPushAgain)
	{
		section.m_nOnAStack = true;
		destinationSectionStack.PushToHead(nSectionIndex);
	}

	BLOB_LOG(BLOB_LOG_FUNCTIONS, VjobSpuLog("CrawlOneSection(%d, %d, %d) - End\n", section.m_nX, section.m_nY, section.m_nZ));
	BLOB_LOG(BLOB_LOG_VERBOSE, CSection::LogAndCheck(section, NULL));
	return bWorkRemaining;
}

bool CThreadData::ExecuteSections(SectionStack & sourceSectionStack, SectionStack & destinationSectionStack, int nUpToNSections, int & nRefNumberOfSectionsExecuted)
{
	BLOB_LOG(BLOB_LOG_FUNCTIONS, VjobSpuLog("ExecuteSections() - Start\n"));
	bool bWorkRemaining = false;
	for (;;)
	{
		if (nRefNumberOfSectionsExecuted >= nUpToNSections)
		{
			// We had to finish early, indicate there is more work (even if the stack may be empty - this is of no consequcne anyway).
			return true;
		}
		int nSectionIndex = sourceSectionStack.PopFromHead();
		if (nSectionIndex < 0)
		{
			// No more section to execute
			break;
		}

		CSection & section = AcquireSection(nSectionIndex);
		// Because it is on this stack, the flag m_nOnAStack has been set.
		// We can't test it yet as on SPU the section has not been DMAed yet.

		if (TryLock(nSectionIndex, section) == false)
		{
			// It is already locked (being seeded by another job?)
			// We have to push it back (otherwise we would lose the section as a unit of job).
			// Use destination so we are not pushing on the same stack, then the next iteration pulling the same section.
			destinationSectionStack.PushToHead(nSectionIndex);
			bWorkRemaining = true;
		}
		else
		{
			// We have the lock, the memory is accessible
			Assert(section.m_nOnAStack);
			section.m_nOnAStack = false;

			// Execute the section, which means that there is no more work to do on that section
			// No need to set the flag m_nOnAStack back to true, it will be set if this section is re-seeded.
			if (section.m_SeedList.GetSize() != 0)
			{
				m_pJobCache->RenderSection(section);
				bWorkRemaining = true;						// Potential seeding so more work remaining
			}
			// To avoid double push / pop / lock / unlock, crawl directly here... It's already locked anyway...
			bWorkRemaining |= CrawlOneSection(nSectionIndex, section, destinationSectionStack);
			Unlock(nSectionIndex, section);

			++nRefNumberOfSectionsExecuted;
		}
		ReleaseSection(nSectionIndex, section);
	}
	BLOB_LOG(BLOB_LOG_FUNCTIONS, VjobSpuLog("ExecuteSections() - End\n"));
	return bWorkRemaining;
}

}
}
