//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_BUCKETIMPLEMENTATION_CBUCKET_BLOB_RENDERER_H
#define BLOBULATOR_BUCKETIMPLEMENTATION_CBUCKET_BLOB_RENDERER_H

#include "blobulator/iblob_renderer.h"
#include "blobulator/smartarray.h"
#include "cbucket.h"
#include "blobulator/point3d.h"
#include "blobulator/vertexbuffers/vertexbuffer.h"

namespace Blobulator
{
	namespace BucketImplementation
	{
		class CBucket;

		class CBucketBlobRenderer : public IBlobRenderer
		{
		public:
			CBucketBlobRenderer();
			virtual ~CBucketBlobRenderer();

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
			CBucketBlobRenderer(const CBucketBlobRenderer & other);
			CBucketBlobRenderer & operator=(const CBucketBlobRenderer & other);
			// End do not implement

			struct XYZ
			{
				cube_index_t x, y, z;
			};

			typedef uint16 YZ;

			FORCEINLINE
			int YZ_GetY(YZ temp)
			{
				return (temp & 0xff);
			}

			FORCEINLINE
			int YZ_GetZ(YZ temp)
			{
				return ((temp >> 8) & 0xff);
			}

			FORCEINLINE
			YZ YZ_SetYZ(int y, int z)
			{
				return (y | (z << 8));
			}

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

			SmartArray<ImpParticle*> particles;

			// These sizes are how many cubes ImpRenderer can draw
			// in any direction. The dimensions of the cache actually
			// have to be larger by one in each direction in order to
			// fit the samples on the far side of the last cube. These
			// dimensions are stored as xsizeP1, ysizeP1 and zsizeP1.

			static const int X_SIZE = RENDERER_CACHE_XSIZE;
			static const int Y_SIZE = RENDERER_CACHE_YSIZE;
			static const int Z_SIZE = RENDERER_CACHE_ZSIZE;
			static const int X_SIZE_P1 = X_SIZE + 1;
			static const int X_SIZE_P2 = X_SIZE + 2;
			static const int Y_SIZE_P1 = Y_SIZE + 1;
			static const int Z_SIZE_P1 = Z_SIZE + 1;

			struct Slice_t
			{
				SmartArray<YZ, false, 0> todo_list;
				SmartArray<YZ> seed_list;
#if (SEED_MULTISLICE == 1 || SEED_SIDEWAYS == 1)
				SmartArray<YZ> try_seed_list;
#endif
			};

			Slice_t slices[X_SIZE_P2];

#pragma warning(push)
#pragma warning( disable: 4201 )
			union CubeInfo
			{
				struct
				{
					unsigned short cornerInfoNo;
					bool doneAbove;
					bool doneBelow;
				};
				unsigned int everything;
			};
#pragma warning(pop)

			typedef CubeInfo (*slice_corners_t)[Z_SIZE_P1];

			SmartArray<slice_corners_t> unused_slice_corners;
			SmartArray<SmartArray<byte, true, 0> > unused_corner_info;
			SmartArray<SmartArray<YZ, false, 0> > unused_todo_lists;

			// For stats on amount of allocated memory
			int n_alloced_slice_corners;
			int n_alloced_slice_corner_infos;
			int n_alloced_slice_todo_lists;

			SmartArray<cube_index_t> recursion_stack_src;

			YZ m_nStack[1024];
			int m_nStackSize;

			static const int BUCKET_FACTOR = 2;
			static const int BUCKET_X = (PARTICLE_CACHE_XSIZE / BUCKET_FACTOR) + 1;
			static const int BUCKET_Y = (PARTICLE_CACHE_YSIZE / BUCKET_FACTOR) + 1;
			static const int BUCKET_Z = (PARTICLE_CACHE_ZSIZE / BUCKET_FACTOR) + 1;

			// One dimensional array, so we can optimize access to each dimension in tight loops
			CBucket * bucketsByXYZ[BUCKET_X * BUCKET_Y * BUCKET_Z];

			// Pool of available Buckets that can be recycled as needed.
			// Optimize this later by using a single pointer (instead of having to access 2 variables).
			SmartArray<CBucket*> cacheBuckets;
			int curCacheBucket;

			SmartArray<CornerInfo *> cacheCornerInfos;
			int curCacheCornerInfo;

			int maxNumberOfParticlesPerBucket;
			int NumberOfParticlesPerBucketOvertime[CBucket::MAX_PARTICLES_PER_BUCKET + 1];

			Point3D m_mins;
			float m_cubeWidth;

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
			static CBucket zeroBucket;
			static CornerInfo zeroCorners[8];

			int currentCornerIndex;
			CornerInfo * m_pAllCorners;

			VERTEX_BUFFER_CLASS * m_pVertexBuffer;

			void RecalculateConstants();
			void RecalculateBoundingBoxes();

			bool IsParticleWithinBounds(ImpParticle & particle) const;

			// This may not be necessary anymore... See DrawDebugInfo...
			void SetMaxNoSlicesToDraw(int no = -1) { m_nMaxNoSlicesToDraw = no; }
			int GetMaxNoSlicesToDraw() const { return m_nMaxNoSlicesToDraw; }
			int GetLastSliceDrawn() const { return m_nLastSliceDrawn; }

			DOINLINE void DOFASTCALL SeedSurface(Point3D & center);
			void AllocSliceTodoList(Slice_t * slice);
			void DeallocSliceTodoList(Slice_t * slice);
			void RenderSlices();
			DOINLINE void DOFASTCALL RenderSlice(const int x, Slice_t * pPrevSlice, Slice_t * pCurrentSlice, Slice_t * pNextSlice);
			bool DOFASTCALL CalcSign(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const float x, const float y, const float z);

			void CheckBucketAllocations(const int localCurCacheBucket);
			void FillBuckets(int particleIndex, int minX, int maxX, int minY, int maxY, int minZ, int maxZ);
			void BuildCache(float cw, float fieldRadius, Point3D & mins, Point3D & maxs);

			void Calculate(CBucket * bucket);
			void CalcVertex(const float x, const float y, const float z, const int edge, const CornerInfo * pCorner1, const CornerInfo * pCorner2);

			FORCEINLINE
			void LazyCalculate(CBucket * bucket)
			{
				if (bucket->IsCalculated() == false)
				{
					Calculate(bucket);
				}
			}

			// Although these functions are fast, you may want to avoid calling them unnecessarily
			// Adding constants is faster than multiplying a variable by a constant and adding it.

			FORCEINLINE
			static int GetIndexFromX(int x)
			{
				return (x / BUCKET_FACTOR) * (BUCKET_Y * BUCKET_Z);
			}

			FORCEINLINE
			static int GetIndexFromY(int y)
			{
				return (y / BUCKET_FACTOR) * (BUCKET_Z);
			}

			FORCEINLINE
			static int GetIndexFromZ(int z)
			{
				return (z / BUCKET_FACTOR);
			}

			FORCEINLINE
			static int GetIndexFromXYZ(int x, int y, int z)
			{
				int index = (x / BUCKET_FACTOR) * (BUCKET_Y * BUCKET_Z);
				index += (y / BUCKET_FACTOR) * (BUCKET_Z);
				index += (z / BUCKET_FACTOR);
				return index;
			}

			FORCEINLINE
			static int GetCornerIndexFromXYZ(int x, int y, int z)
			{
				// & 1 is valid only because BUCKET_FACTOR is 2.
				int offset = (x & 1) * CBucket::X_FLAG;
				offset |= (y & 1) * CBucket::Y_FLAG;
				offset |= (z & 1) * CBucket::Z_FLAG;
				return (offset);
			}

			FORCEINLINE
			CBucket * GetBucketFromXYZ(int x, int y, int z)
			{
				int index = GetIndexFromXYZ(x, y, z);
				return (bucketsByXYZ[index]);
			}

			// From http://www.hackersdelight.org/basics.pdf
			FORCEINLINE
			int FastAbs(int x)
			{
				int y = x >> 31;
				return (x ^ y) - y;
			}

			// This returns the corner so we don't have to read the corner info in the bucket (and we avoid an LHS that way).
			CornerInfo * FillNewCornerInfo(CBucket * bucket);
		};
	}
}

#endif

