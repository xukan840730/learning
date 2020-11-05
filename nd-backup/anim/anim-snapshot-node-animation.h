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

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class AnimStateSnapshot;
class ArtItemAnim;
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
	struct ScriptLambda;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSnapshotNodeAnimation : public AnimSnapshotNode
{
public:
	explicit AnimSnapshotNodeAnimation(StringId64 typeId, StringId64 dcTypeId, SnapshotNodeHeap::Index nodeIndex)
		: AnimSnapshotNode(typeId, dcTypeId, nodeIndex)
	{
	}

	virtual const char* GetName() const override { return DevKitOnly_StringIdToString(m_animation); }

	//
	// Construction, update, etc
	//
	virtual void SnapshotNode(AnimStateSnapshot* pSnapshot,
							  const DC::AnimNode* pNode,
							  const SnapshotAnimNodeTreeParams& params,
							  SnapshotAnimNodeTreeResults& results) override;
	virtual bool RefreshAnims(AnimStateSnapshot* pSnapshot) override;
	virtual void StepNode(AnimStateSnapshot* pSnapshot, float deltaTime, const DC::AnimInfoCollection* pInfoCollection) override;
	virtual bool RefreshPhasesAndBlends(AnimStateSnapshot* pSnapshot,
										float statePhase,
										bool topTrackInstance,
										const DC::AnimInfoCollection* pInfoCollection) override;
	virtual U8 RefreshBreadth(AnimStateSnapshot* pSnapshot) override { return 1; }
	virtual void GenerateAnimCommands(const AnimStateSnapshot* pSnapshot,
									  AnimCmdList* pAnimCmdList,
									  I32F outputInstance,
									  const AnimCmdGenInfo* pCmdGenInfo) const override;

	//
	// Various queries
	//
	virtual ndanim::ValidBits GetValidBits(const ArtItemSkeleton* pSkel, 
										   const AnimStateSnapshot* pSnapshot,
										   U32 iGroup) const override;
	virtual bool IsAdditive(const AnimStateSnapshot* pSnapshot) const override;
	virtual bool IsFlipped() const override { return m_flipped; }
	virtual bool ShouldHandleFlipInBlend() const override;
	virtual bool HasErrors(const AnimStateSnapshot* pSnapshot) const override;
	virtual bool HasLoopingAnimation(const AnimStateSnapshot* pSnapshot) const override;

	static void AddTriggeredEffectsForAnim(const ArtItemAnim* pArtItemAnim,
										   EffectUpdateStruct& effectParams,
										   float nodeBlend,
										   const AnimInstance* pInstance,
										   StringId64 stateId);

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

	void CollectContributingAnims(const AnimStateSnapshot* pSnapshot, float blend, AnimCollection* pCollection) const override;
	virtual IAnimDataEval::IAnimData* EvaluateNode(IAnimDataEval* pEval, const AnimStateSnapshot* pSnapshot) const override;

	virtual void ForAllAnimationsInternal(const AnimStateSnapshot* pSnapshot,
										  AnimationVisitFunc visitFunc,
										  float combinedBlend,
										  uintptr_t userData) const override;

	//
	// Debug Nonsense
	//
	virtual void DebugPrint(const AnimStateSnapshot* pSnapshot, AnimNodeDebugPrintData* pData) const override;
	virtual void DebugSubmitAnimPlayCount(const AnimStateSnapshot* pSnapshot) const override;

	enum class PhaseMode
	{
		kNormal,
		kDetached,
		kGlobalClock
	};

	static const char* GetPhaseModeStr(PhaseMode m)
	{
		switch (m)
		{
		case PhaseMode::kNormal:		return "Normal";
		case PhaseMode::kDetached:		return "Detached";
		case PhaseMode::kGlobalClock:	return "Global Clock";
		}

		return "???";
	}

	StringId64 GetAnimNameId() const { return m_animation; }

	FROM_ANIM_NODE_DECLARE(AnimSnapshotNodeAnimation);

	StringId64 m_origAnimation;
	StringId64 m_animation;
	SkeletonId m_skelId;
	I32 m_hierarchyId;
	ArtItemAnimHandle m_artItemAnimHandle;
	const DC::ScriptLambda* m_phaseFunc;
	F32 m_phase;
	F32 m_prevPhase;
	PhaseMode m_phaseMode;
	bool m_flipped;
	bool m_noReplaceAnimOverlay;
};

ANIM_NODE_DECLARE(AnimSnapshotNodeAnimation);
