/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/ai/controller/action-pack-controller.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/render/util/prim-server-wrapper.h"

#include "gamelib/anim/motion-matching/motion-matching-debug.h"
#include "gamelib/anim/motion-matching/pose-tracker.h"
#include "gamelib/gameplay/ai/agent/nav-character-adapter.h"
#include "gamelib/gameplay/ai/agent/nav-character-adapter.inl"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/controller/nd-animation-controllers.h"
#include "gamelib/gameplay/ai/controller/nd-locomotion-controller.h"
#include "gamelib/gameplay/ap-entry-util.h"
#include "gamelib/gameplay/ap-exit-util.h"
#include "gamelib/gameplay/character-motion-match-locomotion.h"
#include "gamelib/gameplay/nav/action-pack-entry-def.h"
#include "gamelib/gameplay/nav/action-pack-exit-def.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-state-machine.h"
#include "gamelib/gameplay/nav/path-waypoints.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/nd-subsystem-mgr.h"
#include "gamelib/level/art-item-anim.h"
#include "gamelib/scriptx/h/anim-nav-character-info.h"
#include "gamelib/scriptx/h/ap-entry-defines.h"
#include "gamelib/scriptx/h/nav-character-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ValidateExitEffs(const NdGameObject* pCharacter, const DC::ApExitAnimDef* pExitDef)
{
	STRIP_IN_FINAL_BUILD_VALUE(false);

	if (!pCharacter || !pExitDef)
		return false;

	const AnimControl* pAnimControl = pCharacter->GetAnimControl();
	if (!pAnimControl)
		return false;

	const ArtItemAnim* pArtItemAnim = pAnimControl->LookupAnim(pExitDef->m_animName).ToArtItem();
	if (!pArtItemAnim || !pArtItemAnim->m_pClipData)
		return false;

	bool valid = true;

	if (const EffectAnim* pEffectAnim = pArtItemAnim->m_pEffectAnim)
	{
		for (U32F ii = 0; ii < pEffectAnim->m_numEffects; ++ii)
		{
			const EffectAnimEntry& effectEntry = pEffectAnim->m_pEffects[ii];

			const StringId64 effectNameId = effectEntry.GetNameId();

			switch (effectNameId.GetValue())
			{
			case SID_VAL("gear-effect"):
			case SID_VAL("foot-effect"):
			case SID_VAL("voice-effect"):
			case SID_VAL("sound-effect"):
			case SID_VAL("hand-effect-left"):
			case SID_VAL("hand-effect-right"):
				if (!g_navCharOptions.m_actionPack.m_validateApSoundEffs)
					continue;
			}

			const float effectPhase = GetPhaseFromClipFrame(pArtItemAnim->m_pClipData, effectEntry.GetFrame());

			if (effectPhase > pExitDef->m_exitPhase)
			{
				AnimError("EFF '%s' on '%s' may not trigger - phase %0.1f (frame %0.1f) > exit-phase %0.1f\n",
						  DevKitOnly_StringIdToString(effectNameId),
						  DevKitOnly_StringIdToString(pExitDef->m_animName),
						  effectPhase,
						  effectEntry.GetFrame(),
						  pExitDef->m_exitPhase);
				valid = false;
			}
		}
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackEntryDef::OverrideSelfBlend(const DC::SelfBlendParams* pSelfBlend)
{
	if (pSelfBlend)
	{
		m_selfBlendOverride = *pSelfBlend;
	}
	else
	{
		m_selfBlendOverride = DC::SelfBlendParams();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::SelfBlendParams* ActionPackEntryDef::GetSelfBlend() const
{
	if (m_selfBlendOverride.m_phase >= 0.0f)
	{
		return &m_selfBlendOverride;
	}
	
	if (m_pDcDef && m_pDcDef->m_selfBlend && m_pDcDef->m_selfBlend->m_phase >= 0.0f)
	{
		return m_pDcDef->m_selfBlend;
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackController::Reset()
{
	m_hActionPack = nullptr;
	m_hMmController = nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackController::MakeEntryCharacterState(const ActionPackResolveInput& input,
												   const ActionPack* pActionPack,
												   ApEntry::CharacterState* pCsOut) const
{
	if (!pCsOut)
		return;

	const NavCharacterAdapter charAdapter = GetCharacterAdapter();
	ApEntry::CharacterState& cs = *pCsOut;

	const NdAnimControllerConfig* pConfig = charAdapter->GetAnimControllerConfig();

	cs.InitCommon(charAdapter);

	cs.m_moving			= input.m_moving;
	cs.m_frame			= input.m_frame;
	cs.m_motionType		= input.m_motionType;
	cs.m_mtSubcategory	= input.m_mtSubcategory;
	cs.m_velocityPs		= input.m_velocityPs;
	
	cs.m_maxFacingDiffAngleDeg = 45.0f;
	cs.m_maxEntryErrorAngleDeg = 45.0f;

	if (const NavCharacter* pNavChar = charAdapter.ToNavCharacter())
	{
		const I32 dcWat = pNavChar->GetDcWeaponAnimType();
		cs.m_dcWeaponAnimTypeMask = 1UL << dcWat;
	}
	else
	{
		cs.m_dcWeaponAnimTypeMask = 0;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackController::MakeExitCharacterState(const ActionPack* pActionPack,
												  const IPathWaypoints* pPathPs,
												  ApExit::CharacterState* pCsOut) const
{
	const NdGameObject* pGo	= GetCharacterGameObject();
	const NavCharacterAdapter charAdapter = GetCharacterAdapter();
	const NavCharacter* pNavChar = charAdapter.ToNavCharacter();

	if (!pGo || !pCsOut || !pActionPack)
	{
		return;
	}

	ApExit::CharacterState& cs = *pCsOut;

	cs.m_frame = pGo->GetBoundFrame();
	cs.m_facePositionPs = charAdapter->GetFacePositionPs();
	cs.m_motionType		= charAdapter->GetRequestedMotionType();

	const NavStateMachine* pNsm = pNavChar ? pNavChar->GetNavStateMachine() : nullptr;
	const ActionPackEntryDef* pEntryDef = pNsm ? pNsm->GetActionPackEntryDef() : nullptr;

	if (pEntryDef && (pEntryDef->m_mtSubcategoryId != INVALID_STRING_ID_64))
	{
		cs.m_mtSubcategory = pEntryDef->m_mtSubcategoryId;
	}
	else
	{
		cs.m_mtSubcategory = pNavChar->GetCurrentMtSubcategory();
	}

	if (cs.m_mtSubcategory == INVALID_STRING_ID_64)
	{
		cs.m_mtSubcategory = SID("normal");
	}

	cs.m_requestedDcDemeanor = pNavChar->GetRequestedDcDemeanor();
	cs.m_pAnimControl		 = pGo->GetAnimControl();
	cs.m_pNavControl		 = pNavChar->GetNavControl();
	cs.m_pPathPs = pPathPs;
	cs.m_skelId	 = pGo->GetSkeletonId();
	cs.m_stopAtPathEnd = pNsm ? pNsm->StopAtPathEndPs(pPathPs) : false;
	cs.m_apNavLocation = pActionPack->GetRegisteredNavLocation();

	const ActionPack* pEnteredAp = pNavChar->GetEnteredActionPack();
	cs.m_isInAp	 = pEnteredAp != nullptr;

	cs.m_apRegDistance = Dist(pActionPack->GetRegistrationPointWs(), pActionPack->GetLocatorWs().Pos());

	if (pNavChar)
	{
		const I32 dcWat = pNavChar->GetDcWeaponAnimType();
		cs.m_dcWeaponAnimTypeMask = 1UL << dcWat;
	}
	else
	{
		cs.m_dcWeaponAnimTypeMask = 0;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackController::ResolveExit(const ApExit::CharacterState& cs,
									   const BoundFrame& apRef,
									   const DC::ApExitAnimList* pExitList,
									   ActionPackExitDef* pExitOut,
									   StringId64 apRefId /* = INVALID_STRING_ID_64 */) const
{
	if (!pExitList)
	{
		return false;
	}

	if (INVALID_STRING_ID_64 == apRefId)
	{
		apRefId = SID("apReference");
	}

	if (pExitOut)
	{
		*pExitOut = ActionPackExitDef();
	}

	const U32F kMaxAnimExits = 32;

	ApExit::AvailableExit availableExits[kMaxAnimExits];

	const DC::ApExitAnimDef* pBestExit = nullptr;
	I32F bestExitIndex = -1;

	const IPathWaypoints* pPathPs = cs.m_pPathPs;

	if (pPathPs && pPathPs->IsValid())
	{
		const U32F numExits = ApExit::ResolveExit(cs, pExitList, apRef, availableExits, kMaxAnimExits, apRefId);

		float bestAnimRotAngleDeg = kLargeFloat;

		for (U32F ii = 0; ii < numExits; ++ii)
		{
			const ApExit::AvailableExit* pExit = &availableExits[ii];

			if (pExit->m_animRotAngleDeg < bestAnimRotAngleDeg)
			{
				bestAnimRotAngleDeg = pExit->m_animRotAngleDeg;
				bestExitIndex = ii;
			}
		}
	}

	// look for to-stopped exit anims if no moving ones were found (maybe we have no path?)
	if (bestExitIndex == -1)
	{
		const U32F numExits = ApExit::ResolveDefaultExit(cs, pExitList, apRef, availableExits, kMaxAnimExits, apRefId);

		float bestAngleErrDeg = kLargeFloat;

		for (U32F ii = 0; ii < numExits; ++ii)
		{
			const ApExit::AvailableExit* pExit = &availableExits[ii];

			if (pExit->m_angleErrDeg < bestAngleErrDeg)
			{
				bestAngleErrDeg = pExit->m_angleErrDeg;
				bestExitIndex = ii;
			}
		}
	}

	if (bestExitIndex == -1)
	{
		return false;
	}

	if (pExitOut)
	{
		const ApExit::AvailableExit& selectedExit = availableExits[bestExitIndex];

		pExitOut->m_pDcDef		  = selectedExit.m_pDcDef;
		pExitOut->m_mirror		  = selectedExit.m_playMirrored;
		pExitOut->m_apRefId		  = selectedExit.m_apRefId;
		pExitOut->m_apReference	  = selectedExit.m_apRef;
		pExitOut->m_sbApReference = selectedExit.m_apRef;
		pExitOut->m_sbApReference.SetLocatorPs(selectedExit.m_rotApRefPs);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackController::DebugDrawEntryAnims(const ActionPackResolveInput& input, 
											   const ActionPack* pActionPack, 
											   const BoundFrame& apRef, 
											   const DC::ApEntryItemList& entryList, 
											   const DC::ApEntryItem* pChosenEntry, 
											   const NavCharOptions::ApControllerOptions& debugOptions) const
{
	STRIP_IN_FINAL_BUILD;

	const U32 kMaxAnimEntries = 50;

	const NavCharacter* pNavChar = GetCharacter();

	ApEntry::CharacterState entryCs;
	MakeEntryCharacterState(input, pActionPack, &entryCs);

	DC::ApEntryItemList singleList;
	const DC::ApEntryItem* pItem = pChosenEntry;
	const DC::ApEntryItemList* pListToUse = &entryList;

	if ((debugOptions.m_debugEnterIndex >= 0) && (debugOptions.m_debugEnterIndex < entryList.m_count))
	{
		pItem = &entryList.m_array[debugOptions.m_debugEnterIndex];
		pListToUse = &singleList;
	}

	singleList.m_array = pItem;
	singleList.m_count = pItem ? 1 : 0;

	switch (debugOptions.m_debugMode)
	{
	case NavCharOptions::kApAnimDebugModeEnterAnimsChosen:
		DebugDrawApEntryAnims(*pActionPack, apRef, entryCs, singleList, debugOptions);
		break;

	case NavCharOptions::kApAnimDebugModeEnterAnimsValid:
		DebugDrawApEntryAnims(*pActionPack, apRef, entryCs, *pListToUse, debugOptions, true);
		break;

	case NavCharOptions::kApAnimDebugModeEnterAnimsAll:
		DebugDrawApEntryAnims(*pActionPack, apRef, entryCs, *pListToUse, debugOptions);
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackController::DebugDrawExitAnims(const ActionPackResolveInput& input, 
											  const ActionPack* pActionPack, 
											  const BoundFrame& apRef,
											  const IPathWaypoints* pExitPathPs, 
											  const DC::ApExitAnimList& exitList, 
											  const DC::ApExitAnimDef* pChosenExit, 
											  const NavCharOptions::ApControllerOptions& debugOptions) const
{
	STRIP_IN_FINAL_BUILD;

	const U32 kMaxAnimEntries = 50;

	const NavCharacter* pNavChar = GetCharacter();

	ApExit::CharacterState exitCs;
	MakeExitCharacterState(pActionPack, pExitPathPs, &exitCs);

	exitCs.m_motionType = input.m_motionType;
	exitCs.m_mtSubcategory = input.m_mtSubcategory;

	const bool isVerbose = debugOptions.m_verboseAnimDebug;

	DC::ApExitAnimList singleList;
	const DC::ApExitAnimDef* pItem = pChosenExit;
	const DC::ApExitAnimList* pListToUse = &exitList;

	if ((debugOptions.m_debugExitIndex >= 0) && (debugOptions.m_debugExitIndex < exitList.m_count))
	{
		pItem = &exitList.m_array[debugOptions.m_debugExitIndex];
		pListToUse = &singleList;
	}

	singleList.m_array = pItem;
	singleList.m_count = pItem ? 1 : 0;

	switch (debugOptions.m_debugMode)
	{
	case NavCharOptions::kApAnimDebugModeExitAnimsValid:
		{
			if (pExitPathPs)
			{
				pExitPathPs->DebugDraw(input.m_frame.GetParentSpace(), false);
			}

			DebugDrawApExitAnims(apRef, exitCs, *pListToUse, false, isVerbose, true, SID("apReference"));
			DebugDrawApExitAnims(apRef, exitCs, *pListToUse, true, isVerbose, true, SID("apReference"));
		}
		break;

	case NavCharOptions::kApAnimDebugModeExitAnimsAll:
		if (pExitPathPs)
		{
			pExitPathPs->DebugDraw(input.m_frame.GetParentSpace(), false);
		}

		DebugDrawApExitAnims(apRef, exitCs, *pListToUse, false, isVerbose, false, SID("apReference"));
		DebugDrawApExitAnims(apRef, exitCs, *pListToUse, true, isVerbose, false, SID("apReference"));
		break;

	case NavCharOptions::kApAnimDebugModeExitAnimsChosen:
		{
			if (pExitPathPs)
			{
				pExitPathPs->DebugDraw(input.m_frame.GetParentSpace(), false);
			}

			DebugDrawApExitAnims(apRef, exitCs, singleList, false, isVerbose, false, SID("apReference"));
			DebugDrawApExitAnims(apRef, exitCs, singleList, true, isVerbose, false, SID("apReference"));
		}
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackController::EnterUsingMotionMatching(const ArtItemAnimHandle hGuideAnim,
													const BoundFrame& startAp,
													const BoundFrame& endAp,
													float startPhase,
													float endPhase,
													const StringId64 motionMatchSetId)
{
	NavCharacter* pNavChar		  = GetCharacter();
	NdSubsystemMgr* pSubsystemMgr = pNavChar ? pNavChar->GetSubsystemMgr() : nullptr;

	if (!hGuideAnim.ToArtItem() || !pSubsystemMgr)
	{
		return false;
	}

	AiMmActionPackInterface* pApMmInt = nullptr;

	SubsystemSpawnInfo interfaceSpawnInfo(SID("AiMmActionPackInterface"), pNavChar);
	pApMmInt = (AiMmActionPackInterface*)NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap,
															 interfaceSpawnInfo,
															 FILE_LINE_FUNC);

	pApMmInt->Configure(motionMatchSetId, hGuideAnim, startPhase, endPhase, startAp, endAp);

	CharacterMotionMatchLocomotion::SpawnInfo spawnInfo(SID("CharacterMotionMatchLocomotion"), pNavChar);
	spawnInfo.m_locomotionInterfaceType = SID("AiMmActionPackInterface");
	spawnInfo.m_hLocomotionInterface	= pApMmInt;

	NdSubsystem* pMmSubSys = NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap, spawnInfo, FILE_LINE_FUNC);
	CharacterMotionMatchLocomotion* pController = (CharacterMotionMatchLocomotion*)pMmSubSys;

	if (pController)
	{
		pApMmInt->SetParent(pController);
		pController->ResetCustomModelData();
	}
	else if (pApMmInt)
	{
		pApMmInt->Kill();
	}

	m_hMmController = pController;

	return pController != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const CharacterMotionMatchLocomotion* ActionPackController::GetMotionMatchController() const
{
	return m_hMmController.ToSubsystem();
}

/// --------------------------------------------------------------------------------------------------------------- ///
CharacterMotionMatchLocomotion* ActionPackController::GetMotionMatchController()
{
	return m_hMmController.ToSubsystem();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AiMmActionPackInterface* ActionPackController::GetMotionMatchInterface() const
{
	const AiMmActionPackInterface* pApMmInt = nullptr;

	if (const CharacterMotionMatchLocomotion* pController = GetMotionMatchController())
	{
		pApMmInt = pController->GetInterface<AiMmActionPackInterface>(SID("AiMmActionPackInterface"));
	}

	return pApMmInt;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AiMmActionPackInterface* ActionPackController::GetMotionMatchInterface()
{
	AiMmActionPackInterface* pApMmInt = nullptr;

	if (CharacterMotionMatchLocomotion* pController = GetMotionMatchController())
	{
		pApMmInt = pController->GetInterface<AiMmActionPackInterface>(SID("AiMmActionPackInterface"));
	}

	return pApMmInt;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackController::IsMotionMatchingEnterComplete(bool statusIfNoController /* = true */) const
{
	if (!m_hActionPack.IsValid())
	{
		return false;
	}

	bool complete = statusIfNoController;

	if (const CharacterMotionMatchLocomotion* pController = GetMotionMatchController())
	{
		if (pController->IsCharInIdle())
		{
			complete = true;
		}
		else if (const AiMmActionPackInterface* pApMmInt = pController->GetInterface<AiMmActionPackInterface>(SID("AiMmActionPackInterface")))
		{
			if (pApMmInt->IsComplete())
			{
				complete = true;
			}
		}
	}

	return complete;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
bool ActionPackController::TestForImmediateEntry(const NdGameObject* pGo, const ActionPackEntryDef& apEntry)
{
	if (!pGo)
		return false;

	if (!apEntry.m_hResolvedForAp.IsValid())
		return false;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const Locator& charAlignPs = pGo->GetLocatorPs();
	const Vector charVelPs = pGo->GetVelocityPs();
	const Point entryPosWs = apEntry.m_entryNavLoc.GetPosWs();
	const Point entryPosPs = pGo->GetParentSpace().UntransformPoint(entryPosWs);

	Scalar charSpeed = kZero;
	const Vector velDirPs = AsUnitVectorXz(charVelPs, kZero, charSpeed);
	const Vector approachDirPs = AsUnitVectorXz(entryPosPs - charAlignPs.Pos(), kZero);

	const float cosThreshold = 0.707106781f; // 45 deg
	const float distThreshold = 0.25f;
	const float yThreshold = 1.5f;

	const bool stopped = charSpeed < 0.5f;
	const float approachDot = Dot(charVelPs, approachDirPs);
	const bool movingTowardsGoal = !stopped && (approachDot > cosThreshold);

	if (movingTowardsGoal)
	{
		return false;
	}

	const float cosValue = Dot(GetLocalZ(charAlignPs), GetLocalZ(apEntry.m_entryRotPs));
	const float distValue = DistXz(charAlignPs.Pos(), entryPosPs);
	const float yDiff = Abs(charAlignPs.Pos().Y() - entryPosPs.Y());

	const bool bFacingSatisfied = cosValue > cosThreshold;
	const bool bDistSatisfied = (distValue < distThreshold) && (yDiff < yThreshold);

	if (bFacingSatisfied && bDistSatisfied)
	{
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackController::GetMmEnterStatus(StringId64& animIdOut, float& phaseOut) const
{
	bool valid = false;

	if (const CharacterMotionMatchLocomotion* pController = GetMotionMatchController())
	{
		if (const AiMmActionPackInterface* pApMmInt = pController->GetInterface<AiMmActionPackInterface>(SID("AiMmActionPackInterface")))
		{
			const ArtItemAnim* pAnim = pApMmInt->GetGuideAnim();
			phaseOut = pApMmInt->GetCurPhase();
			animIdOut = pAnim ? pAnim->GetNameId() : INVALID_STRING_ID_64;
			valid = true;
		}
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiMmActionPackInterface::Update(const Character* pChar, MotionModel& modelPs)
{
	m_prevPhase = m_curPhase;

	if (modelPs.IsCustomDataValid())
	{
		const CustomStepData& data = modelPs.GetCustomData<CustomStepData>();
		m_curPhase = data.m_phase;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiMmActionPackInterface::Configure(const StringId64 motionMatchSetId,
										const ArtItemAnimHandle hGuideAnim,
										float startPhase,
										float maxEndPhase,
										const BoundFrame& startAp,
										const BoundFrame& endAp)
{
	ANIM_ASSERT(hGuideAnim.ToArtItem());

	m_valid = true;

	m_motionMatchSetId = motionMatchSetId;
	m_hGuideAnim	   = hGuideAnim;

	m_curPhase = m_prevPhase = startPhase;

	m_startPhase = startPhase;
	m_endPhase	 = DetermineEndPhase(hGuideAnim.ToArtItem(), startPhase, maxEndPhase, false);

	m_startAp = startAp;
	m_endAp	  = endAp;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiMmActionPackInterface::SetCurPhase(float phase, MotionModel& model)
{
	m_curPhase = phase;
	CustomStepData& data = model.GetCustomData<CustomStepData>();
	data.m_phase = phase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiMmActionPackInterface::DebugDraw(const Character* pChar, const MotionModel& modelPs) const
{
	STRIP_IN_FINAL_BUILD;

	if (!g_motionMatchingOptions.m_drawOptions.m_drawMotionModel)
		return;

	const ArtItemAnim* pGuideAnim = m_hGuideAnim.ToArtItem();

	if (!pGuideAnim)
		return;

	const Point modelPosPs = modelPs.GetPos() + kUnitYAxis;
	const Point modelPosWs = pChar->GetParentSpace().TransformPoint(modelPosPs);
	g_prim.Draw(DebugString(modelPosWs,
							StringBuilder<256>("'%s' @ %0.3f [%0.3f -> %0.3f]",
											   pGuideAnim->GetName(),
											   m_curPhase,
											   m_startPhase,
											   m_endPhase)
							.c_str(),
							kColorWhiteTrans,
							0.6f));

	// DebugDrawPoseAtPhase(hGuideAnim, m_curPhase, kColorRed);

	const float animDuration = GetDuration(pGuideAnim);
	const float curTime = EngineComponents::GetNdFrameState()->GetClock(kRealClock)->GetCurTime().ToSeconds();
	const float poseTime = fmodf(curTime, animDuration);
	const float posePhase = Limit01(poseTime / animDuration);

	DebugDrawPoseAtPhase(posePhase, kColorRedTrans);

	DebugDrawGuidePath(kColorCyanTrans);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiMmActionPackInterface::DebugDrawPoseAtPhase(float phase, Color c) const
{
	STRIP_IN_FINAL_BUILD;

	const ArtItemAnim* pGuideAnim = m_hGuideAnim.ToArtItem();
	if (!pGuideAnim || phase < 0.0f || phase > 1.0f)
		return;

	AnimSample curSample(m_hGuideAnim, phase);

	const float tt = LerpScale(m_startPhase, m_endPhase, 0.0f, 1.0f, phase);
	const float blendVal = CalculateCurveValue(tt, DC::kAnimCurveTypeEaseIn);

	const Locator apRefWs = Lerp(m_startAp.GetLocatorWs(), m_endAp.GetLocatorWs(), blendVal);

	Locator alignWs = kIdentity;
	if (!FindAlignFromApReference(pGuideAnim->m_skelID, pGuideAnim, phase, apRefWs, SID("apReference"), &alignWs))
		return;

	MotionMatchingDebug::DebugDrawFullAnimPose(curSample, alignWs, c);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiMmActionPackInterface::DebugDrawGuidePath(Color c) const
{
	STRIP_IN_FINAL_BUILD;

	const ArtItemAnim* pGuideAnim = m_hGuideAnim.ToArtItem();

	if (!pGuideAnim)
		return;

	const Locator startLocWs = m_startAp.GetLocatorWs();
	const Locator endLocWs = m_endAp.GetLocatorWs();

	Locator prevLocWs = kIdentity;

	if (!FindAlignFromApReference(pGuideAnim->m_skelID,
								  pGuideAnim,
								  m_startPhase,
								  startLocWs,
								  SID("apReference"),
								  &prevLocWs))
	{
		return;
	}

	float phase = m_startPhase;

	do
	{
		const float tt = LerpScale(m_startPhase, m_endPhase, 0.0f, 1.0f, phase);
		const float blendVal = CalculateCurveValue(tt, DC::kAnimCurveTypeEaseIn);

		const Locator apRefWs = Lerp(startLocWs, endLocWs, blendVal);
		Locator locWs;
		if (!FindAlignFromApReference(pGuideAnim->m_skelID, pGuideAnim, phase, apRefWs, SID("apReference"), &locWs))
			continue;

		g_prim.Draw(DebugLine(prevLocWs.Pos(), locWs.Pos(), c, 2.0f, kPrimEnableHiddenLineAlpha));

		if (phase >= m_endPhase)
			break;

		prevLocWs = locWs;

		phase += pGuideAnim->m_pClipData->m_phasePerFrame;
		phase = Min(m_endPhase, phase);
	} while (true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiMmActionPackInterface::GetInput(ICharacterLocomotion::InputData* pData)
{
	ANIM_ASSERT(m_valid);

	pData->m_desiredFacingPs	  = MAYBE::kNothing;
	pData->m_desiredVelocityDirPs = kZero;
	pData->m_setId = m_motionMatchSetId;
	pData->m_groundNormalPs = kUnitYAxis;

	const NdGameObject* pGo			= GetOwnerGameObject();
	const AnimControl* pAnimControl = pGo ? pGo->GetAnimControl() : nullptr;

	const DC::AnimNavCharInfo* pInfo = pAnimControl ? pAnimControl->Info<DC::AnimNavCharInfo>() : nullptr;

	if (pInfo)
	{
		pData->m_speedScale = pInfo->m_speedFactor;
	}
	else
	{
		pData->m_speedScale = 1.0f;
	}

	if (m_bShouldUseCoverShare)
		pData->m_mmParams.AddGoalLocator(SID("apReference-cover-share"), m_coverShareLocator);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const IMotionPose* AiMmActionPackInterface::GetPose(const MotionMatchingSet* pArtItemSet, bool debug)
{
	ANIM_ASSERT(m_valid);

	const Character* pChar = GetOwnerCharacter();

	const IMotionPose* pPose = nullptr;

	if (const PoseTracker* pPoseTracker = pChar->GetPoseTracker())
	{
		pPose = &pPoseTracker->GetPose();
	}

	return pPose;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiMmActionPackInterface::DoCustomModelStep(MotionModel& model,
												const MotionModelInput& input,
												float deltaTime,
												Locator* pAlignOut,
												float* pClampFactorOut,
												bool debug) const
{
	CustomStepData& data = model.GetCustomData<CustomStepData>();

	const ArtItemAnim* pAnim = m_hGuideAnim.ToArtItem();
	if (!pAnim)
	{
		return false;
	}

	if (data.m_phase < 0.0f)
	{
		data.m_phase = m_startPhase;
	}

	const float animDuration = GetDuration(pAnim);
	const float deltaPhase = (animDuration > 0.0f) ? (deltaTime / animDuration) : kLargeFloat;

	data.m_phase = Limit01(data.m_phase + deltaPhase);

	const float tt = LerpScale(m_startPhase, m_endPhase, 0.0f, 1.0f, data.m_phase);
	const float blendVal = CalculateCurveValue(tt, DC::kAnimCurveTypeEaseIn);

	const Locator apRef = Lerp(m_startAp.GetLocatorPs(), m_endAp.GetLocatorPs(), blendVal);

	if (!FindAlignFromApReference(pAnim->m_skelID, pAnim, data.m_phase, apRef, SID("apReference"), pAlignOut))
	{
		return false;
	}

	*pClampFactorOut = Limit01(1.0f - (2.0f * tt));

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiMmActionPackInterface::GetExtraSample(AnimSampleBiased& extraSample) const
{
	if (m_curPhase < 0.0f)
		return false;

	const float costBias = m_curPhase * 2.0f;
	extraSample = AnimSampleBiased(AnimSample(m_hGuideAnim, m_curPhase), costBias);
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
float AiMmActionPackInterface::DetermineEndPhase(const ArtItemAnim* pGuideAnim,
												 float startPhase,
												 float maxEndPhase,
												 bool mirror)
{
	// This could be done in tools, perhaps a larger ap entry bundle build process

	if (!pGuideAnim)
	{
		return maxEndPhase;
	}

	const float animDuration = pGuideAnim->m_pClipData->m_fNumFrameIntervals
							   * pGuideAnim->m_pClipData->m_secondsPerFrame;
	const float phaseStep = pGuideAnim->m_pClipData->m_phasePerFrame;

	Locator endAlignLs = kIdentity;
	EvaluateChannelInAnim(pGuideAnim->m_skelID, pGuideAnim, SID("align"), 1.0f, &endAlignLs, mirror);

	float endPhase = maxEndPhase;

	for (float evalPhase = 1.0f - phaseStep; evalPhase > startPhase; evalPhase -= phaseStep)
	{
		Locator alignLs = kIdentity;
		if (!EvaluateChannelInAnim(pGuideAnim->m_skelID, pGuideAnim, SID("align"), evalPhase, &alignLs, mirror))
		{
			break;
		}

		const float remDist = Dist(alignLs.Pos(), endAlignLs.Pos());
		const float orientDot = Dot(GetLocalZ(alignLs.Rot()), GetLocalZ(endAlignLs.Rot()));

		const bool distMatch = remDist < 0.025f;
		const bool orientMatch = orientDot > 0.99f;

		if (!distMatch || !orientMatch)
		{
			endPhase = evalPhase + phaseStep;
			break;
		}
	}

	return endPhase;
}

TYPE_FACTORY_REGISTER(AiMmActionPackInterface, CharacterLocomotionInterface);
