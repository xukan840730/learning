/*
 * Copyright (c) 2009 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/util/color.h"
#include "corelib/util/timeframe.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-state.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/scriptx/h/animation-script-types.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class AnimControl;
class AnimLayer;
class AnimStateLayer;
class ArtItemAnim;
class ArtItemSkeleton;
class Clock;
class JointCache;
class Locator;
struct AnimCameraCutInfo;
struct AnimCmd;
struct CompressedChannel;
struct FgAnimData;
struct SkelComponentDesc;

namespace DC
{
	typedef U64 AnimStateFlag;
	struct AnimBlendTable;
}

namespace DMENU
{
	class ItemEnumPair;
}

/// --------------------------------------------------------------------------------------------------------------- ///
typedef const Scalar AlignDistFunc(Point_arg, Point_arg);
const DMENU::ItemEnumPair* GetAnimCurveDevMenuEnumPairs();

/// --------------------------------------------------------------------------------------------------------------- ///
class DualSnapshotNode
{
public:
	DualSnapshotNode();
	~DualSnapshotNode();

	void Init(const FgAnimData* pAnimData);
	void Copy(const DualSnapshotNode& source, const FgAnimData* pAnimData);
	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	ndanim::SnapshotNode* GetSnapshoteNodeForHeirId(U32 heirId);
	const ndanim::SnapshotNode* GetSnapshoteNodeForHeirId(U32 heirId) const;

	bool IsInUse() const;

private:
	ndanim::SnapshotNode m_animSnapshot[2];
	mutable I64 m_lastFrameUsed;
};

/// --------------------------------------------------------------------------------------------------------------- ///
/// Calculate the blend value to use based on various blend curves. (tt is between 0.0 and 1.0f)
/// --------------------------------------------------------------------------------------------------------------- ///
float CalculateCurveValue(float tt, DC::AnimCurveType curveType);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Calculate the corresponding Maya frame (sampling independent) for a given phase
/// --------------------------------------------------------------------------------------------------------------- ///
float GetMayaFrameFromClip(const ndanim::ClipData* pClipData, float phase);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Calculate the corresponding phase for a given Maya frame (sampling independent)
/// --------------------------------------------------------------------------------------------------------------- ///
float GetClipPhaseForMayaFrame(const ndanim::ClipData* pClipData, float mayaFrame);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Calculate desired phase that best match the current foot locations
/// --------------------------------------------------------------------------------------------------------------- ///
float AdvanceByIntegration(const CompressedChannel* pAnimChannel, float& desiredDist, float startPhase);
float AdvanceByIntegration(const CompressedChannel* pAnimChannel,
						   float& desiredDist,
						   float startPhase,
						   AlignDistFunc distFunc);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Get/Set the clock used to advance the animation system
/// --------------------------------------------------------------------------------------------------------------- ///
void AnimationTimeUpdate();
void AnimationSetSkewTime(float seconds);
float GetAnimationSystemTime();
float GetAnimationSystemDeltaTime();
void SetAnimationClock(Clock* pClock);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Get the duration of an animation... 5 frames == 4 frame-intervals == 4 * secondsPerFrame
/// --------------------------------------------------------------------------------------------------------------- ///
float GetDuration(const ArtItemAnim* pAnim);

/// --------------------------------------------------------------------------------------------------------------- ///
float LimitBlendTime(const ArtItemAnim* pAnim, float startPhase, float desiredBlendTime);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Calculate the corresponding phase for a given Maya frame (sampling independent)
/// --------------------------------------------------------------------------------------------------------------- ///
float GetPhaseFromClipFrame(const ndanim::ClipData* pClipData, float frame);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Calculate the corresponding phase for a given time
/// --------------------------------------------------------------------------------------------------------------- ///
float GetPhaseFromClipTime(const ndanim::ClipData* pClipData, float time);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Find out where the action pack reference 'channelName' is in align-space for animation 'animName'.
/// --------------------------------------------------------------------------------------------------------------- ///
bool EvaluateChannelInAnim(SkeletonId skelId,
						   const ArtItemAnim* pAnim,
						   StringId64 chanNameId,
						   float phase,
						   ndanim::JointParams* pParams,
						   bool mirror						 = false,
						   bool wantRawScale				 = false,
						   AnimCameraCutInfo* pCameraCutInfo = nullptr);

bool EvaluateChannelInAnim(SkeletonId skelId,
						   const ArtItemAnim* pAnim,
						   StringId64 chanNameId,
						   float phase,
						   Locator* pLocOut,
						   bool mirror						 = false,
						   bool wantRawScale				 = false,
						   AnimCameraCutInfo* pCameraCutInfo = nullptr);

bool EvaluateChannelInAnim(const AnimControl* pAnimControl,
						   StringId64 animName,
						   StringId64 channelName,
						   float phase,
						   Locator* pLocOut,
						   bool mirror						 = false,
						   bool wantRawScale				 = false,
						   AnimCameraCutInfo* pCameraCutInfo = nullptr);

bool EvaluateChannelInAnim(const AnimControl* pAnimControl,
						   StringId64 animName,
						   StringId64 channelName,
						   float phase,
						   ndanim::JointParams* pParams,
						   bool mirror						 = false,
						   bool wantRawScale				 = false,
						   AnimCameraCutInfo* pCameraCutInfo = nullptr);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Find out where the align is given a world-space apReference locator and an animation
/// --------------------------------------------------------------------------------------------------------------- ///
bool FindAlignFromApReference(SkeletonId skelId,
							  const ArtItemAnim* pAnim,
							  float phase,
							  const Locator& apRef,
							  StringId64 apRefNameId,
							  Locator* pOutAlign,
							  bool mirror = false);

bool FindAlignFromApReference(const AnimControl* pAnimControl,
							  StringId64 animNameId,
							  float phase,
							  const Locator& apRef,
							  StringId64 apRefNameId,
							  Locator* pOutAlign,
							  bool mirror = false);

bool FindAlignFromApReference(const AnimControl* pAnimControl,
							  StringId64 animNameId,
							  float phase,
							  const Locator& apRef,
							  Locator* pOutAlign,
							  bool mirror = false);

bool FindAlignFromApReference(const AnimControl* pAnimControl,
							  StringId64 animNameId,
							  const Locator& apRef,
							  Locator* pOutAlign,
							  bool mirror = false);


/// --------------------------------------------------------------------------------------------------------------- ///
/// Find out where the align would be starting from the current align (no apReference)
/// --------------------------------------------------------------------------------------------------------------- ///

bool FindFutureAlignFromAlign(const SkeletonId skelId,
							  const ArtItemAnim* pAnim,
							  float phase,
							  const Locator& currentAlign,
							  Locator* pOutAlign,
							  bool mirror = false);

bool FindFutureAlignFromAlign(const AnimControl* pAnimControl,
							  StringId64 animNameId,
							  float phase,
							  const Locator& currentAlign,
							  Locator* pOutAlign,
							  bool mirror = false);

bool FindFutureAlignFromAlign(const NdGameObject* pGo,
							  StringId64 animNameId,
							  float phase,
							  Locator* pOutAlign,
							  bool mirror = false);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Find out where the apReference would be given a world-space align locator and an animation
/// --------------------------------------------------------------------------------------------------------------- ///
bool FindApReferenceFromAlign(SkeletonId skeletonId,
							  const ArtItemAnim* pAnim,
							  const Locator& align,
							  Locator* pOutApRef,
							  StringId64 apRefNameId,
							  float phase,
							  bool mirror = false);

bool FindApReferenceFromAlign(SkeletonId skeletonId,
							  const ArtItemAnim* pAnim,
							  const Locator& align,
							  Locator* pOutApRef,
							  float phase,
							  bool mirror = false);

bool FindApReferenceFromAlign(const AnimControl* pAnimControl,
							  StringId64 animNameId,
							  const Locator& align,
							  Locator* pOutApRef,
							  float phase,
							  bool mirror = false);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Find out where the channels would be given a world-space apReference locator and an animation
/// --------------------------------------------------------------------------------------------------------------- ///
U32 FindChannelsFromApReference(SkeletonId skeletonId,
								const ArtItemAnim* pAnim,
								float phase,
								const Locator& apRef,
								const StringId64 apRefId,
								const StringId64* channelNames,
								int numChannels,
								Locator* pLocsOut,
								bool mirror = false);

U32 FindChannelsFromApReference(const AnimControl* pAnimControl,
								StringId64 animNameId,
								float phase,
								const Locator& apRef,
								const StringId64* channelNames,
								int numChannels,
								Locator* pLocsOut,
								bool mirror = false);

U32 FindChannelsFromApReference(SkeletonId skeletonId,
								const ArtItemAnim* pAnim,
								float phase,
								const Locator& apRef,
								const StringId64* channelNames,
								int numChannels,
								Locator* pLocsOut,
								bool mirror = false);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Calculate the equivalent local space JointParams for a particular world space joint (does not handle joints with scale)
/// --------------------------------------------------------------------------------------------------------------- ///
void CalculateJointParamsFromJointLocatorsWs(const JointCache& jc,
											 ndanim::JointParams& outParams,
											 U32F iJoint,
											 const Locator& jointLocatorWs,
											 const Locator& alignWs,
											 const Vector& invScale = Vector(1.0f, 1.0f, 1.0f));

void CalculateJointParamsFromJointLocatorsWsNoAnimation(const JointCache& jc,
														ndanim::JointParams& outParams,
														U32F iJoint,
														const Locator& jointLocatorWs,
														const Locator& alignWs,
														const Vector& invScale = Vector(1.0f, 1.0f, 1.0f));

/// --------------------------------------------------------------------------------------------------------------- ///
/// Convenience functions to animate an object synchronously
/// --------------------------------------------------------------------------------------------------------------- ///
enum AnimateFlags
{
	kAnimateFlag_None			 = 0,
	kAnimateFlag_Mirror			 = 1 << 0,
	kAnimateFlag_AllSegments	 = 1 << 1,
	kAnimateFlag_IncludeProceduralJointParamsInOutput = 1 << 2,
};

bool AnimateObject(const Transform& objectXform,
				   const ArtItemSkeleton* pArtItemSkeleton,
				   const ArtItemAnim* pAnim,
				   float sample,
				   Transform* pOutJointTransforms,
				   ndanim::JointParams* pOutJointParams,
				   float const* pInputControls,
				   float* pOutputControls,
				   AnimateFlags flags = AnimateFlags::kAnimateFlag_None);

void AnimateJoints(const Transform& objectXform,
				   const ArtItemSkeleton* pArtItemSkeleton,
				   const ArtItemAnim* pAnim,
				   float sample,
				   const U32* pJointIndices,
				   U32F numJointIndices,
				   Transform* pOutJointTransforms,
				   ndanim::JointParams* pOutJointParams,
				   float const* pInputControls,
				   float* pOutputControls);

bool ComparePoses(const ArtItemSkeleton* pSkeleton,
				  const ArtItemAnim* pAnim1,
				  float sample1,
				  const ArtItemAnim* pAnim2,
				  float sample2);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Convenience functions to find a joint index by name
/// --------------------------------------------------------------------------------------------------------------- ///
I16 FindJoint(const SkelComponentDesc* descsBegin, U32 numJoints, StringId64 jid);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Convenience functions to find a joint name by index
/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 FindJointNameId(const ArtItemSkeleton* pSkel, const I16 jointIdex);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Validate the joints in the joint cache to find bad joint data
/// --------------------------------------------------------------------------------------------------------------- ///
void ValidateJointCache(const FgAnimData* pAnimData);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Print the animation system commands (not ICE)
/// --------------------------------------------------------------------------------------------------------------- ///
void PrintAnimCmdList(const AnimCmdList* pAnimCmdList,
					  DoutBase* pOutput		   = GetMsgOutput(kMsgAnim),
					  const AnimCmd* pCrashCmd = nullptr);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Check for the existence of a channel
/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimHasChannel(const ArtItemAnim* pAnim, const StringId64 channelId);
bool AnimHasChannels(const ArtItemAnim* pAnim, const StringId64* pChannelIdList);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Helper functions for ik.
/// --------------------------------------------------------------------------------------------------------------- ///
void JointCache_UpdateWSSubPose(const Locator& alignWs,
								JointCache* pJc,
								U32F iJoint,
								bool debugDraw	 = false,
								Color debugColor = kColorBlue);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Misc. helper funcs
/// --------------------------------------------------------------------------------------------------------------- ///

float ComputeFlagContribution(const AnimStateLayer* pLayer,
							  const DC::AnimStateFlag flags,
							  FadeMethodToUse fadeMethod = kUseMasterFade);

float ComputeFloatChannelContribution(const AnimStateLayer* pLayer,
									  StringId64 channelId,
									  float defaultValue,
									  FadeMethodToUse fadeMethod = kUseAnimFade);

/// --------------------------------------------------------------------------------------------------------------- ///
bool GetAPReferenceByName(const AnimLayer* pAnimLayer,
						  StringId64 apRefNameId,
						  ndanim::JointParams& sqt,
						  bool blendWithNonContributingAnimations = true,
						  const ndanim::JointParams* pNeutralSqt  = nullptr,
						  bool wantRawScale						  = false,
						  bool useMotionFade					  = true,
						  DC::AnimFlipMode flipMode				  = DC::kAnimFlipModeFromInstance,
						  bool blendWithNonContributingAnimsForReal = false);

/// --------------------------------------------------------------------------------------------------------------- ///
bool GetAPReferenceByNameConditional(const AnimLayer* pAnimLayer,
									 StringId64 apRefNameId,
									 ndanim::JointParams& sqt,
									 bool blendWithNonContributingAnimations,
									 const ndanim::JointParams* pNeutralSqt,
									 bool wantRawScale,
									 bool useMotionFade,
									 DC::AnimFlipMode flipMode,
									 AnimStateLayerFilterAPRefCallBack filterCallback,
	                                 bool disableRetargeting);

/// --------------------------------------------------------------------------------------------------------------- ///
bool GetAPReferenceByName(const AnimControl* pAnimControl, StringId64 apRefNameId, Locator& outLoc);

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawAnimationAlignPath(const AnimControl* pAnimControl,
								 StringId64 anim,
								 const Locator& apRef,
								 bool mirrored,
								 Color color		  = kColorWhite,
								 TimeFrame drawTime	  = Seconds(1.0f),
								 bool drawStartEnd	  = true,
								 U32F numSamplePoints = 16);

/// --------------------------------------------------------------------------------------------------------------- ///
// Compute the speed of the align at a specific phase
float GetAlignSpeedAtPhase(const ArtItemAnim* pAnim, SkeletonId skelId, float phase);
float GetAlignSpeedAtPhase(const ArtItemAnim* pAnim, SkeletonId skelId, float phase, AlignDistFunc distFunc);
Vector GetAlignVelocityAtPhase(const ArtItemAnim* pAnim, SkeletonId skelId, float phase);

/// --------------------------------------------------------------------------------------------------------------- ///
// Print to the TTY how the joints are segmented for the objects skeleton(s)
void DebugPrintSkeletonSegments(const FgAnimData* pAnimData);

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::BlendParams* LookupAnimBlendTableEntry(const DC::AnimBlendTable* pBlendTable,
												 const StringId64 curAnimId,
												 const StringId64 desAnimId,
												 bool disableWildcardSource);

/// --------------------------------------------------------------------------------------------------------------- ///
F32 CalculatePoseError(const NdGameObject* pGo,
					   const StringId64 animId,
					   float animPhase = 0.0f,
					   bool debug	   = false,
					   const BoundFrame* pFrame = nullptr);

/// --------------------------------------------------------------------------------------------------------------- ///
struct DistAlignPathsParams
{
	SkeletonId m_skelId = INVALID_SKELETON_ID;
	
	const ArtItemAnim* m_pSrcAnim = nullptr;
	Locator m_srcApLoc = kIdentity;
	StringId64 m_srcApId = SID("apReference");
	float m_srcStartPhase = 0.0f;
	float m_srcEndPhase = 1.0f;

	const ArtItemAnim* m_pDstAnim = nullptr;
	Locator m_dstApLoc = kIdentity;
	StringId64 m_dstApId = SID("apReference");
	float m_dstStartPhase = 0.0f;
	float m_dstEndPhase = 1.0f;

	U32 m_maxSamplesPerAnim = 8;

	bool m_debugDraw = false;
	DebugPrimTime m_debugDrawTime = kPrimDuration1FrameAuto;
};

/// --------------------------------------------------------------------------------------------------------------- ///
float DistAlignPaths(const DistAlignPathsParams& params,
					 float* pBestSrcPhaseOut = nullptr,
					 float* pBestDstPhaseOut = nullptr);

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename T>
const T* LookupAnimStateInfo(const AnimControl* pAnimControl, StringId64 infoId, StringId64 animStateId)
{
	const AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer();
	if (!pBaseLayer)
	{
		return nullptr;
	}
	const DC::AnimState* pState = (animStateId != INVALID_STRING_ID_64) ? pBaseLayer->FindStateByName(animStateId)
																		: pBaseLayer->CurrentState();

	return AnimStateLookupStateInfo<T>(pState, infoId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename T>
const T* LookupAnimStateInfo(const AnimStateInstance* pInst, StringId64 infoId)
{
	const DC::AnimState* pState = pInst ? pInst->GetState() : nullptr;
	if (!pState)
	{
		return nullptr;
	}

	return AnimStateLookupStateInfo<T>(pState, infoId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
#ifndef FINAL_BUILD
class AnimReloadPakManager	// Manages the life cycle of all animation reload packages
{
public:
	void Init();
	void FrameEnd();
	void AddPackage(const char *const pPackageName);

private:
	HashTable<Package*, U32F> m_pkgs;
};

extern AnimReloadPakManager g_animReloadPakMgr;
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
Vector GetAnimVelocityAtPhase(const ArtItemAnim* pAnim, float phase, U32F samplePadding = 1);

/// --------------------------------------------------------------------------------------------------------------- ///
const char* GetJointName(const ArtItemSkeleton* pSkel, U32F iJoint, const char* def = nullptr);

/// --------------------------------------------------------------------------------------------------------------- ///
bool ValidBitsDiffer(const ArtItemAnim* pAnimA, const ArtItemAnim* pAnimB);

/// --------------------------------------------------------------------------------------------------------------- ///
SegmentMask GetDependentSegmentMask(const ArtItemSkeleton* pSkel, int segmentIndex);
