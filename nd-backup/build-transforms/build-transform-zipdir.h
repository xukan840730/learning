/*
* Copyright (c) 2018 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/
#pragma once

#include <vector>
#include <string>
#include "build-transform.h"

class ZipDirTransform : public BuildTransform
{
public:
	ZipDirTransform(const std::string& root, const std::string& folder);
	virtual BuildTransformStatus Evaluate() override;
	virtual BuildTransformStatus ResumeEvaluation(const SchedulerResumeItem& resumeItem) override;
	void PopulatePreEvalDependencies();
protected:
	static int EvaluateInThread(toolsutils::SafeJobBase* pJob);
	std::vector<std::string> m_files;
	std::string m_rootFolder;
	std::string m_folderToZip;
};


class ZipAppDirTransform : public ZipDirTransform
{
	// This transform is taylored to only zip the files we would want to publish for an .app
	bool m_localexe;
public:
	ZipAppDirTransform(const std::string& root, const std::string& folder);	
	void PopulatePreEvalDependencies();
};

