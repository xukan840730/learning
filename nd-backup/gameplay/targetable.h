/*
* Copyright (c) 2003 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/


#ifndef NDLIB_TARGETABLE_H
#define NDLIB_TARGETABLE_H

#include "ndlib/anim/attach-system.h"

#include "gamelib/scriptx/h/nd-ai-defines.h"

class NdGameObject;

class TargetableSpot
{
public:
	TargetableSpot(AttachIndex index = AttachIndex())
		: m_worldPos(SMath::kZero)
		, m_prevWorldPos(SMath::kZero)
		, m_attachIndex(index)
	{}

	void			Update(const AttachSystem& attach);

	const Point		GetWorldPos() const
	{
		return m_worldPos;
	}

	const Point		GetPrevWorldPos() const
	{
		return m_prevWorldPos;
	}

	AttachIndex		GetAttachIndex() const
	{
		return m_attachIndex;
	}

	bool operator==(const TargetableSpot& other) const
	{
		return m_attachIndex == other.m_attachIndex;
	}

	bool operator!=(const TargetableSpot& other) const
	{
		return !(*this == other);
	}

private:
	Point			m_worldPos;
	Point			m_prevWorldPos;
	AttachIndex		m_attachIndex;
};

///
/// Targetable -- contains info about a targetable object
///
class Targetable
{
public:
	struct Flags
	{
		bool m_suppressAttack : 1;				// don't attack this target
		bool m_suppressLockOn : 1;				// don't auto lock on to this target
		bool m_suppressAimAssist : 1;			// don't aim assist on this target
		bool m_suppressLockOnHudElements : 1;	// don't draw the lock on hud element when targeting this target
		bool m_suppressAutoThrow : 1;			// don't auto throw on this target
		bool m_allowLockOnAllSpots : 1;			// allow all non-default targetable spots to be considered for lock-on
		bool m_ropeTarget : 1;					// special for rope only
		void Clear() { memset(this, 0, sizeof(*this)); }
	};

private:
	TargetableSpot m_spots[DC::kTargetableSpotCount];
	U16  m_spotsValidMask;
	//U16  m_numSpots;

	float m_priorityWeight = 1.0f;	// Will be clamped to >= 0.0f when set

public:
	MutableNdGameObjectHandle	m_hGameObject;
	Vector			m_targetVelocity;
	Point			m_lastPos;
	I32 			m_direction;
	TimeFrame		m_timeout;					// timeout targets removal (e.g. when character dies)
	Flags			m_flags;

public:
	Targetable(NdGameObject* pProc);
	NdGameObject* GetProcess();
	const NdGameObject* GetProcess() const;
	StringId64 GetScriptId() const;
	void UpdateTargetable(const AttachSystem& attach);

	bool IsSpotValid(DC::TargetableSpot index) const
	{
		GAMEPLAY_ASSERT(index < DC::kTargetableSpotCount);
		if (index >= DC::kTargetableSpotCount)
			return false;

		const U16 spotFlag	 = 1 << index;
		const U16 validFlags = m_spotsValidMask;
		return ((spotFlag & validFlags) != 0);
	}

	const TargetableSpot& GetSpot(DC::TargetableSpot index) const
	{
		GAMEPLAY_ASSERT(IsSpotValid(index));
		if (index >= DC::kTargetableSpotCount)
			index = DC::kTargetableSpotChest;

		return m_spots[index];
	}

	U32		GetNumValidSpots() const { return CountSetBits(m_spotsValidMask); }

	DC::TargetableSpot GetNextValidSpotIndex(DC::TargetableSpot index) const;
	void	AddSpot(DC::TargetableSpot index, const TargetableSpot& spot);
	//void	RemoveSpot(U32F index);
	void	DebugDraw() const;

	void SetLockOnPriorityWeight(float newWeight); // Negative values are clamped to 0.f
	float GetLockOnPriorityWeight() const { return m_priorityWeight; }

};

/// Normally a typedef would be sufficient to define the handle type, but we have a bunch of code
/// that relies on the "ToFocusable()" method, so this is a temp fix for that.

class MutableTargetableHandle
{
	MutableNdGameObjectHandle	m_hProc;

public:
	MutableTargetableHandle() : m_hProc(nullptr) {}
	MutableTargetableHandle(Targetable* pObj) : m_hProc( pObj ? pObj->GetProcess() : nullptr) {}
	MutableTargetableHandle(Targetable& obj) : m_hProc( obj.GetProcess() ) {}
	MutableTargetableHandle(MutableNdGameObjectHandle hOwner);

	MutableNdGameObjectHandle GetOwnerHandle() const { return m_hProc; }
	Targetable* GetMutableTargetable() const;
	const Targetable* GetTargetable() const;
	bool HandleValid() const { return m_hProc.HandleValid(); }

	bool operator==(const MutableTargetableHandle& other) const { return m_hProc == other.m_hProc; }
};

class TargetableHandle
{
	NdGameObjectHandle	m_hProc;

public:
	TargetableHandle() : m_hProc(nullptr) {}
	TargetableHandle(const Targetable* pObj) : m_hProc(pObj ? pObj->GetProcess() : nullptr) {}
	TargetableHandle(const Targetable& obj) : m_hProc(obj.GetProcess()) {}
	TargetableHandle(NdGameObjectHandle hOwner);
	TargetableHandle(MutableNdGameObjectHandle hOwner);
	TargetableHandle(MutableTargetableHandle hTarg) : m_hProc(hTarg.GetOwnerHandle()) {}

	NdGameObjectHandle GetOwnerHandle() const { return m_hProc; }
	const Targetable* GetTargetable() const;
	bool HandleValid() const { return m_hProc.HandleValid(); }

	bool operator==(const TargetableHandle& other) const { return m_hProc == other.m_hProc; }
};

#endif // NDLIB_TARGETABLE_H

