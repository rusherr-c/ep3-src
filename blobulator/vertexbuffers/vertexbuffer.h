//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

// NOTE: Wherever this file is included, ImpDefines.h should be included before this file is included.

#if (DRAW_MODE==DRAW_STRIPS)
	#include "tristripvertexbuffer.h"
#elif (DRAW_MODE==DRAW_INDEX_STRIPS)
	#include "indextristripvertexbuffer.h"
#elif (DRAW_MODE==DRAW_INDEX_TRIS)
	#include "indextrivertexbuffer.h"
#elif (DRAW_MODE==DRAW_SORTED_INDEX_TRIS)
	#include "sortedindextrivertexbuffer.h"
#elif (DRAW_MODE==DRAW_MT_SORTED_INDEX_TRIS)
	#include "mtsortedindextrivertexbuffer.h"
#elif (DRAW_MODE==DRAW_MT_INDEX_TRIS)
	#include "mtindextrivertexbuffer.h"
#endif

#if (DRAW_MODE==DRAW_STRIPS)
	#define VERTEX_BUFFER_CLASS TriStripVertexBuffer
#elif (DRAW_MODE==DRAW_INDEX_STRIPS)
	#define VERTEX_BUFFER_CLASS IndexTriStripVertexBuffer
#elif (DRAW_MODE==DRAW_INDEX_TRIS)
	#define VERTEX_BUFFER_CLASS IndexTriVertexBuffer
#elif (DRAW_MODE==DRAW_SORTED_INDEX_TRIS)
	#define VERTEX_BUFFER_CLASS SortedIndexTriVertexBuffer
#elif (DRAW_MODE==DRAW_MT_SORTED_INDEX_TRIS)
	#define VERTEX_BUFFER_CLASS MTSortedIndexTriVertexBuffer
#elif (DRAW_MODE==DRAW_MT_INDEX_TRIS)
	#define VERTEX_BUFFER_CLASS MTIndexTriVertexBuffer
#endif

#if (DRAW_MODE==DRAW_STRIPS || DRAW_MODE==DRAW_INDEX_STRIPS)
	#define DRAWING_TRIS 0
	#define DRAWING_STRIPS 1
#else
	#define DRAWING_TRIS 1
	#define DRAWING_STRIPS 0
#endif