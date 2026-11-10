//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include "blobulator/vertexbuffers/indextristripvertexbuffer.h"

//vbVertex_t IndexTriStripVertexBuffer::s_staticVertex;

IndexTriStripVertexBuffer::IndexTriStripVertexBuffer(void)
	: m_curTime(1)
	, m_nVerticesOutput(0)
	, m_pMesh(NULL)
	, m_pMeshBuilder(NULL)
	#if (COUNT_N_INDECIES)
		, m_nIndeciesOutput(0)
	#endif
{
}

IndexTriStripVertexBuffer::~IndexTriStripVertexBuffer(void)
{
}
