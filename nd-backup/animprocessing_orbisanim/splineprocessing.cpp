/* SCE CONFIDENTIAL
* $PSLibId$
* Copyright (C) 2015 Sony Computer Entertainment Inc.
* All Rights Reserved.
*/

#include "splineprocessing.h"
#include "animobjectrootanims.h"
#include "clipdatachannelgroup.h"

namespace OrbisAnim {
namespace Tools {
namespace AnimProcessing {

static IBitCompressedArray* BitCompressFloatSpline(SplineAnim const* spline, AnimChannelCompressionType compressionType, std::string const& name)
{
	IBitCompressedArray* pCompressedArray = NULL;
	switch (compressionType) {
	case kAcctFloatSplineUncompressed:
		pCompressedArray = new UncompressedFloatSpline(spline, false, name);
		break;
	default:
		IWARN("BitCompressFloatSpline() ignoring unsupported compression type %02x.\n", compressionType);
		break;
	}
	return pCompressedArray;
}

// this is a placeholder for compressing the spline knots
// for now we simply copy the splines for each bound joint or float anim

// FIXME - need to stop handling joints & floats in separate code blocks
//		 - needs sorting out higher up in the binding structure?

void BuildCompressedHermiteData(AnimationClipCompressedData& compressedData,	// [out] resultant compressed Hermite spline data
								AnimationBinding const& binding,				// [in] anim curve to joint/float mapping
								AnimationClipSourceData const& sourceData,		// [in] uncompressed Hermite spline data
								ClipCompressionDesc const& compression,			// [in] compression parameters
								AnimationHierarchyDesc const& hierarchyDesc)	// [in] hierarchy descriptor
{
	compressedData.Clear();	// FIXME - is this necessary or even correct (it won't delete the channels)?
	compressedData.m_hierarchyId = binding.m_hierarchyId;
	compressedData.m_pRootAnim = NULL;
	compressedData.m_flags = (sourceData.m_flags & ~kClipCompressionMask) | (compression.m_flags & kClipCompressionMask);
	compressedData.m_numFrames = (sourceData.m_flags & kClipLooping) ? sourceData.m_numFrames + 1 : sourceData.m_numFrames;
	if (sourceData.m_pRootAnim) {
		compressedData.m_pRootAnim = sourceData.m_pRootAnim->ConstructCopy();
	}

	size_t const numJointAnims = binding.m_jointAnimIdToAnimatedJointIndex.size();
	for (size_t animIndex = 0; animIndex < numJointAnims; animIndex++) {
		int jointIndex = binding.m_jointAnimIdToAnimatedJointIndex[animIndex];
		if (jointIndex < 0) {
			continue;	// this anim is not bound to any target joint
		}
		for (size_t c = 0; c < kNumJointChannels; c++) {
			ChannelType chanType = (ChannelType)c;
			SplineAnim const* splineAnim = sourceData.GetSplineAnim(chanType, animIndex);
			if (splineAnim) {
				AnimChannelCompressionType compressionType = compression.m_format[chanType][jointIndex].m_compressionType;
				IBitCompressedArray* compressedSpline = BitCompressFloatSpline(splineAnim, compressionType, hierarchyDesc.m_joints[jointIndex].m_name);
				if (compressedSpline) {
					AnimationClipCompressedData::AnimatedData compressedAnim = { compressedSpline, jointIndex };
					compressedData.m_anims[chanType].push_back(compressedAnim);
				}
			}
		}
	}
	// TODO - compress constant channels found during above loop

	size_t const numFloatAnims = binding.m_floatAnimIdToFloatId.size();
	size_t const numFloatChannels = binding.m_floatIdToFloatAnimId.size();
	for (size_t floatIndex = 0; floatIndex < numFloatChannels; floatIndex++) {
		size_t animIndex = binding.m_floatIdToFloatAnimId[floatIndex];
		if (animIndex >= numFloatAnims) {
			continue;
		}
		ChannelType chanType = kChannelTypeScalar;
		SplineAnim const* splineAnim = sourceData.GetSplineAnim(chanType, animIndex);
		if (splineAnim) {
			//AnimChannelCompressionType compressionType = compression.m_format[chanType][floatIndex].m_compressionType;
			AnimChannelCompressionType compressionType = kAcctFloatSplineUncompressed;
			IBitCompressedArray* compressedSpline = BitCompressFloatSpline(splineAnim, compressionType, hierarchyDesc.m_floatChannels[floatIndex].m_name);	
			if (compressedSpline) {
				AnimationClipCompressedData::AnimatedData compressedAnim = { compressedSpline, floatIndex };
				compressedData.m_anims[chanType].push_back(compressedAnim);
			}
		}
	}
	// TODO - compress constant channels found during above loop
}

void ChannelGroup::BuildBlockDataForSplineKeys()
{
	// placeholder
	// this method will eventually pack splines into groups of 4 for the vectorized runtime eval
	// or do nothing if the unpacked clip version is required
}

} // AnimProcessing
} // Tools
} // OrbisAnim
