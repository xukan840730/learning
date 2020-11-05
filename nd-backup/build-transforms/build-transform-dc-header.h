/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform.h"

#include "tools/pipeline3/common/dco.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ToolParams;

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_DcHeader : public BuildTransform
{
public:
	BuildTransform_DcHeader(const DcoConfig& dcoConfig,
							const DcoBuildHeaderEntry& entry,
							const std::string& projectName,
							const std::string& templateDir,
							const std::string& snDbsProjectName,
							const BuildTransformContext* pContext);

	~BuildTransform_DcHeader() {}

	virtual BuildTransformStatus Evaluate() override;
	virtual BuildTransformStatus ResumeEvaluation(const SchedulerResumeItem& resumeItem) override;

private:
	bool OnBuildComplete();
	void RegisterDcxDependencies();

	DcoConfig m_dcoConfig;
	DcoBuildHeaderEntry m_buildEntry;
	std::string m_rootProjectName;
	std::string m_templateDir;
	std::string m_snDbsProjectName;
};
