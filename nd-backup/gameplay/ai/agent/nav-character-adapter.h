/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "corelib/system/templatized-identifier.h" // for StringId64

#include "ndlib/anim/anim-defines.h" // for MotionType

#include "gamelib/gameplay/ai/nav-character-anim-defines.h" // for Demeanor and GunState
#include "gamelib/scriptx/h/nav-character-defines.h"		// for DC::NpcStrafe

/// --------------------------------------------------------------------------------------------------------------- ///
class NavPoly;
class NavMesh;
class NavCharacter;
class SimpleNavCharacter;
class SimpleNavControl;
class Demeanor;
struct NdAnimControllerConfig;
struct NavAnimHandoffDesc;
class ActionPack;
class TraversalActionPack;
class NavLocation;
class IPathWaypoints;
class NdSubsystemMgr;

/// --------------------------------------------------------------------------------------------------------------- ///
// NavCharacterAdapter
//
// Adapter that shunts its interface either to NavCharacter or SimpleNavCharacter as appropriate.
/// --------------------------------------------------------------------------------------------------------------- ///
class NavCharacterAdapter
{
public:
	static NavCharacterAdapter FromProcess(const Process* pProc);

	NavCharacterAdapter(NavCharacter* pNavChar)
		: m_pNavChar(pNavChar)
		, m_pSNavChar(nullptr) {} // implicit to ease transition from NavCharacter* to NavCharacterAdapter
	NavCharacterAdapter(const NavCharacter* pNavChar)
		: m_pNavChar(const_cast<NavCharacter*>(pNavChar)), m_pSNavChar(nullptr)
	{
	} // implicit to ease transition from NavCharacter* to NavCharacterAdapter

	explicit NavCharacterAdapter(SimpleNavCharacter* sNavChar) : m_pNavChar(nullptr), m_pSNavChar(sNavChar) {}
	explicit NavCharacterAdapter(const SimpleNavCharacter* sNavChar)
		: m_pNavChar(nullptr), m_pSNavChar(const_cast<SimpleNavCharacter*>(sNavChar))
	{
	}

	// Little trick to allow us to treat a NavCharacterAdapter (an object) like a NavCharacter* or SimpleNavCharacter* (a pointer).
	NavCharacterAdapter* operator->() { return this; }
	const NavCharacterAdapter* operator->() const { return this; }
	NavCharacterAdapter* operator*() { return this; }
	const NavCharacterAdapter* operator*() const { return this; }
	bool IsValid() const { return (m_pNavChar || m_pSNavChar); }
	operator bool() const { return IsValid(); }

	const NdGameObject* ToGameObject() const;
	NdGameObject* ToGameObject();

	const NavCharacter* ToNavCharacter() const { return m_pNavChar; }
	NavCharacter* ToNavCharacter() { return m_pNavChar; }

	const SimpleNavCharacter* ToSimpleNavCharacter() const { return m_pSNavChar; }
	SimpleNavCharacter* ToSimpleNavCharacter() { return m_pSNavChar; }

	SimpleNavControl* GetNavControl();
	const SimpleNavControl* GetNavControl() const;
	const NavLocation& GetNavLocation() const;

	const IPathWaypoints* GetPathPs() const;
	const IPathWaypoints* GetPostTapPathPs() const;
	const IPathWaypoints* GetLastFoundPathPs() const;
	const Locator GetPathTapAnimAdjustLs() const;
	const TraversalActionPack* GetPathActionPack() const;

	float GetCurrentNavAdjustRadius() const;
	float GetMaximumNavAdjustRadius() const;
	float GetMovingNavAdjustRadius() const;
	float GetIdleNavAdjustRadius() const;

	const Point GetFacePositionPs() const;

	const char* GetOverlayBaseName() const;

	// change how the character moves (walk, run, sprint, etc) without changing the path
	MotionType GetCurrentMotionType() const;
	StringId64 GetCurrentMtSubcategory() const;
	StringId64 GetRequestedMtSubcategory() const;
	MotionType GetRequestedMotionType() const;
	bool IsStrafing() const;

	bool ShouldKickDownDoor(const TraversalActionPack* pTap) const;

	I32 GetRequestedDcDemeanor() const;
	Demeanor GetRequestedDemeanor() const;
	Demeanor GetCurrentDemeanor() const;
	Demeanor GetCinematicActionPackDemeanor() const;
	StringId64 SidFromDemeanor(const Demeanor& demeanor) const;
	GunState GetCurrentGunState() const;
	bool HasDemeanor(I32F demeanorIntVal) const;

	const NdAnimControllerConfig* GetAnimControllerConfig() const;

	ActionPack* GetReservedActionPack() const;
	ActionPack* GetEnteredActionPack() const;
	const TraversalActionPack* GetTraversalActionPack() const;
	void ConfigureNavigationHandOff(const NavAnimHandoffDesc& desc, const char* file, U32F line, const char* func);
	BoundFrame AdjustBoundFrameToUpright(const BoundFrame& bf) const;

	const Vector GetVelocityPs() const;
	bool IsMoving() const;

	Point GetTranslation() const;

	NdSubsystemMgr* GetSubsystemMgr();
	const NdSubsystemMgr* GetSubsystemMgr() const;

	StringId64 GetCapCharacterId() const;

private:
	NavCharacter* m_pNavChar;
	SimpleNavCharacter* m_pSNavChar;
};
