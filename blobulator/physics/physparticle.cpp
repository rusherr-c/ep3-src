//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#ifdef IS_WINDOWS_PC
#include <windows.h>
#include <gl/gl.h>
#include <gl/glu.h>
#endif
#include "../util/rand.h"
#include "blobulator/physics/physparticle.h"

PhysParticle::PhysParticle():center(),radius(1.0) //,temp(1.0)
{
}

/*
PhysParticle::PhysParticle(Point3D _center, float _radius)
{
	center = _center;
	radius = _radius;
	
	interpolants1 = Point3D(1.0f,0.0f,0.0f);

	frozen = false;
	velocity = Point3D(0,0,0);
	temp = -2;
	group = 0;
	prev_group = 0;
	//neighbors = NULL;
}
*/
/*
void PhysParticle::move(float ts)
{
	if(!frozen)
	{
		velocity.add(force*ts);

		float l = velocity.length();
		if (l > 10.0f) velocity = velocity * (10.0f / l);

		center.add(velocity*ts);
	}
}
*/
