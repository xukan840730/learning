/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/process/bound-frame.h"

#include "gamelib/gameplay/ai/controller/nd-weapon-controller.h"

class ProcessWeaponBase;

/// --------------------------------------------------------------------------------------------------------------- ///
//  IAiWeaponController
//  WeaponController handles the motion of drawing or holstering weapons and attaching the appropriate models.
//  It's purely visual and NOT involved with gameplay although gameplay may check if a weapon is fully drawn.
/// --------------------------------------------------------------------------------------------------------------- ///
class IAiWeaponController : public INdAiWeaponController
{
public:
	virtual U64 CollectHitReactionStateFlags() const override = 0;

	// Create the graphical "fire" recoil effect
	virtual void Fire() = 0;

	// Grenade
	virtual bool DrawGrenade(const bool spawnHeldGrenadeIfNotPresent = true) = 0;
	virtual void DrawGrenadeRock() = 0;
	virtual bool GrenadeToss(const BoundFrame& bFrame, const bool spawnHeldGrenadeIfNotPresent = true) = 0;
	virtual bool IsThrowingGrenade() const = 0;
	virtual void AbortGrenadeToss() = 0;
	virtual float GetAnimPhase() const = 0;

	// Toss weapon to another character (generally the player)
	typedef void (*ForgetAboutWeaponFunc)(class Character&, class ProcessWeapon&);
	virtual class ProcessWeapon* WeaponToss(class Character& receiver, ForgetAboutWeaponFunc forget) = 0;

	virtual void SetWeaponIkDesired(bool desired) = 0;

	virtual void RequestWeaponUp() = 0;
	virtual void RequestWeaponDown() = 0;
	virtual bool IsWeaponUpRequested() const = 0;
	virtual void UpdateWeaponUpDownPercent(float pcnt, float target) = 0;

	virtual bool IsRecoiling() const { return false; }
	virtual bool IsDoingWeaponSwitch() const = 0;

	virtual void UpdateOverlays() = 0;

	virtual bool RequestPrimaryWeapon(StringId64 primaryWeaponId) = 0;
	virtual bool ForcePrimaryWeapon(StringId64 primaryWeaponId) = 0;
	virtual bool IsPrimaryWeaponUpToDate() const = 0;
	virtual ProcessWeaponBase* GetPrimaryWeapon() const = 0;
	virtual void ApplyPrimaryWeaponSwitch() = 0;
	virtual void ApplyAnimRequestedWeaponSwitch() = 0;

	// used by EFFs to keep the gun put away if needed (like say for taking a TAP)
	virtual bool EnableGunOutSuppression() = 0;
	virtual bool DisableGunOutSuppression() = 0;
	virtual bool IsGunOutSuppressed() const = 0;

	virtual void AllowGunOut(bool allow) = 0;

	virtual void SetWeaponAnimSetList(StringId64 weaponSetListId) = 0;
	virtual StringId64 GetWeaponAnimSetList() const = 0;

	virtual void RequestWeaponIdleAnim(StringId64 animId) = 0;

	virtual void EnforceGunState(ProcessWeaponBase* pWeapon,
								 const GunState gs,
								 bool immediate	 = false,
								 float blendTime = 0.0f,
								 bool force		 = false) = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiWeaponController* CreateAiWeaponController();
