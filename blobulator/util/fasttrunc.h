//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//
#pragma once

#include <float.h>

#ifdef IS_WINDOWS_PC

// Truncating a float by casting to an int i = (int)f
// is extremely slow because it calls a function that does the work
// Use this instead.
// Truncation mode must be enabled first by a call to turnOnTruncMode.
// Node that this changes the default mode and can possibly cause problems.
inline int round(register float a)
{
	int i;
	__asm {
		fld   a
		fistp i
	}
	return i;
}

// This is much slower, but at least it's safe
// The version above might alter other calculations
// because the truncation mode has to be different.
/*
inline int trunc(float a)
{
	return (int)a;
}
*/
/*
inline int round(float a)
{
	return (int)a;
}
*/

inline int Trunc2(double a) {
	short saved_mode,tmp;
	int x;
	__asm {
		fstcw	saved_mode
		mov		ax,saved_mode
		// 0x0C00=trunc; 0x0800=round up, 0x0400=round down, 0x0000=nearest even
		or		ax, 0x0C00
		mov		tmp,ax
		fldcw	tmp
		fld		a
		fistp   x
		fldcw	saved_mode
	}
      return x;
}

inline int Trunc3( float x ) {
x = (x < 0.0) ? x+0.5f-FLT_EPSILON : x-0.5f+FLT_EPSILON;
int i;
_asm {
fld x
fistp i
}
return i;
}

//fsub        dword ptr [__real@4@3ffdfffffc0000000000 (0042007c)]

// Rounding down (no need to change rounding mode)
// TODO: Shouldn't we be subtracting FLT_EPSILON?????
#define round_down trunc

inline int trunc( float x ) {
x += -0.5f+FLT_EPSILON;
int i;
_asm {
fld x
fistp i
}
return i;
}

// Rounding up (no need to change rounding mode)
inline int round_up( float x ) {
x += 0.5f+FLT_EPSILON;
int i;
_asm {
fld x
fistp i
}
return i;
}

#elif !defined( _PS3 )

// X360TBD: 360 asm implementations?

// TODO (ilya): Verify that these work. Are there faster versions?
inline int round(float a)
{
	return (int)(a + 0.5f);
}
inline int Trunc2(double a) 
{
	return (int)a;
}
inline int Trunc3( float x ) 
{
	return (int)x;
}
#define round_down trunc
inline int trunc( float x ) 
{
	return (int)x;
}
inline int round_up( float x ) 
{
	return (int)(x + 1.0f);
}

#elif defined( _PS3 )

#define round_down trunc
inline int round_up( float x ) 
{
	return (int)(x + 1.0f);
}

#endif

// Note: Calling this and not turning it off may cause problems
// because the default rounding mode is to round
void turnOnTruncMode();

void restoreOldRoundMode();
