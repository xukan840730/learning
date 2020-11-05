/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/leg-ik/move-leg-ik-new.h"

#include "corelib/math/intersection.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-state-snapshot.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/effect-anim-entry-tag.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/anim/footik.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/snapshot-node-heap.h"
#include "ndlib/memory/relocatable-heap.h"
#include "ndlib/nd-options.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/util/graph-display.h"
#include "ndlib/util/quick-plot.h"
#include "ndlib/util/tracker.h"

#include "gamelib/gameplay/character-leg-ik.h"
#include "gamelib/gameplay/character-leg-raycaster.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/leg-ik/anim-ground-plane.h"
#include "gamelib/gameplay/leg-ik/character-leg-ik-controller.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"
#include "gamelib/ndphys/collision-cast-interface.h"
#include "gamelib/ndphys/collision-cast.h"
#include "gamelib/ndphys/collision-filter.h"
#include "gamelib/ndphys/simple-collision-cast.h"
#include "gamelib/scriptx/h/ik-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
const StringId64 kAnkleLocIds[kQuadLegCount] = { SID("lAnkle"), SID("rAnkle"), SID("flAnkle"), SID("frAnkle") };

const StringId64 kHumanFootEffectJointIds[kQuadLegCount] = { SID("l_heel"), SID("r_heel"), INVALID_STRING_ID_64, INVALID_STRING_ID_64 };
const StringId64 kQuadrupedFootEffectJointIds[kQuadLegCount] = { SID("l_toe"), SID("r_toe"), SID("l_hand"), SID("r_hand") };

const StringId64 kHumanFallbackJointIds[kQuadLegCount] = { SID("l_ball"), SID("r_ball"), INVALID_STRING_ID_64, INVALID_STRING_ID_64 };
const StringId64 kQuadrupedFallbackJointIds[kQuadLegCount] = { INVALID_STRING_ID_64, INVALID_STRING_ID_64, INVALID_STRING_ID_64, INVALID_STRING_ID_64 };

//static
int MoveLegIkNew::GetFootJointIds(FootIkCharacterType charType, const StringId64** pOutArray)
{
	ANIM_ASSERT(pOutArray);
	switch (charType)
	{
	case kFootIkCharacterTypeHuman:
		*pOutArray = &kHumanFootEffectJointIds[0];
		break;
	case kFootIkCharacterTypeHorse:
		*pOutArray = &kQuadrupedFootEffectJointIds[0];
		break;
	case kFootIkCharacterTypeDog:
		*pOutArray = &kQuadrupedFootEffectJointIds[0];
		break;
	}

	return GetLegCountForCharacterType(charType);
}

//static
int MoveLegIkNew::GetFallbackFootJointIds(FootIkCharacterType charType, const StringId64** pOutArray)
{
	ANIM_ASSERT(pOutArray);
	switch (charType)
	{
	case kFootIkCharacterTypeHuman:
		*pOutArray = &kHumanFallbackJointIds[0];
		break;
	case kFootIkCharacterTypeHorse:
		*pOutArray = &kQuadrupedFallbackJointIds[0];
		break;
	case kFootIkCharacterTypeDog:
		*pOutArray = &kQuadrupedFallbackJointIds[0];
		break;
	}

	return GetLegCountForCharacterType(charType);
}

bool IsChannelMaskValid(U32 channelMask, int legCount)
{
	for (int iLeg = 0; iLeg < legCount; ++iLeg)
	{
		if (!((channelMask >> iLeg) % 2))
			return false;
	}
	return true;
}

class FootDef
{
public:
	void Init(const DC::LegIkFootDef *const pLegIkFootDef, 
			const Point_arg anklePos,
			const Point_arg heelPos, 
			const Point_arg footRefPos)
	{
		ANIM_ASSERT(pLegIkFootDef);

		const Vector forward = SafeNormalize(footRefPos - heelPos, kUnitZAxis);
		const Vector heelToAnkle = SafeNormalize(anklePos - heelPos, kUnitYAxis);
		const Vector normal = Cross(forward, heelToAnkle);

		m_footPts[0] = Point(heelPos - normal * pLegIkFootDef->m_halfWidth - forward * pLegIkFootDef->m_distHeelToBack);
		m_footPts[1] = Point(heelPos - normal * pLegIkFootDef->m_halfWidth + forward * pLegIkFootDef->m_distHeelToTip);
		m_footPts[2] = Point(heelPos + normal * pLegIkFootDef->m_halfWidth + forward * pLegIkFootDef->m_distHeelToTip);
		m_footPts[3] = Point(heelPos + normal * pLegIkFootDef->m_halfWidth - forward * pLegIkFootDef->m_distHeelToBack);
	}

public:
	Point m_footPts[4];
};

void LegIkPlatform::Init(Point_arg refPt,
						const EdgeInfo& edge,
						float width,
						bool rotateFeet,
						bool disableSpring,
						SpringTracker<Vector>* const pTracker,
						Vector* const pOffset)
{
	if (width < 0.0f)
	{
		Reset();
		return;
	}

	Init(refPt, edge.GetVert0(), edge.GetVert1(), width, rotateFeet, disableSpring, pTracker, pOffset);
}

void LegIkPlatform::Init(Point_arg refPt,
						Point_arg edgePt0,
						Point_arg edgePt1,
						float width,
						bool rotateFeet,
						bool disableSpring,
						SpringTracker<Vector>* const pTracker,
						Vector* const pOffset)
{
	if (width < 0.0f)
	{
		Reset();
		return;
	}

	Point v0 = edgePt0;
	Point v1 = edgePt1;

	m_width = width;
	m_rotateFeet = rotateFeet;
	m_disableSpring = disableSpring;
	m_pTracker = pTracker;
	m_pOffset = pOffset;

	// Flatten the original edge direction
	v0.SetY(refPt.Y());
	v1.SetY(refPt.Y());
	const Vector dir = v1 - v0;

	// Guess normal
	const Vector testNormal = SafeNormalize(Vector(-dir.Z(), dir.Y(), dir.X()), kUnitXAxis);
	const Plane testPlane = Plane(v0, testNormal);

	const float dist = testPlane.Dist(refPt);
	m_base = dist > 0.0f
			 ? Plane(v0, -testNormal)// Make sure the normal is pointing in the opposite direction of refPt
			 : testPlane;
}

void LegIkPlatform::DebugDraw(const Point_arg refPt) const
{
	if (!IsValid())
	{
		return;
	}

	const Vector normal = m_base.GetNormal();
	const Vector perp = Vector(-normal.Z(), normal.Y(), normal.X());
	const Point projectedRefPt = m_base.ProjectPoint(refPt);

	static const float kLen = 1.5f;
	g_prim.Draw(DebugQuad(projectedRefPt - perp * kLen,
						projectedRefPt + perp * kLen,
						projectedRefPt + perp * kLen - normal * m_width,
						projectedRefPt - perp * kLen - normal * m_width,
						kColorYellow, 
						PrimAttrib(kPrimEnableWireframe)), 
				kPrimDuration1FramePauseable);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MoveLegIkNew::Start(Character* pCharacter)
{
	ILegIk::Start(pCharacter);

	m_nextFootplant = -1;
	
	ANIM_ASSERT(m_legCount == pCharacter->GetLegCount());

	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		m_groundModel[iLeg].SetOwner(pCharacter);
		m_singleFrameDisableLeg[iLeg] = false;
		m_prevUsedGroundPtValid[iLeg] = false;
		m_slatBlend[iLeg] = 0.0f;

		m_smoothedGroundNormals[iLeg] = kUnitYAxis;
		m_groundNormalSpringWs[iLeg].Reset();
	}

	// was commented out before quadruped rewrite
	//UpdateFeetPlanted(pCharacter, true);

	m_lastRootBaseValid = false;
	
	InitAnkleSpace(pCharacter);
	m_rootXzOffset = Vector(kZero);
	m_rootXzOffsetTracker.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void SmoothLegGroundPositionsInternal(Vector_arg groundNormal,
											 bool& legInited,
											 float& legY,
											 SpringTracker<float>& legYSpring,
											 SpringTracker<float>& legYVelSpring,
											 Character* pCharacter,
											 Point& leg,
											 const float legSpeed,
											 Vector_arg footVecXZ)
{
	float desiredLegY = leg.Y();
	if (!legInited)
	{
		legInited = true;
		legY = leg.Y();
		legYSpring.Reset();
		legYVelSpring.Reset();
	}
	else
	{
		
		float targetY = leg.Y();
		const float prevY = legY;
		const float deltaTime = GetProcessDeltaTime();
		const float legSpring = 30.0f;
		const float legVSpring = 30.0f;
		const float targetSpeed = (targetY - legY) / deltaTime;
		
		//legYSpring.m_speed = legYVelSpring.Track(legYSpring.m_speed, targetSpeed, deltaTime, legVSpring);
		//legYSpring.m_speed = (legY - prevY) / deltaTime;
		legY = legYSpring.Track(legY, targetY, deltaTime, legSpring);
		const float cosTheta = groundNormal.Y();
		const float sinTheta = Sqrt(1.f - Sqr(cosTheta));
		const float tanTheta = sinTheta / cosTheta;

		Point planeIsect;
		LinePlaneIntersect(kOrigin,
						   groundNormal,
						   Point(kOrigin) + footVecXZ + Vector(kUnitYAxis) * 10.0f,
						   Point(kOrigin) + footVecXZ + Vector(kUnitYAxis) * -10.0f,
						   nullptr,
						   &planeIsect);

		Seek(legY, targetY, FromUPS(Abs((float)planeIsect.Y() / deltaTime), deltaTime));
		//MsgCon("Leg spring speed: %f\n", legYSpring.m_speed);
		leg.SetY(legY);

		if (DEBUG_LEG_IK)
		{			
			MsgCon("GroundNormal Y: %f\n", (float)groundNormal.Y());
			MsgCon("Tan theta: %f\n", tanTheta);
			MsgCon("Tan theta speed: %f\n", tanTheta*legSpeed);
			MsgCon("Isect speed: %f\n", (float)planeIsect.Y() / deltaTime);
			MsgCon("Leg ground smoothing error: %f\n", legY - targetY);
			/*g_prim.Draw(DebugArrow(leg - footVecXZ, footVecXZ, kColorGreen));
			g_prim.Draw(DebugArrow(leg - footVecXZ, footVecXZ + Vector(0.0f, planeIsect.Y(), 0.0f), kColorRed));*/
		}
	}
}

///// --------------------------------------------------------------------------------------------------------------- ///
void MoveLegIkNew::SmoothLegGroundPositions(Character* pCharacter,
											CharacterLegIkController* pController,
											Point& legGroundPt,
											int legIndex)
{
	ANIM_ASSERT(legIndex >= 0 && legIndex < pCharacter->GetLegCount());
	const float desiredY = legGroundPt.Y();
	SmoothLegGroundPositionsInternal(pController->m_groundNormal,
									 pController->m_legYInited[legIndex],
									 pController->m_legY[legIndex],
									 pController->m_legYSpring[legIndex],
									 pController->m_legYVelSpring[legIndex],
									 pCharacter,
									 legGroundPt,
									 pController->m_footSpeedsXZ[legIndex].Otherwise(0.0f),
									 pController->m_footDeltasXZ[legIndex].Otherwise(Vector(kZero)));

	extern bool g_drawIkGraphs;

	//we don't want to draw one graph for each leg because they would overlap
	if (legIndex == kLeftLeg && g_drawIkGraphs)
	{
		GraphDisplay::Params baseParams = GraphDisplay::Params().WithGraphType(GraphDisplay::kGraphTypeLine).NoAvgLine().NoAvgText().NoMaxLine().NoMaxText().NoBackground().WithDataColor(Abgr8FromRgba(0, 0, 255, 255));
		baseParams.m_min = legGroundPt.Y()-2.0f;
		baseParams.m_max = legGroundPt.Y()+2.0f;
		baseParams.m_useMinMax = true;
		baseParams.m_dataColor = Abgr8FromRgba(0,0,255, 128);
		MsgCon("desiredY: %f\n", desiredY);
		float newRootDelta = pController->m_rootBaseY;
		QUICK_PLOT(leftLegPlot,
				   "leftLeg",
				   10,
				   260, // x and y coordinates to draw the plot
				   250,
				   250,		   // width and height of the plot
				   60,		   // show values from 60 frames ago up until now
				   baseParams, // provide additional customization of the plot
				   legGroundPt.Y());
		QUICK_PLOT(rightLegPlot,
				   "leftLeg ",
				   10,
				   260, // x and y coordinates to draw the plot
				   250,
				   250, // width and height of the plot
				   60,	// show values from 60 frames ago up until now
				   baseParams
					   .WithDataColor(Abgr8FromRgba(255, 0, 255, 128)), // provide additional customization of the plot
				   desiredY);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point CastPointToGround(Point_arg pos)
{
	RayCastJob job;
	static int numRays = 1;
	job.Open(numRays, 1, ICollCastJob::kCollCastSynchronous, ICollCastJob::kClientPlayerMisc);

	SimpleCastProbe* probes = STACK_ALLOC(SimpleCastProbe, numRays);
	for (int i = 0; i < numRays; i++)
	{		
		probes[i].m_pos = pos;
		probes[i].m_pos.SetY(pos.Y() + 2.5f);
		probes[i].m_vec = Vector(0.0f, -5.0f, 0.0f);
		probes[i].m_radius = 0.0f;
	}	

	SimpleCastKick(job, probes, numRays, CollideFilter(Collide::kLayerMaskGeneral), ICollCastJob::kCollCastSynchronous);
	job.Wait();
	//job.DebugDraw(kPrimDuration1FramePauseable);
	SimpleCastGather(job, probes, numRays);

	Point groundPoint = pos;
	if (probes[numRays-1].m_cc.m_time >= 0.0f)
	{
		groundPoint.SetY(probes[numRays-1].m_cc.m_contact.Y());
	}
	
	return groundPoint;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ValidatePosCouldBeOnGround(Point_arg pos, const GroundModel& alignModel, const LegRaycaster& legRaycaster)
{
	if (alignModel.IsValid())
	{
		if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugMovingLegIkTest))
		{
			g_prim.Draw(DebugCoordAxesLabeled(Locator(pos), "Leg Point"), kPrimDuration1FramePauseable);
		}
		Maybe<Point> alignResult = alignModel.ProjectPointToGround(pos);
		if (!alignResult.Valid())
		{
			g_prim.Draw(DebugCoordAxesLabeled(Locator(pos), "Leg Point"), kPrimDuration1FramePauseable);
			alignModel.DebugDraw(kColorRed, kPrimDuration1FramePauseable);
			return false;
		 }
		 else
		 {
			 if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugMovingLegIkTest))
			 {
				g_prim.Draw(DebugCoordAxesLabeled(Locator(alignResult.Get()), "Align Point"), kPrimDuration1FramePauseable);
			 }
		 }
	 }
	 if (!legRaycaster.IsCollisionPointValid(pos))
	 {
		 return false;
	 }
	 return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator MoveLegIkNew::ApplyLegToGround(Character *const pCharacter,
									CharacterLegIkController *const pController,
									const Point_arg animGroundPosWs, 
									const Vector_arg animGroundNormalWs, 
									const DC::LegIkFootDef *const pLegIkFootDef, 
									const LegIkPlatform& legIkPlatform, 
									int legIndex,
									const Locator& startingAnkleLocWs,
									const Locator& startingHeelLocWs,
									bool& outOnGround,
									Vector& outRootXzOffset)
{
	PROFILE(Processes, MoveLegIk_ApplyLeg2Grnd);

	const Locator initialLegLocWs = startingAnkleLocWs.TransformLocator(m_footRefPointAnkleSpace[legIndex]);
	if (DEBUG_LEG_IK)
	{
		g_prim.Draw(DebugLine(startingAnkleLocWs.GetPosition(), initialLegLocWs.GetPosition(), kColorBlue, 1.0f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
	}

	const Point legPoint = initialLegLocWs.GetTranslation();
	Point groundPtEstimateAnim;
	LinePlaneIntersect(animGroundPosWs,
					animGroundNormalWs,
					legPoint,
					legPoint - Vector(kUnitYAxis),
					nullptr,
					&groundPtEstimateAnim);
	ALWAYS_ASSERT(IsFinite(groundPtEstimateAnim));

	const LegRaycaster::Results& colResult = pController->m_legRaycaster.GetCollisionRaycastResults(legIndex);
	const LegRaycaster::Results& result = pController->m_legRaycaster.GetProbeResults(legIndex, 0.5f);

	bool standingOnChar = false;
	if (result.m_hitProcessId != 0)
	{
		const Process *const pHitProc = EngineComponents::GetProcessMgr()->LookupProcessByPid(result.m_hitProcessId);
		if (pHitProc && pHitProc->IsKindOf(g_type_Character))
		{
			standingOnChar = true;
		}
	}

	const Locator resultLocWs = result.m_hitGround ? 
								result.m_point.GetLocatorWs() : 
								initialLegLocWs;
	const Point hitposWs = resultLocWs.Pos();

#ifdef SSHEKAR
	if (result.m_hasSlatPoint)
		g_prim.Draw(DebugCross(result.m_slatPoint, 0.2f, 1.0f));
#endif
	outOnGround = result.m_hitGround;

	const Vector resultNormalWs = GetLocalZ(resultLocWs.Rot());
	const Vector colNormalWs = colResult.m_hitGround ? 
							GetLocalZ(colResult.m_point.GetRotationWs()) : 
							kUnitYAxis;

	// JDB: This code is trying to address the player walking over high frequency, detailed terrain (e.g. rocky surfaces)
	// because we use the normal to plane project and extrapolate future ground positions, really noisy normals
	// can make the player shoot up or down with extreme height deltas. For now this should fall back to (hopefully smoother)
	// collision results and pass *that* through a spring to further smooth out high frequency noise. 
	// I think when someone, possibly you, has the time and inclination a more holistic addressing of this logic is
	// in order, probably something more nuanced than plane projection extrapolation. E.g. Ryan B. says he often
	// preserves whatever delta between mesh & collision exists and applies that to future inputs. 
	const float distToRaycastContact = Dist(startingAnkleLocWs.Pos(), hitposWs);
	const float normTT = standingOnChar ? 
						1.0f : 
						LerpScaleClamp(0.1f, 0.3f, 0.0f, 1.0f, distToRaycastContact);
	const float dt = pCharacter->GetClock()->GetDeltaTimeInSeconds();
	const Vector desNormalWs = Slerp(resultNormalWs, colNormalWs, normTT);

	m_smoothedGroundNormals[legIndex] = m_groundNormalSpringWs[legIndex].Track(m_smoothedGroundNormals[legIndex],
																			desNormalWs,
																			dt,
																			7.0f);

	Vector rayCasterNormal = m_smoothedGroundNormals[legIndex];
	if (Dot(rayCasterNormal, kUnitYAxis) <= 0.707f)
	{
		rayCasterNormal = kUnitYAxis;
	}
	
	if (m_singleFrameDisableLeg[legIndex])
	{
		outOnGround = false;
		m_singleFrameDisableLeg[legIndex] = false;
	}

	Point fallBackPoint = groundPtEstimateAnim;
	if (outOnGround)
	{
		LinePlaneIntersect(hitposWs,
						   rayCasterNormal,
						   legPoint,
						   legPoint - Vector(kUnitYAxis),
						   nullptr,
						   &fallBackPoint);
	}
	ALWAYS_ASSERT(IsFinite(fallBackPoint));

	const Maybe<Point> groundModelGroundPt = m_groundModel[legIndex].ProjectPointToGround(startingAnkleLocWs.GetTranslation());
	const bool useGroundModelPt = (outOnGround && groundModelGroundPt.Valid());
	const Point testGroundPt = useGroundModelPt ?
							groundModelGroundPt.Get() :
							fallBackPoint;
	ALWAYS_ASSERT(IsFinite(testGroundPt));

	if (DEBUG_LEG_IK)
	{
		MsgCon("%d : distToRaycastContact = %0.3fm\n", legIndex, distToRaycastContact);
		MsgCon("%d : normTT = %f\n", legIndex, normTT);

		/*g_prim.Draw(DebugCross(hitposWs, 0.1f, kColorYellow));
		g_prim.Draw(DebugString(hitposWs + Vector(0.0f, 0.15f, 0.0f), "raycast contact", kColorYellowTrans, 0.5f));*/

		//g_prim.Draw(DebugArrow(colResult.m_point.GetTranslationWs(), GetLocalZ(colResult.m_point.GetLocatorWs())));
		const Color drawColor = standingOnChar ? 
								kColorOrangeTrans : 
								Slerp(legIndex == 0 ? kColorCyanTrans : kColorMagentaTrans, kColorGrayTrans, normTT);

		g_prim.Draw(DebugArrow(hitposWs, rayCasterNormal, drawColor, 0.5f, kPrimEnableHiddenLineAlpha));
		g_prim.Draw(DebugPlaneCross(hitposWs,
									rayCasterNormal,
									drawColor,
									1.0f,
									2.0f,
									kPrimEnableHiddenLineAlpha));

		/*g_prim.Draw(DebugCross(testGroundPt, 0.1f, kColorPinkTrans, kPrimEnableHiddenLineAlpha));
		g_prim.Draw(DebugString(testGroundPt, 
								StringBuilder<32>("testGroundPt(%s)", useGroundModelPt ? "ground model" : "fallback").c_str(), 
								kColorPinkTrans,
								0.5f));*/
	}
	
	/*if (!ValidatePosCouldBeOnGround(testGroundPt, m_alignGround, pController->m_legRaycaster))
	{
		ASSERT(false);
		testGroundPt = groundPtEstimateAnim;
	}*/

	if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugMovingLegIkTest))
	{
		const float offsetY = testGroundPt.Y() - groundPtEstimateAnim.Y();
		MsgCon("Foot offset raw: %f\n", offsetY);
	}
	
	m_prevUsedGroundPt[legIndex] = BoundFrame(testGroundPt, pCharacter->GetBinding());
	m_prevUsedGroundPtValid[legIndex] = true;

	const float blend = GetBlend(pController);
	const Quat rotAdj = AdjustFootNormal(pCharacter, pController, rayCasterNormal, legIndex, blend, animGroundNormalWs, startingAnkleLocWs.GetRotation());

	Point legGroundPt = legPoint;
	legGroundPt.SetY(testGroundPt.Y());
	ALWAYS_ASSERT(IsFinite(legGroundPt));

	const Point legGroundPtPreSmooth = legGroundPt;
	SmoothLegGroundPositions(pCharacter, pController, legGroundPt, legIndex);

	if (DEBUG_LEG_IK)
	{
		/*g_prim.Draw(DebugCross(legGroundPtPreSmooth, 0.1f, kColorOrangeTrans));
		g_prim.Draw(DebugString(legGroundPtPreSmooth, "legGroundPtPreSmooth", kColorOrangeTrans, 0.5f));*/

		g_prim.Draw(DebugCross(legGroundPt, 0.1f, kColorRedTrans, kPrimEnableHiddenLineAlpha));
		g_prim.Draw(DebugString(legGroundPt,
								StringBuilder<32>("legGroundPt(%s)", useGroundModelPt ? "ground model" : "fallback").c_str(),
								kColorPinkTrans,
								0.5f));

		MsgCon("Move leg ik blend: %f\n", blend);
	}

	const float smoothOffsetY = legGroundPt.Y() - groundPtEstimateAnim.Y();
	if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugMovingLegIkTest))
	{
		MsgCon("Foot offset: %f\n", smoothOffsetY);
	}

	const float bias = -0.00f;
	Point idealLegPoint = legPoint + Vector(0.0f, smoothOffsetY + bias, 0.0f);

	if (g_ndOptions.m_slatLegIkTest)
	{
		F32 desSlatBlend = 0.0f;
		F32 blendSpeed = 5.0f;

		if (outOnGround && 
			result.m_hasSlatPoint)
		{
			Point slatPoint = result.m_slatPoint;
			F32 footSpeed = pController->m_footSpeedsXZ[legIndex].Otherwise(0.0f);
			F32 footHeight = initialLegLocWs.Pos().Y() - legGroundPtPreSmooth.Y();

			F32 footSpeedBlend = LerpScale(2.0f, 4.0f, 1.0f, 0.0f, footSpeed);
			F32 footHeightBlend = LerpScale(0.02f, 0.08f, 1.0f, 0.0f, footHeight);
			F32 slatBlend = Min(footSpeedBlend, footHeightBlend);

			desSlatBlend = slatBlend;

			F32 distFromPlant = DistXz(m_curSlatPos[legIndex], idealLegPoint);
			if (distFromPlant > 0.25f)
			{
				desSlatBlend = 0.0f;
				blendSpeed = LerpScale(0.25f, 0.5f, 5.0f, 25.0f, distFromPlant);
			}

			Seek(m_slatBlend[legIndex], desSlatBlend, FromUPS(blendSpeed));

#ifdef SSHEKAR
			MsgCon("Foot Height: %.2f\n", footHeight);
			MsgCon("FooBlend: %.2f %.2f\n", footSpeedBlend, footHeightBlend);
			MsgCon("Dist: %.2f\n", distFromPlant);
			MsgCon("Blends: %.2f/%.2f\n", m_slatBlend[legIndex], desSlatBlend);
#endif

			if (m_slatBlend[legIndex] == 0.0f && distFromPlant > 0.2f)
			{
				m_curSlatPos[legIndex] = slatPoint;
			}
		}
		else
		{
			Seek(m_slatBlend[legIndex], desSlatBlend, FromUPS(blendSpeed));
		}

#ifdef SSHEKAR
		if (result.m_hasSlatPoint)
		{
			g_prim.Draw(DebugCross(m_curSlatPos[legIndex], 0.2f, 1.0f, kColorRed));
		}

		g_prim.Draw(DebugCross(idealLegPoint, 0.2f, 1.0f, kColorGreen));
#endif

		if (m_slatBlend[legIndex] > 0.0f)
		{
			Point prevPoint = idealLegPoint;
			idealLegPoint = Lerp(idealLegPoint, PointFromXzAndY(m_curSlatPos[legIndex], idealLegPoint), m_slatBlend[legIndex]);
			outRootXzOffset = (idealLegPoint - prevPoint) * 0.5f * m_slatBlend[legIndex];
		}
	}

	Vector legIkPlatformOffset ;

	if (g_enableLegIkPlatform && 
		pLegIkFootDef && 
		legIkPlatform.IsValid())
	{	// Constrain the foot to be within the platform
		const Locator& charLoc = pCharacter->GetLocator();

		Point adjustedLegPoint = idealLegPoint;
		
		if (legIkPlatform.m_rotateFeet)
		{	// Extra rotation adjustment on the foot to reduce extreme leg penetration after ik
			PrimServerWrapper ps(charLoc);
			ps.SetDuration(kPrimDuration1FramePauseable);
			ps.EnableHiddenLineAlpha();

			const Point legPosLs = charLoc.UntransformPoint(adjustedLegPoint);
			const Point flatLegPosLs = Point(legPosLs.X(), 0.0f, legPosLs.Z());
			const Vector flatLegVecLs = SafeNormalize(flatLegPosLs - kOrigin, kUnitZAxis);

			const bool isFront = legPosLs.Z() > 0.0f;
			const Vector refVecLs = isFront ? Vector(kUnitZAxis) : -Vector(kUnitZAxis);
			
			const float d = Clamp((F32)Dot(flatLegVecLs, refVecLs), -1.0f, 1.0f);
			const float angleRads = Acos(d);

			const float kMinAngleRads = isFront ? DegreesToRadians(41.0f) : DegreesToRadians(11.7f);
			if (angleRads < kMinAngleRads)
			{
				const float deltaAngleRads = kMinAngleRads - angleRads;
				const bool isLeft = legPosLs.X() > 0.0f;

				Vector rotateAxis;
				if (isFront)
				{
					rotateAxis = isLeft ?
								Vector(kUnitYAxis) :	// Front left quadrant
								-Vector(kUnitYAxis);	// Front right quadrant
				}
				else
				{
					rotateAxis = isLeft ?
								-Vector(kUnitYAxis) :	// Back left quadrant
								Vector(kUnitYAxis);		// Back right quadrant
				}

				if (DEBUG_LEG_IK)
				{
					static const char *const s_quadrantStr[] = { "br", "bl", "fr", "fl" };

					ps.DrawLine(kOrigin, flatLegPosLs, kColorYellow);
					ps.DrawString(flatLegPosLs, 
								StringBuilder<64>("%s: %3.3f(%3.3f/%3.3f)", s_quadrantStr[(int)isFront * 2 + (int)isLeft], RadiansToDegrees(deltaAngleRads), RadiansToDegrees(angleRads), RadiansToDegrees(kMinAngleRads)).c_str(), 
								kColorYellowTrans, 
								0.5f);
				}

				const Quat deltaRot = QuatFromAxisAngle(rotateAxis, deltaAngleRads);
				const Point rotatedLegPosLs = RotatePoint(deltaRot, legPosLs);
					
				adjustedLegPoint = charLoc.TransformPoint(rotatedLegPosLs);
			}
		}

		const Locator adjustedLegLoc(adjustedLegPoint, Conjugate(rotAdj) * initialLegLocWs.Rot());
		const Locator adjustedAnkleLoc = adjustedLegLoc.TransformLocator(initialLegLocWs.UntransformLocator(startingAnkleLocWs));
		const Locator adjustedHeelLoc = adjustedLegLoc.TransformLocator(initialLegLocWs.UntransformLocator(startingHeelLocWs));

		FootDef footDef;
		footDef.Init(pLegIkFootDef, 
					adjustedAnkleLoc.GetPosition(),
					adjustedHeelLoc.GetPosition(),
					adjustedLegLoc.GetPosition());
		if (DEBUG_LEG_IK)
		{
			g_prim.Draw(DebugLine(adjustedAnkleLoc.GetPosition(), adjustedHeelLoc.GetTranslation(), kColorYellow, 1.0f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugLine(adjustedAnkleLoc.GetPosition(), adjustedLegLoc.GetTranslation(), kColorYellow, 1.0f, kPrimEnableHiddenLineAlpha), kPrimDuration1FramePauseable);

			g_prim.Draw(DebugQuad(footDef.m_footPts[0],
								footDef.m_footPts[1],
								footDef.m_footPts[2],
								footDef.m_footPts[3],
								kColorYellow,
								PrimAttrib(kPrimEnableWireframe)),
						kPrimDuration1FramePauseable);
		}

		float maxDist = -kLargestFloat;
		float minDist = kLargestFloat;
		for (int i = 0; i < 4; i++)
		{
			const float dist = legIkPlatform.m_base.Dist(footDef.m_footPts[i]);

			if (dist > maxDist)
			{
				maxDist = dist;
			}
			
			if (dist < minDist)
			{
				minDist = dist;
			}
		}

		const bool shouldPullBack = (maxDist > 0.0f);
		const bool shouldPushForward = (minDist < -legIkPlatform.m_width);

		if (shouldPullBack && 
			!shouldPushForward)
		{	// Pull the foot back
			adjustedLegPoint -= (legIkPlatform.m_base.GetNormal() * maxDist);
		}
		else if (!shouldPullBack && 
				shouldPushForward)
		{	// Push the foot forward
			adjustedLegPoint += (legIkPlatform.m_base.GetNormal() * (-legIkPlatform.m_width - minDist));
		}

		const float footHeight = fmax(0.0f, legPoint.Y() - animGroundPosWs.Y());
		const float heightBlend = LerpScale(0.16f, 0.02f, 0.0f, 1.0f, footHeight);

		if (DEBUG_LEG_IK)
		{
			g_prim.Draw(DebugString(adjustedLegPoint, StringBuilder<32>("hBlend: %3.3f(%3.3f)", footHeight, heightBlend).c_str(), kColorYellowTrans, 0.5f));
		}

		legIkPlatformOffset = (adjustedLegPoint - idealLegPoint) * heightBlend;
	}
	else
	{
		legIkPlatformOffset = kZero;
	}

	if (legIkPlatform.m_pTracker)
	{
		ANIM_ASSERT(legIkPlatform.m_pOffset);

		if (legIkPlatform.m_disableSpring)
		{
			legIkPlatform.m_pTracker[legIndex].Reset();
			legIkPlatform.m_pOffset[legIndex] = legIkPlatformOffset;
		}
		else
		{
			legIkPlatform.m_pOffset[legIndex] = legIkPlatform.m_pTracker[legIndex].Track(legIkPlatform.m_pOffset[legIndex],
																						legIkPlatformOffset,
																						dt,
																						5.5f);
		}

		idealLegPoint += legIkPlatform.m_pOffset[legIndex]; 
	}
	else
	{
		idealLegPoint += legIkPlatformOffset;
	}

	const Point finalLegPoint = Lerp(legPoint, idealLegPoint, blend);
	ALWAYS_ASSERT(IsFinite(finalLegPoint));
	const Locator finalLegLoc(finalLegPoint, Conjugate(rotAdj) * initialLegLocWs.Rot());
	const Locator ankleFinalLoc = finalLegLoc.TransformLocator(initialLegLocWs.UntransformLocator(startingAnkleLocWs));
	if (DEBUG_LEG_IK)
	{
		g_prim.Draw(DebugCoordAxesLabeled(finalLegLoc, "final", 0.1f, kPrimEnableHiddenLineAlpha, 2.0f, kColorWhiteTrans, 0.5f));
		g_prim.Draw(DebugCoordAxesLabeled(ankleFinalLoc, "final ankle", 0.1f, kPrimEnableHiddenLineAlpha, 2.0f, kColorWhiteTrans, 0.5f));
	}

	return ankleFinalLoc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector MoveLegIkNew::LimitNormalAngle(Vector_arg normal, Vector_arg animationNormal)
{
	float dot = Dot(normal, animationNormal);
	float theta = Acos(MinMax(dot, -1.0f, 1.0f));
	float angle = RADIANS_TO_DEGREES(theta);

	const float kMaxAngle = 45.0f;

	if (angle > kMaxAngle)
	{
		float t = (angle - kMaxAngle) / angle;
		float sinTheta = Sin(theta);

		Vector newNormal = animationNormal * (Sin((1.0f - t) * theta) / sinTheta) + normal * (Sin(t * theta) / sinTheta);
		newNormal = SafeNormalize(newNormal, normal);

		return newNormal;
	}
	else
	{
		return normal;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Quat MoveLegIkNew::AdjustFootNormal(Character* pCharacter,
									CharacterLegIkController* pController,
									Vector normal,
									int legIndex,
									float blend,
									Vector_arg animationGroundNormal,
									Quat_arg animAnkleRot)
{
	// Don't roll the foot sideways because it looks bad
	Vector side = GetLocalX(animAnkleRot);
	normal -= side*Dot(normal, side);
	normal = SafeNormalize(normal, kUnitYAxis);

	normal = LimitNormalAngle(normal, animationGroundNormal);

	Quat trans(kIdentity);

	Vector footNormal = GetLocalZ(m_legIks[legIndex]->GetAnkleLocWs().GetRotation());

	const Vector ankleAxis = GetAnkleAxis(legIndex);
	const Quat ankleRotAdjust = RotationBetween(ankleAxis.GetVec4(), Vector(kUnitYAxis).GetVec4());
	footNormal = Rotate(ankleRotAdjust, footNormal);

	if (!pController->m_footNormalInited[legIndex])
	{
		pController->m_footNormal[legIndex] = normal;
		pController->m_footGroundNormal[legIndex] = normal;
		pController->m_footNormalInited[legIndex] = true;	
	}
	else
	{
		pController->m_footGroundNormal[legIndex] = pController->m_footNormalSpring[legIndex].Track(pController->m_footGroundNormal[legIndex], normal, GetProcessDeltaTime(), g_footNormalSpring);
		pController->m_footGroundNormal[legIndex] = SafeNormalize(pController->m_footGroundNormal[legIndex], normal);
	}

	Quat startQuat = QuatFromLookAt(animationGroundNormal, kUnitZAxis);
	Quat normalQuat = QuatFromLookAt(pController->m_footGroundNormal[legIndex], kUnitZAxis);
	normalQuat = Slerp(startQuat, normalQuat, blend);

	Quat footQuat = QuatFromLookAt(footNormal, kUnitZAxis);
	trans = Conjugate(startQuat) * normalQuat;
	footQuat = footQuat * trans;

	m_footNormals[legIndex] = GetLocalZ(footQuat);

	return trans;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MoveLegIkNew::Update(Character* pCharacter, CharacterLegIkController* pController, bool doCollision)
{
	PROFILE(Processes, MoveLegIkNew_Update);
	
	if (m_pJobCounter)
	{
		ndjob::WaitForCounterAndFree(m_pJobCounter);
		m_pJobCounter = nullptr;
	}

	const Plane animGroundPlane = GetAnimatedGroundPlane(pCharacter);
	const Point animGroundPosWs = animGroundPlane.ProjectPoint(pCharacter->GetTranslation());
	const Vector animGroundNormalWs = animGroundPlane.GetNormal();

	if (DEBUG_LEG_IK)
	{	// Animation ground plane
		/*g_prim.Draw(DebugArrow(animGroundPosWs, animGroundPlane.GetNormal(), kColorBlueTrans, 0.5f, kPrimEnableHiddenLineAlpha));
		g_prim.Draw(DebugPlaneCross(animGroundPosWs,
									animGroundNormalWs,
									kColorBlueTrans,
									1.0f,
									2.0f,
									kPrimEnableHiddenLineAlpha), 
					kPrimDuration1FramePauseable);*/
	}

	if (!m_lastRootBaseValid)
	{
		SetLastRootBase(pCharacter, pController);
	}

	if (DEBUG_LEG_IK)
	{	// Debug draw joints pre IK
		for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
		{
			m_legIks[iLeg]->DebugDrawJoints(false, false, true, kColorBlue);
		}
	}

	const DC::LegIkFootDef *const pLegIkFootDef = pCharacter->GetLegIkFootDef();
	LegIkPlatform legIkPlatform = pCharacter->GetLegIkPlatform();
	if (DEBUG_LEG_IK)
	{
		legIkPlatform.DebugDraw(pCharacter->GetTranslation());
	}
	
	Locator aLegIkTargets[kQuadLegCount];
	Vector totalLegIkXzOffset = Vector(kZero);
	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		Vector legIkXzOffset = Vector(kZero);

		aLegIkTargets[iLeg] = ApplyLegToGround(pCharacter, 
											pController, 
											animGroundPosWs, 
											animGroundNormalWs, 
											pLegIkFootDef,
											legIkPlatform,
											iLeg, 
											m_legIks[iLeg]->GetAnkleLocWs(),
											m_legIks[iLeg]->GetHeelLocWs(), 
											m_legOnGround[iLeg], 
											legIkXzOffset);
		
		pController->SetFootOnGround(iLeg, m_legOnGround[iLeg]);

		totalLegIkXzOffset += legIkXzOffset;
	}

	m_rootXzOffset = m_rootXzOffsetTracker.Track(m_rootXzOffset, totalLegIkXzOffset, GetProcessDeltaTime(), 15.0f);
	DoLegIk(pCharacter, pController, aLegIkTargets, m_legCount, m_forceRootShift ? &m_rootShift : nullptr, m_rootXzOffset);

	if (DEBUG_LEG_IK)
	{	// Debug draw joints post IK
		for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
		{
			m_legIks[iLeg]->DebugDrawJoints(false, false, true, kColorMagenta);
		}
	}

	m_forceRootShift = false;
	SetLastRootBase(pCharacter, pController);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MoveLegIkNew::UpdateFeetPlanted(Character* pCharacter, bool reset, const Locator aLegPts[], int legCount)
{
	PROFILE(Processes, MoveLegIkNew_UpdateFeetPlanted);

	ANIM_ASSERT(legCount == pCharacter->GetLegCount());
	ANIM_ASSERT(legCount == m_legCount);

	const AnimControl *pAnimControl = pCharacter->GetAnimControl();
	const FgAnimData& animData = pAnimControl->GetAnimData();
	const ArtItemSkeleton* pArtItemSkel = animData.m_curSkelHandle.ToArtItem();
	const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	const AnimStateInstance* pInstance = pBaseLayer->CurrentStateInstance();
	const AnimStateSnapshot& stateSnapshotOrig = pInstance->GetAnimStateSnapshot();

	// Duplicate the anim state snapshot
	ScopedTempAllocator jj(FILE_LINE_FUNC);

	AnimStateSnapshot stateSnapshot(stateSnapshotOrig);
	
	SnapshotNodeHeap* pNewHeap = GetGlobalSnapshotNodeHeap();

	{
		PROFILE(Processes, MoveLegIkNew_UpdateFeetPlanted_CloneNodes);
		const AnimSnapshotNode* pRootNode = stateSnapshotOrig.GetSnapshotNode(stateSnapshotOrig.m_rootNodeIndex);
		stateSnapshot.m_rootNodeIndex = pRootNode->Clone(pNewHeap, stateSnapshotOrig.m_pSnapshotHeap);
	}

	stateSnapshot.m_pSnapshotHeap = pNewHeap;

	const DC::AnimInfoCollection* pInfo = pAnimControl->GetInfoCollection();
	const ndanim::JointHierarchy* pJointHierarchy = animData.m_pSkeleton;
	Binding characterBinding = pCharacter->GetBinding();

	const float currentPhase = pInstance->Phase();
	const float updateRate = stateSnapshot.m_updateRate;

	const F32 endPhaseRaw = currentPhase + updateRate * 2.0f;	// 2 second look-ahead
	const F32 endPhasePass0 = Limit01(endPhaseRaw);
	const F32 endPhasePass1 = Limit01(endPhaseRaw - 1.0f);

	bool aFootstepFound[kQuadLegCount] = { false, false, false, false };
	Locator aPredictedLocWs[kQuadLegCount];
	Locator aAnkleWs[kQuadLegCount];
	float aPrevFootstepPhase[kQuadLegCount];

	for (int iLeg = 0; iLeg < legCount; ++iLeg)
	{
		aPrevFootstepPhase[iLeg] = m_footstepPhase[iLeg];
		float footstepPhaseDiff = m_footstepPhase[iLeg] - currentPhase;
		if (footstepPhaseDiff <= 0.0f || footstepPhaseDiff > 0.5f)
			m_footstepPlantTime[iLeg] = 0.2f;
		else if (m_footstepPlantTime[iLeg] > 0.0f)
			m_footstepPlantTime[iLeg] -= GetProcessDeltaTime();

		m_footstepPhase[iLeg] = -1.0f;
	}

	m_nextFootplant = -1;

	// Do pass 1
	const Locator charLocWs = pCharacter->GetLocator();
	UpdateNextFootsteps(pCharacter,
						stateSnapshot,
						currentPhase,
						endPhasePass0,
						charLocWs,
						pInstance->GetApLocator(),
						pArtItemSkel,
						pInfo,
						pJointHierarchy,
						characterBinding);

	DetermineNextFootStep();

	bool hasNegativeFootstepPhase = false;
	for (int iLeg = 0; iLeg < legCount; ++iLeg)
	{
		hasNegativeFootstepPhase = hasNegativeFootstepPhase || m_footstepPhase[iLeg] < 0.0f;
	}

	if (stateSnapshot.HasLoopingAnim() && endPhasePass1 > 0.0f && hasNegativeFootstepPhase)
	{
		// Extract the align movement for the remainder of the animation
		StringId64 alignChannelId = SID("align");
		stateSnapshot.RefreshPhasesAndBlends(currentPhase, true, pInfo);
		ndanim::JointParams effectJointParams1;
		SnapshotEvaluateParams params;
		params.m_statePhase = params.m_statePhasePre = currentPhase;
		params.m_blendChannels = pInfo && pInfo->m_actor && (pInfo->m_actor->m_blendChannels != 0);
		stateSnapshot.Evaluate(&alignChannelId, 1, &effectJointParams1, params);

		stateSnapshot.RefreshPhasesAndBlends(1.0f, true, pInfo);
		params.m_statePhase = params.m_statePhasePre = 1.0f;
		ndanim::JointParams effectJointParams2;
		stateSnapshot.Evaluate(&alignChannelId, 1, &effectJointParams2, params);

		const Locator preAlign = Locator(effectJointParams1.m_trans, effectJointParams1.m_quat);
		const Locator postAlign = Locator(effectJointParams2.m_trans, effectJointParams2.m_quat);

		const Locator deltaLocLs = preAlign.UntransformLocator(postAlign);
		const Locator predictedLoopLocWs = charLocWs.TransformLocator(deltaLocLs);

		// Now do pass 2
		UpdateNextFootsteps(pCharacter,
							stateSnapshot,
							0.0f,
							endPhasePass1,
							predictedLoopLocWs,
							pInstance->GetApLocator(),
							pArtItemSkel,
							pInfo,
							pJointHierarchy,
							characterBinding);

		if (m_nextFootplant < 0)
		{
			DetermineNextFootStep();
		}
	}

	bool hasPositiveFootstepPhase = false;
	for (int iLeg = 0; iLeg < legCount; ++iLeg)
	{
		m_groundModelStartPos[iLeg] = aLegPts[iLeg].TransformLocator(m_footRefPointAnkleSpace[iLeg]).GetTranslation();
		hasNegativeFootstepPhase = hasNegativeFootstepPhase || m_footstepPhase[iLeg] >= 0.0f;
	}

	m_alignGround = GroundModel();

	if (hasPositiveFootstepPhase)
	{
		m_alignPos = charLocWs.GetTranslation();

		m_alignGroundModelEndPos = charLocWs.GetTranslation();

		for (int iLeg = 0; iLeg < legCount; ++iLeg)
		{
			if (m_footstepPhase[iLeg] >= 0.0f)
			{
				ASSERT(Dist(m_groundModelStartPos[iLeg], kOrigin) > 0.01f);
				ASSERT(Dist(m_footstepAnkleLoc[iLeg].GetTranslation(), kOrigin) > 0.01f);
				ASSERT(Dist(m_footstepAlignLoc[iLeg].GetTranslation(), kOrigin) > 0.01f);

				if (Dist(m_alignGroundModelStartPos, m_footstepAlignLoc[iLeg].GetTranslation())
					> Dist(m_alignGroundModelStartPos, m_alignGroundModelEndPos))
				{
					m_alignGroundModelEndPos = m_footstepAlignLoc[iLeg].GetTranslation();
				}
			}
		}

		const Vector alignGroundDir = SafeNormalize(m_alignGroundModelEndPos - m_alignPos, kZero);
		m_alignGroundModelStartPos	= m_alignPos
									 + alignGroundDir
										   * Min(SCALAR_LC(0.05f), Dist(m_alignGroundModelEndPos, m_alignPos));

		Scalar minDotAlignGroundDir = kZero;
		for (int iLeg = 0; iLeg < legCount; iLeg++)
		{
			minDotAlignGroundDir = Min(minDotAlignGroundDir, Dot(m_groundModelStartPos[iLeg] - m_alignPos, alignGroundDir));
		}
		m_alignGroundModelStartPos += minDotAlignGroundDir * alignGroundDir;

		ICharacterLegIkController* pLegIkController = pCharacter->GetLegIkController();
		bool inSnow = pLegIkController ? pLegIkController->InSnow() : false;
		FindFootPlantGround(inSnow);
	}
	else
	{
		for (int iLeg = 0; iLeg < legCount; ++iLeg)
		{
			if (m_footstepPhase[iLeg] < 0.0f)
			{
				m_groundModel[iLeg] = GroundModel();
				m_groundModel[iLeg].SetOwner(pCharacter);
			}
		}
	}
	if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugMovingLegIkTest))
	{
		m_alignGround.DebugDraw(kColorBlue, kPrimDuration1FramePauseable);
	}

	// Make sure the ground planes are sane
	{
		const Point charPos = charLocWs.GetTranslation();
		for (int i = 0; i < legCount; ++i)
		{
			if (m_groundModel[i].IsValid())
			{
				Maybe<Point> isect = m_groundModel[i].ProjectPointToGround(charPos);
				bool planeValid	   = true;
				Point groundModelPos;
				if (isect.Valid())
					groundModelPos = isect.Get();
				else
					groundModelPos = m_groundModel[i].GetClosestPointOnGround(charPos);

				Scalar distSqr = DistSqr(groundModelPos, charPos);
				if (distSqr <= Sqr(0.7f))
				{
					planeValid = true;
				}
				else
				{
					planeValid = false;
				}

				if (!planeValid)
				{
					if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugMovingLegIkTest))
					{
						m_groundModel[i].DebugDraw(isect.Valid() ? kColorRed : kColorOrange, Seconds(2.0f));
					}
					m_groundModel[i] = GroundModel();
					m_groundModel[i].SetOwner(pCharacter);
				}
			}

			if (m_prevUsedGroundPtValid[i] && m_groundModel[i].IsValid())
			{
				m_groundModel[i].EnforcePointOnGround(m_prevUsedGroundPt[i].GetTranslation());
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MoveLegIkNew::UpdateNextFootsteps(Character* pCharacter,
									   AnimStateSnapshot& stateSnapshot,
									   float startPhase,
									   float endPhase,
									   const Locator initialLocWs,
									   const BoundFrame& apRef,
									   const ArtItemSkeleton* pArtItemSkel,
									   const DC::AnimInfoCollection* pInfo,
									   const ndanim::JointHierarchy* pJointHierarchy,
									   Binding characterBinding)
{
	PROFILE(Processes, UpdateNextFootsteps);
	ScopedTempAllocator jj(FILE_LINE_FUNC);

	EffectList effectList;
	effectList.Init(200);

	const AnimStateInstance* pCurInstance = pCharacter->GetAnimControl()->GetBaseStateLayer()->CurrentStateInstance();

	const float kDisableValidation = 0.0f;
	stateSnapshot.GetTriggeredEffects(pInfo,
									  startPhase,
									  endPhase,
									  false,
									  1.0f,
									  true,
									  false,
									  pCurInstance->GetStateName(),
									  pCurInstance->AnimFade(),
									  &effectList,
									  pCurInstance,
									  kDisableValidation);

	StringId64 alignChannelId = SID("align");
	ndanim::JointParams currentJointParams;

	stateSnapshot.RefreshPhasesAndBlends(startPhase, true, pInfo);

	SnapshotEvaluateParams params;
	params.m_blendChannels = pInfo && pInfo->m_actor && (pInfo->m_actor->m_blendChannels != 0);
	params.m_statePhase = params.m_statePhasePre = startPhase;
	params.m_flipped = stateSnapshot.IsFlipped();

	stateSnapshot.Evaluate(&alignChannelId, 1, &currentJointParams, params);

	for (U32 effectNum = 0; effectNum < effectList.GetNumEffects(); ++effectNum)
	{
		const EffectAnimInfo* pEffInfo = effectList.Get(effectNum);
		const EffectAnimEntry* pEntry = pEffInfo->m_pEffect;
		if (pEntry->GetNameId() == SID("foot-effect") && pEntry->GetNumTags() == 3)
		{
			// WTF? This seems pointless
			//if (pEntry->GetTagByName(SID("joint")))
			//{
			//	switch(pEntry->GetTagByName(SID("joint"))->GetValueAsStringId().GetValue())
			//	{
			//	case SID_VAL("l_ball"):
			//	case SID_VAL("r_ball"):
			//		break;
			//	default:
			//		continue;
			//		break;
			//	}
			//}
			const ArtItemAnim* pAnim = AnimMasterTable::LookupAnim(pArtItemSkel->m_skelId, pJointHierarchy->m_animHierarchyId, pEffInfo->m_anim).ToArtItem();
			if (pAnim)
			{
				const F32 animMayaFrames = GetMayaFrameFromClip(pAnim->m_pClipData, 1.0f);
				const F32 animPhase = pEntry->GetFrame() / animMayaFrames;

				// Hardcoded as the layout is 'FRAME NAME JOINT.....'
				const EffectAnimEntryTag* pTag = pEntry->GetTagByName(SID("joint"));
				if (!pTag)
				{
					continue;
				}
				ANIM_ASSERT(pTag->GetNameId() == SID("joint"));
				StringId64 jointId = pTag->GetValueAsStringId();

				const int legCount = m_legCount;

				for (int iLeg = 0; iLeg < legCount; ++iLeg)
				{
					const int footIndex = stateSnapshot.IsFlipped() ? GetMatchingLegIndex(iLeg) : iLeg;
					const StringId64* pFootJointIds;
					const int jointCount = GetFootJointIds(pCharacter->GetFootIkCharacterType(), &pFootJointIds);
					ANIM_ASSERT(jointCount == legCount);

					const StringId64* pFallbackFootJointIds;
					const int fallbackJointCount = GetFallbackFootJointIds(pCharacter->GetFootIkCharacterType(), &pFallbackFootJointIds);
					ANIM_ASSERT(fallbackJointCount == legCount);

					if ((jointId == pFootJointIds[footIndex] || jointId == pFallbackFootJointIds[footIndex]) && m_footstepPhase[iLeg] < 0.0f)
					{
						// Figure out where the 'align' is when this effect fires
						stateSnapshot.RefreshPhasesAndBlends(animPhase, true, pInfo);
						ndanim::JointParams alignEffectJointParams;

						params.m_statePhase = params.m_statePhasePre = animPhase;
						params.m_flipped = stateSnapshot.IsFlipped();

						stateSnapshot.Evaluate(&alignChannelId, 1, &alignEffectJointParams, params);

						const Locator preAlign = Locator(currentJointParams.m_trans, currentJointParams.m_quat);
						const Locator postAlign = Locator(alignEffectJointParams.m_trans, alignEffectJointParams.m_quat);

						const Locator deltaLocLs = preAlign.UntransformLocator(postAlign);
						Locator predictedLocWs = initialLocWs.TransformLocator(deltaLocLs);

						if (stateSnapshot.m_animState.m_flags & DC::kAnimStateFlagApMoveUpdate)
						{
							StringId64 apRefChannelId = SID("apReference");
							ndanim::JointParams apRefEffectJointParams;
							if (stateSnapshot.Evaluate(&apRefChannelId, 1, &apRefEffectJointParams, params))
							{
								Locator apRefAlignSpace(apRefEffectJointParams.m_trans, apRefEffectJointParams.m_quat);
								predictedLocWs = apRef.GetLocator().TransformLocator(Inverse(apRefAlignSpace));
							}
						}

						// Ok, we now know where we will end up... now let's pull the ankle locators
						ndanim::JointParams effectJointParams[kQuadLegCount];
						const U32 validMask = stateSnapshot.Evaluate(kAnkleLocIds, legCount, effectJointParams, params);

						if (IsChannelMaskValid(validMask, legCount))
						{
							m_footstepPhase[iLeg] = animPhase;
							m_footstepAlignLoc[iLeg] = BoundFrame(predictedLocWs, characterBinding);

							const Locator ankleLocLs = Locator(effectJointParams[footIndex].m_trans, effectJointParams[footIndex].m_quat);
							const Locator ankleWs = predictedLocWs.TransformLocator(ankleLocLs);

							const int otherFootIndex = GetMatchingLegIndex(footIndex);
							const Locator offAnkleLocLs = Locator(effectJointParams[otherFootIndex].m_trans, effectJointParams[otherFootIndex].m_quat);
							const Locator offAnkleWs = predictedLocWs.TransformLocator(offAnkleLocLs);
							
							m_footstepAnkleLoc[iLeg] = BoundFrame(ankleWs.TransformLocator(m_footRefPointAnkleSpace[iLeg]), characterBinding);

							Plane groundPlane = GetAnimatedGroundPlane( stateSnapshot, predictedLocWs);
							if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugMovingLegIkTest))
							{
								g_prim.Draw(DebugCoordAxesLabeled(predictedLocWs, StringBuilder<128>("%s\nalign phase: %f\nPos: %.3f %.3f %.3f", DevKitOnly_StringIdToString(jointId), animPhase, (float)predictedLocWs.GetTranslation().X(), (float)predictedLocWs.GetTranslation().Y(), (float)predictedLocWs.GetTranslation().Z()).c_str(), 0.2f), kPrimDuration1FramePauseable);
								g_prim.Draw( DebugCoordAxes(ankleWs, 0.1f), kPrimDuration1FramePauseable);
								g_prim.Draw( DebugCoordAxes(offAnkleWs, 0.1f), kPrimDuration1FramePauseable);
								g_prim.Draw( DebugLine(predictedLocWs.GetTranslation(), ankleWs.GetTranslation()), kPrimDuration1FramePauseable);
								g_prim.Draw( DebugLine(predictedLocWs.GetTranslation(), offAnkleWs.GetTranslation()), kPrimDuration1FramePauseable);
							}

							bool allFootstepPhasesValid = true;
							for (int iFootstep = 0; iFootstep < legCount; ++iFootstep)
							{
								if (m_footstepPhase[iFootstep] < 0.0f)
									allFootstepPhasesValid = false;
							}

							if (allFootstepPhasesValid)
								return;
						}
					}
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MoveLegIkNew::GetMeshRaycastPointsWs(Character* pCharacter,
										  Point* pLeftLegWs,
										  Point* pRightLegWs,
										  Point* pFrontLeftLegWs,
										  Point* pFrontRightLegWs)
{
	InitAnkleSpace(pCharacter);
	const int legCount = pCharacter->GetLegCount();
	Point* apLegWs[kQuadLegCount] = { pLeftLegWs, pRightLegWs, pFrontLeftLegWs, pFrontRightLegWs };
	for (int iLeg = 0; iLeg < legCount; ++iLeg)
	{
		ANIM_ASSERT(apLegWs[iLeg]);
		*apLegWs[iLeg] = m_legIks[iLeg]->GetAnkleLocWs().TransformPoint(m_footRefPointAnkleSpace[iLeg].GetTranslation());
	}
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Vector GroundPlaneNormFromPoints(Point_arg p0, Point_arg p1)
{
	Vector v(p1-p0);
	return SafeNormalize(Cross(Cross(v, kUnitYAxis),v), kUnitYAxis);
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct sFindGroundJobParams
{
	sFindGroundJobParams()
	{
		memset(this, 0, sizeof(*this));
	}

	void Validate()
	{
		ALWAYS_ASSERT(IsFinite(startPos));
		ALWAYS_ASSERT(IsFinite(endPos));
		ALWAYS_ASSERT(IsFinite(alignPos));
	}

	Point startPos;
	Point endPos;
	Point alignPos;
	bool collisionOnly;
	GroundModel* m_pGroundModel;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void MoveLegIkNew::FindFootPlantGround(bool inSnow)
{
	ndjob::JobDecl* jobDecls = NDI_NEW(kAllocSingleGameFrame, kAlign64) ndjob::JobDecl[3];

	ndjob::CounterHandle pJobCounter = ndjob::AllocateCounter(FILE_LINE_FUNC);
	int numJobs = 0;
	
	for (int i = 0; i < m_legCount; i++)
	{
		if (m_footstepPhase[i] >= 0.0f)
		{
			sFindGroundJobParams* pParams = NDI_NEW(kAllocSingleGameFrame) sFindGroundJobParams;
			pParams->alignPos = m_footstepAlignLoc[i].GetTranslation();
			pParams->endPos = m_footstepAnkleLoc[i].GetTranslation();
			pParams->startPos = m_groundModelStartPos[i];
			pParams->m_pGroundModel = &m_groundModel[i];
			pParams->collisionOnly = inSnow;	// Disable mesh raycasts in deep snow
	
			pParams->Validate();
			
			jobDecls[numJobs] = ndjob::JobDecl(FindGroundJob, (uintptr_t)(pParams));
			jobDecls[numJobs].m_associatedCounter = pJobCounter;
	
			numJobs++;
		}
	}

	bool hasPositiveFootstepPhase = false;
	for (int i = 0; i < m_legCount; ++i)
	{
		if (m_footstepPhase[i] >= 0.0f)
		{
			hasPositiveFootstepPhase = true;
			break;
		}
	}

	if (hasPositiveFootstepPhase)
	{
		sFindGroundJobParams* pParams = NDI_NEW(kAllocSingleGameFrame) sFindGroundJobParams;
		pParams->alignPos = m_alignPos;
		pParams->endPos = m_alignGroundModelEndPos;
		pParams->startPos = m_alignGroundModelStartPos;
		pParams->m_pGroundModel = &m_alignGround;
		pParams->collisionOnly = true;

		pParams->Validate();

		jobDecls[numJobs] = ndjob::JobDecl(FindGroundJob, (uintptr_t)(pParams));
		jobDecls[numJobs].m_associatedCounter = pJobCounter;
		numJobs++;
	}
	pJobCounter->SetValue(numJobs);
	ndjob::JobArrayHandle JobArray = ndjob::BeginJobArray(numJobs, ndjob::GetActiveJobPriority());
	ndjob::AddJobs(JobArray, jobDecls, numJobs);
	ndjob::CommitJobArray(JobArray);
	ndjob::WaitForCounterAndFree(pJobCounter);

	
}

/// --------------------------------------------------------------------------------------------------------------- ///
CLASS_JOB_ENTRY_POINT_IMPLEMENTATION(MoveLegIkNew, FindGroundJob)
{
	const sFindGroundJobParams* pParams = reinterpret_cast<sFindGroundJobParams*>(jobParam);
	if (pParams->collisionOnly)
	{
		pParams->m_pGroundModel->FindGroundForceCol(pParams->startPos, pParams->endPos, pParams->alignPos);
	}
	else
	{
		pParams->m_pGroundModel->FindGround(pParams->startPos, pParams->endPos, pParams->alignPos);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct sUpdateFeetPlantedJobParams
{
	Locator m_aAnklePos[kQuadLegCount];
	Character* m_pCharacter;
	MoveLegIkNew* m_pLegIk; // m_pLegIk->m_legCount is the number of valid elements in m_aAnklePos
};

/// --------------------------------------------------------------------------------------------------------------- ///
void MoveLegIkNew::PostAnimUpdate(Character* pCharacter, CharacterLegIkController* pController)
{
	PROFILE(Processes, MoveLegIkNew_PostAnimUpdate);
	//InitAnkleSpace(pCharacter);

	if (m_pJobCounter)
	{
		ndjob::WaitForCounterAndFree(m_pJobCounter);
		m_pJobCounter = nullptr;
	}
	BoxedValue result;
	if (!g_bSendEventToVFunc)
		result = SendEvent(SID("can-use-predictive-ik?"), pCharacter);

	if ((!g_bSendEventToVFunc && result.IsValid() && result.GetAsBool(false)) ||
		(g_bSendEventToVFunc && pCharacter && pCharacter->CanUsePredicativeIk())
		)
		;
	else
	{
		for (int i = 0; i < m_legCount; ++i)
		{
			m_footstepPhase[i] = -1;
			m_groundModel[i] = GroundModel();
			m_groundModel[i].SetOwner(pCharacter);
			m_prevUsedGroundPtValid[i] = false;
		}
		m_nextFootplant = -1;
		m_alignGround = GroundModel();
		m_alignGround.SetOwner(pCharacter);
		return;
	}

	ALWAYS_ASSERT(m_pJobCounter == nullptr);
	m_pJobCounter = ndjob::AllocateCounter(FILE_LINE_FUNC);

	ndjob::JobDecl* pJobDecl = NDI_NEW(kAllocSingleGameFrame, kAlign64) ndjob::JobDecl;

	sUpdateFeetPlantedJobParams* pParams = NDI_NEW(kAllocSingleGameFrame) sUpdateFeetPlantedJobParams;
	for (int i = 0; i < m_legCount; ++i)
	{
		pParams->m_aAnklePos[i] = m_legIks[i]->GetAnkleLocWs();
	}
	pParams->m_pCharacter = pCharacter;
	pParams->m_pLegIk = this;

	*pJobDecl = ndjob::JobDecl(UpdateFeetPlantedJob, (uintptr_t)(pParams));
	pJobDecl->m_associatedCounter = m_pJobCounter;

	m_pJobCounter->SetValue(1);
	ndjob::JobArrayHandle JobArray = ndjob::BeginJobArray(1, ndjob::GetActiveJobPriority()); // we wait for this in PostAnimBlending
	ndjob::AddJobs(JobArray, pJobDecl, 1);
	ndjob::CommitJobArray(JobArray);

	//UpdateFeetPlanted(pCharacter, false, m_legIks[kLeftLeg]->GetAnkleLocWs(), m_legIks[kRightLeg]->GetAnkleLocWs());
}

/// --------------------------------------------------------------------------------------------------------------- ///
CLASS_JOB_ENTRY_POINT_IMPLEMENTATION(MoveLegIkNew, UpdateFeetPlantedJob)
{
	const sUpdateFeetPlantedJobParams* pParams = reinterpret_cast<sUpdateFeetPlantedJobParams*>(jobParam);
	const int legCount = pParams->m_pCharacter->GetLegCount();
	pParams->m_pLegIk->UpdateFeetPlanted(pParams->m_pCharacter, false, pParams->m_aAnklePos, legCount);
}

/// --------------------------------------------------------------------------------------------------------------- ///
MoveLegIkNew::MoveLegIkNew() : m_pJobCounter(nullptr), m_nextFootplant(-1), m_ankleSpaceValid(false) {}

/// --------------------------------------------------------------------------------------------------------------- ///
MoveLegIkNew::~MoveLegIkNew()
{
	if (m_pJobCounter)
	{
		ndjob::WaitForCounterAndFree(m_pJobCounter);
		m_pJobCounter = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<Point> MoveLegIkNew::GetNextPredictedFootPlant() const
{
	if (m_nextFootplant >= 0)
	{
		ANIM_ASSERT(m_nextFootplant < m_legCount);
		return m_groundModel[m_nextFootplant].ProjectPointToGround(m_footstepAnkleLoc[m_nextFootplant].GetTranslation());
	}
	return MAYBE::kNothing;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MoveLegIkNew::DetermineNextFootStep()
{
	//try to find lowest non-negative footstepPhase
	int bestIndex = -1;
	float bestPhase = 1.0f;

	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		if (m_footstepPhase[iLeg] < 0.0f)
			continue;

		if (m_footstepPhase[iLeg] < bestPhase)
		{
			bestPhase = m_footstepPhase[iLeg];
			bestIndex = iLeg;
		}
	}

	if (bestIndex >= 0)
	{
		m_nextFootplant = bestIndex;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MoveLegIkNew::GetPredictedFootPlants(Maybe<Point> (&pos)[kQuadLegCount]) const
{
	for (int i = 0; i < kQuadLegCount; i++)
	{
		if (i < m_legCount && m_footstepPhase[i] >= 0.0f)
		{
			pos[i] = m_groundModel[i].ProjectPointToGround(m_footstepAnkleLoc[i].GetTranslation());
		}
		else
		{
			pos[i] = MAYBE::kNothing;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MoveLegIkNew::SingleFrameDisableLeg(int legIndex)
{
	m_singleFrameDisableLeg[legIndex] = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MoveLegIkNew::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		m_groundModel[iLeg].Relocate(deltaPos, lowerBound, upperBound);
	}

	ILegIk::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MoveLegIkNew::SetLastRootBase(Character* pCharacter, CharacterLegIkController* pController)
{
	m_lastRootBase = pCharacter->GetBoundFrame();
	Point pos = m_lastRootBase.GetTranslation();
	pos.SetY(pController->m_rootBaseY);
	m_lastRootBase.SetTranslation(pos);
	m_lastRootBaseValid = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MoveLegIkNew::InitAnkleSpace(Character* pCharacter)
{
	// TODO@QUAD look at this for how to do adjustment for ankle normals

	if (!m_ankleSpaceValid)
	{		
		if (!pCharacter->IsInitialized())
		{
			m_footRefPointAnkleSpace[0] = m_footRefPointAnkleSpace[1] = m_footRefPointAnkleSpace[2] = m_footRefPointAnkleSpace[3] = Locator(kIdentity);
			m_ankleAxis[0] = m_ankleAxis[1] = m_ankleAxis[2] = m_ankleAxis[3] = Vector(kUnitYAxis);
			return;
		}

		const float blend = 0.5f;
		const ndanim::JointHierarchy* pJointHier = pCharacter->GetAnimControl()->GetSkeleton();
		const Mat34* pInvBindPose = GetInverseBindPoseTable(pJointHier);

		const JointCache* pJointCache = pCharacter->GetAnimControl()->GetJointCache();

		const FootIkCharacterType charType = pCharacter->GetFootIkCharacterType();
		const int legCount = GetLegCountForCharacterType(charType);
		for (int iLeg = 0; iLeg < legCount; ++iLeg)
		{
			const I32 ankleIndex = pCharacter->FindJointIndex(GetJointName(charType, JointFromLegAndJointType(iLeg, kJointTypeAnkle)));
			const I32 refIndex0 = pCharacter->FindJointIndex(GetJointName(charType, JointFromLegAndJointType(iLeg, kJointTypeHeel)));
			const I32 refIndex1 = pCharacter->FindJointIndex(GetJointName(charType, JointFromLegAndJointType(iLeg, kJointTypeBall)));

			const Locator ankleBs(Inverse(pInvBindPose[ankleIndex].GetMat44()));
			const Locator joint0Bs(Inverse(pInvBindPose[refIndex0].GetMat44()));
			const Locator joint1Bs(Inverse(pInvBindPose[refIndex1].GetMat44()));
			const Locator desiredEndEffectorBs = Lerp(joint0Bs, joint1Bs, blend);
			m_footRefPointAnkleSpace[iLeg] = ankleBs.UntransformLocator(desiredEndEffectorBs);

			Transform defaultJointPosOs;
			bool valid = pJointCache->GetBindPoseJointXform(defaultJointPosOs, ankleIndex);
			ANIM_ASSERT(valid);

			const Vec4 baseAnkleAxis(0.0f, 0.0f, 1.0f, 0.0f);
			m_ankleAxis[iLeg] = Vector(MulVectorMatrix(baseAnkleAxis, defaultJointPosOs.GetMat44()));
		}

		m_ankleSpaceValid = true;
	}
}
