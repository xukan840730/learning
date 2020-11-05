/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-snapshot-node-unary.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/snapshot-node-heap.h"
#include "ndlib/scriptx/h/anim-ik-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class AnimStateSnapshot;
struct AnimCmdGenInfo;
struct AnimNodeDebugPrintData;
struct AnimPluginContext;
struct SnapshotAnimNodeTreeParams;
struct SnapshotAnimNodeTreeResults;

namespace DC
{
	struct AnimNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSnapshotNodeArmIk : public AnimSnapshotNodeUnary
{
	typedef AnimSnapshotNodeUnary ParentClass;

public:
	static SnapshotNodeHeap::Index SnapshotAnimNode(AnimStateSnapshot* pSnapshot,
													const DC::AnimNode* pNode,
													const SnapshotAnimNodeTreeParams& params,
													SnapshotAnimNodeTreeResults& results);

	static void AnimPluginCallback(AnimPluginContext* pPluginContext, const void* pData);

	FROM_ANIM_NODE_DECLARE(AnimSnapshotNodeArmIk);

	explicit AnimSnapshotNodeArmIk(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex);

	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

	void SnapshotNode(AnimStateSnapshot* pSnapshot,
					  const DC::AnimNode* pNode,
					  const SnapshotAnimNodeTreeParams& params,
					  SnapshotAnimNodeTreeResults& results) override;

	void GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
							  AnimCmdList* pAnimCmdList,
							  I32F outputInstance,
							  const AnimCmdGenInfo* pCmdGenInfo) const override;

	void DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const override;

private:
	DC::ArmIkNodeConfig m_config;

	mutable float m_lastTt;
	mutable bool m_channelEvaluated;
};

ANIM_NODE_DECLARE(AnimSnapshotNodeArmIk);
