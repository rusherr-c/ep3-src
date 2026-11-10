//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "../point3d.h"
#include "../smartarray.h"
#include "../implicit/impdefines.h"

#if (RENDER_API==OPENGL_API)
	#include <windows.h>
	#include <GL/gl.h>
#else
	#include "materialsystem/imesh.h"
#endif

#define USHORT_MAX (0xFFFF)

// TODO: Would be nice to only have necessary fields.
// I could use just an array of Point3Ds and compute offsets
// based on the number of fields used.
struct vbVertex_t
{
	Point3D position;
	Point3D normal;
	Point3D color;
	Point3D tangent;
	float u, v;
};

// TODO: Would be nice to size this according to the type
// of vertex buffer we were using. For example, a single USHORT
// might be good enough for a TriStripVertexBuffer
struct vbId_t
{
	unsigned short time;
	unsigned short id;
};



// TODO: Once a vertex is used by four
// cubes, it can be erased.
class TriStripVertexBuffer
{
public:
	TriStripVertexBuffer(void);
	~TriStripVertexBuffer(void);

	FORCEINLINE void init();

	FORCEINLINE void beginFrame(void* pRenderContext);
	FORCEINLINE void endFrame();

	FORCEINLINE void beginTile();
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
	inline vbVertex_t* getVertex(const vbId_t* vertexId);
	inline void drawVertex(vbVertex_t* vertex);

	SmartArray<vbVertex_t> vertices;
	vbVertex_t* vertex;

	#if (RENDER_API==VALVE_API)
		IMesh* m_pMesh;
		CMeshBuilder* m_pMeshBuilder;
	#endif

	const static unsigned short m_nullId = USHORT_MAX;
};

