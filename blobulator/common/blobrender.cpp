//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "tier0/vprof.h"
#include "tier1/callqueue.h"

#include "materialsystem/imaterial.h"
#include "materialsystem/imesh.h"

#include "blobulator/iblob_renderer.h"

#include "../common/blobulator/implicit/impdefines.h"
#include "../common/blobulator/implicit/imprenderer.h"
#include "../common/blobulator/implicit/imptiler.h"
#include "../common/blobulator/implicit/userfunctions.h"

#include "tier1/convar.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


typedef CUtlEnvelope<ImpParticle> BlobParticlesEnvelope_t;
typedef CUtlEnvelope<matrix3x4_t> BlobMatricesEnvelope_t;


ConVar r_paintblob_draw_isosurface( "r_paintblob_draw_isosurface", "1", 0, "Draws the surface as an isosurface" );
ConVar r_paintblob_rotate( "r_paintblob_rotate", "0", FCVAR_NONE, "Whether to rotate for transparency" );

ConVar r_paintblob_blr_scale( "r_paintblob_blr_scale", "1.0", FCVAR_NONE, "Scale all surface rendering parameters." );
ConVar r_paintblob_highres_cube( "r_paintblob_highres_cube", "0.8", FCVAR_NONE, "Set cubewidth (coarseness of the mesh)" );

ConVar r_paintblob_calc_uv_and_tan( "r_paintblob_calc_uv_and_tan", "1", FCVAR_ARCHIVE, "Calculate UVs and Tangents" );
ConVar r_paintblob_calc_tan_only( "r_paintblob_calc_tan_only", "0", FCVAR_ARCHIVE, "Calculate Only Tangents" );
ConVar r_paintblob_calc_color( "r_paintblob_calc_color", "0", FCVAR_ARCHIVE, "Just interpolate colors" );
ConVar r_paintblob_calc_hifreq_color( "r_paintblob_calc_hifreq_color", "0", FCVAR_ARCHIVE, "Experimental hi-freq colors" );
ConVar r_paintblob_calc_tile_color( "r_paintblob_calc_tile_color", "0", FCVAR_ARCHIVE, "Shows color of the tile" );

ConVar r_paintblob_tile_index_to_draw( "r_paintblob_tile_index_to_draw", "-1", FCVAR_NONE, "-1 to display all tiles. Otherwise the index of the tile to draw." );
ConVar r_paintblob_debug_draw_margin("r_paintblob_debug_draw_margin", "0", FCVAR_NONE, "If tiler is disabled, whether to draw the margin.");
ConVar r_paintblob_debug_draw_tile_boundaries("r_paintblob_debug_draw_tile_boundaries", "0", FCVAR_NONE, "Whether to draw outlines of all tiles.");

ConVar r_paintblob_wireframe( "r_paintblob_wireframe", "0", FCVAR_NONE, "Draw wireframe" );
ConVar r_paintblob_material("r_paintblob_material", "-1", FCVAR_NONE, "Choose a material from 0 to N");
ConVar r_paintblob_shader("r_paintblob_shader", "", FCVAR_NONE, "Choose a shader");

ConVar r_threaded_blobulator("r_threaded_blobulator", "1", FCVAR_NONE, "If enabled, blobulator will use material thread.");

ConVar r_paintblob_mainview_highres("r_paintblob_mainview_highres", "1", FCVAR_NONE, "If 1, make the main view high resolution. Set to 0 to make the main view low resolution.");
ConVar r_paintblob_otherviews_highres("r_paintblob_otherviews_highres", "0", FCVAR_NONE, "If 0, all other views are low resolution. Set to 1 to make other views high resolution.");
ConVar r_paintblob_only_mainview_displayed("r_paintblob_only_mainview_displayed", "0", FCVAR_NONE, "If 0, other views are displayed (portal, shadow, reflection, ...). Note that shadows are disabled on game consoles. Set to 1 to only display the main view.");

ConVar r_paintblob_force_single_pass( "r_paintblob_force_single_pass", "0", FCVAR_NONE, "If 0, render the blob in two passes. Set to 1 to force rendering of the blob in a single pass." );

namespace Blobulator
{

//-----------------------------------------------------------------------------
// Purpose: Draw a Sphere
//-----------------------------------------------------------------------------

float g_FastSpherePosData[51][8] = {
	{  0.0000,  0.0000,  1.0000,  0.0000,  0.0000,  0.0000,  0.0000,  1.0000 },
	{ -0.0000,  0.5000,  0.8660,  0.2500,  0.1667, -0.0000,  0.5000,  0.8660 },
	{  0.5000,  0.0000,  0.8660,  0.0000,  0.1667,  0.5000,  0.0000,  0.8660 },
	{  0.0000,  0.0000,  1.0000,  0.5000,  0.0000,  0.0000,  0.0000,  1.0000 },
	{ -0.5000, -0.0000,  0.8660,  0.5000,  0.1667, -0.5000, -0.0000,  0.8660 },
	{  0.0000,  0.0000, -1.0000,  0.0000,  1.0000,  0.0000,  0.0000, -1.0000 },
	{  0.5000,  0.0000, -0.8660,  0.0000,  0.8333,  0.5000,  0.0000, -0.8660 },
	{ -0.0000,  0.5000, -0.8660,  0.2500,  0.8333, -0.0000,  0.5000, -0.8660 },
	{  0.0000,  0.0000, -1.0000,  0.5000,  1.0000,  0.0000,  0.0000, -1.0000 },
	{ -0.5000, -0.0000, -0.8660,  0.5000,  0.8333, -0.5000, -0.0000, -0.8660 },
	{  0.0000, -0.5000,  0.8660,  0.7500,  0.1667,  0.0000, -0.5000,  0.8660 },
	{  0.0000,  0.0000,  1.0000,  1.0000,  0.0000,  0.0000,  0.0000,  1.0000 },
	{  0.5000,  0.0000,  0.8660,  1.0000,  0.1667,  0.5000,  0.0000,  0.8660 },
	{  0.0000, -0.5000, -0.8660,  0.7500,  0.8333,  0.0000, -0.5000, -0.8660 },
	{  0.0000,  0.0000, -1.0000,  1.0000,  1.0000,  0.0000,  0.0000, -1.0000 },
	{  0.5000,  0.0000, -0.8660,  1.0000,  0.8333,  0.5000,  0.0000, -0.8660 },
	{  0.6124,  0.6124,  0.5000,  0.1250,  0.3333,  0.6124,  0.6124,  0.5000 },
	{  0.8660,  0.0000,  0.5000,  0.0000,  0.3333,  0.8660,  0.0000,  0.5000 },
	{ -0.0000,  0.8660,  0.5000,  0.2500,  0.3333, -0.0000,  0.8660,  0.5000 },
	{  0.8660,  0.0000, -0.5000,  0.0000,  0.6667,  0.8660,  0.0000, -0.5000 },
	{  0.6124,  0.6124, -0.5000,  0.1250,  0.6667,  0.6124,  0.6124, -0.5000 },
	{ -0.0000,  0.8660, -0.5000,  0.2500,  0.6667, -0.0000,  0.8660, -0.5000 },
	{ -0.6124,  0.6124,  0.5000,  0.3750,  0.3333, -0.6124,  0.6124,  0.5000 },
	{ -0.8660, -0.0000,  0.5000,  0.5000,  0.3333, -0.8660, -0.0000,  0.5000 },
	{ -0.6124,  0.6124, -0.5000,  0.3750,  0.6667, -0.6124,  0.6124, -0.5000 },
	{ -0.8660, -0.0000, -0.5000,  0.5000,  0.6667, -0.8660, -0.0000, -0.5000 },
	{ -0.6124, -0.6124,  0.5000,  0.6250,  0.3333, -0.6124, -0.6124,  0.5000 },
	{  0.0000, -0.8660,  0.5000,  0.7500,  0.3333,  0.0000, -0.8660,  0.5000 },
	{ -0.6124, -0.6124, -0.5000,  0.6250,  0.6667, -0.6124, -0.6124, -0.5000 },
	{  0.0000, -0.8660, -0.5000,  0.7500,  0.6667,  0.0000, -0.8660, -0.5000 },
	{  0.6124, -0.6124,  0.5000,  0.8750,  0.3333,  0.6124, -0.6124,  0.5000 },
	{  0.8660,  0.0000,  0.5000,  1.0000,  0.3333,  0.8660,  0.0000,  0.5000 },
	{  0.6124, -0.6124, -0.5000,  0.8750,  0.6667,  0.6124, -0.6124, -0.5000 },
	{  0.8660,  0.0000, -0.5000,  1.0000,  0.6667,  0.8660,  0.0000, -0.5000 },
	{  0.9239,  0.3827,  0.0000,  0.0625,  0.5000,  0.9239,  0.3827,  0.0000 },
	{  1.0000,  0.0000,  0.0000,  0.0000,  0.5000,  1.0000,  0.0000,  0.0000 },
	{  0.7071,  0.7071,  0.0000,  0.1250,  0.5000,  0.7071,  0.7071,  0.0000 },
	{  0.3827,  0.9239,  0.0000,  0.1875,  0.5000,  0.3827,  0.9239,  0.0000 },
	{ -0.0000,  1.0000,  0.0000,  0.2500,  0.5000, -0.0000,  1.0000,  0.0000 },
	{ -0.3827,  0.9239,  0.0000,  0.3125,  0.5000, -0.3827,  0.9239,  0.0000 },
	{ -0.7071,  0.7071,  0.0000,  0.3750,  0.5000, -0.7071,  0.7071,  0.0000 },
	{ -0.9239,  0.3827,  0.0000,  0.4375,  0.5000, -0.9239,  0.3827,  0.0000 },
	{ -1.0000, -0.0000,  0.0000,  0.5000,  0.5000, -1.0000, -0.0000,  0.0000 },
	{ -0.9239, -0.3827,  0.0000,  0.5625,  0.5000, -0.9239, -0.3827,  0.0000 },
	{ -0.7071, -0.7071,  0.0000,  0.6250,  0.5000, -0.7071, -0.7071,  0.0000 },
	{ -0.3827, -0.9239,  0.0000,  0.6875,  0.5000, -0.3827, -0.9239,  0.0000 },
	{  0.0000, -1.0000,  0.0000,  0.7500,  0.5000,  0.0000, -1.0000,  0.0000 },
	{  0.3827, -0.9239,  0.0000,  0.8125,  0.5000,  0.3827, -0.9239,  0.0000 },
	{  0.7071, -0.7071,  0.0000,  0.8750,  0.5000,  0.7071, -0.7071,  0.0000 },
	{  0.9239, -0.3827,  0.0000,  0.9375,  0.5000,  0.9239, -0.3827,  0.0000 },
	{  1.0000,  0.0000,  0.0000,  1.0000,  0.5000,  1.0000,  0.0000,  0.0000 }
};


int g_FastSphereTriData[84][3] = {
	{ 0, 1, 2 },
	{ 0, 3, 1 },
	{ 3, 4, 1 },
	{ 5, 6, 7 },
	{ 5, 7, 8 },
	{ 8, 7, 9 },
	{ 3, 10, 4 },
	{ 3, 11, 10 },
	{ 11, 12, 10 },
	{ 8, 9, 13 },
	{ 8, 13, 14 },
	{ 14, 13, 15 },
	{ 2, 16, 17 },
	{ 2, 1, 16 },
	{ 1, 18, 16 },
	{ 6, 19, 20 },
	{ 6, 20, 7 },
	{ 7, 20, 21 },
	{ 1, 22, 18 },
	{ 1, 4, 22 },
	{ 4, 23, 22 },
	{ 7, 21, 24 },
	{ 7, 24, 9 },
	{ 9, 24, 25 },
	{ 4, 26, 23 },
	{ 4, 10, 26 },
	{ 10, 27, 26 },
	{ 9, 25, 28 },
	{ 9, 28, 13 },
	{ 13, 28, 29 },
	{ 10, 30, 27 },
	{ 10, 12, 30 },
	{ 12, 31, 30 },
	{ 13, 29, 32 },
	{ 13, 32, 15 },
	{ 15, 32, 33 },
	{ 17, 34, 35 },
	{ 17, 16, 34 },
	{ 16, 36, 34 },
	{ 19, 35, 34 },
	{ 19, 34, 20 },
	{ 20, 34, 36 },
	{ 16, 37, 36 },
	{ 16, 18, 37 },
	{ 18, 38, 37 },
	{ 20, 36, 37 },
	{ 20, 37, 21 },
	{ 21, 37, 38 },
	{ 18, 39, 38 },
	{ 18, 22, 39 },
	{ 22, 40, 39 },
	{ 21, 38, 39 },
	{ 21, 39, 24 },
	{ 24, 39, 40 },
	{ 22, 41, 40 },
	{ 22, 23, 41 },
	{ 23, 42, 41 },
	{ 24, 40, 41 },
	{ 24, 41, 25 },
	{ 25, 41, 42 },
	{ 23, 43, 42 },
	{ 23, 26, 43 },
	{ 26, 44, 43 },
	{ 25, 42, 43 },
	{ 25, 43, 28 },
	{ 28, 43, 44 },
	{ 26, 45, 44 },
	{ 26, 27, 45 },
	{ 27, 46, 45 },
	{ 28, 44, 45 },
	{ 28, 45, 29 },
	{ 29, 45, 46 },
	{ 27, 47, 46 },
	{ 27, 30, 47 },
	{ 30, 48, 47 },
	{ 29, 46, 47 },
	{ 29, 47, 32 },
	{ 32, 47, 48 },
	{ 30, 49, 48 },
	{ 30, 31, 49 },
	{ 31, 50, 49 },
	{ 32, 48, 49 },
	{ 32, 49, 33 },
	{ 33, 49, 50 }
};


// We support up to 4 matrices for exclusion.
static const int MAX_NUMBER_OF_EXCLUSION_MATRICES = 4;

// Use transposed matrices to optimize SIMD calculations
struct TransposedMatrix
{
	fltx4 m_Translation;
	fltx4 m_ColumnX;
	fltx4 m_ColumnY;
	fltx4 m_ColumnZ;
};

TransposedMatrix g_TransposedPortalMatrices[MAX_NUMBER_OF_EXCLUSION_MATRICES];
TransposedMatrix g_TransposedInvertedPortalMatrices[MAX_NUMBER_OF_EXCLUSION_MATRICES];
int g_nNumberOfMatrices;

void SetPortalMatrices(const matrix3x4_t * pMatrices, int nNumberOfMatrices)
{
	if (nNumberOfMatrices > MAX_NUMBER_OF_EXCLUSION_MATRICES)
	{
		nNumberOfMatrices = MAX_NUMBER_OF_EXCLUSION_MATRICES;
	}
	g_nNumberOfMatrices = nNumberOfMatrices;

	// Copy internally the non aligned matrix to the aligned matrix.
	// The matrix is in full in CJobCache so there i.e. no need to DMA it in.
	// For the same reason, we calculate the inverted matrix here.
	// Note that we invert the matrix in each job, where we could invert them once for all jobs.
	for (int i = 0 ; i < nNumberOfMatrices ; ++i)
	{
		matrix3x4a_t portalMatrix;
		portalMatrix = pMatrices[i];
		matrix3x4a_t outInvertedMatrix;
		MatrixInvert(portalMatrix, outInvertedMatrix);

		// Same but this time transposed...

		fltx4 x, y, z, f4Translation;
		x = LoadAlignedSIMD(portalMatrix[0]);
		y = LoadAlignedSIMD(portalMatrix[1]);
		z = LoadAlignedSIMD(portalMatrix[2]);
		f4Translation = Four_Zeros;
		TransposeSIMD(x, y, z, f4Translation);
		g_TransposedPortalMatrices[i].m_ColumnX = x;
		g_TransposedPortalMatrices[i].m_ColumnY = y;
		g_TransposedPortalMatrices[i].m_ColumnZ = z;
		g_TransposedPortalMatrices[i].m_Translation = f4Translation;

		x = LoadAlignedSIMD(outInvertedMatrix[0]);
		y = LoadAlignedSIMD(outInvertedMatrix[1]);
		z = LoadAlignedSIMD(outInvertedMatrix[2]);
		f4Translation = Four_Zeros;
		TransposeSIMD(x, y, z, f4Translation);
		g_TransposedInvertedPortalMatrices[i].m_ColumnX = x;
		g_TransposedInvertedPortalMatrices[i].m_ColumnY = y;
		g_TransposedInvertedPortalMatrices[i].m_ColumnZ = z;
		g_TransposedInvertedPortalMatrices[i].m_Translation = f4Translation;
	}
}


// All the code to clip the blob behind the portal is excluded for the moment as it generates a compile error.
#ifdef OSX
// GCC 4.2 on OSX doesn't like defining a SIMD static const object, it fails to produce correct assembly due to the constraints, so just
// don't const it on OSX
#define PLAT_CONST 
#else
#define PLAT_CONST const
#endif
static PLAT_CONST fltx4 s_ExclusionBoxMin = { Blobulator::g_ExclusionXMin, Blobulator::g_ExclusionYMin, Blobulator::g_ExclusionZMin, 0.0f };
static PLAT_CONST fltx4 s_ExclusionBoxMax = { Blobulator::g_ExclusionXMax, Blobulator::g_ExclusionYMax, Blobulator::g_ExclusionZMax, 0.0f };
FORCEINLINE bool IsPointInBox( const fltx4 & position )
{
	Assert(SubFloat(position, 3) == 0.0f);          // Optimized for W == 0.0f

	bi32x4 greater = CmpGtSIMD( position, s_ExclusionBoxMax );
	bi32x4 less = CmpLtSIMD( position, s_ExclusionBoxMin );
	return (IsAllZeros(OrSIMD(greater,less)));
}


FORCEINLINE void PortalBoxProjection( Vector& vPosition )
{
	fltx4 f4Position = LoadUnaligned3SIMD( vPosition.Base() );
	fltx4 f4PositionX = SplatXSIMD(f4Position);
	fltx4 f4PositionY = SplatYSIMD(f4Position);
	fltx4 f4PositionZ = SplatZSIMD(f4Position);

	// In most cases, we are comparing two portal at a time
	// And if there is only one portal, the blob is not going to go through one portal
	// to the other, thus we don't have any clipping issues...
	// So let's calculate 2 boxes at a time (that's almost the same cost as one box).
	// Note that the list of portals is already clipped against the blob at a higher level, so there may be an even number of portals.
	int i = 0;
	while (i + 2 <= g_nNumberOfMatrices)
	{
		// At least two matrices available
		const TransposedMatrix & tInvMatrix0 = g_TransposedInvertedPortalMatrices[i + 0];
		const TransposedMatrix & tInvMatrix1 = g_TransposedInvertedPortalMatrices[i + 1];

		// We could use an array here to avoid the branch 0 | 1 when there is a clipping
		// But the compiler may not use registers as much.

		fltx4 f4NewPosition0 = MaddSIMD( f4PositionX, tInvMatrix0.m_ColumnX, tInvMatrix0.m_Translation );
		fltx4 f4NewPosition1 = MaddSIMD( f4PositionX, tInvMatrix1.m_ColumnX, tInvMatrix1.m_Translation );
		f4NewPosition0 = MaddSIMD( f4PositionY, tInvMatrix0.m_ColumnY, f4NewPosition0 );
		f4NewPosition1 = MaddSIMD( f4PositionY, tInvMatrix1.m_ColumnY, f4NewPosition1 );
		f4NewPosition0 = MaddSIMD( f4PositionZ, tInvMatrix0.m_ColumnZ, f4NewPosition0 );
		f4NewPosition1 = MaddSIMD( f4PositionZ, tInvMatrix1.m_ColumnZ, f4NewPosition1 );

		bool bIsInBox0 = IsPointInBox( f4NewPosition0 );
		bool bIsInBox1 = IsPointInBox( f4NewPosition1 );

		if (bIsInBox0 | bIsInBox1)
		{
			// With current code (due to no culling), in most cases there are no collisions
			// so do a single test first for both collisions.

			// I was thinking that we should only clip if the vertex is in only in one box and not both
			// (when portals are back to back). But this may create create more side effects than necessary.

			fltx4 f4ClippedPositionY;
			fltx4 f4ClippedPositionZ;
			int nIndex;

			if (bIsInBox0)
			{
				// Inside the exclusion box, let's put the poly in the exclusion box
				// Now do the inverse transformation. X has been projected to zero.

				f4ClippedPositionY = SplatYSIMD(f4NewPosition0);
				f4ClippedPositionZ = SplatZSIMD(f4NewPosition0);
				nIndex = 0;
			}
			else
			{
				f4ClippedPositionY = SplatYSIMD(f4NewPosition1);
				f4ClippedPositionZ = SplatZSIMD(f4NewPosition1);
				nIndex = 1;
			}

			const TransposedMatrix & tMatrix = g_TransposedPortalMatrices[i + nIndex];
			// Skip X as the result is zero.
			fltx4 f4ProjectedPosition = MaddSIMD(f4ClippedPositionY, tMatrix.m_ColumnY, tMatrix.m_Translation);
			f4ProjectedPosition = MaddSIMD(f4ClippedPositionZ, tMatrix.m_ColumnZ, f4ProjectedPosition);

			f4Position = f4ProjectedPosition;
		}

		i += 2;
	}

	// Do the last portal if there is one
	while (i < g_nNumberOfMatrices)
	{
		// At least two matrices available
		const TransposedMatrix & tInvMatrix0 = g_TransposedInvertedPortalMatrices[i + 0];

		fltx4 f4NewPosition0 = MaddSIMD( f4PositionX, tInvMatrix0.m_ColumnX, tInvMatrix0.m_Translation );
		f4NewPosition0 = MaddSIMD( f4PositionY, tInvMatrix0.m_ColumnY, f4NewPosition0 );
		f4NewPosition0 = MaddSIMD( f4PositionZ, tInvMatrix0.m_ColumnZ, f4NewPosition0 );

		bool bIsInBox0 = IsPointInBox( f4NewPosition0 );

		// I was thinking that we should only clip if the vertex is in only in one box and not both
		// (when portals are back to back). But this may create create more side effects than necessary.
		if (bIsInBox0)
		{
			// Inside the exclusion box, let's put the poly in the exclusion box
			// Now do the inverse transformation. X has been projected to zero.

			fltx4 f4ClippedPositionY0 = SplatYSIMD(f4NewPosition0);
			fltx4 f4ClippedPositionZ0 = SplatZSIMD(f4NewPosition0);

			const TransposedMatrix & tMatrix0 = g_TransposedPortalMatrices[i + 0];
			// Skip X as the result is zero.
			fltx4 f4ProjectedPosition0 = MaddSIMD(f4ClippedPositionY0, tMatrix0.m_ColumnY, tMatrix0.m_Translation);
			f4ProjectedPosition0 = MaddSIMD(f4ClippedPositionZ0, tMatrix0.m_ColumnZ, f4ProjectedPosition0);

			f4Position = f4ProjectedPosition0;
		}

		++i;
	}

	StoreUnaligned3SIMD( vPosition.Base(), f4Position );
}


void Paintblob_DrawFastSphere( CMeshBuilder &meshBuilder, const Vector &center, float radius )
{
	int offset = meshBuilder.GetCurrentVertex();

	Vector pos;
	for (int i = 0; i < 51; i++)
	{
		pos.x = g_FastSpherePosData[i][0] + center.x + g_FastSpherePosData[i][5] * radius;
		pos.y = g_FastSpherePosData[i][1] + center.y + g_FastSpherePosData[i][6] * radius;
		pos.z = g_FastSpherePosData[i][2] + center.z + g_FastSpherePosData[i][7] * radius;

		PortalBoxProjection( pos );

		meshBuilder.Position3fv( pos.Base() );
		meshBuilder.Normal3fv( &g_FastSpherePosData[i][5] );
		meshBuilder.TexCoord2fv( 0, &g_FastSpherePosData[i][3] );
		meshBuilder.Color3f( 1.f, 1.f, 1.f );
		meshBuilder.AdvanceVertex();
	}

	for (int i = 0; i < 84; i++)
	{
		meshBuilder.FastIndex( g_FastSphereTriData[i][0] + offset );
		meshBuilder.FastIndex( g_FastSphereTriData[i][1] + offset );
		meshBuilder.FastIndex( g_FastSphereTriData[i][2] + offset );
	}
}

struct PaintBlob_DrawSpheres_sortParticles_t
{
	int no;
	float dist;

	class CSorter
	{
	public:
		FORCEINLINE
			bool gt(PaintBlob_DrawSpheres_sortParticles_t a, PaintBlob_DrawSpheres_sortParticles_t b)
		{
			return a.dist < b.dist;
		}
	};
};

void Paintblob_DrawSpheres( const Point3D& vEyePosition, IMaterial *pMaterial, const BlobRenderInfo_t& blobRenderInfo , ImpParticle* pParticles, int nParticleCount, const matrix3x4_t* pPortalMatrices, int nPortalMatrixCount )
{
	SmartArray<PaintBlob_DrawSpheres_sortParticles_t> sort_particles;
	sort_particles.ensureCapacity( nParticleCount );
	sort_particles.size = nParticleCount;
	for(int i = 0; i < nParticleCount; i++)
	{
		sort_particles[i].no = i;
		sort_particles[i].dist = pParticles[i].center.length( vEyePosition );
	}
	PaintBlob_DrawSpheres_sortParticles_t::CSorter sorter;
	sort_particles.sort(sorter);

	CMatRenderContextPtr pRenderContext( materials );

	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->Bind( pMaterial );

	IMesh* pMesh = pRenderContext->GetDynamicMesh( true );

	int vertMax = MIN( 24000 / 51, 32768 / (84 * 3) );


	SetPortalMatrices( pPortalMatrices, nPortalMatrixCount );

	int j = 0;
	while ( j < nParticleCount )
	{
		int total = MIN( nParticleCount - j, vertMax );

		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, total * 51, total * 84 * 3 );

		int i = 0;
		while ( i < vertMax && j < nParticleCount )
		{
			ImpParticle* imp_particle = &( pParticles[ sort_particles[j].no ] );

			if (imp_particle->scale > 0.01)
			{
				Paintblob_DrawFastSphere( meshBuilder, imp_particle->center.AsVector(), blobRenderInfo.m_flCubeWidth * imp_particle->scale );
				i++;
			}
			j++;
		}

		meshBuilder.End();
		pMesh->Draw();
	}
}


IMaterial* GetDrawMaterial( void )
{
	static ConVarRef mat_wireframe("mat_wireframe");
	if ( r_paintblob_wireframe.GetBool() || mat_wireframe.GetBool() )
	{
		if( r_paintblob_material.GetInt() < 0 )
		{
			return materials->FindMaterial("shadertest/wireframe", TEXTURE_GROUP_OTHER);
		}
		else
		{
			return materials->FindMaterial("shadertest/wireframevertexcolornocull", TEXTURE_GROUP_OTHER);
		}
	}
	else if ( strlen( r_paintblob_shader.GetString() ) > 0 )
	{
		return materials->FindMaterial( r_paintblob_shader.GetString(), TEXTURE_GROUP_OTHER, true );
	}
	else
	{
		switch ( r_paintblob_material.GetInt() )
		{
		case 0:
			return materials->FindMaterial( "models/debug/debugwhite", TEXTURE_GROUP_OTHER, true );
		case 1:
			return materials->FindMaterial( "models/debug/debugwhite2", TEXTURE_GROUP_OTHER, true );
		case 2:
			return materials->FindMaterial( "models/debug/debugwhite3", TEXTURE_GROUP_OTHER, true );
		case 3:
			return materials->FindMaterial( "debug/debugvertexcolor", TEXTURE_GROUP_OTHER, true );
		case 4:
			return materials->FindMaterial( "debug/env_cubemap_model", TEXTURE_GROUP_OTHER, true );
		case 5:
			return materials->FindMaterial( "models/blob/env_cubemap_model_translucent_fountain", TEXTURE_GROUP_OTHER, true );
		case 6:
			return materials->FindMaterial( "models/debug/debugmesh", TEXTURE_GROUP_OTHER, true );
		case 7:
			return materials->FindMaterial( "models/debug/debugmesh_transparent", TEXTURE_GROUP_OTHER, true );
		case 8:
			return materials->FindMaterial( "models/ihvtest/tongue_bumped", TEXTURE_GROUP_OTHER, true );
		case 9:
			return materials->FindMaterial( "models/debug/debugbumps", TEXTURE_GROUP_OTHER, true );
		case 10:
			return materials->FindMaterial( "debug/env_cubemap_model_translucent_no_bumps", TEXTURE_GROUP_OTHER, true );
		case 11:
			return materials->FindMaterial( "models/shadertest/predator", TEXTURE_GROUP_OTHER, true );
		default:
			return NULL;
		}
	}
}


void SetUpImpRendererUserDefinedFuncs( void )
{
#if BLOB_USE_ABSTRACTION == 0
	//RENDERER_CLASS::setCalcSignFunc(calcSignSIMD); // NOTE: This is still slightly slower
	RENDERER_CLASS::setCalcSignFunc(calcSign);
	RENDERER_CLASS::setCalcSign2Func(calcSign2);
	if(r_paintblob_calc_uv_and_tan.GetBool())
	{
		RENDERER_CLASS::setCalcCornerFunc(CALC_CORNER_NORMAL_COLOR_UV_TAN_CI_SIZE, calcCornerNormalColorUVTan);
		RENDERER_CLASS::setCalcVertexFunc(calcVertexNormalNTexCoord4); // HACKHACK: This is blob specific!
		RENDERER_CLASS::setCalcCornerSIMDFunc(CALC_CORNER_NORMAL_COLOR_UV_TAN_CI_SIZE, calcCornerNormalColorUVTan_SIMD);
	}
	else if(r_paintblob_calc_tan_only.GetBool())
	{
		RENDERER_CLASS::setCalcCornerFunc(CALC_CORNER_NORMAL_COLOR_UV_TAN_CI_SIZE, calcCornerNormalColorTanNoUV);
		RENDERER_CLASS::setCalcVertexFunc(calcVertexNormalNColorTanNoUV);
	}
	else if	(r_paintblob_calc_color.GetBool())
	{
		RENDERER_CLASS::setCalcCornerFunc(CALC_CORNER_NORMAL_COLOR_CI_SIZE, calcCornerNormalColor);
		RENDERER_CLASS::setCalcVertexFunc(calcVertexNormalNColor);
	}
	else if (r_paintblob_calc_hifreq_color.GetBool())
	{
		RENDERER_CLASS::setCalcCornerFunc(CALC_CORNER_NORMAL_COLOR_CI_SIZE, calcCornerNormalHiFreqColor);
		RENDERER_CLASS::setCalcVertexFunc(calcVertexNormalNColor);
	}
	else if (r_paintblob_calc_tile_color.GetBool())
	{
		RENDERER_CLASS::setCalcCornerFunc(CALC_CORNER_NORMAL_CI_SIZE, calcCornerNormal);
		RENDERER_CLASS::setCalcVertexFunc(calcVertexNormalDebugColor);
	}
	else
	{
		RENDERER_CLASS::setCalcCornerFunc(CALC_CORNER_NORMAL_CI_SIZE, calcCornerNormal);
		RENDERER_CLASS::setCalcVertexFunc(calcVertexNormal);
	}
#endif
}

class CDrawInfo
{
public:
	CDrawInfo( ImpTiler * pTiler, float fAngle )
		:
		m_pTiler( pTiler ),
		m_fAngle( fAngle )
	{
		// Do nothing...
	}

	~CDrawInfo()
	{
		m_pRenderContext.SafeRelease();
	}

	ImpTiler * GetTiler() const
	{
		return m_pTiler;
	}

	float GetAngle() const
	{
		return m_fAngle;
	}

	CMatRenderContextPtr &  SetRenderContext( IMaterialSystem * pMaterialSystem )
	{
		m_pRenderContext.GetFrom( pMaterialSystem );
		return m_pRenderContext;
	}

	CMatRenderContextPtr & GetRenderContext()
	{
		return m_pRenderContext;
	}

private:
	// Start do not implement
	CDrawInfo(const CDrawInfo & other);
	CDrawInfo & operator=(const CDrawInfo & other);
	// End do not implement

	ImpTiler *	m_pTiler;
	float m_fAngle;
	CMatRenderContextPtr m_pRenderContext;
};

IMaterial * GetMaterial( IMaterial *pMaterial )
{
	IMaterial * pSpecialMaterial = GetDrawMaterial();
	if( pSpecialMaterial && !IsErrorMaterial( pSpecialMaterial ) )
	{
		pMaterial = pSpecialMaterial;
	}
	return pMaterial;
}

void SetupRenderContext( IMaterial *pMaterial, CDrawInfo & drawInfo )
{
	Assert(pMaterial != NULL);
	CMatRenderContextPtr & pRenderContext = drawInfo.SetRenderContext( materials );
	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->Bind( GetMaterial(pMaterial) );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();
	pRenderContext->Translate( 0.0f, 0.0f, 0.0f );
	pRenderContext->Rotate( drawInfo.GetAngle(), 0.0f, 0.0f, 1.0f );
}

void CleanupRenderContext( CDrawInfo & drawInfo )
{
	drawInfo.GetRenderContext()->PopMatrix();
}

Point3D CalculateCenter( ImpParticle *pParticles, int nNumberOfParticles )
{
	fltx4 center = Four_Zeros;

	float divider = (float)nNumberOfParticles;		// Access this much later to reduce potential LHS

	int nCount = nNumberOfParticles;
	ImpParticle * pCurrentParticles = pParticles;
	// SIMD this a little bit...
	while (nCount >= 16)
	{
		fltx4 c0 = AddSIMD(pCurrentParticles[0].center.sse_vec, pCurrentParticles[1].center.sse_vec);
		fltx4 c1 = AddSIMD(pCurrentParticles[2].center.sse_vec, pCurrentParticles[3].center.sse_vec);
		fltx4 c2 = AddSIMD(pCurrentParticles[4].center.sse_vec, pCurrentParticles[5].center.sse_vec);
		fltx4 c3 = AddSIMD(pCurrentParticles[6].center.sse_vec, pCurrentParticles[7].center.sse_vec);
		fltx4 c4 = AddSIMD(pCurrentParticles[8].center.sse_vec, pCurrentParticles[9].center.sse_vec);
		fltx4 c5 = AddSIMD(pCurrentParticles[10].center.sse_vec, pCurrentParticles[11].center.sse_vec);
		fltx4 c6 = AddSIMD(pCurrentParticles[12].center.sse_vec, pCurrentParticles[13].center.sse_vec);
		fltx4 c7 = AddSIMD(pCurrentParticles[14].center.sse_vec, pCurrentParticles[15].center.sse_vec);

		// Try to reduce pipeline stalls
		c0 = AddSIMD(c0, c4);
		c1 = AddSIMD(c1, c5);
		c2 = AddSIMD(c2, c6);
		c3 = AddSIMD(c3, c7);

		c0 = AddSIMD(c0, c1);
		c2 = AddSIMD(c2, c3);

		center = AddSIMD(center, c0);
		center = AddSIMD(center, c1);

		nCount -= 16;
		pCurrentParticles += 16;
	}

	while (nCount >= 4)
	{
		fltx4 c0 = AddSIMD(pCurrentParticles[0].center.sse_vec, pCurrentParticles[1].center.sse_vec);
		fltx4 c1 = AddSIMD(pCurrentParticles[2].center.sse_vec, pCurrentParticles[3].center.sse_vec);
		center = AddSIMD(center, c0);
		center = AddSIMD(center, c1);
		nCount -= 4;
		pCurrentParticles += 4;
	}

	while (nCount >= 1)
	{
		center = AddSIMD(center, pCurrentParticles[0].center.sse_vec);
		--nCount;
		++pCurrentParticles;
	}

	Point3D middle;
	fltx4 f4Count = ReplicateX4(divider);
	middle.sse_vec = DivEstSIMD(center, f4Count);		// Note that this does not have to be accurate, a rough value is enough
	return middle;
}



CDrawInfo * Paintblob_BeginDraw( IMaterial *pMaterial, const BlobRenderInfo_t& blobRenderInfo, ImpParticle *pParticles, int nNumberOfParticles, const matrix3x4_t * pMatrices, int nNumberOfMatrices, bool bUseTwoPassesToDraw )
{
	VPROF_BUDGET( "Paintblob_DrawIsoSurface", "Paintblob_render" );

	float flPaintResolutionHigh = r_paintblob_highres_cube.GetFloat();
	const float flMaxPaintScale = 2.5f;
	float flPaintResolutionLow = flPaintResolutionHigh * flMaxPaintScale;
	float flPaintResolution;

	// assuming MAIN_VIEW is 0, then we do high res blobs
	if ( blobRenderInfo.m_nViewID == 0 )
	{
		flPaintResolution = r_paintblob_mainview_highres.GetBool() ? flPaintResolutionHigh : flPaintResolutionLow;
	}
	else
	{
		if ( r_paintblob_only_mainview_displayed.GetBool() )
		{
			return NULL;
		}
		flPaintResolution = r_paintblob_otherviews_highres.GetBool() ? flPaintResolutionHigh : flPaintResolutionLow;
	}

	// apply view scale
	flPaintResolution *= blobRenderInfo.m_flViewScale;

	// TODO: These probably shouldn't be static variables (not thread safe)
#if BLOB_USE_ABSTRACTION == 0
	RENDERER_CLASS::SetCubeWidth( blobRenderInfo.m_flCubeWidth * r_paintblob_blr_scale.GetFloat() * flPaintResolution );
	RENDERER_CLASS::SetRenderR( blobRenderInfo.m_flCubeWidth * r_paintblob_blr_scale.GetFloat() * blobRenderInfo.m_flRenderRadius );
	RENDERER_CLASS::SetCutoffR( blobRenderInfo.m_flCubeWidth * r_paintblob_blr_scale.GetFloat() * blobRenderInfo.m_flCutoffRadius );
	SetUpImpRendererUserDefinedFuncs();
#endif

	// Insert the particles into the tiler, and fill the blob. The rendering will be done in PaintBlob_WaitEndOfDraw
	// Note that we removed the implementation using the renderer directly (in order to make the code simpler to maintain).

	ImpTiler * pTiler = ImpTilerFactory::GetInstance()->AcquireTiler();

	pTiler->SetCubeWidth( blobRenderInfo.m_flCubeWidth * r_paintblob_blr_scale.GetFloat() * flPaintResolution );
	pTiler->SetRenderRadius( blobRenderInfo.m_flCubeWidth * r_paintblob_blr_scale.GetFloat() * blobRenderInfo.m_flRenderRadius );
	pTiler->SetCutoffRadius( blobRenderInfo.m_flCubeWidth * r_paintblob_blr_scale.GetFloat() * blobRenderInfo.m_flCutoffRadius );

	pTiler->setTileIndexToDraw(r_paintblob_tile_index_to_draw.GetInt());

	CDrawInfo * pDrawInfo = new CDrawInfo( pTiler, 0.0f );
	if (bUseTwoPassesToDraw == false)
	{
		SetupRenderContext( pMaterial, *pDrawInfo );
		pTiler->SetRenderContext( &pDrawInfo->GetRenderContext() );
	}

	// Now that we don't transform the particles anymore (so they are centered around 0.0f, 0.0f, 0.0f),
	// We need to find a proper center. This will reduce the number of tiles and thus optimize the rendering,
	// and reduce the vertex buffer usage. (less jobs spawn, less buffer used, better crawling within a tile, etc...).
	Point3D center = CalculateCenter( pParticles, nNumberOfParticles );

	pTiler->beginFrame( center, !(r_paintblob_debug_draw_margin.GetBool()), bUseTwoPassesToDraw );
	pTiler->SetPortalMatrices( pMatrices, nNumberOfMatrices );

	for (int i = 0; i < nNumberOfParticles; i++)
	{
		pTiler->insertParticle( &pParticles[i] );
	}

	if(r_paintblob_rotate.GetBool())
	{
		pTiler->drawSurfaceSorted( center, bUseTwoPassesToDraw );
	}
	else
	{
		pTiler->drawSurface( bUseTwoPassesToDraw );
	}

	// This is code used for visualizing the boundaries of every tile that's drawn
	// and maybe in the future, slices within the tiles.
	if( r_paintblob_debug_draw_tile_boundaries.GetBool() == true )
	{
		for( int i = 0; i < pTiler->getNoTiles(); i++ )
		{
			Vector mins = pTiler->getRenderDim().AsVector() * -0.5f;
			Vector maxs = pTiler->getRenderDim().AsVector() * 0.5f;

			// FIXME: How do we render debug info in this file?
			//debugoverlay->AddBoxOverlay(pTiler->getTileOffset(i).AsVector(), mins, maxs, QAngle( 0, 0, 0 ), 0, 255, 0, 0, 0 );
		}
	}

	return pDrawInfo;
}

void PaintBlob_WaitEndOfDraw( IMaterial *pMaterial, CDrawInfo * pDrawInfo, bool bUseTwoPassesToDraw )
{
	ImpTiler * pTiler = pDrawInfo->GetTiler();
	if (bUseTwoPassesToDraw)
	{
		SetupRenderContext( pMaterial, *pDrawInfo );
		pTiler->SetRenderContext( &pDrawInfo->GetRenderContext() );
	}

	pTiler->endFrame(bUseTwoPassesToDraw);
	ImpTilerFactory::GetInstance()->ReleaseTiler( pTiler );

	CleanupRenderContext(*pDrawInfo);

	// We took ownership of pDrawInfo. We don't need it anymore. Time to release it.
	delete pDrawInfo;
}


void Paintblob_DrawIsoSurface_Synchronous( IMaterial *pMaterial, const BlobRenderInfo_t& blobRenderInfo, BlobParticlesEnvelope_t &particles, int blobCount, BlobMatricesEnvelope_t & matrices, int nMatrixCount, bool bUseTwoPassesToDraw)
{
	CDrawInfo * pDrawInfo = Paintblob_BeginDraw( pMaterial, blobRenderInfo, particles, blobCount, matrices, nMatrixCount, bUseTwoPassesToDraw );
	if ( pDrawInfo != NULL )
	{
		PaintBlob_WaitEndOfDraw( pMaterial, pDrawInfo, bUseTwoPassesToDraw );
	}
}


void RenderBlob( bool bUseBlobulator, IMatRenderContext* pRenderContext, IMaterial* pMaterial, const BlobRenderInfo_t& blobRenderInfo, const matrix3x4_t* pPortalMatrices, int nPortalMatrixCount, ImpParticle* pParticles, int nParticleCount )
{
	if ( !bUseBlobulator || !r_paintblob_draw_isosurface.GetBool() )
	{
		Vector vEyePosition;
		pRenderContext->GetWorldSpaceCameraPosition( &vEyePosition );
		Paintblob_DrawSpheres( vEyePosition, pMaterial, blobRenderInfo, pParticles, nParticleCount, pPortalMatrices, nPortalMatrixCount );
	}
	else
	{
		bool bUseTwoPassesToDraw = UseTwoPassesToDraw();

		const int NUMBER_OF_PARTICLES_FOR_SINGLE_PASS = 16;
		if ( nParticleCount <= NUMBER_OF_PARTICLES_FOR_SINGLE_PASS )
		{
			// If there is a small number of particles there is no reason to use two passes.
			// (It will use a lot of fragments for no reasons, i.e. we won't necessarily have perf speed up related to two passes).
			bUseTwoPassesToDraw = false;
		}
		else
		{
			// Make sure that we have enough fragment buffer though (we would not want to lock the game because of that).
			const int mTotalNumberOfFragments = BLOB_NUMBER_OF_FRAGMENTS;
			const int mNumberOfRendererInUse = ImpRendererFactory::GetInstance()->GetNumberOfAcquiredTileRenderers();
			const int mNumberOfJobsPerRenderer = BLOB_NUM_JOBS;
			if ( (mNumberOfRendererInUse * mNumberOfJobsPerRenderer) > mTotalNumberOfFragments )
			{
				// We have too many jobs in flight already.
				// We do not want to overload the fragment buffers. This job will use a single pass (on the mat queue thread).
				// A blob will usually have 1 to 3 tiles (some will have much more though but will be very quick to display).
				// Each tile can have 4 to 7 jobs (for the PS3 for example).
				// So a complex blob will use up to 21 jobs. Assuming that we have 4 fragments per job, that's a usage of 84 fragments per blob.
				// It means that we can have 2 complex blobulators before running on the slow pass.
				bUseTwoPassesToDraw = false;
				DevMsg("[Blobulator] Switching to one pass as fragment vertex buffer is already half full.\n");
			}
		}

		bool bPassedFlag = bUseTwoPassesToDraw;
		if (r_paintblob_force_single_pass.GetBool())
		{
			// For debug purpose, we can use a different bPassedFlag (when looping several jobs in Paintblob_DrawIsoSurface_Synchronous)
			bUseTwoPassesToDraw = bPassedFlag = false;
		}

		ICallQueue *pQueue = pRenderContext->GetCallQueue();
		if (bUseTwoPassesToDraw)
		{
			// When we are in two steps mode, first we setup the various jobs and launch them in this thread.
			// We set NULL for pMaterial to make sure that we don't use them in that pass.
			CDrawInfo * pDrawInfo = Paintblob_BeginDraw( NULL, blobRenderInfo, pParticles, nParticleCount, pPortalMatrices, nPortalMatrixCount, bPassedFlag );
			if (pDrawInfo != NULL)
			{
				// Then we are going to wait for the result later (if possible on the mat queue thread. That way we don't have to wait for the result).

				// Ownership of pDrawInfo will be released in PaintBlob_WaitEndOfDraw().
				if ( r_threaded_blobulator.GetBool() && (pQueue != NULL) )
				{
					pQueue->QueueCall( PaintBlob_WaitEndOfDraw, pMaterial, pDrawInfo, bPassedFlag );
				}
				else
				{
					PaintBlob_WaitEndOfDraw( pMaterial, pDrawInfo, bPassedFlag );
				}
			}
		}
		else
		{
			// Mode one step, either we do everything on this thread, or everything on the other thread...
			// Can happen for debug purpose , or if we are worried that the vertex buffer is going to be filled too soon.
			BlobParticlesEnvelope_t envParticles( pParticles, nParticleCount );
			BlobMatricesEnvelope_t envPortalMatrices( pPortalMatrices, nPortalMatrixCount );

			if ( r_threaded_blobulator.GetBool() && (pQueue != NULL) )
			{
				pQueue->QueueCall( Paintblob_DrawIsoSurface_Synchronous, pMaterial, blobRenderInfo, envParticles, nParticleCount, envPortalMatrices, nPortalMatrixCount, bPassedFlag);
			}
			else
			{
				Paintblob_DrawIsoSurface_Synchronous( pMaterial, blobRenderInfo, envParticles, nParticleCount, envPortalMatrices, nPortalMatrixCount, bPassedFlag);
			}
		}
	}
}

void FrameUpdate( int renderFrame )
{
	// Perform per-frame management work
	ImpRendererFactory::GetInstance()->FrameUpdate( renderFrame );
}

void ShutdownBlob()
{
	ImpTilerFactory::GetInstance()->ClearImpTilers();
}

} // namespace Blobulator
