/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "common/libmath/aabb.h"

#include <vector>
#include <algorithm>
#include <stdint.h>

// We only expect PointType to have a member called m_position that has a float Get(uint axis) function.

template <typename PointType>
class KdPointTree
{
public:
	struct NearestNeighbor
	{
		const PointType *m_pPoint;
		float m_distanceSquared;
	};

public:
	KdPointTree()
	{
	}

	KdPointTree(const PointType *points, uint32_t numPoints)
	{	
		if (numPoints > 0)
		{
			m_nodes.reserve(numPoints);
			m_points.resize(numPoints);

			std::vector<const PointType*> buildPoints;
			buildPoints.reserve(numPoints);
			for (uint32_t i = 0; i < numPoints; ++i)
				buildPoints.push_back(&points[i]);

			RecursiveBuild(0, numPoints, buildPoints);
		}
	}

	template <typename Visitor>
	void NNearestNeighbors(const Point &position, float maxDistSquared, Visitor &visitor) const
	{
		if (m_nodes.size() > 0)
		{
			InternalNearestNeighbors(0, position, maxDistSquared, visitor);
		}
	}

private:
	struct Node
	{
		void InitLeaf()
		{
			m_splitAxis = 3;
		}

		void InitNode(float splitPos, uint32_t splitAxis, bool hasLeftChild,
			uint32_t rightChild)
		{
			m_splitPos = splitPos;
			m_splitAxis = splitAxis;
			m_hasLeftChild = hasLeftChild? 1 : 0;
			m_rightChild = rightChild >= (1 << 29)? (1 << 29) - 1 : rightChild;
		}

		float m_splitPos;
		uint32_t m_splitAxis : 2;
		uint32_t m_hasLeftChild : 1;
		uint32_t m_rightChild : 29;
	};

	template <typename Visitor>
	void InternalNearestNeighbors(uint32_t nodeNum, const Point &position,
		float &maxDistSquared, Visitor &visitor) const
	{
		const Node &node = m_nodes[nodeNum];

		uint32_t splitAxis = node.m_splitAxis;

		if (splitAxis != 3)
		{
			float planeDistSquared = (position.Get(splitAxis) - node.m_splitPos)*(position.Get(splitAxis) - node.m_splitPos);

			uint32_t child0 = nodeNum + 1,
				child1 = node.m_rightChild;
			bool hasChild0 = node.m_hasLeftChild == 1;
			bool hasChild1 = node.m_rightChild < (1 << 29) - 1;

			if (position[splitAxis] > node.m_splitPos)
			{
				std::swap(child0, child1);
				std::swap(hasChild0, hasChild1);
			}

			if (hasChild0)
			{
				InternalNearestNeighbors(child0, position, maxDistSquared, visitor);
			}

			if (hasChild1 && planeDistSquared < maxDistSquared)
			{
				InternalNearestNeighbors(child1, position, maxDistSquared, visitor);
			} 
		}

		const PointType &point = m_points[nodeNum];
		SMath::Vec4 vec = (position - Point(point.m_position.Get(0), point.m_position.Get(1), point.m_position.Get(2))).GetVec4();
		float distSquared = Dot3(vec, vec);
		if (distSquared < maxDistSquared)
		{
			visitor(point, distSquared, maxDistSquared);
		}
	}

	uint32_t RecursiveBuild(uint32_t start, uint32_t end, std::vector<const PointType*> &buildPoints)
	{
		uint32_t nodeIndex = AllocateNode();

		if (start + 1 == end)
		{
			m_nodes[nodeIndex].InitLeaf();
			m_points[nodeIndex] = *buildPoints[start];
			return nodeIndex;
		}

		// Find the bound of our current dataset
		Aabb bounds;
		for (uint32_t i = start; i < end; ++i)
		{
			bounds.IncludePoint(Point(m_points[i].m_position.Get(0), m_points[i].m_position.Get(1), m_points[i].m_position.Get(2)));
		}

		uint32_t splitAxis;
		Vector extents = bounds.GetSize();
		
		if (extents.X() > extents.Y() && extents.X() > extents.Z())
			splitAxis = 0;
		else if (extents.Y() > extents.Z())
			splitAxis = 1;
		else
			splitAxis = 2;

		uint32_t splitPos = (start+end)/2;

		struct PointComparer
		{
			PointComparer(uint32_t axis)
				: m_axis(axis)
			{
			}

			bool operator()(const PointType *lhs, const PointType *rhs)
			{
				float lhsPos = lhs->m_position.Get(m_axis), rhsPos = rhs->m_position.Get(m_axis);
				return lhsPos == rhsPos? lhs < rhs : lhsPos < rhsPos;
			}

			uint32_t m_axis;
		};

		// Partition the elements based on the median element
		std::nth_element(&buildPoints[start], &buildPoints[splitPos], &buildPoints[end-1] + 1, PointComparer(splitAxis));
		
		m_points[nodeIndex] = *buildPoints[splitPos];

		bool hasLeftChild = splitPos > start;
		bool hasRightChild = splitPos < end - 1;

		if (hasLeftChild)
		{
			RecursiveBuild(start, splitPos, buildPoints);
		}

		uint32_t rightChild = 0xFFFFFFFF;
		if (hasRightChild)
			rightChild = RecursiveBuild(splitPos + 1, end, buildPoints);

		m_nodes[nodeIndex].InitNode(buildPoints[splitPos]->m_position.Get(splitAxis), splitAxis,
			hasLeftChild, rightChild);

		return nodeIndex;
	}

	uint32_t AllocateNode()
	{
		uint32_t index = static_cast<uint32_t>(m_nodes.size());
		m_nodes.push_back(Node());
		return index;
	}

	std::vector<Node> m_nodes;
	std::vector<PointType> m_points;
};
