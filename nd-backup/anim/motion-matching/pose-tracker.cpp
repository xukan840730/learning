/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/motion-matching/pose-tracker.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"

#include "gamelib/gameplay/human-center-of-mass.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-skeleton.h"

/// --------------------------------------------------------------------------------------------------------------- ///
SUBSYSTEM_UPDATE_DEF(PoseTracker, PostJointUpdate)
{
	const I32F prevPoseIndex = m_currentPoseIndex;
	m_currentPoseIndex = (m_currentPoseIndex + 1) % ARRAY_COUNT(m_pose);

	BaseSegmentMotionPose& curPose = m_pose[m_currentPoseIndex];
	BaseSegmentMotionPose& prevPose = m_pose[prevPoseIndex];
	
	const NdGameObject* pOwner = GetOwnerGameObject();
		
	m_poseLocs[m_currentPoseIndex] = pOwner->GetBoundFrame();
	m_poseTime[m_currentPoseIndex] = pOwner->GetClock()->GetCurTime();

	if (m_teleported)
	{
		m_poseLocs[prevPoseIndex] = m_poseLocs[m_currentPoseIndex];
		m_poseTime[prevPoseIndex] = m_poseTime[m_currentPoseIndex];
		m_teleported = false;
	}

	const float dt = (m_poseTime[m_currentPoseIndex] - m_poseTime[prevPoseIndex]).ToSeconds();

	const Locator& curPosePsLoc = pOwner->GetLocatorPs();
	const Locator& curParentSpace = pOwner->GetParentSpace();

	const Locator& prevPosePsLoc = m_poseLocs[prevPoseIndex].GetLocatorPs();
	
	// Maybe we'll want to try and preserve velocity across parent spaces but for now, eh...
	const bool changedParentSpace = !m_poseLocs[m_currentPoseIndex].IsSameBinding(m_poseLocs[prevPoseIndex].GetBinding());

	if (const JointCache* pJointCache = pOwner->GetAnimControl()->GetJointCache())
	{	
		for (int i = 0; i < curPose.m_jointLocsOs.Size(); i++)
		{
			const Locator& jointWs = pJointCache->GetJointLocatorWs(i);
			const Locator& jointPs = curParentSpace.UntransformLocator(jointWs);
			const Point prevPsPos = prevPosePsLoc.TransformPoint(prevPose.m_jointLocsOs[i].m_loc.Pos());

			const Vector velPs = (dt > NDI_FLT_EPSILON) ? ((jointPs.Pos() - prevPsPos) / dt) : kZero;
			const Vector velOs = curPosePsLoc.UntransformVector(velPs);

			curPose.m_jointLocsOs[i].m_loc = curPosePsLoc.UntransformLocator(jointPs);
			curPose.m_jointLocsOs[i].m_vel = changedParentSpace ? kZero : velOs;
		}
	}

	{
		const Point comWs = HumanCenterOfMass::ComputeCenterOfMassWs(*pOwner->GetAttachSystem());
		const Point comPs = pOwner->GetParentSpace().UntransformPoint(comWs);
		const Point prevComPs = prevPosePsLoc.TransformPoint(prevPose.m_comOs.m_loc.Pos());

		const Vector velPs = (dt > NDI_FLT_EPSILON) ? ((comPs - prevComPs) / dt) : kZero;
		const Vector velOs = curPosePsLoc.UntransformVector(velPs);
		curPose.m_comOs.m_loc.SetTranslation(curPosePsLoc.UntransformPoint(comPs));
		curPose.m_comOs.m_vel = changedParentSpace ? kZero : curPosePsLoc.UntransformVector(velPs);
	}

	m_numPoses++;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err PoseTracker::Init(const SubsystemSpawnInfo& info)
{
	Err result = ParentClass::Init(info);

	const NdGameObject* pOwner = GetOwnerGameObject();

	m_poseLocs[m_currentPoseIndex] = pOwner->GetBoundFrame();
	m_poseTime[m_currentPoseIndex] = pOwner->GetCurTime();

	if (result.Succeeded())
	{
		ArtItemSkeletonHandle hSkel = ResourceTable::LookupSkel(pOwner->GetSkeletonId());

		for (I32F iPose = 0; iPose < ARRAY_COUNT(m_pose); iPose++)
		{
			m_pose[iPose].Init(hSkel);
		}
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const IMotionPose& PoseTracker::GetPose() const
{
	return m_pose[m_currentPoseIndex];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PoseTracker::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	for (int iPose = 0; iPose < ARRAY_COUNT(m_pose); iPose++)
	{
		m_pose[iPose].Relocate(deltaPos, lowerBound, upperBound);
	}

	ParentClass::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
IMotionPose* BaseSegmentMotionPose::Clone() const
{
	BaseSegmentMotionPose* pClone = NDI_NEW BaseSegmentMotionPose();

	pClone->m_hSkel = m_hSkel;
	pClone->m_comOs = m_comOs;
	pClone->m_jointLocsOs.Init(m_jointLocsOs.Size(), FILE_LINE_FUNC);
	pClone->m_jointLocsOs.Resize(m_jointLocsOs.Size());

	memcpy(pClone->m_jointLocsOs.Begin(), m_jointLocsOs.Begin(), m_jointLocsOs.Size() * sizeof(JointData));

	return pClone;
}

/// --------------------------------------------------------------------------------------------------------------- ///
IMotionPose::BodyData BaseSegmentMotionPose::GetCenterOfMassOs() const
{
	BodyData ret;
	ret.m_pos = m_comOs.m_loc.GetTranslation();
	ret.m_vel = m_comOs.m_vel;
	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
IMotionPose::BodyData BaseSegmentMotionPose::GetJointDataByIdOs(StringId64 jointId) const
{
	BodyData ret;

	int jointIndex = JointIdToIndex(jointId);
	if (jointIndex >= 0 && jointIndex < m_jointLocsOs.Size())
	{
		ret.m_pos = m_jointLocsOs[jointIndex].m_loc.GetTranslation();
		ret.m_vel = m_jointLocsOs[jointIndex].m_vel;
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator BaseSegmentMotionPose::GetJointLocatorOs(StringId64 jointId) const
{
	int jointIndex = JointIdToIndex(jointId);
	if (jointIndex >= 0 && jointIndex < m_jointLocsOs.Size())
	{
		return m_jointLocsOs[jointIndex].m_loc;
	}
	else
	{
		return kIdentity;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool BaseSegmentMotionPose::HasDataForJointId(StringId64 jointId) const
{
	return JointIdToIndex(jointId) >= 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BaseSegmentMotionPose::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	m_jointLocsOs.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BaseSegmentMotionPose::DebugDraw(const Locator& charLocWs,
									  Color drawColor,
									  DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	for (const JointData& joint : m_jointLocsOs)
	{
		const Point bodyPosWs = charLocWs.TransformPoint(joint.m_loc.Pos());
		const Vector bodyVelWs = charLocWs.TransformVector(joint.m_vel);

		g_prim.Draw(DebugSphere(bodyPosWs, 0.1f, drawColor), tt);
		g_prim.Draw(DebugArrow(bodyPosWs, bodyVelWs / 10.0f, drawColor), tt);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BaseSegmentMotionPose::Init(ArtItemSkeletonHandle hSkel)
{
	const ArtItemSkeleton* pSkel = hSkel.ToArtItem();
	ANIM_ASSERT(pSkel);

	m_hSkel = hSkel;

	const size_t numJoints = pSkel->m_numGameplayJoints;

	m_jointLocsOs.Init(numJoints, FILE_LINE_FUNC);
	m_jointLocsOs.Resize(numJoints);

	for (U32F i = 0; i < numJoints; i++)
	{
		m_jointLocsOs[i].m_loc = kIdentity;
		m_jointLocsOs[i].m_vel = kZero;
	}
	m_comOs.m_loc = kIdentity;
	m_comOs.m_vel = kZero;

	//Fill with bindpose
	const ndanim::JointHierarchy* pJointHier = pSkel->m_pAnimHierarchy;	
	const Mat34* pInvBindPose = GetInverseBindPoseTable(pJointHier);

	for (U32F i = 0; i < numJoints; i++)
	{
		const Mat34& invBP = pInvBindPose[i];
		Mat44 bindPoseMat44 = Inverse(invBP.GetMat44());
		RemoveScale(&bindPoseMat44);
		m_jointLocsOs[i].m_loc = Locator(bindPoseMat44);
	}

	{
		ScopedTempAllocator alloc(FILE_LINE_FUNC);
		Locator* aJointLocs = NDI_NEW Locator[numJoints];
		for (U32F i = 0; i < numJoints; i++)
		{
			aJointLocs[i] = m_jointLocsOs[i].m_loc;
		}

		const Point com = HumanCenterOfMass::ComputeCenterOfMassFromJoints(pSkel->m_skelId, aJointLocs);
		m_comOs.m_loc.SetTranslation(com);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
int BaseSegmentMotionPose::JointIdToIndex(StringId64 jointId) const
{
	if (const ArtItemSkeleton* pSkel = m_hSkel.ToArtItem())
	{
		return FindJoint(pSkel->m_pJointDescs, pSkel->m_numGameplayJoints, jointId);
	}
	else
	{
		return -1;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ForceLinkPoseTracker()
{
}

TYPE_FACTORY_REGISTER(PoseTracker, NdSubsystem);
