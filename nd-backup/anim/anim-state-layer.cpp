/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-state-layer.h"

#include "corelib/memory/relocate.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/msg.h"

#include "ndlib/anim/anim-actor.h"
#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-command-generator.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/anim-state-instance-track.h"
#include "ndlib/anim/feather-blend-table.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/snapshot-node-heap.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/scriptx/h/dc-types.h"
#include "ndlib/util/maybe.h"

#include "gamelib/level/art-item-anim.h"
#include "gamelib/level/art-item-skeleton.h"

/// --------------------------------------------------------------------------------------------------------------- ///
const StateChangeRequest::ID StateChangeRequest::kInvalidId(0x00FFFFFF);

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateLayer::AnimStateLayer(AnimTable* pAnimTable,
							   DC::AnimInfoCollection* pInfoCollection,
							   AnimOverlaySnapshot* pOverlaySnapshot,
							   FgAnimData* pAnimData,
							   const AnimStateLayerParams& params)
	: AnimLayer(kAnimLayerTypeState, pAnimTable, pOverlaySnapshot)
	, m_pAnimData(pAnimData)
	, m_pAnimActor(nullptr)
	, m_pSnapshotNodeHeap(nullptr)
	, m_changeRequestNextId(0)
	, m_changeRequestsPending(0)
	, m_changeRequestsProcessedListWriteIndex(0)
	, m_transitionsTakenLastUpdate(0)
	, m_statesCompletedLastUpdate(0)
	, m_numStatesStarted(0)
	, m_activeSubsystemControllerId(FadeToStateParams::kInvalidSubsystemId)
	, m_transitionsEnabled(true)
	, m_disableTopUpdateOverlayAndInfo(false)
	, m_debugPrintFakeBaseLayer(false)
	, m_pInfoCollection(pInfoCollection)
	, m_preBlend(nullptr)
	, m_postBlend(nullptr)
	, m_blendCallbackUserData(0)
	, m_defNewInstBehavior(params.m_newInstanceBehavior)
{
	ANIM_ASSERT(params.m_newInstanceBehavior != FadeToStateParams::kUnspecified);
	ANIM_ASSERT(pInfoCollection);

	memset(&m_changeRequestsPendingList[0], 0, sizeof(StateChangeRequest) * kMaxRequestsInFlight);
	memset(&m_changeRequestsProcessedList[0], 0, sizeof(ProcessedChangeRequest) * kMaxRequestsInFlight);

	for (U32F i = 0; i < kMaxRequestsInFlight; ++i)
	{
		m_changeRequestsPendingList[i].m_id   = StateChangeRequest::kInvalidId;
		m_changeRequestsProcessedList[i].m_id = StateChangeRequest::kInvalidId;

		m_changeRequestsPendingList[i].m_status   = StateChangeRequest::kStatusFlagInvalid;
		m_changeRequestsProcessedList[i].m_status = StateChangeRequest::kStatusFlagInvalid;

		m_changeRequestsPendingList[i].m_params = FadeToStateParams();
	}
	
	const U32F maxInstanceTracks = params.m_numTracksInLayer;
	const U32F maxStateInstances = params.m_numInstancesInLayer;

	m_pAllocatedTracks = NDI_NEW AnimStateInstanceTrack[maxInstanceTracks];
	m_numAllocatedTracks = maxInstanceTracks;

	for (I32F ii = 0; ii < maxInstanceTracks; ++ii)
	{
		m_pAllocatedTracks[ii].Allocate(maxStateInstances, params.m_cacheOverlays ? pOverlaySnapshot : nullptr);
	}

	m_pAllocatedInstances = NDI_NEW (kAlign128) AnimStateInstance[maxStateInstances];
	m_numAllocatedInstances = maxStateInstances;

	m_ppTrackList = NDI_NEW (kAlign16) AnimStateInstanceTrack*[AlignSize(maxInstanceTracks, kAlign4)]; // We need this list to be a multiple of 16 bytes in size
	for (I32F ii = 0; ii < maxInstanceTracks; ++ii)
	{
		m_ppTrackList[ii] = nullptr;
	}

	m_pSnapshotNodeHeap = GetGlobalSnapshotNodeHeap();

	for (I32F ii = 0; ii < maxStateInstances; ++ii)
	{
		m_pAllocatedInstances[ii].Allocate(this,
										   m_pAnimTable,
										   pOverlaySnapshot,
										   m_pSnapshotNodeHeap,
										   params.m_pChannelIds,
										   params.m_numChannelIds,
										   pInfoCollection,
										   params.m_cacheTopInfo);
	}

	m_usedInstances.ClearAllBits();
	m_usedTracks.ClearAllBits();

	Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pInfoCollection, deltaPos, lowerBound, upperBound);

	for (U32F i = 0; i < m_numAllocatedInstances; ++i)
	{
		m_pAllocatedInstances[i].Relocate(deltaPos, lowerBound, upperBound);
	}
	RelocatePointer(m_pAllocatedInstances, deltaPos, lowerBound, upperBound);

	for (U32F i = 0; i < m_numAllocatedTracks; ++i)
	{
		m_pAllocatedTracks[i].Relocate(deltaPos, lowerBound, upperBound);
	}
	RelocatePointer(m_pAllocatedTracks, deltaPos, lowerBound, upperBound);

	for (U32F i = 0; i < m_numTracks; ++i)
	{
		RelocatePointer(m_ppTrackList[i], deltaPos, lowerBound, upperBound);
	}
	RelocatePointer(m_ppTrackList, deltaPos, lowerBound, upperBound);

	RelocatePointer(m_pSnapshotNodeHeap, deltaPos, lowerBound, upperBound);
	
	void*& instaceCbUserData = (void*&)m_instanceCallbacks.m_userData;
	RelocatePointer(instaceCbUserData, deltaPos, lowerBound, upperBound);

	void*& blendCbUserData = (void*&)m_blendCallbackUserData;
	RelocatePointer(blendCbUserData, deltaPos, lowerBound, upperBound);

	AnimLayer::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::Shutdown()
{
	for (U32F iInstance = 0; iInstance < m_numAllocatedInstances; ++iInstance)
	{
		if (m_usedInstances.IsBitSet(iInstance))
		{
			m_pAllocatedInstances[iInstance].Shutdown();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::Reset()
{
	for (U32F i = 0; i < m_numAllocatedTracks; ++i)
	{
		m_pAllocatedTracks[i].Reset();
	}
	
	m_usedTracks.ClearAllBits();
	m_numTracks = 0;

	for (U64 iInstance = m_usedInstances.FindFirstSetBit(); iInstance < m_numAllocatedInstances;
		 iInstance	   = m_usedInstances.FindNextSetBit(iInstance))
	{
		m_pAllocatedInstances[iInstance].OnRelease();
	}
	m_usedInstances.ClearAllBits();

	m_transitionsTakenLastUpdate = 0;
	m_statesCompletedLastUpdate = 0;
	m_changeRequestsPending = 0;
	m_changeRequestsProcessedListWriteIndex = 0;
	m_transitionsEnabled = true;
	m_disableTopUpdateOverlayAndInfo = false;
	m_preBlend = nullptr;
	m_postBlend = nullptr;
	m_blendCallbackUserData = 0;
	m_blendCallbacks = sAnimNodeBlendCallbacks();
	m_debugPrintFakeBaseLayer = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::Setup(StringId64 name, ndanim::BlendMode blendMode)
{
	Reset();
	AnimLayer::Setup(name, blendMode);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::Setup(StringId64 name, ndanim::BlendMode blendMode, const DC::AnimActor* pActor)
{
	Setup(name, blendMode);
	m_pAnimActor = pActor;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateLayer::IsValid() const
{
	return CurrentStateInstance() != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateLayer::IsFlipped() const
{
	return CurrentState() ? CurrentStateInstance()->IsFlipped() : false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateLayer::IsTransitionValid(StringId64 transitionId) const
{
	const AnimStateInstance* pCurrentState = CurrentStateInstance();
	if (!pCurrentState)
		return false;

	TransitionQueryInfo tqInfo = pCurrentState->MakeTransitionQueryInfo(m_pInfoCollection);
	tqInfo.m_pAnimTable = m_pAnimTable;
	tqInfo.m_pOverlaySnapshot = m_pOverlaySnapshot;

	const bool valid = AnimStateTransitionValid(pCurrentState->GetState(), transitionId, tqInfo);
	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateLayer::CanTransitionBeTakenThisFrame(StringId64 transitionId) const
{
	const AnimStateInstance* pCurInstance = CurrentStateInstance();
	if (!pCurInstance)
		return false;

	if (!IsTransitionValid(transitionId))
		return false;

	if (nullptr == pCurInstance->GetActiveTransitionByName(transitionId, m_pInfoCollection))
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimStateLayer::GetActiveTransitionByStateName(StringId64 stateName) const
{
	if (const DC::AnimTransition* pTrans = GetActiveDCTransitionByStateName(stateName))
	{
		return pTrans->m_name;
	}
	else
	{
		return INVALID_STRING_ID_64;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTransition* AnimStateLayer::GetActiveDCTransitionByStateName(StringId64 stateName) const
{
	const AnimStateInstance* pCurInstance = CurrentStateInstance();
	if (!pCurInstance)
		return nullptr;

	const DC::AnimTransition* pTrans = pCurInstance->GetActiveTransitionByStateName(stateName, m_pInfoCollection);
	return pTrans;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimStateLayer::GetActiveTransitionByStateFilter(IAnimStateFilter* pFilter) const
{
	if (const DC::AnimTransition* pTrans = GetActiveDCTransitionByStateFilter(pFilter))
	{
		return pTrans->m_name;
	}
	else
	{
		return INVALID_STRING_ID_64;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimTransition* AnimStateLayer::GetActiveDCTransitionByStateFilter(IAnimStateFilter* pFilter) const
{
	const AnimStateInstance* pCurInstance = CurrentStateInstance();
	if (!pCurInstance)
		return nullptr;

	const DC::AnimTransition* pTrans = pCurInstance->GetActiveTransitionByStateFilter(pFilter, m_pInfoCollection);
	return pTrans;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StateChangeRequest::ID AnimStateLayer::FadeToState(StringId64 stateId,
												   const FadeToStateParams& params /* = FadeToStateParams(0.0f) */)
{
#if ENABLE_ANIM_STATE_LAYER_REQUEST_TRAP
	for (U32 i = 0; i < ARRAY_COUNT(m_traps); ++i)
	{
		if (m_traps[i].IsValid())
		{
			if (m_traps[i].m_stateId == stateId || CurrentStateId() == m_traps[i].m_awayFromStateId)
			{
				ALWAYS_HALTF(("Anim State Layer trapped FadeToState(%s)", DevKitOnly_StringIdToString(stateId)));

				if (m_traps[i].m_onceOnly)
				{
					m_traps[i] = AnimStateLayerRequestTrap();
				}
			}
		}
	}
#endif

	if (ShouldAssertOnStateChanges())
	{
		ANIM_ASSERTF(false, ("%s: StateLayer attempting to fade to state '%s' but fades/transitions are currently locked out",
								m_pAnimData ? DevKitOnly_StringIdToStringOrHex(m_pAnimData->m_hProcess.GetUserId()) : "(null)", DevKitOnly_StringIdToStringOrHex(stateId)));
	}

	I32 requestIndex;
	if (params.m_dontClearTransitions)
	{
		// Remove a request if the queue is full
		if (m_changeRequestsPending + 1 > kMaxRequestsInFlight)
		{
			RemoveChangeRequestByIndex(0);
		}

		// move all requests...
		for (U32F i = 0; i < m_changeRequestsPending; ++i)
		{
			m_changeRequestsPendingList[i+1] = m_changeRequestsPendingList[i];
		}

		SetActiveSubsystemControllerId(params.m_subsystemControllerId);

		requestIndex = 0;
		m_changeRequestsPending++;
	}
	else
	{
		// Clear out all pending transition (non-fade) requests...
		for (U32F i = 0; i < m_changeRequestsPending; ++i)
		{
			if (m_changeRequestsPendingList[i].m_type != StateChangeRequest::kTypeFlagDirectFade)
			{
				RemoveChangeRequestByIndex(i);
				--i;
			}
		}

		// Remove a request if the queue is full
		if (m_changeRequestsPending + 1 > kMaxRequestsInFlight)
		{
			RemoveChangeRequestByIndex(0);
		}

		SetActiveSubsystemControllerId(params.m_subsystemControllerId);

		requestIndex = m_changeRequestsPending;
		m_changeRequestsPending++;
	}

	const DC::AnimState* pState = AnimActorFindState(m_pAnimActor, stateId);

	ANIM_ASSERTF(pState,
				 ("AnimStateLayer::FadeToState: Could not find target state (%s)!\n",
				  DevKitOnly_StringIdToString(stateId)));

	MsgAnimVerbose("Animation(" PFS_PTR "): Accepted direct fade request (%s) id: %d\n",
				   this,
				   DevKitOnly_StringIdToString(stateId),
				   m_changeRequestNextId.GetVal());

	StateChangeRequest& request = m_changeRequestsPendingList[requestIndex];
	request.m_id = m_changeRequestNextId++;
	request.m_type = StateChangeRequest::kTypeFlagDirectFade;
	request.m_status = StateChangeRequest::kStatusFlagPending;
	request.m_requestTime = GetAnimationSystemTime();
	request.m_srcStateName = INVALID_STRING_ID_64;
	request.m_transitionId = stateId;

	request.m_useParentApRef = !params.m_apRefValid;
	request.m_params = params;
	request.m_params.m_pPrevInstance = CurrentStateInstance();

	if (request.m_params.m_blendType == DC::kAnimCurveTypeInvalid)
	{
		request.m_params.m_blendType == DC::kAnimCurveTypeLinear;
	}

	if (request.m_params.m_animFadeTime < 0.0f)
	{
		request.m_params.m_animFadeTime = 0.0f;
	}

	// Motion fade defaults to the animation fade if not specified
	if (request.m_params.m_motionFadeTime < 0.0f)
	{
		request.m_params.m_motionFadeTime = request.m_params.m_animFadeTime;
	}

	if ((request.m_params.m_motionFadeTime == 0.0f)
		&& (request.m_params.m_animFadeTime == 0.0f)
		&& (request.m_params.m_newInstBehavior == FadeToStateParams::kUnspecified))
	{
		request.m_params.m_newInstBehavior = FadeToStateParams::kSpawnNewTrack;
	}

	InstanceCallBackPendingChange(request.m_id, stateId, kPendingChangeFadeToState);

	return request.m_id;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StateChangeRequest AnimStateLayer::GetNextStateChangeRequest() const
{
	StateChangeRequest result;
	if (m_changeRequestsPending > 0)
	{
		result = m_changeRequestsPendingList[0];
	}
	return result;
}	

/// --------------------------------------------------------------------------------------------------------------- ///
const StateChangeRequest* AnimStateLayer::GetPendingChangeRequest(I32 index) const
{
	if (m_changeRequestsPending > index)
	{
		return &m_changeRequestsPendingList[index];
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const StateChangeRequest* AnimStateLayer::GetPendingChangeRequest(StateChangeRequest::ID id) const
{
	for (U32F i = 0; i < kMaxRequestsInFlight; ++i)
	{
		if (m_changeRequestsPendingList[i].m_id == id)
			return &m_changeRequestsPendingList[i];
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// For debugging only!  Determine if the AnimActor has changed (due to a reloaded .bin file) and update all pointers
/// to it.
void AnimStateLayer::ReloadScriptData(const DC::AnimActor* pAnimActor)
{
	PROFILE(Animation, ReloadScriptData);

	m_pAnimActor = pAnimActor;

	DebugOnly_ForceUpdateInstanceSnapShots();

	for (U32F i = 0; i < kMaxRequestsInFlight; ++i)
	{
		m_changeRequestsPendingList[i].m_srcStateName = INVALID_STRING_ID_64;
		m_changeRequestsProcessedList[i].m_srcStateName = INVALID_STRING_ID_64;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::RefreshAnimPointers()
{
	for (U32F i = 0; i < m_numTracks; ++i)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[i];
		pTrack->RefreshAnimPointers();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 AnimStateLayer::GetActiveSubsystemControllerId()
{
	return m_activeSubsystemControllerId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::SetActiveSubsystemControllerId(U32 subsystemControllerId)
{
	m_activeSubsystemControllerId = subsystemControllerId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::DebugOnly_ForceUpdateInstanceSnapShots()
{
	for (U32F i = 0; i < m_numTracks; ++i)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[i];
		pTrack->DebugOnly_ForceUpdateInstanceSnapShots(m_pAnimActor, m_pAnimData);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::DebugOnly_ForceUpdateOverlaySnapshot(const AnimOverlaySnapshot* pNewSnapshot)
{
	for (U32F i = 0; i < m_numTracks; ++i)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[i];
		pTrack->UpdateOverlaySnapshot(pNewSnapshot);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateLayer::AreTransitionsPending() const
{
	return m_changeRequestsPending > 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// The controller is considered 'busy' if a fade between states is in progress.
U32F AnimStateLayer::GetNumFadesInProgress() const
{
	const U32F numInstances = GetNumTotalInstances();
	return numInstances >= 1 ? numInstances - 1 : 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateLayer::IsInNonTransitionalState() const
{   
	const DC::AnimState* pCurState = CurrentState();
	if (!pCurState)
		return false;

	if ((pCurState->m_flags & DC::kAnimStateFlagTransitional) != 0)
		return false;

	return true;
}
 
/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimStateLayer::CurrentStateId() const
{
	const DC::AnimState* pCurState = CurrentState();
	if (pCurState == nullptr)
		return INVALID_STRING_ID_64;

	return pCurState->m_name.m_symbol;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AnimStateLayer::CurrentStateUserId() const
{
	const DC::AnimState* pCurState = CurrentState();
	if (pCurState == nullptr)
		return INVALID_STRING_ID_64;

	return pCurState->m_userId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimStateLayer::GetStatesCompletedLastUpdate() const
{
	return m_statesCompletedLastUpdate;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::TrapRequest(const AnimStateLayerRequestTrap& trap)
{
#if ENABLE_ANIM_STATE_LAYER_REQUEST_TRAP
	for (U32 i = 0; i < ARRAY_COUNT(m_traps); ++i)
	{
		if (!m_traps[i].IsValid())
		{
			m_traps[i] = trap;
			return;
		}
	}
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::ClearAllTraps()
{
#if ENABLE_ANIM_STATE_LAYER_REQUEST_TRAP
	for (U32 i = 0; i < ARRAY_COUNT(m_traps); ++i)
	{
		m_traps[i] = AnimStateLayerRequestTrap();
	}
#endif
}
/// --------------------------------------------------------------------------------------------------------------- ///
///  return handle to the transition request.
StateChangeRequest::ID AnimStateLayer::RequestTransition(StringId64 transitionId,
														 const FadeToStateParams* pParams /* = nullptr */)
{
	//ANIM_ASSERT(AreTransitionsEnabled());
	StateChangeRequest::ID requestHandle = StateChangeRequest::kInvalidId;
	
#if ENABLE_ANIM_STATE_LAYER_REQUEST_TRAP
	for (U32 i = 0; i < ARRAY_COUNT(m_traps); ++i)
	{
		if (m_traps[i].IsValid())
		{
			if (m_traps[i].m_transitionId == transitionId || CurrentStateId() == m_traps[i].m_awayFromStateId)
			{
				ALWAYS_HALTF(("Anim State Layer trapped RequestTransition(%s)", DevKitOnly_StringIdToString(transitionId)));

				if (m_traps[i].m_onceOnly)
				{
					m_traps[i] = AnimStateLayerRequestTrap();
				}
			}
		}
	}
#endif

	if (m_changeRequestsPending < 4)
	{
		MsgAnimVerbose("Animation(" PFS_PTR "): Accepted transition request (%s) id: %d\n", this, DevKitOnly_StringIdToString(transitionId), m_changeRequestNextId.GetVal());

		StateChangeRequest& request = m_changeRequestsPendingList[m_changeRequestsPending++];
		requestHandle = m_changeRequestNextId++;

		request.m_id = requestHandle;
		request.m_type = StateChangeRequest::kTypeFlagTransition;
		request.m_transitionId = transitionId;
		request.m_status = StateChangeRequest::kStatusFlagPending;
		request.m_requestTime = GetAnimationSystemTime();
		request.m_srcStateName = INVALID_STRING_ID_64;
		request.m_params = pParams ? *pParams : FadeToStateParams();
		request.m_useParentApRef = !request.m_params.m_apRefValid;

		SetActiveSubsystemControllerId(request.m_params.m_subsystemControllerId);
	}
	else
	{
		MsgAnim("ERROR Animation(" PFS_PTR "): Too many transition requests. Dropping new request (%s)\n", this, DevKitOnly_StringIdToString(transitionId));
		requestHandle = StateChangeRequest::kStatusFlagQueueFull;
	}

	InstanceCallBackPendingChange(requestHandle, transitionId, kPendingChangeRequestTransition);

	return requestHandle;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StateChangeRequest::ID AnimStateLayer::RequestPersistentTransition(StringId64 transitionId,
																   const FadeToStateParams* pParams /* = nullptr */)
{
	StateChangeRequest::ID requestHandle = StateChangeRequest::kInvalidId;

	if (m_changeRequestsPending < 4)
	{
		MsgAnimVerbose("Animation(" PFS_PTR "): Accepted transition request (%s)\n", this, DevKitOnly_StringIdToString(transitionId));

		StateChangeRequest& request = m_changeRequestsPendingList[m_changeRequestsPending++];
		requestHandle = m_changeRequestNextId++;

		request.m_id = requestHandle;
		request.m_type = StateChangeRequest::kTypeFlagTransition;
		request.m_transitionId = transitionId;
		request.m_status = StateChangeRequest::kStatusFlagPending | StateChangeRequest::kStatusFlagDontRemove;
		request.m_requestTime = GetAnimationSystemTime();
		request.m_srcStateName = INVALID_STRING_ID_64;
		request.m_params = pParams ? *pParams : FadeToStateParams();
		request.m_useParentApRef = !request.m_params.m_apRefValid;
	
		SetActiveSubsystemControllerId(request.m_params.m_subsystemControllerId);
	}
	else
	{
		MsgAnim("ERROR Animation(" PFS_PTR "): Too many transition requests. Dropping new request (%s)\n", this, DevKitOnly_StringIdToString(transitionId));
		requestHandle = StateChangeRequest::kStatusFlagQueueFull;
	}

	return requestHandle;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StateChangeRequest::StatusFlag AnimStateLayer::GetTransitionStatus(StateChangeRequest::ID transitionRequestId) const
{
	if (transitionRequestId != StateChangeRequest::kInvalidId)
	{
		for (U32F i = 0; i < m_changeRequestsPending; ++i)
		{
			if (m_changeRequestsPendingList[i].m_id == transitionRequestId)
			{
				return m_changeRequestsPendingList[i].m_status;
			}
		}

		for (U32F i = 0; i < kMaxRequestsInFlight; ++i)
		{
			if (m_changeRequestsProcessedList[i].m_id == transitionRequestId)
			{
				return m_changeRequestsProcessedList[i].m_status;
			}
		}
	}

	return StateChangeRequest::kStatusFlagInvalid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::UpdateTransitionApRef(StateChangeRequest::ID transitionRequestId, const BoundFrame &apRef)
{
	if (transitionRequestId != StateChangeRequest::kInvalidId)
	{
		for (U32F i = 0; i < m_changeRequestsPending; ++i)
		{
			if (m_changeRequestsPendingList[i].m_id == transitionRequestId)
			{
				m_changeRequestsPendingList[i].m_params.m_apRef = apRef;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateInstance* AnimStateLayer::GetTransitionDestInstance(StateChangeRequest::ID transitionRequestId) const
{
	if (transitionRequestId == StateChangeRequest::kInvalidId)
		return nullptr;

	for (U32F i = 0; i < kMaxRequestsInFlight; ++i)
	{
		if (m_changeRequestsProcessedList[i].m_id == transitionRequestId)
		{
			const AnimStateInstance* pInstance = GetInstanceById(m_changeRequestsProcessedList[i].m_instId);

			return pInstance;
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstance* AnimStateLayer::GetTransitionDestInstance(StateChangeRequest::ID transitionRequestId)
{
	if (transitionRequestId == StateChangeRequest::kInvalidId)
		return nullptr;

	for (U32F i = 0; i < kMaxRequestsInFlight; ++i)
	{
		if (m_changeRequestsProcessedList[i].m_id == transitionRequestId)
		{
			AnimStateInstance* pInstance = GetInstanceById(m_changeRequestsProcessedList[i].m_instId);

			return pInstance;
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StateChangeRequest::ID AnimStateLayer::RequestTransitionByFinalState(StringId64 destStateId,
																	 const FadeToStateParams* pParams,
																	 IAnimTransitionSearch* pCustomSearch)
{
	ANIM_ASSERT(destStateId != INVALID_STRING_ID_64);
	const DC::AnimState* pCurrState = CurrentState();

	if (!pCurrState || pCurrState->m_name.m_symbol == destStateId)
		return StateChangeRequest::kInvalidId;

	const DC::AnimTransition* pTrans = CurrentStateInstance()->GetActiveTransitionByDestState(destStateId,
																							  m_pInfoCollection,
																							  pCustomSearch);
	if (pTrans)
	{
		return RequestTransition(pTrans->m_name, pParams);
	}

	return StateChangeRequest::kInvalidId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::RemoveAllPendingTransitions(StateChangeRequest::TypeFlag transitionType)
{
	// Let's assume that all pending change requests are transitions
	I32F indexToLookAt = 0;
	for (I32F transitionsToLookAt = m_changeRequestsPending; transitionsToLookAt > 0; --transitionsToLookAt)
	{
		// Check if the current request is a 'Transition' request...
		if (m_changeRequestsPendingList[indexToLookAt].m_type & transitionType && !(m_changeRequestsPendingList[indexToLookAt].m_status & StateChangeRequest::kStatusFlagDontRemove))
		{
			//... if so, remove it.
			RemoveChangeRequestByIndex(indexToLookAt);
		}
		else
		{
			// ... OR... The current request in the list is a 'DirectFade'. Let's leave it there
			// and keep looking at the remaining requests.
			++indexToLookAt;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::DebugPrintToJson(char*& buf) const
{
	buf += sprintf(buf, "{\"name\":\"%s\", \"type\":", DevKitOnly_StringIdToString(GetName()));

	buf += sprintf(buf, "\"%s\", \"tracks\":[", "AnimStateLayer");
	for (U32F i = 0; i < m_numTracks; ++i)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[i];

		if (i > 0)
			buf += sprintf(buf, ",");

		buf += sprintf(buf, "{\"animFade\":%f, \"motionFade\":%f, \"instances\":[", pTrack->AnimFade(), pTrack->MotionFade());

		int numInstances = pTrack->GetNumInstances();

		for (int iInstance = 0; iInstance < numInstances; ++iInstance)
		{
			const AnimStateInstance* pInstance = pTrack->GetInstance(iInstance);

			if (iInstance > 0)
				buf += sprintf(buf, ",");

			const char* pakName = nullptr;

			const char* skelName = "<unknown>";

			if (const ArtItemAnim* itemAnim = pInstance->GetPhaseAnimArtItem().ToArtItem())
			{
				pakName = itemAnim->m_pDebugOnlyPakName;
				skelName = ResourceTable::LookupSkelName(itemAnim->m_skelID);
			}
			else
				pakName = "null";

			buf += sprintf(buf,
						   "{\"anim\":\"%s\", \"phase\":%f, \"animFade\":%f, \"motionFade\":%f, \"frame\":%f, \"frameCount\":%d, \"pakName\":\"%s\", \"skeletonName\":\"%s\"}",
						   DevKitOnly_StringIdToString(pInstance->GetPhaseAnim()),
						   pInstance->GetPhase(),
						   pInstance->AnimFade(),
						   pInstance->MotionFade(),
						   pInstance->MayaFrame(),
						   pInstance->GetFrameCount(),
						   pakName,
						   skelName);
			// 			pCurrentInst->m_stateSnapshot.DebugPrint(output,
			// 				pCurrentInst->GetAnimTable(),
			// 				pCurrentInst->GetAnimInfoCollection(),
			// 				pCurrentInst->Phase(),
			// 				pCurrentInst->AnimFade(),
			// 				pCurrentInst->MotionFade(),
			// 				pCurrentInst->IsFlipped(),
			// 				additiveLayer,
			// 				baseLayer,
			// 				pCurrentInst->GetFlags().m_phaseFrozen,
			// 				pCurrentInst->m_customApRefId,
			// 				2);

			if (pInstance->MasterFade() == 1.0f)
				break;
		}

		// end of instances
		buf += sprintf(buf, "]}");
	}

	// end of tracks
	buf += sprintf(buf, "]");

	// close layer
	buf += sprintf(buf, "}");
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::DebugPrint(MsgOutput output, U32 priority, const DC::AnimActorInfo* info) const
{
	if (!IsValid() || !Memory::IsDebugMemoryAvailable())
		return;

	if (!g_animOptions.m_debugPrint.ShouldShow(GetName(), m_debugPrintFakeBaseLayer))
		return;

	SetColor(output, 0xFF000000 | 0x00FFFFFF);
	PrintTo(output, "-----------------------------------------------------------------------------------------\n");

	SetColor(output, 0xFF000000 | 0x0055FF55);
	if (!g_animOptions.m_debugPrint.m_simplified)
	{
		PrintTo(output,
				"AnimStateLayer \"%s\": [%s], Pri %d, Cur Fade: %1.2f, Des Fade: %1.2f\n",
				DevKitOnly_StringIdToString(m_name),
				m_blendMode == ndanim::kBlendSlerp ? "blend" : "additive",
				priority,
				GetCurrentFade(),
				GetDesiredFade());

		if (const FeatherBlendTable::Entry* pEntry = g_featherBlendTable.GetEntry(m_featherBlendIndex))
		{
			PrintTo(output, " [%s:%.2f] ", DevKitOnly_StringIdToString(pEntry->m_featherBlendId), 1.0f);
		}
	}
	else
	{
		if (!g_animOptions.m_debugPrint.m_hideNonBaseLayers)
			PrintTo(output, "AnimStateLayer \"%s\"\n", DevKitOnly_StringIdToString(m_name));
	}

	if (const SnapshotNodeHeap* pHeap = GetSnapshotNodeHeap())
	{
		if (!g_animOptions.m_debugPrint.m_simplified && g_animOptions.m_printSnapshotHeapUsage)
		{
			PrintTo(output, "  ");
			pHeap->DebugPrint(output);
		}
	}

	const bool additiveLayer = m_blendMode == ndanim::kBlendAdditive;
	const bool baseLayer = GetName() == SID("base");

	for (U32F i = 0; i < m_numTracks; ++i)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[i];

		SetColor(output, 0xFF000000 | 0x0055FF55);
		if (!g_animOptions.m_debugPrint.m_simplified)
		{
			PrintTo(output,
					"  AnimStateInstanceTrack: Anim Fade: %1.2f, Motion Fade: %1.2f\n",
					pTrack->AnimFade(),
					pTrack->MotionFade());
		}
		else
		{
			PrintTo(output, "Blend %1.2f/1.0\n", pTrack->AnimFade());
		}

		if (i < (m_numTracks - 1))
		{
			const AnimStateInstanceTrack* pNextTrack = m_ppTrackList[i + 1];
			
			const AnimStateInstance* pCurInst = pTrack->CurrentStateInstance();
			const AnimStateInstance* pNextInst = pNextTrack->CurrentStateInstance();

			const ArtItemAnim* pCurPhaseAnim = pCurInst->GetPhaseAnimArtItem().ToArtItem();
			const ArtItemAnim* pNextPhaseAnim = pNextInst->GetPhaseAnimArtItem().ToArtItem();

			if (ValidBitsDiffer(pCurPhaseAnim, pNextPhaseAnim))
			{
				SetColor(output, kColorYellow.ToAbgr8());
				PrintTo(output, "  WARNING Blending mismatched partial sets (THIS WILL CAUSE A POP)\n");
				if (!g_animOptions.m_debugPrint.m_simplified)
				{
					PrintTo(output, "    '%s' != '%s'\n", pCurPhaseAnim->GetName(), pNextPhaseAnim->GetName());
				}
			}
		}

		pTrack->DebugPrint(output, additiveLayer, baseLayer, m_pAnimData);
	}

	if (!g_animOptions.m_debugPrint.m_hideTransitions)
	{
		PrintTo(output,"-----------------------------------------------------------------------------------------\n");

		for (U32F i = 0; i < m_changeRequestsPending; ++i)
		{
			const StateChangeRequest& changeReq = m_changeRequestsPendingList[m_changeRequestsPending - i - 1];
			PrintTo(output,
					"%u: [----.--] '%s' is Pending\n",
					changeReq.m_id.GetVal(),
					DevKitOnly_StringIdToString(changeReq.m_transitionId));
		}

		for (I32 i = kMaxRequestsInFlight - 1; i >= 0; --i)
		{
			U32F currentIndex = (m_changeRequestsProcessedListWriteIndex + i) % kMaxRequestsInFlight;

			if (m_changeRequestsProcessedList[currentIndex].m_id == StateChangeRequest::kInvalidId)
				continue;

			const bool typeFade = m_changeRequestsProcessedList[currentIndex].m_type == StateChangeRequest::kTypeFlagDirectFade;

			if (!g_animOptions.m_debugPrint.m_simplified)
			{
				PrintTo(output,
					"%u: [%.2f] '%s' from '%s'",
					m_changeRequestsProcessedList[currentIndex].m_id.GetVal(),
					m_changeRequestsProcessedList[currentIndex].m_requestTime,
					typeFade ? "fade" : DevKitOnly_StringIdToString(m_changeRequestsProcessedList[currentIndex].m_transitionId),
					DevKitOnly_StringIdToString(m_changeRequestsProcessedList[currentIndex].m_srcStateName));
			}

			switch (m_changeRequestsProcessedList[currentIndex].m_status)
			{
			case StateChangeRequest::kStatusFlagInvalid:
				{
					if (!g_animOptions.m_debugPrint.m_simplified)
					{
						if (m_changeRequestsProcessedList[currentIndex].m_type == StateChangeRequest::kTypeFlagDirectFade)
						{
							PrintTo(output,
									"- Invalid:(%s)\n",
									DevKitOnly_StringIdToString(m_changeRequestsProcessedList[currentIndex]
																	.m_transitionId));
						}
						else
							PrintTo(output, "- Invalid:\n");
					}
				}				
				break;
			case StateChangeRequest::kStatusFlagFailed:
				if (!g_animOptions.m_debugPrint.m_simplified)
				{
					PrintTo(output, " Failed:\n");
				}
				break;
			case StateChangeRequest::kStatusFlagQueueFull:
				if (!g_animOptions.m_debugPrint.m_simplified)
				{
					PrintTo(output, "- QueueFull:\n");
				}
				break;
			case StateChangeRequest::kStatusFlagPending:
				ANIM_ASSERT(false);
				break;
			case StateChangeRequest::kStatusFlagTaken:
				if (!g_animOptions.m_debugPrint.m_simplified)
				{
					PrintTo(output,
							" -> '%s'\n",
							DevKitOnly_StringIdToString(m_changeRequestsProcessedList[currentIndex].m_dstStateName));
				}
				break;
			}
		}
	}
} 

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::CreateAnimCmds(const AnimCmdGenLayerContext& context,
									AnimCmdList* pAnimCmdList,
									const DC::AnimActorInfo* info,
									F32 layerFadeOverride) const
{
	const ndanim::BlendMode layerBlendMode = GetBlendMode();
	
	U32F firstUnusedInstance = context.m_instanceZeroIsValid ? 1 : 0;
	U32F numBlendedTracks = 0;

	for (U32F i = 0; i < m_numTracks; ++i)
	{
		const U32F trackIndex = m_numTracks - i - 1;
		AnimStateInstanceTrack* pTrack = m_ppTrackList[trackIndex];

		pTrack->CreateAnimCmds(this, context, pAnimCmdList, firstUnusedInstance, trackIndex, layerFadeOverride);

		++firstUnusedInstance;
		++numBlendedTracks;

		if (numBlendedTracks >= 2)
		{
			const F32 fade = pTrack->AnimFade();
			ANIM_ASSERT(firstUnusedInstance >= 2);

			if (m_blendCallbacks.m_stateBlendFunc)
			{
				m_blendCallbacks.m_stateBlendFunc(context,
												  pAnimCmdList,
												  firstUnusedInstance - 2,
												  firstUnusedInstance - 1,
												  firstUnusedInstance - 2,
												  ndanim::kBlendSlerp,
												  fade);
			}
			else
			{
				pAnimCmdList->AddCmd_EvaluateBlend(firstUnusedInstance - 2,
												   firstUnusedInstance - 1,
												   firstUnusedInstance - 2,
												   ndanim::kBlendSlerp,
												   fade);
			}

			--firstUnusedInstance;
		}

		if (g_animOptions.m_generatingNetCommands)
		{
			pAnimCmdList->AddCmd_Track(i);
		}
	}

	if (context.m_instanceZeroIsValid)
	{
		const SkeletonId skelId = context.m_pAnimateSkel->m_skelId;

		if (m_preBlend)
		{
			m_preBlend(this, context, pAnimCmdList, skelId, 0, 1, 0, ndanim::kBlendSlerp, m_blendCallbackUserData);
		}

		F32 nodeBlendToUse = 1.0f;

		const OrbisAnim::ChannelFactor* const* ppChannelFactors = nullptr;
		const U32* pNumChannelFactors = nullptr;

		if (m_featherBlendIndex >= 0)
		{
			nodeBlendToUse = g_featherBlendTable.CreateChannelFactorsForAnimCmd(context.m_pAnimateSkel,
																				m_featherBlendIndex,
																				nodeBlendToUse,
																				&ppChannelFactors,
																				&pNumChannelFactors);
		}

		// blend all instances beyond the first down to the first
		if (ppChannelFactors)
		{
			pAnimCmdList->AddCmd_EvaluateFeatherBlend(0,
													  1,
													  1,
													  layerBlendMode,
													  nodeBlendToUse,
													  ppChannelFactors,
													  pNumChannelFactors,
													  m_featherBlendIndex);
		}
		else
		{
			pAnimCmdList->AddCmd_EvaluateBlend(0, 1, 0, ndanim::kBlendSlerp, 1.0f);
		}
		

		if (m_postBlend)
		{
			m_postBlend(this, context, pAnimCmdList, skelId, 0, 1, 0, ndanim::kBlendSlerp, m_blendCallbackUserData);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::UpdateAllApReferences(const BoundFrame& apReference,
										   AnimStateLayerFilterAPRefCallBack filterCallback)
{
	ANIM_ASSERT(IsFinite(apReference));
	ANIM_ASSERT(IsNormal(apReference.GetRotation()));
	ANIM_ASSERT(Length(apReference.GetTranslation()) < 10000.0f);

	for (U32F i = 0; i < m_numTracks; ++i)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[i];
		pTrack->UpdateAllApReferences(apReference, filterCallback);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::UpdateAllApReferencesUntilFalse(const BoundFrame& apReference,
													 AnimStateLayerFilterAPRefCallBack filterCallback)
{
	ANIM_ASSERT(IsFinite(apReference));
	ANIM_ASSERT(IsNormal(apReference.GetRotation()));
	ANIM_ASSERT(Length(apReference.GetTranslation()) < 10000.0f);

	for (U32F i = 0; i < m_numTracks; ++i)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[i];
		bool updated = pTrack->UpdateAllApReferencesUntilFalse(apReference, filterCallback);
		if (!updated)
			return;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::UpdateAllApReferencesTranslationOnly(Point_arg newApPos,
														  AnimStateLayerFilterAPRefCallBack filterCallback)
{
	ANIM_ASSERT(IsFinite(newApPos));
	ANIM_ASSERT(Length(newApPos) < 10000.f);

	for (U32F i = 0; i < m_numTracks; ++i)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[i];
		pTrack->UpdateAllApReferencesTranslationOnly(newApPos, filterCallback);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::TransformAllApReferences(const Locator& oldSpace, const Locator& newSpace)
{
	for (U32F i = 0; i < m_numTracks; ++i)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[i];
		pTrack->TransformAllApReferences(oldSpace, newSpace);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateLayer::GetApRefFromCurrentState(BoundFrame& bf) const
{
	const AnimStateInstance* pAnimStateInst = CurrentStateInstance();

	if (pAnimStateInst)
	{
		bf = pAnimStateInst->GetApLocator();
	}
	else
	{
		// This BoundFrame isn't really valid -- caller should check IsValid() and use
		// the object's BoundFrame instead if it's invalid.
		bf = BoundFrame(kIdentity);
	}

	return pAnimStateInst != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateLayer::GetApRefFromCurrentState(Locator& loc) const
{
	BoundFrame bf;
	bool valid = GetApRefFromCurrentState(bf);
	loc = bf.GetLocator();
	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::SetApRefOnCurrentState(const BoundFrame& apReference)
{
	ANIM_ASSERT(IsFinite(apReference));

	AnimStateInstance* pAnimStateInst = CurrentStateInstance();

	if (pAnimStateInst)
	{
		pAnimStateInst->SetApLocator(apReference);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
//same as GetApRefFromCurrentState, but can be used on any valid state
bool AnimStateLayer::GetApRefFromStateIndex(BoundFrame& bf, int index, int trackIndex /* = 0*/) const
{
	ANIM_ASSERT(trackIndex >= 0 && trackIndex < m_numTracks);
	const AnimStateInstanceTrack* pTopTrack = m_ppTrackList[trackIndex];

	if (!pTopTrack)
	{
		bf = BoundFrame(kIdentity);
		return false;
	}
	
	ANIM_ASSERT(index >= 0 && index < pTopTrack->GetNumInstances());
	const AnimStateInstance* pAnimStateInst = pTopTrack->GetInstance(index);

	if (pAnimStateInst)
	{
		bf = pAnimStateInst->GetApLocator();
	}

	return pAnimStateInst != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
//same as GetApRefFromCurrentState, but can be used on any valid state
bool AnimStateLayer::GetApRefFromStateIndex(Locator& loc, int index, int trackIndex /* = 0*/) const
{
	BoundFrame bf;
	bool valid = GetApRefFromStateIndex(bf, index, trackIndex);
	loc = bf.GetLocator();
	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::UpdateCurrentApReference(const BoundFrame& apReference)
{
	ANIM_ASSERT(IsFinite(apReference));
	ANIM_ASSERT(IsFinite(apReference.GetParentSpace()));
	ANIM_ASSERT(IsNormal(apReference.GetParentSpace().GetRotation()));
	ANIM_ASSERT(IsFinite(apReference.GetLocatorPs()));
	ANIM_ASSERT(IsNormal(apReference.GetRotationPs()));
	ANIM_ASSERT(IsFinite(apReference.GetLocator()));
	ANIM_ASSERT(IsNormal(apReference.GetRotation()));
	ANIM_ASSERT(Length(apReference.GetTranslation()) < 100000.0f);

	AnimStateInstance* pStateInst = CurrentStateInstance();
	pStateInst->SetApLocator(apReference);	
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Get a copy of the ApRef that will be used when the next transition is taken
void AnimStateLayer::UpdatePendingApReference(const BoundFrame& apReference)
{
	if (m_changeRequestsPending > 0)
	{
		StateChangeRequest* pRequest = &m_changeRequestsPendingList[0];

		pRequest->m_params.m_apRef = apReference;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Get a copy of the ApRef that will be used when the next transition is taken
void AnimStateLayer::CheckNextStateApRef(BoundFrame& outLoc) const
{
	// The next transition has already been requested so get the ApRef from it
	if (m_changeRequestsPending > 0)
	{
		const StateChangeRequest* pRequest = &m_changeRequestsPendingList[0];

		if (pRequest->m_useParentApRef)
		{
			GetApRefFromCurrentState(outLoc);
		}
		else
		{
			outLoc = pRequest->m_params.m_apRef;
		}
	}
	// No transitions have been requested and the queue is empty to get the ApRef from the parent state
	else
	{
		GetApRefFromCurrentState(outLoc);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
class EvalFloatBlender : public AnimStateLayer::InstanceBlender<float>
{
public:
	EvalFloatBlender(StringId64 channelName)
		: m_evaluated(false)
		, m_someNotEvaluated(false)
		, m_channelName(channelName)
	{
	}

	virtual float GetDefaultData() const override { return 0.0f; }

	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, float* pDataOut) override
	{
		AnimStateEvalParams params;
		params.m_forceChannelBlending = true;

		float result;
		if (!pInstance->EvaluateFloatChannels(&m_channelName, 1, &result, params))
		{
			m_someNotEvaluated = true;
			return false;
		}

		*pDataOut = result;
		m_evaluated = true;

		return true;
	}

	virtual float BlendData(const float& left,
							const float& right,
							float masterFade,
							float animFade,
							float motionFade) override
	{
		return Lerp(left, right, animFade);
	}

	bool m_evaluated;
	bool m_someNotEvaluated;
	StringId64 m_channelName;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class EvalApBlender : public AnimStateLayer::InstanceBlender<Maybe<Locator>>
{
public:
	EvalApBlender(StringId64 apChannelName, U32F flags)
		: m_flags(flags)
		, m_evaluated(false)
		, m_someNotEvaluated(false)
		, m_apChannelName(apChannelName)
	{
	}

	virtual Maybe<Locator> GetDefaultData() const override
	{
		if ((m_flags & kEvaluateAP_IgnoreInvalid) != 0 ||
			(m_flags & kEvaluateAP_TopOnly) != 0)
			return MAYBE::kNothing;
		return Locator(kIdentity);
	}

	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, Maybe<Locator>* pDataOut) override
	{
		ndanim::JointParams result;

		if ((m_flags & kEvaluateAP_TopOnly) != 0)
		{
			if (pInstance->GetLayer() && pInstance->GetLayer()->CurrentInstanceTrack()->GetInstance(0) != pInstance)
			{
				m_someNotEvaluated = true;
				return false;
			}
		}
		
		if (!pInstance->EvaluateChannels(&m_apChannelName, 1, &result))
		{
			m_someNotEvaluated = true;
			return false;
		}
		
		Locator locResult = Locator(result.m_trans, result.m_quat);

		*pDataOut = locResult;
		m_evaluated = true;

		return true;

	}

	virtual Maybe<Locator> BlendData(const Maybe<Locator>& leftData,
									 const Maybe<Locator>& rightData,
									 float masterFade,
									 float animFade,
									 float motionFade) override
	{
		if ((m_flags & kEvaluateAP_IgnoreInvalid) != 0 ||
			(m_flags & kEvaluateAP_TopOnly) != 0)
		{
			if (leftData.Valid() && rightData.Valid())
			{
				Locator interpolatedAP = Lerp(leftData.Get(), rightData.Get(), animFade);
				return interpolatedAP;
			}
			else if (leftData.Valid())
				return leftData.Get();
			else if (rightData.Valid())
				return rightData.Get();
			else
				return MAYBE::kNothing;
		}
		else
		{
			// GetDefaultData should always return a valid Maybe<Locator> when m_ignoreInvalid is false
			// so it should be safe to call left/rightData.Get() here
			Locator interpolatedAP = Lerp(leftData.Get(), rightData.Get(), animFade);
			return interpolatedAP;
		}
	}

	U32F m_flags;
	bool m_evaluated;
	bool m_someNotEvaluated;
	StringId64 m_apChannelName;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct EvalJointParamsData
{
	ndanim::JointParams* m_pJointParams = nullptr;
	float* m_pBlendVals = nullptr;
	U32F m_validMask = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class EvaluateChannelsBlender : public AnimStateLayer::InstanceBlender<EvalJointParamsData>
{
public:
	EvaluateChannelsBlender(FadeMethodToUse fadeMethod)
		: m_pChannelIds(nullptr)
		, m_numChannels(0)
		, m_fadeMethod(fadeMethod)
	{
	}

	virtual EvalJointParamsData GetDefaultData() const override
	{
		return EvalJointParamsData();
	}

	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, EvalJointParamsData* pDataOut) override
	{
		EvalJointParamsData instData;
		instData.m_pJointParams = NDI_NEW ndanim::JointParams[m_numChannels];
		instData.m_pBlendVals = NDI_NEW float[m_numChannels];

		AnimStateEvalParams params(m_params);

		instData.m_validMask = pInstance->EvaluateChannels(m_pChannelIds, m_numChannels, instData.m_pJointParams, params);

		for (int i = 0; i < m_numChannels; ++i)
		{
			if (instData.m_validMask & (1u << i))
			{
				instData.m_pBlendVals[i] = 1.0f;
			}
			else
			{
				instData.m_pBlendVals[i] = 0.0f;
			}
		}

		*pDataOut = instData;

		return true;
	}

	virtual EvalJointParamsData BlendData(const EvalJointParamsData& leftData,
										  const EvalJointParamsData& rightData,
										  float masterFade,
										  float animFade,
										  float motionFade) override
	{
		float fade = masterFade;
		switch (m_fadeMethod)
		{
		case kUseAnimFade: fade = animFade; break;
		case kUseMotionFade: fade = motionFade; break;
		}

		EvalJointParamsData blendedData;
		blendedData.m_pJointParams = NDI_NEW ndanim::JointParams[m_numChannels];
		blendedData.m_pBlendVals = NDI_NEW float[m_numChannels];

		for (U32F i = 0; i < m_numChannels; ++i)
		{
			const bool leftValid = (leftData.m_validMask & (1ULL << i)) != 0;
			const bool rightValid = (rightData.m_validMask & (1ULL << i)) != 0;

			if (leftValid && rightValid)
			{
				ANIM_ASSERT(leftData.m_pJointParams);
				ANIM_ASSERT(rightData.m_pJointParams);

				blendedData.m_pJointParams[i] = Lerp(leftData.m_pJointParams[i], rightData.m_pJointParams[i], fade);
				blendedData.m_pBlendVals[i] = Lerp(leftData.m_pBlendVals[i], rightData.m_pBlendVals[i], fade);
				blendedData.m_validMask |= (1ULL << i);
			}
			else if (leftValid)
			{
				blendedData.m_pJointParams[i] = leftData.m_pJointParams[i];
				blendedData.m_pBlendVals[i] = Lerp(leftData.m_pBlendVals[i], 0.0f, fade);
				blendedData.m_validMask |= (1ULL << i);
			}
			else if (rightValid)
			{
				blendedData.m_pJointParams[i] = rightData.m_pJointParams[i];
				blendedData.m_pBlendVals[i] = Lerp(0.0f, rightData.m_pBlendVals[i], fade);
				blendedData.m_validMask |= (1ULL << i);
			}
		}

		return blendedData;
	}

	const StringId64* m_pChannelIds;
	size_t m_numChannels;
	EvaluateChannelParams m_params;
	FadeMethodToUse m_fadeMethod;
};

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimStateLayer::EvaluateChannels(const StringId64* pChannelNames,
									  size_t numChannels,
									  ndanim::JointParams* pOutChannelJoints,
									  const EvaluateChannelParams& params,
									  FadeMethodToUse fadeMethod /* = kUseMotionFade */,
									  float* pOutBlendVals /* = nullptr */) const
{
	if (!pChannelNames || (0 == numChannels))
		return 0; 

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	EvaluateChannelsBlender blender(fadeMethod);
	blender.m_pChannelIds = pChannelNames;
	blender.m_numChannels = numChannels;
	blender.m_params = params;

	EvalJointParamsData res = blender.BlendForward(this, EvalJointParamsData());

	if (pOutChannelJoints)
	{
		for (U32F i = 0; i < numChannels; ++i)
		{
			if (res.m_validMask & (1ULL << i))
			{
				pOutChannelJoints[i] = res.m_pJointParams[i];
			}
			else
			{
				pOutChannelJoints[i].m_trans = kOrigin;
				pOutChannelJoints[i].m_quat = kIdentity;
				pOutChannelJoints[i].m_scale = Vector(SCALAR_LC(1.0f));
			}
		}
	}

	if (pOutBlendVals)
	{
		for (U32F i = 0; i < numChannels; ++i)
		{
			pOutBlendVals[i] = res.m_pBlendVals[i];
		}
	}

	return res.m_validMask;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimStateLayer::EvaluateFloat(StringId64 channelName,
									bool* pEvaluated,
									bool* pAllEvaluated,
									float defaultFloat) const
{
	EvalFloatBlender blender(channelName);

	F32 val = blender.BlendForward(this, defaultFloat);
	if (pEvaluated)
		*pEvaluated = blender.m_evaluated;
	if (pAllEvaluated)
		*pAllEvaluated = !blender.m_someNotEvaluated;
	return val;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator AnimStateLayer::EvaluateAP(StringId64 apChannelName,
										 bool* pEvaluated,
										 bool* pAllEvaluated,
										 const Locator* pDefaultLocator,
										 U32F flags) const
{
	EvalApBlender b(apChannelName, flags);

	const bool defaultNone = (flags & kEvaluateAP_IgnoreInvalid) != 0 || (flags & kEvaluateAP_TopOnly) != 0;
	Maybe<Locator> defLoc  = pDefaultLocator ? *pDefaultLocator : (defaultNone ? Maybe<Locator>(MAYBE::kNothing)
																			  : Maybe<Locator>(Locator(kIdentity)));
	Maybe<Locator> interpolatedAp = b.BlendForward(this, defLoc);

	if (pEvaluated)
		*pEvaluated = b.m_evaluated;

	if (pAllEvaluated)
		*pAllEvaluated = b.m_evaluated && !b.m_someNotEvaluated;

	if (interpolatedAp.Valid())
		return interpolatedAp.Get();
	return Locator(kIdentity);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateLayer::IsInstanceActive(const AnimStateInstance* pInstance) const
{
	if (pInstance == nullptr)
	{
		return false;
	}

	const ptrdiff_t index = pInstance - m_pAllocatedInstances;

	if (index >= m_numAllocatedInstances)
		return false;

	if (!m_usedInstances.IsBitSet(index))
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateInstance* AnimStateLayer::GetInstanceById(AnimStateInstance::ID id) const
{
	for (U32F iInstance = 0; iInstance < m_numAllocatedInstances; ++iInstance)
	{		
		if (m_usedInstances.IsBitSet(iInstance))
		{
			if (m_pAllocatedInstances[iInstance].GetId() == id)
				return &m_pAllocatedInstances[iInstance];
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::OnFree()
{
	for (U64 iInstance = m_usedInstances.FindFirstSetBit(); iInstance < m_numAllocatedInstances; iInstance = m_usedInstances.FindNextSetBit(iInstance))
	{
		m_pAllocatedInstances[iInstance].OnRelease();
		m_usedInstances.ClearBit(iInstance);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::InstanceCallBackPrepare(StateChangeRequest::ID requestId,
											 AnimStateInstance::ID instId,
											 bool isTop,
											 const DC::AnimState* pAnimState,
											 FadeToStateParams* pParams) const
{
	if (m_instanceCallbacks.m_prepare)
	{
		m_instanceCallbacks.m_prepare(m_instanceCallbacks.m_userData, GetName(), requestId, instId, isTop, pAnimState, pParams);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::InstanceCallBackCreate(AnimStateInstance* pInst) const
{
	if (m_instanceCallbacks.m_create)
		m_instanceCallbacks.m_create(m_instanceCallbacks.m_userData, pInst);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::InstanceCallBackDestroy(AnimStateInstance* pInst) const
{
	if (m_instanceCallbacks.m_destroy)
		m_instanceCallbacks.m_destroy(m_instanceCallbacks.m_userData, pInst);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateLayer::InstanceCallBackAlignFunc(const AnimStateInstance* pInst,
											   const BoundFrame& prevAlign,
											   const BoundFrame& currAlign,
											   const Locator& apAlignDelta,
											   BoundFrame* pAlignOut,
											   bool debugDraw) const
{
	bool ret = false;

	if (AnimStateLayerInstanceCallBack_AlignFunc* pFunc = m_instanceCallbacks.m_alignFunc)
	{
		ret = pFunc(m_instanceCallbacks.m_userData, pInst, prevAlign, currAlign, apAlignDelta, pAlignOut, debugDraw);
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::InstanceCallBackIkFunc(const AnimStateInstance* pInst,
											AnimPluginContext* pPluginContext,
											const void* pParams) const
{
	if (m_instanceCallbacks.m_ikFunc)
	{
		m_instanceCallbacks.m_ikFunc(m_instanceCallbacks.m_userData, pInst, pPluginContext, pParams);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::InstanceCallBackDebugPrintFunc(const AnimStateInstance* pInst,
													StringId64 debugType,
													IStringBuilder* pText) const
{
	if (m_instanceCallbacks.m_debugPrintFunc)
	{
		m_instanceCallbacks.m_debugPrintFunc(m_instanceCallbacks.m_userData, pInst, debugType, pText);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::InstanceCallBackPendingChange(StateChangeRequest::ID requestId,
												   StringId64 changeId,
												   int changeType) const
{
	if (m_instanceCallbacks.m_pendingChange)
	{
		m_instanceCallbacks.m_pendingChange(m_instanceCallbacks.m_userData, GetName(), requestId, changeId, changeType);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool CollectAnimsCallback(const AnimStateInstance* pInstance,
								 const AnimStateLayer* pStateLayer,
								 uintptr_t userData)
{
	AnimCollection* pCollection = (AnimCollection*)userData;

	if (const AnimSnapshotNode* pRootNode = pInstance->GetRootSnapshotNode())
	{
		pRootNode->CollectContributingAnims(&(pInstance->GetAnimStateSnapshot()), 1.0f, pCollection);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::CollectContributingAnims(AnimCollection* pCollection) const
{
	WalkInstancesOldToNew(CollectAnimsCallback, (uintptr_t)pCollection);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const BlendOverlay* AnimStateLayer::GetStateBlendOverlay() const
{
	return m_pAnimData && m_pAnimData->m_pAnimControl ? m_pAnimData->m_pAnimControl->GetStateBlendOverlay() : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimStateLayer::GetNumTotalInstances() const
{
	U32F numInstances = 0;

	for (U32F i = 0; i < m_numTracks; ++i)
	{
		const U32F index = m_numTracks - i - 1;
		const AnimStateInstanceTrack* pTrack = m_ppTrackList[index];

		numInstances += pTrack->GetNumInstances();
	}

	return numInstances;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateInstance* AnimStateLayer::FindInstanceByName(StringId64 stateName) const
{
	for (U32F i = 0; i < m_numTracks; ++i)
	{
		const U32F index = m_numTracks - i - 1;
		const AnimStateInstanceTrack* pTrack = m_ppTrackList[index];

		const AnimStateInstance* pInst = pTrack->FindInstanceByName(stateName);

		if (pInst)
			return pInst;
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateInstance* AnimStateLayer::FindInstanceByNameNewToOld(StringId64 stateName) const
{
	for (U32F i = 0; i < m_numTracks; ++i)
	{
		const AnimStateInstanceTrack* pTrack = m_ppTrackList[i];

		const AnimStateInstance* pInst = pTrack->FindInstanceByNameNewToOld(stateName);

		if (pInst)
			return pInst;
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateInstance* AnimStateLayer::FindInstanceById(AnimInstance::ID id) const
{
	for (U32F i = 0; i < m_numTracks; ++i)
	{
		const AnimStateInstanceTrack* pTrack = m_ppTrackList[i];

		const AnimStateInstance* pInst = pTrack->FindInstanceById(id);

		if (pInst)
			return pInst;
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateInstance* AnimStateLayer::CurrentStateInstance() const
{
	if (m_numTracks == 0)
		return nullptr;

	const AnimStateInstanceTrack* pTopTrack = m_ppTrackList[0];

	const AnimStateInstance* pCurrentInstance = pTopTrack->CurrentStateInstance();

	return pCurrentInstance;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstance* AnimStateLayer::CurrentStateInstance()
{
	if (m_numTracks == 0)
		return nullptr;

	AnimStateInstanceTrack* pTopTrack = m_ppTrackList[0];

	AnimStateInstance* pCurrentInstance = pTopTrack->CurrentStateInstance();

	return pCurrentInstance;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimState* AnimStateLayer::CurrentState() const
{
	const AnimStateInstance* currentStateInst = CurrentStateInstance();
	return currentStateInst ? &currentStateInst->m_stateSnapshot.m_animState : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimState* AnimStateLayer::CurrentState()
{
	const AnimStateInstance* currentStateInst = CurrentStateInstance();
	return currentStateInst ? &currentStateInst->m_stateSnapshot.m_animState : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimState* AnimStateLayer::FindStateByName(StringId64 stateName) const
{
	const AnimStateInstance* pInstance = FindInstanceByName(stateName);
	return pInstance ? &pInstance->m_stateSnapshot.m_animState : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AnimState* AnimStateLayer::FindStateByName(StringId64 stateName)
{
	const AnimStateInstance* pInstance = FindInstanceByName(stateName);
	return pInstance ? &pInstance->m_stateSnapshot.m_animState : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::DeleteNonContributingInstances()
{
	PROFILE(Animation, DeleteNonContributingInstances);

	for (U32F iTrack = 0; iTrack < m_numTracks; ++iTrack)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[iTrack];

		pTrack->DeleteNonContributingInstances(this);
	}

	DeleteNonContributingTracks();

	const U32F numSetTrackBits = m_usedTracks.CountSetBits();
	ANIM_ASSERTF(m_numTracks == numSetTrackBits, ("%d != %d", I32(m_numTracks), I32(numSetTrackBits)));
	const U32F numTotalInstances = GetNumTotalInstances();
	const U32F numSetBits = m_usedInstances.CountSetBits();
	ANIM_ASSERTF(numTotalInstances == numSetBits, ("%d != %d", I32(numTotalInstances), I32(numSetBits)));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::DeleteNonContributingTracks()
{
	if (0 == m_numTracks)
		return;

	bool parentTrackFullyBlendedIn = false;
	const U32F numTracks = m_numTracks;

	for (U32F iTrack = 0; iTrack < numTracks; ++iTrack)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[iTrack];
		const U32F numInstances = pTrack->GetNumInstances();

		if (parentTrackFullyBlendedIn || (numInstances == 0))
		{
			ReleaseTrack(pTrack);
		}
		else if (pTrack->MasterFade() == 1.0f)
		{
			parentTrackFullyBlendedIn = true;
		}
	}

	ANIM_ASSERT(m_numTracks == m_usedTracks.CountSetBits());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::ReleaseInstance(AnimStateInstance* pInstance)
{
	// Ok, let's remove this instance. Find the instance index and clear it.
	U64 usedInstanceIndex = 0;
	bool found = false;
	for (U32F iInstance = 0; iInstance < m_numAllocatedInstances; ++iInstance)
	{
		// This is only pointer arithmetic and not actually accessing the instance, hence the lack of load.
		if (pInstance == &m_pAllocatedInstances[iInstance])
		{
			found = true;
			break;
		}
		else
		{
			++usedInstanceIndex;
		}
	}

	ANIM_ASSERT(found);

	pInstance->OnRelease();
	m_usedInstances.ClearBit(usedInstanceIndex);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::ReleaseTrack(AnimStateInstanceTrack* pTrack)
{
	U64 usedTrackIndex = 0;
	bool found = false;
	for (U32F iTrack = 0; iTrack < m_numAllocatedTracks; ++iTrack)
	{
		if (pTrack == &m_pAllocatedTracks[iTrack])
		{
			found = true;
			break;
		}
		else
		{
			++usedTrackIndex;
		}
	}

	ANIM_ASSERT(found);
	if (!found)
		return;

	const U32F numInstances = pTrack->GetNumInstances();
	for (U32F i = 0; i < numInstances; ++i)
	{
		AnimStateInstance* pInstance = pTrack->GetInstance(i);
		ReleaseInstance(pInstance);
	}

	pTrack->Reset();

	--m_numTracks;
	m_usedTracks.ClearBit(usedTrackIndex);

	ANIM_ASSERT(m_usedTracks.CountSetBits() == m_numTracks);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::ResetAnimStateChannelDeltas()
{
	const U32F numTracks = m_numTracks;
	for (U32F iTrack = 0; iTrack < numTracks; ++iTrack)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[iTrack];
		pTrack->ResetAnimStateChannelDeltas();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::CopyActorInfoToCurrentState()
{
	AnimStateInstance* pCurrentStateInst = CurrentStateInstance();
	if (pCurrentStateInst)
	{
		const DC::AnimInfoCollection* pInfoCollection = m_pInfoCollection;
		if (pCurrentStateInst->m_flags.m_cacheTopInfo)
		{
			const DC::AnimTopInfo* pTopInfo = pInfoCollection->m_top;
			pCurrentStateInst->UpdateTopInfo(pTopInfo);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::CopyOverlaySnapshotToCurrentTrack()
{
	if (m_numTracks > 0)
	{
		AnimStateInstanceTrack* pCurrentInstTrack = m_ppTrackList[0];
		if (pCurrentInstTrack)
		{
			pCurrentInstTrack->UpdateOverlaySnapshot(m_pOverlaySnapshot);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::CopyOverlayVariantsBackToGlobalSnapshot()
{
	if (m_numTracks > 0)
	{
		AnimStateInstanceTrack* pCurrentInstTrack = m_ppTrackList[0];
		if (pCurrentInstTrack)
		{
			const AnimOverlaySnapshot* pTrackSnapshot = pCurrentInstTrack->GetOverlaySnapshot();
			m_pOverlaySnapshot->UpdateVariantsFrom(pTrackSnapshot);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::RemoveChangeRequestByIndex(U32F index,
												StateChangeRequest::StatusFlag status,
												AnimStateInstance* pDestInstance)
{
	ANIM_ASSERT(index < m_changeRequestsPending);
	if (index >= m_changeRequestsPending)
	{
		--m_changeRequestsPending;
		return;
	}

	if (status != StateChangeRequest::kStatusFlagIgnored)
	{
		const StateChangeRequest& input = m_changeRequestsPendingList[index];
		ProcessedChangeRequest& request = m_changeRequestsProcessedList[m_changeRequestsProcessedListWriteIndex];
		request.m_id					= input.m_id;
		request.m_instId				= AnimStateInstance::ID(-1);
		request.m_requestTime			= input.m_requestTime;
		request.m_srcStateName			= input.m_srcStateName;
		request.m_dstStateName			= INVALID_STRING_ID_64;
		if (pDestInstance)
		{
			request.m_dstStateName = pDestInstance->GetStateName();
			request.m_instId	   = pDestInstance->GetId();
		}
		request.m_transitionId = input.m_transitionId;
		request.m_status	   = status;
		request.m_type		   = input.m_type;

		// Advance and wrap.
		m_changeRequestsProcessedListWriteIndex = (m_changeRequestsProcessedListWriteIndex + 1) % kMaxRequestsInFlight;

		// Debug print
		switch (request.m_status)
		{
		case StateChangeRequest::kStatusFlagTaken:
			MsgAnimVerbose("Animation(" PFS_PTR "): Removing transition (%s) with status 'Taken'\n",
						   this,
						   DevKitOnly_StringIdToString(request.m_transitionId));
			break;
		case StateChangeRequest::kStatusFlagFailed:
			ANIM_ASSERTF(!input.m_params.m_assertOnFailure,
						 ("Removing transition (%s) with status 'Failed'",
						  DevKitOnly_StringIdToString(input.m_transitionId)));

			MsgAnimVerbose("WARNING Animation(" PFS_PTR "): Removing transition (%s) with status 'Failed'\n",
						   this,
						   DevKitOnly_StringIdToString(request.m_transitionId));
			break;
		case StateChangeRequest::kStatusFlagInvalid:
			MsgAnimVerbose("WARNING Animation(" PFS_PTR "): Removing transition (%s) with status 'Invalid'\n",
						   this,
						   DevKitOnly_StringIdToString(request.m_transitionId));
			break;
		case StateChangeRequest::kStatusFlagQueueFull:
			MsgAnimVerbose("WARNING Animation(" PFS_PTR "): Removing transition (%s) with status 'QueueFull'\n",
						   this,
						   DevKitOnly_StringIdToString(request.m_transitionId));
			break;
		case StateChangeRequest::kStatusFlagPending:
			MsgAnimVerbose("WARNING Animation(" PFS_PTR "): Removing transition (%s) with status 'Pending'\n",
						   this,
						   DevKitOnly_StringIdToString(request.m_transitionId));
			break;
		}
	}

	// remove the request from the pending list
	for (U32F i = index; i < m_changeRequestsPending - 1; ++i)
	{
		ANIM_ASSERT(i + 1 < kMaxRequestsInFlight);
		m_changeRequestsPendingList[i] = m_changeRequestsPendingList[i + 1];
	}
	--m_changeRequestsPending;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::RemoveChangeRequestByIndex(U32F index)
{
	RemoveChangeRequestByIndex(index, StateChangeRequest::kStatusFlagFailed, nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateLayer::TakeTransitions(AnimStateInstance** ppNewInstanceOut)
{
	PROFILE(Animation, TakeTransitions);

	//	MsgOut("## Anim ## - AnimStateLayer::TakeTransitions\n");

	ANIM_ASSERT(m_changeRequestsPending == 0
				  || m_changeRequestsPendingList[0].m_type == StateChangeRequest::kTypeFlagDirectFade
				  || m_changeRequestsPendingList[0].m_type == StateChangeRequest::kTypeFlagTransition);

	U32F iterCount = 0;
	bool transitionTaken = false;

	while ((m_changeRequestsPending > 0) && AreTransitionsEnabled())
	{
		StateChangeRequest& request = m_changeRequestsPendingList[0];

		ANIM_ASSERTF(!ShouldAssertOnStateChanges(),
					 ("%s: StateLayer attempting to perform transition '%s' but fades/transitions are currently locked out",
					  m_pAnimData ? DevKitOnly_StringIdToStringOrHex(m_pAnimData->m_hProcess.GetUserId()) : "(null)",
					  DevKitOnly_StringIdToStringOrHex(request.m_transitionId)));

		AnimStateInstance* pCurrentTopInst = CurrentStateInstance();

		ANIM_ASSERTF(iterCount++ < 1024,
					 ("Degenerate loop trying to take state transitions on layer '%s' (req: '%s') (cur inst: '%s')",
					  DevKitOnly_StringIdToString(GetName()),
					  DevKitOnly_StringIdToString(request.m_transitionId),
					  DevKitOnly_StringIdToString(pCurrentTopInst->GetStateName())));

		if (request.m_type == StateChangeRequest::kTypeFlagDirectFade)
		{
			request.m_srcStateName = pCurrentTopInst ? pCurrentTopInst->GetStateName() : INVALID_STRING_ID_64;

			const DC::AnimActor* pAnimActor = m_pAnimActor;
			const DC::AnimState* pState = AnimActorFindState(pAnimActor, request.m_transitionId);

			if (!pState)
			{
				MsgAnim("AnimStateLayer(" PFS_PTR ")::TakeTransitions: Could not find or log in state (%s)!\n",
						this,
						DevKitOnly_StringIdToString(request.m_transitionId));
				RemoveChangeRequestByIndex(0, StateChangeRequest::kStatusFlagInvalid, nullptr);
				++m_transitionsTakenLastUpdate;
			}
			else
			{
				// Get the latest ApRef from the parent if it exists
				const bool apMoveUpdate = pState->m_flags & (DC::kAnimStateFlagApMoveUpdate | DC::kAnimStateFlagFirstAlignRefMoveUpdate);
				if (request.m_useParentApRef && apMoveUpdate && pCurrentTopInst)
				{
					if (!(pCurrentTopInst->m_stateSnapshot.m_animState.m_flags & DC::kAnimStateFlagApMoveUpdate))
					{
						MsgAnim("AnimStateLayer(" PFS_PTR ")::TakeTransitions: Implicit ApMove but parent is not ApMove (%s)!\n",
								this,
								DevKitOnly_StringIdToString(request.m_transitionId));
					}
					request.m_params.m_apRef = pCurrentTopInst->GetApLocator();
					request.m_params.m_customApRefId = pCurrentTopInst->GetCustomApRefId();
				}

				// Motion fade defaults to the animation fade if not specified
				if (request.m_params.m_motionFadeTime < 0.0f)
					request.m_params.m_motionFadeTime = request.m_params.m_animFadeTime;

				// Apply blend overlays
				const DC::BlendOverlayEntry* pEntry = m_pAnimData->m_pAnimControl->LookupStateBlendOverlay(request.m_srcStateName, request.m_transitionId);
				if (pEntry)
				{
					const DC::BlendOverlayEntry entry = *pEntry;
					ASSERT(entry.m_key.m_srcId == request.m_srcStateName && entry.m_key.m_dstId == request.m_transitionId);
					if (entry.m_animFadeTime >= 0.0f)
					{
						request.m_params.m_animFadeTime = entry.m_animFadeTime;
						request.m_params.m_blendOverrideFlags |= AnimStateInstance::kAnimFadeTimeOverriden;
					}
					if (entry.m_motionFadeTime >= 0.0f)
					{
						request.m_params.m_motionFadeTime = entry.m_motionFadeTime;
						request.m_params.m_blendOverrideFlags |= AnimStateInstance::kMotionFadeTimeOverriden;
					}
					if (entry.m_curve != DC::kAnimCurveTypeInvalid)
					{
						request.m_params.m_blendType = entry.m_curve;
						request.m_params.m_blendOverrideFlags |= AnimStateInstance::kAnimCurveOverriden;
					}
				}

				ANIM_ASSERT(m_changeRequestsPendingList[0].m_type == StateChangeRequest::kTypeFlagDirectFade);

				request.m_params.m_pPrevInstance = pCurrentTopInst;
				AnimStateInstance* pNewInst = FadeToStateImmediate(pState, request.m_params, request.m_id);
				ANIM_ASSERTF(pNewInst,
							 ("Failed to transition to state '%s' -> '%s'",
							  DevKitOnly_StringIdToString(request.m_transitionId),
							  pState->m_name.m_string.GetString()));

				++m_statesCompletedLastUpdate;

				if (ppNewInstanceOut)
				{
					*ppNewInstanceOut = pNewInst;
				}

				ANIM_ASSERT(m_changeRequestsPendingList[0].m_type == StateChangeRequest::kTypeFlagDirectFade);

				RemoveChangeRequestByIndex(0, StateChangeRequest::kStatusFlagTaken, pNewInst);
				++m_transitionsTakenLastUpdate;
				transitionTaken = true;
			}
		}
		else if (request.m_type == StateChangeRequest::kTypeFlagTransition)
		{
			//			MsgOut("## Anim ## - AnimStateLayer::TakeTransitions - Transition\n");

			if (pCurrentTopInst == nullptr)
			{
				MsgAnim("Warning(" PFS_PTR "): No active state to transition from - Transition (%s)\n",
						this,
						DevKitOnly_StringIdToString(request.m_transitionId));

				// The transition does not exist in this state.
				// Move request to the 'processed' queue and mark as failed.
				RemoveChangeRequestByIndex(0, StateChangeRequest::kStatusFlagInvalid, nullptr);
				continue;
			}

			request.m_srcStateName = pCurrentTopInst->GetStateName();

			if (!AreTransitionsEnabled())
				break;

			TransitionQueryInfo tqInfo = pCurrentTopInst->MakeTransitionQueryInfo(m_pInfoCollection);
			tqInfo.m_pAnimTable = m_pAnimTable;
			tqInfo.m_pOverlaySnapshot = m_pOverlaySnapshot;

			// Check whether or not this state can handle this transition 
			if (AnimStateTransitionValid(&pCurrentTopInst->m_stateSnapshot.m_animState, request.m_transitionId, tqInfo))
			{
				// It can handle the transition. Let's now see if the conditions are such that we can take it right now.
				const DC::AnimInfoCollection* pInfoCollection = m_pInfoCollection;
				const DC::AnimTransition* pTrans = pCurrentTopInst->GetActiveTransitionByName(request.m_transitionId, pInfoCollection);
				if (pTrans)
				{
#ifdef ANIM_DEBUG
					MsgAnimVerbose("Animation(" PFS_PTR "): Taking transition (%s)\n", this, DevKitOnly_StringIdToString(pTrans->m_name));
#endif

					const TransitionQueryInfo qi = pCurrentTopInst->MakeTransitionQueryInfo(pInfoCollection);

					const bool isCurInst = true; // this function should only ever be called for the top instance
					const bool isTopTrack = true; // ditto for top track
					AnimStateInstance* pNewInst = ApplyTransition(m_ppTrackList[0],
																  pCurrentTopInst,
																  pTrans,
																  request.m_params,
																  true,
																  request.m_useParentApRef,
																  isTopTrack,
																  isCurInst,
																  m_pInfoCollection,
																  qi,
																  request.m_id);

					if (ppNewInstanceOut)
					{
						*ppNewInstanceOut = pNewInst;
					}

					// Move request to the 'processed' queue and mark as failed.
					RemoveChangeRequestByIndex(0, StateChangeRequest::kStatusFlagTaken, pNewInst);
					++m_transitionsTakenLastUpdate;
					transitionTaken = pNewInst != nullptr; // some transitions can be taken that don't result in a new instance! (like no-reset transitions)
				}
				else
				{
					// No, can't take it now... so let's exit and try again next frame.
					break;
				}
			}
			else
			{
#ifdef ANIM_DEBUG
				MsgAnim("Warning(" PFS_PTR "): Transition (%s) from state (%s) does not exist\n",
						this,
						DevKitOnly_StringIdToString(request.m_transitionId),
						CurrentState()->m_name.m_string.GetString());
#endif

				// The transition does not exist in this state.
				// Move request to the 'processed' queue and mark as failed.
				RemoveChangeRequestByIndex(0, StateChangeRequest::kStatusFlagInvalid, nullptr);
			}
		}
		else
		{
			ANIM_ASSERTF(false, ("Unknown pending change request type '%d' (0x%.8x)\n", request.m_type, this));
			RemoveChangeRequestByIndex(0, StateChangeRequest::kStatusFlagInvalid, nullptr);
		}
	}

	return transitionTaken;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateLayer::TakeAutoTransitions(bool tookNormalTransition, AnimStateInstance** ppNewInstanceOut)
{
	bool tookTransition = false;

	const DC::AnimInfoCollection* pGlobalInfoCollection = m_pInfoCollection;

	for (U32F iTrack = 0; iTrack < m_numTracks; ++iTrack)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[iTrack];

		const AnimStateInstance* pTrackTopInst = pTrack->GetInstance(0);

		const bool isTopTrack = (iTrack == 0);

		const DC::AnimInfoCollection* pTrackInfoCollection = pTrackTopInst->GetAnimInfoCollection();
		const DC::AnimInfoCollection* pDestInfoCollection  = (isTopTrack && !m_disableTopUpdateOverlayAndInfo)
																? pGlobalInfoCollection
																: pTrackInfoCollection;

		const U32F numInstances = pTrack->GetNumInstances();

		for (I32F iInstance = 0; iInstance < numInstances; ++iInstance)
		{
			AnimStateInstance* pInst = pTrack->GetInstance(iInstance);
			const bool isTopTrackInst = (iInstance == 0);

			const bool isCurrentInstance = isTopTrack && isTopTrackInst;

			if (tookNormalTransition && isCurrentInstance)
				continue;

			if (TakeAutoTransitions(pTrack, pInst, isTopTrack, isTopTrackInst, pDestInfoCollection, ppNewInstanceOut))
				tookTransition = true;
		}
	}

	return tookTransition;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateLayer::TakeAutoTransitions(AnimStateInstanceTrack* pTrack,
										 AnimStateInstance* pInst,
										 bool isTopTrack,
										 bool isTopTrackInst,
										 const DC::AnimInfoCollection* pDestInfoCollection,
										 AnimStateInstance** ppNewInstanceOut)
{
	//MsgOut("## Anim ## - AnimStateLayer::TakeAutoTransitions\n");
	if (!pInst)
		return false;

	if (pInst->IsAutoTransitionsDisabled())
		return false;

	const bool topTrack = (pTrack == m_ppTrackList[0]);
	const AnimOverlaySnapshot* pOverlaySnapshot = topTrack ? m_pOverlaySnapshot : pTrack->GetOverlaySnapshot();

	DC::AnimInfoCollection collection = *pInst->GetAnimInfoCollection();

	if (isTopTrack && isTopTrackInst)
	{
		collection.m_top = m_pInfoCollection->m_top;
	}

	TransitionQueryInfo tqInfo = pInst->MakeTransitionQueryInfo(&collection);
	tqInfo.m_pAnimTable = m_pAnimTable;
	tqInfo.m_pOverlaySnapshot = m_pOverlaySnapshot;

	// Check whether or not this state can handle this transition 
	if (!AnimStateTransitionValid(pInst->GetState(), SID("auto"), tqInfo))
		return false;

	// It can handle the transition. Let's now see if the conditions are such that we can take it right now.
	const DC::AnimTransition* pTrans = pInst->GetActiveTransitionByName(SID("auto"), &collection);
	if (!pTrans)
		return false;

	if ((pTrans->m_flags & DC::kAnimTransitionFlagInactiveWhileBlending) != 0)
	{
		const U32F numInstances = GetNumTotalInstances();
		if (numInstances > 1)
			return false;
	}

	FadeToStateParams params;
	params.m_subsystemControllerId = pInst->GetSubsystemControllerId();
	params.m_apRef = pInst->GetApLocator();

	AnimStateInstance* pNewInst = ApplyTransition(pTrack,
												  pInst,
												  pTrans,
												  params,
												  false,
												  true,
												  isTopTrack,
												  isTopTrackInst,
												  pDestInfoCollection,
												  tqInfo,
												  StateChangeRequest::kInvalidId);

	if (ppNewInstanceOut)
		*ppNewInstanceOut = pNewInst;

	if (pNewInst)
		return true;
	else
		return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstance* AnimStateLayer::ApplyTransition(AnimStateInstanceTrack* pTrack,
												   AnimStateInstance* pInstance,
												   const DC::AnimTransition* pTrans,
												   const FadeToStateParams& reqParams,
												   bool convertedStateCountAsCompleted,
												   bool useParentApRef,
												   bool isTopTrack,
												   bool isTopTrackInst,
												   const DC::AnimInfoCollection* pDestInfoCollection,
												   const TransitionQueryInfo& tqInfo, // the same TransitionQueryInfo used in IsTransitionActive()
												   StateChangeRequest::ID requestId /* = StateChangeRequest::kInvalid */)
{
	const BoundFrame& apRef			= reqParams.m_apRef;
	const float startPhaseOverride	= reqParams.m_stateStartPhase;
	const StringId64 custApRefId	= reqParams.m_customApRefId;
	const U32 subsystemControllerId = reqParams.m_subsystemControllerId;

	const DC::AnimTransitionFlag transFlags = pTrans->m_flags;

	// default to linear blending
	DC::AnimCurveType blendType = pTrans->m_curve;

	// If we are of blendType 'fade-out-layer' the fade time and fade blendType applies
	// to the fading out of the layer and not the transition which is ignored.
	if (transFlags & DC::kAnimTransitionFlagFadeOutLayer)
	{
		Fade(0.0f, pTrans->m_fadeTime, blendType);
		DisableTransitions();
		return nullptr;
	}

	if (transFlags & DC::kAnimTransitionFlagNoReset)
	{
		return nullptr;
	}

	F32 phaseSyncAdjustment = 0.0f;
	const bool doPhaseSync = (transFlags & DC::kAnimTransitionFlagPhaseSync) || reqParams.m_phaseSync;

	if (doPhaseSync)
	{
		phaseSyncAdjustment += pInstance->Phase();
	}

	F32 startPhase = pTrans->m_startPhase;

	if (startPhaseOverride >= 0.0f && startPhaseOverride <= 1.0f)
	{
		startPhase = startPhaseOverride;
	}

	startPhase += phaseSyncAdjustment;

	if (doPhaseSync)
	{
		// phase sync anims need to allow phase 1.0
		if (startPhase > 1.0f)
		{
			startPhase -= 1.0f;
		}
	}
	else
	{
		if (startPhase >= 1.0f)
		{
			startPhase -= 1.0f;
		}
	}
	
	MsgAnimVerbose("Animation(" PFS_PTR "): Applying Transition '%s' to state '%s'\n",
				   this,
				   DevKitOnly_StringIdToString(pTrans->m_name),
				   pInstance->GetState()->m_name.m_string.GetString());

	// Get the latest ApRef from the parent if it exists
	const DC::AnimState* pDestState = pTrans->m_state;
	const bool apMoveUpdate = pDestState->m_flags & (DC::kAnimStateFlagApMoveUpdate | DC::kAnimStateFlagFirstAlignRefMoveUpdate);
	const bool usingParentApRef = useParentApRef && apMoveUpdate;

	const BoundFrame apRefToUse = usingParentApRef ? pInstance->GetApLocator() : apRef;

	float animFadeTime = pTrans->m_fadeTime;
	float motionFadeTime = pTrans->m_motionFadeTime;

	if (pTrans->m_fadeTimeLambda != nullptr)
	{
		const DC::AnimInfoCollection transInfoCollection(*tqInfo.m_pInfoCollection);

		const ScriptValue argv[] =
		{
			ScriptValue(&transInfoCollection),
			ScriptValue(tqInfo.m_frame),
			ScriptValue(tqInfo.m_phaseAnimId),
			ScriptValue(tqInfo.m_phaseAnimLooping)
		};

		float value = ScriptManager::Eval(pTrans->m_fadeTimeLambda, SID("anim-transition-fade-time-func"), ARRAY_COUNT(argv), argv).m_float;
		if (value >= 0.f)
			animFadeTime = value;
	}

	if (reqParams.m_animFadeTime >= 0.0f)
		animFadeTime = reqParams.m_animFadeTime;
	if (reqParams.m_motionFadeTime >= 0.0f)
		animFadeTime = reqParams.m_motionFadeTime;
	if (reqParams.m_blendType != DC::kAnimCurveTypeInvalid)
		blendType = reqParams.m_blendType;

	if (pTrans->m_flags & (DC::kAnimTransitionFlagScaleBlendTimeByRate))
	{
		animFadeTime /= pInstance->m_phaseRateEstimate;
		motionFadeTime /= pInstance->m_phaseRateEstimate;
	}

	// Motion fade defaults to the animation fade if not specified
	if (motionFadeTime < 0.0f)
	{
		motionFadeTime = animFadeTime;
	}

	// Apply blend overlays
	U32 blendOverrideFlags = 0;
	{
		const StringId64 srcStateId = pInstance->GetState()->m_name.m_symbol;
		const StringId64 destStateId = pDestState->m_name.m_symbol;

		const DC::BlendOverlayEntry* pEntry = m_pAnimData->m_pAnimControl->LookupStateBlendOverlay(srcStateId, destStateId);

		if (pEntry)
		{
			const DC::BlendOverlayEntry entry = *pEntry;

			if (entry.m_allowAutoTransition || pTrans->m_name != SID("auto"))
			{
				if (entry.m_animFadeTime >= 0.0f)
				{
					animFadeTime = entry.m_animFadeTime;
					blendOverrideFlags |= AnimStateInstance::kAnimFadeTimeOverriden;
				}
				if (entry.m_motionFadeTime >= 0.0f)
				{
					motionFadeTime = entry.m_motionFadeTime;
					blendOverrideFlags |= AnimStateInstance::kMotionFadeTimeOverriden;
				}
				if (entry.m_curve != DC::kAnimCurveTypeInvalid)
				{
					blendType = entry.m_curve;
					blendOverrideFlags |= AnimStateInstance::kAnimCurveOverriden;
				}
			}
		}
	}

	FadeToStateParams params;
	params.m_apRef = apRefToUse;
	params.m_customApRefId = custApRefId != INVALID_STRING_ID_64 ? custApRefId : usingParentApRef ? pInstance->GetCustomApRefId() : INVALID_STRING_ID_64;
	params.m_stateStartPhase = startPhase;
	params.m_animFadeTime = animFadeTime;
	params.m_motionFadeTime = motionFadeTime;
	params.m_blendType = blendType;
	params.m_freezeSrcState = transFlags & DC::kAnimTransitionFlagFreezeSrcState;
	params.m_freezeDestState = transFlags & DC::kAnimTransitionFlagFreezeDestState;
	params.m_allowStateLooping = true;
	params.m_skipFirstFrameUpdate = false;
	params.m_preventBlendTimeOverrun = transFlags & DC::kAnimTransitionFlagLimitBlendTime;
	params.m_pPrevInstance = pInstance;
	params.m_pTrack = pTrack;
	params.m_pDestInfoCollection = pDestInfoCollection;
	params.m_subsystemControllerId = subsystemControllerId;
	params.m_blendOverrideFlags = blendOverrideFlags;
	
	if (doPhaseSync)
	{
		params.m_ignoreStartPhaseFunc = true;
	}

	if (transFlags & DC::kAnimTransitionFlagPreserveFeatherBlendTable)
	{
		params.m_customFeatherBlendTableIndex = pInstance->m_customFeatherBlendTableIndex;
		params.m_customFeatherBlendTableBlend = pInstance->GetFeatherBlendTableBlend();
	}

	// First check if transition specifies same or new track. Next check if anim state specifies same or new track. Otherwise use layer default setting.
	if (transFlags & DC::kAnimTransitionFlagSpawnOnNewTrack)
		params.m_newInstBehavior = FadeToStateParams::kSpawnNewTrack;
	else if (transFlags & DC::kAnimTransitionFlagSpawnOnSameTrack)
		params.m_newInstBehavior = FadeToStateParams::kUsePreviousTrack;
	else if (pDestState->m_flags & DC::kAnimStateFlagSpawnOnNewTrack)
		params.m_newInstBehavior = FadeToStateParams::kSpawnNewTrack;
	else if (pDestState->m_flags & DC::kAnimStateFlagSpawnOnSameTrack)
		params.m_newInstBehavior = FadeToStateParams::kUsePreviousTrack;
	else
		params.m_newInstBehavior = m_defNewInstBehavior;

	const bool willSpawnNewTrack = (params.m_newInstBehavior == FadeToStateParams::kSpawnNewTrack);

	// only the top instance in a track can spawn new instances
	// likewise only the top track can have instances spawn new tracks
	if ((!isTopTrackInst) || (willSpawnNewTrack && !isTopTrack))
	{
		if (CanLoopInstance(pInstance, pDestState, params))
		{
			const DC::AnimInfoCollection* pInfoCollection = pInstance->GetAnimInfoCollection();
			pInstance->Loop(pInfoCollection, isTopTrackInst, params);
			return pInstance;
		}

		return nullptr;
	}

	AnimStateInstance* pNewInst = FadeToStateImmediate(pDestState, params, requestId);

	if (pTrans->m_fadeTime <= 0.0f && convertedStateCountAsCompleted)
		++m_statesCompletedLastUpdate;

	++m_transitionsTakenLastUpdate;

	return pNewInst;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstance* AnimStateLayer::FadeToStateImmediate(const DC::AnimState* pState,
														const FadeToStateParams& params,
														StateChangeRequest::ID requestId)
{
	MsgAnimVerbose("Animation(" PFS_PTR "): Fading to state (%s) over %f seconds [anim] %f seconds [motion]\n",
				   this,
				   pState->m_name.m_string.GetString(),
				   params.m_animFadeTime,
				   params.m_motionFadeTime);
	AnimStateInstance* pNewInst = SetState(pState, params, requestId);
	return pNewInst;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstance* AnimStateLayer::AllocateOrReclaimInstance()
{
	AnimStateInstance* pNewInst = nullptr;

	const U64 freeInstanceIndex = m_usedInstances.FindFirstClearBit();
	if (freeInstanceIndex >= m_numAllocatedInstances)
	{
		ANIM_ASSERT(m_numTracks >= 1);

		for (I32F i = m_numTracks - 1; i >= 0; --i)
		{
			AnimStateInstanceTrack* pTrack = m_ppTrackList[i];

			if (pTrack->GetNumInstances() == 0)
				continue;

			pNewInst = pTrack->ReclaimInstance();
			ANIM_ASSERT(pNewInst);
			return pNewInst;
		}

		// if we get here then it means we went through all our tracks and none had any instances
		// but we're supposedly maxed out on allocated instances, so something went very wrong
		ANIM_ASSERTF(false, ("Cats and dogs living together"));
	}
	else
	{
		pNewInst = &m_pAllocatedInstances[freeInstanceIndex];
		m_usedInstances.SetBit(freeInstanceIndex);
	}

	return pNewInst;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstanceTrack* AnimStateLayer::AllocateOrReclaimInstanceTrack()
{
	AnimStateInstanceTrack* pNewTrack = nullptr;

	const U64 freeTrackIndex = m_usedTracks.FindFirstClearBit();
	if (freeTrackIndex >= m_numAllocatedTracks)
	{
		pNewTrack = m_ppTrackList[m_numAllocatedTracks - 1];

		const U32F numInstances = pNewTrack->GetNumInstances();
		for (U32F iInstance = 0; iInstance < numInstances; ++iInstance)
		{
			ReleaseInstance(pNewTrack->GetInstance(iInstance));
		}

		/*		MsgAnim("ERROR - Animation(" PFS_PTR "): AnimStateLayer::SetState reclaiming a contributing instance track in AnimActor \"%s\"\n",
		this,
		m_pAnimActor->m_name.m_string.c_str());*/

		m_numTracks = m_numAllocatedTracks - 1;
	}
	else
	{
		pNewTrack = &m_pAllocatedTracks[freeTrackIndex];
		m_usedTracks.SetBit(freeTrackIndex);
	}

	return pNewTrack;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimStateInstanceTrack* AnimStateLayer::GetTrackForInstance(const AnimStateInstance* pInstance) const
{
	for (U32F i = 0; i < m_numTracks; ++i)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[i];

		if (pTrack->HasInstance(pInstance))
			return pTrack;
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool DisableAutoTrans(AnimStateInstance* pInstance, AnimStateLayer* pStateLayer, uintptr_t userData)
{
	pInstance->DisableAutoTransitions();
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstance* AnimStateLayer::SetState(const DC::AnimState* pState,
											const FadeToStateParams& params,
											StateChangeRequest::ID requestId)
{
	const AnimStateInstance* pPrevInst = params.m_pPrevInstance;
	const DC::AnimInfoCollection* pInfoCollection = params.m_pDestInfoCollection ? params.m_pDestInfoCollection
																				 : m_pInfoCollection;

	ANIM_ASSERT(pInfoCollection);

	if (pState->m_flags & DC::kAnimStateFlagDisablePrevInstsAutoTrans)
	{
		// disable auto transitions on previous tracks/instances
		uintptr_t dummyPtr = -1;
		WalkInstancesNewToOld(DisableAutoTrans, dummyPtr);
	}

	if (CanLoopInstance(params.m_pPrevInstance, pState, params))
	{
		const bool isInstanceTrackTopping = IsInstanceTopOfTrack(params.m_pPrevInstance);
		params.m_pPrevInstance->Loop(pInfoCollection, isInstanceTrackTopping, params);

		MsgAnimVerbose("Animation(" PFS_PTR "): Looping current to state (%s)\n",
					   this,
					   pState->m_name.m_string.GetString());
		return params.m_pPrevInstance;
	}

	FadeToStateParams nonConstParams = params;

	if (nonConstParams.m_animFadeTime < 0.0f)
		nonConstParams.m_animFadeTime = 0.0f;
	if (nonConstParams.m_motionFadeTime < 0.0f)
		nonConstParams.m_motionFadeTime = 0.0f;
	if (nonConstParams.m_blendType == DC::kAnimCurveTypeInvalid)
		nonConstParams.m_blendType = DC::kAnimCurveTypeLinear;

	nonConstParams.m_isBase = GetName() == SID("base");

	AnimStateInstance* pNewInst = AllocateOrReclaimInstance();
	ANIM_ASSERT(pNewInst);

	++m_numStatesStarted;
	if (m_numStatesStarted == INVALID_ANIM_INSTANCE_ID.GetValue())
		++m_numStatesStarted; // handle wrap-around

	AnimStateInstance::ID id(m_numStatesStarted);

	FadeToStateParams::NewInstanceBehavior newInstBehavior = params.m_newInstBehavior;
	if (newInstBehavior == FadeToStateParams::kUnspecified)
	{
		if (pState->m_flags & DC::kAnimStateFlagSpawnOnNewTrack)
			newInstBehavior = FadeToStateParams::kSpawnNewTrack;
		else if (pState->m_flags & DC::kAnimStateFlagSpawnOnSameTrack)
			newInstBehavior = FadeToStateParams::kUsePreviousTrack;
		else
			newInstBehavior = m_defNewInstBehavior;
	}

	const bool spawnOnNewTrack = (newInstBehavior == FadeToStateParams::kSpawnNewTrack) || (m_numTracks == 0);

	bool isTopTrack = false;
	AnimStateInstanceTrack* pTrack = nullptr;

	if (spawnOnNewTrack)
	{
		m_disableTopUpdateOverlayAndInfo = false;
		pTrack = AllocateOrReclaimInstanceTrack();

		pTrack->Reset();
		pTrack->Init(m_pOverlaySnapshot);

		ANIM_ASSERT(m_numTracks < m_numAllocatedTracks);
		for (U32F iTrack = m_numTracks; iTrack > 0; --iTrack)
		{
			m_ppTrackList[iTrack] = m_ppTrackList[iTrack - 1];
		}

		m_ppTrackList[0] = pTrack;

		isTopTrack = true;

		++m_numTracks;
	}
	else
	{
		if (params.m_pTrack)
		{
			pTrack = params.m_pTrack;
		}
		else
		{
			pTrack = m_ppTrackList[0];
		}

		ANIM_ASSERT(pTrack);

		if (!pTrack->CanPushInstance())
		{
			ANIM_ASSERT(pTrack->GetMaxNumInstances() > 0);
			ANIM_ASSERT(pTrack->GetNumInstances() > 0);
			AnimStateInstance* pReclaimedInstance = pTrack->ReclaimInstance();
			ReleaseInstance(pReclaimedInstance);
		}

		isTopTrack = (pTrack == m_ppTrackList[0]);
	}

	// Call Prepare before setting inst id: Inst isn't setup yet so we don't want to be able to find it
	InstanceCallBackPrepare(requestId, id, isTopTrack, pState, &nonConstParams);

	// InstanceCallBackPrepare can modify the FadeToStateParams. It's too late to change some, so just assert if those changed.
	ANIM_ASSERT(params.m_newInstBehavior == nonConstParams.m_newInstBehavior);
	
	pNewInst->SetId(id);

	if (params.m_preventBlendTimeOverrun && pPrevInst)
	{
		const float duration = pPrevInst->GetDuration();
		const float phase = pPrevInst->Phase();
		const float phaseRate = pPrevInst->PhaseRateEstimate();
		const float remainingTime = Limit01(1.0f - phase) * duration / phaseRate;
		nonConstParams.m_animFadeTime = Min(nonConstParams.m_animFadeTime, remainingTime);
		nonConstParams.m_motionFadeTime = Min(nonConstParams.m_motionFadeTime, remainingTime);
	}

	bool updatedOverlay = false;
	if (isTopTrack && !m_disableTopUpdateOverlayAndInfo && !params.m_preserveOverlays)
	{
		updatedOverlay = true;
		// ensure new instances in the top track always get the freshest overlays
		pTrack->UpdateOverlaySnapshot(m_pOverlaySnapshot);
	}

	ANIM_ASSERT(pTrack);
	const bool topTrack = (pTrack == m_ppTrackList[0]);
	AnimOverlaySnapshot* pTrackOverlaySnapShot = pTrack->GetOverlaySnapshot();

	AnimOverlaySnapshot* pOverlaySnapshot = (updatedOverlay || !pTrackOverlaySnapShot) ? m_pOverlaySnapshot
																					   : pTrackOverlaySnapShot;

	if (params.m_preserveInstanceInfo && pPrevInst)
	{
		pInfoCollection = pPrevInst->GetAnimInfoCollection();
	}

	if (pPrevInst)
	{
		nonConstParams.m_animDeltaTweakXform = pPrevInst->GetAnimDeltaTweakTransform();
	}

	pNewInst->m_stateSnapshot.m_flags.m_disableRandomization = m_pAnimData->m_pAnimControl->IsRandomizationDisabled();

	const StringId64 prevPhaseAnimId = pPrevInst ? pPrevInst->GetAnimStateSnapshot().m_translatedPhaseAnimName
												 : INVALID_STRING_ID_64;
	const BlendOverlay* pAnimBlendOverlay = m_pAnimData->m_pAnimControl->GetAnimBlendOverlay();

	pNewInst->Init(pState,
				   pInfoCollection,
				   pOverlaySnapshot,
				   m_pAnimData,
				   prevPhaseAnimId,
				   pAnimBlendOverlay,
				   nonConstParams);

	ANIM_ASSERT(pTrack->CanPushInstance());
	pTrack->PushInstance(pNewInst);

	// Propagate certain values from the previous instance
	if (pPrevInst)
	{
		// If the channel deltas are non-zero it means that this transition was taken after advancing the phase (end transition?)
		// In this case we want to preserve the align delta from the previous instance.
		const U32F numChannelDeltas = pNewInst->m_numChannelDeltas;
		ANIM_ASSERT(numChannelDeltas == pPrevInst->m_numChannelDeltas);

		if (numChannelDeltas)
		{
			Locator* pChannelDeltas = pNewInst->m_pChannelDelta;
			const Locator* pPrevChannelDeltas = pPrevInst->m_pChannelDelta;
			Locator* pChannelPrevLocs = pNewInst->m_pChannelPrevLoc;
			const Locator* pPrevChannelPrevLocs = pPrevInst->m_pChannelPrevLoc;
			Locator* pChannelCurrLocs = pNewInst->m_pChannelCurrLoc;
			const Locator* pPrevChannelCurrLocs = pPrevInst->m_pChannelCurrLoc;
			for (U32F i = 0; i < numChannelDeltas; ++i)
			{
				pChannelDeltas[i] = pPrevChannelDeltas[i];
				pChannelPrevLocs[i] = pPrevChannelPrevLocs[i];
				pChannelCurrLocs[i] = pPrevChannelCurrLocs[i];
			}
		}

		if ((pNewInst->GetStateFlags() & DC::kAnimStateFlagAdjustApToRestrictAlign) &&
			(pPrevInst->GetStateFlags() & DC::kAnimStateFlagAdjustApToRestrictAlign) &&
			params.m_inheritApRestrictAdjustment)
		{
			const Vector adjustmentPs = pPrevInst->GetApRestrictAdjustmentPs();
			pNewInst->ApplyApRestrictAdjustPs(adjustmentPs);
		}
	}

	MsgAnimVerbose("Animation(" PFS_PTR "): Setting state (%s)\n", this, pState->m_name.m_string.GetString());

	InstanceCallBackCreate(pNewInst);

	return pNewInst;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateLayer::WalkStepCompletedInstances(AnimStateInstance* pInstance,
												AnimStateLayer* pStateLayer,
												uintptr_t userData)
{
	EffectList* pTriggeredEffects = (EffectList*)userData;

	if (pInstance->Phase() < 1.0f)
		return true;

	const AnimStateInstance* pCurrentInstance = pStateLayer->CurrentStateInstance();
	const bool isCurInst = (pInstance == pCurrentInstance);

	AnimStateInstanceTrack* pTrack = pStateLayer->GetTrackForInstance(pInstance);
	AnimStateInstanceTrack* pTopTrack = pStateLayer->GetTrackForInstance(pCurrentInstance);
	const bool isTopTrack = (pTopTrack == pTrack);

	if (isCurInst)
	{
		pStateLayer->m_statesCompletedLastUpdate = pStateLayer->m_statesCompletedLastUpdate + 1;
	}

	const float remainderTime = pInstance->m_remainderTime;

	bool tookTransition = false;

	if (pStateLayer->AreTransitionsEnabled())
	{
		AnimStateInstance* pNewInst = nullptr;

		if (isCurInst)
		{
			tookTransition = pStateLayer->TakeTransitions(&pNewInst);
			ANIM_ASSERT(!tookTransition || pNewInst);
		}

		if (!tookTransition)
		{
			const DC::AnimInfoCollection* pDestInfoCollection = (isTopTrack
																 && !pStateLayer->m_disableTopUpdateOverlayAndInfo)
																	? pStateLayer->m_pInfoCollection
																	: pInstance->GetAnimInfoCollection();
			tookTransition = pStateLayer->TakeAutoTransitions(pTrack,
															  pInstance,
															  isTopTrack,
															  isCurInst,
															  pDestInfoCollection,
															  &pNewInst);
			ANIM_ASSERT(!tookTransition || pNewInst);
		}

		// update top info structure
		if (tookTransition)
		{
			if (isCurInst && !pStateLayer->m_disableTopUpdateOverlayAndInfo)
			{
				pStateLayer->CopyActorInfoToCurrentState();
			}

			ANIM_ASSERT(pNewInst);
			pNewInst->PhaseUpdate(remainderTime, isCurInst, pTriggeredEffects);
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void RemoveEffectsWithLowestBlend(EffectList* pTriggeredEffects)
{
	AnimInstance::ID bestInstanceId;
	float			 bestInstanceFade = -1.0f;

	// first pass, find the state with the highest fade
	for (int i = 0; i < pTriggeredEffects->GetNumEffects(); i++)
	{
		const EffectAnimInfo* pEffectAnimInfo = pTriggeredEffects->Get(i);

		if (!pEffectAnimInfo->m_isMotionMatchingState)
			continue;

		if (pEffectAnimInfo->m_animBlend > bestInstanceFade)
		{
			bestInstanceId = pEffectAnimInfo->m_instId;
			bestInstanceFade = pEffectAnimInfo->m_animBlend;
		}
	}

	// no effects need to be culled
	if (bestInstanceFade <= 0.0f)
		return;

	// now remove any motion matching state effects that aren't from our best instance
	for (int i = 0; i < pTriggeredEffects->GetNumEffects(); i++)
	{
		const EffectAnimInfo* pEffectAnimInfo = pTriggeredEffects->Get(i);

		if (!pEffectAnimInfo->m_isMotionMatchingState)
			continue;

		if (pEffectAnimInfo->m_instId != bestInstanceId)
		{
			pTriggeredEffects->Remove(i);
			i--;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::BeginStep(F32 deltaTime, EffectList* pTriggeredEffects, const FgAnimData* pAnimData)
{
	PROFILE(Animation, AnimStateLayer_BeginStep);

	//	MsgOut("## Anim ## - AnimStateLayer::BeginStep\n");

	if (m_changeRequestsPending > 0)
	{
		MsgAnimVerbose("Animation(" PFS_PTR "): Change requests pending %d\n", this, int(m_changeRequestsPending));
		ANIM_ASSERT(m_changeRequestsPendingList[0].m_type == StateChangeRequest::kTypeFlagDirectFade
					|| m_changeRequestsPendingList[0].m_type == StateChangeRequest::kTypeFlagTransition);
	}

	m_transitionsTakenLastUpdate = 0;
	m_statesCompletedLastUpdate = 0;

	MsgAnimVerbose("Animation(" PFS_PTR "): AnimStateLayer::BeginStep - deltaTime: %5.2f\n", this, deltaTime);

	// Delete instances that have faded out
	DeleteNonContributingInstances();

	ANIM_ASSERT(m_changeRequestsPending == 0
				|| m_changeRequestsPendingList[0].m_type == StateChangeRequest::kTypeFlagDirectFade
				|| m_changeRequestsPendingList[0].m_type == StateChangeRequest::kTypeFlagTransition);

	// Make sure that we clear out all previous align movement if we haven't reached the end of the animation
	ResetAnimStateChannelDeltas();

	ANIM_ASSERT(m_changeRequestsPending == 0
				|| m_changeRequestsPendingList[0].m_type == StateChangeRequest::kTypeFlagDirectFade
				|| m_changeRequestsPendingList[0].m_type == StateChangeRequest::kTypeFlagTransition);

	// there will be a sync-problem between controllers expecting a transition
	// to be taken and it never was.
	if (AreTransitionsEnabled())
	{
		AnimStateInstance* pNewInstance;
		const bool tookNormalTransition = TakeTransitions(&pNewInstance);

		TakeAutoTransitions(tookNormalTransition, &pNewInstance);
	}

	if (!m_disableTopUpdateOverlayAndInfo)
	{
		CopyActorInfoToCurrentState();
	}

	// Update the fade and any side-effects that might have been caused by the fade flags.
	// This will enable/disable the phase update of states.
	UpdateInstanceFadeEffects(deltaTime);

	// Update the phase of the states. Clamp at 1.0 and save carry-over phase if needed
	UpdateInstancePhases(deltaTime, pTriggeredEffects);

	// If the phase advanced past the end of the current state, go on to the next state. We simulate a seam-less
	// transition here by advancing the transform twice, once for the old state, once for the new.
	WalkInstancesNewToOld(WalkStepCompletedInstances, (uintptr_t)pTriggeredEffects);

	RemoveEffectsWithLowestBlend(pTriggeredEffects);

	// Delete instances that have faded out
	DeleteNonContributingInstances();

	// the top track always refers to the global snapshot pointer, so any new instances taken on the top track should have latest overlays
	if (!m_disableTopUpdateOverlayAndInfo)
	{
		CopyOverlaySnapshotToCurrentTrack();
	}

	// state transitions may mutate variation indices, so keep the live version in sync with what's in the top track
	CopyOverlayVariantsBackToGlobalSnapshot();	// Take requested transitions before advancing the phase. Otherwise

												// Fade in/out the layer
	AnimLayer::BeginStep(deltaTime, pTriggeredEffects, pAnimData);

	MsgAnimVerbose("Animation(" PFS_PTR
				   "): AnimStateLayer::BeginStep - Done - Transitions Taken: %u, States Completed: %u\n",
				   this,
				   m_transitionsTakenLastUpdate,
				   m_statesCompletedLastUpdate);

	// If the layer is fully faded out and wants to be faded out we remove all states.
	if (GetCurrentFade() == 0.0f && GetCurrentFade() == GetDesiredFade())
	{
		OnFree();

		m_usedInstances.ClearAllBits();
		m_usedTracks.ClearAllBits();
		m_numTracks = 0;
	}

	if (m_statesCompletedLastUpdate > 0)
	{
		MsgAnimVerbose("Animation(" PFS_PTR "): States completed %d\n", this, int(m_statesCompletedLastUpdate));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::UpdateInstanceFadeEffects(F32 deltaTime)
{
	bool freezeNextInstancePhase = false;

	for (U32F iTrack = 0; iTrack < m_numTracks; ++iTrack)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[iTrack];

		freezeNextInstancePhase = pTrack->UpdateInstanceFadeEffects(deltaTime, freezeNextInstancePhase);
	}

	float remFade = GetCurrentFade();

	for (U32F iTrack = 0; iTrack < m_numTracks; ++iTrack)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[iTrack];

		const float trackFade = pTrack->AnimFade();
		const float effectiveFade = remFade * trackFade;

		pTrack->UpdateEffectiveFade(effectiveFade);

		remFade *= Limit01(1.0f - trackFade);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::UpdateInstancePhases(F32 deltaTime, EffectList* pTriggeredEffects)
{
	PROFILE(Animation, UpdateInstancePhases);

	bool topTrack = true;

	for (U32F iTrack = 0; iTrack < m_numTracks; ++iTrack)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[iTrack];

		pTrack->UpdateInstancePhases(deltaTime, topTrack, pTriggeredEffects);

		topTrack = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateLayer::CanLoopInstance(const AnimStateInstance* pInstance,
									 const DC::AnimState* pDestState,
									 const FadeToStateParams& params) const
{
	if (!pInstance || !params.m_allowStateLooping)
		return false;

	const bool isStateLooping = (pDestState->m_name.m_symbol == pInstance->m_stateSnapshot.m_animState.m_name.m_symbol
								 && !(pDestState->m_flags & DC::kAnimStateFlagNeverLoop));

	if (!isStateLooping)
		return false;

	// KANXU: fix anim state can't loop if play backwards. we don't have a good way to know if it's a loop end transition. 
	// so testing the current instance phase is 0 and next start phase is 1. it won't work if we want to start the loop in the middle of anim.
	const bool hasPrevStateEnded = (pInstance->Phase() == 1.0f) ||
		(pInstance->Phase() == 0.0f && pInstance->Phase() < pInstance->PrevPhase() && params.m_stateStartPhase > 0.99f);

	if (!hasPrevStateEnded)
		return false;

	const bool isInstantFade = (params.m_animFadeTime == 0.0f) && (params.m_motionFadeTime == 0.0f);

	if (!isInstantFade)
		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::WalkInstancesOldToNew(PFnVisitAnimStateInstance pfnCallback, uintptr_t userData)
{
	if (!pfnCallback)
		return;

	for (U32F i = 0; i < m_numTracks; ++i)
	{
		const U32F index = m_numTracks - i - 1;
		ANIM_ASSERT(index < m_numAllocatedTracks);

		AnimStateInstanceTrack* pTrack = m_ppTrackList[index];

		if (!pTrack->WalkInstancesOldToNew(pfnCallback, this, userData))
			break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::WalkInstancesOldToNew(PFnVisitAnimStateInstanceConst pfnCallback, uintptr_t userData) const
{
	if (!pfnCallback)
		return;

	for (U32F i = 0; i < m_numTracks; ++i)
	{
		const U32F index = m_numTracks - i - 1;
		ANIM_ASSERT(index < m_numAllocatedTracks);

		AnimStateInstanceTrack* pTrack = m_ppTrackList[index];

		if (!pTrack->WalkInstancesOldToNew(pfnCallback, this, userData))
			break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::WalkInstancesNewToOld(PFnVisitAnimStateInstance pfnCallback, uintptr_t userData)
{
	if (!pfnCallback)
		return;

	for (U32F i = 0; i < m_numTracks; ++i)
	{
		AnimStateInstanceTrack* pTrack = m_ppTrackList[i];

		if (!pTrack->WalkInstancesNewToOld(pfnCallback, this, userData))
			break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer::WalkInstancesNewToOld(PFnVisitAnimStateInstanceConst pfnCallback, uintptr_t userData) const
{
	if (!pfnCallback)
		return;

	for (U32F i = 0; i < m_numTracks; ++i)
	{
		const AnimStateInstanceTrack* pTrack = m_ppTrackList[i];

		if (!pTrack->WalkInstancesNewToOld(pfnCallback, this, userData))
			break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimStateLayer::IsInstanceTopOfTrack(const AnimStateInstance* pInstance) const
{
	if (!pInstance)
		return false;

	for (U32F i = 0; i < m_numTracks; ++i)
	{
		const AnimStateInstanceTrack* pTrack = m_ppTrackList[i];

		if (pTrack->GetInstance(0) == pInstance)
			return true;
	}

	return false;
}
