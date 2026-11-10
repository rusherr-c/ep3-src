//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include "blobulator/vertexbuffers/mtindextrivertexbuffer.h"

#if (MT_ENABLED == 1)

//#include "blobulator/Implicit/ImpTiler.h"
#include "blobulator/implicit/syncro.h"

//vbVertex_t MTIndexTriVertexBuffer::s_staticVertex;
/*
SmartArray<vbBuffer_t*> MTIndexTriVertexBuffer::unusedBuffers;
IMesh* MTIndexTriVertexBuffer::m_pMesh = NULL;
CMeshBuilder* MTIndexTriVertexBuffer::m_pMeshBuilder = NULL;
*/


MTIndexTriVertexBuffer::MTIndexTriVertexBuffer(void)
	: m_curVertex(NULL)
	, m_curBuffer(NULL)
	, m_curTime(1)
	, m_nVerticesOutput(0)
	#if (COUNT_N_INDECIES)
		, m_nIndeciesOutput(0)
	#endif
{
}

MTIndexTriVertexBuffer::~MTIndexTriVertexBuffer(void)
{
}

/*
void MTIndexTriVertexBuffer::beginFrame(void* pRenderContext)
{
	m_pMesh = (*((CMatRenderContextPtr*)pRenderContext))->GetDynamicMesh( true );
	m_pMeshBuilder = new CMeshBuilder();
	
	#if (COUNT_N_FLUSHES == 1)
		m_stat_no_flushes = 0;
	#endif

	#if (COUNT_N_INDECIES == 1)
		m_nIndeciesOutput = 0;
	#endif
}

void MTIndexTriVertexBuffer::endFrame()
{
	//s_pMesh doesn't have to be deleted
	if(m_pMeshBuilder)
	{
		delete m_pMeshBuilder;
		m_pMeshBuilder = NULL;
	}
}

void MTIndexTriVertexBuffer::flushTileBuffers(ImpTile* tile)
{
	for(int i = 0; i < tile->buffers.size; i++)
	{
		beginMesh();

		vbBuffer_t* buf = tile->buffers[i];
		
		for(int j = 0; j < buf->nVertices; j++)
		{
			vbVertex_t* vert = &(buf->vertices[j]);

			m_pMeshBuilder->Position3fv(vert->position.p);
			m_pMeshBuilder->Normal3fv(vert->normal.p);
			#if (INCLUDE_DETAILS == 1)
				m_pMeshBuilder->Color3fv(vert->color.p);
				m_pMeshBuilder->UserData(vert->tangent.p);
				m_pMeshBuilder->TexCoord2f(0, vert->u, vert->v);
			#endif
			m_pMeshBuilder->AdvanceVertex();
		}

		for(int j = 0; j < buf->nIndecies; j++)
		{
			m_pMeshBuilder->FastIndex(buf->indecies[j]);
		}

		endMesh();
	}

	#if (COUNT_N_INDECIES == 1)
		IMP_PRINT("%d indecies written\n", m_nIndeciesOutput);
	#endif

	#if (COUNT_N_FLUSHES == 1)
		if(m_stat_no_flushes > 1)
		{
			IMP_PRINT("%d extra flushes in MTIndexTriVertexBuffer\n", m_stat_no_flushes-1);
		}
	#endif
}

void MTIndexTriVertexBuffer::beginMesh()
{
	m_pMeshBuilder->Begin( m_pMesh, MATERIAL_TRIANGLES, MAX_VERTICES, MAX_INDICES);
	//m_nVerticesOutput = 0;
}

void MTIndexTriVertexBuffer::endMesh()
{
	m_pMeshBuilder->End();
	m_pMesh->Draw();
	#if (COUNT_N_FLUSHES == 1)
		m_stat_no_flushes ++;
	#endif
}

void MTIndexTriVertexBuffer::releaseTileBuffers(ImpTile* tile)
{
	BlrGetLock(&taskListLock);

	while(tile->buffers.size > 0)
	{
		unusedBuffers.pushAutoSize(tile->buffers.pop());
	}

	BlrReleaseLock(&taskListLock);
}

vbBuffer_t* MTIndexTriVertexBuffer::getBuffer()
{
	vbBuffer_t* buffer;
	
	BlrGetLock(&taskListLock);

	if(unusedBuffers.size > 0)
	{
		buffer = unusedBuffers.pop();
	}
	else
	{
		buffer = new vbBuffer_t();
	}

	BlrReleaseLock(&taskListLock);

	return buffer;
}
*/


#endif // if(MT_ENABLED == 1)