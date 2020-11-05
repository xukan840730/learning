/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

class JointSet;

/// --------------------------------------------------------------------------------------------------------------- ///
enum LimbIndex
{
	kLimbArmL,
	kLimbArmR,
	kLimbLegL,
	kLimbLegR,
	kLimbCount
};

/// --------------------------------------------------------------------------------------------------------------- ///
enum ArmIndex
{
	kLeftArm = 0,
	kRightArm,
	kArmCount
};

/// --------------------------------------------------------------------------------------------------------------- ///
enum LegIndex
{
	kLeftLeg = 0,
	kRightLeg,
	kLegCount,
};

/// --------------------------------------------------------------------------------------------------------------- ///
enum FootIkCharacterType : U8
{
	kFootIkCharacterTypeHuman = 0,
	kFootIkCharacterTypeHorse,
	kFootIkCharacterTypeDog,
	kFootIkCharacterTypeCount
};

/// --------------------------------------------------------------------------------------------------------------- ///
// four-footed animals
enum QuadrupedLegIndex
{
	kBackLeftLeg = 0,
	kBackRightLeg,
	kFrontLeftLeg,
	kFrontRightLeg,
	kQuadLegCount
};

STATIC_ASSERT((int)QuadrupedLegIndex::kBackLeftLeg == (int)LegIndex::kLeftLeg
			  && (int)QuadrupedLegIndex::kBackRightLeg == (int)LegIndex::kRightLeg);

/// --------------------------------------------------------------------------------------------------------------- ///
enum LegPairIndex
{
	kLegPairBackLegs = 0,
	kLegPairFrontLegs,
	kLegPairCount
};

/// --------------------------------------------------------------------------------------------------------------- ///
enum FootIkJoint
{
	// these are the back leg joints for quadrupeds
	kFootIkJointLeftHip = 0,
	kFootIkJointRightHip,
	kFootIkJointLeftKnee,
	kFootIkJointRightKnee,
	kFootIkJointLeftAnkle,
	kFootIkJointRightAnkle,
	kFootIkJointLeftHeel,
	kFootIkJointRightHeel,
	kFootIkJointLeftBall,
	kFootIkJointRightBall,
	kFootIkJointLeftToe,
	kFootIkJointRightToe,

	// front legs are quadruped only
	kFootIkJointFrontLeftHip,
	kFootIkJointFrontRightHip,
	kFootIkJointFrontLeftKnee,
	kFootIkJointFrontRightKnee,
	kFootIkJointFrontLeftAnkle,
	kFootIkJointFrontRightAnkle,
	kFootIkJointFrontLeftHeel,
	kFootIkJointFrontRightHeel,
	kFootIkJointFrontLeftBall,
	kFootIkJointFrontRightBall,
	kFootIkJointFrontLeftToe,
	kFootIkJointFrontRightToe,

	kFootIkJointTypeSpine, // used for quadruped ik
	kFootIkJointTypeNeck,  // used for quadruped ik
	kFootIkJointTypeHead,  // used for quadruped ik

	kFootIkJointCount
};

/// --------------------------------------------------------------------------------------------------------------- ///
enum FootIkJointType
{
	kJointTypeHeel = 0,
	kJointTypeToe,
	kJointTypeBall,
	kJointTypeAnkle,
	kJointTypeKnee,
	kJointTypeHip,
	kJointTypeSpine,
	kJointTypeNeck,
	kJointTypeHead,
};

/// --------------------------------------------------------------------------------------------------------------- ///
CONST_EXPR bool kUseAlternateHorseJoints = true;
CONST_EXPR bool kUseAlternateDogJoints = true;

CONST_EXPR inline bool IsQuadruped(FootIkCharacterType charType)
{
	return charType != kFootIkCharacterTypeHuman;
}
CONST_EXPR inline int GetLegCountForCharacterType(FootIkCharacterType charType)
{
	return IsQuadruped(charType) ? kQuadLegCount : kLegCount;
}

CONST_EXPR inline bool ShouldUseBallForLegIkJoint(FootIkCharacterType charType)
{
	return charType == kFootIkCharacterTypeHorse || charType == kFootIkCharacterTypeDog;
}

/// --------------------------------------------------------------------------------------------------------------- ///
FootIkJoint JointFromLegAndJointType(int legIndex, FootIkJointType jointType);
StringId64 GetJointName(FootIkCharacterType charType, FootIkJoint joint);
StringId64 GetAnkleChannelName(FootIkCharacterType charType, int legIndex);
const StringId64* GetRequiredJointsForCharacterType(FootIkCharacterType charType, U32F* pOutRequiredJointCount);
const char* LegIndexToString(int legIndex);
void DrawLeg(JointSet* pJoints, int legIndex, FootIkCharacterType charType, Color c);

/// --------------------------------------------------------------------------------------------------------------- ///
// get the matching leg in the pair AKA given left leg index returns right leg index for a given leg pair
CONST_EXPR inline int GetMatchingLegIndex(int iLeg)
{
	return (iLeg / kLegCount) + (1 - (iLeg % kLegCount));
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 GetHandIkChannelNameId(I32F iArm);
const char* GetHandIkChannelName(I32F iArm);
StringId64 GetHandIkJointNameId(I32F iArm);
const char* GetHandIkJointName(I32F iArm);

const char* ArmIndexToString(int armIndex);
