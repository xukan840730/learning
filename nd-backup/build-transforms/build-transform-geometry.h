/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "tools/pipeline3/common/4_blendshape.h"

namespace pipeline3
{
	struct LightBoTexture;
}

class BuildTransformContext;

using namespace pipeline3;



struct BuildModuleGeometryOutput
{
public:

	SceneBlendShapeData actorBlendShapeData;
	U64 geoMemory;

	BuildModuleGeometryOutput()
	{
		geoMemory = 0;

	}

};

void BuildModuleGeometry_Configure(const BuildTransformContext *const pContext,
								const libdb2::Actor *const pDbActor,
								const std::vector<const libdb2::Actor*>& actorList,
								std::vector<std::string>& arrBoFiles);




