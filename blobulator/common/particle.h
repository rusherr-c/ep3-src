//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "../Point3D.h"

class ImpParticleCacheNode;

class ImpParticle
{
 public:
	ALIGN16 Point3D center;
	ALIGN16 Point3D interpolants;
	ALIGN16 Point3D interpolants2;
	ALIGN16 Point3D interpolants3;
	ALIGN16 Point3D interpolants4;
	float fieldRScaleSq;
	float temp;
	float radius;
	Point3D force, velocity;//, neighbors;
	int neighbor_count;
	int neighbor_list;
	//ImpParticleCacheNode* neighbors;

	ImpParticle();
	//ImpParticle(Point3D center, float radius);
	void move(float ts);

	int group;
	int prev_group;

	// I believe these three variables aren't used
	int frozen;
	int temp1;
	int temp2;

	float scale;

	void setFieldScale(float _scale)
	{
		scale = _scale;
		if(_scale > 1.0f) _scale = 1.0f;
		if(_scale <= 0.0f) _scale = 0.001f;

		float fieldRScale = (1.0f/_scale);
		fieldRScaleSq = fieldRScale*fieldRScale;
	}
};

/*
class ImpParticleListNode
{
  public:
	ImpParticle* particle;
	ImpParticleListNode* next;
	int mask; // TODO: shouldn't really be here
};	

class ImpParticleCacheNode
{
  public:
	ImpParticle* particle;
	float dist;
	Point3D r;
};
*/
