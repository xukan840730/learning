/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef FLOCKING_CONFIG_H
#define FLOCKING_CONFIG_H

namespace Flocking
{
	class FlockingGlobalConfig
	{
	public:
		bool m_debugRegions				= false;

		int m_rvoMaxNeighbors			= 32;
		float m_rvoNeighborRadius		= 3.3f;
		bool m_debugRvoNeighbors		= false;

		int m_rvoVelocitySamples		= 250;
		float m_rvoTimePenaltyWeight	= 7.5f;

		float m_simulationTimeStep		= 0.033f;
		bool m_debugSimulation			= false;
	};

	extern FlockingGlobalConfig g_flockingConfig;
}

#endif // FLOCKING_CONFIG_H


