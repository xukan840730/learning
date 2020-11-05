/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/search-controller.h"

#include "corelib/util/random.h"

#include "ndlib/anim/anim-action.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-layer.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/util/common.h"

#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/ai/component/nd-ai-anim-config.h"
#include "gamelib/gameplay/ap-entry-util.h"
#include "gamelib/gameplay/ap-exit-util.h"
#include "gamelib/gameplay/nav/action-pack-entry-def.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/pullout-action-pack.h"
#include "gamelib/region/region-manager.h"

#include "ailib/nav/nav-ai-util.h"

#include "game/ai/agent/npc.h"
#include "game/ai/base/ai-game-debug.h"
#include "game/ai/base/ai-game-util.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/controller/locomotion-controller.h"
#include "game/ai/coordinator/encounter-coordinator.h"
#include "game/ai/look-aim/look-aim-basic.h"
#include "game/scriptx/h/anim-npc-info.h"
#include "game/scriptx/h/npc-corner-check-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static const float kEnterExitCrossFadeTime = 0.5f;
static CONST_EXPR StringId64 kCornerCheckModuleId = SID("npc-corner-check");

enum ResolveFlags
{
	kLeftSide = 0x1,
	kDefault = 0x2,
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::Init(NavCharacter* pNavChar, const NavControl* pNavControl)
{
	ParentClass::Init(pNavChar, pNavControl);

	const NdAnimControllerConfig* pConfig = pNavChar->GetAnimControllerConfig();

	GetStateMachine().Init(this, FILE_LINE_FUNC);

	m_ledgeChecks.AllocateCache(16);
	m_grassChecks.AllocateCache(16);

	m_lastCornerCheckTime = TimeFrameNegInfinity();
	m_lastProneCheckTime = TimeFrameNegInfinity();

	m_cornerCheckDcNavHandoffParamsValid = false;
	m_enteredMotionType = kMotionTypeMax;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::Reset()
{
	ParentClass::Reset();

	if (BaseState* pState = GetState())
	{
		pState->OnReset();
	}

	m_pathPs.Clear();

	m_enteredMotionType = kMotionTypeMax;

	GoInactive();
	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::Interrupt()
{
	if (BaseState* pState = GetState())
	{
		pState->OnInterrupt();
	}

	m_pathPs.Clear();

	GoInactive();
	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);
	m_ledgeChecks.Relocate(deltaPos, lowerBound, upperBound);
	m_grassChecks.Relocate(deltaPos, lowerBound, upperBound);
	GetStateMachine().Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::ConfigureCharacter(Demeanor demeanor,
											const DC::NpcDemeanorDef* pDemeanorDef,
											const NdAiAnimationConfig* pAnimConfig)
{
	const NavCharacter* pNavChar = GetCharacter();
	const NdAnimControllerConfig* pConfig = pNavChar->GetAnimControllerConfig();

	m_ledgeChecks.RemoveTable(pNavChar, SID("ledge-checks"));
	m_ledgeChecks.AddTable(pNavChar, SID("ledge-checks"), pConfig->m_search.m_ledgeCheckId);
	m_ledgeChecks.RebuildCache(pNavChar);

	m_grassChecks.RemoveTable(pNavChar, SID("grass-checks"));
	m_grassChecks.AddTable(pNavChar, SID("grass-checks"), pConfig->m_search.m_grassCheckId);
	m_grassChecks.RebuildCache(pNavChar);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::CornerCheckAnims* AiSearchController::GetCornerCheckAnims(const SearchActionPack* pSearchAp, bool leftSide) const
{
	if (!pSearchAp)
		return nullptr;

	const Npc* pNpc = Npc::FromProcess(GetCharacter());
	const AnimControllerConfig* pConfig = static_cast<const AnimControllerConfig*>(pNpc->GetAnimControllerConfig());

	const StringId64 setId = pConfig->m_search.m_cornerCheckId;

	if (setId == INVALID_STRING_ID_64)
		return nullptr;

	const DC::CornerCheckAnimCollection* pDcCollection;
	pDcCollection = ScriptManager::LookupInModule<DC::CornerCheckAnimCollection>(setId, kCornerCheckModuleId, nullptr);

	StringId64 animSetId = INVALID_STRING_ID_64;

	switch (pSearchAp->GetMode())
	{
	case SearchActionPack::Mode::kCornerCheck:
		animSetId = leftSide ? pDcCollection->m_leftTense : pDcCollection->m_rightTense;
		break;
	case SearchActionPack::Mode::kDoorCheck:
		animSetId = leftSide ? pDcCollection->m_leftDoor : pDcCollection->m_rightDoor;
		break;
	case SearchActionPack::Mode::kPullout:
		animSetId = leftSide ? pDcCollection->m_prone : INVALID_STRING_ID_64;
		break;
	}

	return ScriptManager::LookupInModule<DC::CornerCheckAnims>(animSetId, kCornerCheckModuleId, nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::MakeExitCharacterState(const ActionPack* pActionPack,
												const IPathWaypoints* pPathPs,
												ApExit::CharacterState* pCsOut) const
{
	ParentClass::MakeExitCharacterState(pActionPack, pPathPs, pCsOut);

	pCsOut->m_motionType = m_enteredMotionType;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::ConfigureCornerCheckNavigationHandoff()
{
	NavCharacter* pNavChar	  = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	AiLogAnim(pNavChar, "AiSearchController::ConfigureCornerCheckNavigationHandoff()\n");

	NavAnimHandoffDesc handoff;
	
	if (m_animAction.IsValid())
	{
		handoff.SetStateChangeRequestId(m_animAction.GetStateChangeRequestId(), INVALID_STRING_ID_64);
	}
	else
	{
		const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

		if (!pBaseLayer->AreTransitionsPending())
		{
			const AnimStateInstance* pAnimStateInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;
			handoff.SetAnimStateInstance(pAnimStateInstance);
		}
	}

	if (m_cornerCheckDcNavHandoffParamsValid)
	{
		handoff.ConfigureFromDc(&m_cornerCheckDcNavHandoffParams);
	}

	pNavChar->ConfigureNavigationHandOff(handoff, FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::EndCornerCheck()
{
	AiLogAnim(GetCharacter(), "AiSearchController::EndCornerCheck\n");

	NavCharacter* pNavChar	  = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	ConfigureCornerCheckNavigationHandoff();

	m_hCorner = nullptr;
	m_animAction.Reset();
	m_cornerCheckDcNavHandoffParamsValid = false;

	m_pathPs.Clear();

	GoInactive();
	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::ApEntryItemList* AiSearchController::GetCornerCheckEntries(const SearchActionPack* pSearchAp,
																	 bool leftSide) const
{
	const DC::CornerCheckAnims* pDcAnims = GetCornerCheckAnims(pSearchAp, leftSide);

	if (!pDcAnims)
		return nullptr;

	const StringId64 moduleId = kCornerCheckModuleId;
	const StringId64 setId = pDcAnims->m_entrySet;

	return ScriptManager::LookupInModule<DC::ApEntryItemList>(setId, moduleId, nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::ApExitAnimList* AiSearchController::GetCornerCheckExits(const SearchActionPack* pSearchAp,
																  bool leftSide) const
{
	const DC::CornerCheckAnims* pDcAnims = GetCornerCheckAnims(pSearchAp, leftSide);

	if (!pDcAnims)
		return nullptr;

	const StringId64 moduleId = kCornerCheckModuleId;
	const StringId64 setId = pDcAnims->m_exitSet;

	return ScriptManager::LookupInModule<DC::ApExitAnimList>(setId, moduleId, nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::MakeEntryCharacterState(const ActionPackResolveInput& input,
												 const ActionPack* pActionPack,
												 ApEntry::CharacterState* pCsOut) const
{
	const NavCharacter* pNavChar = GetCharacter();

	if (!pNavChar || !pCsOut)
		return;

	ParentClass::MakeEntryCharacterState(input, pActionPack, pCsOut);

	ApEntry::CharacterState& cs = *pCsOut;

	// more potential config variables here
	const NdAnimControllerConfig* pConfig = pNavChar->GetAnimControllerConfig();

	cs.m_rangedEntryRadius	   = 3.0f;
	cs.m_maxEntryErrorAngleDeg = 15.0f;
	cs.m_maxFacingDiffAngleDeg = 30.0f;
	cs.m_testClearMotionRadial = true;

	const SearchActionPack* pSearchAp = SearchActionPack::FromActionPack(pActionPack);
	if (pSearchAp && !pSearchAp->IsPullout())
	{
		const NavControl* pNavControl = pNavChar->GetNavControl();
		const float offset = pNavControl ? pNavControl->GetEffectivePathFindRadius() : 0.0f;
		const Point apNavPosWs = pActionPack->GetDefaultEntryPointWs(offset);
		const Locator defApRefWs = pActionPack->GetBoundFrame().GetLocatorWs();

		cs.m_apRotOriginLs = defApRefWs.UntransformPoint(apNavPosWs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::ResolveEntry(const ActionPackResolveInput& input,
									  const ActionPack* pActionPack,
									  ActionPackEntryDef* pDefOut) const
{
	PROFILE_ACCUM(SearchCon_ResolveEntry);
	PROFILE(AI, SearchCon_ResolveEntry);

	if (!pActionPack)
		return false;

	AI_ASSERT(pActionPack->GetType() == ActionPack::kSearchActionPack);

	const SearchActionPack* pSearchAp = SearchActionPack::FromActionPack(pActionPack);
	if (!pSearchAp)
		return false;

	if (!pDefOut)
		return false;

	const BoundFrame& defaultApRef = pActionPack->GetBoundFrame();

	ApEntry::CharacterState cs;
	MakeEntryCharacterState(input, pActionPack, &cs);

	const U32F kMaxEntries = 32;
	ApEntry::AvailableEntry entries[kMaxEntries];

	U32F numEntries = 0;
	bool usedLeftSide = false;

	for (int iSide = 0; iSide < 2; ++iSide)
	{
		const DC::ApEntryItemList* pEntryList = GetCornerCheckEntries(pSearchAp, iSide == 0);

		if (!pEntryList)
		{
			numEntries = 0;
			continue;
		}

		numEntries = ApEntry::ResolveEntry(cs, pEntryList, defaultApRef, entries, kMaxEntries);

		if (numEntries)
		{
			usedLeftSide = iSide == 0;
			break;
		}
	}

	if (numEntries == 0)
	{
		return false;
	}

	const Npc* pNpc = Npc::FromProcess(GetCharacter());

	const Locator& alignPs = pNpc->GetLocatorPs();
	//const U32F iSelectedEntry = ApEntry::EntryWithLeastApproachRotation(entries, numEntries, alignPs);
	const U32F iSelectedEntry = ApEntry::EntryWithLeastRotation(entries, numEntries, defaultApRef);

	if (iSelectedEntry >= numEntries)
		return false;

	const ApEntry::AvailableEntry& bestEntry = entries[iSelectedEntry];

	pDefOut->m_phase			   = bestEntry.m_phase;
	pDefOut->m_apReference		   = bestEntry.m_rotatedApRef;
	pDefOut->m_sbApReference	   = defaultApRef;
	pDefOut->m_entryAnimId		   = bestEntry.m_resolvedAnimId;
	pDefOut->m_pDcDef			   = bestEntry.m_pDcDef;
	pDefOut->m_stopBeforeEntry	   = (bestEntry.m_pDcDef->m_flags & DC::kApEntryItemFlagsForceStop);
	pDefOut->m_preferredMotionType = pNpc->GetConfiguration().m_moveToCoverMotionType;
	pDefOut->m_entryVelocityPs	   = bestEntry.m_entryVelocityPs;
	pDefOut->m_mirror			   = bestEntry.m_playMirrored;

	if (usedLeftSide)
	{
		pDefOut->m_controllerData |= kLeftSide;
	}

	const Locator entryLocPs = bestEntry.m_entryAlignPs;
	const Locator entryLocWs = bestEntry.m_rotatedApRef.GetParentSpace().TransformLocator(entryLocPs);

	const Locator& parentSpace = pNpc->GetParentSpace();
	const NavLocation apNavLoc = pActionPack->GetRegisteredNavLocation();

	if (!ApEntry::ConstructEntryNavLocation(cs, apNavLoc, bestEntry, &pDefOut->m_entryNavLoc))
	{
		return false;
	}

	pDefOut->m_entryRotPs  = parentSpace.UntransformLocator(entryLocWs).Rot();
	pDefOut->m_refreshTime = pNpc->GetCurTime();
	pDefOut->m_hResolvedForAp = pActionPack;
	pDefOut->m_autoExitAfterEnter = true;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::ResolveLastChanceEntry(const ActionPackResolveInput& input,
												const ActionPack* pActionPack,
												ActionPackEntryDef* pDefOut) const
{
	const NavCharacter* pNavChar = GetCharacter();
	if (!pNavChar)
		return false;

	const SearchActionPack* pSearchAp = SearchActionPack::FromActionPack(pActionPack);

	*pDefOut = ActionPackEntryDef();

	const NavControl* pNavControl = pNavChar->GetNavControl();
	const float offset = pNavControl ? pNavControl->GetEffectivePathFindRadius() : 0.0f;

	const Point apNavPosWs = pSearchAp->GetDefaultEntryPointWs(offset);

	const NavLocation lastChanceGoalLoc = pNavChar->AsReachableNavLocationWs(apNavPosWs, NavLocation::Type::kNavPoly);

	pDefOut->m_entryNavLoc = lastChanceGoalLoc;

	pDefOut->m_refreshTime		  = pNavChar->GetCurTime();
	pDefOut->m_hResolvedForAp	  = pActionPack;
	pDefOut->m_autoExitAfterEnter = true;
	pDefOut->m_entryRotPs		  = pNavChar->GetRotationPs();
	//pDefOut->m_stopBeforeEntry  = true;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::ResolveDefaultEntry(const ActionPackResolveInput& input,
											 const ActionPack* pActionPack,
											 ActionPackEntryDef* pDefOut) const
{
	PROFILE_ACCUM(SearchCon_ResolveDefEntry);
	PROFILE(AI, SearchCon_ResolveDefEntry);

	if (!pActionPack)
	{
		return ResolveLastChanceEntry(input, pActionPack, pDefOut);
	}

	AI_ASSERT(pActionPack->GetType() == ActionPack::kSearchActionPack);

	if (!pDefOut)
	{
		return ResolveLastChanceEntry(input, pActionPack, pDefOut);
	}

	const SearchActionPack* pSearchAp = SearchActionPack::FromActionPack(pActionPack);
	if (!pSearchAp)
	{
		return ResolveLastChanceEntry(input, pActionPack, pDefOut);
	}

	const BoundFrame& defaultApRef = pSearchAp->GetBoundFrame();

	ApEntry::CharacterState cs;
	MakeEntryCharacterState(input, pActionPack, &cs);

	const U32F kMaxEntries = 32;
	ApEntry::AvailableEntry entries[kMaxEntries];

	U32F numEntries = 0;
	bool usedLeftSide = false;

	for (int iSide = 0; iSide < 2; ++iSide)
	{
		const DC::ApEntryItemList* pEntryList = GetCornerCheckEntries(pSearchAp, iSide == 0);

		if (!pEntryList)
		{
			numEntries = 0;
			continue;
		}

		numEntries = ApEntry::ResolveDefaultEntry(cs, pEntryList, defaultApRef, entries, kMaxEntries);

		if (numEntries)
		{
			usedLeftSide = iSide == 0;
			break;
		}
	}

	if (numEntries == 0)
	{
		return ResolveLastChanceEntry(input, pActionPack, pDefOut);
	}

	const Npc* pNpc = Npc::FromProcess(GetCharacter());

	const Locator& alignPs = pNpc->GetLocatorPs();
	const U32F iSelectedEntry = ApEntry::EntryWithLeastApproachRotation(entries, numEntries, alignPs);

	if (iSelectedEntry >= numEntries)
	{
		return ResolveLastChanceEntry(input, pActionPack, pDefOut);
	}
	
	const ApEntry::AvailableEntry& bestEntry = entries[iSelectedEntry];

	pDefOut->m_phase		 = bestEntry.m_phase;
	pDefOut->m_apReference	 = bestEntry.m_rotatedApRef;
	pDefOut->m_sbApReference = defaultApRef;
	pDefOut->m_entryAnimId	 = bestEntry.m_resolvedAnimId;
	pDefOut->m_pDcDef		 = bestEntry.m_pDcDef;
	pDefOut->m_stopBeforeEntry	   = (bestEntry.m_pDcDef->m_flags & DC::kApEntryItemFlagsForceStop);
	pDefOut->m_preferredMotionType = pNpc->GetConfiguration().m_moveToCoverMotionType;
	pDefOut->m_entryVelocityPs	   = bestEntry.m_entryVelocityPs;
	pDefOut->m_mirror = bestEntry.m_playMirrored;

	pDefOut->m_controllerData = kDefault;

	if (usedLeftSide)
	{
		pDefOut->m_controllerData |= kLeftSide;
	}

	const Locator entryLocPs = bestEntry.m_entryAlignPs;
	const Locator entryLocWs = bestEntry.m_rotatedApRef.GetParentSpace().TransformLocator(entryLocPs);

	const Locator& parentSpace = pNpc->GetParentSpace();
	const NavLocation apNavLoc = pActionPack->GetRegisteredNavLocation();

	if (!ApEntry::ConstructEntryNavLocation(cs, apNavLoc, bestEntry, &pDefOut->m_entryNavLoc))
	{
		return ResolveLastChanceEntry(input, pActionPack, pDefOut);
	}

	pDefOut->m_entryRotPs		  = parentSpace.UntransformLocator(entryLocWs).Rot();
	pDefOut->m_refreshTime		  = pNpc->GetCurTime();
	pDefOut->m_hResolvedForAp	  = pActionPack;
	pDefOut->m_autoExitAfterEnter = true;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::UpdateEntry(const ActionPackResolveInput& input,
									 const ActionPack* pActionPack,
									 ActionPackEntryDef* pDefOut) const
{
	PROFILE(AI, SearchCon_UpdateEntry);

	pDefOut->m_hResolvedForAp = nullptr;

	if (!pActionPack)
		return false;

	const NavCharacter* pChar = GetCharacter();
	const Npc* pNpc = (const Npc*)pChar;
	const Locator& parentSpace = pChar->GetParentSpace();

	AI_ASSERT(pDefOut);
	AI_ASSERT(pActionPack->GetType() == ActionPack::kSearchActionPack);

	const BoundFrame defaultApRef = pDefOut->m_sbApReference;

	ApEntry::CharacterState cs;
	MakeEntryCharacterState(input, pActionPack, &cs);

	const DC::ApEntryItem* pEntry = pDefOut->m_pDcDef;

	if (!pEntry)
		return false;

	const bool isDefaultEntry = (pEntry->m_flags & DC::kApEntryItemFlagsDefaultEntry) != 0;
	if (isDefaultEntry && !pNpc->IsBusy() && (Dist(pNpc->GetTranslationPs(), pDefOut->m_entryNavLoc.GetPosPs()) > 0.5f))
	{
		return ResolveDefaultEntry(input, pActionPack, pDefOut);
	}

	ApEntry::AvailableEntry updatedEntry;
	if (!ApEntry::ConstructEntry(cs, pEntry, defaultApRef, &updatedEntry))
	{
		return false;
	}

	if (pDefOut->m_entryAnimId != updatedEntry.m_resolvedAnimId)
	{
		return false;
	}

	const ApEntry::GatherMode mode = isDefaultEntry ? ApEntry::GatherMode::kDefault : ApEntry::GatherMode::kResolved;
	ApEntry::FlagInvalidEntries(cs, mode, defaultApRef, &updatedEntry, 1);

	if (updatedEntry.m_errorFlags.m_invalidEntryAngle)
	{
		return false;
	}

	if (updatedEntry.m_errorFlags.m_invalidFacingDir)
	{
		return false;
	}

	if (updatedEntry.m_errorFlags.m_noClearLineOfMotion)
	{
		return false;
	}

	if (updatedEntry.m_errorFlags.m_all)
	{
		return false;
	}

	if (!ApEntry::ConstructEntryNavLocation(cs,
											pActionPack->GetRegisteredNavLocation(),
											updatedEntry,
											&pDefOut->m_entryNavLoc))
	{
		return false;
	}

	const bool forceStop = (updatedEntry.m_pDcDef->m_flags & DC::kApEntryItemFlagsForceStop);

	Locator entryLocPs = updatedEntry.m_entryAlignPs;
	entryLocPs.SetPos(PointFromXzAndY(entryLocPs.Pos(), pNpc->GetTranslationPs()));

	pDefOut->m_entryRotPs  = entryLocPs.Rot();
	pDefOut->m_apReference = updatedEntry.m_rotatedApRef;
	pDefOut->m_phase	   = updatedEntry.m_phase;
	pDefOut->m_mirror	   = updatedEntry.m_playMirrored;
	pDefOut->m_entryVelocityPs = updatedEntry.m_entryVelocityPs;
	pDefOut->m_refreshTime	   = pChar->GetCurTime();
	pDefOut->m_stopBeforeEntry = forceStop;
	pDefOut->m_hResolvedForAp  = pActionPack;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::Enter(const ActionPackResolveInput& input,
							   ActionPack* pActionPack,
							   const ActionPackEntryDef& entryDef)
{
	Npc* pNpc = Npc::FromProcess(GetCharacter());

	AiLogAnim(pNpc, "AiSearchController::Enter: ");
	LogActionPack(pNpc, kNpcLogChannelAnim, pActionPack);
	Log(pNpc, kNpcLogChannelAnim, "\n");

	SearchActionPack* pSearchAp = SearchActionPack::FromActionPack(pActionPack);
	ANIM_ASSERT(pSearchAp);

	ParentClass::Enter(input, pActionPack, entryDef);

	m_hCorner		= pActionPack;
	m_hLatestCorner = pActionPack;
	m_entryDef		= entryDef;

	const StringId64 entryAnimId = entryDef.m_entryAnimId;

	const bool leftSide = (entryDef.m_controllerData & kLeftSide) != 0;
	const bool defaultEntry = (entryDef.m_controllerData & kDefault) != 0;

	AnimControl* pAnimControl = pNpc->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	if ((entryAnimId == INVALID_STRING_ID_64) || (defaultEntry && !IsInProneCheck()))
	{
		const AnimStateInstance* pCurInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;

		if (pCurInstance)
		{
			NavAnimHandoffDesc handoff;
			handoff.SetAnimStateInstance(pCurInstance);
			if (pNpc->IsMoving())
			{
				handoff.m_motionType = pNpc->GetCurrentMotionType();
			}

			pNpc->ConfigureNavigationHandOff(handoff, FILE_LINE_FUNC);
		}

		GoInactive();
		GetStateMachine().TakeStateTransition();
		return;
	}

	if (!pAnimControl->LookupAnim(entryAnimId).ToArtItem())
		return;

	m_enteredMotionType = pNpc->GetRequestedMotionType();

	if (pBaseLayer)
	{
		pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);
	}

	AI::SetPluggableAnim(pNpc, entryAnimId);

	FadeToStateParams fadeParams;
	fadeParams.m_stateStartPhase = entryDef.m_phase;
	fadeParams.m_animFadeTime	 = 0.5f;
	fadeParams.m_motionFadeTime	 = 0.5f;
	fadeParams.m_blendType		 = DC::kAnimCurveTypeUniformS;
	fadeParams.m_apRef		= entryDef.m_apReference;
	fadeParams.m_apRefValid = true;

	fadeParams.ApplyBlendParams(entryDef.m_pDcDef ? entryDef.m_pDcDef->m_blendIn : nullptr);

	// a bit of rotational tolerance
	m_animAction.FadeToState(pAnimControl,
							 SID("s_corner-check-enter"),
							 fadeParams,
							 AnimAction::kFinishOnAnimEndEarly);

	if (const DC::SelfBlendParams* pSelfBlend = entryDef.GetSelfBlend())
	{
		m_animAction.SetSelfBlendParams(pSelfBlend, entryDef.m_sbApReference, pNpc);
	}

	const DC::CornerCheckAnims* pDcAnims = GetCornerCheckAnims(pSearchAp, leftSide);
	Locator endAlignPs = kIdentity;

	if (pDcAnims && pDcAnims->m_exitPathOriginLs)
	{
		const Point exitPathOriginLs = *pDcAnims->m_exitPathOriginLs;
		const Point exitPathOriginWs = pActionPack->GetLocatorWs().TransformPoint(exitPathOriginLs);
		m_exitPathOriginPs = pNpc->GetParentSpace().UntransformPoint(exitPathOriginWs);
	}
	else if (FindAlignFromApReference(pAnimControl,
									  entryDef.m_entryAnimId,
									  1.0f,
									  entryDef.m_sbApReference.GetLocatorPs(),
									  &endAlignPs,
									  entryDef.m_mirror))
	{
		m_exitPathOriginPs = endAlignPs.Pos();
	}
	else
	{
		m_exitPathOriginPs = pActionPack->GetDefaultEntryPointPs(0.5f);
	}

	m_goalLoc = pSearchAp->GetExitGoalLoc();

	GoEnterCornerCheck();
	GetStateMachine().TakeStateTransition();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::IsInCornerCheck() const
{
	const SearchActionPack* pAp = GetLatestCornerCheckAp();
	if (!pAp)
		return false;

	if (!pAp->IsCornerCheck())
	{
		return false;
	}

	return IsState(SID("EnterCornerCheck")) || IsState(SID("ExitCornerCheck"));
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::IsInProneCheck() const
{
	const SearchActionPack* pAp = GetLatestCornerCheckAp();
	if (!pAp)
		return false;

	if (!pAp->IsPullout())
		return false;

	return IsState(SID("EnterCornerCheck")) || IsState(SID("ExitCornerCheck"));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::UpdateStatus()
{
	if (BaseState* pState = GetState())
	{
		pState->UpdateStatus();
	}

	GetStateMachine().TakeStateTransition();

	const TimeFrame curTime = GetCharacter()->GetCurTime();

	if (IsInCornerCheck())
		m_lastCornerCheckTime = curTime;

	if (IsInProneCheck())
		m_lastProneCheckTime = curTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::DebugDraw(ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	ParentClass::DebugDraw(pPrinter);

	if (const BaseState* pState = GetState())
	{
		pState->DebugDraw(pPrinter);
	}

	if (g_aiGameOptions.m_cornerCheck.m_drawGoal)
	{
		if (const Region* pRegion = m_hPulloutRegion.GetTarget())
		{
			pRegion->DebugDraw();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::DebugDrawEntries(const ActionPackResolveInput& input, const ActionPack* pActionPack) const
{
	STRIP_IN_FINAL_BUILD;

	const SearchActionPack* pSearchAp = SearchActionPack::FromActionPack(pActionPack);
	if (!pSearchAp)
		return;

	const BoundFrame& defaultApRef = pActionPack->GetBoundFrame();

	if (g_aiGameOptions.m_cornerCheck.m_drawGoal)
	{
		if (!m_goalLoc.IsNull())
		{
			NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
			m_goalLoc.DebugDraw(kColorPurple);
		}
	}

	for (U32F iSide = 0; iSide < 2; ++iSide)
	{
		const DC::ApEntryItemList* pEntryList = GetCornerCheckEntries(pSearchAp, iSide == 0);
		if (!pEntryList)
			return;

		DebugDrawEntryAnims(input,
							pActionPack,
							defaultApRef,
							*pEntryList,
							m_entryDef.m_pDcDef,
							g_aiGameOptions.m_cornerCheck);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::DebugDrawExits(const ActionPackResolveInput& input,
										const ActionPack* pActionPack,
										const IPathWaypoints* pPathPs) const
{
	STRIP_IN_FINAL_BUILD;

	const SearchActionPack* pSearchAp = SearchActionPack::FromActionPack(pActionPack);
	if (!pSearchAp)
		return;

	if (const DC::ApExitAnimList* pExits = GetCornerCheckExits(pSearchAp, true))
	{
		const BoundFrame& apRef = pActionPack->GetBoundFrame();

		DebugDrawExitAnims(input,
						   pActionPack,
						   apRef,
						   pPathPs,
						   *pExits,
						   nullptr,
						   g_aiGameOptions.m_cornerCheck);
	}

	if (const DC::ApExitAnimList* pExits = GetCornerCheckExits(pSearchAp, false))
	{
		const BoundFrame& apRef = pActionPack->GetBoundFrame();

		DebugDrawExitAnims(input,
						   pActionPack,
						   apRef,
						   pPathPs,
						   *pExits,
						   nullptr,
						   g_aiGameOptions.m_cornerCheck);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32 AiSearchController::GetEnterAnimCount(const ActionPack* pActionPack) const
{
	const SearchActionPack* pSearchAp = SearchActionPack::FromActionPack(pActionPack);
	if (!pSearchAp)
		return 0;

	const DC::ApEntryItemList* pEntryListLeft = GetCornerCheckEntries(pSearchAp, true);
	const DC::ApEntryItemList* pEntryListRight = GetCornerCheckEntries(pSearchAp, false);

	U32F count = 0;
	count += pEntryListLeft ? pEntryListLeft->m_count : 0;
	count += pEntryListRight ? pEntryListRight->m_count : 0;

	return count;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F AiSearchController::GetExitAnimCount(const ActionPack* pActionPack) const
{
	const SearchActionPack* pSearchAp = SearchActionPack::FromActionPack(pActionPack);
	if (!pSearchAp)
		return 0;

	const DC::ApExitAnimList* pExitListLeft	 = GetCornerCheckExits(pSearchAp, true);
	const DC::ApExitAnimList* pExitListRight = GetCornerCheckExits(pSearchAp, false);

	U32F count = 0;
	count += pExitListLeft ? pExitListLeft->m_count : 0;
	count += pExitListRight ? pExitListRight->m_count : 0;

	return count;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::IsBusy() const
{
	if (const BaseState* pState = GetState())
	{
		return pState->IsBusy();
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::ShouldInterruptNavigation() const
{
	if (const BaseState* pState = GetState())
	{
		return pState->ShouldInterruptNavigation();
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U64 AiSearchController::CollectHitReactionStateFlags() const
{
	if (IsInProneCheck())
		return DC::kHitReactionStateMaskProneCheck;

	return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::SetGoalLoc(const NavLocation& navLoc)
{
	if (BaseState* pState = GetState())
	{
		pState->SetGoalLoc(navLoc);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
RegionHandle AiSearchController::GetPulloutRegionHandle() const
{
	return m_hPulloutRegion;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::TryIdlePerformance()
{
	const NavCharacter* pNavChar = GetCharacter();
	if (pNavChar->IsBusy())
		return false;

	if (pNavChar->IsClimbing())
		return false;

	if (BaseState* pState = GetState())
	{
		if (pState->TryIdlePerformance())
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::TryAcknowledgePerformance()
{
	const NavCharacter* pNavChar = GetCharacter();
	if (pNavChar->IsBusy())
		return false;

	if (pNavChar->IsClimbing())
		return false;

	if (BaseState* pState = GetState())
	{
		if (pState->TryAcknowledgePerformance())
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::TryStartPerformance()
{
	const Npc* pOwner = Npc::FromProcess(GetCharacter());

	const Player* pPlayer = GetPlayer();
	if (!pPlayer)
		return false;

	if (pOwner->IsClimbing())
		return false;

	const AiEncounterCoordinator* pCoord = pOwner->GetEncounterCoordinator();
	const AiCharacterGroup& aiGroup		 = pCoord->GetAiCharacterGroup();
	AiCharacterGroupIterator it	   = aiGroup.BeginMembers();
	AiCharacterGroupIterator itEnd = aiGroup.EndMembers();

	bool bestVisible	= false;
	F32 bestDist		= NDI_FLT_MAX;
	const Npc* pBestNpc = nullptr;
	const Point playerPosWs = pPlayer->GetTranslation();

	for (; it != itEnd; ++it)
	{
		const Npc* pNpc = it.GetMemberAs<Npc>();
		if (!pNpc || pNpc->IsDead())
			continue;

		const F32 dist	   = Dist(pNpc->GetTranslation(), playerPosWs);
		const bool visible = pNpc->CanBeSeen();

		if (((dist < bestDist) && (visible == bestVisible)) || (visible && !bestVisible))
		{
			pBestNpc	= pNpc;
			bestDist	= dist;
			bestVisible = visible;
		}
	}

	if (pBestNpc != pOwner)
		return false;

	if (BaseState* pState = GetState())
	{
		if (pState->TryStartPerformance())
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavLocation AiSearchController::GetGoalLoc() const
{
	return m_goalLoc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const PathWaypointsEx* AiSearchController::GetPathPs() const
{
	return &m_pathPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
//  STATES
/// --------------------------------------------------------------------------------------------------------------- ///

class AiSearchController::Inactive : public AiSearchController::BaseState
{
	virtual void OnEnter() override;
	virtual void OnExit() override;
	virtual bool TryIdlePerformance() override;
	virtual bool TryStartPerformance() override;
	virtual bool TryAcknowledgePerformance() override;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::Inactive::OnEnter()
{
	AiSearchController* pSelf = GetSelf();
	pSelf->m_pathPs.Clear();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::Inactive::OnExit()
{
	AiSearchController* pSelf = GetSelf();
	pSelf->m_pathPs.Clear();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::Inactive::TryIdlePerformance()
{
	AiSearchController* pSelf = GetSelf();
	NavCharacter* pNavChar	  = GetCharacter();

	const AnimControllerConfig* pConfig = static_cast<const AnimControllerConfig*>(pNavChar->GetAnimControllerConfig());
	const StringId64 setId = pConfig->m_search.m_waitSetId;

	const DC::SymbolArray* pWaitAnims = ScriptManager::Lookup<DC::SymbolArray>(setId, nullptr);
	if (!pWaitAnims)
		return false;

	if (pWaitAnims->m_count == 0)
		return false;

	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	if (AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer())
	{
		pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);
	}

	const U32F selectedAnim		   = RandomIntRange(0, pWaitAnims->m_count - 1);
	const BoundFrame& initialApRef = pNavChar->GetBoundFrame();

	AI::SetPluggableAnim(pNavChar, pWaitAnims->m_array[selectedAnim]);

	FadeToStateParams params;
	params.m_apRef = initialApRef;
	params.m_apRefValid = true;
	params.m_animFadeTime = 0.4f;
	params.m_motionFadeTime = 0.2f;
	pSelf->m_animAction.FadeToState(pAnimControl,
									SID("s_search-performance"),
									params,
									AnimAction::kFinishOnNonTransitionalStateReached);

	pSelf->GoWaitPerformance();
	pSelf->GetStateMachine().TakeStateTransition();

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::Inactive::TryStartPerformance()
{
	AiSearchController* pSelf = GetSelf();
	NavCharacter* pNavChar	  = GetCharacter();

	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	const BoundFrame& initialApRef = pNavChar->GetBoundFrame();

	const ArtItemAnim* pSearchAnim = pAnimControl->LookupAnim(SID("search-start")).ToArtItem();
	if (!pSearchAnim)
		return false;

	AI::SetPluggableAnim(pNavChar, pSearchAnim->GetNameId());

	if (AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer())
	{
		pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);
	}

	FadeToStateParams params;
	params.m_apRef = initialApRef;
	params.m_apRefValid = true;
	params.m_animFadeTime = 0.4f;
	params.m_motionFadeTime = 0.2f;

	pSelf->m_animAction.FadeToState(pAnimControl,
									SID("s_search-performance"),
									params,
									AnimAction::kFinishOnNonTransitionalStateReached);

	pSelf->GoWaitPerformance();
	pSelf->GetStateMachine().TakeStateTransition();

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::Inactive::TryAcknowledgePerformance()
{
	AiSearchController* pSelf = GetSelf();
	NavCharacter* pNavChar	  = GetCharacter();

	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	const BoundFrame& initialApRef = pNavChar->GetBoundFrame();

	const ArtItemAnim* pSearchAnim = pAnimControl->LookupAnim(SID("search-acknowledge-ally")).ToArtItem();
	if (!pSearchAnim)
		return false;

	if (AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer())
	{
		pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);
	}

	AI::SetPluggableAnim(pNavChar, pSearchAnim->GetNameId());

	FadeToStateParams params;
	params.m_apRef = initialApRef;
	params.m_apRefValid = true;
	params.m_animFadeTime = 0.4f;
	params.m_motionFadeTime = 0.2f;

	pSelf->m_animAction.FadeToState(pAnimControl,
									SID("s_search-performance"),
									params,
									AnimAction::kFinishOnNonTransitionalStateReached);

	pSelf->GoWaitPerformance();
	pSelf->GetStateMachine().TakeStateTransition();

	return true;
}

FSM_STATE_REGISTER(AiSearchController, Inactive, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
class AiSearchController::EnterCornerCheck : public AiSearchController::BaseState
{
public:
	virtual void OnEnter() override;
	virtual void OnExit() override;
	virtual bool IsBusy() const override;
	virtual void UpdateStatus() override;
	virtual void OnReset() override;
	virtual void OnInterrupt() override;
	virtual void SetGoalLoc(const NavLocation& navLoc) override;
	virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const override;

private:
	void DoSearchPathfind();
	bool TimeToExit() const;
	void StartExiting();
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::EnterCornerCheck::OnEnter()
{
	AiSearchController* pSelf = GetSelf();
	// pSelf->m_goalLoc = NavLocation();

	const SearchActionPack* pSearchAp = pSelf->m_hCorner.ToActionPack<SearchActionPack>();

	{
		Npc* pNpc = Npc::FromProcess(GetCharacter());

		const bool leftSide		 = (pSelf->m_entryDef.m_controllerData & kLeftSide) != 0;
		const Locator apLocWs	 = pSearchAp->GetLocatorWs();
		const Vector offsetDirWs = GetLocalX(apLocWs.GetRotation());
		const Point basePosWs	 = apLocWs.GetTranslation();
		const Point aimPosWs	 = basePosWs + (offsetDirWs * (leftSide ? -0.5f : 0.5f)) + Vector(0.f, 0.5f, 0.f);


		LookAimPointWsParams params(aimPosWs);
		pNpc->GetLookAim().SetLookAimMode(kLookAimPrioritySystem,
										  AiLookAimRequest(SID("LookAimPointWs"), &params),
										  FILE_LINE_FUNC);
	}

	pSelf->m_pathPs.Clear();
	pSelf->m_hPulloutRegion = nullptr;
	pSelf->m_cornerCheckDcNavHandoffParamsValid = false;

	if (pSearchAp && pSearchAp->IsPullout())
	{
		const Locator& apLocWs	   = pSearchAp->GetLocatorWs();
		const Region* pProneRegion = nullptr;

		const I32F numRegions = g_regionManager.GetRegionsByPositionAndKey(&pProneRegion,
																		   1,
																		   apLocWs.GetPosition(),
																		   1.0f,
																		   SID("prone-hiding-space"));

		if (numRegions > 0)
		{
			pSelf->m_hPulloutRegion = pProneRegion;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::EnterCornerCheck::OnExit()
{
	Npc* pNpc = Npc::FromProcess(GetCharacter());
	pNpc->ClearLookAimMode(kLookAimPrioritySystem, FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::EnterCornerCheck::IsBusy() const
{
	AiSearchController* pSelf = GetSelf();
	return !pSelf->m_animAction.IsDone();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::EnterCornerCheck::DoSearchPathfind()
{
	PROFILE_AUTO(Navigation);

	const Npc* pNpc = Npc::FromProcess(GetCharacter());
	const NavControl* pNavControl = pNpc->GetNavControl();

	if (!pNavControl)
		return;

	AiSearchController* pSelf = GetSelf();
	const ActionPack* pAp	  = pSelf->m_hCorner.ToActionPack();
	if (!pAp)
		return;

	NavLocation startLoc = pAp->GetRegisteredNavLocation();

	startLoc.UpdatePosPs(pSelf->m_exitPathOriginPs);

	const Locator& parentSpace = pAp->GetBoundFrame().GetParentSpace();
	const U32F traversalSkillMask = pNavControl->GetTraversalSkillMask();

	NavControl::PathFindOptions pfOptions;
	pfOptions.m_parentSpace = parentSpace;
	pfOptions.m_startLoc	= startLoc;
	pfOptions.m_goalLoc		= pSelf->m_goalLoc;
	pfOptions.m_traversalSkillMask = traversalSkillMask;
	// pfOptions.m_navBlockerFilterFunc = NsmPathFindBlockerFilter;
	// pfOptions.m_nbFilterFuncUserData = (uintptr_t)pNpc;

	NavControl::PathFindResults results;
	NavControl::PathFindJobHandle hNavPathJob = pNavControl->BeginPathFind(pNpc, pfOptions, FILE_LINE_FUNC);
	pNavControl->CollectPathFindResults(hNavPathJob, results);

	pSelf->m_pathPs = results.m_pathPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::EnterCornerCheck::UpdateStatus()
{
	Npc* pNpc = Npc::FromProcess(GetCharacter());
	AnimControl* pAnimControl = pNpc->GetAnimControl();
	AiSearchController* pSelf = GetSelf();

	pSelf->m_animAction.Update(pAnimControl);

	const DC::SelfBlendParams* pSelfBlend = pSelf->m_entryDef.GetSelfBlend();

	if (pSelf->m_animAction.WasTransitionTakenThisFrame() && (!pSelfBlend || pSelfBlend->m_phase < 0.0f))
	{
		if (const AnimStateInstance* pDestInstance = pSelf->m_animAction.GetTransitionDestInstance(pAnimControl))
		{
			const DC::SelfBlendParams sb = NavUtil::ConstructSelfBlendParams(pDestInstance);

			pSelf->m_animAction.SetSelfBlendParams(&sb, pSelf->m_entryDef.m_sbApReference, pNpc);
		}
	}

	// unexpected anim state or same-frame pending transition? (e.g. requested
	// by NPC melee controller or melee action controller)
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	if (pBaseLayer && (pBaseLayer->CurrentStateId() != SID("s_corner-check-enter")))
	{
		pSelf->EndCornerCheck();
		return;
	}

	if (!pSelf->m_goalLoc.IsNull() && !pSelf->m_pathPs.IsValid())
	{
		// TODO do pathfind
		DoSearchPathfind();
	}

	if (TimeToExit())
	{
		StartExiting();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::EnterCornerCheck::TimeToExit() const
{
	const Npc* pNpc = Npc::FromProcess(GetCharacter());
	const AnimControl* pAnimControl = pNpc->GetAnimControl();
	const AiSearchController* pSelf = GetSelf();

	if (pSelf->m_animAction.IsDone())
		return true;

	const AnimStateInstance* pDestInstance = pSelf->m_animAction.GetTransitionDestInstance(pAnimControl);
	const ArtItemAnim* pEnterAnim = pDestInstance ? pDestInstance->GetPhaseAnimArtItem().ToArtItem() : nullptr;
	if (!pEnterAnim)
	{
		return true;
	}

	const float curPhase = pDestInstance->Phase();
	const float remainingTime = Limit01(1.0f - curPhase) * GetDuration(pEnterAnim);

	if (remainingTime <= kEnterExitCrossFadeTime + NDI_FLT_EPSILON)
	{
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::EnterCornerCheck::StartExiting()
{
	AiLogAnim(GetCharacter(), "AiSearchController::EnterCornerCheck::StartExiting\n");

	Npc* pNpc = Npc::FromProcess(GetCharacter());
	AnimControl* pAnimControl = pNpc->GetAnimControl();
	AiSearchController* pSelf = GetSelf();

	const SearchActionPack* pSearchAp = pSelf->m_hCorner.ToActionPack<SearchActionPack>();

	const bool leftSide = (pSelf->m_entryDef.m_controllerData & kLeftSide) != 0;

	const DC::ApExitAnimList* pExits = pSelf->GetCornerCheckExits(pSearchAp, leftSide);
	if (!pExits)
	{
		pSelf->EndCornerCheck();
		return;
	}

	ApExit::CharacterState cs;
	pSelf->MakeExitCharacterState(pSearchAp, &pSelf->m_pathPs, &cs);

	ActionPackExitDef selectedExit;
	const bool foundExit = pSelf->ResolveExit(cs, pSelf->m_entryDef.m_sbApReference, pExits, &selectedExit);

	if (!foundExit)
	{
		pSelf->EndCornerCheck();
		return;
	}

	const DC::ApExitAnimDef* pExitPerf = selectedExit.m_pDcDef;

	if (!pExitPerf)
	{
		pSelf->EndCornerCheck();
		return;
	}

	AI::SetPluggableAnim(pNpc, pExitPerf->m_animName, selectedExit.m_mirror);

	StringId64 exitStateId = SID("s_corner-check-idle-exit");

	if (selectedExit.m_pDcDef && selectedExit.m_pDcDef->m_validMotions)
	{
		exitStateId = SID("s_corner-check-moving-exit");
	}

	FadeToStateParams fadeParams;

	fadeParams.m_apRefValid	   = true;
	fadeParams.m_apRef		   = selectedExit.m_apReference;
	fadeParams.m_customApRefId = selectedExit.m_apRefId;
	fadeParams.m_animFadeTime  = kEnterExitCrossFadeTime;
	fadeParams.m_motionFadeTime = kEnterExitCrossFadeTime;
	fadeParams.m_blendType		= DC::kAnimCurveTypeUniformS;

	pSelf->m_animAction.FadeToState(pAnimControl,
									exitStateId,
									fadeParams,
									AnimAction::kFinishOnNonTransitionalStateReached);

	SelfBlendAction::Params sbParams;
	sbParams.m_apChannelId = selectedExit.m_apRefId;
	sbParams.m_destAp	   = selectedExit.m_sbApReference;

	if (pExitPerf->m_selfBlend)
	{
		sbParams.m_blendParams = *pExitPerf->m_selfBlend;
	}
	else
	{
		sbParams.m_blendParams.m_phase = 0.0f;
		sbParams.m_blendParams.m_time  = 1.0f;
		sbParams.m_blendParams.m_curve = DC::kAnimCurveTypeUniformS;
	}

	DC::AnimNavCharInfo* pInfo = pAnimControl->Info<DC::AnimNavCharInfo>();
	if (pExitPerf->m_validMotions)
	{
		pInfo->m_apExitTransitions = SID("ap-exit^move");
	}
	else
	{
		pInfo->m_apExitTransitions = SID("ap-exit^idle");
	}

	pSelf->m_animAction.ClearSelfBlendParams();
	pSelf->m_animAction.ConfigureSelfBlend(pNpc, sbParams);

	pSelf->m_cornerCheckExitPhase = pExitPerf->m_exitPhase;

	if (pExitPerf->m_navAnimHandoff)
	{
		pSelf->m_cornerCheckDcNavHandoffParams		= *pExitPerf->m_navAnimHandoff;
		pSelf->m_cornerCheckDcNavHandoffParamsValid = true;
	}

	pSelf->GoExitCornerCheck();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::EnterCornerCheck::SetGoalLoc(const NavLocation& navLoc)
{
	AiSearchController* pSelf = GetSelf();
	pSelf->m_goalLoc = navLoc;
	pSelf->m_pathPs.Clear();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::EnterCornerCheck::OnReset()
{
	AiSearchController* pSelf = GetSelf();
	pSelf->EndCornerCheck();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::EnterCornerCheck::OnInterrupt()
{
	AiSearchController* pSelf = GetSelf();
	pSelf->EndCornerCheck();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::EnterCornerCheck::DebugDraw(ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	const AiSearchController* pSelf = GetSelf();

	const Npc* pNpc		  = Npc::FromProcess(GetCharacter());
	const ActionPack* pAp = pSelf->m_hCorner.ToActionPack();

	ActionPackResolveInput input = MakeDefaultResolveInput(pNpc);
	input.m_motionType = pSelf->m_enteredMotionType;

	pSelf->DebugDrawExits(input, pAp, &pSelf->m_pathPs);
}

FSM_STATE_REGISTER(AiSearchController, EnterCornerCheck, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
class AiSearchController::ExitCornerCheck : public AiSearchController::BaseState
{
	virtual void OnExit() override;
	virtual bool IsBusy() const override;
	virtual void UpdateStatus() override;
	virtual void OnReset() override;
	virtual void OnInterrupt() override;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::ExitCornerCheck::OnExit()
{
	AiSearchController* pSelf = GetSelf();
	pSelf->m_entryDef = ActionPackEntryDef();
	pSelf->m_pathPs.Clear();
	pSelf->m_hPulloutRegion = nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::ExitCornerCheck::IsBusy() const
{
	AiSearchController* pSelf = GetSelf();
	return !pSelf->m_animAction.IsDone();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::ExitCornerCheck::UpdateStatus()
{
	AiSearchController* pSelf = GetSelf();
	NavCharacter* pNavChar	  = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	pSelf->m_animAction.Update(pAnimControl);

	if (pSelf->m_animAction.IsDone() || pSelf->m_animAction.GetAnimPhase(pAnimControl) >= pSelf->m_cornerCheckExitPhase)
	{
		pSelf->EndCornerCheck();
		return;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::ExitCornerCheck::OnReset()
{
	AiSearchController* pSelf = GetSelf();
	pSelf->EndCornerCheck();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::ExitCornerCheck::OnInterrupt()
{
	AiSearchController* pSelf = GetSelf();
	pSelf->EndCornerCheck();
}

FSM_STATE_REGISTER(AiSearchController, ExitCornerCheck, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
class AiSearchController::WaitPerformance : public AiSearchController::BaseState
{
	virtual void OnEnter() override;
	virtual bool IsBusy() const override;
	virtual void UpdateStatus() override;

	virtual bool ShouldInterruptNavigation() const override { return true; }
};

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::WaitPerformance::OnEnter()
{
	AiSearchController* pSelf = GetSelf();
	NavCharacter* pNavChar	  = GetCharacter();

	NavAnimHandoffDesc handoff;
	handoff.SetStateChangeRequestId(pSelf->m_animAction.GetStateChangeRequestId(), INVALID_STRING_ID_64);
	handoff.m_motionType	= kMotionTypeMax;

	pNavChar->ConfigureNavigationHandOff(handoff, FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSearchController::WaitPerformance::UpdateStatus()
{
	AiSearchController* pSelf = GetSelf();
	NavCharacter* pNavChar	  = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	pSelf->m_animAction.Update(pAnimControl);

	if (pSelf->m_animAction.IsDone())
	{
		pSelf->GoInactive();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSearchController::WaitPerformance::IsBusy() const
{
	AiSearchController* pSelf = GetSelf();
	return !pSelf->m_animAction.IsDone();
}

FSM_STATE_REGISTER(AiSearchController, WaitPerformance, kPriorityMedium);

/// --------------------------------------------------------------------------------------------------------------- ///
AiSearchController* CreateAiSearchController()
{
	return NDI_NEW AiSearchController;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AiSearchController::GetMaxStateAllocSize() const
{
	size_t stateSizes[] =
	{
		sizeof(Inactive),
		sizeof(EnterCornerCheck),
		sizeof(ExitCornerCheck),
		sizeof(WaitPerformance),
	};

	size_t maxStateSize = 0;

	for (U32F ii = 0; ii < ARRAY_COUNT(stateSizes); ++ii)
	{
		maxStateSize = Max(maxStateSize, stateSizes[ii]);
	}

	return maxStateSize;
}

