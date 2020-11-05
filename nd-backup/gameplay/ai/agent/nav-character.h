/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "corelib/containers/ringqueue.h"

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/attach-system.h"
#include "ndlib/anim/joint-modifiers/joint-modifier-data.h"
#include "ndlib/anim/pose-matching.h"
#include "ndlib/ndphys/pat.h"
#include "ndlib/tools-shared/patdefs.h"
#include "ndlib/util/maybe.h"
#include "ndlib/util/tracker.h"

#include "gamelib/gameplay/ai/agent/motion-config.h"
#include "gamelib/gameplay/ai/agent/nav-character-adapter.h"
#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/ai/component/ai-script-logger.h"
#include "gamelib/gameplay/ai/controller/nav-anim-controller.h"
#include "gamelib/gameplay/ai/controller/nd-animation-controllers.h"
#include "gamelib/gameplay/ai/controller/nd-locomotion-controller.h"
#include "gamelib/gameplay/ai/nav-character-anim-defines.h"
#include "gamelib/gameplay/character-lip-sync.h"
#include "gamelib/gameplay/character-types.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/look-at-tracker.h"
#include "gamelib/gameplay/nav/action-pack-entry-def.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-command.h"
#include "gamelib/gameplay/nav/nav-handle.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/scriptx/h/nav-character-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ActionPack;
class AnimControl;
class AnimOverlays;
class AnimationControllers;
class CatmullRom;
class CharacterSpeechAnim;
class DoutMem;
class EffectAnimInfo;
class EffectControlSpawnInfo;
class INdScriptedAnimation;
class JointModifiers;
class NavControl;
class NavStateMachine;
class NdAiAnimationConfig;
class NdGameObject;
class NdNetAnimCmdGeneratorBase;
class PathWaypointsEx;
class ProcessSpawnInfo;
class RigidBody;
class ScreenSpaceTextPrinter;
class TraversalActionPack;
class WaterDetector;
struct BitArray128;
struct NdAnimControllerConfig;


namespace Flocking
{
	class FlockingAgent;
}

namespace Nav
{
	struct CombatVectorInfo;
	enum class PlayerBlockageCost : I32;
}

namespace NavUtil
{
	struct MoveDirChangeParams;
}

namespace DC
{
	struct NpcDemeanorDef;
}

FWD_DECL_PROCESS_HANDLE(NavCharacter);

/// --------------------------------------------------------------------------------------------------------------- ///
// Interface for NPC navigation on the nav-mesh and in the world.
class NavCharacter : public Character
{
private: typedef Character ParentClass;
public:
	static CONST_EXPR U32 kMaxThreats = 16;
	static CONST_EXPR U32 kMaxFriends = 16;

	FROM_PROCESS_DECLARE(NavCharacter);

	struct WetPoint
	{
		StringId64 m_jointA;
		StringId64 m_jointB;
		F32 m_uStart;
		F32 m_vStart;
		F32 m_uEnd;
		F32 m_vEnd;
	};

	NavCharacter();
	virtual ~NavCharacter() override;

	virtual Err Init(const ProcessSpawnInfo& info) override;

	virtual Err SetupAnimControl(AnimControl* pAnimControl) override;
	virtual Err SetupAnimOverlays(AnimControl* pAnimControl) { return Err::kOK; }
	virtual void AllocateAnimationLayers(AnimControl* pAnimControl) {}
	virtual void CreateAnimationLayers(AnimControl* pAnimControl) {}
	virtual void CreateAnimationControllers() = 0;
	virtual StringId64 GetAnimControlStartState() const { return INVALID_STRING_ID_64; }
	virtual bool UseRandomIdlePhaseOnSpawn() const { return false; }
	virtual Err ConfigureAnimParams(bool initialConfigure) { return Err::kOK; }
	virtual Err ConfigureDemeanor(bool initialConfigure);
	virtual Err ConfigureCharacter() = 0;
	virtual void ConvertToFromCheap() override;
	virtual void CreateJointModifiers(const ProcessSpawnInfo& info) = 0;
	virtual void OnKillProcess() override;
	virtual ProcessSnapshot* AllocateSnapshot() const override;
	virtual void RefreshSnapshot(ProcessSnapshot* pSnapshot) const override;

	// high level config
	void SetAnimConfigMatrixId(StringId64 animConfigMatrixId) { m_animConfigMatrixId = animConfigMatrixId; }
	StringId64 GetAnimConfigMatrixId() const { return m_animConfigMatrixId; }

	void SetDemeanorMatrixId(StringId64 demeanorMatrixId) { m_demeanorMatrixId = demeanorMatrixId; }
	StringId64 GetDemeanorMatrixId() const { return m_demeanorMatrixId; }

	const NdAiAnimationConfig* GetAnimConfig() const { return m_pAnimConfig; }

	//------------------------------------------------------------------------
	// Demeanors
	//------------------------------------------------------------------------

	// New
	void DefineDemeanors(const DC::NpcDemeanorDef* const* ppDemeanorDefinitions, U32F numDemeanors);
	const DC::NpcDemeanorDef* const* GetDemeanorDefinitions() const { return &m_demeanorDefinitions[0]; }
	const DC::NpcDemeanorDef** GetDemeanorDefinitions() { return &m_demeanorDefinitions[0]; }
	const DC::NpcDemeanorDef* GetCurrentDemeanorDef() const;
	const DC::NpcDemeanorDef* GetPreviousDemeanorDef() const;

	bool IsCommandPending() const;
	bool IsCommandInProgress() const;

	U32F GetNumDemeanors() const;
	StringId64 GetDemeanorId(Demeanor dem) const;
	StringId64 GetDemeanorCategory(Demeanor dem) const;
	const char* GetDemeanorName(Demeanor dem) const;

	virtual Demeanor GetFacialDemeanorWhenShooting() const = 0;
	virtual Demeanor GetCinematicActionPackDemeanor() const = 0;

	virtual StringId64 SidFromDemeanor(const Demeanor& demeanor) const { return INVALID_STRING_ID_64; }

	//------------------------------------------------------------------------
	// Action packs
	//------------------------------------------------------------------------
	ActionPack* GetCurrentActionPack() const;
	ActionPack* GetReservedActionPack() const;
	ActionPack* GetEnteredActionPack() const override;
	TraversalActionPack* GetTraversalActionPack();
	const TraversalActionPack* GetTraversalActionPack() const;
	const ActionPack* GetGoalActionPack() const;
	uintptr_t GetGoalOrPendingApUserData() const;
	bool IsNextWaypointActionPackEntry() const;
	bool HasResolvedEntryForGoalCoverAp() const;

	// overridden methods from Character class
	virtual bool IsActionPackReserved(const ActionPack* pActionPack) const override;
	virtual bool IsActionPackReserved() const override;
	virtual bool IsActionPackEntered() const override;

	bool IsEnteringActionPack() const;
	bool IsUsingTraversalActionPack() const;
	bool IsUsingMultiCinematicActionPack() const;

	bool IsNormalMovementSuppressed() const;

	virtual bool CaresAboutPlayerBlockage(const ActionPack* pActionPack) const;

	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;

	// if we may have changed nav meshes, call this to re-register to the new one
	void ResetNavMesh();
	virtual void BindToRigidBody(const RigidBody* pRigidBody) override;
	virtual void EnterNewParentSpace(const Transform& matOldToNew,
									 const Locator& oldParentSpace,
									 const Locator& newParentSpace);

	// Selects the animation set to use for locomotion
	bool RequestDemeanor(Demeanor demeanor, AI_LOG_PARAM);
	void RequestDemeanor(I32F demeanorIntVal, AI_LOG_PARAM) { RequestDemeanor(Demeanor(demeanorIntVal), logNugget); }

	bool IsExitingActionPack() const;

	Demeanor GetRequestedDemeanor() const;
	virtual I32 GetCurrentDcDemeanor() const = 0;
	virtual I32 GetRequestedDcDemeanor() const = 0;
	Demeanor GetCurrentDemeanor() const { return m_currentDemeanor; }
	Demeanor GetPreviousDemeanor() const { return m_previousDemeanor; }
	void ForceDemeanor(Demeanor demeanor, AI_LOG_PARAM);
	void ForceDemeanor(I32F demeanorIntVal, AI_LOG_PARAM) { ForceDemeanor(Demeanor(demeanorIntVal), logNugget); }
	bool HasDemeanor(I32F demeanorIntVal) const { return m_demeanorDefinitions[demeanorIntVal] != nullptr; }

	void RequestGunState(GunState gunState, bool slow = false, const DC::BlendParams* pBlend = nullptr);
	GunState GetRequestedGunState() const;
	GunState GetCurrentGunState() const;

	StringId64 GetCustomIdle() const;
	void RequestCustomIdle(StringId64 idleAnimId, AI_LOG_PARAM);

	virtual void CheckInjuredByAttack();
	virtual bool ShouldResetInjured() const;
	DC::AiInjuryLevel GetInjuryLevel() const;
	bool IsInjured(DC::AiInjuryLevel level = DC::kAiInjuryLevelMild) const;

	void SetMotionConfig(const MotionConfig& config);
	const MotionConfig& GetMotionConfig() const;
	MotionConfig& GetMotionConfig();
	virtual MotionConfig GetDefaultMotionConfig() const { return MotionConfig(); }

	// change how the character moves (walk, run, sprint, etc) without changing the path
	MotionType GetCurrentMotionType() const;
	StringId64 GetCurrentMtSubcategory() const;
	StringId64 GetRequestedMtSubcategory() const; // this one asks the NavStateMachine instead of the LocomotionController
	StringId64 GetPendingMtSubcategory() const;
	StringId64 GetDefaultMotionTypeSubcategory() const;
	MotionType GetRequestedMotionType() const;
	void SetRequestedMotionType(MotionType type);
	bool HasMoveSetFor(MotionType mt) const;
	bool CanStrafe(MotionType mt) const;

	bool IsMoving() const;
	bool IsStrafing() const;

	virtual float WaterDepth() const override;
	virtual float WaterSurfaceY() const override;
	bool IsWading() const { return m_isWading; }
	virtual bool IsSwimming() const override;
	virtual bool CanSwim() const { return false; }

	NavGoalReachedType GetGoalReachedType() const;
	const Vector GetPathDirPs() const;
	bool IsWaitingForPath() const;
	const TimeFrame GetPathFoundTime() const;

	// Set the position the character should be facing(align). This will decide whether we are walking/running/sprinting or strafing.
	void SetFacePositionPs(Point_arg facingPosition);
	void SetFacePositionWs(Point_arg facingPosition);
	void SetNaturalFacePosition(bool use = true) { m_useNaturalFacePosition = use; }
	bool IsUsingNaturalFacePosition() const { return m_useNaturalFacePosition; }
	const Point GetFacePositionPs() const;
	const Point GetFacePositionWs() const;

	// get the "eye" position which is used for looking and seeing
	virtual const Locator GetEyeWs() const override;
	virtual bool AdjustEyeToLookDirection() const { return true; }

	const Point GetDefaultLookAtPosWs() const;

	Locator GetJointLocBindPoseLs(I32F jointIdx) const;
	Locator GetEyeLocBindPoseLs() const;
	Scalar GetEyeHeightBindPose() const;

	// get the "eye" position without flattening it
	const Locator GetEyeRawWs() const;

	virtual const Point GetAimOriginWs() const { return GetLocator().TransformPoint(m_aimOriginOs); }
	virtual Point GetAimOriginPs() const override { return GetLocatorPs().TransformPoint(m_aimOriginOs); }
	virtual Point GetLookOriginPs() const override { return GetLocatorPs().TransformPoint(m_lookOriginOs.Pos()); }

	Point GetAimOriginOs() const { return m_aimOriginOs; }
	const Locator& GetLookOriginOs() const { return m_lookOriginOs; }

	virtual bool IsBusy(BusyExcludeFlags excludeFlags = kExcludeNone) const = 0;
	virtual bool IsBusyExcludingControllers(BitArray128& excludeControllerFlags, BusyExcludeFlags excludeFlags = kExcludeNone) const = 0;
	virtual bool IsSpawnAnimPlaying() const { return false; }

	virtual bool GetCombatVectorInfo(Nav::CombatVectorInfo& info) const { return false; }
	virtual U32 GetMeleePathPenaltySegments(Segment* aSegmentOut, U32 maxSegments) const { return 0; }

	virtual void ProcessRegionControlUpdate() override;

	//------------------------------------------------------------------------
	// Move Interface
	//------------------------------------------------------------------------

	NavLocation AsReachableNavLocationWs(Point_arg posWs, NavLocation::Type navType) const;

	virtual NavLocation GetNavLocation() const override;

	void MoveTo(NavLocation dest,
				const NavMoveArgs& args,
				const char* sourceFile,
				U32F sourceLine,
				const char* sourceFunc);

	void MoveToActionPack(ActionPack* pAp,
						  const NavMoveArgs& args,
						  const char* sourceFile,
						  U32F sourceLine,
						  const char* sourceFunc);

	void SwitchActionPack(ActionPack* pAp);
	void TeleportIntoActionPack(ActionPack* pAp,
								bool playEntireEnterAnim,
								bool force,
								float fadeTime,
								uintptr_t apUserData = 0);

	void MoveAlongSpline(const CatmullRom* pSpline,
						 float arcStart,
						 float arcGoal,
						 float arcStep,
						 float advanceStartTolerance,
						 const NavMoveArgs& args,
						 const char* sourceFile,
						 U32F sourceLine,
						 const char* sourceFunc);

	void MoveInDirectionPs(Vector_arg dirPs,
						   const NavMoveArgs& args,
						   const char* sourceFile,
						   U32F sourceLine,
						   const char* sourceFunc);
	void MoveInDirectionWs(Vector_arg dirWs,
						   const NavMoveArgs& args,
						   const char* sourceFile,
						   U32F sourceLine,
						   const char* sourceFunc);

	void SteerTo(NavLocation dest,
				 float steerRateDps,
				 const NavMoveArgs& args,
				 const char* sourceFile,
				 U32F sourceLine,
				 const char* sourceFunc);

	bool GetMoveAlongSplineCurrentArcLen(const StringId64 splineNameId, F32& outArcLen) const;

	// stop moving and exit any action packs (get into a standing/idle pose)
	void StopAndStand(float goalRadius, const char* sourceFile, U32F sourceLine, const char* sourceFunc);
	void StopAndFace(Vector_arg faceDirPs,
					 float goalRadius,
					 const char* sourceFile,
					 U32F sourceLine,
					 const char* sourceFunc);

	virtual void OnStopped() {}

	const ActionPackEntryDef* GetResolvedActionPackEntryDef() const;
	virtual bool CanResumeOrProcessNavCommands() const = 0;
	virtual bool CanProcessNewMoveCommands() const { return true; }

	// set how much time must elapse between when the last TAP was exited and the first TAP of a moveTo is entered
	void EnableBlockFirstTapDelay(bool f);

	bool ValidateMoveToLocation(const NavLocation& dest, float yThreshold = -1.0f) const;

	void ResetNavigation(bool playIdle = true);
	virtual void TeleportToBoundFrame(const BoundFrame& bf,
									  const NavLocation::Type navType = NavLocation::Type::kNavPoly,
									  bool resetNavigation = true,
									  bool allowIdleAnim = true);
	void OnTeleport();
	bool IsNavigationInterrupted() const;
	void DisableNavigation(const char* sourceFile, U32F sourceLine, const char* sourceFunc);
	void EnableNavigation(const char* sourceFile, U32F sourceLine, const char* sourceFunc);
	void AbandonInterruptedNavCommand();

	virtual void ResetLegIkForTeleport();
	bool ShouldConstrainLegs() const;

	NavCommand::Status GetNavStatus() const;
	bool IsNavigationInProgress() const;
	bool IsCommandStopAndStand() const;
	bool IsStopped() const;
	bool IsPlayingWorldRelativeAnim() const;
	bool CanProcessNavCommands() const;

	const Point GetDestinationPs() const;
	const Point GetDestinationWs() const;

	const NavLocation GetFinalNavGoal() const;

	bool IsPathValid() const;
	const PathWaypointsEx* GetPathPs() const;
	const PathWaypointsEx* GetPostTapPathPs() const;
	const PathWaypointsEx* GetLastFoundPathPs() const;
	const Locator GetPathTapAnimAdjustLs() const;
	const TraversalActionPack* GetPathActionPack() const;

	virtual void SetupPathContext(Nav::PathFindContext& context) const;
	virtual void SetupPathBuildParams(const Nav::PathFindContext& context, Nav::BuildPathParams& buildParams) const = 0;

	virtual NavMesh::NpcStature GetMinNavMeshStature() const;

	float GetPathLength() const;
	bool IsMovingStalled() const;
	bool IsWaitingForActionPack() const;
	TimeFrame GetMoveStallTimeElapsed() const;

	bool GetAllowAdjustHeadToNav() const { return m_allowAdjustHeadToNav;}
	void SetAllowAdjustHeadToNav(bool f) { m_allowAdjustHeadToNav = f;}
	virtual bool MeleePreventsAdjustToNav() const { return false; }
	virtual bool MeleePreventsAdjustToGround() const { return false; }
	void AddTraversalSkill(U32 skill);
	void RemoveTraversalSkill(U32 skill);
	bool HasTraversalSkill(U32 skill) const;

	void ResetVelocity()
	{
		m_velocityPsHistory.Reset();
		m_smoothedVelocityPs = kZero;
	}

	virtual const Vector GetVelocityWs() const override { return GetParentSpace().TransformVector(m_smoothedVelocityPs); }
	virtual const Vector GetVelocityPs() const override { return m_smoothedVelocityPs; }
	Vector GetInstantaneousVelocityPs() const;

	// different than Length(GetVelocityPs()) because it handles rotating velocities differently (adding the velocity lengths then averaging, rather than the length of the average vector)
	float GetSpeedPs(bool xzOnly = false) const;

	bool HasPredictOnStairs() const override { return IsBuddyNpc(); }
	bool OnStairs() const override;

	virtual bool IsInWallShimmy() const;
	virtual bool IsHanging() const override;
	virtual bool IsClimbing() const override;
	virtual bool IsFreeRoping() const override;
	virtual bool IsInCover() const override;
	virtual bool IsInHighCover() const override;
	virtual bool IsSharingCover() const;
	virtual bool IsInStealthVegetation() const override;
	virtual bool IsInCombat() const = 0;

	virtual Point GetGroundPosPs() const override { return m_groundPositionPs; }
	virtual Vector GetGroundNormalPs() const override { return m_groundNormalPs; }
	const Point GetFilteredGroundPosPs() const { return m_filteredGroundPosPs; }
	const Vector GetFilteredGroundNormalPs() const { return m_filteredGroundNormalPs; }
	void ResetGroundFilter();
	void ResetGroundPositionAndFilter();

	void SuppressGroundProbeFilter() { m_suppressGroundProbeFilter = true; }

	Locator GetDeltaFromLastFramePs() const override
	{
		const Vector deltaPs = GetTranslationPs() - GetLastTranslationPs();
		return Locator(AsPoint(deltaPs), Quat(kIdentity));
	}
	Point GetLastTranslationPs() const { return m_prevLocatorPs.Pos(); }
	const Locator& GetPrevLocatorPs() const { return m_prevLocatorPs; }

	// andHasAppliedToAnimation specifies whether we are checking that the animation is crouching as opposed to the character's desire to crouch
	virtual bool IsInCrouchMode(bool andHasAppliedToAnimation = false) const { return false; }

	virtual bool TryHookCustomLocoStartAnim(const IPathWaypoints* pPath,
											MotionType mt,
											bool strafing,
											CustomLocoStartAnim* pLocoStartAnimOut) const
	{
		return false;
	}

	virtual bool TryHookCustomStrafeDirChange(NavUtil::MoveDirChangeParams* pParams) { return false; }

	virtual bool DisableStrafeMoveMoveAnimStateTransitions() const { return false; }

	INdAiLocomotionController::MoveDir GetStrafeMoveDir(INdAiLocomotionController::MoveDir defaultVal) const { return defaultVal; }

	virtual U32 GetDesiredLegRayCastMode() const override;
	virtual Vector GetIkGroundNormal() const override { return GetParentSpace().TransformVector(GetFilteredGroundNormalPs()); }

	virtual Flocking::FlockingAgent* const GetFlockingAgent() { return nullptr; }

	//------------------------------------------------------------------------
	// Death Interface
	//------------------------------------------------------------------------
	void DieFromExplosion(Vector_arg fromDirection, Point_arg impactPoint);
	void DieFromPistol(Vector_arg fromDirection, Point_arg impactPoint);
	void DieFromShotgun(Vector_arg fromDirection, Point_arg impactPoint);
	void DieFromMachineGun(Vector_arg fromDirection, Point_arg impactPoint);

	//------------------------------------------------------------------------
	// Weapon Interface
	//------------------------------------------------------------------------
	virtual NdGameObject* LookupPropByName(StringId64 propId) { return nullptr; }
	virtual StringId64 GetWeaponAttachId(const I32 weaponAnimType) const { return INVALID_STRING_ID_64; }
	virtual void SpawnHeldGrenade(const StringId64 weaponId = INVALID_STRING_ID_64, bool leftHand = false) {}
	virtual bool HoldingGrenade() const { return false; }
	virtual void HoldOnToWeaponDuringDeath() {}
	virtual I32 GetDcWeaponAnimType() const { return 0; }
	virtual void NotifyReleaseHeldThrowable(bool missingEff) {}
	virtual void NotifyDrawHeldThrowable(bool missingEff) {}

	virtual U32 GetThreatsPs(Point* aPosPs, Point* aFuturePosPs) const { return 0; }
	virtual U32 GetFriendsPs(Point* aPosPs) const { return 0; }

	//------------------------------------------------------------------------
	// Navigation and Positioning
	//------------------------------------------------------------------------
	const	NavControl*	GetNavControl() const { return m_pNavControl; }
			NavControl*	GetNavControl() { return m_pNavControl; }

	float GetCurrentNavAdjustRadius() const;
	float GetDesiredNavAdjustRadius() const;
	float GetMaximumNavAdjustRadius() const;
	float GetMovingNavAdjustRadius() const;
	float GetIdleNavAdjustRadius() const;
	TimeFrame GetLastUpdatedBlockedApsTime() const { return m_lastUpdatedBlockedApsTime; }

	virtual F32 GetInterCharacterDepenRadius() const;
	virtual F32 GetDefaultCharacterDepenRadius() const { return -1.0f; }
	void ResetInterCharacterDepenRadius() { SetInterCharacterDepenRadius(GetDefaultCharacterDepenRadius()); }
	void SetInterCharacterDepenRadius(F32 r) { m_interCharDepenRadius = r; }

	virtual F32 GetMinNavSpaceDepenetrationRadius() const { return 0.0f; }

	void UpdateDepenSegmentScale();
	Segment GetDepenetrationSegmentPs(Point basePoint, bool allowCapsuleShape = true) const;
	Segment GetDepenetrationSegmentWs(Point basePoint, bool allowCapsuleShape = true) const;
	float GetMaxDepenSegmentComponent() const;
	virtual float GetApBlockageRadius() const { return 0.9f; }
	virtual bool AllowNavMeshDepenetration() const { return true; }

	virtual float GetOnSplineRadius() const { return 1.0f; }

	bool AdjustMoveToNavSpacePs(Point_arg basePosPs,
								Point_arg desiredPosPs,
								float adjustRadius,
								const NavBlockerBits& obeyedBlockers,
								const Sphere* offsetsPs,
								U32F offsetCount,
								Point* pResultPosPsOut) const;

	bool IsPointClearForMePs(Point_arg posPs, bool staticCheckOnly) const;
	bool WasMovementConstrained() const					{ return m_wasMovementConstrained; }

	virtual bool CanEvade() const { return false; }
	virtual void OnSuccessfulEvade();

	virtual bool CanBeShoved(const Character* pShover) const { return false; }
	virtual bool IsBeingShoved() const { return false; }
	virtual bool RequestShove(Character* pPlayer, Point_arg originWs, Vector_arg motionDirWs) { return false; }

	void SetPusherPlanePs(const Plane& planePs) { m_pusherPlanePs = planePs; }
	const Plane& GetPusherPlanePs() const { return m_pusherPlanePs; }

	virtual bool ShouldKickDownDoor(const TraversalActionPack* pTap) const { return false; }

	//------------------------------------------------------------------------
	// Animation Interface
	//------------------------------------------------------------------------
	virtual NdAnimControllerConfig* GetAnimControllerConfig() { return nullptr; }
	virtual const NdAnimControllerConfig* GetAnimControllerConfig() const { return nullptr; }
	virtual const char* GetOverlayBaseName() const override;

			AnimationControllers*	GetAnimationControllers()		{ return PunPtr<AnimationControllers*>(m_pAnimationControllers); }
	const	AnimationControllers*	GetAnimationControllers() const	{ return PunPtr<const AnimationControllers*>(m_pAnimationControllers); }

			NdAnimationControllers*	GetNdAnimationControllers()			{ return m_pAnimationControllers; }
	const	NdAnimationControllers*	GetNdAnimationControllers() const	{ return m_pAnimationControllers; }

	virtual	JointModifiers*			GetJointModifiers() override		{ return m_pJointModifiers; }
	virtual const	JointModifiers*	GetJointModifiers() const override	{ return m_pJointModifiers; }

	virtual StringId64 GetCharacterClassOverlay() const { return INVALID_STRING_ID_64; }
	virtual void ApplyCharacterOverlays(AnimOverlays* pOverlays) const {}

	//------------------------------------------------------------------------
	// Health and Damage
	//------------------------------------------------------------------------
	virtual void DealDamage(F32 damage, bool isExplosion = false, bool ignoreInvincibility = false, bool isBullet = false);
	virtual void OnDeath();
	virtual void SetPlayerPushInflateRadius(float inflateRadius) { m_inflateRadius = inflateRadius; }
	virtual float GetInflateRadius() const { return m_inflateRadius; }
	virtual StringId64 GetDeathLoopAnimId() const { return INVALID_STRING_ID_64; }

	//------------------------------------------------------------------------
	// Ragdoll
	//------------------------------------------------------------------------
	virtual bool CanRagdoll() const override;
	F32  GetRagdollDelay() const;
	bool VehicleDeathRagdollEffectTriggered() const { return m_vehicleDeathRagdollEff; }
	void TriggerVehicleDeathRagdollEffect() { m_vehicleDeathRagdollEff = true; }
	bool HasDiedInVehicle() const { return m_diedInVehicle; }
	void SetDiedInVehicle(bool inVehicle) { m_diedInVehicle = inVehicle; }
	void SetKeepCollisionCapsulesWhenNotRagdolling(bool b) { m_keepCollisionCapsulesIfNotRagdolling = b; }
	bool GetKeepCollisionCapsulesWhenNotRagdolling() const { return m_keepCollisionCapsulesIfNotRagdolling; }
	void SetRagdollDelay(F32 time);
	bool KillIfCantRagdoll() const;
	void SetKillIfCantRagdoll(bool okToKill);
	bool ForceRagdollOnExplosionDeath() const;
	void SetForceRagdollOnExplosionDeath(bool force);

	// Scripted Animation
	void RequestEnterScriptedAnimationState();
	void RequestStopScriptedAnimationState();
	void RequestExitScriptedAnimationState();
	bool IsInScriptedAnimationState() const;			// returns true if actually "in" the scripted animation state

	void SetWetPoints(WetPoint* pWetPoints);

	//------------------------------------------------------------------------
	// Search
	//------------------------------------------------------------------------
	bool IsAnimationWalkToIdle() const;

	// Update the potential move and update the requested movement deltas in the movement root.
	virtual void Update();

	virtual void PostAnimUpdate_Async() override;
	virtual void PostAnimBlending_Async() override;

	virtual void PostJointUpdate_Async() override;
	virtual void UpdateRootLocator();
	virtual void PostRootLocatorUpdate() override;

	virtual void PostRenderUpdateAfterHavokStep(bool allowPoiCollect);

	void RequestPathThroughPlayer() { m_pathThroughPlayer = true; }
	void ClearPathThroughPlayer() { m_pathThroughPlayer = false; }
	bool ShouldPathThroughPlayer() const { return m_pathThroughPlayer; }

	void RequestPushPlayerAway() { m_requestPushPlayer = true; }
	virtual void DisablePushPlayerAway() { m_requestPushPlayer = false; }
	bool WantsToPushPlayerAway() const { return m_requestPushPlayer; }

	bool ConsumePushPlayerRequest()
	{
		const bool ret		= m_requestPushPlayer;
		m_requestPushPlayer = false;
		return ret;
	}

	virtual bool AllowAdjustToPlayer() const { return true; }
	virtual bool AllowAdjustToOtherCharacters() const { return true; }

	// Draw current status of character navigation.
	virtual void DebugDraw(WindowContext* pDebugWindowContext, ScreenSpaceTextPrinter* pPrinter) const override;
	void DebugDrawWetness() const;

	// Log debug text to a buffer
	//DoutMem* GetDebugLog() const { return m_pLog; } //use GetChannelDebugLog->DumpAllToBuffer() if you need all logs
	DoutMemChannels* GetScriptDebugLog() const { return m_pScriptLogger ? m_pScriptLogger->GetScriptDebugLog() : nullptr; }
	AiScriptLogger* GetScriptLogger() const { return m_pScriptLogger; }
	DoutMemChannels* GetChannelDebugLog() const { return m_pChannelLog; }

	const Vector& GetGroundAdjustment() const { return m_groundAdjustment; }

	virtual const Pat GetCurrentPat() const override { return m_currentPat; }

	virtual F32 GetNetSnapshotTime() const override;

	NdNetAnimCmdGeneratorBase* m_pNetAnimCmdGenerator;

	virtual bool IsInWaistDeepWater() const override;

	void SetShouldUpdateRegionControl(bool shouldUpdate) { m_shouldUpdateRegionControl = shouldUpdate; }
	bool ShouldUpdateRegionControl() const { return m_shouldUpdateRegionControl; }
	bool IsInRagdollStumble() const { return m_ragdollStumble; }

	void SetEnablePlayerNpcCollision(bool allow) { m_disablePlayerNpcCollision = !allow; }
	bool IsPlayerNpcCollisionDisabled() const { return m_disablePlayerNpcCollision; }

	virtual bool ConfiguredForSplashers() const { return false; }
	virtual bool ConfiguredForExtraAnimLayers() const { return false; }
	virtual bool ConfiguredForNavigation() const { return false; }
	virtual U32	 GetDefaultTraversalSkillMask() const { return 0; }

	virtual void DebugDrawActionPack(const ActionPack* pActionPack, const ActionPackResolveInput* pInput = nullptr) const
	{
	}

	// Hack to allow this class in Ndlib to alloc memory from NpcManager in Game
	/// Begin hack
	typedef void* (*PFnAllocateDebugMem)(U32*, U32 type);
	typedef void (*PFnFreeDebugMem)(void*, U32 type);

	static PFnAllocateDebugMem s_pFnAllocDebugMem;
	static PFnFreeDebugMem     s_pFnFreeDebugMem;

	enum
	{
		kDebugMemCharLog,
		kDebugMemScriptLog,
		kDebugMemLogicControlLog,
		kDebugMemCount
	};

	/// End hack

	const NavStateMachine* GetNavStateMachine() const { return m_pNavStateMachine; }

	void ConfigureNavigationHandOff(const NavAnimHandoffDesc& desc,
									const char* sourceFile,
									U32F sourceLine,
									const char* sourceFunc);
	U32F GetRegisteredNavAnimHandoffs(NavAnimHandoffDesc* pHandoffsOut, U32F maxHandoffsOut) const;
	bool PeekValidHandoff(NavAnimHandoffDesc* pHandoffOut) const;
	NavAnimHandoffDesc PopValidHandoff();

	void NotifyApReservationLost(const ActionPack* pAp);
	virtual void NotifyNewPath(const IPathWaypoints& oldPathPs, const IPathWaypoints& newPathPs) const {}

	BoundFrame AdjustBoundFrameToUpright(const BoundFrame& bf) const;
	Locator AdjustLocatorToUprightPs(const Locator& locPs) const;
	Locator AdjustLocatorToUprightWs(const Locator& locWs) const;

	virtual const AnkleInfo* GetAnkleInfo() const override { return &m_ankleInfoLs; }

	virtual bool ShouldFailTaskOnDeath() const = 0;

	virtual bool IsGroundSurfaceTypeProbeDisabled() const { return false; }
	virtual MeshProbe::SurfaceType GetCachedFootSurfaceType() const override;

	void CheckForApReservationSwap();

	INavAnimController* GetActiveNavAnimController();
	const INavAnimController* GetActiveNavAnimController() const;
	Point GetNavigationPosPs() const;
	bool IsNextWaypointTypeStop() const;
	void ForceDefaultIdleState();

	virtual bool ShouldForcePlayerPush() const { return false; }

	virtual U32F GetNumInstancesForBaseLayer() const { return 8; }
	virtual U32F GetNumTracksForBaseLayer() const { return 3; }
	virtual U32F GetNumSnapshotsForBaseLayer() const { return 256; }

	virtual const PoseTracker* GetPoseTracker() const override { return m_pPoseTracker; }
	virtual PoseTracker* GetPoseTracker() override { return m_pPoseTracker; }

	void PatchPathStartPs(Point_arg newStartPs);

	Nav::StaticBlockageMask GetObeyedStaticBlockers() const;

	virtual Maybe<Vector> GetOverrideChosenFacingDirPs() const { return MAYBE::kNothing; }
	virtual void OnConstrainedByNavmesh(Point_arg startPos, Point_arg desiredPos, Point_arg resultPos) {}

	virtual void UpdateWetness() override;
	virtual bool ShouldUpdateWetness() const override { return true; }

	void SetRequestSnapToGround(bool snapToGround) { m_requestSnapToGround = snapToGround; }

	void SetUndirectedPlayerBlockageCost(Nav::PlayerBlockageCost undirectedPlayerBlockageCost);
	void ClearUndirectedPlayerBlockageCost();
	Nav::PlayerBlockageCost GetUndirectedPlayerBlockageCost() const;

	void SetDirectedPlayerBlockageCost(Nav::PlayerBlockageCost directedPlayerBlockageCost);
	void ClearDirectedPlayerBlockageCost();
	Nav::PlayerBlockageCost GetDirectedPlayerBlockageCost() const;

	void SetCareAboutPlayerBlockageDespitePushingPlayer();
	void ClearCareAboutPlayerBlockageDespitePushingPlayer();
	bool CareAboutPlayerBlockageDespitePushingPlayer() const;

	virtual bool IsAnimationControlled() const = 0;
	const WaterDetector* GetWaterDetectorAlign() const { return m_pWaterDetectorAlign; }
	virtual bool PreventAdjustToGround() const { return false; }

	void SetDepenCapsuleScaleScriptOverride(float scale)
	{
		m_depenetrationSegmentScaleScriptTarget = scale;
		m_depenSegmentInScriptedOverride = true;
	}

protected:
	static const U32F kMaxAdjustToNavIndexCount = 2;

	// Issues a move command to move the character to the desired location in the parent space.
	// The location will be moved to the closest point on the navigation mesh if outside.
	void IssueNavCommand(const NavCommand& cmd);

	virtual void SetupNavControl(NavControl* pNavControl) {}
	virtual bool ShouldAllowTeleport() const;
	virtual void HandleTriggeredEffect(const EffectAnimInfo* pEffectAnimInfo) override;
	virtual void PopulateEffectControlSpawnInfo(const EffectAnimInfo* pEffectAnimInfo, EffectControlSpawnInfo& effInfo) override;

	void	SetAimOriginAttachIndex(AttachIndex idx) { m_aimOriginAttachIndex = idx; }
	void    SetAdjustToNavAttachIndex(U32F i, AttachIndex index)
	{
		NAV_ASSERT(i < kMaxAdjustToNavIndexCount);
		if (i < kMaxAdjustToNavIndexCount)
			m_adjustToNavIndex[i] = index;
	}

	bool TeleportToPlayersNavMesh();

	bool IsUsingBadTraversalActionPack() const;

	void SetGroundPositionPs(Point_arg posPs) { m_groundPositionPs = posPs; }
	void SetGroundNormalPs(Vector_arg normPs) { m_groundNormalPs = normPs; }
	void SetMinGroundAdjustSpeed(F32 speed) { m_minGroundAdjustSpeed = speed; }
	void SetAllowFall(bool allow) { m_allowFall = allow; }
	void SetCurrentPat(Pat pat)
	{
		NAV_ASSERT(pat.GetSurfaceType() < Pat::kMaxSurfaceType);
		m_currentPat = pat;
	}

	virtual bool CaresAboutHavingNoMesh() const { return false; }
	virtual bool EmergencyTeleportFallRecovery(float timeFalling, Locator& outDesiredLocPs);

	NavStateMachine* GetNavStateMachine() { return m_pNavStateMachine; }

	ILookAtTracker::Priority GetCurrentLookAtPriority() const;

	Quat LastEyeAnimRotWs() const { return m_lastEyeAnimRotWs; }
	void SetLastEyeAnimRotWs(Quat_arg val) { m_lastEyeAnimRotWs = val; }

	virtual const Point AdjustToOtherCharacters(Point_arg oldPos,
											Point_arg desiredPos,
											Scalar_arg radius,
											bool* pHeadOnCollisionDetected,
											bool hardAdjust,
											bool adjustToPlayer = true) const = 0;

	void ConfigRagdollGroundProbes(RagdollGroundProbesConfig& config) const;

	virtual const Pat* GetLadderPat() const override { return &m_ladderPat; }
	void SetLadderPat(const Pat newPat) { m_ladderPat = newPat; }

	virtual bool ForceInShallowWater() const { return false; }

	virtual Err InitJointLimits() { return Err::kOK; }

	NdAiAnimationConfig* GetAnimConfig() { return m_pAnimConfig; }

	virtual void UpdateVulnerability(bool isAnimationControlled);

	virtual bool ShouldUseCrawlGroundProbes() const { return false; }

private:
	virtual bool AlwaysWantAdjustHeadToNav() const { return false; }

	virtual INdScriptedAnimation* CreateScriptedAnimation() = 0;
	virtual NdAnimationControllers* CreateAnimationControllersCollection() = 0;

	void Interrupt(const char* sourceFile, U32F sourceLine, const char* sourceFunc);
	virtual void LogNavInterruption(const char* sourceFile, U32F sourceLine, const char* sourceFunc) const;

	virtual void UpdateNavControlLogic() {}

	const Point AdjustToGround(Point_arg oldPos, Point_arg desiredPos);
	Point FilterGroundPosition(Point_arg targetGroundPosPs, Vector_arg targetGroundNormalPs);

	virtual Point AdjustToPlayer(Point_arg oldPos) { return oldPos; }
	void AdjustToUpright(const AnimControl* pAnimControl);

	// Decide which type of movement we should commit to.
	void BeginMoveToImplPs(Point_arg destPs);
	void SelectNextDestination();

	virtual bool ShouldUseGroundHugRotation() const { return false; }

	Point AdjustRootToNavSpace(Point_arg currentPosPs,
							   Point_arg adjustedPosPs,
							   float navMeshAdjustFactor,
							   float deltaTime,
							   bool shouldAdjustToPlayer);

	bool	m_shouldUpdateRegionControl		: 1;
	bool	m_wasMovementConstrained		: 1;
	bool	m_disableAdjustToCharBlockers	: 1;
	bool	m_allowAdjustHeadToNav			: 1; // should be "adjust joints" to nav map or allowAdjustToNavMapWithSelectedJoints
	bool	m_keepCollisionCapsulesIfNotRagdolling : 1;
	bool	m_killIfCantRagdoll				: 1;
	bool	m_forceRagdollOnExplosionDeath	: 1;
	bool	m_ragdollStumble				: 1;
	bool	m_diedInVehicle					: 1;
	bool	m_vehicleDeathRagdollEff		: 1;
	bool	m_allowFall						: 1;
	bool	m_requestSnapToGround			: 1;
	bool	m_requestPushPlayer				: 1;
	bool	m_pathThroughPlayer				: 1;
	bool	m_disablePlayerNpcCollision		: 1;
	bool	m_useNaturalFacePosition		: 1;
	bool	m_isWading						: 1;
	bool	m_suppressGroundProbeFilter		: 1;

	U8		m_disableNavigationRefCount;	// if nonzero, navigation is disabled

	NdAiAnimationConfig*		m_pAnimConfig;

	// Pathing variables
	NavControl*					m_pNavControl;				// object for dealing with nav meshes and nav maps
	// Nav state variables
	NavStateMachine*			m_pNavStateMachine;
	// Character animation attributes
	NdAnimationControllers*		m_pAnimationControllers;			//!< All animation controlling objects.
	JointModifiers*				m_pJointModifiers;					//!< All joint modifying objects.
	PoseTracker*				m_pPoseTracker;
	WetPoint*					m_pWetPoints;
	WaterDetector*				m_pWaterDetectors;
	WaterDetector*				m_pWaterDetectorAlign;

	void*						m_pLogMem;
	AiScriptLogger*				m_pScriptLogger;
	mutable DoutMemChannels*    m_pChannelLog;

	MotionType					m_requestedMotionType;

	float						m_inflateRadius;
	float						m_adjustToOtherNpcsTime;
	float						m_adjustToOtherNpcsDelayTime;
	float						m_adjustJointsToNavSmoothing;
	float						m_fallSpeed;
	float						m_interCharDepenRadius;
	float						m_delayRagdollTime;
	float						m_secondsNoNavMesh;

	// General variables
	Plane					m_pusherPlanePs;
	Vector					m_groundAdjustment;
	Vector					m_waterAdjustmentWs;
	Pat						m_ladderPat;

	Point					m_groundPositionPs;
	Vector					m_groundNormalPs;
	Point					m_filteredGroundPosPs;
	Vector					m_filteredGroundNormalPs;
	SpringTracker<Vector>		m_groundNormalSpring;
	TwoWaySpringTracker<float>	m_groundHeightDeltaSpring;	// NOTE: use TwoWaySpringTracker cause I dont want any overshoot.

	F32						m_minGroundAdjustSpeed;

	INdScriptedAnimation*	m_pScriptedAnimation;

	AttachIndex				m_aimOriginAttachIndex;
	AttachIndex				m_lookAttachIndex;
	AttachIndex				m_adjustToNavIndex[kMaxAdjustToNavIndexCount];

	Point					m_aimOriginOs;
	Locator					m_lookOriginOs;

	MotionConfig	m_motionConfig;
	LipSync			m_lipSync;

	Locator			m_animDesiredAlignPs;
	Locator			m_desiredLocPs;

protected:
	// Directional settings for different body parts

	RingQueue<Vector>		m_velocityPsHistory;
	Vector					m_smoothedVelocityPs;
	SpringTrackerPoint		m_effectiveLookAtTracker;

	bool					m_handsProbesShot;
	DC::AiInjuryLevel		m_curInjuryLevel;

	CharacterSpeechAnim*	m_pCharacterSpeechAnim;
	Demeanor				m_previousDemeanor;
	Demeanor				m_currentDemeanor;

	Pat						m_currentPat;

	// Local Space Z-capsule extensions for nav mesh depenetration
	F32						m_depenetrationOffsetFw;
	F32						m_depenetrationOffsetBw;
	F32						m_depenetrationSegmentScale;
	F32						m_depenetrationSegmentScaleScriptTarget;
	bool					m_depenSegmentInScriptedOverride;

	float					m_secondsFalling;

	Point					m_lastUpdatedBlockedApsPos;
	TimeFrame				m_lastUpdatedBlockedApsTime;

private:
	Locator	m_prevLocatorPs;

	Quat m_lastEyeAnimRotWs; // this is used so where the eyes are animated to look at are the default look position
							 // when doing the track aiming for look at animations. right now this information is
							 // duplicated with what's in the joint modifier data

	Point m_desiredFacePositionPs;

	const static I32			kMaxDemeanorSlots = 16;
	U32							m_numDemeanors;
	const DC::NpcDemeanorDef*	m_demeanorDefinitions[kMaxDemeanorSlots];
	StringId64					m_animConfigMatrixId;
	StringId64					m_demeanorMatrixId;

	AnkleInfo m_ankleInfoLs;

	static CONST_EXPR size_t kMaxKnownHandoffs = 8;
	NavAnimHandoffDesc m_knownHandoffs[kMaxKnownHandoffs];
};

PROCESS_DECLARE(NavCharacter);

FWD_DECL_PROCESS_HANDLE(NavCharacter);

/// --------------------------------------------------------------------------------------------------------------- ///
class NavCharacterSnapshot : public CharacterSnapshot
{
public:
	PROCESS_SNAPSHOT_DECLARE(NavCharacterSnapshot, CharacterSnapshot);

	explicit NavCharacterSnapshot(const Process* pOwner, const StringId64 typeId) : ParentClass(pOwner, typeId) {}

	float m_curNavAdjustRadius	 = 0.0f;
	float m_maxNavAdjustRadius	 = 0.0f;
	float m_interCharDepenRadius = 0.0f;
	float m_inflateRadius		 = 0.0f;
	Segment m_depenSegmentPs	 = Segment(kOrigin, kOrigin);

	// NsmPathFindBlockerFilter stuff
	TPathWaypoints<16> m_currentPathPs;
	bool m_followingPath = false;
	bool m_interruptedOrMovingBlind = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
const ActionPackResolveInput MakeDefaultResolveInput(const NavCharacterAdapter& pNavChar);
