//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CFRAGMENT_INFO_H
#define BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CFRAGMENT_INFO_H

#include "blobulator/light_bucket_implementation/light_csoftware_cache.h"

#ifdef SPU
#include "ps3/spu_job_shared.h"
#include <string.h>
#else
#include "dbg.h"
#endif

namespace Blobulator
{
	namespace LightBucketImplementation
	{
		class CSection;

		class ALIGN16 CFragmentInfo
		{
		public:
			CFragmentInfo()
				:
				m_bUsable(false),
				m_pCachedIndexBuffer(NULL),
				m_nIndexBuffer_StartIndex(0),
				m_nIndexBuffer_EndIndex(0),
				m_nIndexBuffer_CurrentIndex(0),
				m_pCachedVertexBuffer(NULL),
				m_nVertexBuffer_StartIndex(0),
				m_nVertexBuffer_EndIndex(0),
				m_nVertexBuffer_CurrentIndex(0),
				m_nVertexSize(0),
				m_pThresholdIndexBuffer(NULL),
				m_nThresholdVerticesOutput(0)
			{
				Assert(((int)this & 0xf) == 0);
			}

			int GetVerticesOutput() const
			{
				Assert(m_bUsable);
				int nVerticesOutput = m_nVertexBuffer_CurrentIndex - m_nVertexBuffer_StartIndex;	// Final offset will have to be added during the drawing
				return nVerticesOutput;
			}

			char * GetPositionBuffer() const
			{
				Assert(m_bUsable);
				Assert( ( m_nVertexBuffer_CurrentIndex >= m_nVertexBuffer_StartIndex ) && ( m_nVertexBuffer_CurrentIndex < m_nVertexBuffer_EndIndex ) );
				char * positionBuffer = static_cast<char *>(m_pCachedVertexBuffer);
				positionBuffer += m_nVertexBuffer_CurrentIndex * m_nVertexSize;						// Use another pointer to remove the multiply
				return positionBuffer;
			}

			unsigned short * GetIndexBuffer() const
			{
				Assert(m_bUsable);
				Assert( ( m_nIndexBuffer_CurrentIndex >= m_nIndexBuffer_StartIndex) && ( m_nIndexBuffer_CurrentIndex < m_nIndexBuffer_EndIndex ) );
				unsigned short * currentIndexBuffer = m_pCachedIndexBuffer + m_nIndexBuffer_CurrentIndex;
				return currentIndexBuffer;
			}

			int GetVertexSize() const
			{
				Assert(m_bUsable);
				return m_nVertexSize;
			}

			void UpdateInternals(int nVerticesOutput, unsigned short * pCurrentIndexBuffer)
			{
				Assert(m_bUsable);
				m_nVertexBuffer_CurrentIndex = m_nVertexBuffer_StartIndex + nVerticesOutput;
				Assert(m_nVertexBuffer_CurrentIndex < m_nVertexBuffer_EndIndex);
				m_nIndexBuffer_CurrentIndex = pCurrentIndexBuffer - m_pCachedIndexBuffer;
				Assert(m_nIndexBuffer_CurrentIndex < m_nIndexBuffer_EndIndex);
			}

			void CheckIndices()
			{
				Assert( ( m_nVertexBuffer_CurrentIndex >= m_nVertexBuffer_StartIndex ) && ( m_nVertexBuffer_CurrentIndex < m_nVertexBuffer_EndIndex ) );
				Assert( ( m_nIndexBuffer_CurrentIndex >= m_nIndexBuffer_StartIndex) && ( m_nIndexBuffer_CurrentIndex < m_nIndexBuffer_EndIndex ) );
			}

			bool IsOverThreshold(int nVerticesOutput, unsigned short * pCurrentIndexBuffer) const
			{
				Assert(m_bUsable);
				return (	(nVerticesOutput >= m_nThresholdVerticesOutput)
					||	(pCurrentIndexBuffer >= m_pThresholdIndexBuffer)	);
			}

			void Reset()
			{
				m_nIndexBuffer_CurrentIndex = m_nIndexBuffer_StartIndex;
				m_nVertexBuffer_CurrentIndex = m_nVertexBuffer_StartIndex;
				m_bUsable = true;
			}

			CFragmentInfo & operator=(const CFragmentInfo & other)
			{
				// No need to self test here
				memmove(this, &other, sizeof(*this));
				return (*this);
			}

			// TODO: Create an init method at some point
			// These should not be public, refactor this when time permits.

			bool m_bUsable;

			unsigned short * m_pCachedIndexBuffer;		// This represents the address in main memory
														// For SPU, we will need the corresponding buffer in local storage.
														// Note that this represents the address of the first buffer
														// (m_nIndexBuffer_StartIndex has to be added for each operations).
														// This created many issues for no-reason. Do not do that again. :(

			int m_nIndexBuffer_StartIndex;				// Note that this could be embedded with m_pCachedIndexBuffer
			int m_nIndexBuffer_EndIndex;
			int m_nIndexBuffer_CurrentIndex;

			void * m_pCachedVertexBuffer;				// This represents the address in main memory.
														// For SPU, we will need the corresponding buffer in local storage.
														// Same comment as m_pCachedIndexBuffer. :(
			int m_nVertexBuffer_StartIndex;				// Note that this could be embedded with m_pCachedVertexBuffer
			int m_nVertexBuffer_EndIndex;
			int m_nVertexBuffer_CurrentIndex;

			int m_nVertexSize;

			unsigned short * m_pThresholdIndexBuffer;	// This represents the address in main memory.
														// For SPU, we will need the corresponding address in local storage.
			int m_nThresholdVerticesOutput;

			// It seems this struct is taking 48 bytes.
		} ALIGN16_POST;

		BLOB_COMPILE_TIME_ASSERT((sizeof(CFragmentInfo) & 0xf) == 0);
	}
}

#endif

