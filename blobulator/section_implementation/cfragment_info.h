//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_SECTIONIMPLEMENTATION_CFRAGMENT_INFO_H
#define BLOBULATOR_SECTIONIMPLEMENTATION_CFRAGMENT_INFO_H

#include "dbg.h"

namespace Blobulator
{
	namespace SectionImplementation
	{
		class Section;

		class CFragmentInfo
		{
		public:
			int GetVerticesOutput() const
			{
				Assert(m_bUsable);
				int nVerticesOutput = m_nVertexBuffer_CurrentIndex - m_nVertexBuffer_StartIndex;	// Final offset will have to be added during the drawing
				return nVerticesOutput;
			}

			char * GetPositionBuffer() const
			{
				Assert(m_bUsable);
				char * positionBuffer = static_cast<char *>(m_pCachedVertexBuffer);
				positionBuffer += m_nVertexBuffer_CurrentIndex * m_nVertexSize;						// Use another pointer to remove the multiply
				return positionBuffer;
			}

			unsigned short * GetIndexBuffer() const
			{
				Assert(m_bUsable);
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
				m_nIndexBuffer_CurrentIndex = pCurrentIndexBuffer - m_pCachedIndexBuffer;
				Assert(m_nVertexBuffer_CurrentIndex < m_nVertexBuffer_EndIndex);
				Assert(m_nIndexBuffer_CurrentIndex < m_nIndexBuffer_EndIndex);
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
				memcpy(this, &other, sizeof(*this));
				return (*this);
			}

			// TODO: Create an init method at some point

			bool m_bUsable;

			unsigned short * m_pCachedIndexBuffer;
			int m_nIndexBuffer_StartIndex;				// Note that this could be embedded with m_pCachedIndexBuffer
			int m_nIndexBuffer_EndIndex;
			int m_nIndexBuffer_CurrentIndex;

			void * m_pCachedVertexBuffer;
			int m_nVertexBuffer_StartIndex;				// Note that this could be embedded with m_pCachedVertexBuffer
			int m_nVertexBuffer_EndIndex;
			int m_nVertexBuffer_CurrentIndex;

			int m_nVertexSize;

			unsigned short * m_pThresholdIndexBuffer;
			int m_nThresholdVerticesOutput;
		};
	}
}

#endif

