/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/camera/camera-arc.h"

#include "corelib/math/bisection.h"

#include "ndlib/math/pretty-math.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/render/display.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/camera/camera-manager.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/scriptx/h/nd-camera-settings.h"

#define DEBUG_DRAW 1

bool g_maccDumpIterations = false;
U32 g_maccMaxIterations = 0u;

float g_cameraMidXScale = 1.0f;
float g_cameraOffsetScale = 1.0f;
float g_cameraFOVScale = 1.0f;

float kDebugDrawScale = 60.0f;
float kDebugDrawOffset = 0.0f;
static float kDebugDrawWidth = 1000.0f;
static float kDebugDrawHeight = 500.0f;

// This order presumably helps resolve mouse picking conflicts...
enum { k0 = 1, k1 = 2, k2 = 4, k3 = 5, k4 = 0, k5 = 3, k6 = 6, };

// -------------------------------------------------------------------------------------------------

inline Vec2 EvaluateBezierCurve(const Vec2 p[], Scalar t)
{
	Scalar t2 = t * t;
	Scalar t3 = t * t2;
	Scalar it = Scalar(1.0f) - t;
	Scalar it2 = it * it;
	Scalar it3 = it2 * it;
	Scalar three(3.0f);

	return p[0] * it3 + p[1] * three*it2*t + p[2] * three*it*t2 + p[3] * t3;
}

inline Vec2 EvaluateBezierDerivative(const Vec2 p[], Scalar t)
{
	Scalar t2 = t * t;
	Scalar it = Scalar(1.0f) - t;
	Scalar it2 = it * it;
	Scalar three(3.0f);
	Scalar six(6.0f);

	return (p[1] - p[0]) * three*it2 + (p[2] - p[1]) * six*t*it + (p[3] - p[2]) * three*t2;
}

static F32 EvaluateBezierArcLengthIntegrand(F32 t, uintptr_t userData)
{
	Vec2 *pIntegrandPoints = (Vec2 *)userData;
	Vec2 d = EvaluateBezierDerivative(pIntegrandPoints, t);
	return Length(d);
}

// Use 5-point Gauss-Legendre quadrature.  We could probably get away with 3-point if need be...
#define Integrate IntegrateGaussLegendre5

static F32 CalcBezierArcLength(const Vec2 p[], F32 t0, F32 t1)
{
	F32 length = Integrate(t0, t1, EvaluateBezierArcLengthIntegrand, (uintptr_t)p);
	return length;
}

// -------------------------------------------------------------------------------------------------

void CameraArc::ReadSettingsFromSpawner(DC::CameraArcSettings& arcSettings, const EntitySpawner* pSpawner)
{
	const EntityDB * pDb = (pSpawner != nullptr) ? pSpawner->GetEntityDB() : nullptr;
	if (pDb)
	{
		arcSettings.m_closeX = pDb->GetData<float>(SID("close-x"), arcSettings.m_closeX);
		arcSettings.m_closeY = pDb->GetData<float>(SID("close-y"), arcSettings.m_closeY);
		arcSettings.m_closeAngle = pDb->GetData<float>(SID("close-angle"), arcSettings.m_closeAngle);
		arcSettings.m_closeScale = pDb->GetData<float>(SID("close-scale"), arcSettings.m_closeScale);
		arcSettings.m_closeTargetOffset = pDb->GetData<float>(SID("close-target-offset"), arcSettings.m_closeTargetOffset);
		arcSettings.m_closeTargetOffsetZ = pDb->GetData<float>(SID("close-target-offset-z"), arcSettings.m_closeTargetOffsetZ);
		arcSettings.m_closeTargetAngle = pDb->GetData<float>(SID("close-target-angle"), arcSettings.m_closeTargetAngle);
		arcSettings.m_closeTargetScale = pDb->GetData<float>(SID("close-target-scale"), arcSettings.m_closeTargetScale);

		arcSettings.m_midX = pDb->GetData<float>(SID("mid-x"), arcSettings.m_midX);
		arcSettings.m_midY = pDb->GetData<float>(SID("mid-y"), arcSettings.m_midY);
		arcSettings.m_midAngle = pDb->GetData<float>(SID("mid-angle"), arcSettings.m_midAngle);
		arcSettings.m_midScaleLeft = pDb->GetData<float>(SID("mid-scale-left"), arcSettings.m_midScaleLeft);
		arcSettings.m_midScaleRight = pDb->GetData<float>(SID("mid-scale-right"), arcSettings.m_midScaleRight);
		arcSettings.m_midTargetOffset = pDb->GetData<float>(SID("mid-target-offset"), arcSettings.m_midTargetOffset);
		arcSettings.m_midTargetOffsetZ = pDb->GetData<float>(SID("mid-target-offset-z"), arcSettings.m_midTargetOffsetZ);
		arcSettings.m_midTargetAngle = pDb->GetData<float>(SID("mid-target-angle"), arcSettings.m_midTargetAngle);
		arcSettings.m_midTargetScaleLeft = pDb->GetData<float>(SID("mid-target-scale-left"), arcSettings.m_midTargetScaleLeft);
		arcSettings.m_midTargetScaleRight = pDb->GetData<float>(SID("mid-target-scale-right"), arcSettings.m_midTargetScaleRight);

		arcSettings.m_farX = pDb->GetData<float>(SID("far-x"), arcSettings.m_farX);
		arcSettings.m_farY = pDb->GetData<float>(SID("far-y"), arcSettings.m_farY);
		arcSettings.m_farAngle = pDb->GetData<float>(SID("far-angle"), arcSettings.m_farAngle);
		arcSettings.m_farScale = pDb->GetData<float>(SID("far-scale"), arcSettings.m_farScale);
		arcSettings.m_farTargetOffset = pDb->GetData<float>(SID("far-target-offset"), arcSettings.m_farTargetOffset);
		arcSettings.m_farTargetOffsetZ = pDb->GetData<float>(SID("far-target-offset-z"), arcSettings.m_farTargetOffsetZ);
		arcSettings.m_farTargetAngle = pDb->GetData<float>(SID("far-target-angle"), arcSettings.m_farTargetAngle);
		arcSettings.m_farTargetScale = pDb->GetData<float>(SID("far-target-scale"), arcSettings.m_farTargetScale);
	}
}

void CameraArc::Setup(DC::CameraArcSettings* pSettings, DC::CameraArcSettings* pDebugSettings)
{
	m_pSettings = pSettings;
	m_pDebugSettings = pDebugSettings;
	Update();
}

void CameraArc::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pSettings, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_pDebugSettings, deltaPos, lowerBound, upperBound); // typically a global struct, but no harm in trying to relocate it
}

void CameraArc::CalcCameraBezierPoints(BezierPoints& cameraPoints)
{
	Scalar pmScale(m_photoModeDistScale);
	Scalar pmScaleY(m_photoModeDistScaleY);

	cameraPoints.m_p[0] = Vec2(m_pSettings->m_closeX, m_pSettings->m_closeY) * pmScale;
	cameraPoints.m_p[1] = cameraPoints.m_p[0] + m_pSettings->m_closeScale * pmScale * Vec2(Cos(DEGREES_TO_RADIANS(m_pSettings->m_closeAngle)), Sin(DEGREES_TO_RADIANS(m_pSettings->m_closeAngle)));

	cameraPoints.m_p[3] = Vec2(m_pSettings->m_midX * g_cameraMidXScale, m_pSettings->m_midY) * pmScale;
	cameraPoints.m_p[2] = cameraPoints.m_p[3] - m_pSettings->m_midScaleLeft * pmScale * Vec2(Cos(DEGREES_TO_RADIANS(m_pSettings->m_midAngle)), Sin(DEGREES_TO_RADIANS(m_pSettings->m_midAngle)));
	cameraPoints.m_p[4] = cameraPoints.m_p[3] + m_pSettings->m_midScaleRight * pmScaleY * pmScale * Vec2(Cos(DEGREES_TO_RADIANS(m_pSettings->m_midAngle)), Sin(DEGREES_TO_RADIANS(m_pSettings->m_midAngle)));

	cameraPoints.m_p[6] = Vec2(m_pSettings->m_farX, m_pSettings->m_farY * pmScaleY) * pmScale;
	cameraPoints.m_p[5] = cameraPoints.m_p[6] - m_pSettings->m_farScale * pmScaleY * pmScale * Vec2(Cos(DEGREES_TO_RADIANS(m_pSettings->m_farAngle)), Sin(DEGREES_TO_RADIANS(m_pSettings->m_farAngle)));

	cameraPoints.m_segmentArcLength[0] = CalcBezierArcLength(&cameraPoints.m_p[0], 0.0f, 1.0f);
	cameraPoints.m_segmentArcLength[1] = CalcBezierArcLength(&cameraPoints.m_p[3], 0.0f, 1.0f);
}	

void CameraArc::CalcTargetBezierPoints(BezierPoints& targetPoints)
{
	targetPoints.m_p[0] = Vec2(-m_pSettings->m_closeTargetOffsetZ, m_pSettings->m_closeTargetOffset);
	targetPoints.m_p[1] = targetPoints.m_p[0] + m_pSettings->m_closeTargetScale * Vec2(Cos(DEGREES_TO_RADIANS(m_pSettings->m_closeTargetAngle)), Sin(DEGREES_TO_RADIANS(m_pSettings->m_closeTargetAngle)));

	targetPoints.m_p[3] = Vec2(-m_pSettings->m_midTargetOffsetZ, m_pSettings->m_midTargetOffset);
	targetPoints.m_p[2] = targetPoints.m_p[3] - m_pSettings->m_midTargetScaleLeft * Vec2(Cos(DEGREES_TO_RADIANS(m_pSettings->m_midTargetAngle)), Sin(DEGREES_TO_RADIANS(m_pSettings->m_midTargetAngle)));
	targetPoints.m_p[4] = targetPoints.m_p[3] + m_pSettings->m_midTargetScaleRight * Vec2(Cos(DEGREES_TO_RADIANS(m_pSettings->m_midTargetAngle)), Sin(DEGREES_TO_RADIANS(m_pSettings->m_midTargetAngle)));

	targetPoints.m_p[6] = Vec2(-m_pSettings->m_farTargetOffsetZ, m_pSettings->m_farTargetOffset);
	targetPoints.m_p[5] = targetPoints.m_p[6] - m_pSettings->m_farTargetScale * Vec2(Cos(DEGREES_TO_RADIANS(m_pSettings->m_farTargetAngle)), Sin(DEGREES_TO_RADIANS(m_pSettings->m_farTargetAngle)));

	// Don't bother calculating these unless we need them...
	//targetPoints.m_segmentArcLength[0] = CalcBezierArcLength(&targetPoints.m_p[0], 0.0f, 1.0f);
	//targetPoints.m_segmentArcLength[1] = CalcBezierArcLength(&targetPoints.m_p[3], 0.0f, 1.0f);
	targetPoints.m_segmentArcLength[0] = 0.0f;
	targetPoints.m_segmentArcLength[1] = 0.0f;
}

void CameraArc::Update()
{
	if (m_pSettings != nullptr)
	{
		CalcCameraBezierPoints(m_cameraPoints);
		CalcTargetBezierPoints(m_targetPoints);
	}
}

static float MoveAlongCurve(const Vec2 p[], float segmentLength, float t, float dLength, float* pRemainingLength)
{
	if (pRemainingLength)
		*pRemainingLength = 0.0f;

	if (segmentLength == 0.0f || dLength == 0.0f)
		return t;

	// start with an estimate of delta-t based on dLength and segmentLength
	float dt = dLength / segmentLength;

	float tNew = -1.0f, t0, t1, dLengthDesired, dLengthActual, sign, err = -1.0f;
	if (dLength > 0.0f)
	{
		dLengthDesired = dLength;
		sign = +1.0f;
	}
	else
	{
		dLengthDesired = -dLength;
		sign = -1.0f;
	}

	// converge on a more accurate solution
	enum { kMaccMaxIterations = 5u };
	U32F i;
	for (i = 0u; i < kMaccMaxIterations; ++i)
	{
		tNew = t + dt;

		//tNew = MinMax01(tNew);
		if (tNew > 1.0f)
		{
			tNew = 1.0f;
			ASSERT(sign > 0.0f);
			i = kMaccMaxIterations; // no where left to go, so abort
		}
		else if (tNew < 0.0f)
		{
			tNew = 0.0f;
			ASSERT(sign < 0.0f);
			i = kMaccMaxIterations; // no where left to go, so abort
		}

		if (t < tNew)	{ t0 = t; t1 = tNew; }
		else			{ t1 = t; t0 = tNew; }

		dLengthActual = CalcBezierArcLength(p, t0, t1); // absolute value

		static float kTolerancePct = 0.1f/100.0f;
		float tolerance = kTolerancePct * dLengthDesired;

		err = dLengthDesired - dLengthActual;
		if (err * err <= tolerance * tolerance)
			break;

		// refine the estimate
		if (dLengthActual > 0.0f)
		{
			float adjustment = dLengthDesired / dLengthActual;
			dt *= adjustment;
		}
		else if (tNew == 1.0f || tNew == 0.0f)
		{
			break;
		}
		else
		{
			dt = 2.0f * (dLength / segmentLength); // crazy special case: the last dt didn't actually move
		}
	}

	if (pRemainingLength && err > 0.0f && (tNew == 0.0f || tNew == 1.0f))
	{
		// We clamped tNew but there's still more distance to cover.
		*pRemainingLength = sign * err;
	}

	if (g_maccDumpIterations)
	{
		if (i > g_maccMaxIterations && i <= kMaccMaxIterations)
			g_maccMaxIterations = i;
		MsgCamera("MoveAlongCurve() converged after %u iters (%u high water)\n", (U32)i, g_maccMaxIterations);
	}

	return tNew;
}

float CameraArc::MoveAlongCameraCurve(float arc, float deltaLength)
{
	float arcNew;
	float remainingLength;

	arc = MinMax01(arc);

	if (arc <= 0.5f)
	{
		// Move within the lower segment.

		float t = arc * 2.0f;

		float tNew = MoveAlongCurve(&m_cameraPoints.m_p[0], m_cameraPoints.m_segmentArcLength[0], t, deltaLength, &remainingLength);

		arcNew = tNew * 0.5f;

		if (remainingLength != 0.0f && tNew == 1.0f)
		{
			// We moved into the upper segment.
			tNew = MoveAlongCurve(&m_cameraPoints.m_p[3], m_cameraPoints.m_segmentArcLength[1], 0.0f, remainingLength, nullptr);
			arcNew = (tNew * 0.5f) + 0.5f;
		}
	}
	else
	{
		// Move within the upper segment.

		float t = ((arc - 0.5f) * 2.0f);

		float tNew = MoveAlongCurve(&m_cameraPoints.m_p[3], m_cameraPoints.m_segmentArcLength[1], t, deltaLength, &remainingLength);

		arcNew = (tNew * 0.5f) + 0.5f;

		if (remainingLength != 0.0f && tNew == 0.0f)
		{
			// We moved into the lower segment.
			tNew = MoveAlongCurve(&m_cameraPoints.m_p[0], m_cameraPoints.m_segmentArcLength[0], 1.0f, remainingLength, nullptr);
			arcNew = tNew * 0.5f;
		}
	}

	return arcNew;
}

///-----------------------------------------------------------------------------------------------------------///
void CameraArc::UpdateTargetPointArc(Vec2_arg delta)
{
	// every target points are shifted by an offset, the overall segment length won't change.
	for (int i = 0; i < ARRAY_COUNT(m_targetPoints.m_p); i++)
	{
		m_targetPoints.m_p[i] += Vec2(delta.x, delta.y);
	}
}

///-----------------------------------------------------------------------------------------------------------///
void CameraArc::UpdateCameraPointArc(Vec2_arg delta)
{
	// every camera points are shifted by an offset, the overall segment length won't change.
	for (int i = 0; i < ARRAY_COUNT(m_cameraPoints.m_p); i++)
	{
		m_cameraPoints.m_p[i] += Vec2(delta.x, delta.y);
	}
}

///-----------------------------------------------------------------------------------------------------------///
Vec2 CameraArc::CalcCameraOffset(float arc) const
{
	arc = MinMax01(arc);
	if (arc <= 0.5f)
	{
		Scalar t(arc * 2.0f);
		return EvaluateBezierCurve(&m_cameraPoints.m_p[0], t);
	}
	else
	{
		Scalar t((arc - 0.5f) * 2.0f);
		return EvaluateBezierCurve(&m_cameraPoints.m_p[3], t);
	}
}

Vec2 CameraArc::CalcCameraOffsetDerivative(float arc) const
{
	arc = MinMax01(arc);
	if (arc <= 0.5f)
	{
		Scalar t(arc * 2.0f);
		return 2.0f * EvaluateBezierDerivative(&m_cameraPoints.m_p[0], t);
	}
	else
	{
		Scalar t((arc - 0.5f) * 2.0f);
		return 2.0f * EvaluateBezierDerivative(&m_cameraPoints.m_p[3], t);
	}
}

float CameraArc::CalculateCorrespondingArc(float x, float y) const
{
	Vec2 desiredNorm(x, y);
	desiredNorm = Normalize(desiredNorm);

	const int kNumSteps = 100;
	Scalar bestDot = SCALAR_LC(-1.0f);
	float bestArc = -1.0f;
	

	for (int i = 0; i < kNumSteps; ++i)
	{
		float arc = (1.0f / (kNumSteps-1)) * i;

		Vec2 r = CalcCameraOffset(arc);
		r = Normalize(r);

		Scalar d = Dot(desiredNorm, r);

		if (d > bestDot)
		{
			bestDot = d;
			bestArc = arc;
		}
	}

	return bestArc;
}

Vec2 CameraArc::CalcTargetOffset(float arc) const
{
	arc = MinMax01(arc);
	if (arc <= 0.5f)
	{
		Scalar t(arc * 2.0f);
		if (m_pSettings->m_targetBezierCurve)
			return EvaluateBezierCurve(&m_targetPoints.m_p[0], t);
		else
			return Lerp(m_targetPoints.m_p[0], m_targetPoints.m_p[3], t);
	}
	else
	{
		Scalar t((arc - 0.5f) * 2.0f);
		if (m_pSettings->m_targetBezierCurve)
			return EvaluateBezierCurve(&m_targetPoints.m_p[3], t);
		else
			return Lerp(m_targetPoints.m_p[3], m_targetPoints.m_p[6], t);
	}
}

Vec2 CameraArc::CalcTargetOffsetDerivative(float arc) const
{
	arc = MinMax01(arc);
	if (arc <= 0.5f)
	{
		Scalar t(arc * 2.0f);
		if (m_pSettings->m_targetBezierCurve)
			return 2.0f * EvaluateBezierDerivative(&m_targetPoints.m_p[0], t);
		else
			return 2.0f * (m_targetPoints.m_p[3] - m_targetPoints.m_p[0]);
	}
	else
	{
		Scalar t((arc - 0.5f) * 2.0f);
		if (m_pSettings->m_targetBezierCurve)
			return 2.0f * EvaluateBezierDerivative(&m_targetPoints.m_p[3], t);
		else
			return 2.0f * (m_targetPoints.m_p[6] - m_targetPoints.m_p[3]);
	}
}

Point CameraArc::OffsetTargetPoint(Point_arg baseTargetPos, Vector_arg unitDirXZ, float arc, Vector_arg up) const
{
	const Vec2 offset = CalcTargetOffset(arc);
	const float offsetXz = offset.X();
	const float offsetY = offset.Y();
	const Point offsetTarget = baseTargetPos + up * offsetY + unitDirXZ * offsetXz;
	return offsetTarget;
}

void CameraArc::PushDebugSettings()
{
	// Copy my local settings into the global debug settings struct.
	if (m_pDebugSettings)
	{
		ASSERT(m_pSettings);
		*m_pDebugSettings = *m_pSettings;
	}
	Update();
}

/*
Vector CameraArc::CalcCameraOffsetDeriv(float arc)
{
	if (arc <= 0.5f)
	{
		float t = arc * 2.0f;
		return EvaluateBezierDerivative(&m_cameraPoints.m_p[0], t);
	}
	else
	{
		float t = (arc - 0.5f) * 2.0f;
		return EvaluateBezierDerivative(&m_cameraPoints.m_p[3], t);
	}
}

Vector CameraArc::CalcTargetOffsetDeriv(float arc)
{
	if (arc <= 0.5f)
	{
		float t = arc * 2.0f;
		return EvaluateBezierDerivative(&m_targetPoints.m_p[0], t);
	}
	else
	{
		Scalar t((arc - 0.5f) * 2.0f);
		return EvaluateBezierCurve(&m_targetPoints.m_p[3], t);
	}
}
*/

/*
struct ArcLengthParams
{
	float m_closeArcLength;
	float m_midArcLength;
	float m_farArcLength;
	float m_minMidAngle;
	float m_maxMidAngle;
	Vector m_closeMidDir;
	Vector m_midCenterOffset;
	float m_farRadius;
};

float CameraArc::CalcArcLength(ArcLengthParams& params)
{
	Vector midOffset = Vector(m_pSettings->m_midX, m_pSettings->m_midY, 0.0f);
	Vector closeOffset = Vector(m_pSettings->m_closeX, m_pSettings->m_closeY, 0.0f);
	Vector closeMidDelta = midOffset - closeOffset;

	params.m_closeArcLength = Length(closeMidDelta);

	params.m_closeMidDir = SafeNormalize(closeMidDelta, SMath::kUnitZAxis);
	params.m_midCenterOffset = midOffset + m_pSettings->m_midRadius * Vector(-params.m_closeMidDir.Y(), params.m_closeMidDir.X(), 0.0f);

	params.m_minMidAngle = DEGREES_TO_RADIANS(-90.0f) + Atan2((float)params.m_closeMidDir.Y(),(float)params.m_closeMidDir.X());
	params.m_maxMidAngle = Atan2((float)params.m_midCenterOffset.Y(), (float)params.m_midCenterOffset.X());

	params.m_midArcLength = (params.m_maxMidAngle - params.m_minMidAngle) * m_pSettings->m_midRadius;

	params.m_farRadius = Length(params.m_midCenterOffset) + m_pSettings->m_midRadius;

	params.m_farArcLength = (DEGREES_TO_RADIANS(m_pSettings->m_farAngle) - params.m_maxMidAngle) * params.m_farRadius;

	return params.m_closeArcLength + params.m_midArcLength + params.m_farArcLength;
}
*/

Vector CameraArc::OffsetToWorldSpace(Vec2_arg offset, Point_arg baseTargetPos, Vector_arg cameraDirXZ, Vector_arg cameraDirY)
{
	const Scalar offsetXZ = offset.X();
	const Scalar offsetY = offset.Y();
	const Vector offsetWs = cameraDirXZ * offsetXZ + cameraDirY * offsetY;
	return offsetWs;
}

// -------------------------------------------------------------------------------------------------
// Debugging
// -------------------------------------------------------------------------------------------------

static Vec2 GetPointScreenCoords(Vec2_arg point)
{
	Vec2 width(kDebugDrawWidth, 0);
	Vec2 height(0, kDebugDrawHeight + kDebugDrawOffset);
	Vec2 origin(kVirtualScreenWidth - 100.0f, 100.0f);
	Vec2 ll = origin - width + height + Vec2(kDebugDrawScale * 0.5f, 0);

	return ll + kDebugDrawScale * Vec2(point.X(), - point.Y());
}

static bool MouseOverPoint(Vec2_arg point)
{
	Vec2 screenCoord;
	F32 dx, dy, distSq;
	Mouse& mouse = EngineComponents::GetNdFrameState()->m_mouse;
	float mx = mouse.m_position.x;
	float my = mouse.m_position.y;

	//mx *= (float)kVirtualScreenWidth/g_display.m_screenWidth;
	//my *= (float)kVirtualScreenHeight/g_display.m_screenHeight;

	screenCoord = GetPointScreenCoords(point);
	dx = mx - screenCoord.x;
	dy = my - screenCoord.y;
	distSq = dx*dx + dy*dy;

	return distSq < 200.0f;
}

static Vec2 CovertMouseMovement(const Vec2& point)
{
	return Vec2(point.x/kDebugDrawScale, -point.y/kDebugDrawScale);
}

static void DrawPoint(Vec2_arg offset, Color color, DebugPrimTime debugPrimTime)
{
	const float kPointWidth = 6.f;
	const float kPointHeight = 6.f;

	Vec2 porigin = GetPointScreenCoords(offset);
	Vec2 cpll = porigin - Vec2(kPointWidth, 0) + Vec2(0, kPointHeight);
	Vec2 cplr = porigin + Vec2(kPointWidth, 0) + Vec2(0, kPointHeight);
	Vec2 cpul = porigin - Vec2(kPointWidth, 0) - Vec2(0, kPointHeight);
	Vec2 cpur = porigin + Vec2(kPointWidth, 0) - Vec2(0, kPointHeight);

 	//g_prim.AddTriangle2D( cpur, cpul, cpll, kDebug2DLegacyCoords, color);
 	//g_prim.AddTriangle2D( cpll, cplr, cpur, kDebug2DLegacyCoords, color);
    g_prim.Draw(DebugQuad2D( cpur, cpul, cpll, cplr, kDebug2DLegacyCoords, color ), debugPrimTime);
}

static void DrawLine(Vec2_arg offset1, Vec2_arg offset2, Color color, DebugPrimTime debugPrimTime)
{
	Vec2 width(kDebugDrawWidth, 0);
	Vec2 height(0, kDebugDrawHeight);
	Vec2 origin(kVirtualScreenWidth - 100.0f, 100.0f + kDebugDrawOffset);
	Vec2 ll = origin - width + height + Vec2(kDebugDrawScale * 0.5f, 0);

	const float kPointWidth = kDebugDrawScale * 0.1f;
	const float kPointHeight = kDebugDrawScale * 0.1f;
	Vec2 porigin1 = ll + kDebugDrawScale * Vec2(offset1.X(), - offset1.Y());
	Vec2 porigin2 = ll + kDebugDrawScale * Vec2(offset2.X(), - offset2.Y());

	g_prim.Draw(DebugLine2D(porigin1, porigin2, kDebug2DLegacyCoords, color), debugPrimTime);
}

void CameraArc::DebugInput(bool enableTargetCurve)
{
	STRIP_IN_FINAL_BUILD;

	Update();

	BezierPoints cameraPoints = m_cameraPoints;
	BezierPoints targetPoints = m_targetPoints;

	float minY;
	minY =     (float)cameraPoints.m_p[0].Y();
	minY = Min((float)cameraPoints.m_p[1].Y(), minY);
	minY = Min((float)cameraPoints.m_p[2].Y(), minY);
	minY = Min((float)cameraPoints.m_p[3].Y(), minY);
	minY = Min((float)cameraPoints.m_p[4].Y(), minY);
	minY = Min((float)cameraPoints.m_p[5].Y(), minY);
	minY = Min((float)cameraPoints.m_p[6].Y(), minY);
	Vec2 displayOffset(0, -minY);

	Mouse& mouse = EngineComponents::GetNdFrameState()->m_mouse;

	if (mouse.m_buttonsPressed & kMouseButtonLeft)
	{
		if      (MouseOverPoint(cameraPoints.m_p[k0] + displayOffset)) m_pointSelected = 2;
		else if (MouseOverPoint(cameraPoints.m_p[k1] + displayOffset)) m_pointSelected = 3;
		else if (MouseOverPoint(cameraPoints.m_p[k2] + displayOffset)) m_pointSelected = 5;
		else if (MouseOverPoint(cameraPoints.m_p[k3] + displayOffset)) m_pointSelected = 6;
		else if (MouseOverPoint(cameraPoints.m_p[k4] + displayOffset)) m_pointSelected = 1;
		else if (MouseOverPoint(cameraPoints.m_p[k5] + displayOffset)) m_pointSelected = 4;
		else if (MouseOverPoint(cameraPoints.m_p[k6] + displayOffset)) m_pointSelected = 7;

		if      (MouseOverPoint(targetPoints.m_p[k0] + displayOffset)) m_targetPointSelected = 2;
		else if (MouseOverPoint(targetPoints.m_p[k1] + displayOffset)) m_targetPointSelected = 3;
		else if (MouseOverPoint(targetPoints.m_p[k2] + displayOffset)) m_targetPointSelected = 5;
		else if (MouseOverPoint(targetPoints.m_p[k3] + displayOffset)) m_targetPointSelected = 6;
		else if (MouseOverPoint(targetPoints.m_p[k4] + displayOffset)) m_targetPointSelected = 1;
		else if (MouseOverPoint(targetPoints.m_p[k5] + displayOffset)) m_targetPointSelected = 4;
		else if (MouseOverPoint(targetPoints.m_p[k6] + displayOffset)) m_targetPointSelected = 7;
	}
	else if (!(mouse.m_buttons & kMouseButtonLeft))
	{
		m_pointSelected = 0;
		m_targetPointSelected = 0;
	}

	Vec2 movement = CovertMouseMovement(mouse.m_movement);

	if (m_pointSelected > 0)
	{
		if (m_pointSelected == 1)
		{
			m_pSettings->m_closeX += movement.x;
			m_pSettings->m_closeY += movement.y;
		}
		else if (m_pointSelected == 2)
		{
			cameraPoints.m_p[1] += Vec2(movement.x, movement.y);

			Vec2 dest;
			dest.x = cameraPoints.m_p[1].X() - cameraPoints.m_p[0].X();
			dest.y = cameraPoints.m_p[1].Y() - cameraPoints.m_p[0].Y();

			m_pSettings->m_closeScale = dest.Length();
			m_pSettings->m_closeAngle = RADIANS_TO_DEGREES(atan2f(dest.y, dest.x));
		}
		else if (m_pointSelected == 3)
		{
			cameraPoints.m_p[2] += Vec2(movement.x, movement.y);

			Vec2 dest;
			dest.x = cameraPoints.m_p[2].X() - cameraPoints.m_p[3].X();
			dest.y = cameraPoints.m_p[2].Y() - cameraPoints.m_p[3].Y();

			m_pSettings->m_midScaleLeft = dest.Length();
			m_pSettings->m_midAngle = RADIANS_TO_DEGREES(atan2f(-dest.y, -dest.x));
		}
		else if (m_pointSelected == 4)
		{
			m_pSettings->m_midX += movement.x;
			m_pSettings->m_midY += movement.y;
		}
		else if (m_pointSelected == 5)
		{
			cameraPoints.m_p[4] += Vec2(movement.x, movement.y);

			Vec2 dest;
			dest.x = cameraPoints.m_p[4].X() - cameraPoints.m_p[3].X();
			dest.y = cameraPoints.m_p[4].Y() - cameraPoints.m_p[3].Y();

			m_pSettings->m_midScaleRight = dest.Length();
			m_pSettings->m_midAngle = RADIANS_TO_DEGREES(atan2f(dest.y, dest.x));
		}
		else if (m_pointSelected == 6)
		{
			cameraPoints.m_p[5] += Vec2(movement.x, movement.y);

			Vec2 dest;
			dest.x = cameraPoints.m_p[5].X() - cameraPoints.m_p[6].X();
			dest.y = cameraPoints.m_p[5].Y() - cameraPoints.m_p[6].Y();

			m_pSettings->m_farScale = dest.Length();
			m_pSettings->m_farAngle = RADIANS_TO_DEGREES(atan2f(-dest.y, -dest.x));
		}
		else if (m_pointSelected == 7)
		{
			m_pSettings->m_farX += movement.x;
			m_pSettings->m_farY += movement.y;
		}

		PushDebugSettings();
	}
	else if (m_targetPointSelected > 0)
	{
		if (m_targetPointSelected == 1)
		{
			m_pSettings->m_closeTargetOffsetZ -= movement.x;
			m_pSettings->m_closeTargetOffset += movement.y;
		}
		else if (m_targetPointSelected == 2)
		{
			targetPoints.m_p[1] += Vec2(movement.x, movement.y);

			Vec2 dest;
			dest.x = targetPoints.m_p[1].X() - targetPoints.m_p[0].X();
			dest.y = targetPoints.m_p[1].Y() - targetPoints.m_p[0].Y();

			m_pSettings->m_closeTargetScale = dest.Length();
			m_pSettings->m_closeTargetAngle = RADIANS_TO_DEGREES(atan2f(dest.y, dest.x));
		}
		else if (m_targetPointSelected == 3)
		{
			targetPoints.m_p[2] += Vec2(movement.x, movement.y);

			Vec2 dest;
			dest.x = targetPoints.m_p[2].X() - targetPoints.m_p[3].X();
			dest.y = targetPoints.m_p[2].Y() - targetPoints.m_p[3].Y();

			m_pSettings->m_midTargetScaleLeft = dest.Length();
			m_pSettings->m_midTargetAngle = RADIANS_TO_DEGREES(atan2f(-dest.y, -dest.x));
		}
		else if (m_targetPointSelected == 4)
		{
			m_pSettings->m_midTargetOffsetZ -= movement.x;
			m_pSettings->m_midTargetOffset += movement.y;
		}
		else if (m_targetPointSelected == 5)
		{
			targetPoints.m_p[4] += Vec2(movement.x, movement.y);

			Vec2 dest;
			dest.x = targetPoints.m_p[4].X() - targetPoints.m_p[3].X();
			dest.y = targetPoints.m_p[4].Y() - targetPoints.m_p[3].Y();

			m_pSettings->m_midTargetScaleRight = dest.Length();
			m_pSettings->m_midTargetAngle = RADIANS_TO_DEGREES(atan2f(dest.y, dest.x));
		}
		else if (m_targetPointSelected == 6)
		{
			targetPoints.m_p[5] += Vec2(movement.x, movement.y);

			Vec2 dest;
			dest.x = targetPoints.m_p[5].X() - targetPoints.m_p[6].X();
			dest.y = targetPoints.m_p[5].Y() - targetPoints.m_p[6].Y();

			m_pSettings->m_farTargetScale = dest.Length();
			m_pSettings->m_farTargetAngle = RADIANS_TO_DEGREES(atan2f(-dest.y, -dest.x));
		}
		else if (m_targetPointSelected == 7)
		{
			m_pSettings->m_farTargetOffsetZ -= movement.x;
			m_pSettings->m_farTargetOffset += movement.y;
		}

		PushDebugSettings();
	}
}

void CameraArc::DisplayCurve(float currentArc, bool enableTargetCurve, DebugPrimTime debugPrimTime)
{
	STRIP_IN_FINAL_BUILD;

	DebugInput(enableTargetCurve);

	Vec2 width(kDebugDrawWidth, 0);
	Vec2 height(0, kDebugDrawHeight);
	Vec2 origin(kVirtualScreenWidth - 100.0f, 100.0f + kDebugDrawOffset);

	Vec2 ur = origin;
	Vec2 ul = origin - width;
	Vec2 ll = origin - width + height;
	Vec2 lr = origin + height;

    g_prim.Draw(DebugLine2D(ur, ul, kDebug2DLegacyCoords, kColorBlack, 2.0f), debugPrimTime);
    g_prim.Draw(DebugLine2D(ul, ll, kDebug2DLegacyCoords, kColorBlack, 2.0f), debugPrimTime);
    g_prim.Draw(DebugLine2D(ll, lr, kDebug2DLegacyCoords, kColorBlack, 2.0f), debugPrimTime);
    g_prim.Draw(DebugLine2D(lr, ur, kDebug2DLegacyCoords, kColorBlack, 2.0f), debugPrimTime);

	// firgure out the minimumY value
	float minY;
	minY =     (float)m_cameraPoints.m_p[0].Y();
	minY = Min((float)m_cameraPoints.m_p[1].Y(), minY);
	minY = Min((float)m_cameraPoints.m_p[2].Y(), minY);
	minY = Min((float)m_cameraPoints.m_p[3].Y(), minY);
	minY = Min((float)m_cameraPoints.m_p[4].Y(), minY);
	minY = Min((float)m_cameraPoints.m_p[5].Y(), minY);
	minY = Min((float)m_cameraPoints.m_p[6].Y(), minY);

	Vec2 displayOffset(0, -minY);

	Color background(0.4f, 0.4f, 0.4f, 1.0f);
//  	g_prim.AddTriangle2D( ur, ul, ll, background);
//  	g_prim.AddTriangle2D( ll, lr, ur, background);

	Vec2 porigin = ll + Vec2(kDebugDrawScale * 0.5f, 0);

	Vec2 offset = CalcCameraOffset(0.0f);
	Vec2 lastPoint = porigin + kDebugDrawScale * Vec2(offset.X(), - offset.Y() + minY);
	const int kNumPoints = 20;
	for (int i=1; i<kNumPoints + 1; i++)
	{
		offset = CalcCameraOffset((float)i * (1.0f / (float)kNumPoints));
		Vec2 p = porigin + kDebugDrawScale * Vec2(offset.X(), - offset.Y() + minY);

		g_prim.Draw(DebugLine2D(lastPoint, p, kDebug2DLegacyCoords, kColorGreen), debugPrimTime);
		lastPoint = p;
	}

	Color pointColor(0.3f, 0.3f, 0.3f, 1.0f);
	Color pointSelectColor(0.8f, 0.8f, 0.8f);
	Color lineColor(0.0f, 0.0f, 0.0f, 1.0f);
	DrawLine(m_cameraPoints.m_p[0] + displayOffset, m_cameraPoints.m_p[1] + displayOffset, lineColor, debugPrimTime);
	DrawLine(m_cameraPoints.m_p[2] + displayOffset, m_cameraPoints.m_p[4] + displayOffset, lineColor, debugPrimTime);
	DrawLine(m_cameraPoints.m_p[5] + displayOffset, m_cameraPoints.m_p[6] + displayOffset, lineColor, debugPrimTime);
	DrawPoint(m_cameraPoints.m_p[k0] + displayOffset, MouseOverPoint(m_cameraPoints.m_p[k0] + displayOffset) ? pointSelectColor : pointColor, debugPrimTime);
	DrawPoint(m_cameraPoints.m_p[k1] + displayOffset, MouseOverPoint(m_cameraPoints.m_p[k1] + displayOffset) ? pointSelectColor : pointColor, debugPrimTime);
	DrawPoint(m_cameraPoints.m_p[k2] + displayOffset, MouseOverPoint(m_cameraPoints.m_p[k2] + displayOffset) ? pointSelectColor : pointColor, debugPrimTime);
	DrawPoint(m_cameraPoints.m_p[k3] + displayOffset, MouseOverPoint(m_cameraPoints.m_p[k3] + displayOffset) ? pointSelectColor : pointColor, debugPrimTime);
	DrawPoint(m_cameraPoints.m_p[k4] + displayOffset, MouseOverPoint(m_cameraPoints.m_p[k4] + displayOffset) ? pointSelectColor : pointColor, debugPrimTime);
	DrawPoint(m_cameraPoints.m_p[k5] + displayOffset, MouseOverPoint(m_cameraPoints.m_p[k5] + displayOffset) ? pointSelectColor : pointColor, debugPrimTime);
	DrawPoint(m_cameraPoints.m_p[k6] + displayOffset, MouseOverPoint(m_cameraPoints.m_p[k6] + displayOffset) ? pointSelectColor : pointColor, debugPrimTime);

	offset = CalcCameraOffset(currentArc);
	DrawPoint(offset + displayOffset, Color(0.0f, 0.0f, 0.5f, 1.0f), debugPrimTime);

	offset = CalcTargetOffset(0.0f);
	lastPoint = porigin + kDebugDrawScale * Vec2(offset.X(), - offset.Y() + minY);
	for (int i=1; i<kNumPoints + 1; i++)
	{
		offset = CalcTargetOffset((float)i * (1.0f / (float)kNumPoints));
		Vec2 p = porigin + kDebugDrawScale * Vec2(offset.X(), - offset.Y() + minY);

		g_prim.Draw(DebugLine2D(lastPoint, p, kDebug2DLegacyCoords, kColorGreen), debugPrimTime);
		lastPoint = p;
	}

	DrawLine(m_targetPoints.m_p[0] + displayOffset, m_targetPoints.m_p[1] + displayOffset, lineColor, debugPrimTime);
	DrawLine(m_targetPoints.m_p[2] + displayOffset, m_targetPoints.m_p[4] + displayOffset, lineColor, debugPrimTime);
	DrawLine(m_targetPoints.m_p[5] + displayOffset, m_targetPoints.m_p[6] + displayOffset, lineColor, debugPrimTime);

	DrawPoint(m_targetPoints.m_p[k0] + displayOffset, MouseOverPoint(m_targetPoints.m_p[k0] + displayOffset) ? pointSelectColor : pointColor, debugPrimTime);
	DrawPoint(m_targetPoints.m_p[k1] + displayOffset, MouseOverPoint(m_targetPoints.m_p[k1] + displayOffset) ? pointSelectColor : pointColor, debugPrimTime);
	DrawPoint(m_targetPoints.m_p[k2] + displayOffset, MouseOverPoint(m_targetPoints.m_p[k2] + displayOffset) ? pointSelectColor : pointColor, debugPrimTime);
	DrawPoint(m_targetPoints.m_p[k3] + displayOffset, MouseOverPoint(m_targetPoints.m_p[k3] + displayOffset) ? pointSelectColor : pointColor, debugPrimTime);
	DrawPoint(m_targetPoints.m_p[k4] + displayOffset, MouseOverPoint(m_targetPoints.m_p[k4] + displayOffset) ? pointSelectColor : pointColor, debugPrimTime);
	DrawPoint(m_targetPoints.m_p[k5] + displayOffset, MouseOverPoint(m_targetPoints.m_p[k5] + displayOffset) ? pointSelectColor : pointColor, debugPrimTime);
	DrawPoint(m_targetPoints.m_p[k6] + displayOffset, MouseOverPoint(m_targetPoints.m_p[k6] + displayOffset) ? pointSelectColor : pointColor, debugPrimTime);

	offset = CalcTargetOffset(currentArc);
	DrawPoint(offset + displayOffset, kColorYellow, debugPrimTime);
}

void CameraArc::DisplayCurveWs(float currentArc, Point_arg arcCenter, Vector_arg centerToCameraDirXZ, Vector up, DebugPrimTime tt)
{
	STRIP_IN_FINAL_BUILD;

	g_prim.Draw(DebugSphere(arcCenter, 0.02f, Color(0.5f, 0.5f, 0.f,1.f)), tt);
	g_prim.Draw(DebugString(arcCenter, "arc-center", Color(0.5f, 0.5f, 0.f, 1.f), 0.4f), tt);

	Vec2 offset = CalcCameraOffset(0.0f);
	Vector offsetWs = OffsetToWorldSpace(offset, arcCenter, centerToCameraDirXZ, up);
	Point lastPoint = arcCenter + offsetWs;
	const int kNumPoints = 20;
	for (int i=1; i<kNumPoints + 1; i++)
	{
		offset = CalcCameraOffset((float)i * (1.0f / (float)kNumPoints));
		offsetWs = OffsetToWorldSpace(offset, arcCenter, centerToCameraDirXZ, up);
		Point p = arcCenter + offsetWs;

		g_prim.Draw(DebugLine(lastPoint, p, kColorGreen, 1.f, PrimAttrib(kPrimDisableDepthTest)), tt);
		lastPoint = p;
	}

	offset = CalcCameraOffset(currentArc);
	offsetWs = OffsetToWorldSpace(offset, arcCenter, centerToCameraDirXZ, up);
	g_prim.Draw(DebugSphere(arcCenter + offsetWs, 0.03f, Color(0.0f, 0.0f, 0.5f, 1.0f)), tt);
	g_prim.Draw(DebugString(arcCenter + offsetWs, StringBuilder<64>("arc:%.3f", currentArc).c_str(), kColorWhite, 0.4f), tt);

	offset = CalcTargetOffset(0.0f);
	offsetWs = OffsetToWorldSpace(offset, arcCenter, centerToCameraDirXZ, up);
	lastPoint = arcCenter + offsetWs;
	for (int i=1; i<kNumPoints + 1; i++)
	{
		offset = CalcTargetOffset((float)i * (1.0f / (float)kNumPoints));
		offsetWs = OffsetToWorldSpace(offset, arcCenter, centerToCameraDirXZ, up);
		Point p = arcCenter + offsetWs;

		g_prim.Draw(DebugLine(lastPoint, p, kColorGreenTrans, 1.f, PrimAttrib(kPrimDisableDepthTest)), tt);
		lastPoint = p;
	}

	offset = CalcTargetOffset(currentArc);
	offsetWs = OffsetToWorldSpace(offset, arcCenter, centerToCameraDirXZ, up);
	g_prim.Draw(DebugSphere(arcCenter + offsetWs, 0.03f, kColorYellow), tt);
	g_prim.Draw(DebugString(arcCenter + offsetWs, "tgt", kColorYellow, 0.4f), tt);
}

void CameraArc::DumpSettingsToTtyTags(const DC::CameraArcSettings& settings)
{
	STRIP_IN_FINAL_BUILD;

	MsgCamera( "	close-x						= %.2f\n", settings.m_closeX);
	MsgCamera( "	close-y						= %.2f\n", settings.m_closeY);
	MsgCamera( "	close-angle					= %.2f\n", settings.m_closeAngle);
	MsgCamera( "	close-scale					= %.2f\n", settings.m_closeScale);
	MsgCamera( "	close-target-offset			= %.2f\n", settings.m_closeTargetOffset);
	MsgCamera( "	close-target-offset-z			= %.2f\n", settings.m_closeTargetOffsetZ);
	MsgCamera( "	close-target-angle				= %.2f\n", settings.m_closeTargetAngle);
	MsgCamera( "	close-target-scale				= %.2f\n", settings.m_closeTargetScale);
	MsgCamera( "\n");
	MsgCamera( "	mid-x							= %.2f\n", settings.m_midX);
	MsgCamera( "	mid-y							= %.2f\n", settings.m_midY);
	MsgCamera( "	mid-angle						= %.2f\n", settings.m_midAngle);
	MsgCamera( "	mid-scale-left					= %.2f\n", settings.m_midScaleLeft);
	MsgCamera( "	mid-scale-right				= %.2f\n", settings.m_midScaleRight);
	MsgCamera( "	mid-target-offset				= %.2f\n", settings.m_midTargetOffset);
	MsgCamera( "	mid-target-offset-z			= %.2f\n", settings.m_midTargetOffsetZ);
	MsgCamera( "	mid-target-angle				= %.2f\n", settings.m_midTargetAngle);
	MsgCamera( "	mid-target-scale-left			= %.2f\n", settings.m_midTargetScaleLeft);
	MsgCamera( "	mid-target-scale-right			= %.2f\n", settings.m_midTargetScaleRight);
	MsgCamera( "\n");
	MsgCamera( "	far-x							= %.2f\n", settings.m_farX);
	MsgCamera( "	far-y							= %.2f\n", settings.m_farY);
	MsgCamera( "	far-angle						= %.2f\n", settings.m_farAngle);
	MsgCamera( "	far-scale						= %.2f\n", settings.m_farScale);
	MsgCamera( "	far-target-offset				= %.2f\n", settings.m_farTargetOffset);
	MsgCamera( "	far-target-offset-z			= %.2f\n", settings.m_farTargetOffsetZ);
	MsgCamera( "	far-target-angle				= %.2f\n", settings.m_farTargetAngle);
	MsgCamera( "	far-target-scale				= %.2f\n", settings.m_farTargetScale);
}

void CameraArc::DumpSettingsToTty(const DC::CameraArcSettings& settings)
{
	STRIP_IN_FINAL_BUILD;

	MsgCamera( "	:close-x						%.2f\n", settings.m_closeX);
	MsgCamera( "	:close-y						%.2f\n", settings.m_closeY);
	MsgCamera( "	:close-angle					%.2f\n", settings.m_closeAngle);
	MsgCamera( "	:close-scale					%.2f\n", settings.m_closeScale);
	MsgCamera( "	:close-target-offset			%.2f\n", settings.m_closeTargetOffset);
	MsgCamera( "	:close-target-offset-z			%.2f\n", settings.m_closeTargetOffsetZ);
	MsgCamera( "	:close-target-angle				%.2f\n", settings.m_closeTargetAngle);
	MsgCamera( "	:close-target-scale				%.2f\n", settings.m_closeTargetScale);
	MsgCamera( "\n");
	MsgCamera( "	:mid-x							%.2f\n", settings.m_midX);
	MsgCamera( "	:mid-y							%.2f\n", settings.m_midY);
	MsgCamera( "	:mid-angle						%.2f\n", settings.m_midAngle);
	MsgCamera( "	:mid-scale-left					%.2f\n", settings.m_midScaleLeft);
	MsgCamera( "	:mid-scale-right				%.2f\n", settings.m_midScaleRight);
	MsgCamera( "	:mid-target-offset				%.2f\n", settings.m_midTargetOffset);
	MsgCamera( "	:mid-target-offset-z			%.2f\n", settings.m_midTargetOffsetZ);
	MsgCamera( "	:mid-target-angle				%.2f\n", settings.m_midTargetAngle);
	MsgCamera( "	:mid-target-scale-left			%.2f\n", settings.m_midTargetScaleLeft);
	MsgCamera( "	:mid-target-scale-right			%.2f\n", settings.m_midTargetScaleRight);
	MsgCamera( "\n");
	MsgCamera( "	:far-x							%.2f\n", settings.m_farX);
	MsgCamera( "	:far-y							%.2f\n", settings.m_farY);
	MsgCamera( "	:far-angle						%.2f\n", settings.m_farAngle);
	MsgCamera( "	:far-scale						%.2f\n", settings.m_farScale);
	MsgCamera( "	:far-target-offset				%.2f\n", settings.m_farTargetOffset);
	MsgCamera( "	:far-target-offset-z			%.2f\n", settings.m_farTargetOffsetZ);
	MsgCamera( "	:far-target-angle				%.2f\n", settings.m_farTargetAngle);
	MsgCamera( "	:far-target-scale				%.2f\n", settings.m_farTargetScale);
}

///--------------------------------------------------------------------
/// ---------------------- helper funcs -------------------------------
///--------------------------------------------------------------------
float PitchFromDir(Vector_arg v)
{
	return Atan2(v.Y(), LengthXz(v));
}

float GetWsPitchAngleFroArc(const CameraArc &arc, const float arcPos)
{
	Vec2 arcTargetLs = arc.CalcTargetOffset(arcPos);
	Vec2 camPosLs = arc.CalcCameraOffset(arcPos);
	Vec2 v = arcTargetLs - camPosLs;
	return Atan2(v.y, Abs(v.x));
}

float GetDeltaAnglePerT(const CameraArc &arc, const float arcPos)
{
	// approximate delta pitch wrt delta arc, use that to modify m_userArc for recoil
	Vec2 arcTargetLs = arc.CalcTargetOffset(arcPos);
	Vec2 camPosLs = arc.CalcCameraOffset(arcPos);
	Vec2 arcTargetDeriv = arc.CalcTargetOffsetDerivative(arcPos);
	Vec2 camPosDeriv = arc.CalcCameraOffsetDerivative(arcPos);

	Scalar h = Scalar(0.01f);
	Vec2 camPos2 = camPosLs + camPosDeriv * h;
	Vec2 targPos2 = arcTargetLs + arcTargetDeriv * h;

	Vec2 d1 = camPosLs - arcTargetLs;
	Vec2 d2 = camPos2 - targPos2;

	const float dot = Dot(Normalize(d1), Normalize(d2));
	const float dAngle = SafeAcos(dot) / h;
#ifndef SSHEKAR
	//ASSERT(dAngle != 0.0f);
	ASSERT(IsFinite(dAngle));
#endif
	return dAngle;
}

float GetDeltaArcForDeltaAngle(const CameraArc &arc, const float arcPos, float deltaAngle)
{
	const float pitchPerT = GetDeltaAnglePerT(arc, arcPos);
	const float deltaArc = (Abs(pitchPerT) > 0.0f) ? (deltaAngle / pitchPerT) : 0.0f;

	return deltaArc;
}

struct ArcPitchAngleErrorContext
{
	const CameraArc* pArc;
	float desiredPitch;
};
float ArcPitchAngleError(float arc, void* pContext)
{
	ArcPitchAngleErrorContext* pErrContxt = reinterpret_cast<ArcPitchAngleErrorContext*>(pContext);
	float angle = GetWsPitchAngleFroArc(*pErrContxt->pArc, arc);
	return angle - pErrContxt->desiredPitch;
}

float SolveArcForPitch(const CameraArc& arc, const float arc0, const float arc1, const float desiredPitch)
{
	ArcPitchAngleErrorContext context;
	context.pArc = &arc;
	context.desiredPitch = desiredPitch;
	return Bisection::Solve(ArcPitchAngleError, arc0, arc1, &context, 20, 0.001f);
}

float ClampArcToPitchAngles(const CameraArc &arc, const float arcPos)
{
	PROFILE_AUTO(Camera);
	if (g_cameraOptions.m_enableStrafeScriptConstraints)
	{
		Locator camSpace(Point(0, 0, 0), g_cameraOptions.m_referenceRotation);
		const float refPitch = PitchFromDir(GetLocalZ(g_cameraOptions.m_referenceRotation));
		const float pitchLimits[] = { refPitch + DegreesToRadians(g_cameraOptions.m_maxVerticalAngleUp),
			refPitch - DegreesToRadians(g_cameraOptions.m_maxVerticalAngleDown) };
		const float minPitch = Min(pitchLimits[0], pitchLimits[1]);
		const float maxPitch = Max(pitchLimits[0], pitchLimits[1]);
		const float currentPitch = GetWsPitchAngleFroArc(arc, arcPos);
		float resultPitch = currentPitch;
		float resultArc = arcPos;

		F32 minPitchArc = GetWsPitchAngleFroArc(arc, 0.0f);
		F32 maxPitchArc = GetWsPitchAngleFroArc(arc, 1.0f);

		if (minPitch > minPitchArc)
		{
			return 0.0f;
		}

		if (maxPitch < maxPitchArc)
		{
			return 1.0f;
		}

		if (currentPitch > maxPitch)
		{
			resultPitch = maxPitch;
			resultArc = SolveArcForPitch(arc, 0.0f, 1.0f, maxPitch);
		}
		else if (currentPitch < minPitch)
		{
			resultPitch = minPitch;
			resultArc = SolveArcForPitch(arc, 0.0f, 1.0f, minPitch);
		}
#if 0
		MsgCon("Ref pitch: %f\n", RADIANS_TO_DEGREES(refPitch));
		MsgCon("Min pitch: %f\n", RADIANS_TO_DEGREES(minPitch));
		MsgCon("Max pitch: %f\n", RADIANS_TO_DEGREES(maxPitch));
		MsgCon("Current camera pitch: %f\n", RADIANS_TO_DEGREES(currentPitch));
		MsgCon("Desired Result camera pitch: %f\n", RADIANS_TO_DEGREES(resultPitch));
		MsgCon("Actual Result camera pitch: %f\n", RADIANS_TO_DEGREES(GetWsPitchAngleFroArc(arc, resultArc)));
#endif
		return resultArc;

	}
	return arcPos;
}

float ClampArcToPitchAngles(const CameraArc& arc, const float arcPos, float minPitch, float maxPitch)
{
	PROFILE_AUTO(Camera);
	const float currentPitch = GetWsPitchAngleFroArc(arc, arcPos);
	float resultPitch = currentPitch;
	float resultArc = arcPos;
	if (currentPitch > maxPitch)
	{
		resultPitch = maxPitch;
		resultArc = SolveArcForPitch(arc, 0.0f, 1.0f, maxPitch);
	}
	else if (currentPitch < minPitch)
	{
		resultPitch = minPitch;
		resultArc = SolveArcForPitch(arc, 0.0f, 1.0f, minPitch);
	}
#if 0
	MsgCon("Ref pitch: %f\n", RADIANS_TO_DEGREES(refPitch));
	MsgCon("Min pitch: %f\n", RADIANS_TO_DEGREES(minPitch));
	MsgCon("Max pitch: %f\n", RADIANS_TO_DEGREES(maxPitch));
	MsgCon("Current camera pitch: %f\n", RADIANS_TO_DEGREES(currentPitch));
	MsgCon("Desired Result camera pitch: %f\n", RADIANS_TO_DEGREES(resultPitch));
	MsgCon("Actual Result camera pitch: %f\n", RADIANS_TO_DEGREES(GetWsPitchAngleFroArc(arc, resultArc)));
#endif
	return resultArc;
}