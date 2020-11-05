/*
 * Copyright (c) 2014 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-node-arm-ik.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-plugin-context.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/anim/anim-state.h"
#include "ndlib/anim/armik.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/nd-options.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/anim-ik-defines.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-skeleton.h"

/// --------------------------------------------------------------------------------------------------------------- ///
ANIM_NODE_REGISTER_WITH_SNAPSHOT_FUNC(AnimSnapshotNodeArmIk,
									  AnimSnapshotNodeUnary,
									  SID("anim-node-arm-ik"),
									  AnimSnapshotNodeArmIk::SnapshotAnimNode);

FROM_ANIM_NODE_DEFINE(AnimSnapshotNodeArmIk);

/// --------------------------------------------------------------------------------------------------------------- ///
struct ArmIkPluginParams
{
	Locator m_goalLocatorOs;
	I32 m_outputInstance;
	DC::ArmIkNodeConfig m_config;
	float m_tt;
};

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSnapshotNodeArmIk::AnimSnapshotNodeArmIk(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex)
	: ParentClass(typeId, dcTypeId, nodeIndex), m_lastTt(1.0f), m_channelEvaluated(false)
{
	memset(&m_config, 0, sizeof(m_config));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeArmIk::SnapshotNode(AnimStateSnapshot* pSnapshot,
										 const DC::AnimNode* pNode,
										 const SnapshotAnimNodeTreeParams& params,
										 SnapshotAnimNodeTreeResults& results)
{
	ParentClass::SnapshotNode(pSnapshot, pNode, params, results);

	ANIM_ASSERT(pNode->m_dcType == SID("anim-node-arm-ik"));

	const DC::AnimNodeArmIk* pArmIkNode = static_cast<const DC::AnimNodeArmIk*>(pNode);

	if (pArmIkNode->m_config)
	{
		m_config = *(pArmIkNode->m_config);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeArmIk::GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
												 AnimCmdList* pAnimCmdList,
												 I32F outputInstance,
												 const AnimCmdGenInfo* pCmdGenInfo) const
{
	ParentClass::GenerateAnimCommands(pSnapshot, pAnimCmdList, outputInstance, pCmdGenInfo);

	const FgAnimData* pAnimData = pCmdGenInfo->m_pContext->m_pAnimData;

	if (pAnimData->m_curSkelHandle.ToArtItem() != pAnimData->m_animateSkelHandle.ToArtItem())
	{
		return;
	}

	if (const AnimSnapshotNode* pChild = GetChild(pSnapshot))
	{
		SnapshotEvaluateParams evalParams;

		if (pCmdGenInfo->m_pInfoCollection->m_actor)
		{
			evalParams.m_blendChannels = (pCmdGenInfo->m_pInfoCollection->m_actor->m_blendChannels != 0);
		}

		evalParams.m_channelName   = m_config.m_channelName;
		evalParams.m_statePhase	   = pCmdGenInfo->m_statePhase;
		evalParams.m_statePhasePre = -1.0f;
		// Should probably never flip since this takes place before flipping happens? Make it optional for now so it doesn't break anything.
		evalParams.m_flipped = m_config.m_noFlipChannelEval ? false : pSnapshot->IsFlipped();
		evalParams.m_forceChannelBlending = false;

		ndanim::JointParams channel;

		bool const evaluated = pChild->EvaluateChannel(pSnapshot, &channel, evalParams);
		
		if (evaluated)
		{
			float tt = 1.0f;

			if (m_config.m_enableTtFunc)
			{
				const DC::ScriptLambda* pLambda = m_config.m_enableTtFunc;

				const ScriptValue argv[] = { ScriptValue(pCmdGenInfo->m_pInfoCollection) };

				const ScriptValue result = ScriptManager::Eval(pLambda, INVALID_STRING_ID_64, ARRAY_COUNT(argv), argv);

				tt = result.m_float;
			}

			m_lastTt = tt;

			ArmIkPluginParams params;
			params.m_outputInstance = outputInstance;
			params.m_goalLocatorOs	= Locator(channel.m_trans, channel.m_quat);
			params.m_config			= m_config;
			params.m_tt = tt;

			pAnimCmdList->AddCmd_EvaluateAnimPhasePlugin(SID("arm-ik-node"), &params);
		}

		m_channelEvaluated = evaluated;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeArmIk::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeArmIk::DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const
{
	const Color color = (m_lastTt > 0.0f && m_channelEvaluated) ? Color(0xFFFF00FF) : kColorDarkGray;

	Tab(pData->m_output, pData->m_depth);
	SetColor(pData->m_output, color);
	PrintTo(pData->m_output,
			"Arm Ik: %s Arm -> %s, blend: %.2f %s\n",
			m_config.m_arm == DC::kArmIkArmLeft ? "Left" : "Right",
			DevKitOnly_StringIdToString(m_config.m_channelName),
			m_lastTt,
			m_channelEvaluated ? "" : "Channel not found");

	pData->m_depth++;
	GetChild(pSnapshot)->DebugPrint(pSnapshot, pData);
	pData->m_depth--;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SnapshotNodeHeap::Index AnimSnapshotNodeArmIk::SnapshotAnimNode(AnimStateSnapshot* pSnapshot,
																const DC::AnimNode* pNode,
																const SnapshotAnimNodeTreeParams& params,
																SnapshotAnimNodeTreeResults& results)
{
	ANIM_ASSERT(pNode->m_dcType == SID("anim-node-arm-ik"));

	const DC::AnimNodeArmIk* pDcNode = static_cast<const DC::AnimNodeArmIk*>(pNode);

	SnapshotNodeHeap::Index returnNodeIndex = -1;
	SnapshotNodeHeap* pHeap = pSnapshot->m_pSnapshotHeap;

	if (AnimSnapshotNodeArmIk* pNewNode = pHeap->AllocateNode<AnimSnapshotNodeArmIk>(&returnNodeIndex))
	{
		pNewNode->SnapshotNode(pSnapshot, pNode, params, results);
	}
	else
	{
		MsgAnimErr("[%s] Ran out of snapshot memory creating new snapshot node '%s' for state '%s'\n",
				   DevKitOnly_StringIdToString(params.m_pAnimData->m_hProcess.GetUserId()),
				   DevKitOnly_StringIdToString(pNode->m_dcType),
				   pSnapshot->m_animState.m_name.m_string.GetString());

		returnNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
	}

	return returnNodeIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeArmIk::AnimPluginCallback(AnimPluginContext* pPluginContext, const void* pData)
{
	if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_disableAnimNodeIk))
		return;

	if ((pPluginContext->m_pGroupContext->m_pSegmentContext->m_iSegment != 0) ||
		(pPluginContext->m_pGroupContext->m_iProcessingGroup != 0))
	{
		return;
	}

	// We have to copy the params as pData will likely not be properly aligned
	ArmIkPluginParams copiedParams;
	memcpy(&copiedParams, pData, sizeof(copiedParams));

	JointSet* pJointSet = pPluginContext->m_pContext->m_pPluginJointSet;
	const I32F numJoints = OrbisAnim::kJointGroupSize;
	const I32F armIndex = (copiedParams.m_config.m_arm == DC::kArmIkArmLeft ? 0 : 1);

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	ndanim::JointParams* pJointParamsLs = pPluginContext->GetJoints(copiedParams.m_outputInstance);
	OrbisAnim::ValidBits* pValidBits	= pPluginContext->GetValidBits(copiedParams.m_outputInstance);
	OrbisAnim::ValidBits* pValidBits2	= pPluginContext->GetBlendGroupJointValidBits(copiedParams.m_outputInstance);

	const ndanim::JointHierarchy* pHierarchy = pPluginContext->m_pContext->m_pSkel->m_pAnimHierarchy;
	const ndanim::JointParams* pDefaultParamsLs = ndanim::GetDefaultJointPoseTable(pHierarchy);

	if (pJointSet->ReadFromJointParams(pJointParamsLs, 0, OrbisAnim::kJointGroupSize, 1.0f, pValidBits, pDefaultParamsLs))
	{
		const Locator alignWs = Locator(*pPluginContext->m_pContext->m_pObjXform);
		const Point goalPosWs = alignWs.TransformPoint(copiedParams.m_goalLocatorOs.Pos());

		ArmIkInstance armIk;
		armIk.m_ikChain	  = pJointSet;
		armIk.m_armIndex  = armIndex;
		armIk.m_goalPosWs = goalPosWs;
		armIk.m_tt		  = 1.0f;
		armIk.m_abortIfCantSolve = false;

		if (SolveArmIk(&armIk))
		{
			const I32F wristOffset = pJointSet->FindJointOffset(copiedParams.m_config.m_wristFixupJoint);
			if (wristOffset >= 0)
			{
				pJointSet->SetJointLocOs(wristOffset, copiedParams.m_goalLocatorOs);
			}

			pJointSet->WriteJointParamsBlend(copiedParams.m_tt,
											 pJointParamsLs,
											 0,
											 OrbisAnim::kJointGroupSize,
											 true);

			pJointSet->WriteJointValidBits(armIk.m_jointOffsetsUsed[2], 0, pValidBits);
			pJointSet->WriteJointValidBits(armIk.m_jointOffsetsUsed[2], 0, pValidBits2);
		}
		else
		{
			pJointSet->DiscardJointCache();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ForceLinkAnimSnapshotNodeArmIk()
{
}
