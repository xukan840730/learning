/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/math/locator.h"
#include "corelib/util/timeframe.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-instance.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/anim/anim-state.h"
#include "ndlib/process/bound-frame.h"
#include "ndlib/scriptx/h/animation-script-types.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimOverlaySnapshot;
class AnimSnapshotNode;
class AnimStateInstance;
class AnimStateInstanceTrack;
class AnimStateLayer;
class AnimTable;
class ArtItemAnim;
class ArtItemSkeleton;
class EffectList;
class SnapshotNodeHeap;
class AnimCopyRemapLayer;
struct AnimCameraCutInfo;
struct FgAnimData;
struct EvaluateChannelParams;

namespace DC
{
	struct BlendParams;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct FadeToStateParams
{
	typedef U8 NewInstanceBehavior;
	enum
	{
		kUnspecified,
		kSpawnNewTrack,
		kUsePreviousTrack
	};

	void ApplyBlendParams(const DC::BlendParams* pBlend);

	static const int kInvalidSubsystemId = 0;

	BoundFrame m_apRef = BoundFrame(kIdentity);
	Transform m_animDeltaTweakXform = kIdentity;
	Locator m_warpApRefDif = kIdentity;

	AnimStateInstance* m_pPrevInstance = nullptr;
	AnimStateInstanceTrack* m_pTrack   = nullptr;
	const DC::AnimInfoCollection* m_pDestInfoCollection = nullptr;
	ndanim::SharedTimeIndex* m_pSharedTime = nullptr;

	StringId64 m_customApRefId = INVALID_STRING_ID_64;
	DC::AnimStateFlag m_extraStateFlags = 0;

	F32 m_stateStartPhase = -1.0f;
	F32 m_phaseNetSkip = -1.0f;
	F32 m_animFadeTime	  = -1.0f;
	F32 m_motionFadeTime  = -1.0f;

	I32 m_customFeatherBlendTableIndex = -1;
	F32 m_customFeatherBlendTableBlend = 1.0f;
	U32 m_subsystemControllerId		   = kInvalidSubsystemId;

	F32 m_firstUpdatePhase	 = -1.0f;
	U32 m_blendOverrideFlags = 0;

	NewInstanceBehavior m_newInstBehavior = kUnspecified;
	DC::AnimCurveType m_blendType		  = DC::kAnimCurveTypeInvalid;

	bool m_freezeSrcState				= false;
	bool m_freezeDestState				= false;
	bool m_useMayaFadeStyle				= false;
	bool m_allowStateLooping			= false;
	bool m_skipFirstFrameUpdate			= false;
	bool m_apRefValid					= false;
	bool m_preventBlendTimeOverrun		= false;
	bool m_dontClearTransitions			= false;
	bool m_haveWarpApRefDif				= false;
	bool m_forceWarpApRefDif			= false;
	bool m_inheritApRestrictAdjustment	= false;
	bool m_isBase						= false;
	bool m_ignoreStartPhaseFunc			= false;
	bool m_disableAnimReplacement		= false;
	bool m_preserveOverlays				= false;
	bool m_preserveInstanceInfo			= false;
	bool m_assertOnFailure				= false;
	bool m_phaseSync					= false;

	U8 m_pad : 4;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimStateEvalParams
{
	AnimStateEvalParams() = default;
	AnimStateEvalParams(const EvaluateChannelParams& params);

	const AnimCopyRemapLayer* m_pRemapLayer = nullptr;
	AnimCameraCutInfo* m_pCameraCutInfo = nullptr;

	DC::AnimFlipMode m_flipMode = DC::kAnimFlipModeFromInstance;

	bool m_disableRetargeting = false;
	bool m_wantRawScale = false;
	bool m_forceChannelBlending = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimStateInstance : public AnimInstance
{
public:
	union Flags
	{
		U32 m_bits = 0;
		struct
		{
			bool m_phaseFrozen : 1;				// current freeze status, computed as part of our fade update
			bool m_phaseFrozenRequested : 1;	// user controlled phase-freezing
			bool m_phaseFrozenDuringFadeIn : 1; // don't update the phase during fade-in
			bool m_freezeFadingOutStates : 1;	// don't update the phase for any child/older instances during fade.
			bool m_transitionsEnabled : 1;		// please document
			bool m_useMayaFadeStyle : 1;		// 'cut' beginning frames to mimic animator style blending in maya
			bool m_cacheTopInfo : 1;			//
			bool m_skipPhaseUpdateThisFrame : 1; // During FadeToState we don't want to advance the frame the same frame we did the fade
			bool m_cameraCutThisFrame : 1;
			bool m_savedAlign : 1;
			bool m_phaseUpdatedManuallyThisFrame : 1; // HACK for cinematic capture mode
			bool m_phaseNetSkipAppliedThisFrame : 1; // Hack for mp melee syncing
			bool m_disableApRef : 1;
			bool m_disableFeatherBlend : 1;		// Don't use feather blend from anim state for this instance
			bool m_disableAutoTransitions : 1;	// disallow auto transitions
		};
	};

	enum BlendOverrideFlags
	{
		kNothing = 0,
		kAnimFadeTimeOverriden = 1 << 0,
		kMotionFadeTimeOverriden = 1 << 1,
		kAnimCurveOverriden = 1 << 2,
	};

	friend class AnimStateLayer;
	friend class AnimStateInstanceTrack;

	virtual bool IsSimple() const override { return false; }

	void Allocate(AnimStateLayer* pOwningLayer,
				  const AnimTable* pAnimTable,
				  const AnimOverlaySnapshot* pAnimOverlaySnapshot,
				  SnapshotNodeHeap* pSnapshotNodeHeap,
				  const StringId64* pChannelIds,
				  U32F numChannelIds,
				  const DC::AnimInfoCollection* pInfoCollection,
				  bool cacheTopInfo);

	void Init(const DC::AnimState* pState,
			  const DC::AnimInfoCollection* pInfoCollection,
			  AnimOverlaySnapshot* pOverlaySnapshot,
			  const FgAnimData* pAnimData,
			  StringId64 prevPhaseAnimId,
			  const BlendOverlay* pAnimBlendOverlay,
			  const FadeToStateParams& params);

	void Loop(const DC::AnimInfoCollection* pInfoCollection,
			  const bool topTrackInstance,
			  const FadeToStateParams& params);
	void UpdateTopInfo(const DC::AnimTopInfo* pInfo);
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

	AnimStateLayer* GetLayer() const { return m_pOwningLayer; }
	virtual StringId64 GetLayerId() const override;

	U32F EvaluateFloatChannels(const StringId64* channelNames,
							   U32F numChannels,
							   float* pOutChannelFloats,
							   const AnimStateEvalParams& params = AnimStateEvalParams()) const;

	U32F EvaluateChannels(const StringId64* channelNames,
						  U32F numChannels,
						  ndanim::JointParams* pOutChannelJoints,
						  const AnimStateEvalParams& params = AnimStateEvalParams()) const;

	void ResetChannelDeltas();

	void OnRelease();
	void Shutdown();

	void PhaseUpdate(F32 deltaTime, bool topState, EffectList* pTriggeredEffects);
	F32 Phase() const;
	virtual F32 GetPhase() const override { return Phase(); }
	void SetPhase(float phase);
	virtual F32 GetMayaFrame() const override { return MayaFrame(); }
	F32 MayaFrame() const;
	F32 PhaseToMayaFrame(float phase) const;
	F32 MayaMaxFrame() const;
	virtual F32 GetDuration() const override;
	F32 GetPhaseRateEstimate() const { return m_phaseRateEstimate; }

	virtual void SetFrozen(bool f) override { m_flags.m_phaseFrozenRequested = f; }
	virtual bool IsFrozen() const override { return m_flags.m_phaseFrozen || m_flags.m_phaseFrozenRequested; }

	void DisableFeatherBlend() { m_flags.m_disableFeatherBlend = true; }
	void SetFeatherBlendTable(I32 index) { m_customFeatherBlendTableIndex = index; }
	void SetFeatherBlendTableBlend(float blend) { m_featherBlendTableBlend = blend; }

	StringId64 GetPhaseAnim() const;

	F32 AnimFade() const { return m_currentAnimFade; }
	F32 AnimFadeTime() const { return m_animFadeTotal; }
	F32 MotionFade() const { return m_currentMotionFade; }
	F32 MotionFadeTime() const { return m_motionFadeTotal; }
	F32 AnimFadeTimeLeft() const { return m_animFadeLeft; }
	F32 MotionFadeTimeLeft() const { return m_motionFadeLeft; }
	F32 MasterFade() const { return Min(MotionFade(), AnimFade()); }

	DC::AnimCurveType GetFadeCurve() const { return m_blendType; }

	virtual float GetFade() const override { return MasterFade(); }

	bool FadeUpdate(F32 deltaTime, bool freezePhase);
	void SetEffectiveFade(float effectiveFade) { m_effectiveFade = effectiveFade; }
	float GetEffectiveFade() const { return m_effectiveFade; }

	const DC::AnimState* GetState() const;
	const DC::AnimInfoCollection* GetAnimInfoCollection() const;
	const DC::AnimActorInfo* GetAnimActorInfo() const;
	const DC::AnimInstanceInfo* GetAnimInstanceInfo() const;
	DC::AnimInstanceInfo* GetAnimInstanceInfo();
	const DC::AnimTopInfo* GetAnimTopInfo() const;
	virtual const AnimTable* GetAnimTable() const override { return m_pAnimTable; }

	bool HasChannel(StringId64 channelId) const;
	const Locator GetChannelDelta(StringId64 channelId) const;
	const Locator GetApChannelDelta() const { return GetChannelDelta(GetApRefChannelId()); }
	const Locator GetChannelDeltaInRefChannelSpace(StringId64 channelId, StringId64 refChannelId) const;
	const Locator GetChannelPrevLoc(StringId64 channelId) const;
	const Locator GetChannelCurLoc(StringId64 channelId) const;

	bool IsApLocatorActive() const;
	void SetApLocator(const BoundFrame& apReference);
	void SetCustomApRefId(StringId64 channelId) { m_customApRefId = channelId; }

	virtual void SetApOrigin(const BoundFrame& apRef) override { SetApLocator(apRef); }
	void SetApTranslationOnly(Point_arg newApPos);
	const BoundFrame& GetApLocator() const;
	virtual const BoundFrame& GetApOrigin() const override { return GetApLocator(); }
	StringId64 GetCustomApRefId() const { return m_customApRefId; }
	StringId64 GetApRefChannelId() const;
	void ApplyApRestrictAdjustPs(Vector_arg apMovePs);
	Vector GetApRestrictAdjustmentPs() const { return m_apRestrictAdjustPs; }

	AnimStateSnapshot& GetAnimStateSnapshot() { return m_stateSnapshot; }
	const AnimStateSnapshot& GetAnimStateSnapshot() const { return m_stateSnapshot; }

	const Flags GetFlags() const { return m_flags; }
	DC::AnimStateFlag GetStateFlags() const { return m_stateSnapshot.m_animState.m_flags; }
	DC::AnimStateFlag& GetMutableStateFlags() { return m_stateSnapshot.m_animState.m_flags; }
	virtual bool IsFlipped() const override { return m_stateSnapshot.IsFlipped(); }

	ndanim::ValidBits GetValidBitsFromState(const ArtItemSkeleton* pSkel, U32 iGroup) const;

	StringId64 GetStateName() const;

	F32 PrevPhase() const;
	F32 PhaseRateEstimate() const;
	virtual F32 GetPrevPhase() const override { return PrevPhase(); }
	F32 PrevMayaFrame() const;
	virtual F32 GetPrevMayaFrame() const override { return PrevMayaFrame(); }
	virtual U32 GetFrameCount() const override;
	F32 GetRemainderTime() const { return m_remainderTime; }
	F32 GetUpdatedPhase(F32 oldPhase,
						F32 deltaTime,
						F32& estimatedAnimScale,
						F32* pRemainderTime,
						ndanim::SharedTimeIndex* pSharedTime = nullptr,
						F32* pEstimatedTimeLeftInAnim = nullptr) const;
	const Transform GetAnimDeltaTweakTransform() const { return m_stateSnapshot.GetAnimDeltaTweakTransform(); }
	bool IsAnimDeltaTweakEnabled() const { return m_stateSnapshot.IsAnimDeltaTweakEnabled(); }
	void SetAnimDeltaTweakTransform(const Transform& xform) { m_stateSnapshot.SetAnimDeltaTweakTransform(xform); }

	virtual ID GetId() const override { return m_id; }

	void DisableAutoTransitions() { m_flags.m_disableAutoTransitions = true; }
	bool IsAutoTransitionsDisabled() const { return m_flags.m_disableAutoTransitions; }

	const DC::AnimTransition* GetActiveTransitionByName(StringId64 transitionId,
														const DC::AnimInfoCollection* pInfoCollection) const;
	bool IsTransitionValid(StringId64 transitionId, const DC::AnimInfoCollection* pInfoCollection) const;

	const AnimSnapshotNode* GetSnapshotNode(U32F index) const { return m_stateSnapshot.GetSnapshotNode(index); }
	AnimSnapshotNode* GetSnapshotNode(U32F index) { return m_stateSnapshot.GetSnapshotNode(index); }

	const AnimSnapshotNode* GetRootSnapshotNode() const
	{
		return m_stateSnapshot.GetSnapshotNode(m_stateSnapshot.m_rootNodeIndex);
	}
	AnimSnapshotNode* GetRootSnapshotNode() { return m_stateSnapshot.GetSnapshotNode(m_stateSnapshot.m_rootNodeIndex); }

	ArtItemAnimHandle GetPhaseAnimArtItem() const;
	ArtItemAnimHandle GetOriginalPhaseAnimArtItem() const;
	void ChangePhaseAnim(const ArtItemAnim* pAnim);

	const AnimOverlaySnapshot* GetAnimOverlaySnapshot() const { return m_pAnimOverlaySnapshot; }

	TimeFrame GetStartTimeAnimClock() const { return m_startTimeAnimClock; }

	virtual void SetSkipPhaseUpdateThisFrame(bool f) override { m_flags.m_skipPhaseUpdateThisFrame = f; }

	// Searches current state only
	const DC::AnimTransition* GetActiveTransitionByStateName(StringId64 stateId,
															 const DC::AnimInfoCollection* pInfoCollection) const;

	// Searches current state only
	const DC::AnimTransition* GetActiveTransitionByStateFilter(IAnimStateFilter* pFilter,
															   const DC::AnimInfoCollection* pInfoCollection) const;

	bool HasSavedAlign() const { return m_flags.m_savedAlign; }
	void SetSavedAlign(const BoundFrame& loc)
	{
		ANIM_ASSERT(GetStateFlags() & DC::kAnimStateFlagSaveTopAlign);
		ANIM_ASSERT(IsReasonable(loc));
		// hijack'd
		m_apReference		 = loc;
		m_flags.m_savedAlign = true;
	}
	void UpdateSavedAlign(const Locator& delta);

	void DisableApRef() { m_flags.m_disableApRef = true; }

	void ForAllAnimations(AnimationVisitFunc visitFunc, uintptr_t userData) const;

	I32 GetFeatherBlendTableIndex() const;
	F32 GetFeatherBlendTableBlend() const;
	U32 GetSubsystemControllerId() const { return m_subsystemControllerId; }

	U32 GetBlendOverridenFlags() const { return m_blendOverrideFlags; } // for visualization only!

	bool IsAboutToEnd(F32 deltaTime) const;
	bool IsAboutToEnd(F32 deltaTime, F32 fadeOutTime) const; // returns true when we would begin a new anim with this fade in time

	void ForceNonFractionalFrame(I32 frame) { m_forceNonFractionalFrame = frame; }

private:
	void ReleaseSnapshotNodes();

	// A* search through transition graph
	const DC::AnimTransition* GetActiveTransitionByDestState(StringId64 destStateId,
															 const DC::AnimInfoCollection* pInfoCollection,
															 IAnimTransitionSearch* pCustomSearch = nullptr) const;

	void UpdateFromState(const DC::AnimState* pState,
						 AnimOverlaySnapshot* pOverlaySnapshot,
						 const FgAnimData* pAnimData,
						 const FadeToStateParams* pFadeToStateParams = nullptr);
	void RefreshAnimPointers();

	void UpdateAnimFade(float deltaTime);
	void UpdateMotionFade(float deltaTime);

	void SetId(ID newId) { m_id = newId; }
	TransitionQueryInfo MakeTransitionQueryInfo(const DC::AnimInfoCollection* pInfoCollection) const;

	BoundFrame m_apReference;

	AnimStateSnapshot m_stateSnapshot;

	AnimStateLayer* m_pOwningLayer = nullptr;
	StringId64* m_pChannelId	   = nullptr;
	Locator* m_pChannelDelta	   = nullptr;
	Locator* m_pChannelPrevLoc	   = nullptr;
	Locator* m_pChannelCurrLoc	   = nullptr;
	const AnimTable* m_pAnimTable  = nullptr;
	const AnimOverlaySnapshot* m_pAnimOverlaySnapshot = nullptr;
	DC::AnimInfoCollection* m_pInfoCollection		  = nullptr;

	U32 m_numChannelDeltas = 0;

	F32 m_animFadeLeft	= 0.0f;
	F32 m_animFadeTotal = 0.0f;

	F32 m_currentAnimFade	= 0.0f;
	F32 m_motionFadeLeft	= 0.0f;
	F32 m_motionFadeTotal	= 0.0f;
	F32 m_currentMotionFade = 0.0f;

	F32 m_phase			= 0.0f;
	F32 m_prevPhase		= 0.0f;
	F32 m_remainderTime = 0.0f;
	F32 m_phaseRateEstimate = 0.0f;

	ID m_id = INVALID_ANIM_INSTANCE_ID;
	DC::AnimCurveType m_blendType = DC::kAnimCurveTypeInvalid;
	Flags m_flags;

	StringId64 m_customApRefId		   = INVALID_STRING_ID_64;
	Vector m_apRestrictAdjustPs		   = kZero;
	I32 m_customFeatherBlendTableIndex = -1;
	float m_featherBlendTableBlend	   = 0.0f;

	U32 m_blendOverrideFlags	= 0; // for visualization only!
	F32 m_effectiveFade			= 0.0f;
	U32 m_subsystemControllerId = FadeToStateParams::kInvalidSubsystemId;

	float m_firstUpdatePhase = -1.f; // the desired phase during for first update.
	I32 m_forceNonFractionalFrame = -1.0f;

	TimeFrame m_startTimeAnimClock = TimeFrameNegInfinity();

	ndanim::SharedTimeIndex* m_pSharedTime = nullptr;

	friend void ValidateInstance(const AnimStateInstance* pInstance);
};
