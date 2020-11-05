/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */
#include "gamelib/gameplay/leg-ik/move-leg-ik.h"

#include "corelib/math/intersection.h"
#include "gamelib/gameplay/character-leg-ik.h"
#include "gamelib/gameplay/character-leg-raycaster.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/leg-ik/anim-ground-plane.h"
#include "gamelib/gameplay/leg-ik/character-leg-ik-controller.h"
#include "gamelib/gameplay/leg-ik/move-leg-ik-new.h"
#include "ndlib/anim/footik.h"
#include "ndlib/anim/joint-modifiers/joint-modifier-data.h"
#include "ndlib/util/tracker.h"


//---------------------------------------------------------------------------------------
// Move Leg IK Implementation
//---------------------------------------------------------------------------------------
void MoveLegIk::Start(Character* pCharacter)
{
	ILegIk::Start(pCharacter);
}

DC::LegIkConstants* g_pDebugLegIKConstants = nullptr;
//LegIKConstants g_debugLegIKConstants;
bool g_useDebugLegIKConstants;



Point MoveLegIk::ApplyLegToGround(Character* pCharacter, CharacterLegIkController* pController, const Plane& animationGround, int legIndex, Point legPoint, float baseY, bool& onGround, bool freezeLegIk)
{
	PROFILE(Processes, MoveLegIk_ApplyLeg2Grnd);

	CHECK_LEG_INDEX(legIndex);

	// default to the ik chain's data
	Point trueLegPoint = m_legIks[legIndex]->GetAnkleLocWs().GetTranslation();

	Point groundPointEstimateAnim;
	Scalar animTT = LinePlaneIntersect(animationGround.ProjectPoint(pCharacter->GetTranslation()), animationGround.GetNormal(), trueLegPoint, trueLegPoint - Vector(kUnitYAxis), nullptr, &groundPointEstimateAnim);
	//ASSERT(animTT >= 0.0f);

	bool noAnimationPlane = pController->m_mode == CharacterLegIkController::kModeMovingNonPredictive;

	if (noAnimationPlane)
	{
		LinePlaneIntersect(pCharacter->GetTranslation(), Vector(kUnitYAxis), trueLegPoint, trueLegPoint - Vector(kUnitYAxis), nullptr, &groundPointEstimateAnim);
	}

	const DC::LegIkConstants& constants = *pCharacter->GetLegIKConstants();

	const float baseOffsetFromDefault = trueLegPoint.Y() - groundPointEstimateAnim.Y();

	const float offset = Min(constants.m_maxOffset, baseOffsetFromDefault);

	float deltaY = pCharacter->GetDeltaFromLastFramePs().GetTranslation().Y();

	float footPrintOffset = 0.0f;
	if (pCharacter->GetLegRaycaster()->IsFootprintSurface())
	{
		footPrintOffset = -0.035f/2.0f;
	}

	// find the distance to the ground
	Point newLegPoint = legPoint;
	Vector normal(animationGround.GetNormal());

	LegRaycaster::Results result = pController->m_legRaycaster.GetProbeResults(legIndex, 0.5f);
	if (result.m_hitGround)
	{
		normal = GetLocalZ(result.m_point.GetRotation());
	}
	//float g_debugIkFloorHeight = 0.5f;
	//result.m_point.SetTranslation(Point(result.m_point.GetTranslation().X(), g_debugIkFloorHeight, result.m_point.GetTranslation().Z()));
	//result.m_point.SetRotation(kIdentity);

	//g_prim.Draw(DebugCross(result.m_point.GetTranslation(), 0.3f, kColorCyan), kPrimDuration1FramePauseable);

	Point contactPoint = result.m_hitGround ? result.m_point.GetTranslation() : groundPointEstimateAnim;
	Point groundPointEstimate;
	
 	if (Dot(normal, kUnitYAxis) <= 0.707f)
 	{
 		normal = kUnitYAxis;
 	}

	if (!result.m_hitGround)
	{
		normal = kUnitYAxis;
		contactPoint = animationGround.ProjectPoint(pCharacter->GetTranslation());
	}

	LinePlaneIntersect(contactPoint, normal, trueLegPoint, trueLegPoint - Vector(kUnitYAxis), nullptr, &groundPointEstimate);
	

	//g_prim.Draw(DebugCross(groundPointEstimate, 0.2f, kColorOrange), kPrimDuration1FramePauseable);
	//g_prim.Draw(DebugCross(contactPoint, 0.4f, kColorRed), kPrimDuration1FramePauseable);
	//g_prim.Draw(DebugCross(groundPointEstimateAnim, 0.2f, kColorYellow), kPrimDuration1FramePauseable);

	if (!noAnimationPlane)
	{
		MoveLegIkNew::SmoothLegGroundPositions(pCharacter, pController, groundPointEstimate, legIndex);
	}


	//g_prim.Draw(DebugCross(groundPointEstimate, 0.2f, kColorRed), kPrimDuration1FramePauseable);

	newLegPoint += Vector(0.0f, groundPointEstimate.Y() - groundPointEstimateAnim.Y() + footPrintOffset, 0.0f);

	onGround = result.m_hitGround;

	float normalU = LerpScale(constants.m_normalUMin, constants.m_normalUMax, 1.0f, 0.0f, offset);

	AdjustFootNormal(pCharacter, pController, normal, legIndex, normalU * GetBlend(pController), animationGround.GetNormal());
	
	return Lerp(legPoint, newLegPoint, GetBlend(pController));
}

Vector MoveLegIk::LimitNormalAngle( Vector_arg normal, Vector_arg animationNormal )
{
	float dot = Dot(normal, animationNormal);
	float theta = Acos(MinMax(dot, -1.0f, 1.0f));
	float angle = RADIANS_TO_DEGREES(theta);

	const float kMaxAngle = 45.0f;

	if (angle > kMaxAngle)
	{
		float t = (angle - kMaxAngle) / angle;
		float sinTheta = Sin(theta);

		Vector newNormal = animationNormal * (Sin((1.0f - t) * theta) / sinTheta) + normal * (Sin(t * theta) / sinTheta);
		newNormal = SafeNormalize(newNormal, normal);

		return newNormal;
	}
	else
	{
		return normal;
	}
}

void MoveLegIk::AdjustFootNormal(Character* pCharacter, CharacterLegIkController* pController, Vector normal, int legIndex, float blend, Vector_arg animationGroundNormal)
{
	normal = LimitNormalAngle(normal, animationGroundNormal);

	CHECK_LEG_INDEX(legIndex);

	Vector footNormal = GetLocalZ(m_legIks[legIndex]->GetAnkleLocWs().GetRotation());

	// TODO@QUAD need to check joint rotation offset table
	if (legIndex % kLegCount == 0)
		footNormal = -footNormal;

	if (!pController->m_footNormalInited[legIndex])
	{
		pController->m_footNormal[legIndex] = normal;
		pController->m_footGroundNormal[legIndex] = normal;
		pController->m_footNormalInited[legIndex] = true;
	}
	else
	{
		pController->m_footGroundNormal[legIndex] = SafeNormalize(pController->m_footNormalSpring->Track(pController->m_footGroundNormal[legIndex], normal, GetProcessDeltaTime(), g_footNormalSpring), normal);
	}

	Quat startQuat = QuatFromLookAt(animationGroundNormal, kUnitZAxis);
	Quat normalQuat = QuatFromLookAt(pController->m_footGroundNormal[legIndex], kUnitZAxis);
	normalQuat = Slerp(startQuat, normalQuat, blend);
	Quat footQuat = QuatFromLookAt(footNormal, kUnitZAxis);
	Quat trans = Conjugate(startQuat) * normalQuat;
	footQuat = footQuat * trans;

	m_footNormal[legIndex] = GetLocalZ(footQuat);
}

void MoveLegIk::Update(Character* pCharacter, CharacterLegIkController* pController, bool doCollision)
{
	PROFILE(Processes, MoveLegIk_Update);

	const Locator& loc = pCharacter->GetLocator();

	Plane groundPlane = GetAnimatedGroundPlane(pCharacter);
	Locator primLoc(kIdentity);

	Point pointOnPlane = loc.GetTranslation() - groundPlane.GetNormal()*groundPlane.Dist(loc.GetTranslation());	
	Locator aLegLocs[kQuadLegCount];

	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		const Point startingLegWs = m_legIks[iLeg]->GetAnkleLocWs().GetTranslation();

		Point legProjGroundPlane;
		LinePlaneIntersect(pointOnPlane, groundPlane.GetNormal(), startingLegWs, startingLegWs - Vector(kUnitYAxis), nullptr, &legProjGroundPlane);

		const Point legIkTarget = ApplyLegToGround(pCharacter, pController, groundPlane, iLeg, startingLegWs, legProjGroundPlane.Y(), m_legOnGround[iLeg]);
		pController->SetFootOnGround(iLeg, m_legOnGround[iLeg]);

		// TODO@QUAD need to check joint rotation offset table
		const bool isLeftLeg = iLeg % kLegCount == 0;
		Vector footUp = GetLocalZ(m_legIks[iLeg]->GetAnkleLocWs().GetRotation());
		if (isLeftLeg)
			footUp = -footUp;

		Quat desiredRot = QuatFromVectors(footUp, m_footNormal[iLeg]) * m_legIks[iLeg]->GetAnkleLocWs().GetRotation();
		aLegLocs[iLeg] = Locator(legIkTarget, desiredRot);
	}

	DoLegIk(pCharacter, pController, aLegLocs, m_legCount, nullptr, Vector(kZero));
}
