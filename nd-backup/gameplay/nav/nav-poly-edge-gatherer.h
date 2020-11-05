/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/hashtable.h"
#include "corelib/containers/list-array.h"

#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-mesh-probe.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NavMesh;
class NavPoly;
class NavPolyEx;

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavPolyEdge
{
	NavPolyEdge() : m_iEdge(-1), m_v0(kOrigin), m_v1(kOrigin), m_pSourceMesh(nullptr) { m_sourceId.Invalidate(); }

	bool Equals(const NavPolyEdge& rhs) const { return (m_sourceId == rhs.m_sourceId) && (m_iEdge == rhs.m_iEdge); }

	NavManagerId m_sourceId;
	U64 m_iEdge;
	Point m_v0;
	Point m_v1;
	const NavMesh* m_pSourceMesh;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NavPolyEdgeGatherer : public NavPolyExpansionSearch
{
public:
	typedef HashTable<NavMeshHandle, U64> NavMeshDistTable;

	void Init(Point_arg searchOriginLs,
			  float searchRadius,
			  float yThreshold,
			  const NavMesh::BaseProbeParams& probeParams,
			  U64 maxEdges,
			  U64 maxNavMeshDist);

	void Execute(const NavPoly* pStartPoly);

	const ListArray<NavPolyEdge>& GetResults() const { return m_edges; }
	ListArray<NavPolyEdge>& GetResults() { return m_edges; }
	const NavMesh* GetStartNavMesh() const { return m_pStartMesh; }

protected:
	virtual bool VisitEdge(const NavMesh* pMesh,
						   const NavPoly* pPoly,
						   const NavPolyEx* pPolyEx,
						   I32F iEdge,
						   NavMesh::BoundaryFlags boundaryFlags) override;
	virtual void Finalize() override {}
	void TryAddEdge(const NavPolyEdge& newEdge);
	void OnNewMeshEntered(const NavMesh* pSourceMesh, const NavMesh* pDestMesh) override;

	Point m_searchOriginLs;
	float m_searchRadius;
	float m_yThreshold;
	U64 m_maxNavMeshDist;

	NavMesh::BaseProbeParams m_probeParams;

	ListArray<NavPolyEdge> m_edges;
	NavMeshDistTable m_navMeshDistTable;
};
