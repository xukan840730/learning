/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/ik/joint-limits.h"

#include "corelib/memory/relocate.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/angle.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/io/joypad.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/process.h"
#include "ndlib/profiling/profile-cpu-categories.h"
#include "ndlib/profiling/profile-cpu.h"
#include "ndlib/render/display.h"
#include "ndlib/render/util/mini-gl.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"
#include "gamelib/scriptx/h/ik-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
JointLimits::JointData::JointData()
{
	memset(this, 0, sizeof(*this));
}

/// --------------------------------------------------------------------------------------------------------------- ///
JointLimits::JointSetMap::JointSetMap()
{
	m_numJoints = 0;
	m_jointSetOffset = nullptr;
	m_jointLimitIndex = nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointLimits::JointSetMap::Init(int numJoints)
{
	ANIM_ASSERT(m_jointSetOffset == nullptr);
	ANIM_ASSERT(m_jointLimitIndex == nullptr);

	m_numJoints = numJoints;
	m_jointSetOffset = NDI_NEW short[m_numJoints];
	m_jointLimitIndex = NDI_NEW short[m_numJoints];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointLimits::JointSetMap::Destroy()
{
	NDI_DELETE [] m_jointSetOffset;
	NDI_DELETE [] m_jointLimitIndex;
	m_jointSetOffset = nullptr;
	m_jointLimitIndex = nullptr;
	m_numJoints = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointLimits::JointSetMap::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_jointSetOffset, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_jointLimitIndex, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
JointLimits::JointLimits()
{
	m_jointData = nullptr;
	m_numJoints = 0;

#ifdef JOINT_LIMIT_DEBUG
	m_debugDraw = false;
	m_debugDrawFlags = kDebugDrawDefault;
	m_debugDrawConeSize = 0.2f;
	m_debugDisableLimitsDisable = false;
	m_debugDisableLimits = false;
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointLimits::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	for (int i = 0; i < m_numJoints; i++)
	{
		RelocatePointer(m_jointData[i].m_data.m_coneBoundaryPointsPs, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_jointData[i].m_data.m_coneSlicePlanePs, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_jointData[i].m_data.m_coneBoundaryPlanePs, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_jointData[i].m_data.m_coneBoundryTwistCenter, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_jointData[i].m_data.m_coneBoundryTwistRange, deltaPos, lowerBound, upperBound);
	}

	RelocatePointer(m_jointData, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimits::SetupJoints(NdGameObject* pGo, StringId64 dcIkConstraintId)
{
	const AnimControl* pAnimControl = pGo ? pGo->GetAnimControl() : nullptr;
	if (!pAnimControl)
		return false;

	const ArtItemSkeletonHandle hSkel = pAnimControl->GetAnimData().m_curSkelHandle;
	const AnimTable& animTable = pAnimControl->GetAnimTable();

	return SetupJoints(hSkel, &animTable, dcIkConstraintId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimits::SetupJoints(ArtItemSkeletonHandle hSkel, const AnimTable* pAnimTable, StringId64 dcIkConstraintId)
{
	//m_hGo = pGo;
	m_hSkel = hSkel;

	if (dcIkConstraintId != INVALID_STRING_ID_64)
		m_dcIkConstraints = IkConstraintsPtr(dcIkConstraintId, SID("ik-settings"));

	if (!m_dcIkConstraints.Valid() || m_dcIkConstraints->m_count == 0)
		return false;

	JointData* pOldJointData = nullptr;
	int oldJointCount = m_numJoints;

#ifdef JOINT_LIMIT_DEBUG
	m_debugRefreshMenu = true;
#endif

	// Either we're allocating from a Process::Init (always the case in FINAL_BUILD), or we're re-setting up because of a debug command.
	// Correct allocator will be setup in either case. No need to NDI_DELETE in final build.
	pOldJointData = m_jointData;
	m_numJoints = m_dcIkConstraints->m_count;
	m_jointData = NDI_NEW JointData[m_numJoints];

	memset(m_jointData, 0, m_numJoints*sizeof(JointData));

	for (int i = 0; i < m_numJoints; i++)
	{
		JointData* pJointData = &m_jointData[i];
		const DC::IkConstraint* pConstraint = &m_dcIkConstraints->m_array[i];

		bool wasSetup = false;

		wasSetup = SetupJointConstraintAnim(pAnimTable, pJointData, pConstraint);

		if (!wasSetup)
		{
			wasSetup = SetupJointConstraintConePoints(pJointData, pConstraint);
		}

		if (!wasSetup)
		{
			wasSetup = SetupJointConstraintEllipticalCone(pJointData, pConstraint);
		}

#ifdef JOINT_LIMIT_DEBUG
		pJointData->m_debugMirror = pConstraint->m_debugMirror;
		pJointData->m_data.m_debugFlags = pConstraint->m_debugFlags;

		if (wasSetup && pOldJointData)
		{
			// Restore debug draw settings
			for (int j=0; j<oldJointCount; j++)
			{
				if (pJointData->m_jointIndex == pOldJointData[j].m_jointIndex)
				{
					pJointData->m_debugDraw = pOldJointData[j].m_debugDraw;
					pJointData->m_debugMirror = pOldJointData[j].m_debugMirror;
					break;
				}
			}
		}
#endif
	}

	NDI_DELETE [] pOldJointData;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointLimits::SetupJointConstraintEllipticalCone(JointConstraintData* ikData, const DC::IkConstraint* pConstraint)
{
	int numCorners = pConstraint->m_coneNumCorners;

	const Vector coneCenterDir = pConstraint->m_coneCenterDir ? SafeNormalize(*pConstraint->m_coneCenterDir, kUnitZAxis) : kUnitZAxis;
	const Vector coneCenterDirPs = ikData->m_bindPoseLs.TransformVector(coneCenterDir);
	ikData->m_coneVisPointPs = coneCenterDirPs;

	const Vector bindPosDirPs = GetLocalZ(ikData->m_bindPoseLs.Rot());
	Vector bindPoseToCenterRotAxisPs = Cross(bindPosDirPs, ikData->m_coneVisPointPs);
	bindPoseToCenterRotAxisPs = SafeNormalize(bindPoseToCenterRotAxisPs, bindPosDirPs);
	float bindPoseToCenterDot = MinMax01(Dot(bindPosDirPs, ikData->m_coneVisPointPs));
	float bindPoseToCenterAngle = Acos(bindPoseToCenterDot);
	const Quat bindPoseToCenterRot = QuatFromAxisAngle(bindPoseToCenterRotAxisPs, bindPoseToCenterAngle);

	ikData->m_coneRotPs = Normalize(bindPoseToCenterRot * ikData->m_bindPoseLs.Rot());

	const Vector coneFwdDirPs = GetLocalZ(ikData->m_coneRotPs);
	float testDot = Dot(coneFwdDirPs, ikData->m_coneVisPointPs);

	float twistMin = DEGREES_TO_RADIANS(pConstraint->m_coneCenterTwistMin);
	float twistMax = DEGREES_TO_RADIANS(pConstraint->m_coneCenterTwistMax);

	float twistCenter = 0.5f*(twistMin + twistMax);
	float twistRange = (twistMin <= twistMax) ? twistMax - twistCenter : 0.0f;
	twistCenter = (twistCenter <= PI) ? twistCenter+PI_TIMES_2 : twistCenter;
	twistCenter = (twistCenter > PI) ? twistCenter-PI_TIMES_2 : twistCenter;
	ikData->m_visPointTwistCenter = twistCenter;
	ikData->m_visPointTwistRange = twistRange;

	float a = DEGREES_TO_RADIANS(pConstraint->m_coneScaleX*pConstraint->m_coneAngleRadius);
	float b = DEGREES_TO_RADIANS(pConstraint->m_coneScaleY*pConstraint->m_coneAngleRadius);
	for (int i=0; i<numCorners; i++)
	{
		float t = PI_TIMES_2*i/numCorners;
		float ellipseX = a*Cos(t);
		float ellipseY = b*Sin(t);
		float ellipseDist = Sqrt(ellipseX*ellipseX + ellipseY*ellipseY);

		float angle = Atan2(ellipseY, ellipseX);
		Vector axis = Rotate(QuatFromAxisAngle(kUnitZAxis, angle), kUnitYAxis);
		Quat rot = QuatFromAxisAngle(axis, ellipseDist);
		Vector dir = Rotate(rot, kUnitZAxis);

		Vector dirPs = Rotate(ikData->m_coneRotPs, dir);

		ikData->m_coneBoundaryPointsPs[i] = dirPs;
	}

	for (int i=0; i<numCorners; i++)
	{
		ikData->m_coneSlicePlanePs[i] = Cross(coneCenterDirPs, ikData->m_coneBoundaryPointsPs[i]);
		ikData->m_coneBoundaryPlanePs[i] = Cross(ikData->m_coneBoundaryPointsPs[i], ikData->m_coneBoundaryPointsPs[(i+1)%numCorners]);

		if (ikData->m_coneBoundryTwistCenter)
			ikData->m_coneBoundryTwistCenter[i] = twistCenter;

		if (ikData->m_coneBoundryTwistRange)
			ikData->m_coneBoundryTwistRange[i] = twistRange;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimits::SetupJointConstraintEllipticalCone(JointData* pJointData, const DC::IkConstraint* pConstraint)
{
	//const NdGameObject* pGo = m_hGo.ToProcess();
	//int jointIndex = pGo->FindJointIndex(pConstraint->m_jointName);

	const ArtItemSkeleton* pSkel = m_hSkel.ToArtItem();
	if (!pSkel)
		return false;

	const int jointIndex = FindJoint(pSkel->m_pJointDescs, pSkel->m_numGameplayJoints, pConstraint->m_jointName);

	if (jointIndex < 0)
		return false;

	ANIM_ASSERT(pJointData != nullptr);
	pJointData->m_jointName = pConstraint->m_jointName;
	pJointData->m_jointIndex = jointIndex;

	int numCorners = pConstraint->m_coneNumCorners;
	if (numCorners < 3)
		return false;

	JointConstraintData* ikData = &pJointData->m_data;

	if (ikData->m_coneBoundaryPointsPs == nullptr || ikData->m_coneNumBoundaryPoints != numCorners)
	{
		ikData->m_coneNumBoundaryPoints = numCorners;
		ikData->m_coneBoundaryPointsPs = NDI_NEW Vector[numCorners];
		ikData->m_coneSlicePlanePs = NDI_NEW Vector[numCorners];
		ikData->m_coneBoundaryPlanePs = NDI_NEW Vector[numCorners];
		ikData->m_coneBoundryTwistCenter = NDI_NEW float[numCorners];
		ikData->m_coneBoundryTwistRange = NDI_NEW float[numCorners];
	}

	ANIM_ASSERT(ikData->m_coneBoundaryPointsPs);
	ANIM_ASSERT(ikData->m_coneSlicePlanePs);
	ANIM_ASSERT(ikData->m_coneBoundaryPlanePs);
	ANIM_ASSERT(ikData->m_coneBoundryTwistCenter);
	ANIM_ASSERT(ikData->m_coneBoundryTwistCenter);

	//const ndanim::JointHierarchy* pJointHier = pGo->GetAnimData()->m_pSkeleton;
	const ndanim::JointHierarchy* pJointHier = pSkel->m_pAnimHierarchy;
	const ndanim::JointParams* pDefaultParamsLs = ndanim::GetDefaultJointPoseTable(pJointHier);
	ikData->m_bindPoseLs = Locator(pDefaultParamsLs[jointIndex].m_trans, pDefaultParamsLs[jointIndex].m_quat);

	SetupJointConstraintEllipticalCone(ikData, pConstraint);

	pJointData->m_setupType = JointData::kSetupElliptical;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimits::SetupJointConstraintConePoints(JointData* pJointData, const DC::IkConstraint* pConstraint)
{
	if (pConstraint->m_conePoints  == nullptr || pConstraint->m_coneCenterDir == nullptr)
		return false;

	//const NdGameObject* pGo = m_hGo.ToProcess();
	//int jointIndex = pGo->FindJointIndex(pConstraint->m_jointName);

	const ArtItemSkeleton* pSkel = m_hSkel.ToArtItem();
	if (!pSkel)
		return false;

	const int jointIndex = FindJoint(pSkel->m_pJointDescs, pSkel->m_numGameplayJoints, pConstraint->m_jointName);

	if (jointIndex < 0)
		return false;

	ANIM_ASSERT(pJointData != nullptr);
	pJointData->m_jointName = pConstraint->m_jointName;
	pJointData->m_jointIndex = jointIndex;

	int numCorners = pConstraint->m_conePoints->m_count;
	if (numCorners < 3)
		return false;

	JointConstraintData* ikData = &pJointData->m_data;

	if (ikData->m_coneBoundaryPointsPs == nullptr || ikData->m_coneNumBoundaryPoints != numCorners)
	{
		ikData->m_coneNumBoundaryPoints = numCorners;
		ikData->m_coneBoundaryPointsPs = NDI_NEW Vector[numCorners];
		ikData->m_coneSlicePlanePs = NDI_NEW Vector[numCorners];
		ikData->m_coneBoundaryPlanePs = NDI_NEW Vector[numCorners];
		ikData->m_coneBoundryTwistCenter = NDI_NEW float[numCorners];
		ikData->m_coneBoundryTwistRange = NDI_NEW float[numCorners];
	}

	ANIM_ASSERT(ikData->m_coneBoundaryPointsPs);
	ANIM_ASSERT(ikData->m_coneSlicePlanePs);
	ANIM_ASSERT(ikData->m_coneBoundaryPlanePs);
	ANIM_ASSERT(ikData->m_coneBoundryTwistCenter);
	ANIM_ASSERT(ikData->m_coneBoundryTwistCenter);

	//const ndanim::JointHierarchy* pJointHier = pGo->GetAnimData()->m_pSkeleton;
	const ndanim::JointHierarchy* pJointHier = pSkel->m_pAnimHierarchy;
	const ndanim::JointParams* pDefaultParamsLs = ndanim::GetDefaultJointPoseTable(pJointHier);
	ikData->m_bindPoseLs = Locator(pDefaultParamsLs[jointIndex].m_trans, pDefaultParamsLs[jointIndex].m_quat);

	Vector coneCenterDirPs = *pConstraint->m_coneCenterDir;
	ikData->m_coneVisPointPs = coneCenterDirPs;

	Vector bindPosDirPs = GetLocalZ(ikData->m_bindPoseLs.Rot());
	Vector bindPoseToCenterRotAxisPs = Cross(bindPosDirPs, ikData->m_coneVisPointPs);
	bindPoseToCenterRotAxisPs = SafeNormalize(bindPoseToCenterRotAxisPs, bindPosDirPs);
	float bindPoseToCenterDot = MinMax01(Dot(bindPosDirPs, ikData->m_coneVisPointPs));
	float bindPoseToCenterAngle = Acos(bindPoseToCenterDot);
	Quat bindPoseToCenterRot = QuatFromAxisAngle(bindPoseToCenterRotAxisPs, bindPoseToCenterAngle);

	ikData->m_coneRotPs = Normalize(bindPoseToCenterRot * ikData->m_bindPoseLs.Rot());

	float twistMin = DEGREES_TO_RADIANS(pConstraint->m_coneCenterTwistMin);
	float twistMax = DEGREES_TO_RADIANS(pConstraint->m_coneCenterTwistMax);

	ikData->m_visPointTwistCenter = NormalizeAngle_rad(0.5f*(twistMin + twistMax));
	ikData->m_visPointTwistRange = Abs(0.5f*(twistMax - twistMin));

	for (int i = 0; i < numCorners; i++)
	{
		ikData->m_coneBoundaryPointsPs[i] = *pConstraint->m_conePoints->m_array[i].m_jointRot;

		ikData->m_coneBoundryTwistCenter[i] = NormalizeAngle_rad(0.5f*(twistMin + twistMax));
		ikData->m_coneBoundryTwistRange[i] = Abs(0.5f*(twistMax - twistMin));
	}

	for (int i = 0; i < numCorners; i++)
	{
		ikData->m_coneSlicePlanePs[i] = Cross(coneCenterDirPs, ikData->m_coneBoundaryPointsPs[i]);
		ikData->m_coneBoundaryPlanePs[i] = Cross(ikData->m_coneBoundaryPointsPs[i], ikData->m_coneBoundaryPointsPs[(i+1)%numCorners]);
	}

	pJointData->m_setupType = JointData::kSetupConePoint;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimits::SetupJointConstraintAnim(const AnimTable* pAnimTable,
										   JointData* pJointData,
										   const DC::IkConstraint* pConstraint)
{
	if (!EngineComponents::GetNdGameInfo()->m_jointLimitUseAnimData || !pAnimTable)
		return false;

	//const NdGameObject* pGo = m_hGo.ToProcess();
	//int jointIndex = pGo->FindJointIndex(pConstraint->m_jointName);

	const ArtItemSkeleton* pSkel = m_hSkel.ToArtItem();
	if (!pSkel)
		return false;

	const int jointIndex = FindJoint(pSkel->m_pJointDescs, pSkel->m_numGameplayJoints, pConstraint->m_jointName);

	if (jointIndex < 0)
		return false;

	ANIM_ASSERT(pJointData != nullptr);
	pJointData->m_jointName = pConstraint->m_jointName;
	pJointData->m_jointIndex = jointIndex;

	StringId64 coneAnim = pConstraint->m_coneAnim;
	int animJointIndex = jointIndex;
	if (pConstraint->m_coneAnimJoint != INVALID_STRING_ID_64)
	{
		//animJointIndex = pGo->FindJointIndex(pConstraint->m_coneAnimJoint);
		animJointIndex = FindJoint(pSkel->m_pJointDescs, pSkel->m_numGameplayJoints, pConstraint->m_coneAnimJoint);
		if (animJointIndex < 0)
			return false;
	}

	//int parentAnimJointIndex = pGo->GetAnimData()->m_jointCache.GetParentJoint(animJointIndex);
	const int parentAnimJointIndex = ndanim::GetParentJoint(pSkel->m_pAnimHierarchy, animJointIndex);
	bool parentMirror = pConstraint->m_coneAnimParentMirror;

	if (coneAnim == INVALID_STRING_ID_64)
		return false;

	//const ArtItemAnim* pAnim = pGo->GetAnimControl()->LookupAnim(coneAnim).ToArtItem();
	const ArtItemAnim* pAnim = pAnimTable->LookupAnim(coneAnim).ToArtItem();
	if (pAnim == nullptr)
		return false;

	int numFrames = pAnim->m_pClipData->m_numTotalFrames;
	int numCorners = numFrames/3 - 1;

	if (numCorners < 3)
		return false;

	JointConstraintData* ikData = &pJointData->m_data;

	if (ikData->m_coneBoundaryPointsPs == nullptr || ikData->m_coneNumBoundaryPoints != numCorners)
	{
		ikData->m_coneNumBoundaryPoints = numCorners;
		ikData->m_coneBoundaryPointsPs = NDI_NEW Vector[numCorners];
		ikData->m_coneSlicePlanePs = NDI_NEW Vector[numCorners];
		ikData->m_coneBoundaryPlanePs = NDI_NEW Vector[numCorners];
		ikData->m_coneBoundryTwistCenter = NDI_NEW float[numCorners];
		ikData->m_coneBoundryTwistRange = NDI_NEW float[numCorners];
	}

	ANIM_ASSERT(ikData->m_coneBoundaryPointsPs);
	ANIM_ASSERT(ikData->m_coneSlicePlanePs);
	ANIM_ASSERT(ikData->m_coneBoundaryPlanePs);
	ANIM_ASSERT(ikData->m_coneBoundryTwistCenter);
	ANIM_ASSERT(ikData->m_coneBoundryTwistRange);

	//const ArtItemSkeleton* pSkel = pGo->GetAnimData()->m_curSkelHandle.ToArtItem();
	//const ndanim::JointHierarchy* pJointHier = pGo->GetAnimData()->m_pSkeleton;
	const ndanim::JointHierarchy* pJointHier = pSkel->m_pAnimHierarchy;
	const ndanim::JointParams* pDefaultParamsLs = ndanim::GetDefaultJointPoseTable(pJointHier);

	ikData->m_bindPoseLs = Locator(pDefaultParamsLs[jointIndex].m_trans, pDefaultParamsLs[jointIndex].m_quat);
	Locator animJointBindPose = Locator(pDefaultParamsLs[animJointIndex].m_trans, pDefaultParamsLs[animJointIndex].m_quat);

	Quat mirrorRot = QuatFromAxisAngle(kUnitXAxis, PI);

	Locator jointLocLs = GetAnimJointParamsLs(pSkel, pAnim, 1.0f, animJointIndex, parentAnimJointIndex, parentMirror);
	Locator jointLocLsTwistMin = GetAnimJointParamsLs(pSkel, pAnim, 0.0f, animJointIndex, parentAnimJointIndex, parentMirror);
	Locator jointLocLsTwistMax = GetAnimJointParamsLs(pSkel, pAnim, 2.0f, animJointIndex, parentAnimJointIndex, parentMirror);

	ComputeTwistRange(jointLocLsTwistMin.Rot(), jointLocLsTwistMax.Rot(), jointLocLs.Rot(),
		&ikData->m_visPointTwistCenter, &ikData->m_visPointTwistRange);

	ikData->m_coneVisPointPs = GetLocalZ(jointLocLs.Rot());
	if (parentMirror)
		ikData->m_coneVisPointPs = Rotate(mirrorRot, ikData->m_coneVisPointPs);

	Vector bindPosDirPs = GetLocalZ(animJointBindPose.Rot());
	Vector bindPoseToCenterRotAxisPs = Cross(bindPosDirPs, ikData->m_coneVisPointPs);
	bindPoseToCenterRotAxisPs = SafeNormalize(bindPoseToCenterRotAxisPs, bindPosDirPs);
	float bindPoseToCenterDot = MinMax01(Dot(bindPosDirPs, ikData->m_coneVisPointPs));
	float bindPoseToCenterAngle = Acos(bindPoseToCenterDot);
	Quat bindPoseToCenterRot = QuatFromAxisAngle(bindPoseToCenterRotAxisPs, bindPoseToCenterAngle);

	ikData->m_coneRotPs = Normalize(bindPoseToCenterRot * animJointBindPose.Rot());

	for (int i=0; i<numCorners; i++)
	{
		float frame = 4.0f + 3.0f*i;
		jointLocLs = GetAnimJointParamsLs(pSkel, pAnim, frame, animJointIndex, parentAnimJointIndex, parentMirror);
		jointLocLsTwistMin = GetAnimJointParamsLs(pSkel, pAnim, frame - 1.0f, animJointIndex, parentAnimJointIndex, parentMirror);
		jointLocLsTwistMax = GetAnimJointParamsLs(pSkel, pAnim, frame + 1.0f, animJointIndex, parentAnimJointIndex, parentMirror);

		ikData->m_coneBoundaryPointsPs[i] = GetLocalZ(jointLocLs.Rot());
		if (parentMirror)
			ikData->m_coneBoundaryPointsPs[i] = Rotate(mirrorRot, ikData->m_coneBoundaryPointsPs[i]);

		ComputeTwistRange(jointLocLsTwistMin.Rot(), jointLocLsTwistMax.Rot(), jointLocLs.Rot(),
			&ikData->m_coneBoundryTwistCenter[i], &ikData->m_coneBoundryTwistRange[i]);
	}

	for (int i=0; i<numCorners; i++)
	{
		ikData->m_coneSlicePlanePs[i] = Cross(ikData->m_coneVisPointPs, ikData->m_coneBoundaryPointsPs[i]);
		ikData->m_coneBoundaryPlanePs[i] = Cross(ikData->m_coneBoundaryPointsPs[i], ikData->m_coneBoundaryPointsPs[(i+1)%numCorners]);
	}

	pJointData->m_setupType = JointData::kSetupAnim;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimits::Reload(NdGameObject* pGo)
{
#ifdef JOINT_LIMIT_DEBUG
	if (Memory::IsDebugMemoryAvailable())
	{
		AllocateJanitor jj(kAllocDebug, FILE_LINE_FUNC);
		return SetupJoints(pGo, m_dcIkConstraints.GetId());
	}
	else
	{
		return false;
	}
#else
	return false;
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator JointLimits::GetAnimJointParamsLs(const ArtItemSkeleton* pSkel,
										  const ArtItemAnim* pAnim,
										  float frame,
										  int jointIndex,
										  int parentJointIndex,
										  bool parentMirror)
{
	const U32 jointIndicies[2] = {static_cast<U32>(jointIndex), static_cast<U32>(parentJointIndex)};
	Transform jointTransforms[2];
	Locator jointLocs[2];

	Transform objectXform = kIdentity;
	float phase = GetPhaseFromClipFrame(pAnim->m_pClipData, frame);
	int numJoints = (parentJointIndex < 0) ? 1 : 2;
	AnimateJoints(objectXform, pSkel, pAnim, frame, jointIndicies, numJoints, jointTransforms, nullptr, nullptr, nullptr);

	jointLocs[0] = Locator(jointTransforms[0]);
	jointLocs[1] = Locator((numJoints == 2) ? jointTransforms[1] : objectXform);

	//if (parentMirror)
	//{
	//	Vector parentX = GetLocalX(jointLocs[1].Rot());
	//	Quat mirrorRot = QuatFromAxisAngle(parentX, PI);
	//	jointLocs[0].Rotate(mirrorRot);
	//}

	Locator jointLocLs = jointLocs[1].UntransformLocator(jointLocs[0]);

	//if (parentMirror)
	//{
	//	Quat mirrorRot = QuatFromAxisAngle(kUnitXAxis, PI);
	//	jointLocLs.Rotate(mirrorRot);
	//}


	return jointLocLs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int JointLimits::GetNumJoints()
{
	return m_numJoints;
}

/// --------------------------------------------------------------------------------------------------------------- ///
JointLimits::JointData* JointLimits::GetJointData(int jointNum)
{
	return &m_jointData[jointNum];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointLimits::ApplyJointLimits(JointSet* pJoints) const
{
	if (!pJoints || FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_disableAllJointLimits))
		return;

	for (U32F iLimit = 0; iLimit < m_numJoints; ++iLimit)
	{
		const StringId64 jointId = m_jointData[iLimit].m_jointName;
		const I32F iInputOffset = pJoints->FindJointOffset(jointId);

		if (iInputOffset < 0)
			continue;

		if (!pJoints->IsJointDataValid(iInputOffset))
			continue;

		ApplyJointLimit(pJoints, iInputOffset, iLimit);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointLimits::ApplyJointLimits(JointSet* pJoints, JointSetMap* pJointSetMap) const
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_disableAllJointLimits))
		return;

	for (int i = 0; i < pJointSetMap->m_numJoints; i++)
	{
		int jointLimitIndex = pJointSetMap->m_jointLimitIndex[i];

#ifdef JOINT_LIMIT_DEBUG
		// If joints have been modified in dc file, the precomputed JointSetMap could be wrong. Do a sanity check
		// and update if needed.
		if (jointLimitIndex >= 0)
		{
			StringId64 jointSetName = pJoints->GetJointId(pJointSetMap->m_jointSetOffset[i]);
			StringId64 jointLimitName = INVALID_STRING_ID_64;

			if (jointLimitIndex < m_numJoints)
				jointLimitName = m_jointData[jointLimitIndex].m_jointName;

			if (jointSetName != jointLimitName)
			{
				pJointSetMap->m_jointLimitIndex[i] = -1;
				for (int j=0; j<m_numJoints; j++)
				{
					if (jointSetName == m_jointData[j].m_jointName)
					{
						pJointSetMap->m_jointLimitIndex[i] = j;
						break;
					}
				}
			}
		}
#endif

		if (pJointSetMap->m_jointLimitIndex[i] >= 0)
			ApplyJointLimit(pJoints, pJointSetMap->m_jointSetOffset[i], pJointSetMap->m_jointLimitIndex[i]);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointLimits::ApplyJointLimit(JointSet* pJoints,
								  const JointConstraintData* ikData,
								  const JointData* optionalJointData,
								  int jointSetOffset,
								  ConstraintFuncResult* outResult)
{
	PROFILE_AUTO(Animation);
	ANIM_ASSERT(pJoints != nullptr);
	ANIM_ASSERT(ikData != nullptr);

	if (ikData->m_coneNumBoundaryPoints == 0)
		return;

	// We're assuming the Z axis (or negative Z axis) runs along the direction of the bone, so get that direction
	Locator jointLocPs = pJoints->GetJointLocLs(jointSetOffset);
	Vector jointFwdPs = GetLocalZ(jointLocPs.Rot());

	// Dot forward direction with precomputed slice planes to find the slice it falls in
	const int numPoints = ikData->m_coneNumBoundaryPoints;
	int sliceNum = -1;
	int nextSliceNum = -1;
	float dotPrev = Dot(jointFwdPs, ikData->m_coneSlicePlanePs[0]);
	float dotNext = 0.0f;
	for (int p=0; p<numPoints; p++)
	{
		nextSliceNum = (p+1)%numPoints;
		dotNext = Dot(jointFwdPs, ikData->m_coneSlicePlanePs[nextSliceNum]);

		if (dotPrev >= 0.0f && dotNext < 0.0f)
		{
#ifdef JOINT_LIMIT_DEBUG
			if (optionalJointData != nullptr)
			{
				optionalJointData->m_debugPrevSliceDot = dotPrev;
				optionalJointData->m_debugNextSliceDot = dotNext;
			}
#endif
			sliceNum = p;
			break;
		}

		dotPrev = dotNext;
	}

#ifdef JOINT_LIMIT_DEBUG
	if (optionalJointData != nullptr)
		optionalJointData->m_debugConeSlice = sliceNum;
#endif

	float currTwist = -1.0f, newTwist = -1.0f;
	float twistCenter = ikData->m_visPointTwistCenter;
	float twistRange = ikData->m_visPointTwistRange;

	bool inCone = true;

	if (sliceNum != -1)
	{
		// Dot forward direction with precomputed boundary plane to determine if we're inside the boundary
		float boundaryDot = Dot(jointFwdPs, ikData->m_coneBoundaryPlanePs[sliceNum]);
		inCone = (boundaryDot >= 0);

		// Blend twist values from the two surrounding cone boundry points, and the vis point
		const float boundaryDotClamped = Max(0.0f, boundaryDot);
		const float s = dotPrev - dotNext + boundaryDotClamped;
		const float weightCenter = boundaryDotClamped/s;
		const float weightPrev = -dotNext/s;		// Weight of prev point is greater when the dot of the next boundry point is larger
		const float weightNext = dotPrev/s;			// ... and vice versa.

#ifdef JOINT_LIMIT_DEBUG
		if (optionalJointData != nullptr)
		{
			optionalJointData->m_debugBoundryDot = boundaryDot;
			optionalJointData->m_debugWeightCenter = weightCenter;
			optionalJointData->m_debugWeightPrev = weightPrev;
			optionalJointData->m_debugWeightNext = weightNext;
		}
#endif

		if (ikData->m_coneBoundryTwistCenter != nullptr)
		{
			twistCenter =	weightPrev*ikData->m_coneBoundryTwistCenter[sliceNum] +
				weightNext*ikData->m_coneBoundryTwistCenter[nextSliceNum] +
				weightCenter*ikData->m_visPointTwistCenter;
		}

		if (ikData->m_coneBoundryTwistRange != nullptr)
		{
			twistRange =	weightPrev*ikData->m_coneBoundryTwistRange[sliceNum] +
				weightNext*ikData->m_coneBoundryTwistRange[nextSliceNum] +
				weightCenter*ikData->m_visPointTwistRange;
		}

		// Get current twist (around the Z axis) and normalize relative to the center of the twist range
		currTwist = ComputeTwistRel(jointLocPs.Rot(), ikData->m_bindPoseLs.Rot(), twistCenter);
		newTwist = currTwist;

		Vector boundaryDirPs = kZero;
		if (!inCone && (!(ikData->m_debugFlags & DC::kIkDebugFlagsDisable)))
		{
			// We're not in the boundary, so find the point on the boundary in the direction of the cone center (i.e. the cone visible point)
			Vector visToCurrPs = jointFwdPs - ikData->m_coneVisPointPs;
			float ttNum = -Dot(ikData->m_coneVisPointPs, ikData->m_coneBoundaryPlanePs[sliceNum]);
			float ttDenom = Dot(jointFwdPs - ikData->m_coneVisPointPs, ikData->m_coneBoundaryPlanePs[sliceNum]);
			float tt = ttNum/ttDenom;

			Vector visToBoundary = tt*visToCurrPs;
			boundaryDirPs = ikData->m_coneVisPointPs + visToBoundary;
			boundaryDirPs = Normalize(boundaryDirPs);

			// Compute the new joint rotation based on the boundary point as the forward direction
			Vector jointUpPs = GetLocalY(jointLocPs.Rot());
			Quat newJointRotPs = QuatFromLookAt(boundaryDirPs, jointUpPs);
			pJoints->SetJointRotationLs(jointSetOffset, newJointRotPs);

			jointLocPs = pJoints->GetJointLocLs(jointSetOffset);
			jointFwdPs = boundaryDirPs;

			// Compute the new twist, so we can later factor out any induced twist from this rotation
			newTwist = ComputeTwistRel(newJointRotPs, ikData->m_bindPoseLs.Rot(), twistCenter);
		}
	}
	else
	{
		// Get current twist (around the Z axis) and normalize relative to the center of the twist range
		currTwist = ComputeTwistRel(jointLocPs.Rot(), ikData->m_bindPoseLs.Rot(), twistCenter);
		newTwist = currTwist;
	}

#ifdef JOINT_LIMIT_DEBUG
	if (optionalJointData != nullptr)
	{
		optionalJointData->m_debugTwist = currTwist;
		optionalJointData->m_debugTwistCenter = twistCenter;
		optionalJointData->m_debugTwistRange = twistRange;
	}
#endif

	// Adjust any induced twist back to the old twist, while staying within twist limits
	float targetTwist = MinMax(currTwist, -twistRange, twistRange);
	float targetTwistAmount = targetTwist - newTwist;

	if (!(ikData->m_debugFlags & DC::kIkDebugFlagsDisable))
	{
		if (abs(targetTwistAmount) > FLT_EPSILON)
		{
			Quat twistCorrectionRot = QuatFromAxisAngle(jointFwdPs, targetTwistAmount);
			pJoints->PreRotateJointLs(jointSetOffset, twistCorrectionRot);
		}
	}

	if (outResult != nullptr)
	{
		outResult->m_inCone = inCone;
		outResult->m_jointLocPs = jointLocPs;
		outResult->m_jointFwdPs = jointFwdPs;
		outResult->m_sliceNum = sliceNum;
		outResult->m_targetTwist = targetTwist;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointLimits::ApplyJointLimit(JointSet* pJoints, int jointSetOffset, int jointLimitIndex) const
{
	ANIM_ASSERT(jointLimitIndex < m_numJoints);

	const JointData* jointData = &m_jointData[jointLimitIndex];

#ifdef JOINT_LIMIT_DEBUG
	if (m_debugDisableLimits && !m_debugDisableLimitsDisable)
		return;

	jointData->m_debugAppliedThisFrame = true;
#endif

	const JointConstraintData* ikData = &jointData->m_data;
	ApplyJointLimit(pJoints, ikData, jointData, jointSetOffset, nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float JointLimits::ComputeTwistRel(Quat_arg rot, Quat_arg baseRot, float refRot)
{
	// Compute twist, and normalize to the range (-PI, PI]
	Quat rotDif = Conjugate(baseRot)*rot;
	float twist = 2.0f*Atan2(rotDif.Z(), rotDif.W()) - refRot;
	twist = (twist <= -PI) ? twist+PI_TIMES_2 : twist;
	twist = (twist > PI) ? twist-PI_TIMES_2 : twist;
	return twist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float JointLimits::ComputeTwistAbs(Quat_arg rot, Quat_arg baseRot)
{
	// Compute twist, and normalize to the range (-PI, PI]
	Quat rotDif = Conjugate(baseRot)*rot;
	return 2.0f*Atan2(rotDif.Z(), rotDif.W());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointLimits::ComputeTwistRange(Quat_arg rotMin, Quat_arg rotMax, Quat_arg rotCenter, float* twistCenter, float* twistRange)
{
	float twistMin = ComputeTwistAbs(rotMin, rotCenter);
	float twistMax = ComputeTwistAbs(rotMax, rotCenter);

	twistMin = NormalizeAngle_rad(twistMin);
	twistMax = NormalizeAngle_rad(twistMax);

	if (twistMin > twistMax)
	{
		float temp = twistMin;
		twistMin = twistMax;
		twistMax = temp;
	}

	*twistCenter = 0.5f*(twistMax + twistMin);
	*twistRange = 0.5f*(twistMax - twistMin);
}

///////////////////////////////////////////////////////////////////////////////
// Debug draw and menu
///////////////////////////////////////////////////////////////////////////////

#ifdef JOINT_LIMIT_DEBUG

/// --------------------------------------------------------------------------------------------------------------- ///
void JointLimits::DebugDraw(const NdGameObject* pGo) const
{
	STRIP_IN_FINAL_BUILD;

	if (m_debugDraw)
	{
		DebugApplyJointLimits();

		m_debugTextY = 60.0f;

		//const NdGameObject* pGo = m_hGo.ToProcess();
		const JointCache& jc = pGo->GetAnimData()->m_jointCache;

		for (int i = 0; i < m_numJoints; i++)
		{
			DebugDrawLimit(i, pGo, jc);
			m_jointData[i].m_debugAppliedThisFrame = false;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointLimits::DebugDrawLimit(int jointLimitIndex, const NdGameObject* pGo, const JointCache& jointCache) const
{
	STRIP_IN_FINAL_BUILD;

	const JointData* pJointData = &m_jointData[jointLimitIndex];
	const JointConstraintData* ikData = &pJointData->m_data;

	if (!pJointData->m_debugDraw)	// make debug easier!
		return;

	int jointIndex = pJointData->m_jointIndex;
	int parentJointIndex = jointCache.GetParentJoint(pJointData->m_jointIndex);

	int numConePoints = ikData->m_coneNumBoundaryPoints;

	const ndanim::JointParams& localSpaceJoint = jointCache.GetJointParamsLs(jointIndex);
	Locator jointLocLs = Locator(localSpaceJoint.m_trans, localSpaceJoint.m_quat);
	const Vector jointFwdLs = GetLocalZ(jointLocLs.Rot());
	const Vector jointSideLs = GetLocalX(jointLocLs.Rot());
	const Vector jointUpLs = GetLocalY(jointLocLs.Rot());

	float mirrorSign = (pJointData->m_debugMirror) ? -1.0f : 1.0f;
	float coneLen = m_debugDrawConeSize;

	Locator parentLocWs;
	if (parentJointIndex < 0)
		parentLocWs = pGo->GetLocator();
	else
		parentLocWs = jointCache.GetJointLocatorWs(parentJointIndex);

	// Compute bind pose WS loc
	float rootScale = jointCache.GetJointParamsLs(0).m_scale.X();
	const Point scaledTrans = Point(rootScale * ikData->m_bindPoseLs.Pos().GetVec4());
	const Locator scaledJointLocLs(scaledTrans, ikData->m_bindPoseLs.Rot());
	const Locator bindPoseWs = parentLocWs.TransformLocator(scaledJointLocLs);

	if (m_debugDrawFlags & kDebugDrawBindPoseLoc)
		g_prim.Draw(DebugCoordAxes(bindPoseWs, coneLen, PrimAttrib(kPrimWireFrame)));

	const Vector coneCenterDirWs = mirrorSign*parentLocWs.TransformVector(ikData->m_coneVisPointPs);
	const Point coneCenterPtWs = bindPoseWs.Pos() + coneLen*coneCenterDirWs;

	if (m_debugDrawFlags & kDebugDrawConeVisPoint)
	{
		g_prim.Draw(DebugLine(bindPoseWs.Pos(), coneCenterPtWs, kColorBlue, 1.0f, PrimAttrib(kPrimWireFrame)));
	}

	if (m_debugDrawFlags & kDebugDrawJointDir)
	{
		const Color jointFwdColor = kColorWhite;
		const Color jointSideColor = kColorGray;
		const Vector jointFwdWs = mirrorSign * parentLocWs.TransformVector(jointFwdLs);
		const Vector jointSideWs = mirrorSign * parentLocWs.TransformVector(jointSideLs);
		g_prim.Draw(DebugLine(bindPoseWs.Pos(), coneLen * jointFwdWs, jointFwdColor, 2.0f, PrimAttrib(kPrimWireFrame)));
		g_prim.Draw(DebugLine(bindPoseWs.Pos(), coneLen * jointSideWs * 0.3f, jointSideColor, 2.0f, PrimAttrib(kPrimWireFrame)));
		g_prim.Draw(DebugString(bindPoseWs.Pos() + coneLen * jointFwdWs, "JntZ", jointFwdColor, 0.45f));
		g_prim.Draw(DebugString(bindPoseWs.Pos() + coneLen * jointSideWs * 0.3f, "JntX", jointSideColor, 0.45f));

		//if (!inCone)
		//{
		//	Vector boundaryDirWs = mirrorSign*parentLocWs.TransformVector(boundaryDirPs);
		//	g_prim.Draw(DebugLine(bindPoseWs.Pos(), coneLen*boundaryDirWs, kColorWhite, 1.0f, PrimAttrib(kPrimWireFrame)));
		//}
	}

	if (m_debugDrawFlags & kDebugDrawConeAxes)
	{
		const Vector coneDirXWs = mirrorSign*parentLocWs.TransformVector(GetLocalX(ikData->m_coneRotPs));
		const Vector coneDirYWs = mirrorSign*parentLocWs.TransformVector(GetLocalY(ikData->m_coneRotPs));
		g_prim.Draw(DebugLine(bindPoseWs.Pos(), bindPoseWs.Pos() + 0.03f*coneDirXWs, kColorRed, 1.0f, PrimAttrib(kPrimWireFrame)));
		g_prim.Draw(DebugLine(bindPoseWs.Pos(), bindPoseWs.Pos() + 0.03f*coneDirYWs, kColorGreen, 1.0f, PrimAttrib(kPrimWireFrame)));
		g_prim.Draw(DebugString(bindPoseWs.Pos() + 0.03f * coneDirXWs, "bindpose-X", kColorRed, 0.45f));
		g_prim.Draw(DebugString(bindPoseWs.Pos() + 0.03f * coneDirYWs, "bindpose-Y", kColorGreen, 0.45f));
	}

	const float newTwistRad = pJointData->m_debugTwist + pJointData->m_debugTwistCenter;

	if (m_debugDrawFlags & kDebugDrawConeTwist)
	{
		char buf[256];
		StringId64 jointId = pGo->GetAnimData()->m_pJointDescs[jointIndex].m_nameId;
		float newTwistDeg = RADIANS_TO_DEGREES(newTwistRad);
		sprintf(buf, "%s: Twist - %.1f", DevKitOnly_StringIdToString(jointId), newTwistDeg);
		g_prim.Draw(DebugString2D(Vec2(800.0f, m_debugTextY), kDebug2DLegacyCoords, buf, kColorWhite, 0.7f));

		float minRangeDeg = RADIANS_TO_DEGREES(pJointData->m_debugTwistCenter - pJointData->m_debugTwistRange);
		float maxRangeDeg = RADIANS_TO_DEGREES(pJointData->m_debugTwistCenter + pJointData->m_debugTwistRange);
		sprintf(buf, "Range (%.1f, %.1f)", minRangeDeg, maxRangeDeg);
		g_prim.Draw(DebugString2D(Vec2(1000.0f, m_debugTextY), kDebug2DLegacyCoords, buf, kColorWhite, 0.7f));

		m_debugTextY += 12.0f;
	}

	if (m_debugDrawFlags & kDebugDrawCone)
	{
		if (m_debugDrawFlags & kDebugDrawConeTwist)
		{
			char buf[64];
			sprintf(buf, "dbg-wght:%.2f", pJointData->m_debugWeightCenter);
			g_prim.Draw(DebugString(coneCenterPtWs, buf, kColorWhite, 0.5f));
		}

		const Mouse& mouse = EngineComponents::GetNdFrameState()->m_mouse;
		const Point mousePos(mouse.m_position.x, mouse.m_position.y, 0.0f);

		const Point origin = bindPoseWs.Pos();
		const Vector centerDirWs = mirrorSign*parentLocWs.TransformVector(ikData->m_coneVisPointPs);
		const Point centerPointWs = origin + coneLen*centerDirWs;
		Point centerPointPixelPos = MiniGl::WorldToPixel(centerPointWs);
		centerPointPixelPos.SetX((float)centerPointPixelPos.X());
		centerPointPixelPos.SetY((float)centerPointPixelPos.Y());
		centerPointPixelPos.SetZ(0.0f);

		float distSq = LengthSqr(mousePos - centerPointPixelPos);
		if (distSq < 100.0f)
		{
			g_prim.Draw(DebugSphere(centerPointWs, 0.02f, kColorGreen, PrimAttrib(kPrimWireFrame)));

			char buf[256];
			sprintf(buf, "\nCenter point\nTwist Range: %.1f, %.1f",
				RADIANS_TO_DEGREES(ikData->m_visPointTwistCenter - ikData->m_visPointTwistRange),
				RADIANS_TO_DEGREES(ikData->m_visPointTwistCenter + ikData->m_visPointTwistRange));
			g_prim.Draw(DebugString2D(Vec2(800.0f, m_debugTextY), kDebug2DLegacyCoords, buf, kColorWhite, 0.7f));
			m_debugTextY += 36.0f;
		}


		for (int p=0; p<numConePoints; p++)
		{
			const Vector pointDirAWs = mirrorSign*parentLocWs.TransformVector(ikData->m_coneBoundaryPointsPs[p]);
			const Vector pointDirBWs = mirrorSign*parentLocWs.TransformVector(ikData->m_coneBoundaryPointsPs[(p+1)%numConePoints]);

			const Point pointAWs = origin + coneLen*pointDirAWs;
			const Point pointBWs = origin + coneLen*pointDirBWs;

			Color lineColor = (p == pJointData->m_debugConeSlice) ? kColorYellow : kColorOrange;
			float width = (p == pJointData->m_debugConeSlice) ? 2.0f : 1.0f;

			g_prim.Draw(DebugLine(pointAWs, pointBWs, lineColor, width, PrimAttrib(kPrimWireFrame)));
			g_prim.Draw(DebugTriangle(origin, pointAWs, pointBWs, kColorGrayTrans, PrimAttrib(kPrimWireFrame)));

			Vector coneSlicePlaneWs = mirrorSign * parentLocWs.TransformVector(ikData->m_coneSlicePlanePs[p]);
			Vector coneBoundaryPlaneWs = mirrorSign * parentLocWs.TransformVector(ikData->m_coneBoundaryPlanePs[p]);
			g_prim.Draw(DebugLine(pointAWs, pointAWs + coneLen * coneSlicePlaneWs * 0.2f, kColorRed, 1.2f, PrimAttrib(kPrimWireFrame)));
			g_prim.Draw(DebugLine(pointAWs, pointAWs + coneLen * coneBoundaryPlaneWs * 0.2f, kColorCyan, 1.2f, PrimAttrib(kPrimWireFrame)));

			if (m_debugDrawFlags & kDebugDrawConeTwist)
			{
				float twistWeight = -1.0f;

				if (p == pJointData->m_debugConeSlice)
					twistWeight = pJointData->m_debugWeightPrev;
				else if (p == (pJointData->m_debugConeSlice+1)%numConePoints)
					twistWeight = pJointData->m_debugWeightNext;

				if (twistWeight >= 0.0f)
				{
					char buf[64];
					sprintf(buf, "tw:%d:%.2f", p, twistWeight);
					g_prim.Draw(DebugString(pointAWs, buf, kColorWhite, 0.5f));
				}
			}

			Point coneVertexPixelPos = MiniGl::WorldToPixel(pointAWs);
			coneVertexPixelPos.SetX((float)coneVertexPixelPos.X()*g_display.m_screenWidth/kVirtualScreenWidth);
			coneVertexPixelPos.SetY((float)coneVertexPixelPos.Y()*g_display.m_screenHeight/kVirtualScreenHeight);
			coneVertexPixelPos.SetZ(0.0f);

			float distConeSq = LengthSqr(mousePos - coneVertexPixelPos);
			if (distConeSq < 100.0f)
			{
				g_prim.Draw(DebugSphere(pointAWs, 0.02f, kColorGreen, PrimAttrib(kPrimWireFrame)));

				float swingDot = MinMax((float)Dot(centerDirWs, pointDirAWs), -1.0f, 1.0f);
				float swingAngle = Acos(swingDot);

				char buf[256];
				sprintf(buf, "\nCone Index: %d\nSwing Range: %.1f\nTwist Range: %.1f, %.1f", p,
					RADIANS_TO_DEGREES(swingAngle),
					RADIANS_TO_DEGREES(ikData->m_coneBoundryTwistCenter[p] - ikData->m_coneBoundryTwistRange[p]),
					RADIANS_TO_DEGREES(ikData->m_coneBoundryTwistCenter[p] + ikData->m_coneBoundryTwistRange[p]));
				g_prim.Draw(DebugString2D(Vec2(800.0f, m_debugTextY), kDebug2DLegacyCoords, buf, kColorWhite, 0.7f));
				m_debugTextY += 48.0f;
			}

			//char buf[16];
			//sprintf(buf, "%d", p);
			//g_prim.Draw(DebugString(pointAWs, buf, kColorWhite, 0.5f));
		}

		if (numConePoints > 0)
		{
			const Vector pointDirWs0 = mirrorSign* parentLocWs.TransformVector(ikData->m_coneBoundaryPointsPs[0]);
			const Quat rotFromConeCenterToDir0 = QuatFromVectors(coneCenterDirWs, pointDirWs0);

			Vec4 rotAxisVec4;
			float rotAngleRad;
			rotFromConeCenterToDir0.GetAxisAndAngle(rotAxisVec4, rotAngleRad);
			const Vector rotAxis(rotAxisVec4);

			static const float sc_angleCurveScale = 0.1f;
			static const I32 sc_numSegs = 16;

			for (U32F ii = 0; ii < sc_numSegs; ii++)
			{
				float angle0 = rotAngleRad * ii / sc_numSegs;
				float angle1 = rotAngleRad * (ii + 1) / sc_numSegs;

				Quat rot0 = QuatFromAxisAngle(rotAxis, angle0);
				Quat rot1 = QuatFromAxisAngle(rotAxis, angle1);

				Vector vecCurve0 = Rotate(rot0, coneCenterDirWs);
				Vector vecCurve1 = Rotate(rot1, coneCenterDirWs);

				Point end0 = origin + coneLen * vecCurve0 * sc_angleCurveScale;
				Point end1 = origin + coneLen * vecCurve1 * sc_angleCurveScale;

				g_prim.Draw(DebugLine(end0, end1, kColorOrange, 0.7f, PrimAttrib(kPrimWireFrame)));

				if (ii == 0)
				{
					Vector arrow0 = Rotate(QuatFromAxisAngle(rotAxis, DEGREES_TO_RADIANS(20.0f)), end1 - end0);
					Vector arrow1 = Rotate(QuatFromAxisAngle(rotAxis, DEGREES_TO_RADIANS(-20.0f)), end1 - end0);

					g_prim.Draw(DebugLine(end0, arrow0, kColorOrange, 0.7f, PrimAttrib(kPrimWireFrame)));
					g_prim.Draw(DebugLine(end0, arrow1, kColorOrange, 0.7f, PrimAttrib(kPrimWireFrame)));
				}

				if (ii == sc_numSegs - 1)
				{
					Vector arrow0 = Rotate(QuatFromAxisAngle(rotAxis, DEGREES_TO_RADIANS(20.0f)), end1 - end0);
					Vector arrow1 = Rotate(QuatFromAxisAngle(rotAxis, DEGREES_TO_RADIANS(-20.0f)), end1 - end0);

					g_prim.Draw(DebugLine(end1, -arrow0, kColorOrange, 0.7f, PrimAttrib(kPrimWireFrame)));
					g_prim.Draw(DebugLine(end1, -arrow1, kColorOrange, 0.7f, PrimAttrib(kPrimWireFrame)));
				}
			}
		}
	}

	if (m_debugDrawFlags & kDebugDrawSwingLimitHit)
	{
		if (pJointData->m_debugBoundryDot < 0.0f)
		{
			static float radius = 0.05f;
			float pct = LerpScaleClamp(0.0f, -0.5f, 1.0f, 0.0f, pJointData->m_debugBoundryDot);
			Color color(1.0f, pct, 0.0f);
			g_prim.Draw(DebugSphere(bindPoseWs.Pos(), radius, color, PrimAttrib(kPrimWireFrame)));
		}
	}

	if (m_debugDrawFlags & kDebugDrawTwistLimitHit)
	{
		float twistAbs = Abs(pJointData->m_debugTwist);
		if (twistAbs > pJointData->m_debugTwistRange)
		{
			float pct = LerpScaleClamp(pJointData->m_debugTwistRange, pJointData->m_debugTwistRange + DEGREES_TO_RADIANS(30.0f), 1.0f, 0.0f, twistAbs);
			Color color(1.0f, pct, 0.0f);
			//g_prim.Draw(DebugSphere(bindPoseWs.Pos(), 0.05f, color, PrimAttrib(kPrimWireFrame)));

			Point center = bindPoseWs.Pos();
			Vector left = GetLocalX(bindPoseWs.Rot());
			Vector up = GetLocalY(bindPoseWs.Rot());

			static float radius = 0.08f;
			static float width = 3.0f;

			for (int i=0; i<16; i++)
			{
				const float theta0 = PI_TIMES_2*i/16.0f;
				const float theta1 = PI_TIMES_2*(i+1)/16.0f;
				const float x0 = radius*Sin(theta0);
				const float y0 = radius*Cos(theta0);
				const float x1 = radius*Sin(theta1);
				const float y1 = radius*Cos(theta1);

				Point p0 = center + x0*left + y0*up;
				Point p1 = center + x1*left + y1*up;

				g_prim.Draw(DebugLine(p0, p1, color, color, width, PrimAttrib(kPrimWireFrame)));
			}
		}
	}

	if (m_debugDrawFlags & kDebugDrawConeTwist)
	{
		const Vector coneFwdWs = mirrorSign * parentLocWs.TransformVector(GetLocalZ(ikData->m_coneRotPs));
		const Vector coneSideWs = mirrorSign * parentLocWs.TransformVector(GetLocalX(ikData->m_coneRotPs));

		static const I32 sc_numSegs = 16;
		static const float sc_scale = 0.15f;

		float angleTwistMin = ikData->m_visPointTwistCenter - ikData->m_visPointTwistRange;
		float angleTwistMax = ikData->m_visPointTwistCenter + ikData->m_visPointTwistRange;
		const Point origin = bindPoseWs.GetTranslation();

		for (I32 ii = 0; ii < sc_numSegs; ii++)
		{
			float ff0 = (float)ii / sc_numSegs;
			float ff1 = (float)(ii + 1) / sc_numSegs;

			float angle0 = Lerp(angleTwistMin, angleTwistMax, ff0);
			float angle1 = Lerp(angleTwistMin, angleTwistMax, ff1);

			const Quat rotTwist0 = QuatFromAxisAngle(coneFwdWs, angle0);
			const Quat rotTwist1 = QuatFromAxisAngle(coneFwdWs, angle1);

			const Vector twistSideDirWs0 = Rotate(rotTwist0, coneSideWs);
			const Vector twistSideDirWs1 = Rotate(rotTwist1, coneSideWs);

			const Point end0 = origin + coneLen * twistSideDirWs0 * sc_scale;
			const Point end1 = origin + coneLen * twistSideDirWs1 * sc_scale;

			if (ii == 0)
			{
				g_prim.Draw(DebugLine(origin, end0, kColorGrayTrans, 0.7f, PrimAttrib(kPrimWireFrame)));

				Vector arrow0 = Rotate(QuatFromAxisAngle(coneFwdWs, DEGREES_TO_RADIANS(20.0f)), end1 - end0);
				Vector arrow1 = Rotate(QuatFromAxisAngle(coneFwdWs, DEGREES_TO_RADIANS(-20.0f)), end1 - end0);

				g_prim.Draw(DebugLine(end0, arrow0, kColorCyan, 0.7f, PrimAttrib(kPrimWireFrame)));
				g_prim.Draw(DebugLine(end0, arrow1, kColorCyan, 0.7f, PrimAttrib(kPrimWireFrame)));
			}
			else if (ii == sc_numSegs - 1)
			{
				g_prim.Draw(DebugLine(origin, end1, kColorGrayTrans, 0.7f, PrimAttrib(kPrimWireFrame)));

				Vector arrow0 = Rotate(QuatFromAxisAngle(coneFwdWs, DEGREES_TO_RADIANS(20.0f)), end1 - end0);
				Vector arrow1 = Rotate(QuatFromAxisAngle(coneFwdWs, DEGREES_TO_RADIANS(-20.0f)), end1 - end0);

				g_prim.Draw(DebugLine(end1, -arrow0, kColorCyan, 0.7f, PrimAttrib(kPrimWireFrame)));
				g_prim.Draw(DebugLine(end1, -arrow1, kColorCyan, 0.7f, PrimAttrib(kPrimWireFrame)));
			}

			g_prim.Draw(DebugLine(end0, end1, kColorCyan, 0.7f, PrimAttrib(kPrimWireFrame)));
		}

		const float mirrorTwistRad = mirrorSign * (pJointData->m_debugTwist + pJointData->m_debugTwistCenter);

		const Quat rotCurr = QuatFromAxisAngle(coneFwdWs, mirrorTwistRad);
		const Vector currTwistDirWs = Rotate(rotCurr, coneSideWs);
		const Point currTwistPt = origin + coneLen * currTwistDirWs * sc_scale;

		g_prim.Draw(DebugLine(origin, currTwistPt, kColorMagenta, 0.7f, PrimAttrib(kPrimWireFrame)));

		// debug validation! they're roughly collinear.
		if ((m_debugDrawFlags & kDebugValidateConeTwist) && pJointData->m_setupType == JointData::kSetupElliptical)
		{
			const Locator coneSpace(bindPoseWs.GetTranslation(), parentLocWs.Rot() * ikData->m_coneRotPs);

			//const Vector coneDirXWs = mirrorSign * parentLocWs.TransformVector(GetLocalX(ikData->m_coneRotPs));
			//const Vector coneDirYWs = mirrorSign * parentLocWs.TransformVector(GetLocalY(ikData->m_coneRotPs));

			// project JntX into cone space x-y plane.
			const Vector jointSideWs = mirrorSign * parentLocWs.TransformVector(jointSideLs);
			const Vector jointSideInConeSpace = coneSpace.UntransformVector(jointSideWs);
			const Vector jointSideInConeSpaceXY(jointSideInConeSpace.X(), jointSideInConeSpace.Y(), 0.0f);
			const Vector jointSideInConeSpaceXYNorm = SafeNormalize(jointSideInConeSpaceXY, kUnitXAxis);

			const Vector jointSideXYNormWs = coneSpace.TransformVector(jointSideInConeSpaceXYNorm);
			g_prim.Draw(DebugLine(origin, jointSideXYNormWs * coneLen * sc_scale, kColorOrange, 1.2f, PrimAttrib(kPrimWireFrame)));

			const Vector currTwistInConeSpace = coneSpace.UntransformVector(currTwistDirWs);
			const Vector currTwistInConeSpaceXY(currTwistInConeSpace.X(), currTwistInConeSpace.Y(), 0.0f);
			const Vector currTwistInConeSpaceXYNorm = SafeNormalize(currTwistInConeSpaceXY, kUnitXAxis);
			//const Vector currTwistInBindPoseSpaceNorm = SafeNormalize(currTwistInBindPoseSpace, kUnitXAxis);
			const float testCompare = Dot(jointSideInConeSpaceXYNorm, currTwistInConeSpaceXYNorm);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointLimits::DebugApplyJointLimits() const
{
	STRIP_IN_FINAL_BUILD;

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	//NdGameObject* pGo = m_hGo.ToMutableProcess();
	//const FgAnimData* pAnimData = pGo->GetAnimData();
	const ArtItemSkeleton* pSkel = m_hSkel.ToArtItem();
	if (!pSkel)
		return;
	
	//StringId64 rootJointName = pAnimData->m_pJointDescs[0].m_nameId;
	StringId64 rootJointName = pSkel->m_pJointDescs[0].m_nameId;

	StringId64* endJointNames = NDI_NEW StringId64[m_numJoints];
	for (U32F i = 0; i < m_numJoints; ++i)
	{
		//ANIM_ASSERT(m_jointData[i].m_jointName != INVALID_STRING_ID_64);
		endJointNames[i] = m_jointData[i].m_jointName;
	}

	JointTree debugJointTree;
	debugJointTree.Init(pSkel, rootJointName, m_numJoints, endJointNames);

	debugJointTree.ReadJointCache();

	m_debugDisableLimitsDisable = true;

	for (int i=0; i<m_numJoints; i++)
	{
		int jointOffset = debugJointTree.FindJointOffset(m_jointData[i].m_jointName);

		if (jointOffset >= 0 && !m_jointData[i].m_debugAppliedThisFrame)
			ApplyJointLimit(&debugJointTree, jointOffset, i);
	}

	m_debugDisableLimitsDisable = false;

	debugJointTree.DiscardJointCache();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool JointLimitObjectEnableDebugFunc(DMENU::Item& item, DMENU::Message msg)
{
	Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid);
	if (!pProc)
		return false;

	NdGameObject* pGo = static_cast<NdGameObject*>(pProc);
	bool enable = pGo->GetJointLimits()->m_debugDraw;
	if (msg == DMENU::kExecute)
	{
		enable = !enable;
		pGo->GetJointLimits()->m_debugDraw = enable;
		EngineComponents::GetNdGameInfo()->m_jointLimitDebugDraw = true;
	}

	return enable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float JointLimitObjectConeSizeFunc(DMENU::Item& item, DMENU::Message message, float desiredValue, float oldValue)
{
	Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid);
	if (!pProc)
		return false;

	NdGameObject* pGo = static_cast<NdGameObject*>(pProc);
	JointLimits* pJointLimits = pGo->GetJointLimits();

	switch (message)
	{
		case DMENU::kExecute:
			EngineComponents::GetNdGameInfo()->m_jointLimitDebugDraw = true;
			pGo->GetJointLimits()->m_debugDraw = true;

			pJointLimits->m_debugDrawConeSize = desiredValue;
			break;
	}

	return pJointLimits->m_debugDrawConeSize;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitObjectDrawBindPoseLocFunc(DMENU::Item& item, DMENU::Message message)
{
	Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid);
	if (!pProc)
		return false;

	NdGameObject* pGo = static_cast<NdGameObject*>(pProc);
	JointLimits* pJointLimits = pGo->GetJointLimits();

	bool enabled = (pGo->GetJointLimits()->m_debugDrawFlags & JointLimits::kDebugDrawBindPoseLoc) != 0;
	switch (message)
	{
		case DMENU::kExecute:
			EngineComponents::GetNdGameInfo()->m_jointLimitDebugDraw = true;
			pGo->GetJointLimits()->m_debugDraw = true;

			enabled = !enabled;
			if (enabled)
				pGo->GetJointLimits()->m_debugDrawFlags |= JointLimits::kDebugDrawBindPoseLoc;
			else
				pGo->GetJointLimits()->m_debugDrawFlags &= ~JointLimits::kDebugDrawBindPoseLoc;
			break;
	}

	return enabled;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitObjectDrawConeFunc(DMENU::Item& item, DMENU::Message message)
{
	Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid);
	if (!pProc)
		return false;

	NdGameObject* pGo = static_cast<NdGameObject*>(pProc);
	JointLimits* pJointLimits = pGo->GetJointLimits();

	bool enabled = (pGo->GetJointLimits()->m_debugDrawFlags & JointLimits::kDebugDrawCone) != 0;
	switch (message)
	{
		case DMENU::kExecute:
			EngineComponents::GetNdGameInfo()->m_jointLimitDebugDraw = true;
			pGo->GetJointLimits()->m_debugDraw = true;

			enabled = !enabled;
			if (enabled)
				pGo->GetJointLimits()->m_debugDrawFlags |= JointLimits::kDebugDrawCone;
			else
				pGo->GetJointLimits()->m_debugDrawFlags &= ~JointLimits::kDebugDrawCone;
			break;
	}

	return enabled;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitObjectDrawConeAxesFunc(DMENU::Item& item, DMENU::Message message)
{
	Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid);
	if (!pProc)
		return false;

	NdGameObject* pGo = static_cast<NdGameObject*>(pProc);
	JointLimits* pJointLimits = pGo->GetJointLimits();

	bool enabled = (pGo->GetJointLimits()->m_debugDrawFlags & JointLimits::kDebugDrawConeAxes) != 0;
	switch (message)
	{
		case DMENU::kExecute:
			EngineComponents::GetNdGameInfo()->m_jointLimitDebugDraw = true;
			pGo->GetJointLimits()->m_debugDraw = true;

			enabled = !enabled;
			if (enabled)
				pGo->GetJointLimits()->m_debugDrawFlags |= JointLimits::kDebugDrawConeAxes;
			else
				pGo->GetJointLimits()->m_debugDrawFlags &= ~JointLimits::kDebugDrawConeAxes;
			break;
	}

	return enabled;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitObjectDrawConeVisPointFunc(DMENU::Item& item, DMENU::Message message)
{
	Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid);
	if (!pProc)
		return false;

	NdGameObject* pGo = static_cast<NdGameObject*>(pProc);
	JointLimits* pJointLimits = pGo->GetJointLimits();

	bool enabled = (pGo->GetJointLimits()->m_debugDrawFlags & JointLimits::kDebugDrawConeVisPoint) != 0;
	switch (message)
	{
		case DMENU::kExecute:
			EngineComponents::GetNdGameInfo()->m_jointLimitDebugDraw = true;
			pGo->GetJointLimits()->m_debugDraw = true;

			enabled = !enabled;
			if (enabled)
				pGo->GetJointLimits()->m_debugDrawFlags |= JointLimits::kDebugDrawConeVisPoint;
			else
				pGo->GetJointLimits()->m_debugDrawFlags &= ~JointLimits::kDebugDrawConeVisPoint;
			break;
	}

	return enabled;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitObjectDrawConeTwistFunc(DMENU::Item& item, DMENU::Message message)
{
	Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid);
	if (!pProc)
		return false;

	NdGameObject* pGo = static_cast<NdGameObject*>(pProc);
	JointLimits* pJointLimits = pGo->GetJointLimits();

	bool enabled = (pGo->GetJointLimits()->m_debugDrawFlags & JointLimits::kDebugDrawConeTwist) != 0;
	switch (message)
	{
		case DMENU::kExecute:
			EngineComponents::GetNdGameInfo()->m_jointLimitDebugDraw = true;
			pGo->GetJointLimits()->m_debugDraw = true;

			enabled = !enabled;
			if (enabled)
				pGo->GetJointLimits()->m_debugDrawFlags |= JointLimits::kDebugDrawConeTwist;
			else
				pGo->GetJointLimits()->m_debugDrawFlags &= ~JointLimits::kDebugDrawConeTwist;
			break;
	}

	return enabled;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitObjectValidateConeTwistFunc(DMENU::Item& item, DMENU::Message message)
{
	Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid);
	if (!pProc)
		return false;

	NdGameObject* pGo = static_cast<NdGameObject*>(pProc);
	JointLimits* pJointLimits = pGo->GetJointLimits();

	bool enabled = (pGo->GetJointLimits()->m_debugDrawFlags & JointLimits::kDebugValidateConeTwist) != 0;
	switch (message)
	{
	case DMENU::kExecute:
		EngineComponents::GetNdGameInfo()->m_jointLimitDebugDraw = true;
		pGo->GetJointLimits()->m_debugDraw = true;

		enabled = !enabled;
		if (enabled)
			pGo->GetJointLimits()->m_debugDrawFlags |= JointLimits::kDebugValidateConeTwist;
		else
			pGo->GetJointLimits()->m_debugDrawFlags &= ~JointLimits::kDebugValidateConeTwist;
		break;
	}

	return enabled;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitObjectDrawJointDirFunc(DMENU::Item& item, DMENU::Message message)
{
	Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid);
	if (!pProc)
		return false;

	NdGameObject* pGo = static_cast<NdGameObject*>(pProc);
	JointLimits* pJointLimits = pGo->GetJointLimits();

	bool enabled = (pGo->GetJointLimits()->m_debugDrawFlags & JointLimits::kDebugDrawJointDir) != 0;
	switch (message)
	{
		case DMENU::kExecute:
			EngineComponents::GetNdGameInfo()->m_jointLimitDebugDraw = true;
			pGo->GetJointLimits()->m_debugDraw = true;

			enabled = !enabled;
			if (enabled)
				pGo->GetJointLimits()->m_debugDrawFlags |= JointLimits::kDebugDrawJointDir;
			else
				pGo->GetJointLimits()->m_debugDrawFlags &= ~JointLimits::kDebugDrawJointDir;
			break;
	}

	return enabled;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitObjectDrawTextFunc(DMENU::Item& item, DMENU::Message message)
{
	Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid);
	if (!pProc)
		return false;

	NdGameObject* pGo = static_cast<NdGameObject*>(pProc);
	JointLimits* pJointLimits = pGo->GetJointLimits();

	bool enabled = (pGo->GetJointLimits()->m_debugDrawFlags & JointLimits::kDebugDrawText) != 0;
	switch (message)
	{
		case DMENU::kExecute:
			EngineComponents::GetNdGameInfo()->m_jointLimitDebugDraw = true;
			pGo->GetJointLimits()->m_debugDraw = true;

			enabled = !enabled;
			if (enabled)
				pGo->GetJointLimits()->m_debugDrawFlags |= JointLimits::kDebugDrawText;
			else
				pGo->GetJointLimits()->m_debugDrawFlags &= ~JointLimits::kDebugDrawText;
			break;
	}

	return enabled;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitObjectDisableLimitsFunc(DMENU::Item& item, DMENU::Message msg)
{
	Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid);
	if (!pProc)
		return false;

	NdGameObject* pGo = static_cast<NdGameObject*>(pProc);
	bool enable = pGo->GetJointLimits()->m_debugDisableLimits;
	if (msg == DMENU::kExecute)
	{
		enable = !enable;
		pGo->GetJointLimits()->m_debugDisableLimits = enable;
		EngineComponents::GetNdGameInfo()->m_jointLimitDebugDraw = true;
		pGo->GetJointLimits()->m_debugDraw = true;
	}

	return enable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitObjectDrawSwingLimitExceededFunc(DMENU::Item& item, DMENU::Message message)
{
	Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid);
	if (!pProc)
		return false;

	NdGameObject* pGo = static_cast<NdGameObject*>(pProc);
	JointLimits* pJointLimits = pGo->GetJointLimits();

	bool enabled = (pGo->GetJointLimits()->m_debugDrawFlags & JointLimits::kDebugDrawSwingLimitHit) != 0;
	switch (message)
	{
		case DMENU::kExecute:
			EngineComponents::GetNdGameInfo()->m_jointLimitDebugDraw = true;
			pGo->GetJointLimits()->m_debugDraw = true;

			enabled = !enabled;
			if (enabled)
				pGo->GetJointLimits()->m_debugDrawFlags |= JointLimits::kDebugDrawSwingLimitHit;
			else
				pGo->GetJointLimits()->m_debugDrawFlags &= ~JointLimits::kDebugDrawSwingLimitHit;
			break;
	}

	return enabled;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitObjectDrawTwistLimitExceededFunc(DMENU::Item& item, DMENU::Message message)
{
	Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid);
	if (!pProc)
		return false;

	NdGameObject* pGo = static_cast<NdGameObject*>(pProc);
	JointLimits* pJointLimits = pGo->GetJointLimits();

	bool enabled = (pGo->GetJointLimits()->m_debugDrawFlags & JointLimits::kDebugDrawTwistLimitHit) != 0;
	switch (message)
	{
		case DMENU::kExecute:
			EngineComponents::GetNdGameInfo()->m_jointLimitDebugDraw = true;
			pGo->GetJointLimits()->m_debugDraw = true;

			enabled = !enabled;
			if (enabled)
				pGo->GetJointLimits()->m_debugDrawFlags |= JointLimits::kDebugDrawTwistLimitHit;
			else
				pGo->GetJointLimits()->m_debugDrawFlags &= ~JointLimits::kDebugDrawTwistLimitHit;
			break;
	}

	return enabled;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitOutputSelected(DMENU::Item& item, DMENU::Message message)
{
	if (message == DMENU::kExecute)
	{
		Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid);
		if (!pProc)
			return false;

		NdGameObject* pGo = static_cast<NdGameObject*>(pProc);
		JointLimits* pJointLimits = pGo->GetJointLimits();

		for (int i=0; i<pJointLimits->GetNumJoints(); i++)
		{
			if (pJointLimits->GetJointData(i)->m_debugDraw)
				pJointLimits->GetJointData(i)->DumpToTTY();
		}

		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitOutputAll(DMENU::Item& item, DMENU::Message message)
{
	if (message == DMENU::kExecute)
	{
		Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid);
		if (!pProc)
			return false;

		NdGameObject* pGo = static_cast<NdGameObject*>(pProc);
		JointLimits* pJointLimits = pGo->GetJointLimits();

		for (int i=0; i<pJointLimits->GetNumJoints(); i++)
		{
			pJointLimits->GetJointData(i)->DumpToTTY();
		}

		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitReload(DMENU::Item& item, DMENU::Message message)
{
	if (message == DMENU::kExecute)
	{
		Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid);
		if (!pProc)
			return false;

		NdGameObject* pGo = static_cast<NdGameObject*>(pProc);
		JointLimits* pJointLimits = pGo->GetJointLimits();
		pJointLimits->Reload(pGo);

		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitJointEnableDebugFunc(DMENU::Item& item, DMENU::Message msg)
{
	NdGameObject* pGo = NdGameObject::FromProcess(EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid));
	if (!pGo)
		return false;

	JointLimits* pJointLimits = pGo->GetJointLimits();
	if (!pJointLimits)
		return false;

	const I64 jointIndex = (intptr_t)item.m_pBlindData;
	JointLimits::JointData* pJointData = pJointLimits->GetJointData(jointIndex);

	bool enable = pJointData->m_debugDraw;

	if (msg == DMENU::kExecute)
	{
		pJointLimits->m_debugDraw = true;
		EngineComponents::GetNdGameInfo()->m_jointLimitDebugDraw = true;

		enable = !enable;
		pJointData->m_debugDraw = enable;
	}

	return enable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitExecuteObjectDebugMenu(DMENU::ItemSubmenu& item, DMENU::Message msg)
{
	Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(item.m_idSid);
	if (!pProc)
	{
		DMENU::Menu* pMenu = item.GetMenu();
		pMenu->DeleteAllItems();
		return false;
	}

	NdGameObject* pGo = static_cast<NdGameObject*>(pProc);
	if (msg == DMENU::kExecute || (pGo && pGo->GetJointLimits() && pGo->GetJointLimits()->m_debugRefreshMenu && msg == DMENU::kRefresh))
	{
		AllocateJanitor jj(kAllocDebugDevMenu, FILE_LINE_FUNC);

		StringId64 procId = pGo->GetUserId();

		JointLimits* pJointLimits = pGo->GetJointLimits();

		DMENU::ItemBool* pItemBool;
		DMENU::ItemFloat* pItemFloat;
		DMENU::ItemFunction* pItemFunc;

		DMENU::Menu* pMenu = item.GetMenu();
		pMenu->DeleteAllItems();

		pItemBool = NDI_NEW DMENU::ItemBool("Enable Debug", JointLimitObjectEnableDebugFunc);
		pItemBool->m_id = procId.GetValue();
		pMenu->PushBackItem(pItemBool);

		pItemFloat = NDI_NEW DMENU::ItemFloat("Cone Size", JointLimitObjectConeSizeFunc, 3, "%0.2f", nullptr, DMENU::FloatRange(0.01f, 10.0f), DMENU::FloatSteps(0.01f, 0.1f));
		pItemFloat->m_id = procId.GetValue();
		pMenu->PushBackItem(pItemFloat);

		pItemBool = NDI_NEW DMENU::ItemBool("Draw Cone", JointLimitObjectDrawConeFunc);
		pItemBool->m_id = procId.GetValue();
		pMenu->PushBackItem(pItemBool);

		pItemBool = NDI_NEW DMENU::ItemBool("Draw Cone Axes", JointLimitObjectDrawConeAxesFunc);
		pItemBool->m_id = procId.GetValue();
		pMenu->PushBackItem(pItemBool);

		pItemBool = NDI_NEW DMENU::ItemBool("Draw Cone Vis Point", JointLimitObjectDrawConeVisPointFunc);
		pItemBool->m_id = procId.GetValue();
		pMenu->PushBackItem(pItemBool);

		pItemBool = NDI_NEW DMENU::ItemBool("Draw Cone Twist", JointLimitObjectDrawConeTwistFunc);
		pItemBool->m_id = procId.GetValue();
		pMenu->PushBackItem(pItemBool);

		pItemBool = NDI_NEW DMENU::ItemBool("Draw Joint Dir", JointLimitObjectDrawJointDirFunc);
		pItemBool->m_id = procId.GetValue();
		pMenu->PushBackItem(pItemBool);

		pItemBool = NDI_NEW DMENU::ItemBool("Draw Bind Pose Loc", JointLimitObjectDrawBindPoseLocFunc);
		pItemBool->m_id = procId.GetValue();
		pMenu->PushBackItem(pItemBool);

		pItemBool = NDI_NEW DMENU::ItemBool("Draw Text", JointLimitObjectDrawTextFunc);
		pItemBool->m_id = procId.GetValue();
		pMenu->PushBackItem(pItemBool);

		pItemBool = NDI_NEW DMENU::ItemBool("Validate Cone Twist", JointLimitObjectValidateConeTwistFunc);
		pItemBool->m_id = procId.GetValue();
		pMenu->PushBackItem(pItemBool);

		pMenu->PushBackItem(NDI_NEW DMENU::ItemDivider());

		pItemBool = NDI_NEW DMENU::ItemBool("Disable Limits", JointLimitObjectDisableLimitsFunc);
		pItemBool->m_id = procId.GetValue();
		pMenu->PushBackItem(pItemBool);

		pItemBool = NDI_NEW DMENU::ItemBool("Draw Swing Limit Exceeded", JointLimitObjectDrawSwingLimitExceededFunc);
		pItemBool->m_id = procId.GetValue();
		pMenu->PushBackItem(pItemBool);

		pItemBool = NDI_NEW DMENU::ItemBool("Draw Twist Limit Exceeded", JointLimitObjectDrawTwistLimitExceededFunc);
		pItemBool->m_id = procId.GetValue();
		pMenu->PushBackItem(pItemBool);

		pMenu->PushBackItem(NDI_NEW DMENU::ItemDivider());

		pItemFunc = NDI_NEW DMENU::ItemFunction("Output Selected Joints", JointLimitOutputSelected);
		pItemFunc->m_id = procId.GetValue();
		pMenu->PushBackItem(pItemFunc);

		pItemFunc = NDI_NEW DMENU::ItemFunction("Output All Joints", JointLimitOutputAll);
		pItemFunc->m_id = procId.GetValue();
		pMenu->PushBackItem(pItemFunc);

		pItemFunc = NDI_NEW DMENU::ItemFunction("Reload Joint Limit Data", JointLimitReload);
		pItemFunc->m_id = procId.GetValue();
		pMenu->PushBackItem(pItemFunc);

		pMenu->PushBackItem(NDI_NEW DMENU::ItemDivider());

		for (int i = 0; i < pJointLimits->GetNumJoints(); i++)
		{
			StringId64 jointId = pGo->GetAnimData()->m_pJointDescs[pJointLimits->GetJointData(i)->m_jointIndex].m_nameId;

			pItemBool = NDI_NEW DMENU::ItemBool(DevKitOnly_StringIdToString(jointId), JointLimitJointEnableDebugFunc, (void*)(uintptr_t)i);
			pItemBool->m_id = procId.GetValue();
			pMenu->PushBackItem(pItemBool);
		}

		pGo->GetJointLimits()->m_debugRefreshMenu = false;
		EngineComponents::GetNdGameInfo()->m_jointLimitDebugDraw = true;

		//JointLimitUpdateDebugObjectMenu(pMenu);
		return true;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AddJointLimitObjectMenu(Process* pProcess, void* pMenuV)
{
	DMENU::Menu* pMenu = static_cast<DMENU::Menu*>(pMenuV);

	char name[64], text[64];

	if (pProcess->IsKindOf(g_type_NdGameObject))
	{
		NdGameObject* pObject = static_cast<NdGameObject*>(pProcess);
		if (pObject->GetJointLimits())
		{
			AllocateJanitor jj(kAllocDebugDevMenu, FILE_LINE_FUNC);

			sprintf(name, "%s", pObject->GetName());
			sprintf(text, "%s...", pObject->GetName());
			DMENU::Menu* pObjectMenu = NDI_NEW DMENU::Menu(name);

			DMENU::ItemSubmenu* pSubMenu = NDI_NEW DMENU::ItemSubmenu(text, pObjectMenu, JointLimitExecuteObjectDebugMenu);
			pSubMenu->m_id = pProcess->GetUserId().GetValue();
			pMenu->PushBackItem(pSubMenu);

			//SplasherUpdateDebugObjectSkelMenu(pObject, pSkelMenu);
			//SplasherUpdateDebugObjectSplasherMenu(pObject, pSplasherMenu);
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointLimitUpdateDebugMenu(DMENU::Menu* pMenu)
{
	if (!pMenu)
		return;

	ANIM_ASSERT(Memory::IsDebugMemoryAvailable());

	AllocateJanitor jj(kAllocDebugDevMenu, FILE_LINE_FUNC);

	pMenu->DeleteAllItems();

	EngineComponents::GetProcessMgr()->WalkTree(EngineComponents::GetProcessMgr()->m_pRootTree, AddJointLimitObjectMenu, pMenu);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitExecuteDebugMenu(DMENU::ItemSubmenu& item, DMENU::Message msg)
{
	if (msg == DMENU::kExecute)
	{
		DMENU::Menu* pMenu = item.GetMenu();
		JointLimitUpdateDebugMenu(pMenu);
		return true;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointLimitDebugDrawFunc(Process* pProcess, void*)
{
	STRIP_IN_FINAL_BUILD;

	const NdGameObject* pGo = NdGameObject::FromProcess(pProcess);
	if (pGo)
	{
		const JointLimits* pJointLimits = pGo->GetJointLimits();
		if (pJointLimits)
			pJointLimits->DebugDraw(pGo);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawJointLimits()
{
	STRIP_IN_FINAL_BUILD;
	if (EngineComponents::GetNdGameInfo()->m_jointLimitDebugDraw)
		EngineComponents::GetProcessMgr()->WalkTree(EngineComponents::GetProcessMgr()->m_pRootTree, JointLimitDebugDrawFunc, nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool JointLimitReloadAllFunc(Process* pProcess, void*)
{
	STRIP_IN_FINAL_BUILD;

	NdGameObject* pGo = NdGameObject::FromProcess(pProcess);
	if (pGo)
	{
		JointLimits* pJointLimits = pGo->GetJointLimits();
		if (pJointLimits)
		{
			pJointLimits->Reload(pGo);
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointLimitReloadAll()
{
	STRIP_IN_FINAL_BUILD;
	EngineComponents::GetProcessMgr()->WalkTree(EngineComponents::GetProcessMgr()->m_pRootTree, JointLimitReloadAllFunc, nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointLimits::JointData::DumpToTTY()
{
	MsgPlayer("---------- %s ----------\n", DevKitOnly_StringIdToString(m_jointName));

	MsgPlayer(":cone-center-dir			(new-vector %.5f %.5f %.5f)\n", (float)m_data.m_coneVisPointPs.X(), (float)m_data.m_coneVisPointPs.Y(), (float)m_data.m_coneVisPointPs.Z());
	MsgPlayer(":cone-center-twist-min	%.2f\n", RADIANS_TO_DEGREES(m_data.m_visPointTwistCenter - m_data.m_visPointTwistRange));
	MsgPlayer(":cone-center-twist-max	%.2f\n", RADIANS_TO_DEGREES(m_data.m_visPointTwistCenter + m_data.m_visPointTwistRange));
	MsgPlayer(":cone-points (ik-cone-point-array\n");
	for (int i=0; i<m_data.m_coneNumBoundaryPoints; i++)
	{
		float x = m_data.m_coneBoundaryPointsPs[i].X();
		float y = m_data.m_coneBoundaryPointsPs[i].Y();
		float z = m_data.m_coneBoundaryPointsPs[i].Z();
		MsgPlayer("	(new ik-cone-point :joint-rot (new-vector %s%.5f %s%.5f %s%.5f) :twist-min %.2f :twist-max %.2f)\n",
			x < 0.0f ? "" : " ", x, y < 0.0f ? "" : " ", y, z < 0.0f ? "" : " ", z,
			RADIANS_TO_DEGREES(m_data.m_coneBoundryTwistCenter[i] - m_data.m_coneBoundryTwistRange[i]),
			RADIANS_TO_DEGREES(m_data.m_coneBoundryTwistCenter[i] + m_data.m_coneBoundryTwistRange[i]));
	}
	MsgPlayer(")\n\n");
}
#endif
