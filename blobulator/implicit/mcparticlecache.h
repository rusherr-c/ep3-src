//==== Blobulator. Copyright � 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "impdefines.h"
#include "../util/fasttrunc.h"
#include "../smartarray.h"
#include "../point3d.h"
#include "impparticle.h"

#if (MC_CACHE_TWOWAY == 1)

#define START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, fx, fy, fz, pCache, guard)								\
																											\
	if(!guard ||																							\
		(ix > 0 && ix < RENDERER_CACHE_XSIZE																\
		 && iy > 0 && iy < RENDERER_CACHE_YSIZE																\
		 && iz > 0 && iz < RENDERER_CACHE_ZSIZE ))															\
	{																										\
		ImpParticle** pit = pCache->get(fx, fy, fz);														\
																											\
		while(*pit)																							\
		{																									\
			ImpParticle* p = *pit;																			\
			pit++;																							\
// END of START_LOOP_OVER_NEARBY_PARTICLES


#define END_LOOP_OVER_NEARBY_PARTICLES()    \
		}									\
	}										\
// END of END_LOOP_OVER_NEARBY_PARTICLES

#else

	#if (USE_BIT_MASK == 1)
		#define START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, fx, fy, fz, pCache, guard)								\
																													\
		if(!guard ||																								\
			(ix > 0 && ix < RENDERER_CACHE_XSIZE																	\
			 && iy > 0 && iy < RENDERER_CACHE_YSIZE																	\
			 && iz > 0 && iz < RENDERER_CACHE_ZSIZE ))																\
		{																											\
			int __x = trunc((fx-pCache->BBMins[0]) * pCache->oneOverCubeWidth);										\
			int __y = trunc((fy-pCache->BBMins[1]) * pCache->oneOverCubeWidth);										\
			int __z = trunc((fz-pCache->BBMins[2]) * pCache->oneOverCubeWidth);										\
																													\
			Assert(__x >= 0 && __x < mcpc_xSize && __y >= 0 && __y < mcpc_ySize && __z >= 0 && __z < mcpc_zSize);	\
																													\
			MCArrayItem_t* centerItem = &MCPC_ARRAY(__x, __y, __z);													\
																													\
			for(int i = 0; i < pCache->check_list_size; i++)														\
			{																										\
				CheckListItem* clitem = &(pCache->check_list[i]);													\
				MCCParticleListNode_t* node = (centerItem + clitem->offset)->particlesHere;							\
																													\
				while(node != NULL)																					\
				{																									\
					if(skipBitMask || (node->mask & clitem->mask))													\
					{																								\
						ImpParticle* p = node->particle;															\
		// END of END_LOOP_OVER_NEARBY_PARTICLES
	#else
		#define START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, fx, fy, fz, pCache, guard)								\
																													\
		if(!guard ||																								\
			(ix > 0 && ix < RENDERER_CACHE_XSIZE																	\
			 && iy > 0 && iy < RENDERER_CACHE_YSIZE																	\
			 && iz > 0 && iz < RENDERER_CACHE_ZSIZE ))																\
		{																											\
			int __x = trunc((fx-pCache->BBMins[0]) * pCache->oneOverCubeWidth);										\
			int __y = trunc((fy-pCache->BBMins[1]) * pCache->oneOverCubeWidth);										\
			int __z = trunc((fz-pCache->BBMins[2]) * pCache->oneOverCubeWidth);										\
																													\
			Assert(__x >= 0 && __x < mcpc_xSize && __y >= 0 && __y < mcpc_ySize && __z >= 0 && __z < mcpc_zSize);	\
																													\
			MCArrayItem_t* centerItem = &MCPC_ARRAY(__x, __y, __z);													\
																													\
			for(int i = 0; i < pCache->check_list_size; i++)														\
			{																										\
				CheckListItem* clitem = &(pCache->check_list[i]);													\
				MCCParticleListNode_t* node = (centerItem + clitem->offset)->particlesHere;							\
																													\
				while(node != NULL)																					\
				{																									\
					{																								\
						ImpParticle* p = node->particle;															\
		// END of END_LOOP_OVER_NEARBY_PARTICLES
	#endif

	#define END_LOOP_OVER_NEARBY_PARTICLES()		\
													\
					}								\
					node = node->next;				\
				}									\
			}										\
		}											\
	// END of END_LOOP_OVER_NEARBY_PARTICLES

#endif


#define CUBE(a) ((a) * (a) * (a))
#define MAX_CHECKLIST_RADIUS 5
#define MAX_CHECKLIST_SIZE CUBE(MAX_CHECKLIST_RADIUS)


const int mcpc_xSize = PARTICLE_CACHE_XSIZE;
const int mcpc_ySize = PARTICLE_CACHE_YSIZE;
const int mcpc_zSize = PARTICLE_CACHE_ZSIZE;


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
	ImpParticle** particlesCloseby;
};

struct CheckListItem
{
	int offset;
	#if (USE_BIT_MASK == 1)
		int mask;
	#endif
};


//#define MCPC_ARRAY(x, y, z) (MCParticleCache::mcpc_array[mcpc_ySize * mcpc_zSize * (x) + mcpc_zSize * (y) + (z)])
#define MCPC_ARRAY(x, y, z) (MCParticleCache::mcpc_array[(x)][(y)][(z)])


class MCParticleCache
{
  public:
	//MCParticleCache(float _renderCubeWidth = 1.51f, float _renderInteractionRadius = 3.0f); // 5x5x5 search
	MCParticleCache(float _renderCubeWidth = 3.01f, float _renderInteractionRadius = 3.0f); // 3x3x3 search

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

	// NOTE: These should all be private, but I made the public for the
	// sake of allowing the #define iterator to have acces to these.
 public:
	int n_tiles;
	int64 memory_total;

	//Allocating the array within the class is slower
	//Probably a static allocation within the class is fine
	#if (DYNAMIC_ALLOC==1)
		static MCArrayItem_t (*mcpc_array)[mcpc_ySize][mcpc_zSize];
	#else
		static MCArrayItem_t mcpc_array[mcpc_xSize][mcpc_ySize][mcpc_zSize];
	#endif

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
	int particles_size;
	MCCParticleListNode_t particles[MAX_PARTICLES];
	
	int clear_list_size;
	MCArrayItem_t* clear_list[MAX_PARTICLES];

	// This is the worst case... if we test every cube... the plus one is for the null sentinel value
	int particles_closeby_size;
	ImpParticle* particles_closeby[MAX_PARTICLES * (MAX_CHECKLIST_SIZE+1)];
	
	// This is the worst case... if the particles are really spread out
	int clear_closeby_size;
	MCArrayItem_t* clear_closeby_list[MAX_PARTICLES * MAX_CHECKLIST_SIZE];
};

inline void MCParticleCache::setOffset(Point3D const& _offset)
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
ImpParticle** MCParticleCache::get(float ocx, float ocy, float ocz)
{
	// TODO: This should use round, not trunc for faster performance....
	// Also, I should actually just hand-code this in assembly because
	// the pocessor stupidly writes back to memory and loads again...
	// Also, halfXYZSize can be integers and added using an integer add... not floating
	// point.
	int x = trunc((ocx-BBMins[0]) * oneOverCubeWidth);
	int y = trunc((ocy-BBMins[1]) * oneOverCubeWidth);
	int z = trunc((ocz-BBMins[2]) * oneOverCubeWidth);

	Assert(x >= 0 && x < mcpc_xSize && y >= 0 && y < mcpc_ySize && z >= 0 && z < mcpc_zSize);

	MCArrayItem_t* centerItem = &MCPC_ARRAY(x, y, z);

	if(centerItem->particlesCloseby == NULL)
	{
		clear_closeby_list[clear_closeby_size++] = centerItem;

		centerItem->particlesCloseby = &(particles_closeby[particles_closeby_size]);

		for(int i = 0; i < check_list_size; i++)
		{
			// Get the entry from the array
			CheckListItem* clitem = &(check_list[i]);

			MCCParticleListNode_t* node = (centerItem + clitem->offset)->particlesHere;

			while(node != NULL)
			{
				ImpParticle* b = node->particle;

				#if (USE_BIT_MASK == 1)
				if(node->mask & clitem->mask)
				#endif
				{
					// Get or allocate new node
					particles_closeby[particles_closeby_size++] = b;
				}

				node = node->next;
			}
		}

		particles_closeby[particles_closeby_size++] = NULL;
	}

	return centerItem->particlesCloseby;
}

void MCParticleCache::clearParticles()
{
	memory_total += (mcpc_xSize * mcpc_ySize * mcpc_zSize * sizeof(MCArrayItem_t));
	memory_total += (particles_size * sizeof(MCCParticleListNode_t));
	memory_total += (clear_list_size * sizeof(MCArrayItem_t*));
	memory_total += (particles_closeby_size * sizeof(ImpParticle*));
	memory_total += (clear_closeby_size * sizeof(MCArrayItem_t*));

	n_tiles++;
	if(n_tiles == 100)
	{
		IMP_PRINT("MCParticleCache avg max memory usage = %f\n", float(memory_total) / (100.0f * 1000000.0f));
		n_tiles = 0;
		memory_total = 0;
	}


	for(int i = 0; i < clear_list_size; i++)
	{
		clear_list[i]->particlesHere = NULL;
	}
	for(int i = 0; i < clear_closeby_size; i++)
	{
		clear_closeby_list[i]->particlesCloseby = NULL;
	}
	clear_list_size = 0;
	clear_closeby_size = 0;
	particles_size = 0;
	particles_closeby_size = 0;

	clearCount++;

	//printf("InsertCount=%d(%f), GetCount=%d(%f), SearchCount=%d(%f)\n", insertCount, float(insertCount)/float(clearCount), getCount, float(getCount)/float(clearCount), searchCount, float(searchCount)/float(clearCount));
	//printf("ParticlesCloseby Size = %d, ClearParticlesCloseby = %d, ParticlesPerCube=%f\n", particlesCloseby.size, clearParticlesClosebyList.size, float(particlesCloseby.size) / float(clearParticlesClosebyList.size));
}