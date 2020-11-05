/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */
#include "gamelib/gameplay/leg-ik/character-leg-ik-controller.h"

#include "corelib/math/intersection.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/leg-ik/anim-ground-plane.h"
#include "gamelib/gameplay/leg-ik/arm-ik.h"
#include "gamelib/gameplay/leg-ik/leg-ik.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/footik.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/nd-options.h"
#include "ndlib/ndphys/pat.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/tools-shared/patdefs.h"

class WindowContext;


bool g_enableLegRaycasts = true;
bool g_enableLegIK = true;
bool g_enableLegIkPlatform = true;
bool g_debugLegIK = false;

#ifndef FINAL_BUILD
#ifndef CHECK_LEG_INDEX_CONTROLLER
#define CHECK_LEG_INDEX_CONTROLLER(legIndex)                                                    \
do                                                                                              \
{                                                                                               \
	ANIM_ASSERTF(legIndex >= 0 && legIndex < m_legCount,                                        \
		("Invalid leg index %d, [Character: %s, legCount: %d]",                                  \
			legIndex, m_legCount,                                                               \
			m_pCharacter ? DevKitOnly_StringIdToString(m_pCharacter->GetUserId()) : "<none>")); \
} while (0);                                                                                  
#endif //#ifndef CHECK_LEG_INDEX_CONTROLLER
#else
#ifndef CHECK_LEG_INDEX_CONTROLLER
#define CHECK_LEG_INDEX_CONTROLLER(legIndex)  
#endif // #else (#ifndef FINAL_BUILD
#endif //#ifndef FINAL_BUILD


/// --------------------------------------------------------------------------------------------------------------- ///
ICharacterLegIkController* CreateCharacterLegIkController()
{
	return NDI_NEW CharacterLegIkController;
}

/// --------------------------------------------------------------------------------------------------------------- ///
CharacterLegIkController::CharacterLegIkController()
{
	m_currentLegIk = nullptr;
	m_currentArmIk = nullptr;
	m_enableCollision = true;
	m_enabled = false;
	m_armEnabled = false;
	memset(m_footOnGround, false, sizeof(m_footOnGround));
	m_lastAppliedRootDelta = 0.0f;
	m_handBlend = 0.0f;
	m_handBlendTarget = 0.0f;
	m_handBlendSpeed = 5.0f;
	m_enableRootAdjust = true;
	m_scriptOverride = DC::kAnimIkModeNone;
	m_scriptOverrideFadeTime = 0.0f;
	m_lastFrameResultValid = false;
	m_lastFrameRayCastPosValid = false;
	m_legCount = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::DisableRootAdjustment()
{
	m_enableRootAdjust = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float CharacterLegIkController::GetSlopeBlend()
{
	return m_slopeBlend;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::Init(Character * pCharacter, bool useMeshRaycasts)
{
	const int legCount = pCharacter->GetLegCount();
	ANIM_ASSERTF(legCount == kLegCount || legCount == kQuadLegCount,
				 ("Character %s has an invalid leg count %d",
				  DevKitOnly_StringIdToString(pCharacter->GetUserId()),
				  legCount));

	m_legCount = legCount;

	for (int iLeg = 0; iLeg < legCount; ++iLeg)
	{
		m_legIks[iLeg].Init(pCharacter, iLeg, true);
	}

	m_armIks[kLeftArm].Init(pCharacter, kLeftArm, legCount);
	m_armIks[kRightArm].Init(pCharacter, kRightArm, legCount);

	m_freezeLegIk.InitLegIk(pCharacter, m_legIks, legCount);
	m_moveLegIk.InitLegIk(pCharacter, m_legIks, legCount);
	m_meleeLegIk.InitLegIk(pCharacter, m_legIks, legCount);
	m_scriptedLegIk.InitLegIk(pCharacter, m_legIks, legCount);
	m_scriptedMoveLegIk.InitLegIk(pCharacter, m_legIks, legCount);
	m_moveLegIkNew.InitLegIk(pCharacter, m_legIks, legCount);
	m_scriptedArmIk.InitArmIk(pCharacter, m_armIks);

	m_legRaycaster.SetCharacter(pCharacter);

	m_useMeshRaycasts = useMeshRaycasts;
	m_noMeshIk = false;
	m_legRaycaster.Init();
	m_pCharacter = pCharacter;
	m_mode = kModeDisabled;
	m_frameIndex = 0;

	InitIfNecessary();
	InitArmIfNecessary();
	Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::UseMeshRaycasts(bool useMrc)
{
	m_useMeshRaycasts = useMrc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_currentLegIk, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_currentArmIk, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pCharacter, deltaPos, lowerBound, upperBound);
	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		m_legIks[iLeg].Relocate(deltaPos, lowerBound, upperBound);
	}

	m_armIks[kLeftArm].Relocate(deltaPos, lowerBound, upperBound);
	m_armIks[kRightArm].Relocate(deltaPos, lowerBound, upperBound);
	m_freezeLegIk.Relocate(deltaPos, lowerBound, upperBound);
	m_moveLegIk.Relocate(deltaPos, lowerBound, upperBound);
	m_meleeLegIk.Relocate(deltaPos, lowerBound, upperBound);
	m_scriptedLegIk.Relocate(deltaPos, lowerBound, upperBound);
	m_scriptedMoveLegIk.Relocate(deltaPos, lowerBound, upperBound);
	m_moveLegIkNew.Relocate(deltaPos, lowerBound, upperBound);
	m_scriptedArmIk.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::Reset()
{
	m_slopeBlend = 1.0f;
	m_rootDelta = 0.0f;
	m_rootBaseY = 0.0f;
	m_rootBaseYInited = false;
	m_shouldMirror = false;

	for (U32 iArm = 0; iArm < 2; ++iArm)
	{
		m_handInfos[iArm].blend = 0.0f;
		m_handInfos[iArm].enabled = false;
	}

	for (int iLeg = 0; iLeg < kQuadLegCount; ++iLeg)
	{
		m_legY[iLeg] = 0.0f;
		m_legYInited[iLeg] = false;
		m_footNormalInited[iLeg] = false;
		m_footOnGround[iLeg] = false;
		m_footNormal[iLeg] = kUnitYAxis;
		m_footGroundNormal[iLeg] = kUnitYAxis;
		m_footNormalSpring[iLeg].Reset();
	}

	m_scriptOverride = DC::kAnimIkModeNone;
	m_scriptOverrideFadeTime = 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::PreUpdate()
{
	PROFILE(Processes, CharLegIk_PreUpdate);

	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		m_legIks[iLeg].ReadJointCache();
	}
	m_armIks[kLeftArm].ReadJointCache();
	m_armIks[kRightArm].ReadJointCache();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::PostUpdate()
{
	PROFILE(Processes, CharLegIk_PostUpdate);

	if (IsFullyBlendedOut() || g_animOptions.m_ikOptions.m_disableAllIk)
	{
		for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
		{
			m_legIks[iLeg].DiscardJointCache();
		}
		m_armIks[kLeftArm].DiscardJointCache();
		m_armIks[kRightArm].DiscardJointCache();
		return;
	}

	m_armIks[kLeftArm].WriteJointCache(false);
	m_armIks[kRightArm].WriteJointCache(false);

	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		m_legIks[iLeg].WriteJointCache(false);
	}
	m_pCharacter->GetAnimData()->m_jointCache.UpdateWSRootSubPose(m_pCharacter->GetLocator());
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterLegIkController::GetShouldMirror()
{
	return m_shouldMirror;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::SetShouldMirror(bool shouldMirror)
{
	m_shouldMirror = shouldMirror;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::SetFootOnGround(int legIndex, bool onGround)
{
	CHECK_LEG_INDEX_CONTROLLER(legIndex);
	m_footOnGround[legIndex] = onGround;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterLegIkController::GetFootOnGround(int legIndex) const
{
	CHECK_LEG_INDEX_CONTROLLER(legIndex);
	return m_footOnGround[legIndex];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::InitIfNecessary()
{
	if (m_currentLegIk == nullptr)
	{
		SetCurrentLegIk(&m_moveLegIkNew);
		m_currentLegIk->Start(m_pCharacter);
		m_enabled = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::InitArmIfNecessary()
{
	if (m_currentArmIk == nullptr)
	{
		SetCurrentArmIk(&m_scriptedArmIk);
		m_currentArmIk->Start(m_pCharacter);
		m_armEnabled = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterLegIkController::ShouldMirror(const Point& leftLeg, const Point& rightLeg)
{
	Point currentPos = m_pCharacter->GetTranslation();
	Vector currentDir = GetLocalZ(m_pCharacter->GetRotation());

	Vector toRightLeg = rightLeg - currentPos;
	Vector toLeftLeg = leftLeg - currentPos;

	if (Dot(toRightLeg, currentDir) > Dot(toLeftLeg, currentDir))
	{
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::SingleFrameDisableLeg(int legIndex)
{
	CHECK_LEG_INDEX_CONTROLLER(legIndex);
	if (m_currentLegIk != nullptr)
	{
		m_currentLegIk->SingleFrameDisableLeg(legIndex);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::SingleFrameUnfreeze()
{
	if (m_currentLegIk != nullptr)
	{
		m_currentLegIk->SingleFrameUnfreeze();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterLegIkController::IsEnabledStopped()
{
	return (m_enabled && m_currentLegIk != nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CharacterLegIkController::IsFullyBlendedOut() const
{
	return !m_enabled && m_currentLegIk == nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Point GetRaycastPoint(Point_arg desiredPoint, Point_arg prevPoint)
{
	if (Dist(desiredPoint, prevPoint) < 0.02f)
	{
// 		g_prim.Draw(DebugCross(desiredPoint, 0.1f, kColorGreen));
// 		g_prim.Draw(DebugSphere(prevPoint, 0.01f, kColorRed));
		return prevPoint;
	}
	return desiredPoint;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::Update(Vector deltaTransform, Pat pat)
{
	PROFILE(Processes, CharLegIk_Update);

	if (!g_enableLegIK)
	{
		if (DEBUG_LEG_IK)
		{
			MsgCon("Leg ik disabled\n");
		}

		m_lastFrameResultValid = false;
		m_lastFrameRayCastPosValid = false;

		return;
	}

	const float dt = GetProcessDeltaTime();
	
	if (g_bSendEventToVFunc)
	{
		m_groundNormal = m_pCharacter->GetIkGroundNormal();
	}
	else
	{
		m_groundNormal = kUnitYAxis;
		BoxedValue result = SendEvent(SID("get-ik-ground-normal"), m_pCharacter);
		if (result.IsValid())
		{
			m_groundNormal = result.GetAsVector();
		}
	}

	for (int i = 0; i < m_legCount; i++)
	{
		Point legpos = m_legIks[i].GetAnkleLocWs().GetTranslation();
		if (m_prevFootPos[i].Valid() && dt > 0.0f)
		{
			Point prevPos = m_prevFootPos[i].Get() + deltaTransform;
			Vector deltaXZ = VectorXz(legpos - prevPos);
			m_footDeltasXZ[i] = deltaXZ;
			m_footSpeedsXZ[i] = (float)LengthXz(deltaXZ) / dt;
			if (DEBUG_LEG_IK)
			{
				MsgCon("Foot speed %i: %f\n", i, m_footSpeedsXZ[i].Get());
			}
		}
		else
		{
			m_footDeltasXZ[i] = MAYBE::kNothing;
			m_footSpeedsXZ[i] = MAYBE::kNothing;
			if (DEBUG_LEG_IK)
			{
				MsgCon("Foot speed %i: None\n");
			}
		}
		m_prevFootPos[i] = legpos;
	}

	m_frameIndex++;

	if (m_pCharacter->IsDead() && m_pCharacter->IsRagdollPhysicalized() && (m_enabled || m_blendSpeed < 1.0f / 0.2f))
	{
		// Blend out leg IK if we are dead and ragdolling
		DisableIK(0.2f);
	}

	SanitizeRayCaster();

	const float speed = Length(m_pCharacter->GetVelocityPs());
	const StringId64 animStateId = m_pCharacter->GetAnimControl()->GetBaseStateLayer()->CurrentStateId();
	if ((m_pCharacter->OnStairs())
		&& speed > 0.2f
		&& animStateId != SID("idle")
		&& animStateId != SID("run-idle")
		&& animStateId != SID("run-idle-m")
		&& animStateId != SID("melee-state"))
	{
		Seek(m_slopeBlend, 0.0f, FromUPS(5.0f));
	}
	else
	{
		Seek(m_slopeBlend, 1.0f, FromUPS(5.0f));
	}

	bool disableMrc = false;
	if (m_pCharacter->OnStairs() && animStateId == SID("melee-state"))
	{
		disableMrc = true;
	}

	if (DEBUG_LEG_IK)
	{
		MsgCon("Slope speed: %f\n", speed);
		MsgCon("Slope blend: %f\n", m_slopeBlend);
	}

	if (m_slopeBlend <= 0.0f)
	{
		for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
		{
			m_legYInited[iLeg] = false;
		}

		m_rootBaseYInited = false;
	}

	m_noMeshIk = pat.GetPlayerNoMeshIk();

	ALWAYS_ASSERT(IsFinite(m_pCharacter->GetBoundFrame()));
	ALWAYS_ASSERT(IsFinite(deltaTransform));

	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		m_legY[iLeg] += deltaTransform.Y();
	}
	m_rootBaseY += deltaTransform.Y();
	m_lastRootBaseY += deltaTransform.Y();
	m_lastAlignY += deltaTransform.Y();

	bool legPosFound = false;

	Point aLegPosWs[kQuadLegCount];

	if (m_currentLegIk)
	{
		legPosFound = m_currentLegIk->GetMeshRaycastPointsWs(m_pCharacter, &aLegPosWs[0], &aLegPosWs[1], &aLegPosWs[2], &aLegPosWs[3]);
	}

	if (!legPosFound)
	{
		for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
		{
			aLegPosWs[iLeg] = m_legIks[iLeg].GetAnkleLocWs().Pos();
		}
	}
	if (DEBUG_LEG_IK)
	{
		/*for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
		{
			g_prim.Draw(DebugCross(aLegPosWs[iLeg], 0.2f, kColorGreen));
		}*/
	}

	// TODO@QUAD -- how should we decide whether to mirror or not when we have 4 legs?
	SetShouldMirror(ShouldMirror(aLegPosWs[kLeftLeg], aLegPosWs[kRightLeg]));

	if (m_lastFrameRayCastPosValid)
	{
		for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
		{
			aLegPosWs[iLeg] = GetRaycastPoint(aLegPosWs[iLeg], m_lastFrameRaycastPos[iLeg].GetTranslation());
		}
	}

	Point aLegPosPs[kQuadLegCount];
	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		aLegPosPs[iLeg] = m_pCharacter->GetParentSpace().UntransformPoint(aLegPosWs[iLeg]);
		m_lastFrameRaycastPos[iLeg] = BoundFrame(aLegPosWs[iLeg], m_pCharacter->GetBinding());
	}
	m_lastFrameRayCastPosValid = true;

	m_legRaycaster.UpdateRayCastMode();

	// KANXU: if leg-ik controller is disabled, disable the mesh raycaster too. It helps the performance, especialy when in vehicle.
	const bool useMeshRayCast = m_enabled && 
								m_useMeshRaycasts && 
								g_enableLegRaycasts && 
								!disableMrc;
	m_legRaycaster.UseMeshRayCasts(useMeshRayCast, -1);

	const bool isQuadruped = (m_legCount == kQuadLegCount);
	if (isQuadruped)
	{
		m_legRaycaster.SetProbePointsPs(aLegPosPs[kBackLeftLeg], aLegPosPs[kBackRightLeg], aLegPosPs[kFrontLeftLeg], aLegPosPs[kFrontRightLeg]);
	}
	else
	{
		m_legRaycaster.SetProbePointsPs(aLegPosPs[kLeftLeg], aLegPosPs[kRightLeg]);
	}

	if (DEBUG_LEG_IK)
	{
		MsgCon("Using meshRaycasts: %s\n", useMeshRayCast ? "true" : "false");
	}

	if (m_frameIndex < 2)
	{
		m_legRaycaster.KickMeshRaycasts(m_pCharacter, m_noMeshIk);
		m_lastFrameResultValid = false;

		return;
	}

	//		MsgCon("deltaTransform = %f\n", (float)deltaTransform.Y());

	//		DebugDrawSphere(m_legRaycaster.GetLeftLegPoint(), 0.2f, kColorRed);
	//		DebugDrawSphere(m_legRaycaster.GetRightLegPoint(), 0.2f, kColorRed);

	if (m_currentLegIk)
	{
		if (m_enabled)
		{
			if (DEBUG_LEG_IK)
			{
				MsgCon("Blending in ik mode: '%s' speed: %f\n", m_currentLegIk->GetName(), m_blendSpeed);
			}
			m_currentLegIk->BlendIn(m_blendSpeed);
		}
		else
		{
			if (DEBUG_LEG_IK)
			{
				MsgCon("Blending out ik mode: '%s' speed: %f\n", m_currentLegIk->GetName(), m_blendSpeed);
			}
			m_currentLegIk->BlendOut(m_blendSpeed);
			if (m_currentLegIk->GetBlend(this) == 0.0f)
			{
				Reset();
				if (m_currentLegIk != nullptr)
				{
					m_currentLegIk->Stop(m_pCharacter);
					m_currentLegIk = nullptr;
				}

				for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
				{
					SetFootOnGround(iLeg, true);
				}
			}
		}
	}

	if (m_currentLegIk)
	{
		if (DEBUG_LEG_IK)
		{
			MsgCon("Current leg ik: %s\n", m_currentLegIk->GetName());
		}

		m_currentLegIk->Update(m_pCharacter, this, m_enableCollision);
		m_lastFrameResultValid = true;
		
		const Locator alignWs = m_pCharacter->GetLocator();
		for (int i = 0; i < m_legCount; i++)
		{
			m_lastFrameResultAnklesOs[i] = alignWs.UntransformLocator(m_legIks[i].GetAnkleLocWs());
		}
	}
	else
	{
		m_lastFrameResultValid = false;
		m_lastAppliedRootDelta = 0.0f;
	}

	// ARMS
	if (m_currentArmIk)
	{
		if (m_armEnabled)
		{
			if (DEBUG_LEG_IK)
			{
				MsgCon("Blending in arm ik mode: '%s' speed: %f\n", m_currentArmIk->GetName(), m_blendSpeed);
			}
			m_currentArmIk->BlendIn(m_blendSpeed);
		}
		else
		{
			if (DEBUG_LEG_IK)
			{
				MsgCon("Blending out arm ik mode: '%s' speed: %f\n", m_currentArmIk->GetName(), m_blendSpeed);
			}
			m_currentArmIk->BlendOut(m_blendSpeed);
			if (m_currentArmIk->GetBlend(this) == 0.0f)
			{
				if (m_currentArmIk != nullptr)
				{
					m_currentArmIk->Stop(m_pCharacter);
					m_currentArmIk = nullptr;
				}
			}
		}
	}

	if (m_currentArmIk)
	{
		if (DEBUG_LEG_IK)
		{
			MsgCon("Current arm ik: %s\n", m_currentArmIk->GetName());
		}

		m_currentArmIk->Update(m_pCharacter, this);
	}

	// This clears out previous results, so do it last
	m_legRaycaster.KickMeshRaycasts(m_pCharacter, m_noMeshIk);

	if (m_currentLegIk)
	{
		UpdateHands();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::SetBlend(float blendTime)
{
	if (blendTime == 0.0f)
		m_blendSpeed = 100.0f;
	else
		m_blendSpeed = 1.0f / blendTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::DisableIK(float blendTime /* = 0.2f */)
{
	m_mode = kModeDisabled;
	SetBlend(blendTime);
	m_enabled = false;
	m_armEnabled = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::BlendOutArmIk()
{
	m_armEnabled = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ICharacterLegIkController::Mode CharacterLegIkController::GetCurrentMode() const
{
	return m_mode;
}

/// --------------------------------------------------------------------------------------------------------------- ///
LegRaycaster* CharacterLegIkController::GetLegRaycaster()
{
	return &m_legRaycaster;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const LegRaycaster* CharacterLegIkController::GetLegRaycaster() const
{
	return &m_legRaycaster;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::EnableIK(Mode ikMode, float blendTime /*= 0.2f*/)
{
	switch (ikMode)
	{
	case kModeDisabled:
		DisableIK(blendTime);
		break;
	case kModeStopped:
		EnableStoppedIK(blendTime);
		break;
	case kModeMoving:
		EnableMovingIK(blendTime);
		break;
	case kModeMovingNonPredictive:
		EnableMovingNonPredictiveIK(blendTime);
		break;
	case kModeMelee:
		EnableMeleeIK(blendTime);
		break;
	case kModeScripted:
		EnableScriptedIK(blendTime);
		break;
	case kModeScriptedMove:
		EnableScriptedIK(blendTime);
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::EnableStoppedIK(float blendTime /* = 0.2f */)
{
	m_mode = kModeStopped;

	SetBlend(blendTime);
	InitIfNecessary();

	if (m_currentLegIk != &m_freezeLegIk)
	{
		float startBlend = 0.0f;
		if (m_currentLegIk != nullptr)
			startBlend = m_currentLegIk->GetBlend(this);

		m_currentLegIk->Stop(m_pCharacter);
		SetCurrentLegIk(&m_freezeLegIk);
		m_currentLegIk->Start(m_pCharacter);
		m_currentLegIk->SetBlend(startBlend);
	}

	BlendOutArmIk();

	m_enabled = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::DisableCollision()
{
	m_enableCollision = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::EnableCollision()
{
	m_enableCollision = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::EnableMovingIK(float blendTime /* = 0.2f */)
{
	m_mode = kModeMoving;

	SetBlend(blendTime);
	InitIfNecessary();

	EnableCollision();

	ILegIk *movingLegIk = &m_moveLegIk;
	if (g_ndOptions.m_movingLegIkTest)
		movingLegIk = &m_moveLegIkNew;

	if (m_currentLegIk != movingLegIk)
	{
		float startBlend = 0.0f;
		if (m_currentLegIk != nullptr)
			startBlend = m_currentLegIk->GetBlend(this);

		m_currentLegIk->Stop(m_pCharacter);
		SetCurrentLegIk(movingLegIk);
		m_currentLegIk->Start(m_pCharacter);
		m_currentLegIk->SetBlend(startBlend);
	}

	BlendOutArmIk();

	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		SetFootOnGround(iLeg, true);
	}

	m_enabled = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::EnableMovingScriptedArmIK(float blendTime /* = 0.2f */)
{
	m_mode = kModeMoving;

	SetBlend(blendTime);
	InitIfNecessary();

	EnableCollision();

	ILegIk *movingLegIk = &m_moveLegIk;
	if (g_ndOptions.m_movingLegIkTest)
		movingLegIk = &m_moveLegIkNew;

	if (m_currentLegIk != movingLegIk)
	{
		float startBlend = 0.0f;
		if (m_currentLegIk != nullptr)
			startBlend = m_currentLegIk->GetBlend(this);

		m_currentLegIk->Stop(m_pCharacter);
		SetCurrentLegIk(movingLegIk);
		m_currentLegIk->Start(m_pCharacter);
		m_currentLegIk->SetBlend(startBlend);
	}

	InitArmIfNecessary();

	if (m_currentArmIk != &m_scriptedArmIk)
	{
		float startBlend = 0.0f;
		if (m_currentArmIk != nullptr)
			startBlend = m_currentArmIk->GetBlend(this);

		m_currentArmIk->Stop(m_pCharacter);
		SetCurrentArmIk(&m_scriptedArmIk);
		m_currentArmIk->Start(m_pCharacter);
		m_currentArmIk->SetBlend(startBlend);
	}


	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		SetFootOnGround(iLeg, true);
	}

	m_armEnabled = true;
	m_enabled = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::EnableMovingNonPredictiveIK(float blendTime /* = 0.2f */)
{
	m_mode = kModeMovingNonPredictive;

	SetBlend(blendTime);
	InitIfNecessary();

	EnableCollision();

	ILegIk *movingLegIk = &m_moveLegIk;

	if (m_currentLegIk != movingLegIk)
	{
		float startBlend = 0.0f;
		if (m_currentLegIk != nullptr)
			startBlend = m_currentLegIk->GetBlend(this);

		m_currentLegIk->Stop(m_pCharacter);
		SetCurrentLegIk(movingLegIk);
		m_currentLegIk->Start(m_pCharacter);
		m_currentLegIk->SetBlend(startBlend);
	}

	BlendOutArmIk();

	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		SetFootOnGround(iLeg, true);
	}

	m_enabled = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::EnableMeleeIK(float blendTime /* = 0.2f */)
{
	m_mode = kModeMelee;

	SetBlend(blendTime);
	InitIfNecessary();

	EnableCollision();

	if (m_currentLegIk != &m_meleeLegIk)
	{
		float startBlend = 0.0f;
		if (m_currentLegIk != nullptr)
			startBlend = m_currentLegIk->GetBlend(this);

		m_currentLegIk->Stop(m_pCharacter);
		SetCurrentLegIk(&m_meleeLegIk);
		m_currentLegIk->Start(m_pCharacter);
		m_currentLegIk->SetBlend(startBlend);
	}

	BlendOutArmIk();

	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		SetFootOnGround(iLeg, true);
	}

	m_enabled = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::EnableScriptedIK(float blendTime /* = 0.2f */)
{
	m_mode = kModeScripted;

	SetBlend(blendTime);
	InitIfNecessary();

	EnableCollision();

	if (m_currentLegIk != &m_scriptedLegIk)
	{
		float startBlend = 0.0f;
		if (m_currentLegIk != nullptr)
			startBlend = m_currentLegIk->GetBlend(this);

		m_currentLegIk->Stop(m_pCharacter);
		SetCurrentLegIk(&m_scriptedLegIk);
		m_currentLegIk->Start(m_pCharacter);
		m_currentLegIk->SetBlend(startBlend);
	}

	InitArmIfNecessary();

	if (m_currentArmIk != &m_scriptedArmIk)
	{
		float startBlend = 0.0f;
		if (m_currentArmIk != nullptr)
			startBlend = m_currentArmIk->GetBlend(this);

		m_currentArmIk->Stop(m_pCharacter);
		SetCurrentArmIk(&m_scriptedArmIk);
		m_currentArmIk->Start(m_pCharacter);
		m_currentArmIk->SetBlend(startBlend);
	}

	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		SetFootOnGround(iLeg, true);
	}

	m_armEnabled = true;
	m_enabled = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::EnableScriptedMoveIK(float blendTime, bool leftEnabled, bool rightEnabled)
{
	m_mode = kModeScripted;

	SetBlend(blendTime);
	InitIfNecessary();

	EnableCollision();

	if (m_currentLegIk != &m_scriptedMoveLegIk)
	{
		float startBlend = 0.0f;
		if (m_currentLegIk != nullptr)
			startBlend = m_currentLegIk->GetBlend(this);

		m_currentLegIk->Stop(m_pCharacter);
		SetCurrentLegIk(&m_scriptedMoveLegIk);
		m_currentLegIk->Start(m_pCharacter);
		m_currentLegIk->SetBlend(startBlend);
	}

	InitArmIfNecessary();

	if (m_currentArmIk != &m_scriptedArmIk)
	{
		float startBlend = 0.0f;
		if (m_currentArmIk != nullptr)
			startBlend = m_currentArmIk->GetBlend(this);

		m_currentArmIk->Stop(m_pCharacter);
		SetCurrentArmIk(&m_scriptedArmIk);
		m_currentArmIk->Start(m_pCharacter);
		m_currentArmIk->SetBlend(startBlend);
	}

	m_currentArmIk->m_handBlend[kLeftArm] = leftEnabled ? 1.0f : 0.0f;
	m_currentArmIk->m_handBlend[kRightArm] = rightEnabled ? 1.0f : 0.0f;

	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		SetFootOnGround(iLeg, true);
	}

	m_armEnabled = true;
	m_enabled = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::SetMeleeRootShiftDelta(float delta)
{
	m_moveLegIkNew.SetRootShift(delta);
	//m_meleeLegIk.SetRootShiftDelta(delta);
	ANIM_ASSERT(IsFinite(delta) && delta == delta);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::SetMeleeFeetDeltas(float ldelta, float rdelta, float flDelta, float frDelta)
{
	m_meleeLegIk.SetFeetDeltas(ldelta, rdelta, flDelta, frDelta);
}

/// --------------------------------------------------------------------------------------------------------------- ///
MeleeIkInfo CharacterLegIkController::GetMeleeIkInfo() const
{
	return m_meleeLegIk.GetMeleeIkInfo(m_pCharacter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float CharacterLegIkController::GetRootDelta() const
{
	return m_lastAppliedRootDelta;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::EnableHandAdjustment(float blendTime /*= 0.2f*/)
{
	SetHandBlendTarget(1.0f, blendTime);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::DebugDraw(WindowContext* pWindowContext)
{

}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::UpdateHands()
{
	/* DISABLED FOR NOW BECAUSE: It doesn't seem to work correctly when Drake wades around in ankle-deep water in the colony basement. */
	return;

	// This function is probably broken since the great quadruped IK rewrite of 2019
	ANIM_ASSERTF(m_legCount == kLegCount, ("Quadruped Leg IK does not support updating hands. This will need to be rewritten if we add centaurs or some other four legged creature that also has hands"));

	for (U32 iHand = 0; iHand < 2; ++iHand)
	{
		//Compute distance to the thigh after the legs have been adjusted
		Point wristLoc = m_armIks[iHand].GetWristLocWs().GetTranslation();
		Point upperLegPos = m_legIks[iHand].GetHipLocWs().GetTranslation();
		Point kneeJointPos = m_legIks[iHand].GetKneeLocWs().GetTranslation();

		Scalar dist = DistPointSegment(wristLoc, upperLegPos, kneeJointPos);

		F32 minDist = /*m_handInfos[iHand].distPreIk;//*/Min(m_handInfos[iHand].distPreIk, (float)dist);
		//MsgCon("Hand blend based on preIK %d: %f %f\n", iHand, m_handInfos[iHand].distPreIk, ComputeHandBlendFromDistToLeg(m_handInfos[iHand].distPreIk));
		//MsgCon("Hand blend based on postIK %d: %f %f\n", iHand,(float)dist, ComputeHandBlendFromDistToLeg((float)dist));
		//m_handInfos[iHand].blend =
		Seek(m_handInfos[iHand].blend, ComputeHandBlendFromDistToLeg(minDist), FromUPS(10));
		//MsgCon("Hand blend %d: %f %f\n", iHand, minDist, m_handInfos[iHand].blend);
	}
	Locator legAdjustGoals[2];
	Locator weaponGoals[2];
	Locator finalGoals[2];

	for (U32 iHand = 0; iHand < 2; ++iHand)
	{
		Locator parentJointLocator = m_legIks[iHand].GetHipLocWs();
		Locator wristLocator = parentJointLocator.TransformLocator(m_handInfos[iHand].localSpaceOffset);
		Locator currentWristLocator = m_armIks[iHand].GetWristLocWs();
		legAdjustGoals[iHand] = Lerp(currentWristLocator, wristLocator, m_handInfos[iHand].blend);

	}

	for (U32 iHand = 0; iHand < 2; ++iHand)
	{
		Locator weaponGoal0 = Lerp(legAdjustGoals[iHand], legAdjustGoals[1-iHand].TransformLocator(m_handInfos[iHand].localSpaceOffsetToOtherHand), m_handInfos[1-iHand].weaponBlend);
		weaponGoals[iHand] = legAdjustGoals[iHand];

		if (weaponGoal0.GetTranslation().Y() > legAdjustGoals[iHand].GetTranslation().Y())
		{
			weaponGoals[iHand] = Lerp(legAdjustGoals[iHand], weaponGoal0, m_handInfos[iHand].weaponBlend);
		}
		finalGoals[iHand] = weaponGoals[iHand];
	}

	if (m_handBlend > 0.0f)
	{
		for (U32 iHand = 0; iHand < 2; ++iHand)
		{
			{

				ArmIkInstance instance;
				instance.m_ikChain = &m_armIks[iHand];
				instance.m_goalPosWs = finalGoals[iHand].GetTranslation();
				instance.m_tt = m_handBlend;//m_handInfos[iHand].blend;
				SolveArmIk(&instance);

				// readjust the wrist orientation
				const Quat qWristPostIk = m_armIks[iHand].GetWristLocWs().GetRotation();

				const Quat qWristDeltaRot = Normalize(finalGoals[iHand].GetRotation() * Conjugate(qWristPostIk));
				ALWAYS_ASSERT(IsNormal(qWristDeltaRot));

				m_armIks[iHand].RotateWristWs(Slerp(kIdentity, qWristDeltaRot, m_handBlend));
				//g_prim.Draw(DebugCoordAxes(pJointCache->GetJointLocatorWs(m_armIks[iHand].wristJoint), 0.25f));
				//MsgCon("Hand %d offset: %f\n", iHand, (float)Dist(pJointCache->GetJointLocatorWs(m_armIks[iHand].wristJoint).GetTranslation(), finalGoals[iHand].GetTranslation()));
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 CharacterLegIkController::ComputeHandBlendFromDistToLeg(F32 dist)
{
	static const F32 minDist = 0.1f;
	static const F32 maxDist = 0.2f;
	F32 result = 0.0f;
	if (dist <= minDist)
		result = 1.0f;
	else
		result = LerpScaleClamp(0.10f, 0.2f, 1.0f, 0.0f, dist);
	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::UpdateHandsPreIK(const HandWeaponInfo& handWeaponInfo)
{
	// This function is probably broken since the great quadruped IK rewrite of 2019
	ANIM_ASSERTF(m_legCount == kLegCount, ("Quadruped Leg IK does not support updating hands. This will need to be rewritten if we add centaurs or some other four legged creature that also has hands"));

	UpdateHandBlend();
	JointCache* pJointCache = m_pCharacter->GetAnimControl()->GetJointCache();
	for (U32 iHand = 0; iHand < 2; ++iHand)
	{
		{
			Locator wristLocator = m_armIks[iHand].GetWristLocWs();
			Locator parentJointLocator = m_legIks[iHand].GetHipLocWs();
			m_handInfos[iHand].localSpaceOffset = parentJointLocator.UntransformLocator(wristLocator);
			Locator otherWrist = m_armIks[1 - iHand].GetWristLocWs();
			m_handInfos[iHand].localSpaceOffsetToOtherHand = otherWrist.UntransformLocator(wristLocator);
			if (iHand != handWeaponInfo.m_dominantHand && handWeaponInfo.m_gunBoundJoint >= 0)
			{
				Locator gunCheckLocatorWS =  pJointCache->GetJointLocatorWs(handWeaponInfo.m_gunBoundJoint).TransformLocator(handWeaponInfo.m_gunCheckJointLocalSpace);
				//m_handInfos[iHand].weaponBlend =
				Seek(m_handInfos[iHand].weaponBlend, ComputeOffHandWeaponBlend(wristLocator, gunCheckLocatorWS), FromUPS(10.0f));
			}
			else if (iHand == handWeaponInfo.m_dominantHand)
			{
				m_handInfos[iHand].weaponBlend = 1.0f;
			}
			else
				m_handInfos[iHand].weaponBlend = 0.0f;

			//MsgCon("Hand Weapon blend %d: %f\n", iHand, m_handInfos[iHand].weaponBlend);
			const Point wristLoc = wristLocator.GetTranslation();//pJointCache->GetJointLocatorWs(m_armIks[iHand].wristJoint).GetTranslation();
			const Point upperLegPos = parentJointLocator.GetTranslation(); //pJointCache->GetJointLocatorWs(m_iks[iHand].upperThighJoint).GetTranslation();
			const Point kneeJointPos = m_legIks[iHand].GetKneeLocWs().GetTranslation();

			Scalar dist = DistPointSegment(wristLoc, upperLegPos, kneeJointPos);
			m_handInfos[iHand].distPreIk = dist;
		}
	}
}

static const F32 s_minWeaponHandDist = 0.15f;
static const F32 s_maxWeaponHandDist = 0.2f;

/// --------------------------------------------------------------------------------------------------------------- ///
F32 CharacterLegIkController::ComputeOffHandWeaponBlend(const Locator wristLoc, const Locator gunLoc)
{
	Scalar dist = Dist(wristLoc.GetTranslation(), gunLoc.GetTranslation());
	return LerpScaleClamp(s_minWeaponHandDist, s_maxWeaponHandDist, 1.0f, 0.0f, (float)dist);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::UnconstrainHandsToLegs()
{
	m_handInfos[kLeftArm].enabled = m_handInfos[kRightArm].enabled = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::ConstrainHandsToLegs(StringId64 leftHandJoint, StringId64 rightHandJoint)
{
	SetHandConstraint(kLeftArm, m_pCharacter->FindJointIndex(leftHandJoint));
	SetHandConstraint(kRightArm, m_pCharacter->FindJointIndex(rightHandJoint));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::SetHandConstraint(ArmIndex index, I32 jointIndex)
{
	if (m_handInfos[index].enabled && jointIndex >= 0)
	{
		//ALWAYS_ASSERT(m_handInfos[index].constraintJointIndex == jointIndex);
	}
	else if (jointIndex >= 0)
	{
		//ALWAYS_ASSERT(m_handInfos[index].blend < 0.01f);
		m_handInfos[index].enabled = true;
	}
	else
	{
		m_handInfos[index].enabled = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::UpdateHandBlend()
{
	Seek(m_handBlend, m_handBlendTarget, FromUPS(m_handBlendSpeed));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::SetHandBlendTarget(F32 targetBlend, F32 targetTime)
{
	m_handBlendTarget = targetBlend;
	if (targetTime == 0.0f)
		m_handBlendSpeed = 100.0f;
	else
		m_handBlendSpeed = 1.0f / targetTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::DisableHandAdjustment(float blendTime /*= 0.2f*/)
{
	SetHandBlendTarget(0.0f, blendTime);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::PostAnimUpdate()
{
	if (!m_pCharacter->IsRagdollDying() && m_currentLegIk != nullptr)
	{
		ScopedTempAllocator jj(FILE_LINE_FUNC);
		for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
		{
			m_legIks[iLeg].ReadJointCache();
		}

		m_currentLegIk->PostAnimUpdate(m_pCharacter, this);

		for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
		{
			m_legIks[iLeg].DiscardJointCache();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<Point> CharacterLegIkController::GetNextPredictedFootPlant()
{
	if (m_currentLegIk)
	{
		return m_currentLegIk->GetNextPredictedFootPlant();
	}
	return MAYBE::kNothing;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::GetPredictedFootPlants(Maybe<Point>(&pos)[kQuadLegCount]) const
{
	if (m_currentLegIk)
	{
		m_currentLegIk->GetPredictedFootPlants(pos);
	}
	else
	{
		pos[0] = pos[1] = pos[2] = pos[3] = MAYBE::kNothing;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::SetCurrentLegIk(ILegIk* legIk)
{
	if (m_currentLegIk == nullptr)
	{
		m_legRaycaster.SetMinValidTime(m_legRaycaster.GetClock()->GetCurTime());
	}
	m_currentLegIk = legIk;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::SetCurrentArmIk(IArmIk* armIk)
{
	m_currentArmIk = armIk;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::SetScriptIkModeOverride(DC::AnimIkMode ikMode, float fadeTime)
{
	m_scriptOverride = ikMode;
	m_scriptOverrideFadeTime = fadeTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::AnimIkMode CharacterLegIkController::GetScriptIkModeOverride(float* pFadeTimeOut) const
{
	if (pFadeTimeOut)
		 *pFadeTimeOut = m_scriptOverrideFadeTime;

	return m_scriptOverride;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterLegIkController::SanitizeRayCaster()
{
	LegRaycaster::Results results[] = { m_legRaycaster.GetProbeResults(kLeftLeg, 0.5f),
										m_legRaycaster.GetProbeResults(kRightLeg, 0.5f),
										LegRaycaster::Results(),
										LegRaycaster::Results() };
	const bool isQuadruped = m_legCount != 2;

	if (isQuadruped)
	{
		results[kFrontLeftLeg] = m_legRaycaster.GetProbeResults(kFrontLeftLeg, 0.5f);
		results[kFrontRightLeg] = m_legRaycaster.GetProbeResults(kFrontRightLeg, 0.5f);
	}

	bool resultsValid = true;
	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		resultsValid = resultsValid && results[iLeg].m_valid && results[iLeg].m_hitGround;
	}

	if (!resultsValid)
	{
		return;
	}

	ProcessMgr* pProcessMgr = EngineComponents::GetProcessMgr();

	const int legPairCount = m_legCount / 2;
	for (int iPair = 0; iPair < legPairCount; ++iPair)
	{
		const int leftLeg = iPair * 2;
		const int rightLeg = leftLeg + 1;
		ANIM_ASSERT(rightLeg < m_legCount);

		bool hitDeadBody = false;

		if ((Character::FromProcess(pProcessMgr->LookupProcessByPid(results[leftLeg].m_hitProcessId)) != nullptr)
			|| (Character::FromProcess(pProcessMgr->LookupProcessByPid(results[rightLeg].m_hitProcessId)) != nullptr))
		{
			hitDeadBody = true;
		}

		const Point groundPos[] = { results[leftLeg].m_point.GetTranslation(), results[rightLeg].m_point.GetTranslation() };
		const Vector leftToRight = groundPos[1] - groundPos[0];
		const Scalar angle = RadiansToDegrees(Atan2(leftToRight.Y(), LengthXz(leftToRight)));
		if (DEBUG_LEG_IK)
		{
			MsgCon("Ground result angle: %f\n", (float)angle);
			MsgCon("Ground Y diff: %f\n", (float)Abs(leftToRight.Y()));
		}
		const bool yDeltaTooBig = m_pCharacter->IsChild() ? Abs(leftToRight.Y()) > 0.3f : false;

		const float angleThresholdDeg = hitDeadBody ? kLargeFloat : 35.0f;

		if ((Abs(angle) > angleThresholdDeg) || yDeltaTooBig)
		{
			const Plane groundPlane = GetAnimatedGroundPlane(m_pCharacter);
			const Point alignPos = m_pCharacter->GetTranslation();
			const Point posOnPlane = groundPlane.ProjectPoint(alignPos);
			Scalar distToAlign[2];

			for (int iLeg = 0; iLeg < 2; iLeg++)
			{
				Point groundPlanePos;
				LinePlaneIntersect(posOnPlane, groundPlane.GetNormal(), groundPos[iLeg], groundPos[iLeg] + kUnitYAxis, nullptr, &groundPlanePos);
				distToAlign[iLeg] = Abs(groundPlanePos.Y() - groundPos[iLeg].Y());
			}
			for (int iLeg = 0; iLeg < 2; iLeg++)
			{
				if (distToAlign[iLeg] > distToAlign[1 - iLeg])
				{
					if (DEBUG_LEG_IK)
					{
						MsgCon("Disabling leg raycaster contact\n");
					}
					if (results[iLeg].m_meshRaycastResult)
					{
						m_legRaycaster.InvalidateMeshRayResult(iLeg);
					}
					else
					{
						m_legRaycaster.InvalidateCollisionResult(iLeg);
					}
					SanitizeRayCaster();
					return;
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const GroundModel* CharacterLegIkController::GetGroundModel() const
{
	if (m_currentLegIk == &m_moveLegIkNew)
	{
		return m_moveLegIkNew.GetGroundModel();
	}

	return nullptr;
}
