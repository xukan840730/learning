/*
 * Copyright (c) 2003, 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

/// --------------------------------------------------------------------------------------------------------------- ///
namespace ndanim
{
	struct JointParams;
}

class ArtItemAnim;

extern I32 g_hackTopAnimatedCameraIndex;

/// --------------------------------------------------------------------------------------------------------------- ///
// Compressed Channel structures
const U16 kFlagCompressionUpright				= 1 << 0;
const U16 kFlagCompressionConstantScale			= 1 << 1;
const U16 kFlagCompressionConstantTranslation	= 1 << 2;
const U16 kFlagCompressionConstantRotation		= 1 << 3;
const U16 kFlagCompressionConstantYTranslation	= 1 << 4;
const U16 kFlagCompressionConstantScaleX		= 1 << 5;
const U16 kFlagCompressionConstantScaleY		= 1 << 6;
const U16 kFlagCompressionConstantScaleZ		= 1 << 7;
const U16 kFlagCompression32BitFloats			= 1 << 8;
const U16 kFlagFloatChannel						= 1 << 9;

/// --------------------------------------------------------------------------------------------------------------- ///
struct CompressedChannel
{
	U16 m_numSamples;
	U16 m_flags;
	U8* m_data;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct CompressedChannelList
{
	int m_numChannels;
	StringId64* m_channelNameIds;
	CompressedChannel** m_channels;
};

/// --------------------------------------------------------------------------------------------------------------- ///
const CompressedChannel* FindChannel(const ArtItemAnim* pAnim, StringId64 channelNameId);
U32 GetCompressedChannelDataSize(const CompressedChannel* pChannel);
void ReadFromCompressedChannel(const CompressedChannel* pChannel, U16 sampleIndex, ndanim::JointParams* pOutParams);
const ndanim::JointParams MirrorJointParamsX(const ndanim::JointParams& params);

F32 ReadFromCompressedFloatChannel(const CompressedChannel* pChannel, U16 sampleIndex);

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCameraCutInfo
{
	// INPUTS
	int	m_cameraIndex;

	// OUTPUTS
	bool m_didCameraCut;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(16) EvaluateChannelParams
{
	const ArtItemAnim* m_pAnim = nullptr;
	StringId64 m_channelNameId = INVALID_STRING_ID_64;
	float m_phase = 0.0f;
	AnimCameraCutInfo* m_pCameraCutInfo = nullptr;
	bool m_mirror = false;
	bool m_wantRawScale = false;
	bool m_disableRetargeting = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
#if !FINAL_BUILD
typedef void (*LiveUpdateEvaluateChannelCallBack)(StringId64 channelId,
												  const ArtItemAnim* pAnim,
												  U16 keyframe0,
												  U16 keyframe1,
												  float blendFactor,
												  ndanim::JointParams* pOutJointParams0,
												  ndanim::JointParams* pOutJointParams1);
extern LiveUpdateEvaluateChannelCallBack g_liveUpdateEvaluateChannelCallBack;

typedef void (*LiveUpdateEvaluateFloatChannelCallBack)(StringId64 channelId,
													   const ArtItemAnim* pAnim,
													   U16 keyframe0,
													   U16 keyframe1,
													   float blendFactor,
													   float* pOutJointParams0,
													   float* pOutJointParams1);
extern LiveUpdateEvaluateFloatChannelCallBack g_liveUpdateEvaluateFloatChannelCallBack;

#endif

/// --------------------------------------------------------------------------------------------------------------- ///
/// Find out where the action pack reference 'channelName' is in align-space for animation 'animName'.
/// --------------------------------------------------------------------------------------------------------------- ///
bool EvaluateChannelInAnim(SkeletonId skelId,
						   const EvaluateChannelParams* pEvaluateChannelParams,
						   ndanim::JointParams* pParamsOut);
void EvaluateCompressedChannel(const EvaluateChannelParams* pEvalData,
							   const CompressedChannel* pChannel,
							   ndanim::JointParams* pParamsOut);

bool EvaluateCompressedFloatChannel(const EvaluateChannelParams* pEvalData, float* pFloatOut);

StringId64 LookupMirroredChannelPair(StringId64 channelId);
Quat RotateSwappedChannelPair(Quat channelRot);