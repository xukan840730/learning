/*
 * Copyright (c) 2018 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/leg-fix-ik/leg-fix-ik-plugin.h"

#include "corelib/math/solve-triangle.h"

#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/ik/ik-chain.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/render/util/prim.h"

/// --------------------------------------------------------------------------------------------------------------- ///
LegFixIkPluginCallbackArg::LegFixIkPluginCallbackArg()
{
	m_magic[0] = 0;
	m_blend[0] = m_blend[1] = 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void LegFixIkPluginCallbackArg::SetMagic()
{
	memcpy(m_magic, "LEGFIXIK", 8);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool LegFixIkPluginCallbackArg::CheckMagic() const
{
	return memcmp(m_magic, "LEGFIXIK", 8) == 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void DebugDrawIkChain(const Transform& objXform, IkChain& ikChain, Color color)
{
	for (int iJoint = 0; iJoint < ikChain.m_pChainSetup->m_numIndices - 1; ++iJoint)
	{
		g_prim.Draw(DebugLine(ikChain.GetObjectSpaceLocator(iJoint).GetTranslation() * objXform,
							  ikChain.GetObjectSpaceLocator(iJoint + 1).GetTranslation() * objXform,
							  kColorWhite, color, 1.0f, PrimAttrib(kPrimDisableDepthTest)),
					kPrimDuration1FramePauseable);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SolveLegIK(const Transform& objXform,
				IkChain& ikChain,
				const LegIkJoints* pLegJoints,
				Point_arg goalPos,
				Scalar_arg kneeLimitMin,
				Scalar_arg kneeLimitMax,
				bool debugDraw)
{
	const Locator startingLocUpperThigh = ikChain.GetObjectSpaceLocator(pLegJoints->m_upperThighJoint);
	const Locator startingLocKnee		= ikChain.GetObjectSpaceLocator(pLegJoints->m_kneeJoint);
	const Locator startingLocAnkle		= ikChain.GetObjectSpaceLocator(pLegJoints->m_ankleJoint);

	const Point startingPosUpperThigh = startingLocUpperThigh.Pos();
	const Point startingPosKnee		  = startingLocKnee.Pos();
	const Point startingPosAnkle	  = startingLocAnkle.Pos();

	const Scalar t = Dist(startingPosUpperThigh, startingPosKnee);
	const Scalar c = Dist(startingPosKnee, startingPosAnkle);

	const Vector toGoal = goalPos - startingPosUpperThigh;

	const Scalar l = Min(Length(toGoal), t + c);

	const Vector oldThigh = Normalize(startingPosKnee - startingPosUpperThigh);
	const Vector oldCalf  = Normalize(startingPosAnkle - startingPosKnee);

	const Vector hipForward			= -startingLocUpperThigh.TransformVector(pLegJoints->m_hipAxis);
	const Vector normOrigHipToAnkle = SafeNormalize(startingPosAnkle - startingPosUpperThigh, kZero);

	// either cross might degenerate, but not both
	const Scalar dotP = Abs(Dot(hipForward, normOrigHipToAnkle));
	Vector origPole(kZero);
	if (pLegJoints->m_bReverseKnee ^ dotP < SCALAR_LC(0.9f))
	{
		origPole = Normalize(Cross(normOrigHipToAnkle, hipForward));
	}
	else
	{
		origPole = Normalize(Cross(oldCalf, oldThigh));
	}

	sTriangleAngles triangle = SolveTriangle(l, c, t);
	const float kneeangle	 = Limit(triangle.a, kneeLimitMin, kneeLimitMax);
	const float kneetop		 = triangle.b;

	// normalize toGoal
	const Vector normToGoal = SafeNormalize(toGoal, kZero);

	if (Length(origPole) > kSmallestFloat)
	{
		// Set the knee angle
		const float startKneeAngle = SafeAcos(Dot(-oldThigh, oldCalf));
		const Quat kneeRotor	   = QuatFromAxisAngle(origPole, kneeangle - startKneeAngle);
		ikChain.RotateJointOS(pLegJoints->m_kneeJoint, kneeRotor);

		// Now orient the hip joint so the ankle is in the correct pos
		const Point firstStageAnkle = ikChain.GetObjectSpaceLocator(pLegJoints->m_ankleJoint).Pos();

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			const Point firstStageHip  = ikChain.GetObjectSpaceLocator(pLegJoints->m_upperThighJoint).Pos();
			const Point firstStageKnee = ikChain.GetObjectSpaceLocator(pLegJoints->m_kneeJoint).Pos();
			const float d = Dist(firstStageHip, firstStageAnkle);
			// g_prim.Draw(DebugString(firstStageKnee, StringBuilder<128>("%0.3f", d).c_str(), kColorYellow, 0.5f), kPrimDuration1FramePauseable);

			g_prim.Draw(DebugString(goalPos * objXform,
									StringBuilder<128>("%0.3f (%0.4f)", float(l), Abs(float(l) - d)).c_str(),
									kColorWhite,
									0.5f),
						kPrimDuration1FramePauseable);

			const Vector v1 = SafeNormalize(firstStageHip - firstStageKnee, kZero);
			const Vector v2 = SafeNormalize(firstStageAnkle - firstStageKnee, kZero);
			const float appliedAngleDeg	   = RADIANS_TO_DEGREES(float(kneeangle - startKneeAngle));
			const float actualKneeAngleDeg = RADIANS_TO_DEGREES(SafeAcos(Dot(v1, v2)));
			const float startKneeAngleDeg  = RADIANS_TO_DEGREES(startKneeAngle);
			const float desAngleDeg		   = RADIANS_TO_DEGREES(kneeangle);
			g_prim.Draw(DebugString(firstStageKnee * objXform,
									StringBuilder<128>("start: %0.1fdeg want: %0.1fdeg got: %0.1fdeg (err: %0.2fdeg)",
													   startKneeAngleDeg,
													   desAngleDeg,
													   actualKneeAngleDeg,
													   Abs(actualKneeAngleDeg - desAngleDeg))
										.c_str(),
									kColorYellow,
									0.5f),
						kPrimDuration1FramePauseable);

			// g_prim.Draw(DebugCross(firstStageAnkle, 0.05f, kColorMagenta), kPrimDuration1FramePauseable);
			DebugDrawIkChain(objXform, ikChain, kColorYellow);
		}

		const Vector toAnkleFirstStage = firstStageAnkle - startingPosUpperThigh;
		Quat thighRotor = QuatFromVectors(Normalize(toAnkleFirstStage), normToGoal);
		ikChain.RotateJointOS(pLegJoints->m_upperThighJoint, thighRotor);
	}

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		g_prim.Draw(DebugCross(goalPos * objXform, 0.05, kColorRed, PrimAttrib(kPrimDisableDepthTest)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugArrow(startingPosKnee * objXform, origPole * objXform, kColorWhite, 0.2f, PrimAttrib(kPrimDisableDepthTest)), kPrimDuration1FramePauseable);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void FixKneeRotation(const Transform& objXform, IkChain& ikChain, IkChain& referencePose, const LegIkJoints* pLegSetup)
{
	Point hipPoint	 = ikChain.GetObjectSpaceLocator(pLegSetup->m_upperThighJoint).GetTranslation();
	Point anklePoint = ikChain.GetObjectSpaceLocator(pLegSetup->m_ankleJoint).GetTranslation();

	Vector hipToAnkle = SafeNormalize(anklePoint - hipPoint, kUnitYAxis);
	Vector kneeRight  = GetLocalX(ikChain.GetObjectSpaceLocator(pLegSetup->m_kneeJoint).GetRotation());
	float initialDot  = Dot(hipToAnkle, kneeRight);
	kneeRight		  = SafeNormalize(kneeRight - hipToAnkle * Dot(hipToAnkle, kneeRight), kUnitZAxis);

	Vector refKneeRight = GetLocalX(referencePose.GetObjectSpaceLocator(pLegSetup->m_kneeJoint).GetRotation());
	float initialDotRef = Dot(hipToAnkle, refKneeRight);
	refKneeRight		= SafeNormalize(refKneeRight - hipToAnkle * Dot(hipToAnkle, refKneeRight), kUnitZAxis);

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_debugDrawLegFixIk))
	{
		DebugDrawIkChain(objXform, ikChain, kColorYellow);
		g_prim.Draw(DebugLine(hipPoint * objXform, hipToAnkle * objXform, kColorWhite, 2.0f, PrimAttrib(kPrimDisableDepthTest)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(hipPoint * objXform, kneeRight * objXform, kColorOrange, 2.0f, PrimAttrib(kPrimDisableDepthTest)), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(hipPoint * objXform, refKneeRight * objXform, kColorCyan, 2.0f, PrimAttrib(kPrimDisableDepthTest)), kPrimDuration1FramePauseable);
	}

	Quat rotor = QuatFromVectors(kneeRight, refKneeRight);
	rotor	   = Slerp(kIdentity, rotor, 0.6f);
	ikChain.RotateJointOS(pLegSetup->m_upperThighJoint, rotor);

	Point anklePointPostFix = ikChain.GetObjectSpaceLocator(pLegSetup->m_ankleJoint).GetTranslation();
}

/// --------------------------------------------------------------------------------------------------------------- ///
OrbisAnim::Status LegFixIkPluginCallback(const LegFixIkPluginParams* pParams)
{
	ndanim::JointParams* pJointParamsLs = pParams->m_pJointParamsLs;
	const ndanim::JointParams* pJointParamsPreAdditiveLs = pParams->m_pJointParamsPreAdditiveLs;

	IkChain legIkChains[2] = { IkChain(pParams->m_apLegIkChainSetup[0], pJointParamsLs),
							   IkChain(pParams->m_apLegIkChainSetup[1], pJointParamsLs) };
	IkChain preAdditiveLegIkChains[2] = { IkChain(pParams->m_apLegIkChainSetup[0], pJointParamsPreAdditiveLs),
										  IkChain(pParams->m_apLegIkChainSetup[1], pJointParamsPreAdditiveLs) };

	// DebugPrintf("blended root pos: %f %f %f\n", (float)pJointParamsLs[0].m_trans.X(), (float)pJointParamsLs[0].m_trans.Y(), (float)pJointParamsLs[0].m_trans.Z());
	// DebugPrintf("base root pos: %f %f %f\n", (float)pJointParamsPreAdditiveLs[0].m_trans.X(), (float)pJointParamsPreAdditiveLs[0].m_trans.Y(), (float)pJointParamsPreAdditiveLs[0].m_trans.Z());

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_debugDrawLegFixIk))
	{
		for (int iLeg = 0; iLeg < 2; ++iLeg)
		{
			DebugDrawIkChain(pParams->m_objXform, legIkChains[iLeg], kColorRed);
			DebugDrawIkChain(pParams->m_objXform, preAdditiveLegIkChains[iLeg], kColorBlue);
		}
	}

	for (int iLeg = 0; iLeg < 2; ++iLeg)
	{
		const Point refHip = preAdditiveLegIkChains[iLeg]
								 .GetObjectSpaceLocator(pParams->m_apLegJoints[iLeg]->m_upperThighJoint)
								 .Pos();
		const Point refKnee = preAdditiveLegIkChains[iLeg]
								  .GetObjectSpaceLocator(pParams->m_apLegJoints[iLeg]->m_kneeJoint)
								  .Pos();
		const Point refAnkle = preAdditiveLegIkChains[iLeg]
								   .GetObjectSpaceLocator(pParams->m_apLegJoints[iLeg]->m_ankleJoint)
								   .Pos();

		const Vector v1 = SafeNormalize(refHip - refKnee, kZero);
		const Vector v2 = SafeNormalize(refAnkle - refKnee, kZero);
		const float refKneeAngle = Max(SafeAcos(Dot(v1, v2)), DEGREES_TO_RADIANS(165.0f));

		Locator desiredAnkleLoc = preAdditiveLegIkChains[iLeg]
									  .GetObjectSpaceLocator(pParams->m_apLegJoints[iLeg]->m_ankleJoint);
		SolveLegIK(pParams->m_objXform,
				   legIkChains[iLeg],
				   pParams->m_apLegJoints[iLeg],
				   desiredAnkleLoc.GetTranslation(),
				   0.0f,
				   refKneeAngle,
				   g_animOptions.m_procedural.m_debugDrawLegFixIk);

		// Fix up the knee rotation
		FixKneeRotation(pParams->m_objXform, legIkChains[iLeg], preAdditiveLegIkChains[iLeg], pParams->m_apLegJoints[iLeg]);
		Quat ankleDeltaRotWS = desiredAnkleLoc.GetRotation()
							   * Conjugate(legIkChains[iLeg]
											   .GetObjectSpaceLocator(pParams->m_apLegJoints[iLeg]->m_ankleJoint)
											   .GetRotation());
		legIkChains[iLeg].RotateJointOS(pParams->m_apLegJoints[iLeg]->m_ankleJoint, ankleDeltaRotWS);
	}

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_debugDrawLegFixIk))
	{
		for (int iLeg = 0; iLeg < 2; ++iLeg)
		{
			DebugDrawIkChain(pParams->m_objXform, legIkChains[iLeg], kColorGreen);
		}
	}

	for (int iLeg = 0; iLeg < 2; ++iLeg)
	{
		float blend = 1.0f;
		if (pParams && pParams->m_pArg)
			blend = pParams->m_pArg->m_blend[iLeg];

		legIkChains[iLeg].OutputJoints(pJointParamsLs, blend);
	}

	return OrbisAnim::kSuccess;
}
