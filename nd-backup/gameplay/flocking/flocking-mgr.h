/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef FLOCKING_MGR_H
#define FLOCKING_MGR_H

#include "gamelib/region/region.h"

namespace Flocking
{
	class FlockingAgent;
	class FlockingRvoObstacle;

	// Agent ---------------------------------------------------------------------------------------------------------------------------------------------
	static CONST_EXPR U32 kMaxFlockingAgents = 64;

	FlockingAgent* AddFlockingAgent(const Point_arg initialAgentPos, const Vector_arg initialAgentForward, const StringId64 flockingParamsId);
	void RemoveFlockingAgent(FlockingAgent *const pAgent);

	void FindAllFlockingAgents(FlockingAgent* outAgents[kMaxFlockingAgents], U32F& outNumAgents);
	void FindSelectedFlockingAgents(FlockingAgent* outAgents[kMaxFlockingAgents], U32F& outNumAgents);

	void SelectAllFlockingAgents(FlockingAgent* outAgents[kMaxFlockingAgents], U32F& outNumAgents);
	void SelectFlockingAgents(Point_arg startPtWs, Point_arg endPtWs, FlockingAgent* outAgents[kMaxFlockingAgents], U32F& outNumAgents);
	// ---------------------------------------------------------------------------------------------------------------------------------------------------

	// Region --------------------------------------------------------------------------------------------------------------------------------------------
	class WanderRegion
	{
	public:
		void Init(const Region *const pRegion)
		{
			GAMEPLAY_ASSERT(pRegion);

			m_pRegion = pRegion;
			m_population = 0;
			m_maxPopulation = pRegion->GetTagData(SID("flocking-region-population"), 4);
			m_isBlocked = false;
			m_isEnabled = true;
		}

	public:
		const Region* m_pRegion;
		int m_population;
		int m_maxPopulation;
		bool m_isBlocked;
		bool m_isEnabled;
	};

	static CONST_EXPR U32 kMaxFlockingRegions = 64;

	void SetRegionEnabled(const StringId64 regionNameId, bool isEnabled);
	bool IsRegionBlocked(const StringId64 regionNameId);
	const WanderRegion* GetRegionByNameId(const StringId64 regionNameId);
	// ---------------------------------------------------------------------------------------------------------------------------------------------------

	// Obstacle ------------------------------------------------------------------------------------------------------------------------------------------
	static CONST_EXPR U32 kMaxFlockingRvoObstacles = 256;

	FlockingRvoObstacle* AddFlockingRvoObstacle(const Point_arg p0, const Point_arg p1);
	void ClearFlockingRvoObstacles();

	void FindAllFlockingRvoObstacles(FlockingRvoObstacle* outRvoObstacles[kMaxFlockingRvoObstacles], U32F& outNumRvoObstacles);
	// ---------------------------------------------------------------------------------------------------------------------------------------------------

	// Simulation ----------------------------------------------------------------------------------------------------------------------------------------
	void InitSimulation();
	void StepSimulation(const Point_arg playerPos);
	void ShutdownSimulation();
	// ---------------------------------------------------------------------------------------------------------------------------------------------------
}

#endif // FLOCKING_MGR_H


