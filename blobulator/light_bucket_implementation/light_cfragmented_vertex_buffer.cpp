//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: Implements a LIFO queue of fragment of vertex buffers.
//			Each fragment is filled by other threads, 
//
// $NoKeywords: $
//=============================================================================//

#include "blobulator/light_bucket_implementation/light_cfragmented_vertex_buffer.h"

#include "../vertexbuffers/indextrivertexbuffer_inl.h"
#include "tier1/convar.h"
#include "mathlib/vector.h"

#if IsPlatformX360()
extern "C" void _ReadWriteBarrier();
#pragma intrinsic(_ReadWriteBarrier)
#else
#define _ReadWriteBarrier()
#endif

namespace Blobulator
{
namespace LightBucketImplementation
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
	m_nCurrentSlot(0)
{
	for (int i = 0 ; i < NUMBER_OF_SLOTS ; ++i)
	{
		m_FragmentsToDisplay[i] = &m_FragmentsToDisplayContainer[i];
	}

	m_pVertexBuffer = new VERTEX_BUFFER_CLASS();
	m_pVertexBuffer->init();

	m_pCachedIndexBuffer = (unsigned short *)MemAlloc_AllocAligned((NUMBER_OF_INDICES_RESERVED) * sizeof(unsigned short), 16);
	m_pCachedVertexBuffer = MemAlloc_AllocAligned((NUMBER_OF_VERTICES_RESERVED) * sizeof(JobVertexBuffer), 16);

	for (int i = 0 ; i < BLOB_NUMBER_OF_FRAGMENTS ; ++i)
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
		m_FragmentsAvailable.PushToHead(i);
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

static void CopyIndicesOptimized( uint16 *pDstIndices, const uint16 *pSrcIndices, int nNumIndices, int nIndexOffset )
{
	if ( !nNumIndices )
		return;

	while ( reinterpret_cast< uint32 >( pDstIndices ) & ( 4 + 2 ) )
	{
		*pDstIndices++ = *pSrcIndices++ + nIndexOffset;

		nNumIndices--;
		if ( !nNumIndices )
			return;
	}

	uint64 nIndexOffset64 = nIndexOffset;
	nIndexOffset64 = nIndexOffset64 | ( nIndexOffset64 << 16ULL ) | ( nIndexOffset64 << 32ULL ) | ( nIndexOffset64 << 48ULL );

	int nNumGroupsToCopy = nNumIndices >> 4;

	for ( int i = 0; i < nNumGroupsToCopy; ++i )
	{
		// 32 bytes per iteration (8 * 4), 16 indices
		uint64 nI0 = reinterpret_cast< const uint64 * >( pSrcIndices )[0] + nIndexOffset64;
		uint64 nI1 = reinterpret_cast< const uint64 * >( pSrcIndices )[1] + nIndexOffset64;
		uint64 nI2 = reinterpret_cast< const uint64 * >( pSrcIndices )[2] + nIndexOffset64;
		uint64 nI3 = reinterpret_cast< const uint64 * >( pSrcIndices )[3] + nIndexOffset64;

		reinterpret_cast< uint64 * >( pDstIndices )[0] = nI0; _ReadWriteBarrier();
		reinterpret_cast< uint64 * >( pDstIndices )[1] = nI1; _ReadWriteBarrier();
		reinterpret_cast< uint64 * >( pDstIndices )[2] = nI2; _ReadWriteBarrier();
		reinterpret_cast< uint64 * >( pDstIndices )[3] = nI3; _ReadWriteBarrier();

		pDstIndices += 16;
		pSrcIndices += 16;
	}

	// This could be improved to copy off the remaining DWORD's first, then the last remaining WORD if any.
	nNumIndices &= 15;
	for ( int i = 0; i < nNumIndices; i++)
	{
		*pDstIndices++ = (*pSrcIndices++) + nIndexOffset;
	}
}

// 24 to 40 bytes
static void ExpandVerticesOptimized( uint32 *pDestBuf, const uint32 *pSrcBuf, int nNumberOfVertices, int nOutputVertexSize )
{
	for ( int i = 0; i < nNumberOfVertices; ++i )
	{
		pDestBuf[0] = pSrcBuf[0]; _ReadWriteBarrier();
		pDestBuf[1] = pSrcBuf[1]; _ReadWriteBarrier();
		pDestBuf[2] = pSrcBuf[2]; _ReadWriteBarrier();

		pDestBuf[3] = pSrcBuf[3]; _ReadWriteBarrier();
		pDestBuf[4] = pSrcBuf[4]; _ReadWriteBarrier();
		pDestBuf[5] = pSrcBuf[5]; _ReadWriteBarrier();
		pSrcBuf += 6;

#ifdef PLATFORM_X360
		// On X360 we need to write everything to keep the access to write combined memory optimized
		pDestBuf[6] = 0; _ReadWriteBarrier();
		pDestBuf[7] = 0; _ReadWriteBarrier();
		pDestBuf[8] = 0; _ReadWriteBarrier();
		pDestBuf[9] = 0; _ReadWriteBarrier();

		// Make sure we copied everything
		Assert(nOutputVertexSize == (10 * sizeof(uint32)));
#endif // PLATFORM_X360

		pDestBuf += nOutputVertexSize / sizeof(uint32);
	}
}

ConVar r_paintblob_use_optimized_fragment_copy("r_paintblob_use_optimized_fragment_copy", "1", FCVAR_NONE, "Indicates if optimized VB/IB copy is enabled (Write-combine memory optimization).");


bool CFragmentedVertexBuffer::DrawFragments(int nSlotIndex, int * pnOutputFragmentIndex)
{
	if (pnOutputFragmentIndex != NULL)
	{
		*pnOutputFragmentIndex = -1;
	}

	bool bDrewVB = false;

	const int nOuputVertexSize = m_pVertexBuffer->GetVertexSize();	// Better if this was a compile time defined const
	if ( nOuputVertexSize == 0 )
	{
		// If we get here, we're likely in an alt+tab state.  Because we're writing to the actual vb/ib memory here, we don't have meshbuilder
		// to help us when we don't have VBs or IBs to write into.  Therefore, we need to early out.
		// We can check pPositionBuffer below for whether the VB is valid, but we do it here to only do it once instead of every time through the loop.
		for ( ; ; )
		{
			int nNextFragmentToDisplay = m_FragmentsToDisplay[nSlotIndex].PopFromHead();
			if ( nNextFragmentToDisplay < 0 )
			{
				break;
			}
			m_FragmentsAvailable.PushToHead( nNextFragmentToDisplay );
			bDrewVB = true;
		}
		return bDrewVB;
	}

	for (;;)
	{
		int nNextFragmentToDisplay = m_FragmentsToDisplay[nSlotIndex].PopFromHead();
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

				if (r_paintblob_use_optimized_fragment_copy.GetBool())
				{
					CopyIndicesOptimized( pCurrentIndexBuffer, fragment.m_pCachedIndexBuffer + nStartIndexBuffer, nLastIndexBuffer - nStartIndexBuffer, nVerticesOutput );
				}
				else
				{
					for (int i = nStartIndexBuffer ; i < nLastIndexBuffer ; ++i)
					{
						*pCurrentIndexBuffer++ = fragment.m_pCachedIndexBuffer[i] + nVerticesOutput;
					}
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
				int nNumberOfVertices = nNumVerticesToCopy;
				int64 * pSourceBuffer = reinterpret_cast<int64 *>(pCachedVertexBuffer);

				if (r_paintblob_use_optimized_fragment_copy.GetBool())
				{
					ExpandVerticesOptimized( reinterpret_cast< uint32 * >( pPositionBuffer ), reinterpret_cast< const uint32 * >( pSourceBuffer ), nNumberOfVertices, nOuputVertexSize );
				}
				else
				{
					// positionBuffer can be NULL when running the blob from unit-tests...
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
		}

		if (pnOutputFragmentIndex != NULL)
		{
			// Have to fill the passed value
			*pnOutputFragmentIndex = nNextFragmentToDisplay;
			pnOutputFragmentIndex = NULL;			// Do it only on the first one, the others will be pushed on the stack normally
		}
		else
		{
			m_FragmentsAvailable.PushToHead(nNextFragmentToDisplay);
		}
		bDrewVB = true;
	}
	return bDrewVB;
}

// This function should be called when we detect a deadlock because not enough fragments are available
// We simply remove all the fragments to display and put them as available fragments.
// This resolves the deadlock at the cost of a visual defect for one frame.
void CFragmentedVertexBuffer::ClearFragmentsToDisplay()
{
	const int NUMBER_OF_MILLISECONDS_PER_SPEW = 2 * 1000;
	static uint32 sFirstTime = Plat_MSTime();
	uint32 nNewTime = Plat_MSTime();
	if ( nNewTime > sFirstTime + NUMBER_OF_MILLISECONDS_PER_SPEW )
	{
		sFirstTime = nNewTime;
		Warning("[Blobulator] Recycling fragments to draw to prevent deadlock. Expect visual corruptions. Please set your Effect Detail to a lower setting.\n");
	}

	for (int i = 0 ; i < NUMBER_OF_SLOTS ; ++i)
	{
		for ( ; ; )
		{
			int nIndex = m_FragmentsToDisplay[i].PopFromHead();
			if (nIndex >= 0)
			{
				m_FragmentsAvailable.PushToHead(nIndex);
			}
			else
			{
				break;
			}
		}
	}
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

void CFragmentedVertexBuffer::CheckResetState(int nSlotIndex)
{
	// This is safe to execute even if two steps are enabled. As the work for this tile is over.
	Assert(m_FragmentsToDisplay[nSlotIndex].IsEmpty());

	// No need to lock here as the thread are done. Make sure everything is back to normal.
	if (UseTwoPassesToDraw() == false)
	{
		//	Assert(nSlotIndex == 0);	// In one step, only one slot should be used at a time.
		// Run this code only when we are in one step mode (as otherwise other tiles are going to submit work to the fragmented vertex buffer).
		Assert(m_FragmentsAvailable.GetSize() == BLOB_NUMBER_OF_FRAGMENTS);

		// Reset the values after the checks, so we can continue running the program (esp. if the SPU crashes).
		m_FragmentsToDisplay[nSlotIndex].Clear();
		m_FragmentsAvailable.ResetToFull();
	}
}

int CFragmentedVertexBuffer::RetrievePointers(CLockLessStack<BLOB_NUMBER_OF_FRAGMENTS> * * pOutFragmentsAvailable, CLockLessStack<BLOB_NUMBER_OF_FRAGMENTS> * * pOutFragmentsToDisplay, CFragmentInfo * * pOutAllFragments)
{
	int mSlotIndex = m_nCurrentSlot++;
	if (mSlotIndex >= NUMBER_OF_SLOTS)
	{
		Warning("[Blobulator] More than %d slots are in use! Report this issue.\n", mSlotIndex);
		// If that happens we loop back to the first slots. The worse case is that some blob may be incorrectly transformed on the screen.
		// Because we use the early slots though, they may have been already calculated (but the renderer is not yet recycled, so we may not see any issue).
		mSlotIndex %= NUMBER_OF_SLOTS;
	}

	*pOutFragmentsAvailable = &m_FragmentsAvailable;
	*pOutFragmentsToDisplay = &m_FragmentsToDisplay[mSlotIndex];
	*pOutAllFragments = m_Fragments;
	return mSlotIndex;
}

int CFragmentedVertexBuffer::GetVertexSize()
{
	return sizeof(JobVertexBuffer);
}

}
}
