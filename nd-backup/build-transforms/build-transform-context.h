/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "tools/pipeline3/build/build-transforms/build-scheduler.h"
#include "tools/pipeline3/common/path-converter.h"
#include "tools/pipeline3/build/tool-params.h"
#include <string>

class BuildScheduler;

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransformContext
{
public:
	BuildTransformContext(const std::string& assetName
						, const AssetType assetType
						, uint64_t buildId
						, const ToolParams& toolParams
						, BuildScheduler& buildScheduler)
						: m_assetName(assetName)
						, m_assetType(assetType)
						, m_buildId(buildId)
						, m_toolParams(toolParams)
						, m_buildScheduler(buildScheduler)
	{
	}

	bool Matches(const BuildTransformContext *pOtherContext) const { return MatchesAsset(pOtherContext->m_assetName, pOtherContext->m_assetType); }
	bool MatchesAsset(const std::string &assetName, AssetType assetType) const { return m_assetName == assetName && m_assetType == assetType; }

	const std::string& GetAssetName() const		{ return m_assetName; }
	AssetType GetAssetType() const { return m_assetType; }
	uint64_t GetBuildId() const { return m_buildId; }

public:
	//////// COMMON DATA
	const ToolParams& m_toolParams;

	///////// AUX DATA
	BuildScheduler& m_buildScheduler;		// The scheduler and dependency tree for all build transforms

private:
	const std::string m_assetName;			// Name of the asset (Builder actor/level) or soundbank/movie...
	const AssetType m_assetType;
	uint64_t m_buildId;
};
