//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "blobulator/VertexBuffers/SortedIndexTriVertexBuffer.h"


void SortedIndexTriVertexBuffer::init()
{
}

void SortedIndexTriVertexBuffer::beginFrame(void* pRenderContext)
{
	m_pMesh = (*((CMatRenderContextPtr*)pRenderContext))->GetDynamicMesh( true );
	m_pMeshBuilder = new CMeshBuilder();

	#if (COUNT_N_INDECIES)
		m_nIndeciesOutput = 0;
	#endif
}

void SortedIndexTriVertexBuffer::endFrame()
{
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

void SortedIndexTriVertexBuffer::beginTile(ImpTile* tile)
{
}

void SortedIndexTriVertexBuffer::endTile()
{
	beginMesh();

	unsigned short curTime = 1;
	unsigned short nVerticesOutput = 0;
	unsigned short nIndeciesOutput = 0;

	for(int i = 0; i < RENDERER_CACHE_XSIZE; i++)
	{
		SmartArray<unsigned int>* slice = &(sliceIndexes[i]);		

		for(int j = 0; j < slice->size; j++)
		{
			unsigned int id = (*slice)[j];
			vbVertex_t* vert = &(vertices[id]);

			if(vert->time != curTime)
			{
				m_pMeshBuilder->Position3fv(vert->position.p);
				m_pMeshBuilder->Normal3fv(vert->normal.p);
				m_pMeshBuilder->Color3fv(vert->color.p);
				m_pMeshBuilder->UserData(vert->tangent.p);
				m_pMeshBuilder->TexCoord2f(0, vert->u, vert->v);
				m_pMeshBuilder->AdvanceVertex();

				vert->time = curTime;
				vert->index = nVerticesOutput++;
			}

			m_pMeshBuilder->FastIndex(vert->index);
			nIndeciesOutput ++;
			#if (COUNT_N_INDECIES)
				m_nIndeciesOutput++;
			#endif
	
			// Should be a power of three to avoid ending a mesh
			// while strill drawing a triangle
			if(nIndeciesOutput % 24 == 0)
			{
				if (m_pMeshBuilder->VertexCount() > MAX_VERTICES-30 ||
					m_pMeshBuilder->IndexCount() > MAX_INDICES-30)
				{
					endMesh();
					beginMesh();

					nVerticesOutput = 0;
					nIndeciesOutput = 0;
					// This should never overflow. For it to overflow, we would have to fill up
					// and flush the mesh builder 65k times.
					curTime ++;
					Assert(curTime > 1);
				}
			}
		}

		slice->size = 0;
	}

	vertices.size = 0;

	endMesh();
}

void SortedIndexTriVertexBuffer::beginMesh()
{
	m_pMeshBuilder->Begin( m_pMesh, MATERIAL_TRIANGLES, MAX_VERTICES, MAX_INDICES);
}

void SortedIndexTriVertexBuffer::endMesh()
{
	m_pMeshBuilder->End();
	m_pMesh->Draw();
}

void SortedIndexTriVertexBuffer::beginCube(cube_index_t slice)
{
	vertices.ensureCapacity(vertices.size + 12);
	curSlice = &(sliceIndexes[slice]);
	curSlice->ensureCapacity(curSlice->size + 12*3);
}

void SortedIndexTriVertexBuffer::endCube() {}

void SortedIndexTriVertexBuffer::beginTriStrip() { Assert(false); }

void SortedIndexTriVertexBuffer::endTriStrip() { Assert(false); }

void SortedIndexTriVertexBuffer::clearId(vbId_t* vertexId)
{
	vertexId->id = 0;
}

bool SortedIndexTriVertexBuffer::isIdValid(const vbId_t* vertexId)
{
	return vertexId->id != 0;
}

void SortedIndexTriVertexBuffer::drawVertex(const vbId_t* vertexId)
{
	curSlice->push(vertexId->id);
}

/*vbVertex_t**/ void SortedIndexTriVertexBuffer::beginVertex(vbId_t* out_vertexId)
{
	Assert(!isIdValid(out_vertexId));
	Assert(vertices.size > 0);

	// TODO: There is a bug here: The first id is 0. However,
	// an id of 0 is treated as an invalid id, so vertex 0 keeps
	// getting recalculated.
	// This should activate the vertices.size > 0 alert.

	out_vertexId->id = vertices.size;
	curSlice->push(out_vertexId->id);
	//return &(vertices[vertices.size++]);
	vertex = &(vertices[vertices.size++]);
}

void SortedIndexTriVertexBuffer::setVertexPosition(Point3D& pos)
{
	vertex->position = pos;
}

void SortedIndexTriVertexBuffer::setVertexNormal(Point3D& norm)
{
	vertex->normal = norm;
}

void SortedIndexTriVertexBuffer::setVertexTangent(Point3D& tang)
{
	vertex->tangent = tang;
}

void SortedIndexTriVertexBuffer::setVertexColor(Point3D& color)
{
	vertex->color = color;
}

void SortedIndexTriVertexBuffer::setVertexUV(float u, float v)
{
	vertex->u = u;
	vertex->v = v;
}


void SortedIndexTriVertexBuffer::endVertex(vbId_t* vertexId)
{
	vertex->time = 0;
}