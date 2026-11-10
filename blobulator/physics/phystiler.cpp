//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include "blobulator/point3d.h"
#include "blobulator/physics/physparticle.h"
#include "blobulator/physics/physparticlecache.h"
#include "blobulator/physics/phystiler.h"
#include "blobulator/util/fasttrunc.h"
#include "../util/rand.h"

#if (COLOR_PHYS_TILES==1)
	Point3D phys_tile_debug_color = Point3D();
#endif



void PhysTile::beginIteration()
{
	#if(COLOR_PHYS_TILES==1)
		phys_tile_debug_color = debug_color;
	#endif

	Assert(n == 0);

	m_pParticleCache->setOffset(center); // must set offset first so we can do bounds checking in the loop
	m_pParticleCache->beginTile(tparticles.size);
}

PhysParticle* PhysTile::getNextParticleAndNeighbors(PhysParticle*** neighbors, int* count)
{
	while(n < tparticles.size)
	{
		TileParticle* tparticle = &(tparticles[n++]);
		if(tparticle->in_margin)
		{
			m_pParticleCache->insertParticle(tparticle->p);
		}
		else
		{
			m_pParticleCache->calcNeighbors(tparticle->p, neighbors, count);
			m_pParticleCache->insertParticle(tparticle->p);
			return tparticle->p;
		}
	}

	return NULL;

#if(COLOR_PHYS_TILES==1)
	p->interpolants1 = phys_tile_debug_color;
#endif
}

void PhysTile::endIteration()
{
	Assert(n == tparticles.size);
	n = 0;

	m_pParticleCache->endTile();
}




PhysTiler::PhysTiler(PhysParticleCache* cache)
: iteration_tile(NULL)
, iteration_tile_num(0)
, iteration_particle_count(0)
, iteration_particle_num(0)
{
	m_pParticleCache = cache;

	iterator = new PhysTileIterator(this);
	iterator_active = false;
}

PhysTiler::PhysTiler(float _interactionRadius, float _cubeWidth)
: iteration_tile(NULL)
, iteration_tile_num(0)
, iteration_particle_count(0)
, iteration_particle_num(0)
{
	m_pParticleCache = new PhysParticleCache(_interactionRadius, _cubeWidth);

	iterator = new PhysTileIterator(this);
	iterator_active = false;
}

PhysTiler::~PhysTiler(void)
{
	for(int i = 0; i < tiles.size; i++)
	{
		delete tiles[i];
	}
	delete m_pParticleCache;
	delete iterator;
}

void PhysTiler::setCacheParams(float interactionRadius, float cubeWidth)
{
	m_pParticleCache->setCacheParams(interactionRadius, cubeWidth);
}

void PhysTiler::beginFrame(Point3D& _offset)
{
	offset = _offset;
	outer_cache_dim = m_pParticleCache->getOuterDimensions();
	render_dim = m_pParticleCache->getInnerDimensions();
	render_margin = m_pParticleCache->getMarginWidth();
	corner_offset = offset - (render_dim*0.5f);

	m_pParticleCache->beginFrame();

	Assert(tiles.size == 0);
}

void PhysTiler::insertParticle(PhysParticle* p)
{
	Point3D voxel = ((p->center - corner_offset) / render_dim); // TODO: should keep 1/render_dim
	int x = round_down(voxel[0]);
	int y = round_down(voxel[1]);
	int z = round_down(voxel[2]);

	// Add particle to any neighboring overlapping tiles
	Point3D tileMins = calcTileCorner(x, y, z);
	Point3D tileMaxs = tileMins + render_dim;

	int x_start, x_end;
	if(p->center[0] <= tileMins[0]+render_margin) {x_start = -1; x_end = 0;}
	else if(p->center[0] >= tileMaxs[0]-render_margin) {x_start = 0; x_end = 1;}
	else {x_start = 0; x_end = 0;}

	int y_start, y_end;
	if(p->center[1] <= tileMins[1]+render_margin) {y_start = -1; y_end = 0;}
	else if(p->center[1] >= tileMaxs[1]-render_margin) {y_start = 0; y_end = 1;}
	else {y_start = 0; y_end = 0;}

	int z_start, z_end;
	if(p->center[2] <= tileMins[2]+render_margin) {z_start = -1; z_end = 0;}
	else if(p->center[2] >= tileMaxs[2]-render_margin) {z_start = 0; z_end = 1;}
	else {z_start = 0; z_end = 0;}

	// TODO: I should replace this triple for loop with a lookup
	// based on the 6 booleans calculated above.
	for(int dx=x_start; dx <= x_end; dx++)
	{
		for(int dy=y_start; dy <= y_end; dy++)
		{
			for(int dz=z_start; dz <= z_end; dz++)
			{
				PhysTile* tile = createTile(x+dx, y+dy, z+dz);
				TileParticle* tparticle = &(tile->tparticles.pushAutoSize());
				tparticle->p = p;
				tparticle->in_margin = (dx != 0 || dy != 0 || dz != 0); // TODO: Try doing this with bitwise artithmetic

				#if 0
				if(dx == 0 && dy == 0 && dz == 0)
				{

					Assert((p->center[0] >= tile->center[0] - render_dim[0]*0.5f) && (p->center[0] <= tile->center[0] + render_dim[0]*0.5f));
					Assert((p->center[1] >= tile->center[1] - render_dim[1]*0.5f) && (p->center[1] <= tile->center[1] + render_dim[1]*0.5f));
					Assert((p->center[2] >= tile->center[2] - render_dim[2]*0.5f) && (p->center[2] <= tile->center[2] + render_dim[2]*0.5f));
				}
				else
				{
					Assert((p->center[0] >= tile->center[0] - outer_cache_dim[0]*0.5f) && (p->center[0] <= tile->center[0] + outer_cache_dim[0]*0.5f));
					Assert((p->center[1] >= tile->center[1] - outer_cache_dim[1]*0.5f) && (p->center[1] <= tile->center[1] + outer_cache_dim[1]*0.5f));
					Assert((p->center[2] >= tile->center[2] - outer_cache_dim[2]*0.5f) && (p->center[2] <= tile->center[2] + outer_cache_dim[2]*0.5f));
				}
				#endif
			}
		}
	}


	/*
	bool x_tiles[3] = {p->center[0] <= tileMins[0]+render_margin, true, p->center[0] >= tileMaxs[0]-render_margin};
	bool y_tiles[3] = {p->center[1] <= tileMins[1]+render_margin, true, p->center[1] >= tileMaxs[1]-render_margin};
	bool z_tiles[3] = {p->center[2] <= tileMins[2]+render_margin, true, p->center[2] >= tileMaxs[2]-render_margin};

	// TODO: I should replace this triple for loop with a lookup
	// based on the 6 booleans calculated above.
	for(int dx=-1; dx <= 1; dx++)
	{
		if(x_tiles[dx+1])
		{
			for(int dy=-1; dy <= 1; dy++)
			{
				if(y_tiles[dy+1])
				{
					for(int dz=-1; dz <= 1; dz++)
					{
						if(z_tiles[dz+1])
						{
							if(dx == 0 && dy == 0 && dz == 0)
							{
								PhysTile* tile = createTile(x, y, z);
								tile->particles.pushAutoSize(p);
							}
							else
							{
								PhysTile* tile = createTile(x, y, z);
								tile->margin_particles.pushAutoSize(p);
							}
						}
					}
				}
			}
		}
	}
	*/
}

/*
void PhysTiler::addParticleToTile(PhysParticle*p, int x, int y, int z)
{
	PhysTile* tile = createTile(x, y, z);
	tile->particles.pushAutoSize(p);
}
*/

Point3D PhysTiler::calcTileCorner(int x, int y, int z)
{
	Point3D tile_corner = corner_offset;
	tile_corner[0] += render_dim[0]*x;
	tile_corner[1] += render_dim[1]*y;
	tile_corner[2] += render_dim[2]*z;
	return tile_corner;
}

// Calculate the centerpoint of a tile
Point3D PhysTiler::calcTileOffset(int x, int y, int z)
{
	Point3D tile_offset = offset;
	tile_offset[0] += render_dim[0]*x;
	tile_offset[1] += render_dim[1]*y;
	tile_offset[2] += render_dim[2]*z;
	return tile_offset;
}

/*
void PhysTiler::processTile(PhysTile* tile)
{
	#if(COLOR_PHYS_TILES==1)
		phys_tile_debug_color = tile->debug_color;
	#endif

	m_pParticleCache->setOffset(tile->center); // must set offset first so we can do bounds checking in the loop
	m_pParticleCache->beginTile(tile->tparticles.size);

	for(int i = 0; i < tile->tparticles.size; i++)
	{
		PhysParticle *p = tile->tparticles[i].p;
		m_pParticleCache->insertParticle(p);
	}
	for(int i = 0; i < tile->tparticles.size; i++)
	{
		TileParticle* tparticle = &(tile->tparticles[i]);
		if(tparticle->in_margin == false)
		{
			PhysParticle *p = tile->tparticles[i].p;
			m_pParticleCache->calcNeighbors(p);
#if(COLOR_PHYS_TILES==1)
			p->interpolants1 = phys_tile_debug_color;
#endif
		}
	}

	m_pParticleCache->endTile();
}

void PhysTiler::processTile(int x, int y, int z)
{
	PhysTile* tile = findTile(x, y, z);
	if(tile != NULL)
	{
		processTile(tile);
	}
}
*/

PhysTileIterator* PhysTiler::acquireIterator()
{
	Assert(iterator_active == false);
	
	if(iterator_active == false)
	{
		iterator->reset();
		iterator_active = true;

		return iterator;
	}
	else
	{
		Assert(false);
		return NULL;
	}
}

void PhysTiler::releaseIterator(PhysTileIterator* _iterator)
{
	Assert(_iterator == iterator && iterator_active == true);

	iterator_active = false;
}


void PhysTiler::beginIteration()
{
#if(COLOR_PHYS_TILES==1)
	phys_tile_debug_color = debug_color;
#endif

	Assert(iteration_tile == NULL);
	Assert(iteration_tile_num == 0);
	Assert(iteration_particle_count == 0);
	Assert(iteration_particle_num == 0);

	if(tiles.size > 0)
	{
		iteration_tile = tiles[0];
		iteration_tile_num = 1;
		iteration_particle_count = iteration_tile->tparticles.size;
		iteration_particle_num = 0;

		m_pParticleCache->setOffset(iteration_tile->center); // must set offset first so we can do bounds checking in the loop
		m_pParticleCache->beginTile(iteration_particle_count);
	}
	else
	{
		iteration_tile = NULL;
		iteration_tile_num = 0;
		iteration_particle_count = 0;
		iteration_particle_num = 0;
	}
}

PhysParticle* PhysTiler::getNextParticleAndNeighbors(PhysParticle*** neighbors, int* count)
{
	#if(COLOR_PHYS_TILES==1)
		p->interpolants1 = phys_tile_debug_color;
	#endif

	while(true)
	{
		while(iteration_particle_num < iteration_particle_count)
		{
			TileParticle* tparticle = &(iteration_tile->tparticles[iteration_particle_num++]);
			if(tparticle->in_margin)
			{
				m_pParticleCache->insertParticle(tparticle->p);
			}
			else
			{
				m_pParticleCache->calcNeighbors(tparticle->p, neighbors, count);
				m_pParticleCache->insertParticle(tparticle->p);
				return tparticle->p;
			}
		}

		if(iteration_tile_num < tiles.size)
		{
			m_pParticleCache->endTile();

			iteration_tile = tiles[iteration_tile_num];
			iteration_tile_num++;
			iteration_particle_count = iteration_tile->tparticles.size;
			iteration_particle_num = 0;

			m_pParticleCache->setOffset(iteration_tile->center); // must set offset first so we can do bounds checking in the loop
			m_pParticleCache->beginTile(iteration_particle_count);
		}
		else
		{
			return NULL;
		}
	}
}

void PhysTiler::endIteration()
{
	// Make sure we've iterated through every particle in every tile.
	Assert(iteration_tile_num == tiles.size);
	Assert(iteration_particle_num == iteration_particle_count);

	if(iteration_tile != NULL)
	{
		m_pParticleCache->endTile();
	}
	
	iteration_tile = NULL;
	iteration_tile_num = 0;
	iteration_particle_count = 0;
	iteration_particle_num = 0;
}


/*
void PhysTiler::processTiles()
{
	for(int i = 0; i < tiles.size; i++)
	{
		processTile(tiles[i]);
	}
}
*/

void PhysTiler::endFrame()
{
	//ConMsg("%d\n", tiles.size);
	#if(COLOR_PHYS_TILES==1)
		// This is to preserve the coloring of tiles between frames
		for(int i = 0; i < tiles.size; i++)
		{
			PhysTile* tile = tiles[i];
			if(tile->particles.size + tile->margin_particles.size > 0)
			{
				tile->particles.size = 0;
				tile->margin_particles.size = 0;
			}
			else
			{
				tile->tparticles.size = 0;
				tiles.remove(i);
				i--;
				tile_cache.pushAutoSize(tile);								
			}
		}
	#else
		// TOOD: It might be a good idea to use some sort of linked list to share
		// particles between tiles. Otherwise, a lot of memory could be wasted by
		// the Tiles' particle arrays.
		while(tiles.size > 0)
		{
			PhysTile* tile = tiles.pop();
			tile->tparticles.size = 0;
			tile_cache.pushAutoSize(tile);
		}
	#endif
	//Assert(tiles.size == 0);
	m_pParticleCache->endFrame();
}

PhysTile* PhysTiler::findTile(int x, int y, int z)
{
	for(int i = 0; i < tiles.size; i++)
	{
		PhysTile* tile = tiles[i];
		if(tile->x == x && tile->y == y && tile->z == z)
		{
			//Assert(tile->center == calcTileOffset(x,y,z));
			return tile;
		}
	}
	return NULL;
}

PhysTile* PhysTiler::createTile(int x, int y, int z)
{
	PhysTile* tile = findTile(x,y,z);
	if(tile == NULL)
	{
		if(tile_cache.size == 0)
		{
			tile = new PhysTile(m_pParticleCache, x, y, z);
		}
		else
		{
			tile = tile_cache.pop();
			tile->x = x;
			tile->y = y;
			tile->z = z;
			tile->n = 0;
			tile->tparticles.size = 0;
		}

		tile->center = calcTileOffset(x, y, z);
		tile->dist = 0.0f;
		#if(COLOR_PHYS_TILES==1)
			tile->debug_color.set(frand(),frand(),frand());
		#endif

		tiles.pushAutoSize(tile);
	}
	#if(COLOR_PHYS_TILES==1)
	else // These should be recomputed as they can change between frames.
	{
		tile->center = calcTileOffset(x, y, z);
		tile->dist = 0.0f;
	}
	#endif

	//Assert(tile->center == calcTileOffset(x,y,z));

	return tile;
}








// TODO: Move this into PhysTilerFactory.cpp?
// TODO: I can make an abstract factory class that does all this for any object
PhysTilerFactory* PhysTilerFactory::factory = new PhysTilerFactory();

PhysTilerFactory::PhysTilerFactory()
{
}

// TODO: Make this thread-safe (in case multiple threads are simultaneously using a tiler)
PhysTiler* PhysTilerFactory::getTiler()
{
	if(m_tilers.size > 0)
	{
		return m_tilers.pop();
	}
	else
	{
		return new PhysTiler(PhysParticleCacheFactory::factory->getCache());
	}
}

// TODO: Make this thread-safe (in case multiple threads are simultaneously using a tiler)
void PhysTilerFactory::returnTiler(PhysTiler* tiler)
{
	// TODO: When a tiler is returned, ideally, I should take out the cache,
	// and return that too.
	m_tilers.pushAutoSize(tiler);
}