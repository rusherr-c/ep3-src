//========= Copyright (c), Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "blobulator/job_implementation/job_csection.h"
#include "blobulator/job_implementation/job_cbucket.h"

namespace Blobulator
{
namespace JobImplementation
{

void CSection::Log() const
{
#ifdef SPU
	spu_printf("\n");
	spu_printf("CSection: (%d, %d, %d)\n", m_nX, m_nY, m_nZ);
	spu_printf("    seed_list.size: %d\n", seed_list.GetSize());
	spu_printf("    flags:\n");
	spu_printf("        %llX %llX %llX %llX %llX %llX\n\n", sectionXM.m_nFlags64, sectionXP.m_nFlags64, sectionYM.m_nFlags64, sectionYP.m_nFlags64, sectionZM.m_nFlags64, sectionZP.m_nFlags64);
	spu_printf("    internalDoneState:\n        ");
	for (int i = 0 ; i < 8 ; ++i)
	{
		spu_printf("%llX ", internalDoneState[i].m_nFlags64);
	}
	spu_printf("\n\n        ");
	for (int i = 0 ; i < BUCKETS_PER_SECTION_X * BUCKETS_PER_SECTION_Y * BUCKETS_PER_SECTION_Z ; ++i)
	{
		spu_printf("%d ", mBucketsInSection[i]);
		if ((i % 25) == 24)
		{
			spu_printf("\n        ");
		}
	}
	spu_printf("\n\n");
#endif
}

void CSection::LogAndCheck(const CSection & section, CSection * pEaSection, bool log)
{
#ifdef SPU
	if (log)
	{
		section.Log();
	}

	if (pEaSection != NULL)
	{
#if _DEBUG
		const int TAG = 7;
		// Make sure that the values are in sync with the main memory.
		Assert( section.m_nX == cellDmaGetUint8( uintp( &pEaSection->m_nX ), TAG, 0, 0 ) );
		Assert( section.m_nY == cellDmaGetUint8( uintp( &pEaSection->m_nY ), TAG, 0, 0 ) );
		Assert( section.m_nZ == cellDmaGetUint8( uintp( &pEaSection->m_nZ ), TAG, 0, 0 ) );
#endif
	}
#endif
}

// TODO: Put this in its own cpp file
void CBucket::Log() const
{
#ifdef SPU
	spu_printf("\n");
	spu_printf("CBucket: (%d, %d, %d)\n", m_nX, m_nY, m_nZ);
	spu_printf("    NumParticles: %d\n", m_nNumParticles);
	spu_printf("\n\n");
#endif
}

void CBucket::LogAndCheck(const CBucket & bucket, CBucket * pEaBucket, bool log)
{
#ifdef SPU
	if (log)
	{
		bucket.Log();
	}

	if (pEaBucket != NULL)
	{
#if _DEBUG
		const int TAG = 7;
		// Make sure that the values are in sync with the main memory.
		Assert( bucket.m_nX == cellDmaGetUint8( uintp( &pEaBucket->m_nX ), TAG, 0, 0 ) );
		Assert( bucket.m_nY == cellDmaGetUint8( uintp( &pEaBucket->m_nY ), TAG, 0, 0 ) );
		Assert( bucket.m_nZ == cellDmaGetUint8( uintp( &pEaBucket->m_nZ ), TAG, 0, 0 ) );
		Assert( bucket.m_nNumParticles == cellDmaGetUint8( uintp( &pEaBucket->m_nNumParticles ), TAG, 0, 0 ) );
#endif
	}
#endif
}

}
}
