/*
* Copyright (c) 2016 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/anim/motion-matching/motion-pose.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/nd-anim-util.h"

#include "gamelib/anim/motion-matching/motion-matching-def.h"
#include "gamelib/anim/motion-matching/motion-matching-set.h"
#include "gamelib/gameplay/human-center-of-mass.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"
#include "gamelib/scriptx/h/motion-matching-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static float GetPrevPhase(const ArtItemAnim* pAnim, float curPhase, float phasePerFrame)
{
	if (!pAnim || phasePerFrame < NDI_FLT_EPSILON)
		return curPhase;

	float prevPhase = curPhase;

	if (pAnim->IsLooping())
	{
		if (curPhase < phasePerFrame)
		{
			prevPhase = Fmod(1.0f - curPhase + phasePerFrame);
		}
		else
		{
			prevPhase = curPhase - phasePerFrame;
		}
	}
	else
	{
		prevPhase = Limit01(curPhase - phasePerFrame);
	}

	return prevPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
MotionMatchingPose* MotionMatchingPose::CreatePoseFromSample(const MMPose& poseDef, const AnimSample& animSample)
{
	PROFILE_AUTO(Animation);

	const ArtItemAnim* pAnim = animSample.Anim().ToArtItem();
	const ArtItemSkeleton* pSkel = pAnim ? ResourceTable::LookupSkel(pAnim->m_skelID).ToArtItem() : nullptr;

	if (!pAnim || !pSkel)
		return nullptr;

	if (!IsSkeletonValidForPoseDef(pSkel, poseDef))
	{
		return nullptr;
	}

	Memory::Allocator* pTopAllocator = Memory::TopAllocator();
	MotionMatchingPose* pPose = nullptr;
	
	{
		ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

		const float ppf = pAnim->m_pClipData->m_phasePerFrame;
		const float curPhase = animSample.Phase();
		const float prevPhase = GetPrevPhase(pAnim, curPhase, ppf);
		const bool hasPrevPhase = Abs(curPhase - prevPhase) > NDI_FLT_EPSILON;

		const AnimateFlags flags = animSample.Mirror() ? AnimateFlags::kAnimateFlag_Mirror : AnimateFlags::kAnimateFlag_None;

		Transform* pCurJoints = NDI_NEW Transform[pSkel->m_numGameplayJoints];
		Transform* pPrevJoints = pCurJoints;

		bool valid = true;

		valid = valid
				&& AnimateObject(Transform(kIdentity),
								 pSkel,
								 pAnim,
								 pAnim->m_pClipData->m_numTotalFrames * curPhase,
								 pCurJoints,
								 nullptr,
								 nullptr,
								 nullptr,
								 flags);

		if (hasPrevPhase)
		{
			pPrevJoints = NDI_NEW Transform[pSkel->m_numGameplayJoints];
			valid = valid && AnimateObject(Transform(kIdentity),
											  pSkel,
											  pAnim,
											  pAnim->m_pClipData->m_numTotalFrames * prevPhase,
											  pPrevJoints,
											  nullptr,
											  nullptr,
											  nullptr,
											  flags);
		}

		const float deltaPhase = pAnim->IsLooping() ? ppf : (curPhase - prevPhase);
		const float deltaTime = GetDuration(pAnim) * deltaPhase;

		if (valid)
		{
			AllocateJanitor topAlloc(pTopAllocator, FILE_LINE_FUNC);
			pPose = NDI_NEW MotionMatchingPose;

			for (I32F iBody = 0; iBody < poseDef.m_numBodies; ++iBody)
			{
				const MMPoseBody& body = poseDef.m_aBodies[iBody];

				if (body.m_isCenterOfMass)
				{
					const Point comPos = HumanCenterOfMass::ComputeCenterOfMassFromJoints(pAnim->m_skelID, pCurJoints);
					Vector comVel = kZero;

					if (deltaTime > NDI_FLT_EPSILON)
					{
						const Point prevComPos = HumanCenterOfMass::ComputeCenterOfMassFromJoints(pAnim->m_skelID,
																								  pPrevJoints);
						comVel = (comPos - prevComPos) / deltaTime;
					}

					pPose->m_comOs.m_pos = comPos;
					pPose->m_comOs.m_vel = comVel;
					continue;
				}

				const I32F iJoint = FindJoint(pSkel->m_pJointDescs, pSkel->m_numGameplayJoints, body.m_jointId);
				ANIM_ASSERT(iJoint >= 0 && iJoint < pSkel->m_numGameplayJoints);

				if (iJoint < 0 || iJoint >= pSkel->m_numGameplayJoints)
					continue;

				if (pPose->m_numJoints < kMaxJoints)
				{
					JointData& jointEntry = pPose->m_jointsOs[pPose->m_numJoints];
					jointEntry.m_loc = Locator(pCurJoints[iJoint]);
					jointEntry.m_vel = kZero;

					if (deltaTime > NDI_FLT_EPSILON)
					{
						jointEntry.m_vel = (pCurJoints[iJoint].GetTranslation() - pPrevJoints[iJoint].GetTranslation()) / deltaTime;
					}

					++pPose->m_numJoints;
				}
			}

			const I32F iFacingJoint = FindJoint(pSkel->m_pJointDescs, pSkel->m_numGameplayJoints, poseDef.m_facingJointId);

			if (iFacingJoint >= 0)
			{
				const Locator facingJointOs = Locator(pCurJoints[iFacingJoint]);

				pPose->m_facingOs = AsUnitVectorXz(-GetLocalY(facingJointOs), kUnitZAxis);
			}
		}
	}

	return pPose;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
MotionMatchingPose* MotionMatchingPose::CreatePoseFromOther(const MMPose& poseDef, const IMotionPose& otherPose)
{
	if (!IsPoseValidForPoseDef(otherPose, poseDef))
	{
		return nullptr;
	}

	MotionMatchingPose* pPose = NDI_NEW MotionMatchingPose;

	for (I32F iBody = 0; iBody < poseDef.m_numBodies; ++iBody)
	{
		const MMPoseBody& body = poseDef.m_aBodies[iBody];

		if (body.m_isCenterOfMass)
		{
			pPose->m_comOs = otherPose.GetCenterOfMassOs();
			continue;
		}

		if (pPose->m_numJoints < kMaxJoints)
		{
			BodyData otherData = otherPose.GetJointDataByIdOs(body.m_jointId);

			JointData& jointEntry = pPose->m_jointsOs[pPose->m_numJoints];

			jointEntry.m_jointId = body.m_jointId;
			jointEntry.m_loc = otherPose.GetJointLocatorOs(body.m_jointId);
			jointEntry.m_vel = otherData.m_vel;

			++pPose->m_numJoints;
		}
	}

	pPose->m_facingOs = otherPose.GetFacingOs();

	return pPose;
}

/// --------------------------------------------------------------------------------------------------------------- ///
IMotionPose* MotionMatchingPose::Clone() const
{
	MotionMatchingPose* pPose = NDI_NEW MotionMatchingPose;
	*pPose = *this;
	return pPose;
}

/// --------------------------------------------------------------------------------------------------------------- ///
IMotionPose::BodyData MotionMatchingPose::GetJointDataByIdOs(StringId64 jointId) const
{
	for (U32F iJoint = 0; iJoint < m_numJoints; ++iJoint)
	{
		if (m_jointsOs[iJoint].m_jointId == jointId)
		{
			return { m_jointsOs[iJoint].m_loc.Pos(), m_jointsOs[iJoint].m_vel };
		}
	}

	return { kOrigin, kZero };
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator MotionMatchingPose::GetJointLocatorOs(StringId64 jointId) const
{
	for (U32F iJoint = 0; iJoint < m_numJoints; ++iJoint)
	{
		if (m_jointsOs[iJoint].m_jointId == jointId)
		{
			return m_jointsOs[iJoint].m_loc;
		}
	}

	return kIdentity;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MotionMatchingPose::HasDataForJointId(StringId64 jointId) const
{
	bool valid = false;

	for (U32F iJoint = 0; iJoint < m_numJoints; ++iJoint)
	{
		if (m_jointsOs[iJoint].m_jointId == jointId)
		{
			valid = true;
			break;
		}
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingPose::UpdateJointOs(StringId64 jointId, const Locator& locOs, Vector_arg velOs)
{
	for (U32F iJoint = 0; iJoint < m_numJoints; ++iJoint)
	{
		if (m_jointsOs[iJoint].m_jointId == jointId)
		{
			m_jointsOs[iJoint].m_loc = locOs;
			m_jointsOs[iJoint].m_vel = velOs;
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingPose::DebugDraw(const Locator& charLocWs,
								   Color drawColor,
								   DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	for (U32F iJoint = 0; iJoint < m_numJoints; ++iJoint)
	{
		const Locator jointLocWs = charLocWs.TransformLocator(m_jointsOs[iJoint].m_loc);

		g_prim.Draw(DebugCoordAxesLabeled(jointLocWs,
										  DevKitOnly_StringIdToString(m_jointsOs[iJoint].m_jointId),
										  0.2f,
										  kPrimEnableHiddenLineAlpha,
										  2.0f,
										  kColorWhite,
										  0.5f), tt);

		g_prim.Draw(DebugArrow(jointLocWs.Pos(),
							   charLocWs.TransformVector(m_jointsOs[iJoint].m_vel) * 0.1f,
							   kColorYellowTrans,
							   0.5f,
							   kPrimEnableHiddenLineAlpha), tt);
	}

	const Point comPosWs = charLocWs.TransformPoint(m_comOs.m_pos);
	g_prim.Draw(DebugCross(comPosWs, 0.25f, kColorOrange, kPrimEnableHiddenLineAlpha), tt);
	g_prim.Draw(DebugString(comPosWs, "COM", kColorOrange, 0.5f));

	g_prim.Draw(DebugArrow(comPosWs,
						   charLocWs.TransformVector(m_comOs.m_vel) * 0.1f,
						   kColorOrangeTrans,
						   0.25f,
						   kPrimEnableHiddenLineAlpha),
				tt);

	g_prim.Draw(DebugArrow(charLocWs.Pos() + kUnitYAxis, charLocWs.TransformVector(m_facingOs), kColorCyan));
	g_prim.Draw(DebugString(charLocWs.Pos() + kUnitYAxis + charLocWs.TransformVector(m_facingOs),
							"facing",
							kColorCyan,
							0.5f));
}

/// --------------------------------------------------------------------------------------------------------------- ///
RecordedMotionPose::RecordedMotionPose(const IMotionPose& sourcePose, const MMPose& poseDef)
{
	m_comDataOs = sourcePose.GetCenterOfMassOs();
	
	m_numJointEntries = 0;

	for (I32F i = 0; i < poseDef.m_numBodies; i++)
	{
		if (m_numJointEntries >= kMaxParts)
			break;

		const MMPoseBody& body = poseDef.m_aBodies[i];

		if (body.m_isCenterOfMass)
			continue;

		m_jointDataOs[m_numJointEntries] = sourcePose.GetJointDataByIdOs(body.m_jointId);
		m_jointIds[m_numJointEntries]	 = body.m_jointId;

		++m_numJointEntries;
	}

	m_facingOs		   = sourcePose.GetFacingOs();
	m_facingJointId	   = poseDef.m_facingJointId;
	m_facingJointLocOs = sourcePose.GetJointLocatorOs(poseDef.m_facingJointId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
IMotionPose* RecordedMotionPose::Clone() const
{
	return NDI_NEW RecordedMotionPose(*this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
IMotionPose::BodyData RecordedMotionPose::GetJointDataByIdOs(StringId64 jointId) const
{
	bool valid = false;

	BodyData ret;
	ret.m_pos = kOrigin;
	ret.m_vel = kZero;

	for (U32F i = 0; i < m_numJointEntries; ++i)
	{
		if (m_jointIds[i] == jointId)
		{
			ret = m_jointDataOs[i];
			valid = true;
			break;
		}
	}

	ANIM_ASSERT(valid);

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator RecordedMotionPose::GetJointLocatorOs(StringId64 jointId) const
{
	ANIM_ASSERT(jointId == m_facingJointId);

	return m_facingJointLocOs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool RecordedMotionPose::HasDataForJointId(StringId64 jointId) const
{
	if (jointId == m_facingJointId)
		return true;

	bool valid = false;

	for (U32F i = 0; i < m_numJointEntries; ++i)
	{
		if (jointId == m_jointIds[i])
		{
			valid = true;
			break;
		}
	}

	return valid;
}
