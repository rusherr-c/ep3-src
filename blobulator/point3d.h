//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//
#pragma once

#include <math.h>
#include <stdio.h>
#include <assert.h>
#include "implicit/impdefines.h"


#if (BLR_CODE==BLR_SIMD)
	#define BLR_USE_SSE2 1
	#if (BLOB_SETTINGS==BLOB_MONSTER)
		#include "mathlib/ssemath.h"
		#if defined( _X360 )
			#define _mm_set_ps1 ReplicateSIMD
		#endif
	#else
		#include <xmmintrin.h>		
	#endif
#else
	#define BLR_USE_SSE2 0
#endif

#ifndef SPU
#if (defined(RENDER_API) && RENDER_API==VALVE_API)
	#include "mathlib/vector.h"
#endif
#endif


// TODO: Add constness wherever I can

class Point3D
{
public:
	#if BLR_USE_SSE2
		union {
			fltx4 sse_vec;
			float p[4];
		};
	#else
		float p[3];
	#endif


	//Point3D(Point3D& q) {p[0]=q.p[0];p[1]=q.p[1];p[2]=q.p[2];};
	//inline Point3D() {p[0] = p[1] = p[2] = 0.0f;};
	FORCEINLINE Point3D()
	{
		// Initialize w to 0 if we're using SSE2
		// TODO: This might not be necessary
		#if BLR_USE_SSE2
			p[3] = 0.0f;
		#endif
	};

	FORCEINLINE Point3D(const float x, const float y, const float z)
	{
		p[0]=x;p[1]=y;p[2]=z;
		// Initialize w to 0 if we're using SSE2
		#if BLR_USE_SSE2
			p[3] = 0.0f;
		#endif
	};

#if (defined(RENDER_API) && (RENDER_API==VALVE_API) && !defined(SPU))
	// No sense to support 3D floating point Vector on SPU
	FORCEINLINE Point3D(const Vector& v)
	{
		p[0]=v.x;p[1]=v.y;p[2]=v.z;
		// Initialize w to 0 if we're using SSE2
		#if BLR_USE_SSE2
			p[3] = 0.0f;
		#endif
	};

	FORCEINLINE Point3D& operator=(const Vector& v)
	{
		p[0]=v.x;p[1]=v.y;p[2]=v.z;
		// Initialize w to 0 if we're using SSE2
		#if BLR_USE_SSE2
			p[3] = 0.0f;
		#endif
		return *this;
	};

	FORCEINLINE const Vector& AsVector()
	{
		return *(Vector *)((void *)&p[0]);
	}

#endif

	FORCEINLINE void clear()
	{
		p[0] = p[1] = p[2] = 0.0f;
		#if BLR_USE_SSE2
			p[3] = 0.0f;
		#endif
	};

	FORCEINLINE void set( const float x, const float y, const float z ) {p[0] = x; p[1] = y; p[2] = z;}
	FORCEINLINE void set( const float x, const float y, const float z, const float w ) {p[0] = x; p[1] = y; p[2] = z; p[3] = w;}

#ifdef SPU
	// Executing printf() on SPU is never a good idea.
	void print(void)	{	}
	void printnl(void)	{	}
#else
	void print(void){printf("(%f,%f,%f)",p[0],p[1],p[2]);}
	void printnl(void){print();printf("\n");}
#endif

	/* These are the implementations of the promised methods for Point3D */
	FORCEINLINE float& operator[] (const int i) {
		#ifdef ASSERT_ARRAY
		assert(0<=i && i<3);
		#endif
		return p[i];
	}

	/* These are the implementations of the promised methods for Point3D */
	FORCEINLINE float operator[] (const int i) const {
		#ifdef ASSERT_ARRAY
		assert(0<=i && i<3);
		#endif
		return p[i];
	}

	FORCEINLINE float dot(const Point3D& q) const {return p[0]*q.p[0]+p[1]*q.p[1]+p[2]*q.p[2]; }
	
	FORCEINLINE float length(const Point3D& q) const { float sum = p[0]-q.p[0]; float temp = p[1]-q.p[1]; sum = sum*sum + temp*temp; temp = p[2]-q.p[2]; return sqrtf(sum + temp*temp); }
	FORCEINLINE float length(void) const {return sqrtf(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]);}
	FORCEINLINE float lengthSq(const Point3D& q) const { float sum = p[0]-q.p[0]; float temp = p[1]-q.p[1]; sum = sum*sum + temp*temp; temp = p[2]-q.p[2]; return sum + temp*temp; }
	FORCEINLINE float lengthSq(void) const {return p[0]*p[0] + p[1]*p[1] + p[2]*p[2];}

	FORCEINLINE Point3D& normalize(void)
	{
		#if 1
			// IT's really weird... the code below seems faster than the code that uses three multiplications
			// I think the length call does not get inlined because the compiler does not inline stuff in inlined functions
			float l=length();
			if (l == 0.0f) l = 1.0f;
			p[0]/=l; p[1]/=l; p[2]/=l;
		#else
			// TODO: Can try using reciprocal square root here?
			// TODO: On XBOX, there might be a faster way to do this. Look at Vector class.
			float oneOverL = 1.0f / (sqrtf(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]) + FLT_EPSILON);
			p[0] *= oneOverL; p[1] *= oneOverL; p[2] *= oneOverL;
		#endif
		return *this;
	}
	FORCEINLINE Point3D unit(void) const
	{
		#if 1
			// IT's really weird... the code below seems faster than the code that uses three multiplications
			float l=length();
			if (l == 0.0f) l = 1.0f;
			Point3D n; n.p[0]=p[0]/l;n.p[1]=p[1]/l;n.p[2]=p[2]/l; return n;
		#else
			// TODO: Can try using reciprocal square root here?
			// TODO: On XBOX, there might be a faster way to do this. Look at Vector class.
			float oneOverL = 1.0f / (sqrtf(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]) + FLT_EPSILON);
			Point3D n;
			n[0] = p[0]*oneOverL; n[1] = p[1]*oneOverL; n[2] *= p[2]*oneOverL;
			return n;
		#endif
	}
	FORCEINLINE Point3D& negate(void) { p[0]=-p[0]; p[1]=-p[1]; p[2]=-p[2]; return *this;}
	FORCEINLINE Point3D operator -(void) const {Point3D n; n.p[0] = -p[0]; n.p[1] = -p[1]; n.p[2]=-p[2]; return n; }

	FORCEINLINE Point3D& add(const Point3D& q){ p[0]+=q.p[0];p[1]+=q.p[1];p[2]+=q.p[2]; return *this; }
	FORCEINLINE Point3D operator+ (const Point3D& q) const {Point3D n; n.p[0]=p[0]+q.p[0]; n.p[1]=p[1]+q.p[1]; n.p[2]=p[2]+q.p[2]; return n;}

	FORCEINLINE Point3D& subtract(const Point3D& q){ p[0]-=q.p[0];p[1]-=q.p[1];p[2]-=q.p[2]; return *this; }
	FORCEINLINE Point3D operator- (const Point3D& q) const {Point3D n; n.p[0]=p[0]-q.p[0]; n.p[1]=p[1]-q.p[1]; n.p[2]=p[2]-q.p[2]; return n;}

	FORCEINLINE Point3D& mult(const float s) {p[0]*=s;p[1]*=s;p[2]*=s; return *this; }
	FORCEINLINE Point3D& mult(const Point3D& q){ p[0]*=q.p[0];p[1]*=q.p[1];p[2]*=q.p[2]; return *this; }
	FORCEINLINE Point3D operator* (const float s) const {Point3D n; n.p[0]=p[0]*s;n.p[1]=p[1]*s;n.p[2]=p[2]*s; return n;}
	FORCEINLINE Point3D operator* (const Point3D& q) const {Point3D n; n.p[0]=p[0]*q.p[0];n.p[1]=p[1]*q.p[1];n.p[2]=p[2]*q.p[2]; return n;}

	FORCEINLINE Point3D& div(const float s){ p[0]/=s;p[1]/=s;p[2]/=s; return *this; }
	FORCEINLINE Point3D& div(const Point3D& q){ p[0]/=q.p[0];p[1]/=q.p[1];p[2]/=q.p[2]; return *this; }
	FORCEINLINE Point3D operator/ (const float s) const {Point3D n; n.p[0]=p[0]/s;n.p[1]=p[1]/s;n.p[2]=p[2]/s; return n;}
	FORCEINLINE Point3D operator/ (const Point3D& q) const {Point3D n; n.p[0]=p[0]/q.p[0];n.p[1]=p[1]/q.p[1];n.p[2]=p[2]/q.p[2]; return n;}

	FORCEINLINE bool operator==(const Point3D& q) const { return p[0]==q[0] && p[1]==q[1] && p[2]==q[2];};

	FORCEINLINE Point3D crossProduct(const Point3D& q) const {Point3D n; n.p[0]=p[1]*q.p[2]-p[2]*q.p[1];n.p[1]=-p[0]*q.p[2]+p[2]*q.p[0];n.p[2]=p[0]*q.p[1]-p[1]*q.p[0]; return n;}

	FORCEINLINE Point3D& toInt(){ p[0]=floorf(p[0]); p[1]=floorf(p[1]); p[2]=floorf(p[2]); return *this; }
	FORCEINLINE Point3D getToInt() const { Point3D n; n.p[0]=floorf(p[0]);n.p[1]=floorf(p[1]);n.p[2]=floorf(p[2]); return n;}
};



/*

class Point4D{
public:

#if BLR_USE_SSE2
	union
	{
		float p[4];
		struct xyzw
		{
			float x, y, z, w;
		};
	};
#else
	union
	{
		float p[4];
		struct xyzw
		{
			float x, y, z, w;
		};
	};
#endif

	//Point4D(Point4D& q) {p[0]=q.p[0];p[1]=q.p[1];p[2]=q.p[2];};
	//inline Point4D() {p[0] = p[1] = p[2] = 0.0f;};
	Point4D() {};
	inline Point4D(Flt x,Flt y,Flt z){p[0]=x;p[1]=y;p[2]=z;};
	inline void Point4D::clear() { p[0] = p[1] = p[2] = 0.0f; };

	inline void Point4D::set(float x, float y, float z) {p[0] = x; p[1] = y; p[2] = z;};

	void print(void){printf("(%f,%f,%f)",p[0],p[1],p[2]);}
	void printnl(void){print();printf("\n");}

	// These are the implementations of the promised methods for Point4D
	inline float& Point4D::operator[] (int i){
#ifdef ASSERT_ARRAY
		assert(0<=i && i<3);
#endif
		return p[i];
	}

	inline float Point4D::dot(Point4D& q) {return p[0]*q.p[0]+p[1]*q.p[1]+p[2]*q.p[2]; }

	inline float Point4D::length(Point4D& q) { float sum = p[0]-q.p[0]; float temp = p[1]-q.p[1]; sum = sum*sum + temp*temp; temp = p[2]-q.p[2]; return sqrtf(sum + temp*temp); }
	inline float Point4D::length(void){return sqrtf(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]);}
	inline float Point4D::lengthSq(Point4D& q) { float sum = p[0]-q.p[0]; float temp = p[1]-q.p[1]; sum = sum*sum + temp*temp; temp = p[2]-q.p[2]; return sum + temp*temp; }
	inline float Point4D::lengthSq(void){return p[0]*p[0] + p[1]*p[1] + p[2]*p[2];}

	inline Point4D& Point4D::normalize(void)
	{
		// I think the length call does not get inlined because the compiler does not inline stuff in inlined functions
		float l=length();
		if (l == 0.0f) l = 1.0f;
		p[0]/=l; p[1]/=l; p[2]/=l;
		return *this;
	}
	inline Point4D Point4D::unit(void)
	{
		float l=length();
		if (l == 0.0f) l = 1.0f;
		Point4D n; n.p[0]=p[0]/l;n.p[1]=p[1]/l;n.p[2]=p[2]/l; return n;
	}
	inline Point4D& Point4D::negate(void){ p[0]=-p[0]; p[1]=-p[1]; p[2]=-p[2]; return *this;}
	inline Point4D Point4D::operator -(void){Point4D n; n.p[0] = -p[0]; n.p[1] = -p[1]; n.p[2]=-p[2]; return n; }

	inline Point4D& Point4D::add(const Point4D& q){ p[0]+=q.p[0];p[1]+=q.p[1];p[2]+=q.p[2]; return *this; }
	inline Point4D Point4D::operator+ (Point4D& q){Point4D n; n.p[0]=p[0]+q.p[0]; n.p[1]=p[1]+q.p[1]; n.p[2]=p[2]+q.p[2]; return n;}

	inline Point4D& Point4D::subtract(Point4D& q){ p[0]-=q.p[0];p[1]-=q.p[1];p[2]-=q.p[2]; return *this; }
	inline Point4D Point4D::operator- (Point4D& q){Point4D n; n.p[0]=p[0]-q.p[0]; n.p[1]=p[1]-q.p[1]; n.p[2]=p[2]-q.p[2]; return n;}

	inline Point4D& Point4D::mult(Flt s){p[0]*=s;p[1]*=s;p[2]*=s; return *this; }
	inline Point4D& Point4D::mult(Point4D& q){ p[0]*=q.p[0];p[1]*=q.p[1];p[2]*=q.p[2]; return *this; }
	inline Point4D Point4D::operator* (Flt s){Point4D n; n.p[0]=p[0]*s;n.p[1]=p[1]*s;n.p[2]=p[2]*s; return n;}
	inline Point4D Point4D::operator* (Point4D& q){Point4D n; n.p[0]=p[0]*q.p[0];n.p[1]=p[1]*q.p[1];n.p[2]=p[2]*q.p[2]; return n;}

	inline Point4D& Point4D::div(Flt s){ p[0]/=s;p[1]/=s;p[2]/=s; return *this; }
	inline Point4D& Point4D::div(Point4D& q){ p[0]/=q.p[0];p[1]/=q.p[1];p[2]/=q.p[2]; return *this; }
	inline Point4D Point4D::operator/ (Flt s){Point4D n; n.p[0]=p[0]/s;n.p[1]=p[1]/s;n.p[2]=p[2]/s; return n;}
	inline Point4D Point4D::operator/ (Point4D& q){Point4D n; n.p[0]=p[0]/q.p[0];n.p[1]=p[1]/q.p[1];n.p[2]=p[2]/q.p[2]; return n;}

	inline Point4D Point4D::crossProduct(Point4D& q){Point4D n; n.p[0]=p[1]*q.p[2]-p[2]*q.p[1];n.p[1]=-p[0]*q.p[2]+p[2]*q.p[0];n.p[2]=p[0]*q.p[1]-p[1]*q.p[0]; return n;}

	inline Point4D& Point4D::toInt(){ p[0]=floorf(p[0]); p[1]=floorf(p[1]); p[2]=floorf(p[2]); return *this; }
	inline Point4D Point4D::getToInt(){ Point4D n; n.p[0]=floorf(p[0]);n.p[1]=floorf(p[1]);n.p[2]=floorf(p[2]); return n;}
};

*/
