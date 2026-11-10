//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//
#include "blobulator/util/fasttrunc.h"
#include "tier0/dbg.h"

unsigned int saved;

void turnOnTruncMode()
{
	// RC == Rounding Control, CHOP == Truncate
	Assert( 0 ); 
	// NOT IMPL
	//saved = _statusfp();
	//_controlfp_s(&saved,_MCW_RC,_RC_CHOP);
}

void restoreOldRoundMode()
{
	Assert( 0 ); 
	// NOT IMPL
	//_controlfp_s(&saved,_MCW_RC,saved);
}
