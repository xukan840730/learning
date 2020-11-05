/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-snapshot-node-unary.h"

#include "ndlib/anim/anim-node-library.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/scriptx/h/animation-script-types.h"

class AnimCmdList;
class ArtItemSkeleton;
struct AnimCameraCutInfo;
struct AnimCmdGenInfo;

ANIM_NODE_REGISTER(AnimSnapshotNodeUnary, AnimSnapshotNode, SID("anim-node-unary"));

FROM_ANIM_NODE_DEFINE(AnimSnapshotNodeUnary);

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeUnary::SnapshotNode(AnimStateSnapshot* pSnapshot,
										 const DC::AnimNode* pDcAnimNode,
										 const SnapshotAnimNodeTreeParams& params,
										 SnapshotAnimNodeTreeResults& results)
{
	const DC::AnimNodeUnary* pUnaryNode = static_cast<const DC::AnimNodeUnary*>(pDcAnimNode);
	const DC::AnimNode* pChildNode		= pUnaryNode->m_child;

	m_childIndex = AnimStateSnapshot::SnapshotAnimNodeTree(pSnapshot, pChildNode, params, results);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeUnary::RefreshAnims(AnimStateSnapshot* pSnapshot)
{
	AnimSnapshotNode* pChild = GetChild(pSnapshot);
	return pChild->RefreshAnims(pSnapshot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeUnary::RefreshPhasesAndBlends(AnimStateSnapshot* pSnapshot,
												   float statePhase,
												   bool topTrackInstance,
												   const DC::AnimInfoCollection* pInfoCollection)
{
	AnimSnapshotNode* pChild = GetChild(pSnapshot);
	return pChild->RefreshPhasesAndBlends(pSnapshot, statePhase, topTrackInstance, pInfoCollection);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U8 AnimSnapshotNodeUnary::RefreshBreadth(AnimStateSnapshot* pSnapshot)
{
	AnimSnapshotNode* pChild = GetChild(pSnapshot);
	return pChild->RefreshBreadth(pSnapshot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeUnary::StepNode(AnimStateSnapshot* pSnapshot, float deltaTime, const DC::AnimInfoCollection* pInfoCollection)
{
	AnimSnapshotNode* pChild = GetChild(pSnapshot);
	pChild->StepNode(pSnapshot, deltaTime, pInfoCollection);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeUnary::GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
												 AnimCmdList* pAnimCmdList,
												 I32F outputInstance,
												 const AnimCmdGenInfo* pCmdGenInfo) const
{
	const AnimSnapshotNode* pChild = GetChild(pSnapshot);
	pChild->GenerateAnimCommands(pSnapshot, pAnimCmdList, outputInstance, pCmdGenInfo);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeUnary::GenerateAnimCommands_PreBlend(const AnimStateSnapshot* pSnapshot,
														  AnimCmdList* pAnimCmdList,
														  const AnimCmdGenInfo* pCmdGenInfo,
														  const AnimSnapshotNodeBlend* pBlendNode,
														  bool leftNode,
														  I32F leftInstance,
														  I32F rightInstance,
														  I32F outputInstance) const
{
	if (const AnimSnapshotNode* pChild = GetChild(pSnapshot))
	{
		pChild->GenerateAnimCommands_PreBlend(pSnapshot,
											  pAnimCmdList,
											  pCmdGenInfo,
											  pBlendNode,
											  leftNode,
											  leftInstance,
											  rightInstance,
											  outputInstance);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeUnary::GenerateAnimCommands_PostBlend(const AnimStateSnapshot* pSnapshot,
														   AnimCmdList* pAnimCmdList,
														   const AnimCmdGenInfo* pCmdGenInfo,
														   const AnimSnapshotNodeBlend* pBlendNode,
														   bool leftNode,
														   I32F leftInstance,
														   I32F rightInstance,
														   I32F outputInstance) const
{
	if (const AnimSnapshotNode* pChild = GetChild(pSnapshot))
	{
		pChild->GenerateAnimCommands_PostBlend(pSnapshot,
											   pAnimCmdList,
											   pCmdGenInfo,
											   pBlendNode,
											   leftNode,
											   leftInstance,
											   rightInstance,
											   outputInstance);
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
ndanim::ValidBits AnimSnapshotNodeUnary::GetValidBits(const ArtItemSkeleton* pSkel,
													  const AnimStateSnapshot* pSnapshot,
													  U32 iGroup) const
{
	const AnimSnapshotNode* pChild = GetChild(pSnapshot);
	return pChild->GetValidBits(pSkel, pSnapshot, iGroup);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeUnary::IsAdditive(const AnimStateSnapshot* pSnapshot) const
{
	const AnimSnapshotNode* pChild = GetChild(pSnapshot);
	return pChild->IsAdditive(pSnapshot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeUnary::HasErrors(const AnimStateSnapshot* pSnapshot) const
{
	const AnimSnapshotNode* pChild = GetChild(pSnapshot);
	return pChild->HasErrors(pSnapshot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeUnary::HasLoopingAnimation(const AnimStateSnapshot* pSnapshot) const
{
	const AnimSnapshotNode* pChild = GetChild(pSnapshot);
	return pChild->HasLoopingAnimation(pSnapshot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeUnary::GetTriggeredEffects(const AnimStateSnapshot* pSnapshot,
												EffectUpdateStruct& effectParams,
												float nodeBlend,
												const AnimInstance* pInstance) const
{
	const AnimSnapshotNode* pChild = GetChild(pSnapshot);
	pChild->GetTriggeredEffects(pSnapshot, effectParams, nodeBlend, pInstance);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeUnary::EvaluateFloatChannel(const AnimStateSnapshot* pSnapshot,
												 float* pOutChannelFloat,
												 const SnapshotEvaluateParams& evaluateParams) const
{
	const AnimSnapshotNode* pChild = GetChild(pSnapshot);
	return pChild->EvaluateFloatChannel(pSnapshot, pOutChannelFloat, evaluateParams);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeUnary::EvaluateChannel(const AnimStateSnapshot* pSnapshot,
											ndanim::JointParams* pOutChannelJoint,
											const SnapshotEvaluateParams& evaluateParams) const
{
	const AnimSnapshotNode* pChild = GetChild(pSnapshot);
	return pChild->EvaluateChannel(pSnapshot, pOutChannelJoint, evaluateParams);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeUnary::DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const
{
	Tab(pData->m_output, pData->m_depth);
	SetColor(pData->m_output, 0xFFFF00FF);
	PrintTo(pData->m_output, "Unary Node\n");
	pData->m_depth++;
	const AnimSnapshotNode* pChild = GetChild(pSnapshot);
	pChild->DebugPrint(pSnapshot, pData);
	pData->m_depth--;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeUnary::DebugSubmitAnimPlayCount(const AnimStateSnapshot* pSnapshot) const
{
	const AnimSnapshotNode* pChild = GetChild(pSnapshot);
	pChild->DebugSubmitAnimPlayCount(pSnapshot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeUnary::CollectContributingAnims(const AnimStateSnapshot* pSnapshot,
													 float blend,
													 AnimCollection* pCollection) const
{
	const AnimSnapshotNode* pChild = GetChild(pSnapshot);
	pChild->CollectContributingAnims(pSnapshot, blend, pCollection);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeUnary::AllowPruning(const AnimStateSnapshot* pSnapshot) const
{
	const AnimSnapshotNode* pChild = GetChild(pSnapshot);
	return pChild->AllowPruning(pSnapshot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimSnapshotNode* AnimSnapshotNodeUnary::GetChild(const AnimStateSnapshot* pSnapshot) const
{
	return pSnapshot->GetSnapshotNode(m_childIndex);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSnapshotNode* AnimSnapshotNodeUnary::GetChild(AnimStateSnapshot* pSnapshot)
{
	return pSnapshot->GetSnapshotNode(m_childIndex);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeUnary::ReleaseNodeRecursive(SnapshotNodeHeap* pHeap) const
{
	pHeap->GetNodeByIndex(m_childIndex)->ReleaseNodeRecursive(pHeap);
	ParentClass::ReleaseNodeRecursive(pHeap);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SnapshotNodeHeap::Index AnimSnapshotNodeUnary::Clone(SnapshotNodeHeap* pDestHeap, const SnapshotNodeHeap* pSrcHeap) const
{
	SnapshotNodeHeap::Index clonedIndex = ParentClass::Clone(pDestHeap, pSrcHeap);
	AnimSnapshotNodeUnary* pClonedNode  = static_cast<AnimSnapshotNodeUnary*>(pDestHeap->GetNodeByIndex(clonedIndex));

	const AnimSnapshotNode* pChildNode = pSrcHeap->GetNodeByIndex(m_childIndex);
	pClonedNode->m_childIndex		   = pChildNode->Clone(pDestHeap, pSrcHeap);

	return clonedIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeUnary::EvaluateChannelDelta(const AnimStateSnapshot* pSnapshot,
												 ndanim::JointParams* pOutChannelJoint,
												 const SnapshotEvaluateParams& evaluateParams) const
{
	const AnimSnapshotNode* pChild = GetChild(pSnapshot);
	return pChild->EvaluateChannelDelta(pSnapshot, pOutChannelJoint, evaluateParams);
}

/// --------------------------------------------------------------------------------------------------------------- ///
IAnimDataEval::IAnimData* AnimSnapshotNodeUnary::EvaluateNode(IAnimDataEval* pEval, const AnimStateSnapshot* pSnapshot) const
{
	const AnimSnapshotNode* pChild = GetChild(pSnapshot);
	return pChild->EvaluateNode(pEval, pSnapshot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeUnary::GetHeapUsage(const SnapshotNodeHeap* pSrcHeap, U32& outMem, U32& outNumNodes) const
{
	ParentClass::GetHeapUsage(pSrcHeap, outMem, outNumNodes);
	pSrcHeap->GetNodeByIndex(m_childIndex)->GetHeapUsage(pSrcHeap, outMem, outNumNodes);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimSnapshotNode* AnimSnapshotNodeUnary::FindFirstNodeOfKind(const AnimStateSnapshot* pSnapshot, StringId64 typeId) const
{
	const AnimSnapshotNode* pResult = ParentClass::FindFirstNodeOfKind(pSnapshot, typeId);
	if (pResult)
	{
		return pResult;
	}
	const AnimSnapshotNode* pChild = GetChild(pSnapshot);
	return pChild->FindFirstNodeOfKind(pSnapshot, typeId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeUnary::VisitNodesOfKindInternal(AnimStateSnapshot* pSnapshot,
													 StringId64 typeId,
													 SnapshotVisitNodeFunc visitFunc,
													 AnimSnapshotNodeBlend* pParentBlendNode,
													 float combinedBlend,
													 uintptr_t userData)
{
	if (!ParentClass::VisitNodesOfKindInternal(pSnapshot, typeId, visitFunc, pParentBlendNode, combinedBlend, userData))
	{
		return false;
	}

	bool valid = true;

	if (AnimSnapshotNode* pChild = GetChild(pSnapshot))
	{
		valid = pChild->VisitNodesOfKindInternal(pSnapshot, typeId, visitFunc, pParentBlendNode, combinedBlend, userData);
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeUnary::VisitNodesOfKindInternal(const AnimStateSnapshot* pSnapshot,
													 StringId64 typeId,
													 SnapshotConstVisitNodeFunc visitFunc,
													 const AnimSnapshotNodeBlend* pParentBlendNode,
													 float combinedBlend,
													 uintptr_t userData) const
{
	if (!ParentClass::VisitNodesOfKindInternal(pSnapshot, typeId, visitFunc, pParentBlendNode, combinedBlend, userData))
	{
		return false;
	}

	bool valid = true;

	if (const AnimSnapshotNode* pChild = GetChild(pSnapshot))
	{
		valid = pChild->VisitNodesOfKindInternal(pSnapshot, typeId, visitFunc, pParentBlendNode, combinedBlend, userData);
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeUnary::ForAllAnimationsInternal(const AnimStateSnapshot* pSnapshot,
													 AnimationVisitFunc visitFunc,
													 float combinedBlend,
													 uintptr_t userData) const
{
	ParentClass::ForAllAnimationsInternal(pSnapshot, visitFunc, combinedBlend, userData);

	if (const AnimSnapshotNode* pChild = GetChild(pSnapshot))
	{
		pChild->ForAllAnimationsInternal(pSnapshot, visitFunc, combinedBlend, userData);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ForceLinkAnimSnapshotNodeUnary()
{
}
