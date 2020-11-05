/*
 * Copyright (c) 2003, 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include <cstdint>

#include "icelib/icesupport/bytestream.h"
#include "icelib/icesupport/bytestreamwriter.h"

#include "clipdatachannelgroup.h"
#include "clipdatawriter.h"
#include "animprocessingstructs.h"

//#pragma optimize("", off) // uncomment when debugging in release mode

namespace OrbisAnim {
namespace Tools {
namespace AnimProcessing {

using ICETOOLS::Location;
using ICETOOLS::StreamWriter;

static const unsigned kKeyCacheMaxSizeBytes = kKeyCacheMaxSize * 0x20;

// NOTE: this must match the runtime value of kCmdCopyUniformDataToKeyCache (the first clip decompression command)
// in ice/anim/iceanimbatchpriv.h
const U16 kRuntimeCommandOffset = 0xb;

// NOTE: this must match the runtime value of kCmdNumArgsShift in comandblock.h
const U32 kCmdNumArgsShift = 8;

// NOTE: these must match the order of the runtime values of kCmdCopyUniformDataToKeyCache through kCmdPostDecompressQuatLogPca
// in ice/anim/iceanimbatchpriv.h
enum AnimDecompressionCommand 
{
	kAdcInvalid = -1,

	// copy to keycache commands
	kAdcCopyUniformDataToKeyCache = 0,
	kAdcCopyUniformDataToKeyCacheFloats,
	kAdcFindKeyAndFillKeyCache,
	kAdcFindKeyAndFillKeyCacheFloats,

	// copy from keycache commands
	kAdcKeyCacheUniformLerp,
	kAdcKeyCacheUniformSlerp,
	kAdcKeyCacheUniformLerpFloats,
	kAdcKeyCacheNonUniformLerp,
	kAdcKeyCacheNonUniformSlerp,
	kAdcKeyCacheNonUniformLerpFloats,

	// const decompression commands
	kAdcConstDecompressVec3Uncompressed,
	kAdcConstDecompressVec3Float16,
	kAdcConstDecompressQuatUncompressed,
	kAdcConstDecompressQuat48SmallestThree,
	kAdcConstDecompressFloatUncompressed,

	// key cache decompression commands
	kAdcDecompressVec3Float16,
	kAdcDecompressVec3Range,
	kAdcDecompressQuatSmallestThree,
	kAdcPostDecompressQuatLog,
	kAdcPostDecompressQuatLogPca,

	// related constants
	kAdcNumCommands,
	kAdcKeyCopyCmd0 = kAdcCopyUniformDataToKeyCache,
	kAdcNumKeyCopyCommands = kAdcFindKeyAndFillKeyCacheFloats - kAdcKeyCopyCmd0 + 1,
	kAdcOutputCmd0 = kAdcKeyCacheUniformLerp,
	kAdcNumOutputCommands = kAdcKeyCacheNonUniformLerpFloats - kAdcOutputCmd0 + 1,
	kAdcConstDecompressCmd0 = kAdcConstDecompressVec3Uncompressed,
	kAdcNumConstDecompressCommands = kAdcConstDecompressFloatUncompressed - kAdcConstDecompressCmd0 + 1,
	kAdcConstDecompress3VecCmd0 = kAdcConstDecompressVec3Uncompressed,
	kAdcConstDecompressQuatCmd0 = kAdcConstDecompressQuatUncompressed,
	kAdcConstDecompressFloatCmd0 = kAdcConstDecompressFloatUncompressed,
	kAdcDecompressCmd0 = kAdcDecompressVec3Float16,
	kAdcNumDecompressCommands = kAdcPostDecompressQuatLogPca - kAdcDecompressCmd0 + 1,
};

inline AnimDecompressionCommand GetConstDecompressionCommand(AnimChannelCompressionType compressionType)
{
	switch (compressionType) {
	case kAcctConstVec3Uncompressed:	return kAdcConstDecompressVec3Uncompressed;
	case kAcctConstVec3Float16:			return kAdcConstDecompressVec3Float16;
	case kAcctConstQuatUncompressed:	return kAdcConstDecompressQuatUncompressed;
	case kAcctConstQuat48SmallestThree:	return kAdcConstDecompressQuat48SmallestThree;
	case kAcctConstFloatUncompressed:	return kAdcConstDecompressFloatUncompressed;
	default:							return kAdcInvalid;
	}
}

inline AnimDecompressionCommand GetSharedKeyCopyCommand(AnimChannelType channelType)
{
	switch (channelType) {
	case kActVec3:
	case kActQuat:	return kAdcCopyUniformDataToKeyCache;
	case kActFloat:	return kAdcCopyUniformDataToKeyCacheFloats;
	default:		return kAdcInvalid;
	}
}

inline AnimDecompressionCommand GetUnsharedKeyCopyCommand(AnimChannelType channelType)
{
	switch (channelType) {
	case kActVec3:
	case kActQuat:	return kAdcFindKeyAndFillKeyCache;
	case kActFloat:	return kAdcFindKeyAndFillKeyCacheFloats;
	default:		return kAdcInvalid;
	}
}

unsigned GetDecompressionNumCommands(AnimChannelCompressionType compressionType)
{
	switch (compressionType) {
	case kAcctQuatUncompressed:
	case kAcctVec3Uncompressed:
	case kAcctFloatUncompressed:
	default:						
		return 0;
	case kAcctVec3Float16:
	case kAcctVec3Range:
	case kAcctQuatSmallestThree:	
		return 1;
	case kAcctQuatLog:
	case kAcctQuatLogPca:			
		return 2;
	}
}

AnimDecompressionCommand GetDecompressionCommand(AnimChannelCompressionType compressionType, unsigned iCommand)
{
	switch (compressionType) {
	case kAcctVec3Uncompressed:
	case kAcctFloatUncompressed:
	case kAcctQuatUncompressed:
	default:						return kAdcInvalid;
	case kAcctVec3Float16:			return (iCommand == 0) ? kAdcDecompressVec3Float16			: kAdcInvalid;
	case kAcctVec3Range:			return (iCommand == 0) ? kAdcDecompressVec3Range			: kAdcInvalid;
	case kAcctQuatSmallestThree:	return (iCommand == 0) ? kAdcDecompressQuatSmallestThree	: kAdcInvalid;
	case kAcctQuatLog:				return (iCommand == 0) ? kAdcDecompressVec3Range :
											(iCommand == 1) ? kAdcPostDecompressQuatLog			: kAdcInvalid;
	case kAcctQuatLogPca:			return (iCommand == 0) ? kAdcDecompressVec3Range :
											(iCommand == 1) ? kAdcPostDecompressQuatLogPca		: kAdcInvalid;
	}
}

static const U32 kCommandIndexInvalid = (U32)-1;
U32 GetCommandIndexFromDecompressionCommand(AnimChannelCompressionType compressionType, AnimDecompressionCommand opCmd)
{
	switch (compressionType) {
	case kAcctVec3Uncompressed:
	case kAcctFloatUncompressed:
	case kAcctQuatUncompressed:
	default:						return kCommandIndexInvalid;
	case kAcctVec3Float16:			return (opCmd == kAdcDecompressVec3Float16 )		? 0	: kCommandIndexInvalid;
	case kAcctVec3Range:			return (opCmd == kAdcDecompressVec3Range)			? 0	: kCommandIndexInvalid;
	case kAcctQuatSmallestThree:	return (opCmd == kAdcDecompressQuatSmallestThree)	? 0	: kCommandIndexInvalid;
	case kAcctQuatLog:				return (opCmd == kAdcDecompressVec3Range)			? 0 :
										   (opCmd == kAdcPostDecompressQuatLog)			? 1	: kCommandIndexInvalid;
	case kAcctQuatLogPca:			return (opCmd == kAdcDecompressVec3Range)			? 0 :
										   (opCmd == kAdcPostDecompressQuatLogPca)		? 1 : kCommandIndexInvalid;
	}
}

inline AnimDecompressionCommand GetSharedOutputCommand(AnimChannelType channelType)
{
	switch (channelType) {
	case kActVec3:	return kAdcKeyCacheUniformLerp;
	case kActQuat:	return kAdcKeyCacheUniformSlerp;
	case kActFloat:	return kAdcKeyCacheUniformLerpFloats;
	default:		return kAdcInvalid;
	}
}

inline AnimDecompressionCommand GetUnsharedOutputCommand(AnimChannelType channelType)
{
	switch (channelType) {
	case kActVec3:	return kAdcKeyCacheNonUniformLerp;
	case kActQuat:	return kAdcKeyCacheNonUniformSlerp;
	case kActFloat:	return kAdcKeyCacheNonUniformLerpFloats;
	default:		return kAdcInvalid;
	}
}

inline unsigned GetAnimDecompressionCommandAlignment(AnimDecompressionCommand cmd)
{
	if (cmd >= kAdcDecompressCmd0 && cmd < kAdcNumCommands)
		return (cmd == kAdcDecompressVec3Float16) ? 4 : 2;	// decompress commands work on sequential multiples of 2 or 4
	return 1;		// other commands have no group size restrictions
}

// returns a measure of how different one type of compression is to another from a runtime perspective (which commands are required to decompress)
unsigned GetCommandOverhead(AnimChannelCompressionType compressionTypePrev, AnimChannelCompressionType compressionType)
{
	unsigned iOverhead = 0;
	AnimChannelType channelTypePrev = (AnimChannelType)(compressionTypePrev & kAcctChannelTypeMask);
	AnimChannelType channelType = (AnimChannelType)(compressionType & kAcctChannelTypeMask);
	if ((channelTypePrev == kActFloat) != (channelType == kActFloat))
		iOverhead++;	// key copy op is not the same
	if (channelTypePrev != channelType)
		iOverhead++;	// output op is not the same
	unsigned numCommands = GetDecompressionNumCommands(compressionType);
	unsigned numCommandsPrev = GetDecompressionNumCommands(compressionTypePrev);
	for (unsigned iCommand = 0; iCommand < numCommands; ++iCommand)
		if (iCommand >= numCommandsPrev ||
			GetDecompressionCommand(compressionType, iCommand) != GetDecompressionCommand(compressionTypePrev, iCommand))
			iOverhead++;	// decompress op is not the same
	return iOverhead;
}

struct AnimOpDataSourceGroup 
{
	AnimOpDataSourceGroup() : m_totalKeyCacheSize(0) {}
	std::vector<ChannelGroup::AnimOpDataSource> m_sources;
	unsigned m_totalKeyCacheSize;
};

ChannelGroup::AnimOp::AnimOp( AnimDecompressionCommand cmd, unsigned keyCacheOffset, unsigned blockDataOffset ) 
	: m_cmd(cmd)
	, m_keyCacheOffset(keyCacheOffset)
	, m_blockDataOffset(blockDataOffset)
	, m_keyCacheSize(0)
	, m_numItems(0)
	, m_bBlockDataEmpty(true)
{
	m_alignment = GetAnimDecompressionCommandAlignment(cmd);
}

/// Write format data to a stream writer in runtime format
void WriteFormatData(StreamWriter &streamWriter, Vec3BitPackingFormat const& format)
{
	ITASSERT(format.m_numBitsTotal <= 64);
	U8 bits = (format.m_numBitsX & 0x3F) | (format.m_xAxis << 6);
	streamWriter.Write1( bits );
	bits = (format.m_numBitsY & 0x3F) | (format.m_yAxis << 6);
	streamWriter.Write1( bits );
	streamWriter.Write1( format.m_numBitsX + format.m_numBitsY );
	bits = (format.m_numBitsZ & 0x3F) | (format.m_zAxis << 6);
	streamWriter.Write1( bits );
}

// helper class to handle queuing writes until we know whether they are all identical or not
class SizeWriter 
{
	SizeWriter const& operator=(SizeWriter const&) { ITASSERT(0); return *this; }
public:
	SizeWriter(StreamWriter &streamWriter) : m_streamWriter(streamWriter), m_numWritten(0), m_bVariableSize(false), m_size(0) {}
	void WriteSize(U8 size) {
		if (!m_numWritten) {
			m_size = size;
		} else if (m_bVariableSize) {
			m_streamWriter.Write1(size);
		} else if (size != m_size) {
			m_bVariableSize = true;
			for (size_t i = 0; i < m_numWritten; ++i)
				m_streamWriter.Write1(m_size);
			m_streamWriter.Write1(size);
		}
		++m_numWritten;
	}
	bool End() {
		if (m_numWritten && !m_bVariableSize)
			m_streamWriter.Write1(m_size);
		return m_bVariableSize;
	}
	void Init() { m_numWritten = 0, m_bVariableSize = false; }

	StreamWriter& m_streamWriter;
	size_t m_numWritten;
	bool m_bVariableSize;
	U8 m_size;
};

// helper class to handle queuing writes until we know whether they are all identical or not
class FormatWriter
{
	FormatWriter const& operator=(FormatWriter const&) { ITASSERT(0); return *this; }
public:
	FormatWriter(StreamWriter &streamWriter) : m_streamWriter(streamWriter), m_numWritten(0), m_bVariableSize(false), m_format(kVbpfNone) {}
	void WriteFormat(Vec3BitPackingFormat format) {
		if (!m_numWritten) {
			m_format = format;
		} else if (m_bVariableSize) {
			WriteFormatData(m_streamWriter, format);
		} else if (memcmp(&format, &m_format, sizeof(format))) {
			m_bVariableSize = true;
			for (size_t i = 0; i < m_numWritten; ++i)
				WriteFormatData(m_streamWriter, m_format);
			WriteFormatData(m_streamWriter, format);
		}
		++m_numWritten;
	}
	void Skip() {
		if (m_numWritten && m_bVariableSize)
			WriteFormatData(m_streamWriter, m_format);
		++m_numWritten;
	}
	bool End() {
		if (m_numWritten && !m_bVariableSize)
			WriteFormatData(m_streamWriter, m_format);
		return m_bVariableSize;
	}
	void Init() { m_numWritten = 0, m_bVariableSize = false; }

	StreamWriter& m_streamWriter;
	size_t m_numWritten;
	bool m_bVariableSize;
	Vec3BitPackingFormat m_format;
};

ChannelGroup::ChannelGroup(AnimationChannelGroup const& group, 
						   AnimationClipCompressedData const& compressedData)
	: m_firstAnimatedJoint(group.m_firstAnimatedJoint)
	, m_numAnimatedJoints(group.m_numAnimatedJoints)
	, m_firstFloatChannel(group.m_firstFloatChannel)
	, m_numFloatChannels(group.m_numFloatChannels)
	, m_sizeofKey(0)
	, m_numKeys(0)
{
	ITASSERT(m_numAnimatedJoints <= kJointGroupSize && 
			 m_numFloatChannels  <= kFloatChannelGroupSize);

	for (size_t i = 0; i < kNumChannelTypes; i++) {
		m_valid[i].clear();
	}

	// build array of compressed channels grouped by compression type
	CollectChannelGroupData(compressedData);

	// build the runtime decompression data
	unsigned keyCompressionFlags = compressedData.m_flags & kClipKeyCompressionMask;
	switch (keyCompressionFlags) {
	case kClipKeysUniform:
	case kClipKeysShared:
	case kClipKeysUniform2:
		BuildBlockDataForSharedKeys();
		break;
	case kClipKeysUnshared:
		BuildBlockDataForUnsharedKeys();
		break;
	case kClipKeysHermite:
		BuildBlockDataForSplineKeys();
		break;
	default:
		IERR("Error unknown key compression type encountered %d\n", keyCompressionFlags);
		break;
	}
}

// collect this groups channels from compressedData and group them according to type of compression applied
void ChannelGroup::CollectChannelGroupData(AnimationClipCompressedData const& compressedData)
{
	if (m_numAnimatedJoints > 0) {
		// add animated joint channels
		std::set<size_t> validScale;
		for (size_t i = 0; i < compressedData.m_anims[kChannelTypeScale].size(); ++i) {
			size_t iAnimatedJoint = compressedData.m_anims[kChannelTypeScale][i].m_id;
			size_t iLocalIndex = iAnimatedJoint - m_firstAnimatedJoint;
			if (iAnimatedJoint >= m_firstAnimatedJoint && iLocalIndex < m_numAnimatedJoints) {
				AddCompressedChannel( CompressedChannel( kChannelTypeScale, iLocalIndex, compressedData.m_anims[kChannelTypeScale][i].m_pCompressedArray ) );
				validScale.emplace(iLocalIndex);
			}
		}
		std::set<size_t> validRotation;
		for (size_t i = 0; i < compressedData.m_anims[kChannelTypeRotation].size(); ++i) {
			size_t iAnimatedJoint = compressedData.m_anims[kChannelTypeRotation][i].m_id;
			size_t iLocalIndex = iAnimatedJoint - m_firstAnimatedJoint;
			if (iAnimatedJoint >= m_firstAnimatedJoint && iLocalIndex < m_numAnimatedJoints) {
				AddCompressedChannel( CompressedChannel( kChannelTypeRotation, iLocalIndex, compressedData.m_anims[kChannelTypeRotation][i].m_pCompressedArray ) );
				validRotation.emplace(iLocalIndex);
			}
		}
		std::set<size_t> validTranslation;
		for (size_t i = 0; i < compressedData.m_anims[kChannelTypeTranslation].size(); ++i) {
			size_t iAnimatedJoint = compressedData.m_anims[kChannelTypeTranslation][i].m_id;
			size_t iLocalIndex = iAnimatedJoint - m_firstAnimatedJoint;
			if (iAnimatedJoint >= m_firstAnimatedJoint && iLocalIndex < m_numAnimatedJoints) {
				AddCompressedChannel( CompressedChannel( kChannelTypeTranslation, iLocalIndex, compressedData.m_anims[kChannelTypeTranslation][i].m_pCompressedArray ) );
				validTranslation.emplace(iLocalIndex);
			}
		}
		// add constant joint channels
		for (size_t i = 0; i < compressedData.m_const[kChannelTypeScale].size(); ++i) {
			ChannelList channels;
			std::set<size_t> used;	// index of const values in m_pCompressedArray that are used (whose joints are in this channel group)
			std::vector<size_t> const& jointIds = compressedData.m_const[kChannelTypeScale][i].m_ids;
			for (size_t j = 0; j < jointIds.size(); ++j) {
				size_t iAnimatedJoint = jointIds[j];
				size_t iLocalIndex = iAnimatedJoint - m_firstAnimatedJoint;
				if (iAnimatedJoint >= m_firstAnimatedJoint && iLocalIndex < m_numAnimatedJoints) {
					channels.push_back( Channel(kChannelTypeScale, iLocalIndex) );
					used.emplace(j);
					validScale.emplace(iLocalIndex);
				}
			}
			if (!channels.empty()) {
				AddConstChannels(channels, used, compressedData.m_const[kChannelTypeScale][i].m_pCompressedArray);
			}
		}
		for (size_t i = 0; i < compressedData.m_const[kChannelTypeRotation].size(); ++i) {
			ChannelList channels;
			std::set<size_t> used;
			std::vector<size_t> const& jointIds = compressedData.m_const[kChannelTypeRotation][i].m_ids;
			for (size_t j = 0; j < jointIds.size(); ++j) {
				size_t iAnimatedJoint = jointIds[j];
				size_t iLocalIndex = iAnimatedJoint - m_firstAnimatedJoint;
				if (iAnimatedJoint >= m_firstAnimatedJoint && iLocalIndex < m_numAnimatedJoints) {
					channels.push_back( Channel(kChannelTypeRotation, iLocalIndex) );
					used.emplace(j);
					validRotation.emplace(iLocalIndex);
				}
			}
			if (!channels.empty()) {
				AddConstChannels(channels, used, compressedData.m_const[kChannelTypeRotation][i].m_pCompressedArray);
			}
		}
		for (size_t i = 0; i < compressedData.m_const[kChannelTypeTranslation].size(); ++i) {
			ChannelList channels;
			std::set<size_t> used;
			std::vector<size_t> const& jointIds = compressedData.m_const[kChannelTypeTranslation][i].m_ids;
			for (size_t j = 0; j < jointIds.size(); ++j) {
				size_t iAnimatedJoint = jointIds[j];
				size_t iLocalIndex = iAnimatedJoint - m_firstAnimatedJoint;
				if (iAnimatedJoint >= m_firstAnimatedJoint && iLocalIndex < m_numAnimatedJoints) {
					channels.push_back(Channel(kChannelTypeTranslation, iLocalIndex));
					used.emplace(j);
					validTranslation.emplace(iLocalIndex);
				}
			}
			if (!channels.empty()) {
				AddConstChannels(channels, used, compressedData.m_const[kChannelTypeTranslation][i].m_pCompressedArray);
			}
		}
		m_valid[kChannelTypeScale] = validScale;
		m_valid[kChannelTypeRotation] = validRotation;
		m_valid[kChannelTypeTranslation] = validTranslation;
	}

	if (m_numFloatChannels > 0) {
		// add animated float channels
		for (size_t i = 0; i < compressedData.m_anims[kChannelTypeScalar].size(); ++i) {
			size_t iFloatChannel = compressedData.m_anims[kChannelTypeScalar][i].m_id;
			size_t iLocalIndex = iFloatChannel - m_firstFloatChannel;
			if (iFloatChannel >= m_firstFloatChannel && iLocalIndex < m_numFloatChannels) {
				AddCompressedChannel( CompressedChannel( kChannelTypeScalar, iLocalIndex, compressedData.m_anims[kChannelTypeScalar][i].m_pCompressedArray ) );
				m_valid[kChannelTypeScalar].emplace(iLocalIndex);
			}
		}
		// add constant float channels
		for (size_t i = 0; i < compressedData.m_const[kChannelTypeScalar].size(); ++i) {
			ChannelList channels;
			std::set<size_t> used;
			std::vector<size_t> const& floatIds = compressedData.m_const[kChannelTypeScalar][i].m_ids;
			for (size_t j = 0; j < floatIds.size(); ++j) {
				size_t iFloatChannel = floatIds[j];
				size_t iLocalIndex = iFloatChannel - m_firstFloatChannel;
				if (iFloatChannel >= m_firstFloatChannel && iLocalIndex < m_numFloatChannels) {
					channels.push_back( Channel(kChannelTypeScalar, iLocalIndex) );
					used.emplace(j);
					m_valid[kChannelTypeScalar].emplace(iLocalIndex);
				}
			}
			if (!channels.empty()) {
				AddConstChannels(channels, used, compressedData.m_const[kChannelTypeScalar][i].m_pCompressedArray);
			}
		}
	}
}

void ChannelGroup::CollectAnimOpSourceSets(std::vector<AnimOpDataSource>& sourceSets)
{
	// Collect our animated data into sets that will fit into a key cache, ordered to place
	// similar clip operations adjacent as much as possible.  For the most part, compressionType order
	// accomplishes this, except that we adjust log compressed quaternions to be adjacent to
	// range compressed vectors to make the range decompression operations adjacent.
	size_t iFirstQuatSource = (unsigned)-1;
	bool bFoundVec3Range = false, bInsertQuatLog = false;
	for (auto itByType = m_compressedChannelGroups.begin(); itByType != m_compressedChannelGroups.end(); ++itByType) 
	{
		AnimChannelCompressionType compressionTypeGroup = itByType->GetCompressionType();

		// determine required key cache alignment from decompression command type
		unsigned alignmentSource = 1;
		unsigned numCommands = GetDecompressionNumCommands(compressionTypeGroup);
		for (unsigned iCommand = 0; iCommand < numCommands; ++iCommand) {
			unsigned alignment = GetAnimDecompressionCommandAlignment( GetDecompressionCommand(compressionTypeGroup, iCommand) );
			if (alignmentSource < alignment)
				alignmentSource = alignment;
		}

		// create the sets based on item size & number of available key cache slots
		unsigned keyCacheSize = 0;
		AnimChannelType channelTypeGroup = (AnimChannelType)(compressionTypeGroup & kAcctChannelTypeMask);
		CompressedChannelList::const_iterator itStart = itByType->m_channels.begin();
		if (channelTypeGroup == kActFloat) {
			if (alignmentSource < 4) alignmentSource = 4;	// we must always deal with floats 4 to a quadword
			unsigned maxEntriesInKeyCache = kKeyCacheMaxSize / alignmentSource * alignmentSource;
			const unsigned kCacheEntrySizeBytes = sizeof(float)*2; // 8 bytes
			keyCacheSize = (unsigned)((itByType->m_channels.size() + (alignmentSource-1)) / alignmentSource * alignmentSource * kCacheEntrySizeBytes);
			while (keyCacheSize > maxEntriesInKeyCache * kCacheEntrySizeBytes) {
				AnimOpDataSource source(itStart, itStart + maxEntriesInKeyCache, alignmentSource, maxEntriesInKeyCache * kCacheEntrySizeBytes);
				sourceSets.push_back(source);
				itStart += maxEntriesInKeyCache;
				keyCacheSize -= maxEntriesInKeyCache * kCacheEntrySizeBytes;
			}
			AnimOpDataSource source(itStart, itByType->m_channels.end(), alignmentSource, keyCacheSize);
			sourceSets.push_back(source);
		} else {
			bool bDoInsertQuatLog = false;
			if (compressionTypeGroup == kAcctVec3Range) {
				bFoundVec3Range = true;
			} else if (channelTypeGroup == kActQuat) {
				if (compressionTypeGroup == kAcctQuatLog || compressionTypeGroup == kAcctQuatLog) {
					bDoInsertQuatLog = bInsertQuatLog;
				} else if (bFoundVec3Range && !bInsertQuatLog) {
					iFirstQuatSource = sourceSets.size();
					bInsertQuatLog = true;
				}
			}
			unsigned maxEntriesInKeyCache = kKeyCacheMaxSize / alignmentSource * alignmentSource;
			const unsigned kCacheEntrySizeBytes = sizeof(ITGEOM::Vec4)*2; // 32 bytes
			keyCacheSize = (unsigned)((itByType->m_channels.size() + (alignmentSource-1)) / alignmentSource * alignmentSource * kCacheEntrySizeBytes);
			while (keyCacheSize > maxEntriesInKeyCache * kCacheEntrySizeBytes) {
				AnimOpDataSource source(itStart, itStart + maxEntriesInKeyCache, alignmentSource, maxEntriesInKeyCache * kCacheEntrySizeBytes);
				if (bDoInsertQuatLog) {
					sourceSets.insert(sourceSets.begin() + iFirstQuatSource++, source);
				} else {
					sourceSets.push_back(source);
				}
				itStart += maxEntriesInKeyCache;
				keyCacheSize -= maxEntriesInKeyCache * kCacheEntrySizeBytes;
			}
			AnimOpDataSource source(itStart, itByType->m_channels.end(), alignmentSource, keyCacheSize);
			if (bDoInsertQuatLog) {
				sourceSets.insert(sourceSets.begin() + iFirstQuatSource++, source);
			} else {
				sourceSets.push_back(source);
			}
		}
	}
}

// Collects compressed animated channels into operation groups for uniform or shared keyframe data
void ChannelGroup::BuildBlockDataForSharedKeys()
{
	// set num keys & validate all channels have the same number
	m_numKeys = 0;
	for (auto itGroup = m_compressedChannelGroups.begin(); itGroup != m_compressedChannelGroups.end(); ++itGroup) {
		for (auto itChannel = itGroup->m_channels.begin(); itChannel != itGroup->m_channels.end(); ++itChannel) {
			unsigned numKeys = (unsigned)itChannel->m_pCompressedData->GetNumSamples();
			if (m_numKeys == 0) {
				m_numKeys = numKeys;
			} else {
				ITASSERT(numKeys == m_numKeys);		// for shared keys, all sources must have the same number of keys
			}
		}
	}

	// partition compressed channels into sets that will fit into the key cache
	std::vector<AnimOpDataSource> sourceSets;
	CollectAnimOpSourceSets(sourceSets);
	if (sourceSets.empty()) {
		return;
	}

	// Now iterate through our source sets assigning them to groups
	std::vector<AnimOpDataSourceGroup> sourceGroups;
	{
		auto itSourceBreakStart = sourceSets.begin(), itSourceBreakEnd = sourceSets.begin();
		unsigned iSourceBreakCost = (unsigned)-1;
		unsigned keyCacheSizeToBreak = 0, keyCacheSizeSinceBreak = 0;
		AnimChannelCompressionType compressionTypePrev = kAcctInvalid;
		for (auto itSource = sourceSets.begin(); itSource != sourceSets.end(); ++itSource) {
			AnimChannelCompressionType compressionType = itSource->m_itStart->m_pCompressedData->GetCompressionType();
			if (compressionTypePrev != kAcctInvalid) {
				unsigned iSourceBreakHereCost = GetCommandOverhead(compressionTypePrev, compressionType);
				if (iSourceBreakCost >= iSourceBreakHereCost) {
					iSourceBreakCost = iSourceBreakHereCost;
					keyCacheSizeToBreak += keyCacheSizeSinceBreak;
					itSourceBreakEnd = itSource;
					keyCacheSizeSinceBreak = 0;
				}
			}
			compressionTypePrev = compressionType;
			if (keyCacheSizeToBreak + keyCacheSizeSinceBreak + itSource->m_keyCacheSize > kKeyCacheMaxSizeBytes) {
				// If source group will fit in any previous group, append it, else start a new group.
				auto itGroup = sourceGroups.begin();
				for (; itGroup != sourceGroups.end(); ++itGroup)
					if (itGroup->m_totalKeyCacheSize + keyCacheSizeToBreak <= kKeyCacheMaxSizeBytes)
						break;
				if (itGroup == sourceGroups.end()) {
					sourceGroups.push_back( AnimOpDataSourceGroup() );
					itGroup = sourceGroups.end()-1;
				}
				std::copy( itSourceBreakStart, itSourceBreakEnd, std::back_inserter( itGroup->m_sources ) );
				itGroup->m_totalKeyCacheSize += keyCacheSizeToBreak;

				// restart iteration where we broke the last group to find the new correct min cost
				itSource = itSourceBreakStart = itSourceBreakEnd;
				keyCacheSizeToBreak = keyCacheSizeSinceBreak = 0;
				iSourceBreakCost = (unsigned)-1;
			}
			keyCacheSizeSinceBreak += itSource->m_keyCacheSize;
		}
		// If final source group will fit in any previous group, append it, else start a new group.
		keyCacheSizeToBreak += keyCacheSizeSinceBreak;
		itSourceBreakEnd = sourceSets.end();
		auto itGroup = sourceGroups.begin();
		for (; itGroup != sourceGroups.end(); ++itGroup)
			if (itGroup->m_totalKeyCacheSize + keyCacheSizeToBreak <= kKeyCacheMaxSizeBytes)
				break;
		if (itGroup == sourceGroups.end()) {
			sourceGroups.push_back( AnimOpDataSourceGroup() );
			itGroup = sourceGroups.end()-1;
		}
		std::copy( itSourceBreakStart, itSourceBreakEnd, std::back_inserter( itGroup->m_sources ) );
		itGroup->m_totalKeyCacheSize += keyCacheSizeToBreak;
	}

	// Finally, convert the groups into AnimOp's
	unsigned blockDataBitOffset = 0;
	for (auto itGroup = sourceGroups.begin(); itGroup != sourceGroups.end(); ++itGroup) {
		// add all key copy ops for this group, calculating the key cache offsets of each source as we go
		unsigned keyCacheOffset = 0;
		AnimDecompressionCommand keyCopyOpCmdPrev = kAdcInvalid;
		for (auto itSource = itGroup->m_sources.begin(); itSource != itGroup->m_sources.end(); ++itSource) {
			AnimChannelType channelType = itSource->m_itStart->m_pCompressedData->GetChannelType();
			AnimDecompressionCommand keyCopyOpCmd = GetSharedKeyCopyCommand(channelType);
			if (keyCopyOpCmd != keyCopyOpCmdPrev) {
				// start a new empty op
				blockDataBitOffset = Align(blockDataBitOffset, 8);	// input block data must be byte aligned
				m_animOps.push_back( AnimOp( keyCopyOpCmd, keyCacheOffset, blockDataBitOffset>>3 ) );
				keyCopyOpCmdPrev = keyCopyOpCmd;
			}
			// append to the last op
			m_animOps.back().AddKeyCopySource( const_cast<AnimOpDataSource&>(*itSource) );
			keyCacheOffset = m_animOps.back().GetKeyCacheEndOffset();
			for (CompressedChannelList::const_iterator it = itSource->m_itStart; it != itSource->m_itEnd; ++it)
				blockDataBitOffset += (unsigned)it->m_pCompressedData->GetBitSizeOfSample();
		}

		// add all decompress ops for this group, for iCommand = 0, then 1, ...
		unsigned iCommand = 0, numCommands = 0;
		for (iCommand = 0; iCommand == 0 || iCommand < numCommands; ++iCommand) {
			AnimDecompressionCommand decompressOpCmdPrev = kAdcInvalid;
			for (auto itSource = itGroup->m_sources.begin(); itSource != itGroup->m_sources.end(); ++itSource) {
				AnimChannelCompressionType compressionType = itSource->m_itStart->m_pCompressedData->GetCompressionType();
				unsigned numCommandsForType = GetDecompressionNumCommands(compressionType);
				if (numCommands < numCommandsForType)
					numCommands = numCommandsForType;
				if (iCommand < numCommandsForType) {
					AnimDecompressionCommand decompressOpCmd = GetDecompressionCommand(compressionType, iCommand);
					if (decompressOpCmd != decompressOpCmdPrev) {
						// start a new empty op
						m_animOps.push_back( AnimOp( decompressOpCmd ) );
						decompressOpCmdPrev = decompressOpCmd;
					}
					// append to the last op
					m_animOps.back().AddSource( *itSource );
				}
			}
		}

		// add all output ops for this group
		AnimDecompressionCommand outputOpCmdPrev = kAdcInvalid;
		for (auto itSource = itGroup->m_sources.begin(); itSource != itGroup->m_sources.end(); ++itSource) {
			AnimChannelType channelType = itSource->m_itStart->m_pCompressedData->GetChannelType();
			AnimDecompressionCommand outputOpCmd = kAdcInvalid;
			switch (channelType) {
			case kActVec3:	outputOpCmd = kAdcKeyCacheUniformLerp;			break;
			case kActQuat:	outputOpCmd = kAdcKeyCacheUniformSlerp;			break;
			case kActFloat:	outputOpCmd = kAdcKeyCacheUniformLerpFloats;	break;
			default:		ITASSERT(0);									break;
			}
			if (outputOpCmd != outputOpCmdPrev) {
				// start a new empty op
				m_animOps.push_back( AnimOp( outputOpCmd ) );
				outputOpCmdPrev = outputOpCmd;
			}
			// append to the last op
			m_animOps.back().AddSource( *itSource );
		}
	}
	m_sizeofKey = (unsigned)((blockDataBitOffset + 7)>>3);
}

// Collects compressed animated channels into operation groups for unshared keyframe data
void ChannelGroup::BuildBlockDataForUnsharedKeys()
{
	std::vector<AnimOpDataSource> sourceSets;
	CollectAnimOpSourceSets(sourceSets);

	// Now iterate through our source sets assigning them to groups
	std::vector<AnimOpDataSourceGroup> sourceGroups;
	auto itSourceBreakStart = sourceSets.begin(), itSourceBreakEnd = sourceSets.begin();
	unsigned iSourceBreakCost = (unsigned)-1;
	unsigned keyCacheSizeToBreak = 0, keyCacheSizeSinceBreak = 0;
	AnimChannelCompressionType compressionTypePrev = kAcctInvalid;
	for (auto itSource = sourceSets.begin(); itSource != sourceSets.end(); ++itSource) {
		AnimChannelCompressionType compressionType = itSource->m_itStart->m_pCompressedData->GetCompressionType();
		unsigned keyCacheAlign = 0;
		if (compressionTypePrev != kAcctInvalid) {
			unsigned iSourceBreakHereCost = GetCommandOverhead(compressionTypePrev, compressionType);
			if (iSourceBreakCost >= iSourceBreakHereCost) {
				iSourceBreakCost = iSourceBreakHereCost;
				keyCacheSizeToBreak += keyCacheSizeSinceBreak;
				itSourceBreakEnd = itSource;
				keyCacheSizeSinceBreak = 0;
			}
			// If unshared keys, we must make sure that the offset in the tween factor table is a multiple of qwords
			// when starting a new output command.
			AnimChannelType channelTypePrev = (AnimChannelType)(compressionTypePrev & kAcctChannelTypeMask);
			AnimChannelType channelType = (AnimChannelType)(compressionType & kAcctChannelTypeMask);
			if (channelType != channelTypePrev)
				keyCacheAlign = (0x80 - (keyCacheSizeToBreak + keyCacheSizeSinceBreak)) & 0x7F;
		}
		compressionTypePrev = compressionType;

		// If unshared keys, we must allocate as much space per float as we do per vector,
		// as floats take as much space as vectors in the tween factor cache:
		unsigned keyCacheSizeSource = (itSource->m_itStart->m_pCompressedData->GetChannelType() == kActFloat) ? itSource->m_keyCacheSize*4 : itSource->m_keyCacheSize;
		if (keyCacheSizeToBreak + keyCacheSizeSinceBreak + keyCacheAlign + keyCacheSizeSource > kKeyCacheMaxSizeBytes) {
			// If source group will fit in any previous group, append it, else start a new group.
			auto itGroup = sourceGroups.begin();
			for (; itGroup != sourceGroups.end(); ++itGroup) {
				unsigned keyCacheSize = itGroup->m_totalKeyCacheSize;
				AnimChannelType channelTypeSourcePrev = itGroup->m_sources.back().m_itStart->m_pCompressedData->GetChannelType();
				// If unshared keys, we must make sure that the offset in the tween factor table is a multiple of qwords
				// when starting a new output command.  We've calculated padding into keyCacheSizeToBreak, but if we
				// append to an existing group, we must recalculate all the padding between the sources we are adding:
				auto itSourceAdd = itSourceBreakStart;
				for (; itSourceAdd != itSourceBreakEnd; ++itSourceAdd) {
					AnimChannelType channelTypeSource = itSourceAdd->m_itStart->m_pCompressedData->GetChannelType();
					if (channelTypeSource != channelTypeSourcePrev) {
						keyCacheSize = Align(keyCacheSize, 128);
						channelTypeSourcePrev = channelTypeSource;
					}
					//allocate 4 times as much space for floats as they use, so that the tween factor allocations don't overlap!
					keyCacheSize += (channelTypeSource == kActFloat) ? itSourceAdd->m_keyCacheSize*4 : itSourceAdd->m_keyCacheSize;
					if (keyCacheSize > kKeyCacheMaxSizeBytes)
						break;	// early out if we overrun this group
				}
				if (keyCacheSize <= kKeyCacheMaxSizeBytes) {
					// Found a group we can fit these sources into...
					itGroup->m_totalKeyCacheSize = keyCacheSize;
					break;
				}
			}
			if (itGroup == sourceGroups.end()) {
				// Found no existing source group into which we can fit this source group;  create a new one...
				sourceGroups.push_back( AnimOpDataSourceGroup() );
				itGroup = sourceGroups.end()-1;
				itGroup->m_totalKeyCacheSize = keyCacheSizeToBreak;	// we've already calculated all padding for the group if it starts at offset 0
			}
			std::copy( itSourceBreakStart, itSourceBreakEnd, std::back_inserter( itGroup->m_sources ) );

			// restart iteration where we broke the last group to find the new correct min cost
			itSource = itSourceBreakStart = itSourceBreakEnd;
			keyCacheSizeToBreak = 0;
			iSourceBreakCost = (unsigned)-1;
			keyCacheSizeSinceBreak = keyCacheSizeSource;
		} else
			keyCacheSizeSinceBreak += keyCacheAlign + keyCacheSizeSource;
	}
	{
		// If final source group will fit in any previous group, append it, else start a new group.
		keyCacheSizeToBreak += keyCacheSizeSinceBreak;
		itSourceBreakEnd = sourceSets.end();
		auto itGroup = sourceGroups.begin();
		for (; itGroup != sourceGroups.end(); ++itGroup) {
			unsigned keyCacheSize = itGroup->m_totalKeyCacheSize;
			AnimChannelType channelTypeSourcePrev = itGroup->m_sources.back().m_itStart->m_pCompressedData->GetChannelType();
			// If unshared keys, we must make sure that the offset in the tween factor table is a multiple of qwords
			// when starting a new output command.  We've calculated padding into keyCacheSizeToBreak, but if we
			// append to an existing group, we must recalculate all the padding between the sources we are adding:
			auto itSourceAdd = itSourceBreakStart;
			for (; itSourceAdd != itSourceBreakEnd; ++itSourceAdd) {
				AnimChannelType channelTypeSource = itSourceAdd->m_itStart->m_pCompressedData->GetChannelType();
				if (channelTypeSource != channelTypeSourcePrev) {
					keyCacheSize = Align(keyCacheSize, 128);
					channelTypeSourcePrev = channelTypeSource;
				}
				//allocate 4 times as much space for floats as they use, so that the tween factor allocations don't overlap!
				keyCacheSize += (channelTypeSource == kActFloat) ? itSourceAdd->m_keyCacheSize*4 : itSourceAdd->m_keyCacheSize;
				if (keyCacheSize > kKeyCacheMaxSizeBytes)
					break;	// early out if we overrun this group
			}
			if (keyCacheSize <= kKeyCacheMaxSizeBytes) {
				// Found a group we can fit these sources into...
				itGroup->m_totalKeyCacheSize = keyCacheSize;
				break;
			}
		}
		if (itGroup == sourceGroups.end()) {
			// Found no existing source group into which we can fit this source group;  create a new one...
			sourceGroups.push_back( AnimOpDataSourceGroup() );
			itGroup = sourceGroups.end()-1;
			itGroup->m_totalKeyCacheSize = keyCacheSizeToBreak;	// we've already calculated all padding for the group if it starts at offset 0
		}
		std::copy( itSourceBreakStart, itSourceBreakEnd, std::back_inserter( itGroup->m_sources ) );
	}

	// Finally, convert the groups into AnimOp's
	for (auto itGroup = sourceGroups.begin(); itGroup != sourceGroups.end(); ++itGroup) 
	{
		// add all key copy ops for this group, calculating the key cache offsets of each source as we go
		{
			unsigned keyCacheOffset = 0;
			AnimDecompressionCommand keyCopyOpCmdPrev = kAdcInvalid;
			for (auto itSource = itGroup->m_sources.begin(); itSource != itGroup->m_sources.end(); ++itSource) {
				AnimChannelType channelType = itSource->m_itStart->m_pCompressedData->GetChannelType();
				AnimDecompressionCommand keyCopyOpCmd = GetUnsharedKeyCopyCommand(channelType);
				if (keyCopyOpCmd != keyCopyOpCmdPrev) {
					// start a new empty op
					m_animOps.push_back(AnimOp(keyCopyOpCmd, keyCacheOffset));
					keyCopyOpCmdPrev = keyCopyOpCmd;
				}
				m_animOps.back().AddKeyCopySource(const_cast<AnimOpDataSource&>(*itSource), AnimOp::kNonUniformKeyCache);
				//allocate 4 times as much space for floats as they use, so that the tween factor allocations don't overlap!
				unsigned keyCacheSizeOp = (keyCopyOpCmd == kAdcFindKeyAndFillKeyCacheFloats) ? m_animOps.back().m_keyCacheSize * 4 : m_animOps.back().m_keyCacheSize;
				keyCacheOffset = m_animOps.back().m_keyCacheOffset + keyCacheSizeOp;
				ITASSERT(keyCacheOffset <= kKeyCacheMaxSizeBytes);
			}
		}

		// add all decompress ops for this group, for iCommand = 0, then 1, ...
		{
			for (unsigned iCommand = 0, numCommands = 0; iCommand == 0 || iCommand < numCommands; ++iCommand) {
				AnimDecompressionCommand decompressOpCmdPrev = kAdcInvalid;
				for (auto itSource = itGroup->m_sources.begin(); itSource != itGroup->m_sources.end(); ++itSource) {
					AnimChannelCompressionType compressionType = itSource->m_itStart->m_pCompressedData->GetCompressionType();
					unsigned numCommandsForType = GetDecompressionNumCommands(compressionType);
					if (numCommands < numCommandsForType)
						numCommands = numCommandsForType;
					if (iCommand < numCommandsForType) {
						AnimDecompressionCommand decompressOpCmd = GetDecompressionCommand(compressionType, iCommand);
						if (decompressOpCmd != decompressOpCmdPrev) {
							// start a new empty op
							m_animOps.push_back(AnimOp(decompressOpCmd));
							decompressOpCmdPrev = decompressOpCmd;
						}
						// append to the last op
						m_animOps.back().AddSource(*itSource);
					}
				}
			}
		}

		// add all output ops for this group
		{
			AnimDecompressionCommand outputOpCmdPrev = kAdcInvalid;
			for (auto itSource = itGroup->m_sources.begin(); itSource != itGroup->m_sources.end(); ++itSource) {
				AnimChannelType channelType = itSource->m_itStart->m_pCompressedData->GetChannelType();
				AnimDecompressionCommand outputOpCmd = GetUnsharedOutputCommand(channelType);
				if (outputOpCmd != outputOpCmdPrev) {
					// start a new empty op
					m_animOps.push_back(AnimOp(outputOpCmd));
					outputOpCmdPrev = outputOpCmd;
				}
				// append to the last op
				m_animOps.back().AddSource(*itSource);
			}
		}
	}
	m_sizeofKey = 0;
	m_numKeys = 0;
}

ChannelGroup::CompressedChannelGroup const* ChannelGroup::GetCompressedChannelGroup(AnimChannelCompressionType compressionType) const
{
	for (CompressedChannelGroupList::const_iterator itByType = m_compressedChannelGroups.begin(); itByType != m_compressedChannelGroups.end(); ++itByType) {
		AnimChannelCompressionType compressionTypeGroup = itByType->GetCompressionType();
		if (compressionType == compressionTypeGroup)
			return &(*itByType);
		else if ((unsigned)compressionType < (unsigned)compressionTypeGroup)
			return NULL;
	}
	return NULL;
}

// add channel to group according to type of compression applied to it
void ChannelGroup::AddCompressedChannel(CompressedChannel const& channel)
{
	AnimChannelCompressionType compressionType = channel.m_pCompressedData->GetCompressionType();
	auto itByType = m_compressedChannelGroups.begin();
	for (; itByType != m_compressedChannelGroups.end(); ++itByType) {
		AnimChannelCompressionType compressionTypeGroup = itByType->GetCompressionType();
		if (compressionType == compressionTypeGroup) {
			itByType->AddChannel( channel );	// add to existing group
			return;
		} else if ((unsigned)compressionType < (unsigned)compressionTypeGroup) {
			break;	// no group with same type exists fall through...
		}
	}
	// create a new group if one doesn't already exist
	m_compressedChannelGroups.insert(itByType, CompressedChannelGroup(channel));	
}

void ChannelGroup::AddConstChannels(ChannelList const& channels, std::set<size_t> const& used, IBitCompressedArray const* pCompressedData)
{
	AnimChannelCompressionType compressionType = pCompressedData->GetCompressionType();
	ConstChannelGroupList::iterator itByType;
	for (itByType = m_constChannelGroups.begin(); itByType != m_constChannelGroups.end(); ++itByType) {
		AnimChannelCompressionType compressionTypeGroup = itByType->GetCompressionType();
		if ((unsigned)compressionType <= (unsigned)compressionTypeGroup) {
			if (compressionType == compressionTypeGroup) {
				itByType->m_aUsedData.push_back(used);
				itByType->m_apCompressedData.push_back(pCompressedData);
				itByType->m_channels.insert( itByType->m_channels.end(), channels.begin(), channels.end() );
				return;
			}
			break;
		}
	}
	m_constChannelGroups.insert(itByType, ConstChannelGroup(channels, used, pCompressedData));
}

size_t ChannelGroup::GetNumKeyCopyOps() const
{
	size_t numKeyCopyOps = 0;
	for (AnimOpList::const_iterator itOp = m_animOps.begin(); itOp != m_animOps.end(); ++itOp)
		if (itOp->m_cmd < kAdcKeyCopyCmd0 + kAdcNumKeyCopyCommands && itOp->m_cmd >= kAdcKeyCopyCmd0)
			if (!itOp->IsBlockDataEmpty())
				++numKeyCopyOps;
	return numKeyCopyOps;
}

Location ChannelGroup::WriteGroupValidMask(StreamWriter& streamWriter)
{
    streamWriter.Align(16);
    Location header = streamWriter.CreatePosition();
	//- valid bits - should be contiguous with following CommandBlock data ---------------------------------
	if (m_numAnimatedJoints > 0) {
		for (unsigned iBitGroup=0; iBitGroup<2; iBitGroup++) {
			U64 data = 0;
			for (unsigned iBit = 0; iBit < 64; iBit++) {
				for (size_t i = 0; i < kNumJointChannels; i++) {
					data |= m_valid[i].count(iBitGroup * 64 + iBit) ? (0x8000000000000000ULL >> iBit) : 0;
				}
			}
			streamWriter.Write8(data);
		}
	}
	if (m_numFloatChannels > 0) {
		for (unsigned iBitGroup=0; iBitGroup<2; iBitGroup++) {
			U64 data = 0;
			for (unsigned iBit = 0; iBit < 64; iBit++) {
				data |= m_valid[kChannelTypeScalar].count(iBitGroup * 64 + iBit) ? (0x8000000000000000ULL >> iBit) : 0;
			}
			streamWriter.Write8(data);
		}
	}
	return header;
}

// Writes a group valid channel mask and returns the location of the start of the data written
// uses as many bytes as required by the number of animated joints & float channels
Location ChannelGroup::WriteValidChannelMaskBytes(StreamWriter& streamWriter)
{
	streamWriter.Align(16);	// TODO - check alignment
	Location header = streamWriter.CreatePosition();
	if (m_numAnimatedJoints > 0) {
		size_t const numBytes = Align(m_numAnimatedJoints, 8) / 8;
		for (size_t iBitGroup = 0; iBitGroup < numBytes; iBitGroup++) {
			uint8_t data = 0;
			for (size_t iBit = 0; iBit < 8; iBit++) {
				for (size_t i = 0; i < kNumJointChannels; i++) {
					size_t iAnimJoint = iBitGroup * 8 + iBit;
					data |= m_valid[i].count(iAnimJoint) ? (0x80 >> iBit) : 0;
				}
			}
			streamWriter.Write1(data);
		}
	}
	//streamWriter.Align(16);	// TODO - check alignment
	if (m_numFloatChannels > 0) {
		size_t const numBytes = Align(m_numFloatChannels, 8) / 8;
		for (size_t iBitGroup = 0; iBitGroup < numBytes; iBitGroup++) {
			uint8_t data = 0;
			for (size_t iBit = 0; iBit < 8; iBit++) {
				size_t iAnimFloat = iBitGroup * 8 + iBit;
				data |= m_valid[kChannelTypeScalar].count(iAnimFloat) ? (0x80 >> iBit) : 0;
			}
			streamWriter.Write1(data);
		}
	}
	return header;
}

Location ChannelGroup::WriteGroupCommandBlock(StreamWriter& streamWriter, ClipStats* stats)
{
	using namespace ICETOOLS;

	ClipStats localClipStats;
	ClipStats& clipStats = stats ? *stats : localClipStats;

	//-------------------------------------------------------------------------
	// 1. Write constant & animated op input data to separate byte stream

	std::vector<LocationOffset> aOpDataOffsets;
	std::set<size_t> opVariableFormat;

	ByteStreamWriter::ByteVector constData;
	ByteStreamWriter constDataStream(constData, streamWriter.GetTargetEndianness(), 16);
	Location constDataStartLoc = constDataStream.CreatePosition();

	// const op data
	//clipStats.start("ConstOpData", constDataStream.CreatePosition());
	for (ConstChannelGroupList::const_iterator itConstOp = m_constChannelGroups.begin(); itConstOp != m_constChannelGroups.end(); ++itConstOp) 
	{
 		aOpDataOffsets.push_back( constDataStream.CalcOffset(constDataStream.CreatePosition(), constDataStartLoc) );
	
		// write channel offsets
		clipStats.start("ConstOffsets", constDataStream.CreatePosition());
		for (ChannelList::const_iterator itChannel = itConstOp->m_channels.begin(); itChannel != itConstOp->m_channels.end(); ++itChannel) {
			constDataStream.Write2( itChannel->m_offset );
		}
		// pad with safe offset value to next multiple of 8
		AnimChannelType channelType = itConstOp->GetChannelType();
		unsigned offsetSafe = (channelType == kActFloat) ? Align(m_numFloatChannels, 4) * kSizeofFloatChannel : m_numAnimatedJoints * kSizeofJointParams;
		ITASSERT(offsetSafe <= 0xFFFF);
		for (size_t i = itConstOp->m_channels.size(); i & 0x7; ++i) {
			constDataStream.Write2( (U16)offsetSafe );
		}
		clipStats.end(constDataStream.CreatePosition());

		// start section with channel type name for stats
		static const char* channelTypeNames[] = {
			"ConstInvalid",		// = 0x00,
			"ConstVec3",		// = 0x10,		//!< a stream of Vec3s
			"ConstQuat",		// = 0x20,		//!< a stream of Quat
			"ConstFloat"		// = 0x30,		//!< a stream of floats
		};
		clipStats.start(channelTypeNames[channelType >> 4], constDataStream.CreatePosition());

		// write compressed constant values
		// we use a ByteStream to support bit size samples
		for (size_t i = 0; i < itConstOp->m_apCompressedData.size(); ++i) {
			std::set<size_t> const& used = itConstOp->m_aUsedData[i];
			ITASSERT(used.size() <= itConstOp->m_apCompressedData[i]->GetNumSamples());
			for (std::set<size_t>::const_iterator usedIt = used.begin(); usedIt != used.end(); usedIt++) {
				itConstOp->m_apCompressedData[i]->WriteByteAlignedSample(constDataStream, *usedIt);
			}
		}
		itConstOp->m_apCompressedData[0]->WriteByteAlignedSampleEnd(constDataStream);
		constDataStream.Align(16);

		clipStats.end(constDataStream.CreatePosition());
	}
	//clipStats.end(constDataStream.CreatePosition());

	// anim op data
	//clipStats.start("AnimOpData", constDataStream.CreatePosition());
	size_t iOp = aOpDataOffsets.size();
	for (AnimOpList::const_iterator itAnimOp = m_animOps.begin(); itAnimOp != m_animOps.end(); ++itAnimOp, ++iOp) {
		AnimDecompressionCommand opCmd = itAnimOp->m_cmd;
		if (opCmd < kAdcKeyCopyCmd0 + kAdcNumKeyCopyCommands && opCmd >= kAdcKeyCopyCmd0) {
			if (!itAnimOp->IsBlockDataEmpty()) {
				clipStats.start("AnimCopyOp", constDataStream.CreatePosition());
				aOpDataOffsets.push_back(constDataStream.CalcOffset(constDataStream.CreatePosition(), constDataStartLoc));
				// key copy op data: 1 byte size per item with 0 padding where decompress op alignments split the data
				SizeWriter sizeWriter(constDataStream);
				AnimOpDataSource const* pPrevSource = NULL;
				for (auto itSource = itAnimOp->m_sources.begin(); itSource != itAnimOp->m_sources.end(); ++itSource) {
					if (pPrevSource) {
						size_t padRequired = pPrevSource->GetNumItemsPad(&*itSource);
						for (size_t iPad = 0; iPad < padRequired; ++iPad) {
							sizeWriter.WriteSize(0);
						}
					}
					pPrevSource = &*itSource;
					for (CompressedChannelList::const_iterator itChannel = itSource->m_itStart; itChannel != itSource->m_itEnd; ++itChannel) {
						unsigned size = (unsigned)itChannel->m_pCompressedData->GetBitSizeOfSample();
						ITASSERT(size <= 0x80);
						sizeWriter.WriteSize((U8)size);
					}
				}
				if (sizeWriter.End()) {
					opVariableFormat.emplace(iOp);	// flag this op as having variable size data for when we write the header
				}	
				clipStats.end(constDataStream.CreatePosition());
			}
		} else if (opCmd < kAdcDecompressCmd0 + kAdcNumDecompressCommands && opCmd >= kAdcDecompressCmd0) {
	 		aOpDataOffsets.push_back( constDataStream.CalcOffset(constDataStream.CreatePosition(), constDataStartLoc) );
			// decompress op data : command data (dependent on format) followed by bit format data (if a bit packed format)
			IBitCompressedArray const* pFirstChannel = itAnimOp->m_sources[0].m_itStart->m_pCompressedData;
			AnimChannelCompressionType compressionType = pFirstChannel->GetCompressionType();
			U32 iCommand = GetCommandIndexFromDecompressionCommand(compressionType, opCmd);
			size_t rangeDataSize = pFirstChannel->GetSizeOfRangeData(iCommand);
			if (rangeDataSize) {
				static const char* decompressNames[] = {
					"AnimDecOpVec3Float16",
					"AnimDecOpVec3Range",
					"AnimDecOpQuatSmall3",
					"AnimDecOpQuatLog",
					"AnimDecOpQuatLogPca",
				};
				clipStats.start(decompressNames[itAnimOp->m_cmd - kAdcDecompressVec3Float16], constDataStream.CreatePosition());
				IT::ByteBlock rangeData;
				IT::ByteStream byteStream(rangeData, constDataStream.GetTargetEndianness());
				size_t iNumItems = 0;
				AnimOpDataSource const* pPrevSource = NULL;
				for (auto itSource = itAnimOp->m_sources.begin(); itSource != itAnimOp->m_sources.end(); ++itSource) {
					if (pPrevSource) {
						size_t padRequired = pPrevSource->GetNumItemsPad(&*itSource);
						if (padRequired) {
							byteStream.Skip(rangeDataSize * padRequired);
							iNumItems += padRequired;
						}
					}
					pPrevSource = &*itSource;
					for (CompressedChannelList::const_iterator itChannel = itSource->m_itStart; itChannel != itSource->m_itEnd; ++itChannel, ++iNumItems) {
						itChannel->m_pCompressedData->WriteRangeData(byteStream, iCommand);
					}
				}
				pFirstChannel->WriteRangeDataEnd(byteStream, iCommand);
				byteStream.WriteToStream(constDataStream);
				clipStats.end(constDataStream.CreatePosition());
			}
			// bit format data
			if ( iCommand == 0 && !pFirstChannel->IsFixedBitPackingFormat() ) {
				clipStats.start("AnimDecOpFormat", constDataStream.CreatePosition());
				FormatWriter formatWriter(constDataStream);
				AnimOpDataSource const* pPrevSource = NULL;
				for (auto itSource = itAnimOp->m_sources.begin(); itSource != itAnimOp->m_sources.end(); ++itSource) {
					if (pPrevSource) {
						size_t padRequired = pPrevSource->GetNumItemsPad(&*itSource);
						for (size_t iPad = 0; iPad < padRequired; ++iPad) {
							formatWriter.Skip();
						}
					}
					pPrevSource = &*itSource;
					for (CompressedChannelList::const_iterator itChannel = itSource->m_itStart; itChannel != itSource->m_itEnd; ++itChannel) {
						formatWriter.WriteFormat(itChannel->m_pCompressedData->GetBitPackingFormat());
					}
				}
				if (formatWriter.End()) {
					opVariableFormat.emplace(iOp);	// flag this op as having variable size data for when we write the header
				}
				clipStats.end(constDataStream.CreatePosition());
			}
		} else if (opCmd < kAdcOutputCmd0 + kAdcNumOutputCommands && opCmd >= kAdcOutputCmd0) {
			clipStats.start("AnimOutOp", constDataStream.CreatePosition());
	 		aOpDataOffsets.push_back( constDataStream.CalcOffset(constDataStream.CreatePosition(), constDataStartLoc) );
			// output op data : offset per item with safe offset where decompress op alignments split the data
			IBitCompressedArray const* pFirstChannel = itAnimOp->m_sources[0].m_itStart->m_pCompressedData;
			AnimChannelType channelType = pFirstChannel->GetChannelType();
			unsigned offsetSafe = (channelType == kActFloat) ? Align(m_numFloatChannels, 4) * kSizeofFloatChannel : m_numAnimatedJoints * kSizeofJointParams;
			ITASSERT(offsetSafe <= 0xFFFF);
			size_t iNumItems = 0;
			for (auto itSource = itAnimOp->m_sources.begin(); itSource != itAnimOp->m_sources.end(); ++itSource) {
				// if unshared key compression, all output commands are already separated by type and will pad correctly
				for (CompressedChannelList::const_iterator itChannel = itSource->m_itStart; itChannel != itSource->m_itEnd; ++itChannel, ++iNumItems) {
					constDataStream.Write2((U16)itChannel->m_offset);
				}
				AnimOpDataSource const* pNextSource = (itSource+1 == itAnimOp->m_sources.end()) ? NULL : &*(itSource+1);
				size_t padRequired = itSource->GetNumItemsPad( pNextSource );
				for (size_t iPad = 0; iPad < padRequired; ++iPad, ++iNumItems) {
					constDataStream.Write2((U16)offsetSafe);
				}
			}
			for (size_t iPad = iNumItems; iPad & 0x7; ++iPad) {
				constDataStream.Write2((U16)offsetSafe);
			}
			clipStats.end(constDataStream.CreatePosition());
		}
		clipStats.start("AnimOpAlign", constDataStream.CreatePosition());
		constDataStream.Align(16);
		clipStats.end(constDataStream.CreatePosition());
	}
	//clipStats.end(constDataStream.CreatePosition());

	//-------------------------------------------------------------------------
	// 2. Write the constant & animated op command headers to output stream

	ITASSERT(GetNumOps() <= 0xFF && GetNumKeyCopyOps() <= 0xFF);

	U8 numOps = (U8)GetNumOps();
	unsigned keyDmaSize = GetKeySize();
	U16 const volatileDataLoc = 0x0003;		// start of volatile buffer where the keycache header resides

	clipStats.start("ConstOpHdr", streamWriter.CreatePosition());

	// we are within a ClipData block
	streamWriter.Align(16);
	Location header = streamWriter.CreatePosition();
	Location locCommandBlockStart = streamWriter.CreatePosition();

	std::vector<Location> aLocOpDataOffsets;

	// const op headers
	iOp = 0;
	for (ConstChannelGroupList::const_iterator itConstOp = m_constChannelGroups.begin(); itConstOp != m_constChannelGroups.end(); ++itConstOp, ++iOp) {
		ITASSERT(itConstOp->m_channels.size() <= 0x7FF8);
		U16 cmd = (U16)GetConstDecompressionCommand(itConstOp->GetCompressionType());
		U16 numItems = (U16)itConstOp->m_channels.size();
		U16 numAlignedItems = Align(numItems, 8);
		streamWriter.Write2( (kRuntimeCommandOffset + cmd) | (3<<kCmdNumArgsShift) );			// cmd
		streamWriter.Write2( numAlignedItems );													// numItems
		streamWriter.Write2( volatileDataLoc );													// keyCacheHeader
		aLocOpDataOffsets.push_back(streamWriter.WriteLink(kLocTypeLink2ByteOffset));			// dataOffset
	}

	clipStats.end(streamWriter.CreatePosition());

	clipStats.start("AnimOpHdr", streamWriter.CreatePosition());

	//anim op headers
	unsigned iKeyCopyOp = 0;
	for (AnimOpList::const_iterator itAnimOp = m_animOps.begin(); itAnimOp != m_animOps.end(); ++itAnimOp, ++iOp) {
		ITASSERT(itAnimOp->m_cmd <= 0xFFFF);
		ITASSERT(itAnimOp->m_numItems <= 0x7FF8);
		ITASSERT(itAnimOp->m_keyCacheOffset <= 0xFFFF);
		ITASSERT(itAnimOp->m_blockDataOffset <= 0xFFFF);
		U16 numItemsAndFlags = (U16)itAnimOp->m_numItems;
		if (opVariableFormat.count(iOp)) {
			ITASSERT(itAnimOp->m_cmd < kAdcKeyCacheUniformLerp || itAnimOp->m_cmd > kAdcKeyCacheNonUniformLerpFloats);
			numItemsAndFlags |= 0x8000;
		}
		switch (itAnimOp->m_cmd)
		{
		default:
			//m_cmdStream.AddCommand(cmd, op.m_numItems, keyCacheHeaderLoc, constOpDataLoc, op.m_keyCacheOffset);
			streamWriter.Write2( (kRuntimeCommandOffset + (U16)itAnimOp->m_cmd) | (4<<kCmdNumArgsShift) );	// cmd
			streamWriter.Write2( numItemsAndFlags );												// numItems
			streamWriter.Write2(volatileDataLoc);		                                            // keyCacheHeader
			aLocOpDataOffsets.push_back(streamWriter.WriteLink(kLocTypeLink2ByteOffset));			// dataOffset
			streamWriter.Write2( (U16)itAnimOp->m_keyCacheOffset );									// keyCacheOffset
			break;
		case kAdcCopyUniformDataToKeyCache:
		case kAdcCopyUniformDataToKeyCacheFloats:
			if (!itAnimOp->IsBlockDataEmpty())
			{
				//m_cmdStream.AddCommand(cmd, op.m_numItems, keyCacheHeaderLoc, constOpDataLoc, op.m_keyCacheOffset, op.m_blockDataOffset, keyFrameSize);
				streamWriter.Write2( (kRuntimeCommandOffset + (U16)itAnimOp->m_cmd) | (6<<kCmdNumArgsShift) );	// cmd
				streamWriter.Write2( numItemsAndFlags );												// numItems
				streamWriter.Write2(volatileDataLoc);		                                            // keyCacheHeader
				aLocOpDataOffsets.push_back(streamWriter.WriteLink(kLocTypeLink2ByteOffset));			// dataOffset
				streamWriter.Write2( (U16)itAnimOp->m_keyCacheOffset );									// keyCacheOffset
				streamWriter.Write2( (U16)itAnimOp->m_blockDataOffset );							    // blockDataOffset
				streamWriter.Write2( (U16)keyDmaSize );                                                 // keyFrameSize
			}
			break;
		case kAdcFindKeyAndFillKeyCache:
		case kAdcFindKeyAndFillKeyCacheFloats:
			if (!itAnimOp->IsBlockDataEmpty())
			{
				//m_cmdStream.AddCommand(cmd, op.m_numItems, keyCacheHeaderLoc, constOpDataLoc, op.m_keyCacheOffset, op.m_blockDataOffset);
				U16 layoutDataOffset = (U16)(0x4 * iKeyCopyOp++);
				streamWriter.Write2( (kRuntimeCommandOffset + (U16)itAnimOp->m_cmd) | (5<<kCmdNumArgsShift) );	// cmd
				streamWriter.Write2( numItemsAndFlags );												// numItems
				streamWriter.Write2(volatileDataLoc);		                                            // keyCacheHeader
				aLocOpDataOffsets.push_back(streamWriter.WriteLink(kLocTypeLink2ByteOffset));			// dataOffset
				streamWriter.Write2( (U16)itAnimOp->m_keyCacheOffset );									// keyCacheOffset
				streamWriter.Write2( layoutDataOffset );												// blockDataOffset
			}
			break;
		}
	}

	ITASSERT(iOp == numOps);

	streamWriter.Write2( 0x0000 );	// end cmd
	streamWriter.Align(16);

	clipStats.end(streamWriter.CreatePosition());

	//-------------------------------------------------------------------------
	// 3. Patch input data offsets in header stream with offsets relative to start of the headers

	Location locCommandBlockData = streamWriter.CreatePosition();
	ITASSERT(aLocOpDataOffsets.size() == aOpDataOffsets.size());
	for (unsigned i=0, numOpCmds=(unsigned)aLocOpDataOffsets.size(); i<numOpCmds; ++i)
	{
		streamWriter.SetLink(aLocOpDataOffsets[i], locCommandBlockData, locCommandBlockStart, aOpDataOffsets[i]);
	}
	
	//-------------------------------------------------------------------------
	// 4. Copy input data from step 1. to output stream
	if (!constData.empty()) 
	{
		streamWriter.Write(&*constData.begin(), constData.size());
	}

    return header;
}

// return true if all channels have the same bit packing format
bool ChannelGroup::CompressedChannelGroup::CompareBitPackingFormats() const 
{ 
	for (auto it = m_channels.begin(); it != m_channels.end(); it++) {
		bool equal = m_channels[0].m_pCompressedData->GetBitPackingFormat() == it->m_pCompressedData->GetBitPackingFormat();
		if (!equal) {
			return false;
		}
	}
	return true;
}

// returns a code >= 0 used at runtime to determine how the channel was compressed
// return -1 on error (input compression type unsupported)
static int GetRuntimeCompressionType(AnimChannelCompressionType compType)
{
	switch (compType) {
	// animated channels
	case kAcctQuatUncompressed:			return 0;
	case kAcctVec3Uncompressed:			return 1;
	case kAcctFloatUncompressed:		return 2;
	case kAcctVec3Float16:				return 3;
	case kAcctVec3Range:				return 4;
	case kAcctQuatSmallestThree:		return 5;
	case kAcctQuatLog:					return 6;
	case kAcctQuatLogPca:				return 7;
	case kAcctFloatSplineUncompressed:	return 8;
	// constant channels
	case kAcctConstQuatUncompressed:	return 9;
	case kAcctConstVec3Uncompressed:	return 10;
	case kAcctConstFloatUncompressed:	return 11;
	case kAcctConstVec3Float16:			return 12;
	case kAcctConstQuat48SmallestThree:	return 13;
	};
	// error
	return -1;
};

/// NEW UNIFORM KEY CLIP FORMAT WRITE METHOD
Location ChannelGroup::WriteChannelDescriptors(StreamWriter& streamWriter, ClipStats* /*stats*/)
{
	using namespace ICETOOLS;

	// use a local (discarded) ClipStats if one isn't pass in
	//ClipStats localClipStats;
	//ClipStats& clipStats = stats ? *stats : localClipStats;

	// TODO - check alignments are necessary
	streamWriter.Align(16);	

	// start of the channel group descriptors
	Location descStartLoc = streamWriter.CreatePosition();

	// locations of offsets in the descriptors that need to be linked
	std::vector<Location> dataOffsetLinks;

	// write constant channel group descriptors, ordered by compression type
	for (auto itGroup = m_constChannelGroups.begin(); itGroup != m_constChannelGroups.end(); ++itGroup) {
		int compType = GetRuntimeCompressionType(itGroup->GetCompressionType());
		ITASSERT(compType != -1);
		uint8_t flags = 0;
		size_t numChannelsAligned = Align(itGroup->m_channels.size(), 8);
		streamWriter.Write1((uint8_t)compType);											// compression type
		streamWriter.Write1((uint8_t)flags);											// flags
		streamWriter.Write2((uint16_t)numChannelsAligned);								// num channels (rounded to next multiple of 8)
		dataOffsetLinks.push_back(streamWriter.WriteLink(kLocTypeLink4ByteOffset));		// offset to output channel offsets & compressed values
	}

	// write animated channel group descriptors, ordered by compression type
	for (auto itGroup = m_compressedChannelGroups.begin(); itGroup != m_compressedChannelGroups.end(); ++itGroup) {
		int compType = GetRuntimeCompressionType(itGroup->GetCompressionType());
		ITASSERT(compType != -1);
		uint8_t flags = itGroup->m_allFormatsEqual ? 0 : 1;								// flag indicates 1 bit format per channel
		size_t numChannels = itGroup->m_channels.size();
		streamWriter.Write1((uint8_t)compType);											// compression type
		streamWriter.Write1((uint8_t)flags);											// flags
		streamWriter.Write2((uint16_t)numChannels);										// num channels compressed with this type
		dataOffsetLinks.push_back(streamWriter.WriteLink(kLocTypeLink4ByteOffset));		// offset to context data
		dataOffsetLinks.push_back(streamWriter.WriteLink(kLocTypeLink4ByteOffset));		// offset to key frame data
	}

	// TODO - check alignments are necessary
	streamWriter.Align(16);

	//-------------------------------------------------------------------------
	// write constant channel data, ordered by compression type

	// start of constant channel data
	Location constDataStart = streamWriter.CreatePosition();

	// iterator for data offset links in descriptors
	auto itLink = dataOffsetLinks.begin();

	for (auto itGroup = m_constChannelGroups.begin(); itGroup != m_constChannelGroups.end(); ++itGroup)	{
		
		// patch offset in const descriptor to this point in the stream
		streamWriter.SetLink(*itLink++, streamWriter.CreatePosition(), constDataStart);

		// output channel offsets
		{
			for (auto itChannel = itGroup->m_channels.begin(); itChannel != itGroup->m_channels.end(); ++itChannel) {
				streamWriter.Write2(itChannel->m_offset);
			}
			// pad with safe offset value to next multiple of 8 (runtime does 8 per loop)
			AnimChannelType channelType = itGroup->GetChannelType();
			unsigned safeOffset = (channelType == kActFloat) ? Align(m_numFloatChannels, 4) * kSizeofFloatChannel
															 : m_numAnimatedJoints * kSizeofJointParams;
			ITASSERT(safeOffset <= 0xFFFF);
			for (size_t i = itGroup->m_channels.size(); i & 0x7; ++i) {
				streamWriter.Write2((uint16_t)safeOffset);
			}
		}

		// compressed constant values
		{
			// we use a ByteStream to support bit size samples
			for (size_t i = 0; i < itGroup->m_apCompressedData.size(); ++i) {
				std::set<size_t> const& used = itGroup->m_aUsedData[i];
				ITASSERT(used.size() <= itGroup->m_apCompressedData[i]->GetNumSamples());
				for (auto itUsed = used.begin(); itUsed != used.end(); itUsed++) {
					itGroup->m_apCompressedData[i]->WriteByteAlignedSample(streamWriter, *itUsed);
				}
			}
			itGroup->m_apCompressedData[0]->WriteByteAlignedSampleEnd(streamWriter);
			
			// TODO - check alignments are necessary
			streamWriter.Align(16);
		}
	}

	//-------------------------------------------------------------------------
	// write animated channel context data, ordered by compression type

	//unsigned keySize = GetKeySize();

	// TODO - alignment?

	for (auto itGroup = m_compressedChannelGroups.begin(); itGroup != m_compressedChannelGroups.end(); ++itGroup) 
	{
		// patch offset in anim descriptor to this point in the stream
		streamWriter.SetLink(*itLink++, streamWriter.CreatePosition(), descStartLoc);

		// output channel offsets - required by all compression types
		for (auto itChannel = itGroup->m_channels.begin(); itChannel != itGroup->m_channels.end(); ++itChannel) {
			streamWriter.Write2((uint16_t)itChannel->m_offset);
		}

		// TODO - alignment?

		if (IsVariableBitPackedFormat(itGroup->GetCompressionType())) 
		{
			// bit packing formats (vec3Range, quatS3, quatLog, quatLogPca)
			{
				size_t numSpecs = itGroup->m_allFormatsEqual ? 1 : itGroup->m_channels.size();
				for (size_t iChannel = 0; iChannel < numSpecs; ++iChannel) {
					auto itChannel = itGroup->m_channels.begin() + iChannel;
					WriteFormatData(streamWriter, itChannel->m_pCompressedData->GetBitPackingFormat());
				}
			}
			// range data
			{
				// note: using a ByteStream to avoid alignment constraints of streamWriter
				// TODO: remove this extra layer if runtime requires alignment
				IT::ByteBlock rangeData;
				IT::ByteStream byteStream(rangeData, streamWriter.GetTargetEndianness());	
				// iRangeElement 0 - scale & bias for vec3Range, quatLog & quatLogPca
				// iRangeElement 1 - qMean for quatLog or qPre & qPost for quatLogPca
				size_t const kNumRangeElements = 2;
				IBitCompressedArray const* pFirstChannel = itGroup->m_channels.front().m_pCompressedData;
				for (size_t iRangeElement = 0; iRangeElement < kNumRangeElements; ++iRangeElement) {
					size_t rangeDataSize = pFirstChannel->GetSizeOfRangeData(iRangeElement);
					if (rangeDataSize) {
						for (auto itChannel = itGroup->m_channels.begin(); itChannel != itGroup->m_channels.end(); ++itChannel) {
							itChannel->m_pCompressedData->WriteRangeData(byteStream, iRangeElement);
						}
						// required for iRangeElement == 1 quatLog, swizzles 2 quats as u16's:-
						// qMean0.x, qMean1.x, qMean0.y, qMean1.y, qMean0.z, qMean1.z, qMean0.w, qMean1.w
						// see OrbisAnim::AnimClipQuatLogHeader in run time
						pFirstChannel->WriteRangeDataEnd(byteStream, iRangeElement);
					}
				}
				// write the byte stream to the "real" stream
				byteStream.WriteToStream(streamWriter);
			}
		}

		// TODO - alignment?
	}

	// TODO - is this appropriate?
	streamWriter.Align(16);

	return descStartLoc;
}

/// NEW UNIFORM KEY CLIP FORMAT WRITE METHOD
Location ChannelGroup::WriteCompressedKeyFrames(StreamWriter& streamWriter, ClipStats* stats)
{
	ClipStats localClipStats;
	ClipStats& clipStats = stats ? *stats : localClipStats;

	clipStats.start("KeyFrameSamples", streamWriter.CreatePosition());

	streamWriter.Align(16);
	Location locStart = streamWriter.CreatePosition();

	if (m_sizeofKey == 0) {
		clipStats.end(streamWriter.CreatePosition());
		return locStart;
	}

	for (unsigned iKey = 0; iKey < m_numKeys; iKey++) {
		for (auto itGroup = m_compressedChannelGroups.begin(); itGroup != m_compressedChannelGroups.end(); ++itGroup) {
			if (itGroup->m_totalBitSize==0) {
				continue;	// no compressed keys in this group (it's all in the context data)
			}
			// write sample data using a byteStream to allow misaligned writes and future bit size samples
			IT::ByteBlock sampleData;
			IT::ByteStream byteStream(sampleData, streamWriter.GetTargetEndianness());
			for (auto itChannel = itGroup->m_channels.begin(); itChannel != itGroup->m_channels.end(); ++itChannel) {
				itChannel->m_pCompressedData->WriteBitPackedSample(byteStream, iKey);
			}
			byteStream.WriteToStream(streamWriter);
		}
	}

	clipStats.end(streamWriter.CreatePosition());

	return locStart;
}

Location ChannelGroup::WriteSplineHeaders(StreamWriter& streamWriter, std::vector<Location>& splineKeyOffsets, ClipStats* /*clipStats*/)
{
	using namespace ICETOOLS;

	// TODO - check this alignment is necessary
	streamWriter.Align(16);

	// start of spline headers for this group
	Location locHeaders = streamWriter.CreatePosition();

	for (auto itGroup = m_compressedChannelGroups.begin(); itGroup != m_compressedChannelGroups.end(); ++itGroup) {
		ITASSERT(itGroup->GetCompressionType() == kAcctFloatSplineUncompressed);
		for (auto itChannel = itGroup->m_channels.begin(); itChannel != itGroup->m_channels.end(); ++itChannel) {
			itChannel->m_pCompressedData->WriteRangeData(streamWriter, 1);					// float channel id (name hash)
			streamWriter.Write2((uint16_t)itChannel->m_offset);								// channel output offset
			itChannel->m_pCompressedData->WriteRangeData(streamWriter, 0);					// pre, post infinity & number of keys
			splineKeyOffsets.push_back(streamWriter.WriteLink(kLocTypeLink2ByteOffset));	// offset to key data (16 bit offset is enough for ~5k headers)
		}
	}

	return locHeaders;
}

Location ChannelGroup::WriteSplineKeys(StreamWriter& streamWriter, std::vector<Location> const& splineKeyOffsets, ClipStats* /*clipStats*/)
{
	using namespace ICETOOLS;

	// TODO - check this alignment is necessary
	streamWriter.Align(16);

	// start of spline keys for this group
	Location locKeys = streamWriter.CreatePosition();

	// iterator for patching spline header offsets to the location of the keys we're about to create
	auto itOffsets = splineKeyOffsets.begin();

	for (auto itGroup = m_compressedChannelGroups.begin(); itGroup != m_compressedChannelGroups.end(); ++itGroup) {
		ITASSERT(itGroup->GetCompressionType() == kAcctFloatSplineUncompressed);
		for (auto itChannel = itGroup->m_channels.begin(); itChannel != itGroup->m_channels.end(); ++itChannel, ++itOffsets) {
			streamWriter.SetLink(*itOffsets, streamWriter.CreatePosition(), *itOffsets);		// patch key offset in spline header to this point in the stream
			size_t const numKeys = itChannel->m_pCompressedData->GetNumSamples();
			for (size_t keyIndex = 0; keyIndex < numKeys; keyIndex++) {
				itChannel->m_pCompressedData->WriteByteAlignedSample(streamWriter, keyIndex);	// spline knots (aka keys)
			} 
		}
	}

	return locKeys;
}

Location ChannelGroup::WriteBlockHeaderForUnsharedKeys(
	StreamWriter& streamWriter, 
	std::vector<Location>& alocBlockOffset, 
	std::vector<AnimationClipUnsharedKeyBlock> const& aBlocksInGroup)
{
	// number of frame blocks for this channel / processing group
	unsigned numBlocks = (unsigned)aBlocksInGroup.size();

	// build array of first frame index from each block
	FrameArray aFirstFrameInBlock;
	unsigned numTotalFrames = 0;
	for (unsigned i = 0; i < numBlocks; ++i)
	{
		AnimationClipUnsharedKeyBlock const& block = aBlocksInGroup[i];
		ITASSERT((i == 0 && block.m_firstFrame == 0) || (i > 0 && block.m_firstFrame == numTotalFrames-1));
		aFirstFrameInBlock.push_back( (U16)block.m_firstFrame );
		numTotalFrames = block.m_firstFrame + block.m_numFrames;
	}
	ITASSERT(numTotalFrames <= 0xFFFF);
	
	// add final frame index
	aFirstFrameInBlock.push_back( (U16)(numTotalFrames-1) );

	// write AnimClipFrameTable lookup table
	// this consists of a bit array giving the first frame index in each block & a lookup table for accelerating key frame searches at runtime
	Location locStart = WriteAnimClipFrameTable(streamWriter, aFirstFrameInBlock, numTotalFrames);

	// align to a 4 byte boundary, and write U32 m_blockOffset[numBlocks] table
	streamWriter.Align(4);
	for (unsigned i = 0; i < numBlocks; ++i)
	{
		alocBlockOffset.push_back( streamWriter.WriteLink(ICETOOLS::kLocTypeLink4ByteOffset) );
	}
	// write U32 m_blockOffsetEnd
	alocBlockOffset.push_back(streamWriter.WriteLink(ICETOOLS::kLocTypeLink4ByteOffset));

	// NOTE: 4 byte aligned at exit
	return locStart;
}

Location ChannelGroup::WriteBlockDataForSharedKeys(StreamWriter& streamWriter, ClipStats* stats)
{
	ClipStats localClipStats;
	ClipStats& clipStats = stats ? *stats : localClipStats;

	clipStats.start("ChanGrpBlockData", streamWriter.CreatePosition());

	streamWriter.Align(16);
    Location locStart = streamWriter.CreatePosition();

	if (m_sizeofKey == 0) {
		clipStats.end(streamWriter.CreatePosition());
		return locStart;
	}

	for (unsigned iKey = 0; iKey < m_numKeys; iKey++) {
		for (AnimOpList::const_iterator itOp = m_animOps.begin(); itOp != m_animOps.end(); ++itOp) {
			if (itOp->m_cmd >= kAdcKeyCopyCmd0 + kAdcNumKeyCopyCommands || itOp->m_cmd < kAdcKeyCopyCmd0) {
				continue;
			}
			if (itOp->IsBlockDataEmpty()) {
				continue;
			}
			// write sample data using a byteStream to allow misaligned writes and future bit size samples
			IT::ByteBlock sampleData;
			IT::ByteStream byteStream(sampleData, streamWriter.GetTargetEndianness());
			for (auto itSource = itOp->m_sources.begin(); itSource != itOp->m_sources.end(); ++itSource) {
				for (CompressedChannelList::const_iterator itChannel = itSource->m_itStart; itChannel != itSource->m_itEnd; ++itChannel) {
					itChannel->m_pCompressedData->WriteBitPackedSample(byteStream, iKey);
				}
			}
			itOp->m_sources[0].m_itStart->m_pCompressedData->WriteBitPackedSampleEnd(byteStream);
			byteStream.WriteToStream(streamWriter);
		}
	}

	clipStats.end(streamWriter.CreatePosition());

    return locStart;
}

Location ChannelGroup::WriteBlockDataForUnsharedKeys(
	StreamWriter& streamWriter, 
	AnimationClipUnsharedKeyBlock const& block, 
	AnimationClipUnsharedKeyOffsets const& keyOffsets, 
	unsigned& blockDmaSize)
{
    streamWriter.Align(16);
    Location locStart = streamWriter.CreatePosition();

	std::vector<unsigned> const*const firstKeyByType[kNumChannelTypes] = {
		&keyOffsets.m_firstKey_scale, &keyOffsets.m_firstKey_rotation, &keyOffsets.m_firstKey_translation, &keyOffsets.m_firstKey_float
	};

	U16 const numFrames = (U16)block.m_numFrames;
	ITASSERT(numFrames <= 33);

	U32 const frameTableSize = (numFrames + 6)/8;	// pjm: max numFrames == 33, (33+6)/8 == 4, so the +6 (instead of +7) is to keep frameTablesize within 4 bytes, i think! :S
//	U32 const frameTableSize = (numFrames > 49) ? 8 : (numFrames + 6)/8;

	blockDmaSize = 0;

	// write block layout table for each key copy op
	std::vector<Location> dataOffsetLocs, frameTableSizeLocs;
	for (AnimOpList::const_iterator itOp = m_animOps.begin(); itOp != m_animOps.end(); ++itOp) {
		if (itOp->m_cmd >= kAdcKeyCopyCmd0 + kAdcNumKeyCopyCommands || itOp->m_cmd < kAdcKeyCopyCmd0)
			continue;
		if (itOp->IsBlockDataEmpty())
			continue;
		dataOffsetLocs.push_back( streamWriter.WriteLink( ICETOOLS::kLocTypeLink2ByteOffset ) );		// m_dataOffset | (frameTableSize-1) (of one frameTable)
		frameTableSizeLocs.push_back( streamWriter.WriteLink( ICETOOLS::kLocTypeLink2ByteOffset ) );	// m_frameTableSize (of all frame tables)
		blockDmaSize += 4;
	}

	unsigned iKeyCopyOp = 0;
	for (AnimOpList::const_iterator itOp = m_animOps.begin(); itOp != m_animOps.end(); ++itOp) {
		if (itOp->m_cmd >= kAdcKeyCopyCmd0 + kAdcNumKeyCopyCommands || itOp->m_cmd < kAdcKeyCopyCmd0)
			continue;
		if (itOp->IsBlockDataEmpty())
			continue;
		// align each key copy op data to 4 byte boundary
		streamWriter.Align(4);
		blockDmaSize = Align(blockDmaSize, 4);
		Location keyStartLoc = streamWriter.CreatePosition();
		streamWriter.SetLink( dataOffsetLocs[iKeyCopyOp], keyStartLoc, locStart, frameTableSize-1 );	// misuse offset arg to OR frameTableSize-1 into low 2 bits of m_dataOffset (see above) (maxFrameTableSize == 4)
		// write key time values
		unsigned numFrameTables = 0;
		AnimOpDataSource const* pPrevSource = NULL;
		for (auto itSource = itOp->m_sources.begin(); itSource != itOp->m_sources.end(); ++itSource) {
			if (pPrevSource) {
				// must pad key time values internally with 0 entries where there are size==0 entries in the indices for alignment:
				size_t padRequired = pPrevSource->GetNumItemsPad( &*itSource );
				streamWriter.Skip(padRequired * frameTableSize);
				blockDmaSize += (unsigned)(padRequired * frameTableSize);
				numFrameTables += (unsigned)padRequired;
			}
			pPrevSource = &*itSource;
			for (CompressedChannelList::const_iterator itChannel = itSource->m_itStart; itChannel != itSource->m_itEnd; ++itChannel) {
				FrameOffsetArray const& frameOffsetArray = block.m_frameOffsets[itChannel->m_type][itChannel->m_index];
				unsigned numKeys = (unsigned)frameOffsetArray.size();
				ITASSERT(frameOffsetArray[numKeys-1] == numFrames-1);
				unsigned iKey = 1;
				for (unsigned iFrameTableByte = 0; iFrameTableByte < frameTableSize; ++iFrameTableByte) {
					U8 frameTableByte = 0;
					unsigned firstFrameInByte = iFrameTableByte*8 + 1, lastFrameInByte = firstFrameInByte + 7;
					while (iKey < numKeys && (unsigned)frameOffsetArray[iKey] <= lastFrameInByte)
						frameTableByte |= (U8)(0x80 >> (frameOffsetArray[iKey++] - firstFrameInByte));
					streamWriter.Write1(frameTableByte);
				}
				blockDmaSize += frameTableSize;
				++numFrameTables;
			}
		}
		ITASSERT(numFrameTables == itOp->GetNumItems());
		streamWriter.SetLink( frameTableSizeLocs[iKeyCopyOp], streamWriter.CreatePosition(), keyStartLoc );
		// write sample data using a byteStream to allow misaligned writes and bit size samples
		IT::ByteBlock sampleData;
		IT::ByteStream byteStream(sampleData, streamWriter.GetTargetEndianness());
		for (auto itSource = itOp->m_sources.begin(); itSource != itOp->m_sources.end(); ++itSource) {
			for (CompressedChannelList::const_iterator itChannel = itSource->m_itStart; itChannel != itSource->m_itEnd; ++itChannel) {
				size_t numKeys = block.m_frameOffsets[itChannel->m_type][itChannel->m_index].size();
				size_t iKeyStart = (*firstKeyByType[itChannel->m_type])[itChannel->m_index];
				size_t iKeyEnd = iKeyStart + numKeys;
				ITASSERT(iKeyEnd <= itChannel->m_pCompressedData->GetNumSamples());
				for (size_t iKey = iKeyStart; iKey < iKeyEnd; ++iKey) {
					itChannel->m_pCompressedData->WriteBitPackedSample(byteStream, iKey);
				}
				itChannel->m_pCompressedData->WriteBitPackedSampleEnd(byteStream);
			}
		}
		byteStream.WriteToStream(streamWriter);
		blockDmaSize += (unsigned)byteStream.GetSize();
		++iKeyCopyOp;
	}

	blockDmaSize = Align(blockDmaSize, 16);
	return locStart;
}

// Checks if keyOffsets contains the total number of keys for each channel after a full series of calls to WriteBlockDataForUnsharedKeys
bool ChannelGroup::AssertFinalKeyOffsetsValid(AnimationClipUnsharedKeyOffsets const& keyOffsets) const
{
	std::vector<unsigned> const*const firstKeyByType[kNumChannelTypes] = {
		&keyOffsets.m_firstKey_scale, &keyOffsets.m_firstKey_rotation, &keyOffsets.m_firstKey_translation, &keyOffsets.m_firstKey_float
	};
	for (AnimOpList::const_iterator itOp = m_animOps.begin(); itOp != m_animOps.end(); ++itOp) {
		if (itOp->m_cmd >= kAdcKeyCopyCmd0 + kAdcNumKeyCopyCommands || itOp->m_cmd < kAdcKeyCopyCmd0)
			continue;
		if (itOp->IsBlockDataEmpty())
			continue;
		for (auto itSource = itOp->m_sources.begin(); itSource != itOp->m_sources.end(); ++itSource) {
			for (CompressedChannelList::const_iterator itChannel = itSource->m_itStart; itChannel != itSource->m_itEnd; ++itChannel) {
				unsigned numTotalKeys = (*firstKeyByType[itChannel->m_type])[itChannel->m_index];
				if (numTotalKeys != itChannel->m_pCompressedData->GetNumSamples()) {
					ITASSERT(numTotalKeys == itChannel->m_pCompressedData->GetNumSamples());
					return false;
				}
			}
		}
	}
	return true;
}

// Writes data for a frame index to key index map and returns the location of the start of the data written
Location WriteAnimClipFrameTable(StreamWriter& streamWriter, FrameArray const& aFramesOrdered, unsigned numTotalFrames)
{
	streamWriter.Align(16);
	Location locStart = streamWriter.CreatePosition();

	unsigned numTotalKeys = (unsigned)aFramesOrdered.size();
	ITASSERT(numTotalFrames <= 0xFFFF);
	ITASSERT(numTotalKeys <= numTotalFrames);
	ITASSERT(numTotalKeys >= 2 || numTotalKeys == numTotalFrames);	// must always have at least 1 key at start and end
	if (numTotalFrames == 0)
		return locStart;
	ITASSERT(aFramesOrdered.front() == 0 && aFramesOrdered.back() == (U16)(numTotalFrames-1));

	// write bit array indicating the first frame in each block
	// Write U8 m_frameTable[ numTotalFrames/8 ] bit table;
	// Write a multiple of 32 bits to ensure 4-byte alignment at end of table
	unsigned numTotalFramesAligned = Align(numTotalFrames, 32);
	unsigned iByteNextWrite = 0, iByte;
	U8 byte = 0;
	U16 frameNext = 0;
	for (unsigned i = 0; i < numTotalKeys; i++)
	{
		U16 frame = aFramesOrdered[i];
		ITASSERT((frame >= frameNext) && (frame < numTotalFrames));
		iByte = frame / 8;
		for (; iByteNextWrite < iByte; ++iByteNextWrite)
		{
			streamWriter.Write1( byte );
			byte = 0;
		}
		byte |= (0x80 >> (frame & 0x7));
		frameNext = frame + 1;
	}
	iByte = numTotalFramesAligned / 8;
	for (; iByteNextWrite < iByte; ++iByteNextWrite)
	{
		streamWriter.Write1( byte );
		byte = 0;
	}

	// Write AnimClipFrameTableLookUp m_lookUp[] acceleration table:
	// used to speed up key frame searches at runtime
	//	struct AnimClipFrameTableLookUp
	//	{
	//		U16		m_numKeysBefore;		// total number of keys in all qwords before this one
	//		U8		m_lastFrameBefore;		// frame index of last key before this qword; for qword i, frame index of last key in qwords i-2,i-1 (0..255)
	//		U8		m_firstFrameAfter;		// frame index of first key after this qword; for qword i, frame index of first key in qwords i+1,i+2 (0..255)
	//	};
	unsigned numLookUpEntries = ((unsigned)numTotalFrames + 127)/128;
	unsigned iLookUpEntryNextWrite = 0, iLookUpEntry;
	U16 numKeysBefore = 0;
	U16 lastFrame = 0;

	// there are never any frames before the first qword
	streamWriter.Write2( 0 );	//m_numKeysBefore
	streamWriter.Write1( 0 );	//m_lastFrameBefore

	if (numLookUpEntries > 1)
	{
		for (unsigned i = 0; i < numTotalKeys-1; i++)
		{
			U16 frame = aFramesOrdered[i];
			iLookUpEntry = frame / 128;
			while (iLookUpEntryNextWrite < iLookUpEntry)
			{
				U16 firstFrameAfter = (U16)( frame - (iLookUpEntryNextWrite+1)*128 );
				ITASSERT( firstFrameAfter <= 0xFF );
				streamWriter.Write1( (U8)firstFrameAfter );		//m_firstFrameAfter
				if (++iLookUpEntryNextWrite < numLookUpEntries)
				{
					U16 lastFrameBefore = (U16)( lastFrame - (iLookUpEntryNextWrite-2)*128 );
					ITASSERT( lastFrameBefore <= 0xFF );
					streamWriter.Write2( numKeysBefore );		//m_numKeysBefore
					streamWriter.Write1( (U8)lastFrameBefore );	//m_lastFrameBefore
				}
			}
			++numKeysBefore;
			lastFrame = frame;
		}
	}
	// there are never any frames after the final qword
	streamWriter.Write1( (U8)0 );	//m_firstFrameAfter
	if (++iLookUpEntryNextWrite < numLookUpEntries)
	{
		ITASSERT(iLookUpEntryNextWrite == numLookUpEntries);	// if we had a final key at numTotalFrames-1, we should never end up here
		while (++iLookUpEntryNextWrite < numLookUpEntries)
		{
			U16 lastFrameBefore = (U16)( lastFrame - (iLookUpEntryNextWrite-2)*128 );
			ITASSERT( lastFrameBefore <= 0xFF );
			streamWriter.Write2( numKeysBefore );		//m_numKeysBefore
			streamWriter.Write1( (U8)lastFrameBefore );	//m_lastFrameBefore
			streamWriter.Write1( (U8)0 );				//m_firstFrameAfter
		}
	}

	// NOTE: exits 4-byte aligned
	return locStart;
}

}	//namespace AnimProcessing
}	//namespace Tools
}	//namespace OrbisAnim
