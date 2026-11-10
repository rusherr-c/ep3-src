//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "impparticle.h"
#include "impdefines.h"
#include "particlecache.h"
#include "../vertexbuffers/vertexbuffer.h"

/*
extern float cubewidth;
extern float oneOverCubewidth;

#if (DYNAMIC_FACTORS == 0)
#define BLR_CONST const
#else
#define BLR_CONST
#endif

// Constants for isosurface generation
extern BLR_CONST float cutoffR;
extern BLR_CONST float renderR;

// These are derived from the constants above. Used to accelerate
// drawing code
extern BLR_CONST float cutoffRSq;
extern BLR_CONST float scaler;
extern BLR_CONST float scalerSq;
extern BLR_CONST float scaledRenderRSq;
extern BLR_CONST float threshold;
extern BLR_CONST float addAmt;
extern BLR_CONST short fieldCalcSteps;

#undef BLR_CONST
*/


struct XYZ
{
	cube_index_t x, y, z;
};

struct YZ
{
	cube_index_t y, z;
};

// TODO: Might be faster to put edges[3] and cube_index_t first
// because these are the first things read when rendering. Especially
// if I return value +/- from calc_normal.
// TODO: Move to half-float.
struct CornerInfo
{
	float normal[3];
	float value;
	vbId_t edges[3];
	cube_index_t y, z, pad1, pad2;
	// NOTE: I believe the padding is not necessary since I round
	// up the size of CornerInfo to a multiple of 16 bytes
};

// TODO: I can make this smaller by storing
// just doneTime and a unsigned short index into
// the cornerinfo array here. So, the cube info
// will be half the size it is now....
// to make this happen, I need to do back clearing---
// store back-pointers in CornerInfo. I should also
// have back-pointers in EdgeInfo to clear corners.
// I could also clear the EdgeInfos when the CornerInfos
// that point to them are erased... The back-clearing
// will save lots of space, but might be slow to go
// back over the entire array.

// TODO: Is it safe to say that if a value has been computed,
// the cube is done? No, but for a cube to be done, its value
// has to have been computed in this cycle. Thus, all I need
// is valTime, plus a single bit for done. However, this does
// not work with the current loop which in effect does a breadth
// first search. This could be made to work with depth first.

// TODO: Investigate if breadth first is actually better than
// depth first. It might be worse because it starts to randomly
// skip around after a while.

// The best thing might be to do depth-first in single slices.
// This way, we will do loops within slices.
#pragma warning(push)
#pragma warning( disable: 4201 )
union CubeInfo
{
	struct
	{
		unsigned short cornerInfoNo;
		bool doneAbove;
		bool doneBelow;
	};
	unsigned int everything;
};
#pragma warning(pop)


/*
float imp_getCubeWidth();
float imp_getCutoffR();
float imp_getRenderR();

void imp_setCubeWidth(float newCubeWidth);
void imp_setCutoffR(float cutoffR);
void imp_setRenderR(float renderR);

int imp_getMarginNCubes();
float imp_getMarginWidth();
*/

/////////////////////////////////////////////
// Declaration of CubeInfo cache
/////////////////////////////////////////////

// These sizes are how many cubes ImpRenderer can draw
// in any direction. The dimensions of the cache actually
// have to be larger by one in each direction in order to
// fit the samples on the far side of the last cube. These
// dimensions are stored as xsizeP1, ysizeP1 and zsizeP1.

const int xsize = RENDERER_CACHE_XSIZE;
const int ysize = RENDERER_CACHE_YSIZE;
const int zsize = RENDERER_CACHE_ZSIZE;
const int xsizeP1 = xsize+1;
const int xsizeP2 = xsize+2;
const int ysizeP1 = ysize+1;
const int zsizeP1 = zsize+1;

// TODO: With the sweep algorithm, I can now store cube->done inside of
//       the vertices. The other option may be to store it as a single
//       bit inside the sweep plane. If the cube is not done, 

// Note: With the current arrangement, I keep corner values and dones
// in the same corners array. In typical sweeps, three slices of dones
// and two slices of corner values are needed. Thus, because the corner
// values and dones are in the same array, I have extra corners in
// memory that are not necessary. However, this arrangement is probably
// better for cache locality. Thus, it makes sense to keep it this way
// unless memory is very scarce.

typedef CubeInfo (*slice_corners_t)[zsizeP1];

// NOTE: Instead of an array of CornerInfos, Slice_t carries an SSE aligned
// byte array. That allows expansion of CornerInfo to carry user defined data.
// Also, for convenience, the size in this array actually carries the size of
// CornerInfos, not the size in bytes. However, whenever the array needs resizing,
// I store the size in bytes back in, resize, and then store the size in CornerInfos
// back for consistency.
struct CornerInfoMax_t
{
	byte m_maxSize[96];
};

struct Slice_t
{
	slice_corners_t corners;
	SmartArray<byte, true, 0> corner_info;
	SmartArray<YZ, false, 0> todo_list;
	SmartArray<YZ> seed_list;
	#if (SEED_MULTISLICE == 1 || SEED_SIDEWAYS == 1)
		SmartArray<YZ> try_seed_list;
	#endif
};

class ImpTile;

class SweepRenderer
{
// Static stuff
public:
	// These variables are public so that UserFunctions can access them
	static float cubewidth;
	static float oneOverCubewidth;
	static int cornerInfoSize;

	#if (DYNAMIC_FACTORS == 0)
		#define BLR_CONST const
	#else
		#define BLR_CONST
	#endif

	// Constants for isosurface generation
	static BLR_CONST float cutoffR;
	static BLR_CONST float renderR;

	// These are derived from the constants above. Used to accelerate
	// drawing code
	static BLR_CONST float cutoffRSq;
	static BLR_CONST float scaler;
	static BLR_CONST float scalerSq;
	static BLR_CONST float scaledRenderRSq;
	static BLR_CONST float threshold;
	static BLR_CONST float oneOverThreshold;
	static BLR_CONST float addAmt;
	static BLR_CONST short fieldCalcSteps;

	#undef BLR_CONST


	static float GetCubeWidth();
	static float GetCutoffR();
	static float GetRenderR();

	static void SetCubeWidth(float newCubeWidth);
	static void SetCutoffR(float cutoffR);
	static void SetRenderR(float renderR);

	static int GetMarginNCubes();
	static float GetMarginWidth();
	static Point3D& GetInnerDimensions();

	static void setCalcSignFunc(bool DOFASTCALL (*calc_sign_ptr)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, PARTICLE_CACHE_CLASS* pCache));
	static void setCalcSign2Func(void DOFASTCALL (*calc_corner_ptr)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache));
	static void setCalcCornerFunc(int cornerInfoSize, void DOFASTCALL (*calc_corner_ptr)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache));
	static void setCalcVertexFunc(void DOFASTCALL (*calc_vertex_ptr)(const  float x, const float y, const float z, const int edge, const CornerInfo* corner1, const CornerInfo* corner2, VERTEX_BUFFER_CLASS* vertexBuffer));

	static void setCalcCornerSIMDFunc(int cornerInfoSize, void DOFASTCALL (*calc_corner_ptr)(fltx4 f4Pos, const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache));

private:
	static bool tile_mode;
	static bool m_bUseSIMDLoop;
	static int marginNCubes;
	static float marginWidth;

	static Point3D outerDimensions;
	static Point3D innerDimensions;

	static void changeRadii(float _cutoffR, float _renderR);
	static void changeCubeWidth(float _cubewidth);
	static void recalculateDimensions();

// Non-static stuff
public:
	// These two are public so that UserFunctions can access them
	PARTICLE_CACHE_CLASS* pCache;
	VERTEX_BUFFER_CLASS* vertexBuffer;

	SweepRenderer();
	void rendererInit();
	void SetOffset(Point3D const& _offset);
	void BeginFrame(bool _tile_mode, void* pRenderContext);
	void BeginTile(const Point3D & offset);
	bool isParticleWithinBounds(ImpParticle & p);
	void AddParticle(ImpParticle & p, bool skipBoundsCheck=false);
	void EndTile();
	void EndFrame();

	void setMaxNoSlicesToDraw(int no = -1) { maxNoSlicesToDraw = no; }
	int getMaxNoSlicesToDraw() { return maxNoSlicesToDraw; }
	int getLastSliceDrawn() { return last_slice_drawn; }

private:
	#if (IMP_PROFILE == 1)
		int n_tiles;
		int n_frames;
		__int64 memory_total;
		__int64 start_time;
	#endif

	int maxNoSlicesToDraw;
	int last_slice_drawn;
	bool polygonizationEnabled;


	//bool initialized;

	Point3D offset;

	// The outer bounding box bounds the entire volume of the cache
	Point3D outerBBMins;
	Point3D outerBBMaxs;

	// The inner bounding box bounds the entire volume of the cache
	// with the exception of the margins.
	Point3D innerBBMins;
	Point3D innerBBMaxs;

	SmartArray<ImpParticle*> particles;

	Slice_t slices[xsizeP2];
	SmartArray<slice_corners_t> unused_slice_corners;
	SmartArray<SmartArray<byte, true, 0> > unused_corner_info;
	SmartArray<SmartArray<YZ, false, 0> > unused_todo_lists;

	// For stats on amount of allocated memory
	int n_alloced_slice_corners;
	int n_alloced_slice_corner_infos;
	int n_alloced_slice_todo_lists;

	#if(BREADTH_FIRST == 1)
		SmartArray<cube_index_t> recursion_stack_dest;
	#else
		#define recursion_stack_dest recursion_stack_src
	#endif
	SmartArray<cube_index_t> recursion_stack_src;
	cube_index_t m_nStack[2048];
	int m_nStackSize;

	void recalculateBB();
	DOINLINE void DOFASTCALL seed_surface(Point3D& _center);
	void allocSliceCorners(Slice_t* slice);
	void deallocSliceCorners(Slice_t* slice);
	void allocSliceTodoList(Slice_t* slice);
	void deallocSliceTodoList(Slice_t* slice);
	void render_slices();
	DOINLINE void DOFASTCALL render_slice(const cube_index_t x, Slice_t* prev_slice, Slice_t* cur_slice, Slice_t* next_slice);
	DOINLINE void DOFASTCALL render_slice_simd(const cube_index_t x, Slice_t* prev_slice, Slice_t* cur_slice, Slice_t* next_slice);
};




