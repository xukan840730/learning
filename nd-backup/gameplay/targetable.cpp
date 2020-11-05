/*
* Copyright (c) 2004 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/gameplay/targetable.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "ndlib/anim/attach-system.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"

/// --------------------------------------------------------------------------------------------------------------- ///
const Targetable* TargetableHandle::GetTargetable() const
{
	const Targetable* pTarg = nullptr;
	if (const NdGameObject* pProc = m_hProc.ToProcess())
	{
		pTarg = pProc->GetTargetable();
	}
	return pTarg;
}

/// --------------------------------------------------------------------------------------------------------------- ///
TargetableHandle::TargetableHandle(NdGameObjectHandle hOwner)
{
	if (const NdGameObjectSnapshot* pSnapShot = hOwner.ToSnapshot<NdGameObjectSnapshot>())
	{
		if (pSnapShot->m_hasTargetable)
		{
			m_hProc = hOwner;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
TargetableHandle::TargetableHandle(MutableNdGameObjectHandle hOwner)
{
	if (const NdGameObjectSnapshot* pSnapShot = hOwner.ToSnapshot<NdGameObjectSnapshot>())
	{
		if (pSnapShot->m_hasTargetable)
		{
			m_hProc = hOwner;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Targetable* MutableTargetableHandle::GetTargetable() const
{
	if (const NdGameObject* pProc = m_hProc.ToProcess())
	{
		return pProc->GetTargetable();
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Targetable* MutableTargetableHandle::GetMutableTargetable() const
{
	if (NdGameObject* pProc = m_hProc.ToMutableProcess())
	{
		return pProc->GetTargetable();
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MutableTargetableHandle::MutableTargetableHandle(MutableNdGameObjectHandle hOwner)
{
	if (const NdGameObjectSnapshot* pSnapShot = hOwner.ToSnapshot<NdGameObjectSnapshot>())
	{
		if (pSnapShot->m_hasTargetable)
		{
			m_hProc = hOwner;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void TargetableSpot::Update(const AttachSystem& attach)
{
	m_prevWorldPos = m_worldPos;
	m_worldPos	   = attach.GetAttachPosition(m_attachIndex);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Targetable::Targetable(NdGameObject* pProc)
{
	m_hGameObject	 = pProc;
	m_spotsValidMask = 0;
	m_direction		 = 0;
	m_targetVelocity = Vector(0, 0, 0);
	m_lastPos		 = pProc->GetLocator().Pos();
	m_flags.Clear();

	const EntitySpawner* spawner = (pProc ? pProc->GetSpawner() : nullptr);
	if (spawner)
	{
		const float priorityWeightUnclamped = spawner->GetData(SID("lock-on-aim-priority-weight"), 1.0f);
		SetLockOnPriorityWeight(priorityWeightUnclamped);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject* Targetable::GetProcess()
{
	return m_hGameObject.ToMutableProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NdGameObject* Targetable::GetProcess() const
{
	return m_hGameObject.ToProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 Targetable::GetScriptId() const
{
	return m_hGameObject.HandleValid() ? m_hGameObject.GetScriptId() : INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Targetable::UpdateTargetable(const AttachSystem& attach)
{
	PROFILE(Processes, Targetable_UpdateTargetable);

	const NdGameObject* pObj = GetProcess();
	if (!pObj)
		return;

	const Vector delta		   = pObj->GetLocator().Pos() - m_lastPos;
	const F32 processDeltaTime = pObj->GetClock()->GetDeltaTimeInSeconds();

	if (processDeltaTime > 0.0f)
	{
		m_targetVelocity = delta / processDeltaTime;
	}
	else
	{
		m_targetVelocity = Vector(0, 0, 0);
	}

	m_lastPos = pObj->GetLocator().Pos();

	for (U32 spotNum = 0; spotNum < DC::kTargetableSpotCount; ++spotNum)
	{
		if (IsSpotValid(spotNum))
		{
			TargetableSpot& spot = m_spots[spotNum];
			spot.Update(attach);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Targetable::AddSpot(DC::TargetableSpot index, const TargetableSpot& spot)
{
	ALWAYS_ASSERT(index < DC::kTargetableSpotCount);
	ALWAYS_ASSERT(!IsSpotValid(index));

	m_spots[index] = spot;
	m_spotsValidMask |= (1 << index);
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::TargetableSpot Targetable::GetNextValidSpotIndex(DC::TargetableSpot curIndex) const
{
	for (U32 spotNum = 1; spotNum < DC::kTargetableSpotCount; spotNum++)
	{
		const DC::TargetableSpot nextIndex = (curIndex + spotNum) % DC::kTargetableSpotCount;

		if (!IsSpotValid(nextIndex))
			continue;

		return nextIndex;
	}

	return DC::kTargetableSpotInvalid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Targetable::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	for (U32 spotNum = 0; spotNum < DC::kTargetableSpotCount; ++spotNum)
	{
		if (IsSpotValid(spotNum))
		{
			const TargetableSpot& spot = m_spots[spotNum];
			g_prim.Draw(DebugCross(spot.GetWorldPos(), 0.05f, kColorYellow));
			g_prim.Draw(DebugString(spot.GetWorldPos(), DC::GetTargetableSpotName(spotNum), kColorYellow, 0.4f));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Targetable::SetLockOnPriorityWeight(float newWeight)
{
	m_priorityWeight = (newWeight > 0.f ? newWeight : 0.f);
}

