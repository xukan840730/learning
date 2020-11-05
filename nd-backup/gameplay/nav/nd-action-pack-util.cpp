/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nd-action-pack-util.h"

#include "ndlib/nd-config.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-mgr.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-util.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/platform-control.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/level/level.h"
#include "gamelib/scriptx/h/ai-tap-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static void DebugDrawRegistrationFailure(StringId64 apSpawnerId,
										 const char* apTypeName,
										 const char* levelName,
										 Point_arg findMeshPointWs,
										 const Locator& locatorWs,
										 Point_arg nearestPointWs,
										 Vector_arg probeDirWs,
										 StringId64 reqLevelId,
										 const char* szReason,
										 DebugPrimTime tt = kPrimDuration1FrameAuto)
{
	STRIP_IN_FINAL_BUILD;

	g_prim.Draw(DebugCross(findMeshPointWs, 0.2f, 3.0f, kColorOrangeTrans, kPrimEnableHiddenLineAlpha), tt);
	g_prim.Draw(DebugCoordAxes(locatorWs, 0.1f, kPrimEnableHiddenLineAlpha), tt);

	const RenderCamera& cam = GetRenderCamera(0);

	if (cam.IsSphereInFrustum(Sphere(locatorWs.Pos(), 2.0f)) && Dist(cam.m_position, locatorWs.Pos()) < 35.0f)
	{
		StringBuilder<256> msg;

		msg.append_format("%s Register Failed\n", apTypeName ? apTypeName : "AP");

		if (apSpawnerId != INVALID_STRING_ID_64)
		{
			msg.append_format("spawner: %s", DevKitOnly_StringIdToString(apSpawnerId));

			if (levelName)
			{
				msg.append_format(" [level: %s]", levelName);
			}
			msg.append_format("\n");
		}
		else if (levelName)
		{
			msg.append_format("[level: %s]\n", levelName);
		}

		if (reqLevelId != INVALID_STRING_ID_64)
		{
			msg.append_format("[required level: %s]\n", DevKitOnly_StringIdToString(reqLevelId));
		}

		if (szReason)
		{
			msg.append_format("%s\n", szReason);
		}

		g_prim.Draw(DebugString(findMeshPointWs + Vector(0.f, 0.2f, 0.f), msg.c_str(), kColorWhite, 0.5f), tt);
	}

	if (Length(probeDirWs) > NDI_FLT_EPSILON)
	{
		g_prim.Draw(DebugArrow(nearestPointWs, probeDirWs, kColorRed, 0.4f), tt);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPack* GetActionPackFromSpawner(const EntitySpawner* pSpawner)
{
	if (!pSpawner)
	{
		return nullptr;
	}

	ActionPack* pActionPack = nullptr;

	for (SpawnerLoginChunkNode* pNode = pSpawner->GetLoginChunkList(); pNode; pNode = pNode->GetNext())
	{
		ActionPack* pChunkAp = pNode->GetActionPack();
		if (!pChunkAp)
			continue;
		
		pActionPack = pChunkAp;
		break;
	}

#ifdef JBELLOMY
	ActionPack* pTestAp = static_cast<ActionPack*>(pSpawner->GetLoginData());

	NAV_ASSERT(pTestAp == pActionPack || !pTestAp || pTestAp->IsCorrupted());
#endif

	return pActionPack;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPack* GetActionPackFromSpawnerId(StringId64 spawnerId)
{
	const EntitySpawner* pSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByNameId(spawnerId);
	return GetActionPackFromSpawner(pSpawner);
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::VarTapAnimType BuildVarTapAnimType(StringId64 typeId)
{
	DC::VarTapAnimType animType;

	switch (typeId.GetValue())
	{
	case SID_VAL("water"):
		animType = DC::kVarTapAnimTypeWater;
		break;

	case SID_VAL("ledge"):
		animType = DC::kVarTapAnimTypeLedge;
		break;

	case SID_VAL("ground-small"):
		animType = DC::kVarTapAnimTypeGroundSmall;
		break;

	case SID_VAL("ground"):
	default:
		animType = DC::kVarTapAnimTypeGround;
		break;
	}

	return animType;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsGroundVarTapAnimType(const DC::VarTapAnimType animType)
{
	bool isGround = false;

	switch (animType)
	{
	case DC::kVarTapAnimTypeGroundSmall:
	case DC::kVarTapAnimTypeGround:
		isGround = true;
	}

	return isGround;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool VarTapAnimTypesCompatible(const DC::VarTapAnimType typeA, const DC::VarTapAnimType typeB)
{
	if (IsGroundVarTapAnimType(typeA) && IsGroundVarTapAnimType(typeB))
	{
		return true;
	}

	return typeA == typeB;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FindActionPackNavMesh(const ActionPack* pAp,
						   FindBestNavMeshParams& params,
						   const Level* pAllocLevel,
						   const PlatformControl* pPlatformControl,
						   bool drawFailure)
{
	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	params.m_cullDist = 0.0f;

	if (params.m_bindSpawnerNameId != INVALID_STRING_ID_64 && pPlatformControl)
	{
		pPlatformControl->FindNavMesh(&params);
	}
	else if (const Level* pSearchLevel = EngineComponents::GetLevelMgr()->GetLevel(params.m_requiredLevelId))
	{
		pSearchLevel->FindNavMesh(&params);
	}
	else
	{
		nmMgr.FindNavMeshWs(&params);
	}

	if (FALSE_IN_FINAL_BUILD(drawFailure && !params.m_pNavPoly && pAp))
	{
		DebugDrawRegistrationFailure(pAp->GetSpawnerId(),
									 pAp->GetTypeName(),
									 pAllocLevel ? pAllocLevel->GetName() : nullptr,
									 params.m_pointWs,
									 pAp->GetLocatorWs(),
									 kOrigin,
									 kZero,
									 INVALID_STRING_ID_64,
									 "No NavMesh Found");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackCanRegisterToPoly(StringId64 apNameId,
								 const ActionPackRegistrationParams& params,
								 const BoundFrame& apLoc,
								 const PlatformControl* pPlatformControl /* = nullptr */,
								 const NavPoly** ppOutNavPoly /* = nullptr */)
{
	return ActionPackCanRegisterToPoly(apNameId,
									   apLoc,
									   params.m_regPtLs,
									   params.m_bindId,
									   params.m_pAllocLevel ? params.m_pAllocLevel->GetNameId() : INVALID_STRING_ID_64,
									   Nav::kStaticBlockageMaskAll,
									   params.m_yThreshold,
									   params.m_probeDist,
									   false,
									   params.m_pAllocLevel,
									   pPlatformControl,
									   ppOutNavPoly);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackCanRegisterToPoly(StringId64 apNameId,
								 const BoundFrame& apLoc,
								 Point_arg regPosLs,
								 StringId64 bindSpawnerNameId,
								 StringId64 reqNavMeshLevelId,
								 Nav::StaticBlockageMask obeyedStaticBlockers,
								 float yThreshold,
								 float probeDist,
								 bool drawFailure,
								 const Level* pAllocLevel,
								 const PlatformControl* pPlatformControl /* = nullptr */,
								 const NavPoly** ppOutNavPoly /* = nullptr */)
{
	bool success = false;

	if (ppOutNavPoly)
	{
		*ppOutNavPoly = nullptr;
	}

	const Locator& locWs = apLoc.GetLocator();
	const Point& regPtWs = locWs.TransformPoint(regPosLs);

	FindBestNavMeshParams findMesh;
	findMesh.m_yThreshold = yThreshold;
	findMesh.m_pointWs	  = regPtWs;
	findMesh.m_bindSpawnerNameId = bindSpawnerNameId;
	findMesh.m_requiredLevelId	 = reqNavMeshLevelId;
	findMesh.m_obeyedStaticBlockers = obeyedStaticBlockers;

	FindActionPackNavMesh(nullptr, findMesh, pAllocLevel, pPlatformControl, false);

	if (findMesh.m_pNavPoly && findMesh.m_pNavPoly->GetNavMesh())
	{
		const NavMesh* pNavMesh	= findMesh.m_pNavMesh;
		const Point startPs		= pNavMesh->WorldToParent(findMesh.m_nearestPointWs);
		const Locator locPs		= pNavMesh->GetParentSpace().UntransformLocator(locWs);
		const Vector zDirPs		= Normalize(VectorXz(GetLocalZ(locPs.Rot())));

		NavMesh::ProbeParams probe;
		probe.m_start		= startPs;
		probe.m_move		= RotateY90(zDirPs) * probeDist;
		probe.m_pStartPoly	= findMesh.m_pNavPoly;
		probe.m_obeyedStaticBlockers = obeyedStaticBlockers;

		pNavMesh->ProbePs(&probe);

		if (FALSE_IN_FINAL_BUILD(probe.m_hitEdge && drawFailure))
		{
			const Vector moveWs = pNavMesh->ParentToWorld(probe.m_move);
			DebugDrawRegistrationFailure(apNameId,
										 nullptr,
										 pAllocLevel ? pAllocLevel->GetName() : nullptr,
										 findMesh.m_pointWs,
										 locWs,
										 findMesh.m_nearestPointWs,
										 moveWs,
										 INVALID_STRING_ID_64,
										 "NavMeshProbe Hit Edge");
		}

		if (!probe.m_hitEdge)
		{
			probe.m_move = RotateYMinus90(zDirPs) * probeDist;
			pNavMesh->ProbePs(&probe);

			if (FALSE_IN_FINAL_BUILD(probe.m_hitEdge && drawFailure))
			{
				const Vector moveWs = pNavMesh->ParentToWorld(probe.m_move);
				DebugDrawRegistrationFailure(apNameId,
											 nullptr,
											 pAllocLevel ? pAllocLevel->GetName() : nullptr,
											 findMesh.m_pointWs,
											 locWs,
											 findMesh.m_nearestPointWs,
											 moveWs,
											 INVALID_STRING_ID_64,
											 "NavMeshProbe Hit Edge");
			}
		}
		if (!probe.m_hitEdge)
		{
			probe.m_move = -probeDist*zDirPs;
			pNavMesh->ProbePs(&probe);

			if (FALSE_IN_FINAL_BUILD(probe.m_hitEdge && drawFailure))
			{
				const Vector moveWs = pNavMesh->ParentToWorld(probe.m_move);
				DebugDrawRegistrationFailure(apNameId,
											 nullptr,
											 pAllocLevel ? pAllocLevel->GetName() : nullptr,
											 findMesh.m_pointWs,
											 locWs,
											 findMesh.m_nearestPointWs,
											 moveWs,
											 INVALID_STRING_ID_64,
											 "NavMeshProbe Hit Edge");
			}
		}
		if (!probe.m_hitEdge)
		{
			if (ppOutNavPoly)
			{
				*ppOutNavPoly = findMesh.m_pNavPoly;
			}

			success = true;
		}
	}
	else if (FALSE_IN_FINAL_BUILD(drawFailure))
	{
		DebugDrawRegistrationFailure(apNameId,
									 nullptr,
									 pAllocLevel ? pAllocLevel->GetName() : nullptr,
									 findMesh.m_pointWs,
									 locWs,
									 kOrigin,
									 kZero,
									 reqNavMeshLevelId,
									 "No NavMesh Found");
	}

	return success;
}

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
void FindActionPackNavLedgeGraph(const ActionPack* pAp,
								 FindNavLedgeGraphParams& params,
								 const Level* pAllocLevel,
								 const PlatformControl* pPlatformControl,
								 bool drawFailure)
{
	const NavLedgeGraphMgr& lgMgr = NavLedgeGraphMgr::Get();
	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	if (params.m_bindSpawnerNameId != INVALID_STRING_ID_64 && pPlatformControl)
	{
		pPlatformControl->FindNavLedgeGraph(&params);
	}
	else if (pAllocLevel)
	{
		pAllocLevel->FindNavLedgeGraph(&params);
	}
	else
	{
		lgMgr.FindLedgeGraph(&params);
	}

	if (FALSE_IN_FINAL_BUILD(drawFailure && !params.m_pNavLedge && pAp))
	{
		DebugDrawRegistrationFailure(pAp->GetSpawnerId(),
									 pAp->GetTypeName(),
									 pAllocLevel ? pAllocLevel->GetName() : nullptr,
									 params.m_pointWs,
									 pAp->GetLocatorWs(),
									 kOrigin,
									 kZero,
									 INVALID_STRING_ID_64,
									 "No NavLedgeGraph Found");
	}
}
#endif // ENABLE_NAV_LEDGES

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackCanRegisterToLedge(const ActionPackRegistrationParams& params,
								  const BoundFrame& apLoc,
								  const PlatformControl* pPlatformControl /* = nullptr */,
								  const NavLedge** ppOutNavLedge /* = nullptr */)
{
	return ActionPackCanRegisterToLedge(apLoc,
										params.m_regPtLs,
										params.m_bindId,
										params.m_probeDist,
										false,
										params.m_pAllocLevel,
										pPlatformControl,
										ppOutNavLedge);
}
#endif // ENABLE_NAV_LEDGES

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackCanRegisterToLedge(const BoundFrame& apLoc,
								  Point_arg regPosLs,
								  StringId64 bindSpawnerNameId,
								  float searchRadius,
								  bool drawFailure,
								  const Level* pLevel,
								  const PlatformControl* pPlatformControl /* = nullptr */,
								  const NavLedge** ppOutNavLedge /* = nullptr */)
{
	bool success = false;

	if (ppOutNavLedge)
	{
		*ppOutNavLedge = nullptr;
	}

	const Locator& locWs = apLoc.GetLocator();
	const Point& regPtWs = locWs.TransformPoint(regPosLs);

	FindNavLedgeGraphParams findGraph;
	findGraph.m_pointWs = regPtWs;
	findGraph.m_searchRadius = searchRadius;

	FindActionPackNavLedgeGraph(nullptr, findGraph, pLevel, pPlatformControl, false);

	if (findGraph.m_pNavLedge)
	{
		if (ppOutNavLedge)
		{
			*ppOutNavLedge = findGraph.m_pNavLedge;
		}

		success = true;
	}
	else if (FALSE_IN_FINAL_BUILD(drawFailure))
	{
		DebugDrawRegistrationFailure(INVALID_STRING_ID_64,
									 nullptr,
									 pLevel ? pLevel->GetName() : nullptr,
									 findGraph.m_pointWs,
									 locWs,
									 kOrigin,
									 kZero,
									 INVALID_STRING_ID_64,
									 "No NavLedge Found");
	}

	return success;
}
#endif // ENABLE_NAV_LEDGES

/// --------------------------------------------------------------------------------------------------------------- ///
void DrawDebugFlatArc(const Locator& pivotLocWs,
					  Point_arg centerPosWs,
					  float halfAngleDeg,
					  Color color,
					  float width)
{
	STRIP_IN_FINAL_BUILD;

	DrawDebugFlatArc(pivotLocWs, centerPosWs, halfAngleDeg, halfAngleDeg, color, width);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DrawDebugFlatArc(const Locator& pivotLocWs,
					  Point_arg centerPosWs,
					  float cwAngleDeg,
					  float ccwAngleDeg,
					  Color color,
					  float width)
{
	STRIP_IN_FINAL_BUILD;

	static const Vector kOffsetY = Vector(kUnitYAxis) * 0.1f;

	// Find the pivot position on the pivot axis (pivot loc local Y)
	const Vector pivotToCenterWs = centerPosWs - pivotLocWs.Pos();
	const Vector pivotAxisWs     = GetLocalY(pivotLocWs.Rot());
	const F32 pivotAxisDistWs    = Dot(pivotToCenterWs, pivotAxisWs);
	const Point pivotPosWs       = pivotLocWs.Pos() + pivotAxisDistWs * pivotAxisWs;

	// Find the initial and final vectors
	const float cwAngleRad        = DegreesToRadians(cwAngleDeg);
	const float ccwAngleRad       = DegreesToRadians(ccwAngleDeg);
	const Vector adjPivotToCenter = centerPosWs - pivotPosWs;
	const Transform initialXfrm   = Transform(kUnitYAxis, -cwAngleRad);
	const Vector initialVector    = adjPivotToCenter * initialXfrm;
	const Transform finalXfrm     = Transform(kUnitYAxis, ccwAngleRad);
	const Vector finalVector      = adjPivotToCenter * finalXfrm;

	g_prim.Draw(DebugArc(pivotPosWs + kOffsetY,
						 initialVector,
						 finalVector,
						 color,
						 width,
						 PrimAttrib(kPrimEnableWireframe)));
}


/// --------------------------------------------------------------------------------------------------------------- ///
const DC::VarTapTable* GetVarTapTable(StringId64 id, U64 instanceSeed /* = 0 */)
{
	const DC::AiLibGlobalInfo* pInfo = AI::GetAiLibGlobalInfo();
	const void* pData = ScriptManager::LookupPointerInModule(id, pInfo->m_actionPackModule, nullptr);
	if (!pData)
		return nullptr;

	const DC::VarTapTable* pTable = nullptr;

	if (ScriptManager::TypeOf(pData) == SID("symbol-array"))
	{
		const DC::SymbolArray* pArray = (const DC::SymbolArray*)pData;
		if (pArray->m_count > 0)
		{
			const I32F index = instanceSeed % pArray->m_count;
			const StringId64 tableId = pArray->m_array[index];
			
			pTable = ScriptManager::LookupInModule<DC::VarTapTable>(tableId, pInfo->m_actionPackModule, nullptr);
		}
	}
	else
	{
		pTable = (const DC::VarTapTable*)pData;
	}

	return pTable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::VarTapTable* GetVarTapTable(const TraversalActionPack* pTap, U64 instanceSeed /* = 0 */)
{
	return pTap ? GetVarTapTable(pTap->GetInfoId(), instanceSeed) : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct JumpTapCurveParams
{
	float m_vx = 1.5f;
	float m_g = -9.8f; // TODO: Change for The Last of Us Part 3: Last In Space
};

static CONST_EXPR JumpTapCurveParams s_jumpTapCurve;

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsUnderJumpTapCurveLs(Point_arg posLs)
{
	const float kG = s_jumpTapCurve.m_g;
	const float kVx = s_jumpTapCurve.m_vx;

	const float dy = posLs.Y();
	if (dy > -NDI_FLT_EPSILON)
		return false;

	const float fallTime = Sqrt(2.0f * Max(dy / kG, 0.0f));

	if (fallTime < NDI_FLT_EPSILON)
		return false;

	const float maxX = kVx * fallTime;

	return Abs(posLs.Z()) <= maxX;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawJumpTapCurveWs(const Locator& jumpTopWs,
							 Point_arg bottomPosWs,
							 Color clr,
							 DebugPrimTime tt /* = kPrimDuration1FrameAuto */)
{
	STRIP_IN_FINAL_BUILD;

	static CONST_EXPR size_t kNumSteps = 16;

	const float kG = s_jumpTapCurve.m_g;
	const float kVx = s_jumpTapCurve.m_vx;

	const Point bottomPosLs = jumpTopWs.UntransformPoint(bottomPosWs);

	const float dy = bottomPosLs.Y();

	if (dy > -NDI_FLT_EPSILON)
		return;

	const float fallTime = Sqrt(2.0f * Max(dy / kG, 0.0f));

	if (fallTime < NDI_FLT_EPSILON)
		return;

	const float dx = fallTime * kVx;

	Point prevPosLs = kOrigin;

	PrimServerWrapper ps(jumpTopWs);
	ps.SetDuration(tt);
	ps.EnableHiddenLineAlpha();

	for (U32F i = 0; i < kNumSteps; ++i)
	{
		const float pcnt = Limit01(float(i) / float(kNumSteps - 1));
		const float curTime = pcnt * fallTime;
		const float x = dx * pcnt;
		const float y = 0.5f * kG * curTime * curTime;

		const Point posLs = Point(0.0f, y, x);

		ps.DrawLine(prevPosLs, posLs, clr);
		prevPosLs = posLs;
	}
}
