/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef NDLIB_PHYS_BLAS_HDR
#define NDLIB_PHYS_BLAS_HDR

#include "corelib/math/gamemath.h"
#include "gamelib/ndphys/rope/physvectormath.h"
#include "corelib/util/assertion.h"

#define PHYS_BLAS_PROFILE_FUNCTION() 

// computes the dot product of two vectors, touching the slack
inline float blasDot (U32 numFloats,const float*p, const float*q)
{
	PHYS_BLAS_PROFILE_FUNCTION();
	float res = 0;
	for(U32 i = 0; i < numFloats; ++i)
		res += p[i]*q[i];
	return res;
}

inline float lenSqA (U32 numFloats, const float*p)
{
	PHYS_BLAS_PROFILE_FUNCTION();
	float res;
	res = 0;
	for(U32 i= 0; i < numFloats; ++i)
		res += Sqr(p[i]);
	return res;
}

// blas routine: y += alpha*x
inline void blasAxpy (U32 numFloats, float alpha, const float* x, float* y)
{
	PHYS_BLAS_PROFILE_FUNCTION();
	for(U32 i = 0; i < numFloats; ++i)
		y[i] += alpha * x[i];
}


// blas routine: y = alpha*y + x
inline void blasAypx (U32 numFloats, float alpha, const float* x, float* y)
{
	PHYS_BLAS_PROFILE_FUNCTION();
	for(U32 i = 0; i < numFloats; ++i)
		y[i] = alpha * y[i] + x[i];
}

#endif
