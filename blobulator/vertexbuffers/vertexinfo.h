//========= Copyright (c), Valve Corporation, All rights reserved. ============//

#ifndef BLOBULATOR_VERTEXBUFFERS_VERTEXINFO_H
#define BLOBULATOR_VERTEXBUFFERS_VERTEXINFO_H

// TODO: Would be nice to size this according to the type
// of vertex buffer we were using. For example, a single USHORT
// might be good enough for a TriStripVertexBuffer
struct vbId_t
{
	unsigned short time;
	unsigned short id;
};

#endif
