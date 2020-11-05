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

namespace DC
{
	struct AnimNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSnapshotNodeHandFixIk : public AnimSnapshotNodeUnary
{
private:
	typedef AnimSnapshotNodeUnary ParentClass;

public:
	static SnapshotNodeHeap::Index SnapshotAnimNode(AnimStateSnapshot* pSnapshot,
													const DC::AnimNode* pNode,
													const SnapshotAnimNodeTreeParams& params,
													SnapshotAnimNodeTreeResults& results);
	FROM_ANIM_NODE_DECLARE(AnimSnapshotNodeHandFixIk);

	explicit AnimSnapshotNodeHandFixIk(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex);
	
	void SnapshotNode(AnimStateSnapshot* pSnapshot,
					  const DC::AnimNode* pNode,
					  const SnapshotAnimNodeTreeParams& params,
					  SnapshotAnimNodeTreeResults& results) override;

	virtual void GenerateAnimCommands_PreBlend(const AnimStateSnapshot* pSnapshot,
											   AnimCmdList* pAnimCmdList,
											   const AnimCmdGenInfo* pCmdGenInfo,
											   const AnimSnapshotNodeBlend* pBlendNode,
											   bool leftNode,
											   I32F leftInstance,
											   I32F rightInstance,
											   I32F outputInstance) const override;

	virtual void GenerateAnimCommands_PostBlend(const AnimStateSnapshot* pSnapshot,
												AnimCmdList* pAnimCmdList,
												const AnimCmdGenInfo* pCmdGenInfo,
												const AnimSnapshotNodeBlend* pBlendNode,
												bool leftNode,
												I32F leftInstance,
												I32F rightInstance,
												I32F outputInstance) const override;

	static void GenerateHandFixIkCommands_PreBlend(AnimCmdList* pAnimCmdList,
												   I32F leftInstance,
												   I32F rightInstance,
												   I32F outputInstance);

	static void GenerateHandFixIkCommands_PostBlend(AnimCmdList* pAnimCmdList,
													HandFixIkPluginCallbackArg* pArg,
													I32F leftInstance,
													I32F rightInstance,
													I32F outputInstance);

	static void GenerateHandFixIkCommands_PreEval(AnimCmdList* pAnimCmdList, I32F baseInstance, I32F requiredBreadth);
	static void GenerateHandFixIkCommands_PostEval(AnimCmdList* pAnimCmdList,
												   I32F baseInstance,
												   I32F requiredBreadth,
												   HandFixIkPluginCallbackArg* pArg);

	void DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const override;

private:
	void SetHandsToIk(DC::HandIkHand hands);

	mutable HandFixIkPluginCallbackArg m_args;
};

ANIM_NODE_DECLARE(AnimSnapshotNodeHandFixIk);
