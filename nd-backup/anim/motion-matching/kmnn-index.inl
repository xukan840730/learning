/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "corelib/math/k-means.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include <algorithm>

#ifndef KM_NN_INDEX_H
#error "Do not include kmnn-index.inl directly.  kmnn-index.h instead."
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
template<typename I, typename M, typename A>
KMNNIndex<I, M, A>::~KMNNIndex()
{
	m_means.Reset();
	m_clusterExtents.Reset();
	m_distIndex.Reset();
	if (m_pMetric)
	{
		NDI_DELETE m_pMetric;
		m_pMetric = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
template<typename I, typename M, typename A>
void KMNNIndex<I, M, A>::Init(const ItemArray& vectors, Metric* pMetric, const float s)
{
	const int n = vectors.Size();
	const int k = s*Sqrt(n);

	m_pMetric = pMetric;	
	m_means.Init(k);
	m_distIndex.Init(n);
	m_clusterExtents.Init(k);

	ScopedTempAllocator alloc(FILE_LINE_FUNC);
	auto distSqr = [pMetric](const Item& a, const Item& b) {
		return Sqr(pMetric->Dist(a, b));
	};
	using DistType = typename M::DistType;

	ListArray<int> kMeansAssignments(n);
	ListArray<std::tuple<int, DistType, int>> clusterDistIndex(n);

	KMeans(vectors, k, std::function<DistType(const Item&, const Item&)>(distSqr), 2500, kMeansAssignments, m_means);

	for (int i = 0; i < kMeansAssignments.Size(); ++i)
	{
		clusterDistIndex.PushBack(std::make_tuple(kMeansAssignments[i], -pMetric->Dist(m_means[kMeansAssignments[i]], vectors[i]), i));		
	}

	std::sort(clusterDistIndex.Begin(), clusterDistIndex.End());

	for (int i = 0; i < clusterDistIndex.Size(); ++i)
	{
		m_distIndex.PushBack({ -std::get<1>(clusterDistIndex[i]), std::get<2>(clusterDistIndex[i]) });
	}

	int curClusterIndex = 0;
	int clusterStart = 0;	
	int i = 0;
	m_clusterExtents.Resize(k);

	//initialize the cluster extents
	for (auto& e : m_clusterExtents)
	{
		e = { 0, 0 };
	}

	for (i = 0; i < m_distIndex.Size(); ++i)
	{
		const int clusterIndex = std::get<0>(clusterDistIndex[i]);
		if (clusterIndex != curClusterIndex)
		{
			m_clusterExtents[curClusterIndex] = { clusterStart, i };
			curClusterIndex = clusterIndex;
			clusterStart = i;
		}		
	}
	m_clusterExtents[curClusterIndex] = { clusterStart, i };
	
	ANIM_ASSERT(m_clusterExtents.Size() == k);
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename I, typename M, typename A>
void KMNNIndex<I, M, A>::NearestNeighbors(const ItemArray& vectors,
										  const Item& query,
										  ListArray<std::tuple<float, int>>& nearestNeighbors,
										  int k) const
{
	ScopedTempAllocator alloc(FILE_LINE_FUNC);

	ListArray<std::tuple<float, int>> distanceToMeans(m_means.Size());
	
	nearestNeighbors.Clear();
	for (int i = 0; i < k; ++i)
	{
		nearestNeighbors.PushBack(std::make_tuple(kLargestFloat, 0));
	}
	float maxDist = kLargestFloat;
	
	auto pMetric = m_pMetric;
	auto dist = [pMetric](const Item& a, const Item& b) -> float {
		return  pMetric->Dist(a, b);
	};

	for (int iMean = 0; iMean < m_means.Size(); ++iMean)
	{
		distanceToMeans.PushBack(std::make_tuple(dist(m_means[iMean], query), iMean));
	}

	std::sort(distanceToMeans.Begin(), distanceToMeans.End());

	for (int iMean = 0; iMean < m_means.Size(); ++iMean)
	{
		float distToClusterMean;
		int clusterIndex;
		std::tie(distToClusterMean, clusterIndex) = distanceToMeans[iMean];		
		
		int clusterStart = m_clusterExtents[clusterIndex].m_start;
		int clusterEnd = m_clusterExtents[clusterIndex].m_end;		
		
		for (int i = clusterStart; i < clusterEnd; i++)
		{			
			if (maxDist <= distToClusterMean - m_distIndex[i].m_dist)
				break;

			const int pointIndex = m_distIndex[i].m_index;
			const float distToPoint = dist(query, vectors[pointIndex]);
			if (maxDist > distToPoint)
			{
				nearestNeighbors[k - 1] = std::make_tuple(distToPoint, pointIndex);
				std::sort(nearestNeighbors.Begin(), nearestNeighbors.End());
				maxDist = std::get<0>(nearestNeighbors[k - 1]);
			}
		}
	}	
}

/// --------------------------------------------------------------------------------------------------------------- ///
template<typename I, typename M, typename A>
bool KMNNIndex<I, M, A>::IsValid(const Metric& other) const
{
	return m_pMetric->Hash() == other.Hash();
}

/// --------------------------------------------------------------------------------------------------------------- ///
template<typename I, typename M, typename A>
KMNNIndexCollection<I, M, A>::KMNNIndexCollection(size_t capacity)
{
	m_indicies.Init(capacity);
}

/// --------------------------------------------------------------------------------------------------------------- ///
template<typename I, typename M, typename A>
KMNNIndexCollection<I, M, A>::~KMNNIndexCollection()
{
	for (auto it = m_indicies.begin(); it != m_indicies.end(); ++it)
	{
		Index* pCurIndex = *it;
		NDI_DELETE pCurIndex;
	}
	m_indicies.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
template<typename I, typename M, typename A>
void KMNNIndexCollection<I, M, A>::Add(Index* pIndex)
{
	m_indicies.PushBack(pIndex);
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename I, typename M, typename A>
void KMNNIndexCollection<I, M, A>::NearestNeighbors(const Metric& other,
													const ItemArray& vectors,
													const Item& query,
													ListArray<std::tuple<float, int>>& outNeighbors,
													int k) const
{
	for (auto it = m_indicies.begin(); it != m_indicies.end(); ++it)
	{
		if ((*it)->IsValid(other))
		{
			(*it)->NearestNeighbors(vectors, query, outNeighbors, k);
			return;
		}
	}
	ANIM_ASSERT(false);
	return;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template<typename I, typename M, typename A>
bool KMNNIndexCollection<I,M,A>::IsValid(const Metric& other) const
{
	for (auto it = m_indicies.begin(); it != m_indicies.end(); ++it)
	{
		if ((*it)->IsValid(other))
		{
			return true;
		}
	}
	return false;
}
