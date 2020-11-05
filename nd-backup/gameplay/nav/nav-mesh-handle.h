/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/util/hashfunctions.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NavMesh;
class NavPoly;
class NavPolyEx;
class NavPolyHandle;

typedef U16 NavPolyId;
static CONST_EXPR NavPolyId kInvalidNavPolyId = NavPolyId(-1);

/// --------------------------------------------------------------------------------------------------------------- ///
class NavManagerId
{
public:
	const static U64 kInvalidMgrId = 0;
	union
	{
		struct
		{
			U16		m_navMeshIndex;
			U16		m_uniqueId;
			U16		m_iPoly;
			U16		m_iPolyEx;
		};
		U64 m_u64;
	};

	NavManagerId(U64 id = kInvalidMgrId) : m_u64(id) {}

	NavManagerId(U16 navMeshIndex, U16 uniqueId, U16 iPoly, U16 iPolyEx) : m_navMeshIndex(navMeshIndex), m_uniqueId(uniqueId), m_iPoly(iPoly), m_iPolyEx(iPolyEx) {}

	void Invalidate()
	{
		m_u64 = kInvalidMgrId;
	}

	bool IsValid() const
	{
		return !IsNull();
	}

	bool IsNull() const
	{
		return m_u64 == kInvalidMgrId;
	}

	inline U64 AsU64() const
	{
		return m_u64;
	}

	inline void FromU64(U64 from)
	{
		m_u64 = from;
	}

	bool operator == (const NavManagerId& rhs) const { return m_u64 == rhs.m_u64; }
	bool operator != (const NavManagerId& rhs) const { return m_u64 != rhs.m_u64; }
};

STATIC_ASSERT(sizeof(NavManagerId) == sizeof(U64));

/// --------------------------------------------------------------------------------------------------------------- ///
class NavMeshHandle
{
public:
	const static U64 kInvalidMgrId = 0;
	friend class NavMesh;
	friend class NavMeshMgr;

	NavMeshHandle() { m_managerId.Invalidate(); }
	NavMeshHandle(const NavMesh* pMesh);
	NavMeshHandle(const NavPolyHandle& hPoly);
	NavMeshHandle(const NavMeshHandle&) = default;
	explicit NavMeshHandle(const NavManagerId& managerId) { m_managerId = managerId; m_managerId.m_iPoly = 0; m_managerId.m_iPolyEx = 0; }

	const NavMesh* ToNavMesh() const;

	bool IsValid() const;
	bool IsNull() const { return m_managerId.AsU64() == NavManagerId::kInvalidMgrId; }
	const NavManagerId GetManagerId() const { return m_managerId; }
	inline U64 AsU64() const { return m_managerId.AsU64(); }
	inline void FromU64(U64 from) { m_managerId.FromU64(from); }

	bool operator==(const NavMeshHandle& rhs) const { return m_managerId == rhs.m_managerId; }
	bool operator!=(const NavMeshHandle& rhs) const { return m_managerId != rhs.m_managerId; }

private:
	NavManagerId m_managerId;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NavPolyHandle
{
public:
	const static U64 kInvalidMgrId = 0;

	NavPolyHandle() { m_managerId.Invalidate(); }
	NavPolyHandle(const NavMeshHandle& hMesh, U32 iPoly);
	explicit NavPolyHandle(const NavManagerId& managerId)
	{
		m_managerId = managerId;
		m_managerId.m_iPolyEx = 0;
	}

	NavPolyHandle(const NavPoly* pPoly);

	const NavMesh* ToNavMesh() const;
	const NavPoly* ToNavPoly(const NavMesh** ppMeshOut = nullptr) const;
	NavMeshHandle ToNavMeshHandle() const { return NavMeshHandle(m_managerId); }

	bool IsNull() const { return m_managerId.AsU64() == NavManagerId::kInvalidMgrId; }

	const NavManagerId GetManagerId() const { return m_managerId; }
	inline U64 AsU64() const { return m_managerId.AsU64(); }
	inline void FromU64(U64 from) { m_managerId.FromU64(from); }

	U32 GetPolyId() const { return m_managerId.m_iPoly; }

	// I need to load the poly ID and 2 bytes past it directly into an XMM as fast as possible.
	// Can't do that without the pointer.
	// - komar
	const NavPolyId* GetPointerToPolyId() const { return &m_managerId.m_iPoly; }

	bool operator==(const NavPolyHandle& rhs) const { return m_managerId == rhs.m_managerId; }
	bool operator!=(const NavPolyHandle& rhs) const { return m_managerId != rhs.m_managerId; }

private:
	NavManagerId m_managerId;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NavPolyExHandle
{
public:
	const static U64 kInvalidMgrId = 0;

	NavPolyExHandle() { m_managerId.Invalidate(); }
	NavPolyExHandle(const NavMeshHandle& hMesh, U32 iPoly, U32 iPolyEx);
	explicit NavPolyExHandle(const NavManagerId& managerId) { m_managerId = managerId; }

	NavPolyExHandle(const NavPolyEx* pPolyEx);

	const NavMesh* ToNavMesh() const;
	const NavPoly* ToNavPoly() const;
	const NavPolyEx* ToNavPolyEx() const;
	NavMeshHandle ToNavMeshHandle() const { return NavMeshHandle(m_managerId); }

	bool IsNull() const { return m_managerId.AsU64() == NavManagerId::kInvalidMgrId; }
	const NavManagerId GetManagerId() const { return m_managerId; }
	inline U64 AsU64() const { return m_managerId.AsU64(); }
	inline void FromU64(U64 from) { m_managerId.FromU64(from); }

	U32 GetPolyId() const { return m_managerId.m_iPoly; }
	U32 GetPolyExId() const { return m_managerId.m_iPolyEx; }

	bool operator==(const NavPolyExHandle& rhs) const { return m_managerId == rhs.m_managerId; }
	bool operator!=(const NavPolyExHandle& rhs) const { return m_managerId != rhs.m_managerId; }

private:
	NavManagerId m_managerId;
};

/// --------------------------------------------------------------------------------------------------------------- ///
FORCE_INLINE NavPolyHandle::NavPolyHandle(const NavMeshHandle& hMesh, U32 iPoly)
{
	m_managerId = hMesh.GetManagerId();
	m_managerId.m_iPoly = iPoly;
}

/// --------------------------------------------------------------------------------------------------------------- ///
FORCE_INLINE NavMeshHandle::NavMeshHandle(const NavPolyHandle& hPoly)
{
	m_managerId = hPoly.GetManagerId();
	m_managerId.m_iPoly = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <>
struct HashT<NavManagerId>
{
	typedef StringId64 Type;
	typedef U32 Result;
	inline U32 operator()(const NavManagerId& mgrId) const
	{
		const U64 intVal = mgrId.AsU64();
		return U32(intVal);
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
template <>
struct HashT<NavMeshHandle>
{
	typedef StringId64 Type;
	typedef U32 Result;
	inline U32 operator()(const NavMeshHandle& hMesh) const
	{
		const U64 intVal = hMesh.AsU64();
		return U32(intVal);
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
template <>
struct HashT<NavPolyHandle>
{
	typedef StringId64 Type;
	typedef U32 Result;
	inline U32 operator()(const NavPolyHandle& hPoly) const
	{
		const U64 intVal = hPoly.AsU64();
		return U32(intVal);
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
template <>
struct HashT<NavPolyExHandle>
{
	typedef StringId64 Type;
	typedef U32 Result;
	inline U32 operator()(const NavPolyExHandle& hPolyEx) const
	{
		const U64 intVal = hPolyEx.AsU64();
		return U32(intVal);
	}
};
