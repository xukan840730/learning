/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-defines.h"

#include "gamelib/gameplay/nav/nav-command.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NavCharacter;

/// --------------------------------------------------------------------------------------------------------------- ///
struct WaypointContract
{
	Quat m_rotPs = kIdentity;
	NavGoalReachedType m_reachedType = kNavGoalReachedTypeMax;
	MotionType m_motionType = kMotionTypeMax;
	bool m_exactApproach	= false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
float GetCurrentWaypointAngleDeg(const NavCharacter* pNavChar, const WaypointContract* pContract);
const Vector CalculateFaceDirXzPs(const NavCharacter* pNavChar);
bool IsWaypointContractOrientationSatisfied(const NavCharacter* pNavChar, const WaypointContract* pContract);
bool TestWaypointContractsEqual(const WaypointContract& lhs, const WaypointContract& rhs);

