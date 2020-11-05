/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include <string>

#include "icelib/icesupport/typeconv.h"		// for Vec3BitPackingFormat

#include "animprocessing.h"
#include "bitcompressedarray.h"

namespace OrbisAnim {
	namespace Tools {
		namespace AnimProcessing {

		/// Internal flags used to mark already processed data per joint param
		/// Note that these may be used to exclude joint params from processing by the AnimClipCompression_* functions below
		enum FormatFlags 
		{
			kFormatFlagBitFormatDefined =	0x0001,
			kFormatFlagKeyFramesDefined =	0x0002,
			kFormatFlagConstant =			0x0004,
		};

		/// A structure containing data fully describing the compression of one track/joint param of an AnimationClip
		struct ChannelCompressionDesc
		{
			AnimChannelCompressionType		m_compressionType;		//!< compression type
			unsigned						m_flags;				//!< union of FormatFlags
			Vec3BitPackingFormat			m_bitFormat;			//!< if bit-packed compression type, output bit-packing format (NOTE: float channels only use m_numBytes here)
			U64_Array						m_contextData;			//!< for compression types which require auxiliary context data (quat log, logpca)
		};

		/// A structure containing data fully describing the compression of an AnimationClip
		struct ClipCompressionDesc
		{
			unsigned						m_flags;				//!< compression flags, union of AnimationClip::Flags and AnimClipCompressionFlags
			unsigned						m_maxKeysPerBlock;		//!< if shared keys, max number of keys in a block; if unshared keys, max number of frames in a block
			unsigned						m_maxBlockSize;			//!< if unshared keys, max size of a block in bytes

			std::array<std::vector<ChannelCompressionDesc>, kNumChannelTypes>	m_format;		//!< 1 entry per animated joint or float channel (for each channel type)

			std::set<size_t>				m_sharedSkipFrames;							//!< If shared keys, members of this set are frame indices to skip
			std::vector< std::set<size_t> >	m_unsharedSkipFrames[kNumChannelTypes];		//!< If unshared keys, 1 per animated joint or float channel: 
																						//!< members of this set are frame indices to skip for each channel type

			std::vector<unsigned>			m_jointAnimOrderQuatLog;		//!< joint anim indices of log compressed quaternions reordered for cross-correlation
			std::vector<unsigned>			m_jointAnimOrderQuatLogPca;		//!< joint anim indices of log-pca compressed quaternions reordered for cross-correlation
		};

		/// A full set of local space error tolerance parameters for a single joint or float channel
		struct ChannelLocalSpaceError
		{
			float	m_bitTolerance;		// bit packing error tolerance
			float	m_keyTolerance;		// key removal error tolerance
		};

		/// A full set of local space error tolerance parameters for a joint
		struct JointLocalSpaceError
		{
			ChannelLocalSpaceError	m_scale;
			ChannelLocalSpaceError	m_rotation;
			ChannelLocalSpaceError	m_translation;
		};
		
		/// A full set of root space error tolerance parameters for a single joint or float channel
		// error tolerance = base error tolerance
		//	 			   + change in value per frame * error delta factor
		//				   + change in translation per frame * error speed factor (for joint scale or rotation tracks only)
		// NOTE: Error delta factors translate into an non-linearity tolerance:
		// For instance, a joint translation moving at a speed v = delta/(1 frame) will be
		// treated as linear if its direction changes by less than about m_fTransErrorDeltaFactor
		// radians per frame or if it accelerates less than  delta*m_fTransErrorDeltaFactor per frame.
		// Error speed factors set a cross channel non-linearity tolerance, allowing the
		// speed (or rate of change of translation per frame) to affect the error tolerance
		// used for rotations or scales.
		// Note that for the translation component, the error delta factor and error speed factor 
		// are identical and act like a single error delta factor equal to their sum.
		// While error delta factors are effectively unitless (i.e. changing the size of 
		// the model or the type of the parameter does not require a corresponding change
		// of scale in the error delta factor), error speed factors are not;  The error
		// speed factor for a rotation must effectively include a factor of 
		// (rotation error tolerance)/(translation error tolerance or model size), 
		// and for scale must similarly include a factor of 
		// (scale error tolerance)/(translation error tolerance or model size).
		struct ChannelRootSpaceError
		{
			float	m_tolerance;		// base tolerance
			float	m_deltaFactor;		// error delta & speed factors (used only if root 
			float	m_speedFactor;		// space errors and delta optimization are enabled)
		};

		/// A full set of root space error tolerance parameters for a joint
		struct JointRootSpaceError
		{
			ChannelRootSpaceError	m_scale;
			ChannelRootSpaceError	m_rotation;
			ChannelRootSpaceError	m_translation;
		};

		const U32 kChannelIndexDefault = 0xFFFFFFFF;	// special value for m_iJoint/m_iFloatChannel to indicate all animated channels not specified directly

		/// A structure used to pass target joints and per channel control parameters to 
		/// the local space error version of AnimClipCompression_GenerateLocalSpaceErrors.
		struct JointLocalSpaceErrorTarget
		{
			U32		m_iJoint;				// 0 .. m_numAnimatedJoints-1, or kChannelIndexDefault
			JointLocalSpaceError	m_error;
		};

		/// A structure used to pass target float channels and per channel control parameters to 
		/// the local space error version of AnimClipCompression_GenerateLocalSpaceErrors.
		struct FloatLocalSpaceErrorTarget
		{
			U32		m_iFloatChannel;		// 0 .. m_numFloatChannels-1, or kChannelIndexDefault
			ChannelLocalSpaceError	m_error;
		};

		/// A structure used to pass target joints and per channel control parameters to 
		/// the root space error version of AnimClipCompression_GenerateLocalSpaceErrors.
		struct JointRootSpaceErrorTarget
		{
			U32		m_iJoint;				// 0 .. m_numAnimatedJoints-1, or kChannelIndexDefault
			JointRootSpaceError	m_error;
		};

		/// A structure used to pass target float channels and per channel control parameters to 
		/// the root space error version of AnimClipCompression_GenerateLocalSpaceErrors.
		struct FloatRootSpaceErrorTarget
		{
			U32		m_iFloatChannel;		// 0 .. m_numFloatChannels-1, or kChannelIndexDefault
			ChannelRootSpaceError	m_error;
		};
		
		typedef std::vector<JointLocalSpaceErrorTarget>	LocalSpaceErrorTargetJoints;
		typedef std::vector<FloatLocalSpaceErrorTarget>	LocalSpaceErrorTargetFloats;
		typedef std::vector<JointRootSpaceErrorTarget>	RootSpaceErrorTargetJoints;
		typedef std::vector<FloatRootSpaceErrorTarget>	RootSpaceErrorTargetFloats;

		struct ClipLocalSpaceErrors 
		{
			typedef std::vector<float>				ConstTolerances;	// [num{Joint|Float}Anims]
			typedef std::vector<float>				BitTolerances;		// [num{Joint|Float}Anims]
			typedef std::vector<std::vector<float>> KeyTolerances;		// [num{Joint|Float}Anims][numFrames]

			std::array<ConstTolerances, kNumChannelTypes>	m_const;	// constant channel compression tolerances
			std::array<BitTolerances, kNumChannelTypes>		m_bit;		// bit packing compression tolerances
			std::array<KeyTolerances, kNumChannelTypes>		m_key;		// key frame removal compression tolerances

			void init(size_t numJointAnims, size_t numFloatAnims)
			{
				for (size_t i=0; i<kNumJointChannels; i++) {
					m_const[i].resize(numJointAnims);
					m_bit[i].resize(numJointAnims, 0.0f);
					m_key[i].resize(numJointAnims);
				}
				m_const[kChannelTypeScalar].resize(numFloatAnims);
				m_bit[kChannelTypeScalar].resize(numFloatAnims, 0.0f);
				m_key[kChannelTypeScalar].resize(numFloatAnims);
			}
		};

		// a set of animated joint or float indices to generate skip frames for each channel type
		struct GenerateSkipFrameIndices
		{
			std::set<size_t> m_indices[kNumChannelTypes];	

			void add(ChannelType chanType, size_t index) {
				m_indices[chanType].emplace(index);
			}
		
			bool has(ChannelType chanType, size_t index) const {
				return !!m_indices[chanType].count(index);
			}
		};

		/// Initialize an AnimationClipCompressionDesc
		void AnimClipCompression_Init( 
			ClipCompressionDesc& compression, 
			unsigned clipFlags, 
			unsigned compressionFlags );

		/// Generate local space errors:
		/// If root space errors are not enabled, localSpaceErrors is simply filled out with one constant error
		/// tolerance for each joint parameter and float channel.
		void AnimClipCompression_GenerateLocalSpaceErrors(
			ClipLocalSpaceErrors& localSpaceErrors,						//!< [output] filled out with local space error tolerances per joint animation and float animation
			AnimationBinding const& binding,							//!< mapping from animations to channels (joints/float channels) and back
			AnimationClipSourceData const& sourceData,					//!< source animation data
			LocalSpaceErrorTargetJoints const& targetJoints,			//!< local space error tolerance parameters for joints 
			LocalSpaceErrorTargetFloats const& targetFloatChannels);	//!< local space error tolerance parameters for float channels

		/// Generate local space errors:
		/// If root space errors are enabled, this generates a time dependent local space error tolerance for each 
		/// animated joint based on the pose of that joint's children and their root space error tolerance requirements.
		/// Float channels treat root space error tolerances as local space requirements currently, as we currently
		/// do not track set driven key relationships that might connect them into the hierarchy.
		///
		/// If delta optimization is enabled, this additionally adjusts root space error tolerances based on the
		/// rate of change of the parameter and errordelta factors set for each animated joint or float channel.
		/// For animations with an object root animation, both the root space (i.e. without object root movement)
		/// and world space (i.e. with object root movement) matrices are considered, and the minimum error tolerance
		/// between them is used.  This more advanced technique allows greater compression rates by raising the 
		/// compression rate of joint parameters in fast moving areas of the model, while maintaining fidelity where 
		/// slow moving joints require it (for instance, when a foot is pinned to the ground).
		void AnimClipCompression_GenerateLocalSpaceErrors(
			ClipLocalSpaceErrors& localSpaceErrors,						//!< [output] filled out with local space error tolerances per joint animation and float animation
			AnimationBinding const& binding,							//!< mapping from animations to channels (joints/float channels) and back
			AnimationHierarchyDesc const& hierarchyDesc,				//!< data describing hierarchical relationships between joints and float channels
			AnimationClipSourceData const& sourceData,					//!< source animation data
			RootSpaceErrorTargetJoints const& targetJoints,				//!< root space error tolerance parameters for joints 
			RootSpaceErrorTargetFloats const& targetFloatChannels,		//!< root space error tolerance parameters for float channels
			bool bDeltaOptimization);									//!< enable delta optimization of error tolerances

		/// Given animation source data, a compressionType (from kAcctVec3*), a set of animated joints indices to operate on, 
		/// and local space error tolerances, fill in compression.m_formatScale[].
		bool AnimClipCompression_GenerateBitFormats_Vector(
			ClipCompressionDesc& compression,							//!< [input/output] compression structure to fill out with calculated bit formats
			AnimationHierarchyDesc const& hierarchyDesc,				//!< data describing hierarchical relationships between joints and float channels
			AnimationBinding const& binding,							//!< mapping from animations to channels (joints/float channels) and back
			AnimationClipSourceData const& sourceData,					//!< source animation data
			ClipLocalSpaceErrors const& localSpaceErrors,				//!< as calculated by AnimClipCompression_GenerateLocalSpaceErrors
			AnimChannelCompressionType compressionType,					//!< type of compression to apply (one of kAcctVec3*)
			std::set<size_t> const& targetAnimatedJoints_Scale,			//!< set of animated joint indices to compress scales for
			std::set<size_t> const& targetAnimatedJoints_Translation,	//!< set of animated joint indices to compress translations for
			bool bSharedBitFormat);										//!< if true, one bit format must be chosen to be shared by all animated joints being compressed

		/// Given animation source data, a compressionType (from kAcctQuat*), a set of animated joints indices to operate on, 
		/// and local space error tolerances, fill in compression.m_formatQuat[].
		bool AnimClipCompression_GenerateBitFormats_Rotation(
			ClipCompressionDesc& compression,							//!< [input/output] compression structure to fill out with calculated bit formats
			AnimationHierarchyDesc const& hierarchyDesc,				//!< data describing hierarchical relationships between joints and float channels
			AnimationBinding const& binding,							//!< mapping from animations to channels (joints/float channels) and back
			AnimationClipSourceData const& sourceData,					//!< source animation data
			ClipLocalSpaceErrors const& localSpaceErrors,				//!< as calculated by AnimClipCompression_GenerateLocalSpaceErrors
			AnimChannelCompressionType compressionType,					//!< type of compression to apply (one of kAcctQuat*)
			std::set<size_t> const& targetAnimatedJoints_Rotation,		//!< set of animated joint indices to compress rotations for
			bool bSharedBitFormat);										//!< if true, one bit format must be chosen to be shared by all animated joints being compressed
		
		/// Given animation source data, a compressionType (from kAcctFloat*), a set of float channel indices to operate on, 
		/// and local space error tolerances, fill in compression.m_formatFloat[].
		bool AnimClipCompression_GenerateBitFormats_Float(
			ClipCompressionDesc& compression,							//!< [input/output] compression structure to fill out with calculated bit formats
			AnimationHierarchyDesc const& hierarchyDesc,				//!< data describing hierarchical relationships between joints and float channels
			AnimationBinding const& binding,							//!< mapping from animations to channels (joints/float channels) and back
			AnimationClipSourceData const& sourceData,					//!< source animation data
			ClipLocalSpaceErrors const& localSpaceErrors,				//!< as calculated by AnimClipCompression_GenerateLocalSpaceErrors
			AnimChannelCompressionType compressionType,					//!< type of compression to apply (one of kAcctVec3*)
			std::set<size_t> const& targetFloatChannels,				//!< set of float channel indices to compress
			bool bSharedBitFormat);										//!< if true, one bit format must be chosen to be shared by all float channels being compressed

		/// Given animation source data, an AnimationClipCompressionDesc initialized with all bit 
		/// compression formats defined and local space error tolerances for all animated joints, 
		/// calculate which keyframes may be skipped based on linearity tests against the provided
		/// local space error tolerances.
		/// For shared keys, all joint and float channel animations must share the same key frames.
		/// The skipped key frames list must be generated for all animations at once, and the
		/// results are stored in compression.m_skipFrames.
		bool AnimClipCompression_GenerateSkipFrames_Shared(
			ClipCompressionDesc& compression,							//!< [input/output] compression description to fill out
			AnimationHierarchyDesc const& hierarchyDesc,				//!< data describing hierarchical relationships between joints and float channels
			AnimationBinding const& binding,							//!< mapping from animations to channels (joints/float channels) and back
			AnimationClipSourceData const& sourceData,					//!< source animation data
			ClipLocalSpaceErrors const& localSpaceErrors);				//!< as calculated by AnimClipCompression_GenerateLocalSpaceErrors
		
		/// Given animation source data, an AnimationClipCompressionDesc initialized with all bit 
		/// compression formats defined and local space error tolerances for all animated joints, 
		/// calculate which keyframes may be skipped based on linearity tests against the provided
		/// local space error tolerances.
		/// For unshared keys, each joint and float channel animation may define an independent set 
		/// of keyframes to skip.  A sub-set of joints and float channels for which to generate 
		/// key frames to skip can be specified in targetAnimatedJoints_* and targetFloatChannels, and the
		///	results are stored in compression.m_skipFramesScale, m_skipFramesQuat, 
		///	m_skipFramesTrans, and m_skipFramesFloat.
		bool AnimClipCompression_GenerateSkipFrames_Unshared(
			ClipCompressionDesc& compression,							//!< [input/output] compression description to fill out
			AnimationHierarchyDesc const& hierarchyDesc,				//!< data describing hierarchical relationships between joints and float channels
			AnimationBinding const& binding,							//!< mapping from animations to channels (joints/float channels) and back
			AnimationClipSourceData const& sourceData,					//!< source animation data
			ClipLocalSpaceErrors const& localSpaceErrors,				//!< as calculated by AnimClipCompression_GenerateLocalSpaceErrors
			GenerateSkipFrameIndices const& genSkipFrames);				//!< set of animated joint & float channel indices to generate skip frames for 

		/// Fill in the constant compression formats for any constant joint params or float channels that have not been defined yet.
		bool AnimClipCompression_SetDefaultConstFormats(
			ClipCompressionDesc& compression,							//!< [input/output] compression description to fill out
			AnimationBinding const& binding,							//!< mapping from animations to channels (joints/float channels) and back
			AnimationClipSourceData const& sourceData,					//!< source animation data
			AnimChannelCompressionType constCompressionTypeScale,		//!< compression type to apply to constant joint scale channels
			AnimChannelCompressionType constCompressionTypeQuat,		//!< compression type to apply to constant joint rotation channels
			AnimChannelCompressionType constCompressionTypeTrans,		//!< compression type to apply to constant joint translation channels
			AnimChannelCompressionType constCompressionTypeFloat);		//!< compression type to apply to constant float channels

		/// For all constant joint params or float channels with defined formats, compute the best format for any 
		/// marked with compression type "auto" based on localSpaceErrors, and test and report non-compliance with
		/// localSpaceErrors for any with defined types.
		bool AnimClipCompression_FinalizeConstFormats(
			ClipCompressionDesc& compression,							//!< [input/output] compression description to fill out
			AnimationHierarchyDesc const& hierarchyDesc,				//!< data describing hierarchical relationships between joints and float channels
			AnimationBinding const& binding,							//!< mapping from animations to channels (joints/float channels) and back
			AnimationClipSourceData const& sourceData,					//!< source animation data
			ClipLocalSpaceErrors const& localSpaceErrors);				//!< as calculated by AnimClipCompression_GenerateLocalSpaceErrors

		/// Finish filling out compression, and optionally generate compression stats into pStats, if not NULL.
		bool AnimClipCompression_Finalize(
			ClipCompressionDesc& compression,							//!< [input/output] compression description to finalize
			AnimationHierarchyDesc const& hierarchyDesc,				//!< data describing hierarchical relationships between joints and float channels
			AnimationBinding const& binding,							//!< mapping from animations to channels (joints/float channels) and back
			AnimationClipSourceData const& sourceData,					//!< source animation data
			AnimationClipCompressionStats* stats = NULL);				//!< [output] statistics describing the resulting compression rates

		/// Given animation source data, set an AnimationClipCompressionDesc to all uncompressed data
		bool AnimClipCompression_SetUncompressed(
			ClipCompressionDesc& compression,							//!< [output] compression description to fill out
			AnimMetaData const& metaData,
			AnimationHierarchyDesc const& hierarchyDesc,				//!< data describing hierarchical relationships between joints and float channels
			AnimationBinding const& binding,							//!< mapping from animations to channels (joints/float channels) and back
			AnimationClipSourceData& sourceData,						//!< source animation data
			AnimationClipCompressionStats* stats = NULL);				//!< [output] statistics describing the resulting compression rates

		/// Given animation source data and local space error tolerances, set an AnimationClipCompressionDesc to all auto compressed data with uniform keyframes
		bool AnimClipCompression_SetAuto(
			ClipCompressionDesc& compression,							//!< [output] compression description to fill out
			AnimationHierarchyDesc const& hierarchyDesc,				//!< data describing hierarchical relationships between joints and float channels
			AnimationBinding const& binding,							//!< mapping from animations to channels (joints/float channels) and back
			AnimationClipSourceData const& sourceData,					//!< source animation data
			ClipLocalSpaceErrors const& localSpaceErrors);				//!< as calculated by AnimClipCompression_GenerateLocalSpaceErrors

		/// Dump the contents of an AnimationClipCompressionDesc as xml to INOTE
		void DumpAnimationClipCompression(
			ClipCompressionDesc const& compression,						//!< finalized compression description
			AnimationHierarchyDesc const& hierarchyDesc,				//!< data describing hierarchical relationships between joints and float channels
			AnimationBinding const& binding,							//!< mapping from animations to channels (joints/float channels) and back
			AnimationClipSourceData const& sourceData,					//!< source animation data
			std::string const& clipName);								//!< name of the associated animation clip

		}	//namespace AnimProcessing
	}	//namespace Tools
}	//namespace OrbisAnim

