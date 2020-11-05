/*
 * Copyright (c) 2003 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/util/color.h"
#include "corelib/util/timeframe.h"

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/util/maybe.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimControl;
class AnimOverlays;
class AnimStateInstance;
class ArtItemAnim;
class BoundFrame;
class Clock;
class JointCache;
struct BoundingData;

namespace DC
{
	struct AnimState;
	typedef U64 AnimStateFlag;
};

/// --------------------------------------------------------------------------------------------------------------- ///
enum class PhaseMatchDistMode
{
	k3d,
	kXz,
	kY,
	kProjected,
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct PhaseMatchParams
{
	Vector m_projectedBasis = kUnitZAxis;
	StringId64 m_apChannelId = SID("apReference");
	float* m_pBestDistOut = nullptr;
	float m_minPhase = 0.0f;
	float m_maxPhase = 1.0f;
	float m_minAdvanceFrames = 1.0f;
	PhaseMatchDistMode m_distMode = PhaseMatchDistMode::k3d;
	bool m_mirror = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
/// Find out the blended delta movement of the ApReference locator since last animation update.
/// --------------------------------------------------------------------------------------------------------------- ///
void GetAPDelta(const AnimControl* pAnimControl, StringId64 layerId, Locator& rawAnimApDelta);

float ComputePhaseToTravelDistance(const ArtItemAnim* pAnim,
								   float phase,
								   float distance,
								   float minAdvanceFrames,
								   float deltaTime,
								   bool slowOnly = false);

float ComputePhaseToTravelDistance(const ArtItemAnim* pAnim,
								   float phase,
								   float distance,
								   float minAdvanceFrames,
								   float deltaTime,
								   bool slowOnly,
								   AlignDistFunc distFunc);

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputePhaseToMatchDistance(SkeletonId skelId,
								  const ArtItemAnim* pAnim,
								  float targetDistance,
								  const PhaseMatchParams& params = PhaseMatchParams());

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputePhaseToMatchDistanceCached(SkeletonId skelId,
										const ArtItemAnim* pAnim,
										float targetDistance,
										const PhaseMatchParams& params = PhaseMatchParams());

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputePhaseToMatchApAlignDistance(SkeletonId skelId,
										 const ArtItemAnim* pAnim,
										 const Locator& apLoc,
										 float targetDistance,
										 const PhaseMatchParams& params = PhaseMatchParams());

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputePhaseToMatchDistanceFromEnd(const SkeletonId& skelId,
										 const ArtItemAnim* pAnim,
										 float targetDistance,
										 const PhaseMatchParams& params = PhaseMatchParams());

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputePhaseToMatchApAlign(SkeletonId skelId,
								 const ArtItemAnim* pAnim,
								 const Locator& desAlign,
								 const Locator& apRef,
								 const PhaseMatchParams& params = PhaseMatchParams());

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputePhaseToNoAlignMovement(const ArtItemAnim* pAnim, const PhaseMatchParams& params = PhaseMatchParams());

/// --------------------------------------------------------------------------------------------------------------- ///
float CalculateAnimPhaseFromStartTime(const ArtItemAnim* pArtItem,
									  const StringId64 animId,
									  const TimeFrame startTime,
									  const Clock* pClock);

float CalculateAnimDistance(const ArtItemAnim* pAnim, F32 startPhase = 0.0f, F32 endPhase = 1.0f);

void DeparentAllApReferences(AnimStateLayer* pStateLayer);

float FindEffPhase(const ArtItemAnim* pArtItemAnim,
				   StringId64 effNameId,
				   float defaultPhase = -1.0f,
				   bool* pFoundOut	  = nullptr);
float FindEffDuration(const ArtItemAnim* pArtItemAnim, StringId64 effStartId, StringId64 effStopId);

bool OverlayChangedAnimState(const AnimStateLayer* pLayer, const AnimOverlays* pOverlays);

bool GetRotatedApRefForEntry(SkeletonId skelId,
							 const ArtItemAnim* pAnim,
							 const Locator curLocWs,
							 const BoundFrame& defaultCoverApRef,
							 const Locator& defaultAlignWs,
							 Locator* pRotatedAlignWsOut,
							 BoundFrame* pRotatedApRefOut);

/// --------------------------------------------------------------------------------------------------------------- ///
void ComputeBoundsSimple(BoundingData* pOutputBoundingData,
						 const Locator& alignWs,
						 Vector_arg scale,
						 Vector_arg invScale,
						 const Locator* pJointLocsWs,
						 U32F numJoints,
						 I32F visSphereJointIndex,
						 const SMath::Vec4* pVisSphere,
						 I16 excludeJointIndex[2]);

/// --------------------------------------------------------------------------------------------------------------- ///
void ComputeBoundsSimpleAABB(BoundingData* pOutputBoundingData,
							 const Locator& alignWs,
							 Vector_arg scale,
							 Vector_arg invScale,
							 const Locator* pJointLocsWs,
							 U32F numJoints,
							 I32F boundingVolumeJointIndex,
							 const Aabb* pAabbOs,
							 float paddingRadius,
							 I16 excludeJointIndex[2]);

/// --------------------------------------------------------------------------------------------------------------- ///
extern ArtItemAnimHandle GetPhaseAnimFromStateId(const AnimControl* pAnimControl, StringId64 stateId);
extern CachedAnimLookup GetPhaseAnimLookupFromStateId(const AnimControl* pAnimControl,
													  StringId64 stateId,
													  CachedAnimLookup* pPrevCache = nullptr,
													  const DC::AnimState** pState = nullptr);

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimChannelLocatorBlender : public AnimStateLayer::InstanceBlender<Locator>
{
public:
	AnimChannelLocatorBlender(const StringId64 channelId,
							  Locator defaultLoc = Locator(kIdentity),
							  bool forceChannelBlending = false);

	Locator GetDefaultData() const override;
	bool GetDataForInstance(const AnimStateInstance* pInstance, Locator* pDataOut) override;
	Locator BlendData(const Locator& leftData,
					  const Locator& rightData,
					  float masterFade,
					  float animFade,
					  float motionFade) override;

	StringId64 m_channelId;
	Locator m_defaultLoc;
	bool m_forceChannelBlending;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimChannelDeltaBlender : public AnimStateLayer::InstanceBlender<Locator>
{
private:
	StringId64 m_channelId;

public:
	AnimChannelDeltaBlender(StringId64 channelId);

protected:
	virtual Locator GetDefaultData() const override;
	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, Locator* pDataOut) override;
	virtual Locator BlendData(const Locator& leftData,
							  const Locator& rightData,
							  float masterFade,
							  float animFade,
							  float motionFade) override;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimChannelFromApReferenceBlender : public AnimStateLayer::InstanceBlender<Maybe<Locator>>
{
public:
	AnimChannelFromApReferenceBlender(const StringId64 channelId);

	Maybe<Locator> GetDefaultData() const override;
	bool GetDataForInstance(const AnimStateInstance* pInstance, Maybe<Locator>* pDataOut) override;
	Maybe<Locator> BlendData(const Maybe<Locator>& leftData,
							 const Maybe<Locator>& rightData,
							 float masterFade,
							 float animFade,
							 float motionFade) override;

	StringId64 m_channelId = INVALID_STRING_ID_64;
	DC::AnimFlipMode m_flipMode = DC::kAnimFlipModeFromInstance;
	bool m_valid = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawAnimPose(const Locator& alignLoc, const ArtItemAnim* pAnim, float phase, bool mirrored, Color color);
