/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "corelib/util/bigsort.h"
#include "ndlib/process/debug-selection.h"
#include "gamelib/ndphys/rope/rope2-point-col.h"
#include "gamelib/ndphys/rope/rope-mgr.h"
#include "gamelib/ndphys/rigid-body.h"
#include "gamelib/ndphys/havok-util.h"
#include "gamelib/ndphys/havok-internal.h"

#include <Physics/Physics/Collide/Shape/Composite/Compound/hknpCompoundShape.h>
#include <Physics/Physics/Collide/Shape/Convex/Polytope/hknpConvexPolytopeShape.h>

static const F32 kPlaneNormDistTol = 0.05f;
static const F32 kPlaneNormDistTol2 = kPlaneNormDistTol * kPlaneNormDistTol;
static const F32 kPlaneDistTol = 0.01f;
static const F32 kPlaneDistTol2 = kPlaneDistTol * kPlaneDistTol;
static const Vector kPlaneDistTolVec(kPlaneDistTol, kPlaneDistTol, kPlaneDistTol);

struct RopePointEdge
{
	Point m_vtx0;
	Point m_vtx1;
};

class RopePointEdgeList
{
public:
	enum { kMaxNumEdges = 16 };

	U32 m_numEdges;
	RopePointEdge m_edges[kMaxNumEdges];

	RopePointEdgeList() : m_numEdges(0) {}

	void AddEdge(Point_arg vtx0, Point_arg& vtx1)
	{
		if (m_numEdges >= kMaxNumEdges)
		{
			JAROS_ASSERT(false);
			return;
		}
		RopePointEdge& edge = m_edges[m_numEdges];
		edge.m_vtx0 = vtx0;
		edge.m_vtx1 = vtx1;
		m_numEdges++;
	}
};

struct RopeEdgeSeg
{
	F32 m_tA;
	F32 m_tB;
};

class RopeEdgeSegments
{
public:
	enum { kMaxNumSegs = 16 };

	Point m_p;
	Vector m_dir;
	U32 m_numSegs;
	RopeEdgeSeg m_seg[kMaxNumSegs];

	RopeEdgeSegments(const Point& p, const Vector& dir) 
		: m_p(p)
		, m_dir(dir)
		, m_numSegs(0) 
	{}

	void AddSeg(F32 tA, F32 tB)
	{
		if (m_numSegs >= kMaxNumSegs)
		{
			JAROS_ASSERT(false);
			return;
		}
		RopeEdgeSeg& seg = m_seg[m_numSegs];
		seg.m_tA = tA;
		seg.m_tB = tB;
		m_numSegs++;
	}

	void RemoveSeg(U32 ii)
	{
		m_seg[ii] = m_seg[m_numSegs-1];
		m_numSegs--;
	}

	F32 Dist(F32 t) const
	{
		F32 dist = FLT_MAX;
		for (U32F ii = 0; ii<m_numSegs; ii++)
		{
			F32 d = t<m_seg[ii].m_tA ? m_seg[ii].m_tA-t : (t>m_seg[ii].m_tB ? t-m_seg[ii].m_tB : 0.0f);
			dist = Min(d, dist);
		}
		return dist;
	}
};


bool GetPlaneIntersectionClipped(const Vec4& pl0, const Vec4& pl1, const Aabb& aabb, Point& outA, Point& outB)
{
	// Direction of the plane intersection line
	Vector norm0 = Vector(pl0);
	Vector norm1 = Vector(pl1);
	Vector intDir = Cross(norm0, norm1);
	Scalar intVecLen;
	intDir = Normalize(intDir, intVecLen);
	if (intVecLen < 0.01f)
	{
		return false;
	}

	Vector biNorm0 = Cross(intDir, norm0);
	Vector biNorm1 = Cross(norm1, intDir);
	Point intP;
	{
		// Find point on the intersection line
		Vector pl0P = -pl0.W() * norm0;
		Scalar d1 = pl1.W();
		Scalar t = - (Dot(pl0P, norm1) + d1) / Dot(norm1, biNorm0);
		intP = kOrigin + pl0P + t * biNorm0;
	}

	Scalar scTmin, scTmax;
	if (!aabb.IntersectLine(intP, intDir, scTmin, scTmax))
	{
		return false;
	}

	outA = intP + scTmin * intDir;
	outB = intP + scTmax * intDir;

	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//

Rope2PointCol::Rope2PointCol()
	: m_numTris(0)
	, m_numPlanes(0)
{
}

// static
bool Rope2PointCol::ArePlanesSimilar(const Vec4& pl0, const Vec4& pl1, const Point& refPos)
{
	F32 normDist2 = Length3Sqr(pl0 - pl1);
	if (normDist2 > kPlaneNormDistTol2)
	{
		return false;
	}

	F32 dist0 = Dot4(pl0, refPos.GetVec4());
	F32 dist1 = Dot4(pl1, refPos.GetVec4());
	if (Abs(dist1-dist0) > kPlaneDistTol)
	{
		return false;
	}

	return true;
}

bool g_debugCollisionGeoOverflow = false;

void Rope2PointCol::AddTriangle(I16 triIndex, Vec4_arg plane, Point_arg pt0, Point_arg pt1, Point_arg pt2, const RopeColliderHandle& hCollider)
{
	if (m_numTris >= kMaxNumTris)
	{
		if (g_debugCollisionGeoOverflow)
			JAROS_ASSERT(false);
		return;
	}

	Tri& tri = m_tris[m_numTris];
	m_numTris++;

	tri.m_triIndex = triIndex;
	tri.m_isConvexFace = false;

	for (U32F iPlane = 0; iPlane<m_numPlanes; iPlane++)
	{
		Plane& pl = m_planes[iPlane];

		if (pl.m_isConvexFace)
		{
			continue;
		}

		if (!RopeColliderHandle::AreCollidersAttached(pl.m_hCollider, hCollider))
		{
			continue;
		}

		F32 normDist2 = Length3Sqr(pl.m_plane - plane);
		if (normDist2 > kPlaneNormDistTol2)
		{
			continue;
		}

		Vector ptDist = Vector(Dot4(pl.m_plane, pt0.GetVec4()), Dot4(pl.m_plane, pt1.GetVec4()), Dot4(pl.m_plane, pt2.GetVec4()));
		if (!AllComponentsLessThan(Abs(ptDist), kPlaneDistTolVec))
		{
			continue;
		}

		// Found a similar plane
		tri.m_planeIndex = iPlane;

		// But if this one is "in front" of the other, replace
		if (ptDist.X() + ptDist.Y() + ptDist.Z() > 0.0f)
		{
			pl.m_plane = plane;
			pl.m_hCollider = hCollider;
		}
		return;
	}

	if (m_numPlanes >= kMaxNumPlanes)
	{
		if (g_debugCollisionGeoOverflow)
			JAROS_ASSERT(false);
		m_numTris--;
		return;
	}

	Plane& pl = m_planes[m_numPlanes];
	tri.m_planeIndex = m_numPlanes;
	m_numPlanes++;

	pl.m_plane = plane;
	pl.m_hCollider = hCollider;
	pl.m_dist = FLT_MAX;
	pl.m_startIndex = m_numPlanes-1;
	pl.m_isConvexFace = false;
}

void Rope2PointCol::AddConvexFace(Vec4_arg plane, I16 faceId, const RopeColliderHandle& hCollider)
{
	if (m_numTris >= kMaxNumTris)
	{
		if (g_debugCollisionGeoOverflow)
			JAROS_ASSERT(false);
		return;
	}

	Tri& tri = m_tris[m_numTris];
	m_numTris++;

	tri.m_triIndex = faceId;
	tri.m_isConvexFace = true;

	if (m_numPlanes >= kMaxNumPlanes)
	{
		if (g_debugCollisionGeoOverflow)
			JAROS_ASSERT(false);
		m_numTris--;
		return;
	}

	Plane& pl = m_planes[m_numPlanes];
	tri.m_planeIndex = m_numPlanes;
	m_numPlanes++;

	pl.m_plane = plane;
	pl.m_hCollider = hCollider;
	pl.m_dist = FLT_MAX;
	pl.m_startIndex = m_numPlanes-1;
	pl.m_isConvexFace = true;
}

#if 0
F32 PointInPlaneTriangleDistance2(const Point& P, const Point* pT, const Vector& norm)
{
	I32 vtx = -1;
	for (I32 ii = 0; ii<3; ii++)
	{
		I32 iNext = (ii+1)%3;
		Vector v = pT[iNext] - pT[ii];
		Vector bi = Cross(norm, v);
		if (Dot(bi, P-pT[ii]) < 0.0f)
		{
			F32 s = Dot(v, P-pT[ii]);
			if (s < 0.0f)
			{
				if (vtx == -1 | vtx == iNext)
					vtx = ii;
			}
			else if (s > 1.0f)
			{
				vtx = iNext;
			}
			else
			{
				// Closest point is on this edge
				return DistSqr(P, Lerp(pT[ii], pT[iNext], s));
			}
		}
	}
	if (vtx == -1)
	{
		// Inside
		return 0.0f;
	}
	// Closest point is vertex
	return DistSqr(pT[vtx], P);
}
#endif

F32 PointTriangleDistance(const Point& P, const Point* pT, const Vector& norm, F32& normFactor)
{
	Vector v[3];
	Vector e[3];
	Scalar eLen[3];
	Vector bi[3];
	v[0] = P - pT[0];
	e[0] = Normalize(pT[1] - pT[0], eLen[0]);
	bi[0] = Cross(norm, e[0]);
	v[1] = P - pT[1];
	e[1] = Normalize(pT[2] - pT[1], eLen[1]);
	bi[1] = Cross(norm, e[1]);
	v[2] = P - pT[2];
	e[2] = Normalize(pT[0] - pT[2], eLen[2]);
	bi[2] = Cross(norm, e[2]);

	F32 distNorm = Dot(v[0], norm);

	Vector bs = Vector(Dot(bi[0], v[0]), Dot(bi[1], v[1]), Dot(bi[2], v[2]));
	U32 insideMask = Simd::MoveMask32(Simd::CompareGE(bs.QuadwordValue(), Simd::GetVecAllZero())) & 7;
	if (insideMask == 7)
	{
		// Inside
		normFactor = Sign(distNorm) * 1.0f;
		return Abs(distNorm);
	}

	Vector s = Vector(Dot(e[0], v[0]), Dot(e[1], v[1]), Dot(e[2], v[2]));
	Simd::Mask sPositive = Simd::CompareGT(s.QuadwordValue(), Simd::GetVecAllZero());
	Simd::VF32 eLenXYZ = Simd::SetXYZ(eLen[0].QuadwordValue(), eLen[1].QuadwordValue(), eLen[2].QuadwordValue());
	Simd::Mask sLessThanELen = Simd::CompareLT(s.QuadwordValue(), eLenXYZ);

	U32 edgeMask = (Simd::MoveMask32(Simd::And(sPositive, sLessThanELen)) & ~insideMask) & 7;
	if (edgeMask)
	{
		// Edge
		// ASSERT(edgeMask == 1 || edgeMask == 2 || edgeMask == 4);		// Unfortunately happens occasionally due to numerical presicion
		U32 iEdge = (edgeMask >> 1) & 3;
		F32 dist = Sqrt(Sqr(distNorm) + Sqr(bs[iEdge]));
		normFactor = dist > 0.0f ? distNorm / dist : 1.0f;
		return dist;
	}

	// Vertex
	U32 vtxMask = Simd::MoveMask32(Simd::Not(Simd::Or(sPositive, Simd::ShuffleZXYW(sLessThanELen)))) & 7;
	//ASSERT(vtxMask == 1 || vtxMask == 2 || vtxMask == 4); // Unfortunately happens occasionally due to numerical precision
	if (vtxMask == 0)
	{
		// We are close to a vertex but failed due to numerical imprecision. Just pretend it's inside
		normFactor = 1.0f;
		return distNorm;
	}
	U32 iVtx = (vtxMask >> 1) & 3;
	F32 dist = Dist(P, pT[iVtx]);
	normFactor = dist > 0.0f ? distNorm / dist : 1.0f;
	return dist;
}

F32 PointPolyDistance(const Point& P, const Point* pVtx, U32 numVtx, const Vector& norm, F32 convexRadius, F32& normFactor)
{
	bool inside = true;
	bool outsideFwd = false;
	bool outSideBwdStart = false;
	Point closestP;
	bool found = false;
	for (U32 ii = 0; ii<numVtx; ii++)
	{
		Vector v = P - pVtx[ii];
		Scalar eLen;
		Vector e = Normalize(pVtx[(ii+1)%numVtx] - pVtx[ii], eLen);
		Vector bi = Cross(norm, e);
		if (Dot(bi, v) < 0.0f)
		{
			inside = false;
			Scalar eDot = Dot(e, v);
			if (eDot < 0.0f)
			{
				if (ii > 0)
				{
					closestP = pVtx[ii];
					found = true;
					break;
				}
				outSideBwdStart = true;
			}
			else if (eDot > eLen)
			{
				outsideFwd = true;
			}
			else
			{
				closestP = pVtx[ii]+eDot*e;
				found = true;
				break;
			}
		}
		else if (outsideFwd)
		{
			closestP = pVtx[ii];
			found = true;
			break;
		}
	}

	if (inside)
	{
		F32 dist = Dot(P-pVtx[0], norm) - convexRadius;
		normFactor = Sign(dist) * 1.0f;
		return Abs(dist);
	}

	if (!found)
	{
		ASSERT(outsideFwd || outSideBwdStart);
		closestP = pVtx[0];
	}

	Vector v = P - closestP;
	Scalar dt = Dot(v, norm);
	if (dt >= 0.0f)
	{
		Scalar d = Length(v);
		normFactor = d > Scalar(kZero) ? (F32)(dt/d) : 1.0f;
		return Abs(d - convexRadius);
	}

	// The most obscure case
	// Handling the convexRadius is just the best guess here
	v -= dt * norm;
	Scalar d;
	v = SafeNormalize(v, norm, d);
	closestP += v * Min(convexRadius, (F32)d);
	normFactor = 0.0f;
	return Dist(closestP, P);
}

int ComparePlaneDist(const Rope2PointCol::Plane& a, const Rope2PointCol::Plane& b)
{
	if (Abs(a.m_dist - b.m_dist) < 0.0005f)
	{
		// If 2 planes are at the same distance, the one that has higher norm factor goes first
		// That is if point is close to the edge of 2 planes the one where point collides closer to normal direction has preference
		// This to avoid problem of colliding with tiny edges (sub plane dist tolerance) in otherwise planar surface
		return a.m_normFactor > b.m_normFactor ? -1 : +1;
	}
	return a.m_dist < b.m_dist ? -1 : +1;
}

void Rope2PointCol::GenerateConstraints(Rope2* pRope, U32 iBead)
{
	//const Point& refPos = pRope->m_pPos[iBead];
	const Point& refPos = pRope->m_pLastPos[iBead];
	const Aabb& aabb = pRope->m_pNodeAabb[iBead];
	Rope2::Constraint& con = pRope->m_pConstr[iBead];
	const RopeColCache& colCache = pRope->m_colCache;

	if (m_numPlanes == 0)
		return;

	if (m_numPlanes == 1)
	{
		if (pRope->CheckConstraintPlane(iBead, m_planes[0].m_plane))
		{
			bool edgePat = false;
			con.AddPlane(m_planes[0].m_plane, m_planes[0].m_hCollider);
			// Check some PAT flags
			for (U32 iTri = 0; iTri<m_numTris; iTri++)
			{
				const Tri& tri = m_tris[iTri];
				Pat pat;
				if (tri.m_isConvexFace)
				{
					const hknpShape* pShape = m_planes[0].m_hCollider.GetShape(pRope);
					if (m_planes[0].m_hCollider.GetListIndex() >= 0)
					{
						PHYSICS_ASSERT(pShape && pShape->getType() == hknpShapeType::COMPOUND);
						const hknpCompoundShape* pCompShape = static_cast<const hknpCompoundShape*>(pShape);
						const hknpShapeInstance& inst = pCompShape->getInstance(hknpShapeInstanceId(m_planes[0].m_hCollider.GetListIndex()));
						pShape = inst.getShape();
					}
					pat.m_bits = pShape->m_userData;
				}
				else
				{
					const RopeColTri& colTri = colCache.m_pTris[tri.m_triIndex];
					const RopeColliderHandle& colShape = colCache.m_pShapes[colTri.m_shapeIndex];
					if (const RigidBody* pBody = colShape.GetRigidBody())
					{
						pat = HavokGetPatFromHkRigidBody(pBody->GetHavokBody(), colTri.m_key);
					}
				}
				if (pat.GetCanGrapple())
				{
					con.m_canGrappleFlags |= 1;
				}
				if (pat.GetSurfaceType() == Pat::kSurfaceTypeSnowDeep)
				{
					con.m_snowFlags |= 1;
				}
				else if (pat.GetSurfaceType() == Pat::kSurfaceTypeFrictionless)
				{
					con.m_frictionlessFlags |= 1;
				}

				if (pat.GetSurfaceType() != 0)
				{
					con.m_patSurface = pat.GetSurfaceType();
				}
			}
		}
		return;
	}

#if defined(JSINECKY) && defined(_DEBUG)
	if (DebugSelection::Get().IsProcessOrNoneSelected(pRope->m_pOwner) && g_ropeMgr.m_debugDrawNodes && g_ropeMgr.m_debugDrawSelectedIndex && iBead == g_ropeMgr.m_selectedRopeIndex)
	{
		printf("Break here\n");
	}
#endif

	ScopedTempAllocator jjAlloc(FILE_LINE_FUNC);

	static const U32 kVtxBufSize = 32;
	Point* pVtxBuf = NDI_NEW Point[kVtxBufSize];

	// Calculate distance of each plane from refPnt
	for (U32 ii = 0; ii<m_numTris; ii++)
	{
		const Tri& tri = m_tris[ii];
		if (tri.m_isConvexFace)
		{
			const RopeColliderHandle& hCollider = m_planes[tri.m_planeIndex].m_hCollider;
			Locator loc = hCollider.GetLocator(pRope);
			const hknpShape* pShape = hCollider.GetShape(pRope);
			if (hCollider.GetListIndex() >= 0)
			{
				PHYSICS_ASSERT(pShape && pShape->getType() == hknpShapeType::COMPOUND);
				const hknpCompoundShape* pCompShape = static_cast<const hknpCompoundShape*>(pShape);
				const hknpShapeInstance& inst = pCompShape->getInstance(hknpShapeInstanceId(hCollider.GetListIndex()));
				pShape = inst.getShape();
				loc = loc.TransformLocator(LocatorFromHkTransform(inst.getTransform()));
			}
			PHYSICS_ASSERT(pShape && pShape->asConvexPolytopeShape());
			const hknpConvexPolytopeShape* pPolyShape = pShape->asConvexPolytopeShape();
			hknpConvexPolytopeShape::Face face = pPolyShape->getFaces()[tri.m_triIndex];
			const hkRelArray<hknpConvexShape::VertexIndex>& vtxIndices = pPolyShape->getFaceVertexIndices();
			const hkcdVertex* pVertices = pPolyShape->getVertices().begin();
			Vector norm(pPolyShape->getPlanes()[tri.m_triIndex].getQuad());

			PHYSICS_ASSERT(face.m_numIndices < kVtxBufSize);
			for (U32 iFaceVtx = 0; iFaceVtx<face.m_numIndices; iFaceVtx++)
			{
				pVtxBuf[iFaceVtx] = Point(pVertices[vtxIndices[face.m_firstIndex+iFaceVtx]].getQuad());
			}

			F32 normFactor;
			F32 dist = PointPolyDistance(loc.UntransformPoint(refPos), pVtxBuf, face.m_numIndices, norm, pPolyShape->m_convexRadius, normFactor);
			Plane& pl = m_planes[tri.m_planeIndex];
			if (dist < pl.m_dist)
			{
				pl.m_dist = dist;
				pl.m_normFactor = normFactor;
			}
		}
		else
		{
			const RopeColTri& colTri = colCache.m_pTris[tri.m_triIndex];
			Plane& pl = m_planes[tri.m_planeIndex];

			F32 normFactor;
			F32 dist = PointTriangleDistance(refPos, colTri.m_pnt, Vector(pl.m_plane), normFactor);
			if (dist < pl.m_dist)
			{
				pl.m_dist = dist;
				pl.m_normFactor = normFactor;
			}
		}
	}

	// Sort planes by distance
	QuickSortStack(m_planes, m_numPlanes, ComparePlaneDist);

	// Need to fix plane indices in tris after sorting
	{
		U32 planeRemap[kMaxNumPlanes];
		for (U32 ii = 0; ii<m_numPlanes; ii++)
		{
			planeRemap[m_planes[ii].m_startIndex] = ii;
		}
		for (U32 iTri = 0; iTri<m_numTris; iTri++)
		{
			Tri& tri = m_tris[iTri];
			tri.m_planeIndex = planeRemap[tri.m_planeIndex];
		}
	}

	// Create list of convex edges for each plane
	RopePointEdgeList* pEdgeList = NDI_NEW RopePointEdgeList[m_numPlanes];
	for (U32 ii = 0; ii<m_numTris; ii++)
	{
		const Tri& tri = m_tris[ii];
		if (!tri.m_isConvexFace)
		{
			const RopeColTri& colTri = colCache.m_pTris[tri.m_triIndex];

			if (colTri.m_outerEdge0)
			{
				pEdgeList[tri.m_planeIndex].AddEdge(colTri.m_pnt[0], colTri.m_pnt[1]);
			}
			if (colTri.m_outerEdge1)
			{
				pEdgeList[tri.m_planeIndex].AddEdge(colTri.m_pnt[1], colTri.m_pnt[2]);
			}
			if (colTri.m_outerEdge2)
			{
				pEdgeList[tri.m_planeIndex].AddEdge(colTri.m_pnt[2], colTri.m_pnt[0]);
			}
		}
	}

#if defined(JSINECKY) && defined(_DEBUG)
	if (DebugSelection::Get().IsProcessOrNoneSelected(pRope->m_pOwner) && g_ropeMgr.m_debugDrawNodes && g_ropeMgr.m_debugDrawSelectedIndex && iBead == g_ropeMgr.m_selectedRopeIndex)
	{
		printf("Break here\n");
	}
#endif

	// Plane relationships
	Relationship planeRel[kMaxNumPlanes][kMaxNumPlanes];
	memset(planeRel, 0, sizeof(planeRel));
	bool badPlane[kMaxNumPlanes];
	memset(badPlane, 0, sizeof(badPlane));

	for (U32 iTri = 0; iTri<m_numTris; iTri++)
	{
		const Tri& tri = m_tris[iTri];
		Vec4 triVtx[3];
		if (tri.m_isConvexFace)
		{
			const RopeColliderHandle& hCollider = m_planes[tri.m_planeIndex].m_hCollider;
			Locator loc = hCollider.GetLocator(pRope);
			const hknpShape* pShape = hCollider.GetShape(pRope);
			if (hCollider.GetListIndex() >= 0)
			{
				PHYSICS_ASSERT(pShape && pShape->getType() == hknpShapeType::COMPOUND);
				const hknpCompoundShape* pCompShape = static_cast<const hknpCompoundShape*>(pShape);
				const hknpShapeInstance& inst = pCompShape->getInstance(hknpShapeInstanceId(hCollider.GetListIndex()));
				pShape = inst.getShape();
				loc = loc.TransformLocator(LocatorFromHkTransform(inst.getTransform()));
			}
			PHYSICS_ASSERT(pShape && pShape->asConvexPolytopeShape());
			const hknpConvexPolytopeShape* pPolyShape = pShape->asConvexPolytopeShape();
			hknpConvexPolytopeShape::Face face = pPolyShape->getFaces()[tri.m_triIndex];
			const hkRelArray<hknpConvexShape::VertexIndex>& vtxIndices = pPolyShape->getFaceVertexIndices();
			const hkcdVertex* pVertices = pPolyShape->getVertices().begin();
			Vector convexRadiusShift = Vector(pPolyShape->getPlanes()[tri.m_triIndex].getQuad()) * pPolyShape->m_convexRadius;

			PHYSICS_ASSERT(face.m_numIndices < kVtxBufSize);
			for (U32 iFaceVtx = 0; iFaceVtx<face.m_numIndices; iFaceVtx++)
			{
				pVtxBuf[iFaceVtx] = loc.TransformPoint(Point(pVertices[vtxIndices[face.m_firstIndex+iFaceVtx]].getQuad()) + convexRadiusShift);
			}

			for (U32 iPlane = 0; iPlane<m_numPlanes; iPlane++)
			{
				if (iPlane == tri.m_planeIndex)
					continue;

				const Plane& pl = m_planes[iPlane];
				if (pl.m_hCollider == hCollider)
				{
					planeRel[tri.m_planeIndex][iPlane] = ConvexEdge;
					continue;
				}

				for (U32 iFaceVtx = 0; iFaceVtx<face.m_numIndices; iFaceVtx++)
				{
					F32 ptDist = Dot4(pl.m_plane, pVtxBuf[iFaceVtx].GetVec4());
					if (ptDist >= kPlaneDistTol)
					{
						planeRel[tri.m_planeIndex][iPlane] = InFront;
						break;
					}
				}
			}
		}
		else
		{
			const RopeColTri& colTri = colCache.m_pTris[tri.m_triIndex];
			triVtx[0] = colTri.m_pnt[0].GetVec4();
			triVtx[1] = colTri.m_pnt[1].GetVec4();
			triVtx[2] = colTri.m_pnt[2].GetVec4();

			for (U32 iPlane = 0; iPlane<m_numPlanes; iPlane++)
			{
				if (iPlane == tri.m_planeIndex)
					continue;

				const Plane& pl = m_planes[iPlane];

				Vector ptDist = Vector(Dot4(pl.m_plane, triVtx[0]), Dot4(pl.m_plane, triVtx[1]), Dot4(pl.m_plane, triVtx[2]));
				if (AllComponentsLessThan(ptDist, kPlaneDistTolVec))
				{
					continue;
				}

				planeRel[tri.m_planeIndex][iPlane] = InFront;
			}
		}
	}

	for (U32 ii = 0; ii<m_numPlanes-1; ii++)
	{
		Plane& pl = m_planes[ii];
		for (U32 jj = ii+1; jj<m_numPlanes; jj++)
		{
			Plane& pl2 = m_planes[jj];

			if (planeRel[ii][jj] == ConvexEdge)
			{
				PHYSICS_ASSERT(planeRel[jj][ii] == ConvexEdge);
				continue;
			}
			else if (planeRel[ii][jj] == InFront && planeRel[jj][ii] == InFront)
			{
				planeRel[ii][jj] = Concave;
				planeRel[jj][ii] = Concave;
				continue;
			}
			else if (planeRel[ii][jj] == InFront)
			{
				planeRel[jj][ii] = Behind;
			}
			else if (planeRel[jj][ii] == InFront)
			{
				planeRel[ii][jj] = Behind;
			}
			else
			{
				planeRel[ii][jj] = Convex;
				planeRel[jj][ii] = Convex;
			}

			// Look for shared convex edges
			do
			{
				if (!RopeColliderHandle::AreCollidersAttached(pl.m_hCollider, pl2.m_hCollider))
				{
					break;
				}

				// Direction of the plane intersection line
				Vector norm = Vector(pl.m_plane);
				Vector norm2 = Vector(pl2.m_plane);
				Vector intDir = Cross(norm, norm2);
				Scalar intVecLen;
				intDir = Normalize(intDir, intVecLen);
				if (intVecLen < 0.01f)
				{
					break;
				}

				// Look for edges in this plane that lie in the other plane and project them onto the intersection vector
				F32 edgeTs[RopePointEdgeList::kMaxNumEdges*2];
				U32 numTs = 0;
				for (U32 iEdge = 0; iEdge<pEdgeList[ii].m_numEdges; iEdge++)
				{
					RopePointEdge& edge = pEdgeList[ii].m_edges[iEdge];
					Vector ptDist = Vector(Dot4(pl2.m_plane, edge.m_vtx0.GetVec4()), Dot4(pl2.m_plane, edge.m_vtx1.GetVec4()), 0.0f);
					if (!AllComponentsLessThan(Abs(ptDist), kPlaneDistTolVec))
					{
						continue;
					}

					edgeTs[numTs*2] = Dot(intDir, edge.m_vtx0-kOrigin);
					edgeTs[numTs*2+1] = Dot(intDir, edge.m_vtx1-kOrigin);
					numTs++;
				}

				if (numTs == 0)
				{
					break;
				}

				// Look for edges in the other plane that lie in this plane and project them onto the intersection vector
				F32 edge2Ts[RopePointEdgeList::kMaxNumEdges*2];
				U32 num2Ts = 0;
				for (U32 jEdge = 0; jEdge<pEdgeList[jj].m_numEdges; jEdge++)
				{
					RopePointEdge& edge = pEdgeList[jj].m_edges[jEdge];
					Vector ptDist = Vector(Dot4(pl.m_plane, edge.m_vtx0.GetVec4()), Dot4(pl.m_plane, edge.m_vtx1.GetVec4()), 0.0f);
					if (!AllComponentsLessThan(Abs(ptDist), kPlaneDistTolVec))
					{
						continue;
					}

					edge2Ts[num2Ts*2] = Dot(intDir, edge.m_vtx0-kOrigin);
					edge2Ts[num2Ts*2+1] = Dot(intDir, edge.m_vtx1-kOrigin);
					num2Ts++;
				}

				if (num2Ts == 0)
				{
					break;
				}

				Vector biNorm = Cross(intDir, norm);
				Vector biNorm2 = Cross(norm2, intDir);
				Point intP;
				{
					// Find point on the intersection line
					Vector pl0P = -pl.m_plane.W() * norm;
					Scalar d1 = pl2.m_plane.W();
					Scalar t = - (Dot(pl0P, norm2) + d1) / Dot(norm2, biNorm);
					intP = kOrigin + pl0P + t * biNorm;
				}

				// Check for any pair of projected edges that go in opposite direction and overlap
				for (U32F iEdge = 0; iEdge<numTs; iEdge++)
				{
					for (U32F jEdge = 0; jEdge<num2Ts; jEdge++)
					{
						F32 tA = edgeTs[iEdge*2];
						F32 tB = edgeTs[iEdge*2+1];
						F32 t2A = edge2Ts[jEdge*2];
						F32 t2B = edge2Ts[jEdge*2+1];
						F32 s = Sign(tB-tA);
						if (s*(t2B-t2A)>=0.0f || s*(tB-t2B)<=0.0f || s*(t2A-tA)<=0.0f)
						{
							// edge segments don't match
							continue;
						}

						// We have a piece of a common convex edge
						if (s > 0.0f)
						{
							tA = Max(tA, t2B);
							tB = Min(tB, t2A);
						}
						else
						{
							tA = Max(tB, t2A);
							tB = Min(tA, t2B);
						}

						Scalar scTmin, scTmax;
						if (!aabb.IntersectLine(intP, intDir, scTmin, scTmax))
							continue;

						tA = Max(tA, (F32)scTmin);
						tB = Min(tB, (F32)scTmax);

						if (tB-tA <= 0.0f)
						{
							// Edge segment not in our aabb
							continue;
						}

						// Now clip the segment by triangle from each of the two planes to see if this is edge that is actually covered by the plane

						// Shift tol in the direction of edge biVector to prevent getting clipped by valid edge triangles
						Vector tolShift = s * kPlaneDistTol * biNorm;

						RopeEdgeSegments edgeSeg(intP + tolShift, intDir);
						edgeSeg.AddSeg(tA, tB);

						F32 refT = Dot(refPos - intP, intDir);
						F32 refEdgeDist0 = edgeSeg.Dist(refT);

						if (planeRel[ii][jj] == InFront || planeRel[ii][jj] == Concave)
						{
							// Clip by triangles in first plane 
							if (ClipSegmentsByPlaneTris(ii, edgeSeg, colCache))
							{
								// If we just clipped the part of the edge that was the closest
								// we will ignore this edge and the plane that was behind
								// @@JS: It may still be a valid plane if there is another plane that has convex edge with ii and is concave to jj
								// but for now we ignore that for simplicity
								F32 refEdgeDist = edgeSeg.Dist(refT);
								if (refEdgeDist > refEdgeDist0)
								{
									//badPlane[jj] = true;
									//continue;
								}
							}
						}

						// Shift tol in the direction of edge other biVector to prevent getting clipped by valid edge triangles
						Vector tolShift2 = s * kPlaneDistTol * biNorm2;
						edgeSeg.m_p = intP + tolShift2;

						if (planeRel[jj][ii] == InFront || planeRel[jj][ii] == Concave)
						{
							// Clip by triangles in second plane 
							if (ClipSegmentsByPlaneTris(jj, edgeSeg, colCache))
							{
								// See above
								F32 refEdgeDist = edgeSeg.Dist(refT);
								if (refEdgeDist > refEdgeDist0)
								{
									//badPlane[ii] = true;
									//continue;
								}
							}
						}

						if (edgeSeg.m_numSegs > 0)
						{
							// Yeah!
							planeRel[ii][jj] = ConvexEdge;
							planeRel[jj][ii] = ConvexEdge;
							goto RelFound; // just a double break
						}
					}
				}
			RelFound:
				break;
			} while(0); // just to improve the flow
		}
	}

	// Add planes into list of constraints in the order of distance and checking the relationships
	struct ConstraintPlane
	{
		bool m_accepted;
		U32 m_edges;
		I32 m_conRemap;
		STATIC_ASSERT(kMaxNumPlanes <= 32);
	};
	ConstraintPlane conPlanes[kMaxNumPlanes];
	memset(conPlanes, 0, sizeof(conPlanes));

	// Add first plane
	U32 iPlane = 0;
	while (iPlane < m_numPlanes && badPlane[iPlane])
		iPlane++;

	if (iPlane == m_numPlanes)
		return;

	conPlanes[iPlane].m_accepted = true;
	conPlanes[iPlane].m_conRemap = -1;
	U32 iMaxAcceptedPlane = iPlane;
	U32 numCon = 1;
	iPlane++;

	while (iPlane < m_numPlanes && numCon < Rope2::Constraint::kMaxNumPlanes)
	{
		const Plane& pl = m_planes[iPlane];
		if (badPlane[iPlane])
		{
			iPlane++;
			continue;
		}
		if (conPlanes[iPlane].m_accepted)
		{
			iPlane++;
			continue;
		}

		bool accept = true;
		bool behind = false;
		U32 convexEdgePlanes[Rope2::Constraint::kMaxNumPlanes];
		U32 numConvexEdgePlanes = 0;
		for (U32F iPl2 = 0; iPl2<=iMaxAcceptedPlane; iPl2++)
		{
			if (!conPlanes[iPl2].m_accepted)
			{
				continue;
			}

			if (planeRel[iPl2][iPlane] == ConvexEdge) // || planeRel[iPl2][iPlane] == Convex)
			{
				if (conPlanes[iPl2].m_edges)
				{
					for (U32F iPl3 = 0; iPl3<=iMaxAcceptedPlane; iPl3++)
					{
						if (((conPlanes[iPl2].m_edges >> iPl3) & 1) && planeRel[iPl3][iPlane] != Concave)
						{
							// @@JS: from 3 planes convex to each other choose the correct 2 in case the closest point is the vertex
							accept = false;
							break;
						}
					}
				}
				ASSERT(numConvexEdgePlanes < Rope2::Constraint::kMaxNumPlanes);
				convexEdgePlanes[numConvexEdgePlanes] = iPl2;
				numConvexEdgePlanes++;
			}
			else if (planeRel[iPl2][iPlane] == Concave)
			{
				//if (conPlanes[iPl2].m_edges)
				//{
				//	if (Dot(Vector(pl.m_plane), Vector(m_planes[iPl2].m_plane)) > 0.001f)
				//	{
				//		// This is the problematic case of plane possibly creating a ghost collision (monorail door)
				//		// Exclude the plane for now based on this simple test:
				//		F32 planeDist = Dot4(pl.m_plane, refPos.GetVec4());
				//		accept = pl.m_dist <= planeDist + 0.001f;
				//	}
				//}
			}
			else if (planeRel[iPl2][iPlane] == InFront)
			{
				// If this plane (A) is behind another (B) and concave to all planes that are convex with B, we can still take it
				if (conPlanes[iPl2].m_edges)
				{
					bool fineWithAllOthers = true;
					Point intSegA, intSegB;
					bool hasIntersection = GetPlaneIntersectionClipped(pl.m_plane, m_planes[iPl2].m_plane, aabb, intSegA, intSegB);
					for (U32F iPl3 = 0; iPl3<=iMaxAcceptedPlane; iPl3++)
					{
						if ((conPlanes[iPl2].m_edges >> iPl3) & 1)
						{
							if (planeRel[iPl3][iPlane] != Concave)
							{
								// Has to be concave with all the others
								fineWithAllOthers = false;
								break;
							}
							if (hasIntersection && (Dot4(m_planes[iPl3].m_plane, intSegA.GetVec4()) < 0.0f || Dot4(m_planes[iPl3].m_plane, intSegB.GetVec4()) < 0.0f))
							{
								// The intersection is behind this other plane, reject
								fineWithAllOthers = false;
								break;
							}
						}
					}
					accept = fineWithAllOthers;
				}
				else
				{
					accept = false;
				}
			}
			else if (planeRel[iPl2][iPlane] == Behind)
			{
				// We'll need seconds round once we have collected all convex edges
				behind = true;
			}
			else
			{
				accept = false;
			}

			if (!accept)
			{
				break;
			}
		}

		// Seconds round for Behind case
		if (accept && behind)
		{
			for (U32F iPl2 = 0; iPl2<=iMaxAcceptedPlane; iPl2++)
			{
				if (!conPlanes[iPl2].m_accepted)
				{
					continue;
				}

				if (planeRel[iPl2][iPlane] == Behind)
				{
					// In this case the new plane (A) is the one that is in front of (B). We can only accept it if it has convex edges and all the convex edges planes
					// are concave with B
					if (numConvexEdgePlanes)
					{
						bool fineWithAllOthers = true;
						Point intSegA, intSegB;
						bool hasIntersection = GetPlaneIntersectionClipped(pl.m_plane, m_planes[iPl2].m_plane, aabb, intSegA, intSegB);
						for (U32F iConvex = 0; iConvex<numConvexEdgePlanes; iConvex++)
						{
							U32 iPl3 = convexEdgePlanes[iConvex];
							if (planeRel[iPl3][iPl2] != Concave)
							{
								// Has to be concave with B
								fineWithAllOthers = false;
								break;
							}
							if (hasIntersection && (Dot4(m_planes[iPl3].m_plane, intSegA.GetVec4()) < 0.0f || Dot4(m_planes[iPl3].m_plane, intSegB.GetVec4()) < 0.0f))
							{
								// The intersection is behind this other plane, reject
								fineWithAllOthers = false;
								break;
							}
						}
						accept = fineWithAllOthers;
					}
					else
					{
						accept = false;
					}
					if (!accept)
					{
						break;
					}
				}
			}
		}

		if (accept)
		{
			conPlanes[iPlane].m_accepted = true;
			numCon++;
			conPlanes[iPlane].m_conRemap = -1;
			iMaxAcceptedPlane = Max(iMaxAcceptedPlane, iPlane);
			for (U32 iConvexEdgePlane = 0; iConvexEdgePlane<numConvexEdgePlanes; iConvexEdgePlane++)
			{
				conPlanes[iPlane].m_edges |= (1 << convexEdgePlanes[iConvexEdgePlane]);
				conPlanes[convexEdgePlanes[iConvexEdgePlane]].m_edges |= (1 << iPlane);
			}
			// Go back to see if any plane within tolerance can now be accepted
			F32 dist = pl.m_dist;
			while (iPlane > 1 && (dist - m_planes[iPlane-1].m_dist) < kPlaneDistTol)
			{
				iPlane--;
			}
		}
		else
		{
			iPlane++;
		}
	}

	// Check planes for feasibility
	{
		U32 removedPlanesMask = 0;
		for (U32 iPl = 0; iPl<=iMaxAcceptedPlane; iPl++)
		{
			if (conPlanes[iPl].m_accepted)
			{
				if (pRope->CheckConstraintPlane(iBead, m_planes[iPl].m_plane))
					continue;

				conPlanes[iPl].m_accepted = false;
				removedPlanesMask |= (1 << iPl);

				// Also, if this plane and another plane both have edge with some other 3rd plane, the other plane also has to be discarded
				// It must be plane convex to this plane and if we don't do this we would discard a piece of the 3rd plane that is valid
				for (U32 iPl1 = 0; iPl1<=iMaxAcceptedPlane; iPl1++)
				{
					if (conPlanes[iPl1].m_accepted)
					{
						if ((conPlanes[iPl].m_edges & conPlanes[iPl1].m_edges) != 0)
						{
							conPlanes[iPl1].m_accepted = false;
							removedPlanesMask |= (1 << iPl1);
						}
					}
				}
			}
		}

		// Clear edges with removed planes
		for (U32 iPl = 0; iPl<=iMaxAcceptedPlane; iPl++)
		{
			conPlanes[iPl].m_edges &= ~removedPlanesMask;
		}
	}

	// First add planes with edges
	for (U32 iPl0 = 0; iPl0<=iMaxAcceptedPlane; iPl0++)
	{
		if (conPlanes[iPl0].m_accepted)
		{
			if (conPlanes[iPl0].m_edges)
			{
				for (U32F iPl1 = 0; iPl1<iPl0; iPl1++)
				{
					if ((conPlanes[iPl0].m_edges >> iPl1) & 1)
					{
						if (conPlanes[iPl1].m_conRemap < 0)
							conPlanes[iPl1].m_conRemap = con.AddPlane(m_planes[iPl1].m_plane, m_planes[iPl1].m_hCollider);
						if (conPlanes[iPl0].m_conRemap < 0)
							conPlanes[iPl0].m_conRemap = con.AddPlane(m_planes[iPl0].m_plane, m_planes[iPl0].m_hCollider);
						if (conPlanes[iPl1].m_conRemap < 0 || conPlanes[iPl0].m_conRemap < 0)
						{
							// Failed to add a plane (too many planes probably). Just bail.
							break;
						}
						con.AddEdge(conPlanes[iPl1].m_conRemap, conPlanes[iPl0].m_conRemap);
					}
				}
			}
		}
	}

	// Now just plane planes
	for (U32 iPl0 = 0; iPl0<=iMaxAcceptedPlane; iPl0++)
	{
		if (conPlanes[iPl0].m_accepted)
		{
			if (conPlanes[iPl0].m_conRemap < 0)
			{
				ASSERT(conPlanes[iPl0].m_edges == 0 || con.m_numPlanes >= Rope2::Constraint::kMaxNumPlanes);
				if (conPlanes[iPl0].m_edges == 0)
					conPlanes[iPl0].m_conRemap = con.AddPlane(m_planes[iPl0].m_plane, m_planes[iPl0].m_hCollider);
			}
		}
	}

	// Check some PAT flags
	bool edgePat = false;
	for (U32 iTri = 0; iTri<m_numTris; iTri++)
	{
		const Tri& tri = m_tris[iTri];
		if (conPlanes[tri.m_planeIndex].m_accepted)
		{
			Pat pat;
			if (tri.m_isConvexFace)
			{
				RopeColliderHandle hCollider = m_planes[tri.m_planeIndex].m_hCollider;
				const hknpShape* pShape = hCollider.GetShape(pRope);
				if (hCollider.GetListIndex() >= 0)
				{
					PHYSICS_ASSERT(pShape && pShape->getType() == hknpShapeType::COMPOUND);
					const hknpCompoundShape* pCompShape = static_cast<const hknpCompoundShape*>(pShape);
					const hknpShapeInstance& inst = pCompShape->getInstance(hknpShapeInstanceId(hCollider.GetListIndex()));
					pShape = inst.getShape();
				}
				pat.m_bits = pShape->m_userData;
			}
			else
			{
				const RopeColTri& colTri = colCache.m_pTris[tri.m_triIndex];
				const RopeColliderHandle& colShape = colCache.m_pShapes[colTri.m_shapeIndex];
				if (const RigidBody* pBody = colShape.GetRigidBody())
				{
					pat = HavokGetPatFromHkRigidBody(pBody->GetHavokBody(), colTri.m_key);
				}
			}
			if (pat.GetCanGrapple())
			{
				con.m_canGrappleFlags |= (1 << conPlanes[tri.m_planeIndex].m_conRemap);
			}
			if (pat.GetSurfaceType() == Pat::kSurfaceTypeSnowDeep)
			{
				con.m_snowFlags |= (1 << conPlanes[tri.m_planeIndex].m_conRemap);
			}
			else if (pat.GetSurfaceType() == Pat::kSurfaceTypeFrictionless)
			{
				con.m_frictionlessFlags |= (1 << conPlanes[tri.m_planeIndex].m_conRemap);
			}

			if (!edgePat && pat.GetSurfaceType() != 0)
			{
				con.m_patSurface = pat.GetSurfaceType();
				edgePat = conPlanes[tri.m_planeIndex].m_edges != 0;
			}
		}
	}
}

bool Rope2PointCol::ClipSegmentsByPlaneTris(U32 iPlane, RopeEdgeSegments& segs, const RopeColCache& colCache)
{
	bool clipped = false;
	Vector norm(m_planes[iPlane].m_plane);

	for (U32 iTri = 0; iTri<m_numTris; iTri++)
	{
		const Tri& tri = m_tris[iTri];
		if (tri.m_planeIndex != iPlane)
			continue;

		F32 clipMin = -FLT_MAX;
		F32 clipMax = FLT_MAX;
		const RopeColTri& colTri = colCache.m_pTris[tri.m_triIndex];
		for (U32F ii = 0; ii<3; ii++)
		{
			Point clipPlaneP = colTri.m_pnt[ii];
			Vector edge = colTri.m_pnt[(ii+1)%3] - clipPlaneP;
			Vector clipPlaneNorm = Cross(edge, norm); // this is not normalized norm which shouldn't matter for our purposes
			F32 dt = Dot(clipPlaneNorm, segs.m_dir);
			F32 segPDistNeg = Dot(clipPlaneNorm, clipPlaneP - segs.m_p);
			F32 t = segPDistNeg / dt;
			if (dt > 0.0f)
			{
				ASSERT(IsFinite(t));
				clipMax = Min(clipMax, t);
			}
			else if (dt < 0.0f)
			{
				ASSERT(IsFinite(t));
				clipMin = Max(clipMin, t);
			}
			else if (segPDistNeg <= 0.0f)
			{
				// line going parallel to an edge and outside of the tri
				clipMin = FLT_MAX;
				clipMax = -FLT_MAX;
				break;
			}
		}

		if (clipMax - clipMin > kPlaneDistTol)
		{
			U32 iSeg = 0;
			while (iSeg < segs.m_numSegs)
			{
				RopeEdgeSeg& seg = segs.m_seg[iSeg];
				if ((seg.m_tA >= clipMax) | (seg.m_tB <= clipMin))
				{
					iSeg++;
					continue;
				}
				clipped = true;
				bool before = seg.m_tA < clipMin-kPlaneDistTol;
				bool after = seg.m_tB > clipMax-kPlaneDistTol;
				if (before)
				{
					F32 tB = seg.m_tB;
					seg.m_tB = clipMin;
					if (after)
					{
						segs.AddSeg(clipMax, tB);
						break;
					}
				}
				else if (after)
				{
					seg.m_tA = clipMax;
				}
				else
				{
					segs.RemoveSeg(iSeg);
					continue;
				}
				iSeg++;
			}
		}
	}

	return clipped;
}