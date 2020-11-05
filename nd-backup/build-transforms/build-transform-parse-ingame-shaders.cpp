/*
 * Copyright (c) 2018 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "tools/pipeline3/build/build-transforms/build-transform-parse-ingame-shaders.h"

#include "common/util/fileio.h"
#include "common/util/stringutil.h"

#include "tools/libs/toolsutil/command-job.h"
#include "tools/libs/toolsutil/json_helpers.h"
#include "tools/libs/toolsutil/sndbs.h"
#include "tools/libs/toolsutil/strfunc.h"

#include "tools/pipeline3/toolversion.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-archive-ingame-shaders.h"
#include "tools/pipeline3/build/build-transforms/build-transform-ingame-shader.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/path-converter.h"

#include <vector>
#include <regex>

/// --------------------------------------------------------------------------------------------------------------- ///
std::string ConcatPath(const std::string &base, const std::string &file)
{
	std::vector<std::string> tokens = toolsutils::Tokenize(base, "\\/");
	toolsutils::Tokenize(tokens, file, "\\/");

	std::vector<std::string> path;
	path.reserve(tokens.size());
	
	for (auto &token : tokens)
	{
		if (token == ".")
		{
			continue;
		}

		if (token == "..")
		{
			if (!path.empty())
			{
				path.pop_back();
			}
		}
		else
		{
			path.push_back(token);
		}
	}

	return toolsutils::Join(path, FileIO::separator);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static std::string GetSrcShaderAbsolutePath(const std::string &file)
{
	return ConcatPath(PathConverter::ToAbsolutePath(PathPrefix::SRCCODE),
					  "shared/src/ndlib/render/shaders/" + file);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static std::string FindShaderInclude(const IngameShadersConfig &config,
									 const std::string &base,
									 const std::string &file,
									 FileDateCache &fileDataCache)
{
	std::string defaultPath = ConcatPath(base, file);
	if (fileDataCache.fileExists(defaultPath.c_str()))
	{
		return defaultPath;
	}

	for (auto &inc : config.m_includePaths)
	{
		std::string path = ConcatPath(inc.AsAbsolutePath(), file);
		if (fileDataCache.fileExists(path.c_str()))
		{
			return path;
		}
	}

	return defaultPath;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct ShaderFile
{
	std::string m_file;
	std::vector<IngameShaderEntry> m_entries;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static IngameShaderEntry ParseShaderEntry(const rapidjson::Value &content)
{
	using namespace toolsutils::json_helpers;

	IngameShaderEntry entry;
	if (content.IsArray())
	{
		/*
			Each entry is an array that contains the following content:
			[
				entry point,
				profile,
				entry point suffix,
				[ array of defines ],
				compiler arguments,
			]
		*/

		U32 count = content.Size();
		if (count > 0)
		{
			entry.m_entryPoint = makeString(content[0]);
		}

		if (count > 1)
		{
			entry.m_profile = makeString(content[1]);
		}

		if (count > 2)
		{
			entry.m_entryPointSuffix = makeString(content[2]);
		}

		if (count > 3)
		{
			const rapidjson::Value &defines = content[3];
			if (defines.IsArray())
			{
				for (U32 i = 0; i < defines.Size(); i++)
				{
					std::string define = makeString(defines[i]);
					std::string value;
					if (i + 1 < defines.Size())
					{
						value = makeString(defines[i + 1]);
						i++;
					}

					if (define.length())
					{
						if (value.length())
						{
							entry.m_defines.push_back(define + '=' + value);
						}
						else
						{
							entry.m_defines.push_back(define);
						}
					}
				}
			}
		}

		if (count > 4)
		{
			entry.m_arguments = makeString(content[4]);
		}
	}

	return entry;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ShaderFile ParseShaderFile(const rapidjson::Value &content)
{
	using namespace toolsutils::json_helpers;

	ShaderFile file;

	std::string filename = makeString(content, "File");
	file.m_file = GetSrcShaderAbsolutePath(filename);

	bool isAsm = toolsutils::EndsWith(filename, ".scu");

	const rapidjson::Value &entries = GetValue(content, "Entries");
	if (entries.IsArray())
	{
		file.m_entries.reserve(entries.Size());
		for (U32 i = 0; i < entries.Size(); i++)
		{
			IngameShaderEntry entry = ParseShaderEntry(entries[i]);
			entry.m_isAsm = isAsm;

			file.m_entries.push_back(entry);
		}
	}

	return file;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static std::vector<ShaderFile> ParseFilesJson(const std::string &filesJsonPath)
{
	using namespace toolsutils::json_helpers;

	std::vector<ShaderFile> files;

	rapidjson::Document doc;
	if (LoadJsonFile(doc, filesJsonPath))
	{
		if (doc.IsArray())
		{
			files.reserve(doc.Size());
			for (U32 i = 0; i < doc.Size(); i++)
			{
				files.push_back(ParseShaderFile(doc[i]));
			}
		}
	}

	return files;
}

/// --------------------------------------------------------------------------------------------------------------- ///
typedef std::map<std::string, std::set<std::string>> ShaderFileDependencies;

/// --------------------------------------------------------------------------------------------------------------- ///
static std::string RemoveComments(const std::string &content)
{
	std::string result;
	result.reserve(content.size());

	const char *curr = &content[0];
	const char *end = curr + content.length();
	while (curr < end)
	{
		if (curr[0] == '/' && curr[1] == '*')
		{
			// Skip over multiline comments.
			while (curr < end && (curr[0] != '*' || curr[1] != '/'))
			{
				curr++;
			}

			curr += 2;
		}
		else if (curr[0] == '/' && curr[1] == '/')
		{
			// Skip over single line comments.
			while (curr < end && curr[0] != '\n' && curr[0] != '\r')
			{
				curr++;
			}
		}
		else if (curr[0] == '"')
		{
			// Output string literals ignoring any comments in the literal and accounting for escaped quotes.
			result.push_back(curr[0]);
			curr++;

			bool escape = false;
			while (curr < end && (curr[0] != '"' || escape))
			{
				escape = escape ? false : curr[0] == '\\';

				result.push_back(curr[0]);
				curr++;
			}

			if (curr < end)
			{
				result.push_back(curr[0]);
				curr++;
			}
		}
		else
		{
			result.push_back(curr[0]);
			curr++;
		}
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static std::vector<std::string> ParseIncludes(const IngameShadersConfig &config,
											  const std::string &file,
											  FileDateCache &fileDataCache)
{
	if (!fileDataCache.fileExists(file))
	{
		return {};
	}

	std::string content = FileIO::GetTextFileContent(file.c_str());

	content = RemoveComments(content);

	std::string base = ConcatPath(file, "..");
	std::vector<std::string> includes;

	static const std::regex s_includeRegex("\\s*#\\s*include\\s+\"([^\"]+)\"", std::regex::ECMAScript | std::regex::optimize);
	for (std::sregex_iterator i = std::sregex_iterator(content.begin(), content.end(), s_includeRegex);
		 i != std::sregex_iterator();
		 i++)
	{
		const std::smatch &match = *i;
		std::string inc = FindShaderInclude(config, base, match[1], fileDataCache);

		includes.push_back(inc);
	}

	return includes;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const std::set<std::string> &FindShaderFileDependencies(const IngameShadersConfig &config,
															   const std::string &file,
															   ShaderFileDependencies &dependencies,
															   FileDateCache &fileDataCache)
{
	if (dependencies.find(file) != dependencies.end())
	{
		return dependencies[file];
	}

	std::vector<std::string> includes = ParseIncludes(config, file, fileDataCache);

	std::set<std::string> &deps = dependencies[file];
	deps.insert(file);

	for (auto &inc : includes)
	{
		const std::set<std::string> &infos = FindShaderFileDependencies(config, inc, dependencies, fileDataCache);
		deps.insert(infos.begin(), infos.end());
	}

	for (auto &inc : config.m_forceIncludes)
	{
		const std::set<std::string> &infos = FindShaderFileDependencies(config, inc.AsAbsolutePath(), dependencies, fileDataCache);
		deps.insert(infos.begin(), infos.end());
	}

	return deps;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ShaderFileDependencies FindShaderFileDependencies(const IngameShadersConfig &config,
														 const std::vector<ShaderFile> &info,
														 FileDateCache &fileDataCache)
{
	ShaderFileDependencies dependencies;
	for (auto &file : info)
	{
		FindShaderFileDependencies(config, file.m_file, dependencies, fileDataCache);
	}

	return dependencies;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static std::string GetCompilerBinDir()
{
	const char* orbisSdk = NdGetEnv("SCE_ORBIS_SDK_DIR");

	return ConcatPath(orbisSdk, "host_tools/bin");
}

/// --------------------------------------------------------------------------------------------------------------- ///
static std::string GetCompilerVersion(const std::string &compiler)
{
	std::string version;
	std::string cmd = compiler + " --version";

	if (CommandShell *pCommand = CommandShell::ConstructAndStart(cmd.c_str()))
	{
		pCommand->Join();

		int ret = pCommand->GetReturnValue();
		if (ret == 0)
		{
			version = toolsutils::Trim(pCommand->GetOutput(), "\n\r\t ");
		}
		else
		{
			IERR("Command '%s' failed with exit code %d!\n", cmd.c_str(), ret);
		}
	}
	else
	{
		IABORT("Failed to create and start command '%s'\n", cmd.c_str());
	}

	return version;
}

 /// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_ParseIngameShaderFilesJson::BuildTransform_ParseIngameShaderFilesJson(const std::string &projectName,
																					 const BuildTransformContext *pContext)
	: BuildTransform("ParseIngameShaderFilesJson", pContext)
	, m_projectName(projectName)
{
	SetDependencyMode(BuildTransform::DependencyMode::kIgnoreDependency);
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_ParseIngameShaderFilesJson::Evaluate()
{
	std::string binDir = GetCompilerBinDir();

	IngameShadersConfig config;
	config.m_shaderCompiler = ConcatPath(binDir, "orbis-wave-psslc.exe");
	config.m_shaderAssembler = ConcatPath(binDir, "orbis-cu-as.exe");
	config.m_psarcArchiver = ConcatPath(binDir, "orbis-psarc.exe");
	config.m_includePaths.push_back(ConcatPath(PathConverter::ToAbsolutePath(PathPrefix::SHADER), "include"));
	config.m_forceIncludes.push_back(GetSrcShaderAbsolutePath("hlsl2pssl.fxi"));

	config.m_compilerVersion = GetCompilerVersion(config.m_shaderCompiler);
	config.m_outputPath = toolsutils::GetBuildPathNew() + BUILD_TRANSFORM_INGAME_SHADERS_FOLDER;
	config.m_sndbsProject = "shaders-" + m_projectName;

	bool debugBuild = m_pContext->m_toolParams.m_shadersBuildOptions.find("debug") != std::string::npos;
	bool traceBuild = m_pContext->m_toolParams.m_shadersBuildOptions.find("trace") != std::string::npos;

	// Compiler arguments
	config.m_compilerArgs.push_back("-Wperf");
	config.m_compilerArgs.push_back(debugBuild ? "-Od" : "-O3");
	config.m_compilerArgs.push_back("-enablevalidation");
	config.m_compilerArgs.push_back("-max-user-extdata-count 240");
	config.m_compilerArgs.push_back("-cache");
	config.m_compilerArgs.push_back("-cache-gen-source-hash");
	config.m_compilerArgs.push_back("-sbiversion 3");
	config.m_compilerArgs.push_back("-pssl2");
	config.m_compilerArgs.push_back("-Werror");

	if (traceBuild)
	{
		config.m_compilerArgs.push_back("-ttrace 1");
	}

	// suppress a few warnings
	// warning D7525 : Using barriers on diverging paths can lead to undefined behavior
	// warning D6889 : inlined sampler state initialization has been skipped
	// warning D6890 : FX syntax annotation has been skipped
	// warning D6921 : implicit conversion turns floating - point number into integer : 'float' to 'unsigned int'
	// warning D6922 : implicit conversion turns integer into floating - point number : 'int' to 'float'
	// warning D6923 : implicit vector type narrowing : 'float4' to 'float3'
	// warning D20087 : unreferenced formal parameter
	// warning D20088 : unreferenced local variable
	// error D7524: Derivatives are available only in pixel shaders, LOD0 will be always used
	// error D7520: Not able to achieve target occupancy
	config.m_compilerArgs.push_back("-Wsuppress=7525,6889,6890,6921,6922,6923,20087,20088,7524,7520");

	// Assembler arguments
	config.m_assemblerArgs.push_back("-q");
	config.m_assemblerArgs.push_back("--cache");
	config.m_assemblerArgs.push_back("-Werror");

	// suppress a few warnings
	// warning RUNUSE__ : Register vcc_hi value was unused
	// warning INOFX___ : All output register values from instruction WITH NO SIDE EFFECTS were overwritten before used on all code paths
	// warning EXTGCESP : Export to mrt_color1 components RGBA exports components which will be discarded based on ps_export_color_en(mrt_color1, "xy")
	config.m_assemblerArgs.push_back("-Wdisable=RUNUSE__,INOFX___,EXTGCESP");

	const std::string filesJsonPath = GetInputFile("FilesJson").AsAbsolutePath();
	std::vector<ShaderFile> files = ParseFilesJson(filesJsonPath);

	ShaderFileDependencies dependencies = FindShaderFileDependencies(config, files, m_pContext->m_buildScheduler.GetFileDateCache());

	if (FileIO::makeDirPath(config.m_outputPath.c_str()) != Err::kOK)
	{
		IERR("Could not create directory '%s'\n", config.m_outputPath.c_str());
		return BuildTransformStatus::kFailed;
	}

	// Start the sndbs project
	{
		SnDbs::ProjectParams params;
		params.m_name = config.m_sndbsProject;
		params.m_timeoutSec = 20 * 60;
		params.m_workingDir = PathConverter::ToAbsolutePath(PathPrefix::SRCCODE);

		for (auto& file : files)
			params.m_reserveSize += file.m_entries.size();

		SnDbs::StartProject(params);
	}

	// Create transforms to build the shaders
	std::vector<BuildPath> outputFiles;
	for (auto &file : files)
	{
		BuildPath shaderFile;
		std::vector<BuildPath> deps;
		for (auto &dep : dependencies[file.m_file])
		{
			BuildPath bp(dep);
			if (dep == file.m_file)
			{
				shaderFile = bp;
			}
			else
			{
				deps.push_back(bp);
			}
		}

		std::vector<BuildTransform_IngameShader*> transforms = MakeIngameShadersTransform(config, shaderFile, deps,
																						  file.m_entries, m_pContext);

		for (auto transform : transforms)
		{
			for (auto &output : transform->GetOutputs())
			{
				outputFiles.push_back(output.m_path);
			}
		}

		for (auto transform : transforms)
		{
			m_pContext->m_buildScheduler.AddBuildTransform(transform, m_pContext);
		}
	}

	// Create transform to create the archive of all the shaders
	{
		std::vector<TransformInput> inputs;
		for (auto &input : outputFiles)
		{
			inputs.push_back(TransformInput(input));
		}

		inputs.push_back(GetInput("FilesJson"));

		U32 outputFlags = TransformOutput::kIncludeInManifest;

		if (m_pContext->m_toolParams.HasParam("--replicate"))
		{
			outputFlags |= TransformOutput::kReplicate;
		}

		std::vector<TransformOutput> outputs;
		outputs.push_back(TransformOutput(std::string(PathPrefix::SHADER_OUTPUT) + BUILD_TRANSFORM_SHADERS_ARCHIVE_FOLDER +
										  FileIO::separator + "shaders.psarc", "Archive", outputFlags));

		BuildTransform_ArchiveIngameShaders *pArchive = new BuildTransform_ArchiveIngameShaders(config, m_pContext);

		pArchive->SetInputs(inputs);
		pArchive->SetOutputs(outputs);

		m_pContext->m_buildScheduler.AddBuildTransform(pArchive, m_pContext);
	}

	DataStore::WriteData(GetOutputPath("DummyOutput"), "");

	return BuildTransformStatus::kOutputsUpdated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err ScheduleIngameShaders(const ToolParams &toolParams, const std::string &assetName, BuildTransformContext *pContext)
{
	INOTE_VERBOSE("Creating Build Transforms for Shaders...");

	const std::string filesJsonPath = GetSrcShaderAbsolutePath("files.json");

	std::string projectName = pContext->m_toolParams.m_gameName;
	BuildTransform_ParseIngameShaderFilesJson* pParseTransform = new BuildTransform_ParseIngameShaderFilesJson(projectName, pContext);

	const std::string outputPath = toolsutils::GetBuildPathNew() + BUILD_TRANSFORM_INGAME_SHADERS_FOLDER +
								   FileIO::separator + "dummy-output.txt";

	pParseTransform->AddInput(TransformInput(filesJsonPath, "FilesJson"));
	pParseTransform->SetOutput(TransformOutput(outputPath, "DummyOutput"));

	pContext->m_buildScheduler.AddBuildTransform(pParseTransform, pContext);

	return Err::kOK;
}
