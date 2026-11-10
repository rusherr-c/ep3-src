//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include <math.h>
#include "blobulator/util/constants.h"
#include "blobulator/timer.h"
#include "blobulator/util/fasttrunc.h"
#include "blobulator/smartarray.h"
#include "blobulator/point3d.h"
#include "blobulator/implicit/mcrenderer.h"
#include "blobulator/implicit/particlecache.h"
#include "../vertexbuffers/vertexbuffer_inl.h"


///////////////////////////////
// TODO: MOVE TO VERTEX BUFFER
///////////////////////////////
#if (RENDER_API==OPENGL_API)
	//#include <windows.h>
	//#include <GL/gl.h>
#else
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

//#define SPEED_TEST
//#define ERROR_CHECK
//#define OVERFLOW_CHECK


#define USHORT_MAX (0xFFFF)
// A quick way to check if a float is negative.
#define ISNEG(f) (*(int*)(&(f)) & 0x80000000)


///////////////////////////////////////////////
// Forward function declarations
///////////////////////////////////////////////
/*
void changeCubeWidth(float _cubewidth);
void recalculateDimensions();
void changeRadii(float cutoffR, float renderR);
//DOINLINE void DOFASTCALL seed_surface(float x, float y, float z);
DOINLINE void DOFASTCALL seed_surface(Point3D& center);
DOINLINE void DOFASTCALL docrawl(const cube_index_t x, const cube_index_t y, const cube_index_t z);
DOINLINE void DOFASTCALL crawl(const cube_index_t x, const cube_index_t y, const cube_index_t z);
void DOFASTCALL calc_normal(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, CubeInfo* const cube);
//void DOFASTCALL calc_normal2(const  float x, const float y, const float z);
//DOINLINE void DOFASTCALL calcVertex(const CornerInfo* corner1, const CornerInfo* corner2, const int edge, EdgeInfo* const e);
*/

///////////////////////////////
// TODO: MOVE TO VERTEX BUFFER
///////////////////////////////
/*
void defaultModifyVertexFunc(EdgeInfo* const e, float avgTemp);

void (*modifyVertexFunc)(EdgeInfo* const e, float avgTemp) = defaultModifyVertexFunc;
void defaultModifyVertexFunc(EdgeInfo* const e, float avgTemp) {};
void imp_setModifyVertexFunc(void (*modifyVertexPtr)(EdgeInfo* const e, float avgTemp))
{
	modifyVertexFunc = modifyVertexPtr;
}
*/

///////////////////////////////////////////////////////
// Factors that control how isosurface is rendered
///////////////////////////////////////////////////////

//bool tile_mode = false;
//bool use_simd = false;
//bool initialized = false;
//bool per_vertex_color = false;
//unsigned short curTime = 0;
//SmartArray<ImpParticle*> particles;
//MCParticleCache* particleCache;
//bool calc_per_vertex_norm = true;

/*
#if (DRAW_MODE==DRAW_STRIPS)
	TriStripVertexBuffer vertexBuffer;
#elif (DRAW_MODE==DRAW_INDEX_STRIPS)
	IndexTriStripVertexBuffer vertexBuffer;
#elif (DRAW_MODE==DRAW_INDEX_TRIS)
	IndexTriVertexBuffer vertexBuffer;
#elif (DRAW_MODE==DRAW_SORTED_INDEX_TRIS)
	SortedIndexTriVertexBuffer vertexBuffer;
#endif
*/

///////////////////////////////////////////////////////
// Static factors that control how isosurface is rendered
///////////////////////////////////////////////////////

// Cubewidth is the size of a voxel on a side.
// Increase to speed up rendering, reduce quality
#define DEFAULT_CUBEWIDTH 0.8f
float MCRenderer::cubewidth = DEFAULT_CUBEWIDTH;
float MCRenderer::oneOverCubewidth = 1.0f / DEFAULT_CUBEWIDTH;
int MCRenderer::cornerInfoSize = sizeof(CornerInfo);

#if (DYNAMIC_FACTORS == 0)
	#define BLR_CONST const
#else
	#define BLR_CONST
#endif

// Constants for isosurface generation
BLR_CONST float MCRenderer::cutoffR = 3.3f; // 3.3 min... 3.5 better... 3.0 is much faster but doesn't look as good
BLR_CONST float MCRenderer::renderR = 1.3f; // 1.3... or 1.4

// These are derived from the constants above. Used to accelerate
// drawing code
BLR_CONST float MCRenderer::cutoffRSq = MCRenderer::cutoffR * MCRenderer::cutoffR;
BLR_CONST float MCRenderer::scaler = 1.0f / (sqrtf(2.0f) * MCRenderer::cutoffR);
BLR_CONST float MCRenderer::scalerSq = MCRenderer::scaler*MCRenderer::scaler;
BLR_CONST float MCRenderer::scaledRenderRSq = MCRenderer::renderR * MCRenderer::renderR * MCRenderer::scalerSq;
BLR_CONST float MCRenderer::threshold = 0.25f + MCRenderer::scaledRenderRSq * MCRenderer::scaledRenderRSq - MCRenderer::scaledRenderRSq;
BLR_CONST float MCRenderer::oneOverThreshold = 1.0f / MCRenderer::threshold;
BLR_CONST float MCRenderer::addAmt = 0.25f;
BLR_CONST short MCRenderer::fieldCalcSteps = short(MCRenderer::cutoffR * MCRenderer::oneOverCubewidth);

#undef BLR_CONST

bool MCRenderer::tile_mode = false;
int MCRenderer::marginNCubes = 0;
float MCRenderer::marginWidth = 0;

Point3D MCRenderer::outerDimensions = Point3D();
Point3D MCRenderer::innerDimensions = Point3D();


///////////////////////////////////////////////////////
// Factors that control how isosurface is rendered
///////////////////////////////////////////////////////
/*
// Cubewidth is the size of a voxel on a side.
// Increase to speed up rendering, reduce quality
#define DEFAULT_CUBEWIDTH 0.8f
float cubewidth = DEFAULT_CUBEWIDTH;
float oneOverCubewidth = 1.0f / DEFAULT_CUBEWIDTH;

#if (DYNAMIC_FACTORS == 0)
#define BLR_CONST const
#else
#define BLR_CONST
#endif

// Constants for isosurface generation
BLR_CONST float cutoffR = 3.3f; // 3.3 min... 3.5 better... 3.0 is much faster but doesn't look as good
BLR_CONST float renderR = 1.3f; // 1.3... or 1.4

// These are derived from the constants above. Used to accelerate
// drawing code
BLR_CONST float cutoffRSq = cutoffR * cutoffR;
BLR_CONST float scaler = 1.0f / (sqrtf(2.0f) * cutoffR);
BLR_CONST float scalerSq = scaler*scaler;
BLR_CONST float scaledRenderRSq = renderR * renderR * scalerSq;
BLR_CONST float threshold = 0.25f + scaledRenderRSq * scaledRenderRSq - scaledRenderRSq;
BLR_CONST float addAmt = 0.25f;

#undef BLR_CONST
*/

/////////////////////////////////////////////
// Declaration of CubeInfo cache
/////////////////////////////////////////////

// These sizes are how many cubes ImpRenderer can draw
// in any direction. The dimensions of the cache actually
// have to be larger by one in each direction in order to
// fit the samples on the far side of the last cube. These
// dimensions are stored as xsizeP1, ysizeP1 and zsizeP1.


/*
const float halfxsize = float(xsize) / 2.0f;
const float halfysize = float(ysize) / 2.0f;
const float halfzsize = float(zsize) / 2.0f;
*/
/*
int marginNCubes;
float marginWidth;

Point3D offset(0,0,0);

// The outer bounding box bounds the entire volume of the cache
Point3D outerBBMins;
Point3D outerBB2;
Point3D outerDimensions;

// The inner bounding box bounds the entire volume of the cache
// with the exception of the margins.
Point3D innerBB1;
Point3D innerBB2;
Point3D innerDimensions;
*/



/*
// Given an integral coordinate, returns the floating point one
// TODO: Does this really make things faster?
// I can use the oneOverCubewidth trick to make this fast using floating point
float xToFx[xsizeP1];
float yToFy[ysizeP1];
float zToFz[zsizeP1];
*/

///////////////////////////////////////////////////
// Declaration of data structures for todo list
///////////////////////////////////////////////////

// TODO: I have to verify this is big enough.
/*
#if (BLOB_SETTINGS==LAVA_TABLE || BLOB_SETTINGS==BLOB_MONSTER)
	#define MAX_TODO_LIST 50000
#else
	#define MAX_TODO_LIST 6000
#endif

#if (DYNAMIC_ALLOC==1)
	XYZ* todo_list_a;
	XYZ* todo_list_b;
#else
	// TODO: It makes sense to allocate these on the heap
	// because anyway we are always just using pointers to them
	XYZ todo_list_a[MAX_TODO_LIST];
	XYZ todo_list_b[MAX_TODO_LIST];
#endif

int todo_next_size;
XYZ* todo_next;
int todo_current_size;
XYZ* todo_current;
*/




///////////////////////////////////////////////////
// Declaration of cache for corners
///////////////////////////////////////////////////

// TODO: I've seen this go as high as 20,000 when all particles were shown
// the lamp was at full quality, and most things coalesced at the bottom.
// Thus 100,000 is a good, safe limit. Maybe I can make this a resizing array
// to avoid using up lots and lots of memory. Actually, it's ok if we reuse
// something in this array twice... it shouldn't cause the algorithm to crash.
/*
#if (BLOB_SETTINGS==LAVA_TABLE || BLOB_SETTINGS==BLOB_MONSTER)
	#define MAX_CORNERS 100000 // Should be around MAX_TODO_LIST * 4 (is MAX_TODO_LIST*2 because of limitation of short)
#else
	#define MAX_CORNERS 12000 // Should be around MAX_TODO_LIST * 4
#endif

unsigned short corner_info_size = 0;
#if (DYNAMIC_ALLOC==1)
	CornerInfo* corner_info;
#else
	CornerInfo corner_info[MAX_CORNERS];
#endif
*/

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


////////////////////////////////////////////////////////////
// MCRenderer static member functions
////////////////////////////////////////////////////////////

void MCRenderer::setCalcSignFunc(bool DOFASTCALL (*calc_sign_ptr)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, PARTICLE_CACHE_CLASS* pCache))
{
	calc_sign = calc_sign_ptr;
}

void MCRenderer::setCalcSign2Func(void DOFASTCALL (*calc_sign2_ptr)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache))
{
	calc_sign2 = calc_sign2_ptr;
}

void MCRenderer::setCalcCornerFunc(int _cornerInfoSize, void DOFASTCALL (*calc_corner_ptr)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache))
{
	calc_corner = calc_corner_ptr;
	Assert(_cornerInfoSize >= sizeof(CornerInfo));
	// Round up extraCornerDataSize to a multiple of 16 to allow for proper SSE alignment
	// TODO: Ideally I should only require this when compiling with SSE.
	cornerInfoSize = (((_cornerInfoSize + 15) / 16) * 16);
	Assert(cornerInfoSize % 16 == 0);
}

void MCRenderer::setCalcVertexFunc(void DOFASTCALL (*calc_vertex_ptr)(const  float x, const float y, const float z, const int edge, const CornerInfo* corner1, const CornerInfo* corner2, VERTEX_BUFFER_CLASS* vertexBuffer))
{
	calc_vertex = calc_vertex_ptr;
}




void MCRenderer::setCubeWidth(float _cubewidth)
{
	if(_cubewidth == cubewidth) return;
	changeCubeWidth(_cubewidth);
	recalculateDimensions();
	// Note that this will also affect the bounding box of cubes.
}

void MCRenderer::setCutoffR(float _cutoffR)
{
	if(_cutoffR == cutoffR) return;
	changeRadii(_cutoffR, renderR);
	recalculateDimensions();
	// Note that this will also affect the bounding box of cubes.
}

void MCRenderer::setRenderR(float _renderR)
{
	if(_renderR == renderR) return;
	changeRadii(cutoffR, _renderR);
	recalculateDimensions();
	// Note that this will also affect the bounding box of cubes.
}




float MCRenderer::getCubeWidth()
{
	return cubewidth;
}

float MCRenderer::getCutoffR()
{
	return cutoffR;
}

float MCRenderer::getRenderR()
{
	return renderR;
}




int MCRenderer::getMarginNCubes()
{
	return marginNCubes;
}

float MCRenderer::getMarginWidth()
{
	return marginWidth;
}

Point3D& MCRenderer::getInnerDimensions()
{
	return innerDimensions;
}



void MCRenderer::changeCubeWidth(float _cubewidth)
{
	cubewidth = _cubewidth;
	oneOverCubewidth = 1.0f / cubewidth;
}

void MCRenderer::changeRadii(float _cutoffR, float _renderR)
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

void MCRenderer::recalculateDimensions()
{
	fieldCalcSteps = short(cutoffR * oneOverCubewidth);

	float fudgeFactor = 0.001f; // Just in case, so we don't go too small on the marginNCubes, and get killed by rounding error
	marginNCubes = round_up(cutoffR/cubewidth + fudgeFactor); 
	marginWidth = marginNCubes * cubewidth;

	outerDimensions.set(xsize*cubewidth, ysize*cubewidth, zsize*cubewidth);
	innerDimensions.set((xsize-marginNCubes*2)*cubewidth, (ysize-marginNCubes*2)*cubewidth, (zsize-marginNCubes*2)*cubewidth);
}



///////////////////////////////////////////////////
// Function definitions
///////////////////////////////////////////////////

/*
void debugDrawBounds()
{
	#if (RENDER_API == VALVE_API)
		Vector origin(offset[0], offset[1], offset[2]);
		Vector vMins(innerBB1[0], innerBB1[1], innerBB1[2]);
		Vector vMaxs(innerBB2[0], innerBB2[1], innerBB2[2]);
		debugoverlay->AddBoxOverlay( origin, vMins, vMaxs, QAngle( 0, 0, 0 ), 0, 255, 0, 16, 0 );
	#endif
}
*/

MCRenderer::MCRenderer()
	: curTime(0)
//	, initialized(false)
	, offset(0,0,0)
	, n_frames(0)
	, n_tiles(0)
	, memory_total(0)
	, start_time(0)
{
//	if(initialized) return;
//	initialized = true;

	IMP_PRINT("theshold=%f\n",threshold);

	pCache = new PARTICLE_CACHE_CLASS();
	vertexBuffer = new VERTEX_BUFFER_CLASS();
	vertexBuffer->init();

	//particleCache = new MCParticleCache(1.0, 1.66, 2.0, 3.3);
	//particleCache = new MCParticleCache(1.0, 2.0, 2.0, 3.3);
	//particleCache->setMode(false);

	// NOTE: This is a fudge which is necessary
	// because there are several static variables
	// that don't have defaults
	static bool firstTime = true;
	if(firstTime)
	{
		recalculateDimensions();
		firstTime = false;
	}

	#if (DYNAMIC_ALLOC==1)
		cubes = (CubeInfo (*)[ysizeP1][zsizeP1])malloc(sizeof(CubeInfo) * xsizeP1*ysizeP1*zsizeP1);
		//todo_list_a = (XYZ*)malloc(sizeof(XYZ) * MAX_TODO_LIST);
		//todo_list_b = (XYZ*)malloc(sizeof(XYZ) * MAX_TODO_LIST);
		//corner_info = (CornerInfo*)malloc(sizeof(CornerInfo) * MAX_CORNERS);
	#endif

	int i, j, k;

	//changeCubeWidth(cubewidth);

	for(i=0; i<xsizeP1; i++){
		for(j=0; j<ysizeP1; j++){
			for(k=0; k<zsizeP1; k++){
				cubes[i][j][k].valTime = 0;
				cubes[i][j][k].doneTime = 0; //(i == 0 || i >= xsize-2 || j == 0 || j >= ysize-2 || k == 0 || k >= zsize-2); // This is to put sentinel values for the crawling in.
			}
		}
	}

	/*
	for(i = 0; i < MAX_CORNERS; i++)
	{
		corner_info[i].value = 0.0f;
		corner_info[i].normal[0] = 0.0f;
		corner_info[i].normal[1] = 0.0f;
		corner_info[i].normal[2] = 0.0f;

		vertexBuffer->clearId(&(corner_info[i].edges[0]));
		vertexBuffer->clearId(&(corner_info[i].edges[1]));
		vertexBuffer->clearId(&(corner_info[i].edges[2]));

		corner_info[i].interpolants1[0] = 0.0f;
		corner_info[i].interpolants1[1] = 0.0f;
		corner_info[i].interpolants1[2] = 0.0f;
	}
	*/

	/*
	todo_current_size = 0;
	todo_current = todo_list_a;
	todo_next_size = 0;
	todo_next = todo_list_b;
	*/
}

/*
void changeCubeWidth(float _cubewidth)
{
	cubewidth = _cubewidth;
	oneOverCubewidth = 1.0f / cubewidth;
	
	//int i;
	//for(i = 0; i < xsizeP1; i++)
	//{
	//	xToFx[i] = (float(i) - halfxsize) * cubewidth;
	//}

	//for(i = 0; i < ysizeP1; i++)
	//{
	//	yToFy[i] = (float(i) - halfysize) * cubewidth;
	//}

	//for(i = 0; i < zsizeP1; i++)
	//{
	//	zToFz[i] = (float(i) - halfzsize) * cubewidth;
	//}

	recalculateDimensions();
}


void recalculateDimensions()
{
	float fudgeFactor = 0.001f; // Just in case, so we don't go too small on the marginNCubes, and get killed by rounding error
	marginNCubes = round_up(cutoffR/cubewidth + fudgeFactor); 
	marginWidth = marginNCubes * cubewidth;

	outerDimensions.set(xsize*cubewidth, ysize*cubewidth, zsize*cubewidth);
	innerDimensions.set((xsize-marginNCubes*2)*cubewidth, (ysize-marginNCubes*2)*cubewidth, (zsize-marginNCubes*2)*cubewidth);

	outerBBMins = offset-(outerDimensions*0.5f);
	outerBB2 = offset+(outerDimensions*0.5f);

	innerBB1 = offset-(innerDimensions*0.5f);
	innerBB2 = offset+(innerDimensions*0.5f);
}

void imp_setOffset(Point3D& _offset)
{
	offset = _offset;
	recalculateDimensions();
	particleCache->setOffset(offset);
}

void imp_setUseSimd(bool value)
{
	use_simd = value;
}

void imp_setCalcPerVertexNorm(bool value)
{
	calc_per_vertex_norm = value;
}

void imp_setCubeWidth(float _cubewidth)
{
	if(_cubewidth == cubewidth) return;
	changeCubeWidth(_cubewidth);
}

float imp_getCubeWidth()
{
	return cubewidth;
}

void imp_setCutoffR(float _cutoffR)
{
	if(_cutoffR == cutoffR) return;
	changeRadii(_cutoffR, renderR);
}

float imp_getCutoffR()
{
	return cutoffR;
}

void imp_setRenderR(float _renderR)
{
	if(_renderR == renderR) return;
	changeRadii(cutoffR, _renderR);
}

float imp_getRenderR()
{
	return renderR;
}

void changeRadii(float _cutoffR, float _renderR)
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
		addAmt = 0.25f;

		recalculateDimensions();
	#else
		IMP_PRINT("Can't change Radii if factors are not dynamic\n");
	#endif
}

int imp_getMarginNCubes()
{
	return marginNCubes;
}

float imp_getMarginWidth()
{
	return marginWidth;
}

Point3D& imp_getInnerDimensions()
{
	return innerDimensions;
}
*/

void MCRenderer::recalculateBB()
{
	outerBBMins = offset-(outerDimensions*0.5f);
	outerBBMaxs = offset+(outerDimensions*0.5f);

	innerBBMins = offset-(innerDimensions*0.5f);
	innerBBMaxs = offset+(innerDimensions*0.5f);
}

void MCRenderer::setOffset(Point3D const& _offset)
{
	offset = _offset;
	recalculateBB();
	pCache->setOffset(_offset); // This is used by the regular 3D particle cache
}

// Call this to begin a frame
void MCRenderer::beginFrame(bool _tile_mode, void* pRenderContext)
{
	#if (RENDER_API==VALVE_API)
		VPROF_BUDGET( "IsoSurface", "Blobulator" );
	#endif

	#if (SPEED_TEST==1)
		long start_time = clock();
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

	//debugDrawBounds();

	tile_mode = _tile_mode;
	//per_vertex_color = _per_vertex_color;
	
	vertexBuffer->beginFrame(pRenderContext);
}

// Remember to set offset before calling beginTile
void MCRenderer::beginTile(const Point3D & offset)
{
	#if (RENDER_API==VALVE_API)
		VPROF_BUDGET( "IsoSurface", "Blobulator" );
	#endif

	#if (SPEED_TEST==1)
		long start_time = clock();
	#endif
	
	setOffset(offset);

	//debugDrawBounds();
	pCache->beginTile();
	vertexBuffer->beginTile();

	//IMP_PRINT("Cache Size = %d, Edge Size = %d\n", corner_info_size, edge_info_size);
	//corner_info_size = 0;
	corner_info_sa.size = 0;

	curTime++; // Curtime will wrap around every 1000 seconds... around every 2 minutes.
	if(curTime == 0) curTime++; // zero is a bad time since that's what we initialize to. We could also clear the cache at wraparound
}

bool MCRenderer::isParticleWithinBounds(ImpParticle* p)
{
	return (p->center[0] > outerBBMins[0] && p->center[0] < outerBBMaxs[0] &&
			p->center[1] > outerBBMins[1] && p->center[1] < outerBBMaxs[1] &&
			p->center[2] > outerBBMins[2] && p->center[2] < outerBBMaxs[2]);

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
void MCRenderer::addParticle(ImpParticle* p, bool skipBoundsCheck)
{
	if(particles.size >= MAX_PARTICLES-1)
	{
		IMP_PRINT("imp_addParticle: Trying to add more than MAX_PARTICLES particles.\n");
		return;
	}

	if(skipBoundsCheck || isParticleWithinBounds(p))
	{
		particles.pushAutoSize(p);
		pCache->addParticle(p);
	}
}

// Call this to draw the particles
void MCRenderer::endTile()
{
	#if (RENDER_API==VALVE_API)
		VPROF_BUDGET( "IsoSurface", "Blobulator" );
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

	
	memory_total += (sizeof(CubeInfo) * xsizeP1 * ysizeP1 * zsizeP1);
	//memory_total += (corner_info_sa.size * sizeof(CornerInfo));
	memory_total += (corner_info_sa.size * cornerInfoSize);

	n_tiles++;
	if(n_tiles == 100)
	{
		IMP_PRINT("MCRenderer avg max memory usage = %f\n", float(memory_total) / (100.0f * 1000000.0f));
		n_tiles = 0;
		memory_total = 0;
	}

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
}

void MCRenderer::endFrame()
{
	#if (RENDER_API==VALVE_API)
		VPROF_BUDGET( "IsoSurface", "Blobulator" );
	#endif

	vertexBuffer->endFrame();
}

DOINLINE void DOFASTCALL MCRenderer::seed_surface(Point3D& _center)
{
	// TODO: Rather than calling calc normal, might be able to write a simplified routene for seeding
	// TODO: Could possibly make this faster by calling just_calc_normal and then jumping by
	// one box in the direction of the normal... not sure if this would help

	// find cube corresponding to crawl point
	Point3D center = _center - outerBBMins;
	center.mult(oneOverCubewidth);

	int x = round(center[0]); if(x < 0) x = 0; if(x >= xsize) x = xsize-1;
	int y = round(center[1]); if(y < 0) y = 0; if(y >= ysize) y = ysize-1;
	int z = round(center[2]); if(z < 0) z = 0; if(z >= zsize) z = zsize-1;

	// TODO: this is not right considering that the steps are now diagonal.
	int max_iters = fieldCalcSteps + 2; // the radius of the field + 2 (one to account for possible rounding error, and one to allow steping beyond the field)

	bool first = true;

	for(int iter = 0; iter < max_iters; iter++)
	{
		CubeInfo* cube = &(cubes[x][y][z]);
		if(cube->valTime == curTime)
		{
			first = false;

			x -= 1;
			if(x < 0) return;
			continue;

			// We have hit a sample that was taken... that means
			// there is a shell around this particle
			//break;
		}

		float fx = x*cubewidth + outerBBMins[0];
		float fy = y*cubewidth + outerBBMins[1];
		float fz = z*cubewidth + outerBBMins[2];

		cube->valTime = curTime;
		cube->cornerInfoNo = corner_info_sa.size;
		//CornerInfo* cornerInfo = &(corner_info_sa.pushAutoSize());
		corner_info_sa.size *= cornerInfoSize;
		corner_info_sa.ensureAdditionalCapacity(cornerInfoSize);
		CornerInfo* cornerInfo = (CornerInfo*)&(corner_info_sa[cube->cornerInfoNo*cornerInfoSize]);
		corner_info_sa.size = cube->cornerInfoNo + 1;


		calc_corner(x, y, z, fx, fy, fz, cornerInfo, pCache);				

		if(ISNEG(cornerInfo->value)
			#if (DO_BOUNDS_CHECK == 1)
				|| x == 0 || y == 0 || z == 0
			#endif
			)
		{
			if(first && (x > 0 && y > 0 && z > 0))
				IMP_PRINT("First is true\n");
			docrawl(x, y, z);
			return;
		}
		else
		{
			first = false;

			// Diagonal step is fastest
			x -= 1; //y -= 1; z -= 1; 
			fx -= cubewidth; //fy -= cubewidth; fz -= cubewidth;
			Assert(x >= 0 && y >= 0 && z >= 0);
		}
	}


#if 0
	// TODO: Rather than calling calc normal, might be able to write a simplified routene for seeding
	// TODO: Could possibly make this faster by calling just_calc_normal and then jumping by
	// one box in the direction of the normal... not sure if this would help

	// find cube corresponding to crawl point
	/*
	x = round(cpx * oneOverCubewidth + halfxsize); if(x < 0) x = 0; if(x >= xsize) x = xsize-1;
	cpx = xToFx[x];

	y = round(cpy * oneOverCubewidth + halfysize); if(y < 0) y = 0; if(y >= ysize) y = ysize-1;
	cpy = yToFy[y];
		
	z = round(cpz * oneOverCubewidth + halfzsize); if(z < 0) z = 0; if(z >= zsize) z = zsize-1;
	cpz = zToFz[z];
	*/

	Point3D center = _center - outerBBMins;
	center.mult(oneOverCubewidth);


	// TODO: This is not quite right... because if we jump out of the cube on the first step after
	// doing the rounding, we need to have dx set correctly so that we can find the cube that the
	// surface is in. Also, start_x should refer to a corner, not a cube, and it should be allowed
	// to go as high as (xsizeP1-1). Same goes for start_y and start_z;
	int start_x = round(center[0]); if(start_x < 0) start_x = 0; if(start_x >= xsize) start_x = xsize-1;
	int start_y = round(center[1]); if(start_y < 0) start_y = 0; if(start_y >= ysize) start_y = ysize-1;
	int start_z = round(center[2]); if(start_z < 0) start_z = 0; if(start_z >= zsize) start_z = zsize-1;

	// NOTE: I use an int for x, because it might have to go negative. cube_index_t can't go negative.

	int n_dirs_taken = 0;
	bool dirs_taken[8] = {false, false, false, false, false, false, false, false};
	int x = start_x, y = start_y, z = start_z;
	int dx = 0, dy = 0, dz = 0;

	// There are 8 directions, and the max length of every two opposing directions
	// is the max of xsize,ysize and zsize. There are four of these. We should never
	// actually hit max_iters unless there's a bug.
	int max_iters = max(max(xsize,ysize),zsize)*4;

	for(int iter = 0; iter < max_iters; iter++)
	{
		CubeInfo* cube = &(cubes[x][y][z]);
		if(cube->valTime == curTime)
		{
			// We have hit a sample that was taken... that means
			// there is a shell around this particle
			break;
		}

		Assert(x >= 0 && x < xsizeP1 &&
			   y >= 0 && y < ysizeP1 &&
			   z >= 0 && z < zsizeP1);

		// TODO: Typically, the first cubevalue that's calculated here never gets reused...
		// so maybe, I should just make a function that simply calculates the normal here.
		float fx = x*cubewidth + outerBBMins[0];
		float fy = y*cubewidth + outerBBMins[1];
		float fz = z*cubewidth + outerBBMins[2];

		cube->valTime = curTime;
		cube->cornerInfoNo = corner_info_sa.size;
		CornerInfo* cornerInfo = &(corner_info_sa.pushAutoSize());
		//CornerInfo* cornerInfo = &(corner_info[corner_info_size]);
		//corner_info_size = (corner_info_size + 1) % MAX_CORNERS;
		calc_corner(x, y, z, fx, fy, fz, cornerInfo, pCache);		

		if(ISNEG(cornerInfo->value))
		{
			//IMP_PRINT("Iter = %d\n", iter);
			int crawl_x = dx>0 ? x-1 : x;
			int crawl_y = dy>0 ? y-1 : y;
			int crawl_z = dz>0 ? z-1 : z;

			Assert(crawl_x >= 0 && crawl_x < xsize &&
				   crawl_y >= 0 && crawl_y < ysize &&
				   crawl_z >= 0 && crawl_z < zsize);

			docrawl(cube_index_t(crawl_x), cube_index_t(crawl_y), cube_index_t(crawl_z));
			return;
		}
		else
		{
			if(iter == 0)
			{
				// After the initial iteration, go in the direction of decreasing field.
				if(cornerInfo->normal[0] > 0.0f) dx = 1; else dx = -1;
				if(cornerInfo->normal[1] > 0.0f) dy = 1; else dy = -1;
				if(cornerInfo->normal[2] > 0.0f) dz = 1; else dz = -1;

				int dir_id = (dx==1?0x4:0) | (dy==1?0x2:0) | (dz==1?0x1:0);
				dirs_taken[dir_id] = true;
				n_dirs_taken ++;
			}

			x += dx; y += dy; z += dz;

			while(x < 0 || x >= xsizeP1 || y < 0 || y >= ysizeP1 || z < 0 || z >= zsizeP1)
			{
				if(n_dirs_taken >= 8)
				{
					IMP_PRINT("Error in Blobulator: Couldn't seed particle. Continuing.\n");
					return;
				}

				// If you hit an edge, try to reverse direction. If we've already
				// done that direction, try any other direction.
				if(x < 0 || x >= xsizeP1) { dx = -dx; }
				else if(y < 0 || y >= ysizeP1) { dy = -dy; }
				else if(z < 0 || z >= zsizeP1) { dz = -dz; }
			
				// See if we can do the reversed direction, and
				// make sure we don't do a direction we've already done.
				int dir_id = (dx==1?0x4:0) | (dy==1?0x2:0) | (dz==1?0x1:0);
				while(dirs_taken[dir_id])
				{
					dir_id = (dir_id+1)%8;
				}
				dirs_taken[dir_id] = true;
				n_dirs_taken ++;

				dx = (dir_id&0x4 ? 1:-1);
				dy = (dir_id&0x2 ? 1:-1);
				dz = (dir_id&0x1 ? 1:-1);

				// Reset back to start point. And take step
				x=start_x; y=start_y; z=start_z;
				x += dx; y += dy; z += dz;
			}
		}
	}
#endif
}

// This performs a breadth-first traversal of voxels on the isosurface
DOINLINE void DOFASTCALL MCRenderer::docrawl(const cube_index_t x, const cube_index_t y, const cube_index_t z)
{
	todo_next_sa.size = 0;
	todo_current_sa.size = 0; // not really necessary, but doesn't hurt.

	cubes[x][y][z].doneTime = curTime;
	crawl(x, y, z);
	
	while(todo_next_sa.size > 0)
	{
		/*
		XYZ* temp = todo_current;
		todo_current = todo_next;
		todo_next = temp;

		todo_current_size = todo_next_size;
		todo_next_size = 0;
		*/
		todo_current_sa.swapData(todo_next_sa);
		todo_next_sa.size = 0;

		for(int i = 0; i < todo_current_sa.size; i++)
		{
			XYZ* xyz = &(todo_current_sa[i]);
			crawl(xyz->x, xyz->y, xyz->z);
		}
	}
}


// Note: We only force a bounds check when seeding
// if DO_BOUNDS_CHECK is set to 1. calcSign2 will
// also have a forced bounds check in such a case
// which will do appropriate bounds checking on the borders.
#define BOUNDS_CHECK_OFF(a)

// TODO: The MOD operations in this code might be really slow.
// I should check them with optimize it. I should be able to get
// away with just a single mod (anytime remaining size is less than
// equal to eight, we should wrap)

// This function should only be called on cubes that haven't been done.
DOINLINE void DOFASTCALL MCRenderer::crawl(const cube_index_t x, const cube_index_t y, const cube_index_t z)
{
	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////// This code retrieves the field values at the corners from cache //////////////////
	/////////////// or calculates them if they are not in cache. ////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////

	CornerInfo* cubeCorners[8];


	// TODO: If a corner is outside of the area that will get polygonized,
	// then we only need to calculate a sign there, not a field value.
	// I can replace calc_corner with another function if we're in a non-rendered area.
	// (However, I need to be careful with corners that are at the boundary between
	// the rendered and non-rendered areas. I need a clever, fast way to compute all of that.
	// The check can be: a) check if you're outside of cube (using code already written).
	// b) if outside of cube, do a second check to see if you're outside the cube by one element.
	bool polygonize_cube = true;
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


	float fx[2];
	float fy[2];
	float fz[2];

	/*
	fx[0] = (float(x) - halfxsize) * cubewidth;
	fy[0] = (float(y) - halfysize) * cubewidth;
	fz[0] = (float(z) - halfzsize) * cubewidth;
	fx[1] = fx[0] + cubewidth;
	fy[1] = fy[0] + cubewidth;
	fz[1] = fz[0] + cubewidth;
	*/

	// TODO: Using these arrays may be hurting performance... might be better
	// to calculate fx, fy and fz as done above.
	/*
	fx[0] = xToFx[x];
	fx[1] = xToFx[x+1];
	fy[0] = yToFy[y];
	fy[1] = yToFy[y+1];
	fz[0] = zToFz[z];
	fz[1] = zToFz[z+1];
	*/

	fx[0] = (float(x) * cubewidth) + outerBBMins[0];
	fy[0] = (float(y) * cubewidth) + outerBBMins[1];
	fz[0] = (float(z) * cubewidth) + outerBBMins[2];
	fx[1] = fx[0] + cubewidth;
	fy[1] = fy[0] + cubewidth;
	fz[1] = fz[0] + cubewidth;
	
	//corner_info_sa.ensureAdditionalCapacity(8);
	int corner_info_no = corner_info_sa.size;
	// NOTE: I modify the size here to be the true size in bytes of the corner info array.
	// This is necessary for the ensureAdditionalCapacity call to function correctly.
	corner_info_sa.size *= cornerInfoSize;
	corner_info_sa.ensureAdditionalCapacity(8*cornerInfoSize);
	// NOTE: The reason I do this here instead of at the beginning of the function
	// is because the array may bet moved when it is resized by ensureAdditionalCapacity
	byte* const corner_info_ptr = corner_info_sa.a;

	int index = 0;

	// When we access the cubes array in binary order 000,001,010, the compiler optimize the code for us... yay =)
	CubeInfo* cube;
	
	// TODO: Accessing cubes could be much faster if I use offsets wisely
	// TODO: I could store the isneg value as a 0/1 inside cubeCorners->cornerinfo...
	// then I would just have to shift a little in the other direction... no ISNEG necessary
	cube = &(cubes[x][y][z]);
	if(cube->valTime != curTime)
	{
		cube->valTime = curTime;
		cube->cornerInfoNo = corner_info_no;
		CornerInfo* cornerInfo = (CornerInfo*)&(corner_info_ptr[corner_info_no*cornerInfoSize]);
		corner_info_no++;
		calc_corner_func(x,y,z,fx[0], fy[0], fz[0], cornerInfo, pCache);
	}
	cubeCorners[0] = (CornerInfo*)&(corner_info_ptr[cube->cornerInfoNo*cornerInfoSize]);
	index |= ISNEG(cubeCorners[0]->value) >> 31; // LBF
	
	cube = &(cubes[x][y][z+1]);
	if(cube->valTime != curTime)
	{
		cube->valTime = curTime;
		cube->cornerInfoNo = corner_info_no;
		CornerInfo* cornerInfo = (CornerInfo*)&(corner_info_ptr[corner_info_no*cornerInfoSize]);
		corner_info_no++;
		calc_corner_func(x,y,z+1,fx[0], fy[0], fz[1], cornerInfo, pCache);
	}
	cubeCorners[1] = (CornerInfo*)&(corner_info_ptr[cube->cornerInfoNo*cornerInfoSize]);
	index |= ISNEG(cubeCorners[1]->value) >> 30; // LBN

	cube = &(cubes[x][y+1][z]);
	if(cube->valTime != curTime)
	{
		cube->valTime = curTime;
		cube->cornerInfoNo = corner_info_no;
		CornerInfo* cornerInfo = (CornerInfo*)&(corner_info_ptr[corner_info_no*cornerInfoSize]);
		corner_info_no++;
		calc_corner_func(x,y+1,z,fx[0], fy[1], fz[0], cornerInfo, pCache);
	}
	cubeCorners[2] = (CornerInfo*)&(corner_info_ptr[cube->cornerInfoNo*cornerInfoSize]);
	index |= ISNEG(cubeCorners[2]->value) >> 29; // LTF
	
	cube = &(cubes[x][y+1][z+1]);
	if(cube->valTime != curTime)
	{
		cube->valTime = curTime;
		cube->cornerInfoNo = corner_info_no;
		CornerInfo* cornerInfo = (CornerInfo*)&(corner_info_ptr[corner_info_no*cornerInfoSize]);
		corner_info_no++;
		calc_corner_func(x,y+1,z+1,fx[0], fy[1], fz[1], cornerInfo, pCache);
	}
	cubeCorners[3] = (CornerInfo*)&(corner_info_ptr[cube->cornerInfoNo*cornerInfoSize]);
	index |= ISNEG(cubeCorners[3]->value) >> 28; // * LTN;

	cube = &(cubes[x+1][y][z]);
	if(cube->valTime != curTime)
	{
		cube->valTime = curTime;
		cube->cornerInfoNo = corner_info_no;
		CornerInfo* cornerInfo = (CornerInfo*)&(corner_info_ptr[corner_info_no*cornerInfoSize]);
		corner_info_no++;
		calc_corner_func(x+1,y,z,fx[1], fy[0], fz[0], cornerInfo, pCache);
	}
	cubeCorners[4] = (CornerInfo*)&(corner_info_ptr[cube->cornerInfoNo*cornerInfoSize]);
	index |= ISNEG(cubeCorners[4]->value) >> 27; // RBF;

	cube = &(cubes[x+1][y][z+1]);
	if(cube->valTime != curTime)
	{
		cube->valTime = curTime;
		cube->cornerInfoNo = corner_info_no;
		CornerInfo* cornerInfo = (CornerInfo*)&(corner_info_ptr[corner_info_no*cornerInfoSize]);
		corner_info_no++;
		calc_corner_func(x+1,y,z+1,fx[1], fy[0], fz[1], cornerInfo, pCache);
	}
	cubeCorners[5] = (CornerInfo*)&(corner_info_ptr[cube->cornerInfoNo*cornerInfoSize]);
	index |= ISNEG(cubeCorners[5]->value) >> 26; // RBN;

	cube = &(cubes[x+1][y+1][z]);
	if(cube->valTime != curTime)
	{
		cube->valTime = curTime;
		cube->cornerInfoNo = corner_info_no;
		CornerInfo* cornerInfo = (CornerInfo*)&(corner_info_ptr[corner_info_no*cornerInfoSize]);
		corner_info_no++;
		calc_corner_func(x+1,y+1,z,fx[1], fy[1], fz[0], cornerInfo, pCache);
	}
	cubeCorners[6] = (CornerInfo*)&(corner_info_ptr[cube->cornerInfoNo*cornerInfoSize]);
	index |= ISNEG(cubeCorners[6]->value) >> 25; // RTF;

	cube = &(cubes[x+1][y+1][z+1]);
	if(cube->valTime != curTime)
	{
		cube->valTime = curTime;
		cube->cornerInfoNo = corner_info_no;
		CornerInfo* cornerInfo = (CornerInfo*)&(corner_info_ptr[corner_info_no*cornerInfoSize]);
		corner_info_no++;
		calc_corner_func(x+1,y+1,z+1,fx[1], fy[1], fz[1], cornerInfo, pCache);
	}
	cubeCorners[7] = (CornerInfo*)&(corner_info_ptr[cube->cornerInfoNo*cornerInfoSize]);
	index |= ISNEG(cubeCorners[7]->value) >> 24; // RTN;


	#ifdef ERROR_CHECK
	if(dones[x][y][z])
	{
		IMP_PRINT("MAJOR ERROR... in crawl... already done");
		return;
	}

	// error if this cube does not intersect surface
	if(index == 0 || index == 255)
	{
		IMP_PRINT("ERROR: crawl was called on a cube[%d,%d,%d] with no surface intersection (index=%d)\n", x, y, z, index);
	}
	#endif
	

	/////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////// This code enqueues the neighboring cubes for rendering //////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////

	// mark this cube as completed and set the index
	//cubes[x][y][z].doneTime = curTime;

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

	todo_next_sa.ensureAdditionalCapacity(6);

	char adjacentVoxels = crawlTable[index];
	if((adjacentVoxels & FAR_FACE) && BOUNDS_CHECK_OFF(z>0 &&) cubes[x][y][z-1].doneTime != curTime)
	{
		cubes[x][y][z-1].doneTime = curTime;
		XYZ* xyz = &(todo_next_sa.push());
		xyz->x = x; xyz->y = y; xyz->z = z-1;
	}

	if((adjacentVoxels & BOT_FACE) && BOUNDS_CHECK_OFF(y>0 &&) cubes[x][y-1][z].doneTime != curTime)
	{
		cubes[x][y-1][z].doneTime = curTime;
		XYZ* xyz = &(todo_next_sa.push());
		xyz->x = x; xyz->y = y-1; xyz->z = z;
	}

	if((adjacentVoxels & LEFT_FACE) && BOUNDS_CHECK_OFF(x>0 &&) cubes[x-1][y][z].doneTime != curTime)
	{
		cubes[x-1][y][z].doneTime = curTime;
		XYZ* xyz = &(todo_next_sa.push());
		xyz->x = x-1; xyz->y = y; xyz->z = z;
	}

	if((adjacentVoxels & RIGHT_FACE) && BOUNDS_CHECK_OFF(x<(xsize-1) &&) cubes[x+1][y][z].doneTime != curTime)
	{
		cubes[x+1][y][z].doneTime = curTime;
		XYZ* xyz = &(todo_next_sa.push());
		xyz->x = x+1; xyz->y = y; xyz->z = z;
	}

	if((adjacentVoxels & TOP_FACE) && BOUNDS_CHECK_OFF(y<(ysize-1) &&) cubes[x][y+1][z].doneTime != curTime)
	{
		cubes[x][y+1][z].doneTime = curTime;
		XYZ* xyz = &(todo_next_sa.push());
		xyz->x = x; xyz->y = y+1; xyz->z = z;
	}

	if((adjacentVoxels & NEAR_FACE) && BOUNDS_CHECK_OFF(z<(zsize-1) &&) cubes[x][y][z+1].doneTime != curTime)
	{
		cubes[x][y][z+1].doneTime = curTime;
		XYZ* xyz = &(todo_next_sa.push());
		xyz->x = x; xyz->y = y; xyz->z = z+1;
	}

	//////////////////////////////////////////////////////////////////
	///////////// This is the polygonization code ////////////////////
	//////////////////////////////////////////////////////////////////

	if(polygonize_cube)
	{
		vertexBuffer->beginCube(x);

		static const int edgeOffsets[12][6] = {{0,0,0,2,0,1},{0,0,0,1,0,2},{0,0,1,1,1,3},{0,1,0,2,2,3}, {0,0,0,0,0,4},{0,0,1,0,1,5},{0,1,0,0,2,6},{0,1,1,0,3,7}, {1,0,0,2,4,5},{1,0,0,1,4,6},{1,0,1,1,5,7},{1,1,0,2,6,7}};

		int counter, edgeno;

		counter = 0;
		const char *table = (const char*)&(drawTable[index]);

		edgeno = table[counter];
		// TODO: I can make this loop a little smarter if I have a -2 for
		// end of list, and just exit the loop upon seeing that. Right now,
		// I need two -1s in a row before I exit.
		while(edgeno != -1)
		{
			#if (DRAWING_STRIPS==1)
				vertexBuffer->beginTriStrip();
				do
				{
			#endif
					// generate vertex position and normal data
					const int * edgeOffset = edgeOffsets[edgeno];
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

	// NOTE: Here, I store back the size in CornerInfos back into the
	// corner info arrays (rather than the size in bytes). It is important
	// that there are no returns in the code above that could prevent this
	// from executing.
	corner_info_sa.size = corner_info_no;
}