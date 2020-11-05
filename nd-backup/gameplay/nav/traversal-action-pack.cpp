/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/traversal-action-pack.h"

#include "corelib/containers/list-array.h"
#include "corelib/math/intersection.h"
#include "corelib/math/segment-util.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/tools-shared/patdefs.h"

#include "gamelib/camera/camera-manager.h"
#include "gamelib/feature/feature-db.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/background-ladder.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/nav/action-pack-mgr.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-defines.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-mgr.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-util.h"
#include "gamelib/gameplay/nav/nav-ledge.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/nd-action-pack-util.h"
#include "gamelib/gameplay/nav/platform-control.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/ndphys/collision-cast-interface.h"
#include "gamelib/ndphys/collision-filter.h"
#include "gamelib/ndphys/havok-collision-cast.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class TapBlockingBodyCollector : public HavokAabbQueryCollector
{
public:
	ActionPackHandle m_hAp;

	virtual bool AddBody(RigidBody* pBody)
	{
		if (!pBody->IsBreakable())
		{
			return true;
		}

		NdGameObject* pGo = pBody->GetOwner();
		if (!pGo || !pGo->GetApBlockerInterface())
		{
			return true;
		}

		if (ActionPack* pAp = m_hAp.ToActionPack())
		{
			if (pAp->CheckRigidBodyIsBlocking(pBody, 0))
			{
				return false;
			}
		}

		return true;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
TraversalActionPack::BuildInitParamsCb TraversalActionPack::s_buildInitParamsCb = nullptr;

static float kAnimRangeProbeRadius = 0.25f;

/// --------------------------------------------------------------------------------------------------------------- ///
struct ExtraPathNodes
{
	U16 m_foundPathNodes[TraversalActionPack::kMaxPathNodesPerSide];
	U32 m_ignoreNode = NavPathNodeMgr::kInvalidNodeId;
	U32 m_numFoundPathNodes = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static void WalkGatherPathNodes(const NavPoly* pPoly, const NavPolyEx* pPolyEx, uintptr_t userData)
{
	ExtraPathNodes& gatherData = *(ExtraPathNodes*)userData;

	if (!pPoly || (pPoly->GetPathNodeId() == gatherData.m_ignoreNode) || !pPoly->IsValid())
		return;

	if (gatherData.m_numFoundPathNodes >= TraversalActionPack::kMaxPathNodesPerSide)
		return;

	const U32F newPathNodeId = pPoly->GetPathNodeId();

	NAV_ASSERT(newPathNodeId != NavPathNodeMgr::kInvalidNodeId);
	NAV_ASSERT(newPathNodeId < NavPathNodeMgr::kMaxNodeCount);

	for (U32F i = 0; i < gatherData.m_numFoundPathNodes; ++i)
	{
		if (gatherData.m_foundPathNodes[i] == newPathNodeId)
			return;
	}

	gatherData.m_foundPathNodes[gatherData.m_numFoundPathNodes] = newPathNodeId;
	gatherData.m_numFoundPathNodes++;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Locator GetFlattenedEdgeLocWs(const FeatureEdgeReference& edge, Point_arg posWs)
{
	const Vector fwWs = AsUnitVectorXz(edge.GetWallNormal(), kZero);
	const Quat rotWs = QuatFromLookAt(fwWs, kUnitYAxis);
	return Locator(posWs, rotWs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float ProbeForEdgeClearanceWs(Point_arg fromWs,
									 Vector moveWs,
									 NavMesh::ProbeParams* pProbeParamsPs,
									 bool debugDraw,
									 DebugPrimTime tt)
{
	if (!pProbeParamsPs || !pProbeParamsPs->m_pStartPoly)
		return 0.0f;

	const NavMesh* pMesh = pProbeParamsPs->m_pStartPoly->GetNavMesh();
	if (!pMesh)
		return 0.0f;

	pProbeParamsPs->m_start = pMesh->WorldToParent(fromWs);
	pProbeParamsPs->m_move = pMesh->WorldToParent(moveWs);
	pProbeParamsPs->m_probeRadius = kAnimRangeProbeRadius;

	const NavMesh::ProbeResult res = pMesh->ProbePs(pProbeParamsPs);

	float hitTT = 1.0f;

	switch (res)
	{
	case NavMesh::ProbeResult::kReachedGoal:

		pProbeParamsPs->m_pStartPoly = pProbeParamsPs->m_pReachedPoly;

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			g_prim.Draw(DebugLine(fromWs, moveWs, kColorGreen, 2.0f, kPrimEnableHiddenLineAlpha), tt);
			DebugDrawFlatCapsule(fromWs,
								 fromWs + moveWs,
								 pProbeParamsPs->m_probeRadius,
								 kColorGreenTrans,
								 kPrimEnableHiddenLineAlpha,
								 tt);
		}
		break;

	case NavMesh::ProbeResult::kErrorStartedOffMesh:
	case NavMesh::ProbeResult::kErrorStartNotPassable:
		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			g_prim.Draw(DebugCircle(fromWs,
									pMesh->ParentToWorld(kUnitYAxis),
									pProbeParamsPs->m_probeRadius,
									kColorRed,
									kPrimEnableHiddenLineAlpha),
						tt);
		}
		hitTT = 0.0f;
		break;

	case NavMesh::ProbeResult::kHitEdge:
		{
			pProbeParamsPs->m_pStartPoly = pProbeParamsPs->m_pReachedPoly;

			const Point hitPosWs = pMesh->ParentToWorld(pProbeParamsPs->m_endPoint);
			Scalar stt;
			DistPointLine(hitPosWs, fromWs, fromWs + moveWs, nullptr, &stt);
			hitTT = stt;

			if (FALSE_IN_FINAL_BUILD(debugDraw))
			{
				g_prim.Draw(DebugLine(fromWs, hitPosWs, kColorRed, 4.0f, kPrimEnableHiddenLineAlpha), tt);
				g_prim.Draw(DebugCross(hitPosWs, pProbeParamsPs->m_probeRadius, kColorRed, kPrimEnableHiddenLineAlpha),
							tt);
			}
		}
		break;
	}

	return hitTT;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Locator RecreateEdgeLocFromPathLegPs(const IPathWaypoints& pathPs,
											U32F iWaypoint,
											Quat_arg edgeToApFwLs,
											Point_arg locPosPs)
{
	const U32F waypointCount = pathPs.GetWaypointCount();

	Locator retPs = Locator(locPosPs);

	if ((waypointCount == 0) || (iWaypoint >= waypointCount))
	{
	}
	else if (iWaypoint < (waypointCount - 1))
	{
		const Point pos0Ws = pathPs.GetWaypoint(iWaypoint);
		const Point pos1Ws = pathPs.GetWaypoint(iWaypoint + 1);

		const Vector sideVecWs = AsUnitVectorXz(pos0Ws - pos1Ws, kUnitXAxis);
		const Vector fwWs = Rotate(edgeToApFwLs, sideVecWs);
		const Quat rotWs = QuatFromLookAt(fwWs, kUnitYAxis);
		retPs = Locator(locPosPs, rotWs);
	}
	else if (iWaypoint > 0)
	{
		const Point pos0Ws = pathPs.GetWaypoint(iWaypoint - 1);
		const Point pos1Ws = pathPs.GetWaypoint(iWaypoint);

		const Vector sideVecWs = AsUnitVectorXz(pos0Ws - pos1Ws, kUnitXAxis);
		const Vector fwWs = Rotate(edgeToApFwLs, sideVecWs);
		const Quat rotWs = QuatFromLookAt(fwWs, kUnitYAxis);
		retPs = Locator(locPosPs, rotWs);
	}
	else
	{
		NAV_ASSERTF(false, ("??? %d / %d", iWaypoint, waypointCount));
	}

	return retPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static NavLocation::Type GetNavLocTypeFromAnimType(DC::VarTapAnimType animType)
{
#if ENABLE_NAV_LEDGES
	const NavLocation::Type navLocType = animType == DC::kVarTapAnimTypeLedge ? NavLocation::Type::kNavLedge
																			  : NavLocation::Type::kNavPoly;
	return navLocType;
#else
	return NavLocation::Type::kNavPoly;
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
static DC::VarTapAnimType GetNavLocAnimType(const NavLocation& navLoc)
{
#if ENABLE_NAV_LEDGES
	if (navLoc.GetType() == NavLocation::Type::kNavLedge)
	{
		return DC::kVarTapAnimTypeLedge;
	}
#endif

	DC::VarTapAnimType animType = DC::kVarTapAnimTypeGround;

	if (const NavMesh* pNavMesh = navLoc.ToNavMesh())
	{
		const float kSingleOccupancyArea = 6.5f;

		if (pNavMesh->NavMeshForcesSwim() || pNavMesh->NavMeshForcesDive())
		{
			animType = DC::kVarTapAnimTypeWater;
		}
		else if (pNavMesh->GetMaxOccupancyCount() < 2)
		{
			animType = DC::kVarTapAnimTypeGroundSmall;
		}
		else if (pNavMesh->GetSurfaceAreaXz() < kSingleOccupancyArea)
		{
			animType = DC::kVarTapAnimTypeGroundSmall;
		}
	}

	return animType;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool CanRegisterInternal(StringId64 apNameId,
								NavLocation::Type regType,
								const BoundFrame& tapBf,
								const ActionPackRegistrationParams& params,
								StringId64 navMeshLevelId,
								Nav::StaticBlockageMask obeyedStaticBlockers,
								NavLocation* pNavLocOut,
								bool drawErrors)
{
	const Point regPosWs = tapBf.GetLocatorWs().TransformPoint(params.m_regPtLs);

	if (pNavLocOut)
	{
		pNavLocOut->SetWs(regPosWs);
	}
	bool canRegister = false;

	switch (regType)
	{
	case NavLocation::Type::kNavPoly:
		{
			const NavPoly* pRegPoly = nullptr;

			canRegister = ActionPackCanRegisterToPoly(apNameId,
													  tapBf,
													  params.m_regPtLs,
													  params.m_bindId,
													  navMeshLevelId,
													  obeyedStaticBlockers,
													  params.m_yThreshold,
													  params.m_probeDist,
													  drawErrors,
													  params.m_pAllocLevel,
													  nullptr,
													  &pRegPoly);

			if (pNavLocOut)
			{
				pNavLocOut->SetWs(regPosWs, pRegPoly);
			}
		}
		break;
#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
		{
			const NavLedge* pRegLedge = nullptr;

			canRegister = ActionPackCanRegisterToLedge(tapBf,
													   params.m_regPtLs,
													   params.m_bindId,
													   params.m_probeDist,
													   drawErrors,
													   params.m_pAllocLevel,
													   nullptr,
													   &pRegLedge);

			if (pNavLocOut)
			{
				pNavLocOut->SetWs(regPosWs, pRegLedge);
			}
		}
		break;
#endif // ENABLE_NAV_LEDGES

	default:
		break;
	}

	return canRegister;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame ConstructVarTapEndBoundFrame(const BoundFrame& destBoundFrame,
										Vector_arg traversalDeltaLs,
										Vector_arg exitOffsetLs)
{
	// Transform the end ref into world space
	const Locator destLocWs	= destBoundFrame.GetLocatorWs();
	const Point apRefEndWs	= destLocWs.GetTranslation() + destLocWs.TransformVector(traversalDeltaLs);

	BoundFrame endBoundFrame = destBoundFrame;
	endBoundFrame.SetTranslation(apRefEndWs);

	// Edge offset on ledges is exactly 0 length
	if (LengthSqr(exitOffsetLs) > 0.0f)
	{
		// Orient the end bound frame toward the exit offset
		const Vector exitOffsetWs = Normalize(destLocWs.TransformVector(exitOffsetLs));
		const Vector upWs = GetLocalY(destLocWs.Rot());

		const Quat newRotWs = QuatFromLookAt(exitOffsetWs, upWs);

		endBoundFrame.SetRotationWs(newRotWs);

		//g_prim.Draw(DebugArrow(preRotWs.Pos(), baseDirWs, kColorRed));
		//g_prim.Draw(DebugArrow(preRotWs.Pos(), exitOffsetWs, kColorGreen));
		//g_prim.Draw(DebugCoordAxes(preRotWs, 1.0f, kPrimEnableHiddenLineAlpha, 2.0f));
		//g_prim.Draw(DebugCoordAxes(endBoundFrame.GetLocatorWs(), 0.5f, kPrimEnableHiddenLineAlpha, 4.0f));
	}

	return endBoundFrame;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static NavLocation FindDestNavLocation(const NavLocation::Type destType,
									   Nav::StaticBlockageMask obeyedStaticBlockers,
									   Point_arg searchPosWs)
{
	NavLocation ret;

	switch (destType)
	{
	case NavLocation::Type::kNavPoly:
		{
			FindBestNavMeshParams findParams;
			findParams.m_pointWs	= searchPosWs;
			findParams.m_cullDist	= 0.15f;
			findParams.m_yThreshold = 2.0f;
			findParams.m_bindSpawnerNameId	  = Nav::kMatchAllBindSpawnerNameId;
			findParams.m_obeyedStaticBlockers = obeyedStaticBlockers;

			EngineComponents::GetNavMeshMgr()->FindNavMeshWs(&findParams);

			ret.SetWs(findParams.m_nearestPointWs, findParams.m_pNavPoly);
		}
		break;
#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
		{
			FindNavLedgeGraphParams findParams;
			findParams.m_pointWs			= searchPosWs;
			findParams.m_searchRadius		= 1.0f;
			findParams.m_bindSpawnerNameId	= Nav::kMatchAllBindSpawnerNameId;

			NavLedgeGraphMgr::Get().FindLedgeGraph(&findParams);

			ret.SetWs(findParams.m_nearestPointWs, findParams.m_pNavLedge);
		}
		break;
#endif // ENABLE_NAV_LEDGES

	default:
		ret.SetWs(searchPosWs);
		break;
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Clock* GetGameplayClock()
{
	return EngineComponents::GetNdFrameState()->GetClock(kGameClock);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::CanRegisterSelf(const ActionPackRegistrationParams& params,
										  const BoundFrame& tapBf,
										  NavLocation* pNavLocOut /* = nullptr */) const
{
	const NavLocation::Type srcNavType = GetSourceNavType();

	const StringId64 reqLevelId = IsReverseHalfOfTwoWayAp() ? INVALID_STRING_ID_64 : m_navMeshLevelId;

	const bool canRegister = CanRegisterInternal(GetSpawnerId(),
												 srcNavType,
												 tapBf,
												 params,
												 reqLevelId,
												 m_obeyedStaticBlockers,
												 pNavLocOut,
												 false);

	return canRegister;
}

/// --------------------------------------------------------------------------------------------------------------- ///
TraversalActionPack::TraversalActionPack()
	: ParentClass(kTraversalActionPack)
	, m_usageDelay(0.0f)
	, m_oneShotUsageDelay(0.0f)
	, m_mutexId(INVALID_STRING_ID_64)
	, m_pMutex(nullptr)
	, m_spawnerSpaceLoc(kOrigin)
	, m_exitPtLs(kOrigin)
	, m_exitOffsetLs(kZero)
	, m_extraPathCost(0.0f)
	, m_rbBlockedExtraPathCost(0.0f)
	, m_vaultWallHeight(0.0f)
	, m_ladderRungSpan(0.0f)
	, m_animAdjustWidth(0.0f)
	, m_desAnimAdjustWidth(0.0f)
	, m_animRotDeg(0.0f)
	, m_disableNearPlayerRadius(-1.0f)
	, m_animAdjustType(AnimAdjustType::kNone)
	, m_edgeToApFwLs(kIdentity)
	, m_destNavType(NavLocation::Type::kNavPoly)
	, m_infoId(INVALID_STRING_ID_64)
	, m_directionType(DC::kVarTapDirectionUp)
	, m_enterAnimType(DC::kVarTapAnimTypeGround)
	, m_exitAnimType(DC::kVarTapAnimTypeGround)
	, m_ladderTapAnimOverlayId(INVALID_STRING_ID_64)
	, m_ropeAttachId(INVALID_STRING_ID_64)
	, m_navMeshLevelId(INVALID_STRING_ID_64)
	, m_factionIdMask(0)
	, m_instance(0)
	, m_skillMask(0)
	, m_tensionModeMask(0)
	, m_obeyedStaticBlockers(Nav::kStaticBlockageMaskAll)
	, m_isVarTap(false)
	, m_wantsEnable(true)
	, m_lastFullyReserved(false)
	, m_lastPlayerBlocked(false)
	, m_costDirty(false)
	, m_enablePlayerBlocking(false)
	, m_allowNormalDeaths(false)
	, m_briefReserved(false)
	, m_playerBoost(false)
	, m_singleUse(false)
	, m_allowFixToCollision(false)
	, m_fixedToCollision(false)
	, m_noPlayerBlockForPushyNpcs(false)
	, m_reverseHalfOfTwoWayAp(false)
	, m_noPush(false)
	, m_allowAbort(false)
	, m_heightDelta(0.0f)
	, m_origLoc(kOrigin)
	, m_origExitPtLs(kOrigin)
	, m_changesParentSpaces(false)
	, m_animAdjustSetup(false)
	, m_propagateToMutexOwners(true)
	, m_usageStartTime(TimeFrameNegInfinity())
	, m_fixedAnim(false)
	, m_caresAboutBlockingRb(true)
	, m_unboundedReservations(false)
{
	m_unboundedResCount.Set(0);

	for (U32F i = 0; i < kMaxPathNodesPerSide; ++i)
	{
		m_enterPathNodeIds[i] = NavPathNodeMgr::kInvalidNodeId;
		m_exitPathNodeIds[i]  = NavPathNodeMgr::kInvalidNodeId;
	}

	m_regLocType  = GetNavLocTypeFromAnimType(m_enterAnimType);
	m_destNavType = GetNavLocTypeFromAnimType(m_exitAnimType);
}

/// --------------------------------------------------------------------------------------------------------------- ///
TraversalActionPack::TraversalActionPack(const BoundFrame& bf, const EntitySpawner* pSpawner, const InitParams& params)
	: ParentClass(kTraversalActionPack, params.m_entryPtLs, bf, pSpawner)
	, m_usageDelay(params.m_usageDelay)
	, m_oneShotUsageDelay(0.0f)
	, m_mutexId(params.m_mutexNameId)
	, m_pMutex(nullptr)
	, m_destBoundFrame(bf)
	, m_spawnerSpaceLoc(params.m_spawnerSpaceLoc)
	, m_exitPtLs(params.m_exitPtLs)
	, m_exitOffsetLs(params.m_exitOffsetLs)
	, m_extraPathCost(params.m_extraPathCost)
	, m_rbBlockedExtraPathCost(params.m_rbBlockedExtraPathCost)
	, m_ladderRungSpan(params.m_ladderRungSpan)
	, m_animAdjustWidth(params.m_animAdjustWidth)
	, m_desAnimAdjustWidth(params.m_animAdjustWidth)
	, m_animRotDeg(params.m_animRotDeg)
	, m_disableNearPlayerRadius(params.m_disableNearPlayerRadius)
	, m_animAdjustType(params.m_animAdjustType)
	, m_edgeToApFwLs(kIdentity)
	, m_infoId(params.m_infoId)
	, m_directionType(params.m_directionType)
	, m_enterAnimType(params.m_enterAnimType)
	, m_exitAnimType(params.m_exitAnimType)
	, m_ropeAttachId(params.m_ropeAttachId)
	, m_navMeshLevelId(params.m_navMeshLevelId)
	, m_factionIdMask(params.m_factionIdMask)
	, m_instance(params.m_instance)
	, m_skillMask(params.m_skillMask)
	, m_tensionModeMask(params.m_tensionModeMask)
	, m_obeyedStaticBlockers(params.m_obeyedStaticBlockers)
	, m_isVarTap(params.m_isVarTap)
	, m_wantsEnable(true)
	, m_lastFullyReserved(false)
	, m_lastPlayerBlocked(false)
	, m_costDirty(false)
	, m_enablePlayerBlocking(params.m_enablePlayerBlocking)
	, m_allowNormalDeaths(params.m_allowNormalDeaths)
	, m_briefReserved(false)
	, m_playerBoost(params.m_playerBoost)
	, m_singleUse(params.m_singleUse)
	, m_allowFixToCollision(false)
	, m_fixedToCollision(false)
	, m_noPlayerBlockForPushyNpcs(false)
	, m_reverseHalfOfTwoWayAp(params.m_reverseHalfOfTwoWayAp)
	, m_noPush(params.m_noPush)
	, m_allowAbort(params.m_allowAbort)
	, m_heightDelta(0.0f)
	, m_changesParentSpaces(false)
	, m_animAdjustSetup(false)
	, m_propagateToMutexOwners(true)
	, m_usageStartTime(TimeFrameNegInfinity())
	, m_origLoc(m_loc)
	, m_origExitPtLs(params.m_exitPtLs)
	, m_fixedAnim(params.m_fixedAnim)
	, m_caresAboutBlockingRb(params.m_caresAboutBlockingRb)
	, m_unboundedReservations(params.m_unboundedReservations)
{
	m_unboundedResCount.Set(0);

	for (U32F i = 0; i < kMaxPathNodesPerSide; ++i)
	{
		m_enterPathNodeIds[i] = NavPathNodeMgr::kInvalidNodeId;
		m_exitPathNodeIds[i]  = NavPathNodeMgr::kInvalidNodeId;
	}

	m_regLocType  = GetNavLocTypeFromAnimType(m_enterAnimType);
	m_destNavType = GetNavLocTypeFromAnimType(m_exitAnimType);

	if (pSpawner)
	{
		m_ladderTapAnimOverlayId = pSpawner->GetData<StringId64>(SID("ladder-anim-overlay"), INVALID_STRING_ID_64);

		const StringId64 patSid = pSpawner->GetData<StringId64>(SID("ladder-pat"), INVALID_STRING_ID_64);
		if (patSid != INVALID_STRING_ID_64)
		{
			const Pat ladderPat = Pat(Pat::SurfaceTypeFromSid(patSid));
			SetLadderPat(ladderPat);
		}

		m_noPlayerBlockForPushyNpcs	= pSpawner->GetData<bool>(SID("no-player-block-for-pushy-npcs"), false);
		m_allowFixToCollision		= pSpawner->GetData<bool>(SID("fix-to-collision"), false);
	}
	else
	{
		m_ladderTapAnimOverlayId = INVALID_STRING_ID_64;
	}

	SetVaultWallHeight(params.m_vaultWallHeight);

	if (IsReverseHalfOfTwoWayAp() && (params.m_reverseAnimAdjustWidth >= 0.0f))
	{
		m_animAdjustWidth = AnimAdjustRange(params.m_reverseAnimAdjustWidth);
		m_desAnimAdjustWidth = m_animAdjustWidth;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
TraversalActionPack::~TraversalActionPack()
{
	for (U32F i = 0; i < kMaxPathNodesPerSide; ++i)
	{
		NAV_ASSERT(m_enterPathNodeIds[i] == NavPathNodeMgr::kInvalidNodeId);
		NAV_ASSERT(m_exitPathNodeIds[i] == NavPathNodeMgr::kInvalidNodeId);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::Logout()
{
	MsgAp("TraversalActionPack::Logout [%s]\n", GetName());

	ParentClass::Logout();

	NAV_ASSERT(m_pMutex == nullptr);

	RemovePathNodes();

	for (U32F i = 0; i < kMaxPathNodesPerSide; ++i)
	{
		NAV_ASSERT(m_enterPathNodeIds[i] == NavPathNodeMgr::kInvalidNodeId);
		NAV_ASSERT(m_exitPathNodeIds[i] == NavPathNodeMgr::kInvalidNodeId);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::Reset()
{
	MsgAp("TraversalActionPack::Reset [%s]\n", GetName());

	ParentClass::Reset();

	m_usageStartTime	= TimeFrameNegInfinity();
	m_oneShotUsageDelay	= 0.0f;

	ResetDestBoundFrame();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);

	if (!m_hDestNavLoc.IsNull())
	{
		NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

		if (NavPoly* pDestPoly = const_cast<NavPoly*>(m_hDestNavLoc.ToNavPoly()))
		{
			pDestPoly->RelocateActionPack(this, deltaPos, lowerBound, upperBound);
		}
#if ENABLE_NAV_LEDGES
		else if (NavLedge* pDestLedge = const_cast<NavLedge*>(m_hDestNavLoc.ToNavLedge()))
		{
			pDestLedge->RelocateActionPack(this, deltaPos, lowerBound, upperBound);
		}
#endif // ENABLE_NAV_LEDGES
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::NeedFixedToCollision() const
{
	if (!m_allowFixToCollision)
	{
		return false;
	}

	const BoundFrame& startAp = GetBoundFrame();
	const BoundFrame& destAp = GetBoundFrameInDestSpace();

	if (!startAp.GetBinding().IsSameBinding(destAp.GetBinding()))
	{
		return false;
	}

	if (!IsVault() && !IsJump() && !IsRope())
	{
		return false;
	}

	if (GetSourceNavType() != NavLocation::Type::kNavPoly &&
		GetDestNavType() != NavLocation::Type::kNavPoly)
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::FixToCollision(TraversalActionPack* pTap)
{
	if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_traversal.m_disableFixTapsToCollision))
	{
		return;
	}

	if (!pTap->NeedFixedToCollision())
	{
		pTap->SetFixedToCollision();
		return;
	}

	bool debug = false;
	const bool isRev = pTap->IsReverseHalfOfTwoWayAp();
	if (false) // (pTap->GetSpawnerId() == SID("tap-jump-up-down-28246"))
	{
		debug = true;
	}

	const bool isUp = pTap->GetBoundFrame().GetTranslation().Y() < pTap->GetVarTapEndBoundFrame().GetTranslation().Y();

	TraversalActionPack* pReverseTap = static_cast<TraversalActionPack*>(pTap->GetReverseActionPack());
	TraversalActionPack* pUpTap = isUp ? pTap : pReverseTap;
	TraversalActionPack* pDownTap = isUp ? pReverseTap : pTap;

	if (isUp && pDownTap && pDownTap->IsFixedToCollision())
	{
		pDownTap = nullptr;
	}
	else if (!isUp && pUpTap && pUpTap->IsFixedToCollision())
	{
		pUpTap = nullptr;
	}

	Point topApPosWs;
	//Point bottomApPosWs;

	Point topProbePosWs;
	Point bottomProbePosWs;

	Point topAdjSrcWs;
	Point topCollisionPosWs;

	Point bottomAdjSrcWs;
	Point bottomCollisionPosWs;

	// -- Find the top and bottom positions --

	const bool isVault = pTap->IsVault();

	if (pUpTap)
	{
		topApPosWs	  = pUpTap->GetVarTapEndBoundFrame().GetTranslation();
		//bottomApPosWs = pUpTap->GetBoundFrame().GetTranslation();

		topProbePosWs	 = pUpTap->GetExitPointWs();
		bottomProbePosWs = pUpTap->GetRegistrationPointWs();
	}
	else
	{
		NAV_ASSERT(pDownTap);

		topApPosWs	  = pDownTap->GetBoundFrame().GetTranslation();
		//bottomApPosWs = pDownTap->GetVarTapEndBoundFrame().GetTranslation();

		topProbePosWs	 = pDownTap->GetRegistrationPointWs();
		bottomProbePosWs = pDownTap->GetExitPointWs();
	}

	// -- Find the top and bottom collision positions --

	const CollideFilter collideFilter = GetCollideFilter();
	const F32 yThreshold = 0.5f;

	if (isVault)
	{
		HavokRayCastJob rayCast;

		// Compute the point on collision at the bottom
		rayCast.SimpleKick(bottomProbePosWs + Vector(kUnitYAxis) * yThreshold,
						   bottomProbePosWs - Vector(kUnitYAxis) * yThreshold,
						   collideFilter,
						   ICollCastJob::kCollCastSynchronous);

		if (rayCast.SimpleGather() < 0.0f)
		{
			rayCast.Close();
			return;
		}

		bottomAdjSrcWs = bottomProbePosWs;
		bottomCollisionPosWs = rayCast.GetContactPoint(0, 0);

		rayCast.Close();

		// Compute the point on collision at the top
		rayCast.SimpleKick(topProbePosWs + Vector(kUnitYAxis) * yThreshold,
						   topProbePosWs - Vector(kUnitYAxis) * yThreshold,
						   collideFilter,
						   ICollCastJob::kCollCastSynchronous);

		if (rayCast.SimpleGather() < 0.0f)
		{
			rayCast.Close();
			return;
		}

		topAdjSrcWs = topProbePosWs;
		topCollisionPosWs = rayCast.GetContactPoint(0, 0);

		rayCast.Close();
	}
	else
	{
		const FeatureEdge* pTopEdge = nullptr;

		if (pUpTap)
		{
			const bool edgeFound = pUpTap->FindEdge(debug);
			if (!edgeFound)
			{
				return;
			}

			pTopEdge = pUpTap->GetEdgeRef()->GetSrcEdge();
		}
		else
		{
			NAV_ASSERT(pDownTap);

			FeatureEdgeReference downEdgeRef = pDownTap->FindEdge(topApPosWs,
																  -GetLocalZ(pDownTap->GetLocatorWs().GetRotation()),
																  debug);
			if (!downEdgeRef.IsGood())
			{
				return;
			}

			pDownTap->SetEdgeRef(downEdgeRef);

			pTopEdge = downEdgeRef.GetSrcEdge();
		}

		NAV_ASSERT(pTopEdge);

		if (!pTopEdge)
		{
			return;
		}

		topCollisionPosWs = ClosestPointOnEdgeToPoint(pTopEdge->GetVert0(), pTopEdge->GetVert1(), topApPosWs);
		topAdjSrcWs = topApPosWs;
		bottomAdjSrcWs = bottomProbePosWs;

		bottomProbePosWs += topCollisionPosWs - topApPosWs;

		HavokRayCastJob rayCast;

		// Compute the point on collision at the bottom
		rayCast.SimpleKick(bottomProbePosWs + Vector(kUnitYAxis) * yThreshold,
						   bottomProbePosWs - Vector(kUnitYAxis) * yThreshold,
						   collideFilter,
						   ICollCastJob::kCollCastSynchronous);

		if (rayCast.SimpleGather() < 0.0f)
		{
			rayCast.Close();
			return;
		}

		if (FALSE_IN_FINAL_BUILD(debug))
		{
			g_prim.Draw(DebugLine(pTopEdge->GetVert0(), pTopEdge->GetVert1(), kColorRed, 4.0f, kPrimEnableHiddenLineAlpha), Seconds(5.0f));
			g_prim.Draw(DebugCross(pTopEdge->GetVert0(), 0.1f, kColorRed, kPrimEnableHiddenLineAlpha), Seconds(5.0f));
			g_prim.Draw(DebugCross(pTopEdge->GetVert1(), 0.1f, kColorRed, kPrimEnableHiddenLineAlpha), Seconds(5.0f));
			g_prim.Draw(DebugCross(topApPosWs, 0.25f, kColorGreen, kPrimEnableHiddenLineAlpha), Seconds(5.0f));
		}

		// Compute the point on the top edge
		bottomCollisionPosWs = rayCast.GetContactPoint(0, 0);

		rayCast.Close();
	}

	if (FALSE_IN_FINAL_BUILD(debug))
	{
		g_prim.Draw(DebugCross(topCollisionPosWs, 0.1f, kColorRed, kPrimEnableHiddenLineAlpha), Seconds(5.0f));
		g_prim.Draw(DebugString(topCollisionPosWs, "topCollisionPosWs", kColorRed, 0.5f), Seconds(5.0f));

		g_prim.Draw(DebugCross(bottomCollisionPosWs, 0.1f, kColorCyan, kPrimEnableHiddenLineAlpha), Seconds(5.0f));
		g_prim.Draw(DebugString(bottomCollisionPosWs, "bottomCollisionPosWs", kColorCyan, 0.5f), Seconds(5.0f));

		g_prim.Draw(DebugArrow(bottomProbePosWs + Vector(kUnitYAxis) * yThreshold,
							   bottomProbePosWs - Vector(kUnitYAxis) * yThreshold,
							   kColorCyan,
							   0.5f,
							   kPrimEnableHiddenLineAlpha),
					Seconds(5.0f));
	}

	// -- Find the new tap height --

	const F32 tapHeight = topCollisionPosWs.Y() - bottomCollisionPosWs.Y();

	if (tapHeight < 0.25f)
	{
		if (pUpTap)
		{
			pUpTap->SetFixedToCollision();
		}

		if (pDownTap)
		{
			pDownTap->SetFixedToCollision();
		}

		return;
	}

	const F32 vaultWallTopY		 = pTap->m_vaultWallHeight + topAdjSrcWs.Y();
	const F32 collisionMaxY		 = Max(topCollisionPosWs.Y(), bottomCollisionPosWs.Y());
	const F32 newVaultWallTopY	 = Max(collisionMaxY, vaultWallTopY);
	const F32 newVaultWallHeight = newVaultWallTopY - collisionMaxY;

	// -- Update the taps --

	if (pUpTap)
	{
		Point exitPointLs = pUpTap->GetExitPointLs();
		exitPointLs.SetY(tapHeight);

		BoundFrame upBoundFrame = pUpTap->GetBoundFrame();
		upBoundFrame.AdjustTranslationWs(bottomCollisionPosWs - bottomAdjSrcWs);

		pUpTap->m_vaultWallHeight = newVaultWallHeight;

		pUpTap->SetBoundFrame(upBoundFrame);
		pUpTap->SetBoundFrameInDestSpace(upBoundFrame);
		pUpTap->SetExitPointLs(exitPointLs);

		pUpTap->SetFixedToCollision();
	}

	if (pDownTap)
	{
		Point exitPointLs = pDownTap->GetExitPointLs();
		exitPointLs.SetY(-tapHeight);

		BoundFrame downBoundFrame = pDownTap->GetBoundFrame();
		downBoundFrame.AdjustTranslationWs(topCollisionPosWs - topAdjSrcWs);

		pDownTap->m_vaultWallHeight = newVaultWallHeight;

		pDownTap->SetBoundFrame(downBoundFrame);
		pDownTap->SetBoundFrameInDestSpace(downBoundFrame);
		pDownTap->SetExitPointLs(exitPointLs);

		pDownTap->SetFixedToCollision();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const BoundFrame TraversalActionPack::GetVarTapEndBoundFrame() const
{
	const BoundFrame& destBoundFrame = GetBoundFrameInDestSpace();
	const Vector traversalDeltaLs = GetTraversalDeltaLs();

	return ConstructVarTapEndBoundFrame(destBoundFrame, traversalDeltaLs, m_exitOffsetLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::AdjustEntryToHeight(const Locator& locWs, Scalar_arg newHeight)
{
	// Move the BoundFrame up to the appropriate height, leaving the exit point alone
	const BoundFrame bf(locWs, GetBoundFrame().GetBinding());
	const BoundFrame destBf(locWs, GetBoundFrameInDestSpace().GetBinding());

	if (const EntitySpawner* pSpawner = GetSpawner())
	{
		m_spawnerSpaceLoc = pSpawner->GetWorldSpaceLocator().UntransformLocator(bf.GetLocatorWs());
	}

	const Point heightPtLs(newHeight);

	SetBoundFrame(bf);
	SetBoundFrameInDestSpace(destBf);
	SetExitPointLs(PointFromXzAndY(GetExitPointLs(), heightPtLs));
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* TraversalActionPack::GetBoostAnimOverlaySetName() const
{
	StringId64 animActor = SID("boost-floor-tap");

	if (const EntitySpawner* pSpawner = GetSpawner())
	{
		animActor = pSpawner->GetData(SID("anim-actor"), animActor);
	}

	const char* overlaySetName;

	switch (animActor.GetValue())
	{
	case SID_VAL("boost-window-tap"):	overlaySetName = "window";	break;
	case SID_VAL("boost-fence-tap"):	overlaySetName = "fence";	break;
	case SID_VAL("boost-floor-tap"):
	default:						overlaySetName = "floor";	break;
	}

	return overlaySetName;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::FindEdge(bool debugDraw /* = false */)
{
	// Use var tap end boundframe instead of exit point. We want to try nearest edge on the top platform,
	// but the tap exit point may be on the flat ground. (Think about fence and lower window)
	const BoundFrame& endBoundFrame = GetVarTapEndBoundFrame();
	const Point findEdgePosWs		= endBoundFrame.GetTranslation();

	m_edgeRef = FindEdge(findEdgePosWs, GetLocalZ(GetLocatorWs().GetRotation()), debugDraw);
	return m_edgeRef.IsGood();
}

/// --------------------------------------------------------------------------------------------------------------- ///
FeatureEdgeReference TraversalActionPack::FindEdge(Point_arg findEdgePosWs,
												   Vector_arg tapFacingWs,
												   bool debugDraw /* = false */)
{
	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		g_prim.Draw(DebugCross(findEdgePosWs, 0.25f, kColorYellowTrans, kPrimEnableHiddenLineAlpha), Seconds(1.0f));
	}

	ScopedTempAllocator jj(FILE_LINE_FUNC);
	ListArray<EdgeInfo> edges(512);

	F32 checkDist = 1.2f;
	I32F numEdges = ::FindEdges(findEdgePosWs, &edges, nullptr, checkDist);

	Scalar bestCost(kLargestFloat);
	I32F bestIndex = -1;

	for (I32F ii = 0; ii < numEdges; ++ii)
	{
		const EdgeInfo& edgeRef = edges[ii];
		const FeatureEdge* const pEdge = edgeRef.GetSrcEdge();

		if (pEdge)
		{
			const Vector edgeDirNorm = SafeNormalize(Point(pEdge->GetVert1()) - Point(pEdge->GetVert0()), kZero);
			const float dotProd		 = Dot(edgeDirNorm, tapFacingWs);
			const Point edgeMidPt	 = edgeRef.GetEdgeCenter();
			const Scalar cost		 = DistPointSegment(findEdgePosWs, Point(pEdge->GetVert0()), Point(pEdge->GetVert1()));

			if (FALSE_IN_FINAL_BUILD(debugDraw))
			{
				const Color clr = AI::IndexToColor(ii);
				g_prim.Draw(DebugLine(Point(pEdge->GetVert1()), Point(pEdge->GetVert0()), clr, 1.0f, PrimAttrib(kPrimDisableDepthTest)), Seconds(1.0f));
				g_prim.Draw(DebugString(edgeMidPt, StringBuilder<64>("%.4f/%.4f", Abs(dotProd), (F32)cost).c_str(), clr, 0.5f), Seconds(1.0f));
			}

			if (cost > 0.5f)
			{
				continue;
			}

			if (Abs(dotProd) > 0.25f)
			{
				continue;
			}

			if (cost < bestCost)
			{
				bestIndex = ii;
				bestCost = cost;
			}
		}
	}

	if (bestIndex >= 0)
	{
		const EdgeInfo& edgeRef = edges[bestIndex];

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			const FeatureEdge* const pEdge = edgeRef.GetSrcEdge();
			g_prim.Draw(DebugLine(Point(pEdge->GetVert1()),
								  Point(pEdge->GetVert0()),
								  kColorOrange,
								  4.0f,
								  PrimAttrib(kPrimDisableDepthTest)),
						Seconds(1.0f));
		}

		return edgeRef;
	}

	return FeatureEdgeReference();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point TraversalActionPack::GetExitPointWs() const
{
	return GetExitPointWs(GetBoundFrameInDestSpace(), GetExitPointLs(), m_heightDelta);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
const Point TraversalActionPack::GetExitPointWs(const BoundFrame& destBoundFrame, Point_arg exitPtLs, float heightDelta)
{
	const Locator& parentSpace = destBoundFrame.GetParentSpace();
	const Locator& locWs	   = destBoundFrame.GetLocatorWs();
	const Point destWs		   = locWs.TransformPoint(exitPtLs);

	// Add the ground adjustment
	const Vector heightOffsetWs = GetLocalY(parentSpace.Rot()) * heightDelta;

	return destWs + heightOffsetWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point TraversalActionPack::GetOriginalExitPointWs() const
{
	const BoundFrame origDestBoundFrame(m_origLoc.GetLocatorWs(), m_destBoundFrame.GetBinding());
	const Locator& parentSpace = origDestBoundFrame.GetParentSpace();
	const Locator locWs		   = origDestBoundFrame.GetLocatorWs();
	const Point destWs		   = locWs.TransformPoint(GetOriginalExitPointLs());

	// Add the ground adjustment
	const Vector heightOffsetWs = GetLocalY(parentSpace.Rot()) * m_heightDelta;

	return destWs + heightOffsetWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 TraversalActionPack::GetTypeIdForScript() const
{
	if (IsRope())
	{
		return SID("tap-rope");
	}
	else if (IsRopeClimb())
	{
		return SID("tap-rope-climb");
	}
	else if (IsJump())
	{
		return IsPlayerBoost() ? SID("tap-boost") : SID("tap-jump");
	}
	else if (IsLadder())
	{
		return SID("tap-ladder");
	}
	else if (IsBalanceBeam())
	{
		return SID("tap-balance-beam");
	}
	else if (IsVaultUp())
	{
		return SID("tap-vault-up");
	}
	else if (IsVaultDown())
	{
		return SID("tap-vault-down");
	}
	else if (IsSqueezeThrough())
	{
		return SID("tap-squeeze-through");
	}

	return SID("tap-unknown");
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::GetDebugStatus(DebugStatus* pDebugStatus) const
{
	STRIP_IN_FINAL_BUILD;

	NAV_ASSERT(pDebugStatus);

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	pDebugStatus->m_isEnabled		 = IsEnabled();
	pDebugStatus->m_isSkillValid	 = true;
	pDebugStatus->m_isFactionValid	 = true;
	pDebugStatus->m_isPlayerBlocked	 = IsPlayerBlocked();
	pDebugStatus->m_isAvailable		 = IsAvailableInternal(true);
	pDebugStatus->m_isReserved		 = IsReserved() || (m_unboundedResCount.Get() > 0);
	pDebugStatus->m_hasDestination	 = GetDestNavLocation().IsValid();
	pDebugStatus->m_isRegistered	 = IsRegistered();
	pDebugStatus->m_isRbBlocked		 = IsBlockedByRigidBody();
	pDebugStatus->m_isTensionValid	 = IsTensionModeValid((*g_ndConfig.m_pGetTensionMode)());
	pDebugStatus->m_isPlayerDisabled = IsPlayerDisabled();
	pDebugStatus->m_hasNavClearance	 = !m_navBlocked;

	if (const NavCharacter* pNavChar = NavCharacter::FromProcess(DebugSelection::Get().GetSelectedProcess()))
	{
		if (const NavControl* pNavCon = pNavChar->GetNavControl())
		{
			pDebugStatus->m_isSkillValid = IsTraversalSkillMaskValid(pNavCon->GetTraversalSkillMask());
		}

		const FactionId faction = pNavChar->GetFactionId();
		if (faction != FactionId::kInvalid)
		{
			pDebugStatus->m_isFactionValid = IsFactionIdMaskValid(BuildFactionMask(faction));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::GetStatusDescription(IStringBuilder* pStrOut) const
{
	STRIP_IN_FINAL_BUILD;

	DebugStatus debugStatus;
	TraversalActionPack::GetDebugStatus(&debugStatus);

	const char* spacer = "";
	bool valid = true;

	if (debugStatus.m_isRbBlocked)
	{
		const RigidBody* const pBody = m_hBlockingRigidBody.ToBody();
		pStrOut->append_format("blocked by RB \"%s\"", pBody ? pBody->GetProcessName() : "<null>");
		spacer = " ";
		valid = false;
	}
	if (!debugStatus.m_hasNavClearance)
	{
		pStrOut->append(spacer);
		pStrOut->append_format("no-nav-clearance");
		spacer = " ";
		valid = false;
	}
	if (!debugStatus.m_isEnabled)
	{
		pStrOut->append(spacer);
		if (debugStatus.m_isPlayerDisabled)
			pStrOut->append_format("player-disabled (%0.2fm)", m_disableNearPlayerRadius);
		else
			pStrOut->append_format("disabled");
		spacer = " ";
		valid = false;
	}
	if (!debugStatus.m_isRegistered)
	{
		pStrOut->append(spacer);
		pStrOut->append_format("not-registered");
		spacer = " ";
		valid = false;
	}
	if (!debugStatus.m_hasDestination)
	{
		pStrOut->append(spacer);
		pStrOut->append_format("no-destination");
		spacer = " ";
		valid = false;
	}
	if (!debugStatus.m_isSkillValid)
	{
		pStrOut->append(spacer);
		pStrOut->append_format("invalid-skill");
		spacer = " ";
		valid = false;
	}
	if (!debugStatus.m_isTensionValid)
	{
		pStrOut->append(spacer);
		pStrOut->append_format("invalid-tension");
		spacer = " ";
		valid = false;
	}
	if (!debugStatus.m_isFactionValid)
	{
		pStrOut->append(spacer);
		pStrOut->append_format("invalid-faction");
		spacer = " ";
		valid = false;
	}
	if (debugStatus.m_isPlayerBlocked)
	{
		pStrOut->append(spacer);
		pStrOut->append_format("player-blocked");
		spacer = " ";
		valid = false;
	}

	if (valid && !debugStatus.m_isAvailable)
	{
		pStrOut->append(spacer);
		pStrOut->append_format("in-use");
		spacer = " ";
		valid = false;
	}

	if (debugStatus.m_isReserved)
	{
		pStrOut->append(spacer);
		if (HasUnboundedReservations())
		{
			pStrOut->append_format("reserved-unbound [%d]", m_unboundedResCount.Get());
		}
		else
		{
			pStrOut->append_format("reserved");
		}
		spacer = " ";
		valid = false;
	}

	if (valid)
	{
		pStrOut->append_format("free");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Color TraversalActionPack::GetDebugDrawColor() const
{
	STRIP_IN_FINAL_BUILD_VALUE(kColorWhite);

	const Color kColorDarkGreen(0.0f, 0.1f, 0.0f);
	const Color kColorDarkYellow(0.1f, 0.1f, 0.0f);

	DebugStatus debugStatus;
	TraversalActionPack::GetDebugStatus(&debugStatus);

	return debugStatus.m_isRbBlocked		? kColorCyan		:
		   !debugStatus.m_hasNavClearance	? kColorRed			:
		   !debugStatus.m_isEnabled			? (debugStatus.m_isPlayerDisabled ? kColorDarkYellow : kColorDarkGreen)	:
		   !debugStatus.m_isRegistered		? kColorRed			:
		   !debugStatus.m_hasDestination	? kColorPink		:
		   !debugStatus.m_isSkillValid		? kColorDarkGray	:
		   !debugStatus.m_isTensionValid	? kColorBlue		:
		   !debugStatus.m_isFactionValid	? kColorBlue		:
		   debugStatus.m_isPlayerBlocked	? kColorYellow		:
		   !debugStatus.m_isAvailable		? kColorOrange		:
		   debugStatus.m_isReserved			? kColorMagenta		:
		   GetFactionIdMask() == BuildFactionMask(EngineComponents::GetNdGameInfo()->GetPlayerFaction()) ? kColorPurple :
		   kColorGreen;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* TraversalActionPack::GetSkillMaskString() const
{
	static StringBuilder<256> skills;

	if (g_ndConfig.m_pGetTraversalSkillMaskString)
	{
		(*g_ndConfig.m_pGetTraversalSkillMaskString)(m_skillMask, &skills);
	}
	else
	{
		skills.format("[0x%x]", m_skillMask);
	}

	return skills.c_str();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::DebugFixToCollision()
{
	STRIP_IN_FINAL_BUILD;

	if (!IsFixedToCollision() && NeedFixedToCollision())
	{
		FixToCollision(this);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::DebugSetupAnimRange()
{
	STRIP_IN_FINAL_BUILD;

	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	if (m_animAdjustSetup)
		return;

	DebugFixToCollision();

	SetupAnimAdjust();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void SimpleDebugDrawEdge(const FeatureEdgeReference& edgeRef, const Color& color)
{
	STRIP_IN_FINAL_BUILD;

	if (const FeatureEdge* const pEdge = edgeRef.GetSrcEdge())
	{
		const Point edgeVert0Ws(pEdge->GetVert0());
		const Point edgeVert1Ws(pEdge->GetVert1());
		g_prim.Draw(DebugLine(edgeVert0Ws, edgeVert1Ws, color, 4.0f, kPrimEnableHiddenLineAlpha));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::DebugDraw(DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const Locator locWs		  = GetBoundFrame().GetLocatorWs();
	const Point regPosWs	  = GetRegistrationPointWs();
	const Point exitPosWs	  = locWs.TransformPoint(GetExitPointLs());
	const Point destExitPosWs = GetBoundFrameInDestSpace().GetLocatorWs().TransformPoint(GetExitPointLs());
	const Color drawColor	  = GetDebugDrawColor();

	const Locator destLocWs = GetBoundFrameInDestSpace().GetLocatorWs();

	ParentClass::DebugDraw(tt);

	//DebugDrawAnchor(SID("apReference"), tt);
	//DebugDrawAnchor(SID("apReference-end"), tt);

	if (IsVarTap())
	{
		const Point posWs = locWs.Pos();
		const Point endPosWs = GetVarTapEndBoundFrame().GetTranslation();

		g_prim.Draw(DebugLine(regPosWs, posWs, drawColor, 3.0f, kPrimEnableHiddenLineAlpha), tt);
		g_prim.Draw(DebugLine(endPosWs, destExitPosWs, drawColor, 3.0f, kPrimEnableHiddenLineAlpha), tt);

		if (IsVault())
		{
			Maybe<BoundFrame> maybeV1 = GetAnchorLocator(SID("apReference-vault-1"), 0.0f);
			Maybe<BoundFrame> maybeV2 = GetAnchorLocator(SID("apReference-vault-2"), 1.0f);

			if (maybeV1.Valid() && maybeV2.Valid())
			{
				//DebugDrawAnchor(SID("apReference-vault-1"), tt);
				//DebugDrawAnchor(SID("apReference-vault-2"), tt);

				Point points[4];
				points[0] = posWs;
				points[1] = maybeV1.Get().GetTranslationWs();
				points[2] = maybeV2.Get().GetTranslationWs();
				points[3] = endPosWs;

				g_prim.Draw(DebugLine(points[0], points[1], drawColor, 3.0f, kPrimEnableHiddenLineAlpha), tt);
				g_prim.Draw(DebugLine(points[1], points[2], drawColor, 3.0f, kPrimEnableHiddenLineAlpha), tt);
				g_prim.Draw(DebugLine(points[2], points[3], drawColor, 3.0f, kPrimEnableHiddenLineAlpha), tt);
			}
			else
			{
				g_prim.Draw(DebugString(AveragePos(posWs, endPosWs), "Vault Anchor Failure!", kColorRed), tt);
			}
		}
		else
		{
			g_prim.Draw(DebugLine(posWs, endPosWs, drawColor, 3.0f, kPrimEnableHiddenLineAlpha), tt);
		}
	}
	else
	{
		g_prim.Draw(DebugLine(regPosWs, exitPosWs, drawColor, 3.0f, kPrimEnableHiddenLineAlpha), tt);
	}

	g_prim.Draw(DebugCross(destExitPosWs, 0.03f, 2.0f, drawColor, kPrimEnableHiddenLineAlpha), tt);

	if (g_navMeshDrawFilter.m_drawApDetail)
	{
		static const char* s_anchors[] = {
			"apReference",
			"apReference-vault-1",
			"apReference-vault-2",
			"apReference-end"
		};

		for (int i = 0; i < ARRAY_COUNT(s_anchors); ++i)
		{
			const StringId64 anchorId = StringToStringId64(s_anchors[i]);
			Maybe<BoundFrame> maybeAp = GetAnchorLocator(anchorId, 0.0f);
			if (!maybeAp.Valid())
				continue;

			g_prim.Draw(DebugCoordAxesLabeled(maybeAp.Get().GetLocatorWs(),
											  s_anchors[i],
											  0.3f,
											  kPrimEnableHiddenLineAlpha,
											  2.0f,
											  kColorWhite,
											  0.5f));
		}

		if (IsJumpUp())
		{
			BoundFrame topLoc = GetVarTapEndBoundFrame();
			topLoc.AdjustRotationWs(QuatFromAxisAngle(kUnitYAxis, PI));
			const Locator topLocWs = topLoc.GetLocatorWs();
			const Point bottomPosWs = GetLocatorWs().Pos();
			DebugDrawJumpTapCurveWs(topLocWs, bottomPosWs, drawColor, tt);
		}
		else if (IsJumpDown())
		{
			BoundFrame topLoc = GetBoundFrame();
			const Point bottomPosWs = GetVarTapEndBoundFrame().GetTranslationWs();
			const Locator topLocWs = topLoc.GetLocatorWs();
			DebugDrawJumpTapCurveWs(topLocWs, bottomPosWs, drawColor, tt);
		}
	}

	if (g_navMeshDrawFilter.m_drawTraversalApAnimRange || g_navMeshDrawFilter.m_drawApDetail)
	{
		const RenderCamera& cam = GetRenderCamera(0);
		const Point apPos0Ws = GetBoundFrame().GetTranslationWs();
		const Point apPos1Ws = GetVarTapEndBoundFrame().GetTranslationWs();
		const Point frustumPosWs = AveragePos(apPos0Ws, apPos1Ws);
		const float r = Max(Dist(apPos0Ws, apPos1Ws), SCALAR_LC(2.0f));

		if (cam.IsSphereInFrustum(Sphere(frustumPosWs, r)) && (Dist(cam.m_position, frustumPosWs) < 20.0f))
		{
			DebugDrawAnimRange(tt);
		}
	}

	// Only draw this yellow line if the tap linked
	if (GetDestNavLocation().IsValid() && Dist(exitPosWs, destExitPosWs) > 0.01f)
	{
		g_prim.Draw(DebugLine(exitPosWs, destExitPosWs, kColorYellow, 2.0f, kPrimEnableHiddenLineAlpha), tt);
	}

	if (m_edgeRef.IsGood())
	{
		const Color edgeDrawColor = Lerp(kColorBlack, drawColor, 0.35f);
		SimpleDebugDrawEdge(m_edgeRef, edgeDrawColor);
	}

	if (const BackgroundLadder* pBgLadder = BackgroundLadder::FromProcess(GetProcess()))
	{
		pBgLadder->DebugDraw(tt);
	}

	if (IsBlockedByRigidBody())
	{
		Obb rbCheckObb;
		if (GetCheckBlockingObb(rbCheckObb))
		{
			g_prim.Draw(DebugBox(rbCheckObb, kColorCyan, PrimAttrib(kPrimEnableWireframe)), tt);
		}
	}

	if (g_navMeshDrawFilter.m_drawApDetail)
	{
		DebugDrawPathNodes(tt);
	}

	if (!HasNavMeshClearance(m_hRegisteredNavLoc))
	{
		HasNavMeshClearance(m_hRegisteredNavLoc, true, tt);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::DebugDrawPathNodes(DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	for (U32F i = 0; i < kMaxPathNodesPerSide; ++i)
	{
		const U32F enterNodeId = m_enterPathNodeIds[i];

		if (enterNodeId == NavPathNodeMgr::kInvalidNodeId)
			continue;

		const NavPathNode& enterNode = g_navPathNodeMgr.GetNode(enterNodeId);

		const NavPoly* pEnterPoly = enterNode.GetNavPolyHandle().ToNavPoly();
		const NavMesh* pEnterMesh = pEnterPoly ? pEnterPoly->GetNavMesh() : nullptr;

		const Point posPs = enterNode.GetPositionPs();
		const Point posWs = pEnterMesh ? pEnterMesh->ParentToWorld(posPs) : enterNode.ParentToWorld(posPs);

		NavLocation enterLoc;
		enterLoc.SetPs(posPs, pEnterPoly);
		enterLoc.DebugDraw();

		g_prim.Draw(DebugLine(posWs, posWs + Vector(0.0f, 0.3f, 0.0f), kColorWhite, 2.0f, kPrimEnableHiddenLineAlpha), tt);

		StringBuilder<256> desc;
		desc.format("%d [%s]", enterNodeId, pEnterMesh ? pEnterMesh->GetName() : "null");
		g_prim.Draw(DebugString(posWs + Vector(0.0f, 0.3f, 0.0f), desc.c_str(), kColorWhite, 0.6f), tt);
	}

	for (U32F i = 0; i < kMaxPathNodesPerSide; ++i)
	{
		const U32F exitNodeId = m_exitPathNodeIds[i];

		if (exitNodeId == NavPathNodeMgr::kInvalidNodeId)
			continue;

		const NavPathNode& exitNode = g_navPathNodeMgr.GetNode(exitNodeId);

		const NavPoly* pExitPoly = exitNode.GetNavPolyHandle().ToNavPoly();
		const NavMesh* pExitMesh = pExitPoly ? pExitPoly->GetNavMesh() : nullptr;

		const Point posPs = exitNode.GetPositionPs();
		const Point posWs = pExitMesh ? pExitMesh->ParentToWorld(posPs) : exitNode.ParentToWorld(posPs);

		NavLocation exitLoc;
		exitLoc.SetPs(posPs, pExitPoly);
		exitLoc.DebugDraw();

		g_prim.Draw(DebugLine(posWs, posWs + Vector(0.0f, 0.3f, 0.0f), kColorWhite, 2.0f, kPrimEnableHiddenLineAlpha), tt);

		StringBuilder<256> desc;
		desc.format("%d [%s]", exitNodeId, pExitMesh ? pExitMesh->GetName() : "null");
		g_prim.Draw(DebugString(posWs + Vector(0.0f, 0.3f, 0.0f), desc.c_str(), kColorWhite, 0.6f), tt);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::DebugDrawAnchor(StringId64 apNameId, DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	Maybe<BoundFrame> srcBf = GetAnchorLocator(apNameId, 0.0f);
	Maybe<BoundFrame> dstBf = GetAnchorLocator(apNameId, 1.0f);

	if (srcBf.Valid() && dstBf.Valid() && ChangesParentSpaces())
	{
		const Locator srcWs = srcBf.Get().GetLocatorWs();
		const Locator dstWs = dstBf.Get().GetLocatorWs();

		g_prim.Draw(DebugCoordAxesLabeled(srcWs,
										  DevKitOnly_StringIdToString(apNameId),
										  0.3f,
										  kPrimEnableHiddenLineAlpha,
										  4.0f,
										  kColorWhite,
										  0.5f),
					tt);

		g_prim.Draw(DebugCoordAxes(dstWs, 0.3f, kPrimEnableHiddenLineAlpha, 4.0f), tt);

		g_prim.Draw(DebugLine(srcWs.Pos(), dstWs.Pos(), kColorGreen, 4.0f, kPrimEnableHiddenLineAlpha), tt);
	}
	else if (srcBf.Valid())
	{
		const Locator srcWs = srcBf.Get().GetLocatorWs();

		g_prim.Draw(DebugCoordAxesLabeled(srcWs,
										  DevKitOnly_StringIdToString(apNameId),
										  0.3f,
										  kPrimEnableHiddenLineAlpha,
										  4.0f,
										  kColorWhite,
										  0.5f),
					tt);
	}
	else if (dstBf.Valid())
	{
		// ?!?
		const Locator dstWs = dstBf.Get().GetLocatorWs();

		g_prim.Draw(DebugCoordAxesLabeled(dstWs,
										  DevKitOnly_StringIdToString(apNameId),
										  0.3f,
										  kPrimEnableHiddenLineAlpha,
										  4.0f,
										  kColorWhite,
										  0.5f),
					tt);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::DebugDrawRegistrationFailure() const
{
	STRIP_IN_FINAL_BUILD;

	if (m_navMeshLevelId != INVALID_STRING_ID_64 && !g_navMeshDrawFilter.m_drawHalfLoadedTapRegFailure)
	{
		if (!EngineComponents::GetLevelMgr()->IsLevelFullySpawned(m_navMeshLevelId))
		{
			return;
		}
	}

	const NavLocation::Type srcNavType = GetSourceNavType();
	const StringId64 spawnerId = GetSpawnerId();
	const BoundFrame tapBf = GetBoundFrame();
	CanRegisterInternal(spawnerId,
						srcNavType,
						tapBf,
						m_regParams,
						m_navMeshLevelId,
						m_obeyedStaticBlockers,
						nullptr,
						true);

	const Point regPosWs = GetRegistrationPointWs();
	g_prim.Draw(DebugString(regPosWs + kUnitYAxis, DevKitOnly_StringIdToString(m_infoId), kColorWhite, 0.5f));
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F TraversalActionPack::GetUserCount() const
{
	I32F userCount = 0;

	if (m_pMutex && m_pMutex->GetEnabledActionPack() == this)
	{
		userCount = m_pMutex->GetUserCount();
	}

	return userCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::AddUser(Process* pChar)
{
	bool added = false;
	NAV_ASSERT(IsAvailableFor(pChar) || !pChar || pChar->IsKindOf(SID("SimpleNpc")));

	if (m_pMutex)
	{
		NAV_ASSERT(m_pMutex->GetEnabledActionPack() == this);
		added = m_pMutex->AddUserRef(pChar);
	}

	return added;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::TryAddUser(Process* pChar)
{
	AtomicLockJanitor userLock(&m_addUserLock, FILE_LINE_FUNC);

	if (!HasUsageTimerExpired())
	{
		return false;
	}

	bool added = true;

	if (m_pMutex)
	{
		added = m_pMutex->TryAddUserRef(this, pChar);
	}
	else
	{
		AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

		const Process* pResHolder = m_hReservationHolder.ToProcess();
		if (pResHolder && (pResHolder != pChar))
		{
			added = false;
		}
	}

	MsgAp("TraversalActionPack::TryAddUser [%s] '%s' : %s\n",
		  GetName(),
		  pChar ? pChar->GetName() : "<null>",
		  added ? "success" : "failed");

	ResetReadyToUseTimer();

	return added;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::RemoveUser(Process* pChar)
{
	if (m_pMutex)
	{
		// JDB: turning this off because we might be removing a user because the TAP got deleted or some other form of cleanup
		// NAV_ASSERT(m_pMutex->GetEnabledActionPack() == this);
		const bool removed = m_pMutex->RemoveUserRef(pChar);

		MsgAp("TraversalActionPack::RemoveUser [%s] '%s' : %s\n",
			  GetName(),
			  pChar ? pChar->GetName() : "<null>",
			  removed ? "success" : "failed");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float TraversalActionPack::DistToPointWs(Point_arg posWs, float entryOffset) const
{
	float minDist = kLargeFloat;

	const Locator animAdjustLs = GetApAnimAdjustLs(posWs);

	BoundFrame boundFrame = GetBoundFrame();
	boundFrame.AdjustLocatorLs(animAdjustLs);

	BoundFrame dstBoundFrame = GetBoundFrameInDestSpace();
	dstBoundFrame.AdjustLocatorLs(animAdjustLs);

	const Vector traversalDeltaLs = GetTraversalDeltaLs();
	const BoundFrame endBoundFrame = ConstructVarTapEndBoundFrame(dstBoundFrame, traversalDeltaLs, m_exitOffsetLs);

	if (IsVault())
	{
		const float wallHeight = GetVaultWallHeight();

		const Vector delta0Ls = Vector(0.0f, wallHeight, 0.0f);
		const Vector delta1Ls = Vector(0.0f, wallHeight, traversalDeltaLs.Z());

		BoundFrame apVault0 = boundFrame;
		BoundFrame apVault1 = boundFrame;

		apVault0.AdjustTranslationLs(delta0Ls);
		apVault1.AdjustTranslationLs(delta1Ls);

		Point pointsWs[4];
		pointsWs[0] = boundFrame.GetTranslationWs();
		pointsWs[1] = apVault0.GetTranslationWs();
		pointsWs[2] = apVault1.GetTranslationWs();
		pointsWs[3] = endBoundFrame.GetTranslationWs();

		for (U32F i = 0; i < (ARRAY_COUNT(pointsWs) - 1); ++i)
		{
			const Point p0Ws = pointsWs[i];
			const Point p1Ws = pointsWs[i + 1];

			const float d = DistPointSegment(posWs, p0Ws, p1Ws);
			minDist = Min(d, minDist);
		}
	}
	else
	{
		const Point startPosWs	= boundFrame.GetTranslationWs();
		const Point endPosWs	= endBoundFrame.GetTranslationWs();

		minDist = DistPointSegment(posWs, startPosWs, endPosWs);
	}

	return minDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::SetOneShotUsageDelay(F32 usageDelay)
{
	m_oneShotUsageDelay = usageDelay;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::UpdateSourceBinding()
{
	NAV_ASSERT(GetRegisteredNavLocation().IsValid());

	// Assumes this is authoritative (i.e. ParentClass::RegisterSelfToNavPoly has been called)
	const StringId64 bindSpawnerId = GetRegisteredNavLocation().GetBindSpawnerNameId();

	BoundFrame bf = GetBoundFrame();
	bf.RemoveBinding();

	if (bindSpawnerId != INVALID_STRING_ID_64)
	{
		if (const EntitySpawner* pBindSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByBareNameId(bindSpawnerId))
		{
			if (NdGameObject* pGo = pBindSpawner->GetNdGameObject())
			{
				if (const PlatformControl* pPlatform = pGo->GetPlatformControl())
				{
					bf.SetBinding(Binding(pPlatform->GetBindTarget()));
				}
				else
				{
					MsgErr("TraversalActionPack::UpdateSourceBinding: Platform process %s has no platform control\n",
						   DevKitOnly_StringIdToString(bindSpawnerId));
				}
			}
			else
			{
				// Not an error: This happens normally when platform object has not yet spawned
			}
		}
		else
		{
			MsgErr("TraversalActionPack::UpdateSourceBinding: Platform spawner %s not found\n",
				   DevKitOnly_StringIdToString(bindSpawnerId));
		}
	}

	m_enterAnimType = GetNavLocAnimType(m_hRegisteredNavLoc);

	SetBoundFrame(bf);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::UpdateNavDestUnsafe()
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	const NavLocation srcNavLoc = GetSourceNavLocation();

	const BoundFrame& baseBf = GetBoundFrame();
	const Locator locWs = baseBf.GetLocatorWs();
	const Point destWs = locWs.TransformPoint(GetExitPointLs());

	m_hDestNavLoc = FindDestNavLocation(GetDestNavType(), Nav::kStaticBlockageMaskNone, destWs);
	m_changesParentSpaces = false;

	if (srcNavLoc.IsValid() && m_hDestNavLoc.IsValid())
	{
		if (HavePathNodes())
		{
			const NavPathNode& destNode = g_navPathNodeMgr.GetNode(m_exitPathNodeIds[0]);

			if (destNode.GetNavManagerId() == m_hDestNavLoc.GetNavManagerId())
			{
				// already registered, don't need to unregister
				return;
			}
			else
			{
				// we registered to a different nav mesh than before
				RemovePathNodes();
			}
		}

		AddPathNodes();

		const StringId64 srcBindSpawnerNameId = srcNavLoc.GetBindSpawnerNameId();
		const StringId64 dstBindSpawnerNameId = m_hDestNavLoc.GetBindSpawnerNameId();

		m_changesParentSpaces = srcBindSpawnerNameId != dstBindSpawnerNameId;

		if (m_changesParentSpaces)
		{
			// Parent spaces differ, need to lookup the bound frame of dest parent space
			if (const EntitySpawner* pBindSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByBareNameId(dstBindSpawnerNameId))
			{
				if (NdGameObject* pProc = pBindSpawner->GetNdGameObject())
				{
					if (const PlatformControl* pPlatform = pProc->GetPlatformControl())
					{
						SetBoundFrameInDestSpace(BoundFrame(GetBoundFrameInDestSpace().GetLocatorWs(),
															Binding(pPlatform->GetBindTarget())));
					}
					else
					{
						// This should never happen, pProc should have a PlatformControl if it has one or
						// more navmeshes on it. And we only found it because of the navmesh we found.
						MsgErr("TraversalActionPack::UpdateNavDestUnsafe: dest platform process %s has no platform control\n",
							   DevKitOnly_StringIdToString(dstBindSpawnerNameId));
						ClearNavDestUnsafe();
					}
				}
				else
				{
					// Not an error: This case happens normally when dest platform object has not yet spawned.
					//MsgErr("TraversalActionPack::UpdateNavDestUnsafe: dest platform spawner found but process %s not found\n", DevKitOnly_StringIdToString(destBindSpawnerNameId));
					ClearNavDestUnsafe();
				}
			}
			// Dest spawner was not found
			else if (dstBindSpawnerNameId != INVALID_STRING_ID_64)
			{
				MsgErr("TraversalActionPack::UpdateNavDestUnsafe: dest platform spawner %s not found\n",
					   DevKitOnly_StringIdToString(dstBindSpawnerNameId));
				ClearNavDestUnsafe();
			}
			// Dest has no spawner
			else
			{
				SetBoundFrameInDestSpace(BoundFrame(baseBf.GetLocatorWs()));
			}
		}
		else
		{
			// If src and dest are in the same parent space, use the same locator and binding for both
			SetBoundFrameInDestSpace(baseBf);
		}
	}
	else
	{
		ClearNavDestUnsafe();
	}

	m_exitAnimType = GetNavLocAnimType(m_hDestNavLoc);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::ClearNavDestUnsafe()
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	// There is an infrequent crash that occurs in Nav::FindReachablePolys. Adding an ASSERT narrowed it to a corrupt
	// TAP. The problem seems to be that there are a few TAPs that span levels. If such a TAP is created and
	// registered and linked to the dest level and then the dest level is logged out, the TAP is now pointing to
	// invalid data. If a Nav::FindReachablePolys search (or perhaps a static path find) is performed it will probably
	// crash on the invalid data. The following code is to remove the pointers to data in the level that is logging out.

	RemovePathNodes();

	m_hDestNavLoc.SetWs(GetBoundFrameInDestSpace().GetTranslationWs());
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::RegisterSelfToNavPoly(const NavPoly* pPoly)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	ClearNavDestUnsafe();
	const bool registered = ParentClass::RegisterSelfToNavPoly(pPoly);
	UpdateSourceBinding();
	UpdateNavDestUnsafe();

	return registered;
}

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::RegisterSelfToNavLedge(const NavLedge* pNavLedge)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	ClearNavDestUnsafe();
	const bool registered = ParentClass::RegisterSelfToNavLedge(pNavLedge);
	UpdateSourceBinding();
	UpdateNavDestUnsafe();

	return registered;
}
#endif // ENABLE_NAV_LEDGES

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::RegisterInternal()
{
	MsgAp("TraversalActionPack::RegisterInternal [%s]\n", GetName());

	NAV_ASSERTF(!m_regParams.m_readOnly, ("Trying to register TAP '%s' as read-only, which is not allowed!", GetName()));
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());
	NAV_ASSERT(!m_pMutex);

	if (m_mutexId != INVALID_STRING_ID_64)
	{
		m_pMutex = ActionPackMgr::Get().LookupActionPackMutexByName(m_mutexId);
	}

	if (m_pMutex)
	{
		NAV_ASSERT(m_mutexId == m_pMutex->GetNameId());

		m_pMutex->AddOwnerRef(this);

		if (m_unboundedReservations)
		{
			m_pMutex->EnableDirectionalValve(m_unboundedReservations);
		}
	}

	if (const EntitySpawner* pParentSpawner = GetParentSpawner())
	{
		// reverse taps can originate from anywhere, while forward taps should
		// obey the configured parent
		if (IsReverseHalfOfTwoWayAp())
		{
			m_regParams.m_bindId = Nav::kMatchAllBindSpawnerNameId;
		}
		else
		{
			m_regParams.m_bindId = pParentSpawner->NameId();
		}
	}

	bool isRegistered = false;

	m_hRegisteredNavLoc.SetWs(GetRegistrationPointWs());

	NavLocation srcLoc;
	if (CanRegisterSelf(m_regParams, GetBoundFrame(), &srcLoc))
	{
		m_regLocType = srcLoc.GetType();

		switch (m_regLocType)
		{
		case NavLocation::Type::kNavPoly:
			if (const NavPoly* pPoly = srcLoc.ToNavPoly())
			{
				isRegistered = RegisterSelfToNavPoly(pPoly);
			}
			break;
#if ENABLE_NAV_LEDGES
		case NavLocation::Type::kNavLedge:
			if (const NavLedge* pLedge = srcLoc.ToNavLedge())
			{
				isRegistered = RegisterSelfToNavLedge(pLedge);
			}
			break;
#endif // ENABLE_NAV_LEDGES

		default:
			NAV_ASSERT(m_regLocType != NavLocation::Type::kNone);
			break;
		}
	}

	if (!isRegistered)
	{
		if (m_pMutex)
		{
			m_pMutex->RemoveOwnerRef(this);
		}

		m_pMutex = nullptr;
	}

	if (isRegistered)
	{
		m_enterAnimType = GetNavLocAnimType(m_hRegisteredNavLoc);
		m_exitAnimType = GetNavLocAnimType(m_hDestNavLoc);

		RefreshNavMeshClearance();

		SetupAnimAdjust();

		SearchForBlockingRigidBodies();
	}

	return isRegistered;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::UnregisterInternal()
{
	MsgAp("TraversalActionPack::UnregisterInternal [%s]\n", GetName());

	NAV_ASSERTF(!m_regParams.m_readOnly, ("Trying to unregister TAP '%s' as read-only, which is not allowed!", GetName()));
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	if (HavePathNodes())
	{
		ClearNavDestUnsafe();
	}

	if (m_pMutex)
	{
		m_pMutex->RemoveOwnerRef(this);
		m_pMutex = nullptr;
	}

	RemoveBlockingRigidBody();

	ParentClass::UnregisterInternal();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::GetEdgeVerts(Point& bottom0Ws, Point& bottom1Ws, Point& top0Ws, Point& top1Ws, float offset) const
{
	if (const FeatureEdgeReference* pEdgeRef = GetEdgeRef())
	{
		if (const FeatureEdge* const pEdge = pEdgeRef->GetSrcEdge())
		{
			const Point entryPtWs = GetRegistrationPointWs();
			const Point exitPtWs = GetExitPointWs();

			const Point end0 = Point(pEdge->GetVert0());
			const Point end1 = Point(pEdge->GetVert1());

			float originalDist = Dist(end0, end1);
			if (originalDist > offset * 2.0f)
			{
				const Vector dir = SafeNormalize(end1 - end0, kZero);
				top0Ws = end0 + dir * offset;
				top1Ws = end1 - dir * offset;
			}
			else
			{
				// If adjust offset is longer than edge length, just adjust to center
				float adjustedOffset = originalDist / 2.0f - 0.05f;
				NAV_ASSERT(adjustedOffset > 0.0f);
				const Vector dir = SafeNormalize(end1 - end0, kZero);
				top0Ws = end0 + dir * adjustedOffset;
				top1Ws = end1 - dir * adjustedOffset;
			}

			const Vector deltaLs = GetTraversalDeltaLs();
			const Vector deltaWs = GetLocatorWs().TransformVector(deltaLs);

			bottom0Ws = top0Ws - deltaWs;
			bottom1Ws = top1Ws - deltaWs;

			//ASSERT((bottom0Ws - top0Ws).Y() < SCALAR_LC(0.0f));
			//ASSERT((bottom1Ws - top1Ws).Y() < SCALAR_LC(0.0f));

			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::Update()
{
	if (FALSE_IN_FINAL_BUILD(g_ndAiOptions.m_aggressiveApMutexValidation && m_pMutex))
	{
		m_pMutex->Validate();
	}

	const bool wantsEnable = m_wantsEnable && !IsPlayerDisabled();
	const bool isEnabled = IsEnabled();

	if (isEnabled != wantsEnable)
	{
		if (!wantsEnable && (GetUserCount() > 0))
		{
			// stall until all users leave
		}
		else
		{
			ParentClass::Enable(wantsEnable);
			EnableInternal(wantsEnable);
		}
	}

	// Only block path nodes on an ordinary reserve (because we are taking cover near the TAP) since we might be
	// blocking the TAP for a long time, we don't want anyone to get stuck waiting for it
	const bool fullyReserved = IsReserved() && !m_briefReserved;
	if (m_lastFullyReserved != fullyReserved)
	{
		BlockPathNodesInternal(fullyReserved);
		m_lastFullyReserved = fullyReserved;
		m_costDirty = true;
	}

	if (m_lastPlayerBlocked != m_playerBlocked)
	{
		m_costDirty = true;
		m_lastPlayerBlocked = m_playerBlocked;
	}

	const bool costWasDirty = m_costDirty;

	if (m_costDirty)
	{
		UpdatePathNodeCost();
		m_costDirty = false;
	}

	if (!m_animAdjustSetup)
	{
		SetupAnimAdjust();
	}

	const bool blockedByRb = IsBlockedByRigidBody();

	if (m_navBlocked)
	{
		RefreshNavMeshClearance();

		const bool blockPathNodes = m_navBlocked || !IsEnabled() || blockedByRb;

		BlockPathNodesInternal(blockPathNodes);
	}
	else if (costWasDirty)
	{
		const bool blockPathNodes = m_navBlocked || !IsEnabled() || blockedByRb;
		BlockPathNodesInternal(blockPathNodes);
	}

	const bool needsUpdate = NeedsUpdate();
	ActionPackMgr::Get().SetUpdatesEnabled(this, needsUpdate);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::BlockPathNodesInternal(bool block)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	for (U32F i = 0; i < kMaxPathNodesPerSide; ++i)
	{
		if (m_enterPathNodeIds[i] == NavPathNodeMgr::kInvalidNodeId)
			continue;

		NavPathNode& node = g_navPathNodeMgr.GetNode(m_enterPathNodeIds[i]);
		node.SetBlockageMask(block ? Nav::kStaticBlockageMaskAll : Nav::kStaticBlockageMaskNone);
	}

	for (U32F i = 0; i < kMaxPathNodesPerSide; ++i)
	{
		if (m_exitPathNodeIds[i] == NavPathNodeMgr::kInvalidNodeId)
			continue;

		NavPathNode& node = g_navPathNodeMgr.GetNode(m_exitPathNodeIds[i]);
		node.SetBlockageMask(block ? Nav::kStaticBlockageMaskAll : Nav::kStaticBlockageMaskNone);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float TraversalActionPack::ComputeBasePathCost() const
{
	float cost = 0.0f;

	if (IsReserved())
	{
		cost += g_navCharOptions.m_traversal.m_reservedExtraCost;
	}

	if (!m_caresAboutBlockingRb && m_hBlockingRigidBody.HandleValid())
	{
		cost += m_rbBlockedExtraPathCost;
	}

	const int resCount = m_unboundedResCount.Get();
	cost += resCount * g_navCharOptions.m_traversal.m_reservedExtraCost;

	return cost;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float TraversalActionPack::ComputeBasePathCostForReserver() const
{
	float cost = 0.0f;

	if (!m_caresAboutBlockingRb && m_hBlockingRigidBody.HandleValid())
	{
		cost += m_rbBlockedExtraPathCost;
	}

	const int resCount = m_unboundedResCount.Get();
	cost += Max(resCount - 1, 0) * g_navCharOptions.m_traversal.m_reservedExtraCost;

	return cost;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::UpdatePathNodeCost()
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	// base cost
	float cost = ComputeBasePathCost();

	// extra cost
	cost += m_extraPathCost;

	// clamp for I8 storage
	cost = Clamp(cost, -127.0f, 127.0f);

	// store to nodes
	for (U32F i = 0; i < kMaxPathNodesPerSide; ++i)
	{
		if (m_enterPathNodeIds[i] != NavPathNodeMgr::kInvalidNodeId)
			g_navPathNodeMgr.GetNode(m_enterPathNodeIds[i]).SetExtraCost(cost);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
TimeFrame TraversalActionPack::UsageTimerRemaining() const
{
	F32 usageTimerRemaining = 0.0f;

	if (Clock* pClock = GetGameplayClock())
	{
		const TimeFrame elapsedTime = pClock->GetTimePassed(m_usageStartTime);

		usageTimerRemaining = Max(m_usageDelay, m_oneShotUsageDelay) - elapsedTime.ToSeconds();
		usageTimerRemaining = Max(usageTimerRemaining, 0.0f);
	}

	return Seconds(usageTimerRemaining);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::Enable(bool enable)
{
	Enable(enable, true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::Enable(bool enable, bool propagateToMutexOwners)
{
	m_propagateToMutexOwners = propagateToMutexOwners;

	if ((m_wantsEnable != enable) || (m_wantsEnable == m_disabled))
	{
		m_wantsEnable = enable;
		m_costDirty	  = true;
		ActionPackMgr::Get().SetUpdatesEnabled(this, NeedsUpdate());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::EnableInternal(bool enable)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	MsgAp("TraversalActionPack::EnableInternal [%s] - %s\n", GetName(), enable ? "Enabling" : "Disabling");

/*
	if (!enable)
	{
		if (Process* pHolder = GetReservationHolder())
		{
			MsgAp("TraversalActionPack::EnableInternal [%s] - Releasing holder %s\n", GetName(), pHolder->GetName());

			Release(pHolder);
			m_briefReserved = false;
		}
	}
*/

	if (m_pMutex && m_propagateToMutexOwners)
	{
		for (I32F ii = 0; ii < m_pMutex->GetOwnerCount(); ++ii)
		{
			if (TraversalActionPack* pTap = static_cast<TraversalActionPack*>(m_pMutex->GetOwner(ii)))
			{
				pTap->ClearNavDestUnsafe();

				if (pTap->GetRegisteredNavLocation().IsValid())
				{
					pTap->m_wantsEnable = enable;
					pTap->ParentClass::Enable(enable);

					if (enable)
					{
						pTap->UpdateNavDestUnsafe();
					}
				}
			}
		}
	}
	else
	{
		ClearNavDestUnsafe();

		if (GetRegisteredNavLocation().IsValid())
		{
			ParentClass::Enable(enable);

			if (enable)
			{
				UpdateNavDestUnsafe();
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::SetExtraPathCost(float newCost)
{
	NAV_ASSERT(Abs(newCost) <= 127.0f);

	if (m_pMutex)
	{
		for (I32F ii = 0; ii < m_pMutex->GetOwnerCount(); ++ii)
		{
			if (TraversalActionPack* pTap = static_cast<TraversalActionPack*>(m_pMutex->GetOwner(ii)))
			{
				pTap->m_extraPathCost = newCost;
			}
		}
	}
	else
	{
		m_extraPathCost = newCost;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::AddFactionId(FactionId faction)
{
	m_factionIdMask |= BuildFactionMask(faction);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::RemoveFactionId(FactionId faction)
{
	m_factionIdMask &= ~BuildFactionMask(faction);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::SetVaultWallHeight(float wallHeight)
{
	m_vaultWallHeight = wallHeight;

	const Vector traversalDeltaLs = GetTraversalDeltaLs();

	if (traversalDeltaLs.Y() > 0.0f)
	{
		m_vaultWallHeight += Abs(traversalDeltaLs.Y());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::AdjustGameTime(TimeDelta delta)
{
	m_usageStartTime += delta;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::LiveUpdate(EntitySpawner& spawner)
{
	NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

	TraversalActionPack::InitParams params;

	// Pass the mutex id to indicate to the callback that this is the reverse half of a two way tap
	if (BuildInitParams(&params,
						&spawner,
						spawner.GetBoundFrame(),
						m_reverseHalfOfTwoWayAp ? m_mutexId : INVALID_STRING_ID_64,
						m_instance))
	{
		m_spawnerSpaceLoc				= params.m_spawnerSpaceLoc;
		m_exitPtLs						= params.m_exitPtLs;
		m_exitOffsetLs					= params.m_exitOffsetLs;
		m_infoId						= params.m_infoId;
		m_directionType					= params.m_directionType;
		m_enterAnimType					= params.m_enterAnimType;
		m_exitAnimType					= params.m_exitAnimType;
		m_mutexId						= params.m_mutexNameId;
		m_factionIdMask					= params.m_factionIdMask;
		m_instance						= params.m_instance;
		m_skillMask						= params.m_skillMask;
		m_tensionModeMask				= params.m_tensionModeMask;
		m_obeyedStaticBlockers			= params.m_obeyedStaticBlockers;
		m_ladderRungSpan				= params.m_ladderRungSpan;
		m_animAdjustWidth				= params.m_animAdjustWidth;
		m_animRotDeg					= params.m_animRotDeg;
		m_disableNearPlayerRadius		= params.m_disableNearPlayerRadius;
		m_extraPathCost					= params.m_extraPathCost;
		m_usageDelay					= params.m_usageDelay;
		m_isVarTap						= params.m_isVarTap;
		m_allowNormalDeaths				= params.m_allowNormalDeaths;
		m_playerBoost					= params.m_playerBoost;
		m_singleUse						= params.m_singleUse;
		m_reverseHalfOfTwoWayAp			= params.m_reverseHalfOfTwoWayAp;
		m_noPush						= params.m_noPush;
		m_fixedAnim						= params.m_fixedAnim;
		m_unboundedReservations			= params.m_unboundedReservations;

		if (IsReverseHalfOfTwoWayAp() && (params.m_reverseAnimAdjustWidth >= 0.0f))
		{
			m_animAdjustWidth = AnimAdjustRange(params.m_reverseAnimAdjustWidth);
		}

		m_desAnimAdjustWidth = m_animAdjustWidth;

		m_fixedToCollision = false;
		m_animAdjustSetup  = false;

		m_regLocType  = GetNavLocTypeFromAnimType(m_enterAnimType);
		m_destNavType = GetNavLocTypeFromAnimType(m_exitAnimType);

		const Locator locWs = spawner.GetWorldSpaceLocator().TransformLocator(m_spawnerSpaceLoc);
		BoundFrame bf = spawner.GetBoundFrame();
		bf.SetLocatorWs(locWs);

		SetBoundFrame(bf);

		SetVaultWallHeight(params.m_vaultWallHeight);

		MsgAp("TraversalActionPack::LiveUpdate [%s]\n", GetName());

		if (IsRegistered())
		{
			UnregisterImmediately();
		}

		NAV_ASSERT(!HavePathNodes());

		RegisterImmediately(m_regParams);

		RefreshNavMeshClearance();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::MoveTap(const BoundFrame& enterBf,
								  DC::VarTapAnimType enterTypeId,
								  const BoundFrame& exitBf,
								  DC::VarTapAnimType exitTypeId)
{
	const Vector traverseDeltaWs = exitBf.GetTranslation() - enterBf.GetTranslation();
	const Vector traverseDeltaLs = enterBf.GetLocator().UntransformVector(traverseDeltaWs);
	const Point exitPtLs		 = kOrigin + traverseDeltaLs + m_exitOffsetLs;

	SetBoundFrame(enterBf);
	m_origLoc = enterBf;

	SetBoundFrameInDestSpace(exitBf);
	SetExitPointLs(exitPtLs);

	m_regLocType = GetNavLocTypeFromAnimType(enterTypeId);
	m_destNavType = GetNavLocTypeFromAnimType(exitTypeId);

	if (IsRegistered())
	{
		RequestUnregistration();
	}

	RequestRegistration(m_regParams);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::SetVarTapOffsetLs(Vector_arg deltaLs)
{
	const Point exitPtLs = kOrigin + deltaLs + m_exitOffsetLs;
	SetExitPointLs(exitPtLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLocation::Type TraversalActionPack::GetSourceNavType() const
{
	return m_regLocType;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLocation::Type TraversalActionPack::GetDestNavType() const
{
	return m_destNavType;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLocation TraversalActionPack::GetSourceNavLocation() const
{
	return m_hRegisteredNavLoc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLocation TraversalActionPack::GetDestNavLocation() const
{
	return m_hDestNavLoc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::IsGroupExactMatch(const DC::VarTapGroup* pGroup) const
{
	AI_ASSERT(pGroup);

	const bool isValid = GetDirectionType() == pGroup->m_direction &&
						 GetEnterAnimType() == pGroup->m_enterAnimType &&
						 GetExitAnimType() == pGroup->m_exitAnimType;

	return isValid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::IsGroupCompatible(const DC::VarTapGroup* pGroup) const
{
	if (!pGroup)
		return false;

	bool valid = GetDirectionType() == pGroup->m_direction;

	valid = valid && VarTapAnimTypesCompatible(GetEnterAnimType(), pGroup->m_enterAnimType);
	valid = valid && VarTapAnimTypesCompatible(GetExitAnimType(), pGroup->m_exitAnimType);

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::CaresAboutPlayerBlockage(const Process* pProcess) const
{
	bool caresAboutPlayerBlockage = ParentClass::CaresAboutPlayerBlockage(pProcess);

	if (const NavCharacter* pNavChar = NavCharacter::FromProcess(pProcess))
	{
		const bool wantsToPushPlayer = m_noPlayerBlockForPushyNpcs ? pNavChar->WantsToPushPlayerAway() : false;

		caresAboutPlayerBlockage = caresAboutPlayerBlockage && !wantsToPushPlayer;
	}

	return caresAboutPlayerBlockage;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::HasNavMeshClearance(const NavLocation& navLoc,
											  bool debugDraw /* = false */,
											  DebugPrimTime tt /* = kPrimDuration1FramePauseable */) const
{
	if (!ParentClass::HasNavMeshClearance(navLoc, debugDraw, tt))
	{
		return false;
	}

	if (const ActionPack* pRevAp = GetReverseActionPack())
	{
		const NavLocation revNavLoc = pRevAp->GetRegisteredNavLocation();
		if (!pRevAp->ParentClass::HasNavMeshClearance(revNavLoc, debugDraw, tt))
		{
			return false;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::IsReservedByInternal(const Process* pProcess) const
{
	if (m_pMutex)
	{
		if (m_pMutex->IsUser(pProcess))
		{
			return true;
		}

		const bool enabledApIsMe = (m_pMutex->GetEnabledActionPack() == this);
		const bool hasUsers = m_pMutex->GetUserCount() > 0;
		const bool enabledApIsValid = enabledApIsMe || !hasUsers;

		if (enabledApIsValid && (m_unboundedResCount.Get() > 0))
		{
			return true;
		}
	}
	else if (m_unboundedResCount.Get() > 0)
	{
		return true;
	}

	return ParentClass::IsReservedByInternal(pProcess);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::IsAvailableInternal(bool caresAboutPlayerBlockage /* = true */) const
{
	return IsAvailableCommon(nullptr, caresAboutPlayerBlockage);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::IsAvailableForInternal(const Process* pProcess) const
{
	const bool caresAboutPlayerBlockage = CaresAboutPlayerBlockage(pProcess);
	return IsAvailableCommon(pProcess, caresAboutPlayerBlockage);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::IsAvailableCommon(const Process* pProcess, bool caresAboutPlayerBlockage) const
{
	if (ActionPackMgr::Get().IsPendingUnregistration(m_mgrId))
	{
		return false;
	}

	if (!IsEnabled())
	{
		return false;
	}

	if (caresAboutPlayerBlockage && IsPlayerBlocked())
	{
		return false;
	}

	// if we're already holding the reservation then the subsequent tests (like usage timer) shouldn't matter
	const bool reservedByProc = pProcess && IsReservedByInternal(pProcess);
	if (reservedByProc)
	{
		return true;
	}

	if (pProcess && m_briefReserved && (m_hLastReservationHolder.GetProcessId() == pProcess->GetProcessId()))
	{
		return true;
	}

	if (IsBlockedByRigidBody())
	{
		return false;
	}

	if (m_pMutex)
	{
		if (!m_pMutex->IsAvailable(this, pProcess))
		{
			return false;
		}

		const bool enabledApIsMe = (m_pMutex->GetEnabledActionPack() == this);
		const bool hasUsers = m_pMutex->GetUserCount() > 0;
		const bool enabledApIsValid = enabledApIsMe || !hasUsers;

		if ((m_unboundedResCount.Get() > 0) && !enabledApIsValid)
		{
			return false;
		}
	}

	// Current reservation-holder doesn't need to check usage timer since it must have expired before it was reserved
	if (!HasUsageTimerExpired())
	{
		return false;
	}

	if (!HasUnboundedReservations())
	{
		if (IsReserved())
		{
			return false;
		}

		// Check if the last reservation holder is blocking the exit
		if (const NavCharacter* pLastNavChar = NavCharacter::FromProcess(m_hLastReservationHolder.ToProcess()))
		{
			if (!pLastNavChar->IsDead() && pProcess != pLastNavChar)
			{
				const Point navPosPs = pLastNavChar->GetNavigationPosPs();
				const Point navPosWs = pLastNavChar->GetParentSpace().TransformPoint(navPosPs);

				if (DistSqr(GetExitPointWs(), navPosWs) < Sqr(0.5f))
				{
					return false;
				}
			}
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::ReserveInternal(Process* pProcess, TimeFrame timeout /* = Seconds(0.0f) */)
{
	bool result = false;

	if (IsAvailableForInternal(pProcess))
	{
		bool doIt = true;

		if (m_pMutex)
		{
			if (m_pMutex->TryEnable(this, pProcess))
			{
				NAV_ASSERT(m_pMutex->GetEnabledActionPack() == this);
			}
			else
			{
				doIt = false;
			}
		}

		if (doIt)
		{
			if (HasUnboundedReservations())
			{
				MsgAp("TraversalActionPack::ReserveInternal (unbounded) [%s] '%s' %d\n",
					  GetName(),
					  pProcess ? pProcess->GetName() : "<null>",
					  m_unboundedResCount.Get());

				m_unboundedResCount.Add(1);
				result = true;
			}
			else
			{
				// sanity in case we debug toggle mid-reserve
				m_unboundedResCount.Set(0);

				result = ParentClass::ReserveInternal(pProcess, timeout);
			}
		}
	}

	m_costDirty |= result;
	ActionPackMgr::Get().SetUpdatesEnabled(this, NeedsUpdate());

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::Reserve(Process* pProcess, TimeFrame timeout /* = Seconds(0.0f) */)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	const bool result = ReserveInternal(pProcess, timeout);

	if (result)
	{
		m_briefReserved = false;
		m_hLastReservationHolder = m_hReservationHolder;
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::Release(const Process* pProcess)
{
	m_costDirty		= IsReserved();
	m_briefReserved = false;

	ActionPackMgr::Get().SetUpdatesEnabled(this, NeedsUpdate());

	if (HasUnboundedReservations())
	{
		MsgAp("TraversalActionPack::Release (unbounded) [%s] '%s' %d\n",
			  GetName(),
			  pProcess ? pProcess->GetName() : "<null>",
			  m_unboundedResCount.Get());

		m_unboundedResCount.Sub(1);
		m_costDirty = true;
	}
	else
	{
		// sanity in case we debug toggle mid-reserve
		m_unboundedResCount.Set(0);

		ParentClass::Release(pProcess);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Doors should not do brief reservation hokey, but at least behind a human readable query
bool TraversalActionPack::ShouldDoBriefReserve() const
{
	// Except doors need to not block their reservation holders, which is handled by IsAvailable...
	//if (GetProcess())
	//{
	//	return false;
	//}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// A brief reserve means we aren't going to hold onto the reservation for very long, just
// long enough to enter the TAP and continue on. Therefore we don't block the path nodes.
bool TraversalActionPack::BriefReserve(Process* pProcess)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	const bool result = ReserveInternal(pProcess);

	if (result)
	{
		m_briefReserved = true;
		m_hLastReservationHolder = m_hReservationHolder;
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::IsBriefReserved() const
{
	return m_briefReserved && IsReserved();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::HasReachedOccupancyLimit(const Process* pProcess) const
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	bool reachedLimit = false;

	if (const NavMesh* pDestNavMesh = m_hDestNavLoc.ToNavMesh())
	{
		const NavMesh* pRegMesh = m_hRegisteredNavLoc.ToNavMesh();
		if (pDestNavMesh != pRegMesh)
		{
			const NavCharacter* pNavChar = NavCharacter::FromProcess(pProcess);
			const NavMesh* pNavMesh = pNavChar ? pNavChar->GetNavLocation().ToNavMesh() : nullptr;

			if (pNavMesh != pDestNavMesh)
			{
				reachedLimit = pDestNavMesh->AtMaxOccupancyCount();
			}
		}
	}

	return reachedLimit;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::ResetReadyToUseTimer()
{
	m_usageStartTime = GetGameplayClock()->GetCurTime();
	m_oneShotUsageDelay = 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::AddPathNodes()
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	for (U32F i = 0; i < kMaxPathNodesPerSide; ++i)
	{
		NAV_ASSERT(m_enterPathNodeIds[i] == NavPathNodeMgr::kInvalidNodeId);
		NAV_ASSERT(m_exitPathNodeIds[i] == NavPathNodeMgr::kInvalidNodeId);
	}

	const NavLocation srcNavLoc = GetSourceNavLocation();
	const NavLocation dstNavLoc = GetDestNavLocation();

	// NB: Explicitly do *not* use ComputePathNodePosPs() here because we might've fixed to collision
	// and if our path node position isn't contained by our actual nav poly, then we we patch the nav poly
	// we wont be able to find the right NavPolyEx to contain the TAP link (because none of them will!)
	const Point srcPosPs = srcNavLoc.GetPosPs();
	const Point dstPosPs = dstNavLoc.GetPosPs();

	if (srcNavLoc.IsValid() && dstNavLoc.IsValid())
	{
		Point posPs[2] = { srcPosPs, dstPosPs };
		bool failed = false;

		if (m_enterPathNodeIds[0] == NavPathNodeMgr::kInvalidNodeId)
		{
			m_enterPathNodeIds[0] = g_navPathNodeMgr.AllocateNode();
		}

		if (m_exitPathNodeIds[0] == NavPathNodeMgr::kInvalidNodeId)
		{
			m_exitPathNodeIds[0] = g_navPathNodeMgr.AllocateNode();
		}

		if (!HavePathNodes())
		{
			for (U32F i = 0; i < kMaxPathNodesPerSide; ++i)
			{
				g_navPathNodeMgr.TryFreeNode(m_enterPathNodeIds[i]);
				g_navPathNodeMgr.TryFreeNode(m_exitPathNodeIds[i]);

				m_enterPathNodeIds[i] = NavPathNodeMgr::kInvalidNodeId;
				m_exitPathNodeIds[i] = NavPathNodeMgr::kInvalidNodeId;
			}
			return;
		}

		const U32F pathNodeIds[2] = { m_enterPathNodeIds[0], m_exitPathNodeIds[0] };

		for (U32F ii = 0; ii < 2; ++ii)
		{
			NAV_ASSERT(pathNodeIds[ii] != NavPathNodeMgr::kInvalidNodeId);

			NavPathNode& node = g_navPathNodeMgr.GetNode(pathNodeIds[ii]);
			node.AsActionPack(this,
							  posPs[ii],
							  (ii == 0) ? srcNavLoc : dstNavLoc,
							  m_extraPathCost,
							  (ii != 0));

			node.SetBlockageMask(IsEnabled() ? Nav::kStaticBlockageMaskNone : Nav::kStaticBlockageMaskAll);
		}

		const U32F srcPathNodeId = srcNavLoc.GetPathNodeId();
		const U32F dstPathNodeId = dstNavLoc.GetPathNodeId();

		g_navPathNodeMgr.AddLinkSafe(srcPathNodeId, pathNodeIds[0], srcPosPs, srcPosPs, NavPathNode::kLinkTypeOutgoing);
		g_navPathNodeMgr.AddLinkSafe(pathNodeIds[0], pathNodeIds[1], dstPosPs, dstPosPs, NavPathNode::kLinkTypeOutgoing);
		g_navPathNodeMgr.AddLinkSafe(pathNodeIds[1], dstPathNodeId, dstPosPs, dstPosPs, NavPathNode::kLinkTypeOutgoing);

		g_navPathNodeMgr.AddLinkSafe(dstPathNodeId, pathNodeIds[1], dstPosPs, dstPosPs, NavPathNode::kLinkTypeIncoming);
		g_navPathNodeMgr.AddLinkSafe(pathNodeIds[1], pathNodeIds[0], srcPosPs, srcPosPs, NavPathNode::kLinkTypeIncoming);
		g_navPathNodeMgr.AddLinkSafe(pathNodeIds[0], srcPathNodeId, srcPosPs, srcPosPs, NavPathNode::kLinkTypeIncoming);

		MsgAp("TraversalActionPack::AddPathNodes [%s] [enter: %d] [exit: %d]\n",
			  GetName(),
			  m_enterPathNodeIds[0],
			  m_exitPathNodeIds[0]);

		AddExtraPathNodes();

		g_navPathNodeMgr.Validate();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::AddExtraPathNodes()
{
	if (!m_animAdjustSetup || m_addedExtraPathNodes || !HavePathNodes())
	{
		return;
	}

	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	const NavLocation srcNavLoc = GetSourceNavLocation();
	const NavLocation dstNavLoc = GetDestNavLocation();

	const NavMesh* pSrcMesh = nullptr;
	const NavPoly* pSrcPoly = srcNavLoc.ToNavPoly(&pSrcMesh);

	const NavMesh* pDstMesh = nullptr;
	const NavPoly* pDstPoly = dstNavLoc.ToNavPoly(&pDstMesh);

	ExtraPathNodes srcGatherData;
	srcGatherData.m_ignoreNode = pSrcPoly ? pSrcPoly->GetPathNodeId() : NavPathNodeMgr::kInvalidNodeId;
	ExtraPathNodes dstGatherData;
	dstGatherData.m_ignoreNode = pDstPoly ? pDstPoly->GetPathNodeId() : NavPathNodeMgr::kInvalidNodeId;

	NavMesh::ProbeParams params;
	params.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskNone;
	params.m_crossLinks = false;

	switch (m_animAdjustType)
	{
	case AnimAdjustType::kLinear:
		if (pSrcPoly)
		{
			const Point srcPosWs = srcNavLoc.GetPosWs();
			const Locator startLocWs = GetLocatorWs();
			const Vector startProbeDirWs = GetLocalX(startLocWs.Rot());

			params.m_pStartPoly = pSrcPoly;
			params.m_start		= pSrcMesh->WorldToLocal(srcPosWs);

			params.m_move = pSrcMesh->WorldToLocal(startProbeDirWs * -1.0f * m_animAdjustWidth.m_min);
			pSrcMesh->WalkPolysInLineLs(params, WalkGatherPathNodes, (uintptr_t)&srcGatherData);

			params.m_move = pSrcMesh->WorldToLocal(startProbeDirWs * m_animAdjustWidth.m_max);
			pSrcMesh->WalkPolysInLineLs(params, WalkGatherPathNodes, (uintptr_t)&srcGatherData);
		}

		if (pDstPoly)
		{
			const Point dstPosWs = dstNavLoc.GetPosWs();
			const Locator destLocWs = GetBoundFrameInDestSpace().GetLocatorWs();
			const Vector destProbeDirWs = GetLocalX(destLocWs.Rot());

			params.m_pStartPoly = pDstPoly;
			params.m_start = pDstMesh->WorldToLocal(dstPosWs);

			params.m_move = pDstMesh->WorldToLocal(destProbeDirWs * -1.0f * m_animAdjustWidth.m_min);
			pDstMesh->WalkPolysInLineLs(params, WalkGatherPathNodes, (uintptr_t)&dstGatherData);

			params.m_move = pDstMesh->WorldToLocal(destProbeDirWs * m_animAdjustWidth.m_max);
			pDstMesh->WalkPolysInLineLs(params, WalkGatherPathNodes, (uintptr_t)&dstGatherData);
		}
		break;

	case AnimAdjustType::kEdge:
		if (m_animEdgeRef.IsGood() && m_edgeApPathPs.IsValid())
		{
			const I32F numWaypoints = m_edgeApPathPs.GetWaypointCount();

			for (I32F i = 0; i < numWaypoints - 1; ++i)
			{
				const Point edgePos0Ps = m_edgeApPathPs.GetWaypoint(i);
				const Point edgePos1Ps = m_edgeApPathPs.GetWaypoint(i + 1);

				const Locator apEdgeLoc0Ps = RecreateEdgeLocFromPathLegPs(m_edgeApPathPs, i, m_edgeToApFwLs, edgePos0Ps);
				const Locator apEdgeLoc1Ps = Locator(edgePos1Ps, apEdgeLoc0Ps.Rot());

				const Point enterPos0Ps = apEdgeLoc0Ps.TransformPoint(m_regPtLs);
				const Point exitPos0Ps	= apEdgeLoc0Ps.TransformPoint(m_exitPtLs);
				const Point enterPos1Ps = apEdgeLoc1Ps.TransformPoint(m_regPtLs);
				const Point exitPos1Ps	= apEdgeLoc1Ps.TransformPoint(m_exitPtLs);

				if (pSrcMesh)
				{
					params.m_start = pSrcMesh->ParentToLocal(enterPos0Ps);
					params.m_move  = pSrcMesh->ParentToLocal(enterPos1Ps) - params.m_start;

					pSrcMesh->WalkPolysInLineLs(params, WalkGatherPathNodes, (uintptr_t)&srcGatherData);
				}

				if (pDstMesh)
				{
					params.m_start = pDstMesh->ParentToLocal(exitPos0Ps);
					params.m_move  = pDstMesh->ParentToLocal(exitPos1Ps) - params.m_start;
					pDstMesh->WalkPolysInLineLs(params, WalkGatherPathNodes, (uintptr_t)&dstGatherData);
				}
			}
		}
		break;
	}

	const bool unblocked = IsEnabled() && !m_navBlocked;

	for (U32F iFoundSrc = 0; iFoundSrc < srcGatherData.m_numFoundPathNodes; ++iFoundSrc)
	{
		NAV_ASSERT(m_enterPathNodeIds[iFoundSrc + 1] == NavPathNodeMgr::kInvalidNodeId);

		const U32F srcNodeId = srcGatherData.m_foundPathNodes[iFoundSrc];
		const NavPathNode& srcNode = g_navPathNodeMgr.GetNode(srcNodeId);

		const Point srcPosPs = ComputePathNodePosPs(srcNode, true);

		const U32F newEnterNodeId = g_navPathNodeMgr.AllocateNode();
		m_enterPathNodeIds[iFoundSrc + 1] = newEnterNodeId;

		if (newEnterNodeId == NavPathNodeMgr::kInvalidNodeId)
			continue;

		NavPathNode& newEnterNode = g_navPathNodeMgr.GetNode(newEnterNodeId);
		newEnterNode.AsActionPack(this, srcPosPs, srcNode.GetNavManagerId(), m_extraPathCost, false);
		newEnterNode.SetBlockageMask(unblocked ? Nav::kStaticBlockageMaskNone : Nav::kStaticBlockageMaskAll);

		g_navPathNodeMgr.AddLinkSafe(srcNodeId, newEnterNodeId, srcPosPs, srcPosPs, NavPathNode::kLinkTypeOutgoing);
		g_navPathNodeMgr.AddLinkSafe(newEnterNodeId, srcNodeId, srcPosPs, srcPosPs, NavPathNode::kLinkTypeIncoming);
	}

	for (U32F iFoundDst = 0; iFoundDst < dstGatherData.m_numFoundPathNodes; ++iFoundDst)
	{
		NAV_ASSERT(m_exitPathNodeIds[iFoundDst + 1] == NavPathNodeMgr::kInvalidNodeId);

		const U32F dstNodeId	   = dstGatherData.m_foundPathNodes[iFoundDst];
		const NavPathNode& dstNode = g_navPathNodeMgr.GetNode(dstNodeId);

		const Point dstPosPs = ComputePathNodePosPs(dstNode, false);

		const U32F newExitNodeId = g_navPathNodeMgr.AllocateNode();
		m_exitPathNodeIds[iFoundDst + 1] = newExitNodeId;

		if (newExitNodeId == NavPathNodeMgr::kInvalidNodeId)
			continue;

		NavPathNode& newExitNode = g_navPathNodeMgr.GetNode(newExitNodeId);
		newExitNode.AsActionPack(this, dstPosPs, dstNode.GetNavManagerId(), m_extraPathCost, true);
		newExitNode.SetBlockageMask(unblocked ? Nav::kStaticBlockageMaskNone : Nav::kStaticBlockageMaskAll);

		g_navPathNodeMgr.AddLinkSafe(newExitNodeId, dstNodeId, dstPosPs, dstPosPs, NavPathNode::kLinkTypeOutgoing);
		g_navPathNodeMgr.AddLinkSafe(dstNodeId, newExitNodeId, dstPosPs, dstPosPs, NavPathNode::kLinkTypeIncoming);
	}

	for (U32F iFoundSrc = 0; iFoundSrc < srcGatherData.m_numFoundPathNodes; ++iFoundSrc)
	{
		const U32F newEnterNodeId = m_enterPathNodeIds[iFoundSrc + 1];

		if (newEnterNodeId == NavPathNodeMgr::kInvalidNodeId)
			continue;

		const NavPathNode& newEnterNode = g_navPathNodeMgr.GetNode(newEnterNodeId);
		const Point srcPosPs = newEnterNode.GetPositionPs();

		for (U32F iFoundDst = 0; iFoundDst < dstGatherData.m_numFoundPathNodes; ++iFoundDst)
		{
			const U32F newExitNodeId = m_exitPathNodeIds[iFoundDst + 1];
			if (newExitNodeId == NavPathNodeMgr::kInvalidNodeId)
				continue;

			const NavPathNode& newExitNode = g_navPathNodeMgr.GetNode(newExitNodeId);
			const Point dstPosPs = newExitNode.GetPositionPs();

			g_navPathNodeMgr.AddLinkSafe(newEnterNodeId,
										 newExitNodeId,
										 dstPosPs,
										 dstPosPs,
										 NavPathNode::kLinkTypeOutgoing);
			g_navPathNodeMgr.AddLinkSafe(newExitNodeId,
										 newEnterNodeId,
										 srcPosPs,
										 srcPosPs,
										 NavPathNode::kLinkTypeIncoming);
		}
	}

	m_addedExtraPathNodes = true;

	if (FALSE_IN_FINAL_BUILD((srcGatherData.m_numFoundPathNodes > 0) || (dstGatherData.m_numFoundPathNodes > 0)))
	{
		MsgAp("TraversalActionPack::AddExtraPathNodes [%s]\n", GetName());
		if (srcGatherData.m_numFoundPathNodes > 0)
		{
			MsgAp("    Enter Nodes: ");
			for (U32F i = 1; i < kMaxPathNodesPerSide; ++i)
			{
				if (m_enterPathNodeIds[i] != NavPathNodeMgr::kInvalidNodeId)
				{
					MsgAp("%d ", m_enterPathNodeIds[i]);
				}
			}
			MsgAp("\n");
		}
		if (dstGatherData.m_numFoundPathNodes > 0)
		{
			MsgAp("    Exit Nodes: ", GetName());
			for (U32F i = 1; i < kMaxPathNodesPerSide; ++i)
			{
				if (m_exitPathNodeIds[i] != NavPathNodeMgr::kInvalidNodeId)
				{
					MsgAp("%d ", m_exitPathNodeIds[i]);
				}
			}
			MsgAp("\n");
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::RemovePathNodes()
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	if (FALSE_IN_FINAL_BUILD(true))
	{
		MsgAp("TraversalActionPack::RemovePathNodes [%s]\n", GetName());
		MsgAp("    Enter Nodes: ");
		for (U32F i = 0; i < kMaxPathNodesPerSide; ++i)
		{
			if (m_enterPathNodeIds[i] != NavPathNodeMgr::kInvalidNodeId)
			{
				MsgAp("%d ", m_enterPathNodeIds[i]);
			}
		}
		MsgAp("\n");
		MsgAp("    Exit Nodes: ", GetName());
		for (U32F i = 0; i < kMaxPathNodesPerSide; ++i)
		{
			if (m_exitPathNodeIds[i] != NavPathNodeMgr::kInvalidNodeId)
			{
				MsgAp("%d ", m_exitPathNodeIds[i]);
			}
		}
		MsgAp("\n");
	}

	for (U32F i = 0; i < kMaxPathNodesPerSide; ++i)
	{
		if (m_enterPathNodeIds[i] != NavPathNodeMgr::kInvalidNodeId)
		{
			const NavPathNode& enterNode = g_navPathNodeMgr.GetNode(m_enterPathNodeIds[i]);
			NAV_ASSERT(enterNode.IsActionPackNode());
			NAV_ASSERT(enterNode.GetActionPackHandle().GetMgrId() == GetMgrId());
			g_navPathNodeMgr.RemoveNode(m_enterPathNodeIds[i]);
		}
		if (m_exitPathNodeIds[i] != NavPathNodeMgr::kInvalidNodeId)
		{
			const NavPathNode& exitNode = g_navPathNodeMgr.GetNode(m_exitPathNodeIds[i]);
			NAV_ASSERT(exitNode.IsActionPackNode());
			NAV_ASSERT(exitNode.GetActionPackHandle().GetMgrId() == GetMgrId());
			g_navPathNodeMgr.RemoveNode(m_exitPathNodeIds[i]);
		}
	}

	g_navPathNodeMgr.Validate();

	for (U32F i = 0; i < kMaxPathNodesPerSide; ++i)
	{
		m_enterPathNodeIds[i] = NavPathNodeMgr::kInvalidNodeId;
		m_exitPathNodeIds[i]  = NavPathNodeMgr::kInvalidNodeId;
	}

	m_addedExtraPathNodes = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::HavePathNodes() const
{
	return (m_enterPathNodeIds[0] != NavPathNodeMgr::kInvalidNodeId)
		   && (m_exitPathNodeIds[0] != NavPathNodeMgr::kInvalidNodeId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point TraversalActionPack::GetDefaultEntryPointWs(Scalar_arg offset) const
{
	// for traversal action packs, the entry point is the same as the registration point
	const Locator locWs = m_loc.GetLocator();
	Point regPtLs = m_regPtLs;

	return locWs.TransformPoint(regPtLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point TraversalActionPack::GetDefaultEntryPointPs(Scalar_arg offset) const
{
	// for traversal action packs, the entry point is the same as the registration point
	const Locator locPs = m_loc.GetLocatorPs();
	Point regPtLs = m_regPtLs;

	return locPs.TransformPoint(regPtLs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame TraversalActionPack::GetDefaultEntryBoundFrame(Scalar_arg offset) const
{
	// for traversal action packs, the entry point is the same as the registration point
	BoundFrame apRef = GetBoundFrame();
	Vector regVtLs = GetRegistrationPointLs() - kOrigin;

	Vector regVtWs = apRef.GetLocatorWs().TransformVector(regVtLs);
	apRef.AdjustTranslationWs(regVtWs);

	return apRef;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator TraversalActionPack::GetApAnimAdjustLs(Point_arg charPosWs) const
{
	Locator adjustLs = kIdentity;

	switch (m_animAdjustType)
	{
	case AnimAdjustType::kLinear:
		{
			const BoundFrame& baseAp = GetBoundFrame();
			const Point charPosLs = baseAp.GetLocatorWs().UntransformPoint(charPosWs);

			const Scalar constrainedX = Limit(charPosLs.X(), -m_animAdjustWidth.m_min, m_animAdjustWidth.m_max);
			adjustLs.SetPos(Point(constrainedX, 0.0f, 0.0f));
		}
		break;

	case AnimAdjustType::kEdge:
		if (m_animEdgeRef.IsGood() && m_edgeApPathPs.IsValid())
		{
			const BoundFrame& baseAp = GetBoundFrame();
			const Locator& parentSpace = baseAp.GetParentSpace();
			const Point charPosPs = parentSpace.UntransformPoint(charPosWs);
			I32F closestLeg = -1;
			const Point apPosPs = m_edgeApPathPs.ClosestPointXz(charPosWs, nullptr, &closestLeg);

			if (closestLeg >= 0)
			{
				const Locator apRefPs = RecreateEdgeLocFromPathLegPs(m_edgeApPathPs, closestLeg, m_edgeToApFwLs, apPosPs);
				adjustLs = baseAp.GetLocatorPs().UntransformLocator(apRefPs);
			}
		}
		break;
	}

	return adjustLs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator TraversalActionPack::GetApAnimAdjustLs(Point_arg pathPosAWs, Point_arg pathPosBWs) const
{
	if (FALSE_IN_FINAL_BUILD(g_navCharOptions.m_traversal.m_disableApAnimAdjust))
	{
		return kIdentity;
	}

	Locator adjustLs = kIdentity;

	const BoundFrame& baseAp = GetBoundFrame();
	const Locator baseApWs = baseAp.GetLocatorWs();

	const Point pathPosALs = baseApWs.UntransformPoint(pathPosAWs);
	const Point pathPosBLs = baseApWs.UntransformPoint(pathPosBWs);

	switch (m_animAdjustType)
	{
	case AnimAdjustType::kLinear:
		{
			const Segment tapSegLs = Segment(Point(-m_animAdjustWidth.m_min, 0.0f, 0.0f),
											 Point(m_animAdjustWidth.m_max, 0.0f, 0.0f));

			const Segment pathSegLs = Segment(pathPosALs, pathPosBLs);
			Scalar t0, t1;

			IntersectSegmentSegmentXz(pathSegLs, tapSegLs, t0, t1);

			const float tapTT = Limit01(t1);
			const Point closestPosLs = Lerp(tapSegLs, tapTT);

			adjustLs.SetPos(closestPosLs);
		}
		break;

	case AnimAdjustType::kEdge:
		if (m_animEdgeRef.IsGood() && m_edgeApPathPs.IsValid())
		{
			const Locator& parentSpace = baseAp.GetParentSpace();
			const Point pathPosAPs = parentSpace.UntransformPoint(pathPosAWs);
			const Point pathPosBPs = parentSpace.UntransformPoint(pathPosBWs);

			const Segment pathSegBs = Segment(pathPosAPs, pathPosBPs);

			Point closestEdgePosPs;
			m_edgeApPathPs.IntersectSegmentXz(pathSegBs, &closestEdgePosPs);

			const Point closestEdgePosLs = baseAp.GetLocatorPs().UntransformPoint(closestEdgePosPs);

			adjustLs.SetPos(closestEdgePosLs);
		}
		break;
	}

	const float maxRotDeg = GetAnimRotDeg();
	const float xzDist = DistXz(pathPosAWs, pathPosBWs);

	if ((maxRotDeg > NDI_FLT_EPSILON) && (xzDist > NDI_FLT_EPSILON))
	{
		const Point defEntryLs = m_regPtLs;
		const Point defExitLs  = m_exitPtLs;

		const Vector natVecLs = AsUnitVectorXz(defExitLs - defEntryLs, kZero);
		const Vector desVecLs = AsUnitVectorXz(pathPosBLs - pathPosALs, natVecLs);

		const Quat rotLs = QuatFromVectors(natVecLs, desVecLs);

		const Quat constrainedRotLs = LimitQuatAngle(rotLs, DEGREES_TO_RADIANS(maxRotDeg));

		adjustLs.SetRot(constrainedRotLs);
	}

	return adjustLs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point TraversalActionPack::ComputePathNodePosPs(const NavPathNode& node, bool forEnter) const
{
	const Point posPs = node.GetPositionPs();
	Point retPs = posPs;

	switch (m_animAdjustType)
	{
	case AnimAdjustType::kLinear:
		{
			const Locator locPs = GetLocatorPs();
			const Point posLs = forEnter ? m_regPtLs : m_exitPtLs;

			const Point posMinLs = posLs + Vector(-m_animAdjustWidth.m_min, 0.0f, 0.0f);
			const Point posMaxLs = posLs + Vector(m_animAdjustWidth.m_max, 0.0f, 0.0f);
			const Point posMinPs = locPs.TransformPoint(posMinLs);
			const Point posMaxPs = locPs.TransformPoint(posMaxLs);

			DistPointSegment(posPs, posMinPs, posMaxPs, &retPs);
		}
		break;

	case AnimAdjustType::kEdge:
		if (m_animEdgeRef.IsGood() && m_edgeApPathPs.IsValid())
		{
			const Point posLs = forEnter ? m_regPtLs : m_exitPtLs;

			I32F iEdgeLeg = 0;
			const Point apPosPs = m_edgeApPathPs.ClosestPoint(posPs, nullptr, &iEdgeLeg);

			const Locator apRefPs = RecreateEdgeLocFromPathLegPs(m_edgeApPathPs, iEdgeLeg, m_edgeToApFwLs, apPosPs);

			retPs = apRefPs.TransformPoint(posLs);
		}
		break;
	}

	if (node.IsNavMeshNode())
	{
		const NavMesh* pNodeMesh = nullptr;
		if (const NavPoly* pNodePoly = node.GetNavPolyHandle().ToNavPoly(&pNodeMesh))
		{
			const Point searchLs = pNodeMesh->ParentToLocal(retPs);
			Point retLs = searchLs;
			pNodePoly->FindNearestPointXzLs(&retLs, searchLs);

			retPs = pNodeMesh->LocalToParent(searchLs);
		}
	}

	return retPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::SetupAnimAdjust()
{
	const Level* pLevel = GetAllocLevel();
	if (pLevel && !pLevel->IsLoaded())
		return;

	// HACK HACK HACK -- for some reason on package only the bottom half of this TAP tries to affix itself to collision before the top half of the TAP is registered
	// The patch is due today so let's just try again once the reverse tap exists
	if (m_spawnerId == SID("tap-accessibility-jump-268") && GetReverseActionPack() == nullptr)
	{
		return;
	}

	if (!IsFixedToCollision() && (m_animAdjustType != AnimAdjustType::kNone))
	{
		FixToCollision(this);
	}

	switch (m_animAdjustType)
	{
	case AnimAdjustType::kLinear:
		{
			m_animAdjustWidth = DetermineAnimRangeLinear(m_desAnimAdjustWidth);
		}
		break;

	case AnimAdjustType::kEdge:
		{
			const Point searchPosWs = GetAnimAdjustTestPosWs();

			ScopedTempAllocator jj(FILE_LINE_FUNC);
			ListArray<EdgeInfo> edges(512);

			const I32F numEdges = FindEdges(searchPosWs, &edges, nullptr, 0.5f);

			Scalar bestCost = kLargestFloat;
			I32F bestIndex = -1;
			Point bestEdgePosWs = searchPosWs;

			for (U32F iEdge = 0; iEdge < numEdges; ++iEdge)
			{
				const EdgeInfo& edgeInfo = edges.At(iEdge);

				Point edgePosWs = searchPosWs;
				const Scalar edgeDist = DistPointSegment(searchPosWs,
														 edgeInfo.GetVert0(),
														 edgeInfo.GetVert1(),
														 &edgePosWs);

				if (edgeDist < bestCost)
				{
					bestIndex = iEdge;
					bestCost = edgeDist;
					bestEdgePosWs = edgePosWs;
				}
			}

			if (bestIndex >= 0)
			{
				m_animEdgeRef = edges[bestIndex];

				const AnimAdjustRange desWidth = m_animAdjustWidth;
				const AnimAdjustRange actWidth = DetermineAnimRangeEdge(desWidth,
																		m_animEdgeRef,
																		GetBoundFrame(),
																		GetRegisteredNavLocation(),
																		GetDestNavLocation(),
																		GetRegistrationPointWs(),
																		GetExitPointWs(),
																		&m_edgeApPathPs);
				m_animAdjustWidth = actWidth;

				m_edgeToApFwLs = QuatFromVectors(VectorXz(m_animEdgeRef.GetVert0() - m_animEdgeRef.GetVert1()),
												 GetLocalZ(GetBoundFrame().GetRotationWs()));
			}
			else
			{
				m_animEdgeRef = FeatureEdgeReference();
				m_edgeToApFwLs = kIdentity;
			}
		}
		break;

	default:
		m_animAdjustWidth = AnimAdjustRange(0.0f);
		break;
	}

	m_animAdjustSetup = true;

	AddExtraPathNodes();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point TraversalActionPack::GetAnimAdjustTestPosWs() const
{
	const BoundFrame& boundFrame = GetBoundFrame();

	Point posWs = boundFrame.GetTranslationWs();

	Maybe<BoundFrame> maybeV1 = GetAnchorLocator(SID("apReference-vault-1"), 0.0f);

	if (maybeV1.Valid())
	{
		posWs = maybeV1.Get().GetTranslationWs();
	}
	else if (IsJumpUp())
	{
		posWs = GetVarTapEndBoundFrame().GetTranslation();
	}

	return posWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
TraversalActionPack::AnimAdjustRange TraversalActionPack::DoAnimProbes(const AnimAdjustRange& desRange,
																	   const Vector probeDirPs,
																	   const NavLocation& navLoc,
																	   Nav::StaticBlockageMask obeyedStaticBlockers,
																	   bool debugDraw /* = false */,
																	   DebugPrimTime tt /* = kPrimDuration1FrameAuto */)
{
	TraversalActionPack::AnimAdjustRange ret = desRange;

	switch (navLoc.GetType())
	{
	case NavLocation::Type::kNavPoly:
		if (const NavPoly* pNavPoly = navLoc.ToNavPoly())
		{
			const Point probePosPs = navLoc.GetPosPs();
			const NavMesh* pNavMesh = pNavPoly->GetNavMesh();
			NAV_ASSERT(pNavMesh);

			NavMesh::ProbeParams params;
			params.m_pStartPoly = pNavPoly;
			params.m_start = pNavMesh->ParentToLocal(probePosPs);
			params.m_obeyedStaticBlockers = obeyedStaticBlockers;
			params.m_probeRadius = kAnimRangeProbeRadius;

			params.m_move = pNavMesh->ParentToLocal(probeDirPs * -1.0f * desRange.m_min);
			{
				const NavMesh::ProbeResult lRes = pNavMesh->ProbeLs(&params);

				switch (lRes)
				{
				case NavMesh::ProbeResult::kErrorStartedOffMesh:
				case NavMesh::ProbeResult::kErrorStartNotPassable:
					ret.m_min = 0.0f;
					break;
				case NavMesh::ProbeResult::kHitEdge:
					ret.m_min = Min(ret.m_min, Dist(params.m_endPoint, params.m_start));
					break;

				case NavMesh::ProbeResult::kReachedGoal:
				default:
					break;
				}

				if (FALSE_IN_FINAL_BUILD(debugDraw))
				{
					Nav::DebugDrawProbeResultLs(pNavMesh, lRes, params, tt);
				}
			}

			params.m_move = pNavMesh->ParentToLocal(probeDirPs * desRange.m_max);
			{
				const NavMesh::ProbeResult rRes = pNavMesh->ProbeLs(&params);

				switch (rRes)
				{
				case NavMesh::ProbeResult::kErrorStartedOffMesh:
				case NavMesh::ProbeResult::kErrorStartNotPassable:
					ret.m_max = 0.0f;
					break;
				case NavMesh::ProbeResult::kHitEdge:
					ret.m_max = Min(ret.m_max, Dist(params.m_endPoint, params.m_start));
					break;

				case NavMesh::ProbeResult::kReachedGoal:
				default:
					break;
				}

				if (FALSE_IN_FINAL_BUILD(debugDraw))
				{
					Nav::DebugDrawProbeResultLs(pNavMesh, rRes, params, tt);
				}
			}
		}
		break;
#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
		if (const NavLedge* pLedge = navLoc.ToNavLedge())
		{
			// T2-TODO
			ret = TraversalActionPack::AnimAdjustRange(0.0f);
		}
		break;
#endif // ENABLE_NAV_LEDGES

	case NavLocation::Type::kNone:
	default:
		ret = TraversalActionPack::AnimAdjustRange(0.0f);
		break;
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
TraversalActionPack::AnimAdjustRange Min(const TraversalActionPack::AnimAdjustRange& lhs,
										 const TraversalActionPack::AnimAdjustRange& rhs)
{
	return TraversalActionPack::AnimAdjustRange(Min(lhs.m_min, rhs.m_min),
												Min(lhs.m_max, rhs.m_max));
}

/// --------------------------------------------------------------------------------------------------------------- ///
TraversalActionPack::AnimAdjustRange TraversalActionPack::DetermineAnimRangeLinear(const AnimAdjustRange& desRange,
																				   bool debugDraw /* = false */,
																				   DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	const NavLocation startNavLoc = GetRegisteredNavLocation();
	const Locator startLocPs = GetLocatorPs();
	const Vector startProbeDirPs = GetLocalX(startLocPs.Rot());

	AnimAdjustRange startRange = TraversalActionPack::DoAnimProbes(desRange,
																   startProbeDirPs,
																   startNavLoc,
																   m_obeyedStaticBlockers,
																   debugDraw,
																   tt);

	const NavLocation destNavLoc = GetDestNavLocation();
	const Locator destLocPs		 = GetBoundFrameInDestSpace().GetLocatorPs();
	const Vector destProbeDirPs	 = GetLocalX(destLocPs.Rot());

	AnimAdjustRange endRange = TraversalActionPack::DoAnimProbes(desRange,
																 destProbeDirPs,
																 destNavLoc,
																 m_obeyedStaticBlockers,
																 debugDraw,
																 tt);

	const AnimAdjustRange res = Min(startRange, endRange);

	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
TraversalActionPack::AnimAdjustRange TraversalActionPack::DetermineAnimRangeEdge(const AnimAdjustRange& desRange,
																				 const FeatureEdgeReference& edge,
																				 const BoundFrame& boundFrame,
																				 NavLocation startNavLoc,
																				 NavLocation destNavLoc,
																				 Point_arg regPosWs,
																				 Point_arg exitPosWs,
																				 IPathWaypoints* pApPathPsOut /* = nullptr */,
																				 bool debugDraw /* = false */,
																				 DebugPrimTime tt /* = kPrimDuration1FrameAuto */)
{
	if (!edge.IsGood())
	{
		return TraversalActionPack::AnimAdjustRange(0.0f);
		if (pApPathPsOut)
			pApPathPsOut->Clear();
	}

	TraversalActionPack::AnimAdjustRange res = desRange;
	const Locator& parentSpace = boundFrame.GetParentSpace();

	TPathWaypoints<16> edgePathPs;

#if ENABLE_NAV_LEDGES
	if ((startNavLoc.GetType() == NavLocation::Type::kNavLedge) || (destNavLoc.GetType() == NavLocation::Type::kNavLedge))
	{
		// TODO
		res = TraversalActionPack::AnimAdjustRange(0.0f);
	}
	else
#endif // ENABLE_NAV_LEDGES
	{
		const NavPoly* pStartPoly = startNavLoc.ToNavPoly();
		const NavPoly* pExitPoly = destNavLoc.ToNavPoly();
		const NavMesh* pStartMesh = pStartPoly ? pStartPoly->GetNavMesh() : nullptr;
		const NavMesh* pExitMesh = pExitPoly ? pExitPoly->GetNavMesh(): nullptr;

		if (!pStartPoly || !pExitPoly)
		{
			res = TraversalActionPack::AnimAdjustRange(0.0f);
		}
		else
		{
			const Locator baseApRefWs = boundFrame.GetLocatorWs();

			Point closestEdgePosWs = edge.GetVert0();
			DistPointSegment(regPosWs, edge.GetVert0(), edge.GetVert1(), &closestEdgePosWs);

			const Locator edgeLocWs = GetFlattenedEdgeLocWs(edge, closestEdgePosWs);
			const Point startPosLs = edgeLocWs.UntransformPoint(regPosWs);
			const Point exitPosLs = edgeLocWs.UntransformPoint(exitPosWs);

			const Locator edgeToApLs = edgeLocWs.UntransformLocator(baseApRefWs);

			const Vector edgeDirWs = SafeNormalize(edge.GetVert1() - edge.GetVert0(), kUnitZAxis);

			TPathWaypoints<16> edgePathLeftPs;
			TPathWaypoints<16> edgePathRightPs;

			{
				NavMesh::ProbeParams startProbeParamsPs;
				startProbeParamsPs.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskNone;
				startProbeParamsPs.m_pStartPoly = pStartPoly;

				NavMesh::ProbeParams exitProbeParamsPs;
				exitProbeParamsPs.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskNone;
				exitProbeParamsPs.m_pStartPoly = pExitPoly;

				float remainingDist = desRange.m_min;

				for (FeatureEdgeReference nextEdge = edge; nextEdge.IsGood(); nextEdge = nextEdge.GetLink(0))
				{
					const Point edge0Ws = (nextEdge == edge) ? closestEdgePosWs : nextEdge.GetVert1(); // going in reverse here
					const Point edge1Ws = nextEdge.GetVert0();

					const Locator edgeLoc0Ws = GetFlattenedEdgeLocWs(nextEdge, edge0Ws);
					const Point startSegPosWs = edgeLoc0Ws.TransformPoint(startPosLs);

					const Point exitSegPosWs = edgeLoc0Ws.TransformPoint(exitPosLs);
					const Vector probeMoveWs = LimitVectorLength(edge1Ws - edge0Ws, 0.0f, remainingDist);

					const float startHitTT = ProbeForEdgeClearanceWs(startSegPosWs, probeMoveWs, &startProbeParamsPs, debugDraw, tt);
					const float exitHitTT = ProbeForEdgeClearanceWs(exitSegPosWs, probeMoveWs, &exitProbeParamsPs, debugDraw, tt);

					const float hitTT = Min(startHitTT, exitHitTT);
					const float probeMoveDist = Length(probeMoveWs);
					remainingDist -= probeMoveDist;

					if (hitTT > 0.0f)
					{
						const Point reachedPosWs = Locator(edge0Ws + probeMoveWs * hitTT, edgeLoc0Ws.Rot())
													   .TransformLocator(edgeToApLs)
													   .Pos();
						const Point reachedPosPs = parentSpace.UntransformPoint(reachedPosWs);
						edgePathLeftPs.AddWaypoint(reachedPosPs);
					}

					if (hitTT >= 0.0f && hitTT < 1.0f)
						break;

					if (remainingDist <= 0.1f)
						break;

					if (nextEdge.GetFlags() != edge.GetFlags())
						break;
				}

				res.m_min = Max(0.0f, res.m_min - remainingDist);

				edgePathLeftPs.Reverse();

				if (FALSE_IN_FINAL_BUILD(debugDraw))
				{
					edgePathLeftPs.DebugDraw(parentSpace, true, kColorBlue, kColorOrange, 0.0f, tt);
				}
			}

			{
				NavMesh::ProbeParams startProbeParamsPs;
				startProbeParamsPs.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskNone;
				startProbeParamsPs.m_pStartPoly = pStartPoly;

				NavMesh::ProbeParams exitProbeParamsPs;
				exitProbeParamsPs.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskNone;
				exitProbeParamsPs.m_pStartPoly = pExitPoly;

				float remainingDist = desRange.m_max;

				for (FeatureEdgeReference nextEdge = edge; nextEdge.IsGood(); nextEdge = nextEdge.GetLink(1))
				{
					const Point edge0Ws = (nextEdge == edge) ? closestEdgePosWs : nextEdge.GetVert0();
					const Point edge1Ws = nextEdge.GetVert1();

					const Locator edgeLoc0Ws  = GetFlattenedEdgeLocWs(nextEdge, edge0Ws);
					const Point startSegPosWs = edgeLoc0Ws.TransformPoint(startPosLs);

					const Point exitSegPosWs = edgeLoc0Ws.TransformPoint(exitPosLs);
					const Vector probeMoveWs = LimitVectorLength(edge1Ws - edge0Ws, 0.0f, remainingDist);

					const float startHitTT = ProbeForEdgeClearanceWs(startSegPosWs,
																	 probeMoveWs,
																	 &startProbeParamsPs,
																	 debugDraw,
																	 tt);
					const float exitHitTT  = ProbeForEdgeClearanceWs(exitSegPosWs,
																	 probeMoveWs,
																	 &exitProbeParamsPs,
																	 debugDraw,
																	 tt);

					const float hitTT = Min(startHitTT, exitHitTT);
					remainingDist -= Length(probeMoveWs);

					if (hitTT > 0.0f)
					{
						const Point reachedPosWs = Locator(edge0Ws + probeMoveWs * hitTT, edgeLoc0Ws.Rot())
													   .TransformLocator(edgeToApLs)
													   .Pos();
						const Point reachedPosPs = parentSpace.UntransformPoint(reachedPosWs);
						edgePathRightPs.AddWaypoint(reachedPosPs);
					}

					if (hitTT >= 0.0f && hitTT < 1.0f)
						break;

					if (remainingDist <= 0.1f)
						break;

					if (nextEdge.GetFlags() != edge.GetFlags())
						break;
				}

				res.m_max = Max(0.0f, res.m_max - remainingDist);

				if (FALSE_IN_FINAL_BUILD(debugDraw))
				{
					edgePathRightPs.DebugDraw(parentSpace, true, kColorRed, kColorGreen, 0.0f, tt);
				}
			}

			edgePathPs = edgePathLeftPs;
			edgePathPs.Append(edgePathRightPs);
		}
	}

	if (pApPathPsOut)
	{
		pApPathPsOut->Clear();
		pApPathPsOut->CopyFrom(&edgePathPs);
	}

	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::DebugDrawAnimRange(DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	switch (m_animAdjustType)
	{
	case AnimAdjustType::kLinear:
		{
			const Point startPosWs = GetRegistrationPointWs() + Vector(0.0f, 0.1f, 0.0f);
			const Locator startLocWs = GetLocatorWs();
			const Vector startProbeDirWs = GetLocalX(startLocWs.Rot());

			const Point destPosWs = GetExitPointWs() + Vector(0.0f, 0.05f, 0.0f);
			const Locator destLocWs = GetBoundFrameInDestSpace().GetLocatorWs();
			const Vector destProbeDirWs = GetLocalX(destLocWs.Rot());

			DetermineAnimRangeLinear(m_desAnimAdjustWidth, g_navMeshDrawFilter.m_drawTraversalApAnimRange);

			if (Abs(m_animAdjustWidth.m_min) > NDI_FLT_EPSILON)
			{
				g_prim.Draw(DebugArrow(startPosWs, startProbeDirWs * -1.0f * m_animAdjustWidth.m_min, kColorGreen, 0.5f, kPrimEnableHiddenLineAlpha), tt);
				g_prim.Draw(DebugArrow(destPosWs, destProbeDirWs * -1.0f * m_animAdjustWidth.m_min, kColorYellow, 0.5f, kPrimEnableHiddenLineAlpha), tt);
			}
			if (Abs(m_animAdjustWidth.m_max) > NDI_FLT_EPSILON)
			{
				g_prim.Draw(DebugArrow(startPosWs, startProbeDirWs * m_animAdjustWidth.m_max, kColorGreen, 0.5f, kPrimEnableHiddenLineAlpha), tt);
				g_prim.Draw(DebugArrow(destPosWs, destProbeDirWs * m_animAdjustWidth.m_max, kColorYellow, 0.5f, kPrimEnableHiddenLineAlpha), tt);
			}
		}
		break;

	case AnimAdjustType::kEdge:
		{
			DetermineAnimRangeEdge(m_desAnimAdjustWidth,
								   m_animEdgeRef,
								   GetBoundFrame(),
								   GetRegisteredNavLocation(),
								   GetDestNavLocation(),
								   GetRegistrationPointWs(),
								   GetExitPointWs(),
								   nullptr,
								   g_navMeshDrawFilter.m_drawTraversalApAnimRange,
								   tt);

			DebugDrawAnimRangeEdge(tt);
		}
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::DebugDrawAnimRangeEdge(DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	if (!m_animEdgeRef.IsGood())
		return;

	const Locator& parentSpace = GetBoundFrame().GetParentSpace();

	m_edgeApPathPs.DebugDraw(parentSpace, true, kColorBlue, kColorOrange, kAnimRangeProbeRadius, tt);

	const I32F numWaypoints = m_edgeApPathPs.GetWaypointCount();

	for (I32F i = 0; i < numWaypoints - 1; ++i)
	{
		const Point edgePosPs = AveragePos(m_edgeApPathPs.GetWaypoint(i), m_edgeApPathPs.GetWaypoint(i + 1));
		const Locator apEdgeLocPs = RecreateEdgeLocFromPathLegPs(m_edgeApPathPs, i, m_edgeToApFwLs, edgePosPs);
		g_prim.Draw(DebugCoordAxes(parentSpace.TransformLocator(apEdgeLocPs), 0.3f, kPrimEnableHiddenLineAlpha), tt);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::CheckRigidBodyIsBlocking(RigidBody* pBody, uintptr_t userData)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	if (!IsBlockedByRigidBody())
	{
		if (CheckRigidBodyIsBlockingInternal(pBody))
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::RemoveBlockingRigidBody(const RigidBody* pBody)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	if (m_hBlockingRigidBody == pBody)
	{
		RemoveBlockingRigidBody();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::SearchForBlockingRigidBodies()
{
	PROFILE_AUTO(Navigation);

	if (!IsBlockedByRigidBody())
	{
		Obb obb;
		if (GetCheckBlockingObb(obb))
		{
			Aabb aabb = obb.GetEnclosingAabb();
			TapBlockingBodyCollector collector;
			collector.m_hAp = this;
			HavokAabbQuery(aabb.m_min,
						   aabb.m_max,
						   collector,
						   CollideFilter(Collide::kLayerMaskFgBig, Pat(Pat::kPassThroughMask)));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::GetCheckBlockingObb(Obb& obbOut) const
{
	const Locator locWs = GetLocatorWs();

	obbOut.SetEmpty();
	obbOut.m_transform = locWs.AsTransform();

	if (IsVault())
	{
		Maybe<BoundFrame> maybeV1 = GetAnchorLocator(SID("apReference-vault-1"), 0.0f);
		Maybe<BoundFrame> maybeV2 = GetAnchorLocator(SID("apReference-vault-2"), 1.0f);

		if (!maybeV1.Valid() || !maybeV2.Valid())
		{
			return false;
		}

		const Locator v1Ws = maybeV1.Get().GetLocatorWs();
		const Locator v2Ws = maybeV2.Get().GetLocatorWs();

		const Locator v1Ls = locWs.UntransformLocator(v1Ws);
		const Locator v2Ls = locWs.UntransformLocator(v2Ws);

		const Vector up1Ls = GetLocalY(v1Ls);
		const Vector up2Ls = GetLocalY(v2Ls);

		const float yMin = 0.3f;
		const float yMax = 0.7f;

		obbOut.IncludePointLs(v1Ls.Pos() + (up1Ls * yMin));
		obbOut.IncludePointLs(v2Ls.Pos() + (up2Ls * yMin));
		obbOut.IncludePointLs(v1Ls.Pos() + (up1Ls * yMax));
		obbOut.IncludePointLs(v2Ls.Pos() + (up2Ls * yMax));

		obbOut.Expand(Vector(0.2f, 0.0f, 0.0f), Vector(0.2f, 0.0f, 0.0f));
	}
	else if (false) // (IsVarTap())
	{
		Maybe<BoundFrame> maybeAp	 = GetAnchorLocator(SID("apReference"), 0.0f);
		Maybe<BoundFrame> maybeApEnd = GetAnchorLocator(SID("apReference-end"), 1.0f);

		if (!maybeAp.Valid() || !maybeApEnd.Valid())
		{
			return false;
		}

		const Point apPosWs	   = maybeAp.Get().GetTranslationWs();
		const Point apEndPosWs = maybeApEnd.Get().GetTranslationWs();

		const Point apPosLs	   = locWs.UntransformPoint(apPosWs);
		const Point apEndPosLs = locWs.UntransformPoint(apEndPosWs);

		obbOut.IncludePointLs(apPosLs);
		obbOut.IncludePointLs(apEndPosLs);

		obbOut.Expand(0.25f);
	}
	else
	{
		const Point regPosWs  = GetRegistrationPointWs();
		const Point exitPosWs = GetExitPointWs();

		const Point regPosLs  = locWs.UntransformPoint(regPosWs);
		const Point exitPosLs = locWs.UntransformPoint(exitPosWs);

		obbOut.IncludePointLs(regPosLs);
		obbOut.IncludePointLs(exitPosLs);

		obbOut.Expand(0.25f);
	}

	//obbOut.IncludePointLs(Point(-m_animAdjustWidth.m_min, 0.0f, 0.0f));
	//obbOut.IncludePointLs(Point(m_animAdjustWidth.m_max, 0.0f, 0.0f));

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::CheckRigidBodyIsBlockingInternal(const RigidBody* pBody)
{
	bool isBlocking = false;

	Obb checkObb;
	if (GetCheckBlockingObb(checkObb))
	{
		Obb bodyObb;
		pBody->GetObb(bodyObb);

		if (ObbObbIntersect(bodyObb, checkObb))
		{
			SetBlockingRigidBody(pBody);
			isBlocking = true;
		}
	}

	return isBlocking;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::SetBlockingRigidBody(const RigidBody* pBody)
{
	if ((pBody != nullptr) ^ m_hBlockingRigidBody.HandleValid())
	{
		m_costDirty = true;
		ActionPackMgr::Get().SetUpdatesEnabled(this, NeedsUpdate());
	}

	m_hBlockingRigidBody = pBody;
	NdGameObject* pGo = pBody->GetOwner();
	GAMEPLAY_ASSERT(pGo && pGo->GetApBlockerInterface());
	pGo->GetApBlockerInterface()->AddBlockedAp(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::RemoveBlockingRigidBody()
{
	if (!m_hBlockingRigidBody.HandleValid())
	{
		m_hBlockingRigidBody = nullptr;
		return;
	}

	m_costDirty = true;
	ActionPackMgr::Get().SetUpdatesEnabled(this, NeedsUpdate());

	const RigidBody* pBody = m_hBlockingRigidBody.ToBody();
	m_hBlockingRigidBody = nullptr;

	if (pBody)
	{
		NdGameObject* pGo = pBody->GetOwner();
		pGo->GetApBlockerInterface()->RemoveBlockedAp(this);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::AddPlayerBlock()
{
	if (m_playerBlocked == true)
		return;

	m_playerBlocked = true;
	m_costDirty		= true;
	ActionPackMgr::Get().SetUpdatesEnabled(this, NeedsUpdate());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TraversalActionPack::RemovePlayerBlock()
{
	if (m_playerBlocked == false)
		return;

	m_playerBlocked = false;
	m_costDirty		= true;
	ActionPackMgr::Get().SetUpdatesEnabled(this, NeedsUpdate());
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsTraversalActionPackTypeId(const StringId64 apTypeId)
{
	bool isTap = false;
	switch (apTypeId.GetValue())
	{
	case SID_VAL("TraversalActionPack"):
	case SID_VAL("SqueezeThroughActionPack"):
	case SID_VAL("JumpTraversalActionPack"):
	case SID_VAL("RopeTraversalActionPack"):
	case SID_VAL("RopeClimbTraversalActionPack"):
	case SID_VAL("LadderTraversalActionPack"):
	case SID_VAL("BalanceBeamTraversalActionPack"):
	case SID_VAL("VaultTraversalActionPack"):
	case SID_VAL("VariableTraversalActionPack"):
		isTap = true;
		break;
	}
	return isTap;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<BoundFrame> TraversalActionPack::GetAnchorLocator(StringId64 apNameId, float spaceBlend) const
{
	Maybe<BoundFrame> ret = MAYBE::kNothing;

	switch (apNameId.GetValue())
	{
	case SID_VAL("apReference"):
		{
			const BoundFrame& src = GetBoundFrame();
			const BoundFrame& dst = GetBoundFrameInDestSpace();
			ret = LerpBoundFrame(src, dst, spaceBlend);
		}
		break;

	case SID_VAL("apReference-end"):
		if (IsVarTap())
		{
			const BoundFrame& srcBf		  = GetBoundFrame();
			const BoundFrame& dstBf		  = GetBoundFrameInDestSpace();
			const Vector traversalDeltaLs = GetTraversalDeltaLs();

			const BoundFrame src = ConstructVarTapEndBoundFrame(srcBf, traversalDeltaLs, m_exitOffsetLs);
			const BoundFrame dst = ConstructVarTapEndBoundFrame(dstBf, traversalDeltaLs, m_exitOffsetLs);

			ret = LerpBoundFrame(src, dst, spaceBlend);
		}
		break;

	case SID_VAL("apReference-vault-1"):
		if (IsVault())
		{
			BoundFrame src = GetBoundFrame();
			BoundFrame dst = GetBoundFrameInDestSpace();

			const float wallHeight = GetVaultWallHeight();

			const Vector wallDeltaLs = Vector(0.0f, wallHeight, 0.0f);

			src.AdjustTranslationLs(wallDeltaLs);
			dst.AdjustTranslationLs(wallDeltaLs);

			ret = LerpBoundFrame(src, dst, spaceBlend);
		}
		break;
	case SID_VAL("apReference-vault-2"):
		if (IsVault())
		{
			BoundFrame src = GetBoundFrame();
			BoundFrame dst = GetBoundFrameInDestSpace();
			const Vector traversalDeltaLs = GetTraversalDeltaLs();

			const float wallHeight = GetVaultWallHeight();

			const Vector wallDeltaLs = Vector(0.0f, wallHeight, traversalDeltaLs.Z());

			src.AdjustTranslationLs(wallDeltaLs);
			dst.AdjustTranslationLs(wallDeltaLs);

			ret = LerpBoundFrame(src, dst, spaceBlend);
		}
		break;
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::IsToOrFromDive() const
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const NavMesh* pSourceNavMesh = GetSourceNavLocation().ToNavMesh();
	const NavMesh* pDestNavMesh = GetDestNavLocation().ToNavMesh();

	const bool isToOrFromDive = (pSourceNavMesh && pSourceNavMesh->NavMeshForcesDive()) || (pDestNavMesh && pDestNavMesh->NavMeshForcesDive());

	return isToOrFromDive;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::IsKnownPathNodeId(U32F iNode) const
{
	if (iNode == NavPathNodeMgr::kInvalidNodeId)
		return false;

	for (U32F i = 0; i < kMaxPathNodesPerSide; ++i)
	{
		if (m_enterPathNodeIds[i] == iNode)
			return true;

		if (m_exitPathNodeIds[i] == iNode)
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TraversalActionPack::GetAnimAdjustNavPortalWs(Point_arg prevPosWs,
												   Point_arg curPosWs,
												   NavPathNode::NodeType nodeType,
												   float depenRadius,
												   Point& v0WsOut,
												   Point& v1WsOut,
												   I64 instanceSeed /* = -1 */) const
{
	if (!m_animAdjustSetup)
		return false;

	bool valid = false;

	switch (m_animAdjustType)
	{
	case AnimAdjustType::kLinear:
		{
			Point basePosWs = kOrigin;

			if (nodeType == NavPathNode::kNodeTypeActionPackEnter)
			{
				const BoundFrame& baseBf = GetBoundFrame();
				const Locator baseLocWs = baseBf.GetLocatorWs();
				const Point offsetLs = m_regPtLs - Vector(0.0f, 0.0f, depenRadius);

				basePosWs = baseLocWs.TransformPoint(offsetLs);
			}
			else
			{
				const BoundFrame baseBf = IsVarTap() ? GetVarTapEndBoundFrame() : GetBoundFrame();
				const Locator baseLocWs = baseBf.GetLocatorWs();
				const Point offsetLs = Point(0.0f, 0.0f, depenRadius) + m_exitOffsetLs;

				basePosWs = baseLocWs.TransformPoint(offsetLs);
			}

			const Locator tapLocWs = GetLocatorWs();
			const Vector xDirWs = GetLocalX(tapLocWs);

			const AnimAdjustRange& adjustRange = GetApAnimAdjustRange();

			v0WsOut = basePosWs + (-1.0f * adjustRange.m_min * xDirWs);
			v1WsOut = basePosWs + (adjustRange.m_max * xDirWs);

			if (instanceSeed >= 0)
			{
				const float kSegSize = 0.5f;
				const float totalRange = adjustRange.m_min + adjustRange.m_max;
				const U64 numSegments = U64(totalRange / kSegSize);

				if (numSegments > 1)
				{
					const U64 selSeg = instanceSeed % numSegments;
					const float t0	  = float(selSeg) / float(numSegments);
					const float t1	  = Min(float(selSeg + 1) / float(numSegments), 1.0f);

					const Point limit0Ws = Lerp(v0WsOut, v1WsOut, t0);
					const Point limit1Ws = Lerp(v0WsOut, v1WsOut, t1);

					v0WsOut = limit0Ws;
					v1WsOut = limit1Ws;
				}
			}

			valid = true;
		}
		break;

	case AnimAdjustType::kEdge:
		if (m_edgeApPathPs.IsValid())
		{
			const Locator& parentSpace = GetBoundFrame().GetParentSpace();
			const Point searchPosWs	   = (nodeType == NavPathNode::kNodeTypeActionPackEnter) ? curPosWs : prevPosWs;
			const Point searchPosPs	   = parentSpace.UntransformPoint(searchPosWs);

			I32F iClosestLeg = -1;
			m_edgeApPathPs.ClosestPoint(searchPosPs, nullptr, &iClosestLeg);

			if (iClosestLeg >= 0)
			{
				const Point edgePos0Ps = m_edgeApPathPs.GetWaypoint(iClosestLeg);
				const Point edgePos1Ps = m_edgeApPathPs.GetWaypoint(iClosestLeg + 1);

				const Locator edgeLocPs = RecreateEdgeLocFromPathLegPs(m_edgeApPathPs,
																	   iClosestLeg,
																	   m_edgeToApFwLs,
																	   edgePos0Ps);

				Point posDeltaLs = kOrigin;

				if (nodeType == NavPathNode::kNodeTypeActionPackEnter)
				{
					posDeltaLs = m_regPtLs - Vector(0.0f, 0.0f, -depenRadius);
				}
				else if (IsVarTap())
				{
					posDeltaLs = GetBoundFrame().GetLocatorWs().UntransformPoint(GetVarTapEndBoundFrame().GetTranslationWs());
					posDeltaLs = m_exitPtLs + Vector(0.0f, 0.0f, depenRadius);
				}
				else
				{
					posDeltaLs = GetBoundFrame().GetLocatorWs().UntransformPoint(GetExitPointWs());
					posDeltaLs = m_exitPtLs + Vector(0.0f, 0.0f, depenRadius);
				}

				const Point portalPos0Ps = Locator(edgePos0Ps, edgeLocPs.Rot()).TransformPoint(posDeltaLs);
				const Point portalPos1Ps = Locator(edgePos1Ps, edgeLocPs.Rot()).TransformPoint(posDeltaLs);

				// NB: Intentionally reversed
				v0WsOut = parentSpace.TransformPoint(portalPos1Ps);
				v1WsOut = parentSpace.TransformPoint(portalPos0Ps);

				valid = true;
			}
		}
		break;
	}

	return valid;
}
