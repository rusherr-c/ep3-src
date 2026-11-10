//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include "blobulator/util/constants.h"
#include "blobulator/physics/physparticlecache.h"
#include "blobulator/physics/physparticlecache_inl.h"

#if (RENDER_API==VALVE_API)
#include "tier0/vprof.h"
#else
#include <stdio.h>
#endif

/*
This is not thread-safe
#if (DYNAMIC_ALLOC==1)
	ArrayItem (*array)[ySize][zSize] = NULL;
#else
	ArrayItem array[xSize][ySize][zSize];
#endif
*/

//#define ARRAY(x, y, z) (array[ySize * zSize * (x) + zSize * (y) + (z)])
#define ARRAY(x, y, z) (array[(x)][(y)][(z)])


PhysParticleCache::PhysParticleCache()
	: insertCount(0)
	, getCount(0)
	, searchCount(0)
	, clearCount(0)
	, check_list_size(0)
	, cubeWidth(0.0f)
	, oneOverCubeWidth(0.0f)
	, interactionRadius(0.0f)
	//, check_list2_size(0)
{
	//#if (DYNAMIC_ALLOC==1)
	//	if(array == NULL)
	//	{
			array = (ArrayItem (*)[ySize][zSize])malloc(sizeof(ArrayItem) * xSize*ySize*zSize);
	//	}
	//#endif

	for(int i = 0; i < xSize; i++)
	for(int j = 0; j < ySize; j++)
	for(int k = 0; k < zSize; k++)
	{
		array[i][j][k].particlesHere = NULL;
	}

	setOffset(Point3D(0.0f, 0.0f, 0.0f));
	//setCacheParams(_interactionRadius, _cubeWidth);
}

PhysParticleCache::PhysParticleCache(float _interactionRadius, float _cubeWidth)
	: insertCount(0)
	, getCount(0)
	, searchCount(0)
	, clearCount(0)
	, check_list_size(0)
	, cubeWidth(0.0f)
	, oneOverCubeWidth(0.0f)
	, interactionRadius(0.0f)
	//, check_list2_size(0)
{
	//#if (DYNAMIC_ALLOC==1)
	//	if(array == NULL)
	//	{
			array = (ArrayItem (*)[ySize][zSize])malloc(sizeof(ArrayItem) * xSize*ySize*zSize);
	//	}
	//#endif

	for(int i = 0; i < xSize; i++)
	for(int j = 0; j < ySize; j++)
	for(int k = 0; k < zSize; k++)
	{
		array[i][j][k].particlesHere = NULL;
	}

	setOffset(Point3D(0.0f, 0.0f, 0.0f));
	setCacheParams(_interactionRadius, _cubeWidth);
}

PhysParticleCache::~PhysParticleCache()
{
	if(array) free(array);
}

void PhysParticleCache::setCacheParams(float _interactionRadius, float _cubeWidth)
{
	if(_interactionRadius <= 0.0f)
	{
		_interactionRadius = interactionRadius;
	}

	if(_cubeWidth <= 0.0f)
	{
		_cubeWidth = _interactionRadius / 2.0f;
	}

	if(_interactionRadius == interactionRadius && _cubeWidth == cubeWidth && check_list_size > 0)
	{
		return;
	}

	cubeWidth = _cubeWidth;
	interactionRadius = _interactionRadius;
	interactionRadiusSq = interactionRadius * interactionRadius;
	oneOverCubeWidth = 1.0f/cubeWidth;

	recalculateDimensions();
	recalculateBB();

	generateCheckList(interactionRadius, cubeWidth);
}

void PhysParticleCache::generateCheckList(float cutoff_r, float cube_width)
{
	check_list_size = 0;

	int xStep = zSize * ySize;
	int yStep = zSize;
	int zStep = 1;

	float radius = cutoff_r / cube_width;
	int i_radius = round_up(radius);

	float radiusSq = radius * radius;

	float radius2 = radius * 3;
	float radius2Sq = radius2 * radius2;

	int mark_count = 0;

	for(int i = -i_radius; i <= i_radius; i++)
	for(int j = -i_radius; j <= i_radius; j++)
	for(int k = -i_radius; k <= i_radius; k++)
	{
		int mindx = (i>0 ? i-1 : (i<0 ? i+1 : i));
		int mindy = (j>0 ? j-1 : (j<0 ? j+1 : j));
		int mindz = (k>0 ? k-1 : (k<0 ? k+1 : k));
		
		int minDistSq = (mindx*mindx + mindy*mindy + mindz*mindz);

		if(float(minDistSq) <= radiusSq)
		{
			CheckListItem* item = &(check_list[check_list_size++]);

			item->offset = xStep*i + yStep*j + zStep*k;
			item->mask = 0;

			for(int i2 = (i*3)-1; i2 <= (i*3)+1; i2++)
			for(int j2 = (j*3)-1; j2 <= (j*3)+1; j2++)
			for(int k2 = (k*3)-1; k2 <= (k*3)+1; k2++)
			{
				// Note: Because we don't do different insertions based
				// on position within cube, we subtract two here, to make
				// up for the distance uncertainty on insertion.
				int mindx2 = (i2>1 ? i2-2 : (i2<-1 ? i2+2 : 0));
				int mindy2 = (j2>1 ? j2-2 : (j2<-1 ? j2+2 : 0));
				int mindz2 = (k2>1 ? k2-2 : (k2<-1 ? k2+2 : 0));

				int minDistSq2 = (mindx2*mindx2 + mindy2*mindy2 + mindz2*mindz2);

				if(float(minDistSq2) <= radius2Sq)
				{
					int pos = ((i2-((i*3)-1))*9) + ((j2-((j*3)-1))*3) + (k2-((k*3)-1));
					item->mask |= (1 << pos);
					mark_count++;
				}
			}
		}
	}

	//printf("mark_count = %d\n", mark_count);
	//IMP_PRINT("i_radius = %d, check_list_size = %d\n", i_radius, check_list_size);
}

/*
void PhysParticleCache::generate_check_list2(float cutoff_r, float cube_width)
{
	check_list2_size = 0;

	int xStep = zSize * ySize;
	int yStep = zSize;
	int zStep = 1;

	float radius = cutoff_r / cube_width;
	int i_radius = round_up(radius);

	int radiusSq = trunc(radius * radius);

	for(int i = -i_radius; i <= i_radius; i++)
	for(int j = -i_radius; j <= i_radius; j++)
	for(int k = -i_radius; k <= i_radius; k++)
	{
		int dx = (i>0 ? i-1 : (i<0 ? i+1 : i));
		int dy = (j>0 ? j-1 : (j<0 ? j+1 : j));
		int dz = (k>0 ? k-1 : (k<0 ? k+1 : k));
		
		int distSq = (dx*dx + dy*dy + dz*dz);

		if(distSq <= radiusSq)
		{
			check_list2[check_list2_size++] = xStep*i + yStep*j + zStep*k;
		}
	}

	//printf("i_radius = %d, check_list_size = %d\n", i_radius, check_list_size);
}
*/


/*
#define RESCALE(source, muldest) \
__asm \
{ \
	fld dword ptr[source];
	fmul oneOverCubeWidth;
	fadd ha
}

#define PORTIO __asm      \
{                         \
   __asm mov al, 2        \
   __asm mov dx, 0xD007   \
   __asm out al, dx       \
}
*/


void PhysParticleCache::setOffset(Point3D const& _offset)
{
	offset = _offset;
	recalculateBB();
}

void PhysParticleCache::recalculateDimensions()
{
	float fudgeFactor = 0.001f; // Just in case, so we don't go too small on the marginNCubes, and get killed by rounding error
	marginNCubes = round_up(interactionRadius/cubeWidth + fudgeFactor); 
	marginWidth = marginNCubes * cubeWidth;

	// Here again, we use fudgeFactor to shrink the dimensions of the phys tiler a tiny bit. This prevents
	// a particle that is supposed to be inside the inner dimensions of a physics tile from falling slightly outside of it.
	outerDimensions.set(xSize*cubeWidth - fudgeFactor, ySize*cubeWidth - fudgeFactor, zSize*cubeWidth - fudgeFactor);
	innerDimensions.set((xSize-marginNCubes*2)*cubeWidth - fudgeFactor, (ySize-marginNCubes*2)*cubeWidth - fudgeFactor, (zSize-marginNCubes*2)*cubeWidth - fudgeFactor);
}

void PhysParticleCache::recalculateBB()
{
	outerBBMins = offset-(outerDimensions*0.5f);
	outerBBMaxs = offset+(outerDimensions*0.5f);

	innerBBMins = offset-(innerDimensions*0.5f);
	innerBBMaxs = offset+(innerDimensions*0.5f);
}

void PhysParticleCache::beginFrame()
{
}


// nParticles is the max number of particles that might
// be inserted into the tile.
void PhysParticleCache::beginTile(int nParticles)
{
	clear_list_sa.ensureCapacity(nParticles);
	particles_sa.ensureCapacity(nParticles);
	particles_closeby_sa2.ensureCapacity(nParticles);
}


// TODO: I should divide space into 4 blocks instead of three... then I 
// can use bit shifting instead of division here to make this stuff much faster.
void PhysParticleCache::insertParticle(PhysParticle* b)
{
	float fx = (b->center.p[0]-outerBBMins[0]) * oneOverCubeWidth;
	float fy = (b->center.p[1]-outerBBMins[1]) * oneOverCubeWidth;
	float fz = (b->center.p[2]-outerBBMins[2]) * oneOverCubeWidth;

	int x = trunc(fx);
	int y = trunc(fy);
	int z = trunc(fz);

	// TODO: Consider using 64 bit masks.
	int pos = trunc((fx - float(x)) * 3.0f);
	pos *= 3;
	pos += trunc((fy - float(y)) * 3.0f);
	pos *= 3;
	pos += trunc((fz - float(z)) * 3.0f);

	if(pos >= 27)
	{
		Assert(false);
		pos = 26;
	}

	int mask = 1 << pos;

	// Note: The tiler should take care that this assert never fails.
	// If a user uses particle cache directly, this has to be guaranteed.
	Assert(x >= 0 && x < xSize && y >= 0 && y < ySize && z >= 0 && z < zSize);
	/*
	if(!(x >= 0 && x < xSize && y >= 0 && y < ySize && z >= 0 && z < zSize))
	{
		return;
	}
	*/

	// Get the entry from the array
	ArrayItem* arrayItem = &ARRAY(x, y, z);

	if(arrayItem->particlesHere == NULL)
	{
		// Get or allocate new index
		clear_list_sa.push(arrayItem);
	}

	// Get or allocate new node
	PhysParticleListNode* newNode = &(particles_sa.push()); // Can't do pushAutoSize because we'll be storing a point to newNode (which would get messed up if the array were resized)

	newNode->particle = b;
	newNode->mask = mask;
	newNode->next = arrayItem->particlesHere;
	arrayItem->particlesHere = newNode;

	insertCount ++;
}


// TODO: All of this can be made even more faster if
// within the calc_normal loop, I check bit masks...
// Then, I can have bit masks that allow me to get a much finer resolution.
// However, i would have to calculate a bit mask each time I called this function.

// TODO: I can make this code faster by making truncation the default
// rounding mode... that will save three adds per call.

// TODO: Particles with a small number of particles don't need to use the particle cache...
// you can just always return all the particles in that particle.
void PhysParticleCache::calcNeighbors(PhysParticle* p, PhysParticle*** neighbors, int* size)
{
	// TODO: This should use round, not trunc for faster performance....
	// Also, I should actually just hand-code this in assembly because
	// the pocessor stupidly writes back to memory and loads again...
	/*
	Point3D transformedCenter = (p->center - BBMins) * oneOverCubeWidth;

	int x = trunc(transformedCenter[0]);
	int y = trunc(transformedCenter[1]);
	int z = trunc(transformedCenter[2]);
	*/
	int x = trunc((p->center[0] - outerBBMins[0]) * oneOverCubeWidth);
	int y = trunc((p->center[1] - outerBBMins[1]) * oneOverCubeWidth);
	int z = trunc((p->center[2] - outerBBMins[2]) * oneOverCubeWidth);
	
	/*
	if(!(x >= marginNCubes && x < xSize-marginNCubes &&
		y >= marginNCubes && y < ySize-marginNCubes &&
		z >= marginNCubes && z < zSize-marginNCubes))
	{
		return;
	}
	*/

	Assert(	x >= marginNCubes && x < xSize-marginNCubes &&
			y >= marginNCubes && y < ySize-marginNCubes &&
			z >= marginNCubes && z < zSize-marginNCubes);

	ArrayItem* centerItem = &ARRAY(x, y, z);

	//int old_size = particles_closeby_sa.size;
	//p->neighbor_list = particles_closeby_sa.size;
	particles_closeby_sa2.size = 0;

	for(int i = 0; i < check_list_size; i++)
	{
		// Get the entry from the array
		CheckListItem* clitem = &(check_list[i]);

		PhysParticleListNode* node = (centerItem + clitem->offset)->particlesHere;

		while(node != NULL)
		{
			if(node->mask & clitem->mask)
			{
				PhysParticle* b = node->particle;

				// Get or allocate new node
				//particles_closeby[particles_closeby_size++] = b;
				/*
				float distSq = p->center.lengthSq(b->center);
				if(distSq <= interactionRadiusSq)
				{
					PhysParticleAndDist* pd = &(particles_closeby_sa.pushAutoSize()); // This should be resized once per lookup (I don't think it can be, can it? Unless we always make sure it can store (Particles more particles)
					pd->particle = b;
					pd->distSq = distSq;
				}
				*/
				particles_closeby_sa2.push(b);
			}

			node = node->next;
		}
	}

	//particles_closeby[particles_closeby_size++] = NULL;
	/*
	PhysParticleAndDist* pd = &(particles_closeby_sa.pushAutoSize());
	pd->particle = NULL;
	pd->distSq = 0.0f;
	*/
	*neighbors = particles_closeby_sa2.a;
	*size = particles_closeby_sa2.size;
}


void PhysParticleCache::endTile()
{
	for(int i = 0; i < clear_list_sa.size; i++)
	{
		clear_list_sa[i]->particlesHere = NULL;
	}

	clear_list_sa.size = 0;
	particles_sa.size = 0;

	clearCount++;

	//printf("InsertCount=%d(%f), GetCount=%d(%f), SearchCount=%d(%f)\n", insertCount, float(insertCount)/float(clearCount), getCount, float(getCount)/float(clearCount), searchCount, float(searchCount)/float(clearCount));
	//printf("ParticlesCloseby Size = %d, ClearParticlesCloseby = %d, ParticlesPerCube=%f\n", particlesCloseby.size, clearParticlesClosebyList.size, float(particlesCloseby.size) / float(clearParticlesClosebyList.size));
}

void PhysParticleCache::endFrame()
{
	//particles_closeby_sa.size = 0;
}





// TODO: Move this into PhysParticleCacheFactory.cpp?
// TODO: I can make an abstract factory class that does all this for any object
PhysParticleCacheFactory* PhysParticleCacheFactory::factory = new PhysParticleCacheFactory();

PhysParticleCacheFactory::PhysParticleCacheFactory()
{
}

// TODO: Make this thread-safe (in case multiple threads are simultaneously using a tiler)
PhysParticleCache* PhysParticleCacheFactory::getCache()
{
	if(m_caches.size > 0)
	{
		return m_caches.pop();
	}
	else
	{
		return new PhysParticleCache();
	}
}

// TODO: Make this thread-safe (in case multiple threads are simultaneously using a tiler)
void PhysParticleCacheFactory::returnCache(PhysParticleCache* cache)
{
	m_caches.pushAutoSize(cache);
}