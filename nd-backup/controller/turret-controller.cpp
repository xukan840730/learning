/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/turret-controller.h"

#include "ndlib/anim/anim-action.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"

#include "game/ai/action-pack/turret-action-pack.h"
#include "game/ai/agent/npc.h"
#include "game/ai/base/ai-game-util.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/controller/weapon-controller.h"
#include "game/character-turret-control.h"
#include "game/scriptx/h/anim-npc-info.h"
#include "game/weapon/turret.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AiTurretController : public IAiTurretController
{
public:
	typedef IAiTurretController ParentClass;

	static CONST_EXPR float kStepAngleAmount = DEGREES_TO_RADIANS(60.0f);

	AnimAction						m_animAction;
	AnimAction						m_exitAction;
	ActionPackEntryDef				m_entryDef;
	BoundFrame						m_curApRef;
	bool							m_entered;
	CharacterTurretRotationControl	m_turretControl;

	Point							m_aimPosWs;
	Vector							m_aimVelWs;

	/// --------------------------------------------------------------------------------------------------------------- ///
	AiTurretController()
	: m_entered(false)
	{
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool ResolveEntry(const ActionPackResolveInput& input,
							  const ActionPack* pActionPack,
							  ActionPackEntryDef* pDefOut) const override
	{
		ASSERT(pDefOut);
		ASSERT(pActionPack->GetType() == ActionPack::kTurretActionPack);

		if (!pDefOut)
			return false;

		if (pActionPack->GetType() != ActionPack::kTurretActionPack)
			return false;

		const NavCharacter* pNavChar	= GetCharacter();
		const AnimControl* pAnimControl	= pNavChar->GetAnimControl();
		const StringId64 entryAnimId		= SID("free-turret-idle");
		const Locator apLoc				= pActionPack->GetLocatorWs();

		Locator alignLocWs;
		bool valid = FindAlignFromApReference(pAnimControl, entryAnimId, apLoc, &alignLocWs);
		if (valid)
		{
			const Locator myLocWs		= pNavChar->GetLocator();
			const Point turretPosWs		= apLoc.Pos();
			const Point animEnterPosWs	= alignLocWs.Pos();
			const TurretActionPack* pAp	= static_cast<const TurretActionPack*>(pActionPack);
			const BoundFrame& bf		= pAp->GetBoundFrame();

			pDefOut->m_apReference	= bf;
			pDefOut->m_entryAnimId	= SID("s_turret-enter-front");
			pDefOut->m_entryNavLoc	= pNavChar->AsReachableNavLocationWs(alignLocWs.Pos(), NavLocation::Type::kNavPoly);
			pDefOut->m_entryRotPs	= bf.GetParentSpace().UntransformLocator(alignLocWs).Rot();
			pDefOut->m_hResolvedForAp = pActionPack;
		}

		return valid;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool ResolveDefaultEntry(const ActionPackResolveInput& input,
									 const ActionPack* pActionPack,
									 ActionPackEntryDef* pDefOut) const override
	{
		return false;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool UpdateEntry(const ActionPackResolveInput& input,
							 const ActionPack* pActionPack,
							 ActionPackEntryDef* pDefOut) const override
	{
		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void Enter(const ActionPackResolveInput& input,
					   ActionPack* pActionPack,
					   const ActionPackEntryDef& entryDef) override
	{
		ParentClass::Enter(input, pActionPack, entryDef);

		m_entryDef = entryDef;

		Character* pChar = GetCharacter();
		AnimControl* pAnimControl = pChar->GetAnimControl();
		AnimStateLayer* pBaseLayer = pAnimControl->GetStateLayerById(SID("base"));
		const BoundFrame& apBf = pActionPack->GetBoundFrame();

		float animFadeTime = 0.3f;
		float motionFadeTime = 0.3f;

		if (entryDef.m_forceBlendTime >= 0.0f)
		{
			animFadeTime = motionFadeTime = entryDef.m_forceBlendTime;
		}

		pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

		const StringId64 animStateId = m_entryDef.m_entryAnimId == INVALID_STRING_ID_64 ? SID("s_turret-idle") : m_entryDef.m_entryAnimId;
		FadeToStateParams params;
		params.m_apRef = apBf;
		params.m_apRefValid = true;
		params.m_animFadeTime = animFadeTime;
		params.m_motionFadeTime = motionFadeTime;
		params.m_blendType = DC::kAnimCurveTypeEaseOut;

		pBaseLayer->FadeToState(animStateId, params);

		TurretActionPack* pTurretAp = (TurretActionPack*)const_cast<ActionPack*>(pActionPack);

		if (Turret* pTurret = pTurretAp->GetTurretProcess())
		{
			pTurret->SetTurretUser(pChar);
			pTurret->ResetPitchYawSpeeds();
		}

		if (Npc* pNpc = Npc::FromProcess(pChar))
		{
			pNpc->RequestGunState(kGunStateHolstered);
		}

		m_aimPosWs = apBf.GetTranslationWs() + GetLocalZ(apBf.GetRotationWs());
		m_aimVelWs = Vector(-1.0f, -1.0f, 0.0f);

		m_entered = true;
		m_turretControl.Init();
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	bool TeleportInto(ActionPack* pActionPack,
					  bool playEntireEntryAnim,
					  float fadeTime,
					  BoundFrame* pNewFrameOut,
					  uintptr_t apUserData = 0) override
	{
		if (pActionPack->GetType() != ActionPack::kTurretActionPack)
		{
			return false;
		}

		NavCharacterAdapter navChar = GetCharacterAdapter();

		ParentClass::TeleportInto(pActionPack, playEntireEntryAnim, fadeTime, pNewFrameOut, apUserData);

		ActionPackEntryDef entryDef;
		entryDef.m_forceBlendTime = 0.0f;
		ActionPackResolveInput input = MakeDefaultResolveInput(navChar);

		Enter(input, pActionPack, entryDef);

		if (pNewFrameOut)
		{
			AnimControl* pAnimControl = GetCharacter()->GetAnimControl();
			BoundFrame newFrame = pActionPack->GetBoundFrame();

			const StringId64 destAnimId = SID("free-turret-idle");

			Locator destAlignPs;
			if (FindAlignFromApReference(pAnimControl, destAnimId, newFrame.GetLocatorPs(), &destAlignPs))
			{
				newFrame.SetLocatorPs(destAlignPs);
			}

			*pNewFrameOut = newFrame;
		}

		return true;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void SetAimPositionWs(Point_arg aimPosWs, Vector_arg aimVelWs) override
	{
		m_aimPosWs = aimPosWs;
		m_aimVelWs = aimVelWs;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void UpdateStatus() override
	{
		NavCharacter* pNavChar = GetCharacter();
		AnimControl* pAnimControl = pNavChar->GetAnimControl();
		AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

		m_animAction.Update(pAnimControl);
		m_exitAction.Update(pAnimControl);

		if (m_entered && IsInTurretState())
		{
			TurretActionPack* pTurretAp = static_cast<TurretActionPack*>(m_hActionPack.ToActionPack());
			Turret* pTurret = pTurretAp ? pTurretAp->GetTurretProcess() : nullptr;

			if (pTurret)
			{
				pTurret->AimAt(m_aimPosWs, m_aimVelWs.X(), m_aimVelWs.Y());

				const StringId64 currentStateId = pBaseLayer->CurrentStateId();
				m_turretControl.Update(pNavChar,
									   GetLocalZ(pTurret->GetMuzzleLocator().GetRotation()),
									   currentStateId == SID("s_turret-idle"));
			}
		}
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool IsBusy() const override
	{
		return !m_animAction.IsDone() || !m_exitAction.IsDone();
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void Reset() override
	{
		m_animAction.Reset();
		m_exitAction.Reset();

		m_entered = false;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void DebugDrawEntries(const ActionPackResolveInput& input, const ActionPack* pActionPack) const override
	{
		STRIP_IN_FINAL_BUILD;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void DebugDrawExits(const ActionPackResolveInput& input,
								const ActionPack* pActionPack,
								const IPathWaypoints* pPathPs) const override
	{
		STRIP_IN_FINAL_BUILD;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void Reload(Turret* pTurret) override
	{
		NavCharacter* pNavChar = GetCharacter();
		AnimControl* pAnimControl = pNavChar->GetAnimControl();

		BoundFrame curApRef;
		AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
		pBaseLayer->GetApRefFromCurrentState(curApRef);

		BoundFrame reloadAp = pTurret->GetBoundFrame();
		Vector gunForward = Normalize(GetLocalZ(pTurret->GetMuzzleLocator().GetRotation())*Vector(1.0f, 0.0f, 1.0f));

		reloadAp.SetRotation(QuatFromLookAt(gunForward, kUnitYAxis));

		m_animAction.Request(pAnimControl, SID("reload"), AnimAction::kFinishOnNonTransitionalStateReached, &reloadAp);
		m_animAction.Request(pAnimControl, SID("idle"), AnimAction::kFinishOnNonTransitionalStateReached, &curApRef);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool TryAbortReload() override
	{
		NavCharacter* pNavChar = GetCharacter();
		AnimControl* pAnimControl = pNavChar->GetAnimControl();
		AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

		if (pBaseLayer->CanTransitionBeTakenThisFrame(SID("abort-reload")))
		{
			m_animAction.Request(pAnimControl, SID("abort-reload"), AnimAction::kFinishOnNonTransitionalStateReached);
			return true;
		}

		return false;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void Fire() override
	{
		NavCharacter* pNavChar = GetCharacter();
		AnimControl* pAnimControl = pNavChar->GetAnimControl();
		AnimSimpleLayer* pRecoilLayer = pAnimControl->GetSimpleLayerById(SID("weapon-additive"));
		pRecoilLayer->Fade(1.0f, 0.0f);
		pRecoilLayer->RequestFadeToAnim(SID("turret-fire"));
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	void SetStepApRef(float angle)
	{
		NavCharacter* pNavChar = GetCharacter();
		AnimControl* pAnimControl = pNavChar->GetAnimControl();
		AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
		pBaseLayer->GetApRefFromCurrentState(m_curApRef);
		m_curApRef.SetRotation(m_curApRef.GetRotation() * QuatFromAxisAngle(Vector(kUnitYAxis), angle));
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void StepLeft() override
	{
		NavCharacter* pNavChar = GetCharacter();
		AnimControl* pAnimControl = pNavChar->GetAnimControl();
		SetStepApRef(kStepAngleAmount);
		m_animAction.Request(pAnimControl, SID("turret-step-left"), AnimAction::kFinishOnNonTransitionalStateReached, &m_curApRef);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void StepRight() override
	{
		NavCharacter* pNavChar = GetCharacter();
		AnimControl* pAnimControl = pNavChar->GetAnimControl();
		SetStepApRef(-kStepAngleAmount);
		m_animAction.Request(pAnimControl, SID("turret-step-right"), AnimAction::kFinishOnNonTransitionalStateReached, &m_curApRef);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual bool IsInTurretState() const override
	{
		const NavCharacter* pNavChar = GetCharacter();
		const AnimControl* pAnimControl = pNavChar->GetAnimControl();
		const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
		const StringId64 stateId = pBaseLayer->CurrentStateId();

		switch (stateId.GetValue())
		{
		case SID_VAL("s_turret-enter-front"):
		case SID_VAL("s_turret-idle"):
		case SID_VAL("s_turret-reload"):
		case SID_VAL("s_turret-step-left"):
		case SID_VAL("s_turret-step-right"):
		case SID_VAL("s_turret-fire"):
			return true;
		}

		return false;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual void Exit(const PathWaypointsEx* pExitPathPs) override
	{
		NavCharacter* pNavChar = GetCharacter();

		m_entered = false;

		if (!pNavChar->IsDead())
		{
			ASSERT(pNavChar->GetAnimationControllers());

			if (IAiWeaponController* pWeaponCtrl = pNavChar->GetAnimationControllers()->GetWeaponController())
			{
				pWeaponCtrl->RequestGunState(kGunStateOut);
			}
		}

		if (TurretActionPack* pTurretAp = static_cast<TurretActionPack*>(m_hActionPack.ToActionPack()))
		{
			if (Turret* pTurret = pTurretAp->GetTurretProcess())
			{
				pTurret->SetTurretUser(nullptr);
			}
		}

		AnimControl* pAnimControl  = pNavChar->GetAnimControl();
		AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();

		const bool isEntering = m_animAction.IsValid() && !m_animAction.IsDone();

		if (isEntering)
		{
			pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);
		}

		const StringId64 exitTransitionId = SID("idle");

		if (pBaseLayer->IsTransitionValid(exitTransitionId))
		{
			m_exitAction.Request(pAnimControl, exitTransitionId, AnimAction::kFinishOnNonTransitionalStateReached);

			NavAnimHandoffDesc handoffDesc;
			handoffDesc.m_motionType = kMotionTypeMax;
			handoffDesc.SetStateChangeRequestId(m_exitAction.GetStateChangeRequestId(), INVALID_STRING_ID_64);
			pNavChar->ConfigureNavigationHandOff(handoffDesc, FILE_LINE_FUNC);
		}
		else
		{
			pNavChar->ForceDefaultIdleState();
		}
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiTurretController* CreateAiTurretController()
{
	return NDI_NEW AiTurretController;
}
