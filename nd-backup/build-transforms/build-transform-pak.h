/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform-context.h"

#include <vector>
#include <set>
#include <string>

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_Pak : public BuildTransform
{
public:
	BuildTransform_Pak(const BuildTransformContext* pContext,
					   const BigStreamWriterConfig& streamConfig,
					   I16 pakHeaderFlags = 0);

	BuildTransformStatus Evaluate() override;

private:
	BigStreamWriterConfig m_streamConfig;
	I16 m_pakHeaderFlags;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransformPak_Configure(const BuildTransformContext* pContext,
								 const std::string& pakFilename,
								 const std::vector<std::string>& arrBoFiles,
								 U32 pakHdrFlags = 0);
