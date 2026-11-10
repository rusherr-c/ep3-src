//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

// oliviern:
//	In the current code we are disabling any rendering ferature if pRenderContext is NULL.
//	This adds a bunch of if test everywhere.
//  We should a better way to do that. 
//	Also we provide fake information to simulate execution (like 48 bytes for the vertex buffer, etc...).

#pragma once

#include "blobulator/vertexbuffers/indextrivertexbuffer.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"

void IndexTriVertexBuffer::init()
{
}

void IndexTriVertexBuffer::beginFrame(void* pRenderContext)
{
	if (pRenderContext != NULL)
	{
		m_pMesh = (*((CMatRenderContextPtr*)pRenderContext))->GetDynamicMesh( true );
		m_pMeshBuilder = new CMeshBuilder();
	}
	else
	{
		m_pMesh = NULL;
		m_pMeshBuilder = NULL;
	}
	
	m_curTime = 1; // Makes sure all edges are invalidated

	#if (COUNT_N_FLUSHES == 1)
		m_stat_no_flushes = 0;
	#endif

	beginMesh();
	#if (COUNT_N_INDECIES == 1)
		m_nIndeciesOutput = 0;
	#endif
}

void IndexTriVertexBuffer::endFrame()
{
	endMesh();
	#if (COUNT_N_INDECIES == 1)
		IMP_PRINT("%d indecies written\n", m_nIndeciesOutput);
	#endif

	#if (IMP_PROFILE == 1)
		frame_no++;
		if(frame_no == 100)
		{
			IMP_PRINT("IndexTriVertexBuffer avg cubes per frame = %f, avg tris per frame = %f\n", float(cubes_drawn) / 100.0f, float(indecies_drawn) / 300.0f);
			frame_no = 0;
			cubes_drawn = 0;
			indecies_drawn = 0;
		}
	#endif

	//s_pMesh doesn't have to be deleted
	if(m_pMeshBuilder)
	{
		delete m_pMeshBuilder;
		m_pMeshBuilder = NULL;
	}

	#if (COUNT_N_FLUSHES == 1)
		if(m_stat_no_flushes > 1)
		{
			IMP_PRINT("%d extra flushes in IndexTriVertexBuffer\n", m_stat_no_flushes-1);
		}
	#endif
}

void IndexTriVertexBuffer::beginTile()
{
}

void IndexTriVertexBuffer::endTile()
{
}

void IndexTriVertexBuffer::beginMesh()
{
	if (m_pMeshBuilder != NULL)
	{
		m_pMeshBuilder->Begin( m_pMesh, MATERIAL_TRIANGLES, MAX_VERTICES, MAX_INDICES);
	}
	m_nVerticesOutput = 0;
}

void IndexTriVertexBuffer::endMesh()
{
	if (m_pMeshBuilder != NULL)
	{
		m_pMeshBuilder->End();
		m_pMesh->Draw();
	}
	#if (COUNT_N_FLUSHES == 1)
		m_stat_no_flushes ++;
	#endif
}

void IndexTriVertexBuffer::beginCube(cube_index_t slice)
{
	#if (IMP_PROFILE == 1)
		cubes_drawn++;
	#endif

	if (m_pMeshBuilder != NULL)
	{
		// oliviern: In some difficult to reproduce cases, the blob is displayed incorrectly (on PC, X360 and PS3).
		// This could be related to the index offset being too big, with m_pMeshBuilder->IndexCount() + m_pMeshBuilder->GetIndexOffset() > 32000 or 64000.
		// Depending of the exact size of the internal dynamic index buffer.
		// On PC, m_pMeshBuilder->GetIndexOffset() returns a number between 0 and 2000.
		// On X360, it returns 0 (like on PS3?).
		// As I was not able to reproduce the issue, my assumption is that in some very rare cases the index offset returned by m_pMeshBuilder
		// gets very big, overflowing the index that can be used in the index buffer. Added a tentative fix.
		// Either this issue has always existed or a change in the dynamic index buffer or usage introduced this issue.
		if ((m_pMeshBuilder->VertexCount() > MAX_VERTICES - 24) ||
			((m_pMeshBuilder->IndexCount() + m_pMeshBuilder->GetIndexOffset()) > MAX_INDICES-24))
		{
			endMesh();
			beginMesh();
			
			// This should never overflow. For it to overflow, we would have to fill up
			// and flush the mesh builder 65k times.
			m_curTime ++;
			Assert(m_curTime > 1);

		}
	}
}

void IndexTriVertexBuffer::endCube() {}

void IndexTriVertexBuffer::beginTriStrip() { Assert(false); }

void IndexTriVertexBuffer::endTriStrip() { Assert(false); }

void IndexTriVertexBuffer::clearId(vbId_t* vertexId)
{
	vertexId->time = 0;
	vertexId->id = 0;
}
void IndexTriVertexBuffer::clear3Ids(vbId_t* vertexId)
{
	uint32 *pOut = reinterpret_cast<uint32 *>(vertexId);
	*pOut++ = 0;
	*pOut++ = 0;
	*pOut++ = 0;
}

bool IndexTriVertexBuffer::isIdValid(const vbId_t* vertexId)
{
	return vertexId->time == m_curTime;
}

void IndexTriVertexBuffer::drawVertex(const vbId_t* vertexId)
{
	if (m_pMeshBuilder != NULL)
	{
		m_pMeshBuilder->FastIndex(vertexId->id);
	}
	#if (IMP_PROFILE == 1)
		indecies_drawn++;
	#endif

	#if (COUNT_N_INDECIES == 1)
		m_nIndeciesOutput++;
	#endif
}

void IndexTriVertexBuffer::beginVertex(vbId_t* out_vertexId)
{
	Assert(!isIdValid(out_vertexId));

	out_vertexId->time = m_curTime;
	out_vertexId->id = m_nVerticesOutput;
	m_nVerticesOutput++;
}

void IndexTriVertexBuffer::setVertexPosition(Point3D& pos)
{
	if (m_pMeshBuilder != NULL)
	{
		m_pMeshBuilder->Position3fv(pos.p);
	}
}

void IndexTriVertexBuffer::setVertexPosition(fltx4& f4Position)
{
	if (m_pMeshBuilder != NULL)
	{
		m_pMeshBuilder->Position3f(f4Position);
	}
}

void IndexTriVertexBuffer::setVertexNormal(Point3D& norm)
{
	if (m_pMeshBuilder != NULL)
	{
		m_pMeshBuilder->Normal3fv(norm.p);
	}
}

void IndexTriVertexBuffer::setVertexNormal( const fltx4 &f4Normal )
{
	if (m_pMeshBuilder != NULL)
	{
		m_pMeshBuilder->Normal3f( f4Normal );
	}
}

void IndexTriVertexBuffer::setVertexTangent(Point3D& tang)
{
	if (m_pMeshBuilder != NULL)
	{
		// TODO: WHOA THE USER DATA FUNCTION USES MEMCPY... OUCH!!!
		m_pMeshBuilder->UserData(tang.p);
	}
}

void IndexTriVertexBuffer::setVertexColor(Point3D& color)
{
	if (m_pMeshBuilder != NULL)
	{
	m_pMeshBuilder->Color3fv(color.p);
	}
}

void IndexTriVertexBuffer::setVertexUV(int nStage, float u, float v)
{
	Assert(nStage==0); // if we use more than one stage we need to change the AdvanceVertexF<> call
	if (m_pMeshBuilder != NULL)
	{
		m_pMeshBuilder->TexCoord2f(nStage, u, v);
	}
}

void IndexTriVertexBuffer::setVertexUV(int nStage, float s, float t, float u)
{
	Assert(nStage==0); // if we use more than one stage we need to change the AdvanceVertexF<> call
	if (m_pMeshBuilder != NULL)
	{
		m_pMeshBuilder->TexCoord3f(nStage, s, t, u);
	}
}

void IndexTriVertexBuffer::setVertexUV(int nStage, float s, float t, float u, float w)
{
	Assert(nStage==0); // if we use more than one stage we need to change the AdvanceVertexF<> call
	if (m_pMeshBuilder != NULL)
	{
		m_pMeshBuilder->TexCoord4f(nStage, s, t, u, w);
	}
}

void IndexTriVertexBuffer::setVertexUV(int nStage, const fltx4 &f4stuv)
{
	Assert(nStage==0); // if we use more than one stage we need to change the AdvanceVertexF<> call
	if (m_pMeshBuilder != NULL)
	{
		m_pMeshBuilder->TexCoord4f(nStage, f4stuv);
	}
}

int IndexTriVertexBuffer::GetCurTime() const
{
	return m_curTime;
}

int IndexTriVertexBuffer::GetCurrentVerticesOutput() const
{
	return m_nVerticesOutput;
}

void * IndexTriVertexBuffer::GetInternalPositionBuffer() const
{
	if (m_pMeshBuilder != NULL)
	{
		// Gets the vertex size, so GetVertexDataPtr returns the expected value.
		int vertexSize = m_pMeshBuilder->VertexSize();
		return m_pMeshBuilder->GetVertexDataPtr(vertexSize);
	}
	else
	{
		return NULL;
	}
}

int IndexTriVertexBuffer::GetVertexSize() const
{
	if (m_pMeshBuilder != NULL)
	{
		return m_pMeshBuilder->VertexSize();
	}
	else
	{
		return 48;		// Use a fake size
	}
}

unsigned short * IndexTriVertexBuffer::GetCurrentIndexBuffer() const
{
	if (m_pMeshBuilder != NULL)
	{
		unsigned short const * currentIndexBuffer = m_pMeshBuilder->Index();
		return const_cast<unsigned short *>(currentIndexBuffer);
	}
	else
	{
		return NULL;
	}
}

unsigned short * IndexTriVertexBuffer::IndexBuffer_InterlockedAdd(int numberOfIndices)
{
	if (m_pMeshBuilder != NULL)
	{
		unsigned short * currentIndexBuffer = m_pMeshBuilder->IndexBuffer_InterlockedAdd(numberOfIndices);
		return const_cast<unsigned short *>(currentIndexBuffer);
	}
	else
	{
		return NULL;
	}
}

int IndexTriVertexBuffer::GetIndexOffset() const
{
	if (m_pMeshBuilder != NULL)
	{
		return m_pMeshBuilder->GetIndexOffset();
	}
	else
	{
		return 0;
	}
}

void IndexTriVertexBuffer::UpdateInternals(int numberOfVerticesAdded, int numberOfIndicesAdded)
{
	m_nVerticesOutput += numberOfVerticesAdded;

	if (m_pMeshBuilder != NULL)
	{
		m_pMeshBuilder->AdvanceVerticesF<VTX_HAVEALL, 1>(numberOfVerticesAdded);
		m_pMeshBuilder->AdvanceIndices(numberOfIndicesAdded);
	}
}

void IndexTriVertexBuffer::endVertex(vbId_t* vertexId)
{
	if (m_pMeshBuilder != NULL)
	{
//	m_pMeshBuilder->AdvanceVertex();
	// right now we only use one texture stage so we can do this.
	m_pMeshBuilder->AdvanceVertexF<VTX_HAVEALL, 1>();
	drawVertex(vertexId);
	}
}

