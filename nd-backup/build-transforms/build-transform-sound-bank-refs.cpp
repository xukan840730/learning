/*
* Copyright (c) 2015 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited.
*/

#include "tools/pipeline3/build/stdafx.h"

#include "tools/pipeline3/build/build-transforms/build-transform-sound-bank-refs.h"
#include "tools/libs/bigstreamwriter/ndi-bo-writer.h"
#include "tools/libs/toolsutil/color-display.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/toolversion.h"

//#pragma optimize("", off) // uncomment when debugging in release mode


//--------------------------------------------------------------------------------------------------------------------//
static void WriteSoundBankReferences(BigStreamWriter &stream, const std::vector<std::string>& soundBanks, const BuildTransformContext* pContext)
{
	INOTE_VERBOSE("Writing sound bank references...");

	size_t numSoundBanks = soundBanks.size();
	if (numSoundBanks > 0)
	{
		BigStreamWriter::Item* pItem = stream.StartItem(BigWriter::SOUND_BANK_TABLE);
		stream.AddLoginItem(pItem, BigWriter::LEVEL_OFFSET_PRIORITY);
		stream.Write4((U32)numSoundBanks);

		if (pContext->m_toolParams.m_streamConfig.m_pointerType == ICETOOLS::kLocTypeLink8BytePointer)
			// Pointer padding
			stream.Write4(0);

		std::vector<Location> soundBankLocs;
		for (size_t iSoundBank = 0; iSoundBank < numSoundBanks; iSoundBank++)
		{
			soundBankLocs.push_back(stream.WritePointer());
		}

		stream.WriteNullPointer();
		stream.EndItem();

		stream.StartItem();
		for (size_t iSoundBank = 0; iSoundBank < numSoundBanks; iSoundBank++)
		{
			stream.SetPointer(soundBankLocs[iSoundBank]);
			stream.WriteStr(soundBanks[iSoundBank]);
			INOTE_VERBOSE("  %s", soundBanks[iSoundBank].c_str());
		}
		stream.EndItem();
	}
	else
	{
		INOTE_VERBOSE("  No sound banks.");
	}
}


//--------------------------------------------------------------------------------------------------------------------//
static bool AddActorTxtContent(const std::string& actorName, 
							const BuildTransformContext *const pContext, 
							const std::vector<std::string>& soundBanks, 
							std::string& outActorTxt)
{
	INOTE_VERBOSE("Writing sound banks...");
	size_t numSoundBanks = soundBanks.size();
	if (numSoundBanks > 0)
	{
		// write to the .txt file for the actor the stream names
		for (size_t i = 0; i < numSoundBanks; i++)
		{
			std::string bankName = soundBanks[i];
			std::replace(bankName.begin(), bankName.end(), '\\', '/');

			size_t pos = bankName.find_last_of("/");
			if (pos == std::string::npos)
				pos = 0;
			else
				pos += 1;
			bankName = bankName.substr(pos);
			bankName = bankName.substr(0, bankName.find_last_of("."));

			outActorTxt += ("sound-bank " + bankName + "\n");
		}
	}

	return true;
}



//--------------------------------------------------------------------------------------------------------------------//
BuildTransform_SoundBankReferences::BuildTransform_SoundBankReferences(const std::string& actorName
																	, const BuildTransformContext *const pContext
																	, const std::vector<std::string>& soundBanks)
																	: BuildTransform("SoundBankRefs", pContext)
																	, m_soundBanks(soundBanks)
{
	SetDependencyMode(DependencyMode::kIgnoreDependency);
	m_preEvaluateDependencies.SetString("actorName", actorName);
}


//--------------------------------------------------------------------------------------------------------------------//
BuildTransformStatus BuildTransform_SoundBankReferences::Evaluate()
{
	BigStreamWriter stream(m_pContext->m_toolParams.m_streamConfig);

	WriteSoundBankReferences(stream, m_soundBanks, m_pContext);

	// Write the .bo file.
	NdiBoWriter boWriter(stream);
	boWriter.Write();

	const BuildPath& boPath = GetOutputPath("SoundBankRefsBo");
	DataStore::WriteData(boPath, boWriter.GetMemoryStream());

	// Add to the dependency txt file
	const std::string& actorName = m_preEvaluateDependencies.GetValue("actorName");
	std::string outActorTxt;
	if (!AddActorTxtContent(actorName, m_pContext, m_soundBanks, outActorTxt))
	{
		return BuildTransformStatus::kFailed;
	}

	const BuildPath& actorTxtPath = GetOutputPath("SoundBankRefsActorTxt");
	DataStore::WriteData(actorTxtPath, outActorTxt);

	return BuildTransformStatus::kOutputsUpdated;
}


//--------------------------------------------------------------------------------------------------------------------//
void BuildTransformSoundBankRefs_Configure(const BuildTransformContext *const pContext,
										const libdb2::Actor *const pDbActor,
										const std::vector<const libdb2::Actor*>& actorList,
										std::vector<std::string>& arrBoFiles)
{
	std::vector<std::string> soundBankRefs;
	size_t numActors = actorList.size();
	for (size_t iActor = 0; iActor < numActors; iActor++)
	{
		const libdb2::Actor* dbactor = actorList[iActor];
		if (dbactor)
		{
			if (!dbactor->m_soundBank.m_filename.empty())
				soundBankRefs.push_back(dbactor->m_soundBank.m_filename);
			for (size_t iSndBank = 0; iSndBank < dbactor->m_soundBankList.size(); ++iSndBank)
			{
				if (!dbactor->m_soundBankList[iSndBank].m_filename.empty())
					soundBankRefs.push_back(dbactor->m_soundBankList[iSndBank].m_filename);
			}
		}
	}

	if (!soundBankRefs.empty())
	{
		// Sort and remove duplicates...
		std::sort(soundBankRefs.begin(), soundBankRefs.end());
		soundBankRefs.erase(std::unique(soundBankRefs.begin(), soundBankRefs.end()), soundBankRefs.end());

		BuildTransform_SoundBankReferences *const pSoundBankRefs = new BuildTransform_SoundBankReferences(pDbActor->Name(), pContext, soundBankRefs);
		
		const std::string soundBankRefsBoFilename = pContext->m_toolParams.m_buildPathSoundBankRefs + pDbActor->Name() + ".bo";
		pSoundBankRefs->AddOutput(TransformOutput(soundBankRefsBoFilename, "SoundBankRefsBo"));

		const std::string soundBankRefsActorTxtFilename = pContext->m_toolParams.m_buildPathSoundBankRefs + pDbActor->Name() + ".txt";
		pSoundBankRefs->AddOutput(TransformOutput(soundBankRefsActorTxtFilename, "SoundBankRefsActorTxt"));

		pContext->m_buildScheduler.AddBuildTransform(pSoundBankRefs, pContext);
		arrBoFiles.push_back(soundBankRefsBoFilename);
	}
}
