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
#include "ndlib/anim/anim-state-instance-track.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state.h"
#include "ndlib/process/bound-frame.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class AnimOverlaySnapshot;
class AnimStateInstanceTrack;
class AnimTable;
class EffectList;
class SnapshotNodeHeap;
struct AnimCmdGenLayerContext;
struct AnimStateLayerParams;
struct FgAnimData;
struct AnimPluginContext;

namespace DC
{
	struct AnimActor;
	struct AnimActorInfo;
	struct AnimInfoCollection;
	struct AnimState;
	struct AnimTransition;
	struct BlendOverlay;
}

/************************************************************************/
/* Debugging                                                            */
/************************************************************************/

// Halt upon requests to the AnimStateLayer that satisfy certain conditions.

#if FINAL_BUILD
#define ENABLE_ANIM_STATE_LAYER_REQUEST_TRAP 0
#else
#define ENABLE_ANIM_STATE_LAYER_REQUEST_TRAP 1
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimStateLayerRequestTrap
{
	// Trap any "RequestTransition"
	StringId64 m_transitionId = INVALID_STRING_ID_64;

	// Trap any "FadeToState"
	StringId64 m_stateId = INVALID_STRING_ID_64;

	// Trap any attempt to fade away from the given state
	StringId64 m_awayFromStateId = INVALID_STRING_ID_64;

	bool m_onceOnly = true;

	bool IsValid() const
	{
		return (m_transitionId != INVALID_STRING_ID_64) || (m_stateId != INVALID_STRING_ID_64)
			   || (m_awayFromStateId != INVALID_STRING_ID_64);
	}
};

/************************************************************************/
/* Change Request                                                       */
/************************************************************************/

/// --------------------------------------------------------------------------------------------------------------- ///
struct StateChangeRequest
{
	class ID
	{
	public:
		ID() { *this = kInvalidId; }
		explicit ID(U32 idVal) { m_id = idVal; }

		ID operator=(U32 rhs)
		{
			m_id = rhs;
			return *this;
		}

		bool operator==(const ID& rhs) const { return rhs.m_id == m_id; }
		bool operator!=(const ID& rhs) const { return rhs.m_id != m_id; }

		ID operator++()
		{
			m_id++;
			return *this;
		}
		ID operator++(int) // dummy postfix
		{
			ID it(m_id);
			m_id++;
			return it;
		}

		U32 GetVal() const { return m_id; }

	private:
		U32 m_id;
	};

	const static ID kInvalidId;

	typedef U8 StatusFlag;
	const static StatusFlag kStatusFlagInvalid	  = 1 << 0;
	const static StatusFlag kStatusFlagQueueFull  = 1 << 1;
	const static StatusFlag kStatusFlagFailed	  = 1 << 2;
	const static StatusFlag kStatusFlagPending	  = 1 << 3;
	const static StatusFlag kStatusFlagTaken	  = 1 << 4;
	const static StatusFlag kStatusFlagIgnored	  = 1 << 5;
	const static StatusFlag kStatusFlagDontRemove = 1 << 6;

	typedef U8 TypeFlag;
	const static TypeFlag kTypeFlagInvalid	  = 1 << 0;
	const static TypeFlag kTypeFlagTransition = 1 << 1;
	const static TypeFlag kTypeFlagDirectFade = 1 << 2;
	const static TypeFlag kTypeFlagAll		  = 0xFF;

	// Common variables for a 'Request'
	FadeToStateParams m_params;

	StringId64 m_transitionId;
	StringId64 m_srcStateName;

	// Tracking variables
	ID m_id;
	TypeFlag m_type;
	StatusFlag m_status;

	float m_requestTime;
	bool m_useParentApRef;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ProcessedChangeRequest
{
	StringId64 m_srcStateName;
	StringId64 m_dstStateName;
	StringId64 m_transitionId;

	float m_requestTime;

	StateChangeRequest::ID m_id;
	AnimStateInstance::ID m_instId;

	StateChangeRequest::StatusFlag m_status;
	StateChangeRequest::TypeFlag m_type;
};

/************************************************************************/
/* The Layer                                                            */
/************************************************************************/
enum PendingChangeCallbackType
{
	kPendingChangeFadeToState,
	kPendingChangeRequestTransition,
	kPendingChangeAutoTransition,
};

/// --------------------------------------------------------------------------------------------------------------- ///
typedef void AnimStateLayerBlendCallBack_PreBlend(const AnimStateLayer* pStateLayer,
												  const AnimCmdGenLayerContext& context,
												  AnimCmdList* pAnimCmdList,
												  SkeletonId skelId,
												  I32F leftInstace,
												  I32F rightInstance,
												  I32F outputInstance,
												  ndanim::BlendMode blendMode,
												  uintptr_t userData);

typedef void AnimStateLayerBlendCallBack_PostBlend(const AnimStateLayer* pStateLayer,
												   const AnimCmdGenLayerContext& context,
												   AnimCmdList* pAnimCmdList,
												   SkeletonId skelId,
												   I32F leftInstace,
												   I32F rightInstance,
												   I32F outputInstance,
												   ndanim::BlendMode blendMode,
												   uintptr_t userData);

typedef void AnimStateLayerInstanceCallBack_Prepare(uintptr_t userData,
													StringId64 layerId,
													StateChangeRequest::ID requestId,
													AnimStateInstance::ID instId,
													bool isTop,
													const DC::AnimState* pAnimState,
													FadeToStateParams* pParams);

typedef void AnimStateLayerInstanceCallBack_Create(uintptr_t userData, AnimStateInstance* pInst);
typedef void AnimStateLayerInstanceCallBack_Destroy(uintptr_t userData, AnimStateInstance* pInst);

typedef void AnimStateLayerInstanceCallBack_PendingChange(uintptr_t userData,
														  StringId64 layerId,
														  StateChangeRequest::ID requestId,
														  StringId64 changeId,
														  int changeType);

typedef bool AnimStateLayerInstanceCallBack_AlignFunc(uintptr_t userData,
													  const AnimStateInstance* pInst,
													  const BoundFrame& prevAlign,
													  const BoundFrame& currAlign,
													  const Locator& apAlignDelta,
													  BoundFrame* pAlignOut,
													  bool debugDraw);

typedef void AnimStateLayerInstanceCallBack_IkFunc(uintptr_t userData,
												   const AnimStateInstance* pInst,
												   AnimPluginContext* pPluginContext,
												   const void* pParams);

typedef void AnimStateLayerInstanceCallBack_DebugPrintFunc(uintptr_t userData,
														   const AnimStateInstance* pInst,
														   StringId64 debugType,
														   IStringBuilder* pText);

/// --------------------------------------------------------------------------------------------------------------- ///
enum EvaluateAPFlags
{
	kEvaluateAP_TopOnly		  = 1 << 0,
	kEvaluateAP_IgnoreInvalid = 1 << 1,
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimStateLayer : public AnimLayer
{
public:
	friend class AnimControl;
	friend class AnimStateInstanceTrack;

	struct InstanceCallbacks
	{
		uintptr_t m_userData = 0;

		AnimStateLayerInstanceCallBack_Prepare* m_prepare = nullptr;
		AnimStateLayerInstanceCallBack_Create* m_create	  = nullptr;
		AnimStateLayerInstanceCallBack_Destroy* m_destroy = nullptr;
		AnimStateLayerInstanceCallBack_PendingChange* m_pendingChange = nullptr;
		AnimStateLayerInstanceCallBack_AlignFunc* m_alignFunc		  = nullptr;
		AnimStateLayerInstanceCallBack_IkFunc* m_ikFunc = nullptr;
		AnimStateLayerInstanceCallBack_DebugPrintFunc* m_debugPrintFunc = nullptr;
	};

	AnimStateLayer(AnimTable* pAnimTable,
				   DC::AnimInfoCollection* pInfoCollection,
				   AnimOverlaySnapshot* pOverlaySnapshot,
				   FgAnimData* pAnimData,
				   const AnimStateLayerParams& params);

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void Shutdown() override;

	virtual U32F GetNumFadesInProgress() const override;

	// State information
	StringId64 CurrentStateId() const;
	StringId64 CurrentStateUserId() const;
	const AnimStateInstance* CurrentStateInstance() const;
	AnimStateInstance* CurrentStateInstance();
	const AnimStateInstanceTrack* CurrentInstanceTrack() const { return GetTrackForInstance(CurrentStateInstance()); }
	const AnimStateInstance* FindInstanceByName(StringId64 stateName) const;
	const AnimStateInstance* FindInstanceByNameNewToOld(StringId64 stateName) const;
	const AnimStateInstance* FindInstanceById(AnimInstance::ID id) const;
	const DC::AnimState* CurrentState() const;
	const DC::AnimState* CurrentState();
	const DC::AnimState* FindStateByName(StringId64 stateName) const;
	const DC::AnimState* FindStateByName(StringId64 stateName);
	StateChangeRequest::ID FadeToState(StringId64 stateId, const FadeToStateParams& params);
	bool IsInNonTransitionalState() const;

	const AnimStateInstance* GetInstanceById(AnimStateInstance::ID id) const;
	AnimStateInstance* GetInstanceById(AnimStateInstance::ID id)
	{
		return const_cast<AnimStateInstance*>(const_cast<const AnimStateLayer*>(this)->GetInstanceById(id));
	}

	StateChangeRequest GetNextStateChangeRequest() const;
	const StateChangeRequest* GetPendingChangeRequest(I32 index) const;
	const StateChangeRequest* GetPendingChangeRequest(StateChangeRequest::ID id) const;

	bool HasFreeInstance() const { return m_usedInstances.FindFirstClearBit() < m_numAllocatedInstances; }
	U32F GetNumTotalInstances() const;
	U32F GetMaxInstances() const { return m_numAllocatedInstances; }

	bool HasFreeTrack() const { return m_usedTracks.FindFirstClearBit() < m_numAllocatedTracks; }
	U32 NumUsedTracks() const { return m_numTracks; }
	const AnimStateInstanceTrack* GetTrackByIndex(int index) const { if (index >= m_numTracks) return nullptr; return m_ppTrackList[index]; }
	U32 GetMaxTracks() const { return m_numAllocatedTracks; }

	void TrapRequest(const AnimStateLayerRequestTrap& trap);
	void ClearAllTraps();

	// Transitions requests and queries
	StateChangeRequest::ID RequestTransition(StringId64 transitionId, const FadeToStateParams* pParams = nullptr);

	StateChangeRequest::ID RequestPersistentTransition(StringId64 transitionId,
													   const FadeToStateParams* pParams = nullptr);

	bool IsTransitionValid(StringId64 transitionId) const;
	bool IsTransitionActive(StringId64 transitionId) const { return CanTransitionBeTakenThisFrame(transitionId); }
	bool CanTransitionBeTakenThisFrame(StringId64 transitionId) const;
	StringId64 GetActiveTransitionByStateName(StringId64 stateName) const;
	const DC::AnimTransition* GetActiveDCTransitionByStateName(StringId64 stateName) const;
	StringId64 GetActiveTransitionByStateFilter(IAnimStateFilter* pFilter) const;
	const DC::AnimTransition* GetActiveDCTransitionByStateFilter(IAnimStateFilter* pFilter) const;
	StateChangeRequest::StatusFlag GetTransitionStatus(StateChangeRequest::ID transitionRequestId) const;
	const AnimStateInstance* GetTransitionDestInstance(StateChangeRequest::ID transitionRequestId) const;
	void UpdateTransitionApRef(StateChangeRequest::ID transitionRequestId, const BoundFrame& apRef);
	AnimStateInstance* GetTransitionDestInstance(StateChangeRequest::ID transitionRequestId);
	void RemoveAllPendingTransitions(StateChangeRequest::TypeFlag transitionType);
	U32F GetStatesCompletedLastUpdate() const;
	bool AreTransitionsPending() const;

	const DC::AnimActor* GetAnimActor() const { return m_pAnimActor; }

	void TransformAllApReferences(const Locator& oldSpace3, const Locator& newSpace);
	void UpdateAllApReferences(const BoundFrame& apReference, AnimStateLayerFilterAPRefCallBack filterCallback = nullptr);
	void UpdateAllApReferencesUntilFalse(const BoundFrame& apReference,
										 AnimStateLayerFilterAPRefCallBack filterCallback = nullptr);
	void UpdateAllApReferencesTranslationOnly(Point_arg newApPos,
											  AnimStateLayerFilterAPRefCallBack filterCallback = nullptr);
	void UpdateCurrentApReference(const BoundFrame& apReference);
	void UpdatePendingApReference(const BoundFrame& apReference);
	void CheckNextStateApRef(BoundFrame& outLoc) const;

	bool AreTransitionsEnabled() const { return m_transitionsEnabled; }
	void DisableTransitions() { m_transitionsEnabled = false; }
	void EnableTransitions() { m_transitionsEnabled = true; }

	void DisableTopUpdateOverlayAndInfo() { m_disableTopUpdateOverlayAndInfo = true; }

	StateChangeRequest::ID RequestTransitionByFinalState(StringId64 destStateId,
														 const FadeToStateParams* pParams = nullptr,
														 IAnimTransitionSearch* pCustomSearch = nullptr);

	bool GetApRefFromCurrentState(Locator& loc) const;
	bool GetApRefFromCurrentState(BoundFrame& loc) const;
	void SetApRefOnCurrentState(const Locator& loc) { SetApRefOnCurrentState(BoundFrame(loc, Binding())); }
	void SetApRefOnCurrentState(const BoundFrame& loc);

	bool GetApRefFromStateIndex(BoundFrame& bf, int index, int trackIndex = 0) const;
	bool GetApRefFromStateIndex(Locator& loc, int index, int trackIndex = 0) const;

	virtual U32F EvaluateChannels(const StringId64* pChannelNames,
								  size_t numChannels,
								  ndanim::JointParams* pOutChannelJoints,
								  const EvaluateChannelParams& params,
								  FadeMethodToUse fadeMethod = kUseMotionFade,
								  float* pOutBlendVals		 = nullptr) const override;

	float EvaluateFloat(StringId64 channelName,
						bool* pEvaluated	= nullptr,
						bool* pAllEvaluated = nullptr,
						float defaultFloat	= 0.0f) const;

	const Locator EvaluateAP(StringId64 apChannelName,
							 bool* pEvaluated	 = nullptr,
							 bool* pAllEvaluated = nullptr,
							 const Locator* defaultLocator = nullptr,
							 U32F flags = 0) const;

	void CreateAnimCmds(const AnimCmdGenLayerContext& context,
						AnimCmdList* pAnimCmdList,
						const DC::AnimActorInfo* info,
						F32 layerFadeOverride = -1) const;

	void DebugOnly_ForceUpdateInstanceSnapShots();
	void DebugOnly_ForceUpdateOverlaySnapshot(const AnimOverlaySnapshot* pNewSnapshot);

	virtual bool IsFlipped() const;

	void RefreshAnimPointers();

	U32 GetActiveSubsystemControllerId();
	void SetActiveSubsystemControllerId(U32 subsystemControllerId);

	// Tread gently as thar be dragons! - CGY
	// WARNING - This function should ONLY be called from Player::Init()
	AnimStateInstance* SetState(const DC::AnimState* pState,
								const FadeToStateParams& params,
								StateChangeRequest::ID requestId = StateChangeRequest::kInvalidId);

	void SetLayerBlendCallbacks(AnimStateLayerBlendCallBack_PreBlend preBlendCallBack,
								AnimStateLayerBlendCallBack_PostBlend postBlendCallBack)
	{
		m_preBlend	= preBlendCallBack;
		m_postBlend = postBlendCallBack;
	}

	void SetLayerBlendCallbackUserData(uintptr_t userData) { m_blendCallbackUserData = userData; }

	void SetPostAnimStateCallback(AnimStateLayerPostStateCallBack postStateCallBack)
	{
		m_blendCallbacks.m_postState = postStateCallBack;
	}

	void SetStateBlendCallback(AnimStateLayerBlendStatesCallBack stateBlendCallback)
	{
		m_blendCallbacks.m_stateBlendFunc = stateBlendCallback;
	}

	void SetInstanceCallbacks(const InstanceCallbacks& callbacks) { m_instanceCallbacks = callbacks; }
	const InstanceCallbacks& GetInstanceCallbacks() const { return m_instanceCallbacks; }

	template <typename DATA_TYPE>
	class InstanceBlender
	{
	public:
		virtual ~InstanceBlender() {}

		DATA_TYPE BlendForward(const AnimStateLayer* pStateLayer, DATA_TYPE initialData);  // blending oldest pair first
		DATA_TYPE BlendBackward(const AnimStateLayer* pStateLayer, DATA_TYPE initialData); // blending newest pair first

	protected:
		virtual DATA_TYPE GetDefaultData() const = 0;
		virtual bool GetDataForInstance(const AnimStateInstance* pInstance, DATA_TYPE* pDataOut) = 0;
		virtual DATA_TYPE BlendData(const DATA_TYPE& leftData,
									const DATA_TYPE& rightData,
									float masterFade,
									float animFade,
									float motionFade) = 0;
		virtual void OnHasDataForInstance(const AnimStateInstance* pInstance, const DATA_TYPE& data) {}

	private:
		DATA_TYPE GetDataForTrackForward(const AnimStateInstanceTrack* pTrack, DATA_TYPE initialData);
		DATA_TYPE GetDataForTrackBackward(const AnimStateInstanceTrack* pTrack, DATA_TYPE initialData);

		friend class AnimStateLayer;
	};

	void WalkInstancesNewToOld(PFnVisitAnimStateInstance pfnCallback, uintptr_t userData);
	void WalkInstancesNewToOld(PFnVisitAnimStateInstanceConst pfnCallback, uintptr_t userData) const;

	void WalkInstancesOldToNew(PFnVisitAnimStateInstance pfnCallback, uintptr_t userData);
	void WalkInstancesOldToNew(PFnVisitAnimStateInstanceConst pfnCallback, uintptr_t userData) const;

	virtual bool IsValid() const override;

	void Reset();
	virtual void Setup(StringId64 name, ndanim::BlendMode blendMode) override;
	virtual void Setup(StringId64 name, ndanim::BlendMode blendMode, const DC::AnimActor* pActor);

	virtual void BeginStep(F32 deltaTime, EffectList* pTriggeredEffects, const FgAnimData* pAnimData) override;

	void DebugPrint(MsgOutput output, U32 priority, const DC::AnimActorInfo* info) const;
	void DebugPrintToJson(char*& buf) const;

	void RemoveChangeRequestByIndex(U32F index);
	void RemoveChangeRequestByIndex(U32F index, StateChangeRequest::StatusFlag status, AnimStateInstance* pDestInstance);
	AnimStateInstance* FadeToStateImmediate(const DC::AnimState* pState,
											const FadeToStateParams& params,
											StateChangeRequest::ID requestId = StateChangeRequest::kInvalidId);

	bool TakeTransitions(AnimStateInstance** ppNewInstanceOut);
	bool TakeAutoTransitions(bool tookNormalTransition, AnimStateInstance** ppNewInstanceOut);
	bool TakeAutoTransitions(AnimStateInstanceTrack* pTrack,
							 AnimStateInstance* pInst,
							 bool isTopTrack,
							 bool isTopTrackInst,
							 const DC::AnimInfoCollection* pDestInfoCollection,
							 AnimStateInstance** ppNewInstanceOut);

	AnimStateInstance* ApplyTransition(AnimStateInstanceTrack* pTrack,
									   AnimStateInstance* pInstance,
									   const DC::AnimTransition* pTrans,
									   const FadeToStateParams& reqParams,
									   bool convertedStateCountAsCompleted,
									   bool useParentApRef,
									   bool isTopTrack,
									   bool isTopTrackInst,
									   const DC::AnimInfoCollection* pDestInfoCollection,
									   const TransitionQueryInfo& tqInfo, // the same TransitionQueryInfo used in IsTransitionActive()
									   StateChangeRequest::ID requestId = StateChangeRequest::kInvalidId);

	void ReloadScriptData(const DC::AnimActor* actor);
	void DeleteNonContributingInstances();
	void DeleteNonContributingTracks();
	void UpdateInstanceFadeEffects(F32 deltaTime);
	void UpdateInstancePhases(F32 deltaTime, EffectList* pTriggeredEffects);
	void ResetAnimStateChannelDeltas();
	bool CanLoopInstance(const AnimStateInstance* pInstance,
						 const DC::AnimState* pDestState,
						 const FadeToStateParams& params) const;
	void CopyActorInfoToCurrentState();
	void CopyOverlaySnapshotToCurrentTrack();
	void CopyOverlayVariantsBackToGlobalSnapshot();

	void ReleaseInstance(AnimStateInstance* pInstance);
	void ReleaseTrack(AnimStateInstanceTrack* pTrack);

	bool IsInstanceTopOfTrack(const AnimStateInstance* pInstance) const;
	bool IsInstanceActive(const AnimStateInstance* pInstance) const;

	AnimStateInstance* AllocateOrReclaimInstance();
	AnimStateInstanceTrack* AllocateOrReclaimInstanceTrack();
	const AnimStateInstanceTrack* GetTrackForInstance(const AnimStateInstance* pInstance) const;
	AnimStateInstanceTrack* GetTrackForInstance(const AnimStateInstance* pInstance)
	{
		const AnimStateLayer* pConstThis = const_cast<const AnimStateLayer*>(this);
		const AnimStateInstanceTrack* pTrack = pConstThis->GetTrackForInstance(pInstance);
		return const_cast<AnimStateInstanceTrack*>(pTrack);
	}

	static bool WalkStepCompletedInstances(AnimStateInstance* pInstance,
										   AnimStateLayer* pStateLayer,
										   uintptr_t userData);

	const SnapshotNodeHeap* GetSnapshotNodeHeap() const { return m_pSnapshotNodeHeap; }

	void InstanceCallBackPrepare(StateChangeRequest::ID requestId,
								 AnimStateInstance::ID instId,
								 bool isTop,
								 const DC::AnimState* pAnimState,
								 FadeToStateParams* pParams) const;
	void InstanceCallBackCreate(AnimStateInstance* pInst) const;
	void InstanceCallBackDestroy(AnimStateInstance* pInst) const;
	void InstanceCallBackPendingChange(StateChangeRequest::ID requestId, StringId64 changeId, int changeType) const;
	bool InstanceCallBackAlignFunc(const AnimStateInstance* pInst,
								   const BoundFrame& prevAlign,
								   const BoundFrame& currAlign,
								   const Locator& apAlignDelta,
								   BoundFrame* pAlignOut,
								   bool debugDraw) const;
	bool HasInstanceCallBackAlignFunc() const { return m_instanceCallbacks.m_alignFunc != nullptr; }
	void InstanceCallBackIkFunc(const AnimStateInstance* pInst,
								AnimPluginContext* pPluginContext,
								const void* pParams) const;
	void InstanceCallBackDebugPrintFunc(const AnimStateInstance* pInst,
										StringId64 debugType,
										IStringBuilder* pText) const;

	void CollectContributingAnims(AnimCollection* pCollection) const override;

	const FgAnimData* GetAnimData() const { return m_pAnimData; }

	const BlendOverlay* GetStateBlendOverlay() const;

	void SetDebugFakeBaseLayer() { m_debugPrintFakeBaseLayer = true; }

private:
	virtual void OnFree() override;

	SnapshotNodeHeap* GetSnapshotNodeHeap() { return m_pSnapshotNodeHeap; }

	// Transition request related constants.
	static const U32 kMaxRequestsInFlight = 4; //<! Maximum number of pending transitions allowed

	FgAnimData* m_pAnimData;
	const DC::AnimActor* m_pAnimActor;	   //<! Not relocatable. Pointer to global animation data.
	SnapshotNodeHeap* m_pSnapshotNodeHeap; // not owned by the layer, don't deep relocate

	// Transitional logic variables
	StateChangeRequest m_changeRequestsPendingList[kMaxRequestsInFlight];
	ProcessedChangeRequest m_changeRequestsProcessedList[kMaxRequestsInFlight];
	StateChangeRequest::ID m_changeRequestNextId;
	U32 m_changeRequestsPending;
	U32 m_changeRequestsProcessedListWriteIndex;

	AnimStateInstanceTrack* m_pAllocatedTracks;
	BitArray64 m_usedTracks;
	U32 m_numAllocatedTracks;
	AnimStateInstanceTrack** m_ppTrackList;
	U32 m_numTracks;

	AnimStateInstance* m_pAllocatedInstances;
	BitArray64 m_usedInstances;
	U32 m_numAllocatedInstances;
	// AnimStateInstance** m_ppInstanceList;
	// U32 m_numInstances;

	// Stats
	U32 m_transitionsTakenLastUpdate;
	U32 m_statesCompletedLastUpdate;
	U32 m_numStatesStarted;

	U32 m_activeSubsystemControllerId;

	bool m_transitionsEnabled;
	bool m_disableTopUpdateOverlayAndInfo;

	bool m_debugPrintFakeBaseLayer; // Show debug loco tree even if only base layer selected

	DC::AnimInfoCollection* m_pInfoCollection;
	sAnimNodeBlendCallbacks m_blendCallbacks;
	AnimStateLayerBlendCallBack_PreBlend* m_preBlend;
	AnimStateLayerBlendCallBack_PostBlend* m_postBlend;
	uintptr_t m_blendCallbackUserData;
	FadeToStateParams::NewInstanceBehavior m_defNewInstBehavior;

	InstanceCallbacks m_instanceCallbacks;

#if ENABLE_ANIM_STATE_LAYER_REQUEST_TRAP
	AnimStateLayerRequestTrap m_traps[10];
#endif

	friend void ValidateLayer(const AnimStateLayer* pLayer);
};

#include "ndlib/anim/anim-state-layer.inl" // IWYU pragma: keep
