/*
* Copyright (c) 2015 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "game/ai/controller/push-controller.h"

#include "ndlib/anim/anim-action.h"
#include "ndlib/anim/ik/jacobian-ik.h"
#include "ndlib/anim/ik/joint-chain.h"

#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/state-script/ss-manager.h"

#include "game/ai/agent/npc.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/look-aim/npc-look-aim.h"
#include "game/player/player-options.h"
#include "game/player/player-strafe-base.h"
#include "game/scriptx/h/anim-npc-info.h"
#include "game/scriptx/h/anim-player-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AiPushController : public IAiPushController
{
	typedef IAiPushController ParentClass;

private:
	AnimAction m_pushAction;
	AnimAction m_stopAction;

	StringId64 m_grabJoint;

	bool m_pushRequested;
	bool m_stopRequested;

	Point m_lastMoveDest;
	bool m_moveToIssued;
	bool m_isBusy;

	BoundFrame m_pushApRef;
	MutableNdGameObjectHandle m_hMainPusher;
	float m_helperPercentage;

	enum State
	{
		kOff,
		//kFindPath,
		kMovingTo,
		kPushing,
		kExiting,
	};
	State m_state;
	State m_desiredState;

	struct Input
	{
		Vector pushDirection;
		float pushIntensity;
		float animSpeedScale;
	};

	static const U32F kMaxNumInputHis = 6;
	RingQueue<Input> m_inputHis;

	float m_lastPushIntensity;

	StrafeAnimIndex m_currAnimIndex;

	// TODO: should be unified with player's strafe action.
	float m_strafeMoveAngle;
	SpringTrackerAngleDegrees m_moveAngleTracker;

	//--------------------------------------------------------------------------------------//
	// hand ik. use full body ik!
	JointTree		m_ikTree;
	JacobianMap		m_jacobianArm;

public:
	AiPushController()
	{}

	void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override
	{
		ParentClass::Init(pNavChar, pNavControl);

		m_pushRequested = false;
		m_stopRequested = false;

		m_moveToIssued = false;
		m_lastMoveDest = Point(kOrigin);

		m_isBusy = false;
		m_state = kOff;
		m_desiredState = kOff;

		m_currAnimIndex.SetIdle();

		m_helperPercentage = 0.f;

		Input* buffer = NDI_NEW Input[kMaxNumInputHis];
		m_inputHis.Init(buffer, kMaxNumInputHis);

		ForceResetMoveAngle(0.f);

		// initialize full body ik.
		Character::InitializePushArmIk(pNavChar, &m_ikTree, &m_jacobianArm, SID("*npc-normal-ik*"));
	}

	virtual bool IsBusy() const override
	{
		const NavCharacter* pNavChar = GetCharacter();
		const StringId64 stateId = pNavChar->GetCurrentAnimState();

		switch (stateId.GetValue())
		{
		case SID_VAL("s_push-idle"):
		case SID_VAL("s_push-idle^move"):
		case SID_VAL("s_push-move^idle"):
		case SID_VAL("s_push-move"):
		case SID_VAL("s_push-enter"):
		case SID_VAL("s_push-exit"):
			return true;
		}

		return false;
	}

	virtual bool ShouldInterruptSkills() const override
	{
		return IsBusy() || (m_state != kOff);
	}

	virtual bool ShouldInterruptNavigation() const override
	{
		return IsBusy();
	}

	void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound) override
	{
		ParentClass::Relocate(offset_bytes, lowerBound, upperBound);
		m_inputHis.Relocate(offset_bytes, lowerBound, upperBound);

		m_ikTree.Relocate(offset_bytes, lowerBound, upperBound);
		m_jacobianArm.Relocate(offset_bytes, lowerBound, upperBound);
	}

	void RequestPush(const BoundFrame& grabAp, MutableNdGameObjectHandle mainPusher) override
	{
		m_pushRequested = true;
		m_stopRequested = false;
		m_pushApRef = grabAp;
		m_hMainPusher = mainPusher;
	}

	void StopPush() override
	{
		m_pushRequested = false;
		m_stopRequested = true;
	}

	void StartPush(const BoundFrame& grabAp)
	{
		NavCharacter* pChar = GetCharacter();

		FadeToStateParams params;
		params.m_apRef = grabAp;
		params.m_apRefValid = true;
		params.m_animFadeTime = 0.4f;
		m_pushAction.FadeToState(pChar->GetAnimControl(), SID("s_push-enter"), params, AnimAction::kFinishOnLoopingAnimEnd);

		m_inputHis.Reset();
		m_lastPushIntensity = 0.f;

		m_currAnimIndex.SetIdle();

		ForceResetMoveAngle(0.f);

		m_helperPercentage = 0.f;

		g_ssMgr.BroadcastEvent(SID("npc-start-pushing"), pChar->GetUserId());
	}

	void StartExit()
	{
		NavCharacter* pChar = GetCharacter();

		if (!pChar->IsInScriptedAnimationState())
		{
			FadeToStateParams params;
			params.m_animFadeTime = 0.4f;
			m_stopAction.FadeToState(pChar->GetAnimControl(), SID("s_push-exit"), params, AnimAction::kFinishOnAnimEnd);
		}
		else
		{
			m_stopAction.Reset();
		}
	}

	virtual void UpdateStatus() override
	{
		NavCharacter* pChar = GetCharacter();

		if (m_desiredState != m_state)
		{
			OnExit(m_state);
			OnEnter(m_desiredState);
			m_state = m_desiredState;
		}

		OnUpdate(m_state);

		m_isBusy = IsPushing() || IsExiting();

		m_pushAction.Update(pChar->GetAnimControl());
		m_stopAction.Update(pChar->GetAnimControl());
	}

	bool IsPushing() const override
	{
		if (m_pushAction.IsValid() && !m_pushAction.IsDone())
			return true;

		return false;
	}

	bool IsExiting() const
	{
		if (m_stopAction.IsValid() && !m_stopAction.IsDone())
			return true;

		return false;
	}

	void UpdateInput(Vector_arg pushDirection, float pushIntensity, float animSpeedScale) override
	{
		if (m_state == kPushing)
		{
			Input input;
			input.pushDirection = pushDirection;
			input.pushIntensity = pushIntensity;
			input.animSpeedScale = animSpeedScale;

			ALWAYS_ASSERT(Length(pushDirection) >= 0.f && Length(pushDirection) <= 1.1f);
			ALWAYS_ASSERT(IsFinite(pushIntensity));
			ASSERT(animSpeedScale > 0.f);

			if (m_inputHis.IsFull())
				m_inputHis.Dequeue();

			m_inputHis.Enqueue(input);
		}
	}

	Point GetMoveToDestWs() const
	{
		//ASSERT(m_pushRequested);
		const Point goalPosWs = m_pushApRef.GetLocator().TransformPoint(Point(-0.25f, 0.0f, -0.65f));
		return goalPosWs;
	}

	void IssueMoveCommand(NavCharacter* pNavChar, Point_arg goalPosWs)
	{
		const NavLocation goalLocation = NavUtil::ConstructNavLocation(goalPosWs, NavLocation::Type::kNavPoly, 0.0f);

		//g_prim.Draw(DebugCoordAxes(m_pushApRef.GetLocatorWs()));
		//g_prim.Draw(DebugCross(goalLocation.GetPosPs(), 0.05f, kColorRed, kPrimEnableHiddenLineAlpha));

		const Vector faceDirWs = GetLocalZ(m_pushApRef.GetRotation());
		const Vector faceDirPs = pNavChar->GetParentSpace().UntransformVector(faceDirWs);

		NavMoveArgs args;

		args.m_motionType = kMotionTypeRun;
		args.m_goalRadius = 0.5f;
		args.m_goalReachedType = kNavGoalReachedTypeContinue;
		args.m_goalFaceDirPs = faceDirPs;
		args.m_goalFaceDirValid = true;

		pNavChar->MoveTo(goalLocation, args, FILE_LINE_FUNC);
		m_lastMoveDest = goalPosWs;

		Npc* pNpc = (Npc*)pNavChar;
		pNpc->GetLookAim().DisableFaceAiming(FILE_LINE_FUNC);
		pNpc->SetNaturalFacePosition();
	}

	void OnEnter(State newState)
	{
		NavCharacter* pNavChar = GetCharacter();

		if (newState == kMovingTo)
		{
			const Point goalPosWs = GetMoveToDestWs();
			IssueMoveCommand(pNavChar, goalPosWs);
			m_moveToIssued = true;
		}
		else if (newState == kPushing)
		{
			StartPush(m_pushApRef);
		}
		else if (newState == kExiting)
		{
			StartExit();
		}
	}

	void OnExit(State oldState)
	{

	}

	void OnUpdate(State state)
	{
		NavCharacter* pNavChar = GetCharacter();

		switch (state)
		{
		case kOff:
			m_stopRequested = false;
			if (m_pushRequested)
			{
				m_desiredState = kMovingTo;
				m_pushRequested = false;
				m_moveToIssued = false;
			}
			break;

		//case kFindPath:
		//	break;

		case kMovingTo:
			if (m_moveToIssued)
			{
				if (m_stopRequested)
				{
					pNavChar->StopAndStand(-1.0f, FILE_LINE_FUNC);
				}

				if (!pNavChar->IsNavigationInProgress()
					&& (Dist(pNavChar->GetTranslation(), m_lastMoveDest) < 1.0f))
				{
					m_desiredState = kPushing;
				}
				else
				{
					const Point newGoalPosWs = GetMoveToDestWs();
					if (DistSqr(newGoalPosWs, m_lastMoveDest) > Sqr(0.1f))
					{
						IssueMoveCommand(pNavChar, newGoalPosWs);
					}
				}
			}

			if (m_stopRequested)
			{
				m_desiredState = kOff;
				m_stopRequested = false;
			}
			break;

		case kPushing:

			{
				float desiredHelperPercentage = GetDesiredHelperPercentage(pNavChar);
				if (desiredHelperPercentage > m_helperPercentage)
				{
					m_helperPercentage = desiredHelperPercentage;
					SendEvent(SID("helper-start-pushing"), m_hMainPusher, pNavChar->GetUserId(), m_helperPercentage);
				}
			}

			if (m_stopRequested)
			{
				m_desiredState = kExiting;
				m_stopRequested = false;
			}
			else
			{
				// it's to desync player and npc's push anim.
				Vector pushDirection = kZero;
				float pushIntensity = 0.f;
				float animSpeedScale = 1.f;

				if (m_inputHis.GetCount() >= kMaxNumInputHis)
				{
					const Input& oldestInput = m_inputHis.GetAt(0);
					pushDirection = oldestInput.pushDirection;
					pushIntensity = oldestInput.pushIntensity;
					animSpeedScale = oldestInput.animSpeedScale;
				}

				UpdatePushAnims(pushDirection, pushIntensity, animSpeedScale);
			}
			break;

		case kExiting:
			if (!IsExiting())
			{
				m_desiredState = kOff;
			}
			break;
		}
	}

	void UpdatePushAnims(Vector_arg pushDirection, float pushIntensity, float animSpeedScale)
	{
		const DC::PlayerStrafeSet* pStrafeSet = GetStrafeSet();
		if (!pStrafeSet)
			return;

		UpdateMoveAngle(pStrafeSet, pushDirection, pushIntensity);

		UpdateStrafeAnims(pStrafeSet, m_strafeMoveAngle, pushIntensity, animSpeedScale);
	}

	void UpdateStrafeAnims(const DC::PlayerStrafeSet* pStrafeSet, float moveAngle, float pushIntensity, float animSpeedScale)
	{
		NavCharacter* pNavChar = GetCharacter();
		AnimControl* pAnimControl = pNavChar->GetAnimControl();

		// update animation here.
		if (pushIntensity > 0.f)
		{
			const DC::PlayerStrafeAnimArray* pIdle2MoveFwdAnims = pStrafeSet->m_idleToMoveFwdAnims;
			const DC::PlayerStrafeAnimArray* pIdle2MoveBkwdAnims = pStrafeSet->m_idleToMoveBkwdAnims;

			if (pIdle2MoveFwdAnims != nullptr && pIdle2MoveBkwdAnims != nullptr && m_pushAction.CanTransitionBeTakenThisFrame(pAnimControl, SID("idle^strafe")))
			{
				const StrafeAnimIndex bestAnimResult = FindBestStrafeAnim(pIdle2MoveFwdAnims, pIdle2MoveBkwdAnims, moveAngle, m_currAnimIndex);
				const I32 moveSetIndex = bestAnimResult.GetMoveSetIndex();

				const DC::PlayerStrafeAnimArray* pAnimArray = bestAnimResult.IsBackward() ? pIdle2MoveBkwdAnims : pIdle2MoveFwdAnims;
				ASSERT(moveSetIndex >= 0 && moveSetIndex < pAnimArray->m_count);
				const DC::PlayerStrafeAnimEntry& newEntry = pAnimArray->m_array[moveSetIndex];

				// update these new move set index.
				m_currAnimIndex = bestAnimResult;

				DC::AnimNpcInfo* pInfo = pNavChar->GetAnimControl()->Info<DC::AnimNpcInfo>();
				pInfo->m_pushStrafeAnim = newEntry.m_animName;
				pInfo->m_pushStrafeSpeedScale = animSpeedScale;
				m_pushAction.Request(pAnimControl, SID("idle^strafe"), AnimAction::kFinishOnLoopingAnimEnd);
			}
			else
			{
				const DC::PlayerStrafeAnimArray* pMoveFwdAnims = pStrafeSet->m_moveFwdAnims;
				const DC::PlayerStrafeAnimArray* pMoveBkwdAnims = pStrafeSet->m_moveBkwdAnims;
				StrafeAnimIndex bestAnimResult = FindBestStrafeAnim(pMoveFwdAnims, pMoveBkwdAnims, moveAngle, m_currAnimIndex);

				const AnimStateLayer* pLayer = pAnimControl->GetStateLayerById(m_pushAction.GetLayerId());
				bool wasStrafeIdle = pLayer->CurrentStateId() == SID("s_push-idle");
				bool wasStrafe2Idle = pLayer->CurrentStateId() == SID("s_push-move^idle");
				bool wasIdle2Strafe = pLayer->CurrentStateId() == SID("s_push-idle^move");

				bool canTransitionIntoMove = (bestAnimResult != m_currAnimIndex || wasIdle2Strafe || wasStrafe2Idle) &&
					m_pushAction.CanTransitionBeTakenThisFrame(pAnimControl, SID("strafe-move"));
				// we also allow transition from idle^strafe to strafe-move if it's a different angle.
				bool canCutIntoMove = (bestAnimResult != m_currAnimIndex) && wasIdle2Strafe &&
					m_pushAction.CanTransitionBeTakenThisFrame(pAnimControl, SID("strafe^move"));

				if (canTransitionIntoMove || canCutIntoMove)
				{
					if (wasIdle2Strafe && canTransitionIntoMove)
					{
						bestAnimResult = m_currAnimIndex;
					}

					const I32 moveSetIndex = bestAnimResult.GetMoveSetIndex();

					const DC::PlayerStrafeAnimArray* pAnimArray = bestAnimResult.IsBackward() ? pMoveBkwdAnims : pMoveFwdAnims;
					ASSERT(moveSetIndex >= 0 && moveSetIndex < pAnimArray->m_count);
					const DC::PlayerStrafeAnimEntry& newEntry = pAnimArray->m_array[moveSetIndex];

					// update these new move set index.
					m_currAnimIndex = bestAnimResult;

					DC::AnimNpcInfo* pInfo = pNavChar->GetAnimControl()->Info<DC::AnimNpcInfo>();
					pInfo->m_pushStrafeAnim = newEntry.m_animName;
					pInfo->m_pushStrafeSpeedScale = animSpeedScale;

					if (canCutIntoMove)
						m_pushAction.Request(pAnimControl, SID("strafe^move"), AnimAction::kFinishOnLoopingAnimEnd);
					else
						m_pushAction.Request(pAnimControl, SID("strafe-move"), AnimAction::kFinishOnLoopingAnimEnd);
				}
			}
		}
		else if (m_currAnimIndex.IsMoving())
		{
			const DC::PlayerStrafeAnimArray* pFwd2IdleAnims = pStrafeSet->m_moveFwdToIdleAnims;
			const DC::PlayerStrafeAnimArray* pBkwd2IdleAnims = pStrafeSet->m_moveBkwdToIdleAnims;

			if (pFwd2IdleAnims != nullptr && pBkwd2IdleAnims != nullptr && m_pushAction.CanTransitionBeTakenThisFrame(pAnimControl, SID("strafe^idle")))
			{
				// go to strafe^idle
				StrafeAnimIndex animResult = FindBestStrafeAnim(pFwd2IdleAnims, pBkwd2IdleAnims, moveAngle, m_currAnimIndex);
				const I32 moveSetIndex = animResult.GetMoveSetIndex();
				const DC::PlayerStrafeAnimArray* pAnimArray = animResult.IsBackward() ? pBkwd2IdleAnims : pFwd2IdleAnims;
				ASSERT(moveSetIndex >= 0 && moveSetIndex < pAnimArray->m_count);
				const DC::PlayerStrafeAnimEntry& newEntry = pAnimArray->m_array[moveSetIndex];

				// update these new move set index.
				m_currAnimIndex = animResult;

				DC::AnimNpcInfo* pInfo = pNavChar->GetAnimControl()->Info<DC::AnimNpcInfo>();
				pInfo->m_pushStrafeAnim = newEntry.m_animName;
				pInfo->m_pushStrafeSpeedScale = animSpeedScale;
				m_pushAction.Request(pAnimControl, SID("strafe^idle"), AnimAction::kFinishOnLoopingAnimEnd);
			}
			else if (m_pushAction.CanTransitionBeTakenThisFrame(pAnimControl, SID("strafe-idle")))
			{
				// go to idle.
				m_pushAction.Request(pAnimControl, SID("strafe-idle"), AnimAction::kFinishOnLoopingAnimEnd);

				m_currAnimIndex.SetIdle();
			}
		}
	}

	void UpdateMoveAngle(const DC::PlayerStrafeSet* pStrafeSet, Vector_arg pushDirection, const float pushIntensity)
	{
		const Vector strafeDir = SafeNormalize(VectorFromXzAndY(GetLocalZ(m_pushApRef.GetRotation()), kZero), kUnitZAxis);

		const float desiredMoveAngle = MoveVel2StrafeAngle(pushDirection, strafeDir);
		ALWAYS_ASSERT(IsFinite(desiredMoveAngle));

		// reset spring if necessary.
		if (m_lastPushIntensity == 0.f && pushIntensity > 0.f)
		{
			ForceResetMoveAngle(desiredMoveAngle);
		}

		{
			const float dt = GetProcessDeltaTime();
			const float nearestAngle = Angle(m_strafeMoveAngle).Nearest(Angle(desiredMoveAngle));	// this will work when -179 to +179
			ALWAYS_ASSERT(IsFinite(nearestAngle));
			const float newAngle = m_moveAngleTracker.Track(m_strafeMoveAngle, nearestAngle, dt, pStrafeSet->m_moveAngleSpringFactor, SpringTrackerAngleDegrees::kSpeedTypeNone);
			ALWAYS_ASSERT(IsFinite(newAngle));
			m_strafeMoveAngle = newAngle;
		}

		m_lastPushIntensity = pushIntensity;
	}

	void ForceResetMoveAngle(float moveAngle)
	{
		m_strafeMoveAngle = moveAngle;
		m_moveAngleTracker.Reset();
	}

	const DC::PlayerStrafeSet* GetStrafeSet() const
	{
		const DC::PlayerStrafeSet* pStrafeSet = static_cast<const DC::PlayerStrafeSet*>(ScriptManager::LookupPointerInModule(SID("*player-push-pull-set*"), SID("anim-player"), nullptr));
		return pStrafeSet;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Push Arm Ik.
	static bool IkFilterCallback(const AnimStateInstance* pInstance)
	{
		const DC::AnimStateFlag& flags = pInstance->GetStateFlags();
		if ((flags & DC::kAnimStateFlagPushStrafeIdle) || (flags & DC::kAnimStateFlagPushStrafeMove))
			return true;
		else if (pInstance->GetStateName() == SID("s_push-enter"))
			return true;

		return false;
	}

	virtual void UpdateProcedural() override
	{
		NavCharacter* pChar = GetCharacter();

		float hand_tt = 1.f;
		if (!CanApplyHandIk(pChar, &hand_tt))
			return;

		Character::UpdatePushArmIk(pChar, m_pushApRef, &m_ikTree, &m_jacobianArm, hand_tt, IkFilterCallback, g_playerOptions.m_drawPushObjectIk);
	}

	//-----------------------------------------------------------------------------------//
	struct FindAnimHelper
	{
		StringId64 m_targetAnimState;
		bool m_found;
		float m_phase;
	};

	static bool WalkAndFindAnim(AnimStateInstance* pInstance, AnimStateLayer* pStateLayer, uintptr_t userData)
	{
		FindAnimHelper* pHelper = (FindAnimHelper*)userData;

		if (pHelper->m_found)
			return true;

		if (pInstance->GetStateName() == pHelper->m_targetAnimState)
		{
			pHelper->m_found = true;
			pHelper->m_phase = pInstance->Phase();
		}

		return true;
	}

	float GetDesiredHelperPercentage(NavCharacter* pChar) const
	{
		const AnimStateLayer *pBaseLayer = pChar->GetAnimControl()->GetBaseStateLayer();
		const AnimStateInstance* pCurInst = pBaseLayer->CurrentStateInstance();
		StringId64 animState = pCurInst->GetStateName();
		if (animState == SID("s_push-move"))
		{
			return 1.f;
		}
		else if (animState == SID("s_push-idle^move"))
		{
			return pCurInst->Phase();
		}

		return 0.f;
	}

	//-----------------------------------------------------------------------------------//
	struct FindPushAnimHelper
	{
		bool m_found;
	};

	static bool WalkAndFindPushAnim(AnimStateInstance* pInstance, AnimStateLayer* pStateLyaer, uintptr_t userData)
	{
		FindPushAnimHelper* pHelper = (FindPushAnimHelper*)userData;

		if (pHelper->m_found)
			return true;

		const DC::AnimStateFlag& flags = pInstance->GetStateFlags();
		if (flags & (DC::kAnimStateFlagPushStrafeIdle | DC::kAnimStateFlagPushStrafeMove))
		{
			pHelper->m_found = true;
		}

		return true;
	}

	bool CanApplyHandIk(NavCharacter* pChar, float* hand_tt) const
	{
		AnimStateLayer* pBaseLayer = pChar->GetAnimControl()->GetBaseStateLayer();

		FindAnimHelper helper;
		helper.m_targetAnimState = SID("s_push-enter");
		helper.m_found = false;
		helper.m_phase = 0.f;
		pBaseLayer->WalkInstancesNewToOld(WalkAndFindAnim, (uintptr_t)&helper);

		const AnimStateInstance* pCurInst = pBaseLayer->CurrentStateInstance();
		if (!pCurInst)
			return false;

		StringId64 animState = pCurInst->GetStateName();

		bool canApply = false;

		if (helper.m_found)
		{
			float t	 = LerpScaleClamp(0.f, 0.5f, 0.f, 1.f, helper.m_phase);
			*hand_tt = t;
			canApply = true;
		}
		else if (animState == SID("s_push-idle") || animState == SID("s_push-idle^move")
				 || animState == SID("s_push-move^idle") || animState == SID("s_push-move"))
		{
			*hand_tt = 1.f;
			canApply = true;
		}
		else if (animState == SID("s_push-exit"))
		{
			static const float kStopIkPhase = 0.5f;

			float phase = pBaseLayer->CurrentStateInstance()->Phase();
			if (phase <= kStopIkPhase)
			{
				*hand_tt = LerpScaleClamp(0.f, kStopIkPhase, 1.f, 0.f, phase); // should turn on when ik is fixed.
				canApply = true;
			}
		}
		else if (animState == SID("s_script-full-body-ap"))
		{
			FindPushAnimHelper anotherHelper;
			anotherHelper.m_found = false;
			pBaseLayer->WalkInstancesNewToOld(WalkAndFindPushAnim, (uintptr_t)&anotherHelper);

			if (anotherHelper.m_found)
			{
				float masterFade = pCurInst->MasterFade();
				*hand_tt		 = MinMax01(1.f - Pow(masterFade, 3.f));
				canApply = true;
			}
		}

		return canApply;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiPushController* CreateAiPushController()
{
	return NDI_NEW AiPushController;
}
