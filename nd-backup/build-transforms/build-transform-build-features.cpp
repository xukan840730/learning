/*
* Copyright (c) 2018 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "build-transform-build-features.h"

#include "common/libphys/feature-db/feature-build/feature-db-builder.h"

#include "tools/libs/bigstreamwriter/ndi-bo-writer.h"
#include "tools/libs/physutil/physbackgroundbuilder2.h"
#include "tools/libs/toolsutil/filename.h"

#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-scheduler.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/ingame3pak/autospawner.h"
#include "tools/pipeline3/ingame3pak/emitfeatures.h"
#include "tools/pipeline3/ingame3pak/level-ingame-elements.h"
#include "tools/pipeline3/ingame3pak/spawner.h"
#include "tools/pipeline3/toolversion.h"

std::vector<TransformOutput> BuildTransform_BuildFeatures::FeatureBuildOutputs(const AssetType assetType, const std::string& assetName)
{
	const std::string commonIntermediateBuildPath = toolsutils::GetCommonBuildPathNew(toolsutils::IsBuildPathLocal());

	const EmitFeatures::AssetType featureType = assetType == AssetType::kActor ?
												EmitFeatures::AssetType::kAssetTypeForeground :
												EmitFeatures::AssetType::kAssetTypeBackground;

	std::string relativeAutoFilename;
	if (featureType == EmitFeatures::kAssetTypeForeground)
	{
		relativeAutoFilename += BUILD_TRANSFORM_FG_AUTO_FEATURES_BUCKET;
		relativeAutoFilename += "/actors/" + assetName + "/";
	}
	else
	{
		relativeAutoFilename += BUILD_TRANSFORM_BG_AUTO_FEATURES_BUCKET;
		relativeAutoFilename += "/levels/" + assetName + "/";
	}

	const std::string fullPath = commonIntermediateBuildPath + relativeAutoFilename;
	const std::string nickname = "AutoFeatures";

	std::vector<TransformOutput> outputs;
	for (int i = FeatureDbBuild::FeatureBuildPipelineStage::kGenerateMeshEdges; i < FeatureDbBuild::FeatureBuildPipelineStage::kPipelineStageCount; i++)
	{
		const std::string stageName = FeatureDbBuild::GetPipelineStageName(static_cast<FeatureDbBuild::FeatureBuildPipelineStage>(i));

		std::string stageFilename = fullPath + stageName;
		std::string stageNickname = nickname + "_" + stageName;
		
		outputs.push_back(TransformOutput(stageFilename, stageNickname));
	}

	return outputs;
}

static std::map<FeatureDbBuild::FeatureBuildPipelineStage, BuildPath> FeatureBuildPaths(const std::string& levelName)
{
	std::map<FeatureDbBuild::FeatureBuildPipelineStage, BuildPath> outputs;

	const std::string commonIntermediateBuildPath = toolsutils::GetCommonBuildPathNew(toolsutils::IsBuildPathLocal());

	std::string relativeAutoFilename = BUILD_TRANSFORM_BG_AUTO_FEATURES_BUCKET;
	relativeAutoFilename += "/levels/" + levelName + "/";

	const std::string fullPath = commonIntermediateBuildPath + relativeAutoFilename;

	for (int i = FeatureDbBuild::FeatureBuildPipelineStage::kGenerateMeshEdges; i < FeatureDbBuild::FeatureBuildPipelineStage::kPipelineStageCount; i++)
	{
		const FeatureDbBuild::FeatureBuildPipelineStage stage = static_cast<FeatureDbBuild::FeatureBuildPipelineStage>(i);
		const std::string stageName = FeatureDbBuild::GetPipelineStageName(stage);

		const std::string stageFilename = fullPath + stageName;
		outputs.insert({ stage, BuildPath(stageFilename) });
	}

	return outputs;
}

static void WritePipelineToDataStore(const std::map<FeatureDbBuild::FeatureBuildPipelineStage, BuildPath>& featureBuildPaths,
										const FeatureDbBuild::FeatureDbBuilder *const pBuilder, 
										const FeatureDbBuild::FeatureBuildPipeline *const pPipeline)
{
	for (int i = 0; i < pPipeline->GetStages().size(); i++)
	{
		const FeatureDbBuild::PipelineStage *const pStage = pPipeline->GetStages()[i];

		const std::map<FeatureDbBuild::FeatureBuildPipelineStage, BuildPath>::const_iterator iter = featureBuildPaths.find(pStage->GetStage());
		IASSERT(iter != featureBuildPaths.end());

		FeatureDb featureDb;
		pBuilder->OutputPipelineStageToFeatureDb(pStage, &featureDb);

		EmitFeatures::EmitFeatureDb emitFeatureDb;
		emitFeatureDb.AddFeatureDb(featureDb, false);

		emitFeatureDb.WriteDataStore(iter->second);
	}
}

BuildTransform_BuildFeatures::BuildTransform_BuildFeatures(const BuildTransformContext *const pContext
														, const std::string& levelName
														, const SchemaDependencyToken& token
														, const bool keepNoAimCorners)
														: BuildTransform("BuildFeatures", pContext)
														, m_token(token)
{
	m_preEvaluateDependencies.SetString("levelName", levelName);
	m_preEvaluateDependencies.SetString("autoFeaturesVersion", BUILD_TRANSFORM_AUTO_FEATURES_VERSION);
	m_preEvaluateDependencies.SetBool("keepNoAimCorners", keepNoAimCorners);
}

BuildTransformStatus BuildTransform_BuildFeatures::Evaluate()
{
	const std::string levelName = m_preEvaluateDependencies.GetValue("levelName");

	const std::map<FeatureDbBuild::FeatureBuildPipelineStage, BuildPath> featureBuildPaths = FeatureBuildPaths(levelName);
	const BuildPath autoFeaturesPath = EmitFeatures::EmitFeatureDb::GetAutoFilename(EmitFeatures::kAssetTypeBackground, 
																					levelName, 
																					toolsutils::IsBuildPathLocal());

	const BuildFile &featureMeshesFile = GetInputFile("AutoFeatureMeshes");
	NdbStream autoFeatureMeshesStream;
	DataStore::ReadData(featureMeshesFile, autoFeatureMeshesStream);

	size_t numInsts;
	size_t numNeighborInsts;

	NdbRead(autoFeatureMeshesStream, "numInsts", numInsts);
	NdbRead(autoFeatureMeshesStream, "numNeighborInsts", numNeighborInsts);

	if (!numInsts && !numNeighborInsts)
	{
		EmitFeatures::EmitFeatureDb emitFeatureDb;
		emitFeatureDb.AddFeatureDb(FeatureDb(), true);

		// Output empty feature build stages
		{
			for (std::map<FeatureDbBuild::FeatureBuildPipelineStage, BuildPath>::const_iterator iter = featureBuildPaths.begin(); iter != featureBuildPaths.end(); iter++)
			{
				emitFeatureDb.WriteDataStore(iter->second);
			}
		}

		// Output empty auto features
		{
			emitFeatureDb.WriteDataStore(autoFeaturesPath);
		}

		return BuildTransformStatus::kOutputsUpdated;
	}

	std::vector<PhysBackgroundBuilder2::MeshInstance> insts;
	std::vector<PhysBackgroundBuilder2::MeshInstance> neighborInsts;

	for (size_t i = 0; i < numInsts; i++)
	{
		PhysBackgroundBuilder2::MeshInstance inst;
		inst.m_pCollisionMesh = new BuildPipeline::CollisionMesh();

		const std::string instName = "inst_" + std::to_string(i);

		NdbRead(autoFeatureMeshesStream, (instName + ".collisionMesh").c_str(), (*inst.m_pCollisionMesh));

		Vec4 row;
		NdbRead(autoFeatureMeshesStream, (instName + ".tm0").c_str(), row);
		inst.m_tm.SetRow(0, row);

		NdbRead(autoFeatureMeshesStream, (instName + ".tm1").c_str(), row);
		inst.m_tm.SetRow(1, row);

		NdbRead(autoFeatureMeshesStream, (instName + ".tm2").c_str(), row);
		inst.m_tm.SetRow(2, row);

		NdbRead(autoFeatureMeshesStream, (instName + ".instanceIndex").c_str(), inst.m_instanceIndex);
		NdbRead(autoFeatureMeshesStream, (instName + ".instanceUid").c_str(), inst.m_instanceUid);

		NdbRead(autoFeatureMeshesStream, (instName + ".disableCoverGeneration").c_str(), inst.m_disableCoverGeneration);
		NdbRead(autoFeatureMeshesStream, (instName + ".disableEdgeGeneration").c_str(), inst.m_disableEdgeGeneration);
		NdbRead(autoFeatureMeshesStream, (instName + ".disableEdgeHang").c_str(), inst.m_disableEdgeHang);

		insts.push_back(inst);
	}

	for (size_t i = 0; i < numNeighborInsts; i++)
	{
		PhysBackgroundBuilder2::MeshInstance neighborInst;
		neighborInst.m_pCollisionMesh = new BuildPipeline::CollisionMesh();

		const std::string instName = "neighborInst_" + std::to_string(i);

		NdbRead(autoFeatureMeshesStream, (instName + ".collisionMesh").c_str(), (*neighborInst.m_pCollisionMesh));

		Vec4 row;
		NdbRead(autoFeatureMeshesStream, (instName + ".tm0").c_str(), row);
		neighborInst.m_tm.SetRow(0, row);

		NdbRead(autoFeatureMeshesStream, (instName + ".tm1").c_str(), row);
		neighborInst.m_tm.SetRow(1, row);

		NdbRead(autoFeatureMeshesStream, (instName + ".tm2").c_str(), row);
		neighborInst.m_tm.SetRow(2, row);

		NdbRead(autoFeatureMeshesStream, (instName + ".instanceIndex").c_str(), neighborInst.m_instanceIndex);
		NdbRead(autoFeatureMeshesStream, (instName + ".instanceUid").c_str(), neighborInst.m_instanceUid);

		NdbRead(autoFeatureMeshesStream, (instName + ".disableCoverGeneration").c_str(), neighborInst.m_disableCoverGeneration);
		NdbRead(autoFeatureMeshesStream, (instName + ".disableEdgeGeneration").c_str(), neighborInst.m_disableEdgeGeneration);
		NdbRead(autoFeatureMeshesStream, (instName + ".disableEdgeHang").c_str(), neighborInst.m_disableEdgeHang);

		neighborInsts.push_back(neighborInst);
	}

	PhysBackground background;
	PhysBackgroundBuilder2 backgroundBuilder(&background);

	backgroundBuilder.ConstructBuilder(insts, neighborInsts, 4096);
	background.Validate();

	const std::string autoActorsNickname = AutoSpawners::MakeAutoActorsNickname(levelName);
	const BuildFile autoActorsFile = GetInputFile(autoActorsNickname);

	std::string outAutoActorsStr;
	DataStore::ReadData(autoActorsFile, outAutoActorsStr);

	BigStreamWriterConfig dummyStreamConfig;
	std::unique_ptr<LevelIngameElements> pElems(LevelIngameElements::Create(levelName, dummyStreamConfig, false, outAutoActorsStr, m_token));

	std::vector<FeatureDbBuild::DoorSpawnerInfo> doorSpawnerInfo;
	for (int i = 0; i < pElems->m_spawners.size(); i++)
	{
		const Spawner *const pSpawner = pElems->m_spawners[i];
		const Schema *const pSchema = pSpawner->GetSchema();
		if (pSchema &&		// Skip already deleted schemas.
			(pSchema->Name() == "door-knob-l" || pSchema->Name() == "door-knob-r"))
		{
			FeatureDbBuild::DoorSpawnerInfo info;

			info.m_locator = pSpawner->GetLocator();

			const SchemaPropertyTable& properties = pSpawner->Properties();
			info.m_opensPosZ = properties.GetData<bool>(StringToStringId64("opens-pos-z"), true);
			info.m_opensNegZ = properties.GetData<bool>(StringToStringId64("opens-neg-z"), true);

			doorSpawnerInfo.push_back(info);
		}
	}

	{
		const bool keepNoAimCorners = m_preEvaluateDependencies.GetBool("keepNoAimCorners");

		FeatureDbBuild::FeatureDbBuilder *const pBuilder = new FeatureDbBuild::FeatureDbBuilder();

		pBuilder->BuildForBackground(&background.m_featureDb,
									&background,
									&doorSpawnerInfo, 
									keepNoAimCorners);

		// Output feature build stages
		{
			WritePipelineToDataStore(featureBuildPaths, pBuilder, &pBuilder->GetPipelineEdges());
			WritePipelineToDataStore(featureBuildPaths, pBuilder, &pBuilder->GetPipelineLowCornerEdges());
			WritePipelineToDataStore(featureBuildPaths, pBuilder, &pBuilder->GetPipelineCorners());
			WritePipelineToDataStore(featureBuildPaths, pBuilder, &pBuilder->GetPipelineCovers());
		}

		delete pBuilder;
	}

	// Output auto features
	{
		EmitFeatures::EmitFeatureDb emitFeatureDb;
		emitFeatureDb.AddFeatureDb(background.m_featureDb, true);

		emitFeatureDb.WriteDataStore(autoFeaturesPath);
	}

	background.m_featureDb.Clear();
	backgroundBuilder.Destruct();

	return BuildTransformStatus::kOutputsUpdated;
}

BuildTransform_EmitFeatures::BuildTransform_EmitFeatures(const std::string& levelName
														, const ToolParams& toolParams)
														: BuildTransform("EmitFeatures")
														, m_toolParams(toolParams)
{
	m_preEvaluateDependencies.SetString("levelName", levelName);
	m_preEvaluateDependencies.SetString("autoFeaturesVersion", BUILD_TRANSFORM_AUTO_FEATURES_VERSION);
}

BuildTransformStatus BuildTransform_EmitFeatures::Evaluate()
{
	BigStreamWriter streamWriter(m_toolParams.m_streamConfig);

	EmitFeatures::EmitFeatureDb autoFeatures;
	EmitFeatures::EmitFeatureDb userFeatures;

	autoFeatures.ReadDataStore(GetInputFile("AutoFeatures"));
	if (DoesInputExist("UserFeatures"))
	{
		const std::string filename = GetInputFile("UserFeatures").GetBuildPath().AsAbsolutePath();
		userFeatures.ReadFile(filename);
	}

	// merge auto and user feature together
	EmitFeatures::EmitFeatureDb featureDb;
	featureDb.AddFeatureDb(autoFeatures, true);
	featureDb.MergeUserFeaturesForBackground(userFeatures);

	if (!featureDb.m_nodeList.empty())
	{
		// write it out
		featureDb.WritePak(streamWriter);

		const std::string& levelName = m_preEvaluateDependencies.GetValue("levelName");

		size_t nEdges, nCorners, nCovers;
		featureDb.m_nodeList.CountFeatures(nEdges, nCorners, nCovers);

		INOTE_VERBOSE("Emitted %i edges, %i corners, and %i covers from %s\n",
					(int)nEdges,
					(int)nCorners,
					(int)nCovers,
					featureDb.GetAutoFilename(EmitFeatures::kAssetTypeBackground, levelName, toolsutils::IsBuildPathLocal()).c_str());
	}

	const BuildPath& outputBoPath = GetFirstOutputPath();
	NdiBoWriter boWriter(streamWriter);
	boWriter.Write();

	DataStore::WriteData(outputBoPath, boWriter.GetMemoryStream());

	return BuildTransformStatus::kOutputsUpdated;
}
