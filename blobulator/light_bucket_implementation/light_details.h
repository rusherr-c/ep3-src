//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_COMMON_H
#define BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_COMMON_H

#include "blobulator/implicit/impdefines.h"
#include "blobulator/util/constants.h"

#ifdef SPU
#include "ps3/spu_job_shared.h"
#else
// Disable LS overflow checks when not SPU
# define CheckBufferOverflow()
# define CheckDmaGet(p, size)
#endif

namespace Blobulator
{
	namespace LightBucketImplementation
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

		// We support up to 4 matrices for exclusion.
		static const int MAX_NUMBER_OF_EXCLUSION_MATRICES = 4;

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
		static const int BUCKETS_IN_X = (PARTICLE_CACHE_XSIZE / BUCKET_FACTOR) + 1;
		static const int BUCKETS_IN_Y = (PARTICLE_CACHE_YSIZE / BUCKET_FACTOR) + 1;
		static const int BUCKETS_IN_Z = (PARTICLE_CACHE_ZSIZE / BUCKET_FACTOR) + 1;

		enum
		{
			X_FLAG = (1 << 2),
			Y_FLAG = (1 << 1),
			Z_FLAG = (1 << 0),
		};

		FORCEINLINE
		static int GetCornerIndexFromXYZ(int x, int y, int z)
		{
			// & 1 is valid only because BUCKET_FACTOR is 2.
			int offset = (x & 1) * X_FLAG;
			offset |= (y & 1) * Y_FLAG;
			offset |= (z & 1) * Z_FLAG;
			return (offset);
		}

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
			return (x / BUCKET_FACTOR) * (BUCKETS_IN_Y * BUCKETS_IN_Z);
		}

		FORCEINLINE
		static int GetIndexFromY(int y)
		{
			return (y / BUCKET_FACTOR) * (BUCKETS_IN_Z);
		}

		FORCEINLINE
		static int GetIndexFromZ(int z)
		{
			return (z / BUCKET_FACTOR);
		}

		FORCEINLINE
		static int GetIndexFromXYZ(int x, int y, int z)
		{
			int index = (x / BUCKET_FACTOR) * (BUCKETS_IN_Y * BUCKETS_IN_Z);
			index += (y / BUCKET_FACTOR) * (BUCKETS_IN_Z);
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


