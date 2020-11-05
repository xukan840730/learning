#pragma once

#include "build-transform.h"

#include <string>

class BuildTransformContext;
class ShaderDescription;
class ToolParams;

namespace libdb2
{
	class Actor;
}

namespace ITSCENE
{
	class CgfxShader;
}

namespace material_exporter
{
	class FeatureFilter;
}

class MatExtractSpawnerTransform : public BuildTransform
{
public:
	MatExtractSpawnerTransform(const BuildTransformContext *const pContext,
							const std::string& actorName, 
							const std::string& subactorBaseName,
							const size_t subactorLod,
							const char *const matBuilderInputPath);

	MatExtractSpawnerTransform(const BuildTransformContext *const pContext,
							const std::string& levelName, 
							const char *const matBuilderInputPath);

	BuildTransformStatus Evaluate() override;

private:
	const std::string m_subactorBaseName;
	const size_t m_subactorLod;
	const ToolParams &m_toolParams;
	std::string m_matBuilderInputPath;
};