/*
 * Copyright (c) 2013 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/plugin/retarget-shared.plugin.h"

#include "corelib/containers/list-array.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/armik.h"
#include "ndlib/anim/footik.h"
#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/profiling/profile-cpu-categories.h"
#include "ndlib/profiling/profile-cpu.h"
#include "ndlib/scriptx/h/skel-retarget-defines.h"

#include <orbisanim/util.h>

/// --------------------------------------------------------------------------------------------------------------- ///
static void EvaluateLinearRegression(float (&xArray)[3], float (&yArray)[3], const DC::RetargetMatrix* pDcMatrix)
{
	typedef Eigen::Matrix<float, 3, 3, Eigen::RowMajor> EMatrixfNN;
	typedef Eigen::Matrix<float, 3, 1> EVectorfN;
	typedef Eigen::Matrix<float, 4, 1> EVectorf4;
	typedef Eigen::Matrix<float, Eigen::Dynamic, 3, Eigen::RowMajor> EMatrixfMN;
	typedef Eigen::Matrix<float, Eigen::Dynamic, 1> EVectorfM;
	/*
	typedef Eigen::Matrix<double, 3, 3, Eigen::RowMajor> EMatrixdNN;
	typedef Eigen::Matrix<double, 3, 1> EVectordN;
	typedef Eigen::Matrix<double, 4, 1> EVectord4;
	typedef Eigen::Matrix<double, Eigen::Dynamic, 3, Eigen::RowMajor> EMatrixdMN;
	typedef Eigen::Matrix<double, Eigen::Dynamic, 1> EVectordM;
	*/
	typedef Eigen::Map<const EMatrixfNN> MapfNNConst;
	typedef Eigen::Map<const EVectorfN> MapfVNConst;
	typedef Eigen::Map<const EMatrixfMN> MapfMNConst;
	typedef Eigen::Map<EVectorfN> MapfVN;
	/*
	typedef Eigen::Map<const EMatrixdNN> MapdNNConst;
	typedef Eigen::Map<const EVectordN> MapdVNConst;
	typedef Eigen::Map<const EMatrixdMN> MapdMNConst;
	typedef Eigen::Map<EVectordN> MapdVN;
	*/
	if (pDcMatrix == nullptr || pDcMatrix->m_elements == nullptr)
	{
		yArray[0] = xArray[0];
		yArray[1] = xArray[1];
		yArray[2] = xArray[2];
		return;
	}
	MapfNNConst W(pDcMatrix->m_elements, 3, 3);
	MapfVNConst x(xArray);
	
	MapfVN y(yArray, 3);
	y = x.transpose()*W;	
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ResolveJointIndexIntoValidBit(const ndanim::JointHierarchy* pAnimHierarchy,
								   U32F numProcGroups,
								   const ndanim::ProcessingGroup* pProcGroups,
								   U32F jointIdx,
								   U32* pOutChannelGroupIdx,
								   U32* pOutBitIdx)
{
	const U8 numSegments = pAnimHierarchy->m_numSegments;
	for (U32 segmentIndex = 0; segmentIndex < numSegments; ++segmentIndex)
	{
		const OrbisAnim::AnimHierarchySegment* pSegment = OrbisAnim::AnimHierarchy_GetSegment(pAnimHierarchy, segmentIndex);

		const U32 firstJointInSegment = pSegment->m_firstJoint;
		const U32 firstProcessingGroup = pSegment->m_firstProcessingGroup;
		const U32 firstChannelGroup = pSegment->m_firstChannelGroup;
		const U32 numProcGroupsInSegment = pSegment->m_numProcessingGroups;

		U32 globalChannelGroupIndex = firstChannelGroup;
		U32 globalJointIndex = firstJointInSegment;
		for (U32F ii = 0; ii < numProcGroupsInSegment; ++ii)
		{
			const ndanim::ProcessingGroup& processingGroup = pProcGroups[firstProcessingGroup + ii];
			if (jointIdx >= globalJointIndex &&
				jointIdx < globalJointIndex + processingGroup.m_numAnimatedJoints)
			{
				*pOutChannelGroupIdx = globalChannelGroupIndex;
				*pOutBitIdx = jointIdx - globalJointIndex;
				return true;
			}

			globalChannelGroupIndex += processingGroup.m_numChannelGroups;
			globalJointIndex += processingGroup.m_numAnimatedJoints;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ResolveFloatChannelIndexIntoValidBit(const ndanim::JointHierarchy* pAnimHierarchy,
										  U32F numProcGroups,
										  const ndanim::ProcessingGroup* pProcGroups,
										  U32F floatChannelIndex,
										  U32* pOutChannelGroupIdx,
										  U32* pOutBitIdx)
{
	const U8 numSegments = pAnimHierarchy->m_numSegments;
	for (U32 segmentIndex = 0; segmentIndex < numSegments; ++segmentIndex)
	{
		const OrbisAnim::AnimHierarchySegment* pSegment = OrbisAnim::AnimHierarchy_GetSegment(pAnimHierarchy, segmentIndex);

		const U32 firstProcessingGroup = pSegment->m_firstProcessingGroup;
		const U32 firstChannelGroup = pSegment->m_firstChannelGroup;
		const U32 numProcGroupsInSegment = pSegment->m_numProcessingGroups;

		for (U32F ii = 0; ii < numProcGroupsInSegment; ++ii)
		{
			const ndanim::ProcessingGroup& processingGroup = pProcGroups[firstProcessingGroup + ii];
			if (floatChannelIndex >= processingGroup.m_firstFloatChannel &&
				floatChannelIndex < processingGroup.m_firstFloatChannel + processingGroup.m_numAnimatedJoints)
			{
				*pOutChannelGroupIdx = firstChannelGroup + processingGroup.m_firstChannelGroup + 1;
				*pOutBitIdx = floatChannelIndex - processingGroup.m_firstFloatChannel;
				return true;
			}
		}

	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool DoJointRetarget(const SkelTable::JointRetarget& jointRetarget,
					 const ndanim::JointParams& srcJointLs,
					 const ndanim::JointParams& srcDefJointLs,
					 const ndanim::JointParams& tgtDefJointLs,
					 bool jointDataIsAdditive,
					 ndanim::JointParams* pRetargetJointOut)
{
	ANIM_ASSERT(IsFinite(srcJointLs.m_trans));
	ANIM_ASSERT(IsFinite(srcJointLs.m_quat));
	ANIM_ASSERT(IsFinite(srcJointLs.m_scale));
	// temp comment out cause it's causing crashes
	//ANIM_ASSERT(IsNormal(srcJointLs.m_quat));

	ANIM_ASSERT(pRetargetJointOut);
	if (!pRetargetJointOut)
		return false;

	const Locator srcJointLoc = Locator(srcJointLs.m_trans, srcJointLs.m_quat);

	const Locator parentBindPoseDeltaLoc = jointRetarget.m_parentBindPoseDelta.ToLocator();
	const Locator jointBindPoseDeltaLoc	 = jointRetarget.m_jointBindPoseDelta.ToLocator();
	const float boneScale = MakeF32(jointRetarget.m_boneScale);

	Quat retargetedRot	 = Quat(kIdentity);
	Vector retargetScale = Vector(1.0f, 1.0f, 1.0f);
	Point retargetedPos	 = Point(kOrigin);

	if (jointRetarget.m_retargetMode == DC::kBoneRetargetModeDefault)
	{
		// Set scale
		retargetScale = srcJointLs.m_scale;

		if (jointDataIsAdditive)
		{
			// Counter rotate the translation and then apply the delta rotation to the joint rotation
			const Quat parentBindPoseDeltaRot(parentBindPoseDeltaLoc.GetRotation());
			retargetedPos = UnrotatePoint(parentBindPoseDeltaRot, srcJointLs.m_trans);

			const Locator retargetedLoc1 = srcJointLoc.TransformLocator(jointBindPoseDeltaLoc);
			const Locator retargetedLoc2 = jointBindPoseDeltaLoc.UntransformLocator(retargetedLoc1);
			retargetedRot = retargetedLoc2.GetRotation();
		}
		else
		{
			// Counter rotate the translation and then apply the delta rotation to the joint rotation
			const Quat parentBindPoseDeltaRot(parentBindPoseDeltaLoc.GetRotation());
			retargetedPos = UnrotatePoint(parentBindPoseDeltaRot, srcJointLs.m_trans);

			const Locator retargetedLoc1 = parentBindPoseDeltaLoc.UntransformLocator(srcJointLoc);
			const Locator retargetedLoc2 = retargetedLoc1.TransformLocator(jointBindPoseDeltaLoc);
			retargetedRot = retargetedLoc2.GetRotation();
		}

		// Now apply our gnarly bone scale
		const Point boneLengthCompensatedPoint = (kOrigin + (retargetedPos - kOrigin) * boneScale);
		retargetedPos = boneLengthCompensatedPoint;
	}
	else if (jointRetarget.m_retargetMode == DC::kBoneRetargetModeRotationOnly)
	{
		if (jointDataIsAdditive)
		{
			retargetScale = Vector(1.0f, 1.0f, 1.0f);
			retargetedPos = Point(kOrigin);

			const Locator retargetedLoc1 = srcJointLoc.TransformLocator(jointBindPoseDeltaLoc);
			const Locator retargetedLoc2 = jointBindPoseDeltaLoc.UntransformLocator(retargetedLoc1);
			retargetedRot = retargetedLoc2.GetRotation();
		}
		else
		{
			retargetScale = tgtDefJointLs.m_scale;
			retargetedPos = tgtDefJointLs.m_trans;

			const Locator retargetedLoc1 = parentBindPoseDeltaLoc.UntransformLocator(srcJointLoc);
			const Locator retargetedLoc2 = retargetedLoc1.TransformLocator(jointBindPoseDeltaLoc);
			retargetedRot = retargetedLoc2.GetRotation();
		}
	}
	else if (jointRetarget.m_retargetMode == DC::kBoneRetargetModeAddRotationComponentOnly)
	{
		if (IsFinite(pRetargetJointOut->m_scale)
			&& IsFinite(pRetargetJointOut->m_trans)
			&& IsFinite(pRetargetJointOut->m_quat))
		{
			retargetScale = pRetargetJointOut->m_scale;
			retargetedPos = pRetargetJointOut->m_trans;
			retargetedRot = pRetargetJointOut->m_quat * srcJointLs.m_quat;
		}
		else
		{
			retargetScale = Vector(1.0f, 1.0f, 1.0f);
			retargetedPos = Point(kOrigin);
			retargetedRot = Quat(kIdentity);
		}
	}
	else if (jointRetarget.m_retargetMode == DC::kBoneRetargetModeRotationAndDeltaTranslation)
	{
		if (jointDataIsAdditive)
		{
			retargetScale = Vector(1.0f, 1.0f, 1.0f);
			retargetedPos = Point(kOrigin);

			const Locator retargetedLoc1 = srcJointLoc.TransformLocator(jointBindPoseDeltaLoc);
			const Locator retargetedLoc2 = jointBindPoseDeltaLoc.UntransformLocator(retargetedLoc1);
			retargetedRot = retargetedLoc2.GetRotation();
		}
		else
		{
			retargetScale = tgtDefJointLs.m_scale;

			const Vector deltaTrans = srcJointLs.m_trans - jointRetarget.m_srcJointPosLs.ToPoint();
			retargetedPos = tgtDefJointLs.m_trans + deltaTrans;

			const Locator retargetedLoc1 = parentBindPoseDeltaLoc.UntransformLocator(srcJointLoc);
			const Locator retargetedLoc2 = retargetedLoc1.TransformLocator(jointBindPoseDeltaLoc);
			retargetedRot = retargetedLoc2.GetRotation();
		}
	}
	else if (jointRetarget.m_retargetMode == DC::kBoneRetargetModeVerbatim)
	{
		retargetScale = srcJointLs.m_scale;
		retargetedPos = srcJointLs.m_trans;
		retargetedRot = srcJointLs.m_quat;
	}
	else if (jointRetarget.m_retargetMode == DC::kBoneRetargetModeRegression)
	{
		PROFILE(Animation, RBF);
		retargetScale = srcJointLs.m_scale;
		retargetedPos = srcJointLs.m_trans;
		retargetedRot = srcJointLs.m_quat;

		Locator additiveLoc;
		if (jointDataIsAdditive)
		{
			additiveLoc.SetTranslation(srcJointLs.m_trans);
			additiveLoc.SetRot(srcJointLs.m_quat);			
		}
		else
		{
			additiveLoc = Locator(srcDefJointLs.m_trans, srcDefJointLs.m_quat).UntransformLocator(Locator(srcJointLs.m_trans, srcJointLs.m_quat));
		}

		{			
			Scalar signRotW = Sign(additiveLoc.Rot().W());
			
			if (const DC::RetargetRegression* pRetargetPair = jointRetarget.m_pRegression)
			{				
				bool enableLinearRegression = true;
				if (pRetargetPair->m_translationMatrix && enableLinearRegression)
				{
					float x[] = { additiveLoc.GetTranslation().X(),
								  additiveLoc.GetTranslation().Y(),
								  additiveLoc.GetTranslation().Z() };
					float y[3];
					EvaluateLinearRegression(x, y, pRetargetPair->m_translationMatrix);
					retargetedPos = Point(y[0], y[1], y[2]);
				}
				else
				{
					retargetedPos = additiveLoc.GetTranslation();
				}

				if (pRetargetPair->m_rotationMatrix && enableLinearRegression)
				{
					float x[] = { additiveLoc.Rot().X() / signRotW, additiveLoc.Rot().Y() / signRotW, additiveLoc.Rot().Z() / signRotW };
					float y[3];															
					EvaluateLinearRegression(x, y, pRetargetPair->m_rotationMatrix);

					Vector quatAxis(y[0], y[1], y[2]);
					Scalar length = Length(quatAxis);
					if (length > 1.0f)
					{
						quatAxis /= length;
						retargetedRot = Quat(quatAxis.X() * signRotW,
											 quatAxis.Y() * signRotW,
											 quatAxis.Z() * signRotW,
											 kZero);
					}
					else
					{
						retargetedRot = Quat(quatAxis.X() * signRotW,
											 quatAxis.Y() * signRotW,
											 quatAxis.Z() * signRotW,
											 Sqrt(SCALAR_LC(1.0f) - Sqr(length)) * signRotW);
					}
				}
				else
				{
					retargetedRot = additiveLoc.GetRotation();
				}				
			}
		}
		if (!jointDataIsAdditive)
		{
			Locator targetJoint = Locator(tgtDefJointLs.m_trans, tgtDefJointLs.m_quat).TransformLocator(Locator(retargetedPos, retargetedRot));
			retargetedPos = targetJoint.GetTranslation();
			retargetedRot = targetJoint.GetRotation();
		}
	}
	else
	{
		return false;
	}

	retargetedRot = Normalize(retargetedRot);

	ANIM_ASSERT(IsReasonable(retargetScale));
	ANIM_ASSERT(IsFinite(retargetedRot));
	ANIM_ASSERT(IsNormal(retargetedRot));
	ANIM_ASSERT(IsReasonable(retargetedPos));

	pRetargetJointOut->m_scale = retargetScale;
	pRetargetJointOut->m_quat  = retargetedRot;
	pRetargetJointOut->m_trans = retargetedPos;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DoRetargetArmIk(ArmIndex arm,
					 const RetargetWorkInfo& workInfo,
					 const ndanim::JointParams* pSrcSegJointParams,
					 const U32F totalNumSrcJoints,
					 const ndanim::ValidBits* pSrcValidBitsTable,
					 U32F srcChanGroup,
					 const ndanim::AnimatedJointPose* pTgtJointPose,
					 const U32F totalNumTgtJoints);

/// --------------------------------------------------------------------------------------------------------------- ///
void DoRetargetLegIk(LegIndex leg,
					 const RetargetWorkInfo& workInfo,
					 const ndanim::JointParams* pSrcSegJointParams,
					 const U32F totalNumSrcJoints,
					 const ndanim::ValidBits* pSrcValidBitsTable,
					 U32F srcChanGroup,
					 const ndanim::AnimatedJointPose* pTgtJointPose,
					 const U32F totalNumTgtJoints);

/// --------------------------------------------------------------------------------------------------------------- ///
void RetargetJointsInSegment(const RetargetWorkInfo& workInfo)
{
	// Local copies
	const F32							rootScale = workInfo.m_rootScale;
	const bool							jointDataIsAdditive = workInfo.m_jointDataIsAdditive;
	const ndanim::JointHierarchy*		pSrcHier = workInfo.m_pSrcHier;
	const ndanim::JointHierarchy*		pTgtHier = workInfo.m_pTgtHier;
	const SkelTable::RetargetEntry*		pRetargetEntry = workInfo.m_pRetargetEntry;
	const ndanim::JointParams*			pSrcSegJoints = workInfo.m_pSrcSegJointParams;
	const float*						pSrcSegFloatChannels = workInfo.m_pSrcSegFloatChannels;
	const ndanim::ValidBits*			pSrcValidBitsTable = workInfo.m_pSrcValidBitsTable;
	ndanim::JointParams*				pTgtJoints = workInfo.m_outputPose.m_pJointParams;
	float*								pTgtFloatChannels = workInfo.m_outputPose.m_pFloatChannels;
	ndanim::ValidBits*					pTgtValidBitsTable = workInfo.m_outputPose.m_pValidBitsTable;
	ANIM_ASSERT(pTgtValidBitsTable);


	ANIM_ASSERT(pRetargetEntry);
	const U16 numJointsToRetarget = pRetargetEntry->m_count;

	const U32 tgtSegmentIndex = workInfo.m_tgtSegIndex;
	const U32 srcSegmentIndex = workInfo.m_srcSegIndex;

	// Extracted hierarchy attributes
	const U32 srcNumChannelGroups = ndanim::GetNumChannelGroups(pSrcHier);
	const U32 tgtNumChannelGroups = ndanim::GetNumChannelGroups(pTgtHier);;
	const U32 srcNumProcessingGroups = ndanim::GetNumProcessingGroups(pSrcHier);
	const U32 tgtNumProcessingGroups = ndanim::GetNumProcessingGroups(pTgtHier);
	const U32 firstTgtJointInSegment = ndanim::GetFirstJointInSegment(pTgtHier, tgtSegmentIndex);
	const U32 firstSrcJointInSegment = ndanim::GetFirstJointInSegment(pSrcHier, srcSegmentIndex);
	const U32 numAnimatedTgtJointsInSegment = ndanim::GetNumAnimatedJointsInSegment(pTgtHier, tgtSegmentIndex);
	const U32 numAnimatedSrcJointsInSegment = ndanim::GetNumAnimatedJointsInSegment(pSrcHier, srcSegmentIndex);
	const ndanim::ProcessingGroup* pTgtProcGroups = ndanim::GetProcessingGroupTable(pTgtHier);
	const ndanim::ProcessingGroup* pSrcProcGroups = ndanim::GetProcessingGroupTable(pSrcHier);
	const ndanim::JointParams* pTgtDefJointsLs = ndanim::GetDefaultJointPoseTable(pTgtHier);
	const ndanim::JointParams* pSrcDefJointsLs = ndanim::GetDefaultJointPoseTable(pSrcHier);

	// Only clear the destination when processing the first segment
	if (tgtSegmentIndex == 0 && srcSegmentIndex == 0)
	{
		if (FALSE_IN_FINAL_BUILD(g_animOptions.m_enableValidation))
		{
			const U32F numTgtJointsInSeg = numAnimatedTgtJointsInSegment - firstTgtJointInSegment;
			memset(pTgtJoints + firstTgtJointInSegment, 0x88, sizeof(ndanim::JointParams) * numTgtJointsInSeg);

			if (pTgtFloatChannels && pTgtHier->m_numFloatChannels)
				memset(pTgtFloatChannels, 0x99, sizeof(float) * pTgtHier->m_numFloatChannels);
		}

		if (pSrcValidBitsTable)
		{
			for (U32F ii = 0; ii < tgtNumChannelGroups; ++ii)
			{
				workInfo.m_outputPose.m_pValidBitsTable[ii].Clear();
			}
		}
	}

	if (!g_animOptions.m_forcePostRetargeting &&
		pRetargetEntry->m_srcHierarchyId == pRetargetEntry->m_destHierarchyId)
	{
		if (srcSegmentIndex != tgtSegmentIndex)
			return;

		ANIM_ASSERT(tgtNumChannelGroups == srcNumChannelGroups);

		if (pSrcValidBitsTable)
		{
			for (U32F ii = 0; ii < tgtNumChannelGroups; ++ii)
			{
				pTgtValidBitsTable[ii] = pSrcValidBitsTable[ii];
			}
		}
		else
		{
			const OrbisAnim::AnimHierarchySegment* pSegment = OrbisAnim::AnimHierarchy_GetSegment(pTgtHier, tgtSegmentIndex);
			const ndanim::ProcessingGroup* pTgtProcessingGroups = ndanim::GetProcessingGroupTable(pTgtHier);
			const U16 firstProcGroupInSeg = pSegment->m_firstProcessingGroup;
			const U16 numProcGroupsInSeg = pSegment->m_numProcessingGroups;
			U16 channelGroupOffset = 0;
			for (U16 iProcGrp = 0; iProcGrp < numProcGroupsInSeg; ++iProcGrp)
			{
				const ndanim::ProcessingGroup* pProcessingGroup = &pTgtProcessingGroups[iProcGrp];
				const size_t numAnimatedJoints = pProcessingGroup->m_numAnimatedJoints;
				const U32F channelGroupIndex = pSegment->m_firstChannelGroup + pProcessingGroup->m_firstChannelGroup;
				for (U32F iJoint = 0; iJoint < numAnimatedJoints; ++iJoint)
				{
					pTgtValidBitsTable[channelGroupIndex].SetBit(iJoint);
				}
			}
		}

		if (pSrcSegJoints && pTgtJoints && numAnimatedTgtJointsInSegment > 0)
		{
			ANIM_ASSERT(numAnimatedSrcJointsInSegment == numAnimatedTgtJointsInSegment);
			memcpy(pTgtJoints + firstTgtJointInSegment, pSrcSegJoints, sizeof(ndanim::JointParams) * numAnimatedTgtJointsInSegment);
		}

		if (pSrcSegFloatChannels && pTgtFloatChannels && pTgtHier->m_numFloatChannels > 0)
		{
			ANIM_ASSERT(pSrcHier->m_numFloatChannels == pTgtHier->m_numFloatChannels);
			memcpy(pTgtFloatChannels, pSrcSegFloatChannels, sizeof(float) * pTgtHier->m_numFloatChannels);
		}

		return;
	}

	// Copy valid source joints to mapped target joints
	ANIM_ASSERT(pRetargetEntry->m_jointRetarget);

	for (U32F ii = 0; ii < numJointsToRetarget; ++ii)
	{
		const SkelTable::JointRetarget& jointRetarget = pRetargetEntry->m_jointRetarget[ii];

		const U32F srcJointIdx = jointRetarget.m_srcIndex;
		const U32F tgtJointIdx = jointRetarget.m_destIndex;

		// Make sure that the source joint is available in this segment.
		if (srcJointIdx < firstSrcJointInSegment || srcJointIdx >= (firstSrcJointInSegment + numAnimatedSrcJointsInSegment))
			continue;

		if (tgtJointIdx < firstTgtJointInSegment || tgtJointIdx >= (firstTgtJointInSegment + numAnimatedTgtJointsInSegment))
			continue;

		U32 srcChanGroup = 0;
		U32 srcBit = 0;
		const bool foundSrc = ResolveJointIndexIntoValidBit(pSrcHier,
															srcNumProcessingGroups,
															pSrcProcGroups,
															srcJointIdx,
															&srcChanGroup,
															&srcBit);
		ANIM_ASSERT(foundSrc);

		if (!pSrcValidBitsTable || pSrcValidBitsTable[srcChanGroup].IsBitSet(srcBit))
		{
			U32 tgtChanGroup = 0;
			U32 tgtBit = 0;
			const bool foundTgt = ResolveJointIndexIntoValidBit(pTgtHier,
																tgtNumProcessingGroups,
																pTgtProcGroups,
																tgtJointIdx,
																&tgtChanGroup,
																&tgtBit);
			ANIM_ASSERT(foundTgt);

			workInfo.m_outputPose.m_pValidBitsTable[tgtChanGroup].SetBit(tgtBit);

			const ndanim::JointParams& srcJoint = pSrcSegJoints[srcJointIdx - firstSrcJointInSegment];

			// Convert the global joint index into a packed animated joint pose index
			const U32 animPoseJointIndex = ndanim::OutputJointIndexToAnimatedJointIndex(pTgtHier, tgtJointIdx);
			ndanim::JointParams* pTgtJoint = pTgtJoints + animPoseJointIndex;

			ANIM_ASSERT(animPoseJointIndex == jointRetarget.m_destAnimIndex);
			const ndanim::JointParams& tgtDefJoint = pTgtDefJointsLs[jointRetarget.m_destAnimIndex];
			const ndanim::JointParams& srcDefJoint = pSrcDefJointsLs[jointRetarget.m_srcAnimIndex];

			if (DoJointRetarget(jointRetarget, srcJoint, srcDefJoint, tgtDefJoint, jointDataIsAdditive, pTgtJoint))
			{
				if (FALSE_IN_FINAL_BUILD(g_animOptions.m_enableValidation))
				{
					ANIM_ASSERT(IsReasonable(pTgtJoint->m_trans));
					ANIM_ASSERT(IsFinite(pTgtJoint->m_quat));
					ANIM_ASSERT(IsNormal(pTgtJoint->m_quat));
					ANIM_ASSERT(IsReasonable(pTgtJoint->m_scale));
				}
			}
			else
			{
				ANIM_ASSERT(false);
			}
		}
	}

	// The root joint scales during animation so set the relative length instead of preserving the target bone length
	if ((firstTgtJointInSegment == 0) && (firstSrcJointInSegment == 0))
	{
		U32 srcChanGroup = 0;
		U32 srcBit = 0;
		const bool foundSrc = ResolveJointIndexIntoValidBit(pSrcHier,
															srcNumProcessingGroups,
															pSrcProcGroups,
															0,
															&srcChanGroup,
															&srcBit);

		if (foundSrc && (!pSrcValidBitsTable || pSrcValidBitsTable[srcChanGroup].IsBitSet(srcBit)))
		{
			const Point& kSrcTrans = pSrcSegJoints[0].m_trans;

			pTgtJoints[0].m_trans = Point(kSrcTrans.X() * rootScale,
										  kSrcTrans.Y() * rootScale,
										  kSrcTrans.Z() * rootScale);

			ANIM_ASSERT(IsFinite(kSrcTrans));
			ANIM_ASSERT(IsFinite(rootScale));
			ANIM_ASSERT(IsFinite(pTgtJoints[0].m_trans));

			if (!jointDataIsAdditive && workInfo.m_pRetargetEntry
				&& workInfo.m_pRetargetEntry->m_pSkelRetarget)
			{
				if (workInfo.m_pRetargetEntry->m_pArmData && workInfo.m_pRetargetEntry->m_pSkelRetarget->m_armIk)
				{
					PROFILE(Animation, RetargetArmIk);
					DoRetargetArmIk(kLeftArm,
									workInfo,
									pSrcSegJoints,
									numAnimatedSrcJointsInSegment,
									pSrcValidBitsTable,
									srcChanGroup,
									&workInfo.m_outputPose,
									numAnimatedTgtJointsInSegment);
					DoRetargetArmIk(kRightArm,
									workInfo,
									pSrcSegJoints,
									numAnimatedSrcJointsInSegment,
									pSrcValidBitsTable,
									srcChanGroup,
									&workInfo.m_outputPose,
									numAnimatedTgtJointsInSegment);
				}
				if (workInfo.m_pRetargetEntry->m_pLegData && workInfo.m_pRetargetEntry->m_pSkelRetarget->m_legIk)
				{
					PROFILE(Animation, RetargetLegIk);
					DoRetargetLegIk(kLeftLeg,
									workInfo,
									pSrcSegJoints,
									numAnimatedSrcJointsInSegment,
									pSrcValidBitsTable,
									srcChanGroup,
									&workInfo.m_outputPose,
									numAnimatedTgtJointsInSegment);
					DoRetargetLegIk(kRightLeg,
									workInfo,
									pSrcSegJoints,
									numAnimatedSrcJointsInSegment,
									pSrcValidBitsTable,
									srcChanGroup,
									&workInfo.m_outputPose,
									numAnimatedTgtJointsInSegment);
				}
			}
		}
	}

	// Retarget float channels
	if (pSrcSegFloatChannels && pTgtFloatChannels)
	{
		for (int iFloatChannel = 0; iFloatChannel < pRetargetEntry->m_floatRetargetList.Size(); iFloatChannel++)
		{
			const SkelTable::FloatChannelRetarget& floatChannelRetarget = pRetargetEntry->m_floatRetargetList[iFloatChannel];
			U32 srcChanGroup = 0;
			U32 srcBit = 0;
			const bool foundSrc = ResolveFloatChannelIndexIntoValidBit(pSrcHier, srcNumProcessingGroups, pSrcProcGroups, floatChannelRetarget.m_srcIndex, &srcChanGroup, &srcBit);
			if (foundSrc && (!pSrcValidBitsTable || pSrcValidBitsTable[srcChanGroup].IsBitSet(srcBit)))
			{
				U32 tgtChanGroup = 0;
				U32 tgtBit = 0;
				const bool foundTgt = ResolveFloatChannelIndexIntoValidBit(pTgtHier, tgtNumProcessingGroups, pTgtProcGroups, floatChannelRetarget.m_destIndex, &tgtChanGroup, &tgtBit);
				if (foundTgt)
				{
					pTgtFloatChannels[floatChannelRetarget.m_destIndex] = pSrcSegFloatChannels[floatChannelRetarget.m_srcIndex];
					ANIM_ASSERT(IsReasonable(pTgtFloatChannels[floatChannelRetarget.m_destIndex]));
					workInfo.m_outputPose.m_pValidBitsTable[tgtChanGroup].SetBit(tgtBit);
				}
			}
		}
	}

#ifndef FINAL_BUILD
	// Validate all output
	if (g_animOptions.m_enableValidation)
	{
		const U32 firstSegProcGroupIndex = ndanim::GetFirstProcessingGroupInSegment(pTgtHier, tgtSegmentIndex);
		const U32 numSegProcGroups = ndanim::GetNumProcessingGroupsInSegment(pTgtHier, tgtSegmentIndex);
		for (U32 iProcGroup = 0; iProcGroup < numSegProcGroups; ++iProcGroup)
		{
			const ndanim::ProcessingGroup* pProcGroup = pTgtProcGroups + firstSegProcGroupIndex + iProcGroup;

			// We will only validate the joints (channel group 0) at this point
			const U32 channelGroupIndex = pProcGroup->m_firstChannelGroup;
			const ndanim::ValidBits* pValidBits = pTgtValidBitsTable + channelGroupIndex;
			
			for (U32 iJoint = 0; iJoint < pProcGroup->m_numAnimatedJoints; iJoint++)
			{
				if (pValidBits->IsBitSet(iJoint))
				{
					const ndanim::JointParams& params = pTgtJoints[pProcGroup->m_firstAnimatedJoint + iJoint];
					ANIM_ASSERT(IsReasonable(params.m_trans));
					ANIM_ASSERT(IsFinite(params.m_quat));
					ANIM_ASSERT(IsNormal(params.m_quat));
					ANIM_ASSERT(IsReasonable(params.m_scale));
				}
			}
		}
	}
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DoRetargetArmIk(ArmIndex arm,
					 const RetargetWorkInfo& workInfo,
					 const ndanim::JointParams* pSrcSegJointParams,
					 const U32F totalNumSrcJoints,
					 const ndanim::ValidBits* pSrcValidBitsTable,
					 U32F srcChanGroup,
					 const ndanim::AnimatedJointPose* pTgtJointPose,
					 const U32F totalNumTgtJoints)
{
	if (workInfo.m_pRetargetEntry->m_pArmData->m_apSrcIkChains[arm] == nullptr
		|| workInfo.m_pRetargetEntry->m_pArmData->m_apTgtIkChains[arm] == nullptr)
		return;

	ScopedTempAllocator temp(FILE_LINE_FUNC);

	//Try the arm ik
	ArmIkChain& srcChain = *workInfo.m_pRetargetEntry->m_pArmData->m_apSrcIkChains[arm];
	bool srcValid = srcChain.ReadFromJointParams(pSrcSegJointParams, 0, totalNumSrcJoints, 1.0f, reinterpret_cast<const OrbisAnim::ValidBits*>(&pSrcValidBitsTable[srcChanGroup]));

	if (srcValid)
	{
		ArmIkChain& tgtChain = *workInfo.m_pRetargetEntry->m_pArmData->m_apTgtIkChains[arm];
		bool tgtValid = tgtChain.ReadFromJointParams(pTgtJointPose->m_pJointParams, 0, totalNumTgtJoints, 1.0f, reinterpret_cast<const OrbisAnim::ValidBits*>(&pTgtJointPose->m_pValidBitsTable[0]));
		ANIM_ASSERT(tgtValid == srcValid);

		{
			Locator srcWrist = srcChain.GetWristLocWs();
			Locator tgtWrist = tgtChain.GetWristLocWs();
			float ikScale = workInfo.m_pRetargetEntry->m_pSkelRetarget->m_scale;
			Locator srcScaled(AsPoint(AsVector(srcWrist.GetTranslation()) * ikScale), srcWrist.GetRotation() *workInfo.m_pRetargetEntry->m_pArmData->m_aSrcToTargetRotationOffsets[arm]);

			ArmIkInstance ikInst;
			ikInst.m_ikChain = &tgtChain;
			ikInst.m_tt = 1.0f;
			ikInst.m_goalPosWs = srcScaled.GetTranslation();
			SolveArmIk(&ikInst);

			Quat newRot = tgtChain.GetWristLocWs().Rot();
			Quat rotAmount = srcScaled.GetRotation() * Conjugate(newRot);
			tgtChain.RotateWristWs(rotAmount);

//Useful for debugging in the actor viewer
#if 0
			Locator tgtWristPostIk = tgtChain.GetWristLocWs();			
			Locator origin(kIdentity);
			g_prim.Draw(DebugCoordAxes(origin), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCoordAxesLabeled(origin.TransformLocator(srcWrist), "src"), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCoordAxesLabeled(origin.TransformLocator(srcScaled), "src-scaled"), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCoordAxesLabeled(origin.TransformLocator(tgtWrist), "tgt"), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCoordAxesLabeled(origin.TransformLocator(tgtWristPostIk), "tgt-post"), kPrimDuration1FramePauseable);
#endif
			tgtChain.WriteJointParamsBlend(1.0f, pTgtJointPose->m_pJointParams, 0, totalNumTgtJoints);
		}
	}
	
	srcChain.DiscardJointCache();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DoRetargetLegIk(LegIndex leg,
					 const RetargetWorkInfo& workInfo,
					 const ndanim::JointParams* pSrcSegJointParams,
					 const U32F totalNumSrcJoints,
					 const ndanim::ValidBits* pSrcValidBitsTable,
					 U32F srcChanGroup,
					 const ndanim::AnimatedJointPose* pTgtJointPose,
					 const U32F totalNumTgtJoints)
{
	if (workInfo.m_pRetargetEntry->m_pLegData->m_apSrcIkChains[leg] == nullptr
		|| workInfo.m_pRetargetEntry->m_pLegData->m_apTgtIkChains[leg] == nullptr)
		return;
	
	ScopedTempAllocator temp(FILE_LINE_FUNC);

	//Try the arm ik
	LegIkChain& srcChain = *workInfo.m_pRetargetEntry->m_pLegData->m_apSrcIkChains[leg];
	bool srcValid = srcChain.ReadFromJointParams(pSrcSegJointParams, 0, totalNumSrcJoints, 1.0f, reinterpret_cast<const OrbisAnim::ValidBits*>(&pSrcValidBitsTable[srcChanGroup]));

	if (srcValid)
	{
		LegIkChain& tgtChain = *workInfo.m_pRetargetEntry->m_pLegData->m_apTgtIkChains[leg];
		bool tgtValid = tgtChain.ReadFromJointParams(pTgtJointPose->m_pJointParams, 0, totalNumTgtJoints, 1.0f, reinterpret_cast<const OrbisAnim::ValidBits*>(&pTgtJointPose->m_pValidBitsTable[0]));
		ANIM_ASSERT(tgtValid == srcValid);

		{
			Locator srcAnkle = srcChain.GetAnkleLocWs();
			Locator tgtAnkle = tgtChain.GetAnkleLocWs();
			float ikScale = workInfo.m_pRetargetEntry->m_pSkelRetarget->m_scale;
			Locator srcScaled(AsPoint(AsVector(srcAnkle.GetTranslation()) * ikScale), srcAnkle.GetRotation());
			Locator targetLoc = srcScaled.TransformLocator(workInfo.m_pRetargetEntry->m_pLegData->m_aSrcToTargetOffsets[leg]);
			LegIkInstance ikInst;
			ikInst.m_ikChain = &tgtChain;
			ikInst.m_goalPos = targetLoc.GetTranslation();
			SolveLegIk(&ikInst);

			Quat newRot = tgtChain.GetAnkleLocWs().Rot();
			Quat rotAmount = targetLoc.GetRotation() * Conjugate(newRot);
			tgtChain.RotateAnkleWs(rotAmount);
//Useful for debugging in the actor viewer
#if 0
			Locator tgtAnklePostIk = tgtChain.GetAnkleLocWs();
			Locator origin(kIdentity);
			g_prim.Draw(DebugCoordAxes(origin), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCoordAxesLabeled(origin.TransformLocator(srcAnkle), "src"), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCoordAxesLabeled(origin.TransformLocator(srcScaled), "src-scaled"), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCoordAxesLabeled(origin.TransformLocator(targetLoc), "targetLoc"), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCoordAxesLabeled(origin.TransformLocator(tgtAnkle), "tgt"), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCoordAxesLabeled(origin.TransformLocator(tgtAnklePostIk), "tgt-post"), kPrimDuration1FramePauseable);
#endif
			tgtChain.WriteJointParamsBlend(1.0f, pTgtJointPose->m_pJointParams, 0, totalNumTgtJoints);
		}
	}

	srcChain.DiscardJointCache();
}
