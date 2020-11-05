/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-data-eval.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/snapshot-node-heap.h"
#include "ndlib/scriptx/h/animation-script-types.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class AnimStateSnapshot;
class ArtItemSkeleton;
class JointLimits;
struct AnimCameraCutInfo;
struct AnimCmdGenInfo;
struct AnimNodeDebugPrintData;
struct SnapshotAnimNodeTreeParams;
struct SnapshotAnimNodeTreeResults;
struct SnapshotEvaluateParams;
struct ScriptValue;

namespace DC
{
	struct ScriptLambda;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSnapshotNodeBlend : public AnimSnapshotNode
{
public:
	FROM_ANIM_NODE_DECLARE(AnimSnapshotNodeBlend);

	typedef AnimSnapshotNode ParentClass;

	explicit AnimSnapshotNodeBlend(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex);

	//
	// Construction, update, etc
	//
	virtual void SnapshotNode(AnimStateSnapshot* pSnapshot,
							  const DC::AnimNode* pDcAnimNode,
							  const SnapshotAnimNodeTreeParams& params,
							  SnapshotAnimNodeTreeResults& results) override;
	virtual bool RefreshAnims(AnimStateSnapshot* pSnapshot) override;
	virtual bool RefreshPhasesAndBlends(AnimStateSnapshot* pSnapshot,
										float statePhase,
										bool topTrackInstance,
										const DC::AnimInfoCollection* pInfoCollection) override;
	virtual U8 RefreshBreadth(AnimStateSnapshot* pSnapshot) override;
	virtual void StepNode(AnimStateSnapshot* pSnapshot, float deltaTime, const DC::AnimInfoCollection* pInfoCollection) override;
	virtual void GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
									  AnimCmdList* pAnimCmdList,
									  I32F outputInstance,
									  const AnimCmdGenInfo* pCmdGenInfo) const override;
	virtual void ReleaseNodeRecursive(SnapshotNodeHeap* pHeap) const override;

	virtual SnapshotNodeHeap::Index Clone(SnapshotNodeHeap* pDestHeap, const SnapshotNodeHeap* pSrcHeap) const override;

	//
	// Various queries
	//
	virtual ndanim::ValidBits GetValidBits(const ArtItemSkeleton* pSkel,
										   const AnimStateSnapshot* pSnapshot,
										   U32 iGroup) const override;
	virtual bool IsAdditive(const AnimStateSnapshot* pSnapshot) const override;
	virtual bool AllowPruning(const AnimStateSnapshot* pSnapshot) const override;
	virtual bool HasErrors(const AnimStateSnapshot* pSnapshot) const override;

	virtual bool HasLoopingAnimation(const AnimStateSnapshot* pSnapshot) const override;
	virtual void GetTriggeredEffects(const AnimStateSnapshot* pSnapshot,
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

	virtual void CollectContributingAnims(const AnimStateSnapshot* pSnapshot,
										  float blend,
										  AnimCollection* pCollection) const override;
	virtual void GetHeapUsage(const SnapshotNodeHeap* pSrcHeap, U32& outMem, U32& outNumNodes) const override;
	virtual IAnimDataEval::IAnimData* EvaluateNode(IAnimDataEval* pEval, const AnimStateSnapshot* pSnapshot) const override;

	//
	// Debug Nonsense
	//
	virtual void DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const override;
	virtual void DebugSubmitAnimPlayCount(const AnimStateSnapshot* pSnapshot) const override;

	virtual const AnimSnapshotNode* FindFirstNodeOfKind(const AnimStateSnapshot* pSnapshot, StringId64 typeId) const override;
	virtual bool VisitNodesOfKindInternal(AnimStateSnapshot* pSnapshot,
										  StringId64 typeId,
										  SnapshotVisitNodeFunc visitFunc,
										  AnimSnapshotNodeBlend* pParentBlendNode,
										  float combinedBlend,
										  uintptr_t userData) override;
	virtual bool VisitNodesOfKindInternal(const AnimStateSnapshot* pSnapshot,
										  StringId64 typeId,
										  SnapshotConstVisitNodeFunc visitFunc,
										  const AnimSnapshotNodeBlend* pParentBlendNode,
										  float combinedBlend,
										  uintptr_t userData) const override;

	virtual void ForAllAnimationsInternal(const AnimStateSnapshot* pSnapshot,
										  AnimationVisitFunc visitFunc,
										  float combinedBlend,
										  uintptr_t userData) const override;

	static void GetAnimNodeBlendFuncArgs(ScriptValue* pArgs,
										 U32F maxArgs,
										 const DC::AnimInfoCollection* pInfoCollection,
										 DC::AnimStateSnapshotInfo* pSnapshotInfo,
										 float statePhase,
										 bool flipped,
										 AnimStateSnapshot* pSnapshot,
										 const Process* pContextProcess,
										 AnimSnapshotNodeBlend* pBlendNode);

	void SetExternalBlendFactor(F32 f) { m_externalBlendFactor = f; }

	float GetEffectiveBlend() const { return Limit01(m_blendFactor * m_externalBlendFactor); }
	bool IsAdditiveBlend(const AnimStateSnapshot* pSnapshot) const;

	SnapshotNodeHeap::Index m_leftIndex;
	SnapshotNodeHeap::Index m_rightIndex;
	DC::AnimNodeBlendFlag m_flags;
	ndanim::BlendMode m_blendMode;
	I8 m_staticBlend;
	const DC::ScriptLambda* m_blendFunc;
	F32 m_blendFactor;
	F32 m_externalBlendFactor;
	I32 m_featherBlendIndex;
	StringId64 m_customLimitsId;
private:
	ndanim::BlendMode GetBlendMode(const AnimStateSnapshot* pSnapshot) const;
};

ANIM_NODE_DECLARE(AnimSnapshotNodeBlend);
