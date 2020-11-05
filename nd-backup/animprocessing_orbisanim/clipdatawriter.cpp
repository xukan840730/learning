/*
 * Copyright (c) 2003, 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "clipdatawriter.h"
#include "clipdatachannelgroup.h"
#include "animobjectrootanims.h"

//#pragma optimize("", off) // uncomment when debugging in release mode

namespace OrbisAnim {
namespace Tools {
namespace AnimProcessing {

using ICETOOLS::kLocationInvalid;

// constants from runtime code:
static const U32 kAnimClipMagicVersion1_00		= 0x41430100;		// Initial OrbisAnim version
static const U32 kAnimClipMagicVersion1_01		= 0x41430101;		// dword aligned Locations in command blocks
static const U32 kAnimClipMagicVersion1_02		= 0x41430102;		// little endian
static const U32 kAnimClipMagicVersionCurrent	= kAnimClipMagicVersion1_02;

static const U32 kAnimClipMagicVersion2_00		= 0x41430200;		// used only for new "uniform2" clip format

ClipDataWriter::ClipDataWriter()
{
	m_clipProperties.m_objectRootAnim = kObjectRootAnimNone;
	m_clipProperties.m_framesPerSecond = 0;	
	m_hierarchyId = 0;		
	m_flags	= 0;			
	m_numOutputFrames = 0;	
	m_pObjectRootAnim = 0;
}

ClipDataWriter::~ClipDataWriter()
{
	for (auto it = m_channelGroups.begin(); it != m_channelGroups.end(); ++it) {
		delete *it;
	}
	delete m_pObjectRootAnim;
}

// Construct runtime data from source and control data
void ClipDataWriter::BuildClipData(AnimationHierarchyDesc const& hierarchyDesc,
								   AnimationClipProperties const& clipProperties, 
								   AnimationClipCompressedData const& compressedData)
{
	m_clipProperties = clipProperties;
	m_clipProperties.m_objectRootAnim = kObjectRootAnimNone;
	m_hierarchyId = compressedData.m_hierarchyId;
	m_flags = compressedData.m_flags & (kClipSourceDataMask | kClipKeyCompressionMask);
	m_numOutputFrames = compressedData.m_numFrames;

	SetObjectRootAnim(compressedData.m_pRootAnim);

	for (auto it = m_channelGroups.begin(); it != m_channelGroups.end(); ++it) {
		delete *it;
	}
	m_channelGroups.clear();

	unsigned const numGroups = (unsigned)hierarchyDesc.m_channelGroups.size();
	for (unsigned iGroup = 0; iGroup < numGroups; ++iGroup) {
		ChannelGroup* pChannelGroup = new ChannelGroup(hierarchyDesc.m_channelGroups[iGroup], compressedData);
		m_channelGroups.push_back(pChannelGroup);
	}
}

void ClipDataWriter::BuildClipDataBareHeader(AnimationHierarchyDesc const& hierarchyDesc, 
											 AnimationClipProperties const& clipProperties, 
											 AnimationClipSourceData const& sourceData)
{
	m_clipProperties = clipProperties;
	m_clipProperties.m_objectRootAnim = kObjectRootAnimNone;
	m_hierarchyId = hierarchyDesc.m_hierarchyId;
	m_flags = sourceData.m_flags & (kClipSourceDataMask);
	m_numOutputFrames = sourceData.m_numFrames;

	SetObjectRootAnim(sourceData.m_pRootAnim);

	for (auto it = m_channelGroups.begin(); it != m_channelGroups.end(); ++it) {
		delete *it;
	}
	m_channelGroups.erase(m_channelGroups.begin(), m_channelGroups.end());
}

// Set the object root anim for this ClipData
void ClipDataWriter::SetObjectRootAnim(AnimationClipObjectRootAnim* pObjectRootAnim)
{
	delete m_pObjectRootAnim, m_pObjectRootAnim = NULL;
	if (pObjectRootAnim) {
		m_pObjectRootAnim = pObjectRootAnim->ConstructCopy();
		m_clipProperties.m_objectRootAnim = m_pObjectRootAnim->GetType();
		m_flags |= kClipObjectRootAnim;
	} else {
		m_clipProperties.m_objectRootAnim = kObjectRootAnimNone;
	}
}

Location ClipDataWriter::WriteSharedKeyTable(StreamWriter &streamWriter, 
											 FrameArray const& sharedKeys) const
{
	Location locStart = WriteAnimClipFrameTable(streamWriter, sharedKeys, m_numOutputFrames);
	streamWriter.Align(16);	// pad to qword alignment
	return locStart;
}

// also used for uniform clips
Location ClipDataWriter::WriteClipDataForSharedKeys(StreamWriter &streamWriter, 
													FrameArray const &sharedKeys) const
{
	Location locTotalSize, locUserDataOffset;
	Location locClipData = WriteClipDataForSharedKeys(streamWriter, sharedKeys, locTotalSize, locUserDataOffset);
	if (locClipData == kLocationInvalid)
		return kLocationInvalid;

	// Write Ice::Omega style user data
	Location locUserData = WriteIceOmegaUserDataHeaderAndData(streamWriter, m_pObjectRootAnim);
	if (locUserData != kLocationInvalid)
		streamWriter.SetLink(locUserDataOffset, locUserData, locClipData);
	streamWriter.Align(16);
	streamWriter.SetLink(locTotalSize, streamWriter.CreatePosition(), locClipData);

	return locClipData;
}

// also used for uniform clips
Location ClipDataWriter::WriteClipDataForSharedKeys(StreamWriter& streamWriter, 
													FrameArray const& sharedKeys, 
													Location& locTotalSize, 
													Location& locUserDataOffset,
													ClipStats* stats) const
{
	ClipStats localClipStats;
	ClipStats& clipStats = stats ? *stats : localClipStats;

	using namespace ICETOOLS;

	ITASSERT(m_flags <= 0xFFFF);
	ITASSERT(m_numOutputFrames <= 0xFFFF);
	ITASSERT(m_channelGroups.size() <= 0xFFFF);
	
	unsigned keyCompressionFlags = (m_flags & kClipKeyCompressionMask);
	ITASSERT(((keyCompressionFlags == kClipKeysUniform) && sharedKeys.empty()) ||
			 ((keyCompressionFlags == kClipKeysShared) && !sharedKeys.empty()));

	unsigned numGroups = (unsigned)m_channelGroups.size();
	unsigned numTotalKeys = (keyCompressionFlags == kClipKeysShared) ? (unsigned)sharedKeys.size() : m_numOutputFrames;

	clipStats.start("Header", streamWriter.CreatePosition());

	streamWriter.Align(16);
	Location locHeader = streamWriter.CreatePosition();

	streamWriter.Write4(kAnimClipMagicVersionCurrent);											// m_magic
	streamWriter.Write4(m_hierarchyId);															// m_hierarchyId
	locTotalSize = streamWriter.WriteLink(kLocTypeLink4ByteOffset);								// m_totalSize
	streamWriter.Write2((U16)numGroups);														// m_numGroups
	streamWriter.Write2((U16)numTotalKeys);														// m_numTotalBlocks

	Location formatDataOffset = streamWriter.WriteLink(kLocTypeLink4ByteOffset);				// m_formatDataOffset
	Location groupTableOffset = streamWriter.WriteLink(kLocTypeLink4ByteOffset);				// m_groupHeadersOffset
	locUserDataOffset =	streamWriter.WriteLink(kLocTypeLink4ByteOffset);						// m_userDataOffset
	streamWriter.WriteF(m_clipProperties.m_framesPerSecond);									// m_framesPerSecond

	streamWriter.Write2((U16)m_flags);															// m_clipFlags
	streamWriter.Write2((U16)m_numOutputFrames);												// m_numTotalFrames
	streamWriter.WriteF((m_numOutputFrames > 1) ? 1.0f/(float)(m_numOutputFrames-1) : 0.0f);	// m_phasePerFrame
	streamWriter.WriteF((m_numOutputFrames > 1) ? (float)(m_numOutputFrames-1) : 0.0f);			// m_fNumFrameIntervals
	streamWriter.WriteF(1.0f/m_clipProperties.m_framesPerSecond);								// m_secondsPerFrame

	clipStats.end(streamWriter.CreatePosition());

	// Write AnimClip*FormatData, if any
	if (keyCompressionFlags == kClipKeysShared) {
		clipStats.start("FrameToKeyIndexMap", streamWriter.CreatePosition());
		Location locFormatData = WriteSharedKeyTable(streamWriter, sharedKeys);
		streamWriter.SetLink(formatDataOffset, locFormatData, locHeader);
		clipStats.end(streamWriter.CreatePosition());
	}

	std::vector<Location> constDataOffsetTable;
	std::vector<Location> blockOffsetTable;

	// Write AnimClipSharedKeysGroupHeader array
	clipStats.start("SharedKeysGroupHeaders", streamWriter.CreatePosition());
	streamWriter.Align(16);
	streamWriter.SetLink(groupTableOffset, streamWriter.CreatePosition(), locHeader);
	for (unsigned iGroup = 0; iGroup < numGroups; iGroup++) {
		unsigned keySize = m_channelGroups[iGroup]->GetKeySize();
		ITASSERT( keySize <= 0xFFFF );

		// The first key is always 16-byte aligned.  Other keys start
		// at the ((nKey*keySize) & 0xF)'th byte of 16.  If we find nKey that
		// results in a 16-byte aligned DMA again, we can stop, since following
		// keys will just repeat the same pattern of offsets.
		// NOTE: If numTotalKeys is greater than 16, we know that the worst case 
		// key offset will be ((0-keySize) &~(keySize-1) & 0xF), but this loop 
		// will sometimes produce better results for numTotalKeys <= 16.
		unsigned maxKeyDmaSize = Align(keySize*2, 16);
		for (unsigned nKey = 1; nKey < numTotalKeys-1; ++nKey) {
			unsigned keyDmaOffset = (nKey*keySize) & 0xF;
			if (keyDmaOffset == 0)
				break;
			unsigned keyDmaSize = Align(keyDmaOffset + keySize*2, 16);
			if (maxKeyDmaSize < keyDmaSize)
				maxKeyDmaSize = keyDmaSize;
		}
		ITASSERT( maxKeyDmaSize <= 0xFFFF );

		constDataOffsetTable.push_back(streamWriter.WriteLink(kLocTypeLink4ByteOffset));	// m_commandBlockOffset
		streamWriter.Write2((U16)0);														// m_padding
		streamWriter.Write1( (U8)m_channelGroups[iGroup]->GetNumAnimatedJoints() );			// m_numJoints
		streamWriter.Write1( (U8)m_channelGroups[iGroup]->GetNumFloatChannels() );			// m_numFloatChannels

		blockOffsetTable.push_back(streamWriter.WriteLink(kLocTypeLink4ByteOffset));		// m_blockOffset
		streamWriter.Write2((U16)keySize);													// m_keyDmaSize
		streamWriter.Write2((U16)maxKeyDmaSize);											// m_maxKeyDmaSize
	}
	clipStats.end(streamWriter.CreatePosition());

	// Write valid bits array
	clipStats.start("ValidBitsArrays", streamWriter.CreatePosition());
	streamWriter.Align(16);
	for (unsigned iGroup = 0; iGroup < numGroups; iGroup++) {
		m_channelGroups[iGroup]->WriteGroupValidMask(streamWriter);							// validBits[group.m_numChannelGroups]
	}
	clipStats.end(streamWriter.CreatePosition());

	// Write constant command block data array
	for (unsigned iGroup = 0; iGroup < numGroups; iGroup++) {
		Location locCommandBlock = m_channelGroups[iGroup]->WriteGroupCommandBlock(streamWriter, &clipStats);
		streamWriter.SetLink(constDataOffsetTable[iGroup], locCommandBlock, locHeader);
	}

	// Write block data array
	streamWriter.Align(16);
	for (unsigned iGroup = 0; iGroup < numGroups; iGroup++) {
		Location locBlockData = m_channelGroups[iGroup]->WriteBlockDataForSharedKeys(streamWriter, &clipStats);
		streamWriter.SetLink(blockOffsetTable[iGroup], locBlockData, locHeader);
	}

	return locHeader;
}

Location ClipDataWriter::WriteClipDataForUniformKeys2(StreamWriter& streamWriter,
													  Location& locTotalSize,
													  Location& locUserDataOffset,
													  ClipStats* stats) const
{
	ClipStats localClipStats;
	ClipStats& clipStats = stats ? *stats : localClipStats;

	using namespace ICETOOLS;

	ITASSERT(m_flags <= 0xFFFF);
	ITASSERT(m_numOutputFrames <= 0xFFFF);
	ITASSERT(m_channelGroups.size() <= 0xFFFF);

	size_t const numGroups = m_channelGroups.size();

	//-------------------------------------------------------------------------
	// Clip header

	clipStats.start("Header", streamWriter.CreatePosition());

	streamWriter.Align(16);
	Location locHeader = streamWriter.CreatePosition();

	// qword 1
	streamWriter.Write4(kAnimClipMagicVersion2_00);						// m_magic
	streamWriter.Write4(m_hierarchyId);									// m_hierarchyId
	locTotalSize = streamWriter.WriteLink(kLocTypeLink4ByteOffset);		// m_totalSize
	streamWriter.Write2((U16)numGroups);								// m_numChannelGroups (matches numProcessingGroups of AnimHierarchy)
	streamWriter.Write2((U16)m_numOutputFrames);						// m_numKeyFrames

	// qword 2
	Location groupHeadersOffset = streamWriter.WriteLink(kLocTypeLink4ByteOffset);	// m_groupHeadersOffset
	locUserDataOffset = streamWriter.WriteLink(kLocTypeLink4ByteOffset);			// m_userDataOffset
	streamWriter.WriteF(m_clipProperties.m_framesPerSecond);						// m_framesPerSecond
	streamWriter.Write2((U16)m_flags);												// m_clipFlags
	streamWriter.Write2(0);															// pad

	clipStats.end(streamWriter.CreatePosition());

	//-------------------------------------------------------------------------
	// Channel group headers

	std::vector<Location> channelDescriptorLinks;
	std::vector<Location> keyFrameLinks;
	
	clipStats.start("GroupHeaders", streamWriter.CreatePosition());

	streamWriter.Align(16);
	streamWriter.SetLink(groupHeadersOffset, streamWriter.CreatePosition(), locHeader);
	for (size_t iGroup = 0; iGroup < numGroups; iGroup++) {
		unsigned keySize = m_channelGroups[iGroup]->GetKeySize();
		channelDescriptorLinks.push_back(streamWriter.WriteLink(kLocTypeLink4ByteOffset));	// m_descriptorsOffset
		streamWriter.Write2((U16)m_channelGroups[iGroup]->GetNumConstChannelGroups());		// m_numConstDescriptors
		streamWriter.Write2((U16)m_channelGroups[iGroup]->GetNumAnimChannelGroups());		// m_numAnimDescriptors
		keyFrameLinks.push_back(streamWriter.WriteLink(kLocTypeLink4ByteOffset));			// m_keyframesOffset
		streamWriter.Write4((U32)keySize);													// m_keySize
	}
	
	clipStats.end(streamWriter.CreatePosition());

	//-------------------------------------------------------------------------
	// Valid channel masks
	
	clipStats.start("ValidBitsArrays", streamWriter.CreatePosition());

	streamWriter.Align(16);
	for (size_t iGroup = 0; iGroup < numGroups; iGroup++) {
		m_channelGroups[iGroup]->WriteValidChannelMaskBytes(streamWriter);
	}

	clipStats.end(streamWriter.CreatePosition());

	//-------------------------------------------------------------------------
	// Write channel compression formats & context data

	for (size_t iGroup = 0; iGroup < numGroups; iGroup++) {
		Location locDescriptors = m_channelGroups[iGroup]->WriteChannelDescriptors(streamWriter, &clipStats);
		streamWriter.SetLink(channelDescriptorLinks[iGroup], locDescriptors, locHeader);
	}

	//-------------------------------------------------------------------------
	// Write key frame data

	streamWriter.Align(16);
	for (size_t iGroup = 0; iGroup < numGroups; iGroup++) {
		Location locKeyFrames = m_channelGroups[iGroup]->WriteCompressedKeyFrames(streamWriter, &clipStats);
		streamWriter.SetLink(keyFrameLinks[iGroup], locKeyFrames, locHeader);
	}

	return locHeader;
}

Location ClipDataWriter::WriteClipDataForUnsharedKeys(StreamWriter &streamWriter, 
													  AnimationClipUnsharedKeyFrameBlockDesc const& unsharedKeys) const
{
	Location locTotalSize, locUserDataOffset;
	Location locClipData = WriteClipDataForUnsharedKeys(streamWriter, unsharedKeys, locTotalSize, locUserDataOffset);
	if (locClipData == kLocationInvalid)
		return kLocationInvalid;

	// Write Ice::Omega style user data
	Location locUserData = WriteIceOmegaUserDataHeaderAndData(streamWriter, m_pObjectRootAnim);
	if (locUserData != kLocationInvalid)
		streamWriter.SetLink(locUserDataOffset, locUserData, locClipData);

	streamWriter.Align(16);
	streamWriter.SetLink(locTotalSize, streamWriter.CreatePosition(), locClipData);
	return locClipData;
}

Location ClipDataWriter::WriteClipDataForUnsharedKeys(StreamWriter& streamWriter,
													  AnimationClipUnsharedKeyFrameBlockDesc const& unsharedKeys, 
													  Location &locTotalSize, 
													  Location &locUserDataOffset,
													  ClipStats* stats) const
{
	ClipStats localClipStats;
	ClipStats& clipStats = stats ? *stats : localClipStats;

	using namespace ICETOOLS;

	ITASSERT(m_flags <= 0xFFFF);
	ITASSERT(m_numOutputFrames <= 0xFFFF);
	ITASSERT(m_channelGroups.size() <= 0xFFFF);

	unsigned keyCompressionFlags = (m_flags & kClipKeyCompressionMask);
	ITASSERT((keyCompressionFlags == kClipKeysUnshared) && (unsharedKeys.m_blocksInGroup.size() == m_channelGroups.size()));

	// count the total number of key frame blocks across all channel/processing groups
    unsigned numBlocks = 0;
	unsigned numGroups = (unsigned)m_channelGroups.size();
	for (unsigned iGroup = 0; iGroup < numGroups; iGroup++) {
		numBlocks += (unsigned)unsharedKeys.m_blocksInGroup[iGroup].size();
	}
	ITASSERT(numBlocks <= 0xFFFF);

	// write the header
	clipStats.start("Header", streamWriter.CreatePosition());

	streamWriter.Align(16);
	Location locHeader = streamWriter.CreatePosition();

	streamWriter.Write4(kAnimClipMagicVersionCurrent);											// m_magic
	streamWriter.Write4(m_hierarchyId);															// m_hierarchyId
	locTotalSize = streamWriter.WriteLink(kLocTypeLink4ByteOffset);								// m_totalSize
	streamWriter.Write2((U16)numGroups);														// m_numGroups
	streamWriter.Write2((U16)numBlocks);														// m_numTotalBlocks

	streamWriter.Write4(0);																		// m_formatDataOffset
	Location groupTableOffset = streamWriter.WriteLink(kLocTypeLink4ByteOffset);				// m_groupHeadersOffset
	locUserDataOffset =	streamWriter.WriteLink(kLocTypeLink4ByteOffset);						// m_userDataOffset
	streamWriter.WriteF(m_clipProperties.m_framesPerSecond);									// m_framesPerSecond

	streamWriter.Write2((U16)m_flags);															// m_clipFlags
	streamWriter.Write2((U16)m_numOutputFrames);												// m_numTotalFrames
	streamWriter.WriteF((m_numOutputFrames > 1) ? 1.0f/(float)(m_numOutputFrames-1) : 0.0f);	// m_phasePerFrame
	streamWriter.WriteF((m_numOutputFrames > 1) ? (float)(m_numOutputFrames-1) : 0.0f);			// m_fNumFrameIntervals
	streamWriter.WriteF(1.0f/m_clipProperties.m_framesPerSecond);								// m_secondsPerFrame

	clipStats.end(streamWriter.CreatePosition());

	// Write AnimClipUnsharedKeysBlockHeader array
	std::vector<Location> blockHeaderOffsetTable;
	std::vector<Location> blockOffsetTable;
	clipStats.start("UnsharedKeysBlockHeaders", streamWriter.CreatePosition());
	streamWriter.Align(16);
	for (unsigned iGroup = 0; iGroup < numGroups; iGroup++) {
		std::vector<AnimationClipUnsharedKeyBlock> const& blocksInGroup = unsharedKeys.m_blocksInGroup[iGroup];
		Location locBlockHeaderOffset = m_channelGroups[iGroup]->WriteBlockHeaderForUnsharedKeys(streamWriter, blockOffsetTable, blocksInGroup);
		blockHeaderOffsetTable.push_back(locBlockHeaderOffset);
	}
	clipStats.end(streamWriter.CreatePosition());

	// Write AnimClipUnsharedKeysGroupHeader array
	std::vector<Location> constDataOffsetTable;
	std::vector<Location> commandBlockSizeTable;
	std::vector<Location> maxBlockDmaSizeTable;
	clipStats.start("UnsharedKeysGroupHeaders", streamWriter.CreatePosition());
	streamWriter.Align(16);
	streamWriter.SetLink(groupTableOffset, streamWriter.CreatePosition(), locHeader);
	for (unsigned iGroup = 0; iGroup < numGroups; iGroup++) {
		unsigned numBlocks = (unsigned)unsharedKeys.m_blocksInGroup[iGroup].size();
		ITASSERT( numBlocks <= 0xFFFF );

		constDataOffsetTable.push_back(streamWriter.WriteLink(kLocTypeLink4ByteOffset));	// m_commandBlockOffset
		streamWriter.Write2((U16)0);														// m_padding
		streamWriter.Write1( (U8)m_channelGroups[iGroup]->GetNumAnimatedJoints() );			// m_numJoints
		streamWriter.Write1( (U8)m_channelGroups[iGroup]->GetNumFloatChannels() );			// m_numFloatChannels

		streamWriter.WriteLink(kLocTypeLink4ByteOffset, blockHeaderOffsetTable[iGroup], locHeader); // m_blockHeaderOffset
		streamWriter.Write2((U16)numBlocks);												// m_numBlocks
		maxBlockDmaSizeTable.push_back(streamWriter.WriteLink(kLocTypeLink2ByteOffset));	// m_maxBlockDmaSize
	}
	clipStats.end(streamWriter.CreatePosition());

	// Write valid bits array
	clipStats.start("ValidBitsArrays", streamWriter.CreatePosition());
	streamWriter.Align(16);
	for (unsigned iGroup = 0; iGroup < numGroups; iGroup++) {
		m_channelGroups[iGroup]->WriteGroupValidMask(streamWriter);							// validBits[group.m_numChannelGroups]
	}
	clipStats.end(streamWriter.CreatePosition());

	// Write command block data for each channel group
	// this consists of the constant & animated runtime commands plus the constant command input data (the compressed key values for constant channels)
	streamWriter.Align(16);
	for (unsigned iGroup = 0; iGroup < numGroups; iGroup++) {
		Location locCommandBlock = m_channelGroups[iGroup]->WriteGroupCommandBlock(streamWriter, &clipStats);
		streamWriter.SetLink(constDataOffsetTable[iGroup], locCommandBlock, locHeader);
	}

	// Write key value bit stream blocks for each processing group
	// this consists of the compressed bit stream data for each animated channel in the group
	unsigned iBlockOffset = 0;
	streamWriter.Align(16);
	for (unsigned iGroup = 0; iGroup < numGroups; iGroup++) 
	{
		clipStats.start("ChanGrpBlockData", streamWriter.CreatePosition());

		unsigned numJointsInGroup = m_channelGroups[iGroup]->GetNumAnimatedJoints();
		unsigned numFloatsInGroup = m_channelGroups[iGroup]->GetNumFloatChannels();

		// track first key index in block per channel in temporary arrays :
		AnimationClipUnsharedKeyOffsets keyOffsets;
		keyOffsets.m_firstKey_scale.resize(numJointsInGroup, 0);
		keyOffsets.m_firstKey_rotation.resize(numJointsInGroup, 0);
		keyOffsets.m_firstKey_translation.resize(numJointsInGroup, 0);
		keyOffsets.m_firstKey_float.resize(numFloatsInGroup, 0);

		std::vector<AnimationClipUnsharedKeyBlock> const& blocksInGroup = unsharedKeys.m_blocksInGroup[iGroup];
		unsigned numBlocksInGroup = (unsigned)blocksInGroup.size();
		unsigned maxBlockDmaSize = 0;
		for (unsigned iBlockInGroup = 0; iBlockInGroup < numBlocksInGroup; iBlockInGroup++) 
		{
			AnimationClipUnsharedKeyBlock const& block = blocksInGroup[iBlockInGroup];

			unsigned blockDmaSize = 0;
			Location locBlockData = m_channelGroups[iGroup]->WriteBlockDataForUnsharedKeys(streamWriter, block, keyOffsets, blockDmaSize);
			
			// link m_blockOffset[iBlockInGroup] to offset of this block:
			streamWriter.SetLink(blockOffsetTable[iBlockOffset++], locBlockData, locHeader);
			
			ITASSERT(blockDmaSize <= 0xFFFF);
			if (maxBlockDmaSize < blockDmaSize) {
				maxBlockDmaSize = blockDmaSize;
			}

			for (unsigned i=0; i<numJointsInGroup; i++) {
				keyOffsets.m_firstKey_scale[i] += (unsigned)block.m_frameOffsets[kChannelTypeScale][i].size();
				keyOffsets.m_firstKey_rotation[i] += (unsigned)block.m_frameOffsets[kChannelTypeRotation][i].size();
				keyOffsets.m_firstKey_translation[i] += (unsigned)block.m_frameOffsets[kChannelTypeTranslation][i].size();
			}
			
			for (unsigned i=0; i<numFloatsInGroup; i++)	{
				keyOffsets.m_firstKey_float[i] += (unsigned)block.m_frameOffsets[kChannelTypeScalar][i].size();
			}
		}

		// link m_blockOffsetEnd to end of blocks for this group:
		streamWriter.Align(16);
		streamWriter.SetLink(blockOffsetTable[iBlockOffset++], streamWriter.CreatePosition(), locHeader);

		// set link at maxBlockDmaSizeTable[iGroup] to the maximum block dma size for this group
		streamWriter.SetLink(maxBlockDmaSizeTable[iGroup], locHeader, locHeader, maxBlockDmaSize);

		ITASSERT( m_channelGroups[iGroup]->AssertFinalKeyOffsetsValid(keyOffsets) );

		clipStats.end(streamWriter.CreatePosition());
	}

	return locHeader;
}

/// Write a compressed Hermite spline based clip to the stream
Location ClipDataWriter::WriteClipDataForHermiteKeys(StreamWriter& streamWriter,
													 Location& locTotalSize,
													 Location& locUserDataOffset,
													 ClipStats* stats) const
{
	ClipStats localClipStats;
	ClipStats& clipStats = stats ? *stats : localClipStats;

	using namespace ICETOOLS;

	ITASSERT(m_flags <= 0xFFFF);
	ITASSERT(m_numOutputFrames <= 0xFFFF);
	ITASSERT(m_channelGroups.size() <= 0xFFFF);

	unsigned keyCompressionFlags = (m_flags & kClipKeyCompressionMask);
	ITASSERT(keyCompressionFlags == kClipKeysHermite);

	size_t const numGroups = m_channelGroups.size();

	//-------------------------------------------------------------------------
	// Clip header

	clipStats.start("Header", streamWriter.CreatePosition());

	streamWriter.Align(16);
	Location locClipHeader = streamWriter.CreatePosition();

	// qword 1
	streamWriter.Write4(kAnimClipMagicVersionCurrent);					// m_magic
	streamWriter.Write4(m_hierarchyId);									// m_hierarchyId
	locTotalSize = streamWriter.WriteLink(kLocTypeLink4ByteOffset);		// m_totalSize
	streamWriter.Write2((U16)numGroups);								// m_numChannelGroups (matches numProcessingGroups of AnimHierarchy)
	streamWriter.Write2((U16)m_numOutputFrames);						// m_numKeyFrames

	// qword 2
	streamWriter.Write4(0);															// m_formatDataOffset (unused)
	Location groupHeadersOffset = streamWriter.WriteLink(kLocTypeLink4ByteOffset);	// m_groupHeadersOffset
	locUserDataOffset = streamWriter.WriteLink(kLocTypeLink4ByteOffset);			// m_userDataOffset
	streamWriter.WriteF(m_clipProperties.m_framesPerSecond);						// m_framesPerSecond

	// qword 3
	streamWriter.Write2((U16)m_flags);																// m_clipFlags
	streamWriter.Write2((U16)m_numOutputFrames);													// m_numTotalFrames
	streamWriter.WriteF((m_numOutputFrames > 1) ? 1.0f / (float)(m_numOutputFrames - 1) : 0.0f);	// m_phasePerFrame
	streamWriter.WriteF((m_numOutputFrames > 1) ? (float)(m_numOutputFrames - 1) : 0.0f);			// m_fNumFrameIntervals
	streamWriter.WriteF(1.0f / m_clipProperties.m_framesPerSecond);									// m_secondsPerFrame

	clipStats.end(streamWriter.CreatePosition());

	//-------------------------------------------------------------------------
	// Channel group headers

	clipStats.start("GroupHeaders", streamWriter.CreatePosition());

	std::vector<Location> splineHeaderOffsets;

	streamWriter.Align(16);
	streamWriter.SetLink(groupHeadersOffset, streamWriter.CreatePosition(), groupHeadersOffset);
	for (size_t iGroup = 0; iGroup < numGroups; iGroup++) {
		splineHeaderOffsets.push_back(streamWriter.WriteLink(kLocTypeLink2ByteOffset));		// m_splineHeadersOffset
		streamWriter.Write2((U16)m_channelGroups[iGroup]->GetNumFloatChannels());			// m_numSplines
	}

	clipStats.end(streamWriter.CreatePosition());

	//-------------------------------------------------------------------------
	// Valid channel masks

	clipStats.start("ValidBitsArrays", streamWriter.CreatePosition());

	streamWriter.Align(16);
	for (size_t iGroup = 0; iGroup < numGroups; iGroup++) {
		m_channelGroups[iGroup]->WriteGroupValidMask(streamWriter);
	}

	clipStats.end(streamWriter.CreatePosition());

	//-------------------------------------------------------------------------
	// Spline channel headers

	// locations of offsets in the channel headers that need to be linked 
	std::vector<Location> splineKeyOffsets;

	for (size_t iGroup = 0; iGroup < numGroups; iGroup++) {
		Location locSplineHeaders = m_channelGroups[iGroup]->WriteSplineHeaders(streamWriter, splineKeyOffsets, &clipStats);
		streamWriter.SetLink(splineHeaderOffsets[iGroup], locSplineHeaders, splineHeaderOffsets[iGroup]);
	}

	//-------------------------------------------------------------------------
	// Spline channel keys

	for (size_t iGroup = 0; iGroup < numGroups; iGroup++) {
		m_channelGroups[iGroup]->WriteSplineKeys(streamWriter, splineKeyOffsets, &clipStats);
	}

	return locClipHeader;
}

Location ClipDataWriter::WriteClipDataBareHeader(StreamWriter &streamWriter)
{
	Location locTotalSize, locUserDataOffset;
	Location locClipData = WriteClipDataBareHeader(streamWriter, locTotalSize, locUserDataOffset);
	if (locClipData == kLocationInvalid)
		return kLocationInvalid;

	// Write Ice::Omega style user data
	Location locUserData = WriteIceOmegaUserDataHeaderAndData(streamWriter, m_pObjectRootAnim);
	if (locUserData != kLocationInvalid)
		streamWriter.SetLink(locUserDataOffset, locUserData, locClipData);

	streamWriter.Align(16);
	streamWriter.SetLink(locTotalSize, streamWriter.CreatePosition(), locClipData);
	return locClipData;
}

Location ClipDataWriter::WriteClipDataBareHeader(StreamWriter &streamWriter, 
												 Location &locTotalSize, 
												 Location &locUserDataOffset)
{
	using namespace ICETOOLS;
	streamWriter.Align(16);
	Location locHeader = streamWriter.CreatePosition();

	streamWriter.Write4(kAnimClipMagicVersionCurrent);											// m_magic
	streamWriter.Write4(m_hierarchyId);															// m_hierarchyId
	locTotalSize = streamWriter.WriteLink(kLocTypeLink4ByteOffset);								// m_totalSize
	streamWriter.Write2((U16)0);																// m_numGroups
	streamWriter.Write2((U16)0);																// m_numTotalBlocks

	streamWriter.Write4(0);																		// m_formatDataOffset
	streamWriter.Write4(0);																		// m_groupHeadersOffset
	locUserDataOffset =	streamWriter.WriteLink(kLocTypeLink4ByteOffset);						// m_userDataOffset
	streamWriter.WriteF(m_clipProperties.m_framesPerSecond);									// m_framesPerSecond

	streamWriter.Write2((U16)(m_flags & ~kClipKeyCompressionMask));								// m_clipFlags
	streamWriter.Write2((U16)m_numOutputFrames);												// m_numTotalFrames
	streamWriter.WriteF((m_numOutputFrames > 1) ? 1.0f/(float)(m_numOutputFrames-1) : 0.0f);	// m_phasePerFrame
	streamWriter.WriteF((m_numOutputFrames > 1) ? (float)(m_numOutputFrames-1) : 0.0f);			// m_fNumFrameIntervals
	streamWriter.WriteF(1.0f/m_clipProperties.m_framesPerSecond);								// m_secondsPerFrame

	return locHeader;
}

Location ClipDataWriter::WriteIceOmegaUserDataHeaderAndData(StreamWriter &streamWriter, 
															AnimationClipObjectRootAnim const *pObjectRootAnim)
{
	streamWriter.Align(4);
	Location locUserDataSize;
	std::vector<Location> aLocIceOmegaHeader;
	Location locUserDataHeader = WriteIceOmegaUserDataHeader(streamWriter, 0, locUserDataSize, aLocIceOmegaHeader);
	WriteIceOmegaUserData(streamWriter, aLocIceOmegaHeader, pObjectRootAnim);
	streamWriter.SetLink(locUserDataSize, streamWriter.CreatePosition(), locUserDataHeader);
	return locUserDataHeader;
}

Location ClipDataWriter::WriteIceOmegaUserDataHeader(StreamWriter &streamWriter, 
													 U16 version, 
													 Location& locUserDataSize, 
													 std::vector<Location>& aLocIceOmegaHeader)
{
	streamWriter.Align(4);
	Location locUserDataHeader = streamWriter.CreatePosition();
	aLocIceOmegaHeader.push_back(locUserDataHeader);
	streamWriter.Write2( version );																// m_version
	locUserDataSize = streamWriter.WriteLink( ICETOOLS::kLocTypeLink2ByteOffset );				// m_size
	aLocIceOmegaHeader.push_back(streamWriter.WriteLink(ICETOOLS::kLocTypeLink4ByteOffset));	// m_rootAnimOffset
	return locUserDataHeader;
}

Location ClipDataWriter::WriteIceOmegaUserData(StreamWriter &streamWriter,
											   std::vector<Location> const& aLocIceOmegaHeader,
											   AnimationClipObjectRootAnim const *pObjectRootAnim)
{
	Location locUserData = streamWriter.CreatePosition();
	ITASSERT(aLocIceOmegaHeader.size() >= 2);
	Location locUserDataHeader = aLocIceOmegaHeader[0];
	// Write AnimClip*RootAnimData, if any
	if (pObjectRootAnim != NULL)
	{
	    streamWriter.Align(16);
		Location locRootAnimData = pObjectRootAnim->Write(streamWriter);
		streamWriter.SetLink(aLocIceOmegaHeader[1], locRootAnimData, locUserDataHeader);
	}
	return locUserData;
}

} // namespace ClipData
} // namespace Tools
} // namespace OrbisAnim
