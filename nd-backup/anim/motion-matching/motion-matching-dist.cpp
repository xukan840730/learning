/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/motion-matching/motion-matching-set.h"

// These are split into a separate file so they can be optimized in a hybrid build

/// --------------------------------------------------------------------------------------------------------------- ///
static float LinearDistance(const MotionMatchingSet::AnimVectorMap& a,
							const MotionMatchingSet::AnimVector& b,
							const MotionMatchingSet::AnimVector& scale,
							const MotionMatchingSet::AnimVector& min)
{
	const MotionMatchingSet::AnimVector zeros = MotionMatchingSet::AnimVector::Zero(a.rows());
	auto absDiff = (a - b).array().abs();
	auto constrained = (absDiff - min.array()).max(zeros.array());
	auto scaled = constrained * scale.array();
	return scaled.matrix().norm();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float SquaredDistance(const MotionMatchingSet::AnimVectorMap& a,
							 const MotionMatchingSet::AnimVector& b,
							 const MotionMatchingSet::AnimVector& scale,
							 const MotionMatchingSet::AnimVector& min)
{
	const MotionMatchingSet::AnimVector zeros = MotionMatchingSet::AnimVector::Zero(a.rows());
	auto absDiff = (a - b).array().abs();
	auto constrained = (absDiff - min.array()).max(zeros.array());
	auto scaled = constrained * scale.array();
	return scaled.matrix().squaredNorm();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
float MotionMatchingSet::LinearDistance(const AnimVector& a,
										const AnimVector& b,
										const AnimVector& scale,
										const AnimVector& min)
{
	const AnimVector zeros = AnimVector::Zero(a.rows());
	auto absDiff = (a - b).array().abs();
	auto constrained = (absDiff - min.array()).max(zeros.array());
	auto scaled = constrained * scale.array();
	return scaled.matrix().norm();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
float MotionMatchingSet::LinearDistance(const AnimVector& a,
										const AnimVectorMap& b,
										const AnimVectorMap& scales,
										const AnimVectorMap& mins)
{
	auto zeros = AnimVector::Zero(a.rows());
	auto absDiff = (a - b).cwiseAbs();
	auto constrained = (absDiff - mins).cwiseMax(zeros);
	auto scaled = constrained.cwiseProduct(scales);
	return scaled.norm();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
MotionMatchingSet::AnimVector MotionMatchingSet::DistanceVec(const AnimVector& a,
															 const AnimVector& b,
															 const AnimVector& scale,
															 const AnimVector& min)
{
	auto zeros = MotionMatchingSet::AnimVector::Zero(a.rows());
	auto absDiff = (a - b).array().abs();
	auto constrained = (absDiff - min.array()).max(zeros.array());
	auto scaled = constrained * scale.array();
	return scaled.matrix();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
MotionMatchingSet::AnimVector MotionMatchingSet::DistanceVec(const AnimVector& a,
															 const AnimVectorMap& b,
															 const AnimVector& scale,
															 const AnimVector& min)
{
	auto zeros = MotionMatchingSet::AnimVector::Zero(a.rows());
	auto absDiff = (a - b).array().abs();
	auto constrained = (absDiff - min.array()).max(zeros.array());
	auto scaled = constrained * scale.array();
	return scaled.matrix();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
float MotionMatchingSet::DistanceFunc(const AnimVector& distanceVec)
{
	return distanceVec.squaredNorm();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
float MotionMatchingSet::DistanceFunc(const AnimVector& a,
									  const AnimVector& b,
									  const AnimVector& scale,
									  const AnimVector& min)
{
	AnimVector distanceVec = DistanceVec(a, b, scale, min);
	
	return DistanceFunc(distanceVec);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
float MotionMatchingSet::DistanceFunc(const AnimVector& a,
									  const AnimVectorMap& b,
									  const AnimVector& scale,
									  const AnimVector& min)
{
	AnimVector distanceVec = DistanceVec(a, b, scale, min);

	return DistanceFunc(distanceVec);
}

/// --------------------------------------------------------------------------------------------------------------- ///
MotionMatchingSet::ClosestEntry MotionMatchingSet::FindClosestSampleSubset(const AnimVector& goalVec,
																		   const AnimVector& scales,
																		   const AnimVector& minDiff,
																		   const BitArray64& layerBits,
																		   const float* aLayerCostMods,
																		   I32F startIndex,
																		   I32F numIter) const
{
	MotionMatchingSet::ClosestEntry best		 = { kLargeFloat, -1, false };
	const MotionMatchingVectorTable& vectorTable = VectorTable();

	for (I32F i = 0; i < numIter; ++i)
	{
		const U32F iAnim = startIndex + i;
		const U32F iLayer = m_pLayerIndexTable[iAnim];

		if (!layerBits.IsBitSet(iLayer))
			continue;

		const float layerCostMod = aLayerCostMods[iLayer];

		const AnimVector& animVec = vectorTable[iAnim];

		const float distSqr = DistanceFunc(animVec, goalVec, scales, minDiff);
		const float curCost = Sqrt(distSqr) + layerCostMod;

		if (curCost < best.m_cost)
		{
			best.m_cost		   = curCost;
			best.m_vectorIndex = iAnim;
		}
	}
	return best;
}
