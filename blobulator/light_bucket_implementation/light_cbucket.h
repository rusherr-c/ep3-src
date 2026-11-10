//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CBUCKET_H
#define BLOBULATOR_LIGHTBUCKETIMPLEMENTATION_CBUCKET_H

#include "blobulator/Implicit/ImpDefines.h"
#include "blobulator/light_bucket_implementation/light_csoftware_cache.h"

#ifdef SPU
#include "ps3/spu_job_shared.h"
#else
#include "tier0/platform.h"
#endif

namespace Blobulator
{
	namespace LightBucketImplementation
	{
		class CCornerInfo;

#if IsPlatformPS3()
		// The PS3 needs 16 bytes alignment to help the DMA accesses
		class ALIGN16 CBucket
#else
		class CBucket
#endif
		{
		public:
			CBucket()
				:
				m_nX(0),
				m_nY(0),
				m_nZ(0)
#if ShouldUseSectionForParticles() == 0
				,m_nNumberOfParticles(0)
#endif
			{
				for (int i = 0 ; i < NUM_CORNERS_CACHE ; ++i)
				{
					m_pCorners[i] = NULL;
				}
			}

			~CBucket()
			{
				// Do nothing...
			}

			void SetXYZ(int x, int y, int z)
			{
				// To make debugging easier, keep using the index
				m_nX = x;
				m_nY = y;
				m_nZ = z;
			}

			// 3 bytes - Instead of storing XYZ, we could store the index. This would be faster in some code (although harder to debug).
			uint8 m_nX, m_nY, m_nZ;
			// 1 byte
#if ShouldUseSectionForParticles()
			uint8 m_Pad;
#else
			uint8 m_nNumberOfParticles;
#endif

			// 4 bytes per jobs
			// We use different corner cache per job for several reasons:
			//  - Synchronization of the various corner caches between jobs is particularly hard and inefficient.
			//	- The corners store the vertex ids that are specific by vertex buffer, so sharing them between jobs results in incorrect rendering.
			// This solution is simple and robust. With a minimal memory and performance cost.
			// On SPU, the CCornerInfo points to the local store.

			CCornerInfo * m_pCorners[NUM_CORNERS_CACHE];

#if ShouldUseSectionForParticles() == 0
			// 24 bytes
			// Up to 24 particles in a bucket.
			// It saves memory and improve performance and has non-noticeable impact on the blob (except when integrated in a stream).
			// TODO: If over 24 particles, see how we can linked list to another memory location. Currently 20 particles is not enough to have a nice blob effect.
			static const int MAX_PARTICLES_PER_BUCKET = 24;
			uint8 m_nParticleIndices[MAX_PARTICLES_PER_BUCKET];
#endif

			enum
			{
				X_FLAG = (1 << 2),
				Y_FLAG = (1 << 1),
				Z_FLAG = (1 << 0),
			};


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
			bool IsCalculated(int nJobIndex) const
			{
				Assert(nJobIndex < NUM_CORNERS_CACHE);
				return (m_pCorners[nJobIndex] != NULL);
			}

			// 28 bytes + 4 bytes per job

			void Log() const;
			static void LogAndCheck(const CBucket & bucket, CBucket * pEaBucket, bool log = true);
		}
#if IsPlatformPS3()
		ALIGN16_POST;
#else
		;
#endif
	}
}

#endif

