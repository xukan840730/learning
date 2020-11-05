/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef NAV_LEDGE_GRAPH_UTIL_H
#define NAV_LEDGE_GRAPH_UTIL_H

#if ENABLE_NAV_LEDGES

#include "gamelib/gameplay/nav/nav-defines.h"
#include "gamelib/gameplay/nav/nav-ledge.h"

class NavLedgeGraph;

/// --------------------------------------------------------------------------------------------------------------- ///
struct FindNavLedgeGraphParams
{
	FindNavLedgeGraphParams()
		: m_pointWs(kOrigin)
		, m_searchRadius(0.0f)
		, m_bindSpawnerNameId(Nav::kMatchAllBindSpawnerNameId)
		, m_nearestPointWs(kOrigin)
		, m_pLedgeGraph(nullptr)
		, m_pNavLedge(nullptr)
	{}

	Point		m_pointWs;
	F32			m_searchRadius;
	StringId64	m_bindSpawnerNameId;

	Point					m_nearestPointWs;
	const NavLedgeGraph*	m_pLedgeGraph;
	const NavLedge*			m_pNavLedge;
};

void FindNavLedgeGraphFromArray(const NavLedgeGraphArray& ledgeGraphArray, FindNavLedgeGraphParams* pParams);

/// --------------------------------------------------------------------------------------------------------------- ///
// Use a depth-first search to find the closest ledge from a starting ledge
struct FindLedgeDfsParams
{
	FindLedgeDfsParams()
		: m_point(kOrigin)
		, m_searchRadius(0.0f)
		, m_pGraph(nullptr)
		, m_startLedgeId(UINT_MAX)
		, m_pLedge(nullptr)
		, m_nearestPoint(kOrigin)
		, m_dist(0.0f)
	{
	}

	Point m_point;
	float m_searchRadius;
	const NavLedgeGraph* m_pGraph;
	NavLedgeId m_startLedgeId;

	const NavLedge* m_pLedge;
	Point m_nearestPoint;
	float m_dist;
};

bool FindClosestLedgeDfsLs(FindLedgeDfsParams* pParams);

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavLedgeGraphDrawFilter
{
	NavLedgeGraphDrawFilter();

	bool m_drawNames;
	bool m_drawLedges;
	bool m_drawLedgeNormals;
	bool m_drawLedgeIds;
	bool m_drawLinkIds;
	bool m_drawLinkDetails;
	bool m_drawFeatureFlags;
};

extern NavLedgeGraphDrawFilter g_navLedgeGraphDrawFilter;

/// --------------------------------------------------------------------------------------------------------------- ///
void NavLedgeGraphDebugDraw(const NavLedgeGraph* pLedgeGraph, const NavLedgeGraphDrawFilter& filter);

#endif // ENABLE_NAV_LEDGES

#endif // NAV_LEDGE_GRAPH_UTIL_H
