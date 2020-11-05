/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-snapshot-node-blend.h"

#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-node-hand-fix-ik.h"
#include "ndlib/anim/anim-node-joint-limits.h"
#include "ndlib/anim/anim-node-leg-fix-ik.h"
#include "ndlib/anim/anim-node-library.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/anim/anim-state.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/feather-blend-table.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/process/process.h"
#include "ndlib/script/script-manager.h"

#include "gamelib/gameplay/nd-game-object.h"

/// --------------------------------------------------------------------------------------------------------------- ///
SnapshotNodeHeap::Index SnapshotAnimNodeBlend(AnimStateSnapshot* pSnapshot,
											  const DC::AnimNode* pDcAnimNode,
											  const SnapshotAnimNodeTreeParams& params,
											  SnapshotAnimNodeTreeResults& results);

ANIM_NODE_REGISTER_WITH_SNAPSHOT_FUNC(AnimSnapshotNodeBlend,
									  AnimSnapshotNode,
									  SID("anim-node-blend"),
									  SnapshotAnimNodeBlend);

FROM_ANIM_NODE_DEFINE(AnimSnapshotNodeBlend);

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSnapshotNodeBlend::AnimSnapshotNodeBlend(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex)
	: AnimSnapshotNode(typeId, dcTypeId, nodeIndex)
	, m_customLimitsId(INVALID_STRING_ID_64)
	, m_leftIndex(SnapshotNodeHeap::kOutOfMemoryIndex)
	, m_rightIndex(SnapshotNodeHeap::kOutOfMemoryIndex)
	, m_flags(0)
	, m_blendMode(ndanim::kBlendSlerp)
	, m_staticBlend(0)
	, m_blendFunc(nullptr)
	, m_blendFactor(0.0f)
	, m_externalBlendFactor(1.0f)
	, m_featherBlendIndex(-1)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeBlend::SnapshotNode(AnimStateSnapshot* pSnapshot,
										 const DC::AnimNode* pDcAnimNode,
										 const SnapshotAnimNodeTreeParams& params,
										 SnapshotAnimNodeTreeResults& results)
{
	const DC::AnimNodeBlend* pBlend = static_cast<const DC::AnimNodeBlend*>(pDcAnimNode);
	const bool noOverlayTransform   = pBlend->m_flags & DC::kAnimNodeBlendFlagNoOverlayTransform;

	SnapshotAnimNodeTreeParams blendChildParams = params;

	if (noOverlayTransform)
	{
		blendChildParams.m_pConstOverlaySnapshot = blendChildParams.m_pMutableOverlaySnapshot = nullptr;
	}

	m_leftIndex	  = -1;
	m_rightIndex  = -1;
	m_flags		  = pBlend->m_flags;
	m_staticBlend = pBlend->m_staticBlend != 0;
	m_blendFunc	  = pBlend->m_blendFunc;
	m_blendFactor = pBlend->m_blendFactor;

	m_externalBlendFactor = 1.0f;

	m_featherBlendIndex = g_featherBlendTable.LoginFeatherBlend(pBlend->m_featherBlend, params.m_pAnimData);

	const AnimSnapshotNodeBlend* pSavedParent = params.m_pParentBlendNode;
	params.m_pParentBlendNode = this;

	m_leftIndex	 = AnimStateSnapshot::SnapshotAnimNodeTree(pSnapshot, pBlend->m_left, blendChildParams, results);
	m_rightIndex = AnimStateSnapshot::SnapshotAnimNodeTree(pSnapshot, pBlend->m_right, blendChildParams, results);

	if (AnimSnapshotNode* pLeftNode = pSnapshot->GetSnapshotNode(m_leftIndex))
	{
		pLeftNode->OnAddedToBlendNode(params, pSnapshot, this, true);
	}

	if (AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex))
	{
		pRightNode->OnAddedToBlendNode(params, pSnapshot, this, false);
	}

	params.m_pParentBlendNode = pSavedParent;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeBlend::RefreshAnims(AnimStateSnapshot* pSnapshot)
{
	bool success = true;

	AnimSnapshotNode* pLeftNode = pSnapshot->GetSnapshotNode(m_leftIndex);
	success &= pLeftNode->RefreshAnims(pSnapshot);

	AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);
	success &= pRightNode->RefreshAnims(pSnapshot);

	m_blendMode = GetBlendMode(pSnapshot);
	if (0 != (m_flags & DC::kAnimNodeBlendFlagForceSlerp))
	{
		m_blendMode = ndanim::kBlendSlerp;
	}

	return success;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeBlend::RefreshPhasesAndBlends(AnimStateSnapshot* pSnapshot,
												   float statePhase,
												   bool topTrackInstance,
												   const DC::AnimInfoCollection* pInfoCollection)
{
	const bool liveForTopInstanceOnly = m_flags & DC::kAnimNodeBlendFlagLiveForTopInstanceOnly;
	const bool shouldUpdate			  = (liveForTopInstanceOnly ? topTrackInstance : true);

	AnimSnapshotNode* pLeftNode  = pSnapshot->GetSnapshotNode(m_leftIndex);
	AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);

	if (shouldUpdate)
	{
		float blendFactor = m_blendFactor;
		if (m_blendFunc)
		{
			// We need to be able to assert that the lambda is not larger than this... We need the size of the lambda
			DC::AnimInfoCollection scriptInfoCollection(*pInfoCollection);
			DC::AnimStateSnapshotInfo snapshotInfo;

			ScriptValue argv[6];
			GetAnimNodeBlendFuncArgs(argv,
									 ARRAY_COUNT(argv),
									 &scriptInfoCollection,
									 &snapshotInfo,
									 statePhase,
									 pSnapshot->IsFlipped(),
									 pSnapshot,
									 Process::GetContextProcess(),
									 this);

			const DC::ScriptLambda* pLambda = m_blendFunc;
			VALIDATE_LAMBDA(pLambda, "anim-node-blend-func", pSnapshot->m_animState.m_name.m_symbol);
			blendFactor = ScriptManager::Eval(pLambda, SID("anim-node-blend-func"), ARRAY_COUNT(argv), argv).m_float;
		}

		m_blendFactor = Limit01(blendFactor);
	}

	const float blendFactorToUse = GetEffectiveBlend();

	if (IsClose(blendFactorToUse, 0.0f, 0.0001f))
	{
		pLeftNode->RefreshPhasesAndBlends(pSnapshot, statePhase, topTrackInstance, pInfoCollection);
	}
	else
	{
		pLeftNode->RefreshPhasesAndBlends(pSnapshot, statePhase, topTrackInstance, pInfoCollection);
		pRightNode->RefreshPhasesAndBlends(pSnapshot, statePhase, topTrackInstance, pInfoCollection);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U8 AnimSnapshotNodeBlend::RefreshBreadth(AnimStateSnapshot* pSnapshot)
{
	AnimSnapshotNode* pLeftNode  = pSnapshot->GetSnapshotNode(m_leftIndex);
	AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);

	ANIM_ASSERT(pLeftNode);
	ANIM_ASSERT(pRightNode);

	const U8 leftBreadth  = pLeftNode->RefreshBreadth(pSnapshot);
	const U8 rightBreadth = pRightNode->RefreshBreadth(pSnapshot);

	U8 extraInstancesNeeded = 1;

	if (m_flags
		& (DC::kAnimNodeBlendFlagLegFixIk | DC::kAnimNodeBlendFlagHandFixIkLeft | DC::kAnimNodeBlendFlagHandFixIkRight))
	{
		extraInstancesNeeded++;
	}

	U8 breadth = 0;

	// The right side is broader... let's animate this first
	if (leftBreadth < rightBreadth)
	{
		breadth = extraInstancesNeeded + leftBreadth;
		if (breadth < rightBreadth)
			breadth = rightBreadth;

		m_flags |= DC::kAnimNodeBlendFlagRightBalanced;
	}
	else
	{
		breadth = extraInstancesNeeded + rightBreadth;
		if (breadth < leftBreadth)
			breadth = leftBreadth;

		m_flags &= ~DC::kAnimNodeBlendFlagRightBalanced;
	}

	return breadth;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeBlend::StepNode(AnimStateSnapshot* pSnapshot,
									 float deltaTime,
									 const DC::AnimInfoCollection* pInfoCollection)
{
	AnimSnapshotNode* pLeftNode = pSnapshot->GetSnapshotNode(m_leftIndex);
	pLeftNode->StepNode(pSnapshot, deltaTime, pInfoCollection);

	AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);
	pRightNode->StepNode(pSnapshot, deltaTime, pInfoCollection);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeBlend::GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
												 AnimCmdList* pAnimCmdList,
												 I32F outputInstance,
												 const AnimCmdGenInfo* pCmdGenInfo) const
{
	float nodeBlendToUse = GetEffectiveBlend();
	const ndanim::BlendMode blendMode = m_blendMode;
	const bool isAdditiveBlend = (blendMode == ndanim::kBlendAdditive || blendMode == ndanim::kBlendAddToAdditive);

	// Allow us to disable additive blends.
	if (g_animOptions.m_disableAdditiveAnims && isAdditiveBlend)
	{
		nodeBlendToUse = 0.0f;
	}

	// Let's find out if the right side of the tree is defining all
	const AnimSnapshotNode* pLeftNode  = pSnapshot->GetSnapshotNode(m_leftIndex);
	const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);

	ANIM_ASSERT(pLeftNode);
	ANIM_ASSERT(pRightNode);

	I32F leftInstance  = -1;
	I32F rightInstance = -1;
	bool doNormalBlend = false;

	if (m_flags & DC::kAnimNodeBlendFlagCustomBlendOp)
	{
		pRightNode->GenerateAnimCommands_CustomBlend(this,
													 pLeftNode,
													 nodeBlendToUse,
													 pSnapshot,
													 pAnimCmdList,
													 outputInstance,
													 pCmdGenInfo);
	}
	else if (m_flags & DC::kAnimNodeBlendFlagRightBalanced)
	{
		if (pCmdGenInfo->m_allowTreePruning && IsClose(nodeBlendToUse, 0.0f, 0.0001f))
		{
			pLeftNode->GenerateAnimCommands(pSnapshot, pAnimCmdList, outputInstance, pCmdGenInfo);
		}
		else
		{
			leftInstance  = outputInstance + 1;
			rightInstance = outputInstance;

			pRightNode->GenerateAnimCommands(pSnapshot, pAnimCmdList, rightInstance, pCmdGenInfo);
			pLeftNode->GenerateAnimCommands(pSnapshot, pAnimCmdList, leftInstance, pCmdGenInfo);

			doNormalBlend = true;
		}
	}
	else
	{
		// if the node blend is 1.0 in this right balanced tree we will not be blending in anything
		// from the left side of the tree, hence we prune it.
		if (pCmdGenInfo->m_allowTreePruning && IsClose(nodeBlendToUse, 0.0f, 0.0001f))
		{
			pLeftNode->GenerateAnimCommands(pSnapshot, pAnimCmdList, outputInstance, pCmdGenInfo);
		}
		else
		{
			leftInstance  = outputInstance;
			rightInstance = outputInstance + 1;

			pLeftNode->GenerateAnimCommands(pSnapshot, pAnimCmdList, leftInstance, pCmdGenInfo);
			pRightNode->GenerateAnimCommands(pSnapshot, pAnimCmdList, rightInstance, pCmdGenInfo);

			doNormalBlend = true;
		}
	}

	if (doNormalBlend)
	{
		pLeftNode->GenerateAnimCommands_PreBlend(pSnapshot,
												 pAnimCmdList,
												 pCmdGenInfo,
												 this,
												 true,
												 leftInstance,
												 rightInstance,
												 outputInstance);

		pRightNode->GenerateAnimCommands_PreBlend(pSnapshot,
												  pAnimCmdList,
												  pCmdGenInfo,
												  this,
												  false,
												  leftInstance,
												  rightInstance,
												  outputInstance);

		if (m_flags & DC::kAnimNodeBlendFlagLegFixIk)
		{
			AnimSnapshotNodeLegFixIk::GenerateLegFixIkCommands_PreBlend(pAnimCmdList,
																		leftInstance,
																		rightInstance,
																		outputInstance);
		}

		const bool wantHandFixIk = m_flags & (DC::kAnimNodeBlendFlagHandFixIkLeft | DC::kAnimNodeBlendFlagHandFixIkRight);

		if (wantHandFixIk)
		{
			AnimSnapshotNodeHandFixIk::GenerateHandFixIkCommands_PreBlend(pAnimCmdList,
																		  leftInstance,
																		  rightInstance,
																		  outputInstance);
		}

		const OrbisAnim::ChannelFactor* const* ppChannelFactors = nullptr;
		const U32* pNumChannelFactors							= nullptr;

		if (m_featherBlendIndex >= 0)
		{
			const ArtItemSkeleton* pSkel = pCmdGenInfo->m_pContext->m_pAnimateSkel;
			nodeBlendToUse = g_featherBlendTable.CreateChannelFactorsForAnimCmd(pSkel,
																				m_featherBlendIndex,
																				nodeBlendToUse,
																				&ppChannelFactors,
																				&pNumChannelFactors);
		}

		const bool doAdditiveModeFlip = isAdditiveBlend && pRightNode->IsFlipped()
										&& pRightNode->ShouldHandleFlipInBlend();

		if (doAdditiveModeFlip)
		{
			const U32F tempInstance = Max(Max(leftInstance, rightInstance), outputInstance) + 1;

			pAnimCmdList->AddCmd_EvaluateCopy(leftInstance, tempInstance);
			pAnimCmdList->AddCmd_EvaluateFlip(tempInstance);

			if (ppChannelFactors)
			{
				pAnimCmdList->AddCmd_EvaluateFeatherBlend(tempInstance,
														  rightInstance,
														  tempInstance,
														  blendMode,
														  nodeBlendToUse,
														  ppChannelFactors,
														  pNumChannelFactors,
														  m_featherBlendIndex);
			}
			else
			{
				pAnimCmdList->AddCmd_EvaluateBlend(tempInstance, rightInstance, tempInstance, blendMode, nodeBlendToUse);
			}

			pAnimCmdList->AddCmd_EvaluateFlip(tempInstance);
			pAnimCmdList->AddCmd_EvaluateCopy(tempInstance, outputInstance);
		}
		else
		{
			if (ppChannelFactors)
			{
				pAnimCmdList->AddCmd_EvaluateFeatherBlend(leftInstance,
														  rightInstance,
														  outputInstance,
														  blendMode,
														  nodeBlendToUse,
														  ppChannelFactors,
														  pNumChannelFactors,
														  m_featherBlendIndex);
			}
			else
			{
				pAnimCmdList->AddCmd_EvaluateBlend(leftInstance,
												   rightInstance,
												   outputInstance,
												   blendMode,
												   nodeBlendToUse);
			}
		}

		pLeftNode->GenerateAnimCommands_PostBlend(pSnapshot,
												  pAnimCmdList,
												  pCmdGenInfo,
												  this,
												  true,
												  leftInstance,
												  rightInstance,
												  outputInstance);

		pRightNode->GenerateAnimCommands_PostBlend(pSnapshot,
												   pAnimCmdList,
												   pCmdGenInfo,
												   this,
												   false,
												   leftInstance,
												   rightInstance,
												   outputInstance);

		if (m_flags & DC::kAnimNodeBlendFlagLegFixIk)
		{
			AnimSnapshotNodeLegFixIk::GenerateLegFixIkCommands_PostBlend(pAnimCmdList,
																		 nullptr,
																		 leftInstance,
																		 rightInstance,
																		 outputInstance);
		}

		if (wantHandFixIk)
		{
			HandFixIkPluginCallbackArg arg;
			arg.m_handsToIk[kLeftArm]  = m_flags & DC::kAnimNodeBlendFlagHandFixIkLeft;
			arg.m_handsToIk[kRightArm] = m_flags & DC::kAnimNodeBlendFlagHandFixIkRight;

			AnimSnapshotNodeHandFixIk::GenerateHandFixIkCommands_PostBlend(pAnimCmdList,
																		   &arg,
																		   leftInstance,
																		   rightInstance,
																		   outputInstance);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeBlend::CollectContributingAnims(const AnimStateSnapshot* pSnapshot,
													 float blend,
													 AnimCollection* pCollection) const
{
	const AnimSnapshotNode* pLeftNode  = pSnapshot->GetSnapshotNode(m_leftIndex);
	const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);

	const float nodeBlendToUse = GetEffectiveBlend();

	float newBlendLeft = (1.0f - nodeBlendToUse) * blend;

	if (m_blendMode == ndanim::kBlendAdditive)
	{
		newBlendLeft = blend;
	}
	else
	{
		// Let's find out if the right side of the tree is defining all
		const ArtItemSkeleton* pSkel			= ResourceTable::LookupSkel(pSnapshot->m_translatedPhaseSkelId).ToArtItem();
		const ndanim::ValidBits leftValidBits	= pLeftNode->GetValidBits(pSkel, pSnapshot, 0);
		const ndanim::ValidBits rightValidBits  = pRightNode->GetValidBits(pSkel, pSnapshot, 0);
		const ndanim::ValidBits tempValidBits	= leftValidBits ^ rightValidBits;
		const bool rightBitsFullyDefinesLeftBits = (leftValidBits & tempValidBits).IsEmpty();

		if (!rightBitsFullyDefinesLeftBits)
			newBlendLeft = blend;
	}

	pLeftNode->CollectContributingAnims(pSnapshot, newBlendLeft, pCollection);

	const float newBlendRight = nodeBlendToUse * blend;
	pRightNode->CollectContributingAnims(pSnapshot, newBlendRight, pCollection);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeBlend::ReleaseNodeRecursive(SnapshotNodeHeap* pHeap) const
{
	AnimSnapshotNode* pLeftNode  = pHeap->GetNodeByIndex(m_leftIndex);
	AnimSnapshotNode* pRightNode = pHeap->GetNodeByIndex(m_rightIndex);

	pLeftNode->ReleaseNodeRecursive(pHeap);
	pRightNode->ReleaseNodeRecursive(pHeap);

	ParentClass::ReleaseNodeRecursive(pHeap);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ndanim::ValidBits AnimSnapshotNodeBlend::GetValidBits(const ArtItemSkeleton* pSkel,
													  const AnimStateSnapshot* pSnapshot,
													  U32 iGroup) const
{
	const AnimSnapshotNode* pLeftNode  = pSnapshot->GetSnapshotNode(m_leftIndex);
	const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);

	// If only the right node is additive, it cannot add valid bits to partial anims so only left node valid bits
	// contribute
	if (!pLeftNode->IsAdditive(pSnapshot) && pRightNode->IsAdditive(pSnapshot))
	{
		return pLeftNode->GetValidBits(pSkel, pSnapshot, iGroup);
	}
	const ndanim::ValidBits leftBits  = pLeftNode->GetValidBits(pSkel, pSnapshot, iGroup);
	const ndanim::ValidBits rightBits = pRightNode->GetValidBits(pSkel, pSnapshot, iGroup);

	const ndanim::ValidBits mergedBits = leftBits | rightBits;
	return mergedBits;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeBlend::IsAdditive(const AnimStateSnapshot* pSnapshot) const
{
	bool additive = false;

	const AnimSnapshotNode* pLeftNode  = pSnapshot->GetSnapshotNode(m_leftIndex);
	const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);
	const bool leftAdditive			   = pLeftNode && pLeftNode->IsAdditive(pSnapshot);
	const bool rightAdditive		   = pRightNode && pRightNode->IsAdditive(pSnapshot);

	if (rightAdditive && leftAdditive)
	{
		additive = true;
	}
#ifdef ANIM_DEBUG
	else if (!rightAdditive && leftAdditive)
	{
		MsgAnim("Trying to blend an additive and non additive in the wrong order\n");
	}
#endif

	return additive;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeBlend::HasErrors(const AnimStateSnapshot* pSnapshot) const
{
	const AnimSnapshotNode* pLeftNode  = pSnapshot->GetSnapshotNode(m_leftIndex);
	const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);

	const bool leftErrors = pLeftNode ? pLeftNode->HasErrors(pSnapshot) : true;
	const bool rightErrors = pRightNode ? pRightNode->HasErrors(pSnapshot) : true;
	
	return leftErrors || rightErrors;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeBlend::IsAdditiveBlend(const AnimStateSnapshot* pSnapshot) const
{
	bool isAddBlend = false;
	const ndanim::BlendMode blendMode = GetBlendMode(pSnapshot);

	switch (blendMode)
	{
	case ndanim::kBlendAdditive:
	case ndanim::kBlendAddToAdditive:
		isAddBlend = true;
		break;
	}

	return isAddBlend;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ndanim::BlendMode AnimSnapshotNodeBlend::GetBlendMode(const AnimStateSnapshot* pSnapshot) const
{
	ndanim::BlendMode blendMode = ndanim::kBlendSlerp;

	const AnimSnapshotNode* pLeftNode  = pSnapshot->GetSnapshotNode(m_leftIndex);
	const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);
	const bool leftAdditive			   = pLeftNode->IsAdditive(pSnapshot);
	const bool rightAdditive		   = pRightNode->IsAdditive(pSnapshot);

	if (rightAdditive && leftAdditive)
	{
		blendMode = ndanim::kBlendAddToAdditive;
	}
	else if (rightAdditive && !leftAdditive)
	{
		blendMode = ndanim::kBlendAdditive;
	}
#ifdef ANIM_DEBUG
	else if (!rightAdditive && leftAdditive)
	{
		MsgAnim("Trying to blend an additive and non additive in the wrong order\n");
	}
#endif

	return blendMode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeBlend::HasLoopingAnimation(const AnimStateSnapshot* pSnapshot) const
{
	bool hasLoopingAnim = false;

	const AnimSnapshotNode* pLeftNode  = pSnapshot->GetSnapshotNode(m_leftIndex);
	const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);

	hasLoopingAnim = hasLoopingAnim || pLeftNode->HasLoopingAnimation(pSnapshot);
	hasLoopingAnim = hasLoopingAnim || pRightNode->HasLoopingAnimation(pSnapshot);

	return hasLoopingAnim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeBlend::GetTriggeredEffects(const AnimStateSnapshot* pSnapshot,
												EffectUpdateStruct& effectParams,
												float nodeBlend,
												const AnimInstance* pInstance) const
{
	const float nodeBlendToUse = GetEffectiveBlend();

	// Only play effects from the right animation if it actually is contributing to the final animation.
	// We treat additive and blended animations the same. A more correct way would be to check blend type as
	// well as the valid bits to determine if the left animation is still contributing even with a blend of 1.0
	if (IsClose(nodeBlendToUse, 0.0f, 0.0001f))
	{
		const AnimSnapshotNode* pLeftNode = pSnapshot->GetSnapshotNode(m_leftIndex);
		pLeftNode->GetTriggeredEffects(pSnapshot, effectParams, nodeBlend, pInstance);
	}
	else
	{
		float leftBlend = 1.0f - nodeBlendToUse;
		if (GetBlendMode(pSnapshot) == ndanim::kBlendAdditive)
			leftBlend = 1.0f;

		const AnimSnapshotNode* pLeftNode = pSnapshot->GetSnapshotNode(m_leftIndex);
		pLeftNode->GetTriggeredEffects(pSnapshot, effectParams, nodeBlend * leftBlend, pInstance);

		const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);
		pRightNode->GetTriggeredEffects(pSnapshot, effectParams, nodeBlend * nodeBlendToUse, pInstance);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeBlend::EvaluateFloatChannel(const AnimStateSnapshot* pSnapshot,
												 float* pOutChannelFloat,
												 const SnapshotEvaluateParams& evaluateParams) const
{
	const float nodeBlendToUse = GetEffectiveBlend();

	const AnimSnapshotNode* pLeftNode = pSnapshot->GetSnapshotNode(m_leftIndex);
	float leftChannelFloat;
	const bool leftNodeEvaluatedChannel = pLeftNode->EvaluateFloatChannel(pSnapshot,
																		  &leftChannelFloat,
																		  evaluateParams);

	const bool blendChannels = evaluateParams.m_blendChannels;
	float rightChannelFloat	 = 0.0f;

	bool evaluateBothNodes = blendChannels;
	if (m_flags & DC::kAnimNodeBlendFlagEvaluateBothNodes)
		evaluateBothNodes = true;
	if (m_flags & DC::kAnimNodeBlendFlagEvaluateLeftNodeOnly)
		evaluateBothNodes = false;

	evaluateBothNodes = evaluateBothNodes || evaluateParams.m_forceChannelBlending;

	const bool isBlendCloseToZero  = IsClose(nodeBlendToUse, 0.0f, 0.0001f);
	bool rightNodeEvaluatedChannel = false;

	if (!isBlendCloseToZero && evaluateBothNodes)
	{
		const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);

		rightNodeEvaluatedChannel = pRightNode->EvaluateFloatChannel(pSnapshot,
																	 &rightChannelFloat,
																	 evaluateParams);
	}

	bool channelEvaluated = false;

	// Only blend if the right node was evaluated and it is contributing
	if (rightNodeEvaluatedChannel && !isBlendCloseToZero)
	{
		if (leftNodeEvaluatedChannel)
		{
			*pOutChannelFloat = Lerp(leftChannelFloat, rightChannelFloat, nodeBlendToUse);
		}
		else
		{
			*pOutChannelFloat = rightChannelFloat;
		}

		channelEvaluated = true;
	}
	else if (leftNodeEvaluatedChannel)
	{
		// only use the align movement from the left child.
		*pOutChannelFloat = leftChannelFloat;
		channelEvaluated  = true;
	}

	return channelEvaluated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeBlend::EvaluateChannel(const AnimStateSnapshot* pSnapshot,
											ndanim::JointParams* pOutChannelJoint,
											const SnapshotEvaluateParams& evaluateParams) const
{
	const float nodeBlendToUse = GetEffectiveBlend();

	const AnimSnapshotNode* pLeftNode = pSnapshot->GetSnapshotNode(m_leftIndex);
	ndanim::JointParams leftChannelJoint;

	const bool leftNodeEvaluatedChannel = pLeftNode->EvaluateChannel(pSnapshot,
																	 &leftChannelJoint,
																	 evaluateParams);

	const bool blendChannels = evaluateParams.m_blendChannels;
	
	ndanim::JointParams rightChannelJoint;

	bool evaluateBothNodes = blendChannels;
	if (m_flags & DC::kAnimNodeBlendFlagEvaluateBothNodes)
		evaluateBothNodes = true;
	if (m_flags & DC::kAnimNodeBlendFlagEvaluateLeftNodeOnly)
		evaluateBothNodes = false;

	evaluateBothNodes = evaluateBothNodes || evaluateParams.m_forceChannelBlending;

	const bool isBlendCloseToZero  = IsClose(nodeBlendToUse, 0.0f, 0.0001f);
	bool rightNodeEvaluatedChannel = false;

	if (!isBlendCloseToZero && evaluateBothNodes)
	{
		const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);

		rightNodeEvaluatedChannel = pRightNode->EvaluateChannel(pSnapshot,
																&rightChannelJoint,
																evaluateParams);
	}

	bool channelEvaluated = false;

	// Only blend if the right node was evaluated and it is contributing
	if (rightNodeEvaluatedChannel && !isBlendCloseToZero)
	{
		if (leftNodeEvaluatedChannel)
		{
			// blend the channel
			if (pSnapshot->m_animState.m_flags & DC::kAnimStateFlagRadialChannelBlending)
			{
				AnimChannelJointBlendRadial(pOutChannelJoint, leftChannelJoint, rightChannelJoint, nodeBlendToUse);
			}
			else
			{
				AnimChannelJointBlend(pOutChannelJoint, leftChannelJoint, rightChannelJoint, nodeBlendToUse);
			}
		}
		else
		{
			*pOutChannelJoint = rightChannelJoint;
		}

		channelEvaluated = true;
	}
	else if (leftNodeEvaluatedChannel)
	{
		// only use the align movement from the left child.
		*pOutChannelJoint = leftChannelJoint;
		channelEvaluated  = true;
	}

	return channelEvaluated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeBlend::EvaluateChannelDelta(const AnimStateSnapshot* pSnapshot,
												 ndanim::JointParams* pOutChannelJoint,
												 const SnapshotEvaluateParams& evaluateParams) const
{
	const float nodeBlendToUse = GetEffectiveBlend();

	const AnimSnapshotNode* pLeftNode = pSnapshot->GetSnapshotNode(m_leftIndex);
	ndanim::JointParams leftChannelJoint;

	const bool leftNodeEvaluatedChannel = pLeftNode->EvaluateChannelDelta(pSnapshot,
																		  &leftChannelJoint,
																		  evaluateParams);

	const bool blendChannels = evaluateParams.m_blendChannels;

	ndanim::JointParams rightChannelJoint;

	bool evaluateBothNodes = blendChannels;
	if (m_flags & DC::kAnimNodeBlendFlagEvaluateBothNodes)
		evaluateBothNodes = true;
	if (m_flags & DC::kAnimNodeBlendFlagEvaluateLeftNodeOnly)
		evaluateBothNodes = false;

	evaluateBothNodes = evaluateBothNodes || evaluateParams.m_forceChannelBlending;

	const bool isBlendCloseToZero  = IsClose(nodeBlendToUse, 0.0f, 0.0001f);
	bool rightNodeEvaluatedChannel = false;

	if (!isBlendCloseToZero && evaluateBothNodes)
	{
		const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);

		rightNodeEvaluatedChannel = pRightNode->EvaluateChannelDelta(pSnapshot,
																	 &rightChannelJoint,
																	 evaluateParams);
	}

	bool channelEvaluated = false;

	// Only blend if the right node was evaluated and it is contributing
	if (rightNodeEvaluatedChannel && !isBlendCloseToZero)
	{
		if (leftNodeEvaluatedChannel)
		{
			// blend the channel
			if (pSnapshot->m_animState.m_flags & DC::kAnimStateFlagRadialChannelBlending)
			{
				AnimChannelJointBlendRadial(pOutChannelJoint, leftChannelJoint, rightChannelJoint, nodeBlendToUse);
			}
			else
			{
				AnimChannelJointBlend(pOutChannelJoint, leftChannelJoint, rightChannelJoint, nodeBlendToUse);
			}
		}
		else
		{
			*pOutChannelJoint = rightChannelJoint;
		}

		channelEvaluated = true;
	}
	else if (leftNodeEvaluatedChannel)
	{
		// only use the align movement from the left child.
		*pOutChannelJoint = leftChannelJoint;
		channelEvaluated  = true;
	}

	return channelEvaluated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeBlend::DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const
{
	STRIP_IN_FINAL_BUILD;

	const char* type = 0;
	bool isAdditive  = false;

	switch (m_blendMode)
	{
	case ndanim::kBlendSlerp:
		type = "blend";
		break;
	case ndanim::kBlendAdditive:
		type	   = "additive";
		isAdditive = true;
		break;
	case ndanim::kBlendAddToAdditive:
		type	   = "add to additive";
		isAdditive = true;
		break;
	default:
		type = "unknown";
		break;
	}

	const float nodeBlendToUse = GetEffectiveBlend();

	U8 componentValue = static_cast<U8>(200.0f * pData->m_parentBlendValue);

	if (!isAdditive || !g_animOptions.m_debugPrint.m_hideAdditiveAnimations)
	{
		if (!g_animOptions.m_debugPrint.m_simplified)
		{
			StringBuilder<256> flagsStr;
			DC::GetAnimNodeBlendFlagString(m_flags, &flagsStr, ", ");
			SetColor(pData->m_output, 0xFF555555 | componentValue << 16 | componentValue << 8 | componentValue);
			Tab(pData->m_output, pData->m_depth);
			PrintTo(pData->m_output,
					"Blend: %s%s %1.2f/1.00: %s",
					type,
					(m_flags & DC::kAnimNodeBlendFlagCustomBlendOp) ? "-custom" : "",
					nodeBlendToUse,
					flagsStr.c_str());

			if (const FeatherBlendTable::Entry* pFbEntry = g_featherBlendTable.GetEntry(m_featherBlendIndex))
			{
				PrintTo(pData->m_output, " (feather: %s)", DevKitOnly_StringIdToString(pFbEntry->m_featherBlendId));
			}

			PrintTo(pData->m_output, "\n");
		}
	}

	float leftBlendValue, rightBlendValue;
	if (!isAdditive)
	{
		leftBlendValue  = pData->m_parentBlendValue * (1.0f - nodeBlendToUse);
		rightBlendValue = pData->m_parentBlendValue * nodeBlendToUse;
	}
	else
	{
		leftBlendValue  = pData->m_parentBlendValue;
		rightBlendValue = pData->m_parentBlendValue * nodeBlendToUse;
	}

	const AnimSnapshotNode* pLeftNode  = pSnapshot->GetSnapshotNode(m_leftIndex);
	const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);

	const ArtItemSkeleton* pSkel			 = ResourceTable::LookupSkel(pData->m_pAnimTable->GetSkelId()).ToArtItem();
	const ndanim::ValidBits leftValidBits	 = pLeftNode->GetValidBits(pSkel, pSnapshot, 0);
	const ndanim::ValidBits rightValidBits   = pRightNode->GetValidBits(pSkel, pSnapshot, 0);
	const ndanim::ValidBits tempValidBits	 = leftValidBits ^ rightValidBits;
	const bool rightBitsFullyDefinesLeftBits = (leftValidBits & tempValidBits).IsEmpty();

	bool printLeft = false;
	printLeft = printLeft || (leftBlendValue > 0.0001f);
	printLeft = printLeft || !rightBitsFullyDefinesLeftBits;
	printLeft = printLeft || !g_animOptions.m_debugPrint.m_hideNonContributingNodes;
	printLeft = printLeft || !pLeftNode->AllowPruning(pSnapshot);
	printLeft = printLeft || pLeftNode->HasErrors(pSnapshot);

	if (printLeft)
	{
		AnimNodeDebugPrintData leftData(*pData);
		leftData.m_depth++;
		leftData.m_parentBlendValue = !rightBitsFullyDefinesLeftBits ? pData->m_parentBlendValue : leftBlendValue;
		leftData.m_nodeIndex		= m_leftIndex;

		pLeftNode->DebugPrint(pSnapshot, &leftData);
	}
	else if (!g_animOptions.m_debugPrint.m_simplified)
	{
		SetColor(pData->m_output, 0xFF555555);
		Tab(pData->m_output, pData->m_depth + 1);
		PrintTo(pData->m_output, "{...}\n");
	}

	const bool hideRightBecauseAdd = isAdditive && g_animOptions.m_debugPrint.m_hideAdditiveAnimations;
	bool printRight = false;
	printRight = (rightBlendValue > 0.0001f || !g_animOptions.m_debugPrint.m_hideNonContributingNodes) && !hideRightBecauseAdd;
	printRight = printRight || pRightNode->HasErrors(pSnapshot);

	if (printRight)
	{
		AnimNodeDebugPrintData rightData(*pData);
		rightData.m_depth++;
		rightData.m_parentBlendValue = rightBlendValue;
		rightData.m_nodeIndex		 = m_rightIndex;

		pRightNode->DebugPrint(pSnapshot, &rightData);
	}
	else if (!hideRightBecauseAdd && !g_animOptions.m_debugPrint.m_simplified)
	{
		SetColor(pData->m_output, 0xFF555555);
		Tab(pData->m_output, pData->m_depth + 1);
		PrintTo(pData->m_output, "{...}\n");
	}

	SetColor(pData->m_output, kColorWhite.ToAbgr8());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeBlend::DebugSubmitAnimPlayCount(const AnimStateSnapshot* pSnapshot) const
{
	STRIP_IN_FINAL_BUILD;

	const AnimSnapshotNode* pLeftNode  = pSnapshot->GetSnapshotNode(m_leftIndex);
	const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);

	pLeftNode->DebugSubmitAnimPlayCount(pSnapshot);
	pRightNode->DebugSubmitAnimPlayCount(pSnapshot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimSnapshotNode* AnimSnapshotNodeBlend::FindFirstNodeOfKind(const AnimStateSnapshot* pSnapshot,
																   StringId64 typeId) const
{
	const AnimSnapshotNode* pSelfRes = ParentClass::FindFirstNodeOfKind(pSnapshot, typeId);

	if (pSelfRes)
	{
		return pSelfRes;
	}

	if (const AnimSnapshotNode* pLeftNode = pSnapshot->GetSnapshotNode(m_leftIndex))
	{
		const AnimSnapshotNode* pRes = pLeftNode->FindFirstNodeOfKind(pSnapshot, typeId);

		if (pRes)
		{
			return pRes;
		}
	}

	if (const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex))
	{
		const AnimSnapshotNode* pRes = pRightNode->FindFirstNodeOfKind(pSnapshot, typeId);

		if (pRes)
		{
			return pRes;
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeBlend::VisitNodesOfKindInternal(AnimStateSnapshot* pSnapshot,
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

	const float nodeBlendToUse = GetEffectiveBlend();
	const float leftBlend	   = combinedBlend;
	const float rightBlend	   = nodeBlendToUse * combinedBlend;

	bool valid = true;

	if (AnimSnapshotNode* pLeftNode = pSnapshot->GetSnapshotNode(m_leftIndex))
	{
		valid = valid && pLeftNode->VisitNodesOfKindInternal(pSnapshot, typeId, visitFunc, this, leftBlend, userData);
	}

	if (AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex))
	{
		valid = valid && pRightNode->VisitNodesOfKindInternal(pSnapshot, typeId, visitFunc, this, rightBlend, userData);
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeBlend::VisitNodesOfKindInternal(const AnimStateSnapshot* pSnapshot,
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

	const float nodeBlendToUse = GetEffectiveBlend();
	const float leftBlend	   = combinedBlend;
	const float rightBlend	   = nodeBlendToUse * combinedBlend;

	bool valid = true;

	if (const AnimSnapshotNode* pLeftNode = pSnapshot->GetSnapshotNode(m_leftIndex))
	{
		valid = valid && pLeftNode->VisitNodesOfKindInternal(pSnapshot, typeId, visitFunc, this, leftBlend, userData);
	}

	if (const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex))
	{
		valid = valid && pRightNode->VisitNodesOfKindInternal(pSnapshot, typeId, visitFunc, this, rightBlend, userData);
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeBlend::ForAllAnimationsInternal(const AnimStateSnapshot* pSnapshot,
													 AnimationVisitFunc visitFunc,
													 float combinedBlend,
													 uintptr_t userData) const
{
	ParentClass::ForAllAnimationsInternal(pSnapshot, visitFunc, combinedBlend, userData);

	const float nodeBlendToUse = GetEffectiveBlend();
	const float leftBlend	   = combinedBlend;
	const float rightBlend	   = nodeBlendToUse * combinedBlend;

	if (const AnimSnapshotNode* pLeftNode = pSnapshot->GetSnapshotNode(m_leftIndex))
	{
		pLeftNode->ForAllAnimationsInternal(pSnapshot, visitFunc, leftBlend, userData);
	}

	if (const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex))
	{
		pRightNode->ForAllAnimationsInternal(pSnapshot, visitFunc, rightBlend, userData);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
SnapshotNodeHeap::Index AnimSnapshotNodeBlend::Clone(SnapshotNodeHeap* pDestHeap, const SnapshotNodeHeap* pSrcHeap) const
{
	SnapshotNodeHeap::Index clonedIndex = ParentClass::Clone(pDestHeap, pSrcHeap);
	AnimSnapshotNodeBlend* pClonedNode	= static_cast<AnimSnapshotNodeBlend*>(pDestHeap->GetNodeByIndex(clonedIndex));

	const AnimSnapshotNode* pLeftNode = pSrcHeap->GetNodeByIndex(m_leftIndex);
	pClonedNode->m_leftIndex = pLeftNode->Clone(pDestHeap, pSrcHeap);

	const AnimSnapshotNode* pRightNode = pSrcHeap->GetNodeByIndex(m_rightIndex);
	pClonedNode->m_rightIndex = pRightNode->Clone(pDestHeap, pSrcHeap);

	return clonedIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimSnapshotNodeBlend::GetHeapUsage(const SnapshotNodeHeap* pSrcHeap, U32& outMem, U32& outNumNodes) const
{
	ParentClass::GetHeapUsage(pSrcHeap, outMem, outNumNodes);

	if (const AnimSnapshotNode* pLeftNode = pSrcHeap->GetNodeByIndex(m_leftIndex))
	{
		pLeftNode->GetHeapUsage(pSrcHeap, outMem, outNumNodes);
	}

	if (const AnimSnapshotNode* pRightNode = pSrcHeap->GetNodeByIndex(m_rightIndex))
	{
		pRightNode->GetHeapUsage(pSrcHeap, outMem, outNumNodes);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
IAnimDataEval::IAnimData* AnimSnapshotNodeBlend::EvaluateNode(IAnimDataEval* pEval,
															  const AnimStateSnapshot* pSnapshot) const
{
	IAnimDataEval::IAnimData* pLeftData = nullptr;
	if (const AnimSnapshotNode* pLeftNode = pSnapshot->GetSnapshotNode(m_leftIndex))
	{
		pLeftData = pLeftNode->EvaluateNode(pEval, pSnapshot);
	}

	bool evaluateBothNodes = true;
	if (m_flags & DC::kAnimNodeBlendFlagEvaluateBothNodes)
	{
		evaluateBothNodes = true;
	}
	if (m_flags & DC::kAnimNodeBlendFlagEvaluateLeftNodeOnly)
	{
		evaluateBothNodes = false;
	}

	if (!evaluateBothNodes)
	{
		return pLeftData;
	}

	IAnimDataEval::IAnimData* pRightData = nullptr;
	if (const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex))
	{
		pRightData = pRightNode->EvaluateNode(pEval, pSnapshot);
	}

	const float nodeBlendToUse = GetEffectiveBlend();

	return pEval->Blend(pLeftData, pRightData, nodeBlendToUse);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimSnapshotNodeBlend::AllowPruning(const AnimStateSnapshot* pSnapshot) const
{
	const AnimSnapshotNode* pLeftNode  = pSnapshot->GetSnapshotNode(m_leftIndex);
	const AnimSnapshotNode* pRightNode = pSnapshot->GetSnapshotNode(m_rightIndex);

	return pLeftNode->AllowPruning(pSnapshot) && pRightNode->AllowPruning(pSnapshot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SnapshotNodeHeap::Index SnapshotAnimNodeBlend(AnimStateSnapshot* pSnapshot,
											  const DC::AnimNode* pDcAnimNode,
											  const SnapshotAnimNodeTreeParams& params,
											  SnapshotAnimNodeTreeResults& results)
{
	ANIM_ASSERT(pDcAnimNode->m_dcType == SID("anim-node-blend"));
	ANIM_ASSERT(pSnapshot->m_pSnapshotHeap);
	SnapshotNodeHeap::Index returnNodeIndex = -1;
	const StringId64 typeId = g_animNodeLibrary.LookupTypeIdFromDcType(pDcAnimNode->m_dcType);
	// JDB- the static blend can prevent us from actually creating an anim-node-blend here, so we have to run this
	// before calling g_animNodeLibrary.Create()
	const DC::AnimNodeBlend* pBlend = static_cast<const DC::AnimNodeBlend*>(pDcAnimNode);

	float blendFactor = 0.0f;

	if (pBlend->m_staticBlend != 0)
	{
		if (pBlend->m_blendFunc)
		{
			DC::AnimInfoCollection scriptInfoCollection(*params.m_pInfoCollection);
			DC::AnimStateSnapshotInfo snapshotInfo;

			ScriptValue argv[6];

			AnimSnapshotNodeBlend::GetAnimNodeBlendFuncArgs(argv,
															ARRAY_COUNT(argv),
															&scriptInfoCollection,
															&snapshotInfo,
															0.0f,
															pSnapshot->IsFlipped(),
															pSnapshot,
															Process::GetContextProcess(),
															nullptr);

			const DC::ScriptLambda* pLambda = pBlend->m_blendFunc;
			VALIDATE_LAMBDA(pLambda, "anim-node-blend-func", m_animState.m_name.m_symbol);
			blendFactor = ScriptManager::Eval(pLambda, SID("anim-node-blend-func"), ARRAY_COUNT(argv), argv).m_float;
		}

		blendFactor = Limit01(blendFactor);
	}

	if ((pBlend->m_staticBlend != 0) && (blendFactor <= 0.0))
	{
		SnapshotAnimNodeTreeParams blendChildParams = params;

		if (pBlend->m_flags & DC::kAnimNodeBlendFlagNoOverlayTransform)
		{
			blendChildParams.m_pConstOverlaySnapshot = blendChildParams.m_pMutableOverlaySnapshot = nullptr;
		}

		const DC::AnimNode* pLeftNode = pBlend->m_left;
		returnNodeIndex = AnimStateSnapshot::SnapshotAnimNodeTree(pSnapshot, pLeftNode, blendChildParams, results);
	}
	else
	{
		AnimSnapshotNode* pNewNode = pSnapshot->m_pSnapshotHeap->AllocateNode(typeId, &returnNodeIndex);

		if (AnimSnapshotNodeBlend* pNewBlendNode = AnimSnapshotNodeBlend::FromAnimNode(pNewNode))
		{
			pNewBlendNode->SnapshotNode(pSnapshot, pBlend, params, results);

			if (pBlend->m_staticBlend != 0)
			{
				pNewBlendNode->m_blendFunc   = nullptr;
				pNewBlendNode->m_blendFactor = blendFactor;
			}

			if (pNewBlendNode->m_flags & DC::kAnimNodeBlendFlagApplyJointLimits)
			{
				SnapshotNodeHeap::Index parentNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
				if (AnimNodeJointLimits* pJointLimitsNode = pSnapshot->m_pSnapshotHeap->AllocateNode<AnimNodeJointLimits>(&parentNodeIndex))
				{
					NdGameObjectHandle hGo = params.m_pAnimData->m_hProcess.CastHandleType<MutableNdGameObjectHandle>();
					pJointLimitsNode->SetCustomLimits(hGo, pNewBlendNode->m_customLimitsId);
					pJointLimitsNode->SetChildIndex(returnNodeIndex);
					returnNodeIndex = parentNodeIndex;
				}
			}
		}
		else
		{
			MsgAnimErr("[%s] Ran out of snapshot memory creating new snapshot node '%s' for state '%s'\n",
					   DevKitOnly_StringIdToString(params.m_pAnimData->m_hProcess.GetUserId()),
					   DevKitOnly_StringIdToString(pDcAnimNode->m_dcType),
					   pSnapshot->m_animState.m_name.m_string.GetString());

			returnNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
		}
	}

	return returnNodeIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void AnimSnapshotNodeBlend::GetAnimNodeBlendFuncArgs(ScriptValue* pArgs,
													 U32F maxArgs,
													 const DC::AnimInfoCollection* pInfoCollection,
													 DC::AnimStateSnapshotInfo* pSnapshotInfo,
													 float statePhase,
													 bool flipped,
													 AnimStateSnapshot* pSnapshot,
													 const Process* pContextProcess,
													 AnimSnapshotNodeBlend* pBlendNode)
{
	ANIM_ASSERT(pArgs && (maxArgs >= 6));

	if (!pArgs || maxArgs < 6)
		return;

	if (pSnapshotInfo)
	{
		pSnapshotInfo->m_stateSnapshot					= pSnapshot;
		pSnapshotInfo->m_translatedPhaseAnimName		= pSnapshotInfo->m_translatedPhaseAnimName;
		pSnapshotInfo->m_translatedPhaseAnimSkelId		= pSnapshotInfo->m_translatedPhaseAnimSkelId;
		pSnapshotInfo->m_translatedPhaseAnimHierarchyId = pSnapshotInfo->m_translatedPhaseAnimHierarchyId;
	}

	pArgs[0] = ScriptValue(pInfoCollection);
	pArgs[1] = ScriptValue(statePhase);
	pArgs[2] = ScriptValue(flipped);
	pArgs[3] = ScriptValue(pSnapshotInfo);
	pArgs[4] = ScriptValue(pContextProcess);
	pArgs[5] = ScriptValue(pBlendNode);
}
