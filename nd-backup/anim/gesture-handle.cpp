/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/gesture-handle.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-state-instance.h"

#include "gamelib/anim/gesture-controller.h"
#include "gamelib/anim/gesture-target-manager.h"
#include "gamelib/gameplay/nd-game-object.h"

/// --------------------------------------------------------------------------------------------------------------- ///
GestureHandle::GestureHandle()
	: m_gestureLayerIndex(Gesture::kGestureLayerInvalid)
	, m_originalGestureId(INVALID_STRING_ID_64)
	, m_animLayerId(INVALID_STRING_ID_64)
	, m_scid(StateChangeRequest::kInvalidId)
	, m_uniqueGestureId(-1)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool GestureHandle::operator==(GestureHandle const & other) const
{
	bool same = false;
	
	if (Assigned())
	{
		same = (m_gestureLayerIndex == other.m_gestureLayerIndex && m_uniqueGestureId == other.m_uniqueGestureId);
	}
	else
	{
		same = !other.Assigned();
	}

	return same;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool GestureHandle::Assigned() const
{
	return m_gestureLayerIndex != Gesture::kGestureLayerInvalid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool GestureHandle::Playing(const IGestureController* pGestureController) const
{
	return Assigned() && (*this == pGestureController->GetPlayingGestureHandle(m_gestureLayerIndex));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureHandle::Clear(IGestureController* pGestureController, const Gesture::PlayArgs& clearArgs)
{
	if (Playing(pGestureController))
	{
		Gesture::PlayArgs args(clearArgs);
		args.m_gestureLayer = m_gestureLayerIndex;

		pGestureController->Clear(args);
	}
	else
	{
		*this = GestureHandle();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureHandle::SetPlaybackRate(IGestureController* pGestureController, const float playbackRate) const
{
	if (!Playing(pGestureController))
	{
		return;
	}

	pGestureController->SetGesturePlaybackRate(m_gestureLayerIndex, playbackRate);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float GestureHandle::GetPhase(const AnimControl* pAnimControl) const
{
	float phase = 0.0f;

	if (const AnimStateInstance* pInstance = GetInstance(pAnimControl))
	{
		phase = pInstance->GetPhase();
	}

	return phase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float GestureHandle::GetFade(const AnimControl* pAnimControl) const
{
	float fade = -1.0f;

	const AnimStateLayer* pLayer = pAnimControl ? pAnimControl->GetStateLayerById(m_animLayerId) : nullptr;
	const AnimStateInstance* pInstance = pLayer ? pLayer->GetTransitionDestInstance(m_scid) : nullptr;

	if (pInstance)
	{
		fade = pInstance->MasterFade() * pLayer->GetCurrentFade();
	}

	return fade;
}


/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 GestureHandle::GetCurrentGestureId(const IGestureController* pGestureController) const
{
	if (!Playing(pGestureController))
	{
		return INVALID_STRING_ID_64;
	}

	return pGestureController->GetActiveGesture(m_gestureLayerIndex);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstance* GestureHandle::GetInstance(AnimControl* pAnimControl) const
{
	AnimStateLayer* pLayer = pAnimControl ? pAnimControl->GetStateLayerById(m_animLayerId) : nullptr;
	AnimStateInstance* pInstance = pLayer ? pLayer->GetTransitionDestInstance(m_scid) : nullptr;

	return pInstance;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateInstance* GestureHandle::GetInstance(const AnimControl* pAnimControl) const
{
	const AnimStateLayer* pLayer = pAnimControl ? pAnimControl->GetStateLayerById(m_animLayerId) : nullptr;
	const AnimStateInstance* pInstance = pLayer ? pLayer->GetTransitionDestInstance(m_scid) : nullptr;

	return pInstance;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool GestureHandle::UpdateTarget(IGestureController* pGestureController, const Gesture::Target* pNewTarget)
{
	if (!Playing(pGestureController))
	{
		return false;
	}

	GestureTargetManager& gtm = pGestureController->GetTargetManager();
	const I32F iSlot = gtm.FindSlot(m_originalGestureId);
	return gtm.UpdateTarget(iSlot, pNewTarget);
}
