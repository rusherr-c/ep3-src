//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "blobulator/VertexBuffers/MTIndexTriVertexBuffer.h"
#include "blobulator/Implicit/ImpTiler.h"


void MTIndexTriVertexBuffer::init()
{
}

void MTIndexTriVertexBuffer::beginFrame(void* pRenderContext)
{
	LocalBuffer::beginFrame(pRenderContext);
	/*
	m_pMesh = (*((CMatRenderContextPtr*)pRenderContext))->GetDynamicMesh( true );
	m_pMeshBuilder = new CMeshBuilder();
	
	#if (COUNT_N_FLUSHES == 1)
		m_stat_no_flushes = 0;
	#endif

	#if (COUNT_N_INDECIES == 1)
		m_nIndeciesOutput = 0;
	#endif
	*/
}

void MTIndexTriVertexBuffer::endFrame()
{
	LocalBuffer::endFrame();
	/*
	//s_pMesh doesn't have to be deleted
	if(m_pMeshBuilder)
	{
		delete m_pMeshBuilder;
		m_pMeshBuilder = NULL;
	}
	*/
}

void MTIndexTriVertexBuffer::beginTile(ImpTile* _tile)
{
	Assert(_tile->buffers.size == 0);
	m_tile = _tile;
	m_curTime = 1; // Makes sure edges with time 0 are invalidated
	beginBuffer();
}

void MTIndexTriVertexBuffer::endTile()
{
	endBuffer();
}

void MTIndexTriVertexBuffer::beginBuffer()
{
	m_curBuffer = LocalBuffer::getBuffer();
	m_tile->buffers.pushAutoSize(m_curBuffer);

	m_curBuffer->nIndecies = 0;
	m_curBuffer->nVertices = 0;
	
	m_curVertex = NULL;
	// m_nVerticesOutput = 0;
}

void MTIndexTriVertexBuffer::endBuffer()
{
}

void MTIndexTriVertexBuffer::beginCube(cube_index_t slice)
{
	if (m_curBuffer->nVertices > MAX_VERTICES-24 ||
		m_curBuffer->nIndecies > MAX_INDICES-24)
	{
		endBuffer();
		beginBuffer();
		
		// This should never overflow. For it to overflow, we would have to fill up
		// and flush the mesh builder 65k times.
		m_curTime ++;
		Assert(m_curTime > 1);
	}
}

void MTIndexTriVertexBuffer::endCube() {}

void MTIndexTriVertexBuffer::beginTriStrip() { Assert(false); }

void MTIndexTriVertexBuffer::endTriStrip() { Assert(false); }

void MTIndexTriVertexBuffer::clearId(vbId_t* vertexId)
{
	vertexId->time = 0;
	vertexId->id = 0;
}

bool MTIndexTriVertexBuffer::isIdValid(const vbId_t* vertexId)
{
	return vertexId->time == m_curTime;
}

void MTIndexTriVertexBuffer::drawVertex(const vbId_t* vertexId)
{
	m_curBuffer->indecies[m_curBuffer->nIndecies++] = vertexId->id;

	#if (COUNT_N_INDECIES == 1)
		m_nIndeciesOutput++;
	#endif
}

void MTIndexTriVertexBuffer::beginVertex(vbId_t* out_vertexId)
{
	Assert(!isIdValid(out_vertexId));

	out_vertexId->time = m_curTime;
	out_vertexId->id = m_curBuffer->nVertices;
	m_curVertex = &(m_curBuffer->vertices[m_curBuffer->nVertices++]);
}

void MTIndexTriVertexBuffer::setVertexPosition(Point3D& pos)
{
	m_curVertex->position = pos;
}

void MTIndexTriVertexBuffer::setVertexNormal(Point3D& norm)
{
	m_curVertex->normal = norm;
}

void MTIndexTriVertexBuffer::setVertexTangent(Point3D& tang)
{
	#if (INCLUDE_DETAILS == 1)
    	m_curVertex->tangent = tang;
	#endif
}

void MTIndexTriVertexBuffer::setVertexColor(Point3D& color)
{
	#if (INCLUDE_DETAILS == 1)
		m_curVertex->color = color;
	#endif
}

void MTIndexTriVertexBuffer::setVertexUV(float u, float v)
{
	#if (INCLUDE_DETAILS == 1)
		m_curVertex->u = u;
		m_curVertex->v = v;
	#endif
}


void MTIndexTriVertexBuffer::endVertex(vbId_t* vertexId)
{
	drawVertex(vertexId);
}