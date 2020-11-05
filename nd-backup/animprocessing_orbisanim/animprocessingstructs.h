/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include <string>
#include <set>
#include <array>

#include "icelib/common/error.h"
#include "icelib/geom/cgvec.h"
#include "icelib/geom/cgquat.h"

#include "anim.h"

namespace ITSCENE {
	class SceneDb;
}

namespace OrbisAnim {
	namespace Tools {

		class IBitCompressedArray;
		class IBitCompressedVec3Array;
		class IBitCompressedQuatArray;
		class IBitCompressedFloatArray;
		
		//========================================================================================
		// Helper Functions
		#ifndef ALIGN_HELPER_MACROS
		#define ALIGN_HELPER_MACROS
		
		template<typename V, typename A>
		inline V Align(V value, A alignment) {
			return (V)((value + (alignment - 1)) & ~(alignment - 1));
		}

		inline bool IsAligned(size_t value, size_t alignment) {
			return !(value & (alignment - 1));
		}

		inline bool IsAligned(const void* pAddress, size_t alignment) {
			return !(reinterpret_cast<size_t>(pAddress) & (alignment - 1));
		}
		
		#endif // ALIGN_HELPER_MACROS

		namespace AnimProcessing {

		///=======================================================================================
		// Scene data format structures

		/// Describes a range of frames to extract from some data source
		struct AnimFrameSet
		{
			enum Loop {
				kLoopDefault =	-1,	// loop if the source animation scene was marked with -loopingAnim on export
				kLoopClamped =	 0,	// clamp frame index to range [0,numFrames-1] (do not loop)
				kLoopLooping =	 1,	// wrap frame index to range [0,numFrames) (where Pose(numFrames) == Pose(0))
			};
			enum ObjectRootAnim {
				kObjectRootAnimDefault =	-1,	// check if animation scene was marked with -movementAnim on export
				kObjectRootAnimNone =		 0,	// don't extract an object root animation
				kObjectRootAnimLinear =		 1,	// extract a single key linear object root animation
			};
			enum Frame { 
				kFrameEnd = -1, 
			};

			int m_startFrame;					//!< first frame to extract; default: 0
			int m_endFrame;						//!< last frame to extract + 1; default: kFrameEnd for numFrames
			float m_fFrameRate;					//!< frame rate of this data source; 0.0f for default
			Loop m_eLoop;						//!< behavior of clip for frame indices outside of 0...numFrames-1
			ObjectRootAnim m_eObjectRootAnim;	//!< should this clip extract an object root animation; if so, and it is looping, frame m_endFrame (or numFrames-1 if kFrameEnd) should be a near copy of frame 0

			AnimFrameSet(int startFrame = 0, 
						 int endFrame = kFrameEnd, 
						 Loop eLoop = kLoopDefault, 
						 float fFrameRate = 0.0f, 
						 ObjectRootAnim eObjectRootAnim = kObjectRootAnimDefault) 
				: m_startFrame(startFrame)
				, m_endFrame(endFrame)
				, m_fFrameRate(fFrameRate)
				, m_eLoop(eLoop)
				, m_eObjectRootAnim(eObjectRootAnim) 
			{}
			
			AnimFrameSet(AnimFrameSet const& s) 
				: m_startFrame(s.m_startFrame)
				, m_endFrame(s.m_endFrame)
				, m_fFrameRate(s.m_fFrameRate)
				, m_eLoop(s.m_eLoop)
				, m_eObjectRootAnim(s.m_eObjectRootAnim) 
			{}
		};

		/// Describes a scene data source
		struct AnimDataSource
		{
			std::string m_animScenePath;				//!< path to animation scene ndb; "bindpose" for bindpose
			ITSCENE::SceneDb const* m_pAnimScene;	//!< loaded scene ndb; NULL for bindpose
			AnimFrameSet m_frameSet;					//!< What data to extract from this source

			AnimDataSource() : m_pAnimScene(NULL) {}
			
			AnimDataSource(AnimDataSource const& s)	
				: m_animScenePath(s.m_animScenePath)
				, m_pAnimScene(s.m_pAnimScene)
				, m_frameSet(s.m_frameSet) 
			{}

			AnimDataSource(std::string const& animScenePath, 
						   ITSCENE::SceneDb const* pAnimScene = NULL, 
						   int startFrame = 0, 
						   int endFrame = AnimFrameSet::kFrameEnd, 
						   AnimFrameSet::Loop eLoop = AnimFrameSet::kLoopDefault, 
						   float fFrameRate = 0.0f, 
						   AnimFrameSet::ObjectRootAnim eObjectRootAnim = AnimFrameSet::kObjectRootAnimDefault) 
				: m_animScenePath(animScenePath)
				, m_pAnimScene(pAnimScene)
				, m_frameSet(startFrame, endFrame, eLoop, fFrameRate, eObjectRootAnim) 
			{}

			bool IsValid() const { return !m_animScenePath.empty() && (IsBindPose() == (m_pAnimScene == NULL)); }
			bool IsBindPose() const { return m_animScenePath == "bindpose"; }
		};

		/// Describes a scene data source and processing instructions to produce an output animation
		struct OutputAnimDesc
		{
			AnimDataSource m_inputData;			//!< description of a data source to load data from
			AnimDataSource m_subtractData;		//!< description of a data source to subtract from m_inputData in order to create an additive animation
			float m_fSubtractFrameOffset;		//!< frame[m_fSubtractFrameOffset] of m_subtractData will be subtracted from frame[0] of m_inputData; time will advance normally for each 
			float m_fSubtractFrameSpeed;		//!< If not 1.0, advance m_subtractData at a slower or faster rate relative to m_inputData
			float m_fSubtractFrameStep;			//!< If 0.0, calculate the step using the relative frames per second of each input anim & m_fSubtractFrameSpeed
			std::string m_metaDataPath;			//!< path to root metadata xml file from which to load compression instructions

			OutputAnimDesc() : m_fSubtractFrameOffset(0.0f), m_fSubtractFrameSpeed(1.0f), m_fSubtractFrameStep(0.0f) {}
			explicit OutputAnimDesc(AnimDataSource const& inputData) : m_inputData(inputData),  m_fSubtractFrameOffset(0.0f), m_fSubtractFrameSpeed(1.0f), m_fSubtractFrameStep(0.0f) {}
			OutputAnimDesc(	std::string const& animScenePath, 
							ITSCENE::SceneDb const* pAnimScene = NULL, 
							int startFrame = 0, 
							int endFrame = AnimFrameSet::kFrameEnd, 
							AnimFrameSet::Loop eLoop = AnimFrameSet::kLoopDefault, 
							float fFrameRate = 0.0f, 
							AnimFrameSet::ObjectRootAnim eObjectRootAnim = AnimFrameSet::kObjectRootAnimDefault ) 
				: m_inputData(animScenePath, pAnimScene, startFrame, endFrame, eLoop, fFrameRate, eObjectRootAnim)
				, m_fSubtractFrameOffset(0)
				, m_fSubtractFrameSpeed(1.0f)
				, m_fSubtractFrameStep(0.0f)
			{}
			
			void SetAdditive(AnimDataSource const& subtractData, float fSubtractFrameOffset = 0.0f, float fSubtractFrameSpeed = 1.0f, float fSubtractFrameStep = 0.0f)
			{
				m_subtractData = subtractData;
				m_fSubtractFrameOffset = fSubtractFrameOffset;
				m_fSubtractFrameSpeed = fSubtractFrameSpeed;
				m_fSubtractFrameStep = fSubtractFrameStep;
			}
		};

		///=======================================================================================
		// Intermediate data format structures

		/// Animation relevant joint flags
		enum JointFlags 
		{
			kJointSegmentScaleCompensate =	0x80000000,		//!< Joint has Maya 'segment scale compensate' set; its parent scale will affect its translation, but will not multiply in with its scale
			kJointIsAnimated =				0x40000000,		//!< Joint will be driven by baked animations at runtime
		};
		
		/// Animation relevant data about a joint in the hierarchy
		struct Joint 
		{
			/// enumeration of possible orders of euler rotations:
			enum EulerOrder { kRoZXY, kRoZYX, kRoYXZ, kRoYZX, kRoXYZ, kRoXZY };

			std::string			m_name;					//!< name of this joint
			unsigned			m_flags;				//!< union of JointFlags
			int					m_parent;				//!< parent joint index, or -1 if root
			int					m_child;				//!< child joint index, or -1 if no children
			int					m_sibling;				//!< sibling joint index, or -1 if no sibling
			ITGEOM::Vec3		m_jointScale;			//!< The default pose scale of this joint
			ITGEOM::Quat		m_jointQuat;			//!< The default pose quaternion rotation of this joint
			ITGEOM::Vec3		m_jointTranslation;		//!< The default pose translation of this joint
			EulerOrder			m_eulerOrder;			//!< The order of euler rotations applied to this joint.
			ITGEOM::Quat		m_jointOrient;			//!< The joints orientation.
			ITGEOM::Quat		m_rotAxis;				//!< The joints rotation axis.
		};
		
		/// Animation relevant data about a float channel in the hierarchy
		struct FloatChannel 
		{
			std::string			m_name;					//!< name of this joint
			float				m_defaultValue;			//!< The default pose value of this float channel
		};
		
		/// Description of the contents of an animation channel group
		/// Each group may contain 0 to kMaxJointsPerChannelGroup joints and 
		/// 0 to kMaxFloatChannelsPerChannelGroup float channels.
		/// In order for the output tables to form a contiguous array for
		/// each joint hierarchy segment, all animated joints must be
		/// output in order, followed by all float channels.  Only the
		/// first segment may contain float channels.
		/// Each channel group must therefore follow the following rules:
		/// group[0].firstJoint == group[0].firstFloatChannel = 0;
		/// group[i+1].firstJoint == group[i].firstJoint + group[i].numJoints;
		/// group[i+1].firstFloatChannel == group[i].firstFloatChannel + group[i].numFloatChannels;
		/// sum over i (group[i].numJoints) == hierarchy.numAnimatedJoints;
		/// sum over i (group[i].numFloatChannels) == hierarchy.numFloatChannels;
		/// For iFC = the index of the first group with float channels, and 
		///		iS0End = the index of the last group in the first segment plus 1:
		///		iFC < iS0End || hierarchy.numFloatChannels == 0;
		///		group[i < iFC || i >= iS0End].numJoints > 0;
		///		group[i < iFC || i >= iS0End].numFloatChannels == 0;
		///		group[i > iFC && i < iS0End].numJoints == 0;
		///		group[i >= iFC && i < iS0End].numFloatChannels > 0;
		/// Also, for float channels to form a contiguous array:
		///		(group[i < iS0End-1].numFloatChannels & 0x3) == 0;
		struct AnimationChannelGroup 
		{
			unsigned			m_firstAnimatedJoint;		//!< the first animated joint index in this channel group
			unsigned			m_numAnimatedJoints;		//!< the number of animated joints in this channel group
			unsigned			m_firstFloatChannel;		//!< the first float id in this channel group
			unsigned			m_numFloatChannels;			//!< the number of float channels in this channel group
		};
		
		/// In multi-segment joint hierarchies, joints can not always be animated
		/// in the same order that they are output.  Instead, all joints which must
		/// be animated in each segment are sorted by the segment in which they will
		/// be output.  This leaves a mapping between animated joint index and
		/// output joint id that can be encoded as a series of output joint id 
		/// ranges. 
		struct AnimatedJointRange 
		{
			unsigned			m_firstJoint;				//!< the index of the first output joint in this animated joint range
			unsigned			m_numJoints;				//!< the number of joints in this animated joint range
		};
		
		/// Description of an animation hierarchy's animation relevant properties
		struct AnimationHierarchyDesc 
		{
			unsigned			m_hierarchyId;				//!< unique identifier used to check if an animation may be evaluated on this hierarchy
			unsigned			m_numAnimatedJoints;		//!< total number of animation driven joints in this hierarchy
			unsigned			m_numFloatChannels;			//!< total number of animation driven float channels in this hierarchy
			int					m_objectRootJoint;			//!< index of the of the object root joint, or -1 if undefined
			std::vector<Joint>					m_joints;				//!< table of animation relevant data for all joints in runtime output order
			std::vector<FloatChannel>			m_floatChannels;		//!< table of animation relevant data for all floatChannels in runtime order
			std::vector<AnimationChannelGroup>	m_channelGroups;		//!< table describing how the joints and float channels of this hierarchy are divided into channel groups
			std::vector<AnimatedJointRange>		m_animatedJointRanges;	//!< mapping between animated joint index and output joint index (aka joint id the index into m_joints)
		};

		/// Description of how the joint and float animations in a given data source (an animation scene)
		/// map to and from joints and float channels in an animation hierarchy (a bind pose scene).
		struct AnimationBinding 
		{
			unsigned				m_hierarchyId;						//!< unique hierarchy identifier used to check if this animation may be evaluated on a hierarchy
			std::vector<int>		m_animatedJointIndexToJointAnimId;	//!< index into joint anim list or -1 if not animated, by animated joint index (m_numAnimatedJoints elements)
			std::vector<int>		m_jointIdToJointAnimId;				//!< index into joint anim list or -1 if not animated, by hierarchy joint id (m_numJoints elements)
			std::vector<int>		m_floatIdToFloatAnimId;				//!< index into float anim list or -1 if not animated, by hierarchy float channel id (m_numFloatChannels elements)
			std::vector<int>		m_jointAnimIdToAnimatedJointIndex;	//!< animated joint index or -1 if not valid, by joint animation id (numJointAnimations elements)
			std::vector<int>		m_jointAnimIdToJointId;				//!< index into joint list or -1 if not valid, by joint animation id (numJointAnimations elements)
			std::vector<int>		m_floatAnimIdToFloatId;				//!< index into float channel list or -1 if not valid, by float animation id (numFloatAnimations elements)
		};

		/// Flags controlling how this animation is processed; these must match runtime OrbisAnim::ClipFlags
		enum AnimationClipFlags 
		{
			// runtime flags
			kClipLooping			= 0x00000001,			//!< This clip should be exported with numTotalFrames+1 frames, with the additional final frame copied from frame 0
			kClipAdditive			= 0x00000002,			//!< This clip should be flagged as an additive animation
			kClipObjectRootAnim		= 0x00000004,			//!< Clip has AnimationObjectRootAnimData of some sort
			kClipConstsRemoved		= 0x00000008,			//!< Clip had constant channels equal to the default pose removed (requires default pose copy in EvaluateClip at runtime)
			kClipKeysUniform		= 0x00000000,			//!< Clip is compressed using uniform key times shared by all joint parameters of the animation (i.e. no keyframe compression).
			kClipKeysShared			= 0x00000010,			//!< Clip is compressed using one set of non-uniform key times shared by all joint parameters of the animation.
			kClipKeysUnshared		= 0x00000020,			//!< Clip is compressed using separate non-uniform key times for each scale, rotation, and translation joint parameter.
			kClipKeysUniform2		= 0x00000040,			//!< Uniform clip with new format
			kClipKeysHermite		= 0x00000080,			//!< Clip channels are encoded as cubic Hermite splines
			// tools-only flags
			kClipRootSpaceErrors	= 0x00000100,			//!< Clip should use global optimization techniques when compressing
			kClipDeltaOptimization	= 0x00000200,			//!< Clip should use global optimization techniques when compressing
			kClipGlobalOptimization = 0x00000400,			//!< Clip should use global optimization techniques when compressing

			kClipSourceDataMask		= kClipLooping | kClipAdditive | kClipConstsRemoved,
			kClipKeyCompressionMask	= kClipKeysShared | kClipKeysUnshared | kClipKeysUniform2 | kClipKeysHermite,
			kClipRuntimeFlagsMask	= kClipSourceDataMask | kClipObjectRootAnim | kClipKeyCompressionMask,
			kClipOptimizationsMask	= kClipRootSpaceErrors | kClipDeltaOptimization | kClipGlobalOptimization,
			kClipCompressionMask	= kClipKeyCompressionMask | kClipOptimizationsMask,
		};

		/// Type of object root animation data embedded in the runtime ClipData.
		/// Object root animations are intended to be evaluated separately from the pose
		/// data to be applied to the object root matrix.
		enum AnimationObjectRootAnimType
		{
			kObjectRootAnimNone			= 0,				//!< No object root animation is embedded
			kObjectRootAnimLinear		= 1,				//!< Linear object root animation is embedded
		//	kObjectRootAnimCubic		= 2,				//!< Cubic spline object root animation data is embedded
		//	kObjectRootAnimKeyframed	= 3,				//!< Key framed object root animation is embedded
		};
		
		/// Global properties of this animation clip
		struct AnimationClipProperties 
		{
			AnimationObjectRootAnimType	m_objectRootAnim;	//!< type of object root animation to generate for this clip
			float						m_framesPerSecond;	//!< frame rate of this animation
		};

		// forward declaration of compression error tolerances structure
		struct ClipLocalSpaceErrors;

		/// Uncompressed channels of an animation clip
		class AnimationClipSourceData 
		{
		public:
			unsigned							m_flags;		//!< union of AnimationClipFlags:: kClipLooping, kClipAdditive
			unsigned							m_numFrames;	//!< number of frames of source data; if kClipLooping, an additional frame copy of the first frame is implied
			class AnimationClipObjectRootAnim*	m_pRootAnim;	//!< If non-NULL, points to a AnimationClipObjectRootAnim that was extracted from this source data; owned by this structure
			
			/// Constructor and destructor
			AnimationClipSourceData() : m_flags(0), m_numFrames(0), m_pRootAnim(NULL) {}
			~AnimationClipSourceData();
			
			/// Copy constructor and operator= to handle deep copy of m_pRootAnim properly
			AnimationClipSourceData(AnimationClipSourceData const& copy);
			AnimationClipSourceData const& operator=(AnimationClipSourceData const& copy);

			void Init(size_t numJointAnims, size_t numFloatAnims);

			/// Determine which channels are constant within the tolerances given in the metadata
			void DetermineConstantChannels(	ClipLocalSpaceErrors const& tolerances);

			/// Remove constant channels that are equal to the default pose using the given tolerances
			void RemoveConstantDefaultPoseChannels(ClipLocalSpaceErrors const& tolerances, 
												   AnimationHierarchyDesc const& hierarchyDesc,
												   AnimationBinding const& binding);

			/// Predicate which tells us if the animated channel is considered constant / fixed
			bool IsConstant(ChannelType chanType, size_t animIndex) const {
				Anim* channel = m_channelAnims[chanType][animIndex];
				ITASSERT(channel != NULL);
				if (channel) {
					return channel->IsConstant();
				}
				return true;	// NOTE: a non-existent channel is treated as constant
			}

			/// Generic factory method for creating & adding a sampled anim channel given its sample values
			template<typename T>
			void AddSampledAnim(ChannelType chanType, std::vector<T> const& samples) {
				SampledAnim* channel = new SampledAnim(chanType, samples);
				ITASSERT(channel != NULL);
				m_channelAnims[chanType].push_back(channel);
			}

			/// Generic factory method for creating & adding a sampled anim channel given its anim index/id & sample values
			template<typename T>
			void AddSampledAnim(ChannelType chanType, size_t animIndex, std::vector<T> const& samples) {
				ITASSERT( animIndex < m_channelAnims[chanType].size() );
				SampledAnim* channel = new SampledAnim(chanType, samples);
				ITASSERT(channel != NULL);
				m_channelAnims[chanType][animIndex] = channel;
			}

			/// Generic factory method for creating & adding a constant anim channel
			template<typename T>
			void AddSampledAnim(ChannelType chanType, size_t animIndex, T const& constSample) {
				ITASSERT(animIndex < m_channelAnims[chanType].size());
				SampledAnim* channel = new SampledAnim(chanType, constSample);
				ITASSERT(channel != NULL);
				m_channelAnims[chanType][animIndex] = channel;
			}

			/// Generic factory method for creating & adding a spline anim channel given its anim index/id & splines
			void AddSplineAnim(ChannelType chanType, size_t animIndex, std::vector<HermiteSpline> const& splines) {
				ITASSERT( animIndex < m_channelAnims[chanType].size() );
				SplineAnim* channel = new SplineAnim(splines);
				ITASSERT(channel != NULL);
				m_channelAnims[chanType][animIndex] = channel;
			}
			
			/// Generic factory method for creating & adding a spline anim channel given its anim index/id & spline
			void AddSplineAnim(ChannelType chanType, size_t animIndex, HermiteSpline const& spline) {
				ITASSERT( animIndex < m_channelAnims[chanType].size() );
				SplineAnim* channel = new SplineAnim(spline);
				ITASSERT(channel != NULL);
				m_channelAnims[chanType][animIndex] = channel;
			}

			/// returns the number of anims for the given channel type
			size_t GetNumAnims(ChannelType chanType) const {
				return m_channelAnims[chanType].size();
			}

			void SetNumFrames();

			/// returns true if there exists an animation for the given channel type & anim index
			bool Exists(ChannelType chanType, size_t animIndex) const {
				ITASSERT(chanType < kNumChannelTypes);
				ITASSERT(animIndex < m_channelAnims[chanType].size());
				return m_channelAnims[chanType][animIndex] != NULL;
			}

			SampledAnim* GetSampledAnim(ChannelType chanType, size_t animIndex) const {
				return dynamic_cast<SampledAnim*>(m_channelAnims[chanType][animIndex]);
			}

			SplineAnim* GetSplineAnim(ChannelType chanType, size_t animIndex) const {
				return dynamic_cast<SplineAnim*>(m_channelAnims[chanType][animIndex]);
			}

		private:
			
			/// For each channel type, a vector of anims indexed by (joint or float) anim id
			std::array<std::vector<Anim*>, kNumChannelTypes>	m_channelAnims;		
		};

		/// Useful constants for animation source data
		extern const ITGEOM::Vec3 kIdentityScale;		// (1.0f, 1.0f, 1.0f);
		extern const ITGEOM::Quat kIdentityRotation;	// (0.0f, 0.0f, 0.0f, 1.0f);
		extern const ITGEOM::Vec3 kIdentityTranslation;	// (0.0f, 0.0f, 0.0f);

		///=======================================================================================
		// Runtime data format structures

		// runtime constants
		static const unsigned kInputBufferSize = 32*1024;		//!< size of a runtime input buffer (must match value in src/runtime/ice/anim/iceanimbatchpriv.h)
		static const unsigned kWorkBufferSize = 64*1024;		//!< size of a runtime work buffer (must match value in src/runtime/ice/anim/iceanimbatchpriv.h)
		static const unsigned kKeyCacheMaxSize = 136;			//!< size of clip decompression key cache at runtime (must match value in src/runtime/ice/anim/iceanimclippriv.h)
		static const unsigned kJointGroupSize = 0xffff;			//!< maximum number of joints per processing group
		static const unsigned kFloatChannelGroupSize = 0xffff;	//!< maximum number of float channels per processing group
		static const unsigned kChannelGroupStateSize = 64;		//!< size of state data allocated per processing group channel group at runtime (must match kBlendSafeSize+0x10 value in src/runtime/ice/anim/iceanimstructs.h)
		static const unsigned kSizeofJointParams = 0x30;		//!< size of a runtime JointParams which stores the state of one animated joint
		static const unsigned kSizeofFloatChannel = 0x04;		//!< size of a runtime float channel which stores the state of one animated float channel
		static const unsigned kSizeofScalar = 0x04;				//!< size of a runtime entry in scalar table
		static const unsigned kSizeofVector = 0x10;				//!< size of a runtime 128-bit vector

		/// An array of key frame index offsets in the range [0, 255]
		typedef std::vector<U8> FrameOffsetArray;

		/// A description of the key frame and value data in an unshared key frames block.
		/// Each channel must encode the first and last frame within the block, and
		/// may additionally encode any subset of the frames between.
		struct AnimationClipUnsharedKeyBlock 
		{
			unsigned	m_firstFrame;				//!< first frame encoded in this block
			unsigned	m_numFrames;				//!< number of frames encoded in this block

			// the following vectors store frame offsets from this block's m_firstFrame for each channel in a processing group
			// this tells us which channels in the group have key frames set in this block of frames
			// they are left empty for any constant channels in the processing group

			std::array<std::vector<FrameOffsetArray>, kNumChannelTypes>	m_frameOffsets;
		};
		
		/// A full description of the key frame and value data encoded within an animation
		/// for all blocks, for all channel groups. 
		struct AnimationClipUnsharedKeyFrameBlockDesc 
		{
			typedef std::vector<AnimationClipUnsharedKeyBlock> BlockArray;
			std::vector<BlockArray>	m_blocksInGroup;		//!< an array of blocks for each animation clip processing group
		};
		
		/// A temporary structure used to track indices into AnimationClipCompressedData 
		/// unshared keyframe data as we advance a block at a time within a channel group.
		struct AnimationClipUnsharedKeyOffsets 
		{
			std::vector<unsigned>	m_firstKey_scale;			//!< first key in the current block for each joint scale by group joint id [0 ... group.numJoints)
			std::vector<unsigned>	m_firstKey_rotation;		//!< first key in the current block for each joint rotation by group joint id [0 ... group.numJoints)
			std::vector<unsigned>	m_firstKey_translation;		//!< first key in the current block for each joint translation by group joint id [0 ... group.numJoints)
			std::vector<unsigned>	m_firstKey_float;			//!< first key in the current block for each float channel by group float id [0 ... group.numFloatChannels)
		};

		/// An array of key frame indices
		typedef std::vector<U16> FrameArray;

		/// An abstract interface for classes implementing methods for extracting keyframe value
		/// and frame index data from a data source, allowing our data compression functions to deal with
		/// the different key compression schemes transparently.
		/// Implementations are defined in animkeyextractors.h.
		class KeyExtractor
		{
		public:
			virtual ~KeyExtractor() {}
			virtual void GetScaleValues(unsigned iJointAnimIndex, ITGEOM::Vec3Array& values) const = 0;
			virtual void GetScaleValues(unsigned iJointAnimIndex, ITGEOM::Vec3Array& values, FrameArray& frames) const = 0;
			virtual void GetRotationValues(unsigned iJointAnimIndex, ITGEOM::QuatArray& values) const = 0;
			virtual void GetRotationValues(unsigned iJointAnimIndex, ITGEOM::QuatArray& values, FrameArray& frames) const = 0;
			virtual void GetTranslationValues(unsigned iJointAnimIndex, ITGEOM::Vec3Array& values) const = 0;
			virtual void GetTranslationValues(unsigned iJointAnimIndex, ITGEOM::Vec3Array& values, FrameArray& frames) const = 0;
			virtual void GetFloatValues(unsigned iFloatAnimIndex, std::vector<float>& values) const = 0;
			virtual void GetFloatValues(unsigned iFloatAnimIndex, std::vector<float>& values, FrameArray& frames) const = 0;
		};

		class AnimationClipObjectRootAnim;

		/// Compressed data for all for all channels of this animation clip.
		/// Constants are bit compressed separately and may use any number of constant bit compression schemes.
		/// Non-constant channels are bit compressed using any number of animated bit compression schemes.
		/// One of the three keyframe elimination schemes may also be applied to all non-constant channels.
		struct AnimationClipCompressedData
		{
			struct AnimatedData {
				IBitCompressedArray*		m_pCompressedArray;
				size_t						m_id;					//!< animated joint or float channel index
			};
			
			struct ConstantData {
				IBitCompressedArray*		m_pCompressedArray;
				std::vector<size_t>			m_ids;					//!< Array of animated joint or float channel ids; m_ids.size() == pCompressedArray->GetNumSamples()
			};
			
			unsigned						m_hierarchyId;			//!< unique hierarchy identifier used to check if this animation may be evaluated on a hierarchy
			unsigned						m_flags;				//!< union of AnimationClipFlags
			unsigned						m_numFrames;			//!< number of frames of output data
			AnimationClipObjectRootAnim*	m_pRootAnim;			//!< If non-NULL, points to a AnimationClipObjectRootAnim that was extracted from the source data for this compressed data; owned by this structure
						
			std::vector<AnimatedData>	m_anims[kNumChannelTypes];	//!< Animated joint or float channel compressed data; one entry per channel animation, ordered by joint or float id
			std::vector<ConstantData>	m_const[kNumChannelTypes];	//!< Constant joint or float channel compressed data; one entry per per compression type

			/// Default constructor
			AnimationClipCompressedData() : m_flags(0), m_numFrames(0), m_pRootAnim(NULL) {}
			
			/// Destructor, to simplify cleanup : deletes all IBitCompressedArrays stored in this structure and m_pRootAnim
			~AnimationClipCompressedData();

			/// resizes all anim & const vectors to zero
			void Clear();

		private:
			// copy disallowed due to required deep copy
			AnimationClipCompressedData(AnimationClipCompressedData const& copy);
			AnimationClipCompressedData const& operator=(AnimationClipCompressedData const& copy);
		};

		///=======================================================================================

		/// Sizes resulting from each type of compression
		struct AnimationClipCompressionStats
		{
			// number of animated channels of each type
			unsigned			m_numScaleJoints;			
			unsigned			m_numQuatJoints;
			unsigned			m_numTransJoints;
			unsigned			m_numFloatChannels;
			// number of constant channels of each type
			unsigned			m_numConstChannels[kNumChannelTypes];
			// number of removed constant channels of each type
			unsigned			m_numConstChannelsRemoved[kNumChannelTypes];
			size_t				m_sizeofScaleKey;
			size_t				m_sizeofQuatKey;
			size_t				m_sizeofTransKey;
			size_t				m_sizeofFloatKey;
			unsigned			m_numScaleCompressionTypes;
			unsigned			m_numQuatCompressionTypes;
			unsigned			m_numTransCompressionTypes;
			unsigned			m_numFloatCompressionTypes;
			size_t				m_uncompressedSize;
			size_t				m_sizeofConsts;					// num bytes of constant data
			size_t				m_constantCompressedSize;
			size_t				m_bitCompressedSize;
			size_t				m_keyCompressedSize;
			size_t				m_keyCompressedSizeNoOverheads;
			// number of animated channels by compression type
			std::vector<size_t>	m_numScalesByType;
			std::vector<size_t>	m_numQuatsByType;
			std::vector<size_t>	m_numTransByType;
			std::vector<size_t>	m_numFloatsByType;
			// number of constant channels by compression type
			std::vector<size_t>	m_numConstScalesByType;
			std::vector<size_t>	m_numConstQuatsByType;
			std::vector<size_t>	m_numConstTransByType;
			std::vector<size_t>	m_numConstFloatsByType;
			// number of quats with additional cross-correlation appied
			size_t				m_numCrossCorrelatedQuatLog;
			size_t				m_numCrossCorrelatedQuatLogPca;
			void init(size_t numVec3Types, size_t numQuatTypes, size_t numFloatTypes,
					  size_t numConstVec3Types, size_t numConstQuatTypes, size_t numConstFloatTypes) 
			{
				m_numScaleJoints = m_numQuatJoints = m_numTransJoints = m_numFloatChannels = 0;
				for (size_t i=0; i<kNumChannelTypes; i++) {
					m_numConstChannels[i] = 0;
					m_numConstChannelsRemoved[i] = 0;
				}
				m_sizeofScaleKey = m_sizeofScaleKey = m_sizeofTransKey = m_sizeofFloatKey = 0;
				m_numScaleCompressionTypes = m_numQuatCompressionTypes = 0;
				m_numTransCompressionTypes = m_numFloatCompressionTypes = 0;
				m_uncompressedSize = m_constantCompressedSize =	m_bitCompressedSize = m_keyCompressedSize = 0;
				m_sizeofConsts = 0;
				m_keyCompressedSizeNoOverheads = 0;
				m_numScalesByType.resize(numVec3Types);
				m_numQuatsByType.resize(numQuatTypes);
				m_numTransByType.resize(numVec3Types);
				m_numFloatsByType.resize(numFloatTypes);
				m_numConstScalesByType.resize(numConstVec3Types);
				m_numConstQuatsByType.resize(numConstQuatTypes);
				m_numConstTransByType.resize(numConstVec3Types);
				m_numConstFloatsByType.resize(numConstFloatTypes);
			}
		};

		/// A helper class for recording ClipData section sizes during stream write
		class ClipStats
		{
		public:
			ClipStats() : m_maxSectionNameLength(0)
			{
				memset(&m_compressionStats, 0, sizeof(AnimationClipCompressionStats));
			}

			void setClipName(std::string& name) {
				m_clipName = name;
			}

			void start(std::string name, size_t loc);
			void end(size_t loc);

			void report(std::string name);
			void reportCsv(FILE* fp);
			void reportCsvHdr(FILE* fp);

			static void reportCsvTransposed(FILE* fp, std::vector<ClipStats>& clipStats);

			AnimationClipCompressionStats& getCompStats() {
				return m_compressionStats;
			}

			void sumSections();
			void resizeSections(std::set<std::string> sectionNames);

		private:
			struct Section 
			{
				Section() : m_name(""), m_location(0), m_size(0) {}
				Section(std::string name)
					: m_name(name)
					, m_location(0)
					, m_size(0)
				{}
				
				std::string	m_name;
				size_t		m_location;
				size_t		m_size;
			};
			std::string						m_clipName;
			std::vector<Section>			m_sections;
			std::vector<Section>			m_stack;
			size_t							m_maxSectionNameLength;
			AnimationClipCompressionStats	m_compressionStats;
		};

		}	//namespace AnimProcessing
	}	//namespace Tools
}	//namespace OrbisAnim

