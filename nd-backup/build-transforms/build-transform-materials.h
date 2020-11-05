/*
 * Copyright (c) 2016 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

namespace libdb2
{
	class Actor;
	class Level;
}

namespace matdb
{
	class MatDb;
}

namespace material_compiler
{
	struct CompiledEffect;
}

namespace material_exporter
{
	class FeatureFilter;
}

class BuildTransformContext;
class MaterialExtractTransform;
class MaterialConsolidateTransform;
class ToolParams;

struct BuildModuleMaterialsOutput
{
	std::vector<ITSCENE::CgfxShader*> m_allshaders;
	std::vector< material_compiler::CompiledEffect > m_allCompiledEffects;
	std::map<std::string, std::vector<unsigned int>> m_actorSceneShaderIndexToMergedIndex;

	BuildModuleMaterialsOutput();
	~BuildModuleMaterialsOutput();
};

void BuildModuleMaterials_Configure(const BuildTransformContext *const pContext, 
									const libdb2::Actor *const pDbActor, 
									std::vector<std::string>& arrBoFiles, 
									std::string& consolidatedMaterials, 
									const std::vector<const libdb2::Actor*>& actorList);

bool InitializeFeatureFilter(const libdb2::Actor& dbActor, bool enableVertexCompression, material_exporter::FeatureFilter& filters);
bool InitializeFeatureFilter(const libdb2::Level& dbLevel, bool enableVertexCompression, material_exporter::FeatureFilter& filters);

void NdbWrite(NdbStream& stream, const char* pSymName, const BuildModuleMaterialsOutput& x);

void NdbRead(NdbStream& stream, const char* pSymName, BuildModuleMaterialsOutput& x);
