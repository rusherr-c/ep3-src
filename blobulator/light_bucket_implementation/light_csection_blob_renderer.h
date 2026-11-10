//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CSECTION_BLOB_RENDERER_H
#define BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CSECTION_BLOB_RENDERER_H

#include "blobulator/iblob_renderer.h"
#include "blobulator/light_bucket_implementation/light_csection.h"
#include "blobulator/light_bucket_implementation/light_csection_pool.h"
#include "blobulator/light_bucket_implementation/light_cjob_cache.h"

#include "blobulator/smartarray.h"
#include "blobulator/point3d.h"
#include "blobulator/vertexbuffers/vertexbuffer.h"
#include "blobulator/implicit/impdefines.h"
#include "blobulator/implicit/impparticle.h"

#ifdef SPU
#error	This source should not be included on SPU
#endif

class IThreadPool;

namespace Blobulator
{
	namespace LightBucketImplementation
	{
		class CBucket;
		class CThreadData;

		// This renderer does not do any job, it is able to do some constants calculation,
		// and initialize / tear down the vertex buffer (but not draw directly, nor reserve a slot).
		class ALIGN16 LightweightRenderer : public IBlobRenderer
		{
		public:
			LightweightRenderer(CFragmentedVertexBuffer & fragmentedVertexBuffer, bool bDispatchToVertexBuffer);
			virtual ~LightweightRenderer();

			virtual void BeginFrame(bool bTileMode, void * pRenderContext);
			virtual void EndFrame();

			virtual void BeginTile(const Point3D & offset);
			virtual void DrawTile();
			virtual void EndTile();

			virtual void AddParticle(ImpParticle & particle, bool bSkipBoundsCheck);

			virtual void SetCubeWidth(float fCubeWidth);
			virtual float GetCubeWidth() const;
			virtual void SetRenderRadius(float fRenderRadius);
			virtual float GetRenderRadius() const;
			virtual void SetCutoffRadius(float fCutoffRadius);
			virtual float GetCutoffRadius() const;

			virtual const Point3D & GetInnerDimensions() const;
			virtual float GetMarginWidth() const;

			virtual void SetPortalMatrices(const matrix3x4_t * pMatrices, int nNumberOfMatrices);

			virtual void SetDebugMode(bool bDebugMode);
			virtual bool GetDebugMode() const;
			virtual int GetNumberOfDebugInfo() const;
			virtual void DrawDebugInfo(IVDebugOverlay & debugOverlay, int nDebugInfoIndex);

			virtual int GetDebugId() const;

		protected:
			void RecalculateConstants() const;

			mutable fltx4 m_f4WOnly;
			mutable fltx4 m_f4Threshold;
			mutable fltx4 m_f4Fourpt25s;
			mutable fltx4 m_f4scalerSq;
			mutable fltx4 m_f4cutoffRSq;

			Point3D m_offset;
			mutable Point3D m_OuterDimensions;
			mutable Point3D m_InnerDimensions;

			bool m_bTileMode;
			bool m_bDebugMode;
			bool m_bDispatchToVertexBuffer;
			mutable bool m_bConstantsCalculated;

			float m_fCutoffRadius;
			float m_fCubeWidth;
			mutable float m_RenderRadius;

			mutable float m_fCutoffRadiusSq;
			mutable float m_fOneOverCubeWidth;

			mutable float m_fScaler;
			mutable float m_fScalerSq;

			mutable float m_fOneOverThreshold;
			mutable int m_nMarginNCubes;
			mutable float m_fMarginWidth;

			mutable int m_nFieldCalcSteps;
			mutable float m_fThreshold;

			// We do not want to have one fragmented vertex buffer per renderer, we want to re-use the fragment as much as possible.
			// Which means that the buffer has to be pretty large for all the possible sizes for the blob.
			CFragmentedVertexBuffer & m_FragmentedVertexBuffer;

		private:
			// Start do not implement
			LightweightRenderer(const LightweightRenderer & other);
			LightweightRenderer & operator=(const LightweightRenderer & other);
			// End do not implement
		} ALIGN16_POST;

		// This renderer can spawn some jobs, do the real calculation, and fill the vertex buffer (as such it needs a slot).
		class ALIGN16 CSectionBlobRenderer : public LightweightRenderer
		{
		public:
			CSectionBlobRenderer( CFragmentedVertexBuffer & fragmentedVertexBuffer, CSectionPool & sectionPool );
			virtual ~CSectionBlobRenderer();

			virtual void BeginFrame(bool bTileMode, void * pRenderContext);
			virtual void EndFrame();

			virtual void BeginTile(const Point3D & offset);
			virtual void DrawTile();
			virtual void EndTile();

			virtual void SetPortalMatrices(const matrix3x4_t * pMatrices, int nNumberOfMatrices);
			virtual void AddParticle(ImpParticle & particle, bool bSkipBoundsCheck);

		private:
			// Start do not implement
			CSectionBlobRenderer(const CSectionBlobRenderer & other);
			CSectionBlobRenderer & operator=(const CSectionBlobRenderer & other);
			// End do not implement

			int m_nSlotIndexForFragmentVB;

			// Investigate if these next 3 fields are still needed

			int m_nMaxNoSlicesToDraw;
			int m_nLastSliceDrawn;
			bool m_bPolygonizationEnabled;

			// Some of these variables are mutable as they are cached values that gets lazy updated.
			// Some const accessors may force the refresh of these variables.

			// The outer bounding box bounds the entire volume of the cache
			Point3D m_OuterBBMins;
			Point3D m_OuterBBMaxs;

			// The inner bounding box bounds the entire volume of the cache
			// with the exception of the margins.
			Point3D m_InnerBBMins;
			Point3D m_InnerBBMaxs;

			// 256 particles max supported by the code.
			// The max-size is hard-coded making the DMA access simpler on the SPU.
			// (The SPU has to reserve up to 256 particles anyway).
			// Instead of storing an array of particle pointers, we are going to copy the particle itself.
			// This should also reduce some of the cache misses
			// (now if only we could fit the particle in 16 bytes instead of 20 bytes extended to 32 :( ).
			ImpParticle	ALIGN16 m_Particles[MAX_NUMBER_OF_PARTICLES] ALIGN16_POST;
			int m_nNumberOfParticles;

			int16 ALIGN16 m_nSectionIndices[NUMBER_OF_SECTIONS_IN_X * NUMBER_OF_SECTIONS_IN_Y * NUMBER_OF_SECTIONS_IN_Z] ALIGN16_POST;

			// These sizes are how many cubes ImpRenderer can draw
			// in any direction. The dimensions of the cache actually
			// have to be larger by one in each direction in order to
			// fit the samples on the far side of the last cube. These
			// dimensions are stored as xsizeP1, ysizeP1 and zsizeP1.

			FORCEINLINE
			CSection * GetSection(int globalX, int globalY, int globalZ)
			{
				int nArrayIndex = GetSectionArrayIndex(globalX, globalY, globalZ);
				int nSectionIndex = m_nSectionIndices[nArrayIndex];
				CSection * pSection = m_CacheSections[nSectionIndex];
				if (nSectionIndex != 0)
				{
					Assert(pSection->m_nX == (globalX & ~SECTION_X_MASK));
					Assert(pSection->m_nY == (globalY & ~SECTION_Y_MASK));
					Assert(pSection->m_nZ == (globalZ & ~SECTION_Z_MASK));
				}
				return pSection;
			}

			FORCEINLINE
			CSection * GetSection(XYZ globalXYZ)
			{
				int globalX = XYZ_GetX(globalXYZ);
				int globalY = XYZ_GetY(globalXYZ);
				int globalZ = XYZ_GetZ(globalXYZ);
				return GetSection(globalX, globalY, globalZ);
			}

			XYZ m_nStack[1024];		// With the concept of section, the need for this stack should be much smaller 
			int m_nStackSize;

			SmartArray<CSection *> m_CacheSections;
			int m_nCurrentCacheSection;
			CSectionPool &m_SectionPool;

			CThreadData * m_pThreadData;
			int m_nNumberOfJobsToSpawn;

#if (BLOB_FILL_MODE == BLOB_FILL_MODE_ASYNCHRONOUS)
			class CBlobJob * * m_ppJobs;
#elif (BLOB_FILL_MODE == BLOB_FILL_MODE_SYNCHRONOUS)
			// No thread information to store in this case...
#else
			class BlobulatorVJobInstance * m_pJobInstances;
#endif

			Point3D m_mins;

			// 512 should be enough in many cases. Keep the number a power of 2.
			// For a big blob, we usually over around 7000 buckets or more.
			// Out of these, 2000 to 3000 buckets will be scanned (close to the surface).
			// 512 corresponds to 1/6th of a big blob, thus it is enough for many slices.
			// 512 corners mean that we are going to allocate 128 Kb of memory for the corners.
			// On the SPU, we may use one eight of this size.
			// Each corner group here is actually 8 CornerInfos put together.
			static const int NUM_CORNERS_GROUP_ALLOCATED = 64;
			static const int NUM_CORNERINFOS_PER_CORNER_GROUP = 8;

			static CSection s_ZeroSection;

			// TODO: Optimize the drawTable?
			// This table is a a memory optimized version of "const char drawTable[256][17]"
			// Each index is stored as a nibble (4 bits). The reason why this table exist:
			// - It reduces the memory cost from 4352 bytes to 2048 bytes.
			//		It is especially useful for SPUs (although we may store it as one vector per line still).
			// - We can read the whole line in one memory read (64 bits) and then get the result with simple shift and mask.
			//		This is going to reduce the average number of memory accesses by 8.
			// uint64 m_nTableTri[256];

			CJobCache m_JobCaches[BLOB_NUM_JOBS];

			// It seems that the global thread pool is a bit unstable in term of frame rate, or how the jobs are distributed.
			// I.e. we could have the 4 inter-dependents job executed on one thread, instead of spread over all the threads.
			// So create its own thread pool.
#if BLOB_CREATE_OWN_THREADPOOL
			static IThreadPool * s_pThreadPool;
			static int s_nCounterInstances;
#endif

			typedef CLockLessStack<TOTAL_NUMBER_OF_SECTIONS> SectionStack;

			// These will contain the sections that we are going to execute and parse for each job.
			SectionStack m_SectionStacks[BLOB_NUM_JOBS];
			// This contain the sections that we are going to execute and parse once each job is done.
			SectionStack m_GlobalSectionStack;

			SectionStack::Container * m_pSectionStackContainers[BLOB_NUM_JOBS];
			SectionStack::Container * m_pGlobalSectionStackContainer;

			const matrix3x4_t * m_pPortalMatrices;
			int m_nNumberOfMatrices;

			void RecalculateBoundingBoxes();

			bool IsParticleWithinBounds(ImpParticle & particle) const;

			// This may not be necessary anymore... See DrawDebugInfo...
			void SetMaxNoSlicesToDraw(int no = -1) { m_nMaxNoSlicesToDraw = no; }
			int GetMaxNoSlicesToDraw() const { return m_nMaxNoSlicesToDraw; }
			int GetLastSliceDrawn() const { return m_nLastSliceDrawn; }

			// SIMD version
			template <int STEP>
			bool SeedX(int nStartX, int nStartY, int nStartZ, int nMaxIterations, fltx4 f4Position, fltx4 f4Step);
			template <int STEP>
			bool SeedY(int nStartX, int nStartY, int nStartZ, int nMaxIterations, fltx4 f4Position, fltx4 f4Step);
			template <int STEP>
			bool SeedZ(int nStartX, int nStartY, int nStartZ, int nMaxIterations, fltx4 f4Position, fltx4 f4Step);

			// Non-SIMD version
			template <int STEP>
			bool SeedX(int nStartX, int nStartY, int nStartZ, int nMaxIterations, float fX, const float fY, const float fZ);
			template <int STEP>
			bool SeedY(int nStartX, int nStartY, int nStartZ, int nMaxIterations, const float fX, float fY, const float fZ);
			template <int STEP>
			bool SeedZ(int nStartX, int nStartY, int nStartZ, int nMaxIterations, const float fX, const float fY, float fZ);

			DOINLINE bool DOFASTCALL SeedSurface(Point3D & center, bool bLargerSeeding);
			void SeedSurface();
			void StartRenderSlices();
			void EndRenderSlices();
			DOINLINE void DOFASTCALL RenderSlice(CSection & section);
			bool DOFASTCALL CalcSign(CSection & section, const fltx4 position);
			bool DOFASTCALL CalcSign(CSection & section, const fltx4 position, const int x, const int y, const int z);
			bool DOFASTCALL CalcSign(CSection & section, const float x, const float y, const float z);

			void CheckBucketAllocations(const int localCurCacheBucket);
			void FillSections(int nParticleIndex, int nMinX, int nMaxX, int nMinY, int nMaxY, int nMinZ, int nMaxZ);
			void BuildCache();

			void Calculate(CBucket * bucket);
			void CalcVertex(const float x, const float y, const float z, const int edge, const CCornerInfo * pCorner1, const CCornerInfo * pCorner2);

			// Use a function to skip rendering (so it is the same behavior between StartRenderPass() and EndRenderPass().
			bool ShouldRender() const;

			void StartRenderPass();
			void EndRenderPass();

			void SortSections();

			// Revisit this friend
			friend class CThreadData;
		} ALIGN16_POST;
	}
}

#endif

