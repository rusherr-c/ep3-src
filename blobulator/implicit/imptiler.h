//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "../smartarray.h"
#include "../vertexbuffers/vertexbuffer.h"
#include "impdefines.h"
#include "imprenderer.h"
#include "materialsystem/imaterialsystem.h"

// CThreadFastMutex can be a typedef on some platforms, don't forward declare it.
#include "tier0/threadtools.h"

class ImpParticle;

extern Point3D tile_debug_color;

class ImpTile
{
public:
	ImpTile(int _x, int _y, int _z);
	~ImpTile();
	bool done;
	int x, y, z;
	Point3D center;
	#if(COLOR_TILES==1)
		bool old_tile;
		Point3D debug_color;
	#endif
	float dist;
	SmartArray<ImpParticle*> particles;
	BLOB_RENDERER_CLASS * m_pRenderer;

	#if (MT_ENABLED == 1)
		SmartArray<vbBuffer_t*> buffers;
	#endif
};


class ImpTiler : public CAlignedNewDelete<16>
{
public:
	ImpTiler();
	~ImpTiler(void);

	void beginFrame(const Point3D & offset, bool bTileMode, bool bUseTwoPasses);
	void insertParticle(ImpParticle * p);
	void drawSurface(bool bUseTwoPasses);
	void drawSurfaceSorted(const Point3D & eye, bool bUseTwoPasses);

	int getNoTiles();
	ImpTile* getTile(int tileNo);
	Point3D getTileOffset(int tileNo);

	void setTileIndexToDraw(int no = -1) { m_nTileIndexToDraw = no; }
	int getTileIndexToDraw() const { return m_nTileIndexToDraw; }
	Point3D getRenderDim() { return m_RenderDimension; }
	Point3D getLastTilesOffset() { return m_LastTileOffset; }

	#if(MT_ENABLED == 1)
		void drawSurfaceSortedMT(bool depthSort, Point3D const& eye);
	#endif

	void endFrame(bool bUseTwoPasses);

	// These should only be used for debugging purposes
	void drawTile(ImpTile * pTile, bool bUseTwoPasses);
	void drawTile(int x, int y, int z, bool bUseTwoPasses);

	void SetCubeWidth(float fCubeWidth);
	void SetRenderRadius(float fRenderRadius);
	void SetCutoffRadius(float fCuttoffRadius);
	void SetRenderContext(void * pRenderContext);
	void SetPortalMatrices(const matrix3x4_t * pMatrices, int nNumberOfMatrices);

private:
	SmartArray<ImpTile*> m_Tiles;

	int m_nTileIndexToDraw;
	Point3D m_LastTileOffset;

	Point3D m_Offset;
	Point3D m_RenderDimension;
	float m_fRenderMargin;
	Point3D m_CornerOffset;
	bool m_bTileMode;
	void * m_pRenderContext;
	const matrix3x4_t * m_pMatrices;
	int m_nNumberOfMatrices;

	float m_fCubeWidth, m_fRenderRadius, m_fCuttoffRadius;
	// This renderer will NOT be used to render, but is used to make sure that we have the correct dimensions.
	// Failure to do so will display incorrectly the blob. In the current state of the code,
	// esp. regarding the memory cost of the renderer, there is a lot of wasted memory here (~32 Kb) but that's the safe way for the moment.
	// Revisit this to only include the calculation (could we even make the calculation shared by all implementations?).
	// Also this potentially can have some side effects with BeginTile(), DrawTile(), EndTile() not being called. Revisit this...
	BLOB_RENDERER_CLASS * m_pRenderer;

	Point3D calcTileCorner(int x, int y, int z);
	Point3D calcTileOffset(int x, int y, int z);

	#if(MT_ENABLED == 1)
		void launchThreads();
	#endif
	void addParticleToTile(ImpParticle*p, int x, int y, int z);
	ImpTile* findTile(int x, int y, int z);
	ImpTile* createTile(int x, int y, int z);
};


// Algorithm... calculate how many cubes have to be overlapped in order to not miss particles.
// should be roundup(render_radius/cubewidth);
// Next, can calculate how many cubes are left and what the width/height/depth of a tile is (dx, dy, dz).
// Insert particles into the appropriate tile is easy.
// Rendering... change render offsets so that we're rendering at center of tile. just offset each particle so that it's at the center of the tile.



// TODO: Move this into ImpTilerFactory.h?
class ImpTilerFactory
{
public:
	static ImpTilerFactory * CreateInstance();	// Returns the instance so we can keep old behavior.
	static void ReleaseInstance();

	FORCEINLINE
	static ImpTilerFactory * GetInstance()
	{
		return s_pInstance;
	}

	ImpTiler * AcquireTiler();
	void ReleaseTiler(ImpTiler * pTiler);

	void ClearImpTilers(); // called upon Shutdown

private:
	ImpTilerFactory();
	~ImpTilerFactory();

	// Start do not implement
	ImpTilerFactory(const ImpTilerFactory & other);
	ImpTilerFactory & operator=(const ImpTilerFactory & other);
	// End do not implement

	static void ClearImpTilers( void * pUserData );

	SmartArray<ImpTiler *> m_tilers;
	CThreadFastMutex m_Mutex;			// Contentions should be really rare. Use a fast mutex.
	bool m_bAddedToMaterials;

	static ImpTilerFactory * s_pInstance;
};