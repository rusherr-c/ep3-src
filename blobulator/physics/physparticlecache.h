//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "../smartarray.h"
#include "../point3d.h"
#include "../implicit/impdefines.h"
#include "physparticle.h"

#define CUBE(a) ((a) * (a) * (a))
#define MAX_CHECKLIST_DIAMETER 5
#define MAX_CHECKLIST_SIZE CUBE(MAX_CHECKLIST_DIAMETER)


const int xSize = PARTICLE_CACHE_XSIZE;
const int ySize = PARTICLE_CACHE_YSIZE;
const int zSize = PARTICLE_CACHE_ZSIZE;

class PhysParticleListNode
{
  public:
	PhysParticle* particle;
	PhysParticleListNode* next;
	int mask; // TODO: shouldn't really be here
};

// TODO: Rather than using pointers here, I should
// use unsigned shorts that are indexes into the array....
// it will save lots and lots of memory and really increase
// performance (as it did in imprenderer)
struct ArrayItem
{
	PhysParticleListNode* particlesHere;
};

struct CheckListItem
{
	int offset;
	int mask;
};

struct PhysParticleAndDist
{
	PhysParticle* particle;
	float distSq;
};

/*
#if (DYNAMIC_ALLOC==1)
	// NOTE: This allows ArrayItem to be shared between instances
	// of PhysParticleCache, but is not thread safe.
	// Going forward, I should try to have a single version of
	// the particle cache per thread.
	extern ArrayItem (*array)[ySize][zSize];
#else
	extern ArrayItem array[xSize][ySize][zSize];
#endif
*/

class PhysParticleCache
{
  //Allocating the array within the class is slower
  //Probably a static allocation within the class is fine
  //ArrayItem (*array)[ySize][zSize];

  public:
	PhysParticleCache();
	PhysParticleCache(float interactionRadius, float cubeWidth = 0.0f);
	~PhysParticleCache();

	//void generate_check_list2(float cutoff_r, float cube_width);

	void setCacheParams(float interactionRadius, float cubeWidth = 0.0f);
	void setInteractionRadius(float interactionRadius) { setCacheParams(interactionRadius); }
	void setCubeWidth(float cubeWidth) { setCacheParams(-1.0f, cubeWidth); }
	void setOffset(Point3D const& offset);

	void beginFrame();
	void beginTile(int nParticles);
	void insertParticle(PhysParticle* b);
	void calcNeighbors(PhysParticle* b, PhysParticle*** neighbors, int* size);
	void endTile();
	void endFrame();

	Point3D getOuterDimensions() { return outerDimensions; };
	Point3D getInnerDimensions() { return innerDimensions; };
	float getMarginWidth() { return marginWidth; };

	inline PhysParticleAndDist* get(PhysParticle *b);
	
 private:

	void generateCheckList(float cutoff_r, float cube_width);
	void recalculateBB();
	void recalculateDimensions();

	ArrayItem (*array)[ySize][zSize];

	int marginNCubes;
	float marginWidth;

	Point3D offset;

	Point3D outerDimensions;
	Point3D outerBBMins;
	Point3D outerBBMaxs;

	Point3D innerDimensions;
	Point3D innerBBMins;
	Point3D innerBBMaxs;


	int insertCount;
	int getCount;
	int searchCount;
	int clearCount;

	int check_list_size;
	CheckListItem check_list[MAX_CHECKLIST_SIZE];

	//int check_list2_size;
	//int check_list2[MAX_CHECKLIST_SIZE];

	/*float halfXSize, halfYSize, halfZSize;*/
	// TODO: Is it necessary to store these
	float cubeWidth, oneOverCubeWidth;
	float interactionRadius;
	float interactionRadiusSq;
	//float symCubeWidth, renderCubeWidth;

	 // TODO: I can keep a next particle pointer inside particles to get rid of this array
	/*
	int particles_size;
	PhysParticleListNode particles[MAX_PARTICLES];

	int clear_list_size;
	ArrayItem* clear_list[MAX_PARTICLES];
	*/

	SmartArray<PhysParticleListNode> particles_sa;
	SmartArray<ArrayItem*> clear_list_sa;
	//SmartArray<PhysParticleAndDist> particles_closeby_sa;
	SmartArray<PhysParticle*> particles_closeby_sa2;

	/*
	// This is the worst case... if we test every cube... the plus one is for the null sentinel value
	int particles_closeby_size;
	PhysParticle* particles_closeby[MAX_PARTICLES * (MAX_CHECKLIST_SIZE+1)];
	
	// This is the worst case... if the particles are really spread out
	int clear_closeby_size;
	ArrayItem* clear_closeby_list[MAX_PARTICLES * MAX_CHECKLIST_SIZE];
	*/
};


// TODO: Move this into PhysParticleCacheFactory.h?
class PhysParticleCacheFactory
{
public:
	static PhysParticleCacheFactory* factory;

	PhysParticleCache* getCache();
	void returnCache(PhysParticleCache* cache);

private:
	PhysParticleCacheFactory();
	SmartArray<PhysParticleCache*> m_caches;
};

