/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef GAMELIB_CAMERA_CONTROL_H
#define GAMELIB_CAMERA_CONTROL_H

#include "corelib/memory/relocate.h"
#include "corelib/util/angle.h"

#include "ndlib/camera/camera-location.h"
#include "ndlib/camera/camera-start-info.h"
#include "ndlib/camera/camera-blend.h"
#include "ndlib/camera/camera-config.h"
#include "ndlib/script/script-manager.h"

#include "ndlib/process/event.h"
#include "ndlib/process/spawn-info.h"

#include "gamelib/camera/camera-joypad.h"
#include "gamelib/camera/camera-look-at-controller.h"
#include "gamelib/ndphys/vehicle/phys-vehicle-winch.h"

class EntitySpawner;
class CameraNode;
class CameraManager;
class CameraControl;
namespace DC
{
	struct CameraBaseSettings;
	struct CameraDofSettings;
	struct ReticleOffsetDef1080p;
}

struct CameraInstanceInfo
{
public:
	CameraLocation m_prevLocation;
	bool m_topControl;
	CameraJoypad m_joyPad;
	I32 m_stackIndex;
	float m_blend;
	float m_locationBlend;
	float m_normalBlend;
	const CameraControl* m_prevControl;
	const CameraControl* m_nextControl;
	const CamWaterPlane* m_pWaterPlane;	// water plane, used for camera collision.
	const CamLookAtTargetPos* m_pTargetPos;	// look-at target from look-at-controller

	CameraInstanceInfo() { }

	CameraInstanceInfo(
		const CameraLocation& loc, bool topControl, const CameraJoypad& joypad,
		float blend, float locationBlend, float normalBlend,
		const CameraControl* prevControl, const CameraControl* nextControl, I32 index)
		: m_prevLocation(loc)
		, m_topControl(topControl)
		, m_joyPad(joypad)
		, m_blend(blend)
		, m_locationBlend(locationBlend)
		, m_normalBlend(normalBlend)
		, m_prevControl(prevControl)
		, m_nextControl(nextControl)
		, m_stackIndex(index)
		, m_pWaterPlane(nullptr)
		, m_pTargetPos(nullptr)
	{}
};

struct CameraControlSpawnInfo : public SpawnInfo
{
public:
	U32 m_playerIndex;
	const CameraStartInfo* m_pStartInfo;

	explicit CameraControlSpawnInfo(StringId64 type,
								   Process* pParent = nullptr,
								   const Locator* pRoot = nullptr,
								   const void* pUserData = nullptr,
								   const char * pName = nullptr,
								   const char * pNamespace = nullptr,
								   const char * pFullName = nullptr) :
		SpawnInfo(type, pParent, pRoot, pUserData, pName, pNamespace, pFullName),
		m_playerIndex(0),
		m_pStartInfo(nullptr)
	{ }
};

class CameraControl : public Process
{
	typedef Process ParentClass;
	STATE_DECLARE_OVERRIDE(Active);

public:

	// Process Overrides
	CameraControl();
	virtual ~CameraControl() override;
	virtual Err Init(const ProcessSpawnInfo& spawnInfo) override;
	virtual void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void EventHandler(Event& event) override;
	virtual void OnKillProcess() override;

	virtual bool IsPhotoMode() const { return false; }
	// it's not pretty nasty to have this function, but let's ship the game.
	virtual bool IsReachingPitchLimit() const { return false; }

	// Initialization
	virtual CameraBlendInfo CameraStart(const CameraStartInfo& startInfo);			// be sure derived classes implement this
	virtual bool IsEquivalentTo(const CameraStartInfo& startInfo) const;
	virtual CameraLocation StartLocation(const CameraControl* pPrevCamera) const;	// calculate the start location using the location of the next camera

	// Per-Frame Update
	virtual void DOFUpdate(const CameraInstanceInfo& info);
	virtual void PrePlayerAnimUpdate(const CameraInstanceInfo& info) {}

	///---------------------------------------------------------------------------------------///
	/// Prepare
	///---------------------------------------------------------------------------------------///
	/// Prepare1: read joypad inputs and update desired location.
	virtual void Prepare(const CameraInstanceInfo& info) {}

	/// Prepare2: prepare the rest.
	virtual void PrepareFinalize(bool isTopControl, const CameraControl* pPrevControl);
	virtual void BlendDofSettingsToDefault(float &out_targetAperture);		// Blends forced DoF settings not dictated just by the render settings.
	virtual void SetDofSettingsToDefault(float &out_targetAperture);		// Sets forced DoF settings not dictated just by the render settings.

	virtual void UpdateCuts(const CameraInstanceInfo& info) {}
	virtual CameraLocation UpdateLocation(const CameraInstanceInfo& info) = 0;

	// Control
	virtual void	KillWhenBlendedOut();
	virtual void	End(TimeFrame blendTime = Seconds(0.0f), CameraBlendType type = CameraBlendType::kTime);
	virtual void	ForcePlayerCameraControl() {}
	virtual bool	UseCircularBlend(const CameraControl *pNextControl, bool *sideFirst) const { return false; }
	virtual void	SetDebugFocusObject(const NdGameObject* pFocusObj);
	void			SetRunWhenPaused(bool run) { m_flags.m_runWhenPaused = run; }
	virtual bool	AllowCameraLookAt() const { return true; }

	// does this camera allow lower cameras to adjust their joypad input for look at controller?
	bool			AllowLowerCameraLookAtJoypadAdjust() const { return IsFadeInByTravelDist() || IsFadeInByDistToObj(); }

	TimeFrame		GetStartTime() const { return m_startTime; }

	// Blending
	CameraBlendInfo	GetBlendInfo() const { return m_blendInfo; }
	void			SetBlendInfo(const CameraBlendInfo& blendInfo) { m_blendInfo = blendInfo; }
	virtual void	NotifyCut(const CameraControl* pPrevControl) {}

	///--------------------------------------------------------------------------------------///
	/// special blend-type: by travel-dist, by dist-to-object.
	///--------------------------------------------------------------------------------------///
	virtual bool	IsFadeInByTravelDist() const { return false; }
	virtual bool	IsFadeInByDistToObj() const { return false; }
	virtual bool	IsBlendControlledByScript() const { return false; }

	///--------------------------------------------------------------------------------------///
	/// self-blend: mostly for aim-camera
	///--------------------------------------------------------------------------------------///
	void			RequestSelfFadeOut(TimeFrame fadeTime);
	virtual void	CancelSelfFadeOut(TimeFrame fadeTime, bool useFadeTime = false);
	void			RemoveSelfFadeOut(TimeFrame fadeTime);
	virtual float	GetBlendOutTime() const { return -1.0f; }
	bool			IsSelfFadeOutRequested() const { return m_selfFadeOut.m_requested; }
	virtual bool	IsSelfFadeOutAllowed() const { return false; }
	virtual bool	IsForAimingWeapon() const { return false; }

	U32F			GetPlayerIndex() const { return m_playerIndex; }

	CameraId		GetCameraId() const { return m_cameraId; }
	void			SetCameraId(CameraId c) { m_cameraId = c; }

	CameraRank		GetRank() const { return m_rank; }
	void			SetRank(CameraRank r) { m_rank = r; }

	CameraPriority	GetPriority() const { return m_priority; }
	void			SetPriority(CameraPriority p) { m_priority = p; }

	NdGameObjectHandle	GetFocusObject() const { return m_hFocusObj; }
	StringId64			GetSpawnerId() const { return m_spawnerId; }		// the spawner from which this camera reads its settings, if any
	virtual bool		GetRunWhenPaused() const;
	float				GetNormalBlend() const { return m_normalBlend; }
	virtual float		GetLocationBlend() const { return m_blend; }
	virtual bool		MaintainCameraForBlend() const { return false; }
	virtual bool		IsHintCamera() const { return false; }
	virtual bool		IsDead(const CameraControl* nextControl) const;
	bool				WantsCrossFade() const { return m_blendInfo.m_type == CameraBlendType::kCrossFade; }
	virtual bool		AffectGameCameraLocator() const { return true; }
	StringId64			GetDCSettingsId() const { return m_dcSettingsId; }
	bool				IsManualCamera(bool excludeBottom = false) const;

	float				GetBlend() const { return m_blend; }
	virtual void		UpdateBlend(const CameraControl* pPrevControl, const CameraNode* pNextNode);
	virtual void		UpdateNormalBlend(float contributeBlend, const CameraNode* pNextNode, float contributeDistWeight = -1.f);
	virtual float		GetDistWeight() const { GAMEPLAY_ASSERT(false); return -1.f; }
	virtual bool		IsStaticCamera() const { return false; }

	///---------------------------------------------------------------------------------------///
	/// Location
	///---------------------------------------------------------------------------------------///
	const CameraLocation GetCameraLocation() const { return m_location; }
	CameraLocation       GetCameraLocation() { return m_location; }
	Locator				 GetLocator() const { return m_location.GetLocator(); }
	void				 SetCameraLocation(const CameraLocation& c) { m_location = c; }

	///---------------------------------------------------------------------------------------///
	/// Reticle Direction
	///---------------------------------------------------------------------------------------///
	static float GetPitchRad(float fovDegrees, float pixelOffsetY1080p)
	{
		GAMEPLAY_ASSERT(fovDegrees >= 0.f);
		const float screenWidth = 1920.0f;
		const float screenHeight = 1080.0f;
		const float aspectRatio = screenWidth / screenHeight;
		const float tanHalfFov = Tan(DEGREES_TO_RADIANS(fovDegrees * 0.5f)) / aspectRatio;
		float pitchRad = Atan(tanHalfFov * (pixelOffsetY1080p / (screenHeight * 0.5f)));
		return pitchRad;
	}

	static Vector GetReticleDirFromCameraDir(Vector_arg cameraDir, Vector_arg cameraUp, float fovDegrees, float reticleOffset1080y);
	static Vector GetReticleDirFromCameraDir(Vector_arg cameraDir, Vector_arg cameraUp, float fovDegrees);
	static Vector GetCameraDirFromReticleDir(Vector_arg reticleDir, Vector_arg cameraUp, float fovDegrees);

	///-----------------------------------------------------------------------------------------------///
	static const DC::ReticleOffsetDef1080p* GetReticleOffsetDef1080p();

	///---------------------------------------------------------------------------------------///
	/// Look-At
	///---------------------------------------------------------------------------------------///
	//virtual bool	AllowLookAtWiggle() const { return false; }
	struct LookAtError
	{
		Vec2 m_error = Vec2(kZero);
		bool m_reachPitchLimit = false;
	};
	virtual LookAtError CalcLookAtError(Point_arg targetPosWs, const Locator& refCamLoc) const;

	///---------------------------------------------------------------------------------------///
	/// Misc
	///---------------------------------------------------------------------------------------///
	virtual bool	IsInputDisabledWhenFadeOut(const CameraControl* pNextControl, bool checkBlend) const { return false; }
	virtual float	GetYawSpeedDeg() const { return 0.0f; }

	virtual bool	KeepLowerCamerasAlive() const { return false; }

	struct NodeFlags
	{
		union
		{
			U32 m_bits;
			struct
			{
				bool m_dead : 1;							//!< The camera is dead. Will return false from its Run method
			};
		};
		NodeFlags(U32F i = 0) : m_bits(i) { }
		void Clear() { m_bits = 0; }
	};
	const NodeFlags& GetNodeFlags() const { return m_nodeFlags; }
	NodeFlags& GetNodeFlags() { return m_nodeFlags; }

	bool SuppressJoypadDuringBlend() const { return m_flags.m_suppressJoypadDuringBlend; }

	const char * DevKitOnly_GetSpawnerName() const;	// the spawner from which this camera reads its settings, if any ***DEVKIT ONLY!***

	///----------------------------------------------------------------------------///
	/// Debugging
	///----------------------------------------------------------------------------///
	struct DebugShowParams
	{
		bool showRequestFunc = false;
		bool printLocator = false;
	};
	void DebugShow(bool shortList, const DebugShowParams& params) const;
	void DebugDof(float targetDist, float fov, bool bFlipReticleOffsetX = false) const;
	virtual void DebugDraw() const;

protected:

	virtual void FillDebugText(StringBuilder<1024>& sb, bool shortList, const DebugShowParams& params) const;

	void ReadBaseSettingsFromSpawner(DC::CameraBaseSettings& cameraSettings, const EntitySpawner* pSpawner) const;

	// To use this template, your class needs to define a non-static ReadSettingsFromSpawner() function.
	template< typename DC_SETTINGS_TYPE, typename CAMERA_CONTROL_TYPE >
	static const EntitySpawner* ReadSettingsFromDcAndSpawner(CAMERA_CONTROL_TYPE& self, DC_SETTINGS_TYPE& settings, StringId64 dcSettingsId, StringId64 spawnerId)
	{
		// First read the settings from DC.
		ALWAYS_ASSERT(dcSettingsId != INVALID_STRING_ID_64 && "A camera must always have some kind of valid DC settings id! Make a default if you don't have one.");
		const DC_SETTINGS_TYPE * pSettings = ScriptManager::Lookup<DC_SETTINGS_TYPE>(dcSettingsId);
		ALWAYS_ASSERT(pSettings);
		settings = *pSettings;

		// Overwrite any settings that were specified in Charter via EntitySpawner tags.
		const EntitySpawner* pSpawner = nullptr;// EngineComponents::GetLevelMgr()->LookupEntitySpawnerByNameId(spawnerId);
		// self.ReadSettingsFromSpawner(settings, pSpawner);

		return pSpawner;
	}

	// Data Members

	StringId64 m_spawnerId;	// the spawner from which the camera's settings came, or INVALID_STRING_ID_64
	NdGameObjectHandle m_hFocusObj;
	StringId64 m_dcSettingsId;

	struct Flags
	{
		union
		{
			U32 m_bits;
			struct
			{
				bool m_killWhenBlendedOut : 1;				//!< Kill the camera when its contribution is reduced to zero (on blend out)
				bool m_runWhenPaused : 1;					//!< run the camera when we are paused
				bool m_forceStopWhenPaused : 1;				//!< never run the camera when we are paused
				bool m_needsFocus : 1;						//!< Camera is considered dead if its focus goes away
				bool m_dying : 1;							//!< The camera end has been requested
				bool m_suppressJoypadDuringBlend : 1;		//!< when this control is contributing, suppress the joypad
				bool m_dontKillOnFocusDeath : 1;
				bool m_debugFocusObj : 1;
				bool m_noDefaultDofBehavior : 1;			// allow custom dof behavior.
			};
		};
		Flags(U32F i = 0) : m_bits(i) {}
		void Clear() { m_bits = 0; }
	};
	Flags m_flags;

	I64 m_prepareReachedFn = -1; // used for validate every camera calls Prepare()

	U32 m_handleId;						// id for handles to use
	U32 m_playerIndex;

	StringId64 m_cameraSpaceSpawnerId;
	F32 m_cameraSpaceBlend;
	bool m_cameraSpaceUseOverrideUp;
	Vector m_cameraSpaceOverrideUpVec;

	virtual bool IsCameraSpaceBlendEnabled() const { return true; }

	virtual const Vector GetCameraUpVector(bool useOverrideUpVector = false) const;

	//------------------------------------------------------------------------------------------//
	// update blend by types
	//------------------------------------------------------------------------------------------//
	void UpdateBlendByScript(const CameraControl* pNextControl);
	void UpdateBlendByTime(const CameraControl* pPrevControl, F32 distToSpeedMult);
	virtual void UpdateLocationBlendByTime() {}
	virtual void ResetToTimeBasedBlend();

	void UpdateSelfFade();

	//-------------------------------------------------------------------------//
	// DOF
	//-------------------------------------------------------------------------//
	void UpdateDistToDofFocus(float desiredDist, float fov, const DC::CameraDofSettings* pDofSettings);

protected:
	bool m_bIsTopControl = false;

protected:
	// These members used to reside within CameraNode. They're
	// used by CameraManager for managing camera blending.

	NodeFlags m_nodeFlags;

	CameraBlendInfo m_blendInfo;
	float m_blend = 0.0f;
	float m_normalBlend = 0.0f;

	struct SelfFadeOut
	{
		void Reset()
		{
			m_requested = false;
			m_currFade = 1.f;
			m_fadeWhenRequested = m_currFade;
			m_fadeTime = Seconds(0.f);
			m_startTime = Seconds(0.f);
		}

		bool m_requested;
		float m_currFade;				// current self fade value.
		float m_fadeWhenRequested;		// save self value when save-fade is requested. to smooth the blend.
		TimeFrame m_fadeTime;
		TimeFrame m_startTime;
	};
	SelfFadeOut m_selfFadeOut;

	// DofSettings.
	StringId64 m_dofSettingsId;
	StringId64 m_lastForcedDofSettingsId;
	float m_blurScale;
	float m_distToDofFocus;
	float m_forceDofBlurScale = 1.f;
	float m_forceDofAperture = 22.f;
	float m_forceDofBlendTime = 0.f;
	float m_forceDofBlendTimeRemaining = 0.f;
	float m_aperture;
	float m_apertureSpringConst;
	SpringTracker<float> m_apertureSpring;

	// Dof Constants
	static float s_dofDefaultFilmWidth;	// 35 mm
	static float s_dofDefaultFNumber;
	static float s_dofDefaultFocalLength;
	static float s_dofDefaultBlurScale;
	static float s_dofDefaultBlurThreshold;

	CameraBlendInfo m_deathBlend;

	TimeFrame m_startTime;
	TimeFrame m_endTime;

public:

	// for debugging.
	const char*		m_file;
	int				m_line;
	const char*		m_func;
	
	mutable DebugPlot m_debugDistPlot;
	mutable DebugPlot m_debugFocusDistPlot;

	I32 RankDifference(const CameraControl& other) const;

	Vector m_velocity;
	Point m_lastVelocityPosition;							//!< last position used for velocity calculations
	Point m_velocityPosition;								//!< position used for velocity calculations

private:

	CameraId m_cameraId;
	CameraRank m_rank;
	CameraPriority m_priority;
	CameraLocation m_location;								//!< cache the location

protected:

	float m_focusTargetTravelDist = 0.f;
	BoundFrame m_lastFrameFocusBf;

	static float Ease(float tt);
};

CAMERA_DECLARE(kCameraNone);

void CameraControlForceDofSettings(StringId64 forceDofSettingsId, float springConst, float persistTime, const BoundFrame& focusTargetBf, float blurScale = 1.0f, float easeOutPower = 1.0f);
float CalculateLensFocalLengthFromFov(float fovDeg, float filmWidthMM);
float CalculateFovFromLensFocalLength(float focalLengthMM, const float filmWidthMM);

#endif // CAMERACONTROL


