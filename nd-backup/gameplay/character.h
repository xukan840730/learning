/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "corelib/containers/list-array.h"
#include "corelib/math/sphere.h"

#include "ndlib/anim/anim-state.h"
#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/fx/fx-handle.h"
#include "ndlib/fx/gore-mgr.h"
#include "ndlib/ndphys/pat.h"
#include "ndlib/render/highlights-mgr.h"
#include "ndlib/render/interface/fg-instance.h"
#include "ndlib/render/ngen/mesh-raycaster-job.h"
#include "ndlib/util/tracker.h"

#include "gamelib/gameplay/ai/component/heart-rate-monitor.h"
#include "gamelib/gameplay/character-collision.h"
#include "gamelib/gameplay/character-types.h"
#include "gamelib/gameplay/emotion-control.h"
#include "gamelib/gameplay/health-system.h"
#include "gamelib/gameplay/leg-ik/move-leg-ik-new.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/pathfind-manager.h"
#include "gamelib/gameplay/nd-attachable-object.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/nd-player-joypad.h"
#include "gamelib/gameplay/nd-subsystem.h"
#include "gamelib/ndphys/collision-cast.h"
#include "gamelib/ndphys/collision-filter.h"
#include "gamelib/ndphys/havok-collision-cast.h"
#include "gamelib/ndphys/water-query-callback.h"
#include "gamelib/render/particle/particle-core.h"
#include "gamelib/scriptx/h/ik-defines.h"
#include "gamelib/scriptx/h/nd-ai-defines.h"
#include "gamelib/scriptx/h/nd-gore-defines.h"
#include "gamelib/scriptx/h/nd-script-func-defines.h"
#include "gamelib/scriptx/h/phys-fx-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ActionPack;
class AnimStateInstance;
class Armor;
class BuoyancyAccumulator;
class Character;
class CharacterMeleeImpulse;
class CompositeBody;
class CompositeRagdollController;
class EffectAnimInfo;
class CharacterEffectTracker;
class EffectControlSpawnInfo;
class FeatureDb;
class FootPlantIk;
class HandPlantIk;
class GrenadeMagnet;
class ICharacterLegIkController;
class INearbyEdgeMgr;
class JacobianMap;
class JointChain;
class JointSet;
class JointTree;
class LegRaycaster;
class NdAttachableObject;
class NdPropControl;
class NdSubsystemMgr;
class NdSubsystemAnimController;
class NetRevive;
class PoseTracker;
class ProcessMeleeAction;
class ProcessSpawnInfo;
class ProcessWeaponBase;
class RigidBody;
class ScreenSpaceTextPrinter;
class SpawnInfo;
class MeshRayCastJob;
class NdPlayerJoypad;

struct CompositeBodyInitInfo;
struct LegRayCastInfo;
struct MeleeHistory;
struct ReceiverDamageInfo;

FWD_DECL_PROCESS_HANDLE(ProcessPhysRope);

namespace DC
{
	struct CharacterCollision;
	struct ExplosionSettings;
	struct DynamicStateInfoArray;

	struct GoreMaskArray;
	struct GoreMeshArray;
	struct GorePropArray;
}

FWD_DECL_PROCESS_HANDLE(Character);
FWD_DECL_PROCESS_HANDLE(ProcessMeleeAction);

extern bool g_legIkOnRagdolls;
extern bool g_debugPrintCharacterTextInFinal;
extern bool g_debugPlayerInventoryInFinal;
extern bool g_debugPlayerInventorySelectionInFinal;
extern bool g_debugPlayerInventoryCheckpointInFinal;
extern bool g_debugPlayerClamberTeleportBug;

/// --------------------------------------------------------------------------------------------------------------- ///
union CharacterFlags
{
	U32 m_rawBits;
	struct
	{
		bool m_knockedDown : 1;		//!< Player has been knocked down, to be revived (for coop)
		bool m_grappled : 1;		//!< Player is grappled (coop)
		bool m_turtle : 1;			//!< Character is hunkering
		bool m_onGround : 1;
		bool m_dying : 1;
		bool m_lookingForTruce : 1;	//!< Player has hands up, looking for an alliance
		bool m_cheap : 1;			//!< This NPC is in "cheap" mode (for crowds)
		bool m_spawnedCheap : 1;	//!< This NPC was spawned in "cheap" mode (and therefore may lack some features)
	};
};

struct RagdollReloadData
{
	StringId64 m_artItemId = INVALID_STRING_ID_64;
	bool m_isPhysical = false;
	bool m_isPowered = false;
	bool m_isDying = false;
	StringId64 m_settingsId = INVALID_STRING_ID_64;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class Character : public NdGameObject
{
private: typedef NdGameObject ParentClass;
public:
	FROM_PROCESS_DECLARE(Character);

	STATE_DECLARE_OVERRIDE(Active);

	enum RagdollAnimLayerPriority
	{
		kRagdollLayerPrioritySnapshot = 500,
		kRagdollLayerPriorityBasePose,
		kRagdollLayerPriorityRagdoll,
		kRagdollLayerPriorityPoseAnim,
	};

	enum BodyImpactSoundType
	{
		kBodyImpactChestSound,
		kBodyImpactBackSoundCt,
		kBodyImpactBackSoundLt,
		kBodyImpactBackSoundRt,
		kNumBodyImpactSound,
	};

	enum ShoulderImpactSoundType
	{
		kLeftShoulderImpactSound,
		kRightShoulderImpactSound,
		kNumShoulderImpactSound,
	};

	enum SoundSurfaceJobAttachPoints
	{
		kSoundSurfaceJobAttachPelvis,
		kSoundSurfaceJobAttachChest,
		kSoundSurfaceJobAttachWristL,
		kSoundSurfaceJobAttachWristR,
		kSoundSurfaceJobAttachShoulderL,
		kSoundSurfaceJobAttachShoulderR,
		kSoundSurfaceJobAttachCount,
	};

	struct SoundSurfaceJobData
	{
		Character* m_pChar;
		Locator characterLoc;
		Locator m_attachLoc[kSoundSurfaceJobAttachCount];
		bool m_attachValid[kSoundSurfaceJobAttachCount];
	};

	enum class StanceState
	{
		kStand,
		kCrouch,
		kProne,
		kWadeStand,
		kWadeCrouch,
		kWadeProne,
		kSwimSurface,
		kSwimDive
	};

	struct TimedHighlight
	{
		F32				m_intensity = 0.0f;
		F32				m_fadeInRate = 0.0f;
		F32				m_fadeOutRate = 0.0f;
		Color			m_color = kColorBlack;
		HighlightStyle	m_style = kHighlightStyleGlowNormal;
		TimeFrame		m_endTime = TimeFrameNegInfinity();
	};

	struct RagdollGroundProbesConfig
	{
		RagdollGroundProbesConfig() {}
		F32 m_blendOutHoleDepth = 0.5f;
	};

	static bool IsBuddyNpc(const Process* pProc)
	{
		const Character* const pChar = Character::FromProcess(pProc);
		return pChar && pChar->IsBuddyNpc();
	}

	static bool IsBuddyNpc(ProcessHandle hProc)
	{
		return IsBuddyNpc(hProc.ToProcess());
	}

	Character();
	virtual ~Character() override;
	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual Err InitializeDrawControl(const ProcessSpawnInfo& spawn, const ResolvedLook& resolvedLook) override;
	virtual Err InitCompositeBody(CompositeBodyInitInfo* pInitInfo = nullptr) override;
	virtual ProcessSnapshot* AllocateSnapshot() const override;
	virtual void RefreshSnapshot(ProcessSnapshot* pSnapshot) const override;
	virtual void ResolveLook(ResolvedLook& resolvedLook,
							 const SpawnInfo& spawn,
							 StringId64 desiredActorOrLookId) override;

	virtual Err PostInit(const ProcessSpawnInfo& spawn) override;

	virtual int GetChildScriptEventCapacity() const override { return 32; }
	virtual U32 GetEffectListSize() const override;
	virtual void SetupRagdollHelper(const ProcessSpawnInfo& spawn);
	virtual void PreUpdate() override;
	virtual void PostAnimUpdate_Async() override;
	virtual void PostRootLocatorUpdate();
	void PostAlignMovement(const Locator& delta);
	virtual void PostAnimBlending_Async() override;
	virtual void PostJointUpdate_Async() override;
	virtual void EventHandler(Event& event) override;
	virtual void OnError() override;
	virtual void OnKillProcess() override;
	virtual void DebugDraw(WindowContext* pDebugWindowContext, ScreenSpaceTextPrinter* pPrinter) const;
	virtual void DebugDraw(const DebugDrawFilter& filter) const override;
	virtual void DebugShowProcess(ScreenSpaceTextPrinter& printer) const override;

	static void ResetDebugTextInFinal();
	static void AppendDebugTextInFinal(const char* pText);
	static void DebugPrintTextInFinal(WindowContext* _pWindowContext);

	virtual const DC::PhysFxSet* GetPhysFxSet() const override;
	virtual void HandleTriggeredEffect(const EffectAnimInfo* pEffectAnimInfo) override;
	virtual void UpdateDc();
	virtual bool IsPlayerMeleeingNpc() const { return false; }
	virtual Vector GetIkGroundNormal() const { return kUnitYAxis; }
	virtual LegIkPlatform GetLegIkPlatform() { return LegIkPlatform(); }
	virtual const DC::LegIkFootDef* GetLegIkFootDef() { return nullptr; }

	virtual void SetGasMaskSettingsId(StringId64 gasMaskSettingId);
	virtual StringId64 GetGasMaskSettingsId() const;

	bool WasSpawnedCheap() const { return m_characterFlags.m_spawnedCheap; } // constant throughout the NPC's lifetime
	bool IsCheap() const { return m_characterFlags.m_cheap; }				 // can change dynamically at runtime
	void MakeCheap(bool wantCheap);

	virtual bool IsEllie() const { return false; }
	virtual bool IsAbby() const { return false; }
	virtual bool IsTommy() const { return false; }
	bool IsNpc() const { return m_isNpc; }
	bool IsBuddyNpc() const { return m_isBuddyNpc; }
	bool IsInvisibleToAi() const { return m_invisibleToAi; }
	bool IsInAiDarkness() const { return m_inAiDarkness && m_inAiDarknessRegion; }
	bool IsInAiDarknessRegion() const { return m_inAiDarknessRegion; }
	bool IsInVehicle() const { return GetEnteredVehicle().HandleValid(); }
	Gender GetGender() const { return m_isFemale ? Gender::kFemale : Gender::kMale; }
	void SetInvisibleToAi(bool f);
	void SetInAiDarknessRegion(bool f);
	void SetInAiDarkness(bool f);

	StringId64 GetKillerId() const { return m_killerUserId; }
	void SetKillerUserId(StringId64 killerUserId) { m_killerUserId = killerUserId; }
	virtual FactionId GetNoiseSourceFactionId() const;

	virtual void SetCharacterScale(float scale) {}
	virtual float GetCharacterScale() const { return 1.0f; }

	virtual void SetLastCrouchedTime(TimeFrame lastCrouchedTime) { m_lastCrouchedTime = lastCrouchedTime; }
	virtual TimeFrame GetLastCrouchedTime() const { return m_lastCrouchedTime; }
	virtual bool CanUsePredicativeIk() const { return false; }

	virtual bool IsJumping() const				{ return false; }
	virtual bool IsCrouched() const				{ return false; }
	virtual bool IsPeeking() const				{ return false; }
	virtual bool IsSprinting() const			{ return false; }
	virtual bool IsProne() const				{ return false; }
	virtual bool IsSupine() const				{ return false; }
	virtual bool IsInSqueezeThrough() const     { return false; }
	virtual bool IsRolling() const				{ return false; }
	virtual bool IsStanding() const				{ return !IsCrouched() && !IsProne() && !IsSupine(); }
	virtual bool IsInStealthVegetation() const	{ return GetStealthVegetationHeight() > DC::kStealthVegetationHeightNone; }
	virtual bool IsHanging() const				{ return false; }
	virtual bool IsClimbing() const				{ return false; }
	virtual bool IsFreeRoping() const			{ return false; }
	virtual bool IsInCover() const				{ return false; }
	virtual bool IsInHighCover() const			{ return false; }
	virtual bool IsOnFire() const				{ return false; }
	virtual bool AllowOnFireDamage() const		{ return true;  }
	virtual bool CanBreakBigJumpThrough() const { return false; }
	virtual bool IsShambler() const				{ return false; }
	virtual bool IsBloaterOrRatking() const		{ return false; }
	virtual bool IsRatking() const				{ return false; }
	virtual bool IsStandingForCameraRemap() const { return false; }  // reserved by camera-strafe-remap.
	virtual bool IsCrouchedForCameraRemap() const { return false; }  // reserved by camera-strafe-remap.

	void DisableThrowableSelfDamage() { m_disableThrowableSelfDamage = true; };
	bool IsThrowableSelfDamageDisabled() const { return m_disableThrowableSelfDamage; }

	virtual HeartRateMonitor::HeartRateUpdateParams GetHeartRateUpdateParams(U32 tensionOverride)
	{
		return HeartRateMonitor::HeartRateUpdateParams();
	}
	virtual void HandleBreathStartEvent(float duration, int context);

	virtual F32 GetCurrentVertigoPct() const { return 0.0f; }
	virtual F32 GetCurrentBreathingVertigoPct() const { return 0.0f; }
	virtual bool IsInVertigoRegion() const { return false; }

	void SetCharacterName(const char* name, bool err = false);
	const char* GetCharacterName() const { return m_characterName; }
	const char* GetCharacterNameError() const { return m_characterNameError; }

	virtual F32 GetBurningDamagePerSecond() const { return 0.0f; }

	virtual bool IsHiddenInStealthVegetation() const;

	virtual StringId64 GetBaseEmotion() const override { return SID("neutral");  }

	virtual StringId64 CreateOverlayName(const char* group, const char* mode) const;

	virtual ICharacterLegIkController* GetLegIkController() { return nullptr; }
	virtual LegRaycaster* GetLegRaycaster() { return nullptr; }
	virtual const LegRaycaster* GetLegRaycaster() const { return nullptr; }
	virtual StringId64 GetLegIkConstantsSettingsId() const;
	const DC::LegIkConstants* GetLegIKConstants() const;
	virtual void GetLegRayCastInfo(U32 currentMode, LegRayCastInfo& info) const;
	virtual U32 GetDesiredLegRayCastMode() const;
	virtual bool ShouldIkMoveRootUp() const { return false; }
	virtual F32 EnforceIkHipDistance() const { return 0.0f; }
	virtual bool ShouldUsePredictiveLegRaycasts() const { return false; }
	virtual float GetPredictiveLegRaycastMaxBlend() const { return 0.0f; } // how much we should use results from predictive raycasts vs current raycasts

	virtual F32 GetMissingDistance(FactionId againstFaction) const { return kLargestFloat; }
	virtual F32 GetDesiredMissingDistance(FactionId againstFaction) const { return GetMissingDistance(againstFaction); }
	virtual F32 GetMinimumMissingDistance(FactionId againstFaction) const { return GetMissingDistance(againstFaction); }

	void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound) override;

	void UpdatePushers();
	virtual bool CanBePushedByNpc() const { return true; }

	virtual Armor* GetCharacterArmor() {return nullptr;}
	virtual const Armor* GetCharacterArmor() const {return const_cast<Character*>(this)->GetCharacterArmor();}
	virtual const DC::CharacterCollision* GetCharacterCollision() const { return nullptr; }
	void TurnOnCapsules();
	void TurnOffCapsules();
	bool GetMeleeCapsulesOn() const { return m_meleeCapsulesOn; }
	bool IsBodyMeleeEnabled(const RigidBody* pBody) const;

	bool GetSurfaceContactPoint(Point_arg s0, Point_arg s1, const RigidBody* pRigidBody, Point& pnt, F32 maxDist = FLT_MAX) const;

	// This function looks wrong and expensive. I'm deprecating it. When we have time we should revisit places where we're using it.
	const RigidBody* GetBestSurfaceRigidBodyDeprecated(Point_arg p0, Point &outSurfacePoint) const;

			IHealthSystem* GetHealthSystem()		{ return m_pHealthSystem; }
	const	IHealthSystem* GetHealthSystem() const	{ return m_pHealthSystem; }
	virtual F32 GetAdrenaline() const					{ return m_pHealthSystem->GetAdrenaline(); }
	virtual void Die();
	virtual bool IsDead() const                     { return m_pHealthSystem ? m_pHealthSystem->IsDead() : false; }
	virtual bool IsDown() const						{ return m_characterFlags.m_knockedDown; }

	const HeartRateMonitor* GetHeartRateMonitor() const { return m_pHeartRateMonitor; }
	HeartRateMonitor* GetHeartRateMonitor() { return m_pHeartRateMonitor; }
	F32 GetCurrentHeartRate() const { return m_pHeartRateMonitor ? m_pHeartRateMonitor->GetCurrentHeartRate() : 0.0f; }
	F32 GetTargetHeartRate() const { return m_pHeartRateMonitor ? m_pHeartRateMonitor->GetTargetHeartRate() : 0.0f; }

	TimeFrame LastGotShotTime() const { return m_lastGotShotTime; }
	TimeFrame GetTimeSinceLastGotShot() const { return GetClock()->GetTimePassed(LastGotShotTime()); }
	bool RequestArmBloodSpawnLower()
	{
		bool ret = m_nextArmBloodLower;
		m_nextArmBloodLower = false;
		return ret;
	}

	const Character* GetLastMeleeAttacker() const { return m_hLastAttacker.ToProcess(); }
	void SetLastMeleeAttacker(Character* pAttacker) { m_hLastAttacker = pAttacker; }

	const Character* GetLastMeleeTarget() const { return m_hLastMeleeTarget.ToProcess(); }
	void SetLastMeleeTarget(const Character* pTarget) { m_hLastMeleeTarget = pTarget; }

	virtual TimeFrame GetTimeOfDeath() const { return TimeFrameNegInfinity(); }
	TimeFrame GetTimeSinceDeath() const { return GetClock()->GetTimePassed(GetTimeOfDeath()); }

	// IsAbleToRespond() means the character is neither dead nor otherwise [game-specific] unable to respond to his allies (e.g. during a conversation).
	virtual bool IsAbleToRespond(bool allowGrappledResponse = false) const { return !IsDead(); }

	virtual float GetDamageTimeScale() const { return 1.0f; }
	virtual void SetDamageTimeScaleLastDamage(TimeFrame lastDamage) {}
	virtual void GetReceiverDamageInfo(ReceiverDamageInfo* pInfo) const override;

	void SetAllowDeadRagdollSnapBackToAnimation(bool b) { m_allowDeadRagdollSnapBackToAnimation = b; }
	bool GetAllowDeadRagdollSnapBackToAnimation() const { return m_allowDeadRagdollSnapBackToAnimation; }

	virtual bool AllowIgcAnimation(const SsAnimateParams& params) const override
	{
		if (m_allowDeadRagdollSnapBackToAnimation)
			return true;
		if (!IsDead())
			return true;
		if (params.m_allowAnimateDeadIfRagdollNotActive && !IsRagdollPhysicalized())
			return true;

		return false;
	}

	virtual float GetBackPackPushAway() const		{ return 0.17f; }
	virtual float GetBackPackPushUp() const		{ return 0.1f; }
	virtual StringId64 GetCapCharacterId() const;

	virtual MutableProcessHandle GetEnteredVehicle() const { return MutableProcessHandle(); }
	Process* GetDrivenVehicleProcess() const;
	Process* GetLastDrivenVehicleProcess() const;

	NdGameObject* GetRailVehicleObject() const;

	virtual NavLocation GetNavLocation() const override = 0;

	virtual DC::StealthVegetationHeight GetStealthVegetationHeight() const;

	virtual	ProcessWeaponBase*  GetWeapon()							{ return nullptr; }
	const	ProcessWeaponBase*  GetWeapon() const					{ return const_cast<Character*>(this)->GetWeapon(); }
	virtual	ProcessWeaponBase*  GetWeaponInHand()					{ return nullptr; }
	const	ProcessWeaponBase*  GetWeaponInHand() const				{ return const_cast<Character*>(this)->GetWeaponInHand(); }
	virtual	ProcessWeaponBase*  GetMostRecentWeaponInHand()			{ return GetWeaponInHand(); }
	const	ProcessWeaponBase*  GetMostRecentWeaponInHand() const	{ return const_cast<Character*>(this)->GetMostRecentWeaponInHand(); }
	virtual	GrenadeMagnet* GetGrenadeMagnet()						{ return nullptr; }
	const	GrenadeMagnet* GetGrenadeMagnet() const					{ return const_cast<Character*>(this)->GetGrenadeMagnet(); }

	virtual bool HasFirearm() const { return (GetWeapon() != nullptr); }
	virtual bool HasMeleeWeapon() const { return (GetWeapon() != nullptr); }
	virtual bool HasFirearmInHand() const { return (GetWeaponInHand() != nullptr); }
	virtual bool HasLonggunInHand() const { return (GetWeaponInHand() != nullptr); }
	virtual bool HasPistolInHand() const { return (GetWeaponInHand() != nullptr); }

	virtual bool IsAimingFirearm() const { return false; }
	virtual bool IsAimingAt(Sphere_arg sphere) const { return false; }
	virtual bool IsBlindFiring() const { return false; }

	bool IsAimingFirearmAt(Sphere_arg sphere, float referenceDist = 20.0f) const;

	virtual bool WantNaturalLookAt() const { return false; }
	virtual bool WantNaturalAimAt() const { return false; }
	virtual Point GetAimAtPositionPs() const { return kOrigin; }
	virtual Point GetLookAtPositionPs() const { return kOrigin; }
	Point GetAimAtPositionWs() const { return GetParentSpace().TransformPoint(GetAimAtPositionPs()); }
	Point GetLookAtPositionWs() const { return GetParentSpace().TransformPoint(GetLookAtPositionPs()); }

	// returns number of nearby allies, and up to 'capacity' pointers to the allies, sorted in order of increasing linear distance from self
	U32F GetNearestAlliesByLinearDist(U32F capacity, const Character* apNearestAllies[]) const;
	virtual U32F GetNearestAlliesByPathDist(U32F capacity, const Character* apNearestAllies[]) const { return 0; }
	virtual U32F GetNearestAlliesForDialog(U32F capacity, const Character* apNearestAllies[]) const { return 0; }

	void SetListenModeFootEffectTime(TimeFrame time) { m_listenModeFootEffectTime = time; }
	TimeFrame GetTimeSinceLastListenModeFootEffect() const
	{
		return GetClock()->GetTimePassed(m_listenModeFootEffectTime);
	}

	MeshRaycastResult* GetBodyMeshRaycastResult(BodyImpactSoundType index);
	MeshRaycastResult* GetShoulderMeshRaycastResult(ShoulderImpactSoundType index);
	MeshRaycastResult* GetHandMeshRaycastResult(ArmIndex index);

	// intended to be overridden in subclasses to highlight character and all attachables (weapons, props, clothes, etc.)
	virtual void HighlightEntireCharacter(const Color& color, HighlightStyle style) { Highlight(color, style); }

	void StartTimedHighlight(const TimedHighlight& highlight) { m_timedHighlight = highlight; }
	const TimedHighlight& GetTimedHighlight() const { return m_timedHighlight; }
	void UpdateTimedHighlight();

	virtual void SetFgInstanceFlag(FgInstance::FgInstanceFlags flag) override;
	virtual void ClearFgInstanceFlag(FgInstance::FgInstanceFlags flag) override;

	virtual void PhotoModeSetHidden(bool hidden) override;

public:
	virtual NdSubsystemMgr* GetSubsystemMgr() override { return m_pSubsystemMgr; }
	virtual const NdSubsystemMgr* GetSubsystemMgr() const override { return m_pSubsystemMgr; }

	virtual NdSubsystemAnimController* GetActiveSubsystemController(StringId64 type = INVALID_STRING_ID_64) override;
	virtual const NdSubsystemAnimController* GetActiveSubsystemController(StringId64 type = INVALID_STRING_ID_64) const override;

	void SetIsDoomed() { m_isDoomed = true; }
	bool IsDoomed() const { return m_isDoomed; }

	void SetInProneRegion();
	bool IsInProneRegion() const { return m_isInProneRegion > 0; }

	BoundFrame GetCachedProneOrientationAp() const
	{
		return BoundFrame(GetTranslation(), m_cachedProneOrientation, GetBinding());
	}
	Quat GetCachedProneOrientation() const { return m_cachedProneOrientation; }
	void SetCachedProneOrientation(Quat_arg proneOrientation) { m_cachedProneOrientation = proneOrientation; }

	BoundFrame GetCachedProneAimOrientationAp() const
	{
		return BoundFrame(GetTranslation(), m_cachedProneAimOrientation, GetBinding());
	}
	Quat GetCachedProneAimOrientation() const { return m_cachedProneAimOrientation; }
	void SetCachedProneAimOrientation(Quat_arg proneAimOrientation) { m_cachedProneAimOrientation = proneAimOrientation; }

	Locator GetCachedProneAimCamPivot() const { return m_cachedProneAimCamPivot; }
	void SetCachedProneAimCamPivot(const Locator& pivot) { m_cachedProneAimCamPivot = pivot; }
	void SetUnhittableTime(TimeFrame unhittableTime) { m_unhittableTime = unhittableTime; }
	void SetForceNoBulletFullbodyTime(TimeFrame noBulletFullbodyTime) { m_noBulletFullbodyTime = noBulletFullbodyTime; }
	void SetForceNoBulletHitTime(TimeFrame noBulletHitTime) { m_noBulletHitTime = noBulletHitTime; }
	void SetForceNoBulletShotTime(TimeFrame noBulletShotTime) { m_noBulletShotTime = noBulletShotTime; }
	TimeFrame GetUnhittableTime() const { return m_unhittableTime; }
	TimeFrame GetForceNoBulletFullbodyTime() const { return m_noBulletFullbodyTime; }
	TimeFrame GetForceNoBulletHitTime() const { return m_noBulletHitTime; }
	TimeFrame GetForceNoBulletShotTime() const { return m_noBulletShotTime; }

	//////////////////////////////////////////////////////////////////////////
	// Ragdoll

public:
	void AllowRagdoll() { m_allowRagdoll = true; }
	virtual void DisallowRagdoll() { m_allowRagdoll = false; }
	bool IsRagdollAllowed() const { return m_allowRagdoll; }
	virtual bool CanRagdoll() const;
	void SetPowerRagdollDisabled(bool bDisabled, F32 blendOutTime = 0.2f, F32 timer = FLT_MAX);
	Err InitializeRagdoll(CompositeBodyInitInfo* pInitInfo = nullptr);
	void CopyRagdollBuoyancy(const Character* pSource);
	void ReloadCollision();
	void ReloadRagdollPrepare();
	void ReloadRagdoll();
	// Enables ragdoll. It will start as power ragdoll. If character is dying the ragdoll will fade out power automatically at certain conditions.
	// You can select the power ragdoll settings.
	bool PhysicalizeRagdoll(bool bDying = true,
							StringId64 settingsId = INVALID_STRING_ID_64,
							F32 blendInTime		  = 0.2f,
							bool eff = false);
	bool PhysicalizeRagdollWithTimeOut(F32 timeOut,
									   StringId64 settingsId = INVALID_STRING_ID_64,
									   F32 blendInTime		 = 0.2f);
	// Disables ragdoll instantly
	void UnphysicalizeRagdoll(bool eff = false, bool overrideDeathCheck = false);
	// Returns true if ragdoll is currently enabled
	bool IsRagdollPhysicalized() const;
	// Blends out ragdoll during given time. Once blended out it will unphysicalize automatically.
	void BlendOutRagdoll(float blendOutTime = 0.2f, bool eff = false, bool overrideDeathCheck = false);
	// Is ragdoll blending out and will be unphysicalized soon?
	bool IsRagdollBlendingOut() const { return m_ragdollBlendOutTimer >= 0.0f; }
	// Fades out power of the ragdoll during given time. Once blended out the ragdoll is powerless and animation does not matter anymore.
	void FadeOutRagdollPower(float fadeOutTime = 0.0f, const char* pDebugReason = nullptr, bool limitVelocities = true);
	// Has power of the ragdoll faded out?
	bool IsRagdollPowerFadedOut() const;
	// Is the power of the ragdoll currently fading out? Also returns true if it has already faded out completely.
	bool IsRagdollPowerFadingOut() const;
	bool IsRagdollAsleep() const;
	bool IsRagdollBuoyant() const;
	bool IsRagdollDying() const;
	bool IsRagdollOutOfBroadphase() const { return m_ragdollOutOfBroadphase; }
	void SleepRagdoll();
	CompositeBody* GetRagdollCompositeBody() const { return m_pRagdoll; }
	CompositeRagdollController* GetRagdollController() const;
	virtual Collide::Layer GetCollisionLayer() const { return Collide::kLayerNpc; }
	void SetRagdollGroundProbesEnabled(bool enabled) { m_ragdollGroundProbesEnabled = enabled; }
	void UpdateRagdollGroundProbes(Point& groundPosPs,
								   Vector& groundNormPs,
								   Pat& pats,
								   F32& speedY,
								   const RagdollGroundProbesConfig& config = RagdollGroundProbesConfig());
	// Override default dying ragdoll settings
	void SetRagdollDyingSettings(StringId64 id, F32 blendTime = 0.2f);
	virtual StringId64 GetRagdollDyingSettings() const { return m_ragdollDyingSettings; }
	void SetClientControlsRagdollPostAnimBlending(bool b) { m_clientControlsRagdollPostAnimBlending = b; }
	void RagdollPostAnimBlending();

	virtual void DebugDrawCompositeBody() const override;

	virtual bool HasDeathAnimEnded() const;

	bool HasAnimInstanceRagdollEffs(const AnimStateInstance* pInst);

	void SetTeleport() override;
	virtual void HandleDriveEvent(Event& evt) {}

protected:
	virtual void UpdateRagdoll();
	virtual void OnBodyKilled(RigidBody& body) override;

	void DestroyControllers();

	virtual void DetermineGender();

public:
	virtual const NdAttachableObject* GetShield() const	{ return nullptr; }
	virtual bool ShieldOnBack() const					{ return false; }
	virtual bool HasShieldEquipped() const				{ return false; }

	void SetGenderIsFemale(bool isFemale)				{ m_isFemale = isFemale; UpdateFacts(); }

	virtual Locator GetCurrentEdgeLocator() const { return Locator(kIdentity); }

	virtual StanceState GetStanceState() const;

	/************************************************************************/
	/* FLAGGED FOR FUTURE REFACTORING                                       */
	/************************************************************************/
	bool GetTakingCover() const												{ return m_inCover; }
	void SetTakingCover(bool inCover)										{ m_inCover = inCover; }
	float GetHorzCoverAngle() const											{ return m_horzCoverAngle; }
	float GetVertCoverAngle() const											{ return m_vertCoverAngle; }
	void SetTakingCoverAngle(float horzAngleInDeg, float vertAngleInDeg = 60.0f);
	void SetTakingCoverDirectionWs(Vector_arg dirWs);
	void SetRawCoverDirectionWs(Vector_arg dirWs);
	const Vector GetTakingCoverDirectionWs() const { return m_coverDirWs; }
	const Vector GetRawCoverDirectionWs() const { return m_rawCoverDirWs; }
	virtual bool IsPointInCoverZoneWs(Point_arg testPosWs) const;
	virtual Locator GetDeltaFromLastFramePs() const { return Locator(kIdentity); }

	virtual void SetCharred() {}

	// how vulnerable to attacker am I
	DC::Vulnerability	GetVulnerability() const { return m_vulnerability; }
	TimeFrame			GetVulnerabilityStartTime() const { return m_startVulnerabilityTime; }
	TimeFrame			GetVulnerabilityTime() const;
	TimeFrame			GetCoverVulnerabilityTime() const;
	void				SetVulnerability(DC::Vulnerability vulnerability);
	void				SetAnimationVulnerabilityOverride(DC::Vulnerability vulnerability) { m_animationVulnerabilityOverride = vulnerability; }
	void				ClearAnimationVulnerabilityOverride() { m_animationVulnerabilityOverride = DC::kVulnerabilityNone; }
	DC::Vulnerability	GetAnimationVulnerabilityOverride() const { return m_animationVulnerabilityOverride; }

	void			SetLastShotTime(TimeFrame shotTime) { m_lastShotTime = shotTime; ++m_shotsFired; }
	void			SetLastShotFromAndTo(Point lastShotFrom, Point lastShotTo) { m_lastShotFrom = lastShotFrom; m_lastShotTo = lastShotTo; }
	void			SetLastShotPlayerTime(TimeFrame shotTime) { m_lastShotPlayerTime = shotTime; }

	Point			GetLastShotFrom() const { return m_lastShotFrom; }
	Point			GetLastShotTo() const { return m_lastShotTo; }
	const TimeFrame	GetLastShotTime() const { return m_lastShotTime; }
	const TimeFrame	GetLastShotPlayerTime() const { return m_lastShotPlayerTime; }
	const TimeFrame GetTimeSinceLastShot() const { return GetClock()->GetTimePassed(GetLastShotTime()); }

	U32				GetShotsFired() const { return m_shotsFired; }
	void			ResetShotsFired() { m_shotsFired = 0; }

	virtual bool RequestMeshEffectPermission() { return m_meshEffectsSpawned++ < m_maxMeshEffects; }
	virtual bool RequestSingleExitParticlePermission()
	{
		bool ret = !m_spawnedSingleExitParticleThisFrame;
		m_spawnedSingleExitParticleThisFrame = true;
		return ret;
	}

	virtual void UpdateFeetWet();
	virtual void UpdateMeleeTime();

	void SetLastMeleeMoveStartedTime() { m_lastMeleeMoveStartedTime = GetCurTime(); }
	void SetLastMeleeMoveActiveTime() { m_lastMeleeMoveActiveTime = GetCurTime(); }
	void SetLastMeleeOpponentTime() { m_lastMeleeOpponentTime = GetCurTime(); }
	void SetLastMeleeAttackActiveTime() { m_lastMeleeAttackActiveTime = GetCurTime(); }
	void SetLastBegForLifeTime() { m_lastBegForLifeTime = GetCurTime(); }
	void SetLastMeleeExecutionSurprise(ProcessMeleeActionHandle hExecution) { m_hLastMeleeExecutionSurprise = hExecution; }

	const TimeFrame	GetLastMeleeMoveStartedTime() const { return m_lastMeleeMoveStartedTime; }
	const TimeFrame	GetLastMeleeMoveActiveTime() const { return m_lastMeleeMoveActiveTime; }
	const TimeFrame	GetLastMeleeOpponentTime() const { return m_lastMeleeOpponentTime; }
	const TimeFrame	GetLastMeleeAttackActiveTime() const { return m_lastMeleeAttackActiveTime; }
	const TimeFrame GetLastBegForLifeTime() const { return m_lastBegForLifeTime; }
	ProcessMeleeActionHandle GetLastMeleeExecutionSurprise() const { return m_hLastMeleeExecutionSurprise; }

	const TimeFrame	GetTimeSinceLastMeleeMoveStarted() const { return GetCurTime() - m_lastMeleeMoveStartedTime; }
	const TimeFrame	GetTimeSinceLastMeleeMoveActive() const { return GetCurTime() - m_lastMeleeMoveActiveTime; }
	const TimeFrame	GetTimeSinceLastMeleeOpponent() const { return GetCurTime() - m_lastMeleeOpponentTime; }
	const TimeFrame	GetTimeSinceLastMeleeAttackActiveTime() const { return GetCurTime() - m_lastMeleeAttackActiveTime; }
	const TimeFrame GetTimeSinceLastBegForLife() const { return GetCurTime() - m_lastBegForLifeTime; }

	TimeFrame GetTimeSinceCreation() const;

	virtual void SetTimePassedSinceNonGrazingHit(TimeFrame timeSinceNonGrazingHit) {}
	virtual TimeFrame GetTimePassedSinceNonGrazingHit() const { return Seconds(1000.0f); }
	virtual const Locator GetEyeWs() const;
	virtual const Locator GetEarWs() const;

	virtual const ProcessMeleeAction* GetCurrentMeleeAction() const { return nullptr; }
	virtual ProcessMeleeAction* GetCurrentMeleeAction() { return nullptr; }
	virtual const ProcessMeleeAction* GetNextMeleeAction() const { return nullptr; }
	virtual ProcessMeleeAction* GetNextMeleeAction() { return nullptr; }

	virtual PathfindRequestHandle GetPathfindRequestHandle() const { return PathfindRequestHandle(); }

	virtual bool IsMeleeActionSuccessful() const { return false; }
	CharacterMeleeImpulse* GetMeleeImpulse() const { return m_pMeleeImpulse; }

	virtual int GetMeleeContext() const { return -1; }

	virtual bool IsBusyInMelee() const { return false; }

	DC::GoreFilter GetGoreFilter() const { return m_goreFilter; }
	void SetGoreFilter(DC::GoreFilter filter) { m_goreFilter = filter; }

	const bool GetBloodPoolSaveData(StringId64& jointId, F32& scale) const
	{
		jointId = m_bloodPoolJoint;
		scale	= m_bloodPoolScale;
		return jointId != INVALID_STRING_ID_64 && m_bloodPoolScale > 0.0f;
	}
	void UpdateBloodPoolSaveData(StringId64 jointId, F32 scale)
	{
		if (jointId == INVALID_STRING_ID_64)
			return;

		if (m_bloodPoolJoint != INVALID_STRING_ID_64 && scale < m_bloodPoolScale)
			return;

		m_bloodPoolJoint = jointId;
		m_bloodPoolScale = scale;
	}

	CharacterGoreDamageState GetGoreDamageState() const { return m_goreDamageState; }
	void SetGoreDamageState(CharacterGoreDamageState damageState) { m_goreDamageState = damageState; m_goreDamageState.CleanupGoreIds(); }
	virtual bool AllowNonHeadNonLethalGore(F32 damage) const { return true; }

	// Should be refactored maybe...?
	virtual MeleeHistory* GetMeleeHistory() { return nullptr; }
	virtual const MeleeHistory* GetMeleeHistory() const { return nullptr; }
	virtual CharacterHandle GetOtherMeleeCharacterSynched() const { return nullptr; }
	virtual CharacterHandle GetOtherMeleeCharacter() const { return nullptr; }
	virtual bool IsBeingMeleeAttacked() const { return false; }
	virtual bool IsBeggingForLife() const { return false; }

	virtual void SetValidMeleeTarget(bool valid) { m_validMeleeTarget = valid; }
	virtual bool GetValidMeleeTarget() const { return m_validMeleeTarget; }

	//------------------------------------------------------------------------
	// Action packs
	//------------------------------------------------------------------------
	virtual ActionPack* GetEnteredActionPack() const;
	virtual bool IsActionPackReserved(const ActionPack* pActionPack) const;
	virtual bool IsActionPackReserved() const;
	virtual bool IsActionPackEntered() const;
	//------------------------------------------------------------------------

	virtual bool IsExposed(Point_arg posPs) const { return false; }
	virtual bool IsExposed(Point_arg startPs, Point_arg endPs) const { return false; }

	virtual bool IsInWaistDeepWater() const { return false; }

	virtual bool HasPredictOnStairs() const { return false; }
	virtual bool OnStairs() const { return false; }
	virtual bool OnSlope() const { return false; }

	virtual bool HasDetectAudioGround() const { return !IsQuadruped(); }

	virtual bool IsBodyPartOfHead(const RigidBody* pBody) const;
	virtual bool HeadShotsAllowed() const { return true; }

	StringId64 GetDeathAnimApChannel() const { return m_deathAnimApChannel; }
	void SetDeathAnimApChannel(StringId64 channel) { m_deathAnimApChannel = channel; }

	virtual F32 GetNetSnapshotTime() const { return 0.0f; }
	virtual NetRevive* GetNetRevive() { return nullptr; }
	const NetRevive* GetNetRevive() const { return const_cast<Character*>(this)->GetNetRevive(); }

	int FullEffectCount() const { return m_fullEffectCount; }
	void SetFullEffectCount(int val) { m_fullEffectCount = val; }

	TimeFrame LastFullEffectTime() const { return m_lastFullEffectTime; }
	void SetLastFullEffectTime(TimeFrame val) { m_lastFullEffectTime = val; }

	void SpawnCharacterEffects(EffectControlSpawnInfo* aSpawnInfos, U32 numToSpawn);
	void DespawnCharacterEffects();
	virtual void SpawnCharacterEffects() { return; }
	U32 GetMaxNumPaticleEffests() const { return kNumCharacterParticles; }

	ParticleSpawnContextHandle GetCharacterSpawnedEffects() {return m_hSpawnedEffects;}
	void SetCharacterSpawnedEffects(ParticleSpawnContextHandle hEffects){ m_hSpawnedEffects = hEffects; }
	void ResetCharacterSpawnedEffects() { m_hSpawnedEffects.Reset(); }

	const DeferredMeleeHitSpawn& GetDeferredMeleeHitSpawn() { return m_deferredMeleeHitSpawn; }
	void SetDeferredMeleeHitSpawn(const DeferredMeleeHitSpawn& deferredMeleeHitSpawn)
	{
		m_deferredMeleeHitSpawn = deferredMeleeHitSpawn;
	}
	void ResetDeferredMeleeHitSpawn() { m_deferredMeleeHitSpawn.Reset(); }

	bool AreHitEffectsDisabled() const { return m_disableHitEffects; }

	void SetDismembermentAllowed(bool allow) { m_allowDismemberment = allow; }
	bool IsDismembermentAllowed() const { return m_allowDismemberment; }

	virtual const NdGameObject* GetOtherEffectObject(const EffectAnimInfo* pEffectAnimInfo) override;

	virtual StringId64 FindAttachPointNameFromRigidBody(const RigidBody* pBody) const { return INVALID_STRING_ID_64; }

	Point GetCachedTargChestWs() const { return m_cachedTargChestWs; }

	virtual MeshProbe::SurfaceType GetCachedFootSurfaceType() const { return MeshProbe::SurfaceType(); }
	virtual MeshRaycastResult* GetLegMeshRaycastResult(LegIndex i) { return nullptr; }

	// Overlay debugging
	virtual ListArray<StringId64>* GetDynamicAnimList() const { return nullptr; }

	virtual FxRenderMaskHandle GetBloodMaskHandle() const override { return m_bloodMaskHandle; }
	virtual void SetBloodMaskHandle(FxRenderMaskHandle hdl) override { m_bloodMaskHandle = hdl; }
	virtual I32	GetDynamicMaskType() const override { return kDynamicMaskTypeWet; }

	bool ShouldShowHitTickHUD() const { return m_shouldShowHitTicks; }
	void SetShouldShowHitTickHUD(bool shouldShow) { m_shouldShowHitTicks = shouldShow; }

	void SetEffForwarding(NdGameObject* pGo) { m_hEffForwarding = pGo; }

	// Joypad only works for PlayerGameObject.
	virtual const NdPlayerJoypad* GetNdJoypad() const { return nullptr; }

	//------------------------------------------------------------------------
	// Foot Ik.
	//------------------------------------------------------------------------
	virtual FootPlantIk* GetFootPlantIk() const { return nullptr; }
	virtual bool IsFootPlantIkEnabled() const { return false; }
	virtual bool IsFootPlantIkOnStairsEnabled() const { return false; }
	virtual bool IsHeelToePlantIkEnabled() const { return false; }
	virtual MeshRayCastJob::Priority GetLegIkMeshRaycastPriority() const { return MeshRayCastJob::kPriorityDefault; }
	virtual bool GetLegIkOnRagdolls() const { return g_legIkOnRagdolls; }

	//-------------------------------------------------------------------------
	// Rope
	//-------------------------------------------------------------------------
	void SetRope(ProcessPhysRope* pRope);
	const ProcessPhysRope* GetRope() const { return m_hRope.ToProcess(); }
	ProcessPhysRope* GetRope() { return m_hRope.ToMutableProcess(); }
	void KillRope();
	virtual void UpdateRope();

	void EnableRagdollBuoyancy() { m_buoyancyEnabled = true; }
	void DisableRagdollBuoyancy() { m_buoyancyEnabled = false; }

	virtual bool IsClimbingLadder() const { return false; }

	static void OnMeshRayHit(const MeshProbe::CallbackObject* pObject,
							 MeshRaycastResult* pResult,
							 const MeshProbe::Probe* pRequest,
							 const Binding& binding);

	static void OnMeshRayFailed(MeshRaycastResult* pResult, const MeshProbe::Probe* pRequest);

	// m_groundMeshRaycastResult is invalid if KickGroundSurfaceTypeProbe is not called.
	virtual MeshRaycastResult* GetGroundMeshRaycastResult(MeshRaycastType type) override
	{
		GAMEPLAY_ASSERT(type < kMeshRaycastCount);
		return &m_groundMeshRaycastResult[type];
	}

	virtual MeshRaycastResult* GetSplasherMeshRaycastResult(DC::SplasherLimb splasherLimb) override;

	virtual Vector GetGroundMeshRayDir(MeshRaycastType type) const override;

	TimeFrame GetLastTimeGroundSurfaceTypeProbe() const { return m_lastTimeGroundSurfaceTypeProbe; }
	void SetLastTimeGroundSurfaceTypeProbe(TimeFrame f) { m_lastTimeGroundSurfaceTypeProbe = f; }

	void OverrideHandSurfaceType(StringId64 surfaceType) { m_overrideSurfaceType.m_hand = surfaceType; }
	void OverrideFeetSurfaceType(StringId64 surfaceType) { m_overrideSurfaceType.m_feet = surfaceType; }
	void OverrideBodySurfaceType(StringId64 surfaceType) { m_overrideSurfaceType.m_body = surfaceType; }

	virtual F32 GetSurfaceExpand() const;
	virtual F32 GetSurfaceExpandBackpack() const { return GetSurfaceExpand(); }
	virtual bool IsClippingCameraPlane(bool useCollCastForBackpack, float* pNearestApproach = nullptr) const;
	bool IsBlockingCamera() const; // returns true if the character is in between the top camera and its target
	void OverrideCameraSurfaceExpand(F32 surfaceExpand);

	virtual bool IsChild() const { return false; }

	static void ApplyPushHeadIk(JointSet* pJoints, JacobianMap* pJacobianMap, Point lookAtLocation, float blendAmount);
	static void InitializePushHeadIk(Character* pChar, JointTree* pJointTree, JacobianMap* pJacobianMap, StringId64 ikSettingsId);
	static void UpdatePushHeadIk(Character* pChar,
								 JointSet* pJoints,
								 JacobianMap* pJacobianMap,
								 Point lookAtLocation,
								 float blendAmount);
	static void InitializePushArmIk(Character* pChar, JointTree* pJointTree, JacobianMap* pJacobianMap, StringId64 ikSettingsId);
	static void ApplyPushArmIk(JointSet* pJoints, JacobianMap* pJacobianMap, const Locator* pTargetWristLocs, float tt);
	static bool GetHandRelativeToObject(Character* pChar,
										StringId64 handId,
										const Locator& playerLocator,
										Locator& loc,
										AnimStateLayerFilterAPRefCallBack filterCallback,
										bool debugDraw);
	static void UpdatePushArmIk(Character* pChar,
								const BoundFrame& pushAp,
								JointSet* pJoints,
								JacobianMap* pJacobianMap,
								float tt,
								AnimStateLayerFilterAPRefCallBack filterCallback,
								bool debugDraw);

	virtual const PoseTracker* GetPoseTracker() const { return nullptr; }
	virtual PoseTracker* GetPoseTracker() { return nullptr; }

	bool IsBackSplatterBloodAllowed() const { return m_allowBackSplatterBlood; }
	void SetBackSplatterBloodAllowed(bool allow) { m_allowBackSplatterBlood = allow; }

	virtual void NotifyFeatureDbLogout(const FeatureDb* pFeatureDb) {}
	virtual const INearbyEdgeMgr* GetNearbyEdgeMgr() const { return nullptr; }
	virtual INearbyEdgeMgr* GetNearbyEdgeMgr() { return nullptr; }

	virtual EmotionControl* GetEmotionControl() override { return &m_emotionControl; }
	virtual const EmotionControl* GetEmotionControl() const override { return &m_emotionControl; }

	virtual void SaveActionTime(StringId64 actionId) {}

	virtual bool OnNarrowBalanceBeam() const { return false; }

	virtual bool MeetsCriteriaToDebugRecord(bool& outRecordAsPlayer) const override { return true; }

	void SetGasMask(NdAttachableObject* pGasMask) { m_hGasMask = pGasMask; }
	NdAttachableObject* GetGasMask() { return m_hGasMask.ToMutableProcess(); }
	void SetRiotVisor(NdAttachableObject* pRiotVisor) { m_hRiotVisor = pRiotVisor; }
	NdAttachableObject* GetRiotVisor() { return m_hRiotVisor.ToMutableProcess(); }
	const NdAttachableObject* GetRiotVisor() const { return m_hRiotVisor.ToProcess(); }
	bool HasGasMask() const;
	StringId64 SpawnGasMask(StringId64 artGroupId, StringId64 attachPoint, const Locator& offset);

	virtual bool IsRidingHorse(bool allowMounting = true) const { return false; }
	virtual bool NeedToPlayGesturesOnLayer2() const { return false; }

	// HACK HACK HACK
	virtual BoundFrame GetMyHorseSaddleBoundFrame(bool& outIsValid) const { outIsValid = false; return BoundFrame(); }

	virtual StringId64 GetRenderTargetSamplingId() const override { return m_renderTargetSamplingId; }

	ListArray<BuoyancyAccumulator*>& GetBuouancyList() { return m_buoyancyList; }
	const ListArray<BuoyancyAccumulator*>& GetBuouancyList() const { return m_buoyancyList; }

	virtual void SetTrackedRenderTargetParticleHandle(const ParticleHandle *pHPart, const DC::PartBloodSaveType type) override;
	virtual void RestoreRenderTargetsFromHistory();
	virtual void ReleaseRenderTargets();
	virtual void CopyEffectHistory(Character *pTargetChar) const;
	virtual bool AllowRenderTargetEffects() const override;
	virtual bool AllowAttachedEffects() const override;

	void SuppressFootPlantIkThisFrame() { m_suppressFootPlantIkThisFrame = true; }
	bool FootPlantIkSuppressed() const { return m_suppressFootPlantIkThisFrame; }

	const CharacterEffectTracker* GetEffectTracer() const {
		return m_pCharacterEffectTracker;
	}

protected:
	virtual void ConvertToFromCheap();

	virtual bool EnableEffectFilteringByDefault() const override { return true; }
	virtual void OnPlatformMoved(const Locator& deltaLoc) override;
	void SetCoverVulnerabilityTime(TimeFrame coverTime) { m_coverVulnerabilityTime = coverTime; }

	virtual bool NeedsNetController(const SpawnInfo& spawn) const override { return false; }
	virtual bool NeedsHighlightShaderParams() const { return false; }

	virtual Vector GetRvoVelocityWs() const { return GetParentSpace().TransformVector(GetVelocityPs()); }

	virtual bool ShouldCreateRagdollBuoyancy() { return m_allowRagdollBuoyancy; }
	virtual bool BuoyancyUseDynamicWater() const { return m_buoyancyUseDynamicWater; }

	MeshProbe::SurfaceType GetBodyImpactSurfaceType() const;

private:
	void UpdateBuoyancy();

	struct MyWaterQueryContext
	{
		MutableNdGameObjectHandle m_hGo;
		int m_index;
	};
	static WaterQuery::GeneralWaterQuery* FindWaterQuery(const WaterQuery::GeneralWaterQuery::CallbackContext* pContext);


	void SoundBodyImpactCheck(const SoundSurfaceJobData& soundSurfaceData);
	void SoundShoulderImpactCheck(const SoundSurfaceJobData& soundSurfaceData);
	void SoundHandsImpactCheck(const SoundSurfaceJobData& soundSurfaceData);
	void KickSoundImpactCheckJob();

	void InitGoreFilter();

public:
	bool m_isDoomed = false;
	int m_isInProneRegion = 0;
	Quat m_cachedProneOrientation	 = kIdentity;
	Quat m_cachedProneAimOrientation	 = kIdentity;
	Locator m_cachedProneAimCamPivot = kIdentity;
	TimeFrame m_noBulletHitTime = TimeFrameNegInfinity();
	TimeFrame m_noBulletShotTime = TimeFrameNegInfinity();
	TimeFrame m_noBulletFullbodyTime = TimeFrameNegInfinity();
	TimeFrame m_unhittableTime = TimeFrameNegInfinity();
	TimeFrame m_lastCrouchedTime = TimeFrameNegInfinity();
	TimeFrame m_lastNotAimingTime = TimeFrameNegInfinity();
	TimeFrame m_lastTimeQueryWaterAllowed = TimeFrameNegInfinity();

protected:
	StringId64				m_killerUserId;

	StringId64				m_emotionalOverrideAnim;
	HeartRateMonitor*		m_pHeartRateMonitor;
	ScriptPointer<DC::SymbolArray> m_pSoundGestureList;

	I64						m_lastBreathEventFrame;

	TimeFrame				m_lastGotShotTime;
	bool					m_nextArmBloodLower;

	CharacterEffectTracker *m_pCharacterEffectTracker;

private:
	IHealthSystem*			m_pHealthSystem;

	static U32 const		kNumCharacterParticles = 6;
	MutableProcessHandle	m_hCharacterParticle[kNumCharacterParticles];

	TimeFrame				m_listenModeFootEffectTime;

	TimeFrame				m_lastFullEffectTime;
	int						m_fullEffectCount;

	CharacterMeleeImpulse*	m_pMeleeImpulse;

	I16						m_toeJointIndices[2];
	HavokSphereCastJob		m_feetInWaterGroundProbe;

	ParticleSpawnContextHandle m_hSpawnedEffects; // cached pointer to recently spawned effects

	DeferredMeleeHitSpawn m_deferredMeleeHitSpawn;

	const char*				m_characterName;
	const char*				m_characterNameError;
	MutableNdAttachableObjectHandle		m_hGasMask;
	MutableNdAttachableObjectHandle		m_hRiotVisor;
	StringId64				m_renderTargetSamplingId;

	F32						m_overridenSurfaceExpand = -1.0f;
	I64						m_overridenSurfaceExpandFN = -1;

protected:
	CompositeBody*			m_pRagdoll; // Character has second composite body for ragdoll
	HavokRayCastJob			m_ragdollGroundProbes;
	Point					m_cachedTargChestWs;
	U32						m_ragdollNoGroundCounter;
	F32						m_ragdollPendingSettingsDelay;
	F32						m_ragdollTimer; // auto blend out ragdoll after this timer runs out
	F32						m_ragdollDisabledTimer; // if we want to disable any kind of power ragdoll during a time period
	F32						m_ragdollBlendOutTimer;
	StringId64				m_ragdollBlendOutPendingSettings;
	StringId64				m_ragdollDyingSettings;
	bool					m_ragdollNonEffControlled;
	bool					m_ragdollOutOfBroadphase;
	bool					m_disableRagdollBuoyancyInParentBB;
	bool					m_disableThrowableSelfDamage;
	bool					m_disableCameraFadeOut;
	I64						m_disableCameraFadeOutUntilFrame;

#if !FINAL_BUILD
	RagdollReloadData		m_ragdollReloadData;
#endif

	ListArray<BuoyancyAccumulator*>	m_buoyancyList;
	bool					m_buoyancyEnabled;
	bool					m_photoModeFlashlight;

public:
	CharacterFlags			m_characterFlags;

protected:
	struct NearestAllies
	{
		CharacterHandle*		m_ahAlly;
		mutable U32				m_count;
		mutable U32				m_gameFrameNumber;
	};
	NearestAllies*			m_pNearestAlliesByLinearDist;
	NearestAllies*			m_pNearestAlliesByPathDist;
	NearestAllies*			m_pNearestAlliesForDialog;
	U32						m_nearestAlliesCapacity;

	bool					m_isNpc : 1;
	bool					m_isBuddyNpc : 1;
	bool					m_isFemale : 1;
	bool					m_invisibleToAi : 1;
	bool					m_inAiDarknessRegion : 1; //!< enable use of inAiDarkness flag
	bool					m_inAiDarkness : 1;       //!< NPCs can't see this object unless lit by dynamic light
	bool					m_validMeleeTarget : 1;
	bool					m_apRefValid : 1;
	bool					m_allowRagdoll : 1;	// character is allowed to ragdoll when dying
	bool					m_ragdollGroundProbesEnabled : 1;
	bool					m_isPlayingNetDeathAnim : 1;
	bool					m_allowRagdollBuoyancy : 1;
	bool					m_buoyancyUseDynamicWater : 1;
	bool					m_disableHitEffects : 1;
	bool					m_meleeCapsulesOn : 1;
	bool					m_allowDeadRagdollSnapBackToAnimation : 1;
	bool					m_allowDismemberment : 1;
	bool					m_clientControlsRagdollPostAnimBlending : 1;
	bool					m_suppressFootPlantIkThisFrame : 1;
	bool					m_spawnedSingleExitParticleThisFrame : 1;
	bool					m_allowBackSplatterBlood : 1;


	NdSubsystemMgr*			m_pSubsystemMgr;

	FxRenderMaskHandle		m_bloodMaskHandle;

	MutableProcessPhysRopeHandle	m_hRope;
	MutableProcessPhysRopeHandle	m_hPendingRope;

	MutableNdGameObjectHandle		m_hEffForwarding; // yeah, this is a bit lazy ...

	// body impact surface type ray is kicked from anim EFF tag
	TimeFrame				m_lastBodyImpactRayTime;
	float					m_bodyImpactRayLength;
	float					m_bodyImpactRayRadius;
	MeshRaycastResult		m_bodyMeshRaycastResult[kNumBodyImpactSound];

	// shoulder impact surface type ray is kicked from anim EFF tag
	TimeFrame				m_lastShoulderImpactRayTime;
	float					m_shoulderImpactRayLength;
	float					m_shoulderImpactRayRadius;
	MeshRaycastResult		m_shoulderRaycastResult[kNumShoulderImpactSound];

	// hand impact surface type
	MeshRaycastResult		m_handRaycastResult[kArmCount];

	// single mesh ray shot from align of character, to get approximate hand/foot surface type
	MeshRaycastResult		m_groundMeshRaycastResult[kMeshRaycastCount];
	TimeFrame				m_lastTimeGroundSurfaceTypeProbe;

	ndjob::CounterHandle 			m_surfaceTypeProbeCounter;

	JOB_ENTRY_POINT_CLASS_DECLARE(Character, SoundSurfaceTypeJob);

	struct OverrideSurfaceType
	{
		void Reset()
		{
			m_hand.Clear();
			m_feet.Clear();
			m_body.Clear();
		}

		MeshProbe::SurfaceType	m_hand;
		MeshProbe::SurfaceType	m_feet;
		MeshProbe::SurfaceType	m_body;
	};
	OverrideSurfaceType		m_overrideSurfaceType;

	U32						m_meshEffectsSpawned;
	U32						m_maxMeshEffects; // Max number of bullet decals/splatter to spawn per attack

	// wetness meshray probe index.
	U32						m_wetnessProbeIndex;

	CharacterHandle m_hLastAttacker;
	CharacterHandle m_hLastMeleeTarget;

	TimedHighlight m_timedHighlight;

	EmotionControl m_emotionControl;

	ScriptPointer<DC::LegIkConstants> m_pLegIkConstants;

	/************************************************************************/
	/* FLAGGED FOR FUTURE REFACTORING                                       */
	/************************************************************************/
private:
	bool	m_inCover : 1;
	float	m_horzCoverAngle;
	float	m_vertCoverAngle;
	Vector	m_coverDirWs;
	Vector  m_rawCoverDirWs;

	// vulnerability stuff
	DC::Vulnerability m_vulnerability;
	DC::Vulnerability m_animationVulnerabilityOverride; // the vulnerability used when the character is animation controlled
	TimeFrame	m_startVulnerabilityTime;
	TimeFrame	m_coverVulnerabilityTime;

	DC::GoreFilter				m_goreFilter;
	CharacterGoreDamageState	m_goreDamageState;
	StringId64					m_bloodPoolJoint;
	F32							m_bloodPoolScale;

	StringId64	m_deathAnimApChannel;

	U32			m_shotsFired;
	TimeFrame	m_lastShotTime;
	TimeFrame	m_lastShotPlayerTime;
	TimeFrame	m_lastMeleeMoveStartedTime;
	TimeFrame	m_lastMeleeMoveActiveTime;
	TimeFrame	m_lastMeleeOpponentTime;
	TimeFrame	m_lastMeleeAttackActiveTime;
	TimeFrame	m_lastBegForLifeTime;
	Point		m_lastShotFrom;
	Point		m_lastShotTo;
	ProcessMeleeActionHandle m_hLastMeleeExecutionSurprise;

	bool		m_shouldShowHitTicks;
	/************************************************************************/
	/************************************************************************/

private:

	static char m_debugTextInFinal[4096];
	static int m_debugTextInFinalSize;
	static NdAtomicLock m_debugTextLock;

};

/// --------------------------------------------------------------------------------------------------------------- ///
class Character::Active : public Character::ParentClass::Active
{
public:
	typedef Character::ParentClass::Active ParentClass;

	virtual ~Active() override {}
	BIND_TO_PROCESS(Character);

	virtual void Update() override;
};

PROCESS_DECLARE(Character);

/// --------------------------------------------------------------------------------------------------------------- ///
class CharacterSnapshot : public NdGameObjectSnapshot
{
public:
	PROCESS_SNAPSHOT_DECLARE(CharacterSnapshot, NdGameObjectSnapshot);

	explicit CharacterSnapshot(const Process* pOwner, const StringId64 typeId)
		: ParentClass(pOwner, typeId)
	{
		m_characterFlags = 0;
	}

	virtual ProcessMeleeActionHandle GetCurrentMeleeAction() const { return ProcessMeleeActionHandle(); }

	bool IsAimingFirearm()			const { return m_isAimingFirearm; }
	bool IsJumping()				const { return m_jumping; }
	bool IsCrouched()				const { return m_crouched; }
	bool IsProne()					const { return m_prone; }
	bool IsDead()					const { return m_dead; }
	bool IsDown()					const { return m_down; }
	bool IsClimbing()				const { return m_climbing; }
	bool IsHanging()				const { return m_hanging; }
	bool IsFreeRoping()				const { return m_freeRoping; }
	bool IsTakingCover()			const { return m_inCover; }
	bool IsRagdollPhysicalized()	const { return m_isRagdollPhysicalized; }
	bool IsRagdollPowerFadedOut()	const { return m_isRagdollPowerFadedOut; }
	bool IsInStealthVegetation()	const { return m_isInStealthVegetaion; }
	bool IsBuddyNpc()               const { return m_isBuddyNpc; }

	virtual bool IsOnFire() const { return false; }
	virtual bool AllowOnFireDamage() const { return false; }

	TimeFrame		GetLastShotTime()		const { return m_lastShotTime; }
	TimeFrame		GetTimeSinceLastShot()	const { return GetCurTime() - m_lastShotTime; }
	Locator			GetEyeWs()				const { return m_eyeWs; }
	Point			GetTargChestWs()		const { return m_targChestWs; }
	Locator			GetEarWs()				const { return m_earWs; }
	float			GetHealthPercentage()	const { return m_healthPercent; }
	int				GetCurHealth()			const { return m_curHealth; }

	Vector GetTakingCoverDirectionWs() const	{ return m_coverDirWs; }
	Point GetTakingCoverPositionWs() const		{ return m_coverPosWs; }
	float  GetHorzCoverAngle() const			{ return m_horzCoverAngle; }
	float  GetVertCoverAngle() const			{ return m_vertCoverAngle; }

	const TimeFrame		GetTimeSinceLastMeleeOpponent()	const { return GetCurTime() - m_lastMeleeOpponentTime; }
	CharacterHandle GetOtherMeleeCharacter() const { return m_otherMeleeCharacter; }

	MeshProbe::SurfaceType GetCachedFootSurfaceType() const { return m_cachedSurfaceType; }

	virtual Vector GetStickWS(NdPlayerJoypad::Stick stick) const { return kZero; }
	virtual float GetStickSpeed(NdPlayerJoypad::Stick stick) const { return 0.0f; }

	bool IsSprinting() const { return m_isSprinting; }

	union
	{
		struct
		{
			bool m_isAimingFirearm				: 1;
			bool m_jumping						: 1;
			bool m_crouched						: 1;
			bool m_prone						: 1;
			bool m_dead							: 1;
			bool m_down							: 1;
			bool m_climbing						: 1;
			bool m_hanging						: 1;
			bool m_freeRoping					: 1;
			bool m_inCover						: 1;
			bool m_isBuddyNpc					: 1;
			bool m_isRagdollPhysicalized		: 1;
			bool m_isRagdollPowerFadedOut		: 1;
			bool m_isInStealthVegetaion			: 1;
			bool m_curAnimStateFlagSyncState	: 1;
			bool m_isSprinting					: 1;
		};
		U16 m_characterFlags;
	};

	TimeFrame	m_lastShotTime;
	Locator		m_eyeWs;
	Point		m_targChestWs;
	Locator		m_earWs;
	float		m_healthPercent;
	int			m_curHealth;

	Locator	m_coverWallLocWs;
	Vector	m_coverDirWs;
	Point	m_coverPosWs;
	float	m_horzCoverAngle;
	float	m_vertCoverAngle;

	TimeFrame			m_lastMeleeOpponentTime;
	CharacterHandle     m_otherMeleeCharacter;

	Character::StanceState	m_stanceState;

	MeshProbe::SurfaceType m_cachedSurfaceType;
};

/// --------------------------------------------------------------------------------------------------------------- ///
BoxedValue CharacterDynamicStateData(const DC::DynamicStateInfoArray* pInfo,
									 const ArtItemAnim* pPhaseAnim,
									 StringId64 name,
									 float phase,
									 BoxedValue defaultVal);
BoxedValue CharacterDynamicStateData(const Character* pChar,
									 const DC::AnimState* pState,
									 StringId64 name,
									 float phase,
									 BoxedValue defaultVal);
BoxedValue CharacterDynamicStateData(const AnimStateInstance* pInst, StringId64 name, float phase, BoxedValue defaultVal);
BoxedValue CharacterDynamicStateData(const AnimStateInstance* pInst, StringId64 name, BoxedValue defaultVal);
BoxedValue CharacterDynamicStateData(const Character* pChar, StringId64 name, BoxedValue defaultVal);

/// --------------------------------------------------------------------------------------------------------------- ///
float CharacterDynamicStateDataEnablePhase(const AnimStateInstance* pInst, StringId64 name, BoxedValue targetVal);
float CharacterDynamicStateDataEnablePhase(const Character* pChar, StringId64 name, BoxedValue targetVal);
float CharacterDynamicStateDataEnablePhase(const Character* pChar,
										   const DC::AnimState* pState,
										   StringId64 name,
										   BoxedValue targetVal);

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterDynamicStateDataBool(const Character* pChar, StringId64 name, bool defaultVal = false);
float CharacterDynamicStateDataFloat(const Character* pChar, StringId64 name, float defaultVal = 0.0f);
float CharacterDynamicStateDataFloatAtPhase(const Character* pChar,
											StringId64 name,
											float phase,
											float defaultVal = 0.0f);
float CharacterDynamicStateDataBoolEnablePhase(const DC::DynamicStateInfoArray* pInfo, const ArtItemAnim* pPhaseAnim, StringId64 name, bool targetVal = true);
float CharacterDynamicStateDataBoolEnablePhase(const Character* pChar, StringId64 name, bool targetVal = true);
float CharacterDynamicStateDataBoolEnablePhase(const Character* pChar, const DC::AnimState* pState, StringId64 name, bool targetVal = true);
float CharacterDynamicStateDataBoolEnablePhase(const AnimStateInstance* pInst, StringId64 name, bool targetVal = true);
float CharacterDynamicStateDataBlendVal(const AnimStateInstance* pInst, StringId64 name, float defaultVal = 0.0f);
void CharacterDynamicStateDataDebug(const Character* pChar);
bool CharacterDynamicStateDataBool(const NdGameObject* pGo, StringId64 name);

/// --------------------------------------------------------------------------------------------------------------- ///
class CharacterDynamicStateDataBlender : public AnimStateLayer::InstanceBlender<float>
{
public:
	CharacterDynamicStateDataBlender(Character* pChr, StringId64 name, float defaultVal = 0.0f);

	virtual float GetDefaultData() const override;
	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, float* pDataOut) override;
	virtual float BlendData(const float& left,
							const float& right,
							float masterFade,
							float animFade,
							float motionFade) override;

private:
	Character* m_pChr;
	StringId64 m_name;
	float m_defaultVal;
};
