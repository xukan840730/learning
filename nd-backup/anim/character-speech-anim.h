/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/resource/resource-table.h"
#include "ndlib/util/tracker.h"

#include "gamelib/audio/lipsync-types.h"
#include "gamelib/gameplay/emotion-control.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class ArtItemAnim;
class ArtItemSkeleton;
class NdGameObject;

/// --------------------------------------------------------------------------------------------------------------- ///
class CharacterSpeechAnim : public IAnimCmdGenerator
{
public:
	CharacterSpeechAnim(NdGameObject* pCharacter);

	void Reset(NdGameObject* pCharacter);

	virtual float GetFadeMult() const override;
	virtual void CreateAnimCmds(const AnimCmdGenLayerContext& context,
								AnimCmdList* pAnimCmdList,
								U32F outputInstance) const override;
	virtual void DebugPrint(MsgOutput output) const override;
	virtual void Step(F32 deltaTime) override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	// These are public at the moment for the actor viewer menus
	bool m_disableVisemeSprings		 = false;
	bool m_enableVisemeBlending		 = false;
	bool m_enableTransparentXPhoneme = false;
	bool m_useShippedBlendingCode	 = true;

private:
	NdGameObject* m_pCharacter;
	float m_phonemeBlend[kNdPhonemeCount];
	float m_fadeMult;

	SpringTracker<float> m_fadeMultSpring;
	SpringTracker<float> m_phonemeSpring[kNdPhonemeCount];
	StringId64 m_phonemeEmotion[kNdPhonemeCount];

	void TryRefreshAnimPointers(const ArtItemSkeleton* pAnimateSkel);

	EmotionalState GetEmotionalState() const;

	void DebugPhonemeEmotionCache(int numPhonemes, NdPhoneme phonemes[], float blend[]) const;

	SkeletonId m_skelId;
	U32 m_hierarchyId;
	ArtItemAnimHandle m_phonemeAnim;
	ArtItemAnimHandle m_bindPoseAnim;
	U32 m_atActionCounter;
	U32 m_stActionCounter;
};
