/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nav/action-pack-handle.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/path-waypoints-ex.h"

#include "gamelib/scriptx/h/nd-ai-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NavNodeKey;
struct NavNodeData;

namespace DMENU
{
	class Menu;
}

namespace Nav
{
	class PathFindNodeData;

	enum BuildPathSmoothingParam
	{
		kNoSmoothing = 0,
		kFullSmoothing = 1,
		kApproxSmoothing = 2,
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct BuildPathParams
	{
		static CONST_EXPR U32 kNumExposureResults = 2;

		const PathFindContext* m_pContextOverride = nullptr;

		float m_portalShrink		 = 0.0f;
		float m_finalizeProbeMinDist = -1.0f;
		float m_finalizeProbeMaxDist = -1.0f;
		float m_finalizeProbeMaxDurationMs = -1.0f;
		float m_apEntryDistance = -1.0f;

		I64 m_wideTapInstanceSeed = -1;
		I32 m_truncateAfterTap = 0;

		BuildPathSmoothingParam m_smoothPath	 = kFullSmoothing;
		DC::ExposureSourceMask m_exposureParams[kNumExposureResults] = { 0 };

		bool m_finalizePathWithProbes	  = false;
		bool m_calculateAdditionalResults = false;
		bool m_reversePath = false;

		bool m_debugDrawResults		  = false;
		bool m_debugDrawPortals		  = false;
		bool m_debugDrawRadialEdges	  = false;
		bool m_debugDrawRadialCorners = false;
		DebugPrimTime m_debugDrawTime = kPrimDuration1FramePauseable;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	void CreateBuildPathParamsDevMenu(DMENU::Menu* pMenu, BuildPathParams* pParamsToUse);

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct BuildPathResults
	{
		static CONST_EXPR U32 kNumCachedPolys = 8;

		BuildPathResults()
		{
			Clear();
		}

		void Clear()
		{
			m_pathWaypointsPs.Clear();

			m_hNavActionPack = ActionPackHandle();

			m_combatVectorCost		= 0.0f;
			m_length				= 0.0f;
			m_pathClosestThreatDist = 0.0f;
			m_pathClosestFriendDist = 0.0f;
			m_navTapRotationDeg		= 0.0f;

			m_backtrack = false;
			m_goalFound  = false;
			m_usesTap = false;

			m_pNodeData   = nullptr;
			m_maxNodeData = 0;
			m_numNodeData = 0;
		}

		PathWaypointsEx		m_pathWaypointsPs;

		ActionPackHandle	m_hNavActionPack;
		NavMeshHandle		m_hNextNavMesh;
		NavPolyHandle		m_cachedPolys[kNumCachedPolys];

		Vector				m_initialWaypointDirWs;
		Vector				m_finalWaypointDirWs;

		F32					m_combatVectorCost;
		F32					m_length;

		NavNodeData*		m_pNodeData;
		U32					m_maxNodeData;
		U32					m_numNodeData;

		F32					m_pathClosestThreatDist;
		F32					m_pathClosestFriendDist;
		F32					m_navTapRotationDeg;

		F32					m_exposure[BuildPathParams::kNumExposureResults];

		bool				m_goalFound : 1;
		bool				m_usesTap : 1;
		bool				m_backtrack : 1;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct BuildPathFastResults
	{
		BuildPathFastResults()
		{
			Clear();
		}

		void Clear()
		{
			m_goalFound = false;
			m_initialPathTowardsGoal = false;
			m_initialPathForward	 = false;
		}

		bool	m_goalFound : 1;
		bool	m_initialPathTowardsGoal : 1;
		bool	m_initialPathForward : 1;
		bool	m_usesTap : 1;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Build path to a specific goal using A* results
	void BuildPath(const PathFindNodeData& searchData,
				   const BuildPathParams& buildParams,
				   const NavNodeKey& goalKey,
				   Point_arg requestedGoalPosWs,
				   BuildPathResults* pPathResults);

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Quickly ApproxSmooth and only keep track of distance, without creating or caching a path nor any additional path results
	float GetFastApproxSmoothDistanceOnly(const PathFindNodeData& searchData,
#ifdef HEADLESS_BUILD
										  const NavNodeKey goalKey,
#else
										  const U16 key,
#endif
										  Point_arg requestedGoalPosWs,
										  bool* const pPathCrossesTap = nullptr);

	/// --------------------------------------------------------------------------------------------------------------- ///
	// Same as above, but with minimal (fast!) results
	void BuildPathFast(const PathFindNodeData& searchData,
					   const NavNodeKey goalKey,
					   Point_arg goalPosWs,
					   BuildPathFastResults* pPathResults);

	/// --------------------------------------------------------------------------------------------------------------- ///
	void DebugPrintBuildPathParams(const BuildPathParams& params, MsgOutput output, const char* preamble = "params");
}
