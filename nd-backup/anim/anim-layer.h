/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-defines.h"
#include "ndlib/scriptx/h/animation-script-types.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimOverlaySnapshot;
class AnimTable;
class EffectList;
struct FgAnimData;
struct EvaluateChannelParams;

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimLayer
{
public:
	friend class AnimControl;

	AnimLayer(AnimLayerType type, AnimTable* pAnimTable, AnimOverlaySnapshot* pOverlaySnapshot);

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) = 0;
	virtual void Shutdown() {}

	AnimLayerType GetType() const { return m_type; }
	StringId64 GetName() const { return m_name; }

	ndanim::BlendMode GetBlendMode() const { return m_blendMode; }

	// Layer blend configuration
	virtual F32 GetFadeTimeLeft() const { return m_fadeTimeLeft; }

	void SetFeatherBlendIndex(I32F index) { m_featherBlendIndex = index; }
	I32F GetFeatherBlendIndex() const { return m_featherBlendIndex; }

	bool IsFadedOut() const;

	void Fade(F32 desiredFade, F32 fadeTime, DC::AnimCurveType type = DC::kAnimCurveTypeLinear);
	void FadeOutAndDestroy(F32 fadeTime, DC::AnimCurveType type = DC::kAnimCurveTypeLinear);
	void SetCurrentFade(F32 fade);
	void UpdateDesiredFade(F32 fade) { m_desiredFade = fade; }
	bool IsFadeUpToDate() const { return Abs(m_currentFade - m_desiredFade) < NDI_FLT_EPSILON; }

	void AbortFadeOutAndDestroy();

	virtual U32F GetNumFadesInProgress() const { return 0; }

	virtual bool IsValid() const = 0;

	bool BeingDestroyed() const { return m_freeWhenFadedOut; }

	virtual F32 GetCurrentFade() const { return m_currentFade * m_lodFadeMultiplier; }
	virtual F32 GetDesiredFade() const { return m_desiredFade * m_lodFadeMultiplier; }

	bool TriggeredEffectsEnabled() const { return !m_disableTriggeredEffects; }
	void DisableTriggeredEffects()		 { m_disableTriggeredEffects = true; }
	void EnableTriggeredEffects()		 { m_disableTriggeredEffects = false; }

	virtual void CollectContributingAnims(AnimCollection* pCollection) const {}

	virtual U32F EvaluateChannels(const StringId64* pChannelNames,
								  size_t numChannels,
								  ndanim::JointParams* pOutChannelJoints,
								  const EvaluateChannelParams& params,
								  FadeMethodToUse fadeMethod = kUseMotionFade,
								  float* pBlendValsOut		 = nullptr) const
	{
		return 0;
	}

	bool ShouldAssertOnStateChanges() const;
	void EnableAssertOnStateChanges() { m_assertOnStateChanges = true; }
	void DisableAssertOnStateChanges() { m_assertOnStateChanges = false; }

	///----------------------------------------------------------------------------------///
	/// This function was for player unholster/holster weapons at the same time.
	/// we shouldn't abuse this function.
	///----------------------------------------------------------------------------------///
	void SetAnimOverlaySnapshot(AnimOverlaySnapshot* pOverlaySnapshot) { m_pOverlaySnapshot = pOverlaySnapshot; }
	const AnimOverlaySnapshot* GetAnimOverlaySnapshot() const { return m_pOverlaySnapshot; }

protected:
	const AnimTable* GetAnimTable() const { return m_pAnimTable; }

	virtual void Setup(StringId64 name, ndanim::BlendMode blendMode);
	virtual void BeginStep(F32 deltaTime, EffectList* pTriggeredEffects, const FgAnimData* pAnimData);
	virtual void FinishStep(F32 deltaTime, EffectList* pTriggeredEffects);
	virtual void OnFree() {};
	
	const AnimLayerType		m_type;
	AnimTable*				m_pAnimTable;
	AnimOverlaySnapshot*	m_pOverlaySnapshot;
	StringId64				m_name;				//<! Name of this layer
	ndanim::BlendMode		m_blendMode;		//<! How is this layer blended with other layers
	F32						m_fadeTimeLeft;		//<! How much longer before the fade is done (in seconds)
	F32						m_fadeTimeTotal;	//<! How long to fade (in seconds)
	F32						m_startingFade;		//<! What fade we were at when we started fading
	I32						m_featherBlendIndex = -1;
	DC::AnimCurveType		m_fadeBlendType;		//<! The curve type we want to use to fade the layer
	bool					m_freeWhenFadedOut;	//<! Destroy the layer when the current fade reaches zero.
	bool					m_disableTriggeredEffects; //<! Prevent this layer from generating triggered effects
	bool					m_assertOnStateChanges; //<! Assert whenever a state change occurs on this layer (used by cinematics)

private:
	F32						m_lodFadeMultiplier;//<! The fade multiplier due to lodding
	F32						m_currentFade;		//<! The fade we currently have and apply.
	F32						m_desiredFade;		//<! The desired fade amount
};
