/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/stdafx.h"

#include "tools/pipeline3/build/build-transforms/build-transform-textures.h"

#include "tools/pipeline3/build/build-transforms/build-transform-collect-textures.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-texture-read-spec.h"
#include "tools/pipeline3/build/build-transforms/build-transform-textures-merge.h"
#include "tools/pipeline3/build/util/dependency-database-manager.h"
#include "tools/pipeline3/build/util/loaded-texture-map.h"
#include "tools/pipeline3/build/level/build-transform-bl-lights.h"
#include "tools/pipeline3/common/4_textures.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/build-parser.h"
#include "tools/pipeline3/common/lightmap-injection.h"
#include "tools/pipeline3/textures-common/texture-pakwriter.h"
#include "tools/pipeline3/textures-common/texture-serialization.h"
#include "tools/pipeline3/toolversion.h"
#include "tools/libs/bigstreamwriter/ndi-bo-writer.h"
#include "tools/libs/toolsutil/constify.h"
#include "tools/libs/toolsutil/command-server.h"
#include "tools/libs/toolsutil/filename.h"
#include "tools/libs/toolsutil/ndio.h"
#include "tools/libs/toolsutil/simpledb.h"
#include "tools/libs/toolsutil/strfunc.h"
#include "tools/libs/libdb2/db2-actor.h"
#include "tools/libs/libdb2/db2-level.h"

#include "tools/pipeline3/build/build-transforms/build-transform-spawner.h"


using std::pair;
using std::string;
using std::vector;
using std::unique_ptr;

//#pragma optimize("", off)

static unsigned int GetTextureSize(const BigWriter::Texture *tex)
{
	unsigned int size = 0;
	for (size_t iImage = 0; iImage != tex->m_imageArray.size(); ++iImage)
	{
		const auto& image = tex->m_imageArray[iImage];
		for (size_t iMip = 0; iMip != image.m_mipArray.size(); ++iMip)
		{
			const auto& mip = image.m_mipArray[iMip];
			size += mip.m_size;
		}
	}
	return size;
}

static void DumpTextureStats(const std::vector<const BigWriter::Texture *>& textures, TextureGenerationData& tgd, const std::string& actorname)
{
	Mysql::Connection mysqlConn(false);
	ProjectConfiguration& cfg(GetProjectConfiguration());
	const char *worldDatabase = cfg["db.world_db"];
	const char *worldDatabaseServer = cfg["db.world_host"];
	if (!mysqlConn.realConnect(worldDatabaseServer, "tool", "", worldDatabase, 3306, NULL, 0, NULL))
	{
		IABORT("Prototype: Couldn't connect to mysql! Error %s", mysqlConn.errString());
	}
	for (size_t iTex = 0; iTex != textures.size(); ++iTex)
	{
		auto builtTexture = textures[iTex];
		auto texdesc = tgd.m_textureSpec[iTex];
		unsigned int textureSize = GetTextureSize(builtTexture);

		BuildPath path(texdesc.m_cachedFilename);
		string abs = path.AsAbsolutePath();
		size_t pos = abs.find("main");
		std::string cacheFileName = abs.substr(pos);

		std::string query = "REPLACE  INTO  texture_stats SET where_used = \"" + actorname + "\"";
		query += ", byte_size=" + toolsutils::ToString(textureSize);
		query += ", texture_name=\"" + cacheFileName + "\"";
		//		INOTE("QUERY %s", query.c_str());
		mysqlConn.realQuery(query.c_str(), (unsigned long)query.size());
	}
}

BuildTransform_CollectLightmaps::BuildTransform_CollectLightmaps(const std::string& actorBaseName
																, size_t actorLod
																, const ToolParams& toolParams)
																: BuildTransform("CollectLightmaps")
																, m_toolParams(toolParams)
{
	SetDependencyMode(DependencyMode::kIgnoreDependency);
	m_preEvaluateDependencies.SetBool("isLevel", false);
	m_preEvaluateDependencies.SetString("actorBaseName", actorBaseName);
	m_preEvaluateDependencies.SetInt("actorLod", actorLod);
}

BuildTransform_CollectLightmaps::BuildTransform_CollectLightmaps(const std::string& levelName
																, const ToolParams& toolParams)
																: BuildTransform("CollectLightmaps")
																, m_toolParams(toolParams)
{
	SetDependencyMode(DependencyMode::kIgnoreDependency);
	m_preEvaluateDependencies.SetBool("isLevel", true);
	m_preEvaluateDependencies.SetString("levelName", levelName);
}

static std::string	CopyLightmapToDisk(const BuildFile& exrFile, std::string destPath)
{
	std::string filePath = exrFile.AsAbsolutePath();

	// Get the build file plus one level up of the dir path
	auto pos = filePath.rfind('/');
	pos = filePath.rfind('/', pos - 1);
	std::string	diskExr = filePath.substr(pos + 1);

	INOTE_VERBOSE("Copying %s to dist at location %s", exrFile.AsPrefixedPath().c_str(), (destPath + diskExr).c_str());
	DataStore::DownloadFile(destPath + diskExr, exrFile);

	return destPath + diskExr;
}

static LightmapSet	CopyLightmapsToDisk(const BuildFile& exrListFile, std::string destPath)
{
	LightmapSet	lmap;
	toolsutils::CreatePath(destPath.c_str());

	vector<BuildFile> exrs;
	ExtractBuildFileList(exrListFile, exrs);

	for (U32 iExr = 0; iExr < exrs.size(); iExr += 4)
	{
		lmap.m_baseColor.push_back(CopyLightmapToDisk(exrs[iExr + 0], destPath));
		lmap.m_lightColor.push_back(CopyLightmapToDisk(exrs[iExr + 1], destPath));
		lmap.m_lightDir.push_back(CopyLightmapToDisk(exrs[iExr + 2], destPath));
		lmap.m_bentNormal.push_back(CopyLightmapToDisk(exrs[iExr + 3], destPath));
	}

	return lmap;
}

static BuildFile	GetLevelLightmapList(std::string levelName)
{
	// get the latest level with the lightmap
	using namespace BuildParser;

	BuildIdArray	buildIds(levelName);
	for (const std::string& buildId : buildIds.GetBuildIDs())
	{
		BuildGraph buildGraph(buildId);

		BuildFile exrList = buildGraph.GetBuildFile("/lightmap-exrs.a");
		if (exrList.IsValid())
		{
			return exrList;
		}
	}

	return BuildFile();
}

BuildTransformStatus BuildTransform_CollectLightmaps::Evaluate()
{
	const bool isLevel = m_preEvaluateDependencies.GetBool("isLevel");

	std::string assetName;
	if (isLevel)
	{
		assetName = m_preEvaluateDependencies.GetValue("levelName");
	}
	else
	{
		assetName = "THIS IS NOT A LEVELNAME";
	}

	LightmapTextures lmapTex;

	if (isLevel)
	{
		const std::string lightmapPath = GetLightmapFolder(m_toolParams.m_localLights) + assetName + FileIO::separator;

		vector<BuildFile> exrLists;
		ExtractBuildFileList(GetInputFile("AllLightmapExrs"), exrLists);

		for (auto& exrList : exrLists)
		{
			lmapTex.m_lmap.emplace_back(CopyLightmapsToDisk(exrList, lightmapPath));
		}
	}
	else
	{
		const libdb2::Actor* pDbActor = nullptr;
		const libdb2::LightmapsOverride* pLightmapOverride = nullptr;

		const std::string& actorBaseName = m_preEvaluateDependencies.GetValue("actorBaseName");
		const size_t actorLod = m_preEvaluateDependencies.GetInt("actorLod");

		const std::string lightmapPath = GetLightmapFolder(m_toolParams.m_localLights) + actorBaseName + FileIO::separator;

		pDbActor = libdb2::GetActor(actorBaseName, actorLod);
		if (!pDbActor->Loaded())
		{
			IABORT("Unknown actor %s\n", actorBaseName.c_str());
		}

		const libdb2::LightmapsOverride& lightmapData = pDbActor->m_geometry.m_lightmapsOverride;
		if (lightmapData.m_sources.size() >= 1)
		{
			const std::string& levelName = lightmapData.m_sources[0].m_name;
			BuildFile exrList = GetLevelLightmapList(levelName);
			lmapTex.m_lmap.emplace_back(CopyLightmapsToDisk(exrList, lightmapPath + levelName + FileIO::separator));
		}
		if (lightmapData.m_sources.size() >= 2)
		{
			const std::string& levelName = lightmapData.m_sources[1].m_name;
			BuildFile exrList = GetLevelLightmapList(levelName);
			lmapTex.m_lmap.emplace_back(CopyLightmapsToDisk(exrList, lightmapPath + levelName + FileIO::separator));
		}
	}

	const BuildPath& outputPath = GetOutputPath("LightmapList");
	DataStore::WriteSerializedData(outputPath, lmapTex);

	return BuildTransformStatus::kOutputsUpdated;
}

BuildTransform_ConsolidateAndBuildTextures::BuildTransform_ConsolidateAndBuildTextures(const BuildTransformContext *const pContext
																					, const ToolParams &toolParams
																					, bool isLevel
																					, const std::string& assetName)
																					: BuildTransform("ConsolidateTexture", pContext)
																					, m_toolParams(toolParams)
{
	m_preEvaluateDependencies.SetBool("isLevel", isLevel);
	m_preEvaluateDependencies.SetString("assetName", assetName);
}

BuildTransformStatus BuildTransform_ConsolidateAndBuildTextures::Evaluate()
{
	std::vector<size_t> textureArrayIndex2TGDIndex;
	std::vector<std::unique_ptr<BigWriter::Texture>> builtTextures;

	std::string extraTexturesExportPath = "Z:/big4/build/__delete_me__";

	const BuildFile& inputFile = GetFirstInputFile();
	vector<BuildFile> extractedInputs;
	ExtractBuildFileList(inputFile, extractedInputs);

	TextureGenerationData tgd;
	for (const BuildFile& textureInputFile : extractedInputs)
	{
		if (textureInputFile.AsPrefixedPath().find("dummy.txt") != string::npos)
		{
			continue;
		}
		
		NdbStream inputStream;
		DataStore::ReadData(textureInputFile, inputStream);

		tgd.m_textureSpec.push_back(TextureLoader::TextureGenerationSpec());
		builtTextures.push_back(std::unique_ptr<BigWriter::Texture>(new BigWriter::Texture()));

		NdbRead(inputStream, "spec", tgd.m_textureSpec.back());
		NdbRead(inputStream, "texture", *builtTextures.back().get());

		textureArrayIndex2TGDIndex.push_back(textureArrayIndex2TGDIndex.size()); //this could be remove cause now tgd and buildTextures indices match but some code in bl uses this so I can't remove it yet.
	}

	std::vector<const BigWriter::Texture*> builtTexturesPtrs(toolsutils::extractNonOwningPointers(toolsutils::constifyData(builtTextures)));

	LoadedTextureMap texturesHashesAndTgds;
	const size_t textureCount = builtTextures.size();
	for (size_t index = 0; index < textureCount; ++index)
	{
		TextureLoader::TextureGenerationSpec* spec = &tgd.m_textureSpec[index];
		LoadedTextureId hash = pipeline3::texture_pak_writer::GetTextureNameHash(*builtTexturesPtrs[index]);
		texturesHashesAndTgds.push_back(std::make_pair(hash, spec));
	}

	if (DoesInputExist("LightTexturesIndexed"))
	{
		std::vector<LightBoTexture> lightTextures;
		NdbStream lightTexturesNdb;
		DataStore::ReadData(GetInputFile("LightTexturesIndexed"), lightTexturesNdb);

		NdbRead(lightTexturesNdb, "m_lightTextures", lightTextures);

		PatchLightTextureReferences(lightTextures, textureArrayIndex2TGDIndex, tgd.m_textureSpec.size(), builtTexturesPtrs);
		
		NdbStream lightTexturesPatchedNdb;
		lightTexturesPatchedNdb.OpenForWriting(Ndb::kBinaryStream);
		NdbWrite(lightTexturesPatchedNdb, "m_lightTextures", lightTextures);
		lightTexturesPatchedNdb.Close();

		DataStore::WriteData(GetOutputPath("LightTexturesPatched"), lightTexturesPatchedNdb);
	}

	const bool isLevel = m_preEvaluateDependencies.GetBool("isLevel");
	if (!isLevel)
	{
		const std::string& assetName = m_preEvaluateDependencies.GetValue("assetName");
		DumpTextureStats(builtTexturesPtrs, tgd, assetName); // this is to figure out the size of textures used in actors.
	}

	if (!WriteBoFiles(tgd, textureArrayIndex2TGDIndex, builtTexturesPtrs, extraTexturesExportPath, builtTextures))
	{
		return BuildTransformStatus::kFailed;
	}

	if (!WriteCache(builtTextures, tgd))
	{
		return BuildTransformStatus::kFailed;
	}

	INOTE_VERBOSE("Done writing textures");

	{
		NdbStream hashMapStream;
		hashMapStream.OpenForWriting(Ndb::kBinaryStream);
		NdbWrite(hashMapStream, "hashmap", texturesHashesAndTgds);
		hashMapStream.Close();

		const BuildPath& hashMapPath = GetOutputPath("textureSpecHashMap");
		DataStore::WriteData(hashMapPath, hashMapStream);
	}

	return BuildTransformStatus::kOutputsUpdated;
}

bool BuildTransform_ConsolidateAndBuildTextures::WriteBoFiles(TextureGenerationData& tgd,
					const vector<size_t>& textureArrayIndex2TGDIndex,
					const vector<const BigWriter::Texture*>& builtTexturesPtrs,
					std::string& extraTexturesExportPath,
					const std::vector<unique_ptr<BigWriter::Texture>>& builtTextures)
{
	const bool isLevel = m_preEvaluateDependencies.GetBool("isLevel");
	const auto kAllMips = std::pair<size_t, size_t>(0, isLevel ? 16384 : 8192);
	const auto kLowMips = std::pair<size_t, size_t>(0, 64);

	const bool hasHigh = HasOutput("TexturesBo");

	//const bool hasLow = HasOutput("TexturesBoLow"); // you *must* output at least the low mips
	const BuildPath texturesBoLowPath = GetOutputPath("TexturesBoLow");

	BuildPath texturesBoPath;
	if (hasHigh)
	{
		texturesBoPath = GetOutputPath("TexturesBo");
	}
	else
	{
		std::string absPath = texturesBoLowPath.AsAbsolutePath();

		auto pos = absPath.find(".low");
		if (pos != std::string::npos)
			absPath = absPath.substr(0, pos);

		texturesBoPath = BuildPath(absPath);
	}

	BigStreamWriter textureStream(m_toolParams.m_streamConfig);

	if (hasHigh)
	{
		INOTE_VERBOSE("Writing texture bo %s", texturesBoPath.AsPrefixedPath().c_str());
		pipeline3::texture_pak_writer::WriteTextures(textureStream, &tgd, textureArrayIndex2TGDIndex, builtTexturesPtrs, extraTexturesExportPath, kAllMips, true, true);	// embed dictionary + tiled textures
		NdiBoWriter boWriter(textureStream);
		boWriter.Write();

		DataStore::WriteData(texturesBoPath, boWriter.GetMemoryStream());
	}

	//if (hasLow) // you *must* output at least the low mips
	{
		INOTE_VERBOSE("Writing LOW texture bo %s", texturesBoLowPath.AsPrefixedPath().c_str());
		BigStreamWriter textureStreamLow(m_toolParams.m_streamConfig);
		pipeline3::texture_pak_writer::WriteTextures(textureStreamLow, &tgd, textureArrayIndex2TGDIndex, builtTexturesPtrs, extraTexturesExportPath, (hasHigh ? kLowMips : kAllMips), false, true);
		NdiBoWriter boWriter(textureStreamLow);
		boWriter.Write();

		DataStore::WriteData(texturesBoLowPath, boWriter.GetMemoryStream());
	}

	SimpleDB::Set(texturesBoPath.AsAbsolutePath(), FormatString(64, "%i", textureStream.GetCurrentStreamSize()));		// legacy in case the we page needs it.
	SimpleDB::Set(texturesBoPath.AsAbsolutePath() + ".TextureMemory", FormatString(64, "%i", textureStream.GetCurrentStreamSize()));
	SimpleDB::Set(texturesBoPath.AsAbsolutePath() + ".TextureCount", FormatString(64, "%i", builtTextures.size()));

	return true;
}

bool BuildTransform_ConsolidateAndBuildTextures::WriteCache(const vector<unique_ptr<BigWriter::Texture>>& builtTextures, TextureGenerationData& tgd) const
{
	NdbStream stream;
	Err ret = tgd.WriteToNdbStream(stream);
	if (ret.Failed())
	{
		IERR("Failed to write texture cache to ndb stream.");
		return false;
	}

	const BuildPath& boCachePath = GetOutputPath("TexturesBoCache");
	DataStore::WriteData(boCachePath, stream);

	{
		size_t writeOffset = 0;
		const int BUFFER_SIZE = 1024 * 1024; // 1MB
		unique_ptr<char[]> buffer(new char[BUFFER_SIZE]);
		buffer[0] = '\0'; // null-terminate in case we don't have any texes and never enter the loop

		for (size_t iTexture = 0; iTexture != tgd.m_textureSpec.size(); ++iTexture)
		{
			TextureLoader::TextureGenerationSpec& spec = tgd.m_textureSpec[iTexture];
			LoadedTextureId hash = pipeline3::texture_pak_writer::GetTextureNameHash(*builtTextures[iTexture]);
			
			writeOffset += snprintf(buffer.get() + writeOffset, BUFFER_SIZE, "Texture[%d] = file %s 0x%ullx\n", iTexture, spec.m_cachedFilename.c_str(), hash.Value());
			
			if (writeOffset > BUFFER_SIZE)
			{
				IABORT("Failed to create the TexturesBoCacheTxt because the buffer is too small. Please contact tools-dog.");
			}
		}

		const BuildPath& liveTextureFile = GetOutputPath("TexturesBoCacheTxt");
		DataStore::WriteData(liveTextureFile, buffer.get());

		const bool isLevel = m_preEvaluateDependencies.GetBool("isLevel");
		std::string channel = std::string("livetexture.") + m_toolParams.m_gameName + (isLevel ? ".bl" : ".ba");
		std::string command = std::string("refresh_file ") + boCachePath.AsAbsolutePath();
		toolsutils::g_commandServer.SendRedisCommand(channel, command);
	}

	return true;
}

//--------------------------------------------------------------------------------------------------------------------//
void BuildModuleTextures_Configure(const BuildTransformContext *const pContext,
								const libdb2::Actor *const pDbActor,
								const std::vector<const libdb2::Actor*>& actorList,
								std::string& textureBoFilename,
								std::string& textureBoLowFilename,
								std::string& lightBoFilename,
								U32 numLightSkels)
{
	auto&tool = pContext->m_toolParams;

	// Validate the actors
	bool abort = false;
	for (size_t iactor = 0; iactor<actorList.size(); ++ iactor)
	{
		const libdb2::Actor *const pActor = actorList[iactor];

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

	std::vector<TransformInput> inputsToTextureConsolidate;
	for (auto dbactor : actorList)
	{
		// We need to get our inputs from individual actors material lists and not the consolidated list
		// Because we may need to patch in lightmap texture on a per subactor basis
		if (dbactor->m_geometry.m_sceneFile.size() || dbactor->m_extraMaterialsList.size())
		{
			const std::string fileListPath = toolsutils::GetBuildPathNew() + BUILD_TRANSFORM_BL_LMAP_COLLECT_FOLDER + FileIO::separator + dbactor->Name() + FileIO::separator + "lightmaps.ndb";
			const TransformOutput lightMapList(fileListPath, "LightmapList");

			if (dbactor->m_lodIndex == 0)
			{
				auto *const lmapCollectTransform = new BuildTransform_CollectLightmaps(dbactor->BaseName(), 
																					dbactor->m_lodIndex, 
																					tool);
				std::vector<TransformOutput> lmapCollectOutputs;
				lmapCollectOutputs.push_back(lightMapList);
				lmapCollectTransform->SetOutputs(lmapCollectOutputs);
				pContext->m_buildScheduler.AddBuildTransform(lmapCollectTransform, pContext);
			}

			BuildTransform_CollectTexturesFromMaterials *const pTransform = new BuildTransform_CollectTexturesFromMaterials(pContext, dbactor->m_geometry.m_lightmapsOverride.m_enabled);
			std::string matBuildFolder = pContext->m_toolParams.m_buildPathMatBuild + dbactor->FullName() + FileIO::separator;
			TransformInput inputFile(matBuildFolder + "materials.ndb");
			std::vector<TransformInput> collectTexturesInputs;
			collectTexturesInputs.push_back(inputFile);
			if (dbactor->m_lodIndex == 0)
				collectTexturesInputs.push_back(lightMapList);

			std::vector<TransformOutput> transformOutputs;
			
			const std::string textureGenFilename = pContext->m_toolParams.m_buildPathCollectTexturesFromMaterials + dbactor->FullName() + FileIO::separator + "texturegen.ndb";
			const string samplerPatcheList = pContext->m_toolParams.m_buildPathCollectTexturesFromMaterials + dbactor->FullName() + FileIO::separator + "samplerpatchlist.ndb";
			TransformOutput textureOutput(textureGenFilename, "TextureGenNdb");
			TransformOutput samplerPatchListOutput(samplerPatcheList, "SamplerPatchListNdb");
			transformOutputs.push_back(textureOutput);
			transformOutputs.push_back(samplerPatchListOutput);
			pTransform->SetInputs(collectTexturesInputs);
			pTransform->SetOutputs(transformOutputs);
			inputsToTextureConsolidate.push_back(textureOutput);
			pContext->m_buildScheduler.AddBuildTransform(pTransform, pContext);
		}
	}

	if (numLightSkels > 0)
	{
		BuildTransform_CollectTexturesFromMaterials *const pCollectTexturesFromMats = new BuildTransform_CollectTexturesFromMaterials(pContext, pDbActor->m_geometry.m_lightmapsOverride.m_enabled);
		const std::string lightTexturesFilename = pContext->m_toolParams.m_lightsBoPath + pDbActor->Name() + FileIO::separator + "lights-textures.ndb";
		pCollectTexturesFromMats->SetInput(TransformInput(lightTexturesFilename, "LightTextures"));
		const std::string textureGenFilename = pContext->m_toolParams.m_buildPathCollectTexturesFromMaterials + pDbActor->Name() + FileIO::separator + "lighttexturegen.ndb";
		TransformOutput textureOutput(textureGenFilename, "TextureGenNdb");
		pCollectTexturesFromMats->SetOutput(textureOutput);
	
		const string samplerPatcheList = pContext->m_toolParams.m_buildPathCollectTexturesFromMaterials + pDbActor->Name() + FileIO::separator + "samplerpatchlist.ndb";
		pCollectTexturesFromMats->AddOutput(TransformOutput(samplerPatcheList, "SamplerPatchListNdb"));
		pCollectTexturesFromMats->AddOutput(TransformOutput(pContext->m_toolParams.m_buildPathCollectTexturesFromMaterials + pDbActor->Name() + FileIO::separator + "lights-textures-indexed.ndb", "LightTexturesIndexed"));
		inputsToTextureConsolidate.push_back(TransformInput(textureOutput));
		pContext->m_buildScheduler.AddBuildTransform(pCollectTexturesFromMats, pContext);
	}

	if (inputsToTextureConsolidate.size())
	{
		TexturesMergeTransform* mergeTransform = new TexturesMergeTransform();
		mergeTransform->SetInputs(inputsToTextureConsolidate);
		
		string mergeTransformOutputFilename = pContext->m_toolParams.m_buildPathTexturesMerge + pDbActor->Name() + FileIO::separator + "texturesspeclist.ndb";
		TransformOutput mergeTransformOutputPath(mergeTransformOutputFilename);
		mergeTransform->SetOutput(mergeTransformOutputPath);

		pContext->m_buildScheduler.AddBuildTransform(mergeTransform, pContext);

		// --------------------------------------- Texture build --------------------------------------- 
		// TextureReadSpec
		const string consolidateInputFilename = pContext->m_toolParams.m_buildPathTextureReadSpec + pDbActor->Name() + FileIO::separator + "textureslist.a";
		const string spawnerOutputPath = pContext->m_toolParams.m_buildPathTextureReadSpec;
		const string buildOutputPath = pContext->m_toolParams.m_buildPathTextureBuild;
		TextureReadSpec *const texReadSpecTransform = new TextureReadSpec(pContext, pContext->m_toolParams, spawnerOutputPath, buildOutputPath, consolidateInputFilename);

		// TextureReadSpec - inputs
		texReadSpecTransform->SetInput(TransformInput(mergeTransformOutputPath));

		// TextureReadSpec - outputs
		std::vector<TransformOutput> texReadSpecOutputs;
		const TransformOutput dummyOutput = TransformOutput(pContext->m_toolParams.m_buildPathTextureReadSpec + pDbActor->Name() + FileIO::separator + "dummy.txt");
		//const TransformOutput texReadSpecOutputListOutput = TransformOutput(pContext->m_toolParams.m_buildPathTextureReadSpec + pDbActor->Name() + FileIO::separator + "outputlist.ndb", "OutputList");
		const TransformOutput texReadSpecSpawnListOutput = TransformOutput(pContext->m_toolParams.m_buildPathTextureReadSpec + pDbActor->Name() + FileIO::separator + "spawnlist.ndb", "SpawnList");
		texReadSpecOutputs.push_back(dummyOutput);
		//texReadSpecOutputs.push_back(texReadSpecOutputListOutput);
		texReadSpecOutputs.push_back(texReadSpecSpawnListOutput);
		texReadSpecTransform->SetOutputs(texReadSpecOutputs);

		pContext->m_buildScheduler.AddBuildTransform(texReadSpecTransform, pContext);

		// BuildTransformSpawner
		BuildTransformSpawner *const pSpawnerTransform = new BuildTransformSpawner(*pContext);

		// BuildTransformSpawner - inputs
		pSpawnerTransform->SetInput(texReadSpecSpawnListOutput);

		// BuildTransformSpawner - outputs
		pSpawnerTransform->SetOutput(BuildTransformSpawner::CreateOutputPath(texReadSpecSpawnListOutput));

		pContext->m_buildScheduler.AddBuildTransform(pSpawnerTransform, pContext);
		// --------------------------------------- Texture build --------------------------------------- 

		auto pTransform = new BuildTransform_ConsolidateAndBuildTextures(pContext, 
																		pContext->m_toolParams, 
																		false, 
																		pDbActor->Name());

		const std::string boTextureDirname = tool.m_buildPathConsolidateTextures + pDbActor->FullName() + FileIO::separator;

		textureBoFilename = boTextureDirname + "texture.bo";
		textureBoLowFilename = boTextureDirname + "texture.bo.low";
		std::string boTextureCacheFilename = textureBoFilename + ".cache";
		std::string boTextureCacheTxtFilename = boTextureCacheFilename + ".txt";
		std::string textureHashMapFilename = boTextureDirname + "textureSpecHashMap.ndb";
		std::vector<TransformOutput> outputFiles = {
			{ textureBoFilename, "TexturesBo" },
			{ textureBoLowFilename, "TexturesBoLow" },
			{ boTextureCacheFilename, "TexturesBoCache", TransformOutput::kReplicate }, // Replicate for: livetexture
			{ boTextureCacheTxtFilename, "TexturesBoCacheTxt", TransformOutput::kReplicate}, // Replicate for: livetexture
			{ textureHashMapFilename, "textureSpecHashMap"}
		};

		pTransform->SetInput(TransformInput(consolidateInputFilename));
		pTransform->SetOutputs(outputFiles);
		if (numLightSkels > 0)
		{
			pTransform->AddInput(TransformInput(pContext->m_toolParams.m_buildPathCollectTexturesFromMaterials + pDbActor->Name() + FileIO::separator + "lights-textures-indexed.ndb", "LightTexturesIndexed"));
			pTransform->AddOutput(TransformOutput(boTextureDirname + "lights-textures-patched.ndb", "LightTexturesPatched"));
		}

		pContext->m_buildScheduler.AddBuildTransform(pTransform, pContext);

		if (numLightSkels > 0)
		{
			BuildTransform_FixupLightsBo *pFixupLightsBoTransform = new BuildTransform_FixupLightsBo(true, pContext->m_toolParams);
			std::vector<TransformInput> fixupLightsInputs;
			std::vector<TransformOutput> fixupLightsOutputs;
			fixupLightsInputs.push_back(TransformInput(pContext->m_toolParams.m_lightsBoPath + pDbActor->Name() + FileIO::separator + "unpatched-lights.bo", "UnpatchedLightsBo"));
			fixupLightsInputs.push_back(TransformInput(boTextureDirname + "lights-textures-patched.ndb", "LightTexturesPatched"));
			lightBoFilename = pContext->m_toolParams.m_fixupLightsBoPath + pDbActor->Name() + FileIO::separator + "lights.bo";
			fixupLightsOutputs.push_back(TransformOutput(lightBoFilename, "LightsBo"));
			pFixupLightsBoTransform->SetInputs(fixupLightsInputs);
			pFixupLightsBoTransform->SetOutputs(fixupLightsOutputs);
			pContext->m_buildScheduler.AddBuildTransform(pFixupLightsBoTransform, pContext);
		}
	}
};
