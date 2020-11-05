/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/gesture-target-manager.h"

#include "ndlib/nd-frame-state.h"
#include "ndlib/render/util/prim-server-wrapper.h"

#include "gamelib/anim/gesture-target.h"
#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/nd-game-object.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureTargetManager::Init(NdGameObject* pOwner)
{
	m_hOwner = pOwner;
	m_accessLock.m_atomic.Set(0);

	Gesture::TargetLookAt targetLookAt = Gesture::TargetLookAt(pOwner);
	CreateSlot(SID("look"), &targetLookAt, true);

	Gesture::TargetAimAt targetAimAt = Gesture::TargetAimAt(pOwner);
	CreateSlot(SID("aim"), &targetAimAt, true);
	
	Gesture::TargetLocator targetAlign = Gesture::TargetLocator(pOwner, SID("align"));
	CreateSlot(SID("align"), &targetAlign, true);
	
	Gesture::TargetLocator targetApRef = Gesture::TargetLocator(pOwner, SID("apReference"));
	CreateSlot(SID("apReference"), &targetApRef, true);

	Gesture::TargetAnimEulerAngles targetLookAnim = Gesture::TargetAnimEulerAngles(pOwner,
																				   SID("lookAnimated.euler_x"),
																				   SID("lookAnimated.euler_y"));
	CreateSlot(SID("look-animated"), &targetLookAnim, true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F GestureTargetManager::CreateSlot(const StringId64 slotId,
									  const Gesture::Target* pTarget,
									  bool permanent /* = false */)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	I32F existingSlotIndex = -1;
	I32F freeSlotIndex = -1;

	for (I32F i = 0; i < kMaxActiveTargets; ++i)
	{
		if (m_activeTargets[i].m_slotId == INVALID_STRING_ID_64)
		{
			if (freeSlotIndex < 0)
				freeSlotIndex = i;
			continue;
		}

		if (m_activeTargets[i].m_slotId != slotId)
			continue;

		ANIM_ASSERT(m_activeTargets[i].m_valid);

		existingSlotIndex = i;
		break;
	}

	I32F returnSlotIndex = -1;

	if (existingSlotIndex >= 0)
	{
		ActiveTargetSlot& slot = m_activeTargets[existingSlotIndex];

		slot.m_lastUsedFrameNumber = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
		slot.m_target.CopyFrom(pTarget);

		returnSlotIndex = existingSlotIndex;
	}
	else if (freeSlotIndex >= 0)
	{
		ActiveTargetSlot& slot = m_activeTargets[freeSlotIndex];

		slot.Reset();
		slot.m_slotId = slotId;
		slot.m_lastUsedFrameNumber = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
		slot.m_target.CopyFrom(pTarget);
		slot.m_permanent = permanent;
		slot.m_valid = true;

		returnSlotIndex = freeSlotIndex;
	}

	return returnSlotIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F GestureTargetManager::FindSlot(const StringId64 slotId) const
{
	if (slotId == INVALID_STRING_ID_64)
		return -1;

	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	I32F existingSlotIndex = -1;

	for (I32F i = 0; i < kMaxActiveTargets; ++i)
	{
		if (m_activeTargets[i].m_slotId != slotId)
			continue;

		existingSlotIndex = i;
		break;
	}

	return existingSlotIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Gesture::Target* GestureTargetManager::GetTarget(I32F iSlot) const
{
	if ((iSlot < 0) || (iSlot >= kMaxActiveTargets))
	{
		return nullptr;
	}

	if (!m_activeTargets[iSlot].m_valid)
	{
		return nullptr;
	}

	return (const Gesture::Target*)&m_activeTargets[iSlot].m_target;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool GestureTargetManager::UpdateTarget(I32F iSlot, const Gesture::Target* pNewTarget)
{
	if ((iSlot < 0) || (iSlot >= kMaxActiveTargets) || !pNewTarget)
	{
		return false;
	}

	if (!m_activeTargets[iSlot].m_valid)
	{
		return false;
	}

	m_activeTargets[iSlot].m_target.CopyFrom(pNewTarget);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureTargetManager::TickSlot(I32F iSlot)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);
	
	if ((iSlot >= 0) && (iSlot < kMaxActiveTargets))
	{
		ActiveTargetSlot& slot = m_activeTargets[iSlot];

		ANIM_ASSERT(slot.m_valid);

		slot.m_lastUsedFrameNumber = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool GestureTargetManager::LockSlot(const StringId64 slotId)
{
	if (slotId == INVALID_STRING_ID_64)
		return false;

	for (U32F i = 0; i < kMaxActiveTargets; ++i)
	{
		ActiveTargetSlot& slot = m_activeTargets[i];

		if (slot.m_slotId == slotId)
		{
			slot.m_anglesLocked = true;
			
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureTargetManager::DeleteUnusedSlots()
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	for (U32F i = 0; i < kMaxActiveTargets; ++i)
	{
		ActiveTargetSlot& slot = m_activeTargets[i];

		if (INVALID_STRING_ID_64 == slot.m_slotId)
		{
			continue;
		}

		if (slot.m_permanent)
		{
			continue;
		}

		if (slot.m_lastUsedFrameNumber < (EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused - 2))
		{
			slot.Reset();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureTargetManager::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	StringId64 err = INVALID_STRING_ID_64;

	const NdGameObject* pOwner = m_hOwner.ToProcess();
	const Locator alignWs = pOwner->GetLocator();
	
	PrimServerWrapper ps = PrimServerWrapper(alignWs);
	ps.EnableHiddenLineAlpha();
	ps.EnableWireFrame();

	for (U32F i = 0; i < kMaxActiveTargets; ++i)
	{
		if (m_activeTargets[i].m_slotId == INVALID_STRING_ID_64)
			continue;

		const Vector curAimVecLs = /*m_activeTargets[i].m_currentCoords.m_sphereCoords.ToUnitVector()*/ Vector(kZero);

		const Gesture::Target* pTarget = reinterpret_cast<const Gesture::Target*>(&m_activeTargets[i].m_target);

		const Point originOs = pOwner->GetGestureOriginOs(m_activeTargets[i].m_slotId, m_activeTargets[i].m_slotId);
		const Locator originWs = pOwner->GetLocator().TransformLocator(Locator(originOs));
		const Point desAimPosLs = alignWs.UntransformPoint(pTarget->GetWs(originWs).Otherwise(kOrigin));

		const float drawRadius = (float(i) * 0.2f) + 0.75f;
		const Color drawColor = m_activeTargets[i].m_valid ? AI::IndexToColor(m_activeTargets[i].m_slotId.GetValue()) : kColorRed;

		const Point centerLs = kOrigin; //Gesture::GetLocalGestureCenter(m_activeTargets[i].m_type, *pOwner, pJointReader, &err);
		const Vector desAimVecLs = SafeNormalize(desAimPosLs - centerLs, kZero);
		const Point curPosLs = centerLs + (curAimVecLs * drawRadius);
		const Point desPosLs = centerLs + (desAimVecLs * drawRadius);

		Color c0 = drawColor, c1 = drawColor;
		c0.SetA(0.2f);
		c1.SetA(1.0f);
		ps.DrawLine(centerLs, curPosLs, c0, c1);
		ps.DrawCross(curPosLs, 0.1f, c1);
		
		c1.SetA(0.5f);
		ps.DrawLine(centerLs, desPosLs, c0, c1);
		ps.DrawCross(desPosLs, 0.05f, c1);
		
		ps.DrawArc(centerLs, desAimVecLs * drawRadius, curAimVecLs * drawRadius, c1);

		ps.DrawLine(desPosLs, desAimPosLs, c0, c0);
		
		ps.DrawString(curPosLs, DevKitOnly_StringIdToString(m_activeTargets[i].m_slotId), kColorWhite, 0.65f);
	}
}
