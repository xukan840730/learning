/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/nav/action-pack-handle.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-node-table.h"
#include "gamelib/gameplay/nav/nav-path-build.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/scriptx/h/nd-ai-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class CatmullRom;
class NavHandle;
#if ENABLE_NAV_LEDGES
class NavLedge;
class NavLedgeGraph;
#endif
class NavMesh;
class NavPoly;
class NavPolyEx;

namespace DMENU
{
	class ItemEnumPair;
	class Menu;
}

/// --------------------------------------------------------------------------------------------------------------- ///
namespace Nav
{
	struct PathFindParams;

	static CONST_EXPR size_t kMaxPathFindGoals = 64;
	typedef BitArray64 PathFindGoalBits;

	/************************************************************************/
	/* Core Structures for doing A* on NavMeshes                            */
	/************************************************************************/

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct FindPathOwner
	{
		void Set(NdGameObjectHandle hOwnerGo, const char* sourceFile, U32 sourceLine, const char* sourceFunc)
		{
			m_hOwnerGo	 = hOwnerGo;
			m_sourceFile = sourceFile;
			m_sourceLine = sourceLine;
			m_sourceFunc = sourceFunc;
		}

		NdGameObjectHandle m_hOwnerGo;
		const char* m_sourceFile;
		U32 m_sourceLine;
		const char* m_sourceFunc;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct CombatVectorInfo
	{
		Locator m_combatVectorLocator;
		float m_combatVectorScale = 1.0f;
		DC::PointCurve m_combatVectorCurve;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct PathFindContext
	{
		static CONST_EXPR U32 kMaxThreats = NavCharacter::kMaxThreats;
		static CONST_EXPR U32 kMaxFriends = NavCharacter::kMaxFriends;
		static CONST_EXPR U32 kMaxMeleePenaltySegments = 16;

		Locator m_ownerLocPs = kIdentity;

		StringId64 m_pathCostTypeId = INVALID_STRING_ID_64;
		uintptr_t m_pathCostUserData = 0;

		Locator m_parentSpace = kIdentity;

		U32F m_ownerReservedApMgrId = ActionPackHandle::kInvalidMgrId;
		float m_pathRadius = 0.0f;

		bool m_dynamicSearch = false; // search through dynamically patched nav mesh data
		bool m_reverseSearch = false;	  // take TAP node links backwards
		Nav::StaticBlockageMask m_obeyedStaticBlockers = Nav::kStaticBlockageMaskAll;

		NavBlockerBits m_obeyedBlockers = NavBlockerBits(false);

		U32 m_threatPositionCount = 0;
		Point m_threatPositionsPs[kMaxThreats];
		Point m_threatFuturePosPs[kMaxThreats];

		U32 m_friendPositionCount = 0;
		Point m_friendPositionsPs[kMaxFriends];

		CombatVectorInfo m_combatVectorInfo;
		Segment m_aMeleePenaltySegment[kMaxMeleePenaltySegments];

		union
		{
			struct
			{
				bool m_hasCombatVector			: 1;

				bool m_hasMeleePenaltySegments	: 1;
				U8   m_numMeleePenaltySegments	: 6;

				bool m_isHorse					: 1;
			};
			U16 m_flags = 0;
		};
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Combined location information for representing a location on a NavMesh or NavLedge
	// (should only be used as temporary input to the path find)
	struct PathFindLocation
	{
		PathFindLocation()
			: m_pNavPoly(nullptr)
			, m_pNavPolyEx(nullptr)
#if ENABLE_NAV_LEDGES
			, m_pNavLedge(nullptr)
#endif
			, m_pathNodeId(NavPathNodeMgr::kInvalidNodeId)
			, m_posWs(kOrigin)
			, m_baseCost(0.0f)
		{
		}

		bool Create(const PathFindParams& pathParams,
					const NavLocation& inputLocation,
					NavMeshHandle hFallbackMesh = NavMeshHandle());

		NavLocation GetAsNavLocation() const
		{
			NavLocation ret;

			if (m_pNavPoly)
			{
				ret.SetWs(m_posWs, m_pNavPoly);
			}
			else
			{
				ret.SetWs(m_posWs);
			}

			return ret;
		}

		const NavPoly* m_pNavPoly;
		const NavPolyEx* m_pNavPolyEx;
#if ENABLE_NAV_LEDGES
		const NavLedge* m_pNavLedge;
#endif
		U32F m_pathNodeId;
		Point m_posWs;
		float m_baseCost;
	};

	// If we shrink this debug menus break. We should make the menus support U8.
	enum class PlayerBlockageCost : I32
	{
		kFree,
		kCheap,
		kExpensive,
		kImpassable,
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct PathFindParams
	{
		enum class CostMode
		{
			kBruteForce,
			kRobinHood,
			kAgnostic,

			kDefault = kBruteForce
		};

		void AddStartPosition(NavLocation loc, float initialCost = 0.0f)
		{
			if (m_numStartPositions < kMaxStartPositions)
			{
				m_startPositions[m_numStartPositions] = loc;
				m_startCosts[m_numStartPositions]	  = initialCost;
				++m_numStartPositions;
			}
		}

		void ConstructPreferredPolys(const NavPolyHandle* preferredPolys)
		{
			memcpy(m_preferredPolys, preferredPolys, sizeof(NavPolyHandle) * BuildPathResults::kNumCachedPolys);
		}

		static CONST_EXPR size_t kMaxStartPositions = 4;

		PathFindContext m_context;

		NavLocation m_startPositions[kMaxStartPositions];
		float m_startCosts[kMaxStartPositions];
		U32 m_numStartPositions = 0;

		NavPolyHandle m_preferredPolys[BuildPathResults::kNumCachedPolys];

		F32 m_findPolyYThreshold = 2.0f;
		F32 m_findPolyXZCullRadius = 0.0f;
		StringId64 m_bindSpawnerNameId = INVALID_STRING_ID_64;
		U32 m_traversalSkillMask = 0;
		I32 m_tensionMode = -1;
		U32 m_factionMask = 1;

		ActionPackHandle m_hPreferredTap;
		F32 m_preferredTapBias = -3.0f;
		F32 m_maxTravelDist = -1.0f;
		F32 m_maxExpansionRadius = -1.0f;
		F32 m_distanceGoal = 0.0f;
		CostMode m_costMode = CostMode::kDefault;

		PlayerBlockageCost m_playerBlockageCost = PlayerBlockageCost::kExpensive;

		bool m_fixGapChecks = false;
		bool m_ignoreNodeExtraCost = false;
		bool m_debugDrawSearch = false;
		bool m_debugDrawResults = false;

		DebugPrimTime m_debugDrawTime = kPrimDuration1FrameAuto;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	void CreatePathFindParamsDevMenu(DMENU::Menu* pMenu,
									 PathFindParams* pParamsToUse,
									 const DMENU::ItemEnumPair* pTensionModes);

	void DebugPrintPathFindParams(const PathFindParams& params, MsgOutput output);

	/// --------------------------------------------------------------------------------------------------------------- ///
	class PathCostMgr
	{
	public:
		static const U32 kMaxCostFuncs = 16;
		typedef float (*PFuncCost)(const PathFindContext& context,
								   const NavPathNodeProxy& pathNode,
								   const NavPathNode::Link& link,
								   Point_arg fromPosPs,
								   Point_arg toPosPs,
								   bool* reject);

	public:
		PathCostMgr() : m_numFuncs(0) {}

		static PathCostMgr& Get() { return s_singleton; }

		void RegisterCostFunc(StringId64 nameId, PFuncCost costFunc);
		PFuncCost LookupCostFunc(const PathFindContext& context, StringId64 nameId);

	private:
		StringId64 m_names[kMaxCostFuncs];
		PFuncCost m_funcs[kMaxCostFuncs];
		U32 m_numFuncs;

		static PathCostMgr s_singleton;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	class PathFindConfig
	{
	public:
		void Init(const PathFindParams& pathParams);

		PathCostMgr::PFuncCost m_costFunc;
		float m_zeroValue;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	class PathFindNodeData
	{
	public:
		typedef bool (*FnShouldExpand)(const NavPathNode* pNode,
									   const NavMesh* pNavMesh,
#if ENABLE_NAV_LEDGES
									   const NavLedgeGraph* pLedgeGraph,
#endif
									   const PathFindParams* pParams);

		PathFindNodeData() : m_visitedNodes(), m_config(), m_pFnShouldExpand(nullptr), m_trivialHashVisitedNodes(nullptr) {}

		void Init(const PathFindParams& params);

		void DebugDraw(DebugPrimTime drawTime = kPrimDuration1FrameAuto) const;
		const Locator& GetParentSpace() const { return m_context.m_parentSpace; }

#ifdef HEADLESS_BUILD
		bool IsHandleVisited(const NavHandle& hNav, NavKeyDataPair* pKeyDataPairOut = nullptr) const;
		bool IsLocationVisited(const NavLocation& navLoc, NavKeyDataPair* pKeyDataPairOut = nullptr) const;

	private:
		bool IsKeyVisited(const NavNodeKey& key, const NavHandle& hNav, NavKeyDataPair* pKeyDataPairOut = nullptr) const;

#else
		bool IsHandleVisited(const NavHandle& hNav, TrivialHashNavNodeData* pDataOut = nullptr) const;
		bool IsNodeVisited(const NavManagerId navManagerId, const NavPathNode::NodeId nodeId, TrivialHashNavNodeData* pDataOut = nullptr) const;
#endif

	public:

		NavNodeTable m_visitedNodes;

		// for undirecteds
		TrivialHashNavNodeData* m_trivialHashVisitedNodes;

		PathFindContext m_context; // info about the context in which the path is generated
		PathFindConfig m_config;   // info about the generation of the path data

		FnShouldExpand m_pFnShouldExpand;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct PathFindResults
	{
		PathFindResults()
		{
			m_overflowedClosedList = false;
			m_reachedGoals.ClearAllBits();
		}

		NavNodeKey m_goalNodes[kMaxPathFindGoals];
		PathFindGoalBits m_reachedGoals;
		bool m_overflowedClosedList;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	void DebugDrawNodeTable(const NavNodeTable* pNodeTable, const TrivialHashNavNodeData* pTrivialHashData, DebugPrimTime drawTime = kPrimDuration1FrameAuto);

	/// --------------------------------------------------------------------------------------------------------------- ///
	U32F GatherStartLocations(const PathFindParams& params,
							  PathFindLocation* aStartLocationsOut,
							  U32 maxStartLocations,
							  NavMeshHandle* pFirstStartNavMeshOut = nullptr);
	/// --------------------------------------------------------------------------------------------------------------- ///
	// Costing functions
	F32 CostFuncDistance(const PathFindContext& context,
						 const NavPathNodeProxy& pathNode,
						 const NavPathNode::Link& link,
						 Point_arg fromPosPs,
						 Point_arg toPosPs,
						 bool* pReject);
	F32 CostFuncCombatVector(const PathFindContext& context,
							 const NavPathNodeProxy& pathNode,
							 const NavPathNode::Link& link,
							 Point_arg fromPosPs,
							 Point_arg toPosPs,
							 bool* pReject);
	F32 CostFuncCombatVectorWithStealthGrass(const PathFindContext& context,
		const NavPathNodeProxy& pathNode,
		const NavPathNode::Link& link,
		Point_arg fromPosPs,
		Point_arg toPosPs,
		bool* pReject);
	F32 CostFuncCombatVectorWeighted(const PathFindContext& context,
									 const NavPathNodeProxy& pathNode,
									 const NavPathNode::Link& link,
									 Point_arg fromPosPs,
									 Point_arg toPosPs,
									 float edgeWeight);

	/// --------------------------------------------------------------------------------------------------------------- ///
	U64 ComputePartitionValueFromGaps(const PathFindContext& pathContext,
									  const NavMesh* pMesh,
									  const NavPoly* pPoly,
									  Point_arg posLs);

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Run A* until goals are met, or we stop expanding nodes
	// both possible template args are explicitly instantiated in nav-path-find.cpp
	template <bool useTrivialHashTable>
	PathFindResults DoPathFind(const PathFindParams& pathParams,
							   PathFindNodeData& workData,
							   const PathFindLocation* pStartLocations,
							   const U32F numStartLocations,
							   const PathFindLocation* pGoalLocations,
							   const U32F numGoalLocations);

	/************************************************************************/
	/* Single goal NavMesh A*                                               */
	/************************************************************************/

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct FindSinglePathParams : public PathFindParams
	{
		FindSinglePathParams() {}

		FindSinglePathParams(const PathFindParams& baseParams) : PathFindParams(baseParams) {}

		BuildPathParams m_buildParams;

		NavLocation m_goal;
	};

	void DebugPrintFindSinglePathParams(const FindSinglePathParams& pathParams, MsgOutput output);

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct FindSinglePathResults
	{
		FindSinglePathResults() { Clear(); }

		void Clear() { m_buildResults.Clear(); }

		BuildPathResults m_buildResults;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	// A* on the NavMesh to a single goal
	void FindSinglePath(const FindPathOwner& owner,
						const FindSinglePathParams& pathParams,
						FindSinglePathResults* pPathResults);

	/************************************************************************/
	/* Spline Following A*                                                  */
	/************************************************************************/

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct FindSplinePathParams : public PathFindParams
	{
		FindSplinePathParams(const PathFindParams& baseParams) : PathFindParams(baseParams) {}
		FindSplinePathParams() = default;

		BuildPathParams m_buildParams;

		NavLocation m_tailGoal;
		bool m_tailGoalValid = false;

		const CatmullRom* m_pPathSpline = nullptr;
		float m_arcStart = 0.0f;
		float m_arcGoal = 0.0f;
		float m_arcStep = 2.0f;
		float m_arcObstacleStep = 5.0f;
		float m_onSplineRadius = 1.0f;
	};

	void DebugPrintFindSplinePathParams(const FindSplinePathParams& pathParams, MsgOutput output);

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct FindSplinePathResults
	{
		FindSplinePathResults() { Clear(); }

		void Clear() { m_buildResults.Clear(); m_splineStartArc = -1.0f; }

		float m_splineStartArc;
		BuildPathResults m_buildResults;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	// A* on the NavMesh to move along a spline
	bool TryAdvanceSplineStep(float& cur, float goal, float step, float max, bool looping);
	void FindSplinePath(const FindPathOwner& owner,
						const FindSplinePathParams& pathParams,
						FindSplinePathResults* pPathResults);

	/************************************************************************/
	/* Undirect NavMesh A*													*/
	/************************************************************************/

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct FindUndirectedPathsParams : public PathFindParams
	{
		FindUndirectedPathsParams() {}

		FindUndirectedPathsParams(const PathFindParams& baseParams) : PathFindParams(baseParams) {}
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct FindUndirectedPathsResults
	{
		// using full hash table, 256 KB

		// using trivial hash, 224 KB (assuming a TrivialHashNavNodeData is still 28 bytes...)
		// if we always do fast approxsmooth paths instead of using the fromDist, we could skip storing fromDist and drop
		// another 2 bytes
#ifdef HEADLESS_BUILD
		static CONST_EXPR U32 kBufferSize = 256 * 1024;
#else
		static CONST_EXPR U32 kBufferSize = NavPathNodeMgr::kMaxNodeCount * sizeof(TrivialHashNavNodeData);
#endif

		FindUndirectedPathsResults();

#ifdef HEADLESS_BUILD
		FindUndirectedPathsResults(const FindUndirectedPathsResults &srcResults);
#endif

		void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
		{
			if (m_searchData.m_visitedNodes.IsInitialized())
			{
				m_searchData.m_visitedNodes.Relocate(deltaPos, lowerBound, upperBound);
			}
			if (m_searchData.m_trivialHashVisitedNodes)
			{
				RelocatePointer(m_searchData.m_trivialHashVisitedNodes, deltaPos, lowerBound, upperBound);
			}
		}

		void TrivialRelocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
		{
			NAV_ASSERT(!m_searchData.m_visitedNodes.IsInitialized());
			NAV_ASSERT(m_searchData.m_trivialHashVisitedNodes);

			RelocatePointer(m_searchData.m_trivialHashVisitedNodes, deltaPos, lowerBound, upperBound);
		}

		PathFindNodeData m_searchData;
		U8 m_buffer[kBufferSize];
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Undirected A* on the NavMesh
	void FindUndirectedPaths(const FindPathOwner& owner,
							 const FindUndirectedPathsParams& undirectedPathParams,
							 FindUndirectedPathsResults* pUndirectedPathResults);

	/// --------------------------------------------------------------------------------------------------------------- ///
	// A* on the NavMesh til we hit a certain distance, then stop
	void FindDistanceGoal(const FindPathOwner& owner,
						  const FindSinglePathParams& pathParams,
						  FindSinglePathResults* pPathResults);

} // namespace Nav
