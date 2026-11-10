//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "../point3d.h"
#include "../implicit/impdefines.h"
#include "vertexinfo.h"

class ImpTile;

// If it wasn't for degenerate tris, a vertex would be indexed four times on average.
// However, if the vertex is the first in a tri-strip, it will be indexed twice.
// If it is the last, it will be indexed twice, and possibly three times in the case
// there is an odd number of indecies (to preserve winding). For this reason, I have
// six times as many indecies as vertices. There is probably more indecies generated
// per vertex, but it's ok since the limit is the number of indecies, so we shouldn't
// run out of vertices. This is also true when drawing tris.
#define MAX_INDICES 32000
//#define MAX_VERTICES (MAX_INDICES/6)
#define MAX_VERTICES (MAX_INDICES/4)

class IMesh;
class CMeshBuilder;

class IndexTriVertexBuffer
{
public:
	IndexTriVertexBuffer(void);
	~IndexTriVertexBuffer(void);

	FORCEINLINE void init();

	FORCEINLINE void beginFrame(void* pRenderContext);
	FORCEINLINE void endFrame();

	FORCEINLINE void beginTile();
	FORCEINLINE void endTile();

	FORCEINLINE void beginCube(cube_index_t slice);
	FORCEINLINE void endCube();

	FORCEINLINE void beginTriStrip();
	FORCEINLINE void endTriStrip();

	FORCEINLINE void static clearId(vbId_t* vertexId);
	FORCEINLINE void static clear3Ids(vbId_t* vertexId);
	FORCEINLINE bool isIdValid(const vbId_t* vertexId);
	FORCEINLINE void drawVertex(const vbId_t* vertexId);

	FORCEINLINE void beginVertex(vbId_t* out_vertex_id);
	FORCEINLINE void setVertexPosition(Point3D& pos);
	FORCEINLINE void setVertexPosition(fltx4& f4Position);
	FORCEINLINE void setVertexNormal(Point3D& norm);
	FORCEINLINE void setVertexNormal( const fltx4 &f4Normal );

	FORCEINLINE void setVertexTangent(Point3D& tang);
	FORCEINLINE void setVertexColor(Point3D& color);
	FORCEINLINE void setVertexUV(int nStage, float u, float v);
	FORCEINLINE void setVertexUV(int nStage, float s, float t, float u);
	FORCEINLINE void setVertexUV(int nStage, float s, float t, float u, float w);
	FORCEINLINE void setVertexUV(int nStage, const fltx4 &f4stuv);
	FORCEINLINE void endVertex(vbId_t* vertexId);

	FORCEINLINE int GetCurTime() const;
	FORCEINLINE int GetCurrentVerticesOutput() const;
	FORCEINLINE void * GetInternalPositionBuffer() const;
	FORCEINLINE int GetVertexSize() const;
	FORCEINLINE unsigned short * GetCurrentIndexBuffer() const;
	FORCEINLINE int GetIndexOffset() const;
	FORCEINLINE void UpdateInternals(int numberOfVerticesCreated, int numberOfVerticesDrawn);

	FORCEINLINE unsigned short * IndexBuffer_InterlockedAdd(int numberOfIndices);

	FORCEINLINE void beginMesh();
	FORCEINLINE void endMesh();
private:

	#if (IMP_PROFILE == 1)
		int frame_no;
		int64 cubes_drawn;
		int64 indecies_drawn;
	#endif

	//static vbVertex_t s_staticVertex;

	unsigned short m_curTime;
	unsigned short m_nVerticesOutput;

	IMesh* m_pMesh;
	CMeshBuilder* m_pMeshBuilder;

	#if (COUNT_N_INDECIES)
		int m_nIndeciesOutput;
	#endif

	unsigned short m_stat_no_flushes;
};

