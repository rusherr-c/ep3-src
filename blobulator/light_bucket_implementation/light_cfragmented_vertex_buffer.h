//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CFRAGMENTED_VERTEX_BUFFER_H
#define BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CFRAGMENTED_VERTEX_BUFFER_H

#ifndef SPU
// This class should not be referenced by the SPU code

#include "blobulator/light_bucket_implementation/light_cfragment_info.h"
#include "blobulator/implicit/impdefines.h"
#include "blobulator/vertexbuffers/vertexbuffer.h"
#include "blobulator/smartarray.h"
#include "blobulator/light_bucket_implementation/light_csoftware_cache.h"
#include "blobulator/light_bucket_implementation/light_details.h"

#include "tier0/threadtools.h"

class ImpTile;

namespace Blobulator
{
	namespace LightBucketImplementation
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

			// Draws the fragments for a given slot.
			// A fragment index pointer can be provided so the fragment is not pushed back on the stack but instead returned
			// to the caller of this function (this is to remove the risk that one thread draw the fragments to get one available,
			// and another thread - a helper job - takes it).
			bool DrawFragments(int nSlotIndex, int * pnOutputFragmentIndex = NULL);
			void ClearFragmentsToDisplay();

			void CheckResetState(int nSlotIndex);

			// Gets the pointers for the various stacks, and returns the slot used.
			int RetrievePointers(CLockLessStack<BLOB_NUMBER_OF_FRAGMENTS> * * pOutFragmentsAvailable, CLockLessStack<BLOB_NUMBER_OF_FRAGMENTS> * * pOutFragmentsToDisplay, CFragmentInfo * * pOutAllFragments);

			static const int FRAGMENT_INDEX_BUFFER_SIZE = 4096;
			static const int FRAGMENT_VERTEX_BUFFER_SIZE = FRAGMENT_INDEX_BUFFER_SIZE / 4;	// There is in average of 5 indices submitted by vertex.
			BLOB_COMPILE_TIME_ASSERT( ( FRAGMENT_INDEX_BUFFER_SIZE % 16 ) == 0 );
			BLOB_COMPILE_TIME_ASSERT( ( FRAGMENT_VERTEX_BUFFER_SIZE % 16 )  == 0);

			static int GetVertexSize();

		private:
			static const int FRAGMENT_INDEX_THRESHOLD = 24;
			static const int FRAGMENT_VERTEX_THRESHOLD = 24;

			// A lot of slots because we can several blobs at the same time, and each blob can have several tiles.
			// Each tile has its renderer associated. And each renderer has a slot (because of the different view).
			// Fortunately we are reusing the renderer but still, with the asynchronous mode there is more jobs going on at once.
			// There is only one list of available containers though.
			static const int NUMBER_OF_SLOTS = 64;

			// This represents an array of 128K values, i.e. 256 Kb
			// I have seen current worst case of 82000 indices submitted. It could be worse.
			static const int NUMBER_OF_INDICES_RESERVED = BLOB_NUMBER_OF_FRAGMENTS * FRAGMENT_INDEX_BUFFER_SIZE;

			// This makes the buffer way too big... :( 1.7 MB currently.
			static const int NUMBER_OF_VERTICES_RESERVED = BLOB_NUMBER_OF_FRAGMENTS * FRAGMENT_VERTEX_BUFFER_SIZE;

			// We will only need one mutex for both arrays.
			// As if in most cases, if we are pushing a fragment to display, we are going to pull an available one.
			// If this creates a lot of contention, then consider using 2 different mutexes.

			CLockLessStackContainer<BLOB_NUMBER_OF_FRAGMENTS> m_FragmentsAvailableContainer;
			CLockLessStackContainer<BLOB_NUMBER_OF_FRAGMENTS> m_FragmentsToDisplayContainer[NUMBER_OF_SLOTS];
			CLockLessStack<BLOB_NUMBER_OF_FRAGMENTS> m_FragmentsAvailable;
			CLockLessStack<BLOB_NUMBER_OF_FRAGMENTS> m_FragmentsToDisplay[NUMBER_OF_SLOTS];
			CFragmentInfo m_Fragments[BLOB_NUMBER_OF_FRAGMENTS];

			unsigned short * m_pCachedIndexBuffer;
			void * m_pCachedVertexBuffer;

			// At some point, get rid of this extra layer of indirection
			VERTEX_BUFFER_CLASS * m_pVertexBuffer;

			int m_nCurrentSlot;
		};
	}
}
#endif

#endif

