/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/anim/anim-defines.h"
#include "ndlib/process/bound-frame.h"

#include "gamelib/gameplay/nav/action-pack-handle.h"

namespace DC
{
	struct ApEntryItem;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct ActionPackResolveInput
{
	BoundFrame m_frame		   = kIdentity;
	Vector m_velocityPs		   = kZero;
	MotionType m_motionType	   = kMotionTypeMax;
	StringId64 m_mtSubcategory = INVALID_STRING_ID_64;

	uintptr_t m_apUserData = 0;

	bool m_moving	= false;
	bool m_strafing = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class ActionPackEntryDef
{
public:
	bool IsValidFor(ActionPackHandle hAp) const
	{
		return m_hResolvedForAp.IsValid() && (m_hResolvedForAp.GetMgrId() == hAp.GetMgrId());
	}

	bool IsValidFor(const ActionPack* pAp) const
	{
		return pAp && m_hResolvedForAp.IsValid() && (m_hResolvedForAp.GetMgrId() == pAp->GetMgrId());
	}

	void OverrideSelfBlend(const DC::SelfBlendParams* pSelfBlend);
	const DC::SelfBlendParams* GetSelfBlend() const;

	BoundFrame m_apReference   = BoundFrame(kIdentity);
	BoundFrame m_sbApReference = BoundFrame(kIdentity);

	ActionPackHandle m_hResolvedForAp = ActionPackHandle();

	NavLocation m_entryNavLoc;
	Quat m_entryRotPs = kIdentity;

	Vector m_entryVelocityPs = kZero;
	StringId64 m_entryAnimId = INVALID_STRING_ID_64;
	MotionType m_preferredMotionType = kMotionTypeRun;
	StringId64 m_mtSubcategoryId	 = INVALID_STRING_ID_64;
	TimeFrame m_refreshTime = TimeFrameNegInfinity();
	float m_forceBlendTime	= -1.0f;
	float m_phase	  = 0.0f;
	float m_skipPhase = -1.0f;

	uintptr_t m_controllerData = 0;
	uintptr_t m_apUserData	   = 0;

	const DC::ApEntryItem* m_pDcDef = nullptr;

private:
	DC::SelfBlendParams m_selfBlendOverride;

public:
	bool m_stopBeforeEntry = false; // Should we move or be stopped when blending into the entry?
	bool m_mirror = false;
	bool m_additiveHitReactions = false;
	bool m_autoExitAfterEnter	= false;
	bool m_useDesiredAlign		= false;
	bool m_recklessStopping		= false;
	bool m_alwaysStrafe			= false;
	bool m_slowInsteadOfAutoStop		= false;
};
