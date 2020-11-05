/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/libs/bigstreamwriter/ndi-bo-writer.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/libs/libdb2/db2-actor.h"

//--------------------------------------------------------------------------------------------------------------------//
void BuildModuleSkeleton_Configure(const BuildTransformContext *const pContext, 
								const libdb2::Actor *const pDbActor, 
								const std::vector<const libdb2::Actor*>& actorList, 
								std::vector<std::string>& arrBoFiles);

//--------------------------------------------------------------------------------------------------------------------//
class BuildTransform_ExportSkel : public BuildTransform
{
public:
	BuildTransform_ExportSkel(
		const BuildTransformContext* pContext,
		const std::string& actorFullNameNoLod,
		const std::string& skelExportNamespace,
		const std::string& skelExportSet) :
		BuildTransform("ExportSkel", pContext),
		m_skelExportNamespace(skelExportNamespace),
		m_skelExportSet(skelExportSet)
	{
		PopulatePreEvalDependencies();
	}

	void PopulatePreEvalDependencies();
	virtual BuildTransformStatus Evaluate() override;

	//--------------------------------------------------------------------------------------------------------------------//
	virtual BuildTransformStatus ResumeEvaluation(const SchedulerResumeItem& resumeItem) override
	{
		const BuildFile& inputSceneFile = GetFirstInputFile();
		const BuildPath& skelExportFilename = GetFirstOutputPath();

		// Extract references found while exporting the Maya file
		const std::vector<std::pair<std::string, int>> loadedReferences = ExtractLoadedReferencesFromMaya3ndbOutput(m_pContext->m_buildScheduler, m_jobId);

		// add any referenced maya files discovered during maya export
		const std::string& fsRoot = m_pContext->m_toolParams.m_fsRoot;
		for (const auto& ref : loadedReferences)
		{
			const BuildPath refPath(fsRoot + FileIO::separator + ref.first);
			RegisterDiscoveredDependency(refPath, ref.second);
		}

		return BuildTransformStatus::kOutputsUpdated;
	}

	std::string m_skeletonName;				// Name of the actor (or subactor) that contained this skeleton
	std::string m_skeletonFullName;			// TO BE REMOVED - Used by Maya3Ndb to load the XML files

	const std::string m_actorFullNameNoLod;
	const std::string m_skelExportNamespace;
	const std::string m_skelExportSet;
	FarmJobId m_jobId;
};

//--------------------------------------------------------------------------------------------------------------------//
class BuildTransform_BuildSkel : public BuildTransform
{
public:
	BuildTransform_BuildSkel(const BuildTransformContext* pContext, const std::string& skelSceneFile, const std::string& skelSet, const std::string& skelFullName) :
		BuildTransform("BuildSkel", pContext),
		m_skelSceneFile(skelSceneFile),
		m_skelSet(skelSet),
		m_skelFullName(skelFullName)
	{
		PopulatePreEvalDependencies();
	}

	virtual BuildTransformStatus Evaluate() override
	{
		const bool success = Build();
		if (success)
			return BuildTransformStatus::kOutputsUpdated;
		else
			return BuildTransformStatus::kFailed;
	}

private:
	void PopulatePreEvalDependencies();

	bool BuildSkelBo(const ToolParams &tool,
		BigStreamWriter& streamWriter,
		std::string& skelCppSourceCode,
		const BuildFile &skelNdbFile,
		const BuildFile &rigInfoFile,
		const BuildPath& skelBoFilename) const;

	bool BuildFlipData(const ToolParams &tool,
		BigStreamWriter& streamWriter,
		const BuildFile &rigInfoFilename) const;

	bool Build()
	{
		BigStreamWriterConfig streamCfg(m_pContext->m_toolParams.m_streamConfig);
		streamCfg.m_useIncrementalTags = true;

		const BuildFile& skelNdbFile = GetInputFile("SkelNdb");
		const BuildFile& rigInfoFile = GetInputFile("RigInfo");

		const BuildPath& skelBoFilename = GetOutputPath("SkelBo");			// .bo file for the skeleton generated in the Build step

		std::string skelCppSourceCode;
		BigStreamWriter streamWriter(streamCfg);
		bool success = BuildSkelBo(
			m_pContext->m_toolParams,
			streamWriter,
			skelCppSourceCode,
			skelNdbFile,
			rigInfoFile,
			skelBoFilename);

		if (!success)
			return false;

		success = BuildFlipData(m_pContext->m_toolParams, streamWriter, rigInfoFile);
		if (!success)
			return false;

		NdiBoWriter boWriter(streamWriter);
		boWriter.Write();

		DataStore::WriteData(skelBoFilename, boWriter.GetMemoryStream());

		if (HasOutput("SkelCpp"))
		{
			const BuildPath& skelCppFilename = GetOutputPath("SkelCpp");			// .cpp file for the skeleton generated in the Build step
			DataStore::WriteData(skelCppFilename, skelCppSourceCode);
		}

		return true;
	}

	const std::string m_skelSceneFile;
	const std::string m_skelSet;
	const std::string m_skelFullName;

public:
	std::string m_skeletonName;					// Hack for now
};

//--------------------------------------------------------------------------------------------------------------------//
class BuildTransform_BuildRigInfo : public BuildTransform
{
public:
	BuildTransform_BuildRigInfo(const BuildTransformContext *const pContext
		, const std::string& actorBaseName
		, size_t actorLod) 
		: BuildTransform("BuildRigInfo", pContext)
		, m_pDbActor(nullptr)
		, m_skelSetName("")
	{
		m_pDbActor = libdb2::GetActor(actorBaseName, actorLod);
		if (!m_pDbActor->Loaded() || 
			!m_pDbActor->m_skeleton.Loaded() ||
			!m_pDbActor->m_skeleton.m_sceneFile.size())
		{
//			delete m_pDbActor;
			IABORT("Invalid actor %s\n", actorBaseName.c_str());
		}

		const libdb2::Skeleton *const pSkeleton = &m_pDbActor->m_skeleton;
		m_skelSetName = pSkeleton->m_set.size() ? pSkeleton->m_sceneFile + "." + pSkeleton->m_set : pSkeleton->m_sceneFile;

		const SkeletonId uniqueID = ComputeSkelId(m_skelSetName);
		m_preEvaluateDependencies.SetInt("skelID", uniqueID.GetValue());
	}

	BuildTransform_BuildRigInfo(const BuildTransformContext* pContext, const std::string& skelSetName) :
		BuildTransform("BuildRigInfo", pContext)
		, m_pDbActor(nullptr)
		, m_skelSetName(skelSetName)
	{
		const SkeletonId uniqueID = ComputeSkelId(m_skelSetName);
		m_preEvaluateDependencies.SetInt("skelID", uniqueID.GetValue());
	}

	virtual BuildTransformStatus Evaluate() override
	{
		const bool success = Build();
		if (success)
			return BuildTransformStatus::kOutputsUpdated;
		else
			return BuildTransformStatus::kFailed;
	}

private:
	bool BuildRigInfo(const BuildFile& skelNdbFile,
					const BuildPath& rigInfoFilename,
					DataHash& dataHash) const;

	bool Build()
	{
		BigStreamWriterConfig streamCfg(m_pContext->m_toolParams.m_streamConfig);
		streamCfg.m_useIncrementalTags = true;

		const BuildFile& skelNdbFile = GetInputFile("SkelNdb");
		const BuildPath& rigInfoFilename = GetOutputPath("RigInfo");		// Ndb for the RigInfo generated in the Build step

		// Build the rig info
		DataHash rigInfoDataHash;
		const bool success = BuildRigInfo(skelNdbFile, rigInfoFilename, rigInfoDataHash);

		if (!success)
		{
			return false;
		}

		return true;
	}

	const libdb2::Actor* m_pDbActor;
	std::string m_skelSetName;

public:
	std::string m_skeletonName;					// Hack for now
};

//--------------------------------------------------------------------------------------------------------------------//
template<typename T> inline std::string ToString(const T&val)
{
	std::stringstream strm;
	strm << val;
	return strm.str();
}


