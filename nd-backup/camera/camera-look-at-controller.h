/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef CAMERA_LOOK_AT_CONTROLLER_H
#define CAMERA_LOOK_AT_CONTROLLER_H

#include "ndlib/anim/attach-system.h"
#include "ndlib/util/pid-controller.h"
#include "ndlib/util/tracker.h"
#include "ndlib/camera/camera-location.h"

struct CameraJoypad;

//enum class CameraLookAtErrorMode
//{
//	kInvalid,
//	kInWorldSpace,
//	kInCameraSpace,
//};

class CameraInputController
{
public:
	CameraInputController();
	Vec2 ComputeControls(Vec2 error, float deltaTime, float maxOutput, float speedScale, float slowDownScale, float vertSpeedScale, float vertSlowDownScale);
	void SetPrevOutput(Vec2 prevOutput) { m_prevOutput = prevOutput;}
	void DebugDraw() const;

private:
	PidController		 m_horzControl;
	PidController		 m_vertControl;
	SpringTracker<float> m_outputSprings[2];
	Vec2				 m_prevOutput;
	Vec2				 m_prevError; //For debug draw
	Vec2				 m_prevRawOutput; //For debug draw
	bool				 m_inited;
};

class CameraLookAtTarget
{
public:
	CameraLookAtTarget() {}
	CameraLookAtTarget(const BoundFrame& bf);
	CameraLookAtTarget(NdGameObjectHandle hObj);
	CameraLookAtTarget(NdGameObjectHandle hObj, AttachIndex attachPoint);
	CameraLookAtTarget(NdGameObjectHandle hObj, Vector_arg offsetWs);
	CameraLookAtTarget(Vector_arg dirWS);

	Point GetLookAtPosWS(Point_arg cameraPosWs) const;
	BoundFrame GetLookAtBf() const { return m_targetWs; }

private:
	bool				m_directionValid;
	bool				m_offsetValid;
	BoundFrame			m_targetWs;
	Vector				m_targetDirWs;
	NdGameObjectHandle	m_targetObj;
	AttachIndex			m_targetAttachIndex;
	Vector				m_offsetWs;
};

class CameraLookAtParams
{
public:
	CameraLookAtTarget m_target;
	Vector m_targetFacing;
	float m_dotThreshold;
	float m_completionAngle;		// if completionAngle is 0, look-at is on until user call Disable()
	Vec2 m_allowAdjustmentErr;	// only allow stick adjustment if error is less than this value.
	float m_adjustmentAngle;		// if not 0, allow a small room for camera to turn away from look-at obj.
	float m_maxSpeed;
	float m_speedScale;
	float m_slowDownScale;
	float m_vertSpeedScale;
	float m_vertSlowDownScale;
	Vec2 m_reticleOffset1080p;
	bool m_alwaysUseTopCameraOnly;

public:
	CameraLookAtParams() :
		m_target(),
		m_targetFacing(kZero),
		m_dotThreshold(0.86f),
		m_completionAngle(1.0f),
		m_allowAdjustmentErr(Vec2(0.f, 0.f)),
		m_adjustmentAngle(0.f),
		m_maxSpeed(1.0f),
		m_speedScale(1.0f),
		m_slowDownScale(1.0f),
		m_vertSpeedScale(1.0f),
		m_vertSlowDownScale(1.0f),
		m_reticleOffset1080p(Vec2(0, 0)),
		m_alwaysUseTopCameraOnly(false)
	{}
};

class CameraLookAtController
{
public:
	struct Error
	{
		bool m_valid;
		Vec2 m_err;
	};

	CameraLookAtController(I32 playerIndex);
	CamLookAtTargetPos Update(const CameraJoypad& actualJoypad, float deltaTime, const CameraControl* pCurrCamera);
	void SetLookAt(const CameraLookAtParams &params, StringId64 requestingScriptId);
	void Disable();
	void DisableById(StringId64 scriptId);
	void AdjustCameraJoypad(CameraJoypad& joypad);
	bool IsEnabled() const { return m_enabled; }
	bool IsMovedByStick() const;

	Error GetError() const;

	static bool s_debugDraw;
	static bool s_debugInputCtrl;

private:
	bool IsComplete(const Vec2& error) const;

	struct OscilattingInfo
	{
		void Reset()
		{
			m_valid = false;
			m_limitDirNorm = kZero;
			m_errorNorm = kZero;
			m_dotProdInputAndError = 0.f;
		}

		bool			m_valid;

		Vector			m_limitDirNorm;	// to prevent input oscillating, use previous limit dir.
		Vector			m_errorNorm;
		float			m_dotProdInputAndError;
	};

	struct StickAdjustment
	{
		void Reset()
		{
			m_allowed = false;
			m_moved = false;
			m_hasInputTime = 0.f;

			m_lastFrameOscInfo.Reset();
		}

		bool			m_allowed;		// allow stick adjustment after camera look-at is close.
		bool			m_moved;		// camera joypad is moved by player.
		float			m_hasInputTime;	// how longer player has pushed the stick.

		OscilattingInfo m_lastFrameOscInfo;
	};

	struct AdjustResult
	{
		bool valid;
		Vec2 joypad;

		OscilattingInfo oscRes;
	};
	AdjustResult AdjustByStickInput(const StickAdjustment& adju, const CameraJoypad& actualJoypad, const Vec2& currErr, Point_arg targetPosWs, const Locator& refCamLoc, float angleRange) const;

	//void CalcErrorMode(bool zoomCamOnTop, const Locator& lastFrameTopCamLoc);

	CamLookAtTargetPos UpdateInternal(const CameraJoypad& actualJoypad, float deltaTime, const CameraControl* pCurrCamera);

	//CameraLookAtErrorMode m_errorMode = CameraLookAtErrorMode::kInvalid;
	CameraInputController m_inputController;
	Vec2				  m_desiredJoypad = Vec2(0, 0);
	CameraLookAtParams	  m_lookAtParams2;
	I32					  m_screenIndex;
	I32					  m_playerIndex;

	bool				  m_enabled = false;
	StringId64			  m_requestingScriptId = INVALID_STRING_ID_64;
	Angle				  m_initYawAngle = Angle(0);

	StickAdjustment		  m_stickAdjustment;

	Error				  m_lastError;

	// For neural network
	Angle m_prevPitch, m_prevHeading;
};

#endif