//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#if !defined( _X360 )
#include <windows.h>
#include <gl/gl.h>
#include <gl/glu.h>
#endif
#include "../Util/Rand.h"
#include "blobulator/Common/Particle.h"

ImpParticle::ImpParticle():center(),radius(1.0),temp(1.0)
{
	//interpolants = Point3D(1.0f,0.0f,0.0f);
	interpolants = Point3D(frand(),frand(),frand());
	interpolants[4] = 0.0f;
	fieldRScaleSq = 1.0f;
	

	#if 1
		interpolants2 = Point3D(1.0f, 0.0f, 0.0f);
		interpolants3 = Point3D(0.0f, 1.0f, 0.0f);
		interpolants4 = Point3D(0.0f, 0.0f, 1.0f);
	#else
		interpolants2 = Point3D(frand2(),frand2(),frand2());
		interpolants2.normalize();
		interpolants3 = Point3D(frand2(),frand2(),frand2());
		interpolants3.normalize();
		interpolants4 = interpolants2.crossProduct(interpolants3);
		interpolants3 = interpolants2.crossProduct(interpolants4);
		interpolants3.normalize();
		interpolants4.normalize();
	#endif
}

/*
ImpParticle::ImpParticle(Point3D _center, float _radius)
{
	center = _center;
	radius = _radius;
	
	interpolants = Point3D(1.0f,0.0f,0.0f);

	frozen = false;
	velocity = Point3D(0,0,0);
	temp = -2;
	group = 0;
	prev_group = 0;
	//neighbors = NULL;
}
*/

void ImpParticle::move(float ts)
{
	if(!frozen)
	{
		velocity.add(force*ts);

		float l = velocity.length();
		if (l > 10.0f) velocity = velocity * (10.0f / l);

		center.add(velocity*ts);
	}
}

