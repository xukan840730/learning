/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef NAV_LEDGE_H
#define NAV_LEDGE_H

#if ENABLE_NAV_LEDGES

#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-handle.h"

class ActionPack;
class NavLedgeGraph;

typedef U32 NavLedgeId;

/// --------------------------------------------------------------------------------------------------------------- ///
class NavLedge
{
public:
	union Flags
	{
		struct
		{
			bool m_error	: 1;
			bool m_blocked	: 1;
		};
		U16 m_u16;
	};

	struct LinkDistInfo
	{
		float m_srcTT;
		float m_destTT;
		float m_dist;
	};

	struct Link
	{
		NavLedgeId m_destLedgeId;
		LinkDistInfo m_closest;
		LinkDistInfo m_v0;
		LinkDistInfo m_v1;
	};

	NavLedgeId GetId() const { return m_id; }
	U32F GetPathNodeId() const { return m_pathNodeId; }

	const NavLedgeGraphHandle&	GetNavLedgeGraphHandle()	const { return m_hLedgeGraph; }
	const NavLedgeGraph*		GetNavLedgeGraph()			const { return GetNavLedgeGraphHandle().ToLedgeGraph(); }
	
	Point GetVertex0Ls() const { return m_vertsLs[0]; }
	Point GetVertex1Ls() const { return m_vertsLs[1]; }
	Point GetCenterLs() const { return AveragePos(m_vertsLs[0], m_vertsLs[1]); }
	Vector GetWallNormalLs() const { return m_wallNormalLs; }
	Vector GetWallBinormalLs() const { return m_wallBinormalLs; }

	U32F GetNumNeighbors() const { return m_numNeighbors; }
	const Link& GetLink(U32F iNeighbor) const { NAV_ASSERT(iNeighbor < m_numNeighbors); return m_pNeighbors[iNeighbor]; }

	U64 GetFeatureFlags() const { return m_featureFlags; }

	ActionPack*	GetRegisteredActionPackList() const { return m_pRegistrationList; }
	void		RegisterActionPack(ActionPack* pActionPack);
	bool		UnregisterActionPack(ActionPack* pActionPack);
	void		RelocateActionPack(ActionPack* pActionPack, ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound);
	void		ValidateRegisteredActionPacks() const;

	void		DebugDraw(const Color& color) const;

private:
	friend class NavLedgeGraph;
	friend class NavPathNodeMgr;

	Point		m_vertsLs[2];
	Vector		m_wallNormalLs;
	Vector		m_wallBinormalLs;

	Link*		m_pNeighbors;

	U32			m_numNeighbors;
	NavLedgeId	m_id;

	U64			m_featureFlags;
	ActionPack*	m_pRegistrationList;

	Flags		m_flags;
	U16			m_pathNodeId;

	NavLedgeGraphHandle m_hLedgeGraph;
};

#endif // ENABLE_NAV_LEDGES

#endif // NAV_LEDGE_H
