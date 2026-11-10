//========= Copyright 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef IBLOB_RENDERER_H
#define IBLOB_RENDERER_H

class ImpTile;
class ImpParticle;
class Point3D;
class IVDebugOverlay;
class IMatRenderContext;
class IMaterial;

struct matrix3x4_t;

namespace Blobulator
{
	extern const float g_ExclusionXMin;
	extern const float g_ExclusionXMax;
	extern const float g_ExclusionYMin;
	extern const float g_ExclusionYMax;
	extern const float g_ExclusionZMin;
	extern const float g_ExclusionZMax;

	class IBlobRenderer
	{
	public:
		FORCEINLINE
		virtual ~IBlobRenderer()
		{
			// Do nothing...
		}

		virtual void BeginFrame(bool bTileMode, void * pRenderContext) = 0;
		virtual void EndFrame() = 0;

		// Note that for a given tile, BeginTile(), DrawTile() and EndTile() will always be called in this order.
		// AddParticle() will be called after BeginTile() and before DrawTile().
		virtual void BeginTile(const Point3D & offset) = 0;
		virtual void DrawTile() = 0;
		virtual void EndTile() = 0;

		virtual void AddParticle(ImpParticle & particle, bool bSkipBoundsCheck) = 0;

		virtual void SetCubeWidth(float fCubeWidth) = 0;
		virtual float GetCubeWidth() const = 0;
		virtual void SetRenderRadius(float fRenderRadius) = 0;
		virtual float GetRenderRadius() const = 0;
		virtual void SetCutoffRadius(float fCutoffRadius) = 0;
		virtual float GetCutoffRadius() const = 0;

		virtual const Point3D & GetInnerDimensions() const = 0;
		virtual float GetMarginWidth() const = 0;

		virtual void SetPortalMatrices(const matrix3x4_t * pMatrices, int nNumberOfMatrices) = 0;

		// Sets the debug mode, this may change the rendering of the normal mode.
		virtual void SetDebugMode(bool bDebugMode) = 0;
		virtual bool GetDebugMode() const = 0;
		// Returns the number of debug information the blob contains.
		// Depending of the implementation, this can represent a slice, a bucket, a section, etc...
		virtual int GetNumberOfDebugInfo() const = 0;
		// If nDebugInfoIndex is -1, all debug info will be displayed.
		// Call GetNumberOfDebugInfo() first to know the index (from 0 to GetNumberOfDebugInfo()).
		virtual void DrawDebugInfo(IVDebugOverlay & debugOverlay, int nDebugInfoIndex) = 0;

		virtual int GetDebugId() const = 0;

	protected:
		FORCEINLINE
		IBlobRenderer()
		{
			// Do nothing...
		}
	private:
		// Start do not implement
		IBlobRenderer(const IBlobRenderer & other);
		IBlobRenderer & operator=(const IBlobRenderer & other);
		// End do not implement
	};

	struct BlobRenderInfo_t
	{
		float m_flCubeWidth;
		float m_flCutoffRadius;
		float m_flRenderRadius;
		float m_flViewScale; // to scale resolution of the blobs (higher scale == lower poly count)
		int m_nViewID;
	};

	void RenderBlob( bool bUseBlobulator, IMatRenderContext* pRenderContext, IMaterial* pMaterial, const BlobRenderInfo_t& blobRenderInfo, const matrix3x4_t* pPortalMatrices, int nPortalMatrixCount, ImpParticle* pParticles, int nParticleCount );
	void FrameUpdate( int renderFrame );
	void ShutdownBlob();
}

#endif

