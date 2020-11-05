/*
* Copyright (c) 2005 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "icelib/icesupport/streamwriter.h"
#include "animprocessing.h"
#include "bitcompressedarray.h"
#include "animcompression.h"

namespace OrbisAnim {
namespace Tools {
namespace AnimProcessing {

using ICETOOLS::Location;
using ICETOOLS::StreamWriter;

struct JointAnimInfo;
struct JointContext;
class ChannelGroup;

/// A helper class to construct and write data in runtime ClipData format.
class ClipDataWriter
{
public:
	ClipDataWriter();
	~ClipDataWriter();

	/// Construct runtime data from source and control data
	void BuildClipData(AnimationHierarchyDesc const& hierarchyDesc, AnimationClipProperties const& clipProperties, AnimationClipCompressedData const& compressedData);

	/// Construct runtime header from control data
	void BuildClipDataBareHeader(AnimationHierarchyDesc const& hierarchyDesc, AnimationClipProperties const& clipProperties, AnimationClipSourceData const& sourceData);

	/// Set the object root anim for this ClipData
	void SetObjectRootAnim(AnimationClipObjectRootAnim* pObjectRootAnim);

	/// Returns the object root anim for this ClipData
	AnimationClipObjectRootAnim const* GetObjectRootAnim() const 
	{ 
		return m_pObjectRootAnim; 
	}

	/// Write animation data to stream in OrbisAnim::Tools::ClipData format for uniform keys, with Ice::Omega style user data.
	/// Returns the location of the start of the ClipData written.
	Location WriteClipDataForUniformKeys(StreamWriter &streamWriter) const 
	{ 
		FrameArray emptyArray; return WriteClipDataForSharedKeys(streamWriter, emptyArray); 
	}

	/// Write animation data to stream in OrbisAnim::Tools::ClipData format for uniform keys, ready for custom user data to be written.
	/// Returns the location of the start of the ClipData written.
	/// Fills out locTotalSize with the location of U32 m_totalSize in the ClipData header, which
	/// must be patched with the final size, and locUserDataOffset with the location of the U32 m_userDataOffset
	/// which may be patched with the location of a user data header.
	Location WriteClipDataForUniformKeys(
		StreamWriter& streamWriter,
		Location& locTotalSize,
		Location& locUserDataOffset,
		ClipStats* clipStats = NULL) const
	{ 
		FrameArray emptyArray; 
		return WriteClipDataForSharedKeys(streamWriter, emptyArray, locTotalSize, locUserDataOffset, clipStats); 
	}

	/// As above but without embedded command block
	Location WriteClipDataForUniformKeys2(
		StreamWriter& streamWriter,
		Location& locTotalSize,
		Location& locUserDataOffset,
		ClipStats* clipStats = NULL) const;

	/// Write animation data to stream in OrbisAnim::Tools::ClipData format for shared non-uniform keys, with Ice::Omega style user data.
	/// Returns the location of the start of the ClipData written.
	Location WriteClipDataForSharedKeys(StreamWriter &streamWriter, FrameArray const& sharedKeys) const;

	/// Write animation data to stream in OrbisAnim::Tools::ClipData format for shared non-uniform keys, ready for custom user data to be written.
	/// Returns the location of the start of the ClipData written.
	/// Fills out locTotalSize with the location of U32 m_totalSize in the ClipData header, which
	/// must be patched with the final size, and locUserDataOffset with the location of the U32 m_userDataOffset
	/// which may be patched with the location of a user data header.
	Location WriteClipDataForSharedKeys(
		StreamWriter& streamWriter, 
		FrameArray const& sharedKeys, 
		Location& locTotalSize, 
		Location& locUserDataOffset, 
		ClipStats* clipStats = NULL) const;

	/// Write animation data to stream in OrbisAnim::Tools::ClipData format for unshared non-uniform keys, with Ice::Omega style user data.
	/// Returns the location of the start of the ClipData written.
	Location WriteClipDataForUnsharedKeys(StreamWriter &streamWriter, AnimationClipUnsharedKeyFrameBlockDesc const& unsharedKeys) const;
	
	/// Write animation data to stream in OrbisAnim::Tools::ClipData format for unshared non-uniform keys, ready for custom user data to be written.
	/// Returns the location of the start of the ClipData written.
	/// Fills out locTotalSize with the location of U32 m_totalSize in the ClipData header, which
	/// must be patched with the final size, and locUserDataOffset with the location of the U32 m_userDataOffset
	/// which may be patched with the location of a user data header.
	Location WriteClipDataForUnsharedKeys(
		StreamWriter& streamWriter, 
		AnimationClipUnsharedKeyFrameBlockDesc const& unsharedKeys, 
		Location& locTotalSize, 
		Location& locUserDataOffset,
		ClipStats* clipStats = NULL) const;

	/// Write a compressed Hermite spline based clip to the stream
	Location WriteClipDataForHermiteKeys(
		StreamWriter& streamWriter,
		Location& locTotalSize,
		Location& locUserDataOffset,
		ClipStats* clipStats = NULL) const;

	/// Writes an Ice::Omega style ClipDataUserDataHeader and data, which includes an object root anim
	static Location WriteIceOmegaUserDataHeaderAndData(StreamWriter &streamWriter, AnimationClipObjectRootAnim const *pObjectRootAnim);
	
	/// Writes an Ice::Omega style ClipDataUserDataHeader ready for extension
	/// locUserDataSize should be set to the final size of the user data with streamWriter.SetLink(locUserDataSize, locEndOfUserData, locUserDataHeader);
	static Location WriteIceOmegaUserDataHeader(StreamWriter &streamWriter, U16 version, Location& locUserDataSize, std::vector<Location>& aLocIceOmegaHeader);
	
	/// Writes an Ice::Omega style ClipDataUserDataHeader data ready for extension
	/// aLocIceOmegaHeader should be passed from an earlier call to WriteIceOmegaUserDataHeader
	static Location WriteIceOmegaUserData(StreamWriter &streamWriter, std::vector<Location> const& aLocIceOmegaHeader, AnimationClipObjectRootAnim const *pObjectRootAnim);

	/// Write a bare 48-byte ClipData header with meaningless values stubbed out, with Ice::Omega style user data.
	/// Returns the location of the start of the ClipData written.
	/// This is potentially useful for spoofing the existence of a ClipData, for instance, for streaming,
	/// in which an overall data-less ClipData header is used in external contexts to spoof the existence
	/// of a single large animation while a stream of real ClipData structures are internally substituted
	/// to the runtime.
	/// Stubbed out values include:
	///		m_numGroups=m_numTotalBlocks=m_frameTableOffset=m_groupHeadersOffset=0
	///		m_clipFlags.AnimProcessing::kClipKeyCompressionMask=0
	Location WriteClipDataBareHeader(StreamWriter &streamWriter);
	
	/// Write a bare 48-byte ClipData header with meaningless values stubbed out, ready for custom user data to be written.
	/// Returns the location of the start of the ClipData written.
	Location WriteClipDataBareHeader(StreamWriter &streamWriter, Location &locTotalSize, Location &locUserDataOffset);

protected:
	/// write shared key table
	Location WriteSharedKeyTable(StreamWriter &streamWriter, FrameArray const& sharedKeys) const;

	AnimationClipProperties			m_clipProperties;			//!< global properties of this clip
	unsigned						m_hierarchyId;				//!< unique hierarchy identifier used to check if this animation may be evaluated on a hierarchy
	unsigned						m_flags;					//!< collection of AnimationClipFlags
	unsigned						m_numOutputFrames;			//!< total number of output frames
	AnimationClipObjectRootAnim*	m_pObjectRootAnim;			//!< object root anim if this object has one, or NULL; owned by this class
	std::vector<ChannelGroup*>		m_channelGroups;			//!< our channel groups

private:
	// Disabled operator
	ClipDataWriter(ClipDataWriter const &);
	ClipDataWriter &operator=(ClipDataWriter const &);
};

} // namespace AnimProcessing
} // namespace Tools
} // namespace OrbisAnim
