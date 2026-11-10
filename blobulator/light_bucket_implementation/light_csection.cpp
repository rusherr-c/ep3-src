//========= Copyright (c), Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "blobulator/light_bucket_implementation/light_csection.h"

namespace Blobulator
{
namespace LightBucketImplementation
{

void CSection::Initialize()
{
	Assert(m_nInit == false);
	m_nInit = 1;

	// This method is called the first time we succesfully lock the section.
	// No calculation, seeding has been done yet, so we can reset flags that late.
	ClearDoneState();
	ClearCrawlingState();

	// Although this is not necessary (as we check that the corner indices matches, this will avoid potential very rare issue,
	// where the index matches but the calculation is not correct.
	V_memset(m_nCornerIndices, -1, sizeof(m_nCornerIndices));
}

void CSection::Log() const
{
#ifdef SPU
	VjobSpuLog("\n");
	VjobSpuLog("CSection: (%d, %d, %d)\n", m_nX, m_nY, m_nZ);
	VjobSpuLog("    seed_list.size: %d\n", m_SeedList.GetSize());
	VjobSpuLog("    flags:\n");
	VjobSpuLog("        %llX %llX %llX %llX %llX %llX\n\n", m_SectionXM.m_nFlags64, m_SectionXP.m_nFlags64, m_SectionYM.m_nFlags64, m_SectionYP.m_nFlags64, m_SectionZM.m_nFlags64, m_SectionZP.m_nFlags64);
	VjobSpuLog("    internalDoneState:\n        ");
	for (int i = 0 ; i < 8 ; ++i)
	{
		VjobSpuLog("%llX ", m_InternalDoneState[i].m_nFlags64);
	}
	VjobSpuLog("\n\n");
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
		const int TAG = 12;
		// Make sure that the values are in sync with the main memory.
		Assert( section.m_nX == cellDmaGetUint8( uintp( &pEaSection->m_nX ), TAG, 0, 0 ) );
		Assert( section.m_nY == cellDmaGetUint8( uintp( &pEaSection->m_nY ), TAG, 0, 0 ) );
		Assert( section.m_nZ == cellDmaGetUint8( uintp( &pEaSection->m_nZ ), TAG, 0, 0 ) );
#endif
	}
#endif
}

}
}
