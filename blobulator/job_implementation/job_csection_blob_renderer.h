//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_JOBIMPLEMENTATION_CSECTION_BLOB_RENDERER_H
#define BLOBULATOR_JOBIMPLEMENTATION_CSECTION_BLOB_RENDERER_H

#include "blobulator/iblob_renderer.h"
#include "blobulator/job_implementation/job_csection.h"
#include "blobulator/job_implementation/job_cjob_cache.h"
#include "blobulator/job_implementation/job_cfragmented_vertex_buffer.h"

#include "blobulator/smartarray.h"
#include "job_cbucket.h"
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
	namespace JobImplementation
	{
		class CBucket;
		class CThreadData;

		class CSectionBlobRenderer : public IBlobRenderer
		{
		public:
			CSectionBlobRenderer();
			virtual ~CSectionBlobRenderer();

			virtual void BeginFrame(bool bTileMode, void * pRenderContext);
			virtual void EndFrame();

			virtual void BeginTile(const Point3D & offset);
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

			virtual void SetDebugMode(bool bDebugMode);
			virtual bool GetDebugMode() const;
			virtual int GetNumberOfDebugInfo() const;
			virtual void DrawDebugInfo(IVDebugOverlay & debugOverlay, int nDebugInfoIndex);

		private:
			// Start do not implement
			CSectionBlobRenderer(const CSectionBlobRenderer & other);
			CSectionBlobRenderer & operator=(const CSectionBlobRenderer & other);
			// End do not implement

			bool m_bTileMode;
			bool m_bDebugMode;

			// Investigate if these fields are still needed

			int m_nMaxNoSlicesToDraw;
			int m_nLastSliceDrawn;
			bool m_bPolygonizationEnabled;

			Point3D m_offset;

			float m_fCutoffRadius;
			float m_fCubeWidth;
			float m_RenderRadius;

			float m_fCutoffRadiusSq;
			float m_fOneOverCubeWidth;

			float m_fScaler;
			float m_fScalerSq;

			float m_fOneOverThreshold;

			int m_nMarginNCubes;
			float m_fMarginWidth;

			int m_nFieldCalcSteps;
			float m_fThreshold;

			Point3D m_OuterDimensions;
			Point3D m_InnerDimensions;

			// The outer bounding box bounds the entire volume of the cache
			Point3D m_OuterBBMins;
			Point3D m_OuterBBMaxs;

			// The inner bounding box bounds the entire volume of the cache
			// with the exception of the margins.
			Point3D m_InnerBBMins;
			Point3D m_InnerBBMaxs;

			fltx4 g_f4WOnly;
			fltx4 g_f4Threshold;
			fltx4 g_f4Fourpt25s;
			fltx4 g_f4scalerSq;
			fltx4 g_f4One;
			fltx4 g_f4cutoffRSq;

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
				CSection * pSection = cacheSections[nSectionIndex];
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

			CSection * GetOrAllocateSection(int globalX, int globalY, int globalZ);

			int GetBucketIndexFromXYZ(int globalX, int globalY, int globalZ)
			{
				CSection * section = GetSection(globalX, globalY, globalZ);
				int bucketX = globalX / BUCKET_FACTOR;
				int bucketY = globalY / BUCKET_FACTOR;
				int bucketZ = globalZ / BUCKET_FACTOR;
				bucketX = bucketX & CSection::BUCKETS_PER_SECTION_X_MASK;
				bucketY = bucketY & CSection::BUCKETS_PER_SECTION_Y_MASK;
				bucketZ = bucketZ & CSection::BUCKETS_PER_SECTION_Z_MASK;
				int bucketIndex = section->GetInternalBucket(bucketX, bucketY, bucketZ);

#if _DEBUG
				if (bucketIndex != 0)
				{
					CBucket * foundBucket = cacheBuckets[bucketIndex];
					Assert((globalX & BUCKET_FACTOR_MASK) == foundBucket->m_nX);
					Assert((globalY & BUCKET_FACTOR_MASK) == foundBucket->m_nY);
					Assert((globalZ & BUCKET_FACTOR_MASK) == foundBucket->m_nZ);
				}
#endif

				return (bucketIndex);
			}

			XYZ m_nStack[1024];		// With the concept of section, the need for this stack should be much smaller 
			int m_nStackSize;

			// One dimensional array, so we can optimize access to each dimension in tight loops
			//CBucket * bucketsByXYZ[BUCKET_X * BUCKET_Y * BUCKET_Z];

			// Pool of available Buckets that can be recycled as needed.
			// Optimize this later by using a single pointer (instead of having to access 2 variables).
			SmartArray<CBucket*> cacheBuckets;
			int currentCacheBucket;

			SmartArray<CCornerInfo *> cacheCornerInfos;
			int currentCacheCornerInfo;

			SmartArray<CSection *> cacheSections;
			int currentCacheSection;

			Point3D m_mins;

			static const int BUCKETS_ALLOCATED_AHEAD = 32;
			static const int BUCKETS_PREFETCH_AHEAD = BUCKETS_ALLOCATED_AHEAD / 4;

			// 512 should be enough in many cases. Keep the number a power of 2.
			// For a big blob, we usually over around 7000 buckets or more.
			// Out of these, 2000 to 3000 buckets will be scanned (close to the surface).
			// 512 corresponds to 1/6th of a big blob, thus it is enough for many slices.
			// 512 corners mean that we are going to allocate 128 Kb of memory for the corners.
			// On the SPU, we may use one eight of this size.
			// Each corner group here is actually 8 CornerInfos put together.
			static const int NUM_CORNERS_GROUP_ALLOCATED = 64;
			static const int NUM_CORNERINFOS_PER_CORNER_GROUP = 8;

			// Defines this bucket so no NULL test is needed for the bucket.
			// This makes the code more streamlined.
			static CBucket s_ZeroBucket;
			static CCornerInfo s_ZeroCorners[8];
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

			CFragmentedVertexBuffer m_FragmentedVertexBuffer;
			// It seems that the global thread pool is a bit unstable in term of frame rate, or how the jobs are distributed.
			// I.e. we could have the 4 inter-dependents job executed on one thread, instead of spread over all the threads.
			// So create its own thread pool.
#if BLOB_CREATE_OWN_THREADPOOL
			IThreadPool * m_pThreadPool;
#endif

			typedef CLockLessStack<TOTAL_NUMBER_OF_SECTIONS> SectionStack;

			// These will contain the sections that we are going to execute and parse for each job.
			SectionStack m_SectionStacks[BLOB_NUM_JOBS];
			// This contain the sections that we are going to execute and parse once each job is done.
			SectionStack m_GlobalSectionStack;

			SectionStack::Container * m_pSectionStackContainers[BLOB_NUM_JOBS];
			SectionStack::Container * m_pGlobalSectionStackContainer;

			void RecalculateConstants();
			void RecalculateBoundingBoxes();

			bool IsParticleWithinBounds(ImpParticle & particle) const;

			// This may not be necessary anymore... See DrawDebugInfo...
			void SetMaxNoSlicesToDraw(int no = -1) { m_nMaxNoSlicesToDraw = no; }
			int GetMaxNoSlicesToDraw() const { return m_nMaxNoSlicesToDraw; }
			int GetLastSliceDrawn() const { return m_nLastSliceDrawn; }

			DOINLINE void DOFASTCALL SeedSurface(Point3D & center);
			void RenderSlices();
			DOINLINE void DOFASTCALL RenderSlice(CSection & section);
			DOINLINE void DOFASTCALL OptimizedCalcVertex(fltx4 xyz, fltx4 edgeMask, const CCornerInfo * corner0, const CCornerInfo * corner1);
			DOINLINE void DOFASTCALL OptimizedCalcVertex2(fltx4 xyz, fltx4 edgeMask, const CCornerInfo * corner0, const CCornerInfo * corner1, float * positionBuffer);
			bool DOFASTCALL CalcSign(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z);

			void CheckBucketAllocations(const int localCurCacheBucket);
			void FillBuckets(int nParticleIndex, int nMinX, int nMaxX, int nMinY, int nMaxY, int nMinZ, int nMaxZ);
			void FillSections(int nMinX, int nMaxX, int nMinY, int nMaxY, int nMinZ, int nMaxZ);
			void BuildCache();
			void CheckInternals();

			void Calculate(CBucket * bucket);
			void CalcVertex(const float x, const float y, const float z, const int edge, const CCornerInfo * pCorner1, const CCornerInfo * pCorner2);
			void RenderPass(CThreadData * pThreadData, int nNumberOfJobsToSpawn);

			void SortSections();
			void CheckBucketsInSection(CSection & section);
			DOINLINE void DOFASTCALL OptimizedCalcVertex(fltx4 xyz, fltx4 edgeMask, const CCornerInfo * corner0, const CCornerInfo * corner1, VERTEX_BUFFER_CLASS* vertexBuffer);

			// Revisit this friend
			friend class CThreadData;
		};
	}
}

#endif

