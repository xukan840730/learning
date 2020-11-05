/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-mesh.h"

#include "corelib/math/intersection.h"
#include "corelib/math/segment-util.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/bigsort.h"
#include "corelib/util/user.h"

#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/netbridge/mail.h"
#include "ndlib/text/stringid-util.h"
#include "ndlib/util/bit-array-grid-hash.h"

#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/nav/nav-ex-data.h"
#include "gamelib/gameplay/nav/nav-mesh-patch.h"
#include "gamelib/gameplay/nav/nav-mesh-probe.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-poly-ex.h"
#include "gamelib/ndphys/rigid-body.h"
#include "gamelib/util/exception-handling.h"

/// --------------------------------------------------------------------------------------------------------------- ///
STATIC_ASSERT(sizeof(NavMesh::NavMeshGameData) <= NavMesh::kNumGameDataBytes);

NavMeshProbeDebug g_navMeshProbeDebug;
NavMeshDepenDebug g_navMeshDepenDebug;

/// --------------------------------------------------------------------------------------------------------------- ///
struct ProbeWork
{
	// input
	Point m_curPos;
	Vector m_move;
	I32F m_ignoreEdge;

	// output
	I32F m_clipEdge;
	Vector m_clipNormal;
};

static void TryAdvanceProbe(const NavPolyEx* pPolyEx, const NavPoly* pPoly, ProbeWork* pWork);

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMesh::IncOccupancyCount() const
{
	if (!m_gameData.m_pOccupancyCount)
		return false;

	const I64 prevVal = m_gameData.m_pOccupancyCount->Add(1);
	if (prevVal >= m_gameData.m_maxOccupancyCount)
	{
		m_gameData.m_pOccupancyCount->Sub(1);
		return false;
	}
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMesh::DecOccupancyCount() const
{
	if (!m_gameData.m_pOccupancyCount)
		return false;

	const I64 prevVal = m_gameData.m_pOccupancyCount->Sub(1);
	if (prevVal < 1)
	{
		m_gameData.m_pOccupancyCount->Add(1);
		return false;
	}
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMesh::AtMaxOccupancyCount() const
{
	return m_gameData.m_pOccupancyCount ?
		   (m_gameData.m_pOccupancyCount->Get() >= m_gameData.m_maxOccupancyCount) :
		   false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMesh::PointInBoundingBoxWs(Point_arg worldPt, Scalar_arg radius) const
{
	const Vector r = m_vecRadius + radius;
	Point parentPt = WorldToParent(worldPt);
	Point localPt = ParentToLocal(parentPt);
	Vector absLocalPt = Abs(localPt - SMath::kOrigin);
	return AllComponentsLessThanOrEqual(absLocalPt, r);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMesh::ConfigureParentSpace(const Locator& parentAlignWs, const RigidBody* pBindTarget)
{
	const Locator parentSpace = pBindTarget ? pBindTarget->GetLocatorCm() : Locator(kIdentity);

	// pt is the object align in parent space
	const Locator parentAlignPs = parentSpace.UntransformLocator(parentAlignWs);
	const Locator origin = Locator(parentAlignPs.Pos() + m_originOffsetFromObject, parentAlignPs.Rot());

	m_pBoundFrame->SetBinding(Binding(pBindTarget), origin);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMesh::IsValid() const
{
	return m_managerId.AsU64() != NavMeshHandle::kInvalidMgrId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMesh::IsKnownStaticBlocker(StringId64 blockerId) const
{
	if (!m_pKnownStaticBlockers)
		return false;

	if (IsStringIdInList(blockerId, m_pKnownStaticBlockers))
	{
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMesh::NavMeshForcesSwim() const
{
	return FALSE_IN_FINAL_BUILD(g_navCharOptions.m_forceSwimMesh) || m_gameData.m_flags.m_navMeshForcesSwim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavMesh::GetNumKnownStaticBlockers() const
{
	U32F count = 0;
	if (m_pKnownStaticBlockers)
	{
		const StringId64* pSid = m_pKnownStaticBlockers;
		while (pSid[count] != INVALID_STRING_ID_64)
		{
			++count;
		}
	}
	return count;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NavMesh::GetCollisionLevelId() const
{
	return GetTagData(SID("collision-level"), INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// For live update, redirect the entity DB pointer from the PAK file data to a DB allocated in debug memory
void NavMesh::DebugOverrideEntityDB(const EntityDB* pDebugEntityDB)
{
	STRIP_IN_FINAL_BUILD;
	m_pEntityDBTable[0] = pDebugEntityDB;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavMesh::GetPolyCircleIntersectionsLs(const NavPoly** ppPolys,
										   U32F maxPolyCount,
										   Point_arg ptLs,
										   Scalar_arg radius,
										   Point* const pPoints /* = nullptr */) const
{
	PROFILE_ACCUM(GetPolyCircleIntersectionsLs);

	U32F polyCount = 0;
	BitArray<NavPoly::kMaxPolyCount> bits;
	const BitArrayGridHash* pPolyGridHash = GetPolyGridHash();
	NAV_ASSERT(pPolyGridHash);

	pPolyGridHash->SearchNearest(&bits, ptLs, radius);

	Scalar radius2 = Sqr(radius);
	BitArray<NavPoly::kMaxPolyCount>::Iterator iter(&bits);

	for (U32F iPoly = iter.First(); iPoly < iter.End(); iPoly = iter.Advance())
	{
		const NavPoly& poly = m_pPolyArray[iPoly];
		if (!poly.IsValid())
			continue;

		Point polyPt;
		poly.FindNearestPointLs(&polyPt, ptLs);
		Scalar dist2 = DistXzSqr(polyPt, ptLs);
		if (dist2 < radius2)
		{
			if (polyCount < maxPolyCount)
			{
				ppPolys[polyCount] = &poly;
				if (pPoints)
					pPoints[polyCount] = polyPt;

				++polyCount;
			}
			else
			{
				break;
			}
		}
	}
	return polyCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavMesh::GetPolyBoxIntersectionsLs(const NavPoly** ppPolys, U32F maxPolyCount, Aabb_arg bboxLs) const
{
	PROFILE_ACCUM(GetPolyBoxIntersectionsLs);

	U32F polyCount = 0;

	BitArray<NavPoly::kMaxPolyCount> bits;
	const BitArrayGridHash* pPolyGridHash = GetPolyGridHash();
	NAV_ASSERT(pPolyGridHash);

	pPolyGridHash->SearchBox(&bits, bboxLs.m_min, bboxLs.m_max);

	BitArray<NavPoly::kMaxPolyCount>::Iterator iter(&bits);

	for (U32F iPoly = iter.First(); iPoly < iter.End(); iPoly = iter.Advance())
	{
		const NavPoly& poly = m_pPolyArray[iPoly];
		if (!poly.IsValid())
			continue;

		Aabb polyBoxLs;

		polyBoxLs.IncludePoint(poly.GetVertex(0));
		polyBoxLs.IncludePoint(poly.GetVertex(1));
		polyBoxLs.IncludePoint(poly.GetVertex(2));
		polyBoxLs.IncludePoint(poly.GetVertex(3));

		if (!bboxLs.Overlaps(polyBoxLs))
		{
			continue;
		}

		if (polyCount < maxPolyCount)
		{
			ppPolys[polyCount] = &poly;

			++polyCount;
		}
		else
		{
			break;
		}
	}

	return polyCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavMesh::GetPolyBoxIntersectionsPs(const NavPoly** ppPolys, U32F maxPolyCount, Aabb_arg bboxPs) const
{
	const Point minLs = ParentToLocal(bboxPs.m_min);
	const Point maxLs = ParentToLocal(bboxPs.m_max);
	const Aabb bboxLs = Aabb(minLs, maxLs);

	return GetPolyBoxIntersectionsLs(ppPolys, maxPolyCount, bboxLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavPoly* NavMesh::FindContainingPolyLs(Point_arg ptLs,
											 float yThreshold /* = 2.0f */,
											 Nav::StaticBlockageMask obeyedStaticBlockers /* = Nav::kStaticBlockageMaskAll */) const
{
	PROFILE_ACCUM(FindContainingPolyLs);

	const NavPoly* pOut = nullptr;

	BitArray<NavPoly::kMaxPolyCount> bits;
	const BitArrayGridHash* pPolyGridHash = GetPolyGridHash();
	NAV_ASSERT(pPolyGridHash);

	pPolyGridHash->SearchPoint(&bits, ptLs);

	Scalar zero = SCALAR_LC(0.0f);
	Scalar bestDist = yThreshold;

	BitArray<NavPoly::kMaxPolyCount>::Iterator iter(&bits);

	for (U32F iPoly = iter.First(); iPoly < iter.End(); iPoly = iter.Advance())
	{
		const NavPoly& poly = m_pPolyArray[iPoly];
		if (!poly.IsValid())
			continue;

		if (poly.IsBlocked(obeyedStaticBlockers))
			continue;

		if (poly.PolyContainsPointLs(ptLs))
		{
			Point boxMin = poly.GetBBoxMinLs();
			Point boxMax = poly.GetBBoxMaxLs();
			Scalar dist = Max(Max(zero, ptLs.Y() - boxMax.Y()),
								Max(zero, boxMin.Y() - ptLs.Y()));

			if (dist < bestDist)
			{
				bestDist = dist;
				pOut = &poly;
			}
		}
	}

	return pOut;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavPoly* NavMesh::FindContainingPolyPs(Point_arg ptPs,
											 float yThreshold /* = 2.0f */,
											 const Nav::StaticBlockageMask obeyedStaticBlockers /* = Nav::kStaticBlockageMaskAll */) const
{
	const Point ptLs = ParentToLocal(ptPs);
	return FindContainingPolyLs(ptLs, yThreshold, obeyedStaticBlockers);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMesh::ProbeResult NavMesh::ProbeLs(ProbeParams* pParams) const
{
	PROFILE_AUTO(Navigation);
	PROFILE_ACCUM(DynamicProbeLs);

	ProbeParams& params = *pParams;
	const NavPoly* pPoly = params.m_pStartPoly;
	const NavPolyEx* pPolyEx = params.m_dynamicProbe ? params.m_pStartPolyEx : nullptr;

	const NavBlockerBits& obeyedBlockers = params.m_obeyedBlockers;

	if (pPoly && pPoly->IsBlocked(pParams->m_obeyedStaticBlockers))
	{
		pPoly = nullptr;
		pPolyEx = nullptr;
	}

	if (pPoly && (pPoly->GetNavMeshHandle().GetManagerId() != m_managerId))
	{
		pPoly = nullptr;
		pPolyEx = nullptr;
	}

	if (pPoly && !pPoly->PolyContainsPointLs(params.m_start))
	{
		pPoly = nullptr;
		pPolyEx = nullptr;
	}

	if (pPoly == nullptr)
	{
		if (pPolyEx)
		{
			pPoly = pPolyEx->m_hOrgPoly.ToNavPoly();
		}
		else
		{
			pPoly = FindContainingPolyLs(params.m_start, 2.0f, pParams->m_obeyedStaticBlockers);
		}
	}
	else if (pPolyEx)
	{
		ASSERT(pPolyEx->m_hOrgPoly.ToNavPoly() == pPoly);
	}

	params.m_pHitPoly = params.m_pStartPoly;
	params.m_pHitPolyEx = params.m_pStartPolyEx;

	if (!pPoly || !pPoly->PolyContainsPointLs(params.m_start))
	{
		params.m_endPoint = params.m_start;
		params.m_edgeNormal = Vector(kZero);
		params.m_hitEdge = false;
		params.m_pHitPoly = nullptr;
		params.m_pHitPolyEx = nullptr;
		params.m_pReachedPoly = nullptr;
		params.m_pReachedPolyEx = nullptr;

		return ProbeResult::kErrorStartedOffMesh;
	}

	if ((nullptr == pPolyEx) && params.m_dynamicProbe)
	{
		pPolyEx = pPoly->FindContainingPolyExLs(params.m_start);
	}

	if (pPolyEx && pPolyEx->IsBlockedBy(obeyedBlockers))
	{
		params.m_endPoint = params.m_start;
		params.m_edgeNormal = Vector(kZero);
		params.m_hitEdge = false;
		params.m_pReachedPoly = params.m_pHitPoly = pPoly;
		params.m_pReachedPolyEx = params.m_pHitPolyEx = pPolyEx;
		params.m_impactPoint = params.m_start;

		return ProbeResult::kErrorStartNotPassable;
	}

	if (params.m_probeRadius > NDI_FLT_EPSILON)
	{
		return IntersectCapsuleLs(pParams, pPoly, pPolyEx);
	}

	ProbeWork work;
	work.m_curPos	  = params.m_start;
	work.m_move		  = params.m_move;
	work.m_ignoreEdge = -1;

	const Point destPos = params.m_start + params.m_move;

	ProbeResult result = ProbeResult::kReachedGoal;

	const NavMesh* pInitialMesh = this;
	const NavMesh* pCurMesh = this;
	const NavPoly* pPrevPoly = nullptr;
	const NavPolyEx* pPrevPolyEx = nullptr;

	static const U32F kAbortSentinal = 100000;
	U32F itrCount = 0;
	while (true)
	{
		if (itrCount >= kAbortSentinal)
		{
			result = ProbeResult::kErrorStartNotPassable;

			DebugCaptureProbeFailure(params);

			NAV_ASSERTF(false,
						("Degenerate failure trying to do poly marching [mesh: '%s' start: %s move: %s]",
						 GetName(),
						 PrettyPrint(params.m_start),
						 PrettyPrint(params.m_move)));
			break;
		}

		TryAdvanceProbe(pPolyEx, pPoly, &work);

		if (work.m_clipEdge == -1)
		{
			// no edge encountered
			params.m_endPoint = destPos;
			params.m_edgeNormal = Vector(kZero);
			params.m_hitEdge = false;
			result = ProbeResult::kReachedGoal;
			break;
		}

		// encountered edge -- determine if it is a boundary edge or not
		const NavPoly* pNextPoly = pPoly;
		const NavPolyEx* pNextPolyEx = pPolyEx;
		const NavMesh* pNextMesh = pCurMesh;

		const BoundaryFlags boundaryFlags = NavMesh::IsBlockingEdge(pCurMesh,
																	pPoly,
																	pPolyEx,
																	work.m_clipEdge,
																	params,
																	&pNextMesh,
																	&pNextPoly,
																	&pNextPolyEx);

		const bool isBoundary = boundaryFlags != NavMesh::kBoundaryNone;

		ASSERT(pPolyEx || pNextPolyEx || !pNextPoly || (pNextPoly != pPrevPoly));
		ASSERT(!pNextPolyEx || (pNextPolyEx != pPrevPolyEx));
		NAV_ASSERT(isBoundary || pNextPoly);

		work.m_curPos += work.m_move;

		if (isBoundary)
		{
			const Point e0 = pPolyEx ? pPolyEx->GetVertex(work.m_clipEdge)		: pPoly->GetVertex(work.m_clipEdge);
			const Point e1 = pPolyEx ? pPolyEx->GetNextVertex(work.m_clipEdge)	: pPoly->GetNextVertex(work.m_clipEdge);

			params.m_impactPoint = work.m_curPos;
			params.m_endPoint = work.m_curPos;
			params.m_edgeNormal = -work.m_clipNormal;
			params.m_hitEdge = true;
			params.m_hitBoundaryFlags = boundaryFlags;
			params.m_hitVert[0] = e0;
			params.m_hitVert[1] = e1;

			if (pCurMesh != pInitialMesh)
			{
				params.m_impactPoint = pInitialMesh->WorldToLocal(pCurMesh->LocalToWorld(params.m_impactPoint));
				params.m_endPoint = pInitialMesh->WorldToLocal(pCurMesh->LocalToWorld(params.m_endPoint));
				params.m_hitVert[0] = pInitialMesh->WorldToLocal(pCurMesh->LocalToWorld(params.m_hitVert[0]));
				params.m_hitVert[1] = pInitialMesh->WorldToLocal(pCurMesh->LocalToWorld(params.m_hitVert[1]));
			}

			result = ProbeResult::kHitEdge;
			break;
		}

		if (pNextMesh && (pNextMesh != pCurMesh))
		{
			work.m_curPos = pNextMesh->WorldToLocal(pCurMesh->LocalToWorld(work.m_curPos));
			pCurMesh = pNextMesh;
		}

		Point destPosLs = destPos;
		if (pCurMesh != pInitialMesh)
		{
			destPosLs = pCurMesh->WorldToLocal(pInitialMesh->LocalToWorld(destPos));
		}

		work.m_move = destPosLs - work.m_curPos;
		work.m_ignoreEdge = Nav::GetIncomingEdge(pNextPoly, pNextPolyEx, pPoly, pPolyEx);

		pPrevPoly = pPoly;
		pPrevPolyEx = pPolyEx;

		pPoly = pNextPoly;
		pPolyEx = pNextPolyEx;

		++itrCount;
	}

	params.m_pReachedMesh = pCurMesh;
	params.m_pReachedPoly = params.m_pHitPoly = pPoly;
	params.m_pReachedPolyEx = params.m_pHitPolyEx = pPolyEx;

#ifdef JBELLOMY
	if (FALSE_IN_FINAL_BUILD(false))
	{
		const Point endPosWs = LocalToWorld(params.m_endPoint);
		const Point endPosDestLs = params.m_pReachedMesh->WorldToLocal(endPosWs);

		if (params.m_pReachedPoly && !params.m_pReachedPoly->PolyContainsPointLs(endPosDestLs, 0.1f))
		{
			if (!g_navMeshProbeDebug.m_valid)
			{
				MsgOut("Reached Poly '%d' does not contain end pos %s (ls: %s)\n", params.m_pReachedPoly->GetId(),
					   PrettyPrint(endPosWs),
					   PrettyPrint(endPosDestLs));
			}

			DebugCaptureProbeFailure(params);
		}

		Vec4 vDots;
		if (params.m_pReachedPolyEx && !params.m_pReachedPolyEx->PolyContainsPointLs(endPosDestLs, &vDots, 0.1f))
		{
			if (!g_navMeshProbeDebug.m_valid)
			{
				MsgOut("Reached Poly Ex '%d' does not contain end pos %s (ls: %s) [dots: %s]\n", params.m_pReachedPolyEx->m_id,
					   PrettyPrint(endPosWs),
					   PrettyPrint(endPosDestLs),
					   PrettyPrint(vDots));

				MsgOut("  Center: %s\n", PrettyPrint(params.m_pReachedPolyEx->GetCentroid()));
				MsgOut("      v0: %s\n", PrettyPrint(params.m_pReachedPolyEx->GetVertex(0)));
				MsgOut("      v1: %s\n", PrettyPrint(params.m_pReachedPolyEx->GetVertex(1)));
				MsgOut("      v2: %s\n", PrettyPrint(params.m_pReachedPolyEx->GetVertex(2)));
				MsgOut("      v3: %s\n", PrettyPrint(params.m_pReachedPolyEx->GetVertex(3)));
			}

			DebugCaptureProbeFailure(params);
		}
	}
#endif

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMesh::ProbeResult NavMesh::ProbePs(ProbeParams* pParams) const
{
	const Point startPs = pParams->m_start;
	const Vector movePs = pParams->m_move;
	const Segment capsuleSegPs = pParams->m_capsuleSegment;

	pParams->m_start = ParentToLocal(startPs);
	pParams->m_move = ParentToLocal(movePs);
	pParams->m_capsuleSegment.a = ParentToLocal(pParams->m_capsuleSegment.a);
	pParams->m_capsuleSegment.b = ParentToLocal(pParams->m_capsuleSegment.b);

	ProbeResult res = ProbeLs(pParams);

	pParams->m_start = startPs;
	pParams->m_move = movePs;
	pParams->m_capsuleSegment = capsuleSegPs;

	pParams->m_endPoint = LocalToParent(pParams->m_endPoint);
	pParams->m_hitVert[0] = LocalToParent(pParams->m_hitVert[0]);
	pParams->m_hitVert[1] = LocalToParent(pParams->m_hitVert[1]);
	pParams->m_impactPoint = LocalToParent(pParams->m_impactPoint);

	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool CanCrossLink(const NavMesh::ClearanceParams::BaseProbeParams* const pParams, const NavMesh* const pTo)
{
	NAV_ASSERT(pTo);

	if (!pParams->m_crossLinks)
		return false;

	if ((int)pTo->GetNpcStature() < (int)pParams->m_minNpcStature)
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMesh::FindNearestPointLs(FindPointParams* pParams) const
{
	PROFILE_AUTO(Navigation);

	const float searchRadius = pParams->m_searchRadius;

	BitArray<NavPoly::kMaxPolyCount> bits;
	const BitArrayGridHash* pPolyGridHash = GetPolyGridHash();
	NAV_ASSERT(pPolyGridHash);
	NAV_ASSERT(IsReasonable(pParams->m_point));
	NAV_ASSERT(IsReasonable(searchRadius));

	pPolyGridHash->SearchNearest(&bits, pParams->m_point, searchRadius);

	float bestDist = 0.0f;
	const NavPoly* pNearest = pParams->m_pStartPoly;
	const NavPolyEx* pNearestEx = pParams->m_dynamicProbe ? pParams->m_pStartPolyEx : nullptr;
	Point nearestPt = pParams->m_point;

	if (pNearest && (pNearest->IsBlocked(pParams->m_obeyedStaticBlockers) || !pNearest->IsValid() || !pNearest->PolyContainsPointLs(pParams->m_point)))
	{
		pNearest = nullptr;
		pNearestEx = nullptr;
	}

	if (pNearestEx &&
		(pNearestEx->IsBlockedBy(pParams->m_obeyedBlockers)
		|| !pNearestEx->PolyContainsPointLs(pParams->m_point)))
	{
		pNearest = nullptr; // we don't need to necessarily research for a whole poly here, earmarked for future optimization
		pNearestEx = nullptr;
	}

	if (pNearest)
	{
		Point pos;
		Vector normal;
		pNearest->ProjectPointOntoSurfaceLs(&pos, &normal, pParams->m_point);
		bestDist = Dist(pos, pParams->m_point);
		nearestPt = pos;
	}
	else
	{
		BitArray<NavPoly::kMaxPolyCount>::Iterator iter(&bits);
		bestDist = kLargeFloat;

		for (U32F iPoly = iter.First(); iPoly < iter.End(); iPoly = iter.Advance())
		{
			const NavPoly& poly = m_pPolyArray[iPoly];
			if (!poly.IsValid())
				continue;
			if (poly.IsBlocked(pParams->m_obeyedStaticBlockers))
				continue;

			if (poly.IsStealth())
			{
				if (pParams->m_stealthOption == FindPointParams::kNonStealthOnly)
					continue;
			}
			else
			{
				if (pParams->m_stealthOption == FindPointParams::kStealthOnly)
					continue;
			}

			if (const NavPolyEx* pPolyExList = pParams->m_dynamicProbe ? poly.GetNavPolyExList() : nullptr)
			{
				bool contains = false;

				for (const NavPolyEx* pPolyEx = pPolyExList; pPolyEx; pPolyEx = pPolyEx->m_pNext)
				{
					if (pPolyEx->IsBlockedBy(pParams->m_obeyedBlockers))
						continue;

					if (pPolyEx->PolyContainsPointLs(pParams->m_point))
					{
						Point pos;
						Vector normal;
						pPolyEx->ProjectPointOntoSurfaceLs(&pos, &normal, pParams->m_point);
						bestDist = Dist(pos, pParams->m_point);
						nearestPt = pos;
						pNearest = &poly;
						pNearestEx = pPolyEx;

						contains = true;
						break;
					}

					Point polyExPt;
					const Scalar exDist = pPolyEx->FindNearestPointLs(&polyExPt, pParams->m_point);

					if (exDist < bestDist)
					{
						bestDist = exDist;
						pNearest = &poly;
						pNearestEx = pPolyEx;
						nearestPt = polyExPt;
					}
				}

				if (contains)
					break;
			}
			else if (poly.PolyContainsPointLs(pParams->m_point))
			{
				Point pos;
				Vector normal;
				poly.ProjectPointOntoSurfaceLs(&pos, &normal, pParams->m_point);
				bestDist = Dist(pos, pParams->m_point);
				nearestPt = pos;
				pNearest = &poly;
				pNearestEx = nullptr;
				break;
			}
			else
			{
				Point polyPt;
				const Scalar dist = poly.FindNearestPointLs(&polyPt, pParams->m_point);

				if (dist < bestDist)
				{
					bestDist = dist;
					pNearest = &poly;
					pNearestEx = nullptr;
					nearestPt = polyPt;
				}
			}
		}
	}

	if (pNearest && (pParams->m_depenRadius > NDI_FLT_EPSILON))
	{
		PROFILE(Navigation, FindNearestPoint_Depen);

		ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

		const NavMesh* pStartMesh = this;

		if (pParams->m_crossLinks && pNearest->IsLink())
		{
			const NavMesh* pLinkedMesh;
			if (const NavPoly* const pLinkedPoly = pNearest->GetLinkedPoly(&pLinkedMesh))
			{
				if (CanCrossLink(pParams, pLinkedMesh))
				{
					pNearest = pLinkedPoly;
					pNearestEx = nullptr;
					pStartMesh = pLinkedMesh;

					nearestPt = pStartMesh->WorldToLocal(LocalToWorld(nearestPt));

					Point pos;
					Vector normal;
					pLinkedPoly->ProjectPointOntoSurfaceLs(&pos, &normal, nearestPt);
					bestDist = Dist(pos, pParams->m_point);
					nearestPt = pos;

					pParams->m_point = pStartMesh->WorldToLocal(LocalToWorld(pParams->m_point));
				}
			}
		}

		if (pParams->m_dynamicProbe && !pNearestEx)
		{
			pNearestEx = pNearest->FindContainingPolyExLs(nearestPt);
		}

		if (LengthSqr(pParams->m_capsuleSegment.GetVec()) < NDI_FLT_EPSILON)
		{
			NavMeshDepenetrator2 depenProbe;
			depenProbe.Init(nearestPt, pStartMesh, *pParams);
			depenProbe.Execute(pStartMesh, pNearest, pNearestEx);

			pNearest = depenProbe.GetResolvedPoly();
			pNearestEx = depenProbe.GetResolvedPolyEx();

			if (pNearest)
			{
				nearestPt = depenProbe.GetResolvedPosLs();
			}
		}
		else
		{
			NavMeshStadiumDepenetrator depenProbe;
			depenProbe.Init(nearestPt, pStartMesh, *pParams);
			depenProbe.Execute(pStartMesh, pNearest, pNearestEx);

			pNearest = depenProbe.GetResolvedPoly();
			pNearestEx = depenProbe.GetResolvedPolyEx();

			if (pNearest)
			{
				nearestPt = depenProbe.GetResolvedPosLs();
			}
		}

		if (pStartMesh != this)
		{
			nearestPt = WorldToLocal(pStartMesh->LocalToWorld(nearestPt));
			pParams->m_point = WorldToLocal(pStartMesh->LocalToWorld(pParams->m_point));
		}

		NAV_ASSERT(!pNearest || pNearest->PolyContainsPointLs(pNearest->GetNavMesh()->WorldToLocal(LocalToWorld(nearestPt)), 0.1f));
		NAV_ASSERT(!pNearestEx || pNearestEx->PolyContainsPointLs(pNearest->GetNavMesh()->WorldToLocal(LocalToWorld(nearestPt)), nullptr, 0.1f));
	}

	pParams->m_pPoly = pNearest;
	pParams->m_pPolyEx = pNearestEx;
	NAV_ASSERT((((uintptr_t)pNearest) & 0xf) == 0);
	pParams->m_dist = bestDist;
	pParams->m_nearestPoint = nearestPt;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMesh::FindNearestPointPs(FindPointParams* pParams) const
{
	Point origPtWs = ParentToWorld(pParams->m_point);
	pParams->m_point = ParentToLocal(pParams->m_point);
	pParams->m_capsuleSegment.a = ParentToLocal(pParams->m_capsuleSegment.a);
	pParams->m_capsuleSegment.b = ParentToLocal(pParams->m_capsuleSegment.b);

	FindNearestPointLs(pParams);

	NAV_ASSERT((((uintptr_t) pParams->m_pPoly) & 0xf) == 0);

	pParams->m_capsuleSegment.a = LocalToParent(pParams->m_capsuleSegment.a);
	pParams->m_capsuleSegment.b = LocalToParent(pParams->m_capsuleSegment.b);

	pParams->m_nearestPoint = LocalToParent(pParams->m_nearestPoint);
	pParams->m_point = WorldToParent(origPtWs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMesh::ProbeResult NavMesh::CheckClearanceLs(ClearanceParams* pParams) const
{
	PROFILE_AUTO(Navigation);

	const NavMesh* pMesh = this;
	const NavPoly* pPoly = pParams->m_pStartPoly;
	const NavPolyEx* pPolyEx = pParams->m_pStartPolyEx;

	if (!pPoly || !pPoly->PolyContainsPointLs(pParams->m_point))
	{
		pPoly = FindContainingPolyLs(pParams->m_point);
	}

	if (!pPoly)
	{
		return ProbeResult::kErrorStartedOffMesh;
	}

	if (pParams->m_crossLinks && pPoly->IsLink())
	{
		const NavMesh* pLinkedMesh;
		if (const NavPoly* const pLinkedPoly = pPoly->GetLinkedPoly(&pLinkedMesh))
		{
			if (CanCrossLink(pParams, pLinkedMesh))
			{
				pPoly = pLinkedPoly;
				pMesh = pLinkedMesh;
				pPolyEx = nullptr;
			}
		}
	}

	if (pPoly && pPoly->IsBlocked(pParams->m_obeyedStaticBlockers))
	{
		return ProbeResult::kErrorStartNotPassable;
	}

	if (!pParams->m_dynamicProbe)
	{
		pPolyEx = nullptr;
	}
	else if (!pPolyEx || !pPolyEx->PolyContainsPointLs(pParams->m_point))
	{
		pPolyEx = pPoly->FindContainingPolyExLs(pParams->m_point);
	}

	if (pPolyEx && pPolyEx->IsBlockedBy(pParams->m_obeyedBlockers))
	{
		return ProbeResult::kErrorStartNotPassable;
	}

	{
		ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

		NavMeshClearanceProbe probe;
		probe.Init(pParams->m_point, pParams->m_radius, *pParams);
		probe.Execute(pMesh, pPoly, pPolyEx);

		pParams->m_hitEdge = probe.DidHitEdge();

		const Point impactPointLs = probe.GetImpactPosLs();
		if (pMesh == this)
		{
			pParams->m_impactPoint = impactPointLs;
		}
		else
		{
			pParams->m_impactPoint = WorldToLocal(pMesh->LocalToWorld(impactPointLs));
		}
	}

	return pParams->m_hitEdge ? ProbeResult::kHitEdge : ProbeResult::kReachedGoal;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMesh::ProbeResult NavMesh::CheckClearancePs(ClearanceParams* pParams) const
{
	const Point posPs = pParams->m_point;
	pParams->m_point = ParentToLocal(posPs);
	pParams->m_capsuleSegment.a = ParentToLocal(pParams->m_capsuleSegment.a);
	pParams->m_capsuleSegment.b = ParentToLocal(pParams->m_capsuleSegment.b);

	const ProbeResult res = CheckClearanceLs(pParams);

	pParams->m_point = LocalToParent(pParams->m_point);
	pParams->m_impactPoint = LocalToParent(pParams->m_impactPoint);
	pParams->m_capsuleSegment.a = LocalToParent(pParams->m_capsuleSegment.a);
	pParams->m_capsuleSegment.b = LocalToParent(pParams->m_capsuleSegment.b);

	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
NavMesh::BoundaryFlags NavMesh::IsBlockingEdge(const NavMesh* pMesh,
											   const NavPoly* pPoly,
											   const NavPolyEx* pPolyEx,
											   U32F iEdge,
											   const BaseProbeParams& params,
											   const NavMesh** ppMeshOut /* = nullptr */,
											   const NavPoly** ppPolyOut /* = nullptr */,
											   const NavPolyEx** ppPolyExOut /* = nullptr */)
{
	//PROFILE_AUTO(Navigation);

	NAV_ASSERT(pMesh);
	NAV_ASSERT(pPoly);

	const NavPoly* pAdjPoly = nullptr;
	const NavPolyEx* pAdjPolyEx = nullptr;
	const NavMesh* pAdjMesh = nullptr;

	if (pPolyEx)
	{
		const NavManagerId adjId = pPolyEx->GetAdjacentPolyId(iEdge);

		if (adjId.m_navMeshIndex != pMesh->m_managerId.m_navMeshIndex)
		{
			if (params.m_crossLinks)
			{
				if (const NavMesh* const pCandidateAdjMesh = NavMeshHandle(adjId).ToNavMesh())
				{
					if (CanCrossLink(&params, pCandidateAdjMesh))
						pAdjMesh = pCandidateAdjMesh;
				}
			}
			else
			{
				pAdjMesh = nullptr;
			}
		}
		else
		{
			pAdjMesh = pMesh;
		}

		if (!pAdjMesh)
		{
			// nav mesh not loaded?
		}
		else if (adjId.m_u64 == NavManagerId::kInvalidMgrId)
		{
			// blocking edge
		}
		else if (const NavPolyEx* pAdjEx = NavPolyExHandle(adjId).ToNavPolyEx())
		{
			pAdjPolyEx = pAdjEx;
		}
		else if (adjId.m_iPoly < pAdjMesh->m_polyCount)
		{
			pAdjPoly = &pAdjMesh->GetPoly(adjId.m_iPoly);
		}
		else
		{
			NAV_ASSERTF(false, ("Nonsensical nav mesh data?"));
		}
	}
	else
	{
		const U32F adjId = pPoly->GetAdjacentId(iEdge);

		if (adjId == NavPoly::kNoAdjacent)
		{
			// blocking edge
		}
		else if (adjId < pMesh->m_polyCount)
		{
			pAdjPoly = &pMesh->GetPoly(adjId);
			pAdjMesh = pMesh;

			if (params.m_crossLinks && pAdjPoly->IsLink())
			{
				const NavMesh* pOtherPreLinkMesh;
				if (const NavPoly* const pOtherPreLinkPoly = pAdjPoly->GetLinkedPoly(&pOtherPreLinkMesh))
				{
					if (CanCrossLink(&params, pOtherPreLinkMesh))
					{
						pAdjPoly = pOtherPreLinkPoly;
						pAdjMesh = pOtherPreLinkMesh;
					}
				}
			}
		}
		else
		{
			NAV_ASSERTF(false, ("Nonsensical nav mesh data?"));
		}
	}

	if (!pAdjPolyEx && pAdjPoly && params.m_dynamicProbe)
	{
		NAV_ASSERT(pAdjMesh);

		const NavManagerId sourcePolyId = pPolyEx ? pPolyEx->GetNavManagerId() : pPoly->GetNavManagerId();

		for (const NavPolyEx* pNextPolyEx = pAdjPoly->GetNavPolyExList(); pNextPolyEx; pNextPolyEx = pNextPolyEx->m_pNext)
		{
			for (U32F iSrcEdge = 0; iSrcEdge < pNextPolyEx->GetVertexCount(); ++iSrcEdge)
			{
				if (pNextPolyEx->m_adjPolys[iSrcEdge] == sourcePolyId)
				{
					pAdjPolyEx = pNextPolyEx;
					break;
				}
			}
		}
	}

	if (pAdjPolyEx && !pAdjPoly)
	{
		NAV_ASSERT(pAdjMesh);

		const NavPolyHandle& hAdjPoly = pAdjPolyEx->m_hOrgPoly;
		if (pAdjMesh && (hAdjPoly.GetManagerId().m_navMeshIndex == pAdjMesh->m_managerId.m_navMeshIndex))
		{
			pAdjPoly = &pAdjMesh->GetPoly(hAdjPoly.GetPolyId());
		}
		else
		{
			pAdjPoly = hAdjPoly.ToNavPoly();
		}
	}

	if (ppPolyExOut)
	{
		*ppPolyExOut = pAdjPolyEx;
	}

	if (ppPolyOut)
	{
		*ppPolyOut = pAdjPoly;
	}

	if (ppMeshOut)
	{
		*ppMeshOut = pAdjMesh;
	}

	NAV_ASSERT(!pAdjPoly || !pAdjMesh || (pAdjPoly->GetNavMeshHandle().GetManagerId() == pAdjMesh->GetManagerId()));

	const bool curDynBlocking = !params.m_dynamicProbe || (pPolyEx && pPolyEx->IsBlockedBy(params.m_obeyedBlockers));
	const bool adjDynBlocking = !params.m_dynamicProbe || (pAdjPolyEx && pAdjPolyEx->IsBlockedBy(params.m_obeyedBlockers));

	const bool curBlocking = !pPoly || (pPoly->IsBlocked(params.m_obeyedStaticBlockers)) || !pPoly->IsValid();
	const bool adjBlocking = !pAdjPoly || (pAdjPoly->IsBlocked(params.m_obeyedStaticBlockers)) || !pAdjPoly->IsValid();

	const bool curStealth = (params.m_obeyStealthBoundary && pPoly) ? pPoly->IsStealth() : false;
	const bool adjStealth = (params.m_obeyStealthBoundary && pAdjPoly) ? pAdjPoly->IsStealth() : false;

	// curBlocking == adjBlocking can be true and yet not have an adj poly
	// if you're going from a statically blocked poly through an edge with no neighbor
	const bool staticBlocked = !pAdjPoly || (curBlocking != adjBlocking);
	const bool dynamicBlocked = curDynBlocking != adjDynBlocking;
	const bool stealthBlocked = pAdjPoly && (curStealth != adjStealth);

	U32 res = kBoundaryNone;

	if (staticBlocked)
	{
		res |= kBoundaryTypeStatic;
	}

	if (dynamicBlocked)
	{
		res |= kBoundaryTypeDynamic;
	}

	if (stealthBlocked)
	{
		res |= kBoundaryTypeStealth;
	}

	if ((staticBlocked && curBlocking) || (dynamicBlocked && curDynBlocking) || (stealthBlocked && curStealth))
	{
		res |= kBoundaryInside;
	}

	return BoundaryFlags(res);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMesh::WalkPolysInLineLs(ProbeParams& params, FnVisitNavPoly visitFunc, uintptr_t userData) const
{
	PROFILE_AUTO(Navigation);
	PROFILE_ACCUM(WalkPolysInLineLs);

	const NavPoly* pPoly = params.m_pStartPoly;
	const NavPolyEx* pPolyEx = params.m_dynamicProbe ? params.m_pStartPolyEx : nullptr;

	if (pPoly == nullptr)
	{
		if (pPolyEx)
		{
			pPoly = pPolyEx->m_hOrgPoly.ToNavPoly();
		}
		else
		{
			pPoly = FindContainingPolyLs(params.m_start, 2.0f, params.m_obeyedStaticBlockers);
		}
	}
	else if (pPolyEx)
	{
		NAV_ASSERT(pPolyEx->m_hOrgPoly.ToNavPoly() == pPoly);
	}

	if (!pPoly || !pPoly->PolyContainsPointLs(params.m_start, params.m_polyContainsPointTolerance))
	{
		//return ProbeResult::kErrorStartedOffMesh;
		return;
	}

	if ((nullptr == pPolyEx) && params.m_dynamicProbe)
	{
		pPolyEx = pPoly->FindContainingPolyExLs(params.m_start);
	}

	ProbeWork work;
	work.m_curPos	  = params.m_start;
	work.m_move		  = VectorXz(params.m_move);
	work.m_ignoreEdge = -1;

	const Point destPos = params.m_start + params.m_move;

	const NavMesh* pInitialMesh = this;
	const NavMesh* pCurMesh = this;

	static const U32F kAbortSentinal = 100000;
	U32F itrCount = 0;
	while (true)
	{
		if (itrCount >= kAbortSentinal)
		{
			NAV_ASSERTF(false,
						("Degenerate failure trying to do poly marching [mesh: '%s' start: %s move: %s]",
						 GetName(),
						 PrettyPrint(params.m_start),
						 PrettyPrint(params.m_move)));
			break;
		}

		visitFunc(pPoly, pPolyEx, userData);

		TryAdvanceProbe(pPolyEx, pPoly, &work);

		if (work.m_clipEdge == -1)
		{
			// no edge encountered
			work.m_curPos += work.m_move;
			break;
		}

		const NavPoly* pNextPoly = pPoly;
		const NavPolyEx* pNextPolyEx = pPolyEx;
		const NavMesh* pNextMesh = pCurMesh;

		const bool isBoundary = NavMesh::IsBlockingEdge(pCurMesh,
														pPoly,
														pPolyEx,
														work.m_clipEdge,
														params,
														&pNextMesh,
														&pNextPoly,
														&pNextPolyEx) != kBoundaryNone;

		if (!pNextPoly)
		{
			break;
		}

		work.m_curPos += work.m_move;
		work.m_ignoreEdge = Nav::GetIncomingEdge(pNextPoly, pNextPolyEx, pPoly, pPolyEx);

		if (pNextMesh && (pNextMesh != pCurMesh))
		{
			work.m_curPos = pNextMesh->WorldToLocal(pCurMesh->LocalToWorld(work.m_curPos));
			pCurMesh = pNextMesh;
		}

		Point destPosLs = destPos;
		if (pCurMesh != pInitialMesh)
		{
			destPosLs = pCurMesh->WorldToLocal(pInitialMesh->LocalToWorld(destPos));
		}

		work.m_move = destPosLs - work.m_curPos;

		pPoly = pNextPoly;
		pPolyEx = pNextPolyEx;

		++itrCount;
	}

	params.m_endPoint	  = work.m_curPos;
	params.m_pReachedMesh = pCurMesh;
	params.m_pReachedPoly = pPoly;
	params.m_pReachedPolyEx = pPolyEx;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMesh::WalkPolysInLinePs(ProbeParams& params, FnVisitNavPoly visitFunc, uintptr_t userData) const
{
	params.m_start = ParentToLocal(params.m_start);
	params.m_move  = ParentToLocal(params.m_move);

	WalkPolysInLineLs(params, visitFunc, userData);

	if (params.m_pReachedMesh)
	{
		params.m_start = params.m_pReachedMesh->LocalToParent(params.m_start);
		params.m_move = params.m_pReachedMesh->LocalToParent(params.m_move);
		params.m_endPoint = params.m_pReachedMesh->LocalToParent(params.m_endPoint);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMesh::ProbeResult NavMesh::IntersectCapsuleLs(ProbeParams* pParams,
												 const NavPoly* pContainingPoly,
												 const NavPolyEx* pContainingPolyEx) const
{
	PROFILE_AUTO(Navigation);

	if (!pParams)
		return ProbeResult::kErrorStartNotPassable;

	if (!pContainingPoly)
		return ProbeResult::kErrorStartedOffMesh;

	const float radius = pParams->m_probeRadius;
	NAV_ASSERT(radius >= NDI_FLT_EPSILON);

	pParams->m_hitEdge = false;

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);
	NavMeshCapsuleProbe probe;

	probe.Init(pParams, this);
	probe.Execute(this, pContainingPoly, pContainingPolyEx);

	if (pParams->m_hitEdge)
	{
		if (DistSqr(pParams->m_start, pParams->m_impactPoint) < Sqr(radius))
		{
			return ProbeResult::kErrorStartNotPassable;
		}
		else
		{
			return ProbeResult::kHitEdge;
		}
	}
	else
	{
		return ProbeResult::kReachedGoal;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static I32 CompareNavPolyDistEntries(const NavPolyDistEntry& a, const NavPolyDistEntry& b)
{
	const float distA = a.GetDist();
	const float distB = b.GetDist();

	if (distA < distB)
	{
		return -1;
	}

	if (distB < distA)
	{
		return +1;
	}

	return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const EntityDB* NavMesh::GetEntityDBByIndex(U32F iDb) const
{
	if (iDb >= m_numEntityDBs)
		return nullptr;

	return m_pEntityDBTable[iDb];
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IntersectProbeEdgeXz(const Segment& seg0, const Segment& seg1, Scalar& t0, Scalar& t1)
{
	static const Scalar kOverlapEpsilon = SCALAR_LC(0.0001f);
	static const Scalar kOne = SCALAR_LC(1.0f);
	static const Scalar zero = kZero;

	const Vector u = seg0.b - seg0.a;
	const Vector v = seg1.b - seg1.a;
	const Vector w = seg0.a - seg1.a;

	const Scalar d = u.X() * v.Z() - u.Z() * v.X();
	const Scalar s = v.X() * w.Z() - v.Z() * w.X();
	const Scalar t = u.X() * w.Z() - u.Z() * w.X();

	if (Abs(d) < kOverlapEpsilon)
	{
		return false;
	}

	const Scalar denom = AccurateDiv(kOne, d);
	t0 = s * denom;
	t1 = t * denom;

	if (t0 < zero || t0 > kOne)
	{
		return false;
	}

	if (t1 < zero || t1 > kOne)
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void TryAdvanceProbe(const NavPolyEx* pPolyEx, const NavPoly* pPoly, ProbeWork* pWork)
{
	PROFILE_ACCUM(TryAdvanceProbe);

	NAV_ASSERT(pWork);
	if (!pWork)
		return;

	pWork->m_clipEdge = -1;

	if (!pPolyEx && !pPoly)
		return;

	static const Scalar zero = SCALAR_LC(0.0f);
	static const Scalar one = SCALAR_LC(1.0f);
	static const Scalar epsilon = SCALAR_LC(0.001f);

	Scalar moveLen = zero;
	const Vector moveDir = AsUnitVectorXz(pWork->m_move, kZero, moveLen);

	if (moveLen < epsilon)
		return;

	Scalar bestOverhangDist = kLargeFloat;
	I32F iBestEdge = -1;
	Vector bestNormVec = kZero;
	Vector bestMoveVec = kZero;

	const U32F vertCount = pPolyEx ? pPolyEx->GetVertexCount() : pPoly->GetVertexCount();

	for (U32F iEdge = 0; iEdge < vertCount; ++iEdge)
	{
		if (iEdge == pWork->m_ignoreEdge)
			continue;

		const Point v0 = pPolyEx ? pPolyEx->GetVertex(iEdge)	 : pPoly->GetVertex(iEdge);
		const Point v1 = pPolyEx ? pPolyEx->GetNextVertex(iEdge) : pPoly->GetNextVertex(iEdge);

		const Vector normVec = RotateY90(VectorXz(v0 - v1));
		const Scalar dirDot = Dot(normVec, moveDir);

		// is move vector heading towards the edge?
		if (dirDot < zero)
			continue;

		const Scalar maxEdgeOverhang = SCALAR_LC(0.015f);

		const Point probeStart = pWork->m_curPos - (moveDir * 0.1f);
		const Segment probeSeg = Segment(probeStart, pWork->m_curPos + pWork->m_move);
		Scalar edgeLen;
		const Vector edgeDir = AsUnitVectorXz(v1 - v0, kZero, edgeLen);
		const Vector nudgeVec = edgeDir * maxEdgeOverhang;

		const Scalar probeEdgeLen = (maxEdgeOverhang * 2.0f) + edgeLen;
		const Scalar safeLo = AccurateDiv(maxEdgeOverhang, probeEdgeLen);
		const Scalar safeHi = AccurateDiv(maxEdgeOverhang + edgeLen, probeEdgeLen);

		const Segment edgeSeg = Segment(v0 - nudgeVec, v1 + nudgeVec);

		Scalar t0, t1;
		const bool probeHit = IntersectProbeEdgeXz(probeSeg, edgeSeg, t0, t1);
		if (!probeHit)
		{
			continue;
		}

		if ((t1 > safeLo) && (t1 < safeHi))
		{
			const Point clipPos = Lerp(probeSeg.a, probeSeg.b, t0);
			const Vector clipMove = clipPos - pWork->m_curPos;
			const Scalar clipDot = Max(DotXz(clipMove, moveDir), zero);

			pWork->m_move = moveDir * clipDot;
			pWork->m_clipNormal = SafeNormalize(normVec, kZero);
			pWork->m_clipEdge = iEdge;

			iBestEdge = -1;
			break;
		}
		else
		{
			const Point t1Pos = Lerp(edgeSeg.a, edgeSeg.b, t1);
			const Point mid = Lerp(v0, v1, 0.5f);
			const Point overhangPos = Lerp(edgeSeg.a, edgeSeg.b, t1 < SCALAR_LC(0.5f) ? safeLo : safeHi);
			const Scalar overhang = DistXz(t1Pos, mid) - DistXz(overhangPos, mid);

			if (overhang < bestOverhangDist)
			{
				iBestEdge = iEdge;

				const Point clipPos	  = Lerp(probeSeg.a, probeSeg.b, t0);
				const Vector clipMove = clipPos - pWork->m_curPos;
				const Scalar clipDot  = Max(DotXz(clipMove, moveDir), zero);

				bestMoveVec		 = moveDir * clipDot;
				bestOverhangDist = overhang;
				bestNormVec		 = normVec;
			}
		}
	}

	if (iBestEdge >= 0)
	{
		pWork->m_move		= bestMoveVec;
		pWork->m_clipNormal = SafeNormalize(bestNormVec, kZero);
		pWork->m_clipEdge	= iBestEdge;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMesh::DebugCaptureProbeFailure(const ProbeParams& paramsLs, bool force /* = false */) const
{
	STRIP_IN_FINAL_BUILD;

	if (g_navMeshProbeDebug.m_valid && !force)
		return;

	MsgOut("\n\n========================[ Nav Mesh Probe Failure ]========================\n");

	MsgOut("NavMesh: '%s' [level: '%s']\n", GetName(), DevKitOnly_StringIdToString(m_levelId));
	MsgOut("Params:\n");
	paramsLs.DebugPrint(kMsgOut);
	MsgOut("\n");
	MsgOut("============================================================================\n");

	g_navMeshProbeDebug.m_pNavMesh = this;
	g_navMeshProbeDebug.m_paramsLs = paramsLs;
	g_navMeshProbeDebug.m_valid = true;

#ifdef JBELLOMY
	if (paramsLs.m_dynamicProbe)
	{
		g_ndConfig.m_pDMenuMgr->SetProgPause(true);
		g_ndConfig.m_pDMenuMgr->SetProgPauseLock(true);
		g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchInput = true;
		g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchProcessing = true;
	}
#else
	MailNavMeshReportTo("john_bellomy@naughtydog.com");
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMesh::BaseProbeParams::DebugPrint(MsgOutput chan) const
{
	STRIP_IN_FINAL_BUILD;

	const U32F ppFlags = kPrettyPrintAppendF | kPrettyPrintInsertCommas | kPrettyPrintUseParens;

	if (m_pStartPoly)
	{
		PrintTo(chan,
				"params.m_pStartPoly = FindContainingPoly(Point%s); // [%s : %d]\n",
				PrettyPrint(m_pStartPoly->GetCentroid(), ppFlags),
				m_pStartPoly->GetNavMesh()->GetName(),
				m_pStartPoly->GetId());
	}
	else
	{
		PrintTo(chan, "params.m_pStartPoly = nullptr;\n");
	}

	if (m_pStartPolyEx)
	{
		PrintTo(chan,
				"params.m_pStartPolyEx = FindContainingPolyEx(Point%s);\n",
				PrettyPrint(m_pStartPolyEx->GetCentroid(), ppFlags));
	}
	else
	{
		PrintTo(chan, "params.m_pStartPolyEx = nullptr;\n");
	}

	for (U32F i = 0; i < m_obeyedBlockers.GetNumBlocks(); ++i)
	{
		PrintTo(chan, "params.m_obeyedBlockers.SetBlock(%d, 0x%x);\n", i, m_obeyedBlockers.GetBlock(i));
	}

	PrintTo(chan, "params.m_dynamicProbe = %s;\n", m_dynamicProbe ? "true" : "false");
	PrintTo(chan, "params.m_obeyedStaticBlockers = %d;\n", m_obeyedStaticBlockers);
	PrintTo(chan, "params.m_obeyStealthBoundary = %s;\n", m_obeyStealthBoundary ? "true" : "false");
	PrintTo(chan, "params.m_crossLinks = %s;\n", m_crossLinks ? "true" : "false");
	PrintTo(chan, "params.m_minNpcStature = %s;\n", NavMesh::NpcStatureToString(m_minNpcStature));
	PrintTo(chan, "params.m_debugDraw = %s;\n", m_debugDraw ? "true" : "false");
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMesh::FindPointParams::DebugPrint(MsgOutput chan) const
{
	STRIP_IN_FINAL_BUILD;

	BaseProbeParams::DebugPrint(chan);

	const U32F ppFlags = kPrettyPrintAppendF | kPrettyPrintInsertCommas | kPrettyPrintUseParens;

	PrintTo(chan, "params.m_point = Point%s;\n", PrettyPrint(m_point, ppFlags));
	PrintTo(chan, "params.m_searchRadius = %f;\n", m_searchRadius);
	PrintTo(chan, "params.m_depenRadius = %f;\n", m_depenRadius);

	switch (m_stealthOption)
	{
	case kAll:
		PrintTo(chan, "params.m_stealthOption = NavMesh::FindPointParams::kAll;\n");
		break;
	case kNonStealthOnly:
		PrintTo(chan, "params.m_stealthOption = NavMesh::FindPointParams::kNonStealthOnly;\n");
		break;
	case kStealthOnly:
		PrintTo(chan, "params.m_stealthOption = NavMesh::FindPointParams::kStealthOnly;\n");
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMesh::ProbeParams::DebugPrint(MsgOutput chan) const
{
	STRIP_IN_FINAL_BUILD;

	BaseProbeParams::DebugPrint(chan);

	const U32F ppFlags = kPrettyPrintAppendF | kPrettyPrintInsertCommas | kPrettyPrintUseParens;

	PrintTo(chan, "params.m_start = Point%s;\n", PrettyPrint(m_start, ppFlags));
	PrintTo(chan, "params.m_move = Vector%s;\n", PrettyPrint(m_move, ppFlags));
	PrintTo(chan, "params.m_probeRadius = %f;\n", m_probeRadius);
	PrintTo(chan, "params.m_polyContainsPointTolerance = %f;\n", m_polyContainsPointTolerance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMesh::ClearanceParams::DebugPrint(MsgOutput chan) const
{
	STRIP_IN_FINAL_BUILD;

	BaseProbeParams::DebugPrint(chan);

	const U32F ppFlags = kPrettyPrintAppendF | kPrettyPrintInsertCommas | kPrettyPrintUseParens;

	PrintTo(chan, "params.m_point = Point%s;\n", PrettyPrint(m_point, ppFlags));
	PrintTo(chan, "params.m_radius = %f;\n", m_radius);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MailNavMeshReportTo(const char* toAddr, const char* subject /* = nullptr */)
{
	STRIP_IN_FINAL_BUILD_VALUE(false);

	if (!toAddr)
		return false;

	if (strlen(g_realUserName) <= 0)
		return false;

	StringBuilder<256> fromAddr;
	fromAddr.append_format("%s-devkit@naughtydog.com", g_realUserName);

	beginMail(toAddr, subject ? subject : "AI Log: Nav Mesh Report", fromAddr.c_str());

	{
		ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);
		const U32 kMailBufferSize = 16 * 1024;
		char* pMailBuffer = NDI_NEW char[kMailBufferSize];
		StringBuilderExternal* pSb = StringBuilderExternal::Create(pMailBuffer, kMailBufferSize);

		pSb->append_format("Something bad happened. Here's the stuff.\n\n");

		GameDumpProgramStateInternal(*pSb, nullptr, 0, nullptr, nullptr, kEhcsSuccess, nullptr, false, false);

		addMailBody(*pSb);
	}

	if (const DoutMem* pMsgHistory = MsgGetHistoryMem())
	{
		ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);
		const U32 bufferSize = AlignSizeLower(tempAlloc.GetFreeSize(), kAlign8);
		char* pMsgBuffer = NDI_NEW(kAlign8) char[bufferSize];
		const U32F histSize = MsgGetHistory(pMsgBuffer, bufferSize);
		addMailAttachment(pMsgBuffer, histSize, "tty.txt");
	}

	{
		ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);
		const U32 bufferSize = AlignSizeLower(tempAlloc.GetFreeSize(), kAlign8);
		char* pInputBuf = NDI_NEW(kAlign8) char[bufferSize];

		DoutMem inputBuf = DoutMem("Patch Input", pInputBuf, bufferSize);
		DebugPrintNavMeshPatchInput(&inputBuf, g_navExData.GetNavMeshPatchInputBuffer());

		addMailAttachment(inputBuf.GetBuffer(), inputBuf.Length(), "patch-input.txt");
	}

	endMail();

	return true;
}
