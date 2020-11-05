/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-handle.h"

#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-ledge-graph.h"
#include "gamelib/gameplay/nav/nav-ledge.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/nav-poly.h"

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavHandle::IsValid() const
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	bool valid = false;
	switch (m_type)
	{
	case Type::kNavPoly:
		valid = m_hNavPoly.ToNavPoly() != nullptr;
		break;
#if ENABLE_NAV_LEDGES
	case Type::kNavLedge:
		valid = m_hNavLedge.ToLedge() != nullptr;
		break;
#endif
	}
	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavHandle::GetPathNodeId() const
{
	U32F pathNodeId = NavPathNodeMgr::kInvalidNodeId;

	switch (m_type)
	{
	case Type::kNavPoly:
		{
			if (const NavPoly* pNavPoly = ToNavPoly())
			{
				pathNodeId = pNavPoly->GetPathNodeId();
			}
		}
		break;
#if ENABLE_NAV_LEDGES
	case Type::kNavLedge:
		{
			if (const NavLedge* pNavLedge = ToNavLedge())
			{
				pathNodeId = pNavLedge->GetPathNodeId();
			}
		}
		break;
#endif
	}

	return pathNodeId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NavHandle::GetBindSpawnerNameId() const
{
	StringId64 bindId = INVALID_STRING_ID_64;

	switch (m_type)
	{
	case Type::kNavPoly:
		{
			if (const NavMesh* pNavMesh = ToNavMesh())
			{
				bindId = pNavMesh->GetBindSpawnerNameId();
			}
		}
		break;
#if ENABLE_NAV_LEDGES
	case Type::kNavLedge:
		{
			if (const NavLedgeGraph* pLedgeGraph = ToNavLedgeGraph())
			{
				bindId = pLedgeGraph->GetBindSpawnerNameId();
			}
		}
		break;
#endif
	}

	return bindId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavPoly* NavHandle::ToNavPoly(const NavMesh** ppMeshOut /* = nullptr */) const
{
	if (m_type != Type::kNavPoly)
		return nullptr;

	return m_hNavPoly.ToNavPoly(ppMeshOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavMesh* NavHandle::ToNavMesh() const
{
	if (m_type != Type::kNavPoly)
		return nullptr;

	return m_hNavPoly.ToNavMesh();
}

#if ENABLE_NAV_LEDGES
/// --------------------------------------------------------------------------------------------------------------- ///
const NavLedge* NavHandle::ToNavLedge() const
{
	if (m_type != Type::kNavLedge)
		return nullptr;

	return m_hNavLedge.ToLedge();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavLedgeGraph* NavHandle::ToNavLedgeGraph() const
{
	if (m_type != Type::kNavLedge)
		return nullptr;

	return m_hNavLedge.ToLedgeGraph();
}
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
Point NavHandle::WorldToParent(const Point posWs) const
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	switch (m_type)
	{
	case Type::kNavPoly:
		{
			if (const NavMesh* pNavMesh = ToNavMesh())
			{
				return pNavMesh->WorldToParent(posWs);
			}
		}
		break;
#if ENABLE_NAV_LEDGES
	case Type::kNavLedge:
		{
			if (const NavLedgeGraph* pLedgeGraph = ToNavLedgeGraph())
			{
				return pLedgeGraph->WorldToParent(posWs);
			}
		}
		break;
#endif
	}

	return posWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector NavHandle::WorldToParent(const Vector vecWs) const
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	switch (m_type)
	{
	case Type::kNavPoly:
	{
		if (const NavMesh* pNavMesh = ToNavMesh())
		{
			return pNavMesh->WorldToParent(vecWs);
		}
	}
	break;
#if ENABLE_NAV_LEDGES
	case Type::kNavLedge:
	{
		if (const NavLedgeGraph* pLedgeGraph = ToNavLedgeGraph())
		{
			return pLedgeGraph->WorldToParent(vecWs);
		}
	}
	break;
#endif
	}

	return vecWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavHandle::SameShapeAs(const NavHandle& rhs) const
{
	if (m_type != rhs.m_type)
		return false;

	bool same = true;

	switch (m_type)
	{
	case Type::kNavPoly:
		same = m_hNavPoly.GetManagerId().m_navMeshIndex == rhs.m_hNavPoly.GetManagerId().m_navMeshIndex;
		break;
#if ENABLE_NAV_LEDGES
	case Type::kNavLedge:
		same = m_hNavLedge.GetGraphIndex() == rhs.m_hNavLedge.GetGraphIndex();
		break;
#endif
	}

	return same;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* NavHandle::GetShapeName() const
{
	switch (m_type)
	{
	case Type::kNavPoly:
		{
			if (const NavMesh* pMesh = ToNavMesh())
			{
				return pMesh->GetName();
			}
			else
			{
				return "<none>";
			}
		}
		break;
#if ENABLE_NAV_LEDGES
	case Type::kNavLedge:
		{
			if (const NavLedgeGraph* pGraph = ToNavLedgeGraph())
			{
				return pGraph->GetName();
			}
			else
			{
				return "<none>";
			}
		}
		break;
#endif

	case Type::kNone:
		return "<none>";
	}

	return "<unknown>";
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavHandle::DebugDraw(Color clr /* = kColorYellow */, DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	switch (m_type)
	{
	case Type::kNavPoly:
		{
			if (const NavPoly* pPoly = ToNavPoly())
			{
				pPoly->DebugDrawEdges(clr, clr, 0.01f, 3.0f, tt);
			}
		}
		break;
#if ENABLE_NAV_LEDGES
	case Type::kNavLedge:
		{
			if (const NavLedge* pLedge = ToNavLedge())
			{
				pLedge->DebugDraw(clr);
			}
		}
		break;
#endif
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavHandle::DebugPrint(DoutBase* pDout) const
{
	STRIP_IN_FINAL_BUILD;

	if (!pDout)
		return;

	switch (m_type)
	{
	case Type::kNavPoly:
		pDout->Printf("[NavPoly iMesh: %d, iPoly: %d]", m_hNavPoly.GetManagerId().m_navMeshIndex, m_hNavPoly.GetPolyId());
		break;
#if ENABLE_NAV_LEDGES
	case Type::kNavLedge:
		pDout->Printf("[NavLedge iGraph: %d, iLedge: %d]", m_hNavLedge.GetGraphIndex(), m_hNavLedge.GetLedgeId());
		break;
#endif
	case Type::kNone:
		pDout->Printf("[None]");
		break;
	}
}
