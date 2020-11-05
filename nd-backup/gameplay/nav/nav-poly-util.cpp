/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-poly-util.h"

#include "ndlib/math/pretty-math.h"

#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-mesh.h"

/// --------------------------------------------------------------------------------------------------------------- ///
Scalar NavPolyUtil::FindNearestPoint(Point_arg inPt, const Point* pVerts, size_t numVerts, Point* pPosOut)
{
	Point nearestPt = kOrigin;
	const Scalar zero = SCALAR_LC(0.0f);
	Scalar bestDist = SCALAR_LC(kLargeFloat);
	Vec4 vDots;

	bool containsPt = true;
	Scalar dots[4];

	*pPosOut = inPt;

	for (U32F iEdge = 0; iEdge < numVerts; ++iEdge)
	{
		const Point v0 = pVerts[iEdge];
		const Point v1 = pVerts[(iEdge + 1) % numVerts];

		if (containsPt)
		{
			const Vector v0ToPt = inPt - v0;
			const Vector v0v1Perp = RotateY90(VectorXz(v1 - v0));

			dots[iEdge] = Dot(v0v1Perp, v0ToPt);

			if (dots[iEdge] < zero)
			{
				containsPt = false;
			}
		}

		Point edgePoint = inPt;

		// Note: need to do a full 3d distance check here because we
		// always need to account for the y-delta added into the distance
		// (like we do in the containsPt branch below) because we want
		// to be able to correctly compare results from multiple calls
		// to this function.
		const Scalar dist = DistPointSegment(inPt, v0, v1, &edgePoint);

		if (dist < bestDist)
		{
			bestDist = dist;
			nearestPt = edgePoint;
			NAV_ASSERTF(QuadContainsPointXz(nearestPt, pVerts, 0.1f, &vDots),
						("FindNearestPoint Failed: %s %s %s %s (%d) -> %s %s",
						PrettyPrint(pVerts[iEdge]),
						PrettyPrint(pVerts[(iEdge + 1) % numVerts]),
						PrettyPrint(pVerts[(iEdge + 2) % numVerts]),
						PrettyPrint(pVerts[(iEdge + 3) % numVerts]),
						numVerts,
						PrettyPrint(inPt),
						PrettyPrint(vDots)));
		}
	}

	if (containsPt)
	{
		Vector normal;
		GetPointAndNormal(inPt, pVerts, numVerts, pPosOut, &normal);
		bestDist = Abs(pPosOut->Y() - inPt.Y());
		NAV_ASSERTF(QuadContainsPointXz(nearestPt, pVerts, 0.1f, &vDots),
					("FindNearestPoint Failed: %s", PrettyPrint(vDots)));
	}
	else
	{
		*pPosOut = nearestPt;
	}

	NAV_ASSERTF(QuadContainsPointXz(nearestPt, pVerts, 0.1f, &vDots),
				("FindNearestPoint Failed: %s", PrettyPrint(vDots)));

	return bestDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Scalar NavPolyUtil::FindNearestPointXz(Point_arg inPt, const Point* pVerts, size_t numVerts, Point* pPosOut)
{
	Point nearestPt = kOrigin;
	const Scalar zero = SCALAR_LC(0.0f);
	Scalar bestDist = SCALAR_LC(kLargeFloat);
	Vec4 vDots;

	bool containsPt = true;
	Scalar dots[4];

	*pPosOut = inPt;

	for (U32F iEdge = 0; iEdge < numVerts; ++iEdge)
	{
		const Point v0 = pVerts[iEdge];
		const Point v1 = pVerts[(iEdge + 1) % numVerts];

		if (containsPt)
		{
			const Vector v0ToPt = inPt - v0;
			const Vector v0v1Perp = RotateY90(VectorXz(v1 - v0));

			dots[iEdge] = Dot(v0v1Perp, v0ToPt);

			if (dots[iEdge] < zero)
			{
				containsPt = false;
			}
		}

		Scalar tt;
		const Scalar dist = DistPointSegmentXz(inPt, v0, v1, nullptr, &tt);

		if (dist < bestDist)
		{
			const Point edgePoint = Lerp(v0, v1, tt);

			bestDist = dist;
			nearestPt = edgePoint;

			NAV_ASSERTF(QuadContainsPointXz(nearestPt, pVerts, 0.1f, &vDots),
						("FindNearestPoint Failed: %s %s %s %s (%d) -> %s %s",
						 PrettyPrint(pVerts[iEdge]),
						 PrettyPrint(pVerts[(iEdge + 1) % numVerts]),
						 PrettyPrint(pVerts[(iEdge + 2) % numVerts]),
						 PrettyPrint(pVerts[(iEdge + 3) % numVerts]),
						 numVerts,
						 PrettyPrint(inPt),
						 PrettyPrint(vDots)));
		}
	}

	if (containsPt)
	{
		Vector normal;
		GetPointAndNormal(inPt, pVerts, numVerts, pPosOut, &normal);
		bestDist = 0.0f;
		NAV_ASSERTF(QuadContainsPointXz(nearestPt, pVerts, 0.1f, &vDots),
					("FindNearestPoint Failed: %s", PrettyPrint(vDots)));
	}
	else
	{
		*pPosOut = nearestPt;
	}

	NAV_ASSERTF(QuadContainsPointXz(nearestPt, pVerts, 0.1f, &vDots),
				("FindNearestPoint Failed: %s", PrettyPrint(vDots)));

	return bestDist;
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NavPolyUtil::GetPointAndNormal(Point_arg inPt,
									const Point* pVerts,
									size_t numVerts,
									Point* pPosOut,
									Vector* pNormalOut)
{
	NAV_ASSERT(pVerts);
	NAV_ASSERT(numVerts >= 3);

	// determine the triangle enclosing the point, either verts 0-1-2 or 0-2-3
	Point v0 = pVerts[0];
	Point v1 = pVerts[1];
	Point v2 = pVerts[2];

	static const Scalar zero = SCALAR_LC(0.0f);

	// if its a quad
	if (numVerts > 3)
	{
		Point v3 = pVerts[3];
		U32F iSplitVert;

		// triangulation heuristic:  split the quad using the shortest edge
		if (DistXzSqr(v0, v2) <= DistXzSqr(v1, v3))
		{
			// triangles are v0-1-2 and v2-3-0
			iSplitVert = 0;
		}
		else
		{
			// triangles are v1-2-3 and v3-0-1
			iSplitVert = 1;
		}
		const Vector vSplitToIn = inPt - pVerts[iSplitVert];
		const Vector vSplitR90 = VectorXz(RotateY90(pVerts[(iSplitVert + 2) % numVerts] - pVerts[iSplitVert]));
		const Scalar dot = Dot(vSplitToIn, vSplitR90);

		if (dot > zero)
		{
			v0 = pVerts[(iSplitVert + 2) % numVerts];
			v1 = pVerts[(iSplitVert + 3) & 3];
			v2 = pVerts[iSplitVert];
		}
		else
		{
			v0 = pVerts[(iSplitVert + 0)];
			v1 = pVerts[(iSplitVert + 1)];
			v2 = pVerts[(iSplitVert + 2) % numVerts];
		}
	}

	// now we have the triangle, time to generate a plane
	const Vector v01 = v1 - v0;
	const Vector v02 = v2 - v0;
	const Vector n = SafeNormalize(Cross(v01, v02), kZero);

	if (Abs(n.Y()) < NDI_FLT_EPSILON)
	{
		DistPointSegment(inPt, v2, v1, pPosOut);
		*pNormalOut = kUnitYAxis;
	}
	else
	{
		const Scalar dd = -Dot(v0 - kOrigin, n);

		// plane eq is plane(v) = Dot(n, v) + dd
		const Vector nxz = VectorXz(n);
		const Scalar y = -(Dot(nxz, inPt - kOrigin) + dd) / n.Y();

		*pPosOut = Point(inPt.X(), y, inPt.Z());
		*pNormalOut = n;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static inline __m128 QuadContainsPointCommon(const Point inPt, const Point* const __restrict pVerts)
{
	const __m128 V0 = pVerts[0].QuadwordValue();
	const __m128 V1 = pVerts[1].QuadwordValue();
	const __m128 V2 = pVerts[2].QuadwordValue();
	const __m128 V3 = pVerts[3].QuadwordValue();
	const __m128 EV01 = _mm_shuffle_ps(_mm_sub_ps(V1, V0), _mm_sub_ps(V2, V1), 136);
	const __m128 EV23 = _mm_shuffle_ps(_mm_sub_ps(V3, V2), _mm_sub_ps(V0, V3), 136);
	const __m128 PV01s = _mm_shuffle_ps(_mm_sub_ps(V0, inPt.QuadwordValue()), _mm_sub_ps(V1, inPt.QuadwordValue()), 34);
	const __m128 PV23s = _mm_shuffle_ps(_mm_sub_ps(V2, inPt.QuadwordValue()), _mm_sub_ps(V3, inPt.QuadwordValue()), 34);
	const __m128 M01 = _mm_mul_ps(EV01, PV01s);
	const __m128 M23 = _mm_mul_ps(EV23, PV23s);
	const __m128 R = _mm_hsub_ps(M23, M01);
	return R;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavPolyUtil::QuadContainsPointXzFast(const Point inPt, const Point* const __restrict pVerts)
{
	const __m128 R = QuadContainsPointCommon(inPt, pVerts);
	return _mm_testz_ps(R, R);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavPolyUtil::QuadContainsPointXz(const Point inPt, const Point* const __restrict pVerts, float epsilon /* = NDI_FLT_EPSILON */, Vec4* const __restrict pDotsOut /* = nullptr */)
{
	__m128 R = QuadContainsPointCommon(inPt, pVerts);
	if (pDotsOut)
		*pDotsOut = Vec4(R);

	R = _mm_add_ps(R, _mm_set1_ps(epsilon));
	return _mm_testz_ps(R, R);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavPolyUtil::BlockerContainsPointXzPs(Point_arg posPs, const NavMeshBlocker* pBlocker, const Locator& boxLocPs)
{
	if (!pBlocker)
		return false;

	bool inside = false;

	const float x = posPs.X();
	const float z = posPs.Z();

	for (U32F iV = 0; iV < pBlocker->m_numVerts; ++iV)
	{
		const U32F iVNext = (iV + 1) % pBlocker->m_numVerts;

		const Point& p0 = boxLocPs.TransformPoint(pBlocker->m_aVertsLs[iV]);
		const Point& p1 = boxLocPs.TransformPoint(pBlocker->m_aVertsLs[iVNext]);

		const float p0x = p0.X();
		const float p0z = p0.Z();
		const float p1x = p1.X();
		const float p1z = p1.Z();

		if ((((p1z <= z) && (z < p0z)) || ((p0z <= z) && (z < p1z)))
			&& (x < (p0x - p1x) * (z - p1z) / (p0z - p1z) + p1x))
		{
			inside = !inside;
		}
	}

	return inside;
}
