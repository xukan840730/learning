/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-poly-ex.h"

#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/nav-poly-util.h"

/// --------------------------------------------------------------------------------------------------------------- ///
NavPolyEx::NavPolyEx()
{
	m_pNext = nullptr;
	m_hOrgPoly = NavPolyHandle();

	m_id = kInvalidPolyExId;
	m_numVerts = 0;

	m_pathNodeId = NavPathNodeMgr::kInvalidNodeId;
	m_ownerPathNodeId = NavPathNodeMgr::kInvalidNodeId;

	for (U32F i = 0; i < NavPoly::kMaxVertexCount; ++i)
	{
		m_vertsLs[i] = kOrigin;
		m_adjPolys[i].Invalidate();
	}

	m_blockerBits.ClearAllBits();

	m_sourceFlags.m_u8 = 0;
	m_sourceBlockage = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavMesh* NavPolyEx::GetNavMesh() const
{
	return m_hOrgPoly.ToNavMesh();
}

/// --------------------------------------------------------------------------------------------------------------- ///
// NB: averaging the verts computes the centroid for triangles. It DOES NOT correctly compute
// the centroid for quads! For quads you must compute the pair of centroids of the two triangles obtained by subdividing
// the quad one way, then the other; then draw lines connecting each pair; then return the intersection of the two lines.
Point NavPolyEx::GetCentroid() const
{
	Point center;
	if (m_numVerts == 3)
	{
		center = AveragePos(GetVertex(0), GetVertex(1), GetVertex(2));
	}
	else
	{
		const Point p1 = AveragePos(GetVertex(0), GetVertex(1), GetVertex(2));
		const Point p2 = AveragePos(GetVertex(0), GetVertex(2), GetVertex(3));
		const Point p3 = AveragePos(GetVertex(0), GetVertex(1), GetVertex(3));
		const Point p4 = AveragePos(GetVertex(1), GetVertex(2), GetVertex(3));

		const Vector p13 = p1 - p3;
		const Vector p43 = p4 - p3;
		const Vector p21 = p2 - p1;

		const float d1343 = p13.X() * p43.X() + p13.Y() * p43.Y() + p13.Z() * p43.Z();
		const float d4321 = p43.X() * p21.X() + p43.Y() * p21.Y() + p43.Z() * p21.Z();
		const float d1321 = p13.X() * p21.X() + p13.Y() * p21.Y() + p13.Z() * p21.Z();
		const float d4343 = p43.X() * p43.X() + p43.Y() * p43.Y() + p43.Z() * p43.Z();
		const float d2121 = p21.X() * p21.X() + p21.Y() * p21.Y() + p21.Z() * p21.Z();

		const float denom = d2121 * d4343 - d4321 * d4321;
		const float absDenom = Abs(denom);

		if (absDenom < 1e-10f)
		{
#ifdef RHUAI
			ALWAYS_HALT();
#endif

			center = AveragePos(GetVertex(0), GetVertex(1), GetVertex(2), GetVertex(3));
		}
		else
		{
			const float t = (d1343 * d4321 - d1321 * d4343) / denom;

			center = p1 + t * p21;
		}
	}
	return center;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPolyEx::DebugDrawEdges(const Color& col,
							   const Color& colBoundary,
							   const Color& colBlocked,
							   float yOffset,
							   float lineWidth /* = 3.0f */,
							   DebugPrimTime tt /* = kPrimDuration1FrameAuto */,
							   const NavBlockerBits* pObeyedBlockers /* = nullptr */) const

{
	STRIP_IN_FINAL_BUILD;

	const NavMesh* pNavMesh = GetNavMesh();
	const Vector offset = Vector(kUnitYAxis) * yOffset;

	NavBlockerBits obeyedBlockers;
	if (pObeyedBlockers)
		obeyedBlockers = *pObeyedBlockers;
	else
		obeyedBlockers.SetAllBits();

	const bool isBlocker = IsBlockedBy(obeyedBlockers);

	for (U32F iEdge = 0; iEdge < GetVertexCount(); ++iEdge)
	{
		Color c;
		if (isBlocker)
		{
			c = colBlocked;
		}
		else
		{
			const NavManagerId adjId = GetAdjacentPolyId(iEdge);
			if (adjId == NavManagerId::kInvalidMgrId)
			{
				c = colBoundary;
			}
			else
			{
				c = col;
			}
		}

		const Point pt0 = pNavMesh->LocalToWorld(GetVertex(iEdge) + offset);
		const Point pt1 = pNavMesh->LocalToWorld(GetNextVertex(iEdge) + offset);

		g_prim.Draw(DebugLine(pt0, pt1, c, c, lineWidth, kPrimEnableHiddenLineAlpha), tt);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPolyEx::DebugDraw(const Color& col,
						  float yOffset /* = 0.0f */,
						  DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	const NavMesh* pNavMesh = GetNavMesh();
	const Vector vo = Vector(kUnitYAxis) * yOffset;

	for (U32F iV = 2; iV < GetVertexCount(); ++iV)
	{
		const Point pt0 = pNavMesh->LocalToWorld(GetVertex(0)) + vo;
		const Point pt1 = pNavMesh->LocalToWorld(GetVertex(iV - 1)) + vo;
		const Point pt2 = pNavMesh->LocalToWorld(GetVertex(iV)) + vo;

		g_prim.Draw(DebugTriangle(pt0, pt1, pt2, col, PrimAttrib(kPrimDisableDepthWrite)), tt);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavPolyEx::PolyContainsPointLs(Point_arg ptLs,
									Vec4* pDotsOut /* = nullptr */,
									float epsilon /* = NDI_FLT_EPSILON */) const
{
	return NavPolyUtil::QuadContainsPointXz(ptLs, m_vertsLs, epsilon, pDotsOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Scalar NavPolyEx::FindNearestPointLs(Point* pOutLs, Point_arg inPtLs) const
{
	const float bestDist = NavPolyUtil::FindNearestPoint(inPtLs, m_vertsLs, m_numVerts, pOutLs);

	ASSERT(PolyContainsPointLs(*pOutLs, nullptr, 0.01f));

	return bestDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Scalar NavPolyEx::FindNearestPointPs(Point* pOutPs, Point_arg inPtPs) const
{
	const NavMesh* pMesh = GetNavMesh();

	Point outLs;
	Scalar dist = FindNearestPointLs(&outLs, pMesh->ParentToLocal(inPtPs));
	*pOutPs = pMesh->LocalToParent(outLs);
	return dist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPolyEx::ProjectPointOntoSurfaceLs(Point* pOutLs, Vector* pNormalLs, Point_arg inPtLs) const
{
	NavPolyUtil::GetPointAndNormal(inPtLs, m_vertsLs, m_numVerts, pOutLs, pNormalLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPolyEx::ProjectPointOntoSurfacePs(Point* pOutPs, Vector* pNormalPs, Point_arg inPtPs) const
{
	const Vector localToParent = GetNavMesh()->LocalToParent(Point(kZero)) - kOrigin;

	Point inLs = inPtPs - localToParent;
	Point outLs;
	Vector normalLs;
	ProjectPointOntoSurfaceLs(&outLs, &normalLs, inLs);
	*pOutPs = outLs + localToParent;
	*pNormalPs = normalLs;  // rotate this if local nav mesh space can be rotated wrt parent space
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavPolyEx::IsBlockedBy(const NavBlockerBits& blockers) const
{
	NavBlockerBits masked;
	NavBlockerBits::BitwiseAnd(&masked, m_blockerBits, blockers);

	return !masked.AreAllBitsClear();
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <bool kSquared>
static inline float SignedDistPointPolyXzCommon(const NavPolyEx* const __restrict pPolyEx, const Point ptLs)
{
	const float* __restrict const v = pPolyEx->GetVertexArray();
	const __m128 V0 = _mm_loadu_ps(v + 0);
	const __m128 V1 = _mm_loadu_ps(v + 4);
	const __m128 V2 = _mm_loadu_ps(v + 8);
	const __m128 V3 = _mm_loadu_ps(v + 12);
	const __m128 A = ptLs.QuadwordValue();

	const __m128 dV1V0 = _mm_sub_ps(V1, V0);
	const __m128 dV2V1 = _mm_sub_ps(V2, V1);
	const __m128 dV3V2 = _mm_sub_ps(V3, V2);
	const __m128 dV0V3 = _mm_sub_ps(V0, V3);
	const __m128 dAV0 = _mm_sub_ps(A, V0);
	const __m128 dAV1 = _mm_sub_ps(A, V1);
	const __m128 dAV2 = _mm_sub_ps(A, V2);
	const __m128 dAV3 = _mm_sub_ps(A, V3);
	const __m128 dV1V0dV2V1 = _mm_shuffle_ps(dV1V0, dV2V1, 136); // v1x-v0x, v1z-v0z, v2x-v1x, v2z-v1z
	const __m128 dV3V2dV0V3 = _mm_shuffle_ps(dV3V2, dV0V3, 136); // v3x-v2x, v3z-v2z, v0x-v3x, v0z-v3z
	const __m128 dV1V0dV2V1Sqr = _mm_mul_ps(dV1V0dV2V1, dV1V0dV2V1);
	const __m128 dV3V2dV0V3Sqr = _mm_mul_ps(dV3V2dV0V3, dV3V2dV0V3);
	const __m128 dSqrSums = _mm_hadd_ps(dV1V0dV2V1Sqr, dV3V2dV0V3Sqr);
	const __m128 dSqrInvs = _mm_rcp_ps(dSqrSums);
	const __m128 dV0AdV1A = _mm_shuffle_ps(dAV0, dAV1, 136); // ax-v0x, az-v0z, ax-v1x, az-v1z
	const __m128 dV2AdV3A = _mm_shuffle_ps(dAV2, dAV3, 136); // ax-v2x, az-v2z, ax-v3x, az-v3z
	const __m128 dV0AdV1AMuldV1V0dV2V1 = _mm_mul_ps(dV0AdV1A, dV1V0dV2V1);
	const __m128 dV2AdV3AMuldV3V2dV0V3 = _mm_mul_ps(dV2AdV3A, dV3V2dV0V3);
	const __m128 nums = _mm_hadd_ps(dV0AdV1AMuldV1V0dV2V1, dV2AdV3AMuldV3V2dV0V3);

	// ts of all 4 edges of the poly
	const __m128 ts = _mm_min_ps(_mm_max_ps(_mm_mul_ps(nums, dSqrInvs), _mm_setzero_ps()), _mm_set_ps1(1.0f));

	const __m128 ta = _mm_unpacklo_ps(ts, ts);
	const __m128 tb = _mm_unpackhi_ps(ts, ts);
	const __m128 closestPts01_12 = _mm_mul_ps(ta, dV1V0dV2V1);
	const __m128 closestPts23_30 = _mm_mul_ps(tb, dV3V2dV0V3);
	const __m128 dists01_12 = _mm_sub_ps(closestPts01_12, dV0AdV1A);
	const __m128 dists02_23 = _mm_sub_ps(closestPts23_30, dV2AdV3A);
	const __m128 dists01_12Sqr = _mm_mul_ps(dists01_12, dists01_12);
	const __m128 dists02_23Sqr = _mm_mul_ps(dists02_23, dists02_23);
	const __m128 distSqrs = _mm_hadd_ps(dists01_12Sqr, dists02_23Sqr);
	const __m128 minDistSqrInterm = _mm_min_ps(distSqrs, _mm_shuffle_ps(distSqrs, distSqrs, 78));
	__m128 minDistSqr = _mm_min_ss(minDistSqrInterm, _mm_movehdup_ps(minDistSqrInterm));

	// min dist found (positive). now sign it by checking whether we're inside the poly
	const __m128 PV01s = _mm_shuffle_ps(dAV0, dAV1, 34); // az-v0z, ax-v0x, az-v1z, ax-v1x
	const __m128 PV23s = _mm_shuffle_ps(dAV2, dAV3, 34); // az-v2z, ax-v2x, az-v3z, ax-v3x
	const __m128 M01 = _mm_mul_ps(dV1V0dV2V1, PV01s); // (v1x-v0x)*(az-v0z), (v1z-v0z)*(ax-v0x), (v2x-v1x)*(az-v1z), (v2z-v1z)*(ax-v1x)
	const __m128 M23 = _mm_mul_ps(dV3V2dV0V3, PV23s); // (v3x-v2x)*(az-v2z), (v3z-v2z)*(ax-v2x), (v0x-v3x)*(az-v3z), (v0z-v3z)*(ax-v3x)

	// crosses
	//   (v1x-v0x)*(az-v0z), (v2x-v1x)*(az-v1z), (v3x-v2x)*(az-v2z), (v0x-v3x)*(az-v3z)
	// - (v1z-v0z)*(ax-v0x), (v2z-v1z)*(ax-v1x), (v3z-v2z)*(ax-v2x), (v0z-v3z)*(ax-v3x)
	// -------------------------------------------------------------------------------------
	const __m128 R = _mm_hsub_ps(M01, M23);

	// ensure that zero-length segments do not disqualify a point from counting as inside
	// note that entries in M01 and M23 can be 0.0f OR -0.0f, so simply reversing the tests
	// to match one desired sign state or the other does not suffice
	const __m128 Rinterm = _mm_or_ps(R, _mm_cmp_ps(R, _mm_setzero_ps(), _CMP_EQ_OQ));
	const __m128 Rinterm2 = _mm_and_ps(Rinterm, _mm_shuffle_ps(Rinterm, Rinterm, 78));
	const __m128 Rinterm3 = _mm_and_ps(Rinterm2, _mm_movehdup_ps(Rinterm2));

	if (!kSquared)
		minDistSqr = _mm_sqrt_ss(minDistSqr);

	// negate result if we're inside the poly
	return _mm_cvtss_f32(_mm_blendv_ps(minDistSqr, _mm_sub_ss(_mm_set_ss(0.0f), minDistSqr), Rinterm3));
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NavPolyEx::SignedDistPointPolyXzSqr(Point_arg ptLs) const
{
	return SignedDistPointPolyXzCommon<true>(this, ptLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NavPolyEx::SignedDistPointPolyXz(Point_arg ptLs) const
{
	return SignedDistPointPolyXzCommon<false>(this, ptLs);
}
