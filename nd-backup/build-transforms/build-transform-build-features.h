#pragma once

#include "build-transform.h"
#include "tools/pipeline3/build/tool-params.h"
#include "tools/pipeline3/ingame3pak/schema.h"

class BuildTransformContext;
class SchemaDependencyToken;

class BuildTransform_BuildFeatures : public BuildTransform
{
public:
	static std::vector<TransformOutput> FeatureBuildOutputs(const AssetType assetType, const std::string& assetName);

	BuildTransform_BuildFeatures(const BuildTransformContext *const pContext, 
								const std::string& levelName,
								const SchemaDependencyToken& m_token, 
								const bool keepNoAimCorners);
	
	BuildTransformStatus Evaluate() override;
	SchemaDependencyToken m_token;
};

class BuildTransform_EmitFeatures : public BuildTransform
{
public:
	BuildTransform_EmitFeatures(const std::string& levelName, 
								const ToolParams& toolParams);

	virtual BuildTransformStatus Evaluate() override;

private:
	const ToolParams &m_toolParams;
};