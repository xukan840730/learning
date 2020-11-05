/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-snapshot-node-out-of-memory.h"

#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-state-snapshot.h"

/// --------------------------------------------------------------------------------------------------------------- ///
ANIM_NODE_REGISTER(AnimSnapshotNodeOutOfMemory, AnimSnapshotNode, SID("anim-node-out-of-memory"));

FROM_ANIM_NODE_DEFINE(AnimSnapshotNodeOutOfMemory);

AnimSnapshotNodeOutOfMemory AnimSnapshotNodeOutOfMemory::sm_singleton(SID("AnimSnapshotNodeOutOfMemory"),
																	  SID("anim-node-out-of-memory"),
																	  SnapshotNodeHeap::kOutOfMemoryIndex);

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeOutOfMemory::SnapshotNode(AnimStateSnapshot* pSnapshot,
											   const DC::AnimNode* pNode,
											   const SnapshotAnimNodeTreeParams& params,
											   SnapshotAnimNodeTreeResults& results)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeOutOfMemory::RefreshAnims(AnimStateSnapshot* pSnapshot)
{
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeOutOfMemory::StepNode(AnimStateSnapshot* pSnapshot, float deltaTime, const DC::AnimInfoCollection* pInfoCollection)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeOutOfMemory::RefreshPhasesAndBlends(AnimStateSnapshot* pSnapshot,
														 float statePhase,
														 bool topTrackInstance,
														 const DC::AnimInfoCollection* pInfoCollection)
{
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeOutOfMemory::GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
													   AnimCmdList* pAnimCmdList,
													   I32F outputInstance,
													   const AnimCmdGenInfo* pCmdGenInfo) const
{
	pAnimCmdList->AddCmd_EvaluateBindPose(outputInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeOutOfMemory::ReleaseNodeRecursive(SnapshotNodeHeap* pHeap) const
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
ndanim::ValidBits AnimSnapshotNodeOutOfMemory::GetValidBits(const ArtItemSkeleton* pSkel,
															const AnimStateSnapshot* pSnapshot,
															U32 iGroup) const
{
	return ndanim::ValidBits(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeOutOfMemory::IsAdditive(const AnimStateSnapshot* pSnapshot) const
{
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeOutOfMemory::HasLoopingAnimation(const AnimStateSnapshot* pSnapshot) const
{
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeOutOfMemory::GetTriggeredEffects(const AnimStateSnapshot* pSnapshot,
													  EffectUpdateStruct& effectParams,
													  float nodeBlend,
													  const AnimInstance* pInstance) const
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeOutOfMemory::DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const
{
	STRIP_IN_FINAL_BUILD;

	Tab(pData->m_output, pData->m_depth);
	SetColor(pData->m_output, kColorRed.ToAbgr8());
	PrintTo(pData->m_output, "OUT OF SNAPSHOT NODE MEMORY\n");
	SetColor(pData->m_output, kColorWhite.ToAbgr8());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeOutOfMemory::DebugSubmitAnimPlayCount(const AnimStateSnapshot* pSnapshot) const
{
}
