/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/anim/gesture-target.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class GestureTargetManager
{
public:
	void Init(NdGameObject* pOwner);

	I32F CreateSlot(const StringId64 slotId, const Gesture::Target* pTarget, bool permanent = false);
	I32F FindSlot(const StringId64 slotId) const;

	const Gesture::Target* GetTarget(I32F iSlot) const;
	bool UpdateTarget(I32F iSlot, const Gesture::Target* pNewTarget);

	void TickSlot(I32F iSlot);
	bool LockSlot(const StringId64 slotId);
	void DeleteUnusedSlots();

	void DebugDraw() const;

private:
	static CONST_EXPR size_t kMaxActiveTargets = 8;

	struct ActiveTargetSlot
	{
		void Reset()
		{
			*this = ActiveTargetSlot();
			
			m_target.Clear();
		}

		Gesture::TargetBuffer m_target;

		StringId64 m_slotId		  = INVALID_STRING_ID_64;
		I64 m_lastUsedFrameNumber = 0;
		bool m_anglesLocked		  = false;
		bool m_permanent = false;
		bool m_valid = false;
	};

	mutable NdAtomicLock m_accessLock;
	NdGameObjectHandle m_hOwner;
	ActiveTargetSlot m_activeTargets[kMaxActiveTargets];
};
