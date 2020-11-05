/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/stdafx.h"
#include "tools/pipeline3/build/build-transforms/build-transform-actor-runtime-flags.h"

#include "tools/libs/bigstreamwriter/ndi-bo-writer.h"
#include "tools/libs/toolsutil/simpledb.h"
#include "tools/pipeline3/common/4_gameflags.h"
#include "tools/bamutils/common.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/ingame3pak/spawner.h"
#include "tools/libs/toolsutil/json_helpers.h"

#include "3rdparty/rapidjson/include/rapidjson/document.h"
#include "icelib/itscene/ourstudiopolicy.h"

//#pragma optimize("", off) // uncomment when debugging in release mode

/// --------------------------------------------------------------------------------------------------------------- ///
using std::vector;

using toolsutils::SimpleDependency;
using namespace toolsutils;

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_ActorRuntimeFlags : public BuildTransform
{
public:
	enum OutputIndex
	{
		BoFilename = 0,
		OutputCount
	};

	BuildTransform_ActorRuntimeFlags(const std::string& actorName, 
									const std::string& gameFlagsStr,
									const BuildTransformContext *const pContext) 
		: BuildTransform("ActorRuntimeFlags", pContext)
	{
		m_preEvaluateDependencies.SetString("actorName", actorName);
		m_preEvaluateDependencies.SetString("gameFlagsInfo.gameFlags", gameFlagsStr);

		rapidjson::Document doc;
		const string& gameflags = gameFlagsStr;
		toolsutils::json_helpers::LoadJsonDocument(doc, "gameflags", gameflags.data(), gameflags.data() + gameflags.size());

		std::string tags;
		const rapidjson::Value& tagsValue = toolsutils::json_helpers::TryGetValue(doc, "ActorTags");
		if (tagsValue != toolsutils::json_helpers::s_nullValue)
		{
			tags = toolsutils::json_helpers::makeString(tagsValue);
		}
	}

	BuildTransformStatus Evaluate() override
	{
 		const ToolParams& tool = m_pContext->m_toolParams;
		FileIO::makeDirPath(tool.m_buildPathActorRuntimeFlagsBo.c_str());

		const std::string& gameFlagsStr = m_preEvaluateDependencies.GetValue("gameFlagsInfo.gameFlags");
		const string& gameflags = gameFlagsStr;

		rapidjson::Document doc;
		toolsutils::json_helpers::LoadJsonDocument(doc, "gameflags", gameflags.data(), gameflags.data() + gameflags.size());
		
		std::string tags;
		const rapidjson::Value& tagsValue = toolsutils::json_helpers::TryGetValue(doc, "ActorTags");
		if (tagsValue != toolsutils::json_helpers::s_nullValue)
		{
			tags = toolsutils::json_helpers::makeString(tagsValue);
		}

		BigStreamWriter stream(tool.m_streamConfig);

		if (!tags.empty())
		{
			const std::string& actorName = m_preEvaluateDependencies.GetValue("actorName");
			const std::string itemName = "actor-flags." + actorName;
			BigStreamWriter::Item *const pItem = stream.StartItem(BigWriter::ACTOR_FLAGS, itemName, itemName);
			
			stream.AlignItem(8);

			TagTable tagTable;
			tagTable.ReadSemicolonDelimited(tags);

			SchemaPropertyTable props;
			for (const TagData& tag : tagTable.m_tags)
			{
				props.Add(tag, tag.m_value);
			}
			props.Sort();

			props.Emit(stream, kLocationNull, "global");

			stream.EndItem();
			stream.AddLoginItem(pItem, BigWriter::TAG_PRIORITY);
		}

		NdiBoWriter boWriter(stream);
		boWriter.Write();

		const BuildPath& boFilename = GetOutputs()[OutputIndex::BoFilename].m_path;
		DataStore::WriteData(boFilename, boWriter.GetMemoryStream());

		return BuildTransformStatus::kOutputsUpdated;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildModuleActorRuntimeFlags_Configure(const BuildTransformContext* pContext,
									const std::vector<const libdb2::Actor*>& actorList,
									std::vector<std::string>& outBoFiles)
{
	const ToolParams& tool = pContext->m_toolParams;

	std::vector<TransformInput> inputs;
	std::vector<TransformOutput> outputs;
	for (const libdb2::Actor *const pActor : actorList)
	{
		if (pActor->m_lodIndex != 0)
		{
			continue;
		}

		const std::string& gameFlagsStr = pActor->m_gameFlagsInfo.m_gameFlags;
		const string& gameflags = gameFlagsStr;
		if (!gameflags.empty())
		{
			const std::string boFileName = tool.m_buildPathActorRuntimeFlagsBo + pActor->FullNameNoLod() + ".bo";
			outputs.resize(BuildTransform_ActorRuntimeFlags::OutputCount);
			outputs[BuildTransform_ActorRuntimeFlags::BoFilename] = TransformOutput(BuildPath(boFileName));

			auto *const pXform = new BuildTransform_ActorRuntimeFlags(pActor->Name(), pActor->m_gameFlagsInfo.m_gameFlags, pContext);
			pXform->SetOutputs(outputs);

			pContext->m_buildScheduler.AddBuildTransform(pXform, pContext);

			outBoFiles.push_back(boFileName);
		}
	}
}
