//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "blobulator/VertexBuffers/TriStripVertexBuffer.h"


void TriStripVertexBuffer::init()
{
}

void TriStripVertexBuffer::beginFrame(void* pRenderContext)
{
	#if (RENDER_API==VALVE_API)
		m_pMesh = (*((CMatRenderContextPtr*)pRenderContext))->GetDynamicMesh( true );
		m_pMeshBuilder = new CMeshBuilder();
	#endif
	vertices.size = 0;
}

void TriStripVertexBuffer::endFrame()
{
	if(m_pMeshBuilder)
	{
		delete m_pMeshBuilder;
		m_pMeshBuilder = NULL;
	}
}

void TriStripVertexBuffer::beginTile()
{
}

void TriStripVertexBuffer::endTile()
{
}

void TriStripVertexBuffer::beginCube(cube_index_t slice)
{
	// Make sure that at least 12 more vertices can be
	// added to the vertices array. 12 is the most that can
	// be added in a cube, since a cube has 12 edges.
	vertices.ensureCapacity(vertices.size + 12);
}

void TriStripVertexBuffer::endCube() {}

void TriStripVertexBuffer::beginTriStrip()
{
	#if (RENDER_API==VALVE_API)
		// TODO: I think 12 would be enough here!
		#define MAX_SURFACE_ELEMENTS 24
		m_pMeshBuilder->Begin( m_pMesh, MATERIAL_TRIANGLE_STRIP, MAX_SURFACE_ELEMENTS);
		#undef MAX_SURFACE_ELEMENTS
	#else
		glBegin(GL_TRIANGLE_STRIP);
	#endif
}

void TriStripVertexBuffer::endTriStrip()
{
	#if (RENDER_API==VALVE_API)
		m_pMeshBuilder->End();
		m_pMesh->Draw();
	#else
		glEnd();
	#endif
}

void TriStripVertexBuffer::clearId(vbId_t* vertexId)
{
	vertexId->id = m_nullId;
}

bool TriStripVertexBuffer::isIdValid(const vbId_t* vertexId)
{
	return vertexId->id != m_nullId;
}

vbVertex_t* TriStripVertexBuffer::getVertex(const vbId_t* vertexId)
{
	Assert(vertexId->id != m_nullId && vertexId->id < vertices.size); // Debug assert
	
	return &(vertices[vertexId->id]);
}

void TriStripVertexBuffer::drawVertex(const vbId_t* vertexId)
{
	drawVertex(getVertex(vertexId));
}

/*vbVertex_t**/ void TriStripVertexBuffer::beginVertex(vbId_t* out_vertexId)
{
	Assert(vertexId->id == m_nullId);

	out_vertexId->id = vertices.size++;
	//return &(vertices[out_vertexId->id]);
	vertex = &(vertices[out_vertexId->id]);
}

void TriStripVertexBuffer::setVertexPosition(Point3D& pos)
{
	vertex->position = pos;
}

void TriStripVertexBuffer::setVertexNormal(Point3D& norm)
{
	vertex->normal = norm;
}

void TriStripVertexBuffer::setVertexTangent(Point3D& tang)
{
	vertex->tangent = tang;
}

void TriStripVertexBuffer::setVertexColor(Point3D& color)
{
	vertex->color = color;
}

void TriStripVertexBuffer::setVertexUV(float u, float v)
{
	vertex->u = u;
	vertex->v = v;
}


void TriStripVertexBuffer::endVertex(vbId_t* vertexId)
{
	drawVertex(vertex);
}

void TriStripVertexBuffer::drawVertex(vbVertex_t* v)
{
	#if (RENDER_API==VALVE_API)
		m_pMeshBuilder->Position3fv(v->position.p);
		m_pMeshBuilder->Normal3fv(v->normal.p);
		m_pMeshBuilder->Color3fv(v->color.p);	
		m_pMeshBuilder->UserData(v->tangent.p);
		m_pMeshBuilder->TexCoord2f(0, v->u, v->v);
		m_pMeshBuilder->AdvanceVertex();				
	#else
		glColor3fv(v->color.p);
		glNormal3fv(v->normal.p);
		glVertex3fv(v->position.p);
	#endif
}
