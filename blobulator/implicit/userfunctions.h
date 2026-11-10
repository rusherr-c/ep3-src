//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "impdefines.h"
#include "imprenderer.h"


bool DOFASTCALL calcSign(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, PARTICLE_CACHE_CLASS* pCache);
bool DOFASTCALL calcSignSIMD(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, PARTICLE_CACHE_CLASS* pCache);

void DOFASTCALL calcSign2(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache);


#define CALC_CORNER_NORMAL_CI_SIZE sizeof(CornerInfo)
void DOFASTCALL calcCornerNormal(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache);
void DOFASTCALL calcVertexNormal(const  float x, const float y, const float z, const int edge, const CornerInfo* corner1, const CornerInfo* corner2, VERTEX_BUFFER_CLASS* vertexBuffer);
void DOFASTCALL calcVertexNormalDebugColor(const  float x, const float y, const float z, const int edge, const CornerInfo* corner1, const CornerInfo* corner2, VERTEX_BUFFER_CLASS* vertexBuffer);


struct CornerInfoWithOneInterpolant : public CornerInfo
{
	Point3D interpolants1;
};
#define CALC_CORNER_NORMAL_COLOR_CI_SIZE sizeof(CornerInfoWithOneInterpolant)
void DOFASTCALL calcCornerNormalColor(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache);
void DOFASTCALL calcCornerNormalHiFreqColor(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache);
void DOFASTCALL calcVertexNormalNColor(const  float x, const float y, const float z, const int edge, const CornerInfo* corner1, const CornerInfo* corner2, VERTEX_BUFFER_CLASS* vertexBuffer);


struct CornerInfoWithTwoInterpolants : public CornerInfoWithOneInterpolant
{
	Point3D interpolants2;
};
#define CALC_CORNER_NORMAL_COLOR_TAN_NO_UV_CI_SIZE sizeof(CornerInfoWithTwoInterpolants)
void DOFASTCALL calcCornerNormalColorTanNoUV(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache);
void DOFASTCALL calcVertexNormalNColorTanNoUV(const  float x, const float y, const float z, const int edge, const CornerInfo* corner1, const CornerInfo* corner2, VERTEX_BUFFER_CLASS* vertexBuffer);


struct CornerInfoWithFourInterpolants : public CornerInfoWithTwoInterpolants
{
	Point3D interpolants3;
	Point3D interpolants4;
};
#define CALC_CORNER_NORMAL_COLOR_UV_TAN_CI_SIZE sizeof(CornerInfoWithFourInterpolants)
void DOFASTCALL calcCornerNormalColorUVTan(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache);
void DOFASTCALL calcCornerNormalColorUVTanForBlob(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache);
void DOFASTCALL calcVertexNormalNColorUVTan(const  float x, const float y, const float z, const int edge, const CornerInfo* corner1, const CornerInfo* corner2, VERTEX_BUFFER_CLASS* vertexBuffer);
void DOFASTCALL calcVertexNormalNColorUVTanForBlob(const  float x, const float y, const float z, const int edge, const CornerInfo* corner1, const CornerInfo* corner2, VERTEX_BUFFER_CLASS* vertexBuffer);

void DOFASTCALL calcVertexNormalNTexCoord4(const float x, const float y, const float z, const int edge, const CornerInfo* corner1, const CornerInfo* corner2, VERTEX_BUFFER_CLASS* vertexBuffer);


void DOFASTCALL calcCornerNormalColorUVTan_SIMD(fltx4 f4Pos, const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache);
void DOFASTCALL calcSign2_SIMD(fltx4 f4Pos, const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache);

