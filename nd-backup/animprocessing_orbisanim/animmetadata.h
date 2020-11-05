/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */


#pragma once

#include <vector>
#include <string>
#include <set>

#include "bitcompressedarray.h"
#include "animerrortolerance.h"

namespace OrbisAnim {
	namespace Tools {
		namespace AnimProcessing {

		/// flags applying to the compression of an animation : matched to AnimationClip::Flags
		enum AnimMetaDataFlags
		{
			kAnimMetaDataFlagKeysUniform =				0x0000,
			kAnimMetaDataFlagKeysShared =				0x0010,
			kAnimMetaDataFlagKeysUnshared =				0x0020,
			kAnimMetaDataFlagKeysUniform2 =				0x0040,
			kAnimMetaDataFlagKeysHermite =				0x0080,
			kAnimMetaDataFlagKeysMask =					0x00f0,

			kAnimMetaDataFlagRootSpaceErrors =			0x0100,	// Errors in this metadata are specified in object root space;
																// local space errors must be generated for each joint for
																// each frame based on hierarchy and current pose.
			kAnimMetaDataFlagDeltaOptimization =		0x0200,	// With kAnimMetaDataFlagRootSpaceErrors: use errordelta and 
																// errorspeed values to adjust local space error tolerances 
																// based on root space speed in current frame.
			kAnimMetaDataFlagGlobalOptimization =		0x0400,	// When bit compressing data, optimize bit compression errors 
																// by adjusting child target values to compensate for parent 
																// bit-compression errors.
			kAnimMetaDataFlagRemoveConstChannels =		0x0800	// Remove constant channels that are equal to the default pose
		};

		/// flags applying to the compression of a joint parameter
		enum AnimMetaDataBitFormatFlags
		{
			kAnimMetaDataFlagGenerateBitFormat =		0x80000000,	//!< should generate a bit format for this channel based on error tolerances
			kAnimMetaDataFlagGenerateBitFormatShared =	0x40000000,	//!< generated bit format should be shared with all channels of same type (vector, quaternion, or float) with same label index 
			kAnimMetaDataMaskFormatLabelIndex =			0x00FFFFFF,	//!< label index = (m_flags & kAnimMetaDataMaskFormatLabelIndex), used to determine sharing groups
		};

		enum AnimMetaDataKeyFramesFlags
		{
			kAnimMetaDataFlagGenerateSkipList =			0x80000000,	//!< should generate skip frames for this channel based on error tolerances
		};

		extern const float kfConstErrorToleranceUseDefault;
		extern const float kConstErrorToleranceAuto;

		/// structure containing a description of one joint parameter bit format
		struct AnimMetaDataTrackBitFormat
		{
			U32							m_flags;				//!< union of AnimMetaDataBitFormatFlags
			AnimChannelCompressionType	m_compressionType;		//!< compression type to apply to animated channels
			AnimChannelCompressionType	m_constCompressionType;	//!< compression type to apply to constant channels
			Vec3BitPackingFormat		m_bitFormat;			//!< if (!(m_flags & kAnimMetaDataFlagGenerateBitFormat))
			float						m_fConstErrorTolerance;	//!< error tolerance used to determine if this channel is constant
			float						m_fErrorTolerance;		//!< local space error tolerance for bit compression

			AnimMetaDataTrackBitFormat()
				:	m_flags(0)
				,	m_compressionType(kAcctInvalid)
				,	m_constCompressionType(kAcctInvalid)
				,	m_bitFormat(kVbpfNone)
				,	m_fConstErrorTolerance(kfConstErrorToleranceUseDefault)
				,	m_fErrorTolerance(0.0f)
			{
			}
			AnimMetaDataTrackBitFormat(AnimChannelType channelType)
				:	m_flags(0)
				,	m_compressionType((AnimChannelCompressionType)(channelType | kAcctCompressionTypeUncompressed))
				,	m_constCompressionType((AnimChannelCompressionType)(kAcctConstMask | channelType | kAcctCompressionTypeUncompressed))
				,	m_bitFormat(kVbpfNone)
				,	m_fConstErrorTolerance(kfConstErrorToleranceUseDefault)
				,	m_fErrorTolerance(0.0f)
			{
			}
		};

		/// structure containing a full description of one joint parameter compression method
		struct AnimMetaDataTrackKeyFrames
		{
			U32							m_flags;				//!< union of AnimMetaDataKeyFramesFlags
			std::set<size_t>			m_skipFrames;
			std::set<size_t>			m_keepFrames;
			float						m_fErrorTolerance;		//!< local space error tolerance used if generating skip frames

			AnimMetaDataTrackKeyFrames() : m_flags(0), m_fErrorTolerance(0.0f) {}
		};

		struct AnimMetaDataTrackRootSpaceError
		{
			float						m_fErrorTolerance;		//!< root space error tolerance used to generate local space error tolerance for both keyframe and bit compression
			float						m_fErrorDeltaFactor;	//!< if (metaData.m_flags & kAnimMetaDataFlagDeltaOptimization), expands error tolerance by m_fErrorDeltaFactor * (track rate of change)[iFrame]
			float						m_fErrorSpeedFactor;	//!< if (metaData.m_flags & kAnimMetaDataFlagDeltaOptimization) and joint.scale or joint.rotation track, expands error tolerance by m_fErrorSpeedFactor * (joint.translation rate of change)[iFrame]

			AnimMetaDataTrackRootSpaceError() : m_fErrorTolerance(0.0f), m_fErrorDeltaFactor(0.0f), m_fErrorSpeedFactor(0.0f) {}
		};

		/// structure containing a full description of one track (joint parameter or float channel) compression method
		struct AnimMetaDataTrackCompressionMethod
		{
			AnimMetaDataTrackBitFormat			m_format;
			AnimMetaDataTrackKeyFrames			m_keyFrames;
			AnimMetaDataTrackRootSpaceError		m_rootSpaceError;

			AnimMetaDataTrackCompressionMethod() {}
			AnimMetaDataTrackCompressionMethod(AnimChannelType channelType) : m_format(channelType) {}
		};

		/// structure containing a description of the compression methods to apply to one joint
		struct AnimMetaDataJointCompressionMethod
		{
			AnimMetaDataTrackCompressionMethod	m_scale;
			AnimMetaDataTrackCompressionMethod	m_rotation;
			AnimMetaDataTrackCompressionMethod	m_translation;

			AnimMetaDataJointCompressionMethod() : m_scale(kActVec3), m_rotation(kActQuat), m_translation(kActVec3) {}
		};

		/// structure containing a description of the compression methods to apply to an animation
		struct AnimMetaData
		{
			U16													m_flags;				//!< union of AnimMetaDataFlags
			U16													m_maxKeysPerBlock;		//!< if unshared keys, max keys per block
			U32													m_maxBlockSize;			//!< if unshared keys, max block size
			AnimMetaDataJointCompressionMethod					m_defaultCompression;	//!< format to use for any joints not listed below
			AnimMetaDataTrackCompressionMethod					m_defaultCompressionFloat;	//!< format to use for any float channels not listed below
			AnimMetaDataTrackKeyFrames							m_sharedKeyFrames;		//!< if (keys==shared) contains keyframe data
			std::vector< std::string >							m_jointNames;			//!< names or partial names of joints
			std::vector< std::string >							m_floatNames;			//!< names or partial names of float channels
			std::vector< AnimMetaDataJointCompressionMethod >	m_jointCompression;		//!< compression method per joint
			std::vector< AnimMetaDataTrackCompressionMethod >	m_floatCompression;		//!< compression method per float channel

			AnimMetaData() : m_flags(0), m_maxKeysPerBlock(0), m_maxBlockSize(0), m_defaultCompressionFloat(kActFloat) {}
		};

		/// loads the XML meta-data file returns the configurations
		///     \param metaData		Out: meta data
		///     \param filename		In:  the XML file to load
		///     \param additionalIncludeFiles	In:  additional XML files to load as defaults before filename
		///     \param animname		In:  the name of the animation to parse the metadata file for
		///     \param bindposename	In:  the name of the bindpose to parse the metadata file for
		bool ProcessMetadataFile( AnimMetaData* metaData, std::string const& filename, std::vector<std::string> const& additionalIncludeFiles, std::string const& animname, std::string const& bindposename );

		/// returns the metadata format attribute string corresponding to the given compression kind
		const char *GetFormatAttribName(AnimChannelCompressionType compressionType);

		/// returns true if name matches partialName, which is true if name == partialName or if name ends in "|partialName"
		bool NameMatchesAnimMetaDataName(std::string const& name, std::string const& partialName);

		/// returns the index in partialNameList of an entry with NameMatchesPartialName(name, partialNameList[i]) or -1 if none match
		int FindAnimMetaDataIndexForName(std::string const& name, std::vector<std::string> const& partialNameList);

		/// Returns true if the given metaData is set to leave all channels uncompressed.
		/// A just constructed AnimMetaData is in this state, for instance.
		bool AnimMetaDataIsUncompressed(AnimMetaData const& metaData);

		}	//namespace AnimProcessing
	}	//namespace Tools
}	//namespace OrbisAnim

