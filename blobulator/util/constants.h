//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//
#pragma once

#ifdef SPU
#include "ps3/spu_job_shared.h"
#endif

#define PIx2 (6.28318530718f)
#define PI (3.14159265359f)
#define PId2 (1.5707963267948966192313216916398f)
#define OneOver2PI (0.15915494309189533576888376337251f)
#define SQRT3_OVER_2 (0.86602540378443864676372317075294f)
#define SQRT2 (1.4142135623730950488016887242097f)

namespace Blobulator
{
	struct Constants
	{
		// Somehow there is some linker issues extern-ing global fixed size const arrays.
		// Moved them to static in a struct to resolve the issue.
		static const int32 ALIGN16 s_SIMD_000Mask[4] ALIGN16_POST;
		static const int32 ALIGN16 s_SIMD_001Mask[4] ALIGN16_POST;
		static const int32 ALIGN16 s_SIMD_010Mask[4] ALIGN16_POST;
		static const int32 ALIGN16 s_SIMD_011Mask[4] ALIGN16_POST;
		static const int32 ALIGN16 s_SIMD_100Mask[4] ALIGN16_POST;
		static const int32 ALIGN16 s_SIMD_101Mask[4] ALIGN16_POST;
		static const int32 ALIGN16 s_SIMD_110Mask[4] ALIGN16_POST;
		static const int32 ALIGN16 s_SIMD_111Mask[4] ALIGN16_POST;

		static float s_flByteToFloat[256];

		static uint8 s_FlagShifts[8];
		static uint32 s_FlagsLists[256];

		// Call this function first if you need to use some of the constants above.
		static void InitializeConstants();
	};
}




