/*
 * Copyright (c) 2017 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/subsystem-ik-node.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-state.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/anim/ik/joint-chain.h"

#include "ndlib/scriptx/h/animation-script-types.h"


/// --------------------------------------------------------------------------------------------------------------- ///
ANIM_NODE_REGISTER(AnimSnapshotNodeSubsystemIK, AnimSnapshotNodeUnary, SID("anim-node-subsystem-ik"));
ANIM_NODE_REGISTER(AnimSnapshotNodeSubsystemPreIK, AnimSnapshotNodeUnary, SID("anim-node-subsystem-pre-ik"));

FROM_ANIM_NODE_DEFINE(AnimSnapshotNodeSubsystemIK);
FROM_ANIM_NODE_DEFINE(AnimSnapshotNodeSubsystemPreIK);

void AnimSnapshotNodeSubsystemIK::SnapshotNode(AnimStateSnapshot* pSnapshot, const DC::AnimNode* pDcAnimNode,
	const SnapshotAnimNodeTreeParams& params, SnapshotAnimNodeTreeResults& results)
{
	ParentClass::SnapshotNode(pSnapshot, pDcAnimNode, params, results);

	const DC::AnimNodeSubsystemIk* pDcSubsystemIkNode = static_cast<const DC::AnimNodeSubsystemIk*>(pDcAnimNode);
	m_ikId = pDcSubsystemIkNode->m_ikId;
	m_subsystemType = pDcSubsystemIkNode->m_subsystemType;
	m_subsystemLayer = pDcSubsystemIkNode->m_subsystemLayer;
}

void AnimSnapshotNodeSubsystemIK::GenerateAnimCommands(const AnimStateSnapshot* pSnapshot, AnimCmdList* pAnimCmdList,
	I32F outputInstance, const AnimCmdGenInfo* pCmdGenInfo) const
{
	ParentClass::GenerateAnimCommands(pSnapshot, pAnimCmdList, outputInstance, pCmdGenInfo);

	{
		SubsystemIkPluginParams params;
		params.m_pInst = pCmdGenInfo->m_pInst;
		params.m_instanceIndex = outputInstance;
		params.m_ikId = m_ikId;
		params.m_subsystemType = m_subsystemType;
		params.m_subsystemLayer = m_subsystemLayer;
		pAnimCmdList->AddCmd_EvaluateAnimPhasePlugin(SID("subsystem-ik-node"), &params);
	}
}

void AnimSnapshotNodeSubsystemIK::DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const
{
	Tab(pData->m_output, pData->m_depth);
	SetColor(pData->m_output, 0xFF00FFFF);
	PrintTo(pData->m_output, "Subsystem IK: %s\n", DevKitOnly_StringIdToString(m_ikId));
	pData->m_depth++;
	const AnimSnapshotNode* pChild = GetChild(pSnapshot);
	pChild->DebugPrint(pSnapshot, pData);
	pData->m_depth--;
	SetColor(pData->m_output, 0xFFFFFFFF);
}

//static
void AnimSnapshotNodeSubsystemIK::AnimPluginCallback(AnimPluginContext* pPluginContext, const void* pData)
{
	const SubsystemIkPluginParams* pParams = static_cast<const SubsystemIkPluginParams*>(pData);

	const AnimStateLayer* pLayer = pParams->m_pInst->GetLayer();
	pLayer->InstanceCallBackIkFunc(pParams->m_pInst, pPluginContext, pData);
}



void AnimSnapshotNodeSubsystemPreIK::SnapshotNode(AnimStateSnapshot* pSnapshot, const DC::AnimNode* pDcAnimNode,
	const SnapshotAnimNodeTreeParams& params, SnapshotAnimNodeTreeResults& results)
{
	ParentClass::SnapshotNode(pSnapshot, pDcAnimNode, params, results);

	const DC::AnimNodeSubsystemIk* pDcSubsystemIkNode = static_cast<const DC::AnimNodeSubsystemIk*>(pDcAnimNode);
	m_ikId = pDcSubsystemIkNode->m_ikId;
	m_subsystemType = pDcSubsystemIkNode->m_subsystemType;
	m_subsystemLayer = pDcSubsystemIkNode->m_subsystemLayer;
}

void AnimSnapshotNodeSubsystemPreIK::GenerateAnimCommands(const AnimStateSnapshot* pSnapshot, AnimCmdList* pAnimCmdList,
	I32F outputInstance, const AnimCmdGenInfo* pCmdGenInfo) const
{
	{
		SubsystemIkPluginParams params;
		params.m_pInst = pCmdGenInfo->m_pInst;
		params.m_instanceIndex = outputInstance;
		params.m_ikId = m_ikId;
		params.m_subsystemType = m_subsystemType;
		params.m_subsystemLayer = m_subsystemLayer;
		pAnimCmdList->AddCmd_EvaluateAnimPhasePlugin(SID("subsystem-ik-node"), &params);
	}

	ParentClass::GenerateAnimCommands(pSnapshot, pAnimCmdList, outputInstance, pCmdGenInfo);
}

void AnimSnapshotNodeSubsystemPreIK::DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const
{
	Tab(pData->m_output, pData->m_depth);
	SetColor(pData->m_output, 0xFF00FFFF);
	PrintTo(pData->m_output, "Subsystem PreIK: %s\n", DevKitOnly_StringIdToString(m_ikId));
	pData->m_depth++;
	const AnimSnapshotNode* pChild = GetChild(pSnapshot);
	pChild->DebugPrint(pSnapshot, pData);
	pData->m_depth--;
	SetColor(pData->m_output, 0xFFFFFFFF);
}