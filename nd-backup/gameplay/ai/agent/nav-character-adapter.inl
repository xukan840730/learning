/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

// IMPORTANT:
// #include this .inl file if you need the inline implementations.
// This has been done to resolve a circular dependency between nav-character.h, nav-character-adapter.h and nav-character-adapter.inl.

#include "gamelib/gameplay/ai/agent/nav-character-adapter.h"

#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/agent/simple-nav-character.h"

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
inline NavCharacterAdapter NavCharacterAdapter::FromProcess(const Process* pProc)
{
	const SimpleNavCharacter* pSNavChar = const_cast<SimpleNavCharacter*>(SimpleNavCharacter::FromProcess(pProc));
	if (pSNavChar)
		return NavCharacterAdapter(pSNavChar);

	const NavCharacter* pNavChar = const_cast<NavCharacter*>(NavCharacter::FromProcess(pProc));
	return NavCharacterAdapter(pNavChar);
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline const NdGameObject* NavCharacterAdapter::ToGameObject() const						{ return m_pNavChar ? static_cast<const NdGameObject*>(m_pNavChar) : static_cast<const NdGameObject*>(m_pSNavChar); }
inline NdGameObject* NavCharacterAdapter::ToGameObject()									{ return m_pNavChar ? static_cast<NdGameObject*>(m_pNavChar) : static_cast<NdGameObject*>(m_pSNavChar); }
inline SimpleNavControl* NavCharacterAdapter::GetNavControl()								{ return (m_pNavChar ? m_pNavChar->GetNavControl()					: (m_pSNavChar ? m_pSNavChar->GetNavControl()					: nullptr)); }
inline const SimpleNavControl* NavCharacterAdapter::GetNavControl() const					{ return (m_pNavChar ? m_pNavChar->GetNavControl()					: (m_pSNavChar ? m_pSNavChar->GetNavControl()					: nullptr)); }
inline const NavLocation& NavCharacterAdapter::GetNavLocation() const						{ return GetNavControl()->GetNavLocation(); }
inline const IPathWaypoints* NavCharacterAdapter::GetPathPs() const							{ return (m_pNavChar ? m_pNavChar->GetPathPs()						: nullptr); }
inline const IPathWaypoints* NavCharacterAdapter::GetPostTapPathPs() const					{ return (m_pNavChar ? m_pNavChar->GetPostTapPathPs()				: nullptr); }
inline const IPathWaypoints* NavCharacterAdapter::GetLastFoundPathPs() const				{ return (m_pNavChar ? m_pNavChar->GetLastFoundPathPs()				: nullptr); }
inline const Locator NavCharacterAdapter::GetPathTapAnimAdjustLs() const					{ return m_pNavChar ? m_pNavChar->GetPathTapAnimAdjustLs() : kIdentity; }
inline const TraversalActionPack* NavCharacterAdapter::GetPathActionPack() const			{ return m_pNavChar ? m_pNavChar->GetPathActionPack() : nullptr; }
inline const char* NavCharacterAdapter::GetOverlayBaseName() const							{ return (m_pNavChar ? m_pNavChar->GetOverlayBaseName()				: (m_pSNavChar ? m_pSNavChar->GetOverlayBaseName()				: nullptr)); }
inline MotionType NavCharacterAdapter::GetCurrentMotionType() const							{ return (m_pNavChar ? m_pNavChar->GetCurrentMotionType()			: (m_pSNavChar ? m_pSNavChar->GetCurrentMotionType()			: kMotionTypeWalk)); }
inline StringId64 NavCharacterAdapter::GetCurrentMtSubcategory() const						{ return (m_pNavChar ? m_pNavChar->GetCurrentMtSubcategory()		: (m_pSNavChar ? m_pSNavChar->GetCurrentMtSubcategory()			: INVALID_STRING_ID_64)); }
inline StringId64 NavCharacterAdapter::GetRequestedMtSubcategory() const					{ return (m_pNavChar ? m_pNavChar->GetRequestedMtSubcategory()		: (m_pSNavChar ? m_pSNavChar->GetRequestedMtSubcategory()				: INVALID_STRING_ID_64)); }
inline MotionType NavCharacterAdapter::GetRequestedMotionType() const						{ return (m_pNavChar ? m_pNavChar->GetRequestedMotionType()			: (m_pSNavChar ? m_pSNavChar->GetRequestedMotionType()			: kMotionTypeWalk)); }
inline const Point NavCharacterAdapter::GetFacePositionPs() const							{ return (m_pNavChar ? m_pNavChar->GetFacePositionPs()				: (m_pSNavChar ? m_pSNavChar->GetFacePositionPs()				: Point(kOrigin))); }
inline I32 NavCharacterAdapter::GetRequestedDcDemeanor() const								{ return (m_pNavChar ? m_pNavChar->GetRequestedDcDemeanor()			: (m_pSNavChar ? m_pSNavChar->GetRequestedDcDemeanor()			: 0)); }
inline StringId64 NavCharacterAdapter::SidFromDemeanor(const Demeanor& d) const				{ return (m_pNavChar ? m_pNavChar->SidFromDemeanor(d)				: (m_pSNavChar ? m_pSNavChar->SidFromDemeanor(d)				: INVALID_STRING_ID_64)); }
inline GunState NavCharacterAdapter::GetCurrentGunState() const								{ return (m_pNavChar ? m_pNavChar->GetCurrentGunState()				: (m_pSNavChar ? m_pSNavChar->GetCurrentGunState()				: kGunStateHolstered)); }
inline Demeanor NavCharacterAdapter::GetRequestedDemeanor() const							{ return (m_pNavChar ? m_pNavChar->GetRequestedDemeanor()			: (m_pSNavChar ? m_pSNavChar->GetRequestedDemeanor()			: Demeanor(0))); }
inline Demeanor NavCharacterAdapter::GetCurrentDemeanor() const								{ return (m_pNavChar ? m_pNavChar->GetCurrentDemeanor()				: (m_pSNavChar ? m_pSNavChar->GetCurrentDemeanor()				: Demeanor(0))); }
inline Demeanor NavCharacterAdapter::GetCinematicActionPackDemeanor() const					{ return (m_pNavChar ? m_pNavChar->GetCinematicActionPackDemeanor()	: (m_pSNavChar ? m_pSNavChar->GetCinematicActionPackDemeanor()	: Demeanor(0))); }
inline bool NavCharacterAdapter::HasDemeanor(I32F demeanorIntVal) const						{ return (m_pNavChar ? m_pNavChar->HasDemeanor(demeanorIntVal)		: (m_pSNavChar ? m_pSNavChar->HasDemeanor(demeanorIntVal)		: false)); }
inline const NdAnimControllerConfig* NavCharacterAdapter::GetAnimControllerConfig() const	{ return (m_pNavChar ? m_pNavChar->GetAnimControllerConfig()		: (m_pSNavChar ? m_pSNavChar->GetAnimControllerConfig()			: nullptr)); }
inline ActionPack* NavCharacterAdapter::GetReservedActionPack() const						{ return (m_pNavChar ? m_pNavChar->GetReservedActionPack()			: (m_pSNavChar ? m_pSNavChar->GetReservedActionPack()			: nullptr)); }
inline ActionPack* NavCharacterAdapter::GetEnteredActionPack() const						{ return (m_pNavChar ? m_pNavChar->GetEnteredActionPack()			: (m_pSNavChar ? m_pSNavChar->GetEnteredActionPack()			: nullptr)); }
inline const TraversalActionPack* NavCharacterAdapter::GetTraversalActionPack() const		{ return (m_pNavChar ? m_pNavChar->GetTraversalActionPack()			: (m_pSNavChar ? m_pSNavChar->GetTraversalActionPack()			: nullptr)); }
inline const Vector NavCharacterAdapter::GetVelocityPs() const								{ return (m_pNavChar ? m_pNavChar->GetVelocityPs()					: (m_pSNavChar ? m_pSNavChar->GetVelocityPs()					: Vector(kZero))); }
inline bool NavCharacterAdapter::IsMoving() const											{ return (m_pNavChar ? m_pNavChar->IsMoving()						: (m_pSNavChar ? m_pSNavChar->IsMoving()						: false)); }
inline bool NavCharacterAdapter::IsStrafing() const											{ return (m_pNavChar ? m_pNavChar->IsStrafing()						: (m_pSNavChar ? m_pSNavChar->IsStrafing()						: false)); }
inline Point NavCharacterAdapter::GetTranslation() const									{ return (m_pNavChar ? m_pNavChar->GetTranslation()					: (m_pSNavChar ? m_pSNavChar->GetTranslation()					: kOrigin)); }
inline bool NavCharacterAdapter::ShouldKickDownDoor(const TraversalActionPack* pTap) const	{ return m_pNavChar ? m_pNavChar->ShouldKickDownDoor(pTap)			: false; }

/// --------------------------------------------------------------------------------------------------------------- ///
inline float NavCharacterAdapter::GetCurrentNavAdjustRadius() const
{
	if (m_pNavChar)
	{
		return m_pNavChar->GetCurrentNavAdjustRadius();
	}

	if (m_pSNavChar)
	{
		return m_pSNavChar->GetCurrentNavAdjustRadius();
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline float NavCharacterAdapter::GetMaximumNavAdjustRadius() const
{
	if (m_pNavChar)
	{
		return m_pNavChar->GetMaximumNavAdjustRadius();
	}

	if (m_pSNavChar)
	{
		return m_pSNavChar->GetMaximumNavAdjustRadius();
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline float NavCharacterAdapter::GetMovingNavAdjustRadius() const
{
	if (m_pNavChar)
	{
		return m_pNavChar->GetMovingNavAdjustRadius();
	}

	if (m_pSNavChar)
	{
		return m_pSNavChar->GetMovingNavAdjustRadius();
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline float NavCharacterAdapter::GetIdleNavAdjustRadius() const
{
	if (m_pNavChar)
	{
		return m_pNavChar->GetIdleNavAdjustRadius();
	}

	if (m_pSNavChar)
	{
		return m_pSNavChar->GetIdleNavAdjustRadius();
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline void NavCharacterAdapter::ConfigureNavigationHandOff(const NavAnimHandoffDesc& desc,
															const char* file,
															U32F line,
															const char* func)
{
	if (m_pNavChar)
		m_pNavChar->ConfigureNavigationHandOff(desc, file, line, func);
	else if (m_pSNavChar)
		m_pSNavChar->ConfigureNavigationHandOff(desc, file, line, func);
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline BoundFrame NavCharacterAdapter::AdjustBoundFrameToUpright(const BoundFrame& bf) const
{
	if (m_pNavChar)
		return m_pNavChar->AdjustBoundFrameToUpright(bf);
	else if (m_pSNavChar)
		return m_pSNavChar->AdjustBoundFrameToUpright(bf);
	return bf;
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline NdSubsystemMgr* NavCharacterAdapter::GetSubsystemMgr()
{
	return m_pNavChar ? m_pNavChar->GetSubsystemMgr() : (m_pSNavChar ? m_pSNavChar->GetSubsystemMgr() : nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline const NdSubsystemMgr* NavCharacterAdapter::GetSubsystemMgr() const
{
	return m_pNavChar ? m_pNavChar->GetSubsystemMgr() : (m_pSNavChar ? m_pSNavChar->GetSubsystemMgr() : nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline StringId64 NavCharacterAdapter::GetCapCharacterId() const
{
	return m_pNavChar ? m_pNavChar->GetCapCharacterId()
					  : (m_pSNavChar ? m_pSNavChar->GetCapCharacterId() : INVALID_STRING_ID_64);
}
