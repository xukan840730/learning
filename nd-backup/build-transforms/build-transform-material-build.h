#pragma once

#include "build-transform.h"

#include <string>
#include <utility>
#include <vector>

namespace libdb2
{
	class Actor;
}

namespace ITSCENE
{
	class CgfxShader;
}

namespace matdb
{
	class MatDb;
	struct ShaderInfo;
}

namespace material_compiler
{
	struct CompiledEffect;
}

class BuildTransformContext;

class MaterialBuildTransform : public BuildTransform
{
public:
	MaterialBuildTransform(const std::string& actorName,
						const std::string& geoSceneFile,
						const BuildTransformContext *const pContext);
	MaterialBuildTransform(const BuildTransformContext *const context);
	~MaterialBuildTransform();

	BuildTransformStatus Evaluate() override;

private:

	void LoadShaders(std::vector<ITSCENE::CgfxShader*>& shaders) const;

	void LoadCompiledEffects(const std::vector<const ITSCENE::CgfxShader*>& shaders, 
							 const std::vector<BuildFile>& inputs, 
							 std::vector<int> &effectIndices, 
							 std::vector<material_compiler::CompiledEffect>& effects) const;

	void ReadCompiledEffect(const std::string& compiledEffectFilename, const std::vector<BuildFile>& extractedInputs, material_compiler::CompiledEffect& compiledMaterial) const;

	bool FindInLibraryInput(const std::vector<BuildFile>& inputs, const BuildPath& pathToFind, BuildFile& outFile) const;

	void WriteOutput(const std::vector<ITSCENE::CgfxShader*>& shaders, 
					 const std::vector<int> &compiledEFfectIndices, 
					 const std::vector<material_compiler::CompiledEffect>& compiledEffects) const;

	void RecursivelyExtractBuildFileList(const BuildFile& input, std::vector<BuildFile>& extractedBuildFiles) const;

	void MaterialDependenciesCollectorCallback(const std::string& resource, const std::string& referencedFrom, const std::string& type, int level);
};