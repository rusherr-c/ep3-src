//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_JOBIMPLEMENTATION_CCORNERINFO_H
#define BLOBULATOR_JOBIMPLEMENTATION_CCORNERINFO_H

#include "blobulator/vertexbuffers/vertexinfo.h"
#include "blobulator/job_implementation/csoftware_cache.h"

namespace Blobulator
{
	namespace JobImplementation
	{
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
			CBucket * m_pBucket;					// On SPU, this points to the cached bucket in the local storage.
													// If the bucket is removed from the cache, we will have to clear m_pBucket as well.
													// Otherwise the corner will be considered allocated to an dangling bucket (and probably will point to an unrelated valid bucket),
													// and when we clear their relationship between bucket and corner, we will write at the wrong place and bad things will happen. 
		} ALIGN16_POST;
	}
}

#endif