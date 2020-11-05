/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/background-ladder.h"

#include "ndlib/process/debug-selection.h"

#include "gamelib/feature/feature-db-ref.h"
#include "gamelib/feature/feature-db.h"
#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/character-ladder.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nd-action-pack-util.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"
#include "gamelib/region/region-manager.h"

/// --------------------------------------------------------------------------------------------------------------- ///
PROCESS_REGISTER(BackgroundLadder, NdLocatableObject);

FROM_PROCESS_DEFINE(BackgroundLadder);

BgLadderManager g_bgLadderManager;

/// --------------------------------------------------------------------------------------------------------------- ///
BackgroundLadder::~BackgroundLadder()
{
	UnregisterTaps();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err BackgroundLadder::Init(const ProcessSpawnInfo& spawn)
{
	const Err parentRes = ParentClass::Init(spawn);
	if (parentRes.Failed())
	{
		return parentRes;
	}

	GatherEdges();

	GatherRegionInfo();

	if (DetermineNavLocs())
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		CreateLadderTaps();
	}

	g_bgLadderManager.Register(this);

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BackgroundLadder::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);

	m_edges.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BackgroundLadder::OnKillProcess()
{
	ParentClass::OnKillProcess();

	UnregisterTaps();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BackgroundLadder::DebugDraw(DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	if (m_edges.IsEmpty())
	{
		StringBuilder<256> desc;
		desc.format("[%s] Failed to find any ladder edges (check orientation)", GetName());
		g_prim.Draw(DebugCoordAxes(GetLocator(), 0.5f, kPrimEnableHiddenLineAlpha, 4.0f));
		g_prim.Draw(DebugString(GetTranslation(),
								desc.c_str(),
								kColorRed,
								0.5f));
		return;
	}

	for (const EdgeInfo& edge : m_edges)
	{
		if ((edge.GetFlags() & FeatureEdge::kFlagLadder) == 0)
		{
			g_prim.Draw(DebugLine(edge.GetVert0(), edge.GetVert1(), kColorGreen, 4.0f, kPrimEnableHiddenLineAlpha), tt);
			continue;
		}

		const EdgeInfo centered = GetLadderEdgeCenter(edge);
		const Point centerWs = centered.GetEdgeCenter();
		const Quat rotWs = QuatFromLookAt(centered.GetWallNormal(), centered.GetTopNormal());
		const Locator locWs = Locator(centerWs, rotWs);

		const I32F iEdge = m_edges.IndexOf(&edge);
		StringBuilder<256> desc;
		desc.format("%d", iEdge);

		if (iEdge == 0)
		{
			desc.append_format(" [%s]", GetName());
		}

		g_prim.Draw(DebugString(centerWs, desc.c_str(), kColorWhite, 0.6f), tt);
		g_prim.Draw(DebugCoordAxes(locWs, 0.5f, kPrimEnableHiddenLineAlpha, 3.0f), tt);
	}

	if (false)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		m_topNavLoc.DebugDraw(kColorCyan, tt);
		m_bottomNavLoc.DebugDraw(kColorMagenta, tt);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BackgroundLadder::UnregisterTaps()
{
	if (ActionPack* pUpTap = m_hUpTap.ToActionPack())
	{
		pUpTap->RequestUnregistration();
	}

	if (ActionPack* pDownTap = m_hDownTap.ToActionPack())
	{
		pDownTap->RequestUnregistration();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool BackgroundLadder::GatherEdges()
{
	ScopedTempAllocator scopedTemp(false, FILE_LINE_FUNC);
	HeapAllocator* pTempAllocator = scopedTemp.GetAllocatorInstance();

	const size_t maxWorkingEdges = 256;
	ListArray<EdgeInfo> workingEdges;
	ListArray<EdgeInfo> foundEdges;

	const Locator locWs = GetLocator();
	const Point posWs = locWs.Pos();
	const Vector desiredFwWs = GetLocalZ(locWs);

	const Sphere startingSphereWs = Sphere(posWs, 5.0f);

	{
		AllocateJanitor jj(pTempAllocator, FILE_LINE_FUNC);
		workingEdges.Init(maxWorkingEdges, FILE_LINE_FUNC);
		foundEdges.Init(maxWorkingEdges, FILE_LINE_FUNC);
	}

	FindEdgesWithProbeDb(startingSphereWs, &workingEdges, FeatureEdge::kFlagLadder);

	const I32F numInitialEdges = workingEdges.Size();

	EdgeInfo startingEdge;
	float bestDist = kLargeFloat;

	for (const EdgeInfo& edge : workingEdges)
	{
		const Point centerWs = edge.GetEdgeCenter();

		if (Dot(edge.GetWallNormal(), desiredFwWs) < 0.0f)
			continue;

		const float dist = Dist(centerWs, posWs);

		if (dist < bestDist)
		{
			bestDist = dist;
			startingEdge = edge;
		}
	}

	if (!startingEdge)
	{
		return false;
	}

	foundEdges.PushBack(startingEdge);

	Sphere gatherSphereWs = startingSphereWs;

	EdgeInfo currentEdge = startingEdge;

	bool recentered = false;

	while (!foundEdges.IsFull())
	{
		currentEdge = GetNextLadderEdge(workingEdges, currentEdge, 1);

		if (!currentEdge.GetSrcEdge())
		{
			break;
		}

		foundEdges.PushBack(currentEdge);

		const float gatherDist = Dist(currentEdge.GetEdgeCenter(), gatherSphereWs.GetCenter());

		if (gatherDist > 4.0f)
		{
			gatherSphereWs.SetCenter(currentEdge.GetEdgeCenter());

			FindEdgesWithProbeDb(gatherSphereWs, &workingEdges, FeatureEdge::kFlagLadder);
			recentered = true;
		}
	}

	if (recentered)
	{
		gatherSphereWs = startingSphereWs;
		FindEdgesWithProbeDb(gatherSphereWs, &workingEdges, FeatureEdge::kFlagLadder);
	}

	currentEdge = startingEdge;

	while (!foundEdges.IsFull())
	{
		currentEdge = GetNextLadderEdge(workingEdges, currentEdge, -1);

		if (!currentEdge.GetSrcEdge())
		{
			break;
		}

		foundEdges.PushFront(currentEdge);

		const float gatherDist = Dist(currentEdge.GetEdgeCenter(), gatherSphereWs.GetCenter());

		if (gatherDist > 4.0f)
		{
			gatherSphereWs.SetCenter(currentEdge.GetEdgeCenter());

			FindEdgesWithProbeDb(gatherSphereWs, &workingEdges, FeatureEdge::kFlagLadder);
			recentered = true;
		}
	}

	m_numLadderEdges = foundEdges.Size();

	{
		const Point topPosWs = foundEdges.Back().GetEdgeCenter();

		gatherSphereWs.SetCenter(topPosWs);
		gatherSphereWs.SetRadius(1.0f);

		FindEdgesWithProbeDb(gatherSphereWs, &workingEdges, FeatureEdge::kFlagCanClimbUp);

		for (const EdgeInfo& edge : workingEdges)
		{
			if (foundEdges.IsFull())
				break;

			foundEdges.PushBack(edge);
		}
	}

	m_edges.Init(foundEdges.Size(), FILE_LINE_FUNC);

	for (const EdgeInfo& edge : foundEdges)
	{
		m_edges.PushBack(edge);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool BackgroundLadder::DetermineNavLocs()
{
	m_topNavLoc	   = NavLocation();
	m_bottomNavLoc = NavLocation();

	if (m_edges.IsEmpty())
		return false;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();

	const EdgeInfo& topEdge = m_edges[m_numLadderEdges - 1];
	const EdgeInfo& bottomEdge = m_edges.Front();

	FindBestNavMeshParams params;
	params.m_pointWs	= topEdge.GetEdgeCenter() - (AsUnitVectorXz(topEdge.GetWallNormal(), kUnitZAxis) * 0.35f);
	params.m_cullDist	= 0.25f;
	params.m_yThreshold = 2.0f;
	params.m_bindSpawnerNameId		  = GetBindSpawnerId();
	params.m_obeyedStaticBlockers = 0;

	nmMgr.FindNavMeshWs(&params);

	if (params.m_pNavPoly)
	{
		m_topNavLoc.SetWs(params.m_nearestPointWs, params.m_pNavPoly);
	}

	params.m_pointWs = bottomEdge.GetEdgeCenter() + (AsUnitVectorXz(bottomEdge.GetWallNormal(), kUnitZAxis) * 0.35f);

	nmMgr.FindNavMeshWs(&params);

	if (params.m_pNavPoly)
	{
		m_bottomNavLoc.SetWs(params.m_nearestPointWs, params.m_pNavPoly);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BackgroundLadder::CreateLadderTaps()
{
	const EntitySpawner* pSpawner = GetSpawner();
	LoginTaps* pLoginTaps = pSpawner ? (LoginTaps*)pSpawner->GetLoginData() : nullptr;

	TraversalActionPack* pUpTap = pLoginTaps ? pLoginTaps->m_pUpTap : nullptr;
	TraversalActionPack* pDownTap = pLoginTaps ? pLoginTaps->m_pDownTap : nullptr;

	if (!pUpTap && !pDownTap)
		return;

	const DC::VarTapTable* pVarTapTable = GetVarTapTable(SID("auto-ladder"));
	if (!pVarTapTable)
		return;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const NavPoly* pBottomPoly = m_bottomNavLoc.ToNavPoly();
	const NavPoly* pTopPoly	   = m_topNavLoc.ToNavPoly();

	if (!pBottomPoly || !pTopPoly)
		return;

	const bool upOnly	= pSpawner->GetData(SID("up-only"), false);
	const bool downOnly = pSpawner->GetData(SID("down-only"), false);

	if (upOnly && downOnly)
		return; // wtf?

	const Point bottomNavPosWs = m_bottomNavLoc.GetPosWs();
	const Point topNavPosWs	   = m_topNavLoc.GetPosWs();

	const EdgeInfo& bottomEdge = ClosestHeightLadderEdgeWs(bottomNavPosWs);
	const EdgeInfo& topEdge	   = ClosestHeightLadderEdgeWs(topNavPosWs);

	m_iTapBottomEdge = m_edges.IndexOf(&bottomEdge);
	m_iTapTopEdge	 = m_edges.IndexOf(&topEdge);

	const Point topPosWs	= PointFromXzAndY(topEdge.GetEdgeCenter(), topNavPosWs);
	const Point bottomPosWs = PointFromXzAndY(bottomEdge.GetEdgeCenter(), bottomNavPosWs);

	NAV_ASSERT(IsReasonable(topPosWs));
	NAV_ASSERT(IsReasonable(bottomPosWs));

	const EntityDB* pDb = pSpawner->GetEntityDB();
	const Locator spawnerLocWs = GetLocator();

	const Vector enterOffsetLs = pVarTapTable->m_enterOffsetLs;
	const Vector exitOffsetLs  = pVarTapTable->m_exitOffsetLs;

	NAV_ASSERT(IsReasonable(enterOffsetLs));
	NAV_ASSERT(IsReasonable(exitOffsetLs));

	TraversalActionPack::InitParams initParams;

	initParams.m_infoId = SID("auto-ladder");

	initParams.m_factionIdMask = BuildFactionMask(pDb);
	initParams.m_mutexNameId   = pSpawner->NameId();
	initParams.m_isVarTap	   = true;
	initParams.m_enterAnimType = DC::kVarTapAnimTypeGround;
	initParams.m_exitAnimType  = DC::kVarTapAnimTypeGround;
	initParams.m_entryPtLs	   = kOrigin + enterOffsetLs;
	initParams.m_exitOffsetLs  = exitOffsetLs;
	initParams.m_obeyedStaticBlockers = Nav::BuildStaticBlockageMask(pDb);
	initParams.m_disableNearPlayerRadius = pDb->GetData<float>(SID("disable-near-player-radius"), -1.0f);

	if (g_ndConfig.m_pBuildTensionModeMask)
	{
		initParams.m_tensionModeMask = (*g_ndConfig.m_pBuildTensionModeMask)(pDb);
	}

	ActionPackRegistrationParams params;
	params.m_pAllocLevel = pSpawner->GetLevel();

	if (pUpTap && !downOnly)
	{
		const Quat rotWs = QuatFromLookAt(AsUnitVectorXz(bottomEdge.GetWallNormal(), kUnitZAxis), bottomEdge.GetTopNormal());

		const Locator tapLocWs = Locator(bottomPosWs, rotWs);
		const Vector varTapOffsetLs = tapLocWs.UntransformVector(topPosWs - bottomPosWs);

		BoundFrame tapLoc = GetBoundFrame();
		tapLoc.SetLocatorWs(tapLocWs);

		initParams.m_spawnerSpaceLoc = spawnerLocWs.UntransformLocator(tapLocWs);
		initParams.m_skillMask		 = (1ULL << DC::kAiTraversalSkillLadderUp);
		initParams.m_directionType	 = DC::kVarTapDirectionUp;
		initParams.m_exitPtLs		 = kOrigin + (varTapOffsetLs + exitOffsetLs);

		if (!pUpTap->IsAssigned())
		{
			NDI_NEW(pUpTap) TraversalActionPack(tapLoc, pSpawner, initParams);
		}

		params.m_regPtLs = pUpTap->GetRegistrationPointLs();

		pUpTap->RequestRegistration(params);

		m_hUpTap = pUpTap;
	}
	else
	{
		m_hUpTap = nullptr;
	}
	
	const bool wantDownTap = !upOnly && CanEnterFromTop();

	if (pDownTap && wantDownTap)
	{
		const Quat rotWs = QuatFromLookAt(-AsUnitVectorXz(topEdge.GetWallNormal(), kUnitZAxis), topEdge.GetTopNormal());

		const Locator tapLocWs = Locator(topPosWs, rotWs);
		const Vector varTapOffsetLs = tapLocWs.UntransformVector(bottomPosWs - topPosWs);

		BoundFrame tapLoc = GetBoundFrame();
		tapLoc.SetLocatorWs(tapLocWs);

		initParams.m_spawnerSpaceLoc = spawnerLocWs.UntransformLocator(tapLocWs);
		initParams.m_skillMask		 = (1ULL << DC::kAiTraversalSkillLadderDown);
		initParams.m_directionType	 = DC::kVarTapDirectionDown;
		initParams.m_exitPtLs		 = kOrigin + (varTapOffsetLs + exitOffsetLs);

		if (!pDownTap->IsAssigned())
		{
			NDI_NEW(pDownTap) TraversalActionPack(tapLoc, pSpawner, initParams);
		}

		params.m_regPtLs = pDownTap->GetRegistrationPointLs();

		pDownTap->RequestRegistration(params);

		m_hDownTap = pDownTap;
	}
	else
	{
		m_hDownTap = nullptr;
	}

	if (pUpTap && pDownTap && wantDownTap)
	{
		pUpTap->SetReverseActionPack(pDownTap);
		pDownTap->SetReverseActionPack(pUpTap);

		pDownTap->SetReverseHalfOfTwoWayAp(true);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void BackgroundLadder::AllocateLadderTaps(const EntitySpawner& spawner)
{
	if (spawner.GetData<bool>(SID("disable-auto-taps"), false))
		return;

	BackgroundLadder::LoginTaps* pLoginData = NDI_NEW BackgroundLadder::LoginTaps;
	pLoginData->m_pUpTap   = NDI_NEW TraversalActionPack;
	pLoginData->m_pDownTap = NDI_NEW TraversalActionPack;

	spawner.SetLoginData(pLoginData);

	SpawnerLoginChunkActionPack* pDownChunk = NDI_NEW SpawnerLoginChunkActionPack(pLoginData->m_pDownTap);
	SpawnerLoginChunkActionPack* pUpChunk = NDI_NEW SpawnerLoginChunkActionPack(pLoginData->m_pUpTap);

	spawner.AddLoginChunk(pDownChunk);
	spawner.AddLoginChunk(pUpChunk);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const EdgeInfo& BackgroundLadder::GetTapTopEdge() const
{
	return m_edges[m_iTapTopEdge];
}

/// --------------------------------------------------------------------------------------------------------------- ///
const EdgeInfo& BackgroundLadder::GetTapBottomEdge() const
{
	return m_edges[m_iTapBottomEdge];
}

/// --------------------------------------------------------------------------------------------------------------- ///
const EdgeInfo& BackgroundLadder::ClosestHeightLadderEdgeWs(Point_arg posWs) const
{
	float bestDist = kLargeFloat;
	I32F iBest = 0;

	for (I32F iEdge = 0; iEdge < m_edges.Size(); ++iEdge)
	{
		if (0 == (m_edges[iEdge].GetFlags() & FeatureEdge::kFlagLadder))
		{
			continue;
		}

		const float dist = Abs(m_edges[iEdge].GetEdgeCenter().Y() - posWs.Y());

		if (dist < bestDist)
		{
			bestDist = dist;
			iBest = iEdge;
		}
	}

	return m_edges[iBest];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BackgroundLadder::GatherRegionInfo()
{
	if (m_edges.IsEmpty())
	{
		m_topTypeId = INVALID_STRING_ID_64;
		return;
	}

	const Region* apRegions[1] = { nullptr };

	const EdgeInfo& topEdge = m_edges[m_numLadderEdges - 1];
	const Point topPosWs = topEdge.GetEdgeCenter();

	const I32F numRegions = g_regionManager.GetRegionsByPositionAndKey(apRegions,
																	   ARRAY_COUNT(apRegions),
																	   topPosWs,
																	   0.0f,
																	   SID("ladder-top-region"));

	const Region* pRegion = (numRegions > 0) ? apRegions[0] : nullptr;

	m_topTypeId = pRegion ? pRegion->GetTagData<StringId64>(SID("top-type"), INVALID_STRING_ID_64)
						  : INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BgLadderManager::BgLadderManager()
	: m_accessLock(JlsFixedIndex::kBgLadderManagerLock, SID("BgLadderManager")), m_numLadders(0)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BgLadderManager::Evaluation()
{
	AtomicLockJanitorWrite_Jls writeLock(&m_accessLock, FILE_LINE_FUNC);

	// remove stale handles
	for (U32F i = 0; i < m_numLadders; ++i)
	{
		if (!m_hLadders[i].HandleValid())
		{
			m_hLadders[i] = m_hLadders[--m_numLadders];
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BgLadderManager::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	AtomicLockJanitorRead_Jls readLock(&m_accessLock, FILE_LINE_FUNC);

	for (U32F i = 0; i < m_numLadders; ++i)
	{
		if (const BackgroundLadder* pLadder = m_hLadders[i].ToProcess())
		{
			if (g_ndAiOptions.m_ladders.m_debugDraw || !pLadder->HasEdges())
			{
				pLadder->DebugDraw();
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BgLadderManager::Register(BackgroundLadder* pLadder)
{
	AtomicLockJanitorWrite_Jls writeLock(&m_accessLock, FILE_LINE_FUNC);

	if (m_numLadders < kMaxLadders)
	{
		m_hLadders[m_numLadders] = pLadder;
		++m_numLadders;
	}
}
