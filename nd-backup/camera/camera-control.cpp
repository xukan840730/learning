/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/camera/camera-control.h"

#include "corelib/math/algebra.h"
#include "corelib/memory/relocate.h"

#include "ndlib/nd-frame-state.h"
#include "ndlib/process/event.h"
#include "ndlib/settings/priority.h"
#include "ndlib/settings/render-settings.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/util/curves.h"
#include "ndlib/settings/settings.h"
#include "ndlib/net/nd-net-info.h"

#include "gamelib/camera/camera-manager.h"
#include "gamelib/camera/camera-node.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/scriptx/h/nd-camera-settings.h"
#include "gamelib/scriptx/h/weapon-base-defines.h"
#include "gamelib/state-script/ss-manager.h"

bool g_debugCameraFocusDistance = false;

PROCESS_REGISTER_ABSTRACT(CameraControl, Process);
CAMERA_REGISTER(kCameraNone, CameraControl, CameraStartInfo, CameraPriority::Class::kInvalid);

class CameraControl::Active : public Process::State
{
private:
	BIND_TO_PROCESS(CameraControl);
public:
	virtual void Enter() override {}
	virtual void Update() override {}
};

STATE_REGISTER(CameraControl, Active, kPriorityNormal);

float CameraControl::s_dofDefaultFilmWidth = 35.f;
float CameraControl::s_dofDefaultFNumber = 22.f;
float CameraControl::s_dofDefaultFocalLength = 20.f;
float CameraControl::s_dofDefaultBlurScale = 1.f;
float CameraControl::s_dofDefaultBlurThreshold = 0.f;

/// --------------------------------------------------------------------------------------------------------------- ///

CameraControl::CameraControl ()
	: m_file(nullptr)
	, m_line(0)
	, m_func(nullptr)
{
	static NdAtomic32 s_newHandleId(0);
	m_flags.Clear();
	m_handleId = (U32)s_newHandleId.Add(1);
}

/// --------------------------------------------------------------------------------------------------------------- ///

CameraControl::~CameraControl ()
{
	m_handleId = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraControl::Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(offset_bytes, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CameraControl::GetRunWhenPaused() const
{
	return m_flags.m_runWhenPaused;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControl::EventHandler(Event& event)
{
	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("set-blend"):
		{
			m_blend = event.Get(0).GetFloat();
		}
		break;
	}

	ParentClass::EventHandler(event);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControl::OnKillProcess()
{
	ParentClass::OnKillProcess();
	m_debugDistPlot.Destroy();
	m_debugFocusDistPlot.Destroy();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControl::PrepareFinalize(bool isTopControl, const CameraControl* pPrevControl)
{
	m_bIsTopControl = isTopControl;

	if (isTopControl &&
		(!m_flags.m_noDefaultDofBehavior || g_cameraOptions.m_forceDofSettingsId != INVALID_STRING_ID_64 || m_lastForcedDofSettingsId != INVALID_STRING_ID_64)
	)
	{
		float targetAperture = s_dofDefaultFNumber;
		if (g_cameraOptions.m_forceDofSettingsId != INVALID_STRING_ID_64)
		{
			const DC::CameraDofSettings* pDofSettings = ScriptManager::Lookup<DC::CameraDofSettings>(g_cameraOptions.m_forceDofSettingsId);
			GAMEPLAY_ASSERTF(pDofSettings, ("Invalid Dof Settings Id: %s\n", DevKitOnly_StringIdToString(g_cameraOptions.m_forceDofSettingsId)));
			m_lastForcedDofSettingsId = g_cameraOptions.m_forceDofSettingsId;

			m_blurScale = g_cameraOptions.m_forceDofBlurScale;
			m_forceDofBlurScale = g_cameraOptions.m_forceDofBlurScale;

			const Locator& gameCamLoc = GameCameraLocator();
			const Point ptInCameraView = gameCamLoc.UntransformPoint(g_cameraOptions.m_forceDofFocusTargetBf.GetTranslation());
			float dist = Max(0.f, (float)ptInCameraView.Z());

			UpdateDistToDofFocus(dist, m_location.GetFov(), pDofSettings);
			DebugDof(dist, m_location.GetFov(), false);

			targetAperture = pDofSettings->m_fNumber;
			m_forceDofAperture = pDofSettings->m_fNumber;
			m_apertureSpringConst = g_cameraOptions.m_forceDofFocusApertureSpringConst;

			m_forceDofBlendTime = g_cameraOptions.m_forceDofFocusPersistTime;
			m_forceDofBlendTimeRemaining = g_cameraOptions.m_forceDofFocusPersistTime;
		}
		else if (m_lastForcedDofSettingsId != INVALID_STRING_ID_64)
		{
			BlendDofSettingsToDefault(targetAperture);
		}

		if (m_lastForcedDofSettingsId != INVALID_STRING_ID_64)
		{
			m_dofSettingsId = m_lastForcedDofSettingsId;
		}

		m_aperture = m_apertureSpring.Track(m_aperture, targetAperture, GetProcessDeltaTime(), m_apertureSpringConst);
	}

	m_prepareReachedFn = EngineComponents::GetNdFrameState()->m_gameFrameNumber;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControl::BlendDofSettingsToDefault(float &out_targetAperture)
{
	// Blend only Blur Scale and Aperture, since they're modified freely by the camera. The other parameters are blended in the only function that uses them, DofUpdate().
	if (m_forceDofBlendTime > 0.0f)
	{
		float blend = m_forceDofBlendTimeRemaining / m_forceDofBlendTime;
		m_blurScale = Lerp(s_dofDefaultBlurScale, m_forceDofBlurScale, blend);
		out_targetAperture = Lerp(s_dofDefaultFNumber, m_forceDofAperture, blend);

		m_forceDofBlendTimeRemaining -= GetProcessDeltaTime();
	}

	if (!(m_forceDofBlendTimeRemaining > 0.0f))
	{
		SetDofSettingsToDefault(out_targetAperture);
	}
}

void CameraControl::SetDofSettingsToDefault(float &out_targetAperture)
{
	if (m_dofSettingsId == m_lastForcedDofSettingsId)
		m_dofSettingsId = INVALID_STRING_ID_64;
	m_lastForcedDofSettingsId = INVALID_STRING_ID_64;
	m_blurScale = s_dofDefaultBlurScale;
	m_forceDofBlendTime = 0.0f;
	m_forceDofBlendTimeRemaining = 0.0f;
	out_targetAperture = s_dofDefaultFNumber;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControl::DOFUpdate(const CameraInstanceInfo& info)
{
	ASSERTF(m_flags.m_noDefaultDofBehavior || 
			m_prepareReachedFn == EngineComponents::GetNdFrameState()->m_gameFrameNumber || 
			IsPhotoMode(), 
			("Your camera doesn't call CameraControl::Prepare. Dof won't work!"));

	float blend = info.m_blend;

	DC::RenderSettings &rs = g_renderSettings[ConvertToScreenIndex(GetPlayerIndex())];

	// if you're spamming the zoom button and getting an assload of cameras on the stack, fine, let the DOF pop
	// workaround a crash in settings when we run out of priorities
	U32F priority = kCutsceneCameraPriority - Min(5, info.m_stackIndex);

	const DC::CameraDofSettings* pDofSettings = (m_dofSettingsId != INVALID_STRING_ID_64) ? ScriptManager::Lookup<DC::CameraDofSettings>(m_dofSettingsId) : nullptr;
	if (pDofSettings)
	{
		float focusDist = m_distToDofFocus;
		float filmWidth = pDofSettings->m_filmWidth;
		float dofFNumber = Max(m_aperture, 1.f);	// by default use minimum aperture
		float lensFocalLength = CalculateLensFocalLengthFromFov(m_location.GetFov(), filmWidth);
		float foregroundBlurScale = m_blurScale * pDofSettings->m_foregroundBlurScale;
		float backgroundBlurScale = m_blurScale * pDofSettings->m_backgroundBlurScale;

		// Blend params not blended earlier (because they're not controlled fully by the camera) based on the force DoF persist time
		if (m_forceDofBlendTime > 0.f)
		{
			float forceDofBlend = m_forceDofBlendTimeRemaining / m_forceDofBlendTime;
			filmWidth = Lerp(s_dofDefaultFilmWidth, filmWidth, forceDofBlend);
			lensFocalLength = Lerp(s_dofDefaultFocalLength, lensFocalLength, forceDofBlend);
			foregroundBlurScale = Lerp(s_dofDefaultBlurScale, foregroundBlurScale, forceDofBlend);
			backgroundBlurScale = Lerp(s_dofDefaultBlurScale, backgroundBlurScale, forceDofBlend);
		}

		SettingSetDefault(&g_renderOptions.m_gameplayForceEnableDof, false);
		SettingSetPers(SID("gameplay-force-dof"), &g_renderOptions.m_gameplayForceEnableDof, true, priority, 1.f);

		SettingSet(&g_renderOptions.m_dofBlendFactor, 1.0f, priority, 1.0f, this);	// We want Background Blur disabled for non-zoom cameras when DoF is active
		SettingSet(&rs.m_post.m_dofFocusPlaneDist, focusDist, priority, blend, this);
		SettingSet(&rs.m_post.m_dofFilmWidth, filmWidth, priority, blend, this);
		SettingSet(&rs.m_post.m_dofFNumber, dofFNumber, priority, blend, this);
		SettingSet(&rs.m_post.m_dofLensFocalLength, lensFocalLength, priority, blend, this);
		SettingSet(&rs.m_post.m_dofForegroundBlurScale, foregroundBlurScale, priority, blend, this);
		SettingSet(&rs.m_post.m_dofBackgroundBlurScale, backgroundBlurScale, priority, blend, this);
		SettingSet(&rs.m_post.m_dofBackgroundBlurThresold, rs.m_post.m_dofBackgroundBlurThresold, priority, blend, this);
		SettingSet(&rs.m_post.m_dofForegroundBlurThresold, rs.m_post.m_dofForegroundBlurThresold, priority, blend, this);
	}
	else
	{
		if (m_bIsTopControl && !(blend < 1.0f))
		{
			// DoF needs to ease out globally, as the top level control when force DoF was invoked may not be the same when we're actually easing out DoF.
			// So this becomes a property of the top level control for the frame rather than being restricted to the control instance when DoF was forced.
			g_cameraOptions.m_forceDofFocusEaseOutPower = 1.0f;
		}

		float smoothBlend = Pow(blend, g_cameraOptions.m_forceDofFocusEaseOutPower);

		// set our settings to the current (ie the settings set up by render settings)
		if (m_bIsTopControl)
		{
			// Bring back background blur, depending on whether or not this camera had existing DoF that's being blended out, or is blending out another camera's DoF.
			// Use standard priority because we don't want to be overridden by an animated/zoom camera here if we're on top.
			U32F standardPriority = kCutsceneCameraPriority - info.m_stackIndex;
			SettingSet(&g_renderOptions.m_dofBlendFactor, (1.0f - blend), standardPriority, 1.0f, this);
		}
		SettingSet(&rs.m_post.m_dofFocusPlaneDist, rs.m_post.m_dofFocusPlaneDist, priority, smoothBlend, this);
		SettingSet(&rs.m_post.m_dofFilmWidth, rs.m_post.m_dofFilmWidth, priority, smoothBlend, this);
		SettingSet(&rs.m_post.m_dofFNumber, rs.m_post.m_dofFNumber, priority, smoothBlend, this);
		SettingSet(&rs.m_post.m_dofLensFocalLength, rs.m_post.m_dofLensFocalLength, priority, smoothBlend, this);
		SettingSet(&rs.m_post.m_dofForegroundBlurScale, rs.m_post.m_dofForegroundBlurScale, priority, smoothBlend, this);
		SettingSet(&rs.m_post.m_dofBackgroundBlurScale, rs.m_post.m_dofBackgroundBlurScale, priority, smoothBlend, this);
		SettingSet(&rs.m_post.m_dofBackgroundBlurThresold, rs.m_post.m_dofBackgroundBlurThresold, priority, smoothBlend, this);
		SettingSet(&rs.m_post.m_dofForegroundBlurThresold, rs.m_post.m_dofForegroundBlurThresold, priority, smoothBlend, this);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControl::UpdateDistToDofFocus(float desiredDist, float fov, const DC::CameraDofSettings* pDofSettings)
{
	static const F32 kMultEnd = 300.0f;
	static const F32 kMult = 45.0f;
	F32 focusSpeed = pDofSettings->m_focusSpeed * LerpScale(0.0f, kMultEnd, 1.0f, kMult, m_distToDofFocus);
	if (m_distToDofFocus < desiredDist)
	{
		m_distToDofFocus += Min(focusSpeed * GetProcessDeltaTime(), desiredDist - m_distToDofFocus);
	}
	else if (m_distToDofFocus > desiredDist)
	{
		m_distToDofFocus += Max(-focusSpeed * GetProcessDeltaTime(), desiredDist - m_distToDofFocus);
	}

	ASSERT(fov > 0.f);
	fov = Max(fov, 1.f);
	const float focalLengthMM = CalculateLensFocalLengthFromFov(fov, pDofSettings->m_filmWidth);
	const float focalLength = focalLengthMM / 1000.f;
	ALWAYS_ASSERT(IsFinite(focalLength));
	static const float kLensMinFocusDistance = 0.8f;
	static const float kMaxAperture = 0.8f;
	static const float kMinAperture = 22.f;
	ASSERT(kLensMinFocusDistance > focalLength);
	m_distToDofFocus = Max(focalLength + 0.01f, m_distToDofFocus);
}

/// --------------------------------------------------------------------------------------------------------------- ///

Err CameraControl::Init(const ProcessSpawnInfo& spawnInfo)
{
	m_dcSettingsId = INVALID_STRING_ID_64;

	const CameraControlSpawnInfo& info = *PunPtr<const CameraControlSpawnInfo*>(&spawnInfo);
	m_playerIndex = info.m_playerIndex;
	ALWAYS_ASSERT(m_playerIndex < kMaxCameraManagers);

	// set by CameraManager, prior to calling CameraStart()
	m_cameraId = kCameraNone; //@kCameraNone
	m_rank = CameraRank::Invalid;
	m_priority = CameraPriority::Class::kInvalid;

	m_nodeFlags.m_bits = 0;

	m_selfFadeOut.Reset();

	m_endTime = Seconds(0.0f);

	m_velocityPosition = kInvalidPoint;
	m_lastVelocityPosition = kInvalidPoint;
	m_velocity = kInvalidVector;

	// dof
	m_dofSettingsId = INVALID_STRING_ID_64;
	m_blurScale = s_dofDefaultBlurScale;
	m_distToDofFocus = 10.0f;
	m_aperture = s_dofDefaultFNumber;
	m_apertureSpring.Reset();

	// default initial location should match current camera
	m_location = CameraLocation(g_mainCameraInfo[m_playerIndex].GetLocator());

	Err result = ParentClass::Init(spawnInfo);
	if (result.Succeeded())
	{
		SetIgnoreResetActors(true);

		const Clock* pClock;
		if (GetRunWhenPaused())
			pClock = EngineComponents::GetNdFrameState()->GetClock(kCameraClock);
		else
			pClock = EngineComponents::GetNdFrameState()->GetClock(kGameClock);
		m_startTime = pClock->GetCurTime() - Seconds(pClock->GetDeltaTimeInSeconds());
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraControl::ReadBaseSettingsFromSpawner(DC::CameraBaseSettings& cameraSettings, const EntitySpawner* pSpawner) const
{
	const EntityDB * pDb = (pSpawner != nullptr) ? pSpawner->GetEntityDB() : nullptr;
	if (pDb)
	{
		cameraSettings.m_fov = pDb->GetData<float>(SID("fov"), cameraSettings.m_fov);
		cameraSettings.m_targetOffset = pDb->GetData<Vector>(SID("target-offset"), cameraSettings.m_targetOffset);
		cameraSettings.m_sourceOffset = pDb->GetData<Vector>(SID("source-offset"), cameraSettings.m_sourceOffset);
		cameraSettings.m_locator = pSpawner->GetWorldSpaceLocator();
		cameraSettings.m_3DDistortion = pDb->GetData<float>(SID("3D-distortion"), cameraSettings.m_3DDistortion);
		cameraSettings.m_3DZeroPlaneDist = pDb->GetData<float>(SID("3D-zero-plane-dist"), cameraSettings.m_3DZeroPlaneDist);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///

CameraBlendInfo CameraControl::CameraStart(const CameraStartInfo& startInfo)
{
	// Derived classes may override.

	m_spawnerId = startInfo.m_spawnerId;
	m_flags.m_debugFocusObj = false;

	m_cameraSpaceBlend = EngineComponents::GetNdGameInfo()->m_cameraSpaceBlend;
	m_cameraSpaceSpawnerId = EngineComponents::GetNdGameInfo()->m_cameraSpaceSpawner;
	m_cameraSpaceUseOverrideUp = EngineComponents::GetNdGameInfo()->m_cameraSpaceUseOverrideUp;
	m_cameraSpaceOverrideUpVec = EngineComponents::GetNdGameInfo()->m_cameraSpaceOverrideUpVec;

	const NdGameObject* pFocusObj = startInfo.GetFocusObject().ToProcess();
	m_lastFrameFocusBf = pFocusObj ? pFocusObj->GetBoundFrame() : BoundFrame(kIdentity);
	//m_blendFromTimeToDistProgress = 1.f;

	// read dof settings from render settings.
	m_aperture = GetRenderSettingsForPlayer(m_playerIndex).m_post.m_dofFNumber;
	m_apertureSpringConst = 100.f;
	m_apertureSpring.Reset();

	// Return a reasonable default blend.
	return Seconds(0.5f);
}

/// --------------------------------------------------------------------------------------------------------------- ///

bool CameraControl::IsEquivalentTo(const CameraStartInfo& startInfo) const
{
	if (m_spawnerId != startInfo.m_spawnerId
		|| (!m_flags.m_debugFocusObj && startInfo.GetFocusObject() != m_hFocusObj)
		|| startInfo.m_force
		//	||	startInfo.m_settingsId != m_dcSettingsId
		//	||	startInfo.m_locator != m_startLocator
		//	||	startInfo.m_fov != m_fov
		)
	{
		return false;
	}

	if (IsCameraSpaceBlendEnabled()
		&& (m_cameraSpaceSpawnerId != EngineComponents::GetNdGameInfo()->m_cameraSpaceSpawner
			|| !IsClose(m_cameraSpaceBlend, EngineComponents::GetNdGameInfo()->m_cameraSpaceBlend, 0.01f)))
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///

const Vector CameraControl::GetCameraUpVector(bool useOverrideUpVector) const
{
	Vector upDir(kUnitYAxis);

	if (IsCameraSpaceBlendEnabled() && m_cameraSpaceSpawnerId != INVALID_STRING_ID_64)
	{
		const EntitySpawner * pCameraSpaceSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByBareNameId(m_cameraSpaceSpawnerId);
		if (pCameraSpaceSpawner)
		{
			upDir = Slerp(upDir, GetLocalY(pCameraSpaceSpawner->GetBoundFrame().GetRotation()), Limit01(m_cameraSpaceBlend));
		}

		// sometimes designers don't use camear space up, because it remove tilted feeling.
		if (useOverrideUpVector && m_cameraSpaceUseOverrideUp)
		{
			upDir = m_cameraSpaceOverrideUpVec;
		}
	}

	return upDir;
}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraLocation CameraControl::StartLocation(const CameraControl* pPrevCamera) const
{
	return pPrevCamera ? pPrevCamera->GetCameraLocation() : CameraLocation();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CameraControl::IsDead(const CameraControl* pNextControl) const
{
	static const float kMinBlend = 1e-4f;

	// for zoom camera on the top of camera stack, we don't call Abandon() on it. But it self fade out, kill it.
	if (m_selfFadeOut.m_requested && m_selfFadeOut.m_currFade < kMinBlend)
		return true;

	if (m_flags.m_killWhenBlendedOut && m_normalBlend < kMinBlend && m_blend > 1.0f - kMinBlend)
	{
		if (pNextControl && pNextControl->IsFadeInByTravelDist() && pNextControl->GetBlend() < 1.f - kMinBlend)
			return false;

		// keep this camera even we push a zoom camera on top of next strafe camera, we can blend back.
		//if (//(IsFadeInByTravelDist() || IsFadeInByDistToObj()) &&
		//	GetPriority().GetClass() != CameraPriority::Class::kSpecial &&
		//	pNextControl && pNextControl->GetPriority().GetClass() == CameraPriority::Class::kSpecial)
		//	return false;

		return true;
	}

	if (m_flags.m_dontKillOnFocusDeath)
		return false;

	bool lostFocus = (m_flags.m_needsFocus && (m_hFocusObj.ToProcess() == nullptr));
	return lostFocus;
}

/// --------------------------------------------------------------------------------------------------------------- ///

bool CameraControl::IsManualCamera(bool excludeBottom) const
{
	if (IsType(SID("CameraControlManual")))
	{
		return (!excludeBottom || GetRank() != CameraRank::Bottom);
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///

I32 CameraControl::RankDifference(const CameraControl& other) const
{
	return m_rank.m_data - other.m_rank.m_data;
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraControl::KillWhenBlendedOut()
{
	m_flags.m_killWhenBlendedOut = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControl::RequestSelfFadeOut(TimeFrame fadeTime)
{
	if (m_selfFadeOut.m_requested)
		return;

	const Clock* pClock;
	if (GetRunWhenPaused())
		pClock = EngineComponents::GetNdFrameState()->GetClock(kCameraClock);
	else
		pClock = EngineComponents::GetNdFrameState()->GetClock(kGameClock);

	m_selfFadeOut.m_requested = true;

	// Do NOT set it to m_blend! Using m_blend might cause Camera pop.
	m_selfFadeOut.m_fadeWhenRequested = m_selfFadeOut.m_currFade;		// Remember this is a multiplier, which modifies the m_blend in range [0%, 100%].
	
	m_selfFadeOut.m_fadeTime = Seconds(LerpScale(0.5f, 1.0f, 0.5f, 1.0f, m_blend) * fadeTime.ToSeconds());
	m_selfFadeOut.m_startTime = pClock->GetCurTime();
	//MsgCon("Request Self Fade Out (fade/Time): %.2f %.2f\n", m_selfFadeOut.m_currFade, fadeTime.ToSeconds());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControl::CancelSelfFadeOut(TimeFrame fadeTime, bool useFadeTime)
{
	if (!m_selfFadeOut.m_requested)
		return;

	const Clock* pClock;
	if (GetRunWhenPaused())
		pClock = EngineComponents::GetNdFrameState()->GetClock(kCameraClock);
	else
		pClock = EngineComponents::GetNdFrameState()->GetClock(kGameClock);

	if (!useFadeTime && 
		GetBlendInfo().m_type == CameraBlendType::kTime)
	{
		fadeTime = Seconds(GetBlendInfo().m_time.ToSeconds());
	}

	m_selfFadeOut.m_requested = false;

	// Do NOT set it to m_blend! Using m_blend might cause Camera pop.
	m_selfFadeOut.m_fadeWhenRequested = m_selfFadeOut.m_currFade;		// Remember this is a multiplier, which modifies the m_blend in range [0%, 100%].

	m_selfFadeOut.m_fadeTime = Seconds(LerpScale(0.5f, 1.0f, 0.5f, 1.0f, 1.0f-m_blend) * fadeTime.ToSeconds());
	m_selfFadeOut.m_startTime = pClock->GetCurTime();

	//MsgCon("Cancel Self Fade Out (fade/Time): %.2f %.2f\n", m_selfFadeOut.m_currFade, fadeTime.ToSeconds());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControl::RemoveSelfFadeOut(TimeFrame fadeTime)
{
	if (!m_selfFadeOut.m_requested || m_nodeFlags.m_dead)	// We won't remove self-fade out if camera's death time is already decided
		return;

	const Clock* pClock;
	if (GetRunWhenPaused())
		pClock = EngineComponents::GetNdFrameState()->GetClock(kCameraClock);
	else
		pClock = EngineComponents::GetNdFrameState()->GetClock(kGameClock);

	m_selfFadeOut.m_requested = true;
	m_selfFadeOut.m_fadeWhenRequested = m_blend;
	m_selfFadeOut.m_fadeTime = fadeTime;
	m_selfFadeOut.m_startTime = pClock->GetCurTime();
	End(fadeTime);											// This will eventually result in setting the m_nodeFlags.m_dead to TRUE
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraControl::End(TimeFrame blendTime, CameraBlendType type)
{
	//MsgOut("Called CameraControl::End\n");

	m_deathBlend.m_time = blendTime;
	m_deathBlend.m_type = type;
	m_flags.m_dying = true;

	if (blendTime == Seconds(0))
	{
		const Clock* pClock;
		if (GetRunWhenPaused())
			pClock = EngineComponents::GetNdFrameState()->GetClock(kCameraClock);
		else
			pClock = EngineComponents::GetNdFrameState()->GetClock(kGameClock);
		m_endTime = pClock->GetCurTime();
		m_flags.m_dying = false;
		m_nodeFlags.m_dead = true;
		m_blend = 0.0f;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControl::FillDebugText(StringBuilder<1024>& sb, bool shortList, const DebugShowParams& params) const
{
	const CameraLocation& loc = m_location;
	Point pt = loc.GetLocator().GetTranslation();
	Quat rot = loc.GetLocator().GetRotation();
	const char* typeName = GetTypeName();
	const char* cameraIdName = GetCameraId().ToConfig().GetCameraIdName();
	const char* settingsName = DevKitOnly_StringIdToString(m_dcSettingsId);
	if (!settingsName || m_dcSettingsId == INVALID_STRING_ID_64)
	{
		if (m_spawnerId != INVALID_STRING_ID_64)
		{
			settingsName = DevKitOnly_StringIdToString(m_spawnerId);
		}
		else
		{
			settingsName = "no settings";
		}
	}

	if (shortList)
	{
		F32 timePassed = Min(GetStateTimePassed().ToSeconds(), m_blendInfo.m_time.ToSeconds());

		sb.format("%24.24s (%1.2f) [%2.2f/%2.2f] %4.1f (%s)\n",
			typeName,
			m_normalBlend,
			timePassed,
			m_blendInfo.m_time.ToSeconds(),
			loc.GetFov(),
			settingsName);
	}
	else
	{
		sb.format(" -- %24.24s [%5u] (%1.2f, %1.2f, %1.2f, %10.10s [%1.1s %1.2f])",
			cameraIdName,
			GetProcessId(),
			m_blend,
			m_normalBlend,
			m_selfFadeOut.m_currFade,
			CameraBlendTypeToString(m_blendInfo.m_type),
			m_blendInfo.m_locationBlendTime >= kTimeFrameZero ? "T" : " ",
			GetLocationBlend());

		if (params.printLocator)
		{
			sb.append_format(" (% 7.2f, % 7.2f, % 7.2f : % 4.2f, % 4.2f, % 4.2f, % 4.2f)",
				(float)pt.X(), (float)pt.Y(), (float)pt.Z(),
				(float)rot.X(), (float)rot.Y(), (float)rot.Z(), (float)rot.W());
		}

		sb.append_format(" %4.1f %5.2f %4.1f %4.1f (%s)%s\n",
			loc.GetFov(),
			loc.GetNearPlaneDist(),
			loc.GetDistortion(),
			loc.GetZeroPlaneDist(),
			settingsName,
			(IsPhotoMode() ? " (photo)" : ""));
	}

	if (params.showRequestFunc && m_func != nullptr)
	{
		sb.append_format("                %s, line:%d\n", m_func, m_line);
	}
}

// static float ReverseCalculateAperture(float coc, float distance, float focalDist, float f, float pixelFilmRatio)
// {
// 	float a = (distance - focalDist) / distance * f * f;
// 	float b = pixelFilmRatio / coc * a;
// 	float aperture = b / (focalDist - f);
// 	return aperture;
// }

static float CalculateCocRadius(float N, float distance, float focusDist, float f, float pixelFilmRatio)
{
	float result = (distance - focusDist) / distance * f * f / (N * (focusDist - f)) * pixelFilmRatio;
	return result;
}

static void CalculateNearFarPlane(float coc, float N, float focusDist, float focalLength, float pixelFilmRatio, float* outNearPlane, float* outFarPlane)
{
	{
		float B = (-coc * (N * (focusDist - focalLength))) / (focalLength * focalLength * pixelFilmRatio);
		float C = 1 - B;
		*outNearPlane = focusDist / C;

		float cocNear = CalculateCocRadius(N, *outNearPlane, focusDist, focalLength, pixelFilmRatio);
		ASSERT(abs(cocNear + coc) < 0.0001f);
	}

	{
		float B = (coc * (N * (focusDist - focalLength))) / (focalLength * focalLength * pixelFilmRatio);
		float C = 1 - B;
		*outFarPlane = focusDist / C;
		float cocFar = CalculateCocRadius(N, *outFarPlane, focusDist, focalLength, pixelFilmRatio);
		ASSERT(abs(cocFar - coc) < 0.0001f);
	}
}

// static void CalculateApertureAndFocusDistFromNearFarPlane(float cocNear, float cocFar, float pixelFilmRatio, float focalLength, float nearPlane, float farPlane,
// 	float* outAperture, float* outFocusDist)
// {
// 	// Aperture and focusDist are return values.
// 	// f = focalLength
// 	// 1) (-cocNear / pixelRatio) = ((near - focusDist) * f * f) / (nearPlane * (A * (focusDist - f)))
// 	// 2) (cocFar / pixelRatio) = ((far - focusDist) * f * f) / (farPlane * (A * (focusDist - f)))

// 	// 3) (-cocNear * nearPlane) / (pixelRatio * f * f) = (nearPlane - focusDit) / (A * (focusDist - f))
// 	// 4) (cocFar * farPlane) / (pixelRatio * f * f) = (farPlane - focusDist) / (A * (focusDist - f))

// 	// 3) / 4) =>
// 	// (-cocNear * nearPlane) / (cocFar * farPlane) = (nearPlane - focusDist) / (farPlane - focusDist)
// 	// => focusDist = (-cocNear - cocFar) * nearPlane * farPlane / (-cocNear * near - cocFar * farPlane)
// 	ASSERT(nearPlane > 0.f);
// 	ASSERT(farPlane > 0.f);

// 	float negativeCocNear = -cocNear;

// 	float focusDist = (negativeCocNear - cocFar) * nearPlane * farPlane / (negativeCocNear * nearPlane - cocFar * farPlane);
// 	ASSERT(focusDist > nearPlane && focusDist < farPlane);

// 	float apertureNear = ReverseCalculateAperture(negativeCocNear, nearPlane, focusDist, focalLength, pixelFilmRatio);
// 	float apertureFar = ReverseCalculateAperture(cocFar, farPlane, focusDist, focalLength, pixelFilmRatio);

// 	float coc0 = CalculateCocRadius(apertureNear, nearPlane, focusDist, focalLength, pixelFilmRatio);
// 	float coc1 = CalculateCocRadius(apertureFar, farPlane, focusDist, focalLength, pixelFilmRatio);

// 	//g_prim.Draw(DebugString2D(Vec2(0.4f, 0.4f), kDebug2DNormalizedCoords, StringBuilder<128>("near:%.3f, far:%.3f, focusDist:%.3f, a0:%.3f, a1:%.3f", nearPlane, farPlane, focusDist, apertureNear, apertureFar).c_str(), kColorWhite, 0.6f), kPrimDuration1FramePauseable);

// 	ASSERT(abs(coc0 + cocNear) < 0.0001f);
// 	ASSERT(abs(coc1 - cocFar) < 0.0001f);

// 	*outAperture = apertureNear;
// 	*outFocusDist = focusDist;
// }

static void DrawDofPlane(const Locator& gameCamLoc, const float dist, const Color color, const char* label)
{
	{
		const Point focusPoint0 = gameCamLoc.TransformPoint(Point(1.f, 1.f, dist));
		const Point focusPoint1 = gameCamLoc.TransformPoint(Point(1.f, -1.f, dist));
		const Point focusPoint2 = gameCamLoc.TransformPoint(Point(-1.f, -1.f, dist));
		const Point focusPoint3 = gameCamLoc.TransformPoint(Point(-1.f, 1.f, dist));

		g_prim.Draw(DebugLine(focusPoint0, focusPoint1, color, 2.f, PrimAttrib(kPrimEnableHiddenLineAlpha)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(focusPoint1, focusPoint2, color, 2.f, PrimAttrib(kPrimEnableHiddenLineAlpha)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(focusPoint2, focusPoint3, color, 2.f, PrimAttrib(kPrimEnableHiddenLineAlpha)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(focusPoint3, focusPoint0, color, 2.f, PrimAttrib(kPrimEnableHiddenLineAlpha)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(focusPoint0, focusPoint2, color, 2.f, PrimAttrib(kPrimEnableHiddenLineAlpha)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(focusPoint1, focusPoint3, color, 2.f, PrimAttrib(kPrimEnableHiddenLineAlpha)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugString(focusPoint0, label, color, 0.5f), kPrimDuration1FramePauseable);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControl::DebugDof(float targetDist, float fov, bool bFlipReticleOffsetX /* = false */) const
{
	// The Manual camera draws very confusing information here, namely planes aligned with the game cam's planes with (sometimes) inverted near and far planes.
	// If debug drawing is necessary for it, first fix how/what it draws.
	if (IsManualCamera())
		return;

	if (m_dofSettingsId != INVALID_STRING_ID_64)
	{
		const DC::CameraDofSettings* pDofSettings = ScriptManager::Lookup<DC::CameraDofSettings>(m_dofSettingsId);

		if (FALSE_IN_FINAL_BUILD(g_debugCameraFocusDistance))
		{
			const Locator& gameCamLoc = GameGlobalCameraLocator(GetPlayerIndex());

			if (pDofSettings->m_trackNpcs)
			{
				Vec2 reticleOffset1080p(kZero);
				if (const DC::ReticleOffsetDef1080p *pReticleOffset = GetReticleOffsetDef1080p())
				{
					reticleOffset1080p.x = pReticleOffset->m_offsetX;
					reticleOffset1080p.y = pReticleOffset->m_offsetY;

					if (bFlipReticleOffsetX)
						reticleOffset1080p.x *= -1.f;
				}

				Vec2 centerPos(1920.0f, 1080.0f);
				centerPos += reticleOffset1080p * 2.0f;
				g_prim.Draw(DebugCircle2D(centerPos, pDofSettings->m_trackNpcRadius, 1.0f, kDebug2DCanonicalCoords, kColorOrange));
			}

			g_prim.Draw(DebugString2D(Vec2(0.2f, 0.4f), kDebug2DNormalizedCoords,
				StringBuilder<256>("%s\nf-number (1/aperture):%0.2f\nforeground-blur-scale:%0.2f\nbackground-blur-scale:%0.2f", DevKitOnly_StringIdToString(m_dofSettingsId), pDofSettings->m_fNumber, pDofSettings->m_foregroundBlurScale, pDofSettings->m_backgroundBlurScale).c_str(),
				kColorWhite, 0.6f), kPrimDuration1FramePauseable);

			const USize displaySize = g_display.GetDisplayUSize();
			const float pixelFilmRatio = displaySize.m_w / (pDofSettings->m_filmWidth / 1000.0f);
			// draw focus plane in world.
			DrawDofPlane(gameCamLoc, m_distToDofFocus, kColorGreenTrans, StringBuilder<64>("dof focus-plane:%0.2f", m_distToDofFocus).c_str());

			const float focalLengthMM = CalculateLensFocalLengthFromFov(fov, pDofSettings->m_filmWidth);
			const float focalLength = focalLengthMM / 1000.f;

			{
				float coc1NearPlane;
				float coc1FarPlane;
				CalculateNearFarPlane(1.f, m_aperture, m_distToDofFocus, focalLength, pixelFilmRatio, &coc1NearPlane, &coc1FarPlane);

				float nearCoc = CalculateCocRadius(m_aperture, coc1NearPlane, m_distToDofFocus, focalLength, pixelFilmRatio);
				float farCoc = CalculateCocRadius(m_aperture, coc1FarPlane, m_distToDofFocus, focalLength, pixelFilmRatio);

				//g_prim.Draw(DebugString2D(Vec2(0.4f, 0.4f), kDebug2DNormalizedCoords, StringBuilder<64>("near-plane-coc:%.3f, far-plane-coc:%.3f", abs(nearCoc), abs(farCoc)).c_str(), kColorWhite, 0.6f), kPrimDuration1FramePauseable);

				DrawDofPlane(gameCamLoc, coc1NearPlane, kColorYellowTrans, StringBuilder<64>("coc1-near:%0.2f", coc1NearPlane).c_str());
				DrawDofPlane(gameCamLoc, coc1FarPlane, kColorYellowTrans, StringBuilder<64>("coc1-far:%0.2f", coc1FarPlane).c_str());

				Vec2 normXY(0.2f, 0.5f);
				Vec2 normWidthHeight(0.3f, 0.3f);

				DebugMemoryPlot("target-dist", targetDist, &m_debugDistPlot, 60, 0.f, 100.f, normXY, normWidthHeight, kColorCyan, DebugPlot::kDebugFloatDrawFrame | DebugPlot::kDebugFloatDisplayPauseable);
				DebugMemoryPlot("focus-plane", m_distToDofFocus, &m_debugFocusDistPlot, 60, 0.f, 100.f, normXY, normWidthHeight, kColorYellow, DebugPlot::kDebugFloatDisplayPauseable | DebugPlot::kDebugFloatDrawFrame | DebugPlot::kDebugFloatDisplayPauseable);

				{
					float prevCoc = CalculateCocRadius(m_aperture, 1.f, m_distToDofFocus, focalLength, pixelFilmRatio);

					const float xScale = 0.003f;
					const float yScale = 0.03f;
					const float xStart = 0.6f;
					const float yStart = 0.5f;
					for (U32F ii = 2; ii < 100; ii++)
					{
						float testDist = (float)ii;
						float testCoc = CalculateCocRadius(m_aperture, testDist, m_distToDofFocus, focalLength, pixelFilmRatio);

						Vec2 prevPt(xStart + (ii - 1) * xScale, yStart - prevCoc * yScale);
						Vec2 currPt(xStart + ii * xScale, yStart - testCoc * yScale);

						g_prim.Draw(DebugLine2D(prevPt, currPt, kDebug2DNormalizedCoords, kColorRed), kPrimDuration1FramePauseable);
						prevCoc = testCoc;

						if (ii % 10 == 0)
						{
							g_prim.Draw(DebugString2D(currPt, kDebug2DNormalizedCoords, StringBuilder<64>("%dm", ii).c_str(), kColorGreen, 0.35f), kPrimDuration1FramePauseable);
						}
					}

					g_prim.Draw(DebugLine2D(Vec2(xStart, yStart), Vec2(xStart + 100 * xScale, yStart), kDebug2DNormalizedCoords, kColorWhite), kPrimDuration1FramePauseable);
					g_prim.Draw(DebugString2D(Vec2(xStart + 100 * xScale, yStart), kDebug2DNormalizedCoords, "coc:0.0", kColorWhite, 0.4f), kPrimDuration1FramePauseable);
					g_prim.Draw(DebugLine2D(Vec2(xStart, yStart - 1.f * yScale), Vec2(xStart + 100 * xScale, yStart - 1.f * yScale), kDebug2DNormalizedCoords, kColorWhite), kPrimDuration1FramePauseable);
					g_prim.Draw(DebugString2D(Vec2(xStart + 100 * xScale, yStart - 1.f * yScale), kDebug2DNormalizedCoords, "coc:+1.0", kColorWhite, 0.4f), kPrimDuration1FramePauseable);
					g_prim.Draw(DebugLine2D(Vec2(xStart, yStart + 1.f * yScale), Vec2(xStart + 100 * xScale, yStart + 1.f * yScale), kDebug2DNormalizedCoords, kColorWhite), kPrimDuration1FramePauseable);
					g_prim.Draw(DebugString2D(Vec2(xStart + 100 * xScale, yStart + 1.f * yScale), kDebug2DNormalizedCoords, "coc:-1.0", kColorWhite, 0.4f), kPrimDuration1FramePauseable);
				}
			}

			g_prim.Draw(DebugString2D(Vec2(0.4f, 0.42f), kDebug2DNormalizedCoords, StringBuilder<64>("fov:%.2fdeg, lens-focal-length:%.2fmm", fov, focalLengthMM).c_str(), kColorWhite, 0.6f), kPrimDuration1FramePauseable);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControl::DebugShow(bool shortList, const DebugShowParams& params) const
{
	StringBuilder<1024> sb;
	FillDebugText(sb, shortList, params);

	if (shortList)
	{
		Color desColor(m_blend * 0.5f, m_blend, m_blend * 0.5f);
		SetColor(kMsgCon, desColor);
	}

	MsgCon(sb.c_str());

	if (shortList)
		SetColor(kMsgCon, kColorWhite);
}

/// --------------------------------------------------------------------------------------------------------------- ///

void CameraControl::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	const Locator& loc = m_location.GetLocator();
	StringId64 settingsId = GetDCSettingsId();
	StringBuilder<128> str("%d %s %s", this->m_handleId, m_cameraId.ToConfig().GetCameraIdName(), DevKitOnly_StringIdToStringOrNull(settingsId));
	g_prim.Draw(DebugCoordAxesLabeled(loc, str.c_str()));
}

/// --------------------------------------------------------------------------------------------------------------- ///

const char * CameraControl::DevKitOnly_GetSpawnerName() const
{
	if (m_spawnerId != INVALID_STRING_ID_64)
		return DevKitOnly_StringIdToString(m_spawnerId);
	else
		return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float CameraControl::Ease(float tt)
{
	const float tension = 0.75f;
	const float pio4 = PI * 0.25f;
	float r = pio4*tension;
	float bot = sin(pio4-r);
	float top = sin(pio4+r);
	return LerpScale(bot, top, 0.0f, 1.0f, sin(Lerp(pio4 - r,  pio4 + r, tt)));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControl::UpdateBlendByTime(const CameraControl* pPrevControl, F32 distToSpeedMult)
{
	const Clock* pClock;
	if (GetRunWhenPaused())
		pClock = EngineComponents::GetNdFrameState()->GetClock(kCameraClock);
	else
		pClock = EngineComponents::GetNdFrameState()->GetClock(kGameClock);
	float blendProgress;
	float timeBasis;
	float blendTime;
	float timePassed;

	F32 distMult = 1.0f;

	if (distToSpeedMult > 0.0f)
	{
		const NdGameObject* pFocusGo = GetFocusObject().ToProcess();
		GAMEPLAY_ASSERT(pFocusGo);
		Vector movement(kZero);
		if (pFocusGo->GetBinding().IsSameBinding(m_lastFrameFocusBf.GetBinding()))
		{
			movement = pFocusGo->GetTranslationPs() - m_lastFrameFocusBf.GetTranslationPs();
		}

		Vector toChar = pFocusGo->GetTranslation() - GameCameraLocator().GetTranslation();
		F32 dot = Dot(toChar, movement);
		// only care about movement away from the camera

		if (dot > 0.0f)
		{
			Vector cameraSide = GetLocalX(GameCameraLocator().GetRotation());
			m_focusTargetTravelDist += Abs(Dot(movement, cameraSide));
		}

		m_lastFrameFocusBf = pFocusGo->GetBoundFrame();
		distMult += distToSpeedMult * m_focusTargetTravelDist;
		//MsgCon("Focus Travel Dist: %.2f\n", m_focusTargetTravelDist);
	}

	if (pPrevControl && pPrevControl->IsStaticCamera() && !IsStaticCamera())
	{
		// make sure we blend quick enough to prevent a 180 degree swap
		Vector prevCamFacing = SafeNormalize(VectorXz(GetLocalZ(pPrevControl->GetLocator().GetRotation())), kZero);
		Vector thisCamFacing = SafeNormalize(VectorXz(GetLocalZ(GetLocator().GetRotation())), kZero);
		F32 dot = Dot(prevCamFacing, thisCamFacing);
		F32 angle = Abs(RADIANS_TO_DEGREES(Acos(dot)));
		F32 minMult = LerpScale(30.0f, 120.0f, 1.0f, 2.0f, angle);
		distMult = Max(minMult, distMult);
	}

	timePassed = ToSeconds(pClock->GetDeltaTimeFrame());

	if (m_endTime != TimeFrameZero())
	{
		timeBasis = Max(0.0f, ToSeconds(pClock->GetTimeUntil(m_endTime)));
		blendTime = ToSeconds(m_deathBlend.m_time) / distMult;
	}
	else
	{
		timeBasis = Max(0.0f, ToSeconds(pClock->GetTimePassed(m_startTime))); //*JQG m_endTime was 0, timeBasis is 4.56 when blend is ~0.2
		blendTime = ToSeconds(m_blendInfo.m_time) / distMult; //*JQG blendTime was 0
	}

	if (m_blendInfo.m_instantBlend)
		timeBasis = Max(timeBasis, blendTime);

	if (blendTime > 0.0f)
		blendProgress = Ease(MinMax01(timeBasis / blendTime));
	else if (m_nodeFlags.m_dead)
		blendProgress = 0;
	else
		blendProgress = 1; //*JQG gets here every frame

	if (m_flags.m_dying) //*JQG not dying
	{
		m_endTime = pClock->GetCurTime() + m_deathBlend.m_time;
		m_flags.m_dying = false;
		m_nodeFlags.m_dead = true;
	}

	CameraBlendType blendType = m_nodeFlags.m_dead ? m_deathBlend.m_type : m_blendInfo.m_type;

	// calc final m_blend based on blend type (fade, accelerate or "cut")
	if (blendTime > 0.0f)
	{
		if (blendType == CameraBlendType::kAccelerateEase && blendTime > 0.0f)
		{
			blendProgress = LerpScale(0.15f, 1.0f, 0.0f, 1.0f, blendProgress);

			m_blend = QuadCurveEaseInOutUp(blendProgress);
			// 			float x2 = blendProgress * blendProgress;
			// 			float x3 = blendProgress * x2;
			// 			float x4 = blendProgress * x3;
			// 			m_blend = -4*x4 + 6*x3 - x2;
		}
		else
		{
			m_blend = QuadCurveEaseInOutUp.Evaluate(blendProgress);
		}
	}
	else // blendTime == 0.0f (CameraBlendType::Cut)
	{
		float newBlend;
		if (m_endTime != TimeFrameZero())
			newBlend = (blendProgress > 0.0f) ? 1.0f : 0.0f;
		else
			newBlend = (blendProgress == 1.0f) ? 1.0f : 0.0f; //*JQG comes in here

		// reset the smoother on cuts
		/*if (newBlend != m_blend)
		{
			CameraLocation loc;
			if ((newBlend == 0.0f) && pPrevControl)
				loc = pPrevControl->m_location;
			else
				loc = m_location;
		}*/
		m_blend = newBlend; //*JQG m_blend is always 1.0 -- wtf???
	}

	// will not work well at all with seperate location blend
	if (m_blend == 1.0f && m_blendInfo.m_type == CameraBlendType::kCrossFade)
	{
		m_blendInfo.m_type = CameraBlendType::kTime;
	}
}

void CameraControl::UpdateBlendByScript(const CameraControl* pNextControl)
{
	if (m_blendInfo.m_type == CameraBlendType::kScript)
	{
		static const float kMinBlend = 1e-4f;
		if (m_blend == 1.0f)
		{
			// when dist camera is fully blended in, it can become a fully blended in time camera, so we can kill it safely
			ResetToTimeBasedBlend();
		}
		else if (pNextControl)
		{
			if (pNextControl->GetRank() != CameraRank::Debug && m_normalBlend < kMinBlend)
			{
				// when there's a next camera, and this dist camera is not contributing anymore, we can kill it safely.
				ResetToTimeBasedBlend();
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControl::UpdateBlend(const CameraControl* pPrevControl, const CameraNode* pNextNode)
{
	const NdGameObject* pFocusObj = GetFocusObject().ToProcess();

	float prevNormalBlend = m_normalBlend;

	bool photoModeActive = EngineComponents::GetNdPhotoModeManager() && EngineComponents::GetNdPhotoModeManager()->IsActive();
	if (pFocusObj != nullptr && IsBlendControlledByScript())
	{
		const CameraControl* pNextControl = pNextNode ? pNextNode->m_handle.ToProcess() : nullptr;
		UpdateBlendByScript(pNextControl);
	}
	else
	{
		UpdateBlendByTime(pPrevControl, m_blendInfo.m_distToBlendSpeedMult);
	}

	// update location blend
	UpdateLocationBlendByTime();

	UpdateSelfFade();
	m_blend *= m_selfFadeOut.m_currFade;
	m_blend = MinMax01(m_blend);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControl::UpdateNormalBlend(float contributeBlend, const CameraNode* pNextNode, float contributeDistWeight)
{
	float normalBlend = m_blend * contributeBlend;
	if (contributeDistWeight >= 0.f)
		normalBlend *= contributeDistWeight;
	m_normalBlend = MinMax01(normalBlend);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraControl::ResetToTimeBasedBlend()
{
	m_blendInfo.m_type = CameraBlendType::kTime;
	if (m_blendInfo.m_time <= Seconds(0))
		m_blendInfo.m_time = Seconds(0.5f);
}

///--------------------------------------------------------------------------------------///
void CameraControl::UpdateSelfFade()
{
	if (m_selfFadeOut.m_startTime > Seconds(0.f))
	{
		const Clock* pClock;
		if (GetRunWhenPaused())
			pClock = EngineComponents::GetNdFrameState()->GetClock(kCameraClock);
		else
			pClock = EngineComponents::GetNdFrameState()->GetClock(kGameClock);

		float timeBasis = Max(0.0f, ToSeconds(pClock->GetTimePassed(m_selfFadeOut.m_startTime)));
		float blendTime = m_selfFadeOut.m_fadeTime >= Seconds(0.f) ? m_selfFadeOut.m_fadeTime.ToSeconds() : 1.f;

		//MsgCon(" Self Fade (time basis / start Fade): %.2f %.2f\n", timeBasis, blendTime);
		//MsgCon(" fade when requested %.2f\n", m_selfFadeOut.m_fadeWhenRequested);
		if (m_selfFadeOut.m_requested)
		{
			float blendProgress;
			if (blendTime > 0.0f)
			{
				blendProgress = Ease(MinMax01(timeBasis / blendTime));
			}
			else
			{
				blendProgress = 1.0f;
			}

			//MsgCon(" fadeoutrequested: blend progress: %.2f\n", blendProgress);

			blendProgress = LerpScale(0.0f, 1.0f, 1.0f, 0.f, blendProgress);

			//MsgCon(" fadeoutrequested: blend progress (clamped/reversed): %.2f\n", blendProgress);

			//MsgCon(" fadeoutrequested: blend progress (quad curve): %.2f\n", MinMax01(QuadCurveEaseInOutUp(blendProgress)));

			m_selfFadeOut.m_currFade = LerpScaleClamp(1.0f, 0.0f, m_selfFadeOut.m_fadeWhenRequested, 0.0f, QuadCurveEaseInOutUp(blendProgress));

			//MsgCon(" fadeoutrequested: final self fade: %.2f\n", m_selfFadeOut.m_currFade);
			// 			float x2 = blendProgress * blendProgress;
			// 			float x3 = blendProgress * x2;
			// 			float x4 = blendProgress * x3;
			// 			m_blend = -4*x4 + 6*x3 - x2;
		}
		else
		{
			float blendProgress;
			if (blendTime > 0.0f)
			{
				blendProgress = Ease(MinMax01(timeBasis / blendTime));
			}
			else
			{
				blendProgress = 1.0f;
			}

			//MsgCon(" fadeoutcanceled: blend progress: %.2f\n", blendProgress);
			m_selfFadeOut.m_currFade = LerpScaleClamp(0.f, 1.f, m_selfFadeOut.m_fadeWhenRequested, 1.f, blendProgress);
			//MsgCon(" fadeoutcanceled: final self fade: %.2f\n", m_selfFadeOut.m_currFade);
		}
	}
}

void CameraControl::SetDebugFocusObject(const NdGameObject* pFocusObj)
{
	//MsgCamera("Changing focus object from %s to %s\n",
	//	(GetFocusObject()) ? GetFocusObject()->GetName() : "<null>",
	//	(pFocusObj) ? pFocusObj->GetName() : "<null>");

	m_hFocusObj = pFocusObj;
	m_flags.m_debugFocusObj = (pFocusObj != EngineComponents::GetNdGameInfo()->GetGlobalPlayerGameObject(GetPlayerIndex()));
}

///-----------------------------------------------------------------------------------------------///
const DC::ReticleOffsetDef1080p* CameraControl::GetReticleOffsetDef1080p()
{
	static ScriptPointer<DC::ReticleOffsetDef1080p> s_pNetReticleOffset(SID("*net-reticle-offset*"), SID("weapon-gameplay"));
	static ScriptPointer<DC::ReticleOffsetDef1080p> s_pReticleOffset(SID("*reticle-offset*"), SID("weapon-gameplay"));

	return g_ndConfig.m_pNetInfo->IsNetActive() ? (const DC::ReticleOffsetDef1080p*)s_pNetReticleOffset
												: (const DC::ReticleOffsetDef1080p*)s_pReticleOffset;
}

///-----------------------------------------------------------------------------------------------///
Vector CameraControl::GetReticleDirFromCameraDir(Vector_arg cameraDir, Vector_arg cameraUp, float fovDegrees, float reticleOffset1080y)
{
	// cameraDir is the camera center direction.
	const float pitchRad = GetPitchRad(fovDegrees, reticleOffset1080y);
	const Vector sideDir = SafeNormalize(Cross(cameraDir, cameraUp), kUnitXAxis);
	const Vector reticleDir = Rotate(QuatFromAxisAngle(sideDir, -pitchRad), cameraDir);
	return reticleDir;
}

///-----------------------------------------------------------------------------------------------///
Vector CameraControl::GetReticleDirFromCameraDir(Vector_arg cameraDir, Vector_arg cameraUp, float fovDegrees)
{
	// cameraDir is the camera center direction.
	Vec2 reticleOffset1080p(0.f, 0.f);
	if (const DC::ReticleOffsetDef1080p *pReticleOffset = GetReticleOffsetDef1080p())
		reticleOffset1080p = Vec2(pReticleOffset->m_offsetX, pReticleOffset->m_offsetY);

	return GetReticleDirFromCameraDir(cameraDir, cameraUp, fovDegrees, reticleOffset1080p.Y());
}

///-----------------------------------------------------------------------------------------------///
Vector CameraControl::GetCameraDirFromReticleDir(Vector_arg reticleDir, Vector_arg cameraUp, float fovDegrees)
{
	// cameraDir is the camera center direction.
	float reticleOffset1080y = 0.f;
	if (const DC::ReticleOffsetDef1080p *pReticleOffset = GetReticleOffsetDef1080p())
		reticleOffset1080y = pReticleOffset->m_offsetY;

	const float pitchRad = GetPitchRad(fovDegrees, reticleOffset1080y);
	const Vector sideDir = SafeNormalize(Cross(reticleDir, cameraUp), kUnitXAxis);
	const Vector cameraDir = Rotate(QuatFromAxisAngle(sideDir, pitchRad), reticleDir);
	return cameraDir;
}

///-----------------------------------------------------------------------------------------------///
/// Look-At
///-----------------------------------------------------------------------------------------------///
CameraControl::LookAtError CameraControl::CalcLookAtError(Point_arg targetPosWs, const Locator& refCamLoc) const
{
	Vector cameraDir = GetLocalZ(refCamLoc.GetRotation());
	Vector toTarget = targetPosWs - refCamLoc.GetTranslation();

	const float yawError = (AngleFromUnitXZVec(toTarget) - AngleFromUnitXZVec(cameraDir)).ToDegrees();
	const float pitchError = (Angle(RADIANS_TO_DEGREES(Atan2(toTarget.Y(), LengthXz(toTarget)))) - Angle(RADIANS_TO_DEGREES(Atan2(cameraDir.Y(), LengthXz(cameraDir))))).ToDegrees();

	LookAtError result;
	result.m_error = Vec2(yawError, pitchError);
	result.m_reachPitchLimit = false;
	return result;
}

//------------------------------------------------------------------------------------//
void CameraControlForceDofSettings(StringId64 forceDofSettingsId, float springConst, float persistTime, const BoundFrame& focusTargetBf, float blurScale, float easeOutPower)
{
	if (g_cameraOptions.m_disableForcedDofThisFrame)
	{
		return;
	}

	SettingSetDefault(&g_cameraOptions.m_forceDofSettingsId, INVALID_STRING_ID_64);
	SettingSetPers(SID("camera-control-force-dof-settings"), &g_cameraOptions.m_forceDofSettingsId, forceDofSettingsId, kPlayerModePriority, 1.0f);

	g_cameraOptions.m_forceDofFocusPersistTime = persistTime;
	g_cameraOptions.m_forceDofFocusApertureSpringConst = springConst;
	g_cameraOptions.m_forceDofFocusTargetBf = focusTargetBf;
	g_cameraOptions.m_forceDofBlurScale = blurScale;
	g_cameraOptions.m_forceDofFocusEaseOutPower = easeOutPower;
}

//------------------------------------------------------------------------------------//
float CalculateLensFocalLengthFromFov(float fovDeg, const float filmWidthMM)
{
	const float focalLengthMM = filmWidthMM / (Tan(DEGREES_TO_RADIANS(fovDeg) / 2.f) * 2.f);
	return focalLengthMM;
}

//------------------------------------------------------------------------------------//
float CalculateFovFromLensFocalLength(float focalLengthMM, const float filmWidthMM)
{
	const float fovDeg = RADIANS_TO_DEGREES(2.f * Atan2(filmWidthMM, (2.f * focalLengthMM)));
	return fovDeg;
}
