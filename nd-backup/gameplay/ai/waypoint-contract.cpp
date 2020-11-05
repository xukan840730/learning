/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/ai/waypoint-contract.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/controller/nd-locomotion-controller.h"

/// --------------------------------------------------------------------------------------------------------------- ///
float GetCurrentWaypointAngleDeg(const NavCharacter* pNavChar, const WaypointContract* pContract)
{
	const Quat curRotPs = pNavChar->GetRotationPs();
	const Vector curZ = GetLocalZ(curRotPs);
	const Vector curX = GetLocalX(curRotPs);
	const Vector wayZ = VectorXz(GetLocalZ(pContract->m_rotPs));
	const float cosValue = Dot(curZ, wayZ);
	float curAngleRad = SafeAcos(cosValue);
	if (Dot(curX, wayZ) < Scalar(kZero))
	{
		curAngleRad *= -1.0f;
	}
	const float curAngleDeg = RADIANS_TO_DEGREES(curAngleRad);
	return curAngleDeg;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Vector CalculateFaceDirXzPs(const NavCharacter* pNavChar)
{
	const Point facePosPs = pNavChar->GetFacePositionPs();
	const Point myPosPs = pNavChar->GetTranslationPs();
	const Vector forwardPs = GetLocalZ(pNavChar->GetRotationPs());
	const Vector faceDirPs = AsUnitVectorXz(facePosPs - myPosPs, forwardPs);

	NAV_ASSERT(IsFinite(forwardPs));
	NAV_ASSERT(IsFinite(facePosPs));
	NAV_ASSERT(IsFinite(myPosPs));
	NAV_ASSERT(IsFinite(faceDirPs));

	return faceDirPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsWaypointContractOrientationSatisfied(const NavCharacter* pNavChar, const WaypointContract* pContract)
{
	if (pNavChar->GetEnteredActionPack() != nullptr)
		return true;

	// Are we oriented properly?
	if (pContract->m_reachedType == kNavGoalReachedTypeStop)
	{
		float curAngleDeg = GetCurrentWaypointAngleDeg(pNavChar, pContract);

		if (Abs(curAngleDeg) >= INdAiLocomotionController::kMinTurnInPlaceAngleDeg)
		{
			return false;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TestWaypointContractsEqual(const WaypointContract& lhs, const WaypointContract& rhs)
{
	if (lhs.m_motionType != rhs.m_motionType)
		return false;

	if (lhs.m_reachedType != rhs.m_reachedType)
		return false;

	const float dotP = Dot(lhs.m_rotPs, rhs.m_rotPs);

	if (dotP < 0.995f)
		return false;

	return true;
}
