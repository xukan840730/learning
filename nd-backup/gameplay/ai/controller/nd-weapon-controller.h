/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/ai/controller/animaction-controller.h"
#include "gamelib/gameplay/ai/nav-character-anim-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ProcessWeaponBase;

namespace DC
{
	struct BlendParams;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class INdAiWeaponController : public AnimActionController
{
public:
	// Holster / Unholster
	virtual void RequestGunState(GunState gs, bool slow = false, const DC::BlendParams* pBlend = nullptr) = 0;
	virtual GunState GetRequestedGunState() const = 0;
	virtual GunState GetCurrentGunState() const	  = 0;
	virtual bool IsPendingGunState(GunState gs) const = 0;
	virtual void ForceGunState(GunState gs,
							   bool overrideEqualCheck = false,
							   bool abortAnims = false,
							   float blendTime = 0.0f,
							   bool immediate = false) = 0;					// this means we are not playing any unhoslter anim

	virtual void SetWeaponInHand(const ProcessWeaponBase* pWeapon, bool inHand) = 0;

	// Temporary helper funcs
	bool IsHolstered() const { return GetCurrentGunState() == kGunStateHolstered; }
	void DrawWeapon() { RequestGunState(kGunStateOut); }
	void HolsterWeapon() { RequestGunState(kGunStateHolstered); }

	// Reloading
	virtual void Reload()	 = 0;
	virtual void Rechamber() = 0;
	virtual bool IsReloading() const = 0;
	virtual void AbortReload()		 = 0;

	// aborts all anims
	virtual void Abort(bool immediate = false) = 0;

	virtual ProcessWeaponBase* GetRequestedPrimaryWeapon() const = 0;
	virtual ProcessWeaponBase* GetAnimRequestedWeapon(bool allowRequestedFallback = true) const = 0;

	virtual void SuppressWeaponChanges(float durationSec) = 0;
	virtual bool TryExtendSuppression(float durationSec) = 0;
};
