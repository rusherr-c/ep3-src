//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include "blobulator/util/fasttrunc.h"
#include "blobulator/implicit/volumetricparticlecache.h"

#include "memdbgon.h"

// NOTE: These are copied straight from ImpRenderer.
// might be better to define them in ImpDefines.h
// These sizes are how many cubes ImpRenderer can draw
// in any direction. The dimensions of the cache actually
// have to be larger by one in each direction in order to
// fit the samples on the far side of the last cube. These
// dimensions are stored as xsizeP1, ysizeP1 and zsizeP1.

const int xsize = RENDERER_CACHE_XSIZE;
const int ysize = RENDERER_CACHE_YSIZE;
const int zsize = RENDERER_CACHE_ZSIZE;
const int xsizeP1 = xsize+1;
const int ysizeP1 = ysize+1;
const int zsizeP1 = zsize+1;

// 0x8000, 0x7FFF
#define MIN_SHORT -32767
#define MAX_SHORT 32767


// Note: topSentinel is necessary because
// when the list is empty, we still want the
// checks in calc_normal to pass. These tests
// always look at the center_x of the next item.
VolumetricParticleCache::VolumetricParticleCache()
: curCacheElement(0)
#if (IMP_PROFILE == 1)
	, n_tiles(0)
	, memory_total(0)
#endif
{
	//cachePlane = (PCachePlane_t)calloc(ysizeP1*zsizeP1, sizeof(PCachePlaneElem_t));
	cachePlane = (PCachePlane_t)malloc(ysizeP1*zsizeP1*sizeof(PCachePlaneElem_t));

	botSentinel = new PCacheElem_t;
	midSentinel = new PCacheElem_t;
	topSentinel = new PCacheElem_t;

	botSentinel->center_x = MIN_SHORT;
	botSentinel->bot_x = 0;
	botSentinel->top_x = 0;
	botSentinel->prev = NULL;
	botSentinel->next = midSentinel;
	botSentinel->p = NULL;
	#if (CALC_DY_SQ_PLUS_DZ_SQ==1)
		botSentinel->dySqPlusDzSq = 0.0f;
	#endif

	midSentinel->center_x = MAX_SHORT;
	midSentinel->bot_x = 0;
	midSentinel->top_x = 0;
	midSentinel->prev = botSentinel;
	midSentinel->next = topSentinel;
	midSentinel->p = NULL;
	#if (CALC_DY_SQ_PLUS_DZ_SQ==1)
		midSentinel->dySqPlusDzSq = 0.0f;
	#endif

	topSentinel->center_x = MAX_SHORT;
	topSentinel->bot_x = 0;
	topSentinel->top_x = 0;
	topSentinel->prev = midSentinel;
	topSentinel->next = NULL;
	topSentinel->p = NULL;
	#if (CALC_DY_SQ_PLUS_DZ_SQ==1)
		topSentinel->dySqPlusDzSq = 0.0f;
	#endif

	for(int i = 0; i < ysizeP1; i++)
	for(int j = 0; j < zsizeP1; j++)
	{
		cachePlane[i][j] = midSentinel;
	}
}

VolumetricParticleCache::~VolumetricParticleCache()
{
	free(cachePlane);
}

void VolumetricParticleCache::beginTile()
{

}

void VolumetricParticleCache::addParticle(ImpParticle* p)
{
	particles.pushAutoSize(p);
}

class CSorter
{
public:
	static bool gt(ImpParticle* a, ImpParticle* b)
	{
		return a->center[0] < b->center[0];
	}
};

void VolumetricParticleCache::buildCache(float cw, float fieldRadius, Point3D& mins, Point3D& maxs)
{
	CSorter sorter;
	particles.sort(sorter); // from biggest x to smallest x
	if(!particles.testSort(sorter)) {IMP_PRINT("Error: Sort failed\n");};

	for(int i = 0; i < particles.size; i++)
	{
		insertIntoCache(particles[i], cw, fieldRadius, mins, maxs);
	}
}

void VolumetricParticleCache::endTile()
{
	#if (IMP_PROFILE == 1)
		memory_total += (curCacheElement * (sizeof(PCacheElem_t) + sizeof(PCacheElem_t*)));
		memory_total += (clearList.size * sizeof(pcache_YZ_t));
		memory_total += sizeof(PCachePlane_t);

		n_tiles++;
		if(n_tiles == 100)
		{
			IMP_PRINT("VolumetricParticleCache avg max memory usage = %f\n", float(memory_total) / (100.0f * 1000000.0f));
			n_tiles = 0;
			memory_total = 0;
		}
	#endif


	particles.size = 0;
	curCacheElement = 0;

	for(int i = 0; i < clearList.size; i++)
	{
		pcache_YZ_t e = clearList[i];
		cachePlane[e.y][e.z] = midSentinel;
	}
	clearList.size = 0;
}

FORCEINLINE float sqr(float x)
{
	return x*x;
}

// TODO: Make inline
// NOTE: Must make sure that the ranges of x's generated
// falls within limits of cube_index_t
void VolumetricParticleCache::insertIntoCache(ImpParticle* p, float cw, float fieldRadius, Point3D& mins, Point3D& maxs)
{
	// TODO: use oneOverCw instead of dividing by cw
	Point3D c = (p->center - mins) / cw;
	float fr = (fieldRadius * p->scale) / cw; // field radius in terms of cubewidths
	float frSq = fr * fr;
	#if (CALC_DY_SQ_PLUS_DZ_SQ==1)
		float cwSq = cw * cw;
	#endif

	// Make sure there is enough elements in the cache to fit all the elements of the particle
	// TODO: This might be slower than just doing it in the inner loop the way it was done before. Should check.
	int maxNeededElements = round_up((fr*2.0f)+1.0f); // I don't think the +1 is necessary
	maxNeededElements *= maxNeededElements;
	int freeElements = cacheElements.size - curCacheElement; // calculate how many free elements there are
	if(freeElements < maxNeededElements)
	{
		int dif = maxNeededElements - freeElements;
		for(int i = 0; i < dif; i++ )
		cacheElements.pushAutoSize(new PCacheElem_t);
	}	

	// NOTE: The 0+1 and size-1 max's and min's force the cache
	// to essentially have no entries at the edges. This forces
	// the field function to be 0 there which in turn forces the
	// renderer to go around the edges rendering all surfaces.
	// It also allows us to not check in the renderer that we've
	// run past the edge of a cube because there is no way a surface
	// can continue outside the edge of a cube with this scheme.
	//int min_x = max(round_up(c[0] - fr), 0+1);
	//int max_x = min(round_down(c[0] + fr), xsize-1);

	int min_y = MAX(round_up(c[1] - fr), 0+1);
	int max_y = MIN(round_down(c[1] + fr), ysize-1);

	int min_z = MAX(round_up(c[2] - fr), 0+1);
	int max_z = MIN(round_down(c[2] + fr), zsize-1);

	// Note: The AVOID_SQRT code has a subtle bug in it.
	// I can tell because when I seed, I can sometimes get
	// a field value of 0 at the first seed point where z==0.
	// This never happens with the regular sqrt method.
	#define AVOID_SQRT 0

	#if (AVOID_SQRT == 1)
		int initial_min_x = round_up(c[0]);
		int initial_max_x = round_down(c[0]);
	#endif


	for(int i = min_y; i <= max_y; i++)
	{
		float dy = c[1] - float(i);
		float dySq = dy*dy;

		#if (AVOID_SQRT == 1)
			int min_x = initial_min_x;
			int max_x = initial_max_x;
		#endif

		for(int j = min_z; j <= max_z; j++)
		{
			float dz = c[2] - float(j);
			float dySqPlusDzSq = (dz*dz) + dySq;
			float slack = frSq - dySqPlusDzSq;

			if(slack > 0.0f)
			{
				#if (AVOID_SQRT == 1)
					if(float(j) <= c[2])
					{
						while(sqr(float(min_x-1) - c[0]) <= slack) min_x --;
						while(sqr(float(max_x+1) - c[0]) <= slack) max_x ++;
					}
					else
					{
						while(min_x < initial_min_x && sqr(float(min_x) - c[0]) > slack) min_x ++;
						while(max_x > initial_max_x && sqr(float(max_x) - c[0]) > slack) max_x --;
					}
				#else
					float dx = sqrtf(frSq - dySqPlusDzSq); // calc minx and maxx
					int min_x = round_up(c[0] - dx);
					int max_x = round_down(c[0] + dx);
				#endif

				// Force min of min_x to be 1 so that at x=0, no particles can have an influence, and field is forced to equal 0.
				// Same for the max of max_x. This is the same as we do for min_y, max_y, min_z and max_z.
				min_x = MAX(min_x, 1);
				max_x = MIN(max_x, xsize-1);

				if(min_x <= max_x) // can be not true if we fall betw samples
				{
					// not sure these are correct
					int mid_bot_x = round_down(c[0]);
					//int mid_top_x = mid_bot_x + 1; // not sure if correct!
					//Assert(mid_bot_x >= MIN_CUBE_INDEX && mid_top_x <= MAX_CUBE_INDEX);
					Assert(mid_bot_x >= MIN_CUBE_INDEX && mid_bot_x+1 <= MAX_CUBE_INDEX);
					#if (CALC_DY_SQ_PLUS_DZ_SQ==1)
						float realDySqPlusDzSq = dySqPlusDzSq * cwSq;
					#endif

					Assert(i >= 0 && j >= 0 && i < ysizeP1 && j < zsizeP1);
					PCachePlaneElem_t* planeElem = &(cachePlane[i][j]);

					/* This is not needed now because we do the calculation above.
					if(curCacheElement >= cacheElements.size)
					{
						cacheElements.pushAutoSize(new PCacheElem_t());
					}
					*/
					PCacheElem_t* elem = cacheElements[curCacheElement++];
					elem->center_x = mid_bot_x;
					elem->bot_x = (cube_index_t)min_x;
					elem->top_x = (cube_index_t)max_x;
					//elem->mid_top_x = (cube_index_t)mid_top_x;
					//elem->mid_bot_x = (cube_index_t)mid_bot_x;
					
					elem->prev = botSentinel;
					elem->next = *planeElem;
					elem->p = p;
					#if (CALC_DY_SQ_PLUS_DZ_SQ==1)
						elem->dySqPlusDzSq = realDySqPlusDzSq;
					#endif

					if(*planeElem != midSentinel)
					{
						(*planeElem)->prev = elem;
					}
					else
					{
						pcache_YZ_t e = {i,j};
						clearList.pushAutoSize(e);
					}
					*planeElem = elem;
				}
			}
		}
	}
}
