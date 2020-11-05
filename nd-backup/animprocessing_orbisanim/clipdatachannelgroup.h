/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include <vector>
#include <set>

#include "icelib/common/basictypes.h"
#include "icelib/geom/cgvec.h"
#include "icelib/geom/cgquat.h"
#include "icelib/common/error.h"
#include "icelib/icesupport/streamwriter.h"

#include "animprocessing.h"
#include "bitcompressedarray.h"

namespace OrbisAnim {
namespace Tools {
namespace AnimProcessing {

using ICETOOLS::StreamWriter;
using ICETOOLS::Location;

/// An internal class used by ClipDataWriter to collect and write data for a runtime
/// ClipData channel group.
class ChannelGroup
{
public:
	/// Construct an uninitialized ChannelGroup
	ChannelGroup() {}
	/// Construct a ChannelGroup from compressedData in the specified range of animated joints and float channels
	ChannelGroup(AnimationChannelGroup const& group, AnimationClipCompressedData const& compressedData);
	/// Destruct a ChannelGroup
	~ChannelGroup() {}

	/// Returns the index of the first animated joint in this channel group
	unsigned GetFirstAnimatedJoint() const { return m_firstAnimatedJoint; }
	/// Returns the number of joints in this channel group
	unsigned GetNumAnimatedJoints() const { return m_numAnimatedJoints; }
	/// Returns the index of the first float channel in this channel group
	unsigned GetFirstFloatChannel() const { return m_firstFloatChannel; }
	/// Returns the number of float channels in this channel group
	unsigned GetNumFloatChannels() const { return m_numFloatChannels; }

	/// Returns the key size in bytes for this channel group
	unsigned GetKeySize() const { return m_sizeofKey; }

	/// Writes a group valid mask and returns the location of the start of the data written
	Location WriteGroupValidMask(StreamWriter &streamWriter);
	/// Writes a group CommandBlock and returns the location of the start of the data written
	Location WriteGroupCommandBlock(StreamWriter &streamWriter, ClipStats* clipStats = NULL);
			
	/// Writes an AnimClipUnsharedKeysBlockHeader block header for the given blocks and returns the location
	/// of the start of the data written.
	/// Also pushes blocksInGroup.size()+1 locations of block offsets to be patched later in alocBlockOffset,
	/// one for each block, and one final offset of the end of the blocks for this group.
	Location WriteBlockHeaderForUnsharedKeys(StreamWriter &streamWriter, std::vector<Location>& alocBlockOffset, std::vector<AnimationClipUnsharedKeyBlock> const& blocksInGroup);
	/// Writes the data for only block of shared keys data and returns the location of the start of the data written.
	Location WriteBlockDataForSharedKeys(StreamWriter &streamWriter, ClipStats* clipStats = NULL);
	/// Writes the data for the given block of unshared keys data and returns the location of the start of the data written.
	/// Returns the dma size of the block data written in blockDmaSize.
	Location WriteBlockDataForUnsharedKeys(StreamWriter &streamWriter, AnimationClipUnsharedKeyBlock const& block, AnimationClipUnsharedKeyOffsets const& keyOffsets, unsigned& blockDmaSize);

	/// asserts that keyOffsets contains the total number of keys for each channel after a full series of calls to WriteBlockDataForUnsharedKeys
	bool AssertFinalKeyOffsetsValid(AnimationClipUnsharedKeyOffsets const& keyOffsets) const;

	/// NEW UNIFORM KEY CLIP FORMAT WRITE METHODS...

	/// Writes a group valid channel mask and returns the location of the start of the data written
	Location WriteValidChannelMaskBytes(StreamWriter &streamWriter);
	/// Writes channel decompression descriptors for this group and returns the location of the start of the data written
	Location WriteChannelDescriptors(StreamWriter& streamWriter, ClipStats* clipStats = NULL);
	/// Writes the compressed key frame data and returns the location of the start of the data written.
	Location WriteCompressedKeyFrames(StreamWriter& streamWriter, ClipStats* clipStats = NULL);

	Location WriteSplineHeaders(StreamWriter& streamWriter, std::vector<Location>& splineKeyOffsets, ClipStats* clipStats = NULL);
	Location WriteSplineKeys(StreamWriter& streamWriter, std::vector<Location> const& splineKeyOffsets, ClipStats* clipStats = NULL);

	size_t GetNumConstChannelGroups() const { return m_constChannelGroups.size(); }
	size_t GetNumAnimChannelGroups() const { return m_compressedChannelGroups.size(); }

protected:
	friend struct AnimOpDataSourceGroup;
		
	/// one animated output channel
	struct Channel 
	{
		ChannelType	m_type;			//!< Channel type, one of kChannelType*
		size_t		m_index;		//!< Local index of joint or float channel
		U16			m_offset;		//!< Output data offset

		Channel(ChannelType type, size_t index) 
			: m_type(type)
			, m_index(index)
			, m_offset(0) 
		{
			size_t offset = 0;
			switch(m_type) {
			case kChannelTypeScale:
			case kChannelTypeRotation:
			case kChannelTypeTranslation:
				offset = m_index * AnimProcessing::kSizeofJointParams + (U8)m_type * AnimProcessing::kSizeofVector;
				break;
			case kChannelTypeScalar:
				offset = m_index * AnimProcessing::kSizeofScalar;
				break;
			}
			ITASSERT(offset < 0xffff);
			m_offset = (U16)offset;
		}
	};
	typedef std::vector<Channel> ChannelList;
			
	/// one animated output channel and the compressed animation data associated with it
	struct CompressedChannel : public Channel 
	{
		IBitCompressedArray const* m_pCompressedData;	//!< Compressed data for channel - not owned by this struct

		CompressedChannel(ChannelType type, size_t index, IBitCompressedArray const* pCompressedData) 
			: Channel(type, index)
			, m_pCompressedData(pCompressedData)
		{
			static const AnimChannelType s_channelTypeMap[] = { kActVec3, kActQuat, kActVec3, kActFloat };
			ITASSERT( m_pCompressedData && m_pCompressedData->GetChannelType() == s_channelTypeMap[type] );
		}
		~CompressedChannel() {}
	};
	typedef std::vector<CompressedChannel> CompressedChannelList;
	typedef CompressedChannelList::const_iterator CompressedChannelListIter;

	/// a group of animated output channels with the same compression type
	struct CompressedChannelGroup 
	{
		CompressedChannelList m_channels;		//!< CompressedChannel's in this group
		size_t m_totalBitSize;					//!< sum of m_pCompressedData->GetSampleSizeBits() for this group
		bool m_allFormatsEqual;					//!< true if all bit packing format are equal		

		CompressedChannelGroup(CompressedChannel const &channel) : m_totalBitSize(0), m_allFormatsEqual(true) { AddChannel(channel); }
		void AddChannel(CompressedChannel const &channel) {
			m_channels.push_back(channel);
			m_totalBitSize += channel.m_pCompressedData->GetBitSizeOfSample();
			m_allFormatsEqual &= m_channels.front().m_pCompressedData->GetBitPackingFormat() == channel.m_pCompressedData->GetBitPackingFormat();
		}
		AnimChannelType GetChannelType() const { return m_channels[0].m_pCompressedData->GetChannelType(); }
		AnimChannelCompressionType GetCompressionType() const { return m_channels[0].m_pCompressedData->GetCompressionType(); }
		bool CompareBitPackingFormats() const;
	};
	typedef std::vector<CompressedChannelGroup> CompressedChannelGroupList;

	/// structure to hold a group of constant output channels of the same compression kind, and the compressed data for all of them
	struct ConstChannelGroup 
	{
		ChannelList m_channels;											//!< Concatenation of channels that have been compressed into each m_apCompressedData, in the same order
		std::vector< std::set<size_t> > m_aUsedData;					//!< Array of bit sets indicating which samples in each IBitCompressedData are used by this channel group
		std::vector<IBitCompressedArray const*>	m_apCompressedData;		//!< Array of compressed constant data for these channels (not owned by this struct)

		ConstChannelGroup() {}
		ConstChannelGroup(ChannelList const& channels, std::set<size_t> const& used, IBitCompressedArray const* pCompressedData) : m_channels(channels), m_aUsedData(1, used), m_apCompressedData(1, pCompressedData) {}
		AnimChannelType GetChannelType() const { return m_apCompressedData[0]->GetChannelType(); }
		AnimChannelCompressionType GetCompressionType() const { return m_apCompressedData[0]->GetCompressionType(); }
	};
	typedef std::vector<ConstChannelGroup> ConstChannelGroupList;

	/// Structure describing a set of CompressedChannels of one compression kind that will
	/// act as part of the input data to an animation clip operation.
	struct AnimOpDataSource 
	{
		CompressedChannelListIter m_itStart;	//!< iterator at first entry of this data source from a CompressedChannelList stored in m_compressedChannelGroups
		CompressedChannelListIter m_itEnd;		//!< iterator at end entry of this data source from a CompressedChannelList stored in m_compressedChannelGroups
		unsigned m_alignment;					//!< key cache alignment of this data source - i.e. key cache is processed in groups of m_alignment entries, causing up to (m_alignment-1) entries of padding to avoid overwriting the following data
		unsigned m_keyCacheSize;				//!< key cache size of this data source in bytes
		unsigned m_keyCacheOffset;				//!< key cache offset of this data source, once it has been added to an AnimOp

		AnimOpDataSource( CompressedChannelListIter itStart, CompressedChannelListIter itEnd, unsigned alignment, unsigned keyCacheSize) :
			m_itStart(itStart), m_itEnd(itEnd), m_alignment(alignment), m_keyCacheSize(keyCacheSize), m_keyCacheOffset((unsigned)-1)
		{
			ITASSERT(m_keyCacheSize == (unsigned)(GetItemKeyCacheSize() * GetNumItemsAligned()));
		}
		size_t GetNumItems() const { return (m_itEnd - m_itStart); }
		size_t GetNumItemsAligned() const { return (GetNumItems() + (size_t)m_alignment-1) / (size_t)m_alignment * (size_t)m_alignment; }
		size_t GetNumItemsPad() const { return GetNumItemsAligned() - GetNumItems(); }
		size_t GetNumItemsPad(AnimOpDataSource const* pNextSource) const
		{
			size_t padRequired = GetNumItemsPad();
			if (pNextSource) {
				ITASSERT(pNextSource->m_keyCacheOffset >= m_keyCacheOffset + m_keyCacheSize);
				padRequired += (pNextSource->m_keyCacheOffset - (m_keyCacheOffset + m_keyCacheSize))/GetItemKeyCacheSize();
			}
			return padRequired;
		}
		size_t GetItemKeyCacheSize() const { return (m_itStart->m_type == kChannelTypeScalar) ? 0x8 : 0x20; }
		AnimChannelType GetItemAnimChannelType() const 
		{
			return (m_itStart->m_type == kChannelTypeRotation)  ? kActQuat :
					(m_itStart->m_type == kChannelTypeScalar) ? kActFloat :
					                                            kActVec3;	// kChannelTypeScale or kChannelTypeTrans
		}
		bool IsBlockDataEmpty() const
		{
			for (CompressedChannelList::const_iterator it = m_itStart; it != m_itEnd; ++it)
				if (it->m_pCompressedData->GetBitSizeOfSample() != 0)
					return false;
			return true;
		}
	};

	/// Structure describing one clip operation involved in decompressing animated clip data and its input data sources.
	struct AnimOp 
	{
		enum AnimDecompressionCommand m_cmd;		//!< one of AnimationDecompressionCommand kAdc* run-time commands
		unsigned m_keyCacheOffset;					//!< the start of the location in the key cache of the data this clip operation will act upon
		unsigned m_blockDataOffset;					//!< if this is a key copy operation, the offset of the source data in the input block
		unsigned m_alignment;						//!< the alignment of this command - i.e. this command can only handle key cache data in sets of m_alignment and will overwrite up to (m_alignment-1) entries
		unsigned m_keyCacheSize;					//!< total size of this AnimOp's data in the key cache
		unsigned m_numItems;						//!< the number of items, with internal padding but not command alignment padding, that this operation will process
		std::vector<AnimOpDataSource> m_sources;	//!< a list of the data sources that will be concatenated for processing by this command
		bool m_bBlockDataEmpty;

		AnimOp( enum AnimDecompressionCommand cmd, unsigned keyCacheOffset = 0, unsigned blockDataOffset = 0);

		enum KeyCacheType { kUniformKeyCache = 0, kNonUniformKeyCache = 1 };

		void AddKeyCopySource( AnimOpDataSource &source, KeyCacheType keyCacheType = kUniformKeyCache ) 
		{
			unsigned sourceKeyCacheOffset = m_keyCacheOffset + m_keyCacheSize;
			unsigned itemKeyCacheSize = (unsigned)source.GetItemKeyCacheSize();
			if (keyCacheType != kUniformKeyCache && (m_sources.empty() || source.GetItemAnimChannelType() != m_sources.back().GetItemAnimChannelType())) {
				// for unshared keys, new output commands (indicated by a change in AnimChannelType) must be aligned in tween factor cache:
				unsigned sourceKeyCacheAlignment = 0x80;
				unsigned keyCacheAlign = (sourceKeyCacheAlignment - sourceKeyCacheOffset) & (sourceKeyCacheAlignment - 1);
				if (!m_sources.empty()) {
					m_keyCacheSize += keyCacheAlign;
					m_numItems += (unsigned)m_sources.back().GetNumItemsPad() + (keyCacheAlign / itemKeyCacheSize);
				} else {
					m_keyCacheOffset += keyCacheAlign;
				}
				sourceKeyCacheOffset += keyCacheAlign;
			} else if (!m_sources.empty())
				m_numItems += (unsigned)m_sources.back().GetNumItemsPad();
			ITASSERT(source.m_keyCacheOffset == (unsigned)-1);
			source.m_keyCacheOffset = sourceKeyCacheOffset;
			m_sources.push_back(source);
			m_bBlockDataEmpty = m_bBlockDataEmpty && source.IsBlockDataEmpty();
			//ITASSERT(!m_bBlockDataEmpty);	// why would the source data be empty?
			m_numItems += (unsigned)source.GetNumItems();
			m_keyCacheSize += (unsigned)source.GetNumItemsAligned() * itemKeyCacheSize;
			ITASSERT(!(m_keyCacheSize & 0x1F));
		}
				
		void AddSource( AnimOpDataSource const &source ) 
		{
			ITASSERT(source.m_keyCacheOffset != (unsigned)-1);
			ITASSERT(source.m_keyCacheSize >= source.GetItemKeyCacheSize() * source.GetNumItems());
			if (m_sources.empty()) {
				m_keyCacheOffset = source.m_keyCacheOffset;
				m_numItems = (unsigned)source.GetNumItems();
				m_keyCacheSize = source.m_keyCacheSize;
			} else {
				ITASSERT(source.m_keyCacheOffset >= m_keyCacheOffset + m_keyCacheSize);
				m_numItems = (unsigned)(((source.m_keyCacheOffset - m_keyCacheOffset) / source.GetItemKeyCacheSize()) + source.GetNumItems());
				m_keyCacheSize = source.m_keyCacheOffset + source.m_keyCacheSize - m_keyCacheOffset;
			}
			m_sources.push_back(source);
			ITASSERT(!(m_keyCacheSize & 0x1F));
		}

		size_t GetNumItems() const { return (size_t)m_numItems; }
		size_t GetNumItemsAligned() const { return ((size_t)GetNumItems() + (size_t)m_alignment-1) / (size_t)m_alignment * (size_t)m_alignment; }
		unsigned GetKeyCacheEndOffset() const { return m_keyCacheOffset + m_keyCacheSize; }
		bool IsBlockDataEmpty() const { return m_bBlockDataEmpty; }
	};
	typedef std::vector< AnimOp > AnimOpList;

	/// Collects compressed data in this channel group from compressedData
	void CollectChannelGroupData(AnimProcessing::AnimationClipCompressedData const& compressedData);
	/// Collects our data into source sets
	void CollectAnimOpSourceSets(std::vector<AnimOpDataSource>& sourceSets);
	/// Collects compressed animated channels into operation groups for uniform or shared keyframe data
	void BuildBlockDataForSharedKeys();
	/// Collects compressed animated channels into operation groups for unshared keyframe data
	void BuildBlockDataForUnsharedKeys();
	/// Collects compressed animated channels into operation groups for spline keyframe data
	void BuildBlockDataForSplineKeys();

	/// Returns a pointer to the CompressedChannelGroup in m_compressedChannelGroups matching the given compression kind or NULL if none match
	CompressedChannelGroup const* GetCompressedChannelGroup(AnimChannelCompressionType compressionType) const;
	/// Adds a CompressedChannel to m_compressedChannelGroups, appending to an existing group if one matches the compression kind, or starting a new group if not
	void AddCompressedChannel(CompressedChannel const &channel);
	/// Adds a data for const channels to m_constChannelGroups
	void AddConstChannels(ChannelList const& channels, std::set<size_t> const& used, IBitCompressedArray const* pCompressedData);

	size_t GetNumAnimOps() const { return m_animOps.size(); }
	size_t GetNumKeyCopyOps() const;
	size_t GetNumConstOps() const { return m_constChannelGroups.size(); }
	size_t GetNumOps() const { return GetNumConstOps() + GetNumAnimOps(); }

	std::set<size_t>			m_valid[kNumChannelTypes];	//!< index set of animated joints/float in the group that are affected by this animation
	unsigned					m_firstAnimatedJoint;		//!< index of first animated joint in this blend group
	unsigned					m_firstFloatChannel;		//!< index of first float channel in this blend group
	unsigned					m_numAnimatedJoints;		//!< total number of animated joints in this blend group
	unsigned					m_numFloatChannels;			//!< total number of float channels in this blend group
	unsigned					m_sizeofKey;				//!< the total number of bytes required to store one value from each animated channel of this group (0 if unshared keys)
	unsigned					m_numKeys;					//!< the total number of keys worth of animated data for this group (0 if no animated channels or if unshared keys)
	CompressedChannelGroupList	m_compressedChannelGroups;	//!< compressed channels, grouped by compression kind in increasing order
	ConstChannelGroupList		m_constChannelGroups;		//!< compressed const channels, grouped by compression kind in increasing order
	AnimOpList					m_animOps;					//!< the final sequence of operations performed at runtime to decompress & tween this group of channels
};

/// Writes data for a frame index to key index map and returns the location of the start of the data written
Location WriteAnimClipFrameTable(StreamWriter& streamWriter, AnimProcessing::FrameArray const& aFramesOrdered, unsigned numTotalFrames);

}	//namespace AnimProcessing
}	//namespace Tools
}	//namespace OrbisAnim
