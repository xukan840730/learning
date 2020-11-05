/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-mesh-util.h"

#include "corelib/containers/static-map.h"
#include "corelib/util/bit-array.h"
#include "corelib/util/fast-find.h"

#include "ndlib/process/debug-selection.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/render/util/screen-space-text-printer.h"
#include "ndlib/util/bit-array-grid-hash.h"

#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/find-reachable-polys.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-blocker-mgr.h"
#include "gamelib/gameplay/nav/nav-blocker.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-mesh-gap.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/nav-poly-ex.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"
#include "gamelib/level/entitydb.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/scriptx/h/nd-ai-defines.h"

#define EIGEN_NO_DEBUG 1
#define EIGEN_NO_MALLOC
#define eigen_assert SYSTEM_ASSERT

#include <Eigen/Dense>

/// --------------------------------------------------------------------------------------------------------------- ///
NavMeshDrawFilter g_navMeshDrawFilter;

/// --------------------------------------------------------------------------------------------------------------- ///
NavMeshDrawFilter::NavMeshDrawFilter()
{
	m_apRegFailDrawMask = (1ULL << ActionPack::kTraversalActionPack) | (1ULL << ActionPack::kCinematicActionPack);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// http://en.wikipedia.org/wiki/Triangle#Using_vectors
const Scalar ComputeTriangleAreaXz(Point_arg pt0, Point_arg pt1, Point_arg pt2)
{
	const Vector v0 = VectorXz(pt1 - pt0);
	const Vector v1 = VectorXz(pt2 - pt0);

	return Abs(v0.X() * v1.Z() - v1.X() * v0.Z()) * SCALAR_LC(0.5f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Scalar ComputeNavMeshAreaXz(const NavMesh& mesh)
{
	Scalar totalArea = kZero;
	const U32F iEnd = mesh.GetPolyCount();
	for(U32F iPoly = 0; iPoly < iEnd; ++iPoly)
	{
		totalArea += mesh.GetPoly(iPoly).ComputeSignedAreaXz();
	}
	return totalArea;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// http://mathworld.wolfram.com/TrianglePointPicking.html
const Point PickRandomTrianglePointLs(Point_arg pt0, Point_arg pt1, Point_arg pt2)
{
	Scalar a0 = frand();
	Scalar a1 = frand();

	// Point will be in mirrored triangle of quadrilateral - transform it into the original triangle
	if (a0 + a1 > 1.0f)
	{
		const Scalar sum = a0 + a1;
		a0 = a0 / sum;
		a1 = a1 / sum;
	}

	const Vector v0 = pt1 - pt0;
	const Vector v1 = pt2 - pt0;

	const Vector x = a0 * v0 + a1 * v1;

	return pt0 + x;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point PickRandomNavPolyPointWs(const NavPoly& poly)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	Point posWs = kOrigin;

	const U32F triangleCount = poly.GetVertexCount() / 2;

	if (triangleCount == 1)
	{
		posWs = poly.GetNavMesh()->LocalToWorld(::PickRandomTrianglePointLs(poly.GetVertex(0), poly.GetVertex(1), poly.GetVertex(2)));
		return posWs;
	}

	Scalar cumulativeTriangleArea[NavPoly::kMaxVertexCount - 2];

	for (U32F iTri = 0; iTri < triangleCount; ++iTri)
	{
		const U32F iV = iTri * 2 + 1;
		const Point pt0 = poly.GetVertex(iV);
		const Point pt1 = poly.GetVertex(iV - 1);
		const Point pt2 = poly.GetNextVertex(iV);

		cumulativeTriangleArea[iTri]  = ComputeTriangleAreaXz(pt0, pt1, pt2);
		cumulativeTriangleArea[iTri] += iTri ? cumulativeTriangleArea[iTri - 1] : kZero;
	}

	const Scalar randAreaSum = frand(kZero, cumulativeTriangleArea[triangleCount - 1]);

	// Check area sums to find the chosen triangle
	for (U32F iTri = 0; iTri < triangleCount; ++iTri)
	{
		if (randAreaSum <= cumulativeTriangleArea[iTri])
		{
			const U32F iV = iTri * 2 + 1;
			const Point pt0 = poly.GetVertex(iV);
			const Point pt1 = poly.GetVertex(iV - 1);
			const Point pt2 = poly.GetNextVertex(iV);

			posWs = poly.GetNavMesh()->LocalToWorld(::PickRandomTrianglePointLs(pt0, pt1, pt2));
			break;
		}
	}

	return posWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WalkPath(NavMesh::ProbeParams& params, const PathWaypointsEx* const pPathPs, NavMesh::FnVisitNavPoly visitFunc, uintptr_t userData, bool debug /*=false*/)
{
	NAV_ASSERT(visitFunc);

	if (!pPathPs || !pPathPs->IsValid())
		return;

	params.m_pStartPoly = NavPolyHandle(pPathPs->GetNavId(0)).ToNavPoly();
	if (!params.m_pStartPoly)
		return;

	for (int iWaypoint = 0; iWaypoint < pPathPs->GetWaypointCount() - 1; ++iWaypoint)
	{
		const Point startPs = pPathPs->GetWaypoint(iWaypoint);
		const Point goalPs = pPathPs->GetWaypoint(iWaypoint + 1);

		params.m_start = startPs;
		params.m_move = goalPs - startPs;

		if (FALSE_IN_FINAL_BUILD(debug))
			g_prim.Draw(DebugLine(startPs, goalPs, kColorRed, kColorBlue, 4.0f));

		params.m_pStartPoly->GetNavMesh()->WalkPolysInLinePs(params, visitFunc, userData);

		if (!params.m_pReachedPoly)
			return;

		const float distSqr = DistXzSqr(params.m_endPoint, goalPs);
		if (distSqr > Sqr(0.05f))
			return;

		params.m_pStartPoly = params.m_pReachedPoly;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WalkPath(NavMesh::ProbeParams& params, const PathWaypointsEx* const pPathPs, float walkLength, NavMesh::FnVisitNavPoly visitFunc, uintptr_t userData, bool debug /*=false*/)
{
	NAV_ASSERT(visitFunc);

	if (!pPathPs || !pPathPs->IsValid())
		return;

	float remDist = walkLength;
	params.m_pStartPoly = NavPolyHandle(pPathPs->GetNavId(0)).ToNavPoly();
	if (!params.m_pStartPoly)
		return;

	for (int iWaypoint = 0; iWaypoint < pPathPs->GetWaypointCount() - 1 && remDist > 0.0f; ++iWaypoint)
	{
		const Point startPs = pPathPs->GetWaypoint(iWaypoint);
		Point goalPs = pPathPs->GetWaypoint(iWaypoint + 1);
		const float segLength = Dist(startPs, goalPs);
		const float newRemDist = remDist - segLength;
		if (newRemDist <= 0.0f)
			goalPs = Lerp(startPs, goalPs, Limit01(remDist / segLength));

		params.m_start = startPs;
		params.m_move = goalPs - startPs;

		if (FALSE_IN_FINAL_BUILD(debug))
			g_prim.Draw(DebugLine(startPs, goalPs, kColorRed, kColorBlue, 4.0f));

		params.m_pStartPoly->GetNavMesh()->WalkPolysInLinePs(params, visitFunc, userData);

		if (!params.m_pReachedPoly)
			return;

		const float distSqr = DistXzSqr(params.m_endPoint, goalPs);
		if (distSqr > Sqr(0.05f))
			return;

		params.m_pStartPoly = params.m_pReachedPoly;
		remDist = newRemDist;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void DebugDrawHeightMapCell(const NavMesh* pMesh, int x, int z, float y, Color c)
{
	const NavMeshHeightMap* __restrict const pHeightMap = pMesh->GetHeightMap();

	const Vector localToWorld = AsVector(pMesh->GetOriginWs().GetPosition());
	const Point originWs = localToWorld + pHeightMap->m_bboxMinLs;

	const PrimAttrib primAttrib = PrimAttrib(kPrimDisableDepthTest, kPrimDisableDepthWrite);

	const Vector s(kHeightMapGridSpacing, kHeightMapHeightSpacing, kHeightMapGridSpacing);
	const Point v1 = PointFromXzAndY(originWs + Vector(x, 0.0f, z) * s, y);
	const Point v2 = PointFromXzAndY(originWs + Vector(x, 0.0f, z + 1) * s, y);
	const Point v3 = PointFromXzAndY(originWs + Vector(x + 1, 0.0f, z + 1) * s, y);
	const Point v4 = PointFromXzAndY(originWs + Vector(x + 1, 0.0f, z) * s, y);

	g_prim.Draw(DebugQuad(v1, v2, v3, v4, c, primAttrib));
	g_prim.Draw(DebugQuad(v1, v4, v3, v2, c, primAttrib));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FindSurfaceNormalWithFallbackWs(Vector& normalWs, const NavMesh* pNavMesh, const NavPoly* pNavPoly, const Point posWs, bool debug /*= false*/)
{
	NAV_ASSERT(pNavMesh);
	NAV_ASSERT(pNavPoly);
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	const Point posPs = pNavMesh->WorldToParent(posWs);
	Vector normalPs;
	if (!FindSurfaceNormalUsingHeightMapPs(normalPs, pNavMesh, posPs, debug))
	{
		// fall back to using nav poly
		Point unused;
		pNavPoly->ProjectPointOntoSurfacePs(&unused, &normalPs, posPs);
	}

	normalWs = pNavMesh->ParentToWorld(normalPs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FindSurfaceNormalWithFallbackPs(Vector& normalPs, const NavMesh* pNavMesh, const NavPoly* pNavPoly, const Point posPs, bool debug /*= false*/)
{
	NAV_ASSERT(pNavMesh);
	NAV_ASSERT(pNavPoly);
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	if (!FindSurfaceNormalUsingHeightMapPs(normalPs, pNavMesh, posPs, debug))
	{
		// fall back to using nav poly
		Point unused;
		pNavPoly->ProjectPointOntoSurfacePs(&unused, &normalPs, posPs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool FindSurfaceNormalUsingHeightMapWs(Vector& normalWs, const NavMesh* pNavMesh, const Point posWs, bool debug /*= false*/)
{
	NAV_ASSERT(pNavMesh);
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	const Point posPs = pNavMesh->WorldToParent(posWs);
	Vector normalPs;
	if (!FindSurfaceNormalUsingHeightMapPs(normalPs, pNavMesh, posPs, debug))
		return false;

	normalWs = pNavMesh->ParentToWorld(normalPs);
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool FindSurfaceNormalUsingHeightMapPs(Vector& normalPs, const NavMesh* pNavMesh, const Point posPs, bool debug /*= false*/)
{
	CONST_EXPR float kDebugTextSize = 0.7f;

	NAV_ASSERT(pNavMesh);
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	const NavMeshHeightMap* __restrict const pHeightMap = pNavMesh->GetHeightMap();

	if (!pHeightMap)
	{
		if (FALSE_IN_FINAL_BUILD(debug))
		{
			g_prim.Draw(DebugString(pNavMesh->ParentToWorld(posPs), "NO HEIGHTMAP", 1, kColorRed, kDebugTextSize));
		}

		return false;
	}

	const Vector localToParent = AsVector(pNavMesh->GetOriginPs().GetPosition());
	const Point originPs = localToParent + pHeightMap->m_bboxMinLs;

	const Point posHs = AsPoint((posPs - originPs) * kInvHeightMapGridSpacing);

	const int iX = FastCvtToIntFloor(posHs.X());
	const int iZ = FastCvtToIntFloor(posHs.Z());

	const int sizeX = pHeightMap->m_sizeX;
	const int sizeZ = pHeightMap->m_sizeZ;

	// heightmaps are a little larger than the largest extent of their navmesh,
	// so we can safely discard if we're within 1 cell of the border.
	// This will allow us to check the cell and its neighbors without having to border clamp.
	if (U32(iX - 1U) >= U32(sizeX - 2U) || U32(iZ - 1U) >= U32(sizeZ - 2U))
	{
		if (FALSE_IN_FINAL_BUILD(debug))
		{
			g_prim.Draw(DebugString(pNavMesh->ParentToWorld(posPs), "TOO FAR OFF THE EDGE", 1, kColorRed, kDebugTextSize));
		}

		return false;
	}

	const U64* __restrict const pOnMesh = pHeightMap->m_onNavMesh;
	const U32 onMeshPitch = pHeightMap->m_bitmapPitch;

	// check self and ring of all neighbors (including diagonals) to find nearest onnavmesh cell
	int iDepenX = -1, iDepenZ = -1;
	{
		float bestDistSqr = NDI_FLT_MAX;
		for (int iDx = -1; iDx <= 1; ++iDx)
		{
			for (int iDz = -1; iDz <= 1; ++iDz)
			{
				const U32 candidateX = iX + iDx;
				const U32 candidateZ = iZ + iDz;
				const U32 idx = candidateZ * onMeshPitch + candidateX;
				if ((pOnMesh[idx >> 6] >> (idx & 63)) & 1)
				{
					const Point candidateHs((float)candidateX + 0.5f, 0.0f, (float)candidateZ + 0.5f);
					const float distSqr = DistXzSqr(posHs, candidateHs);
					if (distSqr < bestDistSqr)
					{
						iDepenX = candidateX;
						iDepenZ = candidateZ;
						bestDistSqr = distSqr;
					}
				}
			}
		}
	}

	if (iDepenX == -1)
	{
		if (FALSE_IN_FINAL_BUILD(debug))
		{
			g_prim.Draw(DebugString(pNavMesh->ParentToWorld(posPs), "TOO FAR OFF NAVMESH", 1, kColorRed, kDebugTextSize));
		}

		return false;
	}

	// heightmaps are a little larger than the largest extent of their navmesh,
	// so we can safely discard if we're within 1 cell of the border.
	// This will allow us to check the cell and its neighbors without having to border clamp.
	if (U32(iDepenX - 1U) >= U32(sizeX - 2U) || U32(iDepenZ - 1U) >= U32(sizeZ - 2U))
	{
		if (FALSE_IN_FINAL_BUILD(debug))
		{
			g_prim.Draw(DebugString(pNavMesh->ParentToWorld(posPs), "OFF THE EDGE", 1, kColorRed, kDebugTextSize));
		}

		return false;
	}

	const Vector vScale(kHeightMapGridSpacing, kHeightMapHeightSpacing, kHeightMapGridSpacing);
	const Point depenCellMinCornerPs = originPs + vScale * Vector((float)iDepenX, 0.0f, (float)iDepenZ);
	const Point depenCellMaxCornerPs = depenCellMinCornerPs + vScale;
	const Point depenPs = PointFromXzAndY(Min(Max(posPs, depenCellMinCornerPs), depenCellMaxCornerPs), posPs);

	// fit a plane to those of the 9 surrounding cells with navmesh
	// require at least 4 (guarantees noncollinearity)

	// find candidates
	float dists[9];
	Point ptsPs[9];
	int numPts = 0;

	const Point offsetOriginPs = originPs + 0.5f * Vector(kHeightMapGridSpacing, 0.0f, kHeightMapGridSpacing);

	const U8* __restrict const pData = pHeightMap->m_data;
	const U32 heightMapPitch = pHeightMap->m_sizeX;
	for (int iDx = -1; iDx <= 1; ++iDx)
	{
		for (int iDz = -1; iDz <= 1; ++iDz)
		{
			const U32 candidateX = iDepenX + iDx;
			const U32 candidateZ = iDepenZ + iDz;
			const U32 idx = candidateZ * onMeshPitch + candidateX;
			if ((pOnMesh[idx >> 6] >> (idx & 63)) & 1)
			{
				const U32 iY = pData[candidateZ * heightMapPitch + candidateX];
				const Point candidatePs = offsetOriginPs + vScale * Vector(candidateX, iY, candidateZ);
				ptsPs[numPts] = candidatePs;

				const Vector toDepenPs = Abs(depenPs - candidatePs);
				dists[numPts] = Max(toDepenPs.X(), toDepenPs.Z());

				++numPts;
			}
		}
	}

	const bool enoughSamples = numPts >= 4;
	if (enoughSamples)
	{
		float weights[9];
		CONST_EXPR float kWeightPad = 1.5f * kHeightMapGridSpacing + 0.01f;
		for (int i = 0; i < numPts; ++i)
			weights[i] = kWeightPad - dists[i];

		float sqrtWeights[9];
		for (int i = 0; i < numPts; ++i)
			sqrtWeights[i] = Sqrt(weights[i]);

		float weightSum = 0.0f;
		for (int i = 0; i < numPts; ++i)
			weightSum += weights[i];

		Point weightedCentroidPs = kZero;
		for (int i = 0; i < numPts; ++i)
			weightedCentroidPs += weights[i] * AsVector(ptsPs[i]);
		weightedCentroidPs = AsPoint(AsVector(weightedCentroidPs) / weightSum);

		Vector diffsPs[9];
		for (int i = 0; i < numPts; ++i)
			diffsPs[i] = ptsPs[i] - weightedCentroidPs;

		Eigen::Matrix<float, 3, Eigen::Dynamic, 0, 3, 9> A(3, numPts);
		for (int i = 0; i < numPts; ++i)
		{
			const Vector pt = sqrtWeights[i] * diffsPs[i];
			A.col(i) << pt.X(), pt.Y(), pt.Z();
		}

		auto svd = A.jacobiSvd(Eigen::ComputeThinU);
		auto U = svd.matrixU().col(2);
		Vector n(U(0), U(1), U(2));
		if (n.Y() < 0.0f)
			n = -n;

		normalPs = n;
	}

	if (FALSE_IN_FINAL_BUILD(debug))
	{
		g_prim.Draw(DebugSphere(posPs, 0.06f, kColorRed));

		if (DistXzSqr(depenPs, posPs))
		{
			g_prim.Draw(DebugSphere(depenPs, 0.06f, kColorYellow));
			g_prim.Draw(DebugLine(posPs, depenPs, kColorRed, kColorYellow, 4.0f));
		}

		for (int iDx = -1; iDx <= 1; ++iDx)
		{
			for (int iDz = -1; iDz <= 1; ++iDz)
			{
				const U32 candidateX = iDepenX + iDx;
				const U32 candidateZ = iDepenZ + iDz;
				const U32 idx = candidateZ * onMeshPitch + candidateX;
				const bool onMesh = (pOnMesh[idx >> 6] >> (idx & 63)) & 1;
				const U32 iY = pData[candidateZ * heightMapPitch + candidateX];
				const float y = originPs.Y() + kHeightMapHeightSpacing * iY;
				DebugDrawHeightMapCell(pNavMesh, candidateX, candidateZ, y, onMesh ? kColorGreenTrans : kColorRedTrans);
			}
		}

		for (int i = 0; i < numPts; ++i)
		{
			g_prim.Draw(DebugSphere(ptsPs[i], 0.06f, kColorGreen));
		}

		if (enoughSamples)
			g_prim.Draw(DebugLine(depenPs, normalPs, kColorBlue, kColorRed, 5.0f));
		else
			g_prim.Draw(DebugString(pNavMesh->ParentToWorld(posPs), "NOT ENOUGH SAMPLES", 1, kColorRed, kDebugTextSize));
	}

	return enoughSamples;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point FindYWithFallbackWs(const NavMesh* pNavMesh, const NavPoly* const pNavPoly, Point posWs, bool debug /*= false*/)
{
	NAV_ASSERT(pNavMesh);
	NAV_ASSERT(pNavPoly);
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	Point posPs = pNavMesh->WorldToParent(posWs);
	if (!FindYUsingHeightMapPs(pNavMesh, posPs, debug))
	{
		// fall back to using nav poly
		Vector unused;
		pNavPoly->ProjectPointOntoSurfacePs(&posPs, &unused, posPs);
	}

	return pNavMesh->ParentToWorld(posPs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point FindYWithFallbackPs(const NavMesh* pNavMesh, const NavPoly* const pNavPoly, Point posPs, bool debug /*= false*/)
{
	NAV_ASSERT(pNavMesh);
	NAV_ASSERT(pNavPoly);
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	if (!FindYUsingHeightMapPs(pNavMesh, posPs, debug))
	{
		// fall back to using nav poly
		Vector unused;
		pNavPoly->ProjectPointOntoSurfacePs(&posPs, &unused, posPs);
	}

	return posPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool FindYUsingHeightMapWs(const NavMesh* pNavMesh, Point& posWs, bool debug /*= false*/)
{
	NAV_ASSERT(pNavMesh);
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	Point posPs = pNavMesh->WorldToParent(posWs);
	if (!FindYUsingHeightMapPs(pNavMesh, posPs, debug))
		return false;

	posWs = pNavMesh->ParentToWorld(posPs);
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool FindYUsingHeightMapPs(const NavMesh* pNavMesh, Point& posPs, bool debug /*= false*/)
{
	CONST_EXPR float kDebugTextSize = 0.7f;

	NAV_ASSERT(pNavMesh);
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	const NavMeshHeightMap* __restrict const pHeightMap = pNavMesh->GetHeightMap();

	if (!pHeightMap)
	{
		if (FALSE_IN_FINAL_BUILD(debug))
		{
			g_prim.Draw(DebugString(pNavMesh->ParentToWorld(posPs), "NO HEIGHTMAP", 1, kColorRed, kDebugTextSize));
		}

		return false;
	}

	const Vector localToParent = AsVector(pNavMesh->GetOriginPs().GetPosition());
	const Point originPs = localToParent + pHeightMap->m_bboxMinLs;

	const Point posHs = AsPoint((posPs - originPs) * kInvHeightMapGridSpacing);

	const int iX = FastCvtToIntFloor(posHs.X());
	const int iZ = FastCvtToIntFloor(posHs.Z());

	const int sizeX = pHeightMap->m_sizeX;
	const int sizeZ = pHeightMap->m_sizeZ;

	// heightmaps are a little larger than the largest extent of their navmesh,
	// so we can safely discard if we're within 1 cell of the border.
	// This will allow us to check the cell and its neighbors without having to border clamp.
	if (U32(iX - 1U) >= U32(sizeX - 2U) || U32(iZ - 1U) >= U32(sizeZ - 2U))
	{
		if (FALSE_IN_FINAL_BUILD(debug))
		{
			g_prim.Draw(DebugString(pNavMesh->ParentToWorld(posPs), "OFF THE EDGE", 1, kColorRed, kDebugTextSize));
		}

		return false;
	}

	const U64* __restrict const pOnMesh = pHeightMap->m_onNavMesh;
	const U32 onMeshPitch = pHeightMap->m_bitmapPitch;

	// check self and ring of all neighbors (including diagonals) to find nearest onnavmesh cell
	float bestDistSqr = NDI_FLT_MAX;
	int iDepenX = -1, iDepenZ = -1;
	for (int iDx = -1; iDx <= 1; ++iDx)
	{
		for (int iDz = -1; iDz <= 1; ++iDz)
		{
			const U32 candidateX = iX + iDx;
			const U32 candidateZ = iZ + iDz;
			const U32 idx = candidateZ * onMeshPitch + candidateX;
			if ((pOnMesh[idx >> 6] >> (idx & 63)) & 1)
			{
				const Point candidateHs((float)candidateX + 0.5f, 0.0f, (float)candidateZ + 0.5f);
				const float distSqr = DistXzSqr(posHs, candidateHs);
				if (distSqr < bestDistSqr)
				{
					iDepenX = candidateX;
					iDepenZ = candidateZ;
					bestDistSqr = distSqr;
				}
			}
		}
	}

	if (iDepenX == -1)
	{
		if (FALSE_IN_FINAL_BUILD(debug))
		{
			g_prim.Draw(DebugString(pNavMesh->ParentToWorld(posPs), "TOO FAR OFF NAVMESH", 1, kColorRed, kDebugTextSize));
		}

		return false;
	}

	// get nearest neighbor directions in both x and z
	const Vector depenCellMinCornerToPosHs = posHs - Point((float)iDepenX, 0.0f, (float)iDepenZ);
	const int iNnDx = depenCellMinCornerToPosHs.X() >= 0.5f ? 1 : -1;
	const int iNnDz = depenCellMinCornerToPosHs.Z() >= 0.5f ? 1 : -1;

	const U32 idxNnX = iDepenZ * onMeshPitch + (iDepenX + iNnDx);
	const bool onMeshNnX = (pOnMesh[idxNnX >> 6] >> (idxNnX & 63)) & 1;

	const U32 idxNnZ = (iDepenZ + iNnDz) * onMeshPitch + iDepenX;
	const bool onMeshNnZ = (pOnMesh[idxNnZ >> 6] >> (idxNnZ & 63)) & 1;

	const U32 idxNnDiag = (iDepenZ + iNnDz) * onMeshPitch + (iDepenX + iNnDx);
	const bool onMeshNnDiag = (pOnMesh[idxNnDiag >> 6] >> (idxNnDiag & 63)) & 1;

	const U8* __restrict const pData = pHeightMap->m_data;
	const U32 heightMapPitch = pHeightMap->m_sizeX;
	const float yStart = pData[(iDepenZ + 0) * heightMapPitch + (iDepenX + 0)] * kHeightMapHeightSpacing + originPs.Y();
	const float yNnX = pData[(iDepenZ + 0) * heightMapPitch + (iDepenX + iNnDx)] * kHeightMapHeightSpacing + originPs.Y();
	const float yNnZ = pData[(iDepenZ + iNnDz) * heightMapPitch + (iDepenX + 0)] * kHeightMapHeightSpacing + originPs.Y();
	const float yNnDiag = pData[(iDepenZ + iNnDz) * heightMapPitch + (iDepenX + iNnDx)] * kHeightMapHeightSpacing + originPs.Y();

	float yMin = yStart;
	if (onMeshNnX)
		yMin = Min(yMin, yNnX);
	if (onMeshNnZ)
		yMin = Min(yMin, yNnZ);
	if (onMeshNnDiag)
		yMin = Min(yMin, yNnDiag);

	CONST_EXPR float kCloseHeightThresh = 0.35f;

	const bool closeHeightStart = yStart - yMin < kCloseHeightThresh;
	const bool closeHeightNnX = yNnX - yMin < kCloseHeightThresh;
	const bool closeHeightNnZ = yNnZ - yMin < kCloseHeightThresh;
	const bool closeHeightNnDiag = yNnDiag - yMin < kCloseHeightThresh;

	const bool useStart = closeHeightStart;
	const bool useNnX = onMeshNnX && closeHeightNnX;
	const bool useNnZ = onMeshNnZ && closeHeightNnZ;
	const bool useNnDiag = onMeshNnDiag && closeHeightNnDiag;

	enum CellMask : U32
	{
		kStart = 1,
		kNnX = 2,
		kNnZ = 4,
		kNnDiag = 8
	};

	U32 m = 0U;
	if (useStart)
		m |= kStart;
	if (useNnX)
		m |= kNnX;
	if (useNnZ)
		m |= kNnZ;
	if (useNnDiag)
		m |= kNnDiag;

	float u = Abs(depenCellMinCornerToPosHs.X() - 0.5f);
	float v = Abs(depenCellMinCornerToPosHs.Z() - 0.5f);
	const bool uGreaterV = u > v;
	u = Limit01(u);
	v = Limit01(v);

	switch (m)
	{
	case kStart: // case 1:
		u = v = 0.0f;
	break;
	case kNnX: // case 2:
		u = 1.0f;
		v = 0.0f;
	break;
	case kStart | kNnX: // case 3:
		v = 0.0f;
	break;
	case kNnZ: // case 4:
		u = 0.0f;
		v = 1.0f;
	break;
	case kStart | kNnZ: // case 5:
		u = 0.0f;
	break;
	case kNnX | kNnZ: // case 6:
		if (uGreaterV)
		{
			u = 1.0f;
			v = 0.0f;
		}
		else
		{
			u = 0.0f;
			v = 1.0f;
		}
	break;
	case kStart | kNnX | kNnZ: // case 7:
		if (uGreaterV)
			v = 0.0f;
		else
			u = 0.0f;
	break;
	case kNnDiag: // case 8:
		u = 1.0f;
		v = 1.0f;
	break;
	case kStart | kNnDiag: // case 9:
		u = v = 0.0f;
	break;
	case kNnX | kNnDiag: // case 10:
		u = 1.0f;
	break;
	case kStart | kNnX | kNnDiag: // case 11:
		v = 0.0f;
	break;
	case kNnZ | kNnDiag: // case 12:
		v = 1.0f;
	break;
	case kStart | kNnZ | kNnDiag: // case 13:
		u = 0.0f;
	break;
	case kNnX | kNnZ | kNnDiag: // case 14:
		if (uGreaterV)
			u = 1.0f;
		else
			v = 1.0f;
	break;
	case kStart | kNnX | kNnZ | kNnDiag: // case 15:
		// leave unchanged for full blerp
	break;
	default:
#ifndef FINAL_BUILD
		ALWAYS_HALTF(("Invalid cell mask!"));
#else
		__builtin_unreachable();
#endif
	}

	const float um = 1.0f - u;
	const float q0 = um * yStart + u * yNnX;
	const float q1 = um * yNnZ + u * yNnDiag;
	const float vm = 1.0f - v;
	const float y = vm * q0 + v * q1;

	const Point newPosPs = PointFromXzAndY(posPs, y);

	if (FALSE_IN_FINAL_BUILD(debug))
	{
		const Point posWs = pNavMesh->ParentToWorld(posPs);
		const Point newPosWs = pNavMesh->ParentToWorld(newPosPs);

		CONST_EXPR float kPosSphereRadius = 0.08f;
		CONST_EXPR float kNewSphereRadius = 0.10f;

		g_prim.Draw(DebugSphere(posWs, kPosSphereRadius, kColorBlue));
		g_prim.Draw(DebugLine(posWs, newPosWs, kColorBlue, kColorGreen, 4.0f));
		g_prim.Draw(DebugSphere(newPosWs, kNewSphereRadius, kColorGreen));

		const Color colorOffNav = kColorRedTrans;
		const Color colorUnusable = kColorOrangeTrans;
		const Color colorUnused = kColorGreenTrans;
		const Color colorUsed = LumLerp(kColorGreenTrans, kColorBlueTrans, 0.5f);

		const Color colorStart = (m & kStart) ? kColorBlueTrans : kColorPinkTrans;
		const Color colorNnX = !onMeshNnX ? kColorRedTrans : !useNnX ? kColorOrangeTrans : (m & kNnX) ? colorUsed : colorUnused;
		const Color colorNnZ = !onMeshNnZ ? kColorRedTrans : !useNnZ ? kColorOrangeTrans : (m & kNnZ) ? colorUsed : colorUnused;
		const Color colorNnDiag = !onMeshNnDiag ? kColorRedTrans : !useNnDiag ? kColorOrangeTrans : (m & kNnDiag) ? colorUsed : colorUnused;

		DebugDrawHeightMapCell(pNavMesh, (iDepenX + 0), (iDepenZ + 0), yStart, colorStart);
		DebugDrawHeightMapCell(pNavMesh, (iDepenX + iNnDx), (iDepenZ + 0), yNnX, colorNnX);
		DebugDrawHeightMapCell(pNavMesh, (iDepenX + 0), (iDepenZ + iNnDz), yNnZ, colorNnZ);
		DebugDrawHeightMapCell(pNavMesh, (iDepenX + iNnDx), (iDepenZ + iNnDz), yNnDiag, colorNnDiag);

		char buf[64];
		sprintf(buf, "Mode %d (u=%.3f, v=%.3f)\n", m, u, v);
		g_prim.Draw(DebugString(pNavMesh->ParentToWorld(posPs), buf, 1, kColorWhite, kDebugTextSize));
	}

	posPs = newPosPs;

	return true;
}

struct AssessSpaceDebugData
{
	NavMesh::ProbeParams m_params;
	const NavMesh* m_pNavMesh = nullptr;
	NavMesh::ProbeResult m_res;
};

static float AssessSpaceLengthProbe(const NavPoly* const pPoly, const NavMesh* const pMesh, const Point posWs, const Vector moveWs, const float r, AssessSpaceDebugData& data)
{
	data.m_pNavMesh = pMesh;

	data.m_params.m_start = pMesh->WorldToParent(posWs);
	data.m_params.m_pStartPoly = pPoly;
	data.m_params.m_move = pMesh->WorldToParent(moveWs);
	data.m_params.m_probeRadius = r;
	data.m_params.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskHuman;

	data.m_res = pMesh->ProbePs(&data.m_params);

	//Nav::DebugDrawProbeResultPs(data.m_pNavMesh, data.m_res, data.m_params, kPrimDuration1FramePauseable);

	if (data.m_res == NavMesh::ProbeResult::kReachedGoal || data.m_res == NavMesh::ProbeResult::kHitEdge)
	{
		const Point newPosWs = data.m_params.m_pReachedMesh->ParentToWorld(data.m_params.m_endPoint);
		return Dist(posWs, newPosWs);
	}

	return 0.0f;
}

static bool AssessSpaceMoveProbe(const NavPoly*& pPoly, const NavMesh*& pMesh, Point& posWs, const Vector moveWs)
{
	NavMesh::ProbeParams params;
	params.m_start = pMesh->WorldToParent(posWs);
	params.m_pStartPoly = pPoly;
	params.m_move = pMesh->WorldToParent(moveWs);
	params.m_probeRadius = 0.0f;
	params.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskHuman;

	const NavMesh::ProbeResult res = pMesh->ProbePs(&params);
	if (res == NavMesh::ProbeResult::kReachedGoal)
	{
		pPoly = params.m_pReachedPoly;
		pMesh = params.m_pReachedMesh;
		posWs = pMesh->ParentToWorld(params.m_endPoint);
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AssessSpaceAroundNavLocation(const NavLocation& navLoc, float& totalNavigableDistance, bool& obstructing, float centerR /*= 0.36f*/, float sideR /*= 0.635f*/, bool debug /*= false*/)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());
	NAV_ASSERT(navLoc.IsValid());

	const NavMesh* pMesh = navLoc.ToNavMesh();
	NAV_ASSERT(pMesh);
	const NavPoly* pPoly = navLoc.ToNavPoly();
	NAV_ASSERT(pPoly);

	const Point originWs = navLoc.GetPosWs();

	//CONST_EXPR float centerR = 0.36f;
	//CONST_EXPR float sideR = 0.635f;
	CONST_EXPR float kHalfLen = 5.0f;
	CONST_EXPR float kMinCenterTravel = 0.68f;
	CONST_EXPR float kMinSideTravel = 0.05f;
	CONST_EXPR float kCenterStartOffsetRatio = 1.25f;
	CONST_EXPR float kSideStartOffsetRatio = 0.71f;
	CONST_EXPR int kNumDirs = 18;
	STATIC_ASSERT((kNumDirs & 1) == 0);

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	AssessSpaceDebugData centerDatas[kNumDirs];
	AssessSpaceDebugData topDatas[kNumDirs];
	AssessSpaceDebugData bottomDatas[kNumDirs];

	float centerLens[kNumDirs];
	float topLens[kNumDirs];
	float bottomLens[kNumDirs];

	BitArray<kNumDirs> centersClear;
	BitArray<kNumDirs> topsBlocked;
	BitArray<kNumDirs> bottomsBlocked;

	for (int iDir = 0; iDir < kNumDirs; ++iDir)
	{
		const float thetaRad = iDir * (PI_TIMES_2 / kNumDirs);
		const Vector probeDirWs = RotateVectorAbout(kUnitZAxis, kUnitYAxis, thetaRad);
		const Vector upDirWs = RotateYMinus90(probeDirWs);

		// center probe
		float centerLen = 0.0f;
		{
			const NavMesh* pCenterMesh = pMesh;
			const NavPoly* pCenterPoly = pPoly;
			Point centerStartWs = originWs;
			if (AssessSpaceMoveProbe(pCenterPoly, pCenterMesh, centerStartWs, (kCenterStartOffsetRatio * centerR) * probeDirWs))
			{
				centerLen = AssessSpaceLengthProbe(pCenterPoly, pCenterMesh, centerStartWs, kHalfLen * probeDirWs, centerR, centerDatas[iDir]);
			}
		}

		// top probe
		float topLen = 0.0f;
		{
			const NavMesh* pTopMesh = pMesh;
			const NavPoly* pTopPoly = pPoly;
			Point topStartWs = originWs;
			if (AssessSpaceMoveProbe(pTopPoly, pTopMesh, topStartWs, (kSideStartOffsetRatio * sideR) * probeDirWs + (centerR + sideR - 0.02f) * upDirWs))
			{
				topLen = AssessSpaceLengthProbe(pTopPoly, pTopMesh, topStartWs, +kHalfLen * probeDirWs, sideR, topDatas[iDir]);
			}
		}

		// bottom probe
		float bottomLen = 0.0f;
		{
			const NavMesh* pBottomMesh = pMesh;
			const NavPoly* pBottomPoly = pPoly;
			Point bottomStartWs = originWs;
			if (AssessSpaceMoveProbe(pBottomPoly, pBottomMesh, bottomStartWs, (kSideStartOffsetRatio * sideR) * probeDirWs - (centerR + sideR - 0.02f) * upDirWs))
			{
				bottomLen = AssessSpaceLengthProbe(pBottomPoly, pBottomMesh, bottomStartWs, kHalfLen * probeDirWs, sideR, bottomDatas[iDir]);
			}
		}

		centerLens[iDir] = centerLen;
		topLens[iDir] = topLen;
		bottomLens[iDir] = bottomLen;

		centersClear.AssignBit(iDir, centerLen > kMinCenterTravel);
		topsBlocked.AssignBit(iDir, topLen < kMinSideTravel);
		bottomsBlocked.AssignBit(iDir, bottomLen < kMinSideTravel);
	}

	// compute distance info
	float totalDistanceTraveled = 0.0f;
	for (int iDir1 = 0; iDir1 < kNumDirs / 2; ++iDir1)
	{
		const int iDir2 = iDir1 + kNumDirs / 2;

		const float topLenA = topLens[iDir1];
		const float topLenB = topLens[iDir2];

		const float bottomLenA = bottomLens[iDir1];
		const float bottomLenB = bottomLens[iDir2];

		totalDistanceTraveled += Min(kHalfLen, Max(topLenA + bottomLenB, bottomLenA + topLenB));
	}

	// check for valid paths through the center, blocked on both sides

	float longestFullPathLength = -1.0f;
	int iLongestFullPath1 = -1;
	int iLongestFullPath2 = -1;

	// for each valid halfpath through the center
	CONST_EXPR int kSpread = 4;
	for (int iHalfPath = 0; iHalfPath < kNumDirs - kSpread; ++iHalfPath)
	{
		if (!centersClear.IsBitSet(iHalfPath))
			continue;

		// check opposing neighbors. require a halfpath and its opposite to both be set
		for (int iOtherHalfPath = iHalfPath + kSpread; iOtherHalfPath <= Min(kNumDirs - 1, iHalfPath + kNumDirs - kSpread); ++iOtherHalfPath)
		{
			GAMEPLAY_ASSERT(iOtherHalfPath >= 0);
			GAMEPLAY_ASSERT(iOtherHalfPath < kNumDirs);
			if (centersClear.IsBitSet(iOtherHalfPath))
			{
				if ((topsBlocked.IsBitSet(iHalfPath) || bottomsBlocked.IsBitSet(iOtherHalfPath))
					&&
					(bottomsBlocked.IsBitSet(iHalfPath) || topsBlocked.IsBitSet(iOtherHalfPath)))
				{
					const float fullPathLength = centerLens[iHalfPath] + centerLens[iOtherHalfPath];
					if (fullPathLength > longestFullPathLength)
					{
						longestFullPathLength = fullPathLength;
						iLongestFullPath1 = iHalfPath;
						iLongestFullPath2 = iOtherHalfPath;
					}
				}
			}
		}
	}

	const bool anyFullPath = longestFullPathLength != -1.0f;

	if (FALSE_IN_FINAL_BUILD(debug))
	{
		if (anyFullPath)
		{
			const int iHalfPath1 = iLongestFullPath1;
			const int iHalfPath2 = iLongestFullPath2;

			const AssessSpaceDebugData& centerDataA = centerDatas[iHalfPath1];
			if (centerDataA.m_pNavMesh)
				Nav::DebugDrawProbeResultPs(centerDataA.m_pNavMesh, centerDataA.m_res, centerDataA.m_params, kPrimDuration1FramePauseable);

			const AssessSpaceDebugData& centerDataB = centerDatas[iHalfPath2];
			if (centerDataB.m_pNavMesh)
				Nav::DebugDrawProbeResultPs(centerDataB.m_pNavMesh, centerDataB.m_res, centerDataB.m_params, kPrimDuration1FramePauseable);

			const AssessSpaceDebugData& topDataA = topDatas[iHalfPath1];
			if (topDataA.m_pNavMesh)
				Nav::DebugDrawProbeResultPs(topDataA.m_pNavMesh, topDataA.m_res, topDataA.m_params, kPrimDuration1FramePauseable);

			const AssessSpaceDebugData& topDataB = topDatas[iHalfPath2];
			if (topDataB.m_pNavMesh)
				Nav::DebugDrawProbeResultPs(topDataB.m_pNavMesh, topDataB.m_res, topDataB.m_params, kPrimDuration1FramePauseable);

			const AssessSpaceDebugData& bottomDataA = bottomDatas[iHalfPath1];
			if (bottomDataA.m_pNavMesh)
				Nav::DebugDrawProbeResultPs(bottomDataA.m_pNavMesh, bottomDataA.m_res, bottomDataA.m_params, kPrimDuration1FramePauseable);

			const AssessSpaceDebugData& bottomDataB = bottomDatas[iHalfPath2];
			if (bottomDataB.m_pNavMesh)
				Nav::DebugDrawProbeResultPs(bottomDataB.m_pNavMesh, bottomDataB.m_res, bottomDataB.m_params, kPrimDuration1FramePauseable);
		}

		ScreenSpaceTextPrinter printer(originWs + Vector(0.0f, 0.1f, 0.0f));
		printer.SetDuration(kPrimDuration1FramePauseable);
		printer.PrintText(anyFullPath ? kColorRed : kColorGreen, "%.2fm\n", totalDistanceTraveled);
	}

	totalNavigableDistance = totalDistanceTraveled;
	obstructing = anyFullPath;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FindNavMeshFromArray(const NavMeshArray& navMeshes, FindBestNavMeshParams* pParams)
{
	PROFILE(AI, FindNavMeshFromArray);
	NAV_ASSERT(pParams);
	if (!pParams)
		return;

	if (pParams->m_cullDist < 0.0f)
		return;

	const NavMesh* pBestMesh = nullptr;
	const NavPoly* pBestPoly = nullptr;
	Point bestPos(kZero);
	Scalar cullXZ2 = Sqr(pParams->m_cullDist) + 0.001f;
	Scalar cullY = Abs(pParams->m_yThreshold) + 0.001f;
	Scalar bestDist2 = SCALAR_LC(kLargeFloat);
	const Scalar kXZWeight2 = Sqr(8.0f);  // we care more about XZ than Y so we weight XZ distance squared by this squared factor
	float bboxCullDist = Max(pParams->m_cullDist, pParams->m_yThreshold);
	NavMesh::FindPointParams findPolyParams;
	Point posWs = pParams->m_pointWs;
	findPolyParams.m_searchRadius = pParams->m_cullDist;

	for (const NavMesh* pMesh : navMeshes)
	{
		if (!pMesh->GetManagerId().IsValid())
			continue; // this nav mesh might've failed to login

		const bool bindMatch = ((Nav::kMatchAllBindSpawnerNameId == pParams->m_bindSpawnerNameId)
								|| (pMesh->GetBindSpawnerNameId() == pParams->m_bindSpawnerNameId));

		if (!bindMatch)
			continue;

		if (!pMesh->IsRegistered() || !pMesh->PointInBoundingBoxWs(posWs, bboxCullDist))
			continue;

		if (!pParams->m_swimMeshAllowed && (pMesh->NavMeshForcesSwim() || pMesh->NavMeshForcesDive()))
			continue;

		const Point posPs = pMesh->WorldToParent(posWs);
		findPolyParams.m_point = posPs;
		pMesh->FindNearestPointPs(&findPolyParams);

		if (!findPolyParams.m_pPoly)
			continue;

		const Scalar distY = Abs(findPolyParams.m_nearestPoint.Y() - posPs.Y());
		const Scalar distXZ2 = DistXzSqr(findPolyParams.m_nearestPoint, posPs);

		if (distXZ2 <= cullXZ2 && distY <= cullY && findPolyParams.m_nearestPoint.Y() < pParams->m_yCeiling)
		{
			Scalar dist2 = distXZ2 * kXZWeight2 + Sqr(distY);

			if (findPolyParams.m_pPoly->IsLink() && findPolyParams.m_pPoly->GetLinkedPoly())
			{
				dist2 += SCALAR_LC(1.0f);  // discourage choosing nav mesh where closest poly is a link
			}

			if (dist2 < bestDist2)
			{
				bestDist2 = dist2;
				pBestMesh = pMesh;
				pBestPoly = findPolyParams.m_pPoly;
				bestPos = pMesh->ParentToWorld(findPolyParams.m_nearestPoint);
			}
		}
	}

	pParams->m_pNavMesh = pBestMesh;
	pParams->m_pNavPoly = pBestPoly;
	pParams->m_nearestPointWs = bestPos;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F FindReachableActionPacksFromPolys(ActionPack** ppOutList,
									   U32F maxActionPackCount,
									   NavPolyHandle* polyList,
									   U32F polyCount,
									   const FindReachableActionPacksParams& params)
{
	PROFILE(AI, FindReachableActionPacksFromPolys);
	U32F outCount = 0;

	for (U32F iStart = 0; iStart < params.m_numStartPositions; ++iStart)
	{
		const Point startPosWs				= params.m_startPositions[iStart].GetPosWs();
		const Scalar radiusSq				= Sqr(params.m_radius);
		const U32F actionPackTypeMask		= params.m_actionPackTypeMask;
		const Scalar actionPackEntryDist	= params.m_actionPackEntryDistance;

		for (U32F iPoly = 0; iPoly < polyCount; ++iPoly)
		{
			const NavPoly& poly = *polyList[iPoly].ToNavPoly();
			ActionPack* pAp = poly.GetRegisteredActionPackList();
			while (pAp)
			{
				ASSERT(pAp->GetRegisteredNavLocation().ToNavPoly() == &poly);
				const U32F mask = 1U << pAp->GetType();
				if (pAp->IsEnabled() && (mask & actionPackTypeMask) != 0)
				{
					const Scalar distSq = LengthSqr(pAp->GetDefaultEntryPointWs(actionPackEntryDist) - startPosWs);
					if (distSq < radiusSq)
					{
						if (outCount >= maxActionPackCount)
						{
							return maxActionPackCount;
						}
						ppOutList[outCount++] = pAp;
					}
				}
				pAp = pAp->GetRegistrationListNext();
			}
		}
	}

	return outCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F GetActionPacksFromPolyList(ActionPack** ppOutList,
								U32F maxActionPackCount,
								const NavPoly** pPolyList,
								U32F polyCount,
								U32F actionPackTypeMask,
								bool obeyEnabled /* = true */)
{
	PROFILE(AI, FindReachableActionPacksFromPolys);
	U32F outCount = 0;

	for (U32F iPoly = 0; iPoly < polyCount; ++iPoly)
	{
		const NavPoly* pPoly = pPolyList[iPoly];
		if (!pPoly)
			continue;

		if (outCount >= maxActionPackCount)
		{
			break;
		}

		ActionPack* pAp = pPoly->GetRegisteredActionPackList();
		while (pAp)
		{
			if (outCount >= maxActionPackCount)
			{
				break;
			}

			ASSERT(pAp->GetRegisteredNavLocation().ToNavPoly() == pPoly);
			const U32F mask = 1U << pAp->GetType();

			const bool valid = !obeyEnabled || pAp->IsEnabled();

			if (valid && (mask & actionPackTypeMask) != 0)
			{
				ppOutList[outCount++] = pAp;
			}

			pAp = pAp->GetRegistrationListNext();
		}
	}

	return outCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F FindReachableActionPacks(ActionPack** ppOutList,
							  U32F maxActionPackCount,
							  const FindReachableActionPacksParams& params)
{
	PROFILE(AI, FindReachableActionPacksInRad);

	U32F outCount = 0;
	const U32F kMaxPolyCount = 256;
	NavPolyHandle polyList[kMaxPolyCount];
	Vec4 costVecs[kMaxPolyCount];

	const U32F polyCount = Nav::FindReachablePolys(polyList, costVecs, kMaxPolyCount, params);
	outCount = FindReachableActionPacksFromPolys(ppOutList, maxActionPackCount, polyList, polyCount, params);

	return outCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
//
// DebugDraw
//
void DrawWireframeBox(const Transform& mat, Point_arg boxMin, Point_arg boxMax, const Color& col)
{
	STRIP_IN_FINAL_BUILD;
	Point bbox[2];
	bbox[0] = boxMin;
	bbox[1] = boxMax;
	Point pts[8];
	// generate the 8 corner verts of the box
	for (U32F i = 0; i < 8; ++i)
	{
		// index them such that bit 0 of the index corresponds to X, bit 1 is Y, bit 2 is Z; each bit selects min or max
		Point pt = Point(bbox[(i>>0)&1].X(), bbox[(i>>1)&1].Y(), bbox[(i>>2)&1].Z());
		pts[i] = pt * mat;
	}
	// for each of the corner vertices,
	for (U32F i = 0; i < 8; ++i)
	{
		// flip the X axis bit
		U32F i0 = i ^ 1;
		if (i < i0)
		{
			g_prim.Draw(DebugLine(pts[i], pts[i0], col));
		}
		U32F i1 = i ^ 2;
		if (i < i1)
		{
			g_prim.Draw(DebugLine(pts[i], pts[i1], col));
		}
		U32F i2 = i ^ 4;
		if (i < i2)
		{
			g_prim.Draw(DebugLine(pts[i], pts[i2], col));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void HeightMapDebugDraw(const NavMesh* const pNavMesh)
{
	STRIP_IN_FINAL_BUILD;

	const NavMeshHeightMap* const pHeightMap = pNavMesh->GetHeightMap();
	if (!pHeightMap)
		return;

	const U8* __restrict const pData = pHeightMap->m_data;
	const U64* __restrict const pOnNavMesh = pHeightMap->m_onNavMesh;
	const U64* __restrict const pStealth = pHeightMap->m_isStealth;

	const U32 pitch = pHeightMap->m_sizeX;
	const U32 alignedPitch = pHeightMap->m_bitmapPitch;

	const Vector localToParent = AsVector(pNavMesh->GetOriginPs().GetPosition());
	const Point originWs = localToParent + pHeightMap->m_bboxMinLs + 0.5f * Vector(kHeightMapGridSpacing, 0.0f, kHeightMapGridSpacing);
	const Vector vScale(kHeightMapGridSpacing, kHeightMapHeightSpacing, kHeightMapGridSpacing);
	const Vector vNudge(0.0f, 0.005f, 0.0f);

	for (int iZ = 0; iZ < pHeightMap->m_sizeZ; ++iZ)
	{
		U8 iPrevY = 0;
		Point ptPrevWs;
		Color prevColor;
		bool prevHole = true;

		for (int iX = 0; iX < pHeightMap->m_sizeX; ++iX)
		{
			const U32 idx = iZ * pitch + iX;
			const U32 alignedIdx = iZ * alignedPitch + iX;
			const U8 iY = pData[idx];
			const bool hole = !iY;
			if (!hole)
			{
				const bool stealth = (pStealth[alignedIdx >> 6] >> (alignedIdx & 63)) & 1;
				const bool onNavMesh = (pOnNavMesh[alignedIdx >> 6] >> (alignedIdx & 63)) & 1;

				const Color color = onNavMesh ? (stealth ? kColorBlue : kColorGreen) : kColorRed;
				Point ptWs = originWs + vScale * Vector(iX, iY, iZ);
				if (onNavMesh)
					ptWs += vNudge;

				if (!prevHole)
				{
					g_prim.Draw(DebugLine(ptPrevWs, ptWs, prevColor, color, 2.0f, kPrimEnableHiddenLineAlpha));
				}

				ptPrevWs = ptWs;
				iPrevY = iY;
				prevColor = color;
			}
			prevHole = hole;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshDebugDraw(const NavMesh* pNavMesh, const NavMeshDrawFilter& filter)
{
	STRIP_IN_FINAL_BUILD;

	PROFILE_AUTO(AI);

	if (pNavMesh == nullptr)
		return;

	const RenderCamera& cam = GetRenderCamera(0);

	const NavMesh& mesh = *pNavMesh;
	const bool hasErrorPolys = pNavMesh->HasErrorPolys() && !filter.m_suppressDrawMeshErrors;
	const Transform mat = mesh.GetOriginWs().AsTransform();

	if (filter.m_drawWadePolys)
	{
		for (U32 iPoly = 0; iPoly < mesh.GetPolyCount(); ++iPoly)
		{
			const NavPoly& poly = mesh.GetPoly(iPoly);
			const EntityDB* pDb = poly.GetEntityDB();
			if (!pDb || !pDb->GetData(SID("force-npc-wade"), false))
				continue;

			const Color color = kColorCyanTrans;

			poly.DebugDraw(color, color, 0.05f);
		}
	}

	if (filter.m_drawStealthPolys)
	{
		for (U32 iPoly = 0; iPoly < mesh.GetPolyCount(); ++iPoly)
		{
			const NavPoly& poly = mesh.GetPoly(iPoly);
			if (!poly.IsStealth())
				continue;

			float stealthHeight = 0.0f;
			Color color			= kColorGreenTrans;
			if (filter.m_drawStealthHeights)
			{
				switch (poly.GetStealthVegetationHeight())
				{
				case DC::kStealthVegetationHeightProne:
					stealthHeight = 0.2f;
					color		  = kColorMagentaTrans;
					break;
				case DC::kStealthVegetationHeightCrouch:
					stealthHeight = 0.7f;
					color		  = kColorYellowTrans;
					break;
				case DC::kStealthVegetationHeightStand:
					stealthHeight = 1.4f;
					color		  = kColorBlueTrans;
					break;
				case DC::kStealthVegetationHeightInvalid:
					stealthHeight = 0.5f;
					color		  = kColorRedTrans;
					break;
				default:
					stealthHeight = 0.5f;
					color		  = kColorRedTrans;
					break;
				}
			}

			poly.DebugDraw(color, color, 0.02f + stealthHeight);
		}
	}

	const bool drawEverything = filter.m_drawPolys || filter.m_drawPolysNoTrans || filter.m_drawEdgeIds || filter.m_drawNames || filter.m_drawEntityDB || filter.m_drawExPolys;
	const bool drawOnlyErrorPolys = !drawEverything && hasErrorPolys;

	const bool noPosts = pNavMesh->IsNoPosts();

	if (drawEverything || drawOnlyErrorPolys)
	{
		NavBlockerBits debugObeyedBlockers;
		Nav::BuildDebugSelObeyedBlockers(&debugObeyedBlockers);

		const U32F kDrawPassHidden = 0;
		const U32F kDrawPassInternalEdges = 1;
		const U32F kDrawPassExternalEdges = 2;
		const U32F kDrawPassSpecial = 3;
		const U32F kDrawPassCount = 4;
		for (U32F iDrawPass = 0; iDrawPass < kDrawPassCount; ++iDrawPass)
		{
			if (filter.m_drawPolysNoTrans && iDrawPass == kDrawPassHidden)
			{
				continue;
			}

			for (U32F iPoly = 0; iPoly < mesh.GetPolyCount(); ++iPoly)
			{
				const NavPoly& poly = mesh.GetPoly(iPoly);
				const Point polyCenterWs = pNavMesh->LocalToWorld(poly.GetCentroid());

				if (filter.m_drawExPolys && (iDrawPass == kDrawPassInternalEdges))
				{
					float vo = 0.1f;

					for (const NavPolyEx* pExPoly = poly.GetNavPolyExList(); pExPoly; pExPoly = pExPoly->GetNextPolyInList())
					{
						if (filter.m_explodeExPolys)
						{
							vo = (float(pExPoly->GetId() % 10) * 0.1f) + 0.1f;
						}

						pExPoly->DebugDrawEdges(kColorOrangeTrans,
												kColorPurple,
												kColorRed,
												vo,
												3.0f,
												kPrimDuration1FrameAuto,
												&debugObeyedBlockers);

						const Point centerLs = pExPoly->GetCentroid();
						const Point centerWs = mesh.LocalToWorld(centerLs);

						NavBlockerBits matchingBlockers;
						NavBlockerBits::BitwiseAnd(&matchingBlockers, pExPoly->m_blockerBits, debugObeyedBlockers);
						const bool blocked = !matchingBlockers.AreAllBitsClear();

						if (filter.m_drawPolyIds)
						{
							StringBuilder<256> desc;
							desc.append_format("%d", pExPoly->GetId());

							U64 iBlocker = pExPoly->m_blockerBits.FindFirstSetBit();

							if (iBlocker < kMaxDynamicNavBlockerCount)
							{
								desc.append_format(":");
							}

							for (; iBlocker < kMaxDynamicNavBlockerCount; iBlocker = pExPoly->m_blockerBits.FindNextSetBit(iBlocker))
							{
								desc.append_format(" %d", (int)iBlocker);
							}

							g_prim.Draw(DebugString(centerWs + Vector(0.0f, vo, 0.0f), desc.c_str(), blocked ? kColorRedTrans : kColorOrangeTrans, 0.5f));
						}

						if (filter.m_drawConnectivity)
						{
							for (U32F iAdj = 0; iAdj < pExPoly->m_numVerts; ++iAdj)
							{
								const NavManagerId& adjId = pExPoly->GetAdjacentPolyId(iAdj);

								if (adjId.IsNull())
									continue;

								if (const NavPolyEx* pAdjEx = NavPolyExHandle(adjId).ToNavPolyEx())
								{
									const NavMesh* pAdjMesh = pAdjEx->GetNavMesh();
									const Point adjCenterWs = pAdjMesh->LocalToWorld(pAdjEx->GetCentroid());

									NavBlockerBits::BitwiseAnd(&matchingBlockers, pAdjEx->m_blockerBits, debugObeyedBlockers);
									const bool adjBlocked = !matchingBlockers.AreAllBitsClear();

									g_prim.Draw(DebugLine(centerWs,
														  adjCenterWs,
														  blocked ? kColorRedTrans : kColorOrangeTrans,
														  adjBlocked ? kColorRedTrans : kColorOrangeTrans));
								}
								else if (const NavPoly* pAdjPoly = NavPolyHandle(adjId).ToNavPoly())
								{
									const NavMesh* pAdjMesh = pAdjPoly->GetNavMesh();
									const Point adjCenterWs = pAdjMesh->LocalToWorld(pAdjPoly->GetCentroid());
									const bool adjBlocked = pAdjPoly->IsBlocked();

									g_prim.Draw(DebugLine(centerWs,
														  adjCenterWs,
														  blocked ? kColorRedTrans : kColorOrangeTrans,
														  adjBlocked ? kColorWhiteTrans : kColorGreenTrans));
								}
							}
						}
					}
				}

				/*g_prim.Draw(DebugString(polyCenterWs, StringBuilder<256>("%d", (int)iPoly).c_str(), kColorWhiteTrans));
				for (U32F iVert = 0; iVert < poly.GetVertexCount(); ++iVert)
				{
					g_prim.Draw(DebugLine(pNavMesh->LocalToWorld(poly.GetVertex(iVert)), polyCenterWs, kColorWhiteTrans));
				}*/

				if (filter.m_drawPolys || filter.m_drawPolysNoTrans)
				{
					Color32 col = kColorCyanTrans;
					DebugLine line(Point(kZero), Point(kZero), col);
					U32F drawPassMask = 1U << kDrawPassHidden;
					bool isSpecial = true;
					Vector offset(kZero);
					if (poly.IsValid() && drawOnlyErrorPolys)
					{
						continue;
					}

					if (poly.IsLink())
					{
						col = kColorBlue;
						offset = Vector(0, 0.05f, 0);
						line.width = 2.0f;
						if (nullptr == mesh.GetLink(poly.GetLinkId()).GetDestNavMesh().ToNavMesh())
						{
							col = kColorOrange;
						}
					}
					else if (poly.GetId() == NavPoly::kNoAdjacent)
					{
						col = kColorRedTrans;
						offset = Vector(0, 0.05f, 0);
						line.width = 3.0f;
					}
					else
					{
						isSpecial = false;
					}

					if (g_navMeshDrawFilter.m_drawSourceShapeColors)
					{
						const StringId64 srcId = poly.GetTagData(SID("source-shape"), INVALID_STRING_ID_64);

						if (srcId != INVALID_STRING_ID_64)
						{
							const Color clr = AI::IndexToColor(srcId.GetValue(), 0.33f);
							poly.DebugDraw(clr);
						}
					}
					else if (poly.GetPathNodeId() < g_navPathNodeMgr.GetMaxNodeCount())
					{
						const NavPathNode& node = g_navPathNodeMgr.GetNode(poly.GetPathNodeId());

						if (!poly.IsValid())
						{
							if (!g_navMeshDrawFilter.m_suppressDrawMeshErrors)
							{
								poly.DebugDraw(kColorRedTrans);
							}
						}
						else if (poly.IsBlocked() || node.IsBlocked())
						{
							poly.DebugDraw(kColorWhiteTrans);
						}
					}
					else if (!g_navMeshDrawFilter.m_suppressDrawMeshErrors)
					{
						poly.DebugDraw(kColorRedTrans);
					}

					for (U32F iEdge = 0; iEdge < poly.GetVertexCount(); ++iEdge)
					{
						line.pt0 = poly.GetVertex(iEdge) * mat;
						line.pt1 = poly.GetNextVertex(iEdge) * mat;

						U32F iAdjPoly = poly.GetAdjacentId(iEdge);
						if (isSpecial)
						{
							line.pt0 += offset;
							line.pt1 += offset;
							drawPassMask = (1U << kDrawPassHidden) | (1U << kDrawPassSpecial);
						}
						else if (iAdjPoly == NavPoly::kNoAdjacent)
						{
							// no neighbor
							if (noPosts)
							{
								col = Color32(kColorPurple);
							}
							else
							{
								col = Color32(255, 255, 50, 255);  // yellow
							}

							line.width = 3.0f;
							line.stipple = DebugLine::Stipple();  // no stipple
							drawPassMask = (1U << kDrawPassHidden) | (1U << kDrawPassExternalEdges);
						}
						else if (poly.GetId() > iAdjPoly)
						{
							// only draw shared edge if our id is less than neighbor (avoiding redundant lines)
							drawPassMask = 0;
						}
						else
						{
							if (noPosts)
							{
								col = Color32(kColorCyanTrans);
							}
							else
							{
								col = Color32(0, 255, 0, 90);  // translucent green
							}

							line.width = 2.0f;
							line.stipple = DebugLine::Stipple(0x5555U, 1);
							// no hidden pass
							drawPassMask = 1U << kDrawPassInternalEdges;
						}
						if (drawPassMask & (1U << iDrawPass))
						{
							if (!mesh.IsRegistered())
							{
								col = Color32(128, 128, 128, col.a);
							}
							line.color0 = col;
							line.color1 = col;

							if (kDrawPassHidden == iDrawPass)
							{
								line.attrib = PrimAttrib(kPrimDisableDepthTest);
								line.color0.a = line.color1.a = 130;
								line.width = 1.0f;
								line.stipple = DebugLine::Stipple(0x5555U, 1);
							}
							g_prim.Draw(line);
						}

						if ((iDrawPass == 0) && filter.m_drawEdgeIds)
						{
							const Point edgeCenterWs = Lerp(line.pt0, line.pt1, 0.25f);
							const Point drawPosWs = Lerp(polyCenterWs, edgeCenterWs, 0.75f);
							const Vector vo = poly.IsLink() ? Vector(0.0f, 0.2f, 0.0f) : kZero;
							g_prim.Draw(DebugString(drawPosWs + vo, StringBuilder<64>("%d", (int)iEdge).c_str(), line.color0, 0.5f));

							if (poly.IsLink())
							{
								g_prim.Draw(DebugLine(drawPosWs, drawPosWs + vo, line.color0));
							}
						}
					}
				}
			}
		}

		if (filter.m_drawNames)
		{
			const Locator originWs = mesh.GetOriginWs();

			StringBuilder<1024> desc;
			desc.clear();

			desc.append_format("%s", mesh.GetName());

			desc.append_format(" %.2f", mesh.GetSurfaceAreaXz());

			const StringId64 levelId = mesh.GetLevelId();
			desc.append_format(" [%s]", DevKitOnly_StringIdToString(levelId));

			const U32F maxOccupancy = pNavMesh->GetMaxOccupancyCount();
			const U32F curOccupancy = pNavMesh->GetOccupancyCount();
			if (maxOccupancy < 10)
			{
				desc.append_format(" (occupancy %d/%d)", (int)curOccupancy, (int)maxOccupancy);
			}

			if (!mesh.IsRegistered())
			{
				desc.append_format(" (disabled)");
			}

			if (const EntityDB* pEntityDB = mesh.GetEntityDB())
			{
				for (EntityDB::RecordMap::const_iterator itr = pEntityDB->Begin(); itr != pEntityDB->End(); ++itr)
				{
					const StringId64 key = itr->first;
					const EntityDB::Record* pRec = itr->second;

					if (pRec->IsValidI32())
					{
						const I32 val = pRec->GetData<I32>(0);
						desc.append_format("\n%s = %d", DevKitOnly_StringIdToString(key), val);
					}
				}
			}

			g_prim.Draw(DebugCoordAxesLabeled(originWs, desc.c_str()));
		}
	}

	if (filter.m_drawBoundingBox)
	{
		const Vector vecRadius = mesh.GetBoundingBoxMaxLs() - Point(kZero);
		DrawWireframeBox(mat, kOrigin +(-vecRadius), kOrigin + vecRadius, kColorBlue);
	}

	if (filter.m_drawPolyIds || filter.m_drawLinkIds || filter.m_drawFlags || filter.m_drawEdgeIds)
	{
		for (U32F iPoly = 0; iPoly < mesh.GetPolyCount(); ++iPoly)
		{
			const NavPoly& poly = mesh.GetPoly(iPoly);
			Point center = mesh.LocalToWorld(poly.GetCentroid());

			StringBuilder<256> desc;

			if (filter.m_drawPolyIds)
			{
				desc.append_format("id %d ", (int)iPoly);
			}
			if (filter.m_drawLinkIds && poly.IsLink())
			{
				desc.append_format("ln %d ", (int)poly.GetLinkId());
			}
			if (filter.m_drawFlags && (poly.m_flags.m_u8 != 0))
			{
				desc.append_format("fl ");
				if (!poly.IsValid())
					desc.append_format("error ");
				if (poly.IsLink())
					desc.append_format("link ");
				if (poly.IsPreLink())
					desc.append_format("preLink ");
				if (poly.IsBlocked())
					desc.append_format("blocked ");
			}

			const EntityDB* pPolyDb = poly.GetEntityDB();
			if (filter.m_drawEntityDB && pPolyDb && (Dist(center, cam.GetPosition()) < 15.0f))
			{
				desc.append("\n");
				pPolyDb->DebugPrint(&desc);
			}

			const Vector vo = poly.IsLink() ? Vector(0.0f, 0.2f, 0.0f) : kZero;
			g_prim.Draw(DebugCross(center, 0.05f, kColorGreenTrans));
			g_prim.Draw(DebugString(center + vo, desc.c_str(), kColorGreenTrans, 0.6f));

			if (filter.m_drawLinkIds && (poly.IsLink()))
			{
				const NavMeshLink& link = mesh.GetLink(poly.GetLinkId());
				Point center2 = center + Vector(0, 0.2f, 0);
				if (const NavMesh* pDestMesh = link.GetDestNavMesh().ToNavMesh())
				{
					desc.clear();
					desc.append_format("active link id %s (0x%.16llx)", DevKitOnly_StringIdToString(link.GetLinkId()), link.GetLinkId().GetValue());
					g_prim.Draw(DebugString(center2, desc.c_str(), kColorGreenTrans));
					const NavPoly& destPreLinkPoly = pDestMesh->GetPoly(link.GetDestPreLinkPolyId());
					Point pt = pDestMesh->LocalToWorld(destPreLinkPoly.GetCentroid());
					g_prim.Draw(DebugLine(center2, pt, kColorGreenTrans));
				}
				else
				{
					desc.clear();
					desc.append_format("inactive link id %s (0x%.16llx)", DevKitOnly_StringIdToString(link.GetLinkId()), link.GetLinkId().GetValue());
					g_prim.Draw(DebugString(center2, desc.c_str(), kColorRedTrans));
				}
			}
		}
	}

	if (filter.m_drawConnectivity)
	{
		for (U32F iPoly = 0; iPoly < mesh.GetPolyCount(); ++iPoly)
		{
			const NavPoly& poly = mesh.GetPoly(iPoly);
			//Point center = LocalToWorld(GetNode(iPoly)->m_pos);
			Point center = mesh.LocalToWorld(poly.GetCentroid());

			g_prim.Draw(DebugCross(center, 0.05f, kColorGreenTrans));
			for (U32F iEdge = 0; iEdge < poly.GetVertexCount(); ++iEdge)
			{
				U32F iAdjPoly = poly.GetAdjacentId(iEdge);
				{
					if (iAdjPoly != NavPoly::kNoAdjacent)
					{
						Color col = kColorBlue;
						const NavPoly& adjPoly = mesh.GetPoly(iAdjPoly);
						//Point adjCenter = LocalToWorld(GetNode(iAdjPoly)->m_pos);
						Point adjCenter = mesh.LocalToWorld(adjPoly.GetCentroid());
						bool goodConnection = false;
						for (U32F iAdjEdge = 0; iAdjEdge < adjPoly.GetVertexCount(); ++iAdjEdge)
						{
							if (adjPoly.GetAdjacentId(iAdjEdge) == iPoly)
							{
								goodConnection = true;
							}
						}
						if (! goodConnection)
						{
							col = kColorRed;
						}
						// if its a bad connection, draw; otherwise, only draw if current poly id is less than adj poly id
						//  (avoiding redundant lines)
						if (! goodConnection || iPoly < iAdjPoly)
						{
							g_prim.Draw(DebugLine(center, adjCenter, col, 2.0f, PrimAttrib(kPrimEnableHiddenLineAlpha)));
						}
					}
				}
			}
		}
	}

	if (filter.m_drawGrid)
	{
		mesh.GetPolyGridHash()->DebugDraw(mat);
	}

	if (filter.m_drawHeightMaps || filter.m_drawStaticHeightMaps)
	{
		HeightMapDebugDraw(pNavMesh);
	}

	if (filter.m_drawGaps)
	{
		for (U32F iGap = 0; iGap < mesh.GetNumGaps(); ++iGap)
		{
			const NavMeshGap& gap = mesh.GetGap(iGap);

			const Color col = AI::IndexToColor(iGap);
			const float yOffset = float(iGap % 10) * 0.01f;

			if (filter.m_onlyDrawEnabledGaps && (!gap.m_enabled0 || !gap.m_enabled1))
				continue;

			if ((filter.m_drawGapMinDist >= 0.0f) && (gap.m_gapDist < filter.m_drawGapMinDist))
				continue;

			if ((filter.m_drawGapMaxDist >= 0.0f) && (gap.m_gapDist > filter.m_drawGapMaxDist))
				continue;

			gap.DebugDraw(&mesh, col, yOffset);
		}
	}

	if (filter.m_drawEntityDB && mesh.GetEntityDB())
	{
		Point printerPos = mesh.GetOriginWs().GetTranslation();
		ScreenSpaceTextPrinter printer(printerPos, ScreenSpaceTextPrinter::kPrintNextLineAbovePrevious, kPrimDuration1FrameAuto, g_msgOptions.m_conScale);
		mesh.GetEntityDB()->DebugDraw(&printer);

		if (mesh.GetBindSpawnerNameId() != INVALID_STRING_ID_64)
		{
			printer.PrintText(kColorYellow, "parent = %s", DevKitOnly_StringIdToString(mesh.GetBindSpawnerNameId()));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Nav::AreNavPolysConnected(const NavPoly* pPolyA, const NavPoly* pPolyB)
{
	if (!pPolyA || !pPolyB)
		return false;

	const U32F pathNodeIdA = pPolyA->GetPathNodeId();
	const U32F pathNodeIdB = pPolyB->GetPathNodeId();

	const NavPathNode& pathNodeA = g_navPathNodeMgr.GetNode(pathNodeIdA);

	U32F iLink = pathNodeA.GetFirstLinkId();

	while (iLink != NavPathNodeMgr::kInvalidLinkId)
	{
		const NavPathNode::Link& link = g_navPathNodeMgr.GetLink(iLink);
		iLink = link.m_nextLinkId;

		if (link.IsDynamic())
			continue;

		if (link.m_nodeId == pathNodeIdB)
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F Nav::GetIncomingEdge(const NavPoly* pPoly,
						  const NavPolyEx* pPolyEx,
						  const NavPoly* pSourcePoly,
						  const NavPolyEx* pSourcePolyEx)
{
	if (!pPoly || !pSourcePoly)
	{
		return -1;
	}

	U32F sourcePolyId = pSourcePoly->GetId();

	if (pPoly->GetNavMeshHandle() != pSourcePoly->GetNavMeshHandle() && (pPolyEx == nullptr))
	{
		if (pSourcePoly->IsLink() || pSourcePoly->IsPreLink())
		{
			sourcePolyId = pSourcePoly->GetLinkedPolyId();
		}
		else
		{
			return -1;
		}
	}

	const U32F vertCount = pPolyEx ? pPolyEx->GetVertexCount() : pPoly->GetVertexCount();

	const U32F sourcePolyExId = pSourcePolyEx ? pSourcePolyEx->GetId() : 0;
	I32F foundEdge = -1;

	for (U32F iEdge = 0; iEdge < vertCount; ++iEdge)
	{
		if (pPolyEx)
		{
			const NavManagerId adjId = pPolyEx->GetAdjacentPolyId(iEdge);

			if (adjId.m_u64 == 0)
				continue;

			if ((adjId.m_iPoly == sourcePolyId) && (adjId.m_iPolyEx == sourcePolyExId))
			{
				foundEdge = iEdge;
				break;
			}
		}
		else
		{
			if (pPoly->GetAdjacentId(iEdge) == sourcePolyId)
			{
				foundEdge = iEdge;
				break;
			}
		}
	}

	return foundEdge;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Nav::BuildDebugSelObeyedBlockers(NavBlockerBits* pBits)
{
	STRIP_IN_FINAL_BUILD_VALUE(false);

	if (!pBits)
		return false;

	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	DebugSelection& dsel = DebugSelection::Get();

	MutableNdGameObjectHandle hSelected[256];

	const U32F numSelected = dsel.GetSelectedGameObjects(hSelected, 256);

	if (numSelected == 0)
	{
		pBits->SetAllBits();

		return false;
	}

	pBits->ClearAllBits();

	for (U32F i = 0; i < numSelected; ++i)
	{
		if (!hSelected[i].IsKindOf(g_type_NavCharacter))
			continue;

		const NavCharacter* pNavChar = PunPtr<const NavCharacter*>(hSelected[i].ToProcess());
		const NavControl* pNavControl = pNavChar ? pNavChar->GetNavControl() : nullptr;

		if (!pNavControl)
			continue;

		const NavBlockerBits npcBits = pNavControl->BuildObeyedBlockerList(true);
		NavBlockerBits::BitwiseOr(pBits, *pBits, npcBits);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavBlockerBits Nav::BuildObeyedBlockerList(const SimpleNavControl* pNavControl,
										   SimpleNavControl::PFnNavBlockerFilterFunc filterFunc,
										   bool forPathing,
										   BoxedValue userData /* = BoxedValue() */,
										   bool debug /* = false */)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());
	NAV_ASSERT(filterFunc);

	NavBlockerBits blockerBits;
	blockerBits.ClearAllBits();

	const NavBlockerMgr& nbMgr = NavBlockerMgr::Get();

	for (const DynamicNavBlocker& blocker : nbMgr.GetDynamicBlockers())
	{
		if (filterFunc && !filterFunc(pNavControl, &blocker, userData, forPathing, debug))
		{
			continue;
		}

		blockerBits.SetBit(nbMgr.GetNavBlockerIndex(&blocker));
	}

	return blockerBits;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Nav::DebugPrintObeyedBlockers(const NavBlockerBits& bits, const char* characterName /*= nullptr*/)
{
	STRIP_IN_FINAL_BUILD;

	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	const NavBlockerMgr& nbMgr = NavBlockerMgr::Get();

	if (characterName)
		MsgConPauseable("[%s] Obeyed Nav Blockers:\n", characterName);
	else
		MsgConPauseable("Obeyed Nav Blockers:\n");

	for (const DynamicNavBlocker& blocker : nbMgr.GetDynamicBlockers())
	{
		const I32F blockerIndex = nbMgr.GetNavBlockerIndex(&blocker);

		if (bits.IsBitSet(blockerIndex))
		{
			MsgConPauseable("  |%2d| %s\n", blockerIndex, DevKitOnly_StringIdToString(blocker.GetOwner().GetUserId()));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void DebugDrawProbeResult(const NavMesh* pMesh,
							     const NavMesh::ProbeResult result,
							     const Point posPs,
							     const Point posGoalPs,
							     const Point endPointPs,
							     const Point impactPointPs,
							     const float probeRadius,
							     const NavMesh::BoundaryFlags hitBoundaryFlags,
							     DebugPrimTime tt /* = kPrimDuration1FrameAuto */)
{
	STRIP_IN_FINAL_BUILD;

	if (!pMesh)
		return;

	StringBuilder<256> boundaryStr;

	PrimServerWrapper ps(pMesh->GetParentSpace());
	ps.DisableDepthTest();
	ps.SetDuration(tt);

	switch (result)
	{
	case NavMesh::ProbeResult::kReachedGoal:
		ps.DrawArrow(posPs, posGoalPs, 0.5f, kColorGreen);
		ps.DrawCross(endPointPs, 0.2f, kColorOrangeTrans);
		if (probeRadius > NDI_FLT_EPSILON)
		{
			ps.DrawFlatCapsule(posPs, posGoalPs, probeRadius, kColorGreen);
		}
		break;

	case NavMesh::ProbeResult::kHitEdge:
		ps.DrawArrow(posPs, posGoalPs, 0.5f, kColorRedTrans);
		if (probeRadius > NDI_FLT_EPSILON)
		{
			ps.DrawFlatCapsule(posPs, posGoalPs, probeRadius, kColorRedTrans);
		}

		ps.DrawArrow(posPs, endPointPs, 0.5f, kColorRed);
		if (probeRadius > NDI_FLT_EPSILON)
		{
			ps.DrawFlatCapsule(posPs, endPointPs, probeRadius, kColorRed);
		}

		ps.DrawCross(impactPointPs, 0.25f, kColorRed);

		boundaryStr.append_format("Hit Edge ");

		if (hitBoundaryFlags == NavMesh::kBoundaryNone)
		{
			boundaryStr.append_format("none ?!?");
		}
		if (hitBoundaryFlags & NavMesh::kBoundaryTypeStatic)
		{
			boundaryStr.append_format("static ");
		}
		if (hitBoundaryFlags & NavMesh::kBoundaryTypeDynamic)
		{
			boundaryStr.append_format("dynamic ");
		}
		if (hitBoundaryFlags & NavMesh::kBoundaryTypeStealth)
		{
			boundaryStr.append_format("stealth ");
		}
		if (hitBoundaryFlags & NavMesh::kBoundaryInside)
		{
			boundaryStr.append_format("inside ");
		}

		ps.DrawString(impactPointPs, boundaryStr.c_str(), kColorRed, 0.5f);
		break;

	case NavMesh::ProbeResult::kErrorStartedOffMesh:
		ps.DrawArrow(posPs, posGoalPs, 0.5f, kColorRedTrans);
		ps.DrawCross(posPs, 0.1f, kColorRed);
		ps.DrawCross(impactPointPs, 0.1f, kColorRed);
		if (probeRadius > NDI_FLT_EPSILON)
		{
			ps.DrawCircle(posPs, kUnitYAxis, probeRadius, kColorRedTrans);
		}
		ps.DrawString(posPs, "Start Off Mesh", kColorRed, 0.5f);
		break;

	case NavMesh::ProbeResult::kErrorStartNotPassable:
		ps.DrawArrow(posPs, posGoalPs, 0.5f, kColorRedTrans);
		ps.DrawCross(posPs, 0.1f, kColorRedTrans);
		ps.DrawCross(impactPointPs, 0.1f, kColorRed);
		if (probeRadius > NDI_FLT_EPSILON)
		{
			ps.DrawCircle(posPs, kUnitYAxis, probeRadius, kColorRedTrans);
		}
		ps.DrawString(posPs, "Start Not Passable", kColorRed, 0.5f);
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Nav::DebugDrawProbeResultPs(const NavMesh* pMesh,
								 const NavMesh::ProbeResult result,
								 const NavMesh::ProbeParams& paramsPs,
								 DebugPrimTime tt /* = kPrimDuration1FrameAuto */)
{
	STRIP_IN_FINAL_BUILD;

	const Point posPs		  = paramsPs.m_start;
	const Point posGoalPs	  = paramsPs.m_start + paramsPs.m_move;
	const Point endPointPs	  = paramsPs.m_endPoint;
	const Point impactPointPs = paramsPs.m_impactPoint;

	DebugDrawProbeResult(pMesh, result, posPs, posGoalPs, endPointPs, impactPointPs, paramsPs.m_probeRadius, paramsPs.m_hitBoundaryFlags, tt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Nav::DebugDrawProbeResultLs(const NavMesh* pMesh,
								 const NavMesh::ProbeResult result,
								 const NavMesh::ProbeParams& paramsLs,
								 DebugPrimTime tt /* = kPrimDuration1FrameAuto */)
{
	STRIP_IN_FINAL_BUILD;

	const Point posPs		  = pMesh->LocalToParent(paramsLs.m_start);
	const Point posGoalPs	  = pMesh->LocalToParent(paramsLs.m_start + paramsLs.m_move);
	const Point endPointPs	  = pMesh->LocalToParent(paramsLs.m_endPoint);
	const Point impactPointPs = pMesh->LocalToParent(paramsLs.m_impactPoint);

	DebugDrawProbeResult(pMesh, result, posPs, posGoalPs, endPointPs, impactPointPs, paramsLs.m_probeRadius, paramsLs.m_hitBoundaryFlags, tt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct ExpNode
{
	union
	{
		struct
		{
			U16 m_meshId;
			U16 m_polyId;
		};
		U32 m_bits;
	};

	ExpNode() {}
	ExpNode(U16 meshId, U16 polyId) : m_meshId(meshId), m_polyId(polyId) {}
};

static inline bool operator==(const ExpNode a, const ExpNode b)
{
	return a.m_bits == b.m_bits;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ExpNodeVisited(const ExpNode* visited, U32 numVisited, ExpNode query)
{
	return FastFindInteger32_CautionOverscan(visited, numVisited, query.m_bits) != U32(-1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
//#define ALLOW_POLY_EXPAND_FIND_ACTION_PACKS_DEBUG
U32 PolyExpandFindActionPacksByTypeInRadius(ActionPack** const results,
											U32 maxResults,
											U32 typeMask,
											const NavPoly* pStartPoly,
											const Point startWs,
											const float r,
											const bool debug /*= false*/)
{
	const NavMeshMgr* pMgr = EngineComponents::GetNavMeshMgr();

	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	NAV_ASSERT(results);
	NAV_ASSERT(typeMask);
	NAV_ASSERT(maxResults);
	NAV_ASSERT(pStartPoly);
	NAV_ASSERT(IsReasonable(startWs));
	NAV_ASSERT(r >= 0.0f);

	CONST_EXPR int kExpMaxOpen = 256;

	// must be multiple of 16
	CONST_EXPR int kExpMaxVisited = 768;

	const float r2 = r * r;

	ExpNode open[kExpMaxOpen];
	ExpNode visited[kExpMaxVisited];

	int numOpen = 1;
	int numVisited = 1;
	int numOpenHighWater = 1;
	visited[0] = open[0] = ExpNode(pStartPoly->GetNavMeshHandle().GetManagerId().m_navMeshIndex, pStartPoly->GetId());

	U32 numResults = 0;

	do
	{
		// pop the top
		const ExpNode node = open[--numOpen];
		const NavMesh* __restrict const pMesh = pMgr->UnsafeFastLookupNavMesh(node.m_meshId);
		const NavPoly* __restrict const pPoly = pMesh->UnsafeGetPolyFast(node.m_polyId);

		// see if we should expand: only if poly intersects sphere
		// i.e. only if either a segment intersects sphere or sphere center is inside poly
		const Point centerLs = startWs - AsVector(pMesh->GetOriginPs().GetPosition());
		Point B = pPoly->GetVertex(3);
		for (int iA = 0; iA < 4; ++iA)
		{
			const Point A = pPoly->GetVertex(iA);

			// segment A->B
			const float distToCenter2 = FastDistPointSegmentSqr(centerLs, A, B);

			if (distToCenter2 < r2)
			{
#ifdef ALLOW_POLY_EXPAND_FIND_ACTION_PACKS_DEBUG
				if (debug)
					pPoly->DebugDraw(kColorBlue, kColorBlue, 0.05f, kPrimDuration1Frame);
#endif // ALLOW_POLY_EXPAND_FIND_ACTION_PACKS_DEBUG
				goto poly_expand_found_intersection;
			}

			B = A;
		}

		if (!pPoly->PolyContainsPointLsNoEpsilon(centerLs))
			continue;

#ifdef ALLOW_POLY_EXPAND_FIND_ACTION_PACKS_DEBUG
		if (debug)
			pPoly->DebugDraw(kColorGreen, kColorGreen, 0.05f, kPrimDuration1Frame);
#endif // ALLOW_POLY_EXPAND_FIND_ACTION_PACKS_DEBUG

		poly_expand_found_intersection:;

		// valid. add various types of neighbors to the open list, and add all relevant APs of this poly
		// to output while we're at it

		// add adjacent polys
		{
			for (int iAdj = 0; iAdj < 4; ++iAdj)
			{
				const U16 adjPolyId = pPoly->GetAdjacencyArray()[iAdj];
				if (adjPolyId != kInvalidNavPolyId)
				{
					const ExpNode newNode(node.m_meshId, adjPolyId);
					if (UNLIKELY(!ExpNodeVisited(visited, numVisited, newNode)))
					{
						// navmesh should exist because it's the same as the current navmesh
						open[numOpen++] = newNode;
						visited[numVisited++] = newNode;
						if (UNLIKELY(numOpen >= kExpMaxOpen || numVisited >= kExpMaxVisited))
							goto label_stop_evaluating_poly_expand_find_action_packs;
					}
				}
			}
		}

		// add TAPs
		{
			ActionPack* pAp = pPoly->GetRegisteredActionPackList();
			while (pAp)
			{
				const U32 apType = 1U << (U32)pAp->GetType();

				// add to output?
				if (apType & typeMask)
				{
					if (LIKELY(pAp->IsEnabled()))
					{
						const Point posWs = pAp->GetRegisteredNavLocation().GetPosPs();
						const float d2 = DistSqr(posWs, startWs);

						if (d2 <= r2)
						{
							results[numResults++] = pAp;
							if (numResults >= maxResults)
								goto label_stop_evaluating_poly_expand_find_action_packs;
						}
					}
				}

				// add to open list?
				CONST_EXPR U32 tapTypeMask = 1U << (U32)ActionPack::kTraversalActionPack;
				if (apType == tapTypeMask)
				{
					if (LIKELY(pAp->IsEnabled()))
					{
						const TraversalActionPack* const pTap = static_cast<const TraversalActionPack* const>(pAp);
						const NavLocation& destNavLoc = pTap->GetDestNavLocation();
						const NavManagerId newId = destNavLoc.GetNavManagerId();
						if (newId != NavMeshHandle::kInvalidMgrId)
						{
							// navmesh should exist because the tAP is enabled and has a valid dest nav loc, but
							// because TAP link update is amortized, the dest mesh may have just been logged out,
							// so we do have to check explicitly.
							if (pMgr->UnsafeFastLookupNavMesh(newId))
							{
								const ExpNode newNode(newId.m_navMeshIndex, newId.m_iPoly);
								if (!ExpNodeVisited(visited, numVisited, newNode))
								{
									open[numOpen++] = newNode;
									visited[numVisited++] = newNode;
									if (UNLIKELY(numOpen >= kExpMaxOpen || numVisited >= kExpMaxVisited))
										goto label_stop_evaluating_poly_expand_find_action_packs;
								}
							}
						}
					}
				}

				pAp = pAp->GetRegistrationListNext();
			}
		}

		// add link
		if (pPoly->IsPreLink())
		{
			const NavMeshLink& link = pMesh->GetLink(pPoly->GetLinkId());
			const NavManagerId newId = link.GetDestNavMesh().GetManagerId();
			if (newId != NavMeshHandle::kInvalidMgrId)
			{
				const ExpNode newNode(newId.m_navMeshIndex, link.GetDestPreLinkPolyId());
				if (!ExpNodeVisited(visited, numVisited, newNode))
				{
					// navmesh should exist because the link had a valid dest nav mesh handle
					open[numOpen++] = newNode;
					visited[numVisited++] = newNode;
					if (UNLIKELY(numOpen >= kExpMaxOpen || numVisited >= kExpMaxVisited))
						goto label_stop_evaluating_poly_expand_find_action_packs;
				}
			}
		}

		if (numOpen > numOpenHighWater)
			numOpenHighWater = numOpen;

	} while (numOpen);

label_stop_evaluating_poly_expand_find_action_packs:;

	NAV_ASSERT(numOpen < kExpMaxOpen);
	NAV_ASSERT(numVisited < kExpMaxVisited);

	// debug draw
#ifdef ALLOW_POLY_EXPAND_FIND_ACTION_PACKS_DEBUG
	if (debug)
	{
		char buf[256];
		sprintf(buf, "%d Results\n%d Open\n%d Highwater\n%d Visited", numResults, numOpen, numOpenHighWater, numVisited);
		g_prim.Draw(DebugString(startWs + Vector(0.0f, 1.2f, 0.0f), buf, 0, kColorCyan, 1.1f));
	}
#endif // ALLOW_POLY_EXPAND_FIND_ACTION_PACKS_DEBUG

	return numResults;
}
