#pragma once

#include "tools/pipeline3/build/tool-params.h"

#include "build-transform.h"

class BuildTransformContext;

class TextureReadSpec : public BuildTransform
{
public:
	TextureReadSpec(const BuildTransformContext* pContext, const ToolParams &tool, const std::string &spawnerOutputPath, const std::string &buildOutputPath, const std::string& textureBoTransformInputFilename);
	~TextureReadSpec();

	virtual BuildTransformStatus Evaluate() override;

private:

	const ToolParams &m_toolParams;

	std::string m_texBuildSpawnerPath;
	std::string m_texBuildPath;
	std::string m_textureBoTransformInputFilename;
};