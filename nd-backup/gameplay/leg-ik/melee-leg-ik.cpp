/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */
#include "gamelib/gameplay/leg-ik/melee-leg-ik.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/footik.h"
#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/netbridge/mail.h"

#include "gamelib/gameplay/leg-ik/leg-ik.h"
#include "gamelib/gameplay/character.h"



class Character;
class CharacterLegIkController;

//---------------------------------------------------------------------------------------
// Melee Leg IK Implementation
//---------------------------------------------------------------------------------------

void MeleeLegIk::Start(Character* pCharacter)
{
	ILegIk::Start(pCharacter);

	m_rootDeltaSet = false;

	m_desiredRootDelta = 0;
	m_ikInfoValid = false;
}

void MeleeLegIk::SetRootShiftDelta(float delta)
{
	ANIM_ASSERT(IsFinite(delta) && delta == delta);
	m_desiredRootDelta = delta;
	m_rootDeltaSet = true;
}

void MeleeLegIk::SetFeetDeltas(float leftFootDelta, float rightFootDelta, float frontLeftFootDelta, float frontRightFootDelta)
{
	m_leftFootDelta = leftFootDelta;
	m_rightFootDelta = rightFootDelta;
	m_frontLeftFootDelta = frontLeftFootDelta;
	m_frontRightFootDelta = frontRightFootDelta;
	m_feetDeltasSet = true;
}

MeleeRootShift ComputeMeleeRootShift(const MeleeIkInfo& info, bool player)
{
	const float leftLegDelta = info.m_probePos[kLeftLeg] - info.m_footPos[kLeftLeg];
	const float rightLegDelta = info.m_probePos[kRightLeg] - info.m_footPos[kRightLeg];

	float initialRootShift = Min(rightLegDelta, leftLegDelta);

	float maxLegY = Max(info.m_probePos[kLeftLeg], info.m_probePos[kRightLeg]);
	float minLegY = Min(info.m_probePos[kLeftLeg], info.m_probePos[kRightLeg]);

	const float maxLegToRootY = info.m_rootPos - maxLegY;

	const float minRootDelta = (maxLegY + (player ? 0.3f : 0.1f)) - info.m_rootPos; // allow root to go as low as 0.1m above top foot. For player we use 0.3

	//const float clampedMaxRootY = Max(0.5f, maxLegToRootY);

	//const float minRootDelta = minLegToRootY + minLegY - info.m_rootPos;
	//const float maxRootDelta = maxLegToRootY + maxLegY - info.m_rootPos; // this actually equals to 0!

	const float minLegToRootY = info.m_rootPos - minLegY; // distance (should be > 0) from lowest foot to root. This is the most stretched leg. See if we can stretch it more

	//MsgConPauseable("IK: ComputeMeleeRootShift: minLegToRootY: %.2f, maxLegToRootY %.2f\n", minLegToRootY, maxLegToRootY);
	const float kMaxLegLength = player ? 0.8f : 0.9f;

	float maxRootDelta = (minLegY + kMaxLegLength) - info.m_rootPos; // lowest foot probe pos + offset is how far the root can go up
	maxRootDelta = Max(maxRootDelta, initialRootShift); // this should never happen, but just in case, make sure we will not modify our original desired shift

	//MsgConPauseable("IK: ComputeMeleeRootShift: Allowed low leg length %.2f\n", info.m_rootPos + maxRootDelta - minLegY);


	//const float maxRootDelta = maxLegToRootY + maxLegY - info.m_rootPos; // this actually equals to 0!


	float rootDelta = MinMax(initialRootShift, minRootDelta, maxRootDelta);

	MeleeRootShift result;
	result.m_min = minRootDelta;
	result.m_max = maxRootDelta;
	result.m_desired = rootDelta;

	return result;
}

MeleeIkInfo MeleeLegIk::GetMeleeIkInfo(Character* pCharacter) const
{
	if (m_ikInfoValid)
	{
		MeleeIkInfo result;

		const int legCount = pCharacter->IsQuadruped() ? kQuadLegCount : kLegCount;
		for (int iLeg = 0; iLeg < legCount; ++iLeg)
		{
			result.m_footPos[iLeg] = m_ikInfo.m_footPos[iLeg];
			result.m_probePos[iLeg] = m_ikInfo.m_probePos[iLeg];
		}

		result.m_rootPos = m_ikInfo.m_rootPos;
		result.m_valid = true;
		return result;
	}
	else
		return MeleeIkInfo();
}

float MeleeLegIk::GetAdjustedRootDelta(const float desiredRootDelta)
{
	ANIM_ASSERT(IsFinite(desiredRootDelta) && desiredRootDelta == desiredRootDelta);
	ANIM_ASSERT(IsFinite(m_desiredRootDelta) && m_desiredRootDelta == m_desiredRootDelta);
	return m_rootDeltaSet ? m_desiredRootDelta : desiredRootDelta;
}

void MeleeLegIk::AdjustFeet(Point* pLeftLeg, Point* pRightLeg, Point* pFrontLeftLeg, Point* pFrontRightLeg)
{
	if (m_feetDeltasSet)
	{
		ANIM_ASSERT(pLeftLeg);
		*pLeftLeg += Vector(0.0f, m_leftFootDelta, 0.0f);

		ANIM_ASSERT(pRightLeg);
		*pRightLeg += Vector(0.0f, m_rightFootDelta, 0.0f);

		if (pFrontLeftLeg)
			*pFrontLeftLeg += Vector(0.0f, m_frontLeftFootDelta, 0.0f);

		if (pFrontRightLeg)
			*pFrontRightLeg += Vector(0.0f, m_frontRightFootDelta, 0.0f);
	}
}

void MeleeLegIk::AdjustedRootLimits(float& minRootDelta, float& maxRootDelta)
{
	minRootDelta = -100.0f;
	maxRootDelta =  100.0f;
}

void MeleeLegIk::AdjustRootPositionFromLegs(Character* pCharacter, CharacterLegIkController* pController, Point aLegs[], int legCount, float* pDesiredRootBaseY)
{
	m_ikInfoValid = true;

	ANIM_ASSERT(legCount == kQuadLegCount || legCount == kLegCount);
	m_ikInfo.m_rootPos = m_legIks[kLeftLeg]->GetRootLocWs().GetTranslation().Y();

	for (int iLeg = 0; iLeg < legCount; ++iLeg)
	{
		m_ikInfo.m_probePos[iLeg] = aLegs[iLeg].Y();
		m_ikInfo.m_footPos[iLeg] = m_legIks[iLeg]->GetAnkleLocWs().GetTranslation().Y();
	}

	MoveLegIk::AdjustRootPositionFromLegs(pCharacter, pController, aLegs, legCount, pDesiredRootBaseY);
}

void MeleeLegIk::Update(Character* pCharacter, CharacterLegIkController* pController, bool doCollision)
{
	MoveLegIk::Update(pCharacter, pController, doCollision);

	// clear all the flags
	m_rootDeltaSet = false;
	m_feetDeltasSet = false;
}

