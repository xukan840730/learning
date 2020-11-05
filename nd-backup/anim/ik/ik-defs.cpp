/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/ik/ik-defs.h"

#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/anim-debug.h"

/// --------------------------------------------------------------------------------------------------------------- ///
// HUMAN
/// --------------------------------------------------------------------------------------------------------------- ///
static CONST_EXPR StringId64 s_lHip	  = SID("l_upper_leg");
static CONST_EXPR StringId64 s_rHip	  = SID("r_upper_leg");
static CONST_EXPR StringId64 s_lKnee  = SID("l_knee");
static CONST_EXPR StringId64 s_rKnee  = SID("r_knee");
static CONST_EXPR StringId64 s_lAnkle = SID("l_ankle");
static CONST_EXPR StringId64 s_rAnkle = SID("r_ankle");
static CONST_EXPR StringId64 s_lHeel  = SID("l_heel");
static CONST_EXPR StringId64 s_rHeel  = SID("r_heel");
static CONST_EXPR StringId64 s_lBall  = SID("l_ball");
static CONST_EXPR StringId64 s_rBall  = SID("r_ball");
static CONST_EXPR StringId64 s_lToe	  = SID("l_toe");
static CONST_EXPR StringId64 s_rToe	  = SID("r_toe");
static CONST_EXPR StringId64 s_spine  = SID("spine_d");

static CONST_EXPR StringId64 s_requiredHumanJoints[] =
{
	s_lHip,
	s_rHip,
	s_lKnee,
	s_rKnee,
	s_lAnkle,
	s_rAnkle,
	s_lHeel,
	s_rHeel,
	s_lBall,
	s_rBall
};

static CONST_EXPR StringId64 s_allHumanJoints[] = {
	s_lHip,							//kFootIkJointLeftHip = 0,
	s_rHip,							//kFootIkJointRightHip,
	s_lKnee,						//kFootIkJointLeftKnee,
	s_rKnee,						//kFootIkJointRightKnee,
	s_lAnkle,						//kFootIkJointLeftAnkle,
	s_rAnkle,						//kFootIkJointRightAnkle,
	s_lHeel,						//kFootIkJointLeftHeel,
	s_rHeel,						//kFootIkJointRightHeel,
	s_lBall,						//kFootIkJointLeftBall,
	s_rBall,						//kFootIkJointRightBall,
	s_lToe,							//kFootIkJointLeftToe,
	s_rToe,							//kFootIkJointRightToe,
									////front legs are quadruped only
	INVALID_STRING_ID_64,			//kFootIkJointFrontLeftHip,
	INVALID_STRING_ID_64,			//kFootIkJointFrontRightHip,
	INVALID_STRING_ID_64,			//kFootIkJointFrontLeftKnee,
	INVALID_STRING_ID_64,			//kFootIkJointFrontRightKnee,
	INVALID_STRING_ID_64,			//kFootIkJointFrontLeftAnkle,
	INVALID_STRING_ID_64,			//kFootIkJointFrontRightAnkle,
	INVALID_STRING_ID_64,			//kFootIkJointFrontLeftHeel,
	INVALID_STRING_ID_64,			//kFootIkJointFrontRightHeel,
	INVALID_STRING_ID_64,			//kFootIkJointFrontLeftBall,
	INVALID_STRING_ID_64,			//kJointFrontRightBall,
	INVALID_STRING_ID_64,			//kFootIkJointFrontLeftToe,
	INVALID_STRING_ID_64,			//kFootIkJointFrontRightToe,

	INVALID_STRING_ID_64,			//kFootIkJointTypeSpine
	INVALID_STRING_ID_64,			//kFootIkJointTypeNeck
	INVALID_STRING_ID_64,			//kFootIkJointTypeHead
};
STATIC_ASSERT(ARRAY_COUNT(s_allHumanJoints) == FootIkJoint::kFootIkJointCount);

/// --------------------------------------------------------------------------------------------------------------- ///
// HORSE
/// --------------------------------------------------------------------------------------------------------------- ///

// we may need to use another joint to represent the how horse wrists and ankles are more like human knees

static CONST_EXPR StringId64 s_horseBlHip = kUseAlternateHorseJoints ? SID("l_knee") : SID("l_hip");
static CONST_EXPR StringId64 s_horseBrHip = kUseAlternateHorseJoints ? SID("r_knee") : SID("r_hip");
static CONST_EXPR StringId64 s_horseBlKnee = kUseAlternateHorseJoints ? SID("l_upAnkle") : SID("l_knee");
static CONST_EXPR StringId64 s_horseBrKnee = kUseAlternateHorseJoints ? SID("r_upAnkle") : SID("r_knee");
static CONST_EXPR StringId64 s_horseBlAnkle = SID("l_ankle");
static CONST_EXPR StringId64 s_horseBrAnkle = SID("r_ankle");
// No heel for horses
// No heel for horses
static CONST_EXPR StringId64 s_horseBlBall = SID("l_toe");	// our so called toe joints are effectively our ball joints for the horse
static CONST_EXPR StringId64 s_horseBrBall = SID("r_toe");	// our so called toe joints are effectively our ball joints for the horse
// No toe for horses -- used as ball since we don't have heel
// No toe for horses -- used as ball since we don't have heel

static CONST_EXPR StringId64 s_horseFlHip = kUseAlternateHorseJoints ? SID("l_elbow") : SID("l_shoulder");
static CONST_EXPR StringId64 s_horseFrHip = kUseAlternateHorseJoints ? SID("r_elbow") : SID("r_shoulder");
static CONST_EXPR StringId64 s_horseFlKnee = kUseAlternateHorseJoints ? SID("l_wrist") : SID("l_elbow");
static CONST_EXPR StringId64 s_horseFrKnee = kUseAlternateHorseJoints ? SID("r_wrist") : SID("r_elbow");
static CONST_EXPR StringId64 s_horseFlAnkle = SID("l_hand");
static CONST_EXPR StringId64 s_horseFrAnkle = SID("r_hand");
// No heel for horses
// No heel for horses
static CONST_EXPR StringId64 s_horseFlBall = SID("l_finger"); // our so called toe joints are effectively our ball joints for the horse
static CONST_EXPR StringId64 s_horseFrBall = SID("r_finger"); // our so called toe joints are effectively our ball joints for the horse
// No toe for horses -- used as ball since we don't have heel
// No toe for horses -- used as ball since we don't have heel
static CONST_EXPR StringId64 s_horseSpine = SID("spine_e");
static CONST_EXPR StringId64 s_horseNeck = SID("neck_a");
static CONST_EXPR StringId64 s_horseHead = SID("head_end");

/// --------------------------------------------------------------------------------------------------------------- ///
static CONST_EXPR StringId64 s_requiredHorseJoints[] =
{
	s_horseBlHip,
	s_horseBrHip,
	s_horseBlKnee,
	s_horseBrKnee,
	s_horseBlAnkle,
	s_horseBrAnkle,
	s_horseBlBall,
	s_horseBrBall,
	s_horseFlHip,
	s_horseFrHip,
	s_horseFlKnee,
	s_horseFrKnee,
	s_horseFlAnkle,
	s_horseFrAnkle,
	s_horseFlBall,
	s_horseFrBall,
	s_horseSpine,
	s_horseNeck,
	s_horseHead,
};

/// --------------------------------------------------------------------------------------------------------------- ///
static CONST_EXPR StringId64 s_allHorseJoints[] =
{
	s_horseBlHip,			//kFootIkJointLeftHip = 0,
	s_horseBrHip,			//kFootIkJointRightHip,
	s_horseBlKnee,			//kFootIkJointLeftKnee,
	s_horseBrKnee,			//kFootIkJointRightKnee,
	s_horseBlAnkle,			//kFootIkJointLeftAnkle,
	s_horseBrAnkle,			//kFootIkJointRightAnkle,
	INVALID_STRING_ID_64,	//kFootIkJointLeftHeel,
	INVALID_STRING_ID_64,	//kFootIkJointRightHeel,
	s_horseBlBall,			//kFootIkJointLeftBall,
	s_horseBrBall,			//kFootIkJointRightBall,
	INVALID_STRING_ID_64,	//kFootIkJointLeftToe,
	INVALID_STRING_ID_64,	//kFootIkJointRightToe,
							////front legs are quadruped only
	s_horseFlHip,			//kFootIkJointFrontLeftHip,
	s_horseFrHip,			//kFootIkJointFrontRightHip,
	s_horseFlKnee,			//kFootIkJointFrontLeftKnee,
	s_horseFrKnee,			//kFootIkJointFrontRightKnee,
	s_horseFlAnkle,			//kFootIkJointFrontLeftAnkle,
	s_horseFrAnkle,			//kFootIkJointFrontRightAnkle,
	INVALID_STRING_ID_64,	//kFootIkJointFrontLeftHeel,
	INVALID_STRING_ID_64,	//kFootIkJointFrontRightHeel,
	s_horseFlBall,			//kFootIkJointFrontLeftBall,
	s_horseFrBall,			//kJointFrontRightBall,
	INVALID_STRING_ID_64,	//kFootIkJointFrontLeftToe,
	INVALID_STRING_ID_64,	//kFootIkJointFrontRightToe,

	s_horseSpine,			//kFootIkJointTypeSpine
	s_horseNeck,			//kFootIkJointTypeNeck
	s_horseHead,			//kFootIkJointTypeHead
};
STATIC_ASSERT(ARRAY_COUNT(s_allHorseJoints) == FootIkJoint::kFootIkJointCount);


/// --------------------------------------------------------------------------------------------------------------- ///
// DOG
/// --------------------------------------------------------------------------------------------------------------- ///
static CONST_EXPR StringId64 s_dogBlHip = kUseAlternateDogJoints ? SID("l_knee") : SID("l_hip");
static CONST_EXPR StringId64 s_dogBrHip = kUseAlternateDogJoints ? SID("r_knee") : SID("r_hip");
static CONST_EXPR StringId64 s_dogBlKnee = kUseAlternateDogJoints ? SID("l_upAnkle") : SID("l_knee");
static CONST_EXPR StringId64 s_dogBrKnee = kUseAlternateDogJoints ? SID("r_upAnkle") : SID("r_knee");
static CONST_EXPR StringId64 s_dogBlAnkle = SID("l_ankle");
static CONST_EXPR StringId64 s_dogBrAnkle = SID("r_ankle");
// No heel for dogs
// No heel for dogs
static CONST_EXPR StringId64 s_dogBlBall = SID("l_toe"); // our so called toe joints are effectively our ball joints for dogs
static CONST_EXPR StringId64 s_dogBrBall = SID("r_toe"); // our so called toe joints are effectively our ball joints for dogs
// No toe for dogs -- used as ball since we don't have heel
// No toe for dogs -- used as ball since we don't have heel

static CONST_EXPR StringId64 s_dogFlHip = kUseAlternateDogJoints ? SID("l_elbow") : SID("l_shoulder");
static CONST_EXPR StringId64 s_dogFrHip = kUseAlternateDogJoints ? SID("r_elbow") : SID("r_shoulder");
static CONST_EXPR StringId64 s_dogFlKnee = kUseAlternateDogJoints ? SID("l_wrist") : SID("l_elbow");
static CONST_EXPR StringId64 s_dogFrKnee = kUseAlternateDogJoints ? SID("r_wrist") : SID("r_elbow");
static CONST_EXPR StringId64 s_dogFlAnkle = SID("l_hand");
static CONST_EXPR StringId64 s_dogFrAnkle = SID("r_hand");
// No heel for dogs
// No heel for dogs
static CONST_EXPR StringId64 s_dogFlBall = SID("l_finger");
static CONST_EXPR StringId64 s_dogFrBall = SID("r_finger");
// No toe for dogs -- used as ball since we don't have heel
// No toe for dogs -- used as ball since we don't have heel
static CONST_EXPR StringId64 s_dogSpine = SID("spine_e");
static CONST_EXPR StringId64 s_dogNeck = SID("neck_a");
static CONST_EXPR StringId64 s_dogHead = SID("jaw"); // could be SID("head")

/// --------------------------------------------------------------------------------------------------------------- ///
static CONST_EXPR StringId64 s_requiredDogJoints[] =
{
	s_dogBlHip,
	s_dogBrHip,
	s_dogBlKnee,
	s_dogBrKnee,
	s_dogBlAnkle,
	s_dogBrAnkle,
	s_dogBlBall,
	s_dogBrBall,
	s_dogFlHip,
	s_dogFrHip,
	s_dogFlKnee,
	s_dogFrKnee,
	s_dogFlAnkle,
	s_dogFrAnkle,
	s_dogFlBall,
	s_dogFrBall,
	s_dogSpine,
	s_dogNeck,
	s_dogHead,
};

/// --------------------------------------------------------------------------------------------------------------- ///
static CONST_EXPR StringId64 s_allDogJoints[] =
{
	s_dogBlHip,			//kFootIkJointLeftHip = 0,
	s_dogBrHip,			//kFootIkJointRightHip,
	s_dogBlKnee,			//kFootIkJointLeftKnee,
	s_dogBrKnee,			//kFootIkJointRightKnee,
	s_dogBlAnkle,			//kFootIkJointLeftAnkle,
	s_dogBrAnkle,			//kFootIkJointRightAnkle,
	INVALID_STRING_ID_64,	//kFootIkJointLeftHeel,
	INVALID_STRING_ID_64,	//kFootIkJointRightHeel,
	s_dogBlBall,			//kFootIkJointLeftBall,
	s_dogBrBall,			//kFootIkJointRightBall,
	INVALID_STRING_ID_64,	//kFootIkJointLeftToe,
	INVALID_STRING_ID_64,	//kFootIkJointRightToe,
							////front legs are quadruped only
	s_dogFlHip,			//kFootIkJointFrontLeftHip,
	s_dogFrHip,			//kFootIkJointFrontRightHip,
	s_dogFlKnee,			//kFootIkJointFrontLeftKnee,
	s_dogFrKnee,			//kFootIkJointFrontRightKnee,
	s_dogFlAnkle,			//kFootIkJointFrontLeftAnkle,
	s_dogFrAnkle,			//kFootIkJointFrontRightAnkle,
	INVALID_STRING_ID_64,	//kFootIkJointFrontLeftHeel,
	INVALID_STRING_ID_64,	//kFootIkJointFrontRightHeel,
	s_dogFlBall,			//kFootIkJointFrontLeftBall,
	s_dogFrBall,			//kJointFrontRightBall,
	INVALID_STRING_ID_64,	//kFootIkJointFrontLeftToe,
	INVALID_STRING_ID_64,	//kFootIkJointFrontRightToe,

	s_dogSpine,			//kFootIkJointTypeSpine
	s_dogNeck,			//kFootIkJointTypeNeck
	s_dogHead,			//kFootIkJointTypeHead
};
STATIC_ASSERT(ARRAY_COUNT(s_allDogJoints) == FootIkJoint::kFootIkJointCount);

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 GetJointName(FootIkCharacterType charType, FootIkJoint joint)
{
	ANIM_ASSERT(joint < FootIkJoint::kFootIkJointCount);

	switch (charType)
	{
	case kFootIkCharacterTypeHuman:
		return s_allHumanJoints[joint];
	case kFootIkCharacterTypeHorse:
		return s_allHorseJoints[joint];
	case kFootIkCharacterTypeDog:
		return s_allDogJoints[joint];
	}
	ALWAYS_HALTF(("Unknown FootIkCharacterType %d", (int)charType));
	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 GetAnkleChannelName(FootIkCharacterType charType, int legIndex)
{
	ANIM_ASSERT(legIndex >= 0 && legIndex < GetLegCountForCharacterType(charType));

	switch (charType)
	{
	case kFootIkCharacterTypeHuman:
		{
			switch (legIndex)
			{
			case kBackLeftLeg:
				return SID("lAnkle");
			case kBackRightLeg:
				return SID("rAnkle");
			}
		}
	case kFootIkCharacterTypeHorse:
		{
			switch (legIndex)
			{
			case kBackLeftLeg:
				return SID("lAnkle");
			case kBackRightLeg:
				return SID("rAnkle");
			case kFrontLeftLeg:
				return SID("flAnkle");
			case kFrontRightLeg:
				return SID("frAnkle");
			}
		}
	case kFootIkCharacterTypeDog:
		{
			switch (legIndex)
			{
			case kBackLeftLeg:
				return SID("lAnkle");
			case kBackRightLeg:
				return SID("rAnkle");
			case kFrontLeftLeg:
				return SID("flAnkle");
			case kFrontRightLeg:
				return SID("frAnkle");
			}
		}
	}

	ALWAYS_HALTF(("invalid leg index %d for character type %d", legIndex, charType));
	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
FootIkJoint JointFromLegAndJointType(int legIndex, FootIkJointType jointType)
{
	switch (jointType)
	{
	case kJointTypeHeel:
		{
			switch (legIndex)
			{
			case kBackLeftLeg:
				return kFootIkJointLeftHeel;
			case kBackRightLeg:
				return kFootIkJointRightHeel;
			case kFrontLeftLeg:
				return kFootIkJointFrontLeftHeel;
			case kFrontRightLeg:
				return kFootIkJointFrontRightHeel;
			default:
				ANIM_ASSERTF(false, ("Invalid leg index %d", legIndex));
				return kFootIkJointCount;
			}
		}
		break;

	case kJointTypeToe:
		{
			switch (legIndex)
			{
			case kBackLeftLeg:
				return kFootIkJointLeftToe;
			case kBackRightLeg:
				return kFootIkJointRightToe;
			case kFrontLeftLeg:
				return kFootIkJointFrontLeftToe;
			case kFrontRightLeg:
				return kFootIkJointFrontRightToe;
			default:
				ANIM_ASSERTF(false, ("Invalid leg index %d", legIndex));
				return kFootIkJointCount;
			}
		}
		break;

	case kJointTypeBall:
		{
			switch (legIndex)
			{
			case kBackLeftLeg:
				return kFootIkJointLeftBall;
			case kBackRightLeg:
				return kFootIkJointRightBall;
			case kFrontLeftLeg:
				return kFootIkJointFrontLeftBall;
			case kFrontRightLeg:
				return kFootIkJointFrontRightBall;
			default:
				ANIM_ASSERTF(false, ("Invalid leg index %d", legIndex));
				return kFootIkJointCount;
			}
		}
		break;

	case kJointTypeAnkle:
		{
			switch (legIndex)
			{
			case kBackLeftLeg:
				return kFootIkJointLeftAnkle;
			case kBackRightLeg:
				return kFootIkJointRightAnkle;
			case kFrontLeftLeg:
				return kFootIkJointFrontLeftAnkle;
			case kFrontRightLeg:
				return kFootIkJointFrontRightAnkle;
			default:
				ANIM_ASSERTF(false, ("Invalid leg index %d", legIndex));
				return kFootIkJointCount;
			}
		}

	case kJointTypeKnee:
		{
			switch (legIndex)
			{
			case kBackLeftLeg:
				return kFootIkJointLeftKnee;
			case kBackRightLeg:
				return kFootIkJointRightKnee;
			case kFrontLeftLeg:
				return kFootIkJointFrontLeftKnee;
			case kFrontRightLeg:
				return kFootIkJointFrontRightKnee;
			default:
				ANIM_ASSERTF(false, ("Invalid leg index %d", legIndex));
				return kFootIkJointCount;
			}
		}
		break;

	case kJointTypeHip:
		{
			switch (legIndex)
			{
			case kBackLeftLeg:
				return kFootIkJointLeftHip;
			case kBackRightLeg:
				return kFootIkJointRightHip;
			case kFrontLeftLeg:
				return kFootIkJointFrontLeftHip;
			case kFrontRightLeg:
				return kFootIkJointFrontRightHip;
			default:
				ANIM_ASSERTF(false, ("Invalid leg index %d", legIndex));
				return kFootIkJointCount;
			}
		}
		break;
	case kJointTypeSpine:
		return kFootIkJointTypeSpine;
	case kJointTypeNeck:
		return kFootIkJointTypeNeck;
	case kJointTypeHead:
		return kFootIkJointTypeHead;
	default:
		ANIM_ASSERTF(false, ("Invalid joint type %d", (int)jointType));
		return kFootIkJointCount;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const StringId64* GetRequiredJointsForCharacterType(FootIkCharacterType charType, U32F* pOutRequiredJointCount)
{
	ANIM_ASSERT(pOutRequiredJointCount);
	switch (charType)
	{
	case kFootIkCharacterTypeHuman:
		*pOutRequiredJointCount = ARRAY_COUNT(s_requiredHumanJoints);
		return s_requiredHumanJoints;
	case kFootIkCharacterTypeHorse:
		*pOutRequiredJointCount = ARRAY_COUNT(s_requiredHorseJoints);
		return s_requiredHorseJoints;
	case kFootIkCharacterTypeDog:
		*pOutRequiredJointCount = ARRAY_COUNT(s_requiredDogJoints);
		return s_requiredDogJoints;
	}
	ALWAYS_HALTF(("Unknown FootIkCharacterType %d", (int)charType));
	*pOutRequiredJointCount = 0;
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* LegIndexToString(int legIndex)
{
	switch (legIndex)
	{
	case kBackLeftLeg:
		return "left-foot";
	case kBackRightLeg:
		return "right-foot";
	case kFrontLeftLeg:
		return "front-left-foot";
	case kFrontRightLeg:
		return "front-right-foot";
	default:
		ANIM_ASSERTF(false, ("unknown leg index %d!", legIndex));
		return "???";
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DrawLeg(JointSet* pJoints, int legIndex, FootIkCharacterType charType, Color c)
{
	STRIP_IN_FINAL_BUILD;
	StringId64 lJoints[] =
	{
		GetJointName(charType, kFootIkJointLeftHip),
		GetJointName(charType, kFootIkJointLeftKnee),
		GetJointName(charType, kFootIkJointLeftAnkle),
		GetJointName(charType, kFootIkJointLeftBall),
	};
	StringId64 rJoints[] =
	{
		GetJointName(charType, kFootIkJointRightHip),
		GetJointName(charType, kFootIkJointRightKnee),
		GetJointName(charType, kFootIkJointRightAnkle),
		GetJointName(charType, kFootIkJointRightBall),
	};
	StringId64 flJoints[] =
	{
		GetJointName(charType, kFootIkJointFrontLeftHip),
		GetJointName(charType, kFootIkJointFrontLeftKnee),
		GetJointName(charType, kFootIkJointFrontLeftAnkle),
		GetJointName(charType, kFootIkJointFrontLeftBall),
	};
	StringId64 frJoints[] =
	{
		GetJointName(charType, kFootIkJointFrontRightHip),
		GetJointName(charType, kFootIkJointFrontRightKnee),
		GetJointName(charType, kFootIkJointFrontRightAnkle),
		GetJointName(charType, kFootIkJointFrontRightBall),
	};
	StringId64* allJoints[] =
	{
		lJoints,
		rJoints,
		flJoints,
		frJoints
	};
	const int numJoints = ARRAY_COUNT(lJoints);
	StringId64* paJoints = allJoints[legIndex];

	for (int i = 1; i < numJoints; ++i)
	{
		Point pos = pJoints->GetJointLocWs(pJoints->FindJointOffset(paJoints[i])).GetTranslation();
		Point pos1 = pJoints->GetJointLocWs(pJoints->FindJointOffset(paJoints[i - 1])).GetTranslation();
		g_prim.Draw(DebugLine(pos, pos1, c, c, 2.0f, PrimAttrib(kPrimDisableDepthTest)), kPrimDuration1FramePauseable);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 GetHandIkChannelNameId(I32F iArm)
{
	switch (iArm)
	{
	case kLeftArm:
		return SID("lWrist");
	case kRightArm:
		return SID("rWrist");
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* GetHandIkChannelName(I32F iArm)
{
	switch (iArm)
	{
	case kLeftArm:
		return "lWrist";
	case kRightArm:
		return "rWrist";
	}

	return "<unknown>";
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 GetHandIkJointNameId(I32F iArm)
{
	switch (iArm)
	{
	case kLeftArm:
		return SID("l_wrist");
	case kRightArm:
		return SID("r_wrist");
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* GetHandIkJointName(I32F iArm)
{
	switch (iArm)
	{
	case kLeftArm:
		return "l_wrist";
	case kRightArm:
		return "r_wrist";
	}

	return "<unknown>";
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* ArmIndexToString(int armIndex)
{
	switch (armIndex)
	{
	case kLeftArm:
		return "kLeftArm";
	case kRightArm:
		return "kRightArm";
	}

	return "<unknown>";
}
