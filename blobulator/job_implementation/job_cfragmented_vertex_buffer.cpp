//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: Implements a LIFO queue of fragment of vertex buffers.
//			Each fragment is filled by other threads, 
//
// $NoKeywords: $
//=============================================================================//

#include "blobulator/job_implementation/job_cfragmented_vertex_buffer.h"

#include "../vertexbuffers/indextrivertexbuffer_inl.h"
#include "mathlib/vector.h"

namespace Blobulator
{
namespace JobImplementation
{

struct JobVertexBuffer
{
public:
	Vector m_Position;		// 12 bytes
	Vector m_Normal;		// 12 bytes

	// We don't store the full vertex info, just what is calculated...
	// The vertex will be expanded in the real vertex buffer to draw
};

CFragmentedVertexBuffer::CFragmentedVertexBuffer()
:
m_FragmentsAvailable(&m_FragmentsAvailableContainer),
m_FragmentsToDisplay(&m_FragmentsToDisplayContainer)
{
	m_pVertexBuffer = new VERTEX_BUFFER_CLASS();
	m_pVertexBuffer->init();

	m_pCachedIndexBuffer = (unsigned short *)MemAlloc_AllocAligned((NUMBER_OF_INDICES_RESERVED) * sizeof(unsigned short), 16);
	m_pCachedVertexBuffer = MemAlloc_AllocAligned((NUMBER_OF_VERTICES_RESERVED) * sizeof(JobVertexBuffer), 16);

	for (int i = 0 ; i < NUMBER_OF_FRAGMENTS ; ++i)
	{
		CFragmentInfo fragment;
		fragment.m_bUsable = true;

		fragment.m_pCachedIndexBuffer = m_pCachedIndexBuffer;
		fragment.m_nIndexBuffer_StartIndex = i * FRAGMENT_INDEX_BUFFER_SIZE;
		fragment.m_nIndexBuffer_EndIndex = (i + 1 ) * FRAGMENT_INDEX_BUFFER_SIZE;
		fragment.m_nIndexBuffer_CurrentIndex = fragment.m_nIndexBuffer_StartIndex;

		fragment.m_pCachedVertexBuffer = m_pCachedVertexBuffer;
		fragment.m_nVertexBuffer_StartIndex = i * FRAGMENT_VERTEX_BUFFER_SIZE;
		fragment.m_nVertexBuffer_EndIndex = (i + 1) * FRAGMENT_VERTEX_BUFFER_SIZE;
		fragment.m_nVertexBuffer_CurrentIndex = fragment.m_nVertexBuffer_StartIndex;

		fragment.m_nVertexSize = sizeof(JobVertexBuffer);

		fragment.m_pThresholdIndexBuffer = m_pCachedIndexBuffer + (fragment.m_nIndexBuffer_EndIndex - FRAGMENT_INDEX_THRESHOLD);
		fragment.m_nThresholdVerticesOutput = FRAGMENT_VERTEX_BUFFER_SIZE - FRAGMENT_VERTEX_THRESHOLD;

		m_Fragments[i] = fragment;
		m_FragmentsAvailable.PushHead(i);
	}
}

CFragmentedVertexBuffer::~CFragmentedVertexBuffer()
{
	delete m_pVertexBuffer;

	MemAlloc_FreeAligned(m_pCachedIndexBuffer);
	MemAlloc_FreeAligned(m_pCachedVertexBuffer);
}

void CFragmentedVertexBuffer::BeginFrame(void * pRenderContext)
{
	m_pVertexBuffer->beginFrame(pRenderContext);
}

bool CFragmentedVertexBuffer::DrawFragments()
{
	bool drewVB = false;
	for (;;)
	{
		int nNextFragmentToDisplay = m_FragmentsToDisplay.PopHead();
		if (nNextFragmentToDisplay < 0)
		{
			break;
		}

		CFragmentInfo fragment = m_Fragments[nNextFragmentToDisplay];

		//	VPROF_BUDGET( "DrawFragments", "DrawFragments" );

		// Make it locally usable
		fragment.m_bUsable = true;

		int nNumIndicesToCopy = fragment.m_nIndexBuffer_CurrentIndex - fragment.m_nIndexBuffer_StartIndex;
		if (nNumIndicesToCopy != 0)
		{
			// This fragment contains new data to be pushed on the main vertex buffer
			// While the thread is filling its local vertex buffer information,
			// we are going to draw the information in small batches.

			int nVerticesOutput = m_pVertexBuffer->GetCurrentVerticesOutput();
			// Use char * to use a simple add logic
			char * pPositionBuffer = static_cast<char *>(m_pVertexBuffer->GetInternalPositionBuffer());
			const int nOuputVertexSize = m_pVertexBuffer->GetVertexSize();	// Better if this was a compile time defined const
			unsigned short * pCurrentIndexBuffer = m_pVertexBuffer->GetCurrentIndexBuffer();
			const int indexOffset = m_pVertexBuffer->GetIndexOffset();
			nVerticesOutput += indexOffset;

			int nNumVerticesToCopy = fragment.m_nVertexBuffer_CurrentIndex - fragment.m_nVertexBuffer_StartIndex;
			int nStartIndexBuffer = fragment.m_nIndexBuffer_StartIndex;
			int nLastIndexBuffer = fragment.m_nIndexBuffer_CurrentIndex;

#if BLOB_ENABLE_REINDEXING
			// First we copy the indices (could be unrolled, written as a 64 bits, etc...)
			if (pCurrentIndexBuffer != NULL)
			{
				// currentIndexBuffer is NULL when running the blob from unit-tests...
				for (int i = nStartIndexBuffer ; i < nLastIndexBuffer ; ++i)
				{
					*pCurrentIndexBuffer++ = fragment.m_pCachedIndexBuffer[i] + nVerticesOutput;
				}
			}

			// Then we copy the vertices
			char * pCachedVertexBuffer = static_cast<char *>(fragment.m_pCachedVertexBuffer);
			pCachedVertexBuffer += fragment.m_nVertexBuffer_StartIndex * fragment.GetVertexSize();

			// Instead of doing a memcpy, we are going to actually expand the vertices
			// (as we go from a "compressed" format to the one used by the vertex buffer.
			// It saves memory in JobCache and reduces the cost of the memcpy as well.
			//	memcpy(positionBuffer, cachedVertexBuffer, nNumVerticesToCopy * nOuputVertexSize);

			if (pPositionBuffer != NULL)
			{
				// positionBuffer can be NULL when running the blolb from unit-tests...
				int nNumberOfVertices = nNumVerticesToCopy;
				int64 * pSourceBuffer = reinterpret_cast<int64 *>(pCachedVertexBuffer);
				while (nNumberOfVertices > 0)
				{
					// Let's copy the 24 bytes (using 64 bits integer to speed up on X360 and PS3).
					int64 * destinationBuffer = reinterpret_cast<int64 *>(pPositionBuffer);

					*destinationBuffer = *pSourceBuffer++;
					*(destinationBuffer + 1) = *pSourceBuffer++;
					*(destinationBuffer + 2) = *pSourceBuffer++;
					pPositionBuffer += nOuputVertexSize;
					--nNumberOfVertices;
				}
			}
#if BLOB_ENABLE_DRAW
			m_pVertexBuffer->UpdateInternals(nNumVerticesToCopy, nNumIndicesToCopy);

			// Send the draw command now, then we can wait the next command
			{
				//	VPROF_BUDGET( "EndMesh_BeginMesh", "EndMesh_BeginMesh" );
				m_pVertexBuffer->endMesh();
				m_pVertexBuffer->beginMesh();
			}
#endif

#endif
			drewVB = true;
		}

		m_FragmentsAvailable.PushHead(nNextFragmentToDisplay);
	}
	return (drewVB);
}

void CFragmentedVertexBuffer::EndFrame()
{
	m_pVertexBuffer->endFrame();
}

void CFragmentedVertexBuffer::BeginTile()
{
	m_pVertexBuffer->beginTile();
}

void CFragmentedVertexBuffer::EndTile()
{
	m_pVertexBuffer->endTile();
}

CFragmentInfo CFragmentedVertexBuffer::GetAvailableFragment(int & nOutIndex)
{
	int nFragmentIndex;
	
	while ((nFragmentIndex = m_FragmentsAvailable.PopHead()) < 0)
	{
		// Somehow the available list is empty, we have to busy wait here :(
		// Consider increasing the number of available fragments
		// Or draw faster...

		// If used incorrectly, there could be an infinite loop here...
		ThreadPause();
	}

	CFragmentInfo fragment = m_Fragments[nFragmentIndex];
	fragment.Reset();
	nOutIndex = nFragmentIndex;
	return fragment;
}

void CFragmentedVertexBuffer::CheckResetState()
{
	// No need to lock here as the thread are done. Make sure everything is back to normal.
	Assert(m_FragmentsToDisplay.IsEmpty());
	Assert(m_FragmentsAvailable.GetSize() == NUMBER_OF_FRAGMENTS);

	// Reset the values after the checks, so we can continue running the program (esp. if the SPU crashes).
	m_FragmentsToDisplay.Clear();
	m_FragmentsAvailable.ResetToFull();
}

void CFragmentedVertexBuffer::RetrievePointers(CLockLessStack<NUMBER_OF_FRAGMENTS> * * pOutFragmentsAvailable, CLockLessStack<NUMBER_OF_FRAGMENTS> * * pOutFragmentsToDisplay, CFragmentInfo * * pOutAllFragments)
{
	*pOutFragmentsAvailable = &m_FragmentsAvailable;
	*pOutFragmentsToDisplay = &m_FragmentsToDisplay;
	*pOutAllFragments = m_Fragments;
}

}
}
