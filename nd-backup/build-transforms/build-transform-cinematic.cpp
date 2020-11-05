/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/build-transforms/build-transform-cinematic.h"
#include "tools/pipeline3/build/build-transforms/build-transform-skeleton.h"
#include "tools/pipeline3/build/build-transforms/build-scheduler.h"
#include "tools/libs/bigstreamwriter/ndi-bo-writer.h"
#include "tools/libs/toolsutil/color-display.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/util/dependency-database-manager.h"

//#pragma optimize("", off) // uncomment when debugging in release mode




enum _CinematicTransitionType // IMPORTANT: this enum must match the in-game enum _CinematicTransitionType (cinematic-def.h)
{
	kCinematicTransitionTypeNone,
	kCinematicTransitionTypeCrossFade,
	kCinematicTransitionTypeWipe_RL,
	kCinematicTransitionTypeWipe_LR
};
typedef U32 CinematicTransitionType;

static CinematicTransitionType CinematicTransitionTypeFromString(const std::string& str)
{
	if (str == "crossfade")
		return kCinematicTransitionTypeCrossFade;
	else if (str == "wipe")
		return kCinematicTransitionTypeWipe_RL;
	else if (str == "wipelr")
		return kCinematicTransitionTypeWipe_LR;
	else
		return kCinematicTransitionTypeNone;
}

enum _CinematicTransitionPos
{
	kCinematicTransitionPosLastSequence,
	kCinematicTransitionPosFirstSequence,
};
typedef U32 CinematicTransitionPos;

static CinematicTransitionPos CinematicTransitionPosFromString(const std::string& str)
{
	if (str == "first")
		return kCinematicTransitionPosFirstSequence;
	//else if (str == "last")
	//	return kCinematicTransitionPosLastSequence;
	else
		return kCinematicTransitionPosLastSequence;
}

enum _CinematicCameraCut
{
	kCinematicCameraCutDefault, // default means yes on first frame of cinematic, and yes on last frame of all sequences *except* the last one (== 0 for backward compat)
	kCinematicCameraCutYes,
	kCinematicCameraCutNo,
};
typedef U32 CinematicCameraCut;

static CinematicCameraCut CinematicCameraCutFromString(const std::string& str)
{
	if (str == "no")
		return kCinematicCameraCutNo;
	else if (str == "yes")
		return kCinematicCameraCutYes;
	else
		return kCinematicCameraCutDefault;
}


//--------------------------------------------------------------------------------------------------------------------//
struct BaCinematicBinding : public libdb2::CinematicBinding
{
	BaCinematicBinding() : libdb2::CinematicBinding() {} // do-nothing ctor

	BaCinematicBinding(const libdb2::CinematicSequence& sequence, const libdb2::Anim* pAnim)
		: libdb2::CinematicBinding(sequence, pAnim)
		, m_aliasNameLoc(kLocationInvalid)
		, m_disabled(pAnim->m_flags.m_Disabled)
	{}

	BaCinematicBinding(const std::string& lightAnimName) : m_aliasNameLoc(kLocationInvalid), m_disabled(false)
	{
		m_animName = lightAnimName;
		m_aliasName = lightAnimName.substr(0, lightAnimName.find('='));
		m_lookName = "";
		m_audioStem = "";
		m_parentToAlias = "";
		m_parentJointOrAttach = "";
		m_attachSysId = "";
		m_tintRandomizer = (U32)-1;
	}

	Location m_aliasNameLoc;
	bool m_disabled;
};

static int DetermineSequenceIndex(const std::string& seqName)
{
	int seqIndex = -1;
	std::string::size_type iSuffix = seqName.find_last_of('=');
	if (iSuffix != std::string::npos)
	{
		std::string suffix = seqName.substr(iSuffix + 1);
		sscanf(suffix.c_str(), "%d", &seqIndex);
	}
	return seqIndex;
}

static bool CompareSequenceNames(const std::string& a, const std::string& b)
{
	int seqIndexA = DetermineSequenceIndex(a);
	int seqIndexB = DetermineSequenceIndex(b);
	return (seqIndexA < seqIndexB);
}

static const std::string GetAnimNameWithoutSequenceIndex(const std::string& fullAnimName)
{
	std::string animNameWithoutSeqIndex = fullAnimName;
	std::string::size_type iSuffix = fullAnimName.find_last_of('=');
	if (iSuffix != std::string::npos)
	{
		animNameWithoutSeqIndex = fullAnimName.substr(0, iSuffix);
	}
	return animNameWithoutSeqIndex;
}

static const std::string GetBindingNameFromAnimName(const std::string& fullAnimName)
{
	std::string bindingName = fullAnimName;
	std::string::size_type iSuffix = fullAnimName.find_first_of('=');
	if (iSuffix != std::string::npos)
	{
		bindingName = fullAnimName.substr(0, iSuffix);
	}
	return bindingName;
}


static U32 HexToU32(const char* hex)
{
	U32 val;
	sscanf(hex, "%X", &val);
	return val;
}

enum SkipDirection { kForward, kReverse };

static const char* SkipWhitespace(const char* p, SkipDirection direction = kForward)
{
	int incr = (direction == kReverse) ? -1 : 1;
	while (*p != '\0' && isspace(*p))
	{
		p += incr;
	}
	return p;
}

static const char* SkipHexDigits(const char* p, SkipDirection direction = kForward)
{
	int incr = (direction == kReverse) ? -1 : 1;
	while (*p != '\0' && ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'F') || (*p >= 'a' && *p <= 'f')))
	{
		p += incr;
	}
	return p;
}

static inline StringId64 ConvertAudioLineToHexCode(const char* audioName)
{
	return StringToStringId64(audioName);
}


//--------------------------------------------------------------------------------------------------------------------//
static void WriteStringVectorAsStringIds(BigStreamWriter& stream, Location loc, std::vector<std::string> const& strList)
{
	stream.StartItem();
	stream.Align(kAlignDefaultLink);
	stream.SetPointer(loc);
	for (std::vector<std::string>::const_iterator it = strList.begin(); it != strList.end(); it++)
	{
		const StringId64 sid = StringToStringId64(it->c_str(), true);
		stream.WriteStringId64(sid);
	}
	stream.EndItem();
}


//--------------------------------------------------------------------------------------------------------------------//
static U32 WriteCinematicItems(BuildTransform_Cinematic *const pCineXform, 
							BigStreamWriter &stream, 
							const BuildTransformContext *const pContext, 
							const libdb2::Actor *const pDbActor)
{
	U32 metadataMemoryUsage = 0;

	const libdb2::Cinematic& dbcinematic = pDbActor->m_cinematic;
	if (dbcinematic.Loaded())
	{
		INOTE_VERBOSE("    Gathering cinematic info...\n");

		std::vector<std::string> levelNames;
		std::vector<std::string> sequenceNames;
		std::vector<std::string> bindingNames;
		std::vector<std::string> animationNames;
		std::vector<std::string> seqAnimationNames;

		size_t numLevelIds = dbcinematic.m_levels.size();
		for (int i = 0; i < numLevelIds; ++i)
		{
			const std::string& levelFileRelPath = dbcinematic.m_levels[i].m_value;
			const char* levelFileRelPathCstr = levelFileRelPath.c_str();
			const char* levelFile = FileIO::extractFilename(levelFileRelPathCstr);
			if (levelFile != NULL)
			{
				const char* ext = FileIO::extractLongestExtension(levelFile);
				if (ext != NULL)
				{
					--ext; // back up to include the dot
					ASSERT(*ext == '.');
					size_t iFilename = levelFile - levelFileRelPathCstr;
					size_t iExt = ext - levelFileRelPathCstr;
					std::string levelName = levelFileRelPath.substr(iFilename, iExt - iFilename);

					levelNames.push_back(levelName);
				}
			}
		}
		numLevelIds = levelNames.size();

		const std::string& builderAssetPath = pDbActor->TypedDbPath();
		const char* actorFullNameCstr = builderAssetPath.c_str();
		const char* filename = FileIO::extractFilename(actorFullNameCstr);
		if (filename != NULL)
		{
			std::string actorParentFolderRelPath = builderAssetPath.substr(0, filename - actorFullNameCstr);

			const std::string kSequencePrefix("seq-");

			// populate the sequences array and note the minimum sequence index
			libdb2::ActorRefList::const_iterator it, it_end;
			it_end = pDbActor->m_actorsDependency.end();
			it = pDbActor->m_actorsDependency.begin();
			for (; it != it_end; it++)
			{
				std::string dependentName = (*it).GetRefTargetName();
				if (dependentName.substr(0, kSequencePrefix.length()) == kSequencePrefix)
				{
					sequenceNames.push_back(dependentName);

					std::string path = actorParentFolderRelPath + dependentName + ".actor.xml";
					const libdb2::Actor* pSeqActor = libdb2::GetActor(path);
					if (pSeqActor)
					{
						//push the sequence->cinematic dependency to the dependency db.
						pCineXform->GetAssetDependencies().AddAssetDependency(pSeqActor->DiskPath().c_str(), pDbActor->DiskPath().c_str(), "actor");

						const libdb2::AnimList &dbanimList = pSeqActor->m_animations;
						for (libdb2::AnimList::const_iterator it = dbanimList.begin(), itEnd = dbanimList.end(); it != itEnd; ++it)
						{
							auto dbanim = *it;

							// skip any disabled animations
							//if (dbanim.m_flags.m_Disabled) // NO, include all animation names -- disabled bindings know they are disabled at runtime
							//	continue;

							//INOTE("  Cinematic: sequence %s, animation %s", dependentName.c_str(), dbanim.Name().c_str());
							// strip off the "=XXX" sequence index
							std::string animName = dbanim->Name();
							std::string bindingName = GetBindingNameFromAnimName(animName);
							{
								std::vector<std::string>::const_iterator itFind = std::find(bindingNames.begin(), bindingNames.end(), bindingName);
								if (itFind == bindingNames.end())
								{
									bindingNames.push_back(bindingName);
								}
							}
						}
					}
				}
			}
		}

		if (sequenceNames.size() != 0)
		{
			// order the sequences properly
			std::sort(sequenceNames.begin(), sequenceNames.end(), CompareSequenceNames);
		}

		for (std::vector<std::string>::const_iterator it = sequenceNames.begin(); it != sequenceNames.end(); it++)
		{
			std::string::size_type iSuffix = it->find_last_of('=');
			if (iSuffix != std::string::npos)
			{
				std::string indexStr = it->substr(iSuffix + 1);
				bindingNames.push_back("light-skel" + indexStr);
			}
		}

		// build up a complete list of all animation names which SHOULD theoretically be in this
		// cinematic -- some may not actually exist because some may be omitted or disabled in Builder
		for (std::vector<std::string>::const_iterator itBinding = bindingNames.begin(); itBinding != bindingNames.end(); itBinding++)
		{
			// add the name WITHOUT the "=XXX" sequence index
			std::string animName = *itBinding + "=" + pDbActor->Name();
			animationNames.push_back(animName);
		}
		
		for (std::vector<std::string>::const_iterator it = sequenceNames.begin(); it != sequenceNames.end(); it++)
		{
			std::string::size_type iSuffix = it->find_last_of('=');
			if (iSuffix != std::string::npos)
			{
				std::string indexStr = it->substr(iSuffix + 1);

				for (std::vector<std::string>::const_iterator itBinding = bindingNames.begin(); itBinding != bindingNames.end(); itBinding++)
				{
					// add the name WITH the "=XXX" sequence index
					const std::string animName = *itBinding + "=" + pDbActor->Name() + "=" + indexStr;
					seqAnimationNames.push_back(animName);
				}
			}
		}

		size_t numSequenceIds = sequenceNames.size();
		size_t numAnimationIds = animationNames.size();
		size_t numSeqAnimationIds = seqAnimationNames.size();
		if (numLevelIds + numSequenceIds + numAnimationIds + numSequenceIds != 0)
		{
			DisplayInfo("    Writing cinematic data...\n");

			BigStreamWriter::Item* pItem = stream.StartItem(BigWriter::CINEMATIC_1, pDbActor->Name()); // in-engine: class CinematicItem : public ResItem
			stream.AddLoginItem(pItem, BigWriter::LEVEL_OFFSET_PRIORITY);

			stream.Write4U(0); // deprecated old audioId

			stream.Write4((I32)numLevelIds); // m_numLevelIds
			stream.Write4((I32)numSequenceIds); // m_numSequences
			stream.Write4((I32)numAnimationIds); // m_numAnimations

			Location levelIdsLoc = stream.WriteNullPointer(); // m_pLevelIds
			Location sequenceIdsLoc = stream.WriteNullPointer(); // m_pSequenceIds
			Location animationIdsLoc = stream.WriteNullPointer(); // m_pAnimationIds
			Location sequenceIndicesLoc = stream.WriteNullPointer(); // m_pSequenceIndices

			std::string cinematicBaseName = (pDbActor->Name().substr(0, 4) == "cin-") ? pDbActor->Name().substr(4) : pDbActor->Name();
			std::string generalScriptName = "control-" + cinematicBaseName;
			std::string audioScriptName = "audio-" + cinematicBaseName;

			const StringId64 generalScriptId = StringToStringId64(generalScriptName.c_str());
			const StringId64 audioScriptId = StringToStringId64(audioScriptName.c_str());
			stream.WriteStringId64(generalScriptId);
			stream.WriteStringId64(audioScriptId);

			stream.Write4U(0); // deprecated old dialogId
			stream.Write4U(0); // deprecated transitionType
			stream.Write4U(0); // deprecated transitionPos
			stream.Write4U(0); // deprecated transitionFrames

			CinematicTransitionType transitionTypeStart = CinematicTransitionTypeFromString(dbcinematic.m_transitionTypeStart);
			stream.Write4U(transitionTypeStart);
			stream.Write4U(dbcinematic.m_transitionFramesStart);

			CinematicTransitionType transitionTypeEnd = CinematicTransitionTypeFromString(dbcinematic.m_transitionTypeEnd);
			stream.Write4U(transitionTypeEnd);
			stream.Write4U(dbcinematic.m_transitionFramesEnd);

			Location seqAnimationIdsLoc = stream.WriteNullPointer(); // m_pSeqAnimationIds
			stream.Write4((I32)numSeqAnimationIds); // m_numSeqAnimations
			stream.Write4U(0); // pad to 8 byte align

			StringId64 audioId = ConvertAudioLineToHexCode(dbcinematic.m_audio.c_str());
			stream.WriteStringId64(audioId); // m_audioId

			StringId64 dialogId = ConvertAudioLineToHexCode(dbcinematic.m_dialogAudio.c_str());
			stream.WriteStringId64(dialogId); // m_dialogId

			// add some zero-padding at the end -- reserved for future use
			static const size_t kReservedSize = 108;
			for (size_t i = 0; i < kReservedSize / 4; i++)
				stream.Write4(0);

			metadataMemoryUsage += stream.GetCurrentItemSize();
			stream.EndItem();

			// Write level id list.
			WriteStringVectorAsStringIds(stream, levelIdsLoc, levelNames);

			// Write sequence id list.
			WriteStringVectorAsStringIds(stream, sequenceIdsLoc, sequenceNames);

			// Write cinematic animation id list.
			WriteStringVectorAsStringIds(stream, animationIdsLoc, animationNames);

			// Write sequence id list.
			{
				stream.StartItem();
				stream.Align(kAlignDefaultLink);
				stream.SetPointer(sequenceIndicesLoc);
				for (std::vector<std::string>::const_iterator it = sequenceNames.begin(); it != sequenceNames.end(); it++)
				{
					bool extractedIndex = false;
					int seqIndex = 0;
					std::string::size_type iSuffix = it->find_last_of('=');
					if (iSuffix != std::string::npos)
					{
						std::string indexStr = it->substr(iSuffix + 1);
						extractedIndex = (sscanf(indexStr.c_str(), "%d", &seqIndex) == 1);
					}
					if (!extractedIndex)
					{
						IWARN("Unable to determine sequence index for '%s'.", it->c_str());
					}
					stream.Write4(seqIndex);
				}
				metadataMemoryUsage += stream.GetCurrentItemSize();
				stream.EndItem();
			}

			// Write sequence animation id list.
			WriteStringVectorAsStringIds(stream, seqAnimationIdsLoc, seqAnimationNames);
		}
	}

	return metadataMemoryUsage;
}

//--------------------------------------------------------------------------------------------------------------------//
U32 BuildTransform_CinematicSequence::WriteSequenceItems(BigStreamWriter& stream, 
														const BuildTransformContext *const pContext, 
														const libdb2::Actor *const pDbActor)
{
	U32 metadataMemoryUsage = 0;

	const libdb2::CinematicSequence& dbsequence = pDbActor->m_sequence;
	if (dbsequence.Loaded())
	{
		INOTE_VERBOSE("    Gathering cinematic sequence info...\n");

		std::vector<BaCinematicBinding> bindings;
		int mayaStartFrame = INT_MAX;
		int mayaEndFrame = INT_MIN;

		const libdb2::BundleRefList& bundleList = pDbActor->m_bundleref;
		for (const libdb2::BundleRef& bundle : bundleList)
		{
			std::string bundlePath = libdb2::GetDB()->BasePath() + FileIO::separator + bundle.m_path + "*.anim.xml";
			RegisterDiscoveredDependency(bundlePath, 0);
		}

		const libdb2::AnimList &dbanimList = pDbActor->m_animations;
		std::string lightAnimName;
		std::string lightSkelName;

		for(const libdb2::Anim* dbanim : dbanimList)
		{
			const std::string& animationFullName = dbanim->DiskPath();
			size_t bundleExtensionPosition = animationFullName.find(".bundle");
			std::string bundlePath = animationFullName.substr(0, bundleExtensionPosition);
			size_t bundleNamePosition = bundlePath.find_last_of('/');

			if (lightAnimName.length() == 0)
				lightAnimName = dbanim->GetLightAnimName(pDbActor);

			if (lightSkelName.length() == 0)
				lightSkelName = dbanim->GetLightSkelSceneFileName(pDbActor) + ".light-skel";

			//INOTE("  Cinematic sequence %s: animation %s", pConfig->m_actorName.c_str(), dbanim.Name().c_str());

			BaCinematicBinding binding(dbsequence, dbanim);
			bindings.push_back(binding);

			// skip a disabled animation (but only after having added a binding for it)
			//if (dbanim.m_flags.m_Disabled) // NO, better to include all animations in the start/end frame calculation, since disabled animations act like placeholders
			//	continue;

			if (dbanim->m_animationStartFrame.m_enabled)
			{
				int startFrame = (I32)dbanim->m_animationStartFrame.m_value;
				if (mayaStartFrame > startFrame)
					mayaStartFrame = startFrame;
			}
			if (dbanim->m_animationEndFrame.m_enabled)
			{
				int endFrame = (I32)dbanim->m_animationEndFrame.m_value;
				if (mayaEndFrame < endFrame)
					mayaEndFrame = endFrame;
			}
		}

		size_t numBindings = bindings.size();
		if (numBindings /* + numOther1 + numOther2 + ...*/ != 0)
		{
			BaCinematicBinding binding(lightAnimName);
			binding.m_lookName = lightSkelName;
			bindings.push_back(binding);
			numBindings++;

			// keep the bindings in a predictable, canonical order
			std::sort(bindings.begin(), bindings.end(), BaCinematicBinding::Compare);

			INOTE_VERBOSE("    Writing cinematic sequence data...\n");

			metadataMemoryUsage = 1024; // no need to report an exact amount as it's always < 1 KiB

			BigStreamWriter::Item* pItem = stream.StartItem(BigWriter::CIN_SEQUENCE_1, pDbActor->Name()); // in-engine: class CinematicItem : public ResItem
			stream.AddLoginItem(pItem, BigWriter::LEVEL_OFFSET_PRIORITY);

			stream.Write4((I32)numBindings); // m_numBindings

			int cameraIndex = dbsequence.m_cameraIndex.FullyParsed() ? dbsequence.m_cameraIndex.ParsedValue() : 1;
			if (cameraIndex <= 0 || cameraIndex > 100)
				cameraIndex = 1; // replace invalid camera index with default
			stream.Write4(cameraIndex - 1);	// m_cameraIndex // NB: in-game camera indices are zero-based, in-Maya indices are one-based

			Location bindingsLoc = stream.WriteNullPointer(); // m_pBindings
			Location seqActorLoc = stream.WriteNullPointer(); // m_pSeqActor

			CinematicCameraCut cameraCut = CinematicCameraCutFromString(dbsequence.m_cameraCut);
			stream.Write4U(cameraCut);

			stream.Write4(mayaStartFrame); // m_mayaStartFrame
			stream.Write4(mayaEndFrame);   // m_mayaEndFrame

			// add some zero-padding at the end -- reserved for future use
			static const size_t kReservedSize = 128;
			for (size_t i = 0; i < kReservedSize; i++)
			{
				stream.Write1(0);
			}

			metadataMemoryUsage += stream.GetCurrentItemSize();
			stream.EndItem();

			// Write bindings list.
			{
				StringId64 sid;
				stream.StartItem();
				stream.Align(kAlignDefaultLink);
				stream.SetPointer(bindingsLoc);
				for (std::vector<BaCinematicBinding>::iterator it = bindings.begin(); it != bindings.end(); it++)
				{
					BaCinematicBinding& binding = *it;
					std::string animNameWithoutSeqIndex = GetAnimNameWithoutSequenceIndex(binding.m_animName);

					binding.m_aliasNameLoc = stream.WritePointer(); // m_aliasName
					sid = StringToStringId64(binding.m_aliasName.c_str(), true);
					stream.WriteStringId64(sid); // m_aliasId
					sid = StringToStringId64(animNameWithoutSeqIndex.c_str(), true);
					stream.WriteStringId64(sid); // m_animId
					sid = StringToStringId64(binding.m_lookName.c_str(), true);
					stream.WriteStringId64(sid); // m_lookId
					stream.WriteStringId64(INVALID_STRING_ID_64); // m_lightLinkId (TBD)
					stream.Write4U(binding.m_disabled ? 1U : 0U); // m_disabled
					stream.Write4U(1U); // NOTE: there's no need for a version number, just add new fields to the end -- that's why we have the zero-padding! (keep writing a 1 here, it's unused by the game)

					stream.WriteStringId64(INVALID_STRING_ID_64); // old outdated data
					sid = StringToStringId64(binding.m_parentToAlias.c_str(), true);
					stream.WriteStringId64(sid); // m_parentToAlias
					sid = StringToStringId64(binding.m_parentJointOrAttach.c_str(), true);
					stream.WriteStringId64(sid); // m_parentJointOrAttachId
					sid = StringToStringId64(binding.m_attachSysId.c_str(), true);
					stream.WriteStringId64(sid); // m_attachSystemId
					stream.Write4U(binding.m_tintRandomizer); // m_tintRandomizer
					stream.Write1U(0); // m_parentObjectIdResolved, set at runtime
					stream.Write1U(binding.m_lookName.find("light-skel") != std::string::npos ? 1 : 0); // m_isLight
					stream.Write2U(0); // m_numDisabledSequences, set at runtime

					StringId64 audioId = ConvertAudioLineToHexCode(binding.m_audioStem.c_str());
					stream.WriteStringId64(audioId); // m_audioId

					// add some zero-padding at the end -- reserved for future use
					static const size_t kReservedSize = 56;
					for (size_t i = 0; i < kReservedSize; i++)
						stream.Write1U(0);	// m_reservedForFutureUse

					// also register the StringId for the name we use for the auto-spawned CinematicObject corresponding to this binding
					//DisplayInfoBright(">>> checking %s\n", animNameWithoutSeqIndex.c_str());
					std::string::size_type iDelim = binding.m_aliasName.length(); //animNameWithoutSeqIndex.find_first_of('=');
					if (iDelim != std::string::npos && animNameWithoutSeqIndex[iDelim] == '=')
					{
						std::string cinematicName = animNameWithoutSeqIndex.substr(iDelim + 1);
						std::string autoSpawnedActorName = cinematicName + "[" + binding.m_aliasName + "]";
						//DisplayInfoBright(">>> registered SID %s\n", autoSpawnedActorName.c_str());
						StringToStringId64(autoSpawnedActorName.c_str());
					}
				}
				metadataMemoryUsage += stream.GetCurrentItemSize();
				stream.EndItem();
			}

			// Write the string table.
			{
				stream.StartItem();
				for (std::vector<BaCinematicBinding>::const_iterator it = bindings.begin(); it != bindings.end(); it++)
				{
					const BaCinematicBinding& binding = *it;

					stream.Align(kAlignDefaultLink);
					stream.SetPointer(binding.m_aliasNameLoc);
					stream.WriteStr(binding.m_aliasName.c_str()); // m_aliasName
				}
				metadataMemoryUsage += stream.GetCurrentItemSize();
				stream.EndItem();
			}
		}
		else
		{
			IWARN("Sequence contains no bindings, and will be useless in-game.\n");
		}
	}

	return metadataMemoryUsage;
}


//--------------------------------------------------------------------------------------------------------------------//
BuildTransform_Cinematic::BuildTransform_Cinematic(const BuildTransformContext *const pContext
												, const std::string& actorName)
												: BuildTransform("Cinematic", pContext)
{
	SetDependencyMode(DependencyMode::kIgnoreDependency);

	m_preEvaluateDependencies.SetString("actorName", actorName);
}


//--------------------------------------------------------------------------------------------------------------------//
BuildTransformStatus BuildTransform_Cinematic::Evaluate()
{
	BigStreamWriter stream(m_pContext->m_toolParams.m_streamConfig);

	const std::string& actorName = m_preEvaluateDependencies.GetValue("actorName");
	const libdb2::Actor* pDbActor = libdb2::GetActor(actorName);

	m_metadataMemoryUsage = WriteCinematicItems(this, 
												stream, 
												m_pContext, 
												pDbActor);

	// Write the .bo file.
	NdiBoWriter boWriter(stream);
	boWriter.Write();

	const BuildPath& boPath = GetOutputPath("CinematicsBo");
	DataStore::WriteData(boPath, boWriter.GetMemoryStream());

	return BuildTransformStatus::kOutputsUpdated;
}


//--------------------------------------------------------------------------------------------------------------------//
BuildTransform_CinematicSequence::BuildTransform_CinematicSequence(const BuildTransformContext *const pContext
																, const std::string& actorName)
																: BuildTransform("CinematicSequence", pContext)
{
	m_preEvaluateDependencies.SetString("actorName", actorName);

	const libdb2::Actor* pDbActor = libdb2::GetActor(actorName);

	m_preEvaluateDependencies.SetString("sequence", pDbActor->m_sequence.Xml());
	m_preEvaluateDependencies.SetString("animations", pDbActor->m_animations.Xml());
}


//--------------------------------------------------------------------------------------------------------------------//
BuildTransformStatus BuildTransform_CinematicSequence::Evaluate()
{
	BigStreamWriter stream(m_pContext->m_toolParams.m_streamConfig);

	const std::string& actorName = m_preEvaluateDependencies.GetValue("actorName");
	const libdb2::Actor* pDbActor = libdb2::GetActor(actorName);

	m_metadataMemoryUsage = WriteSequenceItems(stream, m_pContext, pDbActor);

	// Write the .bo file.
	NdiBoWriter boWriter(stream);
	boWriter.Write();

	const BuildPath& boPath = GetOutputPath("SequenceBo");
	DataStore::WriteData(boPath, boWriter.GetMemoryStream());

	return BuildTransformStatus::kOutputsUpdated;
}

//--------------------------------------------------------------------------------------------------------------------//
void BuildModuleCinematic_Configure(const BuildTransformContext *const pContext, 
									const libdb2::Actor *const pDbActor, 
									std::vector<std::string>& arrBoFiles)
{
	// Handle cinematics first
	const libdb2::Cinematic& dbcinematic = pDbActor->m_cinematic;
	if (dbcinematic.Loaded())
	{
		BuildTransform_Cinematic *const pCinematic = new BuildTransform_Cinematic(pContext, pDbActor->Name());
		const std::string cinematicsBoFilename = pContext->m_toolParams.m_buildPathCinematicBo + pDbActor->Name() + ".bo";
		pCinematic->SetOutput(TransformOutput(cinematicsBoFilename, "CinematicsBo"));
		pContext->m_buildScheduler.AddBuildTransform(pCinematic, pContext);
		arrBoFiles.push_back(cinematicsBoFilename);
	}

	// Now, handle sequences
	const libdb2::CinematicSequence& dbsequence = pDbActor->m_sequence;
	if (dbsequence.Loaded())
	{
		BuildTransform_CinematicSequence *const pSequence = new BuildTransform_CinematicSequence(pContext, pDbActor->Name());
		std::vector<TransformOutput> transformOutputs;

		const std::string sequenceBoFilename = pContext->m_toolParams.m_buildPathCinematicSequenceBo + pDbActor->Name() + ".bo";
		transformOutputs.push_back(TransformOutput(sequenceBoFilename, "SequenceBo"));

		pSequence->SetOutputs(transformOutputs);
		pContext->m_buildScheduler.AddBuildTransform(pSequence, pContext);
		arrBoFiles.push_back(sequenceBoFilename);
	}
}

