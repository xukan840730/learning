/*
 * Copyright (c) 2011 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/math/locator.h"
#include "corelib/util/timeframe.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/util/tracker.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ArtItemAnim;

namespace DC
{
	struct AnkleInfo;
	struct WristInfo;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnkleInfo
{
	// TODO@QUAD does this need to support quadrupeds also?

	AnkleInfo();
	void Reset();
	void Update(Point_arg newLAnklePos,
				Point_arg newRAnklePos,
				const float dt,
				Vector_arg groundNormalOs,
				DC::AnkleInfo* pDCInfoOut);
	void DebugDraw(const Locator& alignWs) const;

	Point m_anklePos[2];
	Vector m_ankleVel[2];
	SpringTracker<Vector> m_ankleVelSpring[2];
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct WristInfo
{
	WristInfo();
	void Reset();
	void Update(Point_arg newLWristPos, Point_arg newRWristPos, const float dt, DC::WristInfo* pDCInfoOut);
	void DebugDraw(const Locator& alignWs) const;

	Point m_wristPos[2];
	Vector m_wristVel[2];
	SpringTracker<Vector> m_wristVelSpring[2];
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct PoseMatchEntry
{
	bool		m_valid = false;
	bool		m_velocityValid = false;
	StringId64	m_channelId = INVALID_STRING_ID_64;	// name of the joint's compressed channel to match against
	Point		m_matchPosOs = kOrigin;	// object space position to match to
	Vector		m_matchVel = kZero;		// velocity to match to
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct PoseMatchInfo
{
	static CONST_EXPR size_t kMaxEntries = 16;

	SkeletonId m_skelId = INVALID_SKELETON_ID;

	float m_startPhase = 0.0f;
	float m_endPhase = 0.0f;
	float m_earlyOutThreshold = 0.0f;

	bool m_rateRelativeToEntry0 = false;
	bool m_mirror = false;
	bool m_debug = false;
	bool m_debugPrint = false;

	DebugPrimTime m_debugDrawTime = Seconds(1.0f);
	Vector m_inputGroundNormalOs = kUnitYAxis;
	Locator m_debugDrawLoc = kIdentity;
	Transform m_strideTransform = kIdentity;
	PoseMatchEntry m_entries[kMaxEntries];
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct PoseMatchAlternates
{
	static CONST_EXPR size_t kMaxAlternates = 128;

	float m_alternateThreshold = 0.0f;
	float m_alternatePhase[kMaxAlternates];
	float m_alternateRating[kMaxAlternates];
	U32 m_numAlternates = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct PoseMatchResult
{
	PoseMatchResult() = default;
	PoseMatchResult(float phase) : m_phase(phase) {}

	float m_phase = 0.0f;
	float m_rating = 0.0f;
};

PoseMatchResult CalculateBestPoseMatchPhase(const ArtItemAnim* pAnim,
											const PoseMatchInfo& matchInfo,
											PoseMatchAlternates* pAlternatesOut = nullptr);

/// --------------------------------------------------------------------------------------------------------------- ///
// [TODO]: this should be moved to animation pre-processing!
// foot contact with ground funcs here. should be moved to animation pre-processing
// feet are considered to be on the ground if one of their adjacent joints (either the ankle or the toe) 
// is sufficiently close to the ground and its velocity is below some threshold
enum FootState
{
	kFootInvalid,
	kHeelToePlanted,
	kToePlanted,
	kHeelPlanted,
	kHeelToeInAir,
};

struct FootStateSegment
{
public:
	FootState m_footState;
	I32 m_startFrame;
	I32 m_endFrame;
};

struct SingleFootSegmentInfo
{
public:
	SingleFootSegmentInfo()
		: m_numSegments(0)
	{}

	static const I32 kMaxNumFootSegments = 64;

	FootStateSegment m_footSegments[kMaxNumFootSegments];
	I32 m_numSegments;
};

struct FootContactFuncParams
{
public:
	FootContactFuncParams();

	SkeletonId m_skelId;
	const ArtItemAnim* m_pAnim;
	StringId64 m_groundApRef;
	bool m_debugDraw;
	float m_anklesGroundYThreshold[2];	// the order is right, left.
	float m_toesGroundYThreshold[2];
	float m_ankleLiftingVelThreshold;	// lifting needs smaller velocity threshold.
	float m_ankleLandingVelThreshold;	// landing needs bigger velocity threshold.
	float m_toeLiftingVelThreshold;		// lifting needs smaller velocity threshold.
	float m_toeLandingVelThreshold;
};

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnalyzeFootContact2(const FootContactFuncParams& params,
						 SingleFootSegmentInfo* outRightFootInfo,
						 SingleFootSegmentInfo* outLeftFootInfo);

struct JointDiffWeight
{
public:
	StringId64 m_jointNameId;
	I16 m_jointIndex;
	float m_orientationDiffWeight;
	float m_rotationVelDiffWeight;
};

struct JointDiffResult
{
public:
	StringId64 m_name;
	StringId64 m_type;
	float m_diffRaw;
	float m_diffFinal;
};

struct SingleFrameAnimJointBuffer
{
public:
	SingleFrameAnimJointBuffer()
		: m_joints(nullptr)
		, m_numJoints(0)
	{}

	void Allocate(const I32 numJoints)
	{
		m_numJoints = numJoints;
		m_joints = NDI_NEW ndanim::JointParams[numJoints];
	}

	ndanim::JointParams* m_joints;
	I32 m_numJoints;
};

struct AnimJointBuffer
{
public:
	AnimJointBuffer()
		: m_frames(nullptr)
		, m_numFrames(0)
	{}

	void Allocate(const I32 numFrames, const I32 numJoints)
	{
		m_numFrames = numFrames;
		m_frames = NDI_NEW SingleFrameAnimJointBuffer[m_numFrames];

		for (U32F iFrame = 0; iFrame < numFrames; iFrame++)
		{
			m_frames[iFrame].Allocate(numJoints);
		}
	}

	SingleFrameAnimJointBuffer& GetFrameBuffer(I32 frame) 
	{ 
		ANIM_ASSERT(frame >= 0 && frame < m_numFrames); 
		return m_frames[frame]; 
	}

	const SingleFrameAnimJointBuffer& GetFrameBuffer(I32 frame) const
	{
		ANIM_ASSERT(frame >= 0 && frame < m_numFrames); 
		return m_frames[frame]; 
	}

private:
	SingleFrameAnimJointBuffer* m_frames;
	I32 m_numFrames;
};

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputeJointDifference(const AnimJointBuffer* pAnimJointBufferI,
							 const AnimJointBuffer* pAnimJointBufferJ,
							 StringId64 groundApRef,
							 const JointDiffWeight* arrJointWeights,
							 const I32 numJointWeights,
							 const I16 rootIndex,
							 const float rootDeltaDiffWeight,
							 const SkeletonId skelId,
							 const ArtItemAnim* pAnimI,
							 const I32 frameI,
							 const ArtItemAnim* pAnimJ,
							 const I32 frameJ,
							 JointDiffResult* outResults = nullptr,
							 I32* outNumResults		 = nullptr,
							 const I32 maxNumResults = 0);

void ConvertJointWeights(StringId64 jointDiffDefId,
						 SkeletonId skelId,
						 float* outRootDeltaDiffWeight,
						 JointDiffWeight* outWeights,
						 I32* outNumWeights,
						 const I32 maxNumWeights);

struct JointDiffFuncParams
{
public:
	JointDiffFuncParams();

	StringId64 m_jointDiffDefId;
	SkeletonId m_skelId;
	bool m_debugDraw;
	bool m_printError;
	const ArtItemAnim* m_pAnimI;
	const ArtItemAnim* m_pAnimJ;
	I32 m_frameI;
	float m_minPhase;	// choose between min-max phase.
	float m_maxPhase;	// choose between min-max phase.
	StringId64 m_groundApRef;
	bool m_useLookupTable;
	AnimJointBuffer* m_precacheAnimBufferI;
	AnimJointBuffer* m_precacheAnimBufferJ;
};

bool ComputeJointDiffFrameInternal(const JointDiffFuncParams& params,
								   const SingleFootSegmentInfo* rightFootI,
								   const SingleFootSegmentInfo* leftFootI,
								   const SingleFootSegmentInfo* rightFootJ,
								   const SingleFootSegmentInfo* leftFootJ,
								   I32* outFrameJ);
bool ComputeJointDiffTransitionFrame(const JointDiffFuncParams& params, I32* outFrameJ);

void RegisterPoseMatchingAnim(const ArtItemAnim* pAnim);
void ToggleNeedPreProcessPoseMatchingAnims();
bool IsPreProcessingPoseMatchingAnims();

enum PostMatchLookupResult
{
	kPoseMatchLookupInvalid = 0,
	kPoseMatchLookupTableNotBuilt,
	kPoseMatchLookupFailed,
	kPoseMatchLookupSuccess,
};

PostMatchLookupResult LookupPreProcessTransitionFrame(const SkeletonId skelId,
													  StringId64 weightNameId,
													  const StringId64 srcAnimId,
													  const StringId64 dstAnimId,
													  const I32 srcFrame,
													  I32* outFrame);
