/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/stdafx.h"

#include "tools/pipeline3/build/build-transforms/build-transform-geometry.h"

#include "tools/libs/toolsutil/constify.h"
#include "tools/pipeline3/build/tool-params.h"
#include "tools/pipeline3/build/build-transforms/build-transform.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-materials.h"
#include "tools/pipeline3/build/build-transforms/build-scheduler.h"
#include "tools/pipeline3/build/util/convert_geometry.h"
#include "tools/pipeline3/common/4_gameflags.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/shcomp4/effect_metadata.h"
//#pragma optimize("", off)

using std::vector;

std::vector<const libdb2::Actor*> GetGeometryActors(const std::vector<const libdb2::Actor*>& actorList)
{
	// restrict data to only actors containing geometry.
	std::vector< const libdb2::Actor*> geometryActorList;
	for (auto pActor : actorList)
	{
		if (pActor->m_geometry.Loaded() && pActor->m_geometry.m_sceneFile.size())
		{
			geometryActorList.push_back(pActor);
		}
	}

	return geometryActorList;
}



static void LoadRig(BuildPipeline::RigInfo& actorRigInfo, const BuildFile& ringInfoFile)
{
	try
	{
		NdbStream streamRigInfo;
		DataStore::ReadData(ringInfoFile, streamRigInfo);
		actorRigInfo.NdbSerialize(streamRigInfo);
	}
	catch (const char * szIceError)
	{
		IABORT("ICE threw the following 'const char*' error: %s", szIceError);
		throw;  // in case ABORT did not abort or threw itself
	}
	catch (...)
	{
		throw;
	}
}


class BuildTransformGeometry : public BuildTransform
{
public:
	static const char* INPUT_NICKNAME_GEONDB;

	BuildTransformGeometry(const std::string& geoActorBaseName
						, size_t geoActorLod
						, const ToolParams& toolParams
						, std::vector<std::string>& arrBoFiles) 
						: BuildTransform("Geometry")
						, m_geoActorBaseName(geoActorBaseName)
						, m_geoActorLod(geoActorLod)
						, m_toolParams(toolParams)
						, m_arrBoFiles(arrBoFiles)
	{
		PopulatePreEvalDependencies();
	}

	void PopulatePreEvalDependencies()
	{
		const libdb2::Actor* pDbGeoActor = libdb2::GetActor(m_geoActorBaseName, m_geoActorLod);

		m_preEvaluateDependencies.SetString("db-gameflags", pDbGeoActor->m_gameFlagsInfo.m_gameFlags);
		m_preEvaluateDependencies.SetString("db-m_geometry", pDbGeoActor->m_geometry.Xml());	// the whole text of the geometry section
		m_preEvaluateDependencies.SetString("db-m_materialRemapList", pDbGeoActor->m_materialRemapList.Xml());
		m_preEvaluateDependencies.SetString("db-m_extraMaterialsList", pDbGeoActor->m_extraMaterialsList.Xml());
		m_preEvaluateDependencies.SetBool("db-m_disableReflections", pDbGeoActor->m_disableReflections);
		m_preEvaluateDependencies.SetBool("db-m_occludesRain", pDbGeoActor->m_occludesRain);
		m_preEvaluateDependencies.SetBool("db-m_disableReflections", pDbGeoActor->m_disableReflections);
		m_preEvaluateDependencies.SetBool("db-m_castsLocalShadows", pDbGeoActor->m_castsLocalShadows);
		m_preEvaluateDependencies.SetBool("db-m_castsShadows", pDbGeoActor->m_castsShadows);
	}

protected:
	virtual BuildTransformStatus Evaluate() override
	{
		const libdb2::Actor * pDbGeoActor = libdb2::GetActor(m_geoActorBaseName, m_geoActorLod);

		const BuildFile& materialFile = GetInputFile("consolidatedMaterials");
		NdbStream materialStream;
		DataStore::ReadData(materialFile, materialStream);

		BuildModuleMaterialsOutput materialInput;
		NdbRead(materialStream, "materialConsolidateOutput", materialInput);
		materialStream.Close();

		std::map<std::string, std::vector<unsigned int>>::const_iterator where = materialInput.m_actorSceneShaderIndexToMergedIndex.find(pDbGeoActor->Name());
		if (where == materialInput.m_actorSceneShaderIndexToMergedIndex.end())
		{
			IABORT("Could not find shader information for actor %s in the geometry transform", pDbGeoActor->Name().c_str());
		}
		const std::vector<unsigned int>& translateActorShader = where->second;


		const std::vector<const ITSCENE::CgfxShader*>& allshaders = toolsutils::constifyData(materialInput.m_allshaders);
		const std::map<std::string, std::vector<unsigned int>>& sceneShaderIndexToMergedIndex = materialInput.m_actorSceneShaderIndexToMergedIndex;
		auto pOutput = std::make_unique<BuildModuleGeometryOutput>();
		std::map<std::string, DataHash> outputHashes;

		BuildPipeline::RigInfo actorRigInfo;
		LoadRig(actorRigInfo, GetInputFile("riginfo"));

		ITSCENE::CgfxShaderList shadersList;
		vector<int> uniqueShaderMap;
		LoadShadersList(shadersList, uniqueShaderMap);
		const ITSCENE::ConstCgfxShaderConstList& constUniqueShaderMap = toolsutils::constifyData(materialInput.m_allshaders);

		BuildPipeline::SceneGeoDb actorGeoDb;

		U64 geoMemory = 0;

		const BuildFile& geoNdbFile = GetInputFile(BuildTransformGeometry::INPUT_NICKNAME_GEONDB);
		const BuildFile& matFlagsNdb = GetInputFile("matflags");

		bool ret = main_geo_ConvertGeometry_2(allshaders, translateActorShader, actorGeoDb, actorRigInfo, pDbGeoActor, m_toolParams, m_arrBoFiles, geoMemory, outputHashes, geoNdbFile, matFlagsNdb, uniqueShaderMap);


		INOTE_VERBOSE("Extracting blendshapes for actor %s", pDbGeoActor->Name());
		ITSCENE::SceneDb& scene = actorGeoDb.m_scene;
		BigWriter::Scene& helperScene = actorGeoDb.m_helperScene;
		ExtractBlendShapeData(pOutput->actorBlendShapeData, scene, toolsutils::constifyData(helperScene.m_allRenderUnits4));

		{
			INOTE_VERBOSE("Writing the geometry conversion file for actor %s", pDbGeoActor->Name().c_str());
			NdbStream stream;
			if (stream.OpenForWriting(Ndb::kBinaryStream) == Ndb::kNoError)
			{
				if (!actorGeoDb.ExportToStream(stream))
				{
					IABORT("BLAHHHH EXPORT FAILED");
				}
				ExportToStream(stream, "blendshapedata", pOutput->actorBlendShapeData);
				stream.Close();

				const BuildPath& geoDbConvertPath = GetFirstOutputPath();
				DataStore::WriteData(geoDbConvertPath, stream);
			}
		}

		return BuildTransformStatus::kOutputsUpdated;
	}

	void LoadShadersList(ITSCENE::CgfxShaderList& shadersList, vector<int>& uniqueShaderMap)
	{
		const BuildFile& shadersListFile = GetInputFile("shaderslist");

		NdbStream stream;
		DataStore::ReadData(shadersListFile, stream);

		NdbRead(stream, "m_actorShaders", shadersList);
		NdbRead(stream, "m_shaderToUniqueShader", uniqueShaderMap);
	}

private:
	const std::string m_geoActorBaseName;
	const size_t m_geoActorLod;
	const ToolParams& m_toolParams;		// Global tool configuration
	std::vector<std::string>& m_arrBoFiles;
};

const char* BuildTransformGeometry::INPUT_NICKNAME_GEONDB = "geondb";


class BuildTransformGeometryExtra : public BuildTransform
{
public:
	BuildTransformGeometryExtra(const std::string& geoActorBaseName
								, size_t geoActorLod
								, const ToolParams& tool) 
								: BuildTransform("GeometryExtra")
								, m_geoActorBaseName(geoActorBaseName)
								, m_geoActorLod(geoActorLod)
								, m_toolParams(tool)
								, m_translateActorShader(nullptr)
	{
		PopulatePreEvalDependencies();
	}

	void PopulatePreEvalDependencies() 
	{
		const libdb2::Actor *  pDbGeoActor = libdb2::GetActor(m_geoActorBaseName, m_geoActorLod);

		const libdb2::Geometry& geometry = pDbGeoActor->m_geometry;
		// Dependencies for the boundbox and tags
		m_preEvaluateDependencies.SetString("db-actor-geometry-visSphere", geometry.m_vis_sphere.Xml());
		m_preEvaluateDependencies.SetString("db-actor-geometry-visJointIndex", geometry.m_vis_joint_index.Xml());
		m_preEvaluateDependencies.SetString("db-actor-geometry-lodDistance", geometry.m_lodDistance.Xml());
		m_preEvaluateDependencies.SetString("db-actor-geometry-cubemapJointIndex", geometry.m_cubemap_joint_index);
		m_preEvaluateDependencies.SetString("db-actor-geometry-parentingJointName", geometry.m_parentingJointName.c_str());
		m_preEvaluateDependencies.SetString("db-actor-geometry-boundingBox", geometry.m_boundingBox.Xml());

		// Dependencies for the ambient occluders
		m_preEvaluateDependencies.SetBool("db-actor-castsAmbientShadows", pDbGeoActor->m_castsAmbientShadows);
		m_preEvaluateDependencies.SetInt("db-actor-volumeAmbientOccluderCellSize", pDbGeoActor->m_volumeAmbientOccluderCellSize);
		m_preEvaluateDependencies.SetInt("db-actor-volumeAmbientOccluderScale", pDbGeoActor->m_volumeAmbientOccluderScale);
		m_preEvaluateDependencies.SetBool("db-actor-volumeOccluderUseDirectionalTest", pDbGeoActor->m_volumeOccluderUseDirectionalTest);
		m_preEvaluateDependencies.SetBool("db-actor-generateMultipleOccluders", pDbGeoActor->m_generateMultipleOccluders);
		m_preEvaluateDependencies.SetInt("db-actor-volumeOccluderDefaultAttachJoint", pDbGeoActor->m_volumeOccluderDefaultAttachJoint);
		m_preEvaluateDependencies.SetFloat("db-actor-volumeOccluderSamplePointShift", pDbGeoActor->m_volumeOccluderSamplePointShift);
	}

protected:
	void LoadGeoDb(BuildPipeline::SceneGeoDb& actorGeoDb)
	{
		try
		{
			const BuildFile& geodbFile = GetInputFile("geoconvertndb");
			NdbStream streamGeoConvert;
			DataStore::ReadData(geodbFile, streamGeoConvert);
			actorGeoDb.ImportFromStream(streamGeoConvert);
		}
		catch (const char * szIceError)
		{
			IABORT("ICE threw the following 'const char*' error: %s", szIceError);
			throw;  // in case ABORT did not abort or threw itself
		}
		catch (...)
		{
			throw;
		}

	}

	virtual BuildTransformStatus Evaluate() override
	{
		const libdb2::Actor * pDbGeoActor = libdb2::GetActor(m_geoActorBaseName, m_geoActorLod);

		BuildPipeline::SceneGeoDb actorGeoDb;
		LoadGeoDb(actorGeoDb);

		BuildPipeline::RigInfo actorRigInfo;
		LoadRig(actorRigInfo, GetInputFile("riginfo"));

		U64 geoMemory = 0;
		bool ret = main_geo_pc_convert_geometryExtra_2(pDbGeoActor, 
													m_toolParams, 
													geoMemory, 
													&(actorGeoDb.m_scene), 
													&(actorGeoDb.m_helperScene), 
													actorRigInfo);

		return ret ? BuildTransformStatus::kOutputsUpdated : BuildTransformStatus::kFailed;
	}

private:
	const std::string m_geoActorBaseName;	// All geometry actors (and sub-actors) that need building
	const size_t m_geoActorLod;
	const ToolParams& m_toolParams;		// Global tool configuration
	const std::vector<unsigned int>* m_translateActorShader;
};

//--------------------------------------------------------------------------------------------------------------------//
void BuildModuleGeometry_Configure(const BuildTransformContext *const pContext,
								const libdb2::Actor *const pDbActor, 
								const std::vector<const libdb2::Actor*>& actorList,
								std::vector<std::string>& arrBoFiles)
{
	// Validate the actors
	bool abort = false;
	for (size_t iactor = 0; iactor < actorList.size(); ++iactor)
	{
		const libdb2::Actor *const pActor = actorList[iactor];

		// Geometry is only allowed to be exported from a set.
		if (!pActor->m_geometry.m_sceneFile.empty() && 
			pActor->m_geometry.m_set.empty())
		{
			IERR("Geometry in actor '%s' is exported without using a set. Update the Maya scene file and add the set name in Builder.\n", pActor->Name().c_str());
			abort = true;

			continue;
		}

		// All geometry lods have to use the same scene file
		if (pActor->m_geometryLods.size())
		{
			bool allSame = true;
			const std::string usedSceneFile = pActor->m_geometryLods[0].m_sceneFile;

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
	{
		IABORT("Unable to recover from previous error(s)\n");
	}

	auto geometryActors = GetGeometryActors(actorList);
	if (geometryActors.size())
	{
		// ACTUAL GEOMETRY
		for (const libdb2::Actor *const pActor : geometryActors)
		{
			std::vector<TransformInput> inputPaths;
			std::vector<TransformOutput> outputPaths;

			const std::string exportFileName = pContext->m_toolParams.m_buildPathGeo + 
											toolsutils::MakeGeoNdbFilename(pActor->m_geometry.m_sceneFile, pActor->m_geometry.m_set);
			TransformInput inputFile(exportFileName, BuildTransformGeometry::INPUT_NICKNAME_GEONDB);

			inputPaths.push_back(inputFile);

			const std::string rigInfoFilename = pContext->m_toolParams.m_buildPathRigInfo + pActor->FullNameNoLod() + FileIO::separator + "riginfo.ndb";
			TransformInput rigInfoFile(rigInfoFilename, std::string("riginfo"));
			inputPaths.push_back(rigInfoFile);

			//This should be factorize, see build-transform-materials.cpp line 275
			const std::string expanderActorPath = pContext->m_toolParams.m_buildPathMatExpander + pActor->Name() + FileIO::separator;
			const std::string expanderOutputPath = expanderActorPath + pActor->m_geometry.m_sceneFile + ".lod_" + std::to_string(pActor->m_lodIndex) + FileIO::separator;

			const std::string shadersListFilename = expanderOutputPath + "shaderslist.ndb";
			const std::string matFlagsFilename = expanderOutputPath + "matflags.ndb";

			TransformInput shadersListFile(shadersListFilename, std::string("shaderslist"));
			TransformInput matFlagsFile(matFlagsFilename, "matflags");

			inputPaths.push_back(shadersListFile);
			inputPaths.push_back(matFlagsFile);

			const std::string consolidatedMaterialsFilename = pContext->m_toolParams.m_buildPathMatConsolidateBo + pDbActor->FullName() + FileIO::separator + "consolidatedMaterials.ndb";
			inputPaths.emplace_back(consolidatedMaterialsFilename, "consolidatedMaterials");

			//The BuildTransformGeometry uses the default gameflags to create a json string containing all the gameflags and store it in the sub meshes.
			TransformInput gameFlagPath(GetGameDefaultGameFlagsFilename());
			inputPaths.push_back(gameFlagPath);

			const std::string geoconvertFilename = pContext->m_toolParams.m_buildPathGeoConvert + pActor->FullName() + FileIO::separator + "geoconvert.ndb";

			outputPaths.emplace_back(geoconvertFilename);

			std::unique_ptr<BuildTransformGeometry> geoBuild = std::make_unique<BuildTransformGeometry>(pActor->BaseName(), 
																										pActor->m_lodIndex, 
																										pContext->m_toolParams, 
																										arrBoFiles);
			
			geoBuild->SetInputs(inputPaths);
			geoBuild->SetOutputs(outputPaths);

			pContext->m_buildScheduler.AddBuildTransform(geoBuild.release(), pContext);
		}

		// extra GEOMETRY TRANSFORM
		for (auto pActor : geometryActors)
		{
			std::vector<TransformInput> inputPaths;
			std::vector<TransformOutput> outputPaths;

			const std::string rigInfoFilename = pContext->m_toolParams.m_buildPathRigInfo + pActor->FullNameNoLod() + FileIO::separator + "riginfo.ndb";
			const std::string geoconvertFilename = pContext->m_toolParams.m_buildPathGeoConvert + pActor->FullName() + FileIO::separator + "geoconvert.ndb";

			TransformInput rigInfoFile(rigInfoFilename, std::string("riginfo"));
			inputPaths.push_back(rigInfoFile);
			inputPaths.emplace_back(geoconvertFilename, "geoconvertndb");

			const std::string tagsFilename = pContext->m_toolParams.m_buildPathGeoExtraBo + pActor->FullName() + FileIO::separator + "tags.bo";
			const std::string ambOcclFilename = pContext->m_toolParams.m_buildPathGeoExtraBo + pActor->FullName() + FileIO::separator + "geo_amboccl.bo";

			outputPaths.emplace_back(tagsFilename);
			arrBoFiles.push_back(outputPaths.back().m_path.AsAbsolutePath());

			if (pActor->m_castsAmbientShadows)
			{
				outputPaths.emplace_back(ambOcclFilename);
				arrBoFiles.push_back(outputPaths.back().m_path.AsAbsolutePath());
			}

			std::unique_ptr<BuildTransformGeometryExtra> geometryExtra = std::make_unique<BuildTransformGeometryExtra>(pActor->BaseName(),
																													pActor->m_lodIndex, 
																													pContext->m_toolParams);
			geometryExtra->SetInputs(inputPaths);
			geometryExtra->SetOutputs(outputPaths);
			pContext->m_buildScheduler.AddBuildTransform(geometryExtra.release(), pContext);
		}
	}
}
