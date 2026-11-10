//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_SECTIONIMPLEMENTATION_CCORNERINFO_H
#define BLOBULATOR_SECTIONIMPLEMENTATION_CCORNERINFO_H

#include "blobulator/vertexbuffers/vertexbuffer.h"

namespace Blobulator
{
	namespace SectionImplementation
	{
		// This structure takes 32 bytes.
		// A corner group (8 CornerInfo) takes 256 bytes - 2 cache lines on X360 and PS3.
		class CornerInfo
		{
		public:
			float normal[3];
			float value;
			vbId_t edges[3];
			CBucket * volatile bucket;
		};
	}
}

#endif