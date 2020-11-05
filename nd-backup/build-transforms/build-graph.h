/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include <string>

class BuildScheduler;
class BuildTransformContext;

DataHash GetTransformTableKeyHash();
DataHash GetNodeEdgeGraphKeyHash();
BuildPath GetGraphFilePath(uint64_t buildId);
BuildPath GetSchedulerGraphFilePath(long long executionId);

void WriteSingleAssetWebGraph(const BuildScheduler *const pBuildScheduler,
							std::string const& graphName,
							uint64_t buildId,
							const BuildTransformContext *const pContext);

void WriteSchedulerWebGraph(const BuildScheduler *const pBuildScheduler);


