//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "blobulator/section_implementation/cfragmented_vertex_buffer.h"
#include "../vertexbuffers/indextrivertexbuffer_inl.h"

#include "mathlib/vector.h"

namespace Blobulator
{
namespace SectionImplementation
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
{
	m_pVertexBuffer = new VERTEX_BUFFER_CLASS();
	m_pVertexBuffer->init();

	m_pCachedIndexBuffer = new unsigned short[NUMBER_OF_INDICES_RESERVED];
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

		m_FragmentsAvailable.pushAutoSize(fragment);
	}

	m_FragmentsToDisplay.ensureCapacity(NUMBER_OF_FRAGMENTS);
}

CFragmentedVertexBuffer::~CFragmentedVertexBuffer()
{
	delete m_pVertexBuffer;

	delete[] m_pCachedIndexBuffer;
	MemAlloc_FreeAligned(m_pCachedVertexBuffer);
}

void CFragmentedVertexBuffer::BeginFrame(void * pRenderContext)
{
	m_pVertexBuffer->beginFrame(pRenderContext);
}

bool CFragmentedVertexBuffer::DrawFragments()
{
	bool drewVB = false;
	m_Mutex.Lock();	// Be careful when changing code flow as the number of locks / unlocks has been reduced a bit
	// Thus the lock state is extended when entering / exiting the loop, or while iterating the loop
	for (;;)
	{
		if (m_FragmentsToDisplay.size == 0)
		{
			break;
		}

		CFragmentInfo fragment = m_FragmentsToDisplay.pop();
		m_Mutex.Unlock();	// Unlock during the drawing

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
			char * positionBuffer = static_cast<char *>(m_pVertexBuffer->GetInternalPositionBuffer());
			const int nOuputVertexSize = m_pVertexBuffer->GetVertexSize();	// Better if this was a compile time defined const
			unsigned short * currentIndexBuffer = m_pVertexBuffer->GetCurrentIndexBuffer();
			const int indexOffset = m_pVertexBuffer->GetIndexOffset();
			nVerticesOutput += indexOffset;

			int nNumVerticesToCopy = fragment.m_nVertexBuffer_CurrentIndex - fragment.m_nVertexBuffer_StartIndex;
			int nStartIndexBuffer = fragment.m_nIndexBuffer_StartIndex;
			int nLastIndexBuffer = fragment.m_nIndexBuffer_CurrentIndex;

#if BLOB_ENABLE_REINDEXING
			// First we copy the indices (could be unrolled, written as a 64 bits, etc...)
			for (int i = nStartIndexBuffer ; i < nLastIndexBuffer ; ++i)
			{
				*currentIndexBuffer++ = fragment.m_pCachedIndexBuffer[i] + nVerticesOutput;
			}

			// Then we copy the vertices
			char * cachedVertexBuffer = static_cast<char *>(fragment.m_pCachedVertexBuffer);
			cachedVertexBuffer += fragment.m_nVertexBuffer_StartIndex * fragment.GetVertexSize();

			// Instead of doing a memcpy, we are going to actually expand the vertices
			// (as we go from a "compressed" format to the one used by the vertex buffer.
			// It saves memory in JobCache and reduces the cost of the memcpy as well.
			//	memcpy(positionBuffer, cachedVertexBuffer, nNumVerticesToCopy * nOuputVertexSize);

			int nNumberOfVertices = nNumVerticesToCopy;
			int64 * sourceBuffer = reinterpret_cast<int64 *>(cachedVertexBuffer);
			while (nNumberOfVertices > 0)
			{
				// Let's copy the 24 bytes (using 64 bits integer to speed up on X360 and PS3).
				int64 * destinationBuffer = reinterpret_cast<int64 *>(positionBuffer);

				*destinationBuffer = *sourceBuffer++;
				*(destinationBuffer + 1) = *sourceBuffer++;
				*(destinationBuffer + 2) = *sourceBuffer++;
				positionBuffer += nOuputVertexSize;
				--nNumberOfVertices;
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

		m_Mutex.Lock();		// Lock when pushing, will be unlocked at the beginning of the loop (or if exiting).
		m_FragmentsAvailable.push(fragment);
	}
	m_Mutex.Unlock();
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

CFragmentInfo CFragmentedVertexBuffer::GetAvailableFragment()
{
	m_Mutex.Lock();
	while (m_FragmentsAvailable.size == 0)
	{
		// Somehow the available list is empty, we have to busy wait here :(
		// Consider increasing the number of available fragments
		// Or draw faster...

		// If used incorrectly, there could be an infinite loop here...
		m_Mutex.Unlock();
		ThreadPause();
		m_Mutex.Lock();
	}

	CFragmentInfo fragment = m_FragmentsAvailable.pop();
	m_Mutex.Unlock();
	fragment.Reset();
	return fragment;
}

void CFragmentedVertexBuffer::PushFragmentToDraw(CFragmentInfo & fragment)
{
	// Invalidate it so we can detect if we do something incorrect with it once it has been pushed.
	fragment.m_bUsable = false;

	m_Mutex.Lock();
	m_FragmentsToDisplay.pushAutoSize(fragment);
	m_Mutex.Unlock();
}

// This function is the same as PushFragmentToDraw() and GetAvailableFragment() called back to back
// with only one mutex lock.
bool CFragmentedVertexBuffer::SwapFragments(CFragmentInfo & fragmentToSwap)
{
	if (fragmentToSwap.m_nIndexBuffer_CurrentIndex == fragmentToSwap.m_nIndexBuffer_StartIndex)
	{
		// No index to display, nothing to push, it is directly considered free.
		return true;
	}

	// Invalidate it so we can detect if we do something incorrect with it once it has been pushed.
	fragmentToSwap.m_bUsable = false;

	m_Mutex.Lock();
	m_FragmentsToDisplay.pushAutoSize(fragmentToSwap);
	while (m_FragmentsAvailable.size == 0)
	{
		// Somehow the available list is empty, we have to busy wait here :(
		// Consider increasing the number of available fragments
		// Or draw faster...

		// If used incorrectly, there could be an infinite loop here...
		m_Mutex.Unlock();
		ThreadPause();
		m_Mutex.Lock();
	}

	CFragmentInfo fragment = m_FragmentsAvailable.pop();
	m_Mutex.Unlock();

	fragment.Reset();
	fragmentToSwap = fragment;
	return true;
}

void CFragmentedVertexBuffer::CheckResetState()
{
	// No need to lock here as the thread are done. Make sure everything is back to normal.
	Assert(m_FragmentsToDisplay.size == 0);
	Assert(m_FragmentsAvailable.size == NUMBER_OF_FRAGMENTS);

	// Reset the values after the checks, so we can continue running the program (esp. if the SPU crashes).
	m_FragmentsToDisplay.size = 0;
	m_FragmentsAvailable.size = NUMBER_OF_FRAGMENTS;
}

}
}
