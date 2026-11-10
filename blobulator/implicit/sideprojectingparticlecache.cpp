//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include "blobulator/util/fasttrunc.h"
#include "blobulator/implicit/sideprojectingparticlecache.h"

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
SideProjectingParticleCache::SideProjectingParticleCache()
: curCacheParticle(0)
 //curCacheElement(0)
, n_tiles(0)
, memory_total(0)
, max_alloced_arrays(0)
, max_alloced_cache_elems(0)
{
	//cachePlane = (PCachePlane_t)calloc(ysizeP1*zsizeP1, sizeof(PCachePlaneElem_t));
	//cachePlane = (PCachePlane_t)malloc(xsizeP1*zsizeP1*sizeof(PCachePlaneElem_t));



	for(int i = 0; i < xsizeP1; i++)
	{
		slices[i].sliceData = NULL;
	}

	/*
	for(int i = 0; i < xsizeP1; i++)
	for(int j = 0; j < zsizeP1; j++)
	{
		cachePlane[i][j] = midSentinel;
	}
	*/
}

SideProjectingParticleCache::~SideProjectingParticleCache()
{
	//free(cachePlane);
}

void SideProjectingParticleCache::beginTile()
{

}

void SideProjectingParticleCache::addParticle(ImpParticle* p)
{
	particles.pushAutoSize(p);
}

class CSorter
{
public:
	static bool gt(ImpParticle* a, ImpParticle* b)
	{
		return a->center[1] < b->center[1]; // Sort by y coordinate
	}
};

void SideProjectingParticleCache::buildCache(float cw, float fieldRadius, Point3D& mins, Point3D& maxs)
{
	CSorter sorter;
	particles.sort(sorter); // from biggest x to smallest x
	if(!particles.testSort(sorter)) {IMP_PRINT("Error: Sort failed\n");};

	for(int i = 0; i < particles.size; i++)
	{
		insertIntoCache(particles[i], cw, fieldRadius, mins, maxs);
	}
}

void SideProjectingParticleCache::endTile()
{
	///////////////////////////////////////////
	// Clear slices in clearable slices array
	while(clearableSlices.size > 0)
	{
		PCacheSlice_t* old_slice = &(slices[clearableSlices.pop()]);
		Assert(old_slice->sliceData == NULL);
		clearSlicesData(old_slice);
	}
	
	//////////////////////////////////////
	// Calculate memory totals
	memory_total += (curCacheParticle * (sizeof(PCacheParticle_t) + sizeof(PCacheParticle_t*)));
	memory_total += (unusedSliceData.size * sizeof(PSliceData_t));
	for(int i = 0; i < unusedSliceData.size; i++)
	{
		PSliceData_t* sliceData = unusedSliceData[i];
		memory_total += (sliceData->max_cache_elements_size * sizeof(PCacheElem_t));
		sliceData->max_cache_elements_size = 0;
	}

	n_tiles++;
	if(n_tiles == 100)
	{
		IMP_PRINT("SideProjectingParticleCache avg max memory usage = %f\n", float(memory_total) / (100.0f * 1000000.0f));
		n_tiles = 0;
		memory_total = 0;
	}

	//////////////////////////////////////
	// Clear out all data structs
	particles.size = 0;
	curCacheParticle = 0;
	//curCacheElement = 0;

	// TODO: Introduce a slice clear list
	for(int i = 0; i < xsizeP1; i++)
	{
		slices[i].slice_particles.size = 0;
		Assert(slices[i].sliceData == NULL);
	}
}

// TODO: Make inline
// NOTE: Must make sure that the ranges of y's generated
// falls within limits of cube_index_t
void SideProjectingParticleCache::insertIntoCache(ImpParticle* p, float cw, float fieldRadius, Point3D& mins, Point3D& maxs)
{
	// TODO: use oneOverCw instead of dividing by cw
	Point3D c = (p->center - mins) / cw;

	// TODO: The *p->scale multiplier was added recently. Don't know if it works right when radius check is disabled.
	float fr = (fieldRadius * p->scale) / cw; // field radius in terms of cubewidths
	float frSq = fr * fr;
	#if (CALC_DX_SQ_PLUS_DZ_SQ==1)
		float cwSq = cw * cw;
	#endif

	// Make sure there is enough elements in the cache to fit all the elements of the particle
	// TODO: This might be slower than just doing it in the inner loop the way it was done before. Should check.
	int maxNeededParticles = round_up((fr*2.0f)+1.0f); // I don't think the +1 is necessary
	int freeParticles = cacheParticles.size - curCacheParticle; // calculate how many free elements there are
	if(freeParticles < maxNeededParticles)
	{
		int dif = maxNeededParticles - freeParticles;
		for(int i = 0; i < dif; i++ )
		cacheParticles.pushAutoSize(new PCacheParticle_t);
	}	

	// NOTE: The 0+1 and size-1 max's and min's force the cache
	// to essentially have no entries at the edges. This forces
	// the field function to be 0 there which in turn forces the
	// renderer to go around the edges rendering all surfaces.
	// It also allows us to not check in the renderer that we've
	// run past the edge of a cube because there is no way a surface
	// can continue outside the edge of a cube with this scheme.
	int min_x = MAX(round_up(c[0] - fr), 0+1);
	int max_x = MIN(round_down(c[0] + fr), xsize-1);

	for(int i = min_x; i <= max_x; i++)
	{
		float dx = c[0] - float(i);
		float dxSq = dx*dx;

		float sliceRadius = sqrtf(frSq - dxSq);

		Assert(i >= 0 && i < xsizeP1);

		PCacheParticle_t* cacheParticle = cacheParticles[curCacheParticle++];
		cacheParticle->particle = p;
		cacheParticle->sliceR = sliceRadius;
		cacheParticle->ycenter = c[1];
		cacheParticle->zcenter = c[2];
		cacheParticle->min_z = MAX(round_up(c[2] - sliceRadius), 0+1);
		cacheParticle->max_z = MIN(round_down(c[2] + sliceRadius), zsize-1);

		slices[i].slice_particles.pushAutoSize(cacheParticle);
	}
}

void SideProjectingParticleCache::beginSlice(cube_index_t x)
{
	PCacheSlice_t* slice = &(slices[x]);

	if(slice->sliceData != NULL)
	{
		clearableSlices.findAndRemoveAll(x);
		return;
	}

	allocSliceData(slice);

	for(int i = 0; i < slice->slice_particles.size; i++)
	{
		PCacheParticle_t* cacheParticle = slice->slice_particles[i];

		insertParticleIntoSlice(slice, cacheParticle);
	}
}

void SideProjectingParticleCache::endSlice(cube_index_t x)
{
	if(clearableSlices.size == max_clearableSlices)
	{
		PCacheSlice_t* old_slice = &(slices[clearableSlices.removeAndReturn(0)]);
		if(old_slice->sliceData == NULL)
		{
			Assert(0);
		}
		else
		{
			clearSlicesData(old_slice);
		}
	}

	PCacheSlice_t* slice = &(slices[x]);
	if(slice->sliceData == NULL)
	{
		Assert(0);
		return;
	}
	clearableSlices.pushAutoSize(x);
}

void SideProjectingParticleCache::allocSliceData(PCacheSlice_t* slice)
{
	if(unusedSliceData.size > 0)
	{
		PSliceData_t* sliceData = unusedSliceData.pop();
		slice->sliceData = sliceData;
	}
	else
	{
		max_alloced_arrays++;
		PSliceData_t* sliceData = new PSliceData_t();

		sliceData->max_cache_elements_size = 0;
		for(int i = 0; i < zsizeP1; i++)
		{
			sliceData->zArray[i] = midSentinel;
		}

		PCacheElem_t* midSentinelPtr = &(sliceData->cacheElements.pushAutoSize());
		PCacheElem_t* botSentinelPtr = &(sliceData->cacheElements.pushAutoSize());
		PCacheElem_t* topSentinelPtr = &(sliceData->cacheElements.pushAutoSize());

		botSentinelPtr->center_y = MIN_SHORT;
		botSentinelPtr->bot_y = 0;
		botSentinelPtr->top_y = 0;
		botSentinelPtr->prev = NULL;
		botSentinelPtr->next = midSentinel;
		botSentinelPtr->p = NULL;
		#if (CALC_DX_SQ_PLUS_DZ_SQ==1)
			botSentinelPtr->dxSqPlusDzSq = 0.0f;
		#endif

		midSentinelPtr->center_y = MAX_SHORT;
		midSentinelPtr->bot_y = 0;
		midSentinelPtr->top_y = 0;
		midSentinelPtr->prev = botSentinel;
		midSentinelPtr->next = topSentinel;
		midSentinelPtr->p = NULL;
		#if (CALC_DX_SQ_PLUS_DZ_SQ==1)
			midSentinelPtr->dxSqPlusDzSq = 0.0f;
		#endif

		topSentinelPtr->center_y = MAX_SHORT;
		topSentinelPtr->bot_y = 0;
		topSentinelPtr->top_y = 0;
		topSentinelPtr->prev = midSentinel;
		topSentinelPtr->next = NULL;
		topSentinelPtr->p = NULL;
		#if (CALC_DX_SQ_PLUS_DZ_SQ==1)
			topSentinelPtr->dxSqPlusDzSq = 0.0f;
		#endif

		slice->sliceData = sliceData;
	}
}

void SideProjectingParticleCache::clearSlicesData(PCacheSlice_t* slice)
{
	PSliceData_t* sliceData = slice->sliceData;

	while(sliceData->clearList.size > 0)
	{
		cube_index_t z = sliceData->clearList.pop();
		// TODO: Return all the elements in the slice to the queue more efficiently
		sliceData->zArray[z] = midSentinel;
	}
	sliceData->max_cache_elements_size = MAX(sliceData->max_cache_elements_size, sliceData->cacheElements.size);
	sliceData->cacheElements.size = 3; // The first three elements are reserved for the sentinels. These aren't cleared.
	
	unusedSliceData.pushAutoSize(sliceData);
	slice->sliceData = NULL;
}

void SideProjectingParticleCache::insertParticleIntoSlice(PCacheSlice_t* slice, PCacheParticle_t* cp)
{
	PSliceData_t* sliceData = slice->sliceData;
	elem_index_t* zarray = sliceData->zArray;

	float fr = cp->sliceR;
	float frSq = fr*fr;

	// Make sure there is enough elements in the cache to fit all the elements of the particle
	// TODO: This might be slower than just doing it in the inner loop the way it was done before. Should check.
	/*
	int maxNeededElements = round_up((fr*2.0f)+1.0f); // I don't think the +1 is necessary
	int freeElements = cacheElements.size - curCacheElement; // calculate how many free elements there are
	if(freeElements < maxNeededElements)
	{
		int dif = maxNeededElements - freeElements;
		for(int i = 0; i < dif; i++ )
		cacheElements.pushAutoSize(new PCacheElem_t());
	}
	*/

	//int min_y = max(round_up(c[1] - fr), 0+1);
	//int max_y = min(round_down(c[1] + fr), ysize-1);

	int min_z = cp->min_z;
	int max_z = cp->max_z;

	for(int j = min_z; j <= max_z; j++)
	{
		float dz = cp->zcenter - float(j);
		float dzSq = (dz*dz);
		float dy = sqrtf(frSq - dzSq); // calc minx and maxx

		// Force min of min_y to be 1 so that at y=0, no particles can have an influence, and field is forced to equal 0.
		// Same for the max of max_y. This is the same as we do for min_x, max_x, min_z and max_z.
		int min_y = MAX(round_up(cp->ycenter - dy), 0+1);
		int max_y = MIN(round_down(cp->ycenter + dy), ysize-1);

		if(min_y <= max_y) // can be not true if we fall betw samples
		{
			// not sure these are correct
			int mid_bot_y = round_down(cp->ycenter);
			//int mid_top_x = mid_bot_x + 1; // not sure if correct!
			//Assert(mid_bot_x >= MIN_CUBE_INDEX && mid_top_x <= MAX_CUBE_INDEX);
			Assert(mid_bot_y >= MIN_CUBE_INDEX && mid_bot_y+1 <= MAX_CUBE_INDEX);
			#if (CALC_DX_SQ_PLUS_DZ_SQ==1)
				//float realDxSqPlusDzSq = dxSqPlusDzSq * cwSq;
			#endif

			Assert(j >= 0 && j < zsizeP1);
			elem_index_t* zSliceElem = &(zarray[j]);

			elem_index_t elem = sliceData->cacheElements.size;
			PCacheElem_t* elemPtr = &(sliceData->cacheElements.pushAutoSize());
			elemPtr->center_y = mid_bot_y;
			elemPtr->bot_y = (cube_index_t)min_y;
			elemPtr->top_y = (cube_index_t)max_y;
			//elemPtr->mid_top_y = (cube_index_t)mid_top_y;
			//elemPtr->mid_bot_y = (cube_index_t)mid_bot_y;
			
			elemPtr->prev = botSentinel;
			elemPtr->next = *zSliceElem;
			elemPtr->p = cp->particle;
			#if (CALC_DX_SQ_PLUS_DZ_SQ==1)
				elemPtr->dxSqPlusDzSq = realDxSqPlusDzSq;
			#endif

			if(*zSliceElem != midSentinel)
			{
				sliceData->cacheElements[*zSliceElem].prev = elem;
			}
			else
			{
				sliceData->clearList.pushAutoSize(j);
			}
			*zSliceElem = elem;
		}
	}
}