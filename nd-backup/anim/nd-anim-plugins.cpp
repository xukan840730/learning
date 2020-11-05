/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/nd-anim-plugins.h"

#include "corelib/util/msg.h"

#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-node-arm-ik.h"
#include "ndlib/anim/anim-node-ik.h"
#include "ndlib/anim/anim-node-joint-spring.h"
#include "ndlib/anim/anim-plugin-context.h"
#include "ndlib/anim/nd-anim-structs.h"
#include "ndlib/anim/bounding-data.h"
#include "ndlib/anim/hand-fix-ik-plugin.h"
#include "ndlib/anim/ik/ik-chain-setup.h"
#include "ndlib/anim/ik/ik-chain.h"
#include "ndlib/anim/ik/ik-setup-table.h"
#include "ndlib/anim/ik/jacobian-ik-plugin.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/joint-modifiers/joint-modifiers.h"
#include "ndlib/anim/leg-fix-ik/leg-fix-ik-plugin.h"
#include "ndlib/anim/plugin/retarget-shared.plugin.h"
#include "ndlib/anim/skel-table.h"

#include "gamelib/level/art-item-skeleton.h"

#include <orbisanim/util.h>
#include <orbisanim/workbuffer.h>


/// --------------------------------------------------------------------------------------------------------------- ///
void EvaluateJointModifierPlugin(OrbisAnim::SegmentContext* pSegmentContext,
							U32 ikType, 
							JointModifierData* pData,
							const Transform* pObjXform,
							JointSet* pJointSet)
{
	JointModifierAnimPluginCallback(pSegmentContext, pData, pObjXform, ikType, pJointSet);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ExecuteRetargetPlugin(OrbisAnim::WorkBuffer* pWorkBuffer,
								  OrbisAnim::SegmentContext* pSegmentContext,
								  const SkelTable::RetargetEntry* pRetargetEntry,
								  const U32 tgtSegIndex,
								  const ndanim::ValidBits* pSrcValidBitsTable,
								  bool jointDataIsAdditive,
								  const ArtItemSkeleton* pSrcSkel,
								  const ArtItemSkeleton* pTgtSkel,
								  ndanim::AnimatedJointPose* pOutTgtPose)
{

	// Get the source default joints
	const ndanim::JointHierarchy* pSrcJointHier = pSrcSkel->m_pAnimHierarchy;
	const ndanim::JointParams* pSrcDefJointTable = ndanim::GetDefaultJointPoseTable(pSrcJointHier);

	// Get the target default joints
	const ndanim::JointHierarchy* pTgtJointHier = pTgtSkel->m_pAnimHierarchy;
	const ndanim::JointParams* pTgtDefJointTable = ndanim::GetDefaultJointPoseTable(pTgtJointHier);

	// Get the target processing groups
	const ndanim::ProcessingGroup* pTgtProcGroupTable = ndanim::GetProcessingGroupTable(pTgtJointHier);

	// Calculate the scale of the translation of the default root joints
	const float targetJointLength = Dist(pTgtDefJointTable[0].m_trans, Point(kZero));
	const float sourceJointLength = pSrcDefJointTable ? (float)Dist(pSrcDefJointTable[0].m_trans, Point(kZero)) : targetJointLength;
	const float rootScale = sourceJointLength > 1e-6 ? targetJointLength / sourceJointLength : 1.0f;



	// Output
	ANIM_ASSERT(pOutTgtPose);
	ANIM_ASSERT(IsPointerAligned(pOutTgtPose->m_pValidBitsTable, kAlign16));
	ANIM_ASSERT(IsPointerAligned(pOutTgtPose->m_pJointParams, kAlign16));
	ANIM_ASSERT(IsPointerAligned(pOutTgtPose->m_pFloatChannels, kAlign16));

	RetargetWorkInfo workInfo;
	workInfo.m_srcSegIndex = pSegmentContext->m_iSegment;
	workInfo.m_tgtSegIndex = tgtSegIndex;

	// Input
	workInfo.m_rootScale = rootScale;
	workInfo.m_jointDataIsAdditive = jointDataIsAdditive;
	workInfo.m_pSrcHier = pSrcJointHier;
	workInfo.m_pTgtHier = pTgtJointHier;

	workInfo.m_pSrcSegJointParams = reinterpret_cast<const ndanim::JointParams*>(pSegmentContext->m_locJointParams);
	workInfo.m_pSrcSegFloatChannels = reinterpret_cast<float*>(pSegmentContext->m_locFloatChannels);
	workInfo.m_pSrcValidBitsTable = pSrcValidBitsTable;

	workInfo.m_outputPose = *pOutTgtPose;
	workInfo.m_pRetargetEntry = pRetargetEntry;

	RetargetJointsInSegment(workInfo);
}

/// --------------------------------------------------------------------------------------------------------------- ///
OrbisAnim::Status LegFixIkBinaryPlugInNodeBatchCallback(OrbisAnim::WorkBuffer& wb,
														void* pWorkBufferMemory,
														OrbisAnim::ProcessingGroupContext* pProcessingGroupContext,
														void const* const* ppLeftChannelGroupLoc,
														void const* const* ppRightChannelGroupLoc,
														void* const* ppOutputChannelGroupLoc,
														OrbisAnim::ValidBits const* pLeftValid,
														OrbisAnim::ValidBits const* pRightValid,
														OrbisAnim::ValidBits* pValidOutput,
														const AnimExecutionContext* pContext,
														const LegFixIkPluginData* pLegFixIkPlugin)
{	
	OrbisAnim::Status eStatus = OrbisAnim::kSuccess;

	const OrbisAnim::ProcessingGroup* pProcessingGroup = pProcessingGroupContext->m_pProcessingGroup;
	const I32F firstAnimatedJoint = pProcessingGroup->m_firstAnimatedJoint;
	const I32F lastAnimatedJoint = firstAnimatedJoint + pProcessingGroup->m_numAnimatedJoints - 1;

	for (U8 legSet = 0; legSet < kNumLegSets; legSet++)
	{
		const LegIkChainSetups* pLegsSetup = g_ikSetupTable.GetLegIkSetups(pContext->m_pSkel->m_skelId,
																		   (LegSet)legSet);

		if (!pLegsSetup)
		{
			if (legSet == kBackLegs)
			{
				ANIM_ASSERTF(pLegsSetup,
							 ("[%s] Cannot find LegIkChain for skel ID 0x%x (%s) -- Did you forget to %s?",
							  DevKitOnly_StringIdToString(pContext->m_pAnimDataHack->m_hProcess.GetUserId()),
							  pContext->m_pSkel->m_skelId.GetValue(),
							  ResourceTable::LookupSkelName(pContext->m_pSkel->m_skelId),
							  pContext->m_pAnimDataHack->m_hProcess.IsKindOf(SID("SimpleNpc"))
								  ? "set \":no-leg-ik-setup #f\" in their simple-npc-config"
								  : "call AddLegIkSetupsForSkeleton()"));

				eStatus = OrbisAnim::kFatalErrorUsage; //Don't have the skeleton information to perform the ik
			}

			continue;
		}

		bool missingJoints = false;

		//Make sure the valid bits have all the joints we need
		for (I32F iLeg = 0; iLeg < 2; ++iLeg)
		{
			const IkChainSetupData& setup = pLegsSetup->m_legSetups[iLeg].m_data;

			for (I32F iJoint = 0; iJoint < setup.m_numIndices; ++iJoint)
			{
				const I32F jointIndex = setup.m_jointIndices[iJoint];

				if ((jointIndex < firstAnimatedJoint) || (jointIndex > lastAnimatedJoint))
				{
					missingJoints = true;
					break;
				}

				const U32F localJointIndex = jointIndex - pProcessingGroup->m_firstAnimatedJoint;

				const bool leftJointValid = pLeftValid->IsBitSet(localJointIndex);
				const bool rightJointValid = pRightValid->IsBitSet(localJointIndex);
				if (!leftJointValid || !rightJointValid)
				{
					missingJoints = true;
					break;
				}
			}

			if (missingJoints)
			{
				break;
			}
		}

		if (missingJoints)
		{
			continue;
		}

		ANIM_ASSERT(*ppLeftChannelGroupLoc == *ppOutputChannelGroupLoc);
		LegFixIkPluginParams params;
		params.m_objXform = *pContext->m_pObjXform;
		params.m_pJointParamsLs = reinterpret_cast<ndanim::JointParams* const>(*ppOutputChannelGroupLoc);
		params.m_pJointParamsPreAdditiveLs = reinterpret_cast<const ndanim::JointParams*>(*ppRightChannelGroupLoc);
		params.m_pArg = pLegFixIkPlugin->m_pArg;

		for (int i = 0; i < 2; ++i)
		{
			params.m_apLegIkChainSetup[i] = &pLegsSetup->m_legSetups[i].m_data;
			params.m_apLegJoints[i] = &pLegsSetup->m_legSetups[i].m_legJoints;
		}

		*pValidOutput = *pLeftValid;

		OrbisAnim::Status subStatus = LegFixIkPluginCallback(&params);

		if (eStatus == OrbisAnim::kSuccess)
		{
			eStatus = subStatus;
		}
	}

	return eStatus;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AddLegFixIkPlugin(OrbisAnim::WorkBuffer* pWorkBuffer,
					   OrbisAnim::ProcessingGroupContext* pGroupContext,
					   const AnimExecutionContext* pContext,
					   const LegFixIkPluginData* pLegFixIkPlugin)
{
	// leg fix IK code doesn't correctly handle joint ranges that don't start from the root :|
	if (pGroupContext->m_pSegmentContext->m_iSegment != 0)
		return;
	if (pGroupContext->m_iProcessingGroup != 0)
		return;

	const uint8_t numChannelGroups = pGroupContext->m_pProcessingGroup->m_numChannelGroups;
	const I32 iLeftIndex	  = pLegFixIkPlugin->m_blendedInstance * numChannelGroups;
	const I32 iRightIndex	  = pLegFixIkPlugin->m_baseInstance * numChannelGroups;
	const I32 iOutputIndex	  = pLegFixIkPlugin->m_blendedInstance * numChannelGroups;
	OrbisAnim::Status eStatus = LegFixIkBinaryPlugInNodeBatchCallback(*pWorkBuffer,
																	  pWorkBuffer->GetBuffer(),
																	  pGroupContext,
																	  pGroupContext->m_plocChannelGroupInstance + iLeftIndex,
																	  pGroupContext->m_plocChannelGroupInstance + iRightIndex,
																	  pGroupContext->m_plocChannelGroupInstance + iOutputIndex,
																	  pGroupContext->m_pValidOutputChannels + iLeftIndex,
																	  pGroupContext->m_pValidOutputChannels + iRightIndex,
																	  pGroupContext->m_pValidOutputChannels + iOutputIndex,
																	  pContext,
																	  pLegFixIkPlugin);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static OrbisAnim::Status HandFixIkBinaryPlugInNodeBatchCallback(OrbisAnim::WorkBuffer& workBuffer,
																void* pWorkBufferMemory,
																OrbisAnim::ProcessingGroupContext* pGroupContext,
																void const* const* ppLeftChannelGroupLoc,
																void const* const* ppRightChannelGroupLoc,
																void* const* ppOutputChannelGroupLoc,
																OrbisAnim::ValidBits const* pLeftValid,
																OrbisAnim::ValidBits const* pRightValid,
																OrbisAnim::ValidBits* pValidOutput,
																const AnimExecutionContext* pContext,
																const HandFixIkPluginData* pHandFixIkPlugin,
																JointSet* pPluginJointSet)
{
	const ArmIkChainSetups* pArmSetups = g_ikSetupTable.GetArmIkSetups(pContext->m_pSkel->m_skelId);

	if (!pArmSetups)
	{
		return OrbisAnim::kFatalErrorUsage;
	}

	ANIM_ASSERT(*ppLeftChannelGroupLoc == *ppOutputChannelGroupLoc);

	ndanim::JointParams* pJointParamsLs = reinterpret_cast<ndanim::JointParams*>(*ppOutputChannelGroupLoc);
	ndanim::JointParams const* pJointParamsPreAdditiveLs = reinterpret_cast<ndanim::JointParams const*>(*ppRightChannelGroupLoc);

	HandFixIkPluginParams params;
	params.m_pJointParamsPreAdditiveLs = pJointParamsPreAdditiveLs;
	params.m_pJointParamsLs = pJointParamsLs;
	params.m_pJointSet = pPluginJointSet ? pPluginJointSet : pContext->m_pPluginJointSet;
	params.m_pValidBitsOut = pValidOutput;
	params.m_apArmChains[kLeftArm] = &pArmSetups->m_armSetups[kLeftArm];
	params.m_apArmChains[kRightArm] = &pArmSetups->m_armSetups[kRightArm];

	params.m_tt = pHandFixIkPlugin->m_arg.m_tt;
	params.m_flipped = pHandFixIkPlugin->m_arg.m_flipped;

	for (int i = 0; i < ARRAY_COUNT(params.m_handsToIk); ++i)
	{
		params.m_handsToIk[i] = pHandFixIkPlugin->m_arg.m_handsToIk[i];
	}

	return HandFixIkPluginCallback(&params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AddHandFixIkPlugin(OrbisAnim::WorkBuffer* pWorkBuffer,
						OrbisAnim::ProcessingGroupContext* pGroupContext,
						const AnimExecutionContext* pContext,
						const HandFixIkPluginData* pHandFixIkPlugin,
						JointSet* pPluginJointSet)
{
	if (pGroupContext->m_pSegmentContext->m_iSegment != 0)
		return;
	if (pGroupContext->m_iProcessingGroup != 0)
		return;

	const uint8_t numChannelGroups = pGroupContext->m_pProcessingGroup->m_numChannelGroups;
	const I32 iLeftIndex	  = pHandFixIkPlugin->m_blendedInstance * numChannelGroups;
	const I32 iRightIndex	  = pHandFixIkPlugin->m_baseInstance * numChannelGroups;
	const I32 iOutputIndex	  = pHandFixIkPlugin->m_blendedInstance * numChannelGroups;

	OrbisAnim::Status eStatus = HandFixIkBinaryPlugInNodeBatchCallback(*pWorkBuffer,
																	   pWorkBuffer->GetBuffer(),
																	   pGroupContext,
																	   pGroupContext->m_plocChannelGroupInstance + iLeftIndex,
																	   pGroupContext->m_plocChannelGroupInstance + iRightIndex,
																	   pGroupContext->m_plocChannelGroupInstance + iOutputIndex,
																	   pGroupContext->m_pValidOutputChannels + iLeftIndex,
																	   pGroupContext->m_pValidOutputChannels + iRightIndex,
																	   pGroupContext->m_pValidOutputChannels + iOutputIndex,
																	   pContext,
																	   pHandFixIkPlugin,
																	   pPluginJointSet);
	// 	pMgr->EvaluateBinaryPlugin(pGroupContext,
	// 		pHandFixIkPlugin->m_blendedInstance,
	// 		pHandFixIkPlugin->m_baseInstance,
	// 		pHandFixIkPlugin->m_blendedInstance,
	// 		HandFixIkBinaryPlugInNodeBatchCallback,
	// 		const_cast<HandFixIkPluginData*>(pHandFixIkPlugin));
}

/// --------------------------------------------------------------------------------------------------------------- ///
OrbisAnim::Status JacobianIkUnaryPluginNodeBatchCallback(OrbisAnim::WorkBuffer&,
														 void* pWorkBufferMemory,
														 OrbisAnim::ProcessingGroupContext* pProcessingGroupContext,
														 void* const* ppChannelGroup,
														 OrbisAnim::ValidBits* pValidChannelsOutput,
														 void* pContext)
{
	ANIM_ASSERT(IsPointerAligned(pContext, Alignment(ALIGN_OF(JacobianIkPluginData))));
	const JacobianIkPluginData* pJacobianIkPlugin = PunPtr<const JacobianIkPluginData*>(pContext);

	OrbisAnim::SegmentContext* pSegmentContext = pProcessingGroupContext->m_pSegmentContext;
	OrbisAnim::JointParams* pJointParamsLs = static_cast<OrbisAnim::JointParams*>(pSegmentContext->m_locJointParams);
	const OrbisAnim::AnimHierarchySegment* pSegment = pSegmentContext->m_pSegment;

	return JacobianIkPluginCallback(pJacobianIkPlugin, pJointParamsLs, pSegment, pValidChannelsOutput);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AddJacobianIkPlugin(OrbisAnim::WorkBuffer* pWorkBuffer,
						 OrbisAnim::ProcessingGroupContext* pGroupContext,
						 const OrbisAnim::AnimHierarchy* pSkeleton,
						 const JacobianIkPluginData* pJacobianIkPlugin)
{
	if (pGroupContext->m_iProcessingGroup != 0)
		return;

	ANIM_ASSERT(IsPointerAligned(pJacobianIkPlugin, Alignment(ALIGN_OF(JacobianIkPluginData))));
	const I32 iInputOutputIndex = pJacobianIkPlugin->m_inputOutputInstance;

	OrbisAnim::Status eStatus = JacobianIkUnaryPluginNodeBatchCallback(*pWorkBuffer,
																	   pWorkBuffer->GetBuffer(),
																	   pGroupContext,
																	   pGroupContext->m_plocChannelGroupInstance + iInputOutputIndex,
																	   pGroupContext->m_pValidOutputChannels + iInputOutputIndex,
																	   const_cast<JacobianIkPluginData*>(pJacobianIkPlugin));
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdProcessAnimPhasePluginFunc(OrbisAnim::WorkBuffer* pWorkBuffer,
								  OrbisAnim::SegmentContext* pSegmentContext,
								  OrbisAnim::ProcessingGroupContext* pGroupContext,
								  const AnimExecutionContext* pContext,
								  StringId64 pluginName,
								  const void* pPluginData,
								  JointSet* pPluginJointSet)
{
	ANIM_ASSERT(IsPointerAligned(pPluginData, kAlign8));
	ANIM_ASSERT(pGroupContext);
	ANIM_ASSERT(pGroupContext->m_pSegmentContext);

	switch (pluginName.GetValue())
	{
	case SID_VAL("leg-fix-ik"):
		{
			const LegFixIkPluginData* pLegFixIkPlugin = static_cast<const LegFixIkPluginData*>(pPluginData);
			AddLegFixIkPlugin(pWorkBuffer, pGroupContext, pContext, pLegFixIkPlugin);
		}
		break;

	case SID_VAL("hand-fix-ik"):
		{
			const HandFixIkPluginData* pHandFixIkPlugin = static_cast<const HandFixIkPluginData*>(pPluginData);
			AddHandFixIkPlugin(pWorkBuffer, pGroupContext, pContext, pHandFixIkPlugin, pPluginJointSet);
		}
		break;

	case SID_VAL("jacobian-ik"):
		{
			const JacobianIkPluginData* pJacobianIkPlugin = static_cast<const JacobianIkPluginData*>(pPluginData);
			AddJacobianIkPlugin(pWorkBuffer, pGroupContext, pContext->m_pSkel->m_pAnimHierarchy, pJacobianIkPlugin);
		}
		break;

	case SID_VAL("ik-node"):
		{
			AnimPluginContext pluginContext;
			pluginContext.m_pWorkBuffer = pWorkBuffer;
			pluginContext.m_pGroupContext = pGroupContext;
			pluginContext.m_pContext = pContext;

			AnimSnapshotNodeIK::AnimPluginCallback(&pluginContext, pPluginData);
		}
		break;

	case SID_VAL("arm-ik-node"):
		{
			AnimPluginContext pluginContext;
			pluginContext.m_pWorkBuffer = pWorkBuffer;
			pluginContext.m_pGroupContext = pGroupContext;
			pluginContext.m_pContext = pContext;

			AnimSnapshotNodeArmIk::AnimPluginCallback(&pluginContext, pPluginData);
		}
		break;

	case SID_VAL("joint-spring-node"):
		{
			AnimPluginContext pluginContext;
			pluginContext.m_pWorkBuffer = pWorkBuffer;
			pluginContext.m_pGroupContext = pGroupContext;
			pluginContext.m_pContext = pContext;

			AnimSnapshotNodeJointSpring::AnimPluginCallback(&pluginContext, pPluginData);
		}
		break;

	default:
		break;
	}
}


//----------------------------------------------------------------------------------------------------------------------
static void ApplyJointOverride(ndanim::JointParams* pJointParamsLs, const JointOverrideData* pData)
{
	for (U32F ii = 0; ii < pData->m_maxJoints; ++ii)
	{
		if (pData->m_pJointIndex[ii] != JointOverrideData::kInvalidIndex)
		{
			ndanim::JointParams& jointParams = pJointParamsLs[pData->m_pJointIndex[ii]];
			const U8 componentFlags = pData->m_pComponentFlags[ii];
			if (componentFlags & JointOverrideData::kOverrideTranslationMask)
			{
				const Point trans = pData->m_pJointTrans[ii];
				jointParams.m_trans = trans;
			}
			if (componentFlags & JointOverrideData::kOverrideRotationMask)
			{
				const Quat rot = pData->m_pJointRot[ii];
				jointParams.m_quat = rot;
			}
			if (componentFlags & JointOverrideData::kOverrideScaleMask)
			{
				const float scale = pData->m_pJointScale[ii];
				jointParams.m_scale = Vector(scale, scale, scale);
			}
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdProcessPostAnimPhasePluginFunc(OrbisAnim::WorkBuffer* pWorkBuffer,
									  OrbisAnim::SegmentContext* pSegmentContext,
									  const AnimExecutionContext* pContext,
									  StringId64 pluginName,
									  const void* pPluginData,
									  JointSet* pPluginJointSet)
{
	ANIM_ASSERT(pSegmentContext);

	switch (pluginName.GetValue())
	{
	case SID_VAL("joint-override"):
		{
			const EvaluateJointOverridePluginData* pCmdEvalJointOverridePlugin = static_cast<const EvaluateJointOverridePluginData*>(pPluginData);

			const JointOverrideAnimPluginData* pData = (const JointOverrideAnimPluginData*)pContext;
			ndanim::JointParams* pJointParams = (ndanim::JointParams*)pData->m_locJointParams;

			ApplyJointOverride(pJointParams, pData->m_pOverrideData);
		}
		break;

	case SID_VAL("retarget-anim"):
		{
			const EvaluateRetargetAnimPluginData* pCmdData = static_cast<const EvaluateRetargetAnimPluginData*>(pPluginData);
			ANIM_ASSERT(pCmdData->m_pRetargetEntry);
			ANIM_ASSERT(pCmdData->m_pRetargetEntry->m_jointRetarget);
			ExecuteRetargetPlugin(pWorkBuffer,
								  pSegmentContext,
								  pCmdData->m_pRetargetEntry,
								  pCmdData->m_tgtSegmentIndex,
								  pCmdData->m_pSrcValidBitsArray,
								  pCmdData->m_jointDataIsAdditive,
								  pCmdData->m_pSrcSkel,
								  pCmdData->m_pDestSkel,
								  pCmdData->m_pOutPose);
		}
		break;

	default:
		break;
	}
}
