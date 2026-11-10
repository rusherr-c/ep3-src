//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include "blobulator/vertexbuffers/tristripvertexbuffer.h"


TriStripVertexBuffer::TriStripVertexBuffer(void)
	#if (RENDER_API != VALVE_API)
		: m_pMesh(NULL)
		, m_pMeshBuilder(NULL)
	#endif
{
}

TriStripVertexBuffer::~TriStripVertexBuffer(void)
{
}
