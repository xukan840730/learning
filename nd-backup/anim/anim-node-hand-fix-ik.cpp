/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-node-hand-fix-ik.h"

#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-node-library.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"

/// --------------------------------------------------------------------------------------------------------------- ///
ANIM_NODE_REGISTER_WITH_SNAPSHOT_FUNC(AnimSnapshotNodeHandFixIk,
									  AnimSnapshotNodeUnary,
									  SID("anim-node-hand-fix-ik"),
									  AnimSnapshotNodeHandFixIk::SnapshotAnimNode);

FROM_ANIM_NODE_DEFINE(AnimSnapshotNodeHandFixIk);

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSnapshotNodeHandFixIk::AnimSnapshotNodeHandFixIk(StringId64 typeId,
													 StringId64 dcTypeId,
													 SnapshotNodeHeap::Index nodeIndex)
	: ParentClass(typeId, dcTypeId, nodeIndex)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeHandFixIk::SnapshotNode(AnimStateSnapshot* pSnapshot,
											 const DC::AnimNode* pNode,
											 const SnapshotAnimNodeTreeParams& params,
											 SnapshotAnimNodeTreeResults& results)
{
	ParentClass::SnapshotNode(pSnapshot, pNode, params, results);

	m_args.m_flipped = params.m_stateFlipped;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeHandFixIk::SetHandsToIk(DC::HandIkHand handsToIk)
{
	m_args.m_handsToIk[0] = (handsToIk & DC::kHandIkHandLeft);
	m_args.m_handsToIk[1] = (handsToIk & DC::kHandIkHandRight);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeHandFixIk::GenerateAnimCommands_PreBlend(const AnimStateSnapshot* pSnapshot,
															  AnimCmdList* pAnimCmdList,
															  const AnimCmdGenInfo* pCmdGenInfo,
															  const AnimSnapshotNodeBlend* pBlendNode,
															  bool leftNode,
															  I32F leftInstance,
															  I32F rightInstance,
															  I32F outputInstance) const
{
	GenerateHandFixIkCommands_PreBlend(pAnimCmdList, leftInstance, rightInstance, outputInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeHandFixIk::GenerateAnimCommands_PostBlend(const AnimStateSnapshot* pSnapshot,
															   AnimCmdList* pAnimCmdList,
															   const AnimCmdGenInfo* pCmdGenInfo,
															   const AnimSnapshotNodeBlend* pBlendNode,
															   bool leftNode,
															   I32F leftInstance,
															   I32F rightInstance,
															   I32F outputInstance) const
{
	GenerateHandFixIkCommands_PostBlend(pAnimCmdList, &m_args, leftInstance, rightInstance, outputInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeHandFixIk::DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const
{
	Tab(pData->m_output, pData->m_depth);
	SetColor(pData->m_output, 0xFFFF00FF);
	PrintTo(pData->m_output,
			"Hand Fix Node: %s %s\n",
			m_args.m_handsToIk[0] ? "left" : "",
			m_args.m_handsToIk[1] ? "right" : "");
	pData->m_depth++;
	GetChild(pSnapshot)->DebugPrint(pSnapshot, pData);
	pData->m_depth--;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SnapshotNodeHeap::Index AnimSnapshotNodeHandFixIk::SnapshotAnimNode(AnimStateSnapshot* pSnapshot,
																	const DC::AnimNode* pNode,
																	const SnapshotAnimNodeTreeParams& params,
																	SnapshotAnimNodeTreeResults& results)
{
	ANIM_ASSERT(pNode->m_dcType == SID("anim-node-hand-fix-ik"));

	const DC::AnimNodeHandFixIk* pDcNode = static_cast<const DC::AnimNodeHandFixIk*>(pNode);

	DC::HandIkHand handsToIk = 0;
	if (pDcNode->m_handFunc)
	{
		DC::AnimInfoCollection scriptInfoCollection(*params.m_pInfoCollection);
		scriptInfoCollection.m_actor	= scriptInfoCollection.m_actor;
		scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
		scriptInfoCollection.m_top		= scriptInfoCollection.m_top;

		const ScriptValue argv[] = { ScriptValue(&scriptInfoCollection) };

		const DC::ScriptLambda* pLambda = pDcNode->m_handFunc;
		VALIDATE_LAMBDA(pLambda, "hand-ik-func", m_animState.m_name.m_symbol);
		ScriptValue lambdaResult = ScriptManager::Eval(pLambda, SID("hand-ik-func"), ARRAY_COUNT(argv), argv);
		handsToIk				 = lambdaResult.m_int32;
	}

	SnapshotNodeHeap::Index returnNodeIndex = -1;
	if (handsToIk != 0)
	{
		const StringId64 typeId = g_animNodeLibrary.LookupTypeIdFromDcType(pNode->m_dcType);

		if (typeId == INVALID_STRING_ID_64)
		{
			ANIM_ASSERTF(false, ("Bad AnimNode in tree! '%s'", DevKitOnly_StringIdToString(pNode->m_dcType)));
			return returnNodeIndex;
		}

		if (AnimSnapshotNode* pNewNode = pSnapshot->m_pSnapshotHeap->AllocateNode(typeId, &returnNodeIndex))
		{
			pNewNode->SnapshotNode(pSnapshot, pNode, params, results);

			AnimSnapshotNodeHandFixIk* pHandFixNode = AnimSnapshotNodeHandFixIk::FromAnimNode(pNewNode);
			if (pHandFixNode)
			{
				pHandFixNode->SetHandsToIk(handsToIk);
			}
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
/* static */
void AnimSnapshotNodeHandFixIk::GenerateHandFixIkCommands_PreBlend(AnimCmdList* pAnimCmdList,
																   I32F leftInstance,
																   I32F rightInstance,
																   I32F outputInstance)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_disableHandFixIk))
		return;

	const I32F copyOfLeftInstance = Max(Max(leftInstance, rightInstance), outputInstance) + 1;

	pAnimCmdList->AddCmd_EvaluateCopy(leftInstance, copyOfLeftInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void AnimSnapshotNodeHandFixIk::GenerateHandFixIkCommands_PostBlend(AnimCmdList* pAnimCmdList,
																	HandFixIkPluginCallbackArg* pArg,
																	I32F leftInstance,
																	I32F rightInstance,
																	I32F outputInstance)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_disableHandFixIk))
		return;

	const I32F copyOfLeftInstance = Max(Max(leftInstance, rightInstance), outputInstance) + 1;

	HandFixIkPluginData cmd;
	cmd.m_blendedInstance = outputInstance;
	cmd.m_baseInstance	  = copyOfLeftInstance;
	cmd.m_arg = pArg ? *pArg : HandFixIkPluginCallbackArg();

	pAnimCmdList->AddCmd_EvaluateAnimPhasePlugin(SID("hand-fix-ik"), &cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void AnimSnapshotNodeHandFixIk::GenerateHandFixIkCommands_PreEval(AnimCmdList* pAnimCmdList,
																  I32F baseInstance,
																  I32F requiredBreadth)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_disableHandFixIk))
		return;

	const I32F copyOfBaseInstance = baseInstance + requiredBreadth + 1;

	pAnimCmdList->AddCmd_EvaluateCopy(baseInstance, copyOfBaseInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void AnimSnapshotNodeHandFixIk::GenerateHandFixIkCommands_PostEval(AnimCmdList* pAnimCmdList,
																   I32F baseInstance,
																   I32F requiredBreadth,
																   HandFixIkPluginCallbackArg* pArg)
{
	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_disableHandFixIk))
		return;

	const I32F copyOfBaseInstance = baseInstance + requiredBreadth + 1;

	HandFixIkPluginData cmd;
	cmd.m_blendedInstance = baseInstance;
	cmd.m_baseInstance	  = copyOfBaseInstance;
	cmd.m_arg = pArg ? *pArg : HandFixIkPluginCallbackArg();

	pAnimCmdList->AddCmd_EvaluateAnimPhasePlugin(SID("hand-fix-ik"), &cmd);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ForceLinkAnimSnapshotNodeHandFixIk() {}
