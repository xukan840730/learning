/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/footik.h"

#include "corelib/math/solve-triangle.h"

#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/nd-options.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-skeleton.h"

const StringId64 LegIkChain::kHumanJointIds[kQuadLegCount][kJointCount] = {
	{
		SID("root"),
		SID("l_upper_leg"),
		SID("l_knee"),
		SID("l_ankle"),
		SID("l_heel"),
		SID("l_ball"),
	},
	{
		SID("root"),
		SID("r_upper_leg"),
		SID("r_knee"),
		SID("r_ankle"),
		SID("r_heel"),
		SID("r_ball"),
	},
	{
		INVALID_STRING_ID_64,
		INVALID_STRING_ID_64,
		INVALID_STRING_ID_64,
		INVALID_STRING_ID_64,
		INVALID_STRING_ID_64,
		INVALID_STRING_ID_64,
	},
	{
		INVALID_STRING_ID_64,
		INVALID_STRING_ID_64,
		INVALID_STRING_ID_64,
		INVALID_STRING_ID_64,
		INVALID_STRING_ID_64,
		INVALID_STRING_ID_64,
	},
};

const StringId64 LegIkChain::kHorseJointIds[kQuadLegCount][kJointCount] = {
	{
		SID("root"),
		kUseAlternateHorseJoints ? SID("l_knee") : SID("l_hip"), //SID("l_hip"),
		kUseAlternateHorseJoints ? SID("l_upAnkle") : SID("l_knee"), //SID("l_knee"),
		SID("l_ankle"),
		INVALID_STRING_ID_64, //horses don't have heels
		SID("l_toeEnd"),
	},
	{
		SID("root"),
		kUseAlternateHorseJoints ? SID("r_knee") : SID("r_hip"),
		kUseAlternateHorseJoints ? SID("r_upAnkle") : SID("r_knee"),
		SID("r_ankle"),
		INVALID_STRING_ID_64, //horses don't have heels
		SID("r_toeEnd"),
	},
	{
		SID("root"),
		SID("l_shoulder"),
		SID("l_elbow"),
		SID("l_hand"),
		INVALID_STRING_ID_64, //horses don't have heels
		SID("l_fingerEnd"),
	},
	{
		SID("root"),
		SID("r_shoulder"),
		SID("r_elbow"),
		SID("r_hand"),
		INVALID_STRING_ID_64, //horses don't have heels
		SID("r_fingerEnd"),
	},
};

const StringId64 LegIkChain::kDogJointIds[kQuadLegCount][kJointCount] = {
	// TODO -- I've just copy pasted horse joint names here for now

	{
		SID("root"),
		SID("l_hip"),
		SID("l_knee"),
		SID("l_ankle"),
		INVALID_STRING_ID_64, //horses don't have heels
		SID("l_toeEnd"),
	},
	{
		SID("root"),
		SID("r_hip"),
		SID("r_knee"),
		SID("r_ankle"),
		INVALID_STRING_ID_64, //horses don't have heels
		SID("r_toeEnd"),
	},
	{
		SID("root"),
		SID("l_shoulder"),
		SID("l_elbow"),
		SID("l_hand"),
		INVALID_STRING_ID_64, //horses don't have heels
		SID("l_fingerEnd"),
	},
	{
		SID("root"),
		SID("r_shoulder"),
		SID("r_elbow"),
		SID("r_hand"),
		INVALID_STRING_ID_64, //horses don't have heels
		SID("r_fingerEnd"),
	},
};

// static
const LegIkChain::JointName2DArray* LegIkChain::GetJointIdsForCharType(FootIkCharacterType charType)
{
	switch (charType)
	{
	case kFootIkCharacterTypeHuman:
		return &kHumanJointIds;
	case kFootIkCharacterTypeHorse:
		return &kHorseJointIds;
	case kFootIkCharacterTypeDog:
		return &kDogJointIds;
	default:
		ALWAYS_HALTF(("Unknown FootIkCharacterType: %u", (U8)charType));
		return nullptr;
	}
}

LegIkChain::LegIkChain()
{
	m_type = kTypeChainLeg;
}

bool LegIkChain::Init(NdGameObject* pGo, int legIndex, bool includeRoot)
{
	//const Character* pChar = Character::FromProcess(pGo);
	//ANIM_ASSERTF(pChar, ("LegIkChain only supports Characters. Tried to init with non character gameobject %s\n", DevKitOnly_StringIdToString(pGo->GetUserId())));
	//if (!pChar)
	//	return false;

	FootIkCharacterType charType = pGo->GetFootIkCharacterType();
	ANIM_ASSERTF(legIndex >= 0, ("index: %d", legIndex));

	int legCount = GetLegCountForCharacterType(charType);
	ANIM_ASSERTF(legIndex < legCount, ("legIndex: %d legCount: %d", legIndex, legCount));

	m_legIndex = legIndex;

	const JointName2DArray* pJointNames = GetJointIdsForCharType(charType);
	ANIM_ASSERT(pJointNames);
	if (!pJointNames)
		return false;

	const JointName2DArray& jointNames = *pJointNames;
	StringId64 startJoint = jointNames[legIndex][kHip];
	StringId64 endJoint	= jointNames[legIndex][kBall];

	if (includeRoot)
		startJoint = jointNames[legIndex][kRoot];

	StringId64 endJoints[2] = { jointNames[legIndex][kHeel], jointNames[legIndex][kBall] };
	if (!JointTree::Init(pGo, startJoint, false, 2, endJoints))
		return false;

	for (int i = 0; i < kJointCount; ++i)
	{
		int offset = FindJointOffset(jointNames[legIndex][i]);
		ANIM_ASSERTF(offset < 128 && offset >= -128, ("Trying to store %d in a single byte, this won't fit!", offset));
		m_jointOffsets[i] = (I8)offset;
	}

	return true;
}

bool LegIkChain::Init(const ArtItemSkeleton* pSkel, FootIkCharacterType charType, int legIndex, bool includeRoot)
{
	m_legIndex = (I8)legIndex;

	const JointName2DArray* pJointNames = GetJointIdsForCharType(charType);
	ANIM_ASSERT(pJointNames);
	if (!pJointNames)
		return false;

	const JointName2DArray& jointNames = *pJointNames;
	StringId64 startJoint = jointNames[legIndex][kHip];
	StringId64 endJoint = jointNames[legIndex][kBall];

	if (includeRoot)
		startJoint = jointNames[legIndex][kRoot];

	StringId64 endJoints[2] = { jointNames[legIndex][kHeel], jointNames[legIndex][kBall] };
	if (!JointTree::Init(pSkel, startJoint, 2, endJoints))
		return false;

	for (int i = 0; i < kJointCount; ++i)
	{
		int offset = FindJointOffset(jointNames[legIndex][i]);
		ANIM_ASSERTF(offset < 128 && offset >= -128, ("Trying to store %d in a single byte, this won't fit!", offset));
		m_jointOffsets[i] = (I8)offset;
	}

	return true;
}

void LegIkChain::ReadJointCache()
{
	JointTree::ReadJointCache();

	const NdGameObject* pGo = GetNdGameObject();

	// Compute hip axis and hip down
	const ArtItemSkeleton* pSkel = pGo->GetAnimData()->m_curSkelHandle.ToArtItem();
	ComputeHipData(pSkel, HipJointIndex(), &m_jointData->m_hipAxis, &m_jointData->m_hipDown);
}

bool LegIkChain::ReadFromJointParams(const ndanim::JointParams* pJointParamsLs,
									 U32F indexBase,
									 U32F numJoints,
									 float rootScale,
									 const OrbisAnim::ValidBits* pValidBits)
{
	bool result = JointTree::ReadFromJointParams(pJointParamsLs, indexBase, numJoints, rootScale, pValidBits);

	if (const NdGameObject* pGo = GetNdGameObject())
	{
		// Compute hip axis and hip down
		const ArtItemSkeleton* pSkel = pGo->GetAnimData()->m_curSkelHandle.ToArtItem();
		ComputeHipData(pSkel, HipJointIndex(), &m_jointData->m_hipAxis, &m_jointData->m_hipDown);
	}
	else if (const ArtItemSkeleton* pSkel = ResourceTable::LookupSkel(m_skelId).ToArtItem())
	{
		ComputeHipData(pSkel, HipJointIndex(), &m_jointData->m_hipAxis, &m_jointData->m_hipDown);
	}
	return result;
}

float LegIkChain::GetKneeAngle()
{
	const Point anklePos = GetAnkleLocWs().GetTranslation();
	const Point kneePos = GetKneeLocWs().GetTranslation();
	const Point hipPos = GetHipLocWs().GetTranslation();
	return Acos(Dot(Normalize(anklePos - kneePos), Normalize(hipPos - kneePos)));
}

void LegIkChain::SetKneeAngle( float kneeAngle )
{
	const Point anklePos = GetAnkleLocWs().GetTranslation();
	const Point kneePos = GetKneeLocWs().GetTranslation();
	const Point hipPos = GetHipLocWs().GetTranslation();

	const Vector thigh = hipPos - kneePos;
	const Vector calf = anklePos - kneePos;

	Vector rotAxis = SafeNormalize(Cross(calf, thigh), kUnitYAxis);

	RotateKneeWs(QuatFromAxisAngle(rotAxis, GetKneeAngle() - kneeAngle));
}



LegIkInstance::LegIkInstance()
{
	memset(this, 0, sizeof(LegIkInstance));

	m_thighLimitMin = DEGREES_TO_RADIANS(-180.0f);
	m_thighLimitMax = DEGREES_TO_RADIANS(180.0f);
	m_kneeLimitMax = DEGREES_TO_RADIANS(180.0f);
	m_kneeLimitMin = DEGREES_TO_RADIANS(0.0f);
}

void SolveLegIk(LegIkInstance* ik)
{
	if (FALSE_IN_FINAL_BUILD(g_ndOptions.m_debugDisableArmFootIk || g_animOptions.m_ikOptions.m_disableAllIk))
		return;

	PROFILE(Processes, LegIK_Solve);
	//g_prim.Draw(DebugCross(ik->m_goalPosWs, 0.1f, kColorRed));

	JointSet *pJointSet = ik->m_ikChain;

	int hipOffset;
	int kneeOffset;
	int ankleOffset;
	Vector hipAxis;
	Vector hipDown;

	if (pJointSet->GetType() == JointSet::kTypeChainLeg)
	{
		LegIkChain *pChain = static_cast<LegIkChain*>(pJointSet);
		hipOffset = pChain->HipOffset();
		kneeOffset = pChain->KneeOffset();
		ankleOffset = pChain->AnkleOffset();
		hipAxis = pChain->HipAxis();
		hipDown = pChain->HipDown();
	}
	else
	{
		ANIM_ASSERT(ik->m_legIndex == kLeftLeg || ik->m_legIndex == kRightLeg);
		hipOffset = pJointSet->FindJointOffset(LegIkChain::kHumanJointIds[ik->m_legIndex][LegIkChain::kHip]);
		kneeOffset = pJointSet->FindJointOffset(LegIkChain::kHumanJointIds[ik->m_legIndex][LegIkChain::kKnee]);
		ankleOffset = pJointSet->FindJointOffset(LegIkChain::kHumanJointIds[ik->m_legIndex][LegIkChain::kAnkle]);
		ANIM_ASSERT(hipOffset >= 0 && kneeOffset >= 0 && ankleOffset >= 0);

		ComputeHipData(pJointSet->GetNdGameObject()->GetAnimData()->m_curSkelHandle.ToArtItem(), pJointSet->GetJointIndex(hipOffset), &hipAxis, &hipDown);
	}


	const Point startingPosUpperThigh	= pJointSet->GetJointLocWs(hipOffset).Pos();
	const Point startingPosKnee			= pJointSet->GetJointLocWs(kneeOffset).Pos();
	const Point startingPosAnkle		= pJointSet->GetJointLocWs(ankleOffset).Pos();

	if (FALSE_IN_FINAL_BUILD(g_animOptions.m_procedural.m_drawFootIkSourcePos))
	{
		g_prim.Draw(DebugCross(startingPosUpperThigh, 0.1f, kColorMagenta, PrimAttrib(kPrimDisableDepthTest)));
		g_prim.Draw(DebugString(startingPosUpperThigh, "ik:thigh", kColorWhite, 0.7f));
		g_prim.Draw(DebugCross(startingPosKnee, 0.1f, kColorMagenta, PrimAttrib(kPrimDisableDepthTest)));
		g_prim.Draw(DebugString(startingPosKnee, "ik:knee", kColorWhite, 0.7f));
		g_prim.Draw(DebugCross(startingPosAnkle, 0.1f, kColorMagenta, PrimAttrib(kPrimDisableDepthTest)));
		g_prim.Draw(DebugString(startingPosAnkle, "ik:ankle", kColorWhite, 0.7f));
	}

	const float debugT = Dist(startingPosUpperThigh, startingPosKnee);
	const float debugC = Dist(startingPosKnee, startingPosAnkle);

	// debug
	if (ik->m_useOld)
	{
		ik->m_goalPos = startingPosAnkle;
	}
	const Vector toGoal = ik->m_goalPos - startingPosUpperThigh;

	float l = Length(toGoal);
	const float t = debugT; // ik->m_pSetup->m_lenThigh;
	const float c = debugC; // ik->m_pSetup->m_lenCalf;

/*
	if (l > t+c || l < Abs(t - c))
	{
		g_prim.Draw( DebugSphere( ik->m_goalPos, 0.1f, kColorRed));
		g_prim.Draw(DebugCross(startingPosUpperThigh, 0.1f, kColorWhite));
		g_prim.Draw(DebugCross(startingPosKnee, 0.1f, kColorBlue));
		g_prim.Draw(DebugCross(startingPosAnkle, 0.1f, kColorOrange));
	}
*/

	l = Min(l, t + c);

	if (Dist(startingPosKnee, startingPosUpperThigh) == 0.0f)
		return;

	const Vector oldThigh = Normalize(startingPosKnee - startingPosUpperThigh);
	GAMEPLAY_ASSERT(IsFinite(oldThigh));

	const Vector oldCalf = Normalize(startingPosAnkle - startingPosKnee);
	GAMEPLAY_ASSERT(IsFinite(oldCalf));
	const Vector origPole = Cross(oldCalf, oldThigh);

	sTriangleAngles triangle = SolveTriangle(l, c, t);
	const Scalar kneeangle = Limit(triangle.a, ik->m_kneeLimitMin, ik->m_kneeLimitMax);
	const Scalar kneetop = triangle.b;

	// normalize toGoal
	const Vector normToGoal = SafeNormalize(toGoal, kZero);

	// now, we want to rotate toGoal by kneeTop radians, around poleVector <cross> toGoal
	Vector rotAxis;
	if (!ik->m_havePoleVec)
	{
		Quat hipOrient = pJointSet->GetJointLocWs(hipOffset).Rot();
		Vector fvecHip;
		fvecHip = Rotate(hipOrient, hipAxis);

		rotAxis = Cross(fvecHip, toGoal);

		F32 dotp = Abs(Dot(fvecHip, normToGoal));
		if (dotp > SCALAR_LC(0.85f))
		{
			//MsgCon("> 0.85f - %.2f\n", (F32)Abs(Dot(fvecHip, normToGoal)));
			fvecHip = LerpScale(0.85f, 0.95f, fvecHip, -oldThigh, dotp);
			rotAxis = Cross(fvecHip, toGoal);
		}
		else
		{
			//MsgCon("< 0.95f - %.2f\n", (F32)Abs(Dot(fvecHip, normToGoal)));
		}
	}
	else
	{
		Vector poleCalf = Point(ik->m_poleVectorWs.GetVec4()) - startingPosUpperThigh;
		poleCalf = Normalize(poleCalf);
		GAMEPLAY_ASSERT(IsFinite(poleCalf));
		rotAxis = Cross(toGoal, poleCalf);
	}

	rotAxis = Normalize(rotAxis);
	GAMEPLAY_ASSERT(IsFinite(rotAxis));
	Quat rotQuatUpperThigh = QuatFromAxisAngle(rotAxis, kneetop);

	const Vector newThigh = Normalize(Rotate(rotQuatUpperThigh, toGoal));
	GAMEPLAY_ASSERT(IsFinite(newThigh));
	// okay, we know this new thigh;
	// and we also know the old thigh vec (from ws knee to ws upper thigh)
	// simply get the delta

	Quat qq = QuatFromVectors(oldThigh, newThigh);

	// transform our current WS thigh position by this.
	// propagate the world space computation down to the knee
	pJointSet->RotateJointWs(hipOffset, qq);

	// also, we have to update our knee joint....
	// given our new world space position of our thigh (computed by the above), figure out it's
	// delta transform in the same way.

	float remainingAngle = PI - kneeangle;
	qq = QuatFromAxisAngle(rotAxis, -remainingAngle);
	const Vector newCalf = Normalize(Rotate(qq, newThigh));

	const Point firstStageAnkle = pJointSet->GetJointLocWs(ankleOffset).Pos();
	const Point firstStageKnee = pJointSet->GetJointLocWs(kneeOffset).Pos();
	const Vector firstStageCalf = Normalize(firstStageAnkle - firstStageKnee);

	qq = QuatFromVectors(firstStageCalf, newCalf);

	pJointSet->RotateJointWs(kneeOffset, qq);

	ik->m_outputGoalPos = pJointSet->GetJointLocWs(ankleOffset).Pos();
}



void ComputeHipData(const ArtItemSkeleton* pSkel, int hipIndex, Vector *hipAxis, Vector *hipDown)
{
	const Mat34 * thighBindPoseMat = ndanim::GetInverseBindPoseTable(pSkel->m_pAnimHierarchy) + hipIndex;

	// look at the Z component (facing)
	Vec4 baseHipAxis(0.f, 0.f, -1.f, 0.f);

	// inverse transform into hip space
	baseHipAxis = MulVectorMatrix(baseHipAxis, thighBindPoseMat->GetMat44());

	Vec4 baseHipDown(0.0f, -1.0f, 0.0f, 0.0f);

	// inverse transform into hip space
	baseHipDown = MulVectorMatrix(baseHipAxis, thighBindPoseMat->GetMat44());

	*hipAxis = Vector(baseHipAxis);
	*hipDown = Vector(baseHipDown);
}
