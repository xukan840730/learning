/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include <algorithm>
#include <functional>
#include <limits>
#include <vector>
#include <cstdlib>

//#pragma optimize("", off)

/// --------------------------------------------------------------------------------------------------------------- ///
inline void KMeansWorkerThread(void* param)
{
	std::function<void(void)> func = *(std::function<void(void)>*)param;
	func();
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename X, typename ArrayLikeX, typename I, typename Metric>
void KMeans(const ArrayLikeX& items,
			const int k,
			const Metric& distSqr,
			const int maxIter,
			std::vector<I>& assignments,
			std::vector<X>& means,
			const X& zero)
{
	//Make sure there is enough room for the results
	IABORT_IF(assignments.capacity() < items.size(), "K-Means error: not enough assignments (%d) for given items (%d)\n", assignments.capacity(), items.size());
	IABORT_IF(means.capacity() < k, "K-Means error: means output not big enough (%d < %d)\n", means.capacity(), k);
	IABORT_IF((k - 1) > std::numeric_limits<I>::max(), "K-Means error: Requested K too big (%d / %d)\n", k, std::numeric_limits<I>::max());
	IABORT_IF(k > items.size(), "K-Means error: Requested K too big for given items (%d / %d)\n", k, items.size());
	
	const U64 startTime = TimerGetRawCount();

	assignments.clear();
	assignments.resize(items.size());

	using DistType = typename std::result_of< Metric(X, X)>::type;
	means.clear();
	std::vector<int> numInCluster(k);
	{
		std::vector<I> initialMeanIndices;
		initialMeanIndices.reserve(k);
		std::srand(1); //TODO make seed a parameter
		for (int i = 0; i < k; ++i)
		{
			int index;
			do
			{
				index = std::rand() % items.size();
			} while (std::find(std::begin(initialMeanIndices), std::end(initialMeanIndices), index)
					 != std::end(initialMeanIndices));

			initialMeanIndices.push_back(index);
			means.push_back(items[index]);
		}
	}

	//U32 numJobs = 1;
	U32 numJobs = k;

	DistType prevDistortion = std::numeric_limits<DistType>::infinity();
	int iter = 0;
	while (iter < maxIter)
	{
		iter++;
		// assign clusters
		bool change = false;
		DistType curDistortion = 0.0f;

		// Compute assignments
		{
			struct AssignmentJobParams
			{
				int m_index;
				int m_k;
				std::function<DistType(const X&, const X&)> m_distSqr;
				std::vector<I>* m_pAssignments;
				std::vector<X>* m_pMeans;
				const ArrayLikeX* m_pItems;
				DistType m_distortion;
			};

			std::vector<WorkItemHandle> jobHandles;
			jobHandles.reserve(numJobs);
			std::vector<std::function<void(void)>> jobFuncs;
			jobFuncs.resize(numJobs);
			std::vector<AssignmentJobParams> params;
			params.resize(numJobs);

			for (int i = 0; i < numJobs; ++i)
			{
				AssignmentJobParams& curParams = params[i];

				curParams.m_index = i;
				curParams.m_k = k;
				curParams.m_distSqr = distSqr;
				curParams.m_pAssignments = &assignments;
				curParams.m_pItems = &items;
				curParams.m_pMeans = &means;
				curParams.m_distortion = -1.0f;

				jobFuncs[i] = [&curParams, numJobs]() 
				{
					const int itemsPerJob = (curParams.m_pItems->size() + (numJobs - 1)) / numJobs;
					const int startIndex  = itemsPerJob * curParams.m_index;
					const int maxIndex	  = Min(itemsPerJob * (curParams.m_index + 1),
												static_cast<int>(curParams.m_pItems->size()));
					const int numMeans	  = curParams.m_k;
					DistType distortion = 0;

					for (int ix = startIndex; ix < maxIndex; ix++)
					{
						DistType minDist = std::numeric_limits<DistType>::infinity();
						int mean = -1;
						for (int im = 0; im < numMeans; im++)
						{
							const DistType curDist = curParams.m_distSqr(curParams.m_pMeans->at(im),
																		 curParams.m_pItems->at(ix));
							if (curDist < minDist)
							{
								mean = im;
								minDist = curDist;
							}
						}

						distortion += minDist;

						if (curParams.m_pAssignments->at(ix) != mean)
						{
							curParams.m_pAssignments->at(ix) = mean;
						}
					}
					curParams.m_distortion = distortion;

					INOTE_DEBUG("k-means distortion [%d : %d] = %f\n", startIndex, maxIndex, distortion);
				};				
			
				WorkItemHandle hJob = NdThreadPool::QueueWorkItem(KMeansWorkerThread, &jobFuncs[i]); 
				jobHandles.push_back(hJob);
			}

			NdThreadPool::WaitAll(jobHandles);

			for (int i = 0; i < numJobs; ++i)
			{
				curDistortion += params[i].m_distortion;
			}
		}

		// compute new means
		{
			struct MeansJobParams
			{
				int m_index;
				int m_k;
				const std::vector<I>* m_pAssignments;
				std::vector<X>* m_pMeans;
				std::vector<int>* m_pNumInCluster;
				const ArrayLikeX* m_pItems;
			};

			std::vector<WorkItemHandle> jobHandles;
			jobHandles.reserve(numJobs);
			std::vector<std::function<void(void)>> jobFuncs;
			jobFuncs.resize(numJobs);
			std::vector<MeansJobParams> params;
			params.resize(numJobs);

			for (int i = 0; i < numJobs; ++i)
			{
				MeansJobParams& curParams = params[i];

				curParams.m_index = i;
				curParams.m_k	  = k;
				curParams.m_pAssignments  = &assignments;
				curParams.m_pItems		  = &items;
				curParams.m_pMeans		  = &means;
				curParams.m_pNumInCluster = &numInCluster;

				jobFuncs[i] = [&curParams, numJobs, &zero]() 
				{
					const int itemsPerJob = (curParams.m_pMeans->size() + (numJobs - 1)) / numJobs;
					const int startIndex  = itemsPerJob * curParams.m_index;
					const int maxIndex	  = Min(itemsPerJob * (curParams.m_index + 1),
												static_cast<int>(curParams.m_pMeans->size()));

					//MsgOut("Means job index: %d  start: %d end: %d\n");

					for (int im = startIndex; im < maxIndex; im++)
					{
						curParams.m_pMeans->at(im) = zero;
						curParams.m_pNumInCluster->at(im) = 0;
					}

					for (int ix = 0; ix < curParams.m_pItems->size(); ix++)
					{
						const int curAssignment = curParams.m_pAssignments->at(ix);
						if (curAssignment >= startIndex && curAssignment < maxIndex)
						{
							curParams.m_pMeans->at(curAssignment) += curParams.m_pItems->at(ix);
							curParams.m_pNumInCluster->at(curAssignment)++;
						}
					}

					for (int im = startIndex; im < maxIndex; im++)
					{
						if (curParams.m_pNumInCluster->at(im) > 0)
						{
							curParams.m_pMeans->at(im) /= (float)curParams.m_pNumInCluster->at(im);
						}
						else
						{
							for (int ix = 0; ix < curParams.m_pItems->size(); ix++)
							{
								if (curParams.m_pAssignments->at(ix) == im)
								{
									IABORT("Internal K-Means Assignment Error (empty cluster %d still has assignments)\n", im);
								}
							}
						}
					}
				};

				WorkItemHandle hJob = NdThreadPool::QueueWorkItem(KMeansWorkerThread, &jobFuncs[i]);
				jobHandles.push_back(hJob);
			}

			NdThreadPool::WaitAll(jobHandles);
		}

		static const DistType kMeansEpsilon = 0.00001f;

		const bool noDistortion = (prevDistortion <= kMeansEpsilon)
								  || ((Abs(curDistortion - prevDistortion) / prevDistortion) < kMeansEpsilon);

		INOTE_DEBUG("k-means iter %d : %f (delta: %f)%s\n", iter, curDistortion, Abs(curDistortion - prevDistortion), noDistortion ? " FINISHED" : "");

		if ((iter > 1) && noDistortion)
		{
			break;
		}

		prevDistortion = curDistortion;
	}

	const U64 endTime = TimerGetRawCount();
	const float durationSec = ConvertTicksToSeconds(endTime - startTime);
	
	INOTE_VERBOSE("k-means of %d items into %d clusters took %0.4f seconds\n", items.size(), k, durationSec);
}
