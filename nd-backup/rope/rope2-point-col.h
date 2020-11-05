/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/ndphys/rope/rope2-col.h"
#include "gamelib/ndphys/rope/rope2-collider.h"
#include "gamelib/ndphys/rope/rope2.h"

class RopeEdgeSegments;

class Rope2PointCol
{
public:
	enum { 
		kMaxNumTris = 64,
		kMaxNumPlanes = 16
	};

	struct Tri
	{
		I16 m_triIndex;			// face index in case of convex face
		I8 m_planeIndex;
		I8 m_isConvexFace;
	};

	struct Plane
	{
		Vec4 m_plane;
		RopeColliderHandle m_hCollider;
		F32 m_dist;
		F32 m_normFactor;
		U8 m_startIndex;
		bool m_isConvexFace;
	};

	enum Relationship {
		Behind = 0,
		InFront,
		Concave,
		Convex,
		ConvexEdge
	};

	U32 m_numTris;
	U32 m_numPlanes;
	Tri m_tris[kMaxNumTris];
	Plane m_planes[kMaxNumPlanes];

	Rope2PointCol();

	void AddTriangle(I16 triIndex, Vec4_arg plane, Point_arg pt0, Point_arg pt1, Point_arg pt2, const RopeColliderHandle& hCollider);
	void AddConvexFace(Vec4_arg plane, I16 faceId, const RopeColliderHandle& hCollider);
	void GenerateConstraints(Rope2* pRope, U32 iBead);
	bool ClipSegmentsByPlaneTris(U32 iPlane, RopeEdgeSegments& segs, const RopeColCache& colCache);

	static bool ArePlanesSimilar(const Vec4& pl0, const Vec4& pl1, const Point& refPos);
};

