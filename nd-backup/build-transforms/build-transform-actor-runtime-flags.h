/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "common/libphys/pat.h"
#include "tools/pipeline3/common/blobs/build-file.h"

namespace libdb2
{
	class Actor;
}
class ToolParams;
class BuildTransformContext;

void BuildModuleActorRuntimeFlags_Configure(
	const BuildTransformContext* pContext,
	const std::vector<const libdb2::Actor*>& actorList,
	std::vector<std::string>& outBoFiles);
