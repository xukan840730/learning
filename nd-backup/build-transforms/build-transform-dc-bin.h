/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform.h"

#include "tools/pipeline3/common/dco.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_DcBin : public BuildTransform
{
public:
	BuildTransform_DcBin(const DcoConfig& dcoConfig,
						 const DcoBuildEntry& entry,
						 const std::string& projectName,
						 const std::string& templateDir,
						 const std::string& snDbsProjectName,
						 const BuildTransformContext* pContext);

	~BuildTransform_DcBin() {}

	virtual BuildTransformStatus Evaluate() override;
	virtual BuildTransformStatus ResumeEvaluation(const SchedulerResumeItem& resumeItem) override;

private:
	bool OnBuildComplete();
	void RegisterDcxDependencies(std::vector<DcoSubDepEntry>& deps);

	DcoConfig m_dcoConfig;
	DcoBuildEntry m_buildEntry;
	std::string m_rootProjectName;
	std::string m_templateDir;
	std::string m_snDbsProjectName;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_DcModulesBin : public BuildTransform
{
public:
	BuildTransform_DcModulesBin(const DcoConfig& dcoConfig,
								const std::string& modulesName,
								const std::string& sndbsProject,
								const std::string& projectName,
								const BuildTransformContext* pContext);

	BuildTransformStatus Evaluate() override;

private:
	std::string WriteToTempFile(const std::string& content) const;

	DcoConfig m_dcoConfig;
	std::string m_modulesName;
	std::string m_sndbsProject;
	std::string m_rootProjectName;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_ArchiveStartupBinsSpawner : public BuildTransform
{
public:
	BuildTransform_ArchiveStartupBinsSpawner(const std::string& projectName,
											 const BuildTransformContext* pContext);

	BuildTransformStatus Evaluate() override;

private:
	std::string m_rootProjectName;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_ArchiveStartupBins : public BuildTransform
{
public:
	BuildTransform_ArchiveStartupBins(const BuildTransformContext* pContext);

	BuildTransformStatus Evaluate() override;
};
