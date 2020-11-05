/*
 * Copyright (c) 2016 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform.h"
#include "tools/pipeline3/common/4_textures-old-texture-loader.h"

namespace BigWriter
{
	struct Texture;
}

namespace libdb2
{
	class LightmapsOverride;
}

namespace pipeline3
{
	struct LightBoTexture;
	class LoadedTextureId;
}

namespace material_compiler
{
	struct CompiledEffect;
}

class BuildTransformContext;

using namespace pipeline3;

struct BuildModuleTexturesOutput
{
	std::vector<std::pair<LoadedTextureId, TextureLoader::TextureGenerationSpec*> > m_texturesHashesAndTgds; //This is pointing to the m_tgd structure
	TextureGenerationData m_tgd;
};

class BuildTransform_CollectLightmaps : public BuildTransform
{
	const ToolParams& m_toolParams;

public:
	BuildTransform_CollectLightmaps(const std::string& actorBaseName, 
								size_t actorLod,  
								const ToolParams& toolParams);
	BuildTransform_CollectLightmaps(const std::string& levelName, 
								const ToolParams& toolParams);

	virtual BuildTransformStatus Evaluate() override;
};

class BuildTransform_ConsolidateAndBuildTextures : public  BuildTransform
{
	const ToolParams &m_toolParams;

	bool WriteBoFiles(TextureGenerationData& tgd,
					  const std::vector<size_t>& textureArrayIndex2TGDIndex,
					  const std::vector<const BigWriter::Texture*>& builtTexturesPtrs,
					  std::string& extraTexturesExportPath,
					  const std::vector<std::unique_ptr<BigWriter::Texture>>& builtTextures);

	bool WriteCache(const std::vector<std::unique_ptr<BigWriter::Texture>>& builtTextures, TextureGenerationData& tgd) const;

public:
	BuildTransform_ConsolidateAndBuildTextures(const BuildTransformContext *const pContext
											, const ToolParams &toolParams
											, bool isLevel
											, const std::string& assetName);

	virtual BuildTransformStatus Evaluate() override;
};

void BuildModuleTextures_Configure(const BuildTransformContext *const pContext, 
								const libdb2::Actor *const pDbActor, 			// First actor in actorList
								const std::vector<const libdb2::Actor*>& actorList, 
								std::string& textureBoFilename, 
								std::string& textureBoLowFilename, 
								std::string& lightBoFilename,
								U32 numLightSkels);





