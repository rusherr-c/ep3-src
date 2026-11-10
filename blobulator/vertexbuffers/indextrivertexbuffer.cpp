//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include "blobulator/vertexbuffers/indextrivertexbuffer.h"

//vbVertex_t IndexTriVertexBuffer::s_staticVertex;

IndexTriVertexBuffer::IndexTriVertexBuffer(void)
	: m_curTime(1)
	, m_nVerticesOutput(0)
	, m_pMesh(NULL)
	, m_pMeshBuilder(NULL)
	#if (IMP_PROFILE == 1)
		, frame_no(0)
		, cubes_drawn(0)
		, indecies_drawn(0)
	#endif

	#if (COUNT_N_INDECIES)
		, m_nIndeciesOutput(0)
	#endif
{
}

IndexTriVertexBuffer::~IndexTriVertexBuffer(void)
{
}
