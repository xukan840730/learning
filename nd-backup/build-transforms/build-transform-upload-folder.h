#pragma once

#include "build-transform.h"


class BuildTransform_UploadFolder : public BuildTransform
{
public:
	BuildTransform_UploadFolder(
		const BuildTransformContext* pContext,
		const std::string& baseSrcPath,
		const std::string& baseDestPath,
		const std::vector<std::string>& allowedExtensions,
		bool usePostFixedPath = true);
	virtual ~BuildTransform_UploadFolder();

	BuildTransformStatus Evaluate() override;

private:

	std::set<std::string> m_availableFiles;

	const std::string m_baseSrcPath;
	const std::string m_baseDestPath;
	const std::vector<std::string> m_allowedExtensions;
	bool m_usePostFixedPath;
};


