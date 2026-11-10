//==== Blobulator. Copyright � 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "impdefines.h"
#include "../util/fasttrunc.h"
#include "../smartarray.h"
#include "../point3d.h"
#include "impparticle.h"

#if (MC_CACHE_TWOWAY == 1)

#define START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, fx, fy, fz, pCache, guard)									\
																												\
		ImpParticle** pit = pCache->get(fx, fy, fz);															\
																												\
		while(*pit)																								\
		{																										\
			ImpParticle* p = *pit;																				\
			pit++;																								\
// END of START_LOOP_OVER_NEARBY_PARTICLES


#define END_LOOP_OVER_NEARBY_PARTICLES()    \
		}									\
// END of END_LOOP_OVER_NEARBY_PARTICLES

#else

	#if (USE_BIT_MASK == 1)
		#define START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, fx, fy, fz, pCache, guard)									\
																														\
				int __x = trunc((fx-pCache->BBMins[0]) * pCache->oneOverCubeWidth);										\
				int __y = trunc((fy-pCache->BBMins[1]) * pCache->oneOverCubeWidth);										\
				int __z = trunc((fz-pCache->BBMins[2]) * pCache->oneOverCubeWidth);										\
																														\
				for(int i = 0; i < pCache->check_list_size; i++)														\
				{																										\
					CheckListItem* clitem = &(pCache->check_list[i]);													\
					MCCParticleListNode_t* node = pCache->findArrayItem(__x+clitem->offset_x,							\
						__y+clitem->offset_y, __z+clitem->offset_z)->particlesHere;										\
																														\
					while(node != NULL)																					\
					{																									\
						if(node->mask & clitem->mask)																	\
						{																								\
							ImpParticle* p = node->particle;															\
		// END of END_LOOP_OVER_NEARBY_PARTICLES
	#else
		#define START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, fx, fy, fz, pCache, guard)									\
																														\
				int __x = trunc((fx-pCache->BBMins[0]) * pCache->oneOverCubeWidth);										\
				int __y = trunc((fy-pCache->BBMins[1]) * pCache->oneOverCubeWidth);										\
				int __z = trunc((fz-pCache->BBMins[2]) * pCache->oneOverCubeWidth);										\
																														\
				for(int i = 0; i < pCache->check_list_size; i++)														\
				{																										\
					CheckListItem* clitem = &(pCache->check_list[i]);													\
					MCCParticleListNode_t* node = pCache->findArrayItem(__x+clitem->offset_x,							\
						__y+clitem->offset_y, __z+clitem->offset_z)->particlesHere;										\
																														\
					while(node != NULL)																					\
					{																									\
						{																								\
							ImpParticle* p = node->particle;															\
		// END of END_LOOP_OVER_NEARBY_PARTICLES
	#endif

#define END_LOOP_OVER_NEARBY_PARTICLES()    \
											\
				}							\
				node = node->next;			\
			}								\
		}									\
// END of END_LOOP_OVER_NEARBY_PARTICLES

#endif


#define CUBE(a) ((a) * (a) * (a))
#define MAX_CHECKLIST_RADIUS 5
#define MAX_CHECKLIST_SIZE CUBE(MAX_CHECKLIST_RADIUS)


struct MCCParticleListNode_t
{
	ImpParticle* particle;
	MCCParticleListNode_t* next;
	#if (USE_BIT_MASK == 1)
		int mask;
	#endif
};

// TODO: Rather than using pointers here, I should
// use unsigned shorts that are indexes into the array....
// it will save lots and lots of memory and really increase
// performance (as it did in imprenderer)
struct MCArrayItem_t
{
	MCCParticleListNode_t* particlesHere;
	int particlesCloseby_index;
	MCArrayItem_t* next;
	cube_index_t x, y, z;
};

struct CheckListItem
{
	char offset_x, offset_y, offset_z;
	#if (USE_BIT_MASK == 1)
		int mask;
	#endif
};

class InfHashParticleCache
{
	static const int mcpc_xSize = PARTICLE_CACHE_XSIZE;
	static const int mcpc_ySize = PARTICLE_CACHE_YSIZE;
	static const int mcpc_zSize = PARTICLE_CACHE_ZSIZE;

  public:
	//InfHashParticleCache(float _renderCubeWidth = 1.51f, float _renderInteractionRadius = 3.0f); // 5x5x5 search
	InfHashParticleCache(float _renderCubeWidth = 3.01f, float _renderInteractionRadius = 3.0f); // 3x3x3 search

	void generate_check_list(float cutoff_r, float cube_width);

	inline void beginTile() {};
	void addParticle(ImpParticle* p);
	void buildCache(float cw, float fieldRadius, Point3D& mins, Point3D& maxs) {};
	inline void endTile() { clearParticles(); }

	void beginSlice(cube_index_t x) {};
	void endSlice(cube_index_t x) {};

	inline ImpParticle** get(float ocx, float ocy, float ocz);
	inline void clearParticles();

	inline void setOffset(Point3D const& offset);
	inline MCArrayItem_t* findArrayItem(const cube_index_t x, const cube_index_t y, const cube_index_t z);

	// NOTE: These should all be private, but I made the public for the
	// sake of allowing the #define iterator to have acces to these.
 public:
	 int n_tiles;
	int64 memory_total;

	SmartArray<MCArrayItem_t*> slots;
	int array_item_cache_used;
	SmartArray<MCArrayItem_t*> array_item_cache;

	Point3D offset;
	Point3D cacheDimensions;
	Point3D BBMins;
	Point3D BBMaxs;

	int insertCount;
	int getCount;
	int searchCount;
	int clearCount;

	int check_list_size;
	CheckListItem check_list[MAX_CHECKLIST_SIZE];

	float cubeWidth, oneOverCubeWidth;

	 // TODO: I can keep a next particle pointer inside particles to get rid of this array
	int particle_list_elements_size;
	SmartArray<MCCParticleListNode_t*> particle_list_elements_sa;
	
	// This is the worst case... if we test every cube... the plus one is for the null sentinel value
	//int particles_closeby_sa_size;
	SmartArray<ImpParticle*> particles_closeby_sa;
};


#define P1 (73856093u)
#define P2 (19349663u)
#define P3 (83492791u)
#define MYUINT uint

MCArrayItem_t* InfHashParticleCache::findArrayItem(const cube_index_t x, const cube_index_t y, const cube_index_t z)
{
	MYUINT index = MYUINT((MYUINT(x) * P1) ^ (MYUINT(y) * P2) ^ (MYUINT(z) * P3)) % slots.size;

	MCArrayItem_t** slot = &(slots[index]);

	if(*slot != 0)
	{
		MCArrayItem_t* item = *slot;
		
		while(item != NULL)
		{
			if(item->x == x && item->y == y && item->z == z)
			{
				return item;
			}
			item = item->next;
		}
	}

	if(array_item_cache_used == array_item_cache.size)
	{
		array_item_cache.pushAutoSize((MCArrayItem_t*)malloc(sizeof(MCArrayItem_t)));
	}
	
	// Allocate a new corner
	MCArrayItem_t* new_item = array_item_cache[array_item_cache_used++];

	new_item->particlesHere = NULL;
	new_item->particlesCloseby_index = -1;
	new_item->next = *slot;
	new_item->x = x;
	new_item->y = y;
	new_item->z = z;
	*slot = new_item;

	return new_item;
}

inline void InfHashParticleCache::setOffset(Point3D const& _offset)
{
	offset = _offset;
	BBMins = offset - (cacheDimensions*0.5f);
	BBMaxs = offset - (cacheDimensions*0.5f);
}

// TODO: All of this can be made even more faster if
// within the calc_normal loop, I check bit masks...
// Then, I can have bit masks that allow me to get a much finer resolution.
// However, i would have to calculate a bit mask each time I called this function.

// TODO: I can make this code faster by making truncation the default
// rounding mode... that will save three adds per call.

// TODO: Particles with a small number of particles don't need to use the particle cache...
// you can just always return all the particles in that particle.
ImpParticle** InfHashParticleCache::get(float ocx, float ocy, float ocz)
{
	// TODO: This should use round, not trunc for faster performance....
	// Also, I should actually just hand-code this in assembly because
	// the pocessor stupidly writes back to memory and loads again...
	// Also, halfXYZSize can be integers and added using an integer add... not floating
	// point.
	int x = trunc((ocx-BBMins[0]) * oneOverCubeWidth);
	int y = trunc((ocy-BBMins[1]) * oneOverCubeWidth);
	int z = trunc((ocz-BBMins[2]) * oneOverCubeWidth);

	MCArrayItem_t* centerItem = findArrayItem(x, y, z);

	if(centerItem->particlesCloseby_index == -1)
	{
		centerItem->particlesCloseby_index = particles_closeby_sa.size;

		for(int i = 0; i < check_list_size; i++)
		{
			// Get the entry from the array
			CheckListItem* clitem = &(check_list[i]);

			MCCParticleListNode_t* node =  findArrayItem(x+clitem->offset_x, y+clitem->offset_y, z+clitem->offset_z)->particlesHere;

			while(node != NULL)
			{
				ImpParticle* b = node->particle;

				#if (USE_BIT_MASK == 1)
					if(node->mask & clitem->mask)
				#endif
				{
					// Get or allocate new node
					particles_closeby_sa.pushAutoSize(b);
				}

				node = node->next;
			}
		}

		particles_closeby_sa.pushAutoSize(0);
	}

	return &(particles_closeby_sa[centerItem->particlesCloseby_index]);
}

void InfHashParticleCache::clearParticles()
{
	memory_total += (slots.size * sizeof(MCArrayItem_t*));
	memory_total += (array_item_cache_used * (sizeof(MCArrayItem_t*) + sizeof(MCArrayItem_t)));
	memory_total += (particle_list_elements_size * (sizeof(MCCParticleListNode_t*) + sizeof(MCCParticleListNode_t)));
	memory_total += (particles_closeby_sa.size * sizeof(ImpParticle*));

	n_tiles++;
	if(n_tiles == 100)
	{
		IMP_PRINT("InfHashParticleCache avg max memory usage = %f\n", float(memory_total) / (100.0f * 1000000.0f));
		n_tiles = 0;
		memory_total = 0;
	}

	for(int i = 0; i < slots.size; i++)
	{
		slots[i] = NULL;
	}

	while(slots.size < int(float(array_item_cache_used) * 1.5f))
	{
		slots.pushAutoSize(NULL);
	}

	array_item_cache_used = 0;

	particle_list_elements_size = 0;
	//particles_closeby_size = 0;
	particles_closeby_sa.size = 0;

	clearCount++;

	//printf("InsertCount=%d(%f), GetCount=%d(%f), SearchCount=%d(%f)\n", insertCount, float(insertCount)/float(clearCount), getCount, float(getCount)/float(clearCount), searchCount, float(searchCount)/float(clearCount));
	//printf("ParticlesCloseby Size = %d, ClearParticlesCloseby = %d, ParticlesPerCube=%f\n", particlesCloseby.size, clearParticlesClosebyList.size, float(particlesCloseby.size) / float(clearParticlesClosebyList.size));
}