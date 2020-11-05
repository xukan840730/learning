/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/camera/camera-look-at-controller.h"

#include "ndlib/anim/attach-system.h"
#include "ndlib/render/display.h"
#include "ndlib/render/gui/nd-hud.h"
#include "ndlib/util/quick-plot.h"
#include "ndlib/math/neural-network.h"

#include "gamelib/camera/camera-control.h"
#include "gamelib/camera/camera-joypad.h"
#include "gamelib/camera/camera-manager.h"
#include "gamelib/gameplay/nd-game-object.h"

bool g_trainCameraLookAtNN = false;
bool g_nnReloadWeights = false;

bool CameraLookAtController::s_debugDraw = false;
bool CameraLookAtController::s_debugInputCtrl = false;

struct CameraNeuralNetworkTrainer
{
	struct CameraFrameInfo
	{
		F32 m_pitch;
		Angle m_heading;
		F32 m_pitchSpeed;
		F32 m_headingSpeed;
		F32 m_inputX;
		F32 m_inputY;
	};

	struct CameraMovementStroke
	{
		F32 m_desDeltaPitch;
		F32 m_desDeltaHeading;
		F32 m_pitchSpeed;
		F32 m_headingSpeed;
		F32 m_desInputX;
		F32 m_desInputY;
	};

	CameraMovementStroke GenerateMovementStroke(const CameraFrameInfo* pStartInfo, const CameraFrameInfo* pEndFrame)
	{
		F32 desPitch = pEndFrame->m_pitch;
		Angle desHeading = pEndFrame->m_heading;

		CameraMovementStroke mvStroke;
		mvStroke.m_desDeltaPitch = desPitch - pStartInfo->m_pitch;
		mvStroke.m_desDeltaHeading = (desHeading - pStartInfo->m_heading).ToRadians();
		mvStroke.m_pitchSpeed = pStartInfo->m_pitchSpeed;
		mvStroke.m_headingSpeed = pStartInfo->m_headingSpeed;
		mvStroke.m_desInputX = pStartInfo->m_inputX;
		mvStroke.m_desInputY = pStartInfo->m_inputY;
		return mvStroke;
	}

	bool m_recording = false;

	CameraFrameInfo m_frameInfos[1024];
	I32 m_numFrameInfos = 0;

	Point m_cameraTrainTarget;
	bool m_initCameraTrain = false;

	void GenerateNewTarget()
	{
		const Point startPos = GameCameraLocator().GetTranslation();
		const F32 heading = frand(-PI / 2.0f, PI);
		const F32 pitch = frand(-PI / 6.0f, PI / 3.0f);
		const Vector targetPosXz = VectorFromAngleXZ(Angle::FromRadians(heading));
		m_cameraTrainTarget = startPos + targetPosXz * cos(pitch) * 10.0f + Vector(0.0f, sin(pitch), 0.0f) * 10.0f;
	}

	void TrackCameraFrame(CameraFrameInfo &info)
	{
		if (!m_initCameraTrain)
		{
			GenerateNewTarget();
			m_initCameraTrain = true;
		}

		g_prim.Draw(DebugCross(m_cameraTrainTarget, 1.0f, 1.0f, kColorYellow, kPrimDisableDepthTest));
		Gui2::OnscreenProjection proj = g_gui2.ProjectToScreenClamp(0, m_cameraTrainTarget, 32.0f, 32.0f, DC::kHudScreenClampTypeDefault);

		if (proj.m_offscreen)
		{
			Vec2 sc(proj.m_pos.X(), proj.m_pos.Y());
			g_prim.Draw(DebugCircle2D(sc, 10.0f, 10.0f, kDebug2DLegacyCoords, kColorYellow));
			//g_prim.Draw(DebugArrow(GameCameraLocator().GetTranslation() + GameCameraDirection() * 2.0f, s_cameraTrainTarget, kColorYellow));
		}

		if (!m_recording && (info.m_inputX != 0.0f || info.m_inputY != 0.0f))
		{
			m_recording = true;
			m_numFrameInfos = 0;
		}

		if (m_recording)
		{
			MsgCon("Recording %d\n", m_numFrameInfos);

			if ((info.m_inputX == 0.0f && info.m_inputY == 0.0f && Abs(info.m_pitchSpeed) < 0.05f && Abs(info.m_headingSpeed) < 0.05f) || m_numFrameInfos == 1024)
			{
				GenerateNewTarget();
				m_recording = false;
				char outputFilename[512];
				sprintf(outputFilename, "%s/cameraframes.bin", EngineComponents::GetNdGameInfo()->m_pathDetails.m_localDir);
				FILE* fh = fopen(outputFilename, "ab");

				for (int i = 0; i < (m_numFrameInfos - 1); i++)
				{
					CameraMovementStroke stk = GenerateMovementStroke(m_frameInfos + i, m_frameInfos + m_numFrameInfos - 1);
					fwrite(&stk, sizeof(stk), 1, fh);
				}

				fclose(fh);
			}
			else
			{
				m_frameInfos[m_numFrameInfos++] = info;
			}
		}
	}
};

CameraNeuralNetworkTrainer s_nnTrainer;

static const float s_dnnData[] = {
	-0.1194f,
	1.3336f,
	-0.0114f,
	-0.2396f,
	0.0000f,
	-0.7972f,
	0.5895f,
	-0.7058f,
	-0.1053f,
	0.0000f,
	-0.8036f,
	8.1397f,
	0.1303f,
	-1.0454f,
	0.0000f,
	0.1589f,
	-7.8642f,
	0.0916f,
	1.0125f,
	0.0000f,
	2.4905f,
	-0.0648f,
	-0.4211f,
	0.4365f,
	0.0000f,
	0.4128f,
	-0.9384f,
	0.0009f,
	0.0027f,
	0.0000f,
};

struct CameraLookatDenseNeuralNetwork
{
	DenseNeuralNetworkLayer<4, 4> m_dense1;
	DenseNeuralNetworkLayer<4, 2> m_dense2;

	bool m_loaded = false;

	void LoadWeights()
	{
		I32 dataLen = sizeof(s_dnnData) / sizeof(float);
		I32 count = m_dense1.ReadFromFloatArray(s_dnnData, dataLen);
		I32 count2 = m_dense2.ReadFromFloatArray(s_dnnData + count, dataLen - count);
	}

	void LoadWeightsFile()
	{
		char weightFilename[512], biasFilename[512];
		sprintf(weightFilename, "%s/W0.bin", EngineComponents::GetNdGameInfo()->m_pathDetails.m_localDir);
		sprintf(biasFilename, "%s/b0.bin", EngineComponents::GetNdGameInfo()->m_pathDetails.m_localDir);
		m_dense1.LoadWeights(weightFilename, biasFilename);
		sprintf(weightFilename, "%s/W1.bin", EngineComponents::GetNdGameInfo()->m_pathDetails.m_localDir);
		sprintf(biasFilename, "%s/b1.bin", EngineComponents::GetNdGameInfo()->m_pathDetails.m_localDir);
		m_dense2.LoadWeights(weightFilename, biasFilename);

		float floatArray1[128];
		I32 count = m_dense1.WriteToFloatArray(floatArray1, 128);
		float floatArray2[128];
		I32 count2 = m_dense2.WriteToFloatArray(floatArray2, 128);
		for (int i = 0; i < count; i++)
		{
			MsgOut("%.4ff,\n", floatArray1[i]);
		}
		for (int i = 0; i < count2; i++)
		{
			MsgOut("%.4ff,\n", floatArray2[i]);
		}
	}

	void Run(float* input, float* output, I32 outputSize)
	{
		if (!m_loaded)
		{
			LoadWeights();
			m_loaded = true;
		}
		if (g_nnReloadWeights)
		{
			LoadWeightsFile();
			g_nnReloadWeights = false;
		}

		Eigen::Array<float, 4, 1> inputVector;
		for (int i = 0; i < 4; i++)
		{
			inputVector[i] = input[i];
		}
		auto H0 = m_dense1.ExecuteLayer(inputVector);
		m_dense1.ELU(H0);
		auto H1 = m_dense2.ExecuteLayer(H0);
		GAMEPLAY_ASSERT(outputSize == 2);
		output[0] = H1[0];
		output[1] = H1[1];
	}
};

static CameraLookatDenseNeuralNetwork s_dnn;

/// --------------------------------------------------------------------------------------------------------------- ///
CameraInputController::CameraInputController()
{
	m_prevOutput = Vec2(0.0f, 0.0f);	
	m_inited = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vec2 CameraInputController::ComputeControls( Vec2 error, float deltaTime, float maxOutput, float speedScale, float slowDownScale, float vertSpeedScale, float vertSlowDownScale )
{
	static float s_hP = 0.10f;
	static float hI = 0.0f;
	static float s_hD = 0.12f;
	static float hMaxE = 5.0f;

	float hP = s_hP * speedScale;
	float hD = s_hD * slowDownScale;

	static float s_vP = 0.10f;
	static float vI = 0.0f;
	static float s_vD = 0.12f;
	static float vMaxE = 5.0f;

	float vP = s_vP * vertSpeedScale;
	float vD = s_vD * vertSlowDownScale;

	m_prevError = error;	

	if (!m_inited)
	{
		m_horzControl.SetPrevError(error.X());
		m_vertControl.SetPrevError(error.Y());
		m_inited = true;
	}
	m_horzControl.SetGainValues(hP,hI,hD,hMaxE);
	m_vertControl.SetGainValues(vP,vI,vD,vMaxE);

	float outputX = m_horzControl.Update(error.X(), deltaTime);
	float outputY = m_vertControl.Update(error.Y(), deltaTime);

	static float stickSmoothConst = 100.0f;

	m_prevRawOutput = Vec2(outputX, outputY);	

	m_prevOutput.x = m_outputSprings[0].Track(m_prevOutput.x, MinMax(outputX, -maxOutput, maxOutput), deltaTime, stickSmoothConst);
	m_prevOutput.y = m_outputSprings[1].Track(m_prevOutput.y, MinMax(outputY, -maxOutput, maxOutput), deltaTime, stickSmoothConst);		 

	//Limit to -1 to 1 and change to stick space
	return Vec2(MinMax(-m_prevOutput.x, -maxOutput, maxOutput), MinMax(-m_prevOutput.y, -maxOutput, maxOutput));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraInputController::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	MsgCon("hError accum: %f\n", m_horzControl.GetErrorAccum());
	MsgCon("VError accum: %f\n", m_vertControl.GetErrorAccum());

	QUICK_PLOT(
		yawErrorPlot,
		"Camera yaw control error",
		10, 10, // x and y coordinates to draw the plot
		250, 250, // width and height of the plot
		60, // show values from 60 frames ago up until now
		GraphDisplay::Params().WithGraphType(GraphDisplay::kGraphTypeLine).NoAvgLine().NoAvgText().NoMaxLine().NoMaxText(), // provide additional customization of the plot
		LerpScaleClamp(-180.0f, 180.0f, 0.0f, 1.0f, m_prevError.X()) // float from 0.0f to 1.0f
		);

	QUICK_PLOT(
		pitchErrorPlot,
		"Camera pitch control error",
		10, 260, // x and y coordinates to draw the plot
		250, 250, // width and height of the plot
		60, // show values from 60 frames ago up until now
		GraphDisplay::Params().WithGraphType(GraphDisplay::kGraphTypeLine).NoAvgLine().NoAvgText().NoMaxLine().NoMaxText(), // provide additional customization of the plot
		LerpScaleClamp(-90.0f, 90.0f, 0.0f, 1.0f, m_prevError.Y()) // float from 0.0f to 1.0f
		);


	QUICK_PLOT(
		inputXPlot,
		"Camera control input x",
		260, 10, // x and y coordinates to draw the plot
		250, 250, // width and height of the plot
		60, // show values from 60 frames ago up until now
		GraphDisplay::Params().WithGraphType(GraphDisplay::kGraphTypeLine).NoAvgLine().NoAvgText().NoMaxLine().NoMaxText(), // provide additional customization of the plot
		LerpScaleClamp(-1.0f, 1.0f, 0.0f, 1.0f, m_prevOutput.X()) // float from 0.0f to 1.0f
		);

	QUICK_PLOT(
		inputYPlot,
		"Camera control input y",
		260, 260, // x and y coordinates to draw the plot
		250, 250, // width and height of the plot
		60, // show values from 60 frames ago up until now
		GraphDisplay::Params().WithGraphType(GraphDisplay::kGraphTypeLine).NoAvgLine().NoAvgText().NoMaxLine().NoMaxText(), // provide additional customization of the plot
		LerpScaleClamp(-1.0f, 1.0f, 0.0f, 1.0f, m_prevOutput.Y()) // float from 0.0f to 1.0f
		);

	QUICK_PLOT(
		inputXRawPlot,
		"",
		260, 10, // x and y coordinates to draw the plot
		250, 250, // width and height of the plot	
		60, // show values from 60 frames ago up until now
		GraphDisplay::Params().WithGraphType(GraphDisplay::kGraphTypeLine).NoAvgLine().NoAvgText().NoMaxLine().NoMaxText().WithDataColor(Abgr8FromRgba(255,0,0,255)), // provide additional customization of the plot
		LerpScaleClamp(-1.0f, 1.0f, 0.0f, 1.0f, m_prevRawOutput.X()) // float from 0.0f to 1.0f
		);

	QUICK_PLOT(
		inputYRawPlot,
		"",
		260, 260, // x and y coordinates to draw the plot
		250, 250, // width and height of the plot
		60, // show values from 60 frames ago up until now
		GraphDisplay::Params().WithGraphType(GraphDisplay::kGraphTypeLine).NoAvgLine().NoAvgText().NoMaxLine().NoMaxText().WithDataColor(Abgr8FromRgba(255,0,0,255)), // provide additional customization of the plot
		LerpScaleClamp(-1.0f, 1.0f, 0.0f, 1.0f, m_prevRawOutput.Y()) // float from 0.0f to 1.0f
		);
}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraLookAtTarget::CameraLookAtTarget(const BoundFrame& bf)
	: m_directionValid(false)
	, m_offsetValid(false)
	, m_targetWs(bf)
{}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraLookAtTarget::CameraLookAtTarget(NdGameObjectHandle hObj)
	: m_directionValid(false)
	, m_offsetValid(false)
	, m_targetObj(hObj)
	, m_targetAttachIndex(AttachIndex::kInvalid)
{}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraLookAtTarget::CameraLookAtTarget(NdGameObjectHandle hObj, AttachIndex attachPoint)
	: m_directionValid(false)
	, m_offsetValid(false)
	, m_targetObj(hObj)
	, m_targetAttachIndex(attachPoint)
{}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraLookAtTarget::CameraLookAtTarget(NdGameObjectHandle hObj, Vector_arg offsetWs)
	: m_directionValid(false)
	, m_offsetValid(true)
	, m_targetObj(hObj)
	, m_targetAttachIndex(AttachIndex::kInvalid)
	, m_offsetWs(offsetWs)
{}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraLookAtTarget::CameraLookAtTarget(Vector_arg lookAtDirWs)
	: m_directionValid(true)
	, m_offsetValid(false)
	, m_targetDirWs(lookAtDirWs)
{}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraLookAtController::CameraLookAtController(I32 playerIndex)
	: m_lookAtParams2()
	, m_playerIndex(playerIndex)
	, m_enabled(false) 
{}

/// --------------------------------------------------------------------------------------------------------------- ///
SMath::Point CameraLookAtTarget::GetLookAtPosWS(Point_arg cameraPosWs) const
{
	if (m_directionValid)
	{
		return cameraPosWs + m_targetDirWs*10.0f;
	}
	if (const NdGameObject* pLookAtObj = m_targetObj.ToProcess())
	{
		Point basePoint;
		if (m_targetAttachIndex != AttachIndex::kInvalid)
		{
			basePoint = pLookAtObj->GetAttachSystem()->GetLocator(m_targetAttachIndex).GetTranslation();
		}
		else
		{
			basePoint = pLookAtObj->GetSite(SID("LookAtPos")).GetTranslation();
		}
		
		return m_offsetValid ? (basePoint + m_offsetWs) : basePoint;
	}
	return m_targetWs.GetTranslation();
}

/// --------------------------------------------------------------------------------------------------------------- ///
CamLookAtTargetPos CameraLookAtController::Update(const CameraJoypad& actualJoypad, float deltaTime, const CameraControl* pCurrCamera)
{
	CamLookAtTargetPos lookAtRes;

	Locator lastFrameTopCamLoc(kOrigin);
	if (pCurrCamera != nullptr)
	{
		lastFrameTopCamLoc = pCurrCamera->GetCameraLocation().GetLocator();
	}

	if (FALSE_IN_FINAL_BUILD(s_debugDraw))
	{
		MsgCon("Camera Look At:\n");
		if (!m_enabled)
		{
			MsgCon("Disabled\n");
		}
		else
		{
			MsgCon("Active request from: %s\n", DevKitOnly_StringIdToString(m_requestingScriptId));
		}
	}

	if (deltaTime == 0.0f)
		return lookAtRes;

	Vector pbh;
	QuaternionToEulerAngles(&pbh, lastFrameTopCamLoc.GetRotation());

	Angle curPitch = Angle::FromRadians(pbh.X());
	Angle curHeading = Angle::FromRadians(pbh.Y());

	Angle pitchDiff = curPitch - m_prevPitch;
	Angle headingDiff = curHeading - m_prevHeading;

	if (m_playerIndex == 0)
	{
		m_prevPitch = curPitch;
		m_prevHeading = curHeading;
	}

	if (g_trainCameraLookAtNN && !m_enabled && m_playerIndex == 0)
	{
		CameraNeuralNetworkTrainer::CameraFrameInfo finfo;
		finfo.m_headingSpeed = headingDiff.ToRadians() / deltaTime;
		finfo.m_pitchSpeed = pitchDiff.ToRadians() / deltaTime;
		finfo.m_inputX = actualJoypad.GetX(0);
		finfo.m_inputY = actualJoypad.GetY(0);
		finfo.m_heading = Angle(RADIANS_TO_DEGREES(pbh.Y()));
		finfo.m_pitch = pbh.X();
		s_nnTrainer.TrackCameraFrame(finfo);
	}

	if (m_lookAtParams2.m_maxSpeed < 0.0f)
	{
		// Use neural network if max speed < 0
		if (m_enabled && (!EngineComponents::GetNdPhotoModeManager() || !EngineComponents::GetNdPhotoModeManager()->IsActive()))
		{
			float input[4];
			float output[2];

			const Point targetPosWs = m_lookAtParams2.m_target.GetLookAtPosWS(lastFrameTopCamLoc.GetTranslation());
			const Point targetPosLs = GameGlobalCameraLocator(m_playerIndex).UntransformPoint(targetPosWs);
			const Quat targetRot = QuatFromLookAt(targetPosLs - Point(kOrigin), kUnitYAxis);
			QuaternionToEulerAngles(&pbh, targetRot);

			const float screenWidth = g_display.m_screenWidth;
			const float screenHeight = g_display.m_screenHeight;
			const float aspectRatio = screenWidth / screenHeight;
			const float tanHalfFov = Tan(DEGREES_TO_RADIANS(g_mainCameraInfo[0].GetFov() * 0.5f)) / aspectRatio;

			const float pitchReticle = Atan(tanHalfFov * (m_lookAtParams2.m_reticleOffset1080p.y / (screenHeight * 0.5f)));
			const float yawReticle = Atan(aspectRatio * tanHalfFov * (m_lookAtParams2.m_reticleOffset1080p.x / (screenWidth * 0.5f)));

			input[0] = pbh.X() - pitchReticle;
			input[1] = pbh.Y() + yawReticle;
			input[2] = pitchDiff.ToRadians() / deltaTime;
			input[3] = headingDiff.ToRadians() / deltaTime;

			s_dnn.Run(input, output, 2);
			m_desiredJoypad = Vec2(output[0], output[1]);
			m_stickAdjustment.m_moved = true;

			F32 degreeTotal = RADIANS_TO_DEGREES(Abs(input[0]) + Abs(input[1]));
			if (m_lookAtParams2.m_completionAngle > 0.f && degreeTotal < m_lookAtParams2.m_completionAngle)
			{
				Disable();
				return lookAtRes;
			}

			lookAtRes.m_targetPos = targetPosWs;
			lookAtRes.m_posValid = true;
			lookAtRes.m_targetFacing = m_lookAtParams2.m_targetFacing;
			lookAtRes.m_facingValid = IsNormal(m_lookAtParams2.m_targetFacing);
			lookAtRes.m_dotThres = m_lookAtParams2.m_dotThreshold;

			return lookAtRes;

		}

		m_lastError.m_err = Vec2(0, 0);
		m_lastError.m_valid = false;
		return lookAtRes;
	}

	if (!m_enabled || (EngineComponents::GetNdPhotoModeManager() && EngineComponents::GetNdPhotoModeManager()->IsActive()))
	{
		m_lastError.m_err = Vec2(0, 0);
		m_lastError.m_valid = false;
		return lookAtRes;
	}

	return UpdateInternal(actualJoypad, deltaTime, pCurrCamera);
}

/// --------------------------------------------------------------------------------------------------------------- ///
CamLookAtTargetPos CameraLookAtController::UpdateInternal(const CameraJoypad& actualJoypad, float deltaTime, const CameraControl* pCurrCamera)
{
	CamLookAtTargetPos lookAtRes;

	float topCamBlend = 0.f;
	Locator lastFrameTopCamLoc(kOrigin);
	if (pCurrCamera != nullptr)
	{
		lastFrameTopCamLoc = pCurrCamera->GetCameraLocation().GetLocator();
		topCamBlend = pCurrCamera->GetBlend();
	}

	//CalcErrorMode(zoomCamOnTop, lastFrameTopCamLoc);

	const Locator refCamLoc = (/*m_errorMode == CameraLookAtErrorMode::kInCameraSpace || */m_lookAtParams2.m_alwaysUseTopCameraOnly) 
		? lastFrameTopCamLoc 
		: CameraManager::GetGlobal(m_playerIndex).GetGameRenderCamera().GetLocator();

	// calculate the target pos.
	Point targetPosWs = m_lookAtParams2.m_target.GetLookAtPosWS(refCamLoc.GetTranslation());
	if (m_lookAtParams2.m_reticleOffset1080p.X() != 0.f || m_lookAtParams2.m_reticleOffset1080p.Y() != 0.f)
	{
		// need to account for offset in this case
		const Vector dirToTarget = SafeNormalize(targetPosWs - refCamLoc.GetTranslation(), kZero);

		const Vector newDirToTarget = CameraControl::GetReticleDirFromCameraDir(dirToTarget, kUnitYAxis, g_mainCameraInfo[0].GetFov(), -m_lookAtParams2.m_reticleOffset1080p.Y());
		targetPosWs = refCamLoc.GetTranslation() + newDirToTarget * 10.0f; // this doesn't work if targetPos is very close!
	}

	if (FALSE_IN_FINAL_BUILD(s_debugDraw))
	{
		g_prim.Draw(DebugCross(targetPosWs, 0.2f, kColorYellow, PrimAttrib(kPrimDisableHiddenLineAlpha)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugString(targetPosWs, "tgt", kColorWhite, 0.6f), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(refCamLoc.GetTranslation(), targetPosWs), kPrimDuration1FramePauseable);
	}

	const CameraControl::LookAtError newErrorRes = pCurrCamera ? pCurrCamera->CalcLookAtError(targetPosWs, refCamLoc) : CameraControl::LookAtError();
	const Vec2 newError = newErrorRes.m_error;
	if (FALSE_IN_FINAL_BUILD(s_debugDraw))
	{
		MsgConPauseable("error: %f, %f\n", newError.X(), newError.Y());
	}

	if (!m_stickAdjustment.m_moved && m_lastError.m_valid)
	{
		// this could happen when camera is looked at somewhere right at the feet.
		if (newErrorRes.m_reachPitchLimit)
		{
			const Vector cameraDirXz = SafeNormalize(VectorXz(GetLocalZ(GameCameraLocator().GetRotation())), kZero);
			const Angle currYawAngle = AngleFromUnitXZVec(cameraDirXz);

			// give up look-at to prevent camera spinning.
			if (m_initYawAngle.AngleDiff(currYawAngle) > Abs(90.f))
			{
				Disable();
				return lookAtRes;
			}
		}
	}

	m_lastError.m_err = newError;
	m_lastError.m_valid = true;

	// if completionAngle is 0, look-at is on until user call Disable()
	if (m_lookAtParams2.m_completionAngle > 0.f && IsComplete(m_lastError.m_err))
	{
		Disable();
		return lookAtRes;
	}

	m_desiredJoypad = m_inputController.ComputeControls(m_lastError.m_err,
														deltaTime,
														m_lookAtParams2.m_maxSpeed,
														m_lookAtParams2.m_speedScale,
														m_lookAtParams2.m_slowDownScale,
														m_lookAtParams2.m_vertSpeedScale,
														m_lookAtParams2.m_vertSlowDownScale);

	GAMEPLAY_ASSERT(IsFinite(m_desiredJoypad));

	if (m_lookAtParams2.m_allowAdjustmentErr.X() > 0.f && m_lookAtParams2.m_allowAdjustmentErr.Y() > 0.f && m_lookAtParams2.m_adjustmentAngle > 0.f)
	{
		bool isTopFadeInByDistToObj = pCurrCamera && pCurrCamera->IsFadeInByDistToObj();
		if (!m_stickAdjustment.m_allowed && (topCamBlend > 0.75f || isTopFadeInByDistToObj))
		{
			if (Abs(m_lastError.m_err.X()) < m_lookAtParams2.m_allowAdjustmentErr.X() &&
				Abs(m_lastError.m_err.Y()) < m_lookAtParams2.m_allowAdjustmentErr.Y())
			{
				m_stickAdjustment.m_allowed = true;
			}
		}

		if (m_stickAdjustment.m_allowed)
		{
			// update timer.
			if (abs(actualJoypad.GetX(m_playerIndex)) > 0.f || abs(actualJoypad.GetY(m_playerIndex)) > 0.f)
				m_stickAdjustment.m_hasInputTime += deltaTime;
			else
				m_stickAdjustment.m_hasInputTime = 0.f;

			AdjustResult adjustRes = AdjustByStickInput(m_stickAdjustment, actualJoypad, m_lastError.m_err,
				targetPosWs, lastFrameTopCamLoc, m_lookAtParams2.m_adjustmentAngle * 0.9f);
			if (adjustRes.valid)
			{
				m_desiredJoypad = adjustRes.joypad;
				GAMEPLAY_ASSERT(IsFinite(m_desiredJoypad));
				m_stickAdjustment.m_moved = true;
			}
			m_stickAdjustment.m_lastFrameOscInfo = adjustRes.oscRes;
		}
	}

	if (FALSE_IN_FINAL_BUILD(s_debugInputCtrl))
		m_inputController.DebugDraw();

	lookAtRes.m_targetPos = targetPosWs;
	lookAtRes.m_posValid = true;
	lookAtRes.m_targetFacing = m_lookAtParams2.m_targetFacing;
	lookAtRes.m_facingValid = IsNormal(m_lookAtParams2.m_targetFacing);
	lookAtRes.m_dotThres = m_lookAtParams2.m_dotThreshold;

	return lookAtRes;

}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraLookAtController::SetLookAt(const CameraLookAtParams &params, StringId64 requestingScriptId)
{
	if (const CameraControl* pCurCamera = CameraManager::GetGlobal(m_playerIndex).GetCurrentCamera())
	{
		if (pCurCamera->AllowCameraLookAt())
		{	
			m_lookAtParams2 = params;
			//m_errorMode = CameraLookAtErrorMode::kInvalid;
			m_stickAdjustment.Reset();
			m_enabled = true;
			m_requestingScriptId = requestingScriptId;

			const Vector cameraDirXz = SafeNormalize(VectorXz(GetLocalZ(GameCameraLocator().GetRotation())), kZero);
			m_initYawAngle = AngleFromUnitXZVec(cameraDirXz);
		}
		else
		{
			MsgScript("Ignoring camera look at because current camera doesn't allow scripted look at\n");
		}
	}
	else
	{
		MsgScript("Ignoring camera look at because there is no current camera\n");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraLookAtController::Disable()
{
	m_enabled = false;
	m_stickAdjustment.Reset();
	m_inputController = CameraInputController();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraLookAtController::DisableById(StringId64 scriptId)
{
	if (m_requestingScriptId == scriptId)
	{
		m_enabled = false;
		m_stickAdjustment.Reset();
		m_inputController = CameraInputController();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraLookAtController::AdjustCameraJoypad( CameraJoypad& joypad )
{
	if (m_enabled && (!EngineComponents::GetNdPhotoModeManager() || !EngineComponents::GetNdPhotoModeManager()->IsActive()))
	{
		// fix issue 118207 and 118313. the input was double inverted, it should be inverted once in CameraJoyPad.GetX() and CameraJoyPad.GetY().
		joypad.m_stickCamera = m_desiredJoypad;
		joypad.m_rawStickCamera = joypad.m_stickCamera;

		// @TODO SERVER-PLAYER
		if (EngineComponents::GetNdGameInfo()->m_CameraXMode[0])
		{
			joypad.m_stickCamera.x = -joypad.m_stickCamera.x;
		}
		if (EngineComponents::GetNdGameInfo()->m_CameraYMode[0])
		{
			joypad.m_stickCamera.y = -joypad.m_stickCamera.y;
		}

		joypad.m_fromLookAtControl = true;
	}
	else
	{
		m_inputController.SetPrevOutput(joypad.m_stickCamera);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraLookAtController::Error CameraLookAtController::GetError() const
{
	return m_lastError;
}

/// --------------------------------------------------------------------------------------------------------------- ///
//void CameraLookAtController::CalcErrorMode(bool zoomCamOnTop, const Locator& lastFrameTopCamLoc)
//{
//	// calculate error mode
//	if (m_errorMode == CameraLookAtErrorMode::kInvalid)
//	{
//		m_errorMode = CameraLookAtErrorMode::kInWorldSpace;
//
//		if (zoomCamOnTop)
//		{
//			const Point targetPos = m_lookAtParams2.m_target.GetLookAtPosWS(lastFrameTopCamLoc.GetTranslation());
//			const Point targetInCamSpace = lastFrameTopCamLoc.UntransformPoint(targetPos);
//			const Vector toTarget = targetInCamSpace - Point(kOrigin);
//			float dotP = Dot(SafeNormalize(toTarget, kZero), Vector(kUnitZAxis));
//			bool invalid = dotP < 0.f;
//			if (!invalid)
//			{
//				m_errorMode = CameraLookAtErrorMode::kInCameraSpace;
//			}
//		}
//	}
//	else if (m_errorMode == CameraLookAtErrorMode::kInCameraSpace && !zoomCamOnTop)
//	{
//		// in case the zoom camera is gone.
//		m_errorMode = CameraLookAtErrorMode::kInWorldSpace;
//	}
//}

/// --------------------------------------------------------------------------------------------------------------- ///
CameraLookAtController::AdjustResult CameraLookAtController::AdjustByStickInput(const StickAdjustment& adju, const CameraJoypad& actualJoypad, const Vec2& currError, 
	Point_arg targetPosWs, const Locator& refCamLoc, float angleRange) const
{
	AdjustResult res;
	res.valid = false;

	OscilattingInfo oscRes;
	oscRes.Reset();

	const float rawX = actualJoypad.GetX(m_playerIndex);
	const float rawY = actualJoypad.GetY(m_playerIndex);

	// @TODO SERVER-PLAYER
	const bool xFlipped = EngineComponents::GetNdGameInfo()->m_CameraXMode[0];
	const bool yFlipped = EngineComponents::GetNdGameInfo()->m_CameraYMode[0];

	const bool cameraMovedByStick = abs(rawX) > 0.f || abs(rawY) > 0.f;

	float angleDiffDeg;
	{
		const Vector refCamDir = GetLocalZ(refCamLoc.GetRotation());
		const Vector refCam2TgtPosWs = targetPosWs - refCamLoc.GetTranslation();
		const Vector refCam2TgtPosNorm = SafeNormalize(refCam2TgtPosWs, kZero);
		const float dotP = Dot(refCamDir, refCam2TgtPosNorm);
		angleDiffDeg = RADIANS_TO_DEGREES(SafeAcos(dotP));
	}

	float factor = 0.f;
	float finalX = rawX;
	float finalY = rawY;

	Vector currErrNorm(kZero);
	Vector limitDirNorm(kZero);

	{
		const float currErrorLen = currError.Length();
		Vec2 predictedErr = currError;
		{
			predictedErr.x += (xFlipped ? -rawX : rawX) * 0.5f;		// 0.2f is a hypothesis value, after a short dT.
			predictedErr.y += (yFlipped ? -rawY : rawY) * 0.5f;
		}
		const float predictedErrLen = predictedErr.Length();

		const float defaultStick = 0.5f;

		// ramp up by time.
		const float timeFactor1 = LerpScaleClamp(0.f, 0.2f, 0.f, 1.f, adju.m_hasInputTime);

		if (angleDiffDeg < angleRange * 0.5f)
		{
			factor = defaultStick * timeFactor1;
		}
		else 
		{
			if (cameraMovedByStick)
			{
				// allow stick to slide around circle.
				currErrNorm = SafeNormalize(Vector(currError.x, currError.y, 0.f), kZero);
				const Vector delta = Vector(rawX, rawY, 0.f);
				const Vector deltaNorm = SafeNormalize(delta, kZero);
				const float dotP = Dot(currErrNorm, deltaNorm);

				const float dotThres = LerpScaleClamp(0.9f * angleRange, angleRange, 1.f, 0.f, predictedErrLen);

				const float oscilattingDotThres = 0.995f;

				if (dotP >= dotThres)
				{
					{
						const float halfAngle = Acos(dotThres);

						const Quat limitRot0 = QuatFromAxisAngle(Vector(0.f, 0.f, 1.f), halfAngle);
						const Quat limitRot1 = QuatFromAxisAngle(Vector(0.f, 0.f, 1.f), -halfAngle);

						const Vector limitDirNorm0 = Rotate(limitRot0, currErrNorm);
						const Vector limitDirNorm1 = Rotate(limitRot1, currErrNorm);
						ASSERT(Abs(Dot(limitDirNorm0, currErrNorm) - dotThres) < 0.00001f);
						ASSERT(Abs(Dot(limitDirNorm1, currErrNorm) - dotThres) < 0.00001f);
						const float dotProd0 = Dot(limitDirNorm0, deltaNorm);
						const float dotProd1 = Dot(limitDirNorm1, deltaNorm);

						limitDirNorm = dotProd0 > dotProd1 ? limitDirNorm0 : limitDirNorm1;
					}

					// use previous frame limitDirNorm to prevent oscilatting.
					if (adju.m_lastFrameOscInfo.m_valid)
					{
						if (dotP > oscilattingDotThres)
						{
							float dotTest = Dot(adju.m_lastFrameOscInfo.m_errorNorm, currErrNorm);
							if (dotTest > oscilattingDotThres)
							{
								limitDirNorm = adju.m_lastFrameOscInfo.m_limitDirNorm;
							}
						}
					}

					const float oscilattingDotThres2 = adju.m_lastFrameOscInfo.m_valid ? (oscilattingDotThres - 0.05f) : oscilattingDotThres;
					if (dotP > oscilattingDotThres2 && dotThres < 0.7f)
					{
						oscRes.m_valid = true;
						oscRes.m_dotProdInputAndError = dotP;
						oscRes.m_errorNorm = currErrNorm;
						oscRes.m_limitDirNorm = limitDirNorm;
					}

					Vector adjustedDelta = Dot(delta, limitDirNorm) * limitDirNorm;
					finalX = adjustedDelta.X();
					finalY = adjustedDelta.Y();
				}
			}

			factor = LerpScaleClamp(angleRange * 0.5f, angleRange, defaultStick, 0.2f, predictedErrLen * timeFactor1);
		}
	}

	if (!adju.m_moved && cameraMovedByStick)
	{
		if (angleDiffDeg < m_lookAtParams2.m_adjustmentAngle)
		{
			res.joypad = Vec2(finalX, finalY) * factor;
			res.valid = true;
		}
	}
	else if (adju.m_moved)
	{
		//if (angleDiffDeg < m_lookAtParams2.m_adjustmentAngle * 1.1f)
		{
			res.joypad = Vec2(finalX, finalY) * factor;
			res.valid = true;
		}
	}

	//{
	//	const Vec2 dbgStart(0.5f, 0.5f);
	//	Color c = res.valid ? kColorCyan : kColorRed;
	//	g_prim.Draw(DebugLine2D(dbgStart, dbgStart + Vec2(rawX, rawY) * 0.2f, kDebug2DNormalizedCoords, kColorBlue, 8.f), kPrimDuration1FramePauseable);
	//	g_prim.Draw(DebugLine2D(dbgStart, dbgStart + Vec2(finalX, finalY) * 0.2f, kDebug2DNormalizedCoords, c, 4.f), kPrimDuration1FramePauseable);

	//	if (LengthSqr(limitDirNorm) > 0.01f)
	//		g_prim.Draw(DebugLine2D(dbgStart, dbgStart + Vec2(limitDirNorm.X(), limitDirNorm.Y()) * 0.07f, kDebug2DNormalizedCoords, kColorOrange, 4.f), kPrimDuration1FramePauseable);

	//	//if (LengthSqr(limitDirNorm1) > 0.01f)
	//	//	g_prim.Draw(DebugLine2D(dbgStart, dbgStart + Vec2(limitDirNorm1.X(), limitDirNorm1.Y()) * 0.07f, kDebug2DNormalizedCoords, kColorPink, 4.f), kPrimDuration1FramePauseable);

	//	if (LengthSqr(currErrNorm) > 0.01f)
	//		g_prim.Draw(DebugLine2D(dbgStart, dbgStart + Vec2(currErrNorm.X(), currErrNorm.Y()) * 0.2f, kDebug2DNormalizedCoords, kColorRed, 8.f), kPrimDuration1FramePauseable);

	//	g_prim.Draw(DebugString2D(dbgStart, kDebug2DNormalizedCoords, StringBuilder<32>("%f", factor).c_str(), kColorWhite, 0.7f), kPrimDuration1FramePauseable);
	//}

	res.oscRes = oscRes;
	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CameraLookAtController::IsMovedByStick() const
{
	return m_stickAdjustment.m_allowed && m_stickAdjustment.m_moved;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CameraLookAtController::IsComplete( const Vec2& error ) const
{
	return Max(Abs(error.X()), Abs(error.Y())) < m_lookAtParams2.m_completionAngle;
}