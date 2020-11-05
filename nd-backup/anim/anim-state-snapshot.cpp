/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-state-snapshot.h"

#include "corelib/memory/relocate.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-node-joint-limits.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-snapshot-node-blend.h"
#include "ndlib/anim/anim-snapshot-node-out-of-memory.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/anim/feather-blend-table.h"
#include "ndlib/memory/relocatable-heap-rec.h"
#include "ndlib/memory/relocatable-heap.h"
#include "ndlib/process/process-handles.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/anim-overlay-defines.h"
#include "ndlib/scriptx/h/dc-types.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-anim.h"

/// --------------------------------------------------------------------------------------------------------------- ///
// Ensure that the blend node is the largest of the nodes.
STATIC_ASSERT(sizeof(DC::AnimNodeBlend) >= sizeof(DC::AnimNodeAnimation));

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateSnapshot::~AnimStateSnapshot()
{
	if (AnimSnapshotNode* pRootNode = GetSnapshotNode(m_rootNodeIndex))
	{
		pRootNode->ReleaseNodeRecursive(m_pSnapshotHeap);
	}

	m_rootNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateSnapshot::AllocateAnimDeltaTweakXform()
{
	ANIM_ASSERT(m_pAnimDeltaTweakXform == nullptr);
	m_pAnimDeltaTweakXform = NDI_NEW (kAlign64) Transform(kIdentity);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateSnapshot::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pAnimDeltaTweakXform, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pSnapshotHeap, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateSnapshot::GenerateAnimCommands(AnimCmdList* pAnimCmdList,
											 I32F outputInstance,
											 const AnimCmdGenInfo* pCmdGenInfo) const
{
	const AnimSnapshotNode* pRootNode = GetSnapshotNode(m_rootNodeIndex);
	ANIM_ASSERT(pRootNode);
	pRootNode->GenerateAnimCommands(this, pAnimCmdList, outputInstance, pCmdGenInfo);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ndanim::ValidBits AnimStateSnapshot::GetValidBitsFromAnimNodeTree(const ArtItemSkeleton* pSkel, U32 iGroup) const
{
	const AnimSnapshotNode* pRootNode = GetSnapshotNode(m_rootNodeIndex);
	ANIM_ASSERT(pRootNode);
	const ndanim::ValidBits vb = pRootNode->GetValidBits(pSkel, this, iGroup);
	return vb;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static char* AddSpaces(char* buf, size_t bufSize, int indent)
{
	static int indentStep = 2;

	char* ptr = buf;
	for (int ii = 0; ii < Min(indent * indentStep, (int)bufSize - 1); ++ii)
		*ptr++ = ' ';
	*ptr = 0;
	return buf;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateSnapshot::DebugPrint(MsgOutput output,
								   const AnimStateInstance* pInst,
								   const FgAnimData* pAnimData,
								   const AnimTable* pAnimTable,
								   const DC::AnimInfoCollection* pInfoCollection,
								   float phase,
								   float blend,
								   float blendTime,
								   float motionBlend,
								   float motionBlendTime,
								   float effectiveFade,
								   bool flipped,
								   bool additiveLayer,
								   bool isBaseLayer,
								   bool frozen,
								   StringId64 customApRefChannel,
								   I32 featherBlendTableIndex,
								   float featherBlendTableBlend,
								   I32F indent,
								   const AnimCopyRemapLayer* pRemapLayer)
{
	char indentBuf[512];
	AddSpaces(indentBuf, sizeof(indentBuf), indent);

	SetColor(output, 0xFF000000 | 0x0000FF00);
	if (!g_animOptions.m_debugPrint.m_simplified)
	{
		PrintTo(output, "%sAnimState %s, phase: ", indentBuf, m_animState.m_name.m_string.GetString());
	}

	if (frozen)
		SetColor(output, 0xFFFFAA00);
	if (!g_animOptions.m_debugPrint.m_simplified)
	{
		PrintTo(output, "%1.3f%s", phase, frozen ? " [frozen]" : "");
	}

	SetColor(output, 0xFF000000 | 0x0000FF00);
	if (!g_animOptions.m_debugPrint.m_simplified)
	{
		StringBuilder<64> featherBlendTableStr;

		if (const FeatherBlendTable::Entry* pEntry = g_featherBlendTable.GetEntry(featherBlendTableIndex))
		{
			featherBlendTableStr.format(" [%s:%.2f] ",
										DevKitOnly_StringIdToString(pEntry->m_featherBlendId),
										featherBlendTableBlend);
		}
		
		PrintTo(output,
				", blend: %1.2f/%1.2f [%1.2fs, %1.2fs] (%1.2f) %s (%s) %s\n",
				blend,
				motionBlend,
				blendTime,
				motionBlendTime,
				pInst->GetEffectiveFade(),
				featherBlendTableStr.c_str(),
				(m_animState.m_userId != INVALID_STRING_ID_64) ? DevKitOnly_StringIdToString(m_animState.m_userId) : "",
				flipped ? "flip" : "norm",
				customApRefChannel == INVALID_STRING_ID_64 ? "" : DevKitOnly_StringIdToString(customApRefChannel));

		const DC::AnimStateFlag printFlags = m_animState.m_flags & ~DC::kAnimStateFlagLoggedIn;
		if (printFlags != 0)
		{
			StringBuilder<256> flagsStr;
			DC::GetAnimStateFlagString(printFlags, &flagsStr, ", ");
			PrintTo(output, "%s  flags: %s\n", indentBuf, flagsStr.c_str());
		}

		const U32 blendOverridenFlags = pInst->GetBlendOverridenFlags();
		if (blendOverridenFlags != 0)
		{
			SetColor(output, 0xFF000000 | 0x000030FF);
			PrintTo(output, "%s", indentBuf);
			if (blendOverridenFlags & AnimStateInstance::kAnimFadeTimeOverriden)
				PrintTo(output, "Anim-Fade-Time ");
			if (blendOverridenFlags & AnimStateInstance::kMotionFadeTimeOverriden)
				PrintTo(output, "Motion-Fade-Time ");
			if (blendOverridenFlags & AnimStateInstance::kAnimCurveOverriden)
				PrintTo(output, "Anim-Curve ");
			PrintTo(output, "are overriden\n");
			SetColor(output, 0xFF000000 | 0x0000FF00);
		}
	}

	if (!g_animOptions.m_debugPrint.m_simplified)
	{
		PrintTo(output, "%s Phase Anim: %s ", indentBuf, DevKitOnly_StringIdToString(m_translatedPhaseAnimName));
		if (m_translatedPhaseAnimName != m_originalPhaseAnimName)
		{
			SetColor(output, 0xFF999999);
			PrintTo(output, "[%s]", DevKitOnly_StringIdToString(m_originalPhaseAnimName));
			SetColor(output, 0xFF000000 | 0x0000FF00);
		}
		PrintTo(output, "\n");

		if (!pRemapLayer)
		{
			StringBuilder<1024> callbackBufAction;
			StringBuilder<256> callbackBufController;

			AnimStateLayer* pLayer = pInst->GetLayer();

			pLayer->InstanceCallBackDebugPrintFunc(pInst, SID("SubsystemController"), &callbackBufController);
			pLayer->InstanceCallBackDebugPrintFunc(pInst, SID("SubsystemList"), &callbackBufAction);

			if (!callbackBufController.empty() || !callbackBufAction.empty())
			{
				SetColor(output, 0x0000FFFF);
				PrintTo(output,  "%s Controller:", indentBuf);
				SetColor(output, 0xFF000000 | 0x0000FFFF);
				PrintTo(output,  " %s", callbackBufController.c_str());
				if (!callbackBufAction.empty())
					PrintTo(output,  " - Subsystems:");
				SetColor(output, 0xFF000000 | 0x0000FFFF);
				PrintTo(output,  " %s", callbackBufAction.c_str());
				SetColor(output, 0xFF000000 | 0x0000FF00);
				PrintTo(output,  "\n");
			}
		}
	}

	AnimNodeDebugPrintData data;
	data.m_output		 = output;
	data.m_pAnimData	 = pAnimData;
	data.m_pAnimTable	 = pAnimTable;
	data.m_nodeIndex	 = m_rootNodeIndex;
	data.m_pActorInfo	 = pInfoCollection->m_actor;
	data.m_statePhase	 = phase;
	data.m_additiveLayer = additiveLayer;
	data.m_isBaseLayer	 = isBaseLayer;
	data.m_depth		 = indent + 1;
	data.m_parentBlendValue = g_animOptions.m_debugPrint.m_useEffectiveFade ? effectiveFade : 1.0f;
	data.m_pRemapLayer		= pRemapLayer;

	const AnimSnapshotNode* pNode = GetSnapshotNode(m_rootNodeIndex);
	pNode->DebugPrint(this, &data);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateSnapshot::HasLoopingAnim() const
{
	const AnimSnapshotNode* pRootNode = GetSnapshotNode(m_rootNodeIndex);
	ANIM_ASSERT(pRootNode);
	const bool hasLoopingAnim = pRootNode->HasLoopingAnimation(this);
	return hasLoopingAnim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimSnapshotNode* AnimStateSnapshot::GetSnapshotNode(SnapshotNodeHeap::Index index) const
{
	if (index == SnapshotNodeHeap::kOutOfMemoryIndex)
	{
		return &AnimSnapshotNodeOutOfMemory::GetSingleton();
	}

	return m_pSnapshotHeap->GetNodeByIndex(index);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSnapshotNode* AnimStateSnapshot::GetSnapshotNode(SnapshotNodeHeap::Index index)
{
	if (index == SnapshotNodeHeap::kOutOfMemoryIndex)
	{
		return &AnimSnapshotNodeOutOfMemory::GetSingleton();
	}

	return m_pSnapshotHeap->GetNodeByIndex(index);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateSnapshot::VisitNodesOfKind(StringId64 typeId, SnapshotVisitNodeFunc visitFunc, uintptr_t userData)
{
	if (AnimSnapshotNode* pRootNode = GetSnapshotNode(m_rootNodeIndex))
	{
		pRootNode->VisitNodesOfKind(this, typeId, visitFunc, userData);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateSnapshot::VisitNodesOfKind(StringId64 typeId, SnapshotConstVisitNodeFunc visitFunc, uintptr_t userData) const
{
	if (const AnimSnapshotNode* pRootNode = GetSnapshotNode(m_rootNodeIndex))
	{
		pRootNode->VisitNodesOfKind(this, typeId, visitFunc, userData);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateSnapshot::GetHeapUsage(U32& outMem, U32& outNumNodes) const
{
	outMem		= 0;
	outNumNodes = 0;

	GetSnapshotNode(m_rootNodeIndex)->GetHeapUsage(m_pSnapshotHeap, outMem, outNumNodes);
	const U32 recordMemory = AlignSize(outNumNodes * sizeof(RelocatableHeapRecord), RelocatableHeap::GetAlignment());
	outMem += recordMemory;
}

/// --------------------------------------------------------------------------------------------------------------- ///
IAnimDataEval::IAnimData* AnimStateSnapshot::EvaluateTree(IAnimDataEval* pEval) const
{
	return GetSnapshotNode(m_rootNodeIndex)->EvaluateNode(pEval, this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::AnimNodeBlendFlag AnimOverlayExtraBlendFlags(const DC::AnimOverlayFlags flags)
{
	DC::AnimNodeBlendFlag blendFlags = 0;

	if ((flags & DC::kAnimOverlayFlagsEvaluateBothNodes) == 0)
		blendFlags |= DC::kAnimNodeBlendFlagEvaluateLeftNodeOnly;
	else
		blendFlags |= DC::kAnimNodeBlendFlagEvaluateBothNodes;

	if (flags & DC::kAnimOverlayFlagsLegFixIk)
		blendFlags |= DC::kAnimNodeBlendFlagLegFixIk;

	return blendFlags;
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::AnimStateFlag AnimOverlayExtraStateFlags(const DC::AnimOverlayFlags flags)
{
	DC::AnimStateFlag stateFlags = 0;

	if (flags & DC::kAnimOverlayFlagsNoWeaponIk)
		stateFlags |= DC::kAnimStateFlagNoWeaponIk;

	if (flags & DC::kAnimOverlayFlagsRandomStartPhase)
		stateFlags |= DC::kAnimStateFlagRandomStartPhase;

	if (flags & DC::kAnimOverlayFlagsWeaponIkFeather)
		stateFlags |= DC::kAnimStateFlagWeaponIkFeather;

	return stateFlags;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateSnapshot::SnapshotAnimState(const DC::AnimState* pState,
										  const AnimOverlaySnapshot* pConstOverlaySnapshot,
										  AnimOverlaySnapshot* pMutableOverlaySnapshot,
										  const AnimTable* pAnimTable,
										  const DC::AnimInfoCollection* pInfoCollection,
										  const FgAnimData* pAnimData,
										  const FadeToStateParams* pFadeToStateParams)
{
	PROFILE(Animation, SnapshotAnimState);

	// Snapshot the state itself
	m_animState = *pState;

	if (m_animState.m_initFunc)
	{
		PROFILE(Animation, SAS_InitFunc);

		DC::AnimInfoCollection scriptInfoCollection(*pInfoCollection);
		scriptInfoCollection.m_actor	= scriptInfoCollection.m_actor;
		scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
		scriptInfoCollection.m_top		= scriptInfoCollection.m_top;

		const ScriptValue argv[] = { ScriptValue(&m_animState), ScriptValue(&scriptInfoCollection) };

		const DC::ScriptLambda* pLambda = m_animState.m_initFunc;
		VALIDATE_LAMBDA(pLambda, "anim-state-init-func", m_animState.m_name.m_symbol);
		ScriptManager::Eval(pLambda, SID("anim-state-init-func"), ARRAY_COUNT(argv), argv);
	}

	// Get the state phase animation func
	StringId64 statePhaseAnimId = m_animState.m_phaseAnimName;

	if (m_animState.m_phaseAnimFunc)
	{
		PROFILE(Animation, SAS_StartPhaseFunc);

		DC::AnimInfoCollection scriptInfoCollection(*pInfoCollection);
		scriptInfoCollection.m_actor = scriptInfoCollection.m_actor;
		scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
		scriptInfoCollection.m_top = scriptInfoCollection.m_top;

		const ScriptValue argv[] = { ScriptValue(&m_animState),
									 ScriptValue(&scriptInfoCollection),
									 ScriptValue(pAnimTable) };

		const DC::ScriptLambda* pLambda = m_animState.m_phaseAnimFunc;
		VALIDATE_LAMBDA(pLambda, "anim-state-phase-anim-func", m_animState.m_name.m_symbol);
		statePhaseAnimId = ScriptManager::Eval(pLambda, SID("anim-state-phase-anim-func"), ARRAY_COUNT(argv), argv).m_stringId;
	}

	m_originalPhaseAnimName = statePhaseAnimId;

	//Set the mirror flag
	m_flags.m_isFlipped = IsFlipped(pInfoCollection);

	m_effMinBlend = pState->m_effMinBlend;

	// Do a deep copy of the animNode tree using the animNodeSnapshots.
	SnapshotAnimNodeTreeParams params;
	params.m_pConstOverlaySnapshot	  = pConstOverlaySnapshot;
	params.m_pMutableOverlaySnapshot  = pMutableOverlaySnapshot;
	params.m_pAnimTable			= pAnimTable;
	params.m_pInfoCollection	= pInfoCollection;
	params.m_pFadeToStateParams = pFadeToStateParams;
	params.m_pAnimData	  = pAnimData;
	params.m_stateFlipped = m_flags.m_isFlipped;
	params.m_disableRandomization = m_flags.m_disableRandomization;

	SnapshotAnimNodeTreeResults results;

	const DC::AnimNode* pRootDcNode = m_animState.m_tree;
	m_rootNodeIndex = SnapshotAnimNodeTree(this, pRootDcNode, params, results);

	m_animState.m_tree = nullptr;
	m_animState.m_flags |= results.m_newStateFlags;
	m_animState.m_startPhaseFunc = results.m_pStartPhaseFunc ? results.m_pStartPhaseFunc : m_animState.m_startPhaseFunc;

	if (pConstOverlaySnapshot)
	{
		TransformState(params);
	}

	AnimSnapshotNode* pRootSnapshotNode = GetSnapshotNode(m_rootNodeIndex);
	ANIM_ASSERT(pRootSnapshotNode);
	pRootSnapshotNode->RefreshBreadth(this);

	// Translate the state phase animations using the overlays
	// NOTE:  This needs to happen *after* the state is translated because we might have incremented the variant counters
	//        and if we do this earlier the statePhaseAnimId won't match the animation being played
	const bool disablePhaseRemap = pFadeToStateParams && pFadeToStateParams->m_disableAnimReplacement;
	if (!disablePhaseRemap && pConstOverlaySnapshot)
	{
		statePhaseAnimId = pConstOverlaySnapshot->LookupTransformedAnimId(statePhaseAnimId);
	}

	// Translate the state phase animation
	m_translatedPhaseAnimName	 = statePhaseAnimId;
	m_translatedPhaseSkelId		 = pAnimTable->GetSkelId();
	m_translatedPhaseHierarchyId = pAnimTable->GetHierarchyId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateSnapshot::TransformState(const SnapshotAnimNodeTreeParams& params)
{
	PROFILE(Animation, TransformState);

	if (!params.m_pConstOverlaySnapshot)
		return;

	const StringId64 stateId = m_animState.m_name.m_symbol;
	AnimSnapshotNode* pRootNode = GetSnapshotNode(m_rootNodeIndex);
	ANIM_ASSERT(pRootNode);

	AnimOverlayIterator overlayItr = AnimOverlayIterator(stateId, true);

	overlayItr = params.m_pConstOverlaySnapshot->GetNextEntry(overlayItr);

	while (overlayItr.IsValid())
	{
		const DC::AnimOverlaySetEntry* pEntry = overlayItr.GetEntry();

		if (pEntry->m_type != DC::kAnimOverlayTypeBlend && pEntry->m_type != DC::kAnimOverlayTypeTree)
		{
			overlayItr = params.m_pConstOverlaySnapshot->GetNextEntry(overlayItr);
			continue;
		}

		if ((pEntry->m_type == DC::kAnimOverlayTypeTree) && !pEntry->m_tree)
		{
			MsgAnim("Overlay set entry for '%s' specified as tree remapping but has no tree pointer\n",
					DevKitOnly_StringIdToString(overlayItr.GetSourceId()));
			overlayItr = params.m_pConstOverlaySnapshot->GetNextEntry(overlayItr);
			continue;
		}

		SnapshotNodeHeap::Index blendNodeIndex;
		AnimSnapshotNodeBlend* pNewBlendSnapshot = m_pSnapshotHeap->AllocateNode<AnimSnapshotNodeBlend>(&blendNodeIndex);
		const AnimSnapshotNodeBlend* pSavedParent = params.m_pParentBlendNode;

		if (pNewBlendSnapshot)
		{
			params.m_pParentBlendNode = pNewBlendSnapshot;

			pNewBlendSnapshot->m_featherBlendIndex = g_featherBlendTable.LoginFeatherBlend(pEntry->m_featherBlend,
																						   params.m_pAnimData);
			pNewBlendSnapshot->m_blendFactor = 1.0f;
			pNewBlendSnapshot->m_leftIndex	 = m_rootNodeIndex;
			pNewBlendSnapshot->m_blendFunc	 = pEntry->m_blendFunc;
			pNewBlendSnapshot->m_flags		 = AnimOverlayExtraBlendFlags(pEntry->m_flags);
		}
		else
		{
			MsgAnimErr("[%s] Failed to allocate new AnimSnapshotNodeBlend! (state: %s)\n",
					   DevKitOnly_StringIdToString(params.m_pAnimData->m_hProcess.GetUserId()),
					   DevKitOnly_StringIdToString(stateId));
			break;
		}

		const DC::AnimNode* pRightDcNode = nullptr;
		DC::AnimNodeAnimation animNode;

		if (pEntry->m_tree)
		{
			pRightDcNode = pEntry->m_tree;
		}
		else
		{
			const bool detachedPhase = (pEntry->m_flags & DC::kAnimOverlayFlagsDetachedPhase) != 0;
			const bool globalClockPhase = (pEntry->m_flags & DC::kAnimOverlayFlagsGlobalClockPhase) != 0;
			const bool randomStartPhase = (pEntry->m_flags & DC::kAnimOverlayFlagsRandomStartPhase) != 0;
			DC::AnimNodeAnimationFlag flags = 0;

			if (detachedPhase)
				flags |= DC::kAnimNodeAnimationFlagDetachedPhase;
			if (globalClockPhase)
				flags |= DC::kAnimNodeAnimationFlagGlobalClockPhase;
			if (randomStartPhase)
				flags |= DC::kAnimNodeAnimationFlagRandomStartPhase;

			animNode.m_dcType = SID("anim-node-animation");
			animNode.m_animation = pEntry->m_remapId;
			animNode.m_phaseFunc = nullptr;
			animNode.m_animationFunc = nullptr;
			animNode.m_flipFunc = nullptr;
			animNode.m_flags = flags;

			pRightDcNode = &animNode;
		}

		SnapshotAnimNodeTreeResults results;
		results.m_newStateFlags = AnimOverlayExtraStateFlags(pEntry->m_flags);
		results.m_pStartPhaseFunc = nullptr;
		pNewBlendSnapshot->m_rightIndex = SnapshotAnimNodeTree(this, pRightDcNode, params, results);

		m_animState.m_flags |= results.m_newStateFlags;
		if (results.m_pStartPhaseFunc)
			m_animState.m_startPhaseFunc = results.m_pStartPhaseFunc;

		// Update the root node index to reflect the change in tree structure.
		m_rootNodeIndex = blendNodeIndex;

		if (AnimSnapshotNode* pLeftNode = m_pSnapshotHeap->GetNodeByIndex(pNewBlendSnapshot->m_leftIndex))
		{
			pLeftNode->OnAddedToBlendNode(params, this, pNewBlendSnapshot, true);
		}

		if (AnimSnapshotNode* pRightNode = m_pSnapshotHeap->GetNodeByIndex(pNewBlendSnapshot->m_rightIndex))
		{
			pRightNode->OnAddedToBlendNode(params, this, pNewBlendSnapshot, false);
		}

		if (pNewBlendSnapshot->m_flags & DC::kAnimNodeBlendFlagApplyJointLimits)
		{
			SnapshotNodeHeap::Index parentNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;
			if (AnimNodeJointLimits* pJointLimitsNode = m_pSnapshotHeap->AllocateNode<AnimNodeJointLimits>(&parentNodeIndex))
			{
				NdGameObjectHandle hGo = params.m_pAnimData->m_hProcess.CastHandleType<MutableNdGameObjectHandle>();
				pJointLimitsNode->SetCustomLimits(hGo, pNewBlendSnapshot->m_customLimitsId);
				pJointLimitsNode->SetChildIndex(m_rootNodeIndex);
				m_rootNodeIndex = parentNodeIndex;
			}
		}

		params.m_pParentBlendNode = pSavedParent;

		overlayItr = params.m_pConstOverlaySnapshot->GetNextEntry(overlayItr);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
SnapshotNodeHeap::Index AnimStateSnapshot::SnapshotAnimNodeTree(AnimStateSnapshot* pSnapshot,
																const DC::AnimNode* pNode,
																const SnapshotAnimNodeTreeParams& params,
																SnapshotAnimNodeTreeResults& results)
{
	PROFILE(Animation, SnapshotAnimNodeTree);

	if (!pNode)
	{
		return SnapshotNodeHeap::kOutOfMemoryIndex;
	}

	SnapshotNodeHeap::Index returnNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;

	switch (pNode->m_dcType.GetValue())
	{
	case SID_VAL("anim-node-treeref"):
		{
			// We replace all instances of tree-ref nodes with a copy of the tree being referenced.
			const DC::AnimNodeTreeref* pTreerefLs = static_cast<const DC::AnimNodeTreeref*>(pNode);

			ScriptValue evalRet;
			{
				DC::AnimInfoCollection scriptInfoCollection(*params.m_pInfoCollection);
				scriptInfoCollection.m_actor = scriptInfoCollection.m_actor;
				scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
				scriptInfoCollection.m_top = scriptInfoCollection.m_top;

				const ScriptValue argv[] ={ScriptValue(pTreerefLs), ScriptValue(&scriptInfoCollection)};

				const DC::ScriptLambda* pLambda = pTreerefLs->m_treeFunc;
				VALIDATE_LAMBDA(pLambda, "anim-node-tree-func", m_animState.m_name.m_symbol);
				evalRet = ScriptManager::Eval(pLambda, SID("anim-tree-func"), ARRAY_COUNT(argv), argv);
			}

			const DC::AnimNode* pNewNode = (const DC::AnimNode*)evalRet.m_pointer;

			returnNodeIndex = SnapshotAnimNodeTree(pSnapshot, pNewNode, params, results);
		}
		break;

	default:
		{
			const StringId64 typeId = g_animNodeLibrary.LookupTypeIdFromDcType(pNode->m_dcType);

			if (typeId == INVALID_STRING_ID_64)
			{
				ANIM_ASSERTF(false, ("Bad AnimNode in tree! '%s'", DevKitOnly_StringIdToString(pNode->m_dcType)));
				return returnNodeIndex;
			}

			AnimNodeLibrary::SnapshotAnimNodeTreeFunc* pSnapshotFunc = g_animNodeLibrary.GetSnapshotFuncDcType(pNode->m_dcType);
			if (pSnapshotFunc)
			{
				return pSnapshotFunc(pSnapshot, pNode, params, results);
			}

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
		break;
	}

	return returnNodeIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateSnapshot::RefreshPhasesAndBlends(float statePhase,
											   bool topTrackInstance,
											   const DC::AnimInfoCollection* pInfoCollection)
{
	PROFILE(Animation, SnapshotRefPhAndBl);

	m_statePhase = statePhase;

	AnimSnapshotNode* pRootNode = GetSnapshotNode(m_rootNodeIndex);
	ANIM_ASSERT(pRootNode);
	return pRootNode->RefreshPhasesAndBlends(this, statePhase, topTrackInstance, pInfoCollection);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateSnapshot::StepNodes(float deltaTime, const DC::AnimInfoCollection* pInfoCollection)
{
	PROFILE(Animation, StepNodes);

	AnimSnapshotNode* pRootNode = GetSnapshotNode(m_rootNodeIndex);
	ANIM_ASSERT(pRootNode);
	pRootNode->StepNode(this, deltaTime, pInfoCollection);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateSnapshot::RefreshAnims()
{
	PROFILE(Animation, SnapshotRefreshAnims);

	DC::AnimState* pState = &m_animState;

	bool successfulRefresh = true;

	// Update all animation art pointers in the animation nodes.
	AnimSnapshotNode* pRootNode = GetSnapshotNode(m_rootNodeIndex);

	if (!pRootNode || !pRootNode->RefreshAnims(this))
	{
		successfulRefresh = false;
	}

	// Update the update rate based on the 'update animation'
	const ArtItemAnim* pArtItemAnim = AnimMasterTable::LookupAnim(m_translatedPhaseSkelId,
																  m_translatedPhaseHierarchyId,
																  m_translatedPhaseAnimName).ToArtItem();

	if (pArtItemAnim)
	{
		const ndanim::ClipData* pClipData = pArtItemAnim->m_pClipData;

		if (pClipData)
		{
			ANIM_ASSERT(pClipData->m_numTotalFrames > 0);

			m_phaseAnimFrameCount = static_cast<I32>(static_cast<float>(pClipData->m_numTotalFrames)
													 * 30.0f / pClipData->m_framesPerSecond);

			// 30.0f / (numFrames - 1) 
			//         OR
			// 15.0f / (numFrames - 1) 
			m_updateRate = 30.0f;
			if (pClipData->m_numTotalFrames > 1)
			{
				m_updateRate = pClipData->m_framesPerSecond * pClipData->m_phasePerFrame;
			}

			//m_playbackRate = 1.0f;
			//m_framesPerSecond = pClipData->m_framesPerSecond;
			//m_phasePerFrame = pClipData->m_phasePerFrame;
		}
		else
		{
			if (FALSE_IN_FINAL_BUILD(g_animOptions.m_warnOnMissingAnimations))
			{
				const char* name = DevKitOnly_StringIdToStringOrHex(pState->m_phaseAnimName);
				if (m_translatedPhaseAnimName != INVALID_STRING_ID_64)
					name = DevKitOnly_StringIdToStringOrHex(m_translatedPhaseAnimName);

				MsgAnim("RefreshAnims: can't find animation clip data \"%s\"!\n", name);
			}

			m_updateRate = 0.5f;

			//m_playbackRate = 1.0f;
			//m_framesPerSecond = 30.0f;
			//m_phasePerFrame = m_updateRate / m_framesPerSecond;

			successfulRefresh = false;
		}
	}
	else
	{
		if (FALSE_IN_FINAL_BUILD(g_animOptions.m_warnOnMissingAnimations))
		{
			const char* name = "";

			name = DevKitOnly_StringIdToStringOrHex(pState->m_phaseAnimName);
			if (m_translatedPhaseAnimName != INVALID_STRING_ID_64)
				name = DevKitOnly_StringIdToStringOrHex(m_translatedPhaseAnimName);

			MsgAnim("RefreshAnims: can't find animation \"%s\"!\n", name);
		}

		m_updateRate = 0.5f;

		//m_playbackRate = 1.0f;
		//m_framesPerSecond = 30.0f;
		//m_phasePerFrame = m_updateRate / m_framesPerSecond;

		successfulRefresh = false;
	}

	return successfulRefresh;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateSnapshot::RefreshTransitions(const DC::AnimInfoCollection* pInfoCollection, const AnimTable* pAnimTable)
{
	DC::AnimState* pState = &m_animState;

	if (!pState->m_actor)
		return false;

	AnimDcAssertNotRelocatingJanitor dcnrj(FILE_LINE_FUNC);

	pState->m_dynamicTransitionGroup = nullptr;

	StringId64 dynamicTransitionGroupId = INVALID_STRING_ID_64;
	if (pState->m_dynamicTransitionGroupFunc)
	{
		DC::AnimInfoCollection scriptInfoCollection(*pInfoCollection);
		scriptInfoCollection.m_actor = scriptInfoCollection.m_actor;
		scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
		scriptInfoCollection.m_top = scriptInfoCollection.m_top;

		const ScriptValue argv[] = { ScriptValue(pState), ScriptValue(&scriptInfoCollection) };

		const DC::ScriptLambda* pLambda = pState->m_dynamicTransitionGroupFunc;
		VALIDATE_LAMBDA(pLambda, "anim-state-dtgroup-func", m_animState.m_name.m_symbol);
		dynamicTransitionGroupId = ScriptManager::Eval(pLambda, SID("anim-state-dtgroup-func"), ARRAY_COUNT(argv), argv).m_stringId;
	}

	const DC::AnimActor* pAnimActor = pState->m_actor;
	const DC::Map* pTransGroupMap = pAnimActor->m_transitionGroupMap;
	const DC::AnimTransitionGroup* pTransGroup = ScriptManager::MapLookup<DC::AnimTransitionGroup>(pTransGroupMap,
																								   dynamicTransitionGroupId);

	if (pTransGroup)
	{
		pState->m_dynamicTransitionGroup = pTransGroup;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimStateSnapshot::EvaluateFloat(const StringId64* channelNames,
									  U32F numChannels,
									  float* pOutChannelFloats,
									  const SnapshotEvaluateParams& params /* = SnapshotEvaluateParams() */) const
{
	PROFILE(Animation, SnapshotEvaluateFloat);

	for (U32F ii = 0; ii < numChannels; ++ii)
	{
		pOutChannelFloats[ii] = 0.0f;
	}

	const AnimSnapshotNode* pRootNode = GetSnapshotNode(m_rootNodeIndex);
	ANIM_ASSERT(pRootNode);
	if (!pRootNode)
	{
		return 0;
	}

	SnapshotEvaluateParams evaluateParams(params);
	evaluateParams.m_channelName = INVALID_STRING_ID_64;

	U32F evaluatedChannels = 0;

	for (U32F ii = 0; ii < numChannels; ++ii)
	{
		// Select a new channel to evaluate
		evaluateParams.m_channelName = channelNames[ii];

		if (pRootNode->EvaluateFloatChannel(this, &pOutChannelFloats[ii], evaluateParams))
		{
			evaluatedChannels |= (1 << ii);
		}
	}

	return evaluatedChannels;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimStateSnapshot::Evaluate(const StringId64* channelNames,
								 U32F numChannels,
								 ndanim::JointParams* pOutChannelJoints,
								 const SnapshotEvaluateParams& params /* = SnapshotEvaluateParams() */) const
{
	PROFILE(Animation, SnapshotEvaluate);

	for (U32F ii = 0; ii < numChannels; ++ii)
	{
		pOutChannelJoints[ii].m_trans = kOrigin;
		pOutChannelJoints[ii].m_quat	 = kIdentity;
		pOutChannelJoints[ii].m_scale = Vector(1.0f, 1.0f, 1.0f);
	}

	const AnimSnapshotNode* pRootNode = GetSnapshotNode(m_rootNodeIndex);
	ANIM_ASSERT(pRootNode);
	if (!pRootNode)
	{
		return 0;
	}

	SnapshotEvaluateParams evaluateParams(params);
	evaluateParams.m_channelName = INVALID_STRING_ID_64;
	evaluateParams.m_pRemapLayer = params.m_pRemapLayer;

	U32F evaluatedChannels = 0;

	for (U32F ii = 0; ii < numChannels; ++ii)
	{
		// Select a new channel to evaluate
		evaluateParams.m_channelName = channelNames[ii];

		if (params.m_flipped && (m_animState.m_flags & DC::kAnimStateFlagSwapMirroredChannelPairs) != 0)
		{
			evaluateParams.m_channelName = LookupMirroredChannelPair(channelNames[ii]);
		}

		if (pRootNode->EvaluateChannel(this, &pOutChannelJoints[ii], evaluateParams))
		{
			evaluatedChannels |= (1 << ii);
		}

		const bool channelSwapped = (evaluateParams.m_channelName != channelNames[ii]);
		if (channelSwapped)
		{
			pOutChannelJoints[ii].m_quat = RotateSwappedChannelPair(pOutChannelJoints[ii].m_quat);
		}
	}
	
	return evaluatedChannels;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimStateSnapshot::EvaluateDelta(const StringId64* channelNames,
									  U32F numChannels,
									  ndanim::JointParams* pOutChannelJoints,
									  const SnapshotEvaluateParams& params /* = SnapshotEvaluateParams() */) const
{
	for (U32F ii = 0; ii < numChannels; ++ii)
	{
		pOutChannelJoints[ii].m_trans = kOrigin;
		pOutChannelJoints[ii].m_quat	 = kIdentity;
		pOutChannelJoints[ii].m_scale = Vector(1.0f, 1.0f, 1.0f);
	}

	const AnimSnapshotNode* pRootNode = GetSnapshotNode(m_rootNodeIndex);
	ANIM_ASSERT(pRootNode);
	if (!pRootNode)
	{
		return 0;
	}

	SnapshotEvaluateParams evaluateParams(params);
	evaluateParams.m_channelName = INVALID_STRING_ID_64;

	U32F evaluatedChannels = 0;

	for (U32F ii = 0; ii < numChannels; ++ii)
	{
		// Select a new channel to evaluate
		evaluateParams.m_channelName = channelNames[ii];

		if (pRootNode->EvaluateChannelDelta(this, &pOutChannelJoints[ii], evaluateParams))
		{
			evaluatedChannels |= (1 << ii);
		}
	}

	return evaluatedChannels;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateSnapshot::IsFlipped(const DC::AnimInfoCollection* pInfoCollection) const
{
	bool flipped = false;
	if (m_animState.m_flipFunc)
	{
		DC::AnimInfoCollection scriptInfoCollection(*pInfoCollection);
		scriptInfoCollection.m_actor = scriptInfoCollection.m_actor;
		scriptInfoCollection.m_instance = scriptInfoCollection.m_instance;
		scriptInfoCollection.m_top = scriptInfoCollection.m_top;

		const ScriptValue argv[] ={ScriptValue(&m_animState), ScriptValue(&scriptInfoCollection)};

		const DC::ScriptLambda* pLambda = m_animState.m_flipFunc;
		VALIDATE_LAMBDA(pLambda, "anim-state-flip-func", m_animState.m_name.m_symbol);
		flipped = ScriptManager::Eval(pLambda, SID("anim-state-flip-func"), ARRAY_COUNT(argv), argv).m_boolean;
	}

	return flipped;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateSnapshot::GetTriggeredEffects(const DC::AnimInfoCollection* pInfoCollection,
											float oldPhase,
											float newPhase,
											bool isFlipped,
											float effectiveFade,
											bool isTopState,
											bool isReversed,
											StringId64 stateId,
											float animBlend,
											EffectList* pTriggeredEffects,
											const AnimInstance* pInstance,
											float updateRate) const
{
	PROFILE(Animation, AnimStateGetTriggeredEffects);
	EffectUpdateStruct effectParams;
	effectParams.m_info = pInfoCollection->m_actor;
	effectParams.m_instanceInfo = pInfoCollection->m_instance;
	effectParams.m_oldPhase = oldPhase;
	effectParams.m_newPhase = newPhase;
	effectParams.m_isFlipped = isFlipped;
	effectParams.m_isTopState = isTopState;
	effectParams.m_isReversed = isReversed;
	effectParams.m_pTriggeredEffects = pTriggeredEffects;
	effectParams.m_stateEffectiveFade = effectiveFade;
	effectParams.m_minBlend = m_effMinBlend;
	effectParams.m_isMotionMatching = (stateId == SID("s_motion-match-locomotion"));
	effectParams.m_animBlend = animBlend;

	const AnimSnapshotNode* pRootNode = GetSnapshotNode(m_rootNodeIndex);
	pRootNode->GetTriggeredEffects(this, effectParams, animBlend, pInstance);

	const F32 durationEffective = (pInstance ? pInstance->GetDuration() : 1.0f) * updateRate;
	pTriggeredEffects->SetPhaseRange(oldPhase, newPhase, durationEffective);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Transform AnimStateSnapshot::GetAnimDeltaTweakTransform() const
{
	ANIM_ASSERT(m_pAnimDeltaTweakXform);
	const Transform* pAnimDeltaXformLs = m_pAnimDeltaTweakXform;
	return *pAnimDeltaXformLs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateSnapshot::SetAnimDeltaTweakTransform(const Transform& xform)
{
	ANIM_ASSERT(m_pAnimDeltaTweakXform);
	*m_pAnimDeltaTweakXform = xform;
}
