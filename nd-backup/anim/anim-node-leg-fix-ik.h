/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-snapshot-node-unary.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/leg-fix-ik/leg-fix-ik-plugin.h"
#include "ndlib/anim/snapshot-node-heap.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class AnimStateSnapshot;
struct AnimCmdGenInfo;
struct AnimCmdGenLayerContext;
struct AnimNodeDebugPrintData;
struct SnapshotAnimNodeTreeParams;
struct SnapshotAnimNodeTreeResults;

namespace DC
{
	struct AnimNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSnapshotNodeLegFixIk : public AnimSnapshotNodeUnary
{
	typedef AnimSnapshotNodeUnary ParentClass;

public:
	static SnapshotNodeHeap::Index SnapshotAnimNode(AnimStateSnapshot* pSnapshot,
													const DC::AnimNode* pNode,
													const SnapshotAnimNodeTreeParams& params,
													SnapshotAnimNodeTreeResults& results);

	FROM_ANIM_NODE_DECLARE(AnimSnapshotNodeLegFixIk);

	explicit AnimSnapshotNodeLegFixIk(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex);

	virtual void SnapshotNode(AnimStateSnapshot* pSnapshot,
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

	static void GenerateLegFixIkCommands_PreBlend(AnimCmdList* pAnimCmdList,
												  I32F leftInstance,
												  I32F rightInstance,
												  I32F outputInstance);

	static void GenerateLegFixIkCommands_PostBlend(AnimCmdList* pAnimCmdList,
												   LegFixIkPluginCallbackArg* pArg,
												   I32F leftInstance,
												   I32F rightInstance,
												   I32F outputInstance);

	static void GenerateLegFixIkCommands_PreEval(AnimCmdList* pAnimCmdList, I32F baseInstance, I32F requiredBreadth);
	static void GenerateLegFixIkCommands_PostEval(AnimCmdList* pAnimCmdList, I32F baseInstance, I32F requiredBreadth);

	virtual void DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const override;

private:
	bool m_valid;
};

/// --------------------------------------------------------------------------------------------------------------- ///
ANIM_NODE_DECLARE(AnimSnapshotNodeLegFixIk);

/// --------------------------------------------------------------------------------------------------------------- ///
void LegFixIk_AnimStateLayerBlendCallBack_PreBlend(const AnimStateLayer* pStateLayer,
												   const AnimCmdGenLayerContext& context,
												   AnimCmdList* pAnimCmdList,
												   SkeletonId skelId,
												   I32F leftInstance,
												   I32F rightInstance,
												   I32F outputInstance,
												   ndanim::BlendMode blendMode,
												   uintptr_t userData);

void LegFixIk_AnimStateLayerBlendCallBack_PostBlend(const AnimStateLayer* pStateLayer,
													const AnimCmdGenLayerContext& context,
													AnimCmdList* pAnimCmdList,
													SkeletonId skelId,
													I32F leftInstance,
													I32F rightInstance,
													I32F outputInstance,
													ndanim::BlendMode blendMode,
													uintptr_t userData);

/// --------------------------------------------------------------------------------------------------------------- ///
void LegFixIk_AnimSimpleLayerBlendCallBack_PreBlend(AnimCmdList* pAnimCmdList,
													SkeletonId skelId,
													I32F leftInstance,
													I32F rightInstance,
													I32F outputInstance,
													ndanim::BlendMode blendMode,
													uintptr_t userData);

void LegFixIk_AnimSimpleLayerBlendCallBack_PostBlend(AnimCmdList* pAnimCmdList,
													 SkeletonId skelId,
													 I32F leftInstance,
													 I32F rightInstance,
													 I32F outputInstance,
													 ndanim::BlendMode blendMode,
													 uintptr_t userData);
