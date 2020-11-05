/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/stdafx.h"

#include "tools/pipeline3/build/build-transforms/build-transform-geometry-bo.h"

#include "tools/libs/bigstreamwriter/ndi-bo-writer.h"
#include "tools/libs/toolsutil/constify.h"
#include "tools/pipeline3/common/4_blendshape.h"
#include "tools/pipeline3/common/4_gameflags.h"
#include "tools/pipeline3/common/4_textures.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/libs/libdb2/db2-actor.h"
#include "tools/pipeline3/build/actor/4_geo3bo.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-materials.h"
#include "tools/pipeline3/build/build-transforms/build-transform-textures.h"
#include "tools/pipeline3/build/util/loaded-texture-map.h"

#include "3rdparty/rapidjson/include/rapidjson/document.h"

//#pragma optimize("", off)


namespace pipeline3
{
	namespace jsontobo
	{
		extern void JsonToBoActor(const BuildPath& jsonPath, const rapidjson::Document& jsonDocument, BigStreamWriter& stream, BigStreamWriter& testStream, bool forceWriteGeoSection);
	}
};


static MaterialConstBufferArrayMap ComputeMaterialScopeConstBufferMap(const ExtractedEffectMap &effectMap)
{
	StringId64 scopeSid = StringToStringId64("material");

	// Filter out everything but material scope constant buffers

	MaterialConstBufferArrayMap results;

	for (auto const &elem : effectMap)
	{
		std::vector<MaterialConstBufferInfo> result;
		for (auto const &buffer : elem.second.m_constBuffers)
		{
			if (buffer.m_scope == scopeSid)
				result.push_back(buffer);
		}
		if (result.size() > 0)
			results[elem.first] = result;
	}

	return results;
}

static void FormatToJson(
		toolsutils::JsonStringWriter& writer,
		const std::vector<BuildPipeline::RigInfo>& actorsRigInfos,
		const std::vector<std::unique_ptr<const BuildPipeline::SceneGeoDb> >& actorsGeoDBs,
		const std::vector<GameFlags>& actorGameFlags,
		const std::vector<std::vector<U32>>& sceneShaderIndexToMergedIndex,
		const std::vector<SceneBlendShapeData>& actorBlendShapeData,
		const ExtractedEffectData& extractedEffects,
		const std::vector<std::pair<LoadedTextureId, TextureLoader::TextureGenerationSpec*> >& texturesHashesAndTgds,
		const std::vector<const ITSCENE::CgfxShader *> &allShaders,
		const std::vector<const material_compiler::CompiledEffect*> &allCompiledEffectsPointers,
		const std::vector<const libdb2::Actor*>& actorList)
	{
		std::vector<const material_compiler::CachedEffect*> allCachedEffectsPointers;
		for (auto compiled : allCompiledEffectsPointers)
			allCachedEffectsPointers.push_back(&compiled->m_effect);

		writer.BeginObject();
		writer.BeginObject("Actor");
		ba4::WriteAllGeos(
			writer,
			actorsGeoDBs,
			actorBlendShapeData,
			actorsRigInfos,
			allCachedEffectsPointers,
			sceneShaderIndexToMergedIndex,
			extractedEffects.m_extractedEffects,
			actorGameFlags,
			allShaders, 
			actorList);
		pipeline3::WriteTextures(writer, texturesHashesAndTgds);
		ba4::WriteAllShaders(writer, allShaders, allCompiledEffectsPointers, extractedEffects);
		writer.EndObject();
		writer.EndObject();
}


class BuildTransformGeometryBo : public BuildTransform
{
	const std::vector<const libdb2::Actor*> m_actorList;
public:
	BuildTransformGeometryBo(const BuildTransformContext *const pContext
							, const std::string& actorName
							, const std::vector<const libdb2::Actor*>& actorList)
							: BuildTransform("GeometryBo", pContext),
							m_actorList(actorList)
	{
		m_preEvaluateDependencies.SetString("actorName", actorName);
		m_preEvaluateDependencies.SetBool("enableVertexCompression", pContext->m_toolParams.m_enableVertexCompression);
	}

protected:
	bool JsonToBo(const rapidjson::Document& jsonDocument, 
				const libdb2::Actor *const pDbActor, 
				bool forceWriteGeoSection)
	{
		const ToolParams& tool = m_pContext->m_toolParams;

		INOTE_VERBOSE("%s - geo: %s", pDbActor->Name().c_str(), pDbActor->m_geometry.m_sceneFile.c_str());
		const libdb2::Geometry &geo = pDbActor->m_geometry;

		BigStreamWriter stream(tool.m_streamConfig);
		BigStreamWriter dummyStream(tool.m_streamConfig);
		const BuildPath& jsonBuildPath = GetOutputPath("geometry_json");
		pipeline3::jsontobo::JsonToBoActor(jsonBuildPath, jsonDocument, stream, dummyStream, forceWriteGeoSection);

		NdiBoWriter writer(stream);
		writer.Write();

		const BuildPath& boPath = GetOutputPath("geometry_bo");
		DataStore::WriteData(boPath, writer.GetMemoryStream());

		return true;
	}

	virtual BuildTransformStatus Evaluate() override
	{
		const BuildFile& textureSpecHashMapPath = GetInputFile("textureSpecHashMap");
		NdbStream textureSpecHashMapStream;
		DataStore::ReadData(textureSpecHashMapPath, textureSpecHashMapStream);

		LoadedTextureMap texturesHashesAndTgds;
		NdbRead(textureSpecHashMapStream, "hashmap", texturesHashesAndTgds);
		textureSpecHashMapStream.Close();

		std::unique_ptr<BuildModuleMaterialsOutput> pMaterialModuleOutput = std::make_unique<BuildModuleMaterialsOutput>();
		if (DoesInputExist("consolidatedMaterials"))
		{
			const BuildFile& materialFile = GetInputFile("consolidatedMaterials");
			NdbStream materialStream;
			DataStore::ReadData(materialFile, materialStream);

			NdbRead(materialStream, "materialConsolidateOutput", *pMaterialModuleOutput);
			materialStream.Close();
		}
		auto& materialModuleOutput = *pMaterialModuleOutput;
		const  auto& c_materialModuleOutput = materialModuleOutput;


		std::vector<SceneBlendShapeData> loadedActorBlendShapeData(m_actorList.size());			
		std::vector<GameFlags> loadedActorsGameflags(m_actorList.size());
		std::vector<BuildPipeline::RigInfo> loadedActorsRigInfos(m_actorList.size());
		std::vector<std::unique_ptr<BuildPipeline::SceneGeoDb> > loadedActorsGeoDBs;

		for (size_t iActor = 0; iActor != m_actorList.size(); ++iActor)
		{
			auto geoActor = m_actorList[iActor];
			loadedActorsGameflags[iActor] = GameFlagsFromJsonString(geoActor->m_gameFlagsInfo.m_gameFlags, geoActor->Name());

			{
				NdbStream stream;
				BuildFile riginfoFile = GetInputFile("riginfo_" + geoActor->Name());
				DataStore::ReadData(riginfoFile, stream);
				loadedActorsRigInfos[iActor].NdbSerialize(stream);
			}

			{
				loadedActorsGeoDBs.emplace_back(new BuildPipeline::SceneGeoDb());
				BuildPipeline::SceneGeoDb&geoDb = *(loadedActorsGeoDBs.back());

				NdbStream stream;
				const BuildFile& ndbFile = GetInputFile("geoconvertndb_" + geoActor->Name());
				DataStore::ReadData(ndbFile, stream);

				// read all the bits we need (yeah all the read functions have different names)
				NdbRead(stream, "scene", geoDb.m_scene);
				geoDb.m_scene.m_bigExt->NdbRead(stream, BigScene::kSceneDbFull);
				BuildPipeline::NdbSerialize(stream, "helperScene", geoDb.m_helperScene);
				ImportFromStream(stream, "blendshapedata", loadedActorBlendShapeData[iActor]);
			}
		}

		const std::vector<SceneBlendShapeData>& actorBlendShapeData = loadedActorBlendShapeData;
		const std::vector<GameFlags>& actorGameFlags = loadedActorsGameflags;
		const std::vector<BuildPipeline::RigInfo>& actorsRigInfos = loadedActorsRigInfos;
		const std::vector<std::unique_ptr<const BuildPipeline::SceneGeoDb> >& actorsGeoDBs = toolsutils::constifyData(loadedActorsGeoDBs);

		const std::vector<const ITSCENE::CgfxShader *> &allShaders = toolsutils::constifyData(c_materialModuleOutput.m_allshaders);
		const std::vector<material_compiler::CompiledEffect>& allCompiledEffects = c_materialModuleOutput.m_allCompiledEffects;
		const std::map<std::string, std::vector<unsigned int> >& perActorSceneShaderIndexToMergedIndex = c_materialModuleOutput.m_actorSceneShaderIndexToMergedIndex;

		std::vector<std::vector<U32>> sceneShaderIndexToMergedIndex;
		for (auto pActor : m_actorList)
		{
			auto found = perActorSceneShaderIndexToMergedIndex.find(pActor->Name());
			if (found != perActorSceneShaderIndexToMergedIndex.end())
			{
				sceneShaderIndexToMergedIndex.push_back(found->second);
			}
		}

		const std::string& actorName = m_preEvaluateDependencies.GetValue("actorName");
		const libdb2::Actor* pDbActor = libdb2::GetActor(actorName);

		const ToolParams& tool = m_pContext->m_toolParams;
		const BuildPath& jsonPath = GetOutputPath("geometry_json");

		std::vector<const material_compiler::CompiledEffect*> allCompiledEffectsPointers;
		for (auto& compiledEffect : allCompiledEffects)
			allCompiledEffectsPointers.push_back(&compiledEffect);

		ExtractedEffectData extractedEffects;
		CreateEffectConstBuffersTable({}, allCompiledEffectsPointers, extractedEffects);
		extractedEffects.m_materialScopeConstBufferMap = ComputeMaterialScopeConstBufferMap(extractedEffects.m_extractedEffects);

		// Vertex compression
		{
			AttributeCompressionStats stats;

			bool enableVertexCompression = m_preEvaluateDependencies.GetBool("enableVertexCompression");

			std::vector<const material_compiler::CachedEffect*> allCachedEffectsPointers;
			for (auto compiled : allCompiledEffectsPointers)
			{
				allCachedEffectsPointers.push_back(&compiled->m_effect);
			}

			for (size_t iScene = 0; iScene < actorsGeoDBs.size(); ++iScene)
			{
				const BuildPipeline::SceneGeoDb *sceneGeoDb = actorsGeoDBs[iScene].get();
				if (!sceneGeoDb)
				{
					continue;
				}

				const ITSCENE::SceneDb &sceneDd = sceneGeoDb->m_scene;
				const BigWriter::Scene &helperScene = sceneGeoDb->m_helperScene;
				model_writer_4::Model4 *pModel = (model_writer_4::Model4*)helperScene.m_model4;
				if (!pModel)
				{
					continue;
				}

				std::vector<MeshAttributeData> attributes;
				std::vector<MeshAttributeData> instanceAttributes;

				ba4::GetRenderUnitsAttributesAndIndices(m_actorList[iScene]->m_lodIndex, pModel, sceneDd, helperScene,
														allCachedEffectsPointers, sceneShaderIndexToMergedIndex[iScene],
														attributes, instanceAttributes, pModel->m_indices);

				CompressAttributes(enableVertexCompression, attributes, pModel->m_compressedAttributes, stats);
				CompressAttributes(enableVertexCompression, instanceAttributes, pModel->m_compressedInstanceAttributes, stats);
			}

			PrintAttributeCompressionStats(enableVertexCompression, stats);
		}
		
		/////////////////////////////////////////////////////////////////////////////
		//////////////////////Writing of the json file //////////////////////////////
		/////////////////////////////////////////////////////////////////////////////
		rapidjson::Document doc;
		bool executeJsonToBo = false;
		bool forceWriteGeoSection = false;

		//use a scope here so we don't have twice the json in jsonString and jsonDocument.
		{
			string jsonString;

			//use a scope here so we don't store twice the data in jsonString and in jsonWriter.
			{
				toolsutils::JsonStringWriter jsonWriter;
				FormatToJson(jsonWriter,
							 actorsRigInfos,
							 actorsGeoDBs,
							 actorGameFlags,
							 sceneShaderIndexToMergedIndex,
							 actorBlendShapeData,
							 extractedEffects,
							 texturesHashesAndTgds,
							 allShaders,
							 allCompiledEffectsPointers,
							 m_actorList);

				jsonString = jsonWriter.str();
			}

			DataStore::WriteData(jsonPath, jsonString);

			forceWriteGeoSection = pDbActor->m_extraMaterialsList.size() > 0;
			executeJsonToBo = pDbActor->m_geometry.Loaded() && 
							pDbActor->m_geometry.m_sceneFile.size() || 
							pDbActor->m_subActors.size() || 
							forceWriteGeoSection;

			if (executeJsonToBo)
			{
				doc.Parse(jsonString.c_str());
				if (doc.HasParseError())
				{
					IABORT("Failed to parse the json document %s\n", jsonPath.AsPrefixedPath().c_str());
				}
			}
		}

		if (executeJsonToBo && 
			!JsonToBo(doc, pDbActor, forceWriteGeoSection))
		{
			return BuildTransformStatus::kFailed;
		}

		return BuildTransformStatus::kOutputsUpdated;
	}
};

//--------------------------------------------------------------------------------------------------------------------//
extern std::vector<const libdb2::Actor*> GetGeometryActors(const std::vector<const libdb2::Actor*>& actorList);
bool BuildModuleGeometryBo_Configure(const BuildTransformContext *const pContext, 
									const libdb2::Actor *const pDbActor, 
									const std::vector<const libdb2::Actor*>& actorList, 
									std::vector<std::string>& arrBoFiles, 
									const std::vector<TransformInput>& inputFiles_)
{
	bool forceWriteGeoSection = pDbActor->m_extraMaterialsList.size() > 0;
	std::vector<TransformInput> inputFiles(inputFiles_);
	auto geometryActorList = GetGeometryActors(actorList);

	if (pDbActor->m_geometry.Loaded() && pDbActor->m_geometry.m_sceneFile.size() || pDbActor->m_subActors.size() || forceWriteGeoSection)
	{
		auto pGeoBoTransform = std::make_unique<BuildTransformGeometryBo>(pContext, 
																		pDbActor->Name(), 
																		geometryActorList);
		const auto& tool = pContext->m_toolParams;
		std::vector<TransformOutput> outputFiles;

		const std::string textureHashMapFilename = tool.m_buildPathConsolidateTextures + pDbActor->FullName() + FileIO::separator + "textureSpecHashMap.ndb";
		TransformInput textureHashMapPath(textureHashMapFilename, "textureSpecHashMap");
		inputFiles.push_back(textureHashMapPath);

		for (auto pActor : geometryActorList)
		{
			const std::string rigInfoFilename = pContext->m_toolParams.m_buildPathRigInfo + pActor->FullNameNoLod() + FileIO::separator + "riginfo.ndb";
			TransformInput rigInfoFile(rigInfoFilename, std::string("riginfo_") + pActor->Name());
			inputFiles.push_back(rigInfoFile);

			const std::string geoconvertFilename = pContext->m_toolParams.m_buildPathGeoConvert + pActor->FullName() + FileIO::separator + "geoconvert.ndb";
			TransformInput geoconvertFile(geoconvertFilename, std::string("geoconvertndb_") + pActor->Name());
			inputFiles.push_back(geoconvertFile);
		}


		const std::string& jsonFileName = tool.m_jsonPakPath + pDbActor->Name() + ".pak.json";
		std::string actorBuildFolder = tool.m_buildPathGeoBo + pDbActor->FullName() + FileIO::separator;
		std::string strGeoBo = actorBuildFolder + "geo.bo";
		outputFiles.emplace_back(jsonFileName, "geometry_json");
		outputFiles.emplace_back(strGeoBo, "geometry_bo");
		arrBoFiles.push_back(strGeoBo);


		pGeoBoTransform->SetInputs(inputFiles);
		pGeoBoTransform->SetOutputs(outputFiles);
		pContext->m_buildScheduler.AddBuildTransform(pGeoBoTransform.release(), pContext);
	}


	return true;
}
