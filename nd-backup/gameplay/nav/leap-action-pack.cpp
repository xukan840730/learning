/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/leap-action-pack.h"

#include "corelib/math/matrix3x4.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/render/render-camera.h"

#include "gamelib/feature/feature-db-debug.h"
#include "gamelib/feature/feature-db-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/level/level.h"
#include "gamelib/level/level-mgr.h"

/// --------------------------------------------------------------------------------------------------------------- ///
const float kMinEdgeWidth = 0.4f;
const float kMaxLeapDownHeight = 2.5f;
const float kMaxLeapUpHeight = 1.25f;
const float kRegistrationOffsetDist = 0.45f;
static const FeatureEdge::Flags kRequiredFlags = FeatureEdge::kFlagVault;

/// --------------------------------------------------------------------------------------------------------------- ///
static void DebugDrawEdge(Point_arg v0Ws,
						  Point_arg v1Ws,
						  Color clr,
						  float width,
						  DebugPrimTime tt = kPrimDuration1FrameAuto)
{
	STRIP_IN_FINAL_BUILD;

	g_prim.Draw(DebugLine(v0Ws, v1Ws, clr, width, kPrimEnableHiddenLineAlpha), tt);
	g_prim.Draw(DebugCross(v0Ws, 0.1f, clr, kPrimEnableHiddenLineAlpha), tt);
	g_prim.Draw(DebugCross(v1Ws, 0.1f, clr, kPrimEnableHiddenLineAlpha), tt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool GetExtendedEdge(const FeatureEdge* pEdge, Point* pV0WsOut, Point* pV1WsOut)
{
	const Vector baseTopNormalWs = pEdge->GetTopNormal();
	const Vector baseWallNormalWs = pEdge->GetWallNormal();
	FeatureEdge::Flags baseFlags = pEdge->GetFlags();

	Point v0 = pEdge->GetVert0();
	Point v1 = pEdge->GetVert1();

	bool isLowestId = true;

	for (const FeatureEdge* pLink0 = pEdge->GetLink(0); pLink0; pLink0 = pLink0->GetLink(0))
	{
		if (pLink0 == pEdge)
			break;

		if (Dot(pLink0->GetWallNormal(), baseWallNormalWs) < 0.99f)
			break;

		if (Dot(pLink0->GetTopNormal(), baseTopNormalWs) < 0.99f)
			break;

		if (0 == (pLink0->GetFlags() & kRequiredFlags))
			break;

		isLowestId &= pLink0->GetId() > pEdge->GetId();

		v0 = pLink0->GetVert0();
	}

	for (const FeatureEdge* pLink1 = pEdge->GetLink(1); pLink1; pLink1 = pLink1->GetLink(1))
	{
		if (pLink1 == pEdge)
			break;

		if (Dot(pLink1->GetWallNormal(), baseWallNormalWs) < 0.99f)
			break;

		if (Dot(pLink1->GetTopNormal(), baseTopNormalWs) < 0.99f)
			break;

		if (0 == (pLink1->GetFlags() & kRequiredFlags))
			break;

		isLowestId &= pLink1->GetId() > pEdge->GetId();

		v1 = pLink1->GetVert1();
	}

	if (pV0WsOut)
		*pV0WsOut = v0;
	if (pV1WsOut)
		*pV1WsOut = v1;

	return isLowestId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const NavPoly* FindPolyToRegisterTo(Point_arg posWs, const Level* pLevel, Point* pPolyPosWsOut, bool flip = false)
{
	if (!pLevel)
		return nullptr;

	const NavMeshArray* pNavMeshes = pLevel->GetNavMeshArray();

	if (!pNavMeshes)
		return nullptr;

	const size_t numNavMeshes = pNavMeshes->Size();
	const NavPoly* pBestPoly = nullptr;
	Point bestPosWs = posWs;

	for (U32F iNavMesh = 0; iNavMesh < numNavMeshes; ++iNavMesh)
	{
		const NavMesh* pNavMesh = pNavMeshes->At(iNavMesh);

		if (!pNavMesh || !pNavMesh->IsVersionCorrect())
			continue;

		if (!pNavMesh->IsRegistered())
			continue;

		if (pNavMesh->GetTagData<bool>(SID("no-leap-aps"), false))
			continue;

		const Point posLs = pNavMesh->WorldToLocal(posWs);
		const NavPoly* pPoly = pNavMesh->FindContainingPolyLs(posLs, 10.0f, Nav::kStaticBlockageMaskNone);

		if (!pPoly)
			continue;

		NAV_ASSERT(pPoly->GetNavMesh() == pNavMesh);

		Point closestPosLs = posLs;
		pPoly->FindNearestPointLs(&closestPosLs, posLs);

		const Point posPs = pNavMesh->WorldToParent(posWs);
		const Point bestPosPs = pNavMesh->WorldToParent(bestPosWs);
		const Point closestPosPs = pNavMesh->LocalToParent(closestPosLs);

		if (flip)
		{
			if (closestPosPs.Y() < posPs.Y() - 0.2f)
				continue;
		}
		else if (closestPosPs.Y() >= posPs.Y())
		{
			continue;
		}

		if (!pBestPoly || (closestPosPs.Y() > bestPosPs.Y()))
		{
			pBestPoly = pPoly;
			bestPosWs = pNavMesh->LocalToWorld(closestPosLs);
		}
	}

	if (pPolyPosWsOut)
	{
		*pPolyPosWsOut = bestPosWs;
	}

	return pBestPoly;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
size_t LeapActionPack::GenerateLeapApsFromEdge(const Level* pLevel,
											   const FeatureEdge& edge,
											   LeapApDefinition* pDefsOut,
											   size_t maxDefsOut)
{
	// Currently These APs are Unused.
	return 0;

	bool debugDraw = false;
	const DebugPrimTime drawTime = Seconds(10.0f);

	if (!pDefsOut || (0 == maxDefsOut))
		return 0;

	if (0 == (edge.GetFlags() & kRequiredFlags))
		return 0;

	Point v0Ws = edge.GetVert0();
	Point v1Ws = edge.GetVert1();

	if (!GetExtendedEdge(&edge, &v0Ws, &v1Ws))
		return 0;

	const float edgeLength = Dist(v0Ws, v1Ws);

	if (edgeLength < kMinEdgeWidth)
		return 0;

	const Vector wallNormalWs = edge.GetWallNormal();
	const Vector topNormalWs = edge.GetTopNormal();
	const Vector wallNormalXzWs = SafeNormalize(VectorXz(wallNormalWs), kZero);

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		DebugDrawEdge(v0Ws, v1Ws, kColorGreen, 4.0f, drawTime);
	}

	const size_t numSamples = size_t(((edgeLength - kMinEdgeWidth) / 1.25f) + 1);

	size_t numGenerated = 0;
	const float invTT = 1.0f / float(numSamples + 1);

	for (int i = 0; i < numSamples; i++)
	{
		if (numGenerated >= maxDefsOut)
			break;

		//const float tt = float(i + 1) * invTT;
		const float tt = float(i + 1) * invTT;
		const Point samplePosWs = Lerp(v0Ws, v1Ws, tt);
		const Point searchPosWs = samplePosWs + (wallNormalXzWs * kRegistrationOffsetDist);

		Point polyPosWs = searchPosWs;
		const NavPoly* pRegPoly = FindPolyToRegisterTo(searchPosWs, pLevel, &polyPosWs);

		if (!pRegPoly)
			continue;

		const F32 yDelta = samplePosWs.Y() - polyPosWs.Y();
		if (yDelta > kMaxLeapUpHeight) // We aren't using the leap down attacks.
			continue;

		// Leap down APs go the other direction.
		const bool flip = yDelta > kMaxLeapUpHeight;

		const Point flippedSearchPos = samplePosWs + (wallNormalXzWs * -kRegistrationOffsetDist);
		const NavPoly* pFlippedPoly = flip ? FindPolyToRegisterTo(flippedSearchPos, pLevel, &polyPosWs, flip) : nullptr;

		const Quat edgeRotWs = QuatFromLookAt(flip ? -wallNormalWs : wallNormalWs, topNormalWs);

		const BoundFrame apLoc = BoundFrame(samplePosWs, edgeRotWs);

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			g_prim.Draw(DebugCross(searchPosWs, 0.1f, kColorRed, kPrimEnableHiddenLineAlpha), drawTime);
			g_prim.Draw(DebugArrow(searchPosWs, polyPosWs, kColorRedTrans, kPrimEnableHiddenLineAlpha), drawTime);
			g_prim.Draw(DebugLine(searchPosWs, samplePosWs, kColorRedTrans, 3.0f, kPrimEnableHiddenLineAlpha), drawTime);
			g_prim.Draw(DebugCoordAxes(apLoc.GetLocatorWs(), 0.1f), drawTime);
		}

		pDefsOut[numGenerated].m_loc = apLoc;
		pDefsOut[numGenerated].m_regPtLs = apLoc.GetLocatorWs().UntransformPoint(polyPosWs);
		pDefsOut[numGenerated].m_hRegPoly = flip ? pFlippedPoly : pRegPoly;
		pDefsOut[numGenerated].m_featureFlags = edge.GetFlags();

		++numGenerated;
	}

	if (FALSE_IN_FINAL_BUILD(debugDraw && numGenerated))
	{
		g_prim.Draw(DebugString(AveragePos(v0Ws, v1Ws), StringBuilder<256>("%d", numGenerated).c_str(), kColorRed, 0.5f));
	}

	return numGenerated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
LeapActionPack::LeapActionPack(const Level* pLevel, const LeapApDefinition& def)
	: ActionPack(ActionPack::kLeapActionPack, def.m_regPtLs, def.m_loc, pLevel)
	, m_def(def)
{}

/// --------------------------------------------------------------------------------------------------------------- ///
LeapActionPack::LeapActionPack(const Level* pAllocLevel, const Level* pRegLevel, const LeapApDefinition& def)
	: ActionPack(ActionPack::kLeapActionPack, def.m_regPtLs, def.m_loc, pAllocLevel, pRegLevel)
	, m_def(def)
{}

/// --------------------------------------------------------------------------------------------------------------- ///
void LeapActionPack::DebugDraw(DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	const Locator locWs = GetLocatorWs();
	const Point ledgePosWs = locWs.Pos();
	const Point regPosWs = GetRegistrationPointWs();

	g_prim.Draw(DebugCross(regPosWs, 0.05f, kColorRed), tt);

	g_prim.Draw(DebugArrow(regPosWs, ledgePosWs, kColorRedTrans), tt);

	g_prim.Draw(DebugCoordAxes(GetLocatorWs()), tt);

	if (g_navMeshDrawFilter.m_drawApDetail)
	{
		DebugDrawEdge(locWs.TransformPoint(Point(kMinEdgeWidth * -0.5f, 0.0f, 0.0f)),
					  locWs.TransformPoint(Point(kMinEdgeWidth * 0.5f, 0.0f, 0.0f)),
					  kColorGreen,
					  4.0f,
					  tt);

		const float yDiff = ledgePosWs.Y() - regPosWs.Y();
		g_prim.Draw(DebugString(AveragePos(ledgePosWs, regPosWs),
								StringBuilder<256>("%0.2fm", yDiff).c_str(),
								kColorRedTrans,
								0.5f),
					tt);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool LeapActionPack::RegisterInternal()
{
	PROFILE(AI, ActionPack_RegInternal);

	const bool readOnly = m_regParams.m_readOnly;

	NAV_ASSERT(readOnly ? NavMeshMgr::GetGlobalLock()->IsLocked() : NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	bool registered = false;

	const Point regPosWs = GetRegistrationPointWs();
	m_hRegisteredNavLoc.SetWs(regPosWs);
	m_pNavRegistrationListNext = nullptr;

	switch (m_regLocType)
	{
	case NavLocation::Type::kNavPoly:
		{
			if (const NavPoly* pNavPoly = m_def.m_hRegPoly.ToNavPoly())
			{
				if (readOnly)
				{
					m_hRegisteredNavLoc.SetWs(regPosWs, pNavPoly);
				}
				else
				{
					RegisterSelfToNavPoly(pNavPoly);
				}

				registered = true;
			}
		}
		break;
#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
#endif // ENABLE_NAV_LEDGES
	default:
		registered = false;
		break;
	}

	return registered;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LeapActionPack::UnregisterInternal()
{
	const bool readOnly = m_regParams.m_readOnly;

	NAV_ASSERT(readOnly ? NavMeshMgr::GetGlobalLock()->IsLocked() : NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	switch (m_regLocType)
	{
	case NavLocation::Type::kNavPoly:
		if (readOnly)
		{
			m_pNavRegistrationListNext = nullptr;
		}
		else
		{
			if (NavPoly* pNavPoly = const_cast<NavPoly*>(m_hRegisteredNavLoc.ToNavPoly()))
			{
				pNavPoly->UnregisterActionPack(this);
			}
			else
			{
				m_pNavRegistrationListNext = nullptr;
			}
		}
		break;

#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
		break;
#endif // ENABLE_NAV_LEDGES
	}

	m_hRegisteredNavLoc.SetWs(GetRegistrationPointWs());
	NAV_ASSERT(m_pNavRegistrationListNext == nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point LeapActionPack::GetDefaultEntryPointWs(Scalar_arg offset) const
{
	// for traversal action packs, the entry point is the same as the registration point
	const Locator locWs = m_loc.GetLocator();
	Point regPtLs = m_regPtLs;

	return locWs.TransformPoint(regPtLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point LeapActionPack::GetDefaultEntryPointPs(Scalar_arg offset) const
{
	// for traversal action packs, the entry point is the same as the registration point
	const Locator locPs = m_loc.GetLocatorPs();
	Point regPtLs = m_regPtLs;

	return locPs.TransformPoint(regPtLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame LeapActionPack::GetDefaultEntryBoundFrame(Scalar_arg offset) const
{
	// for traversal action packs, the entry point is the same as the registration point
	BoundFrame apRef = GetBoundFrame();
	Vector regVtLs = GetRegistrationPointLs() - kOrigin;

	Vector regVtWs = apRef.GetLocatorWs().TransformVector(regVtLs);
	apRef.AdjustTranslationWs(regVtWs);

	return apRef;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GenerateLeapApsFromNavMesh(const NavMesh* pNavMesh)
{
	if (!pNavMesh)
		return;

	if (pNavMesh->GetTagData<bool>(SID("no-leap-aps"), false))
		return;

	const Level* pLevel = EngineComponents::GetLevelMgr()->GetLevel(pNavMesh->GetLevelId());
	if (!pLevel)
		return;

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);
	ListArray<const FeatureEdge*> edges;
	edges.Init(2048, FILE_LINE_FUNC);

	Aabb bboxWs = pNavMesh->GetAabbWs();

	bboxWs.Expand(Vector(1.0f, 2.0f, 1.0f));

	g_prim.Draw(DebugBox(bboxWs.m_min, bboxWs.m_max, kColorRed, PrimAttrib(kPrimEnableHiddenLineAlpha, kPrimEnableWireframe)));

	for (U32F i = 0; i < g_featureDbMgr.GetCount(); ++i)
	{
		FeatureDb* pFeatureDb = g_featureDbMgr.GetFeatureDb(i);
		if (pFeatureDb)
		{
			Locator tm = Locator(kOrigin + g_featureDbMgr.GetLevelOffset(i));
			pFeatureDb->FindEdges(&edges, bboxWs, tm, kRequiredFlags);
		}
	}

	const U32 numEdges = edges.size();

	Matrix3x4 tm = Matrix3x4(kIdentity);

	for (U32F iEdge = 0; iEdge < numEdges; ++iEdge)
	{
		const FeatureEdge* pEdge = edges.At(iEdge);

		if (!pEdge)
			continue;

		LeapApDefinition defs[100];
		LeapActionPack::GenerateLeapApsFromEdge(pLevel, *pEdge, defs, 100);
	}
}
