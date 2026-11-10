//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include "blobulator/vertexbuffers/localbuffer.h"

#if (MT_ENABLED == 1)

#include "blobulator/implicit/imptiler.h"
#include "blobulator/implicit/syncro.h"

SmartArray<vbBuffer_t*> LocalBuffer::unusedBuffers;
IMesh* LocalBuffer::m_pMesh = NULL;
CMeshBuilder* LocalBuffer::m_pMeshBuilder = NULL;


LocalBuffer::LocalBuffer(void)
{
}

LocalBuffer::~LocalBuffer(void)
{
}

void LocalBuffer::beginFrame(void* pRenderContext)
{
	m_pMesh = (*((CMatRenderContextPtr*)pRenderContext))->GetDynamicMesh( true );
	m_pMeshBuilder = new CMeshBuilder();
}

void LocalBuffer::endFrame()
{
	//s_pMesh doesn't have to be deleted
	if(m_pMeshBuilder)
	{
		delete m_pMeshBuilder;
		m_pMeshBuilder = NULL;
	}
}

void LocalBuffer::flushTileBuffers(ImpTile* tile)
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
}

void LocalBuffer::beginMesh()
{
	m_pMeshBuilder->Begin( m_pMesh, MATERIAL_TRIANGLES, MAX_VERTICES, MAX_INDICES);
	//m_nVerticesOutput = 0;
}

void LocalBuffer::endMesh()
{
	m_pMeshBuilder->End();
	m_pMesh->Draw();
	#if (COUNT_N_FLUSHES == 1)
		m_stat_no_flushes ++;
	#endif
}

void LocalBuffer::releaseTileBuffers(ImpTile* tile)
{
	#if (MT_ENABLED == 1)
		BlrGetLock(&taskListLock);
	#endif

	while(tile->buffers.size > 0)
	{
		unusedBuffers.pushAutoSize(tile->buffers.pop());
	}

	#if (MT_ENABLED == 1)
		BlrReleaseLock(&taskListLock);
	#endif
}

vbBuffer_t* LocalBuffer::getBuffer()
{
	vbBuffer_t* buffer;
	
	#if (MT_ENABLED == 1)
		BlrGetLock(&taskListLock);
	#endif

	if(unusedBuffers.size > 0)
	{
		buffer = unusedBuffers.pop();
	}
	else
	{
		buffer = new vbBuffer_t();
		for(int i = 0; i < MAX_VERTICES; i++)
		{
			buffer->vertices[i].position.clear();
			buffer->vertices[i].normal.clear();
			buffer->vertices[i].color.clear();
			buffer->vertices[i].tangent.clear();
			if( (int)((void*)&(buffer->vertices[i].tangent)) % 4 != 0)
			{
				IMP_PRINT("ALIGNMENT ERROR\n");
			}
			buffer->vertices[i].u = 0.0f;
			buffer->vertices[i].v = 0.0f;
		}
		for(int i = 0; i < MAX_INDICES; i++)
		{
			buffer->indecies[i] = 0;
		}
	}
	
	#if (MT_ENABLED == 1)
		BlrReleaseLock(&taskListLock);
	#endif

	return buffer;
}

#endif // if(MT_ENABLED == 1)