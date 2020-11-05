/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/nav-poly.h"

class NavMesh;

/// --------------------------------------------------------------------------------------------------------------- ///
class NavPolyEx
{
public:
	static const U32 kInvalidPolyExId = 0;

	NavPolyEx();

	U32F GetId() const { return m_id; }
	U32F GetVertexCount() const { return m_numVerts; }
	NavManagerId GetNavManagerId() const
	{
		NavManagerId ret = m_hOrgPoly.GetManagerId();
		ret.m_iPolyEx	 = m_id;
		return ret;
	}

	NavManagerId GetAdjacentPolyId(U32F iEdge) const { return m_adjPolys[iEdge % NavPoly::kMaxVertexCount]; }

	void DebugDrawEdges(const Color& col,
						const Color& colBoundary,
						const Color& colBlocked,
						float yOffset,
						float lineWidth	 = 3.0f,
						DebugPrimTime tt = kPrimDuration1FrameAuto,
						const NavBlockerBits* pObeyedBlockers = nullptr) const;

	void DebugDraw(const Color& col, float yOffset = 0.0f, DebugPrimTime tt = kPrimDuration1FrameAuto) const;

	Point GetVertex(U32F iEdge) const { return m_vertsLs[iEdge % m_numVerts]; }
	Point GetNextVertex(U32F iEdge) const { return m_vertsLs[(iEdge + 1) % m_numVerts]; }

	const NavMesh* GetNavMesh() const;
	const NavPoly* GetBasePoly() const { return m_hOrgPoly.ToNavPoly(); }

	NavPolyEx* GetNextPolyInList() const { return m_pNext; }

	Point GetCentroid() const;

	bool PolyContainsPointLs(Point_arg ptLs, Vec4* pDotsOut = nullptr, float epsilon = NDI_FLT_EPSILON) const;

	const Scalar FindNearestPointLs(Point* pOutLs, Point_arg inPtLs) const;
	const Scalar FindNearestPointPs(Point* pOutPs, Point_arg inPtPs) const;

	// NB: Does *not* give you a point to be contained by the poly, just one projected onto its extrapolated surface!
	void ProjectPointOntoSurfaceLs(Point* pOutLs, Vector* pNormalLs, Point_arg inPtLs) const;
	void ProjectPointOntoSurfacePs(Point* pOutPs, Vector* pNormalPs, Point_arg inPtPs) const;

	bool IsBlockedBy(const NavBlockerBits& blockers) const;

	bool IsSourceLink() const { return m_sourceFlags.m_link; }
	bool IsSourcePreLink() const { return m_sourceFlags.m_preLink; }
	bool IsSourceStealth() const { return m_sourceFlags.m_stealth; }

	bool IsSourceBlocked(const Nav::StaticBlockageMask blockageType = Nav::kStaticBlockageMaskAll) const
	{
		return (m_sourceBlockage & blockageType) != 0;
	}

	Nav::StaticBlockageMask GetSourceBlockageMask() const { return m_sourceBlockage; }

	U32F GetPathNodeId() const { return m_pathNodeId; }

	float SignedDistPointPolyXzSqr(Point_arg ptLs) const;
	float SignedDistPointPolyXz(Point_arg ptLs) const;
	const float* GetVertexArray() const { return (float*)m_vertsLs; }

	NavPolyEx* m_pNext;
	NavPolyHandle m_hOrgPoly;

	U32 m_id;
	U32 m_numVerts;

	NavPathNode::NodeId m_pathNodeId;
	NavPathNode::NodeId m_ownerPathNodeId;

	Point m_vertsLs[NavPoly::kMaxVertexCount];
	NavManagerId m_adjPolys[NavPoly::kMaxVertexCount];

	NavBlockerBits m_blockerBits;
	Nav::StaticBlockageMask m_sourceBlockage;
	NavPoly::Flags m_sourceFlags;
};
