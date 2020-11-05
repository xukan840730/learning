/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/nd-frame-state.h"

#include "gamelib/gameplay/nav/action-pack-handle.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-handle.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-path-find.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class CatmullRom;
class DynamicNavBlocker;
class NavCharacter;
#if ENABLE_NAV_LEDGES
class NavLedge;
class NavLedgeGraph;
#endif
class NavMesh;
class NavPoly;
class NdGameObject;
class PathWaypointsEx;

/// --------------------------------------------------------------------------------------------------------------- ///
//
// class SimpleNavControl
//
//  Bare-bones NavControl functionality required by both Npc and SimpleNpc.
//  Serves as a mid-level interface to the navigation system. "Mid-level" because it does
//  not depend on higher level classes such as NavCharacter.
//
class SimpleNavControl
{
public:
	static const U32F kMaxNavBlockerIgnoreCount = 16;

	struct NavFlags
	{
		void Reset()
		{
			memset(this, 0, sizeof(*this));
		}

		void UpdateFrom(const NavMesh* pMesh, const NavPoly* pPoly);
#if ENABLE_NAV_LEDGES
		void UpdateFrom(const NavLedgeGraph* pGraph, const NavLedge* pLedge);
#endif

		struct
		{
			StringId64 m_forcedDemeanorId;
			bool m_forcedSwim;
			bool m_forcedDive;
			bool m_forcedWalk;
			bool m_forcedWade;
			bool m_forcedCrouch;
			bool m_forcedProne;
			bool m_stealthVegetation;
		} m_navMesh;

		struct
		{
		} m_navLedge;
	};

	typedef bool (*PFnNavBlockerFilterFunc)(const SimpleNavControl* pNavControl,
											const DynamicNavBlocker* pBlocker,
											BoxedValue userData,
											bool forPathing,
											bool debug);

	static bool DefaultNavBlockerFilter(const SimpleNavControl* pNavControl,
										const DynamicNavBlocker* pBlocker,
										BoxedValue userData,
										bool forPathing,
										bool debug);

	SimpleNavControl();

	bool IsSimple() const						{ return m_isSimple; } // poor man's RTTI

	void SetFactionId(U8 id)					{ m_factionId = id; }
	U8 GetFactionId() const						{ return m_factionId; }

	void SetCanSwim(bool canSwim)				{ m_canSwim = canSwim; }

	void ResetNavMesh(const NavMesh* pMesh, NdGameObject* pGameObj = nullptr);

	void SetActiveNavType(const NdGameObject* pGameObj, const NavLocation::Type navType);
	NavLocation::Type GetActiveNavType() const { return m_activeNavType; }
	void SetNavLocation(const NdGameObject* pGameObj, const NavLocation& navLoc);
	const NavLocation& GetNavLocation() const { return m_navLocation; }
	bool HasNavLocationTurnedInvalid() const;

	// legacy, should be considered deprecated
	const NavMesh* GetNavMesh() const { return m_navLocation.ToNavMesh(); }
	const NavPoly* GetNavPoly() const { return m_navLocation.ToNavPoly(); }
	NavMeshHandle GetNavMeshHandle() const { return m_navLocation.GetNavMeshHandle(); }
	NavPolyHandle GetNavPolyHandle() const { return m_navLocation.GetNavPolyHandle(); }

	// update the NavMesh/NavPoly
	void UpdatePs(Point_arg posPs, NdGameObject* pGameObj, const NavMesh* pAltMesh = nullptr);
	void SimpleUpdatePs(Point_arg pos, const NavPoly* pPoly, NdGameObject* pGameObj);

	void EnableNavMeshAutoRebind(bool f)		{ m_navMeshAutoRebind = f; }

	// NavMesh queries
	const Point FindNearestPointOnNavMeshPs(Point_arg posPs) const;
	bool ClearLineOfMotionPs(Point_arg startPosPs,
							 Point_arg endPosPs,
							 bool radialProbe	   = true,
							 Point* pReachedPosOut = nullptr) const;
	bool StaticClearLineOfMotionPs(Point_arg startPosPs,
								   Point_arg endPosPs,
								   bool radialProbe = true,
								   NavMesh::ProbeParams* pResults = nullptr) const;
	bool DynamicClearLineOfMotionPs(Point_arg startPosPs,
									Point_arg endPosPs,
									bool radialProbe = true,
									NavMesh::ProbeParams* pResults = nullptr) const;
	bool BlockersOnlyClearLineOfMotion(Point_arg startPosPs, Point_arg endPosPs) const;

	// query interface to abstract dynamic pathing
	bool IsPointDynamicallyBlockedPs(Point posPs) const;
	bool IsPointStaticallyBlockedPs(Point posPs, float radius = -1.0f) const;
	bool IsNavLocationBlocked(const NavLocation loc, bool dynamic) const;
	bool IsCurNavLocationBlocked(const NdGameObject* pOwner, bool debugDraw = false) const;

	NavBlockerBits BuildObeyedBlockerList(bool forPathing) const;
	void SetObeyedBlockerFilterFunc(PFnNavBlockerFilterFunc filterFunc, BoxedValue userData = BoxedValue());
	void ClearObeyedBlockerFilterFunc();
	bool HasCustomNavBlockerFilterFunc() const { return m_obeyedBlockerFilterFunc != DefaultNavBlockerFilter; }
	bool IsNavBlockerIgnoredFromFilter(const DynamicNavBlocker* pBlocker, bool forPathing, bool debug = false) const
	{
		return !m_obeyedBlockerFilterFunc(this, pBlocker, m_obeyedBlockerFilterUserData, forPathing, debug);
	}
	PFnNavBlockerFilterFunc GetObeyedBlockerFilterFunc() const { return m_obeyedBlockerFilterFunc; }
	BoxedValue GetObeyedBlockerFilterFuncUserData() const { return m_obeyedBlockerFilterUserData; }
	const NavBlockerBits& GetCachedObeyedNavBlockers() const { return m_obeyedNavBlockers; }

	void SetObeyedStaticBlockers(const Nav::StaticBlockageMask obeyedStaticBlockers)
	{
		m_obeyedStaticBlockers = obeyedStaticBlockers;
	}

	void OverrideObeyedStaticBlockersFrame(const Nav::StaticBlockageMask obeyedStaticBlockers)
	{
		// 2 frames since this get accessed in potentially a bunch of places and we don't want to race with the settings update next frame.
		m_obeyedStaticBlockersOverride = obeyedStaticBlockers;
		m_obeyedStaticBlockersOverrideFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused + 1;
	}

	Nav::StaticBlockageMask GetObeyedStaticBlockers() const
	{
		const I64 curFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
		return curFrame <= m_obeyedStaticBlockersOverrideFrame
				   ? m_obeyedStaticBlockersOverride
				   : (m_ignoreStaticNavBlocker ? Nav::kStaticBlockageMaskNone : (m_obeyedStaticBlockers));
	}

	void EnableEnemyNavBlockers(bool f) { m_addEnemyNavBlockers = f; }

	void SetIgnoreStaticBlocker() { m_ignoreStaticNavBlocker = true; }
	void ClearIgnoreStaticBlocker() { m_ignoreStaticNavBlocker = false; }
	bool GetIgnoreStaticBlocker() const { return m_ignoreStaticNavBlocker; }

	void SetIgnorePlayerBlocker() { m_ignorePlayerNavBlocker = true; }
	void ClearIgnorePlayerBlocker() { m_ignorePlayerNavBlocker = false; }
	bool GetIgnorePlayerBlocker() const { return m_ignorePlayerNavBlocker; }

	void SetIgnoreNavBlocker(U32F i, const Process* pProc)
	{
		NAV_ASSERT(i < kMaxNavBlockerIgnoreCount);
		m_ignoreNavBlockerList[i] = pProc;
	}
	ProcessHandle GetIgnoreNavBlocker(U32F i) const
	{
		NAV_ASSERT(i < kMaxNavBlockerIgnoreCount);
		return m_ignoreNavBlockerList[i];
	}
	bool IsNavBlockerIgnored(const DynamicNavBlocker* pBlocker) const;

	bool IsInStealthVegetation() const { return m_cachedNavFlags.m_navMesh.m_stealthVegetation; }

	StringId64 NavMeshForcedDemeanor() const { return m_cachedNavFlags.m_navMesh.m_forcedDemeanorId; }

	bool NavMeshForcesSwim() const   { return m_cachedNavFlags.m_navMesh.m_forcedSwim; }
	bool NavMeshForcesDive() const   { return m_cachedNavFlags.m_navMesh.m_forcedDive; }
	bool NavMeshForcesWalk() const   { return m_cachedNavFlags.m_navMesh.m_forcedWalk; }
	bool NavMeshForcesWade() const   { return m_cachedNavFlags.m_navMesh.m_forcedWade; }
	bool NavMeshForcesCrouch() const { return m_cachedNavFlags.m_navMesh.m_forcedCrouch; }
	bool NavMeshForcesProne() const  { return m_cachedNavFlags.m_navMesh.m_forcedProne; }

	NavLocation GetLastGoodNavLocation() const { return m_lastGoodNavLocation; }
	Quat GetLastGoodRotation() const { return m_lastGoodRotation; }

	void RefreshCachedNavFlags();

	void ResetEverHadAValidNavMesh() { m_everHadValidNavMesh = false; }
	bool EverHadAValidNavMesh() const { return m_everHadValidNavMesh; }

	float GetEffectivePathFindRadius() const
	{
		return (m_pathFindRadius >= 0.0f) ? m_pathFindRadius : (m_movingNavAdjustRadius + 0.05f); // pad for some safety so we don't catch edges when we follow the path;
	}

	float GetCurrentNavAdjustRadius() const { return m_curNavAdjustRadius; }
	float GetDesiredNavAdjustRadius() const { return m_curNavAdjustRadius; }
	float GetMovingNavAdjustRadius() const { return m_movingNavAdjustRadius; }
	float GetIdleNavAdjustRadius() const { return (m_idleNavAdjustRadius >= 0.0f) ? m_idleNavAdjustRadius : m_movingNavAdjustRadius; }
	float GetMaximumNavAdjustRadius() const { return Max(m_movingNavAdjustRadius, m_idleNavAdjustRadius); }
	float GetPathFindRadius() const { return m_pathFindRadius; }

	void ConfigureNavRadii(float movingNavRadius, float idleNavRadius, float pathFindRadius);
	float GetNavAdjustRadiusConfig(float& movingRadius, float& idleRadius, float& pathRadius) const
	{
		movingRadius = m_movingNavAdjustRadius;
		idleRadius	 = m_idleNavAdjustRadius;
		pathRadius	 = m_pathFindRadius;
		return m_curNavAdjustRadius;
	}

	float GetActionPackEntryDistance() const { return Max(GetMaximumNavAdjustRadius(), m_pathFindRadius); }

protected:
	void UpdateNavAdjustRadius(const NdGameObject* pGameObj);
	void UpdateNpcStature(const NdGameObject* const pGameObj);

	void UpdatePs_NavPoly(Point_arg posPs, NdGameObject* pGameObj, const NavMesh* pAltMesh = nullptr);
#if ENABLE_NAV_LEDGES
	void UpdatePs_NavLedge(Point_arg posPs, NdGameObject* pGameObj);
#endif

	void SetNavPoly(const NdGameObject* pGameObj, Point_arg posPs, const NavPoly* pPoly);
#if ENABLE_NAV_LEDGES
	void SetNavLedge(const NdGameObject* pGameObj, Point_arg posPs, const NavLedge* pLedge);
#endif
	void SetPosWs(const NdGameObject* pGameObj, Point_arg posWs);

	Locator					m_parentSpace;
	Point					m_lastPosPs;
	U32						m_ownerProcessId;

	NavLocation::Type		m_activeNavType;
	NavLocation				m_navLocation;
	NavFlags				m_cachedNavFlags;

	NavLocation				m_lastGoodNavLocation;
	Quat					m_lastGoodRotation;

	Nav::StaticBlockageMask m_obeyedStaticBlockers;
	Nav::StaticBlockageMask m_obeyedStaticBlockersOverride;
	I64						m_obeyedStaticBlockersOverrideFrame;
	NavBlockerBits			m_obeyedNavBlockers;
	ProcessHandle			m_ignoreNavBlockerList[kMaxNavBlockerIgnoreCount]; // which nav blockers to ignore

	U8						m_factionId;					// selects TAPs based on faction

	StringId64				m_ownerProcessType;

	PFnNavBlockerFilterFunc m_obeyedBlockerFilterFunc;
	BoxedValue				m_obeyedBlockerFilterUserData;

	NavMesh::NpcStature		m_minNpcStature;

	float					m_desNavAdjustRadius;
	float					m_curNavAdjustRadius;
	float					m_movingNavAdjustRadius;
	float					m_idleNavAdjustRadius;
	float					m_pathFindRadius;

	bool					m_navMeshAutoRebind		 : 1;	// allow UpdatePs() to change nav meshes automatically
	bool					m_addEnemyNavBlockers	 : 1;
	bool					m_isSimple				 : 1;
	bool					m_everHadValidNavMesh	 : 1;
	bool					m_canSwim				 : 1;
	bool					m_ignoreStaticNavBlocker : 1;
	bool					m_ignorePlayerNavBlocker : 1;
};

/// --------------------------------------------------------------------------------------------------------------- ///
//
// class NavControl
//
// Full implementation used by Npc (but not SimpleNpc). This one *does* depend on NavCharacter, by the way.
//
//   - provides an interface for NavMesh (static) path finds
//   - keeps track of which NavMesh and poly it is currently on.
//   - (optionally) owns a exposure map for influencing both static and dynamic path finds
//
class NavControl : public SimpleNavControl
{
public:
	struct PathFindJobHandle
	{
	public:
		PathFindJobHandle()
			: m_pSinglePathResults(nullptr)
			, m_pSplinePathResults(nullptr)
			, m_hCounter(nullptr)
		{
		}

		bool IsValid() const { return m_hCounter && (m_pSinglePathResults || m_pSplinePathResults); }
		void WaitForJob()
		{
			if (m_hCounter)
			{
				ndjob::WaitForCounterAndFree(m_hCounter);
				m_hCounter = nullptr;
			}
		}

		void SetCounter(ndjob::CounterHandle hCounter) { m_hCounter = hCounter; }

		Nav::FindSinglePathResults*	m_pSinglePathResults;
		Nav::FindSplinePathResults*	m_pSplinePathResults;

	private:
		ndjob::CounterHandle m_hCounter;
	};

	NavControl();
	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) { }

	// SearchBindSpawnerId defines whether to limit searches to a certain platform space
	StringId64				GetSearchBindSpawnerId() const {return m_searchBindSpawnerNameId; }

	struct PathFindOptions
	{
		Locator m_parentSpace;
		NavLocation m_startLoc;
		NavLocation m_goalLoc;

		U32 m_traversalSkillMask = 0;

		ActionPackHandle m_hPreferredTap = ActionPackHandle();

		PFnNavBlockerFilterFunc m_navBlockerFilterFunc = DefaultNavBlockerFilter;
		BoxedValue m_nbFilterFuncUserData = BoxedValue();

		const CatmullRom* m_pPathSpline = nullptr;
		float m_splineArcStart = 0.0f;
		float m_splineArcGoal = 0.0f;
		float m_splineArcStep = 2.0f;

		float m_pathRadius = -1.0f;

		bool m_waitForPatch = false;
		bool m_enableSplineTailGoal = false;
	};

	PathFindJobHandle BeginPathFind(const NavCharacter* pNavChar,
									const PathFindOptions& options,
									const char* sourceFile,
									U32 sourceLine,
									const char* sourceFunc) const;

	struct PathFindResults
	{
		PathWaypointsEx m_pathPs;
		PathWaypointsEx m_postTapPathPs;
		ActionPackHandle m_hNavAp;
		float m_splineArcStart = -1.0f;
	};

	bool CollectPathFindResults(PathFindJobHandle& hJob, PathFindResults& results, bool updateCachedPolys = false) const;

	// get/set configuration options
	void SetPathYThreshold(float thresh);
	void SetPathXZCullRadius(float radius)				{ m_pathXZCullRadius = radius; }
	float GetPathYThreshold() const						{ return m_pathYThreshold; }
	void SetSearchBindSpawnerNameId(StringId64 nameId)	{ m_searchBindSpawnerNameId = nameId; }
	void SetSmoothPath(Nav::BuildPathSmoothingParam f)	{ m_smoothPath = f; }
	Nav::BuildPathSmoothingParam GetSmoothPath() const		{ return m_smoothPath; }
	void SetTruncateAfterTap(U32F tapCount)				{ m_truncateAfterTap = tapCount; }
	U32F GetTruncateAfterTap() const					{ return m_truncateAfterTap; }

	void SetTraversalSkillMask(U32F mask)		{ m_traversalSkillMask = mask; }
	U32F GetTraversalSkillMask() const			{ return m_traversalSkillMask; }

	NavMesh::NpcStature GetMinNpcStature() const { return m_minNpcStature; }

	void SetUndirectedPlayerBlockageCost(Nav::PlayerBlockageCost undirectedPlayerBlockageCost) { m_undirectedPlayerBlockageCost = undirectedPlayerBlockageCost; }
	void ClearUndirectedPlayerBlockageCost() { m_undirectedPlayerBlockageCost = Nav::PlayerBlockageCost::kExpensive; }
	Nav::PlayerBlockageCost GetUndirectedPlayerBlockageCost() const { return m_undirectedPlayerBlockageCost; }

	void SetDirectedPlayerBlockageCost(Nav::PlayerBlockageCost directedPlayerBlockageCost) { m_directedPlayerBlockageCost = directedPlayerBlockageCost; }
	void ClearDirectedPlayerBlockageCost() { m_directedPlayerBlockageCost = Nav::PlayerBlockageCost::kImpassable; }
	Nav::PlayerBlockageCost GetDirectedPlayerBlockageCost() const { return m_directedPlayerBlockageCost; }

	void SetCareAboutPlayerBlockageDespitePushingPlayer() { m_careAboutPlayerBlockageDespitePushingPlayer = true; }
	void ClearCareAboutPlayerBlockageDespitePushingPlayer() { m_careAboutPlayerBlockageDespitePushingPlayer = false; }
	bool CareAboutPlayerBlockageDespitePushingPlayer() const { return m_careAboutPlayerBlockageDespitePushingPlayer; }

	// NavMesh queries
	U32F FindReachablePolysInRadiusPs(NavLocation startLoc, float radius, NavPolyHandle* ppOutList, U32F maxPolyCount) const;
	U32F FindReachableActionPacksInRadiusPs(NavLocation startLoc, float radius, ActionPack** ppOutList, U32F maxCount) const;
	U32F FindReachableActionPacksByTypeInRadiusPs(NavLocation startLoc,
												  float radius,
												  ActionPack::Type tt,
												  ActionPack** ppOutList,
												  U32F maxCount) const;

	U32F FindReachableActionPacksInRadiusWithTypeMaskPs(NavLocation startLoc,
														float radius,
														U32F typeMask,
														ActionPack** ppOutList,
														U32F maxCount) const;

	Nav::FindSinglePathParams GetFindSinglePathParams(const Locator& parentSpace,
													  const NavCharacter* pNavChar,
													  const NavLocation& startLoc,
													  const NavLocation& goalLoc,
													  PFnNavBlockerFilterFunc filterFunc = DefaultNavBlockerFilter,
													  BoxedValue userData = BoxedValue()) const;

	Nav::PathFindContext GetPathFindContext(const NavCharacter* pNavChar,
											const Locator& parentSpace,
											PFnNavBlockerFilterFunc filterFunc = DefaultNavBlockerFilter,
											BoxedValue userData = BoxedValue()) const;

	bool AdjustMoveVectorPs(Point_arg startPosPs,
							Point_arg endPosPs,
							float adjustRadius,
							float maxMoveDist,
							const NavBlockerBits& obeyedBlockers,
							Point* pReachedPosPsOut,
							bool debugDraw = false) const;

	bool AdjustMoveVectorPs(Point_arg startPosPs,
							Point_arg endPosPs,
							Segment probePs,
							float adjustRadius,
							float maxMoveDist,
							const NavBlockerBits& obeyedBlockers,
							Point* pReachedPosPsOut,
							bool debugDraw = false) const;

	const NavPolyHandle* GetCachedPathPolys() const
	{
		return m_cachedPolys;
	}

	void ClearCachedPathPolys()
	{
		for (int i = 0; i < Nav::BuildPathResults::kNumCachedPolys; ++i)
		{
			m_cachedPolys[i] = NavPolyHandle();
		}
	};

private:
	float		m_pathYThreshold;			// y-threshold to use for various NavMesh queries
	float		m_pathXZCullRadius;			// ignore NavMeshes outside this cull-radius

	StringId64	m_searchBindSpawnerNameId;	// defines which platform to search (or all)
	U32			m_traversalSkillMask;		// select which kinds of TAPs to allow for NavMesh path finds
	U32			m_truncateAfterTap;

	PFnNavBlockerFilterFunc m_navBlockerFilterFunc;
	BoxedValue m_navBlockerFilterFuncUserData;

	// how expensive is it to pathfind through TAPs that are player blocked? is it permitted at all?
	Nav::PlayerBlockageCost m_undirectedPlayerBlockageCost;
	Nav::PlayerBlockageCost m_directedPlayerBlockageCost;

	bool m_careAboutPlayerBlockageDespitePushingPlayer;

	// apply smoothing to NavMesh path
	Nav::BuildPathSmoothingParam m_smoothPath;

	// cache the first few path polys of a single-path pathfind, BEFORE smoothing,
	// so we can optionally prefer those (cost them lower) on the next pathfind, for hysteresis
	//
	// mutable because it's a cache
	//
	// NavPolyHandles instead of the smaller NodeIds because NodeIds are not safe to cache across frames
	mutable NavPolyHandle m_cachedPolys[Nav::BuildPathResults::kNumCachedPolys];
};
