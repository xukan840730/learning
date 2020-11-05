/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-mesh-handle.h"

#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-ex-data.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-poly-ex.h"
#include "gamelib/gameplay/nav/nav-poly.h"

////////////////////////////////////////////////////////////////////////////////
///
///
/// NavMeshHandle - safe way to store a reference to a nav mesh
///
///
////////////////////////////////////////////////////////////////////////////////

/// --------------------------------------------------------------------------------------------------------------- ///
NavMeshHandle::NavMeshHandle(const NavMesh* pMesh)
{
	if (pMesh)
	{
		m_managerId = pMesh->m_managerId;
		ASSERT(!pMesh->IsRegistered() || (EngineComponents::GetNavMeshMgr()->LookupRegisteredNavMesh(*this) == pMesh));
	}
	else
	{
		m_managerId.Invalidate();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavMesh* NavMeshHandle::ToNavMesh() const
{
	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	const NavMesh* pMesh = nmMgr.LookupRegisteredNavMesh(*this);

	return pMesh;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshHandle::IsValid() const
{
	return EngineComponents::GetNavMeshMgr()->IsNavMeshHandleValid(*this);
}

////////////////////////////////////////////////////////////////////////////////
///
///
/// NavPolyHandle - safe way to store a reference to a nav poly
///
///
////////////////////////////////////////////////////////////////////////////////

/// --------------------------------------------------------------------------------------------------------------- ///
NavPolyHandle::NavPolyHandle(const NavPoly* pPoly)
{
	if (pPoly)
	{
		const NavMesh* pMesh = pPoly->GetNavMesh();
		NAV_ASSERT(pMesh);
		m_managerId = pMesh->m_managerId;
		m_managerId.m_iPoly = pPoly->GetId();
		//ASSERT(!pMesh->IsEnabled() || ToNavPoly() == pPoly);
	}
	else
	{
		m_managerId.Invalidate();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavMesh* NavPolyHandle::ToNavMesh() const
{
	NavMeshHandle hMesh(*this);
	return hMesh.ToNavMesh();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavPoly* NavPolyHandle::ToNavPoly(const NavMesh** ppMeshOut /* = nullptr */) const
{
	const NavPoly* pNavPoly = nullptr;
	const NavMesh* pNavMesh = ToNavMesh();

	if (pNavMesh && (m_managerId.m_iPoly < pNavMesh->GetPolyCount()))
	{
		pNavPoly = &pNavMesh->GetPoly(m_managerId.m_iPoly);
	}

	if (ppMeshOut)
		*ppMeshOut = pNavMesh;

	return pNavPoly;
}

////////////////////////////////////////////////////////////////////////////////
///
///
/// NavPolyExHandle - safe way to store a reference to a nav poly EXTREME
///
///
////////////////////////////////////////////////////////////////////////////////

/// --------------------------------------------------------------------------------------------------------------- ///
NavPolyExHandle::NavPolyExHandle(const NavPolyEx* pPolyEx)
{
	const NavPoly* pBasePoly = pPolyEx ? pPolyEx->GetBasePoly() : nullptr;
	if (pBasePoly)
	{
		const NavMesh* pMesh = pPolyEx->GetNavMesh();
		NAV_ASSERT(pMesh);
		m_managerId = pMesh->GetManagerId();
		m_managerId.m_iPoly = pBasePoly->GetId();
		m_managerId.m_iPolyEx = pPolyEx->GetId();
	}
	else
	{
		m_managerId.Invalidate();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavMesh* NavPolyExHandle::ToNavMesh() const
{
	NavMeshHandle hMesh(m_managerId);
	return hMesh.ToNavMesh();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavPoly* NavPolyExHandle::ToNavPoly() const
{
	const NavPoly* pNavPoly = nullptr;
	if (const NavMesh* pNavMesh = ToNavMesh())
	{
		if (m_managerId.m_iPoly < pNavMesh->GetPolyCount())
		{
			pNavPoly = &pNavMesh->GetPoly(m_managerId.m_iPoly);
		}
	}
	return pNavPoly;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavPolyEx* NavPolyExHandle::ToNavPolyEx() const
{
	return GetNavPolyExFromId(m_managerId.m_iPolyEx);
}
