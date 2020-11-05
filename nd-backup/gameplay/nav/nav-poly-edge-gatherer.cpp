/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nav/nav-poly-edge-gatherer.h"

#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-poly-ex.h"
#include "gamelib/gameplay/nav/nav-poly.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPolyEdgeGatherer::Init(Point_arg searchOriginLs,
							   float searchRadius,
							   float yThreshold,
							   const NavMesh::BaseProbeParams& probeParams,
							   U64 maxEdges,
							   U64 maxNavMeshDist)
{
	NavPolyExpansionSearch::Init(probeParams, maxEdges);

	m_searchOriginLs = searchOriginLs;
	m_searchRadius	 = searchRadius;
	m_yThreshold	 = yThreshold;
	m_maxNavMeshDist = maxNavMeshDist;

	m_probeParams = probeParams;

	m_edges.Init(maxEdges, FILE_LINE_FUNC);
	m_navMeshDistTable.Init(maxEdges, FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPolyEdgeGatherer::Execute(const NavPoly* pStartPoly)
{
	const NavPolyEx* pStartPolyEx = nullptr;
	if (m_probeParams.m_dynamicProbe && pStartPoly)
	{
		pStartPolyEx = pStartPoly->FindContainingPolyExLs(m_searchOriginLs);
	}

	const NavMesh* pStartMesh = pStartPoly ? pStartPoly->GetNavMesh() : nullptr;

	m_navMeshDistTable.Set(pStartMesh, 0);

	NavPolyExpansionSearch::Execute(pStartMesh, pStartPoly, pStartPolyEx);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavPolyEdgeGatherer::VisitEdge(const NavMesh* pMesh,
									const NavPoly* pPoly,
									const NavPolyEx* pPolyEx,
									I32F iEdge,
									NavMesh::BoundaryFlags boundaryFlags)
{
	if (m_edges.IsFull())
		return false;	

	if (!pPoly || !pMesh)
		return false;

	const Point v0 = pPolyEx ? pPolyEx->GetVertex(iEdge) : pPoly->GetVertex(iEdge);
	const Point v1 = pPolyEx ? pPolyEx->GetNextVertex(iEdge) : pPoly->GetNextVertex(iEdge);

	Point closestPt = v0;
	const float edgeDist = DistPointSegmentXz(m_searchOriginLs, v0, v1, &closestPt);

	if (edgeDist > m_searchRadius)
		return false;

	if (Abs(closestPt.Y() - m_searchOriginLs.Y()) >= m_yThreshold)
		return false;

	NavMeshDistTable::ConstIterator itr = m_navMeshDistTable.Find(pMesh);

	const U64 navMeshDist = (itr != m_navMeshDistTable.End()) ? itr->m_data : 0;

	if (navMeshDist > m_maxNavMeshDist)
	{
		return false;
	}

	// note we always store every poly ex edge, since gaps are the combined blocker bits, we figure out who we care about later on
	const bool shouldStore = boundaryFlags != NavMesh::kBoundaryNone;

	if (shouldStore)
	{
		NavPolyEdge newEdge;
		newEdge.m_pSourceMesh = pMesh;
		newEdge.m_sourceId	  = pPolyEx ? pPolyEx->GetNavManagerId() : pPoly->GetNavManagerId();
		newEdge.m_iEdge		  = iEdge;
		newEdge.m_v0 = v0;
		newEdge.m_v1 = v1;

		TryAddEdge(newEdge);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPolyEdgeGatherer::OnNewMeshEntered(const NavMesh* pSourceMesh, const NavMesh* pDestMesh)
{
	 NavMeshDistTable::ConstIterator itr = m_navMeshDistTable.Find(pSourceMesh);
	 if (itr != m_navMeshDistTable.End())
	 {
		 const U64 sourceCost = itr->m_data;
		 const U64 newDestCost = sourceCost + 1;

		 U64 prevDestCost = -1;

		 NavMeshDistTable::ConstIterator ditr = m_navMeshDistTable.Find(pDestMesh);
		 if (ditr != m_navMeshDistTable.End())
		 {
			 prevDestCost = ditr->m_data;
		 }

		 if (newDestCost < prevDestCost)
		 {
			 m_navMeshDistTable.Set(pDestMesh, newDestCost);
		 }
	 }
	 else
	 {
		 m_navMeshDistTable.Set(pSourceMesh, 0);
		 m_navMeshDistTable.Set(pDestMesh, 1);
	 }
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavPolyEdgeGatherer::TryAddEdge(const NavPolyEdge& newEdge)
{
	if (m_edges.IsFull())
		return;

	for (const NavPolyEdge& edge : m_edges)
	{
		if (edge.Equals(newEdge))
		{
			// already added
			return;
		}
	}

	m_edges.PushBack(newEdge);	
}
