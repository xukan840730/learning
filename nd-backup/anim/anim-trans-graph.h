/*
* Copyright (c) 2017 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "corelib/containers/hashtable.h"
#include "corelib/containers/list-array.h"

#include "gamelib/scriptx/h/anim-trans-graph-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimStateTransGraph
{
public:

	// a path from src-node to dst-node.
	// the first node will be src-node and the last node is the dst-node.
	// if number of nodes is less than 2, path is not found.
	struct Path
	{
		void Clear() { m_nodes.Clear(); }
		void InsertAtTop(StringId64 newNode) { m_nodes.Insert(m_nodes.Begin(), newNode); }
		ListArray<StringId64> m_nodes;
	};

	AnimStateTransGraph()
		: m_settingsId(INVALID_STRING_ID_64)
	{}

	void Init(StringId64 settingsId);
	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	void FindPath(StringId64 srcNode, StringId64 dstNode, Path& outPath);

	void DebugDraw() const;

private:
	void BuildGraph();
	void ClearGraph();

	I32 FindNodeIndex(StringId64 nodeName) const;
	void GatherNeighbors(StringId64 node, ListArray<StringId64>& outNeighbors) const;

	StringId64 m_settingsId;

	static const I32 kMaxNumNodes = 16;
	static const I32 kMaxNumLinks = 32;

	ListArray<StringId64> m_nodes;
	I32 m_numLinks;

	I8 m_incidence[kMaxNumNodes][kMaxNumLinks]; // node is row, link is column
	void GetSrcDstNodeIdx(I32 iLink, I32& srcNodeIdx, I32& dstNodeIdx) const;

	// path finding related
	typedef StringId64 PathFindingNode;

	typedef ListArray<PathFindingNode> PathFindingNodeSet;

	struct PathFindingTableEntry
	{
		StringId64 thisNode;
		StringId64 fromNode;
		float gScore;
	};

	typedef HashTable<StringId64, PathFindingTableEntry> PathFindingCostTable;

	void ReconstructPath(const PathFindingCostTable& costTable, StringId64 srcNode, StringId64 dstNode, Path& outPath);

	static void AddNode(PathFindingNodeSet& nodeSet, PathFindingNode newNode);
	static void RemoveNode(PathFindingNodeSet& nodeSet, PathFindingNode nodeToRemove);
	static PathFindingNode FindLowestScoreNode(const PathFindingNodeSet& nodeSet, const PathFindingCostTable& costTable);
	static PathFindingTableEntry* FindEntry(PathFindingCostTable& table, StringId64 entryName);
	static const PathFindingTableEntry* FindEntry(const PathFindingCostTable& table, StringId64 entryName);

	static void DebugDrawArrow(Vec2_arg start, Vec2_arg end, Color color);
};
