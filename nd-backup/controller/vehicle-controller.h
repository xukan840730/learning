/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef AI_VEHICLE_CONTROLLER_H
#define AI_VEHICLE_CONTROLLER_H

#include "gamelib/gameplay/ai/controller/animaction-controller.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/scriptx/h/driveable-vehicle-script-defines.h"
#include "game/vehicle/vehicle.h"

// Handles ai characters driving or riding as passenger in a car.

FWD_DECL_PROCESS_HANDLE(DriveableVehicle);

struct EnterVehicleArgs;

class IVehicleCtrl;
class ActionPackController;

class IAiVehicleController : public AnimActionController
{
public:

	enum JumpDirection
	{
		kJumpDirectionLeft,
		kJumpDirectionRight,
		kJumpDirectionBack
	};

	virtual ActionPackController* GetVehicleActionPackController() = 0;

	virtual bool IsEnteredIntoVehicle() const = 0;

	virtual void SetActionPackEnterArgs(const EnterVehicleArgs& args) = 0;
	virtual TimeFrame GetActionPackEnterTime() const = 0;
	virtual void EnterActionPackRightAway() = 0;
	virtual void EnterActionPackSimultaneous() = 0;

	virtual void EnterVehicle(const EnterVehicleArgs& args) = 0;

	virtual void HandleEvent(Event & evt) = 0;

	virtual void OnIgcBind() = 0;
	virtual void OnIgcUnbind() = 0;
	virtual bool IsBoundToIgc() const = 0;

	virtual bool CanAim() const = 0;
	virtual void GoAim() = 0;
	virtual bool GoAimFailed() const = 0;
	virtual bool IsAiming() const = 0;

	virtual bool CanJumpTo(VehicleHandle targetCar) const = 0;

	// get ready to jump somewhere
	virtual void GoReadyJump(VehicleHandle targetCar) = 0;
	virtual bool GoReadyJumpFailed() const = 0;

	virtual bool IsReadyJump() const = 0;

	virtual void GoIdle() = 0;

	/* resume animating after an interruption such as a hit reaction */
	virtual void Resume() = 0;

	virtual void Jump() = 0;
	virtual bool IsJumping() const = 0;

	virtual bool IsLanding() const = 0;

	virtual bool IsHangingOn() const = 0;
	virtual DC::CarMeleeHangOnLoc GetHangOnLoc() const = 0;

	virtual bool CanMelee() const = 0;

	virtual bool CanDieFromVehicleImpact() const = 0;

	virtual void ExitVehicle(const DC::CarExitType exitType) = 0;

	virtual void QuickCompleteExit() = 0;

	virtual void OnDeath() = 0;

	virtual bool LockGestures() const = 0;
	virtual bool AllowFlinches() const = 0;

	virtual DC::CarRiderRole GetRole() const = 0;
	virtual VehicleHandle GetCar() const = 0;

	virtual const IVehicleCtrl* GetDriveCtrl() const = 0;
	virtual IVehicleCtrl* GetDriveCtrl() = 0;
};

IAiVehicleController* CreateAiVehicleController();

#endif
