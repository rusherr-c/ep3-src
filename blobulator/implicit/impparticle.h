//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "../point3d.h"
#include "impdefines.h"

#if !IsPlatformPS3_SPU()
#include "utlvector.h"
#endif

class ImpParticleCacheNode;

#if (RENDER_MODE == RENDER_SWEEP) || (RENDER_MODE == RENDER_MC) || (RENDER_MODE == RENDER_HASH) || (RENDER_MODE == RENDER_INF_HASH) || (RENDER_MODE == RENDER_VOLUMETRIC)
#define USE_VMX_PARTICLE 0
#else
#define USE_VMX_PARTICLE (BLR_USE_SSE2)
#endif

class ALIGN16 ImpParticle
{
 public:
	ALIGN16 Point3D center;	// X | Y | Z | fieldRScaleSq

#if USE_VMX_PARTICLE == 0
	// In case this has not been integrated in the vector above
	float fieldRScaleSq;
#endif

	// Do we actually need this anymore? (in some cases yes, it would be better to remove this).
	float scale, pad1, pad2, pad3;
	// NOTE: I can fit three more floats in here without sacrificing SSE alignment

	ImpParticle() {}

	void setFieldScale(float _scale)
	{
		scale = _scale;
		if(_scale > 1.0f) _scale = 1.0f;
		if(_scale <= 0.0f) _scale = 0.001f;

		float fieldRScale = (1.0f/_scale);

#if USE_VMX_PARTICLE
		center[W] = fieldRScale*fieldRScale;
#else
		fieldRScaleSq = fieldRScale*fieldRScale;
#endif
	}

	float GetFieldRScaleSq() const
	{
#if USE_VMX_PARTICLE
		return center[W];
#else
		return fieldRScaleSq;
#endif
	}

	ImpParticle(const ImpParticle & other)
		:
#if USE_VMX_PARTICLE == 0
		fieldRScaleSq(other.fieldRScaleSq),
#endif
		scale(other.scale)
	{
		center.sse_vec = other.center.sse_vec;
	}	

	ImpParticle & operator=(const ImpParticle & other)
	{
		// No need to self test here
		center.sse_vec = other.center.sse_vec;
#if USE_VMX_PARTICLE == 0
		fieldRScaleSq = other.fieldRScaleSq;
#endif
		scale = other.scale;
		return (*this);
	}

private:
	static const int W = 3;
} ALIGN16_POST;


class ImpParticleWithOneInterpolant : public ImpParticle
{
public:
	ALIGN16 Point3D interpolants1;
};

class ImpParticleWithTwoInterpolants : public ImpParticleWithOneInterpolant
{
public:
	ALIGN16 Point3D interpolants2;
};

class ImpParticleWithFourInterpolants : public ImpParticleWithTwoInterpolants
{
public:
	ALIGN16 Point3D interpolants3;
	ALIGN16 Point3D interpolants4;
};

#if !IsPlatformPS3_SPU()
typedef CUtlVector< ImpParticle, CUtlMemoryAligned< ImpParticle, 16 > > ImpParticleList;
#endif
