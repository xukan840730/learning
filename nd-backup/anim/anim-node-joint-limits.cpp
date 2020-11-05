/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-node-joint-limits.h"

#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/anim/anim-state.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/anim/gesture-controller.h"
#include "gamelib/gameplay/nd-game-object.h"

/// --------------------------------------------------------------------------------------------------------------- ///
ANIM_NODE_REGISTER_WITH_SNAPSHOT_FUNC(AnimNodeJointLimits,
									  AnimSnapshotNodeUnary,
									  SID("anim-node-joint-limits"),
									  AnimNodeJointLimits::SnapshotNodeFunc);

FROM_ANIM_NODE_DEFINE(AnimNodeJointLimits);

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
SnapshotNodeHeap::Index AnimNodeJointLimits::SnapshotNodeFunc(AnimStateSnapshot* pSnapshot,
															  const DC::AnimNode* pDcAnimNode,
															  const SnapshotAnimNodeTreeParams& params,
															  SnapshotAnimNodeTreeResults& results)
{
	ANIM_ASSERT(pDcAnimNode->m_dcType == SID("anim-node-joint-limits"));

	SnapshotNodeHeap::Index returnNodeIndex = -1;

	if (AnimNodeJointLimits* pNewNode = pSnapshot->m_pSnapshotHeap->AllocateNode<AnimNodeJointLimits>(&returnNodeIndex))
	{
		pNewNode->SnapshotNode(pSnapshot, pDcAnimNode, params, results);
	}
	else
	{
		MsgAnimErr("[%s] Ran out of snapshot memory creating new snapshot node '%s' for state '%s'\n",
				   DevKitOnly_StringIdToString(params.m_pAnimData->m_hProcess.GetUserId()),
				   DevKitOnly_StringIdToString(pDcAnimNode->m_dcType),
				   pSnapshot->m_animState.m_name.m_string.GetString());

		returnNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
	}

	return returnNodeIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimNodeJointLimits::GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
											   AnimCmdList* pAnimCmdList,
											   I32F outputInstance,
											   const AnimCmdGenInfo* pCmdGenInfo) const
{
	ParentClass::GenerateAnimCommands(pSnapshot, pAnimCmdList, outputInstance, pCmdGenInfo);

	const FgAnimData* pAnimData = pCmdGenInfo->m_pContext->m_pAnimData;
	const JointLimits* pJointLimits = GetCustomLimits();
	
	if (!pJointLimits)
	{
		pJointLimits = pAnimData->m_pJointLimits;
	}

	if (pJointLimits && pAnimData->m_pPluginJointSet && (pCmdGenInfo->m_pContext->m_pass == 0))
	{
		const float rootScale = pAnimData->m_jointCache.GetJointParamsLs(0).m_scale.X();
		pAnimCmdList->AddCmd_InitializeJointSet_AnimPhase(pAnimData->m_pPluginJointSet, rootScale, outputInstance);

		pAnimCmdList->AddCmd_ApplyJointLimits(pJointLimits, pAnimData->m_pPluginJointSet);

		pAnimCmdList->AddCmd_CommitJointSet_AnimPhase(pAnimData->m_pPluginJointSet, 1.0f, outputInstance);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimNodeJointLimits::SetCustomLimits(NdGameObjectHandle hGo, StringId64 customLimitsId)
{
	m_customLimitsId = customLimitsId;
	m_hOwner = hGo;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const JointLimits* AnimNodeJointLimits::GetCustomLimits() const
{
	if (!m_customLimitsId)
		return nullptr;

	const NdGameObject* pGo = m_hOwner.ToProcess();
	const IGestureController* pGestureController = pGo ? pGo->GetGestureController() : nullptr;
	const JointLimits* pCustomLimits = pGestureController ? pGestureController->GetCustomJointLimits(m_customLimitsId)
														  : nullptr;

	return pCustomLimits;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimNodeJointLimits::DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const
{
	const Color color = Color(0xFFFF00FF);

	Tab(pData->m_output, pData->m_depth);
	SetColor(pData->m_output, color);
	
	if (const JointLimits* pCustomLimits = GetCustomLimits())
	{
		PrintTo(pData->m_output,
				"Joint Limits (custom: %s)\n",
				DevKitOnly_StringIdToString(pCustomLimits->GetSettingsId()));
	}
	else
	{
		PrintTo(pData->m_output, "Joint Limits\n");
	}

	pData->m_depth++;
	GetChild(pSnapshot)->DebugPrint(pSnapshot, pData);
	pData->m_depth--;
}
