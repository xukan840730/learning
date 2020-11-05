/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-poly.h"

#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-ex-data.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-mesh-gap-ex.h"
#include "gamelib/gameplay/nav/nav-mesh-gap.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/nav-poly-ex.h"
#include "gamelib/gameplay/nav/nav-poly-util.h"

/// --------------------------------------------------------------------------------------------------------------- ///
STATIC_ASSERT(sizeof(NavPoly) == 128);

/// --------------------------------------------------------------------------------------------------------------- ///
DC::StealthVegetationHeight NavPoly::GetStealthVegetationHeight() const
{
	if (!m_flags.m_stealth)
		return DC::kStealthVegetationHeightNone;

	const EntityDB* pEntityDB = GetEntityDB();

	if (!pEntityDB)
		return DC::kStealthVegetationHeightNone;

	return NavMesh::GetStealthVegetationHeight(pEntityDB);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPoly::SetBlockageMask(const Nav::StaticBlockageMask blockageMask)
{
	if (m_blockageMask != blockageMask)
	{
		// rw lock isn't reentrant, so this will currently deadlock and I'm not sure we have a need for it right now.
		// tbh this needs to be solved a better way. Random people dereferencing handles to nav mesh/poly pointers is
		// bad news bears.
		NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

		m_blockageMask = blockageMask;

		const U32F pathNodeId = GetPathNodeId();
		if (pathNodeId < g_navPathNodeMgr.GetMaxNodeCount())
		{
			NavPathNode& node = g_navPathNodeMgr.GetNode(pathNodeId);
			node.SetBlockageMask(blockageMask);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPoly::ValidateRegisteredActionPacks() const
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	ActionPack* const* ppAp = &m_pRegistrationList;
	for (;;)
	{
		ActionPack* pAp = *ppAp;

		if (nullptr == pAp)
			break;

		NAV_ASSERT(!pAp->IsCorrupted());
		NAV_ASSERT(pAp->GetRegisteredNavLocation().ToNavPoly() == this);

		// advance to next element of the list
		ppAp = &(pAp->m_pNavRegistrationListNext);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPoly::RegisterActionPack(ActionPack* pActionPack)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());
	NAV_ASSERT(!pActionPack->IsCorrupted());
	NAV_ASSERT(!pActionPack->GetRegisteredNavLocation().IsValid());

	ActionPack** ppAp = &m_pRegistrationList;
	for (;;)
	{
		ActionPack* pAp = *ppAp;
		// is the action pack already in the list?
		if (pAp == pActionPack)
		{
			// no need to add it again
			break;
		}
		// are we at the end of the list?
		if (pAp == nullptr)
		{
			// add it
			*ppAp = pActionPack;
			break;
		}

		NAV_ASSERT(!pAp->IsCorrupted());

		// advance to next element of the list
		ppAp = &(pAp->m_pNavRegistrationListNext);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavPoly::UnregisterActionPack(ActionPack* pActionPack)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	bool found = false;

	ActionPack** ppAp = &m_pRegistrationList;
	for (;;)
	{
		ActionPack* pAp = *ppAp;
		// have we found the action pack we are looking for?
		if (pAp == pActionPack)
		{
			// remove it
			*ppAp = pActionPack->m_pNavRegistrationListNext;
			pActionPack->m_pNavRegistrationListNext = nullptr;
			found = true;
			break;
		}
		// reached end of list?
		if (pAp == nullptr)
		{
			// not found
			break;
		}
		// advance to next element of the list
		ppAp = &(pAp->m_pNavRegistrationListNext);
	}

	return found;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPoly::RelocateActionPack(ActionPack* pActionPack, ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	// trivial case, our head pointer is the one we want to relocate
	if (m_pRegistrationList == pActionPack)
	{
		RelocatePointer(m_pRegistrationList, delta, lowerBound, upperBound);
		return;
	}

	ActionPack** ppAp = &m_pRegistrationList;
	for (;;)
	{
		ActionPack* pAp = *ppAp;

		if (pAp == nullptr)
			break;

		if (pAp->m_pNavRegistrationListNext == pActionPack)
		{
			// found referencing AP, relocate the pointer it has internally
			RelocatePointer(pAp->m_pNavRegistrationListNext, delta, lowerBound, upperBound);
			break;
		}
		// advance to next element of the list
		ppAp = &(pAp->m_pNavRegistrationListNext);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavPoly::IsAdjacentPoly(U32F polyId) const
{
	NAV_ASSERT(polyId != NavPoly::kNoAdjacent);
	U32F iEdgeCount = GetVertexCount();
	for (U32F iEdge = 0; iEdge < iEdgeCount; ++iEdge)
	{
		U32F adjId = GetAdjacentId(iEdge);
		if (adjId == polyId)
			return true;
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavPoly::IsAdjacentPoly(const NavPoly* other) const
{
	NAV_ASSERT(other);
	return IsAdjacentPoly(other->GetId());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPoly::DebugDrawEdges(const Color& colInterior,
							 const Color& colBorder,
							 float yOffset,
							 float lineWidth /* = 3.0f */,
							 DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	const NavMesh* pMesh = GetNavMesh();

	Vector offset = Vector(kUnitYAxis) * yOffset;
	for (U32F iEdge = 0; iEdge < GetVertexCount(); ++iEdge)
	{
		const Color col = (GetAdjacentId(iEdge) == NavPoly::kNoAdjacent) ? colBorder : colInterior;

		Point pt0 = pMesh->LocalToWorld(GetVertex(iEdge) + offset);
		Point pt1 = pMesh->LocalToWorld(GetNextVertex(iEdge) + offset);

		g_prim.Draw(DebugLine(pt0, pt1, col, lineWidth, kPrimEnableHiddenLineAlpha), tt);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPoly::DebugDraw(const Color& col,
						const Color& colBlocked /* = kColorWhiteTrans */,
						float yOffset /* = 0.0f */,
						DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	const NavMesh* pMesh = GetNavMesh();
	if (!pMesh)
		return;

	const Color c = IsBlocked() ? colBlocked : col;
	const Vector vo = pMesh->ParentToWorld(Vector(kUnitYAxis) * yOffset);

	const Point center = GetCentroid();

	// handle triangles
	const U32F vertCount = GetVertexCount();

	const Point v0 = GetVertex(0);
	const Point v1 = GetVertex(1);
	const Point v2 = GetVertex(2);
	const Point v3 = GetVertex(3);

	const Vector norm0 = AsUnitVectorXz(RotateY90(v1 - v0), kZero);
	const Vector norm1 = AsUnitVectorXz(RotateY90(v2 - v1), kZero);
	const Vector norm2 = AsUnitVectorXz(RotateY90(v3 - v2), kZero);
	const Vector norm3 = vertCount == 3 ? norm2 : AsUnitVectorXz(RotateY90(v0 - v3), kZero);

	const Vector c0 = SafeNormalize(norm0 + norm3, kZero);
	const Vector c1 = SafeNormalize(norm1 + norm0, kZero);
	const Vector c2 = SafeNormalize(norm2 + norm1, kZero);
	const Vector c3 = vertCount == 3 ? c0 : SafeNormalize(norm3 + norm2, kZero);

	const Point p0 = v0 + (c0 * 0.015f);
	const Point p1 = v1 + (c1 * 0.015f);
	const Point p2 = v2 + (c2 * 0.015f);
	const Point p3 = v3 + (c3 * 0.015f);

	const Point p0Ws = pMesh->LocalToWorld(p0) + vo;
	const Point p1Ws = pMesh->LocalToWorld(p1) + vo;
	const Point p2Ws = pMesh->LocalToWorld(p2) + vo;
	const Point p3Ws = pMesh->LocalToWorld(p3) + vo;

	//g_prim.Draw(DebugArrow(pMesh->LocalToWorld(v3), pMesh->LocalToWorld(c3) * 0.2f, col, 0.25f, kPrimEnableHiddenLineAlpha), tt);
	//g_prim.Draw(DebugCross(pMesh->LocalToWorld(p3), 0.05f, kColorOrange, kPrimEnableHiddenLineAlpha), tt);
	//g_prim.Draw(DebugArrow(pMesh->LocalToWorld(v3), pMesh->LocalToWorld(norm2) * 0.2f, kColorYellow, 0.25f, kPrimEnableHiddenLineAlpha), tt);
	//g_prim.Draw(DebugArrow(pMesh->LocalToWorld(v3), pMesh->LocalToWorld(norm3) * 0.2f, kColorYellow, 0.25f, kPrimEnableHiddenLineAlpha), tt);

	g_prim.Draw(DebugQuad(p0Ws, p1Ws, p2Ws, p3Ws, c, kPrimEnableHiddenLineAlpha), tt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPoly::DebugDrawGaps(const Color& ignoredColor, DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	const NavMesh* pMesh = GetNavMesh();

	for (const NavMeshGapRef* pGapRef = m_pGapList; (pGapRef && pGapRef->m_gapIndex >= 0); ++pGapRef)
	{
		const U32F index = pGapRef->m_pGap - &pMesh->GetGap(0);
		const Color col = AI::IndexToColor(index);
		const float yOffset = float(index) * 0.01f;

		pGapRef->m_pGap->DebugDraw(pMesh, col, yOffset);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPoly::DebugDrawExEdges(const Color& col,
							   const Color& colBoundary,
							   const Color& colBlocked,
							   float yOffset,
							   float lineWidth /* = 3.0f */,
							   DebugPrimTime tt /* = kPrimDuration1FrameAuto */,
							   const NavBlockerBits* pObeyedBlockers /* = nullptr */) const
{
	STRIP_IN_FINAL_BUILD;

	if (!m_pExPolyList)
		return;

	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	for (const NavPolyEx* pPolyEx = m_pExPolyList; pPolyEx; pPolyEx = pPolyEx->m_pNext)
	{
		const float exYOffset = g_navMeshDrawFilter.m_explodeExPolys ? ((float(pPolyEx->GetId() % 10) * 0.1f) + 0.1f) : yOffset + 0.01f;
		pPolyEx->DebugDrawEdges(col, colBoundary, colBlocked, exYOffset, lineWidth, tt, pObeyedBlockers);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPoly::DebugDrawExGaps(const Color& colObeyed,
							  const Color& colIgnored,
							  float yOffset,
							  DebugPrimTime tt /* = kPrimDuration1FrameAuto */,
							  const NavBlockerBits* pObeyedBlockers /* = nullptr */) const
{
	STRIP_IN_FINAL_BUILD;

	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const NavMesh* pMesh = GetNavMesh();

	NavBlockerBits obeyedBlockers;
	if (pObeyedBlockers)
		obeyedBlockers = *pObeyedBlockers;
	else
		obeyedBlockers.SetAllBits();

	NavMeshGapEx gaps[128];
	const size_t numGaps = g_navExData.GatherExGapsForPoly(GetNavManagerId(), gaps, 128);

	const Vector vo = Vector(kUnitYAxis) * yOffset;

	for (U32F iGap = 0; iGap < numGaps; ++iGap)
	{
		const NavMeshGapEx& gap = gaps[iGap];

		NavBlockerBits masked;
		NavBlockerBits::BitwiseAnd(&masked, gap.m_blockerBits, obeyedBlockers);

		const bool obeyed = !masked.AreAllBitsClear();

		const Color col = obeyed ? colObeyed : colIgnored;

		const Point p0 = pMesh->LocalToWorld(gap.m_pos0Ls) + vo;
		const Point p1 = pMesh->LocalToWorld(gap.m_pos1Ls) + vo;

		g_prim.Draw(DebugLine(p0, p1, col, 3.0f), tt);
		g_prim.Draw(DebugString(AveragePos(p0, p1), StringBuilder<256>("%0.1fm", gap.m_gapDist).c_str()), tt);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPoly::AddNavPolyExToList(NavPolyEx* pNewPolyEx) const
{
	if (!pNewPolyEx)
		return;

	if (m_pExPolyList)
	{
		pNewPolyEx->m_pNext = m_pExPolyList;
		m_pExPolyList = pNewPolyEx;
	}
	else
	{
		m_pExPolyList = pNewPolyEx;
	}

	// check for loops
	for (const NavPolyEx* pStart = m_pExPolyList ? m_pExPolyList->m_pNext : nullptr; pStart; pStart = pStart->m_pNext)
	{
		for (const NavPolyEx* pCur = pStart->m_pNext; pCur; pCur = pCur->m_pNext)
		{
			NAV_ASSERT(pCur != pStart);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavPolyEx* NavPoly::FindContainingPolyExLs(Point_arg pos) const
{
	PROFILE_ACCUM(FindContainingPolyExLs);

	const NavPolyEx* pRet = nullptr;

	const NavPolyEx* pNextBest = nullptr;
	float bestDot = -kLargeFloat;

	for (const NavPolyEx* pPolyEx = m_pExPolyList; pPolyEx; pPolyEx = pPolyEx->m_pNext)
	{
		Vec4 vDots;
		if (pPolyEx->PolyContainsPointLs(pos, &vDots))
		{
			pRet = pPolyEx;
			break;
		}
		else
		{
			const float worstDot = MinComp4(vDots);
			if (worstDot > bestDot)
			{
				pNextBest = pPolyEx;
				bestDot = worstDot;
			}
		}
	}

	if (!pRet)
	{
		pRet = pNextBest;
	}

	return pRet;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavPolyEx* NavPoly::FindContainingPolyExPs(Point_arg pos) const
{
	const NavMesh* pNavMesh = GetNavMesh();
	if (!pNavMesh)
		return nullptr;
	const Point posLs = pNavMesh->ParentToLocal(pos);
	return FindContainingPolyExLs(posLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// NB: averaging the verts computes the centroid for triangles. It DOES NOT correctly compute
// the centroid for quads! For quads you must compute the pair of centroids of the two triangles obtained by subdividing
// the quad one way, then the other; then draw lines connecting each pair; then return the intersection of the two lines.
Point NavPoly::GetCentroid() const
{
	Point center;
	if (m_vertCount == 3)
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
		NAV_ASSERT(Abs(denom) > 1e-10);

		const float t = (d1343 * d4321 - d1321 * d4343) / denom;

		center = p1 + t * p21;
	}
	return center;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point NavPoly::GetBBoxMinLs() const
{
	// note for triangles, v(3) is a duplicate of v(0) so this works for tris and quads (and we avoid a branch)
	const Point v0 = GetVertex(0);
	const Point v1 = GetVertex(1);
	const Point v2 = GetVertex(2);
	const Point v3 = GetVertex(3);

	const Point boxMinLs = Min(Min(v0, v1), Min(v2, v3));

	return boxMinLs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point NavPoly::GetBBoxMaxLs() const
{
	// note for triangles, v(3) is a duplicate of v(0) so this works for tris and quads (and we avoid a branch)
	const Point v0 = GetVertex(0);
	const Point v1 = GetVertex(1);
	const Point v2 = GetVertex(2);
	const Point v3 = GetVertex(3);

	const Point boxMaxLs = Max(Max(v0, v1), Max(v2, v3));

	return boxMaxLs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point NavPoly::GetBBoxMinPs() const
{
	const NavMesh* pMesh = GetNavMesh();
	const Point boxMinLs = GetBBoxMinLs();
	const Point boxMinPs = pMesh ? pMesh->LocalToParent(boxMinLs) : boxMinLs;

	return boxMinPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point NavPoly::GetBBoxMaxPs() const
{
	const NavMesh* pMesh = GetNavMesh();
	const Point boxMaxLs = GetBBoxMaxLs();
	const Point boxMaxPs = pMesh ? pMesh->LocalToParent(boxMaxLs) : boxMaxLs;

	return boxMaxPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point NavPoly::GetBBoxMinWs() const
{
	const NavMesh* pMesh = GetNavMesh();
	const Point boxMinLs = GetBBoxMinLs();
	const Point boxMinWs = pMesh ? pMesh->LocalToWorld(boxMinLs) : boxMinLs;

	return boxMinWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point NavPoly::GetBBoxMaxWs() const
{
	const NavMesh* pMesh = GetNavMesh();
	const Point boxMaxLs = GetBBoxMaxLs();
	const Point boxMaxWs = pMesh ? pMesh->LocalToWorld(boxMaxLs) : boxMaxLs;

	return boxMaxWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NavPoly::ComputeSignedAreaXz() const
{
	const __m128 A = _mm_sub_ps(_mm_loadu_ps((float*)&m_vertsLs[2]), _mm_loadu_ps((float*)&m_vertsLs[0]));
	const __m128 B = _mm_sub_ps(_mm_loadu_ps((float*)&m_vertsLs[3]), _mm_loadu_ps((float*)&m_vertsLs[1]));
	__m128 R = _mm_mul_ps(B, _mm_shuffle_ps(A, A, 198));
	R = _mm_sub_ss(R, _mm_shuffle_ps(R, R, 198));
	return 0.5f * _mm_cvtss_f32(R);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavPoly* NavPoly::GetLinkedPoly(const NavMesh** ppDestMeshOut /* = nullptr */) const
{
	const NavMesh* pNavMesh = GetNavMesh();

	NAV_ASSERT(pNavMesh);

	if (!pNavMesh)
		return nullptr;

	if (GetLinkId() == kInvalidNavPolyId)
		return nullptr;

	const NavMeshLink& link = pNavMesh->GetLink(GetLinkId());
	const NavMeshHandle hDestMesh = link.GetDestNavMesh();

	const NavMesh* pDestNavMesh = hDestMesh.ToNavMesh();

	if (!pDestNavMesh)
		return nullptr;

	const NavPoly* pLinkedPoly = nullptr;

	if (IsLink())
	{
		const U32 destPreLinkPolyId = link.GetDestPreLinkPolyId();

		pLinkedPoly = &pDestNavMesh->GetPoly(destPreLinkPolyId);
	}
	else if (IsPreLink())
	{
		const U32 destlinkPolyId = link.GetDestLinkPolyId();

		pLinkedPoly = &pDestNavMesh->GetPoly(destlinkPolyId);
	}
	else
	{
		NAV_ASSERT(false);
	}

	if (ppDestMeshOut)
		*ppDestMeshOut = pDestNavMesh;

	return pLinkedPoly;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavPolyId NavPoly::GetLinkedPolyId() const
{
	const NavMesh* pNavMesh = GetNavMesh();

	NAV_ASSERT(pNavMesh);

	if (!pNavMesh)
		return kInvalidNavPolyId;

	const NavMeshLink& link = pNavMesh->GetLink(GetLinkId());
	const NavMeshHandle hDestMesh = link.GetDestNavMesh();

	const NavMesh* pDestNavMesh = hDestMesh.ToNavMesh();

	if (!pDestNavMesh)
		return kInvalidNavPolyId;

	NavPolyId linkedPolyId = kInvalidNavPolyId;

	if (IsLink())
	{
		linkedPolyId = link.GetDestPreLinkPolyId();
	}
	else if (IsPreLink())
	{
		linkedPolyId = link.GetDestLinkPolyId();
	}
	else
	{
		NAV_ASSERT(false);
	}

	return linkedPolyId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static inline __m128 PolyContainsPointCommon(const NavPoly* const __restrict pPoly, const Point ptLs)
{
	const float* __restrict const v = pPoly->GetVertexArray();
	const __m128 V0 = _mm_loadu_ps(v + 0);
	const __m128 V1 = _mm_loadu_ps(v + 3);
	const __m128 V2 = _mm_loadu_ps(v + 6);
	const __m128 V3 = _mm_loadu_ps(v + 9);
	const __m128 EV01 = _mm_shuffle_ps(_mm_sub_ps(V1, V0), _mm_sub_ps(V2, V1), 136);
	const __m128 EV23 = _mm_shuffle_ps(_mm_sub_ps(V3, V2), _mm_sub_ps(V0, V3), 136);
	const __m128 PV01s = _mm_shuffle_ps(_mm_sub_ps(V0, ptLs.QuadwordValue()), _mm_sub_ps(V1, ptLs.QuadwordValue()), 34);
	const __m128 PV23s = _mm_shuffle_ps(_mm_sub_ps(V2, ptLs.QuadwordValue()), _mm_sub_ps(V3, ptLs.QuadwordValue()), 34);
	const __m128 M01 = _mm_mul_ps(EV01, PV01s);
	const __m128 M23 = _mm_mul_ps(EV23, PV23s);
	const __m128 R = _mm_hsub_ps(M01, M23);
	return R;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavPoly::PolyContainsPointLsNoEpsilon(Point_arg ptLs) const
{
	const __m128 R = PolyContainsPointCommon(this, ptLs);
	return _mm_testz_ps(R, R);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavPoly::PolyContainsPointLs(Point_arg ptLs, float epsilon /* = NDI_FLT_EPSILON */) const
{
	const __m128 R = _mm_add_ps(PolyContainsPointCommon(this, ptLs), _mm_set1_ps(epsilon));
	return _mm_testz_ps(R, R);
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <bool kSquared>
static inline float SignedDistPointPolyXzCommon(const NavPoly* const __restrict pPoly, const Point ptLs)
{
	const float* __restrict const v = pPoly->GetVertexArray();
	const __m128 V0 = _mm_loadu_ps(v + 0);
	const __m128 V1 = _mm_loadu_ps(v + 3);
	const __m128 V2 = _mm_loadu_ps(v + 6);
	const __m128 V3 = _mm_loadu_ps(v + 9);
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
float NavPoly::SignedDistPointPolyXzSqr(Point_arg ptLs) const
{
	return SignedDistPointPolyXzCommon<true>(this, ptLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NavPoly::SignedDistPointPolyXz(Point_arg ptLs) const
{
	return SignedDistPointPolyXzCommon<false>(this, ptLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPoly::ProjectPointOntoSurfaceLs(Point* pOutLs, Vector* pNormalLs, Point_arg inPtLs) const
{
	Point verts[4];
	verts[0] = GetVertexLs(0);
	verts[1] = GetVertexLs(1);
	verts[2] = GetVertexLs(2);
	verts[3] = GetVertexLs(3);

	const U32F numVerts = GetVertexCount();

	NavPolyUtil::GetPointAndNormal(inPtLs, verts, numVerts, pOutLs, pNormalLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPoly::ProjectPointOntoSurfacePs(Point* pOutPs, Vector* pNormalPs, Point_arg inPtPs) const
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
const Scalar NavPoly::FindNearestPointXzLs(Point* pOutLs, Point_arg inPtLs) const
{
	Point verts[4];
	verts[0] = GetVertexLs(0);
	verts[1] = GetVertexLs(1);
	verts[2] = GetVertexLs(2);
	verts[3] = GetVertexLs(3);

	const U32F numVerts = GetVertexCount();

	const float bestDist = NavPolyUtil::FindNearestPointXz(inPtLs, verts, numVerts, pOutLs);

	//ASSERT(PolyContainsPointLs(*pOutLs, 0.001f));

	return bestDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Scalar NavPoly::FindNearestPointLs(Point* pOutLs, Point_arg inPtLs) const
{
	Point verts[4];
	verts[0] = GetVertexLs(0);
	verts[1] = GetVertexLs(1);
	verts[2] = GetVertexLs(2);
	verts[3] = GetVertexLs(3);

	const U32F numVerts = GetVertexCount();

	const float bestDist = NavPolyUtil::FindNearestPoint(inPtLs, verts, numVerts, pOutLs);

	//ASSERT(PolyContainsPointLs(*pOutLs, 0.001f));

	return bestDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Scalar NavPoly::FindNearestPointPs(Point* pOutPs, Point_arg inPtPs) const
{
	const Vector localToParent = GetNavMesh()->LocalToParent(Point(kZero)) - kOrigin;

	Point outLs;
	Scalar dist = FindNearestPointLs(&outLs, inPtPs - localToParent);
	*pOutPs = outLs + localToParent;
	return dist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavPoly::HasLegalShape() const
{
	return ComputeSignedAreaXz() >= 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const EntityDB* NavPoly::GetEntityDB() const
{
	const NavMesh* pMesh = GetNavMesh();
	if (!pMesh)
		return nullptr;

	return pMesh->GetEntityDBByIndex(m_iEntityDB);
}
