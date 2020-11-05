/*
 * Copyright (c) 2003-2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/armik.h"

#include "corelib/math/solve-triangle.h"
#include "corelib/math/sphere.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/msg.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/nd-options.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/nav/action-pack.h"

/// --------------------------------------------------------------------------------------------------------------- ///
const StringId64 ArmIkChain::kJointIds[2][kJointCount] =
{
	{
		SID("root"),
		SID("l_shoulder"),
		SID("l_elbow"),
		SID("l_wrist"),
		SID("l_hand_prop_attachment"),
	},
	{
		SID("root"),
		SID("r_shoulder"),
		SID("r_elbow"),
		SID("r_wrist"),
		SID("r_hand_prop_attachment"),
	},
};

/// --------------------------------------------------------------------------------------------------------------- ///
ArmIkChain::ArmIkChain()
{
	m_hackOverrideArmLen = false;
	m_type = kTypeChainArm;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ArmIkChain::Init(NdGameObject* pGo, ArmIndex armIndex, bool includeRoot, bool includePropAttach)
{
	m_armIndex = (I8)armIndex;

	StringId64 startJoint = kJointIds[m_armIndex][kShoulder];
	StringId64 endJoint = kJointIds[m_armIndex][kWrist];

	if (includeRoot)
		startJoint = kJointIds[m_armIndex][kRoot];

	if (includePropAttach)
		endJoint = kJointIds[m_armIndex][kPropAttach];

	if (!JointChain::Init(pGo, startJoint, endJoint))
		return false;

	for (int i = 0; i < kJointCount; i++)
	{
		m_jointOffsets[i] = (I8)FindJointOffset(kJointIds[m_armIndex][i]);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ArmIkChain::Init(const ArtItemSkeleton* pSkel, ArmIndex armIndex, bool includeRoot, bool includePropAttach)
{
	m_armIndex = (I8)armIndex;

	StringId64 startJoint = kJointIds[m_armIndex][kShoulder];
	StringId64 endJoint = kJointIds[m_armIndex][kWrist];

	if (includeRoot)
		startJoint = kJointIds[m_armIndex][kRoot];

	if (includePropAttach)
		endJoint = kJointIds[m_armIndex][kPropAttach];

	if (!JointChain::Init(pSkel, startJoint, endJoint))
		return false;

	for (int i = 0; i < kJointCount; i++)
	{
		m_jointOffsets[i] = (I8)FindJointOffset(kJointIds[m_armIndex][i]);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ArmIkChain::SetHackOverrideArmLen(float upperArmLen, float lowerArmLen)
{
	m_hackOverrideArmLen = true;
	m_hackUpperArmLen = upperArmLen;
	m_hackLowerArmLen = lowerArmLen;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SolveArmIk(ArmIkInstance* ik)
{
	if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugDisableArmFootIk || g_animOptions.m_ikOptions.m_disableAllIk))
		return true;

	PROFILE(Processes, JointChainIkInstance_Solve);

	JointSet* pJointSet = ik->m_ikChain;

	ANIM_ASSERT(pJointSet);
	ANIM_ASSERT(IsFinite(ik->m_goalPosWs));

	I32F shoulderOffset = -1;
	I32F elbowOffset = -1;
	I32F wristOffset = -1;

	const I32F jointSetType = pJointSet->GetType();
	if (jointSetType == JointSet::kTypeChainArm)
	{
		ArmIkChain *pIkChain = static_cast<ArmIkChain*>(pJointSet);
		shoulderOffset = pIkChain->ShoulderOffset();
		elbowOffset = pIkChain->ElbowOffset();
		wristOffset = pIkChain->WristOffset();
	}
	else
	{
		ANIM_ASSERT(ik->m_armIndex == kLeftArm || ik->m_armIndex == kRightArm);
		shoulderOffset = pJointSet->FindJointOffset(ArmIkChain::kJointIds[ik->m_armIndex][ArmIkChain::kShoulder]);
		elbowOffset = pJointSet->FindJointOffset(ArmIkChain::kJointIds[ik->m_armIndex][ArmIkChain::kElbow]);
		wristOffset = pJointSet->FindJointOffset(ArmIkChain::kJointIds[ik->m_armIndex][ArmIkChain::kWrist]);
	}

	if ((shoulderOffset < 0) || (elbowOffset < 0) || (wristOffset < 0))
	{
		return false;
	}

	ik->m_jointOffsetsUsed[0] = shoulderOffset;
	ik->m_jointOffsetsUsed[1] = elbowOffset;
	ik->m_jointOffsetsUsed[2] = wristOffset;

	const Point posElbowWs = pJointSet->GetJointLocWs(elbowOffset).Pos();
	const Vector elbowPoleVecWs = -GetLocalY(pJointSet->GetJointLocWs(elbowOffset).Rot());
	const Point posShoulderWs = pJointSet->GetJointLocWs(shoulderOffset).Pos();
	const Point posWristWs = pJointSet->GetJointLocWs(wristOffset).Pos();

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_drawArmIkSourcePos))
	{
		g_prim.Draw(DebugCross(posElbowWs, 0.1f, kColorYellow, PrimAttrib(kPrimDisableDepthTest)));
		g_prim.Draw(DebugString(posElbowWs, "ik:elbow", kColorWhite, 0.7f));
		g_prim.Draw(DebugCross(posShoulderWs, 0.1f, kColorYellow, PrimAttrib(kPrimDisableDepthTest)));
		g_prim.Draw(DebugString(posShoulderWs, "ik:shoulder", kColorWhite, 0.7f));
		g_prim.Draw(DebugCross(posWristWs, 0.1f, kColorYellow, PrimAttrib(kPrimDisableDepthTest)));
		g_prim.Draw(DebugString(posWristWs, "ik:wrist", kColorWhite, 0.7f));
	}

	const float lenUpperArm = pJointSet->GetChainLength(shoulderOffset, elbowOffset);
	const float lenLowerArm = pJointSet->GetChainLength(elbowOffset, wristOffset);

	//-----------------------------------------------------------------------
	// compute the plane for the arm to be aligned in
	//-----------------------------------------------------------------------

	Vector shoulderToElbow = Normalize(posElbowWs - posShoulderWs);

	Vector elbowToWrist = Normalize(posWristWs - posElbowWs);

	Vector planeAxis = elbowPoleVecWs;
	float axisLength = Length(planeAxis);
	planeAxis = Normalize(planeAxis);

	//-----------------------------------------------------------------------
	// compute the elbow joint angle
	//-----------------------------------------------------------------------

	// find the distance from the wrist to the goal position
	const float t = Dist(posShoulderWs, posElbowWs); //ik->m_pSetup->lenUpperArm;
	const float c = Dist(posElbowWs, posWristWs); //ik->m_pSetup->lenLowerArm;

	const Vector toGoal = ik->m_goalPosWs - posShoulderWs;

	float l = float(Length(toGoal));
	if (ik->m_tt > 0.5f)
	{
		if (l > (t + c) * 0.99f || l < Abs(t - c))
		{
			if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_showBrokenArmIK))
			{
				g_prim.Draw(DebugSphere(ik->m_goalPosWs, 0.1f, kColorRed));
			}

			if (ik->m_abortIfCantSolve)
			{
				return false;
			}
		}
	}

	l = Min(l, (t + c));

	if (l == c && t == c)
	{
		return false;
	}

	sTriangleAngles triangle = SolveTriangle(l, c, t);
	const Vector elbowCross = Cross(elbowToWrist, -shoulderToElbow);
	const Scalar elbowCrossLength = Length(elbowCross);
	if (elbowCrossLength > SCALAR_LC(0.01f))
	{
		planeAxis = elbowCross / elbowCrossLength;
	}

	//Get the elbow angle to the desired angle
	const Scalar startElbowAngle = SafeAcos(Dot(-shoulderToElbow, elbowToWrist));
	const Quat elbowRotor = QuatFromAxisAngle(planeAxis, startElbowAngle - triangle.a);
	pJointSet->RotateJointWs(elbowOffset, Slerp(kIdentity, elbowRotor, ik->m_tt));

	//Point the wrist at the goal
	const Point newWrist = pJointSet->GetJointLocWs(wristOffset).GetTranslation();
	const Vector toNewWrist = SafeNormalize(newWrist - posShoulderWs, kUnitXAxis);
	const Vector toGoalDir = SafeNormalize(toGoal, kUnitXAxis);
	const Quat shoulderRotor = QuatFromVectors(toNewWrist, toGoalDir);
	pJointSet->RotateJointWs(shoulderOffset, Slerp(kIdentity, shoulderRotor, ik->m_tt));

	ik->m_outputGoalPosWs = pJointSet->GetJointLocWs(wristOffset).Pos();

	//-----------------------------------------------------------------------
	// Debugging
	//-----------------------------------------------------------------------
	if (false)
	{
		Vector offset(0,0,0);

		Vector newUpperArm = -SafeNormalize(pJointSet->GetJointLocWs(shoulderOffset).GetPosition() - pJointSet->GetJointLocWs(elbowOffset).GetPosition(), kZero);
		Vector newLowerArm = -SafeNormalize(pJointSet->GetJointLocWs(elbowOffset).GetPosition() - pJointSet->GetJointLocWs(wristOffset).GetPosition(), kZero);

		//MsgCon("elbowAngle = %f, shoulderAngle = %f\n", elbowAngle, shoulderAngle);
		//MsgCon("axislength: %f\n", (float)Length(prevPoleVec));
		MsgCon("arm ik tt: %f\n", ik->m_tt);
		MsgCon("axislength: %f\n", axisLength);
		MsgCon("startElbowAngle = %f\n", (float)RadiansToDegrees(startElbowAngle));
		MsgCon("goalElbowAngle = %f\n", (float)RadiansToDegrees(triangle.a));
		MsgCon("realElbowAngle = %f\n", (float)RadiansToDegrees(PI - Acos(Dot(newUpperArm, newLowerArm))));
		MsgCon("toGoal length = %f, final length = %f\n", l, float(Length(ik->m_outputGoalPosWs - posShoulderWs)));
		MsgCon("Error = %f\n", float(Dist(ik->m_outputGoalPosWs, ik->m_goalPosWs)));

		newUpperArm *= lenUpperArm;
		newLowerArm *= lenLowerArm;

		g_prim.Draw(DebugLine(posShoulderWs + offset, posShoulderWs + offset + newUpperArm, g_colorBlue));
		g_prim.Draw(DebugLine(posShoulderWs + offset + newUpperArm,
							  posShoulderWs + offset + newUpperArm + newLowerArm,
							  g_colorBlue));
		DebugDrawVector(posElbowWs, planeAxis, g_colorWhite);
		g_prim.Draw(DebugSphere(Sphere(posShoulderWs + offset + newUpperArm + newLowerArm, 0.03f), g_colorBlue));
		g_prim.Draw(DebugSphere(Sphere(ik->m_outputGoalPosWs + offset, 0.05f), g_colorYellow));
		g_prim.Draw(DebugSphere(Sphere(ik->m_goalPosWs + offset, 0.04f), g_colorWhite));
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
WeaponIK::WeaponIK()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WeaponIK::Init(Character* pCharacter)
{
	// now setup the arm ik
	m_armIks[kLeftArm].Init(pCharacter, kLeftArm, false, true);
	m_armIks[kRightArm].Init(pCharacter, kRightArm, false, true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WeaponIK::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	m_armIks[kLeftArm].Relocate(deltaPos, lowerBound, upperBound);
	m_armIks[kRightArm].Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool WeaponIK::Apply(float scale, FgAnimData& animData, float fade, U32 hand, bool abortIfCantSolve /* = false */)
{
	PROFILE(Processes, WeaponIK_Apply);

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_ikOptions.m_disableAllIk))
		return false;

	if (fade == 0.0f)
		return false;

	const float curvedFade = -2.0f*fade*fade*fade + 3.0f*fade*fade;

	if (curvedFade <= 0.0f)
		return false;

	ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);	// For JointChain allocations

	m_armIks[kLeftArm].ReadJointCache();
	m_armIks[kRightArm].ReadJointCache();

	const Locator& rightWristLoc = m_armIks[hand].GetWristLocWs();
	const Locator& leftWristLoc = m_armIks[!hand].GetWristLocWs();
	const Locator& rightPropLoc = m_armIks[hand].GetPropAttachLocWs();
	const Locator& leftPropLoc = m_armIks[!hand].GetPropAttachLocWs();

	ANIM_ASSERT(IsFinite(rightWristLoc.GetTranslation()));
	ANIM_ASSERT(IsFinite(leftWristLoc.GetTranslation()));
	ANIM_ASSERT(IsFinite(rightPropLoc.GetTranslation()));
	ANIM_ASSERT(IsFinite(leftPropLoc.GetTranslation()));

	const Transform rightProp = rightPropLoc.AsTransform();
	const Transform leftProp = leftPropLoc.AsTransform();
	const Transform rightWrist = rightWristLoc.AsTransform();
	const Transform leftWrist = leftWristLoc.AsTransform();

	//g_prim.Draw( DebugCoordAxes(rightProp));
	//g_prim.Draw( DebugCoordAxes(leftProp));

	Transform toLeftWristTransform = leftWrist * Inverse(leftProp);
	Transform finalTransform = toLeftWristTransform * rightProp;
	RemoveScale(&finalTransform);

	Locator finalLocator = Locator(finalTransform);

	ArmIkInstance instance;
	instance.m_ikChain = &m_armIks[hand^1];
	instance.m_goalPosWs = finalLocator.GetTranslation();
	instance.m_tt = curvedFade;
	instance.m_abortIfCantSolve = false;

	ANIM_ASSERT(IsFinite(finalLocator.GetTranslation()));

	bool success = false;

	if (instance.m_tt > 0.0f)
	{
		success = SolveArmIk(&instance);

		if (success || !abortIfCantSolve)
		{
			// make our final wrist joint the desired rotation
			Quat newWristQuat = m_armIks[hand^1].GetWristLocWs().Rot();
			Quat postRotate = Conjugate(newWristQuat)*finalLocator.GetRotation();
			postRotate = Normalize(postRotate);

			Quat fadedPostRotate = Slerp(Quat(kIdentity), postRotate, instance.m_tt);

			m_armIks[hand^1].PostRotateJointLs(m_armIks[hand^1].WristOffset(), fadedPostRotate);
		}
	}

	m_armIks[kLeftArm].WriteJointCache();
	m_armIks[kRightArm].WriteJointCache();

	return success;
}


/// --------------------------------------------------------------------------------------------------------------- ///
WeaponIKFeatherInfo::WeaponIKFeatherInfo(Character* pCharacter, StringId64 ikSettingsId)
{
	JacobianMap::EndEffectorDef effs[] =
	{
		JacobianMap::EndEffectorDef(SID("r_hand_prop_attachment"), IkGoal::kRotation),
		JacobianMap::EndEffectorDef(SID("r_hand_prop_attachment"), IkGoal::kPosition),
		JacobianMap::EndEffectorDef(SID("l_hand_prop_attachment"), IkGoal::kRotation),
		JacobianMap::EndEffectorDef(SID("l_hand_prop_attachment"), IkGoal::kPosition),
	};

	m_arms[kLeftArm].Init(pCharacter, SID("spined"), false, 1, SID_VAL("l_hand_prop_attachment"));
	m_arms[kLeftArm].InitIkData(ikSettingsId);
	m_ikJacobianMaps[kLeftArm].Init(&m_arms[kLeftArm], SID("spined"), 2, &effs[2]);

	m_arms[kRightArm].Init(pCharacter, SID("spined"), false, 1, SID_VAL("r_hand_prop_attachment"));
	m_arms[kRightArm].InitIkData(ikSettingsId);
	m_ikJacobianMaps[kRightArm].Init(&m_arms[kRightArm], SID("spined"), 2, effs);
}

void WeaponIKFeatherInfo::Relocate( ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound )
{
	m_arms[kLeftArm].Relocate(deltaPos, lowerBound, upperBound);
	m_arms[kRightArm].Relocate(deltaPos, lowerBound, upperBound);

	m_ikJacobianMaps[kLeftArm].Relocate(deltaPos, lowerBound, upperBound);
	m_ikJacobianMaps[kRightArm].Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
WeaponIKFeather::WeaponIKFeather(WeaponIKFeatherInfo* pIkInfo) : ParentClass() {}

/// --------------------------------------------------------------------------------------------------------------- ///
bool WeaponIKFeather::Apply(WeaponIKFeatherInfo* pIkInfo,
							float scale,
							FgAnimData& animData,
							float fade,
							U32 hand,
							float handBlend,
							bool abortIfCantSolve)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_ikOptions.m_disableAllIk))
		return false;

	if (fade == 0.0f)
		return false;

	const float curvedFade = -2.0f*fade*fade*fade + 3.0f*fade*fade;

	if (curvedFade <= 0.0f)
		return false;

	{
		PROFILE(Player, WeaponIK_FeatherSeparate);
		ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);	// For JointChain allocations

		for (int i = 0; i < 2; i++)
		{
			pIkInfo->m_arms[i].ReadJointCache();
			pIkInfo->m_arms[i].UpdateIkData();
		}

		JointSet* apJoints[2] = { &pIkInfo->m_arms[0], &pIkInfo->m_arms[1] };
		JacobianMap* apJacobians[2] = { &pIkInfo->m_ikJacobianMaps[0], &pIkInfo->m_ikJacobianMaps[1] };
		SolveWeaponFeatherIk(apJoints, apJacobians, hand, handBlend, curvedFade, 18, 0.90f, 4);

		pIkInfo->m_arms[kRightArm].WriteJointCache();
		pIkInfo->m_arms[kLeftArm].WriteJointCache();

		bool brokenShoulders = false;
		for (int iArm = 0; iArm < 2; iArm++)
		{
			JacobianMap& map = pIkInfo->m_ikJacobianMaps[iArm];
			for (int iJoint = 0; iJoint < map.m_numUniqueJoints; iJoint++)
			{
				if (map.m_uniqueJoints[iJoint].m_jointName == SID("r_shoulder") ||
					map.m_uniqueJoints[iJoint].m_jointName == SID("l_shoulder"))
				{
					Scalar rot = RadiansToDegrees(SafeAcos(Dot(kIdentity, map.m_uniqueJoints[iJoint].m_ikOffset.GetRotation())));
					//MsgCon("arm: %d joint: %d rotation: %f\n", iArm, iJoint, rot);
					if (rot > SCALAR_LC(30.0f))
					{
						brokenShoulders = true;
					}
					break;
				}
			}
		}
		if (brokenShoulders)
		{
			//MsgCon("Resetting because of broken shoulders!\n");
			pIkInfo->m_ikJacobianMaps[0].ResetJointIkOffsets();
			pIkInfo->m_ikJacobianMaps[1].ResetJointIkOffsets();
		}
	}

	ParentClass::Apply(scale, animData, fade, hand, abortIfCantSolve);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WeaponIKFeather::SolveWeaponFeatherIk(JointSet* (&apJoints)[kArmCount],
										   JacobianMap* (apJacobians)[kArmCount],
										   U32 hand,
										   float handBlend,
										   float ikFade,
										   int maxIter,
										   float ikRestorePct,
										   int minNumIter /* = -1 */)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_ikOptions.m_disableAllIk))
		return;

	ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);
	JacobianIkInstance armIks[kArmCount];
	JacobianSolverContext solverContext[kArmCount];

	const I32F rPropIndex = apJoints[kRightArm]->FindJointOffset(SID("r_hand_prop_attachment"));
	const I32F lPropIndex = apJoints[kLeftArm]->FindJointOffset(SID("l_hand_prop_attachment"));

	if ((rPropIndex < 0) || (lPropIndex < 0))
	{
		return;
	}

	for (I32F i = 0; i < kArmCount; i++)
	{
		JacobianIkInstance& ik = armIks[i];
		ik.m_pJoints	  = apJoints[i];
		ik.m_pJacobianMap = apJacobians[i];
		ik.m_pJointLimits = nullptr;
		ik.m_errTolerance = 0.001f;
		ik.m_maxDeltaAngleDeg	  = 0.0f;
		ik.m_disableJointLimits	  = false;
		ik.m_debugDrawJointLimits = true;
		ik.m_pConstraints		  = nullptr;
		ik.m_maxIterations		  = 1;
		ik.m_restoreFactor		  = ikRestorePct;
		ik.m_blend = ikFade;

		solverContext[i] = BeginSolve(&ik, nullptr);
	}

	float handLerp = (hand == kLeftArm) ? handBlend : (1.0f - handBlend);
	float maxErr = 0.0f;
	int lastIter = 0;
	const I32F minIter = Max(minNumIter, 2);

	for (I32F i = 0; i < maxIter; i++)
	{
		lastIter = i;
		const Locator rightLocWs = apJoints[kRightArm]->GetJointLocWs(rPropIndex);
		const Locator leftLocWs = apJoints[kLeftArm]->GetJointLocWs(lPropIndex);

		const Locator avgLocWs = Lerp(rightLocWs, leftLocWs, handLerp);

		maxErr = 0.0f;
		bool solved = true;
		for (int arm = 0; arm < 2; arm++)
		{
			JacobianIkInstance& ik = armIks[arm];
			ik.m_goal[0].SetGoalRotation(avgLocWs.GetRotation());
			ik.m_goal[1].SetGoalPosition(avgLocWs.GetTranslation());
			JacobianIkResult result;
			IterateSolver(solverContext[arm], result);

			maxErr = Max(maxErr, result.m_sqrErr);
			solved = solved && result.m_solved;
		}

		if (maxErr < 0.002f && (i >= (minIter - 1)) && solved)
		{
			break;
		}
	}

	for (I32F i = 0; i < kArmCount; i++)
	{
		FinishSolve(solverContext[i]);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WeaponIKFeather::ResetOffsets(WeaponIKFeatherInfo* pIkInfo)
{
	if (pIkInfo)
	{
		pIkInfo->m_ikJacobianMaps[0].ResetJointIkOffsets();
		pIkInfo->m_ikJacobianMaps[1].ResetJointIkOffsets();
	}
}
