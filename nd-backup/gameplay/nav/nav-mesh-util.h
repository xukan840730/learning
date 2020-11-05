/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/gameplay/nav/find-reachable-polys.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-defines.h"
#include "gamelib/gameplay/nav/nav-mesh.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ActionPack;
class NavMesh;
class NavPoly;
class NavPolyEx;
class NavPolyHandle;

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavMeshDrawFilter
{
	NavMeshDrawFilter();

	bool m_drawNames = false;
	bool m_drawPolys = false;
	bool m_drawPolysNoTrans = false;
	bool m_drawExPolys = false;
	bool m_explodeExPolys = false;
	bool m_drawStealthPolys = false;
	bool m_drawStealthHeights = false;
	bool m_drawWadePolys = false;
	bool m_drawSearchedRatio = false;
	bool m_drawPolyIds = false;
	bool m_drawEdgeIds = false;
	bool m_drawBoundingBox = false;
	bool m_drawGrid = false;
	bool m_drawAreaIds = false;
	bool m_drawFilterId = false;
	bool m_drawLinkIds = false;
	bool m_drawFlags = false;
	bool m_drawConnectivity = false;
	bool m_drawHeightMaps = false;
	bool m_drawStaticHeightMaps = false;
	bool m_drawDynamicHeightMaps = false;
	bool m_drawGaps = false;
	bool m_drawExGaps = false;
	bool m_onlyDrawEnabledGaps = false;
	float m_drawGapMinDist = -1.0f;
	float m_drawGapMaxDist = -1.0f;
	bool m_suppressDrawMeshErrors = false;
	bool m_drawEntityDB = false;

	bool m_drawApDetail = false;
	bool m_drawApFeatureDetail = false;
	bool m_drawOneShotPerPlaythroughCaps = false;
	bool m_drawTraversalApConnectivity = false;
	bool m_drawTraversalApAnimRange = false;
	bool m_displayNavMeshStats = false;
	bool m_drawNavMeshSearchGraph = false;
	float m_apDrawDistance = 0.0f;
	bool m_displayApManager = false;
	bool m_drawApRegFailure = true;
	bool m_drawApRegDistribution = false;
	bool m_drawHalfLoadedTapRegFailure = false;
	U32 m_apDrawMask = 0;
	U32 m_apRegFailDrawMask;

	bool m_drawAllDynamicNavBlockers = false;
	bool m_drawAllStaticNavBlockers = false;
	bool m_drawNavBlockerNames = false;
	bool m_drawNavBlockerSource = false;
	bool m_onlyDrawActiveNavBlockers = false;
	bool m_onlyDrawBlockersForSelectedMeshes = false;
	bool m_drawSelectedObjectBlockers = false;
	bool m_printNavBlockerStats = false;

	bool m_drawSourceShapeColors = false;
};

extern NavMeshDrawFilter g_navMeshDrawFilter;

/// --------------------------------------------------------------------------------------------------------------- ///
struct FindBestNavMeshParams
{
	// inputs
	Point						m_pointWs = kOrigin;
	F32							m_cullDist = 0.5f;
	F32							m_yThreshold = 2.0f;
	F32							m_yCeiling = NDI_FLT_MAX;
	StringId64					m_bindSpawnerNameId = Nav::kMatchAllBindSpawnerNameId;
	StringId64					m_requiredLevelId = INVALID_STRING_ID_64;
	Nav::StaticBlockageMask		m_obeyedStaticBlockers = Nav::kStaticBlockageMaskAll;
	bool						m_swimMeshAllowed = true;

	// outputs
	Point			m_nearestPointWs = kInvalidPoint;
	const NavPoly*	m_pNavPoly = nullptr;
	const NavMesh*	m_pNavMesh = nullptr;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct FindReachableActionPacksParams : Nav::FindReachablePolysParams
{
	// inputs
	U32 m_actionPackTypeMask = 0xffffffff;
	F32	m_actionPackEntryDistance = 0.0f;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void FindNavMeshFromArray(const NavMeshArray& navMeshHandles, FindBestNavMeshParams* pParams);
void NavMeshDebugDraw(const NavMesh* pNavMesh, const NavMeshDrawFilter& filter);
void DrawWireframeBox(const Transform& mat, Point_arg boxMin, Point_arg boxMax, const Color& col);

Scalar ComputeNavMeshAreaXz(const NavMesh& mesh);

Point PickRandomNavPolyPointWs(const NavPoly& poly);

/// --------------------------------------------------------------------------------------------------------------- ///
U32F FindReachableActionPacks(ActionPack** ppOutList,
							  U32F maxActionPackCount,
							  const FindReachableActionPacksParams& params);

U32F FindReachableActionPacksFromPolys(ActionPack** ppOutList,
									   U32F maxActionPackCount,
									   NavPolyHandle* polyList,
									   U32F polyCount,
									   const FindReachableActionPacksParams& params);

U32F GetActionPacksFromPolyList(ActionPack** ppOutList,
								U32F maxActionPackCount,
								const NavPoly** pPolyList,
								U32F polyCount,
								U32F actionPackTypeMask = 0,
								bool obeyEnabled = true);

U32 PolyExpandFindActionPacksByTypeInRadius(ActionPack** const results,
											U32 maxResults,
											U32 typeMask,
											const NavPoly* pStartPoly,
											const Point startWs,
											const float r,
											const bool debug = false);

// NOTE: does not currently support Ex polys
void WalkPath(NavMesh::ProbeParams& params, const PathWaypointsEx* const pPathPs, NavMesh::FnVisitNavPoly visitFunc, uintptr_t userData, bool debug = false);

// NOTE: does not currently support Ex polys
void WalkPath(NavMesh::ProbeParams& params, const PathWaypointsEx* const pPathPs, float walkLength, NavMesh::FnVisitNavPoly visitFunc, uintptr_t userData, bool debug = false);

void FindSurfaceNormalWithFallbackPs(Vector& normalPs, const NavMesh* pNavMesh, const NavPoly* pNavPoly, const Point posPs, bool debug = false);
void FindSurfaceNormalWithFallbackWs(Vector& normalWs, const NavMesh* pNavMesh, const NavPoly* pNavPoly, const Point posWs, bool debug = false);

bool FindSurfaceNormalUsingHeightMapPs(Vector& normalPs,
									   const NavMesh* pNavMesh,
									   const Point posPs,
									   bool debug = false);
bool FindSurfaceNormalUsingHeightMapWs(Vector& normalWs,
									   const NavMesh* pNavMesh,
									   const Point posWs,
									   bool debug = false);

Point FindYWithFallbackPs(const NavMesh* pNavMesh, const NavPoly* const pNavPoly, Point posPs, bool debug = false);
Point FindYWithFallbackWs(const NavMesh* pNavMesh, const NavPoly* const pNavPoly, Point posWs, bool debug = false);
bool FindYUsingHeightMapPs(const NavMesh* pNavMesh, Point& posPs, bool debug = false);
bool FindYUsingHeightMapWs(const NavMesh* pNavMesh, Point& posWs, bool debug = false);

void AssessSpaceAroundNavLocation(const NavLocation& navLoc, float& totalNavigableDistance, bool& obstructing, float centerR = 0.36f, float sideR = 0.635f, bool debug = false);

/// --------------------------------------------------------------------------------------------------------------- ///
namespace Nav
{
	bool AreNavPolysConnected(const NavPoly* pPolyA, const NavPoly* pPolyB);

	I32F GetIncomingEdge(const NavPoly* pPoly,
						 const NavPolyEx* pPolyEx,
						 const NavPoly* pSourcePoly,
						 const NavPolyEx* pSourcePolyEx);

	bool BuildDebugSelObeyedBlockers(NavBlockerBits* pBits);

	NavBlockerBits BuildObeyedBlockerList(const SimpleNavControl* pNavControl,
										  SimpleNavControl::PFnNavBlockerFilterFunc filterFunc,
										  bool forPathing,
										  BoxedValue userData = BoxedValue(),
										  bool debug = false);

	void DebugPrintObeyedBlockers(const NavBlockerBits& bits, const char* characterName = nullptr);

	void DebugDrawProbeResultPs(const NavMesh* pMesh,
								 const NavMesh::ProbeResult result,
								 const NavMesh::ProbeParams& paramsPs,
								 DebugPrimTime tt = kPrimDuration1FrameAuto);

	void DebugDrawProbeResultLs(const NavMesh* pMesh,
								const NavMesh::ProbeResult result,
								const NavMesh::ProbeParams& paramsLs,
								DebugPrimTime tt = kPrimDuration1FrameAuto);
}; // namespace Nav
