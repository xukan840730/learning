/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/gameplay/ai/controller/nd-animation-controllers.h"

#include "game/ai/controller/animation-controller-config.h"
#include "game/ai/controller/carried-controller.h"
#include "game/ai/controller/cinematic-controller.h"
#include "game/ai/controller/climb-controller.h"
#include "game/ai/controller/cover-controller.h"
#include "game/ai/controller/dodge-controller.h"
#include "game/ai/controller/evade-controller.h"
#include "game/ai/controller/face-controller.h"
#include "game/ai/controller/flock-controller.h"
#include "game/ai/controller/hit-controller.h"
#include "game/ai/controller/idle-controller.h"
#include "game/ai/controller/infected-controller.h"
#include "game/ai/controller/investigate-controller.h"
#include "game/ai/controller/leap-controller.h"
#include "game/ai/controller/locomotion-controller.h"
#include "game/ai/controller/melee-action-controller.h"
#include "game/ai/controller/perch-combat-controller.h"
#include "game/ai/controller/performance-controller.h"
#include "game/ai/controller/push-controller.h"
#include "game/ai/controller/ride-horse-controller.h"
#include "game/ai/controller/script-controller.h"
#include "game/ai/controller/search-controller.h"
#include "game/ai/controller/swim-controller.h"
#include "game/ai/controller/tap-controller.h"
#include "game/ai/controller/turret-controller.h"
#include "game/ai/controller/vehicle-controller.h"
#include "game/ai/controller/weapon-controller.h"
#include "game/vehicle/horse-component.h"

/// --------------------------------------------------------------------------------------------------------------- ///
enum AnimationControllerTypes
{
	kLocomotionController,
	kClimbController,
	kCoverController,			// Action Pack controller
	kHitController,
	kPerformanceController,
	kMeleeActionController,
	kIdleController,
	kWeaponController,
	kCinematicController,		// Action Pack controller
	kTurretController,			// Action Pack controller
	kPerchController,			// Action Pack controller
	kTraversalController,		// Action Pack controller
	kInvestigateController,		// Action Pack controller
	kSearchController,
	kFaceController,
	kRideHorseController,
	kHorseJumpController,
	kEvadeController,
	kSwimController,
	kVehicleController,
	kDodgeController,
	kPushController,
	kInfectedController,
	kLeapController,
	kEntryController,
	kFlockController,
	kCarriedController,

	kBeginNpcScriptControllers,
		kNpcScriptFullBodyController = kBeginNpcScriptControllers,
		kBeginNpcScriptGestureControllers,
			kNpcScriptGestureController0 = kBeginNpcScriptGestureControllers,
			kNpcScriptGestureController1,
			kNpcScriptGestureController2,
			kNpcScriptGestureController3,
		kEndNpcScriptGestureControllers,
	kEndNpcScriptControllers = kEndNpcScriptGestureControllers,

	kNpcAnimationControllerTypeCount = kEndNpcScriptControllers,
	kNpcScriptControllerTypeCount = kEndNpcScriptControllers - kBeginNpcScriptControllers,
	kNpcGestureControllerTypeCount = kEndNpcScriptGestureControllers - kBeginNpcScriptGestureControllers,
};

/// --------------------------------------------------------------------------------------------------------------- ///
/// Class AnimationControllers:
/// --------------------------------------------------------------------------------------------------------------- ///
class AnimationControllers : public NdAnimationControllers
{
public:
	AnimationControllers() : NdAnimationControllers(kNpcAnimationControllerTypeCount) {}

	IAiLocomotionController* GetLocomotionController() override		{ return (IAiLocomotionController*)m_controllerList[kLocomotionController]; }
	IAiCoverController* GetCoverController()						{ return (IAiCoverController*)m_controllerList[kCoverController]; }
	IAiHitController* GetHitController() override					{ return (IAiHitController*)m_controllerList[kHitController]; }
	IAiPerformanceController* GetPerformanceController()			{ return (IAiPerformanceController*)m_controllerList[kPerformanceController]; }
	IAiMeleeActionController* GetMeleeActionController()			{ return (IAiMeleeActionController*)m_controllerList[kMeleeActionController]; }
	IAiIdleController* GetIdleController()							{ return (IAiIdleController*)m_controllerList[kIdleController]; }
	IAiWeaponController* GetWeaponController() override				{ return (IAiWeaponController*)m_controllerList[kWeaponController]; }
	IAiCinematicController* GetCinematicController() override		{ return (IAiCinematicController*)m_controllerList[kCinematicController]; }
	IAiTurretController* GetTurretController()						{ return (IAiTurretController*)m_controllerList[kTurretController]; }
	IAiPerchController* GetPerchController()						{ return (IAiPerchController*)m_controllerList[kPerchController]; }
	TapController* GetTraversalController() override				{ return (TapController*)m_controllerList[kTraversalController]; }
	IAiInvestigateController* GetInvestigateController()			{ return (IAiInvestigateController*)m_controllerList[kInvestigateController]; }
	AiSearchController* GetSearchController()						{ return (AiSearchController*)m_controllerList[kSearchController]; }
	IAiFaceController* GetFaceController()							{ return (IAiFaceController*)m_controllerList[kFaceController]; }
	IAiRideHorseController* GetRideHorseController()				{ return (IAiRideHorseController*)m_controllerList[kRideHorseController]; }
	HorseJumpController* GetHorseJumpController()					{ return (HorseJumpController*)m_controllerList[kHorseJumpController]; }
	IAiEvadeController* GetEvadeController()						{ return (IAiEvadeController*)m_controllerList[kEvadeController]; }
	IAiSwimController* GetSwimController() override					{ return (IAiSwimController*)m_controllerList[kSwimController]; }
	IAiVehicleController* GetVehicleController()					{ return (IAiVehicleController*)m_controllerList[kVehicleController]; }
	IAiDodgeController* GetDodgeController()						{ return (IAiDodgeController*)m_controllerList[kDodgeController]; }
	IAiPushController* GetPushController()							{ return (IAiPushController*)m_controllerList[kPushController]; }
	IAiInfectedController* GetInfectedController()					{ return (IAiInfectedController*)m_controllerList[kInfectedController]; }
	AiLeapController* GetLeapController()							{ return (AiLeapController*)m_controllerList[kLeapController]; }
	AiEntryController* GetEntryController() override				{ return (AiEntryController*)m_controllerList[kEntryController]; }
	AiFlockController* GetFlockController()							{ return (AiFlockController*)m_controllerList[kFlockController]; }
	AiCarriedController* GetCarriedControl()						{ return (AiCarriedController*)m_controllerList[kCarriedController]; }

	AiScriptController* GetScriptController(U32F typeIndex);
	AiScriptController* GetScriptControllerByLayerName(StringId64 layerName);

	const IAiLocomotionController* GetLocomotionController() const override	{ return (const IAiLocomotionController*)m_controllerList[kLocomotionController]; }

	const IAiCoverController* GetCoverController() const					{ return (const IAiCoverController*)m_controllerList[kCoverController]; }
	const IAiHitController* GetHitController() const override				{ return (const IAiHitController*)m_controllerList[kHitController]; }
	const IAiPerformanceController* GetPerformanceController() const		{ return (const IAiPerformanceController*)m_controllerList[kPerformanceController]; }
	const IAiMeleeActionController* GetMeleeActionController() const		{ return (const IAiMeleeActionController*)m_controllerList[kMeleeActionController]; }
	const IAiIdleController* GetIdleController() const						{ return (const IAiIdleController*)m_controllerList[kIdleController]; }
	const IAiWeaponController* GetWeaponController() const override			{ return (const IAiWeaponController*)m_controllerList[kWeaponController]; }
	const IAiCinematicController* GetCinematicController() const override	{ return (const IAiCinematicController*)m_controllerList[kCinematicController]; }
	const IAiTurretController* GetTurretController() const					{ return (const IAiTurretController*)m_controllerList[kTurretController]; }
	const IAiPerchController* GetPerchController()	const					{ return (const IAiPerchController*)m_controllerList[kPerchController]; }
	const TapController* GetTraversalController() const override			{ return (const TapController*)m_controllerList[kTraversalController]; }
	const IAiInvestigateController* GetInvestigateController() const		{ return (const IAiInvestigateController*)m_controllerList[kInvestigateController]; }
	const AiSearchController* GetSearchController() const					{ return (const AiSearchController*)m_controllerList[kSearchController]; }
	const IAiFaceController* GetFaceController() const						{ return (const IAiFaceController*)m_controllerList[kFaceController]; }
	const IAiRideHorseController* GetRideHorseController() const			{ return (const IAiRideHorseController*)m_controllerList[kRideHorseController]; }
	const HorseJumpController* GetHorseJumpController() const				{ return (const HorseJumpController*)m_controllerList[kHorseJumpController]; }
	const IAiEvadeController* GetEvadeController() const					{ return (const IAiEvadeController*)m_controllerList[kEvadeController]; }
	const IAiSwimController* GetSwimController() const override				{ return (const IAiSwimController*)m_controllerList[kSwimController]; }
	const IAiVehicleController* GetVehicleController() const				{ return (const IAiVehicleController*)m_controllerList[kVehicleController]; }
	const IAiDodgeController* GetDodgeController() const					{ return (const IAiDodgeController*)m_controllerList[kDodgeController]; }
	const IAiPushController* GetPushController() const						{ return (const IAiPushController*)m_controllerList[kPushController]; }
	const IAiInfectedController* GetInfectedController() const				{ return (const IAiInfectedController*)m_controllerList[kInfectedController]; }
	const AiLeapController* GetLeapController() const						{ return (const AiLeapController*)m_controllerList[kLeapController]; }
	const AiEntryController* GetEntryController() const	override			{ return (const AiEntryController*)m_controllerList[kEntryController]; }
	const AiFlockController* GetFlockController() const 					{ return (const AiFlockController*)m_controllerList[kFlockController]; }
	const AiCarriedController* GetCarriedController() const					{ return (const AiCarriedController*)m_controllerList[kCarriedController]; }

	const AiScriptController* GetScriptController(U32F typeIndex) const;

#if ENABLE_NAV_LEDGES
	IAiClimbController* GetClimbController() override
	{
		return (IAiClimbController*)m_controllerList[kClimbController];
	}
	const IAiClimbController* GetClimbController() const override
	{
		return (const IAiClimbController*)m_controllerList[kClimbController];
	}
#endif

	virtual const ActionPackController* GetControllerForActionPackType(ActionPack::Type apType) const override;
	virtual ActionPackController* GetControllerForActionPackType(ActionPack::Type apType) override;

	bool ShouldInterruptNavigation() const;
	virtual U32F GetShouldInterruptNavigationForEach(BitArray128& bitArrayOut) const override;
	bool ShouldInterruptSkills() const;
	virtual U32F GetShouldInterruptSkillsForEach(BitArray128& bitArrayOut) const override;

	virtual const char* GetControllerName(U32F typeIndex) const override;
};
