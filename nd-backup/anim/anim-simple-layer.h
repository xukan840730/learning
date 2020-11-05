/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/util/bit-array.h"

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-instance.h"
#include "ndlib/anim/anim-layer.h"
#include "ndlib/anim/anim-simple-instance.h"
#include "ndlib/process/bound-frame.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/resource/resource-table.h"

#include "gamelib/scriptx/h/nd-script-func-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class AnimOverlaySnapshot;
class AnimTable;
class ArtItemAnim;
class EffectList;
struct AnimCameraCutInfo;
struct AnimCmdGenLayerContext;
struct FgAnimData;

/// --------------------------------------------------------------------------------------------------------------- ///
typedef void AnimSimpleLayerBlendCallBack_PreBlend(AnimCmdList* pAnimCmdList,
												   SkeletonId skelId,
												   I32F leftInstace,
												   I32F rightInstance,
												   I32F outputInstance,
												   ndanim::BlendMode blendMode,
												   uintptr_t userData);

typedef void AnimSimpleLayerBlendCallBack_PostBlend(AnimCmdList* pAnimCmdList,
													SkeletonId skelId,
													I32F leftInstace,
													I32F rightInstance,
													I32F outputInstance,
													ndanim::BlendMode blendMode,
													uintptr_t userData);

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimSimpleLayer : public AnimLayer
{
public:
	friend class AnimControl;
	friend class NdActorViewerObject;

	AnimSimpleLayer(AnimTable* pAnimTable, AnimOverlaySnapshot* pOverlaySnapshot, U32F numInstancesPerLayer);
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual U32F GetNumFadesInProgress() const override;
	virtual bool IsValid() const override;

	const AnimSimpleInstance* CurrentInstance() const;
	AnimSimpleInstance* CurrentInstance();

	struct FadeOutOnCompleteParams
	{
		float m_fadeTime = 0.3f;
		DC::AnimCurveType m_blendType = DC::kAnimCurveTypeUniformS;
		bool m_enabled = false;
		bool m_fadeEarly = false;
		float m_endPhase = 1.0f;
	};

	struct FadeRequestParams
	{
		FadeRequestParams();

		DC::AnimCurveType m_blendType;
		float m_startPhase;
		float m_fadeTime;
		float m_playbackRate;
		float m_layerFade;
		// We can use this value to guarantee that effs are gathered starting at certain phase. For example cinematics might start clip at phase 0.0001
		// to keep up with cinematic timeline, but that ends up being frame 1. set this value to 0, to make sure first update grabs effs starting at 0
		float m_firstUpdateEffGatherFrame;

		bool m_forceLoop;
		bool m_freezeSrc;
		bool m_freezeDst;
		bool m_mirror;
		bool m_alignToAp;
		bool m_noAlignLocation;
		bool m_skipFirstFrameUpdate;
		BoundFrame m_apRef;
		StringId64 m_apRefChannelId;
		DC::AnimatePhase m_phaseMode;
		float m_firstUpdatePhase;
		FadeOutOnCompleteParams m_layerFadeOutParams;
		ndanim::SharedTimeIndex* m_pSharedTime;
	};

	bool RequestFadeToAnim(ArtItemAnimHandle anim, const FadeRequestParams& params = FadeRequestParams());
	bool RequestFadeToAnim(StringId64 animId, const FadeRequestParams& params = FadeRequestParams());
	bool RequestFadeToAnim(StringId64 animId, const FadeOutOnCompleteParams& fadeOutparams)
	{
		FadeRequestParams params;
		params.m_layerFadeOutParams = fadeOutparams;
		return RequestFadeToAnim(animId, params);
	}

	bool GetApRefFromCurrentInstance(Locator& loc) const;
	bool GetApRefFromCurrentInstance(BoundFrame& loc) const;

	virtual U32F EvaluateChannels(const StringId64* pChannelNames,
								  size_t numChannels,
								  ndanim::JointParams* pOutChannelJoints,
								  const EvaluateChannelParams& params,
								  FadeMethodToUse fadeMethod = kUseMotionFade,
								  float* pBlendValsOut		 = nullptr) const override;

	U32F EvaluateFloatChannels(const StringId64* pChannelNames,
							   U32F numChannels,
							   float* pOutChannelFloats,
							   const EvaluateChannelParams& params) const;

	bool GetAnimAlignDelta(const Locator& curAlign, Locator& alignDeltaOut) const;

	void ForceRefreshAnimPointers();

	void CreateAnimCmds(const AnimCmdGenLayerContext& context, AnimCmdList* pAnimCmdList) const;

	AnimSimpleInstance* GetInstance(U32F index);
	const AnimSimpleInstance* GetInstance(U32F index) const;

	const AnimSimpleInstance* GetInstanceById(AnimSimpleInstance::ID id) const;

	U32F GetNumInstances() const { return m_numInstances; }

	U32F GetMaxInstances() const { return m_numAllocatedInstances; }

	void StepInternal(F32 deltaTime, EffectList* pTriggeredEffects, const FgAnimData* pAnimData);

	void SetLayerBlendCallbacks(AnimSimpleLayerBlendCallBack_PreBlend preBlend,
								AnimSimpleLayerBlendCallBack_PostBlend postBlend)
	{
		m_preBlend	= preBlend;
		m_postBlend = postBlend;
	}

	void SetLayerBlendCallbackUserData(uintptr_t userData) { m_blendCallbackUserData = userData; }

	void CollectContributingAnims(AnimCollection* pCollection) const override;

	void DebugOnly_ForceUpdateOverlaySnapshot(AnimOverlaySnapshot* pNewSnapshot);

	bool HasFreeInstance() const { return m_usedInstances.FindFirstClearBit() < m_numAllocatedInstances; }

private:
	virtual void Setup(StringId64 name, ndanim::BlendMode blendMode) override;
	void Reset();

	virtual void BeginStep(F32 deltaTime, EffectList* pTriggeredEffects, const FgAnimData* pAnimData) override;
	virtual void FinishStep(F32 deltaTime, EffectList* pTriggeredEffects) override;

	void UpdateInstancePhases(F32 deltaTime, const FgAnimData* pAnimData);
	void TakeTransitions();
	void DeleteNonContributingInstances();
	void DeleteAllInstances();

	void DebugPrint(MsgOutput output, U32 priority) const;

	bool GetAnimAlignDelta(const AnimSimpleInstance* pInst, const Locator& curAlign, Locator& alignDelta) const;
	bool GetAnimAlignDeltaRange(const AnimSimpleInstance* pInst,
								float startPhase,
								float endPhase,
								Locator& alignDelta) const;

	void ShowError(const char* szErr) const;

	static const U32 kMaxSupportedInstances = 6;

	AnimSimpleInstance* m_pAllocatedInstances;
	AnimSimpleInstance* m_pInstanceList[kMaxSupportedInstances];
	BitArray64 m_usedInstances;
	U32 m_numAllocatedInstances;
	U32 m_numInstances;
	U32 m_numInstancesStarted;

public:
	StringId64 m_debugId;

private:
	U8 m_padding[3];

	AnimSimpleLayerBlendCallBack_PreBlend* m_preBlend;
	AnimSimpleLayerBlendCallBack_PostBlend* m_postBlend;
	uintptr_t m_blendCallbackUserData;

	mutable const char* m_szDebugPrintError;

	FadeOutOnCompleteParams m_fadeOutParams;
};
