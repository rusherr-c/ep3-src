//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include <math.h>
#include "blobulator/util/constants.h"
#include "blobulator/timer.h"
#include "blobulator/util/fasttrunc.h"
#include "blobulator/smartarray.h"
#include "blobulator/point3d.h"
#include "blobulator/implicit/sweeprenderer.h"
#include "blobulator/implicit/particlecache.h"
#include "../vertexbuffers/vertexbuffer_inl.h"


#if (RENDER_API==VALVE_API)
	#include "tier0/vprof.h"
	#include "engine/ivdebugoverlay.h"
	//#include "materialsystem/imesh.h"
	//#include "cdll_client_int.h"
	//#include "enginecallback.h"
	//#include "engine/ivdebugoverlay.h"
#endif

#if (DRAWING_TRIS==1)
	#include "impconsttable_winding_ccw2_tris.h"
#else
	#include "impconsttable_winding_ccw2.h"
#endif

#include "memdbgon.h"


///////////////////////////////////////////////
// Preprocessor macros
///////////////////////////////////////////////

//#define ERROR_CHECK
//#define OVERFLOW_CHECK
#define USHORT_MAX (0xFFFF)
// A quick way to check if a float is negative.
#define ISNEG(f) (*(int*)(&(f)) & 0x80000000)




///////////////////////////////////////////////////////
// Static factors that control how isosurface is rendered
///////////////////////////////////////////////////////

// Cubewidth is the size of a voxel on a side.
// Increase to speed up rendering, reduce quality
#define DEFAULT_CUBEWIDTH 0.8f
float SweepRenderer::cubewidth = DEFAULT_CUBEWIDTH;
float SweepRenderer::oneOverCubewidth = 1.0f / DEFAULT_CUBEWIDTH;
int SweepRenderer::cornerInfoSize = sizeof(CornerInfo);

#if (DYNAMIC_FACTORS == 0)
	#define BLR_CONST const
#else
	#define BLR_CONST
#endif

// Constants for isosurface generation
BLR_CONST float SweepRenderer::cutoffR = 3.3f; // 3.3 min... 3.5 better... 3.0 is much faster but doesn't look as good
BLR_CONST float SweepRenderer::renderR = 1.3f; // 1.3... or 1.4

// These are derived from the constants above. Used to accelerate
// drawing code
BLR_CONST float SweepRenderer::cutoffRSq = SweepRenderer::cutoffR * SweepRenderer::cutoffR;
BLR_CONST float SweepRenderer::scaler = 1.0f / (sqrtf(2.0f) * SweepRenderer::cutoffR);
BLR_CONST float SweepRenderer::scalerSq = SweepRenderer::scaler*SweepRenderer::scaler;
BLR_CONST float SweepRenderer::scaledRenderRSq = SweepRenderer::renderR * SweepRenderer::renderR * SweepRenderer::scalerSq;
BLR_CONST float SweepRenderer::threshold = 0.25f + SweepRenderer::scaledRenderRSq * SweepRenderer::scaledRenderRSq - SweepRenderer::scaledRenderRSq;
BLR_CONST float SweepRenderer::oneOverThreshold = 1.0f / SweepRenderer::threshold;
BLR_CONST float SweepRenderer::addAmt = 0.25f;
BLR_CONST short SweepRenderer::fieldCalcSteps = short(SweepRenderer::cutoffR * SweepRenderer::oneOverCubewidth);

#undef BLR_CONST

bool SweepRenderer::tile_mode = false;
bool SweepRenderer::m_bUseSIMDLoop = false;

int SweepRenderer::marginNCubes = 0;
float SweepRenderer::marginWidth = 0;

Point3D SweepRenderer::outerDimensions = Point3D();
Point3D SweepRenderer::innerDimensions = Point3D();

float g_flByteToFloat[256];

///////////////////////////////////////////////////
// Declaration of timing variables
///////////////////////////////////////////////////

#if (SPEED_TEST==1)
	long recurse_depth = 0;
	long voxel_count = 0;
	long sample_count = 0;
	long cum_time_diff = 0;

	int no_datapoints = 0;

	int aa = 0;
	int bb = 0;
	int tt = 0;
	int zz = 0;
#endif


///////////////////////////////////////////////
// User Declared Function pointers
///////////////////////////////////////////////

static bool DOFASTCALL (*calc_sign)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, PARTICLE_CACHE_CLASS* pCache) = NULL;
static void DOFASTCALL (*calc_sign2)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache) = NULL;
static void DOFASTCALL (*calc_corner)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache) = NULL;
static void DOFASTCALL (*calc_vertex)(const  float x, const float y, const float z, const int edge, const CornerInfo* corner1, const CornerInfo* corner2, VERTEX_BUFFER_CLASS* vertexBuffer) = NULL;
static void DOFASTCALL (*calc_corner_simd)(fltx4 f4Pos, const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache) = NULL;


////////////////////////////////////////////////////////////
// SweepRenderer static member functions
////////////////////////////////////////////////////////////

void SweepRenderer::setCalcSignFunc(bool DOFASTCALL (*calc_sign_ptr)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, PARTICLE_CACHE_CLASS* pCache))
{
	calc_sign = calc_sign_ptr;
}

void SweepRenderer::setCalcSign2Func(void DOFASTCALL (*calc_sign2_ptr)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache))
{
	calc_sign2 = calc_sign2_ptr;
}

void SweepRenderer::setCalcCornerFunc(int _cornerInfoSize, void DOFASTCALL (*calc_corner_ptr)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache))
{
	calc_corner = calc_corner_ptr;
	Assert(_cornerInfoSize >= sizeof(CornerInfo));
	// Round up extraCornerDataSize to a multiple of 16 to allow for proper SSE alignment
	// TODO: Ideally I should only require this when compiling with SSE.
	cornerInfoSize = (((_cornerInfoSize + 15) / 16) * 16);
	m_bUseSIMDLoop = false;
	Assert(cornerInfoSize % 16 == 0);
}


void SweepRenderer::setCalcCornerSIMDFunc(int _cornerInfoSize, void DOFASTCALL (*calc_corner_ptr)(fltx4 f4Pos, const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache))
{
	calc_corner_simd = calc_corner_ptr;
	Assert(_cornerInfoSize >= sizeof(CornerInfo));
	// Round up extraCornerDataSize to a multiple of 16 to allow for proper SSE alignment
	// TODO: Ideally I should only require this when compiling with SSE.
	cornerInfoSize = (((_cornerInfoSize + 15) / 16) * 16);
	m_bUseSIMDLoop = true;
	Assert(cornerInfoSize % 16 == 0);
}

void SweepRenderer::setCalcVertexFunc(void DOFASTCALL (*calc_vertex_ptr)(const  float x, const float y, const float z, const int edge, const CornerInfo* corner1, const CornerInfo* corner2, VERTEX_BUFFER_CLASS* vertexBuffer))
{
	calc_vertex = calc_vertex_ptr;
}




void SweepRenderer::SetCubeWidth(float _cubewidth)
{
	if(_cubewidth == cubewidth) return;
	changeCubeWidth(_cubewidth);
	recalculateDimensions();
	// Note that this will also affect the bounding box of cubes.
}

void SweepRenderer::SetCutoffR(float _cutoffR)
{
	if(_cutoffR == cutoffR) return;
	changeRadii(_cutoffR, renderR);
	recalculateDimensions();
	// Note that this will also affect the bounding box of cubes.
}

void SweepRenderer::SetRenderR(float _renderR)
{
	if(_renderR == renderR) return;
	changeRadii(cutoffR, _renderR);
	recalculateDimensions();
	// Note that this will also affect the bounding box of cubes.
}




float SweepRenderer::GetCubeWidth()
{
	return cubewidth;
}

float SweepRenderer::GetCutoffR()
{
	return cutoffR;
}

float SweepRenderer::GetRenderR()
{
	return renderR;
}




int SweepRenderer::GetMarginNCubes()
{
	return marginNCubes;
}

float SweepRenderer::GetMarginWidth()
{
	return marginWidth;
}

Point3D& SweepRenderer::GetInnerDimensions()
{
	return innerDimensions;
}



void SweepRenderer::changeCubeWidth(float _cubewidth)
{
	cubewidth = _cubewidth;
	oneOverCubewidth = 1.0f / cubewidth;
}

void SweepRenderer::changeRadii(float _cutoffR, float _renderR)
{
	#if (DYNAMIC_FACTORS == 1)
		if(_renderR >= _cutoffR) _renderR = _cutoffR;

		cutoffR = _cutoffR;
		renderR = _renderR;

		// These are derived from the constants above
		cutoffRSq = cutoffR * cutoffR;
		scaler = 1.0f / (sqrtf(2.0f) * cutoffR);
		scalerSq = scaler*scaler;
		scaledRenderRSq = renderR * renderR * scalerSq;
		threshold = 0.25f + scaledRenderRSq * scaledRenderRSq - scaledRenderRSq;
		oneOverThreshold = 1.0f / threshold;
		addAmt = 0.25f;
	#else
		IMP_PRINT("Can't change Radii if factors are not dynamic\n");
	#endif
}

void SweepRenderer::recalculateDimensions()
{
	fieldCalcSteps = short(cutoffR * oneOverCubewidth);

	float fudgeFactor = 0.001f; // Just in case, so we don't go too small on the marginNCubes, and get killed by rounding error
	marginNCubes = round_up(cutoffR/cubewidth + fudgeFactor); 
	marginWidth = marginNCubes * cubewidth;

	outerDimensions.set(xsize*cubewidth, ysize*cubewidth, zsize*cubewidth);
	innerDimensions.set((xsize-marginNCubes*2)*cubewidth, (ysize-marginNCubes*2)*cubewidth, (zsize-marginNCubes*2)*cubewidth);
}



//////////////////////////////////////////////
// SweepRenderer class member functions
//////////////////////////////////////////////

SweepRenderer::SweepRenderer()
	:
//	, initialized(false)
	  offset(0,0,0)
	, n_alloced_slice_corners(0)
	, n_alloced_slice_corner_infos(0)
	, n_alloced_slice_todo_lists(0)
	, maxNoSlicesToDraw(-1)
	#if (IMP_PROFILE == 1)
		, n_frames(0)
		, n_tiles(0)
		, memory_total(0)
		, start_time(0)
	#endif
{
	Blobulator::Constants::InitializeConstants();

	pCache = new PARTICLE_CACHE_CLASS();
	vertexBuffer = new VERTEX_BUFFER_CLASS();
	vertexBuffer->init();

	// NOTE: This is a fudge which is necessary
	// because there are several static variables
	// that don't have defaults
	static bool firstTime = true;
	if(firstTime)
	{
		recalculateDimensions();
		firstTime = false;
	}

	for(int i = 0; i < xsizeP2; i++)
	{
		slices[i].corners = NULL;
	}

	for ( int i = 0; i < 256; i++ )
	{
		g_flByteToFloat[i] = float(i);
	}
	m_nStackSize = 0;
}

void SweepRenderer::recalculateBB()
{
	outerBBMins = offset-(outerDimensions*0.5f);
	outerBBMaxs = offset+(outerDimensions*0.5f);

	innerBBMins = offset-(innerDimensions*0.5f);
	innerBBMaxs = offset+(innerDimensions*0.5f);
}

void SweepRenderer::SetOffset(Point3D const& _offset)
{
	offset = _offset;
	recalculateBB();
	pCache->setOffset(_offset); // This is used by the regular 3D particle cache
}

// Call this to begin a frame
void SweepRenderer::BeginFrame(bool _tile_mode, void* pRenderContext)
{
	#if (RENDER_API==VALVE_API)
		VPROF_BUDGET( "SweepRenderer::beginFrame", "Blobulator" );
	#endif

	#if (IMP_PROFILE == 1)
		if(n_frames == 100)
		{
			__int64 end_time = get_time();
			float time_per_frame = float(end_time - start_time) / float(get_freq() * 100);
			IMP_PRINT("Render time per frame = %f ms (%f fps)\n", time_per_frame, 1.0f/time_per_frame);
			n_frames = 0;
		}
		if(n_frames == 0) start_time = get_time();
		n_frames++;
	#endif

	tile_mode = _tile_mode;
	vertexBuffer->beginFrame(pRenderContext);
}

// Remember to set offset before calling beginTile
void SweepRenderer::BeginTile(const Point3D & offset)
{
	#if (RENDER_API==VALVE_API)
		VPROF_BUDGET( "SweepRenderer::beginTile", "Blobulator" );
	#endif

	#if (SPEED_TEST==1)
		long start_time = clock();
	#endif

	SetOffset(offset);

	//debugDrawBounds();
	pCache->beginTile();
	vertexBuffer->beginTile();

	//IMP_PRINT("Cache Size = %d, Edge Size = %d\n", corner_info_size, edge_info_size);
	//corner_info_size = 0;

	//curTime++; // Curtime will wrap around every 1000 seconds... around every 2 minutes.
	//if(curTime == 0) curTime++; // zero is a bad time since that's what we initialize to. We could also clear the cache at wraparound
}

bool SweepRenderer::isParticleWithinBounds(ImpParticle & p)
{
	return (p.center[0] > outerBBMins[0] && p.center[0] < outerBBMaxs[0] &&
			p.center[1] > outerBBMins[1] && p.center[1] < outerBBMaxs[1] &&
			p.center[2] > outerBBMins[2] && p.center[2] < outerBBMaxs[2]);

	/*
	// Note: When I'm not in tile mode, I might want the above behavior (which will
	// cut off the surface rather than discarding particles)
	if(tile_mode)
	{

	}
	else
	{
		return (p->center[0] > innerBBMins[0] && p->center[0] < innerBBMaxs[0] &&
				p->center[1] > innerBBMins[1] && p->center[1] < innerBBMaxs[1] &&
				p->center[2] > innerBBMins[2] && p->center[2] < innerBBMaxs[2]);
	}
	*/
}


// Call this to add particles
void SweepRenderer::AddParticle(ImpParticle & p, bool skipBoundsCheck)
{
	/*
	if(particles.size >= MAX_PARTICLES-1)
	{
		IMP_PRINT("imp_addParticle: Trying to add more than MAX_PARTICLES particles.\n");
		return;
	}
	*/

	if(skipBoundsCheck || isParticleWithinBounds(p))
	{
		particles.pushAutoSize(&p);
		pCache->addParticle(&p);
	}
}

// Call this to draw the particles
void SweepRenderer::EndTile()
{
	#if (RENDER_API==VALVE_API)
		VPROF_BUDGET( "SweepRenderer::endTile", "Blobulator" );
	#endif

	pCache->buildCache(cubewidth, cutoffR, outerBBMins, outerBBMaxs);

	// Seed the surface
	for(int i = 0; i < particles.size; i++)
	{
		ImpParticle *b = particles[i];

		// This somehow always ends up missing surfaces, even with a count of 10
		//if(b->neighbor_count < 9)
		//{

		//seed_surface(b->center[0], b->center[1], b->center[2]);
		seed_surface(b->center);
		//}
	}

	render_slices();


	#if (IMP_PROFILE == 1)
		memory_total += (unused_slice_corners.size * sizeof(CubeInfo) * ysizeP1 * zsizeP1);
		for(int i = 0; i < unused_corner_info.size; i++)
		{
			memory_total += (unused_corner_info[i].max_size);
		}
		for(int i = 0; i < unused_todo_lists.size; i++)
		{
			memory_total += (unused_todo_lists[i].capacity * sizeof(YZ));
		}

		n_tiles++;
		if(n_tiles == 100)
		{
			IMP_PRINT("SweepRenderer avg max memory usage = %f\n", float(memory_total) / (100.0f * 1000000.0f));
			n_tiles = 0;
			memory_total = 0;
		}
	#endif


	particles.size = 0;
	vertexBuffer->endTile();
	pCache->endTile();

	// Print out stats
	#if (SPEED_TEST==1)
		long time_diff = clock() - start_time;
		cum_time_diff += time_diff;
		no_datapoints ++;
		IMP_PRINT("n=%d t=%f\n", no_datapoints, (float)cum_time_diff / (float)no_datapoints);
		//IMP_PRINT("aa=%d, bb=%d, tt=%d, zz=%d\n", aa, bb, tt, zz);
		//zz++;
	#endif

	#if (MEMORY_TEST==1)
		int n_alloced_corners = 0;
		for(int i = 0; i < unused_slice_data.size; i++)
		{
			n_alloced_corners += unused_slice_data[i]->corner_info.capacity / cornerInfoSize;
		}
		IMP_PRINT("n slices=%d, n corners=%d\n", n_alloced_slices, n_alloced_corners );
		IMP_PRINT("slice_size=%d B, corners_size=%d B\n", n_alloced_slices*sizeof(CubeInfo)*ysizeP1*zsizeP1, n_alloced_corners*sizeof(CornerInfo));
	#endif
}

void SweepRenderer::EndFrame()
{
	#if (RENDER_API==VALVE_API)
		VPROF_BUDGET( "SweepRenderer::endFrame", "Blobulator" );
	#endif

	vertexBuffer->endFrame();
}

DOINLINE void DOFASTCALL SweepRenderer::seed_surface(Point3D& _center)
{
	// find cube corresponding to crawl point
	Point3D center = _center - outerBBMins;
	center.mult(oneOverCubewidth);

	// NOTE: ints for x,y and z because it might have to go negative. cube_index_t can't go negative.

	int start_x = round(center[0]); if(start_x < 0) start_x = 0; if(start_x >= xsize) start_x = xsize-1;
	int start_y = round(center[1]); if(start_y < 0) start_y = 0; if(start_y >= ysize) start_y = ysize-1;
	int start_z = round(center[2]); if(start_z < 0) start_z = 0; if(start_z >= zsize) start_z = zsize-1;
	/*
	// With tiling on, we can't have any field at borders, so we have to clamp closer than border
	int start_x = round(center[0]); if(start_x <= 0) start_x = 1; if(start_x >= xsize) start_x = xsize-1;
	int start_y = round(center[1]); if(start_y <= 0) start_y = 1; if(start_y >= ysize) start_y = ysize-1;
	int start_z = round(center[2]); if(start_z <= 0) start_z = 1; if(start_z >= zsize) start_z = zsize-1;
	*/

	int x = start_x, y = start_y, z = start_z;

	// There are 8 directions, and the max length of every two opposing directions
	// is the max of xsize,ysize and zsize. There are four of these. We should never
	// actually hit max_iters unless there's a bug.
	//int max_iters = max(max(xsize,ysize),zsize)*4;

	// TODO: Take the particle's scaling of the radius into account
	int max_iters = fieldCalcSteps + 2; // the radius of the field + 2 (one to account for possible rounding error, and one to allow steping beyond the field)
	
	int direction = -1;
#if (SEED_HORIZONTALLY)
	if (y < max_iters)
#else
	if (x < max_iters)
#endif
	{
		// Don't want to go in negative numbers if the number is too small
		direction = +1;
	}

	for(int iter = 0; iter < max_iters; iter++)
	{
		Assert(x >= 0 && x < xsizeP1 &&
			   y >= 0 && y < ysizeP1 &&
			   z >= 0 && z < zsizeP1);
		
		#if (SEED_MULTISLICE == 1)
			YZ todo = {y, z};
			slices[x+1].try_seed_list.pushAutoSize(todo); // The +1 here is very important... because there is an extra slice below slice 0.
			x -= 1;
			if(x < 0) break;
		#elif (SEED_SIDEWAYS == 1)
			YZ todo = {y, z};
			slices[x+1].try_seed_list.pushAutoSize(todo);
			break;
		#else
			float fx = x*cubewidth + outerBBMins[0];
			float fy = y*cubewidth + outerBBMins[1];
			float fz = z*cubewidth + outerBBMins[2];

			if(!calc_sign(x,y,z, fx, fy, fz, pCache)
				#if (DO_BOUNDS_CHECK == 1)
					|| x == 0 || y == 0 || z == 0
				#endif
				)
			{
				YZ todo = { y, z };
				slices[x+1].seed_list.pushAutoSize(todo); // The +1 here is very important... because there is an extra slice below slice 0.
				return;
			}
			else
			{
				#if (SEED_HORIZONTALLY == 1)
					y += direction;
					Assert((y >= 0) && (y < ysizeP1));
				#else
					x += direction;
					Assert((x >= 0) && (x < xsizeP1));
				#endif
			}
		#endif
	}
}

// I use cornerInfoNo == 0 to mean that the corner
// is not calculated. That means that we should never
// write into corner_info[0]. Thus, when corner_info
// is empty, corner_info_size == 1.
#define EMPTY_CORNER_INFO_SIZE 1


void SweepRenderer::allocSliceCorners(Slice_t* slice)
{
	if(slice->corners == NULL)
	{
		if(unused_slice_corners.size > 0)
		{
			slice->corners = unused_slice_corners.pop();
		}
		else
		{
			slice->corners = (slice_corners_t)calloc(ysizeP1 * zsizeP1, sizeof(CornerInfo));
			n_alloced_slice_corners ++;
			IMP_PRINT("Allocated %d slice corner arrays\n", n_alloced_slice_corners);
		}
	}

	if(slice->corner_info.capacity == 0)
	{
		if(unused_corner_info.size > 0)
		{
			slice->corner_info = unused_corner_info.pop();
		}
		else
		{
			slice->corner_info.resize(16*cornerInfoSize);
			slice->corner_info.size = EMPTY_CORNER_INFO_SIZE;
			n_alloced_slice_corner_infos ++;
			IMP_PRINT("Allocated %d slice corner info arrays\n", n_alloced_slice_corner_infos);
		}
	}
}

// Note: storing y and z inside of corner info seems to about
// double the clearing time (from 1% to 2%) but it also
// seems to save on the time in calc_corner that it used to
// take to insert y and z and on the time in render_slice to check
// that the array is big enough!!
void SweepRenderer::deallocSliceCorners(Slice_t* slice)
{
	Assert(slice->corners != NULL && slice->corner_info.capacity != NULL);

	byte* ci_pointer = slice->corner_info.a;

	for(int i = 0; i < slice->corner_info.size; i++)
	{
		CornerInfo* ci = (CornerInfo*)&(ci_pointer[i*cornerInfoSize]); // TODO: It might be faster to calculate this as a byte offset multiplying i by cornerInfoSize
		// Use the corner info to clear the doneAbove and doneBelow flags
		slice->corners[ci->y][ci->z].everything = 0;
	}

	#if(KEEP_MAX_SIZE == 1)
		slice->corner_info.max_size = max(slice->corner_info.max_size, slice->corner_info.size);
	#endif
	// TODO: Rather than having this uglyness, I should use 0xFFFF as
	// the identifier for an empty corner info, allowing the size to be
	// 0 when there are no corner_infos
	slice->corner_info.size = EMPTY_CORNER_INFO_SIZE;

	/*
	for(int i = 0; i < sd->clear_list.size; i+=2)
	{
		//YZ yz = sd->clear_list[i];
		//sd->corners[yz.y][yz.z].everything = 0;
		cube_index_t y = sd->clear_list[i];
		cube_index_t z = sd->clear_list[i+1];
		sd->corners[y][z].everything = 0;
	}
	sd->clear_list.size = 0;
	*/
	
	unused_slice_corners.pushAutoSize(slice->corners);
	slice->corners = NULL;
	unused_corner_info.pushAutoSize(slice->corner_info);
	slice->corner_info.size = 0;
	slice->corner_info.capacity = 0;
	slice->corner_info.a = NULL;
}

void SweepRenderer::allocSliceTodoList(Slice_t* slice)
{
	if(slice->todo_list.capacity == 0)
	{
		if(unused_todo_lists.size > 0)
		{
			slice->todo_list = unused_todo_lists.pop();
		}
		else
		{
			slice->todo_list.resize(16);
			n_alloced_slice_todo_lists ++;
			IMP_PRINT("Allocated %d todo lists\n", n_alloced_slice_todo_lists);
		}
	}
}

void SweepRenderer::deallocSliceTodoList(Slice_t* slice)
{
	Assert(slice->todo_list.capacity >= 0);

	unused_todo_lists.pushAutoSize(slice->todo_list);
	slice->todo_list.size = 0;
	slice->todo_list.capacity = 0;
	slice->todo_list.a = NULL;

	//unused_todo_lists.pushAutoSize(slice->todo_list);
	//slice->todo_list = NULL;
}

void SweepRenderer::render_slices()
{
	int noSlicesDrawn = 0;
	polygonizationEnabled = true;

	Slice_t* prev_slice;
	Slice_t* cur_slice;
	Slice_t* next_slice;

	int numIterations = 0;
	for(int x = 0; x < xsize; x++, numIterations++)
	{
		cur_slice = &(slices[x+1]);

		if ( numIterations >= 4*xsize )
		{
			// FIXME: diagnose why this happens! (seems to happen with non-unit ImpParticle::scale/fieldRScaleSq)
			Warning( "ERROR: SweepRenderer::render_slices got into an infinite loop!\n" );
			Assert( 0 );
			break;
		}

		if(cur_slice->todo_list.size > 0 || cur_slice->seed_list.size > 0
			#if (SEED_MULTISLICE == 1 || SEED_SIDEWAYS == 1)
				|| cur_slice->try_seed_list.size > 0
			#endif
			)
		{
			if(noSlicesDrawn == maxNoSlicesToDraw)
			{
				polygonizationEnabled = false;
			}
			if(polygonizationEnabled)
			{
				last_slice_drawn = x;
			}
			noSlicesDrawn ++;

			// Make sure this and next slice have
			// corner arrays allocated
			prev_slice = &(slices[x]);
			next_slice = &(slices[x+2]);

			allocSliceTodoList(prev_slice);
			// Wrong: We don't allocate a todo list for the current slice because the current slice uses the recursion stack!
			allocSliceTodoList(cur_slice);
			allocSliceTodoList(next_slice);

			//allocSliceCorners(prev_slice);
			allocSliceCorners(cur_slice);
			allocSliceCorners(next_slice);
			pCache->beginSlice(x);
			pCache->beginSlice(x+1);

			// Corners for this slice are on cur_slice and next_slice
			// TODOs for prev slice are on prev_slice
			// TODOs for this slice are on cur_slice
			// TODOs for next slice are on next_slice
			if ( m_bUseSIMDLoop )
			{
				render_slice_simd(x, prev_slice, cur_slice, next_slice);
			}
			else
			{
				render_slice(x, prev_slice, cur_slice, next_slice);
			}

			//Now, everything is done on this slice. If we have no todos above or below, we can delete this slice.
			//If the slice above is done, and has no todos above it, we can delete it's corner data.
			//If the slice below is done, and has no todos below it, we can delete it's corner data.

			/*
			bool prev_prev_slice_done = ((x<=0) || (slices[x-1].todo_list.size==0 && slices[x-1].seed_list.size==0));
			bool prev_slice_done = (prev_slice->todo_list.size==0 && prev_slice->seed_list.size==0);
			bool cur_slice_done = (cur_slice->todo_list.size==0 && cur_slice->seed_list.size==0);
			bool next_slice_done = (next_slice->todo_list.size==0 && next_slice->seed_list.size==0);
			bool next_next_slice_done = ((x>=(xsize-1)) || (slices[x+3].todo_list.size==0 && slices[x+3].seed_list.size==0));
			*/

			/*
			bool prev_prev_slice_done = (x<=0 || slices[x-1].todo_list.size==0);
			bool prev_slice_done = (prev_slice->todo_list.size==0);
			bool cur_slice_done = (cur_slice->todo_list.size==0);
			bool next_slice_done = (next_slice->todo_list.size==0);
			bool next_next_slice_done = (x>=(xsize-1) || slices[x+3].todo_list.size==0);
			*/

			bool prev_slice_done = (prev_slice->todo_list.size==0 && prev_slice->seed_list.size==0);
			Assert(cur_slice->todo_list.size==0 && cur_slice->seed_list.size==0); // cur_slice_done
			bool next_slice_done = (next_slice->todo_list.size==0 && next_slice->seed_list.size==0);

			#if (SEED_MULTISLICE == 1 || SEED_SIDEWAYS == 1)
				prev_slice_done = prev_slice_done && prev_slice->try_seed_list.size == 0;
				Assert(cur_slice->try_seed_list.size == 0);
				next_slice_done = next_slice_done && next_slice->try_seed_list.size == 0;
			#endif

			/*
			if(prev_slice_done && next_slice_done)
			{
				deallocSliceCorners(cur_slice);
			}

			if(prev_slice_done && prev_prev_slice_done)
			{
				deallocSliceCorners(prev_slice);
			}

			if(next_slice_done && next_next_slice_done)
			{
				deallocSliceCorners(next_slice);
			}
			*/

			deallocSliceTodoList(cur_slice);

			if(prev_slice_done)
			{
				deallocSliceTodoList(prev_slice);
				deallocSliceCorners(cur_slice);
				pCache->endSlice(x);
			}

			if(next_slice_done)
			{
				deallocSliceTodoList(next_slice);
				deallocSliceCorners(next_slice);
				pCache->endSlice(x+1);
			}

			// If on previous slice, there is something to do do that.
			// Otherwise, go to next slice.

			if(!prev_slice_done && x>0)
			{
				// Go down if there is work to do on previous slice!
				x -= 2;
			}
		}
	}
}


extern void DOFASTCALL calcSign2_SIMD(fltx4 f4Pos, const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache);

// Note: We only force a bounds check when seeding
// if DO_BOUNDS_CHECK is set to 1. calcSign2 will
// also have a forced bounds check in such a case
// which will do appropriate bounds checking on the borders.
#define BOUNDS_CHECK_OFF(a)

// This function should only be called on cubes that haven't been done.
DOINLINE void DOFASTCALL SweepRenderer::render_slice(const cube_index_t x, Slice_t* prev_slice, Slice_t* cur_slice, Slice_t* next_slice)
{
	float fx[2];
	fx[0] = (float(x) * cubewidth) + outerBBMins[0];
	fx[1] = fx[0] + cubewidth;

	const slice_corners_t cur_cubes = cur_slice->corners;
	int cur_corner_info_no = cur_slice->corner_info.size;

	const slice_corners_t next_cubes = next_slice->corners;
	int next_corner_info_no = next_slice->corner_info.size;

#if (SEED_MULTISLICE==1)
	while(cur_slice->try_seed_list.size > 0)
	{
		YZ temp = cur_slice->try_seed_list.pop();
		//Assert(cur_slice->data->corners[temp.y][temp.z].done == false);
		if(cur_slice->corners[temp.y][temp.z].doneAbove)
		{
			continue;
		}

		//cur_slice->corner_info.ensureAdditionalCapacity(1);
		//next_slice->corner_info.ensureAdditionalCapacity(1);

		float fy = temp.y*cubewidth + outerBBMins[1];
		float fz = temp.z*cubewidth + outerBBMins[2];

		if(calc_sign(x+1,temp.y,temp.z, fx[1], fy, fz, pCache) && !calc_sign(x,temp.y,temp.z, fx[0], fy, fz, pCache))
		{
			cur_slice->seed_list.pushAutoSize(temp);
		}
	}
#endif

#if (SEED_SIDEWAYS == 1)
	while(cur_slice->try_seed_list.size > 0)
	{
		YZ temp = cur_slice->try_seed_list.pop();
		int y = temp.y;
		int z = temp.z;

		// TODO: Take the particle's scaling of the radius into account
		int max_iters = fieldCalcSteps + 2; // the radius of the field + 2 (one to account for possible rounding error, and one to allow steping beyond the field)

		float fz = z*cubewidth + outerBBMins[2];
		float fy = y*cubewidth + outerBBMins[1];

		for(int iter = 0; iter < max_iters; iter++)
		{
			Assert(x >= 0 && x < xsizeP1 &&
				y >= 0 && y < ysizeP1 &&
				z >= 0 && z < zsizeP1);

			if(!calc_sign(x,y,z, fx[0], fy, fz, pCache) 
#if (DO_BOUNDS_CHECK == 1)
				|| x == 0 || y == 0 || z == 0
#endif
				)
			{
				YZ todo = {y, z};
				cur_slice->seed_list.pushAutoSize(todo);
				break;
			}
			else
			{
				y -= 1;
				fy -= cubewidth;
				Assert(y >= 0); // calc sign should always return true when y == 0 if the cache correctly forces field values to 0 at borders.
			}
		}
	}
#endif


	while(true)
	{
#if (BREADTH_FIRST == 1)
		if(recursion_stack_src.size == 0)
		{
#endif

			YZ temp;
			if(cur_slice->todo_list.size > 0)
			{
				temp = cur_slice->todo_list.pop();
			}
			else if(cur_slice->seed_list.size > 0)
			{
				temp = cur_slice->seed_list.pop();
			}
			else
			{
				break;
			}

			//Assert(cur_slice->data->corners[temp.y][temp.z].done == false);
			if(cur_slice->corners[temp.y][temp.z].doneAbove)
			{
				continue;
			}

			cur_slice->corners[temp.y][temp.z].doneAbove = true;
			next_slice->corners[temp.y][temp.z].doneBelow = true;
			recursion_stack_src.push2AutoSize(temp.y, temp.z);

#if (BREADTH_FIRST == 1)
		}
#endif

		do {		
			/////////////////////////////////////////////////////////////////////////////////////////////////
			/////////////// This code retrieves the field values at the corners from cache //////////////////
			/////////////// or calculates them if they are not in cache. ////////////////////////////////////
			/////////////////////////////////////////////////////////////////////////////////////////////////

			const cube_index_t z = recursion_stack_src.pop();
			const cube_index_t y = recursion_stack_src.pop();
			// TODO: Might speed things up I also had an x+1, y+1 and z+1 variable
			// Then, I wouldn't need to calculate those all over the place.


			// TODO: If a corner is outside of the area that will get polygonized,
			// then we only need to calculate a sign there, not a field value.
			// I can replace calc_corner with another function if we're in a non-rendered area.
			// (However, I need to be careful with corners that are at the boundary between
			// the rendered and non-rendered areas. I need a clever, fast way to compute all of that.
			// The check can be: a) check if you're outside of cube (using code already written).
			// b) if outside of cube, do a second check to see if you're outside the cube by one element.
			bool polygonize_cube = polygonizationEnabled;
			void DOFASTCALL (*calc_corner_func)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache) = calc_corner;
			if(tile_mode)
			{
				// When tiling, there's no point drawing cubes in the margin
				if(x < marginNCubes || x >= xsize-marginNCubes ||
					y < marginNCubes || y >= ysize-marginNCubes ||
					z < marginNCubes || z >= zsize-marginNCubes)
				{
					polygonize_cube = false;
					if(x < marginNCubes-1 || x > xsize-marginNCubes ||
						y < marginNCubes-1 || y > ysize-marginNCubes ||
						z < marginNCubes-1 || z > zsize-marginNCubes)
					{
						calc_corner_func = calc_sign2;
					}
				}
			}

			float fy[2];
			float fz[2];


			fy[0] = (float(y) * cubewidth) + outerBBMins[1];
			fy[1] = fy[0] + cubewidth;
			fz[0] = (float(z) * cubewidth) + outerBBMins[2];
			fz[1] = fz[0] + cubewidth;
			// TODO: I can store these in arrays and use lookups (as I did before)
			// Note: The above can be calculated with three mmx instructions:
			// initialize [fy0,fy1,fz0,fz1] = ((y,y+1,z,z+1)*cubewidth + (outerBBMins[1],outerBBMins[1],outerBBMins[2],outerBBMins[2])
			// or         [fy0,fy1,fz0,fz1] = ((y,y,z,z)*cubewidth + (outerBBMins[1],outerBBMins[1]+cw,outerBBMins[2],outerBBMins[2]+cw)


			CornerInfo* cubeCorners[8];
			int index = 0;

			// TODO: Could pass the data for the three slices as an array of size 3...
			// Then, I could pass that into calc_corner, and calc_corner could then
			// pick the appropriate array for itself!

			recursion_stack_dest.ensureAdditionalCapacity(8); // four elements of size 2

			// NOTE: I modify the size here to be the true size in bytes of the corner info array.
			// This is necessary for the ensureAdditionalCapacity call to function correctly.
			cur_slice->corner_info.size = cur_corner_info_no * cornerInfoSize;
			cur_slice->corner_info.ensureAdditionalCapacity(4*cornerInfoSize);
			next_slice->corner_info.size = next_corner_info_no * cornerInfoSize;
			next_slice->corner_info.ensureAdditionalCapacity(4*cornerInfoSize);

			// NOTE: The reason I do this here instead of at the beginning of the function
			// is because the array may bet moved when it is resized by ensureAdditionalCapacity
			byte* const cur_corner_info = cur_slice->corner_info.a;
			byte* const next_corner_info = next_slice->corner_info.a;

			// When we access the cubes array in binary order 000,001,010, the compiler optimize the code for us... yay =)
			// TODO: Accessing cubes could be much faster if I use offsets wisely
			// TODO: I could store the isneg value as a 0/1 inside cubeCorners->cornerinfo...
			// then I would just have to shift a little in the other direction... no ISNEG necessary
			CubeInfo* const cube0 = &(cur_cubes[y][z]);
			if(cube0->cornerInfoNo == 0)
			{
				cube0->cornerInfoNo = cur_corner_info_no;
				CornerInfo* cornerInfo = (CornerInfo*)&(cur_corner_info[cur_corner_info_no*cornerInfoSize]);
				cur_corner_info_no++;
				calc_corner_func(x,y,z,fx[0], fy[0], fz[0], cornerInfo, pCache);
			}
			cubeCorners[0] = (CornerInfo*)&(cur_corner_info[cube0->cornerInfoNo*cornerInfoSize]);
			index |= ISNEG(cubeCorners[0]->value) >> 31; // LBF

			CubeInfo* const cube1 = &(cur_cubes[y][z+1]);
			if(cube1->cornerInfoNo == 0)
			{
				cube1->cornerInfoNo = cur_corner_info_no;
				CornerInfo* cornerInfo = (CornerInfo*)&(cur_corner_info[cur_corner_info_no*cornerInfoSize]);
				cur_corner_info_no++;
				calc_corner_func(x,y,z+1,fx[0], fy[0], fz[1], cornerInfo, pCache);
			}
			cubeCorners[1] = (CornerInfo*)&(cur_corner_info[cube1->cornerInfoNo*cornerInfoSize]);
			index |= ISNEG(cubeCorners[1]->value) >> 30; // LBN

			CubeInfo* const cube2 = &(cur_cubes[y+1][z]);
			if(cube2->cornerInfoNo == 0)
			{
				cube2->cornerInfoNo = cur_corner_info_no;
				CornerInfo* cornerInfo = (CornerInfo*)&(cur_corner_info[cur_corner_info_no*cornerInfoSize]);	
				cur_corner_info_no++;
				calc_corner_func(x,y+1,z,fx[0], fy[1], fz[0], cornerInfo, pCache);
			}
			cubeCorners[2] = (CornerInfo*)&(cur_corner_info[cube2->cornerInfoNo*cornerInfoSize]);
			index |= ISNEG(cubeCorners[2]->value) >> 29; // LTF

			CubeInfo* const cube3 = &(cur_cubes[y+1][z+1]);
			if(cube3->cornerInfoNo == 0)
			{
				cube3->cornerInfoNo = cur_corner_info_no;
				CornerInfo* cornerInfo = (CornerInfo*)&(cur_corner_info[cur_corner_info_no*cornerInfoSize]);
				cur_corner_info_no++;
				calc_corner_func(x,y+1,z+1,fx[0], fy[1], fz[1], cornerInfo, pCache);
			}
			cubeCorners[3] = (CornerInfo*)&(cur_corner_info[cube3->cornerInfoNo*cornerInfoSize]);
			index |= ISNEG(cubeCorners[3]->value) >> 28; // * LTN;

			CubeInfo* const cube4 = &(next_cubes[y][z]);
			if(cube4->cornerInfoNo == 0)
			{
				cube4->cornerInfoNo = next_corner_info_no;
				CornerInfo* cornerInfo = (CornerInfo*)&(next_corner_info[next_corner_info_no*cornerInfoSize]);
				next_corner_info_no++;
				calc_corner_func(x+1,y,z,fx[1], fy[0], fz[0], cornerInfo, pCache);
			}
			cubeCorners[4] = (CornerInfo*)&(next_corner_info[cube4->cornerInfoNo*cornerInfoSize]);
			index |= ISNEG(cubeCorners[4]->value) >> 27; // RBF;

			CubeInfo* const cube5 = &(next_cubes[y][z+1]);
			if(cube5->cornerInfoNo == 0)
			{
				cube5->cornerInfoNo = next_corner_info_no;
				CornerInfo* cornerInfo = (CornerInfo*)&(next_corner_info[next_corner_info_no*cornerInfoSize]);
				next_corner_info_no++;
				calc_corner_func(x+1,y,z+1,fx[1], fy[0], fz[1], cornerInfo, pCache);
			}
			cubeCorners[5] = (CornerInfo*)&(next_corner_info[cube5->cornerInfoNo*cornerInfoSize]);
			index |= ISNEG(cubeCorners[5]->value) >> 26; // RBN;

			CubeInfo* const cube6 = &(next_cubes[y+1][z]);
			if(cube6->cornerInfoNo == 0)
			{
				cube6->cornerInfoNo = next_corner_info_no;
				CornerInfo* cornerInfo = (CornerInfo*)&(next_corner_info[next_corner_info_no*cornerInfoSize]);
				next_corner_info_no++;
				calc_corner_func(x+1,y+1,z,fx[1], fy[1], fz[0], cornerInfo, pCache);
			}
			cubeCorners[6] = (CornerInfo*)&(next_corner_info[cube6->cornerInfoNo*cornerInfoSize]);
			index |= ISNEG(cubeCorners[6]->value) >> 25; // RTF;

			CubeInfo* const cube7 = &(next_cubes[y+1][z+1]);
			if(cube7->cornerInfoNo == 0)
			{
				cube7->cornerInfoNo = next_corner_info_no;
				CornerInfo* cornerInfo = (CornerInfo*)&(next_corner_info[next_corner_info_no*cornerInfoSize]);
				next_corner_info_no++;
				calc_corner_func(x+1,y+1,z+1,fx[1], fy[1], fz[1], cornerInfo, pCache);
			}
			cubeCorners[7] = (CornerInfo*)&(next_corner_info[cube7->cornerInfoNo*cornerInfoSize]);
			index |= ISNEG(cubeCorners[7]->value) >> 24; // RTN;


#ifdef ERROR_CHECK
			if(cur_cubes[y][z].done == false)
			{
				IMP_PRINT("MAJOR ERROR... in render_slice... already done");
				return;
			}

			// error if this cube does not intersect surface
			if(index == 0 || index == 255)
			{
				IMP_PRINT("ERROR: render_slice was called on a cube[%d,%d,%d] with no surface intersection (index=%d)\n", x, y, z, index);
			}
#endif



			/////////////////////////////////////////////////////////////////////////////////////////////////
			/////////////////////// This code enqueues the neighboring cubes for rendering //////////////////
			/////////////////////////////////////////////////////////////////////////////////////////////////

			// polygonize adjacent cubes
			// I might not need to check for x > 0, x < xsize - 1, etc if I can ensure that
			// particles will never go outside dimensions of rendering cube. ** Or if I put sentinel done values

			// TODO: Rather than checking if the item below, to the left, above, etc is done...
			// When I do a cube, I could mark the cubes around it with below done, above done, etc...
			// Then, I don't have to look at surrounding cubes to see which ones have been done.

			// TODO: It might be faster to run this code at the end, after the polygonization
			// since this code might throw out cubeCorners from the cache. Also, it writes
			// into the todo_next table and it might make sense to write into that right before
			// we read back from that. However, it might be better to do this here because
			// cubes might still be in the cache. Also, above, I access all the cubes. Maybe
			// it would be faster to store a pointer to those cubes rather than re-addressing
			// them again here.

			// TODO: It might be better to use ImpConstTable2.h... with the crawlTable
			// and drawTables together, since the current approach has less memory locality.

			// NOTE: With tiling enabled, we need to follow the surface all the way to the
			// edge of the cache. In order to do this, we need to put in checks here to
			// make sure we don't read past the end of the cache. This could be avoided by
			// increasing the margin to (2*cutoffRadius/cubewidth) voxels, but it would
			// waste a tremendous amount of cache.

			char adjacentVoxels = crawlTable[index];
			if((adjacentVoxels & LEFT_FACE) && BOUNDS_CHECK_OFF(x>0 &&)  cur_cubes[y][z].doneBelow == false)
			{
				YZ yz = {y, z};
				prev_slice->todo_list.pushAutoSize(yz);
			}

			if((adjacentVoxels & RIGHT_FACE) && BOUNDS_CHECK_OFF(x<(xsize-1) &&) next_cubes[y][z].doneAbove == false)
			{
				YZ yz = {y, z};
				next_slice->todo_list.pushAutoSize(yz);
			}

			if((adjacentVoxels & BOT_FACE) && BOUNDS_CHECK_OFF(y>0 &&) cur_cubes[y-1][z].doneAbove == false)
			{
				cur_cubes[y-1][z].doneAbove = true;
				next_cubes[y-1][z].doneBelow = true;
				recursion_stack_dest.push2(y-1, z);
			}

			if((adjacentVoxels & FAR_FACE) && BOUNDS_CHECK_OFF(z>0 &&) cur_cubes[y][z-1].doneAbove == false)
			{
				cur_cubes[y][z-1].doneAbove = true;
				next_cubes[y][z-1].doneBelow = true;
				recursion_stack_dest.push2(y, z-1);
			}

			if((adjacentVoxels & TOP_FACE) && BOUNDS_CHECK_OFF(y<(ysize-1) &&) cur_cubes[y+1][z].doneAbove == false)
			{
				cur_cubes[y+1][z].doneAbove = true;
				next_cubes[y+1][z].doneBelow = true;
				recursion_stack_dest.push2(y+1, z);
			}

			if((adjacentVoxels & NEAR_FACE) && BOUNDS_CHECK_OFF(z<(zsize-1) &&) cur_cubes[y][z+1].doneAbove == false)
			{
				cur_cubes[y][z+1].doneAbove = true;
				next_cubes[y][z+1].doneBelow = true;
				recursion_stack_dest.push2(y, z+1);
			}


			//////////////////////////////////////////////////////////////////
			///////////// This is the polygonization code ////////////////////
			//////////////////////////////////////////////////////////////////

			if(polygonize_cube)
			{
				vertexBuffer->beginCube(x);

				static const int edgeOffsets[12][6] = {{0,0,0,2,0,1},{0,0,0,1,0,2},{0,0,1,1,1,3},{0,1,0,2,2,3}, {0,0,0,0,0,4},{0,0,1,0,1,5},{0,1,0,0,2,6},{0,1,1,0,3,7}, {1,0,0,2,4,5},{1,0,0,1,4,6},{1,0,1,1,5,7},{1,1,0,2,6,7}};
				const char * const table = (const char*)&(drawTable[index]);

				// TODO: I can make this loop a little smarter if I have a -2 for
				// end of list, and just exit the loop upon seeing that. Right now,
				// I need two -1s in a row before I exit.
				int counter = 0;
				int edgeno = table[counter];
				while(edgeno != -1)
				{
#if (DRAWING_STRIPS==1)
					vertexBuffer->beginTriStrip();
					do
					{
#endif
						// generate vertex position and normal data
						const int *edgeOffset = edgeOffsets[edgeno];
						const int edgea = edgeOffset[3];

						CornerInfo* const corner1 = cubeCorners[edgeOffset[4]];

						vbId_t* const id = &(corner1->edges[edgea]);

						if(vertexBuffer->isIdValid(id))
						{
							vertexBuffer->drawVertex(id);
						}
						else
						{
							vertexBuffer->beginVertex(id);
							const CornerInfo* const corner2 = cubeCorners[edgeOffset[5]];
							calc_vertex(fx[edgeOffset[0]], fy[edgeOffset[1]], fz[edgeOffset[2]], edgea, corner1, corner2, vertexBuffer);
							vertexBuffer->endVertex(id);
						}

#if (DRAWING_STRIPS==1)
						counter ++;
						edgeno = table[counter];
					} while(edgeno != -1);

					vertexBuffer->endTriStrip();
#endif

					counter ++;
					edgeno = table[counter];
				}

				vertexBuffer->endCube();
			}

		} while(recursion_stack_src.size > 0);

#if (BREADTH_FIRST == 1)
		recursion_stack_src.swapData(recursion_stack_dest);
#endif
	}

	// NOTE: Here, I store back the size in CornerInfos back into the
	// corner info arrays (rather than the size in bytes). It is important
	// that there are no returns in the code above that could prevent this
	// from executing.
	cur_slice->corner_info.size = cur_corner_info_no;
	next_slice->corner_info.size = next_corner_info_no;
}

// This function should only be called on cubes that haven't been done.
DOINLINE void DOFASTCALL SweepRenderer::render_slice_simd(const cube_index_t x, Slice_t* prev_slice, Slice_t* cur_slice, Slice_t* next_slice)
{
	cube_index_t *nStack = m_nStack;
	int nStackSize = m_nStackSize;

	fltx4 f4X = ReplicateX4( g_flByteToFloat[x] );
	fltx4 f4CubeWidth = ReplicateX4( cubewidth );

	const slice_corners_t cur_cubes = cur_slice->corners;
	int cur_corner_info_no = cur_slice->corner_info.size;

	const slice_corners_t next_cubes = next_slice->corners;
	int next_corner_info_no = next_slice->corner_info.size;

	fltx4 f4SIMD_001Mask = LoadAlignedSIMD( Blobulator::Constants::s_SIMD_001Mask );
	fltx4 f4SIMD_010Mask = LoadAlignedSIMD( Blobulator::Constants::s_SIMD_010Mask );
	fltx4 f4SIMD_011Mask = LoadAlignedSIMD( Blobulator::Constants::s_SIMD_011Mask );
	fltx4 f4SIMD_100Mask = LoadAlignedSIMD( Blobulator::Constants::s_SIMD_100Mask );
	fltx4 f4SIMD_101Mask = LoadAlignedSIMD( Blobulator::Constants::s_SIMD_101Mask );
	fltx4 f4SIMD_110Mask = LoadAlignedSIMD( Blobulator::Constants::s_SIMD_110Mask );

	cube_index_t x1 = x+1;
	while(true)
	{
		YZ temp;
		if(cur_slice->todo_list.size > 0)
		{
			temp = cur_slice->todo_list.pop();
		}
		else if(cur_slice->seed_list.size > 0)
		{
			temp = cur_slice->seed_list.pop();
		}
		else
		{
			break;
		}

		// Make sure that we are in the range.
		Assert((temp.y >= 0) && (temp.y <= ysize));
		Assert((temp.z >= 0) && (temp.z <= zsize));

		//Assert(cur_slice->data->corners[temp.y][temp.z].done == false);
		if(cur_slice->corners[temp.y][temp.z].doneAbove)
		{
			continue;
		}

		cur_slice->corners[temp.y][temp.z].doneAbove = true;
		next_slice->corners[temp.y][temp.z].doneBelow = true;
		nStack[nStackSize++] = temp.y;
		nStack[nStackSize++] = temp.z;

		do {		
			/////////////////////////////////////////////////////////////////////////////////////////////////
			/////////////// This code retrieves the field values at the corners from cache //////////////////
			/////////////// or calculates them if they are not in cache. ////////////////////////////////////
			/////////////////////////////////////////////////////////////////////////////////////////////////


			const cube_index_t z = nStack[--nStackSize];
			const cube_index_t y = nStack[--nStackSize];
			cube_index_t y1 = y+1;
			cube_index_t z1 = z+1;

			// Make sure that we are in the range for +1 operations.
			Assert((x >= 0) && (x1 <= xsize));
			Assert((y >= 0) && (y1 <= ysize));
			Assert((z >= 0) && (z1 <= zsize));

			// TODO: Might speed things up I also had an x+1, y+1 and z+1 variable
			// Then, I wouldn't need to calculate those all over the place.


			// TODO: If a corner is outside of the area that will get polygonized,
			// then we only need to calculate a sign there, not a field value.
			// I can replace calc_corner with another function if we're in a non-rendered area.
			// (However, I need to be careful with corners that are at the boundary between
			// the rendered and non-rendered areas. I need a clever, fast way to compute all of that.
			// The check can be: a) check if you're outside of cube (using code already written).
			// b) if outside of cube, do a second check to see if you're outside the cube by one element.
			bool polygonize_cube = polygonizationEnabled;
			void DOFASTCALL (*calc_corner_func)(fltx4 f4Pos, const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache) = calc_corner_simd;
			if(tile_mode)
			{
				// When tiling, there's no point drawing cubes in the margin
				if(x < marginNCubes || x >= xsize-marginNCubes ||
				   y < marginNCubes || y >= ysize-marginNCubes ||
				   z < marginNCubes || z >= zsize-marginNCubes)
				{
					polygonize_cube = false;
					if(x < marginNCubes-1 || x > xsize-marginNCubes ||
					   y < marginNCubes-1 || y > ysize-marginNCubes ||
					   z < marginNCubes-1 || z > zsize-marginNCubes)
					{
						calc_corner_func = calcSign2_SIMD;
					}
				}
			}
			fltx4 f4Y = ReplicateX4( g_flByteToFloat[y] );
			fltx4 f4Z = ReplicateX4( g_flByteToFloat[z] );
			fltx4 f4Position = OrSIMD( AndSIMD( f4SIMD_100Mask, f4X ), AndSIMD( f4SIMD_010Mask, f4Y ) );	// X | Y | Z			<== This creates a lot of LHS
			f4Position = OrSIMD( f4Position, AndSIMD( f4SIMD_001Mask, f4Z ) );								//						<== There should be a better way to do this
			f4Position = AddSIMD( outerBBMins.sse_vec, MulSIMD( f4CubeWidth, f4Position ) ); // pos * cubewidth + outerBBMins
			fltx4 f4Position1 = AddSIMD( f4Position, f4CubeWidth );
			
			// TODO: I can store these in arrays and use lookups (as I did before)
			// Note: The above can be calculated with three mmx instructions:
			// initialize [fy0,fy1,fz0,fz1] = ((y,y+1,z,z+1)*cubewidth + (outerBBMins[1],outerBBMins[1],outerBBMins[2],outerBBMins[2])
			// or         [fy0,fy1,fz0,fz1] = ((y,y,z,z)*cubewidth + (outerBBMins[1],outerBBMins[1]+cw,outerBBMins[2],outerBBMins[2]+cw)


			CornerInfo* cubeCorners[8];
			int index = 0;

			// TODO: Could pass the data for the three slices as an array of size 3...
			// Then, I could pass that into calc_corner, and calc_corner could then
			// pick the appropriate array for itself!

			// NOTE: I modify the size here to be the true size in bytes of the corner info array.
			// This is necessary for the ensureAdditionalCapacity call to function correctly.
			cur_slice->corner_info.size = cur_corner_info_no * sizeof(CornerInfoMax_t);
			cur_slice->corner_info.ensureAdditionalCapacity(4*sizeof(CornerInfoMax_t));
			next_slice->corner_info.size = next_corner_info_no * sizeof(CornerInfoMax_t);
			next_slice->corner_info.ensureAdditionalCapacity(4*sizeof(CornerInfoMax_t));

			// NOTE: The reason I do this here instead of at the beginning of the function
			// is because the array may get moved when it is resized by ensureAdditionalCapacity
			CornerInfoMax_t* const cur_corner_info = (CornerInfoMax_t* const)(cur_slice->corner_info.a);
			CornerInfoMax_t* const next_corner_info = (CornerInfoMax_t* const)(next_slice->corner_info.a);

			// When we access the cubes array in binary order 000,001,010, the compiler optimize the code for us... yay =)
			// TODO: Accessing cubes could be much faster if I use offsets wisely
			// TODO: I could store the isneg value as a 0/1 inside cubeCorners->cornerinfo...
			// then I would just have to shift a little in the other direction... no ISNEG necessary

			CubeInfo* const cube0 = &(cur_cubes[y][z]);
			if(cube0->cornerInfoNo == 0)
			{
				cube0->cornerInfoNo = cur_corner_info_no;
				CornerInfo* cornerInfo = (CornerInfo*)&(cur_corner_info[cur_corner_info_no]);
				cur_corner_info_no++;
				calc_corner_func( f4Position, x,y,z, cornerInfo, pCache);
			}
			cubeCorners[0] = (CornerInfo*)&(cur_corner_info[cube0->cornerInfoNo]);
			index |= ISNEG(cubeCorners[0]->value) >> 31; // LBF

			CubeInfo* const cube1 = &(cur_cubes[y][z1]);
			if(cube1->cornerInfoNo == 0)
			{
				cube1->cornerInfoNo = cur_corner_info_no;
				CornerInfo* cornerInfo = (CornerInfo*)&(cur_corner_info[cur_corner_info_no]);
				cur_corner_info_no++;
				calc_corner_func( MaskedAssign( ( bi32x4 ) f4SIMD_001Mask, f4Position1, f4Position ), x,y,z1, cornerInfo, pCache);
			}
			cubeCorners[1] = (CornerInfo*)&(cur_corner_info[cube1->cornerInfoNo]);
			index |= ISNEG(cubeCorners[1]->value) >> 30; // LBN

			CubeInfo* const cube2 = &(cur_cubes[y1][z]);
			if(cube2->cornerInfoNo == 0)
			{
				cube2->cornerInfoNo = cur_corner_info_no;
				CornerInfo* cornerInfo = (CornerInfo*)&(cur_corner_info[cur_corner_info_no]);	
				cur_corner_info_no++;
				calc_corner_func( MaskedAssign( ( bi32x4 ) f4SIMD_010Mask, f4Position1, f4Position ), x,y1,z, cornerInfo, pCache);
			}
			cubeCorners[2] = (CornerInfo*)&(cur_corner_info[cube2->cornerInfoNo]);
			index |= ISNEG(cubeCorners[2]->value) >> 29; // LTF

			CubeInfo* const cube3 = &(cur_cubes[y1][z1]);
			if(cube3->cornerInfoNo == 0)
			{
				cube3->cornerInfoNo = cur_corner_info_no;
				CornerInfo* cornerInfo = (CornerInfo*)&(cur_corner_info[cur_corner_info_no]);
				cur_corner_info_no++;
				calc_corner_func( MaskedAssign( ( bi32x4 ) f4SIMD_011Mask, f4Position1, f4Position ), x,y1,z1, cornerInfo, pCache);
			}
			cubeCorners[3] = (CornerInfo*)&(cur_corner_info[cube3->cornerInfoNo]);
			index |= ISNEG(cubeCorners[3]->value) >> 28; // * LTN;

			CubeInfo* const cube4 = &(next_cubes[y][z]);
			if(cube4->cornerInfoNo == 0)
			{
				cube4->cornerInfoNo = next_corner_info_no;
				CornerInfo* cornerInfo = (CornerInfo*)&(next_corner_info[next_corner_info_no]);
				next_corner_info_no++;
				calc_corner_func( MaskedAssign( ( bi32x4 ) f4SIMD_100Mask, f4Position1, f4Position ), x1,y,z, cornerInfo, pCache);
			}
			cubeCorners[4] = (CornerInfo*)&(next_corner_info[cube4->cornerInfoNo]);
			index |= ISNEG(cubeCorners[4]->value) >> 27; // RBF;

			CubeInfo* const cube5 = &(next_cubes[y][z1]);
			if(cube5->cornerInfoNo == 0)
			{
				cube5->cornerInfoNo = next_corner_info_no;
				CornerInfo* cornerInfo = (CornerInfo*)&(next_corner_info[next_corner_info_no]);
				next_corner_info_no++;
				calc_corner_func( MaskedAssign( ( bi32x4 ) f4SIMD_101Mask, f4Position1, f4Position ), x1,y,z1, cornerInfo, pCache);
			}
			cubeCorners[5] = (CornerInfo*)&(next_corner_info[cube5->cornerInfoNo]);
			index |= ISNEG(cubeCorners[5]->value) >> 26; // RBN;

			CubeInfo* const cube6 = &(next_cubes[y1][z]);
			if(cube6->cornerInfoNo == 0)
			{
				cube6->cornerInfoNo = next_corner_info_no;
				CornerInfo* cornerInfo = (CornerInfo*)&(next_corner_info[next_corner_info_no]);
				next_corner_info_no++;
				calc_corner_func( MaskedAssign( ( bi32x4 ) f4SIMD_110Mask, f4Position1, f4Position ), x1,y1,z, cornerInfo, pCache);
			}
			cubeCorners[6] = (CornerInfo*)&(next_corner_info[cube6->cornerInfoNo]);
			index |= ISNEG(cubeCorners[6]->value) >> 25; // RTF;

			CubeInfo* const cube7 = &(next_cubes[y1][z1]);
			if(cube7->cornerInfoNo == 0)
			{
				cube7->cornerInfoNo = next_corner_info_no;
				CornerInfo* cornerInfo = (CornerInfo*)&(next_corner_info[next_corner_info_no]);
				next_corner_info_no++;
				calc_corner_func( f4Position1, x1,y1,z1, cornerInfo, pCache );
			}
			cubeCorners[7] = (CornerInfo*)&(next_corner_info[cube7->cornerInfoNo]);
			index |= ISNEG(cubeCorners[7]->value) >> 24; // RTN;


			#ifdef ERROR_CHECK
			if(cur_cubes[y][z].done == false)
			{
				IMP_PRINT("MAJOR ERROR... in render_slice... already done");
				return;
			}

			// error if this cube does not intersect surface
			if(index == 0 || index == 255)
			{
				IMP_PRINT("ERROR: render_slice was called on a cube[%d,%d,%d] with no surface intersection (index=%d)\n", x, y, z, index);
			}
			#endif
			


			/////////////////////////////////////////////////////////////////////////////////////////////////
			/////////////////////// This code enqueues the neighboring cubes for rendering //////////////////
			/////////////////////////////////////////////////////////////////////////////////////////////////

			// polygonize adjacent cubes
			// I might not need to check for x > 0, x < xsize - 1, etc if I can ensure that
			// particles will never go outside dimensions of rendering cube. ** Or if I put sentinel done values

			// TODO: Rather than checking if the item below, to the left, above, etc is done...
			// When I do a cube, I could mark the cubes around it with below done, above done, etc...
			// Then, I don't have to look at surrounding cubes to see which ones have been done.

			// TODO: It might be faster to run this code at the end, after the polygonization
			// since this code might throw out cubeCorners from the cache. Also, it writes
			// into the todo_next table and it might make sense to write into that right before
			// we read back from that. However, it might be better to do this here because
			// cubes might still be in the cache. Also, above, I access all the cubes. Maybe
			// it would be faster to store a pointer to those cubes rather than re-addressing
			// them again here.

			// TODO: It might be better to use ImpConstTable2.h... with the crawlTable
			// and drawTables together, since the current approach has less memory locality.

			// NOTE: With tiling enabled, we need to follow the surface all the way to the
			// edge of the cache. In order to do this, we need to put in checks here to
			// make sure we don't read past the end of the cache. This could be avoided by
			// increasing the margin to (2*cutoffRadius/cubewidth) voxels, but it would
			// waste a tremendous amount of cache.

			char adjacentVoxels = crawlTable[index];
			if((adjacentVoxels & LEFT_FACE) && BOUNDS_CHECK_OFF(x>0 &&)  cur_cubes[y][z].doneBelow == false)
			{
				YZ yz = {y, z};
				prev_slice->todo_list.pushAutoSize(yz);
			}
			if((adjacentVoxels & RIGHT_FACE) && BOUNDS_CHECK_OFF(x<(xsize-1) &&) next_cubes[y][z].doneAbove == false)
			{
				YZ yz = {y, z};
				next_slice->todo_list.pushAutoSize(yz);
			}

			if((adjacentVoxels & BOT_FACE) && BOUNDS_CHECK_OFF(y>0 &&) cur_cubes[y-1][z].doneAbove == false)
			{
				// Make sure that we are in the range for -1 operations.
				Assert(y > 0);

				cur_cubes[y-1][z].doneAbove = true;
				next_cubes[y-1][z].doneBelow = true;
				nStack[nStackSize++] = y-1;
				nStack[nStackSize++] = z;
			}
			if((adjacentVoxels & FAR_FACE) && BOUNDS_CHECK_OFF(z>0 &&) cur_cubes[y][z-1].doneAbove == false)
			{
				// Make sure that we are in the range for -1 operations.
				Assert(z > 0);

				cur_cubes[y][z-1].doneAbove = true;
				next_cubes[y][z-1].doneBelow = true;
				nStack[nStackSize++] = y;
				nStack[nStackSize++] = z-1;
			}
			if((adjacentVoxels & TOP_FACE) && BOUNDS_CHECK_OFF(y<(ysize-1) &&) cur_cubes[y+1][z].doneAbove == false)
			{
				cur_cubes[y1][z].doneAbove = true;
				next_cubes[y1][z].doneBelow = true;
				nStack[nStackSize++] = y1;
				nStack[nStackSize++] = z;
			}
			if((adjacentVoxels & NEAR_FACE) && BOUNDS_CHECK_OFF(z<(zsize-1) &&) cur_cubes[y][z+1].doneAbove == false)
			{
				cur_cubes[y][z1].doneAbove = true;
				next_cubes[y][z1].doneBelow = true;
				nStack[nStackSize++] = y;
				nStack[nStackSize++] = z1;
			}

			//////////////////////////////////////////////////////////////////
			///////////// This is the polygonization code ////////////////////
			//////////////////////////////////////////////////////////////////

			if(polygonize_cube)
			{
				float fx[2], fy[2], fz[2];
				fx[0] = SubFloat( f4Position, 0 ); fx[1] = SubFloat( f4Position1, 0 );
				fy[0] = SubFloat( f4Position, 1 ); fy[1] = SubFloat( f4Position1, 1 );
				fz[0] = SubFloat( f4Position, 2 ); fz[1] = SubFloat( f4Position1, 2 );
				vertexBuffer->beginCube(x);

				static const int edgeOffsets[12][6] = {{0,0,0,2,0,1},{0,0,0,1,0,2},{0,0,1,1,1,3},{0,1,0,2,2,3}, {0,0,0,0,0,4},{0,0,1,0,1,5},{0,1,0,0,2,6},{0,1,1,0,3,7}, {1,0,0,2,4,5},{1,0,0,1,4,6},{1,0,1,1,5,7},{1,1,0,2,6,7}};
				const char * const table = (const char*)&(drawTable[index]);

				// TODO: I can make this loop a little smarter if I have a -2 for
				// end of list, and just exit the loop upon seeing that. Right now,
				// I need two -1s in a row before I exit.
				int counter = 0;
				int edgeno = table[counter];
				while(edgeno != -1)
				{
					// generate vertex position and normal data
					const int *edgeOffset = edgeOffsets[edgeno];
					const int edgea = edgeOffset[3];
					
					CornerInfo* const corner1 = cubeCorners[edgeOffset[4]];

					vbId_t * const id = &(corner1->edges[edgea]);

					if(vertexBuffer->isIdValid(id))
					{
						vertexBuffer->drawVertex(id);
					}
					else
					{
						vertexBuffer->beginVertex(id);
						const CornerInfo* const corner2 = cubeCorners[edgeOffset[5]];
						calc_vertex(fx[edgeOffset[0]], fy[edgeOffset[1]], fz[edgeOffset[2]], edgea, corner1, corner2, vertexBuffer);
						vertexBuffer->endVertex(id);
					}

					counter ++;
					edgeno = table[counter];
				}

				vertexBuffer->endCube();
			}

		} while(nStackSize > 0);
	}

	m_nStackSize = nStackSize;
	// NOTE: Here, I store back the size in CornerInfos back into the
	// corner info arrays (rather than the size in bytes). It is important
	// that there are no returns in the code above that could prevent this
	// from executing.
	cur_slice->corner_info.size = cur_corner_info_no;
	next_slice->corner_info.size = next_corner_info_no;
}





