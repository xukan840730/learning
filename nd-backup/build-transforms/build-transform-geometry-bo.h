/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

namespace libdb2
{
	class Actor;
}

struct BuildModuleMaterialsOutput;
struct BuildModuleTexturesOutput;
struct BuildModuleGeometryOutput;
class BuildTransformContext;
class TransformInput;

bool BuildModuleGeometryBo_Configure(const BuildTransformContext *const pContext, 
									const libdb2::Actor *const pDbActor, 
									const std::vector<const libdb2::Actor*>& actorList, 
									std::vector<std::string>& arrBoFiles, 
									const std::vector<TransformInput>& inputFiles);




