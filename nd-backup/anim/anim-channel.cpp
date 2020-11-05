/*
 * Copyright (c) 2003, 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-channel.h"

#include "corelib/util/float16.h"
#include "corelib/util/msg.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/debug-anim-channels.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/nd-config.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/profiling/profiling.h"

#include "gamelib/level/artitem.h"

/// --------------------------------------------------------------------------------------------------------------- ///
I32 g_hackTopAnimatedCameraIndex = 0;

#if !FINAL_BUILD
LiveUpdateEvaluateChannelCallBack g_liveUpdateEvaluateChannelCallBack = nullptr;
LiveUpdateEvaluateFloatChannelCallBack g_liveUpdateEvaluateFloatChannelCallBack = nullptr;
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
static inline F32 MakeF32FromF32(const F16 *pData)
{
	return *(const F32*)pData;	
}

/// --------------------------------------------------------------------------------------------------------------- ///
static inline VF32 MakeVec4FromVec416F(const F16* pData)
{
	__m128i f16s = _mm_loadu_si128((const __m128i*)pData);
	// Convert the 16-bit floats to 32bit
	VF32 u32s = _mm_cvtph_ps(f16s);
	return u32s;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 GetSize(const CompressedChannel* pChannel, bool firstFrame)
{
	U32 size = 0;
	const U16 flags = pChannel->m_flags;

	if (flags & kFlagFloatChannel)
	{
		return 2;
	}

	// size of quaternion
	if (flags & kFlagCompressionConstantRotation && !firstFrame)
	{
		// do nothing
	}
	else if (flags & kFlagCompressionUpright)
	{
		if (flags & kFlagCompression32BitFloats)
		{
			size += 4;
		}
		else
		{
			size += 2;
		}
	}
	else
	{
		if (flags & kFlagCompression32BitFloats)
		{
			size += 16;
		}
		else
		{
			size += 8;
		}
	}

	if (flags & kFlagCompressionConstantTranslation && !firstFrame)
	{
		// do nothing
	}
	else if (flags & kFlagCompressionConstantYTranslation && !firstFrame)
	{
		if (flags & kFlagCompression32BitFloats)
		{
			size += 8;
		}
		else
		{
			size += 4;
		}
	}
	else
	{
		if (flags & kFlagCompression32BitFloats)
		{
			size += 12;
		}
		else
		{
			// size of translation
			size += 6;
		}
	}

	// size of scale
	if (flags & kFlagCompressionConstantScale && !firstFrame)
	{
		// do nothing
	}
	else if (!firstFrame)
	{
		if (!(flags & kFlagCompressionConstantScaleX))
		{
			if (flags & kFlagCompression32BitFloats)
			{
				size += 4;
			}
			else
			{
				size += 2;
			}
		}

		if (!(flags & kFlagCompressionConstantScaleY))
		{
			if (flags & kFlagCompression32BitFloats)
			{
				size += 4;
			}
			else
			{
				size += 2;
			}
		}

		if (!(flags & kFlagCompressionConstantScaleZ))
		{
			if (flags & kFlagCompression32BitFloats)
			{
				size += 4;
			}
			else
			{
				size += 2;
			}
		}
	}
	else
	{
		if (flags & kFlagCompression32BitFloats)
		{
			size += 12;
		}
		else
		{
			size += 6;
		}
	}

	return size;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 GetCompressedChannelDataSize(const CompressedChannel* pChannel)
{
	return GetSize(pChannel, true) + GetSize(pChannel, false) * pChannel->m_numSamples;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ndanim::JointParams DecompressData(U16 flags,
										 const F16* pData,
										 U32F dataSize,
										 ndanim::JointParams* pFirstFrameParams = nullptr)
{
	const U32F numF16s = (dataSize >> 1);
	U32F dataOffset = 0;

	ndanim::JointParams jointParams;

	if (flags & kFlagFloatChannel)
	{
		ANIM_ASSERTF(false, ("Trying to decompress JointParams from float channel"));
		return jointParams;
	}

	// first the quaternion
	if (flags & kFlagCompressionConstantRotation && pFirstFrameParams != nullptr)
	{
		// return the first frame
		jointParams.m_quat = pFirstFrameParams->m_quat;
	}
	else if (flags & kFlagCompressionUpright)
	{
		if (flags & kFlagCompression32BitFloats)
		{
			const float rot = MakeF32FromF32(&pData[dataOffset]);
			dataOffset += 2;
			Vector dir = Vector(Cos(rot), 0.f, Sin(rot));
			jointParams.m_quat = QuatFromLookAt(dir, kUnitYAxis);
		}
		else
		{
			const float rot = MakeF32(pData[dataOffset++]);
			Vector dir = Vector(Cos(rot), 0.f, Sin(rot));
			jointParams.m_quat = QuatFromLookAt(dir, kUnitYAxis);
		}
	}
	else
	{
		if (flags & kFlagCompression32BitFloats)
		{
			jointParams.m_quat = Quat(Simd::ULoadVec((const float*)&pData[dataOffset]));
			dataOffset += 8;
		}
		else
		{
			jointParams.m_quat = Quat(MakeVec4FromVec416F(&pData[dataOffset]));
			dataOffset += 4;
		}
	}

	jointParams.m_quat = Normalize(jointParams.m_quat);

	if (flags & kFlagCompressionConstantTranslation && pFirstFrameParams != nullptr)
	{
		jointParams.m_trans = pFirstFrameParams->m_trans;
	}
	else if (flags & kFlagCompressionConstantYTranslation && pFirstFrameParams != nullptr)
	{
		if (flags & kFlagCompression32BitFloats)
		{
			const float x = MakeF32FromF32(&pData[dataOffset]);
			dataOffset += 2;
			const float z = MakeF32FromF32(&pData[dataOffset]);
			dataOffset += 2;
			jointParams.m_trans = Point(x, pFirstFrameParams->m_trans.Y(), z);
		}
		else
		{
			// next the translation
			const float x = MakeF32(pData[dataOffset++]);
			const float z = MakeF32(pData[dataOffset++]);
			jointParams.m_trans = Point(x, pFirstFrameParams->m_trans.Y(), z);
		}
	}
	else
	{
		if (flags & kFlagCompression32BitFloats)
		{
			VF32 xyz = Simd::ULoadVec((const float*)&pData[dataOffset]);
			SMATH_VEC_SET_W(xyz, Simd::MakeVec(1.f));
			dataOffset += 6;
			jointParams.m_trans = Point(xyz);
		}
		else
		{
			// next the translation
			VF32 vec = MakeVec4FromVec416F(&pData[dataOffset]);
			SMATH_VEC_SET_W(vec, Simd::MakeVec(1.f));
			dataOffset += 3;
			jointParams.m_trans = Point(vec);
		}
	}

	// now the scale
	if (flags & kFlagCompressionConstantScale && pFirstFrameParams != nullptr)
	{
		jointParams.m_scale = pFirstFrameParams->m_scale;
	}
	else if (pFirstFrameParams != nullptr)
	{
		float x, y, z;
		x = pFirstFrameParams->m_scale.X();
		y = pFirstFrameParams->m_scale.Y();
		z = pFirstFrameParams->m_scale.Z();

		if (!(flags & kFlagCompressionConstantScaleX))
		{
			if (flags & kFlagCompression32BitFloats)
			{
				x = MakeF32FromF32(&pData[dataOffset]);
				dataOffset += 2;
			}
			else
			{
				x = MakeF32(pData[dataOffset++]);
			}
		}

		if (!(flags & kFlagCompressionConstantScaleY))
		{
			if (flags & kFlagCompression32BitFloats)
			{
				y = MakeF32FromF32(&pData[dataOffset]);
				dataOffset += 2;
			}
			else
			{
				y = MakeF32(pData[dataOffset++]);
			}
		}

		if (!(flags & kFlagCompressionConstantScaleZ))
		{
			if (flags & kFlagCompression32BitFloats)
			{
				z = MakeF32FromF32(&pData[dataOffset]);
				dataOffset += 2;
			}
			else
			{
				z = MakeF32(pData[dataOffset++]);
			}
		}

		jointParams.m_scale = Vector(x, y, z);
	}
	else
	{
		if (flags & kFlagCompression32BitFloats)
		{
			VF32 xyz = Simd::ULoadVec((const float *)&pData[dataOffset]);
			SMATH_VEC_SET_W(xyz, Simd::MakeVec(0.f));
			dataOffset += 6;
			jointParams.m_scale = Vector(xyz);
		}
		else
		{
			VF32 vec = MakeVec4FromVec416F(&pData[dataOffset]);
			SMATH_VEC_SET_W(vec, Simd::MakeVec(0.f));
			dataOffset += 3;

			jointParams.m_scale = Vector(vec);
		}
	}

	ANIM_ASSERT(dataOffset <= numF16s);

	return jointParams;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float DecompressFloatData(U16 flags, const F16* pData, U32F dataSize)
{
	const U32F numF16s = (dataSize >> 1);
	U32F dataOffset = 0;

	float value = 0.0f;

	if (!(flags & kFlagFloatChannel))
	{
		ANIM_ASSERTF(false, ("Trying to decompress float data from joint channel"));
		return value;
	}

	value = MakeF32(pData[dataOffset++]);
	return value;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ReadFromCompressedChannel(const CompressedChannel* pChannel, U16 sampleIndex, ndanim::JointParams* pOutParams)
{
	//PROFILE(Animation, ReadFromCompressedChannel);
	
	const U8* pChannelData = pChannel->m_data;
	const U16 flags = pChannel->m_flags;

	// Read in the whole keyframe
	const U32F firstFrameSampleSize = GetSize(pChannel, true);
	const U32F sampleSize = GetSize(pChannel, false);
	ndanim::JointParams firstFrameParams = DecompressData(flags, (const F16*)pChannelData, firstFrameSampleSize);

	if (sampleIndex == 0 || sampleSize == 0)
	{
		*pOutParams = firstFrameParams;
	}
	else
	{
		const U32F offset = firstFrameSampleSize + sampleSize * (sampleIndex - 1);
		const U8* bufferStart = pChannelData + offset;
		*pOutParams = DecompressData(flags, (const F16*)bufferStart, sampleSize, &firstFrameParams);
	}
}	

/// --------------------------------------------------------------------------------------------------------------- ///
F32 ReadFromCompressedFloatChannel(const CompressedChannel* pChannel, U16 sampleIndex)
{
	//PROFILE(Animation, ReadFromCompressedFloatChannel);

	const U8* pChannelData = pChannel->m_data;
	const U16 flags = pChannel->m_flags;

	// Read in the whole keyframe
	const U32F firstFrameSampleSize = GetSize(pChannel, true);
	const U32F sampleSize = GetSize(pChannel, false);
	F32 firstFloat = DecompressFloatData(flags, (const F16*)pChannelData, firstFrameSampleSize);

	if (sampleIndex == 0 || sampleSize == 0)
	{
		return firstFloat;
	}
	else
	{
		const U32F offset = firstFrameSampleSize + sampleSize * (sampleIndex - 1);
		const U8* bufferStart = pChannelData + offset;
		return DecompressFloatData(flags, (const F16*)bufferStart, sampleSize);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const CompressedChannel* FindChannel(const ArtItemAnim* pAnim, StringId64 channelNameId)
{
	if (!pAnim)
		return nullptr;

	const CompressedChannelList* pChannelList = pAnim->m_pCompressedChannelList;
	ANIM_ASSERT(pChannelList);

	const U32F numChannels = pChannelList->m_numChannels;
	const StringId64* pChannelNames = pChannelList->m_channelNameIds;
	const CompressedChannel* const* ppChannels = pChannelList->m_channels;

	for (U32F i = 0; i < numChannels; ++i)
	{
		if (pChannelNames[i] == channelNameId)
		{
			const CompressedChannel* pChannel = ppChannels[i];
			return pChannel;
		}
	}

#if !FINAL_BUILD
	if (DebugAnimChannels::Get())
	{
		const CompressedChannel* pChannel = DebugAnimChannels::Get()->GetChannel(pAnim, channelNameId);
		return pChannel;
	}	
#endif

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const ndanim::JointParams MirrorJointParamsX(const ndanim::JointParams& params)
{
	ndanim::JointParams ret = params;
	ret.m_trans.SetX(-1.0f * ret.m_trans.X());

	// mirror about X, this is NOT a conjugate, although conjugates work if Y is up.
	ret.m_quat = Quat(-ret.m_quat.X(), ret.m_quat.Y(), ret.m_quat.Z(), -ret.m_quat.W());
	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Left.Transform(Right);
void TransformJointParams(ndanim::JointParams* pOut,
						  const ndanim::JointParams* pAlign,
						  const ndanim::JointParams* pOther,
						  bool wantRawScale)
{
	pOut->m_quat = pAlign->m_quat * pOther->m_quat;
	pOut->m_trans = pAlign->m_trans + Rotate(pAlign->m_quat, pOther->m_trans - Point(kOrigin));
	pOut->m_scale = (wantRawScale) ? pOther->m_scale : (pAlign->m_scale * pOther->m_scale);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Left.UnTransform(Right);
void UnTransformJointParams(ndanim::JointParams* pOut,
							const ndanim::JointParams* pAlign,
							const ndanim::JointParams* pOther,
							bool wantRawScale)
{
	pOut->m_quat = Conjugate(pAlign->m_quat) * pOther->m_quat;
	pOut->m_trans = SMath::kOrigin + Unrotate(pAlign->m_quat, pOther->m_trans - pAlign->m_trans);
	pOut->m_scale = (wantRawScale || !AllComponentsGreaterThan(pOther->m_scale, Vector(0,0,0))) ? pOther->m_scale : (pAlign->m_scale / pOther->m_scale);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BlendJointParams(ndanim::JointParams* pOut,
					  const ndanim::JointParams* pLeft,
					  const ndanim::JointParams* pRight,
					  Scalar_arg blendFactor)
{
	pOut->m_quat = Slerp(pLeft->m_quat, pRight->m_quat, blendFactor);
	pOut->m_trans = Lerp(pLeft->m_trans, pRight->m_trans, blendFactor);
	pOut->m_scale = Lerp(pLeft->m_scale, pRight->m_scale, blendFactor);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static inline bool ShouldCameraCut(VF32 cutChannel, VF32 prevFrameVal, VF32 nextFrameVal)
{
	// In Maya, the animator uses a "step key" to change the value of the scaleZ channel from
	// 0.0 to 1.0, or vice-versa, to indicate that a cut should occur.  Step keys are
	// "instantaneous", so if a key is set on frame T, then the transform sampled by the exporter
	// on frame T will be the *new* value set by the step key.  This is what two successive cuts
	// would look like in Maya:
	// __________                ____________
	//           |              |
	//           |______________|
	//  0  1  2  3  4  5  6  7  8  9  ...
	//          cut            cut
	//
	// and here's how we interpolate these samples:
	// ________                 _____________
	//         \               /
	//          \_____________/
	//  0  1  2  3  4  5  6  7  8  9  ...
	//          cut            cut
	//
	// In other words, the value will settle on its final value of either 1.0 or 0.0 on the frame
	// at which we should cut to the new camera transform.
	//
	// To handle these camera cuts properly, the simple rule is:
	//		NEVER SAMPLE THE CAMERA, ALIGN or APREFERENCE ANYWHERE WITHIN THE RAMP.
	// If the phase lies within the ramp, move it up to the next frame with scaleZ = 1.0 or 0.0.

	static const VF32 kCamCutEpsilon = Simd::MakeVec(0.00001f);
	static const VF32 kCamCutOneMinusEpsilon = Simd::MakeVec(1.0f - 0.00001f);
	static const VF32 kCamCutDeltaEpsilon = Simd::MakeVec(0.4f);

	VB32 notNearZero = Simd::CompareGT(cutChannel, kCamCutEpsilon);
	VB32 notNearOne = Simd::CompareLT(cutChannel, kCamCutOneMinusEpsilon);
	VB32 shouldCutOnInterp = Simd::And(notNearZero, notNearOne);
	VF32 delta = Simd::Sub(prevFrameVal, nextFrameVal);
	delta = Simd::Abs(delta);
	VB32 shouldCutOnDelta = Simd::CompareGT(delta, kCamCutDeltaEpsilon);
	VB32 shouldCut(Simd::And(shouldCutOnInterp, shouldCutOnDelta));

	return !Simd::Equal(shouldCut, Simd::GetMaskAllZero());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void EvaluateCompressedChannel(const EvaluateChannelParams* pEvalData,
							   const CompressedChannel* pChannel,
							   ndanim::JointParams* pParamsOut)
{
	ANIM_ASSERT(pEvalData->m_phase >= 0.0f && pEvalData->m_phase <= 1.0f);

	if (pEvalData->m_pCameraCutInfo)
		pEvalData->m_pCameraCutInfo->m_didCameraCut = false;

	const ArtItemAnim* pAnim = pEvalData->m_pAnim;
	const char* animName	 = pAnim->GetName();

	const CompressedChannel* pAlignChannel = FindChannel(pAnim, SID("align"));

	// Somehow there was an anim with out an align channel crashing the game.  Let's return identity in this case.
	if (pAlignChannel == nullptr)
	{
		MsgErr("No align channel in anim: %s\n", animName);
		pParamsOut->m_quat	= Quat(kIdentity);
		pParamsOut->m_trans = Point(kZero);
		pParamsOut->m_scale = Vector(Scalar(1.0f));
		return;
	}
	const CompressedChannel* pOtherChannel	 = (pEvalData->m_channelNameId == SID("align")) ? pAlignChannel : pChannel;
	const CompressedChannel* pAlignChannelLs = pAlignChannel;
	ANIM_ASSERT(pAlignChannelLs);

	// determine keyframe interval (with no clamping or wrapping yet)
	const U16 numSamples = pAlignChannelLs->m_numSamples;
	const U16 lastSample = numSamples - 1;

	// The time we want to sample the custom channel is independent of the duration of the animation.
	// All we care about is the number of samples and the phase...
	const float t = pEvalData->m_phase * lastSample;

	const float keyframe0 = floorf(t); // lower keyframe
	const float keyframe1 = ceilf(t);  // upper keyframe

	// Clamp to within range
	// 	keyframe0 %= numSamples;
	// 	keyframe1 %= numSamples;

	// compute interpolation parameter between 0.0 and 1.0
	Scalar blendFactor = t - keyframe0;

	// handle camera cuts
	bool shouldCameraCut = false;
	if (!g_animOptions.m_disableCameraCuts)
	{
		const StringId64 apReferenceCamera
			= pEvalData->m_pCameraCutInfo
				  ? StringId64ConcatInteger(SID("apReference-camera"), pEvalData->m_pCameraCutInfo->m_cameraIndex + 1)
				  : SID("apReference-camera1"); // cam indices are stored 0-based, but the ap's are named 1-based

		const CompressedChannel* pCameraChannel = FindChannel(pAnim, apReferenceCamera);
		if (pCameraChannel)
		{
			ndanim::JointParams cameraChannelJointParams[2];
			ReadFromCompressedChannel(pCameraChannel, static_cast<U16>(keyframe0), &cameraChannelJointParams[0]);
			ReadFromCompressedChannel(pCameraChannel, static_cast<U16>(keyframe1), &cameraChannelJointParams[1]);

			Scalar interpScaleZ;
			interpScaleZ = Lerp(cameraChannelJointParams[0].m_scale.Z(),
								cameraChannelJointParams[1].m_scale.Z(),
								blendFactor);

			shouldCameraCut = ShouldCameraCut(interpScaleZ.QuadwordValue(),
											  cameraChannelJointParams[0].m_scale.Z().QuadwordValue(),
											  cameraChannelJointParams[1].m_scale.Z().QuadwordValue());
			if (shouldCameraCut)
			{
				// We need to cut the camera, and all other "custom" channels.
				// Bump the blend factor up to 100% of keyframe1.
				if (!g_animOptions.m_onlyCutCamera1 || (pEvalData->m_channelNameId == apReferenceCamera))
				{
					blendFactor = Scalar(1.0f);

					if (pEvalData->m_pCameraCutInfo)
						pEvalData->m_pCameraCutInfo->m_didCameraCut = true;

#if !defined(NDI_ARCH_SPU) && !FINAL_BUILD
					if (g_animOptions.m_debugCameraCutsSuperVerbose)
					{
						MsgCinematic("%05u: CUT detected in anim '%s' [EvaluateCompressedChannel()] channel '%s'\n"
									 "       curFrame (t) = %.1f  phase = %.4f  keyframes = [%.1f, %.1f]  numSamples = %u\n",
									 (U32)EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused,
									 pAnim->GetName(),
									 DevKitOnly_StringIdToStringOrHex(pEvalData->m_channelNameId),
									 t,
									 pEvalData->m_phase,
									 keyframe0,
									 keyframe1,
									 (U32)numSamples);
					}
#endif
				}

				// Double-check that clamping to the next sample is sufficient -- it *should* be in all cases.
				{
					const bool checkCameraCut = ShouldCameraCut(cameraChannelJointParams[1].m_scale.Z().QuadwordValue(),
																cameraChannelJointParams[1].m_scale.Z().QuadwordValue(),
																cameraChannelJointParams[1].m_scale.Z().QuadwordValue());
					if (checkCameraCut)
					{
						MsgCon("CAMERA CUT DONE INCORRECTLY IN ANIMATION '%s' @ FRAME %0.2f -> %0.2f\n",
							   animName,
							   keyframe0,
							   keyframe1);
					}
				}
			}

			// if (g_animOptions.m_debugCameraCutsSuperVerbose && channelNameId == apReferenceCamera
			//&&  !g_ndConfig.m_pDMenuMgr->IsProgPaused() && !g_ndConfig.m_pDMenuMgr->IsProgPausedStep())
			//{
			//	MsgCinematic("ShouldCamCut: %4g-%4g: l=%9g i=%9g u=%9g (%s) %s\n",
			//		keyframe0, keyframe1,
			//		(F32)cameraChannelJointParams[0].m_scale.Z(), (F32)interpScaleZ, (F32)cameraChannelJointParams[1].m_scale.Z(),
			//		animNameLs, (shouldCameraCut ? "CUT!" : ""));
			//}
		}
	}

	// We always need the align channel
	ndanim::JointParams alignChannelJointParams[2];
	ReadFromCompressedChannel(pAlignChannel, static_cast<U16>(keyframe0), &alignChannelJointParams[0]);
	ReadFromCompressedChannel(pAlignChannel, static_cast<U16>(keyframe1), &alignChannelJointParams[1]);

#if !FINAL_BUILD
	if (UNLIKELY(g_liveUpdateEvaluateChannelCallBack != nullptr))
	{
		g_liveUpdateEvaluateChannelCallBack(SID("align"),
											pAnim,
											static_cast<U16>(keyframe0),
											static_cast<U16>(keyframe1),
											(float)(blendFactor),
											&alignChannelJointParams[0],
											&alignChannelJointParams[1]);
	}
#endif
	ANIM_ASSERTF(IsNormal(alignChannelJointParams[0].m_quat), ("'%s' @ %0.1f", pAnim->GetName(), keyframe0));
	ANIM_ASSERTF(IsNormal(alignChannelJointParams[1].m_quat), ("'%s' @ %0.1f", pAnim->GetName(), keyframe1));

	if (pEvalData->m_channelNameId == SID("align"))
	{
		// interpolate joint parameters
		BlendJointParams(pParamsOut, &alignChannelJointParams[0], &alignChannelJointParams[1], blendFactor);

		ANIM_ASSERT(IsNormal(pParamsOut->m_quat));
		ANIM_ASSERT(Length(pParamsOut->m_trans) < 100000.0f);
	}
	else
	{
		ndanim::JointParams otherChannelJointParams[2];
		ReadFromCompressedChannel(pOtherChannel, static_cast<U16>(keyframe0), &otherChannelJointParams[0]);
		ReadFromCompressedChannel(pOtherChannel, static_cast<U16>(keyframe1), &otherChannelJointParams[1]);

#if !FINAL_BUILD
		if (UNLIKELY(g_liveUpdateEvaluateChannelCallBack != nullptr))
			g_liveUpdateEvaluateChannelCallBack(pEvalData->m_channelNameId,
												pAnim,
												static_cast<U16>(keyframe0),
												static_cast<U16>(keyframe1),
												(float)(blendFactor),
												&otherChannelJointParams[0],
												&otherChannelJointParams[1]);
#endif
		// Transform the 'other channel' params into the space of the align channel before blending
		// as the 'other channel' does not contain all information for a proper blending
		ndanim::JointParams combinedChannelJointParams[2];
		TransformJointParams(&combinedChannelJointParams[0],
							 &alignChannelJointParams[0],
							 &otherChannelJointParams[0],
							 pEvalData->m_wantRawScale);
		TransformJointParams(&combinedChannelJointParams[1],
							 &alignChannelJointParams[1],
							 &otherChannelJointParams[1],
							 pEvalData->m_wantRawScale);

		// interpolate joint parameters in align space
		ndanim::JointParams interpOtherJointParams;
		BlendJointParams(&interpOtherJointParams,
						 &combinedChannelJointParams[0],
						 &combinedChannelJointParams[1],
						 blendFactor);

		// Also interpolate the align because we need to untransform the interpolated result because the 'other channel' was requested.
		ndanim::JointParams interpAlignJointParams;
		BlendJointParams(&interpAlignJointParams, &alignChannelJointParams[0], &alignChannelJointParams[1], blendFactor);

		// Slerping the quats can give us enough of a rounding error that 5+ meter translations start to wobble
		// a couple of millimeters which results in camera jitter
		interpAlignJointParams.m_quat = Normalize(interpAlignJointParams.m_quat);

		// Now put the 'other channel' back in it's correct space.
		UnTransformJointParams(pParamsOut, &interpAlignJointParams, &interpOtherJointParams, pEvalData->m_wantRawScale);

		ANIM_ASSERT(IsNormal(pParamsOut->m_quat));
		ANIM_ASSERT(Length(pParamsOut->m_trans) < 100000.0f);
		ANIM_ASSERT(IsFinite(pParamsOut->m_scale));
	}

	// Mirror if needed...
	if (pEvalData->m_mirror)
		*pParamsOut = MirrorJointParamsX(*pParamsOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// Get the value of a particular custom channel from an animation at phase 'phase'
/// --------------------------------------------------------------------------------------------------------------- ///
bool EvaluateChannelInAnim(SkeletonId skelId, const EvaluateChannelParams* pEvalData, ndanim::JointParams* pParamsOutLs)
{
	PROFILE(Animation, EvaluateChannelInAnim);
	ANIM_ASSERT(pEvalData->m_pAnim);
	ANIM_ASSERT(pEvalData->m_channelNameId != INVALID_STRING_ID_64);
	ANIM_ASSERTF(pEvalData->m_phase >= 0.0f && pEvalData->m_phase <= 1.0f, ("Invalid evaluation phase %f", pEvalData->m_phase));
	ANIM_ASSERT(pParamsOutLs);

	const ArtItemAnim* pAnim = pEvalData->m_pAnim;

	const CompressedChannel* pChan = FindChannel(pAnim, pEvalData->m_channelNameId);
	if (!pChan || pChan->m_flags & kFlagFloatChannel)
	{
		// Reset for sane value if something goes wrong...
		pParamsOutLs->m_scale = Vector(Scalar(1.0f));
		pParamsOutLs->m_quat = Quat(kIdentity);
		pParamsOutLs->m_trans = Point(kZero);
		return false;
	}

	EvaluateCompressedChannel(pEvalData, pChan, pParamsOutLs);

	// should we scale, is this remapped?
	if (pAnim->m_skelID != skelId && skelId != INVALID_SKELETON_ID && !pEvalData->m_disableRetargeting)
	{
		// scale the translation
		const SkelTable::RetargetEntry* pRetargetEntry = SkelTable::LookupRetarget(pAnim->m_skelID, skelId);
		if (pRetargetEntry != nullptr && !pRetargetEntry->m_disabled)
		{
			const float retargetScale = pRetargetEntry->m_scale;

			Vector deltaTrans = pParamsOutLs->m_trans - Point(kZero);
			pParamsOutLs->m_trans = Point(kZero) + deltaTrans * retargetScale;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool EvaluateCompressedFloatChannel(const EvaluateChannelParams* pEvalData, float* pFloatOut)
{
	ANIM_ASSERT(pEvalData);
	ANIM_ASSERT(pFloatOut);
	ANIM_ASSERT(pEvalData->m_phase >= 0.0f && pEvalData->m_phase <= 1.0f);

	if (pEvalData->m_pCameraCutInfo)
		pEvalData->m_pCameraCutInfo->m_didCameraCut = false;

	const ArtItemAnim* pAnim = pEvalData->m_pAnim;
	const char* animName	 = pAnim->GetName();

	const CompressedChannel* pChannel = FindChannel(pAnim, pEvalData->m_channelNameId);
	if (!pChannel)
	{
		*pFloatOut = 0.0f;
		return false;
	}

	// determine keyframe interval (with no clamping or wrapping yet)
	const U16 numSamples = pChannel->m_numSamples;
	const U16 lastSample = numSamples - 1;

	// The time we want to sample the custom channel is independent of the duration of the animation.
	// All we care about is the number of samples and the phase...
	const float t = pEvalData->m_phase * lastSample;

	const float keyframe0 = floorf(t); // lower keyframe
	const float keyframe1 = ceilf(t);  // upper keyframe

	// compute interpolation parameter between 0.0 and 1.0
	Scalar blendFactor = t - keyframe0;

	F32 values[2];
	values[0] = ReadFromCompressedFloatChannel(pChannel, static_cast<U16>(keyframe0));
	values[1] = ReadFromCompressedFloatChannel(pChannel, static_cast<U16>(keyframe1));

#if !FINAL_BUILD
	if (UNLIKELY(g_liveUpdateEvaluateFloatChannelCallBack != nullptr))
	{
		g_liveUpdateEvaluateFloatChannelCallBack(pEvalData->m_channelNameId,
												 pAnim,
												 static_cast<U16>(keyframe0),
												 static_cast<U16>(keyframe1),
												 (float)(blendFactor),
												 &values[0],
												 &values[1]);
	}
#endif

	*pFloatOut = Lerp(values[0], values[1], (F32)blendFactor);
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 LookupMirroredChannelPair(StringId64 channelId)
{
	switch (channelId.GetValue())
	{
	case SID_VAL("lAnkle"): return SID("rAnkle");
	case SID_VAL("rAnkle"): return SID("lAnkle");
	case SID_VAL("lWrist"): return SID("rWrist");
	case SID_VAL("rWrist"): return SID("lWrist");
	case SID_VAL("apReference-hand-l"): return SID("apReference-hand-r");
	case SID_VAL("apReference-hand-r"): return SID("apReference-hand-l");
	case SID_VAL("apReference-hand-l-prop"): return SID("apReference-hand-r-prop");
	case SID_VAL("apReference-hand-r-prop"): return SID("apReference-hand-l-prop");
	case SID_VAL("apReference-foot-l"): return SID("apReference-foot-r");
	case SID_VAL("apReference-foot-r"): return SID("apReference-foot-l");
	case SID_VAL("apReference-ik-contact-HL"): return SID("apReference-ik-contact-HR");
	case SID_VAL("apReference-ik-contact-HR"): return SID("apReference-ik-contact-HL");
	case SID_VAL("apReference-ik-contact-FL"): return SID("apReference-ik-contact-FR");
	case SID_VAL("apReference-ik-contact-FR"): return SID("apReference-ik-contact-FL");
	}

	return channelId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Quat RotateSwappedChannelPair(Quat channelRot)
{
	// Does a 180 rotation around the local X axis
	return Quat(channelRot.W(), channelRot.Z(), -channelRot.Y(), -channelRot.X());
}
