/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/list-array.h"

#include "ndlib/anim/anim-debug.h"

#include "gamelib/anim/motion-matching/gameplay-goal.h"
#include "gamelib/anim/motion-matching/motion-matching-def.h"
#include "gamelib/anim/motion-matching/motion-matching.h"

#include <Eigen/Dense>
#include <tuple>

/// --------------------------------------------------------------------------------------------------------------- ///
class IMotionPose;
class MotionMatchingVectorTable;
class AnimSample;
class ArtItemSkeleton;

namespace DC
{
	struct MotionMatchExternalFallback;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class MotionMatchingSet : public MotionMatchingSetDef
{
public:
	enum ExtraSampleDimensions
	{
		kExtraDimensionBias,
		kExtraDimensionGroupId,
		kNumExtraDimensions
	};

	MotionMatchingSet() = delete;

	static CONST_EXPR size_t kMaxVectorDim = 256;

	using AnimVector	 = Eigen::Matrix<float, Eigen::Dynamic, 1, 0, kMaxVectorDim, 1>;
	using AnimVectorTool = Eigen::Matrix<float, Eigen::Dynamic, 1>;
	using AnimVectorMap	 = Eigen::Map<const AnimVectorTool>;

	bool HasValidIndices() const;

	bool IsAnimInSet(StringId64 animId) const;
	bool IsAnimInSet(const AnimSample& sample, bool* pNextSampleInSetOut = nullptr) const;

	// This version only works if the sample is from an animation in the set
	Maybe<AnimSample> FindClosestSampleExisting(const MMSearchParams& params,
												const AnimSampleBiased& sample,
												const AnimTrajectory& trajectory,
												I32F desiredGroupId,
												const AnimSampleBiased& extraSample) const;

	Maybe<AnimSample> FindClosestSampleExternal(const MMSearchParams& params,
												const IMotionPose& pose,
												const AnimTrajectory& trajectory,
												I32F poseGroupId,
												I32F poseBias,
												const AnimSampleBiased& extraSample) const;

	float ComputeGoalCost(const MMSearchParams& params,
						  const AnimSample& sample,
						  const AnimTrajectory& trajectory) const;

	float ComputeGoalCostWithExtras(const MMSearchParams& params,
									const AnimSample& sample,
									bool sampleBiased,
									I32F sampleGroupId,
									const AnimTrajectory& desiredTrajectory,
									AnimVector* pDistanceVecOut = nullptr) const;

	bool CreateTrajectoryFromSample(const AnimSample& sample, AnimTrajectory* pTrajectoryOut) const;

	// Debugging methods
	void DebugClosestSamplesExisting(const MMSearchParams& params,
									 const AnimSampleBiased& sample,
									 const AnimTrajectory& trajectory,
									 I32F desiredGroupId,
									 const AnimSampleBiased& extraSample,
									 const int N,
									 int debugIndex,
									 Maybe<AnimSample> extraDebugSample,
									 const Locator& refLoc,
									 Color32 c) const;

	void DebugClosestSamplesExternal(const MMSearchParams& params,
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
									 Color32 c) const;

	bool DumpToJson(StringId64 nameId, const char* pFileName) const;

	void ValidateEffData() const;
	void ValidateSampleIndices(const char* setName) const;

	bool HasRequiredJointsForSet(const NdGameObject* pGameObj) const;

	// Distance functions with different parameter combinations between runtime and tool vectors to prevent conversions
	static AnimVector DistanceVec(const AnimVector& a,
								  const AnimVector& b,
								  const AnimVector& scale,
								  const AnimVector& min);

	static AnimVector DistanceVec(const AnimVector& a,
								  const AnimVectorMap& b,
								  const AnimVector& scale,
								  const AnimVector& min);

	static float DistanceFunc(const AnimVector& distanceVec);
	static float DistanceFunc(const AnimVector& a, const AnimVector& b, const AnimVector& scale, const AnimVector& min);
	static float DistanceFunc(const AnimVector& a, const AnimVectorMap& b, const AnimVector& scale, const AnimVector& min);

	static float LinearDistance(const AnimVector& a,
								const AnimVector& b,
								const AnimVector& scales,
								const AnimVector& mins);

	static float LinearDistance(const AnimVector& a,
								const AnimVectorMap& b,
								const AnimVectorMap& scales,
								const AnimVectorMap& mins);

private:
	struct ClosestEntry
	{
		float m_cost = kLargeFloat;
		I32 m_vectorIndex = -1;
		float m_costBias = 0.0f;
		bool m_mirror = false;
	};

	// Brute force job params
	struct SearchJobParams
	{
		AnimVector m_goalVec;
		AnimVector m_scales;
		AnimVector m_minDiff;
		BitArray64 m_layerBits;
		const float* m_aLayerCostMods;
		const MotionMatchingSet* m_pMatcher;
		ClosestEntry* m_pResult;
		int m_startIndex;
		int m_numIter;
	};

	using ClosestEntryList = ListArray<ClosestEntry>;

	friend bool operator<(const MotionMatchingSet::ClosestEntry& a, const MotionMatchingSet::ClosestEntry& b);

	AnimVector CompositeWeights(const AnimVector& goalFilterVec, AnimVector* pMinimumsOut) const;

	// Convert the Pose and Trajectory weights into Vectors. This could be precomputed in the tools
	static void CreateScaleAndMinVectors(const MMSettings* pSettings,
										 AnimVector& scalesOut,
										 AnimVector& minsOut,
										 AnimVector* pPoseScales = nullptr,
										 AnimVector* pGoalScales = nullptr,
										 AnimVector* pExtraScales = nullptr);

	const MotionMatchingVectorTable& VectorTable() const;

	// Conversion functions from the various input types to the AnimVector type that the set uses internally

	/* Creates vector assuming animSample already exists somewhere in the set */
	bool CreateAnimVectorFromExisting(const MMSearchParams& params,
									  const AnimSample& animSample,
									  const AnimTrajectory& trajectory,
									  AnimVector* pVectorOut,
									  AnimVector* pGoalFilterOut,
									  I32F* pTableIndexOut = nullptr) const;

	bool CreateAnimVectorFromExternal(const MMSearchParams& params,
									  const IMotionPose& pose,
									  const AnimTrajectory& trajectory,
									  I32F poseGroupId,
									  I32F poseBias,
									  AnimVector* pVectorOut,
									  AnimVector* pGoalFilterOut) const;

	ClosestEntry FindClosestSampleFromVectors(const MMSearchParams& params,
											  const AnimVector& v,
											  const AnimVector& scales,
											  const AnimVector& minimum) const;

	ClosestEntry FindClosestSampleInternal(const AnimVector& queryVector,
										   const AnimVector& scales,
										   const AnimVector& minimum,
										   bool mirrored,
										   const BitArray64& layerBits,
										   const float* aLayerCostMods) const;

	// This method should only be used in a debug setting
	ClosestEntry FindClosestSampleBrute(const AnimVector& queryVector,
										const AnimVector& scales,
										const AnimVector& minimum,
										bool mirrored,
										const BitArray64& layerBits,
										const float* aLayerCostMods) const;

	JOB_ENTRY_POINT_CLASS_DECLARE(MotionMatchingSet, FindClosestSampleJob);
	JOB_ENTRY_POINT_CLASS_DECLARE(MotionMatchingSet, FindClosestSampleIndexedJob);

	ClosestEntry FindClosestSampleSubset(const AnimVector& goalVec,
										 const AnimVector& scales,
										 const AnimVector& minDiff,
										 const BitArray64& layerBits,
										 const float* aLayerCostMods,
										 I32F startIndex,
										 I32F numIter) const;

	ClosestEntry ConsiderExtraSample(const AnimSampleBiased& extraSample,
									 const ClosestEntry& curClosest,
									 const AnimVector& query,
									 const AnimVector& scales,
									 const AnimVector& minimum) const;

	AnimSample AnimSampleFromIndex(int i, bool mirror) const;
	StringId64 AnimNameFromIndex(I32F index) const;

	I32F GetTableIndexFromSample(const AnimSample& sample) const;

	AnimVectorMap GetAutoWeights() const;
	I32F GetNumPoseDimensions() const;
	I32F GetNumGoalDimensions() const;
	I32F GetNumExtraDimensions() const { return kNumExtraDimensions; }
	I32F GetTotalNumDimensions() const { return m_numDimensions; }

	I32F GetGoalLocDimensionIndex(I32F iGoalLoc) const;

	AnimVector GetMirrorVector() const;
	AnimVector MirrorVector(const AnimVector& v) const;

	I32F GetBodyIndex(StringId64 jointId) const;

	I32F GetLayerIndex(StringId64 layerId) const;
	BitArray64 CreateValidLayerBits(const MMSearchParams& params, float* aLayerCostMods) const;

	void GoalLocatorsToVector(const MMSearchParams& params,
							  MotionMatchingSet::AnimVector& outGoalFilter,
							  MotionMatchingSet::AnimVector& outVector) const;

	void FindClosestSamples(const MMSearchParams& params,
							const AnimVector& v,
							const AnimVector& scales,
							const AnimVector& minimum,
							I32F ignoreIndex,
							ClosestEntryList& closest) const;

	void FindClosestSamplesExisting(const MMSearchParams& params,
									const AnimSampleBiased& sample,
									const AnimTrajectory& trajectory,
									I32F desiredGroupId,
									const AnimSampleBiased& extraSample,
									ClosestEntryList& outClosest,
									AnimVector& goalVec,
									AnimVector& scales,
									AnimVector& minimum,
									AnimVector& goalFilterVec) const;

	void FindClosestSamplesExternal(const MMSearchParams& params,
									const IMotionPose& pose,
									const AnimTrajectory& trajectory,
									I32F poseGroupId,
									I32F poseBias,
									const AnimSampleBiased& extraSample,
									ClosestEntryList& outClosest,
									AnimVector& goalVec,
									AnimVector& scales,
									AnimVector& minimum,
									AnimVector& goalFilterVec) const;

	void DebugClosestSamples(const MMSearchParams& params,
							 int debugIndex,
							 Maybe<AnimSample> extraDebugSample,
							 const Locator& refLoc,
							 Color32 c,
							 const ClosestEntryList& closest,
							 const AnimVector& goalVec,
							 const AnimVector& scales,
							 const AnimVector& mins,
							 const AnimVector& goalFilter) const;

	void HandleExtraDebugSample(const Maybe<AnimSample>& extraDebugSample,
								const ClosestEntryList& initialClosest,
								const AnimVector& goalVec,
								const AnimVector& scales,
								const AnimVector& mins,
								I32F* pDebugIndexOut,
								bool* pDebugMirrorOut,
								const ClosestEntryList** ppNewClosestOut) const;

	void DebugExternalAlternative(const MMSearchParams& params,
								  const AnimSample& altSample,
								  const char* name,
								  const AnimTrajectory& trajectory,
								  I32F poseGroupId,
								  I32F poseBias,
								  float costBias) const;

	friend class MotionMatchingIndex;
	friend class MotionMatchingManager;
};

STATIC_ASSERT(sizeof(MotionMatchingSet) == sizeof(MotionMatchingSetDef));

/// --------------------------------------------------------------------------------------------------------------- ///
class MotionMatchingVectorTable : public MMAnimVectorTableRaw
{
public:
	MotionMatchingVectorTable() = delete;

	using AnimVectorMap = MotionMatchingSet::AnimVectorMap;

	int Size() const { return m_numVectors; }

	AnimVectorMap operator[](int i) const
	{
		ANIM_ASSERT(i < m_numVectors);
		int chunkIndex		= i / m_vectorsPerChunk;
		int interChunkIndex = i % m_vectorsPerChunk;
		float* pFloat		= reinterpret_cast<float**>(m_apChunks)[chunkIndex] + interChunkIndex * m_numDimensions;
		return AnimVectorMap(pFloat, m_numDimensions);
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
class MotionMatchingIndex : public MMIndex
{
public:
	MotionMatchingIndex() = delete;

	typedef std::pair<F32, int> Entry;

	bool IsValid(const MotionMatchingSet::AnimVector& scales, const MotionMatchingSet::AnimVector& min) const;
	Entry GetClosest(const MotionMatchingSet::AnimVector& query, const MotionMatchingVectorTable& vectors) const;
	void GetClosestK(const MotionMatchingSet::AnimVector& query,
					 const MotionMatchingVectorTable& vectors,
					 ListArray<Entry>& outNearest) const;
};

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsPoseValidForPoseDef(const IMotionPose& pose, const MMPose& poseDef);
bool IsSkeletonValidForPoseDef(const ArtItemSkeleton* pSkel, const MMPose& poseDef);
