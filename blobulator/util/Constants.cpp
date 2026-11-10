//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "blobulator/util/constants.h"

namespace Blobulator
{
	// This is not the best representation for SPU, but it should still work
	// We will have to revisit this at some point to have a move vector friendly version.
	const int32 ALIGN16 Constants::s_SIMD_000Mask[4] = { 0, 0, 0, 0 };
	const int32 ALIGN16 Constants::s_SIMD_001Mask[4] = { 0, 0, ~0, 0 };
	const int32 ALIGN16 Constants::s_SIMD_010Mask[4] = { 0, ~0, 0, 0 };
	const int32 ALIGN16 Constants::s_SIMD_011Mask[4] = { 0, ~0, ~0, 0 };
	const int32 ALIGN16 Constants::s_SIMD_100Mask[4] = { ~0, 0, 0, 0 };
	const int32 ALIGN16 Constants::s_SIMD_101Mask[4] = { ~0, 0, ~0, 0 };
	const int32 ALIGN16 Constants::s_SIMD_110Mask[4] = { ~0, ~0, 0, 0 };
	const int32 ALIGN16 Constants::s_SIMD_111Mask[4] = { ~0, ~0, ~0, 0 };

	// On SPU, we should not need that.
	float Constants::s_flByteToFloat[256];

	uint8 Constants::s_FlagShifts[8] =	{	1 << 0,	1 << 1,	1 << 2,	1 << 3,	1 << 4,	1 << 5,	1 << 6,	1 << 7	};

	// This table is built at runtime. For a given byte, it indicates how many bits are set, and gives the list in a packed manner.
	// First 4 bits indicate the number of bits. Then each successive 3 bits indicate the offset (0 to 7).
	// Worst case: 4 + (7 * 3) = 25 bits.
	uint32 Constants::s_FlagsLists[256];

	void Constants::InitializeConstants()
	{
		static bool bFirstTime = true;
		if (bFirstTime == false)
		{
			return;
		}
		for ( int i = 0; i < 256; i++ )
		{
			s_flByteToFloat[i] = float(i);
		}

		// Initializes the flag list table
		for (int i = 0 ; i < 256 ; ++i)
		{
			// Do a slow look-up, it is only executed once

			int nFlag = 0;
			int nStoredIndex = 4;		// Initial number is coded on 4 bits (0 to 8)
			int nNumberOfBits = 0;

			int nValue = i;
			int nIndex = 0;
			while (nValue != 0)
			{
				if (nValue & 1)
				{
					nNumberOfBits++;
					nFlag |= nIndex << nStoredIndex;
					nStoredIndex += 3;	// Coded on 3 bits
				}
				nValue >>= 1;
				++nIndex;
			}

			s_FlagsLists[i] = nFlag | nNumberOfBits;
		}

		bFirstTime = false;
	}

}

