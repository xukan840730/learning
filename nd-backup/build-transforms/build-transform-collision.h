/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "common/libphys/pat.h"
#include "tools/pipeline3/common/blobs/build-file.h"
#include "tools/libs/toolsutil/farm.h"

namespace libdb2
{
	class Actor;
}
class ToolParams;
class BuildTransformContext;

// Data shared across build items. Combined pats and memory usage for a 
// collection of related collision actor BO files.
struct AggregateCollisionData {

	AggregateCollisionData() : m_collMemoryBytes(0)
	{
		m_combinedPats.Clear();
		m_floorPats.Clear();
	}

	std::vector<std::string> m_arrBoFiles;
	PatSurfaceTypeBitArray m_combinedPats;
	PatSurfaceTypeBitArray m_floorPats;
	U32 m_collMemoryBytes;
};

void BuildModuleCollision_Configure(const BuildTransformContext *const pContext, 
									const libdb2::Actor *const pDbActor, 
									const std::vector<const libdb2::Actor*>& actorList, 
									std::vector<std::string>& outArrBoFiles, 
									std::vector<BuildPath>& outArrCollisionToolPath);


//--------------------------------------------------------------------------------------------------------------------//
struct CollisionExportItem
{
	CollisionExportItem() : m_jobId(FarmJobId::kInvalidFarmjobId)
	{
	}

	std::string m_geoName;
	std::string m_geoCollSet;
	std::string m_geoFullName;
	std::string m_skelSet;

	std::string m_collBuildDir;				
	mutable FarmJobId m_jobId;
};


//--------------------------------------------------------------------------------------------------------------------//
struct CollisionBuildItem
{
	CollisionBuildItem()
	{
	}

	std::string m_skelSet;
	std::string m_skelSceneFile;
	bool m_geoGenerateFeaturesOnAllSides;
	bool m_geoPrerollHavokSimulation;
	std::string m_gameFlagsStr;
	std::string m_actorName;

	std::string m_collBoBuildDir;
};


//--------------------------------------------------------------------------------------------------------------------//

// Pat data shared across build items. Combined pats and memory usage for a 
// collection of related collision actor BO files.
struct GatherPatItem
{
	std::vector<std::string> m_arrCollBoFiles;

	// input
	std::vector<std::string> m_arrPatInputFiles;

	// output
	std::string m_combinedPatFilename;
	std::string m_combinedFloorPatFilename;
};

