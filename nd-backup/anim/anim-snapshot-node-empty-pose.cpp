/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-snapshot-node-empty-pose.h"

#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-node-library.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/scriptx/h/animation-script-types.h"

/// --------------------------------------------------------------------------------------------------------------- ///
ANIM_NODE_REGISTER(AnimSnapshotNodeEmptyPose, AnimSnapshotNode, SID("anim-node-empty-pose"));

FROM_ANIM_NODE_DEFINE(AnimSnapshotNodeEmptyPose);

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSnapshotNodeEmptyPose::AnimSnapshotNodeEmptyPose(StringId64 typeId,
													 StringId64 dcTypeId,
													 SnapshotNodeHeap::Index nodeIndex)
	: AnimSnapshotNode(typeId, dcTypeId, nodeIndex), m_fromGestureId(INVALID_STRING_ID_64)
{
	m_errorString[0] = '\0';
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeEmptyPose::SnapshotNode(AnimStateSnapshot* pSnapshot,
											 const DC::AnimNode* pNode,
											 const SnapshotAnimNodeTreeParams& params,
											 SnapshotAnimNodeTreeResults& results)
{
	if (!IsFinalBuild() && pNode->m_dcType == SID("anim-node-gesture"))
	{
		m_fromGestureId = static_cast<const DC::AnimNodeGesture*>(pNode)->m_gestureName;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeEmptyPose::GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
													 AnimCmdList* pAnimCmdList,
													 I32F outputInstance,
													 const AnimCmdGenInfo* pCmdGenInfo) const
{
	pAnimCmdList->AddCmd_EvaluateEmptyPose(outputInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ndanim::ValidBits AnimSnapshotNodeEmptyPose::GetValidBits(const ArtItemSkeleton* pSkel,
														  const AnimStateSnapshot* pSnapshot,
														  U32 iGroup) const
{
	return ndanim::ValidBits(0, 0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeEmptyPose::SetErrorString(const char* errorString)
{
	STRIP_IN_FINAL_BUILD;

	strncpy(m_errorString, errorString, kErrorStringBufLen);

	m_errorString[kErrorStringBufLen - 1] = '\0';
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeEmptyPose::DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const
{
	STRIP_IN_FINAL_BUILD;

	Tab(pData->m_output, pData->m_depth);

	if (m_errorString[0] != '\0')
	{
		SetColor(pData->m_output, 0xFFFF44FF);
		PrintTo(pData->m_output, "Empty Pose (%s)\n", m_errorString);
		SetColor(pData->m_output, kColorWhite);

		return;
	}

	SetColor(pData->m_output, 0xFFFF44FF);
	PrintTo(pData->m_output, "Empty Pose");
	if (m_fromGestureId != INVALID_STRING_ID_64)
	{
		PrintTo(pData->m_output, " [%s]", DevKitOnly_StringIdToString(m_fromGestureId));
	}
	PrintTo(pData->m_output, "\n");
	SetColor(pData->m_output, kColorWhite.ToAbgr8());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ForceLinkAnimSnapshotNodeEmptyPose()
{
}
