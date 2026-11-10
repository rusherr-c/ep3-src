//========= Copyright © 1996-2010, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef BLOBULATOR_BUCKETIMPLEMENTATION_CBUCKET_H
#define BLOBULATOR_BUCKETIMPLEMENTATION_CBUCKET_H

namespace Blobulator
{
	namespace BucketImplementation
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
				m_nNumParticles(0),
				m_pCorners(NULL)
			{
				// Do nothing...
			}

			~CBucket()
			{
				// Do nothing...
			}

			void SetXYZ(int x, int y, int z)
			{
				m_nX = x;
				m_nY = y;
				m_nZ = z;
			}

			// 3 bytes - Instead of storing XYZ, we could store the index. This would be faster in some code (although harder to debug).
			uint8 m_nX, m_nY, m_nZ;
			// 1 byte
			uint8 m_nNumParticles;

			// 4 bytes
			CornerInfo * m_pCorners;

			// 1 byte - To reduce LHS, doneAbove and doneBelow are not close to each others
			uint8 doneAbove;

			// 22 bytes
			// Up to 22 particles in a bucket.
			// It saves memory and improve performance and has non-noticeable impact on the blob (except when integrated in a stream).
			// TODO: If over 22 particles, see how we can linked list to another memory location.
			static const int MAX_PARTICLES_PER_BUCKET = 22;
			uint8 m_ParticlesIndex[MAX_PARTICLES_PER_BUCKET];

			enum
			{
				X_FLAG = (1 << 2),
				Y_FLAG = (1 << 1),
				Z_FLAG = (1 << 0),
			};

			FORCEINLINE
			bool IsCalculated() const
			{
				return (m_pCorners != 0);
			}

			// These 8 values represent the 8 cubes in the bucket (x,y,z to x+1,y+1,z+1)
			// Although for the corners, it is not the exact same thing, but the same idea.
			// TODO:	Replace these by a byte. Each value can be accessed with a flag directly.
			//	Investigate if it means we have to use a micro-coded instruction.

			// We don't want to users to access the variable directly as the implementation may change.
			// We could use a table if needed. Compare with the memory access cost.
			// Use accessors functions instead.

			FORCEINLINE
			bool IsDoneAbove(int offset) const
			{
				// Micro-coded on X360 and PS3
				return (doneAbove & (1 << offset)) != 0;
			}

			FORCEINLINE
			bool IsDoneBelow(int offset) const
			{
				// Micro-coded on X360 and PS3
				return (doneBelow & (1 << offset)) != 0;
			}

			FORCEINLINE
			void SetDoneAbove(int offset)
			{
				// Micro-coded on X360 and PS3
				doneAbove |= 1 << offset;
			}

			FORCEINLINE
			void SetDoneBelow(int offset)
			{
				// Micro-coded on X360 and PS3
				doneBelow |= 1 << offset;
			}

			FORCEINLINE
			void ClearAllAboveBelowFlags()
			{
				doneAbove = doneBelow = 0;
			}

			FORCEINLINE
			void SetAllAboveBelowFlags()
			{
				doneAbove = doneBelow = 0xff;
			}

			// 1 byte - To reduce LHS, doneAbove and doneBelow are not close to each others
			uint8 doneBelow;

			// 32 bytes
		};
	}
}

#endif

