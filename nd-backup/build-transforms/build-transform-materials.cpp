/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/stdafx.h"
#include "build-transform-materials.h"

#include "icelib/common/filepath.h"
#include "icelib/itscene/cgfxshader.h"
#include "tools/libs/bigstreamwriter/ndi-bo-writer.h"
#include "tools/pipeline3/effects2bo/effects2bo.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/scene-util.h"
#include "tools/pipeline3/common/4_stubs.h"
#include "tools/pipeline3/common/4_textures.h"
#include "tools/pipeline3/build/build-transforms/build-transform.h"
#include "tools/pipeline3/common/path-converter.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-spawner.h"
#include "tools/pipeline3/build/build-transforms/build-transform-material-build.h"
#include "tools/pipeline3/build/build-transforms/build-transform-material-expander.h"
#include "tools/pipeline3/build/build-transforms/build-transform-material-extract.h"
#include "tools/pipeline3/build/build-transforms/build-transform-matextract-spawner.h"
#include "tools/pipeline3/shcomp4/effect_metadata.h"
#include "tools/pipeline3/shcomp4/material_feature_filter.h"

using std::vector;
using ITSCENE::CgfxShader;
using pipeline3::SamplerPatch;

//#pragma optimize("", off)

extern void CheckForCtrlC();

BuildModuleMaterialsOutput::BuildModuleMaterialsOutput()
	: m_allshaders()
	, m_allCompiledEffects()
	, m_actorSceneShaderIndexToMergedIndex()
{}

BuildModuleMaterialsOutput::~BuildModuleMaterialsOutput()
{
	for (const ITSCENE::CgfxShader* shader : m_allshaders)
		delete shader;
}

//--------------------------------------------------------------------------------------------------------------------//
class MaterialConsolidateTransform : public BuildTransform
{
	std::vector<std::string>  m_actorWithMaterials;	
	BuildModuleMaterialsOutput m_output;
	const ToolParams& m_tools;

public:
	
	static const char* OUTPUT_NICKNAME_MATERIAL_BO;

	MaterialConsolidateTransform(const ToolParams& tools)
		: BuildTransform("MatConsolidate")
		, m_output()
		, m_tools(tools)
	{}

	virtual BuildTransformStatus Evaluate() override
	{
		struct CompareShader
		{
			bool operator ()(const ITSCENE::CgfxShader*pa, const ITSCENE::CgfxShader*pb) const
			{
				return ShaderCompare_Less(pa, pb);
			}
		};

		const vector<TransformInput>& inputs = GetInputs();

		vector<BuildFile> materialInputList;
		vector<BuildFile> samplerPatchInputList;
		for (int ii = 0; ii < inputs.size(); ii = ii + 2)
		{
			materialInputList.push_back(inputs[ii].m_file);
			samplerPatchInputList.push_back(inputs[ii + 1].m_file);
		}

		//Load cgfx shaders and compiled effect and create the map of shader per actor to the global shader list
		std::map<const ITSCENE::CgfxShader*, unsigned int, CompareShader > merged;
		size_t iActor = 0;
		vector<DataHash> intputHashes(materialInputList.size());
		const int matInputSize = materialInputList.size();
		
		for (int ii = 0; ii < matInputSize; ++ii)
		{
			//read the materials and compiled effects
			const BuildFile& buildFile = materialInputList[ii];
			intputHashes[iActor] = buildFile.GetContentHash();

			vector<ITSCENE::CgfxShader*> actorShaders;
			vector<int> actorShadersToEffects;
			vector<material_compiler::CompiledEffect> actorCompiledEffects;

			//keep scope here so the NdbStream is deleted
			{
				NdbStream stream;
				DataStore::ReadData(buildFile, stream);
				NdbRead(stream, "m_actorShaders", actorShaders);
				NdbRead(stream, "m_actorShadersToEffects", actorShadersToEffects);
				NdbRead(stream, "m_actorCompiledEffects", actorCompiledEffects);
			}

			//read the sampler patch
			vector<vector<SamplerPatch>> samplerPatchList;
			//keep scope here so the NdbStream is deleted
			{
				const BuildFile& samplerPatchInput = samplerPatchInputList[ii];
				NdbStream samplerPatchListStream;
				DataStore::ReadData(samplerPatchInput, samplerPatchListStream);
				NdbRead(samplerPatchListStream, "samplerPatchList", samplerPatchList);
			}

			//merge materials
			vector<unsigned int>& translateActorShaders = m_output.m_actorSceneShaderIndexToMergedIndex[m_actorWithMaterials[iActor]];
			translateActorShaders.resize(actorShaders.size());
			for (size_t iShader = 0; iShader != actorShaders.size(); ++iShader)
			{
				const ITSCENE::CgfxShader* pShader = actorShaders[iShader];
				if (merged.find(pShader) == merged.end())
				{
					unsigned int index = static_cast<unsigned int>(merged.size());
					merged[pShader] = index;
					m_output.m_allshaders.push_back(new CgfxShader(*pShader));
					m_output.m_allCompiledEffects.push_back(actorCompiledEffects[actorShadersToEffects[iShader]]);

					//patch compiled effect
					pipeline3::PatchSamplersInCompiledEffect(samplerPatchList[iShader], m_output.m_allCompiledEffects[index]);
				}
				translateActorShaders[iShader] = merged[pShader];
			}
			iActor++;
		}

		//check all samplers were patched
		if (!pipeline3::AreAllSamplersPatched(m_output.m_allCompiledEffects))
			return BuildTransformStatus::kFailed;

		//write outputs
		bool success = WriteMatTable();

		NdbStream stream;
		stream.OpenForWriting(Ndb::kBinaryStream);
		NdbWrite(stream, "materialConsolidateOutput", m_output);
		stream.Close();

		const BuildPath& outputPath = GetOutputPath("consolidatedMaterials");
		DataStore::WriteData(outputPath, stream);
		
		return success ? BuildTransformStatus::kOutputsUpdated : BuildTransformStatus::kFailed;
	}

	void SetActorNames(const std::vector<std::string>&  actorWithShaders)
	{
		m_actorWithMaterials = actorWithShaders;
	}

private:

	bool WriteMatTable()
	{
		std::vector< pipeline3::effect2bo::TechniqueUsageSatus > techsUsages;// we're not stripping techniques on actor. pass an empty usage.
		std::vector< pipeline3::effect2bo::EffectAndUsagePair> effectsAndUsages;
		std::set<std::string> uniqueEffects;
		for (const material_compiler::CompiledEffect& compiledEffect : m_output.m_allCompiledEffects)
		{
			const std::string& effectPath = compiledEffect.m_effect.m_cacheFilename;
			if (uniqueEffects.find(effectPath) == uniqueEffects.end())
			{
				uniqueEffects.insert(effectPath);
				effectsAndUsages.push_back(pipeline3::effect2bo::EffectAndUsagePair(&compiledEffect, &techsUsages));
			}
		}

		BigStreamWriter effectStream(m_tools.m_streamConfig);
		if (effectsAndUsages.size())  // do not write an actual table item if it is empty. (we have a problem with collisions reusing the geometry file (with no geometry/slash materials) and the dependency *requires* a bo file from us
		{
			WriteFakeMaterialTable(effectStream);
			pipeline3::effect2bo::WriteEffectTable(effectStream, effectsAndUsages);
		}

		NdiBoWriter boWriter(effectStream);
		if (!boWriter.Write())
		{
			IERR("Failed to format the bo file.");
			return false;
		}

		const BuildPath& boMaterialPath = GetOutputPath(OUTPUT_NICKNAME_MATERIAL_BO);

		DataStore::WriteData(boMaterialPath, boWriter.GetMemoryStream());

		return true;
	}
};

const char* MaterialConsolidateTransform::OUTPUT_NICKNAME_MATERIAL_BO = "material_bo";

//--------------------------------------------------------------------------------------------------------------------//
void BuildModuleMaterials_Configure(const BuildTransformContext *const pContext, 
									const libdb2::Actor *const pDbActor, 
									vector<string>& arrBoFiles, 
									string& consolidatedMaterials, 
									const vector<const libdb2::Actor*>& actorList)
{
	// Validate the actors
	bool abort = false;
	for (size_t iactor = 0; iactor<actorList.size(); ++ iactor)
	{
		const libdb2::Actor* pActor = actorList[iactor];

		// Geometry is only allowed to be exported from a set.
		if (!pActor->m_geometry.m_sceneFile.empty() && pActor->m_geometry.m_set.empty())
		{
			IERR("Geometry in actor '%s' is exported without using a set. Update the Maya scene file and add the set name in Builder.\n", pActor->Name().c_str());
			abort = true;
			continue;
		}

		// All geometry lods have to use the same scene file
		if (pActor->m_geometryLods.size())
		{
			bool allSame = true;
			std::string usedSceneFile = pActor->m_geometryLods[0].m_sceneFile;
			for (int lodIndex = 1; lodIndex < pActor->m_geometryLods.size(); ++lodIndex)
			{
				const libdb2::ListedGeometry& geo = pActor->m_geometryLods[lodIndex];
				if (geo.m_sceneFile != usedSceneFile)
				{
					allSame = false;
					break;
				}
			}

			if (!allSame)
			{
				IERR("Geometry LODs in actor '%s' reference different Maya scene files. This is no longer allowed\n", pActor->Name().c_str());
				abort = true;
				continue;
			}
		}
	}
	if (abort)
		IABORT("Unable to recover from previous error(s)\n");


	std::vector< TransformInput > consolidateInputFiles;
	std::vector< std::string > actorWithShaders;

	for (size_t actorLoop = 0; actorLoop < actorList.size(); actorLoop++)
	{
		CheckForCtrlC();
		const libdb2::Actor *const dbactor = actorList[actorLoop];
		if (dbactor->m_geometry.m_sceneFile.size() || dbactor->m_extraMaterialsList.size())
		{
			MaterialExpanderTransform *const materialExpander = new MaterialExpanderTransform(pContext, 
																							dbactor->BaseName(), 
																							dbactor->m_lodIndex);
			if (dbactor->m_geometry.m_sceneFile.size())
			{
				std::string inputFilename = pContext->m_toolParams.m_buildPathGeo + toolsutils::MakeGeoNdbFilename(dbactor->m_geometry.m_sceneFile, dbactor->m_geometry.m_set);
				TransformInput inputFile(inputFilename, "GeoNdb");
				materialExpander->SetInput(inputFile);
			}

			//This should be factorize, see build-transform-geometry.cpp line 347
			std::string expanderActorPath = pContext->m_toolParams.m_buildPathMatExpander + dbactor->Name() + FileIO::separator;
			ITFILE::FilePath::CleanPath(expanderActorPath);

			std::string expanderOutputPath = expanderActorPath + dbactor->m_geometry.m_sceneFile + ".lod_" + std::to_string(dbactor->m_lodIndex) + FileIO::separator;
			ITFILE::FilePath::CleanPath(expanderOutputPath);

			const std::string expanderOutputFilename = expanderOutputPath + "shaderslist.ndb";
			const std::string matFlagsOutputFilename = expanderOutputPath + "matflags.ndb";

			const TransformOutput expanderOutput(expanderOutputFilename, MaterialExtractTransform::INPUT_NICKNAME_SHADERS_LIST);
			const TransformOutput matFlagsOutput(matFlagsOutputFilename, "matflags");

			materialExpander->AddOutput(expanderOutput);
			materialExpander->AddOutput(matFlagsOutput);
			pContext->m_buildScheduler.AddBuildTransform(materialExpander, pContext);

			std::string matBuildInput = pContext->m_toolParams.m_buildPathFileList + dbactor->Name() + FileIO::separator + "shaderblobslist.a";
			MatExtractSpawnerTransform *const matExtractSpawner = new MatExtractSpawnerTransform(pContext, 
																								pDbActor->Name(), 
																								dbactor->BaseName(), 
																								dbactor->m_lodIndex, 
																								matBuildInput.c_str());
			matExtractSpawner->SetInput(expanderOutput);

			//TransformOutput extractOutputList(pContext->m_toolParams.m_buildPathMatExtractSpawner + dbactor->Name() + FileIO::separator + "outputlist.txt", "OutputList");
			TransformOutput extractSpawnList(pContext->m_toolParams.m_buildPathMatExtractSpawner + dbactor->Name() + FileIO::separator + "spawnlist.ndb", "SpawnList");
			//matExtractSpawner->AddOutput(extractOutputList);
			matExtractSpawner->AddOutput(extractSpawnList);
			pContext->m_buildScheduler.AddBuildTransform(matExtractSpawner, pContext);

			BuildTransformSpawner *pXformSpawner_MatExtract = new BuildTransformSpawner(*pContext);
			pXformSpawner_MatExtract->SetInput(TransformInput(extractSpawnList));
			pXformSpawner_MatExtract->SetOutput(BuildTransformSpawner::CreateOutputPath(pXformSpawner_MatExtract->GetInputs()[0]));
			pContext->m_buildScheduler.AddBuildTransform(pXformSpawner_MatExtract, pContext);

			MaterialBuildTransform *const matBuildTransform = new MaterialBuildTransform(dbactor->Name(), 
																						dbactor->m_geometry.m_sceneFile, 
																						pContext);
			string  matBuildOutputFilename = pContext->m_toolParams.m_buildPathMatBuild + dbactor->FullName() + FileIO::separator + "materials.ndb";
			TransformOutput matBuildOutputPath(matBuildOutputFilename, "MaterialsNdb");

			vector<TransformInput> matBuildInputs;
			matBuildInputs.push_back(TransformInput(matBuildInput, "shaderlib"));
			matBuildInputs.push_back(TransformInput(expanderOutput));
			matBuildTransform->SetInputs(matBuildInputs);
			
			matBuildTransform->SetOutput(matBuildOutputPath);
			pContext->m_buildScheduler.AddBuildTransform(matBuildTransform, pContext);

			consolidateInputFiles.push_back(TransformInput(matBuildOutputPath));

			const string samplerPatcheList = pContext->m_toolParams.m_buildPathCollectTexturesFromMaterials + dbactor->FullName() + FileIO::separator + "samplerpatchlist.ndb";
			consolidateInputFiles.push_back(TransformInput(samplerPatcheList));

			actorWithShaders.push_back(dbactor->Name());
		}
	}

	if (consolidateInputFiles.size())
	{
		MaterialConsolidateTransform* pConsolidateTransform = new MaterialConsolidateTransform(pContext->m_toolParams);

		pConsolidateTransform->SetInputs(consolidateInputFiles);

		std::string matConsolidateFolder = pContext->m_toolParams.m_buildPathMatConsolidateBo + pDbActor->FullName() + FileIO::separator;

		std::string boMaterialFilename = matConsolidateFolder + ".material.bo";
		std::string ndbMaterialFilename = matConsolidateFolder + "consolidatedMaterials.ndb";
		std::vector< TransformOutput > outputFiles;

		outputFiles.emplace_back(boMaterialFilename, MaterialConsolidateTransform::OUTPUT_NICKNAME_MATERIAL_BO);
		outputFiles.emplace_back(ndbMaterialFilename, "consolidatedMaterials");

		pConsolidateTransform->SetOutputs(outputFiles);
		pConsolidateTransform->SetActorNames(actorWithShaders);
		pContext->m_buildScheduler.AddBuildTransform(pConsolidateTransform, pContext);

		arrBoFiles.push_back(boMaterialFilename);
		consolidatedMaterials = ndbMaterialFilename;
	}


}

bool InitializeFeatureFilter(const libdb2::Actor& dbActor, bool enableVertexCompression, material_exporter::FeatureFilter& filters)
{
	if (enableVertexCompression)
	{
		filters.AddFeatureFilter(std::make_pair("ND_ENABLE_VERTEX_COMPRESSION", material_exporter::FeatureFilter::kFilterAdd));
	}

	if (dbActor.Loaded() && dbActor.m_geometry.Loaded() && dbActor.m_geometry.m_lightmapsOverride.m_enabled && dbActor.m_geometry.m_lightmapsOverride.m_sources.size() == 2)
	{
		filters.AddFeatureFilter(std::make_pair("ENABLE_FG_LIGHTMAPS", material_exporter::FeatureFilter::kFilterAdd));
		filters.AddFeatureFilter(std::make_pair("ENABLE_DUAL_LIGHTMAP", material_exporter::FeatureFilter::kFilterAdd));
	}

	if (dbActor.Loaded() && dbActor.m_geometry.Loaded() && dbActor.m_geometry.m_useBakedForegroundLighting)
	{
		filters.AddFeatureFilter(std::make_pair("ND_FG_VERTEX_LIGHTING", material_exporter::FeatureFilter::kFilterAdd));// enables foreground vertex lighting (if both the actor and the shader require it)
	}

	const libdb2::GeometryTag_ShaderFeatureList &shaderFeatrueList = dbActor.m_geometry.m_shaderfeature;
	for (libdb2::GeometryTag_ShaderFeatureList::const_iterator it = shaderFeatrueList.begin(), itEnd = shaderFeatrueList.end(); it != itEnd; ++it)
	{
		const libdb2::GeometryTag_ShaderFeature &shaderFeature = (*it);
		filters.AddFeatureFilter(std::make_pair(shaderFeature.m_value, material_exporter::FeatureFilter::kFilterAdd));
	}

	return true;
}


static void SetupMaterialFeatureFilters(const libdb2::Level& dblevel2, material_exporter::FeatureFilter& filters)
{
	if (dblevel2.m_levelInfo.m_isIndoor)
	{
		INOTE_VERBOSE("Level is INDOOR, disabling sunlight from shaders");
		filters.AddFeatureFilter(std::make_pair("ND_SUNLIGHT_DISABLE", material_exporter::FeatureFilter::kFilterAdd));
		filters.AddFeatureFilter(std::make_pair("DISABLE_SUNLIGHT", material_exporter::FeatureFilter::kFilterAdd));
		filters.AddFeatureFilter(std::make_pair("DISABLE_RUNTIME_LIGHTS", material_exporter::FeatureFilter::kFilterAdd));
	}

	if (dblevel2.m_levelInfo.m_disableSunShadowFadeout)
		filters.AddFeatureFilter(std::make_pair("GLOBAL_SUNLIGHT_SHADOW_FADEOUT", material_exporter::FeatureFilter::kFilterRemove));

	if (dblevel2.m_levelInfo.m_alternateSunShadowFadeout)	// enable the alternate sun shadow fadeout on all the shaders of the level ( min(baked, lerp(realtime, 1, fadeDistance)))
	{
		filters.AddFeatureFilter(std::make_pair("GLOBAL_SUNLIGHT_SHADOW_FADEOUT", material_exporter::FeatureFilter::kFilterRemove));
		filters.AddFeatureFilter(std::make_pair("GLOBAL_SUNLIGHT_SHADOW_FADEOUT_ALT", material_exporter::FeatureFilter::kFilterAdd));
	}

	if (dblevel2.m_lightingSettings.m_designerLightingMode)	// enable the designer lighting mode in the shader (looks decent without any real lighting)
	{
		filters.AddFeatureFilter(std::make_pair("USE_DESIGNER_LIGHTING", material_exporter::FeatureFilter::kFilterAdd));
	}

	//if (dblevel2.m_lightingSettings.m_lightmapsOverride.m_enabled && dblevel2.m_lightingSettings.m_lightmapsOverride.m_sources.size() == 2)
	if (dblevel2.m_lightingSettings.m_lightAtgis.size() >= 2)
	{
		filters.AddFeatureFilter(std::make_pair("ENABLE_DUAL_LIGHTMAP", material_exporter::FeatureFilter::kFilterAdd));
	}

	const std::string action_enable = "enable";
	const std::string action_disable = "disable";
	for (size_t iFeature = 0; iFeature != dblevel2.m_shaderFeatures.size(); ++iFeature)
	{
		const std::string& featureName = dblevel2.m_shaderFeatures[iFeature].m_name;
		const std::string& action = dblevel2.m_shaderFeatures[iFeature].m_action;
		if (action == action_enable)
		{
			filters.AddFeatureFilter(std::make_pair(featureName, material_exporter::FeatureFilter::kFilterAdd));
		}
		else if (action == action_disable)
		{
			filters.AddFeatureFilter(std::make_pair(featureName, material_exporter::FeatureFilter::kFilterRemove));
		}
	}
}

bool InitializeFeatureFilter(const libdb2::Level &dbLevel, bool enableVertexCompression, material_exporter::FeatureFilter &filters)
{
	if (enableVertexCompression)
	{
		filters.AddFeatureFilter(std::make_pair("ND_ENABLE_VERTEX_COMPRESSION", material_exporter::FeatureFilter::kFilterAdd));
	}

	SetupMaterialFeatureFilters(dbLevel, filters);
	filters.AddFeatureFilter(std::make_pair("NDFILTEROUT_Fg", material_exporter::FeatureFilter::kFilterAdd));

	return true;
}

void NdbWrite(NdbStream& stream, const char* pSymName, const BuildModuleMaterialsOutput& x)
{
	NdbBegin(stream, pSymName, "BuildModuleMaterialsOutput");

	NdbWrite(stream, "allShaders", x.m_allshaders);
	NdbWrite(stream, "allCompiledEffects", x.m_allCompiledEffects);
	NdbWrite(stream, "actorSceneShaderIndexToMergedIndex", x.m_actorSceneShaderIndexToMergedIndex);
}

void NdbRead(NdbStream& stream, const char* pSymName, BuildModuleMaterialsOutput& x)
{
	NdbBegin(stream, pSymName, "BuildModuleMaterialsOutput");

	NdbRead(stream, "allShaders", x.m_allshaders);
	NdbRead(stream, "allCompiledEffects", x.m_allCompiledEffects);
	NdbRead(stream, "actorSceneShaderIndexToMergedIndex", x.m_actorSceneShaderIndexToMergedIndex);
}
