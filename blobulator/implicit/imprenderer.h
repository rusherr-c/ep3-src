//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

// CThreadFastMutex can be a typedef on some platforms, don't forward declare it.
#include "tier0/threadtools.h"

// NOTE: Wherever this file is included, ImpDefines.h should be included before this file is included.

// Unfortunately the old Blobulator code is completely baked in several classes :(
// We do not have the time to clean all the code, and abstract everything.
// So to make some of the code compile for abstracted type we use SweepRenderer for RENDERER_CLASS,
// but the real instantiated class will be a different one.
// Once we are happier with the Blobulator code, get rid of the old classes.

#if ( RENDER_MODE == RENDER_SWEEP )

	#include "sweeprenderer.h"
	#define RENDERER_CLASS			SweepRenderer

#elif ( RENDER_MODE == RENDER_MC )

	#include "mcrenderer.h"
	#define RENDERER_CLASS			MCRenderer

#elif ( RENDER_MODE == RENDER_HASH )

	#include "hashrenderer.h"
	#define RENDERER_CLASS			HashRenderer

#elif ( RENDER_MODE == RENDER_INF_HASH )

	#include "infhashrenderer.h"
	#define RENDERER_CLASS			InfHashRenderer

#elif ( RENDER_MODE == RENDER_VOLUMETRIC )

	#include "volumetricsweeprenderer.h"
	#define RENDERER_CLASS			VolumetricSweepRenderer

#elif ( RENDER_MODE == RENDER_BUCKET_IMPLEMENTATION ) || ( RENDER_MODE == RENDER_SECTION_IMPLEMENTATION ) || ( RENDER_MODE == RENDER_JOB_IMPLEMENTATION ) || ( RENDER_MODE == RENDER_LIGHT_BUCKET_IMPLEMENTATION )

	// Use the SweepRenderer so the old code can still compile, but the blob will use the abstraction.
	#include "sweeprenderer.h"
	#define RENDERER_CLASS			SweepRenderer
	#define BLOB_USE_ABSTRACTION	1

#else
#error	This RENDER_MODE is not supported
#endif

#if BLOB_USE_ABSTRACTION
	#define BLOB_RENDERER_CLASS		Blobulator::IBlobRenderer
	namespace Blobulator
	{
		class IBlobRenderer;
	}
#else
	#define BLOB_RENDERER_CLASS		RENDERER_CLASS
#endif

namespace Blobulator
{
	namespace LightBucketImplementation
	{
		class CFragmentedVertexBuffer;
		class CSectionPool;
	}
}

// TODO: Move this into ImpRendererFactory.h?
// TODO: Revisit this factory pattern. I made it a bit safer, but there is still the concern to determine where to call CallInstance() and ReleaseInstance().
//       Especially as now the vertex buffer is allocated within the factory (shared by all the renderer instances).
//		 To favor better practices, I did not make AcquireRenderer() and ReleaseRenderer() static (so we can easily switch to IRendererFactory).
//Note that there are 2 implementations of the renderer.
//	Tiler: Used for a given blob. Will contain several tiles (at least 1).
//			Handles vertex buffer operations, but doesn fill the buffers themselves.
//	Tile:  Sub-portion of a blob (100*100*100 cubes as of this writing).
//			Will fill the fragmented vertex buffers, but will not draw them.
//			Each tile can spawn up to several jobs 
class ImpRendererFactory
{
public:
	static ImpRendererFactory * CreateInstance();	// Returns the instance so we can keep old behavior.
	static void ReleaseInstance();

	FORCEINLINE
	static ImpRendererFactory * GetInstance()
	{
		return s_pInstance;
	}

	void ClearRenderers();

	// Lightweight renderer. No drawing, but can calculate constants and dispatch to vertex buffer.
	BLOB_RENDERER_CLASS * AcquireTilerRenderer();
	void ReleaseTilerRenderer(BLOB_RENDERER_CLASS * pRenderer);

	// Heavyweight renderer. Calculate and draw the blob.
	BLOB_RENDERER_CLASS * AcquireTileRenderer();
	void ReleaseTileRenderer(BLOB_RENDERER_CLASS * pRenderer);
	int GetNumberOfAcquiredTileRenderers() const;

	// Call to do once-per-frame work (call from the client thread, before any rendering for that frame)
	void FrameUpdate( int frameCount );

private:
	ImpRendererFactory();
	~ImpRendererFactory();

	// Start do not implement
	ImpRendererFactory(const ImpRendererFactory & other);
	ImpRendererFactory & operator=(const ImpRendererFactory & other);
	// End do not implement

	Blobulator::LightBucketImplementation::CFragmentedVertexBuffer * GetFragmentedVertexBuffer();

	SmartArray<BLOB_RENDERER_CLASS *> m_TileRenderers;
	SmartArray<BLOB_RENDERER_CLASS *> m_TilerRenderers;
	CThreadFastMutex m_Mutex;			// Contentions should be really rare. Use a fast mutex.
	Blobulator::LightBucketImplementation::CFragmentedVertexBuffer * m_pFragmentedVertexBuffer;
	Blobulator::LightBucketImplementation::CSectionPool * m_pCSectionPool;
	int m_nNumberOfAcquiredTilerRenderers;
	int m_nNumberOfAcquiredTileRenderers;
	static ImpRendererFactory * s_pInstance;
};

