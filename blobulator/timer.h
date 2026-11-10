#pragma once

//TODO: Move this file into Util

#include "implicit/impdefines.h"

//TODO: These functions should all actually be in platform.cpp/platform.h
//      However, I don't use those functions because they're a bit too high level
#if(RENDER_API == VALVE_API)
	#include "tier0/platform.h"
	#if defined(_WIN32) && !defined(_X360)
		#define WINDOWS_LEAN_AND_MEAN
		#define _WIN32_WINNT 0x0403
		#include <windows.h>
	#endif
#else
	#include <windows.h>
#endif

FORCEINLINE int64 get_time()
{
#ifdef _WIN32
	LARGE_INTEGER time;
	QueryPerformanceCounter(&time);
	return time.QuadPart;
#else
	return Plat_MSTime();
#endif
}

FORCEINLINE int64 get_freq()
{
#ifdef _WIN32
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	return freq.QuadPart;
#else
	return 1000;
#endif
}