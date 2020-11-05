/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nav/nav-blocker-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
struct FindBestNavMeshParams;
class NavPoly;
class ActionPack;
class TraversalActionPack;
class Level;
class PlatformControl;
struct ActionPackRegistrationParams;

namespace DC
{
	typedef I32 VarTapAnimType;
	struct VarTapTable;
}

#if ENABLE_NAV_LEDGES
struct FindNavLedgeGraphParams;
class NavLedge;
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPack* GetActionPackFromSpawner(const EntitySpawner* pSpawner);
ActionPack* GetActionPackFromSpawnerId(StringId64 spawnerId);

/// --------------------------------------------------------------------------------------------------------------- ///
DC::VarTapAnimType BuildVarTapAnimType(StringId64 typeId);
bool IsGroundVarTapAnimType(const DC::VarTapAnimType animType);
bool VarTapAnimTypesCompatible(const DC::VarTapAnimType typeA, const DC::VarTapAnimType typeB);

/// --------------------------------------------------------------------------------------------------------------- ///
void FindActionPackNavMesh(const ActionPack* pAp,
						   FindBestNavMeshParams& params,
						   const Level* pAllocLevel,
						   const PlatformControl* pPlatformControl,
						   bool drawFailure);

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackCanRegisterToPoly(StringId64 apNameId,
								 const ActionPackRegistrationParams& params,
								 const BoundFrame& apLoc,
								 const PlatformControl* pPlatformControl = nullptr,
								 const NavPoly** ppOutNavPoly = nullptr);

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
								 const Level* pLevel,
								 const PlatformControl* pPlatformControl = nullptr,
								 const NavPoly** ppOutNavPoly = nullptr);

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
void FindActionPackNavLedgeGraph(const ActionPack* pAp,
								 FindNavLedgeGraphParams& params,
								 const Level* pAllocLevel,
								 const PlatformControl* pPlatformControl,
								 bool drawFailure);
#endif // ENABLE_NAV_LEDGES

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackCanRegisterToLedge(const ActionPackRegistrationParams& params,
								  const BoundFrame& apLoc,
								  const PlatformControl* pPlatformControl = nullptr,
								  const NavLedge** ppOutNavLedge = nullptr);
#endif // ENABLE_NAV_LEDGES

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackCanRegisterToLedge(const BoundFrame& apLoc,
								  Point_arg regPosLs,
								  StringId64 bindSpawnerNameId,
								  float searchRadius,
								  bool drawFailure,
								  const Level* pLevel,
								  const PlatformControl* pPlatformControl = nullptr,
								  const NavLedge** ppOutNavLedge = nullptr);
#endif // ENABLE_NAV_LEDGES

/// --------------------------------------------------------------------------------------------------------------- ///
void DrawDebugFlatArc(const Locator& pivotLocWs, Point_arg centerPosWs, float halfAngleDeg, Color color, float width);

/// --------------------------------------------------------------------------------------------------------------- ///
void DrawDebugFlatArc(const Locator& pivotLocWs,
					  Point_arg centerPosWs,
					  float cwAngleDeg,
					  float ccwAngleDeg,
					  Color color,
					  float width);

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::VarTapTable* GetVarTapTable(StringId64 id, U64 instanceSeed = 0);
const DC::VarTapTable* GetVarTapTable(const TraversalActionPack* pTap, U64 instanceSeed = 0);

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsUnderJumpTapCurveLs(Point_arg posLs);
void DebugDrawJumpTapCurveWs(const Locator& jumpTopWs,
							 Point_arg bottomPosWs,
							 Color clr,
							 DebugPrimTime tt = kPrimDuration1FrameAuto);
