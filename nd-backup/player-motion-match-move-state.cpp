/*
 * Copyright (c) 2016 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "corelib/math/segment-util.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/nd-anim-util.h"

#include "gamelib/anim/motion-matching/motion-matching-def.h"
#include "gamelib/anim/motion-matching/motion-matching-set.h"
#include "gamelib/anim/motion-matching/pose-tracker.h"
#include "gamelib/gameplay/character-leg-ik.h"
#include "gamelib/gameplay/character-locomotion.h"
#include "gamelib/gameplay/character-motion-match-locomotion.h"
#include "gamelib/gameplay/leg-ik/ik-ground-model.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-poly-edge-gatherer.h"
#include "gamelib/gameplay/nd-subsystem-mgr.h"
#include "gamelib/scriptx/h/motion-matching-defines.h"
#include "gamelib/state-script/ss-animate.h"

#include "game/player/commonactions.h"

#include "game/camera/camera-interface.h"
#include "game/net/net-buff-manager.h"
#include "game/net/net-info.h"
#include "game/player/player-balance.h"
#include "game/player/player-collision.h"
#include "game/player/player-cover-share.h"
#include "game/player/player-cover.h"
#include "game/player/player-door.h"
#include "game/player/player-gunmove.h"
#include "game/player/player-motion-match.h"
#include "game/player/player-move.h"
#include "game/player/player-options.h"
#include "game/player/player-rope.h"
#include "game/player/player-stealth.h"
#include "game/player/player-swim.h"
#include "game/player/player-walk-besides.h"
#include "game/player/player-wall-shimmy.h"
#include "game/player/player.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class PlayerMotionMatchWrapperAction : public PlayerMoveAction
{
public:
	void CreateSubsystemIfNecessary(Player& self)
	{
		bool locomotionControllerActive = false;

		if (auto pController = self.GetSubsystemMgr()->GetActiveSubsystemController())
		{
			if (pController->GetType() == SID("CharacterMotionMatchLocomotion"))
			{
				locomotionControllerActive = true;
			}
		}

		if (g_motionMatchingOptions.m_debugInFinalPlayerState)
		{
			char debugText[1024];
			snprintf(debugText, sizeof(debugText), "PlayerMotionMatchWrapperAction::CreateSubsystemIfNecessary: locomotionControllerActive:%d\n", locomotionControllerActive);
			Character::AppendDebugTextInFinal(debugText);
		}

		if (!locomotionControllerActive)
		{
			CharacterMotionMatchLocomotion::SpawnInfo spawnInfo(SID("CharacterMotionMatchLocomotion"), &self);
			spawnInfo.m_locomotionInterfaceType = SID("PlayerLocomotionInterface");
			spawnInfo.m_initialBlendTime = m_blendInTime;
			spawnInfo.m_initialBlendCurve = m_blendInCurve;
			spawnInfo.m_initialMotionBlendTime = m_motionBlend;
			spawnInfo.m_disableBlendLimiter = m_forceBlend;

			if (!self.m_teleported)
			{
				spawnInfo.m_pHistorySeed = self.GetLocomotionHistory();
			}

			spawnInfo.m_disableExternalTransitions = true;

			if (self.IsPrevState(SID("Script")))
			{
				if (const SsAnimateController* pScriptController = self.GetPrimarySsAnimateController())
				{
					const SsAnimateParams& igcParams = pScriptController->GetParams();
					spawnInfo.m_initialBlendTime	 = igcParams.m_fadeOutSec;
					spawnInfo.m_initialBlendCurve	 = igcParams.m_fadeOutCurve;
				}
			}

			if (self.IsPrevState(SID("Door")))
			{
				spawnInfo.m_initialMotionBlendTime = 0.1f;
			}

			NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap, spawnInfo, FILE_LINE_FUNC);
		}
	}
	PlayerMotionMatchWrapperAction(Player& self,
									float blendInTime,
									float motionBlend, 
									DC::AnimCurveType blendInCurve,
									bool blendingOutOfIGC,
									bool forceBlend,
									IPlayerMotionMatchInput* pInput)
		: m_pInput(pInput), m_blendInTime(blendInTime), m_motionBlend(motionBlend), m_blendInCurve(blendInCurve), m_blendingOutOfIgc(blendingOutOfIGC), m_forceBlend(forceBlend)
	{
		CreateSubsystemIfNecessary(self);
		m_startTime = self.GetClock()->GetCurTime();
		m_startY = self.GetTranslation().Y();

		if (m_pInput)
		{
			PlayerMoveSequencer* pMoveSequencer = self.GetMoveSequencer();
			IPlayerMotionMatchInput::Input inputData = m_pInput->GetInput(self);
			pMoveSequencer->AddMessage(SID("MotionMatchSetId"), inputData.m_setId);
		}
	}

	~PlayerMotionMatchWrapperAction()
	{
		if (m_pInput)
		{
			NDI_DELETE m_pInput;
			m_pInput = nullptr;
		}
	}

	virtual const char* GetDebugString() const override	{ return "PlayerMotionMatchWrapperAction"; }

	virtual void UpdatePreAnim(Player& self) override
	{
		CreateSubsystemIfNecessary(self);

		if (m_pInput)
		{
			const Locator& parentSpace = self.GetParentSpace();
			PlayerMoveSequencer* pMoveSequencer = self.GetMoveSequencer();

			IPlayerMotionMatchInput::Input inputData = m_pInput->GetInput(self);

			pMoveSequencer->AddMessage(SID("MotionMatchDesiredDirPs"), inputData.m_velDirPs);
			if (inputData.m_desiredFacingPs.Valid())
			{
				ANIM_ASSERT(IsReasonable(inputData.m_desiredFacingPs.Get()));

				pMoveSequencer->AddMessage(SID("MotionMatchDesiredFaceDirPs"), inputData.m_desiredFacingPs.Get());
			}
			else
			{
				pMoveSequencer->RemoveMessage(SID("MotionMatchDesiredFaceDirPs"));
			}

			pMoveSequencer->RemoveMessage(SID("MotionMatchSpeedScale"));
			pMoveSequencer->AddMessage(SID("MotionMatchSetId"), inputData.m_setId);
			pMoveSequencer->AddMessage(SID("MotionMatchSpeedScale"), inputData.m_scale);

			bool refresh = false;
			if (pMoveSequencer->GetMessage(SID("MotionMatchForceRefresh"), &refresh))
			{
				pMoveSequencer->RemoveMessage(SID("MotionMatchForceRefresh"));
				pMoveSequencer->AddMessage(SID("MotionMatchForceRefreshPreAnim"), true);
			}
		}
	}

	virtual void UpdatePostAnim(Player& self) override
	{
		const float preMoveY = self.GetTranslation().Y();

		Vector delta = self.GetPlayerCollision()->GetAnimDelta();
		const Quat animRotDelta(self.GetPlayerCollision()->GetAnimRotDelta());

		ANIM_ASSERT(IsNormal(animRotDelta));

		const Locator animDesiredLocWs(self.GetTranslation() + delta,
									   IsIdentity(animRotDelta) ? self.GetRotation() : self.GetRotation()*animRotDelta);

		ASSERT(IsNormal(self.GetRotation()));

		if (!g_netInfo.IsNetActive())
		{
			if (self.IsState(SID("GunMove")) || self.IsState(SID("BowMove")))
			{
				delta = self.DoPlayerDepenetrateWeapons(delta);
				delta = self.DoPushAgainstWallAim(delta);
			}
		}

		delta = self.m_pCoverController->AdjustDeltaWs(self.GetTranslation(), delta);
		delta = self.m_pRopeCarryController->AdjustDeltaWs(self.GetTranslation(), delta);
		
		if (delta.Y() > 0.0f)
			delta.SetY(0.0f);

		self.GetPlayerCollision()->SetVelocityFromDelta(delta);

		if (!g_playerScriptOptions.m_playerEnableMmCollision)
		{
			self.GetPlayerCollision()->GroundMoveAtCurrentVelocityNoCollision(FILE_LINE_FUNC);
		}
		else
		{
			self.GetPlayerCollision()->GroundMoveAtCurrentVelocity(FILE_LINE_FUNC);
		}
		
		F32 newY = self.GetTranslation().Y();

		self.m_pWallShimmyController->OnMmHeightAdjust(newY - preMoveY);

		bool onBoat = self.GetPlatform() && self.GetPlatform()->IsKindOf(SID("DriveableBoat"));

		if (m_blendingOutOfIgc && (onBoat || self.GetPlayerCollision()->GetMovementDueToOnExternal().GetTranslation().Y() == 0.0f))
		{
			F32 timePassed = self.GetClock()->GetTimePassed(m_startTime).ToSeconds();
			F32 blendAmount = LerpScale(0.0f, m_blendInTime, 0.0f, 1.0f, timePassed);
			if (blendAmount < 1.0f)
			{
				F32 targetY = Lerp(m_startY, newY, blendAmount);
				self.SetTranslation(PointFromXzAndY(self.GetTranslation(), targetY));
			}
		}

		// IMPORTANT: Do not touch our orientation quat if we don't actually have to, in this case if
		// the anim rot delta is identity.  Doing so causes the player's quat to jitter back and forth
		// every frame, and this ends up causing in-game cinematics with tight camera angles, like the
		// journal-look, to visibly shake (believe it or not!)  Hello chaos theory!
		
		/*const Point *pGunMoveTargetPoint = self.m_pGunMoveController->GetIkTargetPointWs();
		
		if (pGunMoveTargetPoint)
		{
			self.SetRotation(QuatFromLookAt(VectorXz(*pGunMoveTargetPoint - GameCameraLocator().GetTranslation()), kUnitYAxis));
		}
		else */if (!IsIdentity(animRotDelta))
		{
			self.SetRotation(self.GetRotation() * animRotDelta);
		}

		ASSERT(IsNormal(self.GetRotation()));

		const Locator finalAlignWs = self.GetLocator();
		self.GetSubsystemMgr()->SendEvent(SID("PostAlignMovement"), &finalAlignWs, &animDesiredLocWs);

		Locator alignDelta = animDesiredLocWs.UntransformLocator(finalAlignWs);
		self.PostAlignMovement(alignDelta);
	}
	
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override
	{
		DeepRelocatePointer(m_pInput, deltaPos, lowerBound, upperBound);
		PlayerMoveAction::Relocate(deltaPos, lowerBound, upperBound);
	}
private:
	IPlayerMotionMatchInput* m_pInput = nullptr;
	float m_blendInTime = 0.0f;
	float m_motionBlend = -1.0f;
	bool m_forceBlend = false;
	DC::AnimCurveType m_blendInCurve = DC::kAnimCurveTypeUniformS;
	bool m_blendingOutOfIgc = false;
	TimeFrame m_startTime;
	F32 m_startY = 0.0f;
};

/// --------------------------------------------------------------------------------------------------------------- ///
PlayerMoveAction* CreateMotionMatchAction(Player& player,
										  float blendInTime /* = -1.0f */,
										  float motionBlendInTime,
										  DC::AnimCurveType blendInCurve /* = DC::kAnimCurveTypeInvalid */,
										  bool isBlendingOutOfIgc,
											bool forceBlendTime,
										  IPlayerMotionMatchInput* pInput /* = nullptr */)
{
	return NDI_NEW PlayerMotionMatchWrapperAction(player, blendInTime, motionBlendInTime, blendInCurve, isBlendingOutOfIgc, forceBlendTime, pInput);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Player::MotionMatchMove state
/// --------------------------------------------------------------------------------------------------------------- ///
class Player::MotionMatchMove : public Player::Active
{
public:
	BIND_TO_PROCESS(Player);

	TypedSubsystemHandle<ICharacterLocomotion> m_hController;

	void Enter() override
	{
		Player& player = Self();

		float initialBlendTime = -1.0f;
		DC::AnimCurveType initialBlendCurve = DC::kAnimCurveTypeUniformS;

		if (player.GetPrevStateId() == SID("Script"))
		{
			if (const SsAnimateController* pScriptController = player.GetPrimarySsAnimateController())
			{
				const SsAnimateParams& igcParams = pScriptController->GetParams();
				initialBlendTime = igcParams.m_fadeOutSec;
				initialBlendCurve = igcParams.m_fadeOutCurve;
			}
		}

// 		ICharacterLocomotion::SpawnInfo spawnInfo(SID("CharacterMotionMatchLocomotion"), &player);
// 		spawnInfo.m_inputFunc = PlayerLocotionInputFunc;
// 		m_hController = (ICharacterLocomotion*)NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap, spawnInfo, FILE_LINE_FUNC);
		player.m_pGroundMoveController->ClearPushedActions();
		player.m_pGroundMoveController->CreateAction();
		player.m_pGroundMoveController->PushAction(PlayerMoveSequencer::kChannelMovement,
												   NDI_NEW PlayerMotionMatchWrapperAction(player,
																						  initialBlendTime,
																						  initialBlendTime,
																						  initialBlendCurve,
																						  false, 
																						  false,
																						  nullptr));

		player.m_pLegIkController->DisableIK();

		Player::Active::Enter();
	}

	void Exit() override
	{
		Player& player = Self();



		Player::Active::Exit();
	}

	void Update() override
	{
		Player& player = Self();

		CameraSet(player.GetPlayerIndex(), kCameraStrafe);
		if (!g_playerOptions.m_useMotionMatching)
		{
			player.GoMove();
		}
		Player::Active::Update();
	}

};

STATE_REGISTER(Player, MotionMatchMove, kPriorityNormal);

/// --------------------------------------------------------------------------------------------------------------- ///
// PlayerLocomotionInterface
/// --------------------------------------------------------------------------------------------------------------- ///
TYPE_FACTORY_REGISTER(PlayerLocomotionInterface, CharacterLocomotionInterface);

/// --------------------------------------------------------------------------------------------------------------- ///
Err PlayerLocomotionInterface::Init(const SubsystemSpawnInfo& info)
{
	Err result = ParentClass::Init(info);
	if (result.Failed())
		return result;

	Player* pPlayer = GetOwnerPlayer();
	AnimStateInstance* pCurrInst = pPlayer->GetAnimControl()->GetBaseStateLayer()->CurrentStateInstance();
	RequestMaintainStartingAnim(pCurrInst);

	m_hPlayer = pPlayer;
	if (pPlayer->GetPrevStateId() != INVALID_STRING_ID_64 && pPlayer->GetPrevStateId() != SID("Script"))
	{
		m_startTime = pPlayer->GetClock()->GetCurTime();
	}
	else
	{
		m_startTime = TimeFrameNegInfinity();
	}
	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
namespace
{
	bool CanStopMaintainingAnim(const Player& rPlayer)
	{
		const AnimControl& rAnimControl = *rPlayer.GetAnimControl();
		const AnimStateLayer* pLayer = rAnimControl.GetBaseStateLayer();
		if (!pLayer)
			return true;

		const AnimStateInstance* pInstance = pLayer->CurrentStateInstance();
		if (!pInstance)
			return true;

		const ArtItemAnim* pAnim = pInstance->GetPhaseAnimArtItem().ToArtItem();
		if (!pAnim)
			return true;

		const EffectAnim* pEffectAnim = pAnim->m_pEffectAnim;
		if (!pEffectAnim)
			return true;

		const EffectAnimEntry* pEffect = EffectGroup::GetEffectById(pEffectAnim, SID("motion-matching-suppress-change-end"));
		if (!pEffect)
			return true;

		const F32 effectFrame = pEffect->GetFrame();

		const float duration = pAnim->m_pClipData->m_secondsPerFrame * pAnim->m_pClipData->m_numTotalFrames;
		const F32 effectPhase = Limit01((effectFrame / 30.0f) / duration);

		const F32 currentPhase = pInstance->GetPhase();
		return currentPhase >= effectPhase;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
SUBSYSTEM_UPDATE_DEF(PlayerLocomotionInterface, Update)
{
	Player* pPlayer = GetOwnerPlayer();

	if (pPlayer->GetPrevStateId() == SID("Bipod") || CharacterDynamicStateDataBool(pPlayer, SID("force-starting-anim")))
		m_forceStartingAnim = true;
	else
		m_forceStartingAnim = false;

	if (m_maintainStartingAnim)
	{
		using SymbolArray = DcArray<StringId64*>;

		if (const SymbolArray* pCommandList = ScriptManager::Lookup<SymbolArray>(SID("*player-loco-suppress-change-abort-commands*"), nullptr))
		{
			PlayerJoypad& joypad = pPlayer->GetJoypad();

			for (const StringId64* pCommandId : *pCommandList)
			{
				if (joypad.IsCommandActive(*pCommandId))
				{
					m_requestedAbortMaintainStartingAnim = true;
					break;
				}
			}
		}
	}

	if (m_maintainStartingAnim && CanStopMaintainingAnim(*pPlayer))
	{
		if (pPlayer->GetStateId() == SID("GunMove") || m_requestedAbortMaintainStartingAnim || pPlayer->m_pStealthController->IsProne())
			m_maintainStartingAnim = false;

		AnimStateInstance* pCurrInst = pPlayer->GetAnimControl()->GetBaseStateLayer()->CurrentStateInstance();
		if (m_startingAnimId != pCurrInst->GetPhaseAnim())
			m_maintainStartingAnim = false;

		const float stickSpeed = pPlayer->GetJoypad().GetStickSpeed(PlayerJoypad::kStickMove);
		const Vector stickWs = pPlayer->GetJoypad().GetStickWS(PlayerJoypad::kStickMove);
		const bool bIsIdleAnim = LengthSqr(m_startingAnimDirLs) < NDI_FLT_EPSILON;
		if (bIsIdleAnim)
		{
			if (stickSpeed > 0.0f)
				m_maintainStartingAnim = false;
		}
		else
		{
			if (IsNearZero(stickSpeed))
			{
				m_maintainStartingAnim = false;
			}
			else
			{
				const float angleToAbortInDeg = CharacterDynamicStateDataFloat(pPlayer, SID("motion-matching-suppress-change-abort-angle"));
				const float angleToAbort = DEGREES_TO_RADIANS(angleToAbortInDeg);

				const Vector startingAnimDirWs = SafeNormalize(VectorXz(Rotate(pPlayer->GetRotation(), m_startingAnimDirLs)), kZero);
				const Vector stickWsDir = stickSpeed > 0.0f ? stickWs / stickSpeed : kZero;
				float stickDirDot = Dot(stickWsDir, startingAnimDirWs);
				if (stickDirDot < Cos(angleToAbort))
					m_maintainStartingAnim = false;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool RecentlySwitchedFromStandToCrouch(Player& player)
{
	if (player.IsCrouched())
	{
		TimeFrame lastStandTime = player.GetLastTimeStand();
		if (player.GetClock()->GetTimePassed(lastStandTime) < Seconds(1.0f))
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlayerLocomotionInterface::GetInput(ICharacterLocomotion::InputData* pData)
{
	Player* pPlayer = GetOwnerPlayer();

	PlayerMoveSequencer* pMoveSequencer = pPlayer->GetMoveSequencer();
	const Locator& parentSpace = pPlayer->GetParentSpace();

	pData->m_desiredVelocityDirPs = kZero;
	Vector desiredVelDirPs = kZero;
	if (pMoveSequencer->GetMessage(SID("MotionMatchDesiredDirPs"), &desiredVelDirPs))
	{
		pData->m_desiredVelocityDirPs = desiredVelDirPs;
	}

	//pMoveSequencer->RemoveMessage(SID("MotionMatchDesiredDirPs"));
	pData->m_speedScale = 1.0f;
	pMoveSequencer->GetMessage(SID("MotionMatchSpeedScale"), &pData->m_speedScale);
	pMoveSequencer->RemoveMessage(SID("MotionMatchSpeedScale"));

	float slopeAngle = 0.0f;
	float slopeSpeedScale = 1.0f;

	//MsgCon("Stick Speed Scale: %.2f\n", pData->m_speedScale);
	pMoveSequencer->GetMessage(SID("MotionMatchSlopeAngle"), &slopeAngle);

	if (slopeAngle > 0.0f)
	{
		slopeSpeedScale = LerpScaleClamp(g_playerScriptOptions.m_playerSlopeSpeedAngleUpStart,
										 g_playerScriptOptions.m_playerSlopeSpeedAngleUpEnd,
										 1.0f,
										 g_playerScriptOptions.m_playerSlopeSpeedAngleUpMult,
										 slopeAngle);
	}
	else
	{
		slopeSpeedScale = LerpScaleClamp(-g_playerScriptOptions.m_playerSlopeSpeedAngleDownStart,
										 -g_playerScriptOptions.m_playerSlopeSpeedAngleDownEnd,
										 1.0f,
										 g_playerScriptOptions.m_playerSlopeSpeedAngleDownMult,
										 slopeAngle);
	}

	pData->m_speedScale *= slopeSpeedScale;

	pData->m_speedScale *= g_netBuffManager.GetBuffAmount(SID("speed"), pPlayer);

	const DC::PlayerMovementAnimSettings* pMoveModeSettings = pPlayer->m_moveMode.GetCurrentMovementSettings();
	const DC::PlayerMotionMatchingSettings* pMotionMatchingSettings = pMoveModeSettings->m_motionMatchSettings;

	if (pMotionMatchingSettings)
	{
		if (pPlayer->OnStairs())
		{
			if (!pPlayer->IsCrouched())
			{
				//pPlayer->m_pLegIkController->SetMeleeRootShiftDelta(0.f);
			}
			else
				pPlayer->m_pLegIkController->OverrideRootSmootherSpring(5.0f);

			if (pMotionMatchingSettings->m_stairBiases)
			{
				for (int i = 0; i < pMotionMatchingSettings->m_stairBiases->m_count; i++)
				{
					const DC::MotionMatchingLayerBiasDef* pBiasDef = &pMotionMatchingSettings->m_stairBiases->m_array[i];
					pData->m_mmParams.IncrementActiveLayer(pBiasDef->m_animLayer, -1.0f*pBiasDef->m_bias);
				}
			}
		}
		else if (pPlayer->OnSlope())
		{
			if (pMotionMatchingSettings->m_slopeBiases)
			{
				for (int i = 0; i < pMotionMatchingSettings->m_slopeBiases->m_count; i++)
				{
					const DC::MotionMatchingLayerBiasDef* pBiasDef = &pMotionMatchingSettings->m_slopeBiases->m_array[i];
					pData->m_mmParams.IncrementActiveLayer(pBiasDef->m_animLayer, -1.0f*pBiasDef->m_bias);
				}
			}
		}

		if (pPlayer->m_pSwimController->WadingInWaterWaistUp())
		{
			if (pMotionMatchingSettings->m_wadeBiases)
			{
				for (int i = 0; i < pMotionMatchingSettings->m_wadeBiases->m_count; i++)
				{
					const DC::MotionMatchingLayerBiasDef* pBiasDef = &pMotionMatchingSettings->m_wadeBiases->m_array[i];
					pData->m_mmParams.IncrementActiveLayer(pBiasDef->m_animLayer, -1.0f*pBiasDef->m_bias);
				}
			}
		}

		if (pMotionMatchingSettings->m_defaultBiases)
		{
			for (int i = 0; i < pMotionMatchingSettings->m_defaultBiases->m_count; i++)
			{
				const DC::MotionMatchingLayerBiasDef* pBiasDef = &pMotionMatchingSettings->m_defaultBiases->m_array[i];
				if (!pData->m_mmParams.IsLayerActive(pBiasDef->m_animLayer))
					pData->m_mmParams.IncrementActiveLayer(pBiasDef->m_animLayer, -1.0f*pBiasDef->m_bias);
			}
		}

		if (!RecentlySwitchedFromStandToCrouch(*pPlayer))
		{
			pData->m_mmParams.IncrementActiveLayer(SID("high-to-low-transitions"), -1.0f * -100.0f);
		}
	}

	if (const DC::PlayerMmEntrySettings* pEntrySettings = ScriptManager::LookupInModule<DC::PlayerMmEntrySettings>(SID("*player-mm-entry-settings*"), SID("player-settings")))
	{
		F32 timeSince = pPlayer->GetClock()->GetTimePassed(m_startTime).ToSeconds();
		F32 timeSinceChanged = GetTimeSinceTensionChanged().ToSeconds();
		F32 timeUsed = Min(timeSince, timeSinceChanged);
		F32 biasAmount = NdUtil::EvaluatePointCurve(timeUsed, pEntrySettings->m_timeToBiasCurve);
		pData->m_mmParams.IncrementActiveLayer(pEntrySettings->m_layerName, -1.0f * biasAmount);
	}

	if (FALSE_IN_FINAL_BUILD(g_playerOptions.m_debugPlayerMotionMatchingLayerBiases))
	{
		for (int i = 0; i < pData->m_mmParams.m_numActiveLayers; i++)
		{
			MsgCon("Layer Bias: %s, %.2f\n", DevKitOnly_StringIdToString(pData->m_mmParams.m_activeLayers[i]), -pData->m_mmParams.m_layerCostModifiers[i]);
		}
	}

#if 0
	const ICharacterLegIkController* pLegIk = pPlayer->GetLegIkController();
	const GroundModel* pGroundModel = pLegIk ? pLegIk->GetGroundModel() : nullptr;
	Maybe<Vector> groundNormalWs = MAYBE::kNothing;
	if (pGroundModel)
	{
		const Point playerPosWs = pPlayer->GetTranslation();
		groundNormalWs = pGroundModel->GetGroundNormalAtPos(playerPosWs);
	}

	if (!groundNormalWs.Valid())
	{
		groundNormalWs = pPlayer->GetGroundNormalWs();
	}

	pData->m_groundNormalPs = parentSpace.UntransformVector(groundNormalWs.Get());
#else
	pData->m_groundNormalPs = pPlayer->GetGroundNormalPs();
#endif

	if (m_maintainStartingAnim)
	{
		pData->m_animChangeMode = ICharacterLocomotion::AnimChangeMode::kSuppressed;
	}

	if (m_forceStartingAnim)
	{
		pData->m_animChangeMode = ICharacterLocomotion::AnimChangeMode::kForced;
	}

	if (g_motionMatchingOptions.m_debugInFinalPlayerState)
	{
		char debugText[1024];
		const I64 currFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
		snprintf(debugText, sizeof(debugText), "[FN:%lld], player m_maintainStartingAnim: %d, anim-change-mode: %d\n", currFN, m_maintainStartingAnim, pData->m_animChangeMode);
		Character::AppendDebugTextInFinal(debugText);
	}

	//ApplyObstacleAvoidance(pPlayer, pData->m_desiredVelocityDirWs);

	Vector facingVectorPs = kZero;
	if (pMoveSequencer->GetMessage(SID("MotionMatchDesiredFaceDirPs"), &facingVectorPs))
	{
		//pMoveSequencer->RemoveMessage(SID("MotionMatchDesiredFaceDirPs"));
		ANIM_ASSERT(IsReasonable(facingVectorPs));
		pData->m_desiredFacingPs = facingVectorPs;
	}
	else
	{
		pData->m_desiredFacingPs = MAYBE::kNothing;
	}

	bool mirror;
	if (pMoveSequencer->GetMessage(SID("MotionMatchMirror"), &mirror))
	{
		pData->m_mmParams.m_mirrorMode = mirror ? MMMirrorMode::kForced : MMMirrorMode::kInvalid;
	}

	if (g_playerOptions.m_disableStrafe)
	{
		pData->m_desiredFacingPs = MAYBE::kNothing;
	}

	pData->m_transitionsId = SID("*ellie-mm-transitions*");
	pData->m_playerMoveMode = pPlayer->m_moveMode.GetCurrent();

	const StringId64 mmSetName = pMoveModeSettings->m_motionMatchingSet;
	StringId64 resolvedMmSetName = pPlayer->GetAnimControl()->LookupAnimId(mmSetName);

	IScriptedMove* pScriptedMove = pPlayer->GetScriptedMoveContainer().GetMove();
	if (pPlayer->IsState(SID("ScriptMove")) || pPlayer->IsState(SID("Script")))
	{
		pScriptedMove = pPlayer->GetScriptedMoveContainer().GetMoveOrLastMove();
	}

	if (pScriptedMove)
	{
		if (pScriptedMove->GetMotionMatchingSetIdOverride())
		{
			resolvedMmSetName = pScriptedMove->GetMotionMatchingSetIdOverride();
		}
	}
	else if (pPlayer->IsState(SID("Script")) || pPlayer->IsState(SID("ScriptMove")))
	{
		if (StringId64 enterMMset = pPlayer->GetIgcEnterMMSet())
		{
			resolvedMmSetName = enterMMset;
		}
	}

	pData->m_setId = resolvedMmSetName;
	
	pMoveSequencer->GetMessage(SID("MotionMatchSetId"), &pData->m_setId);
	pMoveSequencer->RemoveMessage(SID("MotionMatchSetId"));

	bool refresh = false;
	if (pMoveSequencer->GetMessage(SID("MotionMatchForceRefreshPreAnim"), &refresh))
	{
		NdSubsystemAnimController* pController = pPlayer->GetSubsystemMgr()->GetActiveSubsystemController();
		if (pController && (pController->GetType() == SID("CharacterMotionMatchLocomotion")))
		{
			pController->RequestRefreshAnimState();
		}

		pMoveSequencer->RemoveMessage(SID("MotionMatchForceRefreshPreAnim"));
	}

	const MoveInterface* pScriptedMoveInt = pScriptedMove ? pScriptedMove->GetMoveInterface() : nullptr;
	const IPathWaypoints* pScriptedPathPs = pScriptedMoveInt ? pScriptedMoveInt->GetPathToGoalPs() : nullptr;

	Point handle;
	if (pPlayer->m_pCoverController->GetCoverMode())
	{
		const StringId64 crouchSetId = pPlayer->m_moveMode.GetMovementSettings(pPlayer->m_moveMode.GetCrouch())->m_motionMatchingSet;

		if (g_playerOptions.m_cover.m_forceCoverAsCrouch && (crouchSetId != INVALID_STRING_ID_64))
		{
			// dont look
			if (pData->m_desiredFacingPs.Valid())
			{
				const Vector intoWallWs = -pPlayer->m_pCoverController->GetWallNormal();
				const Vector intoWallPs = parentSpace.UntransformVector(intoWallWs);
				const Vector hackFacingPs = Slerp(pData->m_desiredFacingPs.Get(), intoWallPs, 0.4f);
				pData->m_desiredFacingPs = hackFacingPs;
			}

			pData->m_setId = crouchSetId;
		}

		pData->m_pStickFunc = PlayerCoverStickFunc;
	}
	else if (pPlayer->m_pDoorController->IsGestureOpeningDoor(&handle) && !g_playerOptions.m_doorProtoResidentEvilStyle)
	{
		pData->m_pStickFunc = PlayerDoorStickFunc;
	}
	else if (pPlayer->m_pWalkBesidesController->IsActive())
	{
		pData->m_pStickFunc = PlayerWalkBesidesStickFunc;
	}
	else if (pPlayer->m_pGroundMoveController->HasEdgeSlip())
	{
		pData->m_pStickFunc = PlayerEdgeSlipStickFunc;
	}
	else if (pPlayer->m_pBalanceMgr->IsInBalance())
	{
		pData->m_pStickFunc = PlayerBalanceStickFunc;
	}
	else if (pScriptedPathPs)
	{
		pData->m_pPathFunc = PlayerMotionMatchPathFunc;
	}
	else if (pScriptedMove && pScriptedMove->HasMotionModel())
	{
		pData->m_pStickFunc = PlayerScriptMoveStickFunc;
	}
	else if (pPlayer->m_pWallShimmyController->IsBusy())
	{
		pData->m_pStickFunc = PlayerWallShimmyStickFunc;
		pData->m_pLayerFunc = PlayerWallShimmyLayerFunc;
	}
	else
	{
		pData->m_pStickFunc = PlayerMoveStickFunc;
	}

	if (pPlayer->m_pWallShimmyController->IsMovingToEntry())
	{
		pData->m_pLayerFunc = PlayerWallShimmyLayerFunc;
	}
/*
	else if (pData->m_desiredFacingPs.Valid())
	{
		pData->m_pStickFunc = PlayerStrafingStickFunc;
	}
*/
	else if (Length(pData->m_desiredVelocityDirPs) > NDI_FLT_EPSILON)
	{
		const U32F playerIndex = pPlayer->GetPlayerIndex();

		if (const CameraControl* pCamera = CameraManager::GetGlobal(playerIndex).GetCurrentCamera())
		{
			pData->m_baseYawSpeed = DEGREES_TO_RADIANS(pCamera->GetYawSpeedDeg());
		}
		else
		{
			pData->m_baseYawSpeed = 0.0f;
		}
	}
	else
	{
		pData->m_baseYawSpeed = 0.0f;
	}

	if (const DC::PlayerMmCameraSettings* pCameraSettings = ScriptManager::LookupInModule<DC::PlayerMmCameraSettings>(SID("*player-mm-camera-settings*"), SID("player-settings")))
	{
		F32 speed = Abs(RADIANS_TO_DEGREES(pData->m_baseYawSpeed));
		F32 biasAmount = NdUtil::EvaluatePointCurve(speed, pCameraSettings->m_cameraSpeedToBiasCurve);

		pData->m_mmParams.AddActiveLayer(pCameraSettings->m_layerName, -1.0f * biasAmount);
	}

	pData->m_pGroupFunc = PlayerMotionMatchGroupFunc;

	if (FALSE_IN_FINAL_BUILD(g_playerOptions.m_drawPlayerMotionMatchingSet))
	{
		MsgCon("Motion Matching Set: %s\n", DevKitOnly_StringIdToString(pData->m_setId));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const IMotionPose* PlayerLocomotionInterface::GetPose(const MotionMatchingSet* pArtItemSet, bool debug)
{
	Player* pPlayer = GetOwnerPlayer();

	const IMotionPose* pPose = nullptr;

	if (const PoseTracker* pPoseTracker = pPlayer->GetPoseTracker())
	{
		pPose = &pPoseTracker->GetPose();
	}

	return pPose;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlayerLocomotionInterface::InstaceCreate(AnimStateInstance* pInst)
{
	RequestMaintainStartingAnim(pInst);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlayerLocomotionInterface::RequestMaintainStartingAnim(AnimStateInstance* pInst)
{
	bool suppressChange = CharacterDynamicStateData(pInst, SID("motion-matching-suppress-change"), false).GetAsBool();

	const ArtItemAnim* pAnim = pInst->GetPhaseAnimArtItem().ToArtItem();
	if (suppressChange && pAnim)
	{
		m_startingAnimId = pInst->GetPhaseAnim();
		m_maintainStartingAnim = true;
		m_requestedAbortMaintainStartingAnim = false;

		// Find ave velocity of the last half second of the anim to decide if it is a transition to run or idle
		float duration = GetDuration(pAnim);
		float aveSpeedDuration = Min(duration, 0.5f);
		float aveSpeedStartPhase = MinMax01(1.0f - aveSpeedDuration / duration);

		Locator alignLocStart, alignLocEnd;
		Player* pPlayer = GetOwnerPlayer();
		FindAlignFromApReference(pPlayer->GetAnimControl(), m_startingAnimId, aveSpeedStartPhase, kIdentity, &alignLocStart, pInst->IsFlipped());
		FindAlignFromApReference(pPlayer->GetAnimControl(), m_startingAnimId, 1.0f, kIdentity, &alignLocEnd, pInst->IsFlipped());
		Vector endVel = aveSpeedDuration > 0.0f ? (alignLocEnd.Pos() - alignLocStart.Pos()) / aveSpeedDuration : kZero;
		float endVelLen = Length(endVel);

		// If speed is less than 0.2 m/s assume it's an idle
		if (endVelLen < 0.2f)
			m_startingAnimDirLs = kZero;
		else
			m_startingAnimDirLs = Unrotate(alignLocEnd.Rot(), endVel / endVelLen);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionModelInput PlayerLocomotionInterface::PlayerCoverStickFunc(const Character* pChar,
																 const ICharacterLocomotion::InputData* pInput,
																 const MotionMatchingSet* pArtItemSet,
																 const DC::MotionMatchingSettings* pSettings,
																 const MotionModelIntegrator& integratorPs,
																 TimeFrame delta,
																 bool finalize,
																 bool debug)
{
	const Player* pPlayer = static_cast<const Player*>(pChar);
	const Locator& parentSpace = pChar->GetParentSpace();

	const Vector desVelDirPs = pInput ? pInput->m_desiredVelocityDirPs : kZero;

	MotionModelInput result = pPlayer->m_pCoverController->MotionMatchModelPs(parentSpace,
																			  desVelDirPs,
																			  integratorPs,
																			  delta,
																			  debug);

	result.m_facingPs = MAYBE::kNothing;
	
	if (pInput && pInput->m_desiredFacingPs.Valid())
	{
		const Vector desiredFacingPs = pInput->m_desiredFacingPs.Get();
		const Vector desiredFacingWs = Rotate(parentSpace.Rot(), desiredFacingPs);
		const Vector desiredFacingXzWs = AsUnitVectorXz(desiredFacingWs, kUnitZAxis);

		result.m_facingPs = Unrotate(parentSpace.Rot(), desiredFacingXzWs);
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionModelInput PlayerLocomotionInterface::PlayerDoorStickFunc(const Character* pChar,
																const ICharacterLocomotion::InputData* pInput,
																const MotionMatchingSet* pArtItemSet,
																const DC::MotionMatchingSettings* pSettings,
																const MotionModelIntegrator& integratorPs,
																TimeFrame delta,
																bool finalize,
																bool debug)
{
	const Player* pPlayer = static_cast<const Player*>(pChar);
	const Locator& parentSpace = pChar->GetParentSpace();

	const Vector desVelDirPs = pInput ? pInput->m_desiredVelocityDirPs : kZero;

	MotionModelInput result = pPlayer->m_pDoorController->MotionMatchModelPs(parentSpace,
																			 desVelDirPs,
																			 integratorPs,
																			 delta,
																			 debug);

	result.m_facingPs = MAYBE::kNothing;

	if (pInput && pInput->m_desiredFacingPs.Valid())
	{
		const Vector desiredFacingPs = pInput->m_desiredFacingPs.Get();
		const Vector desiredFacingWs = Rotate(parentSpace.Rot(), desiredFacingPs);
		const Vector desiredFacingXzWs = AsUnitVectorXz(desiredFacingWs, kUnitZAxis);

		result.m_facingPs = Unrotate(parentSpace.Rot(), desiredFacingXzWs);
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionModelInput PlayerLocomotionInterface::PlayerWalkBesidesStickFunc(const Character* pChar,
																	   const ICharacterLocomotion::InputData* pInput,
																	   const MotionMatchingSet* pArtItemSet,
																	   const DC::MotionMatchingSettings* pSettings,
																	   const MotionModelIntegrator& integratorPs,
																	   TimeFrame delta,
																	   bool finalize,
																	   bool debug)
{
	const Player* pPlayer = static_cast<const Player*>(pChar);
	const Locator& parentSpace = pChar->GetParentSpace();

	const Vector desVelDirPs = pInput ? pInput->m_desiredVelocityDirPs : kZero;

	MotionModelInput result = pPlayer->m_pWalkBesidesController->MotionMatchModelPs(parentSpace,
																					desVelDirPs,
																					integratorPs,
																					delta,
																					debug);

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionModelInput PlayerLocomotionInterface::PlayerScriptMoveStickFunc(const Character* pChar,
																	  const ICharacterLocomotion::InputData* pInput,
																	  const MotionMatchingSet* pArtItemSet,
																	  const DC::MotionMatchingSettings* pSettings,
																	  const MotionModelIntegrator& integratorPs,
																	  TimeFrame delta,
																	  bool finalize,
																	  bool debug)
{
	const Player* pPlayer = static_cast<const Player*>(pChar);
	const Locator& parentSpace = pChar->GetParentSpace();

	const Vector desVelDirPs = pInput ? pInput->m_desiredVelocityDirPs : kZero;

	const IScriptedMove* pScriptedMove = pPlayer->GetScriptedMoveContainer().GetMove();
	if (pPlayer->IsState(SID("ScriptMove")) || pPlayer->IsState(SID("Script")))
	{
		pScriptedMove = pPlayer->GetScriptedMoveContainer().GetMoveOrLastMove();
	}
	MotionModelInput result = pScriptedMove ? pScriptedMove->MotionMatchModelPs(pPlayer,
																				parentSpace,
																				desVelDirPs,
																				integratorPs,
																				delta,
																				debug)
											: MotionModelInput();

	result.m_facingPs = MAYBE::kNothing;

	if (pInput && pInput->m_desiredFacingPs.Valid())
	{
		const Vector desiredFacingPs = pInput->m_desiredFacingPs.Get();
		const Vector desiredFacingWs = Rotate(parentSpace.Rot(), desiredFacingPs);
		const Vector desiredFacingXzWs = AsUnitVectorXz(desiredFacingWs, kUnitZAxis);

		result.m_facingPs = Unrotate(parentSpace.Rot(), desiredFacingXzWs);
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionModelInput PlayerLocomotionInterface::PlayerBalanceStickFunc(const Character* pChar,
																   const ICharacterLocomotion::InputData* pInput,
																   const MotionMatchingSet* pArtItemSet,
																   const DC::MotionMatchingSettings* pSettings,
																   const MotionModelIntegrator& integratorPs,
																   TimeFrame delta,
																   bool finalize,
																   bool debug)
{
	const Player* pPlayer = static_cast<const Player*>(pChar);
	const Locator& parentSpace = pChar->GetParentSpace();

	const Vector desVelDirPs = pInput ? pInput->m_desiredVelocityDirPs : kZero;

	MotionModelInput result = pPlayer->m_pBalanceMgr->MotionMatchModelPs(parentSpace,
																		 desVelDirPs,
																		 integratorPs,
																		 delta,
																		 debug);

	result.m_facingPs = MAYBE::kNothing;
	
	if (pInput && pInput->m_desiredFacingPs.Valid())
	{
		const Vector desiredFacingPs = pInput->m_desiredFacingPs.Get();
		const Vector desiredFacingWs = Rotate(parentSpace.Rot(), desiredFacingPs);
		Vector desiredFacingXzWs = AsUnitVectorXz(desiredFacingWs, kUnitZAxis);

		if (pPlayer->m_pBalanceMgr->OnNarrowBalanceBeam() && g_playerOptions.m_balanceForceFaceFwd)
		{
			EdgeInfo fallEdge = pPlayer->m_pBalanceMgr->GetNearestFallEdgeInfo().m_edge;

			const Vector edgeNormal = fallEdge.GetFlattenedWallNormal();
			Vector beamMoveDir = RotateY90(edgeNormal);
			if (Dot(beamMoveDir, g_playerOptions.m_balanceForceMoveDir) < 0.0f)
				beamMoveDir = -beamMoveDir;

			desiredFacingXzWs = beamMoveDir;
		}

		result.m_facingPs = Unrotate(parentSpace.Rot(), desiredFacingXzWs);
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionModelInput PlayerLocomotionInterface::PlayerWallShimmyStickFunc(const Character* pChar,
																	  const ICharacterLocomotion::InputData* pInput,
																	  const MotionMatchingSet* pArtItemSet,
																	  const DC::MotionMatchingSettings* pSettings,
																	  const MotionModelIntegrator& integratorPs,
																	  TimeFrame delta,
																	  bool finalize,
																	  bool debug)
{
	const Player* pPlayer = static_cast<const Player*>(pChar);
	const Locator& parentSpace = pChar->GetParentSpace();
	
	const Vector desVelDirPs = pInput ? pInput->m_desiredVelocityDirPs : kZero;
	
	MotionModelInput result = pPlayer->m_pWallShimmyController->MotionMatchModelPs(parentSpace,
																desVelDirPs,
																integratorPs,
																delta,
																debug);
	
	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionModelInput PlayerLocomotionInterface::PlayerEdgeSlipStickFunc(const Character* pChar,
																	const ICharacterLocomotion::InputData* pInput,
																	const MotionMatchingSet* pArtItemSet,
																	const DC::MotionMatchingSettings* pSettings,
																	const MotionModelIntegrator& integratorPs,
																	TimeFrame delta,
																	bool finalize,
																	bool debug)
{
	const Player* pPlayer = static_cast<const Player*>(pChar);
	const Locator& parentSpace = pChar->GetParentSpace();

	const Vector desVelDirPs = pInput ? pInput->m_desiredVelocityDirPs : kZero;

	MotionModelInput result = pPlayer->m_pGroundMoveController->EdgeSlipMotion(parentSpace,
																			   desVelDirPs,
																			   integratorPs,
																			   delta,
																			   debug);

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionModelInput PlayerLocomotionInterface::PlayerMoveStickFunc(const Character* pChar,
																const ICharacterLocomotion::InputData* pInput,
																const MotionMatchingSet* pArtItemSet,
																const DC::MotionMatchingSettings* pSettings,
																const MotionModelIntegrator& integratorPs,
																TimeFrame delta,
																bool finalize,
																bool debug)
{
	const Player* pPlayer = static_cast<const Player*>(pChar);
	const Locator& parentSpace = pChar->GetParentSpace();
	Vector desVelDirPs = pInput ? pInput->m_desiredVelocityDirPs : kZero;
	Vector desFacingDirPs = pInput ? pInput->m_desiredFacingPs.Otherwise(integratorPs.Model().GetFacing()) : integratorPs.Model().GetFacing();
	
	F32 futureTime = integratorPs.Time().ToSeconds();
	F32 totalAnglechange = pInput->m_baseYawSpeed * futureTime;

	if (g_playerOptions.m_cameraSpeedAffectsMotionModel)
	{
		desVelDirPs = RotateVectorAbout(desVelDirPs, kUnitYAxis, totalAnglechange);

		if (Abs(pInput->m_baseYawSpeed) > 0.2f)
		{
			desFacingDirPs = desVelDirPs;
			if (Dot(parentSpace.TransformVector(pInput->m_desiredVelocityDirPs), GameCameraDirection()) < 0.0f)
				desFacingDirPs = -desVelDirPs;
		}
		//desFacingDirPs = RotateVectorAbout(desFacingDirPs, kUnitYAxis, totalAnglechange);
	}
	
	//const Point modelPosPs = integratorPs.Model().GetPos();
	//const Point modelPosWs = parentSpace.TransformPoint(modelPosPs);
	//const Point adjustedPosWs = pPlayer->m_pCoverShareMgr->AdjustPositionWs(posWs);

	MotionModelInput result;
	result.m_velDirPs = desVelDirPs;

	if (pSettings->m_supportsStrafe)
	{
		result.m_facingPs = desFacingDirPs;
	}
	else if (pInput)
	{
		result.m_facingPs = pInput->m_desiredFacingPs;
	}
	else
	{
		result.m_facingPs = MAYBE::kNothing;
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionModelInput PlayerLocomotionInterface::PlayerStrafingStickFunc(const Character* pChar,
																	const MotionMatchingSet* pArtItemSet,
																	const DC::MotionMatchingSettings* pSettings,
																	const MotionModelIntegrator& integratorPs,
																	TimeFrame delta,
																	bool finalize,
																	bool debug)
{
	return MotionModelInput(pChar->GetLocatorPs(), kZero, MAYBE::kNothing);
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F PlayerLocomotionInterface::PlayerMotionMatchGroupFunc(const Character* pChar,
														   const MotionMatchingSet* pArtItemSet,
														   const DC::MotionMatchingSettings* pSettings,
														   const MotionModelIntegrator& integratorPs,
														   Point_arg trajectoryEndPosOs,
														   bool debug)
{
	const Player* pPlayer = static_cast<const Player*>(pChar);

	I32F desiredGroupIndex = 0;
	
	const MotionModel& model = integratorPs.Model();
	const Vector vel = model.GetVel();
	const DC::MotionModelSettings modelSettings = integratorPs.GetSettings();
	const float maxSpeed	 = GetMaxSpeedForDirection(&modelSettings, SafeNormalize(vel, kUnitZAxis), model.GetUserFacing());

	//const bool wantToMove	 = pPlayer->GetJoypad().GetStickSpeed(NdPlayerJoypad::kStickMove) > 0.0f;
	const bool wantToMove	 = Length(model.GetDesiredVel()) > 0.2f;

	const float curSpeed	 = Length(vel);
	const float speedPercent = (maxSpeed > NDI_FLT_EPSILON) ? Limit01(curSpeed / maxSpeed) : 1.0f;

	const float sprungSpeed		= model.GetSprungSpeed();
	const float sprungSpeedPcnt = (maxSpeed > NDI_FLT_EPSILON) ? Limit01(sprungSpeed / maxSpeed) : 1.0f;

	const float speedThreshold = integratorPs.GetSettings().m_movingGroupSpeedPercentThreshold;
	const bool thresholdMet = /*(speedPercent >= speedThreshold) ||*/ (sprungSpeedPcnt >= speedThreshold);

	if (wantToMove && (speedThreshold >= 0.0f) && thresholdMet)
	{
		desiredGroupIndex = 1;
	}

	if (pPlayer->m_pCoverController->GetCoverMode())
	{
		const Locator& parentSpace = pChar->GetParentSpace();
		desiredGroupIndex = pPlayer->m_pCoverController->MotionMatchGroupFunc(parentSpace,
																			  integratorPs,
																			  desiredGroupIndex);
	}


	if (FALSE_IN_FINAL_BUILD(g_motionMatchingOptions.m_drawOptions.m_debugGroupSelection))
	{
		MsgConNonpauseable("wantToMove: %s (threshold: %0.1f%%)\n", wantToMove ? "true" : "false", speedThreshold * 100.0f);
		MsgConNonpauseable(" curSpeed: %0.2fm/s %0.1f%%\n", curSpeed, speedPercent * 100.0f);
		MsgConNonpauseable(" sprungSpeed: %0.2fm/s %0.1f%%\n", sprungSpeed, sprungSpeedPcnt * 100.0f);
		MsgConNonpauseable("  -> %d\n", desiredGroupIndex);
	}

	return desiredGroupIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float PlayerLocomotionInterface::GetStoppingFaceDist(const MotionMatchingSet* pArtItemSet)
{
	const MMSettings* pSettings = pArtItemSet ? pArtItemSet->m_pSettings : nullptr;
	const float facingStoppingDist = pSettings ? pSettings->m_goals.m_stoppingFaceDist : -1.0f;

	if (facingStoppingDist >= 0.0f)
	{
		return facingStoppingDist;
	}

	// *~* Do you believe in magic? *~*
	return 0.5f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionModelPathInput PlayerLocomotionInterface::PlayerMotionMatchPathFunc(const Character* pChar,
																		  const MotionMatchingSet* pArtItemSet,
																		  bool debug)
{
	MotionModelPathInput ret;

	const Player* pPlayer = static_cast<const Player*>(pChar);
	const PlayerMoveSequencer* pMoveSequencer = pPlayer ? pPlayer->GetMoveSequencer() : nullptr;


	const IScriptedMove* pScriptedMove = pPlayer->GetScriptedMoveContainer().GetMove();
	if (pPlayer->IsState(SID("ScriptMove")) || pPlayer->IsState(SID("Script")))
	{
		pScriptedMove = pPlayer->GetScriptedMoveContainer().GetMoveOrLastMove();
	}

	const MoveInterface* pScriptedMoveInt = pScriptedMove ? pScriptedMove->GetMoveInterface() : nullptr;
	
	if (!pScriptedMoveInt->BuildMotionModelPathInput(pPlayer, &ret))
	{
		return ret;
	}

	ret.m_stoppingFaceDist = GetStoppingFaceDist(pArtItemSet);

	if (FALSE_IN_FINAL_BUILD(debug))
	{
		ret.m_pathPs.DebugDraw(pPlayer->GetParentSpace(), false);
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlayerLocomotionInterface::PlayerWallShimmyLayerFunc(const Character* pChar, MMSearchParams& params, bool debug)
{
	const Player* pPlayer = static_cast<const Player*>(pChar);
	pPlayer->m_pWallShimmyController->AddMotionMatchingLayers(params, debug);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void PlayerLocomotionInterface::AdjustStickStep(Point& pos, Vector_arg vel, float deltaTime) const
{
	const Player* pPlayer = m_hPlayer.ToProcess();
	if (pPlayer && g_playerOptions.m_cover.m_newCoverShare)
	{
		const Locator& parentSpace = pPlayer->GetParentSpace();

		const Point posWs = parentSpace.TransformPoint(pos);
		const Point adjustedPosWs = pPlayer->m_pCoverShareMgr->AdjustPositionWs(posWs);

		float adjustedPosDistance = Length(adjustedPosWs - posWs);
		
		Point blendedPosWs = adjustedPosWs;

		Vector velWs = parentSpace.TransformVector(vel);
		Point previousPos = posWs - (velWs * deltaTime);
		Vector ds = blendedPosWs - previousPos;

		float speed = Abs(Dot(velWs, Normalize(ds)));
		
		Scalar maxDistance = Length(velWs) * deltaTime;
		float newDistance = Length(ds);

		enum eMode
		{
			Default,
			AdjustedPos,
			SmallDistance,
			ApplyMaxDistance,
			BlendedPos,
			ApplySpeed,
			Out,
			Count
		};

		eMode mode = Default;

		const float kMinAdjustedPosDistance = 0.01f;// if the adjusted distance is below this then use the adjusted position
		const float kMinDistance = 0.0001f;			// the distance we have to move is too small so just use the adjusted position
		const float kMaxDistanceThreshold = 0.01f;	// the max distance value threshold. Below this just use the adjusted position.

		if (adjustedPosDistance < kMinAdjustedPosDistance) // the adjusted position is so close, let's just use it.
		{
			blendedPosWs = adjustedPosWs;
			mode = AdjustedPos;
		}
		else if (newDistance < kMinDistance) // the blended position is so close, let's just use the adjusted position.
		{
			blendedPosWs = adjustedPosWs;
			mode = SmallDistance;
		}
		else
		{
			if(newDistance > maxDistance) // the new position is too far and it would look like the player accelerate
			{
				if (maxDistance > kMaxDistanceThreshold) // the max authorized distance to travel is big enough
				{
					blendedPosWs = previousPos + Normalize(ds) * maxDistance;
					mode = ApplyMaxDistance;
				}
				else // the max authorized distance to travel is too small so move the player using a constant speed.
				{
					const float kSpeed = 1.f;
					blendedPosWs = previousPos + Normalize(ds) * kSpeed * deltaTime;
					mode = ApplySpeed;

				}
			}
			else // the new position is not too far nor too close so leave it like this.
			{
				mode = BlendedPos;
			}
		}

		if (FALSE_IN_FINAL_BUILD(g_playerOptions.m_cover.m_debugDrawCoverShare))
		{
			Color32 color[Count] = { kColorRed, kColorOrange, kColorMagenta, kColorPink, kColorCyan, kColorCyan };
			const char* modeNames[Count] = { "DEFAULT", "ADJUSTED POS", "TOO SMALL DISTANCE", "APPLY MAX DISTANCE", "BLENDED POS", "APPLY SPEED", "OUT" };
			//MsgCon("newDistance=%f  -  maxDistance=%f  -  adustedPosDistance=%f  -  speed=%f  -  mode=%s \n", newDistance, (float)maxDistance, adjustedPosDistance, speed, modeNames[mode]);
			//g_prim.Draw(DebugSphere(closestPointFromPlayer, 0.05f, kColorBlack));
			g_prim.Draw(DebugSphere(blendedPosWs, 0.1f, color[mode]));
			//g_prim.Draw(DebugSphere(adjustedPosWs, 0.1f, kColorRed));
			//g_prim.Draw(DebugSphere(posWs, 0.05f, kColorRed));
			//g_prim.Draw(DebugSphere(previousPos, 0.05f, kColorOrange));
		}

		pos = parentSpace.UntransformPoint(blendedPosWs);

	}
	
}
