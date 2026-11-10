//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include "blobulator/vertexbuffers/sortedindextrivertexbuffer.h"

SortedIndexTriVertexBuffer::SortedIndexTriVertexBuffer(void)
	: m_pMesh(NULL)
	, m_pMeshBuilder(NULL)
	#if (COUNT_N_INDECIES)
		, m_nIndeciesOutput(0)
	#endif
{
}

SortedIndexTriVertexBuffer::~SortedIndexTriVertexBuffer(void)
{
}
