/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/joint-modifiers/joint-modifier-data.h"
#include "ndlib/util/maybe.h"
#include "ndlib/ndphys/pat.h"

#include "gamelib/scriptx/h/nd-script-func-defines.h"

class LegRaycaster;
class Character;
class LegIkChain;
class GroundModel;

/// --------------------------------------------------------------------------------------------------------------- ///
struct MeleeRootShift
{
	float m_max;
	float m_min;
	float m_desired;
};


/// --------------------------------------------------------------------------------------------------------------- ///
struct MeleeIkInfo : public LegIkInfoShared
{
	bool m_valid;

	MeleeIkInfo()
		:m_valid(false)
	{
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct HandWeaponInfo
{
	U8 m_dominantHand;
	I32 m_gunBoundJoint;
	Locator m_gunCheckJointLocalSpace;

	HandWeaponInfo() :
		m_dominantHand(0),
		m_gunBoundJoint(-1),
		m_gunCheckJointLocalSpace(kIdentity)
	{
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
class ICharacterLegIkController
{
public:
	enum Mode
	{
		kModeDisabled,
		kModeStopped,
		kModeMoving,
		kModeMovingNonPredictive,
		kModeMelee,
		kModeScripted,
		kModeScriptedMove,
	};

	virtual ~ICharacterLegIkController() {}
	virtual void Init(Character* pCharacter, bool m_useMeshRaycasts) = 0;
	virtual void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound) = 0;
	virtual void Update(Vector deltaTransform, Pat pat) = 0;
	virtual void Reset() = 0;

	virtual void PreUpdate() = 0;
	virtual void PostUpdate() = 0;

	virtual void PostAnimUpdate() = 0;

	virtual void EnableIK(Mode ikMode, float blendTime = 0.2f) = 0;
	virtual void EnableStoppedIK(float blendTime = 0.2f) = 0;
	virtual void EnableMovingIK(float blendTime = 0.2f) = 0;
	virtual void EnableMovingScriptedArmIK(float blendTime = 0.2f) = 0;
	virtual void EnableMovingNonPredictiveIK(float blendTime = 0.2f) = 0;
	virtual void EnableMeleeIK(float blendTime = 0.2f) = 0;
	virtual void EnableScriptedIK(float blendTime = 0.2f) = 0;
	virtual void EnableScriptedMoveIK(float blendTime = 0.2f, bool leftHandEnabled = true, bool rightHandEnabled = true) = 0;

	virtual void DisableRootAdjustment() = 0;
	virtual void SingleFrameDisableLeg(int legIndex) = 0;

	virtual void EnableHandAdjustment(float blendTime = 0.2f) = 0;
	virtual void DisableHandAdjustment(float blendTime = 0.2f) = 0;

	virtual void DisableIK(float blendTime = 0.2f) = 0;
	virtual Mode GetCurrentMode() const = 0;

	virtual bool GetShouldMirror() = 0;
	virtual void DisableCollision() = 0;
	virtual void EnableCollision() = 0;
	virtual void SingleFrameUnfreeze() = 0;
	virtual bool IsEnabledStopped() = 0;
	virtual LegRaycaster* GetLegRaycaster() = 0;
	virtual const LegRaycaster* GetLegRaycaster() const = 0;
	virtual float GetRootDelta() const = 0;
	virtual void SetMeleeRootShiftDelta(float delta) = 0;
	virtual void SetMeleeFeetDeltas(float ldelta, float rdelta, float flDelta, float frDelta) = 0;
	virtual MeleeIkInfo GetMeleeIkInfo() const = 0;
	virtual bool InSnow() const = 0;

	virtual void UseMeshRaycasts(bool useMrc) = 0;

	virtual void UpdateHandsPreIK(const HandWeaponInfo& handWeaponInfo) = 0;
	virtual void ConstrainHandsToLegs(StringId64 leftHandJoint, StringId64 rightHandJoint) = 0;
	virtual void UnconstrainHandsToLegs() = 0;
	virtual void UpdateHands() = 0;

	virtual void DebugDraw(WindowContext* pWindowContext) = 0;

	virtual Maybe<Point> GetNextPredictedFootPlant() = 0;
	virtual void GetPredictedFootPlants(Maybe<Point> (&pos)[kQuadLegCount]) const = 0;

	virtual void SetScriptIkModeOverride(DC::AnimIkMode ikMode, float fadeTime) = 0;
	virtual DC::AnimIkMode GetScriptIkModeOverride(float* pFadeTimeOut) const = 0;

	virtual void SanitizeRayCaster() = 0;

	virtual const GroundModel* GetGroundModel() const { return nullptr; }

	virtual void OverrideRootSmootherSpring(F32 spring) {}
};

/// --------------------------------------------------------------------------------------------------------------- ///
ICharacterLegIkController* CreateCharacterLegIkController();
MeleeRootShift ComputeMeleeRootShift(const MeleeIkInfo& info, bool player);
