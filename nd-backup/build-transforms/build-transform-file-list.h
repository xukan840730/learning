/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/build/build-transforms/build-transform.h"
#include "tools/pipeline3/build/build-transforms/build-transform-spawner.h"
#include <vector>

/// --------------------------------------------------------------------------------------------------------------- ///

class BuildTransform_FileList : public BuildTransform
{
public:
	BuildTransform_FileList()
		: BuildTransform("FileList")
	{}

	BuildTransform_FileList(const std::string& fileListPath)
		: BuildTransform("FileList")
	{
		SetOutput(TransformOutput(fileListPath));
	}

	BuildTransform_FileList(const std::vector<std::string>& inputFileNames, const std::string& fileListPath)
		: BuildTransform("FileList")
	{
		SetInputFileNames(inputFileNames);

		SetOutput(TransformOutput(fileListPath));
	}

	void SetInputFileNames(const std::vector<std::string>& inputFileNames)
	{
		std::vector<TransformInput> inputs;
		inputs.reserve(inputFileNames.size());

		for (const std::string& input : inputFileNames)
		{
			inputs.push_back(TransformInput(input));
		}

		SetInputs(inputs);
	}

	virtual BuildTransformStatus Evaluate() override
	{
		std::vector<BuildFile> files;
		files.reserve(GetInputs().size());

		for (const TransformInput& input : GetInputs())
		{
			files.push_back(input.m_file);
		}

		WriteBuildFileList(GetFirstOutputPath(), files);
		return BuildTransformStatus::kOutputsUpdated;
	}
};

struct BuildTransform_FileListSpawnDesc : public BuildTransformSpawnDesc
{
	BuildTransform_FileListSpawnDesc()
		: BuildTransformSpawnDesc("BuildTransform_FileList")
	{}

	virtual BuildTransform *CreateTransform(const BuildTransformContext *pContext) const override
	{
		return new BuildTransform_FileList;
	}
};

inline BuildTransform_FileListSpawnDesc *CreateFileListSpawnDesc(const std::vector<std::string> &inputFilenames, const std::string &output)
{
	std::vector<TransformInput> inputs;
	inputs.reserve(inputFilenames.size());

	for (const std::string& input : inputFilenames)
	{
		inputs.push_back(TransformInput(input));
	}

	BuildTransform_FileListSpawnDesc *pDesc = new BuildTransform_FileListSpawnDesc;
	pDesc->m_inputs = inputs;
	pDesc->m_outputs = { TransformOutput(output) };

	return pDesc;
}