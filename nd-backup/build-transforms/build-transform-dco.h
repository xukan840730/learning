/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform.h"

#include "common/util/error.h"

/// --------------------------------------------------------------------------------------------------------------- ///
struct DcoConfig;
class ToolParams;

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_ParseDco : public BuildTransform
{
public:
	enum class Mode
	{
		kBins,
		kHeaders,
		kBuiltIns,
	};

	BuildTransform_ParseDco(const std::string& projectName, Mode mode, const BuildTransformContext* pContext);

	BuildTransformStatus Evaluate() override;

private:
	void BuildBins(const DcoConfig& dcoConfiguration);
	void BuildHeaders(const DcoConfig& dcoConfiguration);
	void BuildBuiltIns(const DcoConfig& dcoConfiguration);

	std::string m_projectName;
	Mode m_mode;
};

/// --------------------------------------------------------------------------------------------------------------- ///
Err ScheduleDcBins(const ToolParams& toolParams, const std::string& assetName, BuildTransformContext* pContext);
Err ScheduleDcHeaders(const ToolParams& toolParams, const std::string& assetName, BuildTransformContext* pContext);
Err ScheduleDcBuiltins(const ToolParams& toolParams, const std::string& assetName, BuildTransformContext* pContext);
