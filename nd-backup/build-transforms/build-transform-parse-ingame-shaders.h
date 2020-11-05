/*
 * Copyright (c) 2018 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform.h"

#include "common/util/error.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ToolParams;

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_ParseIngameShaderFilesJson : public BuildTransform
{
public:
	BuildTransform_ParseIngameShaderFilesJson(const std::string &projectName, const BuildTransformContext *pContext);

	BuildTransformStatus Evaluate() override;

private:
	std::string m_projectName;
};

/// --------------------------------------------------------------------------------------------------------------- ///
Err ScheduleIngameShaders(const ToolParams &toolParams, const std::string &assetName, BuildTransformContext *pContext);

/// --------------------------------------------------------------------------------------------------------------- ///
std::string ConcatPath(const std::string &base, const std::string &file);
