/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef NDPHYS_VECTOR_MATH_H 
#define NDPHYS_VECTOR_MATH_H 

#include "corelib/math/gamemath.h"

inline VF32 Sqr(VF32 x)
{
	return SMATH_VEC_MUL(x, x);
}

inline Vector Sqr(Vector_arg a)
{
	return Vector(Sqr(a.QuadwordValue()));
}

inline VF32 ClampPos(VF32 a)
{
	return SMATH_VEC_MIN(a, SMATH_VEC_SET_ZERO());
}

inline Scalar ClampPos(SMath::Scalar_arg a)
{
	return Scalar(ClampPos(a.QuadwordValue()));
}

inline VF32 ClampNeg(VF32 a)
{
	return SMATH_VEC_MAX(a, SMATH_VEC_SET_ZERO());
}

inline Scalar ClampNeg(SMath::Scalar_arg a)
{
	return Scalar(ClampNeg(a.QuadwordValue()));
}

inline VF32 MakeXYZ0(VF32 a)
{
	SMATH_VEC_SET_W(a, SMATH_VEC_SET_ZERO());
	return a;
}

inline Vector MakeXYZ0(SMath::Vector_arg a)
{
	return Vector(MakeXYZ0(a.QuadwordValue()));
}

inline VF32 MakeXYZ1(VF32 a)
{
	SMATH_VEC_SET_W(a, SMATH_VEC_GET_UNIT_W());
	return a;
}

inline Vector MakeXYZ1(SMath::Vector_arg a)
{
	return Vector(MakeXYZ1(a.QuadwordValue()));
}

inline SMath::Point Mean(SMath::Point_arg a, SMath::Point_arg b)
{
	return SMath::Point((a.GetVec4()+b.GetVec4())*Scalar(0.5f));
}

// this will produce the same result in all components of the result
// ASSUMES: component [3] MUST be ZERO for Dot3's
// otherwise, produces Dot4
inline VF32 FastDot( VF32 a, VF32 b )
{
	VF32 result;
	SMATH_VEC_DOT4(result, a, b);
	return result;
}

inline VF32 FastDot( SMath::Vec4_arg a, SMath::Vec4_arg b )
{
	return FastDot(a.QuadwordValue(), b.QuadwordValue());
}

inline VF32 SumComponentsXYZ0(VF32 a)
{
	VF32 result;
	SMATH_VEC_SUM3(result, a);
	return SMATH_VEC_REPLICATE_X(result);
}

inline Scalar SumComponentsXYZ0(const SMath::Vector_arg value)
{
	return Scalar(SumComponentsXYZ0(value.QuadwordValue()));
}

inline Point LoadPointNA(const void *p)
{
	const float* pFloatArray = (const float*)p;
	return Point(pFloatArray[0], pFloatArray[1], pFloatArray[2]);
}

inline Scalar Dot4(Point_arg point, Vec4_arg plane)
{
	VF32 result;
	SMATH_VEC_DOT4(result, MakeXYZ1(point.QuadwordValue()), plane.QuadwordValue());
	return Scalar(result) ;
}

#endif // NDPHYS_VECTOR_MATH_H 

