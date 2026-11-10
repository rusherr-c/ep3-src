//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_SECTIONIMPLEMENTATION_CBUCKET_H
#define BLOBULATOR_SECTIONIMPLEMENTATION_CBUCKET_H

#include "blobulator/implicit/impdefines.h"

namespace Blobulator
{
	namespace SectionImplementation
	{
		class CornerInfo;

		class CBucket
		{
		public:
			CBucket()
				:
				m_nX(0),
				m_nY(0),
				m_nZ(0),
				m_nNumParticles(0)
			{
				for (int i = 0 ; i < BLOB_NUM_JOBS ; ++i)
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
			uint8 m_nNumParticles;

			// 4 bytes per jobs
			// We use different corner cache per job for several reasons:
			//  - Synchronization of the various corner caches between jobs is particularly hard and inefficient.
			//	- The corners store the vertex ids that are specific by vertex buffer, so sharing them between jobs results in incorrect rendering.
			// This solution is simple and robust. With a minimal memory and performance cost.
			CornerInfo * m_pCorners[BLOB_NUM_JOBS];

			// 1 byte - To reduce LHS, doneAbove and doneBelow are not close to each others
			uint8 cubeDone;

			// 20 bytes
			// Up to 20 particles in a bucket.
			// It saves memory and improve performance and has non-noticeable impact on the blob (except when integrated in a stream).
			// TODO: If over 20 particles, see how we can linked list to another memory location.
			static const int MAX_PARTICLES_PER_BUCKET = 20;
			uint8 m_ParticlesIndex[MAX_PARTICLES_PER_BUCKET];

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
			bool IsCalculated(int jobIndex) const
			{
				return (m_pCorners[jobIndex] != NULL);
			}

			// These 8 values represent the 8 cubes in the bucket (x,y,z to x+1,y+1,z+1)
			// Although for the corners, it is not the exact same thing, but the same idea.
			// TODO:	Replace these by a byte. Each value can be accessed with a flag directly.
			//	Investigate if it means we have to use a micro-coded instruction.

			// We don't want to users to access the variable directly as the implementation may change.
			// We could use a table if needed. Compare with the memory access cost.
			// Use accessors functions instead.

			FORCEINLINE
			bool IsCubeDone(int offset) const
			{
				// Micro-coded on X360 and PS3
				return (cubeDone & (1 << offset)) != 0;
			}

			FORCEINLINE
			bool IsCubeToBeParsed(int offset) const
			{
				// Micro-coded on X360 and PS3
				return (cubeToBeParsed & (1 << offset)) != 0;
			}

			FORCEINLINE
			void SetCubeDone(int offset)
			{
				// Micro-coded on X360 and PS3
				offset = 1 << offset;
				cubeDone |= offset;
				cubeToBeParsed |= offset;
			}

			FORCEINLINE
			void SetCubeToBeParsed(int offset)
			{
				// Micro-coded on X360 and PS3
				cubeToBeParsed |= 1 << offset;
			}

			FORCEINLINE
			void ClearAllCubeFlags()
			{
				cubeDone = cubeToBeParsed = 0;
			}

			FORCEINLINE
			void SetAllCubeFlags()
			{
				cubeDone = cubeToBeParsed = 0xff;
			}

			// 1 byte - To reduce LHS, cubeDone and cubeToBeParsed are not close to each others
			uint8 cubeToBeParsed;

			// 28 bytes + 4 bytes per job
		};
	}
}

#endif

