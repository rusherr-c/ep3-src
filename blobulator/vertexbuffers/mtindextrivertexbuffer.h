//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "../implicit/impdefines.h"

#if (MT_ENABLED == 1)

#include "../point3d.h"
#include "../smartarray.h"
#include "localbuffer.h"
#include "materialsystem/imesh.h"

class ImpTile;

/*
// If it wasn't for degenerate tris, a vertex would be indexed four times on average.
// However, if the vertex is the first in a tri-strip, it will be indexed twice.
// If it is the last, it will be indexed twice, and possibly three times in the case
// there is an odd number of indecies (to preserve winding). For this reason, I have
// six times as many indecies as verticies. There is probably more indecies generated
// per vertex, but it's ok since the limit is the number of indecies, so we shouldn't
// run out of vertices. This is also true when drawing tris.
#define MAX_INDICES 32000
#define MAX_VERTICES (MAX_INDICES/6)


#define INCLUDE_DETAILS 1

// TODO: Would be nice to only have necessary fields.
// I could use just an array of Point3Ds and compute offsets
// based on the number of fields used.
struct vbVertex_t
{
	Point3D position;
	Point3D normal;
	#if (INCLUDE_DETAILS==1)
		Point3D color;
		Point3D tangent;
		float u, v;
	#endif
};

struct vbBuffer_t
{
	unsigned short nIndecies;
	unsigned short nVertices;
	unsigned short indecies[MAX_INDICES];
	vbVertex_t vertices[MAX_VERTICES];
};
*/


// TODO: Would be nice to size this according to the type
// of vertex buffer we were using. For example, a single USHORT
// might be good enough for a TriStripVertexBuffer
struct vbId_t
{
	unsigned short time;
	unsigned short id;
};

class MTIndexTriVertexBuffer
{
/*
public:
	static void flushTileBuffers(ImpTile* tile);
	static void releaseTileBuffers(ImpTile* tile);

	static void beginFrame(void* pRenderContext);
	static void endFrame();

private:
	static SmartArray<vbBuffer_t*> unusedBuffers;
	static IMesh* m_pMesh;
	static CMeshBuilder* m_pMeshBuilder;
	
	static vbBuffer_t* getBuffer();

	static void beginMesh();
	static void endMesh();
*/

public:
	MTIndexTriVertexBuffer(void);
	~MTIndexTriVertexBuffer(void);

	__forceinline void init();

	__forceinline void beginFrame(void* pRenderContext);
	__forceinline void endFrame();

	__forceinline void beginBuffer();
	__forceinline void endBuffer();

	__forceinline void beginTile(ImpTile* tile);
	__forceinline void endTile();

	__forceinline void beginCube(cube_index_t slice);
	__forceinline void endCube();

	__forceinline void beginTriStrip();
	__forceinline void endTriStrip();

	__forceinline void static clearId(vbId_t* vertexId);
	__forceinline bool isIdValid(const vbId_t* vertexId);
	__forceinline void drawVertex(const vbId_t* vertexId);

	__forceinline void beginVertex(vbId_t* out_vertex_id);
	__forceinline void setVertexPosition(Point3D& pos);
	__forceinline void setVertexNormal(Point3D& norm);
	__forceinline void setVertexTangent(Point3D& tang);
	__forceinline void setVertexColor(Point3D& color);
	__forceinline void setVertexUV(float u, float v);
	__forceinline void endVertex(vbId_t* vertexId);

private:
	ImpTile* m_tile;

	vbVertex_t* m_curVertex;
	vbBuffer_t* m_curBuffer;

	unsigned short m_curTime;
	unsigned short m_nVerticesOutput;

	#if (COUNT_N_INDECIES)
		int m_nIndeciesOutput;
	#endif

	unsigned short m_stat_no_flushes;
};

#endif