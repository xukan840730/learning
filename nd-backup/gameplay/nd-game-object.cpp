/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nd-game-object.h"

#include "corelib/containers/list-array.h"
#include "corelib/containers/simple-array.h"
#include "corelib/math/eulerangles.h"
#include "corelib/math/matrix3x4.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/fast-find.h"
#include "corelib/util/strcmp.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-layer.h"
#include "ndlib/anim/anim-mgr.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-overlay.h"
#include "ndlib/anim/anim-simple-instance.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-snapshot-node-animation.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/anim-util.h"
#include "ndlib/anim/attach-system.h"
#include "ndlib/anim/debug-anim-channels.h"
#include "ndlib/anim/effect-anim-entry-tag.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/anim/mesh-table.h"
#include "ndlib/anim/nd-anim-plugins.h"
#include "ndlib/anim/nd-anim-structs.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/anim/snapshot-node-heap.h"
#include "ndlib/camera/camera-final.h"
#include "ndlib/debug/gameplay-assert.h"
#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/frame-params.h"
#include "ndlib/fx/fxmgr.h"
#include "ndlib/io/package-mgr.h"
#include "ndlib/io/package.h"
#include "ndlib/memory/relocatable-heap.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/ndphys/rigid-body-base.h"
#include "ndlib/net/nd-net-controller.h"
#include "ndlib/net/nd-net-game-manager.h"
#include "ndlib/net/nd-net-info.h"
#include "ndlib/net/nd-net-lod-controller.h"
#include "ndlib/net/nd-net-lod-manager.h"
#include "ndlib/netbridge/command.h"
#include "ndlib/netbridge/mail.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/process-error.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/process/spawn-info.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/background.h"
#include "ndlib/render/cubemap-set.h"
#include "ndlib/render/dev/render-options.h"
#include "ndlib/render/draw-control.h"
#include "ndlib/render/interface/fg-geometry.h"
#include "ndlib/render/interface/fg-instance.h"
#include "ndlib/render/interface/material-table.h"
#include "ndlib/render/interface/texture-mgr.h"
#include "ndlib/render/lighting/probetree-weight-control.h"
#include "ndlib/render/listen-mode.h"
#include "ndlib/render/look.h"
#include "ndlib/render/ngen/material.h"
#include "ndlib/render/ngen/mesh.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/render/scene.h"
#include "ndlib/render/shading/shader-parameters.h"
#include "ndlib/render/texture/instance-texture-table.h"
#include "ndlib/render/texture/texture-ids.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/render/util/screen-space-text-printer.h"
#include "ndlib/render/ngen/surfaces.h"
#include "ndlib/render/util/text-printer.h"
#include "ndlib/script/script-material-param.h"
#include "ndlib/scriptx/h/anim-overlay-defines.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/scriptx/h/dc-types.h"
#include "ndlib/scriptx/h/material-param-defines.h"
#include "ndlib/scriptx/h/surface-defines.h"
#include "ndlib/text/stringid-util.h"
#include "ndlib/util/jaf-anim-recorder-manager.h"

#include "gamelib/actor-viewer/nd-actor-viewer-object.h"
#include "gamelib/actor-viewer/nd-actor-viewer.h"
#include "gamelib/anim/blinking.h"
#include "gamelib/anim/gesture-controller.h"
#include "gamelib/anim/subsystem-ik-node.h"
#include "gamelib/audio/arm.h"
#include "gamelib/audio/armtypes.h"
#include "gamelib/audio/nd-foliage-sfx-controller-base.h"
#include "gamelib/audio/nd-vox-controller-base.h"
#include "gamelib/audio/sfx-process.h"
#include "gamelib/audio/virtual-bank.h"
#include "gamelib/cinematic/cinematic-manager.h"
#include "gamelib/facts/fact-manager.h"
#include "gamelib/feature/feature-db-ref.h"
#include "gamelib/feature/feature-db.h"
#include "gamelib/fx/splashers.h"
#include "gamelib/gameplay/bg-attacher.h"
#include "gamelib/gameplay/joint-sfx-helper.h"
#include "gamelib/gameplay/joint-wind-mgr.h"
#include "gamelib/gameplay/nav/action-pack-mgr.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/cover-action-pack.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-blocker-mgr.h"
#include "gamelib/gameplay/nav/nav-blocker.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/gameplay/nav/perch-action-pack.h"
#include "gamelib/gameplay/nav/platform-control.h"
#include "gamelib/gameplay/nav/simple-grid-hash-mgr.h"
#include "gamelib/gameplay/nd-attachment-ctrl.h"
#include "gamelib/gameplay/nd-attack-handler.h"
#include "gamelib/gameplay/nd-effect-control.h"
#include "gamelib/gameplay/nd-interactable.h"
#include "gamelib/gameplay/nd-interactables-mgr.h"
#include "gamelib/gameplay/nd-subsystem-mgr.h"
#include "gamelib/gameplay/nd-subsystem.h"
#include "gamelib/gameplay/spline-tracker.h"
#include "gamelib/gameplay/targetable.h"
#include "gamelib/io/rumblemanager.h"
#include "gamelib/level/art-group-table.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/entitydb.h"
#include "gamelib/level/level-items.h"
#include "gamelib/level/level.h"
#include "gamelib/ndphys/cloth-prototype.h"
#include "gamelib/ndphys/composite-body.h"
#include "gamelib/ndphys/havok.h"
#include "gamelib/ndphys/phys-fx.h"
#include "gamelib/ndphys/rigid-body.h"
#include "gamelib/region/region-control.h"
#include "gamelib/region/region-manager.h"
#include "gamelib/render/particle/particle-rt-mgr.h"
#include "gamelib/scriptx/h/ambient-occluder-defines.h"
#include "gamelib/scriptx/h/characters-collision-settings-defines.h"
#include "gamelib/scriptx/h/custom-attach-points-defines.h"
#include "gamelib/scriptx/h/joint-shader-driver-defines.h"
#include "gamelib/scriptx/h/look2-defines.h"
#include "gamelib/scriptx/h/mesh-audio-config-defines.h"
#include "gamelib/scriptx/h/nd-gesture-defines.h"
#include "gamelib/scriptx/h/nd-state-script-defines.h"
#include "gamelib/scriptx/h/phys-fx-defines.h"
#include "gamelib/scriptx/h/shimmer-defines.h"
#include "gamelib/scriptx/h/splasher-defines.h"
#include "gamelib/spline/catmull-rom.h"
#include "gamelib/state-script/ss-animate.h"
#include "gamelib/state-script/ss-instance.h"
#include "gamelib/state-script/ss-manager.h"
#include "gamelib/state-script/state-script.h"
#include "gamelib/tasks/task-graph-mgr.h"
#include "gamelib/tasks/task-subnode.h"

#include <Eigen/Dense>

/// --------------------------------------------------------------------------------------------------------------- ///
bool g_dontSpawnCrowd = false;
bool g_updateSpecCubemapEveryFrame = false;
bool g_scriptUpdateSpecCubemapEveryFrame = false;

const F32 NdGameObject::kInvalidSplineNotifyDistance = NDI_FLT_MAX;
static const U32F kSwappableDataSize = 8 * 1024;
I32 g_forceInstanceAo = -1;

static NdAtomic64 g_globalLookRandomizer(0);

extern float g_alphaMult[kMaxScreens];

#if 0 // keep this off for best performance; turn on to see more details
	#define PROFILE_DETAILED(A, B)	PROFILE(A, B)
#else
	#define PROFILE_DETAILED(A, B)
#endif

#define ENABLE_ND_GAME_OBJECT_IDENTITY_CHECK 0 // set to 1 to enable the identity skinning matrix check (expensive)

ProcessHandle NdGameObject::sm_deferredMaterialSwapObjects[NdGameObject::kMaxNumDeferredMaterialSwaps] = {};
U32 NdGameObject::sm_numDeferredMaterialSwaps = 0;

NdGameObject::DebugDrawFilter g_gameObjectDrawFilter;

NdGameObjectHandle NdGameObject::s_lookAtObjects[kMaxLookAtObjects];
U32 NdGameObject::s_lookAtObjectCount = 0;
NdAtomic64 NdGameObject::s_lookAtObjectLock(0);
ndjob::CounterHandle NdGameObject::s_pSplasherJobCounter;

static CONST_EXPR size_t kDebugChannelArraySize = 32;

CONST_EXPR StringId64 kHcmFilterModule = SID("high-contrast-mode");

#ifndef FINAL_BUILD
static bool s_sentHcmFilterEmail = false;
#endif

PGO_BOOL(g_bJobfyFindNdGameObjects, true);

/// --------------------------------------------------------------------------------------------------------------- ///
static bool AreSkinningMatricesIdentity(const Vec4* aMtx, U32F count)
{
	static const Vec4 kIdentRow0 = VEC4_LC(1.0f, 0.0f, 0.0f, 0.0f);
	static const Vec4 kIdentRow1 = VEC4_LC(0.0f, 1.0f, 0.0f, 0.0f);
	static const Vec4 kIdentRow2 = VEC4_LC(0.0f, 0.0f, 1.0f, 0.0f);
	static const Scalar kTolerance = SCALAR_LC(0.000001f);

	for (U32F i = 0; i < count; ++i)
	{
		const Vec4* pMtx = &aMtx[i * 3];

		if (!IsClose(pMtx[0], kIdentRow0, kTolerance)
		||  !IsClose(pMtx[1], kIdentRow1, kTolerance)
		||  !IsClose(pMtx[2], kIdentRow2, kTolerance))
		{
			return false;
		}
	}
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
PROCESS_REGISTER_ABSTRACT(NdGameObject, NdLocatableObject);  // abstract types can't be spawned

FROM_PROCESS_DEFINE(NdGameObject);

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject::NdGameObject()
	: m_pDrawControl(nullptr)
	, m_pAnimControl(nullptr)
	, m_pAnimData(nullptr)
	, m_pEffectControl(nullptr)
	, m_pAttachSystem(nullptr)
	, m_pTargetable(nullptr)
	, m_pSplasherController(nullptr)
	, m_splasherSkeletonId(INVALID_STRING_ID_64)
	, m_splasherSetlistId(INVALID_STRING_ID_64)
	, m_pScriptInst(nullptr)
	, m_aChildScriptEventId(nullptr)
	, m_childScriptEventCapacity(0)
	, m_childScriptEventCount(0)
	, m_playingIgcName(INVALID_STRING_ID_64)
	, m_playingIgcAnim(INVALID_STRING_ID_64)
	, m_playingIgcDurationSec(0.0f)
	, m_playingIgcStartTimeSec(0.0f)
	, m_havokSystemId(0)
	, m_pCompositeBody(nullptr)
	, m_prevPosWs(kInvalidPoint)
	, m_prevPosPs(kInvalidPoint)
	, m_velocityWs(kZero)
	, m_velocityPs(kZero)
	, m_prevCheckPointPosWs(kInvalidPoint)
	, m_angularVecWs(kZero)
	, m_prevRegionControlPos(kInvalidPoint)
	, m_pDynNavBlocker(nullptr)
	, m_dynNavBlockerJointIndex(-1)
	, m_pStaticNavBlocker(nullptr)
	, m_pPlatformControl(nullptr)
	, m_pSplineTracker(nullptr)
	, m_pRegionControl(nullptr)
	, m_pAttackHandler(nullptr)
	, m_pNetController(nullptr)
#ifdef HEADLESS_BUILD
	, m_pNetLodController(nullptr)
#endif
	, m_pFoliageSfxController(nullptr)
	, m_paSoundBankHandles(nullptr)
	, m_gasMaskFutz(INVALID_STRING_ID_64)
	, m_bOnIsValidBindTargetCalled(false)
	, m_bDisableFeatureDbs(false)
	, m_bEnableFeatureDbsForAnyLayer(false)
	, m_bAllocatedHavokSystemId(false)
	, m_bFreezeBindingWhenNonPhysical(false)
	, m_wantNavBlocker(true)
	, m_bUpdateCoverActionPacks(false)
	, m_feetSplashersInWater(false)
	, m_feetWet(false)
	, m_spawnShaderInstanceParamsSet(false)
	, m_hasJointWind(false)
	, m_hiddenByPhotoMode(false)
	, m_ignoredByNpcs(false)
	, m_highContrastModeOverridden(false)
	, m_lastIgnoredByNpcsTime(kTimeFrameNegInfinity)
	, m_pProbeTreeWeightControl(nullptr)
	, m_pBgAttacher(nullptr)
	, m_pJointOverrideData(nullptr)
	, m_pJointOverridePluginCmd(nullptr)
	, m_pMaterialSwapMapToApply(nullptr)
	, m_pDebugInfo(nullptr)
	, m_windMgrIndex(-1)
	, m_skeletonId(INVALID_SKELETON_ID)
	, m_pJointShaderDriverList()
	, m_resolvedLookId(INVALID_STRING_ID_64)
	, m_userIdUsedForLook(INVALID_STRING_ID_64)
	, m_resolvedAmbientOccludersId(INVALID_STRING_ID_64)
	, m_gridHashId(SimpleGridHashId::kInvalid)
	, m_lookCollectionRandomizer(kUnspecifiedRandomizer)
	, m_lookTintRandomizer(kUnspecifiedRandomizer)
	, m_cubemapIndex(-1)
	, m_cubemapBgIndex(-1)
	, m_cubemapBgNameId(INVALID_STRING_ID_64)
	, m_pSpecularIblProbeParameters(nullptr)
	, m_pInteractCtrl(nullptr)
	, m_pAttachmentCtrl(nullptr)
	, m_aInstanceMaterialRemaps(nullptr)
	, m_numInstanceMaterialRemaps(0)
	, m_numInstanceTextureTables(1)
{
	SetIsGameObject(true);

	for (int i = 0; i < kMaxInstanceTextureTables; ++i)
	{
		m_pInstanceTextureTable[i] = nullptr;
	}
	m_goFlags.m_rawBits = 0;
	SetFactionId(FactionId(0)); // default to faction 0 (games should order their factions so that the 0th is a reasonable default)

	m_wetMaskIndexes[0] = FxMgr::kInvalid;
	m_wetMaskIndexes[1] = FxMgr::kInvalid;
	m_wetMaskIndexes[2] = FxMgr::kInvalid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NdGameObject::GetMaxStateAllocSize()
{
	return sizeof(Process::State);
}


/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject::~NdGameObject()
{
	if (m_pInteractCtrl)
	{
		NDI_DELETE m_pInteractCtrl;
		m_pInteractCtrl = nullptr;
	}

	BlockNavPoly(false);

	// We need to destroy collision before draw control because collision art item ref counting depends on draw meshes
	if (m_pCompositeBody)
	{
		delete m_pCompositeBody;
		m_pCompositeBody = nullptr;
	}
	HavokFreeSystemId(m_havokSystemId);

	if (m_pDrawControl)
	{
		m_pDrawControl->Shutdown();
		m_pDrawControl = nullptr;
	}

	if (m_pAnimControl)
	{
		m_pAnimControl->Shutdown();
		m_pAnimControl = nullptr;
	}

	if (m_pAnimData)
	{
		ResourceTable::DecrementRefCount(m_pAnimData->m_animateSkelHandle.ToArtItem());
		ResourceTable::DecrementRefCount(m_pAnimData->m_curSkelHandle.ToArtItem());
		EngineComponents::GetAnimMgr()->FreeAnimData(m_pAnimData);
		m_pAnimData = nullptr;
	}

	if (m_pPlatformControl)
	{
		m_pPlatformControl->Destroy(this);
		m_pPlatformControl = nullptr;
	}

	NavBlockerMgr& nbMgr = NavBlockerMgr::Get();

	if (m_pDynNavBlocker)
	{
		nbMgr.FreeDynamic(m_pDynNavBlocker);
		m_pDynNavBlocker = nullptr;
	}

	if (m_pStaticNavBlocker)
	{
		nbMgr.FreeStatic(m_pStaticNavBlocker);
		m_pStaticNavBlocker = nullptr;
	}

	if (m_pRegionControl)
	{
		m_pRegionControl->Shutdown(this);
		m_pRegionControl = nullptr;
	}

	FreeProbeTreeWeightControls();

	if (m_paSoundBankHandles != nullptr)
	{
		for (int i = 0; i < ARM_NUM_LOOK_SOUND_BANKS; i++)
		{
			if (m_paSoundBankHandles[i].Valid())
			{
				EngineComponents::GetAudioManager()->UnloadSoundBank(m_paSoundBankHandles[i]);
				m_paSoundBankHandles[i] = VbHandle();
			}
		}
	}

	if (m_pSplasherController)
	{
		NDI_DELETE m_pSplasherController;
		m_pSplasherController = nullptr;
	}

	DestroyTextureInstanceTable();
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Snapshot
/// --------------------------------------------------------------------------------------------------------------- ///

PROCESS_SNAPSHOT_DEFINE(NdGameObjectSnapshot, NdLocatableSnapshot);

/// --------------------------------------------------------------------------------------------------------------- ///
ProcessSnapshot* NdGameObject::AllocateSnapshot() const
{
#if REDUCED_SNAPSHOTS
	if (m_pTargetable || m_goFlags.m_isPoiLookAt)
		return NdGameObjectSnapshot::Create(this);
	return nullptr;
#else
	return NdGameObjectSnapshot::Create(this);
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdGameObjectSnapshot::Init(const Process* pOwner)
{
	if (const NdGameObject* pGoOwner = NdGameObject::FromProcess(pOwner))
	{
		m_skelNameId = pGoOwner->GetSkelNameId();
		m_pAnimData = pGoOwner->GetAnimData();
		m_lookId = pGoOwner->GetResolvedLookId();
		m_pNetPlayerTracker = pGoOwner->GetNetOwnerTracker();
		m_hasTargetable = pGoOwner->GetTargetable() != nullptr;
		m_feetWet = pGoOwner->AreFeetWet();
	}

	return ParentClass::Init(pOwner);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::RefreshSnapshot(ProcessSnapshot* pSnapshot) const
{
	//PROFILE(Processes, NdGo_RefreshSnapshot);

	ALWAYS_ASSERT(pSnapshot);

	NdGameObjectSnapshot* pNdGameObjectSnapshot = NdGameObjectSnapshot::FromSnapshot(pSnapshot);

	pNdGameObjectSnapshot->m_factionId = GetFactionId();
	if (m_pAnimData)
		pNdGameObjectSnapshot->m_boundingSphere = GetBoundingSphere();
	else
		pNdGameObjectSnapshot->m_boundingSphere = Sphere(GetTranslation(), 1.0f);

	pNdGameObjectSnapshot->m_pDynNavBlocker = GetNavBlocker();
	pNdGameObjectSnapshot->m_velocityWs = GetVelocityWs();
	pNdGameObjectSnapshot->m_velocityPs = GetVelocityPs();
	pNdGameObjectSnapshot->m_goFlags = m_goFlags;
	pNdGameObjectSnapshot->m_ignoredByNpcs = IgnoredByNpcs();
	pNdGameObjectSnapshot->m_minPoiCollectRadius = GetMinLookAtCollectRadius();
	pNdGameObjectSnapshot->m_maxPoiCollectRadius = GetMaxLookAtCollectRadius();
	pNdGameObjectSnapshot->m_poiCollectType = m_poiCollectType;
	pNdGameObjectSnapshot->m_hasAnimControl = (GetAnimControl() != nullptr);
	pNdGameObjectSnapshot->m_feetWet = AreFeetWet();

	ParentClass::RefreshSnapshot(pSnapshot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdGameObject::Init(const ProcessSpawnInfo& spawnInfo)
{
	PROFILE(Processes, NdGameObject_Init);

	const SpawnInfo& spawn = static_cast<const SpawnInfo&>(spawnInfo);

	m_resolvedLookId = INVALID_STRING_ID_64;

	const bool useGlobalLookRandomizer = spawn.GetData<bool>(SID("global-look-randomizer"), false);

	if (useGlobalLookRandomizer)
	{
		m_lookCollectionRandomizer = 0;
	}
	else if (m_lookCollectionRandomizer == kUnspecifiedRandomizer)
	{
		m_lookCollectionRandomizer = (U32)spawn.GetData<I32>(SID("look-collection-randomizer"), 0);
	}

	if (m_lookTintRandomizer == kUnspecifiedRandomizer)
	{
		m_lookTintRandomizer = (U32)spawn.GetData<I32>(SID("look-randomizer"), 0);
	}
	//m_skeletonId = INVALID_SKELETON_ID; // IMPORTANT: do NOT initialize this here... derived classes can override its value prior to calling the parent class' implementation of Init()

	m_firstRequestSoundTime = TimeFramePosInfinity();
	m_lastDestructionFxSoundTime = TimeFrameNegInfinity();
	m_numDestructionFxSoundsThisFrame = 0;

	m_ignoredByNpcs = false;
	m_lastIgnoredByNpcsTime = kTimeFrameNegInfinity;

	for (int kk = 0; kk < kMeshRaycastCount; kk++)
		m_groundSurfaceTypeKickedFN[kk] = -1;

	m_aChildScriptEventId = nullptr;
	m_childScriptEventCount = 0;
	m_childScriptEventCapacity = GetChildScriptEventCapacity();
	if (m_childScriptEventCapacity != 0)
	{
		const int rawBytes = sizeof(StringId64) * m_childScriptEventCapacity;
		const int requiredBytes = GetFastFind_RequiredSizeInBytes(rawBytes);
		const int requiredCapacity = requiredBytes / sizeof(StringId64);
		ASSERT(requiredCapacity >= m_childScriptEventCapacity);
		m_childScriptEventCapacity = requiredCapacity;

		m_aChildScriptEventId = NDI_NEW(kAlignCacheline) StringId64[m_childScriptEventCapacity];
		GAMEPLAY_ASSERT(m_aChildScriptEventId);

		memset(m_aChildScriptEventId, 0, requiredBytes);
	}

	m_pMaterialSwapMapToApply = nullptr;

	m_pDebugInfo                    = nullptr;
	m_aResolvedLookTint				= nullptr;
	m_pScriptInst                   = nullptr;
	m_bOnIsValidBindTargetCalled    = false;
	m_prevPosWs                     = kInvalidPoint;
	m_prevPosPs                     = kInvalidPoint;
	m_velocityWs                    = kZero;
	m_velocityPs                    = kZero;
	m_prevCheckPointPosWs           = kInvalidPoint;
	m_angularVecWs                  = kZero;
	m_prevRegionControlPos          = kInvalidPoint;
	m_bFreezeBindingWhenNonPhysical = spawn.GetData<bool>(SID("freeze-binding-when-non-physical"), false);
	m_goFlags.m_replyAimingAtFriend = spawn.GetData<bool>(SID("reply-aiming-at-friend"), false);
	m_goFlags.m_useLargeEffectList  = spawn.GetData<bool>(SID("use-large-effect-list"), m_goFlags.m_useLargeEffectList);
	m_goFlags.m_playerCanTarget     = spawn.GetData<bool>(SID("allow-player-to-target"), true);
	m_goFlags.m_neverBreakArrow     = spawn.GetData<bool>(SID("never-break-arrow"), false);
	m_prevLocator                   = Locator(Point(FLT_MAX, FLT_MAX, FLT_MAX));

	m_goFlags.m_isPoiLookAt         = spawn.GetData<bool>(SID("look-at-poi"), false);
	m_minPoiCollectRadius           = spawn.GetData<float>(SID("min-poi-collect-radius"), 0.0f);
	m_maxPoiCollectRadius           = spawn.GetData<float>(SID("max-poi-collect-radius"), 15.0f);
	m_poiCollectType				= spawn.GetData<StringId64>(SID("poi-type"), INVALID_STRING_ID_64);
	m_minPoiCollectRadius           = Max(0.0f, m_minPoiCollectRadius);
	m_maxPoiCollectRadius           = Max(m_minPoiCollectRadius, m_maxPoiCollectRadius);

	m_feetSplashersInWater = false;
	m_spawnShaderInstanceParamsSet = false;

	m_initialAmbientScale = 1.0f;

	m_pSplasherController = nullptr;

	m_goFlags.m_disablePlayerParenting = spawn.GetData<bool>(SID("disable-player-parenting"), false);

	MarkSpawnTime(SID("ndgo-before-parent"));

	Err result = ParentClass::Init(spawn);
	if (result.Failed())
	{
		return result;
	}

	MarkSpawnTime(SID("ndgo-parent"));

	m_goFlags.m_usePlayersWetMask = spawn.GetData<bool>(SID("use-player-wet-mask"), false);

	m_pBgAttacher = nullptr;
	StringId64 levelId = spawn.GetData<StringId64>(SID("attach-background"), INVALID_STRING_ID_64);
	if (levelId != INVALID_STRING_ID_64)
	{
		m_pBgAttacher = NDI_NEW BgAttacher;
		m_pBgAttacher->Init(levelId, this);

		levelId = spawn.GetData<StringId64>(SID("attach-background-1"), INVALID_STRING_ID_64);
		int index = 1;
		while (levelId != INVALID_STRING_ID_64)
		{
			index++;
			m_pBgAttacher->AddLevel(levelId);
			StringId64 bgTag = StringId64ConcatInteger(SID("attach-background-"), index);
			levelId = spawn.GetData<StringId64>(bgTag, INVALID_STRING_ID_64);
		}
	}

	m_ssAtSplineDistanceAction.Clear();
	ClearSplineNotify();
	m_goFlags.m_bDisableCanHangFrom = false;  //@ NDLIB problem (minor)

	StringId64 userBareId = spawn.m_userBareId;
	StringId64 namespaceId = spawn.m_userNamespaceId;
	StringId64 userId = spawn.m_userId;
	if (spawn.m_userBareId == INVALID_STRING_ID_64)
	{
		userBareId = spawn.BareNameId(GetUserBareId());
		namespaceId = spawn.NamespaceId(GetUserNamespaceId());
		userId = spawn.NameId(GetUserId());

		if (spawn.m_pSpawner && spawn.m_pSpawner->GetFlags().m_multiSpawner)
		{
			spawn.m_pSpawner->GetDesiredProcessUserId(spawn.m_uniqueIndex, userBareId, namespaceId, userId);
		}
	}
	else if (spawn.m_userId == INVALID_STRING_ID_64)
	{
		userId = spawn.NameId(GetUserId());
	}

	SetUserId(userBareId, namespaceId, userId);
	SetUniqueIndex(spawn.m_uniqueIndex);

	m_soundPhysFxDelayTime = spawn.GetData(SID("delay-phys-fx-sound-time"), -1.0f);

	userId = GetUserId(); // in case derived classes want to mess with it

	if (useGlobalLookRandomizer)
	{
		m_userIdUsedForLook = StringId64(g_globalLookRandomizer.Add(1));
	}
	else
	{
		m_userIdUsedForLook = userId;
	}

	m_pAttachSystem = nullptr;

	IDrawControl* pDrawControl = nullptr;
	ResolvedLook resolvedLook;
	const ResolvedLook* pResolvedLook = spawn.m_pResolvedLook;
	const StringId64 desiredActorOrLookId = GetLookId();

	MarkSpawnTime(SID("ndgo-before-look"));

	if (desiredActorOrLookId != INVALID_STRING_ID_64)
	{
		//PROFILE(Processes, ResolveLook);

		// if a resolved look has not been provided already...
		if (pResolvedLook == nullptr)
		{
			ResolveLook(resolvedLook, spawn, desiredActorOrLookId);
			pResolvedLook = &resolvedLook;
		}

		ASSERT(pResolvedLook != nullptr);
		m_gameObjectConfig.m_drawControlMaxMeshes = Max(pResolvedLook->m_numMeshes, m_gameObjectConfig.m_drawControlMaxMeshes); // IMPORTANT: do this before AllocateAnimData() is called

		OnLookResolved(pResolvedLook);

		m_resolvedLookId = pResolvedLook->m_lookOrMeshId;

		if (m_skeletonId == INVALID_SKELETON_ID)
			m_skeletonId = pResolvedLook->m_skeletonId;
	}
	else
	{
		m_resolvedLookId = INVALID_STRING_ID_64;
		m_skeletonId = INVALID_SKELETON_ID;
	}

	MarkSpawnTime(SID("ndgo-look-resolve"));

	if (m_skeletonId != INVALID_SKELETON_ID)
	{
		PROFILE(Processes, InitAnimAndMesh);

		const ArtItemSkeletonHandle artItemSkelHandle = ResourceTable::LookupSkel(m_skeletonId);
		if (!artItemSkelHandle.ToArtItem())
		{
			MsgErr("Skeleton 0x%.8x not found!\n", m_skeletonId.GetValue());
			return Err::kErrSkeletonNotLoaded;
		}

		// HACK for now...
		if (g_dontSpawnCrowd
		&&  (  artItemSkelHandle.ToArtItem()->GetNameId() == SID("skel.npc-normal-crowd-base")
			|| artItemSkelHandle.ToArtItem()->GetNameId() == SID("skel.npc-normal-crowd-fem-base")))
		{
			return Err::kErrSpawningSuppressed;
		}

		{
			PROFILE(Processes, InitAnimData);

			m_pAnimData = AllocateAnimData(artItemSkelHandle);
			if (!m_pAnimData)
			{
				return Err::kErrOutOfMemory;
			}
			//ANIM_ASSERT(m_pAnimData);
		}

		{
			PROFILE(Processes, InitAnimControl);

			result = InitAnimControl(spawn);
			if (result.Failed())
			{
				return result;
			}
		}

		if (m_pSpawner)
		{
			SetScale(m_pSpawner->GetScale());
		}

		MarkSpawnTime(SID("ndgo-anim-control"));

		{
			PROFILE(Processes, InitDrawControl);

			m_pDrawControl = pDrawControl = NDI_NEW IDrawControl(m_pAnimData, m_gameObjectConfig.m_drawControlMaxMeshes);

			// Only allocate space for sound banks at process init, and then only if
			// necessary.  Note that if the draw control is reinitialized later post-init
			// with a different resolved look that has sound banks where the initial look
			// did not, then this array won't exist.  In this case the user MUST make sure
			// that the base look has some sound bank reference, to trigger the allocation
			// of the array of handles.  This saves memory by not allocating the bank handle
			// array in the many cases where game objects don't have banks in their looks.
			if (pResolvedLook->m_aSoundBanks[0] != nullptr)
			{
				m_paSoundBankHandles = NDI_NEW (kAlign4) VbHandle[ARM_NUM_LOOK_SOUND_BANKS];
				if (m_paSoundBankHandles != nullptr)
				{
					memset(m_paSoundBankHandles, 0, sizeof(VbHandle[ARM_NUM_LOOK_SOUND_BANKS]));
				}
			}

			result = InitializeDrawControl(spawn, *pResolvedLook);
			if (result.Failed())
			{
				GoError(result.GetMessageText());
				return result;
			}
		}

		MarkSpawnTime(SID("ndgo-draw-control"));

		{
			ResolvedBoundingData bounds;
			m_pDrawControl->DetermineBounds(bounds, m_resolvedLookId, m_skeletonId);

			result = ConfigureAnimData(m_pAnimData, bounds);
			if (result.Failed())
			{
				GoError(result.GetMessageText());
				return result;
			}
		}

		// Create attach points
		if (NeedsAttachSystem())
		{
			PROFILE(Processes, SetupAttachSystem);
			m_pAttachSystem = NDI_NEW AttachSystem;
			m_pAttachSystem->SetAnimData(m_pAnimData);
			SetupAttachSystem(spawn);
		}

		// Do we allow this object to not be rendered if outside of visible levels?
		const bool dontAllowHide = spawn.GetData<bool>(SID("DontAllowHide"), false);
		if (dontAllowHide)
		{
			m_pAnimData->m_flags |= FgAnimData::kDisableVisOcclusion;
		}

		if (pDrawControl)
		{
			if (spawn.GetData<bool>(SID("no-show-in-player-flashlight"), false))
			{
				pDrawControl->SetInstanceFlag(FgInstance::kNotShownInPlayerSpotLight);
			}

			PROFILE(Processes, InitAmbientOccluders);
			AddAmbientOccludersToDrawControl(pResolvedLook->m_ambientOccludersId, pResolvedLook->m_alternateAmbientOccludersId);
		}

		// Setup splasher system, after attach system is setup
		m_splasherSkeletonId = INVALID_STRING_ID_64;
		m_splasherSetlistId = INVALID_STRING_ID_64;

		if (SplashersEnabled(spawn))
		{
			PROFILE(Processes, InitSplashers);

			const StringId64 skelNameId = GetSkelNameId();
			const StringId64 lookNameId = m_resolvedLookId;

			const char* splasherSkel = spawn.GetData<String>(SID("splasher-skeleton"), String(nullptr)).GetString();
			const char* splasherSetlist = spawn.GetData<String>(SID("splasher-setlist"), String(nullptr)).GetString();
			StringId64 splasherSkelId = StringToStringId64(splasherSkel);
			StringId64 splasherSetlistId = StringToStringId64(splasherSetlist);

			StringId64 splasherInitDcName = SID("*splasher-setlist-init*");
			const DC::SplasherInitInfoArray* pSplasherInitArray = ScriptManager::LookupInModule<DC::SplasherInitInfoArray>(splasherInitDcName, SID("splashers"));
			if (pSplasherInitArray)
			{
				for (int i=0; i<pSplasherInitArray->m_count; i++)
				{
					StringId64 skelName = pSplasherInitArray->m_array[i].m_skeletonName;
					if (skelName == skelNameId || skelName == lookNameId)
					{
						if (pSplasherInitArray->m_array[i].m_taskName != INVALID_STRING_ID_64)
						{
							const GameTaskSubnode* pSubnode = g_taskGraphMgr.FindSubnode(pSplasherInitArray->m_array[i].m_taskName);
							if (pSubnode == nullptr || !pSubnode->IsActive())
								continue;
						}

						if (splasherSkelId == INVALID_STRING_ID_64)
							splasherSkelId = pSplasherInitArray->m_array[i].m_splasherSkel;
						if (splasherSetlistId == INVALID_STRING_ID_64)
							splasherSetlistId = pSplasherInitArray->m_array[i].m_splasherList;

						break;
					}
				}

				InitializeSplashers(splasherSkelId, splasherSetlistId);
			}
		}
	}
	else if (desiredActorOrLookId != INVALID_STRING_ID_64)
	{
		return Err::kErrSkeletonNotLoaded;
	}

	MarkSpawnTime(SID("ndgo-draw-control-finalize"));

	// create the spline tracker if necessary
	if (CanTrackSplines(spawn))
	{
		InitSplineTracker();
	}

	m_updateShimmer					= false;
	m_shimmerOffset					= 0.0f;
	m_shimmerIntensity				= 1.0f;
	m_pShimmerSettings				= ScriptPointer<DC::ShimmerSettings>(INVALID_STRING_ID_64);
	m_shimmerAlphaMultiplier		= 1.0f;
	m_shimmerIntensityBlendTime		= 0.0f;

	StringId64 shimmer = spawn.GetData<StringId64>(SID("shimmer-settings"), INVALID_STRING_ID_64);
	SetShimmer(shimmer, true);
	if (shimmer != INVALID_STRING_ID_64)
	{
		SetShimmerIntensity(spawn.GetData<F32>(SID("start-shimmer-intensity"), 1.0f));
	}
	else
	{
		SetShimmerIntensity(0.0f);
	}

	m_shimmerAlphaMultiplier = spawn.GetData<float>(SID("shimmer-alpha-mult"), m_shimmerAlphaMultiplier);


	// allocate instance optional parameters if necessary
	if (spawn.GetData<bool>(SID("use-shader-instance-params"), false))
	{
		AllocateShaderInstanceParams();
		SetSpawnShaderInstanceParams(spawn);
	}

	const Err icRes = CreateInteractControl(spawn);
	if (icRes.Failed())
	{
		return icRes;
	}

	bool isPlatform = false;
	const EntitySpawner* pSpawner = spawn.m_pSpawner;
	if (pSpawner != nullptr && pSpawner->GetFlags().m_hasChildren && !pSpawner->GetFlags().m_hasOnlyCameraChildren)
	{
		//Its valid to have a character parented to a box on a vehicle.
		const NdGameObject* pParentObject = pSpawner->GetParentNdGameObject();
		if ((!pParentObject || pParentObject->GetParentProcess() != EngineComponents::GetProcessMgr()->m_pPlatformTree)
			&& spawn.GetData<StringId64>(SID("DrawBucket"), SID("Unspecified")) != SID("DefaultForce"))
		{
			ChangeParentProcess(EngineComponents::GetProcessMgr()->m_pPlatformTree);
			isPlatform = true;
		}
	}

	// should the object spawn hidden?
	if (spawn.GetData<bool>(SID("start-hidden"), false) && pDrawControl)
	{
		pDrawControl->HideAllMesh(0);
		pDrawControl->HideAllMesh(1);
	}

	if (spawn.GetData<bool>(SID("no-decals"), false) && pDrawControl)
	{
		pDrawControl->SetInstanceFlag(FgInstance::kNoDecals);
	}

	if (spawn.GetData<bool>(SID("no-projection"), false) && pDrawControl)
	{
		pDrawControl->SetInstanceFlag(FgInstance::kNoProjection);
	}

	if (spawn.GetData<bool>(SID("no-features"), false))
	{
		DisableFeatures();
	}

	if (spawn.GetData<bool>(SID("features-on-any-layer"), false))
	{
		SetFeaturesEnabledForAnyLayer(true);
	}

	if (spawn.GetData<bool>(SID("no-hang-from"), false)) //@ NDLIB problem (minor)
	{
		m_goFlags.m_bDisableCanHangFrom = true;
	}

	if (spawn.GetData<bool>(SID("is-targetable"), false))
	{
		if (!GetTargetable())
			MakeTargetable();
	}

	// Create the effect control (EFF handler)
	if (NeedsEffectControl(spawn))
	{
		m_pEffectControl = EngineComponents::GetNdGameInfo()->CreateEffectControl();
		if (m_pEffectControl)
		{
			PROFILE(Processes, EnableEffectFiltering);

			bool isPlayer = IsKindOf(SID("Player"));
			if (isPlayer)
			{
				m_pEffectControl->SetPlayer(true);
			}

			bool enableEffectFiltering = EnableEffectFilteringByDefault();
			if (pSpawner)
			{
				PROFILE(Processes, EnableEffectFiltering2);
				enableEffectFiltering = pSpawner->GetData<bool>(SID("enable-effect-filter"), enableEffectFiltering);
			}

			if (enableEffectFiltering)
			{
				//			MsgAudio("Object '%s' Being spawned WITH effect filtering\n", GetName());
				m_pEffectControl->EnableEffectFiltering(SoundEffectFilteringMinTime());
			}
			else
			{
				//			MsgAudio("Object '%s' Being spawned WITHOUT effect filtering\n", GetName());
				m_pEffectControl->DisableEffectFiltering();
			}
		}
	}

	// Create the attack handler
	if (NeedsAttackHandler())
	{
		m_pAttackHandler = EngineComponents::GetNdGameInfo()->CreateAttackHandler(*this);
		ALWAYS_ASSERT(m_pAttackHandler);
		if (!m_pAttackHandler)
		{
			result = Err::kErrOutOfMemory;
			return result;
		}
	}

	MarkSpawnTime(SID("ndgo-before-tex-inst-table"));

	// Has to be done before net controller for emblems
	InitTextureInstanceTable();

	MarkSpawnTime(SID("ndgo-tex-inst-table"));

	// Create the net controller if necessary
	if (g_ndConfig.m_pNetInfo->IsNetActive())
	{
		if (NeedsNetController(spawn))
		{
			m_pNetController = g_ndConfig.m_pNetInfo->CreateNetController(*this);
			ALWAYS_ASSERT(m_pNetController);

			result = m_pNetController->Init(*this, spawn);
			if (result.Failed())
				return result;
		}

#ifdef HEADLESS_BUILD
		if (NeedsNetLodController(spawn))
		{
			m_pNetLodController = g_ndNetLodManager.AllocateController(this);

			if (!m_pNetLodController)
				return Err::kErrOutOfMemory;
		}
#endif
	}

	m_spawnTime = GetCurTime();

	m_charterRegionActorId = spawn.GetData<StringId64>(SID("apply-to-name"), INVALID_STRING_ID_64);

	if (m_charterRegionActorId != INVALID_STRING_ID_64)
	{
		SetRegionControl(NDI_NEW RegionControl(m_charterRegionActorId, true));
		m_goFlags.m_autoUpdateRegionControl = true;
		SetRegionControlUpdateEnabled(true);
	}

	const I32 maxJointOverrideCount = spawn.GetData<I32>(SID("max-joint-override-count"), 0);
	if (maxJointOverrideCount > 0)
	{
		// Enable joint override if no other plugins have been added yet.
		FgAnimData* pAnimData = GetAnimData();
		if (pAnimData && pAnimData->m_pPluginParams == nullptr)
		{
			const U32F numCustomPlugins = 1;
			pAnimData->m_pPluginParams = NDI_NEW (kAlignPtr) FgAnimData::PluginParams[numCustomPlugins + 1];

			pAnimData->m_pPluginParams->m_enabled = false;
			pAnimData->m_pPluginParams->m_pluginName = SID("joint-override");
			pAnimData->m_pPluginParams->m_pPluginData = nullptr;
			pAnimData->m_pPluginParams->m_pluginDataSize = 0;

			// Terminator
			pAnimData->m_pPluginParams[1].m_pluginName = INVALID_STRING_ID_64;

			m_pJointOverrideData = NDI_NEW (kAlign16) JointOverrideData;
			m_pJointOverrideData->m_numJoints = 0;
			m_pJointOverrideData->m_maxJoints = maxJointOverrideCount;
			m_pJointOverrideData->m_pJointIndex		= NDI_NEW (kAlign16) I16[maxJointOverrideCount];
			m_pJointOverrideData->m_pComponentFlags	= NDI_NEW (kAlign16) U8[maxJointOverrideCount];
			m_pJointOverrideData->m_pJointTrans		= NDI_NEW (kAlign16) Point[maxJointOverrideCount];
			m_pJointOverrideData->m_pJointRot		= NDI_NEW (kAlign16) Quat[maxJointOverrideCount];
			m_pJointOverrideData->m_pJointScale		= NDI_NEW (kAlign16) float[maxJointOverrideCount];

			m_pJointOverridePluginCmd = NDI_NEW (kAlign16) EvaluateJointOverridePluginData;

			// Disable all entries
			for (U32F i = 0; i < m_pJointOverrideData->m_maxJoints; ++i)
			{
				m_pJointOverrideData->m_pJointIndex[i] = -1;
			}
		}
	}

	// defaulted in the constructor to 0, if the subclass wants to set this before ParentClass::Init is called, we need to respect that. This reduces need for redundant work
	// in subclasses who need to know to allocate shader instance params if this is true
	if (!m_goFlags.m_isVisibleInListenMode)
	{
		m_goFlags.m_isVisibleInListenMode = spawn.GetData<bool>(SID("is-visible-in-listen-mode"), false);
	}

	m_goFlags.m_enableCameraParenting = spawn.GetData<bool>(SID("enable-camera-parenting"), false);
	m_goFlags.m_forceAllowNpcBinding = spawn.GetData<bool>(SID("force-allow-npc-binding"), false);

	if (m_goFlags.m_isVisibleInListenMode)
	{
		AllocateShaderInstanceParams();
	}

	// Setup the mesh audio config.
	SetupMeshAudioConfigForLook(*pResolvedLook);

	if (!spawn.GetData(SID("disable-joint-wind"), false))
	{
		InitJointWind();
	}

	const StringId64 shaderDriverId = spawn.GetData<StringId64>(SID("joint-shader-driver-list"), resolvedLook.m_jointShaderDriverListId);
	if (shaderDriverId != INVALID_STRING_ID_64)
	{
		m_pJointShaderDriverList = ScriptPointer<DC::JointShaderDriverList>(shaderDriverId);
	}

	if (m_goFlags.m_isPoiLookAt && !EngineComponents::GetNdGameInfo()->m_isHeadless)
	{
		AddLookAtObject(this);
	}

	m_goFlags.m_managesOwnHighContrastMode = spawn.GetData<bool>(SID("manages-own-high-contrast-mode"), false);
	const StringId64 hcmMeshFilterId = spawn.GetData<StringId64>(SID("high-contrast-mode-mesh-exclude-filter"), INVALID_STRING_ID_64);
	if (hcmMeshFilterId != INVALID_STRING_ID_64)
	{
		m_pHcmMeshExcludeFilter = ScriptPointer<const DC::SymbolArray>(hcmMeshFilterId, kHcmFilterModule);
		if (FALSE_IN_FINAL_BUILD(!m_pHcmMeshExcludeFilter.Valid()))
		{
			MsgConErr("%s has an invalid high-contrast-mode-mesh-exclude-filter %s (not found in %s)\n", DevKitOnly_StringIdToString(GetUserId()), DevKitOnly_StringIdToString(hcmMeshFilterId), DevKitOnly_StringIdToString(kHcmFilterModule));
		}
#ifndef FINAL_BUILD
		if (!s_sentHcmFilterEmail)
		{
			CONST_EXPR int kMaxExcludedMeshes = 50;
			if (m_pHcmMeshExcludeFilter.Valid() && m_pHcmMeshExcludeFilter->m_count > kMaxExcludedMeshes)
			{
				MAIL_ASSERT(HighContrastMode, m_pHcmMeshExcludeFilter->m_count <= kMaxExcludedMeshes, ("hcmMeshFilterId %s has more than %d meshes, this is likely too slow for linear search\n", DevKitOnly_StringIdToString(hcmMeshFilterId), kMaxExcludedMeshes));
				s_sentHcmFilterEmail = true;
			}
		}
#endif
	}

	m_forceUpdateHighContrastModeFrame = -1000;

	if (m_pDrawControl != nullptr)
	{
		const bool hasHcmModeTypeTag = spawn.HasData(SID("high-contrast-mode-type"));
		const StringId64 hcmModeType = hasHcmModeTypeTag ? spawn.GetData<StringId64>(SID("high-contrast-mode-type"), SID("None")) : SID("None");
		DC::HCMModeType type = FromStringId64ToHCMModeType(hcmModeType);

		if (type == DC::kHCMModeTypeNone && m_pInteractCtrl != nullptr)
		{
			if (m_pInteractCtrl->IsInteractionEnabled())
				type = FromStringId64ToHCMModeType(m_pInteractCtrl->GetHCMMode());
			else
				type = FromStringId64ToHCMModeType(m_pInteractCtrl->GetHCMModeDisabled());
		}

		if (type != DC::kHCMModeTypeNone)
		{
			AllocateShaderInstanceParams();

			m_pDrawControl->SetHighContrastModeType(type, GetHighContrastModeMeshExcludeFilter());
			if (hasHcmModeTypeTag)
				NotifyHighContrastModeOverriden();
		}
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::ResolveInstanceMaterialRemaps(ResolvedLook& resolvedLook, const SpawnInfo& spawn)
{
	U32 numInstanceMaterialRemaps = spawn.GetData(SID("numInstanceMaterialRemaps"), 0);
	if (numInstanceMaterialRemaps > 0)
	{
		const MaterialTable* pMaterialTable = spawn.GetLevel()->GetMaterialTable();
		if (!pMaterialTable)
		{
			MsgErr("Missing material table in level %s. The actor %s won't have material remap.\n", spawn.GetLevel()->GetName(), spawn.GetName());
			//return;
		}

		char remapSrc[32] = { 0 };
		char remapDst[32] = { 0 };
		char remapDstHash[32] = { 0 };

		InstanceMaterialRemap* aInstanceMaterialRemaps = NDI_NEW(kAllocSingleGameFrame) InstanceMaterialRemap[numInstanceMaterialRemaps];
		for (U32 iMesh = 0; iMesh < resolvedLook.m_numMeshes; iMesh++)
		{
			resolvedLook.m_aMeshDesc[iMesh].m_numInstanceMaterialRemaps = numInstanceMaterialRemaps;
			resolvedLook.m_aMeshDesc[iMesh].m_aInstanceMaterialRemaps = aInstanceMaterialRemaps;

			if (spawn.m_pSpawner->IsAutoActor() && pMaterialTable)
				resolvedLook.m_aMeshDesc[iMesh].m_pLevelBoundMatab = pMaterialTable;
			else
			{
				const ArtItemGeo* pGeoLod0 = resolvedLook.m_aMeshDesc[iMesh].m_geoLod0.ToArtItem();
				const FgGeometry* pFgGeometry = pGeoLod0->m_pFgGeometry;
				pMaterialTable = resolvedLook.m_aMeshDesc[iMesh].m_pLevelBoundMatab = pFgGeometry->GetMaterialTable();
			}
		}

		m_aInstanceMaterialRemaps = NDI_NEW InstanceMaterialRemap[numInstanceMaterialRemaps];
		m_numInstanceMaterialRemaps = numInstanceMaterialRemaps;

		for (U32 iRemap = 0; iRemap < numInstanceMaterialRemaps; iRemap++)
		{
			snprintf(remapSrc, 32, "instanceMaterialRemapSrc%d", iRemap);
			snprintf(remapDst, 32, "instanceMaterialRemapDst%d", iRemap);
			snprintf(remapDstHash, 32, "instanceMaterialRemapDstHash%d", iRemap);

			aInstanceMaterialRemaps[iRemap].m_src = spawn.GetData(StringToStringId64(remapSrc), INVALID_STRING_ID_64);
			StringId64 dstShaderPath = spawn.GetData(StringToStringId64(remapDst), INVALID_STRING_ID_64);
			aInstanceMaterialRemaps[iRemap].m_dst = dstShaderPath.GetValue();

			String def;
			String dstHash = spawn.GetData(StringToStringId64(remapDstHash), def);
			if (!dstHash.IsEmpty() && pMaterialTable)
			{
				U64 matHash = strtoull(dstHash.GetString(), nullptr, 16);
				for (U32 iMat = 0; iMat < pMaterialTable->GetNumMaterials(); iMat++)
				{
					const MaterialInstanceDesc* pDesc = pMaterialTable->GetMaterial(iMat)->GetMaterialInstanceInfo()->m_pMaterialInstanceDesc;
					if (pMaterialTable->GetMaterial(iMat)->GetNameId() == dstShaderPath && pDesc->m_materialHash == matHash)
					{
						aInstanceMaterialRemaps[iRemap].m_dst = iMat;
						break;
					}
				}
			}

			m_aInstanceMaterialRemaps[iRemap].m_src = aInstanceMaterialRemaps[iRemap].m_src;
			m_aInstanceMaterialRemaps[iRemap].m_dst = aInstanceMaterialRemaps[iRemap].m_dst;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::ResolveLook(ResolvedLook& resolvedLook, const SpawnInfo& spawn, StringId64 desiredActorOrLookId)
{
	PROFILE(Processes, NdGO_ResolveLook);

	LookPointer pLook = GetLook(desiredActorOrLookId);

	// ... resolve our look into a list of actual meshes and prop sets
	if (spawn.m_pParentLookSeed)
	{
		resolvedLook.Resolve(spawn.m_pParentLookSeed->m_userId,
							 spawn.m_pParentLookSeed->m_collectionRandomizer,
							 spawn.m_pParentLookSeed->m_tintRandomizer,
							 desiredActorOrLookId,
							 pLook,
							 GetLook(spawn.m_pParentLookSeed->m_lookId),
							 spawn.m_pTintArray);
	}
	else
	{
		resolvedLook.Resolve(GetUserIdUsedForLook(),
							 m_lookCollectionRandomizer,
							 m_lookTintRandomizer,
							 desiredActorOrLookId,
							 pLook,
							 nullptr,
							 spawn.m_pTintArray);
	}

	ResolveInstanceMaterialRemaps(resolvedLook, spawn);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::AssociateWithLevel(const Level* pLevel)
{
	ParentClass::AssociateWithLevel(pLevel);

	if (GetAssociatedLevel() == nullptr)
	{
		if (m_pScriptInst != nullptr)
		{
			m_pScriptInst->SetSpawner(nullptr);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32	NdGameObject::GetWetMaskIndex(U32 instanceTextureTableIndex) const
{
	ASSERT(instanceTextureTableIndex >= 0 && instanceTextureTableIndex < kMaxInstanceTextureTables);
	return (I32)m_wetMaskIndexes[instanceTextureTableIndex];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetWetMaskIndex(I32 newWetMaskIndex, U32 instanceTextureTableIndex)
{
	ASSERT(instanceTextureTableIndex >= 0 && instanceTextureTableIndex < kMaxInstanceTextureTables);
	m_wetMaskIndexes[instanceTextureTableIndex] = (I16)newWetMaskIndex;
};

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdGameObject::PostInit(const ProcessSpawnInfo& spawnInfo)
{
#if !FINAL_BUILD
	// Should we be recorded?
	bool bIsPlayerRelated = false;
	if (MeetsCriteriaToDebugRecord(bIsPlayerRelated))
		JAFAnimRecorderManager::GetManager().AddPotentialObjectToRecord(NdGameObjectHandle(this), bIsPlayerRelated);
#endif

	const SpawnInfo& spawn = static_cast<const SpawnInfo&>(spawnInfo);

	Err result = ParentClass::PostInit(spawn);
	if (!result.Failed())
	{
		const Err platRes = SetupPlatformControl(spawn);
		if (platRes.Failed())
			return platRes;

		MarkSpawnTime(SID("ndgo-platform-control"));

		ASSERT(!m_pScriptInst);
		ASSERT(!IsScriptUpdateEnabled());	// by default, processes do not get ScriptUpdate() calls

		// Configure state script based on spawner tags.
		//
		// IMPORTANT:  We must init our script instance only AFTER the entire class hierarchy has
		// had a chance to init, so we can know what features (targetable, physics etc.)
		// the class has set up when we init the script.

		const bool hasScriptRecord = spawn.HasData(SID("state-script"));
		const StringId64 forceStateScript = !hasScriptRecord ? GetForceStateScriptId() : INVALID_STRING_ID_64;
		const bool scriptDisabled = IsStateScriptDisabled(spawn);

		if (!scriptDisabled && (hasScriptRecord || (forceStateScript != INVALID_STRING_ID_64)))
		{
			m_pScriptInst = NDI_NEW SsInstance;
			if (m_pScriptInst)
			{
				result = m_pScriptInst->Init(spawn, *this, forceStateScript);
				if (result.Failed())
				{
					return Err::kErrMissingStateScript;
				}

				bool async = false;
				if (m_pScriptInst && m_pScriptInst->GetScript() && m_pScriptInst->GetScript()->m_options)
				{
					async = m_pScriptInst->GetScript()->m_options->m_ndFlags & DC::kNdSsOptionsFlagAllowAsyncUpdate;
				}

				SetScriptUpdateEnabled(true, async);
			}
			else
			{
				result = Err::kErrOutOfMemory;
			}
		}

		if (m_pScriptInst)
		{
			GoToInitialScriptState();
		}

		MarkSpawnTime(SID("ndgo-init-script"));

		if (const EntitySpawner* pSpawner = spawn.m_pSpawner)
		{
			String childStateScripts[16];
			const I32F numChildStateScripts = pSpawner->GetDataArray<String>(SID("child-state-script"), childStateScripts, ARRAY_COUNT(childStateScripts), String());
			for (I32F i = 0; i < numChildStateScripts; ++i)
			{
				const StringId64 childStateScript = childStateScripts[i].AsStringId();
				if (childStateScript != INVALID_STRING_ID_64)
				{
					const bool doAutoNumber = true;
					SsManager::SpawnParams params(childStateScript, childStateScripts[i].GetString(), nullptr);
					params.m_autoNumber = doAutoNumber;
					SsProcess* pChildScriptProcess = g_ssMgr.SpawnStateScript(params);
					if (pChildScriptProcess)
					{
						pChildScriptProcess->ChangeParentProcess(this);
					}
				}
			}
		}

		// move to the pass through layer if the correct tag is set
		if (spawn.GetData<bool>(SID("force-player-pass-through"), false))
		{
			if (CompositeBody* pComposite = GetCompositeBody())
			{
				pComposite->SetLayer(Collide::kLayerSmall);
			}
			else if (RigidBody* pBody = GetRigidBody())
			{
				pBody->SetLayer(Collide::kLayerSmall);
			}
		}

		if (IsVisibleInListenMode())
		{
			RegisterInListenModeMgr();
		}

		if (m_pAttackHandler)
		{
			result = m_pAttackHandler->PostInit(*this, spawn);
			if (result.Failed())
				return result;
		}
	}

	/*
	bool hasNetController = m_pNetController != nullptr;
	bool updateShimmer = ShouldUpdateShimmer();
	bool tracksSplines = GetSplineTracker() != nullptr;
	if (!hasNetController && !updateShimmer && !tracksSplines)
	*/

	MarkSpawnTime(SID("ndgo-before-nav-blocker"));

	const Err navRes = InitNavBlocker(spawnInfo);

	if (navRes.Failed())
	{
		return navRes;
	}

	UpdateNavBlocker();

	if (m_bUpdateCoverActionPacks && (g_alphaMult[0] < NDI_FLT_EPSILON))
	{
		PROFILE_ACCUM(NdGo_InitCover);

		const bool isStandingPlatform = (m_pPlatformControl && (m_pPlatformControl->GetNavMeshCount() > 0));

		if (!isStandingPlatform)
		{
			const NdGameObject* pBindObj = GetBoundGameObject();
			const RigidBody* pBindTarget = GetBoundRigidBody();
			RegisterCoverActionPacksInternal(pBindObj, pBindTarget);
		}
		m_bUpdateCoverActionPacks = false;
	}

	{
		StringId64 sfxAutoTannoyId = spawn.GetData<StringId64>(SID("auto-tannoy"), INVALID_STRING_ID_64);
		if (sfxAutoTannoyId != INVALID_STRING_ID_64)
		{
			m_goFlags.m_autoTannoy = true;
			StringId64 jointId = spawn.GetData<StringId64>(SID("auto-tannoy-joint"), INVALID_STRING_ID_64);
			bool bAssign14 = spawn.GetData<bool>(SID("auto-tannoy-1-4"), true);
			bool bAssign58 = spawn.GetData<bool>(SID("auto-tannoy-5-8"), true);
			EngineComponents::GetAudioManager()->AutoTannoyRegister(this, sfxAutoTannoyId, jointId, (bAssign14 ? ARM_SFX_OPTIONS_TANNOY_INPUT_1_4 : 0U) | (bAssign58 ? ARM_SFX_OPTIONS_TANNOY_INPUT_5_8 : 0U));
		}
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::RegisterWithInteractablesMgr()
{
	GAMEPLAY_ASSERT(m_pInteractCtrl);
	GAMEPLAY_ASSERT(!m_goFlags.m_registeredInteractable);
#ifndef HEADLESS_BUILD
	g_ndInteractablesMgr.RegisterInteractable(this);
	m_goFlags.m_registeredInteractable = true;
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::PostSnapshotInit()
{
	ParentClass::PostSnapshotInit();
	//If we have a targetable, we should have a snapshot
	if (GetTargetable() != nullptr)
	{
		GAMEPLAY_ASSERT(HasSnapshot());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetScale(Vector_arg scale)
{
	if (FgAnimData* pAnimData = GetAnimData())
	{
		pAnimData->m_scale = scale;
		pAnimData->SetXform(GetLocator()); // so that scale gets propagated to the joint cache
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector NdGameObject::GetScale() const
{
	if (const FgAnimData* pAnimData = GetAnimData())
	{
		return pAnimData->m_scale;
	}
	return Vector(1.0f, 1.0f, 1.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetUniformScale(float scale)
{
	if (FgAnimData* pAnimData = GetAnimData())
	{
		pAnimData->m_scale = Vector(scale, scale, scale);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NdGameObject::GetUniformScale() const
{
	float scale = 1.0f;
	if (const FgAnimData* pAnimData = GetAnimData())
	{
		Vector scaleX(SMATH_VEC_REPLICATE_X(pAnimData->m_scale.QuadwordValue()));
		if (MaxComp(Abs(pAnimData->m_scale - scaleX)) > 0.00001f)
		{
			MsgConScriptError("Object %s of type %s and look %s has a non-uniform scale which is not supported\n",
				DevKitOnly_StringIdToString(GetUserId()),
				GetTypeName(),
				DevKitOnly_StringIdToString(GetLookId()));
		}

		scale = scaleX.X();
		GAMEPLAY_ASSERT(IsFinite(scale));
	}
	return scale;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::MeetsCriteriaToDebugRecord(bool& outRecordAsPlayer) const
{
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::RespondsToScriptUpdate() const
{
	if (!IsScriptUpdateEnabled())
	{
		return false;
	}

	const SsInstance* pScriptInst = GetScriptInstance();
	if (pScriptInst && pScriptInst->RespondsToUpdate())
	{
		return true;
	}

	return ParentClass::RespondsToScriptUpdate();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::EventHandler(Event& event)
{
	if (m_pNetController)
		m_pNetController->EventHandler(*this, event);
	if (m_pAttackHandler)
		m_pAttackHandler->EventHandler(*this, event);

	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("set-poi-radius"):
	{
		m_goFlags.m_isPoiLookAt = true;
		if (event.GetNumParams() >= 1)
		{
			m_maxPoiCollectRadius = event.Get(0).GetAsF32();
			if (event.GetNumParams() >= 2)
			{
				m_minPoiCollectRadius = event.Get(1).GetAsF32();
				if (event.GetNumParams() >= 3)
				{
					m_poiCollectType = event.Get(2).GetAsStringId();
				}
			}
		}
		break;
	}
	case SID_VAL("Dither"):
	{
		F32 blendAmount = event.Get(0).GetFloat();

		/*char str[32];
		sprintf(str, "%.2f\n", blendAmount);
		g_prim.Draw(DebugString(GetTranslation() + Vector(kUnitYAxis) * 10.0f, str));*/

		GetDrawControl()->SetScriptBlend(blendAmount);
		break;
	}
	case SID_VAL("are-meshes-hidden?"):
		{
			bool hidden = false;
			if (IDrawControl* pDrawControl = GetDrawControl())
			{
				int screenIndex = (event.GetNumParams() > 0) ? event.Get(0).GetAsI32() : -1;
				hidden = pDrawControl->IsObjectHidden(screenIndex);
			}
			event.SetResponse(hidden);
		}
		break;
	case SID_VAL("Show"):
		{
			int screenIndex = (event.GetNumParams() > 0) ? event.Get(0).GetAsI32() : -1;
			bool secondary = (event.GetNumParams() > 1) ? event.Get(1).GetAsBool() : false;
			Show(screenIndex, secondary);
		}
		break;
	case SID_VAL("swap-attached-background"):
		{
			if (m_pBgAttacher)
			{
				m_pBgAttacher->ClearLevels();
				for (int i = 0; i < event.GetNumParams(); i++)
				{
					m_pBgAttacher->AddLevel(event.Get(i).GetStringId());
				}
			}
		}
		break;
	case SID_VAL("EnableMainView"):
		if (IDrawControl* pDrawControl = GetDrawControl())
		{
			pDrawControl->EnableAllMeshInMainView();
		}
		break;
	case SID_VAL("EnableCastShadows"):
		if (IDrawControl* pDrawControl = GetDrawControl())
		{
			pDrawControl->EnableAllMeshCastShadows();
		}
		break;
	case SID_VAL("Hide"):
		{
			int screenIndex = (event.GetNumParams() > 0) ? (int)event.Get(0).GetU32() : -1;
			bool secondary = (event.GetNumParams() > 1) ? event.Get(1).GetAsBool() : false;
			Hide(screenIndex, secondary);
		}
		break;
	case SID_VAL("DisableMainView"):
		if (IDrawControl* pDrawControl = GetDrawControl())
		{
			pDrawControl->DisableAllMeshFromMainView();
		}
		break;
	case SID_VAL("DisableCastShadows"):
		if (IDrawControl* pDrawControl = GetDrawControl())
		{
			pDrawControl->DisableAllMeshCastShadows();
		}
		break;
	case SID_VAL("ShowObject"):
		{
			int screenIndex = -1;
			if (event.GetNumParams() > 0)
			{
				//eff events can call ShowObject. They will pass a pointer as first parameter so make sure the parameter is an integer if any.
				const BoxedValue& value = event.Get(0);
				if (value.IsNumeric())
					screenIndex = value.GetU32();
			}
			ShowObject(screenIndex);
		}
		break;
	case SID_VAL("HideObject"):
		{
			int screenIndex = -1;
			if (event.GetNumParams() > 0)
			{
				//eff events can call HideObject. They will pass a pointer as first parameter so make sure the parameter is an integer if any.
				const BoxedValue& value = event.Get(0);
				if (value.IsNumeric())
					screenIndex = value.GetU32();
			}
			HideObject(screenIndex);
		}
		break;

	case SID_VAL("disable-effects"):
		m_goFlags.m_disableEffects = true;
		break;
	case SID_VAL("enable-effects"):
		m_goFlags.m_disableEffects = false;
		break;

	case SID_VAL("get-spline-id"):
		{
			SplineTracker* pSplineTracker = GetSplineTracker();
			if (pSplineTracker)
			{
				event.SetResponse(pSplineTracker->GetSplineId());
			}
		}
		break;

	case SID_VAL("transfer-to-spline"):
		{
			bool success = false;

			if (event.GetNumParams() >= 2)
			{
				StringId64 splineId = event.Get(0).GetStringId();
				SplineTracker::TransferType type = static_cast<SplineTracker::TransferType>(event.Get(1).GetI32());

				SplineTracker* pSplineTracker = GetSplineTracker();
				if (pSplineTracker)
				{
					if (splineId == INVALID_STRING_ID_64)
					{
						pSplineTracker->SetSpline(nullptr);
						success = true;
					}
					else
					{
						CatmullRom* pSpline = g_splineManager.FindByName(splineId);
						if (pSpline)
						{
							success = pSplineTracker->RequestTransferTo(pSpline, type);
						}
						else
						{
							MsgScript("transfer-to-spline: Unable to find spline '%s' for object '%s' to transfer to.\n",
									  DevKitOnly_StringIdToString(splineId), GetName());
						}
					}
				}
				else
				{
					MsgScript("transfer-to-spline: Object '%s' unable to track splines (add property \"can-track-spline\").\n", GetName());
				}
			}

			event.SetResponse(BoxedValue(success));
		}
		return;

	case SID_VAL("physicalize"):
		SetPhysMotionTypeEvent(kRigidBodyMotionTypeAuto, event);
		break;
	case SID_VAL("physicalize-gamedriven"):
		SetPhysMotionTypeEvent(kRigidBodyMotionTypeGameDriven, event);
		break;
	case SID_VAL("physicalize-fixed"):
		SetPhysMotionTypeEvent(kRigidBodyMotionTypeFixed, event);
		break;
	case SID_VAL("physicalize-live"):
		SetPhysMotionTypeEvent(kRigidBodyMotionTypePhysicsDriven, event);
		break;
	case SID_VAL("unphysicalize"):
		SetPhysMotionTypeEvent(kRigidBodyMotionTypeNonPhysical, event);
		break;
	case SID_VAL("blend-out-physics"):
		if (CompositeBody* pCompositeBody = GetCompositeBody())
		{
			F32 blend = event.GetNumParams() > 0 ? event.Get(0).GetAsF32() : 0.2f;
			U32F bodyIndex = CompositeBody::kInvalidBodyIndex;
			if (event.GetNumParams() > 1)
			{
				StringId64 jointSid = event.Get(1).GetStringId();
				if (jointSid != INVALID_STRING_ID_64)
					bodyIndex = m_pCompositeBody->FindBodyIndexByJointSid(jointSid);
			}
			pCompositeBody->BlendToAnimation(blend, true, bodyIndex);
		}
		break;

	case SID_VAL("break-constraint"):
		{
			if (!m_pCompositeBody)
				return;

			I16 iJoint = -1;
			if (event.GetNumParams() >= 1)
			{
				const StringId64 jointSid = event.Get(0).GetStringId();
				iJoint = FindJointIndex(jointSid);
				if (iJoint < 0)
					break;
			}

			bool noFx = event.Get(1).GetAsBool(false);

			m_pCompositeBody->BreakConstraint(iJoint, noFx);
		}
		break;

	case SID_VAL("destruction-collapse-disable"):
		if (m_pCompositeBody)
			m_pCompositeBody->SetCollapseDisabled(true);
		break;

	case SID_VAL("destruction-collapse-enable"):
		if (m_pCompositeBody)
			m_pCompositeBody->SetCollapseDisabled(false);
		break;

	case SID_VAL("enable-object-as-nav-blocker"):
		EnableNavBlocker(true);
		break;
	case SID_VAL("disable-object-as-nav-blocker"):
		EnableNavBlocker(false);
		break;

	case SID_VAL("block-nav-poly-under-object"):
		BlockNavPoly(true);
		break;
	case SID_VAL("unblock-nav-poly-under-object"):
		BlockNavPoly(false);
		break;

	case SID_VAL("disable-object-features"):
		DisableFeatures();
		break;
	case SID_VAL("enable-object-features"):
		EnableFeatures();
		break;

	case SID_VAL("can-hang-from"):
		if (m_goFlags.m_bDisableCanHangFrom)
		{
			event.SetResponse(false);
			return;
		}
		break;

	case SID_VAL("enable-breakable"):
		SetDestructionEnabledEvent(true, event);
		break;

	case SID_VAL("disable-breakable"):
		SetDestructionEnabledEvent(false, event);
		break;

	case SID_VAL("enable-breakable-for-contacts"):
		SetDestructionEnabledForContactsEvent(true, event);
		break;

	case SID_VAL("disable-breakable-for-contacts"):
		SetDestructionEnabledForContactsEvent(false, event);
		break;

	case SID_VAL("disintegrate-breakable"):
		{
			Vector dir(kUnitYAxis);
			F32 impulse = 0.0f;
			if (event.GetNumParams() >= 2)
			{
				dir = event.Get(0).GetVector();
				impulse = event.Get(1).GetF32();
			}
			bool noFx = false;
			if (event.GetNumParams() >= 3)
			{
				noFx = event.Get(2).GetBool();
			}
			DisintegrateBreakable(dir, impulse, false, noFx);
		}
		break;

	case SID_VAL("disintegrate-breakable-explosion"):
		{
			Point center = Point(kZero) + event.Get(0).GetVector();
			F32 impulse = event.Get(1).GetF32();
			DisintegrateBreakableExplosion(center, impulse);
		}
		break;

	case SID_VAL("set-breakable-disintegrated"):
		DisintegrateBreakable(kUnitYAxis, 0.0f, true);
		break;

	case SID_VAL("set-breakable-collapsed"):
		CollapseBreakable();
		break;

	case SID_VAL("kill-rigid-body"):
		{
			if (m_pCompositeBody)
			{
				const StringId64 jointSid = event.Get(0).GetStringId();
				U32F bodyIndex = m_pCompositeBody->FindBodyIndexByJointSid(jointSid);
				if (bodyIndex != CompositeBody::kInvalidBodyIndex)
				{
					m_pCompositeBody->GetBody(bodyIndex)->Kill();
				}
			}
			else if (RigidBody* pBody = GetRigidBody())
			{
				pBody->Kill();
			}
		}
		break;

	case SID_VAL("NotifyAimingAtYou"):
		{
			if (m_goFlags.m_replyAimingAtFriend)
			{
				SendEvent(SID("AimingAtFriend"), event.GetSender());
			}
		}
		break;

	case SID_VAL("get-selection-target-pos"):
		{
			Point* selectionTarget = NDI_NEW(kAllocSingleGameFrame) Point(GetTranslation());
			NdInteractControl* pInteractCtrl = GetInteractControl();
			const NdGameObject *pTargetObj = NdGameObject::FromProcess(event.Get(0).GetProcess());
			if (pInteractCtrl != nullptr && pTargetObj != nullptr)
				*selectionTarget = pInteractCtrl->GetSelectionTargetWs(pTargetObj);

			event.SetResponse(*selectionTarget);
		}
		break;

	case SID_VAL("net-pick-up-this-client"):
		{
			NdInteractControl* pInteractCtrl = GetInteractControl();
			if (pInteractCtrl != nullptr)
			{
				Locator loc = GetLocator();
				SendEventFrom(this, SID("interact-with-me"), event.GetSender());
			}
		}
		break;

	//case SID_VAL("has-peg-leg"):
	//	{
	//		// so far only elena has peg leg.
	//		IDrawControl* pDrawControl = GetDrawControl();
	//		bool hasPegLeg = pDrawControl ? (pDrawControl->GetMeshByName(SID("fh-elena-12-legs")) != nullptr) : false;
	//		event.SetResponse(hasPegLeg);
	//	}
	//	break;

	case SID_VAL("splasher-event"):
		{
			if (m_pSplasherController)
				m_pSplasherController->EventHandler(event);
		}
		break;

	case SID_VAL("collision-logout-query"):
		{
			const ArtItemCollision* pArtItem = event.Get(0).GetConstPtr<ArtItemCollision*>();
			StringId64 artName = event.Get(1).GetStringId();
			if (m_pCompositeBody && m_pCompositeBody->IsUsingCollision(pArtItem))
			{
				event.SetResponse(true);
				return;
			}
			else if (GetRigidBody() && (artName != INVALID_STRING_ID_64 && GetLookId() == artName))
			{
				event.SetResponse(true);
				return;
			}
			break;
		}

	case SID_VAL("collision-logout"):
		{
			const ArtItemCollision* pArtItem = event.Get(0).GetConstPtr<ArtItemCollision*>();
			StringId64 artName = event.Get(1).GetStringId();
			if (m_pCompositeBody && m_pCompositeBody->IsUsingCollision(pArtItem))
			{
				MsgErr("Logging out collision %s while it's still in use by %s!\n", pArtItem->GetName(), DevKitOnly_StringIdToString(GetUserId()));
				HandleDataLoggedOutError();
				m_pCompositeBody->DestroyCompositeBody();
			}
			else if (GetRigidBody() && (artName != INVALID_STRING_ID_64 && GetLookId() == artName))
			{
				MsgErr("Logging out collision %s while it's still in use by %s!\n", DevKitOnly_StringIdToString(artName), DevKitOnly_StringIdToString(GetUserId()));
				HandleDataLoggedOutError();
				KillProcess(this);
			}
			break;
		}

#if !FINAL_BUILD
	case SID_VAL("collision-reload"):
		{
			const ArtItemCollision* pArtItem = event.Get(0).GetConstPtr<ArtItemCollision*>();
			StringId64 artName = event.Get(1).GetStringId();
			if (m_pCompositeBody && (m_pCompositeBody->GetArtItem() == pArtItem || (artName != INVALID_STRING_ID_64 && GetLookId() == artName)))
			{
				m_pCompositeBody->DestroyCompositeBody();
				event.SetResponse(true);
			}
			else if (GetRigidBody() && (artName != INVALID_STRING_ID_64 && GetLookId() == artName))
			{
				GetRigidBody()->Destroy();
				event.SetResponse(true);
			}
			break;
		}
#endif

	case SID_VAL("set-interaction-enabled"):
		{
			if (NdInteractControl* pCtrl = GetInteractControl())
				pCtrl->SetInteractionEnabled(true);
		}
		break;

	case SID_VAL("set-interaction-disabled"):
		{
			if (NdInteractControl* pCtrl = GetInteractControl())
				pCtrl->SetInteractionEnabled(false);
		}
		break;

	case SID_VAL("get-dist-camera-pos"):
		{
			NdInteractControl* pCtrl = GetInteractControl();
			if (pCtrl != nullptr)
			{
				Point* selectionTarget = NDI_NEW(kAllocSingleGameFrame) Point(pCtrl->GetSelectionTargetWs(NdGameObject::FromProcess(event.Get(0).GetProcess())));
				event.SetResponse(*selectionTarget);
			}
			else
			{
				Point* selectionTarget = NDI_NEW(kAllocSingleGameFrame) Point(GetTranslation());
				event.SetResponse(*selectionTarget);
			}
		}
		break;

	case SID_VAL("get-item-id"):
		event.SetResponse(GetPickupItemId());
		return;

	case SID_VAL("get-item-amount"):
		event.SetResponse(GetPickupItemAmount());
		return;

	case SID_VAL("get-ammo-amount"):
		event.SetResponse(GetPickupAmmoAmount());
		return;

	case SID_VAL("enable-camera-parenting"):
		m_goFlags.m_enableCameraParenting = true;
		break;

	case SID_VAL("disable-camera-parenting"):
		m_goFlags.m_enableCameraParenting = false;
		break;
	case SID_VAL("set-fact-float"):
		{
			if (FactDictionary* pFacts = GetFactDict())
			{
				StringId64 factId = event.Get(0).GetStringId();
				float val = event.Get(1).GetFloat();
				pFacts->Set(factId, val);
			}
		}
		break;
	case SID_VAL("set-fact-sid"):
		{
			if (FactDictionary* pFacts = GetFactDict())
			{
				StringId64 factId = event.Get(0).GetStringId();
				StringId64 val = event.Get(1).GetStringId();
				pFacts->Set(factId, val);
			}
		}
		break;
	case SID_VAL("set-fact-bool"):
		{
			if (FactDictionary* pFacts = GetFactDict())
			{
				StringId64 factId = event.Get(0).GetStringId();
				bool val = event.Get(1).GetBool();
				pFacts->Set(factId, val);
			}
		}
		break;
	case SID_VAL("erase-fact"):
		{
			if (FactDictionary* pFacts = GetFactDict())
			{
				StringId64 factId = event.Get(0).GetStringId();
				pFacts->Erase(factId);
			}
		}
		break;
	case SID_VAL("object-teleport"):
		SetTeleport();
		break;

	default:
		break;
	}

	ParentClass::EventHandler(event);
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct DispatchStatsBase
{
	I32				m_childrenVisited = 0;
	I32				m_childrenDispatched = 0;
	I32				m_childrenHandled = 0;
	I32				m_maxDepth = 0;
};
struct DispatchStats : public DispatchStatsBase
{
	I32				m_totalEventCount = 0;
	I32				m_walkedEventCount = 0;
	I32				m_unhandledEventCount = 0;
};
DispatchStats g_dispatchEventStats;

/// --------------------------------------------------------------------------------------------------------------- ///
struct DispatchArgs
{
	Event*				m_pEvent = nullptr;
	bool				m_scriptDispatch = false;

	Process*			m_pRoot = nullptr;
	DispatchStatsBase	m_stats;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool DispatchEventToChildStateScript(Process* pProcess, void* pUserData)
{
	DispatchArgs& args = *(DispatchArgs*)pUserData;
#if !FINAL_BUILD
	++args.m_stats.m_childrenVisited;
#endif

	if (pProcess->IsType(SID("SsProcess")))
	{
		SsProcess* pSsProcess = static_cast<SsProcess*>(pProcess);
		SsInstance* pSsInstance = pSsProcess->GetScriptInstance();

		const bool handled = pSsInstance->HandleOrEnqueueEvent(*args.m_pEvent, args.m_scriptDispatch);

#if !FINAL_BUILD
		++args.m_stats.m_childrenDispatched;
		if (handled)
			++args.m_stats.m_childrenHandled;
#endif
	}

#if !FINAL_BUILD
	if (args.m_pRoot && !pProcess->IsType(SID("SsTrackGroupProcess")))
	{
		I32 depth = 0;
		Process* pParent = pProcess;
		while (pParent != args.m_pRoot)
		{
			++depth;
			pParent = pParent->GetParentProcess();
		}
		if (args.m_stats.m_maxDepth < depth)
		{
			args.m_stats.m_maxDepth = depth;
			//if (depth > 1)
			//{
			//	MsgScript("depth = %d for %s (root %s)\n", depth, DevKitOnly_StringIdToStringOrHex(pProcess->GetUserId()), DevKitOnly_StringIdToStringOrHex(args.m_pRoot->GetUserId()));
			//}
		}
	}
#endif

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct RespondsToEventArgs
{
	StringId64 m_eventMessage;
	bool*	m_pRespondsToEvent;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ChildStateScriptRespondsToEvent(Process* pProcess, void* pUserData)
{
	const RespondsToEventArgs& args = *(RespondsToEventArgs*)pUserData;

	if (pProcess->IsType(SID("SsProcess")))
	{
		SsProcess* pSsProcess = static_cast<SsProcess*>(pProcess);
		SsInstance* pSsInstance = pSsProcess->GetScriptInstance();

		*args.m_pRespondsToEvent = pSsInstance->RespondsToEvent(args.m_eventMessage);
	}

	return !*args.m_pRespondsToEvent;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::RegisterChildScriptEvent(StringId64 eventId)
{
	if (m_childScriptEventCapacity != 0)
	{
		GAMEPLAY_ASSERT(m_aChildScriptEventId != nullptr);

		const U32 index = FastFindInteger64_CautionOverscan(m_aChildScriptEventId, m_childScriptEventCount, eventId.GetValue());
		if (index == kFastFindInvalid)
		{
			// it's not already in the list
			if (m_childScriptEventCount < m_childScriptEventCapacity)
			{
				// NOTE: We never UNregister child script events, under the thinking that the set of scripts childed under
				// an NdGameObject will tend to be pretty stable, and if we err on the side of sending the event to children
				// even when that child is no longer around, it's ok, it's just a performance hit that should be very rare.
				// (NB: We keep stats on this, so we can see if it ever gets to be NOT rare.)
				m_aChildScriptEventId[m_childScriptEventCount++] = eventId;
			}
			else
			{
				GAMEPLAY_ASSERTF(m_childScriptEventCount < m_childScriptEventCapacity, ("Please increase %s::GetChildScriptEventCapacity() (currently %d)\n", GetTypeName(), m_childScriptEventCapacity));
			}
		}
	}
	else
	{
		GAMEPLAY_ASSERTF(m_aChildScriptEventId, ("Please implement %s::GetChildScriptEventCapacity().", GetTypeName()));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DispatchEvent(Event& e, bool scriptDispatch)
{
	// Pass the event to our script instance, if we have one.
	SsInstance* pScriptInst = GetScriptInstance();
	if (!HasProcessErrorOccurred() && pScriptInst)
	{
		pScriptInst->HandleOrEnqueueEvent(e, scriptDispatch);
	}

#if !FINAL_BUILD
	if (g_gameObjectDrawFilter.m_drawChildScriptEvents)
		g_dispatchEventStats.m_totalEventCount += 1;
#endif

	// Pass the event to any child script instances, if we have any.
	if (!HasProcessErrorOccurred() && m_childScriptEventCapacity != 0)
	{
		GAMEPLAY_ASSERT(m_aChildScriptEventId != nullptr);

		const StringId64 eventId = e.GetMessage();

		const U32 index = FastFindInteger64_CautionOverscan(m_aChildScriptEventId, m_childScriptEventCount, eventId.GetValue());
		const bool dispatchToChildScripts = (index != kFastFindInvalid);

		if (dispatchToChildScripts)
		{
			DispatchArgs args;
			args.m_pEvent = &e;
			args.m_scriptDispatch = scriptDispatch;
			//args.m_pRoot = this; // uncomment this line to re-enable depth stats (they're expensive)

			EngineComponents::GetProcessMgr()->WalkTree(this, &DispatchEventToChildStateScript, &args);

#if !FINAL_BUILD
			if (g_gameObjectDrawFilter.m_drawChildScriptEvents)
			{
				g_dispatchEventStats.m_walkedEventCount += 1;
				g_dispatchEventStats.m_childrenVisited += args.m_stats.m_childrenVisited;
				g_dispatchEventStats.m_childrenDispatched += args.m_stats.m_childrenDispatched;
				g_dispatchEventStats.m_childrenHandled += args.m_stats.m_childrenHandled;
				if (g_dispatchEventStats.m_maxDepth < args.m_stats.m_maxDepth)
					g_dispatchEventStats.m_maxDepth = args.m_stats.m_maxDepth;
				if (args.m_stats.m_childrenDispatched != args.m_stats.m_childrenHandled)
				{
					MsgProcess("UNHANDLED event '%s when walking tree (class %s)\n", DevKitOnly_StringIdToStringOrHex(eventId), GetTypeName());
					g_dispatchEventStats.m_unhandledEventCount += 1;
				}
			}
#endif
		}
	}

	// Now dispatch as usual.
	ParentClass::DispatchEvent(e, scriptDispatch);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*static*/ void NdGameObject::PrintEventDispatchStats()
{
#if !FINAL_BUILD
	GAMEPLAY_ASSERT(g_gameObjectDrawFilter.m_drawChildScriptEvents);

	MsgCon("Child Script Event Dispatch Stats:\n");

	const float oneOverCount = (g_dispatchEventStats.m_walkedEventCount > 0) ? (1.0f / float(g_dispatchEventStats.m_walkedEventCount)) : 0.0f;
	const float avgChildrenVisitedPerCall = float(g_dispatchEventStats.m_childrenVisited) * oneOverCount;
	const float avgChildrenDispatchedPerCall = float(g_dispatchEventStats.m_childrenDispatched) * oneOverCount;
	const float avgChildrenHandledPerCall = float(g_dispatchEventStats.m_childrenHandled) * oneOverCount;
	const float walkedPct = (g_dispatchEventStats.m_totalEventCount > 0) ? (100.0f * float(g_dispatchEventStats.m_walkedEventCount) / float(g_dispatchEventStats.m_totalEventCount)) : 0.0f;
	const float unhandledPct = (g_dispatchEventStats.m_walkedEventCount > 0) ? (100.0f * float(g_dispatchEventStats.m_unhandledEventCount) / float(g_dispatchEventStats.m_walkedEventCount)) : 0.0f;

	MsgCon("  Total events sent to GOs:      %d\n", g_dispatchEventStats.m_totalEventCount);
	MsgCon("  Total events calling WalkTree: %d (%.1f%% of total)\n", g_dispatchEventStats.m_walkedEventCount, walkedPct);
	MsgCon("  Total UNHANDLED events:        %d (%.1f%% of walked)\n", g_dispatchEventStats.m_unhandledEventCount, unhandledPct);
	MsgCon("  Avg Children Visited/Walk:     %.2f\n", avgChildrenVisitedPerCall);
	MsgCon("  Avg Children Dispatched/Walk:  %.2f\n", avgChildrenVisitedPerCall);
	MsgCon("  Avg Children Handled/Walk:     %.2f\n", avgChildrenVisitedPerCall);
	MsgCon("  Max Depth of Tree Walk:        %d\n", g_dispatchEventStats.m_maxDepth);
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::RespondsToScriptEvent(StringId64 eventMessage) const
{
	const SsInstance* pScriptInst = GetScriptInstance();
	if (!HasProcessErrorOccurred() && pScriptInst)
	{
		if (pScriptInst->RespondsToEvent(eventMessage))
		{
			return true;
		}
	}

	// Pass the event to any child script instances, if we have any.
	if (!HasProcessErrorOccurred())
	{
		RespondsToEventArgs args;
		args.m_eventMessage = eventMessage;
		bool responds = false;
		args.m_pRespondsToEvent = &responds;

		EngineComponents::GetProcessMgr()->WalkTree(const_cast<NdGameObject*>(this), &ChildStateScriptRespondsToEvent, &args);

		if (responds)
		{
			return true;
		}
	}

	return ParentClass::RespondsToScriptEvent(eventMessage);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::ProcessDispatchError(const char* strMsg)
{
	EntitySpawner* pSpawner = const_cast<EntitySpawner*>(GetSpawner());

	if (pSpawner)
	{
		pSpawner->GetFlags().m_dataError = true;
	}

	if (AnimControl* pAnimControl = GetAnimControl())
	{
		// disable async callbacks
		FgAnimData& animData = pAnimControl->GetAnimData();
		animData.m_flags &= ~FgAnimData::kEnableAsyncPostAnimUpdate;
		animData.m_flags &= ~FgAnimData::kEnableAsyncPostAnimBlending;
		animData.m_flags &= ~FgAnimData::kEnableAsyncPostJointUpdate;
	}

	char msg[512];
	const char* pLookName = DevKitOnly_StringIdToString(GetLookId());
	sprintf(msg,
			"%s%s%s%s: %s\n",
			GetName(),
			pLookName ? " pkg '" : "",
			pLookName ? pLookName : "",
			pLookName ? "'" : "",
			strMsg);

	MsgErr(msg);

	Point pos = GetLocator().Pos() + Vector(0, 0.1f, 0);
	Process* pProc = SpawnErrorProcess(this, pos, msg);
	if (! pProc)
	{
		MsgErr("NdGameObject::ProcessDispatchError: (type %s) unable to spawn ProcessError.\n", GetTypeName());
	}
	KillProcess(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::GoToInitialScriptState()
{
	// update the targetable
	if (m_pTargetable)
	{
		m_pTargetable->UpdateTargetable(*GetAttachSystem());
	}

	// now go
	ALWAYS_ASSERT(m_pScriptInst);
	ALWAYS_ASSERT(IsScriptUpdateEnabled());
	m_pScriptInst->GoToInitialState();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::DisableAnimation()
{
	if (m_pAnimData)
	{
		AnimExecutionContext* pCtx = GetAnimExecutionContext(m_pAnimData);
		if (!pCtx)
		{
			// We might call this during update before we run animation
			pCtx = const_cast<AnimExecutionContext*>(GetPrevFrameAnimExecutionContext(m_pAnimData));
		}
		if (!pCtx || !pCtx->m_pAllSkinningBoneMats)
		{
			ASSERT(false); // This is kind of bad, we should fix
			MsgErr("DisableAnimation(): Object '%s' has no m_pSkinningBoneMats! This can happen if you try to disable a newly spawned object that never have not animated yet\n", GetName());
			return false;
		}

		BINDPOSE_ASSERT((0 != (m_pAnimData->m_flags & FgAnimData::kIsInBindPose)) == (m_pAnimData->m_pSkinningBoneMats == g_pIdentityBoneMats));
		BINDPOSE_ASSERTF(0 == (m_pAnimData->m_flags & FgAnimData::kIsInBindPose), ("A game object has bind pose optimization enabled BEFORE updates are disabled! Tell Jason G."));

		SsAnimateDebugLog(NdGameObjectHandle(this), nullptr, ("DisableAnimation() called on %s!!!\n", GetName()));

		ChangeAnimConfig(FgAnimData::kAnimConfigNoAnimation);
		EngineComponents::GetAnimMgr()->SetAnimationDisabled(m_pAnimData, FastAnimDataUpdate);
		m_pAnimData->m_flags &= ~(FgAnimData::kEnableAsyncPostAnimUpdate|FgAnimData::kEnableAsyncPostAnimBlending|FgAnimData::kEnableAsyncPostJointUpdate);
		if (m_pCompositeBody)
		{
			m_pCompositeBody->SetProcedurallyAnimating(m_hasJointWind);
			m_pCompositeBody->SetAnimationEnabled(false);
		}

		m_prevLocator = GetLocator();
		m_fastAnimUpdateMoved = true;

		// Zero out hidden joints
		if (pCtx->m_pAllSkinningBoneMats)
		{
			m_goFlags.m_hasHiddenJoints = false;

			U32F numJoints = m_pAnimData->m_jointCache.GetNumTotalJoints();

			I32 hiddenJoints[kMaxHiddenJoints];
			I32 numHiddenJoints = GetHiddenJointIndices(hiddenJoints);

			if (numHiddenJoints > 0)
			{
				Mat34* matData = (Mat34*)pCtx->m_pAllSkinningBoneMats;
				Mat34 zero(kZero);
				m_goFlags.m_hasHiddenJoints = true;

				for (I32F iHidden = 0; iHidden < numHiddenJoints; ++iHidden)
				{
					I32 hiddenIndex = hiddenJoints[iHidden];
					ALWAYS_ASSERT(hiddenIndex >= 0 && hiddenIndex < numJoints);
					matData[hiddenIndex] = zero;
				}
			}

			TestIsInBindPose();
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::EnableAnimation()
{
	// I assume EnableAnimation() is only called for object which animation is turned off
	// Object can have either Simple or Complex animation config, we don't want to change AnimConfig to Simple if it was Complex
	if (IsAnimationEnabled())
		return;

	// we have an animation to play, change our animation type
	ChangeAnimConfig(FgAnimData::kAnimConfigSimpleAnimation);
	m_pAnimData->m_flags |= (FgAnimData::kEnableAsyncPostAnimUpdate|FgAnimData::kEnableAsyncPostAnimBlending|FgAnimData::kEnableAsyncPostJointUpdate);
	//m_pAnimData->m_flags &= ~FgAnimData::kTestIsInBindPose;	// Done by SetIsInBindPose()
	EngineComponents::GetAnimMgr()->SetIsInBindPose(m_pAnimData, false);
	EngineComponents::GetAnimMgr()->SetAnimationEnabled(m_pAnimData);
	if (m_pCompositeBody)
		m_pCompositeBody->SetAnimationEnabled(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Calculates m_velocityWs, m_velocityPs, m_angularVecWs, m_prevPosWs, m_prevPosPs, and m_prevCheckPointPosWs
void NdGameObject::UpdateVelocityInternal()
{
	const Point curPosWs = GetTranslation();
	const Point curPosPs = GetTranslationPs();
	const Point curCheckPointPosWs = GetLocator().TransformPoint(Point(0.0f, 0.0f, 1.0f));
	Scalar fDeltaTime = EngineComponents::GetNdFrameState()->m_clock[kGameClock].GetDeltaTimeInSeconds();

	if (fDeltaTime > SCALAR_LC(0.0f))
	{
		const Point prevPosWs = m_prevPosWs;
		const Vector deltaPosWs = curPosWs - prevPosWs;

		if (LengthSqr(deltaPosWs) >= SCALAR_LC(64.0f))
		{
			// Treat this as a teleport, and leave the previous velocity in
			// place for one frame until we can recalibrate.
		}
		else
		{
			m_velocityWs = deltaPosWs / fDeltaTime;

			const Vector deltaAtCheckPointWs = curCheckPointPosWs - m_prevCheckPointPosWs;
			const Vector curVelAtCheckPoint = deltaAtCheckPointWs / fDeltaTime;
			const Vector rotationVecAtCheckPoint = curVelAtCheckPoint - m_velocityWs;

			// VelocityP = AngularVecWs ^ (PtWs - CenterOfRotationWs);
			// => VelocityP ^ (PtWs - CenterOfRotationWs) = AngularVecWs ^ ((PtWs - CenterOfRotationWs) ^ PtWs - CenterOfRotationWs))
			// => VelocityP ^ (PtWs - CenterOfRotationWs) = AngularVecWs * (|PtWs - CenterOfRotationWs|)^2
			// think GetLocation() is the center of rotation.
			const Vector lRotationCenterToCheckPointWs = (curCheckPointPosWs - curPosWs);
			m_angularVecWs = Cross(lRotationCenterToCheckPointWs, rotationVecAtCheckPoint);	// because norm of "lRotationCenterToCheckPointWs" is 1.
		}

		const Point prevPosPs = m_prevPosPs;
		const Vector deltaPosPs = curPosPs - prevPosPs;

		if (LengthSqr(deltaPosPs) >= SCALAR_LC(64.0f))
		{
			// Treat this as a teleport, and leave the previous velocity in
			// place for one frame until we can recalibrate.
		}
		else
		{
			m_velocityPs = deltaPosPs / fDeltaTime;
		}
	}

	m_prevPosWs = curPosWs;
	m_prevPosPs = curPosPs;
	m_prevCheckPointPosWs = curCheckPointPosWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DisableUpdates()
{
	//SsAnimateDebugLog(NdGameObjectHandle(this), nullptr, ("DisableUpdates() called on %s!!!\n", GetName()));

	m_goFlags.m_updatesDisabled = true;
	SetUpdateEnabled(false);

	if (m_pPlatformControl == nullptr)
	{
		DisableAnimation();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::TestIsInBindPose() const
{
	PROFILE(Processes, TestIsInBindPose);

	ASSERT(m_pAnimData);

	if (!g_animOptimization.m_disableBindPoseOpt
	&&  !m_goFlags.m_hasHiddenJoints
	&&  MayBeInBindPose())
	{
		// temporarily turn on PostAnimBlending so we can test whether or not we're in bind pose
		m_pAnimData->m_flags |= FgAnimData::kTestIsInBindPose;
		EngineComponents::GetAnimMgr()->SetIsInBindPose(m_pAnimData, false); // also be sure animation callbacks are NOT disabled so we can do the test
	}
	else
	{
		m_pAnimData->m_flags &= ~FgAnimData::kTestIsInBindPose;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetDefaultInstanceCallbacks(AnimStateLayer* pLayer)
{
	AnimStateLayer::InstanceCallbacks callbacks;
	callbacks.m_userData	   = (uintptr_t)this;
	callbacks.m_prepare		   = AnimStateInstancePrepareCallback;
	callbacks.m_create		   = AnimStateInstanceCreateCallback;
	callbacks.m_destroy		   = AnimStateInstanceDestroyCallback;
	callbacks.m_pendingChange  = AnimStateInstancePendingChangeCallback;
	callbacks.m_alignFunc	   = AnimStateInstanceAlignFuncCallback;
	callbacks.m_ikFunc		   = AnimStateInstanceIkFuncCallback;
	callbacks.m_debugPrintFunc = AnimStateInstanceDebugPrintFuncCallback;

	pLayer->SetInstanceCallbacks(callbacks);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::AnimStateInstancePrepare(StringId64 layerId,
											StateChangeRequest::ID requestId,
											AnimStateInstance::ID instId,
											bool isTop,
											const DC::AnimState* pAnimState,
											FadeToStateParams* pParams)
{
	if (NdSubsystemMgr* pSubsystemMgr = GetSubsystemMgr())
	{
		pSubsystemMgr->InstancePrepare(layerId, requestId, instId, isTop, pAnimState, pParams);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::AnimStateInstanceCreate(AnimStateInstance* pInst)
{
	if (NdSubsystemMgr* pSubsystemMgr = GetSubsystemMgr())
	{
		pSubsystemMgr->InstanceCreate(pInst);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::AnimStateInstanceDestroy(AnimStateInstance* pInst)
{
	if (NdSubsystemMgr* pSubsystemMgr = GetSubsystemMgr())
	{
		pSubsystemMgr->InstanceDestroy(pInst);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::AnimStateInstanceAlignFunc(const AnimStateInstance* pInst,
											  const BoundFrame& prevAlign,
											  const BoundFrame& currAlign,
											  const Locator& apAlignDelta,
											  BoundFrame* pAlignOut,
											  bool debugDraw)
{
	bool ret = false;

	if (NdSubsystemMgr* pSubsystemMgr = GetSubsystemMgr())
	{
		ret = pSubsystemMgr->InstanceAlignFunc(pInst, prevAlign, currAlign, apAlignDelta, pAlignOut, debugDraw);
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::AnimStateInstanceIkFunc(const AnimStateInstance* pInst,
										   AnimPluginContext* pPluginContext,
										   const void* pParams)
{
	if (NdSubsystemMgr* pSubsystemMgr = GetSubsystemMgr())
	{
		pSubsystemMgr->InstanceIkFunc(pInst,
									  pPluginContext,
									  static_cast<const AnimSnapshotNodeSubsystemIK::SubsystemIkPluginParams*>(pParams));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::AnimStateInstanceDebugPrintFunc(const AnimStateInstance* pInst,
												   StringId64 debugType,
												   IStringBuilder* pText)
{
	if (NdSubsystemMgr* pSubsystemMgr = GetSubsystemMgr())
	{
		pSubsystemMgr->InstanceDebugPrintFunc(pInst, debugType, pText);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::AnimStateInstancePendingChange(StringId64 layerId,
												  StateChangeRequest::ID requestId,
												  StringId64 changeId,
												  int changeType)
{
	if (NdSubsystemMgr* pSubsystemMgr = GetSubsystemMgr())
	{
		pSubsystemMgr->InstancePendingChange(layerId, requestId, changeId, changeType);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::EnableUpdates()
{
	if (m_goFlags.m_updatesDisabled)
	{
		m_goFlags.m_updatesDisabled = false;
		SetUpdateEnabled(true);
	}

	if (m_pAnimData && (m_pAnimData->m_animConfig == FgAnimData::kAnimConfigNoAnimation))
	{
		EnableAnimation();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::CheckIsValidBindTarget()
{
	if (!m_bOnIsValidBindTargetCalled && IsValidBindTarget())
	{
		// call this as soon as the process is ready to init its platform and receive spawned children
		m_bOnIsValidBindTargetCalled = true;

		// Do whatever needs to be done when I become a valid parent object.
		// IMPORTANT: If you override IsValidBindTarget(), you will need to call
		// CheckIsValidBindTarget() manually when the object first becomes a valid parent.

		// NOTE: No one ever overrode these functions, so I made them no longer virtual to facilitate async updates!
		OnIsValidBindTarget();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::IsValidBindTarget(const SpawnInfo*) const
{
	RigidBodyMotionType motionType = GetPhysMotionType();
	return (motionType != kRigidBodyMotionTypeNonPhysical);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::OnIsValidBindTarget()
{
	// Notify the spline manager that I have spawned in.  Any splines parented to me will become active.
	g_splineManager.OnSpawnGameObject(*this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::AcquireWetMasks()
{
	// base class impl contains logic for everyone to acquire an actual wetmask if they need one
	// child classes can rely on this to acquire their wetmask or have their own custom code for acquiring wetmask
	if (NeedsWetmaskInstanceTexture() && !g_fxMgr.IsWetMaskValid(GetWetMaskIndex()))
	{
		g_fxMgr.AcquireWetMaskSlot(this);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::UpdateWetness()
{
	// the base class only tries to use the default "acquire wetmasks" functionality
	AcquireWetMasks();
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator NdGameObject::GetSite(StringId64 nameId) const
{
	switch (nameId.GetValue())
	{
		case SID_VAL("POI"):
			AttachIndex attach;

			if (TryFindAttachPoint(&attach, SID("targPoi")))
			{
				return GetAttachSystem()->GetLocator(attach);
			}

			const Point center =
				GetAnimData()
				? GetBoundingSphere().GetCenter()
				: GetTranslation();

			return Locator(center);
	}

	// default is to return the process location
	return GetLocator();
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdGameObject::GetSkelNameId() const
{
	// This is faster than calling GetLook
	if (m_skeletonId)
		return ResourceTable::LookupSkelNameId(m_skeletonId);

	//const StringId64 lookId = GetResolvedLookId();
	//if (lookId != INVALID_STRING_ID_64)
	//{
	//	if (LookPointer pLook = GetLook(lookId))
	//	{
	//		return pLook.GetLookSkeletonPackageName();
	//	}
	//}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pDebugInfo, delta, lowerBound, upperBound);
	RelocatePointer(m_aResolvedLookTint, delta, lowerBound, upperBound);
	RelocatePointer(m_aChildScriptEventId, delta, lowerBound, upperBound);

	for (I32F i = 0; i < kMaxInstanceTextureTables; ++i)
	{
		RelocatePointer(m_pInstanceTextureTable[i], delta, lowerBound, upperBound);
	}

	DeepRelocatePointer(m_pDrawControl, delta, lowerBound, upperBound);
	DeepRelocatePointer(m_pAnimControl, delta, lowerBound, upperBound);

	if (m_pAnimData)
	{
		FgAnimData::PluginParams* pParams = m_pAnimData->m_pPluginParams;
		while (pParams && pParams->m_pluginName != INVALID_STRING_ID_64)
		{
			if (pParams->m_pluginName == SID("joint-override")) // SPECIAL CASE because this "plugin" has no IJointModifier-derived class on which to call Relocate() so do it here.
			{
				RelocatePointer(pParams->m_pPluginData, delta, lowerBound, upperBound); // points to m_pJointOverridePluginCmd
			}
			pParams++;
		}
	}
	RelocateObject(m_pAnimData, delta, lowerBound, upperBound);

	if (m_pJointOverrideData)
	{
		RelocatePointer(m_pJointOverrideData->m_pJointIndex, delta, lowerBound, upperBound);
		RelocatePointer(m_pJointOverrideData->m_pComponentFlags, delta, lowerBound, upperBound);
		RelocatePointer(m_pJointOverrideData->m_pJointTrans, delta, lowerBound, upperBound);
		RelocatePointer(m_pJointOverrideData->m_pJointRot, delta, lowerBound, upperBound);
		RelocatePointer(m_pJointOverrideData->m_pJointScale, delta, lowerBound, upperBound);
		RelocatePointer(m_pJointOverrideData, delta, lowerBound, upperBound);
		RelocatePointer(m_pJointOverridePluginCmd, delta, lowerBound, upperBound);
	}

	RelocatePointer(m_pTargetable, delta, lowerBound, upperBound);
	DeepRelocatePointer(m_pAttackHandler, delta, lowerBound, upperBound);

	DeepRelocatePointer(m_pScriptInst, delta, lowerBound, upperBound);
	DeepRelocatePointer(m_pCompositeBody, delta, lowerBound, upperBound);

	DeepRelocatePointer(m_pPlatformControl, delta, lowerBound, upperBound);

	DeepRelocatePointer(m_pAttachSystem, delta, lowerBound, upperBound);
	DeepRelocatePointer(m_pRegionControl, delta, lowerBound, upperBound);
	DeepRelocatePointer(m_pSplineTracker, delta, lowerBound, upperBound);
	DeepRelocatePointer(m_pEffectControl, delta, lowerBound, upperBound);
	DeepRelocatePointer(m_pNetController, delta, lowerBound, upperBound);

	DeepRelocatePointer(m_pFoliageSfxController, delta, lowerBound, upperBound);
	RelocatePointer(m_paSoundBankHandles, delta, lowerBound, upperBound);

	DeepRelocatePointer(m_pSplasherController, delta, lowerBound, upperBound);

	RelocatePointer(m_pBgAttacher, delta, lowerBound, upperBound);

	DeepRelocatePointer(m_pInteractCtrl, delta, lowerBound, upperBound);

	RelocatePointer(m_pAttachmentCtrl, delta, lowerBound, upperBound);

	ParentClass::Relocate(delta, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::OnError()
{
	if (m_gridHashId != SimpleGridHashId::kInvalid)
	{
		SimpleGridHashManager::Get().Unregister(*this, m_gridHashId);
		m_gridHashId = SimpleGridHashId::kInvalid;
	}

#ifndef HEADLESS_BUILD
	if (m_pInteractCtrl)
	{
		// because we errored out, we may or may not have actually
		// registered this game object with the interactables manager
		if (m_goFlags.m_registeredInteractable)
		{
			g_ndInteractablesMgr.UnregisterInteractable(this);
			m_goFlags.m_registeredInteractable = false;
		}
	}
#endif

	ParentClass::OnError();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::OnKillProcess()
{
#ifndef HEADLESS_BUILD
	if (m_pInteractCtrl)
	{
		if (HasProcessErrorOccurred())
		{
			GAMEPLAY_ASSERT(!m_goFlags.m_registeredInteractable);
			GAMEPLAY_ASSERT(!NdGameObjectHandle(this).HandleValid());
		}
		else
		{
			GAMEPLAY_ASSERT(m_goFlags.m_registeredInteractable);
			GAMEPLAY_ASSERT(NdGameObjectHandle(this).HandleValid());
			g_ndInteractablesMgr.UnregisterInteractable(this);
			m_goFlags.m_registeredInteractable = false;
		}
	}
#endif

	// let the ActionPackMgr::Update() nuke any APs associated with processes. Any questions, ask John Bellomy
	//UnregisterAllActionPacks();

	if (m_gridHashId != SimpleGridHashId::kInvalid)
	{
		SimpleGridHashManager::Get().Unregister(*this, m_gridHashId);
		m_gridHashId = SimpleGridHashId::kInvalid;
	}

	if (m_pAttackHandler)
	{
		m_pAttackHandler->OnKillProcess(*this);
	}

	// Notify the spline manager that I am going away.  Any splines parented to me will become disabled.
	g_splineManager.OnDestroyGameObject(*this);

	if (m_goFlags.m_autoTannoy)
	{
		EngineComponents::GetAudioManager()->AutoTannoyUnregister(this);
	}

	if (m_pScriptInst)
	{
		m_pScriptInst->OnKillProcess();
		m_pScriptInst = nullptr;
	}

	if (m_pCompositeBody)
	{
		m_pCompositeBody->SetMotionType(kRigidBodyMotionTypeNonPhysical);
	}

	if (m_pDrawControl)
	{
		g_listenModeMgr.RemoveInstance(this);
	}

	if (m_pFoliageSfxController)
	{
		m_pFoliageSfxController->OnKillOwnerProcess();
	}

	EngineComponents::GetAnimMgr()->SetAnimationZombie(m_pAnimData);

	if (m_goFlags.m_isPoiLookAt)
	{
		RemoveLookAtObject(this);
	}

#ifdef HEADLESS_BUILD
	if (m_pNetLodController)
	{
		g_ndNetLodManager.DestroyController(m_pNetLodController);
		m_pNetLodController = nullptr;
	}
#endif

	if (g_fxMgr.IsWetMaskDependent(GetWetMaskIndex(), this))
	{
		g_fxMgr.RemoveWetMaskDependent(GetWetMaskIndex(), this);
	}

	ParentClass::OnKillProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimControl* NdGameObject::GetAnimControl() const
{
	ANIM_ASSERT(!EngineComponents::GetAnimMgr()->IsAnimDataLocked(m_pAnimData));
	return m_pAnimControl;
}

/// --------------------------------------------------------------------------------------------------------------- ///
FgAnimData* NdGameObject::GetAnimData()
{
	ANIM_ASSERT(!EngineComponents::GetAnimMgr()->IsAnimDataLocked(m_pAnimData));
	return m_pAnimData;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const FgAnimData* NdGameObject::GetAnimData() const
{
	ANIM_ASSERT(!EngineComponents::GetAnimMgr()->IsAnimDataLocked(m_pAnimData));
	return m_pAnimData;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const SMath::Mat34* NdGameObject::GetInvBindPoses() const
{
	const ndanim::JointHierarchy* pAnimHierarchy = GetAnimData()->m_curSkelHandle.ToArtItem()->m_pAnimHierarchy;
	return ndanim::GetInverseBindPoseTable(pAnimHierarchy);
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdGameObject::GetCurrentAnimState() const
{
	StringId64 animStateId = INVALID_STRING_ID_64;

	AnimControl* pAnimControl = GetAnimControl();
	if (pAnimControl)
	{
		AnimLayer* pAnimLayer = pAnimControl->GetLayerById(SID("base"));
		if (pAnimLayer)
		{
			if (pAnimLayer->GetType() == kAnimLayerTypeState)
			{
				AnimStateLayer* pStateLayer = static_cast<AnimStateLayer*>(pAnimLayer);
				animStateId = pStateLayer->CurrentStateId();
			}
		}
	}

	return animStateId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdGameObject::GetCurrentAnim(bool usePhaseAnim) const
{
	const AnimControl* pAnimControl = GetAnimControl();
	const AnimLayer* pAnimLayer = pAnimControl ? pAnimControl->GetLayerById(SID("base")) : nullptr;

	if (!pAnimLayer)
		return INVALID_STRING_ID_64;

	const AnimLayerType layerType = pAnimLayer->GetType();
	StringId64 animId = INVALID_STRING_ID_64;

	switch (layerType)
	{
	case kAnimLayerTypeSimple:
		{
			const AnimSimpleLayer* pSimpleLayer = static_cast<const AnimSimpleLayer*>(pAnimLayer);

			if (const AnimSimpleInstance* pInstance = pSimpleLayer->CurrentInstance())
			{
				ANIM_ASSERT(pInstance);
				animId = pInstance->GetAnimId();
			}
		}
		break;
	case kAnimLayerTypeState:
		{
			const AnimStateLayer* pStateLayer = static_cast<const AnimStateLayer*>(pAnimLayer);

			if (usePhaseAnim)
			{
				if (const AnimStateInstance* pInstance = pStateLayer->CurrentStateInstance())
				{
					animId = pInstance->GetAnimStateSnapshot().m_translatedPhaseAnimName;
				}
			}
			else if (const AnimStateInstance* pInst = pStateLayer->CurrentStateInstance())
			{
				const AnimStateSnapshot& stateSnapshot = pInst->GetAnimStateSnapshot();
				const AnimSnapshotNode* pRootNode = stateSnapshot.GetSnapshotNode(stateSnapshot.m_rootNodeIndex);
				const AnimSnapshotNodeAnimation* pAnimNode = pRootNode ? AnimSnapshotNodeAnimation::FromAnimNode(pRootNode->FindFirstNodeOfKind(&stateSnapshot, SID("AnimSnapshotNodeAnimation"))) : nullptr;
				animId = pAnimNode ? pAnimNode->m_animation : INVALID_STRING_ID_64;
			}
		}
		break;
	}

	return animId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct IsPlayingParams
{
	StringId64 m_animId;
	bool m_playing;
	float m_frame;
	float m_phase;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool AnimPlayingNodeVisitor(const AnimStateSnapshot* pSnapshot,
								   const AnimSnapshotNode* pNode,
								   const AnimSnapshotNodeBlend* pParentBlendNode,
								   float combinedBlend,
								   uintptr_t userData)
{
	const AnimSnapshotNodeAnimation* pAnimNode = AnimSnapshotNodeAnimation::FromAnimNode(pNode);
	IsPlayingParams* pParams = (IsPlayingParams*)userData;

	bool keepGoing = true;

	const ArtItemAnim* pAnim = pAnimNode->m_artItemAnimHandle.ToArtItem();
	if (pAnimNode->m_animation == pParams->m_animId && pAnim /* Make sure overlay mapping isn't broken */)
	{
		pParams->m_playing = true;
		pParams->m_phase = pAnimNode->m_phase;
		pParams->m_frame = pAnimNode->m_phase / pAnim->m_pClipData->m_phasePerFrame;
		keepGoing = false;
	}

	return keepGoing;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::IsAnimPlaying(StringId64 animName,
								 StringId64 layerName /* = INVALID_STRING_ID_64 */,
								 F32* pOutPhase /* = nullptr */,
								 F32* pOutFrame /* = nullptr */) const
{
	const AnimControl* pAnimControl = GetAnimControl();
	if (!pAnimControl)
		return false;

	const U32F numLayers = pAnimControl->GetNumLayers();

	for (U32F i = 0; i < numLayers; ++i)
	{
		const AnimLayer* pAnimLayer = pAnimControl->GetLayerByIndex(i);

		if ((nullptr == pAnimLayer) || ((layerName != INVALID_STRING_ID_64) && (pAnimLayer->GetName() != layerName)))
			continue;

		const AnimLayerType layerType = pAnimLayer->GetType();

		switch (layerType)
		{
		case kAnimLayerTypeSimple:
			{
				const AnimSimpleLayer* pSimpleLayer = static_cast<const AnimSimpleLayer*>(pAnimLayer);
				if (const AnimSimpleInstance* pInstance = pSimpleLayer->CurrentInstance())
				{
					if (pInstance->GetAnimId() == animName)
					{
						if (pOutPhase)
						{
							*pOutPhase = pInstance->GetPhase();
						}

						if (pOutFrame)
						{
							*pOutFrame = pInstance->GetPhase() / pInstance->GetClip()->m_phasePerFrame;
						}

						return true;
					}
				}
			}
			break;

		case kAnimLayerTypeState:
			{
				const AnimStateLayer* pStateLayer = static_cast<const AnimStateLayer*>(pAnimLayer);

				IsPlayingParams params;
				params.m_playing = false;
				params.m_animId = animName;

				for (int t = 0; t < pStateLayer->NumUsedTracks(); t++)
				{
					if (const AnimStateInstanceTrack* pTrack = pStateLayer->GetTrackByIndex(t))
					{
						for (int j = 0; j < pTrack->GetNumInstances(); j++)
						{
							if (const AnimStateInstance* pInst = pTrack->GetInstance(j))
							{
								const AnimStateSnapshot& stateSnapshot = pInst->GetAnimStateSnapshot();
								const AnimSnapshotNode* pRootNode = stateSnapshot.GetSnapshotNode(stateSnapshot.m_rootNodeIndex);

								pRootNode->VisitNodesOfKind(&stateSnapshot,
															SID("AnimSnapshotNodeAnimation"),
															AnimPlayingNodeVisitor,
															(uintptr_t)&params);

								if (params.m_playing)
								{
									if (pOutPhase)
									{
										*pOutPhase = params.m_phase;
									}

									if (pOutFrame)
									{
										*pOutFrame = params.m_frame;
									}
									return true;
								}
							}
						}
					}
				}

			}
			break;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::IsCurrentAnim(StringId64 animName, StringId64 layerName, F32* pOutPhase /*= nullptr*/, F32* pOutFrame /*= nullptr*/) const
{
	const AnimControl* pAnimControl = GetAnimControl();
	if (!pAnimControl)
		return false;

	const U32F numLayers = pAnimControl->GetNumLayers();

	for (U32F i = 0; i < numLayers; ++i)
	{
		const AnimLayer* pAnimLayer = pAnimControl->GetLayerByIndex(i);

		if ((nullptr == pAnimLayer) || ((layerName != INVALID_STRING_ID_64) && (pAnimLayer->GetName() != layerName)))
			continue;

		const AnimLayerType layerType = pAnimLayer->GetType();

		switch (layerType)
		{
		case kAnimLayerTypeSimple:
		{
			const AnimSimpleLayer* pSimpleLayer = static_cast<const AnimSimpleLayer*>(pAnimLayer);
			if (const AnimSimpleInstance* pInstance = pSimpleLayer->CurrentInstance())
			{
				if (pInstance->GetAnimId() == animName)
				{
					if (pOutPhase)
					{
						*pOutPhase = pInstance->GetPhase();
					}

					if (pOutFrame)
					{
						*pOutFrame = pInstance->GetPhase() / pInstance->GetClip()->m_phasePerFrame;
					}

					return true;
				}
			}
		}
		break;

		case kAnimLayerTypeState:
		{
			const AnimStateLayer* pStateLayer = static_cast<const AnimStateLayer*>(pAnimLayer);

			IsPlayingParams params;
			params.m_playing = false;
			params.m_animId = animName;

			if (const AnimStateInstance* pInst = pStateLayer->CurrentStateInstance())
			{
				const AnimStateSnapshot& stateSnapshot = pInst->GetAnimStateSnapshot();
				const AnimSnapshotNode* pRootNode = stateSnapshot.GetSnapshotNode(stateSnapshot.m_rootNodeIndex);

				pRootNode->VisitNodesOfKind(&stateSnapshot,
					SID("AnimSnapshotNodeAnimation"),
					AnimPlayingNodeVisitor,
					(uintptr_t)&params);

				if (params.m_playing)
				{
					if (pOutPhase)
					{
						*pOutPhase = params.m_phase;
					}

					if (pOutFrame)
					{
						*pOutFrame = params.m_frame;
					}
					return true;
				}
			}

		}
		break;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
FgAnimData* NdGameObject::AllocateAnimData(const ArtItemSkeletonHandle artItemSkelHandle)
{
	FgAnimData* pAnimData = EngineComponents::GetAnimMgr()->AllocateAnimData(GetMaxNumDrivenInstances(), this);

	if (pAnimData)
		pAnimData->Init(artItemSkelHandle, GetLocator().AsTransform(), JointCache::kConfigNormal);

	return pAnimData;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdGameObject::ConfigureAnimData(FgAnimData* pAnimData, const ResolvedBoundingData& bounds)
{
	bool found = false;
	Point zero(kZero);

	m_pAnimData->m_visSphere			= bounds.m_visSphere.GetVec4();
	m_pAnimData->m_visSphereJointIndex	= bounds.m_visSphereJointIndex;

	m_pAnimData->m_useBoundingBox		= bounds.m_useBoundingBox;
	if (bounds.m_useBoundingBox)
	{
		m_pAnimData->m_visAabb					= bounds.m_aabb;
		m_pAnimData->m_visSphereJointIndex		= bounds.m_jointIndex;
		m_pAnimData->m_dynamicBoundingBoxPad	= bounds.m_dynamicPad;

		// In case the Aabb is dynamic it will be (-FLT_MAX -> FLT_MAX) at this point because we havn't updated it yet
		// and if we try later to calculate the bounding sphere radius from that we would get QNaN
		// So just set it to empty for now
		if (!IsFinite(LengthSqr(m_pAnimData->m_visAabb.GetSize())))
		{
			m_pAnimData->m_visAabb = Aabb(kOrigin, kOrigin);
		}
	}

	// Let's assume that all characters if we have a large number of joints, that we will also have more complicated
	if (IsKindOf(SID("Character")))
	{
		pAnimData->m_useLargeAnimCommandList = true;
	}

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 NdGameObject::GetEffectListSize() const
{
	const U32 effectListSize = m_goFlags.m_useLargeEffectList ? 60 : 20;
	return effectListSize;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdGameObject::InitAnimControl(const ProcessSpawnInfo& spawn)
{
	PROFILE(Processes, InitAnimControl);

	m_pAnimControl = NDI_NEW AnimControl;
	ANIM_ASSERT(m_pAnimControl && m_pAnimData);
	m_pAnimData->m_pAnimControl = m_pAnimControl;

	U32 effectListSize = 0;
	if (NeedsEffectControl(spawn))
	{
		effectListSize = GetEffectListSize();
	}

	m_pAnimControl->Init(m_pAnimData,
						 effectListSize,
						 m_gameObjectConfig.m_animControlMaxLayers,
						 this);

	Err result = SetupAnimControl(m_pAnimControl);
	if (result.Failed())
	{
		GoError(result.GetMessageText());
		return result;
	}

	if (g_animOptions.m_dumpArtItemInfoOnLogin)
	{
		// Intentionally no filtering through the AnimTable here...
//		AnimMasterTable::DumpArtAnimInfo(m_pAnimData->m_curSkelHandle.ToArtItem()->m_skelId, g_animOptions.m_dumpKeyFramedJointsPerAnim);
	}

	if (AnimOverlays* pOverlays = m_pAnimControl->GetAnimOverlays())
	{
		const char* baseName	  = GetOverlayBaseName();
		const StringId64 lookName = GetResolvedLookId();
		LookPointer pLook		  = GetLook(lookName);

		const DC::Look2* pLookDcDef = pLook ? pLook.AsLook2() : nullptr;
		const char* lookNameStr		= pLook ? pLook.GetName() : nullptr;
		const char* lookSkelStr = pLookDcDef ? pLookDcDef->m_skelName.m_string : nullptr;

		if (pLookDcDef && !pLookDcDef->m_customAnimOverlay.IsEmpty() && baseName)
		{
			StringBuilder<256> overlayName;
			overlayName.append_format("anim-overlay-%s-look-group-%s",
									  baseName,
									  pLookDcDef->m_customAnimOverlay.GetString());

			const StringId64 overlaySetId = StringToStringId64(overlayName.c_str());
			pOverlays->SetOverlaySet(overlaySetId, true);
		}

		if (lookNameStr && baseName)
		{
			StringBuilder<256> overlayName;
			overlayName.append_format("anim-overlay-%s-look-%s", baseName, lookNameStr);

			const StringId64 overlaySetId = StringToStringId64(overlayName.c_str());
			pOverlays->SetOverlaySet(overlaySetId, true);
		}

		if (lookSkelStr && baseName)
		{
			StringBuilder<256> overlayName;
			overlayName.append_format("anim-overlay-%s-skel-%s", baseName, lookSkelStr);

			const StringId64 overlaySetId = StringToStringId64(overlayName.c_str());
			pOverlays->SetOverlaySet(overlaySetId, true);
		}
	}

	{
		StringId64 defaultStateBlendOverlayId = GetDefaultStateBlendOverlayId();
		if (defaultStateBlendOverlayId != INVALID_STRING_ID_64)
			m_pAnimControl->SetDefaultStateBlendOverlay(defaultStateBlendOverlayId);

		StringId64 defaultAnimBlendOverlayId = GetDefaultAnimBlendOverlayId();
		if (defaultAnimBlendOverlayId != INVALID_STRING_ID_64)
			m_pAnimControl->SetDefaultAnimBlendOverlay(defaultAnimBlendOverlayId);
	}

	return Err::kOK;
}


/// --------------------------------------------------------------------------------------------------------------- ///
Err NdGameObject::SetupAnimControl(AnimControl* pAnimControl)
{
	ANIM_ASSERT(m_pAnimData);
 	const I32 foundAnimations = ResourceTable::GetNumAnimationsForSkelId(m_pAnimData->m_curSkelHandle.ToArtItem()->m_skelId);
 	const U32F numNeededSimpleLayerInstances = static_cast<U32>((foundAnimations <= 1) ? 1 : 2);

 	pAnimControl->AllocateSimpleLayer(SID("base"), ndanim::kBlendSlerp, 0, numNeededSimpleLayerInstances);

	// The fade was set when allocating the layer, no need to do it again.
	AnimLayer* pBaseLayer = pAnimControl->CreateSimpleLayer(SID("base"));
	//pBaseLayer->Fade(1.0, 0.0f);

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::ChangeAnimConfig(FgAnimData::AnimConfig config)
{
	// It's important to use this one rather than directly manipulating the FgAnimData
	// so that the the RigidBody or CompositeBody will get notified properly.

	m_pAnimData->ChangeAnimConfig(config);

	CompositeBody* pCompositeBody = GetCompositeBody();
	if (pCompositeBody)
	{
		pCompositeBody->OnChangeAnimConfig();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::IsUsingApOrigin() const
{
	if (GetAnimControl())
	{
		const AnimLayer* pBaseLayer = GetAnimControl()->GetLayerById(SID("base"));
		if (pBaseLayer)
		{
			const AnimLayerType type = pBaseLayer->GetType();

			if (type == kAnimLayerTypeSimple)
			{
				const AnimSimpleLayer* pLayer = static_cast<const AnimSimpleLayer*>(pBaseLayer);
				if (pLayer->GetNumInstances() > 0)
					return pLayer->GetInstance(0)->IsAlignedToActionPackOrigin();
			}
			else if (type == kAnimLayerTypeState)
			{
				const AnimStateLayer* pLayer = static_cast<const AnimStateLayer*>(pBaseLayer);
				if (pLayer->CurrentStateInstance())
					return pLayer->CurrentStateInstance()->IsApLocatorActive();
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::GetApOrigin(BoundFrame& apRef) const
{
	if (GetAnimControl())
	{
		const AnimLayer* pBaseLayer = GetAnimControl()->GetLayerById(SID("base"));
		if (pBaseLayer)
		{
			const AnimLayerType type = pBaseLayer->GetType();

			if (type == kAnimLayerTypeSimple)
			{
				return static_cast<const AnimSimpleLayer*>(pBaseLayer)->GetApRefFromCurrentInstance(apRef);
			}
			else if (type == kAnimLayerTypeState)
			{
				return static_cast<const AnimStateLayer*>(pBaseLayer)->GetApRefFromCurrentState(apRef);
			}
		}
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::GetApOrigin(Locator& apRef) const	// deprecated
{
	BoundFrame apRefBf;
	bool valid = GetApOrigin(apRefBf);
	apRef = apRefBf.GetLocator();
	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void SetInstancePropsFromLook(FgInstance* pInst, uintptr_t data)
{
	const ResolvedLook::MeshDesc* pData = reinterpret_cast<const ResolvedLook::MeshDesc*>(data);

	pInst->m_lookBodyPart = pData->m_bodyPart;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdGameObject::InitializeDrawControl(const ProcessSpawnInfo& spawn, const ResolvedLook& resolvedLook)
{
	if (spawn.m_pParent && spawn.m_pParent->IsKindOf(SID("ModelViewerModel")))
	{
		m_pDrawControl->SetFlag(IDrawControl::kOnlyAddLod0, true);
	}

	m_pDrawControl->SetFlag(IDrawControl::kSkipOddLods, resolvedLook.m_skipOddLods);
	m_pDrawControl->SetFlag(IDrawControl::kCastsShadows, (spawn.GetData<I32>(SID("casts-shadows"), 1) != 0));
	m_pDrawControl->SetFlag(IDrawControl::kCastsLocalShadows, (spawn.GetData<I32>(SID("casts-local-shadows"), 1) != 0));
	m_pDrawControl->SetFlag(IDrawControl::kPerBoneVolProbeLighting, (spawn.GetData<bool>(SID("per-bone-vol-probe-lighting"), false) != 0));
	m_pDrawControl->SetFlag(IDrawControl::kGhostingID, (spawn.GetData<I32>(SID("ghosting-id"), 0) == 1));
	m_pDrawControl->SetFlag(IDrawControl::kGhostingIDBg, (spawn.GetData<I32>(SID("ghosting-id"), 0) == 2));
	m_pDrawControl->SetFlag(IDrawControl::kFixAlphaSortWithWater, (spawn.GetData<bool>(SID("fix-alpha-sort-with-water"), false) != 0));
	m_pDrawControl->SetDisappearDistOverride(spawn.GetData<F32>(SID("per-instance-disappear-dist"), 0.f));

	bool isPlayer = IsKindOf(SID("Player"));

	const bool startMeshesHidden = (spawn.GetData(SID("spawn-hidden"), 0) != 0);
	const bool startObjectHidden = (spawn.GetData(SID("spawn-object-hidden"), 0) != 0);

	for (U32F i = 0; i < resolvedLook.m_numMeshes; ++i)
	{
		const StringId64 meshId				= resolvedLook.m_aMeshDesc[i].m_meshId;
		const ArtItemGeo* pArtItemGeo		= resolvedLook.m_aMeshDesc[i].m_geoLod0.ToArtItem();

		ASSERT(meshId != INVALID_STRING_ID_64);
		ASSERT(pArtItemGeo); // nullptr art items will be rejected by ResolvedLook

		const IDrawControl::MeshDesc& meshDesc = resolvedLook.m_aMeshDesc[i];

		if ((meshDesc.m_meshFlags & IDrawControl::Mesh::kPlayerOnly) && !isPlayer)
			continue;
		if ((meshDesc.m_meshFlags & IDrawControl::Mesh::kNonPlayerOnly) && isPlayer)
			continue;

		const Err result = m_pDrawControl->AddMesh(meshDesc, GetAmbientOccludersObjectId(), &resolvedLook, spawn.GetName(), SetInstancePropsFromLook, reinterpret_cast<uintptr_t>(&meshDesc));
		if (result.Failed())
		{
			MsgErr("Could not add mesh '%s' to the draw control!\n", DevKitOnly_StringIdToString(meshId));
			if (result == Err::kErrBadData)
				return result;
			continue;
		}

		if (startMeshesHidden || resolvedLook.m_aHidden.IsBitSet(i)) // initially hidden
		{
			m_pDrawControl->HideMesh(meshId);
		}
		else
		{
			m_pDrawControl->ShowMesh(meshId);
		}
	}

	if (startObjectHidden)
	{
		m_pDrawControl->HideObject();
	}

	if (resolvedLook.HasTints())
	{
		// this array caches the original tints as specified in the look... only used for the m_drawDiffuseTintChannels debug-draw mode!
		m_aResolvedLookTint = NDI_NEW Color[resolvedLook.m_numMeshes * kInstParamDiffuseTintCount];
		if (m_aResolvedLookTint)
		{
			for (U32F i = 0; i < resolvedLook.m_numMeshes; ++i)
			{
				memcpy(&m_aResolvedLookTint[i * kInstParamDiffuseTintCount], resolvedLook.m_aMeshDesc[i].m_aTint, kInstParamDiffuseTintCount * sizeof(m_aResolvedLookTint[0]));
			}
		}
	}
	else
	{
		ASSERT(m_aResolvedLookTint == nullptr);
	}

	ASSERT(!m_goFlags.m_instanceAoEnabled);
	if (resolvedLook.m_enableInstanceAo)
	{
		AllocateShaderInstanceParams();

		const bool instanceAoDesired = !resolvedLook.m_aInstanceAo.AreAllBitsClear();
		m_goFlags.m_instanceAoEnabled = true;
		m_goFlags.m_instanceAoDesired = instanceAoDesired;
		m_goFlags.m_instanceAoCurrent = !instanceAoDesired; // force update on first frame
	}

	// stomp an ambient-scale on our draw lods if we're tagged
	m_initialAmbientScale = spawn.GetData<float>(SID("ambient-scale"), -1.0f);
	if (m_initialAmbientScale != -1.0f)
	{
		for (U32F i = 0; i < m_pDrawControl->GetNumMeshes(); ++i)
		{
			IDrawControl::Mesh& mesh = m_pDrawControl->GetMesh(i);
			for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
			{
				IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];
				currentLod.m_pInstance->SetAmbientScale(m_initialAmbientScale);
			}
		}
	}

	if (resolvedLook.m_numMissingMeshes)
	{
		if (!m_pDebugInfo)
		{
			m_pDebugInfo = NDI_NEW LookDebugInfo;
		}
		for (U32 i = 0; m_pDebugInfo && i < resolvedLook.m_numMissingMeshes && m_pDebugInfo->m_missingMeshCount < LookDebugInfo::kMaxMissingMeshes; ++i)
		{
			const U32 j = (ResolvedLook::kMaxMeshes - 1) - i; // read from the end for missing meshes!
			const StringId64 missingMeshId = resolvedLook.m_aMeshDesc[j].m_meshId;
			m_pDebugInfo->m_aMissingMeshId[m_pDebugInfo->m_missingMeshCount++] = missingMeshId;
		}
	}

	if (m_paSoundBankHandles != nullptr)
	{
		// Dereference any current sound bank handles.
		I32F i;
		for (i = 0; i < ARM_NUM_LOOK_SOUND_BANKS; i++)
		{
			if (m_paSoundBankHandles[i].Valid())
			{
				EngineComponents::GetAudioManager()->UnloadSoundBank(m_paSoundBankHandles[i]);
				m_paSoundBankHandles[i] = VbHandle();
			}
		}

		// Create references for any new sound bank handles.
		const I32F maxSoundBanks = ResolvedLook::kMaxSoundBanks;
		I32F iNextBank = 0;
		for (i = 0; (i < maxSoundBanks) && (iNextBank < ARM_NUM_LOOK_SOUND_BANKS); i++)
		{
			if ((resolvedLook.m_aSoundBanks[i] != nullptr) && (*resolvedLook.m_aSoundBanks[i] != '\0'))
			{
				if (EngineComponents::GetAudioManager()->m_bLogLevelObjectBanks)
					MsgAudio("Loading sound bank (GameObject '%s', look='%s'): '%s'\n", GetName(), DevKitOnly_StringIdToStringOrHex(GetLookId()), resolvedLook.m_aSoundBanks[i]);

				VbHandle hBank = EngineComponents::GetAudioManager()->LoadSoundBank(resolvedLook.m_aSoundBanks[i]);
				if (hBank.Valid())
				{
					m_paSoundBankHandles[iNextBank] = hBank;
					iNextBank++;
				}
			}
		}
	}

	if (resolvedLook.m_uniformScale != 1.0f)
	{
		SetUniformScale(resolvedLook.m_uniformScale);
	}

	if (resolvedLook.m_updateSpecCubemapEveryFrame)
	{
		m_goFlags.m_updateSpecCubemapEveryFrame = true;
	}

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetHiresAnimGeo(bool wantHiresAnimGeo)
{
	IDrawControl* pDrawControl = GetDrawControl();
	FgAnimData* pAnimData = GetAnimData();
	if (pDrawControl && pAnimData)
	{
		// Tell the draw control to show the special LOD chain used for hires animations, and
		// (HACK!) force the animation to think that it requires hires LODs... Ultimately we should
		// tag the animations as "hires" in Builder, and automagically hide or show the draw
		// control's hires LODs based on whether or not any hires animations appear in the blend.

		if (wantHiresAnimGeo)
		{
			pDrawControl->ShowHiresAnimGeo();
			pAnimData->SetRequireHiresAnimGeo();
		}
		else
		{
			pDrawControl->HideHiresAnimGeo();
			pAnimData->ClearRequireHiresAnimGeo();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::IsHiresAnimGeoShown() const
{
	IDrawControl* pDrawControl = GetDrawControl();
	return (pDrawControl) ? pDrawControl->IsHiresAnimGeoShown() : false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetupMeshAudioConfigForLook(const ResolvedLook& resolvedLook)
{
	// ***
	// *** WARNING: This function should ONLY be called during process init!
	// ***
	//
	// TODO: Right now this is also called when swapping looks, and this results in a number of bugs caused
	// by the fact that this code was never intended to be run multiple times per process init.  Bugs such as:
	//
	//   * Foliage sfx controller can't be reinitialized properly
	//   * GO flags that are meant to be unique based on the look are not cleared and reinitialized, only added to
	//
	// Someone FIX ME!!!

	// NOTE: This should be called after InitializeDrawControl() and after the effect control is initialized.

	(void)resolvedLook;

	// If a foliage sfx controller has not yet been created, attempt to create one now.
	//
	// TODO: This isn't quite right; if the look changes post-init, this will need to be dealt with,
	// but we can't call this function outside of init because it allocates from process memory, which
	// only works during init.
	//
	// For now, skip creating the controller if we're post-init.
	if ((m_pFoliageSfxController == nullptr) && !IsInitialized())
	{
		StringId64 lookId = GetBaseLookId();
		if (lookId == INVALID_STRING_ID_64)
		{
			lookId = GetLookId();
		}
		m_pFoliageSfxController = EngineComponents::GetNdGameInfo()->CreateFoliageSfxController(*this, /*spawn, */lookId);
	}

	// Clear gas mask flag.
	m_goFlags.m_gasMaskMesh = false;
	m_gasMaskFutz = INVALID_STRING_ID_64;
	m_goFlags.m_hasBackpack = false;

	IDrawControl* pDrawControl = GetDrawControl();
	if (pDrawControl && pDrawControl->GetNumMeshes() > 0)
	{
		// do we have a backpack
		for (int iMesh = 0; iMesh < resolvedLook.m_numMeshes; iMesh++)
		{
			if (resolvedLook.m_aMeshDesc[iMesh].m_bodyPart == DC::kLook2BodyPartBackpack)
			{
				m_goFlags.m_hasBackpack = true;
			}
		}
	}

	IEffectControl* pEffControl = GetEffectControl();
	if ((pDrawControl != nullptr) && (pEffControl != nullptr))
	{
		pEffControl->ResetMeshAudioConfig();

		// Add any mesh audio configurations to the effect control.
		if (const DC::Map* pMeshAudioCfgMap = ScriptManager::LookupInModule<DC::Map>(SID("*mesh-audio-config*"), SID("mesh-audio-config")))
		{
			const U32F numMeshes = pDrawControl->GetNumMeshes();
			for (U32F i = 0; i < numMeshes; ++i)
			{
				const IDrawControl::Mesh& mesh = pDrawControl->GetMesh(i);
				const ArtItemGeo* pGeo = mesh.m_lod[0].m_artItemGeo.ToArtItem();
				const StringId64 meshNameId = pGeo->GetNameId();
				const DC::MeshAudioConfig* pConfig = ScriptManager::MapLookup<DC::MeshAudioConfig>(pMeshAudioCfgMap, meshNameId);
				if (pConfig)
				{
					DC::Look2BodyPart part = DC::kLook2BodyPartLegs;
					{
						for (int iMesh = 0; iMesh < resolvedLook.m_numMeshes; iMesh++)
						{
							if (resolvedLook.m_aMeshDesc[iMesh].m_meshId == meshNameId)
							{
								part = resolvedLook.m_aMeshDesc[iMesh].m_bodyPart;
								break;
							}
						}
					}
					pEffControl->AddMeshAudioConfig(pConfig, part);

					// Check for mesh options.
					if ((pConfig->m_options & DC::kMeshAudioOptionsGasMask) != 0U)
					{
						m_goFlags.m_gasMaskMesh = true;
						if (pConfig->m_gasMaskFutz != INVALID_STRING_ID_64)
						{
							m_gasMaskFutz = pConfig->m_gasMaskFutz;
						}
					}
				}
			}
		}

		// Add any mesh audio configurations to the effect control.
		if (const DC::Map* pLookAudioCfgMap = ScriptManager::LookupInModule<DC::Map>(SID("*look-audio-config*"), SID("mesh-audio-config")))
		{
			const StringId64 lookId = GetLookId();
			const DC::MeshAudioConfig* pConfig = ScriptManager::MapLookup<DC::MeshAudioConfig>(pLookAudioCfgMap, lookId);
			if (pConfig)
			{
				pEffControl->AddMeshAudioConfig(pConfig, DC::kLook2BodyPartLegs);

				// Check for mesh options.
				if ((pConfig->m_options & DC::kMeshAudioOptionsGasMask) != 0U)
				{
					m_goFlags.m_gasMaskMesh = true;
					if (pConfig->m_gasMaskFutz != INVALID_STRING_ID_64)
					{
						m_gasMaskFutz = pConfig->m_gasMaskFutz;
					}
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetTargetable(Targetable* p)
{
	m_pTargetable = p;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Targetable* NdGameObject::GetTargetable() const
{
	return m_pTargetable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Targetable* NdGameObject::GetTargetable()
{
	return m_pTargetable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetTurnReticleRed(bool bTurnRed)
{
	// make sure flags set on renderables
	if (bTurnRed)
		GetDrawControl()->SetInstanceFlag(FgInstance::kEnemy);
	else
		GetDrawControl()->ClearInstanceFlag(FgInstance::kEnemy);

	m_goFlags.m_bTurnReticleRed = bTurnRed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetTrackableByObjectId(bool bValue)
{
	// make sure flags set on renderables
	if (bValue)
		GetDrawControl()->SetInstanceFlag(FgInstance::kTrackByObjectId);
	else
		GetDrawControl()->ClearInstanceFlag(FgInstance::kTrackByObjectId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::MakeTargetable()
{
	ASSERT(!GetTargetable());
	SetTargetable(NDI_NEW Targetable(this));
	const AttachSystem* pAttachSystem = GetAttachSystem();
	if (pAttachSystem)
	{
		Targetable* pTargetable = GetTargetable();
		for (U32 i = 0; i < pAttachSystem->GetPointCount(); i++)
		{
			pTargetable->AddSpot(i, TargetableSpot((U8)i));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::HandleTriggeredEffect(const EffectAnimInfo* pEffectAnimInfo)
{
	switch (pEffectAnimInfo->m_pEffect->GetNameId().GetValue())
	{
	case SID_VAL("script-event"):
		{
			if (const EffectAnimEntryTag* pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("name")))
			{
				const StringId64 nameValueId = pTag->GetValueAsStringId();
				if (nameValueId != INVALID_STRING_ID_64)
				{
					g_ssMgr.BroadcastEvent(nameValueId, BoxedValue(GetUserId()));
				}
			}
		}
		break;

	case SID_VAL("send-event"):
		{
			const EffectAnimEntryTag* pMessageTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("message"));
			const EffectAnimEntryTag* pReceiverTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("receiver"));
			if (pMessageTag && pReceiverTag)
			{
				const StringId64 msg = pMessageTag->GetValueAsStringId();
				const StringId64 receiver = pReceiverTag->GetValueAsStringId();
				if (Process* pReceiver = EngineComponents::GetProcessMgr()->LookupProcessByUserId(receiver))
				{
					PostEvent(msg, pReceiver);
				}
			}
		}
		break;

	case SID_VAL("rumble-effect"):
		{
			if (const EffectAnimEntryTag* pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("name")))
			{
				const StringId64 nameValueId = pTag->GetValueAsStringId();
				if (nameValueId != INVALID_STRING_ID_64)
				{
					DoRumble(0, nameValueId, 1.0f, -1.0f, false, pEffectAnimInfo->m_pEffect->GetTagByName(SID("preserve"))?SID("preserve"):INVALID_STRING_ID_64);
				}
			}
		}
		break;

	case SID_VAL("splasher-object-state"):
		{
			if (const EffectAnimEntryTag* pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("state")))
				m_effSplasherObjectStateNext = pTag->GetValueAsStringId();
		}
		break;

	case SID_VAL("cloth-setting"):
		{
			const EffectAnimEntryTag* pClothMeshTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("cloth-mesh"));
			const StringId64 clothMesh = pClothMeshTag ? pClothMeshTag->GetValueAsStringId() : INVALID_STRING_ID_64;

			const EffectAnimEntryTag* pBlendTimeTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("blend-time"));
			float blendTime = pBlendTimeTag ? pBlendTimeTag->GetValueAsF32() : 0.5f;

			const EffectAnimEntryTag* pCompressPctTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("allow-compress-pct"));
			if (pCompressPctTag)
			{
				float pct = pCompressPctTag->GetValueAsF32();
				SendEvent(SID("cloth-allow-compress-pct"), this, clothMesh, pct, blendTime);
			}

			const EffectAnimEntryTag* pForceSkinningTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("force-skinning-pct"));
			if (pForceSkinningTag)
			{
				float pct = pForceSkinningTag->GetValueAsF32();
				SendEvent(SID("cloth-force-skinning-pct"), this, clothMesh, pct, blendTime);
			}

			const EffectAnimEntryTag* pMinWorldMovementTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("min-world-movement"));
			if (pMinWorldMovementTag)
			{
				float worldMovement = pMinWorldMovementTag->GetValueAsF32();
				SendEvent(SID("cloth-min-world-movement"), this, clothMesh, worldMovement, blendTime);
			}

			const EffectAnimEntryTag* pMaxWorldMovementTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("max-world-movement"));
			if (pMaxWorldMovementTag)
			{
				float worldMovement = pMaxWorldMovementTag->GetValueAsF32();
				SendEvent(SID("cloth-max-world-movement"), this, clothMesh, worldMovement, blendTime);
			}

			const EffectAnimEntryTag* pMinSkinDistTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("min-skin-dist"));
			if (pMinSkinDistTag)
			{
				float skinDist = pMinSkinDistTag->GetValueAsF32();
				SendEvent(SID("cloth-min-skin-dist"), this, clothMesh, skinDist, blendTime);
			}

			const EffectAnimEntryTag* pMaxSkinDistTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("max-skin-dist"));
			if (pMaxSkinDistTag)
			{
				float skinDist = pMaxSkinDistTag->GetValueAsF32();
				SendEvent(SID("cloth-max-skin-dist"), this, clothMesh, skinDist, blendTime);
			}

			const EffectAnimEntryTag* pBendingStiffnessTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("bending-stiffness"));
			if (pBendingStiffnessTag)
			{
				float bendingStiffness = pBendingStiffnessTag->GetValueAsF32();
				SendEvent(SID("cloth-set-bending-stiffness"), this, clothMesh, bendingStiffness, blendTime);
			}

			const EffectAnimEntryTag* pDisableCollisionTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("disable-collision"));
			if (pDisableCollisionTag)
			{
				int disableCollision = pDisableCollisionTag->GetValueAsI32();
				SendEvent(SID("cloth-setting-disable-collision"), this, clothMesh, disableCollision ? 1.0f : 0.0f, blendTime);
			}

			const EffectAnimEntryTag* pDisableRebootTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("disable-reboot"));
			if (pDisableRebootTag)
			{
				int disableReboot = pDisableRebootTag->GetValueAsI32();
				SendEvent(SID("cloth-setting-disable-reboot"), this, clothMesh, disableReboot ? 1.0f : 0.0f, blendTime);
			}

			const EffectAnimEntryTag* pColliderMultTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("collider-mult"));
			if (pColliderMultTag)
			{
				float colliderMult = pColliderMultTag->GetValueAsF32();
				SendEvent(SID("cloth-set-collider-mult"), this, clothMesh, colliderMult, blendTime);
			}

			const EffectAnimEntryTag* pStretchinessMultTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("stretchiness-mult"));
			if (pStretchinessMultTag)
			{
				float stretchinessMult = pStretchinessMultTag->GetValueAsF32();
				SendEvent(SID("cloth-set-stretchiness-mult"), this, clothMesh, stretchinessMult, blendTime);
			}
		}
		break;

	case SID_VAL("physicalize"):
		{
			SendEvent(SID("physicalize"), this);
			return;
		}
		break;

	case SID_VAL("blink"):
		{
			if (BlinkController* pBlinkController = GetBlinkController())
			{
				pBlinkController->Blink();
			}
		}
		break;

	default:
		{
			EffectControlSpawnInfo effInfo;
			PopulateEffectControlSpawnInfo(pEffectAnimInfo, effInfo);
			m_pEffectControl->FireEffect(&effInfo);
		}
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::PopulateEffectControlSpawnInfo(const EffectAnimInfo* pEffectAnimInfo, EffectControlSpawnInfo& effInfo)
{
	m_pEffectControl->PopulateSpawnInfo(effInfo, pEffectAnimInfo, this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
IDrawControl* NdGameObject::GetDrawControl() const
{
	return m_pDrawControl;
}


/// --------------------------------------------------------------------------------------------------------------- ///
const Sphere NdGameObject::GetBoundingSphere() const
{
	ASSERT(m_pAnimData);
	return Sphere(m_pAnimData->m_pBoundingInfo->m_jointBoundingSphere);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetRegionControl(RegionControl* p)
{
	m_pRegionControl = p;
}

/// --------------------------------------------------------------------------------------------------------------- ///
RegionControl* NdGameObject::GetRegionControl() const
{
	return m_pRegionControl;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::IsInRegion(StringId64 regionId) const
{
	PROFILE(Processes, NdGo_IsInRegion);

	if (const RegionControl* pRegionControl = GetRegionControl())
	{
		return pRegionControl->IsInRegion(regionId);
	}

	return g_regionManager.IsInRegion(GetRegionActorId(), GetTranslation(), 0.0f, regionId, this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdGameObject::GetRegionActorId() const
{
	if (const RegionControl* pRegionControl = GetRegionControl())
	{
		return pRegionControl->GetActorId();
	}
	else if (m_charterRegionActorId != INVALID_STRING_ID_64)
	{
		return m_charterRegionActorId;
	}
	else
	{
		return GetUserId();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DetachFromLevel()
{
	if (m_pRegionControl && GetAssociatedLevel())
	{
		m_pRegionControl->DetachFromRegionsInLevel(GetAssociatedLevel(), this);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::PostAnimUpdate_Async()
{
	m_effSplasherObjectState = m_effSplasherObjectStateNext;
	m_effSplasherObjectStateNext = INVALID_STRING_ID_64;

	UpdateJointShaderDrivers();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::PostAnimBlending_Async()
{
	PROFILE_STRINGID(Processes, NdGo_PostAnimBlending_Async, GetTypeNameId());

	if (m_hasJointWind)
	{
		UpdateJointWind();
	}

	CompositeBody* pCompositeBody = GetCompositeBody();
	if (pCompositeBody)
	{
		pCompositeBody->PostAnimBlending();
	}

	RigidBody* pRigidBody = GetRigidBody();
	if (pRigidBody)
	{
		pRigidBody->PostAnimBlending();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT(SplasherJob)
{
	NdGameObject* pGo = (NdGameObject*)jobParam;
	PROFILE_JOB_SET_STRINGID(pGo->GetUserId());

	pGo->GetSplasherController()->Update();
}

/// --------------------------------------------------------------------------------------------------------------- ///
// static
void NdGameObject::InitSplasherJobs()
{
	s_pSplasherJobCounter = ndjob::AllocateCounter(FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// static
void NdGameObject::DoneSplasherJobs()
{
	ndjob::FreeCounter(s_pSplasherJobCounter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// static
void NdGameObject::WaitForAllSplasherJobs()
{
	ndjob::WaitForCounter(s_pSplasherJobCounter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::PostJointUpdate_Async()
{
	PROFILE_STRINGID(Processes, NdGo_PostJointUpdate_Async, GetTypeNameId());

	if (m_pSplasherController && ShouldUpdateSplashers())
	{
		s_pSplasherJobCounter->Increment();
		ndjob::JobDecl jobDecl(SplasherJob, (uintptr_t)this);
		jobDecl.m_associatedCounter = s_pSplasherJobCounter;
		ndjob::RunJobs(&jobDecl, 1, nullptr, FILE_LINE_FUNC, ndjob::Priority::kGameFrameBelowNormal);
	}

	if (AnimControl* pAnimControl = GetAnimControl())
	{
		PROFILE(Processes, GetTriggeredEffects);
		TriggeredEffects(pAnimControl->GetTriggeredEffects());
	}

	if (m_pBgAttacher)
	{
		// @ASYNC-TODO: Not safe if multiple game objects update the same level's loc!
		m_pBgAttacher->UpdateLevelLoc(this);
	}

	UpdateVelocityInternal();

	CompositeBody* pCompositeBody = GetCompositeBody();
	if (pCompositeBody)
	{
		PROFILE(Processes, CompositeBody_PointJointUpdate);
		pCompositeBody->PostJointUpdate();
	}

	RigidBody* pRigidBody = GetRigidBody();
	if (pRigidBody)
	{
		PROFILE(Processes, RigidBody_PostJointUpdate);
		pRigidBody->PostJointUpdate();
	}

	if (m_pPlatformControl)
	{
		PROFILE(Processes, PJU_PlatformCtrl);
		m_pPlatformControl->PostJointUpdate(*this);
	}

	{
		PROFILE(Processes, NdGo_UpdateNavBlocker);
		UpdateNavBlocker();
	}

	if (m_bUpdateCoverActionPacks)
	{
		const bool isStandingPlatform = (m_pPlatformControl && (m_pPlatformControl->GetNavMeshCount() > 0));

		if (!isStandingPlatform)
		{
			// this will also free the cover aps from level memory
			UnregisterAllActionPacks();

			const NdGameObject* pBindObj = GetBoundGameObject();
			const RigidBody* pBindTarget = GetBoundRigidBody();
			RegisterCoverActionPacksInternal(pBindObj, pBindTarget);
		}
		m_bUpdateCoverActionPacks = false;
	}

	if (m_pTargetable)
	{
		PROFILE(Processes, ProcessGo_PJU_Targetable);
		m_pTargetable->UpdateTargetable(*GetAttachSystem());
	}

	if (m_pDrawControl)
	{
		UpdateAmbientOccluders();
	}

	float dt = GetProcessDeltaTime();
	if (dt > 0.0)
	{
		UpdateProbeTreeWeightControls(dt);
	}

	if (m_goFlags.m_disableAnimationAfterUpdate)
	{
		DisableAnimation();
		m_goFlags.m_disableAnimationAfterUpdate = false;
	}

	if (ShouldUpdateWetness())
	{
		UpdateWetness();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::ClothUpdate_Async()
{
	CompositeBody* pCompositeBody = GetCompositeBody();
	if (pCompositeBody)
	{
		pCompositeBody->UpdateCloth();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector NdGameObject::GetUp() const
{
	return kUnitYAxis;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdGameObject::InitFromScriptOptions(const DC::NdSsOptions& opts)
{
	ProcessBucket bucket = kProcessBucketUnknown;
	switch (opts.m_drawBucket)
	{
	case DC::kSsDrawBucketPlatform:
		bucket = kProcessBucketPlatform;
		break;
	case DC::kSsDrawBucketDefault:
		bucket = kProcessBucketDefault;
		break;
	case DC::kSsDrawBucketCharacter:
		bucket = kProcessBucketCharacter;
		break;
	case DC::kSsDrawBucketAttach:
		bucket = kProcessBucketAttach;
		break;
	case DC::kSsDrawBucketSubAttach:
		bucket = kProcessBucketSubAttach;
		break;
	case DC::kSsDrawBucketUnspecified:
		break;
	default:
		break;
	}

	// If I'_ a game object, only change my tree if explicitly specified in the script.
	if (bucket != kProcessBucketUnknown)
	{
		ChangeParentProcess(EngineComponents::GetProcessMgr()->m_pBucketSubtree[bucket]);
	}

	if ((opts.m_ndFlags & DC::kNdSsOptionsFlagIsFlesh) != 0)
	{
		IDrawControl* pDrawCtrl = GetDrawControl();
		if (pDrawCtrl)
		{
			pDrawCtrl->SetInstanceFlag(FgInstance::kIsBody);
		}
	}

	if ((opts.m_ndFlags & DC::kNdSsOptionsFlagCanTrackSpline) != 0 && GetSplineTracker() == nullptr)
	{
		InitSplineTracker();
	}

	if ((opts.m_ndFlags & DC::kNdSsOptionsFlagIsTargetable) != 0)
	{
		// create targetable, if not already created by host Process
		if (!GetTargetable())
		{
			MakeTargetable();
		}
	}

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::Serialize(BitStream& stream) const
{
	if (const SsInstance* pInst = GetScriptInstance())
		pInst->Serialize(stream);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::Deserialize(BitStream& stream)
{
	if (SsInstance* pInst = GetScriptInstance())
		pInst->Deserialize(stream);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::OnPlayIgcAnimation(StringId64 name, StringId64 animId, F32 durationSec)
{
	m_playingIgcName = name;
	m_playingIgcAnim = animId;
	m_playingIgcDurationSec = durationSec;
	m_playingIgcStartTimeSec = ToSeconds(EngineComponents::GetNdFrameState()->m_clock[kGameClock].GetCurTime());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::OnStopIgcAnimation(StringId64 name, StringId64 animId)
{
	if (animId == INVALID_STRING_ID_64 || (m_playingIgcName == name && m_playingIgcAnim == animId))
	{
		m_playingIgcName = INVALID_STRING_ID_64;
		m_playingIgcAnim = INVALID_STRING_ID_64;
		m_playingIgcDurationSec = 0.0f;
		m_playingIgcStartTimeSec = ToSeconds(kTimeFrameNegInfinity);
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdGameObject::GetPlayingIgcName() const
{
	return m_playingIgcName;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdGameObject::GetPlayingIgcAnimation() const
{
	return m_playingIgcAnim;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 NdGameObject::GetPlayingIgcElapsedTime() const
{
	F32 curTimeSec = ToSeconds(EngineComponents::GetNdFrameState()->m_clock[kGameClock].GetCurTime());
	F32 elapsedTimeSec = curTimeSec - m_playingIgcStartTimeSec;
	return elapsedTimeSec;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I16 NdGameObject::FindJointIndex(const StringId64 name) const
{
	I16 iJoint = -1;

	const FgAnimData* pAnimData = GetAnimData();
	ASSERT(pAnimData);
	if (pAnimData)
	{
		iJoint = pAnimData->FindJoint(name);
	}

	return iJoint;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdVoxControllerBase& NdGameObject::GetVoxController()
{
	return(g_emptyNdVoxControllerBase);
}


/// --------------------------------------------------------------------------------------------------------------- ///
const NdVoxControllerBase& NdGameObject::GetVoxControllerConst() const
{
	NdGameObject& rThis = *const_cast<NdGameObject*>(this);
	return(rThis.GetVoxController());
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SyncFoliageSfxController(void)
{
	if (m_pFoliageSfxController != nullptr)
	{
		m_pFoliageSfxController->SyncMetadata();
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetupAttachSystem(const ProcessSpawnInfo& spawn)
{
	StringId64 customAttachName = spawn.GetData(SID("custom-attach-points"), INVALID_STRING_ID_64);
	if (customAttachName != INVALID_STRING_ID_64)
	{
		InitCustomAttachPoints(customAttachName);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AttachSystem* NdGameObject::GetAttachSystem() const
{
	return m_pAttachSystem;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AttachSystem* NdGameObject::GetAttachSystem()
{
	return m_pAttachSystem;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::TryFindAttachPoint(AttachIndex* pAttach, StringId64 id) const
{
	return GetAttachSystem()->FindPointIndexById(pAttach, id);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AttachIndex NdGameObject::FindAttachPoint(StringId64 id) const
{
	AttachIndex attach;
	bool found = GetAttachSystem()->FindPointIndexById(&attach, id);
	if (!found)
	{
		MsgErr("Couldn't find attach point %s", DevKitOnly_StringIdToString(id));
	}
	ALWAYS_ASSERTF(found, ("Couldn't find attach point %s", DevKitOnly_StringIdToString(id)));
	return attach;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject::AttachOrJointIndex NdGameObject::FindAttachOrJointIndex(StringId64 attachOrJointId) const
{
	AttachOrJointIndex attachOrJointIndex;

	if (!TryFindAttachPoint(&attachOrJointIndex.m_attachIndex, attachOrJointId))
	{
		attachOrJointIndex.m_jointIndex = FindJointIndex(attachOrJointId);
	}

	return attachOrJointIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetAttachable(AttachIndex attachIndex, StringId64 attachableId)
{
	if (GetAttachSystem())
		GetAttachSystem()->SetAttachableId(attachIndex, attachableId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdGameObject::GetAttachable(AttachIndex attachIndex) const
{
	if (GetAttachSystem())
		return GetAttachSystem()->GetAttachableId(attachIndex);

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::NotifyMissingShaderInstParamsTag() const
{
#ifdef JBELLOMY
	return;
#endif
	char msg[256];
	snprintf(msg, sizeof(msg), "Spawner \"%s\" is missing tag: 'use-shader-instance-params = 1'\n", m_pSpawner ? m_pSpawner->Name() : "<unknown>");
	if (!m_goFlags.m_shaderInstParamsErrPrinted)
	{
		MsgErr(msg);
		m_goFlags.m_shaderInstParamsErrPrinted = true;
	}
	g_prim.Draw(DebugString(GetTranslation(), msg, kColorRed, g_msgOptions.m_conScale));
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 NdGameObject::GetShaderInstanceParam(int vecIndex, int componentIndex) const
{
	const IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl)
		return 0.0f;

	for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		const IDrawControl::Mesh& mesh = pDrawControl->GetMesh(i);
		for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
		{
			const IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];

			if (!currentLod.m_pInstance || !currentLod.m_pInstance->HasShaderInstanceParams())
			{
				NotifyMissingShaderInstParamsTag();
				return 0.0f;
			}

			return currentLod.m_pInstance->GetShaderInstanceParam(vecIndex, componentIndex);
		}
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::GetShaderInstanceParam(Vec4* pOut, int vecIndex) const
{
	const IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl)
		return false;

	for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		const IDrawControl::Mesh& mesh = pDrawControl->GetMesh(i);
		for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
		{
			const IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];

			if (!currentLod.m_pInstance || !currentLod.m_pInstance->HasShaderInstanceParams())
			{
				NotifyMissingShaderInstParamsTag();
				return false;
			}

			currentLod.m_pInstance->GetShaderInstanceParam(pOut, vecIndex);
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 NdGameObject::GetMeshShaderInstanceParam(int meshIndex, int vecIndex, int componentIndex) const
{
	const IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl)
		return 0.0f;

	const IDrawControl::Mesh& mesh = pDrawControl->GetMesh(meshIndex);
	for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
	{
		const IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];

		if (!currentLod.m_pInstance || !currentLod.m_pInstance->HasShaderInstanceParams())
		{
			NotifyMissingShaderInstParamsTag();
			return 0.0f;
		}

		return currentLod.m_pInstance->GetShaderInstanceParam(vecIndex, componentIndex);
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::GetMeshShaderInstanceParam(Vec4* pOut, int meshIndex, int vecIndex) const
{
	if (const IDrawControl* pDrawControl = GetDrawControl())
	{
		if (meshIndex < pDrawControl->GetNumMeshes())
		{
			const IDrawControl::Mesh& mesh = pDrawControl->GetMesh(meshIndex);
			const IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[0];

			if (!currentLod.m_pInstance || !currentLod.m_pInstance->HasShaderInstanceParams())
			{
				NotifyMissingShaderInstParamsTag();
				return false;
			}

			return currentLod.m_pInstance->GetShaderInstanceParam(pOut, vecIndex);
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::SetShaderInstanceParam(int vecIndex, F32 value, int componentIndex)
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl)
		return false;

	for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		IDrawControl::Mesh& mesh = pDrawControl->GetMesh(i);
		for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
		{
			IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];

			if (!currentLod.m_pInstance || !currentLod.m_pInstance->HasShaderInstanceParams())
			{
				NotifyMissingShaderInstParamsTag();
				return false;
			}

			if (!currentLod.m_pInstance->SetShaderInstanceParam(vecIndex, value, componentIndex))
			{
				return false;
			}
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::SetShaderInstanceParam(int vecIndex, Vector_arg value)
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl) return false;

	for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		if (!SetMeshShaderInstanceParam(i, vecIndex, value))
		{
			return false;
		}
	}
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::SetMeshShaderInstanceParam(int meshIndex, int vecIndex, F32 value, int componentIndex)
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl)
		return false;

	IDrawControl::Mesh& mesh = pDrawControl->GetMesh(meshIndex);
	for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
	{
		IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];

		if (!currentLod.m_pInstance || !currentLod.m_pInstance->HasShaderInstanceParams())
		{
			NotifyMissingShaderInstParamsTag();
			return false;
		}

		if (!currentLod.m_pInstance->SetShaderInstanceParam(vecIndex, value, componentIndex))
		{
			return false;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::SetMeshShaderInstanceParam(int meshIndex, int vecIndex, Vector_arg value)
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl) return false;

	IDrawControl::Mesh& mesh = pDrawControl->GetMesh(meshIndex);
	for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
	{
		IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];

		if (!currentLod.m_pInstance || !currentLod.m_pInstance->HasShaderInstanceParams())
		{
			NotifyMissingShaderInstParamsTag();
			return false;
		}

		Vec4 v4 = value.GetVec4();
		v4.SetW(1.0f);
		if (!currentLod.m_pInstance->SetShaderInstanceParam(vecIndex, v4))
		{
			return false;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::SetShaderInstanceParam(int vecIndex, Vec4_arg value)
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl) return false;

	for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		if (!SetMeshShaderInstanceParam(i, vecIndex, value))
		{
			return false;
		}
	}
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::SetMeshShaderInstanceParam(int meshIndex, int vecIndex, Vec4_arg value)
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl) return false;

	IDrawControl::Mesh& mesh = pDrawControl->GetMesh(meshIndex);
	for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
	{
		IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];

		if (!currentLod.m_pInstance || !currentLod.m_pInstance->HasShaderInstanceParams())
		{
			NotifyMissingShaderInstParamsTag();
			return false;
		}

		if (!currentLod.m_pInstance->SetShaderInstanceParam(vecIndex, value))
		{
			return false;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::SetMeshShaderInstanceParamsFromPalette(int meshIndex, ResolvedTintPalette& resolvedPalette)
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl) return false;

	IDrawControl::Mesh& mesh = pDrawControl->GetMesh(meshIndex);

	for (int iShConst = 0; iShConst < mesh.m_numShaderConstants; ++iShConst)
	{
		const IDrawControl::Mesh::ShaderConstantDesc& desc = mesh.m_aShaderConstantDesc[iShConst];
		if (desc.m_paletteChannelId != INVALID_STRING_ID_64)
		{
			for (int jChannel = 0; jChannel < resolvedPalette.m_numTintChannels; ++jChannel)
			{
				if (resolvedPalette.m_aTintChannelKey[jChannel] == desc.m_paletteChannelId)
				{
					InstanceParameterSpecFromId paramSpec(desc.m_instParamId, InstanceParameterSpecFromId::kIgnoreComponentSpecifier); // parse the instParamId into a param index and component index (the latter of which we ignore)

					if (paramSpec.m_key != kInstParamCount)
					{
						ALWAYS_ASSERTF(paramSpec.m_componentIndex == InstanceParameterSpecFromId::kComponentIndexAny,
									   ("Since the value is coming from a palette, you cannot change individual components of a value, Error Key %d for Palette Channel %s", paramSpec.m_componentIndex, DevKitOnly_StringIdToString(desc.m_paletteChannelId)));
						const Color& color = resolvedPalette.m_aTintChannelColor[jChannel];
						Vec4 vec4Value(color);
						if (!SetMeshShaderInstanceParam(meshIndex, paramSpec.m_key, vec4Value))
							return false;
					}
				}
			}
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::ShowMeshByMaterial(const char* pMatName, bool show)
{
	if (IDrawControl *pDrawControl = GetDrawControl())
	{
		const U32F numMeshes = pDrawControl->GetNumMeshes();
		for (U32F ii = 0; ii < numMeshes; ++ii)
		{
			IDrawControl::Mesh& mesh = pDrawControl->GetMesh(ii);

			for (U32F jj = 0; jj < IDrawControl::Mesh::kMaxLods; ++jj)
			{
				FgInstance* pFgInstance = mesh.m_lod[jj].m_pInstance;
				if (!pFgInstance || !pFgInstance->m_pMeshAllocator)
				{
					continue;
				}

				const U32F numSubMeshes = pFgInstance->m_pMeshInstance->GetNumSubMeshInstances();
				for (U32F kk = 0; kk < numSubMeshes; ++kk)
				{
					SubMeshInstance* pSubMesh = pFgInstance->m_pMeshInstance->GetSubMeshInstance(kk);
					if (!pSubMesh)
					{
						continue;
					}

					// because the material could be swapped, this needs to check the submesh model, who's material never changes
					const SubMeshModel *pSubmeshModel = pSubMesh->GetSubMeshModel();
					if (!pSubmeshModel)
					{
						continue;
					}

					const Material* pMaterial = pSubmeshModel->GetMaterial();
					if (!pMaterial)
					{
						continue;
					}

					const char* pCurMatName = pMaterial->GetName();
					if (!strprefixi(pMatName, pCurMatName))
					{
						continue;
					}

					pFgInstance->ShowSubMesh(kk, show);
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::CheckHasMaterial(const char* pMatName) const
{
	if (IDrawControl *pDrawControl = GetDrawControl())
	{
		const U32F numMeshes = pDrawControl->GetNumMeshes();
		for (U32F ii = 0; ii < numMeshes; ++ii)
		{
			IDrawControl::Mesh& mesh = pDrawControl->GetMesh(ii);

			FgInstance* pFgInstance = mesh.m_lod[0].m_pInstance;
			if (!pFgInstance || !pFgInstance->m_pMeshAllocator)
			{
				continue;
			}

			const U32F numSubMeshes = pFgInstance->m_pMeshInstance->GetNumSubMeshInstances();
			for (U32F kk = 0; kk < numSubMeshes; ++kk)
			{
				SubMeshInstance* pSubMesh = pFgInstance->m_pMeshInstance->GetSubMeshInstance(kk);
				if (!pSubMesh)
				{
					continue;
				}

				// because the material could be swapped, this needs to check the submesh model, who's material never changes
				const SubMeshModel *pSubmeshModel = pSubMesh->GetSubMeshModel();
				if (!pSubmeshModel)
				{
					continue;
				}

				const Material* pMaterial = pSubmeshModel->GetMaterial();
				if (!pMaterial)
				{
					continue;
				}

				const char* pCurMatName = pMaterial->GetName();
				if (!strprefixi(pMatName, pCurMatName))
				{
					continue;
				}

				return true;
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::SetShaderInstanceParamsFromPalette(ResolvedTintPalette& resolvedPalette)
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl) return false;

	for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		if (!SetMeshShaderInstanceParamsFromPalette(i, resolvedPalette))
		{
			return false;
		}
	}
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::AllocateShaderInstanceParams()
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl) return;

	for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		AllocateShaderInstanceParams(i);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::AllocateShaderInstanceParams(int meshIndex)
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl) return;

	IDrawControl::Mesh& mesh = pDrawControl->GetMesh(meshIndex);
	for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
	{
		IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];

		currentLod.m_pInstance->AllocateShaderInstanceParams();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::HasShaderInstanceParams() const
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl)
		return false;

	for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		const IDrawControl::Mesh& mesh = pDrawControl->GetMesh(i);
		for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
		{
			const IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];
			if (currentLod.m_pInstance->HasShaderInstanceParams())
				return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::HasShaderInstanceParams(int meshIndex) const
{
	const IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl)
		return false;

	const IDrawControl::Mesh& mesh = pDrawControl->GetMesh(meshIndex);
	if (mesh.m_availableLods != 0)
	{
		const IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[0];
		return currentLod.m_pInstance->HasShaderInstanceParams();
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void SetSpecularIblParameters(IDrawControl* pDrawCtrl, SpecularIblProbeParameters* pSpecularIblParameters)
{
	if (pDrawCtrl)
	{
		const I32F numMeshes = pDrawCtrl->GetNumMeshes();
		for (I32F i = 0; i < numMeshes; ++i)
		{
			IDrawControl::Mesh* pMesh = &pDrawCtrl->GetMesh(i);

			for (int j = 0; j < pMesh->m_availableLods; j++)
			{
				IDrawControl::Mesh::Lod* pLod = &pMesh->m_lod[j];

				if (pLod && pLod->m_pInstance)
				{
					FgInstance* pInstance = pLod->m_pInstance;

					pInstance->SetSpecularIblProbeParameters(pSpecularIblParameters);
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::Highlight(const Color& color, HighlightStyle style)
{
	static const Vec4 defaultParams0 = Vec4(-0.5f, 0.5f, 1.1f, 2.0f);
	static const Vec4 defaultParams1 = Vec4(0.3f, 5.0f, 1.1f, 1.0f);

	Highlight(color, defaultParams0, defaultParams1, style);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::Highlight(const Color& color, const Vec4& params0, const Vec4& params1, HighlightStyle style)
{
	IDrawControl* pDrawCtrl = GetDrawControl();
	if (!pDrawCtrl)
		return;

	const int numMeshes = pDrawCtrl->GetNumMeshes();
	for (int iMesh = 0; iMesh < numMeshes; ++iMesh)
	{
		IDrawControl::Mesh& mesh = pDrawCtrl->GetMesh(iMesh);

		for (int iLod = 0; iLod < mesh.m_availableLods; ++iLod)
		{
			IDrawControl::Mesh::Lod& lod = mesh.m_lod[iLod];
			if (lod.m_pInstance)
			{
				lod.m_pInstance->AllocateShaderInstanceParams();
				lod.m_pInstance->SetHighlight(color, params0, params1, style);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdGameObject::InitRigidBody(RigidBody& body)
{
	PROFILE(Processes, NdGob_InitRigidBody);

	if (!HavokIsEnabled())
	{
		return Err::kErrPhysicsInit;	// pretend like we don't have any collision data when Havok isn't up (e.g. in the menus)
	}

	const ArtItemCollision* pCollision = GetSingleCollision();
	if (!pCollision || pCollision->m_numBodies == 0)
	{
		return Err::kErrPhysicsInit;
	}

	if (pCollision->m_numBodies > 1)
	{
		// TO DO: Use hkpListShape to treat all collision pieces like a single rigid piece???
		MsgWarn("WARNING: Object '%s' (mesh '%s') has multiple collision pieces,\n"
				"  but is being used as a simple rigid body. Only 0th piece will be physicalized.\n",
				GetName(), DevKitOnly_StringIdToString(GetLookId()));
	}

	if (!body.InitLinkedToObject(this, nullptr, -1, pCollision->m_ppBodies[0]))
	{
		return Err::kErrPhysicsInit;
	}

	const RigidBody* pPlatformBody = GetPlatformBoundRigidBody();
	if (pPlatformBody)
		body.SetPlatformRigidBody(pPlatformBody);

	CheckIsValidBindTarget();

	ResourceTable::IncrementRefCount(pCollision);

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdGameObject::InitCompositeBody(CompositeBodyInitInfo* pInitInfo)
{
	PROFILE(Processes, NdGameObj_InitCompBody);

	//if (!HavokIsEnabled())
	//{
	//	return Err::kErrPhysicsInit;	// pretend like we don't have any collision data when Havok isn't up (e.g. in the menus)
	//}

	ASSERT(!m_pCompositeBody && "InitCompositeBody() called more than once!");
	if (!m_pCompositeBody)
	{
		m_pCompositeBody = NDI_NEW CompositeBody;
		GAMEPLAY_ASSERT(m_pCompositeBody);

		CompositeBodyInitInfo info;
		if (pInitInfo)
		{
			info = *pInitInfo;
		}
		if (!info.m_pOwner)
		{
			info.m_pOwner = this;
		}
		if (info.m_collisionArtItemId == INVALID_STRING_ID_64 && GetSpawner())
		{
			info.m_collisionArtItemId = StringToStringId64(GetSpawner()->GetArtGroupName());
		}
		if (!info.m_pPhysFxSet && (info.m_bUseRagdoll || !info.m_bCharacter))
		{
			info.m_pPhysFxSet = GetPhysFxSet();
		}
		if (CanHaveCloth() && !info.m_numClothBodies)
		{
			FindClothPrototypes(info);
		}

		Err result = m_pCompositeBody->Init(info);

		if (result.Failed())
		{
			m_pCompositeBody = nullptr;
			FreeHavokSystemId();
			return result;
		}
	}

	CheckIsValidBindTarget();

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
RigidBody* NdGameObject::GetSingleRigidBody() const
{
	return m_pCompositeBody && m_pCompositeBody->GetNumBodies() == 1 ? m_pCompositeBody->GetBody(0) : GetRigidBody();
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::FindClothPrototypes(CompositeBodyInitInfo& info)
{
	PROFILE(Processes, FindClothPrototypes);

	const IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl)
		return;

	const U32F numMeshes = pDrawControl->GetNumMeshes();

	U32F numCloths = 0;
	const U32F kMaxCloths = 32;
	const ClothPrototype* pProtos[kMaxCloths];

	for (U32F ii = 0; ii < numMeshes; ++ii)
	{
		const IDrawControl::Mesh& mesh = pDrawControl->GetMesh(ii);
		const StringId64 meshId = mesh.m_lod[0].m_meshNameId;
		const char* pakName = mesh.m_lod[0].m_artItemGeo.ToArtItem()->GetName();
		const StringId64 pakId = StringToStringId64(pakName, false);

		// should eventually replace this with a global "cloth registry"
		ArtItemClothPrototypeHandle clothHandle = ResourceTable::LookupClothPrototype(pakId);
		if (!clothHandle.IsNull())
		{
			const ArtItemClothPrototype* pArtItem = clothHandle.ToArtItem();
			if (pArtItem->m_version == ArtItemClothPrototype::kCurrentVersion)
			{
				for (U32F jj = 0; jj < pArtItem->m_numPieces; ++jj)
				{
					ALWAYS_ASSERTF(numCloths < kMaxCloths, ("Too many cloth pieces in object %s", GetName()));
					pProtos[numCloths++] = pArtItem->m_pieces[jj];
				}
			}
			else
			{
				MsgErr("Cloth piece %s is the wrong version (need version %d, found version %d) - please force build the actor\n", pakName, ArtItemClothPrototype::kCurrentVersion, pArtItem->m_version);
			}
		}
	}

	if (numCloths)
	{
		info.m_ppClothProtos = NDI_NEW(kAllocSingleGameFrame) const ClothPrototype*[numCloths];
		info.m_ppClothColliderProtos = NDI_NEW(kAllocSingleGameFrame) const DC::ClothColliderProto*[numCloths];
		info.m_numClothBodies = numCloths;
		memcpy(info.m_ppClothProtos, pProtos, numCloths*sizeof(const ClothPrototype*));
		memset(info.m_ppClothColliderProtos, 0, numCloths*sizeof(const DC::ClothColliderProto*));
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
const DC::PhysFxSet* NdGameObject::GetPhysFxSet() const
{
	const StringId64 meshId = GetLookId();
	if (meshId != INVALID_STRING_ID_64)
	{
		const DC::PhysFxSet* pPhysFxSet = LookupPhysFxSet(meshId);
		if (pPhysFxSet)
		{
			return pPhysFxSet;
		}
	}
	const StringId64 skelName = GetSkelNameId();
	if (skelName != INVALID_STRING_ID_64)
	{
		const DC::PhysFxSet* pPhysFxSet = LookupSkelPhysFxSet(skelName);
		if (pPhysFxSet)
		{
			return pPhysFxSet;
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SfxProcess* NdGameObject::PlayPhysicsSound(const SfxSpawnInfo& info, bool playFirstRequest/* = false*/, bool destructionFx /*= false*/, bool playAlways /*=false*/)
{
	TimeFrame curTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime();
	if (curTime < m_firstRequestSoundTime)
	{
		m_firstRequestSoundTime = curTime;
		if (playFirstRequest)
		{
			return (SfxProcess*)NewProcess(info);
		}
	}
	else if (playAlways)
	{
		return (SfxProcess*)NewProcess(info);
	}

	// has enough time passed so we can play our sounds
	if (curTime - m_firstRequestSoundTime >= Seconds(m_soundPhysFxDelayTime))
	{
		if (destructionFx)
		{
			// Limit num of destruction fx sounds to 4 per frame so that we don't play 100 sounds when cover with hundred pieces collapses
			if (curTime == m_lastDestructionFxSoundTime)
			{
				if (m_numDestructionFxSoundsThisFrame >= 4)
				{
					return nullptr;
				}
				m_numDestructionFxSoundsThisFrame++;
			}
			else
			{
				m_lastDestructionFxSoundTime = curTime;
				m_numDestructionFxSoundsThisFrame = 1;
			}
		}
		return (SfxProcess*)NewProcess(info);
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DelayPhysicsSounds(F32 delayInSeconds)
{
	m_firstRequestSoundTime = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetCurTime();
	m_soundPhysFxDelayTime = delayInSeconds;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::InitPhysFx(RigidBody& rigidBody)
{
	PROFILE(Processes, NdGob_InitPhysFx);

	const DC::PhysFxSet* pPhysFxSet = GetPhysFxSet();
	if (pPhysFxSet)
	{
		PhysFxProtos protos;
		ALWAYS_ASSERT(pPhysFxSet->m_commonCount <= 1);
		if (pPhysFxSet->m_commonCount == 1)
		{
			protos.m_pCommon = &pPhysFxSet->m_commonArray[0];
		}
		ALWAYS_ASSERT(pPhysFxSet->m_audioCount <= 1);
		if (pPhysFxSet->m_audioCount == 1)
		{
			protos.m_pAudio = &pPhysFxSet->m_audioArray[0];
		}
		ALWAYS_ASSERT(pPhysFxSet->m_particleCount <= 1);
		if (pPhysFxSet->m_particleCount == 1)
		{
			protos.m_pParticle = &pPhysFxSet->m_particleArray[0];
		}

		if (protos.IsValid())
		{
			PhysFx* pPhysFx = NDI_NEW PhysFx;
			pPhysFx->Init(&rigidBody, protos);
			rigidBody.SetPhysFx(pPhysFx);
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetPhysMotionType(RigidBodyMotionType motionType, F32 blend)
{
	CompositeBody* pCompositeBody = GetCompositeBody();
	if (pCompositeBody)
	{
		pCompositeBody->SetMotionType(motionType, blend);
		return;
	}

	RigidBody* pBody = GetRigidBody();
	if (pBody)
	{
		pBody->SetMotionType(motionType, blend);
		return;
	}

	if (motionType != kRigidBodyMotionTypeNonPhysical)
	{
		MsgWarn("Attempt to enable collision on '%s', but object has no collision.\n", GetName());
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetPhysMotionTypeEvent(RigidBodyMotionType motionType, Event& event)
{
	F32 blend = event.GetNumParams() >= 2 ? event.Get(1).GetF32() : 0.0f;
	if (event.GetNumParams() >= 1 && m_pCompositeBody)
	{
		const StringId64 jointSid = event.Get(0).GetStringId();
		if (jointSid != INVALID_STRING_ID_64)
		{
			U32F bodyIndex = m_pCompositeBody->FindBodyIndexByJointSid(jointSid);
			if (bodyIndex == CompositeBody::kInvalidBodyIndex)
				return;
			RigidBody* pBody = m_pCompositeBody->GetBody(bodyIndex);
			pBody->SetMotionType(motionType, blend);
			if (motionType == kRigidBodyMotionTypePhysicsDriven && pBody->GetHavokMotionType() != kRigidBodyMotionTypePhysicsDriven)
			{
				m_pCompositeBody->SetAnimBlend(1.0f, bodyIndex);
				m_pCompositeBody->BlendToPhysics(blend, bodyIndex);
			}
			return;
		}
	}

	SetPhysMotionType(motionType, blend);
}

/// --------------------------------------------------------------------------------------------------------------- ///
RigidBodyMotionType NdGameObject::GetPhysMotionType() const
{
	const CompositeBody* pCompositeBody = GetCompositeBody();
	if (pCompositeBody)
	{
		return pCompositeBody->GetMotionType();
	}

	const RigidBody* pBody = GetRigidBody();
	if (pBody)
	{
		return pBody->GetMotionType();
	}

	return kRigidBodyMotionTypeNonPhysical;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetPhysLayer(Collide::Layer layer)
{
	RigidBody* pRigidBody = GetRigidBody();
	if (m_pCompositeBody)
		m_pCompositeBody->SetLayer(layer);
	else if (pRigidBody)
		pRigidBody->SetLayer(layer);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetTeleport()
{
	if (m_pCompositeBody)
		m_pCompositeBody->SetTeleport();
	else if (RigidBody* pRigidBody = GetRigidBody())
		pRigidBody->SetTeleport();
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NdGameObject::GetHavokSystemId() const
{
	if (!m_bAllocatedHavokSystemId)
	{
		// Allocate the Havok system id only on the first call, which will be during RigidBody or CompositeBody
		// initialization. This ensures that we don't waste ids on objects that don't need them.
		m_bAllocatedHavokSystemId = true;
		m_havokSystemId = HavokAllocSystemId();
		if (m_havokSystemId == 0)
		{
			MsgWarn("Out of Havok system ids! Collisions against this object won't work properly under some circumstances.\n");
		}
	}
	return m_havokSystemId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::FreeHavokSystemId()
{
	HavokFreeSystemId(m_havokSystemId);
	m_havokSystemId = 0;
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::TakeOverHavokSystemId(NdGameObject* pOther)
{
	m_havokSystemId = pOther->GetHavokSystemId();
	m_bAllocatedHavokSystemId = true;
	pOther->m_havokSystemId = 0;
	pOther->m_bAllocatedHavokSystemId = false;
}


/// --------------------------------------------------------------------------------------------------------------- ///

class SingleCollisionFinder
{
private:
	const char* m_goName;
	const ArtItemCollision* m_pCollision;

	bool Visit(LookPointer pLook, StringId64 meshId)
	{
		const ArtItemCollision* pMeshCollision = ResourceTable::LookupCollision(meshId).ToArtItem();
		if (pMeshCollision != nullptr)
		{
			if (m_pCollision == nullptr)
			{
				m_pCollision = pMeshCollision;
			}
			else
			{
				MsgWarn("WARNING: Look %s contains multiple collision objects, but\n"
					"is being used to initialize a single collision for %s.\n"
					"Using first mesh's collision data arbitrarily.\n",
					DevKitOnly_StringIdToString(pLook.GetNameId()), m_goName);
			}
		}
		return true; // keep iterating
	}

	static bool StaticVisit(LookPointer pLook, StringId64 meshId, void* pContext)
	{
		SingleCollisionFinder* pFinder = PunPtr<SingleCollisionFinder*>(pContext);
		ASSERT(pFinder);
		return pFinder->Visit(pLook, meshId);
	}

public:
	SingleCollisionFinder(LookPointer pLook, const char* goName)
		: m_goName(goName)
		, m_pCollision(nullptr)
	{
		// IMPORTANT: A single collision object is being requested.
		// Let's look through all the meshes in the look, trying to find one that has collision.
		// If more than one have collision, we'll be screwed, so select one arbitrarily and warn the user.
		IterateLookMeshIds(pLook, StaticVisit, this);
	}

	const ArtItemCollision* GetCollision() const { return m_pCollision; }
};

const ArtItemCollision* NdGameObject::GetSingleCollision() const
{
	const ArtItemCollision* pCollision = nullptr;
	const StringId64 lookName = GetLookId();
	LookPointer pLook = GetLook(lookName);
	if (pLook)
	{
		SingleCollisionFinder finder(pLook, GetName());
		pCollision = finder.GetCollision();
	}
	else
	{
		// No look or no meshes in look -- assume look name is actor/package name.
		pCollision = ResourceTable::LookupCollision(lookName).ToArtItem();
	}

	return pCollision;
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DisintegrateBreakable(Vector_arg dir, F32 impulse, bool killDynamic, bool noFx)
{
	CompositeBody* pCompositeBody = GetCompositeBody();
	if (pCompositeBody)
	{
		for (int i = 0; i<pCompositeBody->GetNumBodies(); i++)
			pCompositeBody->GetBody(i)->DisintegrateBreakable(dir, impulse, killDynamic, noFx);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::CollapseBreakable()
{
	CompositeBody* pCompositeBody = GetCompositeBody();
	if (pCompositeBody)
	{
		// To avoid trying to collapse residual parts that have just been collapsed gather the target bodies first
		RigidBody** ppBodies = STACK_ALLOC(RigidBody*, pCompositeBody->GetNumBodies());
		U32 numBodies = 0;
		for (int i = 0; i<pCompositeBody->GetNumBodies(); i++)
		{
			RigidBody* pBody = pCompositeBody->GetBody(i);
			if (pBody->IsBreakable())
			{
				ppBodies[numBodies++] = pBody;
			}
		}

		for (int i = 0; i<numBodies; i++)
		{
			ppBodies[i]->CollapseBreakable();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DisintegrateBreakableExplosion(Point_arg center, F32 impulse)
{
	CompositeBody* pCompositeBody = GetCompositeBody();
	if (pCompositeBody)
	{
		for (int i = 0; i<pCompositeBody->GetNumBodies(); i++)
			pCompositeBody->GetBody(i)->DisintegrateBreakableExplosion(center, impulse);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetDestructionEnabledEvent(bool enable, Event& event)
{
	if (!m_pCompositeBody)
		return;

	I32 iJoint = -1;
	if (event.GetNumParams() >= 1)
	{
		const StringId64 jointSid = event.Get(0).GetStringId();
		if (jointSid != INVALID_STRING_ID_64)
		{
			iJoint = FindJointIndex(jointSid);
			if (iJoint < 0)
				return;
		}
	}
	m_pCompositeBody->SetDestructionEnabled(enable, iJoint);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetDestructionEnabledForContactsEvent(bool enable, Event& event)
{
	if (!m_pCompositeBody)
		return;

	I16 iJoint = -1;
	if (event.GetNumParams() >= 1)
	{
		const StringId64 jointSid = event.Get(0).GetStringId();
		if (jointSid != INVALID_STRING_ID_64)
		{
			iJoint = FindJointIndex(jointSid);
			if (iJoint < 0)
				return;
		}
	}
	m_pCompositeBody->SetDestructionEnabledForContacts(enable, iJoint);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const RigidBody* NdGameObject::GetJointBindTarget(StringId64 bindJointId) const
{
	const RigidBody* pBindTarget = nullptr;

	if (bindJointId == INVALID_STRING_ID_64)
	{
		pBindTarget = GetPlatformBindTarget();
	}

	if (!pBindTarget)
	{
		if (m_pCompositeBody && m_pCompositeBody->GetNumBodies() > 0)
		{
			U32F iBindBody;

			if (bindJointId != INVALID_STRING_ID_64)
				iBindBody = m_pCompositeBody->FindBodyIndexByJointSid(bindJointId);
			else
				iBindBody = m_pCompositeBody->GetParentingBodyIndex();

			if (iBindBody != CompositeBody::kInvalidBodyIndex)
				pBindTarget = m_pCompositeBody->GetBody(iBindBody);
		}
		else
		{
			pBindTarget = GetRigidBody();
		}
	}

	return pBindTarget;

}

/// --------------------------------------------------------------------------------------------------------------- ///
const RigidBody* NdGameObject::GetPlatformBindTarget() const
{
	const RigidBody* pBindTarget = nullptr;
	if (m_pPlatformControl)
	{
		pBindTarget = m_pPlatformControl->GetBindTarget();
	}
	return pBindTarget;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject* NdGameObject::GetBoundPlatform() const
{
	NdGameObject* pGo = const_cast<NdGameObject*>(this);
	while (pGo)
	{
		if (pGo->IsPlatform())
			return pGo;

		pGo = pGo->GetBoundGameObject();
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const RigidBody* NdGameObject::GetPlatformBoundRigidBody() const
{
	PROFILE(Processes, NdGO_GetBoundPlatformRB);
	// Search up the bind hierarchy until we find a platform.  Then return the rigid body
	// TO WHICH the immediate child of that platform is actually attached (which may or may
	// not be the default bind target of the platform object).
	// In case this is moving platform itself default bind target will be returned

	NdGameObject*			pGo			= const_cast<NdGameObject*>(this);
	const RigidBody*	pBindTarget	= pGo->GetDefaultBindTarget();
	while (pGo)
	{
		if (pGo->IsPlatform())
			return pBindTarget;

		pBindTarget	= pGo->GetBoundRigidBody();
		pGo			= pGo->GetBoundGameObject();
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Binding NdGameObject::GetJointBinding(StringId64 jointId) const
{
	return Binding(GetJointBindTarget(jointId));
}

/// --------------------------------------------------------------------------------------------------------------- ///
Binding NdGameObject::GetPlatformBinding() const
{
	return Binding(GetPlatformBindTarget());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::OnRemoveParentRigidBody(RigidBody* pOldParentBody, const RigidBody* pNewParentBody)
{
	if (pOldParentBody)
	{
		NdGameObject* pOldBindProc = pOldParentBody->GetOwner();
		if (pOldBindProc != nullptr)
		{
			if (PlatformControl* pPlatCon = pOldBindProc->GetPlatformControl())
			{
				pPlatCon->RemoveRider(this);
			}
		}
	}

	ParentClass::OnRemoveParentRigidBody(pOldParentBody, pNewParentBody);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::OnAddParentRigidBody(const RigidBody* pOldParentBody, Locator oldParentSpace, const RigidBody* pNewParentBody)
{
	ParentClass::OnAddParentRigidBody(pOldParentBody, oldParentSpace, pNewParentBody);

	NdGameObject* pBindProc = pNewParentBody ? pNewParentBody->GetOwnerHandle().ToMutableProcess() : nullptr;

	if (pBindProc)
	{
		if (PlatformControl* pPlatCon = pBindProc->GetPlatformControl())
		{
			pPlatCon->AddRider(this);
		}
	}

	if (pOldParentBody != pNewParentBody)
	{
		Locator newParentSpace = GetParentSpace();
		Locator oldToNewLoc = newParentSpace.UntransformLocator(oldParentSpace);
		Transform mat(oldToNewLoc.Rot(), oldToNewLoc.Pos());
		if (m_pDynNavBlocker)
		{
			m_pDynNavBlocker->EnterNewParentSpace(mat, oldParentSpace, newParentSpace);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator NdGameObject::GetBindLocator(StringId64 jointId) const
{
	const RigidBody* pRigidBody = GetJointBindTarget(jointId);

	if (pRigidBody && pRigidBody->GetGameLinkage() == RigidBody::kLinkedToJoint)
	{
		Locator alignInJs(GetInvBindPoses()[pRigidBody->GetJointIndex()].GetMat44());
		return pRigidBody->GetLocator().TransformLocator(alignInJs);
	}
	else
		return GetLocator();

}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector NdGameObject::GetPlatformLinearVelocity() const
{
	const RigidBody* pPlatformBody = GetPlatformBoundRigidBody();
	if (pPlatformBody)
	{
		Vector v = pPlatformBody->GetLinearVelocity();
		Vector r = GetLocator().GetPosition() - pPlatformBody->GetLocatorCm().GetPosition();
		Vector vFromRotation = Cross(pPlatformBody->GetAngularVelocity(), r);
		return v + vFromRotation;
	}

	return Vector(kZero);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector NdGameObject::ConvertVelocityWsToPs(Vector_arg velocityWs, ConvertMode mode) const
{
	Vector velocityPs = velocityWs;

	const RigidBody* pPlatformBodyBase = GetPlatformBoundRigidBody();
	if (pPlatformBodyBase)
	{
		if (mode == kIncludePlatformVelocity)
		{
			velocityPs -= GetPlatformLinearVelocity();
		}

		// Rotate the velocity vector into the coordinate space of the platform.
		Quat platformRotWs = pPlatformBodyBase->GetLocatorCm().GetRotation();
		velocityPs = ::Unrotate(platformRotWs, velocityPs);
	}

	return velocityPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector NdGameObject::ConvertVelocityPsToWs(Vector_arg velocityPs, ConvertMode mode) const
{
	Vector velocityWs = velocityPs;

	const RigidBody* pPlatformBodyBase = GetPlatformBoundRigidBody();
	if (pPlatformBodyBase)
	{
		// Rotate the velocity vector from platform space into world space.
		Quat platformRotWs = pPlatformBodyBase->GetLocatorCm().GetRotation();
		velocityWs = ::Rotate(platformRotWs, velocityWs);

		if (mode == kIncludePlatformVelocity)
		{
			velocityWs += GetPlatformLinearVelocity();
		}
	}

	return velocityWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator NdGameObject::ConvertLocatorWsToPs(const Locator& locWs) const
{
	Locator locPs = locWs;

	const RigidBody* pPlatformBodyBase = GetPlatformBoundRigidBody();
	if (pPlatformBodyBase)
	{
		Locator platformLocWs = pPlatformBodyBase->GetLocatorCm();
		locPs = platformLocWs.UntransformLocator(locWs);
	}

	return locPs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator NdGameObject::ConvertLocatorPsToWs(const Locator& locPs) const
{
	Locator locWs = locPs;

	const RigidBody* pPlatformBodyBase = GetPlatformBoundRigidBody();
	if (pPlatformBodyBase)
	{
		Locator platformLocWs = pPlatformBodyBase->GetLocatorCm();
		locWs = platformLocWs.TransformLocator(locPs);
	}

	return locWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::ChangeParentProcess(Process* pNewParent, bool pushLastChild)
{
	if (pNewParent)
	{
		ProcessBucket oldBucket = EngineComponents::GetProcessMgr()->GetProcessBucket(*this);
		ProcessBucket newBucket = EngineComponents::GetProcessMgr()->GetProcessBucket(*pNewParent);

		//// TM: We need this for carry objects, let's see what breaks...
		//// Disallow bucket changes -- they would require a deferred parent-change mechanism.
		//{
		//	ProcessBucket curBucket = HasProcessExecuted()
		//		? EngineComponents::GetProcessMgr()->GetProcessBucket(*this)
		//		: kProcessBucketUnknown;			// if I haven't executed, I'm a new process, so don't worry about changing buckets
		//	ALWAYS_ASSERT(pNewParent == EngineComponents::GetProcessMgr()->m_pDeadTree
		//		|| curBucket == kProcessBucketUnknown
		//		|| curBucket == newBucket);
		//}

		ParentClass::ChangeParentProcess(pNewParent, pushLastChild);

		if (newBucket != kProcessBucketUnknown && newBucket != oldBucket)
		{
			ASSERT(newBucket < kProcessBucketCount);
			EngineComponents::GetAnimMgr()->ChangeBucket(m_pAnimData, (U32F)newBucket);

			CompositeBody* pCompositeBody = GetCompositeBody();
			RigidBody* pRigidBody = GetRigidBody();
			if (pCompositeBody)
			{
				pCompositeBody->UpdateBucket();
			}
			else if (pRigidBody)
			{
				pRigidBody->UpdateBucket();
			}
		}
	}
	else
	{
		ParentClass::ChangeParentProcess(pNewParent);
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetBindingFrozenWhenNonPhysical(bool freeze)
{
	m_bFreezeBindingWhenNonPhysical = freeze;
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetFactionId(FactionId ft)
{
	m_faction = ft;

	UpdateHighContrastModeForFactionChange();
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::GetReceiverDamageInfo(ReceiverDamageInfo* pInfo) const
{
	if (m_pAttackHandler)
		m_pAttackHandler->GetReceiverDamageInfo(*this, pInfo);
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdGameObject::GetDamageReceiverClass(const RigidBody* pBody, bool* pHitArmor) const
{
	if (m_pAttackHandler)
		return m_pAttackHandler->GetDamageReceiverClass(*this, pBody);
	else
		return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::NeedsNetController(const SpawnInfo& spawn) const
{
	if (CanTransferOwnership())
		return true;

	if (ShouldCreateNetSnapshotController(spawn))
		return true;

	if (spawn.GetData<bool>(SID("net-sync-phase"), false))
		return true;

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::NeedsNetLodController(const SpawnInfo& spawn) const
{
	if (!EngineComponents::GetNdGameInfo()->m_isHeadless)
		return false;

	if (spawn.GetData<bool>(SID("net-lod"), false))
		return true;

	return false;
}

bool NdGameObject::ShouldCreateNetSnapshotController(const SpawnInfo& spawn) const
{
	return (spawn.GetData<I32>(SID("net-host-updated"), 0) != 0);
}

bool NdGameObject::ShouldCreateNetPhaseSnapshotController(const SpawnInfo& spawn) const
{
	return spawn.GetData<bool>(SID("net-sync-phase"), false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdNetPlayerTracker* NdGameObject::GetNdNetOwnerTracker() const
{
	if (m_pNetController)
		return m_pNetController->GetNdNetOwnerTracker(*this);
	else
		return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NetPlayerTracker* NdGameObject::GetNetOwnerTracker() const
{
	if (m_pNetController)
		return m_pNetController->GetNetOwnerTracker(*this);
	else
		return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 NdGameObject::GetNetOwnerTrackerId() const
{
	if (m_pNetController)
		return m_pNetController->GetNetOwnerTrackerId(*this);
	else
		return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetNetOwnerTracker(NdNetPlayerTracker* pNewOwner)
{
	if (m_pNetController)
		m_pNetController->SetNetOwnerTracker(*this, pNewOwner);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::IsNetOwnedByMe() const
{
	if (g_ndConfig.m_pNetInfo->m_pNetGameManager->IsCinemaMode())
		return false;
	else if (m_pNetController)
		return m_pNetController->IsNetOwnedByMe(*this);
	else
		return true; // if we aren't in multiplayer, we are always the owner
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::RequestNetOwnership(NdNetPlayerTracker* pNewOwner) const
{
	if (m_pNetController)
		return m_pNetController->RequestNetOwnership(*this, pNewOwner);
	else
		return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::UpdateNetOwnership()
{
	if (m_pNetController)
		m_pNetController->UpdateNetOwnership(*this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::UpdateLocatorNet()
{
	if (m_pNetController)
		m_pNetController->UpdateLocatorNet(*this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdGameObject::InitNavBlocker(const ProcessSpawnInfo& spawn)
{
	const SpawnerNavBlockerType reqType = (SpawnerNavBlockerType)spawn.GetData(SID("NavBlockerType"), (int)SpawnerNavBlockerType::None);

	if (reqType <= SpawnerNavBlockerType::None)
		return Err::kOK;

	// This allows actors to disable nav blocker via their actor game flags (example: car glass)
	if (spawn.GetData<bool>(SID("no-navblocker"), false))
		return Err::kOK;

	NavBlockerMgr& nbMgr = NavBlockerMgr::Get();

	switch (reqType)
	{
	case SpawnerNavBlockerType::Dynamic:
		{
			m_pDynNavBlocker = nbMgr.AllocateDynamic(this, nullptr, FILE_LINE_FUNC);

			if (!m_pDynNavBlocker)
			{
				return Err::kErrOutOfMemory;
			}

			m_pDynNavBlocker->SetFactionId(GetFactionId().GetRawFactionIndex());

			const StringId64 jointNameId = spawn.GetData<StringId64>(SID("NavBlockerBone"), INVALID_STRING_ID_64);
			m_dynNavBlockerJointIndex = GetAnimControl()->GetAnimData().FindJoint(jointNameId);
		}
		break;

	case SpawnerNavBlockerType::Static:
		{
			const BoundFrame& loc = GetBoundFrame();
			const StringId64 bindSpawnerId = GetBindSpawnerId();
			m_pStaticNavBlocker = nbMgr.AllocateStatic(this,
													   loc,
													   bindSpawnerId,
													   SID("NavMeshBlocker"),
													   spawn,
													   FILE_LINE_FUNC);

			if (!m_pStaticNavBlocker)
			{
				return Err::kErrOutOfMemory;
			}
		}
		break;
	}

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetNavBlocker(DynamicNavBlocker* p)
{
	NAV_ASSERT(m_pDynNavBlocker == nullptr);  // we don't want a nav blocker resource leak
	m_pDynNavBlocker = p;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::UpdateNavBlocker()
{
	PROFILE(Processes, Go_UpdateNavBlocker);

	const Locator& parentSpace = GetParentSpace();

	if (m_pDynNavBlocker)
	{
		Locator blockerOriginWs = GetLocator();

		if (AnimControl* pAnimControl = GetAnimControl())
		{
			const I16 jointIndex = m_dynNavBlockerJointIndex;
			const JointCache* pJointCache = pAnimControl->GetJointCache();
			if (jointIndex < pJointCache->GetNumTotalJoints() && jointIndex >= 0)
			{
				blockerOriginWs = pJointCache->GetJointLocatorWs(jointIndex);
			}
		}

		const Point blockerPosPs = parentSpace.UntransformPoint(blockerOriginWs.Pos());

		m_pDynNavBlocker->SetPosPs(blockerPosPs);

		bool setFromCollision = true;

		const EntitySpawner* pSpawner = GetSpawner();

		if (pSpawner && pSpawner->GetRecord(SID("NavBlockerBoxMin")) && pSpawner->GetRecord(SID("NavBlockerBoxMax")))
		{
			const Point minLs = pSpawner->GetData<Vector>(SID("NavBlockerBoxMin"), kZero) + Point(kOrigin);
			const Point maxLs = pSpawner->GetData<Vector>(SID("NavBlockerBoxMax"), kZero) + Point(kOrigin);

			if ((DistSqr(minLs, kOrigin) > NDI_FLT_EPSILON) || (DistSqr(maxLs, kOrigin) > NDI_FLT_EPSILON))
			{
				Point quad[4];
				NavBlockerMgr::CreateQuadVertsFromBoundingBox(parentSpace,
															  blockerPosPs,
															  blockerOriginWs,
															  minLs,
															  maxLs,
															  quad);

				m_pDynNavBlocker->SetQuad(quad);

				setFromCollision = false;
			}
		}

		if (setFromCollision)
		{
			SetNavBlockerPolyFromCollision(m_pDynNavBlocker, blockerOriginWs, m_pCompositeBody);
		}
	}

	UpdateNavBlockerEnabled();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::UpdateNavBlockerEnabled()
{
	if (m_pDynNavBlocker)
	{
		if (m_wantNavBlocker)
		{
			PROFILE(Processes, UpdateNavBlocker_Poly);

			NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
			NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

			const Locator& parentSpace = GetParentSpace();
			const Point blockerPosWs = parentSpace.TransformPoint(m_pDynNavBlocker->GetPosPs());

			const NavPoly* pNewPoly = nullptr;

			if (const NavMesh* pMesh = m_pDynNavBlocker->GetNavMesh())
			{
				const Point blockerPosLs = pMesh->WorldToLocal(blockerPosWs);
				pNewPoly = pMesh->FindContainingPolyLs(blockerPosLs);
			}

			if (!pNewPoly)
			{
				FindBestNavMeshParams findMesh;
				findMesh.m_pointWs = blockerPosWs;
				findMesh.m_bindSpawnerNameId = GetBindSpawnerId();
				findMesh.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskNone;
				findMesh.m_cullDist = m_pDynNavBlocker->GetBoundingRadius();

				nmMgr.FindNavMeshWs(&findMesh);

				pNewPoly = findMesh.m_pNavPoly;
			}

			m_pDynNavBlocker->SetNavPoly(pNewPoly);
		}
		else
		{
			m_pDynNavBlocker->SetNavPoly(nullptr);
		}
	}

	if (m_pStaticNavBlocker)
	{
		if (m_pStaticNavBlocker->IsEnabledRequested() != m_wantNavBlocker)
		{
			m_pStaticNavBlocker->RequestEnabled(m_wantNavBlocker);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetNavBlockerPolyFromCollision(DynamicNavBlocker* pBlocker, const CompositeBody* pCompositeBody)
{
	const U32F numBodies = pCompositeBody->GetNumBodies();

	// locWs defines the local space that the bounding box is computed in, by default we use the align
	Locator locWs = GetLocator();

	U32F numBigBodies = 0;
	U32F firstBigBody = 0;
	for (U32F iBody = 0; iBody < numBodies; ++iBody)
	{
		const RigidBody* pBody = pCompositeBody->GetBody(iBody);
		if (Collide::IsLayerInMask(pBody->GetLayer(), Collide::kLayerMaskGeneral))
		{
			if (numBigBodies == 0)
			{
				firstBigBody = iBody;
			}
			numBigBodies++;
		}
	}
	if (numBigBodies == 0)
		return;

	// however, if we have only a single pusher, we can use the pusher's locator as the local space
	if (numBigBodies == 1)
	{
		const RigidBody* pBody = pCompositeBody->GetBody(firstBigBody);
		locWs = pBody->GetLocatorCm();
	}

	SetNavBlockerPolyFromCollision(pBlocker, locWs, pCompositeBody);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetNavBlockerPolyFromCollision(DynamicNavBlocker* pBlocker,
												  const Locator& locWs,
												  const CompositeBody* pCompositeBody)
{
	if (!pBlocker || !pCompositeBody)
		return;

	const U32F numBodies = pCompositeBody->GetNumBodies();

	// vAlignMin/Max define an axis aligned box in local align coordinates
	Point vAlignMin(kLargeFloat, kLargeFloat, kLargeFloat);
	Point vAlignMax = -vAlignMin;

	U32F iShapeMeshCount = 0;
	for (U32F iBody = 0; iBody < numBodies; ++iBody)
	{
		const RigidBody* pBody = pCompositeBody->GetBody(iBody);
		if (Collide::IsLayerInMask(pBody->GetLayer(), Collide::kLayerMaskGeneral) && pBody->ExpandAabbToEncloseBody(vAlignMin, vAlignMax, locWs))
		{
			++iShapeMeshCount;
		}
	}

	if (iShapeMeshCount > 0)
	{
		Point vertsLs[4];
		NavBlockerMgr::CreateQuadVertsFromBoundingBox(GetParentSpace(),
													  pBlocker->GetPosPs(),
													  locWs,
													  vAlignMin,
													  vAlignMax,
													  vertsLs);
		pBlocker->SetQuad(vertsLs);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::EnableNavBlocker(bool enable)
{
	m_wantNavBlocker = enable;

	AnimControl* pAnimControl = GetAnimControl();
	const bool deferredUpdate = (pAnimControl && pAnimControl->IsAsyncUpdateEnabled(FgAnimData::kEnableAsyncPostJointUpdate));

	if (!deferredUpdate)
	{
		UpdateNavBlockerEnabled();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::AllocateDynamicNavBlocker()
{
	if (!m_pDynNavBlocker)
	{
		InitNavBlocker(SpawnInfo(GetSpawner()));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DeallocateDynamicNavBlocker()
{
	if (m_pDynNavBlocker)
	{
		NavBlockerMgr::Get().FreeDynamic(m_pDynNavBlocker);
		m_pDynNavBlocker = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::BlockNavPoly(bool block)
{
	if (!m_hBlockedNavPoly.IsNull())
	{
		NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

		// first clean up any previous blocks we may have had
		if (NavPoly* pBlockedNavPoly = const_cast<NavPoly*>(m_hBlockedNavPoly.ToNavPoly()))
		{
			pBlockedNavPoly->SetBlockageMask(Nav::kStaticBlockageMaskNone);
			m_hBlockedNavPoly = nullptr;
		}
	}

	// if we are blocking,
	if (block)
	{
		NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

		// if the object spawns before the navmesh under it is loaded and
		// registered, it will fail to block the poly.
		FindBestNavMeshParams params;
		params.m_pointWs = GetTranslation();
		params.m_cullDist = 0.35f;
		params.m_yThreshold = 1.0f;
		params.m_bindSpawnerNameId = GetBindSpawnerId();
		EngineComponents::GetNavMeshMgr()->FindNavMeshWs(&params);
		float dist = DistXz(params.m_nearestPointWs, params.m_pointWs);
		if (params.m_pNavPoly && dist < 0.35f)
		{
			NavPoly* pBlockedNavPoly = const_cast<NavPoly*>(params.m_pNavPoly);
			pBlockedNavPoly->SetBlockageMask(Nav::kStaticBlockageMaskAll);
			m_hBlockedNavPoly = pBlockedNavPoly;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdGameObject::InitPlatformControl(bool basicInitOnly /* = false */,
									  bool suppressTapErrors /* = false */,
									  StringId64 bindJointIdOverride /* = INVALID_STRING_ID_64 */)
{
	if (!m_pCompositeBody)
	{
		return Err::kOK;
	}

	const U32F numBodies = m_pCompositeBody->GetNumBodies();
	if (numBodies == 0)
	{
		return Err::kOK;
	}

	if (m_pPlatformControl && m_pPlatformControl->IsInitialized())
	{
		return Err::kOK;
	}

	if (!m_pPlatformControl)
	{
		m_pPlatformControl = NDI_NEW PlatformControl;
	}

	ALWAYS_ASSERT(m_pPlatformControl);

	// try to find a rigidBody oriented the same as the align for the bindTarget
	const Locator alignLocWs = GetLocator();
	RigidBody* pBindTarget = m_pPlatformControl->GetBindTarget();

	if (!pBindTarget)
	{
		const U32F overrideIndex = m_pCompositeBody->FindBodyIndexByJointSid(bindJointIdOverride);
		if (overrideIndex != CompositeBody::kInvalidBodyIndex)
		{
			pBindTarget = m_pCompositeBody->GetBody(overrideIndex);
		}
	}

	if (!pBindTarget)
	{
		float bestCosAng = -kLargeFloat;

		for (U32F iBody = 0; iBody < numBodies; ++iBody)
		{
			RigidBody* pBody = m_pCompositeBody->GetBody(iBody);

			const I16 bodyJoint = pBody->GetJointIndex();
			const Locator bodyLocWs = pBody->GetLocator();
			const Quat rotDiff = bodyLocWs.Rot() * Conjugate(alignLocWs.Rot());
			const float cosAng = Abs(rotDiff.W());

			if (cosAng > bestCosAng)
			{
				bestCosAng = cosAng;
				pBindTarget = pBody;
			}
		}
	}

	if (!pBindTarget)
	{
		MsgConErr("InitPlatformControl: Error %s skeleton has no joints suitable for navmesh binding\n", GetName());
		return Err::kErrBadData;
	}

	m_pPlatformControl->Init(this, pBindTarget, basicInitOnly, suppressTapErrors); // NOTE: It's an error to init with any root rigid body *EXCEPT* the one the CompositeBody is using!

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DisablePlatformControl()
{
	if (m_pPlatformControl)
	{
		m_pPlatformControl->Destroy(this);
		m_pPlatformControl = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::ForcePlatformControlRegisterTaps()
{
	if (m_pPlatformControl != nullptr)
	{
		//m_pPlatformControl->RegisterTraversalActionPacksImmediately(*this);
		m_pPlatformControl->RegisterTraversalActionPacks(*this);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::OnPlatformMoved(const Locator& deltaLoc)
{
	PROFILE(Processes, Go_OnPlatformMoved);

	// update our bound frame to the new position
	m_boundFrame.UpdateParentSpaceFromBinding();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::RegenerateCoverActionPacks()
{
	m_bUpdateCoverActionPacks = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::RemoveCoverActionPacks(I32F bodyJoint)
{
	if (bodyJoint < 0)
	{
		UnregisterAllActionPacks();
	}
	else
	{
		const ActionPackMgr& apMgr = ActionPackMgr::Get();
		const U32F kMaxActionPackCount = 256;
		ActionPack* pActionPackList[kMaxActionPackCount];
		const U32F iActionPackCount = apMgr.GetListOfOwnedActionPacks(this, pActionPackList, kMaxActionPackCount);

		for (U32F i = 0; i < iActionPackCount; ++i)
		{
			ActionPack* pAp = pActionPackList[i];
			if (!pAp)
				continue;

			if (pAp->GetType() == ActionPack::kCoverActionPack)
			{
				CoverActionPack* pCoverAp = static_cast<CoverActionPack*>(pAp);
				if (pCoverAp->GetBodyJoint() == bodyJoint)
				{
					if (pCoverAp->IsRegistered())
					{
						pCoverAp->RequestUnregistration();
					}
				}
			}
			else if (pAp->GetType() == ActionPack::kPerchActionPack)
			{
				PerchActionPack* pPerchAp = static_cast<PerchActionPack*>(pAp);
				if (pPerchAp->GetBodyJoint() == bodyJoint)
				{
					if (pPerchAp->IsRegistered())
					{
						pPerchAp->RequestUnregistration();
					}
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Locator ParentTransform(const SMath::Transform& parentToWorld, const Locator& locPs)
{
	const SMath::Transform xformPs = SMath::Transform(BuildTransform(locPs.GetRotation(), locPs.GetTranslation().GetVec4()));
	const SMath::Transform xformWs = xformPs * parentToWorld;

	return Locator(xformWs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Locator ParentUntransform(const SMath::Transform& parentToWorld, const Locator& locWs)
{
	const SMath::Transform xformWs = SMath::Transform(BuildTransform(locWs.GetRotation(), locWs.GetTranslation().GetVec4()));
	const SMath::Transform xformPs = xformWs * Inverse(parentToWorld);

	return Locator(xformPs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::RegisterCoverActionPacksInternal(const NdGameObject* pBindObj, const RigidBody* pBindTarget)
{
	PROFILE(AI, RegisterCoverActionPacks);

	//
	// pBindObj may be:
	//  - nullptr, if we are registering to global static nav meshes, or if we are bound to something already
	//    (e.g. a destructible crate resting on a moving platform)
	//  - this object, if we are a platform registering cover APs to our own nav meshes
	//
	const EntitySpawner* const pSpawner = GetSpawner();
	if (!pSpawner || !m_pCompositeBody)
	{
		return;
	}

	Level* const pLevel = const_cast<Level*>(pSpawner->GetLevel());
	if (!pLevel)
	{
		return;
	}

	StringId64 bindId = INVALID_STRING_ID_64;
	if (pBindObj)
	{
		const EntitySpawner* const pBindSpawner = pBindObj->GetSpawner();
		if (pBindSpawner)
		{
			bindId = pBindSpawner->NameId();
		}
	}

	const I32F bodyCount = m_pCompositeBody->GetNumBodies();
	if (bodyCount <= 0)
	{
		return;
	}

	// NB: 1m is probably too much for cover! should lower this after we ship
	const float yThreshold = pSpawner->GetData<float>(SID("cover-ap-reg-height"), 1.0f);

	for (I32F iBody = 0; iBody < bodyCount; ++iBody)
	{
		RigidBody* const pRigidBody = m_pCompositeBody->GetBody(iBody);
		if (!pRigidBody)
		{
			continue;
		}

		const FeatureDb* const pFeatureDb = pRigidBody->GetFeatureDb();
		if (!pFeatureDb)
		{
			continue;
		}

		const size_t numCorners = pFeatureDb->m_cornerArray.size();
		const size_t numEdges = pFeatureDb->m_edgeArray.size();

		SMath::Transform parentToWorld;
		Locator objLoc;
		if (pBindObj)
		{
			// -----------------------------------------------------------------------------------------------------------------------
			// What happens if both m_pCompositeBody and pBindTarget have scaling(hopefully non-zero uniform) on them? Concatenate?
			// -----------------------------------------------------------------------------------------------------------------------

			parentToWorld = GetRigidBodyFeaturesWorld(pBindTarget);
			if (pBindTarget == pRigidBody)
			{
				// large platform registering cover to own navmeshes
				objLoc = Locator(kIdentity);
			}
			else
			{
				// crates parented to a platform
				objLoc = ParentUntransform(parentToWorld, pRigidBody->GetLocator());
			}
		}
		else
		{
			parentToWorld = GetRigidBodyFeaturesWorld(pRigidBody);
			objLoc = ParentUntransform(parentToWorld, pRigidBody->GetLocator());
		}

		const Matrix3x4 tm = Matrix3x4(objLoc);
		const I16 bodyJoint = pRigidBody->GetJointIndex();

		for (I32F iCorner = 0; iCorner < numCorners; ++iCorner)
		{
			const FeatureCorner& corner = pFeatureDb->m_cornerArray[iCorner];
			if (!corner.IsUpright(tm))
			{
				continue;
			}

			Locator apLoc;
			CoverDefinition coverDef;
			if (!CoverActionPack::MakeCoverDefinition(objLoc, corner, &coverDef, &apLoc))
			{
				continue;
			}

			const Locator apLocWs = ParentTransform(parentToWorld, apLoc);
			BoundFrame bf(apLocWs, Binding(pBindTarget));

			NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

			ActionPackRegistrationParams params;
			params.m_pAllocLevel	= pLevel;
			params.m_bindId			= bindId;
			params.m_yThreshold		= yThreshold;
			params.m_regPtLs		= POINT_LC(0.0f, 0.0f, -0.3f);

			Point nearestOnNavmeshPtWs;
			const NavPoly* const pPoly = CoverActionPack::CanRegisterSelf(coverDef, params, bf, &nearestOnNavmeshPtWs, false);
			if (!pPoly)
			{
				continue;
			}

			CoverActionPack* const pActionPack = pLevel->AllocateCoverActionPack();
			if (!pActionPack)
			{
				continue;
			}

			// placement NEW
			NDI_NEW((void*) pActionPack) CoverActionPack(bf, pSpawner, coverDef, bodyJoint);

			pActionPack->SetDynamic(true);
			pActionPack->RequestRegistration(params);
			pActionPack->SetOwnerProcess(this);

			// find occupant position and its navmesh and store as NavLocation
			{
				const Point occupantPosWs = pActionPack->GetDefensivePosWs();
				const NavMesh* pMesh = pPoly->GetNavMesh();
				const Point nearestOnNavmeshPtPs = pMesh->WorldToParent(nearestOnNavmeshPtWs);

				if (pMesh)
				{
					NavLocation occupantNavLoc;
					const Point probeStartPs = nearestOnNavmeshPtPs;
					const Vector probeVecPs	 = pMesh->WorldToParent(occupantPosWs) - nearestOnNavmeshPtPs;

					NavMesh::ProbeParams probeParams;
					probeParams.m_start		  = probeStartPs;
					probeParams.m_pStartPoly  = pPoly;
					probeParams.m_move		  = probeVecPs;
					probeParams.m_probeRadius = 0.0f;
					NavMesh::ProbeResult result = pMesh->ProbePs(&probeParams);

					// ideally we can eventually assert on this
					// NAV_ASSERT(result == NavMesh::ProbeResult::kReachedGoal);

					occupantNavLoc.SetPs(probeParams.m_endPoint, probeParams.m_pReachedPoly);

					pActionPack->SetOccupantNavLoc(occupantNavLoc);
				}

			}
		}

		for (I32F iEdge = 0; iEdge < numEdges; ++iEdge)
		{
			const FeatureEdge& edge = pFeatureDb->m_edgeArray[iEdge];

			Locator apLoc[kMaxPerchesPerEdge];
			PerchDefinition perchDef[kMaxPerchesPerEdge];
			int numPerches = PerchActionPack::MakePerchDefinition(objLoc, edge, perchDef, apLoc, pFeatureDb);

			for (int i = 0; i < numPerches; i++)
			{
				const Locator apLocWs = ParentTransform(parentToWorld, apLoc[i]);
				BoundFrame bf(apLocWs, Binding(pBindTarget));

				NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

				ActionPackRegistrationParams params;
				params.m_pAllocLevel = pLevel;
				params.m_bindId		 = bindId;
				params.m_yThreshold	 = yThreshold;
				params.m_regPtLs	 = POINT_LC(0.0f, 0.0f, -0.3f);

				if (!PerchActionPack::CanRegisterSelf(perchDef[i], params, bf))
				{
					continue;
				}

				PerchActionPack* const pActionPack = pLevel->AllocatePerchActionPack();
				if (!pActionPack)
				{
					continue;
				}

				// placement NEW
				NDI_NEW((void*)pActionPack) PerchActionPack(bf, pSpawner, perchDef[i], bodyJoint);

				pActionPack->SetDynamic(true);
				pActionPack->RequestRegistration(params);
				pActionPack->SetOwnerProcess(this);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::UnregisterAllActionPacks()
{
	if (const EntitySpawner* pSpawner = GetSpawner())
	{
		if (const Level* pLevel = pSpawner->GetLevel())
		{
			const ActionPackMgr& apMgr = ActionPackMgr::Get();
			const U32F kMaxActionPackCount = 256;
			ActionPack* pActionPackList[kMaxActionPackCount];
			const U32F iActionPackCount = apMgr.GetListOfOwnedActionPacks(this, pActionPackList, kMaxActionPackCount);

			if (iActionPackCount > 0)
			{
				NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

				for (U32F iAp = 0; iAp < iActionPackCount; ++iAp)
				{
					ActionPack* pActionPack = pActionPackList[iAp];
					if (pActionPack->IsDynamic())
					{
						pActionPack->Logout();
					}
					else
					{
						pActionPack->UnregisterImmediately();
					}
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::CanTrackSplines(const SpawnInfo& spawn) const
{
	return (spawn.GetData<I32>(SID("can-track-spline"), 0) != 0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::InitSplineTracker()
{
	if (m_pSplineTracker == nullptr)
	{
		m_pSplineTracker = NDI_NEW AutoSplineTracker();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::UpdateSplineTracker()
{
	// by default assume m_pSplineTracker is an AutoSplineTracker... derived classes may do something different
	ASSERT(m_pSplineTracker);
	ASSERT(m_pSplineTracker->GetType() == SID("AutoSplineTracker"));

	AutoSplineTracker* pSplineTracker = PunPtr<AutoSplineTracker*>(m_pSplineTracker);

	bool bCrossedEndOfSpline = false;
	pSplineTracker->Update(&bCrossedEndOfSpline);

	if (bCrossedEndOfSpline)
	{
		g_ssMgr.BroadcastEvent(SID("end-of-spline"), BoxedValue(GetUserId()), BoxedValue(pSplineTracker->GetSplineId()));	// @ASYNC-TODO
	}

	Point pos_WS = pSplineTracker->GetCurrentPoint();
	Vector tangent = pSplineTracker->GetCurrentTangent();
	Quat rot_WS = SMath::QuatFromLookAt(tangent, SMath::kUnitYAxis);
	Locator loc = Locator(SMath::Transform(rot_WS, pos_WS));

	SetLocator(loc);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::NotifyAtSplineDistance(F32 notifyDistance_m, Process& notifyProcess, U32 notifyData)
{
	bool waiting = false;

	SplineTracker* pSplineTracker = GetSplineTracker();
	if (pSplineTracker)
	{
		const CatmullRom* pNotifySpline = pSplineTracker->GetSpline();
		F32 curDistance_m = pSplineTracker->GetCurrentRealDistance_m();

		//SplineTracker* pSplineTracker = PunPtr<AutoSplineTracker*>(m_pSplineTracker);
		F32 speed = pSplineTracker->GetCurrentSpeed();

		m_ssAtSplineDistanceAction.Start(&notifyProcess, notifyData);
		m_hNotifySpline = const_cast<CatmullRom*>(pNotifySpline);
		m_splineNotifyDistance_m = notifyDistance_m;

		waiting = true;
	}

	return waiting;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::ProcessScriptUpdate()
{
	ASSERT(IsScriptUpdateEnabled());	// can't get here unless this flag is set

	// Run my state script, if any.
	SsInstance* pScriptInst = GetScriptInstance();
	if (pScriptInst)
	{
		pScriptInst->ScriptUpdate();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::FinishUpdate()
{
	// this implementation should usually be called LAST from an overridden function

	if (m_pNetController)
	{
		PROFILE(Processes, NdGo_FinishUpdate_NetUpdate);
		if (!m_goFlags.m_disableNetUpdates)
		{
			m_pNetController->UpdateTime(*this);
			if (!ManualLocatorUpdate())
			{
				m_pNetController->UpdateLocatorNet(*this);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::ProcessRegionControlUpdate()
{
	const Point regionControlPos = GetTranslation();
	const Point prevRegionControlPos = AllComponentsEqual(m_prevRegionControlPos, kInvalidPoint) ? regionControlPos : m_prevRegionControlPos;

	GetRegionControl()->Update(prevRegionControlPos, regionControlPos, 0.0f, 0.0f, this);

	m_prevRegionControlPos = regionControlPos;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::GetTagDataSymbol(StringId64 tagId, StringId64& dataId) const
{
	if (const EntitySpawner* pSpawner = GetSpawner())
	{
		dataId = m_pSpawner->GetData<StringId64>(tagId, INVALID_STRING_ID_64);
		if (dataId != INVALID_STRING_ID_64)
			return true;
	}

	if (GetScriptInstance())
	{
		return GetScriptInstance()->GetTagDataSymbol(tagId, dataId);
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::GetTagDataString(StringId64 tagId, const char** ppDataString) const
{
	if (const EntitySpawner* pSpawner = GetSpawner())
	{
		*ppDataString = pSpawner->GetData<String>(tagId, String()).GetString();
		if (*ppDataString)
			return true;
	}

	if (GetScriptInstance())
	{
		return GetScriptInstance()->GetTagDataString(tagId, ppDataString);
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::GetTagDataFloat(StringId64 tagId, F32& dataFloat) const
{
	if (const EntitySpawner* pSpawner = GetSpawner())
	{
		if (const EntityDB::Record* pRec = pSpawner->GetRecord(tagId))
		{
			dataFloat = pRec->GetData<float>(-1.0f);
			return true;
		}
	}

	if (GetScriptInstance())
	{
		return GetScriptInstance()->GetTagDataFloat(tagId, dataFloat);
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::GetTagDataInt(StringId64 tagId, I32& dataInt) const
{
	if (const EntitySpawner* pSpawner = GetSpawner())
	{
		if (const EntityDB::Record* pRec = pSpawner->GetRecord(tagId))
		{
			dataInt = pRec->GetData<I32>(-1);
			return true;
		}
	}

	if (GetScriptInstance())
	{
		return GetScriptInstance()->GetTagDataInt(tagId, dataInt);
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::GetTagDataBoolean(StringId64 tagId, bool& dataBool) const
{
	I32 dataInt = 0;
	F32 dataFloat = 0.0f;
	StringId64 dataSid = INVALID_STRING_ID_64;
	if (GetTagDataInt(tagId, dataInt))
	{
		dataBool = (dataInt != 0);
		return true;
	}
	else if (GetTagDataFloat(tagId, dataFloat))
	{
		dataBool = (dataFloat != 0.0f);
		return true;
	}
	else if (GetTagDataSymbol(tagId, dataSid))
	{
		dataBool = (dataSid != SID("#f") && dataSid != SID("false")  && dataSid != SID("FALSE") && dataSid != SID("0"));
		return true;
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void GetJointSkinningXform(Transform& outXform, const Locator& objectLoc, const JointCache& jointCache, const FgAnimData& animData, U32F iJoint)
{
	const AnimExecutionContext* pCtx = GetAnimExecutionContext(&animData);
	if (pCtx->m_pAllSkinningBoneMats)
	{
		// slower, but works even with cloth.
		const Transform& objXform = animData.m_objXform;
		const Transform jointXform(reinterpret_cast<Mat34*>(pCtx->m_pAllSkinningBoneMats)[iJoint].GetMat44());
		Transform bindPoseXform(kIdentity);
		jointCache.GetBindPoseJointXform(bindPoseXform, iJoint);
		Transform xform = bindPoseXform * jointXform * objXform;
		RemoveScaleSafe(&xform);
		outXform = xform;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void GetJointLocatorWs(Locator& loc,
							  const Locator& objectLoc,
							  const JointCache& jointCache,
							  const FgAnimData& animData,
							  U32F iJoint,
							  bool bindPose,
							  bool useJointCache)
{
	const AnimExecutionContext* pCtx = GetAnimExecutionContext(&animData);

	const int segmentIndex = ndanim::GetSegmentForOutputJoint(animData.m_pSkeleton, iJoint);
	const int jointInSegIndex = iJoint - ndanim::GetFirstJointInSegment(animData.m_pSkeleton, segmentIndex);
	const bool isJointCacheValid = (pCtx->m_processedSegmentMask_JointParams | pCtx->m_processedSegmentMask_Transforms) & (1 << segmentIndex);

	if (iJoint >= jointCache.GetNumTotalJoints())
		useJointCache = false;

	if (bindPose)
	{
		Transform xfm;
		jointCache.GetBindPoseJointXform(xfm, iJoint);
		loc = objectLoc.TransformLocator(Locator(xfm));
	}
	else if (!useJointCache || !isJointCacheValid)
	{
		Transform xform;
		GetJointSkinningXform(xform, objectLoc, jointCache, animData, iJoint);
		loc = Locator(xform.GetTranslation(), Quat(xform.GetMat44()));
	}
	else
	{
		loc = jointCache.GetJointLocatorWs(iJoint);
	}
}

static ndanim::JointParams GetJointParamsLs(const JointCache& jointCache, const FgAnimData& animData, U32F iJoint, bool bindPose, bool useSkinningMatrices = false)
{
	if (useSkinningMatrices)
	{
		const AnimExecutionContext* pCtx = GetAnimExecutionContext(&animData);

		// slower, but works even with cloth.
		const Transform jointXform(reinterpret_cast<Mat34*>(pCtx->m_pAllSkinningBoneMats)[iJoint].GetMat44());
		Transform bindPoseXform(kIdentity);
		jointCache.GetBindPoseJointXform(bindPoseXform, iJoint);
		Transform xform = bindPoseXform * jointXform;

		I32F parentJoint = jointCache.GetParentJoint(iJoint);
		Transform xformParent(kIdentity);
		if (parentJoint >= 0)
		{
			const Transform jointXformParent(reinterpret_cast<Mat34*>(pCtx->m_pAllSkinningBoneMats)[parentJoint].GetMat44());
			Transform bindPoseXformParent(kIdentity);
			jointCache.GetBindPoseJointXform(bindPoseXformParent, parentJoint);
			xformParent = bindPoseXformParent * jointXformParent;
		}

		Transform xformLocal = xform * Inverse(xformParent);

		ndanim::JointParams sqt;
		sqt.m_trans = xformLocal.GetTranslation();
		Scalar scaleX, scaleY, scaleZ;
		xformLocal.SetXAxis(SafeNormalize(xformLocal.GetXAxis(), SMath::kUnitXAxis, scaleX));
		xformLocal.SetYAxis(SafeNormalize(xformLocal.GetYAxis(), SMath::kUnitYAxis, scaleY));
		xformLocal.SetZAxis(SafeNormalize(xformLocal.GetZAxis(), SMath::kUnitZAxis, scaleZ));
		xformLocal.SetTranslation(POINT_LC(0.0f, 0.0f, 0.0f));
		sqt.m_quat = Quat(xformLocal.GetRawMat44());
		sqt.m_scale = Vector(scaleX, scaleY, scaleZ);
		return sqt;
	}
	else
	{
		return jointCache.GetJointParamsSafeLs(iJoint);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::PreRemoveMeshes()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::GetComment(char* pszComment, I32F nCommentSize) const
{
	if (pszComment && nCommentSize > 0)
	{
		Point pt = GetLocator().Pos();
		snprintf(pszComment, nCommentSize, " @{%6.2f,%6.2f,%6.2f}", float(pt.X()), float(pt.Y()), float(pt.Z()));
		pszComment[nCommentSize - 1] = '\0';
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::DebugDrawFilter::ShouldDebugDraw() const
{
	extern bool g_debugShowMeshAudioConfig;

	bool draw = g_animOptimization.m_drawDisabledUpdates
			 || m_drawFocusable
			 || m_drawTargetable
			 || m_debugAttachPoints.m_debugDraw
			 || m_debugAttachPoints.m_drawSelected
			 || m_drawAnimConfig
			 || m_drawProcessedAnimSegments
			 || m_drawAnimControl
			 || m_recordAnimControl
			 || m_printAnimControl
			 || m_drawAnimOverlays
			 || m_drawAlign
			 || m_drawApReference
			 || m_drawChannelLocators
			 || m_drawFloatChannels
			 || m_drawRenderXform
			 || m_drawNumericAlignAp
			 || m_drawNumericAlignApCharterAndMaya
			 || m_drawSkeletonWorldLocs
			 || m_drawSkeletonSkinningMats
			 || (m_drawOpenings && (m_drawAnalogDoorOpenings || m_drawBigBreakableOpenings || m_drawDebrisObjectOpenings))
			 || m_debugJoints.m_debugDraw
			 || m_debugJoints.m_offsetBaseIndex >= 0
			 || m_debugJoints.m_drawNames
			 || m_drawSelectedJoint
			 || m_drawBoundingSphere
			 || m_drawBoundingBox
			 || m_drawLookNames
			 || m_drawMeshNames
			 || m_drawAmbientOccludersId
			 || m_drawFgInstanceSegmentMasks
			 || m_drawStateScripts
			 || m_drawAssociatedLevel
			 || m_drawSpecCubemapInfo
			 || m_drawFaction
			 || m_drawCompositeBody
			 || m_drawNavLocation
			 || m_drawPointOfInterest
			 || (m_drawDiffuseTintChannels != m_drawingDiffuseTintChannels)
			 || m_drawParentSpace
			 || m_drawScale
			 || m_drawRiders
			 || m_drawBucket
			 || m_skelAndAnim
			 || m_writeEffects
			 || m_writeEffectsToPerforceFile
			 || m_visualEffDebugging
			 || m_drawCharacterCollisionCapsule
			 || g_animOptions.m_drawSnapshotHeaps
			 || g_animOptions.m_printSnapshotHeapUsage
			 || (g_factMgr.m_showAllFacts && !g_factMgr.m_omitPerObjectFacts)
			 || g_debugShowMeshAudioConfig
			 || g_ndSubsystemDebugDrawTree
			 || m_drawEntityDB
			 || m_drawState
			 || m_drawChildScriptEvents
			 || g_animOptions.m_gestures.m_drawGestureDirMesh
			 || g_animOptions.m_gestures.m_debugGesturePermissions
			 || m_drawHighContrastModeTag;

	draw = draw || (g_animOptions.m_useLightObjects && (g_gameObjectDrawFilter.m_drawLightJoints || g_gameObjectDrawFilter.m_drawLightJointNames));

	return draw;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::PreUpdate()
{
	ParentClass::PreUpdate();

	#if !FINAL_BUILD
	{
		++g_animOptimization.m_stats.m_num_ProcessUpdate;
		if (m_pAnimData && (m_pAnimData->m_flags & FgAnimData::kIsInBindPose))
			++g_animOptimization.m_stats.m_numBp_ProcessUpdate;
	}
	#endif

	LogRetargetedAnimations();

	if (IDrawControl* pDrawControl = GetDrawControl())
	{
		pDrawControl->ResetHideObjectForOneFrame();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::LogRetargetedAnimations() const
{
	if (!g_animOptions.m_logRetargetedAnimations && !g_animOptions.m_logRetargetedAnimationsOnScreen)
		return;

	const AnimControl* pAnimControl = GetAnimControl();
	if (!pAnimControl)
		return;

	const FgAnimData& rFgAnimData = pAnimControl->GetAnimData();
	const SkeletonId charSkeletonId = rFgAnimData.m_animateSkelHandle.ToArtItem()->m_skelId;

	bool hasPrintedBanner = false;

	FixedArray<StringId64, AnimCollection::kMaxAnimIds> loggedAnimIds;

	for (int i = 0; i < pAnimControl->GetNumLayers(); ++i)
	{
		const AnimStateLayer* pAnimStateLayer = pAnimControl->GetStateLayerByIndex(i);
		if (!pAnimStateLayer)
			continue;

		const AnimStateInstance* pStateInst = pAnimStateLayer->CurrentStateInstance();
		if (!pStateInst)
			continue;

		AnimCollection animCollection;
		if (const AnimSnapshotNode* pRootNode = pStateInst->GetRootSnapshotNode())
			pRootNode->CollectContributingAnims(&pStateInst->GetAnimStateSnapshot(), pStateInst->MasterFade(), &animCollection);

		loggedAnimIds.Clear();

		for (int k = 0; k < animCollection.m_animCount; ++k)
		{
			if (animCollection.m_animArray[k]->m_skelID != charSkeletonId)
			{
				bool bIsEntryDuplicated = false;

				for (const StringId64 loggedEntry : loggedAnimIds)
				{
					if (loggedEntry == animCollection.m_animArray[k]->GetNameId())
					{
						bIsEntryDuplicated = true;
						break;
					}
				}

				if (!bIsEntryDuplicated)
				{
					if (!hasPrintedBanner)
					{
						if (g_animOptions.m_logRetargetedAnimationsOnScreen)
							MsgCon("RETARGETED ANIMATIONS ON FRAME %i FOR CHARACTER: %s\n", EngineComponents::GetNdFrameState()->m_gameFrameNumber, GetName());

						if (g_animOptions.m_logRetargetedAnimations)
							MsgAnim("RETARGETED ANIMATIONS ON FRAME %i FOR CHARACTER: %s\n", EngineComponents::GetNdFrameState()->m_gameFrameNumber, GetName());

						hasPrintedBanner = true;
					}

					if (g_animOptions.m_logRetargetedAnimationsOnScreen)
						MsgCon("%s\n", animCollection.m_animArray[k]->GetName());

					if (g_animOptions.m_logRetargetedAnimations)
						MsgAnim("%s\n", animCollection.m_animArray[k]->GetName());

					loggedAnimIds.PushBack(animCollection.m_animArray[k]->GetNameId());
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::BatchDestroyCompositeBodyPass1(hknpBodyId* pEntityList, I32& destroyCount, I32 maxDestroyCount)
{
	if (m_pCompositeBody)
	{
		m_pCompositeBody->BatchDestroyCompositeBodyPass1(pEntityList, destroyCount, maxDestroyCount);
	}
}

void NdGameObject::BatchDestroyCompositeBodyPass2()
{
	if (m_pCompositeBody)
	{
		m_pCompositeBody->BatchDestroyCompositeBodyPass2();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void DrawStateScriptsRecursive(ScreenSpaceTextPrinter& printer, Process* pProcess)
{
	if (pProcess)
	{
		Process* pChild = pProcess->GetFirstChildProcess();
		for ( ; pChild != nullptr; pChild = pChild->GetNextSiblingProcess())
		{
			DrawStateScriptsRecursive(printer, pChild);
		}

		const SsProcess* pScriptProc = SsProcess::FromProcess(pProcess);
		if (pScriptProc && pScriptProc->GetScriptInstance())
		{
			printer.PrintText(kColorCyan, "%s (child)", DevKitOnly_StringIdToString(pScriptProc->GetScriptInstance()->GetScriptId()));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::ShouldDebugDrawJoint(const DebugDrawFilter& filter, int globalJointIndex) const
{
	return (filter.m_debugJoints.m_debugDraw || filter.m_debugJoints.m_drawNames);
}

static void ConvertSegmentMaskToString(char* pStr, U32 mask, U32 numSegments)
{
	char* pCur = pStr;
	for (U32 ii = 0; ii<numSegments; ii++)
	{
		if (mask & (1 << ii))
		{
			pCur += sprintf(pCur, "%i", ii);
		}
		else
		{
			*pCur = '-';
			pCur++;
			if (ii >= 10)
			{
				*pCur = '-';
				pCur++;
			}
		}
	}
	*pCur = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DebugDraw(const DebugDrawFilter& filter) const
{
	STRIP_IN_FINAL_BUILD;

	ScreenSpaceTextPrinter printer(GetTranslation(), ScreenSpaceTextPrinter::kPrintNextLineAbovePrevious, kPrimDuration1FrameAuto, g_msgOptions.m_conScale);

	if (g_animOptimization.m_drawDisabledUpdates && m_goFlags.m_updatesDisabled)
	{
		const FgAnimData* pAnimData = GetAnimData();
		const bool isVisible = pAnimData ? EngineComponents::GetAnimMgr()->IsVisible(pAnimData) : true;
		Color color;
		if (pAnimData && pAnimData->m_flags & FgAnimData::kIsInBindPose)
		{
			color = (isVisible) ? kColorRed : kColorOrangeTrans;
		}
		else
		{
			color = (isVisible) ? kColorGreen : kColorBlueTrans;
		}
		g_prim.Draw(DebugSphere(Sphere(GetTranslation(), 0.05f), color, PrimAttrib(kPrimEnableHiddenLineAlpha, kPrimEnableWireframe)));
		g_prim.Draw(DebugSphere(Sphere(GetTranslation()+VECTOR_LC(0.0f, 0.075f, 0.0f), 0.05f), color, PrimAttrib(kPrimEnableHiddenLineAlpha, kPrimEnableWireframe)));
	}

	if (filter.m_drawHighContrastModeTag)
	{
		const IDrawControl* pDrawControl = GetDrawControl();
		if (pDrawControl)
		{
			const DC::HCMModeType activeHcmTag = pDrawControl->GetHighContrastModeType();
			if (pDrawControl->IsHighContrastModeTypeInherited())
			{
				const DC::HCMModeType baseHcmTag = pDrawControl->GetRawHighContrastModeType();
				printer.PrintTextMultiLine(kColorWhite, "%s\nHCMT: %s [inherited, self is %s]\n", DevKitOnly_StringIdToString(GetUserId()), DC::GetHCMModeTypeName(activeHcmTag), DC::GetHCMModeTypeName(baseHcmTag));
			}
			else
			{
				printer.PrintTextMultiLine(kColorWhite, "%s\nHCMT: %s\n", DevKitOnly_StringIdToString(GetUserId()), DC::GetHCMModeTypeName(activeHcmTag));
			}

			const NdInteractControl* pInteractCtrl = GetInteractControl();
			const U8 numHcmTargets = pInteractCtrl ? pInteractCtrl->GetNumHighContrastModeTargets() : 0;
			if (numHcmTargets > 0)
			{
				for (U8 iTarget = 0; iTarget < numHcmTargets; ++iTarget)
				{
					printer.PrintTextMultiLine(kColorWhite, "HCMT Target Object %u: %s%s\n",
						iTarget,
						DevKitOnly_StringIdToString(pInteractCtrl->GetHighContrastModeTargetObjectId(iTarget)),
						pInteractCtrl->IsHighContrastModeTargetObjectValid(iTarget) ? "" : " [NOT FOUND]");
				}
			}

			if (m_pHcmMeshExcludeFilter.Valid())
			{
				printer.PrintTextMultiLine(kColorWhite, "Mesh Exclude Filter: %s\n", DevKitOnly_StringIdToString(m_pHcmMeshExcludeFilter.GetId()));
			}

			if (pDrawControl->IsInheritingHighContrastModeDisabled())
			{
				printer.PrintTextMultiLine(kColorWhite, "Inheriting HCMT disabled by script\n");
			}
		}
	}

	if (filter.m_drawSplineTracker)
	{
		if (const SplineTracker* pSplineTracker = GetSplineTracker())
		{
			pSplineTracker->DebugDraw();
		}
	}

	if (filter.m_drawDiffuseTintChannels != filter.m_drawingDiffuseTintChannels)
	{
		if (IDrawControl* pDrawControl = GetDrawControl())
		{
			NdGameObject* mutable_this = const_cast<NdGameObject*>(this);
			const I32 numMeshes = pDrawControl->GetNumMeshes();

			for (I32 iMesh = 0; iMesh < numMeshes; ++iMesh)
			{
				IDrawControl::Mesh& mesh = pDrawControl->GetMesh(iMesh);
				U8 mask = 1U;
				static const Color s_aDebugTints[IDrawControl::Mesh::kMaxTintChannels] =
				{
					kColorRed,
					kColorGreen,
					kColorBlue,
					kColorYellow
				};
				for (int iTintChannel = 0; iTintChannel < IDrawControl::Mesh::kMaxTintChannels; iTintChannel++, mask <<= 1)
				{
					//if (0U != (mesh.m_isTintedBits & mask)) // no, this is a debug mode so do it even if the isTinted bit isn't set!
					{
						if (!HasShaderInstanceParams(iMesh))
							mutable_this->AllocateShaderInstanceParams(iMesh);

						const Color tint = (filter.m_drawDiffuseTintChannels || m_aResolvedLookTint == nullptr) ? s_aDebugTints[iTintChannel] : m_aResolvedLookTint[iMesh * kInstParamDiffuseTintCount + iTintChannel];
						const Vec4 vecTint(tint);
						mutable_this->SetMeshShaderInstanceParam(iMesh, kInstParamDiffuseTint0 + iTintChannel, vecTint);
					}
				}
			}
		}
	}
	if (filter.m_drawFocusable)
	{
		const Point lookAtPosWs = GetSite(SID("LookAtPos")).GetTranslation();
		const Point aimAtPosWs = GetSite(SID("AimAtPos")).GetTranslation();
		g_prim.Draw(DebugSphere(Sphere(lookAtPosWs, 0.1f), kColorGreen, PrimAttrib(kPrimEnableHiddenLineAlpha, kPrimEnableWireframe)));
		//g_prim.Draw(DebugString(lookAtPosWs, "look-at", kColorGreen, g_msgOptions.m_conScale));
		g_prim.Draw(DebugSphere(Sphere(aimAtPosWs, 0.1f), kColorRed, PrimAttrib(kPrimEnableHiddenLineAlpha, kPrimEnableWireframe)));
		//g_prim.Draw(DebugString(aimAtPosWs, "aim-at", kColorRed, g_msgOptions.m_conScale));
	}
	if (filter.m_drawTargetable)
	{
		if (m_pTargetable)
		{
			m_pTargetable->DebugDraw();
		}
	}

	const CompositeBody* pCompositeBody = m_pCompositeBody;
	if (!pCompositeBody && filter.m_drawCompositeBody)
	{
		if (const Character* const pCharacter = Character::FromProcess(this))
		{
			pCompositeBody = pCharacter->GetRagdollCompositeBody();
		}
	}

	if (filter.m_drawCompositeBody && pCompositeBody)
	{
		const CompositeBody& compositeBody = *pCompositeBody;
		U32F numBodies = compositeBody.GetNumBodies();
		if (numBodies > 0)
		{
			// vAlignMin/Max define an axis aligned box in local align coordinates
			Point vAlignMin(kLargeFloat, kLargeFloat, kLargeFloat);
			Point vAlignMax = -vAlignMin;

			// locWs defines the local space that the bounding box is computed in, by default we use the align
			Locator locWs = GetLocator();

			// however, if we have only a single pusher, we can use the pusher's locator as the local space
			if (numBodies == 1)
			{
				const RigidBody* pBody = compositeBody.GetBody(0);
				if (pBody)
				{
					locWs = pBody->GetLocatorCm();
				}
			}

			U32F iShapeMeshCount = 0;
			for (U32F iBody = 0; iBody < numBodies; ++iBody)
			{
				const RigidBody* pBody = compositeBody.GetBody(iBody);
				if (pBody)
				{
					Point vLocalMin(kLargeFloat, kLargeFloat, kLargeFloat);
					Point vLocalMax = -vLocalMin;
					g_prim.Draw(DebugCoordAxes(pBody->GetLocatorCm()));
					bool bAabbExpanded = false;
					{
						HavokMarkForReadJanitor havokJanitor;
						bAabbExpanded = pBody->ExpandAabbToEncloseBody(vLocalMin, vLocalMax, locWs);
					}
					if (bAabbExpanded)
					{
						Point centerWs = locWs.TransformPoint(AveragePos(vLocalMin, vLocalMax));
						Transform mat(locWs.Rot(), centerWs);
						Vector extent = vLocalMax - vLocalMin;
						g_prim.Draw(DebugBox(mat, extent, pBody->IsInvalid() ? kColorRed : pBody->GetMotionType() == kRigidBodyMotionTypeNonPhysical ? kColorOrange : kColorGreen, PrimAttrib(kPrimWireFrame)));
						++iShapeMeshCount;
					}
				}
			}
		}
	}

	if (filter.m_drawScale)
	{
		Vector scale = GetScale();
		printer.PrintText(kColorWhite, "(%.3f, %.3f, %.3f)", (float)scale.X(), (float)scale.Y(), (float)scale.Z());
	}

	if (filter.m_drawBucket)
	{
		static const Color s_bucketColor[kProcessBucketCount] =
		{
			kColorDarkGray, // kProcessBucketTask
			kColorGray, // kProcessBucketMainDriver
			kColorMagenta, // kProcessBucketPlatform
			kColorWhite, // kProcessBucketDefault
			kColorCyan, // kProcessBucketPreCharacter
			kColorGreen, // kProcessBucketCharacter
			kColorOrange, // kProcessBucketAttach
			kColorRed, // kProcessBucketSubAttach
			kColorYellow, // kProcessBucketEffect
		};
		ProcessBucket bucket = EngineComponents::GetProcessMgr()->GetProcessBucket(*this);
		const char* bucketName = GetProcessBucketName(bucket);
		const int iColor = MinMax((int)bucket, 0, (int)ARRAY_COUNT(s_bucketColor));
		printer.PrintText(s_bucketColor[iColor], "Bucket: %s", bucketName);
	}

	//@JQG TO DO add drawing code for blocked nav poly
	if (filter.m_drawRiders)
	{
		if (const PlatformControl* pPlatCon = GetPlatformControl())
		{
			Locator loc = GetLocator();
			g_prim.Draw(DebugCoordAxes(loc));
			Color riderColor = Color(0.5, 0.5, 0.5);
			I32F iRiderCount = 0;
			for (U32F i = 0; i < pPlatCon->GetMaxRiderCount(); ++i)
			{
				if (const NdGameObject* pRider = pPlatCon->GetRider(i))
				{
					++iRiderCount;
					Locator riderLoc = pRider->GetLocator();
					g_prim.Draw(DebugCoordAxes(riderLoc));
					g_prim.Draw(DebugString(riderLoc.Pos(), pRider->GetName(), riderColor));
					g_prim.Draw(DebugLine(loc.Pos(), riderLoc.Pos(), kColorWhite, riderColor));
				}
			}
			printer.PrintText(kColorWhite, "%s - %d riders", GetName(), I32(iRiderCount));
		}
	}

	if (filter.m_skelAndAnim)
	{
		const StringId64 animId = GetCurrentAnim();
		const SkeletonId skelId = GetSkeletonId();
		const char* skelName = ResourceTable::LookupSkelName(skelId);

		printer.PrintText(kColorWhite, "skel: %s\nanim: %s", skelName, DevKitOnly_StringIdToString(animId));
	}

	if (const FgAnimData* pAnimData = GetAnimData())
	{
		const FgAnimData& animData = *pAnimData;
		const JointCache& jointCache = animData.m_jointCache;
		const ArtItemSkeleton* pSkel = animData.m_curSkelHandle.ToArtItem();
		const ndanim::JointHierarchy* pHierarchy = pSkel->m_pAnimHierarchy;
		AnimExecutionContext* pCtx = GetAnimExecutionContext(&animData);
		const AnimExecutionContext* pPrevCtx = GetPrevFrameAnimExecutionContext(&animData);
		if (pCtx)
		{
			if (!filter.m_drawProcessedAnimSegments)
			{
				ProcessRequiredSegments(0xFFFFFFFF,
										AnimExecutionContext::kOutputSkinningMats | AnimExecutionContext::kOutputOutputControls,
										pCtx,
										pPrevCtx);
			}

			if (filter.m_drawSelectedJoint)
			{
				Locator loc;
				Color textColor(1.0f, 1.0f, 1.0f, 0.5f);
				char buf[512];

				if (filter.m_jointIndex < pSkel->m_numTotalJoints)
				{
					GetJointLocatorWs(loc, GetLocator(), jointCache, animData, filter.m_jointIndex, filter.m_drawInBindPose, filter.m_drawJointsInJointCache);

					g_prim.Draw(DebugCoordAxes(loc, 0.02f, PrimAttrib(0), 1.0f));

					const char* jointName = pSkel->m_pJointDescs[filter.m_jointIndex].m_pName;

					int numCharsWritten = snprintf(buf, sizeof(buf), "%s(%d)%s", jointName, filter.m_jointIndex, filter.m_drawJointParent ? " [0]" : "");
					if (numCharsWritten < sizeof(buf) && filter.m_drawJointParams)
					{
						const ndanim::JointParams* aDefaultLs = jointCache.GetDefaultLocalSpaceJoints();
						Quat orientQuat = aDefaultLs[filter.m_jointIndex].m_quat; // Maya's "joint orient", which is really just the parent-relative (local space) bind pose of the joint

						float orientX, orientY, orientZ;
						orientQuat.GetEulerAngles(orientX, orientY, orientZ, (Quat::RotationOrder)pSkel->m_pJointRotateOrder[filter.m_jointIndex]);

						const ndanim::JointParams sqt = GetJointParamsLs(jointCache, animData, filter.m_jointIndex, filter.m_drawInBindPose, filter.m_drawJointsInJointCache);

						Quat mayaQuat = Conjugate(orientQuat) * sqt.m_quat;

						float rotX, rotY, rotZ;
						mayaQuat.GetEulerAngles(rotX, rotY, rotZ, (Quat::RotationOrder)pSkel->m_pJointRotateOrder[filter.m_jointIndex]);

						const char* rotOrderStr;
						switch ((Quat::RotationOrder)pSkel->m_pJointRotateOrder[filter.m_jointIndex])
						{
						case SMath::Quat::RotationOrder::kXYZ: rotOrderStr = "XYZ"; break;
						case SMath::Quat::RotationOrder::kYZX: rotOrderStr = "YZX"; break;
						case SMath::Quat::RotationOrder::kZXY: rotOrderStr = "ZXY"; break;
						case SMath::Quat::RotationOrder::kXZY: rotOrderStr = "XZY"; break;
						case SMath::Quat::RotationOrder::kYXZ: rotOrderStr = "YXZ"; break;
						case SMath::Quat::RotationOrder::kZYX: rotOrderStr = "ZYX"; break;
						default:				rotOrderStr = "???"; break;
						}

						snprintf(buf + numCharsWritten, sizeof(buf) - numCharsWritten,
							"\n"
							"World Trans:    (%7.4f, %7.4f, %7.4f)\n"
							"World Quat:     (%7.4f, %7.4f, %7.4f; %7.4f)\n"
							"Local Trans:    (%7.4f, %7.4f, %7.4f)\n"
							"Local Quat:     (%7.4f, %7.4f, %7.4f; %7.4f)\n"
							"Local Scale:    (%7.4f, %7.4f, %7.4f)\n"
							"Rotation Order: %s\n"
							"Maya Rot:       [%7.4f, %7.4f, %7.4f]\n"
							"Maya Orient:    [%7.4f, %7.4f, %7.4f]\n",
							(float)loc.GetTranslation().X(), (float)loc.GetTranslation().Y(), (float)loc.GetTranslation().Z(),
							(float)loc.GetRotation().X(), (float)loc.GetRotation().Y(), (float)loc.GetRotation().Z(), (float)loc.GetRotation().W(),
							(float)sqt.m_trans.X(), (float)sqt.m_trans.Y(), (float)sqt.m_trans.Z(),
							(float)sqt.m_quat.X(), (float)sqt.m_quat.Y(), (float)sqt.m_quat.Z(), (float)sqt.m_quat.W(),
							(float)sqt.m_scale.X(), (float)sqt.m_scale.Y(), (float)sqt.m_scale.Z(),
							rotOrderStr,
							RADIANS_TO_DEGREES(rotX), RADIANS_TO_DEGREES(rotY), RADIANS_TO_DEGREES(rotZ),
							RADIANS_TO_DEGREES(orientX), RADIANS_TO_DEGREES(orientY), RADIANS_TO_DEGREES(orientZ));
					}
					buf[sizeof(buf)-1] = '\0';
					MsgCon("%s\n", buf);
					g_prim.Draw(DebugString(loc.Pos(), buf, textColor));
				}
				else
				{
					GetJointLocatorWs(loc, GetLocator(), jointCache, animData, 0, filter.m_drawInBindPose, filter.m_drawJointsInJointCache);
					snprintf(buf, sizeof(buf), "selected joint out of range (> %u)", (U32)pSkel->m_numGameplayJoints - 1u);
					g_prim.Draw(DebugString(loc.Pos(), buf, textColor));
				}

				if (filter.m_drawJointParent)
				{
					int jointIndex = filter.m_jointIndex;
					int numJointsSoFar = 0;
					for (int parentJointIndex = ndanim::GetParentJoint(pSkel->m_pAnimHierarchy, jointIndex); parentJointIndex >= 0; ++numJointsSoFar, parentJointIndex = ndanim::GetParentJoint(pSkel->m_pAnimHierarchy, parentJointIndex))
					{
						if (filter.m_numParentJointsToDraw > 0 && numJointsSoFar >= filter.m_numParentJointsToDraw)
							break;

						Locator parentJointLoc;
						char parentJointbuf[128];
						GetJointLocatorWs(parentJointLoc, GetLocator(), jointCache, animData, parentJointIndex, filter.m_drawInBindPose, filter.m_drawJointsInJointCache);

						const char* jointName = pSkel->m_pJointDescs[parentJointIndex].m_pName;

						int numCharsWritten = snprintf(buf, sizeof(buf), "%s(%d) [%d]", jointName, parentJointIndex, numJointsSoFar + 1);

						g_prim.Draw(DebugCoordAxes(parentJointLoc, 0.02f, PrimAttrib(0), 1.0f));
						g_prim.Draw(DebugString(parentJointLoc.Pos(), buf, textColor));
					}
				}
			}

			if (filter.m_debugJoints.m_debugDraw ||
				filter.m_debugJoints.m_drawNames ||
				(g_animOptions.m_useLightObjects && (g_gameObjectDrawFilter.m_drawLightJoints || g_gameObjectDrawFilter.m_drawLightJointNames)))
			{

				const U32F numSegments = pSkel->m_numSegments;
				for (I32F segmentIndex = 0; segmentIndex < numSegments; ++segmentIndex)
				{
					if (segmentIndex < filter.m_debugJoints.m_segmentRangeStart ||
						(filter.m_debugJoints.m_segmentRangeEnd >= 0 && segmentIndex > filter.m_debugJoints.m_segmentRangeEnd))
						continue;

					const U32F firstSegmentJoint = ndanim::GetFirstJointInSegment(pHierarchy, segmentIndex);
					const U32F numSegmentJoints = ndanim::GetNumJointsInSegment(pHierarchy, segmentIndex);
					const U32F numSegmentAnimatedJoints = ndanim::GetNumAnimatedJointsInSegment(pHierarchy, segmentIndex);
					for (U32F iJoint = 0; iJoint < numSegmentJoints; ++iJoint)
					{
						const U32F globalJointIndex = firstSegmentJoint + iJoint;

						if (globalJointIndex < filter.m_debugJoints.m_rangeStart
						||	(filter.m_debugJoints.m_rangeEnd >= 0 && globalJointIndex > filter.m_debugJoints.m_rangeEnd))
							continue;

						if (!ShouldDebugDrawJoint(filter, globalJointIndex))
							continue;

						const char* szFilter1 = filter.m_debugJoints.m_filter1;
						const char* szFilter2 = filter.m_debugJoints.m_filter2;
						const char* szFilter3 = filter.m_debugJoints.m_filter3;
						const char* szFilter4 = filter.m_debugJoints.m_filter4;
						if (szFilter1[0] != 0 || szFilter2[0] != 0 || szFilter3[0] != 0 || szFilter4[0] != 0)
						{
							const char* jointName = pSkel->m_pJointDescs[globalJointIndex].m_pName;
							bool found1 = szFilter1[0] != 0 && strstr(jointName, szFilter1) != nullptr;
							bool found2 = szFilter2[0] != 0 && strstr(jointName, szFilter2) != nullptr;
							bool found3 = szFilter3[0] != 0 && strstr(jointName, szFilter3) != nullptr;
							bool found4 = szFilter4[0] != 0 && strstr(jointName, szFilter4) != nullptr;
							if (!found1 && !found2 && !found3 && !found4)
								continue;
						}

						const Mat34* const pSkinningBoneMats = reinterpret_cast<const Mat34*>(pCtx->m_pAllSkinningBoneMats);
						Transform skinXform = pSkinningBoneMats && !g_gameObjectDrawFilter.m_drawInBindPose ? Transform(pSkinningBoneMats[globalJointIndex].GetMat44()) : Transform(kIdentity);

						const Transform& objXform = animData.m_objXform;
						Transform bindPoseXform(kIdentity);
						jointCache.GetBindPoseJointXform(bindPoseXform, globalJointIndex);
						Transform xform = bindPoseXform * skinXform * objXform;
						g_prim.Draw(DebugArrow(xform.GetTranslation(), xform.GetXAxis() * 0.1f * filter.m_jointScale, kColorRed, Length(xform.GetXAxis() * 0.1f * filter.m_jointScale), PrimAttrib(0)));
						g_prim.Draw(DebugArrow(xform.GetTranslation(), xform.GetYAxis() * 0.1f * filter.m_jointScale, kColorGreen, Length(xform.GetYAxis() * 0.1f * filter.m_jointScale), PrimAttrib(0)));
						g_prim.Draw(DebugArrow(xform.GetTranslation(), xform.GetZAxis() * 0.1f * filter.m_jointScale, kColorBlue, Length(xform.GetZAxis() * 0.1f * filter.m_jointScale), PrimAttrib(0)));

						if (filter.m_debugJoints.m_drawNames || (g_animOptions.m_useLightObjects && g_gameObjectDrawFilter.m_drawLightJointNames))
						{
							const char* jointName = pSkel->m_pJointDescs[globalJointIndex].m_pName;

							Color textColor(1.0f, 1.0f, 1.0f, 0.5f);
							const U32 kBufLen = 255;
							char buf[kBufLen + 1];
							snprintf(buf, kBufLen, "%s(%d)", jointName, (int)globalJointIndex);
							buf[kBufLen] = '\0';

							g_prim.Draw(DebugString(xform.GetTranslation(), buf, textColor, filter.m_jointScale));
						}
						else
						{
							g_prim.Draw(DebugString(xform.GetTranslation(), StringBuilder<128>("%d", int(globalJointIndex)).c_str(), kColorWhiteTrans, 0.5f * filter.m_jointScale));
						}
					}
				}
			}
			if (filter.m_drawSkeletonWorldLocs)
			{
				pAnimData->m_jointCache.DebugDrawSkel(true,
													  1.0f,
													  filter.m_debugJoints.m_rangeStart,
													  filter.m_debugJoints.m_rangeEnd);
			}
			if (filter.m_drawSkeletonSkinningMats)
			{
				// Set a few options to ensure that we can render all joints
				ProcessRequiredSegments(0xFFFFFFFF, AnimExecutionContext::kOutputSkinningMats, pCtx, pPrevCtx);

				if (pCtx->m_pAllSkinningBoneMats)
				{
					//const ArtItemSkeleton* pSkel = GetAnimData()->m_pCurSkel;
					//const ndanim::JointHierarchy* pHierarchy = pSkel->m_pAnimHierarchy;
					const JointCache& jc = GetAnimData()->m_jointCache;
					const U32F numSegments = pSkel->m_numSegments;
					for (I32F segmentIndex = 0; segmentIndex < numSegments; ++segmentIndex)
					{
						if (segmentIndex < filter.m_debugJoints.m_segmentRangeStart ||
							(filter.m_debugJoints.m_segmentRangeEnd >= 0 && segmentIndex > filter.m_debugJoints.m_segmentRangeEnd))
						{
							MsgCon("Segment %d: %s - HIDDEN\n", segmentIndex, pSkel->m_pSegmentDescs[segmentIndex].m_pName);
							continue;
						}

						MsgCon("Segment %d: %s\n", segmentIndex, pSkel->m_pSegmentDescs[segmentIndex].m_pName);

						const U32F firstSegmentJoint = ndanim::GetFirstJointInSegment(pHierarchy, segmentIndex);
						const U32F numSegmentJoints = ndanim::GetNumJointsInSegment(pHierarchy, segmentIndex);
						const U32F numSegmentAnimatedJoints = ndanim::GetNumAnimatedJointsInSegment(pHierarchy, segmentIndex);
						for (U32F iJoint = 0; iJoint < numSegmentJoints; ++iJoint)
						{
							const U32F globalJointIndex = firstSegmentJoint + iJoint;

							if (globalJointIndex < filter.m_debugJoints.m_rangeStart ||
								(filter.m_debugJoints.m_rangeEnd >= 0 && globalJointIndex > filter.m_debugJoints.m_rangeEnd))
								continue;

							Transform jointXform;
							GetJointSkinningXform(jointXform, GetLocator(), jc, *pAnimData, globalJointIndex);
							const Locator loc = Locator(jointXform);

							const Color color = (iJoint < numSegmentAnimatedJoints) ? kColorGray : kColorOrange;

							I32F parentJoint = jc.GetParentJoint(globalJointIndex);
							if (parentJoint >= 0)
							{
								Transform parentJointXform;
								GetJointSkinningXform(parentJointXform, GetLocator(), jc, *pAnimData, parentJoint);

								const Locator parentLoc = Locator(parentJointXform);
								DebugDrawBone(parentLoc, loc, color, filter.m_jointScale, PrimAttrib(kPrimEnableDepthTest, kPrimEnableDepthWrite));
							}
							else
							{
								//const Color color = (iJoint < numSegmentAnimatedJoints) ? kColorGray : kColorOrange;
								DebugDrawSphere(loc.GetPosition(), 0.02f * filter.m_jointScale, color);
							}
						}
					}
				}
			}

			if (filter.m_debugJoints.m_offsetBaseIndex >= 0 && filter.m_debugJoints.m_offsetBaseIndex < jointCache.GetNumTotalJoints())
			{
				Locator loc;
				GetJointLocatorWs(loc, GetLocator(), jointCache, animData, filter.m_debugJoints.m_offsetBaseIndex, false, false);

				const float transX = filter.m_debugJoints.m_transX;
				const float transY = filter.m_debugJoints.m_transY;
				const float transZ = filter.m_debugJoints.m_transZ;

				loc.Move(transX * GetLocalX(loc));
				loc.Move(transY * GetLocalY(loc));
				loc.Move(transZ * GetLocalZ(loc));

				const float rotX = DegreesToRadians(filter.m_debugJoints.m_rotX);
				const float rotY = DegreesToRadians(filter.m_debugJoints.m_rotY);
				const float rotZ = DegreesToRadians(filter.m_debugJoints.m_rotZ);
				const Quat q = Quat(rotX, rotY, rotZ);
				loc.Rotate(q);

				const char* jointName = pSkel->m_pJointDescs[filter.m_debugJoints.m_offsetBaseIndex].m_pName;
				g_prim.Draw(DebugCoordAxesLabeled(loc, jointName, 0.2f, PrimAttrib(), 0.5f, kColorWhite, 1.0f));
			}

			if (filter.m_drawBoundingSphere)
			{
				if (IDrawControl* pDrawControl = GetDrawControl())
				{
					g_prim.Draw(DebugSphere(pDrawControl->GetBoundingSphere(), Color32(255,0,0,40)));
				}
			}
			if (filter.m_drawBoundingBox)
			{
				g_prim.Draw(DebugBox(animData.m_objXform, animData.m_pBoundingInfo->m_aabb.m_min, animData.m_pBoundingInfo->m_aabb.m_max, Color32(0,0,255,255), PrimAttrib(kPrimEnableWireframe)));
			}
			if (filter.m_drawAssociatedLevel)
			{
				const char* pLevelName = GetAssociatedLevel() ? GetAssociatedLevel()->GetName() : "none";
				printer.PrintText(kColorWhite, pLevelName);
			}
			if (filter.m_drawSpecCubemapInfo)
			{
				if (m_cubemapBgIndex>= 0 && m_cubemapIndex >= 0)
				{
					const Background* pBg = g_scene.GetBackground(m_cubemapBgIndex);

					CubemapSet* pCubemapSet = pBg->GetSpecularCubemapSet();
					ALWAYS_ASSERT(pCubemapSet);
					ALWAYS_ASSERT(m_cubemapIndex >= 0 && m_cubemapIndex < pCubemapSet->GetNumCubemaps());
					ALWAYS_ASSERT(GetDrawControl());
					ALWAYS_ASSERT(GetDrawControl()->GetNumMeshes() > 0);

					FgInstance* pInstance = GetDrawControl()->GetMesh(0).m_lod[0].m_pInstance;

					CubemapPoint* pCubemap = &pCubemapSet->GetCubemap(m_cubemapIndex);
					Point objPosWs = (pInstance) ? GetLocator().TransformPoint(Point(pInstance->m_probeJointBindPosition)) : GetTranslation();
					Point cubemapPosWs = Point(pCubemap->m_pointData->m_position) * pBg->GetLoc().AsTransform();
					F32 distance = Dist(objPosWs, cubemapPosWs);
					F32 cubemapPriority = pCubemap->m_pointData->m_priority;

					printer.SetPosition(objPosWs);

					printer.PrintText(kColorWhite, " %s", (m_goFlags.m_updateSpecCubemapEveryFrame || g_updateSpecCubemapEveryFrame) ? "Updates Every Frame" : "Updates On Spawn");
					printer.PrintText(kColorWhite, " Cubemap Priority: %0.1f", cubemapPriority);
					printer.PrintText(kColorWhite, " Cubemap Position: %0.2f, %0.2f, %0.2f (%0.2f m)", (float)cubemapPosWs.X(), (float)cubemapPosWs.Z(), (float)cubemapPosWs.Z(), distance);
					printer.PrintText(kColorWhite, " Cubemap Index: %d", m_cubemapIndex);
					printer.PrintText(kColorWhite, " Cubemap Level: %s", pBg->GetName());
					printer.PrintText(kColorWhite, " Actor: %s", GetName());

					float sx, sy;
					if (GetRenderCamera(0).WorldToVirtualScreen(sx, sy, objPosWs, true))
					{
						DebugDrawCross2D( Vec2(sx, sy), kDebug2DLegacyCoords, 5.0f, kColorWhite );
					}
				}
				else
				{
					//printer.PrintText(kColorWhite, "No Cubemap");
				}
			}
			if (filter.m_drawStateScripts)
			{
				DrawStateScriptsRecursive(printer, GetFirstChildProcess());

				if (m_pScriptInst)
					printer.PrintText(kColorCyan, "%s", DevKitOnly_StringIdToString(m_pScriptInst->GetScriptId()));
			}
			if (filter.m_drawChildScriptEvents && m_childScriptEventCount != 0)
			{
				printer.PrintText(kColorGreen, "%d registered events (capacity %d)", m_childScriptEventCount, m_childScriptEventCapacity);
			}
			if (filter.m_drawLookNames)
			{
				const StringId64 lookId = GetLookId();
				const StringId64 resolvedLookId = GetResolvedLookId();
				if (lookId == resolvedLookId)
					printer.PrintText(kColorGreen, "%s", DevKitOnly_StringIdToString(resolvedLookId));
				else
					printer.PrintText(kColorGreen, "%s (from %s)", DevKitOnly_StringIdToString(resolvedLookId), DevKitOnly_StringIdToString(lookId));
			}
			if (filter.m_drawAmbientOccludersId)
			{
				const StringId64 ambientOccludersId = GetAmbientOccludersId();
				printer.PrintText(kColorGreen, "ambient occluders: %s%s", ambientOccludersId != INVALID_STRING_ID_64 ? "'" : "", DevKitOnly_StringIdToString(ambientOccludersId));
			}
			if (filter.m_drawMeshNames)
			{
				if (m_pDebugInfo)
				{
					for (U32 iMesh = 0; iMesh < m_pDebugInfo->m_missingMeshCount; ++iMesh)
					{
						printer.PrintText(kColorRed, "%s", DevKitOnly_StringIdToStringOrHex(m_pDebugInfo->m_aMissingMeshId[iMesh]));
					}
				}

				if (IDrawControl* pDrawControl = GetDrawControl())
				{
					for (U32F iMesh = 0; iMesh < pDrawControl->GetNumMeshes(); ++iMesh)
					{
						IDrawControl::Mesh& mesh = pDrawControl->GetMesh(iMesh);
						if (mesh.m_availableLods > 0)
						{
							Color textColor = mesh.m_visibleFlags ? kColorGreen : kColorGray;
							if (!mesh.m_lod[0].m_artItemGeo.ToArtItem())
							{
								textColor = kColorRed; // this never actually happens though... see below
							}

							if ((pDrawControl->IsHiresAnimGeoShown() && (mesh.m_flags & IDrawControl::Mesh::kLoresAnimGeo)) ||
								(!pDrawControl->IsHiresAnimGeoShown() && (mesh.m_flags & IDrawControl::Mesh::kHiresAnimGeo)) )
							{
								textColor = kColorDarkGray;
							}

							Vec4 colorTint;

							const IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[0];

							if (currentLod.m_pInstance && currentLod.m_pInstance->HasShaderInstanceParams() && GetMeshShaderInstanceParam(&colorTint, iMesh, kInstParamDiffuseTint0))
							{
								printer.PrintText(textColor,
												  "%s (tint %0.3f, %0.3f, %0.3f)",
												  DevKitOnly_StringIdToStringOrHex(mesh.m_lod[0].m_meshNameId),
												  float(colorTint.X()),
												  float(colorTint.Y()),
												  float(colorTint.Z()));
							}
							else
							{
								printer.PrintText(textColor, "%s", DevKitOnly_StringIdToStringOrHex(mesh.m_lod[0].m_meshNameId));
							}
						}
					}
				}
			}
			if (filter.m_drawFgInstanceSegmentMasks)
			{
				if (m_pDebugInfo)
				{
					for (U32 iMesh = 0; iMesh < m_pDebugInfo->m_missingMeshCount; ++iMesh)
					{
						printer.PrintText(kColorRed, "%s", DevKitOnly_StringIdToStringOrHex(m_pDebugInfo->m_aMissingMeshId[iMesh]));
					}
				}

				if (IDrawControl* pDrawControl = GetDrawControl())
				{
					for (int iMesh = 0; iMesh < pDrawControl->GetNumMeshes(); ++iMesh)
					{
						IDrawControl::Mesh& mesh = pDrawControl->GetMesh(iMesh);

						for (int iLod = mesh.m_availableLods-1; iLod >= 0; iLod--)
						{
							FgInstance* pFgInst = mesh.m_lod[iLod].m_pInstance;

							Color textColor = kColorGray;
							char buf[512];
							snprintf(buf, 512, " Lod %d: 0", iLod);

							U32 segmentMask = pFgInst->m_requiredJointSegmentMask >> 1;
							int segment = 1;
							while (segmentMask)
							{
								if (segmentMask & 0x01)
									strncat(buf, StringBuilder<16>(", %d", segment).c_str(), 512);

								segmentMask >>= 1;
								segment++;
							}

							printer.PrintText(textColor, buf);
						}

						if (mesh.m_availableLods > 0)
						{
							Color textColor = mesh.m_visibleFlags ? kColorGreen : kColorGray;
							if (!mesh.m_lod[0].m_artItemGeo.ToArtItem())
							{
								textColor = kColorRed; // this never actually happens though... see below
							}

							if ((pDrawControl->IsHiresAnimGeoShown() && (mesh.m_flags & IDrawControl::Mesh::kLoresAnimGeo)) ||
								(!pDrawControl->IsHiresAnimGeoShown() && (mesh.m_flags & IDrawControl::Mesh::kHiresAnimGeo)) )
							{
								textColor = kColorGray;
							}

							printer.PrintText(textColor, "%s", DevKitOnly_StringIdToStringOrHex(mesh.m_lod[0].m_meshNameId));
						}

					}
				}
			}
		}

		if (filter.m_drawAnimConfig)
		{
			if (animData.m_flags & FgAnimData::kDisabledAnimData)
			{
				printer.PrintText(kColorWhite, "Pass0(Disabled) - Pass1(Disabled)");
			}
			else
			{
				static CONST_EXPR const char* animSourceName[3] =
				{
					"None",
					"Clip",
					"Cache"
				};

				static CONST_EXPR const char* animResultName[8] =
				{
					"None",
					"WS",
					"LS",
					"WS + LS",
					"RM",
					"RM + WS",
					"RM + LS",
					"RM + WS + LS"
				};

				printer.PrintText(kColorWhite, "Pass0(%s, %s) - Pass1(%s, %s)",
					animSourceName[animData.GetAnimSourceMode(0) & 0x3],
					animResultName[animData.GetAnimResultMode(0) & 0x7],
					animSourceName[animData.GetAnimSourceMode(1) & 0x3],
					animResultName[animData.GetAnimResultMode(1) & 0x7]);
			}
		}

		if (filter.m_drawProcessedAnimSegments)
		{
			if (pPrevCtx)
			{
				Color color = kColorWhite;
				if (animData.m_flags & FgAnimData::kDisabledAnimData)
					color = kColorGray;

				char textBuf[100];
				ConvertSegmentMaskToString(textBuf, pPrevCtx->m_processedSegmentMask_SkinningMats, pPrevCtx->m_pSkel->m_numSegments);
				printer.PrintText(color, "Segments: %s", textBuf);
				//printer.PrintText(color, "Segments (SkinningMats): %s", textBuf);
				//ConvertSegmentMaskToString(textBuf, pPrevCtx->m_processedSegmentMask_Transforms, pPrevCtx->m_pSkel->m_numSegments);
				//printer.PrintText(color, "Segments (Transforms): %s", textBuf);
				//ConvertSegmentMaskToString(textBuf, pPrevCtx->m_processedSegmentMask_JointParams, pPrevCtx->m_pSkel->m_numSegments);
				//printer.PrintText(color, "Segments (JointParams): %s", textBuf);
			}
		}
	}

	if (filter.m_drawRenderXform && m_pAnimData)
	{
		g_prim.Draw(DebugCoordAxes(Locator(m_pAnimData->m_objXform)));
	}

	if (filter.m_drawAlign)
	{
		g_prim.Draw(DebugCoordAxes(GetLocator(), 0.25f, PrimAttrib(0), 1.0f));
		g_prim.Draw(DebugCoordAxes(GetLocator(), 0.75f));

		if (filter.m_drawNumericAlignAp)
		{
			const Point p = GetLocator().Pos();
			const Vector eulers = quatToEuler(GetLocator().Rot());

			char text[256];
			snprintf(text, sizeof(text) - 1, "P(%.3f, %.3f, %.3f) R(%.3f, %.3f, %.3f)",
				(F32)p.X(),
				(F32)p.Y(),
				(F32)p.Z(),
				(F32)RADIANS_TO_DEGREES(eulers.X()),
				(F32)RADIANS_TO_DEGREES(eulers.Y()),
				(F32)RADIANS_TO_DEGREES(eulers.Z()));

			text[255] = '\0';
			g_prim.Draw(DebugString(p - Vector(kUnitYAxis) * Scalar(0.2f), text));
		}

		if (filter.m_drawNumericAlignApCharterAndMaya)
		{
			const Locator& align = GetLocator();
			char text[256];
			Point   pos			= align.Pos();
			Vector  eulers		= quatToEuler(align.Rot());

			{
				snprintf(text, sizeof(text) - 1, "charter: P(%.4f, %.4f, %.4f) R(%.4f, %.4f, %.4f)\n",
					(float)pos.X(), (float)pos.Y(), (float)pos.Z(),
					(float)RADIANS_TO_DEGREES(eulers.X()), (float)RADIANS_TO_DEGREES(eulers.Y()), (float)RADIANS_TO_DEGREES(eulers.Z()));
				g_prim.Draw(DebugString(pos - Vector(kUnitYAxis) * Scalar(0.3f), text, kColorWhite, 0.7f));
			}

			{
				Vector up = GetLocalY(align.Rot());
				Vector forward = -GetLocalZ(align.Rot());
				Quat mayaRot = QuatFromLookAt(forward, up);
				Vector mayaEulers = quatToEuler(mayaRot);
				snprintf(text, sizeof(text) - 1, "%%maya: P(%.4f, %.4f, %.4f) R(%.4f, %.4f, %.4f)\n",
					(float)pos.X(), (float)pos.Y(), (float)pos.Z(),
					(float)RADIANS_TO_DEGREES(mayaEulers.X()), (float)RADIANS_TO_DEGREES(mayaEulers.Y()), (float)RADIANS_TO_DEGREES(mayaEulers.Z()));
				g_prim.Draw(DebugString(pos - Vector(kUnitYAxis) * Scalar(0.4f), text, kColorWhite, 0.7f));
			}
		}
	}

	if (filter.m_drawApReference)
	{
		DebugShowApReference(
			filter.m_drawNumericAlignAp,
			filter.m_drawNumericAlignApCharterAndMaya,
			filter.m_drawApReferenceParenting,
			filter.m_apReferenceScale,
			filter.m_drawApReferenceFromAllStates,
			filter.m_noAlignWhenDrawApReference);
	}

	if (filter.m_drawNavLocation)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		const NavLocation navLoc = GetNavLocation();
		navLoc.DebugDraw();
	}

	if (filter.m_drawPointOfInterest && m_goFlags.m_isPoiLookAt)
	{
		const Point poiPos = GetAnimData() ? GetBoundingSphere().GetCenter() : GetTranslation();

		g_prim.Draw(DebugCross(poiPos, 0.1f, 2.0f, kColorCyan), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugString(poiPos, StringBuilder<128>("%s\n%s", DevKitOnly_StringIdToString(GetUserId()), DevKitOnly_StringIdToString(m_poiCollectType)).c_str(), kColorWhite, 0.5f), kPrimDuration1FramePauseable);

		if (filter.m_drawPointOfInterestMinCollectRadius)
		{
			g_prim.Draw(DebugSphere(poiPos, m_minPoiCollectRadius, kColorBlue), kPrimDuration1FramePauseable);
		}

		if (filter.m_drawPointOfInterestMaxCollectRadius)
		{
			g_prim.Draw(DebugSphere(poiPos, m_maxPoiCollectRadius, kColorCyan), kPrimDuration1FramePauseable);
		}
	}

	if (filter.m_drawChannelLocators || filter.m_drawFloatChannels)
	{
		DrawAnimChannelLocators(INVALID_STRING_ID_64, -1.0f, filter.m_drawChannelLocators, filter.m_drawFloatChannels);
	}

	if (filter.m_drawParentSpace)
	{
		char text[256];
		GetBindingString(GetBoundRigidBody(), GetBindSpawnerId(), text, sizeof(text));

		g_prim.Draw(DebugString(GetTranslation() + Vector(kUnitYAxis) * Scalar(0.2f), text));
		g_prim.Draw(DebugCoordAxes(GetParentSpace(), 0.5f, PrimAttrib(kPrimDepthWrite), 1.0f));
		g_prim.Draw(DebugLine(GetParentSpace().Pos(), GetTranslation(), kColorBlack, kColorWhite, 1.0f, PrimAttrib(kPrimDepthWrite)));
	}

	if (filter.m_drawFaction)
	{
		printer.PrintText(kColorWhite, GetFactionId().GetName());
	}

	if (filter.m_drawState)
	{
		printer.PrintText(kColorWhite, GetStateName());
	}

	if (filter.m_debugAttachPoints.m_debugDraw)
	{
		if (const AttachSystem* pAttachSystem = GetAttachSystem())
		{
			pAttachSystem->DebugDraw(filter.m_debugAttachPoints.m_filter1,
									 filter.m_debugAttachPoints.m_filter2,
									 filter.m_debugAttachPoints.m_filter3);
		}
	}
	else if (filter.m_debugAttachPoints.m_drawSelected)
	{
		if (const AttachSystem* pAttachSystem = GetAttachSystem())
		{
			pAttachSystem->DebugDraw(filter.m_debugAttachPoints.m_selectedIndex);
		}
	}

	bool bForceDebugLightAnim = g_animOptions.m_enableFakePause && IsType(SID("NdLightObject")) && IsSimpleAnimating();

	if (DebugSelection::Get().IsProcessSelected(this) || bForceDebugLightAnim)
	{
		if (m_pAnimControl)
		{
			if (filter.m_writeEffects || filter.m_writeEffectsToPerforceFile)
			{
				if (filter.m_includeExistingEffects || filter.m_includeNewEffects)
				{
					const bool generateFootEffects = filter.m_generateFootEffects;

					JointSfxHelper helper(&GetAnimControl()->GetAnimData(), filter.m_writeEffectsToPerforceFile ? "data/effects" : "effects");
					helper.WriteEffects(filter.m_includeExistingEffects, filter.m_includeNewEffects, generateFootEffects);
				}

				// Reset the flags
				const_cast<DebugDrawFilter&>(filter).m_writeEffects = false;
				const_cast<DebugDrawFilter&>(filter).m_writeEffectsToPerforceFile = false;
			}

			if (filter.m_drawAnimControl || bForceDebugLightAnim)
			{
				const Locator currentAlign = GetLocator();
				m_pAnimControl->DebugPrint(currentAlign);

				//g_prim.Draw(DebugCoordAxes(currentAlign, 0.75f));
			}
			else if (filter.m_recordAnimControl && !GetClock()->IsPaused())
			{
				m_pAnimControl->DebugPrint(GetLocator(), nullptr, kMsgJafAnimRecorder);
			}

			if (filter.m_printAnimControl)
			{
				m_pAnimControl->DebugPrint(GetLocator(), nullptr, kMsgAnim);
				const_cast<DebugDrawFilter*>(&filter)->m_printAnimControl = false;
			}

			if (filter.m_drawAnimOverlays && m_pAnimControl && m_pAnimControl->GetAnimOverlaySnapshot())
			{
				const AnimOverlaySnapshot* pOverlaySnapshot = m_pAnimControl->GetAnimOverlaySnapshot();
				for (U32F i = 0; i < pOverlaySnapshot->GetNumLayers(); ++i)
				{
					const DC::AnimOverlaySet* pSet = pOverlaySnapshot->GetOverlaySet(i);
					if (!pSet)
						continue;

					MsgCon("%d : %s\n", int(i), DevKitOnly_StringIdToString(pSet->m_name));
				}
			}
		}

		extern bool g_debugShowMeshAudioConfig;
		if ((filter.m_visualEffDebugging || g_debugShowMeshAudioConfig) && m_pEffectControl && DebugSelection::Get().IsProcessOrNoneSelected(this))
		{
			m_pEffectControl->DebugDraw(this);
		}
	}

	if (g_factMgr.m_showAllFacts && !g_factMgr.m_omitPerObjectFacts && !g_factMgr.m_showCharacterFacts2D)
	{
		// show character facts (in 3D space)
		NdGameObject* mutable_this = const_cast<NdGameObject*>(this);
		mutable_this->UpdateFacts();
		FactDictionary* pFacts = mutable_this->GetFactDict();
		if (pFacts)
		{
			StringBuilder<4096> buffer;
			buffer.append_format("--- %s Facts ---\n", GetName());
			g_factMgr.PrintFactsToString(buffer, pFacts);
			g_prim.Draw(DebugString(GetTranslation() + VECTOR_LC(0.0f, 1.5f, 0.0f), buffer.c_str(), kColorWhite, g_msgOptions.m_conScale));
		}
	}

	if (g_ndSubsystemDebugDrawTree)
	{
		const NdSubsystemMgr* pSysMgr = GetSubsystemMgr();
		if (pSysMgr)
			pSysMgr->DebugPrint();
	}

	if (filter.m_drawEntityDB)
	{
		const EntitySpawner* pSpawner = GetSpawner();
		const EntityDB* pEntityDB = pSpawner ? pSpawner->GetEntityDB() : nullptr;
		if (pEntityDB)
		{
			pEntityDB->DebugDraw(&printer);
		}
		const EntityDB* pArtGroupEntityDB = ArtGroupTable::LookupFlags(GetLookId());
		if (pArtGroupEntityDB)
		{
			pArtGroupEntityDB->DebugDraw(&printer, pEntityDB);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DebugDrawCompositeBody() const
{
	if (m_pCompositeBody)
	{
		m_pCompositeBody->DebugDraw();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* GetApRefChannelName(const NdGameObject* pGo)
{
	const AnimControl* pAnimControl = pGo ? pGo->GetAnimControl() : nullptr;
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;
	const AnimStateInstance* pCurInstance = pBaseLayer ? pBaseLayer->CurrentStateInstance() : nullptr;

	if (pCurInstance && (pCurInstance->GetCustomApRefId() != INVALID_STRING_ID_64))
	{
		return DevKitOnly_StringIdToString(pCurInstance->GetCustomApRefId());
	}

	if (!pCurInstance)
	{
		// try again with simple base layer
		const AnimSimpleLayer* pSimpleBaseLayer = pAnimControl ? pAnimControl->GetSimpleLayerById(SID("base")) : nullptr;
		const AnimSimpleInstance* pSimpleInstance = pSimpleBaseLayer ? pSimpleBaseLayer->CurrentInstance() : nullptr;

		if (pSimpleInstance && pSimpleInstance->GetApChannelName() != INVALID_STRING_ID_64)
		{
			return DevKitOnly_StringIdToString(pSimpleInstance->GetApChannelName());
		}
	}

	return "apReference";
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DebugShowApReference(bool drawNumericAlignAp,
										bool drawNumericAlignAndApCharterAndMaya,
										bool drawApReferenceParenting,
										F32 scale,
										bool drawApRefOnAllStates,
										bool skipAlign) const
{
	STRIP_IN_FINAL_BUILD;

	const char* name = GetName();
	char label[256];
	bool hasAlign = false;
	bool hasApRef = false;

	const Locator align = GetLocator();

	if (!skipAlign)
	{
		g_prim.Draw(DebugCoordAxes(align, 2.0f * scale, PrimAttrib(0), 1.0f), kPrimDuration1FramePauseable);
		if (drawNumericAlignAp)
		{
			const Vector eulers = quatToEuler(align.GetRotation());

			snprintf(label, sizeof(label) - 1, "%s: align P(%.3f, %.3f, %.3f) R(%.3f, %.3f, %.3f)", name,
				(F32)align.Pos().X(),
				(F32)align.Pos().Y(),
				(F32)align.Pos().Z(),
				(F32)RADIANS_TO_DEGREES(eulers.X()),
				(F32)RADIANS_TO_DEGREES(eulers.Y()),
				(F32)RADIANS_TO_DEGREES(eulers.Z()));
		}
		else
		{
			snprintf(label, sizeof(label) - 1, "%s: align", name);
		}
		label[sizeof(label) - 1] = '\0';
		g_prim.Draw(DebugString(align.Pos() + GetLocalZ(align.GetRotation()) * 0.1f, label, kColorWhite, scale));
	}
	hasAlign = true;

	BoundFrame apRef;
	if (drawApRefOnAllStates)
	{
		if (GetAnimControl())
		{
			const AnimLayer* pBaseLayer = GetAnimControl()->GetLayerById(SID("base"));
			if (pBaseLayer)
			{
				const AnimLayerType type = pBaseLayer->GetType();

				if (type == kAnimLayerTypeSimple)
				{
					//drawing ap ref on all states is not supported for simple layers. Just draw only the one apRef
					if (static_cast<const AnimSimpleLayer*>(pBaseLayer)->GetApRefFromCurrentInstance(apRef))
					{
						const char* apRefName = GetApRefChannelName(this);
						hasApRef = true;
						DebugDrawApRef(apRef, align, apRefName, scale, drawApReferenceParenting, drawNumericAlignAp, drawNumericAlignAndApCharterAndMaya);
					}
				}
				else if (type == kAnimLayerTypeState)
				{
					const AnimStateLayer* pStateLayer = static_cast<const AnimStateLayer*>(pBaseLayer);
					const int numTracks = pStateLayer->NumUsedTracks();
					for (int iTrack = 0; iTrack < numTracks; ++iTrack)
					{
						const AnimStateInstanceTrack* pTrack = pStateLayer->GetTrackByIndex(iTrack);
						ASSERT(pTrack);
						const int numInstances = pTrack->GetNumInstances();
						for (int iInstance = 0; iInstance < numInstances; ++iInstance)
						{
							if (pStateLayer->GetApRefFromStateIndex(apRef, iInstance, iTrack))
							{
								hasApRef = true;
								const char* apRefName = "apReference";
								const AnimStateInstance* pInstance = pTrack->GetInstance(iInstance);
								ASSERT(pInstance);
								if (pInstance->GetCustomApRefId() != INVALID_STRING_ID_64)
								{
									apRefName = DevKitOnly_StringIdToString(pInstance->GetCustomApRefId());
								}
								const char* animName = DevKitOnly_StringIdToString(pInstance->GetStateName());
								StringBuilder<256> sb("%s - %s", apRefName, animName);
								DebugDrawApRef(apRef, align, sb.c_str(), scale, drawApReferenceParenting, drawNumericAlignAp, drawNumericAlignAndApCharterAndMaya);
							}
						}
					}
				}
			}
		}
	}
	else
	{
		if (GetApOrigin(apRef))
		{
			const char* apRefName = GetApRefChannelName(this);
			hasApRef = true;
			DebugDrawApRef(apRef, align, apRefName, scale, drawApReferenceParenting, drawNumericAlignAp, drawNumericAlignAndApCharterAndMaya);
		}
	}


	if (!hasAlign || !hasApRef)
	{
		g_prim.Draw(DebugCoordAxes(GetLocator(), 0.3f * scale, PrimAttrib(0), 2.0f));

		snprintf(label, sizeof(label) - 1, "%s:%s%s", name, (!hasAlign ? " NO align" : ""), (!hasApRef ? " NO apRef" : ""));
		Point p = GetLocator().Pos();
		if (hasAlign)
		{
			p += Vector(0, 0.1, 0);
		}
		g_prim.Draw(DebugString(p, label, kColorWhite, scale));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DebugDrawApRef(const BoundFrame& apRef,
								  const Locator& align,
								  const char* apRefName,
								  F32 scale,
								  bool drawApReferenceParenting,
								  bool drawNumericAlignAp,
								  bool drawNumericAlignAndApCharterAndMaya) const
{
	STRIP_IN_FINAL_BUILD;

	const char* name = GetName();
	char label[256];

	g_prim.Draw(DebugCoordAxes(apRef.GetLocatorWs(), 1.0f * scale, PrimAttrib(0), 4.0f, Color(0.3f, 0.3f, 0.3f, 1.0f)));

	char parentingString[128] = "";
	if (drawApReferenceParenting)
	{
		const Binding& apRefBinding = apRef.GetBinding();

		if (const RigidBody* pBodyBase = apRefBinding.GetRigidBody())
		{
			GetBindingString(pBodyBase, INVALID_STRING_ID_64, parentingString, sizeof(parentingString));
		}
	}

	if (drawNumericAlignAp)
	{
		const Vector eulers = quatToEuler(apRef.GetRotationWs());

		snprintf(label, sizeof(label) - 1, "%s: %s P(%.3f, %.3f, %.3f) R(%.3f, %.3f, %.3f) %s", name, apRefName,
			(F32)apRef.GetTranslationWs().X(),
			(F32)apRef.GetTranslationWs().Y(),
			(F32)apRef.GetTranslationWs().Z(),
			(F32)RADIANS_TO_DEGREES(eulers.X()),
			(F32)RADIANS_TO_DEGREES(eulers.Y()),
			(F32)RADIANS_TO_DEGREES(eulers.Z()),
			parentingString);
	}
	else
	{
		snprintf(label, sizeof(label) - 1, "%s: %s %s", name, apRefName, parentingString);
	}
	label[sizeof(label) - 1] = '\0';
	Point p = apRef.GetTranslationWs() + Vector(0, 0.1, 0);
	g_prim.Draw(DebugString(p + GetLocalZ(apRef.GetRotationWs()) * 0.2f, label, kColorWhite, scale));

	if (drawNumericAlignAndApCharterAndMaya)
	{
		Point   pos = apRef.GetTranslationWs();
		Vector  eulers = quatToEuler(apRef.GetRotationWs());
		char text[256];

		{
			snprintf(text, sizeof(text) - 1, "charter: P(%.4f, %.4f, %.4f) R(%.4f, %.4f, %.4f)\n",
				(float)pos.X(), (float)pos.Y(), (float)pos.Z(),
				(float)RADIANS_TO_DEGREES(eulers.X()), (float)RADIANS_TO_DEGREES(eulers.Y()), (float)RADIANS_TO_DEGREES(eulers.Z()));
			g_prim.Draw(DebugString(pos - Vector(kUnitYAxis) * Scalar(0.3f), text, kColorWhite, 0.7f));
		}

		if (drawNumericAlignAndApCharterAndMaya)
		{
			Vector up = GetLocalY(align.Rot());
			Vector forward = -GetLocalZ(align.Rot());
			Quat mayaRot = QuatFromLookAt(forward, up);
			Vector mayaEulers = quatToEuler(mayaRot);
			snprintf(text, sizeof(text) - 1, "%%maya: P(%.4f, %.4f, %.4f) R(%.4f, %.4f, %.4f)\n",
				(float)pos.X(), (float)pos.Y(), (float)pos.Z(),
				(float)RADIANS_TO_DEGREES(mayaEulers.X()), (float)RADIANS_TO_DEGREES(mayaEulers.Y()), (float)RADIANS_TO_DEGREES(mayaEulers.Z()));
			g_prim.Draw(DebugString(pos - Vector(kUnitYAxis) * Scalar(0.4f), text, kColorWhite, 0.7f));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::NeedDebugParenting() const
{
	return g_drawAttachableObjectParent && DebugSelection::Get().IsProcessOrNoneSelected(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* NdGameObject::GetDebugParentingName() const
{
	char* pDebugName = NDI_NEW (kAllocSingleFrame) char[128];
	snprintf(pDebugName, 128 - 1, "%s", DevKitOnly_StringIdToString(GetUserId()));
	return pDebugName;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void CountChannelsCallback_Anim(const ArtItemAnim* pAnim,
									   const AnimStateSnapshot* pSnapshot,
									   const AnimSnapshotNode* pNode,
									   float combinedBlend,
									   float animPhase,
									   uintptr_t userData)
{
	if (!pAnim)
		return;

	StringId64* pChannelIdListOut = (StringId64*)userData;

	const CompressedChannelList* pChannelList = pAnim->m_pCompressedChannelList;
	const U32F numChannels = pChannelList->m_numChannels;
	const StringId64* channelNameIds = pChannelList->m_channelNameIds;

	for (U32F ii = 0; ii < numChannels; ++ii)
	{
		if (channelNameIds[ii] == SID("align"))
			continue;

		AddStringIdToList(channelNameIds[ii], pChannelIdListOut, kDebugChannelArraySize);
	}

#if ENABLE_DEBUG_ANIM_CHANNELS
	{
		if (DebugAnimChannels::Get())
		{
			ScopedTempAllocator jj(FILE_LINE_FUNC);
			ListArray<StringId64> debugChannelNames(kDebugChannelArraySize);

			{
				AtomicLockJanitor janitor(DebugAnimChannels::Get()->GetLock(), FILE_LINE_FUNC);
				DebugAnimChannels::Get()->GetChannelsForAnim(pAnim, debugChannelNames);
			}
			for (U32F ii = 0; ii < debugChannelNames.Size(); ++ii)
			{
				AddStringIdToList(debugChannelNames[ii], pChannelIdListOut, kDebugChannelArraySize);
			}
		}
	}
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool CountChannelsCallback(const AnimStateInstance* pInstance,
								  const AnimStateLayer* pStateLayer,
								  uintptr_t userData)
{
	if (pInstance)
	{
		pInstance->ForAllAnimations(CountChannelsCallback_Anim, userData);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct CountFloatChannelData
{
	StringId64 m_channelIds[kDebugChannelArraySize] = { INVALID_STRING_ID_64 };
	bool m_printIndividual = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static void CountFloatChannelsCallback_Anim(const ArtItemAnim* pAnim,
											const AnimStateSnapshot* pSnapshot,
											const AnimSnapshotNode* pNode,
											float combinedBlend,
											float animPhase,
											uintptr_t userData)
{
	if (!pAnim)
		return;

	CountFloatChannelData* pData = (CountFloatChannelData*)userData;

	const CompressedChannelList* pChannelList = pAnim->m_pCompressedChannelList;
	const U32F numChannels = pChannelList->m_numChannels;
	const StringId64* channelNameIds = pChannelList->m_channelNameIds;
	const CompressedChannel* const* pChannels = pChannelList->m_channels;

	for (U32F ii = 0; ii < numChannels; ++ii)
	{
		if (pChannels[ii]->m_flags & kFlagFloatChannel)
		{
			AddStringIdToList(channelNameIds[ii], pData->m_channelIds, kDebugChannelArraySize);

			if (FALSE_IN_FINAL_BUILD(pData->m_printIndividual))
			{
				EvaluateChannelParams params;
				params.m_pAnim		   = pAnim;
				params.m_phase		   = animPhase;
				params.m_mirror		   = pSnapshot->m_flags.m_isFlipped;
				params.m_channelNameId = channelNameIds[ii];

				float floatVal = -1.0f;

				if (EvaluateCompressedFloatChannel(&params, &floatVal))
				{
					MsgCon("%-32s : %f [%s @ %0.3f]\n",
						   DevKitOnly_StringIdToString(channelNameIds[ii]),
						   pAnim->GetName(),
						   floatVal,
						   animPhase);
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool CountFloatChannelsCallback(const AnimStateInstance* pInstance,
									   const AnimStateLayer* pStateLayer,
									   uintptr_t userData)
{
	if (pInstance)
	{
		pInstance->ForAllAnimations(CountFloatChannelsCallback_Anim, userData);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator NdGameObject::GetChannelLocatorLs(StringId64 locId, bool forceChannelBlending /* = false */) const
{
	const Locator alignWs = GetLocator();

	switch (locId.GetValue())
	{
	case SID_VAL("align"):
		return kIdentity;

	case SID_VAL("apReference"):
		{
			BoundFrame apLoc;
			if (GetApOrigin(apLoc))
			{
				const Locator apLs = alignWs.UntransformLocator(apLoc.GetLocatorWs());
				return apLs;
			}
			else
			{
				return kIdentity;
			}
		}
	}

	const AnimControl* pAnimControl = GetAnimControl();
	const AnimStateLayer* pBaseLayer = pAnimControl ? pAnimControl->GetBaseStateLayer() : nullptr;

	if (!pBaseLayer) // TODO: make this work with simple layers
		return kIdentity;

	AnimChannelLocatorBlender blender = AnimChannelLocatorBlender(locId, kIdentity, forceChannelBlending);
	const Locator channelLocLs = blender.BlendForward(pBaseLayer, alignWs);

	return channelLocLs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NdGameObject::DrawAnimChannelLocators(const AnimStateLayer* pBaseLayer, bool drawLocators, bool printFloats) const
{
	if (!pBaseLayer)
		return 0;

	U32F numTotalChannels = 0;

	if (drawLocators)
	{
		const Locator locWs = GetLocator();
		StringId64 channelIds[kDebugChannelArraySize];
		channelIds[0] = SID("align");
		channelIds[1] = INVALID_STRING_ID_64;

		pBaseLayer->WalkInstancesNewToOld(CountChannelsCallback, (uintptr_t)&channelIds[1]);
		U32F numChannels = 0;
		for (; (channelIds[numChannels] != INVALID_STRING_ID_64) && (numChannels < kDebugChannelArraySize); ++numChannels)
		{
			const Locator channelLocLs = GetChannelLocatorLs(channelIds[numChannels], true);
			const Locator channelLocWs = locWs.TransformLocator(channelLocLs);

			g_prim.Draw(DebugCoordAxes(channelLocWs, 0.2f, kPrimEnableHiddenLineAlpha));
			g_prim.Draw(DebugString(channelLocWs.Pos(), DevKitOnly_StringIdToString(channelIds[numChannels]), kColorWhite, g_msgOptions.m_conScale));

			MsgCon("Name: %s, Pos: (%.5f, %.5f, %.5f)\n",
				   DevKitOnly_StringIdToString(channelIds[numChannels]),
				   (float)channelLocWs.Pos().X(),
				   (float)channelLocWs.Pos().Y(),
				   (float)channelLocWs.Pos().Z());
		}

		numTotalChannels += numChannels;
	}

	if (printFloats)
	{
		CountFloatChannelData channelData;
		channelData.m_printIndividual = true;
		pBaseLayer->WalkInstancesNewToOld(CountFloatChannelsCallback, (uintptr_t)&channelData);
		U32F numChannels = 0;
		bool printed = false;

		for (; (channelData.m_channelIds[numChannels] != INVALID_STRING_ID_64) && (numChannels < kDebugChannelArraySize); ++numChannels)
		{
			bool evaluated = false;

			const float val = pBaseLayer->EvaluateFloat(channelData.m_channelIds[numChannels], &evaluated);

			if (evaluated)
			{
				if (!printed)
				{
					MsgCon("----------------------------------------------------------------\n");
					printed = true;
				}
				MsgCon("%-32s : %f\n", DevKitOnly_StringIdToString(channelData.m_channelIds[numChannels]), val);
			}
		}

		numTotalChannels += numChannels;
	}

	return numTotalChannels;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool DrawAnimChannel(const ArtItemAnim* pAnim,
							SkeletonId skelId,
							StringId64 channelName,
							float phase,
							bool mirror,
							Locator objLoc,
							Color color)
{
	ndanim::JointParams channelAlignSpace;
	EvaluateChannelParams params;
	params.m_pAnim = pAnim;
	params.m_channelNameId = channelName;
	params.m_phase = phase;
	params.m_mirror = mirror;

	if (EvaluateChannelInAnim(skelId, &params, &channelAlignSpace))
	{
		const Locator channelLocatorLs = Locator(channelAlignSpace.m_trans, channelAlignSpace.m_quat);
		const Locator channelLocatorWs = objLoc.TransformLocator(channelLocatorLs);

		g_prim.Draw(DebugCoordAxes(channelLocatorWs, 0.2f, kPrimEnableHiddenLineAlpha));
		g_prim.Draw(DebugString(channelLocatorWs.Pos(), DevKitOnly_StringIdToString(channelName), color, g_msgOptions.m_conScale));

		MsgCon("Name: %s, Pos: (%.5f, %.5f, %.5f) ScaleX %.5f\n",
			   DevKitOnly_StringIdToString(channelName),
			   (float)channelLocatorWs.Pos().X(),
			   (float)channelLocatorWs.Pos().Y(),
			   (float)channelLocatorWs.Pos().Z(),
				(float)channelAlignSpace.m_scale.X()
			);

		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NdGameObject::DrawAnimChannelLocators(const ArtItemAnim* pAnim, float phase, bool mirror) const
{
	U32F numChannelsDrawn = 0;

	const CompressedChannelList* pChannelList	= pAnim->m_pCompressedChannelList;
	const U32F numChannels						= pChannelList->m_numChannels;
	const StringId64* channelNameIds			= pChannelList->m_channelNameIds;
	const CompressedChannel* const* pChannels	= pChannelList->m_channels;

	for (U32F ii = 0; ii < numChannels; ++ii)
	{
		if (pChannels[ii]->m_flags & kFlagFloatChannel)
		{
			EvaluateChannelParams params;
			params.m_channelNameId = channelNameIds[ii];
			params.m_pAnim = pAnim;
			params.m_phase = phase;

			float floatValue = 0.0f;
			EvaluateCompressedFloatChannel(&params, &floatValue);

			MsgCon("Custom Float Channel: %s: %5.2f\n", DevKitOnly_StringIdToString(channelNameIds[ii]), floatValue);
			++numChannelsDrawn;
		}
		else
		{
			if (DrawAnimChannel(pAnim, GetSkeletonId(), channelNameIds[ii], phase, mirror, GetLocator(), kColorWhite))
			{
				++numChannelsDrawn;
			}
		}
	}

#if !FINAL_BUILD
	//Draw Debug channels
	{
		ScopedTempAllocator jj(FILE_LINE_FUNC);
		ListArray<StringId64> debugChannelNames(16);
		if (DebugAnimChannels::Get())
		{
			{
				AtomicLockJanitor debugChannelLockJanitor(DebugAnimChannels::Get()->GetLock(), FILE_LINE_FUNC);
				DebugAnimChannels::Get()->GetChannelsForAnim(pAnim, debugChannelNames);
			}
			for (U32F ii = 0; ii < debugChannelNames.Size(); ++ii)
			{
				if (DrawAnimChannel(pAnim, GetSkeletonId(), debugChannelNames[ii], phase, mirror, GetLocator(), kColorMagenta))
				{
					++numChannelsDrawn;
				}
			}
		}
	}
#endif

	return numChannelsDrawn;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DrawAnimChannelLocators(StringId64 layerId /* = INVALID_STRING_ID_64 */,
										   float phase /* = -1.0f */,
										   bool drawLocators /* = true */,
										   bool printFloats /* = true */) const
{
	STRIP_IN_FINAL_BUILD;

	U32F numChannelsDrawn = 0;

	if (layerId == INVALID_STRING_ID_64)
		layerId = SID("base");

	AnimControl* pAnimControl = GetAnimControl();
	if (pAnimControl)
	{
		AnimStateLayer* pAnimStateLayer = pAnimControl->GetStateLayerById(layerId);
		AnimSimpleLayer* pAnimSimpleLayer = pAnimControl->GetSimpleLayerById(layerId);

		// to support the actor viewer :(
		AnimStateLayer* pAnimStateLayer1 = pAnimControl->GetStateLayerById(SID("layer1"));
		AnimSimpleLayer* pAnimSimpleLayer1 = pAnimControl->GetSimpleLayerById(SID("layer1"));
		if (!pAnimStateLayer)
			pAnimStateLayer = pAnimStateLayer1;
		if (!pAnimSimpleLayer)
			pAnimSimpleLayer = pAnimSimpleLayer1;
		// END :(

		if (pAnimStateLayer)
		{
			numChannelsDrawn = DrawAnimChannelLocators(pAnimStateLayer, drawLocators, printFloats);
		}
		else if (pAnimSimpleLayer)
		{
			AnimSimpleInstance* pAnimSimpleInst = pAnimSimpleLayer->CurrentInstance();
			if (pAnimSimpleInst)
			{
				if (phase < 0.0f)
					phase = pAnimSimpleInst->GetPhase();

				const StringId64 animName = pAnimSimpleInst->GetAnimId();

				if (const ArtItemAnim* pAnim = pAnimControl->LookupAnim(animName).ToArtItem())
				{
					numChannelsDrawn = DrawAnimChannelLocators(pAnim, phase, pAnimSimpleInst->IsFlipped());
				}
			}
		}
	}

	if (drawLocators && (numChannelsDrawn == 0))
	{
		g_prim.Draw(DebugString(GetTranslation() + VECTOR_LC(0.0f, 0.5f, 0.0f), "[no channel locators]", kColorWhite, g_msgOptions.m_conScale));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::UpdateBSphereWs(FgAnimData* pAnimData, Locator align2world)
{
	// Since we have approx Aabb in object space we use that to calc the world space bounding sphere when the align moves
	// (since we can't rely on ws locator in joint cache
	// This may result in slightly bigger bounding sphere, hopefully not a big deal
	Point centerWs = align2world.TransformPoint(pAnimData->m_pBoundingInfo->m_aabb.GetCenter());
	Scalar radius(FLT_MAX);
	if(pAnimData->m_pBoundingInfo->m_aabb.IsValid())
	{
		radius = Scalar(0.5f) * Length(pAnimData->m_pBoundingInfo->m_aabb.GetScale());
	}

	pAnimData->m_pBoundingInfo->m_jointBoundingSphere = Sphere(centerWs, radius);

	ASSERT(IsFinite(pAnimData->m_pBoundingInfo->m_jointBoundingSphere.GetRadius()));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::AnimDataRecalcBoundAndSkinning(FgAnimData* pAnimData)
{
	const Locator align2world = GetLocator();

	JointCache* pJointCache = &pAnimData->m_jointCache;
	const Locator* pJointLocsWs = pJointCache->GetJointLocatorsWs();

	Vec4 scale = pAnimData->m_scale.GetVec4();
	scale.SetW(1.0f);
	Vec4 scaleInv(Select(Recip(scale), Vec4(kZero), Simd::CompareEQ(scale.QuadwordValue(), Simd::GetVecAllZero()))); // Safe inversion, sets zero for a component of scale that is zero

	const ArtItemSkeleton* pSkel = pAnimData->m_curSkelHandle.ToArtItem();
	U32F numTotalJoints = pSkel->m_numTotalJoints;
	U32F numAnimatedGameplayJoints = pSkel->m_numAnimatedGameplayJoints;

	if (!(pAnimData->m_flags & FgAnimData::kDisableBSphereCompute))
	{
		PROFILE(Processes, Update_BoundingInfo);

		const Locator* pJointsWs = pJointCache->GetJointLocatorsWs();
		if (pAnimData->m_useBoundingBox)
			ComputeBoundsSimpleAABB(pAnimData->m_pBoundingInfo, align2world, Vector(scale), Vector(scaleInv), pJointLocsWs, numAnimatedGameplayJoints, pAnimData->m_visSphereJointIndex, &pAnimData->m_visAabb, pAnimData->m_dynamicBoundingBoxPad, pAnimData->m_boundingSphereExcludeJoints);
		else
			ComputeBoundsSimple(pAnimData->m_pBoundingInfo, align2world, Vector(scale), Vector(scaleInv), pJointLocsWs, numAnimatedGameplayJoints, pAnimData->m_visSphereJointIndex, &pAnimData->m_visSphere, pAnimData->m_boundingSphereExcludeJoints);
	}

	const AnimExecutionContext* pCtx = GetAnimExecutionContext(pAnimData);

	{
		PROFILE(Processes, SkinningMatrices);

		// Allow the 'fastest' update only if we have zero helper joints (animated == total) and
		// there are no joint limits.
		if (numAnimatedGameplayJoints == numTotalJoints &&
			pCtx->m_pSkel->m_numSegments == 1 &&
			pSkel->m_numJointLimitDefs == 0)
		{
			Vec4 scaleX(Simd::RepX(scale.QuadwordValue()));
			Vec4 scaleY(Simd::RepY(scale.QuadwordValue()));
			Vec4 scaleZ(Simd::RepZ(scale.QuadwordValue()));

			const Transform world2align = align2world.AsInverseTransform();
			Mat44 world2alignMat44 = world2align.GetMat44();

			// Add inverse scale into our world2align matrix
			world2alignMat44.SetRow(0, world2alignMat44.GetRow(0) * scaleInv);
			world2alignMat44.SetRow(1, world2alignMat44.GetRow(1) * scaleInv);
			world2alignMat44.SetRow(2, world2alignMat44.GetRow(2) * scaleInv);
			world2alignMat44.SetRow(3, world2alignMat44.GetRow(3) * scaleInv);

			ANIM_ASSERT(pCtx->m_pSegmentData[0].m_pSkinningBoneMats);

			ndanim::JointParams params;
			U32F matIndex = 0;
			U32F iJoint;
			for (iJoint = 0; iJoint < numAnimatedGameplayJoints; iJoint++)
			{
				const Locator& jointWs = pJointLocsWs[iJoint];

				// Joint matrix in object space
				Mat44 jointOs = BuildTransform(jointWs.GetRotation(), jointWs.GetTranslation().GetVec4()) * world2alignMat44;

				// Get inverse bind bose and pre-multiply it with the scale
				Mat44 invBindPose = ndanim::GetInverseBindPoseTable(pAnimData->m_pSkeleton)[iJoint].GetMat44();

				// Pre-multiply it with scale
				// That way each vertex get scaled in bind pose before being skinned and skinning works even with non-uniform scale
				Mat44 preScaledInvBindPose;
				preScaledInvBindPose.SetRow(0, invBindPose.GetRow(0) * scaleX);
				preScaledInvBindPose.SetRow(1, invBindPose.GetRow(1) * scaleY);
				preScaledInvBindPose.SetRow(2, invBindPose.GetRow(2) * scaleZ);

				// We also need to adjust the bind pose translation of the joint with the scale
				// @@JS: slight optimization here possible because we only want multiplication of the 3x3 part plus there is probably more optimal way of doing Transpose(A) * B
				// We could also just pre-calculate and save the jointTranslationScaled vector for each joint
				preScaledInvBindPose.SetRow(3, Vec4(kZero));
				Mat44 scaleInJointBindSpace = MulTransformMatrix(Transpose(invBindPose), preScaledInvBindPose);
				Vec4 jointTranslationScaled = MulVectorMatrix(invBindPose.GetRow(3), scaleInJointBindSpace);
				jointTranslationScaled.SetW(1.0f);
				preScaledInvBindPose.SetRow(3, jointTranslationScaled);

				Mat34 newJointMatrix(preScaledInvBindPose * jointOs);

				// We may have joints scaled to zero to hide them
				params = pJointCache->GetJointParamsLs(iJoint);
				F32 jointScale = params.m_scale.X();

				for (int row = 0; row < 3; ++row)
				{
					pCtx->m_pAllSkinningBoneMats[matIndex++] = jointScale * newJointMatrix.GetRow(row);
				}
			}

			pCtx->m_processedSegmentMask_SkinningMats |= (1 << 0);
			pCtx->m_processedSegmentMask_OutputControls |= (1 << 0);
		}
		else
		{
			ndanim::JointParams* pJointParams = const_cast<ndanim::JointParams*>(pAnimData->m_jointCache.GetJointParamsLs());
			float* pInputControls = const_cast<float*>(pAnimData->m_jointCache.GetInputControls());
			float* pOutputControls = pCtx->m_pAllOutputControls; // const_cast<float*>(pAnimData->m_jointCache.GetOutputControls());

			U32 animCmdMemory[1024];
			AnimCmdList animCmdList;
			animCmdList.Init(&animCmdMemory, sizeof(U32) * 1024);

			animCmdList.AddCmd(AnimCmd::kBeginSegment);
			animCmdList.AddCmd(AnimCmd::kBeginAnimationPhase);
			animCmdList.AddCmd_BeginProcessingGroup();
			animCmdList.AddCmd(AnimCmd::kEndProcessingGroup);
			animCmdList.AddCmd(AnimCmd::kEndAnimationPhase);

			animCmdList.AddCmd_EvaluateFullPose(pJointParams, pOutputControls);

			animCmdList.AddCmd_EvaluateJointHierarchyCmds_Prepare(pInputControls);
			animCmdList.AddCmd_EvaluateJointHierarchyCmds_Evaluate();

			animCmdList.AddCmd(AnimCmd::kEndSegment);

			AnimExecutionContext animExecContext;
			animExecContext.Init(pAnimData->m_curSkelHandle.ToArtItem(),
								 &pAnimData->m_objXform,
								 const_cast<ndanim::JointParams*>(pAnimData->m_jointCache.GetJointParamsLs()),
								 pAnimData->m_jointCache.GetJointTransformsForOutput(),
								 const_cast<float*>(pAnimData->m_jointCache.GetInputControls()),
								 const_cast<float*>(pAnimData->m_jointCache.GetOutputControls()),
								 pAnimData->m_pPersistentData);

			// Hook up the plugin funcs
			animExecContext.m_pAnimPhasePluginFunc = EngineComponents::GetAnimMgr()->GetAnimPhasePluginHandler();
			animExecContext.m_pRigPhasePluginFunc = EngineComponents::GetAnimMgr()->GetRigPhasePluginHandler();

			animExecContext.m_animCmdList = animCmdList;

			animExecContext.m_pAllSkinningBoneMats = pCtx->m_pAllSkinningBoneMats;
			animExecContext.m_pAllOutputControls = pCtx->m_pAllOutputControls;

			for (int segmentIndex = 0; segmentIndex < animExecContext.m_pSkel->m_numSegments; ++segmentIndex)
			{
				animExecContext.m_pSegmentData[segmentIndex].m_pSkinningBoneMats = pCtx->m_pSegmentData[segmentIndex].m_pSkinningBoneMats;
				animExecContext.m_pSegmentData[segmentIndex].m_pOutputControls = pCtx->m_pSegmentData[segmentIndex].m_pOutputControls;
				ANIM_ASSERT(animExecContext.m_pSegmentData[segmentIndex].m_pSkinningBoneMats);
			}

			U32 requiredSegmentMask = (1U << animExecContext.m_pSkel->m_numSegments) - 1;

			ProcessRequiredSegments(requiredSegmentMask, AnimExecutionContext::kOutputSkinningMats | AnimExecutionContext::kOutputOutputControls, &animExecContext, nullptr);

			pCtx->m_processedSegmentMask_SkinningMats |= requiredSegmentMask;
			pCtx->m_processedSegmentMask_OutputControls |= requiredSegmentMask;

		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::FastAnimDataUpdateBoundAndSkinning(FgAnimData* pAnimData)
{
	OMIT_FROM_FINAL_BUILD(++g_animOptimization.m_stats.m_numActiveComposites);

	EngineComponents::GetAnimMgr()->SetIsInBindPose(pAnimData, false);
	OMIT_FROM_FINAL_BUILD(++g_animOptimization.m_stats.m_numPropagateBoneMats);

	const Locator align2world = GetLocator();
	pAnimData->SetXform(align2world, false);

	const U32 numOutputControls = pAnimData->m_pSkeleton->m_numOutputControls;

	AnimExecutionContext* pCtx = GetAnimExecutionContext(pAnimData);
	const AnimExecutionContext* pPrevCtx = GetPrevFrameAnimExecutionContext(pAnimData);

	pCtx->m_pAllOutputControls = NDI_NEW(kAllocDoubleGameToGpuRing, kAlign16) float[numOutputControls];
	ANIM_ASSERT(pPrevCtx);
	memcpy(pCtx->m_pAllOutputControls, pPrevCtx->m_pAllOutputControls, numOutputControls * sizeof(float));

	AnimDataRecalcBoundAndSkinning(pAnimData);

	// Something has moved the joints and we're not animating. We need to set this flag so that skinning does not get optimized out
	// Even if we have animation disabled something (for example physics or some resourceful gameplay programmer) can move the joints
	// In that case we need to set this flag EVERY FRAME so that skinning does not get optimized out
	pAnimData->SetJointsMoved();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void NdGameObject::FastAnimDataUpdate(FgAnimData* pAnimData, F32 dt)
{
	FastAnimDataUpdateInner(pAnimData, dt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void NdGameObject::FastAnimDataUpdateInner(FgAnimData* pAnimData, F32 dt, bool skipRigidBodySyncFromGame)
{
	if (EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
	{
		PropagateSkinningBoneMats(pAnimData);

#ifndef FINAL_BUILD
		GetAnimExecutionContext(pAnimData)->Validate();
#endif
		return;
	}

	OMIT_FROM_FINAL_BUILD(++g_animOptimization.m_stats.m_numFastObjects);
	OMIT_FROM_FINAL_BUILD(if (pAnimData->m_flags & FgAnimData::kIsInBindPose) ++g_animOptimization.m_stats.m_numBpObjects);
	// Process Error 3D has no pGo!?
	NdGameObject* pGo = static_cast<NdGameObject*>(pAnimData->m_hProcess.ToMutableProcess());
	GAMEPLAY_ASSERT(pGo);
	GAMEPLAY_ASSERT(pGo->IsKindOf(SID("NdGameObject")));

	PROFILE_STRINGID(Processes, FastAnimDataUpdate, pGo->GetUserId());

	OMIT_FROM_FINAL_BUILD(if (EngineComponents::GetAnimMgr()->IsVisible(pAnimData)) ++g_animOptimization.m_stats.m_numVisibleObjects);

	pGo->FastProcessUpdate(dt);

//#if !FINAL_BUILD
//	if (IDrawControl* pDrawCtrl = pGo->GetDrawControl())
//	{
//		const bool isSelected = DebugSelection::Get().IsProcessSelected(pGo);
//		pDrawCtrl->SetObjectSelected(isSelected);
//	}
//#endif

	// unless the optimization is disabled, we should never get here for bind-pose objects that are not visible...
	ASSERT(g_animOptimization.m_disableBindPoseVisibilityOpt || !((pAnimData->m_flags & FgAnimData::kIsInBindPose) && !EngineComponents::GetAnimMgr()->IsVisible(pAnimData)));

	if (TRUE_IN_FINAL_BUILD(!g_animOptimization.m_testopt_SkipShimmer))
	{
		if (pGo->m_goFlags.m_updatesDisabled)
		{
			pGo->UpdateShimmer();
		}
	}

	CompositeBody* pCompBody = pGo->m_pCompositeBody;

	const Locator loc = pGo->GetLocator();
	bool alignChanged = (pGo->m_prevLocator != loc);
	pGo->m_prevLocator = loc;

	bool needJointCacheUpdate = false;

	if (pGo->m_hasJointWind)
	{
		pGo->UpdateJointWindNoAnim(pAnimData->m_jointCache.GetDefaultLocalSpaceJoints());
		needJointCacheUpdate = true;
		if (pCompBody)
			pCompBody->SetNeedSyncPhysicsToGame();
	}

	if (pCompBody && HavokNotPaused())
	{
		PROFILE_DETAILED(Processes, FastUpdatePhys);

		OMIT_FROM_FINAL_BUILD(++g_animOptimization.m_stats.m_numCompositeBodyObjects);
		if (TRUE_IN_FINAL_BUILD(!g_animOptimization.m_testopt_SkipPhysUpdate))
		{
			if (pCompBody->NeedSyncPhysicsToGame())
			{
				needJointCacheUpdate = pCompBody->SyncPhysicsToGame();
			}
		}
	}

	if (needJointCacheUpdate)
	{
		pGo->FastAnimDataUpdateBoundAndSkinning(pAnimData);
	}
	else
	{
		PROFILE_DETAILED(Processes, FastUpdateNotMoved);

		OMIT_FROM_FINAL_BUILD(++g_animOptimization.m_stats.m_numNonCompositeObjects);

		if (alignChanged)
		{
			OMIT_FROM_FINAL_BUILD(++g_animOptimization.m_stats.m_numLocatorChangedObjects);

			// TODO:
			// The true here should propagate align change into joint cache but this is really wonky!
			// If joint was moved by physics and now is sleeping, the m_pJointTransforms in joint cache were not updated
			// So if someone asks for a Ws locator of a joint that is a child of such joint he will now probably get a wrong value
			// On the other hand if we make sure the m_pJointTransforms are always up-to-date even for procedurally driven joints
			// we can skip the joint cache Ws locators update bellow
			pAnimData->SetXform(loc);

			bool updatedRoot = false;
			JointCache* pJointCache = &pAnimData->m_jointCache;
			const Transform& objXform = pAnimData->m_objXform;

			const AnimExecutionContext* pPrevCtx = GetPrevFrameAnimExecutionContext(pAnimData);
			if (pPrevCtx &&
				pPrevCtx->m_pAllSkinningBoneMats &&
				(pPrevCtx->m_processedSegmentMask_SkinningMats & 1))
			{
				if (pCompBody)
				{
					// Update ws locators in joint cache since there may be keyframed collision depending on them
					// We can't use joint Ls because they may not be up-to-date. So we derive it from skinning matrices
					// which is the only trust-worthy source
					for (U32F iBody = 0; iBody < pCompBody->GetNumBodies(); iBody++)
					{
						I16 iJoint = pCompBody->GetBody(iBody)->GetJointIndex();
						if (iJoint >= 0)
						{
							if (iJoint == 0)
								updatedRoot = true;

							const Transform jointXform(reinterpret_cast<Mat34*>(pPrevCtx->m_pAllSkinningBoneMats)[iJoint].GetMat44());
							ANIM_ASSERTF(IsReasonable(jointXform), ("Bad skinning joint %d for object %s", iJoint, pGo->GetName()));

							Transform bindPoseXform(kIdentity);
							const bool success = pJointCache->GetBindPoseJointXform(bindPoseXform, iJoint);
							ANIM_ASSERTF(success, ("Failed to get bindpose joint %d for %s", iJoint, pGo->GetName()));
							ANIM_ASSERTF(IsReasonable(bindPoseXform), ("Bad bindpose joint %d for object %s", iJoint, pGo->GetName()));

							Transform xform = bindPoseXform * jointXform * objXform;
							xform.SetXAxis(SafeNormalize(xform.GetXAxis(), SMath::kUnitXAxis));
							xform.SetYAxis(SafeNormalize(xform.GetYAxis(), SMath::kUnitYAxis));
							xform.SetZAxis(SafeNormalize(xform.GetZAxis(), SMath::kUnitZAxis));

							const Locator jointLocWs = Locator(xform);
							ANIM_ASSERT(IsReasonable(jointLocWs));

							pJointCache->OverwriteJointLocatorWs(iJoint, jointLocWs);
						}
					}
				}

				if (!updatedRoot && pGo->IsAnimationEnabled())
				{
					updatedRoot = true; // always update the root because it's used for a lot of things
					const int iJoint = 0;
					const Transform jointXform(reinterpret_cast<Mat34*>(pPrevCtx->m_pAllSkinningBoneMats)[iJoint].GetMat44());
					ANIM_ASSERTF(IsReasonable(jointXform), ("Bad root skinning joint for object %s", pGo->GetName()));

					Transform bindPoseXform(kIdentity);
					const bool success = pJointCache->GetBindPoseJointXform(bindPoseXform, iJoint);
					ANIM_ASSERTF(success, ("Failed to get root bindpose joint for %s", pGo->GetName()));
					ANIM_ASSERTF(IsReasonable(bindPoseXform), ("Bad bindpose root joint for object %s", iJoint, pGo->GetName()));

					Transform xform = bindPoseXform * jointXform * objXform;
					xform.SetXAxis(SafeNormalize(xform.GetXAxis(), SMath::kUnitXAxis));
					xform.SetYAxis(SafeNormalize(xform.GetYAxis(), SMath::kUnitYAxis));
					xform.SetZAxis(SafeNormalize(xform.GetZAxis(), SMath::kUnitZAxis));

					const Locator jointLocWs = Locator(xform);
					ANIM_ASSERT(IsReasonable(jointLocWs));

					pJointCache->OverwriteJointLocatorWs(iJoint, jointLocWs);
				}
			}

			// Update BSphere Ws
			if (!(pAnimData->m_flags & FgAnimData::kDisableBSphereCompute))
			{
				pGo->UpdateBSphereWs(pAnimData, loc);
			}
		}

#if BIND_POSE_OPTIM
		if ((pAnimData->m_flags & FgAnimData::kIsInBindPose)
		&&  pAnimData->m_pSkinningBoneMats != g_pIdentityBoneMats)
		{
			EngineComponents::GetAnimMgr()->SetIsInBindPose(pAnimData, false);
			pAnimData->m_flags |= FgAnimData::kTestIsInBindPose; // re-test to see if we're still in bind pose
		}
#endif

#if BIND_POSE_OPTIM
		if (!(pAnimData->m_flags & FgAnimData::kIsInBindPose))
#endif
		{
			PropagateSkinningBoneMats(pAnimData);

			#if !FINAL_BUILD && ENABLE_ND_GAME_OBJECT_IDENTITY_CHECK
				if (AreSkinningMatricesIdentity(pAnimData->m_pLastFrameSkinningBoneMats, pAnimData->m_pSkeleton->m_numTotalJoints))
				{
					++g_animOptimization.m_stats.m_numIdentityObjects;
				}
			#endif
		}
	}

	if (pGo->m_pCompositeBody && HavokNotPaused())
	{
		if ((needJointCacheUpdate | alignChanged | pGo->m_fastAnimUpdateMoved) & !skipRigidBodySyncFromGame)
		{
			pGo->m_pCompositeBody->SyncGameToPhysics();
		}
		pGo->m_pCompositeBody->UpdateCloth();
	}

	pGo->m_fastAnimUpdateMoved = needJointCacheUpdate || alignChanged;

	if (pGo->GetSplasherController() && pGo->ShouldUpdateSplashers())
		pGo->GetSplasherController()->Update();

#if BIND_POSE_OPTIM
	if ((pAnimData->m_flags & FgAnimData::kTestIsInBindPose) && pAnimData->m_pSkinningBoneMats)
	{
		BINDPOSE_ASSERT((0 != (pAnimData->m_flags & FgAnimData::kIsInBindPose)) == (pAnimData->m_pSkinningBoneMats == g_pIdentityBoneMats));
		BINDPOSE_ASSERT(0 == (pAnimData->m_flags & FgAnimData::kIsInBindPose));

		if (pAnimData->m_pSkeleton->m_numTotalJoints <= kMaxIdentityJoints &&
			AreSkinningMatricesIdentity(pAnimData->m_pSkinningBoneMats, pAnimData->m_pSkeleton->m_numTotalJoints))
		{
			pAnimData->m_pLastFrameSkinningBoneMats = pAnimData->m_pSkinningBoneMats = g_pIdentityBoneMats;
			pAnimData->m_pClothSkinningBoneMatsLabels = nullptr;
			pAnimData->m_numClothSkinningBoneMatsLabels = 0;
			EngineComponents::GetAnimMgr()->SetIsInBindPose(pAnimData, true);

			#if !FINAL_BUILD
				if (g_animOptimization.m_stats.m_maxIdentityJoints < pAnimData->m_pSkeleton->m_numTotalJoints)
					g_animOptimization.m_stats.m_maxIdentityJoints = pAnimData->m_pSkeleton->m_numTotalJoints;
			#endif
		}
		else
		{
			EngineComponents::GetAnimMgr()->SetIsInBindPose(pAnimData, false);
		}

		// we only turned this on for one frame to see if we're in bind pose or not...
		pAnimData->m_flags &= ~FgAnimData::kTestIsInBindPose;
	}
#endif

	if (pGo->m_fastAnimUpdateMoved)
	{
		pGo->UpdateAmbientOccluders();
	}

	pGo->ManuallyRefreshSnapshot();

	if (pGo->m_pDynNavBlocker)
	{
		pGo->UpdateNavBlocker();
	}

#ifndef FINAL_BUILD
	GetAnimExecutionContext(pAnimData)->Validate();
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
//static
void NdGameObject::PropagateSkinningBoneMats(FgAnimData* pAnimData)
{
	//PROFILE(Processes, PropagateSkinningBoneMats);

	OMIT_FROM_FINAL_BUILD(++g_animOptimization.m_stats.m_numPropagateBoneMats);

	EngineComponents::GetAnimMgr()->SetIsInBindPose(pAnimData, false);

	AnimExecutionContext* pCtx = GetAnimExecutionContext(pAnimData);
	const AnimExecutionContext* pPrevCtx = GetPrevFrameAnimExecutionContext(pAnimData);
	ANIM_ASSERT(pPrevCtx);
	ANIM_ASSERT(pCtx->m_pAnimDataHack == pPrevCtx->m_pAnimDataHack);

	const U32F numJoints = pAnimData->m_curSkelHandle.ToArtItem()->m_numTotalJoints;
	const U32F boneMatsSize = numJoints * sizeof(Mat34);
	memcpy(pCtx->m_pAllSkinningBoneMats, pPrevCtx->m_pAllSkinningBoneMats, boneMatsSize);

	const U32F numOutputControls = pAnimData->m_pSkeleton->m_numOutputControls;
	memcpy(pCtx->m_pAllOutputControls, pPrevCtx->m_pAllOutputControls, numOutputControls * sizeof(float));

	pCtx->m_processedSegmentMask_SkinningMats |= pPrevCtx->m_processedSegmentMask_SkinningMats;
	pCtx->m_processedSegmentMask_OutputControls |= pPrevCtx->m_processedSegmentMask_OutputControls;

#ifndef FINAL_BUILD
	pCtx->Validate();
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::GetUpdateFlagString(char flagChars[8]) const
{
	Process::GetUpdateFlagString(flagChars);

	AnimControl* pAnimCtrl = GetAnimControl();
	if (pAnimCtrl)
	{
		U32 bits = pAnimCtrl->GetAnimData().m_flags;
		if (bits & FgAnimData::kEnableAsyncPostAnimUpdate)		flagChars[3] = 'A';
		if (bits & FgAnimData::kEnableAsyncPostAnimBlending)	flagChars[4] = 'B';
		if (bits & FgAnimData::kEnableAsyncPostJointUpdate)		flagChars[5] = 'J';
	}

	const CompositeBody* pCompositeBody = GetCompositeBody();
	const RigidBody* pRigidBody = GetRigidBody();
	if (pCompositeBody)
	{
		U32F numBodies = pCompositeBody->GetNumBodies();
		U32F numFseBodies = pCompositeBody->GetNumBodiesWantingFrameSyncEnd();

		if (numBodies == numFseBodies)							flagChars[6] = 'E';
		else if (numFseBodies != 0)								flagChars[6] = 'e';
	}
	else if (pRigidBody)
	{
		if (pRigidBody->WantFrameSyncEnd())						flagChars[6] = 'E';
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DebugShowBodyMarkers()
{
	Point pos = GetLocator().Pos();
	CompositeBody* pCompositeBody = GetCompositeBody();
	RigidBody* pRigidBody = GetRigidBody();
	if ((pCompositeBody || pRigidBody) && GetRenderCamera(0).IsSphereInFrustum(Sphere(pos, 4.0f)))
	{
		U32 bodyCount;
		if (pCompositeBody)
		{
			bodyCount = pCompositeBody->GetNumBodies();
		}
		else
		{
			bodyCount = 1;
		}

		char buf[64];
		snprintf(buf, 63, "%u", bodyCount);
		buf[63] = '\0';

		const F32 kBodyMarkerHeight = 0.2f;
		g_prim.Draw(DebugString(pos + Scalar(kBodyMarkerHeight) * Vector(kUnitYAxis),	buf, kColorCyan));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DebugShowProcess(ScreenSpaceTextPrinter& printer) const
{
	STRIP_IN_FINAL_BUILD;

	ParentClass::DebugShowProcess(printer);

	const Point pos = GetLocator().Pos();
	float dist = Length(pos - g_mainCameraInfo[0].GetPosition());

	if (dist < 25.0f)
	{
		StringBuilder<512> desc;
		desc.format("%s", GetStateName());

		if (m_pCompositeBody)
		{
			desc.append_format(" (CompositeBody #PJU = %u)\n", m_pCompositeBody->GetNumCallsToPostJointUpdate());
		}

		printer.PrintText(kColorCyan, desc.c_str());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DoFrameSetupOfJointOverridePlugin()
{
	FgAnimData* pAnimData = GetAnimData();

	FgAnimData::PluginParams* pParams = pAnimData->m_pPluginParams;
	while (pParams && pParams->m_pluginName != INVALID_STRING_ID_64)
	{
		if (pParams->m_pluginName == SID("joint-override"))
		{
			pParams->m_pluginDataSize = sizeof(EvaluateJointOverridePluginData);
			pParams->m_pPluginData = m_pJointOverridePluginCmd; // This has to be persistent when game is paused.
			pParams->m_enabled = true;

			EvaluateJointOverridePluginData* pPluginCmd = (EvaluateJointOverridePluginData*)pParams->m_pPluginData;
			pPluginCmd->m_pOverrideData = m_pJointOverrideData;

			break;
		}

		pParams++;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// NdGameObject::Active State Implementation
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::Active::Enter()
{
	ParentClass::State::Enter();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::Active::Update()
{
	NdGameObject& go = Self();
	go.ProcessUpdateInternal();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::ProcessUpdateInternal()
{
	//PROFILE(Processes, ProcessUpdateInternal);
	IDrawControl* pDrawCtrl = GetDrawControl();

//#if !FINAL_BUILD
//	if (pDrawCtrl)
//	{
//		const bool isSelected = DebugSelection::Get().IsProcessSelected(this);
//		pDrawCtrl->SetObjectSelected(isSelected);
//	}
//#endif

	if (m_pInteractCtrl != nullptr)
		m_pInteractCtrl->Update();

	if (pDrawCtrl && EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused >= m_forceUpdateHighContrastModeFrame)
	{
		m_forceUpdateHighContrastModeFrame = -1000;
		pDrawCtrl->UpdateAllMeshesHCMType(GetHighContrastModeMeshExcludeFilter());
	}

	DebugDrawPickups();

	// Follow splines if requested.
	SplineTracker* pSplineTracker = GetSplineTracker();
	if (pSplineTracker && pSplineTracker->GetSpline())
	{
		PROFILE(Processes, Spline);

		UpdateSplineTracker();

		F32 speed = pSplineTracker->GetCurrentSpeed();
		F32 currentSplineDistance_m = pSplineTracker->GetCurrentRealDistance_m();
		const CatmullRom* pCurSpline = pSplineTracker->GetSpline();
		const CatmullRom* pNotifySpline = m_hNotifySpline.ToCatmullRom();

		if (pNotifySpline == pCurSpline && m_splineNotifyDistance_m != NdGameObject::kInvalidSplineNotifyDistance)
		{
			if ((speed >= 0.0f && currentSplineDistance_m >= m_splineNotifyDistance_m) ||
				(speed <= 0.0f && currentSplineDistance_m <= m_splineNotifyDistance_m))
			{
				// Do this first, as sending the event may immediately turn around and set another notify-at.
				ClearSplineNotify();

				// Now tell the process requesting notification that we are there.
				m_ssAtSplineDistanceAction.Stop();
			}
		}
	}

	if (!m_bOnIsValidBindTargetCalled)
		CheckIsValidBindTarget();

	{
		//PROFILE(Processes, FinishUpdate);
		FinishUpdate();
	}

	if (GetInstanceTextureTable(0))
	{
		PROFILE(Processes, InstanceTextures);

		if (m_goFlags.m_needsOverlayInstanceTexture)
		{
			ApplyOverlayInstanceTextures();
		}

		if (m_goFlags.m_needsSpecularCubemapTexture)
		{
			I16 oldBgIndex = m_cubemapBgIndex;
			I16 oldIndex = m_cubemapIndex;

			const Background* pCurBg = g_scene.GetBackground(m_cubemapBgIndex);
			const CubemapSet* pCurCubemapSet = pCurBg ? pCurBg->GetSpecularCubemapSet() : nullptr;
			bool bUpdateShaderParameters = FALSE_IN_FINAL_BUILD(g_updateSpecCubemapEveryFrame || g_scriptUpdateSpecCubemapEveryFrame);
			bool oldValid = pCurBg != nullptr && pCurBg->GetNameId() == m_cubemapBgNameId;
			bool changed = false;

			if (!oldValid ||
			    m_goFlags.m_updateSpecCubemapEveryFrame || g_updateSpecCubemapEveryFrame || g_scriptUpdateSpecCubemapEveryFrame ||
				(pCurCubemapSet && pCurCubemapSet->GetNumCubemaps() <= m_cubemapIndex))
			{
				m_cubemapIndex = -1;
				m_cubemapBgIndex = -1;
				m_cubemapBgNameId = INVALID_STRING_ID_64;

				LoadedTexture* pOldTexture = GetInstanceTextureTable(0)->GetLoadedTexture(kSpecularCubemapTexture);
				LoadedTexture* pNewTexture = nullptr;

				float closestDistance = FLT_MAX;
				float highestPriority = -1.0f;

				//search all loaded levels to find nearest probe
				for (int iBg = 0, numBackgrounds = g_scene.GetNumBackgrounds(); iBg < numBackgrounds; ++iBg)
				{
					const Background* pBg = g_scene.GetBackground(iBg);
					CubemapSet* pCubemapSet = pBg->GetSpecularCubemapSet();
					bool levelVisible = !pBg->GetFlags().m_disableDraw && pBg->GetFlags().m_displayLevel;

					if (pCubemapSet && pCubemapSet->GetNumCubemaps() > 0 && levelVisible)
					{
						ALWAYS_ASSERT(GetDrawControl());
						ALWAYS_ASSERT(GetDrawControl()->GetNumMeshes() > 0);

						FgInstance* pInstance = GetDrawControl()->GetMesh(0).m_lod[0].m_pInstance;

						Point worldPos = (pInstance) ? GetLocator().TransformPoint(Point(pInstance->m_probeJointBindPosition)) : GetTranslation();
						Point localPos = worldPos * pBg->GetLoc().AsInverseTransform();

						const CubemapPoint* pNearestProbe = nullptr;
						pCubemapSet->FindNearestProbe(localPos, closestDistance, highestPriority, &pNearestProbe, &closestDistance, &highestPriority, nullptr, nullptr, nullptr);

						if (pNearestProbe)
						{
							m_cubemapIndex = pCubemapSet->GetCubemapIndex(pNearestProbe);
							m_cubemapBgIndex = iBg;
							m_cubemapBgNameId = pBg->GetNameId();

							pNewTexture = pNearestProbe->m_pTexture;
						}
					}
				}

				if (m_cubemapIndex < 0) // not found? use first probe in the first level we can find
				{
					for (uint32_t iBg = 0, numBackgrounds = g_scene.GetNumBackgrounds(); iBg < numBackgrounds; ++iBg)
					{
						const Background* pBg = g_scene.GetBackground(int(iBg));
						CubemapSet* pCubemapSet = pBg->GetSpecularCubemapSet();
						if (pCubemapSet && pCubemapSet->GetNumCubemaps() > 0)
						{
							m_cubemapIndex = 0;
							m_cubemapBgIndex = iBg;
							m_cubemapBgNameId = pBg->GetNameId();

							pNewTexture = pCubemapSet->GetCubemap(m_cubemapIndex).m_pTexture;
							break;
						}
					}
				}

				changed = !oldValid || m_cubemapBgIndex != oldBgIndex || m_cubemapIndex != oldIndex;

				if (changed)
				{
					bUpdateShaderParameters = pNewTexture != nullptr;

					// Keep a ref on the new texture so it doesn't get unloaded while we're using it!
					if (pNewTexture && TRUE_IN_FINAL_BUILD(pNewTexture != g_postShading.GetDebugRainbowCubemapTexture()))
					{
						EngineComponents::GetTextureMgr()->AddReference(pNewTexture);
						bUpdateShaderParameters = true;
					}

					// Release the ref on the old one
					if (pOldTexture && TRUE_IN_FINAL_BUILD(pOldTexture != g_postShading.GetDebugRainbowCubemapTexture()))
					{
						EngineComponents::GetTextureMgr()->Unlink(pOldTexture);
					}
				}
			}

			// In final we run this everytime as before. Seems like cubemaps may be dynamically captured at runtime so just being safe.
			if (changed || FALSE_IN_FINAL_BUILD(true))
			{
				const CubemapPoint* pSpecCubemap = nullptr;

				if (m_cubemapBgIndex >= 0 && m_cubemapIndex >= 0)
				{
					const Background* pBg = g_scene.GetBackground(m_cubemapBgIndex);
					ALWAYS_ASSERT(pBg);

					const CubemapSet* pCubemapSet = pBg->GetSpecularCubemapSet();
					ALWAYS_ASSERT(pCubemapSet);
					ALWAYS_ASSERT(m_cubemapIndex < pCubemapSet->GetNumCubemaps());

					pSpecCubemap = &pCubemapSet->GetCubemap(m_cubemapIndex);

					if (bUpdateShaderParameters)
					{
						ALWAYS_ASSERT(m_pSpecularIblProbeParameters);

						pCubemapSet->FillCubepointConstData(&m_pSpecularIblProbeParameters->m_cubepointConstants, *pSpecCubemap->m_pointData, true);
						pCubemapSet->FillCubepointConstData(&m_pSpecularIblProbeParameters->m_parentCubepointConstants, *pSpecCubemap->m_pointData, true);

						m_pSpecularIblProbeParameters->m_levelToWorld = Transpose(pBg->GetLoc().AsTransform().GetMat44());
						m_pSpecularIblProbeParameters->m_worldToLevel = Transpose(AffineInverse(pBg->GetLoc().AsTransform().GetMat44()));
						m_pSpecularIblProbeParameters->m_isUnbounded = pSpecCubemap->m_pointData->m_unbounded ? 1 : 0;
						m_pSpecularIblProbeParameters->m_isParentUnbounded = pSpecCubemap->m_pointData->m_unbounded ? 1 : 0;

						SetSpecularIblParameters(GetDrawControl(), m_pSpecularIblProbeParameters);
					}
				}

				for (int i = 0; i < GetNumInstanceTextureTables(); i++)
				{
					if (pSpecCubemap && pSpecCubemap->m_pTexture)
					{
						AddInstanceTexture(i, kSpecularCubemapTexture, pSpecCubemap->GetTexture());
						AddInstanceTexture(i, kParentSpecularCubemapTexture, pSpecCubemap->GetTexture());
					}
					else
					{
						AddInstanceTexture(i, kSpecularCubemapTexture, ndgi::GetBlackCubeTexture());
						AddInstanceTexture(i, kParentSpecularCubemapTexture, ndgi::GetBlackCubeTexture());
					}
				}
			}
		}

		if (m_goFlags.m_needsWetmaskInstanceTexture)
		{
			if (m_goFlags.m_usePlayersWetMask)
			{
				I32 wetMaskIndex = g_fxMgr.GetPlayersWetMaskIndex();
				if (wetMaskIndex != GetWetMaskIndex())
				{
					SetWetMaskIndex(wetMaskIndex);
					SetFgInstanceFlag(FgInstance::kHasWetness);
					g_fxMgr.AddWetMaskDependent(wetMaskIndex, GetProcessId());
				}
			}
		}

		if (m_goFlags.m_needsDamagemaskInstanceTexture) // this is set from shader needs
		{
			//ndgi::Texture hTex = ndgi::GetBlackTexture();

			//if (g_fxMgr.BloodMaskEnabled())
			//	hTex = g_fxMgr.GetFxRenderMask(GetBloodMaskHandle());

			ndgi::Texture hTex = g_particleRtMgr.FindEntryOrBlackTexture(this);

			if (hTex.Valid())
			{
				for (int i = 0; i < GetNumInstanceTextureTables(); i++)
				{
					AddInstanceTexture(i, kBloodMaskTexture, hTex);
				}
			}
		}

		if (m_goFlags.m_needsDynamicGoremaskInstanceTexture) // this is set from shader needs
		{
			ndgi::Texture hTex = g_particleRtMgr.FindEntryOrBlackTexture(this, SID("dynamic-gore"));

			if (hTex.Valid())
			{
				for (int i = 0; i < GetNumInstanceTextureTables(); i++)
				{
					AddInstanceTexture(i, kGoreDynamicTexture, hTex);
				}
			}
		}

		if (m_goFlags.m_needsWoundmaskInstanceTexture) // this is set from shader needs
		{
			ndgi::Texture hTex = g_particleRtMgr.FindEntryOrBlackTexture(this, SID("wound-mask"));

			if (hTex.Valid())
			{
				for (int i = 0; i < GetNumInstanceTextureTables(); i++)
				{
					AddInstanceTexture(i, kWoundMaskTexture, hTex);
				}
			}
		}

		if (m_goFlags.m_needsBurnMaskInstanceTexture) // this is set from shader needs
		{
			ndgi::Texture hTex = g_particleRtMgr.FindEntryOrBlackTexture(this, SID("burn-mask"));

			if (hTex.Valid())
			{
				for (int i = 0; i < GetNumInstanceTextureTables(); i++)
				{
					AddInstanceTexture(i, kBurnMaskTexture, hTex);
				}
			}
		}

		if (m_goFlags.m_needsSnowMaskInstanceTexture) // this is set from shader needs
		{
			ndgi::Texture hTex = g_particleRtMgr.FindEntryOrBlackTexture(this, SID("snow-mask"));
			ndgi::Texture hTex2 = hTex;

			if (m_goFlags.m_hasBackpack)
			{
				hTex2 = g_particleRtMgr.FindEntryOrBlackTexture(this, SID("snow-mask-backpack"));
			}

			if (hTex.Valid())
			{
				for (int i = 0; i < GetNumInstanceTextureTables(); i++)
				{
					AddInstanceTexture(i, kSnowMaskTexture, i == 0 ? hTex : hTex2);
				}
			}
		}
		if (m_goFlags.m_needsMudmaskInstanceTexture) // this is set from shader needs
		{
			ndgi::Texture hTex = g_particleRtMgr.FindEntryOrBlackTexture(this, SID("mud-mask"));
			ndgi::Texture hTex2 = hTex;

			if (m_goFlags.m_hasBackpack)
			{
				hTex2 = g_particleRtMgr.FindEntryOrBlackTexture(this, SID("mud-mask-backpack"));
			}

			if (hTex.Valid())
			{
				for (int i = 0; i < GetNumInstanceTextureTables(); i++)
				{
					AddInstanceTexture(i, kMudMaskTexture, i == 0 ? hTex : hTex2);
				}
			}
		}
	}

	if (m_pJointOverrideData)
	{
		DoFrameSetupOfJointOverridePlugin();
	}

	UpdateAssociatedBackground();

	if (m_pFoliageSfxController != nullptr)
	{
		m_pFoliageSfxController->ActiveUpdate();
	}
}

void NdGameObject::SetEnableFgVis(bool enable)
{
	m_goFlags.m_disableFgVis = !enable;
}

// The FgInstance needs to know the current associated background for visibility purposes
void NdGameObject::UpdateAssociatedBackground()
{
	IDrawControl* pDrawCtrl = GetDrawControl();
	if (pDrawCtrl)
	{
		const I32F numMeshes = pDrawCtrl->GetNumMeshes();
		for (I32F i = 0; i < numMeshes; ++i)
		{
			IDrawControl::Mesh* pMesh = &pDrawCtrl->GetMesh(i);

			for (int j = 0; j < pMesh->m_availableLods; j++)
			{
				IDrawControl::Mesh::Lod* pLod = &pMesh->m_lod[j];

				if (pLod && pLod->m_pInstance)
				{
					FgInstance* pInstance = pLod->m_pInstance;

					const Background* pBg = GetAssociatedLevel() ? GetAssociatedLevel()->GetBackground() : nullptr;
					if (m_goFlags.m_disableFgVis)
						pBg = nullptr;

					pInstance->SetAssociatedBackground(pBg);
				}
			}
		}
	}
}

//static Color GetIgcLogColor(const NdGameObject* pGo)
//{
//	switch (pGo->GetType())
//	{
//	case g_type_Player:
//		return kColorGreen;
//	case g_type_Infected:
//		return kColorGray;
//	case g_type_Npc:
//		return kColorBlue;
//	case g_type_Horse:
//	case g_type_DriveableVehicle:
//	case g_type_RailVehicle:
//		return kColorOrange;
//	default:
//		return kColorYellow;
//	}
//}

bool NdGameObject::DrawPlayingIgc()
{
	STRIP_IN_SUBMISSION_BUILD_VALUE(false);

	if (m_playingIgcAnim == INVALID_STRING_ID_64)
		return false;

	if (SsAnimateController::AnimIsBlacklistedForLogEvent(m_playingIgcAnim))
		return false;

	if (!g_ssMgr.IsInIgcRangeOfCamera(this))
		return false;

	// Try to find the anim in anim control and get some info
	F32 frame = 0;
	F32 numFrames = 0;
	bool lastFrame = false;
	bool looping = false;
	{
		const AnimSimpleLayer* pSimpleLayer = GetAnimControl()->GetSimpleLayerById(SID("base"));
		if (pSimpleLayer)
		{
			const AnimSimpleInstance* pInstance = pSimpleLayer->CurrentInstance();
			if (pInstance && pInstance->GetAnimId() == m_playingIgcAnim)
			{
				frame = pInstance->GetMayaFrame();
				numFrames = (pInstance->GetClip() ? 30.0f * pInstance->GetClip()->m_secondsPerFrame * (static_cast<float>(pInstance->GetFrameCount()) - 1.0f) : 0.0f);
				looping = pInstance->IsLooping();
				if (pInstance->GetPhase() >= 1.0f)
					lastFrame = true;
			}
		}
		else
		{
			const AnimStateLayer* pStateLayer = GetAnimControl()->GetStateLayerById(SID("base"));
			if (pStateLayer)
			{
				const AnimStateInstance* pInstance = pStateLayer->CurrentStateInstance();
				if (pInstance && pInstance->GetPhaseAnim() == m_playingIgcAnim)
				{
					const AnimStateSnapshot& stateSnapshot = pInstance->GetAnimStateSnapshot();
					frame = pInstance->MayaFrame();
					numFrames = pInstance->MayaMaxFrame();
					looping = stateSnapshot.HasLoopingAnim();
					if (pInstance->GetPhase() >= 1.0f)
						lastFrame = true;
				}
			}
		}
	}

	F32 elapsedTimeSec = GetPlayingIgcElapsedTime();
	if (elapsedTimeSec >= m_playingIgcDurationSec && !looping)
	{
		m_playingIgcName = INVALID_STRING_ID_64;
		m_playingIgcAnim = INVALID_STRING_ID_64;
		m_playingIgcDurationSec = m_playingIgcStartTimeSec = 0.0f;
	}
	else if (!(looping && g_ssMgr.m_hideLoopingIgcs))
	{
		F32 durationSec = m_playingIgcDurationSec;
		if (durationSec == NDI_FLT_MAX)
			durationSec = 0.0f; // display zero to mean "infinite looping animation"

		// Limit name length to ensure we have room for the rest.
		char name[80];
		strncpy(name, GetName(), sizeof(name)-1);
		name[sizeof(name)-1] = '\0';

		// Format the string we're going to display.
		char text[256];
		snprintf(text, sizeof(text)-1, g_ssMgr.m_logIgcAnimations2d ? "%s:%.2f/%.2f: %s\n" : "%s:\n%.2f/%.2f: %s",
			name,
			frame,
			numFrames,
			DevKitOnly_StringIdToString(m_playingIgcAnim));
		text[sizeof(text)-1] = '\0';

		if (g_ssMgr.m_logIgcAnimations2d)
		{
			TextPrinterScreenSpace* pPrinter = g_ssMgr.GetIgcTextPrinter();
			pPrinter->SetTextScale(g_msgOptions.m_conScale);
			pPrinter->PrintF(GetIgcLogColor(), text);
			pPrinter->Flush();
		}
		else
		{
			Point pos(GetTranslation());
			IDrawControl* pDrawControl = GetDrawControl();
			if (pDrawControl)
			{
				Sphere bounds = pDrawControl->GetBoundingSphere();
				pos = bounds.GetCenter();
			}

			g_prim.DrawNoHide(DebugString(pos, text, GetIgcLogColor()));
		}
		//we want to stop the igc broadcast if the anim is sitting on the last frame and not looping
		if (!looping && lastFrame)
			return false;
		else
			return true;
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
STATE_REGISTER(NdGameObject, Active, kPriorityNormal);


/// --------------------------------------------------------------------------------------------------------------- ///
// NdGameObject::RigidBodyIterator and NdGameObject::MutableRigidBodyIterator Implementations
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
/*static*/ U32 NdGameObject::RigidBodyIterator::GetCount(const NdGameObject* pGameObject)
{
	const CompositeBody* pComposite = pGameObject->GetCompositeBody();
	if (!pComposite)
		return pGameObject->GetRigidBody() ? 1 : 0;

	U32 cnt = pComposite->GetNumBodies();
	return cnt;
}

/// --------------------------------------------------------------------------------------------------------------- co///
const RigidBody* NdGameObject::RigidBodyIterator::First(const NdGameObject* pGameObject)
{
	m_pGameObject = pGameObject;
	m_rbIndex = 0;
	return Next();
}

/// --------------------------------------------------------------------------------------------------------------- ///
const RigidBody* NdGameObject::RigidBodyIterator::Next()
{
	ASSERT(m_pGameObject);
	const CompositeBody* pComposite = m_pGameObject->GetCompositeBody();
	if (!pComposite)
	{
		if (m_rbIndex == 0)
		{
			m_rbIndex++;
			return m_pGameObject->GetRigidBody();
		}
		else
			return nullptr;
	}

	if (m_rbIndex < pComposite->GetNumBodies())
		return pComposite->GetBody(m_rbIndex++);
	return nullptr;
}

// NdGameObject::MutableRigidBodyIterator Implementations

/// --------------------------------------------------------------------------------------------------------------- ///
/*static*/ U32 NdGameObject::MutableRigidBodyIterator::GetCount(const NdGameObject* pGameObject)
{
	const CompositeBody* pComposite = pGameObject->GetCompositeBody();
	if (!pComposite)
		return pGameObject->GetRigidBody() ? 1 : 0;

	U32 cnt = pComposite->GetNumBodies();
	return cnt;
}

/// --------------------------------------------------------------------------------------------------------------- co///
RigidBody* NdGameObject::MutableRigidBodyIterator::First(NdGameObject* pGameObject)
{
	m_pGameObject = pGameObject;
	m_rbIndex = 0;
	return Next();
}

/// --------------------------------------------------------------------------------------------------------------- ///
RigidBody* NdGameObject::MutableRigidBodyIterator::Next()
{
	ASSERT(m_pGameObject);
	CompositeBody* pComposite = m_pGameObject->GetCompositeBody();
	if (!pComposite)
	{
		if (m_rbIndex == 0)
		{
			m_rbIndex++;
			return m_pGameObject->GetRigidBody();
		}
		else
			return nullptr;
	}

	if (m_rbIndex < pComposite->GetNumBodies())
		return pComposite->GetBody(m_rbIndex++);
	return nullptr;
}
/// --------------------------------------------------------------------------------------------------------------- ///
// End NdGameObject::RigidBodyIterator and NdGameObject::MutableRigidBodyIterator Implementations
/// --------------------------------------------------------------------------------------------------------------- ///

void NdGameObject::InitTextureInstanceTable()
{
	// Already intialized
	if (m_pInstanceTextureTable[0])
	{
		return;
	}

	// reset game object flags
	m_goFlags.m_needsOverlayInstanceTexture = false;
	m_goFlags.m_needsWetmaskInstanceTexture = false;
	m_goFlags.m_needsDamagemaskInstanceTexture = false;
	m_goFlags.m_needsMudmaskInstanceTexture = false;
	m_goFlags.m_needsGoremaskInstanceTexture = false;
	m_goFlags.m_needsDynamicGoremaskInstanceTexture = false;
	m_goFlags.m_needsWoundmaskInstanceTexture = false;
	m_goFlags.m_needsBurnMaskInstanceTexture = false;
	m_goFlags.m_needsSnowMaskInstanceTexture = false;

	if (IDrawControl *pDrawControl = GetDrawControl())
	{
		bool needsInstanceTextureTables = false;

		// loop through and see which instance textures we might need
		for (int i = 0; i < pDrawControl->GetNumMeshes(); ++i)
		{
			IDrawControl::Mesh* pMesh = &pDrawControl->GetMesh(i);

			for (int j = 0; j < pMesh->m_availableLods; ++j)
			{
				IDrawControl::Mesh::Lod* pLod = &pMesh->m_lod[j];

				if (pLod && pLod->m_pInstance)
				{
					FgInstance* pInstance = pLod->m_pInstance;

					if (pInstance->NeedsOverlayTexture())
					{
						m_goFlags.m_needsOverlayInstanceTexture = true;
						needsInstanceTextureTables = true;
					}
					if (pInstance->NeedsWetMaskTexture())
					{
						m_goFlags.m_needsWetmaskInstanceTexture = true;
						needsInstanceTextureTables = true;
					}
					if (pInstance->NeedsMudMaskTexture())
					{
						m_goFlags.m_needsMudmaskInstanceTexture = true;
						needsInstanceTextureTables = true;
					}
					if (pInstance->NeedsDamageMaskTexture())
					{
						m_goFlags.m_needsDamagemaskInstanceTexture = true;
						needsInstanceTextureTables = true;
					}
					if (pInstance->NeedsGoreMaskTexture())
					{
						m_goFlags.m_needsGoremaskInstanceTexture = true;
						needsInstanceTextureTables = true;
					}
					if (pInstance->NeedsWoundMaskTexture())
					{
						m_goFlags.m_needsWoundmaskInstanceTexture = true;
						needsInstanceTextureTables = true;
					}
					if (pInstance->NeedsBurnMaskTexture())
					{
						m_goFlags.m_needsBurnMaskInstanceTexture = true;
						needsInstanceTextureTables = true;
					}
					if (pInstance->NeedsSnowMaskTexture())
					{
						m_goFlags.m_needsSnowMaskInstanceTexture = true;
						needsInstanceTextureTables = true;
					}
					if (pInstance->NeedsDynamicGoreMaskTexture())
					{
						m_goFlags.m_needsDynamicGoremaskInstanceTexture = true;
						needsInstanceTextureTables = true;
					}
					if (pInstance->NeedsSpecularCubemapTexture())
					{
						m_goFlags.m_needsSpecularCubemapTexture = true;
						needsInstanceTextureTables = true;
					}
				}
			}
		}

		if (needsInstanceTextureTables)
		{
			// Allocate however many texture tables we need
			for (int i = 0; i < GetNumInstanceTextureTables(); ++i)
			{
				m_pInstanceTextureTable[i] = InstanceTextureTable::Allocate();
			}

			// Always try to load in the correct default gore texture into the gore mask slots
			if (LoadedTexture* pDefaultTex = g_goreMgr.GetDefaultGoreMask())
			{
				AddInstanceTextureOnAllTextureTables(kGoreMask0Texture, pDefaultTex);
				AddInstanceTextureOnAllTextureTables(kGoreMask1Texture, pDefaultTex);
			}
			else
			{
				AddInstanceTextureOnAllTextureTables(kGoreMask0Texture, ndgi::GetBlackTexture());
				AddInstanceTextureOnAllTextureTables(kGoreMask1Texture, ndgi::GetBlackTexture());
			}

			for (U32 i = 0; i < kMaxInstanceTextureTables; i++)
			{
				I32 wetMaskIndex = GetWetMaskIndex(i);
				if (wetMaskIndex != FxMgr::kInvalid)
				{
					AddInstanceTexture(i, kWetMaskTexture, g_fxMgr.GetWetMaskTexture(wetMaskIndex));
				}
			}

			// Set instance texture tables on all meshes
			for (int i = 0; i < pDrawControl->GetNumMeshes(); ++i)
			{
				IDrawControl::Mesh* pMesh = &pDrawControl->GetMesh(i);
				I32 instanceTextureTableIndex = GetInstanceTextureTableIndexForMesh(i);
				for (int j = 0; j < pMesh->m_availableLods; ++j)
				{
					IDrawControl::Mesh::Lod* pLod = &pMesh->m_lod[j];

					if (pLod && pLod->m_pInstance && pLod->m_pInstance->NeedsInstanceTexture())
					{
						pLod->m_pInstance->SetInstanceTextureTable(m_pInstanceTextureTable[instanceTextureTableIndex]);
					}
				}
			}

			if (m_goFlags.m_needsSpecularCubemapTexture)
			{
				// allocate as fg instance data so it doesn't relocate
				m_pSpecularIblProbeParameters = (SpecularIblProbeParameters*)FgInstance::AllocateInstanceData(sizeof(SpecularIblProbeParameters));
			}
		}
	}
}

void NdGameObject::DestroyTextureInstanceTable()
{
	if (m_goFlags.m_needsSpecularCubemapTexture)
	{
		LoadedTexture* pSpecTexture = GetInstanceTextureTable(0)->GetLoadedTexture(kSpecularCubemapTexture);

		if (pSpecTexture && TRUE_IN_FINAL_BUILD(pSpecTexture != g_postShading.GetDebugRainbowCubemapTexture()))
		{
			EngineComponents::GetTextureMgr()->Unlink(pSpecTexture);
		}

		m_cubemapIndex = -1;
		m_cubemapBgIndex = -1;
		m_cubemapBgNameId = INVALID_STRING_ID_64;

		FgInstance::FreeInstanceData(m_pSpecularIblProbeParameters);
		m_pSpecularIblProbeParameters = nullptr;
	}

	for (int i = 0; i < GetNumInstanceTextureTables(); ++i)
	{
		m_pInstanceTextureTable[i] = nullptr;
	}
}

void NdGameObject::AddInstanceTexture(I32 tableIndex, I32 textureType, ndgi::Texture hTexture)
{
	if (!hTexture.Valid())
	{
		return;
	}

	if (!m_pInstanceTextureTable[tableIndex])
	{
		return;
	}

	m_pInstanceTextureTable[tableIndex]->SetTexture((InstanceTextureId)textureType, hTexture);
}

void NdGameObject::AddInstanceTexture(I32 tableIndex, I32 textureType, LoadedTexture* pTexture)
{
	if (!pTexture)
	{
		return;
	}

	if (!m_pInstanceTextureTable[tableIndex])
	{
		return;
	}

	m_pInstanceTextureTable[tableIndex]->SetTexture((InstanceTextureId)textureType, pTexture);
}

void NdGameObject::AddInstanceTextureOnAllTextureTables(I32 textureType, ndgi::Texture hTexture)
{
	for (I32 tableIndex = 0; tableIndex < GetNumInstanceTextureTables(); ++tableIndex)
	{
		AddInstanceTexture(tableIndex, textureType, hTexture);
	}
}

void NdGameObject::AddInstanceTextureOnAllTextureTables(I32 textureType, LoadedTexture* pTexture)
{
	for (I32 tableIndex = 0; tableIndex < GetNumInstanceTextureTables(); ++tableIndex)
	{
		AddInstanceTexture(tableIndex, textureType, pTexture);
	}
}

InstanceTextureTable* NdGameObject::GetInstanceTextureTable(int tableIndex)
{
	return m_pInstanceTextureTable[tableIndex];
}

void NdGameObject::AddAmbientOccludersToDrawControl(StringId64 occluderListId, StringId64 alternateOccluderListId)
{
	const StringId64 kOccluderTypeId = SID("character-ambient-occluder-array");
	m_resolvedAmbientOccludersId = INVALID_STRING_ID_64;

	// NB: Check the type too, to ensure this isn't a string id hash collision
	const DC::CharacterAmbientOccluderArray* pAmbientOccluderArray = ScriptManager::LookupInModule<DC::CharacterAmbientOccluderArray>(occluderListId, SID("ambient-occluders"), nullptr);
	if (pAmbientOccluderArray && ScriptManager::TypeOf(pAmbientOccluderArray) == kOccluderTypeId)
		m_resolvedAmbientOccludersId = occluderListId;
	else
		pAmbientOccluderArray = nullptr;

	// try the remapping table
	if (!pAmbientOccluderArray)
	{
		const DC::Map* pCharAmbOcclMap = (const DC::Map*)ScriptManager::LookupPointerInModule(SID("*characters-ambient-occluders-map*"), SID("ambient-occluders"), nullptr);
		if (pCharAmbOcclMap)
		{
			int mapIndex = ScriptManager::MapLookupIndexForFirst(pCharAmbOcclMap, occluderListId);
			if (mapIndex != -1)
			{
				DC::CharacterAmbientOccluderMapEntry& entry = *(DC::CharacterAmbientOccluderMapEntry*)pCharAmbOcclMap->m_data[mapIndex].m_ptr;

				pAmbientOccluderArray = ScriptManager::LookupInModule<DC::CharacterAmbientOccluderArray>(entry.m_occluderArray, SID("ambient-occluders"), nullptr);
				if (pAmbientOccluderArray /*&& ScriptManager::TypeOf(pAmbientOccluderArray) == kOccluderTypeId*/) // unsure if the TypeOf will work here so omit it; not really useful anyway
					m_resolvedAmbientOccludersId = SID("*characters-ambient-occluders-map*");
				else
					pAmbientOccluderArray = nullptr;
			}

		}
	}

	// fail over to C++-provided default
	if (!pAmbientOccluderArray)
	{
		StringId64 defaultListId = GetDefaultAmbientOccludersId();
		if (defaultListId != INVALID_STRING_ID_64)
		{
			pAmbientOccluderArray = ScriptManager::LookupInModule<DC::CharacterAmbientOccluderArray>(defaultListId, SID("ambient-occluders"), nullptr);
			if (pAmbientOccluderArray && ScriptManager::TypeOf(pAmbientOccluderArray) == kOccluderTypeId)
				m_resolvedAmbientOccludersId = defaultListId;
			else
				pAmbientOccluderArray = nullptr;
		}
	}

	// if even that fails, try the alternate id (typically based on skeleton id)
	if (!pAmbientOccluderArray && alternateOccluderListId != INVALID_STRING_ID_64 && alternateOccluderListId != occluderListId)
	{
		pAmbientOccluderArray = ScriptManager::LookupInModule<DC::CharacterAmbientOccluderArray>(alternateOccluderListId, SID("ambient-occluders"), nullptr);
		if (pAmbientOccluderArray && ScriptManager::TypeOf(pAmbientOccluderArray) == kOccluderTypeId)
			m_resolvedAmbientOccludersId = alternateOccluderListId;
		else
			pAmbientOccluderArray = nullptr;
	}

	const AttachSystem* pAttachSystem = GetAttachSystem();

	StringId64 infoStringId = occluderListId;

	// capsule occluders
	if (pAttachSystem && pAmbientOccluderArray)
	{
		m_pDrawControl->InitCapsuleAmbientOccluders(this, pAmbientOccluderArray, pAttachSystem);
	}

	// cubemap & volume occluders
	if (m_pDrawControl->MeshesUseAmbientOccluders(infoStringId))
	{
		m_pDrawControl->InitMeshAmbientOccluders(this);
	}

	m_pDrawControl->EnableAmbientOccluders(infoStringId);
}


Locator NdGameObject::GetAmbientOccludersLocator() const
{
	Locator objectLocator = GetLocator();

	return objectLocator;
}

void NdGameObject::UpdateAmbientOccluders()
{
	if (m_pDrawControl->HasAmbientOccluders())
	{
		Locator objectLocator = GetAmbientOccludersLocator();

		const JointCache* jointCache = (m_pAnimData)?&m_pAnimData->m_jointCache:nullptr;

		m_pDrawControl->UpdateAmbientOccluders(this, objectLocator, jointCache);
	}
}

void NdGameObject::SetProbeTreeWeight(StringId64 levelNameId, float targetWeight, float changeSpeed)
{
	if (levelNameId == INVALID_STRING_ID_64 || g_pProbeTreeWeightControlManager == nullptr)
	{
		return;
	}

	ProbeTreeWeightControl* currControl = m_pProbeTreeWeightControl;
	ProbeTreeWeightControl* newControl = nullptr;

	while (currControl)
	{
		if (currControl->m_levelNameId == levelNameId)
		{
			// the control already exists
			newControl = currControl;
			break;
		}

		currControl = currControl->m_nextEntry;
	}

	if (!newControl)
	{
		newControl = g_pProbeTreeWeightControlManager->AllocEntry();
		newControl->m_levelNameId = levelNameId;
		newControl->m_nextEntry = m_pProbeTreeWeightControl;
		newControl->m_weight = 1.0f;
		m_pProbeTreeWeightControl = newControl;
	}

	newControl->m_targetWeight = targetWeight;
	newControl->m_weightChangeSpeed = changeSpeed;
	if (changeSpeed == 0.0f)
	{
		newControl->m_weight = targetWeight;
		ApplyProbeTreeWeights();
	}
}

void NdGameObject::UpdateProbeTreeWeightControls(float deltat)
{
	ProbeTreeWeightControl* prevControl = nullptr;
	ProbeTreeWeightControl* currControl = m_pProbeTreeWeightControl;

	ALWAYS_ASSERT(currControl == nullptr || g_pProbeTreeWeightControlManager != nullptr); // only way for currControl to be non-null is if we have a valid ProbeTreeWeightControlManager

	while (currControl)
	{
		ProbeTreeWeightControl* nextControl = currControl->m_nextEntry;

		float direction = (currControl->m_targetWeight < currControl->m_weight) ? -1.0f: 1.0f;

		currControl->m_weight += deltat * currControl->m_weightChangeSpeed * direction;

		if (currControl->m_weight >= 1.0f) currControl->m_weight = 1.0f;
		if (currControl->m_weight <= 0.0f) currControl->m_weight = 0.0f;

		if ((direction > 0.0f) && (currControl->m_weight >= currControl->m_targetWeight))
		{
			currControl->m_weight = currControl->m_targetWeight;
		}

		if ((direction < 0.0f) && (currControl->m_weight <= currControl->m_targetWeight))
		{
			currControl->m_weight = currControl->m_targetWeight;
		}

		if ((direction > 0.0f) && (currControl->m_weight >= 1.0f))
		{
			// remove control if we're blending in and weight = 0;
			if (prevControl)
			{
				prevControl->m_nextEntry = currControl->m_nextEntry;
			}
			else
			{
				m_pProbeTreeWeightControl = currControl->m_nextEntry;
			}

			g_pProbeTreeWeightControlManager->FreeEntry(currControl);
		}

		prevControl = currControl;
		currControl = nextControl;
	}

	ApplyProbeTreeWeights();
}

void NdGameObject::ApplyProbeTreeWeights()
{
	// set pointers in fgInstances
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl) return;

	for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		IDrawControl::Mesh& mesh = pDrawControl->GetMesh(i);
		for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
		{
			IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];

			if (currentLod.m_pInstance)
			{
				currentLod.m_pInstance->SetProbeTreeWeights(m_pProbeTreeWeightControl);
			}
		}
	}
}

void NdGameObject::FreeProbeTreeWeightControls()
{
	ProbeTreeWeightControl* currControl = m_pProbeTreeWeightControl;

	ALWAYS_ASSERT(currControl == nullptr || g_pProbeTreeWeightControlManager != nullptr); // only way for currControl to be non-null is if we have a valid ProbeTreeWeightControlManager

	while (currControl)
	{
		ProbeTreeWeightControl* nextControl = currControl->m_nextEntry;
		g_pProbeTreeWeightControlManager->FreeEntry(currControl);
		currControl = nextControl;
	}

	m_pProbeTreeWeightControl = nullptr;
}

void NdGameObject::ApplyMaterialSwapMap(const DC::Map* pMaterialSwapMap)
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl) return;

	bool errMessageDisplayed = false;

	for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		IDrawControl::Mesh& mesh = pDrawControl->GetMesh(i);
		for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
		{
			IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];

			if (currentLod.m_pInstance)
			{
				const MaterialTable* pMaterialTable = currentLod.m_pInstance->GetMaterial(0, 0)->GetMaterialTable();

				if (pMaterialTable)
				{
					for (int matIndex = 0; matIndex < pMaterialTable->GetNumMaterials(); matIndex++)
					{
						StringId64 matNameId = pMaterialTable->GetMaterial(matIndex)->GetNameId();
						int mapIndex = ScriptManager::MapLookupIndexForFirst(pMaterialSwapMap, matNameId);

						if (mapIndex != -1)
						{
							DC::MaterialSwapEntry& swapEntry = *(DC::MaterialSwapEntry*)pMaterialSwapMap->m_data[mapIndex].m_ptr;
							currentLod.m_pInstance->ChangeMaterialByName(swapEntry.m_srcMaterial, swapEntry.m_dstMaterial);
						}
					}
				}
			}
		}
	}
}

void NdGameObject::ChangeMaterial(const char* from, const char* to, StringId64 materialTableMeshId/* = INVALID_STRING_ID_64*/, bool fixupOnSwap/* = false*/)
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl) return;

	for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		IDrawControl::Mesh& mesh = pDrawControl->GetMesh(i);
		for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
		{
			IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];

			if (currentLod.m_pInstance)
			{
				currentLod.m_pInstance->ChangeMaterialByName(from, to, materialTableMeshId, fixupOnSwap);
			}
		}
	}
}

void NdGameObject::ChangeMaterial(StringId64 from, StringId64 to, StringId64 materialTableMeshId/* = INVALID_STRING_ID_64*/, bool fixupOnSwap/* = false*/)
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl) return;

	for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		IDrawControl::Mesh& mesh = pDrawControl->GetMesh(i);
		for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
		{
			IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];

			if (currentLod.m_pInstance)
			{
				currentLod.m_pInstance->ChangeMaterialByName(from, to, materialTableMeshId, fixupOnSwap);
			}
		}
	}
}

void NdGameObject::ResetAllMaterials()
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl) return;

	for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		IDrawControl::Mesh& mesh = pDrawControl->GetMesh(i);
		for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
		{
			IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];

			if (currentLod.m_pInstance)
			{
				currentLod.m_pInstance->ResetMaterials();
			}
		}
	}
}

void NdGameObject::ApplyMaterialRemap()
{
	if (IDrawControl* pDrawControl = GetDrawControl())
	{
		if (const MaterialTable* pMatTable = m_pSpawner->GetLevel()->GetMaterialTable())
		{
			pDrawControl->ApplyMaterialRemap(m_numInstanceMaterialRemaps, m_aInstanceMaterialRemaps, pMatTable);
		}
	}
}

void NdGameObject::RemoveMaterialRemap()
{
	if (IDrawControl* pDrawControl = GetDrawControl())
	{
		pDrawControl->RemoveMaterialRemap();
	}
}

void NdGameObject::SetAmbientScale(float scale)
{
	IDrawControl *pDrawControl = GetDrawControl();
	if (!pDrawControl) return;

	for (U32 i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		IDrawControl::Mesh& mesh = pDrawControl->GetMesh(i);
		for (U32 lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
		{
			IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];

			if (currentLod.m_pInstance)
			{
				currentLod.m_pInstance->SetAmbientScale(scale);
			}
		}
	}
}

bool NdGameObject::AddTextureDecal(const Vec2_arg offset, const Vec2_arg scale)
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl) return false;

	bool retVal = false;
	for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		IDrawControl::Mesh& mesh = pDrawControl->GetMesh(i);
		for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
		{
			IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];

			if (currentLod.m_pInstance)
			{
				bool added = currentLod.m_pInstance->AddTextureDecal(offset, scale);
				retVal = retVal || added;
			}
		}
	}

	return retVal;
}


F32 NdGameObject::GetUvDistToTextureDecal(const Vec2_arg hitUv, const Vec2_arg invUvSpaceScale)
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl) return -1.0f;

	for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		IDrawControl::Mesh& mesh = pDrawControl->GetMesh(i);
		for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
		{
			IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];

			if (currentLod.m_pInstance)
			{
				F32 dist = currentLod.m_pInstance->GetUvDistToTextureDecal(hitUv, invUvSpaceScale);
				if (dist >= 0.0f)
				{
					return dist;
				}
			}
		}
	}

	return -1.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Static Helper Functions
/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject* NdGameObject::LookupGameObjectByUniqueId(StringId64 uniqueId)
{
	Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(uniqueId);
	if (!pProc)
		pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserBareId(uniqueId);
	return (pProc) ? FromProcess(pProc) : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MutableNdGameObjectHandle NdGameObject::LookupGameObjectHandleByUniqueId(StringId64 uniqueId)
{
	return MutableNdGameObjectHandle::FromProcessHandle(EngineComponents::GetProcessMgr()->LookupProcessHandleByUserId(uniqueId));
}

/// --------------------------------------------------------------------------------------------------------------- ///
const SplasherSkeletonInfo* NdGameObject::GetSplasherSkeleton() const
{
	return m_pSplasherController ? m_pSplasherController->GetSkel() : nullptr;
}

SplasherSkeletonInfo* NdGameObject::GetSplasherSkeleton()
{
	return m_pSplasherController ? m_pSplasherController->GetSkel() : nullptr;
}
const SplasherSet* NdGameObject::GetSplasherSet() const
{
	return m_pSplasherController ? m_pSplasherController->GetSplasherSet(0) : nullptr;
}

SplasherSet* NdGameObject::GetSplasherSet()
{
	return m_pSplasherController ? m_pSplasherController->GetSplasherSet(0) : nullptr;
}

const SplasherController* NdGameObject::GetSplasherController() const
{
	return m_pSplasherController;
}

SplasherController* NdGameObject::GetSplasherController()
{
	return m_pSplasherController;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::UpdateFeetSplashersInWater(const SplasherSkeletonInfo* pSplasherSkel)
{
	if (!pSplasherSkel)
		return;

	SetFeetSplashersInWater(pSplasherSkel->FeetInWater());
}

void NdGameObject::InitializeSplashers(StringId64 skeletonId, StringId64 setlistId)
{
	if (m_pSplasherController != nullptr || EngineComponents::GetNdGameInfo()->m_isHeadless)
		return;

	if (skeletonId != INVALID_STRING_ID_64)
	{
		m_splasherSkeletonId = skeletonId;
		m_splasherSetlistId = setlistId;

		if (m_splasherSkeletonId != INVALID_STRING_ID_64)
		{
			m_pSplasherController = NDI_NEW SplasherController;
			m_pSplasherController->Init(this, m_splasherSkeletonId, m_splasherSetlistId);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::SplashersEnabled(const SpawnInfo& spawnData)
{
	const bool disableSplashers = spawnData.GetData<bool>(SID("disable-splasher"), false);
	return !disableSplashers;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::CopySplasherState(const NdGameObject* pSource)
{
	ASSERT(m_pSplasherController != nullptr);
	m_pSplasherController->Copy(pSource->GetSplasherController());
}

#if !FINAL_BUILD
	StringId64 g_testEffScriptLambdaId = INVALID_STRING_ID_64; //SID("eff-cabinet-or-safe");
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::TriggeredEffects(const EffectList* pTriggeredEffects)
{
	if (m_goFlags.m_disableEffects)
	{
		return;
	}

	if (GetProcessDeltaTime() <= 0.0)
	{
		return;
	}

	IEffectControl* pEffCtrl = GetEffectControl();

	const U32F numTriggeredEffects = pTriggeredEffects->GetNumEffects();
	for (U32F i = 0; i < numTriggeredEffects; ++i)
	{
		const EffectAnimInfo* pEffectAnimInfo = pTriggeredEffects->Get(i);

		if (!pEffectAnimInfo || !pEffectAnimInfo->m_pEffect)
		{
			continue;
		}

		if (!pEffectAnimInfo->m_topTrackInstance)
		{
			const EffectAnimEntryTag* pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("valid-only-if-top-state"));
			if (pTag && pTag->GetValueAsU32() != 0)
			{
				continue;
			}
		}

		{
			const EffectAnimEntryTag* pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("valid-only-if-layer-is"));
			if (pTag && pTag->GetValueAsStringId() != pEffectAnimInfo->m_layerId)
				continue;
		}

		{
			const EffectAnimEntryTag* pTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("valid-only-if-process-type-is"));
			if (pTag && pTag->GetValueAsStringId() != GetTypeNameId())
				continue;
		}

		if (pEffCtrl)
		{
			StringId64 scriptLambdaId = INVALID_STRING_ID_64;

			if (const EffectAnimEntryTag* pScriptLambdaTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("script")))
			{
				scriptLambdaId = pScriptLambdaTag->GetValueAsStringId();
			}

#if !FINAL_BUILD
			if (g_testEffScriptLambdaId != INVALID_STRING_ID_64)
			{
				scriptLambdaId = g_testEffScriptLambdaId;
			}
#endif

			if (scriptLambdaId != INVALID_STRING_ID_64)
			{
				// call a script which can modify the given EffectAnimInfo (i.e. replace it with a new EFF)
				const NdGameObject* pOtherGo = GetOtherEffectObject(pEffectAnimInfo);
				pEffectAnimInfo = pEffCtrl->HandleScriptEffect(this, pOtherGo, pEffectAnimInfo, scriptLambdaId);
			}
		}

		if (NdSubsystemMgr* pSubSysMgr = GetSubsystemMgr())
		{
			PROFILE(Processes, HandleTriggeredEffect);
			pSubSysMgr->HandleTriggeredEffect(pEffectAnimInfo);
		}

		HandleTriggeredEffect(pEffectAnimInfo);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SetSpawnShaderInstanceParam(NdGameObject* pGo, const ProcessSpawnInfo& spawn, int vecIndex, StringId64 tagId)
{
	if (spawn.HasData(tagId))
		pGo->SetShaderInstanceParam(vecIndex, spawn.GetData<Vec4>(tagId, Vec4(SMath::kZero))); // NB: default value is irrelevant here, thanks to the HasData() check above
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetSpawnShaderInstanceParams(const ProcessSpawnInfo& spawn)
{
	if (!m_spawnShaderInstanceParamsSet)
	{
		SetSpawnShaderInstanceParam(this, spawn, kInstParamUvOffset0, SID("bl-uvOffset0"));
		SetSpawnShaderInstanceParam(this, spawn, kInstParamUvOffset1, SID("bl-uvOffset1"));
		SetSpawnShaderInstanceParam(this, spawn, kInstParamUvOffset2, SID("bl-uvOffset2"));
		SetSpawnShaderInstanceParam(this, spawn, kInstParamUvOffset3, SID("bl-uvOffset3"));

		SetSpawnShaderInstanceParam(this, spawn, kInstParamDiffuseTint0, SID("bl-diffuseTint0"));
		SetSpawnShaderInstanceParam(this, spawn, kInstParamDiffuseTint1, SID("bl-diffuseTint1"));
		SetSpawnShaderInstanceParam(this, spawn, kInstParamDiffuseTint2, SID("bl-diffuseTint2"));
		SetSpawnShaderInstanceParam(this, spawn, kInstParamDiffuseTint3, SID("bl-diffuseTint3"));

		SetSpawnShaderInstanceParam(this, spawn, kInstParamBlendOffset0, SID("bl-blendOffset0"));
		SetSpawnShaderInstanceParam(this, spawn, kInstParamBlendOffset1, SID("bl-blendOffset1"));
		SetSpawnShaderInstanceParam(this, spawn, kInstParamBlendOffset2, SID("bl-blendOffset2"));
		SetSpawnShaderInstanceParam(this, spawn, kInstParamBlendOffset3, SID("bl-blendOffset3"));

		SetSpawnShaderInstanceParam(this, spawn, kInstParamElevation, SID("bl-elevation"));

		m_spawnShaderInstanceParamsSet = true;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::ApplyOverlayInstanceTextures()
{
	if (m_pNetController)
	{
		// Removed, because this was always calling an empty stub function due to a mismatched function signature.
		//m_pNetController->HandleInstanceTextures(*this);
	}
	else
	{
		// What to do in this case?  This shouldn't happen normally but if artists load the level in single player, they will get rainbow texture
	}
}

void NdGameObject::SetFgInstanceFlag(FgInstance::FgInstanceFlags flag)
{
	IDrawControl* drawCtrl = GetDrawControl();
	if (drawCtrl)
	{
		drawCtrl->OrInstanceFlag(flag);
	}
}

void NdGameObject::ClearFgInstanceFlag(FgInstance::FgInstanceFlags flag)
{
	IDrawControl* drawCtrl = GetDrawControl();
	if (drawCtrl)
	{
		drawCtrl->ClearInstanceFlag(flag);
	}
}

bool NdGameObject::IsFgInstanceFlagSet(FgInstance::FgInstanceFlags flag)
{
	IDrawControl* drawCtrl = GetDrawControl();
	if (drawCtrl)
	{
		return (drawCtrl->GetInstanceFlag() & flag);
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::RegisterInListenModeMgrImpl(NdGameObject *pParent, U32 addFlags, float fadeTime)
{
	m_goFlags.m_isVisibleInListenMode = true;
	g_listenModeMgr.AddInstance(this, pParent, addFlags, fadeTime);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::RegisterInListenModeMgr(U32 addFlags, float fadeTime)
{
	RegisterInListenModeMgrImpl(nullptr, addFlags, fadeTime);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::UnregisterFromListenModeMgrImpl(float fadeTime)
{
	m_goFlags.m_isVisibleInListenMode = false;
	g_listenModeMgr.RemoveInstance(this, fadeTime);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::UnregisterFromListenModeMgr(float fadeTime)
{
	UnregisterFromListenModeMgrImpl(fadeTime);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::CanPlayerBeParented() const
{
	if (IsType(SID("MeleeImprovWeapon")))
		return false;

	return !m_goFlags.m_disablePlayerParenting;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetShimmer(StringId64 shimmerSettingsName, bool allowAllocations)
{
	PROFILE(Processes, SetShimmer);
	if (shimmerSettingsName == m_pShimmerSettings.GetId())
	{
		return;
	}

	if (shimmerSettingsName == INVALID_STRING_ID_64)
	{
		m_pShimmerSettings = ScriptPointer<DC::ShimmerSettings>(INVALID_STRING_ID_64);;
		m_updateShimmer = false;
		return;
	}

	m_pShimmerSettings = ScriptPointer<DC::ShimmerSettings>(shimmerSettingsName, SID("weapon-art"));
	if (!m_pShimmerSettings.Valid())
	{
		PROFILE(Processes, SetShimmer_MsgErr);
		MsgErr("Shimmer setting was not found[%s]!!\n", DevKitOnly_StringIdToString(shimmerSettingsName));
		return;
	}

	m_updateShimmer = true;

	if (allowAllocations)
	{
		AllocateShaderInstanceParams();
	}

	FadeShimmerIntensity(1.0f, 1.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetShimmerIntensityInternal(F32 intensity)
{
	if (m_shimmerIntensity > 0.0f && intensity == 0.0f && m_pShimmerSettings.Valid())
	{
		// if we're turning off shimmer, set the variables to 0
		Vec4 zeroVec = Vec4(kZero);

		IDrawControl* pDrawControl = GetDrawControl();
		if (pDrawControl)
		{
			for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
			{
				IDrawControl::Mesh& mesh = pDrawControl->GetMesh(i);
				for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
				{
					IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];
					if (currentLod.m_pInstance && currentLod.m_pInstance->HasShaderInstanceParams())
					{
						currentLod.m_pInstance->SetShaderInstanceParam(kInstParamShimmerIntensity, zeroVec);
					}
				}
			}
		}
	}

	m_shimmerIntensity = intensity;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetShimmerIntensity(F32 intensity)
{
	SetShimmerIntensityInternal(intensity);
	m_shimmerIntensityBlendTime = 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::FadeShimmerIntensity(F32 targetVal, F32 blendTime)
{
	if (blendTime <= 0.0f)
	{
		SetShimmerIntensityInternal(targetVal);
		m_shimmerIntensityBlendTime = 0.0f;
	}
	else
	{
		m_shimmerIntensityBlendTarget = targetVal;
		m_shimmerIntensityBlendTime = blendTime;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::UpdateShimmer()
{
	PROFILE(Processes, UpdateShimmer);

	if (m_shimmerIntensityBlendTime > 0.0f)
	{
		F32 dt = GetProcessDeltaTime();
		F32 newIntensity;
		if (dt >= m_shimmerIntensityBlendTime)
		{
			newIntensity = m_shimmerIntensityBlendTarget;
		}
		else
		{
			newIntensity = m_shimmerIntensity + (m_shimmerIntensityBlendTarget - m_shimmerIntensity) * dt/m_shimmerIntensityBlendTime;
		}
		m_shimmerIntensityBlendTime -= dt;
		SetShimmerIntensityInternal(newIntensity);
	}

	if (!ShouldUpdateShimmer())
		return;

	if (!m_pShimmerSettings.Valid())
		return;

	const DC::ShimmerSettings* pShimmerSettings = &(*m_pShimmerSettings);

	// if object interaction is off, just use *no-shimmer* settings so that it looks like shimmer is off.
	const NdInteractControl* pInteractCtrl = GetInteractControl();
	if (pInteractCtrl && pInteractCtrl->NoShimmer())
	{
		static ScriptPointer<DC::ShimmerSettings> s_noShimmer(SID("*no-shimmer*"), SID("weapon-art"));
		pShimmerSettings = &(*s_noShimmer);
	}

	// @ASYNC-TODO
	// We should be okay to access a process as long as it is in a different bucket than we are...

	float distFadeout = 1.0f;
	BoundFrame playerLoc = BoundFrame(kIdentity);

	if (EngineComponents::GetNdGameInfo()->GetPlayerLocation(&playerLoc))
	{
		const Scalar playerDist = Dist(GetTranslation(), playerLoc.GetTranslationWs());

		if (playerDist < pShimmerSettings->m_distance)
		{
			float fadeRange = Max(pShimmerSettings->m_distance - pShimmerSettings->m_fadeInDistance, 0.01f);
			distFadeout = 1.0f - Max(playerDist - pShimmerSettings->m_fadeInDistance, SCALAR_LC(0.0f)) / fadeRange;
		}
		else
		{
			distFadeout = 0.0f;
		}
	}

	const float difficultyIntensity = pInteractCtrl ? pInteractCtrl->GetDifficultyShimmerIntensity() : 1.f;

	const Vec4 color					= pShimmerSettings->m_color.GetVec4();
	//const float scale					= pShimmerSettings->m_scale;
	const float offset					= pShimmerSettings->m_offset;
	const Vec4 direction				= pShimmerSettings->m_direction.GetVec4();
	const float minLightingIntensity	= g_renderOptions.m_minLightingIntensityForShimmer;

	const Point parentPos				= GetBoundFrame().GetParentSpace().GetTranslation();
	const Point localPos				= GetBoundFrame().GetTranslationPs();

	Vec4 shimmerColor					= Vec4(color.X(), color.Y(), color.Z(), 0.0f);
	shimmerColor						*= m_shimmerIntensity * m_shimmerAlphaMultiplier * distFadeout * difficultyIntensity;

	Vec4 parentPosVec4					= parentPos.GetVec4();
	parentPosVec4.SetW(minLightingIntensity);

	const RenderFrameParams* pFrameParameters = GetRenderFrameParams(GetCurrentFrameNumber() - 1);
	const bool disableShimmering = pFrameParameters && pFrameParameters->m_pRenderSettings && pFrameParameters->m_pRenderSettings->m_misc.m_disableShimmeringInCinematics;

	if (g_renderOptions.m_bTurnOffShimmering || disableShimmering)
	{
		shimmerColor = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
	}

	IDrawControl* pDrawControl = GetDrawControl();
	for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		IDrawControl::Mesh& mesh = pDrawControl->GetMesh(i);
		for (U32F lodIndex = 0; lodIndex < mesh.m_availableLods; ++lodIndex)
		{
			IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lodIndex];
			if (currentLod.m_pInstance && currentLod.m_pInstance->HasShaderInstanceParams())
			{
				// NB: only the R (i.e., x) channel of this vector is actually used; it's really an intensity
				currentLod.m_pInstance->SetShaderInstanceParam(kInstParamShimmerIntensity, shimmerColor);

				// these other params are now provided by the material, not the shader instance params...
				//currentLod.m_pInstance->SetShaderInstanceParam(1, SMath::Vec4(pShimmerSettings->m_size, m_shimmerOffset, scale, offset));
				//currentLod.m_pInstance->SetShaderInstanceParam(2, direction);
				//currentLod.m_pInstance->SetShaderInstanceParam(3, parentPosVec4);
			}
		}
	}

	const float shimmerSpeed = pShimmerSettings->m_speed;

	m_shimmerOffset += shimmerSpeed * GetProcessDeltaTime();
	m_shimmerOffset = fmodf(m_shimmerOffset, 2.0f * PI);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdGameObject::SetupPlatformControl(const SpawnInfo& spawn)
{
	PROFILE(Processes, SetupPlatformControl);

	if (PlatformControl::NeedsPlatformControl(GetSpawner()))
	{
		// turn-off, only push-object in T1 has platform control, and it's dynamic.
		const bool suppressTapError = spawn.GetData<bool>(SID("suppress-tap-register-error"), true);
		const StringId64 bindJointIdOverride = spawn.GetData<StringId64>(SID("platform-joint"), INVALID_STRING_ID_64);
		const Err platRes = InitPlatformControl(false, suppressTapError, bindJointIdOverride);

		if (platRes.Failed())
			return platRes;

		SetIsPlatform(true);
	}

	if (spawn.GetData(SID("IsPlatform"), false))
	{
		SetIsPlatform(true);
	}

	if (IsPlatform())
	{
		if (EngineComponents::GetProcessMgr()->GetProcessBucket(*this) > kProcessBucketPlatform)
		{
			ChangeParentProcess(EngineComponents::GetProcessMgr()->m_pPlatformTree);
		}
	}

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::InitJointWind()
{
	m_hasJointWind = m_pAnimData && m_pAnimData->m_curSkelHandle.ToArtItem() && m_pAnimData->m_curSkelHandle.ToArtItem()->m_pJointWindParams;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::UpdateJointWind()
{
	PROFILE(Processes, UpdateJointWind);

	const Locator alignLoc = GetLocator();
	JointCache* pJointCache = &m_pAnimData->m_jointCache;
	U32F numJoints = pJointCache->GetNumTotalJoints();

	const JointWindParams* pParams = m_pAnimData->m_curSkelHandle.ToArtItem()->m_pJointWindParams;
	ExternalBitArray enabledJoints;
	enabledJoints.InitNoAssign(numJoints, m_pAnimData->m_curSkelHandle.ToArtItem()->m_pJointWindBitArray);

	U32F numWindJoints = enabledJoints.CountSetBits();
	JointWindDataOut* pDataOut = g_jointWindMgr.GetOutputBuffer(m_windMgrIndex);
	JointWindDataIn* pDataIn = g_jointWindMgr.GetInputBuffer(numWindJoints, m_windMgrIndex);
	if (pDataOut || pDataIn)
	{
		U32F iWindJoint = 0;
		for (U64 ii  = enabledJoints.FindFirstSetBit(); ii<numJoints; ii = enabledJoints.FindNextSetBit(ii))
		{
			const JointWindParams& params  = pParams[iWindJoint];

			if (pDataOut)
			{
				ndanim::JointParams jp = pJointCache->GetJointParamsLs(ii);
				Locator wsLoc = pJointCache->GetJointLocatorWs(ii);
				I32F parentJoint = pJointCache->GetParentJoint(ii);
				const Locator& parentLoc = parentJoint >= 0 ? pJointCache->GetJointLocatorWs(parentJoint) : alignLoc;

				// Get wind rotation in local space of the joint
				Quat wsRot = wsLoc.GetRotation();
				Quat offsetRotWs(pDataOut[iWindJoint].m_quat);
				Quat lsWindRot = Conjugate(wsRot) * offsetRotWs * wsRot;

				// Mask the rotation axis
				Vec4 axis;
				F32 angle;
				lsWindRot.GetAxisAndAngle(axis, angle);
				axis *= Vec4(params.m_maskX, params.m_maskY, params.m_maskZ, 0.0f);
				lsWindRot = Quat(axis, angle);

				Quat lsRot = jp.m_quat * lsWindRot;
				wsRot = parentLoc.GetRotation() * lsRot;

				// We don't update the children joints here. Hopefully it's not necessary
				jp.m_quat = lsRot;
				wsLoc.SetRotation(wsRot);
				pJointCache->SetJointParamsLs(ii, jp);
				pJointCache->OverwriteJointLocatorWs(ii, wsLoc);

				//g_prim.Draw(DebugCoordAxes(wsLoc));
			}

			if (pDataIn)
			{
				Transform xfm;
				pJointCache->GetBindPoseJointXform(xfm, ii);
				Point pos = alignLoc.TransformPoint(xfm.GetTranslation());
				pDataIn[iWindJoint].m_pos[0] = (F32)pos.X();
				pDataIn[iWindJoint].m_pos[1] = (F32)pos.Y();
				pDataIn[iWindJoint].m_pos[2] = (F32)pos.Z();
				pDataIn[iWindJoint].m_stiffness = params.m_stiffness;
			}

			iWindJoint++;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetupJointWindInput()
{
	PROFILE(Processes, SetupJointWindInput);

	JointCache* pJointCache = &GetAnimData()->m_jointCache;
	const Locator alignLoc = GetLocator();
	const Vector scale(GetScale());

	U32F numJoints = pJointCache->GetNumAnimatedJoints();

	ExternalBitArray enabledJoints;
	enabledJoints.InitNoAssign(numJoints, m_pAnimData->m_curSkelHandle.ToArtItem()->m_pJointWindBitArray);
	U32 numWindJoints = enabledJoints.CountSetBits();

	JointWindDataIn* pDataIn = g_jointWindMgr.GetInputBuffer(numWindJoints, m_windMgrIndex);
	if (!pDataIn)
		return;

	const JointWindParams* pParams = m_pAnimData->m_curSkelHandle.ToArtItem()->m_pJointWindParams;

	numWindJoints = 0;
	for (U64 ii = enabledJoints.FindFirstSetBit(); ii<pJointCache->GetNumTotalJoints(); ii = enabledJoints.FindNextSetBit(ii))
	{
		Transform xfm;
		pJointCache->GetBindPoseJointXform(xfm, ii);
		Point pos = alignLoc.TransformPoint(kOrigin + scale * (xfm.GetTranslation() - kOrigin));
		pDataIn[numWindJoints].m_pos[0] = (F32)pos.X();
		pDataIn[numWindJoints].m_pos[1] = (F32)pos.Y();
		pDataIn[numWindJoints].m_pos[2] = (F32)pos.Z();
		pDataIn[numWindJoints].m_stiffness = pParams[numWindJoints].m_stiffness;
		numWindJoints++;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::ApplyJointWindOutput(const ndanim::JointParams* pJointLsSource, Locator* pWsLocsOut, ndanim::JointParams* pJointLsOut)
{
	PROFILE(Processes, ApplyJointWindOutput);

	JointWindDataOut* pDataOut = g_jointWindMgr.GetOutputBuffer(m_windMgrIndex);

	JointCache* pJointCache = &GetAnimData()->m_jointCache;
	const Locator alignLoc = GetLocator();
	const Vector scale(GetScale());

	U32F numJoints = pJointCache->GetNumAnimatedJoints();

	ExternalBitArray enabledJoints;
	enabledJoints.InitNoAssign(numJoints, m_pAnimData->m_curSkelHandle.ToArtItem()->m_pJointWindBitArray);

	U32F numWindJoints = 0;
	for (U32F ii = 0; ii<numJoints; ii++)
	{
		ndanim::JointParams jp = pJointLsSource[ii];
		I32F parentJoint = pJointCache->GetParentJoint(ii);
		const Locator& parentLoc = parentJoint >= 0 ? pWsLocsOut[parentJoint] : alignLoc;
		Locator wsLoc = parentLoc.TransformLocator(Locator(kOrigin + scale * (jp.m_trans - kOrigin), jp.m_quat));

		if (enabledJoints.IsBitSet(ii) && pDataOut)
		{
			wsLoc.SetRotation(Quat(pDataOut[numWindJoints].m_quat) * wsLoc.GetRotation());
			jp.m_quat = Conjugate(parentLoc.GetRotation()) * wsLoc.GetRotation();
			numWindJoints++;
		}

		pWsLocsOut[ii] = wsLoc;
		if (pJointLsOut)
			pJointLsOut[ii] = jp;

		//g_prim.Draw(DebugCoordAxes(wsLoc, 0.05f));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::UpdateJointWindNoAnim(const ndanim::JointParams* pJointLsSource)
{
	// This is not exactly equivalent to UpdateJointWind for performance reasons and because each is designed to support different use cases
	JointCache* pJointCache = &GetAnimData()->m_jointCache;
	ApplyJointWindOutput(pJointLsSource, pJointCache->GetJointLocatorsWsForOutput(), const_cast<ndanim::JointParams*>(pJointCache->GetJointParamsLs()));
	SetupJointWindInput();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::UpdateJointShaderDrivers()
{
	if (!m_pJointShaderDriverList.Valid())
		return;

	FgAnimData* pAnimData = GetAnimData();
	if (!pAnimData)
		return;

	const JointCache& jc = pAnimData->m_jointCache;
	const U32F numOutputControls = jc.GetNumOutputControls();

	const DC::JointShaderDriverList* pList = m_pJointShaderDriverList;

	for (U32F i = 0; i < pList->m_count; ++i)
	{
		const DC::JointShaderDriverEntry& entry = pList->m_entries[i];

		I32F foundIndex = -1;
		const char* pName = nullptr;

		for (U32F j = 0; j < numOutputControls; ++j)
		{
			pName = pAnimData->GetOutputControlName(j);
			if (0 == strcmp(entry.m_outputControl.m_string.GetString(), pName))
			{
				foundIndex = j;
				break;
			}
		}

		if (foundIndex < 0)
			continue;

		const float val = jc.GetOutputControl(foundIndex);

		ASSERTF(IsFinite(val), ("[%s] Output control '%s' (%d) has non-finite value (%f)", GetName(), pName, foundIndex, val));
		//MsgCon("%s : %f -> %s [%d]\n", pName, val, entry.m_shaderMatName.m_string.c_str(), entry.m_shaderMatIndex);

		if (IsFinite(val))
		{
			SetTableShaderMaterialParamByIndex(entry.m_shaderMatName.m_symbol, entry.m_shaderMatIndex, val);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
bool NdGameObject::AddLookAtObject(const NdGameObject* pGo)
{
	NdAtomic64Janitor accessLock(&s_lookAtObjectLock);

	bool success = false;

	if (pGo && s_lookAtObjectCount < kMaxLookAtObjects)
	{
#if !FINAL_BUILD
		// Verify that pGo isn't already in the array
		for (U32F ii = 0; ii < s_lookAtObjectCount; ++ii)
		{
			NdGameObjectHandle& hGo = s_lookAtObjects[ii];

			if (hGo.ToProcess() == pGo)
			{
				ASSERT(false);
				return false;
			}
		}
#endif	//!FINAL_BUILD

		s_lookAtObjects[s_lookAtObjectCount++] = pGo;
		success = true;
	}

	if (FALSE_IN_FINAL_BUILD(!success && !g_ndConfig.m_pNetInfo->IsNetActive()))
	{
		MsgConPersistent("Failed to register '%s' as a look-at object (count %d / %d)\n", pGo->GetName(), s_lookAtObjectCount, kMaxLookAtObjects);
	}

	return success;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
bool NdGameObject::RemoveLookAtObject(const NdGameObject* pGo)
{
	NdAtomic64Janitor accessLock(&s_lookAtObjectLock);

	bool success = false;

	if (pGo && s_lookAtObjectCount > 0)
	{
		for (U32F ii = 0; ii < kMaxLookAtObjects; ++ii)
		{
			NdGameObjectHandle& hGo = s_lookAtObjects[ii];

			if (hGo.ToProcess() == pGo)
			{
				if (--s_lookAtObjectCount > ii)
				{
					hGo = s_lookAtObjects[s_lookAtObjectCount];
				}

				s_lookAtObjects[s_lookAtObjectCount] = nullptr;
				success = true;
				break;
			}
		}
	}

	return success;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
U32F NdGameObject::GetLookAtObjectCount()
{
	return s_lookAtObjectCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
NdGameObjectHandle NdGameObject::GetLookAtObject(U32F index)
{
	NdAtomic64Janitor accessLock(&s_lookAtObjectLock);

	return index < s_lookAtObjectCount ? s_lookAtObjects[index] : nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::Show(int screenIndex /*= -1*/, bool secondary /*= false*/)
{
	PROFILE(Processes, Show);

	ShowObject(screenIndex, secondary);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::Hide(int screenIndex /*= -1*/, bool secondary /*= false*/)
{
	PROFILE(Processes, Hide);

	HideObject(screenIndex, secondary);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::ShowObject(int screenIndex /* = -1 */, bool secondary /* = false */)
{
	if (IDrawControl* pDrawControl = GetDrawControl())
	{
		pDrawControl->ShowObject(screenIndex, secondary);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::HideObject(int screenIndex /* = -1 */, bool secondary /* = false */)
{
	if (IDrawControl* pDrawControl = GetDrawControl())
	{
		pDrawControl->HideObject(screenIndex, secondary);

		SendEvent(SID("hide-object"), this);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::HideObjectForOneFrame()
{
	if (IDrawControl* pDrawControl = GetDrawControl())
	{
		pDrawControl->HideObjectForOneFrame();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NdGameObject::CountCustomAttachPoints(const DC::CustomAttachPointSpecArray* pSpecArray) const
{
	if (!pSpecArray)
	{
		return 0;
	}

	U32F count = 0;
	for (U32F i = 0; i < pSpecArray->m_count; ++i)
	{
		const DC::CustomAttachPointSpec& spec = pSpecArray->m_array[i];

		const bool hidden = (spec.m_parentJoint == SID("--hidden--"));
		const I32F iJoint = hidden ? kInvalidJointIndex : FindJointIndex(spec.m_parentJoint);
		if (iJoint == kInvalidJointIndex && !hidden)
			continue;

		++count;
	}

	return count;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::AddCustomAttachPoints(const DC::CustomAttachPointSpecArray* pSpecArray)
{
	ALWAYS_ASSERT(pSpecArray);
	AttachSystem* pAs = GetAttachSystem();

	for (U32F i = 0; i < pSpecArray->m_count; ++i)
	{
		const DC::CustomAttachPointSpec& spec = pSpecArray->m_array[i];
		const bool hidden = (spec.m_parentJoint == SID("--hidden--"));
		const I32F iJoint = hidden ? kInvalidJointIndex : FindJointIndex(spec.m_parentJoint);

		if (iJoint == kInvalidJointIndex && !hidden)
		{
			MsgErr("AttachSystem, couldn't setup attach point %s, joint: %s\n",
				   DevKitOnly_StringIdToString(spec.m_name),
				   DevKitOnly_StringIdToString(spec.m_parentJoint));
			continue;
		}

		const Point posLs = Point(spec.m_x, spec.m_y, spec.m_z);
		const Quat rotLs  = eulerToQuat(Vector(DEGREES_TO_RADIANS(spec.m_degreesX),
											   DEGREES_TO_RADIANS(spec.m_degreesY),
											   DEGREES_TO_RADIANS(spec.m_degreesZ)));
		Locator offsetLs(posLs, rotLs);

		AttachIndex attachIndex;
		if (pAs->FindPointIndexById(&attachIndex, spec.m_name))
		{
			// Override
			pAs->SetJoint(attachIndex, iJoint, hidden);
			pAs->SetPointOffset(attachIndex, offsetLs);
		}
		else
		{
			pAs->AddPointSpec(AttachPointSpec(spec.m_name, iJoint, offsetLs, hidden));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::InitCustomAttachPoints(StringId64 nameId)
{
	AttachSystem* pAs = GetAttachSystem();
	ALWAYS_ASSERT(pAs);
	ALWAYS_ASSERT(pAs->GetAllocCount() == 0); // attach system already has space allocated by someone. This case is not handled.
	if (const DC::CustomAttachPointSpecArray* pCustomPointsArray = ScriptManager::Lookup<DC::CustomAttachPointSpecArray>(nameId))
	{
		const U32F customPointsCount = CountCustomAttachPoints(pCustomPointsArray);
		pAs->Init(customPointsCount);
		AddCustomAttachPoints(pCustomPointsArray);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::PhotoModeSetHidden(bool hidden)
{
	if (m_pDrawControl)
	{
		g_renderOptions.m_hiddenCharacterTweaking = false;

		if (hidden && !m_pDrawControl->IsObjectHidden())
		{
			m_pDrawControl->HideObject();
			m_hiddenByPhotoMode = true;
			g_renderOptions.m_hiddenCharacterTweaking = true;
		}
		else if (!hidden && m_hiddenByPhotoMode)
		{
			m_pDrawControl->ShowObject();
			m_hiddenByPhotoMode = false;
			g_renderOptions.m_hiddenCharacterTweaking = true;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point NdGameObject::GetGestureOriginOs(StringId64 gestureId, StringId64 typeId) const
{
	const Locator& locPs = GetLocatorPs();
	Point posPs = GetTranslationPs();

	const DC::GestureDef* pGestureDef = Gesture::LookupGesture(gestureId);
	if (pGestureDef && pGestureDef->m_throwLaunchPhase >= 0.0f)
	{
		// hack for throwing gesture, it needs be deterministic
		// we need make sure GestureOriginOs return the same value when preparing throw and throwing!
		posPs += Vector(0.0f, 1.2f, 0.0f);
	}
	else
	{
		switch (typeId.GetValue())
		{
		case SID_VAL("look"):
		case SID_VAL("look-animated"):
			posPs = GetLookOriginPs();
			break;

		case SID_VAL("aim"):
			posPs = GetAimOriginPs();
			break;

		default:
			{
				const AttachSystem* pAs = GetAttachSystem();

				const AttachIndex index = pAs ? pAs->FindPointIndexById(SID("targChest")) : AttachIndex::kInvalid;
				if (index != AttachIndex::kInvalid)
				{
					const Point posWs = pAs->GetLocator(index).Pos();
					posPs = GetParentSpace().UntransformPoint(posWs);
				}
			}
			break;
		}
	}

	const Point posOs = locPs.UntransformPoint(posPs);

	return posOs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetMeshRayResult(const MeshProbe::CallbackObject* pObject,
									const MeshProbe::Probe* pRequest,
									const Binding& binding,
									MeshRaycastResult* pResult)
{
	// make sure it's the latest result.
	if (pRequest->m_frameNumber > pResult->m_contactFrameNumber)
	{
		if (pObject)
		{
			pResult->m_valid = true;

			const MeshProbe::ProbeResult& rayResult = pObject->m_probeResults[0];
			const Point posWs = rayResult.m_contactWs;
			const Vector normWs = rayResult.m_normalWs;

			Vector up = SafeNormalize(Cross(normWs, kUnitXAxis), kUnitZAxis);
			pResult->m_contact = BoundFrame(Locator(posWs, Normalize(QuatFromLookAt(normWs, up))), binding);
			pResult->m_t = rayResult.m_tt;

			pResult->m_positionWs = rayResult.m_contactWs;
			pResult->m_normalWs = rayResult.m_normalWs;
			pResult->m_vertexWs0 = rayResult.m_vertexWs0;
			pResult->m_vertexWs1 = rayResult.m_vertexWs1;
			pResult->m_vertexWs2 = rayResult.m_vertexWs2;
		}
		else
		{
			pResult->m_valid = false;
		}

		pResult->m_contactFrameNumber = pRequest->m_frameNumber;
		pResult->m_rayStart = pRequest->m_rayStart;
		pResult->m_rayEnd = pRequest->m_rayEnd;
		pResult->m_radius = pRequest->m_rayRadius;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void GroundMeshRaycastCallback(const MeshProbe::CallbackObject* pObject, MeshRayCastJob::CallbackContext* pInContext, const MeshProbe::Probe& probeReq)
{
	NdGameObject::MeshRaycastContext* pContext = (NdGameObject::MeshRaycastContext*)pInContext;

	NdGameObject* pGameObject = pContext->m_hOwner.ToMutableProcess();
	if (!pGameObject)
		return;

	NdGameObject::MeshRaycastResult* pResult = pGameObject->GetGroundMeshRaycastResult(pContext->m_type);
	if (!pResult)
		return;

	NdGameObject::SetMeshRayResult(pObject, &probeReq, pGameObject->GetBinding(), pResult);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::OnSurfaceTypeReady(const MeshProbe::FullCallbackObject* pObject, MeshRaycastResult* pResult, const MeshProbe::Probe* pRequest)
{
	if (!pObject)
		return;

	const I64 surfaceFrameNumber = pRequest->m_frameNumber;
	if (surfaceFrameNumber > pResult->m_surfaceFrameNumber)
	{
		const MeshProbe::SurfaceTypeResult& surfaceResult0 = pObject->m_surfaceResults[0];
		const MeshProbe::SurfaceTypeResult& surfaceResult1 = pObject->m_surfaceResults[1];

		bool valid0 = surfaceResult0.m_surfaceType.Valid();
		bool valid1 = surfaceResult1.m_surfaceType.Valid();

		// We have made the conscious decision not to invalidate the surface type if we didnt hit a valid one
		// Sound designers have the tools they need to find bad surfaces, we dont have to default to quiet footsteps for untagged surfaces.
		if (valid0 || valid1)
		{
			StringId64 lastBaseLayerSurfaceType = pResult->m_backupBaseLayerSt;
			if (lastBaseLayerSurfaceType == INVALID_STRING_ID_64)
				lastBaseLayerSurfaceType = IEffectControl::ExtractBaseLayerSurfaceType(pResult->m_surfaceTypeInfo);

			pResult->m_surfaceTypeInfo.Clear();

			if (valid0 && valid1)
			{
				// surfaceResult0 might be untagged if it's geometry decal and transparent,	then surfaceResult1 has the correct ground surface-type.
				const bool isDecal0 = pObject->m_probeResults[0].m_isDecal;
				const bool isWater0 = pObject->m_probeResults[0].m_isWater;

				bool additive0 = false;
				bool hasSolid0 = false;
				{
					bool primaryIsSolid;
					{
						const DC::SurfaceDesc* pSurfaceDesc = GetDcSurfaceDesc(surfaceResult0.m_surfaceType.m_primary[0]);
						primaryIsSolid = (!pSurfaceDesc || !pSurfaceDesc->m_soundAdditiveCurve) && !isWater0;
					}

					bool secondaryIsSolid = false;
					if (surfaceResult0.m_surfaceType.m_primary[1] != INVALID_STRING_ID_64)
					{
						const DC::SurfaceDesc* pSurfaceDesc = GetDcSurfaceDesc(surfaceResult0.m_surfaceType.m_primary[1]);
						secondaryIsSolid = (!pSurfaceDesc || !pSurfaceDesc->m_soundAdditiveCurve) && !isWater0;
					}

					additive0 = isDecal0 || !primaryIsSolid;
					hasSolid0 = primaryIsSolid || secondaryIsSolid;
				}

				const bool isDecal1 = pObject->m_probeResults[1].m_isDecal;
				const bool isWater1 = pObject->m_probeResults[1].m_isWater;

				if (surfaceResult0.m_surfaceType.m_primary[0] == INVALID_STRING_ID_64 && isDecal0)
				{
					// surfaceResult0 is untagged, use surfaceResult1.
					pResult->m_surfaceTypeInfo.m_layers[0] = surfaceResult1;
					pResult->m_surfaceTypeInfo.m_isDecal[0] = isDecal1;
					pResult->m_surfaceTypeInfo.m_isWater[0] = isWater1;
				}
				else if ((additive0 || isWater0) && !hasSolid0) // if layer0 has any solid surface, discard layer1.
				{
					pResult->m_surfaceTypeInfo.m_layers[0] = surfaceResult0;
					pResult->m_surfaceTypeInfo.m_layers[1] = surfaceResult1;
					pResult->m_surfaceTypeInfo.m_isDecal[0] = isDecal0;
					pResult->m_surfaceTypeInfo.m_isDecal[1] = isDecal1;
					pResult->m_surfaceTypeInfo.m_isWater[0] = isWater0;
					pResult->m_surfaceTypeInfo.m_isWater[1] = isWater1;
				}
				else
				{
					pResult->m_surfaceTypeInfo.m_layers[0] = surfaceResult0;
					pResult->m_surfaceTypeInfo.m_isDecal[0] = isDecal0;
					pResult->m_surfaceTypeInfo.m_isWater[0] = isWater0;
				}
			}
			else if (valid0)
			{
				const bool isDecal0 = pObject->m_probeResults[0].m_isDecal;
				const bool isWater0 = pObject->m_probeResults[0].m_isWater;

				pResult->m_surfaceTypeInfo.m_layers[0] = surfaceResult0;
				pResult->m_surfaceTypeInfo.m_isDecal[0] = isDecal0;
				pResult->m_surfaceTypeInfo.m_isWater[0] = isWater0;
			}
			// surfaceResult0 might be untagged if it's geometry decal and transparent,	then surfaceResult1 has the correct ground surface-type.
			else if (valid1 && pObject->m_probeResults[0].m_isDecal)
			{
				const bool isDecal1 = pObject->m_probeResults[1].m_isDecal;
				const bool isWater1 = pObject->m_probeResults[1].m_isWater;

				pResult->m_surfaceTypeInfo.m_layers[0] = surfaceResult1;
				pResult->m_surfaceTypeInfo.m_isDecal[0] = isDecal1;
				pResult->m_surfaceTypeInfo.m_isWater[0] = isWater1;
			}

			StringId64 backupBaseLayerSt = lastBaseLayerSurfaceType;
			{
				StringId64 newBaseLayerSt = IEffectControl::ExtractBaseLayerSurfaceType(pResult->m_surfaceTypeInfo);
				if (newBaseLayerSt != INVALID_STRING_ID_64)
				{
					backupBaseLayerSt = newBaseLayerSt;
				}
			}
			pResult->m_backupBaseLayerSt = backupBaseLayerSt;
		}
		else if (g_ndConfig.m_pNetInfo->IsNetActive())
		{
			// we don't have audio designers right now, so our conscious decision is to default to stone-asphalt for untagged surfaces
			pResult->m_surfaceTypeInfo.m_layers[0].m_surfaceType = MeshProbe::SurfaceType(SID("stone-asphalt"));
			pResult->m_surfaceTypeInfo.m_layers[1].Clear();
		}

		pResult->m_surfaceFrameNumber = surfaceFrameNumber;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void GroundMeshRaycastSurfaceTypeCallback(const MeshProbe::FullCallbackObject* pObject, MeshRayCastJob::CallbackContext* pInContext, const MeshProbe::Probe& probeReq)
{
	NdGameObject::MeshRaycastContext* pContext = (NdGameObject::MeshRaycastContext*)pInContext;

	NdGameObject* pGameObject = pContext->m_hOwner.ToMutableProcess();
	if (!pGameObject)
		return;

	NdGameObject::MeshRaycastResult* pResult = pGameObject->GetGroundMeshRaycastResult(pContext->m_type);
	if (!pResult)
		return;

	NdGameObject::OnSurfaceTypeReady(pObject, pResult, &probeReq);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DebugDrawMeshRaycastResult(const MeshRaycastResult* pResult)
{
	if (!pResult)
		return;

	char text[128];
	int length = snprintf(text, sizeof(text) - 1, "snd");
	if (pResult->m_surfaceTypeInfo.m_layers[0].m_surfaceType.Valid())
	{
		char helper[256];
		pResult->m_surfaceTypeInfo.m_layers[0].m_surfaceType.ToString(helper, sizeof(helper) - 1);
		length += snprintf(text + length, sizeof(text) - 1 - length, ", l0:%s", helper);
	}
	if (pResult->m_surfaceTypeInfo.m_layers[1].m_surfaceType.Valid())
	{
		char helper[256];
		pResult->m_surfaceTypeInfo.m_layers[1].m_surfaceType.ToString(helper, sizeof(helper) - 1);
		length += snprintf(text + length, sizeof(text) - 1 - length, ", l1:%s", helper);
	}
	length += snprintf(text + length, sizeof(text) - 1 - length, ":%lld:%lld", pResult->m_contactFrameNumber, pResult->m_surfaceFrameNumber);

	g_prim.Draw(DebugString(pResult->m_positionWs, text, kColorWhite, 0.5f));

	DebugPrimTime debugDrawTime = DebugPrimTime(kPrimDuration1FramePauseable);
	g_prim.Draw(DebugLine(pResult->m_rayStart,
		pResult->m_rayEnd,
		kColorCyan,
		kColorCyan,
		2.0f,
		PrimAttrib(kPrimEnableHiddenLineAlpha)),
		debugDrawTime);

	if (pResult->m_valid)
	{
		g_prim.Draw(DebugCross(pResult->m_positionWs, 0.1f, 4.0f, kColorRed, kPrimEnableHiddenLineAlpha),
			debugDrawTime);
		g_prim.Draw(DebugLine(pResult->m_positionWs,
			pResult->m_normalWs * 0.2f,
			kColorGreen,
			kColorGreen,
			4.0f,
			kPrimEnableHiddenLineAlpha),
			debugDrawTime);

		g_prim.Draw(DebugLine(pResult->m_vertexWs0,
			pResult->m_vertexWs1,
			kColorBlue,
			1.0f,
			kPrimEnableHiddenLineAlpha),
			debugDrawTime);
		g_prim.Draw(DebugLine(pResult->m_vertexWs1,
			pResult->m_vertexWs2,
			kColorBlue,
			1.0f,
			kPrimEnableHiddenLineAlpha),
			debugDrawTime);
		g_prim.Draw(DebugLine(pResult->m_vertexWs2,
			pResult->m_vertexWs0,
			kColorBlue,
			1.0f,
			kPrimEnableHiddenLineAlpha),
			debugDrawTime);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point NdGameObject::GetCenterOfHeels() const
{
	// this function can help if align is blended in. like carry big objects, swimming
	// use the center of two heels as the 'fake' align
	const JointCache* pJointCache = GetAnimControl()->GetJointCache();
	I32 lHeel = FindJointIndex(SID("l_heel"));
	if (lHeel < 0)
		return GetTranslation();

	I32 rHeel = FindJointIndex(SID("r_heel"));
	if (rHeel < 0)
		return GetTranslation();

	const Locator lHeelLocWs = pJointCache->GetJointLocatorWs(lHeel);
	const Locator rHeelLocWs = pJointCache->GetJointLocatorWs(rHeel);

	const Point lHeelPosWs = lHeelLocWs.GetTranslation();
	const Point rHeelPosWs = rHeelLocWs.GetTranslation();

	const Point middlePosWs = Lerp(lHeelPosWs, rHeelPosWs, 0.5f);
	return middlePosWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetIgnoredByNpcs(bool ignore)
{
	m_ignoredByNpcs = ignore;
}

void NdGameObject::SetIgnoredByNpcsForOneFrame()
{
	m_lastIgnoredByNpcsTime = GetClock()->GetCurTime();
}

bool NdGameObject::IgnoredByNpcs() const
{
	if (m_ignoredByNpcs)
	{
		return true;
	}
	if (m_lastIgnoredByNpcsTime != kTimeFrameNegInfinity) // don't do this test if the time has never been set
	{
		const TimeFrame curTime = GetClock()->GetCurTime();
		const TimeFrame timeSinceIgnored = curTime - m_lastIgnoredByNpcsTime;
		if (timeSinceIgnored < Frames(2))
			return true;
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::KickGroundSurfaceTypeProbe(MeshRaycastType type)
{
	PROFILE(AI, NdGameObject_SetupGroundSurfaceTypeProbe);

	// we only need kick surface type probe once per frame.
	I64 currFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	ASSERT(currFN != m_groundSurfaceTypeKickedFN[type]);
	m_groundSurfaceTypeKickedFN[type] = currFN;

	// this is basically the max step-up height for NPCs.  It should be less than 1m to keep them popping up onto low covers
	const Scalar kMaxDeltaHeight = GetGroundMeshRayHalfLength(type);
	const float deltaTime = GetProcessDeltaTime() == 0.0f ? 1.0f / 30.0f : GetProcessDeltaTime();

	// 	const Vector velPs = GetVelocityPs();  // velocity relative to parent (in parent space)
	// 	const Vector velOffsetWs = parentSpace.TransformVector(velPs) * deltaTime;

	ALWAYS_ASSERTF(GetGroundMeshRaycastResult(type), ("You forgot to implement GetGroundMeshRaycastResult()!"));

	const Locator parentSpace = GetParentSpace();
	Vector velocityFlatWs = parentSpace.TransformVector(GetVelocityPs());
	velocityFlatWs.SetY(0.0f);

	const Point centerOfHeel = GetGroundMeshRayPos(type) + velocityFlatWs * deltaTime;
	// probing "down" in parent space (minus Y axis in parent space)
	const Vector rayDirWs = GetGroundMeshRayDir(type);

	const Point probeStart = centerOfHeel - kMaxDeltaHeight * rayDirWs;
	const Point probeEnd = centerOfHeel + rayDirWs * 1.25f; // magic number

	NdGameObject::MeshRaycastContext context;
	STATIC_ASSERT(sizeof(NdGameObject::MeshRaycastContext) <= sizeof(MeshRayCastJob::CallbackContext));

	const float probeRadius = type == kMeshRaycastGround ? 0.075f : 0.0f;

	ASSERT(type < kMeshRaycastCount);
	context.m_type = type;
	context.m_hOwner = this;
	context.m_debugDrawVoidSurface = false;

	MeshRayCastJob::HitFilter filter;
	if (type == kMeshRaycastWater)
		filter = MeshRayCastJob::HitFilter(MeshRayCastJob::HitFilter::kHitWater, GetProcessId());
	else
		filter = MeshRayCastJob::HitFilter(MeshRayCastJob::HitFilter::kHitSoundEffect, GetProcessId());

	U32 behaviorFlags = MeshRayCastJob::kEveryFrame | MeshRayCastJob::kReturnSurfaceType;

	if (IsKindOf(SID("Character")) && type == NdGameObject::kMeshRaycastGround)
		behaviorFlags |= MeshRayCastJob::kAllHitsReturnSurfaceType | MeshRayCastJob::kSurfaceTypeMultiSamples;

	MeshSphereCastJob sphereJob;
	sphereJob.SetProbeExtent(probeStart, probeEnd);
	sphereJob.SetProbeRadius(probeRadius);
	sphereJob.SetHitFilter(filter);
	sphereJob.SetBehaviorFlags(behaviorFlags);
	sphereJob.SetPriority(MeshRayCastJob::kPriorityLow);
	sphereJob.m_pCallback = GroundMeshRaycastCallback;
	sphereJob.m_pSurfaceTypeCallback = GroundMeshRaycastSurfaceTypeCallback;
	sphereJob.m_pCallbackContext = (MeshRayCastJob::CallbackContext*)&context;

	sphereJob.Kick(FILE_LINE_FUNC);
}

///-----------------------------------------------------------------------------------------------------///
Vector NdGameObject::GetGroundWaterFlow()
{
	MeshRaycastResult* pResult = GetGroundMeshRaycastResult(kMeshRaycastWater);
	if (pResult)
	{
		if (pResult->m_surfaceTypeInfo.m_layers[0].m_flowVecValid)
			return Vector(pResult->m_surfaceTypeInfo.m_layers[0].m_flowVec);
	}

	return kZero;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::DebugDrawFootSurfaceType() const
{
	const NdGameObject::MeshRaycastResult* pResult = NdGameObject::GetGroundMeshRaycastResult(NdGameObject::kMeshRaycastGround);
	NdGameObject::DebugDrawMeshRaycastResult(pResult);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// wheel mesh raycaster
/// --------------------------------------------------------------------------------------------------------------- ///
static void WheelMeshRaycastCallback(const MeshProbe::CallbackObject* pObject, MeshRayCastJob::CallbackContext* pInContext, const MeshProbe::Probe& probeReq)
{
	NdGameObject::WheelMeshRaycastContext* pContext = (NdGameObject::WheelMeshRaycastContext*)pInContext;

	NdGameObject* pGameObject = NdGameObject::FromProcess(pContext->m_hOwner.ToMutableProcess());
	if (!pGameObject)
		return;

	NdGameObject::WheelMeshRaycastResult* pResult = pGameObject->GetWheelMeshRaycastResult(pContext->m_wheelIndex);
	if (!pResult)
		return;

	// make sure the latest result.
	if (pContext->m_frameNumber > pResult->m_frameNumber)
	{
		if (pObject)
		{
			pResult->m_valid = true;

			const Point posWs = pObject->m_probeResults[0].m_contactWs;
			const Vector normWs = pObject->m_probeResults[0].m_normalWs;

			Vector up = SafeNormalize(Cross(normWs, kUnitXAxis), kUnitZAxis);
			pResult->m_mrcContactPoint = posWs;
			pResult->m_mrcContactNormal = normWs;
			pResult->m_frameNumber = pContext->m_frameNumber;
		}
		else
		{
			pResult->m_valid = false;
			pResult->m_frameNumber = pContext->m_frameNumber;
			pResult->m_mrcSurfaceValid = false;
			pResult->m_mrcSurfaceFrameNumber = pContext->m_frameNumber;
		}
	}
}

static void WheelMeshRaycastSurfaceTypeCallback(const MeshProbe::FullCallbackObject* pObject, MeshRayCastJob::CallbackContext* pInContext, const MeshProbe::Probe& probeReq)
{
	NdGameObject::WheelMeshRaycastContext* pContext = (NdGameObject::WheelMeshRaycastContext*)pInContext;

	NdGameObject* pGameObject = NdGameObject::FromProcess(pContext->m_hOwner.ToMutableProcess());
	if (!pGameObject)
		return;

	NdGameObject::WheelMeshRaycastResult* pResult = pGameObject->GetWheelMeshRaycastResult(pContext->m_wheelIndex);
	if (!pResult)
		return;

	// make sure the latest result.
	if (probeReq.m_frameNumber > pResult->m_mrcSurfaceFrameNumber)
	{
		if (pObject)
		{
			pResult->m_mrcSurfaceValid = true;
			pResult->m_mrcSurfaceFrameNumber = probeReq.m_frameNumber;
			pResult->m_mrcSurfaceLastValidFrameNumber = probeReq.m_frameNumber;
			pResult->m_mrcSurfaceInfo = pObject->m_surfaceResults[0];
			pResult->m_mrcVertexColor0 = Color(Vec3(pObject->m_probeResults[0].m_color0));
			pResult->m_mrcVertexColor1 = Color(Vec3(pObject->m_probeResults[0].m_color1));
		}
	}
}

void NdGameObject::KickWheelMeshRaycast(I32 wheelIndex, Point_arg start, Point_arg end)
{
	ALWAYS_ASSERTF(GetWheelMeshRaycastResult(wheelIndex) != nullptr, ("you forget to implement GetWheelMeshRaycastResult!!!"));

	MeshRayCastJob::CallbackContext context;
	ALWAYS_ASSERT(sizeof(WheelMeshRaycastContext) <= sizeof(MeshRayCastJob::CallbackContext));
	WheelMeshRaycastContext* pContext = (WheelMeshRaycastContext*)&context;
	pContext->m_hOwner = this;
	pContext->m_frameNumber = GetCurrentFrameNumber();
	pContext->m_wheelIndex = wheelIndex;

	MeshRayCastJob rayJob;
	rayJob.SetProbeExtent(start, end);
	rayJob.SetHitFilter(MeshRayCastJob::HitFilter(MeshRayCastJob::HitFilter::kHitIk, GetProcessId()));
	rayJob.SetBehaviorFlags(MeshRayCastJob::kEveryFrame);
	rayJob.SetPriority(MeshRayCastJob::kPriorityLow);
	rayJob.m_pCallback = ::WheelMeshRaycastCallback;
	rayJob.m_pSurfaceTypeCallback = ::WheelMeshRaycastSurfaceTypeCallback;
	rayJob.m_pCallbackContext = &context;

	rayJob.Kick(FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimOverlaySnapshot* NdGameObject::GetAnimOverlaysForWeapon(NdGameObjectHandle hWeapon) const
{
	const AnimControl* pAnimControl = GetAnimControl();
	const AnimOverlays* pAnimOverlays = pAnimControl ? pAnimControl->GetAnimOverlays() : nullptr;
	const AnimOverlaySnapshot* pAnimOverlaySnapshot = pAnimOverlays ? pAnimOverlays->GetSnapshot() : nullptr;
	return pAnimOverlaySnapshot;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void cmdDumpAnimationPakFileNamesCallback(int argc, char** argv)
{
	STRIP_IN_FINAL_BUILD;

	const char* outputFile = "/host/y:/.tmp-anim-pak-file-names-dump.txt";

	FILE* pStream = fopen(outputFile, "w");
	ASSERTF(pStream, ("Unable to open output file '%s'", outputFile));

	if (pStream)
	{
		AnimCollection collectedAnims;

		const NdGameObject* pSelectedObject = nullptr;
		if (!pSelectedObject)
		{
			if (const NdActorViewer* pActorViewer = g_hActorViewer.ToProcess())
			{
				pSelectedObject = pActorViewer->GetActorWithFocus();
			}
		}
		if (!pSelectedObject)
		{
			pSelectedObject = DebugSelection::Get().GetSelectedGameObject();
		}

		if (pSelectedObject)
		{
			const AnimControl* pAnimControl = pSelectedObject->GetAnimControl();
			if (pAnimControl)
			{
				const U32 numLayers = pAnimControl->GetNumLayers();
				for (U32 i = 0; i < numLayers; ++i)
				{
					const AnimLayer* pLayer = pAnimControl->GetLayerByIndex(i);
					if (pLayer)
					{
						pLayer->CollectContributingAnims(&collectedAnims);
					}
				}
			}
		}

		for (U32 i = 0; i < collectedAnims.m_animCount; ++i)
		{
			const ArtItemAnim* pAnim = collectedAnims.m_animArray[i];
			const char* pakFileName = pAnim->m_pDebugOnlyPakName;
			if (pakFileName)
			{
				fprintf(pStream, "%s\n", pakFileName);
			}
		}

		fclose(pStream);
	}
}

static Command cmdDumpAnimationPakFileNames("dumpAnimationPakFileNames", &cmdDumpAnimationPakFileNamesCallback);

/// --------------------------------------------------------------------------------------------------------------- ///
void GetBindingString(const RigidBody* pBody, const StringId64 bindSpawnerId, char* text, const U32 textSize)
{
	ASSERT(textSize > 0);

	if (pBody)
	{
		sprintf(text, "binding: unknown");

		const NdGameObject* pObj = pBody->GetOwner();

		const char* strObjName = "<dead-object>";
		if (pObj)
		{
			strObjName = pObj->GetName();
		}

		const RigidBody::GameLinkage linkage = pBody->GetGameLinkage();

		if (linkage == RigidBody::kLinkedToObject)
		{
			snprintf(text, textSize, "binding: object %s / %s", strObjName, DevKitOnly_StringIdToString(bindSpawnerId));
		}
		else if (linkage == RigidBody::kLinkedToJoint)
		{
			snprintf(text,
					 textSize,
					 "binding: object %s joint %s",
					 strObjName,
					 DevKitOnly_StringIdToString(pBody->GetAssociatedJointName()));
		}
	}
	else
	{
		sprintf(text, "binding: world / %s", DevKitOnly_StringIdToString(bindSpawnerId));
	}

	text[textSize - 1] = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::ForceLodIndex(I32 lodIndex)
{
	IDrawControl* pDrawControl = GetDrawControl();
	for (U32F i = 0; i < pDrawControl->GetNumMeshes(); ++i)
	{
		IDrawControl::Mesh& mesh = pDrawControl->GetMesh(i);
		for (U32F lod = 0; lod < mesh.m_availableLods; ++lod)
		{
			IDrawControl::Mesh::Lod& currentLod = mesh.m_lod[lod];
			if (currentLod.m_pInstance)
			{
				if (lodIndex == -1)
				{
					currentLod.m_pInstance->SetForceLod(kNotForced);
				}
				else
				{
					currentLod.m_pInstance->SetForceLod(currentLod.m_pInstance->GetLodIndex() == lodIndex ? kForcedOn
																										  : kForcedOff);
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObjectHandle NdGameObject::GetCameraParent() const
{
	if (m_hCameraParent.HandleValid())
		return m_hCameraParent;

	const NdGameObject* pParentGo = GetBoundGameObject();
	if (pParentGo != nullptr)
	{
		if (pParentGo->m_goFlags.m_enableCameraParenting)
			return pParentGo;
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float NdGameObject::GetDistBlendCameraDist(const NdGameObject* pFocusObj) const
{
	if (!pFocusObj)
		return 0.f;

	Point distCameraPos;
	const NdInteractControl* const pCtrl = GetInteractControl();
	if (pCtrl != nullptr)
	{
		distCameraPos = pCtrl->GetSelectionTargetWs(pFocusObj);
	}
	else
	{
		distCameraPos = GetTranslation();
	}

	return DistXz(distCameraPos, pFocusObj->GetTranslation()); // use 2d distance to be consistent with InteractControl::IsInCameraRemapRange
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetHighContrastMode(StringId64 hcmMode)
{
	DC::HCMModeType type = FromStringId64ToHCMModeType(hcmMode);
	SetHighContrastMode(type);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetHighContrastMode(DC::HCMModeType hcmModeType)
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (pDrawControl != nullptr)
	{
		pDrawControl->SetHighContrastModeType(hcmModeType, GetHighContrastModeMeshExcludeFilter());
		NotifyHighContrastModeOverriden();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::HCMModeType NdGameObject::GetHighContrastMode(bool ignoreInherited) const
{
	IDrawControl* pDrawControl = GetDrawControl();
	if (!pDrawControl)
		return DC::kHCMModeTypeNone;

	const DC::HCMModeType hcmt = ignoreInherited ? pDrawControl->GetRawHighContrastModeType() : pDrawControl->GetHighContrastModeType();
	return hcmt;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::SetHighContrastModeMeshExcludeFilter(StringId64 meshExcludeFilterId)
{
	ScriptPointer<const DC::SymbolArray> pNewFilter = ScriptPointer<const DC::SymbolArray>(meshExcludeFilterId, kHcmFilterModule);

	if (!pNewFilter.Valid() && meshExcludeFilterId != INVALID_STRING_ID_64)
	{
		MsgConScriptError("[%s] Could not change High Contrast Mode Mesh Exclude filter to %s because it was not found in module %s\n",
						  DevKitOnly_StringIdToString(GetUserId()),
						  DevKitOnly_StringIdToString(meshExcludeFilterId),
						  DevKitOnly_StringIdToString(kHcmFilterModule));
		return;
	}

	m_pHcmMeshExcludeFilter = pNewFilter;
	m_pDrawControl->UpdateAllMeshesHCMType(GetHighContrastModeMeshExcludeFilter());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::UpdateHighContrastModeForFactionChange()
{
	// we spawn some gameobjects when initing the game, this block will crash if we try and run it too early
	if (g_ndConfig.m_gameInitComplete)
	{
		if (!ManagesOwnHighContrastMode() && !m_highContrastModeOverridden)
		{
			if (IsEnemy(m_faction, EngineComponents::GetNdGameInfo()->GetPlayerFaction()))
			{
				SetHighContrastMode(DC::kHCMModeTypeEnemy);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::UpdateAttached()
{
	if (m_pAttachmentCtrl != nullptr && m_pAttachmentCtrl->IsAttached())
	{
		Locator curAlignWs;
		if (m_pAttachmentCtrl->DetermineAlignWs(&curAlignWs))
		{
			SetLocator(curAlignWs);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::HorseGetSaddleApRef(BoundFrame& outApRef, DC::HorseRiderPosition riderPos) const
{
	Locator loc;
	bool result = HorseGetSaddleApRef(loc, riderPos);
	if (result)
	{
		outApRef = BoundFrame(loc, GetBinding());
		return true;
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdGameObject::HorseGetSaddleApRef(Locator& outApRef, DC::HorseRiderPosition riderPos) const
{
	// special case -- this means use my own apRef
	if (riderPos == DC::kHorseRiderPositionAuto)
	{
		outApRef = GetLocator();
		return true;
	}

	const FgAnimData& animData = GetAnimControl()->GetAnimData();
	const int attachJoint = riderPos == DC::kHorseRiderPositionFront ? animData.FindJoint(SID("hero_attachment")) : animData.FindJoint(SID("passenger_attachment"));
	if (attachJoint < 0)
		return false;

	Locator offsetLoc = animData.m_jointCache.GetJointLocatorWs(attachJoint);

	// these joints are too noisy to use as APrefs directly, so we set the rotation to match the horse's rotation
	offsetLoc.SetRotation(GetLocator().GetRotation());
	outApRef = offsetLoc;
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdGameObject::HandleDataLoggedOutError()
{
	if (!DataLoggedOutError())
	{
		StringBuilder<140> errorMsg("%s: data logged out", GetName());
		SpawnErrorProcess(this, errorMsg.c_str());
		SetDataLoggedOutError(true);
	}
}
