//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#define _WIN32_WINNT 0x0403
#include <windows.h> // For synchronization primitives
#include "../smartarray.h"

#define SPINLOCK_T __declspec(align(4)) volatile LONG

__forceinline void BlrGetLock(volatile LONG* lock)
{
	if (InterlockedCompareExchange(lock, 1, 0) != 0)
	//if (InterlockedExchange(lock, 1) != 0)
	{
		while(true)
		{
			_asm pause;  // pause instruction in spin-loop
			if (*lock == 0) // spin on dirty read, not on lock
			{
				if (InterlockedCompareExchange(lock, 1, 0) == 0)
				//if (InterlockedExchange(lock, 1) == 0)
				{
					break;
				}
			}
		}
		// can suspend thread here if I don't use a "while true"
	}
}

__forceinline void BlrReleaseLock(volatile LONG* lock)
{
	InterlockedExchange(lock, 0);
	//*lock = 0;
}

#define FORCE_READ(a) (*((volatile int*)(&(a))))

extern SPINLOCK_T taskListLock;