/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/stdafx.h"

#include "tools/pipeline3/build/build-transforms/build-transform-geometry-export.h"

#include "tools/libs/chartergeomcache/geomcache.h"
#include "tools/libs/toolsutil/json_helpers.h"
#include "tools/pipeline3/build/build-transforms/build-transform-chartergeom.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"

#include "3rdparty/rapidjson/include/rapidjson/document.h"

//#pragma optimize("", off) // uncomment when debugging in release mode


using CharterGeomCache::Geom;
using toolsutils::SimpleDependency;
extern void CheckForCtrlC();
extern std::string ExtraMaya3NDBOptions(const ToolParams& tool);

class GeometryExportTransform : public BuildTransform
{
private:
	std::string m_gameflagsStr;
	size_t m_lodIndex;
	std::string m_geoName;
	std::string m_geoFullName;
	std::string m_geoSet;
	std::string m_geoSceneFile;
	std::string m_skelSet;

public:
	SimpleDependency m_oldDep;
	std::vector<std::string> m_actorMismatches;
	FarmJobId m_maya3NdbJobId;

	// Input
	std::string m_sceneInputFile;			// Scene used to export the skeleton

	// Export Output / Build Input
	std::string m_geoNdbExportFilename;		// Ndb containing the exported data from the Maya scene

	GeometryExportTransform(const BuildTransformContext *const pContext
							, std::string gameflagsStr
							, size_t lodIndex
							, const std::string& geoName
							, const std::string& geoFullName
							, const std::string& geoSet
							, const std::string& geoSceneFile
							, const std::string& skelSet)
							: BuildTransform("ExportGeometry", pContext)
							, m_gameflagsStr(gameflagsStr)
							, m_lodIndex(lodIndex)
							, m_geoName(geoName)
							, m_geoFullName(geoFullName)
							, m_geoSet(geoSet)
							, m_geoSceneFile(geoSceneFile)
							, m_skelSet(skelSet)
							, m_maya3NdbJobId(0)
	{
		PopulatePreEvalDependencies();
	}

	void PopulatePreEvalDependencies() 
	{
		if (m_pContext->m_toolParams.m_baseMayaVer.size())
			m_preEvaluateDependencies.SetString("mayaVersion", m_pContext->m_toolParams.m_baseMayaVer);

		// WE MAY BE STILL MISSING IMPLIED DEPENDECIES FROM THE XML FILE WHEN MAYA SCRIPTS
		// INFER EXTERNAL CONFIG FILES THAT COULD CHANGE
		m_preEvaluateDependencies.SetString("exportNdb_geometrySet", m_geoSet);
		m_preEvaluateDependencies.SetString("exportNdb_skelSet", m_skelSet);
		m_preEvaluateDependencies.SetString("exportNdb_lodIndex", std::to_string(m_lodIndex));
		m_preEvaluateDependencies.SetString("exportNdb_geoFullName", m_geoFullName);
		m_preEvaluateDependencies.SetString("exportNdb_m_geoSceneFile", m_geoSceneFile);



		const std::string& gameFlags = m_gameflagsStr;
		if (!gameFlags.empty())
		{
			rapidjson::Document doc;
			toolsutils::json_helpers::LoadJsonDocument(doc, "gameflags", gameFlags.data(), gameFlags.data() + gameFlags.size());
			const rapidjson::Value& lodSettingsValue = toolsutils::json_helpers::TryGetValue(doc, "simplygonLodSettings");

			if (lodSettingsValue != toolsutils::json_helpers::s_nullValue)
			{
				const string simplygonFilename = toolsutils::json_helpers::makeString(lodSettingsValue);
				m_preEvaluateDependencies.SetString("gameflags_simplygonLodSettings", simplygonFilename);
				string simplygonAbsoluteFilename = "y:/" + m_pContext->m_toolParams.m_gameName + "/art/general/db/simplygon8/loddb/" + simplygonFilename + ".spl";
				RegisterDiscoveredDependency(simplygonAbsoluteFilename, 0);
			}
		}
	}


	virtual BuildTransformStatus Evaluate() override
	{
		const auto & tool = m_pContext->m_toolParams;

			// Get output filename for maya scene geo, e.g.: Z:/big4/build/ps4/main/geo2/art/objects/hero/skeleton/master_deformation_rig.ma.hero_simple.geo.ndb
		std::string geoNdbFilename = tool.m_buildPathGeo + toolsutils::MakeGeoNdbFilename(m_geoSceneFile, m_geoSet);

		std::string info = m_geoName + (m_lodIndex > 0 ? std::to_string(m_lodIndex) : std::string(""));
//			INOTE_VERBOSE("    Geo : %s %s", geo.FullName().c_str(), geo.m_sceneFile.c_str());
		std::string options = m_geoSceneFile;
		if (m_geoSet != "")
		{
			if (m_skelSet.size())
				options += " -skelset " + m_skelSet;
			options += " -geoset "	+ m_geoSet;
		}
		options += " -output "		+ geoNdbFilename;
		options += " -geo";
		options += " -user "		+ tool.m_userName;
		options += " -dbpath "		+ toolsutils::GetGlobalDbPath(tool.m_host);
		options += " -xmlfile "		+ m_geoFullName;
		options += " -lodindex "	+ std::to_string(m_lodIndex);

		options += ExtraMaya3NDBOptions(tool);

		IASSERT(!tool.m_executablesPath.empty());
		const std::string exePath = tool.m_executablesPath + "/maya3ndb.app/" + "maya3ndb.exe";

		JobDescription job(exePath, options, tool.m_localtools, tool.m_local, tool.m_x64maya);
		job.m_useSetCmdLine = true;

		m_maya3NdbJobId = m_pContext->m_buildScheduler.GetFarmSession().submitJob(job.GetCommand(), 800 * 1024 * 1024, 1);
		m_pContext->m_buildScheduler.RegisterFarmWaitItem(this, m_maya3NdbJobId);

		return BuildTransformStatus::kResumeNeeded;
	}

	virtual BuildTransformStatus ResumeEvaluation(const SchedulerResumeItem& resumeItem) override
	{
		if (!resumeItem.m_farmJob->m_exitcode)
		{
			const std::vector<std::pair<std::string, int>> loadedReferences = ExtractLoadedReferencesFromMaya3ndbOutput(m_pContext->m_buildScheduler, m_maya3NdbJobId);

			// Add the loaded references
			const std::string& fsRoot = m_pContext->m_toolParams.m_fsRoot;
			for (const auto& ref : loadedReferences)
			{
				const BuildPath refPath(fsRoot + FileIO::separator + ref.first);
				RegisterDiscoveredDependency(refPath, ref.second);
			}

			return BuildTransformStatus::kOutputsUpdated;
		}
		else
		{
			return BuildTransformStatus::kFailed;
		}
	}
};

TransformOutput GeometryExportTransform_Configure(const BuildTransformContext *const pContext,
												const libdb2::Actor *const dbactor)
{
	std::string inputFilename = PathPrefix::BAM + dbactor->m_geometry.m_sceneFile;
	std::string exportFileName = pContext->m_toolParams.m_buildPathGeo + toolsutils::MakeGeoNdbFilename(dbactor->m_geometry.m_sceneFile, dbactor->m_geometry.m_set);
	ITFILE::FilePath::CleanPath(exportFileName);

	std::vector<TransformInput> inputs;
	inputs.push_back(TransformInput(BuildPath(inputFilename)));
	
	TransformOutput outputFile = TransformOutput(BuildPath(exportFileName), "GeoNdb");
	
	GeometryExportTransform *const pGeoExport = new GeometryExportTransform(pContext,
																			dbactor->m_gameFlagsInfo.m_gameFlags,
																			dbactor->m_lodIndex,
																			dbactor->m_geometry.Name(),
																			dbactor->m_geometry.FullName(),
																			dbactor->m_geometry.m_set,
																			dbactor->m_geometry.m_sceneFile,
																			dbactor->m_skeleton.m_set);
	pGeoExport->SetInputs(inputs);
	pGeoExport->SetOutput(outputFile);
	pContext->m_buildScheduler.AddBuildTransform(pGeoExport, pContext);

	return outputFile;
}

void BuildModuleGeometryExport_Configure(const BuildTransformContext* pContext, const std::vector<const libdb2::Actor*>& actorList)
{
	// Validate the actors
	bool abort = false;
	for (size_t iactor = 0; iactor < actorList.size(); ++iactor)
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

	for (size_t actorLoop = 0; actorLoop < actorList.size(); actorLoop++)
	{
		CheckForCtrlC();
		const libdb2::Actor *dbactor = actorList[actorLoop];
		const libdb2::Geometry &geo = dbactor->m_geometry;
		if (geo.Loaded() && geo.m_sceneFile.size())
		{
			// Get output filename for maya scene geo, e.g.: Z:/big4/build/ps4/main/geo2/art/objects/hero/skeleton/master_deformation_rig.ma.hero_simple.geo.ndb
			TransformOutput geoNdb = GeometryExportTransform_Configure(pContext, dbactor);

			//only generate the charter geometry for the first lod.
			if (dbactor->m_lodIndex == 0)
			{
				//create charter geometry transform
				std::vector<TransformInput> inputs;
				inputs.push_back(TransformInput(geoNdb));

				string geometryFilename = Geom::GetCacheFilename(dbactor->Name().c_str(), Geom::Source::SOURCE_ACTOR);
				std::vector<TransformOutput> outputs;
				outputs.push_back(TransformOutput(Geom::GetCacheFilename(dbactor->Name().c_str(), Geom::Source::SOURCE_ACTOR), "CharterGeo"));

				BuildTransform_CharterGeom* charterGeometry = new BuildTransform_CharterGeom(pContext, dbactor->Name().c_str(), dbactor->m_geometry.m_set.c_str());
				charterGeometry->SetInputs(inputs);
				charterGeometry->SetOutputs(outputs);
				pContext->m_buildScheduler.AddBuildTransform(charterGeometry, pContext);
			}
		}
	}

}
