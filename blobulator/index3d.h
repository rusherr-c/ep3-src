//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//
#pragma once

#include <math.h>
#include <stdio.h>
#include <assert.h>

class Index3D{
public:
	int x, y, z;
	Index3D(){}
	Index3D(int ax,int ay,int az){x=ax;y=ay;z=az;}

	void print(void){printf("(%d,%d,%d)",x,y,z);}
	void printnl(void){print();printf("\n");}

	Index3D operator- (void);
	Index3D operator+ (Index3D pt);
	Index3D operator- (Index3D pt);
};

Index3D Index3D::operator-(void) { return Index3D(-x, -y, -z); }
Index3D Index3D::operator+(Index3D pt) { return Index3D(x + pt.x, y + pt.y, z + pt.z); }
Index3D Index3D::operator-(Index3D pt) { return Index3D(x - pt.z, y - pt.y, z - pt.z); }