/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef NDLIB_CAMERA_ARC_H
#define NDLIB_CAMERA_ARC_H

#include "corelib/math/basicmath.h"

#include "ndlib/render/util/prim.h"

#include "gamelib/scriptx/h/nd-camera-settings.h"


extern float g_cameraFOVScale;
extern float g_cameraOffsetScale;
extern float g_cameraMidXScale;

// -------------------------------------------------------------------------------------------------
// Bezier curve traced out by game cameras when swinging vertically about a target.
// -------------------------------------------------------------------------------------------------

class CameraArc
{
public:

	// Types and Constants

	struct BezierPoints
	{
		Vec2 m_p[7];
		float m_segmentArcLength[2];
	};

	static void ReadSettingsFromSpawner(DC::CameraArcSettings& arcSettings, const EntitySpawner* pSpawner);

	CameraArc() {}

	void Setup(DC::CameraArcSettings* pSettings, DC::CameraArcSettings* pDebugSettings = nullptr);
	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);
	void SetPhotoModeDistScale(float photoModeDistScale, float photoModeDistScaleY) 
	{ 
		m_photoModeDistScale = photoModeDistScale; 
		m_photoModeDistScaleY = photoModeDistScaleY; 
	}
	void Update();

	Vec2 CalcCameraOffset(float arc) const;
	Vec2 CalcTargetOffset(float arc) const;

	void UpdateTargetPointArc(Vec2_arg delta);
	void UpdateCameraPointArc(Vec2_arg delta);

	Vec2 CalcCameraOffsetDerivative(float arc) const;
	Vec2 CalcTargetOffsetDerivative(float arc) const;

	float CalculateCorrespondingArc(float x, float y) const;

	Point OffsetTargetPoint(Point_arg baseTargetPos, Vector_arg unitDirXZ, float arc, Vector_arg up) const;

	//Vec2 CalcCameraOffsetDeriv(float arc);
	//Vec2 CalcTargetOffsetDeriv(float arc);

	float MoveAlongCameraCurve(float arc, float deltaLength);

	static Vector OffsetToWorldSpace(Vec2_arg offset, Point_arg baseTargetPos, Vector_arg cameraDirXZ, Vector_arg cameraDirY);

	// Debugging and Development
	void DisplayCurve(float currentArc, bool enableTargetCurve=false, DebugPrimTime tt = kPrimDuration1FrameAuto);
	void DisplayCurveWs(float currentArc, Point_arg arcCenter, Vector_arg centerToCameraDirXZ, Vector up, DebugPrimTime tt = kPrimDuration1FrameAuto);
	static void DumpSettingsToTty(const DC::CameraArcSettings& settings);
	static void DumpSettingsToTtyTags(const DC::CameraArcSettings& settings);

private:
	void DebugInput(bool enableTargetCurve);
	void PushDebugSettings();
	void CalcCameraBezierPoints(BezierPoints& cameraPoints);
	void CalcTargetBezierPoints(BezierPoints& targetPoints);

	DC::CameraArcSettings* m_pSettings = nullptr;
	DC::CameraArcSettings* m_pDebugSettings = nullptr;

	BezierPoints m_cameraPoints;
	BezierPoints m_targetPoints;

	float m_photoModeDistScale = 1.0f;
	float m_photoModeDistScaleY = 1.0f;

	// User Tweaking
	int m_pointSelected = 0;
	int m_targetPointSelected = 0;
};

//helper funcs -- moved from camera-strafe.cpp so other cameras can use them too
float PitchFromDir(Vector_arg v);
float GetWsPitchAngleFroArc(const CameraArc& arc, const float arcPos);
float GetDeltaAnglePerT(const CameraArc& arc, const float arcPos);
float GetDeltaArcForDeltaAngle(const CameraArc& arc, const float arcPos, float deltaAngle);
float ArcPitchAngleError(float arc, void* pContext);
float SolveArcForPitch(const CameraArc& arc, const float arc0, const float arc1, const float desiredPitch);
float ClampArcToPitchAngles(const CameraArc& arc, const float arcPos);
float ClampArcToPitchAngles(const CameraArc& arc, const float arcPos, float minPitch, float maxPitch); //takes pitch in radians

#endif //#ifndef NDLIB_CAMERA_ARC_H
