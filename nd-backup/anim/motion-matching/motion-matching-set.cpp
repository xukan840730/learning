/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/motion-matching/motion-matching-set.h"

#include "corelib/memory/memory-fence.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/nd-frame-state.h"

#include "gamelib/anim/motion-matching/motion-matching-debug.h"
#include "gamelib/anim/motion-matching/motion-pose.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-anim.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/scriptx/h/motion-matching-defines.h"

#include "redisrpc/cpp/RedisRPC/redisrpc-types.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static CONST_EXPR size_t kNumGoalEntriesPerTrajSample = 10; // 3 for pos + 3 for vel + 3 for dir + 1 for yaw speed

/// --------------------------------------------------------------------------------------------------------------- ///
static Point GetPointFromAnimVector(const MotionMatchingSet::AnimVector& animVector, U32F offset)
{
	return Point(animVector[offset + 0], animVector[offset + 1], animVector[offset + 2]);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Vector GetVectorFromAnimVector(const MotionMatchingSet::AnimVector& animVector, U32F offset)
{
	return Vector(animVector[offset + 0], animVector[offset + 1], animVector[offset + 2]);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool operator<(const MMCAnimSample& a, const MMCAnimSample& b)
{
	return a.m_animIndex < b.m_animIndex || (a.m_animIndex == b.m_animIndex && a.m_sampleIndex < b.m_sampleIndex);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool operator<(const MotionMatchingSet::ClosestEntry& a, const MotionMatchingSet::ClosestEntry& b)
{
	return a.m_cost < b.m_cost;
}

STATIC_ASSERT(sizeof(MotionMatchingVectorTable) == sizeof(MMAnimVectorTableRaw));

/// --------------------------------------------------------------------------------------------------------------- ///
// Binary search to find the index of a given animation
/// --------------------------------------------------------------------------------------------------------------- ///
static I32F GetAnimIndex(const MMAnimSampleTable* pSampleTable, StringId64 animId)
{
	const StringId64* pAnimIdsStart = pSampleTable->m_aAnimIds;
	const StringId64* pAnimIdsEnd   = pSampleTable->m_aAnimIds + pSampleTable->m_numAnims;
	const StringId64* pAnimIdFound = std::lower_bound(pAnimIdsStart, pAnimIdsEnd, animId);

	if (pAnimIdFound == pAnimIdsEnd || *pAnimIdFound != animId)
	{
		return -1;
	}

	return pAnimIdFound - pAnimIdsStart;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Return the range of samples of an animation given an animation name
/// --------------------------------------------------------------------------------------------------------------- ///
static const MMAnimSampleRange* GetSampleRangeInTable(const MMAnimSampleTable* pSampleTable, const StringId64 animId)
{
	int iAnimIndex = GetAnimIndex(pSampleTable, animId);
	if (iAnimIndex < 0)
	{
		return nullptr;
	}
	return &pSampleTable->m_aAnimRanges[iAnimIndex];
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Find the index of the closest sample in the table to a given AnimSample
// This is used to get the pose data for an AnimSample
/// --------------------------------------------------------------------------------------------------------------- ///
static I32F GetClosestSampleInTable(const MMAnimSampleTable* pSampleTable, const AnimSample& animSample)
{
	ANIM_ASSERT(pSampleTable->m_numAnims < std::numeric_limits<MMAnimIndex>::max());
	
	const I32F iAnimIndex = GetAnimIndex(pSampleTable, animSample.GetAnimNameId());

	if (iAnimIndex < 0)
	{
		return -1;
	}

	MMAnimIndex animIndex  = static_cast<MMAnimIndex>(iAnimIndex);
	const float fSampleRaw = animSample.Sample();
	U16 sample = static_cast<MMSampleIndex>(fSampleRaw);

	MMCAnimSample querySample = { animIndex, sample };

	MMCAnimSample* samplesStart = pSampleTable->m_aSamples;
	MMCAnimSample* samplesEnd   = pSampleTable->m_aSamples + pSampleTable->m_numSamples;

	MMCAnimSample* animSampleStart = samplesStart + pSampleTable->m_aAnimRanges[animIndex].m_startIndex;
	MMCAnimSample* animSampleEnd   = animSampleStart + pSampleTable->m_aAnimRanges[animIndex].m_count;

	ANIM_ASSERT(animSampleStart >= samplesStart && animSampleStart <= samplesEnd);
	ANIM_ASSERT(animSampleEnd >= samplesStart && animSampleEnd <= samplesEnd);
	ANIM_ASSERT(animSampleStart != animSampleEnd);

	MMCAnimSample* closeStart = std::lower_bound(animSampleStart, animSampleEnd, querySample);
	MMCAnimSample* closeEnd   = std::upper_bound(animSampleStart, animSampleEnd, querySample);

	closeStart = std::max(closeStart - 1, animSampleStart);
	closeEnd   = std::min(closeEnd + 1, animSampleEnd);

	float				 minDiff  = kLargeFloat;
	const MMCAnimSample* pClosest = nullptr;
	for (MMCAnimSample* it = closeStart; it != closeEnd; ++it)
	{
		if (pClosest == nullptr)
		{
			pClosest = it;
			minDiff  = Abs(it->m_sampleIndex - fSampleRaw);
		}
		else
		{
			float diff = Abs(it->m_sampleIndex - fSampleRaw);
			if (diff < minDiff)
			{
				minDiff  = diff;
				pClosest = it;
			}
		}
	}
	ANIM_ASSERT(pClosest != nullptr);
	return pClosest - samplesStart;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsPoseValidForPoseDef(const IMotionPose& pose, const MMPose& poseDef)
{
	bool valid = true;

	for (I32F i = 0; i < poseDef.m_numBodies; i++)
	{
		const MMPoseBody& body = poseDef.m_aBodies[i];

		if (body.m_isCenterOfMass) // assume all poses implement GetCenterOfMassOs()
			continue;

		valid = valid && pose.HasDataForJointId(body.m_jointId);
	}

	valid = valid && pose.HasDataForJointId(poseDef.m_facingJointId);

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsSkeletonValidForPoseDef(const ArtItemSkeleton* pSkel, const MMPose& poseDef)
{
	bool valid = true;

	for (I32F i = 0; i < poseDef.m_numBodies; i++)
	{
		const MMPoseBody& body = poseDef.m_aBodies[i];

		if (body.m_isCenterOfMass) // assume whoever uses this finds a way to create COM from skeleton
			continue;

		const I32F iJoint = FindJoint(pSkel->m_pJointDescs, pSkel->m_numGameplayJoints, body.m_jointId);

		if (iJoint < 0)
		{
			valid = false;
			break;
		}
	}

	if (valid && poseDef.m_facingJointId != INVALID_STRING_ID_64)
	{
		const I32F iJoint = FindJoint(pSkel->m_pJointDescs, pSkel->m_numGameplayJoints, poseDef.m_facingJointId);
		valid = valid && (iJoint >= 0);
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Create an AnimVector from an IMotionPose and the settings in a MotionMatching Set
/// --------------------------------------------------------------------------------------------------------------- ///
static I32F MotionPoseToVector(const IMotionPose& pose,
							   MotionMatchingSet::AnimVector& outVector,
							   const MotionMatchingSet* pSet)
{
	I32F index = 0;
	const MMPose& poseDef = pSet->m_pSettings->m_pose;

	ANIM_ASSERT(IsPoseValidForPoseDef(pose, poseDef));

	for (I32F i = 0; i < poseDef.m_numBodies; i++)
	{
		const MMPoseBody& body = poseDef.m_aBodies[i];
		IMotionPose::BodyData d = body.m_isCenterOfMass ? pose.GetCenterOfMassOs() : pose.GetJointDataByIdOs(body.m_jointId);

		outVector[index++] = d.m_pos.X();
		outVector[index++] = d.m_pos.Y();
		outVector[index++] = d.m_pos.Z();
		outVector[index++] = d.m_vel.X();
		outVector[index++] = d.m_vel.Y();
		outVector[index++] = d.m_vel.Z();
	}

	const Locator facingLoc = pose.GetJointLocatorOs(poseDef.m_facingJointId);
	const Vector facing3d = facingLoc.TransformVector(poseDef.m_facingAxisLs);
	const Vector facing = AsUnitVectorXz(facing3d, kZero);

	outVector[index++] = facing.X();
	outVector[index++] = facing.Y();
	outVector[index++] = facing.Z();

	return index;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Convert an AnimTrajectory to entries in an AnimVector
/// --------------------------------------------------------------------------------------------------------------- ///
static int TrajectoryToVector(const AnimTrajectory& trajectory,
							  const int startIndex,
							  const float maxTime,
							  const int numSamples,
							  MotionMatchingSet::AnimVector& outVector,
							  MotionMatchingSet::AnimVector& outScaleFilterVector)
{
	int   index = startIndex;
	const float dt = maxTime / numSamples;
	for (int i = 0; i < numSamples; ++i)
	{
		float t = dt * (i + 1);

		AnimTrajectorySample g = trajectory.Get(t);

		if (g.IsPositionValid())
		{
			outScaleFilterVector[index]		= 1.0f;
			outScaleFilterVector[index + 1] = 1.0f;
			outScaleFilterVector[index + 2] = 1.0f;

			outVector[index++] = g.GetPosition().X();
			outVector[index++] = g.GetPosition().Y();
			outVector[index++] = g.GetPosition().Z();
		}
		else
		{
			outScaleFilterVector[index]		= 0.0f;
			outScaleFilterVector[index + 1] = 0.0f;
			outScaleFilterVector[index + 2] = 0.0f;

			outVector[index++] = 0.0f;
			outVector[index++] = 0.0f;
			outVector[index++] = 0.0f;
		}

		if (g.IsVelocityValid())
		{
			outScaleFilterVector[index]		= 1.0f;
			outScaleFilterVector[index + 1] = 1.0f;
			outScaleFilterVector[index + 2] = 1.0f;

			outVector[index++] = g.GetVelocity().X();
			outVector[index++] = g.GetVelocity().Y();
			outVector[index++] = g.GetVelocity().Z();
		}
		else
		{
			outScaleFilterVector[index]		= 0.0f;
			outScaleFilterVector[index + 1] = 0.0f;
			outScaleFilterVector[index + 2] = 0.0f;

			outVector[index++] = 0.0f;
			outVector[index++] = 0.0f;
			outVector[index++] = 0.0f;
		}

		if (g.IsFacingValid())
		{
			outScaleFilterVector[index] = 1.0f;
			outScaleFilterVector[index + 1] = 1.0f;
			outScaleFilterVector[index + 2] = 1.0f;

			outVector[index++] = g.GetFacingDir().X();
			outVector[index++] = g.GetFacingDir().Y();
			outVector[index++] = g.GetFacingDir().Z();
		}
		else
		{
			outScaleFilterVector[index] = 0.0f;
			outScaleFilterVector[index + 1] = 0.0f;
			outScaleFilterVector[index + 2] = 0.0f;

			outVector[index++] = 0.0f;
			outVector[index++] = 0.0f;
			outVector[index++] = 0.0f;
		}

		if (g.IsYawSpeedValid() && (i == (numSamples - 1)))
		{
			outScaleFilterVector[index] = 1.0f;
			outVector[index++] = g.GetYawSpeed();
		}
		else
		{
			outScaleFilterVector[index] = 0.0f;
			outVector[index++] = 0.0f;
		}
	}

	return index;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Convert an AnimTrajectory to entries in an AnimVector
/// --------------------------------------------------------------------------------------------------------------- ///
static bool TrajectoryToVector(const AnimTrajectory& trajectory,
							   const MMGoals* pSettings,
							   I32F startIndex,
							   MotionMatchingSet::AnimVector& outGoalFilter,
							   MotionMatchingSet::AnimVector& outVector)
{

	I32F vectorIndex = startIndex;

	vectorIndex = TrajectoryToVector(trajectory,
									 vectorIndex,
									 pSettings->m_maxTrajSampleTime,
									 pSettings->m_numTrajSamples,
									 outVector,
									 outGoalFilter);

	vectorIndex = TrajectoryToVector(trajectory,
									 vectorIndex,
									 -pSettings->m_maxTrajSampleTimePrevTraj,
									 pSettings->m_numTrajSamplesPrevTraj,
									 outVector,
									 outGoalFilter);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MMSearchParams::IsLayerActive(StringId64 layerId, float* pLayerCostOut /* = nullptr */) const
{
	bool active = false;
	float costMod = 0.0f;

	for (U32F i = 0; i < m_numActiveLayers; ++i)
	{
		if (m_activeLayers[i] == layerId)
		{
			active = true;
			costMod = m_layerCostModifiers[i];
			break;
		}
	}

	if (pLayerCostOut)
		*pLayerCostOut = costMod;

	return active;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Combine the weights into a single vector (auto weights only used for data)
/// --------------------------------------------------------------------------------------------------------------- ///
static MotionMatchingSet::AnimVector CompositeWeights_Raw(const MotionMatchingSet::AnimVector& settingWeights,
														  const MotionMatchingSet::AnimVector& autoWeights,
														  const MotionMatchingSet::AnimVector& goalFilterWeights)
{
	return settingWeights.cwiseProduct(autoWeights).cwiseProduct(goalFilterWeights);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Combine the weights into a single vector (auto weights used for data and scales)
/// --------------------------------------------------------------------------------------------------------------- ///
static MotionMatchingSet::AnimVector CompositeWeights_NormScales(const MotionMatchingSet::AnimVector& autoWeights,
																 const MotionMatchingSet::AnimVector& poseScales,
																 const MotionMatchingSet::AnimVector& goalScales,
																 const MotionMatchingSet::AnimVector& extraScales,
																 const float poseWeight,
																 const float goalWeight,
																 const size_t numPoseDimensions,
																 const size_t numGoalDimensions,
																 const size_t numExtraDimensions)
{
	const float pose_variance_scale = poseScales.segment(0, numPoseDimensions).squaredNorm() / float(numPoseDimensions);
	const float goal_variance_scale = goalScales.segment(numPoseDimensions, numGoalDimensions).squaredNorm() / float(numGoalDimensions);

	MotionMatchingSet::AnimVector weightedPoseScales = poseScales * (1.0f / Sqrt(pose_variance_scale)) * poseWeight;

//	weightedPoseScales.segment(numPoseDimensions, numGoalDimensions + numExtraDimensions).fill(0.0f);

	MotionMatchingSet::AnimVector weightedGoalScales = goalScales * (1.0f / Sqrt(goal_variance_scale)) * goalWeight;

// 	weightedGoalScales.segment(0, numPoseDimensions).fill(0.0f);
// 	weightedGoalScales.segment(numPoseDimensions + numGoalDimensions, numExtraDimensions).fill(0.0f);

	MotionMatchingSet::AnimVector compositedScales = MotionMatchingSet::AnimVector::Constant(numPoseDimensions
																							 + numGoalDimensions
																							 + numExtraDimensions, 0.0f);

	compositedScales = weightedPoseScales + weightedGoalScales + extraScales;

	MotionMatchingSet::AnimVector ret = autoWeights.cwiseProduct(compositedScales);
	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionMatchingSet::AnimVector MotionMatchingSet::CompositeWeights(const AnimVector& goalFilterVec,
																  AnimVector* pMinimumsOut) const
{
	const I32F numTotalDimensions = GetTotalNumDimensions();

	AnimVector settingsScales(numTotalDimensions);
	AnimVector minimum(numTotalDimensions);
	AnimVector poseScales(numTotalDimensions);
	AnimVector goalScales(numTotalDimensions);
	AnimVector extraScales(numTotalDimensions);

	CreateScaleAndMinVectors(m_pSettings, settingsScales, minimum, &poseScales, &goalScales, &extraScales);

	AnimVector scales;

	AnimVector autoWeights = GetAutoWeights();
	if (!m_pSettings->m_useNormalizedData)
	{
		autoWeights.setOnes();
	}

	if (m_pSettings->m_useNormalizedScales)
	{
		const I32F numPoseDimensions = GetNumPoseDimensions();
		const I32F numGoalDimensions = GetNumGoalDimensions();
		const I32F numExtraDimensions = GetNumExtraDimensions();

		AnimVector goalScalesFiltered = goalScales.cwiseProduct(goalFilterVec);

		scales = CompositeWeights_NormScales(autoWeights,
											 poseScales,
											 goalScalesFiltered,
											 extraScales,
											 m_pSettings->m_pose.m_masterWeight,
											 m_pSettings->m_goals.m_masterWeight,
											 numPoseDimensions,
											 numGoalDimensions,
											 numExtraDimensions);
	}
	else
	{
		scales = CompositeWeights_Raw(settingsScales, autoWeights, goalFilterVec);
	}

	if (pMinimumsOut)
	{
		*pMinimumsOut = minimum;
	}

	return scales;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MotionMatchingSet::IsAnimInSet(StringId64 animId) const 
{ 
	return GetAnimIndex(m_sampleTable, animId) >= 0; 
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MotionMatchingSet::IsAnimInSet(const AnimSample& sample, bool* pNextSampleInSetOut /* = nullptr */) const
{
	bool inSet = false;
	bool nextInSet = false;

	if (const MMAnimSampleRange* pSampleRange = GetSampleRangeInTable(m_sampleTable, sample.GetAnimNameId()))
	{
		const MMCAnimSample* pSampleTable = m_sampleTable->m_aSamples;
		const MMCAnimSample& tableEntry = pSampleTable[pSampleRange->m_startIndex + pSampleRange->m_count - 1];

		const float fSample = sample.Sample();

		if (tableEntry.m_sampleIndex >= fSample)
		{
			inSet = true;

			if (tableEntry.m_sampleIndex >= (fSample + 1.0f))
			{
				nextInSet = true;
			}
		}
	}

	if (pNextSampleInSetOut)
	{
		*pNextSampleInSetOut = nextInSet;
	}

	return inSet;	
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MotionMatchingSet::HasValidIndices() const
{
	const I32F numPoseDimensions  = GetNumPoseDimensions();
	const I32F numGoalDimensions  = GetNumGoalDimensions();
	const I32F numExtraDimensions = GetNumExtraDimensions();
	const I32F numTotalDimensions = GetTotalNumDimensions();

	AnimVector minimum(numTotalDimensions);
	AnimVector goalFilterVec(numTotalDimensions);
	AnimVector queryVec(numTotalDimensions);

	goalFilterVec.fill(1.0f);

	// ugh
	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	AnimTrajectorySample sample;
	AnimTrajectory dummyTraj(2);

	sample.SetPosition(kOrigin);
	sample.SetVelocity(kZero);
	sample.SetFacingDir(kUnitZAxis);
	sample.SetYawSpeed(0.0f);

	sample.SetTime(-kLargeFloat);
	dummyTraj.Add(sample);
	sample.SetTime(kLargeFloat);
	dummyTraj.Add(sample);

	TrajectoryToVector(dummyTraj, &m_pSettings->m_goals, numPoseDimensions, goalFilterVec, queryVec);

	AnimVector scales = CompositeWeights(goalFilterVec, &minimum);

	bool hasValid = false;

	const MotionMatchingIndex* pIndices = static_cast<const MotionMatchingIndex*>(m_aIndices);
	for (I32F i = 0; i < m_numIndices; i++)
	{
		if (pIndices[i].IsValid(scales, minimum))
		{
			hasValid = true;
			break;
		}
	}

	return hasValid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<AnimSample> MotionMatchingSet::FindClosestSampleExisting(const MMSearchParams& params,
															   const AnimSampleBiased& sample,
															   const AnimTrajectory& trajectory,
															   I32F desiredGroupId,
															   const AnimSampleBiased& extraSample) const
{
	Maybe<AnimSample> res = MAYBE::kNothing;

	I32F sampleIndex = -1;

	AnimVector queryVectorSample(m_numDimensions);
	AnimVector goalFilter(m_numDimensions);

	if (CreateAnimVectorFromExisting(params, sample, trajectory, &queryVectorSample, &goalFilter, &sampleIndex))
	{
		AnimVector minimum(m_numDimensions);
		AnimVector scales = CompositeWeights(goalFilter, &minimum);

		const I32F numPoseDimensions = GetNumPoseDimensions();
		const I32F numGoalDimensions = GetNumGoalDimensions();

		queryVectorSample[numPoseDimensions + numGoalDimensions + kExtraDimensionGroupId] = float(desiredGroupId);
		queryVectorSample[numPoseDimensions + numGoalDimensions + kExtraDimensionBias]	  = 0.0f;

		ClosestEntry closest = FindClosestSampleFromVectors(params, queryVectorSample, scales, minimum);

		const I32F iLayer = m_pLayerIndexTable[sampleIndex];
		const StringId64 layerId = m_aLayerIds[iLayer];

		float layerCost = 0.0f;
		if (params.IsLayerActive(layerId, &layerCost) && sample.Mirror() == closest.m_mirror)
		{
			const MotionMatchingVectorTable& vecTable = VectorTable();
			const AnimVector sampleVec = sample.Mirror() ? MirrorVector(vecTable[sampleIndex]) : vecTable[sampleIndex];

			const float naturalBias = sample.CostBias();
			const float naturalDist = DistanceFunc(sampleVec, queryVectorSample, scales, minimum);
			const float naturalCost = naturalDist - naturalBias + layerCost;
			
			if (naturalCost <= closest.m_cost)
			{
				closest.m_cost		  = naturalCost;
				closest.m_vectorIndex = sampleIndex;
				closest.m_mirror	  = sample.Mirror();
				closest.m_costBias	  = naturalBias;
			}
		}

		closest = ConsiderExtraSample(extraSample, closest, queryVectorSample, scales, minimum);

		if (closest.m_vectorIndex >= 0)
		{
			res = AnimSampleFromIndex(closest.m_vectorIndex, closest.m_mirror);
		}
	}

	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Maybe<AnimSample> MotionMatchingSet::FindClosestSampleExternal(const MMSearchParams& params,
															   const IMotionPose& pose,
															   const AnimTrajectory& trajectory,
															   I32F poseGroupId,
															   I32F poseBias,
															   const AnimSampleBiased& extraSample) const
{
	AnimVector v(m_numDimensions);
	AnimVector goalFilterVec(m_numDimensions);

	Maybe<AnimSample> res = MAYBE::kNothing;

	if (CreateAnimVectorFromExternal(params, pose, trajectory, poseGroupId, poseBias, &v, &goalFilterVec))
	{
		AnimVector minimum(m_numDimensions);
		AnimVector scales = CompositeWeights(goalFilterVec, &minimum);

		ClosestEntry closest = FindClosestSampleFromVectors(params, v, scales, minimum);

		closest = ConsiderExtraSample(extraSample, closest, v, scales, minimum);

		if (closest.m_vectorIndex >= 0)
		{
			res = AnimSampleFromIndex(closest.m_vectorIndex, closest.m_mirror);
		}
	}

	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionMatchingSet::ClosestEntry MotionMatchingSet::FindClosestSampleFromVectors(const MMSearchParams& params,
																				const AnimVector& v,
																				const AnimVector& scales,
																				const AnimVector& minimum) const
{
	float* aLayerCostMods = STACK_ALLOC(float, m_numLayers);
	memset(aLayerCostMods, 0, sizeof(float) * m_numLayers);

	const BitArray64 layerBits = CreateValidLayerBits(params, aLayerCostMods);

	ClosestEntry bestResult = { kLargeFloat, -1, false };

	if (layerBits.GetData() == 0ULL)
	{
		return bestResult;
	}

	if (params.m_mirrorMode != MMMirrorMode::kForced)
	{
		bestResult = FindClosestSampleInternal(v, scales, minimum, false, layerBits, aLayerCostMods);
	}

	if (params.m_mirrorMode != MMMirrorMode::kNone)
	{
		AnimVector mv = MirrorVector(v);

		ClosestEntry bestMirror = FindClosestSampleInternal(mv, scales, minimum, true, layerBits, aLayerCostMods);

		if (bestMirror.m_cost < bestResult.m_cost)
		{
			bestResult = bestMirror;
		}
	}

	return bestResult;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float MotionMatchingSet::ComputeGoalCostWithExtras(const MMSearchParams& params,
												   const AnimSample& sample,
												   bool sampleBiased,
												   I32F sampleGroupId,
												   const AnimTrajectory& trajectory,
												   AnimVector* pDistanceVecOut /* = nullptr */) const
{
	const I32F numTotalDimensions = GetTotalNumDimensions();
	const I32F numPoseDimensions  = GetNumPoseDimensions();
	const I32F numGoalDimensions  = GetNumGoalDimensions();
	const I32F numExtraDimensions = GetNumExtraDimensions();

	const size_t extraDimensionsOffset = numPoseDimensions + numGoalDimensions;

	AnimVector sampleVec(numTotalDimensions);

	const I32F iExisting = GetTableIndexFromSample(sample);
	if (iExisting >= 0)
	{
		// grab the tools-built sample vector and zero out the stuff we don't care about
		sampleVec = VectorTable()[iExisting];
		sampleVec.head(numPoseDimensions).fill(0.0f);
	}
	else
	{
		// Build the sample vector from animation's align 
		sampleVec.fill(0.0f);

		// We only care about the goal fields
		I32F index = numPoseDimensions;
		const float dt = m_pSettings->m_goals.m_maxTrajSampleTime / m_pSettings->m_goals.m_numTrajSamples;
		const float stoppingFaceDist = m_pSettings->m_goals.m_stoppingFaceDist;

		Maybe<MMLocomotionState> maybeAlignWithVelInFuture;

		for (I32F i = 0; i < m_pSettings->m_goals.m_numTrajSamples; ++i)
		{
			const float sampleTime = dt * (i + 1);

			maybeAlignWithVelInFuture = ComputeLocomotionStateInFuture(sample, sampleTime, stoppingFaceDist);

			if (!maybeAlignWithVelInFuture.Valid())
			{
				return kLargeFloat;
			}
			const MMLocomotionState& alignWithVelInFuture = maybeAlignWithVelInFuture.Get();

			const Point pos		 = alignWithVelInFuture.m_pathPosOs;
			const Vector vel	 = alignWithVelInFuture.m_velocityOs;
			const Vector facing	 = alignWithVelInFuture.m_strafeDirOs;
			const float yawSpeed = alignWithVelInFuture.m_yawSpeed;

			sampleVec[index++] = pos.X();
			sampleVec[index++] = pos.Y();
			sampleVec[index++] = pos.Z();

			sampleVec[index++] = vel.X();
			sampleVec[index++] = vel.Y();
			sampleVec[index++] = vel.Z();

			sampleVec[index++] = facing.X();
			sampleVec[index++] = facing.Y();
			sampleVec[index++] = facing.Z();

			sampleVec[index++] = yawSpeed;
		}

		sampleVec[numPoseDimensions + numGoalDimensions + kExtraDimensionBias] = sampleBiased ? 0.0f : 1.0f;
		sampleVec[numPoseDimensions + numGoalDimensions + kExtraDimensionGroupId] = float(sampleGroupId);
	}

	AnimVector queryVec(numTotalDimensions);
	queryVec.fill(0.0f);

	AnimVector goalFilterVec(numTotalDimensions);
	goalFilterVec.fill(1.0f);

	TrajectoryToVector(trajectory, &m_pSettings->m_goals, numPoseDimensions, goalFilterVec, queryVec);

	GoalLocatorsToVector(params, goalFilterVec, queryVec);

	// assume desired trajectory is not biased, and in group zero
	queryVec[numPoseDimensions + numGoalDimensions + kExtraDimensionBias]	  = 1.0f;
	queryVec[numPoseDimensions + numGoalDimensions + kExtraDimensionGroupId] = 0.0f;

	AnimVector minimum(numTotalDimensions);
	AnimVector scales = CompositeWeights(goalFilterVec, &minimum);

	// this should match MotionMatchMetric::Dist() in build-transform-motion-matching.cpp
	const float dist = LinearDistance(queryVec, sampleVec, scales, minimum);

	if (pDistanceVecOut)
	{
		*pDistanceVecOut = DistanceVec(queryVec, sampleVec, scales, minimum);
	}

	return dist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float MotionMatchingSet::ComputeGoalCost(const MMSearchParams& params,
										 const AnimSample& sample,
										 const AnimTrajectory& trajectory) const
{
	const I32F numTotalDimensions = GetTotalNumDimensions();
	const I32F numPoseDimensions  = GetNumPoseDimensions();
	const I32F numGoalDimensions  = GetNumGoalDimensions();
	const I32F numExtraDimensions = GetNumExtraDimensions();

	const size_t extraDimensionsOffset = numPoseDimensions + numGoalDimensions;

	AnimVector sampleVec(numTotalDimensions);

	const I32F iExisting = GetTableIndexFromSample(sample);
	if (iExisting >= 0)
	{
		// grab the tools-built sample vector and zero out the stuff we don't care about
		sampleVec = VectorTable()[iExisting];
		sampleVec.head(numPoseDimensions).fill(0.0f);
		sampleVec.segment(numPoseDimensions + numGoalDimensions, numExtraDimensions).fill(0.0f);
	}
	else
	{
		// Build the sample vector from animation's align 
		sampleVec.fill(0.0f);

		// We only care about the goal fields
		I32F index = numPoseDimensions;
		const float dt = m_pSettings->m_goals.m_maxTrajSampleTime / m_pSettings->m_goals.m_numTrajSamples;
		const float stoppingFaceDist = m_pSettings->m_goals.m_stoppingFaceDist;

		Maybe<MMLocomotionState> maybeAlignWithVelInFuture;

		for (I32F i = 0; i < m_pSettings->m_goals.m_numTrajSamples; ++i)
		{
			const float sampleTime = dt * (i + 1);

			maybeAlignWithVelInFuture = ComputeLocomotionStateInFuture(sample, sampleTime, stoppingFaceDist);

			if (!maybeAlignWithVelInFuture.Valid())
			{
				return kLargeFloat;
			}
			const MMLocomotionState& alignWithVelInFuture = maybeAlignWithVelInFuture.Get();

			const Point pos		= alignWithVelInFuture.m_pathPosOs;
			const Vector vel	= alignWithVelInFuture.m_velocityOs;
			const Vector facing = alignWithVelInFuture.m_strafeDirOs;

			sampleVec[index++] = pos.X();
			sampleVec[index++] = pos.Y();
			sampleVec[index++] = pos.Z();

			sampleVec[index++] = vel.X();
			sampleVec[index++] = vel.Y();
			sampleVec[index++] = vel.Z();

			sampleVec[index++] = facing.X();
			sampleVec[index++] = facing.Y();
			sampleVec[index++] = facing.Z();
		}
	}

	AnimVector queryVec(numTotalDimensions);
	queryVec.fill(0.0f);
	
	AnimVector goalFilterVec(numTotalDimensions);
	goalFilterVec.fill(1.0f);

	TrajectoryToVector(trajectory, &m_pSettings->m_goals, numPoseDimensions, goalFilterVec, queryVec);

	GoalLocatorsToVector(params, goalFilterVec, queryVec);

	AnimVector minimum(numTotalDimensions);
	AnimVector scales = CompositeWeights(goalFilterVec, &minimum);

	// this should match MotionMatchMetric::Dist() in build-transform-motion-matching.cpp
	const float dist = LinearDistance(queryVec, sampleVec, scales, minimum);
	
	return dist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MotionMatchingSet::CreateTrajectoryFromSample(const AnimSample& sample, AnimTrajectory* pTrajectoryOut) const
{
	if (!pTrajectoryOut)
		return false;

	Maybe<MMLocomotionState> maybeAlignWithVelInFuture;

	const float stoppingFaceDist = m_pSettings->m_goals.m_stoppingFaceDist;
	const float dt = m_pSettings->m_goals.m_maxTrajSampleTime / m_pSettings->m_goals.m_numTrajSamples;

	for (I32F i = 0; i < m_pSettings->m_goals.m_numTrajSamples; ++i)
	{
		const float sampleTime = dt * (i + 1);

		maybeAlignWithVelInFuture = ComputeLocomotionStateInFuture(sample, sampleTime, stoppingFaceDist);

		if (!maybeAlignWithVelInFuture.Valid())
		{
			return false;
		}

		const MMLocomotionState& alignWithVelInFuture = maybeAlignWithVelInFuture.Get();

		const Point pos		 = alignWithVelInFuture.m_pathPosOs;
		const Vector vel	 = alignWithVelInFuture.m_velocityOs;
		const Vector facing	 = alignWithVelInFuture.m_strafeDirOs;
		const float yawSpeed = alignWithVelInFuture.m_yawSpeed;

		AnimTrajectorySample newSample;
		newSample.SetTime(sampleTime);
		newSample.SetPosition(pos);
		newSample.SetVelocity(vel);
		newSample.SetFacingDir(facing);
		newSample.SetYawSpeed(yawSpeed);

		pTrajectoryOut->Add(newSample);
	}
	
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void PrintDebugCostLegend()
{
	MsgConNotRecorded("\n");
	MsgConNotRecorded("%-21s: %8s : ( %8s * %8s * %8s * %8s )\n", "Category", "Total", "Raw", "Weight", "Auto", "Filter");
	MsgConNotRecorded("----------------------------------------------------------------------------------\n");
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float DebugCost(const MotionMatchingSet::AnimVector& va,
					   const MotionMatchingSet::AnimVector& vb,
					   const MotionMatchingSet::AnimVector& vTotalScale,
					   const MotionMatchingSet::AnimVector& vmin,
					   int index,
					   int size,
					   const char* pName)
{
	const MotionMatchingSet::AnimVector one = MotionMatchingSet::AnimVector::Ones(size);
	const MotionMatchingSet::AnimVector a	= va.segment(index, size);
	const MotionMatchingSet::AnimVector b	= vb.segment(index, size);
	const MotionMatchingSet::AnimVector min = vmin.segment(index, size);
	const MotionMatchingSet::AnimVector totalScale = vTotalScale.segment(index, size);

	const MotionMatchingSet::AnimVector rawDiff = MotionMatchingSet::DistanceVec(a, b, one, min);
	const MotionMatchingSet::AnimVector diff	= MotionMatchingSet::DistanceVec(a, b, totalScale, min);

	const float rawDist = rawDiff.squaredNorm();
	const float dist	= diff.squaredNorm();

	MsgConNotRecorded("%-15s cost : %8.3f : ( %8.3f * %8.3f )\n",
					  pName,
					  dist,
					  rawDist,
					  rawDist > 0.0f ? dist / rawDist : Sqr(totalScale[0]));

	return dist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float DebugCost(const MotionMatchingSet::AnimVector& va,
					   const MotionMatchingSet::AnimVector& vb,
					   const MotionMatchingSet::AnimVector& vUserScale,
					   const MotionMatchingSet::AnimVector& vFilterScale,
					   const MotionMatchingSet::AnimVector& vAutoScale,
					   const MotionMatchingSet::AnimVector& vmin,
					   int index,
					   int size,
					   const char* pName)
{
	const MotionMatchingSet::AnimVector one = MotionMatchingSet::AnimVector::Ones(size);
	const MotionMatchingSet::AnimVector a		  = va.segment(index, size);
	const MotionMatchingSet::AnimVector b		  = vb.segment(index, size);
	const MotionMatchingSet::AnimVector min		  = vmin.segment(index, size);
	const MotionMatchingSet::AnimVector userScale = vUserScale.segment(index, size);
	const MotionMatchingSet::AnimVector filterScale = vFilterScale.segment(index, size);
	const MotionMatchingSet::AnimVector autoScale	= vAutoScale.segment(index, size);
	
	const MotionMatchingSet::AnimVector totalScale = userScale.cwiseProduct(filterScale).cwiseProduct(autoScale);
	
	const MotionMatchingSet::AnimVector rawDiff		= MotionMatchingSet::DistanceVec(a, b, one, min);
	const MotionMatchingSet::AnimVector diff		= MotionMatchingSet::DistanceVec(a, b, totalScale, min);
	
	const float rawDist = rawDiff.squaredNorm();
	const float dist	= diff.squaredNorm();

	MsgConNotRecorded("%-15s cost : %8.3f : ( %8.3f * %8.3f * %8.3f * %8.3f )\n",
					  pName,
					  dist,
					  rawDist,
					  Sqr(userScale[0]),
					  Sqr(autoScale[0]),
					  Sqr(filterScale[0]));

	return dist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingSet::DebugClosestSamplesExisting(const MMSearchParams& params,
													const AnimSampleBiased& sample,
													const AnimTrajectory& trajectory,
													I32F desiredGroupId,
													const AnimSampleBiased& extraSample,
													const int N,
													int debugIndex,
													Maybe<AnimSample> extraDebugSample,
													const Locator& refLocWs,
													Color32 c) const
{
	STRIP_IN_FINAL_BUILD;

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	ClosestEntryList closest(N);
	AnimVector goalVec(m_numDimensions);
	AnimVector scales(m_numDimensions);
	AnimVector mins(m_numDimensions);
	AnimVector goalFilter(m_numDimensions);

	FindClosestSamplesExisting(params,
							   sample,
							   trajectory,
							   desiredGroupId,
							   extraSample,
							   closest,
							   goalVec,
							   scales,
							   mins,
							   goalFilter);

	MsgConNotRecorded("%s%-*s %s %3d (phase: %0.3f) [Current]\n",
					  GetTextColorString(kTextColorGreen),
					  60,
					  sample.Anim().ToArtItem()->GetName(),
					  sample.Mirror() ? "M" : " ",
					  (int)Round(sample.Frame()),
					  sample.Phase());

	if (g_motionMatchingOptions.m_drawOptions.m_drawPoses)
	{
		MotionMatchingDebug::DrawFuturePose(sample, refLocWs, kColorBlue, 0, 6.0f);
	}

	DebugClosestSamples(params, debugIndex, extraDebugSample, refLocWs, c, closest, goalVec, scales, mins, goalFilter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void DrawPoseVector(const MotionMatchingSet::AnimVector& v,
						   const MMPose& pose,
						   const Locator& refLocWs,
						   Color32 c,
						   float radius)
{
	for (int iBody = 0; iBody < pose.m_numBodies; iBody++)
	{
		int bodyStartIndex = iBody * 6;
		const Point bodyPosOs(v[bodyStartIndex], v[bodyStartIndex + 1], v[bodyStartIndex + 2]);
		const Point posWs = refLocWs.TransformPoint(bodyPosOs);

		g_prim.Draw(DebugCross(posWs, radius, c, kPrimEnableHiddenLineAlpha), kPrimDuration1FrameNoRecord);

		if (g_motionMatchingOptions.m_drawOptions.m_drawJointNames)
		{
			const char* pBodyName = pose.m_aBodies[iBody].m_isCenterOfMass
										? "CenterOfMass"
										: DevKitOnly_StringIdToString(pose.m_aBodies[iBody].m_jointId);
			g_prim.Draw(DebugString(posWs, pBodyName, c, 0.5f), kPrimDuration1FrameNoRecord);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void DrawGoalLocators(const MotionMatchingSet::AnimVector& v,
							 const MotionMatchingSet::AnimVector& filter,
							 const MMGoals& goals,
							 const Locator& refLocWs,
							 Color c,
							 U32F startIndex)
{
	for (U32F iLoc = 0; iLoc < goals.m_numGoalLocators; ++iLoc)
	{
		const U32F goalLocIndex = startIndex + (iLoc * 3);

		if (Abs(filter[goalLocIndex]) <= 0.0f)
			continue;

		const Point goalLocOs	= Point(v[goalLocIndex], v[goalLocIndex + 1], v[goalLocIndex + 2]);
		const Point goalLocWs	= refLocWs.TransformPoint(goalLocOs);

		g_prim.Draw(DebugCross(goalLocWs, 0.1f, c, kPrimEnableHiddenLineAlpha), kPrimDuration1FrameNoRecord);
		g_prim.Draw(DebugString(goalLocWs, DevKitOnly_StringIdToString(goals.m_aGoalLocators[iLoc].m_locatorId), c, 0.5f),
					kPrimDuration1FrameNoRecord);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void DrawTrajectory(const Locator& refLocWs,
						   const MotionMatchingSet::AnimVector& vec,
						   const MotionMatchingSet::AnimVector& scales,
						   int trajStartIndex,
						   int numSamples,
						   Color clr,
						   Color faceClr)
{
	Maybe<Point> prevPos = Point(kOrigin);

	for (int i = 0; i < numSamples; i++)
	{
		int posIndex  = trajStartIndex + (i * kNumGoalEntriesPerTrajSample);
		int velIndex  = trajStartIndex + (i * kNumGoalEntriesPerTrajSample) + 3;
		int faceIndex = trajStartIndex + (i * kNumGoalEntriesPerTrajSample) + 6;
		int yawIndex = trajStartIndex + (i * kNumGoalEntriesPerTrajSample) + 9;

		Point pos(vec[posIndex + 0], vec[posIndex + 1], vec[posIndex + 2]);
		Vector vel(vec[velIndex + 0], vec[velIndex + 1], vec[velIndex + 2]);
		Vector facing(vec[faceIndex + 0], vec[faceIndex + 1], vec[faceIndex + 2]);

		float posScale	= MinMax(scales[posIndex] > 0.0f ? 1.0f : 0.0f, 0.0f, 1.0f);
		float faceScale = MinMax(scales[faceIndex] > 0.0f ? 1.0f : 0.0f, 0.0f, 1.0f);

		const Point posWs	  = refLocWs.TransformPoint(pos);
		const Vector velWs	  = refLocWs.TransformVector(vel);
		const Vector facingWs = refLocWs.TransformVector(facing);

		if (prevPos.Valid())
		{
			const Point prevPosWs = refLocWs.TransformPoint(prevPos.Get());

			g_prim.Draw(DebugCross(prevPosWs, 0.05f, clr, kPrimEnableHiddenLineAlpha), kPrimDuration1FrameNoRecord);

			g_prim.Draw(DebugLine(prevPosWs,
								  posWs,
								  clr,
								  3.0f * posScale,
								  kPrimDisableDepthTest), kPrimDuration1FrameNoRecord);

			if (faceScale > 0.0f)
			{
				g_prim.Draw(DebugArrow(posWs,
									   facingWs * 0.2f,
									   faceClr,
									   0.5f,
									   kPrimDisableDepthTest), kPrimDuration1FrameNoRecord);
			}
		}

		prevPos = pos;
	}

	if (prevPos.Valid())
	{
		const Point prevPosWs = refLocWs.TransformPoint(prevPos.Get());

		g_prim.Draw(DebugCross(prevPosWs, 0.05f, clr, kPrimEnableHiddenLineAlpha), kPrimDuration1FrameNoRecord);
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
static void DrawTrajectories(const Locator& refLocWs,
							 const MotionMatchingSet::AnimVector& query,
							 const MotionMatchingSet::AnimVector& sample,
							 const MotionMatchingSet::AnimVector& scales,
							 int trajStartIndex,
							 int numSamples)
{
	Maybe<Point> prev_q = MAYBE::kNothing;
	Maybe<Point> prev_s = MAYBE::kNothing;

	for (int i = 0; i < numSamples; i++)
	{
		int posIndex  = trajStartIndex + i * kNumGoalEntriesPerTrajSample;
		int velIndex  = trajStartIndex + i * kNumGoalEntriesPerTrajSample + 3;
		int faceIndex = trajStartIndex + i * kNumGoalEntriesPerTrajSample + 6;

		Point q(query[posIndex + 0], query[posIndex + 1], query[posIndex + 2]);
		Point s(sample[posIndex + 0], sample[posIndex + 1], sample[posIndex + 2]);

		Vector qVel(query[velIndex + 0], query[velIndex + 1], query[velIndex + 2]);
		Vector sVel(sample[velIndex + 0], sample[velIndex + 1], sample[velIndex + 2]);

		Vector qF(query[faceIndex + 0], query[faceIndex + 1], query[faceIndex + 2]);
		Vector sF(sample[faceIndex + 0], sample[faceIndex + 1], sample[faceIndex + 2]);

		float posScale	= MinMax(scales[posIndex] > 0.0f ? 1.0f : 0.0f, 0.0f, 1.0f);
		float faceScale = MinMax(scales[faceIndex] > 0.0f ? 1.0f : 0.0f, 0.0f, 1.0f);

		const Point queryWs = refLocWs.TransformPoint(q);
		const Point sampleWs = refLocWs.TransformPoint(s);

		const Vector queryFaceWs = refLocWs.TransformVector(qF);
		const Vector sampleFaceWs = refLocWs.TransformVector(sF);

		g_prim.Draw(DebugLine(queryWs,
							  sampleWs,
							  kColorGreen,
							  4.0f * posScale,
							  kPrimEnableHiddenLineAlpha), kPrimDuration1FrameNoRecord);

		if (prev_q.Valid())
		{
			const Point prevQueryWs = refLocWs.TransformPoint(prev_q.Get());

			g_prim.Draw(DebugCross(prevQueryWs, 0.05f, kColorBlueTrans, kPrimEnableHiddenLineAlpha), kPrimDuration1FrameNoRecord);

			g_prim.Draw(DebugLine(prevQueryWs,
								  queryWs,
								  kColorBlue,
								  3.0f * posScale,
								  kPrimEnableHiddenLineAlpha), kPrimDuration1FrameNoRecord);

			if (faceScale > 0.0f)
			{
				g_prim.Draw(DebugArrow(queryWs,
									   queryFaceWs * 0.2f,
									   kColorCyanTrans,
									   0.5f,
									   kPrimEnableHiddenLineAlpha), kPrimDuration1FrameNoRecord);
			}
			// g_prim.Draw(DebugArrow(queryWs, refLocWs.TransformVector(qVel), kColorBlue, 0.5f, kPrimDisableDepthTest), kPrimDuration1FrameNoRecord);
		}

		if (prev_s.Valid())
		{
			const Point prevSampleWs = refLocWs.TransformPoint(prev_s.Get());

			g_prim.Draw(DebugCross(prevSampleWs, 0.05f, kColorRedTrans, kPrimEnableHiddenLineAlpha), kPrimDuration1FrameNoRecord);

			g_prim.Draw(DebugLine(prevSampleWs,
								  sampleWs,
								  kColorRed,
								  3.0f * posScale,
								  kPrimEnableHiddenLineAlpha), kPrimDuration1FrameNoRecord);

			if (faceScale > 0.0f)
			{
				g_prim.Draw(DebugArrow(sampleWs,
									   sampleFaceWs * 0.2f,
									   kColorOrangeTrans,
									   0.5f,
									   kPrimEnableHiddenLineAlpha), kPrimDuration1FrameNoRecord);
			}
		}

		prev_q = q;
		prev_s = s;
	}

	if (prev_q.Valid())
	{
		const Point prevQueryWs = refLocWs.TransformPoint(prev_q.Get());

		g_prim.Draw(DebugCross(prevQueryWs, 0.05f, kColorBlueTrans, kPrimEnableHiddenLineAlpha), kPrimDuration1FrameNoRecord);
	}

	if (prev_s.Valid())
	{
		const Point prevSampleWs = refLocWs.TransformPoint(prev_s.Get());

		g_prim.Draw(DebugCross(prevSampleWs, 0.05f, kColorRedTrans, kPrimEnableHiddenLineAlpha), kPrimDuration1FrameNoRecord);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingSet::HandleExtraDebugSample(const Maybe<AnimSample>& extraDebugSample,
											   const ClosestEntryList& initialClosest,
											   const AnimVector& goalVec,
											   const AnimVector& scales,
											   const AnimVector& mins,
											   I32F* pDebugIndexOut,
											   bool* pDebugMirrorOut,
											   const ClosestEntryList** ppNewClosestOut) const
{
	*pDebugIndexOut	 = -1;
	*pDebugMirrorOut = false;
	*ppNewClosestOut = &initialClosest;

	if (!extraDebugSample.Valid())
	{
		return;
	}

	const MotionMatchingVectorTable& vecTable = VectorTable();

	AnimSample debugSample = extraDebugSample.Get();
	const bool debugExtraMirror = debugSample.Mirror();
	I32F debugExtraIndex = -1;

	const I32F iAnimIndex = GetAnimIndex(m_sampleTable, debugSample.GetAnimNameId());
	if (iAnimIndex < 0)
	{
		return;
	}

	if (debugSample.Frame() < 0.0f)
	{
		// search for best frame
		const MMAnimSampleRange& animSampleRange = m_sampleTable->m_aAnimRanges[iAnimIndex];

		const MMCAnimSample* pSamples = m_sampleTable->m_aSamples;
		const MMCAnimSample* animSampleStart = pSamples + animSampleRange.m_startIndex;
		const MMCAnimSample* animSampleEnd = animSampleStart + animSampleRange.m_count;

		float bestCost = kLargeFloat;

		for (const MMCAnimSample* pSample = animSampleStart; pSample != animSampleEnd; ++pSample)
		{
			const I32F vecIndex = pSample - pSamples;

			AnimVector sampleVector = vecTable[vecIndex];
			if (debugExtraMirror)
			{
				sampleVector = MirrorVector(sampleVector);
			}

			const float sampleCost = DistanceFunc(goalVec, sampleVector, scales, mins);

			if (sampleCost < bestCost)
			{
				bestCost = sampleCost;
				debugExtraIndex = vecIndex;
			}
		}
	}
	else
	{
		debugExtraIndex = GetClosestSampleInTable(m_sampleTable, extraDebugSample.Get());
	}

	if (debugExtraIndex < 0)
	{
		return;
	}

	*pDebugIndexOut = debugExtraIndex;
	*pDebugMirrorOut = debugExtraMirror;

	bool alreadyExists = false;
	for (U32F i = 0; i < initialClosest.Size(); ++i)
	{
		if ((initialClosest[i].m_vectorIndex == debugExtraIndex)
			&& (initialClosest[i].m_mirror == debugExtraMirror))
		{
			alreadyExists = true;
			break;
		}
	}

	if (alreadyExists)
	{
		return;
	}

	ClosestEntryList* pNewClosest = NDI_NEW ClosestEntryList(initialClosest.Capacity() + 1);

	for (const ClosestEntry& existing : initialClosest)
	{
		pNewClosest->PushBack(existing);
	}

	AnimVector sampleVector = vecTable[debugExtraIndex];
	if (debugExtraMirror)
	{
		sampleVector = MirrorVector(sampleVector);
	}

	ClosestEntry newEntry;
	newEntry.m_vectorIndex = debugExtraIndex;
	newEntry.m_mirror	   = debugExtraMirror;
	newEntry.m_cost		   = DistanceFunc(goalVec, sampleVector, scales, mins);

	pNewClosest->PushBack(newEntry);

	std::sort(pNewClosest->Begin(), pNewClosest->End());

	*ppNewClosestOut = pNewClosest;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingSet::DebugClosestSamples(const MMSearchParams& params,
											int debugIndex,
											Maybe<AnimSample> extraDebugSample,
											const Locator& refLocWs,
											Color32 c,
											const ClosestEntryList& initialClosest,
											const AnimVector& goalVec,
											const AnimVector& scales,
											const AnimVector& mins,
											const AnimVector& goalFilter) const
{
	STRIP_IN_FINAL_BUILD;

	const MotionMatchingVectorTable& vecTable = VectorTable();

	float* aLayerCostMods = STACK_ALLOC(float, m_numLayers);
	memset(aLayerCostMods, 0, sizeof(float) * m_numLayers);

	const BitArray64 layerBits = CreateValidLayerBits(params, aLayerCostMods);

	float t = EngineComponents::GetNdFrameState()->GetClock(kRealClock)->GetCurTime().ToSeconds();
	t = fmodf(t, m_pSettings->m_goals.m_maxTrajSampleTime + m_pSettings->m_goals.m_maxTrajSampleTimePrevTraj);

	I32F debugExtraIndex  = -1;
	bool debugExtraMirror = false;

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);
	const ClosestEntryList* pClosest = &initialClosest;

	HandleExtraDebugSample(extraDebugSample,
						   initialClosest,
						   goalVec,
						   scales,
						   mins,
						   &debugExtraIndex,
						   &debugExtraMirror,
						   &pClosest);

	const ClosestEntryList& closest = *pClosest;

	const float minCost = closest[0].m_cost;
	const float maxCost = closest[closest.Size() - 1].m_cost;

	const I32F numPoseDimensions  = GetNumPoseDimensions();
	const I32F numGoalDimensions  = GetNumGoalDimensions();
	const I32F numExtraDimensions = GetNumExtraDimensions();

	while (debugIndex < 0)
	{
		debugIndex += closest.Size();
	}
	debugIndex = debugIndex % closest.Size();
	int strLength = 60;
	
	bool hasCostBias = false;
	for (I32F i = 0; i < closest.Size(); ++i)
	{
		const ClosestEntry& entry = closest[i];
		if (entry.m_costBias > 0.0f)
		{
			hasCostBias = true;
			break;
		}
	}

	MsgConNotRecorded("%s", GetTextColorString(kTextColorNormal));
	MsgConNotRecorded("%-*s     %s |   %s %s)\n", (strLength - 3), "Anim", "Frame", "Cost   (   Pose   +     Goal  +     Extra +     Layer ", hasCostBias ? "-    Bias " : "");
	MsgConNotRecorded("---------------------------------------------------------------------------------------------------------------------\n");

	for (I32F i = 0; i < closest.Size(); ++i)
	{
		const ClosestEntry& entry = closest[i];

		const float cost		 = entry.m_cost;
		const I32F vectorIndex	 = entry.m_vectorIndex;
		const bool sampleMirror	 = entry.m_mirror;
		const I32F iLayer		 = m_pLayerIndexTable[vectorIndex];
		const StringId64 layerId = m_aLayerIds[iLayer];

		const AnimSample closeSample = AnimSampleFromIndex(vectorIndex, sampleMirror);
		const ArtItemAnim* pCloseAnim = closeSample.Anim().ToArtItem();

		StringBuilder<256> name;
		if (pCloseAnim)
		{
			name.append(pCloseAnim->GetName());
		}
		else
		{
			const StringId64 animId = AnimNameFromIndex(vectorIndex);
			name.append_format("%s <not loaded!>", DevKitOnly_StringIdToString(animId));
		}

		const I32F frame = closeSample.Frame();

		AnimVector sampleVector = vecTable[vectorIndex];

		if (sampleMirror)
		{
			sampleVector = MirrorVector(sampleVector);
		}

		AnimVector distVec = DistanceVec(goalVec, sampleVector, scales, mins);
		const float totalCost = DistanceFunc(distVec);

		const AnimVector distPose  = distVec.head(numPoseDimensions);
		const AnimVector distGoal  = distVec.segment(numPoseDimensions, numGoalDimensions);
		const AnimVector distExtra = distVec.segment(numPoseDimensions + numGoalDimensions, numExtraDimensions);

		const float poseCost  = distPose.squaredNorm();
		const float goalCost  = distGoal.squaredNorm();
		const float extraCost = distExtra.squaredNorm();
		const float layerCost = aLayerCostMods[iLayer];

		TextColor color = kTextColorNormal;

		if (i == debugIndex)
			color = kTextColorGreen;
		else if ((vectorIndex == debugExtraIndex) && (sampleMirror == debugExtraMirror))
			color = kTextColorCyan;

		MsgConNotRecorded("%s%-*s %s %3d  |  %6.3f  (%8.3f  +  %8.3f +  %8.3f +  %0.1f [%s] ",
						  GetTextColorString(color),
						  strLength,
						  name.c_str(),
						  closeSample.Mirror() ? "M" : " ",
						  frame,
						  cost,
						  poseCost,
						  goalCost,
						  extraCost,
						  layerCost,
						  DevKitOnly_StringIdToString(layerId));

		if (entry.m_costBias > 0.0f)
		{
			MsgConNotRecorded("-  %8.3f ", entry.m_costBias);
		}

		MsgConNotRecorded(")\n");
	}
	MsgConNotRecorded("%s", GetTextColorString(kTextColorNormal));

	const int vectorIndex	= closest[debugIndex].m_vectorIndex;
	const bool sampleMirror = closest[debugIndex].m_mirror;
	AnimVector sampleVector = vecTable[vectorIndex];

	if (sampleMirror)
	{
		sampleVector = MirrorVector(sampleVector);
	}

	{
		const AnimSample closeSample = AnimSampleFromIndex(vectorIndex, sampleMirror);
		const ArtItemAnim* pCloseAnim = closeSample.Anim().ToArtItem();

		StringBuilder<256> name;
		if (pCloseAnim)
		{
			name.append(pCloseAnim->GetName());
		}
		else
		{
			const StringId64 animId = AnimNameFromIndex(vectorIndex);
			name.append_format("%s <not loaded!>", DevKitOnly_StringIdToString(animId));
		}

		g_prim.Draw(DebugCoordAxesLabeled(refLocWs,
										  name.c_str(),
										  0.3f,
										  kPrimEnableHiddenLineAlpha,
										  2.0f,
										  kColorWhiteTrans,
										  0.5f), kPrimDuration1FrameNoRecord);

		const float poseDrawTime = t - m_pSettings->m_goals.m_maxTrajSampleTimePrevTraj;

		if (g_motionMatchingOptions.m_drawOptions.m_drawPoses)
		{
			MotionMatchingDebug::DrawFuturePose(closeSample, refLocWs, Color(c), poseDrawTime);
			MotionMatchingDebug::DrawFuturePose(closeSample, refLocWs, Color(c), 0);

			DrawPoseVector(goalVec, m_pSettings->m_pose, refLocWs, kColorBlue, 0.1f);
			DrawPoseVector(sampleVector, m_pSettings->m_pose, refLocWs, kColorRed, 0.08f);

			const U32F goalLocStartIndex = GetGoalLocDimensionIndex(0);

			DrawGoalLocators(goalVec, goalFilter, m_pSettings->m_goals, refLocWs, kColorBlue, goalLocStartIndex);
			DrawGoalLocators(sampleVector, goalFilter, m_pSettings->m_goals, refLocWs, kColorRed, goalLocStartIndex);
		}

		if (g_motionMatchingOptions.m_drawOptions.m_drawTrajectories)
		{
			const float stoppingFaceDist = m_pSettings->m_goals.m_stoppingFaceDist;
			Maybe<MMLocomotionState> futureState = ComputeLocomotionStateInFuture(closeSample, poseDrawTime, stoppingFaceDist);
			if (futureState.Valid())
			{
				Point pathPosWs = refLocWs.TransformPoint(futureState.Get().m_pathPosOs);
				g_prim.Draw(DebugSphere(pathPosWs, 0.1f, kColorRed), kPrimDuration1FrameNoRecord);
			}

			DrawTrajectories(refLocWs,
							 goalVec,
							 sampleVector,
							 scales,
							 numPoseDimensions,
							 m_pSettings->m_goals.m_numTrajSamples);
			
			DrawTrajectories(refLocWs,
							 goalVec,
							 sampleVector,
							 scales,
							 numPoseDimensions + m_pSettings->m_goals.m_numTrajSamples * kNumGoalEntriesPerTrajSample,
							 m_pSettings->m_goals.m_numTrajSamplesPrevTraj);
		}
	}

	{
		AnimVector settingsScales(m_numDimensions);
		AnimVector poseScale(m_numDimensions);
		AnimVector goalScale(m_numDimensions);
		AnimVector extraScale(m_numDimensions);
		AnimVector zero = AnimVector::Constant(m_numDimensions, 0.0f);
		AnimVector ones = AnimVector::Constant(m_numDimensions, 1.0f);
		AnimVector localMins(m_numDimensions);

		CreateScaleAndMinVectors(m_pSettings, settingsScales, localMins, &poseScale, &goalScale, &extraScale);

		AnimVector poseAndGoalWeights = ones;
		poseAndGoalWeights.head(numPoseDimensions).fill(m_pSettings->m_pose.m_masterWeight);
		poseAndGoalWeights.segment(numPoseDimensions, numGoalDimensions).fill(m_pSettings->m_goals.m_masterWeight);

		AnimVector poseScaleTest = scales.cwiseQuotient(poseAndGoalWeights);

		AnimVector diff = (((sampleVector - goalVec).array().abs() - localMins.array()).max(zero.array()) * (poseScaleTest).array()).matrix();

		// This math has to match the match in CompositeWeights() for the debugger to be consistent
		AnimVector poseAutoScale(m_numDimensions);
		AnimVector goalAutoScale(m_numDimensions);

		AnimVector autoWeights = GetAutoWeights();

		if (!m_pSettings->m_useNormalizedData)
		{
			autoWeights.setOnes();
		}

		if (m_pSettings->m_useNormalizedScales)
		{
			const float pose_variance_scale = poseScale.segment(0, numPoseDimensions).squaredNorm() / numPoseDimensions;
			const float goal_variance_scale = (goalScale.cwiseProduct(goalFilter)).segment(numPoseDimensions, numGoalDimensions).squaredNorm() / numGoalDimensions;

			const float poseAutoWeight = (1.0f / Sqrt(pose_variance_scale));
			const float goalAutoWeight = (1.0f / Sqrt(goal_variance_scale));
			
			//MsgCon("pose_variance_scale = %f\nposeAutoWeight = %f\n", pose_variance_scale, poseAutoWeight);

			poseAutoScale = autoWeights * poseAutoWeight;
			goalAutoScale = autoWeights * goalAutoWeight;

			poseAutoScale.segment(numPoseDimensions, numGoalDimensions + numExtraDimensions).fill(1.0f);

			goalAutoScale.segment(0, numPoseDimensions).fill(1.0f);
			goalAutoScale.segment(numPoseDimensions + numGoalDimensions, numExtraDimensions).fill(1.0f);
		}
		else 
		{
			poseAutoScale.setOnes();
			goalAutoScale.setOnes();

			poseAutoScale.segment(0, numPoseDimensions) = autoWeights.segment(0, numPoseDimensions);
			goalAutoScale.segment(numPoseDimensions, numGoalDimensions) = autoWeights.segment(numPoseDimensions, numGoalDimensions);
		}

		float debugPoseCost = 0.0f;

		PrintDebugCostLegend();

		//Data to alternate the text color on every line to make it easier to read the wall of text
		char colorTextBuffer[64];
		int lineColor = 0;
		Color lineColors[] = { kColorWhite, Color(0.75f, 0.75f, 0.75f) };

		int vIndex = 0;
		for (int iBody = 0; iBody < m_pSettings->m_pose.m_numBodies; ++iBody)
		{
			const MMPoseBody& body = m_pSettings->m_pose.m_aBodies[iBody];
			const char* pJointName = body.m_isCenterOfMass ? "COM" : DevKitOnly_StringIdToString(body.m_jointId);

			MsgConNotRecorded("%s",
							  GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)], colorTextBuffer));
			debugPoseCost += DebugCost(sampleVector,
									   goalVec,
									   poseScale,
									   goalFilter,
									   poseAutoScale,
									   localMins,
									   vIndex,
									   3,
									   StringBuilder<128>(" %s Pos", pJointName).c_str());
			vIndex += 3;

			MsgConNotRecorded("%s",
							  GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)], colorTextBuffer));
			debugPoseCost += DebugCost(sampleVector,
									   goalVec,
									   poseScale,
									   goalFilter,
									   poseAutoScale,
									   localMins,
									   vIndex,
									   3,
									   StringBuilder<128>(" %s Vel", pJointName).c_str());
			vIndex += 3;
		}
		MsgConNotRecorded("%s", GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)], colorTextBuffer));
		debugPoseCost += DebugCost(sampleVector,
								   goalVec,
								   poseScale,
								   goalFilter,
								   poseAutoScale,
								   localMins,
								   vIndex,
								   3,
								   " Facing");
		vIndex += 3;

		ANIM_ASSERT(vIndex == numPoseDimensions);

		MsgConNotRecorded("%s", GetTextColorString(kTextColorYellow));
		DebugCost(diff,
				  zero,
				  AnimVector::Constant(m_numDimensions, m_pSettings->m_pose.m_masterWeight),
				  zero,
				  0,
				  numPoseDimensions,
				  "Pose");
		MsgConNotRecorded("%s", GetTextColorString(kTextColorNormal));

		float debugGoalCost = 0.0f;

		for (int i = m_pSettings->m_goals.m_numTrajSamplesPrevTraj - 1; i >= 0; --i)
		{
			const float trajTime = -(m_pSettings->m_goals.m_maxTrajSampleTimePrevTraj * (i + 1))
								   / m_pSettings->m_goals.m_numTrajSamplesPrevTraj;
			const int offset = numPoseDimensions + (m_pSettings->m_goals.m_numTrajSamples + i) * kNumGoalEntriesPerTrajSample;

			if (goalFilter[offset] > 0.0f)
			{
				MsgConNotRecorded("%s", GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)], colorTextBuffer));
				debugGoalCost += DebugCost(sampleVector,
										   goalVec,
										   goalScale,
										   goalFilter,
										   goalAutoScale,
										   localMins,
										   offset,
										   3,
										   StringBuilder<128>(" Pos    @ %.2f", trajTime).c_str());
			}
			if (goalFilter[offset + 3] > 0.0f)
			{
				MsgConNotRecorded("%s", GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)], colorTextBuffer));
				debugGoalCost += DebugCost(sampleVector,
										   goalVec,
										   goalScale,
										   goalFilter,
										   goalAutoScale,
										   localMins,
										   offset + 3,
										   3,
										   StringBuilder<128>(" Vel    @ %.2f", trajTime).c_str());
			}
			if (goalFilter[offset + 6] > 0.0f)
			{
				MsgConNotRecorded("%s", GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)], colorTextBuffer));
				debugGoalCost += DebugCost(sampleVector,
										   goalVec,
										   goalScale,
										   goalFilter,
										   goalAutoScale,
										   localMins,
										   offset + 6,
										   3,
										   StringBuilder<128>(" Facing @ %.2f", trajTime).c_str());
			}
		}

		for (int i = 0; i < m_pSettings->m_goals.m_numTrajSamples; ++i)
		{
			const float trajTime = (m_pSettings->m_goals.m_maxTrajSampleTime * (i + 1))
								   / m_pSettings->m_goals.m_numTrajSamples;
			const int offset = numPoseDimensions + i * kNumGoalEntriesPerTrajSample;
			if (goalFilter[offset] > 0.0f)
			{
				MsgConNotRecorded("%s", GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)], colorTextBuffer));
				debugGoalCost += DebugCost(sampleVector,
										   goalVec,
										   goalScale,
										   goalFilter,
										   goalAutoScale,
										   localMins,
										   offset,
										   3,
										   StringBuilder<128>(" Pos    @ %.2f", trajTime).c_str());
			}
			if (goalFilter[offset + 3] > 0.0f)
			{
				MsgConNotRecorded("%s", GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)], colorTextBuffer));
				debugGoalCost += DebugCost(sampleVector,
										   goalVec,
										   goalScale,
										   goalFilter,
										   goalAutoScale,
										   localMins,
										   offset + 3,
										   3,
										   StringBuilder<128>(" Vel    @ %.2f", trajTime).c_str());
			}
			if (goalFilter[offset + 6] > 0.0f)
			{
				MsgConNotRecorded("%s", GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)], colorTextBuffer));
				debugGoalCost += DebugCost(sampleVector,
										   goalVec,
										   goalScale,
										   goalFilter,
										   goalAutoScale,
										   localMins,
										   offset + 6,
										   3,
										   StringBuilder<128>(" Facing @ %.2f", trajTime).c_str());
			}
			if (goalFilter[offset + 9] > 0.0f)
			{
				MsgConNotRecorded("%s", GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)], colorTextBuffer));
				debugGoalCost += DebugCost(sampleVector,
										   goalVec,
										   goalScale,
										   goalFilter,
										   goalAutoScale,
										   localMins,
										   offset + 9,
										   1,
										   StringBuilder<128>(" Yaw    @ %.2f", trajTime).c_str());
			}
		}

		for (int i = 0; i < m_pSettings->m_goals.m_numGoalLocators; ++i)
		{
			const I32F offset = GetGoalLocDimensionIndex(i);

			if (goalFilter[offset] <= 0.0f)
			{
				continue;
			}

			StringBuilder<128> desc;
			desc.format(" %s",
						DevKitOnly_StringIdToString(m_pSettings->m_goals.m_aGoalLocators[i].m_locatorId));

			MsgConNotRecorded("%s", GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)], colorTextBuffer));
			debugGoalCost += DebugCost(sampleVector,
									   goalVec,
									   goalScale,
									   goalFilter,
									   goalAutoScale,
									   localMins,
									   offset,
									   3,
									   desc.c_str());
		}

		MsgConNotRecorded("%s", GetTextColorString(kTextColorYellow));
		DebugCost(diff,
				  zero,
				  AnimVector::Constant(m_numDimensions, m_pSettings->m_goals.m_masterWeight),
				  zero,
				  numPoseDimensions,
				  numGoalDimensions,
				  "Goals");

		MsgConNotRecorded("%s", GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)], colorTextBuffer));
		debugGoalCost += DebugCost(sampleVector,
								   goalVec,
								   extraScale,
								   ones,
								   goalAutoScale,
								   localMins,
								   numPoseDimensions + numGoalDimensions + kExtraDimensionBias,
								   1,
								   " Bias");

		MsgConNotRecorded("%s", GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)], colorTextBuffer));
		debugGoalCost += DebugCost(sampleVector,
								   goalVec,
								   extraScale,
								   ones,
								   goalAutoScale,
								   localMins,
								   numPoseDimensions + numGoalDimensions + kExtraDimensionGroupId,
								   1,
								   StringBuilder<64>(" Grouping [%d]",
													 int(goalVec[numPoseDimensions + numGoalDimensions
																 + kExtraDimensionGroupId]))
									   .c_str());

		MsgConNotRecorded("%s", GetTextColorString(kTextColorYellow));
		
		AnimVector extraScales = AnimVector::Constant(m_numDimensions, 0.0f);
		extraScales.segment(numPoseDimensions + numGoalDimensions + kExtraDimensionBias, 1).fill(m_pSettings->m_goals.m_animBiasWeight);
		extraScales.segment(numPoseDimensions + numGoalDimensions + kExtraDimensionGroupId, 1).fill(m_pSettings->m_goals.m_groupingWeight);
		DebugCost(diff,
				  zero,
				  ones,
				  zero,
				  numPoseDimensions + numGoalDimensions,
				  numExtraDimensions,
				  "Extra");

		MsgConNotRecorded("%s", GetTextColorString(kTextColorNormal));
	}

	if (g_motionMatchingOptions.m_drawOptions.m_printPoseValues)
	{
		char colorTextBuffer[64];
		int lineColor = 0;
		Color lineColors[] ={kColorWhite, Color(0.75f, 0.75f, 0.75f)};

		const char* kValuesFormatStr	   = "%-18s %-36s %-36s %f\n";
		const char* kScalarValuesFormatStr = "%-18s %-36f %-36f %f\n";
		const char* kValuesFormatLabelStr  = "%-18s %-36s %-36s %s\n";

		MsgConNotRecorded("\n");
		MsgConNotRecorded(kValuesFormatLabelStr, "Category", "Goal", "Sample", "Err^2");
		MsgConNotRecorded("----------------------------------------------------------------------------------------------------------\n");

		int vIndex = 0;
		PrettyPrintFlags pfv = PrettyPrintFlags(kPrettyPrintLength | kPrettyPrintSmallNumbers);
		PrettyPrintFlags pfp = kPrettyPrintSmallNumbers;

		for (int iBody = 0; iBody < m_pSettings->m_pose.m_numBodies; ++iBody)
		{
			const MMPoseBody& body = m_pSettings->m_pose.m_aBodies[iBody];
			const char* pJointName = body.m_isCenterOfMass ? "COM" : DevKitOnly_StringIdToString(body.m_jointId);

			if (goalFilter[vIndex] > 0.0f)
			{
				const Point goalPosOs = GetPointFromAnimVector(goalVec, vIndex);
				const Point samplePosOs = GetPointFromAnimVector(sampleVector, vIndex);

				MsgConNotRecorded("%s",
								  GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)],
													 colorTextBuffer));
				MsgConNotRecorded(kValuesFormatStr,
								  StringBuilder<128>("%s Pos", pJointName).c_str(),
								  PrettyPrint(goalPosOs, pfp),
								  PrettyPrint(samplePosOs, pfp),
								  (float)DistSqr(goalPosOs, samplePosOs));
			}

			vIndex += 3;

			if (goalFilter[vIndex] > 0.0f)
			{
				const Vector goalVecOs = GetVectorFromAnimVector(goalVec, vIndex);
				const Vector sampleVecOs = GetVectorFromAnimVector(sampleVector, vIndex);

				MsgConNotRecorded("%s",
								  GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)],
													 colorTextBuffer));
				MsgConNotRecorded(kValuesFormatStr,
								  StringBuilder<128>("%s Vel", pJointName).c_str(),
								  PrettyPrint(goalVecOs, pfv),
								  PrettyPrint(sampleVecOs, pfv),
								  (float)LengthSqr(goalVecOs - sampleVecOs));
			}
			vIndex += 3;
		}

		if (goalFilter[vIndex] > 0.0f)
		{
			const Vector goalFacingOs = GetVectorFromAnimVector(goalVec, vIndex);
			const Vector sampleFacingOs = GetVectorFromAnimVector(sampleVector, vIndex);

			MsgConNotRecorded("%s",
							  GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)], colorTextBuffer));
			MsgConNotRecorded(kValuesFormatStr,
							  "Facing",
							  PrettyPrint(goalFacingOs, pfp),
							  PrettyPrint(sampleFacingOs, pfp),
							  (float)LengthSqr(goalFacingOs - sampleFacingOs));
		}

		for (int i = 0; i < m_pSettings->m_goals.m_numTrajSamplesPrevTraj; ++i)
		{
			const float trajTime = -(m_pSettings->m_goals.m_maxTrajSampleTimePrevTraj * (i + 1))
								   / m_pSettings->m_goals.m_numTrajSamplesPrevTraj;

			const int offset = numPoseDimensions
							   + (m_pSettings->m_goals.m_numTrajSamples + i) * kNumGoalEntriesPerTrajSample;

			const Point prevGoalPosOs		= GetPointFromAnimVector(goalVec, offset);
			const Point prevSamplePosOs		= GetPointFromAnimVector(sampleVector, offset);
			const Vector prevGoalVelOs		= GetVectorFromAnimVector(goalVec, offset + 3);
			const Vector prevSampleVelOs	= GetVectorFromAnimVector(sampleVector, offset + 3);
			const Vector prevGoalFacingOs	= GetVectorFromAnimVector(goalVec, offset + 6);
			const Vector prevSampleFacingOs = GetVectorFromAnimVector(sampleVector, offset + 6);

			const Point prevGoalPosWs		= refLocWs.TransformPoint(prevGoalPosOs);
			const Point prevSamplePosWs		= refLocWs.TransformPoint(prevSamplePosOs);
			const Vector prevGoalVelWs		= refLocWs.TransformVector(prevGoalVelOs);
			const Vector prevSampleVelWs	= refLocWs.TransformVector(prevSampleVelOs);
			const Vector prevGoalFacingWs	= refLocWs.TransformVector(prevGoalFacingOs);
			const Vector prevSampleFacingWs = refLocWs.TransformVector(prevSampleFacingOs);

			const float prevGoalYawSpeed   = goalVec[offset + 9];
			const float prevSampleYawSpeed = sampleVector[offset + 9];

			if (goalFilter[offset] > 0.0f)
			{
				g_prim.Draw(DebugCross(prevGoalPosWs, 0.2f, kColorOrange), kPrimDuration1FrameNoRecord);
				g_prim.Draw(DebugString(prevGoalPosWs,
										StringBuilder<128>("Pos @ %0.2f", trajTime).c_str(),
										kColorOrange,
										0.5f), kPrimDuration1FrameNoRecord);

				MsgConNotRecorded("%s",
								  GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)],
													 colorTextBuffer));
				MsgConNotRecorded(kValuesFormatStr,
								  StringBuilder<128>("Pos @ %0.2f", trajTime).c_str(),
								  PrettyPrint(prevGoalPosOs, pfp),
								  PrettyPrint(prevSamplePosOs, pfp),
								  (float)DistSqr(prevGoalPosOs, prevSamplePosOs));
			}

			if (goalFilter[offset + 3] > 0.0f)
			{
				g_prim.Draw(DebugArrow(prevGoalPosWs, prevGoalVelWs, kColorOrange), kPrimDuration1FrameNoRecord);

				MsgConNotRecorded("%s",
								  GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)],
													 colorTextBuffer));
				MsgConNotRecorded(kValuesFormatStr,
								  StringBuilder<128>("Vel @ %0.2f", trajTime).c_str(),
								  PrettyPrint(prevGoalVelOs, pfv),
								  PrettyPrint(prevSampleVelOs, pfv),
								  (float)LengthSqr(prevGoalVelOs - prevSampleVelOs));
			}

			if (goalFilter[offset + 6] > 0.0f)
			{
				g_prim.Draw(DebugArrow(prevGoalPosWs, prevGoalFacingWs, kColorGreenTrans), kPrimDuration1FrameNoRecord);

				MsgConNotRecorded("%s",
								  GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)],
													 colorTextBuffer));
				MsgConNotRecorded(kValuesFormatStr,
								  StringBuilder<128>("Facing @ %0.2f", trajTime).c_str(),
								  PrettyPrint(prevGoalFacingOs, pfp),
								  PrettyPrint(prevSampleFacingOs, pfp),
								  (float)LengthSqr(prevGoalFacingOs - prevSampleFacingOs));
			}

			if (goalFilter[offset + 9] > 0.0f)
			{
				g_prim.Draw(DebugArrow(prevGoalPosWs, prevGoalFacingWs, kColorGreenTrans), kPrimDuration1FrameNoRecord);

				MsgConNotRecorded("%s",
								  GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)],
													 colorTextBuffer));
				MsgConNotRecorded(kScalarValuesFormatStr,
								  StringBuilder<128>("Yaw Speed @ %0.2f", trajTime).c_str(),
								  prevGoalYawSpeed,
								  prevSampleYawSpeed,
								  (float)Sqr(prevGoalYawSpeed - prevSampleYawSpeed));
			}
		}

		for (int i = 0; i < m_pSettings->m_goals.m_numTrajSamples; ++i)
		{
			const float trajTime = (m_pSettings->m_goals.m_maxTrajSampleTime * (i + 1))
								   / m_pSettings->m_goals.m_numTrajSamples;

			const int offset = numPoseDimensions + i * kNumGoalEntriesPerTrajSample;

			const Point prevGoalPosOs		= GetPointFromAnimVector(goalVec, offset);
			const Point prevSamplePosOs		= GetPointFromAnimVector(sampleVector, offset);
			const Vector prevGoalVelOs		= GetVectorFromAnimVector(goalVec, offset + 3);
			const Vector prevSampleVelOs	= GetVectorFromAnimVector(sampleVector, offset + 3);
			const Vector prevGoalFacingOs	= GetVectorFromAnimVector(goalVec, offset + 6);
			const Vector prevSampleFacingOs = GetVectorFromAnimVector(sampleVector, offset + 6);

			const Point prevGoalPosWs		= refLocWs.TransformPoint(prevGoalPosOs);
			const Point prevSamplePosWs		= refLocWs.TransformPoint(prevSamplePosOs);
			const Vector prevGoalVelWs		= refLocWs.TransformVector(prevGoalVelOs);
			const Vector prevSampleVelWs	= refLocWs.TransformVector(prevSampleVelOs);
			const Vector prevGoalFacingWs	= refLocWs.TransformVector(prevGoalFacingOs);
			const Vector prevSampleFacingWs = refLocWs.TransformVector(prevSampleFacingOs);

			const float prevGoalYawSpeed   = goalVec[offset + 9];
			const float prevSampleYawSpeed = sampleVector[offset + 9];

			if (goalFilter[offset] > 0.0f)
			{
				g_prim.Draw(DebugCross(prevGoalPosWs, 0.2f, kColorOrange), kPrimDuration1FrameNoRecord);
				g_prim.Draw(DebugString(prevGoalPosWs,
										StringBuilder<128>("Pos @ %0.2f", trajTime).c_str(),
										kColorOrange,
										0.5f), kPrimDuration1FrameNoRecord);

				MsgConNotRecorded("%s",
								  GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)],
													 colorTextBuffer));
				MsgConNotRecorded(kValuesFormatStr,
								  StringBuilder<128>("Pos @ %0.2f", trajTime).c_str(),
								  PrettyPrint(prevGoalPosOs, pfp),
								  PrettyPrint(prevSamplePosOs, pfp),
								  (float)DistSqr(prevGoalPosWs, prevSamplePosWs));
			}
			if (goalFilter[offset + 3] > 0.0f)
			{
				g_prim.Draw(DebugArrow(prevGoalPosWs, prevGoalVelWs, kColorOrange), kPrimDuration1FrameNoRecord);

				MsgConNotRecorded("%s",
								  GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)],
													 colorTextBuffer));
				MsgConNotRecorded(kValuesFormatStr,
								  StringBuilder<128>("Vel @ %0.2f", trajTime).c_str(),
								  PrettyPrint(prevGoalVelOs, pfv),
								  PrettyPrint(prevSampleVelOs, pfv),
								  (float)LengthSqr(prevGoalVelWs - prevSampleVelWs));
			}

			if (goalFilter[offset + 6] > 0.0f)
			{
				g_prim.Draw(DebugArrow(prevGoalPosWs, prevGoalFacingWs, kColorGreenTrans), kPrimDuration1FrameNoRecord);

				MsgConNotRecorded("%s",
								  GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)],
													 colorTextBuffer));
				MsgConNotRecorded(kValuesFormatStr,
								  StringBuilder<128>("Facing @ %0.2f", trajTime).c_str(),
								  PrettyPrint(prevGoalFacingOs, pfp),
								  PrettyPrint(prevSampleFacingOs, pfp),
								  (float)LengthSqr(prevGoalFacingWs - prevSampleFacingWs));
			}

			if (goalFilter[offset + 9] > 0.0f)
			{
				g_prim.Draw(DebugArrow(prevGoalPosWs, prevGoalFacingWs, kColorGreenTrans), kPrimDuration1FrameNoRecord);

				MsgConNotRecorded("%s",
								  GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)],
													 colorTextBuffer));
				MsgConNotRecorded(kScalarValuesFormatStr,
								  StringBuilder<128>("Yaw Speed @ %0.2f", trajTime).c_str(),
								  prevGoalYawSpeed,
								  prevSampleYawSpeed,
								  (float)Sqr(prevGoalYawSpeed - prevSampleYawSpeed));
			}
		}

		for (int i = 0; i < m_pSettings->m_goals.m_numGoalLocators; ++i)
		{
			const I32F offset = GetGoalLocDimensionIndex(i);

			if (goalFilter[offset] <= 0.0f)
			{
				continue;
			}

			StringBuilder<128> desc;
			desc.format("%s",
						DevKitOnly_StringIdToString(m_pSettings->m_goals.m_aGoalLocators[i].m_locatorId));

			const Vector goalLocOs = GetVectorFromAnimVector(goalVec, offset);
			const Vector sampleLocOs = GetVectorFromAnimVector(sampleVector, offset);

			MsgConNotRecorded("%s",
							  GetTextColorString(lineColors[lineColor++ % ARRAY_COUNT(lineColors)], colorTextBuffer));

			MsgConNotRecorded(kValuesFormatStr,
							  desc.c_str(),
							  PrettyPrint(goalLocOs, pfp),
							  PrettyPrint(sampleLocOs, pfp),
							  (float)LengthSqr(goalLocOs - sampleLocOs));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingSet::DebugExternalAlternative(const MMSearchParams& params,
												 const AnimSample& altSample,
												 const char* name,
												 const AnimTrajectory& trajectory,
												 I32F poseGroupId,
												 I32F poseBias,
												 float costBias) const
{
	const I32F numPoseDimensions = GetNumPoseDimensions();
	const I32F numGoalDimensions = GetNumGoalDimensions();
	const I32F numExtraDimensions = GetNumExtraDimensions();

	AnimVector distVec(m_numDimensions);

	const float cost = ComputeGoalCostWithExtras(params, altSample, poseBias, poseGroupId, trajectory, &distVec)
					   - costBias;

	const AnimVector distPose  = distVec.head(numPoseDimensions);
	const AnimVector distGoal  = distVec.segment(numPoseDimensions, numGoalDimensions);
	const AnimVector distExtra = distVec.segment(numPoseDimensions + numGoalDimensions, numExtraDimensions);

	const float poseCost  = distPose.norm();
	const float goalCost  = distGoal.norm();
	const float extraCost = distExtra.norm();

	const float frame = altSample.Frame();

	const ArtItemAnim* pAnim = altSample.Anim().ToArtItem();
	StringBuilder<256> desc;
	if (pAnim)
	{
		desc.append_format("%s: %s", name, pAnim->GetName());
	}
	else
	{
		desc.append_format("%s: <unknown>", name);
	}

	if (Abs(costBias) > 0.0f)
	{
		MsgConNotRecorded("  %s%-*s %s %3d  |  %6.3f  (%8.3f  +  %8.3f +  %8.3f - %8.3f )\n",
						  GetTextColorString(kTextColorWhite),
						  58,
						  desc.c_str(),
						  altSample.Mirror() ? "M" : " ",
						  (int)frame,
						  cost,
						  poseCost,
						  goalCost,
						  extraCost,
						  costBias);
	}
	else
	{
		MsgConNotRecorded("  %s%-*s %s %3d  |  %6.3f  (%8.3f  +  %8.3f +  %8.3f )\n",
						  GetTextColorString(kTextColorWhite),
						  58,
						  desc.c_str(),
						  altSample.Mirror() ? "M" : " ",
						  (int)frame,
						  cost,
						  poseCost,
						  goalCost,
						  extraCost);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingSet::DebugClosestSamplesExternal(const MMSearchParams& params,
													const IMotionPose& pose,
													Maybe<AnimSample> maybePoseSample,
													const AnimTrajectory& trajectory,
													I32F poseGroupId,
													I32F poseBias,
													const AnimSampleBiased& extraSample,
													const int N,
													int debugIndex,
													Maybe<AnimSample> extraDebugSample,
													const DcArray<const DC::MotionMatchExternalFallback*>* pFallbacks,
													const Locator& refLoc,
													Color32 c) const
{
	STRIP_IN_FINAL_BUILD;
	
	const I32F numPoseDimensions = GetNumPoseDimensions();
	const I32F numGoalDimensions = GetNumGoalDimensions();
	const I32F numExtraDimensions = GetNumExtraDimensions();

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	AnimTrajectory poseTrajectory(m_pSettings->m_goals.m_numTrajSamples);

	if (maybePoseSample.Valid())
	{
		CreateTrajectoryFromSample(maybePoseSample.Get(), &poseTrajectory);
	}

	ClosestEntryList closest(N);
	AnimVector externalPoseVec(m_numDimensions);
	AnimVector scales(m_numDimensions);
	AnimVector mins(m_numDimensions);
	AnimVector goalFilter(m_numDimensions);
	AnimVector naturalPoseVec(m_numDimensions);
	AnimVector naturalPoseFilter(m_numDimensions);

	FindClosestSamplesExternal(params,
							   pose,
							   trajectory,
							   poseGroupId,
							   poseBias,
							   extraSample,
							   closest,
							   externalPoseVec,
							   scales,
							   mins,
							   goalFilter);

	if (maybePoseSample.Valid()
		&& CreateAnimVectorFromExternal(params,
										pose,
										poseTrajectory,
										poseGroupId,
										poseBias,
										&naturalPoseVec,
										&naturalPoseFilter))
	{
		const AnimSample& poseSample = maybePoseSample.Get();
		const ArtItemAnim* pPoseAnim = poseSample.Anim().ToArtItem();
		const char* pAnimName = pPoseAnim ? pPoseAnim->GetName() : "<null-anim>";
		const float frame = poseSample.Frame();

		AnimVector diffVec = (externalPoseVec - naturalPoseVec).array().abs();
		AnimVector distVec = DistanceVec(externalPoseVec, naturalPoseVec, scales, mins);

		const AnimVector distPose  = distVec.head(numPoseDimensions);
		const AnimVector distGoal  = distVec.segment(numPoseDimensions, numGoalDimensions);
		const AnimVector distExtra = distVec.segment(numPoseDimensions + numGoalDimensions, numExtraDimensions);

		const float poseCost  = distPose.norm();
		const float goalCost  = distGoal.norm();
		const float extraCost = distExtra.norm();

		const float cost = LinearDistance(externalPoseVec, naturalPoseVec, scales, mins);

		MsgConNotRecorded("%s%-*s %s %3d  |  %6.3f  (%8.3f  +  %8.3f +  %8.3f )\n",
						  GetTextColorString(kTextColorGreen),
						  60,
						  StringBuilder<256>("%s [External]", pAnimName).c_str(),
						  poseSample.Mirror() ? "M" : " ",
						  (int)frame,
						  cost,
						  poseCost,
						  goalCost,
						  extraCost);

		DrawTrajectory(refLoc,
					   naturalPoseVec,
					   scales,
					   numPoseDimensions,
					   m_pSettings->m_goals.m_numTrajSamples,
					   kColorOrange,
					   kColorOrangeTrans);
	}
	else
	{
		MsgConNotRecorded("%s%-*s\n", GetTextColorString(kTextColorGreen), 60, "[Pose Match]");
	}

	const ClosestEntry& toolResult = closest.At(0);
	const AnimSample toolSample = AnimSampleFromIndex(toolResult.m_vectorIndex, toolResult.m_mirror);

	if (maybePoseSample.Valid())
	{
		DebugExternalAlternative(params, maybePoseSample.Get(), "No Change", trajectory, poseGroupId, poseBias, 0.0f);
	}

	DebugExternalAlternative(params, toolSample, "Best Tool", trajectory, poseGroupId, poseBias, 0.0f);

	if (pFallbacks)
	{
		for (const DC::MotionMatchExternalFallback* pFallback : *pFallbacks)
		{
			const ArtItemAnimHandle hAnim = AnimMasterTable::LookupAnim(m_skelId,
																		m_hierarchyId,
																		pFallback->m_animName,
																		false);

			const ArtItemAnim* pAnim = hAnim.ToArtItem();
			if (!pAnim)
				continue;

			AnimSample fallbackSample = AnimSample(hAnim, 0.0f);

			DebugExternalAlternative(params,
									 fallbackSample,
									 "Fallback",
									 trajectory,
									 poseGroupId,
									 poseBias,
									 pFallback->m_costBias);
		}
	}

	DebugClosestSamples(params,
						debugIndex,
						extraDebugSample,
						refLoc,
						c,
						closest,
						externalPoseVec,
						scales,
						mins,
						goalFilter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct IndexedSearchJobParams
{
	const MotionMatchingIndex* m_pIndex = nullptr;
	const MotionMatchingSet::AnimVector* m_pQueryVector = nullptr;
	const MotionMatchingVectorTable* m_pVectorTable = nullptr;
	MotionMatchingIndex::Entry* m_pResult = nullptr;
};

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT_CLASS_DEFINE(MotionMatchingSet, FindClosestSampleIndexedJob)
{
	IndexedSearchJobParams* pParams = (IndexedSearchJobParams*)jobParam;

	*pParams->m_pResult = pParams->m_pIndex->GetClosest(*pParams->m_pQueryVector, *pParams->m_pVectorTable);
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionMatchingSet::ClosestEntry MotionMatchingSet::FindClosestSampleInternal(const AnimVector& queryVector,
																			 const AnimVector& scales,
																			 const AnimVector& minimum,
																			 bool mirrored,
																			 const BitArray64& layerBits,
																			 const float* aLayerCostMods) const
{
	ClosestEntry ret;

	if (layerBits.GetData() == 0ULL)
	{
		return ret;
	}

	if (TRUE_IN_FINAL_BUILD(!g_motionMatchingOptions.m_disablePrecomputedIndices))
	{
		const MotionMatchingVectorTable& vectorTable = VectorTable();
		const MotionMatchingIndex* pIndices = static_cast<const MotionMatchingIndex*>(m_aIndices);

		bool hasValidIndex = false;
		
		ret.m_cost = kLargeFloat;

		I32F* aIndicesToSearch = STACK_ALLOC(I32F, m_numIndices);
		U32F numIndicesToSearch = 0;

		for (int i = 0; i < m_numIndices; i++)
		{
			if (!pIndices[i].IsValid(scales, minimum))
				continue;

			hasValidIndex = true;

			const I32F layerIndex = GetLayerIndex(pIndices[i].m_layerId);

			if (!layerBits.IsBitSet(layerIndex))
				continue;

			aIndicesToSearch[numIndicesToSearch++] = i;
		}

		if (numIndicesToSearch > 1)
		{
			ndjob::JobDecl* jobDecls = NDI_NEW(kAllocSingleGameFrame, kAlign64) ndjob::JobDecl[numIndicesToSearch];
			ndjob::CounterHandle hAsyncCounter = ndjob::AllocateCounter(FILE_LINE_FUNC);
			IndexedSearchJobParams* pJobParams = NDI_NEW(kAllocSingleGameFrame) IndexedSearchJobParams[numIndicesToSearch];
			MotionMatchingIndex::Entry* pResults = NDI_NEW(kAllocSingleGameFrame) MotionMatchingIndex::Entry[numIndicesToSearch];

			I32F curIndex = 0;
			for (I32F iJob = 0; iJob < numIndicesToSearch; iJob++)
			{
				const U32F index = aIndicesToSearch[iJob];

				pJobParams[iJob].m_pIndex = &pIndices[index];
				pJobParams[iJob].m_pQueryVector = &queryVector;
				pJobParams[iJob].m_pVectorTable = &vectorTable;
				pJobParams[iJob].m_pResult = &pResults[iJob];

				jobDecls[iJob] = ndjob::JobDecl(FindClosestSampleIndexedJob, (uintptr_t)(&pJobParams[iJob]));
				jobDecls[iJob].m_associatedCounter = hAsyncCounter;
			}

			hAsyncCounter->SetValue(numIndicesToSearch);

			ndjob::JobArrayHandle JobArray = ndjob::BeginJobArray(numIndicesToSearch, ndjob::Priority::kAboveNormal);
			ndjob::AddJobs(JobArray, jobDecls, numIndicesToSearch);
			ndjob::CommitJobArray(JobArray);
			ndjob::WaitForCounterAndFree(hAsyncCounter);

			for (I32F iJob = 0; iJob < numIndicesToSearch; iJob++)
			{
				const MotionMatchingIndex::Entry& jobClosest = pResults[iJob];

				const U32F index = aIndicesToSearch[iJob];
				const U32F layerIndex = GetLayerIndex(pIndices[index].m_layerId);
				const float layerCostMod = aLayerCostMods[layerIndex];

				const float jobClosestCost = Sqr(jobClosest.first) + layerCostMod;

				if (jobClosestCost < ret.m_cost)
				{
					ret.m_cost = jobClosestCost;
					ret.m_vectorIndex = jobClosest.second;
					ret.m_mirror = mirrored;
				}
			}
		}
		else if (numIndicesToSearch == 1)
		{
			const U32F index = aIndicesToSearch[0];
			const I32F layerIndex = GetLayerIndex(pIndices[index].m_layerId);
			const float layerCostMod = aLayerCostMods[layerIndex];

			MotionMatchingIndex::Entry closest = pIndices[index].GetClosest(queryVector, vectorTable);
			const float closestCost = Sqr(closest.first) + layerCostMod;

			if (closestCost < ret.m_cost)
			{
				ret.m_cost = closestCost;
				ret.m_vectorIndex = closest.second;
				ret.m_mirror = mirrored;
			}
		}

		if (hasValidIndex)
		{
			return ret;
		}
	}

	return FindClosestSampleBrute(queryVector, scales, minimum, mirrored, layerBits, aLayerCostMods);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimSample MotionMatchingSet::AnimSampleFromIndex(int i, bool mirror) const
{
	ANIM_ASSERT(i >= 0 && i < m_sampleTable->m_numSamples);

	const MMCAnimSample compressedSample = m_sampleTable->m_aSamples[i];
	const StringId64 animId = m_sampleTable->m_aAnimIds[compressedSample.m_animIndex];

	ArtItemAnimHandle anim = AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, animId);
	// ANIM_ASSERT(anim.ToArtItem());

	AnimSample res = AnimSample();

	if (const ArtItemAnim* pAnim = anim.ToArtItem())
	{
		const float samplePhase = Limit01(compressedSample.m_sampleIndex / pAnim->m_pClipData->m_fNumFrameIntervals);
	
		res = AnimSample(anim, samplePhase, mirror);
	}

	return res;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 MotionMatchingSet::AnimNameFromIndex(I32F index) const
{
	ANIM_ASSERT(index >= 0 && index < m_sampleTable->m_numSamples);

	const MMCAnimSample compressedSample = m_sampleTable->m_aSamples[index];
	return m_sampleTable->m_aAnimIds[compressedSample.m_animIndex];
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F MotionMatchingSet::GetTableIndexFromSample(const AnimSample& sample) const
{
	return GetClosestSampleInTable(m_sampleTable, sample);
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionMatchingSet::AnimVectorMap MotionMatchingSet::GetAutoWeights() const
{
	return AnimVectorMap(m_autoWeights, m_numDimensions);
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F MotionMatchingSet::GetNumPoseDimensions() const
{
	static CONST_EXPR I32F kPosCount = 3;
	static CONST_EXPR I32F kVelCount = 3;
	static CONST_EXPR I32F kFacingCount = 3;

	return (m_pSettings->m_pose.m_numBodies * (kPosCount + kVelCount)) + kFacingCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F MotionMatchingSet::GetNumGoalDimensions() const
{
	static CONST_EXPR I32F kPosCount = 3;
	static CONST_EXPR I32F kVelCount = 3;
	static CONST_EXPR I32F kFacingCount = 3;
	static CONST_EXPR I32F kYawSpeedCount = 1;

	const size_t numTrajSamples = m_pSettings->m_goals.m_numTrajSamples + m_pSettings->m_goals.m_numTrajSamplesPrevTraj;
	const size_t numTrajDimensions = (numTrajSamples * (kPosCount + kVelCount + kFacingCount + kYawSpeedCount));

	const size_t numGoalLocDimensions = m_pSettings->m_goals.m_numGoalLocators * kPosCount;

	return numTrajDimensions + numGoalLocDimensions;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F MotionMatchingSet::GetGoalLocDimensionIndex(I32F iGoalLoc) const
{
	static CONST_EXPR I32F kPosCount = 3;

	const I32F numPoseDimensions = GetNumPoseDimensions();
	const I32F numGoalDimensions = GetNumGoalDimensions();

	const size_t numGoalLocDimensions = m_pSettings->m_goals.m_numGoalLocators * kPosCount;

	return numPoseDimensions + numGoalDimensions - numGoalLocDimensions + (iGoalLoc * kPosCount);
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionMatchingSet::AnimVector MotionMatchingSet::GetMirrorVector() const
{
	const I32F numPoseDimensions = GetNumPoseDimensions();
	const I32F numGoalDimensions = GetNumGoalDimensions();
	const I32F numTotalDimensions = GetTotalNumDimensions();

	AnimVector m(numTotalDimensions);
	m.fill(1.0f);

	int vIndex = 0;
	for (int iBody = 0; iBody < m_pSettings->m_pose.m_numBodies; iBody++)
	{
		m[vIndex] = -1.0f; //pos x
		vIndex += 3;
		m[vIndex] = -1.0f; //vel x
		vIndex += 3;
	}
	
	m[vIndex] = -1.0f; //facing x
	vIndex += 3;

	ANIM_ASSERT(vIndex == numPoseDimensions);

	vIndex = numPoseDimensions;
	for (int iGoal = 0; iGoal < m_pSettings->m_goals.m_numTrajSamples; iGoal++)
	{
		m[vIndex] = -1.0f; //pos x
		vIndex += 3;
		
		m[vIndex] = -1.0f; //vel x
		vIndex += 3;
		
		m[vIndex] = -1.0f; //facing x
		vIndex += 3;
		
		m[vIndex] = -1.0f; // yaw speed
		vIndex += 1;
	}

	for (int iGoal = 0; iGoal < m_pSettings->m_goals.m_numTrajSamplesPrevTraj; iGoal++)
	{
		m[vIndex] = -1.0f; //pos x
		vIndex += 3;

		m[vIndex] = -1.0f; //vel x
		vIndex += 3;

		m[vIndex] = -1.0f; //facing x
		vIndex += 3;

		m[vIndex] = -1.0f; // yaw speed
		vIndex += 1;
	}

	for (I32F iGoalLoc = 0; iGoalLoc < m_pSettings->m_goals.m_numGoalLocators; ++iGoalLoc)
	{
		const I32F xIndex = GetGoalLocDimensionIndex(iGoalLoc);
		
		m[xIndex] = -1.0f; // loc x
		vIndex += 3;
	}

	ANIM_ASSERT(vIndex == numPoseDimensions + numGoalDimensions);

	return m;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F MotionMatchingSet::GetBodyIndex(StringId64 jointId) const
{
	for (I32F iBody = 0; iBody < m_pSettings->m_pose.m_numBodies; iBody++)
	{
		if (m_pSettings->m_pose.m_aBodies[iBody].m_jointId == jointId)
		{
			return iBody;
		}
	}
	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F MotionMatchingSet::GetLayerIndex(StringId64 layerId) const
{
	for (U32F iLayer = 0; iLayer < m_numLayers; ++iLayer)
	{
		if (m_aLayerIds[iLayer] == layerId)
			return iLayer;
	}

	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BitArray64 MotionMatchingSet::CreateValidLayerBits(const MMSearchParams& params, float* aLayerCostMods) const
{
	BitArray64 ret;
	ret.ClearAllBits();

	for (U32F i = 0; i < params.m_numActiveLayers; ++i)
	{
		const StringId64 layerId = params.m_activeLayers[i];
		const I32F iLayer = GetLayerIndex(layerId);

		if (iLayer >= 0)
		{
			ret.SetBit(iLayer);
			aLayerCostMods[iLayer] = params.m_layerCostModifiers[i];
		}
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionMatchingSet::AnimVector MotionMatchingSet::MirrorVector(const AnimVector& v) const
{
	AnimVector m = v.cwiseProduct(GetMirrorVector());

	struct JointPair
	{
		StringId64 m_jointA = INVALID_STRING_ID_64;
		StringId64 m_jointB = INVALID_STRING_ID_64;
	};

	static JointPair s_mirrorJointPairs[] = {
		{ SID("r_ankle"), SID("l_ankle") },
		{ SID("r_palm"), SID("l_palm") },
		{ SID("r_hand"), SID("l_hand") },
		{ SID("r_wrist"), SID("l_wrist") },
	};

	// Need to swap pose values for the mirrored joint pairs
	for (I32F iPair = 0; iPair < ARRAY_COUNT(s_mirrorJointPairs); iPair++)
	{
		const I32F bodyIndexA = GetBodyIndex(s_mirrorJointPairs[iPair].m_jointA);
		if (bodyIndexA < 0)
			continue;

		const I32F bodyIndexB = GetBodyIndex(s_mirrorJointPairs[iPair].m_jointB);

		if (bodyIndexB < 0)
			continue;

		//Swap the segments for the joints
		m.segment<6>(bodyIndexA * 6).swap(m.segment<6>(bodyIndexB * 6));
	}

	return m;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingSet::FindClosestSamplesExisting(const MMSearchParams& params,
												   const AnimSampleBiased& sample,
												   const AnimTrajectory& trajectory,
												   I32F desiredGroupId,
												   const AnimSampleBiased& extraSample,
												   ClosestEntryList& closest,
												   AnimVector& v,
												   AnimVector& scales,
												   AnimVector& minimum,
												   AnimVector& goalFilterVec) const
{
	PROFILE_AUTO(Animation);

	closest.Clear();
	I32F sampleIndex = -1;

	if (CreateAnimVectorFromExisting(params, sample, trajectory, &v, &goalFilterVec, &sampleIndex))
	{
		scales = CompositeWeights(goalFilterVec, &minimum);

		const I32F numPoseDimensions = GetNumPoseDimensions();
		const I32F numGoalDimensions = GetNumGoalDimensions();

		v[numPoseDimensions + numGoalDimensions + kExtraDimensionGroupId] = float(desiredGroupId);
		v[numPoseDimensions + numGoalDimensions + kExtraDimensionBias]	  = 0.0f;

		const I32F iLayer		 = m_pLayerIndexTable[sampleIndex];
		const StringId64 layerId = m_aLayerIds[iLayer];

		float layerCost = 0.0f;
		if (params.IsLayerActive(layerId, &layerCost))
		{
			const MotionMatchingVectorTable& vecTable = VectorTable();
			const AnimVector sampleVec = sample.Mirror() ? MirrorVector(vecTable[sampleIndex]) : vecTable[sampleIndex];

			const float naturalBias = sample.CostBias();
			const float naturalDist = DistanceFunc(sampleVec, v, scales, minimum);
			const float naturalCost = naturalDist - naturalBias + layerCost;

			ClosestEntry natural;
			natural.m_cost		  = naturalCost;
			natural.m_vectorIndex = sampleIndex;
			natural.m_mirror	  = sample.Mirror();
			natural.m_costBias	  = naturalBias;

			closest.PushBack(natural);
		}

		ClosestEntry extraEntry = ConsiderExtraSample(extraSample, ClosestEntry(), v, scales, minimum);

		if (extraEntry.m_vectorIndex >= 0)
		{
			closest.PushBack(extraEntry);
		}

		FindClosestSamples(params, v, scales, minimum, sampleIndex, closest);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingSet::FindClosestSamplesExternal(const MMSearchParams& params,
												   const IMotionPose& pose,
												   const AnimTrajectory& poseTrajectory,
												   I32F poseGroupId,
												   I32F poseBias,
												   const AnimSampleBiased& extraSample,
												   ClosestEntryList& closest,
												   AnimVector& v,
												   AnimVector& scales,
												   AnimVector& minimum,
												   AnimVector& goalFilterVec) const
{
	PROFILE_AUTO(Animation);
	
	closest.Clear();

	if (CreateAnimVectorFromExternal(params, pose, poseTrajectory, poseGroupId, poseBias, &v, &goalFilterVec))
	{
		scales = CompositeWeights(goalFilterVec, &minimum);

		ClosestEntry extraEntry = ConsiderExtraSample(extraSample, ClosestEntry(), v, scales, minimum);

		if (extraEntry.m_vectorIndex >= 0)
		{
			closest.PushBack(extraEntry);
		}

		FindClosestSamples(params, v, scales, minimum, -1, closest);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingSet::FindClosestSamples(const MMSearchParams& params,
										   const AnimVector& v,
										   const AnimVector& scales,
										   const AnimVector& minimum,
										   I32F ignoreIndex,
										   ClosestEntryList& closest) const
{
	const MotionMatchingVectorTable& vectorTable = VectorTable();

	float* aLayerCostMods = STACK_ALLOC(float, m_numLayers);
	memset(aLayerCostMods, 0, sizeof(float) * m_numLayers);

	const BitArray64 layerBits = CreateValidLayerBits(params, aLayerCostMods);

	const AnimVector mv	  = MirrorVector(v);
	const AnimVector* q[] = { &v, &mv };
	const int mirrorCount = (params.m_mirrorMode == MMMirrorMode::kAllow) ? ARRAY_COUNT(q) : 1;
	const bool forcedMirror = params.m_mirrorMode == MMMirrorMode::kForced;

	// Try the indices
	if (TRUE_IN_FINAL_BUILD(!g_motionMatchingOptions.m_disablePrecomputedIndices))
	{
		bool hasValidIndex = false;

		ScopedTempAllocator alloc(FILE_LINE_FUNC);
		ListArray<MotionMatchingIndex::Entry> kNearest(closest.Capacity());

		const MotionMatchingIndex* pIndices = static_cast<const MotionMatchingIndex*>(m_aIndices);
		for (I32F i = 0; i < m_numIndices; i++)
		{
			if (!pIndices[i].IsValid(scales, minimum))
			{
				continue;
			}

			hasValidIndex = true;

			const I32F iLayer = GetLayerIndex(pIndices[i].m_layerId);
			if (!layerBits.IsBitSet(iLayer))
				continue;

			const float layerCostMod = aLayerCostMods[iLayer];

			for (int m = 0; m < mirrorCount; m++)
			{
				const bool mirror = (m > 0) || forcedMirror;
				const AnimVector* pQuery = mirror ? q[1] : q[0];

				pIndices[i].GetClosestK(*pQuery, vectorTable, kNearest);

				for (const MotionMatchingIndex::Entry& neighbor : kNearest)
				{
					if (neighbor.second == ignoreIndex)
						continue;

					ClosestEntry current;
					current.m_cost = Sqr(neighbor.first) + layerCostMod;
					current.m_vectorIndex = neighbor.second;
					current.m_mirror = mirror;

					if (!closest.IsFull())
					{
						closest.PushBack(current);
						std::sort(closest.Begin(), closest.End());
					}
					else if (current < closest.Back())
					{
						closest[closest.Size() - 1] = current;
						std::sort(closest.Begin(), closest.End());
					}
				}
			}
		}

		if (hasValidIndex)
		{
			return;
		}
	}

	for (I32F i = 0; i < vectorTable.Size(); ++i)
	{
		if (i == ignoreIndex)
			continue;

		const I32F iLayer = m_pLayerIndexTable[i];
		
		if (!layerBits.IsBitSet(iLayer))
			continue;

		const float layerCostMod = aLayerCostMods[iLayer];

		for (I32F m = 0; m < mirrorCount; m++)
		{
			const bool mirror = (m > 0) || forcedMirror;
			const AnimVector* pQuery = mirror ? q[1] : q[0];

			// Brute force
			const AnimVectorMap& vecTableEntry = vectorTable[i];

			const float curDist = DistanceFunc(vecTableEntry, *pQuery, scales, minimum);

			ClosestEntry entry;
			entry.m_cost = curDist + layerCostMod;
			entry.m_vectorIndex = i;
			entry.m_mirror = mirror;
			
			if (!closest.IsFull())
			{
				closest.PushBack(entry);
				std::sort(closest.Begin(), closest.End());
			}
			else if (closest[closest.Size() - 1].m_cost > entry.m_cost)
			{
				closest[closest.Size() - 1] = entry;
				std::sort(closest.Begin(), closest.End());
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MotionMatchingSet::CreateAnimVectorFromExisting(const MMSearchParams& params,
													 const AnimSample& animSample,
													 const AnimTrajectory& trajectory,
													 AnimVector* pVectorOut,
													 AnimVector* pGoalFilterOut,
													 I32F* pTableIndexOut /* = nullptr */) const
{
	ANIM_ASSERT(pVectorOut);
	ANIM_ASSERT(pGoalFilterOut);

	const size_t numPoseDimensions = GetNumPoseDimensions();
	const size_t numGoalDimensions = GetNumGoalDimensions();
	const size_t extraDimensionsOffset = numPoseDimensions + numGoalDimensions;

	pVectorOut->setZero();
	pGoalFilterOut->setOnes();

	const I32F index = GetClosestSampleInTable(m_sampleTable, animSample);

	if (index < 0)
	{
		return false;
	}

	const MotionMatchingVectorTable::AnimVectorMap& v = VectorTable()[index];

	// Copy the pose & extra dimension sections of the vector in the table that is closest to it
	pVectorOut->head(numPoseDimensions) = v.head(numPoseDimensions);

	if (animSample.Mirror())
	{
		*pVectorOut = MirrorVector(*pVectorOut);
	}

	TrajectoryToVector(trajectory, &m_pSettings->m_goals, numPoseDimensions, *pGoalFilterOut, *pVectorOut);

	GoalLocatorsToVector(params, *pGoalFilterOut, *pVectorOut);

	pVectorOut->segment(extraDimensionsOffset, kNumExtraDimensions) = v.segment(extraDimensionsOffset, kNumExtraDimensions);

	if (pTableIndexOut)
	{
		*pTableIndexOut = index;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MotionMatchingSet::CreateAnimVectorFromExternal(const MMSearchParams& params,
													 const IMotionPose& pose,
													 const AnimTrajectory& trajectory,
													 I32F poseGroupId,
													 I32F poseBias,
													 AnimVector* pVectorOut,
													 AnimVector* pGoalFilterOut) const
{
	ANIM_ASSERT(pVectorOut);
	ANIM_ASSERT(pGoalFilterOut);

	const size_t numPoseDimensions	   = GetNumPoseDimensions();
	const size_t numGoalDimensions	   = GetNumGoalDimensions();
	const size_t extraDimensionsOffset = numPoseDimensions + numGoalDimensions;

	pVectorOut->setZero();
	pGoalFilterOut->setOnes();

	const I32F index = MotionPoseToVector(pose, *pVectorOut, this);

	if (!TrajectoryToVector(trajectory, &m_pSettings->m_goals, index, *pGoalFilterOut, *pVectorOut))
	{
		return false;
	}

	GoalLocatorsToVector(params, *pGoalFilterOut, *pVectorOut);

	pVectorOut->segment(extraDimensionsOffset + kExtraDimensionBias, 1).fill(float(poseBias));
	pVectorOut->segment(extraDimensionsOffset + kExtraDimensionGroupId, 1).fill(float(poseGroupId));

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Convert the Pose and Trajectory weights into Vectors.
// This could be precomputed in the tools
/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingSet::CreateScaleAndMinVectors(const MMSettings* pSettings,
												 AnimVector& scalesOut,
												 AnimVector& minsOut,
												 AnimVector* pPoseScales /* = nullptr */,
												 AnimVector* pGoalScales /* = nullptr */,
												 AnimVector* pExtraScales /* = nullptr */)
{
	scalesOut.setZero();
	minsOut.setZero();

	AnimVector poseScale(scalesOut.rows()), goalScale(scalesOut.rows()), extraScale(scalesOut.rows());
	poseScale.setZero();
	goalScale.setZero();
	extraScale.setZero();

	I32F  index = 0;
	const MMPose* pPose = &pSettings->m_pose;

	for (I32F i = 0; i < pPose->m_numBodies; i++)
	{
		const MMPoseBody& body = pPose->m_aBodies[i];
		poseScale.segment<3>(index).fill(body.m_positionWeight);
		scalesOut.segment<3>(index).fill(body.m_positionWeight * pPose->m_masterWeight);
		index += 3;

		poseScale.segment<3>(index).fill(body.m_velocityWeight);
		scalesOut.segment<3>(index).fill(body.m_velocityWeight * pPose->m_masterWeight);
		index += 3;
	}
	poseScale.segment<3>(index).fill(pPose->m_facingWeight);
	scalesOut.segment<3>(index).fill(pPose->m_facingWeight * pPose->m_masterWeight);
	index += 3;

	const MMGoals* pGoalWeights = &pSettings->m_goals;
	
	for (I32F i = 0; i < pGoalWeights->m_numTrajSamples; ++i)
	{
		const bool tailSample = i == (pGoalWeights->m_numTrajSamples - 1);

		float discount = 1.0f;
		float weight = pGoalWeights->m_masterWeight * discount;

		goalScale.segment<3>(index).fill(pGoalWeights->m_positionWeight * discount);
		scalesOut.segment<3>(index).fill(weight * pGoalWeights->m_positionWeight);
		index += 3;

		goalScale.segment<3>(index).fill(pGoalWeights->m_velocityWeight * discount);
		scalesOut.segment<3>(index).fill(weight * pGoalWeights->m_velocityWeight);
		index += 3;

		const float directionalWeight = tailSample ? pGoalWeights->m_directionalWeight : pGoalWeights->m_interimDirectionalWeight;
		goalScale.segment<3>(index).fill(directionalWeight * discount);
		scalesOut.segment<3>(index).fill(directionalWeight * weight);
		index += 3;

		goalScale[index] = pGoalWeights->m_yawSpeedWeight * discount;
		scalesOut[index] = weight * pGoalWeights->m_yawSpeedWeight;
		index += 1;
	}

	for (I32F i = 0; i < pGoalWeights->m_numTrajSamplesPrevTraj; ++i)
	{
		const bool tailSample = i == (pGoalWeights->m_numTrajSamplesPrevTraj - 1);

		float discount = 1.0f;
		float weight = pGoalWeights->m_masterWeight * discount;

		goalScale.segment<3>(index).fill(pGoalWeights->m_positionWeight * pGoalWeights->m_prevTrajWeight * discount);
		scalesOut.segment<3>(index).fill(weight * pGoalWeights->m_positionWeight * pGoalWeights->m_prevTrajWeight);
		index += 3;

		goalScale.segment<3>(index).fill(pGoalWeights->m_velocityWeight * pGoalWeights->m_prevTrajWeight * discount);
		scalesOut.segment<3>(index).fill(weight * pGoalWeights->m_velocityWeight * pGoalWeights->m_prevTrajWeight);
		index += 3;

		const float directionalWeight = tailSample ? pGoalWeights->m_directionalWeight : pGoalWeights->m_interimDirectionalWeight;
		goalScale.segment<3>(index).fill(directionalWeight * pGoalWeights->m_prevTrajWeight * discount);
		scalesOut.segment<3>(index).fill(directionalWeight * pGoalWeights->m_prevTrajWeight * weight);
		index += 3;

		goalScale[index] = pGoalWeights->m_yawSpeedWeight * discount * pGoalWeights->m_prevTrajWeight;
		scalesOut[index] = weight * pGoalWeights->m_yawSpeedWeight * pGoalWeights->m_prevTrajWeight;
		index += 1;
	}

	for (U32F i = 0; i < pGoalWeights->m_numGoalLocators; ++i)
	{
		goalScale.segment<3>(index).fill(pGoalWeights->m_aGoalLocators[i].m_goalLocWeight);
		scalesOut.segment<3>(index).fill(pGoalWeights->m_aGoalLocators[i].m_goalLocWeight);
		minsOut.segment<3>(index).fill(pGoalWeights->m_aGoalLocators[i].m_minGoalDist);
		index += 3;
	}

	extraScale[index]  = pGoalWeights->m_animBiasWeight;
	scalesOut[index++] = pGoalWeights->m_animBiasWeight;

	extraScale[index]  = pGoalWeights->m_groupingWeight;
	scalesOut[index++] = pGoalWeights->m_groupingWeight;

	if (pPoseScales)
	{
		*pPoseScales = poseScale;
	}

	if (pGoalScales)
	{
		*pGoalScales = goalScale;
	}

	if (pExtraScales)
	{
		*pExtraScales = extraScale;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionMatchingSet::ClosestEntry MotionMatchingSet::FindClosestSampleBrute(const AnimVector& queryVector,
																		  const AnimVector& scales,
																		  const AnimVector& minimum,
																		  bool mirrored,
																		  const BitArray64& layerBits,
																		  const float* aLayerCostMods) const
{
	ClosestEntry ret;

	if (layerBits.GetData() == 0ULL)
	{
		return ret;
	}

	bool useJobs   = true;
	float bestCost = kLargestFloat;
	int bestIndex  = -1;

	const MotionMatchingVectorTable& vecTable = VectorTable();
	const size_t vecTableSize = vecTable.Size();

	if (useJobs)
	{
		const size_t numThreads = Max(ndjob::GetNumWorkerThreads() - 1, U64(1));

		ndjob::JobDecl* jobDecls = NDI_NEW(kAllocSingleGameFrame, kAlign64) ndjob::JobDecl[numThreads];
		ndjob::CounterHandle hAsyncCounter = ndjob::AllocateCounter(FILE_LINE_FUNC);
		SearchJobParams* pJobParams		   = NDI_NEW(kAllocSingleGameFrame) SearchJobParams[numThreads];
		ClosestEntry* pResults = NDI_NEW(kAllocSingleGameFrame) ClosestEntry[numThreads];

		I32F curIndex = 0;
		for (I32F iJob = 0; iJob < numThreads; iJob++)
		{
			const I32F nextIndex = Min(curIndex + (vecTableSize + numThreads - 1) / numThreads, vecTableSize - 1);

			pJobParams[iJob].m_goalVec		  = queryVector;
			pJobParams[iJob].m_scales		  = scales;
			pJobParams[iJob].m_minDiff		  = minimum;
			pJobParams[iJob].m_layerBits	  = layerBits;
			pJobParams[iJob].m_aLayerCostMods = aLayerCostMods;
			pJobParams[iJob].m_pMatcher		  = this;
			pJobParams[iJob].m_startIndex	  = curIndex;
			pJobParams[iJob].m_numIter		  = nextIndex - pJobParams[iJob].m_startIndex;
			pJobParams[iJob].m_pResult		  = &pResults[iJob];

			jobDecls[iJob] = ndjob::JobDecl(FindClosestSampleJob, (uintptr_t)(&pJobParams[iJob]));
			jobDecls[iJob].m_associatedCounter = hAsyncCounter;

			curIndex = nextIndex;
		}

		hAsyncCounter->SetValue(numThreads);

		ndjob::JobArrayHandle JobArray = ndjob::BeginJobArray(numThreads);
		ndjob::AddJobs(JobArray, jobDecls, numThreads);
		ndjob::CommitJobArray(JobArray);
		ndjob::WaitForCounterAndFree(hAsyncCounter);

		for (int i = 0; i < numThreads; i++)
		{
			if (pResults[i].m_cost < bestCost)
			{
				bestCost  = pResults[i].m_cost;
				bestIndex = pResults[i].m_vectorIndex;
			}
		}
	}
	else
	{
		for (int i = 0; i < vecTableSize; ++i)
		{
			const AnimVectorMap& vecTableEntry = vecTable[i];

			const I32F iLayer = m_pLayerIndexTable[i];

			if (!layerBits.IsBitSet(iLayer))
				continue;

			const float layerCostMod = aLayerCostMods[iLayer];

			const float distSqr = DistanceFunc(vecTableEntry, queryVector, scales, minimum);
			const float curCost = Sqrt(distSqr) + layerCostMod;

			if (curCost < bestCost)
			{
				bestIndex = i;
				bestCost  = curCost;
			}
		}
	}

	ANIM_ASSERT(bestIndex >= 0);
	ANIM_ASSERT(bestIndex < vecTableSize);

	ret.m_cost		  = bestCost;
	ret.m_vectorIndex = bestIndex;
	ret.m_mirror	  = mirrored;

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const MotionMatchingVectorTable& MotionMatchingSet::VectorTable() const
{
	return *static_cast<const MotionMatchingVectorTable*>(m_pVectorTable);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingSet::ValidateEffData() const
{
	STRIP_IN_FINAL_BUILD;
	
	ScopedTempAllocator alloc(FILE_LINE_FUNC);
	EffectList effectList;
	effectList.Init(256);
	for (U32F iAnim = 0; iAnim < m_sampleTable->m_numAnims; ++iAnim)
	{
		const StringId64 animId = m_sampleTable->m_aAnimIds[iAnim];

		if (const ArtItemAnim* pAnim = AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, animId).ToArtItem())
		{
			const float maxFrameSample		 = pAnim->m_pClipData->m_fNumFrameIntervals;
			const float mayaFramesCompensate = 30.0f * pAnim->m_pClipData->m_secondsPerFrame;
			const float maxMayaFrameIndex	 = maxFrameSample * mayaFramesCompensate;
			const bool isLooping   = (pAnim->m_flags & ArtItemAnim::kLooping) != 0;
			const float startPhase = 0.0f;
			const float endPhase   = 1.0f;

			effectList.Clear();

			EffectGroup::GetTriggeredEffects(pAnim->m_pEffectAnim,
											 startPhase * maxMayaFrameIndex,
											 endPhase * maxMayaFrameIndex,
											 maxMayaFrameIndex,
											 true, // playingForward
											 isLooping,
											 false, // isFlipped
											 true,	// isTopState
											 1.0f,
											 0.0f,
											 SID("s_motion-match-locomotion"),
											 &effectList,
											 nullptr);

			bool hasValidFootEffects = false;

			// Verify that anim has valid foot-effect EFFs
			for (U32F iEffect = 0; iEffect < effectList.GetNumEffects(); ++iEffect)
			{
				const EffectAnimInfo* pInfo = effectList.Get(iEffect);
				ANIM_ASSERT(pInfo && pInfo->m_pEffect);

				if (pInfo->m_pEffect->GetNameId() == SID("foot-effect"))
				{
					hasValidFootEffects = true;
					break;
				}
			}

			if (!hasValidFootEffects)
			{
				MsgAnimErr("Motion matching anim '%s' has no valid 'foot-effect' EFFs.\n", pAnim->GetName());
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingSet::ValidateSampleIndices(const char* setName) const
{
	STRIP_IN_FINAL_BUILD;

	const MotionMatchingVectorTable& vecTable = VectorTable();
	const size_t tableSize = vecTable.Size();

	for (U32F i = 0; i < tableSize; ++i)
	{
		const MMCAnimSample compressedSample = m_sampleTable->m_aSamples[i];
		const StringId64 animId = m_sampleTable->m_aAnimIds[compressedSample.m_animIndex];

		ArtItemAnimHandle anim = AnimMasterTable::LookupAnim(m_skelId, m_hierarchyId, animId);

		if (const ArtItemAnim* pAnim = anim.ToArtItem())
		{
			const float samplePhase = compressedSample.m_sampleIndex / pAnim->m_pClipData->m_fNumFrameIntervals;

			ANIM_ASSERTF(samplePhase >= 0.0f && samplePhase <= 1.0f,
						 ("MotionMatching set '%s' refers to out of bounds sample index '%d' for anim '%s' (max: %d)",
						  setName,
						  compressedSample.m_sampleIndex,
						  pAnim->m_pClipData->m_numTotalFrames - 1));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MotionMatchingSet::HasRequiredJointsForSet(const NdGameObject* pGameObj) const
{
	if (!pGameObj)
	{
		return false;
	}

	if (!m_pSettings)
	{
		return true;
	}

	for (U32F i = 0; i < m_pSettings->m_pose.m_numBodies; ++i)
	{
		if (m_pSettings->m_pose.m_aBodies[i].m_isCenterOfMass)
		{
			continue;
		}

		if (pGameObj->FindJointIndex(m_pSettings->m_pose.m_aBodies[i].m_jointId) < 0)
		{
			return false;
		}
	}

	if ((m_pSettings->m_pose.m_facingJointId != INVALID_STRING_ID_64)
		&& (pGameObj->FindJointIndex(m_pSettings->m_pose.m_facingJointId) < 0))
	{
		return false;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionMatchingSet::ClosestEntry MotionMatchingSet::ConsiderExtraSample(const AnimSampleBiased& extraSample,
																	   const ClosestEntry& curClosest,
																	   const AnimVector& query,
																	   const AnimVector& scales,
																	   const AnimVector& minimum) const
{
	if (extraSample.Anim().IsNull())
	{
		return curClosest;
	}

	const I32F biasIndex = GetClosestSampleInTable(m_sampleTable, extraSample);

	if (biasIndex < 0)
	{
		return curClosest;
	}

	const float costBias = extraSample.CostBias();

	const MotionMatchingVectorTable& vecTable = VectorTable();
	const AnimVector& sampleVec = vecTable[biasIndex];

	const float biasDist = DistanceFunc(sampleVec, query, scales, minimum);
	const float biasCost = Max(biasDist - costBias, 0.0f);

	ClosestEntry closest = curClosest;

	if (biasCost <= closest.m_cost)
	{
		closest.m_cost		  = biasCost;
		closest.m_vectorIndex = biasIndex;
		closest.m_mirror	  = extraSample.Mirror();
		closest.m_costBias	  = costBias;
	}

	return closest;
}

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT_CLASS_DEFINE(MotionMatchingSet, FindClosestSampleJob)
{
	SearchJobParams* pParams = (SearchJobParams*)jobParam;

	*pParams->m_pResult = pParams->m_pMatcher->FindClosestSampleSubset(pParams->m_goalVec,
																	   pParams->m_scales,
																	   pParams->m_minDiff,
																	   pParams->m_layerBits,
																	   pParams->m_aLayerCostMods,
																	   pParams->m_startIndex,
																	   pParams->m_numIter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool MotionMatchingIndex::IsValid(const MotionMatchingSet::AnimVector& scales,
								  const MotionMatchingSet::AnimVector& min) const
{
	PROFILE_AUTO(Animation);
	MotionMatchingSet::AnimVectorMap indexScales(m_metricScaleVector, scales.rows());
	MotionMatchingSet::AnimVectorMap indexMins(m_metricMinimunVector, min.rows());

	MotionMatchingSet::AnimVector scaleDiff = scales - indexScales;
	MotionMatchingSet::AnimVector minDiff = min - indexMins;

	const float scaleError = scaleDiff.norm();
	const float minError = minDiff.norm();

	return scaleError < 0.0001f && minError < 0.0001f;	
}

class SortedEntries
{
public:
	union Entry
	{
		struct
		{
			F32 m_dist;
			I32 m_index;
		};
		U64 m_u64;
	};

	SortedEntries(U32F maxEntries)
		: m_numEntries(0)
	{
		m_pEntries = NDI_NEW Entry[maxEntries];
	}

	const Entry& operator[](int i) { return m_pEntries[i]; }

	void Add(F32 dist, I32 index)
	{
/*
		if (UNLIKELY(m_numEntries == 0))
		{
			m_pEntries[0].m_dist = dist;
			m_pEntries[0].m_index = index;
			return;
		}

		if (UNLIKELY(m_numEntries == 1))
		{
			if (dist < m_pEntries[0].m_dist)
			{
				m_pEntries[1].m_u64 = m_pEntries[0].m_u64;
				m_pEntries[0].m_dist = dist;
				m_pEntries[0].m_index = index;
			}
			else
			{
				m_pEntries[1].m_dist = dist;
				m_pEntries[1].m_index = index;
			}
			return;
		}*/

#if 1
		I32F iNew = 0;

		I32F low = 0;
		I32F high = m_numEntries - 1;

		while (low <= high)
		{
			const I32F mid = (low + high) / 2;
			const float midDist = m_pEntries[mid].m_dist;

			if (midDist < dist)
			{
				low = mid + 1;
				iNew = low;
			}
			else if (midDist > dist)
			{
				high = mid - 1;
				iNew = mid;
			}
			else
			{
				iNew = mid;
				break;
			}
		}
#else
		I32F iNew = 0;

		for (I32F i = 1; i < m_numEntries; ++i)
		{
			if (m_pEntries[i].m_dist >= dist)
				break;
			++iNew;
		}
#endif

#if 0
		if (iNew < (m_numEntries - 1))
		{
			memmove(&m_pEntries[iNew + 1], &m_pEntries[iNew], (m_numEntries - 1 - iNew) * sizeof(Entry));
		}
#else
		U64 nextData = m_pEntries[iNew].m_u64;
		m_pEntries[iNew].m_dist = dist;
		m_pEntries[iNew].m_index = index;

		for (I32F i = iNew + 1; i <= m_numEntries; ++i)
		{
			Memory::PrefetchForLoad(&m_pEntries[i], 0x80);

			const U64 savedData = m_pEntries[i].m_u64;
			m_pEntries[i].m_u64 = nextData;
			nextData = savedData;
		}
#endif

		m_pEntries[iNew].m_dist = dist;
		m_pEntries[iNew].m_index = index;

		++m_numEntries;

#if DEBUG
		for (I32F i = 0; i < m_numEntries - 1; ++i)
		{
			ANIM_ASSERT(m_pEntries[i].m_dist <= m_pEntries[i + 1].m_dist);
		}
#endif
	}

private:
	Entry* m_pEntries;
	U32F m_numEntries;
};

#define USE_CUSTOM_LIST 0

/// --------------------------------------------------------------------------------------------------------------- ///
MotionMatchingIndex::Entry MotionMatchingIndex::GetClosest(const MotionMatchingSet::AnimVector& query,
														   const MotionMatchingVectorTable& vectors) const
{
	PROFILE_AUTO(Animation);
	PROFILE_ACCUM(MM_GetClosest);

	ScopedTempAllocator alloc(FILE_LINE_FUNC);

	const MotionMatchingVectorTable& clusterCenters = *static_cast<const MotionMatchingVectorTable*>(m_pMeansTable);

	const U32F numClusters = clusterCenters.Size();

#if USE_CUSTOM_LIST
	SortedEntries distanceToClusters(numClusters);
#else
	ListArray<Entry> distanceToClusters(numClusters);
#endif

	Entry closest = std::make_pair(kLargestFloat, 0);
	F32 closestDist = closest.first;

	MotionMatchingSet::AnimVectorMap scales(m_metricScaleVector, clusterCenters.m_numDimensions);
	MotionMatchingSet::AnimVectorMap mins(m_metricMinimunVector, clusterCenters.m_numDimensions);

	auto distFunc = [&scales, &mins](const MotionMatchingSet::AnimVector& q,
									 const MotionMatchingSet::AnimVectorMap& s) -> float 
	{
		// this should match MotionMatchMetric::Dist() in build-transform-motion-matching.cpp
		return MotionMatchingSet::LinearDistance(q, s, scales, mins); 
	};

	// Compute the distance from the query vector to each cluster
	{
		PROFILE(Animation, GetClosest_MeanDists);
		for (int iMean = 0; iMean < numClusters; iMean++)
		{
			const float dist = distFunc(query, clusterCenters[iMean]);
#if USE_CUSTOM_LIST
			distanceToClusters.Add(dist, iMean);
#else
			distanceToClusters.PushBack(std::make_pair(dist, iMean));
#endif
		}
	}

	// Sort the clusters so we visit closest clusters first
#if !USE_CUSTOM_LIST
	{
		//PROFILE(Animation, GetClosest_Sort);
		std::sort(distanceToClusters.begin(), distanceToClusters.end());
	}
#endif

	for (int iCluster = 0; iCluster < numClusters; ++iCluster)
	{
#if USE_CUSTOM_LIST
		const float distToClusterCenter = distanceToClusters[iCluster].m_dist;
		const int clusterIndex = distanceToClusters[iCluster].m_index;
#else
		float distToClusterCenter;
		int clusterIndex;
		std::tie(distToClusterCenter, clusterIndex) = distanceToClusters[iCluster];
#endif
		
		int clusterStart = m_aExtents[clusterIndex].m_start;
		int clusterEnd	 = m_aExtents[clusterIndex].m_end;

		for (int i = clusterStart; i < clusterEnd; i++)
		{
			// Check if we can early out of the current cluster
			if (closestDist <= distToClusterCenter - m_aDistances[i].m_dist)
				break;

			const int pointIndex	= m_aDistances[i].m_index;
			const float distToPoint = distFunc(query, vectors[pointIndex]);

			if (distToPoint < closestDist)
			{
				closest = std::make_pair(distToPoint, pointIndex);
				closestDist = distToPoint;
			}
		}
	}

	return closest;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingIndex::GetClosestK(const MotionMatchingSet::AnimVector& query,
									  const MotionMatchingVectorTable& vectors,
									  ListArray<Entry>& outNearest) const
{
	PROFILE_AUTO(Animation);
	outNearest.Clear();

	ScopedTempAllocator alloc(FILE_LINE_FUNC);

	const MotionMatchingVectorTable& clusterCenters = *static_cast<const MotionMatchingVectorTable*>(m_pMeansTable);
	ListArray<std::tuple<float, int>> distanceToClusters(clusterCenters.Size());

	const int k = outNearest.Capacity();
	for (int i = 0; i < k; i++)
	{
		outNearest.PushBack(std::make_pair(kLargestFloat, 0));
	}
	F32 maxDist = kLargestFloat;

	MotionMatchingSet::AnimVectorMap scales(m_metricScaleVector, clusterCenters.m_numDimensions);
	MotionMatchingSet::AnimVectorMap mins(m_metricMinimunVector, clusterCenters.m_numDimensions);
	auto distFunc = [&scales, &mins](const MotionMatchingSet::AnimVector& q,
									 const MotionMatchingSet::AnimVectorMap& s) -> float 
	{
		// this should match MotionMatchMetric::Dist() in build-transform-motion-matching.cpp
		return MotionMatchingSet::LinearDistance(q, s, scales, mins);
	};

	// Compute the distance from the query vector to each cluster
	{
		PROFILE(Animation, GetClosest_MeanDists);
		for (int iMean = 0; iMean < clusterCenters.Size(); iMean++)
		{
			const float dist = distFunc(query, clusterCenters[iMean]);
			distanceToClusters.PushBack(std::make_pair(dist, iMean));
		}
	}

	// Sort the clusters so we visit closest clusters first
	{
		PROFILE(Animation, GetClosest_Sort);
		std::sort(distanceToClusters.begin(), distanceToClusters.end());
	}

	for (int iCluster = 0; iCluster < clusterCenters.Size(); ++iCluster)
	{
		float distToClusterCenter;
		int clusterIndex;
		std::tie(distToClusterCenter, clusterIndex) = distanceToClusters[iCluster];

		int clusterStart = m_aExtents[clusterIndex].m_start;
		int clusterEnd	 = m_aExtents[clusterIndex].m_end;

		for (int i = clusterStart; i < clusterEnd; i++)
		{
			// Check if we can early out of the current cluster
			if (maxDist <= distToClusterCenter - m_aDistances[i].m_dist)
				break;

			const int pointIndex	= m_aDistances[i].m_index;
			const float distToPoint = distFunc(query, vectors[pointIndex]);
			if (maxDist > distToPoint)
			{
				std::pop_heap(outNearest.Begin(), outNearest.End());
				outNearest.Back() = std::make_pair(distToPoint, pointIndex);
				std::push_heap(outNearest.Begin(), outNearest.End());
				maxDist = std::get<0>(outNearest.Front());
			}
		}
	}

	std::sort_heap(outNearest.Begin(), outNearest.End());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void MotionMatchingSet::GoalLocatorsToVector(const MMSearchParams& params,
											 MotionMatchingSet::AnimVector& outGoalFilter,
											 MotionMatchingSet::AnimVector& outVector) const
{
	for (U32F i = 0; i < m_pSettings->m_goals.m_numGoalLocators; ++i)
	{
		const U32F index = GetGoalLocDimensionIndex(i);

		outGoalFilter[index + 0] = 0.0f;
		outGoalFilter[index + 1] = 0.0f;
		outGoalFilter[index + 2] = 0.0f;

		for (U32F iGoal = 0; iGoal < params.m_numGoalLocators; ++iGoal)
		{
			const AnimGoalLocator& goalLoc = params.m_goalLocs[iGoal];
			if (goalLoc.m_nameId != m_pSettings->m_goals.m_aGoalLocators[i].m_locatorId)
				continue;

			outGoalFilter[index + 0] = 1.0f;
			outGoalFilter[index + 1] = 1.0f;
			outGoalFilter[index + 2] = 1.0f;

			outVector[index + 0] = goalLoc.m_loc.Pos().X();
			outVector[index + 1] = goalLoc.m_loc.Pos().Y();
			outVector[index + 2] = goalLoc.m_loc.Pos().Z();

			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void WriteVectorAsJsonArray(NdJsonWriter<NdJsonFileWriteStream>& writer, Vector_arg v)
{
	STRIP_IN_FINAL_BUILD;

	writer.StartArray();
	writer.Double(v.X());
	writer.Double(v.Y());
	writer.Double(v.Z());
	writer.EndArray();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void WritePointAsJsonArray(NdJsonWriter<NdJsonFileWriteStream>& writer, Point_arg p)
{
	STRIP_IN_FINAL_BUILD;

	writer.StartArray();
	writer.Double(p.X());
	writer.Double(p.Y());
	writer.Double(p.Z());
	writer.EndArray();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void WriteAnimVectorAsJsonArray(NdJsonWriter<NdJsonFileWriteStream>& writer,
									   const MotionMatchingSet::AnimVector& v)
{
	STRIP_IN_FINAL_BUILD;

	writer.StartArray();
	for (int r = 0; r < v.rows(); ++r)
	{
		writer.Double(v[r]);
	}
	writer.EndArray();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static I32F WriteSampleTrajectoryToJson(NdJsonWriter<NdJsonFileWriteStream>& writer,
										const MotionMatchingSet::AnimVectorMap& sample,
										const I32F startIndex,
										const float maxTime,
										const I32F numSamples)
{
	STRIP_IN_FINAL_BUILD_VALUE(startIndex);

	I32F index = startIndex;

	const float dt = maxTime / numSamples;

	for (int i = 0; i < numSamples; ++i)
	{
		const float t = dt * (i + 1);

		const Point pos = Point(sample[index], sample[index + 1], sample[index + 2]);
		index += 3;
		const Vector vel = Vector(sample[index], sample[index + 1], sample[index + 2]);
		index += 3;
		const Vector facing = Vector(sample[index], sample[index + 1], sample[index + 2]);
		index += 3;
		const float yawSpeed = sample[index++];

		writer.StartObject();
		
		writer.Key("time");
		writer.Double(t);
		
		writer.Key("pos");
		WritePointAsJsonArray(writer, pos);

		writer.Key("vel");
		WriteVectorAsJsonArray(writer, vel);

		writer.Key("facing");
		WriteVectorAsJsonArray(writer, facing);

		writer.Key("yaw-speed");
		writer.Double(yawSpeed);

		writer.EndObject();
	}

	return index;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static I32F WriteSampleGoalLocatorsToJson(NdJsonWriter<NdJsonFileWriteStream>& writer,
										  const MotionMatchingSet::AnimVectorMap& sample,
										  const I32F startIndex,
										  const MMGoals& goals)
{
	STRIP_IN_FINAL_BUILD_VALUE(startIndex);

	I32F index = startIndex;

	for (U32F i = 0; i < goals.m_numGoalLocators; ++i)
	{
		const Point goalPos = Point(sample[index], sample[index + 1], sample[index + 2]);
		index += 3;
		const StringId64 nameId = goals.m_aGoalLocators[i].m_locatorId;
		const float minDist = goals.m_aGoalLocators[i].m_minGoalDist;
		const float weight = goals.m_aGoalLocators[i].m_goalLocWeight;

		writer.StartObject();

		writer.Key("name");
		writer.String(DevKitOnly_StringIdToString(nameId));

		writer.Key("pos");
		WritePointAsJsonArray(writer, goalPos);

		writer.Key("min-dist");
		writer.Double(minDist);

		writer.Key("weight");
		writer.Double(weight);

		writer.EndObject();
	}

	return index;
}


/// --------------------------------------------------------------------------------------------------------------- ///
bool MotionMatchingSet::DumpToJson(StringId64 nameId, const char* pFileName) const
{
	STRIP_IN_FINAL_BUILD_VALUE(false);

	if (!m_sampleTable)
	{
		return false;
	}

	ScopedTempAllocator jj(FILE_LINE_FUNC);
	FILE* pFile = fopen(pFileName, "w");
	if (!pFile)
	{
		return false;
	}

	char writeBuffer[1024];
	NdJsonFileWriteStream fileStream(pFile, writeBuffer, sizeof(writeBuffer));
	NdJsonPrettyWriter<NdJsonFileWriteStream> writer(fileStream);

	const MotionMatchingVectorTable& vectors = VectorTable();

	//JsonWriter writer(&fileStream);
	writer.StartObject();
	writer.Key("json-version");
	writer.Int(3);
	writer.Key("name");
	writer.String(DevKitOnly_StringIdToString(nameId));
	writer.Key("rows");
	writer.Int(vectors.Size());
	writer.Key("cols");
	writer.Int(m_pVectorTable->m_numDimensions);

	writer.Key("auto-weights");
	WriteAnimVectorAsJsonArray(writer, GetAutoWeights());

	writer.Key("pose");
	writer.StartObject();
	{
		writer.Key("master-weight");
		writer.Double(m_pSettings->m_pose.m_masterWeight);
		writer.Key("facing-joint");
		writer.String(DevKitOnly_StringIdToString(m_pSettings->m_pose.m_facingJointId));
		writer.Key("facing-weight");
		writer.Double(m_pSettings->m_pose.m_facingWeight);
		writer.Key("facing-axis-ls");
		WriteVectorAsJsonArray(writer, m_pSettings->m_pose.m_facingAxisLs);
		writer.Key("num-bodies");
		writer.Int(m_pSettings->m_pose.m_numBodies);
		writer.Key("bodies");
		writer.StartArray();
		for (U32F iBody = 0; iBody < m_pSettings->m_pose.m_numBodies; ++iBody)
		{
			const MMPoseBody& bodyDef = m_pSettings->m_pose.m_aBodies[iBody];
			writer.StartObject();
			writer.Key("joint-id");
			writer.String(bodyDef.m_isCenterOfMass ? "COM" : DevKitOnly_StringIdToString(bodyDef.m_jointId));
			writer.Key("pos-weight");
			writer.Double(bodyDef.m_positionWeight);
			writer.Key("vel-weight");
			writer.Double(bodyDef.m_velocityWeight);
			writer.EndObject();
		}
		writer.EndArray();
	}
	writer.EndObject();

	writer.Key("goals");
	writer.StartObject();
	{
		writer.Key("master-weight");
		writer.Double(m_pSettings->m_goals.m_masterWeight);
		writer.Key("position-weight");
		writer.Double(m_pSettings->m_goals.m_positionWeight);
		writer.Key("velocity-weight");
		writer.Double(m_pSettings->m_goals.m_velocityWeight);
		writer.Key("directional-weight");
		writer.Double(m_pSettings->m_goals.m_directionalWeight);
		writer.Key("interim-directional-weight");
		writer.Double(m_pSettings->m_goals.m_interimDirectionalWeight);
		writer.Key("yaw-speed-weight");
		writer.Double(m_pSettings->m_goals.m_yawSpeedWeight);
		writer.Key("anim-bias-weight");
		writer.Double(m_pSettings->m_goals.m_animBiasWeight);
		writer.Key("grouping-weight");
		writer.Double(m_pSettings->m_goals.m_groupingWeight);
		writer.Key("stopping-face-dist");
		writer.Double(m_pSettings->m_goals.m_stoppingFaceDist);

		writer.Key("max-trajectory-sample-time");
		writer.Double(m_pSettings->m_goals.m_maxTrajSampleTime);
		writer.Key("num-trajectory-samples");
		writer.Int(m_pSettings->m_goals.m_numTrajSamples);

		writer.Key("max-prev-trajectory-sample-time");
		writer.Double(m_pSettings->m_goals.m_maxTrajSampleTimePrevTraj);
		writer.Key("num-prev-trajectory-samples");
		writer.Int(m_pSettings->m_goals.m_numTrajSamplesPrevTraj);
		writer.Key("prev-trajectory-weight");
		writer.Double(m_pSettings->m_goals.m_prevTrajWeight);
	}
	writer.EndObject();

	writer.Key("anims");
	writer.StartArray();

	const MMPose& poseDef = m_pSettings->m_pose;

	for (I32F iAnim = 0; iAnim < m_sampleTable->m_numAnims; ++iAnim)
	{
		const MMAnimSampleRange& range = m_sampleTable->m_aAnimRanges[iAnim];

		const StringId64 animId = m_sampleTable->m_aAnimIds[iAnim];
		const I32F endIndex = range.m_startIndex + range.m_count;

		const StringId64 layerId = m_aLayerIds[iAnim];

		writer.StartObject();

		writer.Key("anim-name");
		writer.String(DevKitOnly_StringIdToString(animId));
		writer.Key("layer");
		writer.String(DevKitOnly_StringIdToString(layerId));

		writer.Key("samples");
		writer.StartArray();

		for (I32F iSample = range.m_startIndex; iSample < endIndex; ++iSample)
		{
			const MMCAnimSample& compressedSample = m_sampleTable->m_aSamples[iSample];
			const AnimVectorMap& sampleVec = vectors[iSample];

			const I32F animSample = compressedSample.m_sampleIndex;

			writer.StartObject();

			writer.Key("sample");
			writer.Int(animSample);

			I32F index = 0;

			writer.Key("bodies");
			writer.StartArray();

			for (I32F i = 0; i < poseDef.m_numBodies; i++)
			{
				const MMPoseBody& body = poseDef.m_aBodies[i];

				const Point pos = Point(sampleVec[index], sampleVec[index + 1], sampleVec[index + 2]);
				index += 3;
				const Vector vel = Vector(sampleVec[index], sampleVec[index + 1], sampleVec[index + 2]);
				index += 3;

				writer.StartObject();
				writer.Key("joint-id");
				writer.String(body.m_isCenterOfMass ? "COM" : DevKitOnly_StringIdToString(body.m_jointId));

				writer.Key("pos");
				WritePointAsJsonArray(writer, pos);

				writer.Key("vel");
				WriteVectorAsJsonArray(writer, vel);

				writer.EndObject();
			}

			writer.EndArray();

			const Vector facing = Vector(sampleVec[index], sampleVec[index + 1], sampleVec[index + 2]);
			index += 3;

			writer.Key("pose-facing");
			WriteVectorAsJsonArray(writer, facing);

			ANIM_ASSERT(index == GetNumPoseDimensions());

			writer.Key("trajectory");
			writer.StartArray();

			index = WriteSampleTrajectoryToJson(writer,
												sampleVec,
												index,
												m_pSettings->m_goals.m_maxTrajSampleTime,
												m_pSettings->m_goals.m_numTrajSamples);

			index = WriteSampleTrajectoryToJson(writer,
												sampleVec,
												index,
												m_pSettings->m_goals.m_maxTrajSampleTimePrevTraj,
												m_pSettings->m_goals.m_numTrajSamplesPrevTraj);

			writer.EndArray();

			if (m_pSettings->m_goals.m_numGoalLocators > 0)
			{
				writer.Key("goal-locators");
				writer.StartArray();

				index = WriteSampleGoalLocatorsToJson(writer, sampleVec, index, m_pSettings->m_goals);

				writer.EndArray();
			}

			const float bias = sampleVec[index++];
			const float groupId = sampleVec[index++];

			writer.Key("bias");
			writer.Double(bias);
			writer.Key("group-id");
			writer.Double(groupId);

			ANIM_ASSERT(index == m_numDimensions);
			writer.EndObject();
		}

		writer.EndArray();

		writer.EndObject();
	}

	writer.EndArray();

	writer.EndObject();

	fclose(pFile);

	return true;
}
