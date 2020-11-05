#include "build-transform-matextract-spawner.h"

#include "common/hashes/bighash.h"

#include "icelib/itscene/cgfxshader.h"
#include "icelib/itscene/ndbwriter.h"
#include "icelib/ndb/ndbbasictemplates.h"
#include "icelib/ndb/ndbstdvector.h"
#include "tools/libs/libdb2/db2-actor.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-file-list.h"
#include "tools/pipeline3/build/build-transforms/build-transform-materials.h"
#include "tools/pipeline3/build/build-transforms/build-transform-material-extract.h"
#include "tools/pipeline3/build/tool-params.h"
#include "tools/pipeline3/build/util/shader-description.h"
#include "tools/pipeline3/common/4_shaders.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/material/material_exporter.h"
#include "tools/pipeline3/shcomp4/effect_metadata.h"
#include "tools/pipeline3/shcomp4/material_feature_filter.h"
#include "tools/pipeline3/shcomp4/material_reader.h"
#include "tools/pipeline3/shcomp4/wrapper_codegen_util.h"
#include "tools/pipeline3/toolversion.h"

#include "icelib/itscene/cgfxshader.h"

#include <vector>

//#pragma optimize("", off)

using std::map;
using std::set;
using std::string;
using std::vector;

MatExtractSpawnerTransform::MatExtractSpawnerTransform(const BuildTransformContext *const pContext
													, const std::string& actorName
													, const std::string& subactorBaseName
													, const size_t subactorLod
													, const char *const matBuilderInputPath)
													: BuildTransform("MatExtractSpawner", pContext)
													, m_toolParams(pContext->m_toolParams)
													, m_subactorBaseName(subactorBaseName)
													, m_subactorLod(subactorLod)
													, m_matBuilderInputPath(matBuilderInputPath)
{
	SetDependencyMode(BuildTransform::DependencyMode::kIgnoreDependency);	// must be kIgnoreDependency because it calls AddOutput during execution

	m_preEvaluateDependencies.SetConfigInt("SpawnListVersion", TransformSpawnList::Version());
	m_preEvaluateDependencies.SetConfigString("shaderVersion", SHADERFOLDER);

	m_preEvaluateDependencies.SetBool("isLevel", false);
	m_preEvaluateDependencies.SetString("actorName", actorName);

	//This transform depends on :
	//	- the additional shader features coming from the actor. Added as a pre eval dependency.
	//	- the lightmap override coming from the actor. Added as a pre eval dependency.
	//	- the used bake foreground lighting coming from the actor. Added as a pre eval dependency.

	const libdb2::Actor *  pDbActor = libdb2::GetActor(m_subactorBaseName, m_subactorLod);

	const libdb2::Geometry& geometry = pDbActor->m_geometry;
	m_preEvaluateDependencies.SetString("shaderFeatures", geometry.m_shaderfeature.Xml());
	m_preEvaluateDependencies.SetString("lightmapOverride", geometry.m_lightmapsOverride.Xml());
	m_preEvaluateDependencies.SetBool("useBakedForegroundLighting", geometry.m_useBakedForegroundLighting);
	m_preEvaluateDependencies.SetInt("forcedUvs", geometry.m_forcedUvs);
}

MatExtractSpawnerTransform::MatExtractSpawnerTransform(const BuildTransformContext *const pContext
													, const std::string& levelName
													, const char *const matBuilderInputPath)
													: BuildTransform("MatExtractSpawner", pContext)
													, m_toolParams(pContext->m_toolParams)
													, m_subactorBaseName("")
													, m_subactorLod(-1)
													, m_matBuilderInputPath(matBuilderInputPath)
{
	SetDependencyMode(BuildTransform::DependencyMode::kIgnoreDependency);	// must be kIgnoreDependency because it calls AddOutput during execution

	m_preEvaluateDependencies.SetConfigInt("SpawnListVersion", TransformSpawnList::Version());
	m_preEvaluateDependencies.SetConfigString("shaderVersion", SHADERFOLDER);

	m_preEvaluateDependencies.SetBool("isLevel", true);
	m_preEvaluateDependencies.SetString("levelName", levelName);

	const libdb2::Level *  pDbLevel = libdb2::GetLevel(levelName);
	if (!pDbLevel->Loaded())
	{
		IABORT("Unknown level %s\n", levelName.c_str());
	}
	m_preEvaluateDependencies.SetInt("lightrigCount", pDbLevel->m_lightingSettings.m_lightAtgis.size());
}

BuildTransformStatus MatExtractSpawnerTransform::Evaluate()
{
	const BuildFile& input = GetFirstInputFile();
	NdbStream ndbStream;
	DataStore::ReadData(input, ndbStream);

	vector<ITSCENE::CgfxShader*> shadersList;
	NdbRead(ndbStream, "m_actorShaders", shadersList);

	material_exporter::FeatureFilter featureFilter;
	const bool isLevel = m_preEvaluateDependencies.GetBool("isLevel");
	if (!isLevel)
	{
		const libdb2::Actor *  pDbActor = libdb2::GetActor(m_subactorBaseName, m_subactorLod);

		InitializeFeatureFilter(*pDbActor, false, featureFilter);
	}
	else
	{
		const std::string& levelName = m_preEvaluateDependencies.GetValue("levelName");
		const libdb2::Level *  pDbLevel = libdb2::GetLevel(levelName);
		if (!pDbLevel->Loaded())
		{
			IABORT("Unknown level %s\n", levelName.c_str());
		}

		InitializeFeatureFilter(*pDbLevel, false, featureFilter);
	}

	std::set<std::string> uniqueShaders;
	std::vector<std::string> fileListTransformInput;

	TransformSpawnList spawnList;

	map<string, MaterialExtractTransformSpawnDesc*> transformSpawnDescriptionMap; // This is a map of shaderHash to TransformSpawnDescription

	for (const ITSCENE::CgfxShader* shader : shadersList)
	{
		// shadersList is a list of unique materials (great name right?). Two materials in that list can have the same shader and we don't want to build twice the same shader
		// so use the shader hash to make sure we don't build several times the same shader.
		const BigHash shaderHash = material_exporter::MaterialExporter::GetShaderHash(*shader);
		const std::string strShaderHash = shaderHash.AsString();

		if (shader->m_propList.Find("RemovedRemapSrc") == nullptr)
		{
			INOTE_VERBOSE("non stripped shader %s : %s", strShaderHash.c_str(), shader->GetShaderPath().c_str());
		}

		if (uniqueShaders.find(strShaderHash) != uniqueShaders.cend())
		{
			transformSpawnDescriptionMap[strShaderHash]->m_materials.push_back(shader->GetShaderPath());
			continue;
		}

		uniqueShaders.insert(strShaderHash);
		
		ShaderDescription description;
		CreateShaderDescription(*shader, featureFilter, strShaderHash, *m_toolParams.m_matdb, m_toolParams.m_local, description, isLevel ? 0 : m_preEvaluateDependencies.GetInt("forcedUvs"));

		NdbStream shaderDescriptionStream;
		shaderDescriptionStream.OpenForWriting(Ndb::kBinaryStream);
		NdbWrite(shaderDescriptionStream, "m_shaderDescription", description);
		shaderDescriptionStream.Close();

		const std::string filename = m_pContext->m_toolParams.m_buildPathMatExtractSpawner + strShaderHash + FileIO::separator + "shader.ndb";
		const TransformOutput path(filename, MaterialExtractTransform::INPUT_NICKNAME_SHADERS_LIST);

		DataHash descriptionHash;
		DataStore::WriteData(path.m_path, shaderDescriptionStream, &descriptionHash);
		AddOutput(path);

		INOTE_VERBOSE("shader %s from material %s", strShaderHash.c_str(), shader->GetShaderPath().c_str());
		INOTE_VERBOSE("shader %s : %s", strShaderHash.c_str(), descriptionHash.AsText().c_str());
		
		const std::string compiledNdbPath = description.m_buildPath + description.m_shaderHash + ".ndb";
		const std::string shaderIntermediatesPath = description.m_buildPath + "shaderIntermediates.7z";

		MaterialExtractTransformSpawnDesc *const pDesc = new MaterialExtractTransformSpawnDesc();
		pDesc->m_shaderHash = description.m_shaderHash;
		pDesc->m_materials.push_back(shader->GetShaderPath());
		pDesc->m_inputs.push_back(path);
		pDesc->m_outputs.push_back(TransformOutput(compiledNdbPath, "compiledEffect"));
		pDesc->m_outputs.push_back(TransformOutput(shaderIntermediatesPath, "shaderIntermediates", TransformOutput::kNondeterministic | TransformOutput::kOutputOnFailure));

		transformSpawnDescriptionMap[description.m_shaderHash] = pDesc;

		spawnList.AddTransform(pDesc);

		fileListTransformInput.push_back(compiledNdbPath);
	}

	BuildTransform_FileListSpawnDesc *const pFileListDesc = CreateFileListSpawnDesc(fileListTransformInput, m_matBuilderInputPath);
	spawnList.AddTransform(pFileListDesc);
	
	spawnList.WriteSpawnList(GetOutputPath("SpawnList"));

	//Clean up the loaded CgfxShaders.
	for (const ITSCENE::CgfxShader* shader : shadersList)
		delete shader;

	shadersList.clear();

	return BuildTransformStatus::kOutputsUpdated;
}
