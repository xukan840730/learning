#include "build-transform-material-build.h"

#include "build-transform-materials.h"

#include "common/hashes/bighash.h"

#include "icelib/itscene/ndbwriter.h"
#include "icelib/ndb/ndbbasictemplates.h"
#include "icelib/ndb/ndbstdvector.h"
#include "icelib/itscene/cgfxshader.h"
#include "icelib/itscene/scenedb.h"

#include "tools/libs/libdb2/db2-actor.h"
#include "tools/libs/toolsutil/constify.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/util/dependency-database-manager.h"
#include "tools/pipeline3/build/util/material-dependencies.h"
#include "tools/pipeline3/common/4_shaders.h"
#include "tools/pipeline3/common/4_shader_parameters.h"
#include "tools/pipeline3/common/blobs/blob-cache.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/material/material_exporter.h"
#include "tools/pipeline3/shcomp4/effect_metadata.h"
#include "tools/pipeline3/shcomp4/material_feature_filter.h"
#include "tools/pipeline3/toolversion.h"

#include <vector>

//#pragma optimize("", off)

using std::pair;
using std::string;
using std::vector;

using namespace std::placeholders;

MaterialBuildTransform::MaterialBuildTransform(const std::string& actorName, 
											const std::string& geoSceneFile, 
											const BuildTransformContext *const pContext)
											: BuildTransform("MatBuild", pContext)
{
	m_preEvaluateDependencies.SetBool("isActor", true);
	m_preEvaluateDependencies.SetString("actorName", actorName);
	m_preEvaluateDependencies.SetString("geoSceneFile", geoSceneFile);
}

MaterialBuildTransform::MaterialBuildTransform(const BuildTransformContext *const pContext)
											: BuildTransform("MatBuild", pContext)
{
	m_preEvaluateDependencies.SetBool("isActor", false);
}

MaterialBuildTransform::~MaterialBuildTransform()
{
}

BuildTransformStatus MaterialBuildTransform::Evaluate()
{
	const BuildFile& input = GetInputFile("shaderlib");
	vector<BuildFile> extractedInputs;
	RecursivelyExtractBuildFileList(input, extractedInputs);

	vector<ITSCENE::CgfxShader*> allShaders;
	LoadShaders(allShaders);
	
	vector<int> allShadersToEffects;
	vector<material_compiler::CompiledEffect> compiledEffects;
	LoadCompiledEffects(toolsutils::constifyData(allShaders), extractedInputs, allShadersToEffects, compiledEffects);

	MaterialDependenciesCollector collector;
	for (const ITSCENE::CgfxShader* shader : allShaders)
		collector.Run(*shader, *m_pContext->m_toolParams.m_matdb, std::bind(&MaterialBuildTransform::MaterialDependenciesCollectorCallback, this, _1, _2, _3, _4));

	const bool isActor = m_preEvaluateDependencies.GetBool("isActor");
	if (isActor)
	{
		const std::string& geoSceneFile = m_preEvaluateDependencies.GetValue("geoSceneFile");

		std::map<std::string, std::vector<std::string>> shadersByMayaFile;
		auto& shadersForActorMayaFile = shadersByMayaFile[geoSceneFile];
		for (const ITSCENE::CgfxShader* pShader : allShaders)
		{
			shadersForActorMayaFile.push_back(pShader ? pShader->GetShaderPathAndTextureSet() : std::string());
		}
	}

	WriteOutput(allShaders, allShadersToEffects, compiledEffects);

	return BuildTransformStatus::kOutputsUpdated;
}

void MaterialBuildTransform::LoadShaders(vector<ITSCENE::CgfxShader*>& shaders) const
{
	const BuildFile& shadersList = GetInputFile("shaderlist");
	NdbStream stream;
	DataStore::ReadData(shadersList, stream);

	NdbRead(stream, "m_actorShaders", shaders);
}

void MaterialBuildTransform::LoadCompiledEffects(const vector<const ITSCENE::CgfxShader*>& shaders, 
												 const vector<BuildFile>& inputs, 
												 std::vector<int> &effectIndices,
												 std::vector<material_compiler::CompiledEffect>& effects) const
{
	const ToolParams& param = m_pContext->m_toolParams;

	std::vector<std::string> buildPaths;
	material_exporter::MaterialExporter::GetShaderBuildPaths(buildPaths, shaders, param.m_local);

	for(int ii = 0; ii < shaders.size(); ++ii)
	{
		BigHash hash = material_exporter::MaterialExporter::GetShaderHash(*shaders[ii]);
		buildPaths[ii] = buildPaths[ii] + hash.AsString() + ".ndb";
	}

	effectIndices.clear();
	effectIndices.resize(buildPaths.size());

	std::vector<std::string> uniqueBuildPaths;
	std::map<std::string, int> uniqueBuildPathsMap;
	for (int i = 0; i < buildPaths.size(); i++)
	{
		auto iter = uniqueBuildPathsMap.find(buildPaths[i]);
		if (iter != uniqueBuildPathsMap.end())
		{
			effectIndices[i] = iter->second;
		}
		else
		{
			const int uniqueId = uniqueBuildPaths.size();
			
			effectIndices[i] = uniqueId;
			uniqueBuildPathsMap[buildPaths[i]] = uniqueId;

			uniqueBuildPaths.push_back(buildPaths[i]);
		}
	}

	effects.clear();
	effects.resize(uniqueBuildPaths.size());

	for (size_t iShader = 0; iShader < uniqueBuildPaths.size(); ++iShader)
		ReadCompiledEffect(uniqueBuildPaths[iShader].c_str(), inputs, effects[iShader]);
}

void MaterialBuildTransform::ReadCompiledEffect(const string& compiledEffectFilename, const vector<BuildFile>& extractedInputs, material_compiler::CompiledEffect& compiledMaterial) const
{
	BuildPath compiledEffectPath(compiledEffectFilename);
	BuildFile compiledEffectFile;
	if (!FindInLibraryInput(extractedInputs, compiledEffectPath, compiledEffectFile))
	{
		IABORT("Can't find the file %s in inputs", compiledEffectPath.AsPrefixedPath().c_str());
	}

	NdbStream stream;
	DataStore::ReadData(compiledEffectFile, stream);
	NdbRead(stream, "compiledEffect", compiledMaterial);
}

bool MaterialBuildTransform::FindInLibraryInput(const std::vector<BuildFile>& inputs, const BuildPath& pathToFind, BuildFile& outFile) const
{
	vector<BuildFile>::const_iterator it = std::find_if(inputs.cbegin(), inputs.cend(), [&pathToFind](const BuildFile& file) { return file.GetBuildPath() == pathToFind; });
	if (it == inputs.cend())
		return false;

	outFile = *it;
	return true;
}

void MaterialBuildTransform::WriteOutput(const std::vector<ITSCENE::CgfxShader*>& shaders, 
										 const std::vector<int> &shadersToEffects,
										 const std::vector<material_compiler::CompiledEffect>& compiledEffects) const
{
	NdbStream ndbostream;
	ndbostream.OpenForWriting(Ndb::kBinaryStream);
	NdbWrite(ndbostream, "m_actorShaders", shaders);
	NdbWrite(ndbostream, "m_actorShadersToEffects", shadersToEffects);
	NdbWrite(ndbostream, "m_actorCompiledEffects", compiledEffects);
	ndbostream.Close();

	DataHash ndbostreamHash;
	const BuildPath& ndboPath = GetOutputPath("MaterialsNdb");
	DataStore::WriteData(ndboPath, ndbostream, &ndbostreamHash);
}

void MaterialBuildTransform::RecursivelyExtractBuildFileList(const BuildFile& input, vector<BuildFile>& extractedBuildFiles) const
{
	vector<BuildFile> extractedInputs;
	ExtractBuildFileList(input, extractedInputs);

	const char* EXTENSION = ".a";
	const int EXTENSION_LENGTH = strlen(EXTENSION);

	for (const BuildFile& file : extractedInputs)
	{
		string prefixedPath = file.GetBuildPath().AsPrefixedPath();
		if (prefixedPath.compare(prefixedPath.size() - EXTENSION_LENGTH, EXTENSION_LENGTH, EXTENSION) == 0)
			RecursivelyExtractBuildFileList(file, extractedBuildFiles);
		else
			extractedBuildFiles.push_back(file);		
	}
}

void MaterialBuildTransform::MaterialDependenciesCollectorCallback(const std::string& resource, const std::string& referencedFrom, const std::string& type, int level)
{
	m_assetDeps.AddAssetDependency(resource.c_str(), referencedFrom.c_str(), type.c_str());
}