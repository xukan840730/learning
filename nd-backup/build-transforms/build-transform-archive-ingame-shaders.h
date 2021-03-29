/*
 * Copyright (c) 2018 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform-ingame-shader.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_ArchiveIngameShaders : public BuildTransform
{
public:
	BuildTransform_ArchiveIngameShaders(const IngameShadersConfig& config, const BuildTransformContext* pContext);

	BuildTransformStatus Evaluate() override;

private:
	IngameShadersConfig m_config;
};