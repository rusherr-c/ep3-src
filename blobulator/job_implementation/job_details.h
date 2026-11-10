//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_JOBIMPLEMENTATION_COMMON_H
#define BLOBULATOR_JOBIMPLEMENTATION_COMMON_H

#include "blobulator/implicit/impdefines.h"
#include "blobulator/util/constants.h"

#ifdef SPU
#include "ps3/spu_job_shared.h"
#endif

namespace Blobulator
{
	namespace JobImplementation
	{
		static const int X_SIZE = RENDERER_CACHE_XSIZE;
		static const int Y_SIZE = RENDERER_CACHE_YSIZE;
		static const int Z_SIZE = RENDERER_CACHE_ZSIZE;
		static const int X_SIZE_P1 = X_SIZE + 1;
		static const int X_SIZE_P2 = X_SIZE + 2;
		static const int Y_SIZE_P1 = Y_SIZE + 1;
		static const int Z_SIZE_P1 = Z_SIZE + 1;

		// 256 is the max number of particles that can be supported in this implementation
		// (using  8 bits to store the particle index).
		static const int MAX_NUMBER_OF_PARTICLES = 256;

		// Number of fragments for the vertex buffer.
		static const int NUMBER_OF_FRAGMENTS = 24;			// We need more available fragments the more jobs we have
															// As to avoid any contention.

		// XYZ index has to fit in one integer, but this will be decomposed between the section index and the internal index as needed. 
		typedef uint32 XYZ;

		FORCEINLINE
		int XYZ_GetX(XYZ temp)
		{
			return temp & 0xff;
		}

		FORCEINLINE
		int XYZ_GetY(XYZ temp)
		{
			return (temp >> 8) & 0xff;
		}

		FORCEINLINE
		int XYZ_GetZ(XYZ temp)
		{
			return (temp >> 16) & 0xff;
		}

		FORCEINLINE
		XYZ MakeXYZ(int x, int y, int z)
		{
			return (x | (y << 8) | (z << 16));
		}

		static const int BUCKET_FACTOR = 2;
		static const int BUCKET_FACTOR_MASK = -BUCKET_FACTOR;
		static const int BUCKET_X = (PARTICLE_CACHE_XSIZE / BUCKET_FACTOR) + 1;
		static const int BUCKET_Y = (PARTICLE_CACHE_YSIZE / BUCKET_FACTOR) + 1;
		static const int BUCKET_Z = (PARTICLE_CACHE_ZSIZE / BUCKET_FACTOR) + 1;

		// From http://www.hackersdelight.org/basics.pdf
		FORCEINLINE
		int FastAbs(int x)
		{
			int y = x >> 31;
			return (x ^ y) - y;
		}

		// Although these functions are fast, you may want to avoid calling them unnecessarily
		// Adding constants is faster than multiplying a variable by a constant and adding it.

		FORCEINLINE
		static int GetIndexFromX(int x)
		{
			return (x / BUCKET_FACTOR) * (BUCKET_Y * BUCKET_Z);
		}

		FORCEINLINE
		static int GetIndexFromY(int y)
		{
			return (y / BUCKET_FACTOR) * (BUCKET_Z);
		}

		FORCEINLINE
		static int GetIndexFromZ(int z)
		{
			return (z / BUCKET_FACTOR);
		}

		FORCEINLINE
		static int GetIndexFromXYZ(int x, int y, int z)
		{
			int index = (x / BUCKET_FACTOR) * (BUCKET_Y * BUCKET_Z);
			index += (y / BUCKET_FACTOR) * (BUCKET_Z);
			index += (z / BUCKET_FACTOR);
			return index;
		}

		FORCEINLINE
		uint8 DynamicShift(int index)
		{
			if (IsPlatformX360() || IsPlatformPS3_PPU())
			{
				// We use a table to avoid micro-coded instructions (more latency, stalls the pipeline and other HW thread).
				return (Constants::s_FlagShifts[index]);
			}
			else
			{
				return (1 << index);
			}
		}
	}
}

#endif


