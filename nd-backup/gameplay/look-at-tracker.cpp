/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/look-at-tracker.h"

#include "corelib/math/locator.h"
#include "ndlib/process/clock.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLookAtTracker::Callback_SetLookAtPosition(OwnerType& owner, Point_arg worldSpacePoint)
{
	const Locator& ps = owner.GetParentSpace();
	m_lastLookPosPs = ps.UntransformPoint(worldSpacePoint);

	Enable();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLookAtTracker::Callback_SetLookAtGestureBlendFactor(OwnerType& owner, float gestureBlendFactor)
{
	F32 kGestureBlendFactorSpring = 8.0f;

	const F32 ownerProvidedSpring = owner.GetGestureBlendFactorSpring();
	if (ownerProvidedSpring >= 0.0f)
	{
		kGestureBlendFactorSpring = ownerProvidedSpring;
	}

	if (GetGestureBlendFactor() != gestureBlendFactor)
	{
		const F32 dt = owner.GetClock()->GetDeltaTimeInSeconds();
		m_gestureBlendFactor = m_gestureBlendFactorSpring.Track(GetGestureBlendFactor(),
																gestureBlendFactor,
																dt,
																kGestureBlendFactorSpring);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ILookAtTracker::DebugDraw(OwnerType& owner, const char* message)
{
	Point lookAtPointWs = kOrigin;
	bool valid = false;

	if (m_pCustomUpdate)
	{
		if (m_pCustomUpdate(owner, m_pCustomUpdateData, lookAtPointWs))
		{
			valid = true;
		}
	}
	else if (m_jointIndex >= 0)
	{
		const NdGameObject* pTarget = m_hTarget.ToProcess();
		const FgAnimData* pAnimData = pTarget ? pTarget->GetAnimData() : nullptr;
		if (pAnimData && (m_jointIndex < pAnimData->m_jointCache.GetNumTotalJoints()))
		{
			const Locator& lookAtLoc = pAnimData->m_jointCache.GetJointLocatorWs(m_jointIndex);
			lookAtPointWs = lookAtLoc.Pos();
			valid = true;
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
				lookAtPointWs = lookAtLoc.Pos();
				valid = true;
			}
			else
			{
				lookAtPointWs = pTarget->GetTranslation();
				valid = true;
			}
		}
	}
	else if (HasStaticLookAt())
	{
		if (m_updateStaticLookEveryFrame)
		{
			Callback_SetLookAtPosition(owner, m_staticLookAtPointWS);
		}
		lookAtPointWs = m_staticLookAtPointWS;
		valid = true;
	}

	if (valid)
	{
		Vector offset = kUnitYAxis;
		offset *= 1.5f;
		g_prim.Draw(DebugLine(owner.GetTranslation() + offset, lookAtPointWs, kColorMagenta, 2.0f, kPrimEnableHiddenLineAlpha));
		g_prim.Draw(DebugCross(lookAtPointWs, 0.2f, 2.0f, kColorMagenta, kPrimEnableHiddenLineAlpha));
		if (message && *message != '\0')
			g_prim.Draw(DebugString(lookAtPointWs, message, 0, kColorMagenta, 0.6f));
	}
	return valid;
}
