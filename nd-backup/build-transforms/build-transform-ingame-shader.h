/*
 * Copyright (c) 2018 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform.h"

#include <vector>

/// --------------------------------------------------------------------------------------------------------------- ///
struct IngameShadersConfig
{
	std::string m_shaderCompiler;
	std::string m_shaderAssembler;
	std::string m_psarcArchiver;

	std::vector<BuildPath> m_includePaths;
	std::vector<BuildPath> m_forceIncludes;
	std::vector<std::string> m_compilerArgs;

	std::vector<std::string> m_assemblerArgs;

	std::string m_compilerVersion;

	std::string m_outputPath;

	std::string m_sndbsProject;
};

struct IngameShaderEntry
{
	std::string m_entryPoint;
	std::string m_entryPointSuffix;

	std::string m_profile;

	std::string m_arguments;
	std::vector<std::string> m_defines;

	bool m_isAsm = false;
};

 /// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_IngameShader : public BuildTransform
{
public:
	BuildTransform_IngameShader(const IngameShadersConfig &config,
								const IngameShaderEntry &entry,
								const BuildTransformContext *pContext);

	BuildTransformStatus Evaluate() override;
	BuildTransformStatus ResumeEvaluation(const SchedulerResumeItem &resumeItem) override;

private:
	IngameShadersConfig m_config;
	IngameShaderEntry m_entry;
	std::string m_baseCompilerArgs;
};

/// --------------------------------------------------------------------------------------------------------------- ///
std::vector<BuildTransform_IngameShader*> MakeIngameShadersTransform(const IngameShadersConfig &config,
																	 const BuildPath &shaderFile,
																	 const std::vector<BuildPath> &dependencies,
																	 const std::vector<IngameShaderEntry> &entries,
																	 const BuildTransformContext *pContext);
