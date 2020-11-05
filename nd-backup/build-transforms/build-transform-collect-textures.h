/*
* Copyright (c) 2016 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform.h"
#include "tools/pipeline3/common/4_textures-old-texture-loader.h"

namespace libdb2
{
	class Actor;
	class Level;
}


class BuildTransform_CollectTexturesFromMaterials : public  BuildTransform
{
public:
	BuildTransform_CollectTexturesFromMaterials(const BuildTransformContext *const pContext, 
												bool lightmapsOverrideEnabled);
	BuildTransform_CollectTexturesFromMaterials(const BuildTransformContext *const pContext, 
												const std::string& levelName);

	virtual BuildTransformStatus Evaluate() override;

private:

	void AddAlternateTextures(TextureGenerationDataEx& tgd, 
							const libdb2::Level *const pLevel);
};