/*
* Copyright (c) 2017 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/anim/anim-trans-graph.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/render/util/text.h"
#include "ndlib/script/script-manager.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateTransGraph::Init(StringId64 settingsId)
{
	m_settingsId = settingsId;
	
	StringId64* blocks = NDI_NEW StringId64[kMaxNumNodes];
	m_nodes.Init(kMaxNumNodes, blocks);

	BuildGraph();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateTransGraph::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	m_nodes.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateTransGraph::ClearGraph()
{
	m_nodes.Clear();
	memset(m_incidence, 0, sizeof(m_incidence));
	m_numLinks = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateTransGraph::BuildGraph()
{
	ClearGraph();

	const DC::AnimTransGraph* pGraph = ScriptManager::Lookup<DC::AnimTransGraph>(m_settingsId);
	if (pGraph)
	{
		// nodes!
		for (I32 ii = 0; ii < pGraph->m_nodes->m_count; ii++)
		{
			const DC::AnimTransGraphNode& node = pGraph->m_nodes->m_array[ii];
			m_nodes.PushBack(node.m_name);
		}

		// links!
		for (I32 ii = 0; ii < pGraph->m_links->m_count; ii++)
		{
			const DC::AnimTransGraphLink& link = pGraph->m_links->m_array[ii];
			I32 srcRow = FindNodeIndex(link.m_srcNode);
			if (srcRow < 0)
				continue;

			I32 dstRow = FindNodeIndex(link.m_dstNode);
			if (dstRow < 0)
				continue;

			m_incidence[srcRow][m_numLinks] = -1;
			m_incidence[dstRow][m_numLinks] = 1;
			m_numLinks++;
			ANIM_ASSERT(m_numLinks <= kMaxNumLinks);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32 AnimStateTransGraph::FindNodeIndex(StringId64 nodeName) const
{
	for (I32 ii = 0; ii < m_nodes.Size(); ii++)
		if (m_nodes[ii] == nodeName)
			return ii;
	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateTransGraph::FindPath(StringId64 srcNode, StringId64 dstNode, Path& outPath)
{
	ScopedTempAllocator jj(FILE_LINE_FUNC);

	outPath.Clear();

	StringId64 closedSetData[kMaxNumNodes];
	StringId64 openSetData[kMaxNumNodes];
	StringId64 neighborNodeData[kMaxNumNodes];
	PathFindingNodeSet closedSet(kMaxNumNodes, closedSetData);
	PathFindingNodeSet openSet(kMaxNumNodes, openSetData);

	closedSet.Clear();
	openSet.PushBack(PathFindingNode(srcNode));

	PathFindingCostTable costTable;
	costTable.Init(kMaxNumNodes, FILE_LINE_FUNC);

	// initialize cost table
	for (I32 ii = 0; ii < m_nodes.Size(); ii++)
	{
		StringId64 nodeName = m_nodes[ii];

		PathFindingTableEntry newEntry;
		newEntry.thisNode = nodeName;
		newEntry.fromNode = INVALID_STRING_ID_64;
		
		bool isSrcNode = srcNode == nodeName;
		newEntry.gScore = isSrcNode ? 0.f : FLT_MAX;

		costTable.Set(nodeName, newEntry);
	}

	bool firstLoop = true;

	while (openSet.Size() > 0)
	{
		PathFindingNode currNode = FindLowestScoreNode(openSet, costTable);
		if (currNode == dstNode && !firstLoop)
		{
			ReconstructPath(costTable, srcNode, currNode, outPath);
			return;
		}

		PathFindingTableEntry& currEntry = *FindEntry(costTable, currNode);

		RemoveNode(openSet, currNode);
		bool firstNodeIsDstNode = firstLoop && (currNode == dstNode);
		if (!firstNodeIsDstNode) // if src-node and dst-node are the same, don't add dst-node into closedSet. it's like we path find from the first neighbors of dst-node.
			AddNode(closedSet, currNode);

		ListArray<StringId64> neighborNodes(kMaxNumNodes, neighborNodeData);
		GatherNeighbors(currNode, neighborNodes);

		for (ListArray<StringId64>::ConstIterator it = neighborNodes.Begin(); it != neighborNodes.End(); ++it)
		{
			StringId64 neighbor = *it;

			if (Contains(closedSet, neighbor))
				continue;

			PathFindingTableEntry& neighborEntry = *FindEntry(costTable, neighbor);

			float tentativeScore = currEntry.gScore + 1; // assume distance between each nodes is always 1

			if (!Contains(openSet, neighbor))
				AddNode(openSet, neighbor); // discover a new node.
			else if (tentativeScore >= neighborEntry.gScore)
				continue;  // there was a better path.

			neighborEntry.fromNode = currNode;
			neighborEntry.gScore = tentativeScore;
		}

		firstLoop = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateTransGraph::ReconstructPath(const PathFindingCostTable& costTable,
										  StringId64 srcNode,
										  StringId64 dstNode,
										  Path& outPath)
{
	outPath.Clear();

	StringId64 currNode = dstNode;
	
	do 
	{
		const PathFindingTableEntry& currEntry = *FindEntry(costTable, currNode);
		outPath.InsertAtTop(currEntry.thisNode);
		currNode = currEntry.fromNode;

		// prevent infinite loop if src-node and dst-node are the same.
		if (currEntry.fromNode == srcNode)
			break;

	} while (currNode != INVALID_STRING_ID_64);

	if (outPath.m_nodes.Size() > 0 && outPath.m_nodes[0] != srcNode)
		outPath.InsertAtTop(srcNode);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateTransGraph::AddNode(PathFindingNodeSet& nodeSet, PathFindingNode newNode)
{
	if (!Contains(nodeSet, newNode))
		nodeSet.PushBack(newNode);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateTransGraph::RemoveNode(PathFindingNodeSet& nodeSet, PathFindingNode nodeToRemove)
{
	PathFindingNodeSet::iterator it = nodeSet.Begin();
	while (it != nodeSet.End())
	{
		PathFindingNode node = *it;
		if (node == nodeToRemove)
		{
			it = nodeSet.Erase(it);
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateTransGraph::PathFindingNode AnimStateTransGraph::FindLowestScoreNode(const PathFindingNodeSet& nodeSet,
																			  const PathFindingCostTable& costTable)
{
	float lowestC = FLT_MAX;
	PathFindingNode bestNode = INVALID_STRING_ID_64;

	for (PathFindingNodeSet::ConstIterator it = nodeSet.Begin(); it != nodeSet.End(); ++it)
	{
		const PathFindingNode& node = *it;

		const PathFindingTableEntry& entry = *FindEntry(costTable, node);
		float fScore = entry.gScore;

		if (fScore < lowestC)
		{
			bestNode = node;
			lowestC = fScore;
		}
	}

	return bestNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateTransGraph::GatherNeighbors(StringId64 node, ListArray<StringId64>& outNeighbors) const
{
	outNeighbors.Clear();

	I32 nodeIdx = FindNodeIndex(node);
	if (nodeIdx < 0)
		return;

	for (I32 iLink = 0; iLink < m_numLinks; iLink++)
	{
		I32 srcNodeIdx, dstNodeIdx;
		GetSrcDstNodeIdx(iLink, srcNodeIdx, dstNodeIdx);

		if (srcNodeIdx == nodeIdx)
			outNeighbors.PushBack(m_nodes[dstNodeIdx]);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateTransGraph::GetSrcDstNodeIdx(I32 iLink, I32& srcNodeIdx, I32& dstNodeIdx) const
{
	if (iLink < m_numLinks)
	{
		for (I32 row = 0; row < m_nodes.Size(); row++)
		{
			if (m_incidence[row][iLink] < 0)
				srcNodeIdx = row;
			if (m_incidence[row][iLink] > 0)
				dstNodeIdx = row;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateTransGraph::PathFindingTableEntry* AnimStateTransGraph::FindEntry(PathFindingCostTable& table,
																		   StringId64 entryName)
{
	PathFindingCostTable::Iterator it = table.Find(entryName);
	return &it->m_data;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateTransGraph::PathFindingTableEntry* AnimStateTransGraph::FindEntry(const PathFindingCostTable& table,
																				 StringId64 entryName)
{
	PathFindingCostTable::ConstIterator it = table.Find(entryName);
	return &it->m_data;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateTransGraph::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	if (!g_animOptions.m_debugDrawAnimGraph)
		return;

	if (m_nodes.Size() == 0)
		return;

	Vec2 scGridOrig(640.f, 320.f);

	I32 gridSize = Ceil(Sqrt(m_nodes.Size()));
	float gridDist = 200.f;

	Vec2 blocks[kMaxNumNodes];
	ListArray<Vec2> nodeCenterPos(kMaxNumNodes, blocks);

	// nodes!
	for (I32 iNode = 0; iNode < m_nodes.Size(); iNode++)
	{
		I32 rowIdx = iNode / gridSize;
		I32 colIdx = iNode % gridSize;

		Vec2 center = scGridOrig + Vec2(0.f, gridDist) * rowIdx + Vec2(gridDist, 0.f) * colIdx;
		nodeCenterPos.PushBack(center);
		g_prim.Draw(DebugString2D(center, kDebug2DLegacyCoords, DevKitOnly_StringIdToString(m_nodes[iNode]), kColorWhite, 0.5f, kFontCenter));
	}

	// links!
	for (I32 iLink = 0; iLink < m_numLinks; iLink++)
	{
		I32 srcNodeIdx, dstNodeIdx;
		GetSrcDstNodeIdx(iLink, srcNodeIdx, dstNodeIdx);

		Vec2 start = nodeCenterPos[srcNodeIdx];
		Vec2 end = nodeCenterPos[dstNodeIdx];

		Vec2 dir = end - start;
		Vec2 dirNorm = SafeNormalize(dir, kZero);
		float dirLen = Length(dir);

		if (dirLen > 20.f * 2)
		{
			start = start + dirNorm * 20.f;
			end = end - dirNorm * 20.f;
		}

		DebugDrawArrow(start, end, kColorWhite);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateTransGraph::DebugDrawArrow(Vec2_arg start, Vec2_arg end, Color color)
{
	g_prim.Draw(DebugLine2D(start, end, kDebug2DLegacyCoords, color));

	float thetaRad = DEGREES_TO_RADIANS(15.f);

	Vec2 arrowDir = SafeNormalize(start - end, kZero) * 10.f;
	Vec2 side0 = Vec2(arrowDir.x * Cos(thetaRad) - arrowDir.y * Sin(thetaRad), arrowDir.x * Sin(thetaRad) + arrowDir.y * Cos(thetaRad));
	Vec2 side1 = Vec2(arrowDir.x * Cos(-thetaRad) - arrowDir.y * Sin(-thetaRad), arrowDir.x * Sin(-thetaRad) + arrowDir.y * Cos(-thetaRad));

	g_prim.Draw(DebugLine2D(end, end + side0, kDebug2DLegacyCoords, color));
	g_prim.Draw(DebugLine2D(end, end + side1, kDebug2DLegacyCoords, color));
}
