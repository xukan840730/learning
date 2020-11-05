/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/resource/resource-table.h"

#include <orbisanim/joints.h>
#include <orbisanim/structs.h>

/// --------------------------------------------------------------------------------------------------------------- ///
namespace ndanim
{
	struct AnimatedJointPose;
	struct JointHierarchy;
};

namespace DC
{
	struct AnimOverlaySetEntry;
};

class ArtItemSkeleton;
class AnimSimpleLayer;
class AnimControl;
class AnimStateSnapshot;
class AnimSnapshotNode;
class AnimSnapshotNodeBlend;
struct FgAnimData;
struct AnimCameraCutInfo;

#define VALIDATE_LAMBDA(pLambda, desIdStr, nameId) 
//	ANIM_ASSERTF(pLambda && (pLambda->m_id != INVALID_STRING_ID_64) &&(pLambda->m_id == StringToStringId64(desIdStr)), ("%s in DC state '%s'", desIdStr, DevKitOnly_StringIdToStringOrHex(nameId)))

/// --------------------------------------------------------------------------------------------------------------- ///
enum MotionType
{
	kMotionTypeWalk,
	kMotionTypeRun,
	kMotionTypeSprint,
	kMotionTypeMax
};

enum AnimLayerType
{
	kAnimLayerTypeInvalid		= 0,
	kAnimLayerTypeSimple		= 1 << 0,
	kAnimLayerTypeState			= 1 << 1,
	kAnimLayerTypePose			= 1 << 2,
	kAnimLayerTypeSnapshot		= 1 << 3,
	kAnimLayerTypeCmdGenerator	= 1 << 4,
	kAnimLayerTypeCopyRemap		= 1 << 5,
};

enum FadeMethodToUse
{
	kUseMasterFade,
	kUseAnimFade,
	kUseMotionFade,
};

/// --------------------------------------------------------------------------------------------------------------- ///
namespace ndanim
{
	// These structs/enums are mirrored implementations of OrbisAnim::XXXXX for portability.

	enum BlendMode
	{
		kBlendLerp			= 0, //!< q = Normalize( Lerp(qa, qb, f) );  s = Lerp(sa, sb, f);  t = Lerp(ta, tb, f);  valid = valid_a | valid_b;  Copy a or b where b or a is undefined
		kBlendSlerp			= 1, //!< q = Slerp(qa, qb, f);  s = Lerp(sa, sb, f);  t = Lerp(ta, tb, f);  valid = valid_a | valid_b;  Copy a or b where b or a is undefined
		kBlendAdditive		= 2, //!< q = qa * Slerp(I, qb, f);  s = sa * Lerp(I, sb, f);  t = Lerp(ta, qa.Rotate(sa*tb), f);  valid = valid_a;  Copy a where b is undefined
		kBlendMultiply		= 3, //!< q = qa * qb;  s = Lerp(sa, sb, f);  t = Lerp(ta, tb, f);  valid = valid_a | valid_b;  Copy a or b where b or a is undefined
		kBlendAddToAdditive = 6, //!< q = qa * Slerp(I, qb, f);  s = sa * Lerp(I, sb, f);  t = Lerp(ta, qa.Rotate(sa*tb), f);  valid = valid_a | valid_b;  Use I(dentity) where a is undefined
		kMake32bit			= 0xffffffff
	};

	/*!
		* This general structure is used to represent the local scale, orientation, and
		* position of a joint, which is used by clips, poses, blending operations, and SDKs.
		* It has scale, quaternion and translate, thus it is often called an "SQT".
		*/
	struct ALIGNED(16) JointParams
	{
		Vector m_scale;   //!< Scale of this joint.
		Quat   m_quat;    //!< Rotation (in unit quaternion) form of this joint.
		Point  m_trans;   //!< Translation of this joint.
	};


	/*!
		* The ValidBits structure is a quadword of bits. We use it to define whether a joint
		* in a joint group (128 joints), or a float channel in a float channel group is valid or not.
		*/
	struct ValidBits
	{
	public:
		explicit ValidBits() {}
		explicit ValidBits(U64 bits0_63, U64 bits64_127 = 0) { m_bits = OrbisAnim::ValidBits(bits0_63, bits64_127); }
		explicit ValidBits(const OrbisAnim::ValidBits& bits) { m_bits = bits; }

		// Trampoline functions...
		void SetEmpty() { m_bits.SetEmpty(); }
		void Clear() { m_bits.Clear(); }
		void SetAllBits() { m_bits.SetAllBits(); }
		U32 IsEmpty() const { return m_bits.IsEmpty(); }

		U32 IsBitSet(U32F iBit) const { return m_bits.IsBitSet(iBit); }
		void SetBit(U32F iBit) { m_bits.SetBit(iBit); }

		ValidBits operator~() const { return ValidBits(m_bits.operator~()); }
		ValidBits operator&(const ValidBits& v) const { return ValidBits(m_bits.operator&(v.m_bits)); }
		ValidBits operator|(const ValidBits& v) const { return ValidBits(m_bits.operator|(v.m_bits)); }
		ValidBits operator^(const ValidBits& v) const { return ValidBits(m_bits.operator^(v.m_bits)); }
		ValidBits const& operator&(const ValidBits& v) { m_bits = m_bits.operator&(v.m_bits); return *this; }
		ValidBits const& operator|(const ValidBits& v) { m_bits = m_bits.operator|(v.m_bits); return *this; }
		ValidBits const& operator^(const ValidBits& v) { m_bits = m_bits.operator^(v.m_bits); return *this; }

		bool operator==(const ValidBits& v) const { return m_bits == v.m_bits; }
		bool operator!=(const ValidBits& v) const { return m_bits != v.m_bits; }

		U64 GetU64(int i) const { return m_bits.GetU64(i); }

	private:
		OrbisAnim::ValidBits m_bits;
	};


	// A 'pose' refers to ONLY the animated joints in a packed array. Num animated joints per segment need to 
	// be summed up to properly index into this data.
	struct AnimatedJointPose
	{
		AnimatedJointPose()
		{
			m_pJointParams = nullptr;
			m_pFloatChannels = nullptr;
			m_pValidBitsTable = nullptr;
		}

		JointParams* m_pJointParams;			// [pAnimHierarchy->m_numAnimatedJoints]	(excludes procedural joints)
		float* m_pFloatChannels;				// [pAnimHierarchy->m_numFloatChannels]		(excludes procedural output channels)
		ValidBits* m_pValidBitsTable;			// [pAnimHierarchy->m_numChannelGroups]
	};


	enum ClipFlags
	{
		kClipLooping		= 0x0001, //!< Clip is looping (last frame duplicates first)
		kClipAdditive		= 0x0002, //!< Clip is additive (was generated by subtracting a base pose or animation)
		kClipLinearRootAnim = 0x0004, //!< Clip has AnimClipLinearRootAnimData
		kClipKeysShared		= 0x0010, //!< Clip is compressed using one set of non-uniform key times shared by all joint parameters of the animation.
		kClipKeysUnshared	= 0x0020, //!< Clip is compressed using separate non-uniform key times for each scale, rotation, and translation joint parameter.
	};

	/*!
		* This is the actual clip data.  It contains all global data for this clip that might be
		* interesting to the general user.
		* With iceanimclippriv.h, this header contains enough data to navigate the internal structure
		* of a clip.
		*/
	struct ClipData : public OrbisAnim::AnimClip
	{
	};


	/*!
		* As of JointHierarchy version 1.05 or ice-7.1.1, we now allow the JointHierarchy to define
		* the way its joints and float channels are broken into processing groups during the evaluation
		* of AnimationNodes.
		*
		* The processing groups must satisfy a number of constraints:
		* Each processing group contains a contiguous array of joints and float channels.
		* No processing group may contain more than kJointGroupSize joints or kFloatChannelGroupSize
		* float channels.
		* Processing groups must be sequential and non-overlapping, with all joints processed before
		* any float channels.  group[i+1].m_firstJoint == group[i].m_firstJoint + group[i].m_numJoints.
		* There may therefore be at most one group which contains both joints and float channels,
		* which would have to contain the last joint and the first float channel.
		*/
	struct ProcessingGroup : public OrbisAnim::ProcessingGroup
	{
	};


	/*!
		* The JointHierarchy is a major structure for all animation-related data pertaining
		* to a specific model. It contains parenting data, but also set-driven key information,
		* as well as constraint data.
		* Its internal structure may be parsed with icejointhierarchypriv.h.
		*/
	struct JointHierarchy : public OrbisAnim::AnimHierarchy
	{
	};

	struct JointHierarchyUserDataHeader : public OrbisAnim::AnimHierarchyUserDataHeader
	{
	};

	/*!
		* Input control drivers (for TLOU's custom procedural rig nodes)
		*/
	struct InputControlDriver
	{
		enum _Axis { kAxisX, kAxisY, kAxisZ };
		typedef U32 Axis;

		static const unsigned kNdiMeasureConeAngle = 0x0010A581;
		static const unsigned kNdiMeasureTwist     = 0x0010A582;

		SMath::Quat m_refPosePs;		//!< reference pose of the node, relative to the parent of the input joint (m_inputJointIndex)
		SMath::Quat m_twistRefPosePs;	//!< ndiMeasureConeAngle ONLY: reference pose for twist compensation, relative to the parent of the input joint (m_inputJointIndex)
		U32 m_mayaNodeTypeId;			//!< MTypeId of the custom procedural rig node in Maya (or 0 to mean "this input is not driven")
		I32 m_inputJointIndex;			//!< index of the input joint that drives this node
		Axis m_primaryAxis;				//!< primary axis -- aim axis of the ndiMeasureConeAngle node; twist axis of the ndiMeasureTwist node
		Axis m_twistAxis;				//!< ndiMeasureConeAngle ONLY: axis about which to perform twist compensation
		I32 m_inputTwistControl;		//!< ndiMeasureConeAngle ONLY: index of another input control, which must be driven by an ndiMeasureTwist node, that provides the twist angle for compensation purposes (or (U16)-1 if twist compensation is not enabled)
		F32 m_minAngleDeg;				//!< ndiMeasureConeAngle ONLY: min cone angle
		F32 m_maxAngleDeg;				//!< ndiMeasureConeAngle ONLY: MAX cone angle
		U32 _pad[1];
	};

	/*!
		* A pose node sends a pose to the animation task -
		*  A pose contains the local space position of all joints in JointParams (SQT) format,
		*  the values of all float channels, and bitfields defining which are valid.
		*  Note that m_numChannelGroups == ((m_numTotalJoints + kJointGroupSize-1)/kJointGroupSize) + ((m_numFloatChannels + kFloatChannelGroupSize-1)/kFloatChannelGroupSize);
		*/
	struct PoseNode
	{
		const ndanim::AnimatedJointPose m_jointPose;	//!< Pointer to space to return JointHierarchy::m_numChannelGroups ValidBits structs
												//!< Pointer to space to return JointHierarchy::m_numAnimatedJoints JointParams (SQT); may be nullptr if JointHierarchy::m_numTotalJoints == 0
												//!< Pointer to space to return JointHierarchy::m_numFloatChannels F32 values; may be nullptr if JointHierarchy::m_numFloatChannels == 0
		U32					m_hierarchyId;
	};


	/*!
		* A snapshot node indicates that we want to take a snapshot of the tree at the node's
		* position, saving a pose into 'm_jointParams' and 'm_floatChannels' and the specification
		* of which joints and float channels have valid data into 'm_validBits'.
		*/
	struct SnapshotNode
	{
		ndanim::AnimatedJointPose m_jointPose;	//!< Pointer to space to return JointHierarchy::m_numChannelGroups ValidBits structs
										//!< Pointer to space to return JointHierarchy::m_numAnimatedJoints JointParams (SQT); may be nullptr if JointHierarchy::m_numTotalJoints == 0
										//!< Pointer to space to return JointHierarchy::m_numFloatChannels F32 values; may be nullptr if JointHierarchy::m_numFloatChannels == 0
		U32				m_hierarchyId;
	};

	
	/*! DebugJointParentInfo.m_flags values */
	enum DebugJointParentInfoFlags
	{
		kDebugJointSegmentScaleCompensate =		0x80000000,	//!< Joint has segment scale compensate enabled
		kDebugJointProcedural =					0x40000000,	//!< Joint is procedural (constraint or SDK driven)
		kDebugJointSecondarySetMask =			0x000000FF,	//!< Joint belongs to a secondary parenting set (1..m_numSecondarySets) or primary set if 0
	};


	/*!
		* This is debug data describing the joint hierarchy's tree structure,
		* stored as an array of m_numTotalJoints elements at
		* ((U8*)hierarchy + hierarchy->m_parentInfoOffset)
		*
		* It is unused by the animation and rendering code (superseded by
		* the data at hierarchy->m_parentingDataOffset), but has an easier
		* to parse format for debugging purposes.
		*/
	struct DebugJointParentInfo
	{
		U32			m_flags;	//!< union of DebugJointParentInfoFlags
		I32			m_parent;	//!< Joint index of my parent or -1 if no parent (a root joint).
		I32			m_child;	//!< Joint index of my first child or -1 if no children.
		I32			m_sibling;	//!< Joint index of my next sibling or -1 if no sibling.
	};


	//////////////////////////////////////////////////////////////////////////
	// Helper functions
	//////////////////////////////////////////////////////////////////////////

	void InitSnapshotNode(SnapshotNode& node, const ndanim::JointHierarchy* pHier);

	void CopySnapshotNodeData(const SnapshotNode& source, SnapshotNode* pDest, const JointHierarchy* pHier);

	/// Access the DebugJointParentInfo[m_numTotalJoints] table
	const DebugJointParentInfo* GetDebugJointParentInfoTable(const ndanim::JointHierarchy* pHier);

	/// Access the JointParams[m_numAnimatedJoints] default joint pose table
	const JointParams* GetDefaultJointPoseTable(const ndanim::JointHierarchy* pHier);

	/// Access the float[m_numInputControls] default float channel table
 	const F32* GetDefaultFloatChannelPoseTable(const ndanim::JointHierarchy* pHier);

	/// Access the inverse bind pose Mat34[m_numTotalJoints] table
	const Mat34* GetInverseBindPoseTable(const ndanim::JointHierarchy* pHier);

	/// Index of the first group of the segment
	U32 GetFirstProcessingGroupInSegment(const ndanim::JointHierarchy* pHier, U32 segmentIndex);

	/// Number of processing groups in a particular segment
	U32 GetNumProcessingGroupsInSegment(const ndanim::JointHierarchy* pHier, U32 segmentIndex);

	/// Access the size of the ProcessingGroup table
	U32 GetNumProcessingGroups(const ndanim::JointHierarchy* pHier);

	/// Access the ProcessingGroup[m_numProcessingGroups] table
	const ProcessingGroup* GetProcessingGroupTable(const ndanim::JointHierarchy* pHier);

	/// Access the number of animated channel groups
	U32 GetNumChannelGroups(const ndanim::JointHierarchy* pHier);

	/// Access the number of channel groups in processing group iGroup
	U32 GetNumChannelGroupsInProcessingGroup(const ndanim::JointHierarchy* pHier, U32 processingGroupIndex);

	// Grab the index of the first joint in this segment
	U32 GetFirstJointInSegment(const ndanim::JointHierarchy* pHier, U32 segmentIndex);

	// Grab the index of the first animated joint in this segment (this COLLAPSES away all procedural joints!)
	U32 GetFirstAnimatedJointInSegment(const ndanim::JointHierarchy* pHier, U32 segmentIndex);

	/// Access the number of total joints in the segment
	U32 GetNumJointsInSegment(const ndanim::JointHierarchy* pHier, U32 segmentIndex);

	/// Access the number of output controls in the segment
	U32 GetNumOutputControlsInSegment(const ndanim::JointHierarchy* pHier, U32 segmentIndex);

	/// Access the number of animated joints in the segment
	U32 GetNumAnimatedJointsInSegment(const ndanim::JointHierarchy* pHier, U32 segmentIndex);

	// Convert the output joint index (animated + procedural joints interleaved) to a packed animated joint index
	U32 OutputJointIndexToAnimatedJointIndex(const ndanim::JointHierarchy* pHier, U32 outputJointIndex);

	/// Return the segment that owns a specific output joint index
	I32F GetSegmentForOutputJoint(const ndanim::JointHierarchy* pHier, U32 iOutputJoint);

	/// Access the number of joints in processing group iGroup
	U32 GetNumJointsInGroup(const ndanim::JointHierarchy* pHier, U32 processingGroupIndex);

// 	U32 GetNumSdkFloatInputCommands(const ndanim::JointHierarchy* pHier);

	/// Calculate the total size in bytes of this ClipData
	U32 GetTotalSize(const ndanim::ClipData* pClip);
	
	/// Access the number of processing groups this clip is divided into (which generally matches JointHierarchy::m_numProcessingGroups)
	U32 GetNumProcessingGroups(const ndanim::ClipData* pClip);

	/// Access the ValidBits array for processing group iGroup, which has JointHierarchy_GetNumChannelGroupsInGroup(pJointHierarchy, iGroup) elements
	const ValidBits* GetValidBitsArray(const ndanim::ClipData* pClip, U32 processingGroupIndex);

	/// Check whether a certain joint is animated or procedurally generated
	bool IsJointAnimated(const ndanim::JointHierarchy* pHier, U32 jointIndex);

	/// Gets the index of the parent joints (-1) if it has none
	I32F GetParentJoint(const ndanim::JointHierarchy* pHierarchy, I32 jointIndex);

	struct SharedTimeIndex
	{
		StringId64		m_ownerId;
		I64				m_lastUsedFrameNo;
		F32				m_phase;
		NdAtomicLock*	m_pLock;

		static void GetOrSet(SharedTimeIndex* pSharedTime, F32* pCurPhase, const char* file, U32F line, const char* func);
	};
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(16) AnimControlStepParams
{
	AnimControl* m_pAnimControl;
	FgAnimData* m_pAnimData;
	float m_deltaTime;
	U32 m_padding[3];
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(16) AnimStepSpawnParams
{
	// Data for the anim control jobs
	U32 m_animControlStepParamsEa;
	U32 m_numAnimStepUpdateParams;

	// The jobslist/workload to add the spawned jobs into
	U32 m_jobListEa;
	U32 m_workloadId;

	// Location to put the job command buffers for the spawned jobs
	U32 m_cmdBufferEa;
	U32 m_cmdBufferSize;

	// The code location/size of the spawned jobs
	U32 m_animStepCodeEa;
	U32 m_animStepCodeSize;
	U32 m_animStepCodeSizeInPages;

	// The anim options used by the spawned jobs
	U32 m_animOptionsEa;
	U32 m_animOptionsSize;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimStateInstance;
class AnimStateLayer;
class ArtItemAnim;
class AnimOverlays;

/// --------------------------------------------------------------------------------------------------------------- ///
typedef bool (*PFnVisitAnimStateInstance)(AnimStateInstance* pInstance, AnimStateLayer* pStateLayer, uintptr_t userData);
typedef bool (*PFnVisitAnimStateInstanceConst)(const AnimStateInstance* pInstance,
											   const AnimStateLayer* pStateLayer,
											   uintptr_t userData);

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimOverlaySnapshotHash
{
	friend class AnimOverlaySnapshot;
public:
	AnimOverlaySnapshotHash() : m_val(0) {}

	bool operator == (const AnimOverlaySnapshotHash& h) const { return h.m_val == m_val; }
	bool operator != (const AnimOverlaySnapshotHash& h) const { return h.m_val != m_val; }

	U64 GetValue() const { return m_val; }

private:
	AnimOverlaySnapshotHash(U64 hashVal) : m_val(hashVal) { }
	U64 m_val;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class CachedAnimOverlayLookup
{
	friend class AnimOverlaySnapshot;

public:
	CachedAnimOverlayLookup()
	{
		Reset();
	}

	void Reset()
	{
		m_sourceId = INVALID_STRING_ID_64;
		m_resolvedId = INVALID_STRING_ID_64;
		m_hash = AnimOverlaySnapshotHash();
	}

	const StringId64 GetSourceId() const { return m_sourceId; }
	const StringId64 GetResolvedId() const { return m_resolvedId; }
	AnimOverlaySnapshotHash GetHash() const { return m_hash; }

	void SetSourceId(const StringId64 sourceId)
	{
		if (m_sourceId != sourceId)
		{
			Reset();
			m_sourceId = sourceId;
			m_resolvedId = sourceId;
		}
	}

private:
	CachedAnimOverlayLookup(const StringId64 sourceId, const StringId64 resolvedId, const AnimOverlaySnapshotHash hash)
	{
		m_sourceId = sourceId;
		m_resolvedId = resolvedId;
		m_hash = hash;
	}

	StringId64 m_sourceId;
	StringId64 m_resolvedId;
	AnimOverlaySnapshotHash m_hash;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class CachedAnimLookupRaw 	// no overlays, no anim sets -- just raw animation lookups from the anim table
{
public:

	explicit CachedAnimLookupRaw()
	{
		Reset(INVALID_STRING_ID_64);
	}
	explicit CachedAnimLookupRaw(const StringId64 sourceId)
	{
		Reset(sourceId);
	}

	void Reset(const StringId64 sourceId);

	const StringId64 GetSourceId() const { return m_sourceId; }
	void SetSourceId(StringId64 sourceId)
	{
		if (sourceId != m_sourceId)
		{		
			Reset(sourceId);
		}
	}

	void SetFromAnim(ArtItemAnimHandle hAnim);

	const ArtItemAnim* GetAnim() const { return m_artItemHandle.ToArtItem(); }

	bool CachedValueValid() const;

	bool Equals(const CachedAnimLookupRaw& rhs) const
	{
		return (m_sourceId == rhs.m_sourceId)
			&& (m_artItemHandle == rhs.m_artItemHandle)
			&& (m_atActionCounter == rhs.m_atActionCounter)
			&& (m_stActionCounter == rhs.m_stActionCounter);
	}

private:
	friend class AnimControl;
	void SetFinalResult(ArtItemAnimHandle artItemAnim);

	StringId64				m_sourceId;
	ArtItemAnimHandle		m_artItemHandle;
	U32						m_atActionCounter;
	U32						m_stActionCounter;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class CachedAnimLookup
{
public:

	CachedAnimLookup()
	{
		Reset();
	}

	void Reset();

	const StringId64 GetSourceId() const { return m_overlayLookup.GetSourceId(); }
	const StringId64 GetFinalResolvedId() const { return m_finalResolvedId; }
	const AnimOverlaySnapshotHash GetHash() const { return m_overlayLookup.GetHash(); }
	ArtItemAnimHandle GetAnim() const { return m_artItemAnim; }

	const CachedAnimOverlayLookup& GetOverlayLookup() const { return m_overlayLookup; }

	void SetSourceId(StringId64 sourceId)
	{
		if (sourceId != GetSourceId())
		{		
			Reset();
			m_overlayLookup.SetSourceId(sourceId);
			m_finalResolvedId = sourceId;
		}
	}

	bool CachedValueValid(const AnimOverlays* pOverlays) const;

	bool Equals(const CachedAnimLookup& rhs) const
	{
		return (m_finalResolvedId == rhs.m_finalResolvedId)
			&& (m_overlayLookup.GetHash() == rhs.m_overlayLookup.GetHash())
			&& (m_overlayLookup.GetSourceId() == rhs.m_overlayLookup.GetSourceId())
			&& (m_overlayLookup.GetResolvedId() == rhs.m_overlayLookup.GetResolvedId())
			&& (m_artItemAnim == rhs.m_artItemAnim)
			&& (m_atActionCounter == rhs.m_atActionCounter)
			&& (m_stActionCounter == rhs.m_stActionCounter);
	}

private:
	friend class AnimControl;

	void SetOverlayResult(const CachedAnimOverlayLookup& overlayRes)
	{
		if (m_overlayLookup.GetHash() != overlayRes.GetHash())
		{
			if (m_overlayLookup.GetResolvedId() != overlayRes.GetResolvedId())
			{
				m_finalResolvedId = overlayRes.GetResolvedId();
				m_artItemAnim = ArtItemAnimHandle();
			}

			m_overlayLookup = overlayRes;
		}
	}

	void SetFinalResult(const StringId64 finalId, ArtItemAnimHandle artItemAnim);

	CachedAnimOverlayLookup m_overlayLookup;
	StringId64				m_finalResolvedId;
	ArtItemAnimHandle		m_artItemAnim;
	U32						m_atActionCounter;
	U32						m_stActionCounter;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimLookupSentinal
{
public:
	AnimLookupSentinal();
	bool IsValid() const;
	void Refresh();

private:
	U32	m_atActionCounter;
	U32	m_stActionCounter;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimOverlayIterator
{
public:
	AnimOverlayIterator()
		: m_sourceId(INVALID_STRING_ID_64)
		, m_layerIndex(-1)
		, m_entryIndex(-1)
		, m_pEntry(nullptr)
		, m_matchState(false)
		, m_isUnidirectional(false)
	{
	}

	AnimOverlayIterator(const StringId64 sourceId, bool matchState = false)
		: m_sourceId(sourceId)
		, m_layerIndex(-1)
		, m_entryIndex(-1)
		, m_pEntry(nullptr)
		, m_matchState(matchState)
		, m_isUnidirectional(false)
	{
	}

	bool IsValid() const { return m_pEntry != nullptr; }
	const DC::AnimOverlaySetEntry* GetEntry() const { return m_pEntry; }

	const StringId64 GetSourceId() const { return m_sourceId; }
	void UpdateSourceId(const StringId64 newSourceId) { m_sourceId = newSourceId; }
	I32F GetLayerIndex() const { return m_layerIndex; }
	I32F GetEntryIndex() const { return m_entryIndex; }

	bool IsUnidirectional() const { return m_isUnidirectional; }
	void SetUnidirectional(bool f) { m_isUnidirectional = f; }

private:
	void AdvanceTo(const I32F newLayerIndex, const I32F newEntryIndex, const DC::AnimOverlaySetEntry* pNewEntry)
	{
		m_layerIndex = newLayerIndex;
		m_entryIndex = newEntryIndex;
		m_pEntry = pNewEntry;
	}

	StringId64 m_sourceId;
	I32 m_layerIndex;
	I32 m_entryIndex;
	const DC::AnimOverlaySetEntry* m_pEntry;
	bool m_matchState;
	bool m_isUnidirectional;	// if set true, don't bother to start over to check previous overlays.

	friend class AnimOverlaySnapshot;
};

/// --------------------------------------------------------------------------------------------------------------- ///
// for walking snapshot nodes in an AnimStateSnapshot
typedef bool (*SnapshotVisitNodeFunc)(AnimStateSnapshot* pSnapshot,
									  AnimSnapshotNode* pNode,
									  AnimSnapshotNodeBlend* pParentBlendNode,
									  float combinedBlend,
									  uintptr_t userData);
typedef bool (*SnapshotConstVisitNodeFunc)(const AnimStateSnapshot* pSnapshot,
										   const AnimSnapshotNode* pNode,
										   const AnimSnapshotNodeBlend* pParentBlendNode,
										   float combinedBlend,
										   uintptr_t userData);
typedef void (*AnimationVisitFunc)(const ArtItemAnim* pAnim,
								   const AnimStateSnapshot* pSnapshot,
								   const AnimSnapshotNode* pNode,
								   float combinedBlend,
								   float animPhase,
								   uintptr_t userData);

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCollection
{
	AnimCollection() : m_animCount(0) {}

	static const U32 kMaxAnimIds = 128;

	const ArtItemAnim* m_animArray[kMaxAnimIds];
	U32 m_animCount;

	void Add(const ArtItemAnim* pAnim);
};

/// --------------------------------------------------------------------------------------------------------------- ///
ndanim::JointParams Lerp(const ndanim::JointParams& lhs, const ndanim::JointParams& rhs, float alpha);

/// --------------------------------------------------------------------------------------------------------------- ///
struct FootPlantParams
{
	float m_tt = 0.0f;
	float m_ikBlendOutTime = 0.2f;
	float m_horiSpeedThres = 0.0f;
	float m_heightThres = 0.0f;
	float m_maxAllowedError = 0.05f;
};

/// --------------------------------------------------------------------------------------------------------------- ///
typedef BitArray64 SegmentMask;
