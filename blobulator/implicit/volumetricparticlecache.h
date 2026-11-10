//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "impdefines.h"
#include "impparticle.h"
#include "../smartarray.h"
#include "../point3d.h"


#define START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, fx, fy, fz, pCache, guard)								\
	short min_center_x = (short(ix) - RENDERER_CLASS::fieldCalcSteps) - 1;									\
		/* the -1 above is because center_x of particle is rounded down*/									\
                                                                                                            \
	PCacheElem_t** cachePlaneElem = &(pCache->cachePlane[(iy)][(iz)]);										\
	PCacheElem_t* cacheElem = *cachePlaneElem;                                                              \
	while(cacheElem->prev->center_x >= min_center_x)                                                        \
	{ cacheElem = cacheElem->prev; } /*First go down until we're at first element in range*/                \
	while(cacheElem->next->center_x < min_center_x)                                                         \
	{ cacheElem = cacheElem->next; } /*Then go up*/                                                         \
	*cachePlaneElem = cacheElem;                                                                            \
                                                                                                            \
	short max_center_x = short(ix) + RENDERER_CLASS::fieldCalcSteps;										\
                                                                                                            \
	while(cacheElem->center_x <= max_center_x)                                                              \
	{                                                                                                       \
		if(cacheElem->bot_x <= (ix) && cacheElem->top_x >= (ix)) /*try single & to avoid two branches!!!*/  \
		{                                                                                                   \
			ImpParticle* p = cacheElem->p;                                                                  \
// END of START_LOOP_OVER_NEARBY_PARTICLES


#define END_LOOP_OVER_NEARBY_PARTICLES()    \
		}                                   \
		cacheElem = cacheElem->next;        \
	}                                       \
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



// TODO: Can reduce size of this struct...
// particle can be an array index (16 bit)
// a bit dangerous however if next is 16 bits.
// mid_top_x might not be necessary.
struct PCacheElem_t
{
    short center_x;
	cube_index_t bot_x;
	cube_index_t top_x;
	//cube_index_t mid_top_x;
	//cube_index_t mid_bot_x;
	

	PCacheElem_t* prev;
	PCacheElem_t* next;
	ImpParticle* p;

	#if (CALC_DY_SQ_PLUS_DZ_SQ==1)
		float dySqPlusDzSq;
	#endif
};

struct pcache_YZ_t
{
	cube_index_t y, z;
};

typedef PCacheElem_t* PCachePlaneElem_t;

// TODO: The first line here is the more correct, but won't compile
//typedef PCachePlaneElem_t (*PCachePlane_t)[zsizeP1];
typedef PCachePlaneElem_t (*PCachePlane_t)[RENDERER_CACHE_ZSIZE+1];

class VolumetricParticleCache
{
public:
	VolumetricParticleCache();
	~VolumetricParticleCache();	

	void setOffset(Point3D const& offset) {}; // This is here for compatibility with old MCParticleCache
	void beginTile();
	void addParticle(ImpParticle* p);
	void buildCache(float cw, float fieldRadius, Point3D& mins, Point3D& maxs);
	void endTile();

	void beginSlice(cube_index_t x) {};
	void endSlice(cube_index_t x) {};

private:
	#if (IMP_PROFILE == 1)
		int n_tiles;
		__int64 memory_total;
	#endif

	void insertIntoCache(ImpParticle* p, float cw, float fieldRadius, Point3D& mins, Point3D& maxs);

	PCacheElem_t* botSentinel;
	PCacheElem_t* midSentinel;
	PCacheElem_t* topSentinel;
	SmartArray<ImpParticle*> particles;
	SmartArray<PCacheElem_t*> cacheElements;
	SmartArray<pcache_YZ_t> clearList;
	int curCacheElement;
public:
	PCachePlane_t cachePlane; // Really this shouldn't be public, but ImpRenderer should be a friend of the cache
};