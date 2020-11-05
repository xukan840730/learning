/*
* Copyright (c) 2003 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/camera/camera-manager.h"

#include "corelib/math/mathutils.h"
#include "corelib/math/matrix3x3.h"
#include "corelib/util/angle.h"

#include "ndlib/frame-params.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/io/joypad.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/nd-config.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/netbridge/debug-conn-session.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/spawn-info.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/dev/render-options.h"
#include "ndlib/render/display.h"
#include "ndlib/render/draw-control.h"
#include "ndlib/render/ngen/coll-raycaster.h"
#include "ndlib/render/render-window.h"
#include "ndlib/render/scene-window.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/settings/render-settings.h"
#include "ndlib/settings/settings.h"
#include "ndlib/settings/priority.h"
#include "ndlib/util/curves.h"
#include "ndlib/water/rain-mgr.h"
#include "ndlib/water/water-mgr.h"
#include "ndlib/netbridge/command-server.h"
#include "ndlib/netbridge/redisrpc-server.h"

#include "gamelib/audio/arm.h"
#include "gamelib/camera/camera-animated.h"
#include "gamelib/camera/camera-fixed.h"
#include "gamelib/camera/camera-look-at-controller.h"
#include "gamelib/camera/camera-manual.h"
#include "gamelib/camera/camera-node.h"
#include "gamelib/camera/camera-pivot.h"
#include "gamelib/camera/camera-strafe-remap.h"
#include "gamelib/camera/camera-strafe-auto.h"
#include "gamelib/camera/scene-camera.h"
#include "gamelib/level/entitytags.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/region/region-control.h"
#include "gamelib/region/region-manager.h"
#include "gamelib/gameplay/nd-attachable-object.h"

#include "gamelib/cinematic/cinematic-manager.h"

#include "gamelib/tasks/task-graph-mgr.h" // PSX DEMO HACK

#ifdef JBELLOMY
#undef MsgCamera
#define MsgCamera(...)			(void)0
#endif

#define ENABLE_SUPPRESS_JOYPAD_DURING_BLEND 0 // this feature was in U1, but it was effectively disabled in U2 and U3

CONST_EXPR bool kDebugCameraBlending = FALSE_IN_FINAL_BUILD(false);

extern bool g_disableManualCamera;
bool g_cameraForceZeroBlendTimes = false;
FINAL_CONST bool g_debugAnimatedCamera = false;

const float kCameraManagerEpsilon = 1e-4f;

const int kCameraManagerMaxCameras = 32;
F32 g_manualCamFocusOnSelectedScale = 1.1f; // 110% of bounding radius

bool g_cameraApplyWantLoad;
bool g_cameraDrawRegions;
bool g_cameraScriptDrawRegions;
bool g_broadcastCameraLocator = false;
int g_cameraBroadcastDelay;
float g_floatCameraBlend = 0.0f;

MutableCameraManagerHandle g_hCameraManager[kMaxCameraManagers];

PROCESS_REGISTER_ALLOC_SIZE_BASE(CameraManager, 64*1024);

// -------------------------------------------------------------------------------------------------
// Camera Interface Gamelib
// -------------------------------------------------------------------------------------------------

void CameraInterfaceGamelib::NotifyCameraCut(const char* file, int line, const char* func)
{
	CameraManager::Get().NotifyCameraCut(file, line, func);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Utility Functions
/// --------------------------------------------------------------------------------------------------------------- ///

const char* CameraBlendTypeToString(CameraBlendType t)
{
	switch (t)
	{
	case CameraBlendType::kDefault:			return "default   ";
	case CameraBlendType::kTime:			return "fade-time ";
	case CameraBlendType::kCrossFade:		return "cross-fade";
	case CameraBlendType::kAccelerateEase:	return "accel-ease";
	case CameraBlendType::kTravelDist:		return "travel-dist";
	case CameraBlendType::kDistToObj:		return "to-obj-dist";
	case CameraBlendType::kScript:			return "script";
	}
	GAMEPLAY_ASSERT(0);
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// CameraRequest
/// --------------------------------------------------------------------------------------------------------------- ///

void CameraRequest::Init(CameraId cameraId, AssociationId associationId, const CameraBlendInfo* pBlendInfo, const CameraStartInfo* pStartInfo, NdGameObjectHandle hDefaultFocusObject)
{
	Clear();
	m_isActive = true;
	m_isDormant = false;
	m_cameraId = cameraId;
	m_associationId = associationId;

	ALWAYS_ASSERTF(cameraId != kCameraNone, ("Attempt to request kCameraNone"));

	// Store the blend info, if any.
	m_blendInfo = pBlendInfo ? *pBlendInfo : CameraBlendInfo();

	CameraConfig& config = const_cast<CameraConfig&>(cameraId.ToConfig());

	// Get default start info if not specified.
	if (pStartInfo == nullptr)
	{
		pStartInfo = &config.GetDefaultStartInfo();
	}

	// Copy the start info.
	// IMPORTANT: CameraStartInfo is a POD (has no vtable) so we can make binary copies of it.
	//            Type safety is enforced by the calling code -- it must ensure that 'cameraId'
	//            and the actual class of '*pStartInfo' match.
	ALWAYS_ASSERT(sizeof(m_startInfo) >= config.GetStartInfoSize());
	memset(&m_startInfo, 0, sizeof(m_startInfo));
	memcpy(&m_startInfo, pStartInfo, config.GetStartInfoSize());

	// Install the default focus object if one was not specified.
	if (!m_startInfo.GetFocusObject().HandleValid())
		m_startInfo.SetFocusObject(hDefaultFocusObject);

	// camera-strafe needs a default settings-id, it not provided.
	// TODO: setup CameraId as TypeRecord so that I can call IsKindOf.
	if (m_startInfo.m_signature == SID("CameraStrafeStartInfo"))
	{
		if (g_cameraOptions.m_forceStrafeFocusObj && g_cameraOptions.m_hForceStrafeFocusObj.HandleValid())
		{
			m_startInfo.SetFocusObject(g_cameraOptions.m_hForceStrafeFocusObj);
		}

		const NdGameObject* pFocusObj = m_startInfo.GetFocusObject().ToProcess();
		if (pFocusObj != nullptr && m_startInfo.m_settingsId == INVALID_STRING_ID_64)
			m_startInfo.m_settingsId = pFocusObj->GetStrafeCameraSettingsId();
	}

	// Install the extension if appropriate.
	m_pExtension = config.CreateCameraRequestExtension();
	if (m_pExtension)
		m_pExtension->IncrementReferenceCount();
}

void CameraRequest::Reinit(CameraId newCameraId, const CameraStartInfo* pNewStartInfo)
{
	ASSERT(m_isActive); // should already be initialized
	Init(newCameraId, m_associationId, &m_blendInfo, pNewStartInfo, m_startInfo.GetFocusObject());
}

CameraPriority CameraRequest::GetPriority() const
{
	CameraPriority effectivePriority;

	if (m_startInfo.m_priority == CameraPriority::Class::kUseDefault)
		effectivePriority = m_cameraId.ToConfig().GetDefaultPriority();
	else
		effectivePriority = m_startInfo.m_priority;

	return effectivePriority;
}

CameraRank CameraRequest::GetRank() const
{
	return m_cameraId.ToConfig().GetDefaultRank();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Static CameraManager Interface
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
struct CameraManagerSpawnInfo : public SpawnInfo
{
	U32 m_playerIndex;

	explicit CameraManagerSpawnInfo(StringId64 type, Process* pParent) : SpawnInfo(type, pParent), m_playerIndex(0) {}
};

float CameraManager::s_nearPlaneDistDefault = 0.24f;

/// Start up the camera system. Should be called once when the program starts.
/* static */
void CameraManager::Init(ICameraManagerCustom** pCustoms)
{
	ALWAYS_ASSERTF(CameraConfig::GetMaxStartInfoSize() <= sizeof(CameraStartInfoStorage),
		("A derived class of CameraStartInfo is too big (%u bytes vs %u bytes) -- please increase sizeof(CameraStartInfoStorage).\n",
		CameraConfig::GetMaxStartInfoSize(),
		sizeof(CameraStartInfoStorage)));

	for (U32F i = 0; i < kMaxCameraManagers; i++)
	{
		CameraManagerSpawnInfo spawn(SID("CameraManager"), EngineComponents::GetProcessMgr()->m_pCameraTree);

		spawn.m_playerIndex = i;
		spawn.SetIgnoreResetActors();

		CameraManager* pCameraManager = PunPtr<CameraManager*>(NewProcess(spawn));
		g_hCameraManager[i] = pCameraManager;

		if (pCameraManager != nullptr)
			pCameraManager->InitCustom(pCustoms[i]);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///

/// Destroy the camera system. Called when program ends.
/* static */
void CameraManager::ShutDown()
{
	for (int i = 0; i < GetNumScreens(); i++)
	{
		CameraManager* pCameraManager = CameraManager::GetGlobalPtr(i);
		if (pCameraManager)
		{
			pCameraManager->ShutdownCustom();

			pCameraManager->GoDie();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///

CameraManager::CameraManager ()
{
	m_flags = 0;
	g_cameraDrawRegions = false;
	g_cameraScriptDrawRegions = false;
	m_cameraStartPos = kZero;
	m_cameraStartTarget = Point(0,0,1);
	m_blendInfoOverride[kBlendInfoPriHigh] = m_blendInfoOverride[kBlendInfoPriLow] = CameraBlendInfo();
	m_pCameraNodes = nullptr;
	m_pSceneCameras = nullptr;
	m_pFinalInfo = nullptr;
	m_pFinalInfoAfterCrossFade = nullptr;
	m_pRegionControl = nullptr;
	m_reevaluateCameraRequests = false;
	m_currCamReachPitchLimit = false;
	m_bCameraCutIn3Frames = false;
	m_bCameraCutIn2Frames = false;
	m_bCameraCutNextFrame = false;
	m_bCameraCutThisFrame = false;
	m_bCameraCutLastFrame = false;
	m_bSuppressNextFrameDisplay = false;
	m_bSuppressThisFrameDisplay = false;
	m_bStartingFirstCamera = false;
	m_reevaluateCameraRequests = false;

	ClearReevaluate();
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::ClearReevaluate()
{
	if (m_reevaluateCameraRequests && g_debugAnimatedCamera && GetPlayerIndex() == 0 && !EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
		MsgCamera(FRAME_NUMBER_FMT "ClearReevaluate\n", FRAME_NUMBER);

	m_reevaluateCameraRequests = false;
	m_reevaluateBlendInfo = CameraBlendInfo();
}

/// --------------------------------------------------------------------------------------------------------------- ///

CameraManager::~CameraManager ()
{
//	m_pCameraNodes->Free();
//	m_pSceneCameras->Free();
	m_debugCameraTrajectoryPlot.Destroy();

	if (m_hAsyncWaterDetector.IsValid())
	{
		WaterQuery::ReleaseDetectorCmp(m_hAsyncWaterDetector);
	}

	for (int i = 0; i < kMaxNumPostCameraUpdateProcesses; i++)
	{
		if (m_postCameraUpdateProcesses[i].HandleValid())
			KillProcess(m_postCameraUpdateProcesses[i]);
	}
}

Err CameraManager::Init(const ProcessSpawnInfo& spawn)
{
	const CameraManagerSpawnInfo& spawnInfo = *PunPtr<const CameraManagerSpawnInfo*>(&spawn);

	m_playerIndex = spawnInfo.m_playerIndex;
	m_workingCamera.Init();
	m_workingCameraAfterCrossFade.Init();
	UpdateFinalCameras();

	// Set up global final camera aliases
	m_pFinalInfo  = &g_mainCameraInfo[m_playerIndex];
	m_pFinalInfoAfterCrossFade = nullptr;
	if (m_playerIndex == 0 && !EngineComponents::GetNdGameInfo()->m_isHeadless)
	{
		m_pFinalInfoAfterCrossFade = &g_mainCameraInfo[1];
	}

	Clock* pClock = EngineComponents::GetNdFrameState()->GetClock(kCameraClock);
	pClock->SetRunWhenProgPaused(true);
	SetClock(pClock);
	SetStopWhenPaused(false);
	SetIgnoreResetActors(true);

	m_primaryJoypadControl = JoypadControl::kGame;
	m_pRegionControl	   = NDI_NEW RegionControl(SID("camera-script"), true);

	SettingSetDefault(&g_cameraApplyWantLoad, true);
	SettingSetDefault(&g_disableManualCamera, false);

	// prepare the lists
	m_pCameraNodes		  = NDI_NEW CameraNodeList;
	m_pSceneCameras		  = NDI_NEW CameraSceneTableList;
	m_pPersistentRequests = NDI_NEW CameraRequestList;

	m_pCameraNodes->Allocate(kCameraManagerMaxCameras);
	m_pSceneCameras->Allocate(8);
	m_pPersistentRequests->Allocate(20);

	m_velocity = Vector(kZero);
	m_bCameraCutThisFrame	  = true;
	m_bCameraCutNextFrame	  = false;
	m_bCameraCutIn2Frames	  = false;
	m_bCameraCutIn3Frames	  = false;
	m_bCameraCutLastFrame	  = false;

	m_noManualLocation = CameraLocation(m_pFinalInfo->GetLocator(),
										m_pFinalInfo->GetFov());

	m_noAdjustToWaterLocation = m_noManualLocation;
	m_startLocator = m_noManualLocation.GetLocator();

	m_deltaLocator = Locator(kIdentity);
	m_deltaLocatorUnpaused = Locator(kIdentity);

	m_currentRequest.Clear();

	m_pLookAtController = NDI_NEW CameraLookAtController(m_playerIndex);

	return Process::Init(spawn);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraManager::UpdateFinalCameras()
{
	PROFILE(Camera, UpdateFinalCameras);

	g_mainCameraInfo[m_playerIndex] = m_workingCamera;
	if (m_playerIndex == 0 && !EngineComponents::GetNdGameInfo()->m_isHeadless)
	{
		g_mainCameraInfo[1] = m_workingCameraAfterCrossFade;
	}

	const bool bGamePaused = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused();

	// camera location is determined, so we can update processes which is parented to camera.
	if (!bGamePaused)
	{
		for (int i = 0; i < kMaxNumPostCameraUpdateProcesses; i++)
		{
			if (m_postCameraUpdateProcesses[i].HandleValid())
			{
				SendEvent(SID("post-camera-update"), m_postCameraUpdateProcesses[i]);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound)
{
	DeepRelocatePointer(m_pCameraNodes, offset_bytes, lowerBound, upperBound);
	DeepRelocatePointer(m_pSceneCameras, offset_bytes, lowerBound, upperBound);
	DeepRelocatePointer(m_pPersistentRequests, offset_bytes, lowerBound, upperBound);
	DeepRelocatePointer(m_pRegionControl, offset_bytes, lowerBound, upperBound);

	RelocatePointer(m_pLookAtController, offset_bytes, lowerBound, upperBound);

	Process::Relocate(offset_bytes, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///

ProcessSnapshot* CameraManager::AllocateSnapshot() const
{
	return CameraManagerSnapshot::Create(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::RefreshSnapshot(ProcessSnapshot* pSnapshot) const
{
	PROFILE(Processes, CameraMgr_RefreshSnapshot);

	ALWAYS_ASSERT(pSnapshot);

	ParentClass::RefreshSnapshot(pSnapshot);

	CameraManagerSnapshot& cameraManagerSnapshot = *(CameraManagerSnapshot::FromSnapshot(pSnapshot));

	cameraManagerSnapshot.m_noManualInfo = GetNoManualLocation();
	cameraManagerSnapshot.m_noManualDeltaUnpaused = GetDeltaLocatorUnpaused();
	cameraManagerSnapshot.m_gameFrameNumber = EngineComponents::GetNdFrameState()->m_gameFrameNumber;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraManager::InitCustom(ICameraManagerCustom* pCustom)
{
	m_pCustom = pCustom;
	m_pCustom->Init(*this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraManager::ShutdownCustom()
{
	if (m_pCustom != nullptr)
	{
		m_pCustom->ShutDown(*this);
		m_pCustom = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
CameraManagerHandle CameraManager::GetHandle()
{
	ALWAYS_ASSERT(EngineComponents::GetNdGameInfo()->GetPlayerIndex() >= 0 && EngineComponents::GetNdGameInfo()->GetPlayerIndex() < kMaxCameraManagers);
	return g_hCameraManager[EngineComponents::GetNdGameInfo()->GetPlayerIndex()];
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
CameraManagerHandle CameraManager::GetGlobalHandle(int index)
{
	ALWAYS_ASSERT(index >= 0 && index < kMaxCameraManagers);
	return g_hCameraManager[index];
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
CameraManager* CameraManager::GetPtr()
{
	ALWAYS_ASSERT(EngineComponents::GetNdGameInfo()->GetPlayerIndex() >= 0 && EngineComponents::GetNdGameInfo()->GetPlayerIndex() < kMaxCameraManagers);
	return g_hCameraManager[EngineComponents::GetNdGameInfo()->GetPlayerIndex()].ToMutableProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
CameraManager* CameraManager::GetGlobalPtr(int index)
{
	ALWAYS_ASSERT(index >= 0 && index < kMaxCameraManagers);
	return g_hCameraManager[index].ToMutableProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
CameraManager& CameraManager::Get()
{
	ALWAYS_ASSERT(EngineComponents::GetNdGameInfo()->GetPlayerIndex() >= 0 && EngineComponents::GetNdGameInfo()->GetPlayerIndex() < kMaxCameraManagers);
	CameraManager* pMgr = g_hCameraManager[EngineComponents::GetNdGameInfo()->GetPlayerIndex()].ToMutableProcess();
	ALWAYS_ASSERT(pMgr != nullptr);
	return *pMgr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
CameraManager& CameraManager::GetGlobal(int index)
{
	ALWAYS_ASSERT(index >= 0 && index < kMaxCameraManagers);
	CameraManager* pMgr = g_hCameraManager[index].ToMutableProcess();
	ALWAYS_ASSERT(pMgr != nullptr);
	return *pMgr;
}

/// --------------------------------------------------------------------------------------------------------------- ///

/// Checks to see if the point is in the view frustum.
// FIXME: Fix for split screen.
/* static */
bool CameraManager::IsOnScreen(int playerIndex, Point pos, float radius)
{
	CameraManager& mgr = CameraManager::GetGlobal(playerIndex);
	Sphere sphere(pos, Scalar(radius));

	const CameraControl* pGameCam = mgr.GetCurrentCamera(false);
	const CameraControl* pActualCam = mgr.GetCurrentCamera(true);

	if (!pGameCam || pGameCam == pActualCam)
	{
		// no debug cameras are currently pushed, so just ask the render camera
		return mgr.m_pFinalInfo->GetRenderCamera().IsSphereInFrustum(sphere);
	}
	else
	{
		// a debug camera is currently pushed, so fake up a game RenderCamera
		const RenderCamera& debugRenderCam = mgr.m_pFinalInfo->GetRenderCamera();
		CameraFinalInfo finalInfo;
		finalInfo.Set(mgr.GetNoManualLocation(), debugRenderCam.GetAspect(), debugRenderCam.m_farDist,
					  debugRenderCam.m_nearDist, g_renderOptions.m_cameraHorizontal3DOffset);
		const RenderCamera& gameRenderCam = finalInfo.GetRenderCamera();
		return gameRenderCam.IsSphereInFrustum(sphere);
	}

	return false;
}

RenderCamera CameraManager::GetGameRenderCamera()
{
	const CameraControl* pGameCam = GetCurrentCamera(false);
	const CameraControl* pActualCam = GetCurrentCamera(true);
	if (!pGameCam || pGameCam == pActualCam)
	{
		// no debug cameras are currently pushed, so just ask the render camera
		return m_pFinalInfo->GetRenderCamera();
	}
	else
	{
		// a debug camera is currently pushed, so fake up a game RenderCamera
		const RenderCamera& debugRenderCam = m_pFinalInfo->GetRenderCamera();
		CameraFinalInfo finalInfo;
		finalInfo.Set(GetNoManualLocation(), debugRenderCam.GetAspect(), debugRenderCam.m_farDist,
			m_nearPlaneDist, g_renderOptions.m_cameraHorizontal3DOffset);
		const RenderCamera& gameRenderCam = finalInfo.GetRenderCamera();
		return gameRenderCam;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// If the returns the closest point in WS that is on screen.
/* static */
Point CameraManager::GetClosestPointOnScreen(int playerIndex, Point_arg pos, float radius/* = 0.0f*/)
{
	CameraManager& mgr = CameraManager::GetGlobal(playerIndex);
	Sphere sphere(pos, Scalar(radius));

	RenderCamera gameRenderCam = mgr.GetGameRenderCamera();

	if (gameRenderCam.IsSphereInFrustum(sphere))
	{
		return pos;
	}
	else
	{
		//Find position that is in view of the camera.

		/*
		inline bool ProjectPointOnPlane(Point_arg ptFreePoint,
		Point_arg ptPlane,
		Vector_arg ptNormal ,
		Point* pptPlaneProjection)
		*/

		Vec4 frust[6];
		gameRenderCam.GetWorldSideFrustumPlanes(frust);
		gameRenderCam.GetWorldNearFrustumPlane(&frust[4]);
		U32 planeCount = 5;

		Vec4 center(sphere.GetCenter().GetVec4());
		Point adjustedPos(pos);
		Point pointOnPlane;
		for (U32 ii = 0; ii < planeCount; ++ii)
		{
			Vector normal(frust[ii].X(), frust[ii].Y(), frust[ii].Z());
			if (!ProjectPointOnPlane(adjustedPos,
				gameRenderCam.m_position + normal*sphere.GetRadius(),
				normal,
				&pointOnPlane))
			{
				adjustedPos = pointOnPlane;
			}
		}

		return adjustedPos;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Returns the angle from camdir to a given point
/* static */
Scalar CameraManager::TargetAngle(int playerIndex, Point pos)
{
	CameraManager& mgr = CameraManager::GetGlobal(playerIndex);

	const CameraControl* pGameCam	  = mgr.GetCurrentCamera(false);
	const CameraControl* pActualCam = mgr.GetCurrentCamera(true);

	if (!pGameCam || pGameCam == pActualCam)
	{
		// no debug cameras are currently pushed, so just ask the render camera
		return mgr.m_pFinalInfo->GetRenderCamera().TargetAngle(pos);
	}
	else
	{
		// a debug camera is currently pushed, so fake up a game RenderCamera
		const RenderCamera& debugRenderCam = mgr.m_pFinalInfo->GetRenderCamera();
		CameraFinalInfo finalInfo;
		finalInfo.Set(mgr.GetNoManualLocation(),
					  debugRenderCam.GetAspect(),
					  debugRenderCam.m_farDist,
					  debugRenderCam.m_nearDist,
					  g_renderOptions.m_cameraHorizontal3DOffset);

		const RenderCamera& gameRenderCam = finalInfo.GetRenderCamera();
		return gameRenderCam.TargetAngle(pos);
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void CameraManager::OverrideNearPlaneDist(float nearPlaneDist, U32F priority)
{
	CameraManager &manager = CameraManager::Get();
	manager.SetNearPlaneOverride(nearPlaneDist, priority);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void CameraManager::SetDefaultNearPlaneDist(float nearPlaneDist)
{
	ASSERT(nearPlaneDist > 0.0f);
	CameraManager::s_nearPlaneDistDefault = nearPlaneDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
CameraControl* CameraManager::CreateControlFromName(int playerIndex, StringId64 processType, const CameraStartInfo* pStartInfo)
{
	CameraManager& cameraManager = CameraManager::GetGlobal(playerIndex);

	CameraControlSpawnInfo spawn(processType);
	spawn.m_pParent		= &cameraManager;
	spawn.m_playerIndex = playerIndex;
	spawn.m_pStartInfo = pStartInfo;

	CameraControl* pControl		= static_cast<CameraControl*>(NewProcess(spawn));
	const char* processTypeName = g_typeFactory.GetTypeName(processType);

	if (pControl)
	{
		// if (processTypeName)
		// 	MsgCamera("CAMERA: CreateControl: %s\n", processTypeName);
	}
	else
	{
		MsgErr("Failed to spawn camera -- out of process heap memory (cam type %s)\n", processTypeName);
	}

	return pControl;
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::AddSceneCameraTable (SceneCameraTable* cameraTable, const Level* pLevel)
{
	//MsgOut("Loading camera table with %d entries\n", cameraTable->m_numCameras);

	CameraSceneTable* table = m_pSceneCameras->GetFreeNode();
	if (table)
	{
		table->m_table = cameraTable;
		m_pSceneCameras->PushBack(table);

		// if there's a _cam_start_ in here, teleport the base manual camera there...
		for (int ii = 0; ii < cameraTable->m_numCameras; ++ii)
		{
			SceneCamera* cam = &cameraTable->m_cameraArray[ii];
			const char* name = cam->m_name;
			if (name && strstr(name,"_cam_start_"))
			{
				Vector dir(kZero), up(kZero), left(kZero);
				Point pos(kZero);

				//up = cam->m_up;
				dir = cam->m_forward;
				// Normalize(&up);
				// Normalize(&dir);
				// left = Cross(cam->m_up, cam->m_forward);
				// Normalize(&left);
				pos = cam->m_position;

				m_cameraStartPos = pLevel->GetLoc().TransformPoint(pos);
				dir = Normalize(dir);
				m_cameraStartTarget = m_cameraStartPos + dir*10.f;
			}
		}
	}
	else
	{
		PrintTo(kMsgErr, "Ran out of scene camera tables in camera manager!!!\n");
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::RemoveSceneCameraTable (SceneCameraTable* cameraTable)
{
	for (CameraSceneTableList::Iterator it = m_pSceneCameras->Begin(); it != m_pSceneCameras->End(); ++it)
	{
		if (it->m_table == cameraTable)
		{
			m_pSceneCameras->ReleaseUsedNode(*it);
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject* CameraManager::GetDefaultFocusObject() const
{
	// By default, focus on the player character.
	return EngineComponents::GetNdGameInfo()->GetGlobalPlayerGameObject(GetPlayerIndex());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraManager::StartCameraInternal(CameraControl* pNewCamera, CameraRank rank, CameraBlendInfo blendInfo, const CameraControl* pInsertAfterCam)
{
	if (!pNewCamera)
		return;

	AtomicLockJanitorWrite jj(&m_cameraNodeLock, FILE_LINE_FUNC);

	if (!pNewCamera->AllowCameraLookAt())
	{
		if (m_pLookAtController->IsEnabled())
		{
			MsgScript("Disabling camera look at because current camera doesn't allow scripted look at\n");
			m_pLookAtController->Disable();
		}
	}

	if (blendInfo.m_type == CameraBlendType::kDefault)
	{
		ASSERTF(false, ("Invalid blend parameters -- using global default")); // We could put camera-type-specific default blend params in CameraConfig...
		blendInfo = Seconds(0.5f);
	}

	bool bCameraCut = false;
	if (blendInfo.m_type != CameraBlendType::kTravelDist && blendInfo.m_type != CameraBlendType::kDistToObj && blendInfo.m_time == Seconds(0.0f) && !pNewCamera->IsManualCamera())
	{
		CameraId cameraId = pNewCamera->GetCameraId();
		if (cameraId != kCameraManual)
		{
			NotifyCameraCut(FILE_LINE_FUNC);
			bCameraCut = true;
		}

		// cross fades in zero time are just blends
		if (blendInfo.m_type == CameraBlendType::kCrossFade)
		{
			blendInfo.m_type = CameraBlendType::kTime;
		}
	}

	// report the camera change
	//if (const char* pName = pControl->DevKitOnly_GetSpawnerName())
	//	MsgCamera("CAMERA: StartCamera '%s': %s over %f seconds, rank %s\n", pName, blendInfo.m_type.ToString(), ToSeconds(blendInfo.m_time), rank.ToString());
	//else
	//	MsgCamera("CAMERA: StartCamera: %s over %f seconds, rank %s\n", blendInfo.m_type.ToString(), ToSeconds(blendInfo.m_time), rank.ToString());

	CameraNode* pNode = m_pCameraNodes->GetFreeNode();
	GAMEPLAY_ASSERT(pNode != nullptr);
	if (pNode)
	{
		pNode->m_handle = pNewCamera;

		pNewCamera->SetRank(rank);
		pNewCamera->SetBlendInfo(blendInfo);

		// find the place to insert into the list:
		// it must be before the first node of equal or lower rank.
		// NOTE: the rank system is merely used to keep the "Base" camera at the bottom
		// of the stack and the "Debug" camera at the top
		CameraNode* pInsertNode = nullptr;
		const CameraControl* pInsertControl = nullptr;
		for (CameraNodeList::Iterator it = m_pCameraNodes->Begin(); it != m_pCameraNodes->End(); ++it)
		{
			const CameraControl* pOtherControl = it->m_handle.ToProcess();
			if (pOtherControl && pNewCamera->RankDifference(*pOtherControl) < 0)
				break;

			if (pInsertAfterCam != nullptr && pOtherControl == pInsertAfterCam)
				break;

			pInsertNode = *it;
			pInsertControl = pOtherControl;
		}

		pNewCamera->SetCameraLocation(pNewCamera->StartLocation(pInsertControl));

		// notify the new camera it's a cut.
		if (bCameraCut)
		{
			pNewCamera->NotifyCut(pInsertControl);
		}

		if (pInsertNode)
		{
			pNode->InsertAfter(pInsertNode);
		}
		else
		{
			m_pCameraNodes->PushFront(pNode);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraControl* CameraManager::StartAutoGenCamera(const AutoGenCamParams& params,
												 bool isOutro,
												 CameraControl* pCurCamera,
												 StringId64 baseSettingsId,
												 const CameraBlendInfo* pBlendInfo,
												 bool distBlendImmediate,
												 bool disableInputUntil70)
{
	const CameraControlStrafeBase* pCurCameraStrafeBase = CameraControlStrafeBase::FromProcess(pCurCamera);

	CameraStrafeAutoGenStartInfo startInfo;
	startInfo.SetFocusObject(pCurCamera->GetFocusObject());
	startInfo.m_settingsId = baseSettingsId;
	startInfo.m_params = params;
	startInfo.m_immediate = distBlendImmediate;
	startInfo.m_disableInputUntil70 = disableInputUntil70;

	CameraControl* pAutoGenCamera = CreateControlFromName(m_playerIndex, SID("CameraControlStrafeAutoGenerated"), &startInfo);
	if (pAutoGenCamera != nullptr)
	{
		pAutoGenCamera->SetCameraId(kCameraStrafeAutoGen);
		pAutoGenCamera->SetPriority(pCurCamera->GetPriority());

		GAMEPLAY_ASSERT(IsFinite(startInfo.m_params.m_characterLoc));
		GAMEPLAY_ASSERT(IsFinite(startInfo.m_params.m_cameraLoc));

		CameraBlendInfo blendInfo = pAutoGenCamera->CameraStart(startInfo);
		if (pBlendInfo != nullptr && pBlendInfo->m_type == CameraBlendType::kTime)
		{
			// patch blend time.
			blendInfo.m_time = pBlendInfo->m_time;
		}
		else if (isOutro)
		{
			const CameraControlAnimated* pCameraAnimated = CameraControlAnimated::FromProcess(pCurCamera);
			if (pCameraAnimated != nullptr)
			{
				float fadeOutTime = pCameraAnimated->GetToAutoGenFadeOutTime();
				GAMEPLAY_ASSERT(fadeOutTime >= 0.f);
				blendInfo = CameraBlendInfo(Seconds(fadeOutTime));

				CameraControlStrafeAutoGenerated* pAutoGenCam2 = CameraControlStrafeAutoGenerated::FromProcess(pAutoGenCamera);
				if (pAutoGenCam2 != nullptr)
				{
					pAutoGenCam2->SetFadeOutDistBlendTime(fadeOutTime);
				}
			}
		}

		// for debugging.
		pAutoGenCamera->m_file = __FILE__;
		pAutoGenCamera->m_line = __LINE__;
		pAutoGenCamera->m_func = __PRETTY_FUNCTION__;

		CameraRank rank = pCurCamera->GetRank();
		StartCameraInternal(pAutoGenCamera, rank, blendInfo);

		// Drop the old camera.
		if (pCurCamera)
		{
			ASSERT(pCurCamera->GetRank() != CameraRank::Bottom); // bottom and top cameras are not returned by GetCurrentCamera()
			pCurCamera->KillWhenBlendedOut();
		}

		// clear override blend
		ClearBlendInfoOverride();
	}

	return pAutoGenCamera;
}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraControl* CameraManager::StartDistRemapTop(const DistCameraParams& params,
												CameraControl* pDistCameBase,
												StringId64 baseSettingsId,
												const CameraBlendInfo* pBlendInfo)
{
	CameraStrafeStartInfo startInfo;
	startInfo.SetFocusObject(pDistCameBase->GetFocusObject());
	startInfo.m_settingsId = baseSettingsId;
	startInfo.m_pDistRemapParams = &params;

	CameraControl* pNewCamera = CreateControlFromName(m_playerIndex, SID("CameraControlStrafe"), &startInfo);
	if (pNewCamera != nullptr)
	{
		pNewCamera->SetCameraId(kCameraStrafe);
		pNewCamera->SetPriority(pDistCameBase->GetPriority());

		CameraBlendInfo blendInfo = pNewCamera->CameraStart(startInfo);
		if (pBlendInfo != nullptr && pBlendInfo->m_type == CameraBlendType::kTime)
		{
			// patch blend time.
			blendInfo.m_time = pBlendInfo->m_time;
		}

		// for debugging.
		pNewCamera->m_file = __FILE__;
		pNewCamera->m_line = __LINE__;
		pNewCamera->m_func = __PRETTY_FUNCTION__;

		const CameraRank rank = pDistCameBase->GetRank();
		StartCameraInternal(pNewCamera, rank, blendInfo);

		// Drop the old camera.
		if (pDistCameBase)
		{
			ASSERT(pDistCameBase->GetRank() != CameraRank::Bottom); // bottom and top cameras are not returned by GetCurrentCamera()
			pDistCameBase->KillWhenBlendedOut();
		}

		// clear override blend
		ClearBlendInfoOverride();
	}

	return pNewCamera;
}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraControl* CameraManager::StartDistRemapBase(CameraRequest* pRequest, CameraControl* pCurrCamera, const CameraBlendInfo* pBlendInfo)
{
	const CameraConfig& config = pRequest->m_cameraId.ToConfig();

	CameraControl* pNewCamera = CreateControlFromName(m_playerIndex, config.GetProcessType(), &pRequest->m_startInfo);
	if (pNewCamera != nullptr)
	{
		pNewCamera->SetCameraId(pRequest->m_cameraId);
		pNewCamera->SetPriority(pRequest->GetPriority());

		// Pass the extension to the camera so it can potentially increase its reference count and "own" it
		// even after the persistent CameraRequest is gone.
		pRequest->m_startInfo.m_pExtension = pRequest->m_pExtension;

		CameraBlendInfo blendInfo = pNewCamera->CameraStart(pRequest->m_startInfo);
		blendInfo = (pBlendInfo != nullptr) ? *pBlendInfo : CameraBlendInfo(Seconds(0.f));

		pRequest->m_hCamera = pNewCamera;

		// for debugging.
		pNewCamera->m_file = pRequest->m_file;
		pNewCamera->m_line = pRequest->m_line;
		pNewCamera->m_func = pRequest->m_func;

		const CameraRank rank = pCurrCamera->GetRank();

		StartCameraInternal(pNewCamera, rank, blendInfo);

		// Drop the old camera.
		if (pCurrCamera != nullptr)
		{
			ASSERT(pCurrCamera->GetRank() != CameraRank::Bottom); // bottom and top cameras are not returned by GetCurrentCamera()
			pCurrCamera->KillWhenBlendedOut();
		}

		pNewCamera->KillWhenBlendedOut();

		// clear override blend
		ClearBlendInfoOverride();
	}

	return pNewCamera;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const CameraControl* CameraManager::SearchBackwardsAndMatch(const CameraControl* pPrevControl, const CameraRequest* pBestRequest, bool breakIfTypeMatch) const
{
	// search back any camera matches camera-request.
	const CameraControl* pTestCamera = pPrevControl;
	while (pTestCamera != nullptr)
	{
		if (pTestCamera->GetCameraId() == pBestRequest->m_cameraId)
		{
			if (pTestCamera->IsEquivalentTo(pBestRequest->m_startInfo))
				return pTestCamera;

			if (breakIfTypeMatch)
				break;
		}

		pTestCamera = GetPreviousCamera(pTestCamera);
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::SetPrimaryJoypadControl(JoypadControl ctrl)
{
	m_primaryJoypadControl = ctrl;

	I32 padActive, padSuppressed;
	if (ctrl == JoypadControl::kGame)
	{
		// if the primary joypad controls the game, suppress the camera pad
		padActive     = DC::kJoypadPadPlayer1 + m_playerIndex;
		padSuppressed = DC::kJoypadPadCamera1 + ConvertToScreenIndex(m_playerIndex);
	}
	else
	{
		// if the primary joypad controls the manual camera, suppress the player pad
		padActive     = DC::kJoypadPadCamera1 + ConvertToScreenIndex(m_playerIndex);
		padSuppressed = DC::kJoypadPadPlayer1 + m_playerIndex;
	}

	EngineComponents::GetNdFrameState()->m_joypad[padActive].SetSuppression(false, kSuppressionPriorityDefault);
	EngineComponents::GetNdFrameState()->m_joypad[padSuppressed].SetSuppression(true, kSuppressionPriorityDefault);
}

/// --------------------------------------------------------------------------------------------------------------- ///

I32 CameraManager::GetInputPadIndex() const
{
	const int padIndex = (m_primaryJoypadControl == CameraManager::JoypadControl::kGame)
						 ? (DC::kJoypadPadPlayer1 + m_playerIndex)
						 : (DC::kJoypadPadCamera1 + ConvertToScreenIndex(m_playerIndex));
	return padIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///

//void CameraManager::WaterPatchKickQueries()
//{
//	if (m_playerIndex == 0)
//	{
//		// disable water-patch right now.
//		const Point center = m_noManualLocation.GetLocator().GetTranslation();
//		m_waterPatch.KickQueries(center, 2.5f);
//	}
//}

/// --------------------------------------------------------------------------------------------------------------- ///

//void CameraManager::WaterPatchGatherQueries()
//{
//	if (m_playerIndex == 0)
//	{
//		m_waterPatch.GatherQueries();
//		//m_waterPatch.DebugDraw();
//	}
//}

/// --------------------------------------------------------------------------------------------------------------- ///

extern bool g_disableHideNearCamera[2];

void CameraManager::ActivateManualCamera()
{
	SetPrimaryJoypadControl(JoypadControl::kManualCamera);

	if (!m_hManualCamera.HandleValid())
	{
		CameraBlendInfo blendInfo(Seconds(0.0f));
		m_hManualCamera = RequestCamera(FILE_LINE_FUNC, kCameraManual, &blendInfo);
	}

	//return m_hManualCamera.HandleValid();
	g_disableHideNearCamera[ConvertToScreenIndex(m_playerIndex)] = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::DeactivateManualCamera()
{
	SetPrimaryJoypadControl(JoypadControl::kGame);

	CameraControl* const pManualCamera = m_hManualCamera.ToMutableProcess();
	if (pManualCamera)
	{
		pManualCamera->End(Seconds(0.0f));
		//NotifyCameraCut(FILE_LINE_FUNC);
	}

	g_disableHideNearCamera[ConvertToScreenIndex(m_playerIndex)] = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///

bool CameraManager::ToggleManualCamera()
{
	if (m_hManualCamera.HandleValid())
	{
		DeactivateManualCamera();
		return false;
	}
	else
	{
		ActivateManualCamera();
		return true;
	}

	return m_hManualCamera.HandleValid();
}

/// --------------------------------------------------------------------------------------------------------------- ///

const CameraControl* CameraManager::GetManualCamera() const
{
	return m_hManualCamera.ToProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///

CameraLocation GameGlobalCameraInfo(int playerIndex)
{
	CameraManagerHandle hManager = CameraManager::GetGlobalHandle(playerIndex);

	if (const CameraManagerSnapshot* pCameraManagerSnapshot = hManager.ToSnapshot<CameraManagerSnapshot>())
	{
		return pCameraManagerSnapshot->GetNoManualInfo();
	}

	return CameraLocation();
}

Locator GameGlobalCameraLocator(int playerIndex)
{
	const Locator camLoc = GameGlobalCameraInfo(playerIndex).GetLocator();
	ANIM_ASSERT(IsNormal(camLoc.GetRotation()));
	return camLoc;
}

CameraLocation GameCameraInfo()
{
	return GameGlobalCameraInfo(EngineComponents::GetNdGameInfo()->GetPlayerIndex());
}

Locator GameCameraLocator()
{
	return GameGlobalCameraLocator(EngineComponents::GetNdGameInfo()->GetPlayerIndex());
}

Locator GameGlobalCameraUnpausedDelta()
{
	CameraManagerHandle hManager = CameraManager::GetGlobalHandle(0);

	if (const CameraManagerSnapshot* pCameraManagerSnapshot = hManager.ToSnapshot<CameraManagerSnapshot>())
	{
		const Locator delta = pCameraManagerSnapshot->GetNoManualDeltaUnpaused();
		ANIM_ASSERT(IsNormal(delta.GetRotation()));
		return delta;
	}

	return Locator(kIdentity);
}

/// --------------------------------------------------------------------------------------------------------------- ///
I64 GameCameraFrameNumber()
{
	int playerIndex = EngineComponents::GetNdGameInfo()->GetPlayerIndex();
	CameraManagerHandle hManager = CameraManager::GetGlobalHandle(playerIndex);

	if (const CameraManagerSnapshot* pCameraManagerSnapshot = hManager.ToSnapshot<CameraManagerSnapshot>())
	{
		return pCameraManagerSnapshot->GetFrameNumber();
	}

	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector GameCameraDirectionXz()
{
	const Locator camLoc = GameCameraLocator();
	const Vector camLeft = GetLocalX(camLoc.Rot());
	const Vector camFwd = GetLocalZ(camLoc.Rot());
	const Vector camFwdXz = SafeNormalize(Cross(camLeft, kUnitYAxis), camFwd);
	return camFwdXz;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CameraManager::FindPostCameraUpdateProcess(ProcessHandle hProc)
{
	AtomicLockJanitor jj(&m_postCameraUpdateLock, FILE_LINE_FUNC);

	for (int i = 0; i < kMaxNumPostCameraUpdateProcesses; i++)
	{
		if (m_postCameraUpdateProcesses[i] == hProc)
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraManager::AddPostCameraUpdateProcess(MutableProcessHandle hProc)
{
	if (!hProc.HandleValid())
		return;

	AtomicLockJanitor jj(&m_postCameraUpdateLock, FILE_LINE_FUNC);

	// check if already exists
	for (int i = 0; i < kMaxNumPostCameraUpdateProcesses; i++)
	{
		if (m_postCameraUpdateProcesses[i] == hProc)
			return;
	}

	for (int i = 0; i < kMaxNumPostCameraUpdateProcesses; i++)
	{
		if (!m_postCameraUpdateProcesses[i].HandleValid())
		{
			m_postCameraUpdateProcesses[i] = hProc;
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraManager::RemovePostCameraUpdateProcess(ProcessHandle hProc)
{
	if (!hProc.HandleValid())
		return;

	AtomicLockJanitor jj(&m_postCameraUpdateLock, FILE_LINE_FUNC);

	for (int i = 0; i < kMaxNumPostCameraUpdateProcesses; i++)
	{
		if (m_postCameraUpdateProcesses[i] == hProc)
			m_postCameraUpdateProcesses[i] = MutableProcessHandle();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::TeleportCurrent(const Locator& loc, bool teleportTopManualCamera)
{
	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);
	if (!m_pCameraNodes->Empty())
	{
		I32F nodeCount = 0;
		for (CameraNodeList::Iterator it = m_pCameraNodes->RBegin(); it != m_pCameraNodes->REnd(); --it)
		{
			CameraNode* node = *it;
			CameraControl* control = node->m_handle.ToMutableProcess();
			if (teleportTopManualCamera && control->IsManualCamera())
			{
				CameraControlManual* pManualControl = (CameraControlManual*)control;
				pManualControl->m_prevLocPs = loc;
				pManualControl->m_attachedGameObject = nullptr;
				control->SetCameraLocation(CameraLocation(loc));
			}

			++nodeCount;
		}

		if (nodeCount <= 1)
		{
			m_noManualLocation.SetLocator(loc);

			ManuallyRefreshSnapshot();
		}

		if (!teleportTopManualCamera)
		{
			CameraNode* node = *m_pCameraNodes->Begin();
			CameraControl* control = node->m_handle.ToMutableProcess();
			if (control->IsManualCamera())
			{
				CameraControlManual* pManualControl = (CameraControlManual*)control;
				pManualControl->m_prevLocPs = loc;
				pManualControl->m_attachedGameObject = nullptr;
				control->SetCameraLocation(CameraLocation(loc));
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///

bool CameraManager::CameraStackEmpty() const
{
	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);
	return m_pCameraNodes->Empty();
}

/// --------------------------------------------------------------------------------------------------------------- ///

bool CameraManager::IsAnimated() const
{
	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);
	CameraNode* node = *m_pCameraNodes->RBegin();
	const CameraControl* control = node->m_handle.ToProcess();
	if (control->IsKindOf(SID("CameraControlAnimated")))
	{
		return true;
	}

	return false;
}


/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::TeleportManualCamera(const Locator& loc)
{
	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);
	if (!m_pCameraNodes->Empty())
	{
		CameraNode* node = *m_pCameraNodes->RBegin();
		CameraControl* control = node->m_handle.ToMutableProcess();
		if (control->IsManualCamera())
		{
			CameraControlManual* pManualControl = (CameraControlManual*)control;
			pManualControl->m_prevLocPs = loc;
			pManualControl->m_attachedGameObject = nullptr;
			control->SetCameraLocation(CameraLocation(loc));
		}
	}

	// Stop publishing camera updates for a bit to prevent charter and the game from fighting over who has camera control
	OMIT_FROM_FINAL_BUILD(if (g_broadcastCameraLocator) g_cameraBroadcastDelay = 20;)
}

/// --------------------------------------------------------------------------------------------------------------- ///

#if !FINAL_BUILD
static NdAtomic64 s_lastCameraCutFrame(-1LL);
#endif

void CameraManager::NotifyCameraCut(const char* file, int line, const char* func)
{
	// Remember that the camera is being cut.
	const bool cut = !g_animOptions.m_disableCameraCuts;
	m_bCameraCutThisFrame = cut;
#if !FINAL_BUILD
	if (g_animOptions.m_debugCameraCuts && cut && s_lastCameraCutFrame.Set(EngineComponents::GetNdFrameState()->m_gameFrameNumber) != EngineComponents::GetNdFrameState()->m_gameFrameNumber)
		MsgCinematic(FRAME_NUMBER_FMT "NotifyCameraCut(): %s:%d\n", FRAME_NUMBER, func, line);
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::NotifyCameraCutNextFrame(const char* file, int line, const char* func)
{
	// Remember that the camera is being cut.
	const bool cut = !g_animOptions.m_disableCameraCuts;
	m_bCameraCutNextFrame = cut;
#if !FINAL_BUILD
	if (g_animOptions.m_debugCameraCuts && cut && s_lastCameraCutFrame.Set(EngineComponents::GetNdFrameState()->m_gameFrameNumber) != EngineComponents::GetNdFrameState()->m_gameFrameNumber)
		MsgCinematic(FRAME_NUMBER_FMT "NotifyCameraCutNextFrame(): %s:%d\n", FRAME_NUMBER, func, line);
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::NotifyCameraCutIn2Frames(const char* file, int line, const char* func)
{
	// Remember that the camera is being cut.
	const bool cut = !g_animOptions.m_disableCameraCuts;
	m_bCameraCutIn2Frames = cut;
#if !FINAL_BUILD
	if (g_animOptions.m_debugCameraCuts && cut && s_lastCameraCutFrame.Set(EngineComponents::GetNdFrameState()->m_gameFrameNumber) != EngineComponents::GetNdFrameState()->m_gameFrameNumber)
		MsgCinematic(FRAME_NUMBER_FMT "NotifyCameraCutIn2Frames(): %s:%d\n", FRAME_NUMBER, func, line);
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::NotifyCameraCutIn3Frames(const char* file, int line, const char* func)
{
	// Remember that the camera is being cut.
	const bool cut = !g_animOptions.m_disableCameraCuts;
	m_bCameraCutIn3Frames = cut;
#if !FINAL_BUILD
	if (g_animOptions.m_debugCameraCuts && cut && s_lastCameraCutFrame.Set(EngineComponents::GetNdFrameState()->m_gameFrameNumber) != EngineComponents::GetNdFrameState()->m_gameFrameNumber)
		MsgCinematic(FRAME_NUMBER_FMT "NotifyCameraCutIn3Frames(): %s:%d\n", FRAME_NUMBER, func, line);
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::SuppressDisplayOfNextFrame()
{
	m_bSuppressNextFrameDisplay = true;
#if !FINAL_BUILD
	if (g_animOptions.m_debugCameraCuts)
		MsgCinematic(FRAME_NUMBER_FMT "SuppressDisplayOfNextFrame() called\n", FRAME_NUMBER);
#endif
}

bool CameraManager::IsThisFrameDisplaySuppressed() const
{
	return m_bSuppressThisFrameDisplay;
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::SetFOV(float fov)
{
	g_cameraOptions.m_defaultFov = fov;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float CameraManager::GetCrossFadeBlend(CameraNode* nodeTable[], I32F nodeCount)
{
	for (int i = 0; i < nodeCount; i++)
	{
		const CameraControl* control = nodeTable[i]->m_handle.ToProcess();

		if (control->WantsCrossFade())
			return control->GetBlend();
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CameraManager::HasCrossFade(CameraNode* nodeTable[], I32F nodeCount)
{
	for (int i = 0; i < nodeCount; i++)
	{
		const CameraControl* control = nodeTable[i]->m_handle.ToProcess();

		if (control->WantsCrossFade())
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline CameraJoypad GetBlendSuppressedJoypad(const CameraJoypad& actualPad, const CameraControl* control, bool suppressJoypadDuringBlend)
{
#if ENABLE_SUPPRESS_JOYPAD_DURING_BLEND
	return (suppressJoypadDuringBlend)
		   ? CameraJoypadBlend(CameraJoypad(), actualPad, QuadCurveEaseInUp.Evaluate(control->GetNormalBlend()))
		   : actualPad;
#else
	return actualPad;
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
Joypad& CameraManager::GetJoypad() const
{
	if (EngineComponents::GetNdPhotoModeManager() && EngineComponents::GetNdPhotoModeManager()->IsActive())
	{
		return EngineComponents::GetNdFrameState()->m_joypad[DC::kJoypadPadCamera1 + ConvertToScreenIndex(m_playerIndex)];
	}
	else
	{
		return EngineComponents::GetNdFrameState()->m_joypad[DC::kJoypadPadPlayer1 + m_playerIndex];
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraManager::UpdateBlend(bool* pManualInBlend, bool* pSuppressJoypadDuringBlend)
{
	*pSuppressJoypadDuringBlend = false;

	float contributeBlend = 1.0f;
	bool bTopCamera = true;

	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);
	for (CameraNodeList::Iterator it = m_pCameraNodes->RBegin(); it != m_pCameraNodes->REnd(); --it)
	{
		CameraControl* pControl = it->m_handle.ToMutableProcess();
		if (pControl != nullptr)
		{
			// Save previous frame's velocity position.
			pControl->m_lastVelocityPosition = pControl->m_velocityPosition;

			float normalBlend = 0.f;

			// handle cameras blended by dist-to-obj as single group.
			if (pControl->IsFadeInByDistToObj())
			{
				float remainingDistWeight = 1.0f;

				{
					const CameraControl* prevControl = (it->Prev()) ? it->Prev()->m_handle.ToProcess() : nullptr;
					pControl->UpdateBlend(prevControl, it->Next());

					const float distWeight = pControl->GetDistWeight();
					GAMEPLAY_ASSERT(distWeight >= 0.f);
					pControl->UpdateNormalBlend(contributeBlend, it->Next(), distWeight);
					normalBlend += pControl->GetNormalBlend();
					remainingDistWeight -= distWeight;
				}

				if (pControl->IsManualCamera() && contributeBlend > 0.001f)
					*pManualInBlend = true;

				if (pControl->GetNormalBlend() > kCameraManagerEpsilon && pControl->SuppressJoypadDuringBlend())
					*pSuppressJoypadDuringBlend = true;

				--it;

				pControl = it->m_handle.ToMutableProcess();
				if (pControl != nullptr)
				{
					// Save previous frame's velocity position.
					pControl->m_lastVelocityPosition = pControl->m_velocityPosition;
					const CameraControl* prevControl = (it->Prev()) ? it->Prev()->m_handle.ToProcess() : nullptr;
					pControl->UpdateBlend(prevControl, it->Next());
					pControl->UpdateNormalBlend(contributeBlend, it->Next(), remainingDistWeight);
					normalBlend += pControl->GetNormalBlend();
				}

				bTopCamera = false;
				contributeBlend -= normalBlend;
			}
			else
			{
				const CameraControl* prevControl = (it->Prev()) ? it->Prev()->m_handle.ToProcess() : nullptr;
				pControl->UpdateBlend(prevControl, it->Next());
				pControl->UpdateNormalBlend(contributeBlend, it->Next());
				normalBlend = pControl->GetNormalBlend();

				if (pControl->IsManualCamera() && contributeBlend > 0.001f)
					*pManualInBlend = true;

				if (pControl->GetNormalBlend() > kCameraManagerEpsilon && pControl->SuppressJoypadDuringBlend())
					*pSuppressJoypadDuringBlend = true;

				if (!bTopCamera || !pControl->KeepLowerCamerasAlive())
				{
					bTopCamera = false;
					contributeBlend -= normalBlend;
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraManager::UpdateLocation(bool suppressJoypadDuringBlend)
{
	I32F nodeCount = 0;
	{
		AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);
		for (CameraNodeList::Iterator it = m_pCameraNodes->RBegin(); it != m_pCameraNodes->REnd(); --it, ++nodeCount) {}
	}
	I32 stackIndex = nodeCount - 1;
	bool bTopCamera = true;

	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);
	for (CameraNodeList::Iterator it = m_pCameraNodes->RBegin(); it != m_pCameraNodes->REnd(); --it)
	{
		CameraControl* pControl = it->m_handle.ToMutableProcess();
		if (pControl != nullptr)
		{
			if (!EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused() ||
				(m_flags & CameraManager::kEnableUpdateWhenPaused) ||
				pControl->GetRunWhenPaused())
			{
				const CameraControl* pPrevControl = (it->Prev()) ? it->Prev()->m_handle.ToProcess() : nullptr;

				CameraJoypad joypad(GetJoypad());
				CameraJoypad effectiveJoypad = GetBlendSuppressedJoypad(joypad, pControl, suppressJoypadDuringBlend);

				m_pLookAtController->AdjustCameraJoypad(effectiveJoypad);

				CameraInstanceInfo cameraInstanceInfo(pControl->GetCameraLocation(), bTopCamera, effectiveJoypad, pControl->GetBlend(), pControl->GetLocationBlend(), pControl->GetNormalBlend(), pPrevControl, nullptr, stackIndex);
				cameraInstanceInfo.m_pWaterPlane = &m_waterPlane;
				const CameraLocation loc0 = pControl->UpdateLocation(cameraInstanceInfo);

				if (FALSE_IN_FINAL_BUILD(g_cameraOptions.m_drawThirds))
				{
					g_prim.DrawNoHide(DebugQuad2D(Vec2(0.333f, 0.0f), Vec2(0.666f, 1.0f), kDebug2DNormalizedCoords, kColorBlueTrans));

					if (g_cameraOptions.m_drawHorizontalThirds)
					{
						g_prim.DrawNoHide(DebugQuad2D(Vec2(0.0f, 0.333f), Vec2(1.0f, 0.666f), kDebug2DNormalizedCoords, kColorBlueTrans));
					}
				}

				if (FALSE_IN_FINAL_BUILD(g_cameraOptions.m_drawCameraBlending) && !pControl->IsManualCamera())
				{
					DebugDrawCamera(loc0, 9.0f / 16.0f, kColorGray, kColorBlack, kPrimDuration1FramePauseable);

					StringBuilder<128> sb;
					sb.format("%s (%s) %d", pControl->GetName(), DevKitOnly_StringIdToString(pControl->GetDCSettingsId()), pControl->GetProcessId());
					if (g_cameraOptions.m_drawNearPlaneDist)
						sb.append_format(", npd:%.4f", loc0.GetNearPlaneDist());

					g_prim.Draw(DebugString(loc0.GetLocator().GetTranslation(), sb.c_str(), kColorGreen, 0.6f), kPrimDuration1FramePauseable);

					if (loc0.HasTarget())
					{
						g_prim.Draw(DebugLine(loc0.GetLocator().GetTranslation(), loc0.GetTarget(), kColorBlack, kColorWhite), kPrimDuration1FramePauseable);
					}
				}

				GAMEPLAY_ASSERT(IsFinite(loc0.GetLocator()));
				pControl->SetCameraLocation(loc0);

				pControl->m_velocityPosition = loc0.GetLocator().GetTranslation();
				if (!AllComponentsEqual(pControl->m_lastVelocityPosition, kInvalidPoint))
				{
					Vector velocity = (pControl->m_velocityPosition - pControl->m_lastVelocityPosition) / Scalar(EngineComponents::GetNdFrameState()->m_deltaTime);
					pControl->m_velocity = velocity;
				}
				else
				{
					// Carry forward previous velocity, or leave invalid, since the camera
					// either doesn't have enough history yet to calculate velocity, or changed
					// its target status.
				}
			}

			if (pControl->GetRank() < CameraRank::Override)
				bTopCamera = false;
		}

		stackIndex--;			
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraLocation CameraManager::BlendLocation(const CameraNode* pNode,
											const CameraNodeConstPtr* apNodesTbl,
											const I32F inputNodeCount,
											const bool currentlyBeforeCrossFade,
											const bool returnBeforeCrossFade)
{
	const CameraControl* control = pNode->m_handle.ToMutableProcess();
	const CameraControl* pPrevControl = nullptr;	// this is prev camera, not next camera!

	I32 nodeCount = inputNodeCount;
	if (nodeCount > 0)
	{
		pPrevControl = apNodesTbl[0]->m_handle.ToProcess();

		// if we are after the cross fade, but want the results before the cross fade, return the next camera
		// unless this is the last camera, then we just return this location
		if (!currentlyBeforeCrossFade && returnBeforeCrossFade)
		{
			bool nexCameraIsBeforeCrossFade = (control->GetBlendInfo().m_type == CameraBlendType::kCrossFade);
			return BlendLocation(apNodesTbl[0], &apNodesTbl[1], nodeCount-1, nexCameraIsBeforeCrossFade, returnBeforeCrossFade);
		}
	}

	CameraLocation loc0 = control->GetCameraLocation();

	if (nodeCount == 0)
	{
		return loc0;
	}
	else
	{
		if (control->WantsCrossFade())
		{
			if (currentlyBeforeCrossFade)
			{
				// there are two cross fades in the stack, throw an error and treat the second cross fade as a termination
				MsgConScriptError("Two cross fades pushed at once! This doesn't work!\n");
				return loc0;
			}
			else if (!returnBeforeCrossFade)
			{
				return loc0;
			}
		}

		const CameraNode* pPrevNode = apNodesTbl[0];
		const CameraNodeConstPtr* apPrevTbl = &apNodesTbl[1];

		// handle cameras blended by dist-to-obj as single group.
		if (control->IsFadeInByDistToObj())
		{
			// TODO: support more than one camera
			if (pPrevControl != nullptr)
			{
				const CameraControlStrafeBase* pCurrStrafeBase = CameraControlStrafeBase::FromProcess(control);
				const CameraControlStrafeBase* pPrevStrafeBase = CameraControlStrafeBase::FromProcess(pPrevControl);
				if (pCurrStrafeBase != nullptr && pPrevStrafeBase != nullptr)
				{
					const float distWeight = MinMax01(pCurrStrafeBase->GetDistWeight());
					const CameraLocation prevLoc0 = pPrevStrafeBase->GetCameraLocation();

					loc0 = CameraLocationBlend(prevLoc0, loc0, distWeight, distWeight);

					// skip previous node.
					pPrevNode = apNodesTbl[1];
					apPrevTbl = &apNodesTbl[2];
					nodeCount--;
				}
			}
		}

		const CameraLocation loc1 = BlendLocation(pPrevNode, apPrevTbl, nodeCount-1, currentlyBeforeCrossFade, returnBeforeCrossFade);
		ALWAYS_ASSERT(IsFinite(loc1.GetLocator()));
		GAMEPLAY_ASSERTF(control->GetBlend() >= 0.0f - FLT_EPSILON && control->GetBlend() <= 1.0f + FLT_EPSILON,
			("%s has a blend %f out of range!\n", control->GetTypeName(), control->GetBlend()));
		GAMEPLAY_ASSERTF(control->GetLocationBlend() >= 0.0f - FLT_EPSILON && control->GetLocationBlend() <= 1.0f + FLT_EPSILON,
			("%s has a locationBlend %f out of range!\n", control->GetTypeName(), control->GetLocationBlend()));
		float blend = MinMax01(1.0f - control->GetBlend());
		float locationBlend = MinMax01(1.0f - control->GetLocationBlend());

		bool sideFirst;
		if (pPrevNode && control->UseCircularBlend(pPrevNode->m_handle.ToProcess(), &sideFirst))
		{
			if (kDebugCameraBlending)
			{
				SetColor(kMsgCon, kColorRed);
				MsgCon("Circular location blend: %s [%s] from %s [%s]\n",
					control->GetTypeName(),
					DevKitOnly_StringIdToString(control->GetDCSettingsId()),
					pPrevControl ? pPrevControl->GetTypeName() : "<none>",
					pPrevControl ? DevKitOnly_StringIdToString(pPrevControl->GetDCSettingsId()) : "<none>");
				SetColor(kMsgCon, kColorWhite);
			}

			return CameraLocationBlendCircular(loc0, loc1, blend, locationBlend, sideFirst);
		}
		// CameraLocationBlendPivot is causing camera pop when animated-camera fades out and strafe-camera fades in.
		// animated-camera doesn't have target, but strafe camera does. When animated-camera is gone, the blend function changes and causes pop.
		// wat-infected-tunnels-help-dina-to-theater is an example.
		//else if (loc0.HasTarget() && loc1.HasTarget()/* && blend < 1.0f && blend > 0.0f*/)
		//{
		//	if (kDebugCameraBlending)
		//	{
		//		SetColor(kMsgCon, kColorGreen);
		//		MsgCon("Pivot location blend: %s [%s] from %s [%s]\n",
		//			control->GetTypeName(),
		//			DevKitOnly_StringIdToString(control->GetDCSettingsId()),
		//			pPrevControl ? pPrevControl->GetTypeName() : "<none>",
		//			pPrevControl ? DevKitOnly_StringIdToString(pPrevControl->GetDCSettingsId()) : "<none>");
		//		SetColor(kMsgCon, kColorWhite);
		//	}
		//	return CameraLocationBlendPivot(loc0, loc1, 3.0f, blend, locationBlend);
		//}
		else
		{
			if (kDebugCameraBlending)
			{
				SetColor(kMsgCon, kColorCyan);
				MsgCon("Default location blend: %s [%s] from %s [%s]\n",
					control->GetTypeName(),
					DevKitOnly_StringIdToString(control->GetDCSettingsId()),
					pPrevControl ? pPrevControl->GetTypeName() : "<none>",
					pPrevControl ? DevKitOnly_StringIdToString(pPrevControl->GetDCSettingsId()) : "<none>");
				SetColor(kMsgCon, kColorWhite);
			}
			return CameraLocationBlend(loc0, loc1, blend, locationBlend);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraManager::KickAsyncWaterDetector()
{
	if (m_playerIndex == 0)
	{
		Locator cameraLoc = m_noManualLocation.GetLocator();

		bool usePhotoModeLoc = false;
		// If we're in photo mode, we want to use the top camera. Because m_noManualLocation thinks photomode is a manual cam
		if (EngineComponents::GetNdPhotoModeManager() && EngineComponents::GetNdPhotoModeManager()->IsActive())
		{
			const CameraControl *pCtrl = GetCurrentCamera(true);
			if (pCtrl)
			{
				cameraLoc = pCtrl->GetLocator();
				usePhotoModeLoc = true;
			}
		}

		Point viewCenter;
		{
			const Vector viewDir = GetLocalZ(cameraLoc.GetRotation());
			viewCenter = GetViewCenterFromCamLoc(m_playerIndex, cameraLoc.GetTranslation(), viewDir);
			if (!usePhotoModeLoc && m_forceQueryWaterFN == EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused)
			{
				viewCenter = m_forceQueryWaterPos;
			}
		}
		GAMEPLAY_ASSERT(IsFinite(viewCenter));

		if (!m_hAsyncWaterDetector.IsValid())
		{
			m_hAsyncWaterDetector = WaterQuery::AllocateDetectorCmp(FILE_LINE_FUNC);
		}
		m_hAsyncWaterDetector.VerticalQuery(viewCenter);

		m_dispWaterDetector.Update(FILE_LINE_FUNC, viewCenter);

		const NdGameObject* pFocusObj = GetDefaultFocusObject();
		if (pFocusObj != nullptr)
		{
			const Point testPos = pFocusObj->GetTranslation();
			const Point probeStart = viewCenter + Vector(0.f, 5.f, 0.f);
			const Vector probeDir = -Vector(0.f, 5.f, 0.f) * 2.f;

			m_collWaterDetector.Update(probeStart, probeDir, testPos, false);
		}
		else
		{
			m_collWaterDetector.Reset();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///

CamWaterPlane CameraManager::GatherAsyncWaterDetector()
{
	CamWaterPlane res;
	res.m_waterFound = false;

	if (m_playerIndex == 0)
	{
		Point viewCenter;
		{
			Locator cameraLoc = m_noManualLocation.GetLocator();

			// If we're in photo mode, we want to use the top camera. Because m_noManualLocation thinks photomode is a manual cam
			if (EngineComponents::GetNdPhotoModeManager() && EngineComponents::GetNdPhotoModeManager()->IsActive())
			{
				const CameraControl *pCtrl = GetCurrentCamera(true);
				if (pCtrl)
				{
					cameraLoc = pCtrl->GetLocator();
				}
			}

			const Vector viewDir = GetLocalZ(cameraLoc.GetRotation());
			viewCenter = GetViewCenterFromCamLoc(m_playerIndex, cameraLoc.GetTranslation(), viewDir);
		}

		Maybe<Point> dispWater;
		// gather disp-water
		{
			Maybe<Point> dispWaterAsync;
			if (m_hAsyncWaterDetector.IsValid())
			{
				WaterQuery::DispResult dispQueryRes = m_hAsyncWaterDetector.CollectResult();
				bool dispWaterFound = dispQueryRes.Valid() && dispQueryRes.WaterFound();
				if (dispWaterFound)
					dispWaterAsync = dispQueryRes.WaterSurface();
			}

			Maybe<Point> dispWaterRegular;
			if (m_dispWaterDetector.Valid() && m_dispWaterDetector.WaterFound())
			{
				dispWaterRegular = m_dispWaterDetector.WaterSurface();
			}

			dispWater = g_cameraOptions.m_useRegularDispWater ? dispWaterRegular : dispWaterAsync;
		}

		// gather coll water.
		Maybe<Point> collWater;
		{
			const WaterDetectorColl2::Result collResult = m_collWaterDetector.GetResult();
			if (collResult.m_valid)
			{
				collWater = PointFromXzAndY(viewCenter, collResult.m_surfaceY);
			}
		}

		bool useDisp = false;
		if (dispWater.Valid() && collWater.Valid())
		{
			const float dispY = dispWater.Get().Y();
			const float collY = collWater.Get().Y();
			const float distYToDisp = Abs(dispY - viewCenter.Y());
			const float distYToColl = Abs(collY - viewCenter.Y());
			if (distYToColl < distYToDisp)
			{
				res.m_surface = collWater.Get();
			}
			else
			{
				res.m_surface = dispWater.Get();
				useDisp = true;
			}
			res.m_plane = Plane(res.m_surface, Vector(kUnitYAxis));
			res.m_waterFound = true;
		}
		else if (dispWater.Valid())
		{
			res.m_waterFound = true;
			res.m_surface = dispWater.Get();
			res.m_plane = Plane(res.m_surface, Vector(kUnitYAxis));
			res.m_waterFound = true;
			useDisp = true;
		}
		else if (collWater.Valid())
		{
			res.m_waterFound = true;
			res.m_surface = collWater.Get();
			res.m_plane = Plane(res.m_surface, Vector(kUnitYAxis));
			res.m_waterFound = true;
		}

		if (FALSE_IN_FINAL_BUILD(g_cameraOptions.m_debugCameraAndWater))
		{
			if (res.m_waterFound)
			{
				DebugPrimTime tt = kPrimDuration1FramePauseable;
				g_prim.Draw(DebugCross(res.m_surface, 0.05f, kColorCyan, PrimAttrib(kPrimDisableHiddenLineAlpha)), tt);

				if (useDisp)
					g_prim.Draw(DebugString(res.m_surface, "water-surface: disp", kColorCyan, 0.6f), tt);
				else
					g_prim.Draw(DebugString(res.m_surface, "water-surface: coll", kColorCyan, 0.6f), tt);
			}
		}
	}

	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///

CameraLocation CameraManager::AdjustToWaterSurface(CameraManager& self, const CameraLocation &camLoc, const CamWaterPlane& waterPlane)
{
	if (m_pCustom)
		return m_pCustom->AdjustToWaterSurface(self, camLoc, waterPlane);

	return camLoc;
}

/// --------------------------------------------------------------------------------------------------------------- ///

CameraLocation CameraManager::AdjustToWind(CameraManager& self, const CameraLocation &camLoc)
{
	if (m_pCustom)
		return m_pCustom->AdjustToWind(self, camLoc);

	return camLoc;
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::PreProcessUpdate()
{
	PROFILE(Processes, CameraManagerPreUpdate);
	if (m_pCustom)
		m_pCustom->PreProcessUpdate(*this);

	if (ShouldShowCameras())
	{
		MsgCon("Persistent Camera Requests For Player #%d\n", m_playerIndex);
		MsgCon("%-50s %-30s %s\n", "Name", "Type", "Priority");
		MsgCon("---------------------------------------------------------------------------------------------------\n");

		CameraRequestList::Iterator it = m_pPersistentRequests->RBegin();
		for ( ; it != m_pPersistentRequests->REnd(); --it)
		{
			CameraRequest* pRequest = *it;
			ASSERT(pRequest);

			if (pRequest->m_cameraId.ToConfig().GetProcessType() == SID("CameraControlAnimated"))
			{
				CameraAnimatedStartInfo* pStartInfo = PunPtr<CameraAnimatedStartInfo*>(&pRequest->m_startInfo);

				const char* pName = DevKitOnly_StringIdToString(pRequest->m_associationId.m_id);

				MsgCon("%-50s %-30s %s [cam %d]%s%s\n",
					pName,
					DevKitOnly_StringIdToString(pRequest->m_cameraId.ToConfig().GetProcessType()),
					pRequest->GetPriority().ToString(),
					(pStartInfo->m_cameraIndex + 1),
					(pRequest->m_isDormant ? " (DORMANT)" : ""),
					(pRequest->m_isAbandoned ? " (abandoned)" : ""));
			}
			else
			{
				MsgCon("%-50s %-30s %s%s\n",
					DevKitOnly_StringIdToString(pRequest->m_associationId.m_id),
					DevKitOnly_StringIdToString(pRequest->m_cameraId.ToConfig().GetProcessType()),
					pRequest->GetPriority().ToString(),
					(pRequest->m_isDormant ? " (DORMANT)" : ""),
					(pRequest->m_isAbandoned ? " (abandoned)" : ""));
			}
		}

		MsgCon("---------------------------------------------------------------------------------------------------\n");
	}

	ResetPerFrameOptions();

	//WaterPatchKickQueries();
	KickAsyncWaterDetector();
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::ResetPerFrameOptions()
{
	g_cameraOptions.m_disableForcedDofThisFrame = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::DOFUpdate()
{
	PROFILE(Camera, DOFUpdate);

	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);

	bool suppressJoypadDuringBlend = false;
	for (CameraNodeList::Iterator it = m_pCameraNodes->RBegin(); it != m_pCameraNodes->REnd(); --it)
	{
		const CameraControl* control = it->m_handle.ToProcess();
		if (control && control->GetNormalBlend() > kCameraManagerEpsilon && control->SuppressJoypadDuringBlend())
			suppressJoypadDuringBlend = true;
	}

	CameraJoypad joypad(GetJoypad());

	bool paused = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused();
	bool first = true;
	int index = 0;
	for (CameraNodeList::Iterator it = m_pCameraNodes->RBegin(); it != m_pCameraNodes->REnd(); --it)
	{
		CameraControl* pControl = it->m_handle.ToMutableProcess();
		const CameraControl* prevControl = (it->Prev()) ? it->Prev()->m_handle.ToProcess() : nullptr;
		const CameraControl* nextControl = (it->Next()) ? it->Next()->m_handle.ToProcess() : nullptr;

		if (pControl)
		{
			if (!paused || (m_flags & CameraManager::kEnableUpdateWhenPaused) || pControl->GetRunWhenPaused())
			{
				CameraJoypad effectiveJoypad = GetBlendSuppressedJoypad(joypad, pControl, suppressJoypadDuringBlend);
				m_pLookAtController->AdjustCameraJoypad(effectiveJoypad);
				pControl->DOFUpdate(CameraInstanceInfo(pControl->GetCameraLocation(), first, effectiveJoypad, pControl->GetBlend(), pControl->GetLocationBlend(), pControl->GetNormalBlend(), prevControl, nextControl, index));
			}
			if (pControl->GetRank() < CameraRank::Override)
			{
				first = false;
			}
		}
		index++;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::PrepareCameras ()
{
	PROFILE(Camera, PrepareCameras);

	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);

	EngineComponents::GetNdFrameState()->UpdatePadSticks();

	const CameraJoypad joypad(GetJoypad());

	CamLookAtTargetPos lookAtTargetPos;
	// for zoom camera, the camera look-at is calculated from reference point, and it will better calculate look-at error in camera space.
	{
		const CameraControl* pCurrCamera = GetCurrentCamera();
		lookAtTargetPos = m_pLookAtController->Update(joypad, GetProcessDeltaTime(), pCurrCamera);
	}

	bool suppressJoypadDuringBlend = false;
	for (CameraNodeList::Iterator it = m_pCameraNodes->RBegin(); it != m_pCameraNodes->REnd(); --it)
	{
		const CameraControl* control = it->m_handle.ToProcess();
		if (control && control->GetNormalBlend() > kCameraManagerEpsilon && control->SuppressJoypadDuringBlend())
			suppressJoypadDuringBlend = true;
	}

	const bool paused = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused();

	// phase 1: cameras read Joypad
	{
		bool first = true;
		int index = 0;
		bool cameraControlByLookAt = false;

		for (CameraNodeList::Iterator it = m_pCameraNodes->RBegin(); it != m_pCameraNodes->REnd(); --it)
		{
			CameraControl* pControl = it->m_handle.ToMutableProcess();
			if (!pControl)
				continue;

			bool canResetCamera = false;
			if (pControl->IsPhotoMode() &&
				!EngineComponents::GetNdPhotoModeManager()->CanChangeCustomCamera(canResetCamera))
			{
				continue;
			}

			const CameraControl* pPrevControl = (it->Prev()) ? it->Prev()->m_handle.ToProcess() : nullptr;
			const CameraControl* pNextControl = (it->Next()) ? it->Next()->m_handle.ToProcess() : nullptr;

			if (!paused || (m_flags & CameraManager::kEnableUpdateWhenPaused) || pControl->GetRunWhenPaused())
			{
				CameraJoypad effectiveJoypad = GetBlendSuppressedJoypad(joypad, pControl, suppressJoypadDuringBlend);

				// if top camera is controlled by look-at-controller, for other cameras, use joystick input will make it feel wobble.
				{
					if (cameraControlByLookAt)
					{
						effectiveJoypad.m_stickCamera = Vec2(0, 0);
						effectiveJoypad.m_rawStickCamera = effectiveJoypad.m_stickCamera;
					}
					else
					{
						m_pLookAtController->AdjustCameraJoypad(effectiveJoypad);

						if (!cameraControlByLookAt)
						{
							// do it only for inspect camera now. I don't want to mess up melee look-at.
							cameraControlByLookAt = m_pLookAtController->IsEnabled() && pControl && !pControl->AllowLowerCameraLookAtJoypadAdjust();
							//&& pControl->AllowLookAtWiggle()
							;
						}
					}
				}

				CameraInstanceInfo cameraInstanceInfo(pControl->GetCameraLocation(), first, effectiveJoypad, pControl->GetBlend(), pControl->GetLocationBlend(), pControl->GetNormalBlend(), pPrevControl, pNextControl, index);
				cameraInstanceInfo.m_pTargetPos = &lookAtTargetPos;
				pControl->Prepare(cameraInstanceInfo);
			}
			if (pControl->GetRank() < CameraRank::Override)
			{
				first = false;
			}

			index++;
		}
	}

	// if there camera pair blended by dist-to-obj, blend their arc.
	{
		for (CameraNodeList::Iterator it = m_pCameraNodes->RBegin(); it != m_pCameraNodes->REnd(); --it)
		{
			CameraControlStrafeBase* pControl = CameraControlStrafeBase::FromProcess(it->m_handle.ToMutableProcess());
			if (!pControl)
				continue;

			if (!paused || (m_flags & CameraManager::kEnableUpdateWhenPaused) || pControl->GetRunWhenPaused())
			{
				// handle cameras blended by dist-to-obj as single group.
				if (!pControl->IsFadeInByDistToObj())
					continue;

				const float distWeight = pControl->GetDistWeight();
				if (distWeight < 0.f)
					continue;

				--it;

				CameraControlStrafeBase* pPrevControl = CameraControlStrafeBase::FromProcess(it->m_handle.ToMutableProcess());
				if (pPrevControl != nullptr)
				{
					const float pitchRad0 = pPrevControl->GetPitchRad();
					const float pitchRad1 = pControl->GetPitchRad();

					const float blendedPitchRad = Lerp(pitchRad0, pitchRad1, distWeight);

					pControl->AdjustByPitchRad(blendedPitchRad);
					pPrevControl->AdjustByPitchRad(blendedPitchRad);
				}
			}
		}
	}

	// phase 2: finalize.
	{
		bool first = true;

		for (CameraNodeList::Iterator it = m_pCameraNodes->RBegin(); it != m_pCameraNodes->REnd(); --it)
		{
			CameraControl* pControl = it->m_handle.ToMutableProcess();
			if (!pControl)
				continue;

			if (!paused || (m_flags & CameraManager::kEnableUpdateWhenPaused) || pControl->GetRunWhenPaused())
			{
				const CameraControl* pPrevControl = (it->Prev()) ? it->Prev()->m_handle.ToProcess() : nullptr;
				//const CameraControl* pNextControl = (it->Next()) ? it->Next()->m_handle.ToProcess() : nullptr;

				pControl->PrepareFinalize(first, pPrevControl);
			}
			if (pControl->GetRank() < CameraRank::Override)
			{
				first = false;
			}
		}
	}

	{
		const CameraControl* pControl = GetCurrentCamera();
		m_currCamReachPitchLimit = pControl != nullptr && pControl->IsReachingPitchLimit();
	}

	g_colRayCaster.ScheduleKickJob();
}

void CameraManager::UpdateCuts ()
{
	PROFILE(Camera, UpdateCuts);

	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);

	//if (g_debugAnimatedCamera && m_playerIndex == 0 && !EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
	//	MsgCamera(FRAME_NUMBER_FMT "UpdateCuts\n", FRAME_NUMBER);

	// reset this at the end of every frame
	ClearReevaluate();

	bool suppressJoypadDuringBlend = false;
	for (CameraNodeList::Iterator it = m_pCameraNodes->RBegin(); it != m_pCameraNodes->REnd(); --it)
	{
		const CameraControl* control = it->m_handle.ToProcess();
		if (control && control->GetNormalBlend() > kCameraManagerEpsilon && control->SuppressJoypadDuringBlend())
			suppressJoypadDuringBlend = true;
	}

	CameraJoypad joypad(GetJoypad());

	bool paused = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused();
	bool first = true;
	int index = 0;
	for (CameraNodeList::Iterator it = m_pCameraNodes->RBegin(); it != m_pCameraNodes->REnd(); --it)
	{
		CameraControl* pControl = it->m_handle.ToMutableProcess();
		const CameraControl* prevControl = (it->Prev()) ? it->Prev()->m_handle.ToProcess() : nullptr;
		const CameraControl* nextControl = (it->Next()) ? it->Next()->m_handle.ToProcess() : nullptr;

		if (pControl)
		{
			if	(!paused || (m_flags & CameraManager::kEnableUpdateWhenPaused) || pControl->GetRunWhenPaused())
			{
				CameraJoypad effectiveJoypad = GetBlendSuppressedJoypad(joypad, pControl, suppressJoypadDuringBlend);
				m_pLookAtController->AdjustCameraJoypad(effectiveJoypad);
				pControl->UpdateCuts(CameraInstanceInfo(pControl->GetCameraLocation(), first, effectiveJoypad, pControl->GetBlend(), pControl->GetLocationBlend(), pControl->GetNormalBlend(), prevControl, nextControl, index));
			}
			if (pControl->GetRank() < CameraRank::Override)
			{
				first = false;
			}
		}
		index++;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraManager::PrePlayerAnimUpdate()
{
	PROFILE(Camera, PrePlayerAnimUpdate);

	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);

	bool suppressJoypadDuringBlend = false;
	for (CameraNodeList::Iterator it = m_pCameraNodes->RBegin(); it != m_pCameraNodes->REnd(); --it)
	{
		const CameraControl* control = it->m_handle.ToProcess();
		if (control && control->GetNormalBlend() > kCameraManagerEpsilon && control->SuppressJoypadDuringBlend())
			suppressJoypadDuringBlend = true;
	}

	CameraJoypad joypad(GetJoypad());

	bool paused = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused();
	bool first = true;
	int index = 0;
	for (CameraNodeList::Iterator it = m_pCameraNodes->RBegin(); it != m_pCameraNodes->REnd(); --it)
	{
		CameraControl* pControl = it->m_handle.ToMutableProcess();
		const CameraControl* prevControl = (it->Prev()) ? it->Prev()->m_handle.ToProcess() : nullptr;
		const CameraControl* nextControl = (it->Next()) ? it->Next()->m_handle.ToProcess() : nullptr;

		if (pControl)
		{
			if	(!paused || (m_flags & CameraManager::kEnableUpdateWhenPaused) || pControl->GetRunWhenPaused())
			{
				CameraJoypad effectiveJoypad = GetBlendSuppressedJoypad(joypad, pControl, suppressJoypadDuringBlend);
				m_pLookAtController->AdjustCameraJoypad(effectiveJoypad);
				pControl->PrePlayerAnimUpdate(CameraInstanceInfo(pControl->GetCameraLocation(), first, effectiveJoypad, pControl->GetBlend(), pControl->GetLocationBlend(), pControl->GetNormalBlend(), prevControl, nextControl, index));
			}
			if (pControl->GetRank() < CameraRank::Override)
			{
				first = false;
			}
		}
		index++;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraManager::ReapCameras()
{
	// destroy dead cameras
	AtomicLockJanitorWrite jj(&m_cameraNodeLock, FILE_LINE_FUNC);

	for (CameraNodeList::Iterator it = m_pCameraNodes->Begin(); it != m_pCameraNodes->End(); )
	{
		const CameraControl* nextControl = (it->Next()) ? it->Next()->m_handle.ToProcess() : nullptr;
		if (it->IsDead(nextControl))
		{
			KillProcess(it->m_handle);
			it = m_pCameraNodes->ReleaseUsedNode(*it);
		}
		else
		{
			++it;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraManager::FreePersistentRequestUnused(CameraRequest* pRequest)
{
	ASSERT(pRequest);
	pRequest->Clear(); // very important now that requests can have extension objects
	m_pPersistentRequests->ReleaseUnusedNode(pRequest);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraManager::SetNearPlaneOverride(float nearPlaneDist, U32F priority)
{
	const I64 currFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;

	m_nearPlaneDistOverride.SetOverride(nearPlaneDist, priority, currFN);
	m_nearPlaneDistOverride.SetOverride(nearPlaneDist, priority, currFN + 1);
}

// -------------------------------------------------------------------------------------------------
void CameraManager::NearPlaneDistOverride::SetOverride(float nearPlaneDist, U32F priority, I64 frameNumber)
{
	Slot* pSlot = nullptr;

	bool found = false;
	// try find existing slot.
	for (int kk = 0; kk < 2; kk++)
	{
		if (m_slots[kk].m_frameNumberUnpaused == frameNumber)
		{
			if (m_slots[kk].m_priority < priority)
				pSlot = &m_slots[kk];

			found = true;
			break;
		}
	}

	if (!found)
	{
		pSlot = m_slots[0].m_frameNumberUnpaused < m_slots[1].m_frameNumberUnpaused ? &m_slots[0] : &m_slots[1];
		if (pSlot->m_frameNumberUnpaused > frameNumber)
			pSlot = nullptr;
	}

	if (pSlot != nullptr)
	{
		pSlot->m_value = nearPlaneDist;
		pSlot->m_priority = priority;
		pSlot->m_frameNumberUnpaused = frameNumber;
	}
}

// -------------------------------------------------------------------------------------------------
float CameraManager::NearPlaneDistOverride::Get() const
{
	const I64 currFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
	
	for (int kk = 0; kk < 2; kk++)
	{
		if (m_slots[kk].m_frameNumberUnpaused == currFN)
		{
			return m_slots[kk].m_value;
		}
	}

	return -1.f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraManager::CameraRequestList::Iterator CameraManager::FreePersistentRequest(CameraRequest* pRequest)
{
	ASSERT(pRequest);
	pRequest->Clear(); // very important now that requests can have extension objects
	return m_pPersistentRequests->ReleaseUsedNode(pRequest);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraManager::ReapPersistentCameraRequests(ReapAbandoned reapAbandoned)
{
	// Clean out any persistent requests that are no longer alive.

	CameraRequestList::Iterator it = m_pPersistentRequests->Begin();
	while (it != m_pPersistentRequests->End())
	{
		ALWAYS_ASSERT(*it);
		if (!it->m_isActive || (reapAbandoned == ReapAbandoned::kReap && it->m_isAbandoned))
		{
			it = FreePersistentRequest(*it);
		}
		else
		{
			++it;
		}
	}
}

void CameraManager::ReapPersistentCameraRequestsForAssociationId(CameraRequest::AssociationId id)
{
	// if our camera Id is the same as another, reap it if our associationIds don't match

	CameraRequestList::Iterator it = m_pPersistentRequests->Begin();
	while (it != m_pPersistentRequests->End())
	{
		ALWAYS_ASSERT(*it);
		if (!it->m_isActive || (it->m_associationId != id && it->m_associationId.m_id == id.m_id))
		{
			it = FreePersistentRequest(*it);
		}
		else
		{
			++it;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///

bool CameraManager::MaintainCameraForBlend() const
{
	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);

	for (CameraNodeList::Iterator it = m_pCameraNodes->Begin(); it != m_pCameraNodes->End(); ++it)
	{
		const CameraControl* pControl = it->m_handle.ToProcess();
		if (pControl && pControl->GetBlend() > 0.0f && pControl->MaintainCameraForBlend())
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraManager::InvalidateCameraWaterQueries()
{
	if (m_playerIndex == 0)
	{
		m_hAsyncWaterDetector.Invalidate();
		m_collWaterDetector.Reset();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraManager::ForceQueryWaterAt(Point_arg queryPos)
{
	if (m_playerIndex == 0)
	{
		GAMEPLAY_ASSERT(IsFinite(queryPos));
		m_forceQueryWaterPos = queryPos;
		m_forceQueryWaterFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused + 1;

		// need run g_waterMgr.WaitForAsyncCompute() and WaterQuery::GatherWaterQueriesCmp() before CameraManager::RunCameras() to get correct water surface.
		SettingSetDefault(&WaterQuery::g_bGatherWaterQueriesInNewPostAttachJob, false);
		SettingSetPers(SID("force-query-water-at"), &WaterQuery::g_bGatherWaterQueriesInNewPostAttachJob, true, kPlayerModePriority, 1.0f);
		WaterQuery::g_bGatherWaterQueriesInNewPostAttachJob = true;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraManager::RunCameras(const SceneWindow* pSceneWindow)
{
	PROFILE(Camera, RunCameras);

	const bool kFirst = true;
	const bool kNotBeforeCrossFade = false;
	const bool kReturnBeforeCrossFade = true;
	const bool kDontReturnBeforeCrossFade = false;

	CameraResult res;
	res.isManualOnTop = false;
	res.isNoManualUnderwater = false;

	// update regions using last frame's camera position. this is necessary because the scripts are going to poke rendering stuff and that must happen
	//  before we finalize this frame's camera position.
	m_pRegionControl->Update(m_pFinalInfo->GetPosition(), 0.0f);

	// update water displacement patch.
	//WaterPatchGatherQueries();
	CamWaterPlane waterPlane = GatherAsyncWaterDetector();
	m_waterPlane = waterPlane;

	RenderFrameParams* pRenderFrameParams = GetCurrentRenderFrameParams();
	pRenderFrameParams->m_cameraWaterPlane[ConvertToScreenIndex(m_playerIndex)] = waterPlane;

	CameraLocation oldNoManualLocator = m_noManualLocation;

	{
		// calculate the blends
		bool bManualInBlend = false;
		bool suppressJoypadDuringBlend = false;
		UpdateBlend(&bManualInBlend, &suppressJoypadDuringBlend);

		m_workingCamera.SetManual(bManualInBlend);
		m_workingCameraAfterCrossFade.SetManual(bManualInBlend);

		// update locations
		UpdateLocation(suppressJoypadDuringBlend);
	}

	DebugShowCameras();

	CameraNode* nodeTbl[kCameraManagerMaxCameras];
	I32F nodeCount = 0;
	{
		AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);
		for (CameraNodeList::Iterator it = m_pCameraNodes->RBegin(); it != m_pCameraNodes->REnd(); --it, ++nodeCount)
		{
			nodeTbl[nodeCount] = *it;
			GAMEPLAY_ASSERT(nodeTbl[nodeCount] != nullptr);
		}
	}

	//const RenderWindow* pRenderWindow = pSceneWindow->GetRenderWindow();
	const DC::RenderSettings* pRenderSettings = pSceneWindow->GetRenderSettings();

	//Size size = (pRenderWindow && pRenderWindow->m_rtPrimaryFloat) ? pRenderWindow->m_rtPrimaryFloat->GetSize() : Size(g_display.m_screenWidth, g_display.m_screenHeight);
	F32 aspectRatio = g_display.GetAspectRatio();
	F32 farCullDistance = pRenderSettings ? pRenderSettings->m_misc.m_farCullDistance : g_renderSettings[0].m_misc.m_farCullDistance;

	const bool bGamePaused = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused();

	bool bManualCamActive = false;
	const bool hasCrossFade = HasCrossFade(nodeTbl, nodeCount);

	if (nodeCount > 0)
	{
		// calculate the blended location
		CameraLocation locationBeforeCrossFade;
		CameraLocation locationAfterCrossFade;
		CameraLocation noManualLocation;
		Vector velocity = kInvalidVector;
		{
			PROFILE(Camera, UpdateCameraLocation);

			// nodeTbl[0] : the most recent camera (top of stack)
			// nodeTbl[1] : the next camera (top - 1 of stack)
			locationAfterCrossFade = BlendLocation(nodeTbl[0], &nodeTbl[1], nodeCount-1, kNotBeforeCrossFade, kDontReturnBeforeCrossFade);

			if (hasCrossFade)
			{
				locationBeforeCrossFade = BlendLocation(nodeTbl[0], &nodeTbl[1], nodeCount-1, kNotBeforeCrossFade, kReturnBeforeCrossFade);
			}

			if (kDebugCameraBlending)
				locationAfterCrossFade.DebugPrint();

			ALWAYS_ASSERT(IsFinite(locationAfterCrossFade.GetLocator()));

			m_noAdjustToWaterLocation = locationAfterCrossFade;

			// Push camera up or down a bit to ensure it can never be half above/half below the water surface (but not for manual cam)
			const CameraControl* nodeCtrl = nodeTbl[0]->m_handle.ToProcess();
			if (!nodeCtrl->IsManualCamera())
			{
				locationAfterCrossFade = AdjustToWaterSurface(*this, locationAfterCrossFade, waterPlane);
				ALWAYS_ASSERT(IsFinite(locationAfterCrossFade.GetLocator()));
				locationAfterCrossFade = AdjustToWind(*this, locationAfterCrossFade);
				ALWAYS_ASSERT(IsFinite(locationAfterCrossFade.GetLocator()));
				if (hasCrossFade)
				{
					locationBeforeCrossFade = AdjustToWaterSurface(*this, locationBeforeCrossFade, waterPlane);
					locationBeforeCrossFade = AdjustToWind(*this, locationBeforeCrossFade);
				}

				// Debug hack to give script function to override camera location
				if (EngineComponents::GetNdGameInfo()->m_debugCameraPositionEnable)
				{
					locationAfterCrossFade.SetLocator(EngineComponents::GetNdGameInfo()->m_debugCameraPosition);
					locationBeforeCrossFade.SetLocator(EngineComponents::GetNdGameInfo()->m_debugCameraPosition);
				}
			}

			// Calculate final blended velocity.
			{
				const CameraControl* pControl0 = nodeTbl[0]->m_handle.ToProcess();

				F32 finalBlend = 1.0f - pControl0->GetBlend();
				Vector v0 = pControl0->m_velocity;
				velocity = v0;
				if (nodeCount > 1)
				{
					const CameraControl* pControl1 = nodeTbl[1]->m_handle.ToProcess();
					Vector v1 = pControl1->m_velocity;
					if (!AllComponentsEqual(v0, kInvalidVector) && !AllComponentsEqual(v1, kInvalidVector))
					{
						velocity = Lerp(pControl0->m_velocity, pControl1->m_velocity, finalBlend);
					}
				}
			}

			noManualLocation = locationAfterCrossFade;
			Locator noManualLocator = noManualLocation.GetLocator();
			m_deltaLocator = noManualLocator.UntransformLocator(m_noManualLocation.GetLocator());
			m_noManualLocation = noManualLocation;
			if (!EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused() && !nodeCtrl->IsManualCamera())
			{
				// nope
				//m_deltaLocatorUnpaused = m_deltaLocator;

				// normalize to 1 second instead
				const float dt = EngineComponents::GetNdFrameState()->m_deltaTime;
				if (dt < 0.001f)
				{
					m_deltaLocatorUnpaused = Locator(kIdentity);
				}
				else
				{
					const float scale = 1.0f / (29.97f * dt);
					const Vector dPos = AsVector(m_deltaLocator.GetPosition()) * scale;
					Vec4 axis;
					float angle;
					m_deltaLocator.GetRotation().GetAxisAndAngle(axis, angle);
					const float dAngle = angle * scale;
					m_deltaLocatorUnpaused = Locator(AsPoint(dPos), QuatFromAxisAngle(Vector(axis), dAngle));
				}
			}

			if (nodeCount > 2)
			{
				if (!nodeCtrl->AffectGameCameraLocator())
				{
					const CameraLocation noManualLocationPreWaterAdjust = BlendLocation(nodeTbl[1], &nodeTbl[2], nodeCount-2, kNotBeforeCrossFade, kDontReturnBeforeCrossFade);
					m_noAdjustToWaterLocation = noManualLocationPreWaterAdjust;
					noManualLocation = AdjustToWind(*this, noManualLocation);
					noManualLocation = AdjustToWaterSurface(*this, noManualLocationPreWaterAdjust, waterPlane);
					m_noManualLocation = noManualLocation;
#ifndef FINAL_BUILD
					float heightDiff = Abs(m_noManualLocation.GetLocator().GetTranslation().Y() - noManualLocationPreWaterAdjust.GetLocator().GetTranslation().Y());

					// Debug draw the non adjusted camera semi transparent (only if different from adjusted position)
					if ((nodeCtrl->IsManualCamera(true)) && g_cameraOptions.m_drawCameraWhenInManualMode && heightDiff > 0.01f)
					{
						DebugDrawCamera(noManualLocationPreWaterAdjust, aspectRatio, kColorGreenTrans, kColorRedTrans, kPrimDuration1FrameNoRecord);
					}
#endif
				}
			}
		}

		// Camera Near Plane
		float nearCullDistance;
		bool useDebugNearPlaneDist = false;
		bool useOverrideNearPlaneDist = false;
		{
			if (g_cameraOptions.m_debugNearPlaneDist > 0.f)
			{
				nearCullDistance = g_cameraOptions.m_debugNearPlaneDist;
				useDebugNearPlaneDist = true;
			}
			else if (m_nearPlaneDistOverride.Get() > 0.f)
			{
				// Someone's overriding
				nearCullDistance = m_nearPlaneDistOverride.Get();
				useOverrideNearPlaneDist = true;
			}
			else
			{
				// No override, use the blended value
				nearCullDistance = locationAfterCrossFade.GetNearPlaneDist();
			}
		}

		// At this point, only the camera manager sets this render option
		m_nearPlaneDist = nearCullDistance;
		GAMEPLAY_ASSERT(m_nearPlaneDist > 0.0f);

		if (FALSE_IN_FINAL_BUILD(nodeCount > 0))
		{
			const CameraControl* nodeCtrl = nodeTbl[0]->m_handle.ToProcess();
			// Debug draw the final position after water adjustment
			if ((nodeCtrl->IsManualCamera(true)) && g_cameraOptions.m_drawCameraWhenInManualMode)
			{
				CameraLocation debugNoManualLocation = m_noManualLocation;
				if (useDebugNearPlaneDist || useOverrideNearPlaneDist)
					debugNoManualLocation.SetNearPlaneDist(m_nearPlaneDist);
				DebugDrawCamera(debugNoManualLocation, aspectRatio, kColorGreen, kColorRed, kPrimDuration1FrameNoRecord);
			}
		}

		// move the actual camera
		if (!g_renderOptions.m_3dSettingsOverwrite)
		{
			g_renderOptions.m_zeroPlaneDist = locationAfterCrossFade.GetZeroPlaneDist();
			g_renderOptions.m_distortion = locationAfterCrossFade.GetDistortion() * g_renderOptions.m_distortionMultiplier;
		}

		if (ConvertToScreenIndex(m_playerIndex) == 1)
		{
			aspectRatio = 3.0f / 4.0f;
		}
		m_workingCamera.Set(locationAfterCrossFade, aspectRatio, farCullDistance, m_nearPlaneDist, g_renderOptions.m_cameraHorizontal3DOffset);
		m_workingCameraAfterCrossFade.Set(locationBeforeCrossFade, aspectRatio, farCullDistance, m_nearPlaneDist, g_renderOptions.m_cameraHorizontal3DOffset);

		// setup the cross fade blend value
		if (hasCrossFade)
		{
			g_renderOptions.m_crossFadeFactor = GetCrossFadeBlend(nodeTbl, nodeCount);
		}
		else
		{
			g_renderOptions.m_crossFadeFactor = 0.0f;
		}

		{
			const CameraControl *pCurCamera = GetCurrentCamera(true);
			bManualCamActive = (pCurCamera && pCurCamera->IsManualCamera() && pCurCamera->GetNormalBlend() == 1.0f) ? true : false;
		}

		// Calculate final velocity.
		if (!bGamePaused &&
			!m_bCameraCutLastFrame &&
			!m_bCameraCutThisFrame &&
			!AllComponentsEqual(velocity, kInvalidVector) &&
			(EngineComponents::GetNdFrameState()->m_deltaTime > 0.0f) &&
			!CinematicManager::Get().IsCurrentCinematicSkippable())
		{
			// (jdl) HACK: Cameras can cut without actually using the cut mechanism.
			// This must be detected, otherwise we get crazy velocities for a few frames.
			// This hack should be removed once all the camera cut bugs are finally resolved.
			//
			// Assume the camera is never supposed to move faster than about 150m/s (max speed
			// in manual camera mode).  If it moves faster than this, treat it as a cut.
			if (LengthSqr(velocity) < SCALAR_LC(25000.0f))
			{
				m_velocity = velocity;

				m_workingCamera.SetVelocity(m_velocity);
				m_workingCameraAfterCrossFade.SetVelocity(m_velocity);
			}
			else
			{
				// Carry the previous frame's velocity forward until the cut is finished.
				m_bCameraCutThisFrame = true;
				OMIT_FROM_FINAL_BUILD(if (g_animOptions.m_debugCameraCuts) MsgCinematic(FRAME_NUMBER_FMT "CUT from camera velocity detected by CamMgr! (remove this!)\n", FRAME_NUMBER);)
			}
		}

		if (m_bCameraCutLastFrame || m_bCameraCutThisFrame)
		{
			// Alert the audio manager that the camera (i.e. the listener) is being teleported this frame.
			for (I32F iListener = 0; iListener < ARM_NUM_LISTENERS; iListener++)
			{
				// TODO: Should only teleport individual screens, not globally.
				EngineComponents::GetAudioManager()->ListenerTeleportThisFrame(iListener);
			}
			// Alert the waterMgr that the camera is being teleported this frame
			g_waterMgr.CameraTeleportedThisFrame();
			g_rainMgr.CameraTeleportedThisFrame();
		}

		if (!bGamePaused)
		{
			if (FALSE_IN_FINAL_BUILD(g_animOptions.m_debugCameraCuts || g_animOptions.m_debugCameraCutsVerbose || g_animOptions.m_debugCameraCutsSuperVerbose) && m_playerIndex == 0)
			{
				StringBuilder<256> sb(FRAME_NUMBER_FMT "CAMERA CUT state: N-1: %s N: %s N+1: %s N+2: %s N+3: %s | SUPPRESS DISPLAY state: N: %s N+1: %s\n",
					FRAME_NUMBER,
					m_bCameraCutLastFrame ? "YES" : "   ",
					m_bCameraCutThisFrame ? "YES" : "   ",
					m_bCameraCutNextFrame ? "YES" : "   ",
					m_bCameraCutIn2Frames ? "YES" : "   ",
					m_bCameraCutIn3Frames ? "YES" : "   ",
					m_bSuppressThisFrameDisplay ? "YES" : "   ",
					m_bSuppressNextFrameDisplay ? "YES" : "   ");

				MsgConPauseable(sb.c_str());

				if (m_bCameraCutLastFrame || m_bCameraCutThisFrame || m_bCameraCutNextFrame || m_bCameraCutIn2Frames || m_bCameraCutIn3Frames || m_bSuppressThisFrameDisplay || m_bSuppressNextFrameDisplay)
					MsgCinematic(sb.c_str());
			}

			m_bCameraCutLastFrame = m_bCameraCutThisFrame;
			m_bCameraCutThisFrame = m_bCameraCutNextFrame;
			m_bCameraCutNextFrame = m_bCameraCutIn2Frames;
			m_bCameraCutIn2Frames = m_bCameraCutIn3Frames;
			m_bCameraCutIn3Frames = false;

			m_bSuppressThisFrameDisplay = m_bSuppressNextFrameDisplay;
			m_bSuppressNextFrameDisplay = false;

			OMIT_FROM_FINAL_BUILD(if (g_animOptions.m_debugCameraCutsSuperVerbose) MsgCinematic(FRAME_NUMBER_FMT "SHIFTED: m_bCameraCut[-1,0,1,2] = [%d,%d,%d,%d]  <--\n",
				FRAME_NUMBER, m_bCameraCutLastFrame, m_bCameraCutThisFrame, m_bCameraCutNextFrame, m_bCameraCutIn2Frames);)
		}

		if (FALSE_IN_FINAL_BUILD(true))
		{
			// the great thing about using history graph is, even flag is off, it saves the history.
			I32 debugFlags = 0;
			if (bGamePaused && g_cameraOptions.m_drawCameraTrajectory)
				debugFlags = DebugPosVecPlot::kDebugPosVecDebugVec | DebugPosVecPlot::kDebugPosVecDisplayOnly;
			else if (!bGamePaused && g_cameraOptions.m_drawCameraTrajectory)
				debugFlags = DebugPosVecPlot::kDebugPosVecDebugVec;
			else if (!bGamePaused && !g_cameraOptions.m_drawCameraTrajectory)
				debugFlags = DebugPosVecPlot::kDebugPosVecAddValueOnly;

			if (debugFlags != 0)
			{
				const Locator loc = m_noManualLocation.GetLocator();
				DebugMemoryPosPlot("cameraTrajectory", DebugPosVecValue(loc.GetTranslation(), GetLocalZ(loc.GetRotation())), &m_debugCameraTrajectoryPlot, 120, kColorBlue, kColorBlue, debugFlags);
			}
		}

		if (FALSE_IN_FINAL_BUILD(m_flags & kDebugShowLocator))
		{
			Point p = m_pFinalInfo->GetPosition();
			Quat q = m_pFinalInfo->GetRotation();
			MsgCon("Camera Locator: %s\n", PrettyPrint(m_pFinalInfo->GetLocator()));
		}

		if (FALSE_IN_FINAL_BUILD(m_flags & kDebugShowRotationSpeed))
		{
			if (EngineComponents::GetNdFrameState()->m_deltaTime > 0.f)
			{
				// assume these directions are normalized
				const Vector oldCamDir = GetLocalZ(oldNoManualLocator.GetLocator().GetRotation());
				const Vector newCamDir = GetLocalZ(m_noManualLocation.GetLocator().GetRotation());
				const float dotProd = Dot(oldCamDir, newCamDir);
				const float deltaAngleRad = Acos(MinMax(dotProd, -1.f, 1.f));
				const float deltaAngleDeg = RADIANS_TO_DEGREES(deltaAngleRad);
				const float rotationSpeed = deltaAngleDeg / EngineComponents::GetNdFrameState()->m_deltaTime;
				MsgCon("final camera rotation speed: %.4f degrees/sec\n", rotationSpeed);
			}
		}

		if (FALSE_IN_FINAL_BUILD(g_cameraOptions.m_debugCameraMatching))
		{
			DebugPrimTime tt = kPrimDuration1FramePauseable;
			const Point dbgCamPos = m_noManualLocation.GetLocator().GetTranslation();
			const Vector dbgCamDir = GetLocalZ(m_noManualLocation.GetLocator().GetRotation());
			g_prim.Draw(DebugLine(dbgCamPos, dbgCamDir * 100.f, kColorMagenta, 1.f, PrimAttrib(kPrimEnableHiddenLineAlpha)), tt);
		}

		// Debug near-plane dist
		if (FALSE_IN_FINAL_BUILD(g_cameraOptions.m_drawNearPlaneDist))
		{
			const bool isOverridden		= (m_nearPlaneDist != locationAfterCrossFade.GetNearPlaneDist());

			const char *blendType = (useDebugNearPlaneDist ? "debug" : (bManualCamActive ? "manual"     : (isOverridden ? "overridden" : "blended"  )));
			const Color &msgColor = (useDebugNearPlaneDist ? kColorWhite : (bManualCamActive ? kColorWhite  : (isOverridden ? kColorOrange : kColorGreen)));

			SetColor(kMsgCon, msgColor.ToAbgr8());
			MsgCon("\nNear-plane dist = %.2f (%s)\n", m_nearPlaneDist, blendType);
			SetColor(kMsgCon, kColorWhite.ToAbgr8());
		}

		// Update camera shake stuff
		CameraShake::Update(m_playerIndex);
	}

	//UpdateTheSnapshot
	ManuallyRefreshSnapshot();

	res.isManualOnTop = bManualCamActive;
	res.isNoManualUnderwater = m_pCustom ? m_pCustom->IsCamUnderwater() : false;

	if (FALSE_IN_FINAL_BUILD(g_cameraOptions.m_debugCameraAndWater))
	{
		if (res.isNoManualUnderwater)
			MsgConPauseable("no-manual-is-underwater? true\n");
		else
			MsgConPauseable("no-manual-is-underwater? false\n");
	}

	m_cameraResult = res;
}

/// --------------------------------------------------------------------------------------------------------------- ///

bool CameraManager::ShouldShowCameras() const
{
	if (m_flags & kDebugShowCamerasShort)
		return false;

	if (!(m_flags & kDebugShowCameras))
		return false;

	//if (EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
//		return false;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::DebugShowCameras() const
{
	STRIP_IN_FINAL_BUILD;

	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);

	if ((m_flags & kDebugShowOverlay) || (m_flags & kDebugShowOverlayDebug))
	{
		bool debug = (m_flags & kDebugShowOverlayDebug) != 0;
		StringId64 currRemap = DebugCameraRemap(m_playerIndex, debug);
		MsgCon("---------------------------------\n");
		MsgCon("Current Camera Remap: %s\n", DevKitOnly_StringIdToString(currRemap));
		MsgCon("---------------------------------\n");
	}

	const bool showCameras = ShouldShowCameras();
	const bool showShortCamera = (m_flags & kDebugShowCamerasShort) && (m_flags & kDebugShowCameras);
	const bool showRequestFunc = (m_flags & kDebugShowRequestFunc);
	const bool printCameraLocator = (m_flags & kDebugShowCameraLocator);

	if (showCameras)
	{
		char s_aRunningChars[] = { '/', '|', '\\', '-' };
		MsgCon("Active Camera Stack For Player #%d %c\n", m_playerIndex, s_aRunningChars[EngineComponents::GetNdFrameState()->m_gameFrameNumber % ARRAY_ELEMENT_COUNT(s_aRunningChars)]);
		MsgCon("---------------------------------\n");
		char text[1024];
		int len = 0;
		len += snprintf(text + len, sizeof(text) - len, "                Camera Name    PID#  Blending Information                   ");
		if (printCameraLocator)
		{
			len += snprintf(text + len, sizeof(text) - len, " Position                  Rotation                   ");
		}
		len += snprintf(text + len, sizeof(text) - len, " FoV   Near  Dstr  Zero\n");

		MsgCon(text);
		MsgCon("------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
	}
	else if (showShortCamera)
	{
		MsgCon("          Camera Name    Blend  Time        FoV  Settings\n");
		MsgCon("---------------------------------------------------------\n");
	}

	for (CameraNodeList::Iterator it = m_pCameraNodes->Begin(); it != m_pCameraNodes->End(); ++it)
	{
		const CameraNode* pNode = *it;
		const CameraControl* pControl = pNode->m_handle.ToMutableProcess();
		if (pControl)
		{
			CameraControl::DebugShowParams debugShowParams;
			debugShowParams.showRequestFunc = showRequestFunc;
			debugShowParams.printLocator = printCameraLocator;

			if (showCameras)
			{
				pControl->DebugShow(false, debugShowParams);
			}
			else if (showShortCamera && !pControl->IsManualCamera())
			{
				pControl->DebugShow(true, debugShowParams);
			}

			if (m_flags & (kDebugDrawCameras | kGameDebugDrawCameras))
				pControl->DebugDraw();
		}
	}

	if (showCameras)
	{
		MsgCon("------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::PostRenderUpdate()
{
	Point position1(m_pFinalInfo->GetLocator().GetTranslation());

	if (FALSE_IN_FINAL_BUILD( g_cameraDrawRegions ))
	{
		g_regionManager.DebugDraw(position1, 0.0f, SID("camera"));
	}
	if (FALSE_IN_FINAL_BUILD( g_cameraScriptDrawRegions ))
	{
		g_regionManager.DebugDraw(position1, 0.0f, SID("camera-script"));
	}

	DeciPublishCameraLocation();
}

void CameraManager::DeciPublishCameraLocation(bool forcePublish) const
{
	STRIP_IN_FINAL_BUILD;

#if !defined(FINAL_BUILD)
	static U32 frame = 0; // throttle camera updates to 10 fps to avoid flooding the tools
	if (forcePublish || (g_broadcastCameraLocator && m_playerIndex == 0 && (frame++ % 6) == 0))
	{
		const Mat44 mat = m_pFinalInfo->GetLocator().AsTransform().GetRawMat44();
		float fov = m_pFinalInfo->GetFov();
		static Mat44 lastMat;
		static float lastFov = 70.0f;

		if (forcePublish || g_cameraBroadcastDelay <= 0)
		{
			Mat44 matChange = mat - lastMat;
			Vec4 eps(0.01f, 0.01f, 0.01f, 0.01f);

			bool same = !forcePublish &&
				AllComponentsLessThan(matChange.GetRow(0), eps) && AllComponentsGreaterThan(matChange.GetRow(0), -eps) &&
				AllComponentsLessThan(matChange.GetRow(1), eps) && AllComponentsGreaterThan(matChange.GetRow(1), -eps) &&
				AllComponentsLessThan(matChange.GetRow(2), eps) && AllComponentsGreaterThan(matChange.GetRow(2), -eps) &&
				AllComponentsLessThan(matChange.GetRow(3), eps) && AllComponentsGreaterThan(matChange.GetRow(3), -eps) &&
				fov - lastFov > -0.01f && fov - lastFov < 0.01f;

			// if nothing changed, no need to broadcast.
			if (!same || forcePublish)
			{
				char msg[256];
				snprintf(msg, sizeof(msg)-1, "cameraMatrix %d %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f",
					frame,
					(float)mat[0][0], (float)mat[0][1], (float)mat[0][2], (float)mat[0][3],
					(float)mat[1][0], (float)mat[1][1], (float)mat[1][2], (float)mat[1][3],
					(float)mat[2][0], (float)mat[2][1], (float)mat[2][2], (float)mat[2][3],
					(float)mat[3][0], (float)mat[3][1], (float)mat[3][2], (float)mat[3][3],
					fov);

				g_commandServer.Publish(msg);
			}
		}
		else
		{
			// count down until we should start publishing camera updates again.
			g_cameraBroadcastDelay--;
		}

		lastMat = mat;
		lastFov = fov;
	}
#endif
}


/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::ResetActors()
{
	//UsePad(m_inputPadIndex); // my state is inconsistent after ResetActors, so fix it
	ClearAllPersistentCameras();
	ClearAllGameCameras();
	m_pRegionControl->Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraManager::ResetGameCameras(bool dontClearPersistentCams)
{
	if (!dontClearPersistentCams)
	{
		ClearAllPersistentCameras();
		ClearAllGameCameras();
	}
	m_blendInfoOverride[kBlendInfoPriHigh] = Seconds(0.0f);
	m_bStartingFirstCamera = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
///  New Camera Interface
/// --------------------------------------------------------------------------------------------------------------- ///
CameraId CameraManager::GetCurrentCameraId(bool includeTopCamera) const
{
	const CameraControl* pCamera = GetCurrentCamera(includeTopCamera);
	if (pCamera)
	{
		return pCamera->GetCameraId();
	}
	return kCameraNone; //@kCameraNone
}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraControl* CameraManager::GetCurrentCameraMutable(bool includeTopCamera) const
{
	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);

	const CameraNodeList* pCameras = m_pCameraNodes;
	for (CameraNodeList::ConstIterator it = pCameras->RBegin(); it != pCameras->REnd(); --it)
	{
		CameraControl* pCamera = it->m_handle.ToMutableProcess();
		if (pCamera
		&&	(pCamera->GetRank() != CameraRank::Bottom) // never include the bottom camera
		&&	(includeTopCamera || (pCamera->GetRank() != CameraRank::Debug && pCamera->GetRank() != CameraRank::Override)))
		{
			return pCamera;
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraControl* CameraManager::GetCurrentCameraIncludingOverride() const
{
	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);

	const CameraNodeList* pCameras = m_pCameraNodes;
	for (CameraNodeList::ConstIterator it = pCameras->RBegin(); it != pCameras->REnd(); --it)
	{
		CameraControl* pCamera = it->m_handle.ToMutableProcess();
		if (pCamera
		&&	(pCamera->GetRank() != CameraRank::Bottom) // never include the bottom camera
		&&	(pCamera->GetRank() != CameraRank::Debug))
		{
			return pCamera;
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraControl* CameraManager::GetPreviousCamera(const CameraControl* pCameraControl) const
{
	if (!pCameraControl)
		return nullptr;

	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);

	bool foundMe = false;

	const CameraNodeList* pCameras = m_pCameraNodes;
	for (CameraNodeList::ConstIterator it = pCameras->RBegin(); it != pCameras->REnd(); --it)
	{
		CameraControl* pCamera = it->m_handle.ToMutableProcess();
		if (foundMe)
			return pCamera;

		if (pCamera == pCameraControl)
			foundMe = true;
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const CameraControl* CameraManager::GetNextCameraNoLock(const CameraControl* pCameraControl) const
{
	if (!pCameraControl)
		return nullptr;

	bool foundMe = false;

	const CameraNodeList* pCameras = m_pCameraNodes;
	for (CameraNodeList::ConstIterator it = pCameras->Begin(); it != pCameras->REnd(); ++it)
	{
		CameraControl* pCamera = it->m_handle.ToMutableProcess();
		if (foundMe)
			return pCamera;

		if (pCamera == pCameraControl)
			foundMe = true;
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const CameraControl* CameraManager::GetNextCamera(const CameraControl* pCameraControl) const
{
	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);
	return GetNextCameraNoLock(pCameraControl);
}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraControl* CameraManager::CameraGetByName(StringId64 nameId)
{
	CameraRequestList::Iterator it = m_pPersistentRequests->RBegin();
	for (; it != m_pPersistentRequests->REnd(); --it)
	{
		const CameraRequest* pRequest = *it;
		ASSERT(pRequest);

		// always update the newest camera with the same name. (ignore camera-index here.)
		if (pRequest->m_associationId.m_id == nameId)
		{
			return pRequest->m_hCamera.ToMutableProcess();
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const CameraControl* CameraManager::FindCamera(FindCameraFunc* pFunc, uintptr_t userData) const
{
	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);

	const CameraNodeList* pCameras = m_pCameraNodes;
	for (CameraNodeList::ConstIterator it = pCameras->RBegin(); it != pCameras->REnd(); --it)
	{
		const CameraControl* pCamera = it->m_handle.ToProcess();
		if (pCamera
		&&	(pCamera->GetRank() != CameraRank::Bottom) // never include the bottom camera
		&&  (*pFunc)(pCamera, userData))
		{
			return pCamera;
		}
	}
	return nullptr;
}

//----------------------------------------------------------------------------------------------------------//
bool CameraManager::IsInputDisabledWhenCameraFadeOutBlended() const
{
	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);

	const CameraNodeList* pCameras = m_pCameraNodes;
	for (CameraNodeList::ConstIterator it = pCameras->RBegin(); it != pCameras->REnd(); --it)
	{
		const CameraControl *pControl = it->m_handle.ToProcess();
		const CameraControl* pNextControl = (it->Next()) ? it->Next()->m_handle.ToProcess() : nullptr;
		if (pControl != nullptr)
		{
			if (pControl->IsInputDisabledWhenFadeOut(pNextControl, false))
			{
				return true;
			}
		}
	}

	return false;
}

const CameraControl* CameraManager::GetTopmostCameraById(CameraId cameraId) const
{
	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);

	const CameraNodeList* pCameras = m_pCameraNodes;
	for (CameraNodeList::ConstIterator i = pCameras->RBegin(); i != pCameras->REnd(); --i)
	{
		const CameraControl* pCamera = i->m_handle.ToProcess();
		if (pCamera && pCamera->GetCameraId() == cameraId)
		{
			return pCamera;
		}
	}

	return nullptr;
}

const CameraControl* CameraManager::GetTopmostCameraByKind(const TypeFactory::Record &kind) const
{
	AtomicLockJanitorRead jj(&m_cameraNodeLock, FILE_LINE_FUNC);

	const CameraNodeList* pCameras = m_pCameraNodes;
	for (CameraNodeList::ConstIterator i = pCameras->RBegin(); i != pCameras->REnd(); --i)
	{
		const CameraControl* pCamera = i->m_handle.ToProcess();
		if (pCamera && pCamera->IsKindOf(kind))
		{
			return pCamera;
		}
	}

	return nullptr;
}

bool CameraManager::IsCurrentCameraReachingPitchLimit() const
{
	return m_currCamReachPitchLimit;
}

CameraControl* CameraManager::RequestCamera(const char* file, U32 line, const char* func,
											CameraId cameraId,
											const CameraBlendInfo* pBlendInfo,
											const CameraStartInfo* pStartInfo)
{
	{
		// we never have multiple camera request concurrently, but right now it crashes when jeep fall off.
		AtomicLockJanitor jj(&m_cameraRequestLock, FILE_LINE_FUNC);

		ReapPersistentCameraRequests(ReapAbandoned::kDontReap);

		m_currentRequest.Init(cameraId, CameraRequest::AssociationId(), pBlendInfo, pStartInfo, GetDefaultFocusObject());

		m_currentRequest.m_file = file;
		m_currentRequest.m_line = line;
		m_currentRequest.m_func = func;

		if (m_pCustom)
		{
			m_pCustom->OnRequestCamera(*this, m_currentRequest, false);
		}
	}

	// Now re-evaluate all camera requests to see if this one should be pushed.
	return EvaluateCameraRequests();
}

bool CameraManager::RequestPersistentCamera(CameraId cameraId, CameraRequest::AssociationId associationId, const CameraBlendInfo* pBlendInfo, const CameraStartInfo* pStartInfo)
{
	// JQG TO DO
	//if (g_disablePersistentCameras)
	//	return false;

	CameraRequest* pRequest = m_pPersistentRequests->GetFreeNode();
	if (!pRequest)
		return false; // failure!

	NdGameObject* pDefaultFocusObj = (pStartInfo && pStartInfo->m_noFocusObj) ? nullptr : GetDefaultFocusObject();
	pRequest->Init(cameraId, associationId, pBlendInfo, pStartInfo, pDefaultFocusObj);

	bool allowRequest = true;
	if (m_pCustom)
	{
		allowRequest = m_pCustom->OnRequestCamera(*this, *pRequest, true);
	}

	// Make sure this camera isn't already on the stack
	CameraRequestList::Iterator it = m_pPersistentRequests->End();
	while (it != m_pPersistentRequests->Begin())
	{
		--it;
		if (it->GetPriority() == pRequest->GetPriority() && it->GetAssociationId() == associationId)
		{
			// the camera already exists with the same priority, don't add another one
			FreePersistentRequestUnused(pRequest);
			EvaluateCameraRequests();
			return true;
		}
	}

	if (!allowRequest)
	{
		pRequest->Clear();
		FreePersistentRequestUnused(pRequest);
		return true;
	}

	// Now insert the request into the list in priority order.
	it = m_pPersistentRequests->End();
	CameraRequest* pInsertNode = *it;
	while (it != m_pPersistentRequests->Begin())
	{
		--it;
		if (it->GetPriority() <= pRequest->GetPriority())
		{
			pInsertNode = *it;
		}
	}
	pRequest->InsertBefore(pInsertNode);

	// OK, if the request we just pushed is now the best NON-ABANDONED request, then clear
	// all abandoned cameras... but if there's a higher-priority request already on the
	// stack (i.e. this new request wouldn't have changed anything anyway), then let the
	// abandoned camera(s) stand.
	CameraRequest* pBestNonAbandonedRequest = GetBestPersistentRequest(ExcludeAbandoned::kExclude);
	ReapAbandoned reapAbandoned = (pBestNonAbandonedRequest == pRequest) ? ReapAbandoned::kReap : ReapAbandoned::kDontReap;
	ReapPersistentCameraRequests(reapAbandoned);
	ReapPersistentCameraRequestsForAssociationId(pRequest->m_associationId);

	if (g_debugAnimatedCamera && m_playerIndex == 0 && !EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
		MsgCamera(FRAME_NUMBER_FMT "RequestPersistentCamera\n", FRAME_NUMBER);

	// Finally, re-evaluate all camera requests to see if this one should be pushed.
	EvaluateCameraRequests();

	return true;
}

bool CameraManager::AbandonPersistentCamera(StringId64 nameId)
{
	CameraRequestList::Iterator it = m_pPersistentRequests->RBegin();
	for ( ; it != m_pPersistentRequests->REnd(); --it)
	{
		ASSERT(*it);
		// abandon the oldest camera with the same name (ignore camera-index here), since camera-enable and camera-abandon are supposed to be pairs.
		if (!it->m_isAbandoned && it->m_associationId.m_id == nameId)
		{
			it->m_isAbandoned = true;
			return true;
		}
	}
	return false;
}

void CameraManager::AbandonAllPersistentCameras()
{
	CameraRequestList::Iterator it = m_pPersistentRequests->Begin();
	for ( ; it != m_pPersistentRequests->End(); ++it)
	{
		ASSERT(*it);
		it->m_isAbandoned = true;
	}
}

static void DisablePersistentCameraRequest(CameraRequest* pRequest, 
										   const CameraBlendInfo* pBlendInfo, 
										   const AutoGenScriptParams* pAutoGenScriptParams, 
										   const Vector* pNextCameraInitDir,
										   bool disableNextCameraInputUntil70,
										   CameraManager* pMgr)
{
	pRequest->m_isActive = false;
	if (CameraControl* pCamera = pRequest->m_hCamera.ToMutableProcess())
	{
		if (pCamera->GetRank() == CameraRank::Override)
		{
			CameraBlendInfo blendInfo = pBlendInfo ? *pBlendInfo : CameraBlendInfo(Seconds(0.0f));
			pCamera->End(blendInfo.m_time, blendInfo.m_type);

			if (blendInfo.m_time == Seconds(0.0f))
			{
				pMgr->NotifyCameraCutIn2Frames(FILE_LINE_FUNC); //@CAMCUT why 2 frames? is this correct anymore?
			}
		}

		if (pAutoGenScriptParams != nullptr)
		{
			CameraControlAnimated* pCameraAnimated = CameraControlAnimated::FromProcess(pCamera);
			if (pCameraAnimated != nullptr)
			{
				pCameraAnimated->SetNextCameraAutoGen(*pAutoGenScriptParams);
				float fadeOutTime = pBlendInfo ? pBlendInfo->m_time.ToSeconds() : 0.f;
				pCameraAnimated->SetToAutoGenFadeOutTime(fadeOutTime);
			}
		}
		else if (pNextCameraInitDir != nullptr)
		{
			CameraControlAnimated* pCameraAnimated = CameraControlAnimated::FromProcess(pCamera);
			if (pCameraAnimated != nullptr)
			{
				pCameraAnimated->SetNextCameraInitDir(*pNextCameraInitDir);
			}
		}

		if (disableNextCameraInputUntil70)
		{
			CameraControlAnimated* pCameraAnimated = CameraControlAnimated::FromProcess(pCamera);
			if (pCameraAnimated != nullptr)
			{
				pCameraAnimated->SetNextCameraInputDisabledUntil70(true);
			}
		}
	}
}

bool CameraManager::DisablePersistentCamera(StringId64 nameId, 
											const CameraBlendInfo* pBlendInfo, 
											const AutoGenScriptParams* pAutoGenScriptParams, 
											const Vector* pNextCameraInitDir,
											bool disableNextCameraInputUntil70)
{
	CameraRequestList::Iterator it = m_pPersistentRequests->RBegin();
	for ( ; it != m_pPersistentRequests->REnd(); --it)
	{
		// disable the oldest camera with the same name (ignore camera-index here), since camera-enable and camera-disable/camera-fade-out are supposed to be pairs.
		if (it->m_isActive && it->m_associationId.m_id == nameId)
		{
			DisablePersistentCameraRequest(*it, pBlendInfo, pAutoGenScriptParams, pNextCameraInitDir, disableNextCameraInputUntil70, this);
			EvaluateCameraRequests(pBlendInfo);
			return true;
		}
	}
	return false;
}

void CameraManager::DisableAllPersistentCameras(const CameraBlendInfo* pBlendInfo)
{
	CameraRequestList::Iterator it = m_pPersistentRequests->Begin();
	for ( ; it != m_pPersistentRequests->End(); ++it)
	{
		DisablePersistentCameraRequest(*it, pBlendInfo, nullptr, nullptr, false, this);
	}

	EvaluateCameraRequests(pBlendInfo);
}

void CameraManager::SetPersistentCameraDormant(CameraRequest& request, bool dormant, const CameraBlendInfo* pBlendInfo)
{
	const CameraBlendInfo blendInfo = pBlendInfo ? *pBlendInfo : CameraBlendInfo();

	bool wasDormant = request.m_isDormant;
	request.m_isDormant = dormant;

	if (dormant != wasDormant)
	{
		if (dormant)
		{
			// just went dormant
			if (CameraControl* pCamera = request.m_hCamera.ToMutableProcess())
			{
				if (pCamera->GetRank() == CameraRank::Override)
				{
					pCamera->End(blendInfo.m_time, blendInfo.m_type);
					if (blendInfo.m_time == Seconds(0.0f))
					{
						NotifyCameraCutIn2Frames(FILE_LINE_FUNC); // @CAMCUT again why 2 frames? correct?
					}
				}
			}
		}
	}
}

void CameraManager::ClearAllPersistentCameras()
{
	CameraRequestList::Iterator it = m_pPersistentRequests->Begin();
	while (it != m_pPersistentRequests->End())
	{
		it = FreePersistentRequest(*it);
	}
}

void CameraManager::ClearAllGameCameras()
{
	AtomicLockJanitorWrite jj(&m_cameraNodeLock, FILE_LINE_FUNC);
	for (CameraNodeList::Iterator it = m_pCameraNodes->Begin(); it != m_pCameraNodes->End(); )
	{
		const CameraControl* pControl = it->m_handle.ToProcess();
		if (pControl && pControl->GetRank() != CameraRank::Debug && pControl->GetRank() != CameraRank::Bottom)
		{
			KillProcess(it->m_handle);
			it = m_pCameraNodes->ReleaseUsedNode(*it);
		}
		else
		{
			++it;
		}
	}
}

bool CameraManager::IsRequestValid(const CameraRequest& persistentRequest, IncludeDormant includeDormantCameras)
{
	if (persistentRequest.m_isActive && (includeDormantCameras == IncludeDormant::kInclude || !persistentRequest.m_isDormant))
	{
		if (m_pCustom)
		{
			const CameraRequest* pCurrentRequest = (m_currentRequest.m_isActive)
													? &m_currentRequest
													: nullptr;

			return m_pCustom->IsPersistentRequestValid(*this, persistentRequest, pCurrentRequest);
		}
		return true;
	}
	return false;
}

CameraRequest* CameraManager::GetBestPersistentRequest(ExcludeAbandoned excludeAbandonedCameras, const CameraBlendInfo* pCallerBlendInfo)
{
	CameraRequestList::Iterator it = m_pPersistentRequests->Begin();
	for ( ; it != m_pPersistentRequests->End(); ++it)
	{
		CameraRequest* pRequest = *it;
		ASSERT(pRequest);
		if (excludeAbandonedCameras == ExcludeAbandoned::kInclude || !pRequest->m_isAbandoned)
		{
			if (pRequest->m_pExtension)
			{
				if (g_debugAnimatedCamera && m_playerIndex == 0 && !EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
					MsgCamera(FRAME_NUMBER_FMT "UpdateCameraRequest\n", FRAME_NUMBER);

				pRequest->m_pExtension->UpdateCameraRequest(*this, *pRequest);

				if (pRequest->m_pExtension->ShouldReevaluateCameraRequests(*this, *pRequest))
				{
					m_reevaluateCameraRequests = true;
					m_reevaluateBlendInfo = pCallerBlendInfo ? *pCallerBlendInfo : CameraBlendInfo();

					if (g_debugAnimatedCamera && m_playerIndex == 0 && !EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
						MsgCamera(FRAME_NUMBER_FMT "m_reevaluateCameraRequests = true;\n", FRAME_NUMBER);
				}
			}

			if (IsRequestValid(*pRequest, IncludeDormant::kExclude))
				return pRequest;
		}
	}
	return nullptr;
}

// not needed, and has some ramifications for animated cameras, so let's not implement unless it's really needed
//bool CameraManager::IsPersistentCameraRequested()
//{
//	CameraRequest* pBestRequest = GetBestPersistentRequest(kIncludeAbandoned);
//	return (pBestRequest != nullptr);
//}

CameraRequest* CameraManager::FindPersistentCameraRequest(FindCameraRequestFunc* pFunc, uintptr_t userData, ExcludeAbandoned excludeAbandonedCameras, IncludeDormant includeDormantCameras)
{
	AtomicLockJanitor jj(&m_cameraRequestLock, FILE_LINE_FUNC);

	if (pFunc)
	{
		CameraRequestList::Iterator it = m_pPersistentRequests->Begin();
		for ( ; it != m_pPersistentRequests->End(); ++it)
		{
			CameraRequest* pRequest = *it;
			ASSERT(pRequest);
			if ((excludeAbandonedCameras == ExcludeAbandoned::kInclude || !pRequest->m_isAbandoned) && IsRequestValid(*pRequest, includeDormantCameras))
			{
				if (pFunc(pRequest, userData))
					return pRequest;
			}
		}
	}

	return nullptr;
}

void CameraManager::ReevaluateCameraRequests()
{
	if (g_debugAnimatedCamera && m_playerIndex == 0 && !EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
		MsgCamera(FRAME_NUMBER_FMT "*** ReevaluateCameraRequests: %s ***\n", FRAME_NUMBER, m_reevaluateCameraRequests ? "true" : "false");

	if (m_reevaluateCameraRequests)
		EvaluateCameraRequests(&m_reevaluateBlendInfo);

	ClearReevaluate();
}

CameraControl* CameraManager::EvaluateCameraRequests(const CameraBlendInfo* pCallerBlendInfo)
{
	PROFILE_AUTO(Camera);
	AtomicLockJanitor jj(&m_cameraRequestLock, FILE_LINE_FUNC);

	if (m_pCameraNodes->Count() == m_pCameraNodes->AllocatedSize())
	{
		if (FALSE_IN_FINAL_BUILD(!(m_flags & (kDebugShowCameras | kDebugShowRequestFunc))))
		{
			SetColor(kMsgConPersistent, kColorRed.ToAbgr8());
			MsgConPersistent("Camera Manager Overflow! Please Screen Cap this debug and send it to Eli or Kan.\n");
			SetColor(kMsgConPersistent, kColorWhite.ToAbgr8());
			m_flags |= kDebugShowCameras | kDebugShowRequestFunc;
		}

#ifndef FINAL_BUILD
		if (g_cameraOptions.m_pauseGameOnCameraStackOverflow)
			g_ndConfig.m_pDMenuMgr->SetProgPauseDebugImmediate();
#endif

		return nullptr;
	}

	CameraControl* pNewCamera = nullptr;
	CameraControl* pCurCamera = GetCurrentCameraMutable(false);
	CameraControlAnimated* pCurCamAnimated = CameraControlAnimated::FromProcess(pCurCamera);
	const CameraControl* pPrevCamera = GetPreviousCamera(pCurCamera);
	CameraRequest* pPersistentRequest = GetBestPersistentRequest(ExcludeAbandoned::kInclude, pCallerBlendInfo);
	CameraRequest* pBestRequest = pPersistentRequest;


	if (m_currentRequest.m_isActive)
	{
		/*if (m_currentRequest.GetPriority() == CameraPriority::Class::kJournal && pBestRequest && pBestRequest->m_pExtension && pBestRequest->m_pExtension->HasSources())
		{
			// If we're cutting to the follow camera after leaving the journal state and there is a scripted
			// follow camera, override the scripted camera's blend so we cut but keep the right settings
			pBestRequest->m_blendInfo = m_currentRequest.m_blendInfo;
			if (const Locator* pLoc = m_currentRequest.m_startInfo.GetLocator())
				pBestRequest->m_startInfo.SetLocator(*pLoc);
		}
		else */if (!pBestRequest
		||	m_currentRequest.GetRank() == CameraRank::Debug // a Top-ranked immediate request always trumps a persistent request
		||	m_currentRequest.GetRank() == CameraRank::Override // an Override-ranked immediate request always trumps a persistent request
		||	pBestRequest->GetPriority() <= m_currentRequest.GetPriority())
		{
			pBestRequest = &m_currentRequest;
		}
	}

	// If the best request represents a camera that would be DIFFERENT from the
	// currently-running camera, then push the new camera... otherwise do nothing.

	bool isEquivalentToCurrent = false;
	// NB: use nested if()s to make IsEquivalentTo() easier to debug...
	if (pBestRequest)
	{
		if (pCurCamera)
		{
			if (pCurCamera->IsPhotoMode() && EngineComponents::GetNdPhotoModeManager()->IsActive() && pBestRequest->GetPriority().GetClass() != CameraPriority::Class::kForced)
			{
				// Photo Mode hack: if someone asks for a non-photo-mode camera while a photo mode cam is active and photo mode itself is active, don't let it activate.
				pBestRequest = nullptr;
			}
			if (pBestRequest && pCurCamera->GetCameraId() == pBestRequest->m_cameraId)
			{
				if (!pCurCamera->IsSelfFadeOutRequested() && pCurCamera->IsEquivalentTo(pBestRequest->m_startInfo))
				{
					isEquivalentToCurrent = true;
					pBestRequest = nullptr;
				}
			}
		}
	}

	if (pBestRequest != nullptr)
	{
		const CameraControl* const pOriginalCurCamera = pCurCamera; //For debugging
		//We need to compare against the override cameras
		if (pBestRequest->GetRank() == CameraRank::Override)
		{
			pCurCamera = GetCurrentCameraIncludingOverride();
		}
		if (pCurCamera != nullptr)
		{
			if (pCurCamera->IsPhotoMode() && (EngineComponents::GetNdPhotoModeManager() && EngineComponents::GetNdPhotoModeManager()->IsActive()) && pBestRequest->GetPriority().GetClass() != CameraPriority::Class::kForced)
			{
				// Photo Mode hack: if someone asks for a non-photo-mode camera while a photo mode cam is active and photo mode itself is active, don't let it activate.
				pBestRequest = nullptr;
			}
			if (pCurCamera->GetPriority().GetClass() == CameraPriority::Class::kDeath &&
				(pBestRequest->GetPriority().GetClass() != CameraPriority::Class::kForced && pBestRequest->GetPriority().GetClass() != CameraPriority::Class::kDeath) &&
				pBestRequest != pPersistentRequest)
			{
				pBestRequest = nullptr;
			}

			if (pBestRequest && pCurCamera->GetCameraId() == pBestRequest->m_cameraId && pCurCamera->IsEquivalentTo(pBestRequest->m_startInfo))
			{
				isEquivalentToCurrent = true;
				pBestRequest = nullptr;

				if (pCurCamera->IsSelfFadeOutRequested())
					pCurCamera->CancelSelfFadeOut(Seconds(0.25f));
			}
			else if (pBestRequest && pBestRequest->m_cameraId.ToConfig().GetProcessType() == SID("CameraControlStrafe") &&
				pCurCamera->IsFadeInByDistToObj() &&
				SearchBackwardsAndMatch(pPrevCamera, pBestRequest, true) != nullptr) // stop at the first CameraControlStrafe.
			{
				pCurCamera->KillWhenBlendedOut();
				isEquivalentToCurrent = true;
				pBestRequest = nullptr;
			}
			else if (pBestRequest && 
					!pBestRequest->m_startInfo.m_photoMode && 
					pBestRequest->GetRank() == CameraRank::Normal && 
					pCurCamera->IsSelfFadeOutAllowed())
			{
				if (pBestRequest->m_cameraId.ToConfig().GetProcessType() == SID("CameraControlStrafe") ||
					pBestRequest->m_cameraId.ToConfig().GetProcessType() == SID("CameraControlStrafeAutoGenerated"))
				{
					const CameraControlStrafeBase* pPrevStrafeCamera = CameraControlStrafeBase::FromProcess(pPrevCamera);
					if (pPrevStrafeCamera != nullptr)
					{
						StringId64 baseSettingsId = pBestRequest->m_startInfo.m_settingsId;
						if (baseSettingsId != INVALID_STRING_ID_64)
						{
							CameraRemapSettings remapSettings = RemapCameraSettingsId(baseSettingsId, GetPlayerIndex(), true);
							StringId64 dcSettingsId = remapSettings.m_remapSettingsId;

							// only let special camera self fade out if the previous camera equals the newly requested camera.
							if (dcSettingsId == pPrevStrafeCamera->GetDCSettingsId() || pPrevStrafeCamera->IsFadeInByDistToObj())
							{
								// experiment always self-fade-out special/zoom camera.
								// it will resolve the issue entering a dist-camera region with zoom-camera only.
								TimeFrame blendTime = Seconds(0.5f);
								if (pBestRequest->m_blendInfo.m_type == CameraBlendType::kTime)
									blendTime = pBestRequest->m_blendInfo.m_time;

								if (pCurCamera->GetBlendOutTime() >= 0.0f)
								{
									blendTime = Seconds(pCurCamera->GetBlendOutTime());
								}								

								pCurCamera->RequestSelfFadeOut(blendTime);
								pBestRequest = nullptr;
							}
						}
					}
				}
			}
		}

		if (pBestRequest != nullptr)
		{
			pNewCamera = TryPushStrafeCameraPair(pCurCamera, pBestRequest);
			if (pNewCamera != nullptr)
				pBestRequest = nullptr;
		}
	}

	if (pBestRequest != nullptr)
	{
		// Any time a new camera takes over from an abandoned camera, clear out all abandoned cameras.
		if (!pBestRequest->m_isAbandoned)
			ReapPersistentCameraRequests(ReapAbandoned::kReap);

		// OK, create and start the appropriate CameraControl.
		const CameraConfig& config = pBestRequest->m_cameraId.ToConfig();

		ALWAYS_ASSERTF(pBestRequest->m_cameraId != kCameraNone, ("Attempt to instantiate kCameraNone"));
		ALWAYS_ASSERTF(config.GetProcessType() != SID("CameraControl"), ("Attempt to instantiate abstract CameraControl"));

		pNewCamera = CreateControlFromName(m_playerIndex, config.GetProcessType(), &pBestRequest->m_startInfo);
		if (pNewCamera)
		{
			// Do this first so that we can have more than one CameraId map to a single
			// CameraControl subclass -- the derived class can use its id to determine
			// how to configure itself (e.g. various kinds of spline cameras).
			// NB: We could just pass this to CameraStart(), but it's too rare to be on
			// the main code path IMO.
			pNewCamera->SetCameraId(pBestRequest->m_cameraId);

			// Also cache the priority in the new camera.
			pNewCamera->SetPriority(pBestRequest->GetPriority());

			// If this is the first gameplay camera being started, AND the caller hasn't requested
			// a specific locator, then use the "start locator".
			const Locator* pOldLoc = pBestRequest->m_startInfo.GetLocator();
			if ((pCurCamera == nullptr || m_bStartingFirstCamera) && pOldLoc == nullptr)
				pBestRequest->m_startInfo.SetLocator(GetStartLocator());

			// Pass the extension to the camera so it can potentially increase its reference count and "own" it
			// even after the persistent CameraRequest is gone.
			pBestRequest->m_startInfo.m_pExtension = pBestRequest->m_pExtension;

			// CameraStart() should start up the camera, using whatever parameters it deems fit
			// from the CameraStartInfo provided. It should also provide valid blend parameters,
			// if blendInfo.m_type == CameraBlendType::kDefault.
			CameraBlendInfo blendInfo = pNewCamera->CameraStart(pBestRequest->m_startInfo);

			// Restore the request's old locator if necessary.
			if (pCurCamera == nullptr && pOldLoc == nullptr)
			{
				pBestRequest->m_startInfo.ClearLocator();
			}

			pBestRequest->m_hCamera = pNewCamera;

			// for debugging.
			pNewCamera->m_file = pBestRequest->m_file;
			pNewCamera->m_line = pBestRequest->m_line;
			pNewCamera->m_func = pBestRequest->m_func;

			// Figure out the camera blend
			if (pCurCamera == nullptr)
			{
				// If we're starting the first gameplay camera, don't blend.
				blendInfo = Seconds(0.0f);
			}
			else if (m_blendInfoOverride[kBlendInfoPriHigh] != CameraBlendInfo())
			{
				// If the high-priority global override is active, use it.
				blendInfo = m_blendInfoOverride[kBlendInfoPriHigh];
			}
			else if (pCallerBlendInfo != nullptr)
			{
				// If the caller has requested a specific blend, use it rather than the one supplied by the camera.
				blendInfo = *pCallerBlendInfo;
			}
			else if (pBestRequest->m_blendInfo != CameraBlendInfo())
			{
				// Try to use the requested blend.
				blendInfo = pBestRequest->m_blendInfo;
			}
			else if (pNewCamera->GetCameraId().ToConfig().GetDefaultPriority().GetClass() < CameraPriority::Class::kSpecial &&
				m_blendInfoOverride[kBlendInfoPriLow] != CameraBlendInfo())
			{
				// If the low-priority global override is active, use it.
				blendInfo = m_blendInfoOverride[kBlendInfoPriLow];
			}

			if (g_cameraForceZeroBlendTimes) // useful for debugging certain kinds of problems (like "is this pop due to camera A or camera B blended with camera A?")
				blendInfo = Seconds(0.0f);

			// Reset latches.
			ClearBlendInfoOverride();
			m_bStartingFirstCamera = false;

			// figure out the rank.
			CameraRank rank = config.GetDefaultRank();
			OMIT_FROM_FINAL_BUILD(extern bool g_fakePhotoModeCam);
			if (pNewCamera->IsPhotoMode() && !FALSE_IN_FINAL_BUILD(g_fakePhotoModeCam))
				rank = CameraRank::Debug; // hackette

			// Start the new camera.
			StartCameraInternal(pNewCamera, rank, blendInfo);

			CameraControl* pOldCamera = pCurCamera;
			if (pOldCamera != nullptr)
			{
				// Drop the old camera.
				ASSERT(pOldCamera->GetRank() != CameraRank::Bottom); // bottom and top cameras are not returned by GetCurrentCamera()
				pOldCamera->KillWhenBlendedOut();
			}
		}
	}
	else if (!isEquivalentToCurrent && pCallerBlendInfo != nullptr)
	{
		// No camera request was available, so the requested blend didn't "take".
		// Set it up to "take" on the next request that *is* processsed.
		m_blendInfoOverride[kBlendInfoPriLow] = *pCallerBlendInfo;
	}

	// Always clear the current request -- it is only valid between the
	// call to RequestCamera() and the end of EvaluateCameraRequests().
	m_currentRequest.Clear();

	return pNewCamera;
}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraControl* CameraManager::TryPushStrafeCameraPair(CameraControl* pCurCamera, CameraRequest* pBestRequest)
{
	if (!pBestRequest)
		return nullptr;

	if (pBestRequest->m_cameraId.ToConfig().GetProcessType() != SID("CameraControlStrafe"))
		return nullptr;

	// photo mode camera shouldn't use camera pair. (aka, blend by dist-to-obj)
	if (pBestRequest->m_startInfo.m_photoMode)
		return nullptr;

	CameraControl* pNewCamera = nullptr;

	CameraControlStrafeBase* pCurrStrafeBase = CameraControlStrafeBase::FromProcess(pCurCamera);
	CameraControlAnimated* pCurCamAnimated = CameraControlAnimated::FromProcess(pCurCamera);	

	if (pCurrStrafeBase != nullptr && !pCurrStrafeBase->IsFadeInByDistToObj() && !pCurrStrafeBase->IsFadeInByTravelDist())
	{
		// create auto-gen from animated camera which hasn't started yet.
		{
			DistCameraParams distRemapParams;
			AutoGenCamParams autoGenParams;
			if (TryAutoGenerateIntroCamera(m_playerIndex, pCurrStrafeBase, &distRemapParams, &autoGenParams))
			{
				const CameraBlendInfo* pBlendInfo = nullptr;
				CameraBlendInfo defaultBlend(Seconds(1.0f));
				CameraControl* pOldCamera = pCurCamera;

				bool needPushBase = pCurrStrafeBase->GetBlend() < 1.f - FLT_EPSILON; // if previous camera is not fully blended, it will pop
				if (!needPushBase)
				{
					const NdGameObject* pFocusObj = pCurrStrafeBase->GetFocusObject().ToProcess();
					const NdGameObject* pDistObj = distRemapParams.m_hDistObj.ToProcess();
					if (pFocusObj != nullptr && pDistObj != nullptr)
					{
						const float distToObj = pDistObj->GetDistBlendCameraDist(pFocusObj);
						if (distToObj < distRemapParams.m_startBlendInDist * 0.9f) // if player enter dist-remap-region half way, it will pop.
							needPushBase = true;
					}
				}

				if (needPushBase)
				{
					// in case current strafe camera is still blending-in, to avoid pop, push a pair
					pBestRequest->m_startInfo.m_distCamBase = true;
					pBlendInfo = &defaultBlend;
					pNewCamera = StartDistRemapBase(pBestRequest, pCurrStrafeBase, pBlendInfo);
					pOldCamera = pNewCamera;
				}

				pNewCamera = StartAutoGenCamera(autoGenParams, false, pOldCamera, pBestRequest->m_startInfo.m_settingsId, pBlendInfo);
				pBestRequest = nullptr;
			}
		}

		// only push dist-remap-top camera if current camera could serve dist-remap-base.
		if (pBestRequest != nullptr && pBestRequest->m_startInfo.m_settingsId == pCurrStrafeBase->GetBaseDcSettingsId())
		{
			DistCameraParams distRemapParams;
			if (TryTimeBasedDistRemapCamera(m_playerIndex, pBestRequest, &distRemapParams))
			{
				const CameraBlendInfo* pBlendInfo = nullptr;
				CameraBlendInfo defaultBlend(Seconds(1.0f));
				CameraControl* pOldCamera = pCurCamera;

				bool needPushBase = pCurrStrafeBase->GetBlend() < 1.f - FLT_EPSILON; // if previous camera is not fully blended, it will pop
				if (!needPushBase)
				{ 
					const NdGameObject* pFocusObj = pCurrStrafeBase->GetFocusObject().ToProcess();
					const NdGameObject* pDistObj = distRemapParams.m_hDistObj.ToProcess();
					if (pFocusObj != nullptr && pDistObj != nullptr)
					{
						const float distToObj = pDistObj->GetDistBlendCameraDist(pFocusObj);
						if (distToObj < distRemapParams.m_startBlendInDist * 0.9f) // if player enter dist-remap-region half way, it will pop.
							needPushBase = true;
					}
				}

				if (needPushBase)
				{
					// in case current strafe camera is still blending-in, to avoid pop, push a pair
					pBestRequest->m_startInfo.m_distCamBase = true;
					pBlendInfo = &defaultBlend;
					pNewCamera = StartDistRemapBase(pBestRequest, pCurrStrafeBase, pBlendInfo);
					pOldCamera = pNewCamera;
				}

				pNewCamera = StartDistRemapTop(distRemapParams, pOldCamera, pBestRequest->m_startInfo.m_settingsId, pBlendInfo);
				pBestRequest = nullptr;
			}
		}
	}
	else if (pCurCamAnimated != nullptr)
	{
		// fade out animated to auto-gen by dist.
		if (pCurCamAnimated->GetNextCameraAutoGenScriptParams().m_fadeOutDist > 0.f)
		{
			AutoGenCamParams autoGenParams;
			if (TryAutoGenerateOutroCamera(pBestRequest, pCurCamAnimated, &autoGenParams))
			{
				pNewCamera = StartAutoGenCamera(autoGenParams, true, pCurCamAnimated, pBestRequest->m_startInfo.m_settingsId);
				pBestRequest = nullptr;
			}
		}
	}

	if (pBestRequest != nullptr && pCurCamera != nullptr)
	{
		{
			// create auto-gen camera and use dist-camera-remap.
			AutoGenCamParams autoGenParams;
			if (TryAutoGenerateDistRemapCamera(m_playerIndex, pBestRequest, &autoGenParams))
			{
				bool blendInfoValid = false;
				CameraBlendInfo blendInfo;
				if (pBestRequest->m_blendInfo.m_useThisBlendForAutoGenStrafeCams)
				{
					blendInfo = pBestRequest->m_blendInfo;
					blendInfoValid = true;
				}
				else if (m_blendInfoOverride[kBlendInfoPriHigh] != CameraBlendInfo())
				{
					blendInfo = m_blendInfoOverride[kBlendInfoPriHigh];
					blendInfoValid = true;
				}
				else if (m_blendInfoOverride[kBlendInfoPriLow] != CameraBlendInfo())
				{
					blendInfo = m_blendInfoOverride[kBlendInfoPriLow];
					blendInfoValid = true;
				}

				const bool disableInputUntil70 = pCurCamAnimated ? pCurCamAnimated->GetNextCameraInputDisabledUntil70() : false;

				CameraBlendInfo defaultBlend(Seconds(0.5f));
				const CameraBlendInfo* pBlendInfo = blendInfoValid ? &blendInfo : &defaultBlend;
				// first push a strafe-camera as base of dist-camera-remap.
				pBestRequest->m_startInfo.m_distCamBase = true;
				pBestRequest->m_startInfo.m_disableInputUntil70 = disableInputUntil70;

				pNewCamera = StartDistRemapBase(pBestRequest, pCurCamera, pBlendInfo);
				pNewCamera = StartAutoGenCamera(autoGenParams, false, pNewCamera, pBestRequest->m_startInfo.m_settingsId, pBlendInfo, true, disableInputUntil70);

				pBestRequest = nullptr;
			}
		}

		if (pBestRequest != nullptr) // not handled.
		{
			DistCameraParams distRemapParams;
			if (TryTimeBasedDistRemapCamera(m_playerIndex, pBestRequest, &distRemapParams))
			{
				bool blendInfoValid = false;
				CameraBlendInfo blendInfo;
				if (pBestRequest->m_blendInfo.m_useThisBlendForAutoGenStrafeCams)
				{
					blendInfo = pBestRequest->m_blendInfo;
					blendInfoValid = true;
				}
				else if (m_blendInfoOverride[kBlendInfoPriHigh] != CameraBlendInfo())
				{
					blendInfo = m_blendInfoOverride[kBlendInfoPriHigh];
					blendInfoValid = true;
				}
				else if (m_blendInfoOverride[kBlendInfoPriLow] != CameraBlendInfo())
				{
					blendInfo = m_blendInfoOverride[kBlendInfoPriLow];
					blendInfoValid = true;
				}

				CameraBlendInfo defaultBlend(Seconds(0.5f));
				const CameraBlendInfo* pBlendInfo = blendInfoValid ? &blendInfo : &defaultBlend;
				// first push a strafe-camera as base of dist-camera-remap.
				pBestRequest->m_startInfo.m_distCamBase = true;
				pNewCamera = StartDistRemapBase(pBestRequest, pCurCamera, pBlendInfo);

				distRemapParams.m_immediate = true; // pop into desired distance.
				pNewCamera = StartDistRemapTop(distRemapParams, pNewCamera, pBestRequest->m_startInfo.m_settingsId, pBlendInfo);

				pBestRequest = nullptr;
			}
		}
	}

	return pNewCamera;
}

/// --------------------------------------------------------------------------------------------------------------- ///
///  CameraManager::Active State
/// --------------------------------------------------------------------------------------------------------------- ///

class CameraManager::Active : public Process::State
{
private:
	typedef Process::State ParentClass;
	BIND_TO_PROCESS(CameraManager);

public:
	void Enter () override
	{
		CameraManager& pp = Self();

		// setup base camera
		CameraManualStartInfo startInfo;
		startInfo.SetLocator(Locator(pp.m_cameraStartPos));
		startInfo.SetTarget(pp.m_cameraStartTarget);

		CameraBlendInfo blendInfo(Seconds(0.0f));
		pp.RequestCamera(FILE_LINE_FUNC, kCameraManualBase, &blendInfo, &startInfo);
	}

	void ActuallyRunCameraManager(CameraManager& pp)
	{
		if (pp.m_playerIndex == 0)
		{
			const Clock* pClock = (EngineComponents::GetNdPhotoModeManager() && EngineComponents::GetNdPhotoModeManager()->IsActive())
								? EngineComponents::GetNdFrameState()->GetClock(kCameraClock)
								: EngineComponents::GetNdFrameState()->GetClock(kGameClock);
			if (!pClock->IsPaused())
			{
				Seek(g_cameraOptions.m_overrideFovBlend, g_cameraOptions.m_desiredOverrideFovBlend, 5.0f * pClock->GetDeltaTimeInSeconds());
			}
		}
		pp.ReapCameras();
		//pp.DebugShowCameras(); // moved into RunCameras(), camera weights here are wrong
	}

	void Update() override
	{
		PROFILE(Camera, CameraManagerActiveUpdate);
		CameraManager& pp = Self();
		ParentClass::Update();
		ActuallyRunCameraManager(pp);
	}
};

STATE_REGISTER(CameraManager, Active, kPriorityNormal);

void CameraManager::DebugDrawCamera(const CameraLocation &cameraLocation,
									F32 aspectRatio,
									Color color1 /* = kColorGreen*/,
									Color color2 /* = kColorRed */,
									DebugPrimTime tt /*= kPrimDuration1FrameAuto */)
{
	STRIP_IN_FINAL_BUILD;

	Point center = cameraLocation.GetLocator().GetTranslation();
	Quat rot = cameraLocation.GetLocator().GetRotation();
	Vector dir = GetLocalZ(rot);
	Vector right = GetLocalX(rot);
	Vector up = GetLocalY(rot);
	const float nearPlaneDist = cameraLocation.GetNearPlaneDist();
	float width = nearPlaneDist * Tan(DEGREES_TO_RADIANS(cameraLocation.GetFov() * 0.5f));
	float height = width * aspectRatio;
	Point ur = center + height * up + width * right + nearPlaneDist * dir;
	Point ul = center + height * up - width * right + nearPlaneDist * dir;
	Point lr = center - height * up + width * right + nearPlaneDist * dir;
	Point ll = center - height * up - width * right + nearPlaneDist * dir;
	DebugDrawLine(center, ur, color1, tt);
	DebugDrawLine(center, ul, color1, tt);
	DebugDrawLine(center, lr, color1, tt);
	DebugDrawLine(center, ll, color1, tt);
	DebugDrawLine(lr, ur, color2, tt);
	DebugDrawLine(ur, ul, color2, tt);
	DebugDrawLine(ul, ll, color2, tt);
	DebugDrawLine(ll, lr, color2, tt);
}

// -------------------------------------------------------------------------------------------------
// ICameraRequestExtension
// -------------------------------------------------------------------------------------------------

ICameraRequestExtension::~ICameraRequestExtension()
{
}

// -------------------------------------------------------------------------------------------------
// Snapshot
// -------------------------------------------------------------------------------------------------

PROCESS_SNAPSHOT_DEFINE(CameraManagerSnapshot, ProcessSnapshot);


// -------------------------------------------------------------------------------------------------
Point GetViewCenterFromCamLoc(int playerIndex, Point_arg basePos, Vector_arg viewDir, float scaleAlongViewDir)
{
	const float nearPlaneDist = CameraManager::GetGlobal(playerIndex).GetNearPlaneDist();
	Point viewCenter = basePos + scaleAlongViewDir * nearPlaneDist * viewDir;
	return viewCenter;
}

Point GetViewCenterFromCamLoc(int playerIndex, const Locator& camLoc, float scaleAlongViewDir)
{
	const Point cameraPos = camLoc.GetTranslation();
	const Vector cameraDir = GetLocalZ(camLoc.GetRotation());
	return GetViewCenterFromCamLoc(playerIndex, cameraPos, cameraDir, scaleAlongViewDir);
}

// -------------------------------------------------------------------------------------------------
namespace CameraCommands
{
	static Mat44 GetGameCameraTransformToScreen()
	{
		return g_mainCameraInfo[0].GetRenderCamera().m_mtx.m_worldToScreen;
	}

	static Mat44 GetGameCameraTransform()
	{
		return g_mainCameraInfo[0].GetLocator().AsTransform().GetRawMat44();
	}

	static float GetGameCameraFOV()
	{
		return g_mainCameraInfo[0].GetFov();
	}

	static void SetManualCamPos(const Point &pt)
	{
		Locator loc(pt, CameraManager::Get().GetManualCamera() ? CameraManager::Get().GetManualCamera()->GetLocator().GetRotation() : Quat(kIdentity));
		CameraManager::Get().TeleportManualCamera(loc);
	}

	static void SetManualCameraLookAt(const Point &pt)
	{
		Point camPt = CameraManager::Get().GetManualCamera() ? CameraManager::Get().GetManualCamera()->GetLocator().GetTranslation() : Point(kZero);


		g_prim.Draw(DebugCross(pt), Seconds(10.0f));

		Vector fwDir = SafeNormalize(pt - camPt, kUnitZAxis);

		Quat rot = QuatFromLookAt(fwDir, kUnitYAxis);

		Locator loc(camPt, rot);
		CameraManager::Get().TeleportManualCamera(loc);
	}

	static void SetManualCameraRotation(float rqx, float rqy, float rqz, float rqw)
	{
		Point camPt = CameraManager::Get().GetManualCamera() ? CameraManager::Get().GetManualCamera()->GetLocator().GetTranslation() : Point(kZero);

		Quat rot(rqx, rqy, rqz, rqw);

		Locator loc(camPt, rot);
		CameraManager::Get().TeleportManualCamera(loc);
	}
	static Point GetManualCameraPosition()
	{
		Point camPt = CameraManager::Get().GetManualCamera() ? CameraManager::Get().GetManualCamera()->GetLocator().GetTranslation() : Point(kZero);

		return camPt;
	}


	static void EnableManualCamera()
	{
		if (!CameraManager::Get().IsManualCameraActive())
		{
			CameraManager::Get().ToggleManualCamera();
		}
	}


	static void DisableManualCamera()
	{
		if (CameraManager::Get().IsManualCameraActive())
		{
			CameraManager::Get().ToggleManualCamera();
		}
	}


	static void RotateManualCameraDeg(float x, float y, float z)
	{
		Point camPt = CameraManager::Get().GetManualCamera() ? CameraManager::Get().GetManualCamera()->GetLocator().GetTranslation() : Point(kZero);
		Locator loc(camPt, CameraManager::Get().GetManualCamera() ? CameraManager::Get().GetManualCamera()->GetLocator().GetRotation() : Quat(kIdentity));

		Quat rot(DEGREES_TO_RADIANS(x), DEGREES_TO_RADIANS(y), DEGREES_TO_RADIANS(z), Quat::RotationOrder::kXYZ);

		loc.SetRotation(rot * loc.GetRotation());

		CameraManager::Get().TeleportManualCamera(loc);
	}

	static void RotateManualCameraDegAroundFocalPoint(float x, float y, float z, Point_arg focalPoint)
	{
		Point camPt = CameraManager::Get().GetManualCamera() ? CameraManager::Get().GetManualCamera()->GetLocator().GetTranslation() : Point(kZero);
		Locator loc(camPt, CameraManager::Get().GetManualCamera() ? CameraManager::Get().GetManualCamera()->GetLocator().GetRotation() : Quat(kIdentity));

		Quat rot(DEGREES_TO_RADIANS(x), DEGREES_TO_RADIANS(y), DEGREES_TO_RADIANS(z), Quat::RotationOrder::kXYZ);

		Locator focalLoc(focalPoint);

		Locator cameraFocalPtSpace = focalLoc.UntransformLocator(loc);
		focalLoc.SetRotation(rot * focalLoc.GetRotation());

		loc = focalLoc.TransformLocator(cameraFocalPtSpace);

		CameraManager::Get().TeleportManualCamera(loc);
	}

	static void MoveManualCameraAlongLookAt(float d)
	{
		Point camPt = CameraManager::Get().GetManualCamera() ? CameraManager::Get().GetManualCamera()->GetLocator().GetTranslation() : Point(kZero);
		Locator loc(camPt, CameraManager::Get().GetManualCamera() ? CameraManager::Get().GetManualCamera()->GetLocator().GetRotation() : Quat(kIdentity));


		Vector fw = GetLocalZ(loc);

		camPt = camPt + fw * d;

		loc.SetTranslation(camPt);

		CameraManager::Get().TeleportManualCamera(loc);
	}

	static float GetNeededMoveToFitAabb(const Aabb &aabb)
	{
		Point camPt = CameraManager::Get().GetManualCamera() ? CameraManager::Get().GetManualCamera()->GetLocator().GetTranslation() : Point(kZero);
		Locator loc(camPt, CameraManager::Get().GetManualCamera() ? CameraManager::Get().GetManualCamera()->GetLocator().GetRotation() : Quat(kIdentity));

		g_prim.Draw(DebugBox(aabb, kColorRed, PrimAttrib(kPrimEnableWireframe)), Seconds(10.0f));

		Point p0 = aabb.m_min;                       // - - -
		Point p1 = p0; p1.SetX(aabb.m_max.X());      // + - -
		Point p2 = p1; p2.SetY(aabb.m_max.Y());      // + + -
		Point p3 = p2; p3.SetZ(aabb.m_max.Z());      // + + +
		Point p4 = p3; p4.SetY(aabb.m_min.Y());      // + - +
		Point p5 = p4; p5.SetX(aabb.m_min.X());      // - - +
		Point p6 = p5; p6.SetY(aabb.m_max.Y());      // - + +
		Point p7 = p6; p7.SetZ(aabb.m_min.Z());      // - + -

		Point pts[8] = { p0, p1, p2, p3, p4, p5, p6, p7 };


		Point blockMins = POINT_LC(10000.0f, 10000.0f, 10000.0f);
		Point blockMaxs = POINT_LC(-10000.0f, -10000.0f, -10000.0f);


		for (int i = 0; i < 8; ++i)
		{
			pts[i] = loc.UntransformPoint(pts[i]);

			blockMins = Min(blockMins, pts[i]);
			blockMaxs = Max(blockMaxs, pts[i]);
		}

		float w = Max(Abs(blockMaxs.X()), Abs(blockMins.X()));

		float fov = g_cameraOptions.m_defaultFov;

		float d = w / (Tan(DEGREES_TO_RADIANS(fov / 2.0f)));

		F32 aspectRatio = g_display.GetAspectRatio();

		float h = Max(Abs(blockMaxs.Y()), Abs(blockMins.Y()));

		float fovV = fov * aspectRatio;
		float dV = h / (Tan(DEGREES_TO_RADIANS(fovV / 2.0)));


		float relMotion = blockMins.Z() - Max(d, dV);

		return relMotion;
	}


	void SetManualCameraPosAwayFromPointAlongLookAt(float d, const Point &pt)
	{
		Point camPt = CameraManager::Get().GetManualCamera() ? CameraManager::Get().GetManualCamera()->GetLocator().GetTranslation() : Point(kZero);
		Locator loc(camPt, CameraManager::Get().GetManualCamera() ? CameraManager::Get().GetManualCamera()->GetLocator().GetRotation() : Quat(kIdentity));

		Vector fw = GetLocalZ(loc);

		camPt = pt - fw * d;

		loc.SetTranslation(camPt);

		CameraManager::Get().TeleportManualCamera(loc);
	}
}

REGISTER_RPC_NAMESPACE_FUNC(Mat44, CameraCommands, GetGameCameraTransformToScreen, ());

REGISTER_RPC_NAMESPACE_FUNC(Mat44, CameraCommands, GetGameCameraTransform, ());

REGISTER_RPC_NAMESPACE_FUNC(float, CameraCommands, GetGameCameraFOV, ());

REGISTER_RPC_NAMESPACE_FUNC(void, CameraCommands, SetManualCamPos, (const Point &pt));

REGISTER_RPC_NAMESPACE_FUNC(void, CameraCommands, SetManualCameraLookAt, (const Point &pt));

REGISTER_RPC_NAMESPACE_FUNC(void, CameraCommands, SetManualCameraRotation, (float rqx, float rqy, float rqz, float rqw));

REGISTER_RPC_NAMESPACE_FUNC(Point, CameraCommands, GetManualCameraPosition, ());

REGISTER_RPC_NAMESPACE_FUNC(void, CameraCommands, EnableManualCamera, ());

REGISTER_RPC_NAMESPACE_FUNC(void, CameraCommands, DisableManualCamera, ());

REGISTER_RPC_NAMESPACE_FUNC(void, CameraCommands, RotateManualCameraDeg, (float x, float y, float z));

REGISTER_RPC_NAMESPACE_FUNC(void, CameraCommands, RotateManualCameraDegAroundFocalPoint, (float x, float y, float z, Point_arg focalPoint));

REGISTER_RPC_NAMESPACE_FUNC(void, CameraCommands, MoveManualCameraAlongLookAt, (float d));

REGISTER_RPC_NAMESPACE_FUNC(void, CameraCommands, SetManualCameraPosAwayFromPointAlongLookAt, (float d, const Point &pt));

REGISTER_RPC_NAMESPACE_FUNC(float, CameraCommands, GetNeededMoveToFitAabb, (const Aabb &aabb));


