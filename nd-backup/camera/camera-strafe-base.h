/*
* Copyright (c) 2019 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "gamelib/camera/camera-arc.h"
#include "gamelib/camera/camera-control.h"
#include "gamelib/camera/camera-shake.h"
#include "gamelib/camera/nd-camera-utility.h"

//#include "game/camera/camera-interface.h"
#include "gamelib/camera/camera-strafe-remap.h"
#include "gamelib/camera/camera-strafe-collision.h"
#include "gamelib/camera/camera-strafe-yaw.h"
#include "gamelib/scriptx/h/nd-camera-settings.h"

///-------------------------------------------------------------------------------------------///
struct StrafeCameraDebugOptions
{
	bool m_drawT1Cam = false;
	bool m_drawBlendedColl = false;
	bool m_drawT1Coll = false;
	bool m_useMovementOffsetCamera = true;
	bool m_debugPullIn = false;
	bool m_debugVehicleMode = false;
	bool m_debugVehicleModeYawSpeed = false;
	bool m_neverRecenter = false;

	bool m_drawSmoothing = false;
	bool m_useStrafeBaseCam = false;
	//bool m_drawDistBlending = false;

	bool m_displayArc = false;
	bool m_displayArcInWorldSpace = false;
	bool m_displayTarget = false;
	bool m_displaySafePos = false;
	bool m_displaySpeed = false;

};
extern StrafeCameraDebugOptions g_strafeCamDbgOptions;

///-------------------------------------------------------------------------------------------///
/// CameraControlStrafeBase: the minimal strafe-camera can just work.
///-------------------------------------------------------------------------------------------///
class CameraControlStrafeBase : public CameraControl
{
	typedef CameraControl ParentClass;

public:

	CameraControlStrafeBase()
		: ParentClass()
		, m_safePosValid(false)
		, m_prepareCalled(false)
		, m_killWhenSelfFadedOut(true)
		, m_pullInPercent(0.0f)
		, m_preparedDt(0.0f)
		, m_preparedCameraPos(kOrigin)
		, m_baseTargetPos(kOrigin)
		, m_preparedTargetPos(kOrigin)
		, m_preparedCameraDir(kZero)
		, m_baseDcSettingsId(INVALID_STRING_ID_64)
		, m_arc(0.0f)
		, m_collNearPlane(Vec2(0, 0))
	{}

	FROM_PROCESS_DECLARE(CameraControlStrafeBase);

	/// ------------------------------------------------------------------------------------------- ///
	/// CameraStart() is declared final, you should override the following funcs instead:
	/// 1) Initialize():		initialize member variables
	/// 2) CalcInitArcAndYaw():	determine first look-at direction.
	/// 3) CalcBlendInfo():		determine how blend in.
	/// ------------------------------------------------------------------------------------------- ///
	virtual CameraBlendInfo		CameraStart(const CameraStartInfo& startInfo) override final;
	virtual void				Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual bool IsEquivalentTo(const CameraStartInfo& startInfo) const override;
	virtual void				Prepare(const CameraInstanceInfo& info) override;
	virtual void				PrepareFinalize(bool isTopControl, const CameraControl* pPrevControl) override;
	virtual CameraLocation		UpdateLocation(const CameraInstanceInfo& info) override;
	virtual void				OnKillProcess() override;

	virtual const DC::CameraStrafeBaseSettings* GetSettings() const { return LookupSettings(m_dcSettingsId); }
	static const DC::CameraStrafeBaseSettings* LookupSettings(StringId64 settingsId);

	/// functions reserved by StrafeCameraCollision
	Point						GetBaseTargetPos() const { return m_baseTargetPos; }
	Point						GetCameraSafePos() const;
	Point						GetRawSafePos() const { return m_rawSafePosWs; }
	const CameraStrafeYawCtrl&	GetYawController() const { return m_yawControl; }

	virtual Point				GetTargetPosition(const NdGameObject* pFocusGo) const;

	virtual Vector				GetOffsetForPhotoMode() const { return Vector(kZero); }
	virtual void				SetPhotoModeOffsetRange(float offsetMaxXz0, float offsetMaxXz1, float offsetMinY, float offsetMaxY) {}

	bool						GetStopPullInHitCeilingFloor() const { return m_stopPullInHitCeilingFloor; }

	TimeFrame					GetLastStickDirTime(int iDir) const { GAMEPLAY_ASSERT(iDir < kStickDirCount); return m_lastStickDirTime[iDir]; }
	TimeFrame					GetLastTargetMoveTime() const { return m_lastTargetMoveTime; }
	TimeFrame					GetLastStickTouchTime() const { return m_lastStickTouchTime; }
	TimeFrame					GetLastStickNoTouchTime() const { return m_lastStickNoTouchTime; }

	StringId64					GetBaseDcSettingsId() const { return m_baseDcSettingsId; }

	bool						IsCharacterBlockingCamera(const Character* pCharacter) const { return m_collision.IsCharacterBlockingCamera(pCharacter); }

	virtual bool				IsAutoAimEnabledForZoomCamera(const CameraControl* pCamera) const { return false; }

	const CameraRemapParamsEx&	GetRemapParams() const { return m_remapParams; }

	float						GetDistWeight() const override { return m_distWeight; }

	virtual bool				IsFadeInByTravelDist() const override;
	virtual bool				IsFadeInByDistToObj() const override;
	virtual bool				IsBlendControlledByScript() const override;

	float						GetPitchSpeedDeg() const;
	float						GetPitchRad() const; // assume arc is updated in Prepare(), calculate delta pitch angle this frame.
	virtual void				AdjustByPitchRad(float desiredPitchRad);

protected:

	// initialize member variables. only be called in CameraStart()
	virtual void				Initialize(const CameraStartInfo& baseStartInfo);

	virtual void				InitCameraSettings(const CameraStartInfo& baseStartInfo);

	virtual CameraRemapSettings RemapSettingsId(StringId64 baseDcSettingsId, bool includeDistRemap) const;

	// calulate initial arc and yaw-control. only be called in CameraStart()
	virtual void				CalcInitArcAndYaw(const CameraStartInfo& baseStartInfo);

	// calculate how to blend myself. only be called in CameraStart()
	virtual CameraBlendInfo		CalcBlendInfo(const CameraStartInfo& baseStartInfo) const { return Seconds(0.5f); }

	virtual void				OnCameraStart(const CameraStrafeStartInfo& startInfo) {} // hook for doing things at the end of CameraStart() because it is marked as final
	virtual void				NotifyCut(const CameraControl* pPrevControl) override;

	static const NdGameObject*	GetCameraParent(const NdGameObject* pFocusGo);
	virtual Locator				GetBaseSafeLocWs() const;

	// settings:
	virtual bool				AllowRemap() const { return !IsPhotoMode(); }
	virtual void				UpdateCollNearPlane(float nearDist);

	void						CalcCameraSafePos(Vector_arg cameraForwardWs, Point_arg baseTargetPosWs);

	float						GetSensitivityScaleX() const;
	float						GetSensitivityScaleY() const;
	virtual void				ReadJoypad(const CameraJoypad& joyPad, const CameraInstanceInfo& info);
	void						UpdatePitch(const DC::CameraStrafeBaseSettings* pSettings, float y);

	// look-at
	struct sErrorForArcDirContext
	{
		const CameraArc* pVerticalArc = nullptr;
		Vector aimDirInCameraCurveSpace = Vector(kZero);
	};
	static float				GetErrorForArcDir(float arc, void* pContext);
	void						SetLookAtDirXz(Vector_arg lookAtDirWs);
	bool						CalcYawAndArcForTargetDir(Vector_arg targetDir, Angle* pOutYawAnglePs, float* pOutArc) const;
	virtual bool				SetLookAtDir(Vector_arg lookAtDirWs);

	virtual Point				GetBaseLookAtPointWs() const;
	virtual StringId64			GetLookAtSplineGuideY() const { return INVALID_STRING_ID_64; } // find closest point on spline and use its y for look-at target.

	virtual Point				CalcCameraPosition(Point_arg targetPos, Vector_arg cameraDirXZ, float arc) const;
	static Point				ComputeCameraPosition(Point_arg safePosWs, Point_arg targetPosWs, Point_arg cameraPosWs, Scalar_arg pullInPercent, float nearDist);
	Vector						CalcCameraDir(Vector_arg cameraDirXZ, float arc) const;

	virtual float				ComputePullInPercent(const CameraInstanceInfo& info, float defaultNearDist, bool shouldCollideWaterPlane, bool ignoreCeilingProbe);

	static bool					EnableNpcCollision(const NdGameObject* pFocusObj);
	void						InitCollisionState(const CameraControl* pPrevCamera, Point_arg baseTargetPos, Point_arg arcCenterWs, Vector_arg cameraUpDirWs);
	virtual CollideFilter		GetCollideFilter() const;

	virtual float				GetFov() const 
	{ 
		const float fov = m_curFov * g_cameraFOVScale; 
		GAMEPLAY_ASSERT(fov > 0.f);
		return fov;
	}

	virtual bool				ShouldCollideWaterPlane() const { return GetSettings()->m_collideWithWaterPlane; }
	virtual bool				IgnoreCeilingProbe() const { return false; }

	void						UpdateNearPlane2(bool topControl, float defaultNearDist, float pullInPercent);
	virtual float				GetDefaultNearDist(const CameraControl* pPrevControl) const;

	void						UpdateParentSpace(const NdGameObject* pFocusGo);

	bool						DisableInputThisFrame() const;

protected:

	///-------------------------------------------------------------------------------------------///
	/// member variables
	///-------------------------------------------------------------------------------------------///

	bool		m_safePosValid;
	bool		m_prepareCalled;
	bool		m_killWhenSelfFadedOut;

	StrafeCameraCollision m_collision;
	float		m_pullInPercent;

	float		m_preparedDt;
	Point		m_preparedCameraPos;
	Point		m_baseTargetPos;
	Point		m_preparedTargetPos;
	Vector		m_preparedCameraDir;

	float		m_currentSideOffset;

	// safe pos
	Point		m_rawSafePosWs;
	Vector		m_safePosCeilingAdjust;
	TwoWaySpringTracker<F32> m_safePosCeilingAdjustSpring;

	Maybe<float> m_safePointAdjY;
	SpringTracker<float> m_safePointAdjSpring;

	// near-plane half width/height
	Vec2		m_collNearPlane;

	// yaw
	CameraStrafeYawCtrl m_yawControl;

	// arc
	CameraArc	m_verticalArc;
	float		m_arc = 0.5f;
	Vec2		m_arcRange = Vec2(0.f, 1.f);

	float		m_origArcForPitchSpeed = 0.f; // arc for calculating pitch speed.
	float		m_arcDelta = 0.f; // Delta of arc from player's input. Used to calculate camera pitch speed, without taking maintain-aim-point, recoil, etc into account.

	float		m_curveYControl;
	DampedSpringTracker m_curveYSpring;

	Vec2		m_slowdownScale;
	SpringTracker<Vec2> m_slowdownTracker;

	bool		m_disableControlsBlendIn = false;

	TimeFrame	m_startTimeCameraClock;	// I don't know what's this for!

	float		m_targetNearPlaneDist = 0.24f;

	enum StickDir
	{
		kStickDirPosX = 0,
		kStickDirNegX,
		kStickDirNegY,
		kStickDirPosY,
		kStickDirCount
	};
	TimeFrame	m_lastStickDirTime[kStickDirCount];

	//intended to be used only in vehicle cam mode. Relies on stick-active-min-dist in vehicle cam mode section of camera-strafe-settings
	enum StickAxis
	{
		kStickAxisYaw = 0,
		kStickAxisPitch,
		kStickAxisCount
	};
	TimeFrame	m_lastStickAxisTime[kStickAxisCount]; // [0] is yaw (stick X), [1] is pitch (stick Y)

	TimeFrame	m_lastTargetMoveTime;
	TimeFrame	m_lastStickTouchTime;
	TimeFrame	m_lastStickNoTouchTime;

	float		m_motionBlurIntensity;

	// for collision.
	bool		m_stopPullInHitCeilingFloor;

	// fov
	float		m_curFov;
	SpringTracker<float> m_fovSpring;

	bool		m_collideWithSimpleNpcs;
	bool		m_disableInputUntil70 = false;

	StringId64	m_baseDcSettingsId; // settings-id before remapping.

private:

	CameraRemapParamsEx m_remapParams;

	virtual bool IsDead(const CameraControl* nextControl) const override;
	virtual void UpdateBlend(const CameraControl* pPrevControl, const CameraNode* pNextNode) override;
	virtual void UpdateNormalBlend(float contributeBlend, const CameraNode* pNextNode, float contributeDistWeight) override;
	void UpdateBlendByTravelDist(float fadeStartDist, float fadeDist/*, TimeFrame timeToDist, float targetBlendToDist*/);

	void UpdateWeightByDistToObj(const DistCameraParams& params);
	virtual void ResetToTimeBasedBlend() override;

	//float m_blendFromTimeToDistProgress;

	// for dist-to-obj camera only, will be set true when there's a newer camera on stack.
	// which means current camera can die, by changing the fade mode to time-based.
	// however, just doing that will cause camera blend value pop. so adding these flags to smooth it out.
	struct DistToObj
	{
		void Reset()
		{
			m_readyToDie = false;
			m_startBlendInDist = -1.f;
			m_fullyBlendInDist = -1.f;
			m_lastFrameDist = 0.f;
		}

		bool m_readyToDie;
		float m_startBlendInDist;
		float m_fullyBlendInDist;
		float m_lastFrameDist;
	};
	DistToObj m_distToObj;

	float m_distWeight = -1.f; // used by dist-to-obj and travel-dist.
};

PROCESS_DECLARE(CameraControlStrafeBase);
CAMERA_DECLARE(kCameraStrafeBase);

bool ShouldDrawCollision(const CameraControlStrafeBase& self);

extern bool g_strafeCamUpdateDuringIgcBlend;
extern bool g_useMovementOffsetCamera;
extern bool g_enableRotatedSafePos;
