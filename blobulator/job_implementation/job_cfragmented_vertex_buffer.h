//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_JOBIMPLEMENTATION_CFRAGMENTED_VERTEX_BUFFER_H
#define BLOBULATOR_JOBIMPLEMENTATION_CFRAGMENTED_VERTEX_BUFFER_H

#ifndef SPU
// This class should not be referenced by the SPU code

#include "blobulator/job_implementation/job_cfragment_info.h"
#include "blobulator/implicit/impdefines.h"
#include "blobulator/vertexbuffers/vertexbuffer.h"
#include "blobulator/smartarray.h"
#include "blobulator/job_implementation/csoftware_cache.h"
#include "blobulator/job_implementation/job_details.h"

#include "tier0/threadtools.h"

class ImpTile;

namespace Blobulator
{
	namespace JobImplementation
	{
		class CJobCache;
		class CFragmentInfo;

		class CFragmentedVertexBuffer
		{
		public:
			// We do not want to call these methods on SPU
			CFragmentedVertexBuffer();
			~CFragmentedVertexBuffer();

			void BeginFrame(void * pRenderContext);
			void EndFrame();
			void BeginTile();
			void EndTile();

			bool DrawFragments();

			// This should be called per job when we start a frame.
			// It may create an infinite loop if used improperly (like too many times with no code drawing and pushing back free items),
			CFragmentInfo GetAvailableFragment(int & nOutIndex);

			void CheckResetState();

			void RetrievePointers(CLockLessStack<NUMBER_OF_FRAGMENTS> * * pOutFragmentsAvailable, CLockLessStack<NUMBER_OF_FRAGMENTS> * * pOutFragmentsToDisplay, CFragmentInfo * * pOutAllFragments);

		private:
			// TODO: Add compile time checks to make sure we are multiple of 16. The SPUs will thank us.
#if _DEBUG
			static const int FRAGMENT_INDEX_BUFFER_SIZE = 2048;
#else
			static const int FRAGMENT_INDEX_BUFFER_SIZE = 8192;
#endif
			static const int FRAGMENT_VERTEX_BUFFER_SIZE = FRAGMENT_INDEX_BUFFER_SIZE / 4;	// There is in average of 5 indices submitted by vertex.
			static const int FRAGMENT_INDEX_THRESHOLD = 24;
			static const int FRAGMENT_VERTEX_THRESHOLD = 24;

			// This represents an array of 128K values, i.e. 256 Kb
			// I have seen current worst case of 82000 indices submitted. It could be worse.
			static const int NUMBER_OF_INDICES_RESERVED = NUMBER_OF_FRAGMENTS * FRAGMENT_INDEX_BUFFER_SIZE;

			// This makes the buffer way too big... :( 1.7 MB currently.
			static const int NUMBER_OF_VERTICES_RESERVED = NUMBER_OF_FRAGMENTS * FRAGMENT_VERTEX_BUFFER_SIZE;

			// We will only need one mutex for both arrays.
			// As if in most cases, if we are pushing a fragment to display, we are going to pull an available one.
			// If this creates a lot of contention, then consider using 2 different mutexes.

			CLockLessStackContainer<NUMBER_OF_FRAGMENTS> m_FragmentsAvailableContainer;
			CLockLessStackContainer<NUMBER_OF_FRAGMENTS> m_FragmentsToDisplayContainer;
			CLockLessStack<NUMBER_OF_FRAGMENTS> m_FragmentsAvailable;
			CLockLessStack<NUMBER_OF_FRAGMENTS> m_FragmentsToDisplay;
			CFragmentInfo m_Fragments[NUMBER_OF_FRAGMENTS];

			unsigned short * m_pCachedIndexBuffer;
			void * m_pCachedVertexBuffer;

			// At some point, get rid of this extra layer of indirection
			VERTEX_BUFFER_CLASS * m_pVertexBuffer;
		};
	}
}
#endif

#endif

