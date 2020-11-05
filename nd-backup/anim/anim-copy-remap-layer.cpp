/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-copy-remap-layer.h"

#include "corelib/memory/relocate.h"

#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/nd-anim-util.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-anim.h"
#include "gamelib/level/art-item-skeleton.h"

/// --------------------------------------------------------------------------------------------------------------- ///
AnimCopyRemapLayer::AnimCopyRemapLayer(AnimTable* pAnimTable, AnimOverlaySnapshot* pOverlaySnapshot)
: AnimLayer(kAnimLayerTypeCopyRemap, pAnimTable, pOverlaySnapshot)
{
}

	/// --------------------------------------------------------------------------------------------------------------- ///
void AnimCopyRemapLayer::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	AnimLayer::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimCopyRemapLayer::IsValid() const
{
	return m_hTargetGameObject.HandleValid();
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimCopyRemapLayer::GetNumFadesInProgress() const
{
	return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimCopyRemapLayer::Setup(StringId64 name, ndanim::BlendMode blendMode)
{
	ANIM_ASSERTF(false, ("This variant of AnimLayer::Setup() is not intended to be used on AnimSnapshotLayer"));
	Setup(name, blendMode, nullptr, nullptr, INVALID_STRING_ID_64, INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimCopyRemapLayer::Setup(StringId64 name,
							   ndanim::BlendMode blendMode,
							   const NdGameObject* pOwner,
							   const NdGameObject* targetObject,
							   StringId64 targetLayerId,
							   StringId64 symbolMap)
{
	AnimLayer::Setup(name, blendMode);

	m_numLayers = 1;
	m_layerFades[0] = 1.0f;
	m_layerFadeTime[0] = 0.0f;
	m_hOwnerGameObject = pOwner;
	m_hTargetGameObject = targetObject;
	m_targetLayerName	= targetLayerId;
	m_symbolMap[0]	= ScriptPointer<DC::Map>(symbolMap);
}

void AnimCopyRemapLayer::SetRemapId(StringId64 remapId, F32 blend)
{
	if (blend == 0.0f)
	{
		m_symbolMap[0] = ScriptPointer<DC::Map>(remapId);
		m_layerFades[0] = 1.0f;
		m_layerFadeTime[0] = 0.0f;
		m_numLayers = 1;
		return;
	}

	GAMEPLAY_ASSERT(m_numLayers < kMaxLayers);
	if (m_numLayers >= kMaxLayers)
		return;

	m_symbolMap[m_numLayers] = ScriptPointer<DC::Map>(remapId);
	m_layerFades[m_numLayers] = 0.0f;
	m_layerFadeTime[m_numLayers] = blend;
	m_numLayers++;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateLayer* AnimCopyRemapLayer::GetTargetLayer() const
{
	ANIM_ASSERT(m_hTargetGameObject.ToProcess());
	const AnimControl* pTargetAnimControl = m_hTargetGameObject.ToProcess()->GetAnimControl();
	const AnimStateLayer* pAnimLayer	  = pTargetAnimControl->GetStateLayerById(m_targetLayerName);
	ANIM_ASSERT(pAnimLayer);
	return pAnimLayer;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimCopyRemapLayer::DebugPrint(MsgOutput output, U32 priority, const FgAnimData* pAnimData) const
{
	if (!IsValid() || !Memory::IsDebugMemoryAvailable())
		return;

	if (!g_animOptions.m_debugPrint.ShouldShow(GetName()))
		return;

	SetColor(output, 0xFF000000 | 0x00FFFFFF);
	PrintTo(output, "-----------------------------------------------------------------------------------------\n");

	SetColor(output, 0xFF000000 | 0x0055FF55);
	if (!g_animOptions.m_debugPrint.m_simplified)
	{
		PrintTo(output,
				"AnimCopyRemapLayer \"%s\": [%s], Pri %d, Cur Fade: %1.2f, Des Fade: %1.2f\n",
				DevKitOnly_StringIdToString(m_name),
				m_blendMode == ndanim::kBlendSlerp ? "blend" : "additive",
				priority,
				GetCurrentFade(),
				GetDesiredFade());
	}
	else
	{
		if (!g_animOptions.m_debugPrint.m_hideNonBaseLayers)
			PrintTo(output, "AnimCopyRemapLayer \"%s\"\n", DevKitOnly_StringIdToString(m_name));
	}

	const bool additiveLayer = m_blendMode == ndanim::kBlendAdditive;
	const bool baseLayer	 = false;

	const AnimStateLayer* pAnimLayer = GetTargetLayer();

	for (U32F i = 0; i < pAnimLayer->NumUsedTracks(); ++i)
	{
		const AnimStateInstanceTrack* pTrack = pAnimLayer->GetTrackByIndex(i);

		SetColor(output, 0xFF000000 | 0x0055FF55);
		if (!g_animOptions.m_debugPrint.m_simplified)
		{
			PrintTo(output,
					"  COPIED AnimStateInstanceTrack: Anim Fade: %1.2f, Motion Fade: %1.2f\n",
					pTrack->AnimFade(),
					pTrack->MotionFade());
		}
		else
		{
			PrintTo(output, "Blend %1.2f/1.0\n", pTrack->AnimFade());
		}

		pTrack->DebugPrint(output, additiveLayer, baseLayer, pAnimData, this);
	}
} 

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimCopyRemapLayer::RemapAnimation(StringId64 animName, I32 layerIndex) const
{
	if (m_symbolMap[layerIndex])
	{
		const StringId64* sid = ScriptManager::MapLookup<StringId64>(m_symbolMap[layerIndex], animName);
		if (sid)
		{
			return *sid;
		}

		sid = ScriptManager::MapLookup<StringId64>(m_symbolMap[layerIndex], SID("default"));

		if (sid)
		{
			return *sid;
		}
	}

	return animName;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ArtItemAnim* AnimCopyRemapLayer::GetRemappedArtItem(const ArtItemAnim* pArtItemAnim, I32 layerIndex) const
{
	if (pArtItemAnim && m_symbolMap[layerIndex])
	{
		const StringId64* sid = ScriptManager::MapLookup<StringId64>(m_symbolMap[layerIndex], pArtItemAnim->GetNameId());
		if (sid)
		{
			const ArtItemAnim* pNewItem = m_pAnimTable->LookupAnim(*sid).ToArtItem();
			if (pNewItem)
				return pNewItem;
		}
		
		sid = ScriptManager::MapLookup<StringId64>(m_symbolMap[layerIndex], SID("default"));

		if (sid)
		{
			const ArtItemAnim* pNewItem = m_pAnimTable->LookupAnim(*sid).ToArtItem();
			if (pNewItem)
				return pNewItem;
		}
	}

	return pArtItemAnim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimCopyRemapLayer::BeginStep(F32 deltaTime, EffectList* pTriggeredEffects, const FgAnimData*)
{
	AnimLayer::BeginStep(deltaTime, pTriggeredEffects, nullptr);

	for (int i = 1; i < m_numLayers; i++)
	{
		m_layerFades[i] += deltaTime * (1.0f / m_layerFadeTime[i]);
		if (m_layerFades[i] >= 1.0f)
		{
			m_layerFades[i] = 1.0f;
		}
	}

	for (int i = 1; i < m_numLayers; i++)
	{
		if (m_layerFades[i] >= 1.0f)
		{
			for (int j = i; j < m_numLayers; j++)
			{
				m_symbolMap[j - i] = m_symbolMap[j];
				m_layerFades[j - i] = m_layerFades[j];
				m_layerFadeTime[j - i] = m_layerFadeTime[j];
			}
			
			m_numLayers = m_numLayers - i;
			break;
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void AnimCopyRemapLayer::CreateAnimCmds(const AnimCmdGenLayerContext& context, AnimCmdList* pAnimCmdList) const
{
	ANIM_ASSERT(context.m_instanceZeroIsValid);
	const AnimStateLayer* pAnimLayer = GetTargetLayer();
	U32* buffer = (U32*)pAnimCmdList->GetBuffer() + pAnimCmdList->GetNumWordsUsed();
	const AnimControl* pOwnerAnimControl = m_hOwnerGameObject.ToProcess()->GetAnimControl();
	const AnimControl* pTargetAnimControl = m_hTargetGameObject.ToProcess()->GetAnimControl();

	AnimCmdGenLayerContext tgtContext = context;
	tgtContext.m_pAnimData = &pTargetAnimControl->GetAnimData();

	for (int i=0;i<m_numLayers;i++)
	{
		pAnimLayer->CreateAnimCmds(tgtContext, pAnimCmdList, pTargetAnimControl->GetInfo(), this->GetCurrentFade() * m_layerFades[i]);

		U32* bufferend = (U32*)pAnimCmdList->GetBuffer() + pAnimCmdList->GetNumWordsUsed();

		while (buffer < bufferend)
		{
			AnimCmd* pCmd = (AnimCmd*)buffer;

			switch (pCmd->m_type)
			{
				case AnimCmd::kEvaluateClip:
				{
					AnimCmd_EvaluateClip* clip = (AnimCmd_EvaluateClip*)pCmd;

					const ArtItemAnim* pArtItemAnim = GetRemappedArtItem(clip->m_pArtItemAnim, i);

					clip->m_pArtItemAnim = pArtItemAnim;

					break;
				}

				case AnimCmd::kInitializeJointSet_AnimPhase:
				{
					AnimCmd_InitializeJointSet_AnimPhase* pCmdInitJointSet = static_cast<AnimCmd_InitializeJointSet_AnimPhase*>(pCmd);
					pCmdInitJointSet->m_pJointSet = pOwnerAnimControl->GetAnimData().m_pPluginJointSet;
					break;
				}

				case AnimCmd::kInitializeJointSet_RigPhase:
				{
					AnimCmd_InitializeJointSet_RigPhase* pCmdInitJointSet = static_cast<AnimCmd_InitializeJointSet_RigPhase*>(pCmd);
					pCmdInitJointSet->m_pJointSet = pOwnerAnimControl->GetAnimData().m_pPluginJointSet;
					break;
				}

				case AnimCmd::kCommitJointSet_AnimPhase:
				{
					AnimCmd_CommitJointSet_AnimPhase* pCmdCommitJointSet = static_cast<AnimCmd_CommitJointSet_AnimPhase*>(pCmd);
					pCmdCommitJointSet->m_pJointSet = pOwnerAnimControl->GetAnimData().m_pPluginJointSet;
					break;
				}

				case AnimCmd::kCommitJointSet_RigPhase:
				{
					AnimCmd_CommitJointSet_RigPhase* pCmdCommitJointSet = static_cast<AnimCmd_CommitJointSet_RigPhase*>(pCmd);
					pCmdCommitJointSet->m_pJointSet = pOwnerAnimControl->GetAnimData().m_pPluginJointSet;
					break;
				}

				case AnimCmd::kApplyJointLimits:
				{
					AnimCmd_ApplyJointLimits* pCmdApplyJointLimits = static_cast<AnimCmd_ApplyJointLimits*>(pCmd);
					pCmdApplyJointLimits->m_pJointSet = pOwnerAnimControl->GetAnimData().m_pPluginJointSet;
					break;
				}

				case AnimCmd::kEvaluateAnimPhasePlugin:
				{
					AnimCmd_EvaluateAnimPhasePlugin* pCmdEvalPlugin = static_cast<AnimCmd_EvaluateAnimPhasePlugin*>(pCmd);
					pCmdEvalPlugin->m_pPluginJointSet = pOwnerAnimControl->GetAnimData().m_pPluginJointSet;
					break;
				}

				case AnimCmd::kEvaluateRigPhasePlugin:
				{
					AnimCmd_EvaluateRigPhasePlugin* pCmdEvalPlugin = static_cast<AnimCmd_EvaluateRigPhasePlugin*>(pCmd);
					pCmdEvalPlugin->m_pPluginJointSet = pOwnerAnimControl->GetAnimData().m_pPluginJointSet;
					break;
				}

			}

			ANIM_ASSERT(pCmd->m_numCmdWords > 0);
			buffer += pCmd->m_numCmdWords;
		}
	}
}
