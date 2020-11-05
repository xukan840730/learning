/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-state-layer.h"

#include "gamelib/anim/nd-gesture-util.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimControl;
class AnimStateInstance;
class IGestureController;

/// --------------------------------------------------------------------------------------------------------------- ///
// Handle to a currently playing gesture
struct GestureHandle
{
	GestureHandle();

	bool operator==(const GestureHandle& other) const;

	bool Valid() const { return m_originalGestureId != INVALID_STRING_ID_64; }

	bool Assigned() const;
	bool Playing(const IGestureController* pGestureController) const;
	void Clear(IGestureController* pGestureController, const Gesture::PlayArgs& clearArgs = Gesture::g_defaultPlayArgs);
	void SetPlaybackRate(IGestureController* pGestureController, float playbackRate) const;
	
	float GetPhase(const AnimControl* pAnimControl) const;
	float GetFade(const AnimControl* pAnimControl) const;

	// Returns the name of the gesture if it is currently playing, and INVALID_STRING_ID_64 otherwise.
	StringId64 GetCurrentGestureId(const IGestureController* pGestureController) const;

	const AnimStateInstance* GetInstance(const AnimControl* pAnimControl) const;
	AnimStateInstance* GetInstance(AnimControl* pAnimControl) const;

	inline bool ValidAndPlaying(const AnimControl* pAnimControl) const
	{
		return Valid() && GetPhase(pAnimControl) >= 0.0f;
	}

	bool UpdateTarget(IGestureController* pGestureController, const Gesture::Target* pNewTarget);

	StringId64 m_originalGestureId;
	StringId64 m_animLayerId;
	Gesture::LayerIndex m_gestureLayerIndex;
	StateChangeRequest::ID m_scid;
	U32 m_uniqueGestureId;
};
