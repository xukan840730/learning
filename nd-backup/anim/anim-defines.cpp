/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-defines.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/nd-frame-state.h"

#include "gamelib/level/art-item-anim.h"

#include <orbisanim/animclip.h>
#include <orbisanim/animhierarchy.h>
#include <orbisanim/structs.h>
#include <orbisanim/util.h>

/// --------------------------------------------------------------------------------------------------------------- ///
STATIC_ASSERT(sizeof(ndanim::JointParams) == (3 * 16));
STATIC_ASSERT(sizeof(OrbisAnim::JointParams) == sizeof(ndanim::JointParams));

/// --------------------------------------------------------------------------------------------------------------- ///
// Verify blend enum values
STATIC_ASSERT(OrbisAnim::kBlendLerp == (OrbisAnim::BlendMode)ndanim::kBlendLerp);
STATIC_ASSERT(OrbisAnim::kBlendSlerp == (OrbisAnim::BlendMode)ndanim::kBlendSlerp);
STATIC_ASSERT(OrbisAnim::kBlendAdditive == (OrbisAnim::BlendMode)ndanim::kBlendAdditive);
STATIC_ASSERT(OrbisAnim::kBlendMultiply == (OrbisAnim::BlendMode)ndanim::kBlendMultiply);
STATIC_ASSERT(OrbisAnim::kBlendAddToAdditive == (OrbisAnim::BlendMode)ndanim::kBlendAddToAdditive);

/// --------------------------------------------------------------------------------------------------------------- ///
AnimLookupSentinal::AnimLookupSentinal()
{
	m_atActionCounter = AnimMasterTable::kInvalidActionCounter;
	m_stActionCounter = SkelTable::kInvalidActionCounter;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimLookupSentinal::IsValid() const
{
	if (m_atActionCounter != AnimMasterTable::m_actionCounter)
		return false;

	if (m_stActionCounter != SkelTable::m_actionCounter)
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimLookupSentinal::Refresh()
{
	m_atActionCounter = AnimMasterTable::m_actionCounter;
	m_stActionCounter = SkelTable::m_actionCounter;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CachedAnimLookupRaw::Reset(const StringId64 sourceId)
{
	m_sourceId = sourceId;
	m_artItemHandle = ArtItemAnimHandle();
	m_atActionCounter = AnimMasterTable::kInvalidActionCounter;
	m_stActionCounter = SkelTable::kInvalidActionCounter;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CachedAnimLookupRaw::SetFromAnim(ArtItemAnimHandle hAnim)
{
	if (const ArtItemAnim* pAnim = hAnim.ToArtItem())
	{
		m_sourceId = pAnim->GetNameId();
		SetFinalResult(hAnim);
	}
	else
	{
		Reset(INVALID_STRING_ID_64);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CachedAnimLookupRaw::CachedValueValid() const
{
	if (m_atActionCounter != AnimMasterTable::m_actionCounter)
		return false;

	if (m_stActionCounter != SkelTable::m_actionCounter)
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CachedAnimLookupRaw::SetFinalResult(ArtItemAnimHandle artItemAnim)
{
	m_artItemHandle = artItemAnim;
	m_atActionCounter = AnimMasterTable::m_actionCounter;
	m_stActionCounter = SkelTable::m_actionCounter;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CachedAnimLookup::Reset()
{
	m_overlayLookup.Reset();
	m_finalResolvedId = INVALID_STRING_ID_64;
	m_artItemAnim = ArtItemAnimHandle();
	m_atActionCounter = AnimMasterTable::kInvalidActionCounter;
	m_stActionCounter = SkelTable::kInvalidActionCounter;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CachedAnimLookup::CachedValueValid(const AnimOverlays* pOverlays) const
{
	if (m_atActionCounter != AnimMasterTable::m_actionCounter)
		return false;

	if (m_stActionCounter != SkelTable::m_actionCounter)
		return false;

	if (pOverlays && (m_overlayLookup.GetHash() != pOverlays->GetHash()))
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CachedAnimLookup::SetFinalResult(const StringId64 finalId, ArtItemAnimHandle artItemHandle)
{
	m_artItemAnim = artItemHandle;
	m_finalResolvedId = finalId;
	m_atActionCounter = AnimMasterTable::m_actionCounter;
	m_stActionCounter = SkelTable::m_actionCounter;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimCollection::Add(const ArtItemAnim* pAnim)
{
	if (pAnim)
	{
		const bool isSpaceToAdd = m_animCount < kMaxAnimIds;
		if (isSpaceToAdd)
		{
			m_animArray[m_animCount] = pAnim;
			++m_animCount;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
namespace ndanim
{
	const DebugJointParentInfo* GetDebugJointParentInfoTable(const ndanim::JointHierarchy* pHier)
	{
		return (const DebugJointParentInfo*)OrbisAnim::AnimHierarchy_GetDebugJointParentInfoTable(pHier);
	}

	const JointParams* GetDefaultJointPoseTable(const ndanim::JointHierarchy* pHier)
	{
		return (const JointParams*)OrbisAnim::AnimHierarchy_GetDefaultJointPoseTable(pHier);
	}

	const F32* GetDefaultFloatChannelPoseTable(const ndanim::JointHierarchy* pHier)
	{ 
		return OrbisAnim::AnimHierarchy_GetDefaultFloatChannelPoseTable(pHier);
	}

	const Mat34* GetInverseBindPoseTable(const ndanim::JointHierarchy* pHier)
	{
		return (Mat34*)OrbisAnim::AnimHierarchy_GetInverseBindPoseTable(pHier);
	}

	U32 GetFirstProcessingGroupInSegment(const ndanim::JointHierarchy* pHier, U32 segmentIndex)
	{
		return OrbisAnim::AnimHierarchy_GetFirstProcessingGroupInSegment(pHier, segmentIndex);
	}

	U32 GetNumProcessingGroupsInSegment(const ndanim::JointHierarchy* pHier, U32 segmentIndex)
	{
		return OrbisAnim::AnimHierarchy_GetNumProcessingGroupsInSegment(pHier, segmentIndex);
	}

	U32 GetNumProcessingGroups(const ndanim::JointHierarchy* pHier)
	{
		return OrbisAnim::AnimHierarchy_GetNumProcessingGroups(pHier);
	}

	const ProcessingGroup* GetProcessingGroupTable(const ndanim::JointHierarchy* pHier)
	{
		return (const ProcessingGroup*)OrbisAnim::AnimHierarchy_GetProcessingGroupTable(pHier);
	}

	U32 GetNumChannelGroups(const ndanim::JointHierarchy* pHier)
	{
		return OrbisAnim::AnimHierarchy_GetNumChannelGroups(pHier);
	}

	/// Access the number of channel groups in processing group iGroup
	U32 GetNumChannelGroupsInProcessingGroup(const ndanim::JointHierarchy* pHier, U32 processingGroupIndex)
	{
		return OrbisAnim::AnimHierarchy_GetNumChannelGroupsInGroup(pHier, processingGroupIndex);
	}

	U32 GetFirstJointInSegment(const ndanim::JointHierarchy* pHier, U32 segmentIndex)
	{
		return OrbisAnim::AnimHierarchy_GetFirstJointInSegment(pHier, segmentIndex);
	}

	U32 GetFirstAnimatedJointInSegment(const ndanim::JointHierarchy* pHier, U32 segmentIndex)
	{
		return OrbisAnim::AnimHierarchy_GetFirstAnimatedJointInSegment(pHier, segmentIndex);
	}

	U32 GetNumJointsInSegment(const ndanim::JointHierarchy* pHier, U32 segmentIndex)
	{
		return OrbisAnim::AnimHierarchy_GetNumJointsInSegment(pHier, segmentIndex);
	}

	U32 GetNumOutputControlsInSegment(const ndanim::JointHierarchy* pHier, U32 segmentIndex)
	{
		return OrbisAnim::AnimHierarchy_GetNumOutputControlsInSegment(pHier, segmentIndex);
	}

	U32 GetNumAnimatedJointsInSegment(const ndanim::JointHierarchy* pHier, U32 segmentIndex)
	{
		return OrbisAnim::AnimHierarchy_GetNumAnimatedJointsInSegment(pHier, segmentIndex);
	}

	U32 OutputJointIndexToAnimatedJointIndex(const ndanim::JointHierarchy* pHier, U32 outputJointIndex)
	{
		return OrbisAnim::AnimHierarchy_OutputJointIndexToAnimatedJointIndex(pHier, outputJointIndex);
	}
	
	I32F GetSegmentForOutputJoint(const ndanim::JointHierarchy* pHier, U32 iOutputJoint)
	{
		for (U32F iSeg = 0; iSeg < pHier->m_numSegments; ++iSeg)
		{
			const U32F firstJoint = ndanim::GetFirstJointInSegment(pHier, iSeg);
			const U32F numJoints = ndanim::GetNumJointsInSegment(pHier, iSeg);

			if ((iOutputJoint >= firstJoint) && (iOutputJoint < firstJoint+numJoints))
			{
				return iSeg;
			}
		}

		return -1;
	}

	U32 GetNumJointsInGroup(const ndanim::JointHierarchy* pHier, U32 processingGroupIndex)
	{ 
		return OrbisAnim::AnimHierarchy_GetNumAnimatedJointsInGroup(pHier, processingGroupIndex);
	}

// 	U32 GetNumSdkFloatInputCommands(const ndanim::JointHierarchy* pHier)
// 	{
// 		return OrbisAnim::AnimHierarchy_GetNumSdkFloatInputCommands(pHier);
// 	}

	U32 GetTotalSize(const ndanim::ClipData* pClip)
	{ 
		return OrbisAnim::AnimClip_GetTotalSize(pClip);
	}

	U32 GetNumProcessingGroups(const ndanim::ClipData* pClip)
	{ 
		return OrbisAnim::AnimClip_GetNumGroups(pClip);
	}

	const ValidBits* GetValidBitsArray(const ndanim::ClipData* pClip, U32 processingGroupIndex)
	{ 
		return (const ValidBits*)OrbisAnim::AnimClip_GetValidBitsArray(pClip, processingGroupIndex);
	}

	bool IsJointAnimated(const ndanim::JointHierarchy* pHier, U32 jointIndex)
	{
		const U32F numSegments = pHier->m_numSegments;
		for (U32F segIndex = 0; segIndex < numSegments; ++segIndex)
		{
			const OrbisAnim::AnimHierarchySegment* pSegment = OrbisAnim::AnimHierarchy_GetSegment(pHier, segIndex);
			if (jointIndex >= pSegment->m_firstJoint &&
				jointIndex < pSegment->m_firstJoint + pSegment->m_numAnimatedJoints)
			{
				return true;
			}
		}
		return false;
	}

	void InitSnapshotNode(SnapshotNode& node, const JointHierarchy* pAnimHierarchy)
	{
		int numBaseSkelFloatChannels = pAnimHierarchy->m_numFloatChannels;
		int numBaseSkelChannelGroups = pAnimHierarchy->m_numChannelGroups;
		int numBaseSkelTotalJoints = pAnimHierarchy->m_numAnimatedJoints;

		node.m_hierarchyId = pAnimHierarchy->m_animHierarchyId;
		node.m_jointPose.m_pValidBitsTable = NDI_NEW(kAlign16) ndanim::ValidBits[numBaseSkelChannelGroups];
		memset((ndanim::ValidBits*)node.m_jointPose.m_pValidBitsTable, 0, numBaseSkelChannelGroups * sizeof(ndanim::ValidBits));

		node.m_jointPose.m_pJointParams = NDI_NEW(kAlign16) ndanim::JointParams[numBaseSkelTotalJoints];
		memset((ndanim::JointParams*)node.m_jointPose.m_pJointParams, 0, numBaseSkelTotalJoints * sizeof(ndanim::JointParams));

		if (numBaseSkelFloatChannels > 0)
		{
			node.m_jointPose.m_pFloatChannels = NDI_NEW(kAlign16) float[numBaseSkelFloatChannels];
			memset((float*)node.m_jointPose.m_pFloatChannels, 0, numBaseSkelFloatChannels * sizeof(float));
		}
		else
		{
			node.m_jointPose.m_pFloatChannels = nullptr;
		}
	}

	void CopySnapshotNodeData(const SnapshotNode& source, SnapshotNode* pDest, const JointHierarchy* pHier)
	{
		if (!pDest || !pHier)
			return;

		ANIM_ASSERT(source.m_hierarchyId == pDest->m_hierarchyId);

		const U32F numBaseSkelFloatChannels = pHier->m_numFloatChannels;
		const U32F numBaseSkelChannelGroups = pHier->m_numChannelGroups;
		const U32F numBaseSkelTotalJoints = pHier->m_numAnimatedJoints;

		memcpy(pDest->m_jointPose.m_pJointParams, source.m_jointPose.m_pJointParams, sizeof(ndanim::JointParams) * numBaseSkelTotalJoints);
		memcpy(pDest->m_jointPose.m_pValidBitsTable, source.m_jointPose.m_pValidBitsTable, sizeof(ndanim::ValidBits) * numBaseSkelChannelGroups);

		if (source.m_jointPose.m_pFloatChannels && pDest->m_jointPose.m_pFloatChannels)
		{
			memcpy(pDest->m_jointPose.m_pFloatChannels, source.m_jointPose.m_pFloatChannels, sizeof(float) * numBaseSkelFloatChannels);
		}
	}

	I32F GetParentJoint(const JointHierarchy* pHierarchy, I32 iJoint)
	{
		if (iJoint < 0)
			return -1;

		if (iJoint >= pHierarchy->m_numTotalJoints)
			return -1;

		const ndanim::DebugJointParentInfo* pParentInfo = ndanim::GetDebugJointParentInfoTable(pHierarchy);
		return pParentInfo[iJoint].m_parent;
	}

	void SharedTimeIndex::GetOrSet(SharedTimeIndex* pSharedTime, F32* pCurPhase, const char* file, U32F line, const char* func)
	{
		if (pSharedTime)
		{
			AtomicLockJanitor lj(pSharedTime->m_pLock, file, line, func);

			pSharedTime->m_lastUsedFrameNo = EngineComponents::GetNdFrameState()->m_gameFrameNumber;

			if (pSharedTime->m_phase <= -1.0f)
				pSharedTime->m_phase = *pCurPhase; // global phase is invalid: set it
			else
				*pCurPhase = pSharedTime->m_phase; // global phase is valid: use it
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
ndanim::JointParams Lerp(const ndanim::JointParams& lhs, const ndanim::JointParams& rhs, float alpha)
{
	ndanim::JointParams res;
	res.m_trans = Lerp(lhs.m_trans, rhs.m_trans, alpha);
	res.m_quat = Slerp(lhs.m_quat, rhs.m_quat, alpha);
	res.m_scale = Lerp(lhs.m_scale, rhs.m_scale, alpha);
	return res;
}
