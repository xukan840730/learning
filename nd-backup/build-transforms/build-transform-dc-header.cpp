/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/build-transforms/build-transform-dc-header.h"

#include "tools/libs/racketlib/util.h"

#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/common/blobs/data-store.h"

// #pragma optimize("", off) // uncomment when debugging in release mode

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_DcHeader::BuildTransform_DcHeader(const DcoConfig& dcoConfig,
												 const DcoBuildHeaderEntry& entry,
												 const std::string& projectName,
												 const std::string& templateDir,
												 const std::string& snDbsProjectName,
												 const BuildTransformContext* pContext)
	: BuildTransform("DcHeader", pContext)
	, m_dcoConfig(dcoConfig)
	, m_buildEntry(entry)
	, m_rootProjectName(projectName)
	, m_templateDir(templateDir)
	, m_snDbsProjectName(snDbsProjectName)
{
	m_preEvaluateDependencies.SetString("dcoDepFileVersion", DcoDepFile::kDependencyFileVersion);
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_DcHeader::Evaluate()
{
	int ret = -1;
	const std::string& precompileOutput = RacketPrecompileThreadSafe("", &ret);

	if (ret != 0)
	{
		IABORT("Pre compilation failed!\n%s", precompileOutput.c_str());
		return BuildTransformStatus::kFailed;
	}

	const std::string cmdLine = CreateHeaderBuildCommand(m_dcoConfig, m_rootProjectName, m_buildEntry);

	SnDbs::JobParams job;
	job.m_id = m_buildEntry.m_moduleName;
	job.m_command = cmdLine;
	job.m_title = m_buildEntry.m_moduleName + ".h";
	job.m_local = false;

	INOTE_VERBOSE("Adding sn-dbs job '%s'\n", job.m_title.c_str());

	if (!SnDbs::AddJobToProject(m_snDbsProjectName, job))
	{
		IABORT("Failed to add job '%s' to SN-DBS project '%s'\n", job.m_title.c_str(), m_snDbsProjectName.c_str());
	}

	m_pContext->m_buildScheduler.RegisterSnDbsWaitItem(this, m_snDbsProjectName, m_buildEntry.m_moduleName);

	return BuildTransformStatus::kResumeNeeded;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_DcHeader::ResumeEvaluation(const SchedulerResumeItem& resumeItem)
{
	const SnDbs::JobResult& res = resumeItem.m_snDbsResult;

	if (res.m_status != SnDbs::kSucceeded)
	{
		IERR("SnDbs job '%s' failed: %s\n", res.m_id.c_str(), res.m_failReason.c_str());
		IERR("  command: %s\n", res.m_command.c_str());
		IERR("  host: %s (%s) [%s]\n", res.m_hostName.c_str(), res.m_hostIp.c_str(), res.m_where.c_str());
		IERR("%s\n", res.m_stdErr.c_str());

		return BuildTransformStatus::kFailed;
	}

	if (!OnBuildComplete())
	{
		return BuildTransformStatus::kFailed;
	}

	return BuildTransformStatus::kOutputsUpdated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool BuildTransform_DcHeader::OnBuildComplete()
{
	const BuildPath headerPath = GetOutputPath("HeaderPath");

	DataStore::UploadFile(m_buildEntry.m_outputPath, headerPath, nullptr, DataStorage::kAllowAsyncUpload);

	// Register all discovered dependencies
	RegisterDcxDependencies();

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform_DcHeader::RegisterDcxDependencies()
{
	const TransformInput dcxInput = GetInput("DcxPath");
	const std::string dcxPath = dcxInput.m_file.AsAbsolutePath();

	std::vector<DcoSubDepEntry> deps;
	
	const std::string srcBasePath = PathConverter::ToAbsolutePath(PathPrefix::SRCCODE);
	const std::string makeHdrPath = srcBasePath + m_dcoConfig.m_dcCollectsDir + "dc/make-header.rkt";

	GatherDcxPhysicalDependencies(m_dcoConfig, m_rootProjectName, makeHdrPath, 1, deps);

	GatherDcxPhysicalDependencies(m_dcoConfig, m_rootProjectName, dcxPath, 0, deps);

	for (const DcoSubDepEntry& entry : deps)
	{
		RegisterDiscoveredDependency(BuildPath(entry.m_modulePath), entry.m_depth - 1);
	}
}
