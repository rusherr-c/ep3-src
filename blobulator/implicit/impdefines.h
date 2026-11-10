//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once


#define IMP_PROFILE 0


// What API we're using for drawing (VALVE/OpenGL)
#define OPENGL_API 0
#define VALVE_API 1
#define RENDER_API VALVE_API

// How we're drawing (tri strips, indexed tri strips, indexed tris, sorted+indexed tris)
// In terms of perf, tri strips are the slowest.
// indexed tri strips and indexed tris are about the same.
// sorted+indexed tris are slightly slower but guarantee correct sorting for transparency
//    (even thought transparency is usually correct with indexed tris and tri strips).
// mt sorted+indexed tris is a vertex buffer that is safe for multiple threads working on multiple slices
#define DRAW_STRIPS 0
#define DRAW_INDEX_STRIPS 1
#define DRAW_INDEX_TRIS 2
#define DRAW_SORTED_INDEX_TRIS 3
#define DRAW_MT_SORTED_INDEX_TRIS 4
#define DRAW_MT_INDEX_TRIS 5
#define DRAW_MODE DRAW_INDEX_TRIS

// TODO: I might want to make setting cache and render mode
// settings more foolproof. It would be nice if setting the
// renderer would automatically determine which caches and seed
// settings you could use, and would check that you did it correctly.
#define PROJECTING_PARTICLE_CACHE 0
#define SIDE_PROJECTING_PARTICLE_CACHE 1  // Remember to enable SEED_SIDEWAYS or SEED_MULTISLICE 
#define SLICE_2D_PARTICLE_CACHE 2 // NOTE: This was never finished. It was supposed to be a cache that kept a list of 2D projections for each slice, and deallocated slices as necessary.
#define MC_PARTICLE_CACHE 3
#define HASH_PARTICLE_CACHE 4
#define INF_HASH_PARTICLE_CACHE 5
#define VOLUMETRIC_PARTICLE_CACHE 6 // NOTE: This was never finished. It was supposed to be a cache that doesn't trace the surface but goes from bottom to top, efficiently finding transitions in sign.
#define CACHE_MODE PROJECTING_PARTICLE_CACHE

// NOTE: Either SEED_SIDEWAYS or SEED_MULTISLICE needs to be on
// for the SIDE_PROJECTING_PARTICLE_CACHE. Otherwise they should be off.
// I don't remember when SEED_HORIZONTALLY should be used.
#define SEED_HORIZONTALLY 0
#define SEED_SIDEWAYS 0
#define SEED_MULTISLICE 0

//Status as of 1/8/2008
//RENDER_SWEEP - works fine (with both PROJECTING_PARTICLE_CACHE and SIDE_PROJECTING_PARTICLE_CACHE)
//RENDER_MC - crashes when spanning multiple blocks and particle radius scaling doesn't work properly (probably a problem in MC_PARTICLE_CACHE)
//RENDER_HASH - errors when spanning multiple blocks in debug mode and particle radius scaling doesn't work properly (probably a problem in HASH_PARTICLE_CACHE)
//RENDER_INF_HASH - particle radius scaling doesn't work properly (probably a problem in INF_HASH_PARTICLE_CACHE)
//RENDER_VOLUMETRIC - not finished

#define RENDER_SWEEP 0
#define RENDER_MC 1
#define RENDER_HASH 2
#define RENDER_INF_HASH 3
#define RENDER_VOLUMETRIC 4						// NOTE: This was never finished. It was supposed to be a renderer that doesn't trace the surface but goes from bottom to top, efficiently finding transitions in sign.

// More recent implementations for Portal 2 - For X360 and PS3

#define RENDER_BUCKET_IMPLEMENTATION 5			// Version that was 75% faster on X360. Big memory impact.
// These implementations do not care about the CACHE_MODE as the cache is baked in the renderer implementation.
#define RENDER_SECTION_IMPLEMENTATION 6			// First version with section. Naive job implementation to make it work.
#define RENDER_JOB_IMPLEMENTATION 7				// 2nd version with section. More advanced job implementation. Work on SPU with software cache.
#define RENDER_LIGHT_BUCKET_IMPLEMENTATION 8	// Version with buckets baked in section. Less memory usage than RENDER_JOB_IMPLEMENTATION, no time spent initializing buckets.

#define RENDER_MODE RENDER_LIGHT_BUCKET_IMPLEMENTATION

// Settings for LavaTable. Otherwise, use settings for LavaLamp
#define LAVA_TABLE 0
#define LAVA_LAMP 1
#define BLOB_MONSTER 2
#define BLOB_SETTINGS BLOB_MONSTER

// NOTE: With the current settings, the limit of these sizes is 254.
// A size of 255 could cause an overflow in indecies (which are limited
// to 16 bits and have one value reserved for to indicate no data).

#if (BLOB_SETTINGS == LAVA_TABLE)
	#define MAX_PARTICLES 1500

	#define RENDERER_CACHE_XSIZE 254
	#define RENDERER_CACHE_YSIZE 200
	#define RENDERER_CACHE_ZSIZE 100

	#define PARTICLE_CACHE_XSIZE 127
	#define PARTICLE_CACHE_YSIZE 100
	#define PARTICLE_CACHE_ZSIZE 50
#elif (BLOB_SETTINGS == LAVA_LAMP || BLOB_SETTINGS == BLOB_MONSTER)
	//#define MAX_PARTICLES 400

	#define RENDERER_CACHE_XSIZE 100
	#define RENDERER_CACHE_YSIZE 100
	#define RENDERER_CACHE_ZSIZE 100

	#define PARTICLE_CACHE_XSIZE 100
	#define PARTICLE_CACHE_YSIZE 100
	#define PARTICLE_CACHE_ZSIZE 100
#endif

// Define a function for printing messages
//#define IMP_PRINT printf
#define IMP_PRINT ConMsg


////////////////////////////////////
// Settings for Cache

// Whether to use bit masks in 3D grid based caches
#define USE_BIT_MASK 0
// Whether to allocate caches dynamically or statically
// Needed for old MCParticleCache
#define DYNAMIC_ALLOC 1
#define MAX_PARTICLES 3100
#define MC_CACHE_TWOWAY 0


////////////////////////////////////
// Settings for Renderer

// Whether the renderer needs to make sure we don't step out of bounds (doesn't apply to infinite hash renderer)
#if (CACHE_MODE == MC_PARTICLE_CACHE || CACHE_MODE == HASH_PARTICLE_CACHE)
	#define DO_BOUNDS_CHECK 1
#else
	#define DO_BOUNDS_CHECK 0
#endif
// Whether to do a breadth first iteration in the sweep renderer (other renderers don't care about this)
#define BREADTH_FIRST 0

// Comment this to make the surface factors constant
#define DYNAMIC_FACTORS 1
// Force compiler to inline renderer functions
//#define DOINLINE __forceinline
#define DOINLINE
// Force compiler to use fastcall convention
//#define DOFASTCALL __fastcall
#define DOFASTCALL 

// Whether normals at the corners of a cube should be normalized.
// This seems to improve normal quality slightly, but perhaps not
// enough to justify the added computation.
#define NORMALIZE_CORNER_NORMALS 0
#define NORMALIZE_INNER_LOOP_NORMALS 1

// Whether we use X86 assembly with a floating point stack
// regular floating point (which gets optimized by compiler)
// or SIMD SSE instructions (in this case, SSE2)
#define BLR_X86ASM 0
#define BLR_REGULAR 1
#define BLR_SIMD 2
#define BLR_CODE BLR_SIMD  //TODO: BLR_REGULAR doesn't compile.


////////////////////////////////////
// Settings for User Defined Functions

// Whether to check the radius of a particle in user defined functions (needed with imprecise particle caches)
// DO_DADIUS_CHECK is automatically disabled when using precise caches (these include all of the projecting caches by
// scaling radius of particles as they are inserted). This does not work correctly for other caches.
#if (CACHE_MODE==PROJECTING_PARTICLE_CACHE || CACHE_MODE==SIDE_PROJECTING_PARTICLE_CACHE || CACHE_MODE==VOLUMETRIC_PARTICLE_CACHE)
	#define DO_RADIUS_CHECK 0
#else
	#define DO_RADIUS_CHECK 1
#endif
// Whether to enable coloring of render tiles
// Coloring has been disabled as it does not work very well with recycling of Renderer (for the two steps mode)
#define COLOR_TILES 0
// Whether to enable coloring of physics tiles
#define COLOR_PHYS_TILES 0
// Whether to calculate DySqPlusDzSq in ParticleCache
#define CALC_DY_SQ_PLUS_DZ_SQ 0
// Whether to calculate DySqPlusDzSq in ParticleCache2
#define CALC_DX_SQ_PLUS_DZ_SQ 0
// Whether to calculate number of indecies generated by VertexBuffer
#define COUNT_N_INDECIES 0
#define COUNT_N_FLUSHES 0
#define MEMORY_TEST 0
#define SPEED_TEST 0

//////////////////////////////////
// Multithreading settings
//////////////////////////////////

// Remember to use multi-threaded vertex buffer if MT is enabled
// Multithreading doesn't work on the 360 yet (nor PS3 SPU)
#define MT_ENABLED 0
#define MAX_WORKER_THREADS 4
#define SUSPEND_BET_FRAMES 1
#define MAIN_THREAD_DOESNT_DRAW 1
#define SORT_BUFFERS 0


// The following defines are different multi-threading than above.
// They have been introduced during July-August 2010 refactoring.
// Instead of doing the multi-threading at the tile level (trashing the cache a lot on X360
// not working on SPU, we instead do the threading at the section level.
// Test the overhead of just one job for the moment.

// As stated above, there is a code to enable multi-threading (MT_ENABLED).
// Unfortunately, that code is only valid for multi-threading at the tile level.
// For some platforms (like X360, PS3 SPU), the granularity is not good enough.
// I.e. in many cases the blob fits in only one tile and still takes several ms.
// The optimization here allows the multi-threading down at the section level (8 * 8 * 8 cubes).
// A given section fits in the SPU memory (unlike a full tile like above).

#define BLOB_FILL_MODE_SYNCHRONOUS	0
#define BLOB_FILL_MODE_ASYNCHRONOUS	1
#define BLOB_FILL_MODE_EMULATE_SPU	2
#define BLOB_FILL_MODE_SPU			3

#if _PS3

# if 1
#  define BLOB_NUM_JOBS				6			// 5 SPUs and one additional main job
#  define BLOB_FILL_MODE			BLOB_FILL_MODE_SPU
# else
#  define BLOB_NUM_JOBS				1			// The SPU emulation can't work with more than one job
												// as the Bucket / Section Functor for the DMA caching can only be used
												// in one thread.
#  define BLOB_FILL_MODE			BLOB_FILL_MODE_EMULATE_SPU
# endif

#else

# ifdef _X360
#  define BLOB_NUM_JOBS				5		// X360 has one main job and 4 threads in the thread pool
# else
#  define BLOB_NUM_JOBS				4		// PC has one main job and 3 threads in the thread pool
#endif

# define BLOB_FILL_MODE				BLOB_FILL_MODE_ASYNCHRONOUS

#endif

// If set to 1, it means that we fill the blob in one thread (spawning other jobs) and drawing it in the mat queue thread.
// If set to 0, it means that we fill and draw the blob in the same thread.
#define UseTwoPassesToDraw()		1

// Number of fragments for the vertex buffer.
#define BLOB_NUMBER_OF_FRAGMENTS	128

// On PS3, the buckets are stored on the SPU
// and each SPU have their own cache. (Only one CCornerInfo stored).
#if (BLOB_NUM_JOBS == 1) || (BLOB_FILL_MODE == BLOB_FILL_MODE_SPU)
# define HasSingleCornerCache()		1
# define NUM_CORNERS_CACHE			1
#else
# define HasSingleCornerCache()		0
# define NUM_CORNERS_CACHE			BLOB_NUM_JOBS
#endif

#if _DEBUG
# define BLOB_LOG_LEVEL 1
#else
# if _CERT
#  define BLOB_LOG_LEVEL 0
# else
#  define BLOB_LOG_LEVEL 1			// Keep this enabled until everything is fixed
# endif
#endif

// Important to LOG
#define BLOB_LOG_HASHFULL		1
#define BLOB_LOG_VERTEXBUFFER	3

// Logs used for debugging
#define BLOB_LOG_CACHEUSAGE		2
#define BLOB_LOG_DMAS			4
#define BLOB_LOG_FUNCTIONS		4
#define BLOB_LOG_ITERATIONS		4
#define BLOB_LOG_LOCKS			4
#define BLOB_LOG_POLYS			6
#define BLOB_LOG_STACKOPS		8
#define BLOB_LOG_VERBOSE		10

#if defined(SPU) || (BLOB_FILL_MODE == BLOB_FILL_MODE_EMULATE_SPU)
# define BLOB_LOG(level, a)	if (level <= BLOB_LOG_LEVEL)	a
#else
# define BLOB_LOG(level, a)
#endif

#if (BLOB_FILL_MODE == BLOB_FILL_MODE_EMULATE_SPU)
#ifndef SPU
#include <stdio.h>
#define spu_printf printf
#endif
#endif

//#define BLOB_RESET_INSIDE_JOBS 1

#define BLOB_ENABLE_BUFFER_FILLING	1
#define BLOB_ENABLE_REINDEXING		1
#define BLOB_ENABLE_DRAW			1
#define BLOB_USE_FLAGS_LIST			1

#if _PS3
#define BLOB_CREATE_OWN_THREADPOOL	0
#else
// Do this to decouple main thread / mat queue thread from the blobulator jobs.
// In some cases, the blobulator jobs can be be waiting for the mat queue thread to draw fragments.
// But the mat queue thread could potentially be scheduled after the blob job are executed (thus we have a deadlock).
#define BLOB_CREATE_OWN_THREADPOOL	1
#endif

//////////////////////////////////
// The following are implementation specific defines.
// In general, they should not be modified by users.
//////////////////////////////////

#define MIN_CUBE_INDEX 0
#define MAX_CUBE_INDEX 255

#if (RENDER_MODE==RENDER_INF_HASH)
	typedef int cube_index_t;
	typedef int signed_cube_index_t;
#else
	typedef unsigned char cube_index_t;
	typedef char signed_cube_index_t;
#endif
