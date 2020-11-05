/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/ride-horse-controller.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-layer.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-snapshot-node-blend.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/feather-blend-table.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/netbridge/mail.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/settings/fader.h"
#include "ndlib/util/maybe.h"

#include "gamelib/anim/gesture-controller.h"
#include "gamelib/anim/gesture-handle.h"
#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/nd-subsystem-mgr.h"
#include "gamelib/ndphys/composite-body.h"
#include "gamelib/state-script/ss-animate.h"
#include "gamelib/state-script/ss-context.h"
#include "gamelib/state-script/ss-instance.h"

#include "game/ai/action-pack/horse-action-pack.h"
#include "game/ai/agent/npc.h"
#include "game/ai/characters/horse.h"
#include "game/ai/component/buddy-combat-logic.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/controller/horse-jump-controller.h"
#include "game/ai/controller/weapon-controller.h"
#include "game/ai/knowledge/entity.h"
#include "game/anim/ride-horse-dual-arm-ik.h"
#include "game/character/character-stirrups-ik.h"
#include "game/character/hand-to-joint-ik.h"
#include "game/dmenu.h"
#include "game/player/melee/process-characters-joint-ik.h"
#include "game/player/player-ride-horse.h"
#include "game/player/player.h"
#include "game/scriptx/h/anim-npc-info.h"
#include "game/scriptx/h/gesture-script-defines.h"
#include "game/vehicle/horse-move-controller.h"
#include "game/vehicle/horse-stirrups-controller.h"
#include "game/vehicle/horse-utility.h"

#if FINAL_BUILD
const bool g_disableHorseCommandAnims = true;
const bool g_disableHorseRiderSprintLean = false;
const bool g_disableHorseRiderTurnAnims = false;
const bool g_disableHorseStirrupIkDuringMountDismount = false;
#else
extern bool g_disableHorseCommandAnims;
extern bool g_disableHorseRiderSprintLean;
extern bool g_disableHorseRiderTurnAnims;
extern bool g_disableHorseStirrupIkDuringMountDismount;
#endif

bool FINAL_CONST g_disableHorseRiderPhaseOffset = false;
bool FINAL_CONST g_debugHorseRiderPhaseOffset = false;
bool FINAL_CONST g_useHorsePassengerRagdoll = true;
bool FINAL_CONST g_debugHorsePassengerRagdollAndIk = false;
bool FINAL_CONST g_debugHorseAimStateMachine = false;
bool FINAL_CONST g_debugHorseSlopeLean = false;
bool FINAL_CONST g_disableHorseSlopeLean = true;
bool FINAL_CONST g_allowMirrorSwitchSeatsAnim = true;
bool FINAL_CONST g_useNewRiderArmIkSystem = true;
bool FINAL_CONST g_enableDismountBlockedWarning = true;
bool FINAL_CONST g_useDualArmIk = true;
bool FINAL_CONST g_allowBackseatRagdollDuringJumps = false;

static CONST_EXPR float kTurnAnimFadeOutTime = 0.2f;
static CONST_EXPR float kFinishMountPhase = 0.26f;

//these are the list of anim states that expect us to update their AP to match the horse rider attach AP
static CONST_EXPR StringId64 kRideHorseApUpdateStates[] =
{
	SID("ride-horse-mm"),
	SID("ride-horse-no-sync"),
	SID("ride-horse-pistol-aim"),
	SID("s_ride-horse-dismount-left"),
	SID("s_ride-horse-dismount-left-f"),
	SID("s_ride-horse-dismount-right"),
	SID("s_ride-horse-dismount-right-f"),
	SID("s_ride-horse-mount-left"),
	SID("s_ride-horse-mount-left-f"),
	SID("s_ride-horse-mount-right"),
	SID("s_ride-horse-mount-right-f"),
	SID("ride-horse-backseat-aim-idle-left"),
	SID("ride-horse-backseat-aim-idle-right"),
	SID("ride-horse-backseat-aim-idle-left^left"),
	SID("ride-horse-backseat-aim-idle-right^left"),
	SID("ride-horse-backseat-aim-left"),
	SID("ride-horse-backseat-aim-left^idle-left"),
	SID("ride-horse-backseat-aim-left^aim-right"),
	SID("ride-horse-backseat-aim-idle-left^right"),
	SID("ride-horse-backseat-aim-idle-right^right"),
	SID("ride-horse-backseat-aim-right"),
	SID("ride-horse-backseat-aim-right^idle-right"),
	SID("ride-horse-backseat-aim-right^aim-left"),
};
static CONST_EXPR U32F kNumRideHorseApUpdateStates = ARRAY_COUNT(kRideHorseApUpdateStates);

static CONST_EXPR StringId64 kUseDinaApStates[] =
{
	SID("ride-horse-no-sync"),
	SID("ride-horse-backseat-aim-idle-left"),
	SID("ride-horse-backseat-aim-idle-right"),
	SID("ride-horse-backseat-aim-idle-left^left"),
	SID("ride-horse-backseat-aim-idle-right^left"),
	SID("ride-horse-backseat-aim-left"),
	SID("ride-horse-backseat-aim-left^idle-left"),
	SID("ride-horse-backseat-aim-left^aim-right"),
	SID("ride-horse-backseat-aim-idle-left^right"),
	SID("ride-horse-backseat-aim-idle-right^right"),
	SID("ride-horse-backseat-aim-right"),
	SID("ride-horse-backseat-aim-right^idle-right"),
	SID("ride-horse-backseat-aim-right^aim-left"),
};
static CONST_EXPR U32F kNumUseDinaApStates = ARRAY_COUNT(kUseDinaApStates);

static CONST_EXPR StringId64 kSwapSeatsApUpdateStates[] =
{
	SID("s_ride-horse-front^back"),
	SID("s_ride-horse-back^front"),
	SID("s_ride-horse-back^front-mirrored"),
};
static CONST_EXPR U32F kNumSwapSeatsApUpdateStates = ARRAY_COUNT(kSwapSeatsApUpdateStates);

struct MountDismountState
{
	StringId64 m_animStateId;
	float m_exitPhase;
};

static CONST_EXPR MountDismountState kMountDismountStates[] =
{
	{ SID("s_ride-horse-mount-left"),		1.0f },
	{ SID("s_ride-horse-mount-left-f"),		1.0f },
	{ SID("s_ride-horse-mount-right"),		1.0f },
	{ SID("s_ride-horse-mount-right-f"),	1.0f },
	{ SID("s_ride-horse-front^back"),		1.0f },
	{ SID("s_ride-horse-back^front"),		1.0f },
};
static CONST_EXPR U32F kNumMountDismountStates = ARRAY_COUNT(kMountDismountStates);

static CONST_EXPR StringId64 kHorseRiderSettingsModule = SID("anim-npc/anim-npc-ride-horse");

static CONST_EXPR StringId64 kDefaultIdleAnim = SID("horse-mm-ambi-idle--npc");

#ifndef FINAL_BUILD
static bool s_sentMail = false;
#endif

//static CONST_EXPR StringId64 kRiderRagdollSettings = SID("*horse-passenger-ragdoll-control-settings*");

static bool RideHorseApUpdateFilter(const AnimStateInstance* pInstance)
{
	const DC::AnimState* pAnimState = pInstance->GetState();
	if (!pAnimState)
		return false;

	StringId64 animStateId = pAnimState->m_name.m_symbol;

	for (U32F i = 0; i < kNumRideHorseApUpdateStates; ++i)
	{
		if (kRideHorseApUpdateStates[i] == animStateId)
			return true;
	}
	return false;
}

static bool SwapSeatsApUpdateFilter(const AnimStateInstance* pInstance)
{
	const DC::AnimState* pAnimState = pInstance->GetState();
	if (!pAnimState)
		return false;

	StringId64 animStateId = pAnimState->m_name.m_symbol;
	for (U32F i = 0; i < kNumSwapSeatsApUpdateStates; ++i)
	{
		if (kSwapSeatsApUpdateStates[i] == animStateId)
			return true;
	}
	return false;
}

//static bool MountDismountApUpdateFilter(const AnimStateInstance* pInstance)
//{
//	const DC::AnimState* pAnimState = pInstance->GetState();
//	if (!pAnimState)
//		return false;
//
//	StringId64 animStateId = pAnimState->m_name.m_symbol;
//
//	for (U32F i = 0; i < kNumMountDismountStates; ++i)
//	{
//		if (kMountDismountStates[i].m_animStateId == animStateId)
//		{
//			if (pInstance->Phase() < kMountDismountStates[i].m_exitPhase)
//				return true;
//		}
//	}
//	return false;
//}

static bool IsSwitchSeatsAnimState(StringId64 animName)
{
	for (U32F i = 0; i < kNumSwapSeatsApUpdateStates; ++i)
	{
		if (animName == kSwapSeatsApUpdateStates[i])
			return true;
	}
	return false;
}

static inline TimeFrame Now()
{
	return GetProcessClock()->GetCurTime();
}

static bool IsBackseatAnimInstance(const AnimStateInstance* pInstance, const DC::HorseNpcRiderSettings& riderSettings)
{
	const StringId64 animName = pInstance->GetPhaseAnim();

	for (U32F i = 0; i < riderSettings.m_backSeatAnimRemaps->m_count; ++i)
	{
		if (animName == riderSettings.m_backSeatAnimRemaps->m_array[i].m_newAnim)
			return true;
	}
	return false;
}

static bool IsUseDinaApAnim(const StringId64 animName)
{
	for (U32F i = 0; i < kNumUseDinaApStates; ++i)
	{
		if (animName == kUseDinaApStates[i])
			return true;
	}

	return false;
}

StringId64 IAiRideHorseController::GetDismountAnimForSide(HorseClearance::HorseSide side)
{
	return side == HorseClearance::kLeftSide
		? SID("s_ride-horse-dismount-left")
		: SID("s_ride-horse-dismount-right");
}

struct UpdateRideHorseApParams
{
	BoundFrame	m_frontSeat;
	BoundFrame	m_backSeat;
	float		m_apFixStrength;

	UpdateRideHorseApParams(const BoundFrame& frontSeat, const BoundFrame& backSeat, float apFixStrength)
		: m_frontSeat(frontSeat)
		, m_backSeat(backSeat)
		, m_apFixStrength(apFixStrength)
	{}

	BoundFrame GetAdjustedBackseatAp() const
	{
		const Quat fullAdjust = QuatFromXZDir(AsUnitVectorXz(m_frontSeat.GetTranslationWs() - m_backSeat.GetTranslationWs(), kUnitZAxis));
		const Quat baseRot = m_backSeat.GetRotationWs();
		const Quat resultRot = Slerp(baseRot, fullAdjust, m_apFixStrength);

		return BoundFrame(Locator(m_backSeat.GetTranslationWs(), resultRot), m_backSeat.GetBinding());
	}

	void UpdateApForInstance(AnimStateInstance* pInstance)
	{
		if (RideHorseApUpdateFilter(pInstance) || SwapSeatsApUpdateFilter(pInstance))
		{
			const DC::AnimNpcTopInfo* pTopInfo = static_cast<const DC::AnimNpcTopInfo*>(pInstance->GetAnimTopInfo());
			GAMEPLAY_ASSERT(pTopInfo);

			const bool backseatAnim = pTopInfo->m_rideHorse.m_playedFromBackseat;

			if (IsUseDinaApAnim(pInstance->GetStateName()))
			{
				ASSERT(backseatAnim);
				BoundFrame adjustedBackseatAp = GetAdjustedBackseatAp();
				pInstance->SetApLocator(adjustedBackseatAp);
			}
			else
			{
				pInstance->SetApLocator(backseatAnim ? m_backSeat : m_frontSeat);
			}
		}
	}
};

bool UpdateRideHorseApFunc(AnimStateInstance* pInstance, AnimStateLayer* pStateLayer, uintptr_t userData)
{
	UpdateRideHorseApParams* pUrhaParams = (UpdateRideHorseApParams*)userData;
	pUrhaParams->UpdateApForInstance(pInstance);

	//always continue
	return true;
}

bool FindTopSwapSeatsAnimFunc(const AnimStateInstance* pInstance, const AnimStateLayer* pStateLayer, uintptr_t userData)
{
	const AnimStateInstance** ppOutInst = (const AnimStateInstance**)userData;
	if (SwapSeatsApUpdateFilter(pInstance))
	{
		*ppOutInst = pInstance;
		return false;
	}

	return true;
}


//--------------------------------------------------------------------------------------
// HorseActionPackController
//--------------------------------------------------------------------------------------

class HorseActionPackController : public ActionPackController
{
	typedef ActionPackController ParentClass;

public:
	bool IsBusy() const override { return false; }
	void UpdateStatus() override {}

	//void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override
	//{
	//	ParentClass::Init(pNavChar, pNavControl);
	//}

	bool ResolveEntry(const ActionPackResolveInput& input,
					  const ActionPack* pActionPack,
					  ActionPackEntryDef* pDefOut) const override
	{
		GAMEPLAY_ASSERT(pDefOut);
		GAMEPLAY_ASSERT(pActionPack->GetType() == ActionPack::kHorseActionPack);

		return ResolveDefaultEntry(input, pActionPack, pDefOut);
	}

	bool ResolveDefaultEntry(const ActionPackResolveInput& input,
							 const ActionPack* pActionPack,
							 ActionPackEntryDef* pDefOut) const override
	{
		GAMEPLAY_ASSERT(pDefOut);
		GAMEPLAY_ASSERT(pActionPack->GetType() == ActionPack::kHorseActionPack);

		if (pActionPack->GetType() != ActionPack::kHorseActionPack)
		{
			return false;
		}

		const HorseActionPack* pHorseAp = static_cast<const HorseActionPack*>(pActionPack);

		ActionPackEntryDef& out = *pDefOut;

		const Point posWs = pActionPack->GetDefaultEntryPointWs(0.0f);

		out.m_entryAnimId = pHorseAp->GetEntryAnimId();
		out.m_entryNavLoc = GetCharacter()->AsReachableNavLocationWs(posWs, NavLocation::Type::kNavPoly);

		out.m_preferredMotionType = kMotionTypeRun;
		out.m_stopBeforeEntry = false;
		out.m_hResolvedForAp = ActionPackHandle(pHorseAp);
		out.m_entryRotPs = pHorseAp->GetEntryRotWs();
		out.m_alwaysStrafe = true;
		out.m_slowInsteadOfAutoStop = true;
		return true;
	}

	void Enter(const ActionPackResolveInput& input, ActionPack* pActionPack, const ActionPackEntryDef& entryDef) override
	{
		ParentClass::Enter(input, pActionPack, entryDef);

		const HorseActionPack* pHorseAp = nullptr;
		if (pActionPack->GetType() == ActionPack::kHorseActionPack)
		{
			pHorseAp = static_cast<const HorseActionPack*>(pActionPack);
		}
		GAMEPLAY_ASSERT(pHorseAp);

		EnterRightAway(false, pHorseAp);
	}

	void EnterRightAway(const bool popIn, const HorseActionPack* pHorseAp)
	{
		if (!pHorseAp)
			return;

		AiLogAnim(GetCharacter(), "Horse action pack controller: EnterRightAway(%s)\n", popIn ? "true" : "false");

		Npc* pNpc = Npc::FromProcess(GetCharacter());
		if (!pNpc)
			return;

		Horse* pHorse = Horse::FromProcess(pHorseAp->GetHorse().ToMutableProcess());
		if (!pHorse)
			return;

		const HorseDefines::RiderPosition desiredRiderPos = pHorseAp->GetRiderPos();
		Maybe<HorseDefines::RiderPosition> bestRiderPos = pHorse->GetBestRiderPos(&desiredRiderPos);
		if (!bestRiderPos.Valid())
			return;

		pHorse->SetNpcRiderImmediately(pNpc, FILE_LINE_FUNC, bestRiderPos.Get(), false, false, pHorseAp->IsOnLeftSide() ? HorseClearance::kLeftSide : HorseClearance::kRightSide);
	}

	bool UpdateEntry(const ActionPackResolveInput& input,
					 const ActionPack* pActionPack,
					 ActionPackEntryDef* pDefOut) const override
	{
		AI_ASSERT(pActionPack->GetType() == ActionPack::kHorseActionPack);
		const HorseActionPack* pHorseAp = static_cast<const HorseActionPack*>(pActionPack);
		const Point goalPtWs = pHorseAp->GetRegistrationPointWs();
		pDefOut->m_entryNavLoc = GetCharacter()->AsReachableNavLocationWs(goalPtWs, NavLocation::Type::kNavPoly);
		return true;
	}

	void DebugDrawEntries(const ActionPackResolveInput& input, const ActionPack* pActionPack) const override
	{}

	void DebugDrawExits(const ActionPackResolveInput& input,
		const ActionPack* pActionPack,
		const IPathWaypoints* pPathPs) const override
	{}
};

struct EnableHandIkRequest
{
	bool m_wantEnable;
	F32 m_fadeTimeOverride;

	EnableHandIkRequest()
		: m_wantEnable(false)
		, m_fadeTimeOverride(-1.0f)
	{}

	bool ShouldEnableHandIk() const
	{
		return m_wantEnable;
	}

	F32 GetFadeInTime() const
	{
		if (ShouldEnableHandIk())
			return m_fadeTimeOverride;
		return -1.0f;
	}

	void InitRequest()
	{
		m_wantEnable = true;
		m_fadeTimeOverride = -1.0f;
	}

	void InitRequest(F32 fadeTimeOverride)
	{
		m_wantEnable = true;
		m_fadeTimeOverride = fadeTimeOverride;
	}

	void Clear()
	{
		m_wantEnable = false;
	}
};

static bool IsMountAnimCb(const AnimStateInstance* pInstance, const AnimStateLayer* pStateLayer, uintptr_t userData);

struct SwapSeatsRequest
{
	bool m_active;
	bool m_skipAnim;
	HorseDefines::RiderPosition m_newRiderPos;

	SwapSeatsRequest()
	{
		Clear();
	}

	SwapSeatsRequest(HorseDefines::RiderPosition riderPos, bool skipAnim)
		: m_active(true)
		, m_skipAnim(skipAnim)
		, m_newRiderPos(riderPos)
	{
		GAMEPLAY_ASSERT(riderPos == HorseDefines::kRiderFront || riderPos == HorseDefines::kRiderBack);
	}

	bool IsPending() const { return m_active; }

	void Clear() { m_active = false; }
};

//--------------------------------------------------------------------------------------
// AiRideHorseController
//--------------------------------------------------------------------------------------

class AiRideHorseController : public IAiRideHorseController
{
	typedef AnimActionController ParentClass;

private:
	enum RiderState
	{
		kStateInactive,
		kStateMount,
		kStateMove,
		kStateDismount,
		kStateMelee,
		kStateExit,
	};

	HorseActionPackController* m_pActionPackController;
	HorseAimStateMachine* m_pAimStateMachine;

	RiderState m_riderState;
	MutableHorseHandle m_hHorse;
	MutableHorseHandle m_hOldHorse;
	StringId64 m_animState;
	HorseDefines::RiderPosition m_riderPosition;

	BoundFrame m_frontSeatBoundFrame;
	BoundFrame m_backSeatBoundFrame;
	BoundFrame m_mountDismountBoundFrame;

	TimeFrame m_apEnterTime;
	TimeFrame m_lastSprintTime;
	TimeFrame m_lastSlowFromGallopTime;
	SsAction m_dismountSsAction;
	AnimInstance::ID m_horseAnimIdLastFrame;
	StringId64 m_riderSettingsId;
	ScriptPointer<const DC::HorseNpcRiderSettings> m_pRiderSettings;
	I64 m_switchSeatsGameFrame;
	I64 m_mountGameFrame;
	bool m_skipUpdate : 1;
	bool m_isMoveStateChangeAllowed : 1;
	bool m_dismountPending : 1;
	bool m_dismountBlocked : 1;
	bool m_isSteeringWithTwoHands : 1;
	bool m_dontExitIgcBeforeHorse : 1;
	bool m_backseatRagdollEnabled : 1;
	bool m_backseatIkEnabled : 1;
	bool m_needRefreshBackseatIk : 1;
	bool m_swapRiderPosPending : 1;
	bool m_mirrorSwapSeatsAnim : 1;
	bool m_needSwitchSeatsApFixup : 1;

	SwapSeatsRequest m_swapSeatsRequest;
	EnableHandIkRequest m_handIkRequest;

	I64 m_blizzardSteeringRequestFrame;
	I64 m_waterBlockSteeringRequestFrame;
	I64 m_disableSystemicGesturesFrame;
	I64 m_disableCommandGesturesFrame;
	I64 m_twoHandSteeringRequestFrame;
	I64 m_oneHandSteeringRequestFrame;

	LimbLock m_limbLock;

	Gesture::CachedGestureRemap m_turnGestureRemap;
	GestureHandle m_hTurnGesture;

	F32 m_jumpRagdollDelay;

	MutableProcessHandle m_ahJointIkProcess[kArmCount];
	MutableProcessHandle m_hDualArmIkProcess;

	// -- used for RideHorseDualArmIk only
	JointTree m_followerJointTree;
	// --

public:

	static bool IsMountAnimState(StringId64 animName)
	{
		return animName == SID("s_ride-horse-mount-left") || animName == SID("s_ride-horse-mount-right")
			|| animName == SID("s_ride-horse-mount-left-f") || animName == SID("s_ride-horse-mount-right-f");
	}

	static bool IsDismountAnimState(StringId64 animName)
	{
		return animName == SID("s_ride-horse-dismount-left") || animName == SID("s_ride-horse-dismount-right")
			|| animName == SID("s_ride-horse-dismount-left-f") || animName == SID("s_ride-horse-dismount-right-f");
	}

	virtual bool IsSwitchingSeats() const override
	{
		//account for the same frame that we started switching seats -- we won't have started to play the switch seats animation yet
		if (EngineComponents::GetFrameState()->m_gameFrameNumber == m_switchSeatsGameFrame)
			return true;

		Character* pChar = GetCharacter();
		StringId64 curAnimState = pChar->GetCurrentAnimState();
		return IsSwitchSeatsAnimState(curAnimState);
	}

	const AnimStateInstance* GetTopSwitchSeatsAnimState() const
	{
		const Character* pChar = GetCharacter();
		const AnimStateLayer* pLayer = pChar->GetAnimControl()->GetBaseStateLayer();
		
		const AnimStateInstance* pResult = nullptr;
		pLayer->WalkInstancesNewToOld(FindTopSwapSeatsAnimFunc, uintptr_t(&pResult));

		return pResult;
	}

	bool IsNoInterruptAnim(StringId64 animName) const
	{
		return IsDismountAnimState(animName) || IsMountAnimState(animName) || IsSwitchSeatsAnimState(animName);
	}

	inline bool IsIdleAnim(StringId64 animName) const
	{
		return animName == SID("s_ride-horse-idle") || animName == SID("s_ride-horse-idle-f");
	}

	//prevent trying to switch to unimplemented anims
	inline bool IsAnimBlacklisted(StringId64 animName) const
	{
		return (animName == SID("s_ride-horse-idle-turn-l") || animName == SID("s_ride-horse-idle-turn-r"));
	}

	//pass in new riderPos
	inline StringId64 GetSwitchSeatsAnim(HorseDefines::RiderPosition riderPos) const
	{
		if (riderPos == HorseDefines::kRiderBack)
		{
			return SID("s_ride-horse-front^back");
		}
		else
		{
			const bool mirror = ShouldMirrorSwitchSeatsAnim();
			return mirror ? SID("s_ride-horse-back^front-mirrored") : SID("s_ride-horse-back^front");
		}
	}

	StringId64 GetMountAnimState(const Horse* pHorse, const Character* pCharacter, Maybe<HorseClearance::HorseSide> forceSide) const
	{
		bool leftSide = true;
		if (forceSide.Valid())
		{
			GAMEPLAY_ASSERT(forceSide.Get() != HorseClearance::kNoClearSide);
			leftSide = forceSide.Get() != HorseClearance::kRightSide;
		}
		else
		{
			const Vector horseToNpc = pCharacter->GetTranslation() - pHorse->GetTranslation();
			const Scalar dotLeft = Dot(horseToNpc, GetLocalX(pHorse->GetRotation()));
			leftSide = dotLeft > Scalar(0.0f);
			// by this point we've already moved into position to mount and are starting the anim. It's too late to decide we want to mount on the opposite side of the horse

			//leftSide = pHorse && pHorse->BestMountSide(pCharacter, m_riderPosition == HorseDefines::kRiderBack) == HorseClearance::kLeftSide;
		}

		StringId64 animState = SID("s_ride-horse-mount-right");

		if (leftSide)
			animState = SID("s_ride-horse-mount-left");

		if (m_riderPosition == HorseDefines::kRiderFront)
			animState = StringId64Concat(animState, "-f");
		return animState;
	}

	StringId64 GetCurrentAnimState() const
	{
		Character* pCharacter = GetCharacter();
		AnimControl* pAnimControl = pCharacter->GetAnimControl();
		ASSERT(pAnimControl);
		AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
		if (!pBaseLayer)
			return INVALID_STRING_ID_64;

		return pBaseLayer->CurrentStateId();
	}

	virtual U64 CollectHitReactionStateFlags() const override
	{
		U64 flags = 0;

		flags |= DC::kHitReactionStateMaskVehicleTypeHorse;

		if (m_riderPosition == HorseDefines::kRiderBack)
			flags |= DC::kHitReactionStateMaskVehicleRolePassengerBack;
		else if (m_riderPosition == HorseDefines::kRiderFront)
			flags |= DC::kHitReactionStateMaskVehicleRoleDriver;

		return flags;
	}

	virtual void Shutdown() override
	{
		ParentClass::Shutdown();
		for (int iArm = 0; iArm < kArmCount; ++iArm)
		{
			if (m_ahJointIkProcess[iArm].HandleValid())
				KillProcess(m_ahJointIkProcess[iArm]);
		}

		if (m_hDualArmIkProcess.HandleValid())
			KillProcess(m_hDualArmIkProcess);
	}

	//assumes m_hHorse is valid
	void UnhookOldHorse()
	{
		if (Horse* pOldHorse = m_hHorse.ToMutableProcess())
		{
			pOldHorse->SetNpcRider(nullptr, FILE_LINE_FUNC, m_riderPosition);
			m_hHorse = nullptr;
		}
	}

	bool IsHorseDebugFlying() const
	{
		STRIP_IN_SUBMISSION_BUILD_VALUE(false);

		const Horse* pHorse = m_hHorse.ToProcess();
		if (!pHorse)
			return false;

		const Player* pPlayerRider = pHorse->GetPlayerRider();
		if (!pPlayerRider)
			return false;

		return pPlayerRider->IsDebugFly();
	}

	bool IsAnyMountAnimPlaying() const
	{
		if (EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused <= m_mountGameFrame)
			return true;

		const AnimStateLayer* pBaseLayer = GetCharacter()->GetAnimControl()->GetBaseStateLayer();

		bool result = false;
		pBaseLayer->WalkInstancesNewToOld(IsMountAnimCb, reinterpret_cast<uintptr_t>(&result));

		return result;
	}

	bool IsPlayingScriptedAnimation() const
	{
		const NavCharacter* pCharacter = GetCharacter();
		if (pCharacter->IsInScriptedAnimationState())
			return true;

		const AnimationControllers* pAnimControllers = pCharacter->GetAnimationControllers();
		for (U32F i = kBeginNpcScriptControllers; i < kEndNpcScriptControllers; ++i)
		{
			const AiScriptController* pScriptController = pAnimControllers->GetScriptController(i);
			if (pScriptController && pScriptController->HookIsInScriptState())
				return true;
		}
		return false;
	}

	bool DoesGestureAnimFeatherBlendWantIk(const AnimStateInstance* pAnim) const
	{
		if (!pAnim)
			return false;

		const AnimSnapshotNodeBlend* pRootSnapshotNode = AnimSnapshotNodeBlend::FromAnimNode(pAnim->GetRootSnapshotNode());
		if (!pRootSnapshotNode)
			return false;

		const I32 featherBlendIdx = pRootSnapshotNode->m_featherBlendIndex;
		const FeatherBlendTable::Entry* pFeatherBlend = g_featherBlendTable.GetEntry(featherBlendIdx);
		if (!pFeatherBlend)
			return false;
		
		const StringId64 featherBlendId = pFeatherBlend->m_featherBlendId;
		if (featherBlendId == SID("*conversation-head-only*"))
			return true;

		return false;
	}

	bool ShouldGestureDisableHandIk() const
	{
		// so... turns out the gesture controller can say it isn't playing a gesture when we do have gestures active
		// this definitely happens for conversation gestures, maybe for other gestures too?

		NavCharacter* pChar = GetCharacter();

		const AnimControl* pAc = pChar->GetAnimControl();
		const AnimStateLayer* pGesture1 = pAc->GetStateLayerById(SID("gesture-1"));
		const AnimStateLayer* pGesture2 = pAc->GetStateLayerById(SID("gesture-2"));
		const IGestureController* pGestureController = pChar->GetGestureController();

		if (pGesture1 && pGesture1->GetNumTotalInstances() > 0)
		{
			// hand fix IK also means we want to leave rider hand IK enabled
			if (pGestureController->GetHandFixIkMaskForLayer(pGestureController->GetFirstRegularGestureLayerIndex()) != 0x0)
				return false;

			const AnimStateInstance* pAnim = pGesture1->CurrentStateInstance();
			if (DoesGestureAnimFeatherBlendWantIk(pAnim))
				return false;

			return true;
		}

		if (pGesture2 && pGesture2->GetNumTotalInstances() > 0)
		{
			// hand fix IK also means we want to leave rider hand IK enabled
			if (pGestureController->GetHandFixIkMaskForLayer(pGestureController->GetFirstRegularGestureLayerIndex() + 1) != 0x0)
				return false;

			const AnimStateInstance* pAnim = pGesture2->CurrentStateInstance();
			if (DoesGestureAnimFeatherBlendWantIk(pAnim))
				return false;

			return true;
		}

		return false;
	}

	// meant to be used inside ShouldRideAsRagdoll and ShouldEnableHandIk, not to be used seperately
	bool CheckShouldEnableRagdollOrHandIk(bool debug = false, bool skipScriptCheck = false) const
	{
		const Horse* pHorse = GetHorse();
		const bool systemEnabled = g_useHorsePassengerRagdoll;
		const bool riderPositionValid = m_riderPosition == HorseDefines::kRiderBack;
		const bool horseValid = pHorse != nullptr;
		const bool notMounting = !IsAnyMountAnimPlaying();
		const bool notDismounting = !IsDismounting();
		const bool notSwitchingSeats = !IsSwitchingSeats();
		const bool notDebugFlying = !IsHorseDebugFlying();
		const bool notInScriptedAnimState = skipScriptCheck || !IsPlayingScriptedAnimation();
		const bool settingsValid = GetBackseatRagdollSettings() != nullptr;
		const bool notInFadeToBlack = GetFadeToBlack() <= 0.1f;
		const bool notSkipUpdate = !m_skipUpdate;


		const bool result = systemEnabled
							&& riderPositionValid
							&& horseValid
							&& notMounting
							&& notDismounting
							&& notSwitchingSeats
							&& notDebugFlying
							&& notInScriptedAnimState
							&& settingsValid
							&& notInFadeToBlack
							&& notSkipUpdate;

		if (FALSE_IN_FINAL_BUILD(g_debugHorsePassengerRagdollAndIk && debug && DebugSelection::Get().IsProcessOrNoneSelected(GetCharacter())))
		{
			MsgConPauseable("Rider settings: %s\n", DevKitOnly_StringIdToString(m_riderSettingsId));
			MsgConPauseable(PRETTY_PRINT_BOOL(systemEnabled));
			MsgConPauseable(PRETTY_PRINT_BOOL(riderPositionValid));
			MsgConPauseable(PRETTY_PRINT_BOOL(horseValid));
			MsgConPauseable(PRETTY_PRINT_BOOL(notMounting));
			MsgConPauseable(PRETTY_PRINT_BOOL(notDismounting));
			MsgConPauseable(PRETTY_PRINT_BOOL(notSwitchingSeats));
			MsgConPauseable(PRETTY_PRINT_BOOL(notDebugFlying));
			if (skipScriptCheck)
			{
				MsgConPauseable(PRETTY_PRINT_BOOL(skipScriptCheck));
			}
			else
			{
				MsgConPauseable(PRETTY_PRINT_BOOL(notInScriptedAnimState));
			}
			MsgConPauseable(PRETTY_PRINT_BOOL(settingsValid));
			MsgConPauseable(PRETTY_PRINT_BOOL(notInFadeToBlack));
			MsgConPauseable(PRETTY_PRINT_BOOL(notSkipUpdate));
			MsgConPauseable(PRETTY_PRINT_BOOL(result));
		}

		return result;
	}

	bool ShouldEnableHandIk(bool debug = false, bool skipScriptCheck = false) const
	{
		const DC::HorseRiderRagdollSettings* pRagdollSettings = GetBackseatRagdollSettings();
		if (!pRagdollSettings || (!pRagdollSettings->m_leftHandTarget && !pRagdollSettings->m_rightHandTarget))
			return false;

		const bool baseResult = CheckShouldEnableRagdollOrHandIk(debug, skipScriptCheck);
		const bool notInGesture = skipScriptCheck || !ShouldGestureDisableHandIk();

		if (FALSE_IN_FINAL_BUILD(g_debugHorsePassengerRagdollAndIk && DebugSelection::Get().IsProcessOrNoneSelected(GetCharacter())))
		{
			if (skipScriptCheck)
			{
				MsgConPauseable(PRETTY_PRINT_BOOL(skipScriptCheck));
			}
			else
			{
				MsgConPauseable(PRETTY_PRINT_BOOL(notInGesture));
			}
		}

		return baseResult && notInGesture;
	}

	void UpdateRagdollAllowedDueToJump()
	{
		const Horse* pHorse = GetHorse();
		const bool notInJump = pHorse ? !pHorse->GetJumpController()->IsMidair() && !pHorse->GetJumpController()->IsJumpStarting() : true;
		if (!notInJump)
		{
			m_jumpRagdollDelay = 0.5f;
		}
		else if (m_jumpRagdollDelay > 0.0f)
		{
			m_jumpRagdollDelay -= GetProcessDeltaTime();
		}
	}

	bool ShouldRideAsRagdoll(bool debug = false, bool skipScriptCheck = false) const
	{
		const bool baseResult = CheckShouldEnableRagdollOrHandIk(debug, skipScriptCheck);

		const bool result = baseResult && (g_allowBackseatRagdollDuringJumps || m_jumpRagdollDelay <= 0.0f);

		if (FALSE_IN_FINAL_BUILD(g_debugHorsePassengerRagdollAndIk && debug && DebugSelection::Get().IsProcessOrNoneSelected(GetCharacter())))
		{
			if (!g_allowBackseatRagdollDuringJumps)
			{
				MsgConPauseable("Ragdoll jump delay: %.2f\n", m_jumpRagdollDelay);
			}
			MsgConPauseable("[%s] Horse rider ragdoll enabled?: %s%s%s\n",
							DevKitOnly_StringIdToString(GetCharacter()->GetUserId()),
							GetTextColorString(result ? kTextColorGreen : kTextColorRed),
							result ? "YES" : "NO",
							GetTextColorString(kTextColorNormal));
		}

		return result;
	}

	bool IsAnimWhitelistedForRagdoll(const StringId64 animNameId) const
	{
		if (animNameId == kDefaultIdleAnim)
			return true;

		if (IsRemappedBackseatAnim(animNameId))
			return true;

		return false;
	}

	StringId64 GetPendingRiderAnim() const
	{
		const NavCharacter* pNpc = GetCharacter();
		const AnimControl* pAnimControl = pNpc->GetAnimControl();
		const DC::AnimNpcTopInfo* const pTopInfo = static_cast<const DC::AnimNpcTopInfo*>(pAnimControl->GetTopInfo()); //wtf
		return pTopInfo->m_rideHorse.m_animMm;
	}

	bool IsPendingAnimWhitelistedForRagdoll() const
	{
		const StringId64 animId = GetPendingRiderAnim();
		return IsAnimWhitelistedForRagdoll(animId);
	}

	const DC::HorseRiderRagdollSettings* GetBackseatRagdollSettings() const
	{
		const HorseAimStateMachine* pHASM = GetAimStateMachine();
		const bool shouldUseWeaponVersion = pHASM && pHASM->IsActive();
		const DC::HorseNpcRiderSettings& riderSettings = GetRiderSettings();
		const DC::HorseRiderRagdollSettings* pRagdollSettings = riderSettings.m_backseatRagdollSettings;
		if (shouldUseWeaponVersion && riderSettings.m_weaponOutBackseatRagdollSettings)
			pRagdollSettings = riderSettings.m_weaponOutBackseatRagdollSettings;
		return pRagdollSettings;
	}

	virtual void EnableBackseatHandIk(float blendTimeOverride = -1.0f) override
	{
		// need to request this to be enabled in PostRenderUpdate rather than immediately
		m_handIkRequest.InitRequest(blendTimeOverride);
	}

	virtual void DisableBackseatHandIk(float blendTimeOverride = -1.0f) override
	{
		// it should be safe to disable (but not enable) backseat hand IK at any point in the frame
		const DC::HorseRiderRagdollSettings* pSettings = GetRiderSettings().m_backseatRagdollSettings;
		if (pSettings)
			DisableBackseatHandIkInternal(pSettings, blendTimeOverride);
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual float GetStirrupsRiderModeBlend(bool& outDoSwitchSeatsBlend) const override
	{
		outDoSwitchSeatsBlend = false;
		if (IsDismounting())
		{
			const AnimStateInstance* pAnimState = GetCharacter()->GetAnimControl()->GetBaseStateLayer()->CurrentStateInstance();
			if (!IsDismountAnimState(pAnimState->GetStateName()))
				return 1.0f;

			const float currentPhase = pAnimState->GetPhase();
			const float result = MinMax01(Lerp(1.0f, 0.0f, currentPhase));
			return result;
		}
		else if (IsMounting())
		{
			const AnimStateInstance* pAnimState = GetCharacter()->GetAnimControl()->GetBaseStateLayer()->CurrentStateInstance();
			if (!IsMountAnimState(pAnimState->GetStateName()))
				return 0.0f;

			const DC::MountAnimFadeSettings* pFadeSettings = GetMountFadeSettingsForAnim(pAnimState->GetStateName());
			const DC::PointCurve* pCurve = pFadeSettings ? pFadeSettings->m_stirrupsRiderModeCurve : nullptr;

			const float currentPhase = pAnimState->GetPhase();
			if (pCurve)
			{
				const float result = pCurve ? NdUtil::EvaluatePointCurve(currentPhase, pCurve) : currentPhase;
				return result;
			}
			else
			{
				const float endPhase = pFadeSettings ? pFadeSettings->m_endPhase : 0.7f;
				const float result = LerpScaleClamp(0.0f, endPhase, 0.0f, 1.0f, currentPhase);
				return result;
			}
		}
		else
		{
			const AnimStateInstance* pSwapSeatsInst = GetTopSwitchSeatsAnimState();
			if (pSwapSeatsInst)
			{
				outDoSwitchSeatsBlend = GetRiderPos() == HorseDefines::kRiderFront;
				const float currentPhase = pSwapSeatsInst->GetPhase();

				const DC::HorseNpcRiderSettings& riderSettings = GetRiderSettings();
				const DC::PointCurve* pCurve = GetRiderPos() == HorseDefines::kRiderFront ? riderSettings.m_swapToFrontStirrupsIkCurve : riderSettings.m_swapToBackStirrupsIkCurve;

				const float curveResult = pCurve ? NdUtil::EvaluatePointCurve(currentPhase, pCurve) : 0.0f;

				const float newValue = GetRiderPos() == HorseDefines::kRiderFront ? 1.0f : 0.0f;
				const float oldValue = GetRiderPos() == HorseDefines::kRiderFront ? 0.0f : 1.0f;
				const float endPhase = riderSettings.m_swapSeatsEndPhase;
				const float minResult = LerpScaleClamp(0.0f, endPhase, oldValue, newValue, currentPhase);

#if HTOWNSEND
				MsgConPauseable("  Switch seats phase: %.2f\n  curveResult: %.2f\n  minResult: %.2f\n", currentPhase, curveResult, minResult);
#endif
				float result = curveResult;
				if (GetRiderPos() == HorseDefines::kRiderBack)
					result = Max(minResult, curveResult);

				return result;
			}
			else if (IsSwitchingSeats()) // to catch the first frame before the anim has started
			{
				outDoSwitchSeatsBlend = GetRiderPos() == HorseDefines::kRiderFront;
				return GetRiderPos() == HorseDefines::kRiderFront ? 1.0f : 0.0f;
			}
			else
			{
				return 1.0f;
			}
		}
	}
	
	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual JointTree& GetJointTree() override
	{
		return m_followerJointTree;
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	virtual const JointTree& GetJointTree() const override
	{
		return m_followerJointTree;
	}

	StringId64 GetRiderToHorseHandIkSubsystemId() const
	{
		if (g_useDualArmIk)
			return SID("RideHorseDualArmIk");
		else if (g_useNewRiderArmIkSystem)
			return SID("RiderHandToJointIk");
		else
			return SID("HandToJointIk");
	}

	void RequestHandIkRefresh()
	{
		m_needRefreshBackseatIk = true;
	}

	bool IsLeftHandIkEnabled() const
	{
		if (!m_backseatIkEnabled)
			return false;

		GAMEPLAY_ASSERT(g_useDualArmIk); // no time to make this work for non dual arm ik, we probably wont ship it anyway

		const RideHorseDualArmIk* pDualArmIk = RideHorseDualArmIk::FromProcess(m_hDualArmIkProcess.ToProcess());
		if (!pDualArmIk)
			return false;

		return pDualArmIk->IsEnabled(kLeftArm);
	}

	bool IsRightHandIkEnabled() const
	{
		if (!m_backseatIkEnabled)
			return false;

		GAMEPLAY_ASSERT(g_useDualArmIk); // no time to make this work for non dual arm ik, we probably wont ship it anyway

		const RideHorseDualArmIk* pDualArmIk = RideHorseDualArmIk::FromProcess(m_hDualArmIkProcess.ToProcess());
		if (!pDualArmIk)
			return false;

		return pDualArmIk->IsEnabled(kRightArm);
	}

	bool AreIkTargetsFlipped() const
	{
		if (!m_backseatIkEnabled)
			return false;
		
		GAMEPLAY_ASSERT(g_useDualArmIk); // no time to make this work for non dual arm ik, we probably wont ship it anyway

		const RideHorseDualArmIk* pDualArmIk = RideHorseDualArmIk::FromProcess(m_hDualArmIkProcess.ToProcess());
		if (!pDualArmIk)
			return false;

		return pDualArmIk->AreTargetsFlipped();
	}

	void EnableBackseatHandIkInternal(const DC::HorseRiderRagdollSettings* pSettings, float blendTimeOverride, bool wantLeftHand, bool wantRightHand, bool flipTargets)
	{
		GAMEPLAY_ASSERT(pSettings);

		GAMEPLAY_ASSERT(wantLeftHand || wantRightHand);

		if (IsLeftHandIkEnabled() == wantLeftHand && IsRightHandIkEnabled() == wantRightHand && blendTimeOverride < 0.0f && flipTargets == AreIkTargetsFlipped())
			return;

		m_handIkRequest.Clear();

		Character* pCharacter = GetCharacter();
		Horse* pHorse = GetHorse();
		Character* pFrontSeatRider = pHorse->GetCharacterRider(HorseDefines::kRiderFront);
		if (pFrontSeatRider == pCharacter)
		{
			// if this somehow happens in the shipping game, just drop the request
			return;
		}

		AnimStateInstance* pCurrentAnimState = pCharacter->GetAnimControl()->GetBaseStateLayer()->CurrentStateInstance();
		DC::AnimNpcTopInfo* pCurrentAnimTopInfo = static_cast<DC::AnimNpcTopInfo*>(const_cast<DC::AnimTopInfo*>(pCurrentAnimState->GetAnimTopInfo()));
		GAMEPLAY_ASSERT(pCurrentAnimTopInfo);
		pCurrentAnimTopInfo->m_rideHorse.m_flip = false; // can't have flipped anims with hand ik


		const DC::RiderHandIkTarget* pIkTargets[kArmCount] = { pSettings->m_leftHandTarget, pSettings->m_rightHandTarget };
		if (g_useDualArmIk)
		{
			RideHorseDualArmIk* pDualArmIk = nullptr;
			if (m_hDualArmIkProcess.HandleValid())
			{
				pDualArmIk = RideHorseDualArmIk::FromProcess(m_hDualArmIkProcess.ToMutableProcess());
				GAMEPLAY_ASSERT(pDualArmIk);
			}

			RideHorseDualArmIkSpawnInfo dualIkInfo;
			dualIkInfo.m_hFollower = pCharacter;
			dualIkInfo.m_flipTargets = flipTargets;
			bool spawnSubsystem = false;
			for (U8 iArm = 0; iArm < kArmCount; ++iArm)
			{
				const DC::RiderHandIkTarget* pIkTarget = pIkTargets[iArm];
				if (!pIkTarget)
					continue;

				bool blendOut = false;
				if (iArm == kLeftArm && !wantLeftHand)
				{
					blendOut = true;
				}
				else if (iArm == kRightArm && !wantRightHand)
				{
					blendOut = true;
				}

				dualIkInfo.m_hLeader[iArm] = (pIkTarget->m_targetType == DC::kRiderHandIkTargetTypeHorse ? pHorse : pFrontSeatRider);
				dualIkInfo.m_leaderJointName[iArm] = pIkTarget->m_targetJoint;
				dualIkInfo.m_blendInTime[iArm] = blendTimeOverride >= 0.0f ? blendTimeOverride : pIkTarget->m_blendInTime;
				dualIkInfo.m_blendOutTime[iArm] = pIkTarget->m_blendOutTime;
				dualIkInfo.m_enable[iArm] = !blendOut;
				if (pDualArmIk)
				{
					GAMEPLAY_ASSERT(pDualArmIk->GetLeader((ArmIndex)iArm) == dualIkInfo.m_hLeader[iArm]);
					if (blendOut)
					{
						pDualArmIk->BlendOut((ArmIndex)iArm, dualIkInfo.m_blendOutTime[iArm]);
					}
					else
					{
						pDualArmIk->BlendIn((ArmIndex)iArm, dualIkInfo.m_blendInTime[iArm]);
					}
					pDualArmIk->SetTargetsFlipped(flipTargets);
				}
				else
				{
					spawnSubsystem = true;
				}
			}

			if (spawnSubsystem)
			{
				SpawnInfo spawnInfo(SID("RideHorseDualArmIk"));
				spawnInfo.m_pUserData = &dualIkInfo;
				spawnInfo.m_pParent = pCharacter;
				GAMEPLAY_ASSERT(!m_hDualArmIkProcess.HandleValid());
				m_hDualArmIkProcess = NewProcess(spawnInfo);
			}

			m_backseatIkEnabled = m_hDualArmIkProcess.HandleValid();
		}
		else
		{
			for (int iArm = 0; iArm < kArmCount; ++iArm)
			{
				const DC::RiderHandIkTarget* pIkTarget = pIkTargets[iArm];
				if (!pIkTarget)
					continue;

				if (pIkTarget->m_targetType == DC::kRiderHandIkTargetTypeHorse)
				{
					m_backseatIkEnabled = true;
					SubsystemSpawnInfo info(GetRiderToHorseHandIkSubsystemId(), pCharacter);
					HandToJointIkInfo contactInfo;
					contactInfo.m_pTargetObject = pHorse;
					contactInfo.m_armIndex = iArm;
					contactInfo.m_contactJoint = pIkTarget->m_targetJoint;
					contactInfo.m_blendTime = pIkTarget->m_blendInTime;
					info.m_pUserData = &contactInfo;
					NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap, info, FILE_LINE_FUNC);
				}
				else
				{
					m_backseatIkEnabled = true;
					GAMEPLAY_ASSERT(!m_ahJointIkProcess[iArm].Valid());

					CharactersJointIkSpawnInfo jointInfo(pCharacter, pFrontSeatRider, (ArmIndex)iArm, pIkTarget->m_targetJoint, false, pIkTarget->m_blendInTime, true, true);
					SpawnInfo spawnInfo(SID("ProcessCharactersJointIk"));
					spawnInfo.m_pUserData = &jointInfo;
					spawnInfo.m_pParent = pCharacter;
					m_ahJointIkProcess[iArm] = NewProcess(spawnInfo);
				}
			}
		}
	}

	void DisableBackseatHandIkInternal(const DC::HorseRiderRagdollSettings* pSettings, float blendTimeOverride = -1.0f)
	{
		GAMEPLAY_ASSERT(pSettings);

		m_handIkRequest.Clear();

		m_backseatIkEnabled = false;

		Character* pCharacter = GetCharacter();

		float aBlendTimes[kArmCount] =
		{
			blendTimeOverride >= 0.0f ? blendTimeOverride : pSettings->m_leftHandTarget->m_blendOutTime,
			blendTimeOverride >= 0.0f ? blendTimeOverride : pSettings->m_rightHandTarget->m_blendOutTime,
		};

		int index = 0;
		while (HandToJointIkInterface *pContactIk = (HandToJointIkInterface*)pCharacter->GetSubsystemMgr()->FindSubsystem(GetRiderToHorseHandIkSubsystemId(), index++))
		{
			const float blendTime = aBlendTimes[pContactIk->GetArmIndex()];
			pContactIk->BlendOut(blendTime);
		}

		for (int iArm = 0; iArm < kArmCount; ++iArm)
		{
			if (m_ahJointIkProcess[iArm].Valid())
				SendEvent(SID("stop-sync"), m_ahJointIkProcess[iArm], aBlendTimes[iArm]);
			
			if (m_hDualArmIkProcess.Valid())
			{
				RideHorseDualArmIk* pDualArmIk = RideHorseDualArmIk::FromProcess(m_hDualArmIkProcess.ToMutableProcess());
				GAMEPLAY_ASSERT(pDualArmIk);
				pDualArmIk->BlendOut((ArmIndex)iArm, aBlendTimes[iArm]);
			}

		}
		//if (m_hDualArmIkProcess.HandleValid())
		//	SendEvent(SID("stop-sync"), m_hDualArmIkProcess, aBlendTimes[kLeftArm], aBlendTimes[kRightArm]);
	}

	virtual void EnableRagdoll(float blendTimeOverride = -1.0f) override
	{
		const DC::HorseRiderRagdollSettings* pRiderRagdollSettings = GetBackseatRagdollSettings();
		GAMEPLAY_ASSERT(pRiderRagdollSettings);

		const StringId64 ragdollSettingsId = pRiderRagdollSettings->m_ragdollSettingsId;

		//GAMEPLAY_ASSERT(ShouldRideAsRagdoll());
		m_backseatRagdollEnabled = true;

		Npc* pCharacter = Npc::FromProcess(GetCharacter());

		if (ragdollSettingsId != INVALID_STRING_ID_64)
		{
			const float blendTime = blendTimeOverride >= 0.0f ? blendTimeOverride : pRiderRagdollSettings->m_ragdollBlendInTime;
			if (FALSE_IN_FINAL_BUILD(g_debugHorsePassengerRagdollAndIk && DebugSelection::Get().IsProcessOrNoneSelected(pCharacter)))
			{
				MsgConPauseable("Physicalizing ragdoll with %.2f blend time and %s settingsId\n", blendTime, DevKitOnly_StringIdToString(ragdollSettingsId));
			}

			pCharacter->PhysicalizeRagdoll(false, ragdollSettingsId, blendTime);

			const Horse* pHorse = GetHorse();
			const CompositeBody* pCompositeBody = pHorse->GetCompositeBody();
			const U32F bodyIndex = pCompositeBody->FindBodyIndexByJointSid(SID("pelvis"));
			GAMEPLAY_ASSERT(bodyIndex != CompositeBody::kInvalidBodyIndex);

			const RigidBody* pRigidBody = pCompositeBody->GetBody(bodyIndex);

			CompositeBody* pRagdollBody = pCharacter->GetRagdollCompositeBody();
			pRagdollBody->SetParentBody(pRigidBody);
			pRagdollBody->SetAutoUpdateParentBody(false);
		}

		//if (!m_backseatIkEnabled && !m_handIkRequest.ShouldEnableHandIk())
		//{
		//	m_handIkRequest.InitRequest();
		//}

		if (FALSE_IN_FINAL_BUILD(g_debugHorsePassengerRagdollAndIk && DebugSelection::Get().IsProcessOrNoneSelected(pCharacter)))
		{
			MsgNet2(FRAME_NUMBER_FMT "ENABLING Backseat Rider Ragdoll/IK on character %s, current anim %s [%s]\n",
					FRAME_NUMBER,
					DevKitOnly_StringIdToString(pCharacter->GetUserId()),
					DevKitOnly_StringIdToString(pCharacter->GetCurrentAnim()),
					DevKitOnly_StringIdToString(pCharacter->GetCurrentAnimState()));
		}


		// we were doing this in the script prototype, but this is probably not needed?
		//pCharacter->DisableCharacterCollider();
	}

	virtual void DisableRagdoll(float blendTimeOverride = -1.0f) override
	{
		if (!m_backseatRagdollEnabled)
			return;

		//GAMEPLAY_ASSERT(!ShouldRideAsRagdoll());

		const DC::HorseRiderRagdollSettings* pRiderRagdollSettings = GetBackseatRagdollSettings();
		GAMEPLAY_ASSERT(pRiderRagdollSettings);

		m_backseatRagdollEnabled = false;

		Npc* pCharacter = Npc::FromProcess(GetCharacter());
		if (!pCharacter->IsDead())
		{
			const float blendOutTime = blendTimeOverride >= 0.0f ? blendTimeOverride : pRiderRagdollSettings->m_ragdollBlendOutTime;
			pCharacter->BlendOutRagdoll(blendOutTime);
			pCharacter->GetRagdollCompositeBody()->SetAutoUpdateParentBody(true);


			// we were doing this in the script prototype, but this is probably not needed?
			//pCharacter->EnableCharacterCollider();
		}
		//DisableBackseatHandIkInternal(pRiderRagdollSettings);

		if (FALSE_IN_FINAL_BUILD(g_debugHorsePassengerRagdollAndIk && DebugSelection::Get().IsProcessOrNoneSelected(GetCharacter())))
		{
			MsgNet2(FRAME_NUMBER_FMT "DISABLING Backseat Rider Ragdoll/IK on character %s, current anim %s [%s]\n",
					FRAME_NUMBER,
					DevKitOnly_StringIdToString(pCharacter->GetUserId()),
					DevKitOnly_StringIdToString(pCharacter->GetCurrentAnim()),
					DevKitOnly_StringIdToString(pCharacter->GetCurrentAnimState()));
		}

	}

	void UpdateRagdollEnabled()
	{
		UpdateRagdollAllowedDueToJump();
		const bool wantRagdoll = ShouldRideAsRagdoll(true);

		if (wantRagdoll)
			EnableRagdoll();
		else
			DisableRagdoll();
	}

	const DC::MountAnimFadeSettings* GetMountFadeSettingsForAnim(StringId64 animName) const
	{
		const DC::HorseNpcRiderSettings& riderSettings = GetRiderSettings();
		const DC::MountAnimFadeSettingsArray* pArray = riderSettings.m_mountFadeSettings;
		for (int i = 0; i < pArray->m_count; ++i)
		{
			if (pArray->m_array[i].m_animStateName == animName)
				return &pArray->m_array[i];
		}
		return nullptr;
	}

	//assumes m_hHorse is valid
	void MountHorseInternal(bool skipAnimation, bool teleport, Maybe<HorseClearance::HorseSide> forceSide)
	{
		Horse* pHorse = m_hHorse.ToMutableProcess();
		ASSERT(pHorse);
		Character* pCharacter = GetCharacter();
		ASSERT(pCharacter);
		AnimStateLayer* pBaseLayer = pCharacter->GetAnimControl()->GetBaseStateLayer();
		ASSERT(pBaseLayer);

		m_isMoveStateChangeAllowed = true;
		m_animState = INVALID_STRING_ID_64;

		if (!pCharacter->GetSubsystemMgr()->FindSubsystem(SID("CharacterStirrupsIk")))
		{
			SubsystemSpawnInfo stirrupsIkInfo(SID("CharacterStirrupsIk"), pCharacter);
			NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap, stirrupsIkInfo, FILE_LINE_FUNC);
		}

		pHorse->SetNpcRider(Npc::FromProcess(pCharacter), FILE_LINE_FUNC, m_riderPosition);


		if (skipAnimation)
		{
			Gesture::PlayArgs args;
			args.m_blendInTime = 0.0f;
			args.m_blendOutTime = 0.0f;
			pCharacter->GetGestureController()->ClearNonBaseGestures(&args);
			MoveNpcToHorse();
			//m_frontSeatBoundFrame = pHorse->GetBoundFrame();
		}
		else
		{
			const StringId64 animState = GetMountAnimState(pHorse, pCharacter, forceSide);
			m_mountDismountBoundFrame = pHorse->GetBoundFrame();
			m_backSeatBoundFrame = pHorse->GetRiderAttachBoundFrame(HorseDefines::kRiderBack);
			m_frontSeatBoundFrame = pHorse->GetRiderAttachBoundFrame(HorseDefines::kRiderFront);
			//UpdateAnim(animState);

			GAMEPLAY_ASSERTF(IsMountAnimState(animState), ("Anim %s not found in mount anim list! Please add it to IsMountAnimState (or tell Harold)", DevKitOnly_StringIdToString(animState)));

			pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

			DC::AnimNpcTopInfo* pTopInfo = pCharacter->GetAnimControl()->TopInfo<DC::AnimNpcTopInfo>();

			Npc* pNpc = Npc::FromProcess(pCharacter);
			GAMEPLAY_ASSERT(pNpc);
			pTopInfo->m_rideHorse.m_playedFromBackseat = pNpc->GetAnimationControllers()->GetRideHorseController()->GetRiderPos() == HorseDefines::kRiderBack;

			const DC::MountAnimFadeSettings* pFadeSettings = GetMountFadeSettingsForAnim(animState);

			FadeToStateParams params;
			float fadeInSec = pFadeSettings ? pFadeSettings->m_fadeInTime : 0.4f;
			if (teleport)
				fadeInSec = 0.0f;
			params.m_stateStartPhase = 0.0f;
			params.m_animFadeTime = fadeInSec;
			params.m_motionFadeTime = fadeInSec;
			params.m_allowStateLooping = false;
			params.m_apRef = m_riderPosition == HorseDefines::kRiderFront ? m_frontSeatBoundFrame : m_backSeatBoundFrame; //m_mountDismountBoundFrame;
			params.m_apRefValid = true;
			params.m_blendType = DC::kAnimCurveTypeUniformS;
			pBaseLayer->FadeToState(animState, params);
			m_apEnterTime = Now();
			m_skipUpdate = true;
			m_mountGameFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;


			Gesture::PlayArgs args;
			args.m_blendInTime = fadeInSec;
			args.m_blendOutTime = fadeInSec;
			args.m_blendInCurve = DC::kAnimCurveTypeUniformS;
			args.m_blendOutCurve = DC::kAnimCurveTypeUniformS;
			pCharacter->GetGestureController()->ClearNonBaseGestures(&args);
		}

		if (ILimbManager* pLimbManager = pCharacter->GetLimbManager())
		{
			LimbLockRequest request;
			request.m_limbs = (kLockArmR | kLockArmL);
			request.m_subsystem = SID("drive");

			m_limbLock = pLimbManager->GetLock(request);
		}

		Npc* pNpc = Npc::FromProcess(pCharacter);
		if (pNpc)
		{
			pNpc->m_dialogLook.DisableGesturesForOneFrame(pNpc->GetClock()->GetCurTime());
		}

		if (m_riderPosition == HorseDefines::kRiderFront)
		{
			SetIsSteeringWithTwoHands(false);
		}
	}

	virtual void SetShouldMirrorSwitchSeatsAnim(bool mirror) override
	{
		m_mirrorSwapSeatsAnim = mirror;
	}

	bool ShouldMirrorSwitchSeatsAnim() const
	{
		if (!g_allowMirrorSwitchSeatsAnim)
			return false;

		return m_mirrorSwapSeatsAnim;
	}

	void SwitchSeatsInternal(HorseDefines::RiderPosition riderPos, bool skipAnim, bool doSwitchSeatsApFixup)
	{
		Horse* pHorse = m_hHorse.ToMutableProcess();
		ASSERT(pHorse);
		Character* pCharacter = GetCharacter();
		ASSERT(pCharacter);
		AnimStateLayer* pBaseLayer = pCharacter->GetAnimControl()->GetBaseStateLayer();
		ASSERT(pBaseLayer);

		ClearCommandGestures(pCharacter->GetGestureController());
		ClearTurnAnims();
		ClearSprintLean();

		Npc* pNpc = Npc::FromProcess(pCharacter);
		ASSERT(pNpc);
		pHorse->SetNpcRider(pNpc, FILE_LINE_FUNC, riderPos);

		m_switchSeatsGameFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
		m_needSwitchSeatsApFixup = doSwitchSeatsApFixup;

		m_animState = INVALID_STRING_ID_64;
		StringId64 animState = GetSwitchSeatsAnim(riderPos);
		//UpdateApRef();
		//UpdateMountDismountApRef();
		//MoveNpcToHorse();
		if (!skipAnim)
		{
			m_animState = animState;

			const DC::HorseNpcRiderSettings& riderSettings = GetRiderSettings();

			pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

			DC::AnimNpcTopInfo* pTopInfo = GetCharacter()->GetAnimControl()->TopInfo<DC::AnimNpcTopInfo>();

			// backwards of what you might expect because we havn't actually swapped seats at this point in time
			pTopInfo->m_rideHorse.m_playedFromBackseat = pNpc->GetAnimationControllers()->GetRideHorseController()->GetRiderPos() != HorseDefines::kRiderBack;

			FadeToStateParams params;
			params.m_stateStartPhase = 0.0f;
			params.m_animFadeTime = riderSettings.m_swapSeatsFadeTime;
			params.m_motionFadeTime = riderSettings.m_swapSeatsFadeTime;
			params.m_allowStateLooping = false;
			params.m_apRef = m_riderPosition == HorseDefines::kRiderFront ? m_backSeatBoundFrame : m_frontSeatBoundFrame;
			params.m_customApRefId = m_riderPosition == HorseDefines::kRiderFront ? SID("dinaAP") : SID("apReference");
			params.m_apRefValid = true;
			params.m_blendType = riderSettings.m_swapSeatsFadeCurve;
			pBaseLayer->FadeToState(animState, params);
			m_apEnterTime = Now();
			m_skipUpdate = true;
			m_swapRiderPosPending = true;
			//UpdateApRefOnAllAnims();
		}
		else
		{
			//if we skipped the animation we want to immediately start using the new apRef
			m_switchSeatsGameFrame = 0;
			m_riderPosition = riderPos;
		}

		m_swapSeatsRequest.Clear();

		//if (ShouldRideAsRagdoll())
		//{
		//	EnableRagdoll();
		//}
		//else
		//{
		//	DisableRagdoll();
		//}
	}

	bool CanPerformSwitchSeats() const
	{
		if (IsDismounting())
			return false;

		if (IsMounting())
		{
			const AnimStateInstance* pCurInstance = GetCharacter()->GetAnimControl()->GetBaseStateLayer()->CurrentStateInstance();
			if (IsMountAnimState(pCurInstance->GetStateName()) && pCurInstance->Phase() < 0.325f)
				return false;
		}

		return true;
	}

	void SwitchSeatsFromRequest(const SwapSeatsRequest& request)
	{
		if (!CanPerformSwitchSeats())
		{
			m_swapSeatsRequest = SwapSeatsRequest(request.m_newRiderPos, request.m_skipAnim);
			return;
		}

		SwitchSeatsInternal(request.m_newRiderPos, request.m_skipAnim, true);
	}

	void SwitchSeats(HorseDefines::RiderPosition riderPos, bool skipAnim)
	{
		if (GetCharacter()->IsInScriptedAnimationState())
			return;

		if (!CanPerformSwitchSeats())
		{
			m_swapSeatsRequest = SwapSeatsRequest(riderPos, skipAnim);
			return;
		}

		SwitchSeatsInternal(riderPos, skipAnim, false);
	}

	float FixupPhase(float rawPhase, bool looping) const
	{
		float adjustedPhase = rawPhase;
		if (looping)
		{
			//wraparound to valid range
			while (adjustedPhase < 0.0f)
				adjustedPhase += 1.0f;
			while (adjustedPhase > 1.0f)
				adjustedPhase -= 1.0f;
		}
		else
		{
			//clamp to valid range
			adjustedPhase = MinMax01(adjustedPhase);
		}
		return adjustedPhase;
	}

	float CalculateRiderPhase(Horse* pHorse, float updateRate, int framesBehind, bool looping) const
	{
		float dt = GetProcessDeltaTime();
		float horsePhase = pHorse->GetAnimPhase();
		float phase = horsePhase - (dt * updateRate * framesBehind);
		float adjustedPhase = FixupPhase(phase, looping);
		if (FALSE_IN_FINAL_BUILD(g_debugHorseRiderPhaseOffset))
		{
			MsgConPauseable("Horse Anim Phase: %.5f\nRider Adjusted Phase: %.5f\nRider Delta Time: %.6f\n", horsePhase, phase, dt);
		}
		return adjustedPhase;
	}

	float CalculateRiderPhaseNoUpdateRate(Horse* pHorse, const ArtItemAnim* pAnim, float curPhase, int framesBehind, bool looping) const
	{
		const float phasePerFrame = pAnim->m_pClipData->m_phasePerFrame;
		const float phase = curPhase - (framesBehind * phasePerFrame);
		const float adjustedPhase = FixupPhase(phase, looping);
		if (FALSE_IN_FINAL_BUILD(g_debugHorseRiderPhaseOffset))
		{
			MsgConPauseable("Frames behind: %d\nBase Phase: %.6f\nAdjusted Phase: %.6f\nPhase Per Frame: %.6f\n", framesBehind, phase, adjustedPhase, phasePerFrame);
		}
		return adjustedPhase;
	}

	StringId64 GetRiderAnim(const Horse* pHorse) const
	{
		StringId64 horseAnim = pHorse->GetCurrentAnim();
		StringId64 animId = StringId64Concat(horseAnim, "--npc");

		if (pHorse->IsHorsePettingAnim(horseAnim))
			animId = kDefaultIdleAnim;

		//dirty temp hack since we don't have a new rear animation yet
		if (horseAnim == SID("horse-idle^rear"))
			animId = SID("ellie-horse-rear");

		if (m_riderPosition == HorseDefines::kRiderBack)
			animId = GetRemappedBackseatAnim(animId);

		return animId;
	}

	bool IsRemappedBackseatAnim(const StringId64 animNameId) const
	{
		const DC::HorseNpcRiderSettings& settings = GetRiderSettings();
		for (int i = 0; i < settings.m_backSeatAnimRemaps->m_count; ++i)
		{
			if (animNameId == settings.m_backSeatAnimRemaps->m_array[i].m_newAnim)
				return true;
		}
		return false;
	}

	StringId64 GetRemappedBackseatAnim(StringId64 baseAnimId) const
	{
		const DC::HorseNpcRiderSettings& settings = GetRiderSettings();
		return RemapRiderAnimForBackseat(settings, baseAnimId);
	}

	bool IsHorseAnimLooping(Horse* pHorse)
	{
		const ArtItemAnim* pArtItem = pHorse->GetAnimControl()->LookupAnim(pHorse->GetCurrentAnim()).ToArtItem();
		if (!pArtItem)
			return false;
		return pArtItem->IsLooping();
	}

	bool CanFlip() const
	{
		// we could turn on hand IK after we start playing a mirrored anim, so never allow flipping in the backseat
		return GetRiderPos() != HorseDefines::kRiderBack;
	}

	bool ShouldUseRagdollAnim() const
	{
		return GetRiderPos() == HorseDefines::kRiderBack && GetBackseatRagdollSettings() != nullptr;
	}

	// return value is whether we should be allowed to play a new anim
	bool UpdateTopInfo(AnimControl* pControl, const Horse* pHorse)
	{
		const StringId64 currentAnimState = GetCharacter()->GetCurrentAnimState();

		if (!pControl || !pHorse || pHorse->IsDead())
			return false;

		const SsAnimateController* pHorseAnimCtrl = pHorse->GetPrimarySsAnimateController();

		m_animState = SID("ride-horse-mm");

		if (pHorseAnimCtrl && pHorseAnimCtrl->IsPlayingCinematic())
			return false;

		if (m_pAimStateMachine && m_pAimStateMachine->IsActive())
			return false;

		const StringId64 horseAnimState = pHorse->GetCurrentAnimState();
		StringId64 animId = GetRiderAnim(pHorse);

		if (animId == SID("ellie-horse-rear"))
			m_animState = SID("ride-horse-rear");

		bool newAnimAllowed = true;
		if (ShouldUseRagdollAnim() && !IsAnimWhitelistedForRagdoll(animId))
		{
			m_animState = SID("ride-horse-no-sync");
			animId = GetRemappedBackseatAnim(kDefaultIdleAnim);
			newAnimAllowed = currentAnimState != m_animState;
		}

		//GAMEPLAY_ASSERT(animId != SID("horse-mm-ambi-idle--npc-back"));

		const AnimStateLayer* pHorseBaseLayer = pHorse->GetAnimControl()->GetBaseStateLayer();
		ASSERT(pHorseBaseLayer);

		const DC::AnimNpcTopInfo* pHorseTopInfo = pHorse->GetAnimControl()->TopInfo<DC::AnimNpcTopInfo>();

		const bool flipped = newAnimAllowed && pHorseBaseLayer->IsFlipped();

		DC::AnimNpcTopInfo* pTopInfo = pControl->TopInfo<DC::AnimNpcTopInfo>();
		pTopInfo->m_rideHorse.m_animMm = animId;

		pTopInfo->m_rideHorse.m_flip = flipped && CanFlip();

		return newAnimAllowed;
	}

	void UpdateApRef()
	{
		const Horse* pHorse;
		if (m_hHorse.Valid())
			pHorse = m_hHorse.ToProcess();
		else if (m_hOldHorse.Valid())
			pHorse = m_hOldHorse.ToProcess();
		else
			return;

		m_frontSeatBoundFrame = pHorse->GetRiderAttachBoundFrame(HorseDefines::kRiderFront);
		m_backSeatBoundFrame = pHorse->GetRiderAttachBoundFrame(HorseDefines::kRiderBack);

		m_mountDismountBoundFrame = pHorse->GetBoundFrame();
	}

	bool IsInTapAnim(const DC::AnimNpcTopInfo* pTopInfo) const
	{
		return IsHorseRiderTapAnim(pTopInfo->m_rideHorse.m_animMm);
	}

	const BoundFrame& GetApRef(bool doSwapSeatsRemap) const
	{
		HorseDefines::RiderPosition effectiveRiderPos = m_riderPosition;
		if (doSwapSeatsRemap && IsSwitchingSeats())
		{
			if (m_riderPosition == HorseDefines::kRiderFront)
				effectiveRiderPos = HorseDefines::kRiderBack;
			else
				effectiveRiderPos = HorseDefines::kRiderFront;
		}

		if (effectiveRiderPos == HorseDefines::kRiderBack)
			return m_backSeatBoundFrame;
		else
			return m_frontSeatBoundFrame;
	}

	void StartNewAnim(AnimStateLayer* pBaseLayer,
					  const Horse* pHorse,
					  bool doSwapSeatsRemap,
					  F32 animFadeTime,
					  F32 motionFadeTime = -1.0f,
					  DC::AnimCurveType blendCurve = DC::kAnimCurveTypeUniformS,
					  FadeToStateParams::NewInstanceBehavior nib = FadeToStateParams::kUnspecified)
	{
		//if (pHorse->IsInScriptState() || pHorse->IsInScriptedAnimationState())
		if (GetCharacter()->IsInScriptedAnimationState())
			return;

		m_dontExitIgcBeforeHorse = false;

		if (motionFadeTime < 0.0f)
			motionFadeTime = animFadeTime;

		pBaseLayer->RemoveAllPendingTransitions(StateChangeRequest::kTypeFlagAll);

		DC::AnimNpcTopInfo* pTopInfo = GetCharacter()->GetAnimControl()->TopInfo<DC::AnimNpcTopInfo>();

		pTopInfo->m_rideHorse.m_playedFromBackseat = GetRiderPos() == HorseDefines::kRiderBack;

		UpdateApRef();

		FadeToStateParams params;
		params.m_apRef = GetApRef(doSwapSeatsRemap);
		params.m_apRefValid = true;
		params.m_animFadeTime = animFadeTime;
		params.m_motionFadeTime = motionFadeTime;
		params.m_allowStateLooping = true;
		params.m_customApRefId = GetApRefName(pHorse);
		params.m_blendType = blendCurve;
		params.m_newInstBehavior = nib;

#ifdef HTOWNSEND
		GAMEPLAY_ASSERT(CanFlip() || !pTopInfo->m_rideHorse.m_flip);
#endif

		pBaseLayer->FadeToState(m_animState, params);
	}

	StringId64 GetApRefName(const Horse* pHorse) const
	{
		if (m_animState == SID("ride-horse-no-sync"))
			return SID("dinaAP");

		StringId64 animName = GetRiderAnim(pHorse);

		const AnimControl* pAnimControl = GetCharacter()->GetAnimControl();
		const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animName).ToArtItem();
		if (!pAnim)
		{
			MsgConScriptError("Anim %s not found in character %s's anim control\n", DevKitOnly_StringIdToString(animName), DevKitOnly_StringIdToString(GetCharacter()->GetUserId()));
			return SID("apReference");
		}

		StringId64 apName;
		if (m_riderPosition == HorseDefines::kRiderFront)
		{
			apName = SID("driverAP");
			if (AnimHasChannel(pAnim, apName))
				return apName;
		}
		else
		{
			if (IsNoFrameOffsetAnim(animName))
				return SID("dinaAP");

			if (IsUseDinaApAnim(animName))
				return SID("dinaAP");

			apName = SID("riderAP");
			if (AnimHasChannel(pAnim, apName))
				return apName;
		}

		return SID("apReference");
	}

	void MoveNpcToHorse()
	{
		UpdateApRef();
		const BoundFrame& apRef = GetApRef(true);

		Character* pCharacter = GetCharacter();
		AnimStateLayer* pBaseLayer = pCharacter->GetAnimControl()->GetBaseStateLayer();
		pBaseLayer->UpdateAllApReferences(apRef, RideHorseApUpdateFilter);
	}

	void UpdateSlopeLean(const Horse* pHorse)
	{
		Character* pCharacter = GetCharacter();
		AnimControl* pAnimControl = pCharacter->GetAnimControl();
		AnimSimpleLayer* pSlopeLayer = pAnimControl->GetSimpleLayerById(kSlopeLeanAdditiveLayer);

		AnimSimpleInstance* pExistingAnim = pSlopeLayer ? pSlopeLayer->CurrentInstance() : nullptr;

		const float slopeLeanPhase = g_disableHorseSlopeLean ? 0.0f : pHorse->GetSlopeLeanPhase(g_debugHorseSlopeLean);
		const float fadeTime = pHorse->GetHorseAnimSettings().m_slopeLeanSettings.m_slopeLeanFadeTime;

		const bool wantSlopeLean = slopeLeanPhase > 0.01f && GetRiderPos() == HorseDefines::kRiderBack;
		if (wantSlopeLean)
		{
			if (!pSlopeLayer)
			{
				pSlopeLayer = pAnimControl->CreateSimpleLayer(kSlopeLeanAdditiveLayer, ndanim::kBlendAdditive, 131);
				ANIM_ASSERT(pSlopeLayer);
				pSlopeLayer->SetCurrentFade(0.001f);
			}

			if (!pExistingAnim)
			{
				AnimSimpleLayer::FadeRequestParams params;
				params.m_startPhase = slopeLeanPhase;
				params.m_fadeTime = fadeTime;
				params.m_layerFade = 1.0f;
				params.m_blendType = DC::kAnimCurveTypeUniformS;
				pSlopeLayer->RequestFadeToAnim(SID("dina-horse-neck-up-hill--add"), params);
			}
		}
		else
		{
			if (pSlopeLayer)
			{
				pSlopeLayer->FadeOutAndDestroy(fadeTime);
			}
		}

		if (pExistingAnim)
			pExistingAnim->SetPhase(slopeLeanPhase);
	}

	bool ShouldDuck() const
	{
		if (GetCharacter()->IsInScriptedAnimationState())
			return false;

		const Horse* pHorse = GetHorse();
		return pHorse && pHorse->ShouldDuck(GetRiderPos());
	}

	void UpdateDuck(const Horse* pHorse, AnimControl* pAnimControl)
	{
		AnimSimpleLayer* pDuckLayer = pAnimControl->GetSimpleLayerById(kDuckAdditiveLayer);
		const bool frontSeat = m_riderPosition == HorseDefines::kRiderFront;
		if (ShouldDuck())
		{
			if (!pDuckLayer)
			{
				pDuckLayer = pAnimControl->CreateSimpleLayer(kDuckAdditiveLayer, ndanim::kBlendAdditive, 127);
				//idk why this is needed, seems like it should start at 0 fade by default but it actually starts at 1
				pDuckLayer->SetCurrentFade(0.001f);
			}
			ALWAYS_ASSERT(pDuckLayer);
			const float fadeTime = frontSeat ? GetRiderSettings().m_duckAnimFadeInTime : GetRiderSettings().m_duckAnimBackSeatFadeInTime;
			const float layerFade = pHorse->GetDuckAnimFade(GetRiderPos());
			if (!pDuckLayer->CurrentInstance())
			{
				AnimSimpleLayer::FadeRequestParams params;
				params.m_startPhase = 0.0f;
				params.m_fadeTime = fadeTime;
				params.m_layerFade = layerFade;
				params.m_blendType = DC::kAnimCurveTypeUniformS;
				params.m_mirror = pHorse->WantDuckAnimFlip(GetRiderPos());
				StringId64 animId = frontSeat ? SID("ellie-horse-duck-under--add") : SID("dina-horse-duck-under--add");
				pDuckLayer->RequestFadeToAnim(animId, params);
			}
		}
		else
		{
			if (pDuckLayer)
			{
				const float fadeTime = frontSeat ? GetRiderSettings().m_duckAnimFadeOutTime : GetRiderSettings().m_duckAnimBackSeatFadeOutTime;
				pDuckLayer->FadeOutAndDestroy(fadeTime);
			}
		}
	}
	void AllocateAnimLayers(NdGameObject* pCharacter)
	{
		AnimControl* pAnimControl = pCharacter->GetAnimControl();
		pAnimControl->AllocateSimpleLayer(kDuckAdditiveLayer, ndanim::kBlendAdditive, 127, 1); //duck layer
		//pAnimControl->AllocateSimpleLayer(kSprintLeanAdditiveLayer, ndanim::kBlendAdditive, 128, 1); //sprint lean layer
		pAnimControl->AllocateSimpleLayer(kSlopeLeanAdditiveLayer, ndanim::kBlendAdditive, 131, 1);

		{
			AnimStateLayerParams params;
			params.m_blendMode = ndanim::kBlendAdditive;
			params.m_priority = 128;
			pAnimControl->AllocateStateLayer(kSprintLeanAdditiveLayer, params);
		}

		//{
		//	AnimStateLayerParams params;
		//	params.m_blendMode = ndanim::kBlendAdditive;
		//	params.m_priority = 131;
		//	pAnimControl->AllocateStateLayer(kSlopeLeanAdditiveLayer, params); //additive slope lean layer
		//}
	}

	void StopDismountSsAction()
	{
		m_dismountSsAction.Stop();
	}

	void UpdateSsWaits()
	{
		if (!m_dismountSsAction.IsStarted())
			return;

		const bool isDismounting = IsDismounting();
		GAMEPLAY_ASSERT(isDismounting || m_dismountPending);

		if (IsDismounting())
		{
			Character* pCharacter = GetCharacter();
			AnimControl* pAnimControl = pCharacter->GetAnimControl();
			ASSERT(pAnimControl);
			AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
			ASSERT(pBaseLayer);
			AnimStateInstance* pInstance = pBaseLayer->CurrentStateInstance();

			if (pInstance->Phase() >= 0.65)
			{
				m_dismountSsAction.Stop();
			}
		}
	}

	bool IsDrivingHorse(bool weaponsAllowed) const
	{
		const AnimationControllers* pAnimControllers = GetCharacter()->GetAnimationControllers();
		const IAiWeaponController* pWeaponController = pAnimControllers ? pAnimControllers->GetWeaponController() : nullptr;

		bool weaponsValid = true;
		if (!weaponsAllowed)
			weaponsValid = pWeaponController ? pWeaponController->IsWeaponUpRequested() : true;

		return m_riderPosition == HorseDefines::kRiderFront
								  && !IsMounting()
								  && !IsDismounting()
								  && !IsSwitchingSeats()
								  && weaponsValid;
	}

	bool RightHandBusy() const
	{
		//TODO
		return false;


		//IPlayerWeaponController* pWeaponController = m_pPlayer->m_pWeaponController;
		//return pWeaponController->IsWeaponOut() || pWeaponController->IsDrawingWeapon() || pWeaponController->IsRechambering();
	}

	bool ShouldSteerWithTwoHands()
	{
		const Horse* pHorse = m_hHorse.ToProcess();
		if (!pHorse)
			return false;

		const TimeFrame curTime = GetProcessClock()->GetCurTime();

		const bool isSprinting = pHorse->IsSprinting();
		const bool rightHandBusy = RightHandBusy();
		if (isSprinting)
			m_lastSprintTime = curTime;

		if (rightHandBusy)
		{
			return false;
		}

		if (IsTwoHandedSteeringRequested())
		{
			return true;
		}

		if (IsOneHandedSteeringRequested())
		{
			return false;
		}

		if (isSprinting)
		{
			return true;
		}

		float secondsSinceSprint = (curTime - m_lastSprintTime).ToSeconds();
		const DC::HorseNpcRiderSettings& riderSettings = GetRiderSettings();
		return secondsSinceSprint <= riderSettings.m_oneHandedSteeringDelay;
	}

	void SetIsSteeringWithTwoHands(bool usingTwoHands)
	{
		NavCharacter* pChar = GetCharacter();
		GAMEPLAY_ASSERT(pChar);

		m_isSteeringWithTwoHands = usingTwoHands;
		const char* baseName = pChar->GetOverlayBaseName();
		AnimOverlays* pOverlays = pChar->GetAnimControl()->GetAnimOverlays();

		if (m_isSteeringWithTwoHands)
		{
			pOverlays->SetOverlaySet(AI::CreateNpcAnimOverlayName(baseName, "horse-steering", "two-hand"));
		}
		else
		{
			pOverlays->SetOverlaySet(AI::CreateNpcAnimOverlayName(baseName, "horse-steering", "one-hand"));
		}
	}

	bool IsBlizzardSteeringRequested() const
	{
		const I64 gameFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
		if (gameFrame <= m_blizzardSteeringRequestFrame + 1)
		{
			return !IsAnyMountAnimPlaying();
		}
		return false;
	}

	bool IsWaterBlockSteeringRequested() const
	{
		const I64 gameFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
		if (gameFrame <= m_waterBlockSteeringRequestFrame + 1)
		{
			return !IsAnyMountAnimPlaying();
		}
		return false;
	}

	bool IsOneHandedSteeringRequested() const
	{
		const I64 gameFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
		return gameFrame <= m_oneHandSteeringRequestFrame + 1;
	}

	bool IsTwoHandedSteeringRequested() const
	{
		const I64 gameFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
		if (gameFrame <= m_twoHandSteeringRequestFrame + 1)
		{
			return !IsAnyMountAnimPlaying();
		}
		return false;
	}

	bool AreSystemicGesturesDisabled() const
	{
		const I64 gameFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
		return gameFrame <= m_disableSystemicGesturesFrame + 1;
	}

	bool AreCommandGesturesDisabled() const
	{
		const I64 gameFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
		return gameFrame <= m_disableCommandGesturesFrame + 1;
	}

	StringId64 GetDesiredTurnGestureId()
	{
		const TimeDelta slowGesturePlaytime = Seconds(GetRiderSettings().m_oneHandedSteeringDelay);
		const bool slowingFromSprint = GetProcessClock()->GetCurTime() - m_lastSlowFromGallopTime < slowGesturePlaytime;

		const bool inBlizzard = IsBlizzardSteeringRequested();
		const bool inWater = IsWaterBlockSteeringRequested();
		const bool wantTwoHand = ShouldSteerWithTwoHands();

		const bool needTwoHands = !inWater && (wantTwoHand || slowingFromSprint || inBlizzard);
		const bool needTransition = needTwoHands != m_isSteeringWithTwoHands;
		SetIsSteeringWithTwoHands(needTwoHands);

		if (inWater)
			return kRideHorseWaterBlockedGestureId;

		if (inBlizzard)
			return kRideHorseTwoHandsBlizzardGestureId;

		if (slowingFromSprint)
		{
			return kRideHorseSlowTwoHandedTurnGestureId;
		}
		else if (wantTwoHand)
		{
			if (needTransition)
			{
				return kRideHorseOneToTwoHandsGestureId;
			}
			else
			{
				return kRideHorseTwoHandedTurnGestureId;
			}
		}
		else
		{
			if (needTransition)
			{
				return kRideHorseTwoToOneHandsGestureId;
			}
			else
			{
				return kRideHorseOneHandedTurnGestureId;
			}
		}
	}

	void UpdateGestureTargets(Point_arg gestureTarget)
	{
		IGestureController* pGestureController = GetCharacter()->GetGestureController();

		if (m_hTurnGesture.Valid())
		{
			const Gesture::TargetPoint targetPt = Gesture::TargetPoint(BoundFrame(gestureTarget));
			m_hTurnGesture.UpdateTarget(pGestureController, &targetPt);
		}
	}

	bool AreTurnAnimsDisabled() const
	{
		return !IsDrivingHorse(true) || AreSystemicGesturesDisabled() || g_disableHorseRiderTurnAnims;
	}

	void UpdateTurnDirection(Vector_arg fakeStickInput)
	{
		Vector direction;
		if (LengthSqr(fakeStickInput) < 0.01f)
		{
			direction = AsUnitVectorXz(GetLocalZ(GetHorse()->GetRotation()), kZero);
		}
		else
		{
			direction = AsUnitVectorXz(fakeStickInput, kZero);
		}

		GAMEPLAY_ASSERT(IsNormal(direction));
		if (!IsNormal(direction))
			return;

		NavCharacter* pChar = GetCharacter();
		const Point gestureTarget = pChar->GetTranslation() + direction;

		UpdateGestureTargets(gestureTarget);

		IGestureController* pGestureController = pChar->GetGestureController();

		StringId64 gestureId = GetDesiredTurnGestureId();
		if (gestureId == INVALID_STRING_ID_64)
			return;

		// has to be after calling GetDesiredTurnGestureId so we can update overlay
		if (!IsDrivingHorse(true) || FALSE_IN_FINAL_BUILD(g_disableHorseRiderTurnAnims))
		{
			ClearTurnAnims(); // may not be needed
			return;
		}

		m_turnGestureRemap.SetSourceId(gestureId);
		m_turnGestureRemap = Gesture::RemapGesture(m_turnGestureRemap, pChar->GetAnimControl());

		if (!pGestureController->IsPlayingOnAnyLayer(m_turnGestureRemap.m_finalGestureId))
		{
			ClearTurnAnims();
			Gesture::PlayArgs args;
			args.m_tryRefreshExistingGesture = true;
			args.m_gestureLayer = pGestureController->GetFirstRegularGestureLayerIndex();
			args.SetPriority(DC::kGesturePriorityRideHorse);
			args.m_pOutGestureHandle = &m_hTurnGesture;
			Gesture::Err gerr = pGestureController->PlayGesture(m_turnGestureRemap.m_finalGestureId, gestureTarget, &args);

			//if (!gerr.Success())
			//	MsgConPauseable("%s[%s] Gesture %s Failed because of: %s\nExtra details: %s\n%s",
			//		GetTextColorString(kTextColorRed),
			//		DevKitOnly_StringIdToString(pChar->GetUserId()),
			//		DevKitOnly_StringIdToString(gestureId),
			//		DevKitOnly_StringIdToString(gerr.m_errId),
			//		gerr.m_pLockoutReason,
			//		GetTextColorString(kTextColorNormal));
		}
	}

	const DC::HorseAnimSettings& GetHorseAnimSettings() const
	{
		const Horse* pHorse = m_hHorse.ToProcess();
		if (!pHorse)
			pHorse = m_hOldHorse.ToProcess();

		if (pHorse)
		{
			const DC::HorseAnimSettings& settings = pHorse->GetHorseAnimSettings();
			return settings;
		}
		else
		{
			const DC::HorseAnimSettings* pSettings = ScriptManager::LookupInModule<DC::HorseAnimSettings>(Horse::kHorseAnimSettingsId, Horse::kHorseAnimSettingsModule);
			GAMEPLAY_ASSERT(pSettings);
			return *pSettings;
		}
	}

	void UpdateSprintLean(bool isSprinting)
	{
		AnimControl* pAnimControl = GetCharacter()->GetAnimControl();
		AnimStateLayer* pAdditiveSprintLayer = pAnimControl->GetStateLayerById(kSprintLeanAdditiveLayer);

		const float fadeTime = GetRiderSettings().m_sprintLeanFadeTime;

		if (isSprinting && !g_disableHorseRiderSprintLean)
		{
			if (!pAdditiveSprintLayer)
			{
				pAdditiveSprintLayer = pAnimControl->CreateStateLayer(kSprintLeanAdditiveLayer, ndanim::kBlendAdditive, 512);
			}
			GAMEPLAY_ASSERT(pAdditiveSprintLayer);
			pAdditiveSprintLayer->Fade(0.4f, fadeTime);

			FadeToStateParams params;
			params.m_stateStartPhase = 0.0f;
			params.m_animFadeTime = fadeTime;
			params.m_blendType = DC::kAnimCurveTypeUniformS;
			pAdditiveSprintLayer->FadeToState(kSprintLeanAnimId, params);
		}
		else if (pAdditiveSprintLayer)
		{
			pAdditiveSprintLayer->FadeOutAndDestroy(fadeTime);
		}
	}

	void UpdateCommandGestures()
	{
		m_isMoveStateChangeAllowed = true;

		IGestureController* pGestureController = GetCharacter()->GetGestureController();

		//-1 because slow command doesn't interrupt move state changes
		for (U32F i = 0; i < kNumCommandGestures/* - 1*/; ++i)
		{
			I32F gestureLayer = pGestureController->FindLayerPlaying(kCommandGestureIds[i]);
			if (gestureLayer < 0)
				continue;

			float phase = pGestureController->GetGesturePhase(gestureLayer);
			float regainControlPhase = 0.5f;

			switch (i)
			{
			case 1:
				regainControlPhase = 0.25f;
				break;
			case 3:
				regainControlPhase = 0.125f;
				break;
			default:
				break;
			}

			if (phase < regainControlPhase)
			{
				m_isMoveStateChangeAllowed = false;
				return;
			}
		}
	}

	inline void ClearCommandGestures(IGestureController* pGestureController, float fadeTimeOverride = -1.0f)
	{
		ASSERT(pGestureController);
		for (int i = 0; i < kNumCommandGestures; ++i)
		{
			I32F gestureLayer = pGestureController->FindLayerPlaying(kCommandGestureIds[i]);
			if (gestureLayer < 0)
				continue;
			Gesture::PlayArgs params;
			if (fadeTimeOverride >= 0.0f)
				params.m_blendOutTime;
			pGestureController->ClearGesture(gestureLayer, &params);
		}
	}

	virtual void PlayCommandGesture(StringId64 commandGestureId) override
	{
		if (g_disableHorseCommandAnims)
			return;

		if (AreSystemicGesturesDisabled())
			return;

		if (AreCommandGesturesDisabled())
			return;

		//check if we should play gesture? Like CanPlayCommandGesture() in player-ride-horse.cpp?

		IGestureController* pGestureController = GetCharacter()->GetGestureController();
		ASSERT(pGestureController);

		ClearCommandGestures(pGestureController);

		Gesture::PlayArgs args;
		args.m_gestureLayer = pGestureController->GetFirstRegularGestureLayerIndex() + 1;
		args.SetPriority(DC::kGesturePriorityRideHorse);

		Gesture::Err gestureError = pGestureController->Play(commandGestureId, args);

		if (gestureError.Success())
			m_isMoveStateChangeAllowed = false;
	}

	virtual void ScriptRequestBlizzardSteering() override
	{
		m_blizzardSteeringRequestFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
	}

	virtual void ScriptRequestWaterBlockSteering() override
	{
		m_waterBlockSteeringRequestFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
	}

	virtual void ScriptRequestOneHandedSteering() override
	{
		m_oneHandSteeringRequestFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
	}

	virtual void ScriptRequestTwoHandedSteering() override
	{
		m_twoHandSteeringRequestFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
	}

	virtual void ScriptDisableSystemicGestures() override
	{
		m_disableSystemicGesturesFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
		ClearTurnAnims();
		ClearCommandGestures(GetCharacter()->GetGestureController());
	}

	virtual void ScriptDisableCommandGestures() override
	{
		m_disableCommandGesturesFrame = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
		ClearCommandGestures(GetCharacter()->GetGestureController());
	}

	void ClearTurnAnims(float fadeTimeOverride = -1.0f)
	{
		NavCharacter* pChar = GetCharacter();
		IGestureController* pGestureController = pChar->GetGestureController();
		Gesture::PlayArgs args;
		if (fadeTimeOverride >= 0.0f)
			args.m_blendOutTime = fadeTimeOverride;
		m_hTurnGesture.Clear(pGestureController, args);

		if (!IsMounting() && !IsSwitchingSeats())
		{
			AnimOverlays* pOverlays = pChar->GetAnimControl()->GetAnimOverlays();
			if (pOverlays)
			{
				const char* baseName = pChar->GetOverlayBaseName();
				pOverlays->ClearLayer(SID("horse-steering"));
			}
		}
	}

	void ClearSprintLean(float fadeTimeOverride = -1.0f)
	{
		AnimControl* pAnimControl = GetCharacter()->GetAnimControl();
		AnimSimpleLayer* pSprintLeanLayer = pAnimControl->GetSimpleLayerById(kSprintLeanAdditiveLayer);
		if (pSprintLeanLayer)
		{
			const float fadeTime = fadeTimeOverride >= 0.0f ? fadeTimeOverride : GetRiderSettings().m_sprintLeanFadeTime;
			pSprintLeanLayer->FadeOutAndDestroy(fadeTime);
		}
	}

	void ClearDuck(float fadeTimeOverride = -1.0f)
	{
		AnimControl* pAnimControl = GetCharacter()->GetAnimControl();
		AnimSimpleLayer* pDuckLayer = pAnimControl->GetSimpleLayerById(kDuckAdditiveLayer);
		if (pDuckLayer)
		{
			const float fadeTime = fadeTimeOverride >= 0.0f ? fadeTimeOverride : GetRiderSettings().m_duckAnimFadeOutTime;
			pDuckLayer->FadeOutAndDestroy(fadeTime);
		}
	}

	void ClearIdleGestures(float fadeTimeOverride = -1.0f)
	{
		IGestureController* pGestureController = GetCharacter()->GetGestureController();
		if (pGestureController)
		{
			const I32F iLayer = pGestureController->GetFirstRegularGestureLayerIndex() + 1;
			Gesture::PlayArgs playArgs;
			if (fadeTimeOverride >= 0.0f)
				playArgs.m_blendOutTime = fadeTimeOverride;
			pGestureController->ClearGesture(iLayer, &playArgs);
		}
	}

	void ClearSlopeLean(float fadeTimeOverride = -1.0f)
	{
		AnimControl* pAnimControl = GetCharacter()->GetAnimControl();
		AnimSimpleLayer* pSlopeLayer = pAnimControl->GetSimpleLayerById(kSlopeLeanAdditiveLayer);
		if (pSlopeLayer)
		{
			const Horse* pHorse = GetHorse();
			const float fadeTime = fadeTimeOverride >= 0.0f ? fadeTimeOverride : pHorse->GetHorseAnimSettings().m_slopeLeanSettings.m_slopeLeanFadeTime;
			pSlopeLayer->FadeOutAndDestroy(fadeTime);
		}
	}

	virtual void Cleanup(float fadeTimeOverride = -1.0f, bool includeTurnAnims = true) override
	{
		ClearDuck(fadeTimeOverride);
		ClearCommandGestures(GetCharacter()->GetGestureController(), fadeTimeOverride);
		ClearSprintLean(fadeTimeOverride);
		if (includeTurnAnims)
			ClearTurnAnims(fadeTimeOverride);
		ClearIdleGestures(fadeTimeOverride);
		ClearSlopeLean(fadeTimeOverride);
	}

	virtual bool AreScriptedGesturesAllowed(bool allowMounting/* = false*/, bool allowDismounting/* = false*/, bool debug/* = false*/) const override
	{
		const Horse* pHorse = GetHorse();
		if (!pHorse)
			return true;

		if (!allowMounting && IsMounting())
		{
			if (FALSE_IN_FINAL_BUILD(debug))
			{
				MsgConPauseable("%s: scripted gestures not allowed because they are mounting\n", DevKitOnly_StringIdToString(GetCharacter()->GetUserId()));
			}
			return false;
		}

		if (!allowDismounting && IsDismounting())
		{
			if (FALSE_IN_FINAL_BUILD(debug))
			{
				MsgConPauseable("%s: scripted gestures not allowed because they are dismounting\n", DevKitOnly_StringIdToString(GetCharacter()->GetUserId()));
			}
			return false;
		}

		if (IsSwitchingSeats())
		{
			if (FALSE_IN_FINAL_BUILD(debug))
			{
				MsgConPauseable("%s: scripted gestures not allowed because they are switching seats\n", DevKitOnly_StringIdToString(GetCharacter()->GetUserId()));
			}
			return false;
		}

		if (pHorse->IsPathingThroughTAP())
		{
			if (FALSE_IN_FINAL_BUILD(debug))
			{
				MsgConPauseable("%s: scripted gestures not allowed because their horse is about to take a TAP\n", DevKitOnly_StringIdToString(GetCharacter()->GetUserId()));
			}
			return false;
		}


		const HorseJumpController* pJumpController = pHorse->GetJumpController();
		GAMEPLAY_ASSERT(pJumpController);
		if (pJumpController->IsJumping())
		{
			if (FALSE_IN_FINAL_BUILD(debug))
			{
				MsgConPauseable("%s: scripted gestures not allowed because their horse is in a jump\n", DevKitOnly_StringIdToString(GetCharacter()->GetUserId()));
			}
			return false;
		}

		return true;
	}


public:
	AiRideHorseController()
		: m_animState(INVALID_STRING_ID_64)
		, m_hHorse(nullptr)
		, m_riderState(kStateInactive)
		, m_frontSeatBoundFrame(kZero)
		, m_backSeatBoundFrame(kZero)
		, m_mountDismountBoundFrame(kZero)
		, m_apEnterTime(kTimeFrameNegInfinity)
		, m_lastSprintTime(kTimeFrameNegInfinity)
		, m_hOldHorse(nullptr)
		, m_horseAnimIdLastFrame(INVALID_ANIM_INSTANCE_ID)
		, m_skipUpdate(false)
		, m_isMoveStateChangeAllowed(false)
		, m_riderSettingsId(SID("*default-horse-npc-rider-settings*"))
		, m_switchSeatsGameFrame(0)
		, m_dismountPending(false)
		, m_pActionPackController(nullptr)
		, m_pAimStateMachine(nullptr)
		, m_isSteeringWithTwoHands(false)
		, m_lastSlowFromGallopTime(kTimeFrameNegInfinity)
		, m_blizzardSteeringRequestFrame(-1000)
		, m_waterBlockSteeringRequestFrame(-1000)
		, m_disableSystemicGesturesFrame(-1000)
		, m_disableCommandGesturesFrame(-1000)
		, m_twoHandSteeringRequestFrame(-1000)
		, m_oneHandSteeringRequestFrame(-1000)
		, m_dontExitIgcBeforeHorse(false)
		, m_backseatRagdollEnabled(false)
		, m_backseatIkEnabled(false)
		, m_needRefreshBackseatIk(false)
		, m_swapRiderPosPending(false)
		, m_mirrorSwapSeatsAnim(false)
		, m_needSwitchSeatsApFixup(false)
		, m_mountGameFrame(-1000)
		, m_jumpRagdollDelay(0.0f)
		, m_dismountBlocked(false)
	{
	}

	//virtual void Init(NdGameObject* pCharacter, const SimpleNavControl* pNavControl) override
	//{
	//	AnimActionController::Init(pCharacter, pNavControl);
	//	AllocateAnimLayers(pCharacter);
	//	m_pRiderSettings = ScriptPointer<const DC::HorseNpcRiderSettings>(m_riderSettingsId, kHorseRiderSettingsModule);
	//	ASSERT(m_pRiderSettings);
	//
	//	m_pActionPackController = NDI_NEW HorseActionPackController;
	//	m_pActionPackController->Init(pCharacter, pNavControl);
	//}

	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override
	{
		AnimActionController::Init(pNavChar, pNavControl);
		AllocateAnimLayers(pNavChar);

		Npc* pNpc = Npc::FromProcess(pNavChar);
		const StringId64 riderSettingsId = pNpc ? pNpc->GetHorseRiderSettingsId() : m_riderSettingsId;
		SetRiderSettings(riderSettingsId);
		GAMEPLAY_ASSERTF(m_pRiderSettings, ("Could not find rider settings %s for character %s in module %s",
						 DevKitOnly_StringIdToString(m_riderSettingsId),
						 DevKitOnly_StringIdToString(pNavChar->GetUserId()),
						 DevKitOnly_StringIdToString(kHorseRiderSettingsModule)));

		m_pActionPackController = NDI_NEW HorseActionPackController;
		m_pActionPackController->Init(pNavChar, pNavControl);

		m_pAimStateMachine = NDI_NEW HorseAimStateMachine;
		GAMEPLAY_ASSERT(m_pAimStateMachine);
		m_pAimStateMachine->Init(pNpc);

		RideHorseDualArmIk::InitJointTree(pNpc, m_followerJointTree);
	}

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override
	{
		ParentClass::Relocate(deltaPos, lowerBound, upperBound);
		DeepRelocatePointer(m_pActionPackController, deltaPos, lowerBound, upperBound);
		RelocatePointer(m_pAimStateMachine, deltaPos, lowerBound, upperBound);
		m_followerJointTree.Relocate(deltaPos, lowerBound, upperBound);
	}

	virtual ActionPackController* GetHorseActionPackController() override
	{
		return m_pActionPackController;
	}

	virtual const ActionPackController* GetHorseActionPackController() const override
	{
		return m_pActionPackController;
	}

	virtual HorseAimStateMachine* GetAimStateMachine() override
	{
		const Npc* pNpc = Npc::FromProcess(GetCharacter());
		if (!pNpc || !pNpc->ShouldUseHorseAimStateMachine())
			return nullptr;

		return m_pAimStateMachine;
	}

	virtual const HorseAimStateMachine* GetAimStateMachine() const override
	{
		const Npc* pNpc = Npc::FromProcess(GetCharacter());
		if (!pNpc || !pNpc->ShouldUseHorseAimStateMachine())
			return nullptr;

		return m_pAimStateMachine;
	}

	Locator GetMountAnimStartLocator(const Horse* pHorse, HorseDefines::RiderPosition riderPos, bool skipAnim) const
	{
		GAMEPLAY_ASSERT(pHorse);
		GAMEPLAY_ASSERT(riderPos == HorseDefines::kRiderFront || riderPos == HorseDefines::kRiderBack);
		if (skipAnim)
			return pHorse->GetRiderAttachLocator(riderPos);

		const bool leftSide = pHorse->BestMountSide(GetCharacter(), riderPos == HorseDefines::kRiderBack) != HorseClearance::kRightSide;
		const StringId64 animId = Horse::GetAnimFromRiderPosAndSide(riderPos, leftSide);

		bool success = false;
		BoundFrame entryRef = IVehicleCtrl::GetEntryReference_Generic(GetCharacter()->GetAnimControl(), animId, pHorse->GetRiderAttachLocator(riderPos), 0, false, false, &success);
		if (!success)
		{
			MsgConErr("Could not find horse mount entry reference for anim %s\n", DevKitOnly_StringIdToString(animId));
			return pHorse->GetRiderAttachLocator(riderPos);
		}

		return entryRef.GetLocator();
	}

	virtual void MountHorse(Horse *pHorse,
							HorseDefines::RiderPosition riderPos = HorseDefines::kRiderBack,
							bool skipAnim = false,
							bool teleport = false,
							Maybe<HorseClearance::HorseSide> forceSide = MAYBE::kNothing) override
	{
		m_swapRiderPosPending = false;
		if (riderPos != HorseDefines::kRiderFront)
		{
			riderPos = HorseDefines::kRiderBack;
		}

		// pHorse is null when we are dismounting
		if (pHorse)
		{
			if (teleport || (skipAnim && GetFadeToBlack() > 0.1f))
				GetCharacter()->TeleportToBoundFrame(BoundFrame(GetMountAnimStartLocator(pHorse, riderPos, skipAnim), pHorse->GetBinding()), NavLocation::Type::kNavPoly, true, false);
			else if (skipAnim)
				GetCharacter()->SetLocator(pHorse->GetRiderAttachLocator(riderPos));
		}

		if (m_hHorse.Valid())
		{
			if (m_hHorse.ToProcess() != pHorse)
			{
				UnhookOldHorse();
			}
			else if (m_riderPosition != riderPos)
			{
				SwitchSeats(riderPos, skipAnim);
			}
		}
		else
		{
			m_hHorse = pHorse;
			m_riderPosition = riderPos;
			if (pHorse)
			{
				MountHorseInternal(skipAnim, teleport, forceSide);
			}
		}
	}

	void OnDismount(float fadeInSec = 0.8f, DC::AnimCurveType curveType = DC::kAnimCurveTypeEaseIn)
	{
		m_dismountBlocked = false;
		Horse* pHorse = m_hHorse.ToMutableProcess();
		if (!pHorse)
			return;

		pHorse->PlayLoopingStirrupPartialAnim(kNoRiderStirrupAnim, fadeInSec, curveType);

		m_hOldHorse = m_hHorse;

		UpdateSprintLean(false);
		Cleanup();
		MountHorse(nullptr, m_riderPosition, true);

		DisableBackseatHandIk();
		DisableRagdoll();

		pHorse->StopAndStand(100.0f, FILE_LINE_FUNC);
	}

	// return value is whether or not we should suppress new animations playing
	bool DismountInternal()
	{
		GAMEPLAY_ASSERT(m_dismountPending);

		Horse* pHorse = m_hHorse.ToMutableProcess();
		if (!pHorse)
		{
			StopDismountSsAction();
			m_dismountPending = false;
			m_dismountBlocked = false;
			return false;
		}

		const bool inBackSeat = GetRiderPos() != HorseDefines::kRiderFront;
		const bool switchingSeats = IsSwitchingSeats();
		if (inBackSeat && switchingSeats)
		{
			// if we are in the back seat and are switching seats, that means we shifted to the back seat. Drop the request
			StopDismountSsAction();
			m_dismountPending = false;
			m_dismountBlocked = false;
			return false;
		}

		if (inBackSeat || switchingSeats)
		{
			if (!switchingSeats)
			{
				if (pHorse->HasRider(HorseDefines::kRiderFront))
				{
					// Someone got in the front seat, we won't be able to switch to the front seat to dismount. Drop the request.
					StopDismountSsAction();
					m_dismountPending = false;
					m_dismountBlocked = false;
				}
			}
			// delay dismounting if we are in the back seat or switching seats. we should be able to complete the request once we've finished switching to the front seat

			return false;
		}

		//if (!pHorse->GatherClearanceProbes(false, GetCharacter()))
		//{
		//	pHorse->KickClearanceProbes(false, GetCharacter());
		//	return false;
		//}


		const HorseClearance::HorseSide side = pHorse->BestDismountSide(GetCharacter(), GetRiderPos() == HorseDefines::kRiderBack);

		if (side == HorseClearance::kNoClearSide)
		{
			m_dismountBlocked = true;
			return false;
		}

		m_dismountBlocked = false;
		m_dismountPending = false;

		StringId64 dismountState = GetDismountAnimForSide(side);

		if (m_riderPosition == HorseDefines::kRiderFront)
			dismountState = StringId64Concat(dismountState, "-f");

		UpdateApRef();
		Character* pCharacter = GetCharacter();
		ASSERT(pCharacter);
		AnimControl* pAnimControl = pCharacter->GetAnimControl();
		ASSERT(pAnimControl);
		AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
		ASSERT(pBaseLayer);
		CONST_EXPR DC::AnimCurveType curveType = DC::kAnimCurveTypeEaseIn;
		CONST_EXPR float fadeInSec = 0.5f;

		DC::AnimNpcTopInfo* pTopInfo = pCharacter->GetAnimControl()->TopInfo<DC::AnimNpcTopInfo>();

		// we never dismount from the backseat
		pTopInfo->m_rideHorse.m_playedFromBackseat = false;

		const BoundFrame& apRef = m_riderPosition == HorseDefines::kRiderFront ? m_frontSeatBoundFrame : m_backSeatBoundFrame; //m_mountDismountBoundFrame;

		FadeToStateParams params;
		params.m_stateStartPhase = 0.0f;
		params.m_animFadeTime = fadeInSec;
		params.m_motionFadeTime = fadeInSec;
		params.m_allowStateLooping = false;
		params.m_apRef = apRef;
		params.m_apRefValid = true;
		params.m_blendType = curveType;
		GAMEPLAY_ASSERTF(IsDismountAnimState(dismountState), ("Anim state %s is not in the dismount anim list! Please add it to IsDismountAnimState (or tell Harold)", DevKitOnly_StringIdToString(dismountState)));

		DC::AnimCharacterInstanceInfo* pAnimInstanceInfo = pAnimControl->InstanceInfo<DC::AnimCharacterInstanceInfo>();

		const Quat apRot = apRef.GetRotation();
		const Binding apBinding = apRef.GetBinding();

		const Point groundPtWs = pHorse->GetClearanceController()->GetGroundPt(side, pCharacter, GetRiderPos() == HorseDefines::kRiderBack);
		BoundFrame apRef2 = BoundFrame(groundPtWs, apRot, apBinding);

		pAnimInstanceInfo->m_misc.m_doubleMotionFadeAp2 = apRef2;

		pBaseLayer->FadeToState(dismountState, params);

		TimeFrame ignoreUntil = GetProcessClock()->GetCurTime() + Seconds(3.0f);
		pHorse->GetMoveController()->SetWantIgnoreNavBlocker(pCharacter, ignoreUntil);

		OnDismount(fadeInSec, curveType);

		if (ILimbManager* pLimbManager = pCharacter->GetLimbManager())
		{
			LimbLockRequest request;
			request.m_limbs = (kLockArmR | kLockArmL);
			request.m_subsystem = SID("drive");

			m_limbLock = pLimbManager->GetLock(request);
		}

		Npc* pNpc = Npc::FromProcess(pCharacter);
		if (pNpc)
		{
			pNpc->m_dialogLook.DisableGesturesForOneFrame(pNpc->GetClock()->GetCurTime());
		}

		return true;
	}
	
	virtual bool IsDismountBlocked() const override
	{
		return m_dismountPending && m_dismountBlocked;
	}

	//scripts call this function to tell the npc to get off the horse
	virtual bool DismountHorse(bool skipAnim) override
	{
		const Horse* pHorse = m_hHorse.ToProcess();
		if (!pHorse)
			return false;

		if (skipAnim)
		{
			OnDismount();
			return true;
		}

		m_dismountPending = true;
		m_dismountBlocked = false;

		//pHorse->KickClearanceProbes(false, GetCharacter());

		return true;
	}

	void UpdateHandIkEnabled()
	{
		Character* pCharacter = GetCharacter();
		float blendTimeOverride = m_handIkRequest.GetFadeInTime();

		const bool ikEnabled = m_backseatIkEnabled;
		bool wantIk = ShouldEnableHandIk() || m_handIkRequest.ShouldEnableHandIk();

		bool leftHandAllowed = true;
		bool rightHandAllowed = true;
		bool flipTargets = false;

		HorseAimStateMachine* pHasm = GetAimStateMachine();
		if (pHasm)
		{
			pHasm->ShouldAllowHandIk(leftHandAllowed, rightHandAllowed);
			flipTargets = pHasm->ShouldFlipIkTargets();
		}

		if (!leftHandAllowed && !rightHandAllowed)
			wantIk = false;

		if (FALSE_IN_FINAL_BUILD(g_debugHorsePassengerRagdollAndIk && DebugSelection::Get().IsProcessOrNoneSelected(GetCharacter())))
		{
			MsgConPauseable("[%s] Backseat hand IK enabled?: %s%s%s\n   Left Hand?: %s%s%s\n  Right Hand?: %s%s%s\n  Flip Targets? %s%s%s\n",
							DevKitOnly_StringIdToString(GetCharacter()->GetUserId()),
							GetTextColorString(wantIk ? kTextColorGreen : kTextColorRed),
							wantIk ? "YES" : "NO",
							GetTextColorString(kTextColorNormal),
							GetTextColorString(leftHandAllowed ? kTextColorGreen : kTextColorRed),
							leftHandAllowed ? "YES" : "NO",
							GetTextColorString(kTextColorNormal),
							GetTextColorString(rightHandAllowed ? kTextColorGreen : kTextColorRed),
							rightHandAllowed ? "YES" : "NO",
							GetTextColorString(kTextColorNormal),
							GetTextColorString(flipTargets ? kTextColorGreen : kTextColorRed),
							flipTargets ? "YES" : "NO",
							GetTextColorString(kTextColorNormal));
		}

		if (ikEnabled != wantIk || m_needRefreshBackseatIk)
		{
			m_needRefreshBackseatIk = false;
			const DC::HorseRiderRagdollSettings* pSettings = GetRiderSettings().m_backseatRagdollSettings;
			if (wantIk)
				EnableBackseatHandIkInternal(pSettings, blendTimeOverride, leftHandAllowed, rightHandAllowed, flipTargets);
			else
				DisableBackseatHandIkInternal(pSettings);
		}

		if (m_backseatIkEnabled)
		{
			// don't keep it around
			m_handIkRequest.Clear();
		}
	}

	void UpdateDismountDoubleApRef() const
	{
		if (!IsDismounting())
			return;

		Horse* pHorse = GetHorse();
		if (!pHorse)
			pHorse = GetOldHorse();

		if (!pHorse)
			return;

		AnimStateInstance* pDismountAnimState = GetCharacter()->GetAnimControl()->GetBaseStateLayer()->CurrentStateInstance();
		if (!pDismountAnimState)
			return;

		if (IsDismountAnimState(pDismountAnimState->GetStateName()))
		{
			DC::AnimCharacterInstanceInfo* pInstanceInfo = static_cast<DC::AnimCharacterInstanceInfo*>(pDismountAnimState->GetAnimInstanceInfo());
			ANIM_ASSERT(pInstanceInfo);
			const HorseClearance::HorseSide side = pDismountAnimState->IsFlipped() ? HorseClearance::kRightSide : HorseClearance::kLeftSide;

			pInstanceInfo->m_misc.m_doubleMotionFadeAp2.SetTranslationWs(pHorse->GetClearanceController()->GetGroundPt(side, GetCharacter(), GetRiderPos() == HorseDefines::kRiderBack));
			//g_prim.Draw(DebugCross(pInstanceInfo->m_misc.m_doubleMotionFadeAp2.GetTranslationWs(), 1.0f, 2.0f, kColorMagenta, PrimAttrib(kPrimEnableHiddenLineAlpha)), kPrimDuration1FramePauseable);
		}

		pHorse->GetClearanceController()->RequestKick(GetCharacter(), false);
	}

	//npc calls this function to update animation state before processing animation update
	virtual void RequestAnimations() override
	{
		NavCharacter* pCharacter = GetCharacter();

		AnimControl* pAnimControl = pCharacter->GetAnimControl();
		ASSERT(pAnimControl);
		AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
		ASSERT(pBaseLayer);

		StringId64 curAnimState = pBaseLayer->CurrentStateId();
		Horse* pHorse = m_hHorse.ToMutableProcess();

		if (!IsRidingHorse())
		{
			NdSubsystem* pStirrupsIk = pCharacter->GetSubsystemMgr()->FindSubsystem(SID("CharacterStirrupsIk"));
			if (pStirrupsIk)
				pStirrupsIk->Kill();
		}

		if (!pHorse)
		{
			//keep updating ap for old horse if we are dismounting
			if (m_hOldHorse.HandleValid())
			{
				MoveNpcToHorse();
			}
			return;
		}

		if (pCharacter->IsInScriptedAnimationState())
			Cleanup(); //clear additives when in IGC

		if (pHorse->IsJumping())
		{
			const IHorseJump* pHorseJump = pHorse->GetJumpController()->GetActiveJump();
			const float jumpFade = pHorseJump ? pHorseJump->GetAnimFadeTime() : -1.0f;
			Cleanup(-1.0f, false);
		}
		else if (pHorse->IsUsingTraversalActionPack())
		{
			Cleanup(-1.0f, false);
		}

		if (pHorse->IsDead())
		{
			Cleanup();
			return;
		}

		if (!pCharacter->IsInScriptedAnimationState())
		{
			if (IsMounting() || IsDismounting())
			{
				if (m_limbLock.IsValid())
				{
					pCharacter->GetLimbManager()->HoldLock(m_limbLock);
				}

				Npc* pNpc = Npc::FromProcess(pCharacter);
				if (pNpc)
				{
					pNpc->m_dialogLook.DisableGesturesForOneFrame(pNpc->GetClock()->GetCurTime());
				}
			}

			UpdateDuck(pHorse, pAnimControl);
			UpdateCommandGestures();
			UpdateTurnDirection(GetHorse()->GetMoveController()->GetNpcControlledStickDir());
			UpdateSlopeLean(pHorse);

			if (IAiLocomotionController* pLocomotionController = pCharacter->GetAnimationControllers()->GetLocomotionController())
			{
				pLocomotionController->ConfigureWeaponUpDownOverlays(pLocomotionController->GetDesiredWeaponUpDownState(false));
			}
		}

		// we need to swap rider position on the first frame we actually have the switch seats animation playing
		if (m_swapRiderPosPending)
		{
			m_swapRiderPosPending = false;
			if (m_riderPosition == HorseDefines::kRiderFront)
				m_riderPosition = HorseDefines::kRiderBack;
			else
				m_riderPosition = HorseDefines::kRiderFront;
		}

		const AnimStateLayer* pHorseBaseLayer = pHorse->GetAnimControl()->GetBaseStateLayer();
		if (!pHorseBaseLayer)
			return;

		const AnimStateInstance* pHorseAnimInstance = pHorseBaseLayer->CurrentStateInstance();
		if (!pHorseAnimInstance)
			return;

		AnimInstance::ID horseAnimId = pHorseAnimInstance->GetId();

		if (m_pAimStateMachine)
			m_pAimStateMachine->Update();

		if (IsMounting() && pHorse->IsPlayerHorse())
		{
			const AnimStateInstance* pCurInstance = pBaseLayer->CurrentStateInstance();
			GAMEPLAY_ASSERT(pCurInstance);

			const StringId64 animStateId = pCurInstance->GetStateName();
			if (!IsMountAnimState(animStateId) || pCurInstance->GetPhase() < kFinishMountPhase)
				pHorse->StopAndWaitForNpcToMount();
		}

		//prevents skipping mount animation
		if (m_skipUpdate)
		{
			MoveNpcToHorse();
			m_skipUpdate = false;
			return;
		}

		if (m_swapSeatsRequest.IsPending())
		{
			SwitchSeatsFromRequest(m_swapSeatsRequest);
		}

		if (m_dismountPending)
		{
			if (DismountInternal())
			{
				MoveNpcToHorse();
				return;
			}
		}

		bool newAnimAllowed = UpdateTopInfo(pAnimControl, pHorse);
		
		bool doFinishMount = false;

		const bool noInterrupt = IsSwapSeatsPending() || IsNoInterruptAnim(curAnimState) || (GetAimStateMachine() && GetAimStateMachine()->IsActive());
		//we don't want to interrupt mount or dismount animations
		if (!noInterrupt)
		{
			//if player failed to mount horse and we're in the backseat, move back to the front seat
			if (m_riderPosition == HorseDefines::kRiderBack && !pHorse->HasRider(HorseDefines::kRiderFront))
			{
				Player* pPlayer = GetPlayer();
				if (!pPlayer || pPlayer->GetHorse() != pHorse)
					MountHorse(pHorse, HorseDefines::kRiderFront, false);
			}
			else
			{
				bool playNewAnim = horseAnimId != m_horseAnimIdLastFrame && newAnimAllowed;
				if (!m_dontExitIgcBeforeHorse)
					playNewAnim = playNewAnim || curAnimState != m_animState;

				bool isMmAnim = m_animState == SID("ride-horse-mm")/* || m_animState == SID("ride-horse-pistol-aim")*/;

				if (playNewAnim)
				{
					DC::AnimCharacterInstanceInfo* pInstanceInfo = static_cast<DC::AnimCharacterInstanceInfo*>(pAnimControl->GetInstanceInfo());
					pInstanceInfo->m_horse.m_horsePhase = pHorse->GetAnimPhase();
					pInstanceInfo->m_horse.m_horseAnimInstanceId = horseAnimId.GetValue();

					const AnimStateInstance* pHorseAnim = pHorseBaseLayer->CurrentStateInstance();

					F32 fadeIn = isMmAnim ? pHorseAnim->AnimFadeTime() : 0.4f;
					F32 motionFadeTime = isMmAnim ? pHorseAnim->MotionFadeTime() : fadeIn;
					DC::AnimCurveType blendCurve = isMmAnim ? pHorseAnim->GetFadeCurve() : DC::kAnimCurveTypeUniformS;
					FadeToStateParams::NewInstanceBehavior nib = FadeToStateParams::kUnspecified;

					if (IsKnownScriptedAnimationState(curAnimState))
					{
						if (const SsAnimateController* pScriptController = pCharacter->GetPrimarySsAnimateController())
						{
							const SsAnimateParams& igcParams = pScriptController->GetParams();
							fadeIn = motionFadeTime = igcParams.m_fadeOutSec;
							blendCurve = igcParams.m_fadeOutCurve;
							nib = FadeToStateParams::kSpawnNewTrack;
						}
					}

					StartNewAnim(pBaseLayer, pHorse, true, fadeIn, motionFadeTime, blendCurve, nib);
				}
			}
		}
		else if (IsSwitchSeatsAnimState(curAnimState))
		{
			const DC::HorseNpcRiderSettings& riderSettings = GetRiderSettings();
			const float endPhase = riderSettings.m_swapSeatsEndPhase;
			doFinishMount = pBaseLayer->CurrentStateInstance()->GetPhase() > endPhase;
		}
		else if (IsMountAnimState(curAnimState))
		{
			const DC::MountAnimFadeSettings* pFadeSettings = GetMountFadeSettingsForAnim(curAnimState);
			const float endPhase = pFadeSettings ? pFadeSettings->m_endPhase : 0.7f;
			doFinishMount = pBaseLayer->CurrentStateInstance()->GetPhase() > endPhase;
		}

		if (doFinishMount)
		{
			bool newAnim = curAnimState != m_animState;
			if (newAnim)
			{
				const DC::MountAnimFadeSettings* pFadeSettings = GetMountFadeSettingsForAnim(curAnimState);
				const float fadeTime = pFadeSettings ? pFadeSettings->m_fadeOutTime : 0.4f;
				StartNewAnim(pBaseLayer, pHorse, true, fadeTime, fadeTime);
				DC::AnimCharacterInstanceInfo* pInstanceInfo = static_cast<DC::AnimCharacterInstanceInfo*>(pAnimControl->GetInstanceInfo());
				pInstanceInfo->m_horse.m_horsePhase = pHorse->GetAnimPhase();
				pInstanceInfo->m_horse.m_horseAnimInstanceId = horseAnimId.GetValue();
			}
		}

		//update instance info
		const DC::HorseNpcRiderSettings& riderSettings = GetRiderSettings();
		int framesBehind = riderSettings.m_backSeatFrameDelay;
		if (m_riderPosition == HorseDefines::kRiderFront || g_disableHorseRiderPhaseOffset)
			framesBehind = 0;

		CONST_EXPR U8 kMaxNoOffsetAnims = 32;

		StringId64 noOffsetAnims[kMaxNoOffsetAnims];
		int noOffsetAnimCount = GetNoFrameOffsetAnims(noOffsetAnims, kMaxNoOffsetAnims);

		UpdateRideHorseInfoParams params(pHorseBaseLayer, framesBehind, noOffsetAnims, noOffsetAnimCount);
		params.m_preventAllFlips = !CanFlip();
		params.m_isNpc = true;
		pAnimControl->GetBaseStateLayer()->WalkInstancesNewToOld(&UpdateRideHorseInstanceInfoFunc, (uintptr_t)(&params));

		//update bound frame of non mount/dismount anims to keep up with horse
		MoveNpcToHorse();
		m_horseAnimIdLastFrame = horseAnimId;
	}

	//npc calls this function after selecting animation
	virtual void UpdateStatus() override
	{
		UpdateSsWaits();
		UpdateRagdollEnabled();
	}

	virtual void SetNewMoveState(HorseDefines::MoveState newState, HorseDefines::MoveState oldState, bool inFrontSeat) override
	{
		ASSERT(newState != oldState);

		const bool isSprinting = newState >= HorseDefines::kMoveStateGallopSprint;

		UpdateSprintLean(isSprinting);


		Character* pCharacter = GetCharacter();
		Horse* pHorse = GetHorse();
		if (!pHorse)
			return;

		if (!inFrontSeat)
			return;

		if (!IsDrivingHorse(false))
			return;

		const bool wasGalloping = oldState >= HorseDefines::kMoveStateGallop;
		const bool isGalloping = newState >= HorseDefines::kMoveStateGallop;

		if (isGalloping)
		{
			m_lastSlowFromGallopTime = TimeFrameNegInfinity();
		}
		else if (wasGalloping)
		{
			m_lastSlowFromGallopTime = GetProcessClock()->GetCurTime();
		}

		StringId64 commandGestureId = INVALID_STRING_ID_64;
		if (pHorse->GetMoveController()->IsNpcStickBraking())
		{
			commandGestureId = SID("ellie-command-horse-slow-gesture");
		}
		else if (newState >= oldState)
		{
			switch (newState)
			{
			case HorseDefines::kMoveStateCanter:
				commandGestureId = SID("ellie-command-horse-canter-gesture");
				break;
			case HorseDefines::kMoveStateGallop:
			case HorseDefines::kMoveStateGallopSprint:
				commandGestureId = SID("ellie-command-horse-gallop-gesture");
				break;
			case HorseDefines::kMoveStateWalk:
			case HorseDefines::kMoveStateTrot:
				commandGestureId = SID("ellie-command-horse-walk-gesture");
				break;
			}
		}

		if (commandGestureId != INVALID_STRING_ID_64)
		{
			PlayCommandGesture(commandGestureId);
		}
	}

	virtual bool IsMoveStateChangeAllowed() const override
	{
		return m_isMoveStateChangeAllowed;
	}

	//after animations have all been processed
	virtual void PostRootLocatorUpdate() override
	{
	}

	void UpdateStirrups()
	{
		Horse* pHorse = m_hHorse.ToMutableProcess();
		// eventually we will want this once I get stirrups IK to work with dismounting characters
		if (!pHorse && IsDismounting())
			pHorse = m_hOldHorse.ToMutableProcess();

		if (pHorse && (m_riderPosition == HorseDefines::kRiderFront || IsSwitchingSeats()))
		{
			Character* pSelf = GetCharacter();
			HorseStirrupsController* pStirrupsController = pHorse->GetStirrupsController();

			if (!pStirrupsController)
				return;

			bool needRealRiderLoc = IsMounting() || IsDismounting() || IsSwitchingSeats();

			bool doIk = false;
			if (g_disableHorseStirrupIkDuringMountDismount)
			{
				doIk = !needRealRiderLoc;
			}
			else
			{
				doIk = true;
			}

			if (doIk)
				pStirrupsController->RiderStirrupsUpdate(pSelf, pHorse, needRealRiderLoc);
			else
				pStirrupsController->RiderDisableIk();

			pStirrupsController->RiderCacheFinalStirrupLocs(pHorse);

			CharacterStirrupsIk* pStirrupsIk = static_cast<CharacterStirrupsIk*>(pSelf->GetSubsystemMgr()->FindSubsystem(SID("CharacterStirrupsIk")));
			if (pStirrupsIk && pStirrupsIk->IsAlive() && !pStirrupsController->IsDisabled())
			{
				{
					Locator leftStirrupDeltaLoc;
					Locator rightStirrupDeltaLoc;

					bool useRealRiderLoc = IsMounting() || IsDismounting() || IsSwitchingSeats();

					pStirrupsController->GetDeltaLocators(pSelf, pHorse, useRealRiderLoc, leftStirrupDeltaLoc, rightStirrupDeltaLoc);

					const float leftRiderBlend = pStirrupsController->GetLegToStirrupBlend(kLeftStirrup);
					const float rightRiderBlend = pStirrupsController->GetLegToStirrupBlend(kRightStirrup);

					pStirrupsIk->AdjustFeetToStirrups(leftStirrupDeltaLoc, rightStirrupDeltaLoc, leftRiderBlend, rightRiderBlend);
				}
			}
		}
	}

	virtual void UpdateProcedural() override
	{
		UpdateStirrups();
		ParentClass::UpdateProcedural();
	}

	virtual bool IsRidingHorse(bool allowMounting = true, bool allowDismounting = true) const override
	{
		const bool horseHandleValid = m_hHorse.HandleValid();
		const StringId64 currState = GetCurrentAnimState();
		const bool isIdleAnim = IsIdleAnim(currState);
		const bool knownHorseRiderAnim = currState == SID("ride-horse-mm") || currState == SID("ride-horse-no-sync");
		const bool isMountAnim = allowMounting && IsMountAnimState(currState);
		const bool isDismountAnim = allowDismounting && IsDismountAnimState(currState);
		const bool result = horseHandleValid || isIdleAnim || isMountAnim || isDismountAnim || knownHorseRiderAnim;
		return result;
	}

	virtual Horse* GetHorse() const override
	{
		return m_hHorse.ToMutableProcess();
	}

	virtual Horse* GetOldHorse() const override
	{
		return m_hOldHorse.ToMutableProcess();
	}

	virtual bool IsMounting() const override
	{
		bool horseHandleValid = m_hHorse.HandleValid();
		StringId64 currState = GetCurrentAnimState();
		bool isMountAnim = IsMountAnimState(currState) || EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused <= m_mountGameFrame;
		return isMountAnim && horseHandleValid;
	}

	virtual bool IsDismounting() const override
	{
		StringId64 currState = GetCurrentAnimState();
		return IsDismountAnimState(currState);
	}

	virtual const TimeFrame GetActionPackEnterTime() const override
	{
		return m_apEnterTime;
	}

	virtual bool IsBusy() const override
	{
		return IsRidingHorse();
	}
	virtual void OnIgcBind(float fadeInTime, bool allowRagdoll = false) override
	{
		DisableBackseatHandIk(fadeInTime);
		if (!allowRagdoll)
			DisableRagdoll(fadeInTime);
	}
	virtual void OnIgcUnbind(float fadeOutTime) override
	{
		if (ShouldRideAsRagdoll())
		{
			EnableRagdoll(fadeOutTime);
		}
		if (ShouldEnableHandIk())
		{
			EnableBackseatHandIk(fadeOutTime);
		}
	}

	virtual bool ShouldInterruptNavigation() const override
	{
		return IsRidingHorse();
	}

	virtual HorseDefines::RiderPosition GetRiderPos() const override
	{
		return m_riderPosition;
	}

	virtual bool WaitDismountHorse(SsTrackGroupInstance* pGroupInst, int trackIndex) override
	{
		if (!m_hHorse.HandleValid())
			return false;

		if (pGroupInst && pGroupInst->GetTrackGroupProcessHandle().HandleValid() && trackIndex >= 0)
		{
			if (DismountHorse(false))
			{
				m_dismountSsAction.Start(pGroupInst->GetTrackGroupProcessHandle(), trackIndex);
				return true;
			}
		}
		return false;
	}

	virtual bool WaitMountHorse(SsTrackGroupInstance* pGroupInst, int trackIndex, EnterHorseArgs enterArgs) override
	{
		if (pGroupInst && pGroupInst->GetTrackGroupProcessHandle().HandleValid() && trackIndex >= 0)
		{
			Npc* pNpc = Npc::FromProcess(GetCharacter());
			GAMEPLAY_ASSERT(pNpc);
			Horse* pHorse = enterArgs.m_hHorse.ToMutableProcess();
			GAMEPLAY_ASSERT(pHorse);
			pHorse->GetClearanceController()->RequestKick(pNpc, enterArgs.m_riderPos == HorseDefines::kRiderBack);
			pNpc->ScriptCommandMoveToAndEnterHorse(pGroupInst, trackIndex, enterArgs);
			return true;
		}
		return false;
	}

	virtual void SetRiderSettings(StringId64 settingsId) override
	{
		m_riderSettingsId = settingsId;
		m_pRiderSettings = ScriptPointer<const DC::HorseNpcRiderSettings>(m_riderSettingsId, kHorseRiderSettingsModule);
		m_handIkRequest.Clear(); // don't hold onto old requests
	}

	StringId64 GetRiderSettingsId() const
	{
		return m_riderSettingsId;
	}

	const DC::HorseNpcRiderSettings& GetRiderSettings() const
	{
		ASSERT(m_pRiderSettings);
		return *m_pRiderSettings;
	}

	bool IsSwapSeatsPending() const
	{
		return m_swapRiderPosPending;
	}

	bool IsNoFrameOffsetAnim(StringId64 animName) const
	{
		const DC::HorseNpcRiderSettings& settings = GetRiderSettings();
		for (int i = 0; i < settings.m_backSeatAnimRemaps->m_count; ++i)
		{
			if (settings.m_backSeatAnimRemaps->m_array[i].m_newAnim == animName)
				return true;
		}
		return false;
	}

	int GetNoFrameOffsetAnims(StringId64 aOut[], int capacity) const
	{
		int count = 0;
		const DC::HorseNpcRiderSettings& settings = GetRiderSettings();
		if (!settings.m_backSeatAnimRemaps)
			return count;

		for (int i = 0; i < settings.m_backSeatAnimRemaps->m_count; ++i)
		{
			StringId64 anim = settings.m_backSeatAnimRemaps->m_array[i].m_newAnim;
			GAMEPLAY_ASSERT(count < capacity);
			aOut[count++] = anim;
		}
		return count;
	}

	bool SwappedSeatsThisFrame() const
	{
		I64 gameFrameUnpaused = EngineComponents::GetFrameState()->m_gameFrameNumberUnpaused;
		return gameFrameUnpaused == m_switchSeatsGameFrame;
	}

	bool NeedSwapSeatsFixup() const
	{
		return SwappedSeatsThisFrame() && m_needSwitchSeatsApFixup;
	}

	virtual BoundFrame GetSaddleBoundFrame(bool& outIsValid) const override
	{
		outIsValid = false;
		const Horse* pHorse = m_hHorse.ToProcess();
		if (!pHorse)
			pHorse = m_hOldHorse.ToProcess();
		if (!pHorse)
			return BoundFrame();

		outIsValid = true;
		HorseDefines::RiderPosition riderPos = GetRiderPos();
		return pHorse->GetRiderAttachBoundFrame(riderPos);
	}

	virtual void RequestDontExitIgcBeforeHorse() override
	{
		ANIM_ASSERTF(GetHorse() != nullptr, ("%s: Can't use (animate-arg dont-exit-before-horse) if the character is not yet riding a horse! You should set them to ride a horse before calling animate", DevKitOnly_StringIdToString(GetCharacter()->GetUserId())));
		m_dontExitIgcBeforeHorse = true;
	}

	virtual void PostRenderUpdate() override
	{
		if (g_dmenuMgr.IsProgPaused())
			return;

		if (!IsRidingHorse(false, false))
			return;

		UpdateHandIkEnabled();
	}
};

IAiRideHorseController* CreateAiRideHorseController()
{
	return NDI_NEW AiRideHorseController;
}

static bool IsMountAnimCb(const AnimStateInstance* pInstance, const AnimStateLayer* pStateLayer, uintptr_t userData)
{
	bool* pResult = reinterpret_cast<bool*>(userData);
	const StringId64 animState = pInstance->GetStateName();

	if (AiRideHorseController::IsMountAnimState(animState))
	{
		*pResult = true;
		return false; // don't keep looking
	}

	return true;
}

//--------------------------------------------------------------------------------------
// Anim Actions
//--------------------------------------------------------------------------------------

void NpcDismountHorseAnimAction::InstanceDestroy(AnimStateInstance* pInst)
{
	ParentClass::InstanceDestroy(pInst);

	Character* pChar = GetOwnerCharacter();
	if (pChar)
	{
		const Horse* pHorse = m_hHorse.ToProcess();
		if (!pHorse || (pHorse->GetCharacterRider(HorseDefines::kRiderFront) != pChar && pHorse->GetCharacterRider(HorseDefines::kRiderBack) != pChar))
			pChar->EnableNavBlocker(true);
	}
}

void NpcDismountHorseAnimAction::InstanceReplace(const DC::AnimState* pAnimState, FadeToStateParams* pParams)
{
	ParentClass::InstanceReplace(pAnimState, pParams);

	Npc* pNpc = Npc::FromProcess(GetOwnerCharacter());

	if (pNpc)
	{
		AiRideHorseController* pRideHorse = static_cast<AiRideHorseController*>(pNpc->GetAnimationControllers()->GetRideHorseController());
		pRideHorse->StopDismountSsAction(); // make sure we don't get stuck waiting
	}
}

// replaced by CharacterDoubleApMotionFade
//float NpcDismountHorseAnimAction::GetGroundAdjustFactor(const AnimStateInstance* pInstance, float desired) const
//{
//	float startLandPhase = 0.0f;
//	float finishLandPhase = 1.0f;
//
//	switch (pInstance->GetStateName().GetValue())
//	{
//	case SID_VAL("s_ride-horse-dismount-left"):
//		// these are never used
//		startLandPhase = 0.45f;
//		finishLandPhase = 0.615f;
//		break;
//	case SID_VAL("s_ride-horse-dismount-right"):
//		// these are never used
//		startLandPhase = 0.35f;
//		finishLandPhase = 0.52f;
//		break;
//	case SID_VAL("s_ride-horse-dismount-left-f"):
//		startLandPhase = 0.45f;
//		finishLandPhase = 0.615f;
//		break;
//	case SID_VAL("s_ride-horse-dismount-right-f"):
//		startLandPhase = 0.35f;
//		finishLandPhase = 0.52f;
//		break;
//	default:
//		return desired;
//	}
//
//	const float phase = pInstance->Phase();
//	const float result = LerpScaleClamp(startLandPhase, finishLandPhase, 0.0f, 1.0f, phase);
//	return result;
//}

SUBSYSTEM_UPDATE_DEF(NpcDismountHorseAnimAction, Update)
{
	SUBSYSTEM_PARENT_UPDATE(Update);

	Character* pChar = GetOwnerCharacter();
	if (!pChar)
		return;

	Npc* pNpc = Npc::FromProcess(pChar);
	if (pNpc)
	{
		AiRideHorseController* pRideHorse = static_cast<AiRideHorseController*>(pNpc->GetAnimationControllers()->GetRideHorseController());
		pRideHorse->UpdateDismountDoubleApRef();
	}

	ICharacterLegIkController* pLegIkController = pChar->GetLegIkController();
	if (!pLegIkController)
		return;

	AnimStateInstance* pBoundTopInstance = GetTopBoundInstance(); // nullptr if not top anim
	if (pBoundTopInstance)
	{
		if (pBoundTopInstance->GetPhase() >= 0.5f)
		{
			pLegIkController->EnableMovingIK(0.2f);
		}
		//if (pBoundTopInstance == pChar->GetAnimControl()->GetBaseStateLayer()->CurrentStateInstance())
		//{
		//	pLegIkController->DisableIK(0.0f);
		//}
		//else
		//{
		//	pLegIkController->EnableMovingIK(0.05f);
		//}
	}

}
TYPE_FACTORY_REGISTER(NpcDismountHorseAnimAction, NpcRideHorseAnimAction);

//--------------------------------------------------------------------------------------
SUBSYSTEM_UPDATE_DEF(NpcRideHorseAnimAction, PostAnimUpdate)
{
	SUBSYSTEM_PARENT_UPDATE(PostAnimUpdate);

	Npc* pNpc = Npc::FromProcess(GetOwnerCharacter());
	if (!pNpc)
		return;

	AiRideHorseController* pRideHorse = static_cast<AiRideHorseController*>(pNpc->GetAnimationControllers()->GetRideHorseController());

	const Horse* pHorse = m_hHorse.ToProcess();
	if (!pHorse)
	{
		m_hHorse = pRideHorse->GetHorse();
		pHorse = m_hHorse.ToProcess();
	}

	if (!pHorse)
		return;

	const BoundFrame frontSeatAp = pHorse->GetRiderAttachBoundFrame(HorseDefines::kRiderFront);
	const BoundFrame backSeatAp = pHorse->GetRiderAttachBoundFrame(HorseDefines::kRiderBack);

	const DC::HorseNpcRiderSettings& riderSettings = pRideHorse->GetRiderSettings();
	float apFixStrength = riderSettings.m_noSyncApRotFixStrength;
	AnimStateLayer* pBaseLayer = pNpc->GetAnimControl()->GetBaseStateLayer();
	UpdateRideHorseApParams params = UpdateRideHorseApParams(frontSeatAp, backSeatAp, apFixStrength);
	pBaseLayer->WalkInstancesNewToOld(UpdateRideHorseApFunc, (uintptr_t)&params);
}

//--------------------------------------------------------------------------------------
HorseDefines::RiderPosition NpcRideHorseAnimAction::GetRiderPos() const
{
	const Npc* pNpc = Npc::FromProcess(GetOwnerCharacter());
	const AiRideHorseController* pRideHorse = static_cast<const AiRideHorseController*>(pNpc->GetAnimationControllers()->GetRideHorseController());
	HorseDefines::RiderPosition riderPos = pRideHorse->GetRiderPos();
	return riderPos;
}

//--------------------------------------------------------------------------------------
Err NpcRideHorseAnimAction::Init(const SubsystemSpawnInfo& info)
{
	Err parentResult = ParentClass::Init(info);
	if (!parentResult.Succeeded())
	{
		return parentResult;
	}

	const Npc* pNpc = Npc::FromProcess(GetOwnerCharacter());
	GAMEPLAY_ASSERT(pNpc);
	if (!pNpc)
		return Err::kErrAbort;

	m_hHorse = pNpc->GetAnimationControllers()->GetRideHorseController()->GetHorse();
	return parentResult;
}
TYPE_FACTORY_REGISTER(NpcRideHorseAnimAction, NdSubsystemAnimAction);


//--------------------------------------------------------------------------------------
SUBSYSTEM_UPDATE_DEF(NpcIgcRideHorseAnimAction, PostAnimUpdate)
{
	SUBSYSTEM_PARENT_UPDATE(PostAnimUpdate);

	Npc* pNpc = Npc::FromProcess(GetOwnerCharacter());
	if (!pNpc)
		return;

	const Horse* pHorse = m_hHorse.ToProcess();
	if (!pHorse)
	{
		m_hHorse = pNpc->GetAnimationControllers()->GetRideHorseController()->GetHorse();
		pHorse = m_hHorse.ToProcess();
	}

	if (!pHorse)
		return;

	AnimStateInstance* pBoundInstance = GetTopBoundInstance();
	if (pBoundInstance)
	{
		const HorseDefines::RiderPosition riderPos = GetRiderPos();
		const BoundFrame apRef = pHorse->GetRiderAttachBoundFrame(riderPos);
		pBoundInstance->SetApLocator(apRef);
	}
}
TYPE_FACTORY_REGISTER(NpcIgcRideHorseAnimAction, NpcRideHorseAnimAction);



//--------------------------------------------------------------------------------------
Err NpcDieOnHorseAnimAction::Init(const SubsystemSpawnInfo& info)
{
	Err parentResult = ParentClass::Init(info);
	if (!parentResult.Succeeded())
	{
		return parentResult;
	}

	Npc* pNpc = Npc::FromProcess(GetOwnerCharacter());
	GAMEPLAY_ASSERT(pNpc);

	m_riderPos = pNpc->GetAnimationControllers()->GetRideHorseController()->GetRiderPos();

	const Horse* pHorse = m_hHorse.ToProcess();
	GAMEPLAY_ASSERT(pHorse);

	m_horsePrevYWs = pHorse->GetTranslation().Y();

	// this anim action used for both death animations from the horse dying and for when the NPC dies while riding the horse.
	// this block is only used when the horse dies
	const StringId64 horseDeathAnim = pHorse->GetDeathAnim();
	if (horseDeathAnim != INVALID_STRING_ID_64)
	{
		DC::AnimNpcTopInfo* pTopInfo = GetOwnerCharacter()->GetAnimControl()->TopInfo<DC::AnimNpcTopInfo>();


		const StringId64 npcAnim = StringId64Concat(horseDeathAnim, "--npc");
		pTopInfo->m_rideHorse.m_animMm = npcAnim;

		const bool flipped = pHorse->IsDeathAnimFlipped();
		pTopInfo->m_rideHorse.m_flip = flipped;
	}

	return parentResult;
}

//--------------------------------------------------------------------------------------
void NpcDieOnHorseAnimAction::InstanceCreate(AnimStateInstance* pInst)
{
	ParentClass::InstanceCreate(pInst);

	const Horse* pHorse = m_hHorse.ToProcess();
	GAMEPLAY_ASSERT(pHorse);

	// this anim action used for both death animations from the horse dying and for when the NPC dies while riding the horse.
	// this block is only used when the horse dies
	const StringId64 horseDeathAnim = pHorse->GetDeathAnim();
	if (horseDeathAnim == INVALID_STRING_ID_64)
		return;

	DC::AnimNpcTopInfo* pTopInfo = GetOwnerCharacter()->GetAnimControl()->TopInfo<DC::AnimNpcTopInfo>();

	const StringId64 npcAnim = StringId64Concat(horseDeathAnim, "--npc");
	pTopInfo->m_rideHorse.m_animMm = npcAnim;

	const bool flipped = pHorse->IsDeathAnimFlipped();
	pTopInfo->m_rideHorse.m_flip = flipped;

	DC::AnimCharacterInstanceInfo* pInstanceInfo = static_cast<DC::AnimCharacterInstanceInfo*>(pInst->GetAnimInstanceInfo());
	GAMEPLAY_ASSERT(pInstanceInfo);

	const AnimStateLayer* pBaseLayer = pHorse->GetAnimControl()->GetBaseStateLayer();
	GAMEPLAY_ASSERT(pBaseLayer);

	const AnimStateInstance* pHorseDeathAnim = pBaseLayer->FindInstanceByNameNewToOld(SID("s_death-state"));

	if (pHorseDeathAnim)
	{
		pInstanceInfo->m_horse.m_horsePhase = pHorseDeathAnim->Phase();
	}
	else
	{
		pInstanceInfo->m_horse.m_horsePhase = 0.0f;
	}

	const BoundFrame ap = pHorse->GetRiderAttachBoundFrame(m_riderPos);
	pInst->SetApLocator(ap);


#ifndef FINAL_BUILD
	MsgAnim("Npc %s is dying from horseback with death anim %s %s\n",
		DevKitOnly_StringIdToString(GetOwnerCharacter()->GetUserId()),
		DevKitOnly_StringIdToString(npcAnim),
		flipped ? "[flipped]" : "");
#endif

}

//--------------------------------------------------------------------------------------
void NpcDieOnHorseAnimAction::InstancePrepare(const DC::AnimState* pAnimState, FadeToStateParams* pParams)
{
	ParentClass::InstancePrepare(pAnimState, pParams);

	const Horse* pHorse = m_hHorse.ToProcess();
	GAMEPLAY_ASSERT(pHorse);

	const BoundFrame ap = pHorse->GetRiderAttachBoundFrame(m_riderPos);
	pParams->m_apRef = ap;
	pParams->m_apRefValid = true;
}


//--------------------------------------------------------------------------------------
SUBSYSTEM_UPDATE_DEF(NpcDieOnHorseAnimAction, Update)
{
	const Horse* pHorse = m_hHorse.ToProcess();
	if (!pHorse)
	{
		// horse may have been destroyed, just stop animating. If this isn't sufficient we can cache the previous frames update rate and keep using that to update our phase
		SUBSYSTEM_PARENT_UPDATE(Update);
		return;
	}

	AnimStateInstance* pBoundInstance = GetTopBoundInstance();
	GAMEPLAY_ASSERT(pBoundInstance);

	const bool shouldUpdateApYOnly = pBoundInstance->GetStateName() == SID("s_synced-death-on-horse-state");;

	DC::AnimCharacterInstanceInfo* pInstanceInfo = static_cast<DC::AnimCharacterInstanceInfo*>(pBoundInstance->GetAnimInstanceInfo());
	GAMEPLAY_ASSERT(pInstanceInfo);

	const AnimStateLayer* pBaseLayer = pHorse->GetAnimControl()->GetBaseStateLayer();
	GAMEPLAY_ASSERT(pBaseLayer);

	const AnimStateInstance* pHorseDeathAnim = pBaseLayer->FindInstanceByNameNewToOld(SID("s_death-state"));
#ifdef HTOWNSEND
	GAMEPLAY_ASSERT(pHorseDeathAnim || !shouldUpdateApYOnly);
#endif

	if (pHorseDeathAnim)
	{
		pInstanceInfo->m_horse.m_horsePhase = pHorseDeathAnim->Phase();
	}

	if (shouldUpdateApYOnly)
	{
		if (pBoundInstance->IsApLocatorActive())
		{
			// Account for terrain slope in bucked death reaction
			// Yeah. This is a hack.
			const F32 yDelta	  = pHorse->GetTranslation().Y() - m_horsePrevYWs;
			BoundFrame adjustedAp = pBoundInstance->GetApLocator();
			adjustedAp.AdjustTranslation(Vector(0.0f, yDelta, 0.0f));
			m_horsePrevYWs += yDelta;
			pBoundInstance->SetApLocator(adjustedAp);
		}
	}
	else
	{
		const BoundFrame ap = pHorse->GetRiderAttachBoundFrame(m_riderPos);
		pBoundInstance->SetApLocator(ap);
	}

	SUBSYSTEM_PARENT_UPDATE(Update);
}
TYPE_FACTORY_REGISTER(NpcDieOnHorseAnimAction, NpcRideHorseAnimAction);



//--------------------------------------------------------------------------------------
Err BindToHorseAnimAction::Init(const SubsystemSpawnInfo& info)
{
	Err parentResult = ParentClass::Init(info);
	if (!parentResult.Succeeded())
		return parentResult;

	GAMEPLAY_ASSERT(info.m_pUserData);

	const DC::HorseBinding* pHorseBinding = PunPtr<const DC::HorseBinding*>(info.m_pUserData);

	Horse* pHorse = GetHorseFromSymbol(pHorseBinding->m_horseId);
	GAMEPLAY_ASSERTF(pHorse, ("Could not find horse %s", DevKitOnly_StringIdToString(pHorseBinding->m_horseId)));
	m_hHorse = pHorse;

	const DC::HorseRiderPosition dcRiderPos = pHorseBinding->m_riderPosition;
	if (dcRiderPos == DC::kHorseRiderPositionFront)
		m_attachRiderPos = HorseDefines::kRiderFront;
	else if (dcRiderPos == DC::kHorseRiderPositionBack)
		m_attachRiderPos = HorseDefines::kRiderBack;
	else
		m_attachRiderPos = HorseDefines::kRiderCount; // use align as apRef

	return parentResult;
}

//--------------------------------------------------------------------------------------
void BindToHorseAnimAction::InstancePrepare(const DC::AnimState* pAnimState, FadeToStateParams* pParams)
{
	ParentClass::InstancePrepare(pAnimState, pParams);

	pParams->m_apRef = GetBoundFrame();
}


//--------------------------------------------------------------------------------------
void BindToHorseAnimAction::InstanceCreate(AnimStateInstance* pInst)
{
	ParentClass::InstanceCreate(pInst);

	pInst->SetApLocator(GetBoundFrame());
}

//--------------------------------------------------------------------------------------
void BindToHorseAnimAction::SubsystemUpdateMacro()
{
	ParentClass::SubsystemUpdateMacro();

	// could happen if the horse disappears
	if (!m_hHorse.HandleValid())
		return;

	AnimStateInstance* pInstance = GetTopBoundInstance();
	if (!pInstance)
		return;

	const BoundFrame apRef = GetBoundFrame();
	pInstance->SetApLocator(apRef);
}

//--------------------------------------------------------------------------------------
BoundFrame BindToHorseAnimAction::GetBoundFrame() const
{
	GAMEPLAY_ASSERT(m_hHorse.HandleValid());

	const Horse* pHorse = Horse::FromProcess(m_hHorse.ToProcess());
	if (m_attachRiderPos != HorseDefines::kRiderCount)
	{
		return pHorse->GetRiderAttachBoundFrame(m_attachRiderPos);
	}
	else
	{
		return pHorse->GetBoundFrame();
	}
}
TYPE_FACTORY_REGISTER(BindToHorseAnimAction, NdSubsystemAnimAction);


//--------------------------------------------------------------------------------------
// Horse Aim State Machine
//--------------------------------------------------------------------------------------
Color ColorFromAimState(DC::HorseAimState state)
{
	switch (state)
	{
	case DC::kHorseAimStateWeaponHolsteredIdle:
		return kColorGray;
	case DC::kHorseAimStateWeaponOutIdleLeft:
		return kColorWhite;
	case DC::kHorseAimStateWeaponOutIdleRight:
		return kColorOrange;
	case DC::kHorseAimStateAimLeft:
		return kColorMagenta;
	case DC::kHorseAimStateAimRight:
		return kColorCyan;
	default:
		return kColorWhite;
	}
}

//--------------------------------------------------------------------------------------
void HorseAimStateMachine::Init(Npc* pNpc)
{
	GAMEPLAY_ASSERT(pNpc);
	m_hNpc = pNpc;
	m_state = DC::kHorseAimStateWeaponHolsteredIdle;
	m_nextAllowedSwitchSidesTime = kTimeFrameNegInfinity;
	m_disableSwitchSidesTimeout = false;
	//m_weaponDownRequestTime = kTimeFrameNegInfinity;
}

//--------------------------------------------------------------------------------------
void HorseAimStateMachine::OnWeaponDown()
{
	if (IsActive())
	{
		const DC::HorseAimState desiredState = IsRightSide() ? DC::kHorseAimStateWeaponOutIdleRight : DC::kHorseAimStateWeaponOutIdleLeft;
		GoState(desiredState, FILE_LINE_FUNC);
	}
}

//--------------------------------------------------------------------------------------
bool HorseAimStateMachine::SetEnabled(bool enabled, const char* file, int line, const char* func)
{
	if (enabled)
	{
		// do nothing if we already have our weapon up
		if (m_state != DC::kHorseAimStateWeaponHolsteredIdle)
			return false;

		const Npc* pNpc = m_hNpc.ToProcess();
		GAMEPLAY_ASSERT(pNpc);

		DC::HorseAimState newState = IsRightSide() ? DC::kHorseAimStateWeaponOutIdleRight : DC::kHorseAimStateWeaponOutIdleLeft;
		if (!ShouldNotAim())
		{
			const Locator loc = pNpc->GetLocator();
			const Vector left = AsUnitVectorXz(GetLocalX(loc), kUnitXAxis);

			const Point aimAtPosition = pNpc->GetAimAtPositionWs();
			const Vector toAimNorm = AsUnitVectorXz(aimAtPosition - loc.Pos(), kUnitZAxis);

			const float kDeadZone = Cos(DEGREES_TO_RADIANS(50.0f));
			const float aimDotLeft = Dot(left, toAimNorm);

			if (FALSE_IN_FINAL_BUILD(g_debugHorseAimStateMachine))
			{
				MsgConPauseable("aimDotLeft: %.2f | kDeadZone: %.2f\n", aimDotLeft, kDeadZone);
			}
			newState = aimDotLeft > 0.0f ? DC::kHorseAimStateAimLeft : DC::kHorseAimStateAimRight;
		}


		GoState(newState, file, line, func);
		return true;
	}
	else
	{
		GoState(DC::kHorseAimStateWeaponHolsteredIdle, file, line, func);
		return true;
	}
}

//--------------------------------------------------------------------------------------
void HorseAimStateMachine::SetOverlayForNewState(DC::HorseAimState newState)
{
	const char* newOverlayState = nullptr;
	switch (newState)
	{
	case DC::kHorseAimStateAimLeft:
		newOverlayState = "left";
		break;
	case DC::kHorseAimStateAimRight:
		newOverlayState = "right";
		break;
	}

	Npc* pNpc = m_hNpc.ToMutableProcess();
	if (!pNpc)
		return;

	const char* baseName = pNpc->GetOverlayBaseName();
	AnimOverlays* pOverlays = pNpc->GetAnimControl()->GetAnimOverlays();

	const char* overlayName = "horse-aiming";
	if (newOverlayState)
		pOverlays->SetOverlaySet(AI::CreateNpcAnimOverlayName(baseName, overlayName, newOverlayState));
	else
		pOverlays->ClearLayer(SID("horse-aiming"));
}

//--------------------------------------------------------------------------------------
bool HorseAimStateMachine::GoState(DC::HorseAimState newState, const char* file, int line, const char* func)
{
	if (newState == m_state)
		return false;

	bool prevAllowLeftArm;
	bool prevAllowRightArm;

	ShouldAllowHandIk(prevAllowLeftArm, prevAllowRightArm);

	const bool prevShouldFlipIkTargets = ShouldFlipIkTargets();

	DC::HorseAimState prevState = m_state;
	if (newState == DC::kHorseAimStateAimLeft || prevState == DC::kHorseAimStateAimRight)
	{
		if (prevState != DC::kHorseAimStateWeaponHolsteredIdle)
		{
			if (!m_disableSwitchSidesTimeout)
			{
				if (GetProcessClock()->GetCurTime() < m_nextAllowedSwitchSidesTime)
					return false;
			}

			m_nextAllowedSwitchSidesTime = GetProcessClock()->GetCurTime() + Seconds(kSwitchSidesTimeout);
		}
		else
		{
			m_nextAllowedSwitchSidesTime = GetProcessClock()->GetCurTime() + Seconds(kSwitchSidesFromUnholsterTimeout);
		}
	}

	SetOverlayForNewState(newState);

	m_state = newState;
	bool fromWeaponDown = prevState == DC::kHorseAimStateWeaponHolsteredIdle;

	if (FALSE_IN_FINAL_BUILD(g_debugHorseAimStateMachine))
	{
		MsgConPauseable("|HASM| Transition from %s to %s\n   (from %s [line %d of %s])\n",
						DC::GetHorseAimStateName(prevState),
						DC::GetHorseAimStateName(newState),
						file, line, func);

		MsgAi(FRAME_NUMBER_FMT "|HASM| Transition from %s to %s (from %s [line %d of %s])\n",
			  FRAME_NUMBER,
			  DC::GetHorseAimStateName(prevState),
			  DC::GetHorseAimStateName(newState),
			  file, line, func);
	}

	if (fromWeaponDown)
	{
		PlayAnimation();
	}
	else
	{
		RequestTransition();
	}

	bool newAllowLeftArm;
	bool newAllowRightArm;
	ShouldAllowHandIk(newAllowLeftArm, newAllowRightArm);

	const bool newShouldFlipIkTargets = ShouldFlipIkTargets();

	if (newAllowLeftArm != prevAllowLeftArm || newAllowRightArm != prevAllowRightArm || newShouldFlipIkTargets != prevShouldFlipIkTargets)
	{
		Npc* pNpc = m_hNpc.ToMutableProcess();
		GAMEPLAY_ASSERT(pNpc);

		AiRideHorseController* pRideHorse = static_cast<AiRideHorseController*>(pNpc->GetAnimationControllers()->GetRideHorseController());
		pRideHorse->RequestHandIkRefresh();
	}

	return true;
}

//--------------------------------------------------------------------------------------
void HorseAimStateMachine::ShouldAllowHandIk(bool& outAllowLeftHand, bool& outAllowRightHand)
{
	switch (m_state)
	{
	case DC::kHorseAimStateWeaponOutIdleLeft:
		outAllowLeftHand = false;
		outAllowRightHand = true;
		break;

	case DC::kHorseAimStateWeaponOutIdleRight:
		outAllowLeftHand = true;
		outAllowRightHand = false;
		break;

	case DC::kHorseAimStateAimLeft:
	case DC::kHorseAimStateAimRight:
		outAllowLeftHand = false;
		outAllowRightHand = false;
		break;

	case DC::kHorseAimStateWeaponHolsteredIdle:
	default:
		outAllowLeftHand = true;
		outAllowRightHand = true;
		break;
	}
}

//--------------------------------------------------------------------------------------
StringId64 HorseAimStateMachine::GetDesiredTransition() const
{
	switch (m_state)
	{
	case DC::kHorseAimStateWeaponHolsteredIdle:
		return INVALID_STRING_ID_64;
	case DC::kHorseAimStateWeaponOutIdleLeft:
	case DC::kHorseAimStateWeaponOutIdleRight:
		return SID("StopAim");
	case DC::kHorseAimStateAimLeft:
		return SID("AimLeft");
	case DC::kHorseAimStateAimRight:
		return SID("AimRight");
	default:
		return INVALID_STRING_ID_64;
	}
}

//--------------------------------------------------------------------------------------
StringId64 HorseAimStateMachine::GetDesiredAnimState() const
{

	switch (m_state)
	{
	case DC::kHorseAimStateWeaponHolsteredIdle:
		return INVALID_STRING_ID_64;
	case DC::kHorseAimStateWeaponOutIdleLeft:
		return SID("ride-horse-backseat-aim-idle-left");
	case DC::kHorseAimStateWeaponOutIdleRight:
		return SID("ride-horse-backseat-aim-idle-right");
	case DC::kHorseAimStateAimLeft:
		return SID("ride-horse-backseat-aim-left");
	case DC::kHorseAimStateAimRight:
		return SID("ride-horse-backseat-aim-right");
	default:
		return INVALID_STRING_ID_64;
	}
}

//--------------------------------------------------------------------------------------
bool HorseAimStateMachine::ShouldNotAim() const
{
	const Npc* pNpc = m_hNpc.ToProcess();

	if (pNpc->WantNaturalAimAt())
		return true;

	// hack to fix http://devtrack-dog/DevSuite/#p-3028/138376
	if (pNpc->GetLookAim().GetActiveAimModeId() == SID("LookAimBuddy"))
		return true;

	return false;
}

//--------------------------------------------------------------------------------------
void HorseAimStateMachine::PlayAnimation()
{
	Npc* pNpc = m_hNpc.ToMutableProcess();
	GAMEPLAY_ASSERT(pNpc);

	const StringId64 desiredAnimState = GetDesiredAnimState();
	if (desiredAnimState == INVALID_STRING_ID_64)
		return;

	AnimStateLayer* pBaseLayer = pNpc->GetAnimControl()->GetBaseStateLayer();
	GAMEPLAY_ASSERT(pBaseLayer);

	DC::AnimNpcTopInfo* pTopInfo = pNpc->GetAnimControl()->TopInfo<DC::AnimNpcTopInfo>();

	pTopInfo->m_rideHorse.m_playedFromBackseat = pNpc->GetAnimationControllers()->GetRideHorseController()->GetRiderPos() == HorseDefines::kRiderBack;

	FadeToStateParams params;
	params.m_allowStateLooping = true;
	params.m_customApRefId = SID("dinaAP");
	params.m_apRef = pNpc->GetAnimationControllers()->GetRideHorseController()->GetSaddleBoundFrame(params.m_apRefValid);
	params.m_animFadeTime = 0.4f;
	pBaseLayer->FadeToState(desiredAnimState, params);

	if (FALSE_IN_FINAL_BUILD(g_debugHorseAimStateMachine))
	{
		MsgConPauseable("|HASM| fading to state %s\n", DevKitOnly_StringIdToString(desiredAnimState));
		MsgAi(FRAME_NUMBER_FMT "|HASM| fading to state %s\n", FRAME_NUMBER, DevKitOnly_StringIdToString(desiredAnimState));
	}
}

//--------------------------------------------------------------------------------------
void HorseAimStateMachine::RequestTransition()
{
	Npc* pNpc = m_hNpc.ToMutableProcess();
	GAMEPLAY_ASSERT(pNpc);

	const StringId64 desiredTransition = GetDesiredTransition();
	if (desiredTransition == INVALID_STRING_ID_64)
		return;

	AnimStateLayer* pBaseLayer = pNpc->GetAnimControl()->GetBaseStateLayer();
	GAMEPLAY_ASSERT(pBaseLayer);

	DC::AnimNpcTopInfo* pTopInfo = pNpc->GetAnimControl()->TopInfo<DC::AnimNpcTopInfo>();

	pTopInfo->m_rideHorse.m_playedFromBackseat = pNpc->GetAnimationControllers()->GetRideHorseController()->GetRiderPos() == HorseDefines::kRiderBack;

	FadeToStateParams params;
	params.m_customApRefId = SID("dinaAP");
	params.m_apRef = pNpc->GetAnimationControllers()->GetRideHorseController()->GetSaddleBoundFrame(params.m_apRefValid);
	pBaseLayer->RequestTransition(desiredTransition, &params);

	if (FALSE_IN_FINAL_BUILD(g_debugHorseAimStateMachine))
	{
		MsgConPauseable("|HASM| Requesting transition %s\n", DevKitOnly_StringIdToString(desiredTransition));
		//MsgAi(FRAME_NUMBER_FMT "|HASM| Requesting transition %s\n", FRAME_NUMBER, DevKitOnly_StringIdToString(desiredTransition));
	}
}

//--------------------------------------------------------------------------------------
bool HorseAimStateMachine::IsActive() const
{
	return m_state != DC::kHorseAimStateWeaponHolsteredIdle;
}

//--------------------------------------------------------------------------------------
float HorseAimStateMachine::GetDeadZone() const
{
	const float kDefaultDeadZone = Cos(DEGREES_TO_RADIANS(50.0f));
	const Npc* pNpc = m_hNpc.ToProcess();
	if (!pNpc)
		return kDefaultDeadZone;

	const DC::HorseNpcRiderSettings& riderSettings = static_cast<const AiRideHorseController*>(pNpc->GetAnimationControllers()->GetRideHorseController())->GetRiderSettings();
	return Cos(DEGREES_TO_RADIANS(riderSettings.m_horseAimSwapSidesDegrees));
}

//--------------------------------------------------------------------------------------
void HorseAimStateMachine::Update()
{
	const Npc* pNpc = m_hNpc.ToProcess();
	GAMEPLAY_ASSERT(pNpc);

	if (!pNpc->ShouldUseHorseAimStateMachine())
		return;

	if (FALSE_IN_FINAL_BUILD(g_debugHorseAimStateMachine))
	{
		g_prim.Draw(DebugString(pNpc->GetTranslation() + kUnitYAxis, DC::GetHorseAimStateName(m_state), ColorFromAimState(m_state)), kPrimDuration1FramePauseable);
		MsgConPauseable("%s\n", DC::GetHorseAimStateName(m_state));
	}

	if (!IsActive())
		return;

	if (ShouldNotAim())
	{
		if (FALSE_IN_FINAL_BUILD(g_debugHorseAimStateMachine))
		{
			MsgConPauseable("Npc wants natural aim at\n");
		}
		
		DC::HorseAimState newState = IsRightSide() ? DC::kHorseAimStateWeaponOutIdleRight : DC::kHorseAimStateWeaponOutIdleLeft;
		GoState(newState, FILE_LINE_FUNC);
		// spam transitions because we may be in a state that can't take the transition when we first request it
		RequestTransition();
		return;
	}

	const Locator loc = pNpc->GetLocator();
	const Vector left = AsUnitVectorXz(GetLocalX(loc), kUnitXAxis);

	const Point aimAtPosition = pNpc->GetAimAtPositionWs();
	const Vector toAimNorm = AsUnitVectorXz(aimAtPosition - loc.Pos(), kUnitZAxis);

	const float kDeadZone = GetDeadZone();
	const float aimDotLeft = Dot(left, toAimNorm);

	if (FALSE_IN_FINAL_BUILD(g_debugHorseAimStateMachine))
	{
		MsgConPauseable("aimDotLeft: %.2f | kDeadZone: %.2f\n", aimDotLeft, kDeadZone);
	}

	if (Abs(aimDotLeft) > kDeadZone || m_state == DC::kHorseAimStateWeaponOutIdleRight || m_state == DC::kHorseAimStateWeaponOutIdleLeft)
	{
		//const AimState newState = aimDotLeft > 0.0f ? AimState::kAimLeft : AimState::kAimRight;
		const DC::HorseAimState newState = aimDotLeft > 0.0f ? DC::kHorseAimStateAimLeft : DC::kHorseAimStateAimRight;
		if (GoState(newState, FILE_LINE_FUNC))
			return;
	}
	// spam transitions because we may be in a state that can't take the transition when we first request it
	RequestTransition();
}

class RiderHandToJointIk : public HandToJointIkInterface
{
protected:
	typedef NdSubsystem ParentClass;

	JointChain m_jointChain;
	JacobianMap m_jacobian;
	NdGameObjectHandle m_hArmTarget;
	StringId64 m_jointTarget;

	F32 m_desiredBlend = 1.0f;
	F32 m_currentBlend = 0.0f;
	F32 m_blendSpeed = 1.0f;
	ArmIndex m_armIndex;

	int m_wristOffset;
	int m_propAttachOffset;

	virtual Err Init(const SubsystemSpawnInfo& info) override
	{
		Err result = ParentClass::Init(info);
		if (result.Failed())
			return result;

		HandToJointIkInfo *pInfo = ((HandToJointIkInfo *)info.m_pUserData);
		m_armIndex = (ArmIndex)pInfo->m_armIndex;
		m_jointTarget = pInfo->m_contactJoint;
		m_hArmTarget = pInfo->m_pTargetObject;

		if (pInfo->m_blendTime > 0.0)
			m_blendSpeed = 1.0f / pInfo->m_blendTime;
		else
			m_currentBlend = 1.0f;

		Character* pOwner = GetOwnerCharacter();
		JointLimits* pJointLimits = pOwner->GetJointLimits();
		GAMEPLAY_ASSERT(pJointLimits);

		const StringId64 startJointId = SID("spinea");
		const StringId64 wristJointId = m_armIndex == kLeftArm ? SID("l_wrist") : SID("r_wrist");
		const StringId64 propAttachId = m_armIndex == kLeftArm ? SID("l_hand_prop_attachment") : SID("r_hand_prop_attachment");
		const StringId64 endJointId = propAttachId;
		m_jointChain.Init(pOwner, startJointId, endJointId);
		m_jointChain.InitIkData(pJointLimits->GetSettingsId());

		m_wristOffset = m_jointChain.FindJointOffset(wristJointId);
		ANIM_ASSERT(m_wristOffset >= 0);

		m_propAttachOffset = m_jointChain.FindJointOffset(propAttachId);
		ANIM_ASSERT(m_propAttachOffset >= 0);

		JacobianMap::EndEffectorDef aEndEffectors[] =
		{
			JacobianMap::EndEffectorDef(wristJointId, IkGoal::kPosition),
		};

		m_jacobian.Init(&m_jointChain, startJointId, ARRAY_COUNT(aEndEffectors), aEndEffectors);

		return result;
	}

	int GetWristOffset() const
	{
		return m_wristOffset;
	}

	int GetPropAttachOffset() const
	{
		return m_propAttachOffset;
	}

	Locator GetPropAttachLocWs()
	{
		return m_jointChain.GetJointLocWs(GetPropAttachOffset());
	}

	Locator GetWristLocWs()
	{
		return m_jointChain.GetJointLocWs(GetWristOffset());
	}

	void UpdateArmIk()
	{
		ScopedTempAllocator tempAlloc(FILE_LINE_FUNC);	// For JointChain allocations

		const NdGameObject *pArmTarget = m_hArmTarget.ToProcess();
		if (!pArmTarget)
			return;

		if (m_currentBlend <= 0.0f)
			return;

		Character* pOwner = GetOwnerCharacter();

		m_jointChain.ReadJointCache();

		I32 jointIndex = pArmTarget->FindJointIndex(m_jointTarget);
		Locator jointLoc = pArmTarget->GetAnimControl()->GetJointCache()->GetJointLocatorWs(jointIndex);

		Locator wristInPropSpace = GetPropAttachLocWs().UntransformLocator(GetWristLocWs());
		Locator wristTarget = jointLoc.TransformLocator(wristInPropSpace);

		JacobianIkInstance ik;
		ik.m_blend = m_currentBlend;
		ik.m_maxIterations = 10;
		ik.m_pJoints = &m_jointChain;
		ik.m_pJacobianMap = &m_jacobian;
		ik.m_pJointLimits = pOwner->GetJointLimits();
		ik.m_pConstraints = m_jointChain.GetJointConstraints();
		ik.m_goal[0].SetGoalPosition(wristTarget.GetTranslation());

		SolveJacobianIK(&ik);

		Quat newWristQuat = GetWristLocWs().Rot();
		Quat finalSavedQuat = Slerp(newWristQuat, wristTarget.GetRotation(), m_currentBlend);
		m_jointChain.PostRotateJointLs(GetWristOffset(), Normalize(Conjugate(newWristQuat)*finalSavedQuat));

		m_jointChain.WriteJointCache();
		m_jointChain.DiscardJointCache();
	}

public:
	virtual void BlendOut(F32 blendTime) override
	{
		m_desiredBlend = 0.0f;
		if (blendTime > 0.0f)
		{
			m_blendSpeed = 1.0f / blendTime;
		}
		else
		{
			m_currentBlend = 0.0f;
		}
	}

	virtual ArmIndex GetArmIndex() const override
	{
		return m_armIndex;
	}

	SUBSYSTEM_UPDATE(Update)
	{
		if (m_currentBlend != m_desiredBlend)
		{
			Seek(m_currentBlend, m_desiredBlend, FromUPS(m_blendSpeed));
		}

		if (m_currentBlend == 0.0f && m_desiredBlend == 0.0f)
		{
			Kill();
		}
	}

	SUBSYSTEM_UPDATE(PostAnimBlending)
	{
		UpdateArmIk();
	}

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override
	{
		NdSubsystem::Relocate(deltaPos, lowerBound, upperBound);
		m_jointChain.Relocate(deltaPos, lowerBound, upperBound);
		m_jacobian.Relocate(deltaPos, lowerBound, upperBound);
	}

	virtual void RelocateOwner(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override
	{
		NdSubsystem::RelocateOwner(deltaPos, lowerBound, upperBound);
		m_jointChain.RelocateOwner(deltaPos, lowerBound, upperBound);
	}
};

TYPE_FACTORY_REGISTER(RiderHandToJointIk, NdSubsystem);