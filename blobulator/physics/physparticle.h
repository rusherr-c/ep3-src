//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#pragma once

#include "../point3d.h"

//class PhysParticleCacheNode;

class PhysParticle
{
public:
	ALIGN16 Point3D center;
	float radius; // NOTE: The radius is not used currently!!!!
	//Point3D force, velocity;//, neighbors;
	//int neighbor_count;
	//int neighbor_list;
	//PhysParticleCacheNode* neighbors;

	PhysParticle();
	//PhysParticle(Point3D center, float radius);
	//void move(float ts);

	//int group;
	//int prev_group;

	// I believe these three variables aren't used
	//int frozen;
	int temp1;
	//int temp2;

	/*
	float scale;

	void setFieldScale(float _scale)
	{
		scale = _scale;
		if(_scale > 1.0f) _scale = 1.0f;
		if(_scale <= 0.0f) _scale = 0.001f;

		float fieldRScale = (1.0f/_scale);
		fieldRScaleSq = fieldRScale*fieldRScale;
	}
	*/
};

/*
class PhysParticleListNode
{
public:
PhysParticle* particle;
PhysParticleListNode* next;
int mask; // TODO: shouldn't really be here
};	

class PhysParticleCacheNode
{
public:
PhysParticle* particle;
float dist;
Point3D r;
};
*/
