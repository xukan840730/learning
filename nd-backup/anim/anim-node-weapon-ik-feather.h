/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-snapshot-node-unary.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/hand-fix-ik-plugin.h"
#include "ndlib/anim/snapshot-node-heap.h"
#include "ndlib/scriptx/h/anim-ik-defines.h"

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
class AnimSnapshotNodeWeaponIkFeather : public AnimSnapshotNodeUnary
{
private:
	typedef AnimSnapshotNodeUnary ParentClass;

public:
	static SnapshotNodeHeap::Index SnapshotAnimNode(AnimStateSnapshot* pSnapshot,
													const DC::AnimNode* pNode,
													const SnapshotAnimNodeTreeParams& params,
													SnapshotAnimNodeTreeResults& results);
	FROM_ANIM_NODE_DECLARE(AnimSnapshotNodeWeaponIkFeather);

	explicit AnimSnapshotNodeWeaponIkFeather(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex);

	void SnapshotNode(AnimStateSnapshot* pSnapshot,
					  const DC::AnimNode* pNode,
					  const SnapshotAnimNodeTreeParams& params,
					  SnapshotAnimNodeTreeResults& results) override;

	void GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
							  AnimCmdList* pAnimCmdList,
							  I32F outputInstance,
							  const AnimCmdGenInfo* pCmdGenInfo) const override;

	static void GenerateCommands_PostBlend(AnimCmdList* pAnimCmdList,
										   I32F outputInstance,
										   const DC::WeaponIkFeatherParams& params);

	static void AnimPluginCallback(StringId64 pluginId, AnimPluginContext* pPluginContext, const void* pData);

	void DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const override;

private:
	DC::WeaponIkFeatherParams m_params;
	StringId64 m_paramsId;
};

ANIM_NODE_DECLARE(AnimSnapshotNodeWeaponIkFeather);
