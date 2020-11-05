/*
 * Copyright (c) 2018 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "tools/pipeline3/build/build-transforms/build-transform-ingame-shader.h"

#include "tools/libs/toolsutil/command-job.h"
#include "tools/libs/toolsutil/filename.h"
#include "tools/libs/toolsutil/sndbs.h"
#include "tools/libs/toolsutil/strfunc.h"

#include "tools/pipeline3/toolversion.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-parse-ingame-shaders.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/blobs/blob-cache.h"

#include <shader/binary.h>

/// --------------------------------------------------------------------------------------------------------------- ///
static const char *GetOrbisShaderOutputExtension(const std::string &profile)
{
	if (profile == "vs_5_0") return ".vxo";
	else if (profile == "ps_5_0") return ".pxo";
	else if (profile == "cs_5_0") return ".cxo";
	else if (profile == "gs_5_0") return ".gxo";
	else if (profile == "es_5_0") return ".vxo";

	IABORT("Unknown shader profile %s\n", profile.c_str());
	return "";
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const char *GetOrbisShaderProfile(const std::string &profile)
{
	if (profile == "vs_5_0") return "sce_vs_vs_orbis";
	else if (profile == "ps_5_0") return "sce_ps_orbis";
	else if (profile == "cs_5_0") return "sce_cs_orbis";
	else if (profile == "gs_5_0") return "sce_gs_on_chip_orbis";
	else if (profile == "es_5_0") return "sce_vs_es_on_chip_orbis";

	IABORT("Unknown shader profile %s\n", profile.c_str());
	return "";
}

/// --------------------------------------------------------------------------------------------------------------- ///
static std::string GetOutputPath(const IngameShadersConfig &config, const IngameShaderEntry &entry)
{
	return config.m_outputPath + FileIO::separator + entry.m_entryPoint + entry.m_entryPointSuffix +
		   GetOrbisShaderOutputExtension(entry.m_profile);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static std::string GetBaseCompilerArguments(const IngameShadersConfig &config, const IngameShaderEntry &entry)
{
	std::vector<std::string> arguments;

	if (entry.m_isAsm)
	{
		arguments.push_back(std::string("-e ") + entry.m_entryPoint);
		arguments.insert(arguments.end(), config.m_assemblerArgs.begin(), config.m_assemblerArgs.end());
	}
	else
	{
		arguments.push_back(std::string("-entry ") + entry.m_entryPoint);

		for (auto &def : entry.m_defines)
		{
			arguments.push_back(std::string("-D") + def);
		}

		arguments.push_back(std::string("-profile ") + GetOrbisShaderProfile(entry.m_profile));

		arguments.insert(arguments.end(), config.m_compilerArgs.begin(), config.m_compilerArgs.end());
		arguments.push_back(entry.m_arguments);
	}

	std::vector<std::string> args;
	for (auto &arg : arguments)
	{
		std::string a = toolsutils::Trim(arg, "\t\n\r ");
		if (!a.empty())
		{
			args.push_back(a);
		}
	}

	return toolsutils::Join(args, " ");
}

/// --------------------------------------------------------------------------------------------------------------- ///
static std::string FinalizeCompilerArguments(const IngameShadersConfig &config, const IngameShaderEntry &entry,
											 const BuildFile &src, const BuildPath &output, const std::string &args)
{
	std::string result;
	if (entry.m_isAsm)
	{
		result = config.m_shaderAssembler + " " + args + " -o " + output.AsAbsolutePath() + " " + src.AsAbsolutePath();
	}
	else
	{
		std::vector<std::string> includeArgs;

		for (auto &inc : config.m_forceIncludes)
		{
			includeArgs.push_back(std::string("-include ") + inc.AsAbsolutePath());
		}

		for (auto &inc : config.m_includePaths)
		{
			includeArgs.push_back(std::string("-I") + inc.AsAbsolutePath());
		}

		result = config.m_shaderCompiler + " " + toolsutils::Join(includeArgs, " ") + " " + args + " -o " + output.AsAbsolutePath() + " " + src.AsAbsolutePath();
	}

	return result;
}

 /// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_IngameShader::BuildTransform_IngameShader(const IngameShadersConfig &config,
														 const IngameShaderEntry &entry,
														 const BuildTransformContext *pContext)
	: BuildTransform("IngameShaders", pContext)
	, m_config(config)
	, m_entry(entry)
{
	m_preEvaluateDependencies.SetString("compiler-version", config.m_compilerVersion);

	m_baseCompilerArgs = GetBaseCompilerArguments(config, entry);

	if (!entry.m_isAsm)
	{
		for (U32 i = 0; i < config.m_includePaths.size(); i++)
		{
			std::stringstream name;
			name << "include-path-" << i;
			m_preEvaluateDependencies.SetString(name.str(), config.m_includePaths[i].AsPrefixedPath());
		}
	}

	m_preEvaluateDependencies.SetString("entry-point", entry.m_entryPoint + entry.m_entryPointSuffix);
	m_preEvaluateDependencies.SetString("arguments", m_baseCompilerArgs);
	m_preEvaluateDependencies.SetBool("is-asm", entry.m_isAsm);
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_IngameShader::Evaluate()
{
	const BuildFile &shaderFile = GetInput("Shader").m_file;
	std::string fileName = shaderFile.ExtractFilename();

	const BuildPath &output = GetOutputPath("Binary");
	std::string cmd = FinalizeCompilerArguments(m_config, m_entry, shaderFile, output, m_baseCompilerArgs);

	SnDbs::JobParams job;
	job.m_id = output.ExtractFilename();
	job.m_command = cmd;
	job.m_title =  m_entry.m_entryPoint + m_entry.m_entryPointSuffix;
	job.m_local = false;

	if (!SnDbs::AddJobToProject(m_config.m_sndbsProject, job))
	{
		IABORT("Failed to add job '%s' to SN-DBS project '%s'\n", job.m_title.c_str(), m_config.m_sndbsProject.c_str());
	}

	m_pContext->m_buildScheduler.RegisterSnDbsWaitItem(this, m_config.m_sndbsProject, job.m_id);

	return BuildTransformStatus::kResumeNeeded;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_IngameShader::ResumeEvaluation(const SchedulerResumeItem &resumeItem)
{
	const SnDbs::JobResult &result = resumeItem.m_snDbsResult;
	if (result.m_exitCode != 0)
	{
		IERR("Compiler exited with code %d!\nCommand: %s\nOutput: %s\n", result.m_exitCode,
			 result.m_command.c_str(), (result.m_stdOut + result.m_stdErr).c_str());
		return BuildTransformStatus::kFailed;
	}

	const BuildPath &output = GetOutputPath("Binary");
	std::vector<unsigned char> binary = FileIO::GetBinaryFileContent(output.AsAbsolutePath().c_str());
	DataStore::WriteData(output, OutputBlob(&binary[0], binary.size()));

	// Copy the shader sdb to a global location for razor.
	if (binary.size() >= sizeof(sce::Shader::Binary::Header))
	{
		sce::Shader::Binary::Header *pHeader = (sce::Shader::Binary::Header*)&binary[0];

		char hash[17];
		sprintf_s(hash, 17, "%08x%08x", pHeader->m_associationHash0, pHeader->m_associationHash1);
		std::string sdbFilename = m_entry.m_entryPoint + m_entry.m_entryPointSuffix + "_" + hash + ".sdb";
		std::string sdbPath = ConcatPath(output.AsAbsolutePath(), "../" + sdbFilename);

		if (FileIO::fileExists(sdbPath.c_str()))
		{
			std::string subDir = GetInputFile("Shader").ExtractFilename();
			if (toolsutils::HasExtension(subDir))
			{
				subDir = subDir.substr(0, subDir.rfind('.'));
			}

			char outputDir[256];
			snprintf(outputDir, 256, "x:/build/psslc-ingame-capture/%s/%s/%02x", subDir.c_str(),
					 (m_entry.m_entryPoint + m_entry.m_entryPointSuffix).c_str(),
					 pHeader->m_associationHash0 >> 24);

			Err err = FileIO::makeDirPath(outputDir);
			if (err.Succeeded())
			{
				std::string outputPath = ConcatPath(outputDir, sdbFilename);
				Err err = FileIO::moveFileWithRetry(outputPath.c_str(), sdbPath.c_str(), 200, false);
				if (err.Failed())
				{
					IERR("Failed to move shader sdb file after 10 attempts from %s to %s.",
						 sdbPath.c_str(), outputPath.c_str());
					return BuildTransformStatus::kFailed;
				}
			}
			else
			{
				IERR("Failed to make directories for sdb '%s'.", sdbPath.c_str());
				return BuildTransformStatus::kFailed;
			}
		}
	}

	return BuildTransformStatus::kOutputsUpdated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
std::vector<BuildTransform_IngameShader*> MakeIngameShadersTransform(const IngameShadersConfig &config,
																	 const BuildPath &shaderFile,
																	 const std::vector<BuildPath> &dependencies,
																	 const std::vector<IngameShaderEntry> &entries,
																	 const BuildTransformContext *pContext)
{
	std::vector<TransformInput> inputs;
	inputs.push_back(TransformInput(shaderFile, "Shader"));

	for (auto &dep : dependencies)
	{
		inputs.push_back(TransformInput(dep));
	}

	std::vector<BuildTransform_IngameShader*> transforms;
	for (auto &entry : entries)
	{
		BuildTransform_IngameShader *pXform = new BuildTransform_IngameShader(config, entry, pContext);

		pXform->SetInputs(inputs);
		pXform->SetOutput(TransformOutput(BuildPath(GetOutputPath(config, entry)), "Binary"));

		transforms.push_back(pXform);
	}

	return transforms;
}
