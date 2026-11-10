//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_SECTIONIMPLEMENTATION_CFRAGMENTED_VERTEX_BUFFER_H
#define BLOBULATOR_SECTIONIMPLEMENTATION_CFRAGMENTED_VERTEX_BUFFER_H

#include "blobulator/section_implementation/cfragment_info.h"
#include "blobulator/implicit/impdefines.h"
#include "blobulator/vertexbuffers/vertexbuffer.h"
#include "blobulator/smartarray.h"

#include "tier0/threadtools.h"

class ImpTile;

namespace Blobulator
{
	namespace SectionImplementation
	{
		class CJobCache;
		class CFragmentInfo;

		class CFragmentedVertexBuffer
		{
		public:
			CFragmentedVertexBuffer();
			~CFragmentedVertexBuffer();

			void BeginFrame(void * pRenderContext);
			void EndFrame();
			void BeginTile();
			void EndTile();

			bool DrawFragments();

			// This should be called per job when we start a frame.
			// If used improperly (like too many times with no code drawing and pushing back free items), it may 
			CFragmentInfo GetAvailableFragment();
			// This should be called when a fragment is ready to be drawn.
			// Be *very careful* that each call to this function means that the indices are reset.
			// Any call to this should reset the cache afterward.
			void PushFragmentToDraw(CFragmentInfo & fragment);

			// This will push the current fragment for display, and will replace it with a free fragment.
			// Same comment as PushFragmentToDraw.
			// Be *very careful* that each call to this function means that the indices are reset.
			// Any call to this should reset the cache afterward.
			// Returns true if the new fragment is usable, false otherwise.
			bool SwapFragments(CFragmentInfo & fragmentToSwap);

			void CheckResetState();

			static const int NUMBER_OF_FRAGMENTS = 12;

		private:
			static const int FRAGMENT_INDEX_BUFFER_SIZE = 8192;
			static const int FRAGMENT_VERTEX_BUFFER_SIZE = FRAGMENT_INDEX_BUFFER_SIZE / 4;	// There is in average of 5 indices submitted by vertex.
			static const int FRAGMENT_INDEX_THRESHOLD = 24;
			static const int FRAGMENT_VERTEX_THRESHOLD = 24;

			// This represents an array of 128K values, i.e. 256 Kb
			// I have seen current worst case of 82000 indices submitted. It could be worse.
			static const int NUMBER_OF_INDICES_RESERVED = NUMBER_OF_FRAGMENTS * FRAGMENT_INDEX_BUFFER_SIZE;

			// This makes the buffer way too big... :( 1.7 MB currently.
			static const int NUMBER_OF_VERTICES_RESERVED = NUMBER_OF_FRAGMENTS * FRAGMENT_VERTEX_BUFFER_SIZE;

			// At some point, get rid of this extra layer of indirection
			VERTEX_BUFFER_CLASS * m_pVertexBuffer;

			// We will only need one mutex for both arrays.
			// As if in most cases, if we are pushing a fragment to display, we are going to pull an available one.
			// If this is creating a lot of contention, then consider using 2 different mutexes.
			volatile CThreadFastMutex m_Mutex;
			SmartArray<CFragmentInfo> m_FragmentsAvailable;
			SmartArray<CFragmentInfo> m_FragmentsToDisplay;

			unsigned short * m_pCachedIndexBuffer;
			void * m_pCachedVertexBuffer;
		};
	}
}

#endif

