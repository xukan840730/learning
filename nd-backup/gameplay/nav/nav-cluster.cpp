/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-cluster.h"

#include "corelib/containers/hashtable.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "gamelib/gameplay/nav/nav-ledge-graph-handle.h"
#include "gamelib/gameplay/nav/nav-ledge-graph.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-node-table.h"
#include "gamelib/gameplay/nav/nav-path-find.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"

namespace Nav
{

void PolyCluster::AddPoly(NavPolyHandle hPoly, Point_arg posWs)
{
	NAV_ASSERT(m_numPolys < kMaxPolysPerCluster);
	m_hPolys[m_numPolys] = hPoly;
	m_numPolys++;

	m_bounds.IncludePoint(posWs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
class ClusterOpenList
{
public:
	void Init(size_t maxSize, const char* sourceFile, U32F sourceLine, const char* sourceFunc);

	void TryUpdateCost(const NavNodeKey& k, const NavNodeData& d, Point_arg nodePosWs, U32 curClusterIdx);

	bool RemoveBestNode(NavNodeKey* pNodeOut,
						NavNodeData* pDataOut,
						const Aabb& curBounds,
						U32 curClusterIdx,
						F32 maxClusterAxis,
						bool* pValidForCluster);

	bool Remove(const NavNodeKey& key, NavNodeData* pDataOut = nullptr);

private:
	struct FlatTable
	{
		FlatTable()
			: m_pCostArray(nullptr)
			, m_pClusterIdxArray(nullptr)
			, m_pPosArray(nullptr)
			, m_pKeyList(nullptr)
			, m_pDataList(nullptr)
			, m_count(0)
			, m_capacity(0)
		{
		}

		F32* m_pCostArray;
		U32* m_pClusterIdxArray;
		Point* m_pPosArray;
		NavNodeKey* m_pKeyList;
		NavNodeData* m_pDataList;
		size_t m_count;
		size_t m_capacity;
	};

	I32 LookupTableIndex(const NavNodeKey& k) const;

	FlatTable m_flatTable;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void ClusterOpenList::Init(size_t maxSize, const char* sourceFile, U32F sourceLine, const char* sourceFunc)
{
	m_flatTable.m_pCostArray	   = NDI_NEW F32[maxSize];
	m_flatTable.m_pClusterIdxArray = NDI_NEW U32[maxSize];
	m_flatTable.m_pPosArray		   = NDI_NEW Point[maxSize];
	m_flatTable.m_pKeyList		   = NDI_NEW NavNodeKey[maxSize];
	m_flatTable.m_pDataList		   = NDI_NEW NavNodeData[maxSize];

	m_flatTable.m_capacity = maxSize;
	m_flatTable.m_count	   = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32 ClusterOpenList::LookupTableIndex(const NavNodeKey& k) const
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
void ClusterOpenList::TryUpdateCost(const NavNodeKey& k, const NavNodeData& d, Point_arg nodePosWs, U32 curClusterIdx)
{
	//PROFILE_AUTO(Navigation);
	PROFILE_ACCUM(ClusterTryUpdateCost);

	NAV_ASSERT(g_navPathNodeMgr.GetNode(k.GetPathNodeId()).GetNavMeshHandle() == d.m_pathNode.GetNavMeshHandle());

	NavKeyDataPair pair;
	pair.m_key				 = k;
	pair.m_data				 = d;
	const F32 combinedCost =  + d.m_toCost;

	const I32 tableIndex = LookupTableIndex(k);

	if (tableIndex < 0)
	{
		m_flatTable.m_pCostArray[m_flatTable.m_count]		= d.m_fromCost;
		m_flatTable.m_pClusterIdxArray[m_flatTable.m_count] = curClusterIdx;
		m_flatTable.m_pPosArray[m_flatTable.m_count]		= nodePosWs;
		m_flatTable.m_pKeyList[m_flatTable.m_count]			= k;
		m_flatTable.m_pDataList[m_flatTable.m_count]		= d;
		m_flatTable.m_count++;
	}
	else
	{
		NAV_ASSERT(m_flatTable.m_pKeyList[tableIndex] == k);
		NavNodeData& existingData = m_flatTable.m_pDataList[tableIndex];

		if (d.m_fromCost < existingData.m_fromCost)
		{
			m_flatTable.m_pCostArray[tableIndex]	   = d.m_fromCost;
			m_flatTable.m_pPosArray[tableIndex]		   = nodePosWs;
			m_flatTable.m_pClusterIdxArray[tableIndex] = curClusterIdx;
			m_flatTable.m_pDataList[tableIndex]		   = d;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ClusterOpenList::RemoveBestNode(NavNodeKey* pNodeOut,
									 NavNodeData* pDataOut,
									 const Aabb& curBounds,
									 U32 curClusterIdx,
									 F32 maxClusterAxis,
									 bool* pValidForCluster)
{
	// PROFILE_AUTO(Navigation);
	PROFILE_ACCUM(ClusterRemoveBestNode);

	F32 bestCost  = kLargeFloat;
	I32 bestIndex = -1;

	bool foundInCluster = false;

	for (size_t i = 0; i < m_flatTable.m_count; ++i)
	{
		const F32 cost		 = m_flatTable.m_pCostArray[i];
		const Point posWs	 = m_flatTable.m_pPosArray[i];
		const U32 clusterIdx = m_flatTable.m_pClusterIdxArray[i];
		const NavNodeData& d = m_flatTable.m_pDataList[i];

		bool validForCluster = (d.m_pathNode.IsActionPackNode()) || curBounds.ContainsPoint(posWs);

		if (!validForCluster)
		{
			Aabb newBounds = curBounds;
			newBounds.IncludePoint(posWs);

			const Vector boundsSize = VectorXz(newBounds.GetSize());
			validForCluster			= (MaxComp(boundsSize) < maxClusterAxis) && (clusterIdx == curClusterIdx);
		}

		bool better = false;

		if (!foundInCluster && validForCluster)
		{
			better = true;
		}
		else if (cost < bestCost)
		{
			better = (foundInCluster == validForCluster);
		}

		if (better)
		{
			bestCost  = cost;
			bestIndex = i;
		}

		foundInCluster |= validForCluster;
	}

	if (bestIndex >= 0)
	{
		if (pNodeOut)
			*pNodeOut = m_flatTable.m_pKeyList[bestIndex];

		if (pDataOut)
			*pDataOut = m_flatTable.m_pDataList[bestIndex];

		if (pValidForCluster)
			*pValidForCluster = foundInCluster;

		{
			// replace removed element in flat table with last element
			const size_t lastIndex = m_flatTable.m_count - 1;

			m_flatTable.m_pCostArray[bestIndex]		  = m_flatTable.m_pCostArray[lastIndex];
			m_flatTable.m_pPosArray[bestIndex]		  = m_flatTable.m_pPosArray[lastIndex];
			m_flatTable.m_pClusterIdxArray[bestIndex] = m_flatTable.m_pClusterIdxArray[lastIndex];
			m_flatTable.m_pKeyList[bestIndex]		  = m_flatTable.m_pKeyList[lastIndex];
			m_flatTable.m_pDataList[bestIndex]		  = m_flatTable.m_pDataList[lastIndex];

			--m_flatTable.m_count;
		}

		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ClusterOpenList::Remove(const NavNodeKey& key, NavNodeData* pDataOut /* = nullptr */)
{
	const I32F foundIndex = LookupTableIndex(key);

	if (foundIndex < 0)
		return false;

	if (pDataOut)
		*pDataOut = m_flatTable.m_pDataList[foundIndex];

	const size_t lastIndex = m_flatTable.m_count - 1;

	m_flatTable.m_pCostArray[foundIndex]	   = m_flatTable.m_pCostArray[lastIndex];
	m_flatTable.m_pPosArray[foundIndex]		   = m_flatTable.m_pPosArray[lastIndex];
	m_flatTable.m_pClusterIdxArray[foundIndex] = m_flatTable.m_pClusterIdxArray[lastIndex];
	m_flatTable.m_pKeyList[foundIndex]		   = m_flatTable.m_pKeyList[lastIndex];
	m_flatTable.m_pDataList[foundIndex]		   = m_flatTable.m_pDataList[lastIndex];

	m_flatTable.m_count--;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DoClustering(const ClusterPolyParams& clusterParams,
				  const PathFindLocation& startLocation,
				  ClusterPolyResults* pResults)
{
	PROFILE_AUTO(AI);
	PROFILE_ACCUM(DoClusterByTravelDist);

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	ClusterPolyResults& ret	   = *pResults;
	ret.m_overflowedClosedList = false;

	ClusterOpenList openList;
	openList.Init(2048, FILE_LINE_FUNC);

	NavNodeTable closedList;
	closedList.Init(2048, FILE_LINE_FUNC);

	const U32 maxClusters		  = clusterParams.m_maxClusters;
	const F32 maxTravelDist = (clusterParams.m_maxTravelDist >= 0.0f) ? clusterParams.m_maxTravelDist : NDI_FLT_MAX;
	const F32 maxExpansionRadius = clusterParams.m_maxExpansionRadius;

	const F32 maxAxis = clusterParams.m_maxClusterAxis;
	const F32 minAxis = clusterParams.m_minClusterAxis;

	const NavPathNode::Link* pathLinks = &g_navPathNodeMgr.GetLink(0);

	U32 numClusters = 0;
	PolyCluster cluster;
	cluster.Clear();

	// setup the start node
	{
		const NavPathNode* pStartNode = &g_navPathNodeMgr.GetNode(startLocation.m_pathNodeId);
		const Point startPosPs		  = clusterParams.m_context.m_parentSpace.UntransformPoint(startLocation.m_posWs);

		const NavNodeKey startNode = NavNodeKey(startLocation, clusterParams.m_context);

		NavNodeData startData;
		startData.m_parentNode = NavNodeKey(NavPathNodeMgr::kInvalidNodeId, 0);
		startData.m_fromDist   = 0.0f;
		startData.m_fromCost   = startLocation.m_baseCost;
		startData.m_toCost	   = 0.0f;
		startData.m_fringeNode = false;
		startData.m_pathNode.Init(*pStartNode);

		startData.m_pathNodePosPs = startPosPs;

		openList.TryUpdateCost(startNode, startData, startLocation.m_posWs, numClusters);
	}

	NavNodeKey curNode;
	NavNodeData curData;
	bool validForCluster = false;

	while (!ret.m_overflowedClosedList
		   && openList.RemoveBestNode(&curNode, &curData, cluster.GetBounds(), numClusters, maxAxis, &validForCluster))
	{
		if (closedList.IsFull())
		{
			MsgAiWarn("Overflowed A* closed list (encountered more than %d nodes)\n", closedList.Size());
			ret.m_overflowedClosedList = true;
			break;
		}

		NAV_ASSERT(g_navPathNodeMgr.GetNode(curNode.GetPathNodeId()).GetNavMeshHandle() == curData.m_pathNode.GetNavMeshHandle());

		if (cluster.IsFull() || !validForCluster)
		{
			if (cluster.IsValid(minAxis))
			{
				ret.StoreCluster(cluster, numClusters);
				numClusters++;
			}

			cluster.Clear();

			if (numClusters >= maxClusters)
				break;

			curData.m_fromCost = 0.f;
			curData.m_fromDist = 0.f;
			curData.m_toCost   = 0.f;
		}

		NavNodeTable::Iterator curNodeIter = closedList.Set(curNode, curData);

		const U32F			iCurNode = curNode.GetPathNodeId();
		const NavPathNode*	pCurNode = &g_navPathNodeMgr.GetNode(iCurNode);

		NAV_ASSERTF(!pCurNode->IsCorrupted(), ("Path Node '%d' is corrupted", iCurNode));
		NAV_ASSERTF(pCurNode->IsValid(), ("Path Node '%d' is corrupted", iCurNode));

		const Point curNodePosPs = curData.m_pathNodePosPs;
		const NavMesh* pCurNavMesh = pCurNode->GetNavMeshHandle().ToNavMesh();

		if (pCurNode->IsNavMeshNode())
		{
			const Point curNodePosWs = pCurNavMesh->ParentToWorld(curNodePosPs);
			cluster.AddPoly(pCurNode->GetNavPolyHandle(), curNodePosWs);
		}

		for (U32F iLink = pCurNode->GetFirstLinkId(); iLink != NavPathNodeMgr::kInvalidLinkId; iLink = pathLinks[iLink].m_nextLinkId)
		{
			NAV_ASSERT(iLink < g_navPathNodeMgr.GetMaxLinkCount());
			if (iLink >= g_navPathNodeMgr.GetMaxLinkCount())
				break;

			const NavPathNode::Link& link = pathLinks[iLink];
			const U32F iAdjNode = link.m_nodeId;

			NAV_ASSERT(iAdjNode < g_navPathNodeMgr.GetMaxNodeCount());
			if (iAdjNode >= g_navPathNodeMgr.GetMaxNodeCount())
				break;

			const NavPathNode* pAdjNode = &g_navPathNodeMgr.GetNode(iAdjNode);

			NAV_ASSERTF(!pAdjNode->IsCorrupted(), ("Path Node '%d' is corrupted (link '%d' from '%d')", iAdjNode, iLink, iCurNode));
			NAV_ASSERTF(pAdjNode->IsValid(), ("Path Node '%d' is invalid (link '%d' from '%d')", iAdjNode, iLink, iCurNode));

			if (clusterParams.m_restrictNodes && !clusterParams.m_validNodes.IsBitSet(iAdjNode))
				continue;

			if (pAdjNode->IsBlocked(clusterParams.m_context.m_obeyedStaticBlockers))
				continue;

			if (link.m_type == NavPathNode::kLinkTypeIncoming)
				continue;

			const NavMesh* pAdjNavMesh = pAdjNode->GetNavMeshHandle().ToNavMesh();
#if ENABLE_NAV_LEDGES
			const NavLedgeGraph* pAdjNavLedgeGraph = pAdjNode->GetNavLedgeHandle().ToLedgeGraph();
#endif // ENABLE_NAV_LEDGES

			if (!pAdjNavMesh
#if ENABLE_NAV_LEDGES
				&& !pAdjNavLedgeGraph
#endif // ENABLE_NAV_LEDGES
				)
				continue;

			if (link.IsDynamic())
				continue;

			const Point adjNodePosPs = pAdjNode->GetPositionPs();
			const NavNodeKey adjNode = NavNodeKey(iAdjNode, 0);

			if (closedList.Find(adjNode) != closedList.End())
				continue;

			if (pAdjNode->IsActionPackNode())
			{
				const TraversalActionPack* pTap = static_cast<const TraversalActionPack*>(pAdjNode->GetActionPack());

				const bool isFactionValid	  = pTap->IsFactionIdMaskValid(clusterParams.m_factionMask);
				const bool isTensionModeValid = pTap->IsTensionModeValid(clusterParams.m_tensionMode);
				const bool isSkillValid		  = pTap->IsTraversalSkillMaskValid(clusterParams.m_traversalSkillMask);
				const bool isPlayerBlocked	  = clusterParams.m_playerBlockageCost == PlayerBlockageCost::kImpassable && pTap->IsPlayerBlocked();

				if (!isFactionValid || !isTensionModeValid || !isSkillValid || isPlayerBlocked)
				{
					// can't cross this TAP, mark it closed and carry on
					NavNodeData adjNodeData;
					const bool removed = openList.Remove(adjNode, &adjNodeData);

					if (closedList.IsFull())
					{
						ret.m_overflowedClosedList = true;
						break;
					}
					else if (removed)
					{
						NAV_ASSERT(g_navPathNodeMgr.GetNode(adjNode.GetPathNodeId()).GetNavMeshHandle()
								   == adjNodeData.m_pathNode.GetNavMeshHandle());
						closedList.Set(adjNode, adjNodeData);
					}
					else
					{
						NavNodeData newData;
						newData.m_parentNode	= curNode;
						newData.m_fromCost		= -1.0f;
						newData.m_fromDist		= -1.0f;
						newData.m_pathNodePosPs = adjNodePosPs;
						newData.m_toCost		= 0.0f;
						newData.m_fringeNode	= false;
						newData.m_pathNode.Init(*pAdjNode);

						NAV_ASSERT(g_navPathNodeMgr.GetNode(adjNode.GetPathNodeId()).GetNavMeshHandle()
								   == newData.m_pathNode.GetNavMeshHandle());
						closedList.Set(adjNode, newData);
					}

					continue;
				}
			}

			if (maxExpansionRadius >= 0.f)
			{
#if ENABLE_NAV_LEDGES
				const Point startPosPs = pAdjNavMesh ? pAdjNavMesh->WorldToParent(startLocation.m_posWs)
													 : pAdjNavLedgeGraph->WorldToParent(startLocation.m_posWs);
#else
				const Point startPosPs = pAdjNavMesh->WorldToParent(startLocation.m_posWs);
#endif // ENABLE_NAV_LEDGES

				if (Dist(adjNodePosPs, startPosPs) > maxExpansionRadius)
					continue;
			}

			const F32 thisDist = Dist(curNodePosPs, adjNodePosPs);
			const F32 fromDist = curData.m_fromDist + thisDist;

			if (fromDist >= maxTravelDist)
				continue;

			const F32 thisCost = thisDist;
			const F32 fromCost = curData.m_fromCost + thisCost;

			NavNodeData newData;
			newData.m_parentNode	= curNode;
			newData.m_toCost		= 0.f; // no goal so no heuristic cost
			newData.m_fromCost		= fromCost;
			newData.m_fromDist		= fromDist;
			newData.m_pathNodePosPs = adjNodePosPs;
			newData.m_fringeNode	= false;
			newData.m_pathNode.Init(*pAdjNode);

			NAV_ASSERT(g_navPathNodeMgr.GetNode(adjNode.GetPathNodeId()).GetNavMeshHandle()
					   == newData.m_pathNode.GetNavMeshHandle());

#if ENABLE_NAV_LEDGES
			const Point adjNodePosWs = pAdjNavMesh ? pAdjNavMesh->ParentToWorld(adjNodePosPs)
				: pAdjNavLedgeGraph->ParentToWorld(adjNodePosPs);
#else
			const Point adjNodePosWs = pAdjNavMesh->ParentToWorld(adjNodePosPs);
#endif // ENABLE_NAV_LEDGES

			const U32 clusterIdx = pAdjNode->IsNavMeshNode() ? numClusters : -1;

			openList.TryUpdateCost(adjNode, newData, adjNodePosWs, clusterIdx);
		}
	}

	if (cluster.IsValid(minAxis))
	{
		ret.StoreCluster(cluster, numClusters);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ClusterPolys(const ClusterPolyParams& clusterParams, ClusterPolyResults* pClusterResults)
{
	PROFILE_ACCUM(FindClusters);

	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	PROFILE_AUTO(AI);

	PathFindLocation aStartLocations[PathFindParams::kMaxStartPositions];
	NavMeshHandle hFirstStartNavMesh = NavMeshHandle();
	const U32F numStartLocations	 = GatherStartLocations(clusterParams,
															aStartLocations,
															PathFindParams::kMaxStartPositions,
															&hFirstStartNavMesh);

	if (!g_navPathNodeMgr.Failed() && numStartLocations == 1)
	{
		DoClustering(clusterParams, aStartLocations[0], pClusterResults);
	}
}

} // namespace Nav

