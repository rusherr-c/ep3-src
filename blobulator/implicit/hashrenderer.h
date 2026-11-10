//==== Blobulator. Copyright � 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "impparticle.h"
#include "impdefines.h"
#include "particlecache.h"
#include "../vertexbuffers/vertexbuffer.h"

struct XYZ
{
	cube_index_t x, y, z;
};

#define CUBE_DONE 0x01
#define VAL_DONE 0x02

struct CornerInfo
{
	float normal[3];
	float value;
	vbId_t edges[3];
	CornerInfo* next;
	cube_index_t x, y, z;
	unsigned char dones;
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
struct CubeInfo
{
	unsigned short doneTime;
	unsigned short valTime;
	unsigned short cornerInfoNo;
};

/*
void imp_rendererInit();
void imp_setUseSimd(bool value);
void imp_setCalcPerVertexNorm(bool value);
void imp_setOffset(Point3D& _offset);
float imp_getCubeWidth();
float imp_getCutoffR();
float imp_getRenderR();
int imp_getMarginNCubes();
float imp_getMarginWidth();
Point3D& imp_getInnerDimensions();
void imp_setCubeWidth(float newCubeWidth);
void imp_setCutoffR(float cutoffR);
void imp_setRenderR(float renderR);

///////////////////////////////
// TODO: MOVE TO VERTEX BUFFER
///////////////////////////////
//void imp_setModifyVertexFunc(void (*modifyVertexPtr)(EdgeInfo* const e, float avgTemp));

//void imp_drawSurface(ImpParticle *particles[], int particle_count, float radius, bool tile_mode, bool per_vertex_color, void* pRenderContext = NULL);
void imp_beginFrame(bool _tile_mode, bool _per_vertex_color, void* pRenderContext);
bool imp_isParticleWithinBounds(ImpParticle* p);
void imp_addParticle(ImpParticle* p, bool skipBoundsCheck=false);
void imp_endFrame();
*/


class ImpTile;

class HashRenderer
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


	static float getCubeWidth();
	static float getCutoffR();
	static float getRenderR();

	static void setCubeWidth(float newCubeWidth);
	static void setCutoffR(float cutoffR);
	static void setRenderR(float renderR);

	static int getMarginNCubes();
	static float getMarginWidth();
	static Point3D& getInnerDimensions();

	static void setCalcSignFunc(bool DOFASTCALL (*calc_sign_ptr)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, PARTICLE_CACHE_CLASS* pCache));
	static void setCalcSign2Func(void DOFASTCALL (*calc_corner_ptr)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache));
	static void setCalcCornerFunc(int cornerInfoSize, void DOFASTCALL (*calc_corner_ptr)(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache));
	static void setCalcVertexFunc(void DOFASTCALL (*calc_vertex_ptr)(const  float x, const float y, const float z, const int edge, const CornerInfo* corner1, const CornerInfo* corner2, VERTEX_BUFFER_CLASS* vertexBuffer));

private:
	static bool tile_mode;
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

	HashRenderer();
	void rendererInit();
	void setOffset(Point3D const& _offset);
	void beginFrame(bool _tile_mode, void* pRenderContext);
	void beginTile(const Point3D & offset);
	bool isParticleWithinBounds(ImpParticle* p);
	void addParticle(ImpParticle* p, bool skipBoundsCheck=false);
	void endTile();
	void endFrame();

	// NOTE: These are dummy functions as HashRenderer does not support slices
	void setMaxNoSlicesToDraw(int no) { }
	int getMaxNoSlicesToDraw() { return -1; }
	int getLastSliceDrawn() { return -1; }

private:
	int n_tiles;
	int n_frames;
	int64 memory_total;
	int64 start_time;

	//unsigned short curTime;
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

	static const int xsize = RENDERER_CACHE_XSIZE;
	static const int ysize = RENDERER_CACHE_YSIZE;
	static const int zsize = RENDERER_CACHE_ZSIZE;
	static const int xsizeP1 = xsize+1;
	static const int ysizeP1 = ysize+1;
	static const int zsizeP1 = zsize+1;

	/*
	#if (DYNAMIC_ALLOC==1)
		CubeInfo (*cubes)[ysizeP1][zsizeP1];
	#else
		// NOTE: I might want to add +1s for field values at corners.
		CubeInfo cubes[xsizeP1][ysizeP1][zsizeP1];
	#endif
	*/

	SmartArray<CornerInfo*> slots;

	SmartArray<XYZ> todo_current_sa;
	SmartArray<XYZ> todo_next_sa;

	int corner_info_cache_used;
	SmartArray<CornerInfo*> corner_info_cache;

	void recalculateBB();
	FORCEINLINE bool findCorner(const cube_index_t x, const cube_index_t y, const cube_index_t z, CornerInfo** out_corner);
	//__forceinline bool findOrAllocateCorner(const cube_index_t x, const cube_index_t y, const cube_index_t z, CornerInfo** out_corner);
	DOINLINE void DOFASTCALL seed_surface(Point3D& _center);
	DOINLINE void DOFASTCALL docrawl(const cube_index_t x, const cube_index_t y, const cube_index_t z);
	DOINLINE void DOFASTCALL crawl(const cube_index_t x, const cube_index_t y, const cube_index_t z);
};


