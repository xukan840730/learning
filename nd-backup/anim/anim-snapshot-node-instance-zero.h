/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/snapshot-node-heap.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class AnimStateSnapshot;
class ArtItemSkeleton;
struct AnimCmdGenInfo;
struct AnimNodeDebugPrintData;
struct SnapshotAnimNodeTreeParams;
struct SnapshotAnimNodeTreeResults;

namespace DC
{
	struct AnimNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSnapshotNodeInstanceZero : public AnimSnapshotNode
{
public:
	FROM_ANIM_NODE_DECLARE(AnimSnapshotNodeInstanceZero);

	explicit AnimSnapshotNodeInstanceZero(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex)
		: AnimSnapshotNode(typeId, dcTypeId, nodeIndex)
	{
	}

	virtual void SnapshotNode(AnimStateSnapshot* pSnapshot,
							  const DC::AnimNode* pNode,
							  const SnapshotAnimNodeTreeParams& params,
							  SnapshotAnimNodeTreeResults& results) override;
	virtual void GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
									  AnimCmdList* pAnimCmdList,
									  I32F outputInstance,
									  const AnimCmdGenInfo* pCmdGenInfo) const override;

	virtual void DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const override;
};

ANIM_NODE_DECLARE(AnimSnapshotNodeInstanceZero);
