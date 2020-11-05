/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/character-speech-anim.h"

#include "corelib/memory/relocate.h"

#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/process/clock.h"
#include "ndlib/profiling/profiling.h"

#include "gamelib/actor-viewer/nd-actor-viewer-object.h"
#include "gamelib/audio/lipsync.h"
#include "gamelib/audio/nd-vox-controller-base.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/emotion-control.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"

float g_emotionBlendInDefault = 0.5f;

/// --------------------------------------------------------------------------------------------------------------- ///
CharacterSpeechAnim::CharacterSpeechAnim(NdGameObject* pCharacter)
{
	Reset(pCharacter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterSpeechAnim::Reset(NdGameObject* pCharacter)
{
	m_pCharacter = pCharacter;

	for (I32F i = 0; i < kNdPhonemeCount; i++)
	{
		m_phonemeBlend[i] = 0.0f;
		m_phonemeSpring[i].Reset();
		m_phonemeEmotion[i] = INVALID_STRING_ID_64;
	}

	m_fadeMult = 1.0f;

	const FgAnimData* pAnimData		 = pCharacter->GetAnimData();
	const ArtItemSkeleton* pCurSkel	 = pAnimData->m_curSkelHandle.ToArtItem();
	const ArtItemSkeleton* pAnimSkel = pAnimData->m_animateSkelHandle.ToArtItem();

	if (pAnimSkel)
	{
		m_skelId	  = pAnimSkel->m_skelId;
		m_hierarchyId = pAnimSkel->m_hierarchyId;
	}
	else if (pCurSkel)
	{
		m_skelId	  = pCurSkel->m_skelId;
		m_hierarchyId = pCurSkel->m_hierarchyId;
	}
	else
	{
		m_skelId	  = INVALID_SKELETON_ID;
		m_hierarchyId = 0;
	}

	m_phonemeAnim  = ArtItemAnimHandle();
	m_bindPoseAnim = ArtItemAnimHandle();

	m_atActionCounter = -1;
	m_stActionCounter = -1;	 
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterSpeechAnim::Step(F32 deltaTime)
{
	PROFILE(Animation, CharSpeechAnim_Step);

	const FgAnimData* pAnimData = m_pCharacter->GetAnimData();
	const ArtItemSkeleton* pAnimSkel = pAnimData ? pAnimData->m_animateSkelHandle.ToArtItem() : nullptr;
	TryRefreshAnimPointers(pAnimSkel);

	if (EmotionControl* pEmotionControl = m_pCharacter->GetEmotionControl())  
	{
		StringId64 currentEmotion = m_pCharacter->GetVoxController().GetCurrentEmotion();

		if (currentEmotion != INVALID_STRING_ID_64 || m_pCharacter->GetVoxController().IsSpeaking())
		{
			I32 priority = kEmotionPriorityCharacter;
			if (currentEmotion == INVALID_STRING_ID_64)
			{
				currentEmotion = SID("neutral");
				priority = kEmotionPriorityUnspecified;
			}

			if (currentEmotion == SID("neutral"))
			{
				StringId64 baseEmotion = m_pCharacter->GetBaseEmotion();
				if (baseEmotion != INVALID_STRING_ID_64)
					currentEmotion = baseEmotion;
			}

			pEmotionControl->SetEmotionalState(currentEmotion, -1.0f, priority, -1.0f, -1.0f);
		}
	}

	if (m_phonemeAnim.IsNull())
	{
		// Fade out in error condition, because otherwise we'd be stuck faded in and apply a facial bindpose all the time.
		m_fadeMult = 0.0f;
		return;
	}

	const int kMaxLayers = 20;
	NdPhoneme phonemeIndex[kMaxLayers];
	float animBlend[kMaxLayers];

	int numAnims;
	
	//--------------------------------------------
	// fill in data from lipsync stuff

	numAnims = 0;
	F32 fTotalWeight = 0.0f;
	{

		PROFILE(Animation, CharSpeechAnim_GetData);		
		NdVoxControllerBase& rVoxController = m_pCharacter->GetVoxController();
		NdLipsyncArticulationBlend artic = rVoxController.GetLipsyncArticulation();		
		if (artic.m_uCurTime != 0xffffffffU)
		{
			I64 range = artic.m_end.m_uStartTime - artic.m_start.m_uStartTime;
			ANIM_ASSERT(artic.m_uCurTime >= artic.m_start.m_uStartTime);
			const float endWeight = (range == 0  || !m_enableVisemeBlending) ? 0.0f : float(artic.m_uCurTime - artic.m_start.m_uStartTime) / range;
			const float startWeight = 1.0f - endWeight;
			
			//MsgCon("StartWeight: %s, %f\n", DevKitOnly_StringIdToString(m_pCharacter->GetUserId()), startWeight);

			for (I32F iPhoneme = 0; (iPhoneme < LIPSYNC_MAX_PPA) && (iPhoneme < kMaxLayers); iPhoneme++)
			{
				NdPhoneme phoneme = artic.m_start.m_auPhonemes[iPhoneme];
				U8 weight = artic.m_start.m_auWeights[iPhoneme];
				if (phoneme != kNdPhonemeInvalid)
				{
					phonemeIndex[numAnims] = phoneme;
					animBlend[numAnims] = (F32)weight / 100.0f*startWeight;
					fTotalWeight += animBlend[numAnims];
					numAnims++;
				}
			}
			for (I32F iPhoneme = 0; (iPhoneme < LIPSYNC_MAX_PPA) && (iPhoneme < kMaxLayers); iPhoneme++)
			{
				NdPhoneme phoneme = artic.m_end.m_auPhonemes[iPhoneme];
				U8 weight = artic.m_end.m_auWeights[iPhoneme];
				if (phoneme != kNdPhonemeInvalid)
				{
					I32F startPhoneme = 0;
					for (startPhoneme = 0; startPhoneme < numAnims; ++startPhoneme)
					{
						if (phonemeIndex[startPhoneme] == phoneme)
						{
							break;
						}
					}
					phonemeIndex[startPhoneme] = phoneme;
					const float curWeight = (F32)weight / 100.0f*endWeight;					
					fTotalWeight += curWeight;
					if (startPhoneme == numAnims)
					{
						animBlend[startPhoneme] = curWeight;
						numAnims++;
					}
					else
					{ 
						animBlend[startPhoneme] += curWeight;
					}
				}
			}
		}
		else
		{
			int debug = 123;
		}
	}

	const EmotionalState emotionState = GetEmotionalState();

	for (int iAnim = 0; iAnim < numAnims; ++iAnim)
	{
		NdPhoneme phoneme = phonemeIndex[iAnim];

		// only update a phoneme's emotion state if it had very low
		// contribution last frame, in order to prevent animation pops
		static const float kEmotionSwitchThreshold = 0.01f;
		if (m_phonemeBlend[phoneme] < kEmotionSwitchThreshold)
		{
			m_phonemeEmotion[phoneme] = emotionState.m_emotion;
		}
	}

//	DebugPhonemeEmotionCache(numAnims, phonemeIndex, animBlend);

	// end data
	//--------------------------------------------
	if (m_useShippedBlendingCode)
	{
		// now blend all of our weights
		float totalWeight = 0.0f;
		{
			PROFILE(Animation, CharSpeechAnim_Blend);
			const float dt = GetProcessDeltaTime();

			bool isMurmurating = m_pCharacter->GetVoxController().IsMurmurating();

			for (int i = 0; i < kNdPhonemeCount; i++)
			{
				bool isActive = false;
				for (int j = 0; j < numAnims; j++)
				{
					if (phonemeIndex[j] == i)
					{
						m_phonemeBlend[i] = m_phonemeSpring[i].Track(m_phonemeBlend[i], animBlend[j], dt, isMurmurating ? 30.0f : 100.0f);
						isActive = true;
					}
				}

				if (!isActive)
				{
					m_phonemeBlend[i] = m_phonemeSpring[i].Track(m_phonemeBlend[i], 0.0f, dt, 30.0f);
				}

				totalWeight += m_phonemeBlend[i];
			}
		}

		m_fadeMult = 1.0f; // MinMax01(totalWeight);	// Per Travis M, we should now have this additive layer always blended in
		m_fadeMultSpring.Reset();
		return;
	}
	// now blend all of our weights
	float totalWeight = 0.0f;
	float desiredFade = 0.0f;
	const float dt = GetProcessDeltaTime();
	if (numAnims > 0)
	{
		desiredFade = 1.0f;
		PROFILE(Animation, CharSpeechAnim_Blend);
		bool fadedOut = m_fadeMult < 0.01f;

		for (int i = 0; i < kNdPhonemeCount; i++)
		{
			bool isActive = false;
			for (int j=0; j<numAnims; j++)
			{
				if (phonemeIndex[j] == i)
				{
					if (m_disableVisemeSprings)
						m_phonemeBlend[i] = animBlend[j];
					else
					{						
						if (fadedOut)
						{
							m_phonemeBlend[i] = animBlend[j];
							m_phonemeSpring[i].Reset();
						}
						else
						{
							m_phonemeBlend[i] = m_phonemeSpring[i].Track(m_phonemeBlend[i], animBlend[j], dt, 100.0f);
						}
					}

					isActive = true;
				}
			}

			if (!isActive)
			{
				if (m_disableVisemeSprings)
					m_phonemeBlend[i] = 0.0f;
				else
				{
					if (fadedOut)
					{
						m_phonemeBlend[i] = 0.0f;
						m_phonemeSpring[i].Reset();
					}
					else
					{
						m_phonemeBlend[i] = m_phonemeSpring[i].Track(m_phonemeBlend[i],
																	 0.0f,
																	 dt,
																	 numAnims > 0 ? 100.0f : 1.0f);
						m_phonemeBlend[i] = Max(m_phonemeBlend[i], 0.0f);
					}
				}
			}

			totalWeight += m_phonemeBlend[i];
		}
	}

	m_fadeMult = m_fadeMultSpring.Track(m_fadeMult, desiredFade, dt, desiredFade > 0.5f ? 100.0f : 30.f);
	m_fadeMult = MinMax01(m_fadeMult);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterSpeechAnim::DebugPhonemeEmotionCache(int numPhonemes, NdPhoneme phonemes[], float blend[]) const
{
	if (!m_pCharacter->IsPlayer())
		return;

	NdVoxControllerBase& rVoxController = m_pCharacter->GetVoxController();
	NdLipsyncArticulationBlend artic = rVoxController.GetLipsyncArticulation();
	MsgCon("Cur Articulation: CurTime: %d\n", artic.m_uCurTime);
	MsgCon("                  Start: Time: %4d  ", artic.m_start.m_uStartTime);
	for (I32F i = 0; i < LIPSYNC_MAX_PPA; i++)
	{
		MsgCon("%2d (%3.2f)", artic.m_start.m_auPhonemes[i], artic.m_start.m_auWeights[i] / 100.0f);
		if (i != LIPSYNC_MAX_PPA - 1) MsgCon(", ");
	}
	MsgCon("\n                  End:   Time: %4d  ", artic.m_end.m_uStartTime);
	for (I32F i = 0; i < LIPSYNC_MAX_PPA; i++)
	{
		MsgCon("%2d (%3.2f)", artic.m_end.m_auPhonemes[i], artic.m_end.m_auWeights[i] / 100.0f);
		if (i != LIPSYNC_MAX_PPA - 1) MsgCon(", ");
	}
	MsgCon("\n");

	if (numPhonemes == 0)
	{
		MsgCon("No phonemes playing\n");
	}
	else
	{
		MsgCon("Playing phonemes: ");
		for (int i = 0; i < numPhonemes; ++i)
		{
			MsgCon("%2d (%s, %3.2f)", phonemes[i], DevKitOnly_StringIdToString(m_phonemeEmotion[phonemes[i]]), blend[i]);
			if (i != numPhonemes - 1) MsgCon(", ");
		}
		MsgCon("\n");
	}

	MsgCon("Player Emotion: %s\n", DevKitOnly_StringIdToString(GetEmotionalState().m_emotion));
	MsgCon("Phoneme emotion cache:\n");
	for (int i = 0; i < kNdPhonemeCount; ++i)
	{
		if (m_phonemeEmotion[i] != INVALID_STRING_ID_64)
		{
			MsgCon("    Phoneme %2d: %-20s (%3.2f)\n", i, DevKitOnly_StringIdToString(m_phonemeEmotion[i]), m_phonemeBlend[i]);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterSpeechAnim::TryRefreshAnimPointers(const ArtItemSkeleton* pAnimateSkel)
{
	if (!pAnimateSkel)
	{
		m_bindPoseAnim = ArtItemAnimHandle();
		m_phonemeAnim  = ArtItemAnimHandle();
		return;
	}

	const EmotionalState emotionState = GetEmotionalState();

	if (m_atActionCounter == AnimMasterTable::m_actionCounter && m_stActionCounter == SkelTable::m_actionCounter
		&& m_skelId == pAnimateSkel->m_skelId && m_hierarchyId == pAnimateSkel->m_hierarchyId)
	{
		return;
	}

	//ANIM_ASSERT(m_skelId == pAnimateSkel->m_skelId);
	if (m_skelId != pAnimateSkel->m_skelId)
	{
		return;
	}

	ANIM_ASSERT(m_hierarchyId == pAnimateSkel->m_hierarchyId);

	m_skelId	  = pAnimateSkel->m_skelId;
	m_hierarchyId = pAnimateSkel->m_hierarchyId;

	m_phonemeAnim  = AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, SID("phonemes-neutral-full-set-01"), false);
	m_bindPoseAnim = AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, SID("bindpose-face-null-add"), false);

	const ArtItemAnim* pBindPoseAnim = m_bindPoseAnim.ToArtItem();

	const ndanim::JointHierarchy* const pJointHier = pAnimateSkel->m_pAnimHierarchy;

	if (pBindPoseAnim && pJointHier)
	{
		if (!pBindPoseAnim->m_pClipData)
		{
			m_bindPoseAnim = ArtItemAnimHandle();
		}
		else if (pBindPoseAnim->m_pClipData->m_animHierarchyId != pJointHier->m_animHierarchyId)
		{
			m_bindPoseAnim = ArtItemAnimHandle();
		}
	}
	else if (!pJointHier)
	{
		m_bindPoseAnim = ArtItemAnimHandle();
	}

	m_atActionCounter = AnimMasterTable::m_actionCounter;
	m_stActionCounter = SkelTable::m_actionCounter;	
}

/// --------------------------------------------------------------------------------------------------------------- ///
float CharacterSpeechAnim::GetFadeMult() const
{
	return m_fadeMult;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterSpeechAnim::CreateAnimCmds(const AnimCmdGenLayerContext& context,
										 AnimCmdList* pAnimCmdList,
										 U32F outputInstance) const
{
	PROFILE(Animation, CharacterSpeechAnim_CreateAnimCmds);

	if (m_hierarchyId != context.m_pAnimateSkel->m_hierarchyId)
	{
		pAnimCmdList->AddCmd_EvaluateBindPose(outputInstance);
		return;
	}

	// figure out the unnormalized weighting to produce our normalized blends
	float phonemeSum = 0.0f;
	for (I32F i = 0; i < kNdPhonemeCount; i++)
	{
		phonemeSum += m_phonemeBlend[i];
	}
	// if sum > 1.0, normalize.
	// Otherwise, don't increase the blend values.
	float phonemeNorm = Max(1.0f, phonemeSum);
	
	float animBlendNonNormalized[kNdPhonemeCount];
	float totalBlendLeft = 1.0f;
	for (I32F i = kNdPhonemeCount - 1; i >= 0; i--)
	{
		if (totalBlendLeft > 0.01f)
		{
			float phonemeNormalized = m_phonemeBlend[i] / phonemeNorm;
			ANIM_ASSERT(IsFinite(phonemeNormalized));
			
			animBlendNonNormalized[i] = phonemeNormalized / totalBlendLeft;
			ANIM_ASSERT(IsFinite(animBlendNonNormalized[i]));
			//ANIM_ASSERT(animBlendNonNormalized[i] < 2.00f);
			animBlendNonNormalized[i] = Min(animBlendNonNormalized[i], 1.0f);
			totalBlendLeft *= (1.0f - phonemeNormalized);
		}
		else
		{
			animBlendNonNormalized[i] = 0.0f;
		}
	}

	const ndanim::JointHierarchy* const pJointHier = context.m_pAnimateSkel->m_pAnimHierarchy;

	const ArtItemAnim* pBindPoseAnim = m_bindPoseAnim.ToArtItem();

 	if (pBindPoseAnim && (pBindPoseAnim->m_pClipData->m_animHierarchyId == pJointHier->m_animHierarchyId))
 	{
 		// Prefer this bindpose that has only the phoneme joints set.
 		pAnimCmdList->AddCmd_EvaluateClip(pBindPoseAnim, outputInstance, 0.0f);
 		pAnimCmdList->AddCmd_EvaluateCopy(outputInstance, outputInstance + 1);
 	}
 	else
	{
		pAnimCmdList->AddCmd_EvaluateEmptyPose(outputInstance);
	}

	bool generatedOutput = false;

	for (I32F i = 0; i < kNdPhonemeCount; i++)
	{
		if (animBlendNonNormalized[i] <= 0.01f)
			continue;

		if (i == kNdPhoneme_x && m_enableTransparentXPhoneme)
		{
			pAnimCmdList->AddCmd_EvaluateCopy(0, outputInstance + 1);
			generatedOutput = true;
		}
		else if (i > 0)
		{
			const StringId64 animId = GetPhonemeAnimationId(m_pCharacter, m_phonemeEmotion[i]);
			ArtItemAnimHandle phonemeAnim = AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, animId, false);
			const ArtItemAnim* pPhonemeAnim = phonemeAnim.ToArtItem();

			if (pPhonemeAnim && (pPhonemeAnim->m_pClipData->m_animHierarchyId == pJointHier->m_animHierarchyId))
			{
				pAnimCmdList->AddCmd_EvaluateClip(pPhonemeAnim, outputInstance + 1, (float)(i - 1));
				generatedOutput = true;
			}
		}

		if (generatedOutput)
		{
			pAnimCmdList->AddCmd_EvaluateBlend(outputInstance,
											   outputInstance + 1,
											   outputInstance,
											   ndanim::kBlendSlerp,
											   animBlendNonNormalized[i]);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterSpeechAnim::DebugPrint(MsgOutput output) const
{
	STRIP_IN_FINAL_BUILD;

	if (m_phonemeAnim.IsNull())
	{
		SetColor(output, kColorRed);
		PrintTo(output, "    'phonemes-neutral-full-set-01' is MISSING\n");
		SetColor(output, Color(0xFF000000 | 0x0055FF55));
		return;
	}

	if (g_animOptions.m_debugPrint.m_simplified)
		return;

	PrintTo(output, "  Phoneme Anims:\n");

	for (I32F i = 0; i < kNdPhonemeCount; i++)
	{
		if (m_phonemeBlend[i] < 0.1f)
			continue;

		const StringId64 animId = GetPhonemeAnimationId(m_pCharacter, m_phonemeEmotion[i]);
		ArtItemAnimHandle phonemeAnim = AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, animId, false);
		const ArtItemAnim* pPhonemeAnim = phonemeAnim.ToArtItem();

		if (!pPhonemeAnim)
		{
			SetColor(output, kColorRed);
			PrintTo(output, "    Phoneme: %2d <missing> (%s)", i, DevKitOnly_StringIdToString(animId));
			SetColor(output, Color(0xFF000000 | 0x0055FF55));
		}
		else if (pPhonemeAnim->m_pClipData->m_animHierarchyId != m_hierarchyId)
		{
			SetColor(output, kColorRed);

			if (const ArtItemSkeleton* pOtherSkel = ResourceTable::LookupSkel(pPhonemeAnim->m_pClipData->m_animHierarchyId).ToArtItem())
			{
				PrintTo(output,
					"    Phoneme: %2d  <wrong skel '%s' [0x%x]>", i,
					ResourceTable::LookupSkelName(pOtherSkel->m_skelId),
					pPhonemeAnim->m_pClipData->m_animHierarchyId);
			}
			else
			{
				PrintTo(output, "    Phoneme: %2d  <wrong skel 0x%x>", i, pPhonemeAnim->m_pClipData->m_animHierarchyId);
			}

			SetColor(output, Color(0xFF000000 | 0x0055FF55));
		}
		else
		{
			PrintTo(output, "    Phoneme: %2d %s blend: %3.2f", i, pPhonemeAnim->GetName(), m_phonemeBlend[i]);
		}

		PrintTo(output, "\n");
	}

	if (const ArtItemSkeleton* pSkel = ResourceTable::LookupSkel(m_hierarchyId).ToArtItem())
	{
		PrintTo(output, " (skel: '%s' [0x%x])\n", ResourceTable::LookupSkelName(pSkel->m_skelId), m_hierarchyId);
	}
	else
	{
		PrintTo(output, " (<skel not loaded> [0x%x])\n", m_hierarchyId);
	}

	if (const ArtItemAnim* pBindPoseAnim = m_bindPoseAnim.ToArtItem())
	{
		PrintTo(output, "  BindPose Anim: %s\n", pBindPoseAnim->GetName());
	}
	else
	{
		PrintTo(output, "  BindPose Anim: <empty pose>\n");
	}

	PrintTo(output, "  Fade: %f\n", m_fadeMult);

	for (I32F i = 0; i < kNdPhonemeCount; i++)
	{
		if (m_phonemeBlend[i] > 0.01f)
		{
			PrintTo(output, " + (%s : %f)", GetPhonemeName(NdPhoneme(i)), m_phonemeBlend[i]);
		}
	}

	PrintTo(output,
			"  %s %s %s\n",
			m_useShippedBlendingCode ? "Shipping Code" : "",
			m_enableTransparentXPhoneme ? "Transparent X" : "",
			m_enableVisemeBlending ? "Viseme Blending" : "");
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterSpeechAnim::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pCharacter, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
EmotionalState CharacterSpeechAnim::GetEmotionalState() const
{
	EmotionalState emotionState;

	const EmotionControl* pEmotionControl = m_pCharacter ? m_pCharacter->GetEmotionControl() : nullptr;
	if (pEmotionControl)
	{
		emotionState = pEmotionControl->GetEmotionalState();

		//if (m_pCharacter)
		//	emotionState.m_emotion = RemapEmotion(m_pCharacter->GetSkelNameId(), emotionState.m_emotion);
	}

	return emotionState;
}
