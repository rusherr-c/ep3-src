//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include "blobulator/implicit/impdefines.h"

#if (MT_ENABLED == 1)
#include "blobulator/implicit/syncro.h"

SPINLOCK_T taskListLock = 1;
#endif

#include "memdbgon.h"
