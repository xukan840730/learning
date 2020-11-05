/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef NAV_LEDGE_GRAPH_H
#define NAV_LEDGE_GRAPH_H

#if ENABLE_NAV_LEDGES

#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-handle.h"
#include "gamelib/gameplay/nav/nav-ledge.h"
#include "gamelib/level/entitydb.h"
#include "ndlib/io/pak-structs.h"

class Level;

/// --------------------------------------------------------------------------------------------------------------- ///
class NavLedgeGraph : public ResItem
{
public:
	static const U32 kVersionId = 6;
	static const U32 kSignature = 0x4c656447; // hex for "LedG"
	static const size_t kGameDataBufferSize = 256;

	struct GameData
	{
		bool m_registered;
		bool m_attached;
	};

	const char*	GetName() const					{ return m_strName; }
	StringId64	GetNameId() const				{ return m_nameId; }
	StringId64	GetLevelId() const				{ return m_levelId; }
	StringId64	GetBindSpawnerNameId() const	{ return m_bindSpawnerNameId; }
	size_t		GetLedgeCount() const			{ return m_numLedges; }
	Aabb		GetBoundingBoxLs() const		{ return Aabb(m_bboxMinLs, m_bboxMaxLs); }

	bool		IsLoggedIn() const				{ return !m_handle.IsNull(); }
	bool		IsRegistered() const			{ return m_gameData.m_registered; }
	bool		IsAttached() const				{ return m_gameData.m_attached; }

	void		SetAttached(bool f)				{ m_gameData.m_attached = f; }

	NavLedgeGraphHandle GetNavLedgeGraphHandle() const { return m_handle; }

	void Login(Level* pLevel);
	void Register();
	void Unregister();
	void Logout(Level* pLevel);

	void UnregisterAllActionPacks();

	const Point ParentToLocal(Point_arg posPs) const { return GetBoundFrame().GetLocatorPs().UntransformPoint(posPs); }
	const Point LocalToParent(Point_arg posLs) const { return GetBoundFrame().GetLocatorPs().TransformPoint(posLs); }

	const Point WorldToParent(Point_arg posWs) const { return GetBoundFrame().GetParentSpace().UntransformPoint(posWs); }
	const Point ParentToWorld(Point_arg posPs) const { return GetBoundFrame().GetParentSpace().TransformPoint(posPs); }

	const Vector WorldToParent(Vector_arg vecWs) const { return GetBoundFrame().GetParentSpace().UntransformVector(vecWs); }
	const Vector ParentToWorld(Vector_arg vecPs) const { return GetBoundFrame().GetParentSpace().TransformVector(vecPs); }

	const Point WorldToLocal(Point_arg posWs) const { return GetBoundFrame().GetLocatorWs().UntransformPoint(posWs); }
	const Point LocalToWorld(Point_arg posLs) const { return GetBoundFrame().GetLocatorWs().TransformPoint(posLs); }

	const Vector WorldToLocal(Vector_arg vecWs) const { return GetBoundFrame().GetLocatorWs().UntransformVector(vecWs); }
	const Vector LocalToWorld(Vector_arg vecLs) const { return GetBoundFrame().GetLocatorWs().TransformVector(vecLs); }

	const BoundFrame& GetBoundFrame() const { return *m_pBoundFrame; }
	const Locator& GetParentSpace() const { return GetBoundFrame().GetParentSpace(); }
	void SetBinding(const Binding& newBinding) { m_pBoundFrame->SetBinding(newBinding, m_originPs); }

	struct FindLedgeParams
	{
		FindLedgeParams()
			: m_point(kOrigin)
			, m_searchRadius(0.0f)
			, m_pLedge(nullptr)
			, m_nearestPoint(kOrigin)
			, m_dist(0.0f)
		{
		}

		Point m_point;
		float m_searchRadius;

		const NavLedge* m_pLedge;
		Point m_nearestPoint;
		float m_dist;
	};

	bool FindClosestLedgeLs(FindLedgeParams* pParams) const;
	bool FindClosestLedgePs(FindLedgeParams* pParams) const;

	const NavLedge* FindClosestLedgeLs(Point_arg posLs, float searchRadius) const;
	const NavLedge* FindClosestLedgePs(Point_arg posPs, float searchRadius) const;

	const NavLedge* GetLedge(U32F iLedge) const
	{
		const NavLedge* pRet = nullptr;

		if (iLedge < m_numLedges)
		{
			pRet = &m_pLedges[iLedge];
		}

		return pRet;
	}

	// convenient access to the EntityDB, if it exists
	template<typename T>
	const T GetTagData(StringId64 recordName, const T& def) const
	{
		if (!m_pEntityDB)
			return def;

		const EntityDB::Record* pRecord = m_pEntityDB->GetRecord(recordName);
		return pRecord ? pRecord->GetData<T>(def) : def;
	}

private:
	friend class NavLedgeGraphMgr;
	friend class NavPathNodeMgr;

	NavLedge& GetLedge(U32F iLedge) { NAV_ASSERT(iLedge < m_numLedges); return m_pLedges[iLedge]; }

	U32			m_signature;			// 4 bytes
	U32			m_versionId;			// 4 bytes
	BoundFrame*	m_pBoundFrame;			// 8 bytes

	Locator		m_originPs;				// 16 bytes
	StringId64	m_bindSpawnerNameId;	// 8 bytes
	StringId64	m_levelId;				// 8 bytes
	Point		m_bboxMinLs;
	Point		m_bboxMaxLs;

	const char*	m_strName;
	StringId64	m_nameId;

	union
	{
		GameData	m_gameData;
		U8			m_gameDataBuf[kGameDataBufferSize];
	};

	const EntityDB*		m_pEntityDB;

	NavLedge*			m_pLedges;
	U32					m_numLedges;
	NavLedgeGraphHandle	m_handle;
};

#endif // ENABLE_NAV_LEDGES

#endif // NAV_LEDGE_GRAPH_H
