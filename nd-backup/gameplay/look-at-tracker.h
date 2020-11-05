/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/attach-system.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/process/process.h"
#include "ndlib/util/tracker.h"

#include "gamelib/gameplay/character-types.h"
#include "gamelib/gameplay/nd-game-object.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ILookAtTracker
{
public:
	enum Priority
	{
		kPriorityLow = 0,
		kPriorityNormal,
		kPriorityCinematic,	// commands from in-game cinematics (IGCs)
		kPriorityCount,
	};

	typedef NdGameObject OwnerType;

	typedef bool (*CustomLookAtUpdate)(OwnerType&, void* /* callback user data */, Point& /* word-space position output */);

	explicit ILookAtTracker(Priority p = kPriorityNormal)
		: m_staticLookAtPointWS(kOrigin)
		, m_updateStaticLookEveryFrame(false)
		, m_hTarget()
		, m_attachIndex(AttachIndex::kInvalid)
		, m_jointIndex(-1)
		, m_priority(p)
		, m_pCustomUpdate(nullptr)
		, m_pCustomUpdateData(nullptr)
	{}

	void Reset()
	{
		m_hTarget = NdGameObjectHandle();
		m_attachIndex = AttachIndex::kInvalid;
		m_jointIndex = -1;
		m_updateStaticLookEveryFrame = false;
		m_pCustomUpdate = nullptr;
		m_pCustomUpdateData = nullptr;
	}

	void SetPriority(Priority p)
	{
		GAMEPLAY_ASSERT(p < kPriorityCount);
		m_priority = p;
	}

	Priority GetPriority() const
	{
		return m_priority;
	}

	void SetCustomLookAt(OwnerType& owner, CustomLookAtUpdate pCustomUpdate, void* pUserData)
	{
		Reset();

		m_pCustomUpdate = pCustomUpdate;
		m_pCustomUpdateData = pUserData;
	}

	void SetDynamicLookAt(OwnerType& owner, const NdGameObject* pTarget, AttachIndex index)
	{
		Reset();

		if (pTarget != nullptr)
		{
			m_hTarget = pTarget;
			m_attachIndex = index;
			Update(owner);
		}
		else
		{
			Callback_ClearLookAtPosition(owner);
		}
	}

	void SetDynamicLookAtJoint(OwnerType& owner, const NdGameObject* pTarget, U32 jointIndex)
	{
		Reset();

		if (pTarget != nullptr)
		{
			m_hTarget = pTarget;
			m_jointIndex = jointIndex;
			Update(owner);
		}
		else
		{
			Callback_ClearLookAtPosition(owner);
		}
	}

	void SetStaticLookAt(OwnerType& owner, Point_arg worldSpacePoint, bool updateEveryFrame = false)
	{
		Reset();
		GAMEPLAY_ASSERT(LengthSqr(worldSpacePoint) < Sqr(1000000.f));
		m_staticLookAtPointWS = worldSpacePoint;
		m_updateStaticLookEveryFrame = updateEveryFrame;
		Callback_SetLookAtPosition(owner, worldSpacePoint);
	}

	void DisableLookAt(OwnerType& owner)
	{
		Reset();
		Callback_ClearLookAtPosition(owner);
	}

	void Update(OwnerType& owner)
	{
		if (m_pCustomUpdate)
		{
			Point lookAtPointWs = kOrigin;
			if (m_pCustomUpdate(owner, m_pCustomUpdateData, lookAtPointWs))
			{
				Callback_SetLookAtPosition(owner, lookAtPointWs);
			}
			else
			{
				DisableLookAt(owner);
			}
		}
		else if (m_jointIndex >= 0)
		{
			const NdGameObject* pTarget = m_hTarget.ToProcess();
			const FgAnimData* pAnimData = pTarget ? pTarget->GetAnimData() : nullptr;
			if (pAnimData && (m_jointIndex < pAnimData->m_jointCache.GetNumTotalJoints()))
			{
				const Locator& lookAtLoc = pAnimData->m_jointCache.GetJointLocatorWs(m_jointIndex);
				const Point lookAtPointWs = lookAtLoc.Pos();
				Callback_SetLookAtPosition(owner, lookAtPointWs);
			}
			else
			{
				DisableLookAt(owner);
			}
		}
		else if (m_attachIndex != AttachIndex::kInvalid)
		{
			const NdGameObject* pTarget = m_hTarget.ToProcess();
			if (pTarget)
			{
				const AttachSystem* pAs = pTarget->GetAttachSystem();
				if (pAs && pAs->IsValidAttachIndex(m_attachIndex))
				{
					const Locator& lookAtLoc = pAs->GetLocator(m_attachIndex);
					const Point lookAtPointWs = lookAtLoc.Pos();
					Callback_SetLookAtPosition(owner, lookAtPointWs);
				}
				else
				{
					Callback_SetLookAtPosition(owner, pTarget->GetTranslation());
				}
			}
			else
			{
				DisableLookAt(owner);
			}
		}
		else if (m_updateStaticLookEveryFrame)
		{
			Callback_SetLookAtPosition(owner, m_staticLookAtPointWS);
		}

		Callback_SetLookAtGestureBlendFactor(owner, IsEnabled() ? 1.0f : 0.0f);
	}

	virtual bool IsEnabled() const
	{
		return m_jointIndex >= 0 || m_attachIndex != AttachIndex::kInvalid || m_updateStaticLookEveryFrame;
	}

	bool DebugDraw(OwnerType& owner, const char* message);

protected:
	// These must be provided by derived classes.
	virtual void Callback_SetLookAtPosition(OwnerType& owner, Point_arg worldSpacePoint) = 0;
	virtual void Callback_ClearLookAtPosition(OwnerType& owner) = 0;
	virtual void Callback_SetLookAtGestureBlendFactor(OwnerType& owner, float gestureBlendFactor) = 0;
	virtual bool HasStaticLookAt() const { return true; }

	Point m_staticLookAtPointWS;
	bool m_updateStaticLookEveryFrame;
	NdGameObjectHandle m_hTarget;
	AttachIndex m_attachIndex;
	I32 m_jointIndex;
	Priority m_priority;
	CustomLookAtUpdate m_pCustomUpdate;
	void* m_pCustomUpdateData;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class CharacterLookAtTracker : public ILookAtTracker
{
	/*
	 *		OwnerType::SetLookAtPosition()					// pipes the desired look at into the tracker, also turns that tracker on
	 *			-> LookAtTracker::SetLookAtPosition()
	 *		
	 *		OwnerType::Update()
	 *			-> NavChar::UpdateLookAt()					// iterates and updates all trackers
	 *
	 *		OwnerType::GetLookAtPositionPs()
	 *			-> // return the top priority look-at tracker's current look-at position
	 *
	 */

public:
	CharacterLookAtTracker() : m_enabled(false), m_lastLookPosPs(kZero), m_gestureBlendFactor(0.0f) {}

	virtual bool IsEnabled() const override	{ return m_enabled; }

	void EnterNewParentSpace(const Transform& matOldToNew, const Locator& oldParentSpace, const Locator& newParentSpace)
	{
		m_lastLookPosPs = m_lastLookPosPs * matOldToNew;
	}

	Point GetLookAtPosPs() const		{ return m_lastLookPosPs; }
	float GetGestureBlendFactor() const	{ return m_gestureBlendFactor; }

protected:
	virtual void Callback_SetLookAtPosition(OwnerType& owner, Point_arg worldSpacePoint) override;
	virtual void Callback_ClearLookAtPosition(OwnerType& owner) override { Disable(); }
	virtual void Callback_SetLookAtGestureBlendFactor(OwnerType& owner, float gestureBlendFactor) override;
	virtual bool HasStaticLookAt() const override { return false; }

	void Enable()	{ m_enabled = true; }
	void Disable()	{ m_enabled = false; }

	bool m_enabled;
	Point m_lastLookPosPs;
	float m_gestureBlendFactor;
	SpringTracker<F32> m_gestureBlendFactorSpring;
};
