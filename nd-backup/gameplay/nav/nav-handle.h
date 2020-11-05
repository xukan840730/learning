/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nav/nav-ledge-graph-handle.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"

class DoutBase;
#if ENABLE_NAV_LEDGES
class NavLedge;
class NavLedgeGraph;
#endif
class NavMesh;
class NavPoly;

/// --------------------------------------------------------------------------------------------------------------- ///
class NavHandle
{
public:
	enum class Type
	{
		kNone,
		kNavPoly,
#if ENABLE_NAV_LEDGES
		kNavLedge
#endif
	};

	static const char* GetTypeName(Type t)
	{
		switch (t)
		{
		case Type::kNone: return "None";
		case Type::kNavPoly: return "NavPoly";
#if ENABLE_NAV_LEDGES
		case Type::kNavLedge: return "NavLedge";
#endif
		}
		return "<unknown>";
	}

	NavHandle()
	{
		SetNone();
	}

	bool IsValid() const;

	U32F GetPathNodeId() const;
	Type GetType() const { return m_type; }
	StringId64 GetBindSpawnerNameId() const;

	void SetNone()
	{
		m_hNavPoly = NavPolyHandle();
#if ENABLE_NAV_LEDGES
		m_hNavLedge = NavLedgeHandle();
#endif
		m_type = Type::kNone;
	}

	inline bool IsNull() const
	{
		switch (m_type)
		{
			case Type::kNavPoly: return m_hNavPoly.IsNull();
#if ENABLE_NAV_LEDGES
			case Type::kNavLedge: return m_hNavLedge.IsNull();
#endif
		}

		return true;
	}

	void SetNavPoly(const NavPoly* pPoly)
	{
		m_hNavPoly = pPoly;
#if ENABLE_NAV_LEDGES
		m_hNavLedge = NavLedgeHandle();
#endif
		m_type = Type::kNavPoly;
	}

	void SetNavPoly(const NavPolyHandle hPoly)
	{
		m_hNavPoly = hPoly;
#if ENABLE_NAV_LEDGES
		m_hNavLedge = NavLedgeHandle();
#endif
		m_type = Type::kNavPoly;
	}

#if ENABLE_NAV_LEDGES
	void SetNavLedge(const NavLedge* pLedge)		{ m_hNavPoly = NavPolyHandle(); m_hNavLedge = pLedge; m_type = Type::kNavLedge; }
	void SetNavLedge(const NavLedgeHandle hLedge)	{ m_hNavPoly = NavPolyHandle(); m_hNavLedge = hLedge; m_type = Type::kNavLedge; }
	const NavManagerId GetNavManagerId() const { return (m_type == Type::kNavPoly) ? m_hNavPoly.GetManagerId() : NavManagerId::kInvalidMgrId; }
	const NavPolyHandle GetNavPolyHandle() const { return (m_type == Type::kNavPoly) ? m_hNavPoly : NavPolyHandle(); }
	const NavMeshHandle GetNavMeshHandle() const { return (m_type == Type::kNavPoly) ? NavMeshHandle(m_hNavPoly) : NavMeshHandle(); }
#else
	const NavManagerId GetNavManagerId() const { return m_hNavPoly.GetManagerId(); }
	const NavPolyHandle GetNavPolyHandle() const { return m_hNavPoly; }
	const NavMeshHandle GetNavMeshHandle() const { return NavMeshHandle(m_hNavPoly); }
#endif

	const NavPoly* ToNavPoly(const NavMesh** ppMeshOut = nullptr) const;
	const NavMesh* ToNavMesh() const;

#if ENABLE_NAV_LEDGES
	const NavLedgeHandle GetNavLedgeHandle() const				{ return (m_type == Type::kNavLedge) ? m_hNavLedge : NavLedgeHandle(); }
	const NavLedgeGraphHandle GetNavLedgeGraphHandle() const	{ return (m_type == Type::kNavLedge) ? (NavLedgeGraphHandle)m_hNavLedge : NavLedgeGraphHandle(); }
	const NavLedge* ToNavLedge() const;
	const NavLedgeGraph* ToNavLedgeGraph() const;
#endif

	Point WorldToParent(const Point posWs) const;
	Vector WorldToParent(const Vector posWs) const;

	bool SameShapeAs(const NavHandle& rhs) const;
	const char* GetShapeName() const;

	void DebugDraw(Color clr = kColorYellow, DebugPrimTime tt = kPrimDuration1FrameAuto) const;
	void DebugPrint(DoutBase* pDout) const;

private:
	NavPolyHandle m_hNavPoly;
#if ENABLE_NAV_LEDGES
	NavLedgeHandle m_hNavLedge;
#endif
	Type m_type;
};
