/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-node-leg-fix-ik.h"

#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-node-library.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/anim/anim-state.h"
#include "ndlib/anim/ik/ik-chain-setup.h"
#include "ndlib/anim/ik/ik-setup-table.h"
#include "ndlib/anim/leg-fix-ik/leg-fix-ik-plugin.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/scriptx/h/code.h"

#include "gamelib/level/art-item-skeleton.h"

/// --------------------------------------------------------------------------------------------------------------- ///
ANIM_NODE_REGISTER_WITH_SNAPSHOT_FUNC(AnimSnapshotNodeLegFixIk,
									  AnimSnapshotNodeUnary,
									  SID("anim-node-leg-fix-ik"),
									  AnimSnapshotNodeLegFixIk::SnapshotAnimNode);

FROM_ANIM_NODE_DEFINE(AnimSnapshotNodeLegFixIk);

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSnapshotNodeLegFixIk::AnimSnapshotNodeLegFixIk(StringId64 typeId,
												   StringId64 dcTypeId,
												   SnapshotNodeHeap::Index nodeIndex)
	: ParentClass(typeId, dcTypeId, nodeIndex), m_valid(false)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeLegFixIk::SnapshotNode(AnimStateSnapshot* pSnapshot,
											const DC::AnimNode* pNode,
											const SnapshotAnimNodeTreeParams& params,
											SnapshotAnimNodeTreeResults& results)
{
	ParentClass::SnapshotNode(pSnapshot, pNode, params, results);

	const ArtItemSkeleton* pAnimateSkel = params.m_pAnimData->m_animateSkelHandle.ToArtItem();

	if (pAnimateSkel && g_ikSetupTable.GetLegIkSetups(pAnimateSkel->m_skelId, kBackLegs))
	{
		m_valid = true;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeLegFixIk::GenerateAnimCommands_PreBlend(const AnimStateSnapshot* pSnapshot,
															 AnimCmdList* pAnimCmdList,
															 const AnimCmdGenInfo* pCmdGenInfo,
															 const AnimSnapshotNodeBlend* pBlendNode,
															 bool leftNode,
															 I32F leftInstance,
															 I32F rightInstance,
															 I32F outputInstance) const
{
	if (!m_valid)
		return;

	GenerateLegFixIkCommands_PreBlend(pAnimCmdList, leftInstance, rightInstance, outputInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeLegFixIk::GenerateAnimCommands_PostBlend(const AnimStateSnapshot* pSnapshot,
															  AnimCmdList* pAnimCmdList,
															  const AnimCmdGenInfo* pCmdGenInfo,
															  const AnimSnapshotNodeBlend* pBlendNode,
															  bool leftNode,
															  I32F leftInstance,
															  I32F rightInstance,
															  I32F outputInstance) const
{
	if (!m_valid)
		return;

	GenerateLegFixIkCommands_PostBlend(pAnimCmdList, nullptr, leftInstance, rightInstance, outputInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void AnimSnapshotNodeLegFixIk::GenerateLegFixIkCommands_PreBlend(AnimCmdList* pAnimCmdList,
																 I32F leftInstance,
																 I32F rightInstance,
																 I32F outputInstance)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_disableLegFixIk))
		return;

	const I32F copyOfLeftInstance = Max(Max(leftInstance, rightInstance), outputInstance) + 1;

	pAnimCmdList->AddCmd_EvaluateCopy(leftInstance, copyOfLeftInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void AnimSnapshotNodeLegFixIk::GenerateLegFixIkCommands_PostBlend(AnimCmdList* pAnimCmdList,
																  LegFixIkPluginCallbackArg* pArg,
																  I32F leftInstance,
																  I32F rightInstance,
																  I32F outputInstance)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_disableLegFixIk))
		return;

	const I32F copyOfLeftInstance = Max(Max(leftInstance, rightInstance), outputInstance) + 1;

	LegFixIkPluginData cmd;
	cmd.m_blendedInstance = outputInstance;
	cmd.m_baseInstance	  = copyOfLeftInstance;
	cmd.m_pArg			  = pArg;

	pAnimCmdList->AddCmd_EvaluateAnimPhasePlugin(SID("leg-fix-ik"), &cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void AnimSnapshotNodeLegFixIk::GenerateLegFixIkCommands_PreEval(AnimCmdList* pAnimCmdList,
																I32F baseInstance,
																I32F requiredBreadth)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_disableLegFixIk))
		return;

	const I32F copyOfBaseInstance = baseInstance + requiredBreadth + 1;

	pAnimCmdList->AddCmd_EvaluateCopy(baseInstance, copyOfBaseInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void AnimSnapshotNodeLegFixIk::GenerateLegFixIkCommands_PostEval(AnimCmdList* pAnimCmdList,
																 I32F baseInstance,
																 I32F requiredBreadth)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_disableLegFixIk))
		return;

	const I32F copyOfBaseInstance = baseInstance + requiredBreadth + 1;

	LegFixIkPluginData cmd;
	cmd.m_blendedInstance = baseInstance;
	cmd.m_baseInstance	  = copyOfBaseInstance;
	cmd.m_pArg			  = nullptr;

	pAnimCmdList->AddCmd_EvaluateAnimPhasePlugin(SID("leg-fix-ik"), &cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeLegFixIk::DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const
{
	Tab(pData->m_output, pData->m_depth);
	SetColor(pData->m_output, 0xFFFF00FF);
	PrintTo(pData->m_output, "Leg Fix Node%s\n", m_valid ? "" : " (FAILED due to no LegIkChain for this skeleton)");
	pData->m_depth++;
	GetChild(pSnapshot)->DebugPrint(pSnapshot, pData);
	pData->m_depth--;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SnapshotNodeHeap::Index AnimSnapshotNodeLegFixIk::SnapshotAnimNode(AnimStateSnapshot* pSnapshot,
																   const DC::AnimNode* pNode,
																   const SnapshotAnimNodeTreeParams& params,
																   SnapshotAnimNodeTreeResults& results)
{
	ANIM_ASSERT(pNode->m_dcType == SID("anim-node-leg-fix-ik"));

	const DC::AnimNodeLegFixIk* pDcNode = static_cast<const DC::AnimNodeLegFixIk*>(pNode);

	bool enabled = true;
	if (pDcNode->m_enableFunc)
	{
		const ScriptValue argv[] = { ScriptValue(params.m_pInfoCollection) };

		const DC::ScriptLambda* pLambda = pDcNode->m_enableFunc;
		ANIM_ASSERT(pLambda->m_id == SID("leg-fix-ik-func"));
		
		const ScriptValue lambdaResult = ScriptManager::Eval(pLambda, SID("leg-fix-ik-func"), ARRAY_COUNT(argv), argv);
		enabled = lambdaResult.m_boolean;
	}

	SnapshotNodeHeap::Index returnNodeIndex = -1;

	if (enabled)
	{
		const StringId64 typeId = g_animNodeLibrary.LookupTypeIdFromDcType(pNode->m_dcType);

		ANIM_ASSERT(typeId != INVALID_STRING_ID_64);

		if (AnimSnapshotNode* pNewNode = pSnapshot->m_pSnapshotHeap->AllocateNode(typeId, &returnNodeIndex))
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
	}
	else
	{
		returnNodeIndex = AnimStateSnapshot::SnapshotAnimNodeTree(pSnapshot, pDcNode->m_child, params, results);
	}

	return returnNodeIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegFixIk_AnimStateLayerBlendCallBack_PreBlend(const AnimStateLayer* pStateLayer,
												   const AnimCmdGenLayerContext& context,
												   AnimCmdList* pAnimCmdList,
												   SkeletonId skelId,
												   I32F leftInstance,
												   I32F rightInstance,
												   I32F outputInstance,
												   ndanim::BlendMode blendMode,
												   uintptr_t userData)
{
	AnimSnapshotNodeLegFixIk::GenerateLegFixIkCommands_PreBlend(pAnimCmdList,
																leftInstance,
																rightInstance,
																outputInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegFixIk_AnimStateLayerBlendCallBack_PostBlend(const AnimStateLayer* pStateLayer,
													const AnimCmdGenLayerContext& context,
													AnimCmdList* pAnimCmdList,
													SkeletonId skelId,
													I32F leftInstance,
													I32F rightInstance,
													I32F outputInstance,
													ndanim::BlendMode blendMode,
													uintptr_t userData)
{
	LegFixIkPluginCallbackArg* pArg = (LegFixIkPluginCallbackArg*)userData;
	if (pArg)
	{
		ANIM_ASSERT(pArg->CheckMagic());
	}

	AnimSnapshotNodeLegFixIk::GenerateLegFixIkCommands_PostBlend(pAnimCmdList,
																 pArg,
																 leftInstance,
																 rightInstance,
																 outputInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegFixIk_AnimSimpleLayerBlendCallBack_PreBlend(AnimCmdList* pAnimCmdList,
													SkeletonId skelId,
													I32F leftInstance,
													I32F rightInstance,
													I32F outputInstance,
													ndanim::BlendMode blendMode,
													uintptr_t userData)
{
	AnimSnapshotNodeLegFixIk::GenerateLegFixIkCommands_PreBlend(pAnimCmdList,
																leftInstance,
																rightInstance,
																outputInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegFixIk_AnimSimpleLayerBlendCallBack_PostBlend(AnimCmdList* pAnimCmdList,
													 SkeletonId skelId,
													 I32F leftInstance,
													 I32F rightInstance,
													 I32F outputInstance,
													 ndanim::BlendMode blendMode,
													 uintptr_t userData)
{
	LegFixIkPluginCallbackArg* pArg = (LegFixIkPluginCallbackArg*)userData;
	if (pArg)
	{
		ANIM_ASSERT(pArg->CheckMagic());
	}

	AnimSnapshotNodeLegFixIk::GenerateLegFixIkCommands_PostBlend(pAnimCmdList,
																 pArg,
																 leftInstance,
																 rightInstance,
																 outputInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ForceLinkAnimSnapshotNodeLegFixIk()
{
}
