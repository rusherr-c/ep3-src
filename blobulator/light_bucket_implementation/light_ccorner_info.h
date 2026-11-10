//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CCORNERINFO_H
#define BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CCORNERINFO_H

#include "blobulator/vertexbuffers/vertexinfo.h"
#include "blobulator/light_bucket_implementation/light_csoftware_cache.h"

namespace Blobulator
{
	namespace LightBucketImplementation
	{
		static const int EMPTY_CORNER = -1;
		static const int ZERO_CORNER = 0;

		// This structure takes 32 bytes.
		// A corner group (8 CornerInfo) takes 256 bytes - 2 cache lines on X360 and PS3.
		class ALIGN16 CCornerInfo
		{
		public:
			float normal[3];
			union
			{
				float value;
				int32 valueAsInt;
			};
			union
			{
				vbId_t edges[3];
				int32 edgesAsInt[3];
				float edgesAsFloat[3];
			};

			union
			{
				struct
				{
					int16 m_nSectionIndex;
					int16 m_nBucketIndex;			// Index within the section. Do we need that or can we replace it with XYZ in 5:5:5 bits?
				} Corner0;							// Used for the corner 0 (out of the group of 8)
				struct 
				{
					int8 m_nX, m_nY, m_nZ, m_Pad;
				} Corner1;							// Used for the corner 1 (out of the group of 8).
			};
		} ALIGN16_POST;
	}
}

#endif