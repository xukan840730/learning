/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

//#include "tools/pipeline3/build/util/source-asset-view.h"


enum class AssetType
{
	kInvalid,

	kActor,
	kLevel,
	kSoundBank,

	kSoundBanksUpload,			// Deprecated

	// Temp types during transition to having these assets in the build pipeline
	kIrpacksUpload,
	kMusicUpload,
	kSpeechUpload,
	kMovieUpload,
	kTextUpload,
	kMiscUpload,
	kGameplay,
	kDcScript,
	kPipelineExecutables,
	kShaders,
	kNumTypes
};

inline const char* GetAssetTypeName(AssetType type)
{
	if (type == AssetType::kActor)						return "actor";
	else if (type == AssetType::kLevel)					return "level";
	else if (type == AssetType::kSoundBank)				return "soundbank";
	else if (type == AssetType::kSoundBanksUpload)		return "soundbanks";			// Deprecated
	else if (type == AssetType::kIrpacksUpload)			return "irpack";
	else if (type == AssetType::kMusicUpload)			return "music";
	else if (type == AssetType::kSpeechUpload)			return "speech";
	else if (type == AssetType::kMovieUpload)			return "movie";
	else if (type == AssetType::kTextUpload)			return "text";
	else if (type == AssetType::kMiscUpload)			return "misc";
	else if (type == AssetType::kGameplay)				return "gameplay";
	else if (type == AssetType::kDcScript)				return "dc";
	else if (type == AssetType::kPipelineExecutables)	return "pipeline-executables";
	else if (type == AssetType::kShaders)				return "shaders";
	else return "unknown";
}

inline AssetType GetAssetTypeFromName(const std::string &name)
{
	if (name == "actor") return AssetType::kActor;
	if (name == "level") return AssetType::kLevel;
	if (name == "soundbank") return AssetType::kSoundBank;
	if (name == "soundbanks") return AssetType::kSoundBanksUpload;
	if (name == "irpack") return AssetType::kIrpacksUpload;
	if (name == "music") return AssetType::kMusicUpload;
	if (name == "speech") return AssetType::kSpeechUpload;
	if (name == "movie") return AssetType::kMovieUpload;
	if (name == "text") return AssetType::kTextUpload;
	if (name == "misc") return AssetType::kMiscUpload;
	if (name == "gameplay") return AssetType::kGameplay;
	if (name == "dc") return AssetType::kDcScript;
	if (name == "pipeline-executables") return AssetType::kPipelineExecutables;
	if (name == "shaders") return AssetType::kShaders;
	return AssetType::kInvalid;
}

enum class BuildStatus
{
	kOK,
	kErrorOccurred
};

enum class BuildTransformStatus
{
	kWaitingInputs,
	kFailed,
	kOutputsUpdated,
	kResumeNeeded,

	kNumStatus		// should always be the last
};

enum class SourceAssetViewStatus
{
	kIgnoreSourceAssetView,
	kUseSourceAssetView,
	kUsingSourceAssetView,
};


