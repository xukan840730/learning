/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/leg-ik/leg-ik.h"

#include "ndlib/anim/footik.h"
#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/nd-options.h"
#include "ndlib/util/graph-display.h"
#include "ndlib/util/quick-plot.h"
#include "ndlib/util/tracker.h"

#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/leg-ik/character-leg-ik-controller.h"

#include <Eigen/Dense>

/// --------------------------------------------------------------------------------------------------------------- ///
// Spring Constants
float g_rootYSpeedSpring = 20.0f;
float g_rootYSpring = 7.0f;
float g_legYSpring = 300.0f;
float g_footNormalSpring = 20.0f;

bool g_drawIkGraphs = false;

bool g_useNewIkSolve = true;

/// --------------------------------------------------------------------------------------------------------------- ///
void ILegIk::InitLegIk(Character* pCharacter, LegIkChain *pLegIks, int legCount)
{
	ANIM_ASSERT(legCount == kLegCount || legCount == kQuadLegCount);

	m_legCount = legCount;

	for (int iLeg = 0; iLeg < legCount; ++iLeg)
	{
		m_legIks[iLeg] = &pLegIks[iLeg];
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ILegIk::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		RelocatePointer(m_legIks[iLeg], deltaPos, lowerBound, upperBound);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ILegIk::Start(Character* pCharacter)
{
	m_blend = 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ILegIk::SetBlend(float blend)
{
	m_blend = blend;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ILegIk::BlendIn(float blendSpeed)
{
	Seek(m_blend, 1.0f, FromUPS(blendSpeed));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ILegIk::BlendOut(float blendSpeed)
{
	Seek(m_blend, 0.0f, FromUPS(blendSpeed));
}

/// --------------------------------------------------------------------------------------------------------------- ///
float ILegIk::GetBlend(CharacterLegIkController* pController)
{
	return m_blend * pController->GetSlopeBlend();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ILegIk::SetupDefaultIkInfo(LegIkInstance* ik)
{
	ik->m_thighLimitMin = DEGREES_TO_RADIANS(-180.0f);
	ik->m_thighLimitMax = DEGREES_TO_RADIANS(180.0f);
	ik->m_kneeLimitMax	= DEGREES_TO_RADIANS(180.0f);
	ik->m_kneeLimitMin	= DEGREES_TO_RADIANS(0.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ILegIk::SingleFrameDisableLeg(int legIndex) {}

/// --------------------------------------------------------------------------------------------------------------- ///
void ILegIk::SingleFrameUnfreeze() {}

/// --------------------------------------------------------------------------------------------------------------- ///
void ILegIk::AdjustRootPositionFromLegs(Character* pCharacter,
										CharacterLegIkController* pController,
										Point aLegs[],
										int legCount,
										float* pDesiredRootBaseY)
{
	ANIM_ASSERT(legCount == m_legCount);
	if (!pController->m_enableRootAdjust)
		return;

	Point* apLegs[kQuadLegCount] = { nullptr };

	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		ALWAYS_ASSERTF(IsFinite(aLegs[iLeg]),
					   ("invalid legs passed in. leg Index: %d Character: %s",
						iLeg,
						pCharacter ? DevKitOnly_StringIdToString(pCharacter->GetUserId()) : "<none>"));
		apLegs[iLeg] = &aLegs[iLeg];
	}

	//apLegs has pointers to all of the members of aLegs, so aLegs may be modified by this call
	AdjustFeet(apLegs[0], apLegs[1], apLegs[2], apLegs[3]);

	Point aOrigLegPos[kQuadLegCount];
	Vector aLegDelta[kQuadLegCount];
	F32 rootBase = kLargeFloat;
	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		aOrigLegPos[iLeg] = m_legIks[iLeg]->GetAnkleLocWs().GetTranslation();
		aLegDelta[iLeg] = aLegs[iLeg] - aOrigLegPos[iLeg];
		rootBase = Min(rootBase, (float)aLegDelta[iLeg].Y());
	}

	if (pCharacter->ShouldIkMoveRootUp())
	{
		rootBase = -kLargeFloat;
		for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
		{
			rootBase = Max(rootBase, aLegDelta[iLeg].Y());
		}
	}

	const float baseY = pCharacter->GetTranslation().Y();
	ALWAYS_ASSERT(IsFinite(baseY));

	
	float rootShiftDelta = GetAdjustedRootDelta(rootBase);

	

// 	pController->m_rootDelta = realRootY - baseY;
// 
// 	if (pController->m_rootYInited)
// 	{
// 		pController->m_rootY += pController->m_rootDelta - pController->m_lastRootDelta;
// 		pController->m_lastRootY += pController->m_rootDelta - pController->m_lastRootDelta;
// 	}
// 	pController->m_lastRootDelta = pController->m_rootDelta;

	float rootBaseY = rootShiftDelta + baseY;

	if (pDesiredRootBaseY)
	{
		if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugMovingLegIkTest))
		{
			MsgCon("Desired Root base Y: %f\n",  *pDesiredRootBaseY);
		}
		rootBaseY = *pDesiredRootBaseY;
	}

	if (pController->m_rootBaseYInited)
	{				
		float desiredSpeed = 0.0f;
		if (GetProcessDeltaTime() > 0.0001f)
		{
			desiredSpeed = (rootBaseY - pController->m_lastRootBaseY) / GetProcessDeltaTime();
		}

		pController->m_rootBaseYSpeed = pController->m_rootYSpeedSpring.Track(pController->m_rootBaseYSpeed, desiredSpeed, GetProcessDeltaTime(), g_rootYSpeedSpring);

		float rootSpring = g_rootYSpring;
		if (pDesiredRootBaseY)
		{
			pController->m_rootBaseYSpeed = desiredSpeed;
			pController->m_rootYSpeedSpring.Reset();
			rootSpring = 200.0f;
			pController->m_rootBaseY = rootBaseY;
			pController->m_rootYSpring.Reset();
		}
		else
		{
			pController->m_rootBaseY += FromUPS(pController->m_rootBaseYSpeed);
			pController->m_rootBaseY = pController->m_rootYSpring.Track(pController->m_rootBaseY, rootBaseY, GetProcessDeltaTime(), rootSpring);
		}

		
						
// 		if (rootBaseY < pController->m_rootY)
// 		{
// 			pController->m_rootY = rootBaseY;
// 			pController->m_rootYSpring.Reset();
// 		}
		
	}
	else
	{
		pController->m_rootBaseYSpeed = 0.0f;
		pController->m_lastRootBaseY = rootBaseY;
		pController->m_rootBaseY = rootBaseY;
		pController->m_rootYSpring.Reset();
		pController->m_rootYSpeedSpring.Reset();
		pController->m_rootBaseYInited = true;
	}

	//Debug display a graph
	if (FALSE_IN_FINAL_BUILD(g_drawIkGraphs))
	{
		GraphDisplay::Params baseParams = GraphDisplay::Params().WithGraphType(GraphDisplay::kGraphTypeLine).NoAvgLine().NoAvgText().NoMaxLine().NoMaxText().NoBackground().WithDataColor(Abgr8FromRgba(0, 0, 255, 255));
		baseParams.m_min = -1;
		baseParams.m_max = 1;
		baseParams.m_useMinMax = true;
		baseParams.m_dataColor = Abgr8FromRgba(0,0,255, 128);

		float newRootDelta = pController->m_rootBaseY - baseY;
		QUICK_PLOT(rootDeltaPlot,
				   "Delta",
				   10,
				   260, // x and y coordinates to draw the plot
				   250,
				   250,		   // width and height of the plot
				   60,		   // show values from 60 frames ago up until now
				   baseParams, // provide additional customization of the plot
				   newRootDelta);

		baseParams.m_dataColor = Abgr8FromRgba(255,0,0, 128);
		QUICK_PLOT(lFootDeltaPlot,
				   "Delta",
				   10,
				   260, // x and y coordinates to draw the plot
				   250,
				   250,		   // width and height of the plot
				   60,		   // show values from 60 frames ago up until now
				   baseParams, // provide additional customization of the plot
				   aLegDelta[kLeftLeg].Y());

		baseParams.m_dataColor = Abgr8FromRgba(0,255,0,128);
		QUICK_PLOT(rFootDeltaPlot,
				   "Delta",
				   10,
				   260, // x and y coordinates to draw the plot
				   250,
				   250,		   // width and height of the plot
				   60,		   // show values from 60 frames ago up until now
				   baseParams, // provide additional customization of the plot
				   aLegDelta[kRightLeg].Y());

		// TODO@QUAD add graphs for front legs?
	}

	pController->m_lastRootBaseY = rootBaseY;

	float newRootDelta = pController->m_rootBaseY - baseY;
	ALWAYS_ASSERT(Abs(newRootDelta) < 1e6f);

	if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugMovingLegIkTest))
	{
		MsgCon("Root delta: %f\n", newRootDelta);
	}

	const Point realRootPoint = m_legIks[kLeftLeg]->GetRootLocWs().GetTranslation();
	ALWAYS_ASSERT(IsFinite(realRootPoint));
	const float realRootY = realRootPoint.Y();

	
	pController->m_lastAppliedRootDelta = newRootDelta;

	float minLegY = kLargeFloat;
	float maxLegY = -kLargeFloat;

	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		minLegY = Min(minLegY, aLegs[iLeg].Y());
		maxLegY = Max(maxLegY, aLegs[iLeg].Y());
	}

	const float minLegToRootY = realRootY + newRootDelta - minLegY;
	const float maxLegToRootY = realRootY + newRootDelta - maxLegY;

	if (!pCharacter->ShouldIkMoveRootUp())
	{
		// make sure the highest foot doesn't get too close to the root
		// by moving the feet back down

		const int pairCount = m_legCount / 2;
		for (int iPair = 0; iPair < pairCount; ++iPair)
		{
			const int leftIndex = pairCount * 2;
			const int rightIndex = leftIndex + 1;
			Point& leftLeg = aLegs[leftIndex];
			Point& rightLeg = aLegs[rightIndex];

			if (rightLeg.Y() > leftLeg.Y())
			{
				const float clampedMaxRootY = Max(Min(-GetClampedMin(), realRootY - float(aOrigLegPos[rightIndex].Y())), maxLegToRootY);
				const float legDelta = clampedMaxRootY - maxLegToRootY;
				rightLeg -= Vector(0.0f, legDelta, 0.0f);
			}
			else
			{
				const float clampedMaxRootY = Max(Min(-GetClampedMin(), realRootY - float(aOrigLegPos[leftIndex].Y())), maxLegToRootY);
				const float legDelta = clampedMaxRootY - maxLegToRootY;
				leftLeg -= Vector(0.0f, legDelta, 0.0f);
			}
		}
	}
	

	float minRootDelta = minLegToRootY + minLegY - realRootY;
	float maxRootDelta = maxLegToRootY + maxLegY - realRootY;

	AdjustedRootLimits(minRootDelta, maxRootDelta);

	const float rootDeltaLen = MinMax(newRootDelta * GetBlend(pController), minRootDelta, maxRootDelta)*GetBlend(pController);
	const Vector rootDelta = Vector(0, rootDeltaLen, 0);

	//g_prim.Draw(DebugCross(pCharacter->GetTranslation() + rootDelta, 0.5f, kColorYellow), kPrimDuration1FramePauseable);
	for (int iLeg = 0; iLeg < m_legCount; ++iLeg)
	{
		m_legIks[iLeg]->TranslateRootWs(rootDelta);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ILegIk::AdjustRootPositionFromLegs_Simple(Character* pCharacter,
											   CharacterLegIkController* pController,
											   Point& leftLeg,
											   Point& rightLeg)
{
	ANIM_ASSERTF(m_legCount == kLegCount,
				 ("ILegIk::AdjustRootPositionFromLegs_Simple does not yet support Quadrupeds (character: %s)",
				  pCharacter ? DevKitOnly_StringIdToString(pCharacter->GetUserId()) : "<none>"));

	ALWAYS_ASSERT(IsFinite(leftLeg));
	ALWAYS_ASSERT(IsFinite(rightLeg));

	const Point curRoot = m_legIks[kLeftLeg]->GetRootLocWs().GetTranslation();

	const Point originalLeftLeg = m_legIks[kLeftLeg]->GetAnkleLocWs().GetTranslation();
	const Vector leftLegDelta = leftLeg - originalLeftLeg;

	const Point originalRightLeg = m_legIks[kRightLeg]->GetAnkleLocWs().GetTranslation();
	const Vector rightLegDelta = rightLeg - originalRightLeg;

	float desiredRootDelta = Min((float)leftLegDelta.Y(), (float)rightLegDelta.Y());

	if (!pController->m_rootBaseYInited)
	{
		pController->m_rootBaseYSpeed = 0.0f;
		pController->m_lastRootBaseY = curRoot.Y();
		pController->m_rootBaseY = curRoot.Y();
		pController->m_rootYSpring.Reset();
		pController->m_rootYSpeedSpring.Reset();
		pController->m_rootBaseYInited = true;
		pController->m_lastRootDelta = 0.0f;
	}

	const float dt = GetProcessDeltaTime();
	const float newDelta = pController->m_rootYSpring.Track(pController->m_lastRootDelta, desiredRootDelta, dt, g_rootYSpring);
	const float newDeltaLimited = Limit(newDelta, -0.5f, 0.5f);

	const Vector rootTrans = Vector(0.0f, newDeltaLimited, 0.0f);
	m_legIks[kLeftLeg]->TranslateRootWs(rootTrans);
	m_legIks[kRightLeg]->TranslateRootWs(rootTrans);

	pController->m_lastRootDelta = newDeltaLimited;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ILegIk::DoLegIk(Character* pCharacter,
					 CharacterLegIkController* pController,
					 Locator aLegs[],
					 int legCount,
					 float* pDesiredRootOffset,
					 Vector rootXzOffset)
{
	PROFILE(Processes, DoLegIk);

	ANIM_ASSERT(pCharacter);
	FootIkCharacterType charType = pCharacter->GetFootIkCharacterType();
	ANIM_ASSERT(legCount == GetLegCountForCharacterType(pCharacter->GetFootIkCharacterType()));

	for (int iLeg = 0; iLeg < legCount; ++iLeg)
	{
		ALWAYS_ASSERTF(IsFinite(aLegs[iLeg]), ("Bad Leg Locator given to DoLegIk(). leg: %d, Character: %s", iLeg, DevKitOnly_StringIdToString(pCharacter->GetUserId())));
	}

	if (UseNewSolveTechnique())
	{
		Locator desiredAnkleLoc[kQuadLegCount];

		for (int iLeg = 0; iLeg < legCount; ++iLeg)
		{
			desiredAnkleLoc[iLeg] = aLegs[iLeg];
		}

		FootAnalysis::FeetIkData ikData = FootAnalysis::GetFootData(pCharacter);
// 		for (int i = 0; i < 2; i++)
// 		{
// 			MsgCon("Flight arc: %f\n", ikData.m_footData[i].m_flightArc);
// 			MsgCon("Ground weight: %f\n", ikData.m_footData[i].m_groundWeight);
// 		}

		float flightArcs[] = { ikData.m_footData[kLeftLeg].m_flightArc,
							   ikData.m_footData[kRightLeg].m_flightArc,
							   ikData.m_footData[kFrontLeftLeg].m_flightArc,
							   ikData.m_footData[kFrontRightLeg].m_flightArc };

		RootBaseSmoother smoother(pController, GetBlend(pController), pCharacter->GetTranslation().Y(), pCharacter->OnStairs());
// 
// 		g_prim.Draw(DebugCoordAxesLabeled(desiredAnkleLoc[0], "left"), kPrimDuration1FramePauseable);
// 		g_prim.Draw(DebugCoordAxesLabeled(desiredAnkleLoc[1], "right"), kPrimDuration1FramePauseable);
// 
// 		MsgCon("align y: %f\n", (float)pCharacter->GetTranslation().Y());
		pController->m_lastAppliedRootDelta = FootAnalysis::SolveLegIk(pCharacter->GetLocator(),
																	   m_legIks,
																	   desiredAnkleLoc,
																	   flightArcs,
																	   &smoother,
																	   charType,
																	   legCount,
																	   pCharacter->EnforceIkHipDistance(),
																	   pCharacter->ShouldIkMoveRootUp(),
																	   rootXzOffset,
																	   pDesiredRootOffset);

		pController->OverrideRootSmootherSpring(-1.0f);
		return;
	}

// 	g_prim.Draw( DebugCross(leftLeg, 0.1f, kColorMagenta) );
// 	g_prim.Draw( DebugCross(rightLeg, 0.1f, kColorCyan) );

	if (false) // (pCharacter->IsNpc() && g_npcLegIkSimpleRoot)
	{
		// was unused so I didn't retrofit it to work with quadrupeds -- HRT

		//Point leftLegPos(leftLeg.GetTranslation());
		//Point rightLegPos(rightLeg.GetTranslation());
		//AdjustRootPositionFromLegs_Simple(pCharacter, pController, leftLegPos, rightLegPos);
		//leftLeg.SetTranslation(leftLegPos);
		//rightLeg.SetTranslation(rightLegPos);
	}
	else
	{
		Point aLegPts[kQuadLegCount];
		for (int iLeg = 0; iLeg < legCount; ++iLeg)
		{
			aLegPts[iLeg] = aLegs[iLeg].GetTranslation();
		}
		AdjustRootPositionFromLegs(pCharacter, pController, aLegPts, legCount, pDesiredRootOffset);
		for (int iLeg = 0; iLeg < legCount; ++iLeg)
		{
			aLegs[iLeg].SetTranslation(aLegPts[iLeg]);
		}
	}

	LegIkInstance instances[kQuadLegCount];

	for (int iLeg = 0; iLeg < legCount; ++iLeg)
	{
		instances[iLeg].m_ikChain = m_legIks[iLeg];
		instances[iLeg].m_goalPos = aLegs[iLeg].GetTranslation();
		SetupDefaultIkInfo(&instances[iLeg]);

		const Quat anklePreIk = m_legIks[kLeftLeg]->GetAnkleLocWs().GetRotation();

		SolveLegIk(&instances[iLeg]);
		
		// readjust the foot normal
		const Quat anklePostIk = m_legIks[iLeg]->GetAnkleLocWs().GetRotation();
		const Quat ankleDeltaRot = Normalize(anklePreIk * Conjugate(anklePostIk));
		ALWAYS_ASSERT(IsNormal(ankleDeltaRot));

		m_legIks[iLeg]->RotateAnkleWs(ankleDeltaRot);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<Point> ILegIk::GetNextPredictedFootPlant() const
{
	return MAYBE::kNothing;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ILegIk::GetPredictedFootPlants(Maybe<Point> (&pos)[kQuadLegCount]) const
{
	pos[0] = pos[1] = pos[2] = pos[3] = MAYBE::kNothing;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ILegIk::ILegIk()
{
	m_blend = 0.0f;
	m_legCount = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ILegIk::UseNewSolveTechnique() const
{
	return g_useNewIkSolve;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ILegIk::RootBaseSmoother::RootBaseSmoother(CharacterLegIkController* pController, float blend, float alignY, bool onStairs)
	: m_pController(pController)
	, m_blend(blend)
	, m_alignY(alignY)
	, m_onStairs(onStairs)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
float ILegIk::RootBaseSmoother::SmoothY(float desiredY, float maxY)
{
	if (m_pController->m_rootBaseYInited)
	{	
		if (m_onStairs)
			m_pController->m_rootBaseY += (m_alignY - m_pController->m_lastAlignY);

		const float dt = GetProcessDeltaTime();
		float desiredSpeed = 0.0f;
		if (GetProcessDeltaTime() > 0.0001f)
		{
			desiredSpeed = (m_alignY - m_pController->m_lastAlignY) / dt;
		}

		m_pController->m_rootBaseYSpeed = m_pController->m_rootYSpeedSpring.Track(m_pController->m_rootBaseYSpeed,
																				  desiredSpeed,
																				  dt,
																				  m_pController->m_rootBaseSpring>0.0f? m_pController->m_rootBaseSpring:g_rootYSpeedSpring);

		/*Seek(m_pController->m_rootBaseYSpeed,
			desiredSpeed,
			FromUPS(4.0f));*/
		//MsgCon("root base y speed: %.2f %.2f\n", m_pController->m_rootBaseYSpeed, desiredSpeed);
		float rootSpring = g_rootYSpring;
		{
			if (!m_onStairs)
				m_pController->m_rootBaseY += FromUPS(m_pController->m_rootBaseYSpeed);

			m_pController->m_rootBaseY = m_pController->m_rootYSpring.Track(m_pController->m_rootBaseY,
																			desiredY,
																			dt,
																			rootSpring);
		}
	}
	else
	{
		m_pController->m_rootBaseYSpeed = 0.0f;
		m_pController->m_lastRootBaseY = desiredY;
		m_pController->m_rootBaseY = desiredY;
		m_pController->m_rootYSpring.Reset();
		m_pController->m_rootYSpeedSpring.Reset();
		m_pController->m_rootBaseYInited = true;
	}

	m_pController->m_lastAlignY = m_alignY;

	if (g_drawIkGraphs)
	{

		GraphDisplay::Params baseParams = GraphDisplay::Params()
											  .WithGraphType(GraphDisplay::kGraphTypeLine)
											  .NoAvgLine()
											  .NoAvgText()
											  .NoMaxLine()
											  .NoMaxText()
											  .NoBackground()
											  .WithDataColor(Abgr8FromRgba(0, 0, 255, 255));
		baseParams.m_min = m_pController->m_rootBaseY - 0.5f;
		baseParams.m_max = m_pController->m_rootBaseY + 0.5f;
		baseParams.m_useMinMax = true;
		baseParams.m_dataColor = Abgr8FromRgba(0,0,255, 128);

		//float newRootDelta = pController->m_rootBaseY - baseY;
		QUICK_PLOT(leftLegPlot,
				   "root",
				   250 + 10,
				   260, // x and y coordinates to draw the plot
				   250,
				   250,		   // width and height of the plot
				   60,		   // show values from 60 frames ago up until now
				   baseParams, // provide additional customization of the plot
				   m_pController->m_rootBaseY);
		QUICK_PLOT(rightLegPlot,
				   "root ",
				   250 + 10,
				   260, // x and y coordinates to draw the plot
				   250,
				   250, // width and height of the plot
				   60,	// show values from 60 frames ago up until now
				   baseParams
					   .WithDataColor(Abgr8FromRgba(255, 0, 255, 128)), // provide additional customization of the plot
				   desiredY);
		QUICK_PLOT(outputPlot,
				   "root  ",
				   250 + 10,
				   260, // x and y coordinates to draw the plot
				   250,
				   250, // width and height of the plot
				   60,	// show values from 60 frames ago up until now
				   baseParams
					   .WithDataColor(Abgr8FromRgba(0, 255, 0, 128)), // provide additional customization of the plot
				   Lerp(desiredY, m_pController->m_rootBaseY, m_blend));
	}

	m_pController->m_lastRootBaseY = desiredY;
	return Lerp(m_alignY, m_pController->m_rootBaseY, m_blend);
}
