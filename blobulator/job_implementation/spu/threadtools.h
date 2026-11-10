//========= Copyright (c), Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_SPU_THREADTOOLS_H
#define BLOBULATOR_SPU_THREADTOOLS_H

#include "ps3/spu_job_shared.h"

typedef uint64 ThreadHandle_t;

class CThreadFastMutex
{
public:
	void Lock() volatile
	{
		// Do nothing for the moment...
	}
	void Unlock() volatile
	{
		// Do nothing for the moment...
	}
};

inline
void ThreadPause()
{
	// Just run some nops, no need to saturate the DMA for no reasons
	// Should we use more nops then?
	for (int i = 0 ; i < 64 ; ++i)
	{
		si_nop();
	}
}

inline
void ThreadSleep()
{
	ThreadPause();
}

inline
int32 ThreadInterlockedIncrement(int32 volatile * pEa)
{
	uint32_t buf[32] __attribute__((aligned(128)));
	return cellAtomicIncr32(buf, (uint64)pEa);
}

inline
bool ThreadInterlockedAssignIf( int32 volatile *pEa, int32 value, int32 comperand )
{
	Assert( (((int)pEa) & 3) == 0 );

	uint32_t buf[32] __attribute__((aligned(128)));
	return (cellAtomicCompareAndSwap32(buf, (uint64)pEa, comperand, value) == (uint32)comperand);
}

inline
void ThreadMemoryBarrier()
{
	// Do nothing...
}

#endif

