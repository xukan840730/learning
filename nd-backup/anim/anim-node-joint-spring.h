/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-snapshot-node-unary.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/snapshot-node-heap.h"
#include "ndlib/util/tracker.h"
#include "ndlib/scriptx/h/animation-script-types.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class AnimStateSnapshot;
struct AnimCmdGenInfo;
struct AnimNodeDebugPrintData;
struct SnapshotAnimNodeTreeParams;
struct SnapshotAnimNodeTreeResults;
struct AnimPluginContext;

namespace DC
{
	struct AnimNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSnapshotNodeJointSpring : public AnimSnapshotNodeUnary
{
	typedef AnimSnapshotNodeUnary ParentClass;

public:
	FROM_ANIM_NODE_DECLARE(AnimSnapshotNodeJointSpring);

	explicit AnimSnapshotNodeJointSpring(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex);

	virtual void SnapshotNode(AnimStateSnapshot* pSnapshot,
							  const DC::AnimNode* pDcAnimNode,
							  const SnapshotAnimNodeTreeParams& params,
							  SnapshotAnimNodeTreeResults& results) override;

	virtual void StepNode(AnimStateSnapshot* pSnapshot,
						  float deltaTime,
						  const DC::AnimInfoCollection* pInfoCollection) override;

	void GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
							  AnimCmdList* pAnimCmdList,
							  I32F outputInstance,
							  const AnimCmdGenInfo* pCmdGenInfo) const override;

	void DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const override;

	bool IsContiguousWith(const DC::AnimNodeJointSpring* pDcNode) const;

	static SnapshotNodeHeap::Index SnapshotAnimNode(AnimStateSnapshot* pSnapshot,
													const DC::AnimNode* pNode,
													const SnapshotAnimNodeTreeParams& params,
													SnapshotAnimNodeTreeResults& results);

	static void AnimPluginCallback(AnimPluginContext* pPluginContext, const void* pData);

private:
	DampedSpringTrackerQuat m_rotTracker;

	Quat m_goalRot;
	Quat m_sprungRot;
	
	StringId64 m_jointNameId;
	I32F m_jointIndex;
	DC::JointSpringMode m_mode;
	float m_springK;
	float m_dampingRatio;
	bool m_firstFrame;
	bool m_error;
};

/// --------------------------------------------------------------------------------------------------------------- ///
ANIM_NODE_DECLARE(AnimSnapshotNodeJointSpring);
