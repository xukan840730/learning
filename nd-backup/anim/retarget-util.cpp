/*
 * Copyright (c) 2013 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/retarget-util.h"

#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/bit-array.h"

#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-mgr.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/nd-anim-plugins.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/profiling/profiling.h"

#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"

#include <orbisanim/util.h>

/// --------------------------------------------------------------------------------------------------------------- ///
static bool AnimContainsJointsToRetarget(const ArtItemAnim* pArtItemAnim,
										 const ArtItemSkeleton* pSrcSkel,
										 I32F iOutputSeg,
										 const SkelTable::RetargetEntry* pRetargetEntry)
{
	PROFILE_AUTO(Animation);
	if (!pRetargetEntry->m_dstToSrcSegMapping || iOutputSeg < 0)
		return true;

	const U32F numSegments = pSrcSkel->m_pAnimHierarchy->m_numSegments;
	bool jointValid = false;
	for (I32F iSeg = 0; iSeg < numSegments; ++iSeg)
	{
		if (pRetargetEntry->m_dstToSrcSegMapping[iOutputSeg].IsBitSet(iSeg))
		{
			U32F const numGroups = ndanim::GetNumProcessingGroupsInSegment(pSrcSkel->m_pAnimHierarchy, iSeg);
			U32F const firstGroup = ndanim::GetFirstProcessingGroupInSegment(pSrcSkel->m_pAnimHierarchy, iSeg);
			ANIM_ASSERT(numGroups <= 10);
			for (I32F iProcGroup = firstGroup; iProcGroup < firstGroup + numGroups; ++iProcGroup)
			{				
				const OrbisAnim::ValidBits* pGroupBits = OrbisAnim::AnimClip_GetValidBitsArray(pArtItemAnim->m_pClipData, iProcGroup);
				//The first group is the joint group.
				ANIM_ASSERT(pGroupBits);
				if (pGroupBits && !pGroupBits->IsEmpty())
				{
					jointValid = true;
					break;
				}
				
			}			
		}
	}
	return jointValid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void PrintRetargetingDebugInfo(const ArtItemAnim* pArtItemAnim)
{
	// Define to '1' for debug output
#if 0
	const DebugSkelInfo* pSkelInfo = pArtItemAnim->m_pDebugSkelInfo;
	const SkelSegmentDetails* pSegDetails = pSkelInfo->m_pSkelSegmentDetails;
	const SkelProcGroupDetails* pProcGroupDetails = pSkelInfo->m_pProcessingGroupDetails;
	const StringId64* pJointIds = pSkelInfo->m_pJointNameIds;

	MsgOut("Skel Information\n");
	MsgOut("    Total Joints:       %u\n", pSkelInfo->m_numTotalJoints);
	MsgOut("    Num Float Channels: %u\n", pSkelInfo->m_numFloatChannels);
	MsgOut("    Num Segments:       %u\n", pSkelInfo->m_numSegments);


	MsgOut("\n");
	MsgOut("Joint Information\n");
	for (I64 i = 0; i < pSkelInfo->m_numTotalJoints + pSkelInfo->m_numFloatChannels; ++i)
	{
		if (i < pSkelInfo->m_numTotalJoints)
		{
			MsgOut("Joint %llu: %s\n", i, DevKitOnly_StringIdToString(pJointIds[i]));
		}
		else
		{
			const I64 floatChannelIndex = i - pSkelInfo->m_numTotalJoints;
			MsgOut("Float Channel %llu: %s\n", floatChannelIndex, DevKitOnly_StringIdToString(pJointIds[i]));
		}
	}

	MsgOut("\n");
	MsgOut("Segment Information\n");
	I64 accProcGroups = 0;
	for (I64 i = 0; i < pSkelInfo->m_numSegments; ++i)
	{
		MsgOut("Seg %llu: \n", i);
		MsgOut("    First Joint:    %u\n", pSegDetails[i].m_firstJoint);
		MsgOut("    Anim Joints:    %u\n", pSegDetails[i].m_numAnimatedJoints);
		MsgOut("    Num Joints:     %u\n", pSegDetails[i].m_numJoints);
		MsgOut("    First Proc Grp: %u\n", (U32)pSegDetails[i].m_firstProcessingGroup);
		MsgOut("    Num Proc Grps:  %u\n", (U32)pSegDetails[i].m_numProcessingGroups);

		for (I64 j = 0; j < pSegDetails[i].m_numProcessingGroups; ++j)
		{
			MsgOut("        Num Channel Grps: %u\n", (U32)pProcGroupDetails[accProcGroups + j].m_numChannelGroups);
			MsgOut("           Num Anim Joints:  %u\n", (U32)pProcGroupDetails[accProcGroups + j].m_numAnimatedJoints);
			MsgOut("           Num Float Chan:   %u\n", (U32)pProcGroupDetails[accProcGroups + j].m_numFloatChannels);
		}

		accProcGroups += pSegDetails[i].m_numProcessingGroups;
	}

	ANIM_ASSERT(accProcGroups == ndanim::GetNumProcessingGroups(pArtItemAnim->m_pClipData));

	MsgOut("\n");
	MsgOut("Animation Information\n");
	accProcGroups = 0;
	I64 accChanGroups = 0;
	I64 accAnimatedJoints = 0;
	I64 accFloatChannels = 0;
	for (I64 iSeg = 0; iSeg < pSkelInfo->m_numSegments; ++iSeg)
	{
		MsgOut("Segment %lld\n", iSeg);
		for (I64 iProcGroup = 0; iProcGroup < pSegDetails[iSeg].m_numProcessingGroups; ++iProcGroup)
		{
			const SkelProcGroupDetails& groupDetails = pProcGroupDetails[accProcGroups + iProcGroup];
			ANIM_ASSERT((groupDetails.m_numChannelGroups == 1 && groupDetails.m_numAnimatedJoints > 0 && groupDetails.m_numFloatChannels == 0) ||
				(groupDetails.m_numChannelGroups == 2 && groupDetails.m_numAnimatedJoints > 0 && groupDetails.m_numFloatChannels > 0));

			const ndanim::ValidBits* pValidBitsArray = ndanim::GetValidBitsArray(pArtItemAnim->m_pClipData, accProcGroups + iProcGroup);
			for (I64 iChan = 0; iChan < groupDetails.m_numChannelGroups; ++iChan)
			{
				const ndanim::ValidBits& validBits = pValidBitsArray[iChan];
				const U64* pU64s = (const U64*)&validBits;
				MsgOut("0x%016llX 0x%016llX\n", pU64s[0], pU64s[1]);

				if (iChan == 0) // Joints
				{
					for (I64 iJoint = 0; iJoint < groupDetails.m_numAnimatedJoints; ++iJoint)
					{
						if (validBits.IsBitSet(iJoint))
						{
							MsgOut("Animated Joint: %lld - %s\n", accAnimatedJoints + iJoint, DevKitOnly_StringIdToString(pJointIds[accAnimatedJoints + iJoint]));
						}
					}
				}
				else // Float Channels
				{
					for (I64 iFloat = 0; iFloat < groupDetails.m_numFloatChannels; ++iFloat)
					{
						if (validBits.IsBitSet(iFloat))
						{
							MsgOut("Float Channel: %lld - %s\n", accFloatChannels + iFloat, DevKitOnly_StringIdToString(pJointIds[pSkelInfo->m_numTotalJoints + accFloatChannels + iFloat]));
						}
					}
				}
			}

			accAnimatedJoints += groupDetails.m_numAnimatedJoints;
			accFloatChannels += groupDetails.m_numFloatChannels;
			accChanGroups += groupDetails.m_numChannelGroups;
		}

		accProcGroups += pSegDetails[iSeg].m_numProcessingGroups;
	}
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void RetargetPoseForSegment(const RetargetInput& input,
							const ArtItemSkeleton* pSrcSkel,
							const ArtItemSkeleton* pTgtSkel,
							const SkelTable::RetargetEntry* pRetargetEntry,
							U32 iOutputSeg,
							ndanim::AnimatedJointPose* pOutPose)
{
	PROFILE_AUTO(Animation);
	PROFILE_ACCUM(RetargetPoseForSegment);

	ANIM_ASSERT(iOutputSeg < pTgtSkel->m_numSegments);
	ANIM_ASSERT(pOutPose);
	ANIM_ASSERT(pOutPose->m_pJointParams);
//	ANIM_ASSERT(pOutPose->m_pFloatChannels);		// This is allowed to be NULL if there are no float channels
	ANIM_ASSERT(pOutPose->m_pValidBitsTable);

	// Ensure that we are actually evaluating the clip on the proper source skeleton. If not, then it needs to be double-retargeted 
	ANIM_ASSERT(!input.m_pArtItemAnim || input.m_pArtItemAnim->m_pClipData->m_animHierarchyId == pSrcSkel->m_hierarchyId);

	// --- Batch commands to retarget the animation ---
	PrintRetargetingDebugInfo(input.m_pArtItemAnim);

	// Allocate the output buffers in whatever allocator we got when coming into this function
 	const U32F tgtNumChannelGroups = ndanim::GetNumChannelGroups(pTgtSkel->m_pAnimHierarchy);
 	const U32F tgtValidBitsSize	   = tgtNumChannelGroups * sizeof(ndanim::ValidBits);

	memset(pOutPose->m_pValidBitsTable, 0, tgtValidBitsSize);

	//If the anim has no joints that will affect the output pose for this segment, don't retarget anything
	if (input.m_pArtItemAnim && !AnimContainsJointsToRetarget(input.m_pArtItemAnim, pSrcSkel, iOutputSeg, pRetargetEntry))
		return;

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_enableValidation))
	{
		const OrbisAnim::AnimHierarchySegment* pSegment = OrbisAnim::AnimHierarchy_GetSegment(pTgtSkel->m_pAnimHierarchy, iOutputSeg);
		const U32 firstAnimatedJointInSeg = OrbisAnim::AnimHierarchy_OutputJointIndexToAnimatedJointIndex(pTgtSkel->m_pAnimHierarchy, pSegment->m_firstJoint);
		memset(pOutPose->m_pJointParams + firstAnimatedJointInSeg, 0xee, sizeof(OrbisAnim::JointParams) * pSegment->m_numAnimatedJoints);

		if (pOutPose->m_pFloatChannels && pSegment->m_numFloatChannels)
			memset(pOutPose->m_pFloatChannels, 0xee, sizeof(float) * pSegment->m_numFloatChannels);
	}

	// Push a new allocator for memory inside this function
	ScopedTempAllocator scopedTempAlloc(FILE_LINE_FUNC);

	void* pPersistentData = NDI_NEW (kAlign16) U8[pSrcSkel->m_persistentDataSize];
	memcpy(pPersistentData, pSrcSkel->m_pInitialPersistentData, pSrcSkel->m_persistentDataSize);

	AnimExecutionContext animExecContext;
	animExecContext.Init(pSrcSkel, nullptr, nullptr, nullptr, nullptr, nullptr, pPersistentData, Memory::TopAllocator());

	// Hook up the plugin funcs
	animExecContext.m_pAnimPhasePluginFunc = EngineComponents::GetAnimMgr()->GetAnimPhasePluginHandler();
	animExecContext.m_pRigPhasePluginFunc = EngineComponents::GetAnimMgr()->GetRigPhasePluginHandler();

	// Retarget all segments up to the highest indexed one we need. Not entirely sure why we need this anymore. Possibly due to IK retargeting.
	const U32F numSrcSkelSegments = pSrcSkel->m_pAnimHierarchy->m_numSegments;

	U32 requiredSegmentMask = 0;
	if (pRetargetEntry->m_dstToSrcSegMapping)
	{
		const ExternalBitArray& bitArray = pRetargetEntry->m_dstToSrcSegMapping[iOutputSeg];
		U64 requiredSegIndex = bitArray.FindFirstSetBit();
		while (requiredSegIndex < numSrcSkelSegments)
		{
			requiredSegmentMask |= (1 << requiredSegIndex);
			requiredSegIndex = bitArray.FindNextSetBit(requiredSegIndex);
		}
	}
	else
	{
		// This case should only happen when we force post retargeting 
		requiredSegmentMask |= (1 << iOutputSeg);
	}

	{
		AnimCmdList* pAnimCmdList = &animExecContext.m_animCmdList;

		pAnimCmdList->AddCmd(AnimCmd::kBeginSegment);

		pAnimCmdList->AddCmd(AnimCmd::kBeginAnimationPhase);

		pAnimCmdList->AddCmd_BeginProcessingGroup();
		if (input.m_pInPose)
		{
			pAnimCmdList->AddCmd_EvaluatePoseDeferred(0, pSrcSkel->m_hierarchyId, input.m_pInPose);
		}
		else
		{
			pAnimCmdList->AddCmd_EvaluateClip(input.m_pArtItemAnim, 0, input.m_sample);
		}
		pAnimCmdList->AddCmd(AnimCmd::kEndProcessingGroup);

		// Merge work buffer allocations for all jointParams groups, floatChannel groups
		pAnimCmdList->AddCmd(AnimCmd::kEndAnimationPhase);

		ANIM_ASSERT(pRetargetEntry);
		ANIM_ASSERT(pRetargetEntry->m_jointRetarget);

		const ndanim::ValidBits* pSrcValidBitsArray = nullptr;
		bool isAdditive = false;
		if (input.m_pArtItemAnim)
		{
			pSrcValidBitsArray = ndanim::GetValidBitsArray(input.m_pArtItemAnim->m_pClipData, 0);
			isAdditive = OrbisAnim::AnimClip_IsAdditive(input.m_pArtItemAnim->m_pClipData);
		}
		else
		{
			pSrcValidBitsArray = input.m_pInPose->m_pValidBitsTable;
			isAdditive = input.m_inPoseIsAdditive;
		}

		// The retargeting plugin will run once for each selected src segment and will only
		// retarget joints to the target segment(iOutputSeg)
		EvaluateRetargetAnimPluginData cmd;
		cmd.m_pRetargetEntry = pRetargetEntry;
		cmd.m_tgtSegmentIndex = iOutputSeg;
		cmd.m_pSrcValidBitsArray = pSrcValidBitsArray;
		cmd.m_jointDataIsAdditive = isAdditive;
		cmd.m_pSrcSkel = pSrcSkel;
		cmd.m_pDestSkel = pTgtSkel;
		cmd.m_pOutPose = pOutPose;
		pAnimCmdList->AddCmd_EvaluateRigPhasePlugin(SID("retarget-anim"), &cmd);
	
		pAnimCmdList->AddCmd(AnimCmd::kEndSegment);
	}

	ProcessRequiredSegments(requiredSegmentMask, 0, &animExecContext, nullptr);
}


/// --------------------------------------------------------------------------------------------------------------- ///
static const OrbisAnim::AnimHierarchySegment* GetSegmentFromGlobalProcessingGroupIndex(const ndanim::JointHierarchy* pAnimHierarchy,
																					   const int processingGroupIndex)
{
	const U8 numSegments = pAnimHierarchy->m_numSegments;
	for (U32 segmentIndex = 0; segmentIndex < numSegments; ++segmentIndex)
	{
		const OrbisAnim::AnimHierarchySegment* pSegment = OrbisAnim::AnimHierarchy_GetSegment(pAnimHierarchy, segmentIndex);
		if (processingGroupIndex >= pSegment->m_firstProcessingGroup && processingGroupIndex < pSegment->m_firstProcessingGroup + pSegment->m_numProcessingGroups)
		{
			return pSegment;
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ndanim::ValidBits GetRetargetedValidBits(const ArtItemSkeleton* pSkel,
										 const ArtItemAnim* pArtItemAnim,
										 U32 iProcessingGroup)
{
	PROFILE_ACCUM(GetRetargetedValidBits);
	PROFILE_AUTO(Animation);

	if (!pArtItemAnim || !pSkel)
	{
		return ndanim::ValidBits(0ULL, 0ULL);
	}

	if (pSkel->m_pAnimHierarchy->m_animHierarchyId == pArtItemAnim->m_pClipData->m_animHierarchyId)
	{
		if (iProcessingGroup >= ndanim::GetNumProcessingGroups(pArtItemAnim->m_pClipData))
		{
			return ndanim::ValidBits(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL);
		}

		const ndanim::ValidBits* pValidBits = ndanim::GetValidBitsArray(pArtItemAnim->m_pClipData, iProcessingGroup);
		ANIM_ASSERT(pValidBits);
		return *pValidBits;
	}
	else
	{
		if (!g_animOptions.m_enableAnimRetargeting)
		{
			return ndanim::ValidBits(0ULL, 0ULL);
		}

		SkeletonId srcSkelId = pArtItemAnim->m_skelID;
		const ArtItemSkeleton* pSrcSkel = ResourceTable::LookupSkel(srcSkelId).ToArtItem();
		const SkelTable::RetargetEntry* pRetargetEntry = SkelTable::LookupRetarget(srcSkelId, pSkel->m_skelId);

		//This is out of date. We have no data so assume the valid bits of the out of date skel and current skel match?
		if (!pRetargetEntry && srcSkelId == pSkel->m_skelId)
		{
			if (iProcessingGroup >= ndanim::GetNumProcessingGroups(pArtItemAnim->m_pClipData))
			{
				return ndanim::ValidBits(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL);
			}

			const ndanim::ValidBits* pValidBits = ndanim::GetValidBitsArray(pArtItemAnim->m_pClipData, iProcessingGroup);
			ANIM_ASSERT(pValidBits);
			return *pValidBits;
		}

		if (!pRetargetEntry)
		{
			return ndanim::ValidBits(0ULL, 0ULL);
		}

		if (pRetargetEntry->m_disabled)
		{
			return ndanim::ValidBits(0ULL, 0ULL);
		}

		const ndanim::ValidBits* paOrigValidBits = ndanim::GetValidBitsArray(pArtItemAnim->m_pClipData, 0);

		const OrbisAnim::AnimHierarchySegment* pDestSgment = GetSegmentFromGlobalProcessingGroupIndex(pSkel->m_pAnimHierarchy,
																									  iProcessingGroup);
		ANIM_ASSERT(pDestSgment);

		ndanim::ValidBits resultBits;
		resultBits.Clear();

		for (int iRetarget = 0; iRetarget < pRetargetEntry->m_count; iRetarget++)
		{
			const SkelTable::JointRetarget& jointRetarget = pRetargetEntry->m_jointRetarget[iRetarget];

			if (jointRetarget.m_dstChannelGroup != pDestSgment->m_firstChannelGroup)
				continue;

			if (jointRetarget.m_srcChannelGroup < 0)
				continue;

			ANIM_ASSERT(jointRetarget.m_srcValidBit < 128);

			const bool srcBitSet = paOrigValidBits ? paOrigValidBits[jointRetarget.m_srcChannelGroup]
														 .IsBitSet(jointRetarget.m_srcValidBit)
												   : true;

			if (srcBitSet)
			{
				resultBits.SetBit(jointRetarget.m_dstValidBit);
			}
		}

		return resultBits;
	}
}
