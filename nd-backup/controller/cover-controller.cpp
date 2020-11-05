/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/cover-controller.h"

#include "corelib/util/angle.h"

#include "ndlib/anim/anim-align-cache.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-layer.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-util.h"
#include "ndlib/anim/ik/jacobian-ik.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/screen-space-text-printer.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/scriptx/h/dc-types.h"

#include "gamelib/gameplay/ai/agent/nav-character-adapter.inl"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/ap-entry-util.h"
#include "gamelib/gameplay/ap-exit-util.h"
#include "gamelib/gameplay/character-motion-match-locomotion.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-state-machine.h"
#include "gamelib/level/artitem.h"
#include "gamelib/ndphys/composite-body.h"
#include "gamelib/scriptx/h/ap-entry-defines.h"

#include "ailib/util/ai-action-pack-util.h"
#include "ailib/util/ai-lib-util.h"

#include "game/ai/agent/npc-action-pack-util.h"
#include "game/ai/agent/npc.h"
#include "game/ai/base/ai-game-debug.h"
#include "game/ai/base/ai-game-util.h"
#include "game/ai/characters/infected.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/coordinator/encounter-coordinator.h"
#include "game/player/player-action-pack-mgr.h"
#include "game/player/player-snapshot.h"
#include "game/scriptx/h/ai-weapon-defines.h"
#include "game/scriptx/h/anim-npc-info.h"
#include "game/scriptx/h/hit-reactions-defines.h"
#include "game/scriptx/h/npc-cover-defines.h"
#include "game/weapon/process-weapon-base.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsKnownCoverState(StringId64 stateId)
{
	switch (stateId.GetValue())
	{
	case SID_VAL("s_enter-cover"):
	case SID_VAL("s_cover-exit^idle"):
	case SID_VAL("s_cover-exit^moving"):
	case SID_VAL("s_cover-idle"):
	case SID_VAL("s_cover-idle^aim"):
	case SID_VAL("s_cover-idle-fire"):
	case SID_VAL("s_cover-idle^open-idle"):
	case SID_VAL("s_cover-idle^open-aim"):
	case SID_VAL("s_cover-idle^hunker"):
	case SID_VAL("s_cover-idle^peek"):
	case SID_VAL("s_cover-peek"):
	case SID_VAL("s_cover-peek^idle"):
	case SID_VAL("s_cover-peek^aim"):
	case SID_VAL("s_cover-aim"):
	case SID_VAL("s_cover-aim-reassess"):
	case SID_VAL("s_cover-aim-fire"):
	case SID_VAL("s_cover-aim^idle"):
	case SID_VAL("s_cover-aim^peek"):
	case SID_VAL("s_cover-aim^idle-fire"):
	case SID_VAL("s_cover-aim^open-aim"):
	case SID_VAL("s_cover-aim^hunker"):
	case SID_VAL("s_cover-open-idle"):
	case SID_VAL("s_cover-open-idle^idle"):
	case SID_VAL("s_cover-open-aim"):
	case SID_VAL("s_cover-open-aim^idle"):
	case SID_VAL("s_cover-open-aim^aim"):
	case SID_VAL("s_cover-open-grenade-draw"):
	case SID_VAL("s_cover-open-grenade-toss"):
	case SID_VAL("s_cover-performance"):
	case SID_VAL("s_cover-hit-reaction"):
	case SID_VAL("s_cover-grenade-draw"):
	case SID_VAL("s_cover-grenade-draw-flipped"):
	case SID_VAL("s_cover-grenade-toss"):
	case SID_VAL("s_cover-grenade-toss-flipped"):
	case SID_VAL("s_cover-step-out^idle"):
	case SID_VAL("s_cover-step-out"):
	case SID_VAL("s_cover-idle^step-out"):
	case SID_VAL("s_cover-hunker"):
	case SID_VAL("s_cover-hunker^idle"):
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool CanShareCoverWithPlayer(const NavCharacter* pChar)
{
	return pChar->IsBuddyNpc();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static StringId64 GetCoverTypeId(CoverDefinition::CoverType coverType)
{
	switch (coverType)
	{
	case CoverDefinition::kCoverStandLeft:
		return SID("standing-cover-left");
	case CoverDefinition::kCoverStandRight:
		return SID("standing-cover-right");
	case CoverDefinition::kCoverCrouchLeft:
		return SID("low-cover-left");
	case CoverDefinition::kCoverCrouchRight:
		return SID("low-cover-right");
	case CoverDefinition::kCoverCrouchOver:
		return SID("low-cover-over");
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static StringId64 GetCoverModeId(IAiCoverController::CoverMode coverMode)
{
	switch (coverMode)
	{
	case IAiCoverController::kCoverModeAim:		return SID("aim");
	case IAiCoverController::kCoverModeHunker:	return SID("hunker");
	default:									return SID("default");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <class ReturnType>
const ReturnType* GetEntriesMap(const StringId64 sourceMapId, CoverDefinition::CoverType coverType)
{
	const DC::Map* pEntriesMap = ScriptManager::Lookup<DC::Map>(sourceMapId, nullptr);
	if (!pEntriesMap)
		return nullptr;

	const StringId64 coverTypeId = GetCoverTypeId(coverType);
	const ReturnType* pEntries = ScriptManager::MapLookup<ReturnType>(pEntriesMap, coverTypeId);
	return pEntries;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <class ReturnType>
const ReturnType* GetExitsMap(const StringId64 sourceMapId,
							  const IAiCoverController::CoverMode coverMode,
							  CoverDefinition::CoverType coverType)
{
	const DC::Map* pCoverModeMap = ScriptManager::Lookup<DC::Map>(sourceMapId, nullptr);
	if (!pCoverModeMap)
		return nullptr;

	StringId64 coverModeId = GetCoverModeId(coverMode);
	StringId64 coverTypeId = GetCoverTypeId(coverType);

	const DC::Map* pExitMap = ScriptManager::MapLookup<DC::Map>(pCoverModeMap, coverModeId);

	// exit map non-existent, fall back to default
	if (!pExitMap)
	{
		if (coverModeId == SID("default"))
			return nullptr;

		coverModeId = SID("default");
		coverTypeId = GetCoverTypeId(coverType);
		pExitMap = ScriptManager::MapLookup<DC::Map>(pCoverModeMap, coverModeId);
		if (!pExitMap)
			return nullptr;
	}

	const ReturnType* pExits = ScriptManager::MapLookup<ReturnType>(pExitMap, coverTypeId);

	// exit list non-existent, fall back to default
	if (!pExits && coverModeId != SID("default"))
	{
		coverModeId = SID("default");
		coverTypeId = GetCoverTypeId(coverType);
		pExitMap = ScriptManager::MapLookup<DC::Map>(pCoverModeMap, coverModeId);
		if (!pExitMap)
			return nullptr;

		pExits = ScriptManager::MapLookup<ReturnType>(pExitMap, coverTypeId);
	}

	return pExits;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static CoverDefinition::CoverType GetCoverTypeForNpc(CoverDefinition::CoverType baseType,
													 CoverActionPack::PreferredDirection dirPref)
{
	CoverDefinition::CoverType ct = baseType;
	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_cover.m_forceAsLow))
	{
		switch (baseType)
		{
		case CoverDefinition::kCoverStandLeft:
			ct = CoverDefinition::kCoverCrouchLeft;
			break;
		case CoverDefinition::kCoverStandRight:
			ct = CoverDefinition::kCoverCrouchRight;
			break;
		}
	}

	if (ct == CoverDefinition::kCoverCrouchOver)
	{
		switch (dirPref)
		{
		case CoverActionPack::kPreferLeft:
			ct = CoverDefinition::kCoverCrouchLeft;
			break;

		case CoverActionPack::kPreferRight:
			ct = CoverDefinition::kCoverCrouchRight;
			break;

		case CoverActionPack::kPreferNone:
			// crouch over should not be a thing anymore. if you don't specify a dirpref,
			// it should just pick one. but currently, we still ask DCX for 'over',
			// and currently, that always maps to right. TODO remove the notion of 'over'
			ct = CoverDefinition::kCoverCrouchRight;
			break;

		default:
			ALWAYS_HALT();
		}
	}

	return ct;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame GetApReferenceForCover(const CoverActionPack* pCoverAp,
								  CoverDefinition::CoverType ct,
								  F32 sidewaysOffset /* = 0.0f */)
{
	AI_ASSERT(pCoverAp);

	if (!pCoverAp)
		return BoundFrame(kIdentity);

	BoundFrame apRef = pCoverAp->GetBoundFrame();

	switch (ct)
	{
	case CoverDefinition::kCoverCrouchLeft:
	case CoverDefinition::kCoverCrouchRight:
	case CoverDefinition::kCoverStandLeft:
	case CoverDefinition::kCoverStandRight:
		apRef.AdjustTranslationWs(pCoverAp->GetWallDirectionWs() * 0.5f);
		break;
	}

	apRef.AdjustTranslationPs(GetLocalX(apRef.GetRotationPs()) * sidewaysOffset);

	return apRef;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Point GetDefaultEntryPoint(const Locator& coverApRef, const float defaultEntryDist)
{
	const Vector entryVec = Vector(0.0f, 0.0f, -defaultEntryDist);
	const Vector offset = coverApRef.TransformVector(entryVec);
	const Point entryPos = coverApRef.Pos() + offset;
	return entryPos;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static F32 ComputePhaseError(F32 a, F32 b)
{
	const F32 aToB = b-a;

	if (aToB > 0.5f)
	{
		return Abs(a + (1.0f - b));
	}
	else if (aToB < -0.5f)
	{
		return Abs(b + (1.0f - a));
	}
	else
	{
		return Abs(aToB);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static F32 GetAnimSyncPhaseError(const Character* pChar, StringId64 syncAnimId, F32 syncPhase)
{
	const AnimControl* pAnimControl		= pChar->GetAnimControl();
	const StringId64 translatedSyncAnimId	= (syncAnimId != INVALID_STRING_ID_64)? pAnimControl->LookupAnimId(syncAnimId) : INVALID_STRING_ID_64;
	const AnimStateInstance* pStateInst	= pAnimControl->GetBaseStateLayer()->CurrentStateInstance();
	const ArtItemAnim* pCurrentAnim		= pStateInst? pStateInst->GetPhaseAnimArtItem().ToArtItem() : nullptr;
	const bool animCorrect				= pCurrentAnim && (pCurrentAnim->GetNameId() == translatedSyncAnimId);
	if (!animCorrect)
	{
		return 1.0f;
	}

	if (syncPhase < 0.0f)
	{
		return 0.0f; // if no sync phase defined, any phase is ok
	}

	return ComputePhaseError(syncPhase, pStateInst->Phase());
}

/// --------------------------------------------------------------------------------------------------------------- ///
IAiCoverController::IAiCoverController()
	: m_rangedEntryRadius(3.0f)
	, m_coverType(CoverDefinition::kCoverTypeCount)
	, m_enterAnimOverride(0)
	, m_sidewaysOffset(0.0f)
	, m_cachedCoverMode(kCoverModeInvalid)
	, m_shootSide(true)
	, m_canStepOut(true)
	, m_doingGrenadeDraw(false)
	, m_doingGrenadeToss(false)
	, m_doingMmEnter(false)
	, m_exitPhase(-1.0f)
	, m_pIkTree(nullptr)
	, m_ikCurDeltaY(0.0f)
{
	m_pIkJacobianMap[kLeftLeg] = m_pIkJacobianMap[kRightLeg] = nullptr;
	m_ikHeightTracker.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::Init(NavCharacter* pNavChar, const NavControl* pNavControl)
{
	ActionPackController::Init(pNavChar, pNavControl);
	const SkeletonId skelId = pNavChar->GetSkeletonId();
	const AnimControllerConfig* pConfig = static_cast<const AnimControllerConfig*>(pNavChar->GetAnimControllerConfig());

	if (pConfig->m_gameCover.m_coverIdleIkData != INVALID_STRING_ID_64)
	{
		StringId64 endJoints[] =
		{
			SID("root"),
			SID("l_ball"),
			SID("r_ball")
		};

		m_pIkTree = NDI_NEW JointTree;
		m_pIkTree->Init(pNavChar, SID("root"), false, ARRAY_COUNT(endJoints), endJoints);
		m_pIkTree->InitIkData(pConfig->m_gameCover.m_coverIdleIkData);

		m_pIkJacobianMap[kLeftLeg] = NDI_NEW JacobianMap;
		JacobianMap::EndEffectorDef leftEndEffectors[] =
		{
			JacobianMap::EndEffectorDef(SID("l_ball"), IkGoal::kRotation),
			JacobianMap::EndEffectorDef(SID("l_ball"), IkGoal::kPosition),
		};

		m_pIkJacobianMap[kLeftLeg]->Init(m_pIkTree, SID("l_upper_leg"), ARRAY_COUNT(leftEndEffectors), leftEndEffectors);

		m_pIkJacobianMap[kRightLeg] = NDI_NEW JacobianMap;
		JacobianMap::EndEffectorDef rightEndEffectors[] =
		{
			JacobianMap::EndEffectorDef(SID("r_ball"), IkGoal::kRotation),
			JacobianMap::EndEffectorDef(SID("r_ball"), IkGoal::kPosition),
		};

		m_pIkJacobianMap[kRightLeg]->Init(m_pIkTree, SID("r_upper_leg"), ARRAY_COUNT(rightEndEffectors), rightEndEffectors);
	}

	m_doingMmEnter = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ActionPackController::Relocate(deltaPos, lowerBound, upperBound);

	DeepRelocatePointer(m_pIkTree, deltaPos, lowerBound, upperBound);
	DeepRelocatePointer(m_pIkJacobianMap[kLeftLeg], deltaPos, lowerBound, upperBound);
	DeepRelocatePointer(m_pIkJacobianMap[kRightLeg], deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::Reset()
{
	AiLogAnim(GetCharacter(), "IAiCoverController::Reset()\n");

	m_animAction.Reset();
	m_enterAction.Reset();
	m_exitAction.Reset();
	m_peekAction.Reset();
	m_performanceAction.Reset();
	m_throwGrenadeAction.Reset();

	m_performanceStateMask = 0;
	m_doingGrenadeToss = false;
	m_doingGrenadeDraw = false;
	m_doingMmEnter = false;
	m_grenadeDrawn = false;
	m_grenadeReleased = false;

	ResetCoverOverlays();
	m_sidewaysOffset = 0.0f;

	if (m_pIkJacobianMap[kLeftLeg])
	{
		m_pIkJacobianMap[kLeftLeg]->ResetJointIkOffsets();
	}

	if (m_pIkJacobianMap[kRightLeg])
	{
		m_pIkJacobianMap[kRightLeg]->ResetJointIkOffsets();
	}

	m_ikCurDeltaY = 0.0f;
	m_ikHeightTracker.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::RequestAnimations()
{
	const CoverActionPack* pCoverAp = m_hActionPack.ToActionPack<CoverActionPack>();
	if (!pCoverAp)
		return;

	NavCharacter* pNavChar		= GetCharacter();
	AnimControl* pAnimControl	= pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer	= pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	DC::AnimNpcInfo* pInfo		= pAnimControl ? pAnimControl->Info<DC::AnimNpcInfo>() : nullptr;
	const BoundFrame coverApRef	= GetApReferenceForCurrentCover();

	AnimStateInstance* pCurInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;

	if (!pInfo)
		return;

	const CoverMode curMode = GetCurrentCoverMode();

	Locator coverShareLocWs = Locator(kOrigin);
	const bool wasSharing	= m_sharingCover;
	m_sharingCover = AI::ShouldHunkerForCoverShare(pNavChar, pCoverAp, wasSharing, &coverShareLocWs);

/*
	if (m_sharingCover)
	{
		pNavChar->RequestPathThroughPlayer();
	}
*/

	const bool animSharing = curMode == kCoverModeHunker;
	const bool animOutOfDate = (animSharing != m_sharingCover) && !IsBusy();

	if (false)
	{
		MsgCon("[%s]\n", pNavChar->GetName());
		MsgCon("  Sharing:          %s\n", m_sharingCover ? "true" : "false");
		MsgCon("  Was Sharing:      %s\n", wasSharing ? "true" : "false");
		MsgCon("  Anim Sharing:     %s\n", animSharing ? "true" : "false");
		MsgCon("  Anim Out of Date: %s\n", animOutOfDate ? "true" : "false");
	}

	if (curMode == kCoverModeEntering)
	{
		if (m_sharingCover)
		{
			EnableCoverShare(coverShareLocWs);
		}
		else
		{
			DisableCoverShare();
		}
	}
	else if (((m_sharingCover != wasSharing) && (curMode != kCoverModeInvalid)) || animOutOfDate)
	{
		float startPhase = -1.0f;

		switch (pCurInstance->GetStateName().GetValue())
		{
		case SID_VAL("s_cover-hunker^idle"):
		case SID_VAL("s_cover-idle^hunker"):
		case SID_VAL("s_cover-aim^hunker"):
			startPhase = Limit01(0.7f - pCurInstance->Phase());
			break;
		}

		if (m_sharingCover)
		{
			Share(startPhase);
		}
		else
		{
			Unshare(startPhase);
		}
	}

	if (curMode == kCoverModeEntering)
	{
		const ProcessWeaponBase* pCurWeapon = pNavChar->GetWeapon();
		if (pNavChar->IsBuddyNpc() && pCurWeapon && pCurWeapon->IsBow())
		{
			IAiWeaponController* pWeaponController = pNavChar->GetAnimationControllers()->GetWeaponController();
			pWeaponController->TryExtendSuppression(0.2f);
		}

		static float kCoverSpeedFilter = 0.5f;
		pInfo->m_speedFactor = AI::TrackSpeedFactor(pNavChar, pInfo->m_speedFactor, 1.0f, kCoverSpeedFilter);
	}

	if (curMode == kCoverModeIdle
		&& pNavChar->IsInCombat()
		&& !pNavChar->IsBuddyNpc()
		&& !pNavChar->IsKindOf(g_type_Infected)
		&& IsBlockedByBreakable()
		&& !pBaseLayer->AreTransitionsPending())
	{
		FadeToStateParams params;
		params.m_stateStartPhase = 0.0f;
		params.m_animFadeTime	 = 0.5f;
		params.m_motionFadeTime	 = 0.5f;
		params.m_blendType		 = DC::kAnimCurveTypeEaseIn;
		params.m_newInstBehavior = FadeToStateParams::kSpawnNewTrack;

		DoCoverPerformance(SID("cover-break-glass"), params);
	}

	if ((curMode == kCoverModeExiting) && pCurInstance && IsKnownCoverState(pCurInstance->GetStateName()))
	{
		DC::AnimStateFlag& exitStateFlags = pCurInstance->GetMutableStateFlags();
		bool shouldEnableAdjustment = false;
		const bool adjustmentEnabled = (exitStateFlags & DC::kAnimStateFlagNoAdjustToNavMesh) == 0;

		if (m_exitPhase < 0.0f)
		{
			const Point myPosPs = pNavChar->GetTranslationPs();
			shouldEnableAdjustment = pNavChar->IsPointClearForMePs(myPosPs, false);
		}
		else
		{
			const float curPhase = pCurInstance->Phase();

			if (curPhase >= m_exitPhase)
			{
				shouldEnableAdjustment = true;
			}
		}

		if (shouldEnableAdjustment && !adjustmentEnabled)
		{
			AiLogAnim(pNavChar,
					  "IAiCoverController enabling nav mesh adjustment for instance '%s' (%s) @ phase %0.3f\n",
					  DevKitOnly_StringIdToString(pCurInstance->GetStateName()),
					  pCurInstance->GetPhaseAnimArtItem().ToArtItem()
						  ? pCurInstance->GetPhaseAnimArtItem().ToArtItem()->GetName()
						  : "<null>",
					  pCurInstance->Phase());

			exitStateFlags &= ~DC::kAnimStateFlagNoAdjustToNavMesh;
			exitStateFlags |= DC::kAnimStateFlagAdjustApToRestrictAlign;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::UpdateStatus()
{
	PROFILE(AI, CoverCtrl_UpdateStatus);
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	const CoverMode currentCoverMode = GetCurrentCoverMode();

	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const StringId64 currentStateId = pBaseLayer ? pBaseLayer->CurrentStateId() : INVALID_STRING_ID_64;

	if (m_doingMmEnter && IsMotionMatchingEnterComplete(false))
	{
		const bool isInNonEnterMode = currentStateId != SID("s_motion-match-locomotion");

		if (isInNonEnterMode)
		{
			AiLogAnim(pNavChar,
					  "IAiCoverController::UpdateStatus -- Motion matched enter is complete but we're already doing something else\n");
		}
		else
		{
			AiLogAnim(pNavChar,
					  "IAiCoverController::UpdateStatus -- Motion matched enter is complete, fading to cover idle\n");

			const StringId64 desStateId = GetDestinationState(m_coverType);
			if (currentStateId != desStateId && !pBaseLayer->AreTransitionsPending())
			{
				ForceBlendToDestinationState(m_coverType);
			}
		}

		m_doingMmEnter = false;
	}

	m_cachedCoverMode = currentCoverMode;

	{
		//PROFILE(AI, CoverCtrl_Update_Actions);
		m_enterAction.Update(pAnimControl);
		m_exitAction.Update(pAnimControl);
		m_peekAction.Update(pAnimControl);
		m_performanceAction.Update(pAnimControl);
		m_throwGrenadeAction.Update(pAnimControl);
		m_animAction.Update(pAnimControl);
	}

	// draw
	if (m_throwGrenadeAction.IsValid() && !m_throwGrenadeAction.IsDone() && m_doingGrenadeDraw && !m_grenadeDrawn)
	{
		if (const AnimStateInstance* pDrawInst = m_throwGrenadeAction.GetTransitionDestInstance(pAnimControl))
		{
			const ArtItemAnim* pDrawAnim = pDrawInst->GetPhaseAnimArtItem().ToArtItem();
			const float curPhase = pDrawInst->GetPhase();

			bool foundEff = false;
			const float drawPhase = FindEffPhase(pDrawAnim, SID("draw-grenade"), 0.60f, &foundEff);

			if (curPhase >= drawPhase)
			{
				pNavChar->NotifyDrawHeldThrowable(!foundEff);
				m_grenadeDrawn = true;
			}
		}
	}

	// throw
	if (m_throwGrenadeAction.IsValid() && !m_throwGrenadeAction.IsDone() && m_doingGrenadeToss && !m_grenadeReleased)
	{
		if (const AnimStateInstance* pThrowInst = m_throwGrenadeAction.GetTransitionDestInstance(pAnimControl))
		{
			const ArtItemAnim* pThrowAnim = pThrowInst->GetPhaseAnimArtItem().ToArtItem();
			const float grenadeTossPhase = pThrowInst->GetPhase();

			bool foundEff = false;
			const float releasePhase = FindEffPhase(pThrowAnim, SID("throw-grenade"), 0.47f, &foundEff);

			if (grenadeTossPhase >= releasePhase)
			{
				pNavChar->NotifyReleaseHeldThrowable(!foundEff);
				m_grenadeReleased = true;
			}
		}
	}

	if (m_throwGrenadeAction.IsDone())
	{
		if (m_doingGrenadeDraw || m_doingGrenadeToss)
		{
			m_doingGrenadeDraw = m_doingGrenadeToss = false;

			pNavChar->GetAnimationControllers()->GetWeaponController()->UpdateOverlays();
		}
	}

	if ((m_entryDef.m_skipPhase > 0.0f) && m_enterAction.WasTransitionTakenThisFrame())
	{
		AnimStateInstance* pInst = m_enterAction.GetTransitionDestInstance(pAnimControl);
		const ArtItemAnim* pEntryAnim = pInst ? pInst->GetPhaseAnimArtItem().ToArtItem() : nullptr;
		if (pEntryAnim)
		{
			const Locator myLocWs = pNavChar->GetLocator();
			BoundFrame apRef = pInst->GetApLocator();
			Locator apRefWs;
			const SkeletonId skelId = pNavChar->GetSkeletonId();
			const float phase = pInst->PrevPhase();
			const float flipped = pInst->IsFlipped();
			if (FindApReferenceFromAlign(skelId, pEntryAnim, myLocWs, &apRefWs, SID("apReference"), phase, flipped))
			{
				apRef.SetLocatorWs(apRefWs);
				pInst->SetApLocator(apRef);
			}
		}
	}

	if (m_exitAction.IsValid() && !m_exitAction.IsDone())
	{
		if (const AnimStateInstance* pExitInst = m_exitAction.GetTransitionDestInstance(pAnimControl))
		{
			const float curPhase = pExitInst->Phase();
			const float exitPhase = m_exitPhase;

			if ((exitPhase >= 0.0f) && (curPhase >= exitPhase))
			{
				m_exitHandoff.SetStateChangeRequestId(m_exitAction.GetStateChangeRequestId(), pExitInst->GetStateName());
				pNavChar->ConfigureNavigationHandOff(m_exitHandoff, FILE_LINE_FUNC);

				m_exitAction.Reset();
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
class IkFactorBlender : public AnimStateLayer::InstanceBlender<float>
{
public:
	IkFactorBlender(bool forced) : m_forced(forced) {}
	virtual ~IkFactorBlender() override {}

	virtual float GetDefaultData() const override { return 0.0f; }
	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, float* pDataOut) override
	{
		const StringId64 stateId = pInstance->GetStateName();

		if (m_forced)
		{
			if (IsKnownCoverState(stateId) || stateId == SID("s_motion-match-locomotion"))
			{
				*pDataOut = 1.0f;
			}
			else
			{
				*pDataOut = 0.0f;
			}

			return true;
		}

		switch (stateId.GetValue())
		{
		case SID_VAL("s_cover-idle^aim"):
		case SID_VAL("s_cover-aim"):
		case SID_VAL("s_cover-open-aim"):
		case SID_VAL("s_cover-aim-fire"):
		case SID_VAL("s_cover-idle-fire"):
		case SID_VAL("s_cover-peek"):
		case SID_VAL("s_cover-idle^peek"):
		case SID_VAL("s_cover-peek^idle"):
		case SID_VAL("s_cover-aim-reassess"):
			*pDataOut = 1.0f;
			break;

		default:
			*pDataOut = 0.0f;
			break;
		}

		return true;
	}

	virtual float BlendData(const float& leftData,
							const float& rightData,
							float masterFade,
							float animFade,
							float motionFade) override
	{
		const float factor = Lerp(leftData, rightData, animFade);
		return factor;
	}

	bool m_forced;
};

/// --------------------------------------------------------------------------------------------------------------- ///
float IAiCoverController::GetIkFade(float* pDeltaYOut, bool force) const
{
	const NavCharacter* pNavChar = GetCharacter();
	const CoverActionPack* pCoverActionPack = GetActionPack();
	if (!pNavChar || !pCoverActionPack)
		return 0.0f;

	const CoverDefinition& coverDef	= pCoverActionPack->GetDefinition();
	const Point aimOriginWs			= pNavChar->GetAimOriginWs();
	const Point aimPosWs			= pNavChar->GetAimAtPositionWs();
	const Vector aimDirWs			= SafeNormalize(aimPosWs - aimOriginWs, kUnitZAxis);

	const Vector coverFwWs = GetLocalZ(pCoverActionPack->GetLocatorWs().Rot());
	const float coverAimDot = Dot(aimDirWs, coverFwWs);

	float ikFade = 0.0f;

	const bool aimToggle = ((coverAimDot > 0.0f) && (coverDef.m_coverType == CoverDefinition::kCoverCrouchOver));

	if (force || aimToggle)
	{
		IkFactorBlender blender(force);
		ikFade = blender.BlendForward(pNavChar->GetAnimControl()->GetBaseStateLayer(), 0.0f);
	}

	if (aimToggle)
	{
		const float coverDepth = 1.75f;
		const Point projectedPosWs = aimOriginWs + (aimDirWs * coverDepth);
		const Point coverPosWs = pCoverActionPack->GetBoundFrame().GetTranslationWs();
		const float coverHeight = coverDef.m_height;
		const float heightDelta = projectedPosWs.Y() - (coverPosWs.Y() + coverHeight);

		const float desiredDy = LerpScale(-0.5f, 0.0f, 0.225f, 0.0f, heightDelta) * ikFade;

		//g_prim.Draw(DebugString(aimOriginWs, StringBuilder<256>("%0.2fm -> %0.2fm", coverHeight, desiredDy).c_str(), kColorWhite, 0.75f));

		*pDeltaYOut = desiredDy;
	}
	else
	{
		*pDeltaYOut = 0.0f;
	}

	return ikFade;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::UpdateProcedural()
{
	NavCharacter* pNavChar = GetCharacter();
	FgAnimData* pAnimData = pNavChar->GetAnimData();

	if (!pAnimData || !pNavChar)
		return;

	float waterAdj = 0.0f;
	const NavControl* pNavControl = pNavChar->GetNavControl();

	if (pNavControl && pNavControl->NavMeshForcesWade() && m_cachedCoverMode != kCoverModeExiting)
	{
		const WaterDetector* pWaterDetector = pNavChar->GetWaterDetectorAlign();
		const CoverActionPack* pCoverActionPack = GetActionPack();
		if (pWaterDetector && pWaterDetector->IsValid() && pCoverActionPack && pCoverActionPack->GetDefinition().IsLow())
		{
			const I32F iHeadJoint = pAnimData->FindJoint(SID("headb"));

			if (iHeadJoint >= 0)
			{
				const Locator headLocWs = pAnimData->m_jointCache.GetJointLocatorWs(iHeadJoint);
				const float headRadius = 0.15f; // bump for big head mode

				const Point surfaceWs = pWaterDetector->WaterSurface();
				const Point myPosWs = pNavChar->GetTranslation();
				const float waterHeight = surfaceWs.Y() - myPosWs.Y();
				const float headHeight = headLocWs.Pos().Y() - myPosWs.Y();
				const float desHeadHeight = waterHeight + headRadius;

				waterAdj = Limit(desHeadHeight - headHeight, 0.0f, 0.75f);

				//g_prim.Draw(DebugCross(surfaceWs, 0.5f, kColorCyan, kPrimEnableHiddenLineAlpha, StringBuilder<64>("%0.3fm", waterHeight).c_str()));
				//g_prim.Draw(DebugCross(headLocWs.Pos(), headRadius, kColorMagenta, kPrimEnableHiddenLineAlpha));
				//g_prim.Draw(DebugString(headLocWs.Pos(), StringBuilder<64>("%0.3fm -> %0.3fm (%0.3fm)", headHeight, waterAdj, m_ikCurDeltaY).c_str(), kColorMagenta, 0.5f));
			}
		}
	}

	float desiredDy = 0.0f;
	const bool forceIk = (waterAdj > 0.001f) || (m_ikCurDeltaY > 0.001f);
	float ikFade = GetIkFade(&desiredDy, forceIk);

	desiredDy += waterAdj;

	const float kIkHeightSpringK = 5.0f;
	const float dt = pNavChar->GetClock()->GetDeltaTimeInSeconds();
	m_ikCurDeltaY = m_ikHeightTracker.Track(m_ikCurDeltaY, desiredDy, dt, kIkHeightSpringK);

	if (ikFade > 0.0f && m_pIkTree && m_pIkJacobianMap[kLeftLeg] && m_pIkJacobianMap[kRightLeg])
	{
		ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

		const Locator& lBallWs = pAnimData->m_jointCache.GetJointLocatorWs(pAnimData->FindJoint(SID("l_ball")));
		const Locator& rBallWs = pAnimData->m_jointCache.GetJointLocatorWs(pAnimData->FindJoint(SID("r_ball")));

		const Vector ikHeightAdjustmentWs = Vector(0.0f, m_ikCurDeltaY, 0.0f);

		JacobianIkInstance ikInst[2];

		ikInst[kLeftLeg].m_blend				= ikFade;
		ikInst[kLeftLeg].m_maxIterations		= 10;
		ikInst[kLeftLeg].m_pJoints				= m_pIkTree;
		ikInst[kLeftLeg].m_pJacobianMap			= m_pIkJacobianMap[kLeftLeg];
		ikInst[kLeftLeg].m_pJointLimits			= pNavChar->GetJointLimits();
		ikInst[kLeftLeg].m_pConstraints			= m_pIkTree->GetJointConstraints();
		ikInst[kLeftLeg].m_disableJointLimits	= true;
		ikInst[kLeftLeg].m_debugDrawJointLimits	= false;
		ikInst[kLeftLeg].m_goal[0].SetGoalRotation(lBallWs.Rot());
		ikInst[kLeftLeg].m_goal[1].SetGoalPosition(lBallWs.Pos());

		ikInst[kRightLeg].m_blend					= ikFade;
		ikInst[kRightLeg].m_maxIterations			= 10;
		ikInst[kRightLeg].m_pJoints					= m_pIkTree;
		ikInst[kRightLeg].m_pJacobianMap			= m_pIkJacobianMap[kRightLeg];
		ikInst[kRightLeg].m_pJointLimits			= pNavChar->GetJointLimits();
		ikInst[kRightLeg].m_pConstraints			= m_pIkTree->GetJointConstraints();
		ikInst[kRightLeg].m_disableJointLimits		= true;
		ikInst[kRightLeg].m_debugDrawJointLimits	= false;
		ikInst[kRightLeg].m_goal[0].SetGoalRotation(rBallWs.Rot());
		ikInst[kRightLeg].m_goal[1].SetGoalPosition(rBallWs.Pos());

		m_pIkTree->ReadJointCache();

		const U32F jointOffset = m_pIkTree->FindJointOffset(SID("root"));
		Locator locLs = m_pIkTree->GetJointLocLs(jointOffset);
		locLs.SetPos(locLs.Pos() + ikHeightAdjustmentWs);
		m_pIkTree->SetJointLocLs(jointOffset, locLs);
		m_pIkTree->UpdateAllJointLocsWs();

		SolveJacobianIK(&ikInst[kLeftLeg]);

		SolveJacobianIK(&ikInst[kRightLeg]);

		m_pIkTree->WriteJointCacheBlend(ikFade);
	}
	else if (m_ikCurDeltaY != 0.0f)
	{
		m_ikCurDeltaY = 0.0f;
		m_ikHeightTracker.Reset();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::IsBusy() const
{
	return !m_animAction.IsDone()			||
		   !m_enterAction.IsDone()			||
		   !m_exitAction.IsDone()			||
		   !m_peekAction.IsDone()			||
		   !m_performanceAction.IsDone()	||
		   !m_throwGrenadeAction.IsDone();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::IsEnterComplete() const
{
	const NavCharacter* pNavChar = GetCharacter();
	const AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	if (!pBaseLayer || pBaseLayer->AreTransitionsPending())
	{
		return false;
	}

	const AnimStateInstance* pCurInstance = pBaseLayer->CurrentStateInstance();

	if (!pCurInstance)
	{
		return false;
	}

	const StringId64 animStateId = pCurInstance->GetStateName();
	if ((animStateId != SID("s_enter-cover")) && (animStateId != SID("s_motion-match-locomotion")))
	{
		return true;
	}

	const Locator& myLocPs = pNavChar->GetLocatorPs();

	const float remDist	  = Dist(myLocPs.Pos(), m_enterFinalLocPs.Pos());
	const float orientDot = Dot(GetLocalZ(myLocPs.Rot()), GetLocalZ(m_enterFinalLocPs.Rot()));

	const bool wantsToBreakGlass = pNavChar->IsInCombat() && IsBlockedByBreakable();

	const float distThresh = wantsToBreakGlass ? 0.25f : 0.1f;
	const float rotThresh  = wantsToBreakGlass ? 0.8f : 0.99f;

	const bool distMatch   = remDist < distThresh;
	const bool orientMatch = orientDot > rotThresh;

	if (distMatch && orientMatch)
	{
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U64 IAiCoverController::CollectHitReactionStateFlags() const
{
	const CoverActionPack* pCoverActionPack = GetActionPack();
	if (!pCoverActionPack)
		return 0;

	U64 flags = 0;

	const CoverMode curMode = GetCurrentCoverMode();

	switch (curMode)
	{
	case kCoverModeEntering:
		if (m_entryDef.m_additiveHitReactions)
		{
			flags = DC::kHitReactionStateMaskAdditiveOnly;
		}
		else
		{
			flags |= DC::kHitReactionStateMaskMoving;
		}
		break;

	case kCoverModeExiting:
		{
			const StringId64 stateId = m_exitAction.GetTransitionIdOrFadeToStateId();
			if (stateId == SID("s_cover-exit^moving"))
			{
				flags |= DC::kHitReactionStateMaskMoving;
			}
			else
			{
				flags |= DC::kHitReactionStateMaskIdle;
			}
		}
		break;

	case kCoverModeStepOut:
		flags |= DC::kHitReactionStateMaskIdle;
		break;

	case kCoverModeAim:
		{
			const NavCharacter* pChar		 = GetCharacter();
			const AnimControl* pAnimControl	 = pChar ? pChar->GetAnimControl() : nullptr;
			const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
			const StringId64 curStateId		 = pBaseLayer ? pBaseLayer->CurrentStateId() : INVALID_STRING_ID_64;

			// Count aim transition states as idle for hit reactions
			if (curStateId != SID("s_cover-aim^idle") && curStateId != SID("s_cover-idle^aim"))
			{
				switch (m_coverType)
				{
				case CoverDefinition::kCoverStandLeft:
					flags |= DC::kHitReactionStateMaskCoverHighLeftAim;
					break;
				case CoverDefinition::kCoverStandRight:
					flags |= DC::kHitReactionStateMaskCoverHighRightAim;
					break;
				case CoverDefinition::kCoverCrouchLeft:
					flags |= m_shootSide ? DC::kHitReactionStateMaskCoverLowLeftAim
										 : DC::kHitReactionStateMaskCoverLowLeftOverAim;
					break;
				case CoverDefinition::kCoverCrouchRight:
					flags |= m_shootSide ? DC::kHitReactionStateMaskCoverLowRightAim
										 : DC::kHitReactionStateMaskCoverLowRightOverAim;
					break;
				case CoverDefinition::kCoverCrouchOver:
					flags |= DC::kHitReactionStateMaskCoverLowRightOverAim;
					break;
				}
				break;
			}
		}
		FALLTHROUGH;
	default:
		switch (m_coverType)
		{
		case CoverDefinition::kCoverStandLeft:		flags |= DC::kHitReactionStateMaskCoverHighLeft;	break;
		case CoverDefinition::kCoverStandRight:		flags |= DC::kHitReactionStateMaskCoverHighRight;	break;
		case CoverDefinition::kCoverCrouchLeft:		flags |= DC::kHitReactionStateMaskCoverLowLeft;		break;
		case CoverDefinition::kCoverCrouchRight:	flags |= DC::kHitReactionStateMaskCoverLowRight;	break;
		case CoverDefinition::kCoverCrouchOver:		flags |= DC::kHitReactionStateMaskCoverLowRight;	break;
		}
		break;
	}

	const NavCharacter* pChar = GetCharacter();
	if (pChar && pChar->IsInjured())
	{
		flags |= DC::kHitReactionStateMaskCoverInjured;
	}

	if (m_performanceAction.IsValid() && !m_performanceAction.IsDone())
	{
		flags |= m_performanceStateMask;
	}

	return flags;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::RequestAbortAction()
{
	if (IsBusy())
	{
		AiLogAnim(GetCharacter(), "IAiCoverController::RequestAbortAction()\n");

		if (!m_enterAction.IsDone())
		{
			m_enterAction.Reset();
		}
		if (!m_animAction.IsDone())
		{
			m_animAction.Reset();
		}
		if (!m_peekAction.IsDone())
		{
			m_peekAction.Reset();
		}
		if (!m_performanceAction.IsDone())
		{
			m_performanceAction.Reset();
			m_performanceStateMask = 0;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::ResolveEntry(const ActionPackResolveInput& input,
									  const ActionPack* pActionPack,
									  ActionPackEntryDef* pDefOut) const
{
	PROFILE_ACCUM(CoverCon_ResolveEntry);
	PROFILE(AI, CoverCon_ResolveEntry);

	if (FALSE_IN_FINAL_BUILD(g_aiGameOptions.m_cover.m_forceDefault))
		return false;

	bool validEntry = false;

	const Npc* pNpc = static_cast<const Npc*>(GetCharacter());
	const Locator& parentSpace = pNpc->GetParentSpace();
	const AnimControl* pAnimControl = pNpc->GetAnimControl();
	const NavControl* pNavControl = pNpc->GetNavControl();

	ANIM_ASSERT(pActionPack->GetType() == ActionPack::kCoverActionPack);

	if (!pDefOut)
		return false;

	pDefOut->m_apUserData = input.m_apUserData;

	const CoverActionPack::PreferredDirection dirPref = CoverActionPack::PreferredDirection(input.m_apUserData);

	const CoverActionPack* pCoverAp = static_cast<const CoverActionPack*>(pActionPack);
	const CoverDefinition::CoverType type = GetCoverTypeForNpc(pCoverAp->GetDefinition().m_coverType, dirPref);

	BoundFrame coverApRef = GetApReferenceForCover(pCoverAp, dirPref);

	ApEntry::CharacterState cs;
	MakeEntryCharacterState(input, pActionPack, &cs);

	const DC::ApEntryItemList* pDcEntryList = GetCoverEntries(type);
	if (!pDcEntryList)
		return false;

	const U32F kMaxEntries = 50;
	ApEntry::AvailableEntry availableEntries[kMaxEntries];

	const U32F numUsableEntries = ApEntry::ResolveEntry(cs, pDcEntryList, coverApRef, availableEntries, kMaxEntries);

	// this action pack may be in a different parent space than the Npc, so we first need to get the entry point in world space
	Locator entryLocWs;
	const Locator coverApRefWs = coverApRef.GetLocatorWs();

	// Overridden entry
	if ((m_enterAnimOverride != INVALID_STRING_ID_64)
		&& FindAlignFromApReference(pAnimControl, m_enterAnimOverride, coverApRefWs, &entryLocWs))
	{
		pDefOut->m_entryAnimId = m_enterAnimOverride;
		validEntry = true;
	}
	// Typical entry
	else if (numUsableEntries > 0)
	{
		const U32F selectedIndex = ApEntry::EntryWithLeastRotation(availableEntries, numUsableEntries, coverApRef);
		const ApEntry::AvailableEntry* pSelectedEntry = &availableEntries[selectedIndex];

		AI_ASSERT(pSelectedEntry);
		AI_ASSERT(pSelectedEntry->m_pDcDef);

		if (!ApEntry::ConstructEntryNavLocation(cs,
												pActionPack->GetRegisteredNavLocation(),
												*pSelectedEntry,
												&pDefOut->m_entryNavLoc))
		{
			return false;
		}

		pDefOut->m_apReference = pSelectedEntry->m_rotatedApRef;
		pDefOut->m_entryAnimId = pSelectedEntry->m_resolvedAnimId;
		pDefOut->m_pDcDef	   = pSelectedEntry->m_pDcDef;
		pDefOut->m_stopBeforeEntry	   = pSelectedEntry->m_pDcDef->m_flags & DC::kApEntryItemFlagsForceStop;
		pDefOut->m_preferredMotionType = pNpc->GetConfiguration().m_moveToCoverMotionType;
		pDefOut->m_phase = pSelectedEntry->m_phase;
		pDefOut->m_entryVelocityPs = pSelectedEntry->m_entryVelocityPs;
		pDefOut->m_mirror = pSelectedEntry->m_playMirrored;

		const Locator entryLocPs = pSelectedEntry->m_entryAlignPs;
		entryLocWs = pSelectedEntry->m_rotatedApRef.GetParentSpace().TransformLocator(entryLocPs);

		validEntry = true;
	}
	// Invalid entry
	else
	{
		pDefOut->m_preferredMotionType = pNpc->GetConfiguration().m_moveToCoverMotionType;
		pDefOut->m_apReference		   = coverApRef;
		pDefOut->m_entryAnimId		   = INVALID_STRING_ID_64;
		pDefOut->m_pDcDef = nullptr;
		pDefOut->m_stopBeforeEntry = true;
		pDefOut->m_phase = 0.0f;
		pDefOut->m_entryVelocityPs = kZero;

		const Scalar apRegPtDist = pNavControl->GetActionPackEntryDistance();

		entryLocWs = coverApRefWs;
		entryLocWs.SetTranslation(GetDefaultEntryPoint(coverApRefWs, apRegPtDist));

		pDefOut->m_entryNavLoc = pNpc->AsReachableNavLocationWs(entryLocWs.Pos(), NavLocation::Type::kNavPoly);
	}

	pDefOut->m_entryRotPs = parentSpace.UntransformLocator(entryLocWs).Rot();

	if (validEntry)
	{
		pDefOut->m_refreshTime	   = pNpc->GetCurTime();
		pDefOut->m_hResolvedForAp  = pActionPack;
		pDefOut->m_mtSubcategoryId = cs.m_mtSubcategory;
	}

	return validEntry;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void DebugDefaultRating(const Npc* pNpc,
							   U32F index,
							   const ApEntry::AvailableEntry& defaultEntry,
							   float rating,
							   const char* desc)
{
	STRIP_IN_FINAL_BUILD;

	if (!g_aiGameOptions.m_cover.m_debugDefaultRating)
		return;

	const Locator entryLocPs = defaultEntry.m_entryAlignPs;
	const ArtItemAnim* pEntryAnim = defaultEntry.m_anim.ToArtItem();

	PrimServerWrapper ps(pNpc->GetParentSpace());
	ps.EnableHiddenLineAlpha();
	ps.DrawCoordAxes(entryLocPs);
	ps.DrawString(entryLocPs.Pos(), StringBuilder<256>("%s : %0.4f %s", pEntryAnim->GetName(), rating, desc).c_str(), AI::IndexToColor(index), 0.5f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::ResolveDefaultEntry(const ActionPackResolveInput& input,
											 const ActionPack* pActionPack,
											 ActionPackEntryDef* pDefOut) const
{
	PROFILE(AI, CoverCon_ResolveDefEntry);

	AI_ASSERT(pDefOut);
	AI_ASSERT(pActionPack->GetType() == ActionPack::kCoverActionPack);

	if (!pDefOut)
		return false;

	pDefOut->m_apUserData = input.m_apUserData;

	const Npc* pNpc = static_cast<const Npc*>(GetCharacter());
	AnimControl* pAnimControl = pNpc->GetAnimControl();

	const CoverActionPack::PreferredDirection dirPref = CoverActionPack::PreferredDirection(input.m_apUserData);

	const CoverActionPack* pCoverAp = static_cast<const CoverActionPack*>(pActionPack);
	const CoverDefinition::CoverType type = GetCoverTypeForNpc(pCoverAp->GetDefinition().m_coverType, dirPref);
	BoundFrame coverApRef = GetApReferenceForCover(pCoverAp, dirPref);

	const DC::ApEntryItemList* pDcEntryList = GetCoverEntries(type);
	if (!pDcEntryList)
		return false;

	ApEntry::CharacterState cs;
	MakeEntryCharacterState(input, pActionPack, &cs);

	ApEntry::AvailableEntry defaultEntries[16];
	const U32F numDefaultEntries = ApEntry::ResolveDefaultEntry(cs, pDcEntryList, coverApRef, defaultEntries, 16);

	const Locator& alignPs = pNpc->GetLocatorPs();

	const INdAiLocomotionController* pLocomotionController = pNpc->GetAnimationControllers()->GetLocomotionController();
	const MotionType mt		= pLocomotionController->RestrictMotionType(input.m_motionType, false);
	const Vector alignFwPs	= GetLocalZ(alignPs.Rot());
	const bool strafing		= input.m_strafing && pLocomotionController->CanStrafe(mt);
	const Vector faceDirPs	= AsUnitVectorXz(pNpc->GetFacePositionPs() - alignPs.Pos(), kUnitZAxis);

	const PlayerSnapshot* pPlayer = GetPlayerSnapshot();
	const bool forCoverShare = cs.m_mtSubcategory == SID("hunker") && pPlayer;

	I32F bestEntryIndex = -1;
	float bestRating = -1.0f;

	for (U32F i = 0; i < numDefaultEntries; ++i)
	{
		const Locator entryLocPs = defaultEntries[i].m_entryAlignPs;
		const float xzDist		 = DistXz(entryLocPs.Pos(), alignPs.Pos());
		const float minDist		 = 0.5f;

		if ((xzDist >= defaultEntries[i].m_pDcDef->m_distMax) && (defaultEntries[i].m_pDcDef->m_distMax >= 0.0f))
		{
			continue;
		}

		if ((xzDist < defaultEntries[i].m_pDcDef->m_distMin) && (defaultEntries[i].m_pDcDef->m_distMin >= 0.0f))
		{
			continue;
		}

		const Vector entryFwPs = GetLocalZ(entryLocPs.Rot());
		const bool mirror = defaultEntries[i].m_playMirrored;

		float rating = -1.0f;

		const ArtItemAnim* pEntryAnim = defaultEntries[i].m_anim.ToArtItem();
		if (forCoverShare && pEntryAnim)
		{
			const Point playerPosWs = pPlayer->GetTranslation();
			const Point entryPosWs = pNpc->GetParentSpace().TransformPoint(entryLocPs.Pos());
			rating = Dist(playerPosWs, entryPosWs);

			DebugDefaultRating(pNpc, i, defaultEntries[i], rating, "[cover-share]");
		}
		else if (strafing)
		{
			rating = Dot(entryFwPs, faceDirPs);

			DebugDefaultRating(pNpc, i, defaultEntries[i], rating, "[strafe]");
		}
		else if (xzDist >= minDist)
		{
			const Vector approachDirPs = AsUnitVectorXz(entryLocPs.Pos() - alignPs.Pos(), kZero);
			rating = Dot(entryFwPs, approachDirPs);

			DebugDefaultRating(pNpc, i, defaultEntries[i], rating, "[approach]");
		}
		else
		{
			rating = Dot(entryFwPs, alignFwPs);

			DebugDefaultRating(pNpc, i, defaultEntries[i], rating, "[facing]");
		}

		if (rating > bestRating)
		{
			bestRating = rating;
			bestEntryIndex = i;
		}
	}

	if (bestEntryIndex < 0 || bestEntryIndex >= numDefaultEntries)
		return false;

	const ApEntry::AvailableEntry& defaultEntry = defaultEntries[bestEntryIndex];

	AI_ASSERT(defaultEntry.m_pDcDef);
	if (!defaultEntry.m_pDcDef)
		return false;

	const Locator defaultEntryLocPs = defaultEntry.m_entryAlignPs;

	BoundFrame rotatedApRef = defaultEntry.m_rotatedApRef;
	Locator entryLocPs = defaultEntryLocPs;
	//entryLocPs.SetPos(PointFromXzAndY(entryLocPs.Pos(), pNpc->GetTranslationPs()));

	if (!ApEntry::ConstructEntryNavLocation(cs,
											pActionPack->GetRegisteredNavLocation(),
											defaultEntry,
											&pDefOut->m_entryNavLoc))
	{
		return false;
	}

	const bool forceStop = (defaultEntry.m_pDcDef->m_flags & DC::kApEntryItemFlagsForceStop);

	pDefOut->m_apReference			= rotatedApRef;
	pDefOut->m_entryAnimId			= defaultEntry.m_resolvedAnimId;
	pDefOut->m_pDcDef				= defaultEntry.m_pDcDef;
	pDefOut->m_stopBeforeEntry		= forceStop;
	pDefOut->m_preferredMotionType	= pNpc->GetConfiguration().m_moveToCoverMotionType;
	pDefOut->m_phase				= defaultEntry.m_phase;
	pDefOut->m_entryRotPs			= entryLocPs.Rot();
	pDefOut->m_entryVelocityPs		= kZero;
	pDefOut->m_mirror				= defaultEntry.m_playMirrored;
	pDefOut->m_hResolvedForAp		= pActionPack;
	pDefOut->m_refreshTime			= pNpc->GetCurTime();
	pDefOut->m_mtSubcategoryId		= cs.m_mtSubcategory;
	pDefOut->m_recklessStopping		= !forCoverShare;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::UpdateEntry(const ActionPackResolveInput& input,
									 const ActionPack* pActionPack,
									 ActionPackEntryDef* pDefOut) const
{
	PROFILE(AI, CoverCon_UpdateEntry);

	const NavCharacter* pChar = GetCharacter();
	const Npc* pNpc = (const Npc*)pChar;
	const Locator& parentSpace = pChar->GetParentSpace();

	AI_ASSERT(pDefOut);
	AI_ASSERT(pActionPack->GetType() == ActionPack::kCoverActionPack);

	pDefOut->m_hResolvedForAp = nullptr;

	const CoverActionPack::PreferredDirection dirPref = CoverActionPack::PreferredDirection(input.m_apUserData);

	const CoverActionPack* pCoverAp	   = static_cast<const CoverActionPack*>(pActionPack);
	const BoundFrame defaultCoverApRef = GetApReferenceForCover(pCoverAp, dirPref);

	BoundFrame coverApRef = defaultCoverApRef;

	ApEntry::CharacterState cs;
	cs.m_mtSubcategory = pDefOut->m_mtSubcategoryId;
	MakeEntryCharacterState(input, pActionPack, &cs);

	if (!CanShareCoverWithPlayer(pChar) && pActionPack->IsPlayerBlocked())
	{
		AiLogAnimDetails(pChar, "IAiCoverController::UpdateEntry() - no longer valid, it is player blocked\n");
		return false;
	}

	const DC::ApEntryItem* pEntry = pDefOut->m_pDcDef;

	if (!pEntry)
	{
		return false;
	}

	const bool isDefaultEntry = (pEntry->m_flags & DC::kApEntryItemFlagsDefaultEntry) != 0;

	if (isDefaultEntry && (pDefOut->m_mtSubcategoryId == SID("hunker")))
	{
		return ResolveDefaultEntry(input, pActionPack, pDefOut);
	}

	if (isDefaultEntry && !pNpc->IsBusy() && (Dist(pNpc->GetTranslationPs(), pDefOut->m_entryNavLoc.GetPosPs()) > 0.5f))
	{
		return ResolveDefaultEntry(input, pActionPack, pDefOut);
	}

	const bool curHunker = pDefOut->m_mtSubcategoryId == SID("hunker");
	const bool wantHunker = cs.m_mtSubcategory == SID("hunker");
	if (curHunker != wantHunker)
	{
		AiLogAnimDetails(pChar,
						 "IAiCoverController::UpdateEntry() - no longer valid, want hunker went from %s to %s\n",
						 curHunker ? "true" : "false",
						 wantHunker ? "true" : "false");
		return false;
	}

	ApEntry::AvailableEntry updatedEntry;
	if (!ApEntry::ConstructEntry(cs, pEntry, defaultCoverApRef, &updatedEntry))
	{
		AiLogAnimDetails(pChar, "IAiCoverController::UpdateEntry() - no longer valid, ConstructEntry() failed\n");
		return false;
	}

	if (pDefOut->m_entryAnimId != updatedEntry.m_resolvedAnimId)
	{
		AiLogAnimDetails(pChar,
						 "IAiCoverController::UpdateEntry() - no longer valid, overlays changed entry animation\n");
		return false;
	}

	const ApEntry::GatherMode mode = isDefaultEntry ? ApEntry::GatherMode::kDefault : ApEntry::GatherMode::kResolved;
	ApEntry::FlagInvalidEntries(cs, mode, defaultCoverApRef, &updatedEntry, 1);

	if (updatedEntry.m_errorFlags.m_invalidEntryAngle)
	{
		AiLogAnimDetails(pChar, "IAiCoverController::UpdateEntry() - no longer valid, IsEntryAngleValid() failed\n");
		return false;
	}

	if (updatedEntry.m_errorFlags.m_invalidFacingDir)
	{
		if (pChar->IsBuddyNpc())
			AiLogAnim(pChar, "IAiCoverController::UpdateEntry() - no longer valid, facing angle failed\n");
		return false;
	}

	if (updatedEntry.m_errorFlags.m_noClearLineOfMotion)
	{
		AiLogAnimDetails(pChar, "IAiCoverController::UpdateEntry() - no longer valid, no clear motion!\n");
		return false;
	}

	if (updatedEntry.m_errorFlags.m_wrongMotionSubCategory)
	{
		AiLogAnimDetails(pChar, "IAiCoverController::UpdateEntry() - no longer valid, wrong motion subcat!\n");
		return false;
	}

	if (updatedEntry.m_errorFlags.m_all)
	{
		AiLogAnimDetails(pChar,
						 "IAiCoverController::UpdateEntry() - no longer valid, other. Flags = %d\n",
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

	const bool forceStop = (updatedEntry.m_pDcDef->m_flags & DC::kApEntryItemFlagsForceStop);

	Locator entryLocPs = updatedEntry.m_entryAlignPs;
	entryLocPs.SetPos(PointFromXzAndY(entryLocPs.Pos(), pNpc->GetTranslationPs()));

	pDefOut->m_entryRotPs  = entryLocPs.Rot();
	pDefOut->m_apReference = updatedEntry.m_rotatedApRef;
	pDefOut->m_phase	   = updatedEntry.m_phase;
	pDefOut->m_entryVelocityPs = updatedEntry.m_entryVelocityPs;
	pDefOut->m_refreshTime	   = pChar->GetCurTime();
	pDefOut->m_stopBeforeEntry = forceStop;
	pDefOut->m_mirror = updatedEntry.m_playMirrored;
	pDefOut->m_mtSubcategoryId = cs.m_mtSubcategory;
	pDefOut->m_hResolvedForAp  = pActionPack;
	pDefOut->m_apUserData	   = input.m_apUserData;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const CoverActionPack* IAiCoverController::GetActionPack() const
{
	return CoverActionPack::FromActionPack(m_hActionPack.ToActionPack());
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame IAiCoverController::GetApReferenceForCurrentCover() const
{
	return ::GetApReferenceForCover(GetActionPack(), m_coverType, m_sidewaysOffset);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::GrenadeDraw(Point targetPos, bool spawnHeldGrenadeIfNotPresent)
{
	ANIM_ASSERT(IsReasonable(targetPos));

	NavCharacter* pNavChar = GetCharacter();
	const CoverActionPack* pActionPack = GetActionPack();

	if (!pActionPack)
	{
		return false;
	}

	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	if (!pAnimControl || !pBaseLayer)
	{
		return false;
	}

	if ((m_cachedCoverMode == kCoverModeInvalid) || pBaseLayer->AreTransitionsPending()
		|| (m_cachedCoverMode == kCoverModeExiting))
	{
		return false;
	}

	const StringId64 animId = SID("cover-grenade-draw");
	const StringId64 stateId = SID("s_cover-grenade-draw");
	const SkeletonId skelId = pNavChar->GetSkeletonId();

	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animId).ToArtItem();
	if (!pAnim)
		return false;

	const Point charPos = pNavChar->GetTranslation();
	const Vector toTargetXzDir = Normalize(targetPos - charPos);
	const Locator coverLoc = GetApReferenceForCurrentCover().GetLocator();

	Locator charAlignEnd;
	if (!FindAlignFromApReference(skelId, pAnim, 1.0f, coverLoc, SID("apReference"), &charAlignEnd))
		return false;

	charAlignEnd.SetRotation(QuatFromLookAt(toTargetXzDir, kUnitYAxis));

	Locator apRefEnd;
	if (!FindApReferenceFromAlign(skelId, pAnim, charAlignEnd, &apRefEnd, 1.0f))
		return false;

	BoundFrame apRefBf = pNavChar->GetBoundFrame();
	apRefBf.SetLocator(apRefEnd);

	const bool leftHanded = m_coverType == CoverDefinition::kCoverStandLeft;

	if (spawnHeldGrenadeIfNotPresent)
	{
		StringId64 grenadeId = INVALID_STRING_ID_64;
		if (const Npc* pNpc = Npc::FromProcess(pNavChar))
		{
			if (const DC::AiWeaponLoadout* pWeaponLoadout = pNpc->GetWeaponLoadout())
			{
				grenadeId = pWeaponLoadout->m_grenade;
			}
		}
		pNavChar->SpawnHeldGrenade(grenadeId);
	}

	// Set grenade overlay before throw anim.
	pNavChar->GetAnimationControllers()->GetWeaponController()->UpdateOverlays();

	m_animAction.Reset();
	m_enterAction.Reset();
	m_exitAction.Reset();
	m_peekAction.Reset();
	m_performanceAction.Reset();
	m_throwGrenadeAction.Reset();
	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	AI::SetPluggableAnim(pNavChar, animId, leftHanded);

	const F32 animDuration = GetDuration(pAnim);
	const F32 animBlendTime = 0.1f;
	const F32 motionBlendTime = animDuration;

	FadeToStateParams params;
	params.m_apRef		  = apRefBf;
	params.m_apRefValid	  = true;
	params.m_animFadeTime = animBlendTime;
	params.m_motionFadeTime = motionBlendTime;

	m_throwGrenadeAction.ClearSelfBlendParams();
	m_throwGrenadeAction.FadeToState(pAnimControl, stateId, params, AnimAction::kFinishOnAnimEndEarly);

	m_grenadeDrawn = false;
	m_doingGrenadeDraw = true;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::GrenadeToss(const bool spawnHeldGrenadeIfNotPresent /* = true */)
{
	NavCharacter* pNavChar = GetCharacter();

	const ActionPack* pActionPack = GetActionPack();

	if (!pActionPack || (pActionPack->GetType() != ActionPack::kCoverActionPack))
	{
		return false;
	}

	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

	if (!pAnimControl || !pBaseLayer)
	{
		return false;
	}

	if ((GetCurrentCoverMode() == kCoverModeInvalid) || pBaseLayer->AreTransitionsPending())
	{
		return false;
	}

	const StringId64 animId = SID("cover-grenade-toss");
	const StringId64 stateId = SID("s_cover-grenade-toss");

	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animId).ToArtItem();
	if (!pAnim)
		return false;

	const BoundFrame apRef = GetApReferenceForCurrentCover();

	const bool leftHanded = m_coverType == CoverDefinition::kCoverStandLeft;
	if (spawnHeldGrenadeIfNotPresent)
	{
		StringId64 grenadeId = INVALID_STRING_ID_64;
		if (const Npc* pNpc = Npc::FromProcess(pNavChar))
		{
			if (const DC::AiWeaponLoadout* pWeaponLoadout = pNpc->GetWeaponLoadout())
			{
				grenadeId = pWeaponLoadout->m_grenade;
			}
		}
		pNavChar->SpawnHeldGrenade(grenadeId, leftHanded);
	}

	// Set grenade overlay before throw anim.
	pNavChar->GetAnimationControllers()->GetWeaponController()->UpdateOverlays();

	m_animAction.Reset();
	m_enterAction.Reset();
	m_exitAction.Reset();
	m_peekAction.Reset();
	m_performanceAction.Reset();
	m_throwGrenadeAction.Reset();
	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	AI::SetPluggableAnim(pNavChar, animId, leftHanded);

	const F32 animDuration = GetDuration(pAnim);
	const F32 animBlendTime = 0.1f;
	const F32 motionBlendTime = animDuration;

	FadeToStateParams params;
	params.m_apRef		  = apRef;
	params.m_apRefValid	  = true;
	params.m_animFadeTime = animBlendTime;
	params.m_motionFadeTime = 0.4f;

	m_throwGrenadeAction.FadeToState(pAnimControl, stateId, params, AnimAction::kFinishOnNonTransitionalStateReached);

	m_doingGrenadeToss = true;
	m_grenadeReleased = false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::AbortGrenadeToss()
{
	if (GetCurrentCoverMode() == kCoverModeHitReaction)
		return;

	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const bool transitionValid = pBaseLayer ? pBaseLayer->IsTransitionValid(SID("abort")) : false;

	if (transitionValid)
	{
		m_animAction.ClearSelfBlendParams();
		m_animAction.Request(pAnimControl, SID("abort"), AnimAction::kFinishOnNonTransitionalStateReached);
	}

	// Set grenade overlay before throw anim.
	pNavChar->GetAnimationControllers()->GetWeaponController()->UpdateOverlays();
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 IAiCoverController::GetDestinationState(CoverDefinition::CoverType coverType) const
{
	if (m_entryDef.m_pDcDef && m_entryDef.m_pDcDef->m_mtSubcategory == SID("hunker"))
		return SID("s_cover-hunker");

	return SID("s_cover-idle");
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::ForceBlendToDestinationState(CoverDefinition::CoverType coverType)
{
	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;

	m_coverType = coverType;

	const StringId64 animStateId = GetDestinationState(coverType);
	const BoundFrame coverApRef	 = GetApReferenceForCurrentCover();

	FadeToStateParams params;
	params.m_stateStartPhase = 0.0f;
	params.m_apRef		  = coverApRef;
	params.m_apRefValid	  = true;
	params.m_animFadeTime = 0.4f;
	params.m_motionFadeTime	 = 0.4f;
	params.m_freezeSrcState	 = false;
	params.m_freezeDestState = false;
	params.m_blendType		 = DC::kAnimCurveTypeEaseIn;
	params.m_newInstBehavior = FadeToStateParams::kSpawnNewTrack;
	params.m_preventBlendTimeOverrun = false;

	m_enterAction.FadeToState(pAnimControl, animStateId, params, AnimAction::kFinishOnTransitionTaken);
	m_enterAction.ClearSelfBlendParams();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::OnBulletHitNear()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::IsInOpenState() const
{
	const NavCharacter* pNavChar				= GetCharacter();
	const AnimControl* pAnimControl				= pNavChar ? pNavChar->GetAnimControl() : nullptr;
	const AnimStateLayer* pBaseLayer			= pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const AnimStateInstance* pAnimStateInstance	= pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;

	if (!pAnimStateInstance)
	{
		return false;
	}

	const StringId64 stateId = pBaseLayer->CurrentStateInstance()->GetStateName();
	return stateId == SID("s_cover-open-idle")			||
		   stateId == SID("s_cover-open-aim")			||
		   stateId == SID("s_cover-open-grenade-draw")	||
		   stateId == SID("s_cover-open-grenade-toss");
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::CanAim() const
{
	const NavCharacter* pNavChar = GetCharacter();
	const NdAnimControllerConfig* pConfig = pNavChar->GetAnimControllerConfig();

	bool canAim = true;

	switch (m_coverType)
	{
	case CoverDefinition::kCoverStandLeft:
	case CoverDefinition::kCoverStandRight:
		if (pConfig && pConfig->m_cover.m_standingAimStepOutOnly)
		{
			canAim = m_canStepOut;
		}
		break;
	}

	if (canAim && !pNavChar->HasFirearmInHand())
	{
		canAim = false;
	}

	//if (canAim)
	//{
	//	if (const Npc* pNpc = Npc::FromProcess(pNavChar))
	//	{
	//		if (const AiEncounterCoordinator* pCoord = pNpc->GetEncounterCoordinator())
	//		{
	//			if (pCoord->IsMemberInRole(pNpc, SID("Healer")))
	//				canAim = false;
	//		}
	//	}
	//}

	return canAim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::ConfigureCharacter(Demeanor demeanor,
											const DC::NpcDemeanorDef* pDemeanorDef,
											const NdAiAnimationConfig* pAnimConfig)
{
	if (IsBusy())
		return;

	switch (m_cachedCoverMode)
	{
	case kCoverModeExiting:
	case kCoverModeInvalid:
		break;
	default:
		ApplyOverlays();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::ApplyOverlays()
{
	if (m_cachedCoverMode == kCoverModeInvalid)
		return;

	const NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	if (!pBaseLayer)
		return;

	if (pBaseLayer->AreTransitionsPending())
		return;

	const CoverActionPack* pCoverAp = m_hActionPack.ToActionPack<CoverActionPack>();
	if (!pCoverAp)
		return;

	switch (m_cachedCoverMode)
	{
	case kCoverModeIdle:
		{
			FadeToStateParams params;
			params.m_apRef		= GetApReferenceForCurrentCover();
			params.m_apRefValid = true;
			params.m_stateStartPhase = 0.0f;
			params.m_animFadeTime	 = 0.35f;
			params.m_motionFadeTime	 = 0.35f;
			params.m_blendType		 = DC::kAnimCurveTypeEaseIn;
			params.m_preventBlendTimeOverrun = !pBaseLayer->IsTransitionValid(SID("auto"));
			params.m_newInstBehavior		 = FadeToStateParams::kSpawnNewTrack;
			m_animAction.FadeToState(pAnimControl,
									 SID("s_cover-idle"),
									 params,
									 AnimAction::kFinishOnNonTransitionalStateReached);
		}
		break;
	case kCoverModeAim:
		{
			FadeToStateParams params;
			params.m_apRef		= GetApReferenceForCurrentCover();
			params.m_apRefValid = true;
			params.m_stateStartPhase = 0.0f;
			params.m_animFadeTime	 = 0.35f;
			params.m_motionFadeTime	 = 0.35f;
			params.m_blendType		 = DC::kAnimCurveTypeEaseIn;
			params.m_preventBlendTimeOverrun = !pBaseLayer->IsTransitionValid(SID("auto"));
			params.m_newInstBehavior		 = FadeToStateParams::kSpawnNewTrack;
			m_animAction.FadeToState(pAnimControl,
									 SID("s_cover-aim"),
									 params,
									 AnimAction::kFinishOnNonTransitionalStateReached);
		}
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::Idle(bool restoreAp)
{
	if (GetCurrentCoverMode() == kCoverModeHitReaction)
		return;

	const CoverActionPack* pCoverAp = m_hActionPack.ToActionPack<CoverActionPack>();
	if (!pCoverAp)
		return;

	const BoundFrame coverApRef = GetApReferenceForCurrentCover();

	const NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	if (!pBaseLayer)
		return;

	if (restoreAp)
	{
		const StringId64 stateId = SID("s_cover-idle");

		const AnimStateInstance* pTopInstance = pBaseLayer->CurrentStateInstance();
		const F32 startPhase = pTopInstance && (pTopInstance->GetState()->m_name.m_symbol == stateId)
								   ? pTopInstance->GetPhase()
								   : 0.0f;

		FadeToStateParams params;
		params.m_apRef		= coverApRef;
		params.m_apRefValid = true;
		params.m_stateStartPhase = startPhase;
		params.m_animFadeTime	 = 0.35f;
		params.m_motionFadeTime	 = 0.35f;
		params.m_blendType		 = DC::kAnimCurveTypeEaseIn;
		params.m_preventBlendTimeOverrun = !pBaseLayer->IsTransitionValid(SID("auto"));
		params.m_newInstBehavior		 = FadeToStateParams::kSpawnNewTrack;
		m_animAction.FadeToState(pAnimControl,
								 stateId,
								 params,
								 AnimAction::kFinishOnNonTransitionalStateReached);

		return;
	}

	const StringId64 transitionId = SID("cover-idle");
	const bool transitionValid = pBaseLayer ? pBaseLayer->IsTransitionValid(transitionId) : false;

	if (transitionValid)
	{
		AiLogAnim(pNavChar, "CoverController requesting %s\n", DevKitOnly_StringIdToString(transitionId));

		m_animAction.ClearSelfBlendParams();
		m_animAction.Request(pAnimControl, transitionId, AnimAction::kFinishOnNonTransitionalStateReached, &coverApRef);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::Aim()
{
	if (GetCurrentCoverMode() == kCoverModeHitReaction)
		return;

	if (!CanAim())
		return;

	const StringId64 transitionId = SID("aim");

	const NavCharacter* pNavChar	= GetCharacter();
	AnimControl* pAnimControl		= pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer		= pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const bool transitionValid		= pBaseLayer ? pBaseLayer->IsTransitionValid(transitionId) : false;

	if (transitionValid)
	{
		AiLogAnim(pNavChar, "CoverController requesting %s\n", DevKitOnly_StringIdToString(transitionId));

		const BoundFrame coverApRef = GetApReferenceForCurrentCover();
		m_animAction.ClearSelfBlendParams();
		m_animAction.Request(pAnimControl, transitionId, AnimAction::kFinishOnNonTransitionalStateReached, &coverApRef);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::AimReassess()
{
	if (GetCurrentCoverMode() == kCoverModeHitReaction)
		return;

	const NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const bool transitionValid = pBaseLayer ? pBaseLayer->IsTransitionValid(SID("reassess")) : false;

	if (transitionValid)
	{
		AiLogAnim(pNavChar, "Requested AimReassess\n");

		const BoundFrame coverApRef = GetApReferenceForCurrentCover();
		m_animAction.ClearSelfBlendParams();
		m_animAction.Request(pAnimControl,
							 SID("reassess"),
							 AnimAction::kFinishOnNonTransitionalStateReached,
							 &coverApRef);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::AimFire()
{
	if (GetCurrentCoverMode() != kCoverModeAim)
		return false;

	const StringId64 animId = SID("cover-aim-fire");
	if (INVALID_STRING_ID_64 == animId)
		return false;

	const NavCharacter* pChar		= GetCharacter();
	AnimControl* pAnimControl		= pChar ? pChar->GetAnimControl() : nullptr;
	AnimSimpleLayer* pFireAddLayer	= pAnimControl ? pAnimControl->GetSimpleLayerById(SID("weapon-additive")) : nullptr;

	if (!pFireAddLayer || !pAnimControl->LookupAnim(animId).ToArtItem())
		return false;

	AnimSimpleLayer::FadeOutOnCompleteParams params;
	params.m_enabled	= true;
	params.m_fadeTime	= 0.2f;
	params.m_blendType	= DC::kAnimCurveTypeEaseOut;

	AiLogAnim(pChar, "Requested AimFire\n");

	if (!pFireAddLayer->RequestFadeToAnim(animId, params))
	{
		MsgAnimErr("Npc '%s' tried to play cover fire additive '%s' that doesn't exist\n",
				   pChar->GetName(),
				   DevKitOnly_StringIdToString(animId));
		AiLogAnim(pChar,
				  "Tried to play cover fire additive '%s' that doesn't exist\n",
				  DevKitOnly_StringIdToString(animId));
		return false;
	}

	pFireAddLayer->Fade(1.0f, 0.0f);
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::Peek()
{
	if (GetCurrentCoverMode() == kCoverModeHitReaction)
		return;

	const NavCharacter* pNavChar	= GetCharacter();
	AnimControl* pAnimControl		= pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer		= pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const bool transitionValid		= pBaseLayer ? pBaseLayer->IsTransitionValid(SID("peek")) : false;

	if (transitionValid)
	{
		AiLogAnim(pNavChar, "Requested Peek\n");

		const BoundFrame coverApRef = GetApReferenceForCurrentCover();
		m_peekAction.Request(pAnimControl, SID("peek"), AnimAction::kFinishOnNonTransitionalStateReached, &coverApRef);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::IsBlockedByBreakable() const
{
	if (const CoverActionPack* pCoverAp = GetActionPack())
	{
		if (pCoverAp->GetBlockingRigidBody(CoverActionPack::kBlockingOver).HandleValid())
		{
			return true;
		}
		else if (pCoverAp->GetBlockingRigidBody(CoverActionPack::kBlockingSide).HandleValid())
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 IAiCoverController::DoCoverPerformance(StringId64 performanceId,
												  FadeToStateParams params,
												  DC::HitReactionStateMask hitReactionStateMask)
{
	StringId64 resolvedAnimId = INVALID_STRING_ID_64;

	const CoverMode curMode = GetCurrentCoverMode();
	switch (curMode)
	{
	case kCoverModeHitReaction:
	case kCoverModeInvalid:
		return resolvedAnimId;
	}

	const NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const bool transitionValid = pBaseLayer ? pBaseLayer->IsTransitionValid(SID("performance")) : false;

	if (transitionValid || (curMode == kCoverModeIdle))
	{
		AiLogAnim(pNavChar, "Requested Performance '%s'\n", DevKitOnly_StringIdToString(performanceId));

		if (!params.m_apRefValid)
		{
			const BoundFrame coverApRef = GetApReferenceForCurrentCover();

			params.m_apRef = coverApRef;
			params.m_apRefValid = true;
		}
		resolvedAnimId = pAnimControl->LookupAnimId(performanceId);

		if (resolvedAnimId != INVALID_STRING_ID_64)
		{
			AI::SetPluggableAnim(pNavChar, performanceId);
			m_performanceAction.FadeToState(pAnimControl,
											SID("s_cover-performance"),
											params,
											AnimAction::kFinishOnNonTransitionalStateReached);
			m_performanceStateMask = hitReactionStateMask;
		}
	}

	return resolvedAnimId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::IdleFire()
{
	if (GetCurrentCoverMode() == kCoverModeHitReaction)
		return;

	const NavCharacter* pNavChar	= GetCharacter();
	AnimControl* pAnimControl		= pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer		= pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const bool transitionValid		= pBaseLayer ? pBaseLayer->IsTransitionValid(SID("idle-fire")) : false;

	if (transitionValid)
	{
		AiLogAnim(pNavChar, "Requested IdleFire\n");

		const BoundFrame coverApRef = GetApReferenceForCurrentCover();
		m_animAction.ClearSelfBlendParams();
		m_animAction.Request(pAnimControl, SID("idle-fire"), AnimAction::kFinishOnNonTransitionalStateReached, &coverApRef);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::StepOut()
{
	const NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const bool transitionValid = pBaseLayer ? pBaseLayer->IsTransitionValid(SID("step-out")) : false;

	if (!transitionValid)
	{
		return false;
	}

	AiLogAnim(pNavChar, "Requested StepOut\n");

	const BoundFrame coverApRef = GetApReferenceForCurrentCover();
	m_animAction.ClearSelfBlendParams();
	const bool res = m_animAction.Request(pAnimControl,
										  SID("step-out"),
										  AnimAction::kFinishOnNonTransitionalStateReached,
										  &coverApRef);

	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::Share(float startAnimPhase)
{
	const NavCharacter* pNavChar = GetCharacter();
	const CoverActionPack* pActionPack = GetActionPack();

	if (!pActionPack)
		return;

	AiLogAnim(pNavChar,
			  "Requested Cover Share. nav state %s, startphase %g\n",
			  DevKitOnly_StringIdToString(pNavChar->GetNavStateMachine()->GetStateId()),
			  startAnimPhase);

	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const bool transitionValid = pBaseLayer ? pBaseLayer->IsTransitionValid(SID("share")) : false;

	if (transitionValid)
	{
		const BoundFrame coverApRef = GetApReferenceForCurrentCover();

		m_animAction.ClearSelfBlendParams();
		m_animAction.Request(pAnimControl,
							 SID("share"),
							 AnimAction::kFinishOnNonTransitionalStateReached,
							 &coverApRef,
							 startAnimPhase);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::Unshare(float startAnimPhase)
{
	const NavCharacter* pNavChar = GetCharacter();
	const CoverActionPack* pActionPack = GetActionPack();

	if (!pActionPack)
		return;

	AiLogAnim(pNavChar,
			  "Requested Cover Unshare. nav state %s, startphase %g\n",
			  DevKitOnly_StringIdToString(pNavChar->GetNavStateMachine()->GetStateId()),
			  startAnimPhase);

	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const bool transitionValid = pBaseLayer ? pBaseLayer->IsTransitionValid(SID("unshare")) : false;

	if (transitionValid)
	{
		const BoundFrame coverApRef = GetApReferenceForCurrentCover();

		m_animAction.ClearSelfBlendParams();
		m_animAction.Request(pAnimControl,
							 SID("unshare"),
							 AnimAction::kFinishOnNonTransitionalStateReached,
							 &coverApRef,
							 startAnimPhase);
	}
}

/// --------------------------------------------------------------------------------------------------------------- //
bool IAiCoverController::IsSharing() const
{
	const IAiCoverController::CoverMode coverMode = GetCurrentCoverMode();
	return coverMode == kCoverModeHunker;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::ApEntryItemList* IAiCoverController::GetCoverEntries(CoverDefinition::CoverType coverType,
															   StringId64 setIdOverride /* = INVALID_STRING_ID_64 */) const
{
	const DC::ApEntryItemList* pRet = nullptr;
	NavCharacter* pNavChar = GetCharacter();

	if (setIdOverride != INVALID_STRING_ID_64)
	{
		pRet = GetEntriesMap<DC::ApEntryItemList>(setIdOverride, coverType);
	}
	else if (const NdAnimControllerConfig* pConfig = pNavChar->GetAnimControllerConfig())
	{
		pRet = GetEntriesMap<DC::ApEntryItemList>(pConfig->m_cover.m_entryDefsSetId, coverType);
	}

	return pRet;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::ApExitAnimList* IAiCoverController::GetExits(CoverDefinition::CoverType coverType) const
{
	const NdAnimControllerConfig* pConfig = GetCharacter()->GetAnimControllerConfig();

	const CoverMode curMode = GetCurrentCoverMode();
	if (curMode == kCoverModeAim && !m_shootSide && coverType != CoverDefinition::kCoverCrouchOver)
	{
		coverType = CoverDefinition::kCoverCrouchOver;
	}

	return GetExitsMap<DC::ApExitAnimList>(pConfig->m_cover.m_exitDefsSetId, curMode, coverType);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::ApEntryItemList* IAiCoverController::GetTransfers(CoverDefinition::CoverType coverType) const
{
	const AnimControllerConfig* pConfig = static_cast<const AnimControllerConfig*>(GetCharacter()->GetAnimControllerConfig());

	// TODO: handle cover share for cover transfers
	return GetEntriesMap<DC::ApEntryItemList>(pConfig->m_gameCover.m_transferSetId, coverType);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::IsAimingFirearm() const
{
	return m_cachedCoverMode == kCoverModeAim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 IAiCoverController::GetFlinchGestureName(bool smallFlinch) const
{
	switch (m_cachedCoverMode)
	{
	case kCoverModeEntering:	return smallFlinch ? SID("cover-enter-flinch-small-gesture")		: SID("cover-enter-flinch-gesture");
	case kCoverModeIdle:		return smallFlinch ? SID("cover-idle-flinch-small-gesture")			: SID("cover-idle-flinch-gesture");
	case kCoverModeAim:			return smallFlinch ? SID("cover-aim-flinch-small-gesture")			: SID("cover-aim-flinch-gesture");
	case kCoverModePeek:		return smallFlinch ? SID("cover-peek-flinch-small-gesture")			: SID("cover-peek-flinch-gesture");
	case kCoverModeIdleFire:	return smallFlinch ? SID("cover-idle-fire-flinch-small-gesture")	: SID("cover-idle-fire-flinch-gesture");
	case kCoverModeHunker:		return smallFlinch ? SID("cover-hunker-flinch-small-gesture")		: SID("cover-hunker-flinch-gesture");
	case kCoverModeExiting:		return smallFlinch ? SID("cover-exit-flinch-small-gesture")			: SID("cover-exit-flinch-gesture");
	case kCoverModeDraw:		return smallFlinch ? SID("cover-draw-flinch-small-gesture")			: SID("cover-draw-flinch-gesture");
	case kCoverModeThrow:		return smallFlinch ? SID("cover-throw-flinch-small-gesture")		: SID("cover-throw-flinch-gesture");
	case kCoverModeHitReaction:	return smallFlinch ? SID("cover-reaction-flinch-small-gesture")		: SID("cover-reaction-flinch-gesture");
	case kCoverModePerformance:	return smallFlinch ? SID("cover-performance-flinch-small-gesture")	: SID("cover-performance-flinch-gesture");
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::SetShootSide(bool shootSide)
{
	if (shootSide != m_shootSide)
	{
		const NavCharacter* pNavChar = GetCharacter();
		AiLogAnim(pNavChar, "IAiCoverController - ShootSide is now %s, refreshing overlays\n", shootSide ? "TRUE" : "FALSE");
		m_shootSide = shootSide;

		RefreshCoverOverlays();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::EnableCoverShare(const Locator& coverShareLocWs)
{
	if (IsMotionMatchingEnterComplete(false))
		return;

	if (AiMmActionPackInterface* pAimActionPackInterface = GetMotionMatchInterface())
	{
		pAimActionPackInterface->EnableCoverShare(coverShareLocWs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::DisableCoverShare()
{
	if (IsMotionMatchingEnterComplete(false))
		return;

	AiMmActionPackInterface* pAimActionPackInterface = GetMotionMatchInterface();
	if (!pAimActionPackInterface)
		return;

	pAimActionPackInterface->DisableCoverShare();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ValidateCoverType(const StringId64 enterAnimId, const CoverDefinition::CoverType ct)
{
	STRIP_IN_FINAL_BUILD_VALUE(true);

	switch (enterAnimId.GetValue())
	{
	// stand right
	// (npc-p-cover-stand-r-hunker-enter-idle-l	:valid-demeanors (ambient uneasy aggressive search crawl crouch) :phase-range (0.1 0.15) :mt-subcategory (hunker) :blend-in (0.5 0.5 ease-in) :self-blend (0.3 1.0 ease-out) :flags (default-entry force-stop))
	// (npc-p-cover-stand-r-hunker-enter-idle-r	:valid-demeanors (ambient uneasy aggressive search crawl crouch) :phase-range (0.1 0.15) :mt-subcategory (hunker) :blend-in (0.5 0.5 ease-in) :self-blend (0.3 1.0 ease-out) :flags (default-entry force-stop))
	case SID_VAL("npc-p-cover-stand-r-hunker-enter-idle-l"):
	case SID_VAL("npc-p-cover-stand-r-hunker-enter-idle-r"):
		return ct == CoverDefinition::kCoverStandRight;

	// stand left
	// (npc-p-cover-stand-l-hunker-enter-idle-l	:valid-demeanors (ambient uneasy aggressive search crawl crouch) :phase-range (0.1 0.15) :mt-subcategory (hunker) :blend-in (0.5 0.5 ease-in) :self-blend (0.3 1.0 ease-out) :flags (default-entry force-stop))
	// (npc-p-cover-stand-l-hunker-enter-idle-r	:valid-demeanors (ambient uneasy aggressive search crawl crouch) :phase-range (0.1 0.15) :mt-subcategory (hunker) :blend-in (0.5 0.5 ease-in) :self-blend (0.3 1.0 ease-out) :flags (default-entry force-stop))
	case SID_VAL("npc-p-cover-stand-l-hunker-enter-idle-l"):
	case SID_VAL("npc-p-cover-stand-l-hunker-enter-idle-r"):
		return ct == CoverDefinition::kCoverStandLeft;

	// low-right / low-over
	// (npc-nw-cover-low-r-hunker-enter-idle-l		:valid-demeanors (ambient uneasy aggressive search crawl crouch) :phase-range (0.1 0.4) :mt-subcategory (hunker) :blend-in (0.5 0.5 ease-in) :self-blend (0.3 1.0 ease-out) :flags (default-entry force-stop))
	// (npc-nw-cover-low-r-hunker-enter-idle-r		:valid-demeanors (ambient uneasy aggressive search crawl crouch) :phase-range (0.1 0.4) :mt-subcategory (hunker) :blend-in (0.5 0.5 ease-in) :self-blend (0.3 1.0 ease-out) :flags (default-entry force-stop))
	case SID_VAL("npc-nw-cover-low-r-hunker-enter-idle-l"):
	case SID_VAL("npc-nw-cover-low-r-hunker-enter-idle-r"):
		return ct == CoverDefinition::kCoverCrouchRight;

	// low left
	// (npc-nw-cover-low-l-hunker-enter-idle-l		:valid-demeanors (ambient uneasy aggressive search crawl crouch) :phase-range (0.1 0.4) :mt-subcategory (hunker) :blend-in (0.5 0.5 ease-in) :self-blend (0.3 1.0 ease-out) :flags (default-entry force-stop))
	// (npc-nw-cover-low-l-hunker-enter-idle-r		:valid-demeanors (ambient uneasy aggressive search crawl crouch) :phase-range (0.1 0.4) :mt-subcategory (hunker) :blend-in (0.5 0.5 ease-in) :self-blend (0.3 1.0 ease-out) :flags (default-entry force-stop))
	case SID_VAL("npc-nw-cover-low-l-hunker-enter-idle-l"):
	case SID_VAL("npc-nw-cover-low-l-hunker-enter-idle-r"):
		return ct == CoverDefinition::kCoverCrouchLeft;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::Enter(const ActionPackResolveInput& input,
							   ActionPack* pActionPack,
							   const ActionPackEntryDef& entryDef)
{
	ParentClass::Enter(input, pActionPack, entryDef);

	m_entryDef = entryDef;
	m_cachedCoverMode = kCoverModeInvalid;

	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();
	DC::AnimNpcInfo* pInfo = pAnimControl->Info<DC::AnimNpcInfo>();
	const NdAnimControllerConfig* pConfig = pNavChar->GetAnimControllerConfig();

	AiLogAnim(pNavChar, "IAiCoverController::Enter %s\n", DevKitOnly_StringIdToString(entryDef.m_entryAnimId));

	pAnimControl->GetBaseStateLayer()->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	AI_ASSERT(pActionPack->GetType() == ActionPack::kCoverActionPack);
	const CoverActionPack* pCoverAp = static_cast<const CoverActionPack*>(pActionPack);

	const CoverActionPack::PreferredDirection dirPref = CoverActionPack::PreferredDirection(entryDef.m_apUserData);
	const CoverDefinition& coverDef = pCoverAp->GetDefinition();
	const CoverDefinition::CoverType coverType = GetCoverTypeForNpc(coverDef.m_coverType, dirPref);

	//ANIM_ASSERT(input.m_apUserData == entryDef.m_apUserData);
	const bool dirPrefValid = TRUE_IN_FINAL_BUILD(ValidateCoverType(entryDef.m_entryAnimId, coverType));
	if (!dirPrefValid)
	{
		AiLogAnim(pNavChar,
				  "Entering cover with anim '%s' but cover type '%s'! (dir pref: %d)\n",
				  DevKitOnly_StringIdToString(entryDef.m_entryAnimId),
				  CoverDefinition::GetCoverTypeName(coverType),
				  dirPref);
		MailNpcLogTo(pNavChar, "john_bellomy@naughtydog.com", "Invalid Cover Dir Pref", FILE_LINE_FUNC);
	}

	// FadeToState parameters
	//const bool coverShare = CanShareCoverWithPlayer(pNavChar) && pActionPack->IsPlayerBlocked();
	StringId64 animStateId			 = SID("s_enter-cover");
	float startPhase				 = 0.0f;
	const BoundFrame destApReference = ::GetApReferenceForCover(pCoverAp, coverType, m_sidewaysOffset);
	BoundFrame apReference			 = destApReference;
	float animFadeTime				 = 0.4f;
	float motionFadeTime			 = 0.2f;
	bool freezeSrcState				 = true;
	bool freezeDestState			 = false;

	const DC::ApEntryItem* pDcEntryItem = entryDef.m_pDcDef;
	ArtItemAnimHandle hAnim;

	UpdateOverlayMode(coverType, coverDef.m_canStepOut);

	m_canStepOut = coverDef.m_canStepOut;

	if (entryDef.m_pDcDef && entryDef.m_pDcDef->m_motionMatchingSetId != INVALID_STRING_ID_64)
	{
		hAnim = pAnimControl->LookupAnim(entryDef.m_entryAnimId);

		const bool res = EnterUsingMotionMatching(hAnim,
												  entryDef.m_apReference,
												  destApReference,
												  entryDef.m_phase,
												  1.0f,
												  entryDef.m_pDcDef->m_motionMatchingSetId);
		if (!res)
		{
			ForceBlendToDestinationState(coverType);
		}

		m_doingMmEnter = true;
	}
	else if (entryDef.m_entryAnimId != INVALID_STRING_ID_64)
	{
		hAnim = pAnimControl->LookupAnim(entryDef.m_entryAnimId);

		const float distErr		= Dist(entryDef.m_entryNavLoc.GetPosPs(), pNavChar->GetTranslationPs());
		const float rotErrDeg	= RADIANS_TO_DEGREES(SafeAcos(Dot(GetLocalZ(entryDef.m_entryRotPs),
																  GetLocalZ(pNavChar->GetRotationPs()))));
		const float rotErrFloor = LerpScale(10.0f, 25.0f, 0.05f, 0.25f, rotErrDeg);

		motionFadeTime = rotErrFloor + LerpScale(0.075f, 0.4f, 0.0f, 0.4f, distErr);

		motionFadeTime = Max(motionFadeTime, 0.25f);

		if (pDcEntryItem && pDcEntryItem->m_blendIn)
		{
			animFadeTime   = entryDef.m_pDcDef->m_blendIn->m_animFadeTime;
			motionFadeTime = entryDef.m_pDcDef->m_blendIn->m_motionFadeTime;
		}

		const float maxFadeTime = LimitBlendTime(hAnim.ToArtItem(), entryDef.m_phase, animFadeTime);
		animFadeTime   = Min(maxFadeTime, animFadeTime);
		motionFadeTime = Min(maxFadeTime, motionFadeTime);

		const bool mirror = pDcEntryItem && ((pDcEntryItem->m_flags & DC::kApEntryItemFlagsMirror) != 0);

		// Set up enter state data
		AI::SetPluggableAnim(pNavChar, entryDef.m_entryAnimId, mirror);

		freezeSrcState	= false;
		freezeDestState	= false;
		startPhase		= m_entryDef.m_phase;
		apReference		= m_entryDef.m_apReference;

		if (pDcEntryItem->m_flags & DC::kApEntryItemFlagsDefaultEntry)
		{
			const SkeletonId skelId = pNavChar->GetSkeletonId();
			const Locator& alignPs = pNavChar->GetLocatorPs();
			Locator rotApRefPs;

			if (FindApReferenceFromAlign(skelId,
										 hAnim.ToArtItem(),
										 alignPs,
										 &rotApRefPs,
										 SID("apReference"),
										 startPhase,
										 mirror))
			{
				apReference.SetLocatorPs(rotApRefPs);
			}
		}

		AiLogAnim(pNavChar,
				  "IAiCoverController::Enter() anim: '%s' phase: %0.2f dist: %0.1fm\n",
				  DevKitOnly_StringIdToString(entryDef.m_entryAnimId),
				  entryDef.m_phase,
				  float(Dist(entryDef.m_entryNavLoc.GetPosPs(), pNavChar->GetTranslationPs())));
	}
	else
	{
		hAnim = pAnimControl->LookupAnim(SID("cover-idle"));

		// Skip the enter state and go directly to the destination state
		animStateId = GetDestinationState(coverType);

		AiLogAnim(pNavChar,
				  " - IAiCoverController::Enter() Forcing blend to state '%s' Dist: %0.1fm\n",
				  DevKitOnly_StringIdToString(animStateId),
				  float(Dist(pNavChar->GetLastTranslationPs(),
							 pActionPack->GetDefaultEntryPointPs(GetNavControl()->GetActionPackEntryDistance()))));
	}

	if (!m_doingMmEnter)
	{
		const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
		const bool hasAutoTransition = pBaseLayer->IsTransitionValid(SID("auto"));

		FadeToStateParams params;
		params.m_stateStartPhase = startPhase;
		params.m_apRef = apReference;
		params.m_apRefValid = true;
		params.m_animFadeTime = animFadeTime;
		params.m_motionFadeTime = motionFadeTime;
		params.m_freezeSrcState = freezeSrcState;
		params.m_freezeDestState = freezeDestState;
		params.m_blendType = DC::kAnimCurveTypeEaseIn;
		params.m_newInstBehavior = pBaseLayer->HasFreeTrack() ? FadeToStateParams::kSpawnNewTrack
															  : FadeToStateParams::kUsePreviousTrack;
		params.m_preventBlendTimeOverrun = !hasAutoTransition;

		m_enterAction.FadeToState(pAnimControl, animStateId, params, AnimAction::kFinishOnNonTransitionalStateReached);

		m_enterAction.ClearSelfBlendParams();

		if (const DC::SelfBlendParams* pSelfBlend = entryDef.GetSelfBlend())
		{
			m_enterAction.SetSelfBlendParams(pSelfBlend, destApReference, pNavChar, 1.0f);
		}
		else if (const ArtItemAnim* pAnim = hAnim.ToArtItem())
		{
			const float totalAnimTime = GetDuration(pAnim);
			const float animTime = Limit01(1.0f - startPhase) * totalAnimTime;

			DC::SelfBlendParams manualParams;
			manualParams.m_time = animTime;
			manualParams.m_phase = startPhase;
			manualParams.m_curve = DC::kAnimCurveTypeUniformS;

			m_enterAction.SetSelfBlendParams(&manualParams, destApReference, pNavChar, 1.0f);
		}

		const Vector entryVelocityPs = entryDef.m_entryVelocityPs;
		const Vector charVelocityPs = pNavChar->GetVelocityPs();
		const float desSpeed = Length(entryVelocityPs);
		const float projSpeed = Dot(SafeNormalize(entryVelocityPs, kZero), charVelocityPs);
		const float speedRatio = (desSpeed > 0.0f) ? (projSpeed / desSpeed) : 1.0f;
		pInfo->m_speedFactor = Limit(speedRatio, 0.8f, 1.1f);

		if (pDcEntryItem && (pDcEntryItem->m_mtSubcategory == SID("hunker")))
		{
			pInfo->m_coverEndTransitions = SID("cover-end^hunker");
		}
		else
		{
			pInfo->m_coverEndTransitions = SID("cover-end^idle");
		}
	}

	m_coverType			= coverType;
	m_enterAnimOverride	= INVALID_STRING_ID_64;

	if (m_pIkJacobianMap[kLeftLeg])
	{
		m_pIkJacobianMap[kLeftLeg]->ResetJointIkOffsets();
	}

	if (m_pIkJacobianMap[kRightLeg])
	{
		m_pIkJacobianMap[kRightLeg]->ResetJointIkOffsets();
	}

	const SkeletonId skelId = pNavChar->GetSkeletonId();
	if (!hAnim.ToArtItem()
		|| !FindAlignFromApReference(skelId,
									 hAnim.ToArtItem(),
									 1.0f,
									 destApReference.GetLocatorPs(),
									 SID("apReference"),
									 &m_enterFinalLocPs))
	{
		m_enterFinalLocPs = destApReference.GetLocatorPs();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::RefreshCoverOverlays()
{
	UpdateOverlayMode(m_coverType, m_canStepOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::ResolveForImmediateEntry(const ActionPackResolveInput& input,
												  const ActionPack* pActionPack,
												  ActionPackEntryDef* pDefOut) const
{
	const Npc* pNpc = (const Npc*)GetCharacter();
	const ActionPack* pCurAp = pNpc->GetEnteredActionPack();
	AnimControl* pAnimControl = pNpc->GetAnimControl();

	if (!pCurAp || !pActionPack || (pCurAp == pActionPack) || (m_cachedCoverMode == kCoverModeEntering))
	{
		return false;
	}

	if (m_cachedCoverMode == kCoverModeAim)
	{
		return false;
	}

	const CoverActionPack::PreferredDirection dirPref = CoverActionPack::PreferredDirection(input.m_apUserData);

	const CoverActionPack* pDestCoverAp	  = static_cast<const CoverActionPack*>(pActionPack);
	const CoverDefinition::CoverType type = GetCoverTypeForNpc(pDestCoverAp->GetDefinition().m_coverType, dirPref);
	const BoundFrame destApRef = GetApReferenceForCover(pDestCoverAp, dirPref);
	const SkeletonId skelId	   = pNpc->GetSkeletonId();
	const DC::ApEntryItemList* pTransferList = GetTransfers(type);
	const Locator& alignPs = pNpc->GetLocatorPs();

	if (!pTransferList)
		return false;

	ApEntry::CharacterState cs;
	MakeEntryCharacterState(input, pDestCoverAp, &cs);

	if (cs.m_mtSubcategory == SID("hunker"))
	{
		return false;
	}

	ApEntry::AvailableEntry availableEntries[256];

	const U32F numInitialEntries = BuildInitialEntryList(cs,
														 pTransferList,
														 ApEntry::GatherMode::kResolved,
														 destApRef,
														 availableEntries,
														 256);

	FlagInvalidEntries(cs, ApEntry::GatherMode::kResolved, destApRef, availableEntries, numInitialEntries);

	// Note for transfers we can shortcut you need to _always_ set the phase-range to [0,1]
	// and provide a skippable phase-range in the user-data.
	// e.g. :user-data (rangeval 0.2 0.8) means that the npc will always play the anim from
	// phase [0.0, 0.2], then maybe some frames in between 0.2 and 0.8, then always play phase [0.8, 1.0]
	for (U32F iEntry = 0; iEntry < numInitialEntries; ++iEntry)
	{
		ApEntry::AvailableEntry& entry = availableEntries[iEntry];
		if (entry.m_errorFlags.m_all)
			continue;

		const DC::AiRangeval* pProceduralPhaseRange = entry.m_pDcDef ? (const DC::AiRangeval*)entry.m_pDcDef->m_userData : nullptr;
		if (!pProceduralPhaseRange)
		{
			continue;
		}

		const bool mirror = entry.m_pDcDef->m_flags & DC::kApEntryItemFlagsMirror;

		const ArtItemAnim* pAnim = entry.m_anim.ToArtItem();
		const Locator myLocWs = pNpc->GetLocator();
		const Point myPosWs = myLocWs.Pos();
		const Point apRefPosWs = entry.m_rotatedApRef.GetTranslationWs();
		const float curDist = DistXz(myPosWs, apRefPosWs);

		Locator initialAnimLocLs = Locator(kIdentity);
		Locator minPhaseAnimLocLs = Locator(kIdentity);
		bool valid = true;

		valid = valid
				&& FindAlignFromApReference(skelId,
											pAnim,
											0.0f,
											Locator(kIdentity),
											SID("apReference"),
											&initialAnimLocLs,
											mirror);
		valid = valid
				&& FindAlignFromApReference(skelId,
											pAnim,
											pProceduralPhaseRange->m_val0,
											Locator(kIdentity),
											SID("apReference"),
											&minPhaseAnimLocLs,
											mirror);

		if (!valid)
		{
			continue;
		}

		const float initialAnimDist = DistXz(initialAnimLocLs.Pos(), minPhaseAnimLocLs.Pos());

		const float remainingDistance = Max(curDist - initialAnimDist, 0.0f);
		const float realStartPhase	  = ApEntry::GetPhaseToMatchDistance(cs,
																		 entry.m_anim,
																		 remainingDistance,
																		 entry.m_pDcDef,
																		 entry.m_phaseMin,
																		 entry.m_phaseMax);

		if (realStartPhase > pProceduralPhaseRange->m_val1)
		{
			entry.m_errorFlags.m_invalidEntryDist = true;
			continue;
		}

		entry.m_phase = pProceduralPhaseRange->m_val0;
		entry.m_skipPhase = realStartPhase;
	}

	const U32F numEntries = FilterInvalidEntries(availableEntries, numInitialEntries);

	if (0 == numEntries)
	{
		return false;
	}

	const U32F selectedIndex = ApEntry::ClosestEntry(availableEntries, numEntries, alignPs);

	const ApEntry::AvailableEntry* pSelectedEntry = &availableEntries[selectedIndex];

	AI_ASSERT(pSelectedEntry);
	AI_ASSERT(pSelectedEntry->m_pDcDef);

	pDefOut->m_apReference			= pSelectedEntry->m_rotatedApRef;
	pDefOut->m_entryAnimId			= pSelectedEntry->m_resolvedAnimId;
	pDefOut->m_pDcDef				= pSelectedEntry->m_pDcDef;
	pDefOut->m_stopBeforeEntry		= pSelectedEntry->m_pDcDef->m_flags & DC::kApEntryItemFlagsForceStop;
	pDefOut->m_preferredMotionType	= pNpc->GetConfiguration().m_moveToCoverMotionType;
	pDefOut->m_phase				= pSelectedEntry->m_phase;
	pDefOut->m_skipPhase			= pSelectedEntry->m_skipPhase;
	pDefOut->m_entryVelocityPs		= pSelectedEntry->m_entryVelocityPs;
	pDefOut->m_mirror				= pSelectedEntry->m_playMirrored;
	pDefOut->m_sbApReference		= GetApReferenceForCover(pDestCoverAp, CoverActionPack::kPreferNone);

	if (pDefOut->m_skipPhase > 0.0f)
	{
		const ArtItemAnim* pAnim = pSelectedEntry->m_anim.ToArtItem();
		const Locator myLocWs = pNpc->GetLocator();

		BoundFrame initialApRef = pSelectedEntry->m_rotatedApRef;
		Locator initialApRefWs;
		if (FindApReferenceFromAlign(skelId, pAnim, myLocWs, &initialApRefWs, 0.0f, pDefOut->m_mirror))
		{
			initialApRef.SetLocatorWs(initialApRefWs);
		}

		pDefOut->m_apReference = initialApRef;
	}

	const Locator entryLocPs = pSelectedEntry->m_entryAlignPs;
	const Locator entryLocWs = pSelectedEntry->m_rotatedApRef.GetParentSpace().TransformLocator(entryLocPs);

	const bool closeToEntry = Dist(pNpc->GetTranslation(), entryLocWs.Pos()) < 1.0f;

	return closeToEntry;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::EnterImmediately(const ActionPack* pActionPack, const ActionPackEntryDef& entryDef)
{
	if (!ParentClass::EnterImmediately(pActionPack, entryDef))
	{
		return false;
	}

	m_entryDef = entryDef;

	NavCharacter* pNavChar = GetCharacter();

	AiLogAnim(pNavChar, "IAiCoverController::EnterImmediately()\n");

	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	AnimStateLayer* pBaseLayer = pAnimControl->GetStateLayerById(SID("base"));

	if (pActionPack->GetType() != ActionPack::kCoverActionPack)
		return false;

	const CoverActionPack::PreferredDirection dirPref = CoverActionPack::PreferredDirection(entryDef.m_apUserData);

	const CoverActionPack* pCoverActionPack = static_cast<const CoverActionPack*>(pActionPack);
	const CoverDefinition::CoverType type = GetCoverTypeForNpc(pCoverActionPack->GetDefinition().m_coverType, dirPref);
	const BoundFrame destApReference = GetApReferenceForCover(pCoverActionPack, dirPref);

	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

	m_enterAction.ClearSelfBlendParams();

	StringId64 animId = SID("cover-idle");

	if (entryDef.m_entryAnimId != INVALID_STRING_ID_64)
	{
		animId = entryDef.m_entryAnimId;

		const DC::ApEntryItem* pDcEntryItem = entryDef.m_pDcDef;

		AI::SetPluggableAnim(pNavChar, entryDef.m_entryAnimId, entryDef.m_mirror);

		if (entryDef.m_skipPhase > 0.0f)
		{
			FadeToStateParams params;
			params.m_apRef = entryDef.m_apReference;
			params.m_apRefValid = true;
			params.m_animFadeTime = 0.5f;
			params.m_motionFadeTime = 0.3f;

			m_exitAction.FadeToState(pAnimControl,
									 SID("s_enter-cover"),
									 params,
									 AnimAction::kFinishOnNonTransitionalStateReached);

			m_enterAction.RequestDeferred(pAnimControl,
										  SID("self"),
										  entryDef.m_phase,
										  AnimAction::kFinishOnNonTransitionalStateReached,
										  nullptr,
										  entryDef.m_skipPhase);
		}
		else
		{
			FadeToStateParams params;
			params.m_apRef = entryDef.m_apReference;
			params.m_apRefValid = true;
			params.m_animFadeTime = 0.5f;
			params.m_motionFadeTime = 0.3f;
			params.m_stateStartPhase = entryDef.m_phase;

			m_enterAction.FadeToState(pAnimControl,
									  SID("s_enter-cover"),
									  params,
									  AnimAction::kFinishOnNonTransitionalStateReached);
		}

		const Vector entryVelocityPs	= entryDef.m_entryVelocityPs;
		const Vector charVelocityPs		= pNavChar->GetVelocityPs();
		const float desSpeed			= Length(entryVelocityPs);
		const float projSpeed			= Dot(SafeNormalize(entryVelocityPs, kZero), charVelocityPs);
		const float speedRatio			= (desSpeed > 0.0f) ? (projSpeed / desSpeed) : 1.0f;

		DC::AnimNpcInfo* pInfo = pAnimControl->Info<DC::AnimNpcInfo>();
		pInfo->m_speedFactor			= Limit(speedRatio, 0.8f, 1.1f);

		if (pDcEntryItem && (pDcEntryItem->m_mtSubcategory == SID("hunker")))
		{
			pInfo->m_coverEndTransitions = SID("cover-end^hunker");
		}
		else
		{
			pInfo->m_coverEndTransitions = SID("cover-end^idle");
		}
	}
	else
	{
		const StringId64 destStateId = GetDestinationState(type);

		const BoundFrame coverApRef = destApReference;
		FadeToStateParams params;
		params.m_apRef = coverApRef;
		params.m_apRefValid = true;
		params.m_animFadeTime = 0.6f;
		params.m_freezeSrcState = true;
		params.m_blendType = DC::kAnimCurveTypeUniformS;
		m_enterAction.FadeToState(pAnimControl, destStateId, params, AnimAction::kFinishOnTransitionTaken);
	}

	if (const DC::SelfBlendParams* pSelfBlend = entryDef.GetSelfBlend())
	{
		m_enterAction.SetSelfBlendParams(pSelfBlend, entryDef.m_sbApReference, pNavChar, 1.0f);
	}

	m_coverType = type;
	m_canStepOut = pCoverActionPack->GetDefinition().m_canStepOut;

	RefreshCoverOverlays();

	const SkeletonId skelId = pNavChar->GetSkeletonId();
	const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animId).ToArtItem();

	if (!pAnim
		|| !FindAlignFromApReference(skelId,
									 pAnim,
									 1.0f,
									 destApReference.GetLocatorPs(),
									 SID("apReference"),
									 &m_enterFinalLocPs))
	{
		m_enterFinalLocPs = destApReference.GetLocatorPs();
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::TeleportInto(ActionPack* pActionPack,
									  bool playEntireEntryAnim,
									  float fadeTime,
									  BoundFrame* pNewFrameOut,
									  uintptr_t apUserData /* = 0 */)
{
	if (!ParentClass::TeleportInto(pActionPack, playEntireEntryAnim, fadeTime, pNewFrameOut))
	{
		return false;
	}

	NavCharacter* pNavChar = GetCharacter();
	AnimControl* pAnimControl = pNavChar->GetAnimControl();

	CoverActionPack::PreferredDirection dirPref = CoverActionPack::PreferredDirection(apUserData);

	if (dirPref == CoverActionPack::kPreferNone)
	{
		const Vector myFwWs = GetLocalZ(pNavChar->GetLocator());
		const Vector apLeftWs = GetLocalX(pActionPack->GetBoundFrame().GetLocatorWs());

		if (Dot(myFwWs, apLeftWs) > 0.0f)
		{
			dirPref = CoverActionPack::kPreferLeft;
		}
		else
		{
			dirPref = CoverActionPack::kPreferRight;
		}
	}

	const CoverActionPack* pCoverActionPack = static_cast<const CoverActionPack*>(pActionPack);
	const CoverDefinition::CoverType type = GetCoverTypeForNpc(pCoverActionPack->GetDefinition().m_coverType, dirPref);
	const BoundFrame coverApRef = GetApReferenceForCover(pCoverActionPack, dirPref);

	const StringId64 destStateId = GetDestinationState(type);

	FadeToStateParams params;

	params.m_stateStartPhase = 0.0f;
	params.m_apRef = coverApRef;
	params.m_apRefValid = true;
	params.m_animFadeTime = fadeTime;
	params.m_motionFadeTime = fadeTime;
	params.m_blendType = DC::kAnimCurveTypeUniformS;

	m_enterAction.FadeToState(pAnimControl, destStateId, params, AnimAction::kFinishOnTransitionTaken);

	m_coverType = type;
	m_canStepOut = pCoverActionPack->GetDefinition().m_canStepOut;
	m_cachedCoverMode = kCoverModeInvalid;

	RefreshCoverOverlays();

	if (pNewFrameOut)
	{
		BoundFrame newFrame = coverApRef;

		const StringId64 destAnimId = SID("cover-idle");

		Locator destAlignPs;
		if (FindAlignFromApReference(pAnimControl, destAnimId, coverApRef.GetLocatorPs(), &destAlignPs))
		{
			newFrame.SetLocatorPs(destAlignPs);
		}

		m_enterFinalLocPs = newFrame.GetLocatorPs();

		*pNewFrameOut = newFrame;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F IAiCoverController::GetEnterAnimNames(const ActionPack* pAp, StringId64* pNamesOut, U32F maxNamesOut) const
{
	const CoverActionPack* pCoverAp = CoverActionPack::FromActionPack(pAp);
	DC::ApEntryItem* pItems = STACK_ALLOC(DC::ApEntryItem, maxNamesOut);

	if (!pItems || !pCoverAp)
		return 0;

	const I32F numEntries = GetCoverApEnterAnims(pCoverAp, pItems, maxNamesOut, false, CoverActionPack::kPreferNone);

	for (U32F i = 0; i < numEntries; ++i)
	{
		pNamesOut[i] = pItems[i].m_animName;
	}

	return numEntries;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F IAiCoverController::GetExitAnimNames(const ActionPack* pAp, StringId64* pNamesOut, U32F maxNamesOut) const
{
	const CoverActionPack* pCoverAp = CoverActionPack::FromActionPack(pAp);
	DC::ApExitAnimDef* pItems = STACK_ALLOC(DC::ApExitAnimDef, maxNamesOut);

	if (!pItems || !pCoverAp)
		return 0;

	const I32F numEntries = GetCoverApExitAnims(pCoverAp, pItems, maxNamesOut, CoverActionPack::kPreferNone);

	for (U32F i = 0; i < numEntries; ++i)
	{
		if (pItems[i].m_motionMatchingSetId)
		{
			pNamesOut[i] = pItems[i].m_motionMatchingSetId;
		}
		else
		{
			pNamesOut[i] = pItems[i].m_animName;
		}
	}

	return numEntries;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F IAiCoverController::GetTransferAnimNames(const ActionPack* pAp, StringId64* pNamesOut, U32F maxNamesOut) const
{
	const CoverActionPack* pCoverAp = CoverActionPack::FromActionPack(pAp);
	DC::ApEntryItem* pItems = STACK_ALLOC(DC::ApEntryItem, maxNamesOut);

	if (!pItems || !pCoverAp)
		return 0;

	const I32F numEntries = GetCoverApEnterAnims(pCoverAp, pItems, maxNamesOut, true, CoverActionPack::kPreferNone);

	for (U32F i = 0; i < numEntries; ++i)
	{
		pNamesOut[i] = pItems[i].m_animName;
	}

	return numEntries;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F IAiCoverController::GetCoverApEnterAnimCount(const CoverActionPack* pCoverAp) const
{
	return GetCoverApEnterAnims(pCoverAp, nullptr, 0, false, CoverActionPack::kPreferNone);
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F IAiCoverController::GetCoverApExitAnimCount(const CoverActionPack* pCoverAp) const
{
	return GetCoverApExitAnims(pCoverAp, nullptr, 0, CoverActionPack::kPreferNone);
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F IAiCoverController::GetCoverApTransferAnimCount(const CoverActionPack* pCoverAp) const
{
	return GetCoverApEnterAnims(pCoverAp, nullptr, 0, true, CoverActionPack::kPreferNone);
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F IAiCoverController::GetCoverApEnterAnims(const CoverActionPack* pCoverAp,
											  DC::ApEntryItem entryItems[],
											  I32F entryItemsSize,
											  bool isTransfer,
											  CoverActionPack::PreferredDirection dirPref) const
{
	if (!pCoverAp)
		return 0;

	const NavCharacter* pNavChar = GetCharacter();
	const CoverDefinition::CoverType type = GetCoverTypeForNpc(pCoverAp->GetDefinition().m_coverType, dirPref);

	const DC::ApEntryItemList* pDcEntryList = GetCoverEntries(type);

	if (isTransfer)
	{
		const DC::ApEntryItemList* pDcTransferList = GetTransfers(type);
		if (pDcTransferList && pDcTransferList->m_count > 0)
		{
			pDcEntryList = pDcTransferList;
		}
	}

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
I32F IAiCoverController::GetCoverApExitAnims(const CoverActionPack* pCoverAp,
											 DC::ApExitAnimDef exitItems[],
											 I32F exitItemsSize,
											 CoverActionPack::PreferredDirection dirPref) const
{
	const NavCharacter* pNavChar = GetCharacter();

	const CoverDefinition::CoverType type = GetCoverTypeForNpc(pCoverAp->GetDefinition().m_coverType, dirPref);
	const DC::ApExitAnimList* pDcExitList = GetExits(type);
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
bool IAiCoverController::PickBestCoverExit(const IPathWaypoints* pPathPs, ActionPackExitDef* pSelectedExitOut) const
{
	const NavCharacter* pNavChar = GetCharacter();

	const CoverActionPack* pActionPack = m_hActionPack.ToActionPack<CoverActionPack>();
	if (!pActionPack)
		return nullptr;

	const DC::ApExitAnimList* pExitList = GetExits(m_coverType);
	const BoundFrame apRef = GetApReferenceForCurrentCover();

	ApExit::CharacterState cs;
	MakeExitCharacterState(pActionPack, pPathPs, &cs);

	return ResolveExit(cs, apRef, pExitList, pSelectedExitOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::Exit(const PathWaypointsEx* pExitPathPs)
{
	NavCharacter* pNavChar	   = GetCharacter();
	AnimControl* pAnimControl  = pNavChar->GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	const AnimStateInstance* pCurInstance = pBaseLayer->CurrentStateInstance();

	if (!m_doingMmEnter)
	{
		pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);
	}

	m_doingMmEnter = false;

	m_animAction.Reset();
	m_enterAction.Reset();
	m_exitAction.Reset();
	m_peekAction.Reset();
	m_performanceAction.Reset();
	m_throwGrenadeAction.Reset();

	m_performanceStateMask = 0;
	m_exitPhase = -1.0f;
	m_exitHandoff.Reset();

	Npc* pNpc = (Npc*)pNavChar;
	if (IAiLocomotionController* pLocomotionController = pNpc->GetAnimationControllers()->GetLocomotionController())
	{
		pLocomotionController->ConfigureWeaponUpDownOverlays(pLocomotionController->GetDesiredWeaponUpDownState(false));
	}

	m_exitAction.ClearSelfBlendParams();

	const CoverMode curMode = GetCurrentCoverMode();
	const CoverActionPack* pCoverAp = m_hActionPack.ToActionPack<CoverActionPack>();

	const BoundFrame apRef = pCoverAp ? GetApReferenceForCurrentCover() : pNavChar->GetBoundFrame();
	ActionPackExitDef selectedExit;

	if (nullptr == pCoverAp)
	{
		AiLogAnim(pNavChar, "IAiCoverController::Exit - No ActionPack! Fading to idle\n");
		FadeToStateParams params;
		params.m_animFadeTime = 0.4f;
		m_exitAction.FadeToState(pAnimControl, SID("s_idle"), params, AnimAction::kFinishOnTransitionTaken);

		m_exitHandoff.SetStateChangeRequestId(m_exitAction.GetStateChangeRequestId(), SID("s_idle"));
	}
	else if (curMode == kCoverModeEntering)
	{
		if (const CharacterMotionMatchLocomotion* pController = GetMotionMatchController())
		{
			AiLogAnim(pNavChar, "IAiCoverController::Exit - Aborting motion matched cover enter\n");

			m_exitAction.Reset();

			m_exitHandoff.SetSubsystemControllerId(pController->GetSubsystemId(), pCurInstance->GetStateName());
		}
		else
		{
			AiLogAnim(pNavChar, "IAiCoverController::Exit - Aborting cover enter with 'idle' transition\n");

			m_exitAction.Request(pAnimControl,
								 SID("idle"),
								 AnimAction::kFinishOnNonTransitionalStateReached,
								 &pNavChar->GetBoundFrame());

			m_exitHandoff.SetStateChangeRequestId(m_exitAction.GetStateChangeRequestId(), SID("s_idle"));
		}

		m_enterAction.Reset();
	}
	else if (IsInOpenState())
	{
		if (pCurInstance)
		{
			m_exitHandoff.SetAnimStateInstance(pCurInstance);
		}
		else
		{
			AiLogAnim(pNavChar, "IAiCoverController::Exit - No current state instance, fading to idle\n");
			FadeToStateParams params;
			params.m_apRef = apRef;
			params.m_apRefValid = true;
			params.m_animFadeTime = 0.4f;
			m_exitAction.FadeToState(pAnimControl, SID("s_idle"), params, AnimAction::kFinishOnTransitionTaken);

			m_exitHandoff.SetStateChangeRequestId(m_exitAction.GetStateChangeRequestId(), SID("s_idle"));
		}
	}
	else if (PickBestCoverExit(pExitPathPs, &selectedExit))
	{
		const DC::ApExitAnimDef* pExitPerf = selectedExit.m_pDcDef;

		if (pExitPerf->m_motionMatchingSetId != INVALID_STRING_ID_64)
		{
			AiLogAnim(pNavChar,
					  "IAiCoverController::Exit - Exiting by motion matching set '%s' to %s\n",
					  DevKitOnly_StringIdToString(pExitPerf->m_motionMatchingSetId),
					  (pExitPerf->m_validMotions != 0) ? "MOVING" : "STOPPED");

			if (!pCurInstance || (0 == (pCurInstance->GetStateFlags() & DC::kAnimStateFlagLocomotionReady)))
			{
				FadeToStateParams params;
				params.m_apRef = apRef;
				params.m_apRefValid = true;
				params.m_animFadeTime = 0.4f;
				m_exitAction.FadeToState(pAnimControl, SID("s_cover-idle"), params, AnimAction::kFinishOnTransitionTaken);

				m_exitHandoff.SetStateChangeRequestId(m_exitAction.GetStateChangeRequestId(), SID("s_cover-idle"));
			}
			else
			{
				m_exitHandoff.SetAnimStateInstance(pCurInstance);
			}

			m_exitHandoff.m_motionType	   = pNpc->GetRequestedMotionType();
			m_exitHandoff.m_handoffMmSetId = pExitPerf->m_motionMatchingSetId;
		}
		else
		{
			FadeToStateParams params;
			params.m_apRefValid	   = true;
			params.m_apRef		   = selectedExit.m_apReference;
			params.m_customApRefId = selectedExit.m_apRefId;
			params.m_animFadeTime  = 0.4f;
			params.m_motionFadeTime = 0.4f;

			AiLogAnim(pNavChar,
					  "IAiCoverController::Exit - Exiting by anim '%s' to %s\n",
					  DevKitOnly_StringIdToString(pExitPerf->m_animName),
					  (pExitPerf->m_validMotions != 0) ? "MOVING" : "STOPPED");

			AI::SetPluggableAnim(pNavChar, pExitPerf->m_animName, selectedExit.m_mirror);

			DC::AnimNavCharInfo* pInfo = pAnimControl->Info<DC::AnimNavCharInfo>();

			StringId64 stateNameId = INVALID_STRING_ID_64;

			if (pExitPerf->m_validMotions != 0)
			{
				m_exitAction.FadeToState(pAnimControl, SID("s_cover-exit^moving"), params, AnimAction::kFinishOnAnimEndEarly);
				m_exitHandoff.m_motionType = pNavChar->GetRequestedMotionType();
				stateNameId = SID("s_cover-exit^moving");
				pInfo->m_apExitTransitions = SID("ap-exit^move");
			}
			else
			{
				m_exitAction.FadeToState(pAnimControl, SID("s_cover-exit^idle"), params, AnimAction::kFinishOnNonTransitionalStateReached);
				stateNameId = SID("s_cover-exit^idle");
				pInfo->m_apExitTransitions = SID("ap-exit^idle");
			}

			m_exitHandoff.SetStateChangeRequestId(m_exitAction.GetStateChangeRequestId(), stateNameId);

			m_exitPhase = pExitPerf->m_exitPhase;
			const float constraintPhase = m_exitPhase >= 0.0f ? m_exitPhase : 1.0f;

			SelfBlendAction::Params sbParams;
			sbParams.m_destAp = selectedExit.m_sbApReference;
			sbParams.m_constraintPhase = constraintPhase;
			sbParams.m_apChannelId = selectedExit.m_apRefId;

			if (pExitPerf->m_selfBlend)
			{
				sbParams.m_blendParams = *pExitPerf->m_selfBlend;
			}
			else
			{
				sbParams.m_blendParams.m_phase = 0.0f;
				sbParams.m_blendParams.m_time = GetDuration(pAnimControl->LookupAnim(pExitPerf->m_animName).ToArtItem());
				sbParams.m_blendParams.m_curve = DC::kAnimCurveTypeEaseOut;
			}

			m_exitAction.ConfigureSelfBlend(pNavChar, sbParams);
		}

		if (pExitPerf->m_navAnimHandoff)
		{
			m_exitHandoff.ConfigureFromDc(pExitPerf->m_navAnimHandoff);
		}
		else
		{
			m_exitHandoff.m_steeringPhase = pExitPerf->m_exitPhase;
		}
	}
	else
	{
		AiLogAnim(pNavChar, "IAiCoverController::Exit - No exit anim, fading to idle\n");
		FadeToStateParams params;
		params.m_apRef = apRef;
		params.m_apRefValid = true;
		params.m_animFadeTime = 0.4f;
		m_exitAction.FadeToState(pAnimControl, SID("s_idle"), params, AnimAction::kFinishOnTransitionTaken);

		m_exitHandoff.SetStateChangeRequestId(m_exitAction.GetStateChangeRequestId(), SID("s_idle"));
	}

	pNavChar->ConfigureNavigationHandOff(m_exitHandoff, FILE_LINE_FUNC); // if we're self blending we'll need to call this again, but that should be fine

	ParentClass::Exit(pExitPathPs);

	m_cachedCoverMode = kCoverModeExiting;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::OnExitComplete()
{
	m_sidewaysOffset = 0.0f;

	// Reset the cover overlay to 'default', if that is available
	ResetCoverOverlays();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::IsReadyToExit() const
{
	NavCharacter* pNavChar = GetCharacter();
	if (!pNavChar)
		return true;

	const AnimationControllers* pAnimControllers = pNavChar->GetAnimationControllers();
	if (!pAnimControllers)
		return true;

	const IAiWeaponController* pWeaponController = pAnimControllers->GetWeaponController();
	if (!pWeaponController)
		return true;

	if (pWeaponController->IsPrimaryWeaponUpToDate())
		return true;

	const ProcessWeaponBase* pCurrWeapon = pWeaponController->GetPrimaryWeapon();
	if (!pCurrWeapon)
		return true;

	const ProcessWeaponBase* pRequestedWeapon = pWeaponController->GetRequestedPrimaryWeapon();
	if (!pRequestedWeapon)
		return true;

	if (pCurrWeapon->IsMeleeWeapon() == pRequestedWeapon->IsMeleeWeapon())
		return true;

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::ResetCoverOverlays()
{
	NavCharacter* pNavChar	  = GetCharacter();
	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimOverlays* pOverlays	  = pAnimControl ? pAnimControl->GetAnimOverlays() : nullptr;
	const NdAnimControllerConfig* pConfig = pNavChar ? pNavChar->GetAnimControllerConfig() : nullptr;
	const StringId64 mapId = StringToStringId64(StringBuilder<128>("anim-overlay-%s-action-pack",
																   pNavChar->GetOverlayBaseName())
													.c_str());

	const DC::Map* pMap	   = pConfig ? ScriptManager::Lookup<DC::Map>(mapId, nullptr) : nullptr;
	if (!pMap || !pOverlays)
		return;

	const StringId64* pSetId = ScriptManager::MapLookup<StringId64>(pMap, SID("no-change"));
	if (!pSetId || (*pSetId == INVALID_STRING_ID_64))
		return;

	const DC::AnimOverlaySet* pSet = ScriptManager::Lookup<DC::AnimOverlaySet>(*pSetId);
	if (!pSet)
		return;

	pOverlays->SetOverlaySet(pSet);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::IsPointOpenOnNavPs(Point_arg posPs) const
{
	if (const NavControl* pNavCon = GetNavControl())
	{
		return !pNavCon->IsPointStaticallyBlockedPs(posPs);
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::IsDoingPeek() const
{
	return !m_peekAction.IsDone();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::OverrideNextEnterAnim(const StringId64 enterAnimId)
{
	AiLogAnim(GetCharacter(), "Override next enter anim with: %s\n", DevKitOnly_StringIdToString(enterAnimId));
	m_enterAnimOverride = enterAnimId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::DebugDraw(ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	const NavCharacter* pNavChar = GetCharacter();
	const Point myPosWs = pNavChar->GetTranslation();
	const CoverMode curMode = GetCurrentCoverMode();

	if (false) //if (curMode != kCoverModeInvalid)
	{
		g_prim.Draw(DebugString(myPosWs, GetCoverModeStr(curMode), kColorWhite, 0.5f));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::DebugDrawEntries(const ActionPackResolveInput& input, const ActionPack* pActionPack) const
{
	STRIP_IN_FINAL_BUILD;

	const U32 kMaxAnimEntries = 50;

	const NavCharacter* pNavChar	= GetCharacter();
	const CoverActionPack* pCoverAp	= CoverActionPack::FromActionPack(pActionPack);
	const CoverActionPack* pCurAp = CoverActionPack::FromActionPack(pNavChar->GetEnteredActionPack());
	const bool isTransfer = pCurAp && pCurAp != pCoverAp;

	const CoverActionPack::PreferredDirection dirPref = CoverActionPack::PreferredDirection(input.m_apUserData);

	const BoundFrame apRef = GetApReferenceForCover(pCoverAp, dirPref);

	const DC::ApEntryItem* pChosenEntry = m_entryDef.m_pDcDef;

	DC::ApEntryItemList entryList;
	DC::ApEntryItem entryItems[kMaxAnimEntries];

	entryList.m_array = entryItems;
	entryList.m_count = GetCoverApEnterAnims(pCoverAp, entryItems, kMaxAnimEntries, isTransfer, dirPref);

	DebugDrawEntryAnims(input,
						pActionPack,
						apRef,
						entryList,
						pChosenEntry,
						g_aiGameOptions.m_cover);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::DebugDrawExits(const ActionPackResolveInput& input,
										const ActionPack* pActionPack,
										const IPathWaypoints* pPathPs) const
{
	STRIP_IN_FINAL_BUILD;

	switch (g_aiGameOptions.m_cover.m_debugMode)
	{
	case NavCharOptions::kApAnimDebugModeExitAnimsValid:
	case NavCharOptions::kApAnimDebugModeExitAnimsChosen:
	case NavCharOptions::kApAnimDebugModeExitAnimsAll:
		break;

	default:
		return;
	}

	const U32 kMaxAnimEntries = 50;

	const NavCharacter* pNavChar	= GetCharacter();
	const CoverActionPack* pCoverAp	= CoverActionPack::FromActionPack(pActionPack);

	const CoverActionPack::PreferredDirection dirPref = CoverActionPack::PreferredDirection(input.m_apUserData);

	const BoundFrame apRef = GetApReferenceForCover(pCoverAp, dirPref);
	const DC::ApExitAnimDef* pChosenExit = nullptr;

	DC::ApExitAnimList exitList;
	DC::ApExitAnimDef exitAnimDefs[kMaxAnimEntries];

	exitList.m_array = exitAnimDefs;
	exitList.m_count = GetCoverApExitAnims(pCoverAp, exitAnimDefs, kMaxAnimEntries, dirPref);

	DebugDrawExitAnims(input,
					   pActionPack,
					   apRef,
					   pPathPs,
					   exitList,
					   pChosenExit,
					   g_aiGameOptions.m_cover);
}

/// --------------------------------------------------------------------------------------------------------------- ///
IAiCoverController::CoverMode IAiCoverController::GetCurrentCoverMode() const
{
	const NavCharacter* pChar = GetCharacter();
	const AnimControl* pAnimControl = pChar ? pChar->GetAnimControl() : nullptr;
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	CoverMode ret = kCoverModeInvalid;

	const StringId64 curStateId = pBaseLayer ? pBaseLayer->CurrentStateId() : INVALID_STRING_ID_64;

	if (m_cachedCoverMode == kCoverModeExiting)
	{
		if (IsKnownCoverState(curStateId) || curStateId == SID("s_motion-match-locomotion"))
			return kCoverModeExiting;

		return kCoverModeInvalid;
	}

	switch (curStateId.GetValue())
	{
	case SID_VAL("s_enter-cover"):
	case SID_VAL("s_motion-match-locomotion"):
		ret = IsEnterComplete() ? kCoverModeIdle : kCoverModeEntering;
		break;

	case SID_VAL("s_cover-idle"):
	case SID_VAL("s_cover-open-idle"):
		ret = kCoverModeIdle;
		break;

	case SID_VAL("s_cover-aim^idle"):
	case SID_VAL("s_cover-idle^aim"):
	case SID_VAL("s_cover-aim"):
	case SID_VAL("s_cover-aim-fire"):
	case SID_VAL("s_cover-peek^aim"):
	case SID_VAL("s_cover-aim-reassess"):
	case SID_VAL("s_cover-open-aim"):
		ret = kCoverModeAim;
		break;

	case SID_VAL("s_cover-peek"):
	case SID_VAL("s_cover-idle^peek"):
	case SID_VAL("s_cover-peek^idle"):
	case SID_VAL("s_cover-aim^peek"):
		ret = kCoverModePeek;
		break;

	case SID_VAL("s_cover-idle-fire"):
		ret = kCoverModeIdleFire;
		break;

	case SID_VAL("s_cover-idle^hunker"):
	case SID_VAL("s_cover-aim^hunker"):
	case SID_VAL("s_cover-hunker^idle"):
	case SID_VAL("s_cover-hunker"):
		ret = kCoverModeHunker;
		break;

	case SID_VAL("s_cover-exit^idle"):
	case SID_VAL("s_cover-exit^moving"):
		ret = kCoverModeExiting;
		break;

	case SID_VAL("s_cover-grenade-draw"):
	case SID_VAL("s_cover-open-grenade-draw"):
		ret = kCoverModeDraw;
		break;

	case SID_VAL("s_cover-grenade-toss"):
	case SID_VAL("s_cover-open-grenade-toss"):
		ret = kCoverModeThrow;
		break;

	case SID_VAL("s_hit-reaction-state"):
	case SID_VAL("s_hit-reaction-state-w-gesture-state"):
	case SID_VAL("s_cover-hit-reaction"):
		ret = kCoverModeHitReaction;
		break;

	case SID_VAL("s_cover-performance"):
		ret = kCoverModePerformance;
		break;

	case SID_VAL("s_cover-idle^step-out"):
	case SID_VAL("s_cover-step-out"):
	case SID_VAL("s_cover-step-out^idle"):
		ret = kCoverModeStepOut;
		break;
	}

	if (m_exitAction.IsValid() && !m_exitAction.IsDone() && ret != kCoverModeExiting)
	{
		ret = kCoverModeExiting;
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IAiCoverController::TakeHit(const HitDescription* pHitDesc)
{
	NavCharacter* pChar			= GetCharacter();
	AnimControl* pAnimControl	= pChar ? pChar->GetAnimControl() : nullptr;
	AnimStateLayer* pBaseLayer	= pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	const CoverActionPack* pCoverAp = (const CoverActionPack*)pChar->GetEnteredActionPack();
	if (!pBaseLayer || !pCoverAp)
		return false;

	const StringId64 animId	= SID("cover-hit-reaction");
	const StringId64 stateId = SID("s_cover-hit-reaction");

	if (!pAnimControl->LookupAnim(animId).ToArtItem())
		return false;

	pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);
	BoundFrame apRef = GetApReferenceForCurrentCover();

	FadeToStateParams params;
	params.m_apRef = apRef;
	params.m_apRefValid = true;
	params.m_animFadeTime = 0.2f;

	m_animAction.FadeToState(pAnimControl, stateId, params, AnimAction::kFinishOnNonTransitionalStateReached);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::UpdateOverlayMode(const CoverDefinition::CoverType coverType, bool canStepOut)
{
	NavCharacter* pNavChar = GetCharacter();
	const char* pOverlayBaseName = pNavChar->GetOverlayBaseName();
	const StringId64 mapId = StringToStringId64(StringBuilder<128>("anim-overlay-%s-action-pack", pOverlayBaseName).c_str());

	AnimControl* pAnimControl = pNavChar ? pNavChar->GetAnimControl() : nullptr;
	AnimOverlays* pOverlays = pAnimControl ? pAnimControl->GetAnimOverlays() : nullptr;

	if (!pOverlays)
		return;

	const DC::Map* pMap = ScriptManager::Lookup<DC::Map>(mapId, nullptr);
	if (!pMap)
		return;

	StringId64 typeSid = INVALID_STRING_ID_64;
	switch (coverType)
	{
	case CoverDefinition::kCoverStandLeft:
		typeSid = SID("cover-stand-left");
		break;
	case CoverDefinition::kCoverStandRight:
		typeSid = SID("cover-stand-right");
		break;

	case CoverDefinition::kCoverCrouchLeft:
		if (m_shootSide)
		{
			typeSid = SID("cover-low-left");
		}
		else
		{
			typeSid = SID("cover-low-over-left");
		}
		break;
	case CoverDefinition::kCoverCrouchRight:
		if (m_shootSide)
		{
			typeSid = SID("cover-low-right");
		}
		else
		{
			typeSid = SID("cover-low-over-right");
		}
		break;
	case CoverDefinition::kCoverCrouchOver:
		typeSid = SID("cover-low-over-right");
		break;
	}

	if (INVALID_STRING_ID_64 == typeSid)
		return;

	const StringId64* pSetId = ScriptManager::MapLookup<StringId64>(pMap, typeSid);
	if (!pSetId || (*pSetId == INVALID_STRING_ID_64))
		return;

	pOverlays->SetOverlaySet(*pSetId);

	const StringId64 stepOutSetId = StringToStringId64(StringBuilder<128>("anim-overlay-%s-cover-step-out-%s",
																		  pOverlayBaseName,
																		  canStepOut ? "enabled" : "disabled").c_str());

	pOverlays->SetOverlaySet(stepOutSetId, true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void IAiCoverController::MakeEntryCharacterState(const ActionPackResolveInput& input,
												 const ActionPack* pActionPack,
												 ApEntry::CharacterState* pCsOut) const
{
	const NavCharacter* pNavChar = GetCharacter();

	if (!pNavChar || !pCsOut)
		return;

	ParentClass::MakeEntryCharacterState(input, pActionPack, pCsOut);

	ApEntry::CharacterState& cs = *pCsOut;

	const CoverActionPack* pCoverAp = CoverActionPack::FromActionPack(pActionPack);

	// skills should make this decision and pass it in via the nav state machine,
	// because they need to do higher-level, more complex logic to make it
	// look nice, such as deciding not to switch if almost at the destination,
	// deciding to stop-and-stand for a frame to abort an already-started entry,
	// etc.
	//const bool curHunker = cs.m_mtSubcategory == SID("hunker");
	//if (AI::ShouldHunkerForCoverShare(pNavChar, pCoverAp, curHunker))
	//{
	//	cs.m_mtSubcategory = SID("hunker");
	//}

	const FgAnimData* pAnimData = pNavChar->GetAnimData();

	// more potential config variables here
	const NdAnimControllerConfig* pConfig = pNavChar->GetAnimControllerConfig();
	cs.m_rangedEntryRadius		= 3.0f;
	cs.m_maxEntryErrorAngleDeg	= g_aiGameOptions.m_cover.m_maxEntryErrorAngleDeg;
	cs.m_maxFacingDiffAngleDeg	= g_aiGameOptions.m_cover.m_maxFacingDiffAngleDeg;

	const CoverActionPack* pCurrentCoverAp = CoverActionPack::FromActionPack(pNavChar->GetEnteredActionPack());
	const bool isTransfer = pCurrentCoverAp && pCurrentCoverAp != pActionPack;

	if (isTransfer)
	{
		cs.m_testClearMotionInitRadial = false;
		cs.m_testClearMotionRadial = false;
		cs.m_maxFacingDiffAngleDeg = 45.0f;
	}
	else
	{
		cs.m_testClearMotionRadial = pAnimData && (pAnimData->m_animLod < DC::kAnimLodFar);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoundFrame IAiCoverController::GetApReferenceForCover(const CoverActionPack* pCoverAp,
													  CoverActionPack::PreferredDirection dirPref) const
{
	if (!pCoverAp)
	{
		return BoundFrame(kIdentity);
	}

	const CoverDefinition::CoverType type = GetCoverTypeForNpc(pCoverAp->GetDefinition().m_coverType, dirPref);

	BoundFrame coverApRef = ::GetApReferenceForCover(pCoverAp, type, m_sidewaysOffset);

	return coverApRef;
}

/// --------------------------------------------------------------------------------------------------------------- ///
IAiCoverController* CreateAiCoverController()
{
	return NDI_NEW IAiCoverController;
}
