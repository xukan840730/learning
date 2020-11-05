/*
* Copyright (c) 2016 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/
#pragma once

/*
* This implements a k-nearest neighbor search index using the algorithm described in 
* "A Fast Exact k-Nearest Neighbors Algorithm for High Dimensional Search Using k-Means Clustering and Triangle Inequality"
* http://www.ncbi.nlm.nih.gov/pmc/articles/PMC3255306/
*/

#ifndef KM_NN_INDEX_H
#define KM_NN_INDEX_H

#include <tuple>

#include "corelib/containers/list-array.h"

template<typename TItem, typename TMetric, typename TArrayLikeItem>
class KMNNIndex
{
public:
	using Item = TItem;
	using Metric = TMetric;
	using ItemArray = TArrayLikeItem;

	~KMNNIndex();
	void Init(const ItemArray& vectors, Metric* pMetric, const float s);
	void NearestNeighbors(const ItemArray& vectors, const Item& query, ListArray<std::tuple<float, int>>& outNeighbors, int k) const;
	bool IsValid(const Metric& other) const;

private:	
	struct ClusterExtents
	{
		int m_start;
		int m_end;
	};

	struct DistanceIndex
	{
		typename Metric::DistType m_dist;
		int m_index;
	};

	Metric* m_pMetric = nullptr;
	ListArray<Item> m_means;
	ListArray<ClusterExtents> m_clusterExtents;
	ListArray<DistanceIndex> m_distIndex;
};

template <typename TItem, typename TMetric, typename TArrayLikeItem>
class KMNNIndexCollection
{
public:
	using Index = KMNNIndex < TItem, TMetric, TArrayLikeItem>;
	using Item = typename Index::Item;
	using Metric = typename Index::Metric;
	using ItemArray = typename Index::ItemArray;

	KMNNIndexCollection(size_t capacity);
	~KMNNIndexCollection();

	void Add(Index* pIndex);
	void NearestNeighbors(const Metric& other, const ItemArray& vectors, const Item& query, ListArray<std::tuple<float, int>>& outNeighbors, int k) const;
	bool IsValid(const Metric& other) const;

private:
	ListArray<Index*> m_indicies;
};

#include "gamelib/anim//motion-matching/kmnn-index.inl"

#endif //KM_NN_INDEX_H