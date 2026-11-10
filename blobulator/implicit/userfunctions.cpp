//==== Blobulator. Copyright © 2005-2006, Rosware LLC, All rights reserved. ====//

#include "tier0/platform.h"
#include "mathlib/ssemath.h"

#include "blobulator/util/constants.h"
#include "blobulator/implicit/userfunctions.h"
#include "blobulator/implicit/imptiler.h" // this is to get access to tile_debug_color
#include "../vertexbuffers/vertexbuffer_inl.h"

#include "memdbgon.h"


/////////////////////////////////////////////////////
// User Defined Sign Calc Funcs
/////////////////////////////////////////////////////

// TODO: Test if fastcall actually helps perf
// TODO: Re-enable the SIMD version. Optimize it as well.
// TODO: Change ix, iy, iz to be 'int' and not cube_index_t (i.e. unsigned char), this will help detect overflow more easily (by having -1 instead of 0xff).
bool DOFASTCALL calcSign(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, PARTICLE_CACHE_CLASS* pCache)
{
	#ifdef SPEED_CHECK
		//tt++;
		int cache_count = 0;
		int influence_count = 0;
	#endif

	float totalf = 0.0f;

	START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, pCache, false)
		float *c = p->center.p;

		// This is the code without assembly
		float diff_x = x - c[0];
		float diff_y = y - c[1];
		float rSq = diff_x * diff_x;
		float diff_z = z - c[2];
		rSq += diff_y * diff_y;
		rSq += diff_z * diff_z;
		rSq *= p->GetFieldRScaleSq();  // TODO: The fieldRScaleSq should incorporate scalerSq (can compare rSq with one over sqrt(2) instead of cutoffRSq below)
		
		#if (DO_RADIUS_CHECK == 1)
		if(rSq <= RENDERER_CLASS::cutoffRSq)
		{	
		#endif
			rSq *= RENDERER_CLASS::scalerSq;
			float f = 0.25f + rSq*rSq - rSq;
			totalf += f;

			if(totalf > RENDERER_CLASS::threshold) return true;

			#ifdef SPEED_CHECK
			//aa++;
			influence_count++;
			#endif
		#if (DO_RADIUS_CHECK == 1)
		}
		#endif
	END_LOOP_OVER_NEARBY_PARTICLES()

	return false;

	#ifdef SPEED_CHECK
		IMP_PRINT("particle_count = %d, cache_count = %d, influence_count = %d, %f%%\n", particle_count, cache_count, influence_count, float(cache_count*100) / float(particleCount));
	#endif
}


// NOTE: still not faster than the scalar version, do not use

bool DOFASTCALL calcSignSIMD(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, PARTICLE_CACHE_CLASS* pCache)
{
	fltx4 position;
	SubFloat(position, 0) = x;
	SubFloat(position, 1) = y;
	SubFloat(position, 2) = z;
	SubFloat(position, 3) = 0.0f;


	fltx4 f4Total = Four_Zeros;
	fltx4 f4scalerSq = ReplicateX4( RENDERER_CLASS::scalerSq );
	fltx4 f4Fourpt25s = ReplicateX4( 0.25f );
	fltx4 f4Threshold = ReplicateX4( RENDERER_CLASS::threshold );

	START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, pCache, false)
		fltx4 f4pRSq = ReplicateX4( p->GetFieldRScaleSq() );
		fltx4 diff = SubSIMD(position, p->center.sse_vec);
		fltx4 diffSq = MulSIMD(diff, diff);
		fltx4 f4rSq = MulSIMD( f4pRSq, AddSIMD( AddSIMD( diffSq, RotateLeft( diffSq ) ), RotateLeft2( diffSq ) ) );

		//rSq *= RENDERER_CLASS::scalerSq;
		//float f = 0.25f + rSq*rSq - rSq;
		f4rSq = MulSIMD( SplatXSIMD( f4rSq ), f4scalerSq );
		fltx4 f4 = SubSIMD( AddSIMD( f4Fourpt25s, MulSIMD( f4rSq, f4rSq ) ), f4rSq );
		f4Total = AddSIMD( f4Total, f4 );

		bi32x4 f4Test = CmpGtSIMD( f4Total, f4Threshold );
		if ( IsAnyTrue( f4Test ) )
			return true;
	END_LOOP_OVER_NEARBY_PARTICLES()

	return false;
}


void DOFASTCALL calcSign2(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache)
{
	#ifdef SPEED_CHECK
		//tt++;
		int cache_count = 0;
		int influence_count = 0;
	#endif

	float totalf = 0.0f;

	START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, pCache, DO_BOUNDS_CHECK == 1)
		float *c = p->center.p;

		// This is the code without assembly
		float diff_x = x - c[0];
		float diff_y = y - c[1];
		float rSq = diff_x * diff_x;
		float diff_z = z - c[2];
		rSq += diff_y * diff_y;
		rSq += diff_z * diff_z;
		rSq *= p->GetFieldRScaleSq();  // TODO: The fieldRScaleSq should incorporate scalerSq (can compare rSq with one over sqrt(2) instead of cutoffRSq below)
		
		#if (DO_RADIUS_CHECK == 1)
		if(rSq <= RENDERER_CLASS::cutoffRSq)
		{
		#endif
			rSq *= RENDERER_CLASS::scalerSq;
			float f = 0.25f + rSq*rSq - rSq;
			totalf += f;

			if(totalf > RENDERER_CLASS::threshold) break; // Early out
		#if (DO_RADIUS_CHECK == 1)
		}
		#endif
	END_LOOP_OVER_NEARBY_PARTICLES()

	cornerInfo->value = totalf - RENDERER_CLASS::threshold;
	cornerInfo->y = iy;
	cornerInfo->z = iz;
}



/////////////////////////////////////////////////////
// User Defined Normal Calc Funcs
/////////////////////////////////////////////////////


void DOFASTCALL calcCornerNormal(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache)
{
	#ifdef SPEED_CHECK
		//tt++;
		int cache_count = 0;
		int influence_count = 0;
	#endif

	float nx = 0.0f, ny = 0.0f, nz = 0.0f;
	//float totalTemp = 0.0f;
	float totalf = 0.0f;

	START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, pCache, false)
		float *c = p->center.p;

		// This is the code without assembly
		float diff_x = x - c[0];
		float diff_y = y - c[1];
		float rSq = diff_x * diff_x;
		float diff_z = z - c[2];
		rSq += diff_y * diff_y;
		rSq += diff_z * diff_z;
		rSq *= p->GetFieldRScaleSq();  // TODO: The fieldRScaleSq should incorporate scalerSq (can compare rSq with one over sqrt(2) instead of cutoffRSq below)
		
		#if (DO_RADIUS_CHECK == 1)
		if(rSq <= RENDERER_CLASS::cutoffRSq)
		{	
		#endif
			rSq *= RENDERER_CLASS::scalerSq;
			float f = 0.25f + rSq*rSq - rSq;

			//Note that I should also difvide the vector (diff_x, diff_y, diff_z) by it's length
			//to get a unit vector, but this doesn't seem to make the result look any better

			nx += diff_x * f;
			ny += diff_y * f;
			nz += diff_z * f;

			//totalTemp += f * p->temp;
			totalf += f;
		#if (DO_RADIUS_CHECK == 1)
		}
		#endif
	END_LOOP_OVER_NEARBY_PARTICLES()


	cornerInfo->value = totalf - RENDERER_CLASS::threshold;

	// I doesn't make sense to normalize normals in the following
	// way because this doesn't make them length one.
	//float normalizer = totalf > 0.0f ? 1.0f/totalf : 0.0f;
	//cornerInfo->normal[0] = nx * normalizer;
	//cornerInfo->normal[1] = ny * normalizer;
	//cornerInfo->normal[2] = nz * normalizer;

	#if (NORMALIZE_CORNER_NORMALS==1)
		float normalizer = sqrtf(nx*nx + ny*ny + nz*nz);
		if(normalizer > 0.0f) normalizer = 1.0f/normalizer;
		cornerInfo->normal[0] = nx * normalizer;
		cornerInfo->normal[1] = ny * normalizer;
		cornerInfo->normal[2] = nz * normalizer;
	#else
		cornerInfo->normal[0] = nx;
		cornerInfo->normal[1] = ny;
		cornerInfo->normal[2] = nz;
	#endif
	
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[0]));
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[1]));
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[2]));

	cornerInfo->y = iy;
	cornerInfo->z = iz;
}


/*
// Produced Jaggy Lines:
min_dy = 1000.0f;
loop
	min_dy = min(abs(diff_y), min_dy);
end_loop
color = min_dy*0.25
*/


void DOFASTCALL calcCornerNormalHiFreqColor(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache)
{
	#ifdef SPEED_CHECK
		//tt++;
		int cache_count = 0;
		int influence_count = 0;
	#endif

	float max_dx = 0.0f, max_dy = 0.0f, max_dz = 0.0f;
	float nx = 0.0f, ny = 0.0f, nz = 0.0f;
	//float totalTemp = 0.0f;
	float totalf = 0.0f;

	START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, pCache, false)
		float *c = p->center.p;

		// This is the code without assembly
		float diff_x = x - c[0];
		float diff_y = y - c[1];
		float rSq = diff_x * diff_x;
		float diff_z = z - c[2];
		rSq += diff_y * diff_y;
		rSq += diff_z * diff_z;
		rSq *= p->GetFieldRScaleSq();  // TODO: The fieldRScaleSq should incorporate scalerSq (can compare rSq with one over sqrt(2) instead of cutoffRSq below)
		
		#if (DO_RADIUS_CHECK == 1)
		if(rSq <= RENDERER_CLASS::cutoffRSq)
		{	
		#endif
			rSq *= RENDERER_CLASS::scalerSq;
			float f = 0.25f + rSq*rSq - rSq;

			//Note that I should also difvide the vector (diff_x, diff_y, diff_z) by it's length
			//to get a unit vector, but this doesn't seem to make the result look any better

			max_dx = MAX(abs(diff_x*f), max_dx);
			max_dy = MAX(abs(diff_y*f), max_dy);
			max_dz = MAX(abs(diff_z*f), max_dz);

			nx += diff_x * f;
			ny += diff_y * f;
			nz += diff_z * f;

			//totalTemp += f * p->temp;
			totalf += f;
		#if (DO_RADIUS_CHECK == 1)
		}
		#endif
	END_LOOP_OVER_NEARBY_PARTICLES()

	cornerInfo->value = totalf - RENDERER_CLASS::threshold;

	// I doesn't make sense to normalize normals in the following
	// way because this doesn't make them length one.
	//float normalizer = totalf > 0.0f ? 1.0f/totalf : 0.0f;
	//cornerInfo->normal[0] = nx * normalizer;
	//cornerInfo->normal[1] = ny * normalizer;
	//cornerInfo->normal[2] = nz * normalizer;

	#if (NORMALIZE_CORNER_NORMALS==1)
		float normalizer = sqrtf(nx*nx + ny*ny + nz*nz);
		if(normalizer > 0.0f) normalizer = 1.0f/normalizer;
		cornerInfo->normal[0] = nx * normalizer;
		cornerInfo->normal[1] = ny * normalizer;
		cornerInfo->normal[2] = nz * normalizer;
	#else
		cornerInfo->normal[0] = nx;
		cornerInfo->normal[1] = ny;
		cornerInfo->normal[2] = nz;
	#endif
	
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[0]));
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[1]));
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[2]));

	cornerInfo->y = iy;
	cornerInfo->z = iz;

	// Normalizing the interpolants can be done here, but it seems better to
	// do it in the polygonization code once we've interpolated between vertices.
	// That is because if we normalize here, then the contributions from particles
	// to a vertex that doesn't have many neighbors will be weighted higher than
	// the contributions from a vertex with many particles. By normalizing in
	// the polygonization code, all contributions are weighted by f*vertexInterpolant
	// and then divided by the total of all f*vertexInterpolant terms which is equal
	// to threshold.
	
	((CornerInfoWithOneInterpolant*)cornerInfo)->interpolants1[0] = max_dx * 0.75f;
	((CornerInfoWithOneInterpolant*)cornerInfo)->interpolants1[1] = max_dy * 0.75f;
	((CornerInfoWithOneInterpolant*)cornerInfo)->interpolants1[2] = max_dz * 0.75f;

	//float normalizer = totalf > 0.0f ? 1.0f/totalf : 0.0f;
	//cornerInfo->interpolants1[0] = interpolants1[0]*normalizer;
	//cornerInfo->interpolants1[1] = interpolants1[1]*normalizer;
	//cornerInfo->interpolants1[2] = interpolants1[2]*normalizer;

	//corner_info_size = (corner_info_size + 1) % MAX_CORNERS;

	// TODO: Can try adding normalization for these, but would have to do it in polygonize function
	//return totalf;
	//return *((unsigned int*)(&totalf)) >> 31;
	//return (reinterpret_cast<unsigned int>(totalf)) >> 31;
}


void DOFASTCALL calcCornerNormalColor(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache)
{
	#ifdef SPEED_CHECK
		//tt++;
		int cache_count = 0;
		int influence_count = 0;
	#endif

	float nx = 0.0f, ny = 0.0f, nz = 0.0f;
	//float totalTemp = 0.0f;
	float totalf = 0.0f;
	float interpolants1[3] = {0.0f, 0.0f, 0.0f};

	START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, pCache, false)
		float *c = p->center.p;

		// This is the code without assembly
		float diff_x = x - c[0];
		float diff_y = y - c[1];
		float rSq = diff_x * diff_x;
		float diff_z = z - c[2];
		rSq += diff_y * diff_y;
		rSq += diff_z * diff_z;
		rSq *= p->GetFieldRScaleSq();  // TODO: The fieldRScaleSq should incorporate scalerSq (can compare rSq with one over sqrt(2) instead of cutoffRSq below)
		
		#if (DO_RADIUS_CHECK == 1)
		if(rSq <= RENDERER_CLASS::cutoffRSq)
		{	
		#endif
			rSq *= RENDERER_CLASS::scalerSq;
			float f = 0.25f + rSq*rSq - rSq;

			//Note that I should also difvide the vector (diff_x, diff_y, diff_z) by it's length
			//to get a unit vector, but this doesn't seem to make the result look any better

			nx += diff_x * f;
			ny += diff_y * f;
			nz += diff_z * f;

			//totalTemp += f * p->temp;
			interpolants1[0] += f * ((ImpParticleWithOneInterpolant*)p)->interpolants1[0];
			interpolants1[1] += f * ((ImpParticleWithOneInterpolant*)p)->interpolants1[1];
			interpolants1[2] += f * ((ImpParticleWithOneInterpolant*)p)->interpolants1[2];

			totalf += f;
		#if (DO_RADIUS_CHECK == 1)
		}
		#endif
	END_LOOP_OVER_NEARBY_PARTICLES()

	cornerInfo->value = totalf - RENDERER_CLASS::threshold;

	// I doesn't make sense to normalize normals in the following
	// way because this doesn't make them length one.
	//float normalizer = totalf > 0.0f ? 1.0f/totalf : 0.0f;
	//cornerInfo->normal[0] = nx * normalizer;
	//cornerInfo->normal[1] = ny * normalizer;
	//cornerInfo->normal[2] = nz * normalizer;

	#if (NORMALIZE_CORNER_NORMALS==1)
		float normalizer = sqrtf(nx*nx + ny*ny + nz*nz);
		if(normalizer > 0.0f) normalizer = 1.0f/normalizer;
		cornerInfo->normal[0] = nx * normalizer;
		cornerInfo->normal[1] = ny * normalizer;
		cornerInfo->normal[2] = nz * normalizer;
	#else
		cornerInfo->normal[0] = nx;
		cornerInfo->normal[1] = ny;
		cornerInfo->normal[2] = nz;
	#endif
	
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[0]));
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[1]));
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[2]));

	cornerInfo->y = iy;
	cornerInfo->z = iz;

	// Normalizing the interpolants can be done here, but it seems better to
	// do it in the polygonization code once we've interpolated between vertices.
	// That is because if we normalize here, then the contributions from particles
	// to a vertex that doesn't have many neighbors will be weighted higher than
	// the contributions from a vertex with many particles. By normalizing in
	// the polygonization code, all contributions are weighted by f*vertexInterpolant
	// and then divided by the total of all f*vertexInterpolant terms which is equal
	// to threshold.
	
	((CornerInfoWithOneInterpolant*)cornerInfo)->interpolants1[0] = interpolants1[0];
	((CornerInfoWithOneInterpolant*)cornerInfo)->interpolants1[1] = interpolants1[1];
	((CornerInfoWithOneInterpolant*)cornerInfo)->interpolants1[2] = interpolants1[2];

	//float normalizer = totalf > 0.0f ? 1.0f/totalf : 0.0f;
	//cornerInfo->interpolants1[0] = interpolants1[0]*normalizer;
	//cornerInfo->interpolants1[1] = interpolants1[1]*normalizer;
	//cornerInfo->interpolants1[2] = interpolants1[2]*normalizer;

	//corner_info_size = (corner_info_size + 1) % MAX_CORNERS;

	// TODO: Can try adding normalization for these, but would have to do it in polygonize function
	//return totalf;
	//return *((unsigned int*)(&totalf)) >> 31;
	//return (reinterpret_cast<unsigned int>(totalf)) >> 31;
}

void DOFASTCALL calcCornerNormalColorTanNoUV(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache)
{
	#ifdef SPEED_CHECK
		//tt++;
		int cache_count = 0;
		int influence_count = 0;
	#endif

	float totalf = 0.0f;
	fltx4 normal = Four_Zeros;
	fltx4 interpolants1 = Four_Zeros;
	fltx4 interpolants2 = Four_Zeros;
	fltx4 position;
	SubFloat(position, 0) = x;
	SubFloat(position, 1) = y;
	SubFloat(position, 2) = z;
	SubFloat(position, 3) = 0.0f;

	START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, pCache, false)
		fltx4 diff = SubSIMD(position, p->center.sse_vec);
		fltx4 diffSq = MulSIMD(diff, diff);
		// TODO: Can do this with mov,shuf,add
		
		float rSq = SubFloat(diffSq, 0)+SubFloat(diffSq, 1)+SubFloat(diffSq, 2); //Can this be done in SSE?
		rSq *= p->GetFieldRScaleSq();  // TODO: The fieldRScaleSq should incorporate scalerSq (can compare rSq with one over sqrt(2) instead of cutoffRSq below)

		#if (DO_RADIUS_CHECK == 1)
		if(rSq <= RENDERER_CLASS::cutoffRSq)
		{	
		#endif
			rSq *= RENDERER_CLASS::scalerSq;
			float f = 0.25f + rSq*rSq - rSq;
			totalf += f;

			//Note that I should also divide the vector (diff_x, diff_y, diff_z) by it's length
			//to get a unit vector, but this doesn't seem to make the result look any better

			fltx4 f4 = ReplicateX4(f);
			normal = AddSIMD(normal, MulSIMD(diff, f4));
			interpolants1 = AddSIMD(interpolants1, MulSIMD(((ImpParticleWithTwoInterpolants*)p)->interpolants1.sse_vec,f4));
			interpolants2 = AddSIMD(interpolants2, MulSIMD(((ImpParticleWithTwoInterpolants*)p)->interpolants2.sse_vec,f4));
		#if (DO_RADIUS_CHECK == 1)
		}
		#endif
	END_LOOP_OVER_NEARBY_PARTICLES()

	// TODO: I can actually store value as part of normal.
	// (nx, ny, nz, value)... It would be relatively easy
	// to do the calculations this way. I would just have to initialize
	// normal to (0,0,0,-threshold), and when I calculate distance,
	// always set postion to (x,y,z,1) so that we always get 1*f into
	// the value field.
	cornerInfo->value = totalf - RENDERER_CLASS::threshold;

	// I doesn't make sense to normalize normals in the following
	// way because this doesn't make them length one.
	//float normalizer = totalf > 0.0f ? 1.0f/totalf : 0.0f;
	//cornerInfo->normal[0] = nx * normalizer;
	//cornerInfo->normal[1] = ny * normalizer;
	//cornerInfo->normal[2] = nz * normalizer;

	cornerInfo->normal[0] = SubFloat(normal, 0);
	cornerInfo->normal[1] = SubFloat(normal, 1);
	cornerInfo->normal[2] = SubFloat(normal, 2);
	
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[0]));
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[1]));
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[2]));

	cornerInfo->y = iy;
	cornerInfo->z = iz;

	// Normalizing the interpolants can be done here, but it seems better to
	// do it in the polygonization code once we've interpolated between vertices.
	// That is because if we normalize here, then the contributions from particles
	// to a vertex that doesn't have many neighbors will be weighted higher than
	// the contributions from a vertex with many particles. By normalizing in
	// the polygonization code, all contributions are weighted by f*vertexInterpolant
	// and then divided by the total of all f*vertexInterpolant terms which is equal
	// to threshold.
	
	
	//cornerInfo->interpolants1[0] = interpolants1.m128_f32[0];
	//cornerInfo->interpolants1[1] = interpolants1.m128_f32[1];
	//cornerInfo->interpolants1[2] = interpolants1.m128_f32[2];

	((CornerInfoWithTwoInterpolants*)cornerInfo)->interpolants1.sse_vec = interpolants1;
	((CornerInfoWithTwoInterpolants*)cornerInfo)->interpolants2.sse_vec = interpolants2;
}


void DOFASTCALL calcCornerNormalColorUVTan(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache)
{
	#ifdef SPEED_CHECK
		//tt++;
		int cache_count = 0;
		int influence_count = 0;
	#endif

	//float totalf = 0.0f;
	fltx4 normal = Four_Zeros;
	fltx4 interpolants1 = Four_Zeros;
	fltx4 interpolants2 = Four_Zeros;
	fltx4 interpolants3 = Four_Zeros;
	fltx4 interpolants4 = Four_Zeros;
	fltx4 position;
	SubFloat(position, 0) = x;
	SubFloat(position, 1) = y;
	SubFloat(position, 2) = z;
	SubFloat(position, 3) = 1.0f; // This way, I can calculate TotalF as part of the normal calculation.

	START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, pCache, false)
		fltx4 diff = SubSIMD(position, p->center.sse_vec);
		fltx4 diffSq = MulSIMD(diff, diff);
		// TODO: Can do this with mov,shuf,add
		
		float rSq = SubFloat(diffSq, 0)+SubFloat(diffSq, 1)+SubFloat(diffSq, 2); //Can this be done in SSE?
		rSq *= p->GetFieldRScaleSq(); // TODO: The fieldRScaleSq should incorporate scalerSq (can compare rSq with one over sqrt(2) instead of cutoffRSq below)

		#if (DO_RADIUS_CHECK == 1)
		if(rSq <= RENDERER_CLASS::cutoffRSq)
		{	
		#endif
			rSq *= RENDERER_CLASS::scalerSq;
			float f = 0.25f + rSq*rSq - rSq;
			//totalf += f;

			//Note that I should also divide the vector (diff_x, diff_y, diff_z) by it's length
			//to get a unit vector, but this doesn't seem to make the result look any better

			fltx4 f4 = ReplicateX4(f);
			normal = AddSIMD(normal, MulSIMD(diff, f4));
			interpolants1 = AddSIMD(interpolants1, MulSIMD(((ImpParticleWithFourInterpolants*)p)->interpolants1.sse_vec,f4));
			interpolants2 = AddSIMD(interpolants2, MulSIMD(((ImpParticleWithFourInterpolants*)p)->interpolants2.sse_vec,f4));
			interpolants3 = AddSIMD(interpolants3, MulSIMD(((ImpParticleWithFourInterpolants*)p)->interpolants3.sse_vec,f4));
			interpolants4 = AddSIMD(interpolants4, MulSIMD(((ImpParticleWithFourInterpolants*)p)->interpolants4.sse_vec,f4));
		#if (DO_RADIUS_CHECK == 1)
		}
		#endif
	END_LOOP_OVER_NEARBY_PARTICLES()

	// TODO: I can actually store value as part of normal.
	// (nx, ny, nz, value)... It would be relatively easy
	// to do the calculations this way. I would just have to initialize
	// normal to (0,0,0,-threshold), and when I calculate distance,
	// always set postion to (x,y,z,1) so that we always get 1*f into
	// the value field.
	// I did just that, except I initialize the normal to all zeros and
	// subtract out the threshold here for now. If we actually use the
	// normal to store the value, then I will change the threshold to be
	// subtracted out at the beginning. This assumes that particles have
	// their position[3] value all set to 0.
	// cornerInfo->value = totalf - RENDERER_CLASS::threshold;
	cornerInfo->value = SubFloat(normal, 3) - RENDERER_CLASS::threshold;

	// I doesn't make sense to normalize normals in the following
	// way because this doesn't make them length one.
	//float normalizer = totalf > 0.0f ? 1.0f/totalf : 0.0f;
	//cornerInfo->normal[0] = nx * normalizer;
	//cornerInfo->normal[1] = ny * normalizer;
	//cornerInfo->normal[2] = nz * normalizer;

	// TODO: In the SSE version, I should store the normal as a point3d.
	// Also, I can use the fourth location to store the field value.
	cornerInfo->normal[0] = SubFloat(normal, 0);
	cornerInfo->normal[1] = SubFloat(normal, 1);
	cornerInfo->normal[2] = SubFloat(normal, 2);
	
	// TODO: I should replace this with a single function that
	// simultaneously clears three vertices
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[0]));
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[1]));
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[2]));

	cornerInfo->y = iy;
	cornerInfo->z = iz;

	// Normalizing the interpolants can be done here, but it seems better to
	// do it in the polygonization code once we've interpolated between vertices.
	// That is because if we normalize here, then the contributions from particles
	// to a vertex that doesn't have many neighbors will be weighted higher than
	// the contributions from a vertex with many particles. By normalizing in
	// the polygonization code, all contributions are weighted by f*vertexInterpolant
	// and then divided by the total of all f*vertexInterpolant terms which is equal
	// to threshold.
	
	
	//cornerInfo->interpolants1[0] = interpolants1.m128_f32[0];
	//cornerInfo->interpolants1[1] = interpolants1.m128_f32[1];
	//cornerInfo->interpolants1[2] = interpolants1.m128_f32[2];


	((CornerInfoWithFourInterpolants*)cornerInfo)->interpolants1.sse_vec = interpolants1;
	((CornerInfoWithFourInterpolants*)cornerInfo)->interpolants2.sse_vec = interpolants2;
	((CornerInfoWithFourInterpolants*)cornerInfo)->interpolants3.sse_vec = interpolants3;
	((CornerInfoWithFourInterpolants*)cornerInfo)->interpolants4.sse_vec = interpolants4;
}

// Takes UVW inputs (U and V are regular tex coords, W is position along tentacle)
// Also takes three basis vectors. Outputs data needed for the calcVertexNormalColorUVTanForBlob function.
void DOFASTCALL calcCornerNormalColorUVTanForBlob(const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, const  float x, const float y, const float z, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache)
{
	#ifdef SPEED_CHECK
		//tt++;
		int cache_count = 0;
		int influence_count = 0;
	#endif

	//float totalf = 0.0f;
	fltx4 normal = Four_Zeros;
	fltx4 interpolants1 = Four_Zeros;
	fltx4 interpolants2 = Four_Zeros;
	fltx4 interpolants3 = Four_Zeros;
	fltx4 interpolants4 = Four_Zeros;
	fltx4 position;
	SubFloat(position, 0) = x;
	SubFloat(position, 1) = y;
	SubFloat(position, 2) = z;
	SubFloat(position, 3) = 1.0f; // This way, I can calculate TotalF as part of the normal calculation.

	START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, pCache, false)
		fltx4 diff = SubSIMD(position, p->center.sse_vec);
		fltx4 diffSq = MulSIMD(diff, diff);
		// TODO: Can do this with mov,shuf,add

		float rSq = SubFloat(diffSq, 0)+SubFloat(diffSq, 1)+SubFloat(diffSq, 2); //Can this be done in SSE?
		rSq *= p->GetFieldRScaleSq(); // TODO: The fieldRScaleSq should incorporate scalerSq (can compare rSq with one over sqrt(2) instead of cutoffRSq below)

		#if (DO_RADIUS_CHECK == 1)
		if(rSq <= RENDERER_CLASS::cutoffRSq)
		{	
		#endif
			rSq *= RENDERER_CLASS::scalerSq;
			float f = 0.25f + rSq*rSq - rSq;
			//totalf += f;

			//Note that I should also divide the vector (diff_x, diff_y, diff_z) by it's length
			//to get a unit vector, but this doesn't seem to make the result look any better

			fltx4 f4 = ReplicateX4(f);
			normal = AddSIMD(normal, MulSIMD(diff, f4));
			interpolants1 = AddSIMD(interpolants1, MulSIMD(((ImpParticleWithFourInterpolants*)p)->interpolants1.sse_vec,f4));
			interpolants2 = AddSIMD(interpolants2, MulSIMD(((ImpParticleWithFourInterpolants*)p)->interpolants2.sse_vec,f4));
			interpolants3 = AddSIMD(interpolants3, MulSIMD(((ImpParticleWithFourInterpolants*)p)->interpolants3.sse_vec,f4));
			interpolants4 = AddSIMD(interpolants4, MulSIMD(((ImpParticleWithFourInterpolants*)p)->interpolants4.sse_vec,f4));
		#if (DO_RADIUS_CHECK == 1)
		}
		#endif
	END_LOOP_OVER_NEARBY_PARTICLES()

	// TODO: I can actually store value as part of normal.
	// (nx, ny, nz, value)... It would be relatively easy
	// to do the calculations this way. I would just have to initialize
	// normal to (0,0,0,-threshold), and when I calculate distance,
	// always set postion to (x,y,z,1) so that we always get 1*f into
	// the value field.
	// I did just that, except I initialize the normal to all zeros and
	// subtract out the threshold here for now. If we actually use the
	// normal to store the value, then I will change the threshold to be
	// subtracted out at the beginning. This assumes that particles have
	// their position[3] value all set to 0.
	// cornerInfo->value = totalf - RENDERER_CLASS::threshold;
	cornerInfo->value = SubFloat(normal, 3) - RENDERER_CLASS::threshold;

	// I doesn't make sense to normalize normals in the following
	// way because this doesn't make them length one.
	//float normalizer = totalf > 0.0f ? 1.0f/totalf : 0.0f;
	//cornerInfo->normal[0] = nx * normalizer;
	//cornerInfo->normal[1] = ny * normalizer;
	//cornerInfo->normal[2] = nz * normalizer;

	// TODO: In the SSE version, I should store the normal as a point3d.
	// Also, I can use the fourth location to store the field value.
	cornerInfo->normal[0] = SubFloat(normal, 0);
	cornerInfo->normal[1] = SubFloat(normal, 1);
	cornerInfo->normal[2] = SubFloat(normal, 2);

	// TODO: I should replace this with a single function that
	// simultaneously clears three vertices
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[0]));
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[1]));
	VERTEX_BUFFER_CLASS::clearId(&(cornerInfo->edges[2]));

	cornerInfo->y = iy;
	cornerInfo->z = iz;

	// Normalizing the interpolants can be done here, but it seems better to
	// do it in the polygonization code once we've interpolated between vertices.
	// That is because if we normalize here, then the contributions from particles
	// to a vertex that doesn't have many neighbors will be weighted higher than
	// the contributions from a vertex with many particles. By normalizing in
	// the polygonization code, all contributions are weighted by f*vertexInterpolant
	// and then divided by the total of all f*vertexInterpolant terms which is equal
	// to threshold.


	//cornerInfo->interpolants1[0] = interpolants1.m128_f32[0];
	//cornerInfo->interpolants1[1] = interpolants1.m128_f32[1];
	//cornerInfo->interpolants1[2] = interpolants1.m128_f32[2];


	((CornerInfoWithFourInterpolants*)cornerInfo)->interpolants1.sse_vec = interpolants1;
	((CornerInfoWithFourInterpolants*)cornerInfo)->interpolants2.sse_vec = interpolants2;
	((CornerInfoWithFourInterpolants*)cornerInfo)->interpolants3.sse_vec = interpolants3;
	((CornerInfoWithFourInterpolants*)cornerInfo)->interpolants4.sse_vec = interpolants4;
}



// Old crap:
#if 0


	//float normalizer = totalf > 0.0f ? 1.0f/totalf : 0.0f;
	//cornerInfo->interpolants1[0] = interpolants1[0]*normalizer;
	//cornerInfo->interpolants1[1] = interpolants1[1]*normalizer;
	//cornerInfo->interpolants1[2] = interpolants1[2]*normalizer;
	


	//corner_info_size = (corner_info_size + 1) % MAX_CORNERS;

	// TODO: Can try adding normalization for these, but would have to do it in polygonize function
	//return totalf;
	//return *((unsigned int*)(&totalf)) >> 31;
	//return (reinterpret_cast<unsigned int>(totalf)) >> 31;

	//#elif (BLR_CODE==BLR_X86ASM)
	#if(BLR_CODE==BLR_X86ASM)
		float nx = 0.0f, ny = 0.0f, nz = 0.0f;
		//float totalTemp = 0.0f;
		float totalf = 0.0f;
		float interpolants1[3] = {0.0f, 0.0f, 0.0f};

		// TODO: This is the if-based loop ... borrowed from other code.....
		// TODO: I can get rid of if statement by using FCMOV (conditional move... only valid on pentium pro and above)
		// Basically, if the radius is greater than the cutoff, can use FCMOV to set it to the cutoff at which the
		// result will be 0
		// Code was rearranged to avoid stalls... This is the fastest loop!!!
		__asm
		{
			
			fld totalf;
			mov ecx, bit;

			mov ebx, dword ptr [ecx]
			test ebx, ebx;
			je ndone;
			
			// Can use rotation... maybe that can make this stuff faster?	
	nloop:	//add ebx, 8;                 Add the offset to center from start of particle object (no more offset)
			fld x;						Load x into st(0)      (totalf, x)
			fld y;						Load y into st(0)      (totalf, x, y)
			fxch st(1);                 Exchange-helps later   (totalf, y, x)
			fld z;						Load z into st(0)      (totalf, y, x, z)
			fxch st(1);                 Exchange               (totalf, y, z, x)
			fsub dword ptr[ebx];		Subtract centerx       (totalf, y, z, dx)
			fxch st(2);                 Exchange               (totalf, dx, z, y)
			fsub dword ptr[ebx + 4];	Subtract centery       (totalf, dx, z, dy)
			fxch st(1);                 Exchange               (totalf, dx, dy, z)
			fsub dword ptr[ebx + 8];	Subtract centerz       (totalf, dx, dy, dz)
			fld st(2);					Duplicate dx           (totalf, dx, dy, dz, dx)
			fmul st(0), st(3);			Square dx              (totalf, dx, dy, dz, dx^2)
			fld st(2);                  Duplicate dy           (totalf, dx, dy, dz, dx^2, dy)
			fmul st(0), st(3);          Square dy              (totalf, dx, dy, dz, dx^2, dy^2)
			fld st(2);                  Duplicate dz           (totalf, dx, dy, dz, dx^2, dy^2, dz)
			fmul st(0), st(3);          Square dz              (totalf, dx, dy, dz, dx^2, dy^2, dz^2)
			fxch st(2);                 Swap dz^2 with dx^2    (totalf, dx, dy, dz, dz^2, dy^2, dx^2)
			faddp st(1), st(0);			Add dx^2 and dy^2      (totalf, dx, dy, dz, dz^2, dx^2+dy^2)
			faddp st(1), st(0);			Add dy^2 and dz^2      (totalf, dx, dy, dz, dx^2+dy^2+dz^2)

			//fcom dword ptr [cutoff];
			//fcom lcutoff;
			//fnstsw ax;
			//test ah,41h;
			//je skip;
			
			fcom cutoffRSq;				compare ST(0) with the value of the real8_var variable
			fstsw ax          ;copy the Status Word containing the result to AX
			//mov ecx, [ecx+4];  this doesn't make a huge difference, but you can just stick the increment into the delay slot here
			add ecx, 4;
			fwait             ;insure the previous instruction is completed
			sahf              ;transfer the condition codes to the CPUs flag register
			ja [abovethresh];  Make this a jump down since this will help the branch predictor to know not to take this jump most of the time
			//add aa, 1;
			fmul scalerSq;
			fld st(0);
			fmul st(0), st(0);
			fsubrp st(1), st(0);
			fadd addAmt;
			fmul st(1), st(0);			(totalf, dx, dy, dz*f, f)
			fmul st(2), st(0);			(totalf, dx, dy*f, dz*f, f)
			fmul st(3), st(0);			(totalf, dx*f, dy*f, dz*f, f)
			fadd st(4), st(0);			(totalf+f, dx*f, dy*f, dz*f, f)
			fmul dword ptr[ebx + 12];   (totalf+f, dx*f, dy*f, dz*f, temp*f)
			mov ebx, dword ptr [ecx];					Load the location of c
			fadd totalTemp;             (totalf+f, dx*f, dy*f, dz*f, temp*f + totalTemp)
			fstp totalTemp;             (totalf+f, dx*f, dy*f, dz*f)
			test ebx, ebx;
			fadd nz;
			fstp nz;
			fadd ny;
			fstp ny;
			fadd nx;
			fstp nx
			jne [nloop];
			jmp [ndone];

	abovethresh:
			//add bb, 1;
			mov ebx, dword ptr [ecx];	
			test ebx, ebx;
			fstp st;
			fstp st;
			fstp st;
			fstp st;
			jne [nloop];
	ndone:
			fstp totalf;

		}
	#endif

	#ifdef SPEED_CHECK
		IMP_PRINT("particle_count = %d, cache_count = %d, influence_count = %d, %f%%\n", particle_count, cache_count, influence_count, float(cache_count*100) / float(particleCount));
	#endif


/*
	cube->cornerInfoNo = corner_info_size;
	CornerInfo* cornerInfo = &(corner_info[corner_info_size]);

	cornerInfo->value = totalf - threshold;

	// I doesn't make sense to normalize normals in the following
	// way because this doesn't make them length one.
	//float normalizer = totalf > 0.0f ? 1.0f/totalf : 0.0f;
	//cornerInfo->normal[0] = nx * normalizer;
	//cornerInfo->normal[1] = ny * normalizer;
	//cornerInfo->normal[2] = nz * normalizer;

	#if (NORMALIZE_CORNER_NORMALS==1)
		float normalizer = sqrtf(nx*nx + ny*ny + nz*nz);
		if(normalizer > 0.0f) normalizer = 1.0f/normalizer;
		cornerInfo->normal[0] = nx * normalizer;
		cornerInfo->normal[1] = ny * normalizer;
		cornerInfo->normal[2] = nz * normalizer;
	#else
		#if (BLR_CODE==BLR_SIMD)
			cornerInfo->normal[0] = normal.m128_f32[0];
			cornerInfo->normal[1] = normal.m128_f32[1];
			cornerInfo->normal[2] = normal.m128_f32[2];
		#else
			cornerInfo->normal[0] = nx;
			cornerInfo->normal[1] = ny;
			cornerInfo->normal[2] = nz;
		#endif
	#endif
	

	#if (DRAW_MODE==DRAW_VALVE_INDEX || DRAW_MODE==DRAW_VALVE_INDEX_TRIS)
		cornerInfo->edges[0].time = 0;
		cornerInfo->edges[1].time = 0;
		cornerInfo->edges[2].time = 0;
	#else
		cornerInfo->edges[0] = USHORT_MAX;
		cornerInfo->edges[1] = USHORT_MAX;
		cornerInfo->edges[2] = USHORT_MAX;
	#endif

	// Normalizing the interpolants can be done here, but it seems better to
	// do it in the polygonization code once we've interpolated between vertices.
	// That is because if we normalize here, then the contributions from particles
	// to a vertex that doesn't have many neighbors will be weighted higher than
	// the contributions from a vertex with many particles. By normalizing in
	// the polygonization code, all contributions are weighted by f*vertexInterpolant
	// and then divided by the total of all f*vertexInterpolant terms which is equal
	// to threshold.
	
	#if (BLR_CODE==BLR_SIMD)
		cornerInfo->interpolants1[0] = interpolants1.m128_f32[0];
		cornerInfo->interpolants1[1] = interpolants1.m128_f32[1];
		cornerInfo->interpolants1[2] = interpolants1.m128_f32[2];
	#else
		cornerInfo->interpolants1[0] = interpolants1[0];
		cornerInfo->interpolants1[1] = interpolants1[1];
		cornerInfo->interpolants1[2] = interpolants1[2];
	#endif


	//float normalizer = totalf > 0.0f ? 1.0f/totalf : 0.0f;
	//cornerInfo->interpolants1[0] = interpolants1[0]*normalizer;
	//cornerInfo->interpolants1[1] = interpolants1[1]*normalizer;
	//cornerInfo->interpolants1[2] = interpolants1[2]*normalizer;
	


	corner_info_size = (corner_info_size + 1) % MAX_CORNERS;

	// TODO: Can try adding normalization for these, but would have to do it in polygonize function
	//return totalf;
	//return *((unsigned int*)(&totalf)) >> 31;
	//return (reinterpret_cast<unsigned int>(totalf)) >> 31;
	*/
}

#endif




/////////////////////////////////////////////////////
// User Defined Vertex Calc Funcs
/////////////////////////////////////////////////////



// Calculates vertex position, and normal
void DOFASTCALL calcVertexNormal(const  float x, const float y, const float z, const int edge, const CornerInfo* corner1, const CornerInfo* corner2, VERTEX_BUFFER_CLASS* vertexBuffer)
{
	const float loval = corner1->value;
	const float hival = corner2->value;
	float percent = loval / (loval - hival);
	float omPercent = 1.0f - percent;

	//float vertex[3];
	Point3D position;
	position[0] = x;
	position[1] = y;
	position[2] = z;
	position[edge] += RENDERER_CLASS::cubewidth * percent;
	vertexBuffer->setVertexPosition(position);

	// Idea: I can calculate normals and colors at the vertex postion
	// rather than trying to interpolate. This might yield much nicer
	// normals and colors. It would also save storage for normals and
	// interpolants.
	const float *loNormal = corner1->normal;
	const float *hiNormal = corner2->normal;
	//float normal[3];
	Point3D normal;
	normal[0] = loNormal[0] * omPercent + hiNormal[0] * percent;
	normal[1] = loNormal[1] * omPercent + hiNormal[1] * percent;
	normal[2] = loNormal[2] * omPercent + hiNormal[2] * percent;
	vertexBuffer->setVertexNormal(normal);
}

// Calculates vertex position, and normal, outputs the debug color
void DOFASTCALL calcVertexNormalDebugColor(const  float x, const float y, const float z, const int edge, const CornerInfo* corner1, const CornerInfo* corner2, VERTEX_BUFFER_CLASS* vertexBuffer)
{
	const float loval = corner1->value;
	const float hival = corner2->value;
	float percent = loval / (loval - hival);
	float omPercent = 1.0f - percent;

	//float vertex[3];
	Point3D position;
	position[0] = x;
	position[1] = y;
	position[2] = z;
	position[edge] += RENDERER_CLASS::cubewidth * percent;
	vertexBuffer->setVertexPosition(position);

	// Idea: I can calculate normals and colors at the vertex postion
	// rather than trying to interpolate. This might yield much nicer
	// normals and colors. It would also save storage for normals and
	// interpolants.
	const float *loNormal = corner1->normal;
	const float *hiNormal = corner2->normal;
	//float normal[3];
	Point3D normal;
	normal[0] = loNormal[0] * omPercent + hiNormal[0] * percent;
	normal[1] = loNormal[1] * omPercent + hiNormal[1] * percent;
	normal[2] = loNormal[2] * omPercent + hiNormal[2] * percent;
	// FIXME: this is producing non-unit normals, so I'm inserting a normalize in here:
	normal.normalize();
	vertexBuffer->setVertexNormal(normal);

	vertexBuffer->setVertexColor(tile_debug_color);
}


// Calculates vertex position, normal, and normalized color
void DOFASTCALL calcVertexNormalNColor(const  float x, const float y, const float z, const int edge, const CornerInfo* corner1, const CornerInfo* corner2, VERTEX_BUFFER_CLASS* vertexBuffer)
{
	const float loval = corner1->value;
	const float hival = corner2->value;
	float percent = loval / (loval - hival);
	float omPercent = 1.0f - percent;

	//float vertex[3];
	Point3D position;
	position[0] = x;
	position[1] = y;
	position[2] = z;
	position[edge] += RENDERER_CLASS::cubewidth * percent;
	vertexBuffer->setVertexPosition(position);
	
	//s_pMeshBuilder->Position3fv(vertex);

	// Idea: I can calculate normals and colors at the vertex postion
	// rather than trying to interpolate. This might yield much nicer
	// normals and colors. It would also save storage for normals and
	// interpolants.

	const float *loNormal = corner1->normal;
	const float *hiNormal = corner2->normal;
	//float normal[3];
	Point3D normal;
	normal[0] = loNormal[0] * omPercent + hiNormal[0] * percent;
	normal[1] = loNormal[1] * omPercent + hiNormal[1] * percent;
	normal[2] = loNormal[2] * omPercent + hiNormal[2] * percent;
	vertexBuffer->setVertexNormal(normal);

	//s_pMeshBuilder->Normal3fv(normal.p); // TODO: Might be faster to pass in normals as params into this function

	// If you do the math, it turns out that the interpolated field from
	// the two contributing points is equal to threshold as it should be.
	// (The interpolated value is equal to 0)
	// Thus, the normalizer is 1.0f/threshold
	/*
	const float loTotalf = loval+threshold;
	const float hiTotalf = hival+threshold;
	float totalf = loTotalf * omPercent + hiTotalf * percent;
	float normalizer = totalf > 0.0f ? 1.0f/totalf : 0.0f;

	float color[3];
	color[0] = corner1->interpolants1[0]*omPercent + corner2->interpolants1[0]*percent;
	color[1] = corner1->interpolants1[1]*omPercent + corner2->interpolants1[1]*percent;
	color[2] = corner1->interpolants1[2]*omPercent + corner2->interpolants1[2]*percent;

	color[0]*= normalizer;
	color[1]*= normalizer;
	color[2]*= normalizer;
	*/
	
	// This is the optimized version of the code above
	float normalizer = RENDERER_CLASS::oneOverThreshold;
	omPercent *= normalizer; percent *= normalizer;
	//float color[3];
	Point3D color;
	color[0] = ((CornerInfoWithOneInterpolant*)corner1)->interpolants1[0]*omPercent + ((CornerInfoWithOneInterpolant*)corner2)->interpolants1[0]*percent;
	color[1] = ((CornerInfoWithOneInterpolant*)corner1)->interpolants1[1]*omPercent + ((CornerInfoWithOneInterpolant*)corner2)->interpolants1[1]*percent;
	color[2] = ((CornerInfoWithOneInterpolant*)corner1)->interpolants1[2]*omPercent + ((CornerInfoWithOneInterpolant*)corner2)->interpolants1[2]*percent;
	vertexBuffer->setVertexColor(color);
}

// Calculates vertex position, normal, normalized color, UV and Tangent
void DOFASTCALL calcVertexNormalNColorTanNoUV(const  float x, const float y, const float z, const int edge, const CornerInfo* _corner1, const CornerInfo* _corner2, VERTEX_BUFFER_CLASS* vertexBuffer)
{
	const CornerInfoWithTwoInterpolants* corner1 = (CornerInfoWithTwoInterpolants*)_corner1;
	const CornerInfoWithTwoInterpolants* corner2 = (CornerInfoWithTwoInterpolants*)_corner2;

	const float loval = corner1->value;
	const float hival = corner2->value;
	float percent = loval / (loval - hival);
	float omPercent = 1.0f - percent;

	//float vertex[3];
	Point3D position;
	position[0] = x;
	position[1] = y;
	position[2] = z;
	position[edge] += RENDERER_CLASS::cubewidth * percent;
	vertexBuffer->setVertexPosition(position);
	
	//s_pMeshBuilder->Position3fv(vertex);

	// Idea: I can calculate normals and colors at the vertex postion
	// rather than trying to interpolate. This might yield much nicer
	// normals and colors. It would also save storage for normals and
	// interpolants.

	const float *loNormal = corner1->normal;
	const float *hiNormal = corner2->normal;
	//float normal[3];
	Point3D normal;
	normal[0] = loNormal[0] * omPercent + hiNormal[0] * percent;
	normal[1] = loNormal[1] * omPercent + hiNormal[1] * percent;
	normal[2] = loNormal[2] * omPercent + hiNormal[2] * percent;
	vertexBuffer->setVertexNormal(normal);

	//s_pMeshBuilder->Normal3fv(normal.p); // TODO: Might be faster to pass in normals as params into this function

	// If you do the math, it turns out that the interpolated field from
	// the two contributing points is equal to threshold as it should be.
	// (The interpolated value is equal to 0)
	// Thus, the normalizer is 1.0f/threshold
	/*
	const float loTotalf = loval+threshold;
	const float hiTotalf = hival+threshold;
	float totalf = loTotalf * omPercent + hiTotalf * percent;
	float normalizer = totalf > 0.0f ? 1.0f/totalf : 0.0f;

	float color[3];
	color[0] = corner1->interpolants1[0]*omPercent + corner2->interpolants1[0]*percent;
	color[1] = corner1->interpolants1[1]*omPercent + corner2->interpolants1[1]*percent;
	color[2] = corner1->interpolants1[2]*omPercent + corner2->interpolants1[2]*percent;

	color[0]*= normalizer;
	color[1]*= normalizer;
	color[2]*= normalizer;
	*/
	
	// This is the optimized version of the code above
	float normalizer = RENDERER_CLASS::oneOverThreshold;
	omPercent *= normalizer; percent *= normalizer;
	//float color[3];
	Point3D color;
	color[0] = corner1->interpolants1[0]*omPercent + corner2->interpolants1[0]*percent;
	color[1] = corner1->interpolants1[1]*omPercent + corner2->interpolants1[1]*percent;
	color[2] = corner1->interpolants1[2]*omPercent + corner2->interpolants1[2]*percent;
	vertexBuffer->setVertexColor(color);

	#if 0
	float v = corner1->interpolants1[3]*omPercent + corner2->interpolants1[3]*percent;
	#endif

	fltx4 mm_omPercent = ReplicateX4(omPercent);
	fltx4 mm_percent = ReplicateX4(percent);
	Point3D interpolants2; interpolants2.sse_vec = AddSIMD(MulSIMD(corner1->interpolants2.sse_vec, mm_omPercent), MulSIMD(corner2->interpolants2.sse_vec, mm_percent));

	/*
	interpolants2.sse_vec = MulSIMD(interpolants2.sse_vec, normal.sse_vec);
	interpolants3.sse_vec = MulSIMD(interpolants3.sse_vec, normal.sse_vec);
	interpolants4.sse_vec = MulSIMD(interpolants4.sse_vec, normal.sse_vec);

	float a = interpolants2.m128_f32[0]+interpolants2.m128_f32[1]+interpolants2.m128_f32[2];
	float b = interpolants3.m128_f32[0]+interpolants3.m128_f32[1]+interpolants3.m128_f32[2];
	float c = interpolants4.m128_f32[0]+interpolants4.m128_f32[1]+interpolants4.m128_f32[2];
	*/

	/* Perturb normals
	normal[0] += 0.2f * sin(5.0f * a * PI);
	normal[1] += 0.2f * sin(5.0f * b * PI);
	normal[2] += 0.2f * sin(5.0f * c * PI);
	*/

	//Point3D normal2 = interpolants2.crossProduct(normal);

	normal.normalize();
	interpolants2.normalize();
	#if 0
	interpolants3.normalize();
	interpolants4.normalize();

	float a = interpolants3.dot(normal);
	float b = interpolants4.dot(normal);

	float ablen = sqrtf(a*a + b*b);
	if(ablen == 0.0f) ablen = 1.0f;
	a /= ablen;
	b /= ablen;

	float u = ((atan2(a,b) / PI) + 1.0f) * 0.5f;
	//float u = abs(atan2(a,b) / PI);
	//float v = (interpolants2.dot(normal) + 1.0f) * 0.5f;
	if(v > 1.0f) v = 1.0f;
	else if(v < 0.0f) v = 0.0f;
	#endif

	vertexBuffer->setVertexUV(0, 0.0f, 0.0f);
	//s_pMeshBuilder->TexCoord2f(0, u, v);
	
	//s_pMeshBuilder->Color3fv(color);	

	// Send Tangent vector (need to set w to 1.0 or -1.0) to determine
	// which way the crossproduct will point
	interpolants2 = interpolants2.crossProduct(normal);
	interpolants2[3] = 1.0f;
	
	vertexBuffer->setVertexTangent(interpolants2);
	//s_pMeshBuilder->UserData(interpolants2.p);
}

// Calculates vertex position, normal, normalized color, UV and Tangent
void DOFASTCALL calcVertexNormalNColorUVTan(const  float x, const float y, const float z, const int edge, const CornerInfo* _corner1, const CornerInfo* _corner2, VERTEX_BUFFER_CLASS* vertexBuffer)
{
	const CornerInfoWithFourInterpolants* corner1 = (CornerInfoWithFourInterpolants*)_corner1;
	const CornerInfoWithFourInterpolants* corner2 = (CornerInfoWithFourInterpolants*)_corner2;

	const float loval = corner1->value;
	const float hival = corner2->value;
	float percent = loval / (loval - hival);
	float omPercent = 1.0f - percent;

	// TODO: this seems inefficient. Might be better to pass
	//       in the position as a Point3D rather than trying to
	//       assemble it here from parts.
	Point3D position;
	position[0] = x;
	position[1] = y;
	position[2] = z;
	position[edge] += RENDERER_CLASS::cubewidth * percent;
	vertexBuffer->setVertexPosition(position);
	
	// Idea: I can calculate normals and colors at the vertex position
	// rather than trying to interpolate. This might yield much nicer
	// normals and colors. It would also save storage for normals and
	// interpolants.

	// TODO: Why not interpolate the normals with SSE???

	const float *loNormal = corner1->normal;
	const float *hiNormal = corner2->normal;
	//float normal[3];
	Point3D normal;
	normal[0] = loNormal[0] * omPercent + hiNormal[0] * percent;
	normal[1] = loNormal[1] * omPercent + hiNormal[1] * percent;
	normal[2] = loNormal[2] * omPercent + hiNormal[2] * percent;
	vertexBuffer->setVertexNormal(normal);

	//s_pMeshBuilder->Normal3fv(normal.p); // TODO: Might be faster to pass in normals as params into this function

	// If you do the math, it turns out that the interpolated field from
	// the two contributing points is equal to threshold as it should be.
	// (The interpolated value is equal to 0)
	// Thus, the normalizer is 1.0f/threshold
	/*
	const float loTotalf = loval+threshold;
	const float hiTotalf = hival+threshold;
	float totalf = loTotalf * omPercent + hiTotalf * percent;
	float normalizer = totalf > 0.0f ? 1.0f/totalf : 0.0f;

	float color[3];
	color[0] = corner1->interpolants1[0]*omPercent + corner2->interpolants1[0]*percent;
	color[1] = corner1->interpolants1[1]*omPercent + corner2->interpolants1[1]*percent;
	color[2] = corner1->interpolants1[2]*omPercent + corner2->interpolants1[2]*percent;

	color[0]*= normalizer;
	color[1]*= normalizer;
	color[2]*= normalizer;
	*/
	
	// This is the optimized version of the code above. The point of normalizing
	// is that when the interpolated values are calculated for the normal and interpolants,
	// and then interpolated to get a value at the vertex, they are all essentially getting
	// scaled to threshold times their value. This is because they are getting scaled by
	// f in the corner calulation and never normalized by totalf. However, that normalization
	// is unnecessary because we know that by the time we get here, the average of both
	// corners is essentially scaled by the threshold.

	// TODO: We can also scale the normal by the normalizer to get it closer to
	//       a unit normal if we want to, but it 's not necessary.
	float normalizer = RENDERER_CLASS::oneOverThreshold;
	omPercent *= normalizer; percent *= normalizer;

	/*
	Point3D color;
	color[0] = corner1->interpolants1[0]*omPercent + corner2->interpolants1[0]*percent;
	color[1] = corner1->interpolants1[1]*omPercent + corner2->interpolants1[1]*percent;
	color[2] = corner1->interpolants1[2]*omPercent + corner2->interpolants1[2]*percent;
	vertexBuffer->setVertexColor(color);
	//float v = corner1->interpolants1[3]*omPercent + corner2->interpolants1[3]*percent;
	*/

	fltx4 mm_omPercent = ReplicateX4(omPercent);
	fltx4 mm_percent = ReplicateX4(percent);
	Point3D color; color.sse_vec = AddSIMD(MulSIMD(corner1->interpolants1.sse_vec, mm_omPercent), MulSIMD(corner2->interpolants1.sse_vec, mm_percent));
	vertexBuffer->setVertexColor(color);
	float v = color.p[3];

	Point3D interpolants2; interpolants2.sse_vec = AddSIMD(MulSIMD(corner1->interpolants2.sse_vec, mm_omPercent), MulSIMD(corner2->interpolants2.sse_vec, mm_percent));
	Point3D interpolants3; interpolants3.sse_vec = AddSIMD(MulSIMD(corner1->interpolants3.sse_vec, mm_omPercent), MulSIMD(corner2->interpolants3.sse_vec, mm_percent));
	Point3D interpolants4; interpolants4.sse_vec = AddSIMD(MulSIMD(corner1->interpolants4.sse_vec, mm_omPercent), MulSIMD(corner2->interpolants4.sse_vec, mm_percent));

	//TODO: I can optimize a lot of this math by using chris greens
	//      class which treats four vectors of length three as three
	//      vectors of length four.


	/*
	interpolants2.sse_vec = MulSIMD(interpolants2.sse_vec, normal.sse_vec);
	interpolants3.sse_vec = MulSIMD(interpolants3.sse_vec, normal.sse_vec);
	interpolants4.sse_vec = MulSIMD(interpolants4.sse_vec, normal.sse_vec);

	float a = interpolants2.m128_f32[0]+interpolants2.m128_f32[1]+interpolants2.m128_f32[2];
	float b = interpolants3.m128_f32[0]+interpolants3.m128_f32[1]+interpolants3.m128_f32[2];
	float c = interpolants4.m128_f32[0]+interpolants4.m128_f32[1]+interpolants4.m128_f32[2];
	*/

	/* Perturb normals
	normal[0] += 0.2f * sin(5.0f * a * PI);
	normal[1] += 0.2f * sin(5.0f * b * PI);
	normal[2] += 0.2f * sin(5.0f * c * PI);
	*/

	//Point3D normal2 = interpolants2.crossProduct(normal);

	// TODO: Is it really necessary to normalize here? It's very expensive.
	#if 0
	normal.normalize();
	interpolants2.normalize();
	interpolants3.normalize();
	interpolants4.normalize();
	#elif 1
	// In theory, all the interpolants should already be almost normalized.
	// The normal could use a bit of normalization, but it's not necessary
	normal.normalize();
	#endif


	// TODO: This can be done with SSE
	float a = interpolants3.dot(normal);
	float b = interpolants4.dot(normal);

	#if 0
	// I believe that this normalization is not necessary.
	float ablen = sqrtf(a*a + b*b);
	ablen = 1.0f / (ablen + 0.00001f); // should change .00001 to an epsilon.
	a *= ablen;
	b *= ablen;
	#endif

	float u = (atan2(a,b) * OneOver2PI) + 0.5f;

	#if 0
	// TODO: This check shouldn't be necessary if v's are passed in correctly.
	// Also, u's and v's should be clamped (we should be using a clamped texture)
	// if(v > 1.0f) v = 1.0f;
	// else if(v < 0.0f) v = 0.0f;
	#endif

	vertexBuffer->setVertexUV(0, u, v);

	// Send Tangent vector (need to set w to 1.0 or -1.0) to determine
	// which way the crossproduct will point
	// TODO: I wonder why I can't just use interpolants2 here as the tangent.
	//       It should be almost perfectly tangent.
	//       I think it is possible to use interpolants2, but it will fail
	//       at the round end-caps. The result might be ok, though. I might
	//       need to rotate the texture to get this to work correctly.
	interpolants2 = interpolants2.crossProduct(normal);
	interpolants2[3] = 1.0f;
	
	vertexBuffer->setVertexTangent(interpolants2);
	//s_pMeshBuilder->UserData(interpolants2.p);


	// color by direction
	/*
	a = (a + 1.0f) * 0.5f;
	b = (b + 1.0f) * 0.5f;
	c = (c + 1.0f) * 0.5f;
	*/

	/*
	// Hi Freq directional noise
	a = sin(10.0f * a * PI);
	b = sin(10.0f * b * PI);
	c = sin(10.0f * c * PI);
	a = (a + 1.0f) * 0.5f;
	b = (b + 1.0f) * 0.5f;
	c = (c + 1.0f) * 0.5f;
	*/

	/*
	// Hi Freq directional noise
	a = sin(10.0f * a * PI);
	b = sin(10.0f * b * PI);
	c = sin(10.0f * c * PI);
	a = (a + 1.0f) * 0.5f;
	b = (b + 1.0f) * 0.5f;
	c = (c + 1.0f) * 0.5f;
	*/						

	//if(per_vertex_color) s_pMeshBuilder->Color3fv(color);	
	//if(per_vertex_color) s_pMeshBuilder->Color3f(a, b, c);
}


// Takes UVW inputs (U and V are regular tex coords, W is position along tentacle)
// Also takes three basis vectors.
// Calculates vertex position, normal, normalized color, UVs for base and detail texture and Tangent
void DOFASTCALL calcVertexNormalNColorUVTanForBlob(const  float x, const float y, const float z, const int edge, const CornerInfo* _corner1, const CornerInfo* _corner2, VERTEX_BUFFER_CLASS* vertexBuffer)
{
	const CornerInfoWithFourInterpolants* corner1 = (CornerInfoWithFourInterpolants*)_corner1;
	const CornerInfoWithFourInterpolants* corner2 = (CornerInfoWithFourInterpolants*)_corner2;

	const float loval = corner1->value;
	const float hival = corner2->value;
	float percent = loval / (loval - hival);
	float omPercent = 1.0f - percent;

	// TODO: this seems inefficient. Might be better to pass
	//       in the position as a Point3D rather than trying to
	//       assemble it here from parts.
	Point3D position;
	position[0] = x;
	position[1] = y;
	position[2] = z;
	position[edge] += RENDERER_CLASS::cubewidth * percent;
	vertexBuffer->setVertexPosition(position);

	// Idea: I can calculate normals and colors at the vertex position
	// rather than trying to interpolate. This might yield much nicer
	// normals and colors. It would also save storage for normals and
	// interpolants.

	// TODO: Why not interpolate the normals with SSE???

	const float *loNormal = corner1->normal;
	const float *hiNormal = corner2->normal;
	//float normal[3];
	Point3D normal;
	normal[0] = loNormal[0] * omPercent + hiNormal[0] * percent;
	normal[1] = loNormal[1] * omPercent + hiNormal[1] * percent;
	normal[2] = loNormal[2] * omPercent + hiNormal[2] * percent;
	vertexBuffer->setVertexNormal(normal);

	//s_pMeshBuilder->Normal3fv(normal.p); // TODO: Might be faster to pass in normals as params into this function

	// If you do the math, it turns out that the interpolated field from
	// the two contributing points is equal to threshold as it should be.
	// (The interpolated value is equal to 0)
	// Thus, the normalizer is 1.0f/threshold
	/*
	const float loTotalf = loval+threshold;
	const float hiTotalf = hival+threshold;
	float totalf = loTotalf * omPercent + hiTotalf * percent;
	float normalizer = totalf > 0.0f ? 1.0f/totalf : 0.0f;

	float color[3];
	color[0] = corner1->interpolants1[0]*omPercent + corner2->interpolants1[0]*percent;
	color[1] = corner1->interpolants1[1]*omPercent + corner2->interpolants1[1]*percent;
	color[2] = corner1->interpolants1[2]*omPercent + corner2->interpolants1[2]*percent;

	color[0]*= normalizer;
	color[1]*= normalizer;
	color[2]*= normalizer;
	*/

	// This is the optimized version of the code above. The point of normalizing
	// is that when the interpolated values are calculated for the normal and interpolants,
	// and then interpolated to get a value at the vertex, they are all essentially getting
	// scaled to threshold times their value. This is because they are getting scaled by
	// f in the corner calulation and never normalized by totalf. However, that normalization
	// is unnecessary because we know that by the time we get here, the average of both
	// corners is essentially scaled by the threshold.

	// TODO: We can also scale the normal by the normalizer to get it closer to
	//       a unit normal if we want to, but it 's not necessary.
	float normalizer = RENDERER_CLASS::oneOverThreshold;
	omPercent *= normalizer; percent *= normalizer;

	/*
	Point3D color;
	color[0] = corner1->interpolants1[0]*omPercent + corner2->interpolants1[0]*percent;
	color[1] = corner1->interpolants1[1]*omPercent + corner2->interpolants1[1]*percent;
	color[2] = corner1->interpolants1[2]*omPercent + corner2->interpolants1[2]*percent;
	vertexBuffer->setVertexColor(color);
	//float v = corner1->interpolants1[3]*omPercent + corner2->interpolants1[3]*percent;
	*/

	fltx4 mm_omPercent = ReplicateX4(omPercent);
	fltx4 mm_percent = ReplicateX4(percent);
	Point3D color; color.sse_vec = AddSIMD(MulSIMD(corner1->interpolants1.sse_vec, mm_omPercent), MulSIMD(corner2->interpolants1.sse_vec, mm_percent));
	vertexBuffer->setVertexColor(color);
	float v = color.p[3];

	Point3D interpolants2; interpolants2.sse_vec = AddSIMD(MulSIMD(corner1->interpolants2.sse_vec, mm_omPercent), MulSIMD(corner2->interpolants2.sse_vec, mm_percent));
	Point3D interpolants3; interpolants3.sse_vec = AddSIMD(MulSIMD(corner1->interpolants3.sse_vec, mm_omPercent), MulSIMD(corner2->interpolants3.sse_vec, mm_percent));
	Point3D interpolants4; interpolants4.sse_vec = AddSIMD(MulSIMD(corner1->interpolants4.sse_vec, mm_omPercent), MulSIMD(corner2->interpolants4.sse_vec, mm_percent));

	//TODO: I can optimize a lot of this math by using chris greens
	//      class which treats four vectors of length three as three
	//      vectors of length four.


	/*
	interpolants2.sse_vec = MulSIMD(interpolants2.sse_vec, normal.sse_vec);
	interpolants3.sse_vec = MulSIMD(interpolants3.sse_vec, normal.sse_vec);
	interpolants4.sse_vec = MulSIMD(interpolants4.sse_vec, normal.sse_vec);

	float a = interpolants2.m128_f32[0]+interpolants2.m128_f32[1]+interpolants2.m128_f32[2];
	float b = interpolants3.m128_f32[0]+interpolants3.m128_f32[1]+interpolants3.m128_f32[2];
	float c = interpolants4.m128_f32[0]+interpolants4.m128_f32[1]+interpolants4.m128_f32[2];
	*/

	/* Perturb normals
	normal[0] += 0.2f * sin(5.0f * a * PI);
	normal[1] += 0.2f * sin(5.0f * b * PI);
	normal[2] += 0.2f * sin(5.0f * c * PI);
	*/

	//Point3D normal2 = interpolants2.crossProduct(normal);

	// TODO: Is it really necessary to normalize here? It's very expensive.
#if 0
	normal.normalize();
	interpolants2.normalize();
	interpolants3.normalize();
	interpolants4.normalize();
#elif 1
	// In theory, all the interpolants should already be almost normalized.
	// The normal could use a bit of normalization, but it's not necessary
	normal.normalize();
#endif


	// TODO: This can be done with SSE
	float a = interpolants3.dot(normal);
	float b = interpolants4.dot(normal);

#if 0
	// I believe that this normalization is not necessary.
	float ablen = sqrtf(a*a + b*b);
	ablen = 1.0f / (ablen + 0.00001f); // should change .00001 to an epsilon.
	a *= ablen;
	b *= ablen;
#endif

	float u = (atan2(a,b) * OneOver2PI) + 0.5f;

#if 0
	// TODO: This check shouldn't be necessary if v's are passed in correctly.
	// Also, u's and v's should be clamped (we should be using a clamped texture)
	// if(v > 1.0f) v = 1.0f;
	// else if(v < 0.0f) v = 0.0f;
#endif

	vertexBuffer->setVertexUV(0, u, v);

	// Send Tangent vector (need to set w to 1.0 or -1.0) to determine
	// which way the crossproduct will point
	// TODO: I wonder why I can't just use interpolants2 here as the tangent.
	//       It should be almost perfectly tangent.
	//       I think it is possible to use interpolants2, but it will fail
	//       at the round end-caps. The result might be ok, though. I might
	//       need to rotate the texture to get this to work correctly.
	interpolants2 = interpolants2.crossProduct(normal);
	interpolants2[3] = 1.0f;

	vertexBuffer->setVertexTangent(interpolants2);
	//s_pMeshBuilder->UserData(interpolants2.p);


	// color by direction
	/*
	a = (a + 1.0f) * 0.5f;
	b = (b + 1.0f) * 0.5f;
	c = (c + 1.0f) * 0.5f;
	*/

	/*
	// Hi Freq directional noise
	a = sin(10.0f * a * PI);
	b = sin(10.0f * b * PI);
	c = sin(10.0f * c * PI);
	a = (a + 1.0f) * 0.5f;
	b = (b + 1.0f) * 0.5f;
	c = (c + 1.0f) * 0.5f;
	*/

	/*
	// Hi Freq directional noise
	a = sin(10.0f * a * PI);
	b = sin(10.0f * b * PI);
	c = sin(10.0f * c * PI);
	a = (a + 1.0f) * 0.5f;
	b = (b + 1.0f) * 0.5f;
	c = (c + 1.0f) * 0.5f;
	*/						

	//if(per_vertex_color) s_pMeshBuilder->Color3fv(color);	
	//if(per_vertex_color) s_pMeshBuilder->Color3f(a, b, c);
}

void DOFASTCALL calcVertexNormalNTexCoord4(const float x, const float y, const float z, const int edge, const CornerInfo* _corner1, const CornerInfo* _corner2, VERTEX_BUFFER_CLASS* vertexBuffer)
{
	const CornerInfoWithFourInterpolants* corner1 = (CornerInfoWithFourInterpolants*)_corner1;
	const CornerInfoWithFourInterpolants* corner2 = (CornerInfoWithFourInterpolants*)_corner2;

	float ALIGN16 flPosition[4];
	flPosition[0] = x;
	flPosition[1] = y;
	flPosition[2] = z;

	fltx4 f4LoVal = ReplicateX4( corner1->value );
	fltx4 f4HiVal = ReplicateX4( corner2->value );
	fltx4 f4Pct = DivSIMD( f4LoVal, SubSIMD( f4LoVal, f4HiVal ) );
	fltx4 f4OMPct = SubSIMD( Four_Ones, f4Pct );

	fltx4 f4Edge = AndSIMD( LoadAlignedSIMD( g_SIMD_ComponentMask[edge] ), f4Pct );

	const float *loNormal = corner1->normal;
	const float *hiNormal = corner2->normal;
	fltx4 f4Normal = AddSIMD( MulSIMD( LoadAlignedSIMD(loNormal), f4OMPct ), MulSIMD( LoadAlignedSIMD(hiNormal), f4Pct ) );

#if _X360
	f4Normal = XMVector3Normalize( f4Normal );
#else
	VectorNormalize( (float *)&f4Normal );
#endif

	// This is the optimized version of the code above. The point of normalizing
	// is that when the interpolated values are calculated for the normal and interpolants,
	// and then interpolated to get a value at the vertex, they are all essentially getting
	// scaled to threshold times their value. This is because they are getting scaled by
	// f in the corner calculation and never normalized by totalf. However, that normalization
	// is unnecessary because we know that by the time we get here, the average of both
	// corners is essentially scaled by the threshold.

	// TODO: We can also scale the normal by the normalizer to get it closer to
	//       a unit normal if we want to, but it 's not necessary.
	fltx4 f4Normalizer = ReplicateX4( RENDERER_CLASS::oneOverThreshold );
	f4Pct = MulSIMD( f4Normalizer, f4Pct );
	f4OMPct = MulSIMD( f4Normalizer, f4OMPct );

	fltx4 f4TexCoord = AddSIMD(MulSIMD(corner1->interpolants1.sse_vec, f4OMPct), MulSIMD(corner2->interpolants1.sse_vec, f4Pct));


	fltx4 f4Position = AddSIMD( LoadAlignedSIMD( flPosition ), MulSIMD( f4Edge, ReplicateX4( RENDERER_CLASS::cubewidth ) ) );

	vertexBuffer->setVertexPosition( f4Position );
	vertexBuffer->setVertexNormal( f4Normal );
	vertexBuffer->setVertexUV( 0, f4TexCoord );
}

void DOFASTCALL calcCornerNormalColorUVTan_SIMD(fltx4 f4Pos, const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache)
{
#ifdef SPEED_CHECK
	//tt++;
	int cache_count = 0;
	int influence_count = 0;
#endif

	//float totalf = 0.0f;
	fltx4 normal = Four_Zeros;
	fltx4 interpolants1 = Four_Zeros;
	fltx4 interpolants2 = Four_Zeros;
	fltx4 interpolants3 = Four_Zeros;
	fltx4 interpolants4 = Four_Zeros;
	fltx4 position = MaskedAssign( ( bi32x4 ) LoadAlignedSIMD( g_SIMD_ComponentMask[3] ), Four_Ones, f4Pos );

#if _X360
	uint32 nOut = (uint32(iy)<<24) + (uint32(iz)<<16);
#endif

	fltx4 f4scalerSq = ReplicateX4( RENDERER_CLASS::scalerSq );
	fltx4 f4Fourpt25s = ReplicateX4( 0.25f );
#if (DO_RADIUS_CHECK == 1)
	fltx4 f4CutoffRSq = ReplicateX4( RENDERER_CLASS::cutoffRSq );
#endif
	START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, pCache, false)
	fltx4 f4pRSq = ReplicateX4( p->GetFieldRScaleSq() );
	fltx4 diff = SubSIMD( position, p->center.sse_vec );
	fltx4 diffSq = MulSIMD( diff, diff );

	// scalar code
	//	float rSq = SubFloat(diffSq, 0)+SubFloat(diffSq, 1)+SubFloat(diffSq, 2); //Can this be done in SSE?
	//	rSq *= p->fieldRScaleSq; // TODO: The fieldRScaleSq should incorporate scalerSq (can compare rSq with one over sqrt(2) instead of cutoffRSq below)

	fltx4 f4rSq = MulSIMD( f4pRSq, AddSIMD( AddSIMD( diffSq, RotateLeft( diffSq ) ), RotateLeft2( diffSq ) ) );

#if (DO_RADIUS_CHECK == 1)
	bi32x4 f4Test = CmpLeSIMD( f4rSq, f4CutoffRSq );
	if( !IsAllZeros(f4Test) )
	{	
#endif
		//rSq *= RENDERER_CLASS::scalerSq;
		//float f = 0.25f + rSq*rSq - rSq;
		f4rSq = MulSIMD( SplatXSIMD( f4rSq ), f4scalerSq );
		fltx4 f4 = SubSIMD( AddSIMD( f4Fourpt25s, MulSIMD( f4rSq, f4rSq ) ), f4rSq );

		//Note that I should also divide the vector (diff_x, diff_y, diff_z) by it's length
		//to get a unit vector, but this doesn't seem to make the result look any better

		normal = AddSIMD(normal, MulSIMD(diff, f4));
		interpolants1 = AddSIMD(interpolants1, MulSIMD(((ImpParticleWithFourInterpolants*)p)->interpolants1.sse_vec,f4));
		interpolants2 = AddSIMD(interpolants2, MulSIMD(((ImpParticleWithFourInterpolants*)p)->interpolants2.sse_vec,f4));
		interpolants3 = AddSIMD(interpolants3, MulSIMD(((ImpParticleWithFourInterpolants*)p)->interpolants3.sse_vec,f4));
		interpolants4 = AddSIMD(interpolants4, MulSIMD(((ImpParticleWithFourInterpolants*)p)->interpolants4.sse_vec,f4));
#if (DO_RADIUS_CHECK == 1)
	}
#endif
	END_LOOP_OVER_NEARBY_PARTICLES()

	fltx4 f4WOnly = LoadAlignedSIMD( g_SIMD_ComponentMask[3] );
		// TODO: I can actually store value as part of normal.
		// (nx, ny, nz, value)... It would be relatively easy
		// to do the calculations this way. I would just have to initialize
		// normal to (0,0,0,-threshold), and when I calculate distance,
		// always set position to (x,y,z,1) so that we always get 1*f into
		// the value field.
		// I did just that, except I initialize the normal to all zeros and
		// subtract out the threshold here for now. If we actually use the
		// normal to store the value, then I will change the threshold to be
		// subtracted out at the beginning. This assumes that particles have
		// their position[3] value all set to 0.
		// cornerInfo->value = totalf - RENDERER_CLASS::threshold;
		fltx4 f4Threshold = ReplicateX4(RENDERER_CLASS::threshold);
		f4Threshold = AndSIMD( f4Threshold, f4WOnly );
		normal = SubSIMD( normal, f4Threshold );

	// I doesn't make sense to normalize normals in the following
	// way because this doesn't make them length one.
	//float normalizer = totalf > 0.0f ? 1.0f/totalf : 0.0f;
	//cornerInfo->normal[0] = nx * normalizer;
	//cornerInfo->normal[1] = ny * normalizer;
	//cornerInfo->normal[2] = nz * normalizer;

#if _X360
	float *pflOut = reinterpret_cast<float *>(cornerInfo);
	StoreAlignedSIMD( pflOut, normal );
	fltx4 f4Index = ReplicateIX4(nOut);
	f4Index = AndSIMD(f4WOnly, f4Index);
	StoreAlignedSIMD( pflOut+4, f4Index );

	//fltx4 f4Index = Four_Zeros;
	//StoreAlignedSIMD( pf4Out++, f4Index );

	StoreAlignedSIMD( pflOut+8, interpolants1 );
	StoreAlignedSIMD( pflOut+12, interpolants2 );
	StoreAlignedSIMD( pflOut+16, interpolants3 );
	StoreAlignedSIMD( pflOut+20, interpolants4 );

#else
	StoreAlignedSIMD( cornerInfo->normal, normal );
	// TODO: In the SSE version, I should store the normal as a point3d.
	// Also, I can use the fourth location to store the field value.

	VERTEX_BUFFER_CLASS::clear3Ids( cornerInfo->edges );

	cornerInfo->y = iy;
	cornerInfo->z = iz;
	cornerInfo->pad1 = 0;
	cornerInfo->pad2 = 0;

	// Normalizing the interpolants can be done here, but it seems better to
	// do it in the polygonization code once we've interpolated between vertices.
	// That is because if we normalize here, then the contributions from particles
	// to a vertex that doesn't have many neighbors will be weighted higher than
	// the contributions from a vertex with many particles. By normalizing in
	// the polygonization code, all contributions are weighted by f*vertexInterpolant
	// and then divided by the total of all f*vertexInterpolant terms which is equal
	// to threshold.


	//cornerInfo->interpolants1[0] = interpolants1.m128_f32[0];
	//cornerInfo->interpolants1[1] = interpolants1.m128_f32[1];
	//cornerInfo->interpolants1[2] = interpolants1.m128_f32[2];


	((CornerInfoWithFourInterpolants*)cornerInfo)->interpolants1.sse_vec = interpolants1;
	((CornerInfoWithFourInterpolants*)cornerInfo)->interpolants2.sse_vec = interpolants2;
	((CornerInfoWithFourInterpolants*)cornerInfo)->interpolants3.sse_vec = interpolants3;
	((CornerInfoWithFourInterpolants*)cornerInfo)->interpolants4.sse_vec = interpolants4;
#endif
}

void DOFASTCALL calcSign2_SIMD(fltx4 f4Pos, const cube_index_t ix, const cube_index_t iy, const cube_index_t iz, CornerInfo* const cornerInfo, PARTICLE_CACHE_CLASS* pCache)
{
	fltx4 f4Total = Four_Zeros;
	fltx4 f4scalerSq = ReplicateX4( RENDERER_CLASS::scalerSq );
	fltx4 f4Fourpt25s = ReplicateX4( 0.25f );
	fltx4 position = f4Pos;
	fltx4 f4Threshold = ReplicateX4( RENDERER_CLASS::threshold );
	START_LOOP_OVER_NEARBY_PARTICLES(ix, iy, iz, pCache, false)
		fltx4 f4pRSq = ReplicateX4( p->GetFieldRScaleSq() );
		fltx4 diff = SubSIMD(position, p->center.sse_vec);
		fltx4 diffSq = MulSIMD(diff, diff);
		fltx4 f4rSq = MulSIMD( f4pRSq, AddSIMD( AddSIMD( diffSq, RotateLeft( diffSq ) ), RotateLeft2( diffSq ) ) );

		//rSq *= RENDERER_CLASS::scalerSq;
		//float f = 0.25f + rSq*rSq - rSq;
		f4rSq = MulSIMD( SplatXSIMD( f4rSq ), f4scalerSq );
		fltx4 f4 = SubSIMD( AddSIMD( f4Fourpt25s, MulSIMD( f4rSq, f4rSq ) ), f4rSq );
		f4Total = AddSIMD( f4Total, f4 );

		bi32x4 f4Test = CmpGtSIMD( f4Total, f4Threshold );
		if ( IsAnyTrue( f4Test ) )
			break;
	END_LOOP_OVER_NEARBY_PARTICLES()

	cornerInfo->value = SubFloat( f4Total, 0 ) - RENDERER_CLASS::threshold;
	cornerInfo->y = iy;
	cornerInfo->z = iz;
}
