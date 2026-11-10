#pragma once

#include "tier0/platform.h"
#include "vstdlib/random.h"

FORCEINLINE float frand() { return RandomFloat(0.0f, 1.0f); }
FORCEINLINE float frand2() { return RandomFloat(-1.0f, 1.0f); }