/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/anim/nd-gesture-util.h"

class NdGameObject;

/// --------------------------------------------------------------------------------------------------------------- ///
// Defines whether the GestureController is allowed to play a certain gesture at this time.
class GesturePermit
{
public:
	// Contextual information supplied to help GesturePermit make its decision.
	struct Context
	{
		// Owner of the GestureController.
		const NdGameObject* m_pOwner = nullptr;

		// Priority of the gesture under consideration.
		Gesture::PlayPriority m_priority = -1;
		
		// effective blend out time of the currently playing gesture
		float m_blendOutTime = -1.0f;
		mutable float m_lockOutBlend = -1.0f;

		// Place where a reason for this decision can be returned.
		const char** m_ppOutReason = nullptr;
		Gesture::PlayPriority* m_pLockoutPriority = nullptr;
	};

	virtual ~GesturePermit() {}

	virtual void Relocate(ptrdiff_t, uintptr_t, uintptr_t) {}

	// Is this new gesture allowed to play?
	// If INVALID_STRING_ID_64 is passed in as the gestureId, try to make a decision about whether an arbitrary gesture
	// *would* be allowed to play.
	virtual bool AllowPlay(StringId64 gestureId, const Context& ctx) = 0;

	// Is this already-playing gesture allowed to continue playing?
	virtual bool AllowContinue(StringId64 gestureId, const Context& ctx) = 0;
};
