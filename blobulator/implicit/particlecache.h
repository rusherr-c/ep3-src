//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

// NOTE: Wherever this file is included, ImpDefines.h should be included before this file is included.

#if (CACHE_MODE==PROJECTING_PARTICLE_CACHE)
	#define PARTICLE_CACHE_CLASS ProjectingParticleCache
	#include "projectingparticlecache.h"
#elif (CACHE_MODE==SIDE_PROJECTING_PARTICLE_CACHE)
	#define PARTICLE_CACHE_CLASS SideProjectingParticleCache
	#include "sideprojectingparticlecache.h"
#elif (CACHE_MODE==SLICE_2D_PARTICLE_CACHE)
	#define PARTICLE_CACHE_CLASS Slice2DParticleCache
	#include "slice2dparticlecache.h"
#elif (CACHE_MODE==MC_PARTICLE_CACHE)
	#define PARTICLE_CACHE_CLASS MCParticleCache
	#include "mcparticlecache.h"
#elif (CACHE_MODE==HASH_PARTICLE_CACHE)
	#define PARTICLE_CACHE_CLASS HashParticleCache
	#include "hashparticlecache.h"
#elif (CACHE_MODE==INF_HASH_PARTICLE_CACHE)
	#define PARTICLE_CACHE_CLASS InfHashParticleCache
	#include "infhashparticlecache.h"
#elif (CACHE_MODE==VOLUMETRIC_PARTICLE_CACHE)
	#define PARTICLE_CACHE_CLASS VolumetricParticleCache
	#include "volumetricparticlecache.h"
#endif

