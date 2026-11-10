//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include "blobulator/point3d.h"
#include "blobulator/implicit/impparticle.h"
#include "blobulator/implicit/impdefines.h"
#include "blobulator/implicit/imprenderer.h"
#include "blobulator/implicit/imptiler.h"

#if ( RENDER_MODE == RENDER_BUCKET_IMPLEMENTATION )
#include "blobulator/bucket_implementation/cbucket_blob_renderer.h"
#elif (RENDER_MODE == RENDER_SECTION_IMPLEMENTATION )
#include "blobulator/section_implementation/csection_blob_renderer.h"
#elif (RENDER_MODE == RENDER_JOB_IMPLEMENTATION )
#include "blobulator/job_implementation/job_csection_blob_renderer.h"
#elif (RENDER_MODE == RENDER_LIGHT_BUCKET_IMPLEMENTATION )
#include "blobulator/light_bucket_implementation/light_csection_blob_renderer.h"
#endif

#include "blobulator/light_bucket_implementation/light_cfragmented_vertex_buffer.h"
#include "tier0/threadtools.h"
#include "memdbgon.h"

// TODO: Move this into ImpRendererFactory.cpp?
// TODO: I can make an abstract factory class that does all this for any object
// Find the right place to call CreateInstance() and ReleaseInstance(). As this demonstrates, usage of singleton should be avoided.
ImpRendererFactory* ImpRendererFactory::s_pInstance = ImpRendererFactory::CreateInstance();

ImpRendererFactory::ImpRendererFactory()
	:
	m_Mutex(),
	m_pFragmentedVertexBuffer(NULL),
	m_nNumberOfAcquiredTilerRenderers(0),
	m_nNumberOfAcquiredTileRenderers(0)
{
	m_pCSectionPool = new Blobulator::LightBucketImplementation::CSectionPool();
}

ImpRendererFactory::~ImpRendererFactory()
{
	delete m_pFragmentedVertexBuffer;
	delete m_pCSectionPool;
}

ImpRendererFactory * ImpRendererFactory::CreateInstance()
{
	Assert(s_pInstance == NULL);
	s_pInstance = new ImpRendererFactory();
	return s_pInstance;
}

void ImpRendererFactory::ReleaseInstance()
{
	Assert(s_pInstance != NULL);
	delete s_pInstance;
	s_pInstance = NULL;
}

int ImpRendererFactory::GetNumberOfAcquiredTileRenderers() const
{
	return m_nNumberOfAcquiredTileRenderers;
}

// Will clear the renderers only on the lists.
// This is safe to be called at any point as the renderers would not be in use, but it may leave some renderers behind in some cases.
// If all the renderers have been deleted, the vertex buffer is also freed.
void ImpRendererFactory::ClearRenderers()
{
	AUTO_LOCK(m_Mutex);

	while (m_TilerRenderers.size > 0)
	{
		BLOB_RENDERER_CLASS * pRenderer = m_TilerRenderers.pop();
		delete pRenderer;
	}

	while (m_TileRenderers.size > 0)
	{
		BLOB_RENDERER_CLASS * pRenderer = m_TileRenderers.pop();
		delete pRenderer;
	}

	if ( ( m_nNumberOfAcquiredTilerRenderers == 0 ) && ( m_nNumberOfAcquiredTileRenderers == 0) )
	{
		// No usage at all, we are fine deleting the fragmented vertex buffer
		delete m_pFragmentedVertexBuffer;
		m_pFragmentedVertexBuffer = NULL;
	}

	// Clear the CSection pool (CSections can consume several MB of memory)
	m_pCSectionPool->Purge();
}

BLOB_RENDERER_CLASS * ImpRendererFactory::AcquireTilerRenderer()
{
	AUTO_LOCK(m_Mutex);

	++m_nNumberOfAcquiredTilerRenderers;

	BLOB_RENDERER_CLASS * pRenderer;
	if (m_TilerRenderers.size > 0)
	{
		pRenderer = m_TilerRenderers.pop();
	}
	else
	{
		pRenderer = new Blobulator::LightBucketImplementation::LightweightRenderer(*GetFragmentedVertexBuffer(), true);
	}
	return pRenderer;
}

void ImpRendererFactory::ReleaseTilerRenderer(BLOB_RENDERER_CLASS * pRenderer)
{
	AUTO_LOCK(m_Mutex);

	Assert(m_nNumberOfAcquiredTilerRenderers >= 1);
	--m_nNumberOfAcquiredTilerRenderers;

	m_TilerRenderers.pushAutoSize(pRenderer);
}

BLOB_RENDERER_CLASS * ImpRendererFactory::AcquireTileRenderer()
{
	AUTO_LOCK(m_Mutex);

	++m_nNumberOfAcquiredTileRenderers;

	BLOB_RENDERER_CLASS * pRenderer;
	if(m_TileRenderers.size > 0)
	{
		pRenderer = m_TileRenderers.pop();
	}
	else
	{
#if ( RENDER_MODE == RENDER_BUCKET_IMPLEMENTATION )
		pRenderer = new Blobulator::BucketImplementation::CBucketBlobRenderer();
#elif ( RENDER_MODE == RENDER_SECTION_IMPLEMENTATION )
		pRenderer = new Blobulator::SectionImplementation::CSectionBlobRenderer();
#elif ( RENDER_MODE == RENDER_JOB_IMPLEMENTATION )
		pRenderer = new Blobulator::JobImplementation::CSectionBlobRenderer();
#elif ( RENDER_MODE == RENDER_LIGHT_BUCKET_IMPLEMENTATION )
		pRenderer = new Blobulator::LightBucketImplementation::CSectionBlobRenderer( *GetFragmentedVertexBuffer(), *m_pCSectionPool );
#else
		pRenderer = new RENDERER_CLASS();
#endif
	}
	return pRenderer;
}

// TODO: Make this thread-safe (in case multiple threads are simultaneously using a tiler)
void ImpRendererFactory::ReleaseTileRenderer(BLOB_RENDERER_CLASS * pRenderer)
{
	AUTO_LOCK(m_Mutex);

	Assert(m_nNumberOfAcquiredTileRenderers >= 1);
	--m_nNumberOfAcquiredTileRenderers;

	m_TileRenderers.pushAutoSize(pRenderer);
}

void ImpRendererFactory::FrameUpdate( int renderFrame )
{
	m_pCSectionPool->FrameUpdate( renderFrame );
}

Blobulator::LightBucketImplementation::CFragmentedVertexBuffer * ImpRendererFactory::GetFragmentedVertexBuffer()
{
	if (m_pFragmentedVertexBuffer == NULL)
	{
		// Only allocate at first usage
		m_pFragmentedVertexBuffer = new Blobulator::LightBucketImplementation::CFragmentedVertexBuffer();
	}
	return m_pFragmentedVertexBuffer;
}
