/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#ifndef NAV_CLUSTER_H
#define NAV_CLUSTER_H

#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-path-find.h"

namespace Nav
{
	typedef HashTable<NavManagerId, U32, false> NavPolyTable;

	struct PolyCluster
	{
		static const U8 kMaxPolysPerCluster = 128;
		void AddPoly(NavPolyHandle hPoly, Point_arg posWs);

		bool IsFull() const { return m_numPolys >= kMaxPolysPerCluster; }

		bool IsEmpty() const { return m_numPolys == 0; }

		bool IsValid(F32 minAxis)
		{
			if (IsEmpty())
				return false;

			const Vector boundsSize = VectorXz(m_bounds.GetSize());
			return (Max(boundsSize.X(), boundsSize.Z()) > minAxis);
		}

		void Clear()
		{
			m_numPolys = 0;
			m_bounds.SetEmpty();
		}

		const Aabb& GetBounds() const { return m_bounds; }

		Aabb m_bounds;
		NavPolyHandle m_hPolys[kMaxPolysPerCluster];
		U32 m_numPolys;
	};

	struct ClusterPolyResults
	{
		void Init(U32 maxPolys, const char* srcFile, const U32 srcLine, const char* srcFunc)
		{
			m_table.Init(maxPolys, srcFile, srcLine, srcFunc);
		}

		void StoreCluster(const PolyCluster& cluster, U32 clusterIdx)
		{
			for (U32 polyNum = 0; polyNum < cluster.m_numPolys; polyNum++)
			{
				const NavPolyHandle hPoly = cluster.m_hPolys[polyNum];
				m_table.Add(hPoly.GetManagerId(), clusterIdx);
			}

			m_numClusters = Max(m_numClusters, clusterIdx + 1);
		}

		U32 m_numClusters = 0;
		bool m_overflowedClosedList : 1;
		NavPolyTable m_table;
	};

	struct ClusterPolyParams : public PathFindParams
	{
		U32 m_maxClusters = 0;
		F32 m_maxClusterAxis = 0.f;
		F32 m_minClusterAxis = 0.f;

		NavPathNodeMgr::NodeBits m_validNodes;
		bool m_restrictNodes : 1;
	};

	void ClusterPolys(const ClusterPolyParams& clusterParams, ClusterPolyResults* pClusterResults);

} // namespace Nav

#endif // NAV_CLUSTER_H

