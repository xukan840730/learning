/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-snapshot-node-instance-zero.h"

#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/anim/anim-state.h"

/// --------------------------------------------------------------------------------------------------------------- ///
ANIM_NODE_REGISTER(AnimSnapshotNodeInstanceZero, AnimSnapshotNode, SID("anim-node-instance-0"));

FROM_ANIM_NODE_DEFINE(AnimSnapshotNodeInstanceZero);

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeInstanceZero::SnapshotNode(AnimStateSnapshot* pSnapshot,
												const DC::AnimNode* pNode,
												const SnapshotAnimNodeTreeParams& params,
												SnapshotAnimNodeTreeResults& results)
{
	return;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeInstanceZero::GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
														AnimCmdList* pAnimCmdList,
														I32F outputInstance,
														const AnimCmdGenInfo* pCmdGenInfo) const
{
	if (pCmdGenInfo->m_pContext->m_instanceZeroIsValid)
	{
		if (outputInstance != 0)
		{
			pAnimCmdList->AddCmd_EvaluateCopy(0, outputInstance);

			if (pSnapshot->m_flags.m_isFlipped)
			{
				pAnimCmdList->AddCmd_EvaluateFlip(outputInstance);
			}
		}
		else
		{
			// valid instance zero that's also our output so... do nothing
		}
	}
	else 
	{
		pAnimCmdList->AddCmd_EvaluateEmptyPose(outputInstance);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeInstanceZero::DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const
{
	STRIP_IN_FINAL_BUILD;

	if (g_animOptions.m_debugPrint.m_simplified)
		return;

	Tab(pData->m_output, pData->m_depth);
	SetColor(pData->m_output, 0xFFFF00FF);
	PrintTo(pData->m_output, "Instance 0\n");
	SetColor(pData->m_output, 0xFFFFFFFF);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ForceLinkAnimSnapshotNodeInstanceZero() {}
