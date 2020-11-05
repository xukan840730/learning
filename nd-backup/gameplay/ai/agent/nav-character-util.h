/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/ai/agent/nav-character-adapter.h"
#include "gamelib/gameplay/nav/nav-command.h"
#include "gamelib/scriptx/h/nd-ai-defines.h"
#include "gamelib/scriptx/h/nd-script-func-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class PathWaypointsEx;

namespace DC
{
	struct NpcMoveSetContainer;
	struct NpcMoveSetDef;
	struct NpcMotionTypeMoveSets;
} // namespace DC

/// --------------------------------------------------------------------------------------------------------------- ///
extern MotionType DcMotionTypeToGame(DC::NpcMotionType dcMotionType);
extern DC::NpcMotionType GameMotionTypeToDc(MotionType motionType);
extern NavGoalReachedType ConvertDcGoalReachedType(DC::NpcGoalReachedType dcGoalReachedType);
extern const char* MotionTypeToString(MotionType motionType);

/// --------------------------------------------------------------------------------------------------------------- ///
namespace Nav
{
	void UpdateSplineAdvancement(const Locator& charLocWs,
								 Vector_arg moveDirWs,
								 const CatmullRom* const pSpline,
								 const F32 splineArcGoal,
								 const F32 splineArcStep,
								 const F32 onSplineRadius,
								 const F32 splineAdvanceStartTolerance,
								 bool& outBeginSplineAdvancement,
								 F32& outCurArcLen,
								 bool debugDraw = false);

	bool IsOnSameNavSpace(const NavLocation& locA, const NavLocation& locB);

	CatmullRom* CreateApproachSpline(const Process* pOwner,
									 Point_arg startingPosWs,
									 const BoundFrame& targetLoc,
									 Vector_arg targetVelWs,
									 float cutoffRadius);

	bool IsMoveSetValid(const DC::NpcMoveSetDef* pMoveSet);

	U64 GetValidMotionTypes(bool wantToStrafe, const DC::NpcMoveSetDef* const* apMoveSets);
	MotionType GetRestrictedMotionTypeFromValid(const NavCharacter* pNavChar,
												const MotionType desiredMotionType,
												bool wantToStrafe,
												const U64 validBits);

	const DC::NpcMoveSetDef* PickMoveSet(const NavCharacter* pNavChar, const DC::NpcMoveSetContainer* pMoveSetContainer);
	const DC::NpcMoveSetContainer* PickMoveSetSubcat(const DC::NpcMotionTypeMoveSets& mtMoveSets,
													 StringId64 requestedSubcat,
													 StringId64* pActualSubcatOut);

	void SplitPathAfterTap(const PathWaypointsEx& orgPath, PathWaypointsEx* pPrePathOut, PathWaypointsEx* pPostPathOut);
} // namespace Nav
