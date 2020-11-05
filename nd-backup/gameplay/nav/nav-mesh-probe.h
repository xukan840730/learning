/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/nav/nav-mesh.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NavManagerId;
class NavPoly;
class NavPolyEx;
struct NavPolyOpenListEntry;

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavPolyListEntry
{
	const NavMesh* m_pMesh;
	const NavPoly* m_pPoly;
	const NavPolyEx* m_pPolyEx;

	bool IsSameAs(const NavPolyListEntry& rhs) const;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NavPolyExpansionSearch
{
public:
	void Execute(const NavMesh* pStartMesh, const NavPoly* pStartPoly, const NavPolyEx* pStartPolyEx);
	void Execute(const NavPolyListEntry* pList, U32 listSize);

	size_t GatherClosedList(NavManagerId* pListOut, size_t maxSizeOut) const;
	size_t GatherClosedList(const NavPoly** ppPolysOut, size_t maxSizeOut) const;

protected:
	void Init(const NavMesh::BaseProbeParams& params, U32F maxListSize);
	virtual bool VisitEdge(const NavMesh* pMesh,
						   const NavPoly* pPoly,
						   const NavPolyEx* pPolyEx,
						   I32F iEdge,
						   NavMesh::BoundaryFlags boundaryFlags) = 0;
	virtual void Finalize() = 0;
	virtual void OnNewMeshEntered(const NavMesh* pSourceMesh, const NavMesh* pDestMesh) {}

	const NavMesh* m_pStartMesh;
	const NavPoly* m_pStartPoly;
	const NavPolyEx* m_pStartPolyEx;

	NavPolyOpenListEntry* m_pOpenList;
	NavPolyListEntry* m_pClosedList;

	NavMesh::BaseProbeParams m_baseProbeParams;

	U32F m_maxListSize;
	U32F m_closedListSize;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NavMeshCapsuleProbe : public NavPolyExpansionSearch
{
public:
	void Init(NavMesh::ProbeParams* pParams, const NavMesh* pInitialMesh);

protected:
	virtual bool VisitEdge(const NavMesh* pMesh,
						   const NavPoly* pPoly,
						   const NavPolyEx* pPolyEx,
						   I32F iEdge,
						   NavMesh::BoundaryFlags boundaryFlags) override;
	virtual void Finalize() override;

private:
	void ChangeToNewNavMeshSpace(const NavMesh* pNewMesh);

	NavMesh::ProbeParams* m_pProbeParams;
	const NavMesh* m_pCurMesh;
	float m_closestProbeStopTT;
	bool m_isStadium;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NavMeshClearanceProbe : public NavPolyExpansionSearch
{
public:
	void Init(Point_arg posLs, float radius, const NavMesh::BaseProbeParams& probeParams);

	bool DidHitEdge() const { return m_hitEdge; }
	Point GetImpactPosLs() const { return m_impactPt; }

	static CONST_EXPR float kNudgeEpsilon = 0.01f;

protected:
	virtual bool VisitEdge(const NavMesh* pMesh,
						   const NavPoly* pPoly,
						   const NavPolyEx* pPolyEx,
						   I32F iEdge,
						   NavMesh::BoundaryFlags boundaryFlags) override;
	virtual void Finalize() override {}

	Point m_posLs;
	float m_radius;

	bool m_isStadium;
	bool m_hitEdge;
	float m_bestDist;
	Point m_impactPt;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NavMeshStadiumDepenetrator : public NavMeshClearanceProbe
{
public:
	virtual void Init(Point_arg posLs,
					  const NavMesh* pInitialMesh,
					  const NavMesh::FindPointParams& probeParams);

	virtual Point GetResolvedPosLs() const { return m_resolvedPoint; };

	const NavPoly* GetResolvedPoly() const { return m_pResolvedPoly; }
	const NavPolyEx* GetResolvedPolyEx() const { return m_pResolvedPolyEx; }

private:
	struct Edge
	{
		Point m_v0;
		Point m_v1;
		Point m_offset0;
		Point m_offset1;
		Vector m_normal;

		const NavMesh* m_pMesh;

		I32 m_neighborParallel0;
		I32 m_neighborParallel1;
		I32 m_neighborInterior0;
		I32 m_neighborInterior1;
		I32 m_iCorner0;
		I32 m_iCorner1;

		F32 m_distToPos;

		bool m_fromLink;
		bool m_zeroLength;

		static I32 Compare(const Edge& a, const Edge& b)
		{
			if (a.m_distToPos < b.m_distToPos)
				return -1.0f;
			if (a.m_distToPos > b.m_distToPos)
				return 1.0f;
			return 0.0f;
		}
	};

	enum FeatureType : U8
	{
		kEdgeFeature,
		kCornerFeature,
		kCornerBeginEdgeFeature,
		kCornerArc0Feature,
		kCornerBetweenEdgeFeature,
		kCornerArc1Feature,
		kCornerEndEdgeFeature,
	};

	struct FeatureIndex
	{
		static FeatureIndex FromEdge(I16 i) { return { i, kEdgeFeature }; }
		static FeatureIndex FromCorner(I16 i) { return { i, kCornerFeature }; }

		bool IsValid() const { return m_index >= 0; }

		FeatureIndex As(FeatureType type) const { FeatureIndex ft = *this; ft.m_type = type; return ft; }

		bool IsEdge() const { return IsValid() && m_type == kEdgeFeature; }
		bool IsArc() const { return IsValid() && (m_type == kCornerArc0Feature || m_type == kCornerArc1Feature); }

		bool operator==(FeatureIndex other) const { return m_index == other.m_index && m_type == other.m_type; }

		I16 m_index = -1;
		FeatureType m_type = kEdgeFeature;
	};

	static FeatureIndex kInvalidFeatureIndex;

	struct Intersection
	{
		Point m_intPos;
		FeatureIndex m_feature0;
		FeatureIndex m_feature1;
		F32 m_tt0;
		F32 m_tt1;
	};

	struct LocalIntersection
	{
		F32 m_tt;
		bool m_forward;

		static I32 Compare(const LocalIntersection& a, const LocalIntersection& b)
		{
			if (a.m_tt < b.m_tt)
				return -1.0f;
			if (a.m_tt > b.m_tt)
				return 1.0f;
			return 0.0f;
		}
	};

	struct ExtCornerArc
	{
		Point m_origin;
		Point m_begin;
		Point m_end;
	};

	struct ExtCorner
	{
		Point m_vert;

		Point m_begin;
		ExtCornerArc m_arc0;
		ExtCornerArc m_arc1;
		Point m_end;

		I32 m_iEdge0;
		I32 m_iEdge1;

		U32 m_edge0Fake				: 1;
		U32 m_edge1Fake				: 1;
		U32 m_hasBeginEdge			: 1;
		U32 m_hasArc0				: 1;
		U32 m_hasEdgeBetweenArcs	: 1;
		U32 m_hasArc1				: 1;
		U32 m_hasEndEdge			: 1;
	};

	virtual bool VisitEdge(const NavMesh* pMesh,
						   const NavPoly* pPoly,
						   const NavPolyEx* pPolyEx,
						   I32F iEdge,
						   NavMesh::BoundaryFlags boundaryFlags) override;
	virtual void Finalize() override;

	void FinalizePoint(Point_arg pos);

	void FindBestPositionOnEdge(bool debugDraw,
								F32 searchRadius,
								const ExtCorner* pCorners,
								U32 numCorners,
								FeatureIndex featureIdx,
								Intersection* pIntersections,
								U32 numIntersections,
								LocalIntersection* pLocalIntersections,
								Point& bestPos,
								F32& bestDist) const;
	void FindBestPositionOnArc(bool debugDraw,
							   F32 searchRadius,
							   const ExtCorner* pCorners,
							   U32 numCorners,
							   FeatureIndex featureIdx,
							   Intersection* pIntersections,
							   U32 numIntersections,
							   LocalIntersection* pLocalIntersections,
							   Point& bestPos,
							   F32& bestDist) const;

	Vector GetArcNormal(const ExtCornerArc& arc) const;
	Vector GetArcNormalAndHalfAngleRad(const ExtCornerArc& arc, F32& halfAngleRad) const;

	Aabb GetCornerBound(const ExtCorner& corner) const;

	U32 GenerateCorners(ExtCorner* pCornersOut, U32 maxCornersOut);

	U32 GenerateIntersections(Intersection* pIntersectionsOut,
							  U32 maxIntersections,
							  const ExtCorner* pCorners,
							  U32 numCorners) const;

	void StretchInteriorCornerEdges(I32 iEdge0, I32 iEdge1);
	void StretchInteriorCornersAgainstNeighboringCorners(const ExtCorner* pCorners, I32 iEdge0, I32 iEdge1);

	void GenerateEdgeEdgeIntersections(F32 searchRadius,
									   const ExtCorner* pCorners,
									   U32 numCorners,
									   Intersection* pIntersectionsOut,
									   U32 maxIntersections,
									   U32 &numIntersections,
									   const FeatureIndex edgeIdx0,
									   const Edge& edge0,
									   const FeatureIndex edgeIdx1,
									   const Edge& edge1) const;

	void GenerateCornerEdgeIntersections(F32 searchRadius,
										 const ExtCorner* pCorners,
										 U32 numCorners,
										 Intersection* pIntersectionsOut,
										 U32 maxIntersections,
										 U32 &numIntersections,
										 const FeatureIndex cornerIdx,
										 const ExtCorner& corner,
										 const FeatureIndex edgeIdx,
										 const Edge& edge) const;

	void GenerateCornerCornerIntersections(F32 searchRadius,
										   const ExtCorner* pCorners,
										   U32 numCorners,
										   Intersection* pIntersectionsOut,
										   U32 maxIntersections,
										   U32 &numIntersections,
										   const FeatureIndex cornerIdx0,
										   const ExtCorner& corner0,
										   const FeatureIndex cornerIdx1,
										   const ExtCorner& corner1) const;

	const ExtCornerArc& GetArc(FeatureIndex index, const ExtCorner* pCorners) const;
	Vector GetEdgeNormal(FeatureIndex index, const ExtCorner* pCorners) const;
	Segment GetEdgeSegment(FeatureIndex index, const ExtCorner* pCorners) const;

	struct IntersectEdgeCornerResults
	{
		FeatureIndex m_fi[2];
		F32 m_t0[2] = { kLargeFloat, -kLargeFloat };
		F32 m_t1[2] = { kLargeFloat, -kLargeFloat };
		U32 m_num = 0;
	};

	IntersectEdgeCornerResults IntersectEdgeCorner(const FeatureIndex cornerIdx,
												   const ExtCorner& corner,
												   const Segment& edge) const;

	bool IntersectEdgeAsPlaneCorner(const ExtCorner& corner, const Segment& edge,
									F32& t0, F32& t1) const;

	bool IntersectArcEdge(const ExtCornerArc& cornerArc, const Segment& edge,
						  F32& t00, F32& t01, F32& t10, F32& t11) const;

	struct IntersectCornerCornerResults
	{
		enum
		{
			kMaxIntersections = 8,
		};

		Point m_intersection[kMaxIntersections];
		FeatureIndex m_fi0[kMaxIntersections];
		FeatureIndex m_fi1[kMaxIntersections];
		F32 m_t0[kMaxIntersections];
		F32 m_t1[kMaxIntersections];
		U32 m_num = 0;
	};

	IntersectCornerCornerResults IntersectCornerCorner(const FeatureIndex cornerIdx0,
													   const ExtCorner& corner0,
													   const FeatureIndex cornerIdx1,
													   const ExtCorner& corner1) const;

	struct IntersectArcCornerResults
	{
		enum
		{
			kMaxIntersections = 4,
		};

		Point m_intersection[kMaxIntersections];
		FeatureIndex m_fi[kMaxIntersections];
		F32 m_t0[kMaxIntersections];
		F32 m_t1[kMaxIntersections];
		U32 m_num = 0;
	};

	IntersectArcCornerResults IntersectArcCorner(const FeatureIndex cornerIdx,
												 const ExtCorner& corner,
												 const ExtCornerArc& arc) const;

	bool IntersectArcArc(const ExtCornerArc& arc0, const ExtCornerArc& arc1,
						 F32& t00, F32& t01, F32& t10, F32& t11,
						 Point& i0, Point& i1) const;

	void AddItersection(const Intersection& i,
						Intersection* pIntersections,
						U32 maxNumIntersections,
						U32& numIntersections) const;

	U32 GatherIntersectionsForEdge(FeatureIndex edgeFeature,
								   Vector_arg edgeVec,
								   const Intersection* pIntersections,
								   U32 numIntersections,
								   const ExtCorner* pCorners,
								   U32 numCorners,
								   LocalIntersection* pIntersectionsOut) const;
	U32 GatherIntersectionsForArc(FeatureIndex cornerFeature,
								  const Intersection* pIntersections,
								  U32 numIntersections,
								  const ExtCorner* pCorners,
								  U32 numCorners,
								  LocalIntersection* pIntersectionsOut) const;

	NavPolyListEntry GetContainingPoly(Point_arg pos) const;

	bool IsPointClear(Point_arg pos,
					  FeatureIndex ignore0,
					  FeatureIndex ignore1,
					  float maxDist,
					  Vector_arg testNorm,
					  const ExtCorner* pCorners,
					  U32F numCorners) const;

	void CreateCorner(I32 cornerIdx,
					  ExtCorner& cornerOut,
					  Point_arg vert,
					  Vector_arg normal0,
					  Vector_arg normal1,
					  I32 iEdge0,
					  I32 iEdge1);

	bool AddStadiumCornerEdge(Vector_arg clipNormal, F32 clipDist, Point& begin, Point& end, bool &hasEdge) const;
	bool AddStadiumCornerArc(Vector_arg clipNormal, F32 clipDist, F32 radius, Point_arg origin, Point& begin,
							 Point& end, bool &hasArc) const;
	bool AddStadiumCornerConnectionToEdge(Point_arg connectFrom, const Segment& connectTo, Point& end) const;

	// Finds the planes at the beginning and end of a corner pointing into the corner
	bool FindCornerBeginPlane(const ExtCorner& corner, Vector& n, F32& d) const;
	bool FindCornerEndPlane(const ExtCorner& corner, Vector& n, F32& d) const;

	bool IsCornerDirectlyConnected(const ExtCorner& corner, const Edge& edge) const;

	I32 FindFirstConnectedEdge(I32 start) const;
	I32 FindMatchingConnectedEdge(I32 start, I32 find0, I32 find1) const;

	void ClipCorner(Vector n, F32 d, ExtCorner& corner) const;
	void ClipMidEdge(Vector_arg mid, F32 midDist, Vector n[2], F32 d[2], Point& a, Point& b) const;
	void ClipBeginEdge(Vector n[2], F32 d[2], Point& a, Point& b) const;
	void ClipEndEdge(Vector n[2], F32 d[2], Point& a, Point& b) const;
	void ClipCornerEdge(Vector_arg n, F32 d, Point& begin, Point& end) const;
	void ClipEdgesAgainstCorner(U32 cornerIdx, ExtCorner* pCorners, U32 numCorners);
	void ClipCornerAgainstCorner(ExtCorner& corner0, ExtCorner& corner1) const;

	F32 DistPointStadiumCornerArcXz(Point_arg point, const ExtCornerArc& arc, Point& pClosestPoint) const;
	F32 DistPointStadiumCornerXz(Point_arg point, const ExtCorner& corner, Point& pClosestPoint) const;

	bool FeatureHasEdge(FeatureIndex idx, I32F iEdge, const ExtCorner* pCorners, U32F numCorners) const
	{
		if (!idx.IsValid())
		{
			return false;
		}

		if (idx.IsEdge())
		{
			return idx.m_index == iEdge;
		}
		else if (idx.m_index < numCorners)
		{
			const ExtCorner& c = pCorners[idx.m_index];
			return (c.m_iEdge0 == iEdge) || (c.m_iEdge1 == iEdge);
		}

		return false;
	}

	void DebugDrawEdge(const Edge& edge, float vo, Color c, const char* label = nullptr) const;
	void DebugDrawCorner(const ExtCorner& corner, float vo, Color c, const char* label = nullptr) const;
	void DebugDrawEdgeRange(Point_arg v0, Point_arg v1) const;
	void DebugDrawCornerRange(Point_arg origin, Vector_arg normal, float startAngleRad, float endAngleRad) const;

	Point m_resolvedPoint;
	Vector m_posLsOffset;

	const NavPoly* m_pResolvedPoly;
	const NavPolyEx* m_pResolvedPolyEx;
	const NavMesh* m_pMesh;

	Edge* m_pEdges;
	U32F m_maxEdges;
	U32F m_numEdges;

	Segment m_stadiumSeg;

	float m_maxDepenRadius;
	float m_depenRadius;
	float m_invDepenRadius;
	float m_closestEdgeDist;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NavMeshDepenetrator2 : public NavMeshClearanceProbe
{
public:
	virtual void Init(Point_arg posLs,
					  const NavMesh* pInitialMesh,
					  const NavMesh::FindPointParams& probeParams);

	virtual Point GetResolvedPosLs() const { return m_resolvedPoint; };

	const NavPoly* GetResolvedPoly() const { return m_pResolvedPoly; }
	const NavPolyEx* GetResolvedPolyEx() const { return m_pResolvedPolyEx; }

private:
	struct Edge
	{
		Point m_v0;
		Point m_v1;
		Point m_offset0;
		Point m_offset1;
		Vector m_normal;
		float m_distToPos;
		I16 m_iCorner0;
		I16 m_iCorner1;

		static I32 Compare(const Edge& a, const Edge& b)
		{
			if (a.m_distToPos < b.m_distToPos)
				return -1.0f;
			if (a.m_distToPos > b.m_distToPos)
				return 1.0f;
			return 0.0f;
		}
	};

	struct FeatureIndex
	{
		static FeatureIndex FromEdge(I16 i) { return { i, false }; }
		static FeatureIndex FromCorner(I16 i) { return { i, true }; }

		bool IsValid() const { return m_index >= 0; }

		void AsEdge(I32F i) { m_index = i; m_isCorner = false; }
		void AsCorner(I32F i) { m_index = i; m_isCorner = true; }

		bool IsEdge(I32F i) const { return m_index == i && !m_isCorner; }
		bool IsCorner(I32F i) const { return m_index == i && m_isCorner; }

		I16 m_index = -1;
		bool m_isCorner = false;
	};

	static FeatureIndex kInvalidFeatureIndex;

	struct Intersection
	{
		Point m_intPos;
		FeatureIndex m_feature0;
		FeatureIndex m_feature1;
		F32 m_tt0;
		F32 m_tt1;
	};

	struct LocalIntersection
	{
		float m_tt;
		bool m_forward;

		static I32 Compare(const LocalIntersection& a, const LocalIntersection& b)
		{
			return (a.m_tt > b.m_tt) - (a.m_tt < b.m_tt);
		}
	};

	struct ExtCorner
	{
		Point m_vert;
		Vector m_normal;
		Vector m_dir0;
		Vector m_dir1;

		I32F m_iEdge0;
		I32F m_iEdge1;
		F32 m_cosHalfAngle;
		F32 m_halfAngleRad;
	};

	virtual bool VisitEdge(const NavMesh* pMesh,
						   const NavPoly* pPoly,
						   const NavPolyEx* pPolyEx,
						   I32F iEdge,
						   NavMesh::BoundaryFlags boundaryFlags) override;
	virtual void Finalize() override;

	void FinalizePoint(Point_arg pos);

	U32F GenerateCorners(ExtCorner* pCornersOut, U32F maxCornersOut) const;

	U32F GenerateIntersections(Intersection* pIntersectionsOut,
							   U32F maxIntersections,
							   const ExtCorner* pCorners,
							   U32F numCorners) const;

	U32F GatherIntersectionsForEdge(I32F iEdge,
									const Intersection* pIntersections,
									U32F numIntersections,
									const ExtCorner* pCorners,
									U32F numCorners,
									LocalIntersection* pIntersectionsOut) const;

	U32F GatherIntersectionsForCorner(U32F iCorner,
									  const Intersection* pIntersections,
									  U32F numIntersections,
									  const ExtCorner* pCorners,
									  U32F numCorners,
									  LocalIntersection* pIntersectionsOut) const;

	NavPolyListEntry GetContainingPoly(Point_arg pos) const;

	bool IsPointClear(Point_arg pos,
					  FeatureIndex ignore0,
					  FeatureIndex ignore1,
					  float maxDist,
					  Vector_arg testNorm,
					  const ExtCorner* pCorners,
					  U32F numCorners) const;

	void CreateCorner(ExtCorner& cornerOut,
					  Point_arg vert,
					  Vector_arg normal0,
					  Vector_arg normal1,
					  I32F iEdge0,
					  I32F iEdge1) const;

	bool FeatureHasEdge(FeatureIndex idx, I32F iEdge, const ExtCorner* pCorners, U32F numCorners) const
	{
		if (!idx.IsValid())
			return false;

		if (!idx.m_isCorner)
		{
			return idx.m_index == iEdge;
		}
		else if (idx.m_index < numCorners)
		{
			const ExtCorner& c = pCorners[idx.m_index];
			return (c.m_iEdge0 == iEdge) || (c.m_iEdge1 == iEdge);
		}
		return false;
	}

	bool AreEdgesConnected(U32F iEdge0, U32F iEdge1, const ExtCorner* pCorners, U32F numCorners) const;

	void DebugDrawEdge(const Edge& edge, float vo, Color c, const char* label = nullptr) const;
	void DebugDrawCorner(const ExtCorner& corner, float vo, Color c, const char* label = nullptr) const;
	void DebugDrawEdgeRange(const Edge& edge, Point_arg v0, Point_arg v1) const;
	void DebugDrawCornerRange(const ExtCorner& corner, float startAngleRad, float endAngleRad) const;

	Point m_resolvedPoint;

	const NavPoly* m_pResolvedPoly;
	const NavPolyEx* m_pResolvedPolyEx;
	const NavMesh* m_pMesh;

	Edge* m_pEdges;
	U32F m_maxEdges;
	U32F m_numEdges;

	float m_depenRadius;
	float m_closestEdgeDist;
};
