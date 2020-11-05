/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/script-controller.h"

#include "corelib/util/angle.h"

#include "ndlib/anim/anim-actor.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-layer.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/armik.h"
#include "ndlib/anim/attach-system.h"
#include "ndlib/anim/ik/two-bone-ik.h"
#include "ndlib/anim/joint-modifiers/joint-modifiers.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/process/event.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/anim/blinking.h"
#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/level/artitem.h"
#include "gamelib/state-script/ss-action.h"
#include "gamelib/state-script/ss-animate.h"

#include "game/ai/agent/npc.h"
#include "game/ai/base/ai-game-util.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/requests/requests.h"
#include "game/ai/skill/skill-mgr.h"
#include "game/ai/skill/skill.h"
#include "game/event-attack.h"
#include "game/player/player.h"
#include "game/player/player-ride-horse.h"
#include "game/scriptx/h/anim-npc-info.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ArmIkFactorBlender : public AnimStateLayer::InstanceBlender<float>
{
public:
	virtual ~ArmIkFactorBlender() override {}

	virtual float GetDefaultData() const override { return 0.0f; }
	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, float* pDataOut) override
	{
		const DC::AnimNpcTopInfo* pCurTopInfo = (const DC::AnimNpcTopInfo*)pInstance->GetAnimTopInfo();
		*pDataOut = pCurTopInfo->m_scriptArmIkEnabled ? 1.0f : 0.0f;
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
};

/// --------------------------------------------------------------------------------------------------------------- ///
// AiScriptController
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
AiScriptController::AiScriptController(NdGameObject& npc, StringId64 layerId, bool additive, LayerType layerType)
	: SsAnimateController(npc, layerId, additive, layerType)
	, m_noPushPlayer(false)
	, m_isBusy(false)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
Npc& AiScriptController::GetCharacter() const
{
	Npc* pNpc = Npc::FromProcess(GetOwner());
	ALWAYS_ASSERT(pNpc); // this is not strictly valid... GetOwner() can return nullptr for killed processes
	return *pNpc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiScriptController::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	SsAnimateController::Relocate(deltaPos, lowerBound, upperBound);
	AnimActionController::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiScriptController::UpdateStatus()
{
	m_isBusy = IsPlaying();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiScriptController::IsBusy() const
{
	return m_isBusy;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// AiSimpleScriptController
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
AiSimpleScriptController::AiSimpleScriptController(NdGameObject& npc, StringId64 layerId, bool additive)
	: AiScriptController(npc, layerId, additive, kSimpleLayer)
{
	m_isBusy = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiSimpleScriptController::UpdateStatus()
{
	m_isBusy = IsAnimating();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiSimpleScriptController::IsBusy() const
{
	const SsAnimateParams& params = GetParams();

	return m_isBusy && params.m_interruptNavigation;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// AiFullBodyScriptController
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
AiFullBodyScriptController::AiFullBodyScriptController(NdGameObject& npc)
: AiScriptController(npc, SID("base"), false, kStateLayer)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
SsAnimateController::Error AiFullBodyScriptController::RequestStartAnimation(const SsAnimateParams& params,
																			 MutableProcessHandle hSourceProcess)
{
	Npc& npc = GetCharacter();

	m_noPushPlayer = params.m_noPushPlayer;

	DC::AnimNpcInstanceInfo* pInstaceInfo = npc.GetAnimControl()->InstanceInfo<DC::AnimNpcInstanceInfo>();

	return AiScriptController::RequestStartAnimation(params, hSourceProcess);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiFullBodyScriptController::Reset()
{
	if (m_isBusy)
	{
		const Npc& npc = GetCharacter();
		AiLogAnim(&npc, "AiFullBodyScriptController::Reset(), was busy, forcing not animating anymore\n");

		ForceNotAnimatingAnymore();
		m_isBusy = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiFullBodyScriptController::RequestAnimations()
{
	AiScriptController::RequestAnimations();

	if (false /*m_isBusy*/)
	{
		Npc& npc = GetCharacter();

		if (BlinkController* pBlinkController = npc.GetBlinkController())
		{
			pBlinkController->SuppressBlinking(Seconds(0.25f));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiFullBodyScriptController::UpdateStatus()
{
	const SsAnimateParams& animateParams = GetParams();

	AnimStateLayer* pLayer = GetStateLayer();
	const StateChangeRequest::ID changeId = GetStateChangeRequestId();
	AnimStateInstance* pDestInstance	  = pLayer ? pLayer->GetTransitionDestInstance(changeId) : nullptr;

	if (pDestInstance)
	{
		DC::AnimStateFlag& stateFlags = pDestInstance->GetMutableStateFlags();

		const bool wantSta = animateParams.m_exitMode != DC::kAnimateExitWalkRun;
		const bool hasSta  = (stateFlags & DC::kAnimStateFlagSaveTopAlign) != 0;

		if (hasSta != wantSta)
		{
			if (wantSta)
			{
				stateFlags |= DC::kAnimStateFlagSaveTopAlign;
			}
			else
			{
				stateFlags &= ~DC::kAnimStateFlagSaveTopAlign;
			}
		}
	}

	AiScriptController::UpdateStatus();

	if (m_isBusy)
	{
		switch (animateParams.m_animIkMode)
		{
		case DC::kAnimIkModeHandLegFromAnim:
		case DC::kAnimIkModeHandFromAnimLegToGround:
		case DC::kAnimIkModeRightHandFromAnimLegToGround:
			{
				const DC::AnimNpcTopInfo* pDestTopInfo = pDestInstance
															 ? (const DC::AnimNpcTopInfo*)pDestInstance->GetAnimTopInfo()
															 : nullptr;
				if (pDestTopInfo && !pDestTopInfo->m_scriptArmIkEnabled)
				{
					DC::AnimNpcTopInfo topInfo = *pDestTopInfo;
					topInfo.m_scriptArmIkEnabled = true;
					pDestInstance->UpdateTopInfo(&topInfo);
				}
			}
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiFullBodyScriptController::UpdateProcedural()
{
	NdGameObject* pOwner = GetOwner();
	const AnimStateLayer* pLayer = GetStateLayer();

	if (!pOwner || !pLayer)
		return;

	ArmIkFactorBlender armIkBlender;
	const float armIkBlend = armIkBlender.BlendForward(pLayer, 0.0f);

	if (armIkBlend > 0.0f)
	{
		const SsAnimateParams& animateParams = GetParams();

		FgAnimData* pAnimData = pOwner ? pOwner->GetAnimData() : nullptr;
		JointSet* pJointSet = pAnimData ? pAnimData->m_pPluginJointSet : nullptr;
		const AnimStateInstance* pInstance = pLayer->GetTransitionDestInstance(GetStateChangeRequestId());

		if (pJointSet && pInstance)
		{
			StringId64 channelIds[2];
			channelIds[kLeftArm] = animateParams.m_armIkLeftTargetId;
			channelIds[kRightArm] = animateParams.m_armIkRightTargetId;
			ndanim::JointParams targetJp[2];
			const U32F evalMask = pInstance->EvaluateChannels(channelIds, 2, targetJp);

			if (evalMask != 0)
			{
				ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

				pJointSet->ReadJointCache();

				TwoBoneIkParams params;
				params.m_objectSpace = true;
				params.m_pJointSet	 = pJointSet;
				params.m_tt = armIkBlend;

				TwoBoneIkResults results;

				if (evalMask & 0x1)
				{
					params.m_goalPos		 = targetJp[kLeftArm].m_trans;
					params.m_finalGoalRot	 = targetJp[kLeftArm].m_quat;
					params.m_jointOffsets[0] = pJointSet->FindJointOffset(ArmIkChain::kJointIds[kLeftArm][1]);
					params.m_jointOffsets[1] = pJointSet->FindJointOffset(ArmIkChain::kJointIds[kLeftArm][2]);
					params.m_jointOffsets[2] = pJointSet->FindJointOffset(ArmIkChain::kJointIds[kLeftArm][3]);

					SolveTwoBoneIK(params, results);
				}

				if (evalMask & 0x2)
				{
					params.m_goalPos		 = targetJp[kRightArm].m_trans;
					params.m_finalGoalRot	 = targetJp[kRightArm].m_quat;
					params.m_jointOffsets[0] = pJointSet->FindJointOffset(ArmIkChain::kJointIds[kRightArm][1]);
					params.m_jointOffsets[1] = pJointSet->FindJointOffset(ArmIkChain::kJointIds[kRightArm][2]);
					params.m_jointOffsets[2] = pJointSet->FindJointOffset(ArmIkChain::kJointIds[kRightArm][3]);

					SolveTwoBoneIK(params, results);
				}

				pJointSet->WriteJointCacheBlend(armIkBlend, true);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::AnimNpcScriptInfo& AiFullBodyScriptController::GetNpcScriptInfo() const
{
	NdGameObject* pOwner = GetOwner();
	AI_ASSERT(pOwner);
	AnimControl* pAnimControl = pOwner->GetAnimControl();
	AI_ASSERT(pAnimControl);
	DC::AnimNpcTopInfo* pTopInfo = pAnimControl->TopInfo<DC::AnimNpcTopInfo>();
	AI_ASSERT(pTopInfo);

	switch (GetLayerId().GetValue())
	{
	case SID_VAL("base"):
		return pTopInfo->m_scriptFullBody;
	case SID_VAL("s_script-gesture-0"):
	case SID_VAL("s_script-gesture-1"):
	case SID_VAL("s_script-gesture-2"):
	case SID_VAL("s_script-gesture-3"):
	default:
		AI_ASSERT(false);
		return pTopInfo->m_scriptFullBody;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::AnimScriptInfo& AiFullBodyScriptController::HookGetScriptInfo() const
{
	return GetNpcScriptInfo();
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AiFullBodyScriptController::HookGetPluggableStateId(const SsAnimateParams& params, bool isLooping) const
{
	switch (GetLayerId().GetValue())
	{
	case SID_VAL("s_script-gesture-0"):
	case SID_VAL("s_script-gesture-1"):
	case SID_VAL("s_script-gesture-2"):
	case SID_VAL("s_script-gesture-3"):
	default:
		AI_ASSERT(false);
		FALLTHROUGH;

	case SID_VAL("base"):
		{
			BoundFrame apLocator;
			const bool alignToAp = params.ShouldCharacterAlignToApRef(GetOwner());
			const bool stayOnNav = (params.m_collisionMode == DC::kAnimateCollisionStayOnNav)
								   || (params.m_collisionMode == DC::kAnimateCollisionGroundMove);

			StringId64 stateId = INVALID_STRING_ID_64;
			if (params.m_isStateName)
			{
				stateId = params.m_nameId;
			}
			else
			{
				if (isLooping)
				{
					if (stayOnNav)
						stateId = (alignToAp ? SID("s_script-full-body-loop-ap-collide") : SID("s_script-full-body-loop-collide"));
					else
						stateId = (alignToAp ? SID("s_script-full-body-loop-ap") : SID("s_script-full-body-loop"));
				}
				else
				{
					if (stayOnNav)
						stateId = (alignToAp ? SID("s_script-full-body-ap-collide") : SID("s_script-full-body-collide"));
					else
						stateId = (alignToAp ? SID("s_script-full-body-ap") : SID("s_script-full-body"));
				}

				if (params.m_lookGesture != INVALID_STRING_ID_64 && stateId != INVALID_STRING_ID_64)
				{
					stateId = StringId64Concat(stateId, "-gesture");
				}
			}

			return stateId;
		}
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiFullBodyScriptController::HookEnterScriptState()
{
	// IMPORTANT: Since we're really entering a new "state" here, we MUST call
	// OnEnterScriptState() manually when the state request is fulfilled by the NPC.
	Npc& npc = GetCharacter();
	npc.RequestEnterScriptedAnimationState();
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiFullBodyScriptController::HookFadeToState(const SsAnimateParams& params,
												 StringId64 stateId,
												 bool additive,
												 BoundFrame* pBoundFrame,
												 StateChangeRequest::ID& resultChangeId)
{
	Npc& npc = GetCharacter();
	AnimControl* pAnimControl = npc.GetAnimControl();

	const bool success = AiScriptController::HookFadeToState(params, stateId, additive, pBoundFrame, resultChangeId);

	AnimSimpleLayer* pStairsLayer = pAnimControl->GetSimpleLayerById(SID("stairs"));
	if (success && pStairsLayer)
	{
		pStairsLayer->Fade(0.0f, params.m_fadeInSec);
	}

	if (success && params.m_dontExitBeforeHorse)
	{
		IAiRideHorseController* pRideHorse = npc.GetAnimationControllers()->GetRideHorseController();
		GAMEPLAY_ASSERT(pRideHorse);
		pRideHorse->RequestDontExitIgcBeforeHorse();
	}

	if (pBoundFrame)
	{
		npc.BindToRigidBody(pBoundFrame->GetBinding().GetRigidBody());
	}

	if (params.m_fadeInSec < NDI_FLT_EPSILON)
	{
		npc.ResetLegIkForTeleport();
		if (success)
		{
			// if the NPC is entering into an IGC while spawning we should make sure the NPC actually is at the start of the IGC position
			if (npc.IsNewlySpawned())
			{
				npc.ResetVelocity();

				if (params.m_collisionMode == DC::kAnimateCollisionGroundMove
					|| params.m_collisionMode == DC::kAnimateCollisionGroundMoveTurn
					|| params.m_collisionMode == DC::kAnimateCollisionSnapToGroundOnly)
				{
					npc.SetRequestSnapToGround(true);
					npc.ResetGroundFilter();
				}

				if (pBoundFrame)
				{
					const StringId64 animNameId = pAnimControl->GetAnimOverlays()->LookupTransformedAnimId(params.m_nameId);

					if (animNameId != INVALID_STRING_ID_64)
					{
						const ArtItemAnim* pArtItemAnim = npc.GetAnimControl()->LookupAnim(params.m_nameId).ToArtItem();
						if (pArtItemAnim)
						{
							Locator alignWs;
							const bool alignValid = FindAlignFromApReference(npc.GetSkeletonId(),
																			 pArtItemAnim,
																			 params.m_startPhase,
																			 pBoundFrame->GetLocator(),
																			 SID("apReference"),
																			 &alignWs,
																			 params.m_mirrored);

							if (alignValid)
							{
								npc.SetLocator(alignWs);
								if (params.m_collisionMode == DC::kAnimateCollisionGroundMove
									|| params.m_collisionMode == DC::kAnimateCollisionGroundMoveTurn
									|| params.m_collisionMode == DC::kAnimateCollisionSnapToGroundOnly)
								{
									npc.ResetGroundPositionAndFilter();
								}
							}
						}
					}
				}
			}
		}
	}

	if (NavControl* pNavControl = npc.GetNavControl())
	{
		pNavControl->ResetEverHadAValidNavMesh();
	}

	if (params.m_abandonNpcMove)
	{
		npc.AbandonInterruptedNavCommand();
	}

	m_isBusy = success;

	return success;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiFullBodyScriptController::HookExitScriptState(bool holdCharactersInScriptState)
{
	Npc& npc = GetCharacter();
	npc.SetHeldInScriptState(holdCharactersInScriptState);

	if (holdCharactersInScriptState)
	{
		return;
	}

	// IMPORTANT: Since we're really exiting a "state" here, we MUST call
	// OnExitScriptState() manually when the exit request is fulfilled by the NPC.

	SkillMgr* pSkillMgr = npc.GetSkillMgr();
	Skill* pScriptSkill = pSkillMgr->GetSkill(DC::kAiArchetypeSkillScript);
	if (pScriptSkill)
		pScriptSkill->ForceHighPriority(false);

	npc.RequestExitScriptedAnimationState();

	ForceNotAnimatingAnymore();

	m_isBusy = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiFullBodyScriptController::HookFadeOutLayer()
{
	Npc& npc = GetCharacter();

	//If we are dead dont stop our current anim, just ragdoll instead.
	if (npc.IsDead())
		return;

	const SsAnimateParams& animParams = GetParams();
	DC::AnimateExit exitMode = animParams.m_exitMode;

	AnimStateLayer* pLayer = GetStateLayer();
	const AnimStateInstance* pCurInst = pLayer ? pLayer->CurrentStateInstance() : nullptr;

	if (!IsKnownScriptedAnimationState(pCurInst ? pCurInst->GetStateName() : INVALID_STRING_ID_64))
	{
		ForceNotAnimatingAnymore();

		m_isBusy = false;

		return;
	}

	if (npc.GetScriptCommandInfo().m_commandQueued
		&& (npc.GetScriptCommandInfo().m_command.m_moveArgs.m_motionType < kMotionTypeMax))
	{
		// TODO: make this an animate arg?
		npc.SetRequestedMotionType(npc.GetScriptCommandInfo().m_command.m_moveArgs.m_motionType);
	}

	NavAnimHandoffDesc handoff;
	handoff.SetAnimStateInstance(pCurInst);
	handoff.m_steeringPhase = -1.0f;
	
	// for now don't do any handoff/steering, just blend out immediately
	// if we ever want to support procedural steering of IGCs we will need to set this
	// conditionally below (as well as set a steering phase)
	handoff.m_exitPhase = 0.0f; 

	NavControl* pNavControl = npc.GetNavControl();

	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
		pNavControl->UpdatePs(npc.GetTranslationPs(), &npc);
	}

	if (animParams.m_exitDemeanor.Valid())
	{
		const Demeanor exitDem = DemeanorFromDcDemeanor(animParams.m_exitDemeanor.Get());

		if (npc.HasDemeanor(exitDem.ToI32()))
		{
			npc.ForceDemeanor(exitDem.ToI32(), AI_LOG);
		}
	}

	bool tryIdleExit = false;
	switch (exitMode)
	{
	case DC::kAnimateExitNone:
		break;
	case DC::kAnimateExitRideHorse:
	case DC::kAnimateExitRideHorseBackseat:
		if (npc.GetAnimationControllers()->GetRideHorseController()->GetHorse() == nullptr)
		{
			bool placedOnPlayerHorse = false;
			if (npc.IsBuddyNpc())
			{
				const Player* pPlayer = ::GetPlayer();
				Horse* pPlayerHorse = pPlayer ? pPlayer->m_pRideHorseController->GetPlayerOwnedHorse() : nullptr;
				if (pPlayerHorse)
				{
					MsgConScriptError("%s has (animate-exit %s) but isn't riding a horse. putting them on the player's horse because they are a buddy\n", DevKitOnly_StringIdToString(npc.GetUserId()), DC::GetAnimateExitName(exitMode));
					npc.GetAnimationControllers()->GetRideHorseController()->MountHorse(pPlayerHorse, exitMode == DC::kAnimateExitRideHorseBackseat ? HorseDefines::kRiderBack : HorseDefines::kRiderFront, true, false);
					placedOnPlayerHorse = true;
				}
			}

			if (!placedOnPlayerHorse)
			{
				MsgConScriptError("%s has (animate-exit %s) but isn't riding a horse. This won't work properly\n", DevKitOnly_StringIdToString(npc.GetUserId()), DC::GetAnimateExitName(exitMode));
			}
		}
		break; // do nothing, assume someone else has or will change our animation state

#if ENABLE_NAV_LEDGES
	case DC::kAnimateExitEdge:
		if (const AttachSystem* pAs = npc.GetAttachSystem())
		{
			const Point rWristPosWs = pAs->GetLocatorById(SID("targRWrist")).Pos();
			const Point lWristPosWs = pAs->GetLocatorById(SID("targLWrist")).Pos();
			const Point posWs = Lerp(rWristPosWs, lWristPosWs, 0.5f);

			NavLocation newNavLoc;
			newNavLoc.SetWs(posWs);
			pNavControl->SetNavLocation(&npc, newNavLoc);
			pNavControl->SetActiveNavType(&npc, NavLocation::Type::kNavLedge);

			tryIdleExit = true;
		}
		else
		{
			tryIdleExit = true;
		}
		break;

	case DC::kAnimateExitEdgeStandingShimmy:
		pNavControl->SetActiveNavType(&npc, NavLocation::Type::kNavLedge);

		tryIdleExit = true;
		break;
#endif // ENABLE_NAV_LEDGES

	case DC::kAnimateExitCoverLowLeft:
	case DC::kAnimateExitCoverLowRight:
	case DC::kAnimateExitCoverHighLeft:
	case DC::kAnimateExitCoverHighRight:
		{
			CoverActionPack::PreferredDirection dirPref = CoverActionPack::kPreferNone;

			switch (exitMode)
			{
			case DC::kAnimateExitCoverLowLeft:
			case DC::kAnimateExitCoverHighLeft:
				dirPref = CoverActionPack::kPreferLeft;
				break;
			case DC::kAnimateExitCoverLowRight:
			case DC::kAnimateExitCoverHighRight:
				dirPref = CoverActionPack::kPreferRight;
				break;
			}

			if (!npc.TrySnapIntoClosestCoverPost(animParams.m_fadeOutSec, dirPref))
			{
				AiLogAnim(&npc, "Script exit failed to blend into AP, fading to s_idle\n");
				tryIdleExit = true;
			}
		}
		break;

	case DC::kAnimateExitCap:
		if (!npc.TrySnapIntoClosestAp(ActionPack::kCinematicActionPack, animParams.m_fadeOutSec))
		{
			tryIdleExit = true;
		}
		break;

	case DC::kAnimateExitWalkRun:
		{
			handoff.m_motionType = npc.GetRequestedMotionType();

			if (handoff.m_motionType >= kMotionTypeMax)
			{
				handoff.m_motionType = kMotionTypeWalk;
			}
		}
		break;

	case DC::kAnimateExitSwim:
	case DC::kAnimateExitSwimUnderwater:
		if (pCurInst)
		{
			handoff.m_motionType = npc.GetRequestedMotionType();
		}
		else
		{
			tryIdleExit = true;
		}
		break;

	case DC::kAnimateExitProne:
	case DC::kAnimateExitProneBack:
		if (npc.HasDemeanor(kDemeanorCrawl))
		{
			npc.ForceDemeanor(kDemeanorCrawl, AI_LOG);
		}
		tryIdleExit = true;
		break;

	case DC::kAnimateExitCrouch:
		if (npc.HasDemeanor(kDemeanorCrouch))
		{
			npc.ForceDemeanor(kDemeanorCrouch, AI_LOG);
		}
		tryIdleExit = true;
		break;

	case DC::kAnimateExitIdle:
		tryIdleExit = true;
		break;

	case DC::kAnimateExitVehicle:
		if (IAiVehicleController* pVehicleController = npc.GetAnimationControllers()->GetVehicleController())
		{
			pVehicleController->GoIdle();
		}
		break;

	case DC::kAnimateExitRagdollDeath:
		{
			const bool bCountAsPlayerKill = false;
			const bool bIgnoreInvincibility = true;

			//if (!npc.IsDead() && bCountAsPlayerKill && GetPlayer())
			//{
			//	EngineComponents::GetPlayerStatManager()->IncrementKill(*pNpc, *GetPlayer(), SID("script"), false);
			//}
			if (!bCountAsPlayerKill)
			{
				npc.m_useGroundProbeToSpawnPickup = false;
			}

			npc.DealDamage(npc.GetHealthSystem()->GetMaxHealth(), bIgnoreInvincibility);
		}
		break;

	default:
		MsgScriptErr("[%s] %s tried to use unsupported exit mode '%s' (%d)",
					 DevKitOnly_StringIdToString(animParams.m_nameId),
					 npc.GetName(),
					 DC::GetAnimateExitName(exitMode),
					 exitMode);
		tryIdleExit = true;
		break;
	}

	if (exitMode != DC::kAnimateExitRideHorse && exitMode != DC::kAnimateExitRideHorseBackseat && exitMode != DC::kAnimateExitStayInScript)
	{
		IAiRideHorseController* pRideHorse = npc.GetAnimationControllers()->GetRideHorseController();
		if (npc.GetHorse() != nullptr || (pRideHorse && pRideHorse->GetHorse() != nullptr))
		{
			pRideHorse->DismountHorse(true);
			//pRideHorse->MountHorse(nullptr, pRideHorse->GetRiderPos(), true);
		}
	}

	if (tryIdleExit && pLayer)
	{
		const bool shouldFadeOut = (nullptr == pCurInst) || ((pCurInst->GetStateFlags() & DC::kAnimStateFlagScriptState) != 0);
		INavAnimController* pNavController = npc.GetActiveNavAnimController();
		if (shouldFadeOut && pNavController)
		{
			// Instead, fade to the appropriate idle state
			DC::BlendParams blend;
			blend.m_animFadeTime   = animParams.m_fadeOutSec;
			blend.m_motionFadeTime = animParams.GetMotionFadeOutSec();
			blend.m_curve = (animParams.m_fadeOutCurve == DC::kAnimCurveTypeInvalid) ? DC::kAnimCurveTypeUniformS
																					 : animParams.m_fadeOutCurve;
			const StateChangeRequest::ID changeId = pNavController->FadeToIdleState(&blend);

			handoff.SetStateChangeRequestId(changeId, INVALID_STRING_ID_64);
			handoff.m_motionType = kMotionTypeMax;
		}
	}

	npc.ConfigureNavigationHandOff(handoff, FILE_LINE_FUNC);

	ForceNotAnimatingAnymore();

	m_isBusy = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AiFullBodyScriptController::OnAnimationDone()
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
	Npc& npc = GetCharacter();

	AiLogSkillDetails(&npc, " ScriptedAnimation Done\n");

	npc.ResetNavMesh();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiFullBodyScriptController::HookIsInScriptState() const
{
	Npc& npc = GetCharacter();
	return npc.IsInScriptedAnimationState();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AiFullBodyScriptController::IsPlaying() const
{
	// Because I never fade out my layer, define IsPlaying() like this...
	return (IsAnimating() || IsRequestPending());
}
