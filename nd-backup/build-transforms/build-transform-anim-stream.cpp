/*
 * Copyright (c) 2017 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/stdafx.h"

#include "tools/pipeline3/build/build-transforms/build-transform-anim-stream.h"
#include "tools/libs/bigstreamwriter/ndi-bo-writer.h"
#include "tools/libs/bigstreamwriter/ndi-bo-reader.h"
#include "tools/libs/bigstreamwriter/ndi-pak-writer.h"
#include "tools/libs/toolsutil/color-display.h"
#include "tools/libs/toolsutil/filename.h"
#include "tools/libs/toolsutil/ndi-endian.h"
#include "tools/libs/bolink/bolink.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/toolversion.h"
#include "common/hashes/crc32.h"

#include "tools/pipeline3/build/tool-params.h"


static I32 GetNumFrames(const libdb2::Anim & anim)
{
	ALWAYS_ASSERT(anim.m_animationStartFrame.m_enabled && anim.m_animationEndFrame.m_enabled);
	I32 startFrame = (I32)anim.m_animationStartFrame.m_value;
	I32 endFrame = (I32)anim.m_animationEndFrame.m_value;

	return endFrame - startFrame;
}

//--------------------------------------------------------------------------------------------------------------------//
BuildTransformStatus BuildTransform_AnimStreamStm::Evaluate()
{
	const std::vector<TransformInput>& animBoFiles = GetInputs();
	const BuildPath& animStreamPath = GetOutputPath("AnimStream");
	const BuildPath& animStreamBoPath = GetOutputPath("AnimStreamBo");

	// The inputs are ordered in order of stream index and then animation name to support interleaving
	int numSlotsInStream = 0;
	int numFramesInStream = 0;
	for (const auto& entry : m_streamEntries)
	{
		if (entry.m_index != 1)
			break;

		// Verify that all animations in this stream have the same number of frames
		const int numAnimFrames = entry.m_pAnim ? GetNumFrames( *entry.m_pAnim ) : entry.m_numFrames;
		if (numFramesInStream && numAnimFrames != numFramesInStream)
		{
			INOTE("\n-------------------------------------------------------------------------\n");
			IERR("Animation %s has %d frames, while animation %s has %d frames.", 
				entry.m_pAnim->Name().c_str(),
				numAnimFrames,
				m_streamEntries[0].m_pAnim->Name().c_str(),
				numFramesInStream);
			IERR("Since they are both in igc-stream bundle. %s, their frame ranges must be the same!", entry.m_streamName.c_str());
			INOTE("-------------------------------------------------------------------------\n");
			IABORT(0);
		}
		numFramesInStream = numAnimFrames;
		numSlotsInStream++;
	}

	const int numInterleavedBlocks = m_streamEntries.size() / numSlotsInStream;

	const ToolParams& tool = m_pContext->m_toolParams;

	// now write our streams, with the knowledge that every separate block comes in the correct order
	AnimStreamHeader header;
	header.m_streamName = m_streamEntries[0].m_streamName;
	header.m_framesPerBlock = kNumFramesPerStreamingChunk;

	header.m_interleavedBlockSizes.resize(numInterleavedBlocks);
	for (auto& value : header.m_interleavedBlockSizes)
		value = 0;

	// construct the arrays for our slots
	for (unsigned slotIndex=0; slotIndex<numSlotsInStream; slotIndex++)
	{
		if (m_streamEntries[slotIndex].m_pAnim)
		{
			const libdb2::Actor* skelActor = libdb2::GetActor(m_streamEntries[slotIndex].m_pAnim->m_actorRef, false);

			std::string skelId = skelActor->m_skeleton.m_sceneFile;
			if (skelActor->m_skeleton.m_set.size())
				skelId += "." + skelActor->m_skeleton.m_set;
			const SkeletonId skelHash = ComputeSkelId(skelId);

			header.m_skelIds.push_back(skelHash);
			header.m_animNames.push_back(m_streamEntries[slotIndex].m_pAnim->Name());
		}
		else
		{
			header.m_skelIds.push_back(m_streamEntries[slotIndex].m_skelId);
			header.m_animNames.push_back(m_streamEntries[slotIndex].m_animName);
		}
	}

	header.m_numBlocks = m_streamEntries.size();
	header.m_numSlotsInStream = numSlotsInStream;

	// Initialize a new stream
	std::stringstream ostream;
	for (unsigned i = 0; i<m_streamEntries.size(); i++)
	{
		const StreamBoEntry& streamEntry = m_streamEntries[i];
		const int currentInterleavedBlockIndex = i / numSlotsInStream;
		const int currentSlotIndex = i % numSlotsInStream;

		// Read the .bo file
		NdbMemoryStream animBoStream;
		DataStore::ReadData(animBoFiles[i].m_file, animBoStream);

		// Create a .pak file
		std::vector<std::pair<NdbMemoryStream*, std::string>> arrBoFiles;
		arrBoFiles.reserve(1);
		arrBoFiles.emplace_back(&animBoStream, animBoFiles[i].m_file.AsAbsolutePath());

		BigStreamWriter streamPakWriter(tool.m_streamConfig);
		BoLink(arrBoFiles, tool.m_streamConfig, streamPakWriter);

		NdiPakWriter pakWriter(streamPakWriter, 0, streamPakWriter.GetTargetEndianness());
		pakWriter.Write();

		const U32 pakSize = pakWriter.GetMemoryStream().size();
		const char* pakData = pakWriter.GetMemoryStream().data();

		// compute a CRC32 for the pak file and write it out first
		U32 crc32 = Crc32((const unsigned char*)pakData, pakSize);

		// DB: Only swap if the target endianness is actually different!
		if (tool.m_streamConfig.m_targetEndian != ICETOOLS::kHostEndian)
		{
			EndianSwap(crc32);
		}
			
		U32 pad[3];
		pad[0] = 0x4d494e41;	// 'ANIM'
		pad[1] = 0x4947414d;	// 'MAGI'
		pad[2] = 0x52545343;	// 'CSTR'

		ostream.write((const char*)&crc32, sizeof(U32));
		ostream.write((const char*)&pad[0], sizeof(U32)); // padding
		ostream.write((const char*)&pad[1], sizeof(U32)); // padding
		ostream.write((const char*)&pad[2], sizeof(U32)); // padding
		ostream.write((const char*)pakData, pakSize);

		const int blockSize = pakSize + 16;
		header.m_blockSizes.push_back(blockSize);
		header.m_interleavedBlockSizes[currentInterleavedBlockIndex] += blockSize;
	}

	DataStore::WriteData(animStreamPath, ostream.str());

	//////////////////////////////////////////////////////////////////////////
	// Now, write the bo file using the information in the header struct
	//////////////////////////////////////////////////////////////////////////
	BigStreamWriter streamWriter(m_pContext->m_toolParams.m_streamConfig);

	// figure out the max interleaved block size
	int maxInterleavedBlockSize = 0;
	for (unsigned k=0; k<header.m_interleavedBlockSizes.size(); k++)
	{
		if (header.m_interleavedBlockSizes[k] > maxInterleavedBlockSize)
		{
			maxInterleavedBlockSize = header.m_interleavedBlockSizes[k];
		}
	}

	int kMaxSupportedRuntimeBlockSize = 1024 * 1024;
	if (maxInterleavedBlockSize > kMaxSupportedRuntimeBlockSize)
	{
		IABORT("The animation stream '%s' is too large. (%u > %u)",
			   header.m_streamName.c_str(),
			   maxInterleavedBlockSize,
			   kMaxSupportedRuntimeBlockSize);
	}

	BigStreamWriter::Item* pItem = streamWriter.StartItem(BigWriter::ANIM_STREAM, header.m_streamName.c_str());

	// write the header data for our stream
	streamWriter.Write4(header.m_numSlotsInStream);											// m_numAnims
	streamWriter.Write4(header.m_framesPerBlock);											// m_framesPerBlock
	streamWriter.Write4(maxInterleavedBlockSize);											// m_maxBlockSize
	streamWriter.Write4(header.m_numBlocks);												// m_numBlocks

	Location streamNamePtr = streamWriter.WritePointer();									// m_pStreamName
	Location skelIdsPtr = streamWriter.WritePointer();										// m_pSkelIds
	Location animNamesPtr = streamWriter.WritePointer();									// m_pAnimNames
	Location blockSizesPtr = streamWriter.WritePointer();									// m_pBlockSizes;
	streamWriter.WriteNullPointer();														// m_padding


	//--------------------------------------------------------------------------------
	// write the stream name
	streamWriter.AlignPointer();
	streamWriter.SetPointer(streamNamePtr);													// stream name
	streamWriter.WriteString(header.m_streamName.c_str());


	//--------------------------------------------------------------------------------
	// write the skeleton ids
	streamWriter.AlignPointer();
	streamWriter.SetPointer(skelIdsPtr);													// skel ID array
	for (unsigned slotIndex=0; slotIndex<header.m_numSlotsInStream; slotIndex++)
	{
		streamWriter.Write4(header.m_skelIds[slotIndex].GetValue());
	}


	//--------------------------------------------------------------------------------
	// write the animation ids
	streamWriter.AlignPointer();
	streamWriter.SetPointer(animNamesPtr);													// anim name array
	
	for (U32F slotIndex = 0; slotIndex < header.m_numSlotsInStream; slotIndex++)
	{
		const StringId64 animId = StringToStringId64(header.m_animNames[slotIndex].c_str(), true);
		streamWriter.WriteStringId64(animId);
	}


	//--------------------------------------------------------------------------------
	// write our block sizes array
	streamWriter.AlignPointer();
	streamWriter.SetPointer(blockSizesPtr);													// block size array
	for (unsigned blockIndex=0; blockIndex<header.m_blockSizes.size(); blockIndex++)
	{
		streamWriter.Write4(header.m_blockSizes[blockIndex]);
	}

	streamWriter.EndItem();
	streamWriter.AddLoginItem(pItem, BigWriter::ANIM_PRIORITY);


	// Write out the bo file
	NdiBoWriter boWriter(streamWriter);
	boWriter.Write();

	DataStore::WriteData(animStreamBoPath, boWriter.GetMemoryStream());

	return BuildTransformStatus::kOutputsUpdated;
}

