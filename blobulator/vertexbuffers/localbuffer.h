//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "../implicit/impdefines.h"
#if (MT_ENABLED == 1)

#include "../point3d.h"
#include "../smartarray.h"
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
//#define MAX_VERTICES (MAX_INDICES/6)
#define MAX_VERTICES (MAX_INDICES/2)


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

class LocalBuffer
{
public:
	LocalBuffer();
	~LocalBuffer();

	static void flushTileBuffers(ImpTile* tile);
	static void releaseTileBuffers(ImpTile* tile);

	static void beginFrame(void* pRenderContext);
	static void endFrame();

	static vbBuffer_t* getBuffer();

private:
	static SmartArray<vbBuffer_t*> unusedBuffers;
	static IMesh* m_pMesh;
	static CMeshBuilder* m_pMeshBuilder;

	static void beginMesh();
	static void endMesh();
};

#endif // if (MT_ENABLED == 1)