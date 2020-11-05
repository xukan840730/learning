/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-layer.h"

#include "corelib/memory/relocate.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/anim-data.h"

/// --------------------------------------------------------------------------------------------------------------- ///
AnimLayer::AnimLayer(AnimLayerType type, AnimTable* pAnimTable, AnimOverlaySnapshot* pOverlaySnapshot)
: m_type(type)
, m_pAnimTable(pAnimTable)
, m_pOverlaySnapshot(pOverlaySnapshot)
, m_name(0)
, m_blendMode(ndanim::kBlendSlerp)
, m_currentFade(0.0f)
, m_desiredFade(0.0f)
, m_fadeTimeLeft(0.0f)
, m_fadeTimeTotal(1.0f)
, m_startingFade(0.0f)
, m_fadeBlendType(DC::kAnimCurveTypeInvalid)
, m_freeWhenFadedOut(false)
, m_disableTriggeredEffects(false)
, m_assertOnStateChanges(false)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimLayer::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pAnimTable, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pOverlaySnapshot, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimLayer::Setup(StringId64 name, ndanim::BlendMode blendMode)
{
	m_name = name;
	m_blendMode = blendMode;
	m_freeWhenFadedOut = false;	
	m_currentFade = 0.0f;
	m_lodFadeMultiplier = 1.0f;
	m_desiredFade = 0.0f;
	m_fadeTimeLeft = 0.0f;
	m_fadeTimeTotal = 1.0f;
	m_startingFade = 0.0f;
	m_fadeBlendType = DC::kAnimCurveTypeInvalid;
	m_disableTriggeredEffects = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimLayer::ShouldAssertOnStateChanges() const
{
	return FALSE_IN_FINAL_BUILD(m_assertOnStateChanges && !g_animOptions.m_disableAssertOnStateChanges);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimLayer::IsFadedOut() const
{
	if (m_lodFadeMultiplier == 0.0f)
		return true;

	return (m_currentFade == 0.0f) && (m_currentFade == m_desiredFade);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimLayer::FadeOutAndDestroy(F32 fadeTime, DC::AnimCurveType type)
{
	Fade(0.0f, fadeTime, type);
	m_freeWhenFadedOut = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimLayer::AbortFadeOutAndDestroy()
{
	m_freeWhenFadedOut = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimLayer::BeginStep(F32 deltaTime, EffectList* pTriggeredEffects, const FgAnimData* pFgAnimData)
{
	float fadeValue = 1.0f;

	if (m_fadeTimeLeft > 0.0f)
	{
		m_fadeTimeLeft -= deltaTime;

		float tt = Limit01((m_fadeTimeTotal - m_fadeTimeLeft) / m_fadeTimeTotal);
		fadeValue = CalculateCurveValue(tt, m_fadeBlendType);
	}

	// Scale the fade to what we want it to go between...
	m_currentFade = LerpScale(0.0f, 1.0f, m_startingFade, m_desiredFade, fadeValue);

	if (pFgAnimData && GetName() != SID("base") && GetName() != SID("net-anim-layer"))
	{
		if (pFgAnimData->m_animLod >= DC::kAnimLodFar)
		{
			m_lodFadeMultiplier -= FromUPS(30.0f * deltaTime);
		}
		else
		{
			m_lodFadeMultiplier += FromUPS(30.0f * deltaTime);
		}
	}

	m_lodFadeMultiplier = Limit01(m_lodFadeMultiplier);

	// Debug stuff
	m_currentFade = FALSE_IN_FINAL_BUILD(g_animOptions.m_disableLayerFades) ? 1.0f : m_currentFade;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimLayer::FinishStep(F32 deltaTime, EffectList* pTriggeredEffects)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimLayer::Fade(F32 desiredFade, F32 fadeTime, DC::AnimCurveType type)
{
	// ignore this if the layer is already fading out and is scheduled to be released,
	// and we're asking it to fade
	// otherwise the layer might never properly fade out
	if (m_freeWhenFadedOut && desiredFade > 0.0f)
		return;

	// ignore if we're already in the middle of this fade
	if (desiredFade == m_desiredFade && fadeTime == m_fadeTimeTotal)
		return;

	m_desiredFade = Limit01(desiredFade);
	m_fadeTimeTotal = Limit(fadeTime, 0.0f, fadeTime);

	ANIM_ASSERT(type > DC::kAnimCurveTypeInvalid && type < DC::kAnimCurveTypeMax);
	m_fadeBlendType = type;

	m_fadeTimeLeft = m_fadeTimeTotal;
	m_startingFade = m_currentFade;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimLayer::SetCurrentFade(F32 fade)
{
	m_desiredFade	= Limit01(fade);
	m_currentFade	= m_desiredFade;
	m_startingFade	= m_currentFade;
	m_fadeTimeTotal = 0.0f;
	m_fadeTimeLeft	= 0.0f;
}
