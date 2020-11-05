/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/stdafx.h"
#include "tools/pipeline3/build/build-transforms/build-transform-pak.h"

#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/build/tool-params.h"
#include "tools/pipeline3/toolversion.h"

#include "tools/libs/bigstreamwriter/ndi-pak-writer.h"
#include "tools/libs/bolink/bolink.h"
#include "tools/libs/toolsutil/color-display.h"
#include "tools/libs/toolsutil/filename.h"
#include "tools/libs/toolsutil/strfunc.h"

#include "common/mysqlutil/mysql.h"

#include "icelib/ndb/ndbmemorystream-helpers.h"

//#pragma optimize("", off)

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_Pak::BuildTransform_Pak(const BuildTransformContext* pContext,
									   const BigStreamWriterConfig& streamConfig,
									   I16 pakHeaderFlags)
	: BuildTransform("Pak", pContext), m_pakHeaderFlags(pakHeaderFlags), m_streamConfig(streamConfig)
{}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_Pak::Evaluate()
{
	std::vector<std::unique_ptr<NdbMemoryStream>> boStreams;
	std::vector<std::pair<NdbMemoryStream*, std::string>> arrBoStreams;

	arrBoStreams.reserve(GetInputs().size());
	bool hadError = false;
	for (const auto& input : GetInputs())
	{
		if (IsBuildFileList(input.m_file))
		{
			std::vector<BuildFile> fileList;
			ExtractBuildFileList(input.m_file, fileList);
			for (const auto& file : fileList)
			{
				boStreams.emplace_back(new NdbMemoryStream());
				arrBoStreams.emplace_back(boStreams.back().get(), file.AsAbsolutePath());

				DataStore::ReadData(file, *boStreams.back());
			}
		}
		else
		{
			boStreams.emplace_back(new NdbMemoryStream());
			arrBoStreams.emplace_back(boStreams.back().get(), input.m_file.AsAbsolutePath());

			DataStore::ReadData(input.m_file, *boStreams.back());
		}
	}

	if (hadError)
	{
		return BuildTransformStatus::kFailed;
	}

	BigStreamWriter streamWriter(m_streamConfig);
	Err boLinkRet = ::BoLink(arrBoStreams, m_streamConfig, streamWriter, NULL, NULL);
	if (boLinkRet.Failed())
		return BuildTransformStatus::kFailed;

	NdiPakWriter pakWriter(streamWriter, m_pakHeaderFlags, streamWriter.GetTargetEndianness());
	if (!pakWriter.Write())
		return BuildTransformStatus::kFailed;

	const BuildPath& pakFilePath = GetOutputPath("PakFile");
	DataStore::WriteData(pakFilePath, pakWriter.GetMemoryStream());

	return BuildTransformStatus::kOutputsUpdated;
}


/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransformPak_Configure(const BuildTransformContext* pContext, const std::string& pakFilename, const std::vector<std::string>& arrBoFiles, U32 pakHdrFlags)
{
	BuildTransform_Pak* pPakXform = new BuildTransform_Pak(pContext, pContext->m_toolParams.m_streamConfig, pakHdrFlags);
	std::vector<TransformInput> pakInputs;
	for (auto boFile : arrBoFiles)
	{
		pakInputs.push_back(TransformInput(boFile));
	}
	pPakXform->SetInputs(pakInputs);
	pPakXform->SetOutput(TransformOutput(pakFilename, "PakFile", TransformOutput::kIncludeInManifest));
	pContext->m_buildScheduler.AddBuildTransform(pPakXform, pContext);
}
