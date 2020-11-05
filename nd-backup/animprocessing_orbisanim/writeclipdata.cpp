/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "animmetadata.h"
#include "animcompression.h"
#include "clipdatawriter.h"
#include "animkeyextractors.h"
#include "splineprocessing.h"

//#pragma optimize("", off) // uncomment when debugging in release mode

namespace OrbisAnim {
namespace Tools {
namespace AnimProcessing {

bool FileExists(std::string const& filename)
{
	FILE *fileExists = fopen( filename.c_str(), "r" );
	if (fileExists) {
		fclose(fileExists);
		return true;
	}
	return false;
}

bool ReadMetaDataFromFiles(AnimMetaData& metaData,
						   OutputAnimDesc const& outputAnimDesc,
						   std::string const& bindPoseSceneFileName,
						   std::vector<std::string> const& metadataFiles)
{
	if (!outputAnimDesc.m_metaDataPath.empty() && FileExists(outputAnimDesc.m_metaDataPath)) {
		if (ProcessMetadataFile( &metaData, outputAnimDesc.m_metaDataPath, metadataFiles, outputAnimDesc.m_inputData.m_animScenePath, bindPoseSceneFileName )) {
			INOTE(IMsg::kVerbose, "Read metadata file '%s' associated with clip '%s'\n", outputAnimDesc.m_metaDataPath.c_str(), outputAnimDesc.m_inputData.m_animScenePath.c_str());
		} else {
			IWARN("Metadata file '%s' associated with clip '%s' is invalid; using no compression\n", outputAnimDesc.m_metaDataPath.c_str(), outputAnimDesc.m_inputData.m_animScenePath.c_str());
			metaData = AnimMetaData();
		}
	} else {
		if (!metadataFiles.empty() && ProcessMetadataFile(&metaData, "", metadataFiles, outputAnimDesc.m_inputData.m_animScenePath, bindPoseSceneFileName)) {
			INOTE(IMsg::kVerbose, "No metadata file '%s' associated with clip '%s'; read default includes\n", outputAnimDesc.m_metaDataPath.c_str(), outputAnimDesc.m_inputData.m_animScenePath.c_str());
		} else {
			INOTE(IMsg::kBrief, "No metadata file '%s' associated with clip '%s'; using no compression\n", outputAnimDesc.m_metaDataPath.c_str(), outputAnimDesc.m_inputData.m_animScenePath.c_str());
			metaData = AnimMetaData();
		}
	}
	return true;
}

ICETOOLS::Location WriteClipData(ICETOOLS::StreamWriter& streamWriter,
								 ITSCENE::SceneDb const& bindPoseScene,
								 OutputAnimDesc const& outputAnimDesc,
								 AnimMetaData const& metaData,
								 ClipStats* clipStats)
{
	// extract intermediate clip data
	AnimationHierarchyDesc hierarchyDesc;
	AnimationBinding binding;
	AnimationClipProperties clipProperties;
	AnimationClipSourceData sourceData;
	ClipCompressionDesc compression;
	AnimationClipCompressionStats* compStats = clipStats ? &clipStats->getCompStats() : NULL;
	if (!ExtractClipDataIntermediateData(hierarchyDesc, binding, clipProperties, sourceData, compression, bindPoseScene, outputAnimDesc, metaData, compStats)) {
		return ICETOOLS::kLocationInvalid;	
	}

	// localClipStats is used as a temporary if none was passed in
	ClipStats localClipStats;
	ClipStats& stats = clipStats ? *clipStats : localClipStats;

	// write clip header & body 
	ICETOOLS::Location locTotalSize, locUserDataOffset;
	ICETOOLS::Location locClipData = WriteClipData(streamWriter, hierarchyDesc, binding, clipProperties, sourceData, compression, locTotalSize, locUserDataOffset, &stats);
	if (locClipData == ICETOOLS::kLocationInvalid) {
		return ICETOOLS::kLocationInvalid;
	}

	// write root anim as user data
	stats.start("RootAnimData", streamWriter.CreatePosition());
	ICETOOLS::Location locUserData = ClipDataWriter::WriteIceOmegaUserDataHeaderAndData(streamWriter, sourceData.m_pRootAnim);
	if (locUserData != ICETOOLS::kLocationInvalid) {
		streamWriter.SetLink(locUserDataOffset, locUserData, locClipData);
	}
	streamWriter.Align(16);
	streamWriter.SetLink(locTotalSize, streamWriter.CreatePosition(), locClipData);
	stats.end(streamWriter.CreatePosition());

	return locClipData;
}

ICETOOLS::Location WriteClipDataBareHeader(ICETOOLS::StreamWriter& streamWriter,
										   ITSCENE::SceneDb const& bindPoseScene,
										   OutputAnimDesc const& outputAnimDesc)
{
	AnimationHierarchyDesc hierarchyDesc;
	AnimationBinding binding;
	AnimationClipProperties clipProperties;
	AnimationClipSourceData sourceData;
	if (!ExtractClipDataIntermediateData(hierarchyDesc, binding, clipProperties, sourceData, bindPoseScene, outputAnimDesc)) {
		return ICETOOLS::kLocationInvalid;
	}

	ICETOOLS::Location locTotalSize, locUserDataOffset;
	ICETOOLS::Location locClipData = WriteClipDataBareHeader(streamWriter, hierarchyDesc, clipProperties, sourceData, locTotalSize, locUserDataOffset);
	if (locClipData == ICETOOLS::kLocationInvalid) {
		return ICETOOLS::kLocationInvalid;
	}

	ICETOOLS::Location locUserData = ClipDataWriter::WriteIceOmegaUserDataHeaderAndData(streamWriter, sourceData.m_pRootAnim);
	if (locUserData != ICETOOLS::kLocationInvalid) {
		streamWriter.SetLink(locUserDataOffset, locUserData, locClipData);
	}
	streamWriter.Align(16);
	streamWriter.SetLink(locTotalSize, streamWriter.CreatePosition(), locClipData);

	return locClipData;
}

ICETOOLS::Location WriteClipData(ICETOOLS::StreamWriter& streamWriter,
								 AnimationHierarchyDesc const& hierarchyDesc,
								 AnimationBinding const& binding,
								 AnimationClipProperties const& clipProperties,
								 AnimationClipSourceData const& sourceData,
								 ClipCompressionDesc const& compression,
								 ICETOOLS::Location& locTotalSize,
								 ICETOOLS::Location& locUserDataOffset,
								 ClipStats* clipStats)
{
	unsigned const clipType = compression.m_flags & kClipKeyCompressionMask;
	switch (clipType) {
	case kClipKeysUniform:
	case kClipKeysUniform2:	
	{
		UniformKeyExtractor uniformKeyExtractor(sourceData);
		AnimationClipCompressedData compressedData;
		if (compression.m_flags & kClipGlobalOptimization) {
			BuildAnimationClipCompressedDataOptimizedGlobally(compressedData, binding, sourceData, compression, uniformKeyExtractor, hierarchyDesc.m_joints);
		} else {
			BuildAnimationClipCompressedData(compressedData, binding, sourceData, compression, uniformKeyExtractor);
		}
		ClipDataWriter clipDataWriter;
		clipDataWriter.BuildClipData(hierarchyDesc, clipProperties, compressedData);
		if (clipType == kClipKeysUniform) {
			return clipDataWriter.WriteClipDataForUniformKeys(streamWriter, locTotalSize, locUserDataOffset, clipStats);
		} else {
			return clipDataWriter.WriteClipDataForUniformKeys2(streamWriter, locTotalSize, locUserDataOffset, clipStats);
		}
	}
	case kClipKeysShared:
	{
		FrameArray sharedKeys;
		BuildAnimationClipSharedKeyFrameArray(sharedKeys, compression, sourceData);
		AnimationClipCompressedData compressedData;
		SharedKeyExtractor sharedKeyExtractor(sourceData, sharedKeys);
		if (compression.m_flags & kClipGlobalOptimization) {
			BuildAnimationClipCompressedDataOptimizedGlobally(compressedData, binding, sourceData, compression, sharedKeyExtractor, hierarchyDesc.m_joints);
		} else {
			BuildAnimationClipCompressedData(compressedData, binding, sourceData, compression, sharedKeyExtractor);
		}
		ClipDataWriter clipDataWriter;
		clipDataWriter.BuildClipData(hierarchyDesc, clipProperties, compressedData);
		return clipDataWriter.WriteClipDataForSharedKeys(streamWriter, sharedKeys, locTotalSize, locUserDataOffset, clipStats);
	}
	case kClipKeysUnshared:
	{
		AnimationClipUnsharedKeyFrameBlockDesc unsharedKeys;
		BuildAnimationClipUnsharedKeyFrameBlocks(unsharedKeys, compression, binding, sourceData, hierarchyDesc);
		AnimationClipCompressedData compressedData;
		UnsharedKeyExtractor unsharedKeyExtractor(hierarchyDesc, binding, sourceData, unsharedKeys);
		if (compression.m_flags & kClipGlobalOptimization) {
			BuildAnimationClipCompressedDataOptimizedGlobally(compressedData, binding, sourceData, compression, unsharedKeyExtractor, hierarchyDesc.m_joints);
		} else {
			BuildAnimationClipCompressedData(compressedData, binding, sourceData, compression, unsharedKeyExtractor);
		}
		ClipDataWriter clipDataWriter;
		clipDataWriter.BuildClipData(hierarchyDesc, clipProperties, compressedData);
		return clipDataWriter.WriteClipDataForUnsharedKeys(streamWriter, unsharedKeys, locTotalSize, locUserDataOffset, clipStats);
	}
	case kClipKeysHermite:
	{
		AnimationClipCompressedData compressedData;
		BuildCompressedHermiteData(compressedData, binding, sourceData, compression, hierarchyDesc);
		ClipDataWriter clipDataWriter;
		clipDataWriter.BuildClipData(hierarchyDesc, clipProperties, compressedData);
		return clipDataWriter.WriteClipDataForHermiteKeys(streamWriter, locTotalSize, locUserDataOffset, clipStats);
	}
	default:
		ITASSERT(0);
		return ICETOOLS::kLocationInvalid;
	}
}

ICETOOLS::Location WriteClipDataBareHeader(ICETOOLS::StreamWriter& streamWriter,
										   AnimationHierarchyDesc const& hierarchyDesc,
										   AnimationClipProperties const& clipProperties,
										   AnimationClipSourceData const& sourceData,
										   ICETOOLS::Location &locTotalSize,
										   ICETOOLS::Location &locUserDataOffset)
{
	ClipDataWriter clipDataWriter;
	clipDataWriter.BuildClipDataBareHeader( hierarchyDesc, clipProperties, sourceData );
	return clipDataWriter.WriteClipDataBareHeader(streamWriter, locTotalSize, locUserDataOffset);
}

} // namespace AnimProcessing
} // namespace Tools
} // namespace OrbisAnim
