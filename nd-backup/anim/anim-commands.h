/*
 * Copyright (c) 2003 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ArtItemAnim;
class ArtItemSkeleton;
class JointLimits;
class JointSet;
struct AnimExecutionContext;
struct BoundingData;

namespace OrbisAnim 
{
	class WorkBuffer;
	struct ProcessingGroupContext;
	struct SegmentContext;
}

/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(8) AnimCmd
{
	enum CmdType
	{
		kInvalidCmd,

		// Control commands
		kBeginSegment,
			kBeginAnimationPhase,
				kBeginProcessingGroup,
				kEndProcessingGroup,
			kEndAnimationPhase,
		kEndSegment,

		// Animation commands
		kEvaluateClip,							// 07
		kEvaluateBlend,
		kEvaluateFeatherBlend,
		kEvaluateFlip,
		kEvaluateEmptyPose,						
		kEvaluatePose,
		kEvaluateBindPose,
		kEvaluateSnapshot,
		kEvaluateCopy,

		// This is special in that it does not need the whole group/instance thing.
		kEvaluateImpliedPose,					// 16	Evaluate the implied pose supplied in the AnimExecutionContext's SegmentData (usually the joint cache)
		kEvaluateFullPose,						// 17

		kEvaluateJointHierarchyCmds_Prepare,	// 18	Allocate needed buffers for hierarchy command evaluation
		kEvaluateJointHierarchyCmds_Evaluate,
		kInitializeJointSet_AnimPhase,
		kInitializeJointSet_RigPhase,
		kCommitJointSet_AnimPhase,
		kCommitJointSet_RigPhase,
		kApplyJointLimits,

		kEvaluateAnimPhasePlugin,				// 25	Plugin called during the animation phase (clip eval + blends...)
		kEvaluateRigPhasePlugin,				//		Plugin called before rig ops, but after all joint blending is done


		// Misc commands
		kLayer,
		kTrack,
		kState,
		kRetargetPose,
		kEvaluatePostRetarget,
		kEvaluatePoseDeferred,
		kEvaluateSnapshotPoseDeferred,
		kEvaluateSnapshotDeferred,
	};

	AnimCmd(U16 type, U16 numWords) : m_type(type), m_numCmdWords(numWords) {}

	U16 m_type = kInvalidCmd;
	U16 m_numCmdWords = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_BeginProcessingGroup : public AnimCmd
{
	AnimCmd_BeginProcessingGroup() : AnimCmd(kBeginProcessingGroup, sizeof(*this) / sizeof(U32)) {}

	U32 m_neededProcGroupInstances;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluateClip : public AnimCmd
{
	AnimCmd_EvaluateClip() : AnimCmd(kEvaluateClip, sizeof(*this) / sizeof(U32)) {}

	const ArtItemAnim* m_pArtItemAnim;
	U32 m_outputInstance;
	float m_frame;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluateBlend : public AnimCmd
{
	AnimCmd_EvaluateBlend() : AnimCmd(kEvaluateBlend, sizeof(*this) / sizeof(U32)) {}

	U32 m_leftInstance;
	U32 m_rightInstance;
	U32 m_outputInstance;
	ndanim::BlendMode m_blendMode;
	float m_blendFactor;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluateFeatherBlend : public AnimCmd_EvaluateBlend
{
	AnimCmd_EvaluateFeatherBlend() : m_pNumChannelFactors(nullptr), m_ppChannelFactors(nullptr), m_featherBlendIndex(-1)
	{
		m_type = kEvaluateFeatherBlend;
		m_numCmdWords = sizeof(*this) / sizeof(U32);
	}

	const U32* m_pNumChannelFactors;
	const OrbisAnim::ChannelFactor* const* m_ppChannelFactors;
	I32 m_featherBlendIndex;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluateFlip : public AnimCmd
{
	AnimCmd_EvaluateFlip() : AnimCmd(kEvaluateFlip, sizeof(*this) / sizeof(U32)) {}

	U32 m_outputInstance;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluateEmptyPose : public AnimCmd
{
	AnimCmd_EvaluateEmptyPose() : AnimCmd(kEvaluateEmptyPose, sizeof(*this) / sizeof(U32)) {}

	U32 m_outputInstance;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluatePose : public AnimCmd
{
	AnimCmd_EvaluatePose() : AnimCmd(kEvaluatePose, sizeof(*this) / sizeof(U32)) {}

	U32 m_outputInstance;
	U32 m_hierarchyId;
	ndanim::AnimatedJointPose m_jointPose;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluatePoseDeferred : public AnimCmd
{
	AnimCmd_EvaluatePoseDeferred() : AnimCmd(kEvaluatePoseDeferred, sizeof(*this) / sizeof(U32)) {}

	U32 m_outputInstance;
	U32 m_hierarchyId;
	ndanim::AnimatedJointPose m_jointPose;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluateSnapshot : public AnimCmd
{
	AnimCmd_EvaluateSnapshot() : AnimCmd(kEvaluateSnapshot, sizeof(*this) / sizeof(U32)) {}

	U32 m_inputInstance;
	U32 m_hierarchyId;
	ndanim::AnimatedJointPose m_jointPose;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluateBindPose : public AnimCmd
{
	AnimCmd_EvaluateBindPose() : AnimCmd(kEvaluateBindPose, sizeof(*this) / sizeof(U32)) {}

	U32 m_outputInstance;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluateCopy : public AnimCmd
{
	AnimCmd_EvaluateCopy() : AnimCmd(kEvaluateCopy, sizeof(*this) / sizeof(U32)) {}

	U32 m_srcInstance;
	U32 m_destInstance;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluateImpliedPose : public AnimCmd
{
	AnimCmd_EvaluateImpliedPose() : AnimCmd(kEvaluateImpliedPose, sizeof(*this) / sizeof(U32)) {}
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluateFullPose : public AnimCmd
{
	AnimCmd_EvaluateFullPose() : AnimCmd(kEvaluateFullPose, sizeof(*this) / sizeof(U32)) {}

	const ndanim::JointParams* m_pJointParams;
	const float* m_pFloatChannels;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluateJointHierarchy_Prepare : public AnimCmd
{
	AnimCmd_EvaluateJointHierarchy_Prepare() : AnimCmd(kEvaluateJointHierarchyCmds_Prepare, sizeof(*this) / sizeof(U32))
	{
	}

	const float* m_pInputControls;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluateJointHierarchy_Evaluate : public AnimCmd
{
	AnimCmd_EvaluateJointHierarchy_Evaluate()
		: AnimCmd(kEvaluateJointHierarchyCmds_Evaluate, sizeof(*this) / sizeof(U32))
	{
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_InitializeJointSet_AnimPhase : public AnimCmd
{
	AnimCmd_InitializeJointSet_AnimPhase() : AnimCmd(kInitializeJointSet_AnimPhase, sizeof(*this) / sizeof(U32)) {}

	JointSet* m_pJointSet;
	float m_rootScale;
	U32 m_instance;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_CommitJointSet_AnimPhase : public AnimCmd
{
	AnimCmd_CommitJointSet_AnimPhase() : AnimCmd(kCommitJointSet_AnimPhase, sizeof(*this) / sizeof(U32)) {}

	JointSet* m_pJointSet;
	float m_blend;
	U32 m_instance;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_InitializeJointSet_RigPhase : public AnimCmd
{
	AnimCmd_InitializeJointSet_RigPhase() : AnimCmd(kInitializeJointSet_RigPhase, sizeof(*this) / sizeof(U32)) {}

	JointSet* m_pJointSet;
	float m_rootScale;
//	U32 m_instance;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_CommitJointSet_RigPhase : public AnimCmd
{
	AnimCmd_CommitJointSet_RigPhase() : AnimCmd(kCommitJointSet_RigPhase, sizeof(*this) / sizeof(U32)) {}

	JointSet* m_pJointSet;
	float m_blend;
	U32 m_instance;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_ApplyJointLimits : public AnimCmd
{
	AnimCmd_ApplyJointLimits() : AnimCmd(kApplyJointLimits, sizeof(*this) / sizeof(U32)) {}

	const JointLimits* m_pJointLimits;
	JointSet* m_pJointSet;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluateAnimPhasePlugin : public AnimCmd
{
	AnimCmd_EvaluateAnimPhasePlugin() : AnimCmd(kEvaluateAnimPhasePlugin, sizeof(*this) / sizeof(U32)) {}

	StringId64 m_pluginId;
	JointSet* m_pPluginJointSet;
	U32 m_blindData[0];
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluateRigPhasePlugin : public AnimCmd
{
	AnimCmd_EvaluateRigPhasePlugin() : AnimCmd(kEvaluateRigPhasePlugin, sizeof(*this) / sizeof(U32)) {}

	StringId64 m_pluginId;
	JointSet* m_pPluginJointSet;
	U32 m_blindData[0];
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_Layer : public AnimCmd
{
	AnimCmd_Layer() : AnimCmd(kLayer, sizeof(*this) / sizeof(U32)) {}

	StringId64 m_layerNameId;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_Track : public AnimCmd
{
	AnimCmd_Track() : AnimCmd(kTrack, sizeof(*this) / sizeof(U32)) {}

	U32 m_trackIndex;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_State : public AnimCmd
{
	AnimCmd_State() : AnimCmd(kState, sizeof(*this) / sizeof(U32)) {}

	StringId64 m_stateNameId;
	F32 m_fadeTime;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluatePostRetarget : public AnimCmd
{
	AnimCmd_EvaluatePostRetarget() : AnimCmd(kEvaluatePostRetarget, sizeof(*this) / sizeof(U32)) {}

	const ArtItemSkeleton* m_pSrcSkel;
	const ArtItemSkeleton* m_pTgtSkel;
	ndanim::AnimatedJointPose m_inputPose;
	U32 m_srcSegIndex;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_RetargetPose : public AnimCmd
{
	AnimCmd_RetargetPose() : AnimCmd(kRetargetPose, sizeof(*this) / sizeof(U32)) {}

	const ndanim::JointParams* m_pSrcJointsLs;
	const ndanim::ValidBits* m_pSrcValidBits;
	ndanim::JointParams* m_pTgtJointsLs;
	ndanim::ValidBits* m_pTgtValidBits;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluateSnapshotPoseDeferred : public AnimCmd
{
	AnimCmd_EvaluateSnapshotPoseDeferred() : AnimCmd(kEvaluateSnapshotPoseDeferred, sizeof(*this) / sizeof(U32)) {}

	U32 m_outputInstance;
	const ndanim::SnapshotNode* m_pNode;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmd_EvaluateSnapshotDeferred : public AnimCmd
{
	AnimCmd_EvaluateSnapshotDeferred() : AnimCmd(kEvaluateSnapshotDeferred, sizeof(*this) / sizeof(U32)) {}

	U32 m_inputInstance;
	ndanim::SnapshotNode* m_pNode;
};

/// --------------------------------------------------------------------------------------------------------------- ///
#if ENABLE_ANIM_CMD_GEN_VALIDATION
#define VALIDATE_START() const U32 validationValue = m_currentWords;
#define VALIDATE_END(x)                                                                                                \
	ANIM_ASSERT(((x)&0x3) == 0);                                                                                       \
	ANIM_ASSERT((validationValue + ((x) / 4)) == m_currentWords)
#else
#define VALIDATE_START()
#define VALIDATE_END(x)
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList
{
public:

	AnimCmdList()
	{
		Init(nullptr, 0);
	}

	void Init(void* pBuffer, U32F bufferSize)
	{
		ANIM_ASSERT(IsPointerAligned(pBuffer, kAlign8));
		ANIM_ASSERT(IsSizeAligned(bufferSize, kAlign8));

		m_pBuffer = (U32*)pBuffer;
		m_maxWords = bufferSize / sizeof(U32);
		m_currentWords = 0;
		m_maxInstanceIndex = 0;
		m_pNeededProcGroupInstances = nullptr;
	}

	void Reset()
	{
		m_currentWords	   = 0;
		m_maxInstanceIndex = 0;
		m_pNeededProcGroupInstances = nullptr;
	}

	void AddCmd_BeginProcessingGroup()
	{
		VALIDATE_START();

		AnimCmd_BeginProcessingGroup cmd;
		cmd.m_neededProcGroupInstances = 1;

		// Store off where the AnimCmd_BeginProcessingGroup::m_neededInstances will be stored
		m_pNeededProcGroupInstances = &m_pBuffer[m_currentWords
										+ offsetof(AnimCmd_BeginProcessingGroup, m_neededProcGroupInstances) / sizeof(U32)];

		AddCmd(cmd);

		m_maxInstanceIndex = 0;

		VALIDATE_END(sizeof(AnimCmd_BeginProcessingGroup));
	}

	void AddCmd_EvaluateClip(const ArtItemAnim* pAnimation, U32F outputInstance, float frame)
	{
		ANIM_ASSERT(pAnimation);

		VALIDATE_START();

		AnimCmd_EvaluateClip cmd;
		cmd.m_pArtItemAnim	 = pAnimation;
		cmd.m_outputInstance = outputInstance;
		cmd.m_frame = frame;

		UpdateMaxInstanceIndex(outputInstance);

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_EvaluateClip));
	}

	void AddCmd_EvaluateBlend(U32F leftInstance,
							  U32F rightInstance,
							  U32F outputInstance,
							  ndanim::BlendMode blendMode,
							  float blendFactor)
	{
		VALIDATE_START();

		ANIM_ASSERT(leftInstance < 100000);
		ANIM_ASSERT(rightInstance < 100000);
		ANIM_ASSERT(outputInstance < 100000);

		ANIM_ASSERT(leftInstance <= m_maxInstanceIndex);
		ANIM_ASSERT(rightInstance <= m_maxInstanceIndex);

		ANIM_ASSERTF(outputInstance == leftInstance || outputInstance == rightInstance,
					 ("OrbisAnim::EvaluateBlend only supports outputting to left or right instance"));

		AnimCmd_EvaluateBlend cmd;
		cmd.m_leftInstance	   = leftInstance;
		cmd.m_rightInstance	   = rightInstance;
		cmd.m_outputInstance   = outputInstance;
		cmd.m_blendMode		   = blendMode;
		cmd.m_blendFactor	   = blendFactor;

		UpdateMaxInstanceIndex(outputInstance);

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_EvaluateBlend));
	}

	void AddCmd_EvaluateFeatherBlend(U32F leftInstance,
									 U32F rightInstance,
									 U32F outputInstance,
									 ndanim::BlendMode blendMode,
									 float blendFactor,
									 const OrbisAnim::ChannelFactor* const* ppChannelFactors,
									 const U32* pNumChannelFactors,
									 I32 featherBlendIndex)
	{
		VALIDATE_START();

		ANIM_ASSERT(leftInstance < 100000);
		ANIM_ASSERT(rightInstance < 100000);
		ANIM_ASSERT(outputInstance < 100000);
		ANIM_ASSERT(ppChannelFactors);
		ANIM_ASSERT(pNumChannelFactors);
		ANIM_ASSERT(featherBlendIndex >= 0);

		ANIM_ASSERT(leftInstance <= m_maxInstanceIndex);
		ANIM_ASSERT(rightInstance <= m_maxInstanceIndex);

		ANIM_ASSERTF(outputInstance == leftInstance || outputInstance == rightInstance,
					 ("OrbisAnim::EvaluateBlend only supports outputting to left or right instance"));

		AnimCmd_EvaluateFeatherBlend cmd;
		cmd.m_leftInstance	   = leftInstance;
		cmd.m_rightInstance	   = rightInstance;
		cmd.m_outputInstance   = outputInstance;
		cmd.m_blendMode		   = blendMode;
		cmd.m_blendFactor	   = blendFactor;
		cmd.m_ppChannelFactors = ppChannelFactors;
		cmd.m_pNumChannelFactors = pNumChannelFactors;
		cmd.m_featherBlendIndex	 = featherBlendIndex;

		UpdateMaxInstanceIndex(outputInstance);

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_EvaluateFeatherBlend));
	}

	void AddCmd_EvaluateFlip(U32F outputInstance)
	{
		VALIDATE_START();

		ANIM_ASSERT(outputInstance <= m_maxInstanceIndex);

		AnimCmd_EvaluateFlip cmd;
		cmd.m_outputInstance = outputInstance;

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_EvaluateFlip));
	}

	void AddCmd_EvaluateEmptyPose(U32F outputInstance)
	{
		VALIDATE_START();

		AnimCmd_EvaluateEmptyPose cmd;
		cmd.m_outputInstance = outputInstance;

		UpdateMaxInstanceIndex(outputInstance);

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_EvaluateEmptyPose));
	}

	void AddCmd_EvaluatePose(U32F outputInstance, U32 hierarchyId, const ndanim::AnimatedJointPose* pJointPose)
	{
		VALIDATE_START();

		ANIM_ASSERT(pJointPose);
		ANIM_ASSERT(pJointPose->m_pJointParams);
		//		ANIM_ASSERT(pJointPose->m_pValidBitsTable);
		ANIM_ASSERT(IsPointerAligned(pJointPose->m_pJointParams, kAlign16));
		ANIM_ASSERT(IsPointerAligned(pJointPose->m_pFloatChannels, kAlign16));
		ANIM_ASSERT(IsPointerAligned(pJointPose->m_pValidBitsTable, kAlign16));

		AnimCmd_EvaluatePose cmd;
		cmd.m_outputInstance = outputInstance;
		cmd.m_hierarchyId	 = hierarchyId;
		cmd.m_jointPose		 = *pJointPose;

		UpdateMaxInstanceIndex(outputInstance);

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_EvaluatePose));
	}

	void AddCmd_EvaluatePoseDeferred(U32F outputInstance, U32 hierarchyId, const ndanim::AnimatedJointPose* pJointPose)
	{
		VALIDATE_START();

		ANIM_ASSERT(pJointPose);
		ANIM_ASSERT(IsPointerAligned(pJointPose->m_pJointParams, kAlign16));
		ANIM_ASSERT(IsPointerAligned(pJointPose->m_pFloatChannels, kAlign16));
		ANIM_ASSERT(IsPointerAligned(pJointPose->m_pValidBitsTable, kAlign16));

		AnimCmd_EvaluatePoseDeferred cmd;
		cmd.m_outputInstance = outputInstance;
		cmd.m_hierarchyId	 = hierarchyId;
		cmd.m_jointPose		 = *pJointPose;

		UpdateMaxInstanceIndex(outputInstance);

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_EvaluatePoseDeferred));
	}

	void AddCmd_EvaluateSnapshot(U32F inputInstance, U32 hierarchyId, const ndanim::AnimatedJointPose* pJointPose)
	{
		VALIDATE_START();

		ANIM_ASSERT(inputInstance <= m_maxInstanceIndex);

		ANIM_ASSERT(pJointPose);
		ANIM_ASSERT(IsPointerAligned(pJointPose->m_pJointParams, kAlign16));
		ANIM_ASSERT(IsPointerAligned(pJointPose->m_pFloatChannels, kAlign16));
		ANIM_ASSERT(IsPointerAligned(pJointPose->m_pValidBitsTable, kAlign16));

		AnimCmd_EvaluateSnapshot cmd;
		cmd.m_inputInstance = inputInstance;
		cmd.m_hierarchyId	= hierarchyId;
		cmd.m_jointPose		= *pJointPose;

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_EvaluateSnapshot));
	}

	void AddCmd_EvaluateBindPose(U32F outputInstance)
	{
		VALIDATE_START();

		AnimCmd_EvaluateBindPose cmd;
		cmd.m_outputInstance = outputInstance;

		UpdateMaxInstanceIndex(outputInstance);

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_EvaluateBindPose));
	}

	void AddCmd_EvaluateCopy(U32F srcInstance, U32F destInstance)
	{
		VALIDATE_START();

		ANIM_ASSERT(srcInstance <= m_maxInstanceIndex);

		AnimCmd_EvaluateCopy cmd;
		cmd.m_srcInstance  = srcInstance;
		cmd.m_destInstance = destInstance;

		UpdateMaxInstanceIndex(destInstance);

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_EvaluateCopy));
	}

	void AddCmd_EvaluateFullPose(const ndanim::JointParams* pJointParams, const float* pFloatChannels)
	{
		VALIDATE_START();

		ANIM_ASSERT(IsPointerAligned(pJointParams, kAlign16));
		ANIM_ASSERT(IsPointerAligned(pFloatChannels, kAlign16));

		AnimCmd_EvaluateFullPose cmd;
		cmd.m_pJointParams = pJointParams;
		cmd.m_pFloatChannels = pFloatChannels;

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_EvaluateFullPose));
	}

	void AddCmd_EvaluateImpliedPose()
	{
		VALIDATE_START();

		AnimCmd_EvaluateImpliedPose cmd;

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_EvaluateImpliedPose));
	}

	void AddCmd_EvaluateJointHierarchyCmds_Prepare(const float* pInputControls)
	{
		VALIDATE_START();

		ANIM_ASSERT(IsPointerAligned(pInputControls, kAlign16));

		AnimCmd_EvaluateJointHierarchy_Prepare cmd;
		cmd.m_pInputControls = pInputControls;

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_EvaluateJointHierarchy_Prepare));
	}

	void AddCmd_EvaluateJointHierarchyCmds_Evaluate()
	{
		VALIDATE_START();

		AnimCmd_EvaluateJointHierarchy_Evaluate cmd;

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_EvaluateJointHierarchy_Evaluate));
	}

	void AddCmd_InitializeJointSet_AnimPhase(JointSet* pJointSet, float rootScale, U32 instanceIndex)
	{
		VALIDATE_START();

		ANIM_ASSERT(instanceIndex <= m_maxInstanceIndex);

		AnimCmd_InitializeJointSet_AnimPhase cmd;
		cmd.m_pJointSet = pJointSet;
		cmd.m_rootScale = rootScale;
		cmd.m_instance	= instanceIndex;

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_InitializeJointSet_AnimPhase));
	}

	void AddCmd_InitializeJointSet_RigPhase(JointSet* pJointSet, float rootScale)
	{
		VALIDATE_START();

		AnimCmd_InitializeJointSet_RigPhase cmd;
		cmd.m_pJointSet = pJointSet;
		cmd.m_rootScale = rootScale;

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_InitializeJointSet_RigPhase));
	}

	void AddCmd_CommitJointSet_AnimPhase(JointSet* pJointSet, float blend, U32 instance)
	{
		VALIDATE_START();

		AnimCmd_CommitJointSet_AnimPhase cmd;
		cmd.m_pJointSet = pJointSet;
		cmd.m_blend		= blend;
		cmd.m_instance	= instance;

		UpdateMaxInstanceIndex(instance);

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_CommitJointSet_AnimPhase));
	}

	void AddCmd_CommitJointSet_RigPhase(JointSet* pJointSet, float blend)
	{
		VALIDATE_START();

		AnimCmd_CommitJointSet_RigPhase cmd;
		cmd.m_pJointSet = pJointSet;
		cmd.m_blend		= blend;

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_CommitJointSet_RigPhase));
	}

	void AddCmd_ApplyJointLimits(const JointLimits* pJointLimits, JointSet* pJointSet)
	{
		VALIDATE_START();

		AnimCmd_ApplyJointLimits cmd;
		cmd.m_pJointLimits = pJointLimits;
		cmd.m_pJointSet	   = pJointSet;

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_ApplyJointLimits));
	}

	template <typename T>
	void AddCmd_EvaluateAnimPhasePlugin(StringId64 pluginNameId,
										const T* pBlindData,
										JointSet* pPluginJointSet = nullptr)
	{
		VALIDATE_START();

		ANIM_ASSERT(pBlindData);
		ANIM_ASSERT(IsPointerAligned(pBlindData, kAlign8));
		ANIM_ASSERT(IsSizeAligned(sizeof(T), kAlign8));
		ANIM_ASSERT(sizeof(T) <= 128);

		AnimCmd_EvaluateAnimPhasePlugin cmd;
		cmd.m_pluginId = pluginNameId;
		cmd.m_pPluginJointSet = pPluginJointSet;
		cmd.m_numCmdWords += sizeof(T) / sizeof(U32);

		AddCmd(cmd);

		U32F offset = m_currentWords;
		const U32* pBlindDataAsU32 = (U32*)pBlindData;
		for (U32F i = 0; i < sizeof(T) / sizeof(U32); ++i)
		{
			offset = AddWord(offset, pBlindDataAsU32[i]);
		}
		m_currentWords = offset;

		VALIDATE_END(sizeof(AnimCmd_EvaluateAnimPhasePlugin) + sizeof(T));
	}

	void AddCmd_EvaluateAnimPhasePlugin(StringId64 pluginNameId,
										const void* pBlindData,
										U32 blindDataSize,
										JointSet* pPluginJointSet = nullptr)
	{
		VALIDATE_START();

		ANIM_ASSERT(pBlindData);
		ANIM_ASSERT(IsPointerAligned(pBlindData, kAlign8));
		ANIM_ASSERT(IsSizeAligned(blindDataSize, kAlign8));
		ANIM_ASSERT(blindDataSize <= 128);

		AnimCmd_EvaluateAnimPhasePlugin cmd;
		cmd.m_pluginId = pluginNameId;
		cmd.m_pPluginJointSet = pPluginJointSet;
		cmd.m_numCmdWords += blindDataSize / sizeof(U32);

		AddCmd(cmd);

		U32F offset = m_currentWords;
		const U32* pBlindDataAsU32 = (U32*)pBlindData;
		for (U32F i = 0; i < blindDataSize / sizeof(U32); ++i)
		{
			offset = AddWord(offset, pBlindDataAsU32[i]);
		}
		m_currentWords = offset;

		VALIDATE_END(sizeof(AnimCmd_EvaluateAnimPhasePlugin) + blindDataSize);
	}

	template <typename T>
	void AddCmd_EvaluateRigPhasePlugin(StringId64 pluginNameId,
											const T* pBlindData,
											JointSet* pPluginJointSet = nullptr)
	{
		VALIDATE_START();

		ANIM_ASSERT(pBlindData);
		ANIM_ASSERT(IsPointerAligned(pBlindData, kAlign8));
		ANIM_ASSERT(IsSizeAligned(sizeof(T), kAlign8));
		ANIM_ASSERT(sizeof(T) <= 128);

		AnimCmd_EvaluateRigPhasePlugin cmd;
		cmd.m_pluginId = pluginNameId;
		cmd.m_pPluginJointSet = pPluginJointSet;
		cmd.m_numCmdWords += sizeof(T) / sizeof(U32);

		AddCmd(cmd);

		U32F offset = m_currentWords;
		const U32* pBlindDataAsU32 = (U32*)pBlindData;
		for (U32F i = 0; i < sizeof(T) / sizeof(U32); ++i)
		{
			offset = AddWord(offset, pBlindDataAsU32[i]);
		}
		m_currentWords = offset;

		VALIDATE_END(sizeof(AnimCmd_EvaluateRigPhasePlugin) + sizeof(T));
	}

	void AddCmd_EvaluateRigPhasePlugin(StringId64 pluginNameId,
											const void* pBlindData,
											U32 blindDataSize,
											JointSet* pPluginJointSet = nullptr)
	{
		VALIDATE_START();

		ANIM_ASSERT(pBlindData);
		ANIM_ASSERT(IsPointerAligned(pBlindData, kAlign8));
		ANIM_ASSERT(IsSizeAligned(blindDataSize, kAlign8));
		ANIM_ASSERT(blindDataSize <= 128);

		AnimCmd_EvaluateRigPhasePlugin cmd;
		cmd.m_pluginId = pluginNameId;
		cmd.m_pPluginJointSet = pPluginJointSet;
		cmd.m_numCmdWords += blindDataSize / sizeof(U32);

		AddCmd(cmd);

		U32F offset = m_currentWords;
		const U32* pBlindDataAsU32 = (U32*)pBlindData;
		for (U32F i = 0; i < blindDataSize / sizeof(U32); ++i)
		{
			offset = AddWord(offset, pBlindDataAsU32[i]);
		}
		m_currentWords = offset;

		VALIDATE_END(sizeof(AnimCmd_EvaluateRigPhasePlugin) + blindDataSize);
	}

	void AddCmd_Layer(StringId64 layerNameId)
	{
		VALIDATE_START();

		AnimCmd_Layer cmd;
		cmd.m_layerNameId = layerNameId;

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_Layer));
	}

	void AddCmd_Track(U32 trackIndex)
	{
		VALIDATE_START();

		AnimCmd_Track cmd;
		cmd.m_trackIndex = trackIndex;

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_Track));
	}

	void AddCmd_State(StringId64 stateNameId, F32 fadeTime)
	{
		VALIDATE_START();

		AnimCmd_State cmd;
		cmd.m_stateNameId = stateNameId;
		cmd.m_fadeTime = fadeTime;

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_State));
	}

	void AddCmd_EvaluatePostRetarget(const ArtItemSkeleton* pSrcSkel,
									 const ArtItemSkeleton* pTgtSkel,
									 const ndanim::AnimatedJointPose& inputPose)
	{
		VALIDATE_START();

		AnimCmd_EvaluatePostRetarget cmd;
		cmd.m_pSrcSkel = pSrcSkel;
		cmd.m_pTgtSkel = pTgtSkel;
		cmd.m_inputPose = inputPose;

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_EvaluatePostRetarget));
	}

	void AddCmd(AnimCmd::CmdType cmdType)
	{
		VALIDATE_START();

		// Begin Segment command had to be the first command in the stream
		ANIM_ASSERT(cmdType != AnimCmd::kBeginSegment || m_currentWords == 0);

		// Add the type and command size
		AnimCmd cmd((U16)cmdType, sizeof(AnimCmd) / 4);
		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd));
	}

	template <typename T>
	void AddCmd(const T& animCmd)
	{
		// Add the type and command size
		*reinterpret_cast<T*>(m_pBuffer + m_currentWords) = animCmd;
		m_currentWords += sizeof(T) / sizeof(U32);
		ANIM_ASSERT(m_currentWords < m_maxWords);

		if (animCmd.m_type == AnimCmd::kEndProcessingGroup)
		{
			// Patch up the AnimCmd_BeginProcessingGroup::m_neededInstances
			*m_pNeededProcGroupInstances = m_maxInstanceIndex + 1;
			m_pNeededProcGroupInstances	= nullptr;
			m_maxInstanceIndex	= 0;
		}
	}

	void AddCmd_EvaluateSnapshotPoseDeferred(U32F outputInstance, const ndanim::SnapshotNode* pSnapshot)
	{
		VALIDATE_START();

		AnimCmd_EvaluateSnapshotPoseDeferred cmd;
		cmd.m_outputInstance = outputInstance;
		cmd.m_pNode = pSnapshot;

		UpdateMaxInstanceIndex(outputInstance);

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_EvaluateSnapshotPoseDeferred));
	}

	void AddCmd_EvaluateSnapshotDeferred(U32F inputInstance, ndanim::SnapshotNode* pSnapshot)
	{
		VALIDATE_START();

		ANIM_ASSERT(inputInstance <= m_maxInstanceIndex);

		AnimCmd_EvaluateSnapshotDeferred cmd;
		cmd.m_inputInstance = inputInstance;
		cmd.m_pNode			= pSnapshot;

		AddCmd(cmd);

		VALIDATE_END(sizeof(AnimCmd_EvaluateSnapshotDeferred));
	}

	void CopyCmd(const AnimCmd* pCmd)
	{
		memcpy(m_pBuffer + m_currentWords, pCmd, pCmd->m_numCmdWords * sizeof(U32));
		m_currentWords += pCmd->m_numCmdWords;
	}

	const U32* GetBuffer() const { return m_pBuffer; }
	U32F GetNumWordsUsed() const { return m_currentWords; }
	U32F GetMaxWords() const { return m_maxWords; }
	U32F GetMaxInstanceIndex() const { return m_maxInstanceIndex; }

private:
	U32F AddWord(U32F offset, U16 data1, U16 data2)
	{
		ANIM_ASSERT(offset < m_maxWords);
		U16* buffer16 = reinterpret_cast<U16*>(m_pBuffer + offset);
		buffer16[0]	  = data1;
		buffer16[1]	  = data2;

		return offset + 1;
	}

	U32F AddWord(U32F offset, U32 data)
	{
		ANIM_ASSERT(offset < m_maxWords);
		m_pBuffer[offset] = data;
		return offset + 1;
	}

	U32F AddFloat(U32F offset, float data)
	{
		ANIM_ASSERT(offset < m_maxWords);
		*reinterpret_cast<float*>(m_pBuffer + offset) = data;
		return offset + sizeof(float) / 4;
	}

	U32F AddPtr(U32F offset, const void* ptr)
	{
		ANIM_ASSERT(offset < m_maxWords);
		*reinterpret_cast<uintptr_t*>(m_pBuffer + offset) = reinterpret_cast<uintptr_t>(ptr);
		return offset + sizeof(uintptr_t) / 4;
	}

	void UpdateMaxInstanceIndex(U32 instance) { m_maxInstanceIndex = Max(m_maxInstanceIndex, instance); }

	U32* m_pBuffer;
	U32 m_maxWords;
	U32 m_currentWords;
	U32* m_pNeededProcGroupInstances;
	U32 m_maxInstanceIndex;
};


/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimExecutionSegmentData
{
	ndanim::JointParams*	m_pJointParams = nullptr;					// Output from 'local space joints'
	Transform*				m_pJointTransforms = nullptr;				// Output from 'joint-conversion'
	Vec4*					m_pSkinningBoneMats = nullptr;				// Output from 'skinning matrices'
	float*					m_pOutputControls = nullptr;				// Output from 'local space joints' as it is [num float channels] followed by [num procedural output controls]
};


/// --------------------------------------------------------------------------------------------------------------- ///
// Only joints in the active processing group have been animated
typedef void AnimPhasePluginCommandHandler(OrbisAnim::WorkBuffer* pWorkBuffer,
										   OrbisAnim::SegmentContext* pSegmentContext,
										   OrbisAnim::ProcessingGroupContext* pGroupContext,
										   const AnimExecutionContext* pContext,
										   StringId64 pluginName,
										   const void* pPluginData,
										   JointSet* pPluginJointSet);

/// --------------------------------------------------------------------------------------------------------------- ///
// All joints have been animated in a segment
typedef void RigPhasePluginCommandHandler(OrbisAnim::WorkBuffer* pWorkBuffer,
											   OrbisAnim::SegmentContext* pSegmentContext,
											   const AnimExecutionContext* pContext,
											   StringId64 pluginName,
											   const void* pPluginData,
											   JointSet* pPluginJointSet);

/// --------------------------------------------------------------------------------------------------------------- ///
/// This structure is used to evaluate animations and output the results.
/// It does not require an FgAnimData or other dependencies
/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimExecutionContext
{
	enum Outputs
	{
		// Animated pose
		kOutputJointParamsLs		= (1 << 0),
		kOutputFloatChannels		= (1 << 1),

		// Full pose
		kOutputTransformsOs			= (1 << 2),
		kOutputSkinningMats			= (1 << 3),
		kOutputOutputControls		= (1 << 4),
	};

	void Init(const ArtItemSkeleton* pArtItemSkel,
			  const Transform* pObjXform,
			  ndanim::JointParams* pJointCacheJointParams,
			  Transform* pJointCacheTransforms,
			  const float* pJointCacheInputControls,
			  float* pJointCacheOutputControls,
			  void* pPersistentData,
			  Memory::Allocator* const pAlloc = nullptr);

	void AllocateSkinningMats(Memory::Allocator* const pAlloc = nullptr);

	void Validate() const;

	NdAtomic64				m_lock;

	const ArtItemSkeleton*	m_pSkel = nullptr;
	
	const FgAnimData*		m_pAnimDataHack = nullptr;				// REMOVE THIS ASAP - CGY

	AnimCmdList				m_animCmdList;					// Clip evaluation and blend command list
	AnimExecutionSegmentData* m_pSegmentData = nullptr;

	JointSet*				m_pPluginJointSet = nullptr;
	const Transform*		m_pObjXform = nullptr;

	void*					m_pDependencyTable = nullptr;
	void*					m_pPersistentData = nullptr;

	// All skinning mats and output controls. This is migrating into the per-segment data
	Vec4*					m_pAllSkinningBoneMats = nullptr;
	const float*			m_pAllInputControls = nullptr;
	float*					m_pAllOutputControls = nullptr;

	AnimPhasePluginCommandHandler* m_pAnimPhasePluginFunc = nullptr;
	RigPhasePluginCommandHandler* m_pRigPhasePluginFunc = nullptr;

	// Cloth hack for now
	ndgi::Label32*			m_pClothSkinningBoneMatsLabels = nullptr;			// Array of Labels used to signal with cloth is finished modifying m_pSkinningBoneMats. All labels are 0 when complete.
	U32						m_numClothSkinningBoneMatsLabels = 0;

	mutable U32				m_processedSegmentMask_JointParams = 0;				// Animated
	mutable U32				m_processedSegmentMask_FloatChannels = 0;			// Animated
	mutable U32				m_processedSegmentMask_Transforms = 0;				// Parented (Animated + Procedural)
	mutable U32				m_processedSegmentMask_SkinningMats = 0;			// Parented (Animated + Procedural)
	mutable U32				m_processedSegmentMask_OutputControls = 0;			// Animated and driven

	U32 GetCombinedSegmentMask() const
	{
		return m_processedSegmentMask_JointParams | m_processedSegmentMask_FloatChannels
			   | m_processedSegmentMask_Transforms | m_processedSegmentMask_SkinningMats
			   | m_processedSegmentMask_OutputControls;
	}

	bool 					m_allowAnimPhasePlugins : 1;
	bool 					m_allowRigPhasePlugins : 1;

	// Move this parameter as a function parameter of ProcessRequiredSegments
	bool					m_useImpliedPoseAsInput : 1;			// Use the supplied JointParams as the input (Joint Cache)

	bool					m_disableBoundingVolumeUpdate : 1;
	bool					m_includeProceduralJointParamsInOutput : 1;

	bool					m_dependencyTableValid : 1;
};


/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessRequiredSegments(U32 requiredSegmentMask,
							 U32 outputMask,
							 AnimExecutionContext* pCtx,
							 const AnimExecutionContext* pPrevCtx,
							 bool useImpliedPoseAsInput = false,
							 bool kickEarlyNextFrame = false);
void BatchProcessRequiredSegmentsForAllObjects(const U32* pRequiredSegmentMasks /*AnimMgr::kMaxAnimData*/,
											   const U32* pOutputMasks /*AnimMgr::kMaxAnimData*/,
											   I64 frameIndex);

/// --------------------------------------------------------------------------------------------------------------- ///
AnimExecutionContext* AllocateAnimContextForThisFrame(int animDataIndex);
AnimExecutionContext* GetAnimExecutionContext(const FgAnimData* pAnimData);
const AnimExecutionContext* GetPrevFrameAnimExecutionContext(const FgAnimData* pAnimData);

