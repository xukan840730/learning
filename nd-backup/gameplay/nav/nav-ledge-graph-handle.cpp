/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#if ENABLE_NAV_LEDGES

#include "gamelib/gameplay/nav/nav-ledge-graph-handle.h"
#include "gamelib/gameplay/nav/nav-ledge.h"
#include "gamelib/gameplay/nav/nav-ledge-graph.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-mgr.h"

/// --------------------------------------------------------------------------------------------------------------- ///
NavLedgeGraphHandle::NavLedgeGraphHandle(const NavLedgeHandle& hLedge)
{
	m_u32 = hLedge.m_u32;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLedgeGraphHandle::NavLedgeGraphHandle(const NavLedge* pLedge)
{
	if (pLedge)
	{
		*this = pLedge->GetNavLedgeGraphHandle();
	}
	else
	{
		Invalidate();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLedgeGraphHandle::NavLedgeGraphHandle(const NavLedgeGraph* pGraph)
{
	if (pGraph)
	{
		*this = pGraph->GetNavLedgeGraphHandle();
	}
	else
	{
		Invalidate();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavLedgeGraph* NavLedgeGraphHandle::ToLedgeGraph() const
{
	if (m_u32 == 0)
		return nullptr;

	const NavLedgeGraphMgr& lgMgr = NavLedgeGraphMgr::Get();
	return lgMgr.LookupRegisteredNavLedgeGraph(*this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeGraphHandle::IsValid() const
{
	if (m_u32 == 0)
		return false;

	return NavLedgeGraphMgr::Get().IsNavLedgeGraphHandleValid(*this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLedgeHandle::NavLedgeHandle(const NavLedge* pLedge)
	: ParentClass(pLedge)
{
	if (pLedge)
	{
		m_ledgeIndex = pLedge->GetId();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavLedge* NavLedgeHandle::ToLedge() const
{
	const NavLedge* pRet = nullptr;

	if (const NavLedgeGraph* pGraph = ToLedgeGraph())
	{
		pRet = pGraph->GetLedge(m_ledgeIndex);
	}

	return pRet;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavLedgeHandle::IsValid() const
{
	return ToLedge() != nullptr;
}

#endif // ENABLE_NAV_LEDGES
