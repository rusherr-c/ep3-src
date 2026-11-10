//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

// NOTE: Wherever this file is included, ImpDefines.h should be included before this file is included.

#if (DRAW_MODE==DRAW_STRIPS)
	#include "tristripvertexbuffer_inl.h"
#elif (DRAW_MODE==DRAW_INDEX_STRIPS)
	#include "indextristripvertexbuffer_inl.h"
#elif (DRAW_MODE==DRAW_INDEX_TRIS)
	#include "indextrivertexbuffer_inl.h"
#elif (DRAW_MODE==DRAW_SORTED_INDEX_TRIS)
	#include "sortedindextrivertexbuffer_inl.h"
#elif (DRAW_MODE==DRAW_MT_SORTED_INDEX_TRIS)
	#include "mtsortedindextrivertexbuffer_inl.h"
#elif (DRAW_MODE==DRAW_MT_INDEX_TRIS)
	#include "mtindextrivertexbuffer_inl.h"
#endif