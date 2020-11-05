/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-data-eval.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/snapshot-node-heap.h"
#include "ndlib/scriptx/h/animation-script-types.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class AnimOverlaySnapshot;
class AnimSnapshotNode;
class AnimTable;
class ArtItemSkeleton;
class EffectList;
class AnimSnapshotNodeBlend;
class AnimInstance;
class AnimCopyRemapLayer;

struct AnimCameraCutInfo;
struct AnimCmdGenInfo;
struct FadeToStateParams;
struct FgAnimData;

namespace DC
{
	struct ScriptLambda;

	typedef U16 AnimOverlayFlags;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct SnapshotAnimNodeTreeParams
{
	const AnimOverlaySnapshot* m_pConstOverlaySnapshot = nullptr;
	AnimOverlaySnapshot* m_pMutableOverlaySnapshot	   = nullptr;
	const AnimTable* m_pAnimTable = nullptr;
	const DC::AnimInfoCollection* m_pInfoCollection = nullptr;
	const FadeToStateParams* m_pFadeToStateParams	= nullptr;
	const FgAnimData* m_pAnimData = nullptr;
	mutable const AnimSnapshotNodeBlend* m_pParentBlendNode = nullptr;

	bool m_stateFlipped			= false;
	bool m_disableRandomization = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct SnapshotAnimNodeTreeResults
{
	const DC::ScriptLambda* m_pStartPhaseFunc = nullptr;
	DC::AnimStateFlag m_newStateFlags		  = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct SnapshotEvaluateParams
{
	StringId64 m_channelName = INVALID_STRING_ID_64;

	AnimCameraCutInfo* m_pCameraCutInfo		= nullptr;
	const AnimCopyRemapLayer* m_pRemapLayer = nullptr;

	float m_statePhase	  = 0.0f;
	float m_statePhasePre = -1.0f;

	bool m_blendChannels = false;
	bool m_forceChannelBlending = false;
	bool m_flipped		= false;
	bool m_wantRawScale = false;
	bool m_disableRetargeting = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimNodeDebugPrintData
{
	U32 m_nodeIndex;
	MsgOutput m_output;
	const FgAnimData* m_pAnimData;
	const AnimTable* m_pAnimTable;
	const DC::AnimActorInfo* m_pActorInfo;
	const DC::AnimInstanceInfo* m_pInstanceInfo;
	float m_statePhase;
	bool m_additiveLayer;
	bool m_isBaseLayer;
	I32F m_depth;
	float m_parentBlendValue;
	const AnimCopyRemapLayer* m_pRemapLayer;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimStateSnapshot
{
public:
	~AnimStateSnapshot();

	void AllocateAnimDeltaTweakXform();

	void Init(SnapshotNodeHeap* pSnapshotHeap)
	{
		Reset();

		m_pSnapshotHeap = pSnapshotHeap;
	}

	void Reset()
	{
		memset(&m_animState, 0, sizeof(DC::AnimState));
		m_pSnapshotHeap = nullptr;
	}

	void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound);

	void SnapshotAnimState(const DC::AnimState* pState,
						   const AnimOverlaySnapshot* pConstOverlaySnapshot,
						   AnimOverlaySnapshot* pMutableOverlaySnapshot,
						   const AnimTable* pAnimTable,
						   const DC::AnimInfoCollection* pInfoCollection,
						   const FgAnimData* pAnimData,
						   const FadeToStateParams* pFadeToStateParams = nullptr);

	bool RefreshPhasesAndBlends(float statePhase, bool topTrackInstance, const DC::AnimInfoCollection* pInfoCollection);
	void StepNodes(float deltaTime, const DC::AnimInfoCollection* pInfoCollection);
	bool RefreshAnims();
	bool RefreshTransitions(const DC::AnimInfoCollection* pInfoCollection, const AnimTable* pAnimTable);

	U32F EvaluateFloat(const StringId64* channelNames,
					   U32F numChannels,
					   float* pOutChannelFloats,
					   const SnapshotEvaluateParams& params = SnapshotEvaluateParams()) const;

	U32F Evaluate(const StringId64* channelNames,
				  U32F numChannels,
				  ndanim::JointParams* pOutChannelJoints,
				  const SnapshotEvaluateParams& params = SnapshotEvaluateParams()) const;

	U32F EvaluateDelta(const StringId64* channelNames,
					   U32F numChannels,
					   ndanim::JointParams* pOutChannelJoints,
					   const SnapshotEvaluateParams& params = SnapshotEvaluateParams()) const;

	void GenerateAnimCommands(AnimCmdList* pAnimCmdList, I32F outputInstance, const AnimCmdGenInfo* pCmdGenInfo) const;

	void GetTriggeredEffects(const DC::AnimInfoCollection* pInfoCollection,
							 float oldPhase,
							 float newPhase,
							 bool isFlipped,
							 float effectiveFade,
							 bool isTopState,
							 bool isReversed,
							 StringId64 stateId,
							 float animBlend,
							 EffectList* pTriggeredEffects,
							 const AnimInstance* pInstance,
							 float updateRate) const;

	ndanim::ValidBits GetValidBitsFromAnimNodeTree(const ArtItemSkeleton* pSkel, U32 iGroup) const;
	
	bool HasLoopingAnim() const;

	void DebugPrint(MsgOutput output,
					const AnimStateInstance* pInst,
					const FgAnimData* pAnimData,
					const AnimTable* pAnimTable,
					const DC::AnimInfoCollection* pInfoCollection,
					float phase,
					float blend,
					float blendTime,
					float motionBlend,
					float motionBlendTime,
					float effectiveFade,
					bool flipped,
					bool additiveLayer,
					bool isBaseLayer,
					bool frozen,
					StringId64 customApRefChannel,
					I32 featherBlendTableIndex,
					F32 featherBlendTableBlend,
					I32F indent,
					const AnimCopyRemapLayer* pRemapLayer = nullptr);

	const AnimSnapshotNode* GetSnapshotNode(SnapshotNodeHeap::Index index) const;
	AnimSnapshotNode* GetSnapshotNode(SnapshotNodeHeap::Index index);
	void VisitNodesOfKind(StringId64 typeId, SnapshotVisitNodeFunc visitFunc, uintptr_t userData);
	void VisitNodesOfKind(StringId64 typeId, SnapshotConstVisitNodeFunc visitFunc, uintptr_t userData) const;

	const Transform GetAnimDeltaTweakTransform() const;
	bool IsAnimDeltaTweakEnabled() const { return m_animState.m_flags & DC::kAnimStateFlagAnimDeltaTweaking; }
	void SetAnimDeltaTweakTransform(const Transform& xform);
	bool IsFlipped() const { return m_flags.m_isFlipped; }
	bool IsFlipped(const DC::AnimInfoCollection* pInfoCollection) const;

	void GetHeapUsage(U32& outMem, U32& outNumNodes) const;
	IAnimDataEval::IAnimData* EvaluateTree(IAnimDataEval* pEval) const;

	static SnapshotNodeHeap::Index SnapshotAnimNodeTree(AnimStateSnapshot* pSnapshot,
														const DC::AnimNode* pNode,
														const SnapshotAnimNodeTreeParams& params,
														SnapshotAnimNodeTreeResults& results);

private:
	void TransformState(const SnapshotAnimNodeTreeParams& params);

	friend class AnimSnapshotNodeBlend;

public:
	union Flags
	{
		U8 m_bits = 0;
		struct
		{
			bool m_isFlipped : 1;
			bool m_disableRandomization : 1;
		};
	};

	DC::AnimState m_animState;
	Transform* m_pAnimDeltaTweakXform = nullptr;
	StringId64 m_translatedPhaseAnimName = INVALID_STRING_ID_64;
	StringId64 m_originalPhaseAnimName = INVALID_STRING_ID_64;
	SkeletonId m_translatedPhaseSkelId = INVALID_SKELETON_ID;
	I32 m_translatedPhaseHierarchyId = 0;
	I32 m_phaseAnimFrameCount = 0;
	F32 m_updateRate = 0.0f;
	F32 m_statePhase = 0.0f;
	F32 m_effMinBlend = 0.0f;

	SnapshotNodeHeap* m_pSnapshotHeap = nullptr;
	SnapshotNodeHeap::Index m_rootNodeIndex = SnapshotNodeHeap::kOutOfMemoryIndex;

	Flags m_flags;
};

/// --------------------------------------------------------------------------------------------------------------- ///
DC::AnimNodeBlendFlag AnimOverlayExtraBlendFlags(const DC::AnimOverlayFlags flags);
DC::AnimStateFlag AnimOverlayExtraStateFlags(const DC::AnimOverlayFlags flags);
