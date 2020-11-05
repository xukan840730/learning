/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-path-find.h"

#include "corelib/containers/hashtable.h"
#include "corelib/math/quat-util.h"
#include "corelib/math/segment-util.h"
#include "corelib/math/segment.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/fast-min.h"
#include "corelib/util/msg.h"

#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/util/point-curve.h"

#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/ai/exposure-map.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-ex-data.h"
#include "gamelib/gameplay/nav/nav-handle.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-handle.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-mgr.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-util.h"
#include "gamelib/gameplay/nav/nav-ledge-graph.h"
#include "gamelib/gameplay/nav/nav-ledge.h"
#include "gamelib/gameplay/nav/nav-mesh-gap-ex.h"
#include "gamelib/gameplay/nav/nav-mesh-gap.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/nav-poly-ex.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/path-waypoints-ex.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"
#include "gamelib/spline/catmull-rom.h"

//#define DEBUG_PATH_HYSTERESIS
#define USE_FAST_LINEAR_LOOKUP 1

/// --------------------------------------------------------------------------------------------------------------- ///
namespace Nav
{

PathCostMgr PathCostMgr::s_singleton;

/// --------------------------------------------------------------------------------------------------------------- ///
#ifdef HEADLESS_BUILD

FindUndirectedPathsResults::FindUndirectedPathsResults()
{
	CustomAllocateJanitor tableMem(m_buffer, kBufferSize, FILE_LINE_FUNC);
	const size_t numElems = NavNodeTable::DetermineSizeInElements(kBufferSize);
	m_searchData.m_visitedNodes.Init(numElems, FILE_LINE_FUNC);
	m_searchData.m_trivialHashVisitedNodes = nullptr;
}

FindUndirectedPathsResults::FindUndirectedPathsResults(const FindUndirectedPathsResults &srcResults)
{
	CustomAllocateJanitor tableMem(m_buffer, kBufferSize, FILE_LINE_FUNC);
	const size_t numElems = NavNodeTable::DetermineSizeInElements(kBufferSize);
	m_searchData.m_visitedNodes.Init(numElems, FILE_LINE_FUNC);

	m_searchData.m_visitedNodes.Copy(srcResults.m_searchData.m_visitedNodes);

	m_searchData.m_trivialHashVisitedNodes = nullptr;
}

#else

FindUndirectedPathsResults::FindUndirectedPathsResults()
{
	memset(m_buffer, 0, kBufferSize);
	m_searchData.m_trivialHashVisitedNodes = reinterpret_cast<TrivialHashNavNodeData*>(m_buffer);
}

#endif

/// --------------------------------------------------------------------------------------------------------------- ///
struct OpenListKey
{
	OpenListKey(const NavNodeKey& k)
#ifdef HEADLESS_BUILD
		: m_u64(k.GetCombinedValue())
#else
		: m_u32(k.GetCombinedValue())
#endif
	{
	}

	NavNodeKey AsNavNodeKey() const
	{
		return NavNodeKey(m_pathNodeId, m_partitionId);
	}

#ifdef HEADLESS_BUILD
	bool operator == (const NavNodeKey& rhs) const { return (m_u64 == rhs.GetCombinedValue()); }

	union
	{
		struct
		{
			NavPathNode::NodeId m_pathNodeId;
			U16 m_partitionId;
			U8 m_padding[2];
		};
		U32 m_u64;
	};
#else
	bool operator == (const NavNodeKey& rhs) const { return (m_u32 == rhs.GetCombinedValue()); }

	union
	{
		struct
		{
			NavPathNode::NodeId m_pathNodeId;
			U16 m_partitionId;
		};
		U32 m_u32;
	};
#endif
};

typedef float OpenCostType;

/// --------------------------------------------------------------------------------------------------------------- ///
struct OpenNodeData : public NavNodeData
{
	const NavMesh* m_pNavMesh;
#if ENABLE_NAV_LEDGES
	const NavLedgeGraph* m_pLedgeGraph;
#endif
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct OpenNodeDataPair
{
	OpenNodeData m_data;
	OpenCostType m_cost;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class OpenList
{
public:
	void Init(size_t maxSize,
			  PathFindParams::CostMode mode,
			  const char* sourceFile,
			  U32F sourceLine,
			  const char* sourceFunc);

	void TryUpdateCost(const NavNodeKey& k, const OpenNodeData& d);

	bool RemoveBestNode(NavNodeKey* pNodeOut, OpenNodeData* pDataOut);
	bool Remove(const NavNodeKey& key, OpenNodeData* pDataOut = nullptr);

	bool GetData(const NavNodeKey& key, OpenNodeData* pParentDataOut) const;

	size_t GetCountHighWater() const { return m_countHw; }

private:
	struct FlatTable
	{
		OpenCostType* m_pCostArray = nullptr;
		OpenListKey* m_pKeyList	   = nullptr;
		OpenNodeData* m_pDataList  = nullptr;
		size_t m_count	  = 0;
		size_t m_capacity = 0;
	};

	I32F LookupTableIndex(const NavNodeKey& k) const;

	PathFindParams::CostMode m_costMode;
	FlatTable m_flatTable;

	typedef RobinHoodHashTable<NavNodeKey, OpenNodeDataPair> OpenHash;
	OpenHash m_hashTable;
	size_t m_countHw;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void OpenList::Init(size_t maxSize,
					PathFindParams::CostMode mode,
					const char* sourceFile,
					U32F sourceLine,
					const char* sourceFunc)
{
	m_countHw = 0;
	m_costMode = mode;

	switch (m_costMode)
	{
	case PathFindParams::CostMode::kRobinHood:
		{
			m_hashTable.Init(maxSize, FILE_LINE_FUNC);
		}
		break;

	default:
		{
			m_flatTable.m_pCostArray = NDI_NEW(kAlign128) OpenCostType[maxSize];
			m_flatTable.m_pKeyList = (OpenListKey*)(NDI_NEW(kAlign128) U8[sizeof(OpenListKey) * maxSize]);
			m_flatTable.m_pDataList = (OpenNodeData*)(NDI_NEW(kAlign128) U8[sizeof(OpenNodeData) * maxSize]);
			m_flatTable.m_capacity = maxSize;
			m_flatTable.m_count = 0;
		}
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F OpenList::LookupTableIndex(const NavNodeKey& k) const
{
	for (size_t i = 0; i < m_flatTable.m_count; ++i)
	{
		if (m_flatTable.m_pKeyList[i] == k)
		{
			return i;
		}
	}

	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void OpenList::TryUpdateCost(const NavNodeKey& k, const OpenNodeData& d)
{
	//PROFILE_AUTO(Navigation);
	PROFILE_ACCUM(TryUpdateCost);

	NAV_ASSERT(g_navPathNodeMgr.GetNode(k.GetPathNodeId()).GetNavMeshHandle() == d.m_pathNode.GetNavMeshHandle());

	const OpenCostType combinedCost = d.m_fromCost + d.m_toCost;

	switch (m_costMode)
	{
	case PathFindParams::CostMode::kRobinHood:
		{
			OpenHash::Iterator itr = m_hashTable.Find(k);
			if (itr != m_hashTable.End())
			{
				OpenNodeDataPair& existingPair = itr->m_value;

				if (d.m_fromCost < existingPair.m_data.m_fromCost)
				{
					existingPair.m_data = d;
					existingPair.m_cost = combinedCost;
				}
			}
			else
			{
				OpenNodeDataPair pair;
				pair.m_data = d;
				pair.m_cost = combinedCost;
				m_hashTable.Set(k, pair);

				m_countHw = Max(m_countHw, m_hashTable.Size());
			}
		}
		break;
	default:
		{
			const I32F tableIndex = LookupTableIndex(k);

			if (tableIndex < 0)
			{
				NAV_ASSERTF(m_flatTable.m_count < m_flatTable.m_capacity,
							("Overflowed A* open list (more than %d nodes)", m_flatTable.m_capacity));

				if (m_flatTable.m_count < m_flatTable.m_capacity)
				{
					m_flatTable.m_pCostArray[m_flatTable.m_count] = combinedCost;
					m_flatTable.m_pKeyList[m_flatTable.m_count] = k;
					m_flatTable.m_pDataList[m_flatTable.m_count] = d;
					m_flatTable.m_count++;
					m_countHw = Max(m_countHw, m_flatTable.m_count);
				}
			}
			else
			{
				NAV_ASSERT(tableIndex < m_flatTable.m_capacity);
				NAV_ASSERT(m_flatTable.m_pKeyList[tableIndex] == k);

				OpenNodeData& existingData = m_flatTable.m_pDataList[tableIndex];

				if (d.m_fromCost < existingData.m_fromCost)
				{
					m_flatTable.m_pCostArray[tableIndex] = combinedCost;
					existingData = d;
				}
			}
		}
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool OpenList::RemoveBestNode(NavNodeKey* pNodeOut, OpenNodeData* pDataOut)
{
	//PROFILE_AUTO(Navigation);
	PROFILE_ACCUM(RemoveBestNode);

	OpenCostType bestCost = kLargeFloat;
	I32F bestIndex = -1;
	bool found = false;

	OpenNodeData d;
	NavNodeKey k;

#if USE_FAST_LINEAR_LOOKUP
	if (m_flatTable.m_count)
	{
		bestIndex = FastMinFloatArray_CautionOverscan(m_flatTable.m_pCostArray, m_flatTable.m_count, bestCost);
	}

	if (bestIndex >= 0)
	{
		k = m_flatTable.m_pKeyList[bestIndex].AsNavNodeKey();
		d = m_flatTable.m_pDataList[bestIndex];

		const size_t lastIndex = m_flatTable.m_count - 1;

		m_flatTable.m_pCostArray[bestIndex] = m_flatTable.m_pCostArray[lastIndex];

		m_flatTable.m_pKeyList[bestIndex] = m_flatTable.m_pKeyList[lastIndex];
		m_flatTable.m_pDataList[bestIndex] = m_flatTable.m_pDataList[lastIndex];

		--m_flatTable.m_count;

		found = true;
	}
#else
	switch (m_costMode)
	{
	case PathFindParams::CostMode::kDefault:
		{
			for (size_t i = 0; i < m_flatTable.m_count; ++i)
			{
				const OpenCostType cost = m_flatTable.m_pCostArray[i];

				if (cost < bestCost)
				{
					bestCost = cost;
					bestIndex = i;
				}
			}

			if (bestIndex >= 0)
			{
				k = m_flatTable.m_pKeyList[bestIndex].AsNavNodeKey();
				d = m_flatTable.m_pDataList[bestIndex];

				const size_t lastIndex = m_flatTable.m_count - 1;

				m_flatTable.m_pCostArray[bestIndex] = m_flatTable.m_pCostArray[lastIndex];

				m_flatTable.m_pKeyList[bestIndex] = m_flatTable.m_pKeyList[lastIndex];
				m_flatTable.m_pDataList[bestIndex] = m_flatTable.m_pDataList[lastIndex];

				--m_flatTable.m_count;

				found = true;
			}
		}
		break;

	case PathFindParams::CostMode::kAgnostic:
		{
			if (m_flatTable.m_count > 0)
			{
				bestIndex = m_flatTable.m_count - 1;

				k = m_flatTable.m_pKeyList[bestIndex].AsNavNodeKey();
				d = m_flatTable.m_pDataList[bestIndex];

				--m_flatTable.m_count;
				found = true;
			}
		}
		break;

	case PathFindParams::CostMode::kRobinHood:
		{
			OpenHash::Iterator itrEnd = m_hashTable.End();
			OpenHash::Iterator itrBest = itrEnd;

			for (OpenHash::Iterator itr = m_hashTable.Begin(); itr != m_hashTable.End(); itr++)
			{
				const OpenNodeDataPair& pair = itr->m_value;
				if (pair.m_cost < bestCost)
				{
					bestCost = pair.m_cost;
					itrBest = itr;
				}
			}

			if (itrBest != itrEnd)
			{
				k = itrBest->m_key;
				d = itrBest->m_value.m_data;

				m_hashTable.Erase(itrBest);

				found = true;
			}
		}
		break;
	}
#endif

	if (found)
	{
		if (pNodeOut)
			*pNodeOut = k;

		if (pDataOut)
			*pDataOut = d;
	}

	return found;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool OpenList::Remove(const NavNodeKey& key, OpenNodeData* pDataOut /* = nullptr */)
{
	switch (m_costMode)
	{
	case PathFindParams::CostMode::kRobinHood:
		{
			OpenHash::Iterator itr = m_hashTable.Find(key);
			if (itr != m_hashTable.End())
			{
				m_hashTable.Erase(itr);
				return true;
			}

			return false;
		}
		break;

	default:
		{
			const I32F foundIndex = LookupTableIndex(key);

			if (foundIndex < 0)
				return false;

			const size_t lastIndex = m_flatTable.m_count - 1;

			if (pDataOut)
			{
				*pDataOut = m_flatTable.m_pDataList[foundIndex];
			}

			m_flatTable.m_pKeyList[foundIndex]	 = m_flatTable.m_pKeyList[lastIndex];
			m_flatTable.m_pDataList[foundIndex]	 = m_flatTable.m_pDataList[lastIndex];
			m_flatTable.m_pCostArray[foundIndex] = m_flatTable.m_pCostArray[lastIndex];

			m_flatTable.m_count--;

			return true;
		}
		break;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool OpenList::GetData(const NavNodeKey& key, OpenNodeData* pDataOut) const
{
	const I32F foundIndex = LookupTableIndex(key);

	if (foundIndex < 0)
		return false;

	if (pDataOut)
	{
		*pDataOut = m_flatTable.m_pDataList[foundIndex];
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct PathFindData
{
	PathFindData(OpenList& openList,
				 NavNodeTable* const pClosedList,
				 const PathFindLocation* pGoals,
				 U32F numGoals,
				 const PathFindContext& pathContext);

	OpenList& m_openList;
	NavNodeTable* m_pClosedList;

	NavNodeKey m_aGoalNodes[kMaxPathFindGoals];
	PathFindGoalBits m_remainingGoalBits;
};

/************************************************************************/
/* Local function prototypes                                            */
/************************************************************************/

/// --------------------------------------------------------------------------------------------------------------- ///
// NavPathFind functions
static const NavPoly* GetNavPolyFromPathNode(const NavMesh* pMesh, const NavPathNode* pNode);
static Point GetAdjacentNodePosPs(const NavPathNode* pCurNode,
								  const NavMesh* pCurNavMesh,
								  const NavPathNode* pAdjNode,
								  const NavPathNode::Link& link,
								  Point_arg curNodePosPs,
								  Point_arg goalNodePosPs);
static bool DynamicLinkAvailable(const NavPathNode* pCurNode,
								 const NavPathNode::Link& link,
								 const NavPathNode::Link* pPathLinks);
static bool DynamicExpansionAllowed(const NavPathNode* pNode, const NavBlockerBits& obeyedBlockers);

static bool IsLineBlockedByGaps(const PathFindParams& pathParams,
								const NavMesh* pNavMesh,
								const NavMeshGapRef* pGapList,
								Point_arg startLs,
								Point_arg endLs);

static bool IsLineBlockedByExGaps(const PathFindParams& pathParams,
								  const NavMesh* pNavMesh,
								  Point_arg prevPosLs,
								  Point_arg nextPosLs,
								  const NavMeshGapEx* pExGaps,
								  const U32F numGaps);

static bool RadialExpansionAllowed(const NavMesh* pNavMesh,
								   const NavPathNode* pPrevNode,
								   const NavPathNode* pNextNode,
								   const PathFindParams& pathParams,
								   Point_arg prevPosPs,
								   Point_arg nextPosPs,
								   const NavPathNode::Link& link,
								   NavMeshGapEx* pExGapBuffer,
								   size_t exGapBufferSize);

static bool IsGoalSatisfied(const NavNodeKey& curNode,
							Point_arg curPosPs,
							const NavNodeKey& goalNode,
							const PathFindLocation& goalLocation,
							const PathFindParams& pathParams,
							NavMeshGapEx* pExGapBuffer,
							size_t exGapBufferSize);

static bool TrySatisfyGoal(PathFindData& pfData,
						   const PathFindLocation* aGoalLocations,
						   const NavNodeKey& curNode,
						   const NavNodeData& curData,
						   const PathFindParams& pathParams,
						   PathFindResults& results,
						   NavMeshGapEx* pExGapBuffer,
						   size_t exGapBufferSize);

static void DebugDrawNavNode(const NavNodeKey& key,
							 const NavNodeData& data,
							 const NavNodeData& parentData,
							 bool singularGoal,
							 Point_arg singularGoalPosPs,
							 PathFindNodeData& workData,
							 DebugPrimTime drawTime,
							 bool drawText);

/// --------------------------------------------------------------------------------------------------------------- ///
inline static F32 ComputeHeuristicCost(Point_arg nodePosPs, Point_arg goalPosPs, Scalar_arg cost)
{
	const F32 kHeuristicScale = 1.01f;
	const F32 toCost = Dist(nodePosPs, goalPosPs) * cost * kHeuristicScale;
	return toCost;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool GapLineTest(Point_arg p, Point_arg g0, Point_arg g1)
{
#if 1
	const Scalar ux = p.X() - g0.X();
	const Scalar uz = p.Z() - g0.Z();

	const Scalar vx = g1.X() - g0.X();
	const Scalar vz = g1.Z() - g0.Z();

	const Scalar crossY = (uz * vx) - (ux * vz);

	return crossY > SCALAR_LC(0.0f);
#else
	const Vec2 pV2 = Vec2(p.X(), p.Z());
	const Vec2 p0v2 = Vec2(g0.X(), g0.Z());
	const Vec2 p1v2 = Vec2(g1.X(), g1.Z());

	const TriangleWinding tw = ComputeTriangleWinding(p0v2, pV2, p1v2);

	return (tw != TriangleWinding::kWindingCW);
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
U64 ComputePartitionValueFromGaps(const PathFindContext& pathContext,
								  const NavMesh* pMesh,
								  const NavPoly* pPoly,
								  Point_arg posLs)
{
	if ((pathContext.m_pathRadius <= 0.0f) || !pMesh || !pPoly)
		return 0;

	//PROFILE_ACCUM(ComputePartitionValueFromGaps);

	const float maxGapDiameter = pathContext.m_pathRadius * 2.0f;

	const NavMeshGapRef* pGapList = pPoly->GetGapList();

	U64 val = 0;
	U64 iGap = 0;
	const U32F numGaps = pMesh->GetNumGaps();

	for (const NavMeshGapRef* pGapRef = pGapList; (pGapRef && pGapRef->m_pGap); ++pGapRef)
	{
		const NavMeshGap* pGap = pGapRef->m_pGap;

		if ((!pGap->m_enabled0) || (!pGap->m_enabled1))
			continue;

		if (pGap->m_gapDist > maxGapDiameter)
			continue;

		const Point p0 = pGap->m_pos0;
		const Point p1 = pGap->m_pos1;

		if (GapLineTest(posLs, p0, p1))
		{
			val |= (1ULL << iGap);
		}

		++iGap;
	}

	if (pathContext.m_dynamicSearch && pPoly->HasExGaps())
	{
		//PROFILE_ACCUM(ComputePartitionValueFromGaps_Ex);

		NavMeshGapEx exGaps[16];
		const size_t numExGaps = g_navExData.GatherExGapsForPolyUnsafe(pPoly->GetNavManagerId(), exGaps, 16);
		NavBlockerBits matchingBits;

		for (U32F iExGap = 0; iExGap < numExGaps; ++iExGap)
		{
			const NavMeshGapEx& exGap = exGaps[iExGap];

			if (exGap.m_gapDist > maxGapDiameter)
				continue;

			NavBlockerBits::BitwiseAnd(&matchingBits, exGap.m_blockerBits, pathContext.m_obeyedBlockers);

			if (matchingBits.AreAllBitsClear())
				continue;

			if (GapLineTest(posLs, exGap.m_pos0Ls, exGap.m_pos1Ls))
			{
				val |= (1ULL << iGap);
			}

			++iGap;
		}
	}

	return val;
}

/************************************************************************/
/* Publicly Defined Functions                                           */
/************************************************************************/

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathFindLocation::Create(const PathFindParams& pathParams,
							  const NavLocation& inputLocation,
							  NavMeshHandle hFallbackMesh /* = NavMeshHandle() */)
{
	*this = PathFindLocation();

	const Point origPosWs = inputLocation.GetPosWs();
	const PathFindContext& pathContext = pathParams.m_context;

	NAV_ASSERT(IsReasonable(origPosWs));
	if (FALSE_IN_FINAL_BUILD(!IsReasonable(origPosWs)))
	{
		return false;
	}

#if ENABLE_NAV_LEDGES
	if (inputLocation.GetType() == NavLocation::Type::kNavLedge)
	{
		m_pNavLedge = inputLocation.ToNavLedge();

		if (m_pNavLedge)
		{
			m_pathNodeId = inputLocation.GetPathNodeId();
			const NavLedgeGraph* pGraph = m_pNavLedge->GetNavLedgeGraphHandle().ToLedgeGraph();
			const Point inputPosLs = pGraph->WorldToLocal(origPosWs);

			Point ledgePosLs = inputPosLs;
			DistPointSegment(inputPosLs, m_pNavLedge->GetVertex0Ls(), m_pNavLedge->GetVertex1Ls(), &ledgePosLs);
			m_posWs = pGraph->LocalToWorld(ledgePosLs);
		}

		return m_pNavLedge != nullptr;
	}
#endif

	const NavPoly* pPoly = inputLocation.ToNavPoly();
	const NavMeshMgr& navMeshMgr = *EngineComponents::GetNavMeshMgr();

	Point posWs = origPosWs;

	if (pPoly)
	{
		const NavMesh* pMesh = pPoly->GetNavMesh();
		const Point origPosLs = pMesh->WorldToLocal(origPosWs);
		if (!pPoly->PolyContainsPointLs(origPosLs))
		{
			pPoly = nullptr;
		}
	}

	if (!pPoly)
	{
		const F32 yThreshold = pathParams.m_findPolyYThreshold;

		if (const NavMesh* pMesh = inputLocation.ToNavMesh())
		{
			const Point posPs = pMesh->WorldToParent(origPosWs);
			pPoly = pMesh->FindContainingPolyPs(posPs, yThreshold);

			NAV_ASSERT(!pPoly || pPoly->PolyContainsPointLs(pMesh->WorldToLocal(origPosWs), 0.1f));
		}

		if (!pPoly)
		{
			if (const NavMesh* pFallbackMesh = hFallbackMesh.ToNavMesh())
			{
				const Point posPs = pFallbackMesh->WorldToParent(origPosWs);
				pPoly = pFallbackMesh->FindContainingPolyPs(posPs, yThreshold);

				NAV_ASSERT(!pPoly || pPoly->PolyContainsPointLs(pFallbackMesh->WorldToLocal(origPosWs), 0.1f));
			}
		}

		if (!pPoly)
		{
			PROFILE(AI, FindMesh);

			FindBestNavMeshParams navMeshParams;
			navMeshParams.m_pointWs = origPosWs;
			navMeshParams.m_cullDist = pathParams.m_findPolyXZCullRadius;
			navMeshParams.m_yThreshold = yThreshold;
			navMeshParams.m_bindSpawnerNameId = pathParams.m_bindSpawnerNameId;
			navMeshParams.m_obeyedStaticBlockers = pathParams.m_context.m_obeyedStaticBlockers;

			navMeshMgr.FindNavMeshWs(&navMeshParams);

			if (navMeshParams.m_pNavPoly)
			{
				posWs = navMeshParams.m_nearestPointWs;
				pPoly = navMeshParams.m_pNavPoly;

				NAV_ASSERT(pPoly->PolyContainsPointLs(navMeshParams.m_pNavMesh->WorldToLocal(navMeshParams.m_nearestPointWs), 0.1f));
			}
		}

		if (pPoly && pPoly->IsLink())
		{
			// if poly is link poly, try to switch nav meshes and find
			// a non-link poly because we're going to ignore link polys
			// during the search
			pPoly = pPoly->GetLinkedPoly();
		}
	}

	if (pPoly && !pPoly->IsValid())
	{
		pPoly = nullptr;
	}

	const NavMesh* pPolyMesh = pPoly ? pPoly->GetNavMesh() : nullptr;
	const NavPolyEx* pPolyEx = nullptr;

	if (pPoly && pathContext.m_dynamicSearch)
	{
		const Point posLs = pPolyMesh->WorldToLocal(posWs);
		pPolyEx = pPoly->FindContainingPolyExLs(posLs);
	}

	const bool shouldSearchForPos = (pPolyEx && pPolyEx->IsBlockedBy(pathContext.m_obeyedBlockers))
									|| (pPoly && pathContext.m_pathRadius >= kSmallestFloat);

	if (shouldSearchForPos)
	{
		NAV_ASSERT(pPolyMesh);

		NavMesh::FindPointParams params;
		params.m_point = pPolyMesh->WorldToLocal(posWs);
		params.m_dynamicProbe = pathContext.m_dynamicSearch;
		params.m_obeyedStaticBlockers = pathContext.m_obeyedStaticBlockers;
		params.m_obeyedBlockers = pathContext.m_obeyedBlockers;
		params.m_searchRadius = (pathContext.m_pathRadius * 2.0f) + 0.5f;
		params.m_depenRadius = pathContext.m_pathRadius;
		params.m_crossLinks = true;

		pPolyMesh->FindNearestPointLs(&params);

		pPoly = params.m_pPoly;
		pPolyEx = params.m_pPolyEx;

		const Point posLs = params.m_nearestPoint;
		posWs = pPoly ? pPolyMesh->LocalToWorld(posLs) : origPosWs;

		if (pPoly)
		{
			pPolyMesh = pPoly->GetNavMesh();
		}

#if JBELLOMY
		if (pPoly && !pPoly->PolyContainsPointLs(pPolyMesh->WorldToLocal(posWs), 0.1f))
		{
			g_prim.Draw(DebugArrow(origPosWs, posWs, kColorRed, 0.5f, kPrimEnableHiddenLineAlpha), Seconds(30.0f));
			g_prim.Draw(DebugCross(posWs, 0.1f, kColorRed, kPrimEnableHiddenLineAlpha), Seconds(30.0f));
			pPoly->DebugDrawEdges(kColorWhiteTrans, kColorWhiteTrans, 0.01f, 3.0f, Seconds(30.0f));

			g_ndConfig.m_pDMenuMgr->SetProgPauseLock(true);
			g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchInput = true;
			g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchProcessing = true;
		}
#endif

		NAV_ASSERT(!pPoly || pPoly->PolyContainsPointLs(pPolyMesh->WorldToLocal(posWs), 0.1f));
		NAV_ASSERT(!pPolyEx || pPolyEx->PolyContainsPointLs(pPolyMesh->WorldToLocal(posWs), nullptr, 0.1f));
	}

	if (!pPoly)
	{
#if ENABLE_NAV_LEDGES
		FindNavLedgeGraphParams findLedgeParams;
		findLedgeParams.m_pointWs = origPosWs;
		findLedgeParams.m_searchRadius = pathParams.m_findPolyXZCullRadius;

		NavLedgeGraphMgr::Get().FindLedgeGraph(&findLedgeParams);

		if (findLedgeParams.m_pNavLedge)
		{
			m_pathNodeId = findLedgeParams.m_pNavLedge->GetPathNodeId();
			m_pNavLedge = findLedgeParams.m_pNavLedge;

			const NavLedgeGraph* pGraph = findLedgeParams.m_pNavLedge->GetNavLedgeGraphHandle().ToLedgeGraph();
			const Point inputPosLs = pGraph->WorldToLocal(origPosWs);

			Point ledgePosLs = inputPosLs;
			DistPointSegment(inputPosLs,
							 findLedgeParams.m_pNavLedge->GetVertex0Ls(),
							 findLedgeParams.m_pNavLedge->GetVertex1Ls(),
							 &ledgePosLs);
			m_posWs = pGraph->LocalToWorld(ledgePosLs);

			return true;
		}
#endif
	}

	m_pNavPoly = pPoly;
	m_pNavPolyEx = pPolyEx;
	m_pathNodeId = NavPathNodeMgr::kInvalidNodeId;
	m_posWs = posWs;

	if (m_pNavPolyEx)
	{
		m_pathNodeId = m_pNavPolyEx->GetPathNodeId();
	}
	else if (m_pNavPoly)
	{
		m_pathNodeId = m_pNavPoly->GetPathNodeId();
	}

	if (pPoly)
	{
		NAV_ASSERT(pPolyMesh == pPoly->GetNavMesh());

		const Point posLs = pPolyMesh->WorldToLocal(posWs);

		if (!pPoly->PolyContainsPointLs(posLs, 0.1f))
		{
			Nav::DebugPrintPathFindParams(pathParams, kMsgOut);

			NAV_ASSERTF(false, ("Nav::PathFindLocation::Create() has returned a poly which doesn't properly contain the search point. Input pos: %s Determined pos: %s Reached Poly: 0x%x", PrettyPrint(origPosWs), PrettyPrint(posWs), pPoly->GetNavManagerId().AsU64()));
			*this = PathFindLocation();
			m_posWs = origPosWs;

#ifdef JBELLOMY
			g_ndConfig.m_pDMenuMgr->SetProgPauseLock(true);
			g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchInput = true;
			g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchProcessing = true;
#endif
			m_pNavPoly = nullptr;
			m_pNavPolyEx = nullptr;
		}
	}

	return m_pNavPoly != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsPreferredNode(const NavPathNode::NodeId* pPref, const NavPathNode::NodeId node, const U32 numNodes)
{
	for (U32 i = 0; i < numNodes; ++i)
	{
		if (pPref[i] == node)
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <bool useTrivialHashTable>
PathFindResults DoPathFind(const PathFindParams& pathParams,
						   PathFindNodeData& workData,
						   const PathFindLocation* pStartLocations,
						   const U32F numStartLocations,
						   const PathFindLocation* pGoalLocations,
						   const U32F numGoalLocations)
{
	PROFILE_AUTO(AI);
	PROFILE_ACCUM(DoPathFind);

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	const PathFindContext& pathContext = pathParams.m_context;

	PathFindResults ret;
	ret.m_reachedGoals.ClearAllBits();
	ret.m_overflowedClosedList = false;

	if (0 == numStartLocations)
	{
		return ret;
	}

	// how many nodes from a previous path to cost down for hysteresis
	//
	// fewer nodes is faster and also more relevant, in the sense that we only
	// really care about avoiding path changes immediately in front of the NavChar, not far ahead of them.
	// so it's not ideal to cost down faraway nodes just because they happen to be part of the previous pathfind.
	//
	// however, if we only cost down one node, say, by 2 meters, say, we can run into trouble: we can't permit
	// negative cost, so what if that node is only 0.1 m long? We will lower its cost to 0, and end up only
	// adding 0.1 m of incentive instead of the desired 2.0 m.
	//
	// as a compromise, we select a small number of nodes, 2 or 3 works well, and distribute the incentive among them.
	//
	// nodes instead of polys as we will be dealing with nodes inside the hot loop
	//
	CONST_EXPR U32 kMaxPreferredNodes = 3;
	NavPathNode::NodeId preferredNodes[kMaxPreferredNodes];

	// initialize to invalid
	memset(preferredNodes, -1, sizeof(NavPathNode::NodeId) * kMaxPreferredNodes);

	PathFindNodeData::FnShouldExpand pFnShouldExpand = workData.m_pFnShouldExpand;

/*
	if (!pFnShouldExpand)
	{
		pFnShouldExpand = AlwaysExpand;
	}
*/

	OpenList openList;
	openList.Init(2048, pathParams.m_costMode, FILE_LINE_FUNC);
	// NB: RobinHoodHash, as currently checked in, has performance degredation that scales with allocated size :|
	//openList.Init(256, pathParams.m_costMode, FILE_LINE_FUNC);

	TrivialHashNavNodeData* const pTrivialHashTable = workData.m_trivialHashVisitedNodes;

#ifndef FINAL_BUILD
	if (useTrivialHashTable)
	{
		// if we have a trivial hash, we shouldn't have a closed list
		ALWAYS_ASSERT(!workData.m_visitedNodes.IsInitialized());

		// verify that trivial hash table is empty and ready to go
		for (int i = 0; i < NavPathNodeMgr::kMaxNodeCount; ++i)
		{
			const TrivialHashNavNodeData& data = pTrivialHashTable[i];
			ALWAYS_ASSERT(!data.IsValid());
		}
	}
#endif

	NavNodeTable localClosedList;
	NavNodeTable* const pClosedList = useTrivialHashTable
										  ? nullptr
										  : (workData.m_visitedNodes.IsInitialized() ? &workData.m_visitedNodes
																					 : &localClosedList);
	if (!useTrivialHashTable)
	{
		if (!pClosedList->IsInitialized())
		{
			pClosedList->Init(2048, FILE_LINE_FUNC);
		}
	}

	static const size_t kExGapBufferSize = 128;
	NavMeshGapEx* pExGapBuffer = NDI_NEW NavMeshGapEx[kExGapBufferSize];

	const float pathDiameter = 2.0f * pathContext.m_pathRadius;

	PathFindData pfData = PathFindData(openList, pClosedList, pGoalLocations, numGoalLocations, pathContext);

	if (pfData.m_remainingGoalBits.AreAllBitsClear() && (numGoalLocations > 0))
	{
		return ret;
	}

	const F32 maxTravelDist = (pathParams.m_maxTravelDist >= 0.0f) ? pathParams.m_maxTravelDist : NDI_FLT_MAX;
	const F32 maxExpansionRadius = pathParams.m_maxExpansionRadius;
	const NavPathNode::LinkType ignoreLinkType = pathParams.m_context.m_reverseSearch ? NavPathNode::kLinkTypeOutgoing
																					  : NavPathNode::kLinkTypeIncoming;

	NavPathNodeMgr* pPathNodeMgr = &g_navPathNodeMgr;

	const NavPathNode::Link* pathLinks = &pPathNodeMgr->GetLink(0);
	const NavPathNode* pathNodes = &pPathNodeMgr->GetNode(0);

	const bool singularGoal = (numGoalLocations == 1);
	const Point singularGoalPosWs = singularGoal ? pGoalLocations[0].m_posWs : kOrigin;

	U32 numPreferredNodes = 0;

	// setup the start node
	for (U32F iStart = 0; iStart < numStartLocations; ++iStart)
	{
		const PathFindLocation& startLocation = pStartLocations[iStart];

		const NavPathNode* pStartNode = &pathNodes[startLocation.m_pathNodeId];
		const Point startPosPs = pathContext.m_parentSpace.UntransformPoint(startLocation.m_posWs);

		const NavNodeKey startNode = NavNodeKey(startLocation, pathContext);

		OpenNodeData startData;
		startData.m_parentNode = NavNodeKey(NavPathNodeMgr::kInvalidNodeId, 0);
		startData.m_fromDist = 0.0f;
		startData.m_fromCost = startLocation.m_baseCost;
		startData.m_toCost = 0.0f;
		startData.m_fringeNode = false;
		startData.m_pathNode.Init(*pStartNode);
		startData.m_pNavMesh = startLocation.m_pNavPoly ? startLocation.m_pNavPoly->GetNavMesh() : nullptr;
#if ENABLE_NAV_LEDGES
		startData.m_pLedgeGraph = startLocation.m_pNavLedge ? startLocation.m_pNavLedge->GetNavLedgeGraph() : nullptr;
#endif

		// move the start and goal nodes to the start and goal positions. this
		// is to avoid needing to visit the positions of the start and goal
		// nodes (we should go straight from start position to the neighbor
		// nodes). the extra step would be removed by the path smoothing anyway
		// but this should lead to better routes.
		startData.m_pathNodePosPs = startPosPs;

		// prepare our array of up to kMaxPreferredNodes preferred nodes based on the first start position
		// important to eliminate nodes the NavChar has already passed so we don't incent nodes behind us - that's
		// the whole thing we're trying to avoid with path hysteresis!
		//
		// note that if the start poly does not match ANY of the polys in the cached path, NO nodes will be preferred.
		// this is intentional, as in that case we cannot be sure where we stand relative to the cached path, and so
		// it's safest to not incent anything.
		if (iStart == 0)
		{

#if defined(DEBUG_PATH_HYSTERESIS) && !defined(FINAL_BUILD)
			{
				const NavPathNode& nonExStartNode = pPathNodeMgr->GetNode(startLocation.m_pNavPoly->GetPathNodeId());
				g_prim.Draw(DebugSphere(nonExStartNode.ParentToWorld(nonExStartNode.GetPositionPs()), 0.4f, kColorRed));
			}
#endif

			// look for a matching node...
			U32 i = 0;
			for (; i < BuildPathResults::kNumCachedPolys;)
			{
				if (pathParams.m_preferredPolys[i++] == startLocation.m_pNavPoly)
					break;
			}

			// ...then keep (up to) kMaxPreferredNodes nodes after it (not including it)
			for (; i < BuildPathResults::kNumCachedPolys && numPreferredNodes < kMaxPreferredNodes;)
			{
				if (const NavPoly* pPoly = pathParams.m_preferredPolys[i++].ToNavPoly())
					preferredNodes[numPreferredNodes++] = pPoly->GetPathNodeId();
			}
		}

		openList.TryUpdateCost(startNode, startData);
	}

#if defined(DEBUG_PATH_HYSTERESIS) && !defined(FINAL_BUILD)
	{
		printf("\n>>> PATH HYSTERESIS DEBUG <<<\nRaw preferred node IDs:\n");
		for (int i = 0; i < BuildPathResults::kNumCachedPolys; ++i)
		{
			if (pathParams.m_preferredPolys[i].ToNavPoly())
			{
				printf("%d ", pathParams.m_preferredPolys[i].ToNavPoly()->GetPathNodeId());
			}
			else
			{
				printf("--- ");
			}
		}
		printf("\n\nFinal preferred nodes after conditioning:\n");

		for (int i = 0; i < kMaxPreferredNodes; ++i)
		{
			if (preferredNodes[i] != NavPathNodeMgr::kInvalidNodeId)
			{
				printf("%d ", preferredNodes[i]);
			}
			else
			{
				printf("--- ");
			}
		}
		printf("\n>>> END PATH HYSTERESIS DEBUG <<<\n\n");
	}
#endif

	NavNodeKey curNode;
	OpenNodeData curData;
	while (openList.RemoveBestNode(&curNode, &curData) && !ret.m_overflowedClosedList)
	{
		if (!useTrivialHashTable)
		{
			if (pClosedList->IsFull())
			{
				//MsgAiWarn("Overflowed A* closed list (encountered more than %d nodes)\n", pClosedList->Size());
				ret.m_overflowedClosedList = true;
				break;
			}
		}

		NAV_ASSERT(pPathNodeMgr->GetNode(curNode.GetPathNodeId()).GetNavMeshHandle() == curData.m_pathNode.GetNavMeshHandle());

		NavNodeTable::Iterator curNodeIter;
		if (!useTrivialHashTable)
		{
			curNodeIter = pClosedList->Set(curNode, curData);
		}
		else
		{
			pTrivialHashTable[curNode.GetPathNodeId()] = curData;
		}

		const U32F iCurNode = curNode.GetPathNodeId();
		const NavPathNode* pCurNode = &pathNodes[iCurNode];
		const NavMesh* pCurNavMesh = curData.m_pNavMesh;
#if ENABLE_NAV_LEDGES
		const NavLedgeGraph* pCurLedgeGraph = curData.m_pLedgeGraph;
#endif

		NAV_ASSERTF(!pCurNode->IsCorrupted(), ("Path Node '%d' is corrupted", iCurNode));
		NAV_ASSERTF(pCurNode->IsValid(), ("Path Node '%d' is corrupted", iCurNode));

		const Point curNodePosPs = curData.m_pathNodePosPs;

		if (FALSE_IN_FINAL_BUILD(pathParams.m_debugDrawSearch))
		{
			OpenNodeData parentOpenData;
			NavNodeData parentData;

			NavNodeTable::ConstIterator citr;
			if (!useTrivialHashTable)
			{
				citr = pClosedList->Find(curData.m_parentNode);
			}
			bool valid = true;

			const TrivialHashNavNodeData* pParent = nullptr;
			if (useTrivialHashTable)
			{
				const NavPathNode::NodeId parentNodeId = curData.m_parentNode.GetPathNodeId();
				if (parentNodeId < NavPathNodeMgr::kMaxNodeCount)
				{
					const TrivialHashNavNodeData& parentNode = pTrivialHashTable[parentNodeId];
					if (parentNode.IsValid())
					{
						pParent = &parentNode;
					}
				}
			}

			if (useTrivialHashTable ? (!pParent) : (citr == pClosedList->End()))
			{
				valid = openList.GetData(curData.m_parentNode, &parentOpenData);
				parentData = parentOpenData;
			}
			else
			{
				if (useTrivialHashTable)
				{
					// populate as desired for debug draw
					parentData.m_pathNodePosPs = pParent->m_posPs;
				}
				else
				{
					parentData = citr->m_data;
				}
			}

			if (valid)
			{
				const Point singularGoalPosPs = pCurNavMesh ? pCurNavMesh->WorldToParent(singularGoalPosWs)
															: singularGoalPosWs;
				DebugDrawNavNode(curNode,
								 curData,
								 parentData,
								 singularGoal,
								 singularGoalPosPs,
								 workData,
								 pathParams.m_debugDrawTime,
								 g_navCharOptions.m_displaySinglePathfindText);
			}
		}

		const bool keepRunning = TrySatisfyGoal(pfData,
												pGoalLocations,
												curNode,
												curData,
												pathParams,
												ret,
												pExGapBuffer,
												kExGapBufferSize);
		if (!keepRunning)
			break;

		const bool curIsTap = pCurNode->IsActionPackNode();

		for (U32F iLink = pCurNode->GetFirstLinkId(); iLink != NavPathNodeMgr::kInvalidLinkId; iLink = pathLinks[iLink].m_nextLinkId)
		{
			NAV_ASSERT(iLink < pPathNodeMgr->GetMaxLinkCount());
			const NavPathNode::Link& link = pathLinks[iLink];

			if (link.m_type == ignoreLinkType)
				continue;

			const U32F iAdjNode = link.m_nodeId;
			NAV_ASSERT(iAdjNode < pPathNodeMgr->GetMaxNodeCount());

			if (useTrivialHashTable)
			{
				if (pTrivialHashTable[iAdjNode].IsValid())
					continue;
			}

			const NavPathNode* pAdjNode = &pathNodes[iAdjNode];

			NAV_ASSERTF(!pAdjNode->IsCorrupted(), ("Path Node '%d' is corrupted (link '%d' from '%d')", iAdjNode, iLink, iCurNode));
			NAV_ASSERTF(pAdjNode->IsValid(), ("Path Node '%d' is invalid (link '%d' from '%d')", iAdjNode, iLink, iCurNode));

			if (pAdjNode->IsBlocked(pathParams.m_context.m_obeyedStaticBlockers))
				continue;

			if (pathContext.m_dynamicSearch)
			{
				if (!DynamicExpansionAllowed(pAdjNode, pathContext.m_obeyedBlockers))
					continue;

				if (!link.IsDynamic() && DynamicLinkAvailable(pCurNode, link, pathLinks))
					continue;
			}
			else if (link.IsDynamic())
			{
				continue;
			}

			const NavMesh* pAdjNavMesh = (pCurNode->GetNavMeshHandle() == pAdjNode->GetNavMeshHandle())
											 ? pCurNavMesh
											 : pAdjNode->GetNavMeshHandle().ToNavMesh();
#if ENABLE_NAV_LEDGES
			const NavLedgeGraph* pAdjNavLedgeGraph = (pCurNode->GetNavLedgeHandle() == pAdjNode->GetNavLedgeHandle())
														 ? pCurLedgeGraph
														 : pAdjNode->GetNavLedgeHandle().ToLedgeGraph();
#endif

			if (!pAdjNavMesh
#if ENABLE_NAV_LEDGES
				&& !pAdjNavLedgeGraph
#endif
				)
				continue;

			if (pFnShouldExpand
				&& !pFnShouldExpand(pAdjNode,
									pAdjNavMesh,
#if ENABLE_NAV_LEDGES
									pAdjNavLedgeGraph,
#endif
									&pathParams))
			{
				if (!useTrivialHashTable)
					curNodeIter->m_data.m_fringeNode = true;
				continue;
			}

			const bool adjIsTap = pAdjNode->IsActionPackNode();
			const Point singularGoalPosPs = (singularGoal && pCurNavMesh)
												? pCurNavMesh->WorldToParent(singularGoalPosWs)
												: singularGoalPosWs;
			const Point goalNodePosPs = singularGoal ? singularGoalPosPs : curNodePosPs;
			const Point adjNodePosPs  = (adjIsTap || !singularGoal) ? Point(pAdjNode->GetPositionPs())
																   : GetAdjacentNodePosPs(pCurNode,
																						  pCurNavMesh,
																						  pAdjNode,
																						  link,
																						  curNodePosPs,
																						  goalNodePosPs);
			Point adjNodePosWs = adjNodePosPs;
			Point adjNodePosCurPs = adjNodePosPs;
			U64 partitionIndex = 0;

			if (pAdjNavMesh)
			{
				adjNodePosWs = pAdjNavMesh->ParentToWorld(adjNodePosPs);

				if (pathContext.m_pathRadius > 0.0f)
				{
					const Point adjNodePosLs = pAdjNavMesh->ParentToLocal(adjNodePosPs);
					const NavPoly* pAdjPoly = &pAdjNavMesh->GetPoly(pAdjNode->GetNavManagerId().m_iPoly);

					partitionIndex = ComputePartitionValueFromGaps(pathContext, pAdjNavMesh, pAdjPoly, adjNodePosLs);
				}
			}
#if ENABLE_NAV_LEDGES
			else if (pAdjNavLedgeGraph)
			{
				adjNodePosWs = pAdjNavLedgeGraph->ParentToWorld(adjNodePosPs);
			}
#endif

			const NavNodeKey adjNode = NavNodeKey(iAdjNode, partitionIndex);

			if (!useTrivialHashTable)
			{
				if (pClosedList->Find(adjNode) != pClosedList->End())
					continue;
			}

			const TraversalActionPack* pTap = adjIsTap ? static_cast<const TraversalActionPack*>(pAdjNode->GetActionPack()) : nullptr;

			const bool isPlayerBlocked = pTap && pTap->IsPlayerBlocked();

			if (adjIsTap)
			{
				if (FALSE_IN_FINAL_BUILD(!pTap && g_navPathNodeMgr.m_pDebugLog))
				{
					g_navPathNodeMgr.m_pDebugLog->Dump(GetMsgOutput(kMsgOut));
				}

				NAV_ASSERTF(pTap,
							("NavPathNode %d (%s) is missing it's TAP (mgr id: 0x%x)",
							 adjNode.GetPathNodeId(),
							 NavPathNode::GetNodeTypeStr(pAdjNode->GetNodeType()),
							 pAdjNode->GetActionPackHandle().GetMgrId()));

				if (!pTap)
					continue;

				if (pTap->ChangesParentSpaces())
				{
					adjNodePosCurPs = pCurNode->WorldToParent(adjNodePosWs);
				}

				const bool isFactionValid	  = pTap->IsFactionIdMaskValid(pathParams.m_factionMask);
				const bool isTensionModeValid = pTap->IsTensionModeValid(pathParams.m_tensionMode);
				const bool isSkillValid		  = pTap->IsTraversalSkillMaskValid(pathParams.m_traversalSkillMask);

				const bool isImpassableDueToPlayerBlockage = isPlayerBlocked && pathParams.m_playerBlockageCost == PlayerBlockageCost::kImpassable;

				if (!isFactionValid || !isTensionModeValid || !isSkillValid || isImpassableDueToPlayerBlockage)
				{
					// can't cross this TAP, mark it closed and carry on
					OpenNodeData adjNodeData;
					const bool removed = openList.Remove(adjNode, &adjNodeData);

					if (!useTrivialHashTable && pClosedList->IsFull())
					{
						ret.m_overflowedClosedList = true;
						break;
					}
					else if (removed)
					{
						if (useTrivialHashTable)
						{
							pTrivialHashTable[iAdjNode] = adjNodeData;
						}
						else
						{
							pClosedList->Set(adjNode, adjNodeData);
						}
					}
					else
					{
						NavNodeData newData;
						newData.m_parentNode = curNode;
						newData.m_fromCost = -1.0f;
						newData.m_fromDist = -1.0f;
						newData.m_pathNodePosPs = adjNodePosPs;
						newData.m_toCost = 0.0f;
						newData.m_fringeNode = false;
						newData.m_pathNode.Init(*pAdjNode);

						if (useTrivialHashTable)
						{
							pTrivialHashTable[iAdjNode] = newData;
						}
						else
						{
							pClosedList->Set(adjNode, newData);
						}
					}

					continue;
				}
			}

			if (maxExpansionRadius >= 0.0f)
			{
				bool stopExpansion = true;
				for (U32F iStart = 0; iStart < numStartLocations; ++iStart)
				{
					const PathFindLocation& startLocation = pStartLocations[iStart];
					const Point startPosWs = startLocation.m_posWs;

					if (Dist(adjNodePosWs, startPosWs) <= maxExpansionRadius)
					{
						stopExpansion = false;
						break;
					}

				}

				if (stopExpansion)
					continue;
			}

			if (!RadialExpansionAllowed(pCurNavMesh,
										pCurNode,
										pAdjNode,
										pathParams,
										curNodePosPs,
										adjNodePosCurPs,
										link,
										pExGapBuffer,
										kExGapBufferSize))
			{
				continue;
			}

			bool reject = false;
			F32 thisCost = workData.m_config.m_costFunc(pathContext,
														curData.m_pathNode,
														link,
														curNodePosPs,
														adjNodePosCurPs,
														&reject);

			if (reject)
				continue;

			if (IsPreferredNode(preferredNodes, pAdjNode->GetId(), numPreferredNodes))
			{
#if defined(DEBUG_PATH_HYSTERESIS) && !defined(FINAL_BUILD)
				{
					g_prim.Draw(DebugSphere(pAdjNode->ParentToWorld(pAdjNode->GetPositionPs()), 0.3f, kColorBlue));
				}
#endif
				CONST_EXPR float kPreferredNodeTotalCostDecrease = 2.7f;
				thisCost = Max(0.0f, thisCost - kPreferredNodeTotalCostDecrease / (float)numPreferredNodes);
			}

			F32 fromCost = curData.m_fromCost + thisCost;

			if (adjIsTap && (!curIsTap || (pAdjNode->GetActionPackHandle() != pCurNode->GetActionPackHandle())))
			{
				F32 preferredBias = 0.0f;

				if (pAdjNode->GetActionPackHandle() == pathParams.m_hPreferredTap)
				{
					preferredBias = pathParams.m_preferredTapBias;
				}

				if (!pathParams.m_ignoreNodeExtraCost)
				{
					fromCost += pAdjNode->GetExtraCost();
				}

				if (isPlayerBlocked)
				{
					switch (pathParams.m_playerBlockageCost)
					{
					case PlayerBlockageCost::kFree:
						// no extra cost
						break;
					case PlayerBlockageCost::kCheap:
						fromCost += 3.5f;
						break;
					case PlayerBlockageCost::kExpensive:
						fromCost += 16.0f;
						break;

					// should not be possible to get here
					case PlayerBlockageCost::kImpassable:
					default:
						AI_ASSERT(false);
						break;
					}
				}

				fromCost += preferredBias;
				fromCost = Max(0.0f, fromCost);
			}

			const F32 thisDist = CostFuncDistance(pathContext, curData.m_pathNode, link, curNodePosPs, adjNodePosCurPs, nullptr);
			const F32 fromDist = curData.m_fromDist + thisDist;

			if ((numGoalLocations == 0) && (fromDist >= maxTravelDist))
			{
				if (!useTrivialHashTable)
					curNodeIter->m_data.m_fringeNode = true;
				continue;
			}

			OpenNodeData newData;
			newData.m_parentNode = curNode;
			newData.m_fromCost = fromCost;
			newData.m_fromDist = fromDist;
			newData.m_pathNodePosPs = adjNodePosPs;
			newData.m_fringeNode = false;
			newData.m_pathNode.Init(*pAdjNode);
			newData.m_pNavMesh = pAdjNavMesh;
#if ENABLE_NAV_LEDGES
			newData.m_pLedgeGraph = pAdjNavLedgeGraph;
#endif

			if (singularGoal)
			{
				newData.m_toCost = ComputeHeuristicCost(adjNodePosWs, singularGoalPosWs, workData.m_config.m_zeroValue);
			}
			else
			{
				newData.m_toCost = 0.0f;
			}

			NAV_ASSERT(pPathNodeMgr->GetNode(adjNode.GetPathNodeId()).GetNavMeshHandle() == newData.m_pathNode.GetNavMeshHandle());

			openList.TryUpdateCost(adjNode, newData);
		}
	}

	if (FALSE_IN_FINAL_BUILD(pathParams.m_debugDrawSearch))
	{
		DebugDrawNodeTable(pClosedList, pTrivialHashTable, pathParams.m_debugDrawTime);

		const bool save = g_navCharOptions.m_displaySinglePathfindText;
		g_navCharOptions.m_displaySinglePathfindText = true;

		while (openList.RemoveBestNode(&curNode, &curData))
		{
			const U32F iCurNode			= curNode.GetPathNodeId();
			const NavPathNode* pCurNode = &pathNodes[iCurNode];
			const NavMesh* pCurNavMesh	= curData.m_pNavMesh;

			NAV_ASSERTF(!pCurNode->IsCorrupted(), ("Path Node '%d' is corrupted", iCurNode));
			NAV_ASSERTF(pCurNode->IsValid(), ("Path Node '%d' is corrupted", iCurNode));

			OpenNodeData parentOpenData;
			NavNodeData parentData;

			NavNodeTable::ConstIterator citr;
			if (!useTrivialHashTable)
			{
				citr = pClosedList->Find(curData.m_parentNode);
			}
			bool valid = true;

			const TrivialHashNavNodeData* pParent = nullptr;
			if (useTrivialHashTable)
			{
				const NavPathNode::NodeId parentNodeId = curData.m_parentNode.GetPathNodeId();
				if (parentNodeId < NavPathNodeMgr::kMaxNodeCount)
				{
					const TrivialHashNavNodeData& parentNode = pTrivialHashTable[parentNodeId];
					if (parentNode.IsValid())
					{
						pParent = &parentNode;
					}
				}
			}

			if (useTrivialHashTable ? (!pParent) : (citr == pClosedList->End()))
			{
				valid = openList.GetData(curData.m_parentNode, &parentOpenData);
				parentData = parentOpenData;
			}
			else
			{
				if (useTrivialHashTable)
				{
					// populate as desired for debug draw
					parentData.m_pathNodePosPs = pParent->m_posPs;
				}
				else
				{
					parentData = citr->m_data;
				}
			}

			if (valid)
			{
				const Point singularGoalPosPs = pCurNavMesh ? pCurNavMesh->WorldToParent(singularGoalPosWs) : singularGoalPosWs;

				DebugDrawNavNode(curNode,
								 curData,
								 parentData,
								 singularGoal,
								 singularGoalPosPs,
								 workData,
								 pathParams.m_debugDrawTime,
								 true);
			}
		}

		MsgCon("A*:\n");
		MsgCon("  num visited nodes: %d\n", pClosedList ? pClosedList->Size() : -1);
		MsgCon("  max open nodes:    %d\n", openList.GetCountHighWater());

		g_navCharOptions.m_displaySinglePathfindText = save;
	}

	//MsgCon("max open count: %d\n", openList.GetCountHighWater());

	return ret;
}

// explicitly instantiate both
template PathFindResults DoPathFind<true>(const PathFindParams&,
										  PathFindNodeData&,
										  const PathFindLocation*,
										  const U32F,
										  const PathFindLocation*,
										  const U32F);
template PathFindResults DoPathFind<false>(const PathFindParams&,
										   PathFindNodeData&,
										   const PathFindLocation*,
										   const U32F,
										   const PathFindLocation*,
										   const U32F);

/// --------------------------------------------------------------------------------------------------------------- ///
U32F GatherStartLocations(const PathFindParams& params,
						  PathFindLocation* aStartLocationsOut,
						  U32 maxStartLocations,
						  NavMeshHandle* pFirstStartNavMeshOut /*= nullptr*/)
{
	U32F numStartLocations = 0;

	for (U32F iStartInput = 0; iStartInput < params.m_numStartPositions && numStartLocations < maxStartLocations; ++iStartInput)
	{
		const NavLocation& startLocation = params.m_startPositions[iStartInput];

		PathFindLocation pfLocation;
		if (!pfLocation.Create(params, startLocation))
			continue;

		pfLocation.m_baseCost = params.m_startCosts[iStartInput];

		aStartLocationsOut[numStartLocations++] = pfLocation;

		if (pFirstStartNavMeshOut && !pFirstStartNavMeshOut->IsValid() && pfLocation.m_pNavPoly)
		{
			*pFirstStartNavMeshOut = pfLocation.m_pNavPoly->GetNavMeshHandle();
		}
	}

	return numStartLocations;
}

/// --------------------------------------------------------------------------------------------------------------- ///
extern bool g_hackPrintParams;

void FindSinglePath(const FindPathOwner& owner,
					const FindSinglePathParams& pathParams,
					FindSinglePathResults* pPathResults)
{
	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const bool needExposureLock =
		pathParams.m_context.m_pathCostTypeId == SID("buddy-combat") ||
		pathParams.m_context.m_pathCostTypeId == SID("sneak") ||
		pathParams.m_context.m_pathCostTypeId == SID("sneak-reduced-exposure") ||
		pathParams.m_context.m_pathCostTypeId == SID("buddy-follow") ||
		pathParams.m_context.m_pathCostTypeId == SID("buddy-lead");

	if (needExposureLock)
		g_exposureMapMgr.GetExposureMapLock()->AcquireReadLock(FILE_LINE_FUNC);

	PROFILE_ACCUM(FindSinglePath);

	PROFILE_PRINTF(AI, FindSinglePath,
		("(%s)->(%s) (%s)",
		PrettyPrint(pathParams.m_startPositions[0].GetPosWs()),
		PrettyPrint(pathParams.m_goal.GetPosWs()),
		DevKitOnly_StringIdToString(pathParams.m_context.m_pathCostTypeId)));

	PathFindLocation aStartLocations[PathFindParams::kMaxStartPositions];
	NavMeshHandle hFirstStartNavMesh = NavMeshHandle();

	const U32F numStartLocations = GatherStartLocations(pathParams,
														aStartLocations,
														PathFindParams::kMaxStartPositions,
														&hFirstStartNavMesh);

	PathFindLocation goalLocation;
	goalLocation.Create(pathParams, pathParams.m_goal, hFirstStartNavMesh);

	pPathResults->Clear();

	if ((numStartLocations > 0) && (goalLocation.m_pathNodeId != NavPathNodeMgr::kInvalidNodeId) && !g_navPathNodeMgr.Failed())
	{
		ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

		PathFindNodeData workData;

		workData.Init(pathParams);
		workData.m_visitedNodes.Init(2048, FILE_LINE_FUNC);

		NAV_ASSERT(!workData.m_trivialHashVisitedNodes);
		NAV_ASSERT(!owner.m_hOwnerGo.IsKindOf(SID("Buddy")) || pathParams.m_ignoreNodeExtraCost);

		const PathFindResults res = DoPathFind<false>(pathParams,
													  workData,
													  aStartLocations,
													  numStartLocations,
													  &goalLocation,
													  1);

		if (res.m_reachedGoals.IsBitSet(0) && !res.m_overflowedClosedList)
		{
			BuildPath(workData,
					  pathParams.m_buildParams,
					  res.m_goalNodes[0],
					  goalLocation.m_posWs,
					  &pPathResults->m_buildResults);

			if (g_hackPrintParams)
			{
				DebugPrintFindSinglePathParams(pathParams, kMsgOut);
				g_hackPrintParams = false;
			}

			pPathResults->m_buildResults.m_goalFound = pPathResults->m_buildResults.m_pathWaypointsPs.IsValid();
		}
	}

	if (needExposureLock)
		g_exposureMapMgr.GetExposureMapLock()->ReleaseReadLock(FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool TryAdvanceSplineStep(float& cur, float goal, float step, float max, bool looping)
{
	if ((goal > max) || (goal < 0.0f))
	{
		return false;
	}

	if (Abs(cur - goal) < NDI_FLT_EPSILON)
	{
		return false;
	}

	if (Sign(goal - cur) != Sign(goal - (cur + step)))
	{
		// trivially crossed our goal value
		cur = goal;
		return true;
	}

	float newArc = cur + step;

	if (looping)
	{
		if (newArc < 0.0f)
		{
			newArc = Max(max + step + cur, goal);
		}
		else if (newArc > max)
		{
			newArc = Min(newArc - max, goal);
		}
	}

	cur = Limit(newArc, 0.0f, max);

	if ((newArc < 0.0f) || (newArc > max))
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool TryAddNewSplinePathWaypoint(const Locator& pathSpace,
										Point_arg pos,
										const NavPathNodeProxy& pathNode,
										PathWaypointsEx& path,
										Vector& prevMoveDir)
{
	if (path.GetWaypointCount() < 2)
	{
		path.AddWaypoint(pos, pathNode);
		prevMoveDir = kZero;
		return true;
	}
	else if (path.IsFull())
	{
		return false;
	}

	const Point prevEnd = path.GetEndWaypoint();
	const Vector newMoveDir = AsUnitVectorXz(pos - prevEnd, kZero);

	bool added = false;
	static const Scalar kDotThreshold = 0.996194698f; // Cos(5 deg)

	if (Dot(newMoveDir, prevMoveDir) >= kDotThreshold)
	{
		path.UpdateEndNode(pathSpace, pos, pathNode);
	}
	else
	{
		path.AddWaypoint(pos, pathNode);
		prevMoveDir = newMoveDir;
	}

	return added;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void FindSplineTailGoalPath(const FindPathOwner& owner,
								   const FindSplinePathParams& pathParams,
								   FindSplinePathResults* pPathResults,
								   bool useEntire = false)
{
	if (!pathParams.m_tailGoalValid)
		return;

	PathWaypointsEx& pathResultsPs = pPathResults->m_buildResults.m_pathWaypointsPs;

	const NavLocation pathEndLoc = pathResultsPs.GetEndAsNavLocation(pathParams.m_context.m_parentSpace);
	const NavLocation tailGoalLoc = pathParams.m_tailGoal;

	const Point pathEndWs = pathEndLoc.GetPosWs();
	const Point goalWs = tailGoalLoc.GetPosWs();

	if ((DistXz(pathEndWs, goalWs) < pathParams.m_context.m_pathRadius)
		&& (tailGoalLoc.GetNavMeshHandle() == pathEndLoc.GetNavMeshHandle()))
	{
		return;
	}

	FindSinglePathParams singlePathParams(pathParams);

	singlePathParams.m_buildParams = pathParams.m_buildParams;
	singlePathParams.AddStartPosition(pathEndLoc);
	singlePathParams.m_goal = tailGoalLoc;
	singlePathParams.m_buildParams.m_debugDrawResults = false;
	singlePathParams.m_debugDrawSearch = false;
	singlePathParams.m_debugDrawResults = false;
	singlePathParams.m_findPolyXZCullRadius = Abs(pathParams.m_arcObstacleStep);

	FindSinglePathResults singlePathResults;
	FindSinglePath(owner, singlePathParams, &singlePathResults);

	if (singlePathResults.m_buildResults.m_goalFound)
	{
		if (useEntire)
		{
			pPathResults->m_buildResults = singlePathResults.m_buildResults;
		}
		else
		{
			pathResultsPs.RemoveEndWaypoint();
			const Point curEndPs = pathResultsPs.GetEndWaypoint();

			const PathWaypointsEx& tailPathPs = singlePathResults.m_buildResults.m_pathWaypointsPs;
			const I32F tailPathLen = tailPathPs.GetWaypointCount();

			I32F iStart = 0;
			while (iStart < tailPathLen && (DistXz(tailPathPs.GetWaypoint(iStart), curEndPs) < 0.1f))
			{
				++iStart;
			}

			for (I32F i = iStart; i < tailPathLen; ++i)
			{
				pathResultsPs.AddWaypoint(tailPathPs, i);
			}
		}
	}
	else
	{
		pPathResults->m_buildResults.m_goalFound = false;
		pPathResults->m_buildResults.m_pathWaypointsPs.Clear();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FindSplinePath(const FindPathOwner& owner,
					const FindSplinePathParams& pathParams,
					FindSplinePathResults* pPathResults)
{
	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	PROFILE_ACCUM(FindSplinePath);

	PROFILE_PRINTF(AI, FindSplinePath,
				   ("(%s)->(%s [%0.2f:%0.2f])",
				   PrettyPrint(pathParams.m_startPositions[0].GetPosWs()),
				   pathParams.m_pPathSpline ? pathParams.m_pPathSpline->GetBareName() : "<null>",
				   pathParams.m_arcStart,
				   pathParams.m_arcGoal));

	pPathResults->Clear();

	if (!pathParams.m_pPathSpline || g_navPathNodeMgr.Failed())
	{
		return;
	}

	const bool loopingSpline = pathParams.m_pPathSpline->IsLooped();
	const float maxArcLength = pathParams.m_pPathSpline->GetTotalArcLength();

	if (!loopingSpline && (Sign(pathParams.m_arcGoal - pathParams.m_arcStart) != Sign(pathParams.m_arcStep))
		&& (Abs(pathParams.m_arcGoal - pathParams.m_arcStart) > NDI_FLT_EPSILON))
	{
		// will never reach goal stepping in the wrong direction
		return;
	}

	NAV_ASSERT(pathParams.m_arcStart >= 0.0f && pathParams.m_arcStart <= maxArcLength);
	NAV_ASSERT(pathParams.m_arcGoal >= 0.0f && pathParams.m_arcGoal <= maxArcLength);
	NAV_ASSERT(IsReasonable(pathParams.m_arcStep));

	const Point splineStartPosWs = pathParams.m_pPathSpline->EvaluatePointAtArcLength(pathParams.m_arcStart);
	NavLocation inputStartLoc;
	inputStartLoc.SetWs(splineStartPosWs);
	PathFindLocation splineStartLocation;
	splineStartLocation.Create(pathParams, inputStartLoc, NavMeshHandle());

	FindSinglePathResults singlePathResults;

	Vector pathMoveDirPs = kZero;

	PathWaypointsEx& pathResultsPs = pPathResults->m_buildResults.m_pathWaypointsPs;

	if (FALSE_IN_FINAL_BUILD(pathParams.m_debugDrawSearch))
	{
		CatmullRom::DrawOptions splineDebug;
		splineDebug.m_duration = pathParams.m_debugDrawTime;
		pathParams.m_pPathSpline->Draw(&splineDebug);

		const Point splineGoalPosWs = pathParams.m_pPathSpline->EvaluatePointAtArcLength(pathParams.m_arcGoal);
		g_prim.Draw(DebugSphere(splineStartPosWs, 0.25f, kColorGreen), pathParams.m_debugDrawTime);
		g_prim.Draw(DebugSphere(splineGoalPosWs, 0.25f, kColorRed), pathParams.m_debugDrawTime);
	}

	if ((splineStartLocation.m_pathNodeId != NavPathNodeMgr::kInvalidNodeId) && (splineStartLocation.m_pNavPoly))
	{
		const F32 obstacleStep = Sign(pathParams.m_arcStep) * Abs(pathParams.m_arcObstacleStep);
		FindSinglePathParams singlePathParams(pathParams);

		singlePathParams.m_buildParams = pathParams.m_buildParams;
		singlePathParams.m_goal = inputStartLoc;
		singlePathParams.m_findPolyXZCullRadius = Abs(pathParams.m_arcObstacleStep);

		singlePathParams.m_debugDrawSearch = false;
		singlePathParams.m_debugDrawResults = false;
		//singlePathParams.m_debugDrawTime = Seconds(0.5f);
		//singlePathParams.m_buildParams.m_debugDrawRadialEdges = false;
		singlePathParams.m_buildParams.m_debugDrawResults = false;
		//singlePathParams.m_buildParams.m_debugDrawTime = Seconds(0.5f);
		//singlePathParams.m_buildParams.m_finalizePathWithProbes = false;

		const float distToSplineStart = DistXz(splineStartPosWs, pathParams.m_startPositions[0].GetPosWs());

		const bool splineAdvancementNeeded = (Abs(pathParams.m_arcStart - pathParams.m_arcGoal) >= NDI_FLT_EPSILON);
		const bool closeToSplineStart = (distToSplineStart < pathParams.m_onSplineRadius);

		NavLocation initialSpGoalLoc = pathParams.m_startPositions[0];
		const NavLocation splineStartLoc = splineStartLocation.GetAsNavLocation();

		if (closeToSplineStart && (initialSpGoalLoc.GetNavMeshHandle() == splineStartLoc.GetNavMeshHandle()))
		{
			initialSpGoalLoc = splineStartLoc;

			pPathResults->m_buildResults.Clear();
			pPathResults->m_buildResults.m_goalFound = true;

			if (splineAdvancementNeeded)
			{
				pathResultsPs.AddWaypoint(pathParams.m_startPositions[0]);
			}
			else
			{
				pathResultsPs.AddWaypoint(pathParams.m_startPositions[0]);
				pathResultsPs.AddWaypoint(initialSpGoalLoc);
			}
		}
		else
		{
			FindSinglePath(owner, singlePathParams, &singlePathResults);

			pPathResults->m_buildResults = singlePathResults.m_buildResults;

			if (!singlePathResults.m_buildResults.m_goalFound
				|| ((singlePathParams.m_buildParams.m_truncateAfterTap > 0) && singlePathResults.m_buildResults.m_hNavActionPack.IsValid())
				|| singlePathResults.m_buildResults.m_pathWaypointsPs.IsFull())
			{
				return;
			}

			initialSpGoalLoc = singlePathResults.m_buildResults.m_pathWaypointsPs.GetEndAsNavLocation(pathParams.m_context.m_parentSpace);
		}

		if (!splineAdvancementNeeded)
		{
			// no actual spline advancement logic needed
			FindSplineTailGoalPath(owner, pathParams, pPathResults, true);
			return;
		}

		I32 lastPointHint;
		const F32 totalArcLen = pathParams.m_pPathSpline->GetTotalArcLength();
		if (pathParams.m_arcStart >= totalArcLen)	// Special handling for end points
		{
			const U32F segCount = pathParams.m_pPathSpline->GetSegmentCount();
			const bool isLooped = pathParams.m_pPathSpline->IsLooped();
			const bool reverse = pathParams.m_arcStep < 0.0f;

			if (!isLooped ||
				reverse)
			{
				lastPointHint = segCount - 1;
			}
			else
			{
				lastPointHint = 0;
			}
		}
		else
		{
			const CatmullRom::LocalParameter param = pathParams.m_pPathSpline->ArcLengthToLocalParam(pathParams.m_arcStart);
			lastPointHint = param.m_iSegment;
		}

		Point prevPosWs = initialSpGoalLoc.GetPosWs();
		float arcCur = pathParams.m_pPathSpline->FindArcLengthClosestToPoint(prevPosWs,
																			&lastPointHint,
																			false,
																			1.0f);

		const NavMesh* pCurMesh = initialSpGoalLoc.ToNavMesh();

		if (!pCurMesh)
		{
			pPathResults->m_buildResults.m_goalFound = false;
			return;
		}

		NavMesh::ProbeParams probeParams;
		probeParams.m_pStartPoly   = initialSpGoalLoc.ToNavPoly();
		probeParams.m_pStartPolyEx = nullptr;
		probeParams.m_obeyedStaticBlockers = pathParams.m_context.m_obeyedStaticBlockers;
		probeParams.m_obeyedBlockers = pathParams.m_context.m_obeyedBlockers;
		probeParams.m_dynamicProbe = pathParams.m_context.m_dynamicSearch;
		probeParams.m_probeRadius = pathParams.m_context.m_pathRadius - 0.001f;
		bool first = true;

		while (TryAdvanceSplineStep(arcCur, pathParams.m_arcGoal, pathParams.m_arcStep, maxArcLength, loopingSpline))
		{
			if (pathResultsPs.IsFull())
			{
				break;
			}

			const Point curPosWs = pathParams.m_pPathSpline->EvaluatePointAtArcLength(arcCur);

			const Point prevPosLs = pCurMesh->WorldToLocal(prevPosWs);
			const Point curPosLs = pCurMesh->WorldToLocal(curPosWs);

			probeParams.m_start = prevPosLs;
			probeParams.m_move = curPosLs - probeParams.m_start;

			const NavMesh::ProbeResult probeResult = pCurMesh->ProbeLs(&probeParams);
			if (probeResult == NavMesh::ProbeResult::kReachedGoal)
			{
				if (first)
				{
					first = false;
				}

				const Point curPosPs = pathParams.m_context.m_parentSpace.UntransformPoint(curPosWs);
				NavPathNodeProxy newNode;
				newNode.Invalidate();

				if (probeParams.m_pReachedPolyEx)
				{
					newNode.m_navMgrId = probeParams.m_pReachedPolyEx->GetNavManagerId();
					newNode.m_nodeType = NavPathNode::kNodeTypePolyEx;
				}
				else if (probeParams.m_pReachedPoly)
				{
					newNode.m_navMgrId = probeParams.m_pReachedPoly->GetNavManagerId();
					newNode.m_nodeType = NavPathNode::kNodeTypePoly;
				}
				else
				{
					return;
				}

				TryAddNewSplinePathWaypoint(pathParams.m_context.m_parentSpace,
											curPosPs,
											newNode,
											pathResultsPs,
											pathMoveDirPs);

				probeParams.m_pStartPoly   = probeParams.m_pReachedPoly;
				probeParams.m_pStartPolyEx = probeParams.m_pReachedPolyEx;

				pCurMesh  = probeParams.m_pReachedPoly->GetNavMesh();
				prevPosWs = curPosWs;
			}
			else
			{
				pathMoveDirPs = kZero;

				TryAdvanceSplineStep(arcCur, pathParams.m_arcGoal, obstacleStep, maxArcLength, loopingSpline);

				if (first)
				{
					pPathResults->m_splineStartArc = arcCur;
					first = false;
				}

				const Point obstaclePosWs = pathParams.m_pPathSpline->EvaluatePointAtArcLength(arcCur);

				NavLocation goalNavLoc;
				goalNavLoc.SetWs(obstaclePosWs);
				PathFindLocation obstacleGoalLoc;
				obstacleGoalLoc.Create(pathParams, goalNavLoc, NavMeshHandle());

				singlePathParams.m_goal.SetWs(obstaclePosWs, obstacleGoalLoc.m_pNavPoly);
				singlePathParams.m_startPositions[0].SetWs(prevPosWs, probeParams.m_pStartPoly);
				singlePathParams.m_startCosts[0] = 0.0f;
				singlePathParams.m_numStartPositions = 1;

				FindSinglePath(owner, singlePathParams, &singlePathResults);

				if (singlePathResults.m_buildResults.m_goalFound)
				{
					pathResultsPs.RemoveEndWaypoint();
					pathResultsPs.AppendPath(singlePathResults.m_buildResults.m_pathWaypointsPs);
				}

				if (!singlePathResults.m_buildResults.m_goalFound
					|| ((singlePathParams.m_buildParams.m_truncateAfterTap > 0) && singlePathResults.m_buildResults.m_hNavActionPack.IsValid())
					|| singlePathResults.m_buildResults.m_pathWaypointsPs.IsFull())
				{
					pPathResults->m_buildResults.m_goalFound = singlePathResults.m_buildResults.m_goalFound;
					pPathResults->m_buildResults.m_hNavActionPack = singlePathResults.m_buildResults.m_hNavActionPack;
					break;
				}

				NavLocation obstacleEndLoc = singlePathResults.m_buildResults.m_pathWaypointsPs.GetEndAsNavLocation(pathParams.m_context.m_parentSpace);

				probeParams.m_pStartPoly = obstacleEndLoc.ToNavPoly();
				probeParams.m_pStartPolyEx = nullptr;

				pCurMesh = obstacleEndLoc.ToNavMesh();
				prevPosWs = pathParams.m_context.m_parentSpace.TransformPoint(obstacleEndLoc.GetPosPs());
			}
		}

		//FindSplineTailGoalPath(owner, pathParams, pPathResults);
	}

	if (FALSE_IN_FINAL_BUILD(pathParams.m_buildParams.m_debugDrawResults))
	{
		pathResultsPs.DebugDraw(pathParams.m_context.m_parentSpace,
								false,
								pathParams.m_context.m_pathRadius,
								PathWaypointsEx::ColorScheme(),
								pathParams.m_buildParams.m_debugDrawTime);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FindUndirectedPaths(const FindPathOwner& owner,
						 const FindUndirectedPathsParams& undirectedPathParams,
						 FindUndirectedPathsResults* pUndirectedPathsResults)
{
	PROFILE_AUTO(AI);

	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const bool needExposureLock =
		undirectedPathParams.m_context.m_pathCostTypeId == SID("buddy-combat") ||
		undirectedPathParams.m_context.m_pathCostTypeId == SID("sneak") ||
		undirectedPathParams.m_context.m_pathCostTypeId == SID("sneak-reduced-exposure") ||
		undirectedPathParams.m_context.m_pathCostTypeId == SID("buddy-follow") ||
		undirectedPathParams.m_context.m_pathCostTypeId == SID("buddy-lead");

	if (needExposureLock)
		g_exposureMapMgr.GetExposureMapLock()->AcquireReadLock(FILE_LINE_FUNC);

	PathFindLocation aStartLocations[PathFindParams::kMaxStartPositions];

	const U32F numStartLocations = GatherStartLocations(undirectedPathParams, aStartLocations, PathFindParams::kMaxStartPositions);

	if ((numStartLocations > 0) && !g_navPathNodeMgr.Failed())
	{
		PathFindNodeData& workData = pUndirectedPathsResults->m_searchData;

		workData.Init(undirectedPathParams);

		NAV_ASSERT(!(owner.m_hOwnerGo.IsKindOf(SID("Buddy")) || owner.m_hOwnerGo.IsKindOf(SID("PlayerBase")))
				   || undirectedPathParams.m_ignoreNodeExtraCost);

#ifdef HEADLESS_BUILD
		const PathFindResults res = DoPathFind<false>(undirectedPathParams,
													  workData,
													  aStartLocations,
													  numStartLocations,
													  nullptr,
													  0);
#else
		NAV_ASSERT(!workData.m_visitedNodes.IsInitialized());

		const bool useTrivialHashtable = true;

		if (useTrivialHashtable)
		{
			const PathFindResults res = DoPathFind<true>(undirectedPathParams,
														 workData,
														 aStartLocations,
														 numStartLocations,
														 nullptr,
														 0);
		}
		else
		{
			CustomAllocateJanitor tableMem(pUndirectedPathsResults->m_buffer, FindUndirectedPathsResults::kBufferSize, FILE_LINE_FUNC);
			const size_t numElems = NavNodeTable::DetermineSizeInElements(FindUndirectedPathsResults::kBufferSize);
			workData.m_visitedNodes.Init(numElems, FILE_LINE_FUNC);
			workData.m_trivialHashVisitedNodes = nullptr;

			const PathFindResults res = DoPathFind<false>(undirectedPathParams,
														  workData,
														  aStartLocations,
														  numStartLocations,
														  nullptr,
														  0);
		}
#endif

		if (FALSE_IN_FINAL_BUILD(undirectedPathParams.m_debugDrawResults))
		{
			PROFILE(AI, FindUndirectedPathsDebugDraw);
			workData.DebugDraw(undirectedPathParams.m_debugDrawTime);
		}
	}

	if (needExposureLock)
		g_exposureMapMgr.GetExposureMapLock()->ReleaseReadLock(FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FindDistanceGoal(const FindPathOwner& owner,
					  const FindSinglePathParams& pathParams,
					  FindSinglePathResults* pPathResults)
{
	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	PROFILE_PRINTF(AI, FindDistanceGoal,
				   ("(%s) @ %0.1fm (%s)",
				   PrettyPrint(pathParams.m_startPositions[0].GetPosWs()),
				   pathParams.m_maxTravelDist,
				   DevKitOnly_StringIdToString(pathParams.m_context.m_pathCostTypeId)));

	PathFindLocation aStartLocations[PathFindParams::kMaxStartPositions];
	const U32F numStartLocations = GatherStartLocations(pathParams, aStartLocations, PathFindParams::kMaxStartPositions);

	pPathResults->Clear();

	if ((numStartLocations > 0) && !g_navPathNodeMgr.Failed())
	{
		ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

		PathFindNodeData workData;
		workData.Init(pathParams);
		workData.m_visitedNodes.Init(2048, FILE_LINE_FUNC);

		NAV_ASSERT(!workData.m_trivialHashVisitedNodes);
		NAV_ASSERT(!(owner.m_hOwnerGo.IsKindOf(SID("Buddy")) || owner.m_hOwnerGo.IsKindOf(SID("PlayerBase")))
				   || pathParams.m_ignoreNodeExtraCost);

		const PathFindResults res = DoPathFind<false>(pathParams, workData, aStartLocations, numStartLocations, nullptr, 0);

		if (res.m_reachedGoals.IsBitSet(0) && !res.m_overflowedClosedList)
		{
			const NavNodeTable::ConstIterator gItr = workData.m_visitedNodes.Find(res.m_goalNodes[0]);

			if (gItr != workData.m_visitedNodes.End())
			{
				const NavNodeData& goalData = gItr->m_data;
				const NavMeshHandle& hGoalMesh = goalData.m_pathNode.GetNavMeshHandle();
				const NavMesh* pGoalMesh = hGoalMesh.ToNavMesh();
				const Point goalPosWs = pGoalMesh->ParentToWorld(goalData.m_pathNodePosPs);

				BuildPath(workData, pathParams.m_buildParams, res.m_goalNodes[0], goalPosWs, &pPathResults->m_buildResults);

				pPathResults->m_buildResults.m_goalFound = true;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawNodeTable(const NavNodeTable* pNodeTable,
						const TrivialHashNavNodeData* pTrivialHashData,
						DebugPrimTime drawTime /* = kPrimDuration1FrameAuto */)
{
	STRIP_IN_FINAL_BUILD;

	if (!pNodeTable && !pTrivialHashData)
		return;

	const bool useTrivialHashTable = pTrivialHashData;

	NAV_ASSERT(!useTrivialHashTable || !pNodeTable || !pNodeTable->IsInitialized());

	NavNodeTable::ConstIterator itr;
	if (!useTrivialHashTable)
		itr = pNodeTable->Begin();

	NavPathNode::NodeId i = 0;

	for (; useTrivialHashTable ? (i < NavPathNodeMgr::kMaxNodeCount) : (itr != pNodeTable->End()); useTrivialHashTable ? (void)(i++) : (void)(itr++))
	{
		const NavPathNode::NodeId pathNodeId = useTrivialHashTable ? i : itr->m_key.GetPathNodeId();
		const U16 partitionId = useTrivialHashTable ? 0 : itr->m_key.GetPartitionId();

		const TrivialHashNavNodeData* pData = useTrivialHashTable ? &pTrivialHashData[i] : nullptr;
		if (useTrivialHashTable && !pData->IsValid())
			continue;

		const Point nodePosPs = useTrivialHashTable ? pData->m_posPs : itr->m_data.m_pathNodePosPs;

		F32 textVerticalOffset = 0.0f;
		F32 nodeVerticalOffset = 0.0f;
		Color linkColor = kColorWhite;

		StringBuilder<128> nodeStr;
		if (useTrivialHashTable)
		{
			const F32 dist = pData->GetFromDist();
			nodeStr.append_format("%5.3f", dist);
		}
		else
		{
			const F32 cost = itr->m_data.m_fromCost;
			const F32 dist = itr->m_data.m_fromDist;
			nodeStr.append_format("%5.3f (%5.3f)", dist, cost);
		}

		const int type = useTrivialHashTable ? pData->m_nodeType : itr->m_data.m_pathNode.GetNodeType();

		switch (type)
		{
		case NavPathNode::kNodeTypePoly:
		case NavPathNode::kNodeTypeActionPackEnter:
		case NavPathNode::kNodeTypeActionPackExit:
			{
				const NavMesh* pNavMesh = nullptr;

				if (useTrivialHashTable)
					pNavMesh = EngineComponents::GetNavMeshMgr()->UnsafeFastLookupNavMesh(pData->m_navMeshIndex, pData->m_uniqueId);
				else
					pNavMesh = itr->m_data.m_pathNode.GetNavMeshHandle().ToNavMesh();

				if (!pNavMesh)
					continue;

				// nodeVerticalOffset = 0.25f;
				linkColor		   = kColorYellow;

				textVerticalOffset	= F32((pathNodeId + partitionId) % 10) * 0.01f;

				if (partitionId)
				{
					nodeStr.append_format(" [%d-%d]", pathNodeId, partitionId);
				}
				else
				{
					nodeStr.append_format(" [%d]", pathNodeId);
				}
			}
			break;

#if ENABLE_NAV_LEDGES
		case NavPathNode::kNodeTypeNavLedge:
			{
				NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
				NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

				const NavLedgeGraph* pNavLedgeGraph = pNode->GetNavLedgeHandle().ToLedgeGraph();
				if (!pNavLedgeGraph)
					continue;

				linkColor = kColorGreen;
			}
			break;
#endif
		default:
			// dunno how to handle anything else
			continue;
		}

		Point nodePosWs;
		if (useTrivialHashTable)
		{
			const NavMesh* pNavMesh = EngineComponents::GetNavMeshMgr()->UnsafeFastLookupNavMesh(pData->m_navMeshIndex, pData->m_uniqueId);
			if (pNavMesh)
			{
				nodePosWs = pNavMesh->ParentToWorld(nodePosPs);
			}
			else
			{
				nodePosWs = nodePosPs;
			}
		}
		else
		{
			nodePosWs = itr->m_data.m_pathNode.ParentToWorld(nodePosPs);
		}

		nodePosWs += Vector(kUnitYAxis) * nodeVerticalOffset;

		const Vector textVerticalOffsetVector = Vector(kUnitYAxis) * textVerticalOffset;
		g_prim.Draw(DebugString(nodePosWs + textVerticalOffsetVector, nodeStr.c_str(), kColorWhite, 0.5f), drawTime);

		NavNodeTable::ConstIterator parentItr;
		NavPathNode::NodeId parentI = 0;
		if (useTrivialHashTable)
		{
			parentI = pTrivialHashData[i].m_iParent;
		}
		else
		{
			parentItr = pNodeTable->Find(itr->m_data.m_parentNode);
		}
		if (useTrivialHashTable ? (parentI < NavPathNodeMgr::kMaxNodeCount && pTrivialHashData[parentI].IsValid()) : (parentItr != pNodeTable->End()))
		{
			const Point parentNodePosPs = useTrivialHashTable ? pTrivialHashData[parentI].m_posPs : parentItr->m_data.m_pathNodePosPs;

			Point parentNodePosWs;
			if (useTrivialHashTable)
			{
				const NavMesh* pNavMesh = EngineComponents::GetNavMeshMgr()->UnsafeFastLookupNavMesh(pData->m_navMeshIndex, pData->m_uniqueId);
				if (pNavMesh)
				{
					parentNodePosWs = pNavMesh->ParentToWorld(parentNodePosPs);
				}
				else
				{
					parentNodePosWs = nodePosPs;
				}
			}
			else
			{
				parentNodePosWs = parentItr->m_data.m_pathNode.ParentToWorld(parentNodePosPs);
			}

			parentNodePosWs += Vector(kUnitYAxis) * nodeVerticalOffset;
			g_prim.Draw(DebugLine(parentNodePosWs, nodePosWs, linkColor, 3.0f), drawTime);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathFindNodeData::DebugDraw(DebugPrimTime drawTime /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	DebugDrawNodeTable(&m_visitedNodes, m_trivialHashVisitedNodes, drawTime);
}

#ifdef HEADLESS_BUILD

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathFindNodeData::IsKeyVisited(const NavNodeKey& key, const NavHandle& hNav, NavKeyDataPair* pKeyDataPairOut /* = nullptr */) const
{
	if (!m_visitedNodes.IsInitialized())
		return false;

	NavNodeTable::ConstIterator itr = m_visitedNodes.Find(key);

	if (itr == m_visitedNodes.End())
		return false;

	const NavNodeData& nodeData = itr->m_data;

	switch (hNav.GetType())
	{
	case NavLocation::Type::kNavPoly:
		if (nodeData.m_pathNode.GetNavMeshHandle() != hNav.GetNavMeshHandle())
			// this search data has become stale (usually a nav mesh got logged out)
			return false;
		break;
#if ENABLE_NAV_LEDGES
	case NavLocation::Type::kNavLedge:
		if (nodeData.m_pathNode.GetNavLedgeHandle() != hNav.GetNavLedgeHandle())
			// this search data has become stale (usually a nav mesh got logged out)
			return false;
		break;
#endif
	}

	if (pKeyDataPairOut)
	{
		pKeyDataPairOut->m_key = itr->m_key;
		pKeyDataPairOut->m_data = nodeData;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathFindNodeData::IsHandleVisited(const NavHandle& hNav, NavKeyDataPair* pKeyDataPairOut /* = nullptr */) const
{
	const NavNodeKey key(hNav);
	return IsKeyVisited(key, hNav, pKeyDataPairOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathFindNodeData::IsLocationVisited(const NavLocation& navLoc, NavKeyDataPair* pKeyDataPairOut /* = nullptr */) const
{
	const NavNodeKey key(navLoc, m_context);
	return IsKeyVisited(key, navLoc, pKeyDataPairOut);
}

#else

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathFindNodeData::IsHandleVisited(const NavHandle& hNav, TrivialHashNavNodeData* pDataOut /* = nullptr */) const
{
	const NavManagerId navManagerId = hNav.GetNavManagerId();
	const NavPathNode::NodeId nodeId = hNav.GetPathNodeId();

	return IsNodeVisited(navManagerId, nodeId, pDataOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool PathFindNodeData::IsNodeVisited(const NavManagerId navManagerId,
									 const NavPathNode::NodeId nodeId,
									 TrivialHashNavNodeData* pDataOut /* = nullptr */) const
{
	NAV_ASSERTF(!m_visitedNodes.IsInitialized(), ("Undirecteds only!"));

	if (!m_trivialHashVisitedNodes)
		return false;

	if (navManagerId == NavManagerId::kInvalidMgrId)
		return false;

	if (nodeId >= NavPathNodeMgr::kMaxNodeCount)
		return false;

	const U16 navMeshIndex = navManagerId.m_navMeshIndex;
	NAV_ASSERT(navMeshIndex < NavMeshMgr::kMaxNavMeshCount);

	const U16 iPoly = navManagerId.m_iPoly;
	const U16 uniqueId = navManagerId.m_uniqueId;

	const TrivialHashNavNodeData& data = m_trivialHashVisitedNodes[nodeId];
	if (!data.IsValid() || uniqueId != data.m_uniqueId)
		return false;

	if (pDataOut)
		*pDataOut = data;

	return true;
}

#endif

/// --------------------------------------------------------------------------------------------------------------- ///
void PathCostMgr::RegisterCostFunc(StringId64 nameId, PFuncCost costFunc)
{
	NAV_ASSERT(m_numFuncs < kMaxCostFuncs);

	m_names[m_numFuncs] = nameId;
	m_funcs[m_numFuncs] = costFunc;
	m_numFuncs++;
}

/// --------------------------------------------------------------------------------------------------------------- ///
PathCostMgr::PFuncCost PathCostMgr::LookupCostFunc(const PathFindContext& pathContext, StringId64 nameId)
{
	PFuncCost func = nullptr;

	switch (nameId.GetValue())
	{
	case SID_VAL("combat-vector"):
		if (pathContext.m_hasCombatVector)
		{
			func = CostFuncCombatVector;
		}
		else
		{
			func = CostFuncDistance;
		}
		break;

	case SID_VAL("combat-vector-with-stealth-grass"):
		if (pathContext.m_hasCombatVector)
		{
			func = CostFuncCombatVectorWithStealthGrass;
		}
		else
		{
			func = CostFuncDistance;
		}
		break;

	case SID_VAL("distance"):
	case 0:
		func = CostFuncDistance;
		break;
	}

	if (!func)
	{
		for (U32 funcIdx = 0; funcIdx < m_numFuncs; funcIdx++)
		{
			if (m_names[funcIdx] == nameId)
			{
				func = m_funcs[funcIdx];
				break;
			}
		}
	}

	NAV_ASSERTF(func, ("Unable to find pathfind cost func %s", DevKitOnly_StringIdToString(nameId)));

	return func;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathFindNodeData::Init(const PathFindParams& params)
{
	m_context = params.m_context;
	m_config.Init(params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PathFindConfig::Init(const PathFindParams& pathParams)
{
	const PathFindContext& pathContext = pathParams.m_context;

	m_zeroValue = 1.0f;

	// setup distance func
	PathCostMgr& pcMgr = PathCostMgr::Get();
	m_costFunc = pcMgr.LookupCostFunc(pathContext, pathContext.m_pathCostTypeId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
PathFindData::PathFindData(OpenList& openList,
						   NavNodeTable* const pClosedList,
						   const PathFindLocation* pGoals,
						   U32F numGoals,
						   const PathFindContext& pathContext)
	: m_openList(openList)
	, m_pClosedList(pClosedList)
{
	m_remainingGoalBits.ClearAllBits();

	for (U64 iGoal = 0; iGoal < numGoals; ++iGoal)
	{
		const NavNodeKey goalKey = NavNodeKey(pGoals[iGoal], pathContext);
		if (goalKey.GetPathNodeId() == NavPathNodeMgr::kInvalidNodeId)
			continue;

		m_aGoalNodes[iGoal] = goalKey;
		m_remainingGoalBits.SetBit(iGoal);
	}
}

/************************************************************************/
/* Local function implementations                                       */
/************************************************************************/

/// --------------------------------------------------------------------------------------------------------------- ///
static const NavPoly* GetNavPolyFromPathNode(const NavMesh* pMesh, const NavPathNode* pNode)
{
	if (!pNode || !pMesh)
		return nullptr;

	const NavPoly* pPoly = nullptr;

	switch (pNode->GetNodeType())
	{
	case NavPathNode::kNodeTypePoly:
	case NavPathNode::kNodeTypePolyEx:
		pPoly = &pMesh->GetPoly(pNode->GetNavManagerId().m_iPoly);
		break;
	default:
		break;
	}

	return pPoly;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Point PointOnSegment(Point_arg point, Point_arg a, Point_arg b)
{
	const Vector d = (b - a);
	const Vector ap = (point - a);
	const Scalar lenSqr = LengthSqr(d);

	Point closestPt;

	if (UNLIKELY(lenSqr < NDI_FLT_EPSILON))
	{
		closestPt = a;
	}
	else
	{
		const Scalar dotP = Dot(d, ap);
		const Scalar t = MinMax01(AccurateDiv(dotP, lenSqr));
		const Vector v = ap - (t * d);
		closestPt = point - v;
	}

	return closestPt;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsPolyNodeType(NavPathNode::NodeType nodeType)
{
	switch (nodeType)
	{
	case NavPathNode::kNodeTypePoly:
	case NavPathNode::kNodeTypePolyEx:
		return true;

	default:
		return false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Point GetAdjacentNodePosPs(const NavPathNode* pCurNode,
								  const NavMesh* pCurNavMesh,
								  const NavPathNode* pAdjNode,
								  const NavPathNode::Link& link,
								  Point_arg curNodePosPs,
								  Point_arg goalNodePosPs)
{
	//PROFILE_ACCUM(GetAdjacentNodePosPs);

	// NOTE: this bit of magic hugely improves the quality
	// of found paths.  in particular, the paths are much
	// less likely to choose a longer route because of how
	// the nav mesh is triangulated.  It does this by
	// adjusting the positions of nodes when a node is
	// opened.

	NAV_ASSERT(pCurNode);
	NAV_ASSERT(pAdjNode);

	Point adjNodePosPs;

#if 0
	const NavPathNode::NodeType adjNodeType = pAdjNode->GetNodeType();
	const NavPathNode::NodeType curNodeType = pCurNode->GetNodeType();

	if (IsPolyNodeType(adjNodeType))
	{
		if (IsPolyNodeType(curNodeType))
		{
			const Point v0Ps = link.m_edgeVertsPs[0];
			const Point v1Ps = link.m_edgeVertsPs[1];

			adjNodePosPs = PointOnSegment(curNodePosPs, v0Ps, v1Ps);
		}
		else
		{
			adjNodePosPs = pCurNode->GetPositionPs();
		}
	}
	else
	{
		adjNodePosPs = pAdjNode->GetPositionPs();
	}
#elif 0
	const Point v0Ps = link.m_edgeVertsPs[0];
	const Point v1Ps = link.m_edgeVertsPs[1];

	//adjNodePosPs = PointOnSegment(curNodePosPs, v0Ps, v1Ps);
	adjNodePosPs = ClosestPointOnEdgeToPoint(v0Ps, v1Ps, curNodePosPs, 0.1f);
#else
	const Point v0Ps = link.m_edgeVertsPs[0];
	const Point v1Ps = link.m_edgeVertsPs[1];

	Scalar t0, t1;

	const Segment boundarySegPs = Segment(v0Ps, v1Ps);
	const Segment toGoalSegPs = Segment(curNodePosPs, goalNodePosPs);

	// if (IntersectLinesXz(boundarySegPs, toGoalSegPs, t0, t1))
	if (IntersectSegmentSegmentXz(boundarySegPs, toGoalSegPs, t0, t1))
	{
		adjNodePosPs = Lerp(boundarySegPs.a, boundarySegPs.b, Limit01(t0));
	}
	else
	{
		const Vector boundaryNormPs = AsUnitVectorXz(RotateY90(boundarySegPs.b - boundarySegPs.a), kZero);
		const Vector goalDirPs = AsUnitVectorXz(curNodePosPs - goalNodePosPs, kZero);
		const float normDot = Dot(boundaryNormPs, goalDirPs);

		if ((Abs(normDot) < 0.7f) || g_navOptions.m_disableNewHeuristicHack)
		{
			adjNodePosPs = pAdjNode->GetPositionPs();
		}
		else if (t0 < 0.0f)
		{
			adjNodePosPs = boundarySegPs.a;
		}
		else if (t0 > 1.0f)
		{
			adjNodePosPs = boundarySegPs.b;
		}
		else
		{
			adjNodePosPs = pAdjNode->GetPositionPs();
		}
	}
#endif

	return adjNodePosPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool DynamicLinkAvailable(const NavPathNode* pCurNode,
								 const NavPathNode::Link& link,
								 const NavPathNode::Link* pPathLinks)
{
	if (!pCurNode || !pPathLinks)
		return false;

	bool foundDynamic = false;

	for (U32F iLink = pCurNode->GetFirstLinkId(); iLink != NavPathNodeMgr::kInvalidLinkId; iLink = pPathLinks[iLink].m_nextLinkId)
	{
		const NavPathNode::Link& otherLink = pPathLinks[iLink];

		if (!otherLink.IsDynamic())
			continue;

		if (otherLink.m_staticNodeId == link.m_nodeId)
		{
			foundDynamic = true;
			break;
		}
	}

	return foundDynamic;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool DynamicExpansionAllowed(const NavPathNode* pNode, const NavBlockerBits& obeyedBlockers)
{
	if (!pNode)
		return false;

	if (pNode->GetNodeType() != NavPathNode::kNodeTypePolyEx)
		return true;

	const U32F polyExId = pNode->GetNavManagerId().m_iPolyEx;

	const NavPolyEx* pPolyEx = GetNavPolyExFromId(polyExId);
	if (!pPolyEx)
		return false;

	NavBlockerBits obeyedPolyBlockers;
	NavBlockerBits::BitwiseAnd(&obeyedPolyBlockers, pPolyEx->m_blockerBits, obeyedBlockers);

	return obeyedPolyBlockers.AreAllBitsClear();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool LineCrossesGap(Point_arg start, Point_arg end, Point_arg gap0, Point_arg gap1)
{
	const Scalar kEpsilon = SCALAR_LC(0.0001f);

	const Vector gapVec = VectorXz(gap1 - gap0);
	const Vector gapToStart = VectorXz(start - gap0);
	const Vector gapToEnd	= VectorXz(end - gap0);

	const Vector cross0 = Cross(gapToStart, gapVec);
	const Vector cross1 = Cross(gapToEnd, gapVec);

	const Scalar dotP = Dot(cross0, cross1);

	return dotP <= kEpsilon;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsLineBlockedByGaps(const PathFindParams& pathParams,
								const NavMesh* pNavMesh,
								const NavMeshGapRef* pGapList,
								Point_arg startLs,
								Point_arg endLs)
{
	//PROFILE_ACCUM(IsLineBlockedByGaps);

	const PathFindContext& pathContext = pathParams.m_context;
	const float minRadius = pathContext.m_pathRadius;

	if ((minRadius <= kSmallestFloat) || !pGapList)
		return false;

	const U32F numGaps = pNavMesh->GetNumGaps();

	bool blocked = false;
	const float minDiameter = minRadius * 2.0f;

	Scalar t0, t1;
	const Segment moveSegLs = Segment(startLs, endLs);

	for (const NavMeshGapRef* pGapRef = pGapList; (pGapRef && pGapRef->m_pGap); ++pGapRef)
	{
		const NavMeshGap* pGap = pGapRef->m_pGap;

		if ((!pGap->m_enabled0) || (!pGap->m_enabled1))
			continue;

		if ((pathParams.m_context.m_obeyedStaticBlockers & pGap->m_blockageMask0) == 0)
			continue;

		if ((pathParams.m_context.m_obeyedStaticBlockers & pGap->m_blockageMask1) == 0)
			continue;

		const float gapDist = pGap->m_gapDist;
		if (gapDist >= minDiameter)
			continue;

		if (gapDist < 0.025f)
			continue;

		const Segment gapSegLs = Segment(pGap->m_pos0, pGap->m_pos1);

		const bool intersects = pathParams.m_fixGapChecks ? IntersectSegmentSegmentXz(moveSegLs, gapSegLs, t0, t1)
														  : LineCrossesGap(startLs, endLs, pGap->m_pos0, pGap->m_pos1);
		if (intersects)
		{
			if (FALSE_IN_FINAL_BUILD(pathParams.m_debugDrawSearch))
			{
				const Vector vo = Vector(0.0f, 0.1f, 0.0f);
				pGap->DebugDraw(pNavMesh, kColorRed, 0.1f);
				g_prim.Draw(DebugArrow(pNavMesh->LocalToWorld(startLs) + vo, pNavMesh->LocalToWorld(endLs) + vo, kColorRed),
							pathParams.m_debugDrawTime);
			}

			blocked = true;
			break;
		}
	}

	return blocked;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsLineBlockedByExGaps(const PathFindParams& pathParams,
								  const NavMesh* pNavMesh,
								  Point_arg prevPosLs,
								  Point_arg nextPosLs,
								  const NavMeshGapEx* pExGaps,
								  const U32F numGaps)
{
	//PROFILE_ACCUM(IsLineBlockedByExGaps);

	const PathFindContext& pathContext = pathParams.m_context;
	const float sweepRadius = pathContext.m_pathRadius;
	const NavBlockerBits& obeyedBlockers = pathContext.m_obeyedBlockers;

	NavBlockerBits testBits;

	const Segment moveSeg = Segment(prevPosLs, nextPosLs);

	bool blocked = false;
	const float minDiameter = sweepRadius * 2.0f;

	for (U32F iGap = 0; iGap < numGaps; ++iGap)
	{
		const NavMeshGapEx& gapEx = pExGaps[iGap];

		if (gapEx.m_gapDist > minDiameter)
			continue;

		NavBlockerBits::BitwiseAnd(&testBits, obeyedBlockers, gapEx.m_blockerBits);
		if (testBits.AreAllBitsClear())
			continue;

		const Segment gapSeg = Segment(gapEx.m_pos0Ls, gapEx.m_pos1Ls);

		Scalar t0, t1;
		if (IntersectSegmentSegmentXz(moveSeg, gapSeg, t0, t1)) // if (LineCrossesGap(prevPosLs, nextPosLs, pos0Ls, pos1Ls))
		{
			if (FALSE_IN_FINAL_BUILD(pathParams.m_debugDrawSearch && pNavMesh))
			{
				const Vector vo = Vector(0.0f, 0.1f, 0.0f);
				const Point pos0Ws = pNavMesh->LocalToWorld(gapSeg.a) + vo;
				const Point pos1Ws = pNavMesh->LocalToWorld(gapSeg.b) + vo;
				const Point prevWs = pNavMesh->LocalToWorld(moveSeg.a) + vo;
				const Point nextWs = pNavMesh->LocalToWorld(moveSeg.b) + vo;

				g_prim.Draw(DebugLine(pos0Ws, pos1Ws, kColorOrange, 4.0f), pathParams.m_debugDrawTime);
				g_prim.Draw(DebugArrow(prevWs, nextWs, kColorRed), pathParams.m_debugDrawTime);
				g_prim.Draw(DebugLine(prevWs, pos0Ws, kColorRed), pathParams.m_debugDrawTime);
				g_prim.Draw(DebugLine(prevWs, pos1Ws, kColorRed), pathParams.m_debugDrawTime);
			}

			blocked = true;
			break;
		}
	}

	return blocked;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static U32F GatherExGaps(const NavPathNode* pPrevNode,
						 const NavPathNode* pNextNode,
						 NavMeshGapEx* pExGapBuffer,
						 size_t exGapBufferSize)
{
	NAV_ASSERT(pPrevNode);
	NAV_ASSERT(pNextNode);
	NAV_ASSERT(pExGapBuffer);
	NAV_ASSERT(exGapBufferSize > 0);

	NavManagerId prevPolyId = pPrevNode->GetNavManagerId();
	NavManagerId nextPolyId = pNextNode->GetNavManagerId();
	prevPolyId.m_iPolyEx = 0;
	nextPolyId.m_iPolyEx = 0;

	size_t numGaps = g_navExData.GatherExGapsForPolyUnsafe(prevPolyId, pExGapBuffer, exGapBufferSize);
	NAV_ASSERT(numGaps <= exGapBufferSize);

	if (prevPolyId.m_u64 != nextPolyId.m_u64)
	{
		numGaps += g_navExData.GatherExGapsForPolyUnsafe(nextPolyId, &pExGapBuffer[numGaps], exGapBufferSize - numGaps);
		NAV_ASSERT(numGaps <= exGapBufferSize);
	}

	return numGaps;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool RadialExpansionAllowed(const NavMesh* pNavMesh,
								   const NavPathNode* pPrevNode,
								   const NavPathNode* pNextNode,
								   const PathFindParams& pathParams,
								   Point_arg prevPosPs,
								   Point_arg nextPosPs,
								   const NavPathNode::Link& link,
								   NavMeshGapEx* pExGapBuffer,
								   size_t exGapBufferSize)
{
	const PathFindContext& pathContext = pathParams.m_context;
	const float sweepRadius = pathContext.m_pathRadius;

	if (sweepRadius <= NDI_FLT_EPSILON)
		return true;

	if (!pPrevNode || !pNextNode || !pNavMesh)
		return true;

	if (!pNextNode->IsNavMeshNode())
		return true;

	//PROFILE_ACCUM(RadialExpansionAllowed);

	if ((pPrevNode->GetId() == 687) && (pNextNode->GetId() == 691))
	{
		// g_prim.Draw(DebugArrow(prevPosPs, nextPosPs, kColorCyan, 0.3f, kPrimEnableHiddenLineAlpha));
	}

	const NavPathNode::NodeType prevType = pPrevNode->GetNodeType();

	const NavManagerId prevMgrId = pPrevNode->GetNavManagerId();
	const NavManagerId nextMgrId = pNextNode->GetNavManagerId();

	const NavPoly* pPrevPoly = pNavMesh->UnsafeGetPolyFast(prevMgrId.m_iPoly);
	const NavPoly* pNextPoly = pNavMesh->UnsafeGetPolyFast(nextMgrId.m_iPoly);

	bool blocked = false;

	const NavMeshGapRef* pGaps = pPrevPoly->GetGapList();
	const NavMeshGapRef* pNextGaps = (pNextPoly && pNextPoly->GetNavMeshHandle() == pPrevPoly->GetNavMeshHandle())
										 ? pNextPoly->GetGapList()
										 : nullptr;

	const Point link0Ps = link.m_edgeVertsPs[0];
	const Point link1Ps = link.m_edgeVertsPs[1];

	const Vector moveDirPs = AsUnitVectorXz(nextPosPs - prevPosPs, kZero);
	//const float nudgeDist = 0.1f;
	const float nudgeDist = 0.01f;
	const Vector moveNudgePs = moveDirPs * nudgeDist;

	const Point nudgedPrevPs = prevPosPs - moveNudgePs;
	const Point nudgedNextPs = nextPosPs + moveNudgePs;

	const Point prevPosLs = pNavMesh->ParentToLocal(nudgedPrevPs);
	const Point nextPosLs = pNavMesh->ParentToLocal(nudgedNextPs);
	const Vector moveDirLs = pNavMesh->ParentToLocal(moveDirPs);

	const bool wantExGaps = pathContext.m_dynamicSearch && (pPrevPoly->HasExGaps() || pNextPoly->HasExGaps());

	const U32F numExGaps = wantExGaps ? GatherExGaps(pPrevNode, pNextNode, pExGapBuffer, exGapBufferSize) : 0;

	if (IsLineBlockedByGaps(pathParams, pNavMesh, pGaps, prevPosLs, nextPosLs))
	{
		blocked = true;
	}
	else if (pNextGaps && IsLineBlockedByGaps(pathParams, pNavMesh, pNextGaps, prevPosLs, nextPosLs))
	{
		blocked = true;
	}

	if (!blocked && wantExGaps)
	{
		blocked = IsLineBlockedByExGaps(pathParams,
										pNavMesh,
										prevPosLs,
										nextPosLs,
										pExGapBuffer,
										numExGaps);
	}
	Scalar t0, t1;
	if (!blocked && !IntersectSegmentSegmentXz(Segment(link0Ps, link1Ps), Segment(prevPosPs, nextPosPs), t0, t1))
	{
		// Sometimes due to poly shapes testing the line from path node to path node might not overlap all the relevant gaps
		// because the path node positions don't generate a segment that goes through the boundary segment joining the two
		// polys. Soo in that case we test two extra segments: one from the start to the middle of the boundary, and another
		// from the middle of the boundary to the goal position.
		// NB: In theory could use t0 here to test prev -> intersection pt -> next but for path finding
		// going through the middle should be safer just in case we lay right on the edge
		const Point linkMidPs = AveragePos(link0Ps, link1Ps);
		const Point linkMidLs = pNavMesh->ParentToLocal(linkMidPs);

		if (FALSE_IN_FINAL_BUILD(pathParams.m_debugDrawSearch))
		{
			PrimServerWrapper ps(pathContext.m_parentSpace);
			ps.EnableHiddenLineAlpha();
			ps.SetLineWidth(4.0f);
			ps.SetDuration(pathParams.m_debugDrawTime);
			ps.DrawLine(link0Ps, link1Ps, kColorOrange);
			ps.DrawLine(prevPosPs, nextPosPs, kColorYellow);
			ps.SetLineWidth(2.0f);
			ps.DrawArrow(prevPosPs, linkMidPs, 0.2f, kColorGreenTrans);
			ps.DrawArrow(linkMidPs, nextPosPs, 0.2f, kColorGreenTrans);
		}

		const Vector linkPerpPs = RotateY90(link1Ps - link0Ps);
		const Vector linkNormPs = AsUnitVectorXz(linkPerpPs, moveDirPs) * Sign(DotXz(linkPerpPs, moveDirPs));
		const Vector linkNormLs = pNavMesh->ParentToLocal(linkNormPs);

		const Point linkMid0Ls = linkMidLs + (linkNormLs * nudgeDist);
		const Point linkMid1Ls = linkMidLs - (linkNormLs * nudgeDist);

		if (IsLineBlockedByGaps(pathParams, pNavMesh, pGaps, prevPosLs, linkMid0Ls))
		{
			blocked = true;
		}

		if (!blocked && IsLineBlockedByGaps(pathParams, pNavMesh, pGaps, linkMid1Ls, nextPosLs))
		{
			blocked = true;
		}

		if (wantExGaps)
		{
			if (!blocked
				&& IsLineBlockedByExGaps(pathParams,
										 pNavMesh,
										 prevPosLs,
										 linkMid0Ls,
										 pExGapBuffer,
										 numExGaps))
			{
				blocked = true;
			}

			if (!blocked
				&& IsLineBlockedByExGaps(pathParams,
										 pNavMesh,
										 linkMid1Ls,
										 nextPosLs,
										 pExGapBuffer,
										 numExGaps))
			{
				blocked = true;
			}
		}
	}

	return !blocked;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsGoalSatisfied(const NavNodeKey& curNode,
							Point_arg curPosPs,
							const NavNodeKey& goalNode,
							const PathFindLocation& goalLocation,
							const PathFindParams& pathParams,
							NavMeshGapEx* pExGapBuffer,
							size_t exGapBufferSize)
{
	if (curNode == goalNode)
		return true;

	if (curNode.GetPathNodeId() != goalNode.GetPathNodeId())
		return false;

	bool blocked = false;

	// same path node id, but different partition id, need to check if there are any blocking gaps
	if (const NavPoly* pGoalPoly = goalLocation.m_pNavPoly)
	{
		const NavMesh* pGoalMesh = pGoalPoly->GetNavMesh();
		const NavMeshGapRef* pGoalPolyGaps = pGoalPoly->GetGapList();
		const Point curPosLs = pGoalMesh->ParentToLocal(curPosPs);
		const Point goalPosLs = pGoalMesh->WorldToLocal(goalLocation.m_posWs);

		const NavManagerId goalPolyId = pGoalPoly->GetNavManagerId();

		blocked = IsLineBlockedByGaps(pathParams, pGoalMesh, pGoalPolyGaps, curPosLs, goalPosLs);

		if (!blocked && pathParams.m_context.m_dynamicSearch && pGoalPoly->HasExGaps())
		{
			const size_t numGaps = g_navExData.GatherExGapsForPolyUnsafe(pGoalPoly->GetNavManagerId(), pExGapBuffer, exGapBufferSize);

			if (IsLineBlockedByExGaps(pathParams, pGoalMesh, curPosLs, goalPosLs, pExGapBuffer, numGaps))
			{
				blocked = true;
			}
		}
	}

	return !blocked;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool TrySatisfyGoal(PathFindData& pfData,
						   const PathFindLocation* aGoalLocations,
						   const NavNodeKey& curNode,
						   const NavNodeData& curData,
						   const PathFindParams& pathParams,
						   PathFindResults& results,
						   NavMeshGapEx* pExGapBuffer,
						   size_t exGapBufferSize)
{
	if (pathParams.m_distanceGoal > 0.0f)
	{
		if (curData.m_fromDist >= pathParams.m_distanceGoal)
		{
			results.m_reachedGoals.SetBit(0);
			results.m_goalNodes[0] = curNode;
			return false;
		}
	}
	else if (!pfData.m_remainingGoalBits.AreAllBitsClear())
	{
		bool keepRunning = true;

		for (U64 iGoal = pfData.m_remainingGoalBits.FindFirstSetBit(); iGoal < 64; iGoal = pfData.m_remainingGoalBits.FindNextSetBit(iGoal))
		{
			const NavNodeKey& goalNode = pfData.m_aGoalNodes[iGoal];
			const PathFindLocation& goalLocation = aGoalLocations[iGoal];

			if (IsGoalSatisfied(curNode, curData.m_pathNodePosPs, goalNode, goalLocation, pathParams, pExGapBuffer, exGapBufferSize))
			{
				//g_prim.Draw(DebugArrow(curData.m_pathNodePosPs, goalPoly.m_posWs, kColorOrange));

				// our cur node and goal node might be on different partition id's, so we may have never directly opened our goal
				pfData.m_openList.Remove(curNode);
				pfData.m_openList.Remove(goalNode);

				NAV_ASSERT(g_navPathNodeMgr.GetNode(goalNode.GetPathNodeId()).GetNavMeshHandle() == curData.m_pathNode.GetNavMeshHandle());

				pfData.m_pClosedList->Set(goalNode, curData);

				pfData.m_remainingGoalBits.ClearBit(iGoal);

				results.m_reachedGoals.SetBit(iGoal);
				results.m_goalNodes[iGoal] = curNode;

				if (pfData.m_remainingGoalBits.AreAllBitsClear())
				{
					keepRunning = false;
					break;
				}
			}
		}

		return keepRunning;
	}

	// undirected path search, run forever
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void DebugDrawNavNode(const NavNodeKey& key,
							 const NavNodeData& data,
							 const NavNodeData& parentData,
							 bool singularGoal,
							 Point_arg singularGoalPosPs,
							 PathFindNodeData& workData,
							 DebugPrimTime drawTime,
							 bool drawText)
{
	STRIP_IN_FINAL_BUILD;

	const NavPathNode*	pCurPathNode = &g_navPathNodeMgr.GetNode(key.GetPathNodeId());
	const NavPathNode*	pParentPathNode = &g_navPathNodeMgr.GetNode(data.m_parentNode.GetPathNodeId());

	const NavMesh* pCurNavMesh = pCurPathNode->GetNavMeshHandle().ToNavMesh();
	const NavMesh* pParentNavMesh = pParentPathNode->GetNavMeshHandle().ToNavMesh();

	if (!pCurNavMesh || !pParentNavMesh)
		return;

	const Point curNodePosPs = data.m_pathNodePosPs;
	const Point parentNodePosPs = parentData.m_pathNodePosPs;

	const Point curNodePosWs = pCurNavMesh->ParentToWorld(curNodePosPs);
	const Point parentNodePosWs = pParentNavMesh->ParentToWorld(parentNodePosPs);

	g_prim.Draw(DebugLine(parentNodePosWs, curNodePosWs, kColorWhite, kColorBlack, 3.0f, PrimAttrib(kPrimDisableDepthTest)), drawTime);

	if (drawText)
	{
		char buf[200];
		const F32 toCost = singularGoal ? ComputeHeuristicCost(curNodePosPs, singularGoalPosPs, workData.m_config.m_zeroValue) : 0.0f;
		const Vector offset = pCurPathNode->IsActionPackNode() ? Vector(0.0f, 0.25f, 0.0f) : Vector(0.0f, 0.0f, 0.0f);

		sprintf(buf, "[%d:%d]: %5.3f + %5.3f = %5.3f", key.GetPathNodeId(), key.GetPartitionId(), data.m_fromCost, toCost, data.m_fromCost + toCost);
		g_prim.Draw(DebugString(curNodePosWs + offset, buf, kColorWhite, g_navCharOptions.m_drawSinglePathfindTextSize), drawTime);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 CostFuncDistance(const PathFindContext& pathContext,
					 const NavPathNodeProxy& pathNode,
					 const NavPathNode::Link& link,
					 Point_arg fromPosPs,
					 Point_arg toPosPs,
					 bool* pReject)
{
	return Dist(fromPosPs, toPosPs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 CostFuncCombatVector(const PathFindContext& pathContext,
						 const NavPathNodeProxy& pathNode,
						 const NavPathNode::Link& link,
						 Point_arg fromPosPs,
						 Point_arg toPosPs,
						 bool* pReject)
{
	return CostFuncCombatVectorWeighted(pathContext, pathNode, link, fromPosPs, toPosPs, 1.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 CostFuncCombatVectorWithStealthGrass(const PathFindContext& pathContext,
										 const NavPathNodeProxy& pathNode,
										 const NavPathNode::Link& link,
										 Point_arg fromPosPs,
										 Point_arg toPosPs,
										 bool* pReject)
{
	if (const NavPoly* pPoly = pathNode.GetNavPolyHandle().ToNavPoly())
	{
		if (pPoly->IsStealth())
		{
			const Vector vEdge = fromPosPs - toPosPs;
			const Scalar edgeLen = Length(vEdge);

			return edgeLen;
		}
	}

	return CostFuncCombatVectorWeighted(pathContext, pathNode, link, fromPosPs, toPosPs, 1.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 CostFuncCombatVectorWeighted(const PathFindContext& pathContext,
								 const NavPathNodeProxy& pathNode,
								 const NavPathNode::Link& link,
								 Point_arg startPosPs,
								 Point_arg goalPosPs,
								 float edgeWeight)
{
	// TODO There's a load of PS in here that should be WS. Sigh, what BS.
	const Vector edgePs	 = goalPosPs - startPosPs;
	const Scalar edgeLen = Length(edgePs);

	const Nav::CombatVectorInfo& cvInfo = pathContext.m_combatVectorInfo;
	const DC::PointCurve* pCurve		= &cvInfo.m_combatVectorCurve;

	// Cs == CombatVectorSpace
	const Point cvPosWs	   = cvInfo.m_combatVectorLocator.GetPosition();
	const Scalar cvScale   = Max(0.01f, cvInfo.m_combatVectorScale);
	const Point startPosCs = cvPosWs + (startPosPs - cvPosWs) / cvScale;
	const Point goalPosCs  = cvPosWs + (goalPosPs - cvPosWs) / cvScale;
	const Vector cvDirWs   = GetLocalZ(cvInfo.m_combatVectorLocator.GetRotation());
	const Vector edgeCs	   = startPosPs - goalPosPs;
	const Vector edgeDirCs = SafeNormalize(edgeCs, kZero);

	Scalar cvToStartDist;
	const Vector cvToStartWs = SafeNormalize(startPosCs - cvPosWs, kZero, cvToStartDist);
	const Vector cvPerpWs	 = RotateY90(cvDirWs);
	const bool flipSides	 = Dot(cvToStartWs, cvPerpWs) < 0.f;
	const F32 cvSideDot		 = Dot(edgeDirCs, flipSides ? cvPerpWs : -cvPerpWs);
	const F32 cvForwardDot	 = Dot(edgeDirCs, cvDirWs);
	const F32 goalInside	 = NdUtil::WeightPositionOnCurve(goalPosCs, cvPosWs, cvDirWs, pCurve);

	const F32 sideScore	   = LerpScale(-0.7f, 0.3f, 2.5f, 0.f, cvSideDot);
	const F32 forwardScore = LerpScale(1.f, 0.2f, 3.f, 0.f, cvForwardDot);
	const F32 insideScore  = LerpScale(0.5f, 1.f, 1.f, 0.f, goalInside);
	const F32 distScore	   = LerpScale(20.f, 30.f, 1.f, 0.1f, cvToStartDist);

	F32 scoreMult = 1.f;

	if (goalInside > 2.f)
		// if we get too far outside the combat vector, start penalising, too!
		scoreMult = LerpScale(2.f, 3.f, 2.f, 4.f, goalInside);
	else
		scoreMult = (1.f + ((sideScore + forwardScore) * insideScore * distScore));

	const F32 finalScore = scoreMult * edgeLen;

	return scoreMult * edgeLen;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CreatePathFindParamsDevMenu(DMENU::Menu* pMenu, PathFindParams* pParamsToUse, const DMENU::ItemEnumPair* pTensionModes)
{
	if (!pMenu || !pParamsToUse)
		return;

	static DMENU::ItemEnumPair s_costModePairs[] =
	{
		DMENU::ItemEnumPair("Brute Force", (I32)PathFindParams::CostMode::kBruteForce),
		DMENU::ItemEnumPair("Robin Hood", (I32)PathFindParams::CostMode::kRobinHood),
		DMENU::ItemEnumPair("Agnostic", (I32)PathFindParams::CostMode::kAgnostic),
		DMENU::ItemEnumPair()
	};

	static DMENU::ItemEnumPair s_playerBlockageCostPairs[] =
	{
		DMENU::ItemEnumPair("Free", (I32)PlayerBlockageCost::kFree),
		DMENU::ItemEnumPair("Cheap", (I32)PlayerBlockageCost::kCheap),
		DMENU::ItemEnumPair("Expensive", (I32)PlayerBlockageCost::kExpensive),
		DMENU::ItemEnumPair("Impassable", (I32)PlayerBlockageCost::kImpassable),
		DMENU::ItemEnumPair()
	};

	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Dynamic Search", &pParamsToUse->m_context.m_dynamicSearch));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Path Radius", &pParamsToUse->m_context.m_pathRadius, DMENU::FloatRange(0.0f, kMaxNavMeshGapDiameter * 0.5f), DMENU::FloatSteps(0.1f, 0.5f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Reverse Search", &pParamsToUse->m_context.m_reverseSearch));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemEnum("Cost Mode", s_costModePairs, DMENU::EditInt, &pParamsToUse->m_costMode));

	if (pTensionModes)
	{
		pMenu->PushBackItem(NDI_NEW DMENU::ItemEnum("Tension Mode", const_cast<DMENU::ItemEnumPair*>(pTensionModes), DMENU::EditInt, &pParamsToUse->m_tensionMode));
	}

	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Find Poly Y-Threshold", &pParamsToUse->m_findPolyYThreshold, DMENU::FloatRange(0.0f, 5.0f), DMENU::FloatSteps(0.1f, 1.0f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Find Poly XZ Cull Radius", &pParamsToUse->m_findPolyXZCullRadius, DMENU::FloatRange(0.0f, 5.0f), DMENU::FloatSteps(0.1f, 1.0f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Max Travel Dist", &pParamsToUse->m_maxTravelDist, DMENU::FloatRange(-1.0f, 1000.0f), DMENU::FloatSteps(1.0f, 10.0f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Max Expansion Dist", &pParamsToUse->m_maxExpansionRadius, DMENU::FloatRange(-1.0f, 1000.0f), DMENU::FloatSteps(1.0f, 10.0f)));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemFloat("Distance Goal", &pParamsToUse->m_distanceGoal, DMENU::FloatRange(0.0f, 100.0f), DMENU::FloatSteps(1.0f, 10.0f)));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemEnum("Player Blockage Cost", s_playerBlockageCostPairs, DMENU::EditInt, &pParamsToUse->m_playerBlockageCost));
	//pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Ignore Node Blockage", &pParamsToUse->m_ignoreNodeBlockage));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Ignore Node Extra Cost", &pParamsToUse->m_ignoreNodeExtraCost));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Debug Draw Search", &pParamsToUse->m_debugDrawSearch));
	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Debug Draw Results", &pParamsToUse->m_debugDrawResults));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugPrintPathFindParams(const PathFindParams& params, MsgOutput output)
{
	STRIP_IN_FINAL_BUILD;

	// m_context
	PrintTo(output, "params.m_context.m_ownerLocPs = Locator(Point(%ff, %ff, %ff), Quat(%ff, %ff, %ff, %ff));\n",
			(float)params.m_context.m_ownerLocPs.Pos().X(),
			(float)params.m_context.m_ownerLocPs.Pos().Y(),
			(float)params.m_context.m_ownerLocPs.Pos().Z(),
			(float)params.m_context.m_ownerLocPs.Rot().X(),
			(float)params.m_context.m_ownerLocPs.Rot().Y(),
			(float)params.m_context.m_ownerLocPs.Rot().Z(),
			(float)params.m_context.m_ownerLocPs.Rot().W());

	PrintTo(output, "params.m_context.m_reverseSearch = %s;\n", params.m_context.m_reverseSearch ? "true" : "false");
	PrintTo(output, "params.m_context.m_hasCombatVector = %s;\n", params.m_context.m_hasCombatVector ? "true" : "false");

	if (params.m_context.m_hasCombatVector)
	{
		PrintTo(output, "params.m_context.m_combatVectorInfo.m_combatVectorLocator = Locator(Point(%ff, %ff, %ff), Quat(%ff, %ff, %ff, %ff));\n",
				(float)params.m_context.m_combatVectorInfo.m_combatVectorLocator.Pos().X(),
				(float)params.m_context.m_combatVectorInfo.m_combatVectorLocator.Pos().Y(),
				(float)params.m_context.m_combatVectorInfo.m_combatVectorLocator.Pos().Z(),
				(float)params.m_context.m_combatVectorInfo.m_combatVectorLocator.Rot().X(),
				(float)params.m_context.m_combatVectorInfo.m_combatVectorLocator.Rot().Y(),
				(float)params.m_context.m_combatVectorInfo.m_combatVectorLocator.Rot().Z(),
				(float)params.m_context.m_combatVectorInfo.m_combatVectorLocator.Rot().W());


		PrintTo(output, "params.m_context.m_combatVectorInfo.m_combatVectorCurve.m_count = %d;\n", params.m_context.m_combatVectorInfo.m_combatVectorCurve.m_count);

		for (U32F i = 0; i < params.m_context.m_combatVectorInfo.m_combatVectorCurve.m_count; ++i)
		{
			PrintTo(output, "params.m_context.m_combatVectorInfo.m_combatVectorCurve.m_keys[%d] = %ff;\n",
					i,
					params.m_context.m_combatVectorInfo.m_combatVectorCurve.m_keys[i]);

			PrintTo(output, "params.m_context.m_combatVectorInfo.m_combatVectorCurve.m_values[%d] = %ff;\n",
					i,
					params.m_context.m_combatVectorInfo.m_combatVectorCurve.m_values[i]);
		}
	}

	PrintTo(output, "params.m_context.m_threatPositionCount = %d;\n", params.m_context.m_threatPositionCount);

	for (U32F i = 0; i < Min(params.m_context.m_threatPositionCount, PathFindContext::kMaxThreats); ++i)
	{
		PrintTo(output, "params.m_context.m_threatPositionsPs[%d] = Point(%ff, %ff, %ff);\n",
				i,
				(float)params.m_context.m_threatPositionsPs[i].X(),
				(float)params.m_context.m_threatPositionsPs[i].Y(),
				(float)params.m_context.m_threatPositionsPs[i].Z());
	}

	PrintTo(output, "params.m_context.m_parentSpace = Locator(Point(%ff, %ff, %ff), Quat(%ff, %ff, %ff, %ff));\n",
			(float)params.m_context.m_parentSpace.Pos().X(),
			(float)params.m_context.m_parentSpace.Pos().Y(),
			(float)params.m_context.m_parentSpace.Pos().Z(),
			(float)params.m_context.m_parentSpace.Rot().X(),
			(float)params.m_context.m_parentSpace.Rot().Y(),
			(float)params.m_context.m_parentSpace.Rot().Z(),
			(float)params.m_context.m_parentSpace.Rot().W());

	if (params.m_context.m_pathCostTypeId)
	{
		PrintTo(output, "params.m_context.m_pathCostTypeId = SID(\"%s\");\n", DevKitOnly_StringIdToString(params.m_context.m_pathCostTypeId));
	}

	StringBuilder<256> blockageMaskAsString;
	Nav::GetStaticBlockageMaskStr(params.m_context.m_obeyedStaticBlockers, &blockageMaskAsString);
	PrintTo(output, "params.m_context.m_obeyedStaticBlockers = 0x%x; // %s\n", params.m_context.m_obeyedStaticBlockers, blockageMaskAsString.c_str());

	for (U32F iStart = 0; iStart < params.m_numStartPositions; ++iStart)
	{
		const U32F ppFlags = kPrettyPrintAppendF | kPrettyPrintInsertCommas | kPrettyPrintUseParens;

		const NavPoly* pPoly = params.m_startPositions[iStart].ToNavPoly();
		const NavMesh* pMesh = params.m_startPositions[iStart].ToNavMesh();

		PrintTo(output, "params.m_startPositions[%d].SetPs(Point%s, NavPolyHandle(0x%x)); // NavMesh: '%s' iPoly: %d\n",
				iStart,
				PrettyPrint(params.m_startPositions[iStart].GetPosPs(), ppFlags),
				params.m_startPositions[iStart].GetNavPolyHandle().AsU64(),
				pMesh ? pMesh->GetName() : "<null>",
				pPoly ? pPoly->GetId() : 0);
	}

	PrintTo(output, "params.m_numStartPositions = %d;\n", params.m_numStartPositions);

	PrintTo(output, "params.m_findPolyYThreshold = %ff;\n", params.m_findPolyYThreshold);
	PrintTo(output, "params.m_findPolyXZCullRadius = %ff;\n", params.m_findPolyXZCullRadius);
	PrintTo(output, "params.m_bindSpawnerNameId = StringId64(0x%.16llx);\n", params.m_bindSpawnerNameId.GetValue());
	PrintTo(output, "params.m_traversalSkillMask = 0x%x;\n", params.m_traversalSkillMask);
	PrintTo(output, "params.m_tensionMode = %d;\n", params.m_tensionMode);
	PrintTo(output, "params.m_factionMask = 0x%x;\n", params.m_factionMask);

	PrintTo(output, "params.m_hPreferredTap.InitFromId(0x%x);\n", params.m_hPreferredTap.GetMgrId());
	PrintTo(output, "params.m_maxTravelDist = %ff;\n", params.m_maxTravelDist);
	PrintTo(output, "params.m_maxExpansionRadius = %ff;\n", params.m_maxExpansionRadius);
	PrintTo(output, "params.m_costMode = Nav::PathFindParams::CostMode(%d);\n", params.m_costMode);
	PrintTo(output, "params.m_distanceGoal = %ff;\n", params.m_distanceGoal);

	switch (params.m_playerBlockageCost)
	{
	case PlayerBlockageCost::kFree:
		PrintTo(output, "params.m_playerBlockageCost = Nav::PlayerBlockageCost::kFree;\n");
		break;
	case PlayerBlockageCost::kCheap:
		PrintTo(output, "params.m_playerBlockageCost = Nav::PlayerBlockageCost::kCheap;\n");
		break;
	case PlayerBlockageCost::kExpensive:
		PrintTo(output, "params.m_playerBlockageCost = Nav::PlayerBlockageCost::kExpensive;\n");
		break;
	case PlayerBlockageCost::kImpassable:
		PrintTo(output, "params.m_playerBlockageCost = Nav::PlayerBlockageCost::kImpassable;\n");
		break;
	}

	PrintTo(output, "params.m_ignoreNodeExtraCost = %s;\n", params.m_ignoreNodeExtraCost ? "true" : "false");
	PrintTo(output, "params.m_debugDrawSearch = %s;\n", params.m_debugDrawSearch ? "true" : "false");
	PrintTo(output, "params.m_debugDrawResults = %s;\n", params.m_debugDrawResults ? "true" : "false");

	PrintTo(output, "params.m_context.m_pathRadius = %ff;\n", params.m_context.m_pathRadius);
	PrintTo(output, "params.m_context.m_dynamicSearch = %s;\n", params.m_context.m_dynamicSearch ? "true" : "false");

	for (U32F i = 0; i < params.m_context.m_obeyedBlockers.GetNumBlocks(); ++i)
	{
		PrintTo(output, "params.m_context.m_obeyedBlockers.SetBlock(%d, 0x%x);\n", i, params.m_context.m_obeyedBlockers.GetBlock(i));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugPrintFindSinglePathParams(const FindSinglePathParams& params, MsgOutput output)
{
	STRIP_IN_FINAL_BUILD;

	DebugPrintPathFindParams(params, output);

	//PrintTo(output, "params.m_goal = ");
	//params.m_goal.DebugPrint(GetMsgOutput(output));
	//PrintTo(output, ";\n");

	const U32F ppFlags = kPrettyPrintAppendF | kPrettyPrintInsertCommas | kPrettyPrintUseParens;
	const NavPoly* pPoly = params.m_goal.ToNavPoly();
	const NavMesh* pMesh = params.m_goal.ToNavMesh();

	PrintTo(output, "params.m_goal.SetPs(Point%s, NavPolyHandle(NavManagerId(0x%x))); // NavMesh: '%s' iPoly: %d\n",
			PrettyPrint(params.m_goal.GetPosPs(), ppFlags),
			params.m_goal.GetNavPolyHandle().AsU64(),
			pMesh ? pMesh->GetName() : "<null>",
			pPoly ? pPoly->GetId() : 0);

	DebugPrintBuildPathParams(params.m_buildParams, output, "params.m_buildParams");
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugPrintFindSplinePathParams(const FindSplinePathParams& params, MsgOutput output)
{
	STRIP_IN_FINAL_BUILD;

	DebugPrintPathFindParams(params, output);

	PrintTo(output, "params.m_pPathSpline = %s;\n", params.m_pPathSpline ? params.m_pPathSpline->GetBareName() : "<null>");
	PrintTo(output, "params.m_arcStart = %ff;\n", params.m_arcStart);
	PrintTo(output, "params.m_arcGoal = %ff;\n", params.m_arcGoal);
	PrintTo(output, "params.m_arcStep = %ff;\n", params.m_arcStep);
	PrintTo(output, "params.m_arcObstacleStep = %ff;\n", params.m_arcObstacleStep);
	PrintTo(output, "params.m_onSplineRadius = %ff;\n", params.m_onSplineRadius);

	DebugPrintBuildPathParams(params.m_buildParams, output, "params.m_buildParams");
}

} // namespace Nav
