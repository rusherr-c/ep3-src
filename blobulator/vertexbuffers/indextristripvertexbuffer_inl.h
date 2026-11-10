//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "blobulator/VertexBuffers/IndexTriStripVertexBuffer.h"


void IndexTriStripVertexBuffer::init()
{
}

void IndexTriStripVertexBuffer::beginFrame(void* pRenderContext)
{
	m_pMesh = (*((CMatRenderContextPtr*)pRenderContext))->GetDynamicMesh( true );
	m_pMeshBuilder = new CMeshBuilder();

	#if (COUNT_N_INDECIES)
		m_nIndeciesOutput = 0;
	#endif
	m_curTime = 1; // Makes sure all edges are invalidated

	beginMesh();
}

void IndexTriStripVertexBuffer::endFrame()
{
	endMesh();
	#if (COUNT_N_INDECIES)
		IMP_PRINT("%d indecies written\n", m_nIndeciesOutput);
	#endif

	//s_pMesh doesn't have to be deleted
	if(m_pMeshBuilder)
	{
		delete m_pMeshBuilder;
		m_pMeshBuilder = NULL;
	}
}

void IndexTriStripVertexBuffer::beginTile(ImpTile* tile)
{
}

void IndexTriStripVertexBuffer::endTile()
{
}

void IndexTriStripVertexBuffer::beginMesh()
{
	m_pMeshBuilder->Begin( m_pMesh, MATERIAL_TRIANGLE_STRIP, MAX_VERTICES, MAX_INDICES);

	// Throw in this vertex in the beginning so that winding is correct.
	// This is necessary because at the beginning of every tri-strip, I output
	// a vertex to make a degenerate tri connecting to the previous vertex.
	// This will throw off the winding unless we do this below.
	m_pMeshBuilder->Position3f(0.0f, 0.0f, 0.0f);
	m_pMeshBuilder->Normal3f(1.0f, 0.0f, 0.0f);
	m_pMeshBuilder->Color3f(0.0f, 0.0f, 0.0f);
	float dummy[4] = {1.0f, 0.0f, 0.0f, 1.0f};
	m_pMeshBuilder->UserData(dummy);
	m_pMeshBuilder->TexCoord2f(0, 0.0f, 0.0f);
	m_pMeshBuilder->AdvanceVertex();

	m_pMeshBuilder->FastIndex(0);
	m_nVerticesOutput = 1;

	#if (COUNT_N_INDECIES)
		m_nIndeciesOutput++;
	#endif
}

void IndexTriStripVertexBuffer::endMesh()
{
	// Throw in another vertex to make sure winding
	// is still correct
	m_pMeshBuilder->FastIndex(m_nVerticesOutput-1);
	m_pMeshBuilder->End();
	m_pMesh->Draw();
	#if (COUNT_N_INDECIES)
		m_nIndeciesOutput++;
	#endif
}

void IndexTriStripVertexBuffer::beginCube(cube_index_t slice)
{
	if (m_pMeshBuilder->NumVertices() > MAX_VERTICES-24 ||
		m_pMeshBuilder->NumIndices() > MAX_INDICES-24)
	{
		endMesh();
		beginMesh();
		
		// This should never overflow. For it to overflow, we would have to fill up
		// and flush the mesh builder 65k times.
		m_curTime ++;
		Assert(m_curTime > 1);
	}
}

void IndexTriStripVertexBuffer::endCube() {}

void IndexTriStripVertexBuffer::beginTriStrip()
{
	m_nVerticesInStrip = 0;
}

void IndexTriStripVertexBuffer::endTriStrip()
{
	m_pMeshBuilder->FastIndex(m_lastVertexId);// Add end to degenerate tri list
	#if (COUNT_N_INDECIES)
		m_nIndeciesOutput++;
	#endif
	// If an odd number of vertices drawn, must add another to fix winding
	// This could also be done by modifying the vertex tables or by
	// drawing triangles instead of triangle strips.
	if(m_nVerticesInStrip & 0x1)
	{
		m_pMeshBuilder->FastIndex(m_lastVertexId);
 		#if (COUNT_N_INDECIES)
			m_nIndeciesOutput++;
		#endif
	}
}

void IndexTriStripVertexBuffer::clearId(vbId_t* vertexId)
{
	vertexId->time = 0;
	vertexId->id = 0;
}

bool IndexTriStripVertexBuffer::isIdValid(const vbId_t* vertexId)
{
	return vertexId->time == m_curTime;
}

void IndexTriStripVertexBuffer::drawVertex(const vbId_t* vertexId)
{
	if(m_nVerticesInStrip == 0)
	{
		m_pMeshBuilder->FastIndex(vertexId->id);
		#if (COUNT_N_INDECIES)
			m_nIndeciesOutput++;
		#endif
	}
	m_pMeshBuilder->FastIndex(vertexId->id);
	#if (COUNT_N_INDECIES)
		m_nIndeciesOutput++;
	#endif
	m_nVerticesInStrip++;
	m_lastVertexId = vertexId->id;
}

/*vbVertex_t**/void IndexTriStripVertexBuffer::beginVertex(vbId_t* out_vertexId)
{
	Assert(!isIdValid(out_vertexId));

	out_vertexId->time = m_curTime;
	out_vertexId->id = m_nVerticesOutput;
	m_nVerticesOutput++;
	//return &s_staticVertex;
}

void IndexTriStripVertexBuffer::setVertexPosition(Point3D& pos)
{
	m_pMeshBuilder->Position3fv(pos.p);
}

void IndexTriStripVertexBuffer::setVertexNormal(Point3D& norm)
{
	m_pMeshBuilder->Normal3fv(norm.p);
}

void IndexTriStripVertexBuffer::setVertexTangent(Point3D& tang)
{
	m_pMeshBuilder->UserData(tang.p);
}

void IndexTriStripVertexBuffer::setVertexColor(Point3D& color)
{
	m_pMeshBuilder->Color3fv(color.p);
}

void IndexTriStripVertexBuffer::setVertexUV(float u, float v)
{
	m_pMeshBuilder->TexCoord2f(0, u, v);
}


void IndexTriStripVertexBuffer::endVertex(vbId_t* vertexId)
{
	m_pMeshBuilder->AdvanceVertex();

	drawVertex(vertexId);
}