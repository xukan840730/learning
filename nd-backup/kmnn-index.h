/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "tools/pipeline3/common/k-means.h"

#include <memory>
#include <tuple>
#include <vector>

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename TItem, typename TMetric, typename TArrayLikeItem>
class KMNNIndex
{
public:
	using Item		 = TItem;
	using MetricType = TMetric;
	using ItemArray	 = TArrayLikeItem;

	struct ClusterExtents
	{
		int m_start;
		int m_end;
	};

	struct DistanceIndex
	{
		typename MetricType::DistType m_dist;
		int m_index;
	};

	KMNNIndex(const MetricType& metric);
	void Init(const ItemArray& vectors, const float s, const TItem& zero);
	void NearestNeighbors(const ItemArray& vectors,
						  const Item& query,
						  std::vector<std::tuple<float, int>>& outNeighbors,
						  int k) const;
	bool IsValid(const MetricType& other) const;

	const std::vector<ClusterExtents>& Clusters() const { return m_clusterExtents; }
	const std::vector<DistanceIndex>& Distances() const { return m_distIndex; }
	const std::vector<Item>& Means() const { return m_means; }
	const MetricType& Metric() const { return m_metric; }

private:
	MetricType m_metric;
	std::vector<Item> m_means;
	std::vector<ClusterExtents> m_clusterExtents;
	std::vector<DistanceIndex> m_distIndex;
};

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename I, typename M, typename A>
KMNNIndex<I, M, A>::KMNNIndex(const MetricType& metric) : m_metric(metric)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename I, typename M, typename A>
void KMNNIndex<I, M, A>::Init(const ItemArray& vectors, const float s, const I& zero)
{
	const int n = vectors.size();
	const int k = Min(int(s * Sqrt(n)), n);

	m_means.resize(k);

	auto distSqr   = [this](const Item& a, const Item& b) { return Sqr(this->m_metric.Dist(a, b)); };
	using DistType = typename M::DistType;

	std::vector<int> kMeansAssignments;
	kMeansAssignments.reserve(n);

	KMeans(vectors, k, std::function<DistType(const Item&, const Item&)>(distSqr), 2500, kMeansAssignments, m_means, zero);

	std::vector<std::tuple<int, DistType, int>> clusterDistIndex;
	clusterDistIndex.reserve(n);

	for (int i = 0; i < kMeansAssignments.size(); ++i)
	{
		const float meanIndex = kMeansAssignments[i];
		const DistType distToMean = m_metric.Dist(m_means[meanIndex], vectors[i]);

		clusterDistIndex.push_back(std::make_tuple(meanIndex, -distToMean, i));
	}

	std::sort(clusterDistIndex.begin(), clusterDistIndex.end());

	m_distIndex.reserve(n);
	for (int i = 0; i < clusterDistIndex.size(); ++i)
	{
		m_distIndex.push_back({ -std::get<1>(clusterDistIndex[i]), std::get<2>(clusterDistIndex[i]) });
	}

	int curClusterIndex = 0;
	int clusterStart	= 0;
	int i = 0;
	m_clusterExtents.resize(k);

	// initialize the cluster extents
	for (auto& e : m_clusterExtents)
	{
		e = { 0, 0 };
	}

	for (i = 0; i < m_distIndex.size(); ++i)
	{
		const int clusterIndex = std::get<0>(clusterDistIndex[i]);
		if (clusterIndex != curClusterIndex)
		{
			m_clusterExtents[curClusterIndex] = { clusterStart, i };
			curClusterIndex = clusterIndex;
			clusterStart	= i;
		}
	}
	m_clusterExtents[curClusterIndex] = { clusterStart, i };

	ALWAYS_ASSERT(m_clusterExtents.size() == k);
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename I, typename M, typename A>
void KMNNIndex<I, M, A>::NearestNeighbors(const ItemArray& vectors,
										  const Item& query,
										  std::vector<std::tuple<float, int>>& nearestNeighbors,
										  int k) const
{

	std::vector<std::tuple<float, int>> distanceToMeans;
	distanceToMeans.reserve(m_means.size());

	nearestNeighbors.clear();
	for (int i = 0; i < k; ++i)
	{
		nearestNeighbors.emplace_back(std::make_tuple(kLargestFloat, 0));
	}
	float maxDist = kLargestFloat;

	auto dist = [this](const Item& a, const Item& b) -> float { return this->m_metric.Dist(a, b); };

	for (int iMean = 0; iMean < m_means.size(); ++iMean)
	{
		distanceToMeans.emplace_back(std::make_tuple(dist(m_means[iMean], query), iMean));
	}

	std::sort(distanceToMeans.begin(), distanceToMeans.end());

	for (int iMean = 0; iMean < m_means.size(); ++iMean)
	{
		float distToClusterMean;
		int clusterIndex;
		std::tie(distToClusterMean, clusterIndex) = distanceToMeans[iMean];

		int clusterStart = m_clusterExtents[clusterIndex].m_start;
		int clusterEnd	 = m_clusterExtents[clusterIndex].m_end;

		for (int i = clusterStart; i < clusterEnd; i++)
		{
			if (maxDist <= distToClusterMean - m_distIndex[i].m_dist)
				break;

			const int pointIndex	= m_distIndex[i].m_index;
			const float distToPoint = dist(query, vectors[pointIndex]);
			if (maxDist > distToPoint)
			{
				nearestNeighbors[k - 1] = std::make_tuple(distToPoint, pointIndex);
				std::sort(nearestNeighbors.begin(), nearestNeighbors.end());
				maxDist = std::get<0>(nearestNeighbors[k - 1]);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename I, typename M, typename A>
bool KMNNIndex<I, M, A>::IsValid(const MetricType& other) const
{
	return m_metric == other;
}
