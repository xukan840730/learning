#pragma once

#include "build-transform.h"
#include "build-transform-spawner.h"

#include "tools/pipeline3/build/util/shader-description.h"

#include <string>
#include <vector>

namespace ITSCENE
{
	class CgfxShader;
}

namespace libdb2
{
	class Actor;
}

namespace matdb
{
	class MatDb;
}

namespace material_compiler
{
	struct CompiledEffect;
}

class ToolParams;

struct MaterialExtractTransformSpawnDesc : public BuildTransformSpawnDesc
{
	MaterialExtractTransformSpawnDesc();

	//Those are here for debug purpose so we can save what material is using the shader
	std::string m_shaderHash;
	std::vector<std::string> m_materials;

	BuildTransform* CreateTransform(const BuildTransformContext *pContext) const override;
	void ReadExtra(NdbStream &stream, const char *pSymData) override;
	void WriteExtra(NdbStream &stream, const char *pSymData) override;
};

class MaterialExtractTransform : public BuildTransform
{
public:
	static const char* INPUT_NICKNAME_SHADERS_LIST;

	MaterialExtractTransform(const ToolParams &toolParams);
	~MaterialExtractTransform();

	BuildTransformStatus Evaluate() override;

	BuildTransformStatus ResumeEvaluation(const SchedulerResumeItem& resumeItem) override;

	void OnJobError() override;

	void AddTextureDependency(const ITSCENE::CgfxShader* pShader, const material_compiler::CompiledEffect& effect, matdb::MatDb& matDb);

	void ExportShaders(const ToolParams& tool, matdb::MatDb& matDbCopy, const ShaderDescription& description);

	void RegisterShcompDependencies(const std::string& jobOutput);

private:
	const ToolParams& m_toolParams;
	
	ShaderDescription m_description;

	std::vector<std::pair<std::string, std::string>> m_dependenciesTexturesList; //pair of material - texture
};