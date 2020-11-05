/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/cinematic-controller.h"

#include "corelib/util/angle.h"

#include "ndlib/anim/anim-action.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-layer.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-util.h"
#include "ndlib/anim/armik.h"
#include "ndlib/anim/ik/two-bone-ik.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/draw-control.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/gameplay/ai/agent/nav-character-adapter.inl"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/ap-entry-util.h"
#include "gamelib/gameplay/ap-exit-util.h"
#include "gamelib/gameplay/nav/action-pack-mgr.h"
#include "gamelib/gameplay/nd-effect-control.h"
#include "gamelib/level/artitem.h"
#include "gamelib/render/particle/particle.h"
#include "gamelib/state-script/ss-manager.h"

#include "ailib/util/ai-action-pack-util.h"

#include "game/player/commonactions.h"

#include "game/ai/action-pack/cinematic-action-pack.h"
#include "game/ai/agent/npc-action-pack-util.h"
#include "game/ai/agent/npc.h"
#include "game/ai/base/ai-game-debug.h"
#include "game/ai/base/ai-game-util.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/coordinator/encounter-coordinator.h"
#include "game/ai/knowledge/entity.h"
#include "game/audio/dialog-manager.h"
#include "game/interactable/openable-object.h"
#include "game/scriptx/h/ai-action-packs-defines.h"
#include "game/scriptx/h/ai-defines.h"
#include "game/scriptx/h/anim-npc-info.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AiCinematicController : public IAiCinematicController
{
	typedef IAiCinematicController ParentClass;

	static CONST_EXPR size_t kMaxAnimEntries = 50;

public:
	AiCinematicController();

	class BaseState : public Fsm::TState<AiCinematicController>
	{
	public:
		FSM_BIND_TO_SELF(AiCinematicController);
		virtual void RequestAnimations() {}
		virtual void UpdateStatus() {}
		virtual float GetNavMeshAdjustBlendFactor() const { return 0.0f; }
	};

	typedef Fsm::TStateMachine<BaseState> StateMachine;

protected:
	FSM_BASE_DECLARE(StateMachine);
public:

	// Common
	FSM_STATE_DECLARE(None);
	FSM_STATE_DECLARE(Idle);
	FSM_STATE_DECLARE(WaitingToPlayInPlace);
	FSM_STATE_DECLARE(Entering);
	FSM_STATE_DECLARE(Looping);
	FSM_STATE_DECLARE(WaitingForMultiCapSynchronousPlay);
	FSM_STATE_DECLARE(ReadyToExit);
	FSM_STATE_DECLARE(Exiting);

	// For Fsm
	virtual U32F GetMaxStateAllocSize();
	void GoInitialState();
	const Clock* GetClock() const;

	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override;
	virtual void Init(NdGameObject* pCharacter, const SimpleNavControl* pNavControl) override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual void Reset() override;
	virtual void SetCapProcess(MutableProcessCinematicActionPackHandle hCapProcess) override { m_hCapProcess = hCapProcess; }

	virtual bool ResolveEntry(const ActionPackResolveInput& input,
							  const ActionPack* pActionPack,
							  ActionPackEntryDef* pDefOut) const override;
	virtual bool ResolveDefaultEntry(const ActionPackResolveInput& input,
									 const ActionPack* pActionPack,
									 ActionPackEntryDef* pDefOut) const override;
	virtual bool UpdateEntry(const ActionPackResolveInput& input,
							 const ActionPack* pActionPack,
							 ActionPackEntryDef* pDefOut) const override;

	virtual U64 CollectHitReactionStateFlags() const override;

	void RequestDialog(StringId64 dialogId, NdGameObject* pSpeaker, StringId64 capId);

	virtual void MakeEntryCharacterState(const ActionPackResolveInput& input,
										 const ActionPack* pActionPack,
										 ApEntry::CharacterState* pCsOut) const override;

	virtual void MakeExitCharacterState(const ActionPack* pActionPack,
										const IPathWaypoints* pPathPs,
										ApExit::CharacterState* pCsOut) const override;

	virtual bool TeleportInto(ActionPack* pActionPack,
							  bool playEntireEntryAnim,
							  float fadeTime,
							  BoundFrame* pNewFrameOut,
							  uintptr_t apUserData = 0) override;

	virtual void Enter(const ActionPackResolveInput& input,
					   ActionPack* pActionPack,
					   const ActionPackEntryDef& entryDef) override;
	virtual void PlayPartial(const CinematicActionPack* pActionPack,
							 const DC::CineActionPackPerformancePartial* pPerformancePartial) override;
	virtual bool IsPlaying() const override;
	virtual bool NoLegIk() const override;
	virtual bool IsAbortRequested() const override;
	virtual bool NoAdjustToGround() const override;
	virtual void Exit(const PathWaypointsEx* pExitPathPs) override;
	virtual void SetAutomaticMode(bool enableAutomaticMode) override;
	virtual void StartWait(StringId64 waitAnimId, StringId64 waitPropAnimId = INVALID_STRING_ID_64) override;
	virtual bool StartUsing(bool first) override;
	virtual bool IsLoopComplete() const override;
	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;
	virtual void UpdateProcedural() override;
	virtual bool IsBusy() const override;
	virtual bool RequestAbortAction() override;
	virtual void ForceAbortAction() override;
	virtual void DebugDrawEntries(const ActionPackResolveInput& input, const ActionPack* pActionPack) const override;
	virtual void DebugDrawExits(const ActionPackResolveInput& input,
								const ActionPack* pActionPack,
								const IPathWaypoints* pPathPs) const override;

	// From IAiCinematicController
	virtual I32F GetCapEnterAnimCount(const CinematicActionPack* pCineAp) const override;
	virtual I32F GetCapExitAnimCount(const CinematicActionPack* pCineAp) const override;

	virtual bool IsEnterComplete() const override
	{
		if (IsState(kStateNone))
		{
			return false;
		}

		if (IsState(kStateEntering))
		{
			if (m_ownerAnimAction.IsDone())
			{
				return true;
			}

			return false;
		}

		return true;
	}

	virtual bool ShouldAutoExitAp(const ActionPack* pAp) const override
	{
		return false; // TODO: Revisit after the demo?
		//return IsState(kStateExiting);
	}

	void StartLoop(const DC::CapAnimLoop* pLoopSet, bool first);

	I32F GetCapEnterAnims(const CinematicActionPack* pCineAp, DC::ApEntryItem entryItems[], I32F entryItemsSize) const;
	I32F GetCapExitAnims(const CinematicActionPack* pCineAp, DC::ApExitAnimDef exitItems[], I32F exitItemsSize) const;

	CinematicActionPack* GetEnteredActionPack() const;

	StringId64 GetLoopStateId() const;
	StringId64 GetExitStateId() const;
	StringId64 GetInterruptStateId() const;
	StringId64 GetWaitStateId() const;

	const DC::ApExitAnimList* GetApExitListFor(const CinematicActionPack* pCineAp, const Process* pCharacter) const;

	virtual bool IsInValidStateToStartLoop() const override;

	virtual void TryDropProp(CinematicActionPack* pCineAp) override;

	virtual float GetNavMeshAdjustBlendFactor() const override;

	BoundFrame m_capBoundFrame;

	AnimActionWithSelfBlend	m_ownerAnimAction;
	AnimActionWithSelfBlend	m_propAnimAction;
	ActionPackEntryDef		m_entryDef;
	NavAnimHandoffDesc		m_exitHandoff;
	Demeanor				m_startingDem;

	MutableNdGameObjectHandle	m_hProp;
	MutableProcessCinematicActionPackHandle m_hCapProcess;

	ActionPackHandle		m_hIkCap;

	GunState				m_startingGunState;
	float					m_exitPhase;
	float					m_propExitPhase;

	bool					m_noLegIk : 1;
	bool					m_noAdjustToGround : 1;
	bool					m_skipEnterAnimation : 1;
	bool					m_abortRequested : 1;
	bool					m_enteredAp : 1;
	bool					m_auto : 1;
	bool					m_mirror : 1;

	DC::CapAnimLoop			m_lastLoop;
	TimeFrame				m_usageTime;
};

/// --------------------------------------------------------------------------------------------------------------- ///
AiCinematicController::AiCinematicController()
	: m_startingGunState(kGunStateOut)
	, m_exitPhase(-1.0f)
	, m_propExitPhase(-1.0f)
	, m_noLegIk(false)
	, m_noAdjustToGround(false)
	, m_skipEnterAnimation(false)
	, m_abortRequested(false)
	, m_enteredAp(false)
	, m_auto(false)
	, m_mirror(false)
	, m_usageTime(TimeFrameNegInfinity())
	, m_capBoundFrame(kIdentity)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::GoInitialState()
{
	GoNone();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Clock* AiCinematicController::GetClock() const
{
	NdGameObject* pCharacter = GetCharacterGameObject();
	return pCharacter ? pCharacter->GetClock() : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::Init(NavCharacter* pNavChar, const NavControl* pNavControl)
{
	ParentClass::Init(pNavChar, pNavControl);

	GetStateMachine().Init(this, FILE_LINE_FUNC, true, sizeof(StringId64));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::Init(NdGameObject* pCharacter, const SimpleNavControl* pNavControl)
{
	ParentClass::Init(pCharacter, pNavControl);

	GetStateMachine().Init(this, FILE_LINE_FUNC, true, sizeof(StringId64));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);

	GetStateMachine().Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::Reset()
{
	if (CinematicActionPack* pCineAp = GetEnteredActionPack())
	{
		pCineAp->RemoveUser(GetCharacterGameObject());

		TryDropProp(pCineAp);
	}

	m_ownerAnimAction.Reset();
	m_propAnimAction.Reset();

	m_noLegIk = false;
	m_noAdjustToGround = false;
	m_skipEnterAnimation = false;
	m_startingGunState = kGunStateOut;
	m_exitPhase = -1.0f;
	m_propExitPhase = -1.0f;
	m_abortRequested = false;
	m_usageTime = TimeFrameNegInfinity();
	m_enteredAp = false;
	m_mirror = false;
	m_capBoundFrame = kIdentity;

	m_entryDef = ActionPackEntryDef();

	GoNone();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::RequestDialog(StringId64 dialogId, NdGameObject* pSpeaker, StringId64 capId)
{
	AUTO_FACT_DICTIONARY(facts);

	facts.Set(SID("speaker"), pSpeaker->GetUserId());
	facts.Set(SID("action-pack"), capId);

	EngineComponents::GetDialogManager()->RequestDialog(dialogId, facts);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::MakeEntryCharacterState(const ActionPackResolveInput& input,
													const ActionPack* pActionPack,
													ApEntry::CharacterState* pCsOut) const
{
	const NavCharacterAdapter pNavChar = GetCharacterAdapter();
	const NdGameObject* pCharacter = GetCharacterGameObject();

	if (!pCharacter || !pCsOut)
		return;

	ParentClass::MakeEntryCharacterState(input, pActionPack, pCsOut);

	const NdAnimControllerConfig* pConfig = pNavChar->GetAnimControllerConfig();

	// more potential config variables here
	pCsOut->m_rangedEntryRadius		= 3.0f;
	pCsOut->m_maxEntryErrorAngleDeg	= 15.0f;
	pCsOut->m_maxFacingDiffAngleDeg	= 20.0f;

	const CinematicActionPack* pCineAp = CinematicActionPack::FromActionPack(pActionPack);
	const DC::CapAnimCollection* pAnimCollection = pCineAp ? pCineAp->GetCapAnimCollection(pCharacter) : nullptr;
	const DC::CineActionPackPerformanceFull* pPerformanceFull = pCineAp ? pCineAp->GetPerformanceFull(pCharacter) : nullptr;
	const DC::CineActionPackDef* pCapDef = pCineAp ? pCineAp->GetDef() : nullptr;

	pCsOut->m_mirrorBase = pCapDef && pCapDef->m_mirror;

	const bool inPlace = pPerformanceFull && (pPerformanceFull->m_location == DC::kCineActionPackLocationInPlace);

	if (!inPlace && pAnimCollection && pAnimCollection->m_useAutoRotationEntries && (pNavChar->GetEnteredActionPack() != pActionPack))
	{
		if (const DC::CapAnimLoop* pLoop = pCineAp->GetLoop(pCharacter))
		{
			const BoundFrame& apRef = inPlace ? pCharacter->GetBoundFrame() : pCineAp->GetBoundFrame();

			const AnimControl* pAnimControl = pCharacter->GetAnimControl();
			Locator desStartAlignPs = kIdentity;

			if (FindAlignFromApReference(pAnimControl,
										 pLoop->m_animName,
										 0.0,
										 apRef.GetLocatorPs(),
										 &desStartAlignPs,
										 pCsOut->m_mirrorBase))
			{
				pCsOut->m_matchApToAlign = true;
				pCsOut->m_apMatchAlignPs = desStartAlignPs;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::MakeExitCharacterState(const ActionPack* pActionPack,
												   const IPathWaypoints* pPathPs,
												   ApExit::CharacterState* pCsOut) const
{
	ParentClass::MakeExitCharacterState(pActionPack, pPathPs, pCsOut);

	const NdGameObject* pCharacter = GetCharacterGameObject();

	if (!pCharacter || !pCsOut)
		return;

	pCsOut->m_mirrorBase = m_mirror;

	const CinematicActionPack* pCineAp = CinematicActionPack::FromActionPack(pActionPack);
	const DC::CapAnimCollection* pAnimCollection = pCineAp ? pCineAp->GetCapAnimCollection(pCharacter) : nullptr;
	if (pAnimCollection && pAnimCollection->m_useAutoRotationExits)
	{
		if (const DC::CapAnimLoop* pLoop = pCineAp->GetLoop(pCharacter))
		{
			const AnimControl* pAnimControl = pCharacter->GetAnimControl();

			const BoundFrame capApRef = (m_entryDef.m_hResolvedForAp.IsValid() && m_entryDef.m_useDesiredAlign)
											? m_entryDef.m_sbApReference
											: (pCineAp ? m_capBoundFrame : pCharacter->GetBoundFrame());

			Locator matchAlignPs;
			if (FindAlignFromApReference(pAnimControl,
										 pLoop->m_animName,
										 1.0,
										 capApRef.GetLocatorPs(),
										 &matchAlignPs,
										 pCsOut->m_mirrorBase))
			{
				pCsOut->m_apMatchAlignPs = matchAlignPs;
				pCsOut->m_matchApToAlign = true;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiCinematicController::ResolveEntry(const ActionPackResolveInput& input,
										 const ActionPack* pActionPack,
										 ActionPackEntryDef* pDefOut) const
{
	ASSERT(pDefOut);
	ASSERT(pActionPack->GetType() == ActionPack::kCinematicActionPack);

	if (!pDefOut)
		return false;

	if (pActionPack->GetType() != ActionPack::kCinematicActionPack)
		return false;

	const NdGameObject* pCharacter = GetCharacterGameObject();
	if (!pCharacter)
		return false;

	if (!pActionPack->IsAvailableFor(pCharacter))
		return false;

	AnimControl* pAnimControl = pCharacter->GetAnimControl();
	const CinematicActionPack* pCineAp = static_cast<const CinematicActionPack*>(pActionPack);

	const DC::CineActionPackPerformanceFull* pPerformanceFull = pCineAp->GetPerformanceFull(pCharacter);
	ASSERT(pPerformanceFull);
	if (!pPerformanceFull)
		return false;

	if (const EntitySpawner* pCapSpawner = pCineAp->GetSpawner())
	{
		if (pCapSpawner->GetData<bool>(SID("default-entries-only"), false))
		{
			return false;
		}
	}

	switch (pPerformanceFull->m_location)
	{
	case DC::kCineActionPackLocationInPlace:
	case DC::kCineActionPackLocationApRef:
	case DC::kCineActionPackLocationApRefLoose:
		{
			const DC::ApEntryItemList* pEntryList = GetApEntryListFor(pCineAp, pCharacter);

			const bool inPlace = pPerformanceFull->m_location == DC::kCineActionPackLocationInPlace;

			BoundFrame old = inPlace ? pCharacter->GetBoundFrame() : pCineAp->GetBoundFrame();
			BoundFrame apRef = inPlace ? pCharacter->GetBoundFrame() : pCineAp->GetBoundFrame();

			//g_prim.Draw(DebugCoordAxesLabeled(apRef.GetLocatorWs()	, "raw"), kPrimDuration1FramePauseable);

			ApEntry::CharacterState cs;
			MakeEntryCharacterState(input, pActionPack, &cs);

			ApEntry::AvailableEntry availableEntries[32];
			const U32F numEntries = ApEntry::ResolveEntry(cs,
														  pEntryList,
														  apRef,
														  availableEntries,
														  ARRAY_COUNT(availableEntries));
			if (numEntries == 0)
				return false;

			const U32F selectedIndex = ApEntry::EntryWithLeastRotation(availableEntries, numEntries, apRef);
			const ApEntry::AvailableEntry& selectedEntry = availableEntries[selectedIndex];

			const bool forceStop = (selectedEntry.m_pDcDef->m_flags & DC::kApEntryItemFlagsForceStop);

			NavLocation entryNavLoc;
			if (!ApEntry::ConstructEntryNavLocation(cs, pActionPack->GetRegisteredNavLocation(), selectedEntry, &entryNavLoc))
			{
				return false;
			}

			pDefOut->m_entryNavLoc = entryNavLoc;
			pDefOut->m_entryRotPs = selectedEntry.m_entryAlignPs.Rot();

			pDefOut->m_entryAnimId	 = selectedEntry.m_resolvedAnimId;
			pDefOut->m_apReference	 = selectedEntry.m_rotatedApRef;
			pDefOut->m_sbApReference = selectedEntry.m_sbApRef;
			pDefOut->m_phase		 = selectedEntry.m_phase;
			pDefOut->m_preferredMotionType = kMotionTypeWalk; // ?
			pDefOut->m_stopBeforeEntry	   = (selectedEntry.m_pDcDef->m_flags & DC::kApEntryItemFlagsForceStop);
			pDefOut->m_pDcDef = selectedEntry.m_pDcDef;
			pDefOut->m_mirror = selectedEntry.m_playMirrored;
			pDefOut->m_hResolvedForAp = pActionPack;

			if (cs.m_matchApToAlign)
			{
				pDefOut->m_useDesiredAlign = true;
			}
		}
		break;

	default:
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiCinematicController::ResolveDefaultEntry(const ActionPackResolveInput& input,
												const ActionPack* pActionPack,
												ActionPackEntryDef* pDefOut) const
{
	const NdGameObject* pCharacter = GetCharacterGameObject();

	ASSERT(pDefOut);
	ASSERT(pActionPack->GetType() == ActionPack::kCinematicActionPack);

	if (!pActionPack || !pActionPack->IsAvailableFor(pCharacter))
		return false;

	if (!pDefOut)
		return false;

	if (pActionPack->GetType() != ActionPack::kCinematicActionPack)
		return false;

	AnimControl* pAnimControl = pCharacter->GetAnimControl();
	const CinematicActionPack* pCineAp = static_cast<const CinematicActionPack*>(pActionPack);

	const DC::CineActionPackPerformanceFull* pPerformanceFull = pCineAp->GetPerformanceFull(pCharacter);
	ASSERT(pPerformanceFull);
	if (!pPerformanceFull)
		return false;


	switch (pPerformanceFull->m_location)
	{
	case DC::kCineActionPackLocationInPlace:
	case DC::kCineActionPackLocationApRef:
	case DC::kCineActionPackLocationApRefLoose:
		{
			const DC::ApEntryItemList* pEntryList = GetApEntryListFor(pCineAp, pCharacter);
			const bool inPlace = pPerformanceFull->m_location == DC::kCineActionPackLocationInPlace;
			BoundFrame apRef = inPlace ? pCharacter->GetBoundFrame() : pCineAp->GetBoundFrame();

			ApEntry::CharacterState cs;
			MakeEntryCharacterState(input, pActionPack, &cs);

			ApEntry::AvailableEntry availableEntries[8];
			const U32F numEntries = ApEntry::ResolveDefaultEntry(cs, pEntryList, apRef, availableEntries, 8);
			if (numEntries == 0)
				return false;

			const U32F selectedIndex = ApEntry::EntryWithLeastRotation(availableEntries, numEntries, apRef);
			const ApEntry::AvailableEntry& selectedEntry = availableEntries[selectedIndex];

			const NavLocation apNavLoc = pActionPack->GetRegisteredNavLocation();

			NavLocation entryNavLoc;
			if (!ApEntry::ConstructEntryNavLocation(cs, apNavLoc, selectedEntry, &entryNavLoc))
			{
				return false;
			}

			const bool forceStop = (selectedEntry.m_pDcDef->m_flags & DC::kApEntryItemFlagsForceStop);

			pDefOut->m_entryNavLoc	 = entryNavLoc;
			pDefOut->m_entryAnimId	 = selectedEntry.m_resolvedAnimId;
			pDefOut->m_entryRotPs	 = selectedEntry.m_entryAlignPs.Rot();
			pDefOut->m_apReference	 = selectedEntry.m_rotatedApRef;
			pDefOut->m_sbApReference = selectedEntry.m_rotatedApRef;
			pDefOut->m_phase		 = selectedEntry.m_phase;
			pDefOut->m_stopBeforeEntry = forceStop;
			pDefOut->m_pDcDef		  = selectedEntry.m_pDcDef;
			pDefOut->m_mirror		  = selectedEntry.m_playMirrored;
			pDefOut->m_hResolvedForAp = pActionPack;

			if (cs.m_matchApToAlign)
			{
				pDefOut->m_useDesiredAlign = true;
			}
		}
		break;

	default:
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiCinematicController::UpdateEntry(const ActionPackResolveInput& input,
										const ActionPack* pActionPack,
										ActionPackEntryDef* pDefOut) const
{
	const NdGameObject* pCharacter = GetCharacterGameObject();
	const Locator& parentSpace = pCharacter->GetParentSpace();

	AI_ASSERT(pDefOut);
	AI_ASSERT(pActionPack->GetType() == ActionPack::kCinematicActionPack);

	const CinematicActionPack* pCineAp = static_cast<const CinematicActionPack*>(pActionPack);
	const DC::ApEntryItem* pEntry = pDefOut->m_pDcDef;

	pDefOut->m_hResolvedForAp = nullptr;

	if (!pEntry)
		return false;

	BoundFrame defaultApRef = pCineAp->GetBoundFrame();
	ApEntry::CharacterState cs;
	MakeEntryCharacterState(input, pActionPack, &cs);

	ApEntry::AvailableEntry updatedEntry;

	if (!ApEntry::ConstructEntry(cs, pEntry, defaultApRef, &updatedEntry))
	{
		const NavCharacter* pNavChar = GetCharacter();
		if (pNavChar)
			AiLogAnim(pNavChar, "IAiCinematicController::UpdateEntry() - no longer valid, ConstructEntry() failed\n");
		return false;
	}

	const bool isDefaultEntry = (pEntry->m_flags & DC::kApEntryItemFlagsDefaultEntry) != 0;
	const ApEntry::GatherMode mode = isDefaultEntry ? ApEntry::GatherMode::kDefault : ApEntry::GatherMode::kResolved;

	ApEntry::FlagInvalidEntries(cs, mode, defaultApRef, &updatedEntry, 1);

	if (updatedEntry.m_errorFlags.m_all)
	{
		const NavCharacter* pNavChar = GetCharacter();
		if (pNavChar)
			AiLogAnim(pNavChar,
					  "IAiCinematicController::UpdateEntry() - no longer valid (flags = %d)\n",
					  updatedEntry.m_errorFlags.m_all);
		return false;
	}

	if (!ApEntry::ConstructEntryNavLocation(cs,
											pActionPack->GetRegisteredNavLocation(),
											updatedEntry,
											&pDefOut->m_entryNavLoc))
	{
		return false;
	}

	pDefOut->m_entryRotPs  = updatedEntry.m_entryAlignPs.Rot();
	pDefOut->m_apReference = updatedEntry.m_rotatedApRef;
	pDefOut->m_phase	   = updatedEntry.m_phase;
	pDefOut->m_hResolvedForAp = pActionPack;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U64 AiCinematicController::CollectHitReactionStateFlags() const
{
	const NdGameObject* pCharacter = GetCharacterGameObject();
	const CinematicActionPack* pCineAp = GetEnteredActionPack();
	const DC::CineActionPackPerformance* pPerformance = pCineAp ? pCineAp->GetPerformance(pCharacter) : nullptr;

	U64 flags = 0;

	if (pPerformance)
	{
		flags = pPerformance->m_hitReactionState;
	}

	return flags;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiCinematicController::TeleportInto(ActionPack* pActionPack,
										 bool playEntireEntryAnim,
										 float fadeTime,
										 BoundFrame* pNewFrameOut,
										 uintptr_t apUserData /* = 0 */)
{
	if (pActionPack->GetType() != ActionPack::kCinematicActionPack)
		return false;

	const NavCharacterAdapter pNavChar = GetCharacterAdapter();
	const NdGameObject* pCharacter	   = GetCharacterGameObject();

	bool valid = false;

	m_skipEnterAnimation = !playEntireEntryAnim;
	ActionPackResolveInput defRes = MakeDefaultResolveInput(pNavChar);
	ActionPackEntryDef actionPackEntry;

	actionPackEntry.m_forceBlendTime = fadeTime;

	if (ResolveEntry(defRes, pActionPack, &actionPackEntry))
	{
		Enter(defRes, pActionPack, actionPackEntry);
		valid = true;
	}
	else if (ResolveDefaultEntry(defRes, pActionPack, &actionPackEntry))
	{
		Enter(defRes, pActionPack, actionPackEntry);
		valid = true;
	}

	if (pNewFrameOut)
	{
		BoundFrame newFrame = actionPackEntry.m_apReference;
		newFrame.SetLocatorPs(Locator(actionPackEntry.m_entryNavLoc.GetPosPs(), actionPackEntry.m_entryRotPs));
		*pNewFrameOut = newFrame;
	}

	if (valid)
	{
		ParentClass::TeleportInto(pActionPack, playEntireEntryAnim, fadeTime, pNewFrameOut, apUserData);
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static NdGameObject* GetPropForCap(NavCharacter* pNavChar, CinematicActionPack* pCap)
{
	if (!pCap)
	{
		return nullptr;
	}

	if (NdGameObject* pProp = pCap->GetPropHandle().ToMutableProcess())
	{
		return pProp;
	}

	if (!pNavChar)
	{
		return nullptr;
	}

	StringId64 propId = pCap->GetPropId();

	if (!propId)
	{
		if (const DC::CineActionPackDef* pDcDef = pCap->GetDef())
		{
			propId = pDcDef->m_propId;
		}
	}

	return pNavChar->LookupPropByName(propId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::Enter(const ActionPackResolveInput& input,
								  ActionPack* pActionPack,
								  const ActionPackEntryDef& entryDef)
{
	ParentClass::Enter(input, pActionPack, entryDef);

	m_ownerAnimAction.Reset();
	m_propAnimAction.Reset();

	m_exitPhase = -1.0f;
	m_propExitPhase = -1.0f;
	m_abortRequested = false;

	m_hProp = nullptr;

	NavCharacterAdapter pNavChar = GetCharacterAdapter();
	NdGameObject* pCharacter = GetCharacterGameObject();

	AI_ASSERT(pActionPack->GetType() == ActionPack::kCinematicActionPack);
	if (pActionPack->GetType() != ActionPack::kCinematicActionPack)
		return;

	CinematicActionPack* pCineAp = const_cast<CinematicActionPack*>(static_cast<const CinematicActionPack*>(pActionPack));
	const DC::CineActionPackPerformanceFull* pPerformanceFull = pCineAp->GetPerformanceFull(GetCharacterGameObject());

	ASSERT(pPerformanceFull);
	if (!pPerformanceFull)
		return;

	m_entryDef = entryDef;

	AnimControl* pAnimControl = pCharacter->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetStateLayerById(SID("base"));

	const NdAnimControllerConfig* pConfig = pNavChar->GetAnimControllerConfig();
	const DC::CapAnimCollection* pCapAnimCollection = pCineAp->GetCapAnimCollection(pCharacter);
	AI_ASSERT(pCapAnimCollection);

	if (!pCapAnimCollection)
		return;

	m_mirror = pCineAp->GetDef()->m_mirror;

	bool isApPerf = false;

	switch (pPerformanceFull->m_location)
	{
	case DC::kCineActionPackLocationApRef:
	case DC::kCineActionPackLocationApRefLoose:
		isApPerf = true;
		break;
	}

	const DC::ApEntryItem* pEntryItem = m_entryDef.m_pDcDef;
	if (pEntryItem)
	{
		AI::SetPluggableAnim(pCharacter, pEntryItem->m_animName, m_entryDef.m_mirror);
	}
	else if (isApPerf)
	{
		// failed to resolve but still called Enter?
		m_auto = true;
	}

	if (FALSE_IN_FINAL_BUILD(!pEntryItem))
	{
		const DC::ApEntryItem* pDefaultItem = nullptr;
		const DC::ApEntryItemList* pEntryList = GetApEntryListFor(pCineAp, pCharacter);

		for (int i = 0; i < (pEntryList ? pEntryList->m_count : 0); ++i)
		{
			if (pEntryList->m_array[i].m_flags & DC::kApEntryItemFlagsDefaultEntry)
			{
				pDefaultItem = &pEntryList->m_array[i];
				break;
			}
		}

		if (pDefaultItem)
		{
			g_prim.Draw(DebugString(pCharacter->GetTranslation() + kUnitYAxis,
									StringBuilder<256>("Failed to resolve CAP entry! Is the actor containing anim '%s' loaded?\nCAP: %s\nNpc: %s (%s)",
													   DevKitOnly_StringIdToString(pDefaultItem->m_animName),
													   DevKitOnly_StringIdToString(pCineAp->GetDefId()),
													   pCharacter->GetName(),
													   DevKitOnly_StringIdToString(pNavChar->GetCapCharacterId())).c_str(), kColorRed), Seconds(15.0f));
		}
		else
		{
			g_prim.Draw(DebugString(pCharacter->GetTranslation() + kUnitYAxis,
									StringBuilder<256>("Failed to resolve CAP entry! Does it have a valid default?\nCAP: %s\nNpc: %s (%s)",
													   DevKitOnly_StringIdToString(pCineAp->GetDefId()),
													   pCharacter->GetName(),
													   DevKitOnly_StringIdToString(pNavChar->GetCapCharacterId())).c_str(), kColorRed), Seconds(15.0f));
		}
		AI::SetPluggableAnim(pCharacter, SID("idle-zen"));
	}
	else if (!pEntryItem)
	{
		AI::SetPluggableAnim(pCharacter, INVALID_STRING_ID_64);
	}

	// Enter the action pack.
	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);
	m_ownerAnimAction.ClearSelfBlendParams();
	m_propAnimAction.ClearSelfBlendParams();

	float startPhase = 0.0f;

	FadeToStateParams params;
	params.m_animFadeTime = 0.4f;
	params.m_blendType = DC::kAnimCurveTypeUniformS;

	params.ApplyBlendParams(pCapAnimCollection->m_defaultEntryBlendIn);
	params.ApplyBlendParams(pEntryItem ? pEntryItem->m_blendIn : nullptr);

	m_capBoundFrame = pActionPack->GetBoundFrame();

	if ((pPerformanceFull->m_location == DC::kCineActionPackLocationApRefLoose) && pEntryItem)
	{
		const SkeletonId skelId = pCharacter->GetSkeletonId();
		BoundFrame entryLoc = pCharacter->GetBoundFrame();

		const AnimStateInstance* pCurInstance = pBaseLayer->CurrentStateInstance();
		const ArtItemAnim* pPhaseAnim = pCurInstance ? pCurInstance->GetPhaseAnimArtItem().ToArtItem() : nullptr;
		if (pPhaseAnim && !pPhaseAnim->IsLooping())
		{
			Locator align0, align1;
			const float duration = GetDuration(pPhaseAnim);
			const float curPhase = pCurInstance->Phase();
			const float lookAheadTime = Max(params.m_motionFadeTime, 0.5f);
			const float lookAheadPhase = (duration > NDI_FLT_EPSILON) ? Limit01((lookAheadTime / duration) + curPhase) : curPhase;

			EvaluateChannelInAnim(skelId, pPhaseAnim, SID("align"), curPhase, &align0, pCurInstance->IsFlipped());
			EvaluateChannelInAnim(skelId, pPhaseAnim, SID("align"), lookAheadPhase, &align1, pCurInstance->IsFlipped());

			const Locator alignDelta = align0.UntransformLocator(align1);

			entryLoc.AdjustLocatorLs(alignDelta);
		}

		entryLoc.SetRotationPs(m_entryDef.m_entryRotPs);

		//g_prim.Draw(DebugCoordAxes(entryLoc.GetLocatorWs(), 1.0f, kPrimEnableHiddenLineAlpha), Seconds(0.5f));

		Locator looseApPs;
		if (FindApReferenceFromAlign(pAnimControl,
									 pEntryItem->m_animName,
									 entryLoc.GetLocatorPs(),
									 &looseApPs,
									 entryDef.m_phase,
									 entryDef.m_mirror))
		{
			m_capBoundFrame.SetLocatorPs(looseApPs);
			m_entryDef.m_sbApReference.SetLocatorPs(looseApPs);
		}
	}

	if (pEntryItem && isApPerf)
	{
		BoundFrame unrotatedApRef = (m_entryDef.m_hResolvedForAp.IsValid() && m_entryDef.m_useDesiredAlign)
										? m_entryDef.m_sbApReference
										: m_capBoundFrame;
		BoundFrame rotatedApRef = m_entryDef.m_apReference;

		if (!pCapAnimCollection->m_noAdjustToUpright)
		{
			unrotatedApRef = pNavChar->AdjustBoundFrameToUpright(unrotatedApRef);
			rotatedApRef = pNavChar->AdjustBoundFrameToUpright(rotatedApRef);
		}

		startPhase = m_skipEnterAnimation ? 1.0f : m_entryDef.m_phase;

		const float distErr			= Dist(entryDef.m_entryNavLoc.GetPosPs(), pCharacter->GetTranslationPs());
		const float rotErrDeg		= RADIANS_TO_DEGREES(SafeAcos(Dot(GetLocalZ(entryDef.m_entryRotPs), GetLocalZ(pCharacter->GetRotationPs()))));
		const float rotErrFadeTime	= LerpScale(5.0f, 15.0f, 0.3f, 1.0f, rotErrDeg);
		const float distErrFadeTime	= LerpScale(0.0f, 0.5f, 0.2f, 1.0f, distErr);

		if (params.m_motionFadeTime < 0.0f)
		{
			params.m_motionFadeTime = rotErrFadeTime + distErrFadeTime;
		}

		const ArtItemAnim* pAnim = pAnimControl->LookupAnim(pEntryItem->m_animName).ToArtItem();
		const DC::SelfBlendParams* pSelfblend = m_skipEnterAnimation ? nullptr : pEntryItem->m_selfBlend;
		const BoundFrame apRef = pSelfblend ? rotatedApRef : unrotatedApRef;

		params.m_stateStartPhase = startPhase;
		params.m_apRef		  = apRef;
		params.m_apRefValid	  = true;
		params.m_animFadeTime = LimitBlendTime(pAnim, startPhase, params.m_animFadeTime);
		params.m_motionFadeTime	 = LimitBlendTime(pAnim, startPhase, params.m_motionFadeTime);
		params.m_freezeSrcState	 = false;
		params.m_freezeDestState = false;
		params.m_newInstBehavior = FadeToStateParams::kSpawnNewTrack;

		m_ownerAnimAction.FadeToState(pAnimControl, SID("s_cinematic-state-enter"), params, AnimAction::kFinishOnAnimEndEarly);

		if (pSelfblend)
		{
			m_ownerAnimAction.SetSelfBlendParams(pSelfblend, unrotatedApRef, pCharacter, 1.0f);
		}

		DC::AnimNavCharInfo* pInfo = pAnimControl->Info<DC::AnimNavCharInfo>();
		const Vector entryVelocityPs = entryDef.m_entryVelocityPs;
		const Vector charVelocityPs = pNavChar->GetVelocityPs();
		const float desSpeed = Length(entryVelocityPs);
		const float projSpeed = Dot(SafeNormalize(entryVelocityPs, kZero), charVelocityPs);
		const float speedRatio = (desSpeed > 0.0f) ? (projSpeed / desSpeed) : 1.0f;
		pInfo->m_speedFactor = Limit(speedRatio, 0.8f, 1.1f);
	}
	else
	{
		params.m_stateStartPhase = startPhase;
		params.m_freezeSrcState	 = false;
		params.m_freezeDestState = false;

		m_ownerAnimAction.FadeToState(pAnimControl, SID("s_cinematic-non-ap-state-enter"), params, AnimAction::kFinishOnAnimEndEarly);
	}

	if (pEntryItem)
	{
		if (NdGameObject* pProp = GetPropForCap(pNavChar.ToNavCharacter(), pCineAp))
		{
			m_hProp = pProp;
			SendEvent(SID("cap-reserve"), m_hProp);
			pProp->EnableAnimation();
			pProp->EnableUpdates();

			if (const DC::ApPropAnimList* pPropList = pEntryItem->m_props)
			{
				if (pPropList->m_count > 0)
				{
					//TODO(eomernick): support multiple props
					StringId64 propEntryAnim = pPropList->m_array[0].m_animName;

					if (propEntryAnim != INVALID_STRING_ID_64)
					{
						m_propAnimAction.FadeToAnim(pProp->GetAnimControl(),
													propEntryAnim,
													params.m_stateStartPhase,
													params.m_animFadeTime,
													params.m_blendType,
													false,
													false,
													AnimAction::kFinishOnAnimEndEarly);

						const DC::SelfBlendParams* pSelfblend = m_skipEnterAnimation ? nullptr : pEntryItem->m_selfBlend;
						if (pSelfblend)
						{
							BoundFrame unrotatedApRef = m_capBoundFrame;

							if (!pCapAnimCollection->m_noAdjustToUpright)
							{
								unrotatedApRef = pNavChar->AdjustBoundFrameToUpright(unrotatedApRef);
							}

							m_propAnimAction.SetSelfBlendParams(pSelfblend, unrotatedApRef, pProp, 1.0f);
						}
					}
				}
			}
		}
	}

	ActionPackMutex* pMutex = pCineAp->GetMutex();
	if (pMutex)
	{
		const bool enabled = pMutex->TryEnable(pCineAp, pCharacter);
		NAV_ASSERTF(enabled,
					("EnterCinematicActionPack: Unable to enable CAP mutex - Should have been tested in IsAvailable call in ReserveActionPack"));
	}

	CinematicActionPack* pMutableAp = const_cast<CinematicActionPack*>(pCineAp);
	pMutableAp->AddUser(pCharacter);

	// Ensure that partial animations can't be re-triggered
	if (!pCineAp->IsMultiCap())
	{
		pCineAp->ResetReadyToUseTimer();
	}

	m_noLegIk = pCapAnimCollection->m_noLegIk;
	m_noAdjustToGround = pCapAnimCollection->m_noAdjustToGround;
	m_startingDem = pNavChar->GetRequestedDemeanor();
	m_startingGunState = pNavChar->GetCurrentGunState();

	// If this CAP is associated with an interactable object, tell it to play its synchronized animation.
	const DC::CineActionPackPerformanceInteract* pPerfInteract = pCineAp->GetPerformanceInteract(pCharacter);
	NdGameObject* pInteractable = NdGameObject::FromProcess(pCineAp->GetProcess());
	if (pPerfInteract && pInteractable)
	{
		NdInteractControl* pInteractCtrl = pInteractable->GetInteractControl();
		if (pInteractCtrl)
		{
			if (pEntryItem && pEntryItem->m_userData)
			{
				InteractArgs interactArgs;
				interactArgs.m_startFrame = startPhase; // startPhase is a misnomer
				interactArgs.m_animId = *(StringId64*)pEntryItem->m_userData;

				pInteractCtrl->Interact(pCharacter, &interactArgs);
			}
			else if (pPerfInteract->m_defaultObjectAnim != INVALID_STRING_ID_64)
			{
				InteractArgs interactArgs;
				interactArgs.m_startFrame = startPhase; // startPhase is a misnomer
				interactArgs.m_animId = pPerfInteract->m_defaultObjectAnim;

				pInteractCtrl->Interact(pCharacter, &interactArgs);
			}
		}
	}

	g_ssMgr.BroadcastEvent(SID("cap-entered"), pCharacter->GetUserId(), pCineAp->GetSpawnerId());
	RequestDialog(SID("cap-entered"), pCharacter, pCineAp->GetSpawnerId()); //@DLG@ vox-remap missing

	m_skipEnterAnimation = false;
	m_enteredAp = true;

	m_hIkCap = pActionPack;

	GoEntering();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::PlayPartial(const CinematicActionPack* pActionPack,
										const DC::CineActionPackPerformancePartial* pPerformancePartial)
{
	ASSERT(pPerformancePartial);
	if (!pPerformancePartial)
		return;

	AnimControl* pAnimControl = GetCharacterGameObject()->GetAnimControl();
	AnimSimpleLayer* pPartialLayer = pAnimControl->GetSimpleLayerById(SID("weapon-partial"));

	if (!pAnimControl->LookupAnim(pPerformancePartial->m_anim).ToArtItem())
		return;

	AnimSimpleLayer::FadeRequestParams params;
	params.m_layerFadeOutParams.m_enabled = true;
	pPartialLayer->RequestFadeToAnim(pPerformancePartial->m_anim, params);
	pPartialLayer->Fade(1.0f, 1.0f);

	// Ensure that partial animations can't be re-triggered
	pActionPack->ResetReadyToUseTimer();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiCinematicController::IsPlaying() const
{
	const NdGameObject* pCharacter = GetCharacterGameObject();
	if (!pCharacter)
		return false;

	AnimControl* pAnimControl = pCharacter->GetAnimControl();
	if (!pAnimControl)
		return false;

	const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	if (!pBaseLayer)
		return false;

	switch (pBaseLayer->CurrentStateId().GetValue())
	{
	case SID_VAL("s_cinematic-state-enter"):
	case SID_VAL("s_cinematic-state-loop"):
	case SID_VAL("s_cinematic-state-wait"):
	case SID_VAL("s_cinematic-state-exit"):
	case SID_VAL("s_cinematic-state-interrupt"):
	case SID_VAL("s_cinematic-non-ap-state-enter"):
	case SID_VAL("s_cinematic-non-ap-state-loop"):
	case SID_VAL("s_cinematic-non-ap-state-wait"):
	case SID_VAL("s_cinematic-non-ap-state-exit"):
	case SID_VAL("s_cinematic-non-ap-state-interrupt"):
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiCinematicController::NoLegIk() const
{
	return m_noLegIk;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiCinematicController::IsAbortRequested() const
{
	return m_abortRequested;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiCinematicController::NoAdjustToGround() const
{
	return m_noAdjustToGround;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::Exit(const PathWaypointsEx* pExitPathPs)
{
	NavCharacterAdapter charAdapter = GetCharacterAdapter();
	NdGameObject* pCharacter = GetCharacterGameObject();
	const Npc* pNpc = Npc::FromProcess(pCharacter);

	AnimControl* pAnimControl = pCharacter->GetAnimControl();

	CinematicActionPack* pCineAp = GetEnteredActionPack();
	const DC::CapAnimCollection* pCapAnimCollection = pCineAp ? pCineAp->GetCapAnimCollection(pCharacter) : nullptr;

	const Demeanor dem = charAdapter.GetRequestedDemeanor();
	const Demeanor capDem = m_startingDem;
	const bool demeanorChanged = (dem != capDem);

	const AiEntity* pTargetEntity = pNpc ? pNpc->GetCurrentTargetEntity() : nullptr;
	const bool validHumanTarget = pTargetEntity && (pTargetEntity->GetEntityType() == DC::kAiEntityTypeHuman);

	const AiEntity* pInvestigateEntity = pNpc ? pNpc->GetCurrentInvestigateEntity() : nullptr;
	const AiEncounterCoordinator* pCoord = pNpc ? pNpc->GetEncounterCoordinator() : nullptr;
	const bool validInvestigation = pInvestigateEntity;

	const bool wantInterrupt = (demeanorChanged || validHumanTarget) && pCapAnimCollection && !pCapAnimCollection->m_neverInterrupt;

	if (wantInterrupt)
	{
		TryDropProp(pCineAp);

		if (const IEffectControl* pEffControl = pNpc->GetEffectControl())
		{
			KillParticle(pEffControl->GetLastParticleHandle());
		}
	}

	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	const StringId64 curStateId = pBaseLayer->CurrentStateId();

	switch (curStateId.GetValue())
	{
	case SID_VAL("s_cinematic-state-enter"):
	case SID_VAL("s_cinematic-non-ap-state-enter"):
	case SID_VAL("s_cinematic-state-wait"):
	case SID_VAL("s_cinematic-non-ap-state-wait"):
		{
			FadeToStateParams params;
			params.m_animFadeTime = 0.4f;
			params.m_motionFadeTime = 0.4f;
			m_ownerAnimAction.FadeToState(pAnimControl, SID("s_idle"), params, AnimAction::kFinishOnNonTransitionalStateReached);
			m_exitHandoff.SetStateChangeRequestId(m_ownerAnimAction.GetStateChangeRequestId(), SID("s_idle"));
			TryDropProp(pCineAp);
			return;
		}
	}

	m_exitPhase = -1.0f;
	m_propExitPhase = -1.0f;

	StringId64 stateId = SID("s_idle");
	const BoundFrame capApRef = (m_entryDef.m_hResolvedForAp.IsValid() && m_entryDef.m_useDesiredAlign)
									? m_entryDef.m_sbApReference
									: (pCineAp ? m_capBoundFrame : pCharacter->GetBoundFrame());
	BoundFrame rotApRef = capApRef;

	FadeToStateParams params;
	params.m_animFadeTime = 0.4f;
	params.m_motionFadeTime = 0.4f;
	params.m_blendType = DC::kAnimCurveTypeUniformS;
	params.m_apRef = capApRef;
	params.m_apRefValid = true;

	const DC::SelfBlendParams* pSelfBlend = nullptr;

	DC::AnimNavCharInfo* pInfo = pAnimControl->Info<DC::AnimNavCharInfo>();
	pInfo->m_apExitTransitions = SID("ap-exit^idle");

	m_exitHandoff.Reset();

	bool didPropAnim = false;

	if (!m_abortRequested && pCapAnimCollection)
	{
		if (wantInterrupt)
		{
			if (pCapAnimCollection->m_interrupt)
			{
				if (NavCharacter* pNavChar = charAdapter.ToNavCharacter())
				{
					if (GetTensionMode() == DC::kTensionModeCombat)
					{
						pNavChar->ForceDemeanor(kDemeanorAggressive, AI_LOG);
					}
				}

				const DC::BlendParams* pBlendParams = pCapAnimCollection->m_interrupt->m_blendIn;

				if (pBlendParams)
				{
					params.m_animFadeTime = pBlendParams->m_animFadeTime;
					params.m_motionFadeTime = pBlendParams->m_motionFadeTime;
					params.m_blendType = pBlendParams->m_curve;
				}

				params.m_apRef = m_capBoundFrame;
				params.m_apRefValid = true;

				AI::SetPluggableAnim(pCharacter, pCapAnimCollection->m_interrupt->m_animName, m_mirror);
				stateId = GetInterruptStateId();
			}
		}
		else if (pCapAnimCollection->m_exitList)
		{
			const DC::BlendParams* pBlendParams = pCapAnimCollection->m_defaultExitBlendIn;

			if (pBlendParams)
			{
				params.m_animFadeTime = pBlendParams->m_animFadeTime;
				params.m_motionFadeTime = pBlendParams->m_motionFadeTime;
				params.m_blendType = pBlendParams->m_curve;
			}

			ApExit::CharacterState cs;
			MakeExitCharacterState(pCineAp, pExitPathPs, &cs);

			ActionPackExitDef selectedExit;
			if (ResolveExit(cs, capApRef, pCapAnimCollection->m_exitList, &selectedExit) && selectedExit.m_pDcDef)
			{
				const DC::ApExitAnimDef* pExitDef = selectedExit.m_pDcDef;

				params.m_apRef		   = selectedExit.m_apReference;
				params.m_apRefValid	   = true;
				params.m_customApRefId = selectedExit.m_apRefId;
				rotApRef = selectedExit.m_sbApReference;

				stateId = GetExitStateId();

				AI::SetPluggableAnim(pCharacter, pExitDef->m_animName, selectedExit.m_mirror);

				if (pCapAnimCollection->m_compensateExitPhaseCrossFade && (m_lastLoop.m_crossFadeTime > 0.0f))
				{
					const AnimStateInstance* pCurInst = pBaseLayer->CurrentStateInstance();
					const ArtItemAnim* pLoopAnim = pCurInst ? pCurInst->GetPhaseAnimArtItem().ToArtItem() : nullptr;
					const ArtItemAnim* pExitAnim = pAnimControl->LookupAnim(pExitDef->m_animName).ToArtItem();

					if (pLoopAnim && pExitAnim)
					{
						const float curPhase = pCurInst->Phase();
						const float maxDuration = GetDuration(pLoopAnim);
						const float crossFadeTime = maxDuration - m_lastLoop.m_crossFadeTime;
						const float curDuration = curPhase * maxDuration;
						const float compensateTime = Max(curDuration - crossFadeTime, 0.0f);
						const float newDuration = GetDuration(pExitAnim);

						params.m_stateStartPhase = (newDuration > NDI_FLT_EPSILON) ? Limit01(compensateTime / newDuration) : 0.0f;
						params.m_preventBlendTimeOverrun = true;
					}
				}

				if (pExitDef->m_blendIn)
				{
					params.m_animFadeTime	= pExitDef->m_blendIn->m_animFadeTime;
					params.m_motionFadeTime = pExitDef->m_blendIn->m_motionFadeTime;
					params.m_blendType		= pExitDef->m_blendIn->m_curve;
				}

				if (pExitDef->m_selfBlend)
				{
					pSelfBlend = pExitDef->m_selfBlend;
				}

				m_exitPhase = pExitDef->m_exitPhase;
				m_propExitPhase = pCineAp->GetReleasePropPhase();

				if (pExitDef->m_validMotions)
				{
					pInfo->m_apExitTransitions = SID("ap-exit^move");
					m_exitHandoff.m_motionType = charAdapter->GetRequestedMotionType();
				}
				else
				{
					pInfo->m_apExitTransitions = SID("ap-exit^idle");
				}

				if (pExitDef->m_navAnimHandoff)
				{
					m_exitHandoff.ConfigureFromDc(pExitDef->m_navAnimHandoff);
				}
			}

			if (const NdGameObject* pProp = pCineAp->GetPropHandle().ToProcess())
			{
				if (selectedExit.m_pDcDef)
				{
					if (const DC::ApPropAnimList* pPropList = selectedExit.m_pDcDef->m_props)
					{
						if (pPropList->m_count > 0)
						{
							//TODO(eomernick): support multiple props
							StringId64 propExitAnim = pPropList->m_array[0].m_animName;

							if (propExitAnim != INVALID_STRING_ID_64)
							{
								m_propAnimAction.FadeToAnim(pProp->GetAnimControl(),
															propExitAnim,
															params.m_stateStartPhase,
															params.m_animFadeTime,
															params.m_blendType,
															false,
															false,
															AnimAction::kFinishOnAnimEnd);

								didPropAnim = true;
							}
						}
					}
				}
			}
		}
	}

	params.m_freezeSrcState = false;
	params.m_freezeDestState = false;

	m_ownerAnimAction.FadeToState(pAnimControl, stateId, params, AnimAction::kFinishOnNonTransitionalStateReached);

	if (!didPropAnim)
	{
		TryDropProp(pCineAp);
	}

	if (pSelfBlend)
	{
		if (m_exitPhase >= 0.0f)
		{
			m_ownerAnimAction.SetSelfBlendParams(pSelfBlend, rotApRef, pCharacter, m_exitPhase);
			m_propAnimAction.SetSelfBlendParams(pSelfBlend, rotApRef, pCineAp->GetPropHandle().ToMutableProcess(), m_exitPhase);
		}
		else
		{
			m_ownerAnimAction.SetSelfBlendParams(pSelfBlend, rotApRef, pCharacter, 1.0f);
			m_propAnimAction.SetSelfBlendParams(pSelfBlend, rotApRef, pCineAp->GetPropHandle().ToMutableProcess(), 1.0f);
		}
	}
	else
	{
		m_ownerAnimAction.ClearSelfBlendParams();
		m_propAnimAction.ClearSelfBlendParams();
	}

	m_exitHandoff.SetStateChangeRequestId(m_ownerAnimAction.GetStateChangeRequestId(), stateId);
	charAdapter.ConfigureNavigationHandOff(m_exitHandoff, FILE_LINE_FUNC);

	if (pCineAp)
	{
		pCineAp->ResetReadyToUseTimer();
		pCineAp->RemoveUser(pCharacter);

		g_ssMgr.BroadcastEvent(SID("cap-exited"), pCharacter->GetUserId(), pCineAp->GetSpawnerId());
		RequestDialog(SID("cap-exited"), pCharacter, pCineAp->GetSpawnerId()); //@DLG@ vox-remap missing

		if (NdGameObject* pInteractable = NdGameObject::FromProcess(pCineAp->GetProcess()))
		{
			if (NdInteractControl* pInteractCtrl = pInteractable->GetInteractControl())
			{
				pInteractCtrl->EnableNavBlockerWhenClear(pCharacter);
			}
		}
	}

	GoExiting();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::StartWait(StringId64 waitAnimId, StringId64 waitPropAnimId /*= INVALID_STRING_ID_64*/)
{
	NdGameObject* pCharacter = GetCharacterGameObject();
	AnimControl* pAnimControl = pCharacter->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	StringId64 animId = waitAnimId;
	StringId64 stateId = GetWaitStateId();

	if (animId == INVALID_STRING_ID_64)
	{
		animId = SID("idle");
		stateId = SID("s_cinematic-non-ap-state-wait");
	}

	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	AI::SetPluggableAnim(pCharacter, animId, m_mirror);

	m_ownerAnimAction.ClearSelfBlendParams();
	m_propAnimAction.ClearSelfBlendParams();
	FadeToStateParams params;
	params.m_animFadeTime = 0.4f;
	m_ownerAnimAction.FadeToState(pAnimControl, stateId, params, AnimAction::kFinishOnTransitionTaken);

	if (waitPropAnimId)
	{
		CinematicActionPack* pCineAp = GetEnteredActionPack();
		if (NdGameObject* pProp = pCineAp->GetPropHandle().ToMutableProcess())
		{
			m_propAnimAction.FadeToAnim(pProp->GetAnimControl(),
										waitPropAnimId,
										0.0f,
										0.0f,
										DC::kAnimCurveTypeUniformS,
										false,
										false,
										AnimAction::kFinishOnLoopingAnimEnd);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::SetAutomaticMode(bool enableAuto)
{
	m_auto = enableAuto;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::StartLoop(const DC::CapAnimLoop* pLoopSet, bool first)
{
	if (!pLoopSet)
		return;

	NdGameObject* pCharacter	 = GetCharacterGameObject();
	AnimControl* pAnimControl	 = pCharacter->GetAnimControl();
	AnimStateLayer* pBaseLayer	 = pAnimControl->GetBaseStateLayer();
	CinematicActionPack* pCineAp = GetEnteredActionPack();

	if (!pCineAp)
		return;

	if (!IsInValidStateToStartLoop())
	{
		ASSERTF(false, ("You called StartLoop() for a character who can't do that"));
		return;
	}

	const DC::CapAnimCollection* pCapAnimCollection = pCineAp->GetCapAnimCollection(pCharacter);

	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	const StringId64 stateId = GetLoopStateId();
	AI::SetPluggableAnim(pCharacter, pLoopSet->m_animName, m_mirror);

	m_ownerAnimAction.ClearSelfBlendParams();
	m_propAnimAction.ClearSelfBlendParams();

	FadeToStateParams params;
	params.m_animFadeTime	= 0.0f;
	params.m_motionFadeTime = 0.0f;
	params.m_blendType		= DC::kAnimCurveTypeUniformS;
	params.m_apRef		= m_capBoundFrame;
	params.m_apRefValid = true;

	if (first)
	{
		params.m_animFadeTime	= 0.4f;
		params.m_motionFadeTime = 0.4f;

		if (pLoopSet->m_blendIn)
		{
			params.m_animFadeTime	= pLoopSet->m_blendIn->m_animFadeTime;
			params.m_motionFadeTime = pLoopSet->m_blendIn->m_motionFadeTime;
			params.m_blendType		= pLoopSet->m_blendIn->m_curve;
		}
		else if (pCapAnimCollection->m_defaultLoopBlendIn)
		{
			params.m_animFadeTime	= pCapAnimCollection->m_defaultLoopBlendIn->m_animFadeTime;
			params.m_motionFadeTime = pCapAnimCollection->m_defaultLoopBlendIn->m_motionFadeTime;
			params.m_blendType		= pCapAnimCollection->m_defaultLoopBlendIn->m_curve;
		}
	}
	else
	{
		params.m_animFadeTime = m_lastLoop.m_crossFadeTime;
		params.m_motionFadeTime = m_lastLoop.m_crossFadeTime;
	}

	m_ownerAnimAction.FadeToState(pAnimControl, stateId, params, AnimAction::kFinishOnTransitionTaken);

	m_lastLoop = *pLoopSet;

	// If this CAP is associated with an interactable object, tell it to play its synchronized animation.
	if (NdGameObject* pInteractable = NdGameObject::FromProcess(pCineAp->GetProcess()))
	{
		if (NdInteractControl* pInteractCtrl = pInteractable->GetInteractControl())
		{
			InteractArgs interactArgs;
			interactArgs.m_animId = pLoopSet->m_objectAnim;

			pInteractCtrl->Interact(pCharacter, &interactArgs);
		}
	}

	if (NdGameObject* pProp = pCineAp->GetPropHandle().ToMutableProcess())
	{
		if (const DC::ApPropAnimList* pPropList = pLoopSet->m_props)
		{
			if (pPropList->m_count > 0)
			{
				//TODO(eomernick): support multiple props
				StringId64 propEntryAnim = pPropList->m_array[0].m_animName;

				if (propEntryAnim != INVALID_STRING_ID_64)
				{
					if (OpenableObject* pOpenable = OpenableObject::FromProcess(pProp))
					{
						pOpenable->Open(pCharacter, propEntryAnim);
					}
					else
					{
						m_propAnimAction.FadeToAnim(pProp->GetAnimControl(),
													propEntryAnim,
													params.m_stateStartPhase,
													params.m_animFadeTime,
													params.m_blendType,
													false,
													false,
													AnimAction::kFinishOnAnimEndEarly);
					}
				}
			}
		}
	}

	if (first)
	{
		g_ssMgr.BroadcastEvent(SID("cap-loop-started"), pCharacter->GetUserId(), pCineAp->GetSpawnerId());
		RequestDialog(SID("cap-loop-started"), pCharacter, pCineAp->GetSpawnerId()); //@DLG@ vox-remap missing
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiCinematicController::IsLoopComplete() const
{
	if (!m_ownerAnimAction.IsDone())
		return false;

	const NdGameObject* pCharacter = GetCharacterGameObject();
	AnimControl* pAnimControl = pCharacter ? pCharacter->GetAnimControl() : nullptr;
	const AnimStateInstance* pDestInst = m_ownerAnimAction.GetTransitionDestInstance(pAnimControl);

	const float instDuration = pDestInst ? pDestInst->GetDuration() : -1.0f;

	if (!pDestInst || (instDuration < kSmallFloat))
	{
		return true;
	}

	const float crossFadePhase = Limit01((instDuration - m_lastLoop.m_crossFadeTime) / instDuration);

	if (!pDestInst || (pDestInst->Phase() >= crossFadePhase))
	{
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::RequestAnimations()
{
	if (BaseState* pState = GetState())
	{
		pState->RequestAnimations();
	}

	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::UpdateStatus()
{
	NdGameObject* pCharacter = GetCharacterGameObject();
	AI_ASSERT(pCharacter);
	AnimControl* pAnimControl = pCharacter->GetAnimControl();
	AI_ASSERT(pAnimControl);

	m_ownerAnimAction.Update(pAnimControl);

	bool hasProp = false;
	if (NdGameObject* pProp = m_hProp.ToMutableProcess())
	{
		AnimControl* pPropAnimControl = pProp->GetAnimControl();
		m_propAnimAction.Update(pPropAnimControl);
		SendEvent(SID("cap-in-use"), m_hProp, BoxedValue(pCharacter));
		hasProp = true;
	}

	if (hasProp || m_hCapProcess.HandleValid())
	{
		if (AnimStateLayer* pLayer = pAnimControl->GetBaseStateLayer())
		{
			StringId64 curState = pLayer->CurrentStateId();
			if (hasProp && (curState == SID("s_cinematic-state-exit") || curState == SID("s_idle")))
			{
				if (AnimStateInstance* pInst = pLayer->CurrentStateInstance())
				{
					const float curPhase = pInst->GetPhase();
					const float curFadeLeft = pInst->AnimFadeTimeLeft();
					if (curState == SID("s_idle")
						|| (((!hasProp || curPhase >= (m_propExitPhase >= 0.0 ? m_propExitPhase : 1.0f))
							&& curFadeLeft < 0.1f)))
					{
						// I hate doors.
						SendEvent(SID("cap-release"), m_hProp);
						m_hProp = nullptr;
						m_propAnimAction.Reset();
					}
				}
			}

			if (m_hCapProcess.HandleValid() && IsEnterComplete() && IsState(kStateIdle))
			{
				switch (curState.GetValue())
				{
				case SID_VAL("s_cinematic-state-loop"):
				case SID_VAL("s_cinematic-state-wait"):
				case SID_VAL("s_cinematic-state-enter"):
				case SID_VAL("s_cinematic-non-ap-state-enter"):
				case SID_VAL("s_cinematic-non-ap-state-loop"):
				case SID_VAL("s_cinematic-non-ap-state-wait"):
				case SID_VAL("s_cinematic-state-exit"):
				case SID_VAL("s_cinematic-state-interrupt"):
				case SID_VAL("s_cinematic-non-ap-state-exit"):
				case SID_VAL("s_cinematic-non-ap-state-interrupt"):
					break;
				default:
					KillProcess(m_hCapProcess);
					break;
				}
			}
		}
	}

	if (!m_ownerAnimAction.IsDone() && m_exitPhase >= 0.0f)
	{
		const AnimStateInstance* pExitInst = m_ownerAnimAction.GetTransitionDestInstance(pAnimControl);
		NavCharacter* pNavChar = GetCharacter();
		if (pExitInst && pNavChar)
		{
			const float curPhase = pExitInst->Phase();
			if (curPhase >= m_exitPhase)
			{
				m_exitHandoff.SetAnimStateInstance(pExitInst);
				pNavChar->ConfigureNavigationHandOff(m_exitHandoff, FILE_LINE_FUNC);
				m_ownerAnimAction.Reset();
			}
		}
	}

	GetStateMachine().TakeStateTransition();

	if (BaseState* pState = GetState())
	{
		pState->UpdateStatus();
	}

	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct CapIkTargets
{
	Locator m_ikTargetOs[kArmCount] = { kInvalidLocator, kInvalidLocator };
	float m_ikStrength[kArmCount] = { kLargeFloat, kLargeFloat };
	bool m_valid[kArmCount]		  = { false, false };
};

/// --------------------------------------------------------------------------------------------------------------- ///
static CONST_EXPR StringId64 s_capIkFloatChannelIds[kArmCount] = { SID("handIkCutomLocAttr.l_wrist_ik_blend"),
																   SID("handIkCutomLocAttr.r_wrist_ik_blend") };
static CONST_EXPR StringId64 s_capIkApChannelIds[kArmCount] = { SID("apReference-hand-l"), SID("apReference-hand-r") };

/// --------------------------------------------------------------------------------------------------------------- ///
class IkTargetBlender : public AnimStateLayer::InstanceBlender<CapIkTargets>
{
public:
	virtual CapIkTargets GetDefaultData() const override { return CapIkTargets(); }

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, CapIkTargets* pDataOut) override
	{
		*pDataOut = CapIkTargets();

		const StringId64 stateId = pInstance->GetStateName();

		switch (stateId.GetValue())
		{
		case SID_VAL("s_cinematic-state-enter"):
		case SID_VAL("s_cinematic-state-loop"):
		case SID_VAL("s_cinematic-state-wait"):
		case SID_VAL("s_cinematic-state-exit"):
		case SID_VAL("s_cinematic-state-interrupt"):
		case SID_VAL("s_cinematic-non-ap-state-enter"):
		case SID_VAL("s_cinematic-non-ap-state-loop"):
		case SID_VAL("s_cinematic-non-ap-state-wait"):
		case SID_VAL("s_cinematic-non-ap-state-exit"):
		case SID_VAL("s_cinematic-non-ap-state-interrupt"):
			{
				AnimStateEvalParams params;
				params.m_disableRetargeting = true;

				float floatVals[kArmCount] = { kLargeFloat, kLargeFloat };
				ndanim::JointParams apVals[kArmCount];

				const U32F floatMask = pInstance->EvaluateFloatChannels(s_capIkFloatChannelIds,
																		kArmCount,
																		floatVals,
																		params);

				const U32F apMask = pInstance->EvaluateChannels(s_capIkApChannelIds, kArmCount, apVals, params);

				const U32F validMask = apMask; //floatMask & apMask;

				for (U32F iArm = 0; iArm < kArmCount; ++iArm)
				{
					if (0 == (validMask & (1ULL << iArm)))
						continue;

					ANIM_ASSERT(IsReasonable(floatVals[iArm]));
					ANIM_ASSERT(IsReasonable(apVals[iArm].m_quat));
					ANIM_ASSERT(IsReasonable(apVals[iArm].m_scale));
					ANIM_ASSERT(IsReasonable(apVals[iArm].m_trans));

					pDataOut->m_valid[iArm] = true;
					pDataOut->m_ikStrength[iArm] = (floatMask & (1ULL <<iArm)) ? floatVals[iArm] : 1.0f;
					pDataOut->m_ikTargetOs[iArm] = Locator(apVals[iArm].m_trans, apVals[iArm].m_quat);
				}
			}
			break;
		}

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual CapIkTargets BlendData(const CapIkTargets& leftData,
								   const CapIkTargets& rightData,
								   float masterFade,
								   float animFade,
								   float motionFade) override
	{
		CapIkTargets ret;

		for (U32F iArm = 0; iArm < kArmCount; ++iArm)
		{
			ret.m_valid[iArm] = leftData.m_valid[iArm] || rightData.m_valid[iArm];

			if (leftData.m_valid[iArm] && rightData.m_valid[iArm])
			{
				ret.m_ikTargetOs[iArm] = Lerp(leftData.m_ikTargetOs[iArm], rightData.m_ikTargetOs[iArm], animFade);
				ret.m_ikStrength[iArm] = Lerp(leftData.m_ikStrength[iArm], rightData.m_ikStrength[iArm], animFade);
			}
			else if (leftData.m_valid[iArm])
			{
				ret.m_ikTargetOs[iArm] = leftData.m_ikTargetOs[iArm];
				ret.m_ikStrength[iArm] = leftData.m_ikStrength[iArm];
			}
			else if (rightData.m_valid[iArm])
			{
				ret.m_ikTargetOs[iArm] = rightData.m_ikTargetOs[iArm];
				ret.m_ikStrength[iArm] = rightData.m_ikStrength[iArm];
			}
		}

		return ret;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::UpdateProcedural()
{
	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_cinematic.m_disableCapHandIk))
	{
		return;
	}

	if (!m_hIkCap.IsValid())
	{
		return;
	}

	NavCharacter* pNavChar = GetCharacterAdapter().ToNavCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const AnimStateInstance* pCurInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;

	if (!pCurInstance)
	{
		return;
	}

	const FgAnimData* pAnimData = pNavChar->GetAnimData();
	JointSet* pJointSet = pAnimData ? pAnimData->m_pPluginJointSet : nullptr;

	if (!pJointSet)
	{
		m_hIkCap = ActionPackHandle();
		return;
	}

	Vector ikOffsetOs;

	const CinematicActionPack* pCineAp = m_hIkCap.ToActionPack<CinematicActionPack>();
	if (pCineAp && !AllComponentsEqual(pCineAp->GetOverrideIkOffsetOs(), kInvalidVector))
	{
		ikOffsetOs = pCineAp->GetOverrideIkOffsetOs();
	}
	else
	{
		const EntitySpawner* pCapSpawner = pCineAp ? pCineAp->GetSpawner() : nullptr;
		const EntityDB* pEntityDb = pCapSpawner ? pCapSpawner->GetEntityDB() : nullptr;

		if (!pEntityDb || pEntityDb->GetData<bool>(SID("no-hand-ik"), false))
		{
			m_hIkCap = ActionPackHandle();
			return;
		}

		const EntityDB::Record* pOffsetRec = pEntityDb->GetRecord(SID("hand-ik-offset"));
		if (!pOffsetRec || !pOffsetRec->IsValidVector())
		{
			m_hIkCap = ActionPackHandle();
			return;
		}

		ikOffsetOs = pOffsetRec->GetData<Vector>(kZero);
	}

	IkTargetBlender blender;
	const CapIkTargets targets = blender.BlendForward(pBaseLayer, CapIkTargets());

	if (!targets.m_valid[kLeftArm] && !targets.m_valid[kRightArm])
	{
		if (IsState(kStateNone) && !HasNewState())
		{
			m_hIkCap = ActionPackHandle();
		}
		return;
	}

	const Locator capLocWs = pCineAp->GetLocatorWs();

	const Locator alignWs = pNavChar->GetLocator();
	const Locator animApWs = pCurInstance->GetApOrigin().GetLocatorWs();

	const ArtItemAnim* pCapAnim = pCurInstance->GetPhaseAnimArtItem().ToArtItem();
	if (!pCapAnim)
	{
		return;
	}

	const SkeletonId skelId = pNavChar->GetSkeletonId();
	const float curPhase = pCurInstance->Phase();
	const StringId64 apChannelId = pCurInstance->GetApRefChannelId();
	const bool flipped = pCurInstance->IsFlipped();

	Locator desAlignWs = alignWs;
	if (!FindAlignFromApReference(skelId, pCapAnim, curPhase, capLocWs, apChannelId, &desAlignWs, flipped))
	{
		return;
	}

	const bool debugDraw = false;

	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_cinematic.m_debugDrawCapHandIk))
	{
		g_prim.Draw(DebugCoordAxes(desAlignWs));
		g_prim.Draw(DebugCoordAxes(alignWs, 0.2f, kPrimEnableHiddenLineAlpha));
	}

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	if (pJointSet->ReadJointCache())
	{
		for (U32F iArm = 0; iArm < kArmCount; ++iArm)
		{
			if (!targets.m_valid[iArm])
				continue;

			if (targets.m_ikStrength[iArm] < NDI_FLT_EPSILON)
				continue;

			const Locator targWithOffsetOs = Locator(targets.m_ikTargetOs[iArm].Pos() + ikOffsetOs, targets.m_ikTargetOs[iArm].Rot());
			const Locator ikTargWs = desAlignWs.TransformLocator(targWithOffsetOs);

			if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_cinematic.m_debugDrawCapHandIk))
			{
				MsgCon("%s : %s [%s @ %f]\n",
					   GetHandIkJointName(iArm),
					   PrettyPrint(targWithOffsetOs.Pos()),
					   DevKitOnly_StringIdToString(s_capIkFloatChannelIds[iArm]),
					   targets.m_ikStrength[iArm]);

				g_prim.Draw(DebugCoordAxes(ikTargWs));
				g_prim.Draw(DebugString(ikTargWs.Pos(),
										StringBuilder<64>("%s goal @ %0.3f",
														  GetHandIkJointName(iArm),
														  targets.m_ikStrength[iArm])
											.c_str(),
										kColorWhiteTrans,
										0.5f));

				const Locator ikAnimWs = alignWs.TransformLocator(targets.m_ikTargetOs[iArm]);
				g_prim.Draw(DebugCoordAxes(ikAnimWs, 0.2f, kPrimEnableHiddenLineAlpha));
				g_prim.Draw(DebugString(ikAnimWs.Pos(),
										StringBuilder<64>("%s anim", GetHandIkJointName(iArm)).c_str(),
										kColorWhiteTrans,
										0.5f));
			}

			TwoBoneIkParams params;
			params.m_goalPos = ikTargWs.Pos();
			params.m_finalGoalRot = ikTargWs.Rot();
			params.m_jointOffsets[0] = pJointSet->FindJointOffset(ArmIkChain::kJointIds[iArm][1]);
			params.m_jointOffsets[1] = pJointSet->FindJointOffset(ArmIkChain::kJointIds[iArm][2]);
			params.m_jointOffsets[2] = pJointSet->FindJointOffset(ArmIkChain::kJointIds[iArm][3]);
			params.m_pJointSet = pJointSet;
			params.m_tt = targets.m_ikStrength[iArm];

			TwoBoneIkResults results;
			SolveTwoBoneIK(params, results);
		}

		pJointSet->WriteJointCache();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiCinematicController::IsBusy() const
{
	return !m_ownerAnimAction.IsDone();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::ForceAbortAction()
{
	NdGameObject* pCharacter = GetCharacterGameObject();
	AnimControl* pAnimControl = pCharacter->GetAnimControl();

	NavCharacter* pNavChar = GetCharacterAdapter().ToNavCharacter();

	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	if (!pBaseLayer->AreTransitionsPending())
	{
		if (!RequestAbortAction())
		{
			m_abortRequested = true;

			m_ownerAnimAction.Request(pAnimControl, SID("idle"), AnimAction::kFinishOnNonTransitionalStateReached);
			AiLogAnim(pNavChar, "AiCinematicController::ForceAbortAction() - pushing 'idle' transition'\n");

			CinematicActionPack* pCineAp = GetEnteredActionPack();

			TryDropProp(pCineAp);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiCinematicController::RequestAbortAction()
{
	if (m_abortRequested)
	{
		return true;
	}

	if (IsEnterComplete())
	{
		return false;
	}

	if (!m_enteredAp)
	{
		return false;
	}

	const DC::ApEntryItem* pEntryItem = m_entryDef.m_pDcDef;
	if (!pEntryItem)
	{
		return false;
	}

	NdGameObject* pCharacter	= GetCharacterGameObject();
	AnimControl* pAnimControl	= pCharacter->GetAnimControl();
	const float curPhase		= m_ownerAnimAction.GetAnimPhase(pAnimControl);

	m_abortRequested = (pEntryItem->m_commitPhase < 0.0f) || (curPhase < pEntryItem->m_commitPhase);

	if (m_abortRequested)
	{
		NavCharacter* pNavChar = GetCharacterAdapter().ToNavCharacter();
		AiLogAnim(pNavChar, "AiCinematicController::RequestAbortAction() - pushing 'idle' transition'\n");

		AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
		pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);
		m_ownerAnimAction.Request(pAnimControl, SID("idle"), AnimAction::kFinishOnNonTransitionalStateReached);

		CinematicActionPack* pCineAp = GetEnteredActionPack();

		TryDropProp(pCineAp);
	}

	return m_abortRequested;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::TryDropProp(CinematicActionPack* pCineAp)
{
	if (!pCineAp)
		return;

	NavCharacter* pNavChar = GetCharacterAdapter().ToNavCharacter();
	if (!pNavChar)
		return;

	if (NdAttachableObject* pProp = NdAttachableObject::FromProcess(pCineAp->GetPropHandle().ToMutableProcess()))
	{
		if (pProp->GetParentGameObject() == pNavChar)
		{
			AiLogAnim(pNavChar, "AiCinematicController::RequestAbortAction() - Dropping prop '%s''\n", DevKitOnly_StringIdToString(pProp->GetUserId()));

			pProp->RequestDetach(0.0f, pProp->GetVelocity(), pProp->GetAngularVelocityWs());
			SendEvent(SID("physicalize-live"), pProp);
			m_propAnimAction.Reset();
		}
		// I Hate Doors
		SendEvent(SID("cap-release"), pProp);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AiCinematicController::GetNavMeshAdjustBlendFactor() const
{
	float factor = 0.0f;

	if (const BaseState* pState = GetState())
	{
		factor = pState->GetNavMeshAdjustBlendFactor();
	}

	//MsgCon("[%s] : %f (%s)\n", GetCharacterGameObject()->GetName(), factor, GetStateMachine().GetStateName("???"));

	return factor;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::DebugDrawEntries(const ActionPackResolveInput& input, const ActionPack* pActionPack) const
{
	STRIP_IN_FINAL_BUILD;

	const NdGameObject* pCharacter = GetCharacterGameObject();
	const CinematicActionPack* pCineAp = static_cast<const CinematicActionPack*>(pActionPack);

	const BoundFrame apRef = pActionPack->GetBoundFrame();

	const DC::ApEntryItem* pChosenEntry = m_entryDef.m_pDcDef;

	DC::ApEntryItemList entryList;
	DC::ApEntryItem entryItems[kMaxAnimEntries];

	entryList.m_array = entryItems;
	entryList.m_count = GetCapEnterAnims(pCineAp, entryItems, kMaxAnimEntries);

	DebugDrawEntryAnims(input,
						pActionPack,
						apRef,
						entryList,
						pChosenEntry,
						g_aiGameOptions.m_cinematic);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiCinematicController::DebugDrawExits(const ActionPackResolveInput& input,
										   const ActionPack* pActionPack,
										   const IPathWaypoints* pPathPs) const
{
	STRIP_IN_FINAL_BUILD;

	const NdGameObject* pCharacter = GetCharacterGameObject();
	const CinematicActionPack* pCineAp = static_cast<const CinematicActionPack*>(pActionPack);

	const BoundFrame apRef = pActionPack->GetBoundFrame();

	const DC::ApExitAnimDef* pChosenExit = nullptr;

	DC::ApExitAnimList exitList;
	DC::ApExitAnimDef exitAnimDefs[kMaxAnimEntries];

	exitList.m_array = exitAnimDefs;
	exitList.m_count = GetCapExitAnims(pCineAp, exitAnimDefs, kMaxAnimEntries);

	DebugDrawExitAnims(input,
					   pActionPack,
					   apRef,
					   pPathPs,
					   exitList,
					   pChosenExit,
					   g_aiGameOptions.m_cinematic);
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F AiCinematicController::GetCapEnterAnimCount(const CinematicActionPack* pCineAp) const
{
	return GetCapEnterAnims(pCineAp, nullptr, 0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F AiCinematicController::GetCapExitAnimCount(const CinematicActionPack* pCineAp) const
{
	return GetCapExitAnims(pCineAp, nullptr, 0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F AiCinematicController::GetCapEnterAnims(const CinematicActionPack* pCineAp,
											 DC::ApEntryItem entryItems[],
											 I32F entryItemsSize) const
{
	const NdGameObject* pCharacter = GetCharacterGameObject();

	const DC::ApEntryItemList* pDcEntryList = GetApEntryListFor(pCineAp, pCharacter);
	if (!pDcEntryList)
	{
		return 0;
	}

	I32F count = 0;

	if (entryItems)
	{
		for (U32F ii = 0; ii < pDcEntryList->m_count && count < entryItemsSize; ++ii)
		{
			entryItems[count++] = pDcEntryList->m_array[ii];
		}
	}
	else
	{
		count += pDcEntryList->m_count;
	}

	return count;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F AiCinematicController::GetCapExitAnims(const CinematicActionPack* pCineAp,
											DC::ApExitAnimDef exitItems[],
											I32F exitItemsSize) const
{
	const NdGameObject* pCharacter = GetCharacterGameObject();

	const DC::ApExitAnimList* pDcExitList = GetApExitListFor(pCineAp, pCharacter);
	if (!pDcExitList)
	{
		return 0;
	}

	I32F count = 0;

	if (exitItemsSize > 0)
	{
		for (U32F ii = 0; ii < pDcExitList->m_count && count < exitItemsSize; ++ii)
		{
			exitItems[count++] = pDcExitList->m_array[ii];
		}
	}
	else
	{
		count += pDcExitList->m_count;
	}

	return count;
}

/// --------------------------------------------------------------------------------------------------------------- ///
CinematicActionPack* AiCinematicController::GetEnteredActionPack() const
{
	NavCharacterAdapter pNavChar = GetCharacterAdapter();
	AI_ASSERT(pNavChar);
	ActionPack* pActionPack = pNavChar->GetEnteredActionPack();

	if (!pActionPack || pActionPack->GetType() != ActionPack::kCinematicActionPack)
	{
		return nullptr;
	}

	CinematicActionPack* pCineAp = static_cast<CinematicActionPack*>(pActionPack);
	return pCineAp;
}

/*
;; Cinematic AP
s_cinematic-state-enter
s_cinematic-state-loop
s_cinematic-state-wait
s_cinematic-state-exit
s_cinematic-state-interrupt
s_cinematic-non-ap-state-enter
s_cinematic-non-ap-state-loop
s_cinematic-non-ap-state-wait
s_cinematic-non-ap-state-exit
s_cinematic-non-ap-state-interrupt
*/

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiCinematicController::GetLoopStateId() const
{
	const NdGameObject* pCharacter = GetCharacterGameObject();
	const CinematicActionPack* pCineAp = GetEnteredActionPack();
	const DC::CineActionPackPerformanceFull* pPerformanceFull = pCineAp ? pCineAp->GetPerformanceFull(pCharacter) : nullptr;

	if (pPerformanceFull && (pPerformanceFull->m_location == DC::kCineActionPackLocationInPlace))
	{
		return SID("s_cinematic-non-ap-state-loop");
	}
	else
	{
		return SID("s_cinematic-state-loop");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiCinematicController::GetExitStateId() const
{
	const NdGameObject* pCharacter = GetCharacterGameObject();
	const CinematicActionPack* pCineAp = GetEnteredActionPack();
	const DC::CineActionPackPerformanceFull* pPerformanceFull = pCineAp ? pCineAp->GetPerformanceFull(pCharacter) : nullptr;

	if (pPerformanceFull && (pPerformanceFull->m_location == DC::kCineActionPackLocationInPlace))
	{
		return SID("s_cinematic-non-ap-state-exit");
	}
	else
	{
		return SID("s_cinematic-state-exit");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiCinematicController::GetInterruptStateId() const
{
	const NdGameObject* pCharacter = GetCharacterGameObject();
	const CinematicActionPack* pCineAp = GetEnteredActionPack();
	const DC::CineActionPackPerformanceFull* pPerformanceFull = pCineAp ? pCineAp->GetPerformanceFull(pCharacter)
																		: nullptr;

	if (!pPerformanceFull)
	{
		return SID("s_cinematic-non-ap-state-interrupt");
	}

	DC::CineActionPackLocation locType = pPerformanceFull->m_locationInterrupt;

	if (locType == DC::kCineActionPackLocationInvalid)
	{
		locType = pPerformanceFull->m_location;
	}

	if (locType == DC::kCineActionPackLocationInPlace)
	{
		return SID("s_cinematic-non-ap-state-interrupt");
	}
	else
	{
		return SID("s_cinematic-state-interrupt");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiCinematicController::GetWaitStateId() const
{
	const NdGameObject* pCharacter = GetCharacterGameObject();
	const CinematicActionPack* pCineAp = GetEnteredActionPack();
	const DC::CineActionPackPerformanceFull* pPerformanceFull = pCineAp ? pCineAp->GetPerformanceFull(pCharacter)
																		: nullptr;

	if (pPerformanceFull && (pPerformanceFull->m_location == DC::kCineActionPackLocationInPlace))
	{
		return SID("s_cinematic-non-ap-state-wait");
	}
	else
	{
		return SID("s_cinematic-state-wait");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
const DC::ApEntryItemList* IAiCinematicController::GetApEntryListFor(const CinematicActionPack* pCineAp,
																	 const Process* pCharacter)
{
	if (const DC::CapAnimCollection* pCapAnimCollection = pCineAp->GetCapAnimCollection(pCharacter))
	{
		return pCapAnimCollection->m_entryList;
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::ApExitAnimList* AiCinematicController::GetApExitListFor(const CinematicActionPack* pCineAp,
																  const Process* pCharacter) const
{
	if (const DC::CapAnimCollection* pCapAnimCollection = pCineAp->GetCapAnimCollection(pCharacter))
	{
		return pCapAnimCollection->m_exitList;
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiCinematicController::StartUsing(bool first)
{
	CinematicActionPack* pCineAp = GetEnteredActionPack();
	if (!pCineAp)
	{
		return false;
	}

	m_usageTime = Seconds(pCineAp->GetUsageTime());

	NavCharacterAdapter pAdapter = GetCharacterAdapter();

	bool valid = false;

	if (const DC::CapAnimLoop* pLoop = pCineAp->GetLoop(pAdapter.ToGameObject()))
	{
		AiLogAnim(pAdapter.ToNavCharacter(),
				  "Using Cinematic AP (loop '%s')\n",
				  DevKitOnly_StringIdToString(pLoop->m_animName));
		StartLoop(pLoop, first);
		GoLooping();

		valid = true;
	}
	else
	{
		// don't go to exiting state before we've had a chance to play an exit animation!!
		GoReadyToExit();
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiCinematicController::IsInValidStateToStartLoop() const
{
	const NdGameObject* pChar = GetCharacterGameObject();
	const AnimControl* pAnimControl = pChar ? pChar->GetAnimControl() : nullptr;
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	if (!pBaseLayer)
		return false;

	if (m_abortRequested)
		return false;

	const StringId64 stateId = pBaseLayer->CurrentStateId();

	switch (stateId.GetValue())
	{
	case SID_VAL("s_cinematic-state-loop"):
	case SID_VAL("s_cinematic-state-wait"):
	case SID_VAL("s_cinematic-non-ap-state-enter"):
	case SID_VAL("s_cinematic-non-ap-state-loop"):
	case SID_VAL("s_cinematic-non-ap-state-wait"):
		return true;

	case SID_VAL("s_cinematic-state-exit"):
	case SID_VAL("s_cinematic-state-interrupt"):
	case SID_VAL("s_cinematic-non-ap-state-exit"):
	case SID_VAL("s_cinematic-non-ap-state-interrupt"):
		return false;

	case SID_VAL("s_cinematic-state-enter"):
	default:
		return IsEnterComplete();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
IAiCinematicController* CreateAiCinematicController()
{
	return NDI_NEW AiCinematicController;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class AiCinematicController::None : public AiCinematicController::BaseState
{
	virtual float GetNavMeshAdjustBlendFactor() const override { return 1.0f; }
};

FSM_STATE_REGISTER(AiCinematicController, None, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
class AiCinematicController::Idle : public AiCinematicController::BaseState
{
};

FSM_STATE_REGISTER(AiCinematicController, Idle, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
// State WaitingToPlayInPlace
/// --------------------------------------------------------------------------------------------------------------- ///

class AiCinematicController::WaitingToPlayInPlace : public AiCinematicController::BaseState
{
public:
	virtual void UpdateStatus() override
	{
		AiCinematicController& self = Self();
		NdGameObject* pCharacter = self.GetCharacterGameObject();
		NavCharacter* pNavChar = self.GetCharacter();

		// Wait for the NavCharacter to stop
		if (!pNavChar
		||  (pNavChar->GetNavStatus() == NavCommand::kStatusCommandSucceeded)
		||	(pNavChar->GetNavStatus() == NavCommand::kStatusAwaitingCommands))
		{
			if (pNavChar)
				AiLogAnim(pNavChar, "Playing In-Place Cinematic AP\n");

			self.StartUsing(true);
		}
	}
};

FSM_STATE_REGISTER(AiCinematicController, WaitingToPlayInPlace, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
// State Entering
/// --------------------------------------------------------------------------------------------------------------- ///

class AiCinematicController::Entering : public AiCinematicController::BaseState
{
public:

	virtual void RequestAnimations() override
	{
		AiCinematicController& self = Self();
		NdGameObject* pCharacter = self.GetCharacterGameObject();
		AnimControl* pAnimControl = pCharacter->GetAnimControl();
		DC::AnimNavCharInfo* pInfo = pAnimControl->Info<DC::AnimNavCharInfo>();

		static float kCapSpeedFilter = 0.5f;
		pInfo->m_speedFactor = AI::TrackSpeedFactor(pCharacter, pInfo->m_speedFactor, 1.0f, kCapSpeedFilter);
	}

	virtual void UpdateStatus() override
	{
		AiCinematicController& self = Self();
		CinematicActionPack* pCineAp = self.GetEnteredActionPack();
		NavCharacterAdapter pAdapter = self.GetCharacterAdapter();
		NdGameObject* pCharacter = self.GetCharacterGameObject();
		AnimControl* pAnimControl = pCharacter->GetAnimControl();

		if (!pCineAp || !pCineAp->IsEnabled())
		{
			return;
		}

		const DC::CapAnimCollection* pCapAnimCollection = pCineAp->GetCapAnimCollection(pCharacter);

		if (pCapAnimCollection->m_defaultLoopBlendIn)
		{
			if (const AnimStateInstance* pEnterInstance = self.m_ownerAnimAction.GetTransitionDestInstance(pAnimControl))
			{
				const float enterDuration = pEnterInstance->GetDuration();
				const float enterPhase = pEnterInstance->Phase();
				const float remDuration = Limit01(1.0f - enterPhase) * enterDuration;

				const float loopCrossFadeTime = Max(pCapAnimCollection->m_defaultLoopBlendIn->m_animFadeTime,
													pCapAnimCollection->m_defaultLoopBlendIn->m_motionFadeTime);

				if (loopCrossFadeTime >= remDuration)
				{
					self.m_ownerAnimAction.Reset();
					self.m_propAnimAction.Reset();
				}
			}
		}

		if (self.m_ownerAnimAction.IsDone())
		{
			if (!self.m_auto)
			{
				self.GoIdle();
			}
			else if (pCineAp->IsMultiCap())
			{
				self.GoWaitingForMultiCapSynchronousPlay();
			}
			else
			{
				self.StartUsing(true);
			}
		}
	}
};

FSM_STATE_REGISTER(AiCinematicController, Entering, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
// State WaitingForMultiCapSynchronousPlay
/// --------------------------------------------------------------------------------------------------------------- ///

class AiCinematicController::WaitingForMultiCapSynchronousPlay : public AiCinematicController::BaseState
{
public:
	virtual void OnEnter() override
	{
		AiCinematicController& self = Self();
		CinematicActionPack* pCineAp = self.GetEnteredActionPack();
		NavCharacterAdapter pAdapter = self.GetCharacterAdapter();
		NdGameObject* pCharacter = self.GetCharacterGameObject();

		if (!pCineAp || !pCineAp->IsEnabled())
		{
			return;
		}

		StringId64 waitPropAnimId = INVALID_STRING_ID_64;
		const StringId64 waitAnimId = pCineAp->GetWaitId(pCharacter, waitPropAnimId);

		self.StartWait(waitAnimId, waitPropAnimId);

		pCineAp->SignalWaiting(pCharacter);
	}

	virtual void UpdateStatus() override
	{
		AiCinematicController& self = Self();
		CinematicActionPack* pCineAp = self.GetEnteredActionPack();
		NavCharacterAdapter pAdapter = self.GetCharacterAdapter();
		NdGameObject* pCharacter = self.GetCharacterGameObject();

		if (!pCineAp || !pCineAp->IsEnabled())
		{
			return;
		}

		if (pCineAp->AreAllNpcsWaiting())
		{
			self.StartUsing(true);
		}
	}
};

FSM_STATE_REGISTER(AiCinematicController, WaitingForMultiCapSynchronousPlay, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
// State Looping
/// --------------------------------------------------------------------------------------------------------------- ///

class AiCinematicController::Looping : public AiCinematicController::BaseState
{
public:

	virtual void UpdateStatus() override
	{
		AiCinematicController& self = Self();
		CinematicActionPack* pCineAp = self.GetEnteredActionPack();
		NavCharacterAdapter pAdapter = self.GetCharacterAdapter();
		NdGameObject* pCharacter = self.GetCharacterGameObject();

		if (!pCineAp || !pCineAp->IsEnabled())
		{
			//self.SetFailed(SID("Cap not enabled"));
			return;
		}

		bool shouldExit = (pCineAp == nullptr);
		const bool loopComplete = self.IsLoopComplete();

		if (pCineAp)
		{
			const DC::CapAnimCollection* pCapAnimCollection = pCineAp->GetCapAnimCollection(pCharacter);

			const TimeFrame stateTimePassed = GetStateTimePassed();
			const bool hasUsageTime = self.m_usageTime > Seconds(0.0f);
			const bool infiniteLoops = pCineAp->DoInfiniteLoops();

			if (hasUsageTime && (stateTimePassed > self.m_usageTime) && !infiniteLoops)
			{
				shouldExit = true;
			}
			else if (loopComplete && pCineAp->PlayOneLoop())
			{
				shouldExit = true;
			}
		}

		if (shouldExit)
		{
			// don't go to exiting state before we've had a chance to play an exit animation!!
			self.GoReadyToExit();
		}
		else if (loopComplete)
		{
			g_ssMgr.BroadcastEvent(SID("loop-complete"), pCharacter->GetUserId(), pCineAp->GetSpawnerId());

			const NdAnimControllerConfig* pConfig = pAdapter->GetAnimControllerConfig();
			AI_ASSERT(pConfig);
			self.StartLoop(pCineAp->GetLoop(pCharacter), false);
		}
	}
};

FSM_STATE_REGISTER(AiCinematicController, Looping, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
// State Ready to Exit
/// --------------------------------------------------------------------------------------------------------------- ///

class AiCinematicController::ReadyToExit : public AiCinematicController::BaseState
{
};

FSM_STATE_REGISTER(AiCinematicController, ReadyToExit, kPriorityMedium);


/// --------------------------------------------------------------------------------------------------------------- ///
// State Exiting
/// --------------------------------------------------------------------------------------------------------------- ///

class AiCinematicController::Exiting : public AiCinematicController::BaseState
{
public:
	virtual void OnEnter() override
	{
		m_navMeshAdjustEnabled = false;
	}

	virtual void UpdateStatus() override
	{
		AiCinematicController& self = Self();
		if (!self.IsBusy())
		{
			self.GoNone();
		}

		if (!m_navMeshAdjustEnabled)
		{
			NdGameObject* pCharacter = self.GetCharacterGameObject();
			NavCharacterAdapter pNavChar = self.GetCharacterAdapter();
			const SimpleNavControl* pNavControl = pNavChar->GetNavControl();
			const Point charPosPs = pCharacter->GetTranslationPs();

			if (!pNavControl->IsPointStaticallyBlockedPs(charPosPs))
			{
				m_navMeshAdjustEnabled = true;
			}
		}
	}

	virtual float GetNavMeshAdjustBlendFactor() const override { return m_navMeshAdjustEnabled ? 1.0f : 0.0f; }

	bool m_navMeshAdjustEnabled;
};

FSM_STATE_REGISTER(AiCinematicController, Exiting, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AiCinematicController::GetMaxStateAllocSize()
{
	return Max(Max(Max(Max(sizeof(Idle),
							sizeof(Entering)),
							sizeof(Looping)),
							sizeof(WaitingForMultiCapSynchronousPlay)),
							sizeof(Exiting));
}
