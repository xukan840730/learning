/*
 * Copyright (c) 2018 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/build-transforms/build-transform-dc-bin.h"

#include "common/util/stringutil.h"

#include "tools/libs/racketlib/util.h"
#include "tools/libs/toolsutil/command-job.h"
#include "tools/libs/toolsutil/filename.h"
#include "tools/libs/toolsutil/sndbs.h"
#include "tools/libs/toolsutil/temp-files.h"

#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-dco.h"
#include "tools/pipeline3/build/build-transforms/build-transform-upload-file.h"
#include "tools/pipeline3/common/blobs/blob-cache.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/path-converter.h"

#define LIBXML_STATIC
#include "tools/libs/libxml2/include/libxml/parser.h"

#include <fstream>

// #pragma optimize("", off) // uncomment when debugging in release mode

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_DcBin::BuildTransform_DcBin(const DcoConfig& dcoConfig,
										   const DcoBuildEntry& entry,
										   const std::string& projectName,
										   const std::string& templateDir,
										   const std::string& snDbsProjectName,
										   const BuildTransformContext* pContext)
	: BuildTransform("DcBin", pContext)
	, m_dcoConfig(dcoConfig)
	, m_buildEntry(entry)
	, m_rootProjectName(projectName)
	, m_templateDir(templateDir)
	, m_snDbsProjectName(snDbsProjectName)
{
	m_preEvaluateDependencies.SetString("dcoDepFileVersion", DcoDepFile::kDependencyFileVersion);
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_DcBin::Evaluate()
{
	const BuildPath dcBinPath = GetOutputPath("BinPath");

	int ret = -1;
	const std::string& precompileOutput = RacketPrecompileThreadSafe("", &ret);

	if (ret != 0)
	{
		IABORT("Pre compilation failed!\n%s", precompileOutput.c_str());
		return BuildTransformStatus::kFailed;
	}

	std::string includesListFile;
	if (HasOutput("StartupIncludes"))
	{
		includesListFile = GetOutputPath("StartupIncludes").AsAbsolutePath();
	}

	const std::string cmdLine = CreateBinBuildCommand(m_dcoConfig,
													  m_rootProjectName,
													  m_buildEntry,
													  includesListFile);

	SnDbs::JobParams job;
	job.m_id	  = m_buildEntry.m_moduleName;
	job.m_command = cmdLine;
	job.m_title	  = m_buildEntry.m_moduleName + ".bin";
	job.m_local	  = !m_buildEntry.m_remote;

	INOTE_VERBOSE("Adding sn-dbs job '%s'\n", job.m_title.c_str());
	INOTE_VERBOSE("%s\n", cmdLine.c_str());

	if (!SnDbs::AddJobToProject(m_snDbsProjectName, job))
	{
		IABORT("Failed to add job '%s' to SN-DBS project '%s'\n", job.m_title.c_str(), m_snDbsProjectName.c_str());
	}

	m_pContext->m_buildScheduler.RegisterSnDbsWaitItem(this, m_snDbsProjectName, m_buildEntry.m_moduleName);

	return BuildTransformStatus::kResumeNeeded;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_DcBin::ResumeEvaluation(const SchedulerResumeItem& resumeItem)
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
bool BuildTransform_DcBin::OnBuildComplete()
{
	const BuildPath binPath = GetOutputPath("BinPath");
	const BuildPath dciPath = GetOutputPath("DciPath");
	
	const std::string binUploadSource = GetBinFileOutputPath(m_buildEntry.m_moduleName, m_rootProjectName);
	const std::string dciUploadSource = GetDciFileOutputPath(m_buildEntry.m_moduleName, m_rootProjectName);

	DataStore::UploadFile(binUploadSource, binPath, nullptr, DataStorage::kAllowAsyncUpload);
	DataStore::UploadFile(dciUploadSource, dciPath, nullptr, DataStorage::kAllowCaching | DataStorage::kAllowAsyncUpload);

	if (HasOutput("JsonPath"))
	{
		const BuildPath jsonPath = GetOutputPath("JsonPath");

		const std::string jsonUploadSource = GetLevelSetJsonFileOutputPath(m_buildEntry.m_moduleName, m_rootProjectName);

		if (FileIO::fileExists(jsonUploadSource.c_str()))
		{
			DataStore::UploadFile(jsonUploadSource, jsonPath, nullptr, DataStorage::kAllowAsyncUpload);
		}
		else
		{
			DataStore::WriteData(jsonPath, "");
		}
	}

#if DELETE_OUTPUT_FILES
	// Hack - Clean up after ourselves
	FileIO::deleteFile(binUploadSource.c_str());
	FileIO::deleteFile(dciUploadSource.c_str());
#endif

	// Register all discovered dependencies
	std::vector<DcoSubDepEntry> deps;
	std::set<std::string> startupModules;

	if (HasOutput("StartupIncludes"))
	{
		const BuildPath& includesListPath = GetOutputPath("StartupIncludes");
		const std::string includesListFile = includesListPath.AsAbsolutePath();
		const std::string srcBasePath = PathConverter::ToAbsolutePath(PathPrefix::SRCCODE);

		const std::vector<std::string> includesList = FileIO::ReadAllLines(includesListFile.c_str());

		for (const std::string& startupInclude : includesList)
		{
			if (startupInclude.find("*.dcx") == std::string::npos)
			{
				startupModules.insert(startupInclude);
				continue;
			}

			const std::string wcPath = srcBasePath + startupInclude;

			UpdateDepEntry(wcPath, 1, deps);
		}

		DataStore::UploadFile(includesListFile,
							  includesListPath,
							  nullptr,
							  DataStorage::kAllowCaching | DataStorage::kAllowAsyncUpload);
	}

	RegisterDcxDependencies(deps);

	if (HasOutput("StartupModules"))
	{
		std::vector<DcoBuildEntry> buildEntries;
		
		NdbStream stream;
		DataStore::ReadData(GetInputFile("BuildEntries"), stream);
		NdbRead(stream, "dcoBuildEntries", buildEntries);

		for (const std::string& startupModule : startupModules)
		{
			const DcoBuildEntry* pFoundBuild = FindBuildEntryByName(buildEntries, startupModule);
			IABORT_UNLESS(pFoundBuild, "startup.dcx depends on non-existent startup module entry '%s'\n", startupModule.c_str());
			
			const std::string dcxPath = pFoundBuild->m_sourceBaseDir + pFoundBuild->m_sourcePath;
			GatherDcxPhysicalDependencies(m_dcoConfig, m_rootProjectName, dcxPath, 1, deps);
		}

		const BuildPath startupModulesPath = GetOutputPath("StartupModules");
		const std::string startupModulesFilePath = startupModulesPath.AsAbsolutePath();

		GatherDcoModulesFromDependencies(deps, buildEntries, startupModules);

		INOTE_VERBOSE("Startup Modules:\n");

		std::string txt;
		for (const std::string& moduleName : startupModules)
		{
			const DcoBuildEntry* pFoundBuild = FindBuildEntryByName(buildEntries, moduleName);

			INOTE_VERBOSE("  %s\n", moduleName.c_str());
			IABORT_UNLESS(pFoundBuild, "startup.dcx depends on non-existent module '%s'\n", moduleName.c_str());

			txt += moduleName + "\r\n";
		}
		DataStore::WriteData(startupModulesPath, txt);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform_DcBin::RegisterDcxDependencies(std::vector<DcoSubDepEntry>& deps)
{
	const TransformInput dcxInput = GetInput("DcxPath");
	const std::string dcxPath = dcxInput.m_file.AsAbsolutePath();

	const std::string srcBasePath = PathConverter::ToAbsolutePath(PathPrefix::SRCCODE);
	const std::string makeBinPath = srcBasePath + m_dcoConfig.m_dcCollectsDir + "dc/make-bin.rkt";

	GatherDcxPhysicalDependencies(m_dcoConfig, m_rootProjectName, makeBinPath, 1, deps);

	GatherDcxPhysicalDependencies(m_dcoConfig, m_rootProjectName, dcxPath, 0, deps);

	for (const DcoSubDepEntry& entry : deps)
	{
		RegisterDiscoveredDependency(BuildPath(entry.m_modulePath), entry.m_depth - 1);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_DcModulesBin::BuildTransform_DcModulesBin(const DcoConfig& dcoConfig,
														 const std::string& modulesName,
														 const std::string& sndbsProject,
														 const std::string& projectName,
														 const BuildTransformContext* pContext)
	: BuildTransform("DcModulesBin", pContext)
	, m_dcoConfig(dcoConfig)
	, m_sndbsProject(sndbsProject)
	, m_rootProjectName(projectName)
	, m_modulesName(modulesName)
{
	m_preEvaluateDependencies.SetString("dcoDepFileVersion", DcoDepFile::kDependencyFileVersion);
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_DcModulesBin::Evaluate()
{
	SnDbs::StopProject(m_sndbsProject);

	const std::vector<TransformInput>& inputs = GetInputs();

	std::string dciArgs = " ";
	std::string dciConcat;
	dciConcat.reserve(inputs.size() * 4096);
	for (const TransformInput& input : inputs)
	{
 		const std::string relPath = input.m_file.AsRelativePath();
 		const std::string relDciPath = relPath.substr(relPath.find(FileIO::separator, 4) + 1); /* dc1/<proj-name>/*/
 		dciArgs += relDciPath + " ";

		const std::string moduleName = relDciPath.substr(0, relDciPath.length() - 4 /*.dci */);

		// Hack - We need to download all dci files so that Racket can load them during the modules.bin creation
		const std::string absoluteDciPath = GetDciFileOutputPath(moduleName, m_rootProjectName);
		DataStore::DownloadFile(absoluteDciPath.substr(0, absoluteDciPath.length() - 4) + ".dci", input.m_file);
		
		std::string dciContents;
		DataStore::ReadData(input.m_file, dciContents);
		dciConcat += dciContents + "\n";
	}

	const std::string outputPath = GetBinFileOutputPath(m_modulesName, m_rootProjectName);

	// Hack - write the concatenated dci file
	const std::string dciConcatPath = outputPath.substr(0, outputPath.length() - 4 /* .bin */) + ".dci";
	FILE* fout = fopen(dciConcatPath.c_str(), "wb");
	if (fout == nullptr)
	{
		IERR("Failed to open file \"%s\" for output.", dciConcatPath.c_str());
		return BuildTransformStatus::kFailed;
	}
	size_t written = fwrite(dciConcat.c_str(), 1, dciConcat.length(), fout);
	if (written != dciConcat.length())
	{
		IERR("Failed to write the file \"%s\".", dciConcatPath.c_str());
		return BuildTransformStatus::kFailed;
	}
	fclose(fout);


	// use temp file
 	//std::string tempFile = WriteToTempFile(dciArgs);
 	//dciArgs = std::string("--cmd-line-file ") + tempFile;

	// Use the concatenated dci file
	dciArgs = " " + m_modulesName + ".dci ";

	const std::string cmd = CreateModuleIndexBuildCommand(m_dcoConfig, dciArgs, outputPath);

	const std::string workingDir = PathConverter::ToAbsolutePath(PathPrefix::SRCCODE);

	if (CommandShell* pCommand = CommandShell::ConstructAndStart(cmd, workingDir))
	{
		pCommand->Join();
		
		const int ret = pCommand->GetReturnValue();
		
		INOTE_VERBOSE("\nExit Code: %d\nOutput Was:\n%s\n", ret, StringToUnixLf(pCommand->GetOutput()).c_str());

		if (ret != 0)
		{
			IERR("make-module-index failed!\n", ret);
			IERR("\nExit Code: %d\nOutput Was:\n%s\n", ret, StringToUnixLf(pCommand->GetOutput()).c_str());
			
			return BuildTransformStatus::kFailed;
		}
	}
	else
	{
		IABORT("Failed to create & start command '%s'\n", cmd.c_str());
		return BuildTransformStatus::kFailed;
	}

	const BuildPath dcBinPath = GetOutputPath("BinPath");

	DataStore::UploadFile(outputPath, dcBinPath, nullptr, DataStorage::kAllowAsyncUpload);

#if DELETE_OUTPUT_FILES
	// Hack - Clean up after ourselves
	for (const TransformInput& input : inputs)
	{
		const std::string relPath = input.m_file.AsRelativePath();
		const std::string relDciPath = relPath.substr(relPath.find(FileIO::separator, 4) + 1); /* dc1/<proj-name>/*/

		const std::string moduleName = relDciPath.substr(0, relDciPath.length() - 4 /*.dci */);

		const std::string absoluteDciPath = GetBinFileOutputPath(moduleName, m_rootProjectName);

		FileIO::deleteFile((absoluteDciPath.substr(0, absoluteDciPath.length() - 4) + ".dci").c_str());
	}
	
	FileIO::deleteFile(outputPath.c_str());
	FileIO::deleteFile(dciConcatPath.c_str());
#endif

	const std::string srcBasePath = PathConverter::ToAbsolutePath(PathPrefix::SRCCODE);
	const std::string makeIndexPath = srcBasePath + m_dcoConfig.m_dcCollectsDir + "dc/make-module-index.rkt";

	std::vector<DcoSubDepEntry> deps;

	GatherDcxPhysicalDependencies(m_dcoConfig, m_rootProjectName, makeIndexPath, 1, deps);

	for (const DcoSubDepEntry& entry : deps)
	{
		RegisterDiscoveredDependency(BuildPath(entry.m_modulePath), entry.m_depth - 1);
	}

	return BuildTransformStatus::kOutputsUpdated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
std::string BuildTransform_DcModulesBin::WriteToTempFile(const std::string& content) const
{
	std::string tempDir = NdGetEnv("TEMP");
	tempDir += FileIO::separator;
	tempDir += "dco-build";
	tempDir += FileIO::separator;

	const Err res = FileIO::makeDirPath(tempDir.c_str());
	
	IABORT_IF(res.Failed(), "Failed to create temp directory '%s'", tempDir.c_str());

	const std::string tempFile = toolsutils::MakeTempFileName(tempDir, "dco_cmd_line_", "wt");
	
	FILE* pTempFile = fopen(tempFile.c_str(), "wt");

	if (pTempFile)
	{
		fwrite(content.c_str(), content.length(), 1, pTempFile);

		fclose(pTempFile);
		pTempFile = nullptr;
	}
	else
	{
		IABORT("Failed to open temp file '%s'\n", tempFile.c_str());
	}

	return tempFile;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_ArchiveStartupBinsSpawner::BuildTransform_ArchiveStartupBinsSpawner(const std::string& projectName,
																				   const BuildTransformContext* pContext)
	: BuildTransform("ArchiveStartupBinsSpawner", pContext), m_rootProjectName(projectName)
{
	SetDependencyMode(DependencyMode::kIgnoreDependency);

	m_preEvaluateDependencies.SetString("dcoDepFileVersion", DcoDepFile::kDependencyFileVersion);
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_ArchiveStartupBinsSpawner::Evaluate()
{
	const TransformInput& modulesListInput = GetInput("StartupModules");

	std::string modulesListTxt;
	DataStore::ReadData(modulesListInput.m_file, modulesListTxt);

	std::vector<std::string> modulesList = SplitLines(modulesListTxt);
	
	modulesList.push_back("startup");

	std::vector<TransformInput> archiveInputs;
	std::string binsTxt;

	const std::string outputBasePath = std::string(PathPrefix::BIN_OUTPUT) + "dc1" + FileIO::separator;

	for (const std::string& startupModule : modulesList)
	{
		const std::string inputPath = outputBasePath + startupModule + ".bin";

		binsTxt += inputPath + "\r\n";

		archiveInputs.push_back(TransformInput(BuildPath(inputPath)));
	}

	BuildTransform_ArchiveStartupBins* pArchiveXfrm = new BuildTransform_ArchiveStartupBins(m_pContext);

	const std::string archiveFile = outputBasePath + "startup-dc-files.psarc";

	uint32_t outputFlags = TransformOutput::kIncludeInManifest;

	if (m_pContext->m_toolParams.HasParam("--replicate"))
	{
		outputFlags |= TransformOutput::kReplicate;
	}

	pArchiveXfrm->SetInputs(archiveInputs);
	pArchiveXfrm->AddOutput(TransformOutput(archiveFile, "StartupPsArc", outputFlags));

	m_pContext->m_buildScheduler.AddBuildTransform(pArchiveXfrm, m_pContext);

	const BuildPath& binsListOutputPath = GetOutputPath("StartupBins");
	DataStore::WriteData(binsListOutputPath, binsTxt);

	return BuildTransformStatus::kOutputsUpdated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_ArchiveStartupBins::BuildTransform_ArchiveStartupBins(const BuildTransformContext* pContext)
	: BuildTransform("ArchiveStartupBins", pContext)
{
	m_preEvaluateDependencies.SetString("dcoDepFileVersion", DcoDepFile::kDependencyFileVersion);
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_ArchiveStartupBins::Evaluate()
{
	const std::string psarcArchiver = NdGetEnv("SCE_ORBIS_SDK_DIR") + FileIO::separator + "host_tools"
									  + FileIO::separator + "bin" + FileIO::separator + "orbis-psarc.exe";

	if (!FileIO::fileExists(psarcArchiver.c_str()))
	{
		IERR("Failed to find psarc archiver '%s'!\n", psarcArchiver.c_str());
		return BuildTransformStatus::kFailed;
	}

	std::string tmpDir;
	CreateTemporaryLocalDir(tmpDir, FileIO::MakeGloballyUnique("ArchiveStartupBins"));

	const std::string baseTmpDir = tmpDir;

	tmpDir = StringUtil::replaceAll(tmpDir, "/", FileIO::separator);
	tmpDir = StringUtil::replaceAll(tmpDir, "\\", FileIO::separator);
	tmpDir = tmpDir + FileIO::separator;

	const BuildPath& archivePath = GetOutputPath("StartupPsArc");
	const std::string archiveTmpPath = tmpDir + archivePath.ExtractFilename();
	const std::string outputBasePath = BuildPath(std::string(PathPrefix::BIN_OUTPUT) + "dc1" + FileIO::separator).AsAbsolutePath();

	INOTE_VERBOSE("Creating startup bins archive:\n");
	INOTE_VERBOSE("  temp path:   %s\n", archiveTmpPath.c_str());
	INOTE_VERBOSE("  output path: %s\n", archivePath.AsAbsolutePath().c_str());

	// Create the contents of the archive manifest
	std::string manifestTmpPath = tmpDir + "manifest.xml";
	std::ofstream manifest(manifestTmpPath, std::ofstream::out | std::ofstream::trunc);
	manifest << "<psarc>\n";
	manifest << "\t<create archive=\"" << archiveTmpPath << "\" overwrite=\"true\">\n";
	manifest << "\t\t<compression enabled=\"false\" />\n";
	manifest << "\t\t<strip regex=\"" << tmpDir << "\" />\n";

	INOTE_VERBOSE("Archive files:\n");
	for (const TransformInput& input : GetInputs())
	{
		const std::string subPath = input.m_file.AsAbsolutePath().substr(outputBasePath.size());
		INOTE_VERBOSE("  %s\n", subPath.c_str());

		const std::string tempBinPath = tmpDir + subPath;
		DataStore::DownloadFile(tempBinPath, input.m_file);

		manifest << "\t\t<file path=\"" << tempBinPath << "\" />\n";
	}

	manifest << "\t</create>\n</psarc>";
	manifest.close();

	// Create the archive
	std::string cmd = psarcArchiver + " create --xml=" + manifestTmpPath;

	INOTE_VERBOSE("Executing archive cmd:\n");
	INOTE_VERBOSE("  %s\n", cmd.c_str());

	if (CommandShell* pCommand = CommandShell::ConstructAndStart(cmd.c_str()))
	{
		pCommand->Join();

		int ret = pCommand->GetReturnValue();
		if (ret != 0)
		{
			IERR("Archiver exited with code %d!\nCommand: %s\nOutput: %s\n",
				 ret,
				 cmd.c_str(),
				 pCommand->GetOutput().c_str());

			FileIO::deleteDirectory(baseTmpDir.c_str());
			return BuildTransformStatus::kFailed;
		}

		DataStore::UploadFile(archiveTmpPath, archivePath);
		FileIO::deleteDirectory(baseTmpDir.c_str());
	}
	else
	{
		IERR("Failed to create and start command '%s'\n", cmd.c_str());
		FileIO::deleteDirectory(baseTmpDir.c_str());
		return BuildTransformStatus::kFailed;
	}

	return BuildTransformStatus::kOutputsUpdated;
}
