/*
* Copyright (c) 2018 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "tools/pipeline3/toolversion.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-actor-txt.h"
#include "tools/pipeline3/build/build-transforms/build-scheduler.h"

#include "tools/libs/libdb2/db2-actor.h"

//#pragma optimize("", off)

BuildTransform_ActorTxt::BuildTransform_ActorTxt(const BuildTransformContext *const pContext
												, const std::string& animActorTxtEntries)
												: BuildTransform("ActorTxt", pContext)
{
	m_preEvaluateDependencies.SetString("animActorTxtEntries", animActorTxtEntries);
}

BuildTransformStatus BuildTransform_ActorTxt::Evaluate()
{
	std::string txtContent = m_preEvaluateDependencies.GetValue("animActorTxtEntries");

	if (DoesInputExist("LightsAnimActorTxt"))
	{
		const BuildFile& actorTxtFile = GetInputFile("LightsAnimActorTxt");
		std::string actorTxt;
		DataStore::ReadData(actorTxtFile, actorTxt);

		txtContent += actorTxt;
	}

	if (DoesInputExist("SoundBankRefsActorTxt"))
	{
		const BuildFile& actorTxtFile = GetInputFile("SoundBankRefsActorTxt");
		std::string actorTxt;
		DataStore::ReadData(actorTxtFile, actorTxt);

		txtContent += actorTxt;
	}

	//push the list of package to level def file
	const std::vector<TransformInput>& allInputs = GetInputs();
	for (const TransformInput& input : allInputs)
	{
		const BuildFile& buildFile = input.m_file;
		const std::string prefixedFilename = buildFile.AsPrefixedPath();
		if (toolsutils::EndsWith(prefixedFilename, ".pak"))
		{
			txtContent += "package " + buildFile.ExtractFilename() + "\n";
		}
	}

	DataStore::WriteData(GetOutputs()[0].m_path, txtContent);
	DataStore::WriteData(GetOutputs()[1].m_path, txtContent);

	return BuildTransformStatus::kOutputsUpdated;
}

void BuildTransformActorTxt_Configure(const libdb2::Actor *const pDbActor,
									const BuildTransformContext *const pContext,
									const std::vector<std::string>& pakFilenames,
									const std::string& animActorTxtEntries,
									const std::vector<std::string>& soundBankRefsBoFiles,
									U32 numLightSkels)
{
	const std::string actorName = pDbActor->Name();
	const TransformOutput actorTxtPath(toolsutils::GetBuildOutputDir() + ACTORFOLDER + FileIO::separator + actorName + ".txt");
	const TransformOutput assetTxtPath(toolsutils::GetBuildOutputDir() + ASSETFOLDER + FileIO::separator + actorName + ".txt");

	std::vector<TransformOutput> outputs = { actorTxtPath, assetTxtPath };

	BuildTransform_ActorTxt *const pActorTxt = new BuildTransform_ActorTxt(pContext, animActorTxtEntries);

	for(const std::string& pak : pakFilenames)
		pActorTxt->AddInput(TransformInput(pak));

	if (numLightSkels > 0)
	{
		const std::string lightsAnimActorTxtFilename = pContext->m_toolParams.m_lightsBoPath + pDbActor->Name() + ".txt";
		pActorTxt->AddInput(TransformInput(lightsAnimActorTxtFilename, "LightsAnimActorTxt"));
	}

	if (soundBankRefsBoFiles.size() > 0)
	{
		const std::string soundBankRefsActorTxtFilename = pContext->m_toolParams.m_buildPathSoundBankRefs + pDbActor->Name() + ".txt";
		pActorTxt->AddInput(TransformInput(soundBankRefsActorTxtFilename, "SoundBankRefsActorTxt"));
	}

	pActorTxt->SetOutputs(outputs);

	pContext->m_buildScheduler.AddBuildTransform(pActorTxt, pContext);
}