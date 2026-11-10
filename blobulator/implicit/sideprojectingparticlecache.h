//==== Blobulator. Copyright � 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "impdefines.h"
#include "impparticle.h"
#include "../smartarray.h"
#include "../point3d.h"

#define START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, fx, fy, fz, pCache, guard)								\
	short min_center_y = (short(iy) - RENDERER_CLASS::fieldCalcSteps) - 1;									\
		/* the -1 above is there because center_y of particle is rounded down*/								\
																											\
	PSliceData_t* sliceData = pCache->slices[ix].sliceData;													\
	SmartArray<PCacheElem_t>& cacheElements = sliceData->cacheElements;										\
	elem_index_t* cachePlaneElemPtr = &(sliceData->zArray[iz]);												\
	elem_index_t cacheElem = *cachePlaneElemPtr;															\
	PCacheElem_t* cacheElemPtr = &(cacheElements[cacheElem]);												\
	while(cacheElements[cacheElemPtr->prev].center_y >= min_center_y)                                       \
	{ cacheElem = cacheElemPtr->prev;																		\
	  cacheElemPtr = &(cacheElements[cacheElem]);															\
	} /*First go down until we're at first element in range*/												\
	while(cacheElements[cacheElemPtr->next].center_y < min_center_y)                                        \
	{ cacheElem = cacheElemPtr->next;																		\
	  cacheElemPtr = &(cacheElements[cacheElem]);															\
	} /*Then go up*/																						\
	*cachePlaneElemPtr = cacheElem;                                                                         \
                                                                                                            \
	short max_center_y = short(iy) + RENDERER_CLASS::fieldCalcSteps;                                        \
                                                                                                            \
	while(cacheElemPtr->center_y <= max_center_y)															\
	{                                                                                                       \
		/* Note: should try single & to avoid two branches!!!*/												\
		if(cacheElemPtr->bot_y <= (iy) && cacheElemPtr->top_y >= (iy))										\
		{                                                                                                   \
			ImpParticle* p = cacheElemPtr->p;																\
// END of START_LOOP_OVER_NEARBY_PARTICLES


#define END_LOOP_OVER_NEARBY_PARTICLES()					\
		}													\
		cacheElem = cacheElemPtr->next;						\
		cacheElemPtr = &(cacheElements[cacheElem]);			\
	}														\
// END of END_LOOP_OVER_NEARBY_PARTICLES


/*
#define END_LOOP_OVER_NEARBY_PARTICLES()    \
			#ifdef SPEED_CHECK              \
			influence_count ++;             \
			#endif                          \
		}                                   \
		#ifdef SPEED_CHECK                  \
		cache_count ++;                     \
		#endif                              \
		cacheElem = cacheElem->next;        \
	}                                       \
// END of END_LOOP_OVER_NEARBY_PARTICLES
*/

// NOTE: this may overflow for extremely large data sets.
typedef unsigned int elem_index_t;

// TODO: Can reduce size of this struct...
// particle can be an array index (16 bit)
// a bit dangerous however if next is 16 bits.
// mid_top_x might not be necessary.
struct PCacheElem_t
{
    short center_y;
	cube_index_t bot_y;
	cube_index_t top_y;
	//cube_index_t mid_top_x;
	//cube_index_t mid_bot_x;
	

	elem_index_t prev;
	elem_index_t next;
	ImpParticle* p;

	#if (CALC_DX_SQ_PLUS_DZ_SQ==1)
		float dxSqPlusDzSq;
	#endif
};


struct PCacheParticle_t
{
	ImpParticle* particle;
	float sliceR;
	float ycenter;
	float zcenter;
	char min_z;
	char max_z;
};

struct PSliceData_t
{
	elem_index_t zArray[RENDERER_CACHE_ZSIZE+1];
	SmartArray<PCacheElem_t> cacheElements;
	SmartArray<cube_index_t> clearList;
	int max_cache_elements_size;
};

struct PCacheSlice_t
{
	PSliceData_t* sliceData;
	SmartArray<PCacheParticle_t*> slice_particles;
};

struct pcache_XZ_t
{
	cube_index_t x, z;
};


class SideProjectingParticleCache
{
public:
	SideProjectingParticleCache();
	~SideProjectingParticleCache();	

	void setOffset(Point3D const& offset) {}; // This is here for compatibility with old MCParticleCache
	void beginTile();
	void addParticle(ImpParticle* p);
	void buildCache(float cw, float fieldRadius, Point3D& mins, Point3D& maxs);

	void beginSlice(cube_index_t x);
	void endSlice(cube_index_t x);

	void endTile();

private:
	int n_tiles;
	int64 memory_total;
	int max_alloced_arrays;
	int max_alloced_cache_elems;

	void insertIntoCache(ImpParticle* p, float cw, float fieldRadius, Point3D& mins, Point3D& maxs);
	FORCEINLINE void insertParticleIntoSlice(PCacheSlice_t* slice, PCacheParticle_t* particle);
	
	FORCEINLINE void allocSliceData(PCacheSlice_t* slice);
	FORCEINLINE void clearSlicesData(PCacheSlice_t* slice);

	const static elem_index_t midSentinel = 0;
	const static elem_index_t botSentinel = 1;
	const static elem_index_t topSentinel = 2;

	SmartArray<ImpParticle*> particles;
	SmartArray<PSliceData_t*> unusedSliceData;
	SmartArray<PCacheParticle_t*> cacheParticles;
	static const int max_clearableSlices = 3;
	SmartArray<cube_index_t> clearableSlices;
	/*
	PCacheElem_t* unusedCacheElems;
	*/

	int curCacheParticle;
	

	//int curCacheElement;
public:
	PCacheSlice_t slices[PARTICLE_CACHE_XSIZE+1];
	//PCachePlane_t cachePlane; // Really this shouldn't be public, but ImpRenderer should be a friend of the cache
};