/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-data-eval.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/snapshot-node-heap.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class AnimStateSnapshot;
class ArtItemSkeleton;

struct AnimCameraCutInfo;
struct AnimCmdGenInfo;
struct AnimNodeDebugPrintData;
struct SnapshotAnimNodeTreeParams;
struct SnapshotAnimNodeTreeResults;
struct SnapshotEvaluateParams;

namespace DC
{
	struct AnimInfoCollection;
	struct AnimNode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// A base class for a node which has one child.
// By default all virtual functions pass through to the child node.
class AnimSnapshotNodeUnary : public AnimSnapshotNode
{
public:
	FROM_ANIM_NODE_DECLARE(AnimSnapshotNodeUnary);

	explicit AnimSnapshotNodeUnary(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex)
		: AnimSnapshotNode(typeId, dcTypeId, nodeIndex)
	{
	}

	//
	// Construction, update, etc
	//
	void SnapshotNode(AnimStateSnapshot* pSnapshot,
					  const DC::AnimNode* pDcAnimNode,
					  const SnapshotAnimNodeTreeParams& params,
					  SnapshotAnimNodeTreeResults& results) override;
	bool RefreshAnims(AnimStateSnapshot* pSnapshot) override;
	bool RefreshPhasesAndBlends(AnimStateSnapshot* pSnapshot,
								float statePhase,
								bool topTrackInstance,
								const DC::AnimInfoCollection* pInfoCollection) override;
	U8 RefreshBreadth(AnimStateSnapshot* pSnapshot) override;
	void StepNode(AnimStateSnapshot* pSnapshot, float deltaTime, const DC::AnimInfoCollection* pInfoCollection) override;
	void GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
							  AnimCmdList* pAnimCmdList,
							  I32F outputInstance,
							  const AnimCmdGenInfo* pCmdGenInfo) const override;

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

	void ReleaseNodeRecursive(SnapshotNodeHeap* pHeap) const override;

	SnapshotNodeHeap::Index Clone(SnapshotNodeHeap* pDestHeap, const SnapshotNodeHeap* pSrcHeap) const override;
	void GetHeapUsage(const SnapshotNodeHeap* pSrcHeap, U32& outMem, U32& outNumNodes) const override;
	IAnimDataEval::IAnimData* EvaluateNode(IAnimDataEval* pEval, const AnimStateSnapshot* pSnapshot) const override;

	ndanim::ValidBits GetValidBits(const ArtItemSkeleton* pSkel,
								   const AnimStateSnapshot* pSnapshot,
								   U32 iGroup) const override;
	virtual bool IsAdditive(const AnimStateSnapshot* pSnapshot) const override;
	virtual bool AllowPruning(const AnimStateSnapshot* pSnapshot) const override;
	virtual bool HasErrors(const AnimStateSnapshot* pSnapshot) const override;

	bool HasLoopingAnimation(const AnimStateSnapshot* pSnapshot) const override;
	void GetTriggeredEffects(const AnimStateSnapshot* pSnapshot,
							 EffectUpdateStruct& effectParams,
							 float nodeBlend,
							 const AnimInstance* pInstance) const override;

	virtual bool EvaluateFloatChannel(const AnimStateSnapshot* pSnapshot,
									  float* pOutChannelFloat,
									  const SnapshotEvaluateParams& evaluateParams) const override;

	virtual bool EvaluateChannel(const AnimStateSnapshot* pSnapshot,
								 ndanim::JointParams* pOutChannelJoint,
								 const SnapshotEvaluateParams& evaluateParams) const override;

	virtual bool EvaluateChannelDelta(const AnimStateSnapshot* pSnapshot,
									  ndanim::JointParams* pOutChannelJoint,
									  const SnapshotEvaluateParams& evaluateParams) const override;

	void DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const override;
	void DebugSubmitAnimPlayCount(const AnimStateSnapshot* pSnapshot) const override;
	void CollectContributingAnims(const AnimStateSnapshot* pSnapshot,
								  float blend,
								  AnimCollection* pCollection) const override;

	const AnimSnapshotNode* FindFirstNodeOfKind(const AnimStateSnapshot* pSnapshot, StringId64 typeId) const override;
	bool VisitNodesOfKindInternal(AnimStateSnapshot* pSnapshot,
								  StringId64 typeId,
								  SnapshotVisitNodeFunc visitFunc,
								  AnimSnapshotNodeBlend* pParentBlendNode,
								  float combinedBlend,
								  uintptr_t userData) override;
	bool VisitNodesOfKindInternal(const AnimStateSnapshot* pSnapshot,
								  StringId64 typeId,
								  SnapshotConstVisitNodeFunc visitFunc,
								  const AnimSnapshotNodeBlend* pParentBlendNode,
								  float combinedBlend,
								  uintptr_t userData) const override;

	virtual void ForAllAnimationsInternal(const AnimStateSnapshot* pSnapshot,
										  AnimationVisitFunc visitFunc,
										  float combinedBlend,
										  uintptr_t userData) const override;

	void SetChildIndex(SnapshotNodeHeap::Index index) { m_childIndex = index; }
	SnapshotNodeHeap::Index GetChildIndex() const { return m_childIndex; }

protected:
	const AnimSnapshotNode* GetChild(const AnimStateSnapshot* pSnapshot) const;
	AnimSnapshotNode* GetChild(AnimStateSnapshot* pSnapshot);

private:
	typedef AnimSnapshotNode ParentClass;

	SnapshotNodeHeap::Index m_childIndex;
};

ANIM_NODE_DECLARE(AnimSnapshotNodeUnary);
