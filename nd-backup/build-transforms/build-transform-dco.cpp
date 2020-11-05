/*
 * Copyright (c) 2018 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/libs/toolsutil/simple-dependency.h"
#include "tools/pipeline3/build/build-transforms/build-transform-dco.h"

#include "common/util/stringutil.h"

#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-dc-bin.h"
#include "tools/pipeline3/build/build-transforms/build-transform-dc-header.h"
#include "tools/pipeline3/build/build-transforms/build-transform-dc-built-ins.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/path-converter.h"

//#pragma optimize("", off) // uncomment when debugging in release mode

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_GenerateRsFile : public BuildTransform
{
public:
	BuildTransform_GenerateRsFile(const DcoProject& project, const BuildTransformContext* pContext)
		: BuildTransform("GenerateRsFile", pContext), m_project(project)
	{
		SetDependencyMode(DependencyMode::kIgnoreDependency);
	}

	virtual BuildTransformStatus Evaluate() override;

	DcoProject m_project;
};

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_GenerateRsFile::Evaluate()
{
	std::map<std::string, std::set<std::string>> filesPerDirectory;

	const std::string rsDir = GetDcoSourceBasePath(m_project);

	const std::string searchDir = rsDir + "render-settings";

	std::set<std::string> availableFiles;
	std::vector<std::string> allowedExtensions = { "dcx" };
	GatherFilesInFolderRecursively(availableFiles,
								   searchDir,
								   allowedExtensions,
								   m_pContext->m_buildScheduler.GetFileDateCache());

	std::string fileListTxt;

	for (const std::string& filePath : availableFiles)
	{
		std::string shortPath = filePath.substr(searchDir.size() + 1);
		fileListTxt += shortPath + "\r\n";
	}

	const TransformOutput& rsListOutput = GetOutput("RsList");
	DataStore::WriteData(rsListOutput.m_path, fileListTxt);

	return BuildTransformStatus::kOutputsUpdated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_ParseDco::BuildTransform_ParseDco(const std::string& projectName,
												 Mode mode,
												 const BuildTransformContext* pContext)
	: BuildTransform("ParseDco", pContext), m_projectName(projectName), m_mode(mode)
{
	SetDependencyMode(DependencyMode::kIgnoreDependency);
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_ParseDco::Evaluate()
{
	const std::string configPath = GetInputFile("ConfigFile").AsAbsolutePath();

	DcoConfig dcoConfiguration;
	ReadDcoConfig(configPath, dcoConfiguration);

	if (nullptr == GetDcoProjectByName(dcoConfiguration, m_projectName))
	{
		IERR("Failed to find DCO project '%s' (check your project.dco?)", m_projectName.c_str());
		return BuildTransformStatus::kFailed;
	}

	switch (m_mode)
	{
	case Mode::kBins:
		BuildBins(dcoConfiguration);
		break;

	case Mode::kHeaders:
		BuildHeaders(dcoConfiguration);
		break;

	case Mode::kBuiltIns:
		BuildBuiltIns(dcoConfiguration);
		break;
	}

	if (m_mode != Mode::kBins)
	{
		DataStore::WriteData(GetOutputPath("BuildEntries"), "");
	}

	if (m_pContext->m_toolParams.HasParam("--generate-rs-file"))
	{
		if (const DcoProject* pProject = GetDcoProjectByName(dcoConfiguration, m_projectName))
		{
			const std::string rsPath = std::string(PathPrefix::BIN_OUTPUT) + "dc1" + FileIO::separator
									   + "render-settings-file-list.txt";

			BuildTransform_GenerateRsFile* pRsTransform = new BuildTransform_GenerateRsFile(*pProject, m_pContext);

			uint32_t outputFlags = TransformOutput::kIncludeInManifest;

			if (m_pContext->m_toolParams.HasParam("--replicate"))
			{
				outputFlags |= TransformOutput::kReplicate;
			}

			pRsTransform->SetOutput(TransformOutput(rsPath, "RsList", outputFlags));
		
			m_pContext->m_buildScheduler.AddBuildTransform(pRsTransform, m_pContext);
		}
	}

	return BuildTransformStatus::kOutputsUpdated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool TransformDependsOnFile(BuildTransform* pXform, const std::string& mrSourceFile)
{
	const std::string uniqueConfigString = pXform->GetOutputConfigString();

	DataHash keyHash;
	if (DataStore::RetrieveDisabledTransformKeyHash(uniqueConfigString, keyHash))
	{
		BuildFile depFile(pXform->GetFirstOutput().m_path.AsPrefixedPath() + ".d", keyHash);
		std::string depJsonString;
		DataStore::ReadData(depFile, depJsonString);

		// Add all missing dependencies and their timestamps.
		toolsutils::SimpleDependency dep;
		if (!FromJsonString(dep, depJsonString))
		{
			IERR("Failed to parse mr dependency file with hash '%s#%s'", depFile.AsPrefixedPath().c_str(), depFile.GetContentHash().AsText().c_str());
		}

		const std::map<std::string, std::string>& depMap = dep.GetDependencies();
		for (auto& entry : depMap)
		{
			if (entry.second.find("*.") != std::string::npos)
			{
				const std::string absPath = PathConverter::ToAbsolutePath(entry.second);
				const std::string dirPath = FileIO::getDirPath(absPath);

				std::map<std::string, FileIO::FileTime> fileTimeMap;
				FileIO::readDirFileTimes(dirPath.c_str(), fileTimeMap);

				for (auto& dirEntry : fileTimeMap)
				{
					const std::string fullDirEntry = dirPath + dirEntry.first;
					if (fullDirEntry.find(mrSourceFile) != std::string::npos)
					{
						return true;
					}
				}
			}
			else if (entry.second.find(mrSourceFile) != std::string::npos)
			{
				return true;
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform_ParseDco::BuildBins(const DcoConfig& dcoConfiguration)
{
	const ToolParams& toolParams = m_pContext->m_toolParams;

	// Prefetch all timestamps
	for (const auto& proj : dcoConfiguration.m_projects)
	{
		const std::string prefixedSrcDir = std::string(PathPrefix::SRCCODE) + proj.m_sourceRoot;

		std::set<std::string> availableFiles;
		std::vector<std::string> allowedExtensions = { "dcx" };
		GatherFilesInFolderRecursively(availableFiles,
									   PathConverter::ToAbsolutePath(prefixedSrcDir),
									   allowedExtensions,
									   m_pContext->m_buildScheduler.GetFileDateCache());
	}

	std::string templateDir = NdGetEnv("TEMP");
	templateDir += FileIO::separator;
	templateDir += "dco-build";
	templateDir += FileIO::separator;
	templateDir += "sndbs-templates-" + m_projectName;
	templateDir += FileIO::separator;

	FileIO::FixSlashes(templateDir);

	const std::string sndbsTemplate = GenerateSndbsToolTemplate(dcoConfiguration, m_projectName);

	const Err res = FileIO::makeDirPath(templateDir.c_str());

	IABORT_IF(res.Failed(), "Failed to create temp template directory '%s'", templateDir.c_str());

	const std::string templatePath = templateDir + "Racket.exe.sn-dbs-tool.ini";

	FILE* pTemplateFile = fopen(templatePath.c_str(), "wt");
	IABORT_IF(!pTemplateFile, "Failed to write template to '%s'", templatePath.c_str());

	fwrite(sndbsTemplate.c_str(), sndbsTemplate.length(), 1, pTemplateFile);

	fclose(pTemplateFile);
	pTemplateFile = nullptr;

	std::vector<DcoBuildEntry> buildEntries;
	GatherDcoBuildEntries(dcoConfiguration, m_projectName, buildEntries);

	const std::string outputBasePath = std::string(PathPrefix::BIN_OUTPUT) + "dc1" + FileIO::separator;

	const BuildPath& buildEntriesNdbPath = GetOutputPath("BuildEntries");

	{
		NdbStream stream;
		stream.OpenForWriting(Ndb::kBinaryStream);
		NdbWrite(stream, "dcoBuildEntries", buildEntries);
		stream.Close();

		DataStore::WriteData(buildEntriesNdbPath, stream);
	}

	const std::string snDbsProjectName = "build dc bins " + m_projectName;
	const std::string snDbsDbgProjectName = "build dc bins " + m_projectName + " (debug)";

	std::vector<TransformInput> modulesBinInputs;
	std::vector<TransformInput> debugModulesBinInputs;

	uint32_t outputFlags = TransformOutput::kIncludeInManifest;

	if (m_pContext->m_toolParams.HasParam("--replicate"))
	{
		outputFlags |= TransformOutput::kReplicate;
	}

	const std::string startupIncludesList = outputBasePath + "startup-includes.txt";
	const std::string startupModulesList = outputBasePath + "startup-modules.txt";
	const std::string startupBinsList = outputBasePath + "startup-bins.txt";

	bool startupValid = false;

	bool doingDirectMr = false;

	if (!toolParams.m_mrSourceFile.empty())
	{
		const DcoBuildEntry* pDirectEntry = nullptr;

		for (const DcoBuildEntry& entry : buildEntries)
		{
			const std::string fullDcxPath = entry.m_sourceBaseDir + entry.m_sourcePath;
			
			if (fullDcxPath.find(toolParams.m_mrSourceFile) != std::string::npos)
			{
				pDirectEntry = &entry;
				break;
			}
		}

		if (pDirectEntry)
		{
			DcoBuildEntry directEntry = *pDirectEntry;
			buildEntries.clear();
			buildEntries.push_back(directEntry);
			doingDirectMr = true;
		}
	}

	for (const DcoBuildEntry& entry : buildEntries)
	{
		INOTE_VERBOSE("Build Entry: %s [%s%s]\n",
					  entry.m_moduleName,
					  entry.m_projectName,
					  entry.m_debug ? " (debug)" : "");

		const std::string& projName = entry.m_debug ? snDbsDbgProjectName : snDbsProjectName;

		BuildTransform_DcBin* pXform = new BuildTransform_DcBin(dcoConfiguration,
																entry,
																m_projectName,
																templateDir,
																projName,
																m_pContext);

		const TransformOutput output = TransformOutput(outputBasePath + entry.m_moduleName + ".bin", "BinPath", outputFlags);
		const TransformOutput dciOutput = TransformOutput(outputBasePath + entry.m_moduleName + ".dci", "DciPath");
		
		pXform->SetInput(TransformInput(entry.m_sourceBaseDir + entry.m_sourcePath, "DcxPath"));
		pXform->AddOutput(output);
		pXform->AddOutput(dciOutput);

		if (entry.m_moduleName == "startup")
		{
			pXform->AddInput(TransformInput(buildEntriesNdbPath, "BuildEntries"));
			pXform->AddOutput(TransformOutput(startupIncludesList, "StartupIncludes"));
			pXform->AddOutput(TransformOutput(startupModulesList, "StartupModules"));
		}

		// If module starts with "level-sets" then expect a "module-name.json" output. This output is used by disc build.
		if (entry.m_moduleName.find("level-sets/") == 0)
		{
			const TransformOutput jsonOutput = TransformOutput(outputBasePath + entry.m_moduleName + ".json", "JsonPath", outputFlags);
			pXform->AddOutput(jsonOutput);
		}
		
		bool shouldRun = true;
		if (!doingDirectMr && !toolParams.m_mrSourceFile.empty())
		{
			shouldRun = TransformDependsOnFile(pXform, toolParams.m_mrSourceFile);
		}

		if (shouldRun)
		{
			if (entry.m_moduleName == "startup")
			{
				startupValid = true;
			}

			m_pContext->m_buildScheduler.AddBuildTransform(pXform, m_pContext);

			if (entry.m_debug)
			{
				debugModulesBinInputs.push_back(dciOutput);
			}
			else
			{
				modulesBinInputs.push_back(dciOutput);
			}
		}	
	}

	if (startupValid && toolParams.m_mrSourceFile.empty())
	{
		BuildTransform_ArchiveStartupBinsSpawner* pStartupXfrm = new BuildTransform_ArchiveStartupBinsSpawner(m_projectName,
																											  m_pContext);

		pStartupXfrm->SetInput(TransformInput(startupModulesList, "StartupModules"));
		pStartupXfrm->SetOutput(TransformOutput(startupBinsList, "StartupBins"));
	
		m_pContext->m_buildScheduler.AddBuildTransform(pStartupXfrm, m_pContext);
	}

	if (!modulesBinInputs.empty())
	{
		SnDbs::ProjectParams params;
		params.m_name = snDbsProjectName;
		params.m_noDefaultRewriteRules = true;
		params.m_templateDirs.push_back(templateDir);
		params.m_timeoutSec = 20 * 60;
		params.m_workingDir = PathConverter::ToAbsolutePath(PathPrefix::SRCCODE);
		params.m_reserveSize = modulesBinInputs.size();

		SnDbs::StartProject(params);

		if (toolParams.m_mrSourceFile.empty())
		{
			BuildTransform_DcModulesBin* pXform = new BuildTransform_DcModulesBin(dcoConfiguration,
																				  "modules",
																				  snDbsProjectName,
																				  m_projectName,
																				  m_pContext);
			pXform->SetInputs(modulesBinInputs);
			pXform->SetOutput(TransformOutput(outputBasePath + "modules.bin", "BinPath", outputFlags));

			m_pContext->m_buildScheduler.AddBuildTransform(pXform, m_pContext);
		}
	}

	if (!debugModulesBinInputs.empty())
	{
		SnDbs::ProjectParams params;
		params.m_name = snDbsDbgProjectName;
		params.m_noDefaultRewriteRules = true;
		params.m_templateDirs.push_back(templateDir);
		params.m_timeoutSec = 20 * 60;
		params.m_workingDir = PathConverter::ToAbsolutePath(PathPrefix::SRCCODE);
		params.m_reserveSize = debugModulesBinInputs.size();

		SnDbs::StartProject(params);

		if (toolParams.m_mrSourceFile.empty())
		{
			BuildTransform_DcModulesBin* pXform = new BuildTransform_DcModulesBin(dcoConfiguration,
																				  "modules-debug",
																				  snDbsDbgProjectName,
																				  m_projectName,
																				  m_pContext);
			pXform->SetInputs(debugModulesBinInputs);
			pXform->SetOutput(TransformOutput(outputBasePath + "modules-debug.bin", "BinPath", outputFlags));

			m_pContext->m_buildScheduler.AddBuildTransform(pXform, m_pContext);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform_ParseDco::BuildHeaders(const DcoConfig& dcoConfiguration)
{
	std::string templateDir = NdGetEnv("TEMP");
	templateDir += FileIO::separator;
	templateDir += "dco-build";
	templateDir += FileIO::separator;
	templateDir += "sndbs-templates-" + m_projectName;
	templateDir += FileIO::separator;

	FileIO::FixSlashes(templateDir);

	const std::string sndbsTemplate = GenerateSndbsToolTemplate(dcoConfiguration, m_projectName);

	const Err res = FileIO::makeDirPath(templateDir.c_str());

	IABORT_IF(res.Failed(), "Failed to create temp template directory '%s'", templateDir.c_str());

	const std::string templatePath = templateDir + "Racket.exe.sn-dbs-tool.ini";

	FILE* pTemplateFile = fopen(templatePath.c_str(), "wt");
	IABORT_IF(!pTemplateFile, "Failed to write template to '%s'", templatePath.c_str());

	fwrite(sndbsTemplate.c_str(), sndbsTemplate.length(), 1, pTemplateFile);

	fclose(pTemplateFile);
	pTemplateFile = nullptr;

	std::vector<DcoBuildHeaderEntry> buildEntries;

	GatherDcoHeaderBuildEntries(dcoConfiguration, m_projectName, false, buildEntries);

	const std::string outputBasePath = std::string(PathPrefix::DCXBUILD) + "dch" + FileIO::separator;

	const std::string snDbsProjectName = "build dc headers " + m_projectName;

	for (const DcoBuildHeaderEntry& entry : buildEntries)
	{
		BuildTransform_DcHeader* pXform = new BuildTransform_DcHeader(dcoConfiguration,
																	  entry,
																	  m_projectName,
																	  templateDir,
																	  snDbsProjectName,
																	  m_pContext);


		pXform->SetInput(TransformInput(entry.m_sourceBaseDir + entry.m_sourcePath, "DcxPath"));
		pXform->SetOutput(TransformOutput(entry.m_outputPath, "HeaderPath", TransformOutput::kReplicate));

		m_pContext->m_buildScheduler.AddBuildTransform(pXform, m_pContext);
	}

	if (!buildEntries.empty())
	{
		SnDbs::ProjectParams params;
		params.m_name = snDbsProjectName;
		params.m_noDefaultRewriteRules = true;
		params.m_templateDirs.push_back(templateDir);
		params.m_timeoutSec = 60;
		params.m_workingDir = PathConverter::ToAbsolutePath(PathPrefix::SRCCODE);

		SnDbs::StartProject(params);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform_ParseDco::BuildBuiltIns(const DcoConfig& dcoConfiguration)
{
	const DcoProject* pNdlibProj = GetDcoProjectByName(dcoConfiguration, "ndlib");

	IABORT_IF(!pNdlibProj, "Can't build DC built-ins without an ndlib project dco config!\n");

	const std::string rktInputDir = std::string(PathPrefix::SRCCODE) + dcoConfiguration.m_dcCollectsDir + FileIO::separator
								 + "dc" + FileIO::separator;

	const std::string codeDstPath = GetHeaderFileOutputPath("code", dcoConfiguration, *pNdlibProj);

	const std::string builtinSrcPath = std::string(PathPrefix::SRCCODE) + pNdlibProj->m_sourceRoot + FileIO::separator
									   + "dc-builtins.h";
	const std::string builtinDstPath = GetHeaderFileOutputPath("dc-builtins", dcoConfiguration, *pNdlibProj);

	// racket will try to put files in this directory, so we have to make sure it exists
	const std::string dstDir = FileIO::getDirPath(codeDstPath);
	if (FileIO::makeDirPath(dstDir.c_str()) != Err::kOK)
	{
		IABORT("Could not create directory '%s'", dstDir.c_str());
	}

	std::vector<TransformInput> inputs;
	inputs.push_back(TransformInput(BuildPath(rktInputDir + "script.rkt"), "script.rkt"));
	inputs.push_back(TransformInput(BuildPath(rktInputDir + "script-ast.rkt"), "script-ast.rkt"));
	inputs.push_back(TransformInput(BuildPath(rktInputDir + "script-op.rkt"), "script-op.rkt"));
	inputs.push_back(TransformInput(BuildPath(rktInputDir + "script-types.rkt"), "script-types.rkt"));
	inputs.push_back(TransformInput(BuildPath(builtinSrcPath), "dc-builtins.h"));

	std::vector<TransformOutput> outputs;
	outputs.push_back(TransformOutput(codeDstPath, "code.h", TransformOutput::kReplicate));
	outputs.push_back(TransformOutput(builtinDstPath, "dc-builtins.h", TransformOutput::kReplicate));

	BuildTransform_DcBuiltins* pXform = new BuildTransform_DcBuiltins(dcoConfiguration, m_pContext);

	pXform->SetInputs(inputs);
	pXform->SetOutputs(outputs);

	m_pContext->m_buildScheduler.AddBuildTransform(pXform, m_pContext);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Err ScheduleDcBuild(const ToolParams& toolParams,
						   const std::string& assetName, 
						   const std::string& projectName,
						   BuildTransformContext* pContext,
						   BuildTransform_ParseDco::Mode mode)
{
	INOTE_VERBOSE("Creating Build Transforms for DC %s...", assetName.c_str());

	const std::string configPath = PathConverter::ToAbsolutePath(PathPrefix::SRCCODE) + "project.dco";

	BuildTransform_ParseDco* pParseTransform = new BuildTransform_ParseDco(projectName,
																		   mode,
																		   pContext);

	const std::string outputPath = std::string(PathPrefix::BUILD_OUTPUT) + "dco1" + FileIO::separator + "build-entries.ndb";

	pParseTransform->AddInput(TransformInput(configPath, "ConfigFile"));
	pParseTransform->SetOutput(TransformOutput(outputPath, "BuildEntries"));

	pContext->m_buildScheduler.AddBuildTransform(pParseTransform, pContext);

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err ScheduleDcBins(const ToolParams& toolParams, const std::string& assetName, BuildTransformContext* pContext)
{
	std::string projName = pContext->m_toolParams.m_gameName;
	
	const size_t dashPos = assetName.find('-');

	if (dashPos != std::string::npos)
	{
		projName = assetName.substr(dashPos + 1);

		if (toolParams.m_isMr)
		{
			const size_t secondDashPos = assetName.find('-', dashPos + 1);
			if (secondDashPos)
			{
				projName = assetName.substr(dashPos + 1, secondDashPos - dashPos - 1);
			}
		}
	}

	return ScheduleDcBuild(toolParams, assetName, projName, pContext, BuildTransform_ParseDco::Mode::kBins);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err ScheduleDcHeaders(const ToolParams& toolParams, const std::string& assetName, BuildTransformContext* pContext)
{
	std::string projName = pContext->m_toolParams.m_gameName;

	const size_t dashPos = assetName.find('-');
	if (dashPos != std::string::npos)
	{
		projName = assetName.substr(dashPos + 1);
	}

	return ScheduleDcBuild(toolParams, assetName, projName, pContext, BuildTransform_ParseDco::Mode::kHeaders);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err ScheduleDcBuiltins(const ToolParams& toolParams, const std::string& assetName, BuildTransformContext* pContext)
{
	std::string projName = pContext->m_toolParams.m_gameName;

	return ScheduleDcBuild(toolParams, assetName, projName, pContext, BuildTransform_ParseDco::Mode::kBuiltIns);
}
