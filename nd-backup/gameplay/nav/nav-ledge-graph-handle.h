/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef NAV_LEDGE_GRAPH_HANDLE_H
#define NAV_LEDGE_GRAPH_HANDLE_H

#if ENABLE_NAV_LEDGES

class NavLedge;
class NavLedgeGraph;
class NavLedgeHandle;

/// --------------------------------------------------------------------------------------------------------------- ///
class NavLedgeGraphHandle
{
public:
	NavLedgeGraphHandle() { Invalidate(); }
	NavLedgeGraphHandle(const NavLedge* pLedge);
	NavLedgeGraphHandle(const NavLedgeGraph* pGraph);
	NavLedgeGraphHandle(const NavLedgeHandle& hLedge);

	void Invalidate() { m_u32 = 0; }

	const NavLedgeGraph* ToLedgeGraph() const;

	bool IsValid() const;
	bool IsNull() const { return m_u32 == 0; }
	U32F GetGraphIndex() const { return m_managerIndex; }

	bool operator == (const NavLedgeGraphHandle& rhs) const { return m_u32 == rhs.m_u32; }
	bool operator != (const NavLedgeGraphHandle& rhs) const { return m_u32 != rhs.m_u32; }

protected:
	friend class NavLedgeGraphMgr;

	union
	{
		struct
		{
			U16	m_managerIndex;
			U16	m_uniqueId;
		};
		U32 m_u32;
	};
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NavLedgeHandle : public NavLedgeGraphHandle
{
public:
	typedef NavLedgeGraphHandle ParentClass;

	NavLedgeHandle() { Invalidate(); }

	NavLedgeHandle(const NavLedge* pLedge);

	const NavLedge* ToLedge() const;
	U32F GetLedgeId() const { return m_ledgeIndex; }
	void Invalidate() { m_u32 = m_ledgeIndex = 0; }

	bool IsValid() const;
	bool IsNull() const { return m_u32 == 0; }

	bool operator == (const NavLedgeHandle& rhs) const { return (m_u32 == rhs.m_u32) && (m_ledgeIndex == rhs.m_ledgeIndex); }
	bool operator != (const NavLedgeHandle& rhs) const { return !(*this == rhs); }

private:
	U32 m_ledgeIndex;
};

#endif // ENABLE_NAV_LEDGES

#endif // NAV_LEDGE_GRAPH_HANDLE_H
