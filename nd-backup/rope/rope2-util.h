/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef NDPHYS_ROPE2_UTIL_H 
#define NDPHYS_ROPE2_UTIL_H 

#include "gamelib/ndphys/rope/physvectormath.h"

inline bool LinePlaneIntersection(Point_arg a1, Vector_arg b1, Vec4_arg plane, Scalar& s)
{
	Scalar dt = Dot(b1, Vector(plane));
	s = -Dot4(a1, plane) / dt;
	return dt != Scalar(kZero);
}

inline bool LinePlaneIntersection(Point_arg a1, Vector_arg b1, Point_arg planePnt, Vector_arg planeNorm, Scalar& s)
{
	Scalar dt = Dot(b1, planeNorm);
	s = -Dot(a1-planePnt, planeNorm) / dt;
	return dt != Scalar(kZero);
}

// Line(a1, b1) x Line(a2, b2). t will be parameter of the second line.
// b1, b2 must be normalized.
// Returns false if lines are (too close to) parallel.
inline bool LineLineIntersection(Point_arg a1, Vector_arg b1, Point_arg a2, Vector_arg b2, Scalar& t)
{
	Vector z = SafeNormalize(Cross(b1, b2), Vector(kZero));
	Vector y = Cross(z, b1);
	Scalar b2y = Dot(y, b2);
	Scalar ey = Dot(y, a2-a1);
	bool bParallel = b2y == 0.0f;
	t = bParallel ? Scalar(kZero) : -ey/b2y;
	return !bParallel;
}

// Line(a1, b1) x Line(a2, b2). s will be parameter of first line, t will be parameter of the second line.
// b1, b2 must be normalized.
// Returns false if lines are (too close to) parallel.
inline bool LineLineIntersection(Point_arg a1, Vector_arg b1, Point_arg a2, Vector_arg b2, Scalar& s, Scalar& t)
{
	bool bNotParallel = LineLineIntersection(a1, b1, a2, b2, t);
	Scalar b2x = Dot(b1, b2);
	Scalar ex = Dot(b1, a2-a1);
	s = ex + b2x*t;
	return bNotParallel;
}

// Returns false if dist is bigger than earlyOutDist
// t will be parameter on the line segment
// b1, b2 must be normalized.
inline bool LineSegDist(Point_arg a1, Vector_arg b1, Point_arg a2, Vector_arg b2, Scalar len2, Scalar_arg earlyOutDist, 
	Scalar& dist, Scalar& s, Scalar& t)
{
	Scalar zLen;
	Vector z = Normalize(Cross(b1, b2), zLen);
	Vector a1Toa2 = a2-a1;
	if (zLen > 0.0001f)
	{
		// Not parallel
		dist = Abs(Dot(a1Toa2, z));
		if (dist > earlyOutDist)
			return false;

		Vector y = Cross(z, b1);
		Scalar b2y = Dot(y, b2);
		Scalar ey = Dot(y, a1Toa2);
		t = -ey/b2y;

		t = MinMax(t, Scalar(kZero), len2);
		s = Dot(a1Toa2+t*b2, b1);
		dist = Dist(a1+s*b1, a2+t*b2);
	}
	else
	{
		// ~Parallel
		t = 0.0f;
		s = Dot(a1Toa2, b1);
		dist = Dist(a1Toa2, s * b1);
	}

	return dist <= earlyOutDist;
}

// Returns false if dist is bigger than earlyOutDist
// t will be parameter on the line segment
// b1, b2 must be normalized.
inline bool SegSegDist(Point_arg a1, Vector_arg b1, Scalar len1, Point_arg a2, Vector_arg b2, Scalar len2, Scalar_arg earlyOutDist, 
	Scalar& dist, Scalar& s, Scalar& t)
{
	Scalar zLen;
	Vector z = Normalize(Cross(b1, b2), zLen);
	Vector a1Toa2 = a2-a1;
	if (zLen > 0.0001f)
	{
		// Not parallel
		dist = Abs(Dot(a1Toa2, z));
		if (dist >= earlyOutDist)
			return false;

		Vector y = Cross(z, b1);
		Scalar b2y = Dot(y, b2);
		Scalar ey = Dot(y, a1Toa2);
		t = -ey/b2y;

		t = MinMax(t, Scalar(kZero), len2);
		s = Dot(a1Toa2+t*b2, b1);
		s = MinMax(s, Scalar(kZero), len1);
		dist = Dist(a1+s*b1, a2+t*b2);
	}
	else
	{
		// ~Parallel
		bool opposite = Dot(b1, b2) < 0.0f;
		Scalar sa2 = Dot(a1Toa2, b1);
		Scalar send2 = Dot(a1Toa2+len2*b2, b1);
		if (opposite)
		{
			if (sa2 < len1)
			{
				t = 0.0f;
				s = Max(sa2, Scalar(kZero));
			}
			else if (send2 > 0.0f)
			{
				t = len2;
				s = Min(send2, len1);
			}
			else
			{
				t = Dot(-a1Toa2, b2);
				s = 0.0f;
			}
		}
		else
		{
			if (sa2 > 0.0f)
			{
				t = 0.0f;
				s = Min(sa2, len1);
			}
			else if (send2 < len1)
			{
				t = len2;
				s = Max(send2, Scalar(kZero));
			}
			else
			{
				t = Dot(-a1Toa2, b2);
				s = 0.0f;
			}
		}
		dist = Dist(a1+s*b1, a2+t*b2);
	}

	return dist <= earlyOutDist;
}

// t will be parameter on the line segment
// b must be normalized.
inline Scalar PointSegDist(Point_arg a1, Vector_arg b1, Scalar len, Point_arg p, Scalar& t)
{
	t = Dot(p - a1, b1);
	if (t <= 0.0f)
	{
		t = kZero;
		return Dist(a1, p);
	}
	else if (t >= len)
	{
		t = len;
		return Dist(a1 + len * b1, p);
	}
	else 
		return Dist(a1 + t * b1, p);
}

// t will be parameter <0, 1> parameter of the segment
inline Scalar PointSegDist(Point_arg a1, Point_arg a2, Point_arg p, Scalar& t)
{
	Scalar len;
	Vector b1 = SafeNormalize(a2-a1, kZero, len);
	Scalar d = PointSegDist(a1, b1, len, p, t);
	t /= len;
	return d;
}

#endif // NDPHYS_ROPE2_UTIL_H 

