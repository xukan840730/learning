/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent punishable by burning with hot shell casings
 */

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/net/nd-net-game-manager.h"
#include "ndlib/net/nd-net-info.h"
#include "ndlib/net/nd-net-player-tracker.h"
#include "ndlib/script/script-args.h"
#include "ndlib/script/script-material-param.h"

#include "gamelib/gameplay/character-motion-match-locomotion.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/health-system.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nd-interactable.h"
#include "gamelib/gameplay/nd-subsystem-mgr.h"
#include "gamelib/script/nd-script-arg-iterator.h"
#include "gamelib/script/nd-script-args.h"
#include "gamelib/scriptx/h/anim-character-defines.h"
#include "gamelib/scriptx/h/nd-gore-defines.h"
#include "gamelib/scriptx/h/nd-script-func-defines.h"
#include "gamelib/state-script/ss-context.h"
#include "gamelib/tasks/task-graph-mgr.h"
#include "gamelib/tasks/task-subnode.h"

/// --------------------------------------------------------------------------------------------------------------- ///
void ForceLinkCharacterScriptFuncs()
{
	// needed to avoid dead stripping of this entire translation unit
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(bool, dcCharacterInIFrameP, (Character& character), "character-in-i-frame?")
{
	return character.GetHealthSystem()->InIFrame() || character.GetHealthSystem()->InCoreFrame();
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(void, dcGiveIFrames, (Character& character, float duration), "give-i-frames")
{
	character.GetHealthSystem()->GiveIFrames(Seconds(duration));

	if (NdNetPlayerTracker *pTracker = g_ndConfig.m_pNetInfo->IsNetActive() ? g_ndConfig.m_pNetInfo->m_pNetGameManager->GetNdNetPlayerTrackerFromProcess(&character) : nullptr)
	{
		pTracker->LogIFrameWindow(EngineComponents::GetNdFrameState()->GetClock(kNetworkClock)->GetCurTime(), duration);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(void, dcGiveCoreFrames, (Character& character, float duration), "give-core-frames")
{
	character.GetHealthSystem()->GiveCoreFrames(Seconds(duration));
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("character-set-invincibility", DcCharacterSetInvincibility)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	Character* pCharacter = args.NextCharacter();
	DC::Invincibility invincibility = args.NextI32();
	bool excludeExplosionsFromInvinc = args.NextBoolean();
	if (pCharacter)
	{
		if (invincibility == DC::kInvincibilityInvalid)
			args.MsgScriptError("Setting invalid invincibility (%d) on character %s\n", invincibility, pCharacter->GetName());
		if (pCharacter->IsDead() && invincibility != DC::kInvincibilityNone)
			MsgScript("WARNING: %s: Setting invincibility '%s' on DEAD character %s\n", args.GetDcFunctionName(), DC::GetInvincibilityName(invincibility), pCharacter->GetName());

		pCharacter->GetHealthSystem()->SetInvincibility(invincibility, !excludeExplosionsFromInvinc);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("character-set-invincibility/f", DcCharacterSetInvincibilityF)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	Character* pCharacter = args.NextCharacter();
	DC::Invincibility invincibility = args.NextI32();
	bool excludeExplosionsFromInvinc = args.NextBoolean();
	if (pCharacter)
	{
		if (invincibility == DC::kInvincibilityInvalid)
			args.MsgScriptError("Overriding invalid invincibility (%d) on character %s\n", invincibility, pCharacter->GetName());
		if (pCharacter->IsDead() && invincibility != DC::kInvincibilityNone)
			MsgScript("WARNING: %s: Overriding invincibility '%s' on DEAD character %s\n", args.GetDcFunctionName(), DC::GetInvincibilityName(invincibility), pCharacter->GetName());

		pCharacter->GetHealthSystem()->OverrideInvincibilityThisFrame(invincibility, !excludeExplosionsFromInvinc);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("character-set-gender-is-female", DcCharacterSetGender)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	Character* pCharacter = args.NextCharacter();
	bool isFemale = args.NextBoolean();
	if (pCharacter)
	{
		pCharacter->SetGenderIsFemale(isFemale);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("character-set-invincibility-cannot-die-threshold", DcCharacterSetInvincibilityCannotDieThreshold)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	Character* pCharacter = args.NextCharacter();
	I32 thresh = args.NextI32();

	if (pCharacter)
	{
		pCharacter->GetHealthSystem()->SetInvincibilityThreshold(thresh);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("character-sleep-ragdoll", DcCharacterSleepRagdoll)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	if (Character* pCharacter = args.NextCharacter())
	{
		pCharacter->SleepRagdoll();
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("character-is-dead?", DcCharacterIsDeadP)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	if (Character* pCharacter = args.NextCharacter())
	{
		return ScriptValue(pCharacter->IsDead());
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("character-is-down?", DcCharacterIsDownP)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	if (Character* pCharacter = args.NextCharacter())
	{
		return ScriptValue(pCharacter->IsDown());
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-prop-id", DcGetPropId)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject* pCharacter = args.NextGameObject();
	StringId64 propId = args.NextStringId();
	if (pCharacter && propId != INVALID_STRING_ID_64)
	{
		BoxedValue boxedPropName = SendEvent(SID("get-prop-id"), pCharacter, propId);
		if (boxedPropName.IsValid())
		{
			return ScriptValue(boxedPropName.GetStringId());
		}
	}
	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("spawn-prop", DcSpawnProp)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	Character* pCharacter = args.NextCharacter();
	StringId64 propId = args.NextStringId();
	if (pCharacter && propId != INVALID_STRING_ID_64)
	{
		SendEvent(SID("spawn-prop"), pCharacter, propId);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("destroy-prop", DcDestroyProp)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	Character* pCharacter = args.NextCharacter();
	StringId64 propId = args.NextStringId();
	if (pCharacter && propId != INVALID_STRING_ID_64)
	{
		SendEvent(SID("destroy-prop"), pCharacter, propId);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("has-helmet?", DcHasHelmetP)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	if (Character* pCharacter = args.NextCharacter())
	{
		BoxedValue boxedPropName = SendEvent(SID("has-helmet?"), pCharacter);
		return ScriptValue(boxedPropName.GetBool());
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-vulnerability", DcGetVulnerability)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	Character* pCharacter = args.NextCharacter();
	if (pCharacter)
	{
		return ScriptValue(pCharacter->GetVulnerability());
	}
	return ScriptValue(DC::kVulnerabilityNone);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("set-animation-vulnerability-override", DcSetAnimationVulnerabilityOverride)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	Character* pCharacter = args.NextCharacter();
	DC::Vulnerability vulnerability = args.NextI32();
	if (pCharacter)
	{
		pCharacter->SetAnimationVulnerabilityOverride(vulnerability);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("override-gore-filter", DcOverrideGoreFilter)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	Character* pCharacter = args.NextCharacter();
	DC::GoreFilter filter = args.NextI32();
	if (pCharacter)
	{
		pCharacter->SetGoreFilter(filter);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("set-back-splatter-blood-allowed", DcSetBackSplatterBloodAllowed)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	Character* pCharacter = args.NextCharacter();
	bool allow = args.NextBoolean();
	if (pCharacter)
	{
		pCharacter->SetBackSplatterBloodAllowed(allow);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("clear-animation-vulnerability-override", DcClearAnimationVulnerabilityOverride)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	Character* pCharacter = args.NextCharacter();
	if (pCharacter)
	{
		pCharacter->ClearAnimationVulnerabilityOverride();
	}
	return ScriptValue(0);
}


/// --------------------------------------------------------------------------------------------------------------- ///
/*
	;;Force a character to play a specific hit reaction
	(define-c-function character-play-additive-hit-reaction ((player symbol) (anim-name symbol) (additive-blend float (:default 1.0)) (mirror boolean (:default #f)))
		void)
*/
SCRIPT_FUNC("character-play-additive-hit-reaction", DcCharacterPlayAdditiveHitReaction)
{
	SCRIPT_ARG_ITERATOR(args, 4);
	Character* pCharacter = args.NextCharacter();
	StringId64 animName = args.NextStringId();
	float additiveBlend = args.NextFloat();
	bool mirror = args.NextBoolean();

	SendEvent(SID("play-additive-hit-reaction"), pCharacter, BoxedValue(animName), BoxedValue(additiveBlend), BoxedValue(mirror));
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function character-abort-additive-hit-reaction ((player symbol) (blend-out-time float (:default 0.2)))
		void)
*/
SCRIPT_FUNC("character-abort-additive-hit-reaction", DcCharacterAbortAdditiveHitReaction)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	Character* pCharacter = args.NextCharacter();
	const float blendOutTime = args.NextFloat();

	SendEvent(SID("abort-additive-hit-reaction"), pCharacter, BoxedValue(blendOutTime));
	return ScriptValue(0);
}


/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function is-character? ((character symbol))
		boolean)
*/

SCRIPT_FUNC("is-character?", DcIsCharacterP)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pObj = args.NextGameObject(kItemMissingOk);

	if (pObj && pObj->IsKindOf(g_type_Character))
		return ScriptValue(true);

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function character-is-climbing? ((character symbol))
		boolean)
*/
SCRIPT_FUNC("character-is-climbing?", DcCharacterIsClimbingP)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pObj = args.NextGameObject(kItemMissingOk);

	if (const Character* pChar = Character::FromProcess(pObj))
	{
		return ScriptValue(pChar->IsClimbing());
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function character-is-swimming? ((character symbol))
		boolean)
*/

SCRIPT_FUNC("character-is-swimming?", DcCharacterIsSwimmingP)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pObj = args.NextGameObject(kItemMissingOk);

	if (const Character* pChar = Character::FromProcess(pObj))
	{
		return ScriptValue(pChar->IsSwimming());
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function character-allow-dead-ragdoll-snap-back-to-animation ((character symbol) (allow boolean))
		none)
*/

SCRIPT_FUNC("character-allow-dead-ragdoll-snap-back-to-animation", DcCharacterAllowDeadRagdollSnapBackToAnimation)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject* pObj = args.NextGameObject();
	bool b = args.NextBoolean();

	if (Character* pChar = Character::FromProcess(pObj))
	{
		pChar->SetAllowDeadRagdollSnapBackToAnimation(b);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(bool,
								 DcAnimScriptFloatValue,
								 (Character& character, float fVal),
								 "set-anim-info-script-float-value")
{
	AnimControl* pAnimControl = character.GetAnimControl();
	DC::AnimCharacterInfo* pInfo = pAnimControl ? pAnimControl->Info<DC::AnimCharacterInfo>() : nullptr;

	if (!pInfo)
		return false;

	pInfo->m_scriptFloatValue = fVal;
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(const Vector*,
								 DcCharacterGetFutureFacing,
								 (Character& character, float futureTimeSec, bool debug),
								 "character-get-future-facing")
{
	const NdSubsystemAnimController* pController = character.GetActiveSubsystemController(SID("CharacterMotionMatchLocomotion"));
	const CharacterMotionMatchLocomotion* pMmController = (const CharacterMotionMatchLocomotion*)pController;

	const Locator locWs = character.GetLocator();

	Vector facingOs = kUnitZAxis;

	const ArtItemAnim* pAnimUsed = nullptr;
	float phaseUsed = 0.0f;

	if (pMmController)
	{
		pMmController->GetFutureFacingOs(futureTimeSec, &facingOs);
	}
	else
	{
		const AnimControl* pAnimControl = character.GetAnimControl();
		const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
		const AnimStateInstance* pCurrentInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;

		const ArtItemAnim* pCurrentAnim = pCurrentInstance ? pCurrentInstance->GetPhaseAnimArtItem().ToArtItem() : nullptr;

		if (pCurrentAnim)
		{
			const float curPhase = pCurrentInstance->Phase();
			const float desPhase = Limit01(curPhase + (futureTimeSec / Max(GetDuration(pCurrentAnim), 0.001f)));

			Locator animStartLocOs;
			Locator animEndLocOs;

			bool valid = true;
			valid = valid && EvaluateChannelInAnim(pCurrentAnim->m_skelID, pCurrentAnim, SID("align"), curPhase, &animStartLocOs);
			valid = valid && EvaluateChannelInAnim(pCurrentAnim->m_skelID, pCurrentAnim, SID("align"), desPhase, &animEndLocOs);

			const Locator deltaLocOs = animStartLocOs.UntransformLocator(animEndLocOs);
			facingOs = GetLocalZ(deltaLocOs);

			pAnimUsed = pCurrentAnim;
			phaseUsed = desPhase;
		}
	}

	const Vector facingWs = locWs.TransformVector(facingOs);
	Vector* pResult = NDI_NEW(kAllocSingleFrame, kAlign16) Vector(facingWs);

	if (FALSE_IN_FINAL_BUILD(debug))
	{
		StringBuilder<256> src;

		bool failed = false;

		if (pMmController)
		{
			src.append_format("Motion Matching +%0.3fsec", futureTimeSec);
		}
		else if (pAnimUsed)
		{
			src.append_format("Anim '%s' @ %f", pAnimUsed->GetName(), phaseUsed);
		}
		else
		{
			src.append("FAILED :(");
			failed = true;
		}

		const Color clr = failed ? kColorRed : kColorWhite;

		g_prim.Draw(DebugArrow(locWs.Pos() + Vector(kUnitYAxis), facingWs, clr, 0.5f, kPrimEnableHiddenLineAlpha), Seconds(5.0f));
		g_prim.Draw(DebugString(locWs.Pos() + Vector(kUnitYAxis) + facingWs, src.c_str(), clr, 0.5f), Seconds(5.0f));
	}

	return pResult;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(void,
								 DcCharacterRequestAnimRefresh,
								 (Character& character, float blendTime),
								 "character-request-anim-refresh")
{
	NdSubsystemMgr* pSubSysMgr = character.GetSubsystemMgr();
	NdSubsystemAnimController* pController = pSubSysMgr ? pSubSysMgr->GetActiveSubsystemController() : nullptr;

	FadeToStateParams* pParams = nullptr;
	FadeToStateParams params;
	if (blendTime >= 0.0f)
	{
		params.m_animFadeTime = params.m_motionFadeTime = blendTime;
		pParams = &params;
	}

	if (pController)
	{
		pController->RequestRefreshAnimState(pParams);
	}
	else if (AnimControl* pAnimControl = character.GetAnimControl())
	{
		AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

		if (pBaseLayer && pBaseLayer->IsTransitionValid(SID("demeanor-change")))
		{
			pBaseLayer->RequestTransition(SID("demeanor-change"), pParams);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(void,
								 DcCharacterSuppressFootPlantIkF,
								 (Character & character),
								 "character-suppress-foot-plant-ik/f")
{
	character.SuppressFootPlantIkThisFrame();
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("camera-set-npc-fade-out-expand/f", DcCameraSetNpcFadeOutExpandF)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject* pObj = args.NextGameObject();
	float surfaceExpand = args.NextFloat();

	Character* pChar = Character::FromProcess(pObj);
	if (pChar != nullptr)
	{
		pChar->OverrideCameraSurfaceExpand(surfaceExpand);
	}

	return ScriptValue(0);
}