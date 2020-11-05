/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef CHARACTER_LEG_IK_CONTROLLER_H
#define CHARACTER_LEG_IK_CONTROLLER_H

#include <Eigen/Dense>

#include "gamelib/gameplay/character-leg-ik.h"
#include "gamelib/gameplay/character-leg-raycaster.h"
#include "gamelib/gameplay/leg-ik/freeze-leg-ik.h"
#include "gamelib/gameplay/leg-ik/melee-leg-ik.h"
#include "gamelib/gameplay/leg-ik/move-leg-ik-new.h"
#include "gamelib/gameplay/leg-ik/move-leg-ik.h"
#include "gamelib/gameplay/leg-ik/scripted-arm-ik.h"
#include "gamelib/gameplay/leg-ik/scripted-leg-ik.h"
#include "gamelib/gameplay/leg-ik/scripted-move-leg-ik.h"
#include "gamelib/scriptx/h/nd-script-func-defines.h"
#include "ndlib/anim/armik.h"
#include "ndlib/anim/footik.h"
#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/util/maybe.h"
#include "ndlib/util/tracker.h"

class Character;
class IArmIk;
class ILegIk;
class Pat;
class WindowContext;

extern bool g_debugLegIK;

#define DEBUG_LEG_IK FALSE_IN_FINAL_BUILD(g_debugLegIK)

struct HandConstraintInfo
{
	Locator localSpaceOffset;
	Locator localSpaceOffsetToOtherHand;
	F32 blend;
	F32 weaponBlend;
	F32 distPreIk;
	bool enabled;

	HandConstraintInfo()
		: localSpaceOffset(kIdentity)
		, localSpaceOffsetToOtherHand(kIdentity)
		, blend(0.f)
		, weaponBlend(0.0f)
		, distPreIk(100.0f)
		, enabled(false)
	{}
};

class CharacterLegIkController : public ICharacterLegIkController
{
public:
	CharacterLegIkController();

	virtual void DisableRootAdjustment() override;
	virtual float GetSlopeBlend();
	virtual void Init(Character * pCharacter, bool useMeshRaycasts) override;
	virtual void UseMeshRaycasts(bool useMrc) override;
	virtual void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void Reset() override;
	virtual void PreUpdate() override;
	virtual void PostUpdate() override;
	bool GetShouldMirror() override;
	void SetShouldMirror(bool shouldMirror);
	void SetFootOnGround(int legIndex, bool onGround);
	bool GetFootOnGround(int legIndex) const;
	bool ShouldMirror(const Point& leftLeg, const Point& rightLeg); // TODO@QUAD -- how do we decide whether or not to mirror with four legs?
	virtual bool InSnow() const override {return m_noMeshIk;}
	virtual void SingleFrameDisableLeg(int legIndex) override;
	virtual void SingleFrameUnfreeze() override;
	virtual bool IsEnabledStopped() override;
	bool IsFullyBlendedOut() const;
	virtual void Update(Vector deltaTransform, Pat pat) override;
	virtual void PostAnimUpdate() override;
	void SetBlend(float blendTime);
	virtual void DisableIK(float blendTime /* = 0.2f */) override;
	virtual Mode GetCurrentMode() const override;
	virtual LegRaycaster* GetLegRaycaster() override;
	virtual const LegRaycaster* GetLegRaycaster() const override;
	virtual void EnableIK(Mode ikMode, float blendTime = 0.2f) override;
	virtual void EnableStoppedIK(float blendTime /* = 0.2f */) override;
	virtual void DisableCollision() override;
	virtual void EnableCollision() override;
	virtual void EnableMovingIK(float blendTime /* = 0.2f */) override;
	virtual void EnableMovingScriptedArmIK(float blendTime /* = 0.2f */) override;
	virtual void EnableMovingNonPredictiveIK(float blendTime /* = 0.2f */) override;
	virtual void EnableMeleeIK(float blendTime /* = 0.2f */) override;
	virtual void EnableScriptedIK(float blendTime /* = 0.2f */) override;
	virtual void EnableScriptedMoveIK(float blendTime, bool leftHandEnabled, bool rightHandEnabled) override;
	virtual void SetMeleeRootShiftDelta(float delta) override;
	virtual void SetMeleeFeetDeltas(float ldelta, float rdelta, float flDelta, float frDelta) override;
	virtual MeleeIkInfo GetMeleeIkInfo() const override;
	float GetRootDelta() const override;
	virtual void EnableHandAdjustment(float blendTime = 0.2f) override;
	virtual void DisableHandAdjustment(float blendTime = 0.2f) override;
	void SetHandBlendTarget(F32 targetBlend, F32 targetTime);
	void UpdateHandBlend();
	void SetHandConstraint(ArmIndex index, I32 jointIndex);
	virtual void ConstrainHandsToLegs(StringId64 leftHandJoint, StringId64 rightHandJoint) override;
	virtual void UnconstrainHandsToLegs() override;
	F32 ComputeOffHandWeaponBlend(const Locator wristLoc, const Locator gunLoc);
	virtual void UpdateHandsPreIK(const HandWeaponInfo& handWeaponInfo) override;
	F32 ComputeHandBlendFromDistToLeg(F32 dist);
	virtual void UpdateHands() override;
	virtual void DebugDraw(WindowContext* pWindowContext) override;
	virtual Maybe<Point> GetNextPredictedFootPlant() override;
	virtual void GetPredictedFootPlants(Maybe<Point> (&pos)[kQuadLegCount]) const override;

	virtual void SetScriptIkModeOverride(DC::AnimIkMode ikMode, float fadeTime) override;
	virtual DC::AnimIkMode GetScriptIkModeOverride(float* pFadeTimeOut) const override;

	virtual void SanitizeRayCaster() override;

	virtual const GroundModel* GetGroundModel() const override;

	virtual void OverrideRootSmootherSpring(F32 spring) override { m_rootBaseSpring = spring; }

private:

	void InitIfNecessary();
	void InitArmIfNecessary();

	void SetCurrentLegIk(ILegIk* legIk);
	void SetCurrentArmIk(IArmIk* armIk);

	void BlendOutArmIk();


public:

	Character* m_pCharacter;

	FreezeLegIk m_freezeLegIk;
	MoveLegIk m_moveLegIk;
	MeleeLegIk m_meleeLegIk;
	ScriptedLegIk m_scriptedLegIk;
	ScriptedMoveLegIk m_scriptedMoveLegIk;
	MoveLegIkNew m_moveLegIkNew;

	ScriptedArmIk m_scriptedArmIk;

	ILegIk* m_currentLegIk;
	IArmIk* m_currentArmIk;
	bool m_enableCollision;
	LegIkChain m_legIks[kQuadLegCount];
	bool m_enabled;
	bool m_armEnabled;
	float m_blendSpeed;
	bool m_shouldMirror;
	bool m_footOnGround[kQuadLegCount];
	float m_rootDelta;
	float m_lastAppliedRootDelta;
	float m_lastRootDelta;
	float m_slopeBlend;
	float m_rootBaseSpring = -1.0f;
	Vector m_groundNormal;

	Maybe<Point> m_prevFootPos[kQuadLegCount];
	Maybe<float> m_footSpeedsXZ[kQuadLegCount];
	Maybe<Vector> m_footDeltasXZ[kQuadLegCount];

	I32 m_frameIndex;
	bool m_legYInited[kQuadLegCount];

	float m_legY[kQuadLegCount];

	SpringTracker<float> m_legYSpring[kQuadLegCount];
	SpringTracker<float> m_legYVelSpring[kQuadLegCount];

	bool m_rootBaseYInited;
	float m_rootBaseY;
	float m_lastRootBaseY;
	float m_rootBaseYSpeed;
	SpringTracker<float> m_rootYSpeedSpring;
	float m_lastAlignY;

	SpringTracker<float> m_rootYSpring;

	bool m_footNormalInited[kQuadLegCount];
	Vector m_footNormal[kQuadLegCount];
	Vector m_footGroundNormal[kQuadLegCount];
	SpringTracker<Vector> m_footNormalSpring[kQuadLegCount];

	LegRaycaster m_legRaycaster;

	bool m_useMeshRaycasts;
	bool m_noMeshIk;
	bool m_enableRootAdjust;

	Mode m_mode;

	ArmIkChain m_armIks[2];
	HandConstraintInfo m_handInfos[2];
	F32 m_handBlend;
	F32 m_handBlendTarget;
	F32 m_handBlendSpeed;

	Locator m_lastFrameResultAnklesOs[kQuadLegCount];
	bool m_lastFrameResultValid;

	int m_legCount;

private:
	DC::AnimIkMode m_scriptOverride;
	float m_scriptOverrideFadeTime;

	BoundFrame m_lastFrameRaycastPos[kQuadLegCount];
	bool m_lastFrameRayCastPosValid;
};

extern bool g_enableLegRaycasts;
extern bool g_enableLegIK;
extern bool g_enableLegIkPlatform;

#endif //CHARACTER_LEG_IK_CONTROLLER_H
