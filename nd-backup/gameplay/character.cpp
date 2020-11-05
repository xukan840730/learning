/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/character.h"

#include "corelib/math/intersection.h"
#include "corelib/math/segment-util.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/bigsort.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-snapshot-node-animation.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/attach-system.h"
#include "ndlib/anim/ik/jacobian-ik.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/anim/joint-modifiers/joint-modifier-data.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/camera/camera-final.h"
#include "ndlib/frame-params.h"
#include "ndlib/fx/fxmgr.h"
#include "ndlib/nd-config.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/ndphys/rigid-body-base.h"
#include "ndlib/net/nd-net-info.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/process/spawn-info.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/dev/render-options.h"
#include "ndlib/render/draw-control.h"
#include "ndlib/render/interface/fg-instance.h"
#include "ndlib/render/look.h"
#include "ndlib/render/ngen/surfaces.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/render/texture/instance-texture-table.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/render/util/text.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/scriptx/h/surface-defines.h"
#include "ndlib/tools-shared/patdefs.h"

#include "gamelib/anim/gesture-controller.h"
#include "gamelib/anim/motion-matching/motion-matching.h"
#include "gamelib/audio/nd-vox-controller-base.h"
#include "gamelib/camera/camera-manager.h"
#include "gamelib/camera/camera-strafe-base.h"
#include "gamelib/facts/fact-manager.h"
#include "gamelib/fx/splashers.h"
#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/ai/base/nd-ai-util.h"
#include "gamelib/gameplay/armor.h"
#include "gamelib/gameplay/character-leg-raycaster.h"
#include "gamelib/gameplay/character-manager.h"
#include "gamelib/gameplay/character-melee-impulse.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/cover-action-pack.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nd-attachable-object.h"
#include "gamelib/gameplay/nd-attack-handler.h"
#include "gamelib/gameplay/nd-effect-control.h"
#include "gamelib/gameplay/nd-prop-control.h"
#include "gamelib/gameplay/nd-subsystem-mgr.h"
#include "gamelib/gameplay/process-phys-rope.h"
#include "gamelib/level/art-item-anim.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/ndphys/buoyancy.h"
#include "gamelib/ndphys/collision-cast-interface.h"
#include "gamelib/ndphys/composite-body-penetration.h"
#include "gamelib/ndphys/composite-body.h"
#include "gamelib/ndphys/composite-ragdoll-controller.h"
#include "gamelib/ndphys/debugdraw/havok-debug-draw.h"
#include "gamelib/ndphys/havok.h"
#include "gamelib/ndphys/phys-fx.h"
#include "gamelib/ndphys/rigid-body.h"
#include "gamelib/ndphys/rope/rope2.h"
#include "gamelib/render/particle/particle.h"
#include "gamelib/script/nd-script-arg-iterator.h"
#include "gamelib/scriptx/h/anim-character-defines.h"
#include "gamelib/scriptx/h/anim-gas-mask-defines.h"
#include "gamelib/scriptx/h/character-fx-defines.h"
#include "gamelib/scriptx/h/characters-collision-settings-defines.h"
#include "gamelib/scriptx/h/nd-gore-defines.h"
#include "gamelib/scriptx/h/splasher-defines.h"
#include "gamelib/stats/event-log.h"

#include <Physics/Physics/Collide/Shape/Convex/Capsule/hknpCapsuleShape.h>

#include <Eigen/Dense>

/// --------------------------------------------------------------------------------------------------------------- ///
#define GRAB_WITHOUT_AP_REFERENCES 0

PROCESS_REGISTER_ABSTRACT(Character, NdGameObject);

FROM_PROCESS_DEFINE(Character);

/// --------------------------------------------------------------------------------------------------------------- ///
bool g_disableRagdolls = false;
bool g_disableRagdollEffs = false;

bool g_updateCharacterPushers = true;
bool g_characterDontDestroyRigidBodiesOnRagdoll = false;

bool g_debugDrawRagdollGroundProbes = false;

bool g_ragdollKeyframedTillCollision = false;

bool g_showBodyImpactSurfaceType = false;
bool g_showShoulderImpactSurfaceType = false;
bool g_showNpcHandImpactSurfaceType = false;

F32 g_trunkHeightTest = 0.3f;
F32 g_limbsHeightTest = 0.18f;
F32 g_distTest = 0.4f;

bool g_legIkOnRagdolls = false;
bool g_debugPrintCharacterTextInFinal = false;
bool g_debugPlayerInventoryInFinal = false;
bool g_debugPlayerInventorySelectionInFinal = false;
bool g_debugPlayerInventoryCheckpointInFinal = false;
bool g_debugPlayerClamberTeleportBug = false;


static const U32 s_uCcHealthPct = CcVar("health-pct");

extern DC::LegIkConstants* g_pDebugLegIKConstants;
extern bool g_useDebugLegIKConstants;

char Character::m_debugTextInFinal[4096];
int Character::m_debugTextInFinalSize = 0;
NdAtomicLock Character::m_debugTextLock;

SCRIPT_FUNC("get-gas-mask", DcGetGasMask)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	Process* pProcess = args.NextProcess();
	if (!pProcess)
	{
		MsgErr("get-gas-mask: failed to find the process.");
		return ScriptValue(INVALID_STRING_ID_64);
	}

	Character* pCharacter = Character::FromProcess(pProcess);
	if (!pCharacter)
	{
		MsgErr("get-gas-mask: failed to find the character.");
		return ScriptValue(INVALID_STRING_ID_64);
	}

	const NdAttachableObject* pAttachable = pCharacter->GetGasMask();
	if (!pAttachable)
		return ScriptValue(INVALID_STRING_ID_64);

	StringId64 gasMaskSid = pAttachable->GetUserId();
	return ScriptValue(gasMaskSid);
}

SCRIPT_FUNC("set-gas-mask", DcSetGasMask)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	Process* pProcess = args.NextProcess();
	if (!pProcess)
	{
		MsgErr("set-gas-mask: failed to find the process.");
		return ScriptValue();
	}

	Character* pCharacter = Character::FromProcess(pProcess);
	if (!pCharacter)
	{
		MsgErr("set-gas-mask: failed to find the character.");
		return ScriptValue();
	}

	pProcess = args.NextProcess();
	if (!pProcess)
	{
		MsgErr("set-gas-mask: failed to find the gas mask process.");
		return ScriptValue();
	}

	NdAttachableObject* pGasMask = NdAttachableObject::FromProcess(pProcess);
	if (!pGasMask)
	{
		MsgErr("set-gas-mask: failed to find the gas mask process.");
		return ScriptValue();
	}

	pCharacter->SetGasMask(pGasMask);
	return ScriptValue();
}

SCRIPT_FUNC("set-riot-visor", DcSetRiotVisor)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	Character* pCharacter = args.NextCharacter();
	if (!pCharacter)
	{
		MsgErr("set-riot-visor: failed to find the character.");
		return ScriptValue();
	}

	NdAttachableObject* pVisor = NdAttachableObject::FromProcess(args.NextProcess());
	if (!pVisor)
	{
		MsgErr("set-riot-visor: failed to find the visor process.");
		return ScriptValue();
	}

	if (IDrawControl* pDrawControl = pVisor->GetDrawControl())
	{
		pDrawControl->ClearInstanceFlag(FgInstance::kDisableMeshRayCasts | FgInstance::kNoDecals
										| FgInstance::kNoProjection);
	}

	pCharacter->SetRiotVisor(pVisor);
	return ScriptValue();
}


/// --------------------------------------------------------------------------------------------------------------- ///
// Removes the "logical" gas mask from the character. The gas mask is not visible anymore from other systems. But it doesn't remove
// the gas mask game object.
void RemoveGasMask(Character* pCharacter)
{
	pCharacter->SetGasMask(nullptr);
	pCharacter->ClearHasGasMask();
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Attach the gas mask to the character using the gas mask settings.
void OnAttachGasMask(Character* pCharacter)
{
	if (pCharacter->IsState(SID("Slide")))
	{
		MsgOut("The character %s is sliding, you need to make sure we are not sliding to put on the mask\n", DevKitOnly_StringIdToString(pCharacter->GetUserId()));
		return;
	}

	StringId64 gasMaskSettingId = pCharacter->GetGasMaskSettingsId();
	if (gasMaskSettingId == INVALID_STRING_ID_64)
	{
		MsgOut("The character %s has no gas mask setting id. Did you forget to call set-gas-mask-setting-id from your dc script?\n", DevKitOnly_StringIdToString(pCharacter->GetUserId()));
		return;
	}

	const DC::GasMaskSetup* pGasMaskSetting = ScriptManager::Lookup<DC::GasMaskSetup>(gasMaskSettingId, nullptr);
	if (!pGasMaskSetting)
	{
		MsgOut("Can't find gas mask setting %s in dc.\n", DevKitOnly_StringIdToString(gasMaskSettingId));
		return;
	}

	const Locator offset(kIdentity);
	SendEvent(SID("set-attached"), pCharacter->GetGasMask(), pCharacter, pGasMaskSetting->m_attachPoint, &offset);
	pCharacter->SetHasGasMask(pGasMaskSetting->m_futz);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Special case when a character removes his gas mask. The gas mask is attached to the right hand
// to play animations.
void OnAttachGasMaskToRightHand(Character* pCharacter)
{
	if (pCharacter->IsState(SID("Slide")))
	{
		MsgOut("The character %s is sliding, you need to make sure we are not sliding to take off the mask\n", DevKitOnly_StringIdToString(pCharacter->GetUserId()));
		return;
	}

	StringId64 gasMaskSettingId = pCharacter->GetGasMaskSettingsId();
	if (gasMaskSettingId == INVALID_STRING_ID_64)
	{
		MsgOut("The character %s has no gas mask setting id. Did you forget to call set-gas-mask-setting-id from your dc script?\n", DevKitOnly_StringIdToString(pCharacter->GetUserId()));
		return;
	}

	const DC::GasMaskSetup* pGasMaskSetting = ScriptManager::Lookup<DC::GasMaskSetup>(gasMaskSettingId, nullptr);
	if (!pGasMaskSetting)
	{
		MsgOut("Can't find gas mask setting %s in dc.\n", DevKitOnly_StringIdToString(gasMaskSettingId));
		return;
	}

	const Locator offset(kIdentity);
	SendEvent(SID("set-attached"), pCharacter->GetGasMask(), pCharacter, SID("rightHand"), &offset);
	RemoveGasMask(pCharacter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Completely detach the gas mask from the character.
void OnDetachGasMask(Character* pCharacter)
{
	if (pCharacter->IsState(SID("Slide")))
	{
		MsgOut("The character %s is sliding, you need to make sure we are not sliding to take off the mask\n", DevKitOnly_StringIdToString(pCharacter->GetUserId()));
		return;
	}

	StringId64 gasMaskSettingId = pCharacter->GetGasMaskSettingsId();
	if (gasMaskSettingId == INVALID_STRING_ID_64)
	{
		MsgOut("The character %s has no gas mask setting id. Did you forget to call set-gas-mask-setting-id from your dc script?\n", DevKitOnly_StringIdToString(pCharacter->GetUserId()));
		return;
	}

	const DC::GasMaskSetup* pGasMaskSetting = ScriptManager::Lookup<DC::GasMaskSetup>(gasMaskSettingId, nullptr);
	if (!pGasMaskSetting)
	{
		MsgOut("Can't find gas mask setting %s in dc.\n", DevKitOnly_StringIdToString(gasMaskSettingId));
		return;
	}

	SendEvent(SID("detach-without-physicalizing"), pCharacter->GetGasMask());
	RemoveGasMask(pCharacter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Character::Character()
: m_pHealthSystem(nullptr)
, m_pHeartRateMonitor(nullptr)
, m_pMeleeImpulse(nullptr)
, m_pCharacterEffectTracker(nullptr)
, m_pNearestAlliesByLinearDist(nullptr)
, m_pNearestAlliesByPathDist(nullptr)
, m_pNearestAlliesForDialog(nullptr)
, m_nearestAlliesCapacity(0)
, m_isNpc(false)
, m_isBuddyNpc(false)
, m_invisibleToAi(false)
, m_inAiDarknessRegion(false)
, m_inAiDarkness(false)
, m_validMeleeTarget(true)
, m_apRefValid(false)
, m_allowRagdoll(true)
, m_ragdollGroundProbesEnabled(true)
, m_isPlayingNetDeathAnim(false)
, m_pRagdoll(nullptr)
, m_bloodMaskHandle(kFxRenderMaskHandleInvalid)
, m_coverDirWs(kZero)
, m_rawCoverDirWs(kZero)
, m_vulnerability(DC::kVulnerabilityStanding)
, m_animationVulnerabilityOverride(DC::kVulnerabilityNone)
, m_startVulnerabilityTime(TimeFrameNegInfinity())
, m_lastMeleeMoveActiveTime(TimeFrameNegInfinity())
, m_shotsFired(0)
, m_ragdollNonEffControlled(false)
, m_ragdollOutOfBroadphase(false)
, m_ragdollDyingSettings(INVALID_STRING_ID_64)
, m_ragdollTimer(-1.0f)
, m_ragdollBlendOutTimer(-1.0f)
, m_bloodPoolScale(-1.0f)
, m_bloodPoolJoint(INVALID_STRING_ID_64)
, m_buoyancyEnabled(false)
, m_photoModeFlashlight(false)
, m_shouldShowHitTicks(true)
, m_pSubsystemMgr(nullptr)
, m_characterName(nullptr)
, m_characterNameError(nullptr)
, m_isDoomed(false)
, m_allowDismemberment(true)
, m_clientControlsRagdollPostAnimBlending(false)
, m_suppressFootPlantIkThisFrame(false)
, m_isInProneRegion(0)
, m_allowDeadRagdollSnapBackToAnimation(false)
, m_disableThrowableSelfDamage(false)
, m_allowBackSplatterBlood(true)
{
	m_characterFlags.m_rawBits = 0;
	m_gameObjectConfig.m_animControlMaxLayers = 34;
	m_gameObjectConfig.m_drawControlMaxMeshes = 12;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Character::~Character()
{
	PROFILE(Processes, CharacterDestructor);

	for (U32 i = 0; i < kNumCharacterParticles; i++)
	{
		if (m_hCharacterParticle[i].HandleValid())
		{
			KillProcess(m_hCharacterParticle[i]);
		}
	}

	if (m_bloodMaskHandle != kFxRenderMaskHandleInvalid)
	{
		g_fxMgr.ReleaseFxRenderMask(m_bloodMaskHandle);
		m_bloodMaskHandle = kFxRenderMaskHandleInvalid;
	}

	for (int i = 0; i < m_buoyancyList.Size(); i++)
	{
		if (m_buoyancyList[i])
		{
			NDI_DELETE m_buoyancyList[i];
			m_buoyancyList[i] = nullptr;
		}
	}

	if (m_pRagdoll)
	{
		delete m_pRagdoll;
	}

	DestroyControllers();
}

void Character::DestroyControllers()
{
	if (m_pSubsystemMgr)
	{
		m_pSubsystemMgr->Destroy();
		m_pSubsystemMgr = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 Character::GetEffectListSize() const
{
	// This bumps the maximum number of effects a character can collect in one frame. - CGY
	return 60;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err Character::Init(const ProcessSpawnInfo& spawn)
{
	MEMTRACK_BEGIN(11);

	m_emotionControl.SetOwner(this);

	m_cachedTargChestWs = kOrigin;

	m_noBulletHitTime = TimeFrameNegInfinity();
	m_noBulletShotTime = TimeFrameNegInfinity();
	m_ragdollBlendOutPendingSettings = INVALID_STRING_ID_64;
	m_ragdollPendingSettingsDelay = 0.0f;
	m_disableRagdollBuoyancyInParentBB = true;

	m_lastFullEffectTime = Seconds(0);
	m_fullEffectCount = 0;
	m_lastShotTime = TimeFrameNegInfinity();
	m_lastShotPlayerTime = TimeFrameNegInfinity();
	m_lastMeleeMoveStartedTime = TimeFrameNegInfinity();
	m_lastMeleeAttackActiveTime = TimeFrameNegInfinity();
	m_lastBegForLifeTime = TimeFrameNegInfinity();
	m_lastMeleeMoveActiveTime = TimeFrameNegInfinity();
	m_lastMeleeOpponentTime = TimeFrameNegInfinity();
	m_lastGotShotTime = TimeFrameNegInfinity();
	m_lastBreathEventFrame = -1;
	m_nextArmBloodLower = true;
	m_shotsFired = 0;
	m_killerUserId = INVALID_STRING_ID_64;


	m_emotionalOverrideAnim = INVALID_STRING_ID_64;
	m_meshEffectsSpawned = 0;
	m_spawnedSingleExitParticleThisFrame = false;

	const I32* pMaxMeshEffects = ScriptManager::Lookup<I32>(SID("*max-char-mesh-effects-per-frame*"), nullptr);
	if (!pMaxMeshEffects)
	{
		MsgScriptErr("Failed to lookup *max-char-mesh-effects-per-frame*\n");
	}

	m_maxMeshEffects = pMaxMeshEffects ? *pMaxMeshEffects : 1;

	m_pHealthSystem = nullptr;
	m_surfaceTypeProbeCounter = nullptr;

	Err result = ParentClass::Init(spawn);
	if (result.Failed())
		return result;

	MEMTRACK_SAMPLE("ParentClass");

	// Make sure mesh ray casts are not done against my PmInstance.
	// !!!IMPORTANT!!! This MUST be done here so it works for the player, NetCharacter and NPCs.  (Don't fuck with this ever again, please! This means YOU.)
	IDrawControl* pDrawControl = GetDrawControl();
	if (pDrawControl)
	{
		pDrawControl->SetInstanceFlag(FgInstance::kDisableMeshRayCasts | FgInstance::kNoProjection);
	}

	InitGoreFilter();
	MEMTRACK_SAMPLE("GoreFilter");

	SetupRagdollHelper(spawn);
	MEMTRACK_SAMPLE("RegdollHelper");

	m_inCover = false;
	m_horzCoverAngle = 0.0f;
	m_vertCoverAngle = 0.0f;
	m_coverDirWs = kUnitXAxis;
	m_feetSplashersInWater = false;

	m_listenModeFootEffectTime = TimeFrameNegInfinity();

	SetVulnerability(DC::kVulnerabilityStanding);

	m_pHealthSystem = CreateHealthSystem();
	if (!m_pHealthSystem)
		return Err::kErrOutOfMemory;

	MEMTRACK_SAMPLE("HealthSys");
	m_pMeleeImpulse = NDI_NEW CharacterMeleeImpulse();
	MEMTRACK_SAMPLE("MeleeImpulse");

	m_pCharacterEffectTracker = NDI_NEW CharacterEffectTracker;
	MEMTRACK_SAMPLE("m_pCharacterEffectTracker");

	m_renderTargetSamplingId = INVALID_STRING_ID_64;

	if (FgAnimData* pAnimData = GetAnimData())
	{
		const I32 leftHandPropAttachJointIndex = pAnimData->FindJoint(SID("l_hand_prop_attachment"));
		const I32 rightHandPropAttachJointIndex = pAnimData->FindJoint(SID("r_hand_prop_attachment"));
		pAnimData->m_boundingSphereExcludeJoints[0] = leftHandPropAttachJointIndex;
		pAnimData->m_boundingSphereExcludeJoints[1] = rightHandPropAttachJointIndex;
	}

	for (U32F ii = 0; ii < kNumBodyImpactSound; ii++)
		m_bodyMeshRaycastResult[ii].Reset(SID("stone-asphalt"));

	for (U32F ii = 0; ii < kNumShoulderImpactSound; ii++)
		m_shoulderRaycastResult[ii].Reset(SID("stone-asphalt"));

	for (U32F ii = 0; ii < kArmCount; ii++)
		m_handRaycastResult[ii].Reset(SID("stone-asphalt"));	// initialize to most common surface

	m_bodyImpactRayLength = 2.f;
	m_bodyImpactRayRadius = 0.05f;
	m_shoulderImpactRayLength = 2.f;
	m_shoulderImpactRayRadius = 0.05f;

	m_overrideSurfaceType.Reset();

	MEMTRACK_SAMPLE("BodyMeshRayCast");
	if (NeedsHighlightShaderParams())
	{
		AllocateShaderInstanceParams();
		SetSpawnShaderInstanceParams(spawn);
	}
	MEMTRACK_SAMPLE("ShaderInstParams");

	if (m_nearestAlliesCapacity > 0)
	{
		m_pNearestAlliesByLinearDist = NDI_NEW NearestAllies;
		ALWAYS_ASSERT(m_pNearestAlliesByLinearDist);
		m_pNearestAlliesByLinearDist->m_ahAlly = NDI_NEW CharacterHandle[m_nearestAlliesCapacity];
		m_pNearestAlliesByLinearDist->m_count = 0;
		m_pNearestAlliesByLinearDist->m_gameFrameNumber = 0;

		m_pNearestAlliesByPathDist = NDI_NEW NearestAllies;
		ALWAYS_ASSERT(m_pNearestAlliesByPathDist);
		m_pNearestAlliesByPathDist->m_ahAlly = NDI_NEW CharacterHandle[m_nearestAlliesCapacity];
		m_pNearestAlliesByPathDist->m_count = 0;
		m_pNearestAlliesByPathDist->m_gameFrameNumber = 0;

		m_pNearestAlliesForDialog = NDI_NEW NearestAllies;
		ALWAYS_ASSERT(m_pNearestAlliesForDialog);
		m_pNearestAlliesForDialog->m_ahAlly = NDI_NEW CharacterHandle[m_nearestAlliesCapacity];
		m_pNearestAlliesForDialog->m_count = 0;
		m_pNearestAlliesForDialog->m_gameFrameNumber = 0;
	}
	MEMTRACK_SAMPLE("NearestAllies");

	if (true)
	{
		m_toeJointIndices[0] = m_pAnimData->FindJoint(SID("l_toe"));
		m_toeJointIndices[1] = m_pAnimData->FindJoint(SID("r_toe"));
	}
	else
	{
		m_toeJointIndices[0] = -1;
		m_toeJointIndices[1] = -1;
	}

	m_groundMeshRaycastResult[kMeshRaycastGround].Reset(SID("stone-asphalt"));
	m_groundMeshRaycastResult[kMeshRaycastWater].Reset(INVALID_STRING_ID_64);

	m_lastTimeGroundSurfaceTypeProbe = Seconds(0);

	m_wetnessProbeIndex = 0;

	m_shouldShowHitTicks   = spawn.GetData<bool>(SID("show-hit-ticks?"), true);
	m_allowRagdollBuoyancy = spawn.GetData<bool>(SID("allow-ragdoll-buoyancy"), true);
	m_buoyancyUseDynamicWater = spawn.GetData<bool>(SID("buoyancy-use-dynamic-water"), true);
	m_disableHitEffects = spawn.GetData<bool>(SID("disable-hit-effects"), false);
	m_allowDismemberment = !spawn.GetData<bool>(SID("no-dismemberment"), false);
	m_disableCameraFadeOut = spawn.GetData(SID("disable-camera-fade-out"), false);
	m_disableCameraFadeOutUntilFrame = 0;

	if (spawn.GetData<bool>(SID("no-gore"), false))
	{
		m_goreFilter |= DC::kGoreFilterDisallowAll;
	}

	StringId64 genderOverride = spawn.GetData<StringId64>(SID("gender"), INVALID_STRING_ID_64);
	if (genderOverride != INVALID_STRING_ID_64)
	{
		m_isFemale = genderOverride == SID("female");
	}

	const StringId64 legIkConstants = GetLegIkConstantsSettingsId();
	if (legIkConstants != INVALID_STRING_ID_64)
	{
		CONST_EXPR StringId64 kLegIkSettingsModule = SID("ik-settings");
		m_pLegIkConstants = ScriptPointer<DC::LegIkConstants>(legIkConstants, kLegIkSettingsModule);
		ANIM_ASSERTF(m_pLegIkConstants.Valid(), ("Could not find %s in module %s", DevKitOnly_StringIdToString(legIkConstants), DevKitOnly_StringIdToString(kLegIkSettingsModule)));
	}

	MEMTRACK_END("Character");

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::DetermineGender()
{
	m_isFemale = IsFemaleSkel(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err Character::InitializeDrawControl(const ProcessSpawnInfo& spawn, const ResolvedLook& resolvedLook)
{
	const Err result = ParentClass::InitializeDrawControl(spawn, resolvedLook);

	DetermineGender();

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err Character::InitCompositeBody(CompositeBodyInitInfo* pInitInfo)
{
	CompositeBodyInitInfo info;
	if (pInitInfo)
	{
		info = *pInitInfo;
	}

	info.m_initialMotionType = kRigidBodyMotionTypeGameDriven;
	info.m_bCharacter = true;
	info.m_pCharColl = GetCharacterCollision();
	GAMEPLAY_ASSERT(info.m_pCharColl);

	Err err = ParentClass::InitCompositeBody(&info);
	if(err != Err::kOK)
	{
		return err;
	}

	CharacterCollision::PostInit(*this, info.m_pCharColl);

	return err;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ProcessSnapshot* Character::AllocateSnapshot() const
{
	return CharacterSnapshot::Create(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::RefreshSnapshot(ProcessSnapshot* pSnapshot) const
{
	PROFILE(Processes, Character_RefreshSnapshot);

	ParentClass::RefreshSnapshot(pSnapshot);

	if (CharacterSnapshot* pCharSnapshot = CharacterSnapshot::FromSnapshot(pSnapshot))
	{
		pCharSnapshot->m_stanceState		   = GetStanceState();
		pCharSnapshot->m_isAimingFirearm       = IsAimingFirearm();
		pCharSnapshot->m_jumping               = IsJumping();
		pCharSnapshot->m_crouched              = IsCrouched();
		pCharSnapshot->m_prone                 = IsProne();
		pCharSnapshot->m_dead                  = IsDead();
		pCharSnapshot->m_down                  = IsDown();
		pCharSnapshot->m_climbing              = IsClimbing();
		pCharSnapshot->m_hanging               = IsHanging();
		pCharSnapshot->m_freeRoping            = IsFreeRoping();
		pCharSnapshot->m_inCover               = GetTakingCover();
		pCharSnapshot->m_isBuddyNpc = IsBuddyNpc();
		pCharSnapshot->m_isRagdollPhysicalized = IsRagdollPhysicalized();
		pCharSnapshot->m_isInStealthVegetaion  = IsInStealthVegetation();
		pCharSnapshot->m_isRagdollPowerFadedOut = IsRagdollPowerFadedOut();
		pCharSnapshot->m_isSprinting = IsSprinting();

		pCharSnapshot->m_lastShotTime = GetLastShotTime();
		pCharSnapshot->m_eyeWs        = GetEyeWs();
		pCharSnapshot->m_targChestWs  = GetAttachSystem()->GetAttachPositionById(SID("targChest"));
		pCharSnapshot->m_earWs        = GetEarWs();

		pCharSnapshot->m_coverDirWs     = GetTakingCoverDirectionWs();
		pCharSnapshot->m_horzCoverAngle = GetHorzCoverAngle();
		pCharSnapshot->m_vertCoverAngle = GetVertCoverAngle();

		pCharSnapshot->m_coverPosWs = kInvalidPoint;
		if (const ActionPack* pAp = GetEnteredActionPack())
		{
			if (pAp->GetType() == ActionPack::kCoverActionPack)
			{
				const CoverActionPack* const pCoverAp = static_cast<const CoverActionPack* const>(pAp);
				pCharSnapshot->m_coverPosWs = pCoverAp->GetDefensivePosWs();
				pCharSnapshot->m_coverWallLocWs = pCoverAp->GetLocatorWs();
			}
		}

		if (m_pHealthSystem)
		{
			pCharSnapshot->m_healthPercent = m_pHealthSystem->GetHealthPercentage();
			pCharSnapshot->m_curHealth = m_pHealthSystem->GetHealth();
		}
		else
		{
			pCharSnapshot->m_curHealth = 100;
			pCharSnapshot->m_healthPercent = 1.0f;
		}

		pCharSnapshot->m_lastMeleeOpponentTime = m_lastMeleeOpponentTime;
		pCharSnapshot->m_otherMeleeCharacter = GetOtherMeleeCharacter();

		pCharSnapshot->m_cachedSurfaceType = GetCachedFootSurfaceType();

		const AnimControl* pAnimControl = GetAnimControl();
		const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
		const AnimStateInstance* pCurInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;
		pCharSnapshot->m_curAnimStateFlagSyncState = pCurInstance ? (pCurInstance->GetStateFlags() & DC::kAnimStateFlagSyncState) : false;
	}
}

void Character::ResolveLook(ResolvedLook& resolvedLook, const SpawnInfo& spawn, StringId64 desiredActorOrLookId)
{
	ParentClass::ResolveLook(resolvedLook, spawn, desiredActorOrLookId);

	if (g_ndConfig.m_pNetInfo->IsNetActive())
	{
		resolvedLook.m_skeletonId = g_ndConfig.m_pNetInfo->ResolveSkeletonOverride(resolvedLook.m_skeletonId);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::MakeCheap(bool wantCheap)
{
	if (m_characterFlags.m_cheap != wantCheap)
	{
		m_characterFlags.m_cheap = wantCheap;
		ConvertToFromCheap();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::ConvertToFromCheap()
{
	// TBD
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NdGameObject* Character::GetOtherEffectObject(const EffectAnimInfo*)
{
	return GetOtherMeleeCharacter().ToProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SpawnCharacterEffects(EffectControlSpawnInfo* aSpawnInfos, U32 numToSpawn)
{
	DespawnCharacterEffects();

	ASSERT(numToSpawn <= kNumCharacterParticles);
	for (U32 i = 0; i < Min(kNumCharacterParticles, numToSpawn); i++)
	{
		EffectControlSpawnInfo& spawnInfo = aSpawnInfos[i];

		if (spawnInfo.m_name != INVALID_STRING_ID_64)
		{
			Locator spawnLoc = GetLocator();

			FgAnimData& animData = GetAnimControl()->GetAnimData();
			JointCache& jointCache = animData.m_jointCache;

			int nJointCount = static_cast<int>(jointCache.GetNumTotalJoints());
			int nJoint = animData.FindJoint(spawnInfo.m_jointId);

			if ((nJoint >= 0) && (nJoint < nJointCount))
			{
				spawnLoc = jointCache.GetJointLocatorWs(nJoint);
			}

			SpawnInfo partSpawn(EngineComponents::GetNdGameInfo()->m_particleTrackerClass);
			partSpawn.m_pRoot = &spawnLoc;
			partSpawn.m_pUserData = &spawnInfo;
			partSpawn.m_pParent = EngineComponents::GetProcessMgr()->m_pAttachTree;
			m_hCharacterParticle[i] = NewProcess(partSpawn);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::DespawnCharacterEffects()
{
	for (U32 i = 0; i < kNumCharacterParticles; i++)
	{
		if (m_hCharacterParticle[i].HandleValid())
		{
			KillProcess(m_hCharacterParticle[i]);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err Character::PostInit(const ProcessSpawnInfo& spawn)
{
	Err result = ParentClass::PostInit(spawn);

	CharacterManager::GetManager().Register(this);

	SpawnCharacterEffects();

	if (IGestureController* pGestureController = GetGestureController())
	{
		pGestureController->PostInit();
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::PreUpdate()
{
	ParentClass::PreUpdate();
	if (m_ragdollOutOfBroadphase)
	{
		KillProcess(this);
	}

	UpdateTimedHighlight();

	if (m_pSubsystemMgr)
	{
		m_pSubsystemMgr->CleanDeadSubsystems();
		m_pSubsystemMgr->PreProcessUpdate();
	}

	m_spawnedSingleExitParticleThisFrame = false;
	m_meshEffectsSpawned = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SetInProneRegion()
{
	if (m_isInProneRegion == 0)
	{
		// we weren't in prone last frame, but are this frame
		g_eventLog.LogEvent("player-in-prone-hiding-space", 1);

	}
	m_isInProneRegion = 2;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::UpdateDc()
{
	if (m_pHeartRateMonitor)
		m_pHeartRateMonitor->UpdateDc();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::HandleTriggeredEffect(const EffectAnimInfo* pEffectAnimInfo)
{
	if (IsRagdollPowerFadedOut())
	{
		// Ragdoll power is fully blended out. Any eff should be irrelevant or plain wrong at this point
		return;
	}

	if (pEffectAnimInfo->m_pEffect->GetTagByName(SID("valid-if-ellie")) && !IsEllie())
		return;

	if (pEffectAnimInfo->m_pEffect->GetTagByName(SID("valid-if-abby")) && !IsAbby())
		return;

	StringId64 effId = pEffectAnimInfo->m_pEffect->GetNameId();
	switch (effId.GetValue())
	{
	case SID_VAL("apply-impulse"):
		{
			RigidBody* pBody = GetBinding().GetRigidBody();
			if (!pBody)
			{
				BoundFrame bf;
				GetApOrigin(bf);
				pBody = bf.GetBinding().GetRigidBody();
			}
			if (!pBody)
			{
				NdGameObject* pVehicle = NdGameObject::FromProcess( GetEnteredVehicle().ToMutableProcess() );
				if (pVehicle)
				{
					if (CompositeBody* pCompBody = pVehicle->GetCompositeBody())
					{
						const U32F chassisIndex = pCompBody->FindBodyIndexByJointSid( SID("body") );
						if (chassisIndex != CompositeBody::kInvalidBodyIndex)
						{
							pBody = pCompBody->GetBody(chassisIndex);
						}
					}
				}
			}
			if (pBody)
			{
				Vector impulse(kZero);
				const EffectAnimEntryTag* pTag;
				pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("impulse-x"));
				if (pTag)
				{
					impulse.SetX(pTag->GetValueAsF32());
				}
				pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("impulse-y"));
				if (pTag)
				{
					impulse.SetY(pTag->GetValueAsF32());
				}
				pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("impulse-z"));
				if (pTag)
				{
					impulse.SetZ(pTag->GetValueAsF32());
				}

				Locator loc = GetLocator();
				pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("joint"));
				if (pTag)
				{
					StringId64 jointSid = pTag->GetValueAsStringId();
					I16 iJoint = GetAnimData()->FindJoint(jointSid);
					if (iJoint >= 0)
						loc = GetAnimData()->m_jointCache.GetJointLocatorWs(iJoint);
				}

				F32 time = 0.0f;
				pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("time"));
				if (pTag)
				{
					time = pTag->GetValueAsF32();
				}

				pBody->ApplyPointImpulse(loc.GetTranslation(), loc.TransformVector(impulse), time);
			}
			return;
		}
	case SID_VAL("power-ragdoll-disable"):
		{
			const EffectAnimEntryTag* pTag;
			pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("player-only"));
			U32F playerOnly = pTag ? pTag->GetValueAsU32() : 0;
			if (playerOnly && !IsKindOf(SID("Player")))
				break;
			pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("blend"));
			F32 blendTime = pTag ? pTag->GetValueAsF32() : 0.2f;
			pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("time"));
			F32 time = pTag ? pTag->GetValueAsF32() : 0.01f;
			SetPowerRagdollDisabled(true, blendTime, time);
			return;
		}
	case SID_VAL("power-ragdoll-on"):
	case SID_VAL("power-ragdoll-off"):
		{
			if (!g_disableRagdollEffs && pEffectAnimInfo->m_topTrackInstance)
			{
				const EffectAnimEntryTag* pTag;
				pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("player-only"));
				U32F playerOnly = pTag ? pTag->GetValueAsU32() : 0;
				if (playerOnly && !IsKindOf(SID("Player")))
					break;
				pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("settings"));
				StringId64 settingsId = pTag ? pTag->GetValueAsStringId() : INVALID_STRING_ID_64;
				pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("blend"));
				F32 blend = pTag ? pTag->GetValueAsF32() : 0.2f;

				if (effId == SID("power-ragdoll-on"))
				{
					PhysicalizeRagdoll(false, settingsId, blend, true);
				}
				else
				{
					BlendOutRagdoll(blend, true);
				}
			}
			return;
		}
	case SID_VAL("blend-out-ragdoll-power"):
		{
			if (IsRagdollPhysicalized() && IsRagdollDying())
			{
				const EffectAnimEntryTag* pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("blend-time"));
				F32 blendTime = pTag ? pTag->GetValueAsF32() : -1.0f;
				if (m_pRagdoll->GetRagdollController()->GetKeepMotors())
				{
					m_pRagdoll->GetRagdollController()->SetKeepMotors(false, blendTime);
				}
				else
				{
					m_pRagdoll->GetRagdollController()->PowerOff("blend-out-ragdoll-power eff", blendTime);
				}
			}
			return;
		}
	case SID_VAL("ragdoll-detach-platform"):
		{
			// Used for ragdolls falling of vehicles
			if (m_pRagdoll)
			{
				m_pRagdoll->SetParentBody(nullptr);
				m_pRagdoll->SetAutoUpdateParentBody(false);
			}
			return;
		}

	case SID_VAL("ragdoll-ground-probes-on"):
		{
			m_ragdollGroundProbesEnabled = true;
			return;
		}

	case SID_VAL("attach-gas-mask"):
		{
			OnAttachGasMask(this);
			return;
		}
		break;

	case SID_VAL("attach-gas-mask-to-right-hand"):
		{
			OnAttachGasMaskToRightHand(this);
			return;
		}
		break;


	}

	if (ProcessPhysRope* pRope = m_hRope.ToMutableProcess())
	{
		pRope->HandleTriggeredCharacterEffect(pEffectAnimInfo, this);
	}

	if (NdGameObject* pGo = m_hEffForwarding.ToMutableProcess())
	{
		pGo->HandleTriggeredCharacterEffect(pEffectAnimInfo, this);
	}

	ParentClass::HandleTriggeredEffect(pEffectAnimInfo);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SetGasMaskSettingsId(StringId64 gasMaskSettingId)
{
	ALWAYS_ASSERTF(false, ("Attempting to set the gas mask settings to a class that doesn't support gas masks."));
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 Character::GetGasMaskSettingsId() const
{
	ALWAYS_ASSERTF(false, ("Attempting to get the gas mask settings from a class that doesn't support gas masks."));
	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::DebugShowProcess(ScreenSpaceTextPrinter& printer) const
{
	STRIP_IN_FINAL_BUILD;

	ParentClass::DebugShowProcess(printer);

	const Point& pos = GetLocator().Pos();
	float dist = Length(pos - g_mainCameraInfo[0].GetPosition());
	if (dist < 25.0f)
	{
		printer.PrintText(kColorGreen,
						  "%s",
						  GetLookId() != INVALID_STRING_ID_64 ? DevKitOnly_StringIdToString(GetLookId())
															  : "[NO LOOK NAME]");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::PhysFxSet* Character::GetPhysFxSet() const
{
	const DC::PhysFxSet* pPhysFxSet = ParentClass::GetPhysFxSet();
	if(!pPhysFxSet)
	{
		pPhysFxSet = LookupPhysFxSet(SID("character"));
	}
	return pPhysFxSet;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SetupRagdollHelper(const ProcessSpawnInfo& spawn)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::InitGoreFilter()
{
	m_goreFilter = 0;

	const DC::Map* pGoreMeshDependencyTable = ScriptManager::Lookup<DC::Map>(SID("*gore-mesh-dependency-table*"), nullptr);
	if (pGoreMeshDependencyTable)
	{
		if (IDrawControl* pDrawControl = GetDrawControl())
		{
			for (int i = 0; i < pDrawControl->GetNumMeshes(); i++)
			{
				IDrawControl::Mesh& mesh = pDrawControl->GetMesh(i);
				if (const DC::GoreMeshDependency* pGoreMeshDep = ScriptManager::MapLookup<DC::GoreMeshDependency>(pGoreMeshDependencyTable, mesh.m_lod[0].m_meshNameId))
				{
					m_goreFilter |= pGoreMeshDep->m_filter;
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SetInvisibleToAi(bool f)
{
	m_invisibleToAi = f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SetInAiDarknessRegion(bool f)
{
	m_inAiDarknessRegion = f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SetInAiDarkness(bool f)
{
	m_inAiDarkness = f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SetCharacterName(const char* charName, bool err)
{
	if (!err)
	{
		m_characterNameError = nullptr;
		m_characterName = charName;
	}
	else
	{
		m_characterNameError = charName;
		m_characterName = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 Character::CreateOverlayName(const char* group, const char* mode) const
{
	const char* pBaseName = GetOverlayBaseName();
	if (pBaseName)
	{
		StringId64 overlayBase = StringId64Concat(SID("anim-overlay-"), pBaseName);
		overlayBase = StringId64Concat(overlayBase, "-");
		overlayBase = StringId64Concat(overlayBase, group);
		overlayBase = StringId64Concat(overlayBase, "-");
		overlayBase = StringId64Concat(overlayBase, mode);

		return overlayBase;
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::UpdateFeetWet()
{
	const F32 kProbeExtent = 1.5f;
	const F32 kProbeRadius = 0.2f;

	const Vector kExtentVector = Vector(0.0f, kProbeExtent, 0.0f);

	CollideFilter collideFilter = CollideFilter(Collide::kLayerMaskBackground | Collide::kLayerMaskWater);
	collideFilter.SetPatInclude(Pat(Pat::kWaterMask | Pat::kWetSurfaceMask));
	collideFilter.SetPatExclude(Pat(Pat::kPassThroughMask));

	m_feetInWaterGroundProbe.Wait();

	bool feetWet = false;

	if (m_feetInWaterGroundProbe.IsValid())
	{
		Point startPos = m_feetInWaterGroundProbe.GetProbeStart(0) - kExtentVector;

		if (m_feetInWaterGroundProbe.IsContactValid(0, 0))
		{
			const ShapeCastContact contact = m_feetInWaterGroundProbe.GetContact(0, 0);
			bool layerHit = contact.m_hRigidBody.HandleValid() && contact.m_hRigidBody->GetLayer() == Collide::kLayerWater;

			Point pnt = m_feetInWaterGroundProbe.GetContactPoint(0, 0);
			Vector norm = m_feetInWaterGroundProbe.GetContactNormal(0, 0);
			pnt.SetY(pnt.Y() - kProbeRadius / Max(0.0001f, (F32)norm.Y()));
			PHYSICS_ASSERT(IsFinite(pnt));

			feetWet = startPos.Y() - (!layerHit ? .3f : 0.f) < pnt.Y();
		}

		SetFeetWet(feetWet);

		//if (ShouldUpdateSplashers())
		//	SetFeetSplashersInWater(feetWet);

		if (FALSE_IN_FINAL_BUILD(g_gameObjectDrawFilter.m_drawFeetWet && DebugSelection::Get().IsProcessOrNoneSelected(Process::GetContextProcess())))
		{
			const float probeLen = 5.0f;

			const Color clr = AreFeetWet() ? kColorGreen : kColorRed;
			g_prim.Draw(DebugCross(startPos + kExtentVector, 0.25f, clr));
			g_prim.Draw(DebugLine(startPos + kExtentVector, -2.0f * kExtentVector, clr));
			g_prim.Draw(DebugCross(startPos, 0.25f, clr));
		}

		m_feetInWaterGroundProbe.Close();
	}

	// Setup for next Frame
	const FgAnimData* pAnimData = GetAnimData();

	const SplasherSkeletonInfo* pSplasherSkel = GetSplasherSkeleton();
	const bool shouldSetSplashers = !ShouldUpdateSplashers() || !pSplasherSkel || !pSplasherSkel->HasFeetInfo();

	if (!pAnimData)
	{
		return;
	}

	const Point alignPosWs = GetTranslation();
	Point probePosWs = alignPosWs;

	const U32F numJoints = pAnimData->m_jointCache.GetNumTotalJoints();
	if ((m_toeJointIndices[0] >= 0)
		&& (m_toeJointIndices[1] >= 0)
		&& (m_toeJointIndices[0] < numJoints)
		&& (m_toeJointIndices[1] < numJoints))
	{
		const Point leftHeelWs = pAnimData->m_jointCache.GetJointLocatorWs(m_toeJointIndices[0]).Pos();
		const Point rightHeelWs = pAnimData->m_jointCache.GetJointLocatorWs(m_toeJointIndices[1]).Pos();

		probePosWs = Lerp(leftHeelWs, rightHeelWs, 0.5f);

		const float minHeelHeight = Min(leftHeelWs.Y(), rightHeelWs.Y());

		probePosWs.SetY(minHeelHeight);
	}

	m_feetInWaterGroundProbe.Open(1);
	m_feetInWaterGroundProbe.SetFilterForAllProbes(collideFilter);

	m_feetInWaterGroundProbe.SetProbeExtents(0, probePosWs + kExtentVector, probePosWs - kExtentVector);
	m_feetInWaterGroundProbe.SetProbeRadius(0, kProbeRadius);

	m_feetInWaterGroundProbe.Kick(FILE_LINE_FUNC);

}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::UpdateMeleeTime()
{
	if (GetCurrentMeleeAction())
	{
		SetLastMeleeMoveActiveTime();
	}

	if (IsBeingMeleeAttacked())
	{
		SetLastMeleeOpponentTime();
	}

	if (IsBeggingForLife())
	{
		SetLastBegForLifeTime();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::PostAnimUpdate_Async()
{
	ParentClass::PostAnimUpdate_Async();

	UpdateMeleeTime();
	UpdateFeetWet();

	KickSoundImpactCheckJob();

	if (m_pSubsystemMgr)
	{
		m_pSubsystemMgr->PostAnimUpdate();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct SaveAlignData
{
	bool m_top;
	BoundFrame m_align;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool WalkSaveAlign(AnimStateInstance* pInstance, AnimStateLayer* pStateLayer, uintptr_t userData)
{
	SaveAlignData& data = *(SaveAlignData*)userData;

	if (!data.m_top && (pInstance->GetStateFlags() & DC::kAnimStateFlagSaveTopAlign) && !pInstance->HasSavedAlign())
	{
		BoundFrame align = data.m_align;
		//if we have an apRef don't trash it (set saved align overwrites the apRef)
		if (!pInstance->IsApLocatorActive())
		{
			pInstance->SetSavedAlign(align);
		}
	}

	data.m_top = false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::PostRootLocatorUpdate()
{
	if (m_pSubsystemMgr)
	{
		m_pSubsystemMgr->PostRootLocatorUpdate();
	}

	AnimControl* pAnimControl = GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	if (pBaseLayer)
	{
		SaveAlignData data;
		data.m_top = true;
		data.m_align = GetBoundFrame();
		pBaseLayer->WalkInstancesNewToOld(WalkSaveAlign, (uintptr_t)&data);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct UpdateAlignData
{
	bool m_top;
	Locator m_delta;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool WalkUpdateSaveAlign(AnimStateInstance* pInstance, AnimStateLayer* pStateLayer, uintptr_t userData)
{
	UpdateAlignData& data = *(UpdateAlignData*)userData;

	if (!data.m_top && (pInstance->GetStateFlags() & DC::kAnimStateFlagSaveTopAlign) && pInstance->HasSavedAlign())
	{
		//if we have an apRef don't trash it (set saved align overwrites the apRef)
		if (!pInstance->IsApLocatorActive())
		{
			pInstance->UpdateSavedAlign(data.m_delta);
		}
	}

	data.m_top = false;

	return true;
}

void Character::PostAlignMovement(const Locator& delta)
{
	if (IsIdentity(delta.GetRotation()) && AllComponentsEqual(delta.GetTranslation(), kZero))
		return;

	AnimControl* pAnimControl = GetAnimControl();
	AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	if (pBaseLayer)
	{
		UpdateAlignData data;
		data.m_top = true;
		data.m_delta = delta;
		pBaseLayer->WalkInstancesNewToOld(WalkUpdateSaveAlign, (uintptr_t)&data);
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void Character::PostAnimBlending_Async()
{
	ParentClass::PostAnimBlending_Async();

	if (m_pSubsystemMgr)
	{
		m_pSubsystemMgr->PostAnimBlending();
	}

	// Since some subsystems do IK and we usually want ragdoll to go after IK this should go after subsystems update
	if (m_pRagdoll && !m_clientControlsRagdollPostAnimBlending)
	{
		m_pRagdoll->PostAnimBlending();
	}

	m_disableThrowableSelfDamage = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::RagdollPostAnimBlending()
{
	PHYSICS_ASSERT(m_clientControlsRagdollPostAnimBlending);
	if (m_pRagdoll)
	{
		m_pRagdoll->PostAnimBlending();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::PostJointUpdate_Async()
{
	m_cachedTargChestWs = GetAttachSystem()->GetAttachPositionById(SID("targChest"));
	m_isInProneRegion = Max(0, m_isInProneRegion - 1);
	if (m_surfaceTypeProbeCounter)
	{
		ndjob::WaitForCounterAndFree(m_surfaceTypeProbeCounter);
		m_surfaceTypeProbeCounter = nullptr;
	}

	ParentClass::PostJointUpdate_Async();

	if (m_pRagdoll)
	{
		m_pRagdoll->PostJointUpdate();
		UpdateBuoyancy();
	}
	m_pMeleeImpulse->PostJointUpdate(this);

	UpdateRope();

	if (m_pSubsystemMgr)
	{
		m_pSubsystemMgr->PostJointUpdate();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::EventHandler(Event& event)
{
	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("is-dead"):
		{
			const bool isDead = IsDead();
			event.SetResponse(isDead);
			break;
		}
#if !FINAL_BUILD
	case SID_VAL("collision-reload"):
		{
			const ArtItemCollision* pArtItem = event.Get(0).GetConstPtr<ArtItemCollision*>();
			if (m_pCompositeBody && m_pCompositeBody->GetArtItem() == pArtItem)
			{
				m_pCompositeBody->DestroyCompositeBody();
				event.SetResponse(false);
			}
			if (m_pRagdoll && m_pRagdoll->GetArtItem() == pArtItem)
			{
				ReloadRagdollPrepare();
				event.SetResponse(false);
			}
			return;
		}
	case SID_VAL("post-collision-reload"):
		{
			const ArtItemCollision* pArtItem = event.Get(0).GetConstPtr<ArtItemCollision*>();
			if (m_pCompositeBody && m_pCompositeBody->GetArtItem() && m_pCompositeBody->GetArtItem()->GetNameId() == pArtItem->GetNameId())
			{
				ReloadCollision();
			}
			if (m_ragdollReloadData.m_artItemId == pArtItem->GetNameId())
			{
				ReloadRagdoll();
			}
			return;
		}
#endif
	case SID_VAL("collision-logout-query"):
		{
			const ArtItemCollision* pArtItem = event.Get(0).GetConstPtr<ArtItemCollision*>();
			if (m_pRagdoll && m_pRagdoll->IsUsingCollision(pArtItem))
			{
				event.SetResponse(true);
				return;
			}
			break;
		}
	case SID_VAL("collision-logout"):
		{
			const ArtItemCollision* pArtItem = event.Get(0).GetConstPtr<ArtItemCollision*>();
			if (m_pRagdoll && m_pRagdoll->IsUsingCollision(pArtItem))
			{
				MsgErr("Logging out collision %s while it's still in use by ragdoll %s!\n", pArtItem->GetName(), DevKitOnly_StringIdToString(GetUserId()));
				HandleDataLoggedOutError();
				m_pRagdoll->DestroyCompositeBody();
			}
			break;
		}
	case SID_VAL("enable-body-impact-surface-ray"):
		{
			m_lastBodyImpactRayTime = GetCurTime();
			float rayLength = event.Get(0).GetFloat();
			if (rayLength > 0.f)
				m_bodyImpactRayLength = rayLength;
			if (event.GetNumParams() > 1)
			{
				m_bodyImpactRayRadius = event.Get(1).GetFloat();
				m_bodyImpactRayRadius = Min(m_bodyImpactRayRadius, 0.2f);
			}
		}
		break;
	case SID_VAL("enable-shoulder-impact-surface-ray"):
		{
			m_lastShoulderImpactRayTime = GetCurTime();
			float rayLength = event.Get(0).GetFloat();
			if (rayLength > 0.f)
				m_shoulderImpactRayLength = rayLength;
			if (event.GetNumParams() > 1)
			{
				m_shoulderImpactRayRadius = event.Get(1).GetFloat();
				m_shoulderImpactRayRadius = Min(m_shoulderImpactRayRadius, 0.2f);
			}
		}
		break;
	case SID_VAL("power-ragdoll-disable"):
		{
			SetPowerRagdollDisabled(true, 0.2f, event.Get(0).GetFloat());
			break;
		}

	case SID_VAL("debug-reinit-armor"):
		{
			Armor* pArmor = GetCharacterArmor();
			if (pArmor)
				pArmor->DebugReinit(GetCharacterCollision());
			break;
		}

	case SID_VAL("attach-gas-mask"):
		{
			OnAttachGasMask(this);
			return;
		}
		break;

	case SID_VAL("detach-gas-mask"):
		{
			OnDetachGasMask(this);
			return;
		}
		break;

	case SID_VAL("get-rope-channel-provider"):
		{
			event.SetResponse(BoxedValue((IAnimCustomChannelProvider*)m_hRope.ToProcess()));
			return;
		}

	case SID_VAL("sound-gesture"):
		{
			if (m_pSoundGestureList)
			{
				const I32 iGesture = event.Get(0).GetAsI32(-1);
				if (iGesture < 0)
				{
					MsgErr("sound-gesture event on %s failed to set index!\n", DevKitOnly_StringIdToString(GetUserId()));
					return;
				}

				if (iGesture >= m_pSoundGestureList->m_count)
				{
					MsgErr("sound-gesture event on %s requested index %d. %s has size %d.\n",
						DevKitOnly_StringIdToString(GetUserId()),
						iGesture,
						DevKitOnly_StringIdToString(m_pSoundGestureList.GetId()),
						m_pSoundGestureList->m_count);
					return;
				}

				if (IGestureController* pGestureController = GetGestureController())
				{
					Gesture::Err result = pGestureController->Play(m_pSoundGestureList->m_array[iGesture]);
					if (FALSE_IN_FINAL_BUILD(!result.Success()))
					{
						MsgErr("sound-gesture failed to play %s : %s\n",
								  DevKitOnly_StringIdToString(m_pSoundGestureList->m_array[iGesture]),
								  DevKitOnly_StringIdToString(result.m_errId));
					}
				}
			}
			return;
		}

	case SID_VAL("breath-start"):
		{
			if (GetVoxControllerConst().GetCurrentVoxPriority() > DC::kVoxPriorityBreathHigh)
				return;

			const I32 context = event.Get(0).GetAsI32(-1);
			const I32 durationMillis = event.Get(1).GetAsI32(-1); // Scream can only pass ints
			if (durationMillis < 0)
			{
				MsgErr("breath-start event on %s failed to set duration-millis!\n",
					DevKitOnly_StringIdToString(GetUserId()));
				return;
			}

			if (durationMillis < 100 || durationMillis > 100000)
			{
				MsgAnimErr("Invalid duration millis for breath-start event: %d", durationMillis);
				return;
			}

			const F32 durationSec = ((float)durationMillis) / 1000.0f;


			HandleBreathStartEvent(durationSec, context);
			break;
		}
	case SID_VAL("disable-camera-fade-out-this-frame"):
		{
			m_disableCameraFadeOutUntilFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused + 2;
		}
	}


	ParentClass::EventHandler(event);
}

void Character::HandleBreathStartEvent(float duration, int context)
{
	const I64 curFrameNumber = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	if (m_lastBreathEventFrame >= curFrameNumber)
	{
		if (!EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPausedA11y())
			MsgAiErr("Multiple Breath Start Events on the same frame! Was there a massive frame hitch?\n");
		return;
	}

	if (m_pHeartRateMonitor)
	{
		m_pHeartRateMonitor->HandleBreathStartEvent(this, duration, context);
		SendEvent(SID("splasher-event"), this, SID("breath-out"), m_pHeartRateMonitor->GetCurrentHeartRate(), duration);
	}

	m_lastBreathEventFrame = curFrameNumber;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::OnError()
{
	CharacterManager::GetManager().Unregister(this);

	ParentClass::OnError();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::OnKillProcess()
{
	CharacterManager::GetManager().Unregister(this);

	if (m_pRagdoll)
	{
		m_pRagdoll->SetMotionType(kRigidBodyMotionTypeNonPhysical);
	}

	// release fx stuff
	g_goreMgr.ReleaseGoreMaskSlot(this);
	g_fxMgr.ReleaseWetMaskSlot(this);

	KillRope();

	if (NdAttachableObject* pVisor = GetRiotVisor())
	{
		KillProcess(pVisor);
	}

	ParentClass::OnKillProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::OnPlatformMoved(const Locator& deltaLoc)
{
	PROFILE(Processes, Char_OnPlatformMoved);

	ParentClass::OnPlatformMoved(deltaLoc);

	if (m_pCompositeBody)
	{
		// This is here because player probes cast against other character collision and if the platform moves fast
		// the characters will be one frame behind.

		U32F bodyCount = m_pCompositeBody->GetNumBodies();
		for (U32F iBody = 0; iBody < bodyCount; ++iBody)
		{
			RigidBody* pBody = m_pCompositeBody->GetBody(iBody);
			if (pBody->GetGameLinkage() == RigidBody::kLinkedToObject)
			{
				Locator loc = pBody->GetLocator();
				Locator newLoc = deltaLoc.TransformLocator(loc);
				pBody->PredictLocator(newLoc);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 Character::GetLegIkConstantsSettingsId() const
{
	return SID("*default-character-leg-ik-constants*");
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::LegIkConstants* Character::GetLegIKConstants() const
{
	if (FALSE_IN_FINAL_BUILD(g_useDebugLegIKConstants))
		return g_pDebugLegIKConstants;

	return m_pLegIkConstants;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::GetLegRayCastInfo(U32 currentMode, LegRayCastInfo& info) const
{
	switch (currentMode)
	{
	case LegRaycaster::kModeEdge:
		info.m_dir = GetLocalZ(GetRotation());
		info.m_length = 2.2f;
		break;
	case LegRaycaster::kModeWallRope:
	default:
		info.m_length = 2.0f;
		info.m_projectToAlign = true;
		break;
	}

	const LegRaycaster* const pLegRaycaster = GetLegRaycaster();
	const bool predictOnStairs = pLegRaycaster
								 && pLegRaycaster->GetPredictOnStairs().m_result == LegRaycaster::PredictOnStairs::Result::kHitStairs;

	if (OnStairs() || predictOnStairs)
	{
		info.m_probeStartAdjust = -0.75f;
		info.m_useLengthForCollisionCast = true;
		info.m_length = 2.0f;
		//info.m_projectToAlign = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 Character::GetDesiredLegRayCastMode() const
{
	return LegRaycaster::kModeDefault;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::Die()
{
	if (m_pHealthSystem)
	{
		m_pHealthSystem->SetHealth(0);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::GetReceiverDamageInfo(ReceiverDamageInfo* pInfo) const
{
	m_pAttackHandler->GetReceiverDamageInfo(*this, pInfo);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Process* Character::GetDrivenVehicleProcess() const
{
	Event queryEvt(SID("drive-event"), MutableProcessHandle(), SID("is-driver?"));
	// const_cast alert!
	const_cast<Character*>(this)->HandleDriveEvent(queryEvt);
	const BoxedValue& result = queryEvt.GetResponse();

	if (result.IsValid() && result.GetAsBool())
	{
		return GetEnteredVehicle().ToMutableProcess();
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Process* Character::GetLastDrivenVehicleProcess() const
{
	Event queryEvt(SID("drive-event"), MutableProcessHandle(), SID("get-last-driven-vehicle"));
	// const_cast alert!
	const_cast<Character*>(this)->HandleDriveEvent(queryEvt);
	const BoxedValue& result = queryEvt.GetResponse();

	if (result.IsValid())
	{
		return result.GetMutableProcess();
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject* Character::GetRailVehicleObject() const
{
	NdGameObject* pParent = GetBoundGameObject();
	while (pParent && !pParent->IsType(SID("RailVehicle")))
	{
		pParent = pParent->GetBoundGameObject();
	}
	if (pParent && pParent->IsType(SID("RailVehicle")))
	{
		return pParent;
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemAnimController* Character::GetActiveSubsystemController(StringId64 type)
{
	return GetSubsystemMgr() ? GetSubsystemMgr()->GetActiveSubsystemController(SID("base"), type) : nullptr;
}

const NdSubsystemAnimController* Character::GetActiveSubsystemController(StringId64 type) const
{
	return GetSubsystemMgr() ? GetSubsystemMgr()->GetActiveSubsystemController(SID("base"), type) : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err Character::InitializeRagdoll(CompositeBodyInitInfo* pInitInfo)
{
	if (!HavokIsEnabled())
	{
		return Err::kErrPhysicsInit;	// pretend like we don't have any collision data when Havok isn't up (e.g. in the menus)
	}

	ASSERT(!m_pRagdoll && "InitializeRagdoll() called more than once!");
	if (!m_pRagdoll)
	{
		m_pRagdoll = NDI_NEW CompositeBody;

		CompositeBodyInitInfo info;
		if (pInitInfo)
		{
			info = *pInitInfo;
		}

		info.m_initialMotionType = kRigidBodyMotionTypeNonPhysical;
		info.m_bCharacter = true;
		info.m_bUseRagdoll = true;
		info.m_bLookForCollisionInSkeleton = true;
		info.m_pCharColl = nullptr;
		info.m_numBodies = 0;
		info.m_pPhysFxSet = nullptr; // ragdoll controller takes care of this

		if (!info.m_pOwner)
		{
			info.m_pOwner = this;
		}

		Err result = m_pRagdoll->Init(info);

		if (result.Failed())
		{
			m_pRagdoll->DestroyCompositeBody();
			m_pRagdoll = nullptr;
			return result;
		}

		m_pRagdoll->SetLockPoseTranslations(true);
		m_pRagdoll->SetAnimBlend(1.0f);

		// T2 ship it hack for ragdoll blood splats
		m_pRagdoll->SetPhysFxParticlesDisabled(true);

		m_ragdollNoGroundCounter = 0;
		m_ragdollDisabledTimer = 0.0f;

		if (ShouldCreateRagdollBuoyancy())
		{
			bool dynamicWater = BuoyancyUseDynamicWater();
			m_buoyancyList.Init(m_pRagdoll->GetNumBodies(), FILE_LINE_FUNC);
			for (U32F iBody = 0; iBody < m_pRagdoll->GetNumBodies(); ++iBody)
			{
				RigidBody* pBody = m_pRagdoll->GetBody(iBody);
				BuoyancyAccumulator* pBuoyancy = NDI_NEW BuoyancyAccumulator();
				pBuoyancy->Init(pBody, SID("buoyancy-object-ragdoll"), dynamicWater);
				m_buoyancyList.PushBack(pBuoyancy);
			}
		}
	}
	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::CopyRagdollBuoyancy(const Character* pSource)
{
	if (ShouldCreateRagdollBuoyancy() && pSource->m_buoyancyList.Size() > 0)
	{
		m_buoyancyList.Init(pSource->m_buoyancyList.Size(), FILE_LINE_FUNC);
		PHYSICS_ASSERT(pSource->m_buoyancyList.Size() == m_pRagdoll->GetNumBodies());
		for (U32F ii = 0; ii<pSource->m_buoyancyList.Size(); ii++)
		{
			BuoyancyAccumulator* pBuoyancy = NDI_NEW BuoyancyAccumulator();
			pBuoyancy->TakeOver(pSource->m_buoyancyList[ii], m_pRagdoll->GetBody(ii));
			m_buoyancyList.PushBack(pBuoyancy);
		}
		m_buoyancyEnabled = pSource->m_buoyancyEnabled;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::ReloadRagdollPrepare()
{
	STRIP_IN_FINAL_BUILD;

#if !FINAL_BUILD
	if (!HavokIsEnabled())
	{
		return;	// pretend like we don't have any collision data when Havok isn't up (e.g. in the menus)
	}

	if (!m_pRagdoll)
	{
		return;
	}

	if (!m_pRagdoll->GetArtItem())
	{
		return;
	}

	m_ragdollReloadData.m_artItemId = m_pRagdoll->GetArtItem()->GetNameId();

	CompositeRagdollController* pRagdollCtrl = m_pRagdoll->GetRagdollController();
	PHYSICS_ASSERT(pRagdollCtrl);
	m_ragdollReloadData.m_isPhysical = IsRagdollPhysicalized();
	m_ragdollReloadData.m_isPowered = pRagdollCtrl->GetPowered();
	m_ragdollReloadData.m_isDying = pRagdollCtrl->GetPowerOffCheck();
	m_ragdollReloadData.m_settingsId = pRagdollCtrl->GetControlSettings();

	m_pRagdoll->DestroyCompositeBody();
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::ReloadCollision()
{
	STRIP_IN_FINAL_BUILD;

	if (!HavokIsEnabled())
	{
		return;	// pretend like we don't have any collision data when Havok isn't up (e.g. in the menus)
	}

	if (!m_pCompositeBody)
	{
		return;
	}

	CompositeBody* pOldCompo = m_pCompositeBody;
	m_pCompositeBody = nullptr;

	AllocateJanitor jjj(kAllocDebug,FILE_LINE_FUNC);

	Err err = InitCompositeBody();
	if (err != Err::kOK)
	{
		MsgHavokErr("Error reloading character collision");
		m_pCompositeBody = pOldCompo;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::ReloadRagdoll()
{
	STRIP_IN_FINAL_BUILD;

	if (!HavokIsEnabled())
	{
		return;	// pretend like we don't have any collision data when Havok isn't up (e.g. in the menus)
	}

	if (!m_pRagdoll)
	{
		return;
	}

	AllocateJanitor jjj(kAllocDebug, FILE_LINE_FUNC);

	CompositeBody* pNewRagdoll = NDI_NEW CompositeBody;
	if (!pNewRagdoll)
	{
		MsgHavokErr("Out of debug memory. Can't reload ragdoll");
		return;
	}

#if !FINAL_BUILD
	CompositeBodyInitInfo info;
	info.m_bCharacter = true;
	info.m_bUseRagdoll = true;
	info.m_pCharColl = nullptr;
	info.m_numBodies = 0;
	info.m_pOwner = this;
	info.m_pCopyFromCompo = m_pRagdoll;
	info.m_bInitFromBindPose = false;

	Err result = pNewRagdoll->Init(info);
	if (result.Failed())
	{
		MsgHavokErr("Error reloading ragdoll");
		return;
	}

	m_pRagdoll = pNewRagdoll;

	if (m_ragdollReloadData.m_isPhysical)
	{
		PhysicalizeRagdoll(m_ragdollReloadData.m_isDying, m_ragdollReloadData.m_settingsId, 0.0f);
		if (!m_ragdollReloadData.m_isPowered)
		{
			FadeOutRagdollPower(0.0f);
		}
	}

	m_ragdollReloadData.m_artItemId = INVALID_STRING_ID_64;
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::PhysicalizeRagdoll(bool bDying, StringId64 settingsId, F32 blendInTime, bool eff)
{
	if (!HavokIsEnabled())
		return false;

	if (g_disableRagdolls)
		return false;

	if (!bDying && m_ragdollDisabledTimer > 0.0f)
		return false;

	if (m_ragdollBlendOutPendingSettings != INVALID_STRING_ID_64 && m_ragdollBlendOutPendingSettings == settingsId)
	{
		m_ragdollTimer = -1.0f;
		return true;
	}
	m_ragdollBlendOutPendingSettings = INVALID_STRING_ID_64;

	m_ragdollBlendOutTimer = -1.0f;

	if (m_pRagdoll)
	{
		CompositeRagdollController* pRagdollCtrl = m_pRagdoll->GetRagdollController();
		PHYSICS_ASSERT(pRagdollCtrl);

		m_ragdollNonEffControlled = !eff;

		if (!IsRagdollPhysicalized() || bDying != pRagdollCtrl->IsDying() || settingsId != pRagdollCtrl->GetControlSettings())
		{
			if (pRagdollCtrl->IsDying())
			{
				// Is already dying, ignore power ragdoll requests
				return false;
			}
			if (settingsId != INVALID_STRING_ID_64 && !ScriptManager::LookupInModule<DC::RagdollControlSettings>(settingsId, SID("ragdoll-settings"), nullptr))
			{
				MsgConScriptError("Invalid ragdoll settings id %s\n", DevKitOnly_StringIdToString(settingsId));
				return false;
			}

			if (settingsId == INVALID_STRING_ID_64 && bDying)
			{
				settingsId = GetRagdollDyingSettings();
			}

			if (blendInTime > 0.0f && pRagdollCtrl->CheckSettingsChangeForBlendOut(settingsId))
			{
				BlendOutRagdoll(blendInTime, eff);
				m_ragdollBlendOutPendingSettings = settingsId;
				m_ragdollTimer = -1.0f;
				return true;
			}

			pRagdollCtrl->SetControlSettings(settingsId, blendInTime);

			m_pRagdoll->SetMotionType(kRigidBodyMotionTypePhysicsDriven);

			pRagdollCtrl->SetDying(bDying);
			if (bDying)
			{
				m_pRagdoll->SetPredictParentMovement(true);
				if (IDrawControl* pDrawCtrl = GetDrawControl())
				{
					pDrawCtrl->SetIsRagdoll(true);
				}
			}

			if (CompositeBody* pCompositeBody = GetCompositeBody())
			{
				// Only do this if the character is dead - we don't want his rigid bodies disappearing if
				// he's going to powered ragdoll!
				if (IsDead() || bDying)
				{
					if (!g_characterDontDestroyRigidBodiesOnRagdoll)
						pCompositeBody->SetMotionType(kRigidBodyMotionTypeNonPhysical);
				}
				else
				{
					// If we have ragdoll we don't want the character collision to kick things around
					pCompositeBody->SetCharacterCollisionDisabled(pRagdollCtrl->GetDisableCharacterCapsulesCollision());
				}
			}
		}

		m_pRagdoll->BlendToPhysics(blendInTime);

		m_ragdollTimer = -1.0f;
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::PhysicalizeRagdollWithTimeOut(F32 timeOut, StringId64 settingsId, F32 blendInTime)
{
	if (PhysicalizeRagdoll(false, settingsId, blendInTime, false))
	{
		m_ragdollTimer = timeOut;
		return true;
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::UnphysicalizeRagdoll(bool eff, bool overrideDeathCheck)
{
	if (m_pRagdoll)
	{
		CompositeRagdollController* pRagdollCtrl = m_pRagdoll->GetRagdollController();
		PHYSICS_ASSERT(pRagdollCtrl);
		if (pRagdollCtrl->IsDying() && !overrideDeathCheck)
		{
			// Is already dying, ignore all requests
			return;
		}

		m_pRagdoll->SetMotionType(kRigidBodyMotionTypeNonPhysical);
		m_pRagdoll->SetAnimBlend(1.0f);
		m_pRagdoll->SetAutoUpdateParentBody(true);
		m_pRagdoll->SetPredictParentMovement(true);
		m_pRagdoll->GetRagdollController()->PowerOn();
		m_ragdollNoGroundCounter = 0;
		m_ragdollBlendOutPendingSettings = INVALID_STRING_ID_64;

		if (CompositeBody* pCompositeBody = GetCompositeBody())
		{
			// Turn the character collision back on
			pCompositeBody->SetCharacterCollisionDisabled(false);
		}

		if (m_ragdollNonEffControlled && !eff)
			m_ragdollNonEffControlled = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::IsRagdollPhysicalized() const
{
	return m_pRagdoll && m_pRagdoll->GetMotionType() == kRigidBodyMotionTypePhysicsDriven;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::BlendOutRagdoll(float blendOutTime, bool eff, bool overrideDeathCheck)
{
	if (m_pRagdoll)
	{
		CompositeRagdollController* pRagdollCtrl = m_pRagdoll->GetRagdollController();
		PHYSICS_ASSERT(pRagdollCtrl);
		if (pRagdollCtrl->IsDying() && !overrideDeathCheck)
		{
			// Is already dying, ignore all requests
			return;
		}

		m_ragdollBlendOutPendingSettings = INVALID_STRING_ID_64;

		if (blendOutTime <= 0.0f)
		{
			UnphysicalizeRagdoll(eff);
		}
		else if (m_ragdollBlendOutTimer <= 0.0f || blendOutTime < m_ragdollBlendOutTimer)
		{
			m_pRagdoll->BlendToAnimation(blendOutTime, false);

			if (m_ragdollNonEffControlled && eff)
				m_ragdollNonEffControlled = false;

			m_ragdollBlendOutTimer = blendOutTime;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::FadeOutRagdollPower(float fadeOutTime, const char* pDebugReason, bool limitVelocities)
{
	if (IsRagdollPhysicalized())
	{
		m_pRagdoll->GetRagdollController()->PowerOff(pDebugReason ? pDebugReason : "Game code", fadeOutTime, limitVelocities);
		m_ragdollBlendOutTimer = -1.0f;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::IsRagdollPowerFadingOut() const
{
	if (IsRagdollPhysicalized())
	{
		return !m_pRagdoll->GetRagdollController()->GetPowered() || m_pRagdoll->GetRagdollController()->GetPowerFadeoutAge() > 0.0f;
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::IsRagdollPowerFadedOut() const
{
	if (IsRagdollPhysicalized())
	{
		return !m_pRagdoll->GetRagdollController()->GetPowered();
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool CheckAnimNodeForPowerRagdollEffs(const AnimStateSnapshot* pSnapshot,
											 const AnimSnapshotNode* pNode,
											 const AnimSnapshotNodeBlend* pParentBlendNode,
											 float combinedBlend,
											 uintptr_t userData)
{
	bool keepGoing = true;
	const AnimSnapshotNodeAnimation* pAnimNode = static_cast<const AnimSnapshotNodeAnimation*>(pNode);
	const ArtItemAnim* pAnim = pAnimNode ? pAnimNode->m_artItemAnimHandle.ToArtItem() : nullptr;

	if (pAnim && pAnim->m_pEffectAnim)
	{
		for (U32F ii = 0; ii < pAnim->m_pEffectAnim->m_numEffects; ii++)
		{
			const EffectAnimEntry& eff = pAnim->m_pEffectAnim->m_pEffects[ii];
			if (eff.GetNameId() == SID("power-ragdoll-on") || eff.GetNameId() == SID("power-ragdoll-off"))
			{
				*(bool*)userData = true;
				keepGoing = false;
				break;
			}
		}
	}

	return keepGoing;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::HasAnimInstanceRagdollEffs(const AnimStateInstance* pInst)
{
	bool hasRagdollEffs = false;
	pInst->GetRootSnapshotNode()->VisitNodesOfKind(&pInst->GetAnimStateSnapshot(),
												   SID("AnimSnapshotNodeAnimation"),
												   CheckAnimNodeForPowerRagdollEffs,
												   (uintptr_t)&hasRagdollEffs);
	return hasRagdollEffs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::UpdateRagdoll()
{
	PROFILE(Animation, Character_UpdateRagdoll);

	if (m_ragdollDisabledTimer > 0.0f)
	{
		m_ragdollDisabledTimer = Max(0.0f, m_ragdollDisabledTimer - GetProcessDeltaTime());
	}

	if (IsRagdollPhysicalized())
	{
#if !FINAL_BUILD
		if (g_disableRagdolls)
		{
			UnphysicalizeRagdoll();
			return;
		}
#endif
		if (m_ragdollBlendOutTimer >= 0.0f)
		{
			m_ragdollBlendOutTimer = Max(0.0001f, m_ragdollBlendOutTimer-GetProcessDeltaTime()); // keep it positive because we only check GetAnimFullyBlendedIn as blend out done test
			if (m_pRagdoll->GetAnimFullyBlendedIn())
			{
				if (m_ragdollBlendOutPendingSettings != INVALID_STRING_ID_64)
				{
					m_pRagdoll->SetMotionType(kRigidBodyMotionTypeNonPhysical);
					m_ragdollPendingSettingsDelay = 0.4f;
				}
				else
				{
					UnphysicalizeRagdoll();
				}
			}
		}
		else if (!m_ragdollNonEffControlled)
		{
			// As a safeguard if we have power ragdoll on and controlled only by EFFs we check if the current anim has any power ragdoll EFFs and if not
			// we will blend the ragdoll out
			AnimControl* pAnimControl = GetAnimControl();
			const AnimStateLayer* pLayer = pAnimControl->GetStateLayerById(SID("base"));
			bool hasRagdollEffs = false;
			if (pLayer)
			{
				if (const AnimStateInstance* pInst = pLayer->CurrentStateInstance())
				{
					hasRagdollEffs = HasAnimInstanceRagdollEffs(pInst);
				}
			}
			if (!hasRagdollEffs)
				BlendOutRagdoll();
		}
		else
		{
			if (m_ragdollTimer >= 0.0f)
			{
				m_ragdollTimer -= GetProcessDeltaTime();
				if (m_ragdollTimer < 0.0f)
					BlendOutRagdoll();
			}
			// Turn off motors on ragdoll if the dead anim has ended
			CompositeRagdollController* pRagdollCtrl = m_pRagdoll->GetRagdollController();
			if (pRagdollCtrl->IsDying())
			{
				if (!IsRagdollPowerFadingOut() && HasDeathAnimEnded())
				{
					m_pRagdoll->SetLocalTargetPose(true); // no anim left, just use local target pose
					pRagdollCtrl->PowerOff("Animation end", g_ragdollMotorSettings.m_maxForceEndAnimLerpTime, false);
				}
			}
		}
	}
	else if (m_ragdollBlendOutPendingSettings != INVALID_STRING_ID_64)
	{
		m_ragdollPendingSettingsDelay -= GetProcessDeltaTime();
		if (m_ragdollPendingSettingsDelay <= 0.0f)
		{
			StringId64 settingsId = m_ragdollBlendOutPendingSettings;
			m_ragdollBlendOutPendingSettings = INVALID_STRING_ID_64;
			PhysicalizeRagdoll(false, settingsId, 0.2f, !m_ragdollNonEffControlled);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SetPowerRagdollDisabled(bool bDisabled, F32 blendOutTime, F32 timer)
{
	if (bDisabled)
	{
		if (IsRagdollPhysicalized() && !m_pRagdoll->GetRagdollController()->IsDying())
		{
			BlendOutRagdoll(blendOutTime);
		}

		m_ragdollDisabledTimer = timer;
	}
	else
	{
		m_ragdollDisabledTimer = 0.0f;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::DebugDrawCompositeBody() const
{
	ParentClass::DebugDrawCompositeBody();
	if (m_pRagdoll)
		m_pRagdoll->DebugDraw();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::HasDeathAnimEnded() const
{
	if (IsRagdollPowerFadedOut())
	{
		return true;
	}

	const AnimControl* pAnimControl = GetAnimControl();
	const AnimStateLayer* pLayer = pAnimControl->GetStateLayerById(SID("base"));
	if (pLayer && (pLayer->GetNumTotalInstances() == 1))
	{
		if (const AnimStateInstance* pInst = pLayer->CurrentStateInstance())
		{
			const F32 phase = pInst->Phase();
			if (phase >= 1.0f)
			{
				return true;
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::CanRagdoll() const
{
	return m_pRagdoll && m_allowRagdoll;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::IsRagdollBuoyant() const
{
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::IsRagdollAsleep() const
{
	return !m_pRagdoll || m_pRagdoll->IsSleeping();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::IsRagdollDying() const
{
	if (m_pRagdoll)
	{
		CompositeRagdollController* pRagdollCtrl = m_pRagdoll->GetRagdollController();
		PHYSICS_ASSERT(pRagdollCtrl);
		return pRagdollCtrl->IsDying();
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SleepRagdoll()
{
	if (m_pRagdoll)
		m_pRagdoll->GetRagdollController()->PutToSleep();
}

/// --------------------------------------------------------------------------------------------------------------- ///
CompositeRagdollController* Character::GetRagdollController() const
{
	return m_pRagdoll ? m_pRagdoll->GetRagdollController() : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::UpdateRagdollGroundProbes(Point& groundPosPs, Vector& groundNormPs, Pat& pats, F32& speedY, const RagdollGroundProbesConfig& config)
{
	// This is a total heuristics trying to find the correct height to which we should align the character
	// during death anim.
	// We're trying to make it work for death animation where character falls to the ground and still plays
	// animation on the ground

	if (!m_ragdollGroundProbesEnabled)
		return;

	pats.m_bits = 0;

	if (m_ragdollGroundProbes.IsValid())
	{
		m_ragdollGroundProbes.Wait();
		Point lowestP = Point(0.0f, FLT_MAX, 0.0f);
		Vector norm(kZero);
		bool falling = false;

		for (U32F ii = 0; ii<m_ragdollGroundProbes.NumProbes(); ii++)
		{
			if (m_ragdollGroundProbes.IsContactValid(ii, 0))
			{
				Point p = m_ragdollGroundProbes.GetContactPoint(ii, 0);
				if (p.Y() < lowestP.Y())
				{
					lowestP = p;
					norm = m_ragdollGroundProbes.GetContactNormal(ii, 0);
					pats = m_ragdollGroundProbes.GetContactPat(ii, 0);
				}
			}
			else
			{
				falling = true;
			}
		}

		if (FALSE_IN_FINAL_BUILD(g_debugDrawRagdollGroundProbes))
		{
			m_ragdollGroundProbes.DebugDraw(ICollCastJob::DrawConfig(kPrimDuration1FramePauseable));
			if (!falling)
				DebugDrawSphere(lowestP, 0.03f, kColorRed, kPrimDuration1FramePauseable);
		}

		const Locator parentSpace = GetParentSpace();
		if (falling)
		{
			if (++m_ragdollNoGroundCounter >= 1)
			{
				if (!IsRagdollPowerFadingOut() && !IsRagdollPowerFadedOut())
				{
					FadeOutRagdollPower(-1.0f, "No Ground");
				}
			}
			groundPosPs = parentSpace.UntransformPoint(m_ragdollGroundProbes.GetProbeEnd(0));
			groundNormPs = Vector(kUnitYAxis);
		}
		else
		{
			if (pats.m_bits == 0)
			{
				pats.m_bits = 0x100;
			}
			m_ragdollNoGroundCounter = 0;
			groundPosPs = parentSpace.UntransformPoint(lowestP);
			groundNormPs = parentSpace.UntransformVector(norm);
		}

		m_ragdollGroundProbes.Close();
	}

	Point alignPos = GetLocator().GetPosition();

	const U32 kNumJoints = 6;
	const StringId64 jointSid[kNumJoints] = {
		SID("pelvis"),
		SID("l_ankle"),
		SID("r_ankle"),
		SID("spined"),
		SID("l_wrist"),
		SID("r_wrist")
	};
	I32F jointIndex[kNumJoints];
	Point jointPos[kNumJoints];

	memset(jointIndex, -1, sizeof(I32F)*kNumJoints);

	ASSERT(m_pRagdoll);

	{
		HavokMarkForReadJanitor hkjj;

		for (U32F ii = 0; ii < m_pRagdoll->GetNumBodies(); ii++)
		{
			RigidBody* pBody = m_pRagdoll->GetBody(ii);
			if (pBody->IsAddedToWorld())
			{
				StringId64 bodyJoint = pBody->GetJointSid();
				for (U32F jj = 0; jj < kNumJoints; jj++)
				{
					if (jointSid[jj] == bodyJoint)
					{
						jointIndex[jj] = ii;
						jointPos[jj] = pBody->GetHavokLocator().GetPosition();
						break;
					}
				}
			}
		}

		// Align adjustment speed is derived from pelvis vertical speed
		Vector linVec, angVec;
		m_pRagdoll->GetBody(0)->GetHavokVelocity(linVec, angVec);
		speedY = Max(-(F32)linVec.Y(), 0.0f);
		speedY += 10.0f * GetProcessDeltaTime(); // plus gravitational acceleration
	}

	// Adjust for platform velocity because we're frame off
	if (const RigidBody* pPlatformBody = (RigidBody*)GetPlatformBoundRigidBody())
	{
		Vector ds = pPlatformBody->GetPointVelocity(alignPos) * HavokGetDeltaTime();
		for (U32F jj = 0; jj < kNumJoints; jj++)
		{
			jointPos[jj] += ds;
		}
	}

	const CompositeBodyPenetration* pPenetrationUtil = m_pRagdoll->GetPenetrationUtil();

	F32 rayLength = 0.5f + config.m_blendOutHoleDepth;

	U32F numProbes = 0;
	m_ragdollGroundProbes.Open(5, 1);
	m_ragdollGroundProbes.SetProbeExtents(numProbes, alignPos + VECTOR_LC(0.0f, 0.5f, 0.0f), VECTOR_LC(0.0f, -1.0f, 0.0f), rayLength);
	numProbes++;

	if (jointIndex[0] >= 0)
	{
		if (Dist(jointPos[0], alignPos) > g_distTest && jointPos[0].Y() - alignPos.Y() < g_trunkHeightTest)
		{
			m_ragdollGroundProbes.SetProbeExtents(numProbes, jointPos[0] + VECTOR_LC(0.0f, 0.5f, 0.0f), VECTOR_LC(0.0f, -1.0f, 0.0f), jointPos[0].Y() - alignPos.Y() + rayLength);
			numProbes++;
		}
		else
		{
			if (jointIndex[1] >= 0 && !pPenetrationUtil->IsBodyPenetrated(jointIndex[1]) && Dist(jointPos[1], alignPos) > g_distTest && jointPos[1].Y() - alignPos.Y() < g_limbsHeightTest)
			{
				m_ragdollGroundProbes.SetProbeExtents(numProbes, jointPos[1] + VECTOR_LC(0.0f, 0.5f, 0.0f), VECTOR_LC(0.0f, -1.0f, 0.0f), jointPos[1].Y() - alignPos.Y() + rayLength);
				numProbes++;
			}
			if (jointIndex[2] >= 0 && !pPenetrationUtil->IsBodyPenetrated(jointIndex[2]) && Dist(jointPos[2], alignPos) > g_distTest && jointPos[2].Y() - alignPos.Y() < g_limbsHeightTest)
			{
				m_ragdollGroundProbes.SetProbeExtents(numProbes, jointPos[2] + VECTOR_LC(0.0f, 0.5f, 0.0f), VECTOR_LC(0.0f, -1.0f, 0.0f), jointPos[2].Y() - alignPos.Y() + rayLength);
				numProbes++;
			}
		}
	}
	//ASSERT(jointIndex[3] >= 0); // triggers if we run through here first frame when ragdoll is physicalized
	if (jointIndex[3] >= 0)
	{
		if (Dist(jointPos[3], alignPos) > g_distTest && jointPos[3].Y() - alignPos.Y() < g_trunkHeightTest)
		{
			m_ragdollGroundProbes.SetProbeExtents(numProbes, jointPos[3] + VECTOR_LC(0.0f, 0.5f, 0.0f), VECTOR_LC(0.0f, -1.0f, 0.0f), jointPos[3].Y() - alignPos.Y() + rayLength);
			numProbes++;
		}
		else
		{
			if (jointIndex[4] >= 0 && !pPenetrationUtil->IsBodyPenetrated(jointIndex[4]) && Dist(jointPos[4], alignPos) > g_distTest && jointPos[4].Y() - alignPos.Y() < g_limbsHeightTest)
			{
				m_ragdollGroundProbes.SetProbeExtents(numProbes, jointPos[4] + VECTOR_LC(0.0f, 0.5f, 0.0f), VECTOR_LC(0.0f, -1.0f, 0.0f), jointPos[4].Y() - alignPos.Y() + rayLength);
				numProbes++;
			}
			if (jointIndex[5] >= 0 && !pPenetrationUtil->IsBodyPenetrated(jointIndex[5]) && Dist(jointPos[5], alignPos) > g_distTest && jointPos[5].Y() - alignPos.Y() < g_limbsHeightTest)
			{
				m_ragdollGroundProbes.SetProbeExtents(numProbes, jointPos[5] + VECTOR_LC(0.0f, 0.5f, 0.0f), VECTOR_LC(0.0f, -1.0f, 0.0f), jointPos[5].Y() - alignPos.Y() + rayLength);
				numProbes++;
			}
		}
	}

	m_ragdollGroundProbes.SetFilterForAllProbes(CollideFilter(Collide::kLayerMaskGeneral, Pat(1ULL << Pat::kNoPhysicsShift)));
	m_ragdollGroundProbes.Kick(FILE_LINE_FUNC, numProbes);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SetRagdollDyingSettings(StringId64 id, F32 blendTime)
{
	if (id != m_ragdollDyingSettings)
	{
		if (m_pRagdoll && m_pRagdoll->GetRagdollController()->IsDying())
		{
			m_pRagdoll->GetRagdollController()->SetControlSettings(id, blendTime);
		}
		m_ragdollDyingSettings = id;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::OnBodyKilled(RigidBody& body)
{
	ParentClass::OnBodyKilled(body);
	if (!IsDead())
	{
		MsgConScriptError("Character %s is outside of havok broadphase. Bad things may happen.", DevKitOnly_StringIdToString(GetUserId()));
	}
	else
	{
		m_ragdollOutOfBroadphase = true;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SetTeleport()
{
	ParentClass::SetTeleport();
	if (m_pRagdoll)
		m_pRagdoll->SetTeleport();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pHealthSystem, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pMeleeImpulse, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pCharacterEffectTracker, deltaPos, lowerBound, upperBound);

	if (m_pNearestAlliesByLinearDist)
	{
		RelocatePointer(m_pNearestAlliesByLinearDist->m_ahAlly, deltaPos, lowerBound, upperBound);
	}
	RelocatePointer(m_pNearestAlliesByLinearDist, deltaPos, lowerBound, upperBound);
	if (m_pNearestAlliesByPathDist)
	{
		RelocatePointer(m_pNearestAlliesByPathDist->m_ahAlly, deltaPos, lowerBound, upperBound);
	}
	RelocatePointer(m_pNearestAlliesByPathDist, deltaPos, lowerBound, upperBound);
	if (m_pNearestAlliesForDialog)
	{
		RelocatePointer(m_pNearestAlliesForDialog->m_ahAlly, deltaPos, lowerBound, upperBound);
	}
	RelocatePointer(m_pNearestAlliesForDialog, deltaPos, lowerBound, upperBound);
	if (m_pRagdoll)
	{
		m_pRagdoll->Relocate(deltaPos, lowerBound, upperBound);
		RelocatePointer(m_pRagdoll, deltaPos, lowerBound, upperBound);
	}
	if (m_pHeartRateMonitor)
	{
		DeepRelocatePointer(m_pHeartRateMonitor, deltaPos, lowerBound, upperBound);
	}
	for (int iBouyancy = 0; iBouyancy < m_buoyancyList.Size(); iBouyancy++)
	{
		DeepRelocatePointer(m_buoyancyList[iBouyancy], deltaPos, lowerBound, upperBound);
	}
	m_buoyancyList.Relocate(deltaPos, lowerBound, upperBound);
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);

	DeepRelocatePointer(m_pSubsystemMgr, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct LinearDistSortData
{
	float m_distSq;
	const Character* m_pChar;

	static I32 Compare(const LinearDistSortData& dataA, const LinearDistSortData& dataB)
	{
		if (!dataA.m_pChar)
			return dataB.m_pChar ? +1 : -1;
		else if (!dataB.m_pChar)
			return -1;

		return (dataA.m_distSq < dataB.m_distSq) ? -1 : +1;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
U32F Character::GetNearestAlliesByLinearDist(U32F capacity, const Character* apNearestAllies[]) const
{
	U32F count = 0;
	if (m_pNearestAlliesByLinearDist)
	{
		ASSERT(m_nearestAlliesCapacity != 0);

		U32 frameIndex = (U32)(EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused);
		if (m_pNearestAlliesByLinearDist && frameIndex != m_pNearestAlliesByLinearDist->m_gameFrameNumber)
		{
			// update this list at most once per frame
			m_pNearestAlliesByLinearDist->m_gameFrameNumber = frameIndex;

			ScopedTempAllocator jj(FILE_LINE_FUNC);
			const Character** apAlly = NDI_NEW const Character*[m_nearestAlliesCapacity];
			LinearDistSortData* aSortData = NDI_NEW LinearDistSortData[m_nearestAlliesCapacity];
			ALWAYS_ASSERT(apAlly && aSortData);

			I32F numAllies = CharacterManager::GetManager().GetCharactersFriendlyToFaction(GetFactionId(), apAlly, m_nearestAlliesCapacity);

			// remove myself from the list
			for (I32F iAlly = numAllies - 1; iAlly >= 0; --iAlly)
			{
				const Character* pAlly = apAlly[iAlly];
				if (pAlly == this)
				{
					apAlly[iAlly] = apAlly[numAllies - 1];
					--numAllies;
				}
			}

			const Point sortCenter = GetTranslation();
			for (I32F iAlly = 0; iAlly < numAllies; ++iAlly)
			{
				const Character* pAlly = apAlly[iAlly];

				LinearDistSortData& sortData = aSortData[iAlly];
				sortData.m_distSq = DistSqr(sortCenter, GetProcessSnapshot<CharacterSnapshot>(pAlly)->GetTranslation());
				sortData.m_pChar = pAlly;
			}

			if (numAllies > 0)
			{
				// sort allies from nearest to farthest from speaker
				QuickSort(aSortData, numAllies, LinearDistSortData::Compare);
			}

			// save results for subsequent queries this frame
			m_pNearestAlliesByLinearDist->m_count = numAllies;
			for (U32F iAlly = 0; iAlly < numAllies; ++iAlly)
			{
				m_pNearestAlliesByLinearDist->m_ahAlly[iAlly] = aSortData[iAlly].m_pChar;
			}

			// may as well copy the results directly in this case, instead of converting to ProcessHandle and back below
			count = Min(capacity, (U32F)m_pNearestAlliesByLinearDist->m_count);
			for (U32F iAlly = 0; iAlly < count; ++iAlly)
			{
				apNearestAllies[iAlly] = aSortData[iAlly].m_pChar;
			}
			return count;
		}

		// IMPORTANT: never leave "blank" slots in the retured array... the caller depends on it being contiguous
		ASSERT(m_pNearestAlliesByLinearDist->m_count <= m_nearestAlliesCapacity);
		const U32F allyCount = m_pNearestAlliesByLinearDist->m_count;
		count = 0;
		for (U32F iAlly = 0; iAlly < allyCount && count < capacity; ++iAlly)
		{
			const Character* pAlly = m_pNearestAlliesByLinearDist->m_ahAlly[iAlly].ToProcess();
			if (pAlly)
				apNearestAllies[count++] = pAlly;
		}
	}
	return count;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::IsPointInCoverZoneWs(Point_arg testPosWs) const
{
	const float maxHorizAngleRad = DEGREES_TO_RADIANS(m_horzCoverAngle);
	const float maxVertAngleRad = DEGREES_TO_RADIANS(m_vertCoverAngle);

	const Point alignPosWs = GetLocator().Pos();
	Point sourcePosWs = alignPosWs;
	Vector coverDirWs = GetTakingCoverDirectionWs();

	if (HasShieldEquipped())
	{
		const NdAttachableObject* pShield = GetShield();
		sourcePosWs = Point(sourcePosWs.X(), pShield->GetLocator().GetTranslation().Y(), sourcePosWs.Z());
		coverDirWs = GetLocalZ(GetLocator().Rot());
	}
	else
	{
		// 180 degree cones work best when flush with cover at the AP ref but 90 degree cones work best when pulled back
		// to the align so lerp the cover position between the align and AP ref based on the horizontal cover angle.
		BoundFrame apRef;
		if (IsUsingApOrigin() && GetApOrigin(apRef))
		{
			const Point apRefPosWs = apRef.GetTranslation();
			const float scale = (m_horzCoverAngle - 45.0f) / (90.0f - 45.0f);
			sourcePosWs = Lerp(alignPosWs, apRefPosWs, MinMax01(scale));
			sourcePosWs.SetY(alignPosWs.Y());
		}
	}

	{
		const Vector xzSourceToPtWs = VectorXz(testPosWs - sourcePosWs);
		const Vector normXzSourceToPtWs = SafeNormalize(xzSourceToPtWs, kZero);

		const float dotXZp = DotXz(normXzSourceToPtWs, coverDirWs);
		const float horizTestAngleRad = SafeAcos(dotXZp);

		if (horizTestAngleRad > maxHorizAngleRad)
			return false;
	}

	{
		const Vector sourceToPtWs = testPosWs - sourcePosWs;
		const Vector normSourceToPtWs = SafeNormalize(sourceToPtWs, kZero);

		const float vertTestAngleRad = SafeAsin(normSourceToPtWs.Y());

		if (vertTestAngleRad > maxVertAngleRad)
			return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SetTakingCoverAngle(float horizAngleInDeg, float vertAngleInDeg /* = 60.0f */)
{
	m_horzCoverAngle = horizAngleInDeg;
	m_vertCoverAngle = vertAngleInDeg;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SetTakingCoverDirectionWs(Vector_arg dirWs)
{
	ALWAYS_ASSERT(IsFinite(dirWs));
	m_coverDirWs = dirWs;
	m_coverDirWs.SetY(0.0f);
	m_coverDirWs = SafeNormalize(m_coverDirWs, kUnitZAxis);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SetRawCoverDirectionWs(Vector_arg dirWs)
{
	ALWAYS_ASSERT(IsFinite(dirWs));
	m_rawCoverDirWs = dirWs;
	m_rawCoverDirWs.SetY(0.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
TimeFrame Character::GetVulnerabilityTime() const
{
	return GetCurTime() - m_startVulnerabilityTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
TimeFrame Character::GetCoverVulnerabilityTime() const
{
	return GetCurTime() - m_coverVulnerabilityTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SetVulnerability(DC::Vulnerability vulnerability)
{
	if ((m_vulnerability != DC::kVulnerabilityPeekFromCover && vulnerability == DC::kVulnerabilityPeekFromCover) ||
		(m_vulnerability != DC::kVulnerabilityPeekFromEdge && vulnerability == DC::kVulnerabilityPeekFromEdge))
	{
		m_coverVulnerabilityTime = GetCurTime();
	}

	if (vulnerability != m_vulnerability)
	{
		m_vulnerability = vulnerability;
		m_startVulnerabilityTime = GetCurTime();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 Character::GetCapCharacterId() const
{
	return SID("any");
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::UpdatePushers()
{
	if (g_updateCharacterPushers)
	{
		float deltaTime = GetProcessDeltaTime();
		const float kMaxDeltaTime = 0.1f;  // min frame rate 10fps
		if (deltaTime > 0.0f)
		{
			CompositeBody* pCompositeBody = GetCompositeBody();
			if (pCompositeBody)
			{
				//@GAMEPHYS: Move this into CompositeBody, and reconcile with SyncLocator() and OnPlatformMoved().
				//           Why are they done differently for different cases?  Is there any *real* difference?
				//U32F numBodies = pCompositeBody->GetNumBodies();
				//for (U32F iBody = 0; iBody < numBodies; ++iBody)
				//{
				//	RigidBody* pBody = pCompositeBody->GetBody(iBody);
				//	PhysExternalPusher* pPusher = (pBody) ? pBody->GetPhysPusher() : nullptr;
				//	if (pPusher && g_physEnabled)
				//	{
				//		g_physicsMgr.GetWorldCache()->SyncLocator(pPusher);
				//		// compute motion estimation in pusher
				//		const Locator& oldLoc = pPusher->m_oldLocation;
				//		const Locator& newLoc = pPusher->m_newLocation;
				//		const Vector deltaPos = newLoc.Pos() - oldLoc.Pos();
				//		const Vector motionVec = deltaPos * Scalar(kMaxDeltaTime / deltaTime);
				//		const Scalar w = Length(motionVec) * 0.6f;  // uncertainty distance
				//		Vec4 vMotion = motionVec.GetVec4();
				//		vMotion.SetW(w);
				//		pPusher->m_motion = vMotion;
				//	}
				//}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPack* Character::GetEnteredActionPack() const
{
	// Note(JDB): Since the player doesn't override these functions, we shouldn't assert that
	// they're called. Otherwise any method that uses the character manager to iterate over
	// the characters in the world could never use the resulting pointers to call these functions!
//	ALWAYS_ASSERT(false);
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::IsActionPackReserved() const
{
//	ALWAYS_ASSERT(false);
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::IsActionPackReserved(const ActionPack* pActionPack) const
{
//	ALWAYS_ASSERT(false);
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::IsActionPackEntered() const
{
//	ALWAYS_ASSERT(false);
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator Character::GetEyeWs() const
{
	const FgAnimData* pAnimData = GetAnimData();
	int jointIndex = pAnimData->FindJoint(SID("headb"));
	if (jointIndex >= 0)
	{
		return pAnimData->m_jointCache.GetJointLocatorWs(jointIndex);
	}
	else
	{
		return GetLocator();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Locator Character::GetEarWs() const
{
//TODO(MB) This reduces issues with noise range values, but noise occlusion assumes a position on the head
// 	const Locator eyeLocWs = GetEyeWs();
// 	const Point earPosWs   = kOrigin + ((eyeLocWs.Pos() + (GetTranslation() - kOrigin) - kOrigin) / 2.0f);
//
// 	return Locator(earPosWs, eyeLocWs.GetRotation());

	return GetEyeWs();
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::StealthVegetationHeight Character::GetStealthVegetationHeight() const
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const NavLocation& navLoc = GetNavLocation();
	const NavPoly* pPoly = navLoc.ToNavPoly();

	if (!pPoly)
		return DC::kStealthVegetationHeightNone;

	const DC::StealthVegetationHeight height = pPoly->GetStealthVegetationHeight();

	if (height == DC::kStealthVegetationHeightCrouch && IsCrouched() && IsAimingFirearm())
		return DC::kStealthVegetationHeightNone;

	return height;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::IsBodyPartOfHead(const RigidBody* pBody) const
{
	if (!pBody)
		return false;

	const StringId64 jointId = pBody->GetJointSid();
	switch (jointId.GetValue())
	{
	case SID_VAL("head"):
	case SID_VAL("heada"):
	case SID_VAL("headb"):
		return true;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::IsAimingFirearmAt(Sphere_arg sphere, float referenceDist) const
{
	if (!IsAimingFirearm())
		return false;

	return IsAimingAt(sphere);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SetRope(ProcessPhysRope* pRope)
{
	// We need to delay this because this may be called from script and rope step may be running right now
	m_hPendingRope = pRope;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::KillRope()
{
	if (m_hPendingRope != m_hRope)
	{
		KillProcess(m_hPendingRope);
		m_hPendingRope = nullptr;
	}
	else
	{
		KillProcess(m_hRope);
		m_hRope = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::UpdateRope()
{
	if (m_hPendingRope != m_hRope)
	{
		if (ProcessPhysRope* pPrevRope = m_hRope.ToMutableProcess())
		{
			if (pPrevRope->GetActiveCharacter() == this)
			{
				pPrevRope->ClearActiveCharacter();
			}
		}

		m_hRope = m_hPendingRope;

		if (ProcessPhysRope* pRope = m_hRope.ToMutableProcess())
		{
			pRope->SetActiveCharacter(this, true);
			pRope->m_rope.InitRopeDebugger();
		}
	}

	if (ProcessPhysRope* pRope = m_hRope.ToMutableProcess())
	{
		pRope->UpdateCharacterRopeControl();
		if (CameraManager::Get().DidCameraCutThisFrame())
		{
			pRope->m_rope.ResetDynamics();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::UpdateBuoyancy()
{
	if (IsRagdollDying() && HavokNotPaused())
	{
		bool insideBoat = false;
		if (m_disableRagdollBuoyancyInParentBB)
		{
			if (const RigidBody* pParentBodyBase = m_pRagdoll->GetParentBody())
			{
				if (const NdGameObject* pParentGo = pParentBodyBase->GetOwner())
				{
					Point pelvisLs = m_pRagdoll->GetBody(0)->GetLocator().GetTranslation() * Inverse(pParentGo->GetAnimData()->m_objXform);
					if (pParentGo->GetAnimData()->m_pBoundingInfo->m_aabb.ContainsPoint(pelvisLs))
					{
						// If pelvis is inside bbox of our parent (platform) object skip buoyancy
						// This is for boats because even if we are inside of the boat we may be in fact under the water level
						insideBoat = true;
					}
				}
			}
		}

		if (FactDictionary* pFacts = GetFactDict())
		{
			pFacts->Set(SID("inside-boat"), insideBoat);
		}
		if (insideBoat)
			return;

		if (m_buoyancyEnabled && m_buoyancyList.Size() > 0)
		{
			for (int iBuoyancy = 0; iBuoyancy < m_buoyancyList.Size(); iBuoyancy++)
			{
				WaterQuery::GeneralWaterQuery::CallbackContext context;
				STATIC_ASSERT(sizeof(MyWaterQueryContext) <= sizeof(WaterQuery::GeneralWaterQuery::CallbackContext));
				MyWaterQueryContext* pContext = (MyWaterQueryContext*)&context;
				pContext->m_hGo = this;
				pContext->m_index = iBuoyancy;

				m_buoyancyList[iBuoyancy]->GatherWaterDetector();
				m_buoyancyList[iBuoyancy]->KickWaterDetector(FILE_LINE_FUNC, this, WaterQuery::GeneralWaterQuery::Category::kMisc, m_lastTimeQueryWaterAllowed, FindWaterQuery, &context);
				m_buoyancyList[iBuoyancy]->Update(this);
			}
			if (m_buoyancyList[0]->GetDepth() > 0.40f)
			{
				// If pelvis is under water power off
				m_pRagdoll->GetRagdollController()->PowerOff("Pelvis under water");
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
WaterQuery::GeneralWaterQuery* Character::FindWaterQuery(const WaterQuery::GeneralWaterQuery::CallbackContext* pContext)
{
	const MyWaterQueryContext* pInContext = (const MyWaterQueryContext*)pContext;
	Character* pChar = Character::FromProcess(pInContext->m_hGo.ToMutableProcess());
	if (!pChar)
		return nullptr;

	I32 index = pInContext->m_index;
	if (index >= 0 && index < pChar->m_buoyancyList.Size())
		return pChar->m_buoyancyList[index]->GetWaterQuery();

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::OnMeshRayHit(const MeshProbe::CallbackObject* pObject,
 							 MeshRaycastResult* pResult,
							 const MeshProbe::Probe* pRequest,
							 const Binding& binding)
{
	// make sure the latest result.
	if (pRequest->m_frameNumber > pResult->m_contactFrameNumber)
	{
		pResult->m_valid = true;

		const MeshProbe::ProbeResult& rayResult = pObject->m_probeResults[0];

		const Point posWs = rayResult.m_contactWs;
		const Vector normWs = rayResult.m_normalWs;

		Vector up = SafeNormalize(Cross(normWs, kUnitXAxis), kUnitZAxis);
		pResult->m_contact = BoundFrame(Locator(posWs, Normalize(QuatFromLookAt(normWs, up))), binding);
		pResult->m_contactFrameNumber = pRequest->m_frameNumber;
		pResult->m_t = rayResult.m_tt;

		pResult->m_positionWs = rayResult.m_contactWs;
		pResult->m_normalWs = rayResult.m_normalWs;
		pResult->m_vertexWs0 = rayResult.m_vertexWs0;
		pResult->m_vertexWs1 = rayResult.m_vertexWs1;
		pResult->m_vertexWs2 = rayResult.m_vertexWs2;
		pResult->m_rayStart = pRequest->m_rayStart;
		pResult->m_rayEnd = pRequest->m_rayEnd;
		pResult->m_radius = pRequest->m_rayRadius;

		//if (rayResult.m_isGeoDecal)
		//	pResult->m_hitGeoDecalFrameNumber = pResult->m_contactFrameNumber;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::OnMeshRayFailed(MeshRaycastResult* pResult, const MeshProbe::Probe* pRequest)
{
	if (pRequest->m_frameNumber > pResult->m_contactFrameNumber)
	{
		pResult->m_valid = false;
		pResult->m_contactFrameNumber = pRequest->m_frameNumber;
		//pResult->m_surfaceTypeInfo.Clear();	// we don't clear surface type info if ray missed. so we can use last good surface type.
		pResult->m_rayStart = pRequest->m_rayStart;
		pResult->m_rayEnd = pRequest->m_rayEnd;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void BodyMeshRaycastCallback(const MeshProbe::CallbackObject* pObject, MeshRayCastJob::CallbackContext* pInContext, const MeshProbe::Probe& probeReq)
{
	NdGameObject::MeshRaycastContext* pContext = (NdGameObject::MeshRaycastContext*)pInContext;
	Character* pCharacter = Character::FromProcess(pContext->m_hOwner.ToMutableProcess());
	if (!pCharacter)
		return;

	NdGameObject::MeshRaycastResult* pResult = pCharacter->GetBodyMeshRaycastResult((Character::BodyImpactSoundType)pContext->m_index);
	if (!pResult)
		return;

	if (pObject)
		Character::OnMeshRayHit(pObject, pResult, &probeReq, pCharacter->GetBinding());
	else
		Character::OnMeshRayFailed(pResult, &probeReq);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void BodyMeshRaycastSurfaceTypeCallback(const MeshProbe::FullCallbackObject* pObject,
											   MeshRayCastJob::CallbackContext* pInContext,
											   const MeshProbe::Probe& probeReq)
{
	NdGameObject::MeshRaycastContext* pContext = (NdGameObject::MeshRaycastContext*)pInContext;
	Character* pCharacter = Character::FromProcess(pContext->m_hOwner.ToMutableProcess());
	if (!pCharacter)
		return;

	NdGameObject::MeshRaycastResult* pResult = pCharacter->GetBodyMeshRaycastResult((Character::BodyImpactSoundType)pContext->m_index);
	if (!pResult)
		return;

	Character::OnSurfaceTypeReady(pObject, pResult, &probeReq);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ShoulderMeshRaycastCallback(const MeshProbe::CallbackObject* pObject, MeshRayCastJob::CallbackContext* pInContext, const MeshProbe::Probe& probeReq)
{
	NdGameObject::MeshRaycastContext* pContext = (NdGameObject::MeshRaycastContext*)pInContext;
	Character* pCharacter = Character::FromProcess(pContext->m_hOwner.ToMutableProcess());
	if (!pCharacter)
		return;

	NdGameObject::MeshRaycastResult* pResult = pCharacter->GetShoulderMeshRaycastResult((Character::ShoulderImpactSoundType)pContext->m_index);
	if (!pResult)
		return;

	if (pObject)
		Character::OnMeshRayHit(pObject, pResult, &probeReq, pCharacter->GetBinding());
	else
		Character::OnMeshRayFailed(pResult, &probeReq);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ShoulderMeshRaycastSurfaceTypeCallback(const MeshProbe::FullCallbackObject* pObject,
												   MeshRayCastJob::CallbackContext* pInContext,
												   const MeshProbe::Probe& probeReq)
{
	NdGameObject::MeshRaycastContext* pContext = (NdGameObject::MeshRaycastContext*)pInContext;
	Character* pCharacter = Character::FromProcess(pContext->m_hOwner.ToMutableProcess());
	if (!pCharacter)
		return;

	NdGameObject::MeshRaycastResult* pResult = pCharacter->GetShoulderMeshRaycastResult((Character::ShoulderImpactSoundType)pContext->m_index);
	if (!pResult)
		return;

	Character::OnSurfaceTypeReady(pObject, pResult, &probeReq);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void HandMeshRaycastCallback(const MeshProbe::CallbackObject* pObject, MeshRayCastJob::CallbackContext* pInContext, const MeshProbe::Probe& probeReq)
{
	NdGameObject::MeshRaycastContext* pContext = (NdGameObject::MeshRaycastContext*)pInContext;
	Character* pCharacter = Character::FromProcess(pContext->m_hOwner.ToMutableProcess());
	if (!pCharacter)
		return;

	NdGameObject::MeshRaycastResult* pResult = pCharacter->GetHandMeshRaycastResult((ArmIndex)pContext->m_index);
	if (!pResult)
		return;

	if (pObject)
		Character::OnMeshRayHit(pObject, pResult, &probeReq, pCharacter->GetBinding());
	else
		Character::OnMeshRayFailed(pResult, &probeReq);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void HandMeshRaycastSurfaceTypeCallback(const MeshProbe::FullCallbackObject* pObject,
											   MeshRayCastJob::CallbackContext* pInContext,
											   const MeshProbe::Probe& probeReq)
{
	NdGameObject::MeshRaycastContext* pContext = (NdGameObject::MeshRaycastContext*)pInContext;
	Character* pCharacter = Character::FromProcess(pContext->m_hOwner.ToMutableProcess());
	if (!pCharacter)
		return;

	NdGameObject::MeshRaycastResult* pResult = pCharacter->GetHandMeshRaycastResult((ArmIndex)pContext->m_index);
	if (!pResult)
		return;

	Character::OnSurfaceTypeReady(pObject, pResult, &probeReq);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject::MeshRaycastResult* Character::GetBodyMeshRaycastResult(BodyImpactSoundType index)
{
	ALWAYS_ASSERT(index >= 0 && index < kNumBodyImpactSound);
	return &m_bodyMeshRaycastResult[index];
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject::MeshRaycastResult* Character::GetShoulderMeshRaycastResult(ShoulderImpactSoundType index)
{
	ALWAYS_ASSERT(index >= 0 && index < kNumShoulderImpactSound);
	return &m_shoulderRaycastResult[index];
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject::MeshRaycastResult* Character::GetHandMeshRaycastResult(ArmIndex index)
{
	ALWAYS_ASSERT(index >= 0 && index < kArmCount);
	return &m_handRaycastResult[index];
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void SoundImpactCheck(Character* pChar,
							 const Point* pRayStart,
							 const Point* pRayEnd,
							 const float* meshRayRadius,
							 Character::MeshRaycastResult* pResults,
							 I32 numRays,
							 bool debugDraw,
							 MeshRayCastJob::CallbackFunc* pCallback,
							 MeshRayCastJob::SurfaceTypeCallbackFunc* pSurfaceTypeCallback)
{
	for (U32F ii = 0; ii < numRays; ii++)
	{
		MeshRayCastJob::CallbackContext context;
		STATIC_ASSERT(sizeof(NdGameObject::MeshRaycastContext) <= sizeof(MeshRayCastJob::CallbackContext));

		const Point meshRayStart = pRayStart[ii];
		const Point meshRayEnd = pRayEnd[ii];
		GAMEPLAY_ASSERT(IsFinite(meshRayStart));
		GAMEPLAY_ASSERT(IsFinite(meshRayEnd));
		const float radius = meshRayRadius != nullptr ? Max(meshRayRadius[ii], 0.f) : 0.f;

		NdGameObject::MeshRaycastContext* pContext = (NdGameObject::MeshRaycastContext*)&context;
		pContext->m_hOwner = MutableCharacterHandle(pChar);
		pContext->m_index = ii;
		pContext->m_debugDrawVoidSurface = false;

		MeshRayCastJob::HitFilter filter(MeshRayCastJob::HitFilter::kHitSoundEffect, pChar->GetProcessId());

		MeshSphereCastJob sphereJob;
		sphereJob.SetProbeExtent(meshRayStart, meshRayEnd);
		sphereJob.SetProbeRadius(radius);
		sphereJob.SetHitFilter(filter);
		sphereJob.SetBehaviorFlags(MeshRayCastJob::kEveryFrame);
		sphereJob.SetPriority(MeshRayCastJob::kPriorityLow);
		sphereJob.m_pCallback = pCallback;
		sphereJob.m_pSurfaceTypeCallback = pSurfaceTypeCallback;
		sphereJob.m_pCallbackContext = &context;

		sphereJob.Kick(FILE_LINE_FUNC);
	}

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		for (U32F ii = 0; ii < numRays; ii++)
		{
			DebugPrimTime debugDrawTime = DebugPrimTime(kPrimDuration1FramePauseable);

			const NdGameObject::MeshRaycastResult& result = pResults[ii];
			const MeshProbe::SurfaceTypeLayers surfaceTypeInfo = result.m_surfaceTypeInfo;

			char text[128];
			int length = snprintf(text, sizeof(text) - 1, "snd");
			if (surfaceTypeInfo.m_layers[0].m_surfaceType.Valid())
			{
				char helper[256];
				surfaceTypeInfo.m_layers[0].m_surfaceType.ToString(helper, sizeof(helper) - 1);
				length += snprintf(text + length, sizeof(text) - 1 - length, ", l0:%s", helper);
			}
			if (surfaceTypeInfo.m_layers[1].m_surfaceType.Valid())
			{
				char helper[256];
				surfaceTypeInfo.m_layers[1].m_surfaceType.ToString(helper, sizeof(helper) - 1);
				length += snprintf(text + length, sizeof(text) - 1 - length, ", l1:%s", helper);
			}

			g_prim.Draw(DebugString(result.m_rayStart, text, kColorWhite, 0.5f), debugDrawTime);

			g_prim.Draw(DebugLine(result.m_rayStart, result.m_rayEnd, kColorCyan, kColorCyan, 2.0f, PrimAttrib(kPrimEnableHiddenLineAlpha)), debugDrawTime);

			if (result.m_valid)
			{
				g_prim.Draw(DebugCross(result.m_positionWs, 0.1f, 4.0f, kColorRed, kPrimEnableHiddenLineAlpha), debugDrawTime);
				g_prim.Draw(DebugLine(result.m_positionWs, result.m_normalWs * 0.2f, kColorGreen, kColorGreen, 4.0f, kPrimEnableHiddenLineAlpha), debugDrawTime);

				g_prim.Draw(DebugLine(result.m_vertexWs0, result.m_vertexWs1, kColorBlue, 1.0f, kPrimEnableHiddenLineAlpha), debugDrawTime);
				g_prim.Draw(DebugLine(result.m_vertexWs1, result.m_vertexWs2, kColorBlue, 1.0f, kPrimEnableHiddenLineAlpha), debugDrawTime);
				g_prim.Draw(DebugLine(result.m_vertexWs2, result.m_vertexWs0, kColorBlue, 1.0f, kPrimEnableHiddenLineAlpha), debugDrawTime);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
MeshProbe::SurfaceType Character::GetBodyImpactSurfaceType() const
{
	I32 bestIndex = kBodyImpactBackSoundCt;
	I64 bestFrameNumber = m_bodyMeshRaycastResult[bestIndex].m_surfaceFrameNumber;

	for (U32F ii = kBodyImpactBackSoundLt; ii < kNumBodyImpactSound; ii++)
	{
		I32 offset = (bestIndex == kBodyImpactBackSoundCt) ? 2 : 0;	// center probe is top priority.

		if (m_bodyMeshRaycastResult[ii].m_surfaceFrameNumber > bestFrameNumber + offset)
		{
			bestFrameNumber = m_bodyMeshRaycastResult[ii].m_surfaceFrameNumber;
			bestIndex = ii;
		}
	}

	if (bestIndex >= 0)
	{
		return m_bodyMeshRaycastResult[bestIndex].m_surfaceTypeInfo.m_layers[0].m_surfaceType;
	}

	return SID("stone-asphalt");
}

/// --------------------------------------------------------------------------------------------------------------- ///
Character::StanceState Character::GetStanceState() const
{
	if (IsProne() || IsDead())
	{
		return StanceState::kProne;
	}
	else if (IsCrouched())
	{
		return StanceState::kCrouch;
	}
	else
	{
		return StanceState::kStand;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SoundBodyImpactCheck(const SoundSurfaceJobData& soundSurfaceData)
{
	bool bodyImpactRayEnabled = m_lastBodyImpactRayTime > Seconds(0) && GetCurTime() - m_lastBodyImpactRayTime < Seconds(2.f);

	Point rayStart[kNumBodyImpactSound] = { kOrigin, kOrigin, kOrigin, kOrigin };
	Point rayEnd[kNumBodyImpactSound] = { kOrigin, kOrigin, kOrigin, kOrigin };
	float rayRadius[kNumBodyImpactSound] = { m_bodyImpactRayRadius, m_bodyImpactRayRadius, m_bodyImpactRayRadius, m_bodyImpactRayRadius };

	if (bodyImpactRayEnabled)
	{
		Point pelvisPos = soundSurfaceData.characterLoc.Pos() + GetLocalY(soundSurfaceData.characterLoc.Rot()) * 1.f;

		// chest ray.
		{
			// shoot ray 45 degrees down so that it can hit the ledge.
			const Quat testRot =soundSurfaceData.characterLoc.Rot() * QuatFromAxisAngle(Vector(kUnitXAxis), PI / 4.f);
			const Vector testDir = GetLocalZ(testRot);
			rayStart[kBodyImpactChestSound] = pelvisPos;
			rayEnd[kBodyImpactChestSound] = rayStart[kBodyImpactChestSound] + testDir * m_bodyImpactRayLength;
		}

		// back ray.
		{
			Vector testDir = -GetLocalZ(soundSurfaceData.characterLoc.Rot());
			if (soundSurfaceData.m_attachValid[kSoundSurfaceJobAttachPelvis])
				testDir = GetLocalY(soundSurfaceData.m_attachLoc[kSoundSurfaceJobAttachPelvis].Rot());

			const Quat ltRot = QuatFromAxisAngle(Vector(kUnitYAxis), DEGREES_TO_RADIANS(45.f));
			const Quat rtRot = QuatFromAxisAngle(Vector(kUnitYAxis), DEGREES_TO_RADIANS(-45.f));

			rayStart[kBodyImpactBackSoundCt] = pelvisPos;
			rayStart[kBodyImpactBackSoundLt] = pelvisPos;
			rayStart[kBodyImpactBackSoundRt] = pelvisPos;
			rayEnd[kBodyImpactBackSoundCt] = rayStart[kBodyImpactBackSoundCt] + testDir * m_bodyImpactRayLength;
			rayEnd[kBodyImpactBackSoundLt] = rayStart[kBodyImpactBackSoundLt] + Rotate(ltRot, testDir) * m_bodyImpactRayLength;
			rayEnd[kBodyImpactBackSoundRt] = rayStart[kBodyImpactBackSoundRt] + Rotate(rtRot, testDir) * m_bodyImpactRayLength;
		}
	}
	else
	{
		// reset to default values.
		m_bodyImpactRayLength = 2.f;
		m_bodyImpactRayRadius = 0.05f;
	}

	if (bodyImpactRayEnabled)
	{
		SoundImpactCheck(this, rayStart, rayEnd, rayRadius, m_bodyMeshRaycastResult, kNumBodyImpactSound, g_showBodyImpactSurfaceType,
			BodyMeshRaycastCallback, BodyMeshRaycastSurfaceTypeCallback);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SoundShoulderImpactCheck(const SoundSurfaceJobData& soundSurfaceData)
{
	bool rayEnabled = m_lastShoulderImpactRayTime > Seconds(0) && GetCurTime() - m_lastShoulderImpactRayTime < Seconds(2.f);

	Point rayStart[kNumShoulderImpactSound] = { kOrigin, kOrigin };
	Point rayEnd[kNumShoulderImpactSound] = { kOrigin, kOrigin };
	float rayRadius[kNumShoulderImpactSound] = { m_shoulderImpactRayRadius, m_shoulderImpactRayRadius };

	if (rayEnabled)
	{
		Point chestPos = soundSurfaceData.characterLoc.Pos();
		if (soundSurfaceData.m_attachValid[kSoundSurfaceJobAttachChest])
			chestPos = soundSurfaceData.m_attachLoc[kSoundSurfaceJobAttachChest].Pos();


		int shoulderAttachIndices[kNumShoulderImpactSound] = { kSoundSurfaceJobAttachShoulderL, kSoundSurfaceJobAttachShoulderR };
		for (U32F i = 0; i < kNumShoulderImpactSound; i++)
		{
			Point shoulderPos(kOrigin);
			if (soundSurfaceData.m_attachValid[shoulderAttachIndices[i]])
				shoulderPos = soundSurfaceData.m_attachLoc[shoulderAttachIndices[i]].Pos();

			const Vector dir = SafeNormalize(VectorXz(shoulderPos - chestPos), kUnitZAxis);
			rayStart[i] = shoulderPos - dir * 0.2f;
			rayEnd[i] = rayStart[i] + dir * m_shoulderImpactRayLength;
		}
	}
	else
	{
		// reset to default values.
		m_shoulderImpactRayLength = 2.f;
		m_shoulderImpactRayRadius = 0.05f;
	}

	if (rayEnabled)
	{
		SoundImpactCheck(this, rayStart, rayEnd, rayRadius, m_shoulderRaycastResult, kNumShoulderImpactSound, g_showShoulderImpactSurfaceType,
			ShoulderMeshRaycastCallback, ShoulderMeshRaycastSurfaceTypeCallback);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SoundHandsImpactCheck(const SoundSurfaceJobData& soundSurfaceData)
{
	bool rayEnabled = false;
	if (IsBuddyNpc())
	{
		if (IsClimbing() || IsClimbingLadder())
			rayEnabled = true;

		if (!rayEnabled)
		{
			const NavCharacter* pNavChar = NavCharacter::FromProcess(this);
			if (pNavChar != NULL && pNavChar->IsUsingTraversalActionPack())
				rayEnabled = true;
		}
	}

	Point rayStart[kArmCount] = { kOrigin, kOrigin };
	Point rayEnd[kArmCount] = { kOrigin, kOrigin };
	float meshRayRadius[kArmCount] = { 0.05f, 0.05f };

	if (rayEnabled)
	{
		// fill arms
		GAMEPLAY_ASSERT(soundSurfaceData.m_attachValid[kSoundSurfaceJobAttachWristL] && soundSurfaceData.m_attachValid[kSoundSurfaceJobAttachWristR]);
		Locator lWrist = soundSurfaceData.m_attachLoc[kSoundSurfaceJobAttachWristL];
		Locator rWrist = soundSurfaceData.m_attachLoc[kSoundSurfaceJobAttachWristR];

		Vector lDeltaBig = -GetLocalY(lWrist.GetRotation());
		Vector rDeltaBig = GetLocalY(rWrist.GetRotation());

		float rayLengthBig = 1.f;

		rayStart[kLimbArmL] = lWrist.GetTranslation() - lDeltaBig * 0.1f;
		rayStart[kLimbArmR] = rWrist.GetTranslation() - rDeltaBig * 0.1f;

		rayEnd[kLimbArmL] = rayStart[kLimbArmL] + lDeltaBig * rayLengthBig;
		rayEnd[kLimbArmR] = rayStart[kLimbArmR] + rDeltaBig * rayLengthBig;
	}

	if (rayEnabled)
	{
		SoundImpactCheck(this, rayStart, rayEnd, meshRayRadius, m_handRaycastResult, kArmCount, g_showNpcHandImpactSurfaceType,
			HandMeshRaycastCallback, HandMeshRaycastSurfaceTypeCallback);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT_CLASS_DEFINE(Character, SoundSurfaceTypeJob)
{
	SoundSurfaceJobData* pSoundSurfaceData = (SoundSurfaceJobData*)jobParam;
	Character* pChar = pSoundSurfaceData->m_pChar;

	pChar->SoundBodyImpactCheck(*pSoundSurfaceData);
	pChar->SoundShoulderImpactCheck(*pSoundSurfaceData);
	pChar->SoundHandsImpactCheck(*pSoundSurfaceData);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::KickSoundImpactCheckJob()
{
	if (m_surfaceTypeProbeCounter)
	{
		ndjob::WaitForCounterAndFree(m_surfaceTypeProbeCounter);
		m_surfaceTypeProbeCounter = nullptr;
	}

	SoundSurfaceJobData* pSoundSurfaceData = NDI_NEW(kAllocSingleGameFrame) SoundSurfaceJobData;
	pSoundSurfaceData->m_pChar = this;
	pSoundSurfaceData->characterLoc = GetLocator();

	AttachSystem* pAttachSystem = GetAttachSystem();

	for (int iAttach=0; iAttach<kSoundSurfaceJobAttachCount; iAttach++)
	{
		StringId64 attachName = INVALID_STRING_ID_64;
		switch (iAttach)
		{
			case kSoundSurfaceJobAttachPelvis: attachName = SID("targPelvis"); break;
			case kSoundSurfaceJobAttachChest: attachName = SID("targChest"); break;
			case kSoundSurfaceJobAttachWristL: attachName = SID("targLWrist"); break;
			case kSoundSurfaceJobAttachWristR: attachName = SID("targRWrist"); break;
			case kSoundSurfaceJobAttachShoulderL: attachName = SID("targLShoulder"); break;
			case kSoundSurfaceJobAttachShoulderR: attachName = SID("targRShoulder"); break;
		}
		GAMEPLAY_ASSERT(attachName != INVALID_STRING_ID_64);

		AttachIndex ind;
		pSoundSurfaceData->m_attachValid[iAttach] = pAttachSystem->FindPointIndexById(&ind, attachName);
		if (pSoundSurfaceData->m_attachValid[iAttach])
			pSoundSurfaceData->m_attachLoc[iAttach] = pAttachSystem->GetLocator(ind);
	}

	ndjob::JobDecl decl(SoundSurfaceTypeJob, (uintptr_t)pSoundSurfaceData);
	ndjob::RunJobs(&decl, 1, &m_surfaceTypeProbeCounter, FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject::MeshRaycastResult* Character::GetSplasherMeshRaycastResult(DC::SplasherLimb splasherLimb)
{
	int legIndex = -1;
	switch (splasherLimb)
	{
		case DC::kSplasherLimbFootLeft:		legIndex = kLeftLeg; break;
		case DC::kSplasherLimbFootRight:	legIndex = kRightLeg; break;
	}

	MeshRaycastResult* pResult = nullptr;
	if (legIndex >= 0)
		pResult = GetLegMeshRaycastResult((LegIndex)legIndex);

	if (!pResult)
		pResult = GetGroundMeshRaycastResult(NdGameObject::kMeshRaycastGround);

	return pResult;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector Character::GetGroundMeshRayDir(MeshRaycastType type) const
{
	PROFILE(AI, Character_SetupGroundSurfaceTypeProbe);

	bool isClimbing = (type == kMeshRaycastGround) && (IsClimbingLadder() || IsClimbing());

	const Locator parentSpace = GetParentSpace();
	const Vector rayDirWs = isClimbing ? GetLocalZ(GetRotation()) : -GetLocalY(parentSpace.Rot());
	return rayDirWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Push Head Ik.
void Character::InitializePushHeadIk(Character* pChar, JointTree* pJointTree, JacobianMap* pJacobianMap, StringId64 ikSettingsId)
{
	StringId64 headb = SID("headb");

	pJointTree->Init(pChar, SID("root"), false, 1, headb.GetValue());
	pJointTree->InitIkData(ikSettingsId);

	JacobianMap::EndEffectorDef headEffs[] = {
		JacobianMap::EndEffectorDef(headb, IkGoal::kLookTarget, Locator(Point(0.0f, 0.0f, 0.1f), kIdentity))
	};

	pJacobianMap->Init(pJointTree, SID("neck"), ARRAY_COUNT(headEffs), headEffs);
	pJacobianMap->m_rotationGoalFactor = 1.0f;
}

void Character::ApplyPushHeadIk(JointSet* pJoints, JacobianMap* pJacobianMap, Point lookAtLocation, float blendAmount)
{
	if (blendAmount > 0.f)
	{
		JacobianIkInstance ik;
		ik.m_pJoints = pJoints;
		ik.m_pConstraints = pJoints->GetJointConstraints();
		ik.m_maxIterations = 10;

		ik.m_blend = blendAmount;
		{
			Locator headLoc = ik.m_pJoints->GetJointLocWs(ik.m_pJoints->FindJointOffset(SID("headb")));
			ik.m_goal[0].SetGoalLookTarget(lookAtLocation, IkGoal::kLookTargetAxisYNeg, GetLocalY(headLoc.Rot()));
		}

		ik.m_pJacobianMap = pJacobianMap;

		SolveJacobianIK(&ik);

		pJoints->WriteJointCache();
	}
	else
	{
		pJoints->DiscardJointCache();
	}
}

void Character::UpdatePushHeadIk(Character* pChar,
								 JointSet* pJoints,
								 JacobianMap* pJacobianMap,
								 Point lookAtLocation,
								 float blendAmount)
{
	pJoints->ReadJointCache();
	ApplyPushHeadIk(pJoints, pJacobianMap, lookAtLocation, blendAmount);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Push Arm Ik.
void Character::InitializePushArmIk(Character* pChar, JointTree* pJointTree, JacobianMap* pJacobianMap, StringId64 ikSettingsId)
{
	StringId64 lWrist = SID("l_wrist");
	StringId64 rWrist = SID("r_wrist");

	pJointTree->Init(pChar, SID("root"), false, 2, lWrist.GetValue(), rWrist.GetValue());
	pJointTree->InitIkData(ikSettingsId);

	// use full body ik!
	JacobianMap::EndEffectorDef handEffs[] =
	{
		JacobianMap::EndEffectorDef(lWrist, IkGoal::kPosition),
		JacobianMap::EndEffectorDef(lWrist, IkGoal::kRotation),
		JacobianMap::EndEffectorDef(rWrist, IkGoal::kPosition),
		JacobianMap::EndEffectorDef(rWrist, IkGoal::kRotation),
	};
	pJacobianMap->Init(pJointTree, SID("spinea"), ARRAY_COUNT(handEffs), handEffs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::ApplyPushArmIk(JointSet* pJoints, JacobianMap* pJacobianMap, const Locator* pTargetWristLocs, float tt)
{
	if (tt > 0.f)
	{
		JacobianIkInstance ik;
		ik.m_pJoints = pJoints;
		ik.m_pConstraints = pJoints->GetJointConstraints();
		ik.m_maxIterations = 10;

		ik.m_blend = tt;
		{
			const Locator& targetWristLoc = pTargetWristLocs[kLeftArm];
			ik.m_goal[0].SetGoalPosition(targetWristLoc.GetTranslation());
			ik.m_goal[1].SetGoalRotation(targetWristLoc.GetRotation());
		}
		{
			const Locator& targetWristLoc = pTargetWristLocs[kRightArm];
			ik.m_goal[2].SetGoalPosition(targetWristLoc.GetTranslation());
			ik.m_goal[3].SetGoalRotation(targetWristLoc.GetRotation());
		}

		ik.m_pJacobianMap = pJacobianMap;

		SolveJacobianIK(&ik);

		pJoints->WriteJointCache();
	}
	else
	{
		pJoints->DiscardJointCache();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// hand offset relative to push object.
bool Character::GetHandRelativeToObject(Character* pChar,
										StringId64 handId,
										const Locator& playerLocator,
										Locator& loc,
										AnimStateLayerFilterAPRefCallBack filterCallback,
										bool debugDraw)
{
	ndanim::JointParams joint0;
	bool foundAp = GetAPReferenceByNameConditional(pChar->GetAnimControl()->GetBaseStateLayer(), SID("apReference"), joint0, true, nullptr, false, true, DC::kAnimFlipModeFromInstance, filterCallback, true);
	Locator apLoc(joint0.m_trans, joint0.m_quat);

	if (foundAp)
	{
		ndanim::JointParams joint1;
		bool foundWristAp = GetAPReferenceByNameConditional(pChar->GetAnimControl()->GetBaseStateLayer(), handId, joint1, true, nullptr, false, true, DC::kAnimFlipModeNever, filterCallback, true);
		Locator wristLoc(joint1.m_trans, joint1.m_quat);

		if (foundWristAp)
		{
			loc = apLoc.UntransformLocator(wristLoc);

			if (FALSE_IN_FINAL_BUILD(debugDraw))
			{
				const Locator& testLoc = playerLocator.TransformLocator(loc);
				g_prim.Draw(DebugCoordAxes(testLoc, 0.2f, PrimAttrib(0)), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugString(testLoc.GetTranslation(), StringBuilder<64>("%s", DevKitOnly_StringIdToString(handId)).c_str(), kColorWhite, 0.8f), kPrimDuration1FramePauseable);
			}
			return true;
		}
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::UpdatePushArmIk(Character* pChar,
								const BoundFrame& pushAp,
								JointSet* pJoints,
								JacobianMap* pJacobianMap,
								float tt,
								AnimStateLayerFilterAPRefCallBack filterCallback,
								bool debugDraw)
{
	const Locator& objApReferenceLocator = pushAp.GetLocatorWs();

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		g_prim.Draw(DebugCoordAxes(objApReferenceLocator, 0.2f, PrimAttrib(0)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugString(objApReferenceLocator.GetTranslation(), "objApRef", kColorWhite, 0.7f), kPrimDuration1FramePauseable);
	}

	bool leftValid = false;
	bool rightValid = false;

	Locator targetWristLocs[kArmCount];

	// left arm
	{
		Locator locRelativeToObject;
		// because player is one frame later, use apFromAnim instread of current object apReference
		bool resolved = GetHandRelativeToObject(pChar, SID("apReference-hand-l"), objApReferenceLocator, locRelativeToObject, filterCallback, debugDraw);
		if (resolved)
		{
			targetWristLocs[kLeftArm] = objApReferenceLocator.TransformLocator(locRelativeToObject);
			leftValid = true;
		}
	}
	// right arm
	{
		Locator locRelativeToObject;
		// because player is one frame later, use apFromAnim instread of current object apReference
		bool resolved = GetHandRelativeToObject(pChar, SID("apReference-hand-r"), objApReferenceLocator, locRelativeToObject, filterCallback, debugDraw);
		if (resolved)
		{
			targetWristLocs[kRightArm] = objApReferenceLocator.TransformLocator(locRelativeToObject);
			rightValid = true;
		}
	}

	if (leftValid && rightValid)
	{
		ScopedTempAllocator jj(FILE_LINE_FUNC);

		pJoints->ReadJointCache();
		ApplyPushArmIk(pJoints, pJacobianMap, targetWristLocs, tt);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::GetSurfaceContactPoint(Point_arg s0, Point_arg s1, const RigidBody* pBody, Point& pnt, F32 maxDist) const
{
	const hknpShape* pShape = pBody->GetHavokShape();
	const Locator bodyLocWs = pBody->GetLocatorCm();

	// Havok GSK cannot work with just edges so we need to use thin capsule
	U8* pBuf = STACK_ALLOC_ALIGNED(U8, hknpCapsuleShape::getInPlaceSize(), kAlign16);
	hknpCapsuleShape* pQueryShape = hknpCapsuleShape::createInPlace(pBuf, hknpCapsuleShape::getInPlaceSize(), hkVector4(s0.QuadwordValue()), hkVector4(s1.QuadwordValue()), 0.01f);

	ClosestPointContact res;
	bool hit = HavokShapeClosestPointQuery(pQueryShape, Locator(kIdentity), pShape, bodyLocWs, maxDist, res);
	if (!hit)
	{
		return false;
	}
	pnt = res.m_pos;
	return true;
}

const RigidBody* Character::GetBestSurfaceRigidBodyDeprecated(Point_arg p0, Point &outSurfacePoint) const
{
	const CompositeBody* pCompositeBody = GetCompositeBody();

	F32 bestDist = FLT_MAX;
	const RigidBody* pBestBody = nullptr;
	outSurfacePoint = p0;
	if (pCompositeBody)
	{
		for (U32F iBody = 0; iBody < pCompositeBody->GetNumBodies(); ++iBody)
		{
			const RigidBody* pBody = pCompositeBody->GetBody(iBody);
			if (!IsBodyMeleeEnabled(pBody))
				continue;

			Point targetPoint;
			if (GetSurfaceContactPoint(p0, p0, pBody, targetPoint, bestDist))
			{
				outSurfacePoint = targetPoint;
				bestDist = Dist(targetPoint, p0);
				pBestBody = pBody;
			}
		}
	}

	return pBestBody;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::TurnOnCapsules()
{
	m_meleeCapsulesOn = true;

	CompositeBody* pCompositeBody = GetCompositeBody();
	if (pCompositeBody)
	{
		for (U32F iBody = 0; iBody < pCompositeBody->GetNumBodies(); ++iBody)
		{
			RigidBody* pBody = pCompositeBody->GetBody(iBody);
			const DC::CharacterCollisionSettings* pDcSet = CharacterCollision::GetBodyCollisionSettings(pBody);
			if (pDcSet && pDcSet->m_meleeCore)
			{
				Locator offset;
				if (const DC::CharacterCollisionBody* pDcBody = pBody->GetCharacterCollisionBody())
				{
					// Old way
					offset = Locator(pDcBody->m_offset, pDcBody->m_rotation);
				}
				else
				{
					const HavokProtoBody* pProtoBody = pBody->GetProtoBody();
					GAMEPLAY_ASSERT(pProtoBody);
					offset = pProtoBody->m_body2JointLoc;
				}
				pBody->SetAttachOffset(offset);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::TurnOffCapsules()
{
	m_meleeCapsulesOn = false;

	CompositeBody* pCompositeBody = GetCompositeBody();
	if (pCompositeBody)
	{
		for (U32F iBody = 0; iBody < pCompositeBody->GetNumBodies(); ++iBody)
		{
			RigidBody* pBody = pCompositeBody->GetBody(iBody);
			const DC::CharacterCollisionSettings* pDcSet = CharacterCollision::GetBodyCollisionSettings(pBody);
			if (pDcSet && pDcSet->m_meleeCore)
			{
				Locator offset;
				if(const DC::CharacterCollisionBody* pDcBody = pBody->GetCharacterCollisionBody())
				{
					// Old way
					offset = Locator(pDcBody->m_offset, pDcBody->m_rotation);
				}
				else
				{
					const HavokProtoBody* pProtoBody = pBody->GetProtoBody();
					GAMEPLAY_ASSERT(pProtoBody);
					offset = pProtoBody->m_body2JointLoc;
				}

				Locator bodyLoc = pBody->GetLocator();
				Vector motionDir = GetVelocityWs();
				motionDir = SafeNormalize(motionDir, kZero);
				offset.Move(bodyLoc.UntransformVector(motionDir * 0.13f));
				pBody->SetAttachOffset(offset);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::IsBodyMeleeEnabled(const RigidBody* pBody) const
{
	if (pBody->GetGameLinkage() == RigidBody::kLinkedToObject)
	{
		// The Align capsule
		return false;
	}
	if (const DC::CharacterCollisionSettings* pDcSet = CharacterCollision::GetBodyCollisionSettings(pBody))
	{
		if (pDcSet->m_meleeThrough)
		{
			return false;
		}
		if (!pDcSet->m_meleeCore && !m_meleeCapsulesOn)
		{
			return false;
		}
	}
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::DebugDraw(WindowContext* pDebugWindowContext, ScreenSpaceTextPrinter* pPrinter) const
{
	PROFILE(AI, Character_DebugDraw);

	if (m_pHeartRateMonitor)
	{
		m_pHeartRateMonitor->DebugDraw(this, pPrinter);
	}

	if (FALSE_IN_FINAL_BUILD(DebugSelection::Get().IsProcessOrNoneSelected(this)) && g_navCharOptions.m_displayGoreDamageState)
	{
		m_goreDamageState.DebugDraw(pPrinter);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::DebugDraw(const DebugDrawFilter& filter) const
{
	if (const IGestureController* pGestureController = GetGestureController())
	{
		pGestureController->DebugDraw();
	}

	if (filter.m_drawCharacterCollisionCapsule)
	{
		const DC::CharacterCollision* pCharacterCollision = GetCharacterCollision();
		const CompositeBody* pCompositeBody = GetCompositeBody();

		if (pCharacterCollision && pCompositeBody)
		{
			for (U32F iBody = 0; iBody < pCompositeBody->GetNumBodies(); ++iBody)
			{
				const RigidBody* pBody = pCompositeBody->GetBody(iBody);
				if (pBody->GetGameLinkage() == RigidBody::kLinkedToObject)
					continue;

				const Locator bodyLocWs = pBody->GetLocatorCm();
				Color32 color = !IsBodyMeleeEnabled(pBody) ? kColorBlue : kColorCyan;
				if (GetHealthSystem()->InIFrame())
				{
					color = kColorYellow;
				}

				HavokDebugDrawRigidBody(pBody->GetHavokBodyId(), bodyLocWs, color, CollisionDebugDrawConfig::MenuOptions(), kPrimDuration1FramePauseable);
			}
		}
	}

	if (g_navMeshDrawFilter.m_drawStealthHeights)
	{
		const DC::StealthVegetationHeight height = GetStealthVegetationHeight();
		const char* pStr = "None";
		switch (height)
		{
		case DC::kStealthVegetationHeightProne:
			pStr = "Prone";
			break;
		case DC::kStealthVegetationHeightCrouch:
			pStr = "Crouch";
			break;
		case DC::kStealthVegetationHeightStand:
			pStr = "Stand";
			break;
		case DC::kStealthVegetationHeightInvalid:
			pStr = "Invalid";
			break;
		}

		g_prim.Draw(DebugString(GetTranslation() + 0.5f * Vector(kUnitYAxis), pStr, kColorYellow, 0.5f));
	}

	ParentClass::DebugDraw(filter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::ResetDebugTextInFinal()
{
	memset(m_debugTextInFinal, 0, sizeof(m_debugTextInFinal));
	m_debugTextInFinalSize = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::AppendDebugTextInFinal(const char* pText)
{
	if (!pText)
		return;

	const int len = strnlen(pText, 1024);

	{
		AtomicLockJanitor jj(&m_debugTextLock, FILE_LINE_FUNC);

		if (len + m_debugTextInFinalSize < sizeof(m_debugTextInFinal))
		{
			m_debugTextInFinalSize += snprintf(m_debugTextInFinal + m_debugTextInFinalSize, sizeof(m_debugTextInFinal) - m_debugTextInFinalSize, "%s", pText);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::DebugPrintTextInFinal(WindowContext* _pWindowContext)
{
	extern bool g_debugMovieInFinal;
	if (g_motionMatchingOptions.m_debugInFinalPlayerState ||
		g_debugPrintCharacterTextInFinal ||
		g_debugPlayerInventoryInFinal ||
		g_debugPlayerInventorySelectionInFinal ||
		g_debugPlayerInventoryCheckpointInFinal ||
		g_debugPlayerClamberTeleportBug ||
		g_debugMovieInFinal)
	{
		if (m_debugTextInFinalSize > 0)
		{
			textPrint(_pWindowContext, 20, 30, 0.5f, 0.5f, kColorWhite.ToAbgr8(), "%.4096s\n", m_debugTextInFinal);
		}
	}

	ResetDebugTextInFinal();
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 Character::GetSurfaceExpand() const
{
	const I64 currFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	if (m_overridenSurfaceExpandFN + 1 >= currFN)
	{ 
		if (m_overridenSurfaceExpand >= 0.f)
			return m_overridenSurfaceExpand;
	}

	const Scalar surfaceExpand = GetDrawControl()->IsNearCamera() ? SCALAR_LC(0.1f) : SCALAR_LC(0.06f);
	return surfaceExpand;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::IsClippingCameraPlane(bool useCollCastForBackpack, float* pNearestApproach) const
{
	PROFILE_AUTO(Camera);

	if (m_disableCameraFadeOut)
		return false;

	const I64 currFrame = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	if (currFrame < m_disableCameraFadeOutUntilFrame)
		return false;

	if (pNearestApproach)
		*pNearestApproach = kLargestFloatSqrt;

	// expand a bit when hidden so when we unhide small motions will not clip back into the camera
	const float surfaceExpand = GetSurfaceExpand();
	const float surfaceExpandBackpack = GetSurfaceExpandBackpack();

	RenderCamera renderCam = CameraManager::Get().GetUpdatedCameraInfo().GetRenderCamera();

	const bool photoModeActive = EngineComponents::GetNdPhotoModeManager() &&  EngineComponents::GetNdPhotoModeManager()->IsActive();
	if (!photoModeActive)
	{
		const Locator camLocWs = CameraManager::Get().GetNoManualLocator();
		renderCam.m_camLeft = GetLocalX(camLocWs.GetRotation());
		renderCam.m_camUp = GetLocalY(camLocWs.GetRotation());
		renderCam.m_camDir = GetLocalZ(camLocWs.GetRotation());
		renderCam.m_position = camLocWs.GetTranslation();
		renderCam.m_nearDist = CameraManager::Get().GetNoManualLocation().GetNearPlaneDist();
	}

	const float nearDist = renderCam.m_nearDist;

	const Vector planeNormalWs = renderCam.GetDir();
	const Point planeCenterWs = renderCam.m_position + planeNormalWs * nearDist;
	const Scalar kMaxIntersectingDist = SCALAR_LC(3.0f);

	const bool debugDraw = g_renderOptions.m_displayIsClippingCamera && DebugSelection::Get().IsProcessOrNoneSelected(this);

	// This test is a quick way to avoid all this, which happens if we're far away from the center
	if (DistSqr(GetTranslation(), planeCenterWs) > Sqr(kMaxIntersectingDist) && !debugDraw)
	{
		return false;
	}

	// to clip, the intersection point must be within a circle around the near plane
	Vec4 frustumPointArray[8];
	renderCam.GetFrustumPoints(frustumPointArray, Vector(kZero));
	const Scalar clipRadius = Dist(Point(kOrigin) + Vector(frustumPointArray[0]), planeCenterWs);

	if (debugDraw)
	{
		g_prim.Draw(DebugCircle(planeCenterWs, planeNormalWs, clipRadius, kColorBlue));
		MsgCon("surface-expand: %f, %f\n", surfaceExpand, surfaceExpandBackpack);
	}

	const CompositeBody* pCompositeBody = GetCompositeBody();
	if (pCompositeBody == nullptr)
	{
		return false;
	}

	const Quat cameraRot = renderCam.GetRotation();
	const float cameraWidth = Dist(Vector(frustumPointArray[0]), Vector(frustumPointArray[1]));
	const float cameraHeight = Dist(Vector(frustumPointArray[0]), Vector(frustumPointArray[2]));
	const Vec2 cameraWidthHeight = Vec2(cameraWidth, cameraHeight);

	return CharacterCollision::IsClippingCameraPlane(pCompositeBody,
											 		 useCollCastForBackpack,
													 surfaceExpand,
													 surfaceExpandBackpack,
													 planeCenterWs,
													 planeNormalWs,
													 cameraRot,
													 cameraWidthHeight,
													 clipRadius,
													 pNearestApproach,
													 debugDraw);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::IsBlockingCamera() const
{
	const CameraControl* pCurrentCamera = CameraManager::Get().GetCurrentCamera();
	if (pCurrentCamera && pCurrentCamera->IsKindOf(g_type_CameraControlStrafeBase))
	{
		const CameraControlStrafeBase* pStrafeCam = static_cast<const CameraControlStrafeBase*>(pCurrentCamera);
		return pStrafeCam->IsCharacterBlockingCamera(this);
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::OverrideCameraSurfaceExpand(F32 surfaceExpand)
{
	m_overridenSurfaceExpand = surfaceExpand;
	m_overridenSurfaceExpandFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool Character::IsHiddenInStealthVegetation() const
{
	DC::StealthVegetationHeight minRequiredHeight = DC::kStealthVegetationHeightStand;
	if (IsDead())
		minRequiredHeight = DC::kStealthVegetationHeightProne;
	else if (IsCrouched())
		minRequiredHeight = DC::kStealthVegetationHeightCrouch;
	else if (IsProne())
		minRequiredHeight = DC::kStealthVegetationHeightProne;

	DC::StealthVegetationHeight currentHeight = GetStealthVegetationHeight();
	if (currentHeight == DC::kStealthVegetationHeightInvalid)
		currentHeight = DC::kStealthVegetationHeightCrouch;

	return currentHeight >= minRequiredHeight;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::UpdateTimedHighlight()
{
	if (m_timedHighlight.m_endTime != TimeFrameNegInfinity())
	{
		const F32 dt = GetProcessDeltaTime();

		if (GetCurTime() < m_timedHighlight.m_endTime)
		{
			m_timedHighlight.m_intensity += m_timedHighlight.m_fadeInRate * dt;
			m_timedHighlight.m_intensity = Min(1.0f, m_timedHighlight.m_intensity);
		}
		else
		{
			m_timedHighlight.m_intensity -= m_timedHighlight.m_fadeOutRate * dt;
			m_timedHighlight.m_intensity = Max(0.0f, m_timedHighlight.m_intensity);
		}

		if (m_timedHighlight.m_intensity > 0.0f)
		{
			Color color = m_timedHighlight.m_color;
			color.SetA(m_timedHighlight.m_intensity);
			Highlight(color, m_timedHighlight.m_style);
		}
	}

}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::SetFgInstanceFlag(FgInstance::FgInstanceFlags flag)
{
	ParentClass::SetFgInstanceFlag(flag);

	NdPropControl* pPropCtrl = GetNdPropControl();
	if (pPropCtrl)
	{
		for (int iProp = 0; pPropCtrl && iProp < pPropCtrl->GetNumProps(); iProp++)
		{
			NdAttachableObject* pProp = pPropCtrl->GetPropObject(iProp);
			if (nullptr != pProp)
			{
				IDrawControl* propDrawCtrl = pProp->GetDrawControl();
				if (!propDrawCtrl)
				{
					continue;
				}

				propDrawCtrl->OrInstanceFlag(flag);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::ClearFgInstanceFlag(FgInstance::FgInstanceFlags flag)
{
	ParentClass::ClearFgInstanceFlag(flag);

	NdPropControl* pPropCtrl = GetNdPropControl();
	if (pPropCtrl)
	{
		for (int iProp = 0; pPropCtrl && iProp < pPropCtrl->GetNumProps(); iProp++)
		{
			NdAttachableObject* pProp = pPropCtrl->GetPropObject(iProp);
			if (nullptr != pProp)
			{
				IDrawControl* propDrawCtrl = pProp->GetDrawControl();
				if (!propDrawCtrl)
				{
					continue;
				}

				propDrawCtrl->ClearInstanceFlag(flag);
			}
		}
	}
}

void Character::PhotoModeSetHidden(bool hidden)
{
	if (m_pDrawControl)
	{
		g_renderOptions.m_hiddenCharacterTweaking = false;

		if (hidden && !m_pDrawControl->IsObjectHidden())
		{
			m_pDrawControl->HideObject();
			m_hiddenByPhotoMode = true;
			g_renderOptions.m_hiddenCharacterTweaking = true;

			// Remember this so that we can restore the flashlight state properly when it is not hidden by photo mode.
			const BoxedValue evtResult = SendEvent(SID("is-flashlight-on?"), this);
			m_photoModeFlashlight = evtResult.IsValid() && evtResult.GetBool();

			if (m_photoModeFlashlight)
			{
				SendEvent(SID("photomode-suppress-flashlight"), this);
			}
		}
		else if (!hidden && m_hiddenByPhotoMode)
		{
			m_pDrawControl->ShowObject();
			m_hiddenByPhotoMode = false;
			g_renderOptions.m_hiddenCharacterTweaking = true;

			if (m_photoModeFlashlight)
			{
				SendEvent(SID("photomode-suppress-off-flashlight"), this);
			}
		}
	}
}

bool Character::HasGasMask() const
{
	if (const NdAttachableObject* pGasMask = m_hGasMask.ToProcess())
	{
		if (pGasMask->IsAttached() && pGasMask->GetParentGameObject() == this
			&& (pGasMask->GetParentAttachName() == SID("targJaw")
				|| pGasMask->GetParentAttachName() == SID("targGasMask")
				|| pGasMask->GetParentAttachName() == SID("targEllieMask")
				|| pGasMask->GetParentAttachName() == SID("targAbbyMask")))
		{
			return true;
		}
	}

	StringId64 lookId = GetLookId();
	if (lookId == INVALID_STRING_ID_64)
		return false;

	struct Context
	{
		bool gasMask;
		const NdGameObject* pGo;
	};

	Context iterateContext;
	iterateContext.gasMask = false;
	iterateContext.pGo = this;

	LookPointer pObjectLook = GetLook(lookId);
	IterateLookMeshes(pObjectLook, [](LookPointer pLook, const DC::Look2Mesh* pDcMesh, void* pContext)
	{
		Context* pRealContext = static_cast<Context*>(pContext);

		if (pDcMesh->m_gasMask)
		{
			if (!pRealContext->pGo->GetDrawControl()->IsMeshHidden(pDcMesh->m_assetName.m_symbol))
				pRealContext->gasMask = true;
			return false;
		}

		return true;
	},
		&iterateContext);

	return iterateContext.gasMask;
}

StringId64 Character::SpawnGasMask(StringId64 artGroupId, StringId64 attachPointId, const Locator& offset)
{
	NdAttachableInfo attachInfo;
	attachInfo.m_artGroupId		  = artGroupId;
	attachInfo.m_hParentProcessGo = this;
	attachInfo.m_parentAttach	  = attachPointId;
	attachInfo.m_attachJoint	  = SID("root");
	attachInfo.m_attachOffset	  = offset;

	attachInfo.m_useShaderInstanceParams = true;
	NdAttachableObject* pGasMask = CreateAttachable(attachInfo, SID("NdAttachableObject"));

	if (pGasMask == nullptr)
		return INVALID_STRING_ID_64;

	SetGasMask(pGasMask);

	return pGasMask->GetUserId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
TimeFrame Character::GetTimeSinceCreation() const
{
	return GetCurTime() - m_spawnTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* BodyPartIndexToString(I32 bodyPart)
{
	return DC::GetLook2BodyPartName(bodyPart);
}

void Character::SetTrackedRenderTargetParticleHandle(const ParticleHandle *pHPart, const DC::PartBloodSaveType type)
{
	if (m_pCharacterEffectTracker)
	{
		m_pCharacterEffectTracker->SetParticleHandle(this, *pHPart, type);
	}
}

void Character::CopyEffectHistory(Character *pTargetChar) const
{
	CharacterEffectTracker *pTarget = pTargetChar->m_pCharacterEffectTracker;
	if (m_pCharacterEffectTracker && pTarget)
	{
		*pTarget = *m_pCharacterEffectTracker;
	}
}

void Character::RestoreRenderTargetsFromHistory()
{
	if (m_pCharacterEffectTracker)
	{
		m_pCharacterEffectTracker->RestoreBloodOnCharacter(this);
	}
}

void Character::ReleaseRenderTargets()
{
	if (m_pCharacterEffectTracker)
	{
		m_pCharacterEffectTracker->ReleaseRenderTargetsOnCharacter(this);
	}
}

bool Character::AllowRenderTargetEffects() const
{
	if (m_pCharacterEffectTracker)
	{
		return m_pCharacterEffectTracker->IsActive();
	}

	return false;
}

bool Character::AllowAttachedEffects() const
{
	if (m_pCharacterEffectTracker)
	{
		return m_pCharacterEffectTracker->IsActive();
	}

	return false;
}

FactionId Character::GetNoiseSourceFactionId() const
{
	if (IsDead() && GetTimeSinceDeath().ToSeconds() < 2.0f)
	{
		if (Character* pKiller = CharacterManager::GetManager().FindCharacterByUserId(GetKillerId()))
		{
			return pKiller->GetFactionId();
		}
	}
	return GetFactionId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void Character::Active::Update()
{
	ParentClass::Update();

	if (NdSubsystemMgr* pSsMgr = Self().GetSubsystemMgr())
	{
		pSsMgr->Update();
	}

	Character& pSelf = Self();

	if (pSelf.IsCrouched())
		pSelf.SetLastCrouchedTime(pSelf.GetCurTime());

	if (!pSelf.IsAimingFirearm())
		pSelf.m_lastNotAimingTime = pSelf.GetCurTime();


	if (pSelf.m_pHeartRateMonitor)
	{
		pSelf.m_pHeartRateMonitor->Update(&pSelf);

		if (FactDictionary* pFacts = pSelf.GetFactDict())
		{
			StringId64 heartRateStateId = INVALID_STRING_ID_64;
			if (const DC::HeartRateState* pHrState = pSelf.m_pHeartRateMonitor->GetCurrentHeartRateState())
			{
				heartRateStateId = pHrState->m_name;
			}
			if (pSelf.IsPlayer())
			{
				PostEvent(SID("set-fact-sid"), pSelf, SID("heart-rate-state"), heartRateStateId);
			}
			else
			{
				pFacts->Set(SID("heart-rate-state"), heartRateStateId);
			}
		}
	}

	if (IHealthSystem* pHealthSystem = pSelf.GetHealthSystem())
	{
		NdVoxControllerBase& rVoxController = pSelf.GetVoxController();
		rVoxController.SetCcLocalVariable(s_uCcHealthPct,
										  pHealthSystem->GetHealthPercentage(),
										  ARM_LOCAL_VARIABLE_FLAG_IMMEDIATE_ON_START);
	}
	if (pSelf.m_pCharacterEffectTracker)
	{
		pSelf.m_pCharacterEffectTracker->Update(&pSelf);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
PROCESS_SNAPSHOT_DEFINE(CharacterSnapshot, NdGameObjectSnapshot);

STATE_REGISTER(Character, Active, kPriorityNormal);

