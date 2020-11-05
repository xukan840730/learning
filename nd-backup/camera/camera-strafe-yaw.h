/*
* Copyright (c) 2019 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "corelib/util/angle.h"
#include "gamelib/camera/camera-control.h"

#include "gamelib/scriptx/h/nd-camera-settings.h"

class CameraStrafeYawCtrl
{
private:

	enum { kCurrent, kRequested, kMarioRequested, kCount };

	enum AngleIndex
	{
		kAngleIndexMain = 0,
		kAngleIndexForCalcSpeed = 1,
		kAngleIndexCount = 2
	};

	struct YawAngle
	{
		YawAngle() { Reset(); }

		void Reset()
		{
			m_angle[0] = Angle(0);
			m_angle[1] = Angle(0);
		}

		void SetBoth(Angle v)
		{
			m_angle[0] = v;
			m_angle[1] = v;
		}

		void Add(Angle v)
		{
			m_angle[0] = m_angle[0] + v;
			m_angle[1] = m_angle[1] + v;
		}

		void AdjustForNewSpace(Quat_arg oldParentSpace, Quat_arg newParentSpace)
		{
			for (int kk = 0; kk < kAngleIndexCount; kk++)
			{
				const Vector currYawWs = Rotate(oldParentSpace, VectorFromAngleXZ(m_angle[kk]));
				Vector newYawPs = Unrotate(newParentSpace, currYawWs);
				m_angle[kk] = AngleFromUnitXZVec(newYawPs);
			}
		}

		// index 0: actual yaw-angle.
		// index 1: because maintain aim-point will update requested yaw angle, however we don't want it for speed calculation.
		Angle m_angle[kAngleIndexCount];
	};

	YawAngle m_yawAngle[kCount];

	struct YawSpring
	{
		void Reset()
		{
			m_spring[0].Reset();
			m_spring[1].Reset();
		}

		void SetSpeed(float s)
		{
			m_spring[0].m_speed = s;
			m_spring[1].m_speed = s;
		}

		// index 0: actual yaw-spring.
		// index 1: because maintain aim-point will update requested yaw angle, however we don't want it for speed calculation.
		DampedSpringTracker m_spring[kAngleIndexCount];
	};
	YawSpring m_yawSpring;

	///----------------------------------------------------------------------------///
	/// parent-space
	///----------------------------------------------------------------------------///
	Quat m_parentSpaceRot = kIdentity;
	NdGameObjectHandle m_hPrevParentObj;

	// Allow for delayed reaction when parent space is rotating
	SpringTrackerQuat m_parentSpaceRotSpring;

	TimeFrame m_lastSetDesiredDirTime;
	bool m_desiredDirRequested;
	F32 m_completionDot = 0.995f;
	bool m_desiredDirForceCW;
	bool m_desiredDirForceCWMarioCam;
	float m_desiredDirSpringConst;
	float m_desiredDirSpringConstMarioCam;
	float m_currentHorizontalSpeed;
	float m_lastYawSpeed;
	float m_horizontalSpeedMax;

	bool m_marioDirRequested;
	TimeFrame m_lastSetMarioDirTime;

	bool m_activeInput;

	float m_yawSpringConstOverride;
	I64 m_yawSpringConstOverrideFrame;

	float m_timeScaleMultiplier;
	I64 m_timeScaleMultiplierValidFrame;

public:
	CameraStrafeYawCtrl();

	void SetInitialYawAngle(Angle yaw);
	void SetInitialDirPs(Vector_arg dir);
	void SetDesiredDirXZPs(Vector_arg unitDirXZ, float springConst, bool forceCWDir = false, F32 completionDot = 0.995f);
	void SetDesiredMarioDirXZ(Vector_arg dirXZ, float springConst, bool forceCwDir = false);
	void ClearMarioDir();
	bool HasRequestedMarioDir();

	Angle GetCurrentAnglePs() const { return m_yawAngle[kCurrent].m_angle[kAngleIndexMain]; }
	Angle GetRequestedAnglePs() const { return m_yawAngle[kRequested].m_angle[kAngleIndexMain]; }

	void SetRequestedAnglePs(Angle v);
	void SetMaintainAimPointAngle(Angle v);

	Vector GetCurrentDirPs() const { return VectorFromAngleXZ(m_yawAngle[kCurrent].m_angle[kAngleIndexMain]); }
	Vector GetRequestedDirPs() const { return VectorFromAngleXZ(m_yawAngle[kRequested].m_angle[kAngleIndexMain]); }
	Vector GetMarioRequestedDir() const { return VectorFromAngleXZ(m_yawAngle[kMarioRequested].m_angle[kAngleIndexMain]); }

	// parent-space
	void InitParentSpace(const NdGameObject* pParentGo);
	void UpdateParentSpace(const NdGameObject* pParentGo, const DC::CameraStrafeBaseSettings* pSettings, float dt);

	// world-space
	Vector YawDirToWorldSpace(Vector_arg cameraDirXZPs) const;
	Vector YawDirFromWorldSpace(Vector_arg cameraDirXZWs) const;

	Vector GetCurrentDirWs() const { return YawDirToWorldSpace(GetCurrentDirPs()); }

	bool HasRequestedDirectionXZ() const;
	void ClearRequestedDirectionXZ(bool clearVector = false);
	void SetInitialSpeed(float speed, float springConst);
	F32 GetSpringSpeed() const;
	void UpdateRequestYawFromJoypad(float x, 
									float slowdownX, 
									float targetLockSpeedX, 
									bool stickNotFull, 
									const float joypadThreshHold, 
									const CameraInstanceInfo& info, 
									const DC::CameraStrafeBaseSettings* pSettings, 
									const float normalBlend, 
									const bool isFadeInByDist,
									const float targetSpeed);

	void UpdateCurrentYaw(float dt, 
						  const CameraInstanceInfo& info, 
						  const DC::CameraStrafeBaseSettings* pSettings, 
						  const float normalBlend, 
						  const bool isFadeInByDist, 
						  F32 distanceTravelled, 
						  bool inQuickTurn);

	float GetCurrentHorizontalSpeed() const { return m_currentHorizontalSpeed; }
	float GetLastYawSpeed() const { return m_lastYawSpeed; }
	void SetHorizontalSpeedMax(float m) { m_horizontalSpeedMax = m; }

	void ClampRequestedYawDir(const Locator& targetToWorld, 
							  const DC::CameraStrafeBaseSettings* pSettings, 
							  const NdGameObject* pParentGo);

	static DC::SpringSettings BlendSpringSettings(const DC::SpringSettings& mySettings, 
												  const DC::SpringSettings* prevSettings, 
												  const DC::SpringSettings* nextSettings, 
												  const float normalBlend, 
												  const float nextNormalBlend);

	void BlendHorizontalSettings(const DC::CameraStrafeBaseSettings* pMySettings,
								 const DC::CameraStrafeBaseSettings* pPrevSettings, 
								 const DC::CameraStrafeBaseSettings* pNextSettings, 
								 const float normalBlend, 
								 const float nextNormalBlend,
								 float* outHorizontalSpeed, 
								 float* outHorizontalSpeedMax, 
								 float* outHorizontalSpeedRampTime, 
								 float* outRotationSpeedMultiplier);

	//float PredictRequestedYawDelta(const CameraJoypad& joyPad, 
	//							   const float sensitivityScale, 
	//							   const DC::CameraStrafeBaseSettings* pSettings, 
	//							   I32 playerIndex) const;

	void ApplySpringConstOverride(float springConst); //lasts for the current frame only -- call before updateCurrentYaw
	void ApplyTimeScaleMultiplier(float timeScaleMultiplier); //lasts for the current frame only -- call before updateCurrentYaw

	void AdjustYawForTargetRotation(Quat_arg prevTargetRot, Quat_arg newTargetRot);

protected:

	void AdjustYawForNewParentSpace(Quat_arg oldParentSpace, Quat_arg newParentSpace);

};

Angle SpringTrackUnitXZVector(DampedSpringTracker& spring, Vector_arg currentVec, Vector_arg desiredVec, float dt, const DC::SpringSettings* pSpringSettings, bool forceCWDir, float springConstOverride, bool debugDraw = false);
