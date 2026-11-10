//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "../smartarray.h"

class PhysParticle;
class PhysTiler;
class PhysTileIterator;

#if(COLOR_PHYS_TILES==1)
	extern Point3D phys_tile_debug_color;
#endif

struct TileParticle
{
	PhysParticle* p;
	bool in_margin;
};

class PhysTile
{
public:
	int x, y, z;
	Point3D center;
	#if(COLOR_PHYS_TILES==1)
		Point3D debug_color;
	#endif
	float dist;

	SmartArray<TileParticle> tparticles;

	PhysParticleCache* m_pParticleCache;
	int n;
	void beginIteration();
	PhysParticle* getNextParticleAndNeighbors(PhysParticle*** neighbors, int* count);
	void endIteration();

	PhysTile(PhysParticleCache* _cache, int _x, int _y, int _z) : m_pParticleCache(_cache), x(_x), y(_y), z(_z), n(0) {};
};

class PhysTiler
{
friend class PhysTileIterator;

public:
	PhysTiler(PhysParticleCache* cache);
	PhysTiler(float _interactionRadius, float _cubeWidth = 0.0f);
	~PhysTiler(void);

	void setCacheParams(float interactionRadius, float cubeWidth = 0.0f);
	void setInteractionRadius(float interactionRadius) { setCacheParams(interactionRadius); }
	void setCubeWidth(float cubeWidth) { setCacheParams(-1.0f, cubeWidth); }
	void beginFrame(Point3D& _offset);
	void insertParticle(PhysParticle* p);
	//void processTiles();
	PhysTileIterator* acquireIterator();
	void releaseIterator(PhysTileIterator* iterator);
	void endFrame();

	void beginIteration();
	PhysParticle* getNextParticleAndNeighbors(PhysParticle*** neighbors, int* count);
	void endIteration();

	// NOTE: This should not be accessible here. Instead, I should have
	// a function that returns an iterator to a particle's neighbors.
	//PhysParticleCache* getParticleCache() { return m_pParticleCache; };

private:
	PhysParticleCache* m_pParticleCache;

	SmartArray<PhysTile*> tiles;
	SmartArray<PhysTile*> tile_cache;
	PhysTileIterator* iterator;
	bool iterator_active;

	PhysTile* iteration_tile;
	int iteration_tile_num;
	int iteration_particle_count;
	int iteration_particle_num;
	
	Point3D offset;
	Point3D outer_cache_dim;
	Point3D render_dim; // TODO: Rename to inner_cache_dim
	float render_margin; // TODO: Rename to cache_margin
	Point3D corner_offset;

	Point3D calcTileCorner(int x, int y, int z);
	Point3D calcTileOffset(int x, int y, int z);

	//void addParticleToTile(PhysParticle*p, int x, int y, int z);
	void processTile(PhysTile* tile);
	void processTile(int x, int y, int z);

	PhysTile* findTile(int x, int y, int z);
	PhysTile* createTile(int x, int y, int z);
};

class PhysTileIterator
{
	friend class PhysTiler;

private:
	PhysTileIterator(PhysTiler* _tiler)
		: n(0)
		, tiler(_tiler)
	{
	}

public:
	PhysTile* getNextTile()
	{
		if(n < tiler->tiles.size)
		{
			return tiler->tiles[n++];
		}
		else
		{
			return NULL;
		}
	}

private:
	int n;
	PhysTiler* tiler;

	void reset() { n = 0; }
};


// Algorithm... calculate how many cubes have to be overlapped in order to not miss particles.
// should be roundup(render_radius/cubewidth);
// Next, can calculate how many cubes are left and what the width/height/depth of a tile is (dx, dy, dz).
// Insert particles into the appropriate tile is easy.
// Rendering... change render offsets so that we're rendering at center of tile. just offset each particle so that it's at the center of the tile.



// TODO: Move this into PhysTilerFactory.h?
class PhysTilerFactory
{
public:
	static PhysTilerFactory* factory;

	PhysTiler* getTiler();
	void returnTiler(PhysTiler* tiler);

private:
	PhysTilerFactory();
	SmartArray<PhysTiler*> m_tilers;
};
