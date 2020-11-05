/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/stdafx.h"
#include "tools/pipeline3/build/build-transforms/build-transform-collision.h"

#include "tools/libs/bigstreamwriter/ndi-bo-writer.h"
#include "tools/libs/toolsutil/dependencies.h"
#include "tools/libs/toolsutil/simpledb.h"
#include "tools/pipeline3/common/4_gameflags.h"
#include "tools/bamutils/common.h"
#include "tools/pipeline3/build/actor/ba_havok.h"
#include "tools/pipeline3/build/tool-params.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/build/util/spawn-helper.h"
#include "tools/pipeline3/toolversion.h"

#include "icelib/itscene/ourstudiopolicy.h"

//#pragma optimize("", off) // uncomment when debugging in release mode

/// --------------------------------------------------------------------------------------------------------------- ///
using std::vector;

using toolsutils::SimpleDependency;
using namespace toolsutils;


extern std::string ExtraMaya3NDBOptions(const ToolParams& tool);

/// --------------------------------------------------------------------------------------------------------------- ///
// Collision
/// --------------------------------------------------------------------------------------------------------------- ///
bool Geo3Bo_Collision(const ToolParams& tool,
					const std::string& actorName, 
					const std::string& gameflagsStr, 
					const std::string& skelSetName, 
					const std::string& skelSceneFile, 
					bool geoGenerateFeaturesOnAllSides, 
					bool geoPrerollHavokSimulation, 
					const BuildPath& strCollBo, 
					const BuildPath& collisionToolPath, 
					const BuildFile& rigInfoFile, 
					ITSCENE::SceneDb* scene // modified by ExportCollision 
	)
{
	BigStudioPolicy aOurStudioPolicy;
	AutoPushPopValue<StudioPolicy*> auto_g_theStudioPolicy(g_theStudioPolicy, &aOurStudioPolicy);

	std::string skelname = skelSceneFile;
	if (skelSetName.size())
	{
		skelname += "." + skelSetName;
	}

	const SkeletonId skelID = ComputeSkelId(skelname);
	INOTE_VERBOSE("          compat skel id = 0x%08X (%s)", skelID.GetValue(), skelname.c_str());

	NdbStream rigInfoStream;
	DataStore::ReadData(rigInfoFile, rigInfoStream);

	BuildPipeline::RigInfo rigInfo;
	rigInfo.NdbSerialize(rigInfoStream);
	const BuildPipeline::RigInfo *pRigInfo = &rigInfo;

	ExportCollision(tool, 
					actorName, 
					gameflagsStr, 
					geoGenerateFeaturesOnAllSides, 
					geoPrerollHavokSimulation, 
					scene, 
					pRigInfo, 
					skelID, 
					strCollBo, 
					collisionToolPath);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_ExportCollision : public BuildTransform
{
public:
	enum InputIndex
	{
		SceneFilename = 0,
		SkelSceneFilename = 1,
		InputCount
	};

	enum OutputIndex
	{
		NdbFilename = 0,
		OutputCount
	};

	BuildTransform_ExportCollision(const CollisionExportItem& exportItem, const BuildTransformContext *const pContext) 
								: BuildTransform("ExportCollision", pContext)
								, m_exportItem(exportItem)
	{

		if (m_pContext->m_toolParams.m_baseMayaVer.size())
		{
			m_preEvaluateDependencies.SetString("mayaVersion", m_pContext->m_toolParams.m_baseMayaVer);
		}

		m_preEvaluateDependencies.SetString("exportNdb_skelSet", m_exportItem.m_skelSet);
		m_preEvaluateDependencies.SetString("exportNdb_geoCollSet",  m_exportItem.m_geoCollSet);
	}

	virtual BuildTransformStatus Evaluate() override
	{
		const ToolParams& tool = m_pContext->m_toolParams;

		FileIO::makeDirPath(m_exportItem.m_collBuildDir.c_str());


		//DisplayInfoBright("          %s\n", geo.Name().c_str());
		//INOTE_VERBOSE("    Collision : %s %s", m_pCollisionItem->m_actorName.c_str(), sceneFile.c_str());

		Dependencies::Clear(m_exportItem.m_geoName, "actor", "collision");

		const BuildFile& sceneFileInput = GetInputs()[InputIndex::SceneFilename].m_file;
		const string sceneFile = sceneFileInput.AsRelativePath();

		std::string options = sceneFile;
		options += " -coll";
		if (!m_exportItem.m_skelSet.empty())
		{
			options += " -skelset " + m_exportItem.m_skelSet;
		}
		options += " -collset " + m_exportItem.m_geoCollSet;
		options += " -output " + GetOutputs()[OutputIndex::NdbFilename].m_path.AsAbsolutePath();
		options += " -user " + tool.m_userName;

		//Nate
		std::string dbPath = GetGlobalDbPath(tool.m_host);
		options += " -dbpath " + dbPath;
		options += " -xmlfile " + m_exportItem.m_geoFullName;

		options += ExtraMaya3NDBOptions(tool);

		options += " -verbose ";

		IASSERT(!tool.m_executablesPath.empty());
		const std::string exePath = tool.m_executablesPath + "/maya3ndb.app/" + "maya3ndb.exe";

		JobDescription job(exePath, options, tool.m_localtools, tool.m_local, tool.m_x64maya);
		job.m_useSetCmdLine = true;

		Farm& farm = m_pContext->m_buildScheduler.GetFarmSession();
		m_exportItem.m_jobId = farm.submitJob(job.GetCommand(), 800ULL * 1024ULL * 1024ULL, 1);  //default on the farm agent is 400 megs
		Dependencies::Add(m_exportItem.m_geoName, "actor", sceneFile, "collision");

		if (m_exportItem.m_jobId != FarmJobId::kInvalidFarmjobId)
		{
			m_pContext->m_buildScheduler.RegisterFarmWaitItem(this, m_exportItem.m_jobId);
			return BuildTransformStatus::kResumeNeeded;
		}
		else
		{
			return BuildTransformStatus::kOutputsUpdated;
		}
	}

	virtual BuildTransformStatus ResumeEvaluation(const SchedulerResumeItem& resumeItem) override
	{
		const std::vector<std::pair<std::string, int>> loadedReferences = ExtractLoadedReferencesFromMaya3ndbOutput(m_pContext->m_buildScheduler, m_exportItem.m_jobId);

		// add any referenced maya files discovered during maya export
		const std::string& fsRoot = m_pContext->m_toolParams.m_fsRoot;
		for (const auto& ref : loadedReferences)
		{
			const BuildPath refPath(fsRoot + FileIO::separator + ref.first);
			RegisterDiscoveredDependency(refPath, ref.second);
		}

		return BuildTransformStatus::kOutputsUpdated;
	}

	CollisionExportItem m_exportItem;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_BuildCollision : public BuildTransform
{
public:
	enum InputIndex
	{
		CollisionNdbFilename = 0,
		RigInfoFilename = 1,
		UserFeaturesFilename = 2,
		InputCount
	};

	enum OutputIndex
	{
		CollisionBoFilename = 0,
		AutoFeaturesFilename = 1,
		CollisionToolFilename = 2,
		OutputCount
	};


	BuildTransform_BuildCollision(const CollisionBuildItem& buildItem
								, const BuildTransformContext *const pContext) 
								: BuildTransform("BuildCollision", pContext)
								, m_buildItem(buildItem)
	{
		PopulatePreEvalDependencies();
	}

	void PopulatePreEvalDependencies()
	{
		//All of those variables are used in ExportCollision(...) so they need to be in the dependencies.
		m_preEvaluateDependencies.SetString("skeletonSet", m_buildItem.m_skelSet);
		m_preEvaluateDependencies.SetString("skeletonScene", m_buildItem.m_skelSceneFile);

		m_preEvaluateDependencies.SetBool("generateFeaturesOnAllSides", m_buildItem.m_geoGenerateFeaturesOnAllSides);
		m_preEvaluateDependencies.SetBool("prerollHavokSimulation", m_buildItem.m_geoPrerollHavokSimulation);

		// Check collision tolerance change in game flags
		Vector collisionScaleTolerance = GetActorCollisionTolerance(m_buildItem.m_gameFlagsStr);
		m_preEvaluateDependencies.SetFloat("collisionScaleTolerance0", collisionScaleTolerance.X());
		m_preEvaluateDependencies.SetFloat("collisionScaleTolerance1", collisionScaleTolerance.Y());
		m_preEvaluateDependencies.SetFloat("collisionScaleTolerance2", collisionScaleTolerance.Z());

		m_preEvaluateDependencies.SetString("autoFeaturesVersion", BUILD_TRANSFORM_AUTO_FEATURES_VERSION);
	}

	BuildTransformStatus Evaluate() override
	{
 		const ToolParams& tool = m_pContext->m_toolParams;
		const BuildFile& collNdbFile = GetInputs()[InputIndex::CollisionNdbFilename].m_file;
		const BuildFile& rigInfoFile = GetInputs()[InputIndex::RigInfoFilename].m_file;

		const BuildPath& collBoFilename = GetOutputs()[OutputIndex::CollisionBoFilename].m_path;
		const BuildPath& collToolFilename = GetOutputs()[OutputIndex::CollisionToolFilename].m_path;
		FileIO::makeDirPath(m_buildItem.m_collBoBuildDir.c_str());

		// Load scene NDB
		NdbStream collisionNdbStream;
		DataStore::ReadData(collNdbFile, collisionNdbStream);

		ITSCENE::SceneDb *pCollisionScene = new ITSCENE::SceneDb();
		if (!LoadSceneNdb(collisionNdbStream, pCollisionScene))
		{
			IERR("Failed to load the SceneDb from NdbStream for %s [%s].", collNdbFile.AsPrefixedPath().c_str(), collNdbFile.GetContentHash().AsText().c_str());
			return BuildTransformStatus::kFailed;
		}

		//////////////////////////////////////////////////////////////////////////////////////////////
		// GENERATE THE COLLISION ////////////////////////////////////////////////////////////////////
		bool boSuccess = Geo3Bo_Collision(tool, 
										m_buildItem.m_actorName, 
										m_buildItem.m_gameFlagsStr, 
										m_buildItem.m_skelSet, 
										m_buildItem.m_skelSceneFile, 
										m_buildItem.m_geoGenerateFeaturesOnAllSides, 
										m_buildItem.m_geoPrerollHavokSimulation, 
										collBoFilename, 
										collToolFilename, 
										rigInfoFile, 
										pCollisionScene);
		//////////////////////////////////////////////////////////////////////////////////////////////

		return BuildTransformStatus::kOutputsUpdated;

	}

	CollisionBuildItem m_buildItem;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildModuleCollision_Configure(const BuildTransformContext *const pContext,
									const libdb2::Actor *const pDbActor,
									const std::vector<const libdb2::Actor*>& actorList,
									std::vector<std::string>& outArrBoFiles,
									std::vector<BuildPath>& outArrCollisionToolPath)
{
	bool hasCollision = false;
	const ToolParams& tool = pContext->m_toolParams;

	AggregateCollisionData collisionData;
	std::vector<TransformInput> inputs;
	std::vector<TransformOutput> outputs;
	for (const libdb2::Actor *dbactor : actorList)
	{
		const libdb2::Geometry &geo = dbactor->m_geometry;
		const std::string& sceneFile = geo.m_collisionSceneFile.empty() ? geo.m_sceneFile : geo.m_collisionSceneFile;

		// early out if this actor has no collision configured.
		if (dbactor->m_lodIndex != 0 || 
			geo.m_collisionSet.empty() || 
			sceneFile.empty() || 
			!geo.Loaded())
		{
			continue;
		}

		hasCollision = true;
		INOTE_VERBOSE("%s - collision: %s", dbactor->Name().c_str(), sceneFile.c_str());

		// XXXdsmith Is this still supposed to be here, or will the build scheduler handle it?
		if (!FileIO::fileExists(sceneFile.c_str()))
		{
			Dependencies::Clear(geo.Name(), "actor", "collision");
			IABORT("'%s' maya collision file is missing", sceneFile.c_str());
		}

		CollisionExportItem exportItem;
		exportItem.m_geoName = dbactor->m_geometry.Name();
		exportItem.m_geoCollSet = dbactor->m_geometry.m_collisionSet;
		exportItem.m_geoFullName = dbactor->m_geometry.FullName();
		exportItem.m_skelSet = dbactor->m_skeleton.m_set;
		exportItem.m_collBuildDir = tool.m_buildPathColl + dbactor->FullNameNoLod() + FileIO::separator;

		const std::string collisionNdbFilename = exportItem.m_collBuildDir + MakeColNdbFilename(geo.m_collisionSet);

		// Add the .ma -> .ndb build transform
		inputs.resize(BuildTransform_ExportCollision::InputCount);
		outputs.resize(BuildTransform_ExportCollision::OutputCount);

		inputs[BuildTransform_ExportCollision::SceneFilename] = TransformInput(PathPrefix::BAM + sceneFile);
		inputs[BuildTransform_ExportCollision::SkelSceneFilename] = TransformInput(PathPrefix::BAM + dbactor->m_skeleton.m_sceneFile);
		
		outputs[BuildTransform_ExportCollision::NdbFilename] = TransformOutput(collisionNdbFilename);
		
		BuildTransform_ExportCollision *const pExportXform = new BuildTransform_ExportCollision(exportItem, pContext);
		pExportXform->SetInputs(inputs);
		pExportXform->SetOutputs(outputs);
		pContext->m_buildScheduler.AddBuildTransform(pExportXform, pContext);

		CollisionBuildItem buildItem;
		buildItem.m_skelSet = dbactor->m_skeleton.m_set;
		buildItem.m_skelSceneFile = dbactor->m_skeleton.m_sceneFile;
		buildItem.m_geoGenerateFeaturesOnAllSides = dbactor->m_geometry.m_generateFeaturesOnAllSides;
		buildItem.m_geoPrerollHavokSimulation = dbactor->m_geometry.m_prerollHavokSimulation;
		buildItem.m_gameFlagsStr = dbactor->m_gameFlagsInfo.m_gameFlags;
		buildItem.m_actorName = dbactor->Name();
		// Gather file names for the collision export (maya3ndb) and collision build (ndb to BO file)
		// collision ndb output dir, e.g. X:\build\ndi-farm\t2\build\main\common\coll5\some\path
		buildItem.m_collBoBuildDir = tool.m_buildPathCollBo + dbactor->FullNameNoLod() + FileIO::separator;

		// Add the .ndb -> .bo build transform
		inputs.clear();
		inputs.push_back(TransformInput(collisionNdbFilename));
		inputs.push_back(TransformInput(tool.m_buildPathRigInfo + dbactor->FullNameNoLod() + FileIO::separator + "riginfo.ndb"));
		
		const string geomFile = EmitFeatures::EmitFeatureDb::GetUserFilename(EmitFeatures::kAssetTypeForeground, dbactor->Name());
		if (FileIO::fileExists(geomFile.c_str()))
		{
			inputs.push_back(TransformInput(geomFile));
		}

		inputs.push_back(TransformInput(toolsutils::GetDbPath() + std::string("/data/db/feature_builder_settings.json"), "SettingsXml"));

		outputs.resize(BuildTransform_BuildCollision::OutputCount);

		const std::string collBoFilename = buildItem.m_collBoBuildDir + "coll.bo";

		const TransformOutput collisionToolPath = TransformOutput(tool.m_buildPathCollBo + dbactor->Name() + ".collision.bo.tc", "toolCollision", TransformOutput::kReplicate); // Replicate for: nav meshes loading collision data for static nav blockers

		TransformOutput outAutoFeaturesPath;
		MakeAutoFeaturesPath(EmitFeatures::kAssetTypeForeground, dbactor->Name(), outAutoFeaturesPath);

		outputs[BuildTransform_BuildCollision::CollisionBoFilename] = TransformOutput(collBoFilename);
		outputs[BuildTransform_BuildCollision::CollisionToolFilename] = collisionToolPath;
		outputs[BuildTransform_BuildCollision::AutoFeaturesFilename] = outAutoFeaturesPath;

		BuildTransform_BuildCollision *const pBuildXform = new BuildTransform_BuildCollision(buildItem, pContext);
		pBuildXform->SetInputs(inputs);
		pBuildXform->SetOutputs(outputs);

		pContext->m_buildScheduler.AddBuildTransform(pBuildXform, pContext);

		outArrCollisionToolPath.push_back(collisionToolPath.m_path);
		outArrBoFiles.push_back(collBoFilename);
	}

	if (!hasCollision)
	{
		INOTE_VERBOSE("No collision in this actor\n");
	} 
}
