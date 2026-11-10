//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "../point3d.h"
#include "../smartarray.h"
#include "../implicit/impdefines.h"
#include "materialsystem/imesh.h"

class ImpTile;

// If it wasn't for degenerate tris, a vertex would be indexed four times on average.
// However, if the vertex is the first in a tri-strip, it will be indexed twice.
// If it is the last, it will be indexed twice, and possibly three times in the case
// there is an odd number of indecies (to preserve winding). For this reason, I have
// six times as many indecies as verticies. There is probably more indecies generated
// per vertex, but it's ok since the limit is the number of indecies, so we shouldn't
// run out of vertices. This is also true when drawing tris.
#define MAX_INDICES 32000
#define MAX_VERTICES (MAX_INDICES/6)

// TODO: Would be nice to size this according to the type
// of vertex buffer we were using. For example, a single USHORT
// might be good enough for a TriStripVertexBuffer
struct vbId_t
{
	unsigned short time;
	unsigned short id;
};


class IndexTriStripVertexBuffer
{
public:
	IndexTriStripVertexBuffer(void);
	~IndexTriStripVertexBuffer(void);

	FORCEINLINE void init();

	FORCEINLINE void beginFrame(void* pRenderContext);
	FORCEINLINE void endFrame();

	FORCEINLINE void beginTile(ImpTile* tile);
	FORCEINLINE void endTile();

	FORCEINLINE void beginCube(cube_index_t slice);
	FORCEINLINE void endCube();

	FORCEINLINE void beginTriStrip();
	FORCEINLINE void endTriStrip();

	FORCEINLINE static void clearId(vbId_t* vertexId);
	FORCEINLINE bool isIdValid(const vbId_t* vertexId);
	FORCEINLINE void drawVertex(const vbId_t* vertexId);

	FORCEINLINE /*vbVertex_t**/ void beginVertex(vbId_t* out_vertexId);
	FORCEINLINE void setVertexPosition(Point3D& pos);
	FORCEINLINE void setVertexNormal(Point3D& norm);
	FORCEINLINE void setVertexTangent(Point3D& tang);
	FORCEINLINE void setVertexColor(Point3D& color);
	FORCEINLINE void setVertexUV(float u, float v);
	FORCEINLINE void endVertex(vbId_t* vertexId);

private:
	FORCEINLINE void beginMesh();
	FORCEINLINE void endMesh();

	//static vbVertex_t s_staticVertex;

	unsigned short m_curTime;
	unsigned short m_nVerticesOutput;

	unsigned short m_lastVertexId;
	unsigned char m_nVerticesInStrip;

	IMesh* m_pMesh;
	CMeshBuilder* m_pMeshBuilder;

	#if (COUNT_N_INDECIES)
		int m_nIndeciesOutput;
	#endif
};

