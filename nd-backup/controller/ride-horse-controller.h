/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

/*! \file driving_controller.h
   \brief Controller managing driving a vehicle.
*/

#ifndef AI_RIDE_HORSE_CONTROLLER_H
#define AI_RIDE_HORSE_CONTROLLER_H

#include "ndlib/process/process.h"

#include "gamelib/gameplay/ai/controller/animaction-controller.h"

#include "game/ai/characters/horse.h"
#include "game/vehicle-ctrl.h"


class AiEntity;
class SsTrackGroupInstance;

namespace DC
{
	struct HorseCharacterRiderSettings;
}

class HorseAimStateMachine
{
public:
	void Init(Npc* pNpc);
	bool IsActive() const;
	DC::HorseAimState GetState() const { return m_state; }

	void OnWeaponDown();
	bool IsRightSide() const { return m_state == DC::kHorseAimStateWeaponOutIdleRight || m_state == DC::kHorseAimStateAimRight; }
	bool SetEnabled(bool enabled, const char* file, int line, const char* func);
	void Update();

	void ScriptDisableSwitchSidesTimeout(bool disable) { m_disableSwitchSidesTimeout = disable; }
	void ShouldAllowHandIk(bool& outAllowLeftHand, bool& outAllowRightHand);
	bool ShouldFlipIkTargets() const { return m_state == DC::kHorseAimStateWeaponOutIdleRight; }

protected:
	void RequestTransition();
	void PlayAnimation();
	bool GoState(DC::HorseAimState state, const char* file, int line, const char* func);
	void SetOverlayForNewState(DC::HorseAimState state);
	StringId64 GetDesiredTransition() const;
	StringId64 GetDesiredAnimState() const;
	bool ShouldNotAim() const;
	float GetDeadZone() const;

	MutableNpcHandle		m_hNpc = nullptr;
	DC::HorseAimState		m_state = DC::kHorseAimStateWeaponHolsteredIdle; //AimState::kWeaponHolsteredIdle;
	TimeFrame				m_nextAllowedSwitchSidesTime = kTimeFrameNegInfinity;
	bool					m_disableSwitchSidesTimeout = false;

	static CONST_EXPR float kSwitchSidesTimeout = 0.8f;
	static CONST_EXPR float kSwitchSidesFromUnholsterTimeout = 1.2f;
};


//--------------------------------------------------------------------------------------
/// Interface IAiRideHorseController:
//--------------------------------------------------------------------------------------
struct IAiRideHorseController : public AnimActionController
{
	virtual void MountHorse(Horse *pHorse, HorseDefines::RiderPosition riderPos = HorseDefines::kRiderBack, bool skipAnim = false, bool teleport = false, Maybe<HorseClearance::HorseSide> forceSide = MAYBE::kNothing) = 0;
	virtual bool DismountHorse(bool skipAnim) = 0;
	//virtual void UpdateAnim(StringId64 moveState) = 0;

	virtual bool IsMounting() const = 0;
	virtual bool IsDismounting() const = 0;
	virtual bool IsSwitchingSeats() const = 0;

	virtual bool IsDismountBlocked() const = 0;

	virtual void SetShouldMirrorSwitchSeatsAnim(bool mirror) = 0;

	virtual const TimeFrame GetActionPackEnterTime() const = 0;

	virtual bool IsRidingHorse(bool allowMounting = true, bool allowDismounting = true) const = 0;
	virtual Horse* GetHorse() const = 0;
	virtual Horse* GetOldHorse() const = 0; //when dismounting this is equal to what the horse previously was

	virtual void OnIgcBind(float fadeInTime, bool allowRagdoll = false) = 0;
	virtual void OnIgcUnbind(float fadeOutTime) = 0;

	virtual bool WaitDismountHorse(SsTrackGroupInstance* pGroupInst, int trackIndex) = 0;
	virtual bool WaitMountHorse(SsTrackGroupInstance* pGroupInst, int trackIndex, EnterHorseArgs enterArgs) = 0;

	virtual void SetNewMoveState(HorseDefines::MoveState newState, HorseDefines::MoveState oldState, bool inFrontSeat) = 0;
	virtual bool IsMoveStateChangeAllowed() const = 0;

	virtual void PlayCommandGesture(StringId64 commandGestureId) = 0;

	virtual void SetRiderSettings(StringId64 settingsId) = 0;

	virtual HorseDefines::RiderPosition GetRiderPos() const = 0;
	virtual BoundFrame GetSaddleBoundFrame(bool& outIsValid) const = 0;

	virtual ActionPackController* GetHorseActionPackController() = 0;
	virtual const ActionPackController* GetHorseActionPackController() const = 0;

	virtual HorseAimStateMachine* GetAimStateMachine() = 0;
	virtual const HorseAimStateMachine* GetAimStateMachine() const = 0;

	virtual void ScriptRequestBlizzardSteering() = 0;
	virtual void ScriptRequestWaterBlockSteering() = 0;
	virtual void ScriptRequestOneHandedSteering() = 0;
	virtual void ScriptRequestTwoHandedSteering() = 0;
	virtual void ScriptDisableSystemicGestures() = 0;
	virtual void ScriptDisableCommandGestures() = 0;

	virtual void Cleanup(float fadeTimeOverride = -1.0f, bool includeTurnAnims = true) = 0;
	virtual bool AreScriptedGesturesAllowed(bool allowMounting = false, bool allowDismounting = false, bool debug = false) const = 0;

	virtual void RequestDontExitIgcBeforeHorse() = 0;

	virtual void PostRenderUpdate() = 0;

	virtual void EnableRagdoll(float blendTimeOverride = -1.0f) = 0;
	virtual void DisableRagdoll(float blendTimeOverride = -1.0f) = 0;
	virtual void EnableBackseatHandIk(float blendTimeOverride = -1.0f) = 0;
	virtual void DisableBackseatHandIk(float blendTimeOverride = -1.0f) = 0;

	virtual float GetStirrupsRiderModeBlend(bool& outDoSwitchSeatsBlend) const = 0;

	virtual JointTree& GetJointTree() = 0;
	virtual const JointTree& GetJointTree() const = 0;

	static StringId64 GetDismountAnimForSide(HorseClearance::HorseSide side);
};

IAiRideHorseController* CreateAiRideHorseController();

//--------------------------------------------------------------------------------------
// Anim Actions
//--------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------
struct NpcRideHorseAnimAction : public NdSubsystemAnimAction
{
	typedef NdSubsystemAnimAction ParentClass;

	virtual Err Init(const SubsystemSpawnInfo& info) override;

protected:
	HorseHandle m_hHorse;

	SUBSYSTEM_UPDATE(PostAnimUpdate);

	virtual HorseDefines::RiderPosition GetRiderPos() const;
};

struct NpcIgcRideHorseAnimAction : public NpcRideHorseAnimAction
{
	typedef NpcRideHorseAnimAction ParentClass;

protected:
	SUBSYSTEM_UPDATE(PostAnimUpdate);
};

//--------------------------------------------------------------------------------------
struct NpcDismountHorseAnimAction : public NpcRideHorseAnimAction
{
	typedef NpcRideHorseAnimAction ParentClass;

public:
	void InstanceDestroy(AnimStateInstance* pInst) override;
	void InstanceReplace(const DC::AnimState* pAnimState, FadeToStateParams* pParams) override;

protected:
	SUBSYSTEM_UPDATE(Update);
};

//--------------------------------------------------------------------------------------
struct NpcDieOnHorseAnimAction : public NpcRideHorseAnimAction
{
	typedef NpcRideHorseAnimAction ParentClass;

	virtual Err Init(const SubsystemSpawnInfo& info) override;

public:
	virtual void InstanceCreate(AnimStateInstance* pInst) override;
	virtual void InstancePrepare(const DC::AnimState* pAnimState, FadeToStateParams* pParams) override;

protected:
	HorseDefines::RiderPosition m_riderPos;
	F32 m_horsePrevYWs;
	SUBSYSTEM_UPDATE(Update);

	virtual HorseDefines::RiderPosition GetRiderPos() const override { return m_riderPos; }
};

// works for both player and NPC
//--------------------------------------------------------------------------------------
struct BindToHorseAnimAction : public NdSubsystemAnimAction
{
	typedef NdSubsystemAnimAction ParentClass;

public:
	virtual void InstanceCreate(AnimStateInstance* pInst) override;
	virtual void InstancePrepare(const DC::AnimState* pAnimState, FadeToStateParams* pParams) override;

	virtual Err Init(const SubsystemSpawnInfo& info) override;

	const Horse* GetHorse() const { return m_hHorse.ToProcess(); }

protected:
	HorseHandle m_hHorse;
	HorseDefines::RiderPosition m_attachRiderPos; // count means use horse align

	SUBSYSTEM_UPDATE(Update);

	BoundFrame GetBoundFrame() const;
};


#endif

