/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#if ENABLE_NAV_LEDGES

#include "gamelib/gameplay/nav/nav-ledge-graph-handle.h"
#include "ndlib/text/stringid-selection.h"

class NavLedgeGraph;
struct FindNavLedgeGraphParams;

/// --------------------------------------------------------------------------------------------------------------- ///
class NavLedgeGraphMgr
{
public:
	typedef void(*PFnNotifyObserver)(const NavLedgeGraph* pGraph);

	const static U32F kMaxNavLedgeGraphCount = 128;
	typedef BitArray<kMaxNavLedgeGraphCount> NavLedgeGraphBits;

	static NavLedgeGraphMgr& Get() { return *s_pSingleton; }

	NavLedgeGraphMgr();

	void Init();
	void Shutdown();

	void AddLoginObserver(PFnNotifyObserver fn);
	void AddRegisterObserver(PFnNotifyObserver fn);
	void AddUnregisterObserver(PFnNotifyObserver fn);
	void AddLogoutObserver(PFnNotifyObserver fn);

	bool AddLedgeGraph(NavLedgeGraph* pGraphToAdd);
	void SetRegistrationEnabled(NavLedgeGraph* pGraph, bool enabled);
	void RemoveLedgeGraph(NavLedgeGraph* pGraphToRemove);

	void OnLogin(NavLedgeGraph* pGraph);
	void OnRegister(NavLedgeGraph* pGraph);
	void OnUnregister(NavLedgeGraph* pGraph);
	void OnLogout(NavLedgeGraph* pGraph);

	const NavLedgeGraph* LookupRegisteredNavLedgeGraph(NavLedgeGraphHandle hLedgeGraph) const;
	const NavLedgeGraph* GetNavLedgeGraphFromHandle(NavLedgeGraphHandle hLedgeGraph) const;
	bool IsNavLedgeGraphHandleValid(NavLedgeGraphHandle hLedgeGraph) const;

	U32F GetNavLedgeGraphList(NavLedgeGraph** navLedgeGraphList, U32F maxListLen) const;

	bool FindLedgeGraph(FindNavLedgeGraphParams* pParams) const;

private:
	struct NavLedgeGraphEntry
	{
		NavLedgeGraphEntry()
			: m_pLedgeGraph(nullptr)
			, m_bindSpawnerNameId(INVALID_STRING_ID_64)
			, m_hLedgeGraph()
			, m_registered(false)
			, m_loc(kIdentity)
			, m_boundingBoxLs()
		{}

		NavLedgeGraph*		m_pLedgeGraph;
		StringId64			m_bindSpawnerNameId;
		NavLedgeGraphHandle	m_hLedgeGraph;
		bool				m_registered;
		BoundFrame			m_loc;
		Aabb				m_boundingBoxLs;
	};

	void UpdateLedgeGraphBBox(U32F iEntry);

	static const size_t kMaxObservers = 4;

	static NavLedgeGraphMgr* s_pSingleton;

	mutable NdAtomicLock	m_accessLock;

	NavLedgeGraphBits		m_usedLedgeGraphs;
	NavLedgeGraphEntry		m_ledgeGraphEntries[kMaxNavLedgeGraphCount];
	U32						m_idCounter;

	PFnNotifyObserver		m_fnLoginObservers[kMaxObservers];
	PFnNotifyObserver		m_fnRegisterObservers[kMaxObservers];
	PFnNotifyObserver		m_fnUnregisterObservers[kMaxObservers];
	PFnNotifyObserver		m_fnLogoutObservers[kMaxObservers];

	U32						m_numLoginObservers;
	U32						m_numRegisterObservers;
	U32						m_numUnregisterObservers;
	U32						m_numLogoutObservers;

	StringIdSelection		m_selection;
	StringId64*				m_pSelectionStorage;

	NavLedgeGraphMgr(const NavLedgeGraphMgr&) {} // NOPE
};

#endif // ENABLE_NAV_LEDGES
