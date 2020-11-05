
#include "animkeyextractors.h"

namespace OrbisAnim {
namespace Tools {
namespace AnimProcessing {

UniformKeyExtractor::UniformKeyExtractor(UniformKeyExtractor const& e)
	: m_sourceData(e.m_sourceData)
	, m_numOutputFrames(e.m_numOutputFrames)
{
}

UniformKeyExtractor::UniformKeyExtractor(AnimationClipSourceData const& sourceData)
	: m_sourceData(sourceData)
{
	m_numOutputFrames = m_sourceData.m_numFrames;
	if (m_sourceData.m_flags & kClipLooping) {
		++m_numOutputFrames;
	}
}

void UniformKeyExtractor::GetScaleValues(unsigned iJointAnimIndex, Vec3Array& values) const
{
	SampledAnim const& sourceValues = *m_sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex);
	for (unsigned iFrame = 0; iFrame < m_sourceData.m_numFrames; ++iFrame) {
		values.push_back(sourceValues[iFrame]);
	}
	if (m_sourceData.m_flags & kClipLooping) {
		values.push_back(sourceValues[0]);
	}
}

void UniformKeyExtractor::GetRotationValues(unsigned iJointAnimIndex, QuatArray& values) const
{
	SampledAnim const& sourceValues = *m_sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex);
	for (unsigned iFrame = 0; iFrame < m_sourceData.m_numFrames; ++iFrame) {
		values.push_back(sourceValues[iFrame]);
	}
	if (m_sourceData.m_flags & kClipLooping) {
		values.push_back(sourceValues[0]);
	}
}

void UniformKeyExtractor::GetTranslationValues(unsigned iJointAnimIndex, Vec3Array& values) const
{
	SampledAnim const& sourceValues = *m_sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex);
	for (unsigned iFrame = 0; iFrame < m_sourceData.m_numFrames; ++iFrame) {
		values.push_back(sourceValues[iFrame]);
	}
	if (m_sourceData.m_flags & kClipLooping) {
		values.push_back(sourceValues[0]);
	}
}

void UniformKeyExtractor::GetFloatValues(unsigned iFloatAnimIndex, std::vector<float>& values) const
{
	SampledAnim const& sourceValues = *m_sourceData.GetSampledAnim(kChannelTypeScalar, iFloatAnimIndex);
	for (unsigned iFrame = 0; iFrame < m_sourceData.m_numFrames; ++iFrame) {
		values.push_back(sourceValues[iFrame]);
	}
	if (m_sourceData.m_flags & kClipLooping) {
		values.push_back(sourceValues[0]);
	}
}

void UniformKeyExtractor::GetScaleValues(unsigned iJointAnimIndex, Vec3Array& values, FrameArray& frames) const
{
	GetScaleValues(iJointAnimIndex, values);
	GetFrameArray(frames);
}

void UniformKeyExtractor::GetRotationValues(unsigned iJointAnimIndex, QuatArray& values, FrameArray& frames) const
{
	GetRotationValues(iJointAnimIndex, values);
	GetFrameArray(frames);
}

void UniformKeyExtractor::GetTranslationValues(unsigned iJointAnimIndex, Vec3Array& values, FrameArray& frames) const
{
	GetTranslationValues(iJointAnimIndex, values);
	GetFrameArray(frames);
}

void UniformKeyExtractor::GetFloatValues(unsigned iFloatAnimIndex, std::vector<float>& values, FrameArray& frames) const
{
	GetFloatValues(iFloatAnimIndex, values);
	GetFrameArray(frames);
}

void UniformKeyExtractor::GetFrameArray(FrameArray& frames) const
{
	for (unsigned iFrame = 0; iFrame < m_numOutputFrames; ++iFrame) {
		frames.push_back((U16)iFrame);
	}
}

UniformKeyExtractor& UniformKeyExtractor::operator=(const UniformKeyExtractor&) 
{ 
	ITASSERT(0); 
	return *this; 
}

//-----------------------------------------------------------------------------

SharedKeyExtractor::SharedKeyExtractor(SharedKeyExtractor const& e)
	: m_sourceData(e.m_sourceData)
	, m_sharedKeys(e.m_sharedKeys)
	, m_numOutputFrames(e.m_numOutputFrames)
	, m_iLastFrame(e.m_iLastFrame)
{
}

SharedKeyExtractor::SharedKeyExtractor(AnimationClipSourceData const& sourceData, FrameArray const& sharedKeys)
	: m_sourceData(sourceData)
	, m_sharedKeys(sharedKeys)
{
	m_iLastFrame = m_sourceData.m_numFrames - 1;
	m_numOutputFrames = m_sourceData.m_numFrames;
	if (m_sourceData.m_flags & kClipLooping) {
		m_iLastFrame = 0;
		++m_numOutputFrames;
	}
	ITASSERT(m_sharedKeys[0] == 0 && m_sharedKeys[m_sharedKeys.size() - 1] == m_numOutputFrames - 1);
}

void SharedKeyExtractor::GetScaleValues(unsigned iJointAnimIndex, Vec3Array& values) const
{
	SampledAnim const& sourceValues = *m_sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex);
	for (unsigned iKey = 0; iKey < m_sharedKeys.size() - 1; ++iKey) {
		values.push_back(sourceValues[m_sharedKeys[iKey]]);
	}
	values.push_back(sourceValues[m_iLastFrame]);
}

void SharedKeyExtractor::GetRotationValues(unsigned iJointAnimIndex, QuatArray& values) const
{
	SampledAnim const& sourceValues = *m_sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex);
	for (unsigned iKey = 0; iKey < m_sharedKeys.size() - 1; ++iKey) {
		values.push_back(sourceValues[m_sharedKeys[iKey]]);
	}
	values.push_back(sourceValues[m_iLastFrame]);
}

void SharedKeyExtractor::GetTranslationValues(unsigned iJointAnimIndex, Vec3Array& values) const
{
	SampledAnim const& sourceValues = *m_sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex);
	for (unsigned iKey = 0; iKey < m_sharedKeys.size() - 1; ++iKey) {
		values.push_back(sourceValues[m_sharedKeys[iKey]]);
	}
	values.push_back(sourceValues[m_iLastFrame]);
}

void SharedKeyExtractor::GetFloatValues(unsigned iFloatAnimIndex, std::vector<float>& values) const
{
	SampledAnim const& sourceValues = *m_sourceData.GetSampledAnim(kChannelTypeScalar, iFloatAnimIndex);
	for (unsigned iKey = 0; iKey < m_sharedKeys.size() - 1; ++iKey) {
		values.push_back(sourceValues[m_sharedKeys[iKey]]);
	}
	values.push_back(sourceValues[m_iLastFrame]);
}

void SharedKeyExtractor::GetScaleValues(unsigned iJointAnimIndex, Vec3Array& values, FrameArray& frames) const
{
	GetScaleValues(iJointAnimIndex, values);
	GetFrameArray(frames);
}

void SharedKeyExtractor::GetRotationValues(unsigned iJointAnimIndex, QuatArray& values, FrameArray& frames) const
{
	GetRotationValues(iJointAnimIndex, values);
	GetFrameArray(frames);
}

void SharedKeyExtractor::GetTranslationValues(unsigned iJointAnimIndex, Vec3Array& values, FrameArray& frames) const
{
	GetTranslationValues(iJointAnimIndex, values);
	GetFrameArray(frames);
}

void SharedKeyExtractor::GetFloatValues(unsigned iFloatAnimIndex, std::vector<float>& values, FrameArray& frames) const
{
	GetFloatValues(iFloatAnimIndex, values);
	GetFrameArray(frames);
}

void SharedKeyExtractor::GetFrameArray(FrameArray& frames) const
{
	frames.insert(frames.end(), m_sharedKeys.begin(), m_sharedKeys.end());
}

SharedKeyExtractor& SharedKeyExtractor::operator=(const SharedKeyExtractor&)
{ 
	ITASSERT(0); 
	return *this; 
}

//-----------------------------------------------------------------------------

UnsharedKeyExtractor::UnsharedKeyExtractor(UnsharedKeyExtractor const& e)
	: m_sourceData(e.m_sourceData)
	, m_jointAnimIdToBlockArray(e.m_jointAnimIdToBlockArray)
	, m_floatAnimIdToBlockArray(e.m_floatAnimIdToBlockArray)
	, m_jointAnimIdToLocalIndex(e.m_jointAnimIdToLocalIndex)
	, m_floatAnimIdToLocalIndex(e.m_floatAnimIdToLocalIndex)
	, m_iLastFrame(e.m_iLastFrame)
{
}

UnsharedKeyExtractor::UnsharedKeyExtractor(AnimationHierarchyDesc const& hierarchyDesc,
										   AnimationBinding const& binding,
										   AnimationClipSourceData const& sourceData,
										   AnimationClipUnsharedKeyFrameBlockDesc const& unsharedKeys)
	: m_sourceData(sourceData)
{
	m_iLastFrame = (m_sourceData.m_flags & kClipLooping) ? 0 : m_sourceData.m_numFrames - 1;
	m_jointAnimIdToBlockArray.resize(binding.m_jointAnimIdToJointId.size(), NULL);
	m_floatAnimIdToBlockArray.resize(binding.m_floatAnimIdToFloatId.size(), NULL);
	m_jointAnimIdToLocalIndex.resize(binding.m_jointAnimIdToJointId.size(), (unsigned)-1);
	m_floatAnimIdToLocalIndex.resize(binding.m_floatAnimIdToFloatId.size(), (unsigned)-1);
	for (unsigned iChannelGroup = 0; iChannelGroup < hierarchyDesc.m_channelGroups.size(); ++iChannelGroup) {
		unsigned const iJointStart = hierarchyDesc.m_channelGroups[iChannelGroup].m_firstAnimatedJoint;
		unsigned const iJointEnd = iJointStart + hierarchyDesc.m_channelGroups[iChannelGroup].m_numAnimatedJoints;
		for (unsigned iJoint = iJointStart; iJoint < iJointEnd; ++iJoint) {
			int iJointAnimIndex = binding.m_animatedJointIndexToJointAnimId[iJoint];
			if (iJointAnimIndex >= 0) {
				ITASSERT(!unsharedKeys.m_blocksInGroup[iChannelGroup].empty());
				m_jointAnimIdToBlockArray[iJointAnimIndex] = &unsharedKeys.m_blocksInGroup[iChannelGroup];
				m_jointAnimIdToLocalIndex[iJointAnimIndex] = (iJoint - iJointStart);
			}
		}
		unsigned const iFloatStart = hierarchyDesc.m_channelGroups[iChannelGroup].m_firstFloatChannel;
		unsigned const iFloatEnd = iFloatStart + hierarchyDesc.m_channelGroups[iChannelGroup].m_numFloatChannels;
		for (unsigned iFloat = iFloatStart; iFloat < iFloatEnd; ++iFloat) {
			int iFloatAnimIndex = binding.m_floatIdToFloatAnimId[iFloat];
			if (iFloatAnimIndex >= 0) {
				ITASSERT(!unsharedKeys.m_blocksInGroup[iChannelGroup].empty());
				m_floatAnimIdToBlockArray[iFloatAnimIndex] = &unsharedKeys.m_blocksInGroup[iChannelGroup];
				m_floatAnimIdToLocalIndex[iFloatAnimIndex] = (iFloat - iFloatStart);
			}
		}
	}
}

void UnsharedKeyExtractor::GetScaleValues(unsigned iJointAnimIndex, Vec3Array& values) const
{
	SampledAnim const& sourceValues = *m_sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex);
	AnimationClipUnsharedKeyFrameBlockDesc::BlockArray const* pBlockArray = m_jointAnimIdToBlockArray[iJointAnimIndex];
	unsigned iLocalIndex = m_jointAnimIdToLocalIndex[iJointAnimIndex];
	for (AnimationClipUnsharedKeyFrameBlockDesc::BlockArray::const_iterator it = pBlockArray->begin(), itLast = pBlockArray->end() - 1;; ++it) {
		FrameOffsetArray const& frameOffsets = it->m_frameOffsets[kChannelTypeScale][iLocalIndex];
		unsigned numKeys = (unsigned)frameOffsets.size();
		values.push_back(sourceValues[it->m_firstFrame]);
		for (unsigned iKey = 1; iKey < numKeys - 1; ++iKey) {
			values.push_back(sourceValues[it->m_firstFrame + frameOffsets[iKey]]);
		}
		if (it == itLast) {
			break;
		}
		values.push_back(sourceValues[it->m_firstFrame + it->m_numFrames - 1]);
	}
	values.push_back(sourceValues[m_iLastFrame]);
}

void UnsharedKeyExtractor::GetRotationValues(unsigned iJointAnimIndex, QuatArray& values) const
{
	SampledAnim const& sourceValues = *m_sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex);
	AnimationClipUnsharedKeyFrameBlockDesc::BlockArray const* pBlockArray = m_jointAnimIdToBlockArray[iJointAnimIndex];
	unsigned iLocalIndex = m_jointAnimIdToLocalIndex[iJointAnimIndex];
	for (AnimationClipUnsharedKeyFrameBlockDesc::BlockArray::const_iterator it = pBlockArray->begin(), itLast = pBlockArray->end() - 1;; ++it) {
		FrameOffsetArray const& frameOffsets = it->m_frameOffsets[kChannelTypeRotation][iLocalIndex];
		unsigned numKeys = (unsigned)frameOffsets.size();
		values.push_back(sourceValues[it->m_firstFrame]);
		for (unsigned iKey = 1; iKey < numKeys - 1; ++iKey) {
			values.push_back(sourceValues[it->m_firstFrame + frameOffsets[iKey]]);
		}
		if (it == itLast) {
			break;
		}
		values.push_back(sourceValues[it->m_firstFrame + it->m_numFrames - 1]);
	}
	values.push_back(sourceValues[m_iLastFrame]);
}

void UnsharedKeyExtractor::GetTranslationValues(unsigned iJointAnimIndex, Vec3Array& values) const
{
	SampledAnim const& sourceValues = *m_sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex);
	AnimationClipUnsharedKeyFrameBlockDesc::BlockArray const* pBlockArray = m_jointAnimIdToBlockArray[iJointAnimIndex];
	unsigned iLocalIndex = m_jointAnimIdToLocalIndex[iJointAnimIndex];
	for (AnimationClipUnsharedKeyFrameBlockDesc::BlockArray::const_iterator it = pBlockArray->begin(), itLast = pBlockArray->end() - 1;; ++it) {
		FrameOffsetArray const& frameOffsets = it->m_frameOffsets[kChannelTypeTranslation][iLocalIndex];
		unsigned numKeys = (unsigned)frameOffsets.size();
		values.push_back(sourceValues[it->m_firstFrame]);
		for (unsigned iKey = 1; iKey < numKeys - 1; ++iKey) {
			values.push_back(sourceValues[it->m_firstFrame + frameOffsets[iKey]]);
		}
		if (it == itLast) {
			break;
		}
		values.push_back(sourceValues[it->m_firstFrame + it->m_numFrames - 1]);
	}
	values.push_back(sourceValues[m_iLastFrame]);
}

void UnsharedKeyExtractor::GetFloatValues(unsigned iFloatAnimIndex, std::vector<float>& values) const
{
	SampledAnim const& sourceValues = *m_sourceData.GetSampledAnim(kChannelTypeScalar, iFloatAnimIndex);
	AnimationClipUnsharedKeyFrameBlockDesc::BlockArray const* pBlockArray = m_floatAnimIdToBlockArray[iFloatAnimIndex];
	unsigned iLocalIndex = m_floatAnimIdToLocalIndex[iFloatAnimIndex];
	for (AnimationClipUnsharedKeyFrameBlockDesc::BlockArray::const_iterator it = pBlockArray->begin(), itLast = pBlockArray->end() - 1;; ++it) {
		FrameOffsetArray const& frameOffsets = it->m_frameOffsets[kChannelTypeScalar][iLocalIndex];
		unsigned numKeys = (unsigned)frameOffsets.size();
		values.push_back(sourceValues[it->m_firstFrame]);
		for (unsigned iKey = 1; iKey < numKeys - 1; ++iKey) {
			values.push_back(sourceValues[it->m_firstFrame + frameOffsets[iKey]]);
		}
		if (it == itLast) {
			break;
		}
		values.push_back(sourceValues[it->m_firstFrame + it->m_numFrames - 1]);
	}
	values.push_back(sourceValues[m_iLastFrame]);
}

void UnsharedKeyExtractor::GetScaleValues(unsigned iJointAnimIndex, Vec3Array& values, FrameArray& frames) const
{
	SampledAnim const& sourceValues = *m_sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex);
	AnimationClipUnsharedKeyFrameBlockDesc::BlockArray const* pBlockArray = m_jointAnimIdToBlockArray[iJointAnimIndex];
	unsigned iLocalIndex = m_jointAnimIdToLocalIndex[iJointAnimIndex];
	for (AnimationClipUnsharedKeyFrameBlockDesc::BlockArray::const_iterator it = pBlockArray->begin(), itLast = pBlockArray->end() - 1;; ++it) {
		FrameOffsetArray const& frameOffsets = it->m_frameOffsets[kChannelTypeScale][iLocalIndex];
		unsigned numKeys = (unsigned)frameOffsets.size();
		frames.push_back((U16)it->m_firstFrame);
		values.push_back(sourceValues[it->m_firstFrame]);
		for (unsigned iKey = 1; iKey < numKeys - 1; ++iKey) {
			frames.push_back((U16)(it->m_firstFrame + frameOffsets[iKey]));
			values.push_back(sourceValues[frames.back()]);
		}
		if (it == itLast) {
			break;
		}
		frames.push_back((U16)(it->m_firstFrame + it->m_numFrames - 1));
		values.push_back(sourceValues[frames.back()]);
	}
	frames.push_back((U16)m_iLastFrame);
	values.push_back(sourceValues[m_iLastFrame]);
}

void UnsharedKeyExtractor::GetRotationValues(unsigned iJointAnimIndex, QuatArray& values, FrameArray& frames) const
{
	SampledAnim const& sourceValues = *m_sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex);
	AnimationClipUnsharedKeyFrameBlockDesc::BlockArray const* pBlockArray = m_jointAnimIdToBlockArray[iJointAnimIndex];
	unsigned iLocalIndex = m_jointAnimIdToLocalIndex[iJointAnimIndex];
	for (AnimationClipUnsharedKeyFrameBlockDesc::BlockArray::const_iterator it = pBlockArray->begin(), itLast = pBlockArray->end() - 1;; ++it) {
		FrameOffsetArray const& frameOffsets = it->m_frameOffsets[kChannelTypeRotation][iLocalIndex];
		unsigned numKeys = (unsigned)frameOffsets.size();
		frames.push_back((U16)it->m_firstFrame);
		values.push_back(sourceValues[it->m_firstFrame]);
		for (unsigned iKey = 1; iKey < numKeys - 1; ++iKey) {
			frames.push_back((U16)(it->m_firstFrame + frameOffsets[iKey]));
			values.push_back(sourceValues[frames.back()]);
		}
		if (it == itLast) {
			break;
		}
		frames.push_back((U16)(it->m_firstFrame + it->m_numFrames - 1));
		values.push_back(sourceValues[frames.back()]);
	}
	frames.push_back((U16)m_iLastFrame);
	values.push_back(sourceValues[m_iLastFrame]);
}

void UnsharedKeyExtractor::GetTranslationValues(unsigned iJointAnimIndex, Vec3Array& values, FrameArray& frames) const
{
	SampledAnim const& sourceValues = *m_sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex);
	AnimationClipUnsharedKeyFrameBlockDesc::BlockArray const* pBlockArray = m_jointAnimIdToBlockArray[iJointAnimIndex];
	unsigned iLocalIndex = m_jointAnimIdToLocalIndex[iJointAnimIndex];
	for (AnimationClipUnsharedKeyFrameBlockDesc::BlockArray::const_iterator it = pBlockArray->begin(), itLast = pBlockArray->end() - 1;; ++it) {
		FrameOffsetArray const& frameOffsets = it->m_frameOffsets[kChannelTypeTranslation][iLocalIndex];
		unsigned numKeys = (unsigned)frameOffsets.size();
		frames.push_back((U16)it->m_firstFrame);
		values.push_back(sourceValues[it->m_firstFrame]);
		for (unsigned iKey = 1; iKey < numKeys - 1; ++iKey) {
			frames.push_back((U16)(it->m_firstFrame + frameOffsets[iKey]));
			values.push_back(sourceValues[frames.back()]);
		}
		if (it == itLast) {
			break;
		}
		frames.push_back((U16)(it->m_firstFrame + it->m_numFrames - 1));
		values.push_back(sourceValues[frames.back()]);
	}
	frames.push_back((U16)m_iLastFrame);
	values.push_back(sourceValues[m_iLastFrame]);
}

void UnsharedKeyExtractor::GetFloatValues(unsigned iFloatAnimIndex, std::vector<float>& values, FrameArray& frames) const
{
	SampledAnim const& sourceValues = *m_sourceData.GetSampledAnim(kChannelTypeScalar, iFloatAnimIndex);
	AnimationClipUnsharedKeyFrameBlockDesc::BlockArray const* pBlockArray = m_floatAnimIdToBlockArray[iFloatAnimIndex];
	unsigned iLocalIndex = m_floatAnimIdToLocalIndex[iFloatAnimIndex];
	for (AnimationClipUnsharedKeyFrameBlockDesc::BlockArray::const_iterator it = pBlockArray->begin(), itLast = pBlockArray->end() - 1;; ++it) {
		FrameOffsetArray const& frameOffsets = it->m_frameOffsets[kChannelTypeScalar][iLocalIndex];
		unsigned numKeys = (unsigned)frameOffsets.size();
		frames.push_back((U16)it->m_firstFrame);
		values.push_back(sourceValues[it->m_firstFrame]);
		for (unsigned iKey = 1; iKey < numKeys - 1; ++iKey) {
			frames.push_back((U16)(it->m_firstFrame + frameOffsets[iKey]));
			values.push_back(sourceValues[frames.back()]);
		}
		if (it == itLast) {
			break;
		}
		frames.push_back((U16)(it->m_firstFrame + it->m_numFrames - 1));
		values.push_back(sourceValues[frames.back()]);
	}
	frames.push_back((U16)m_iLastFrame);
	values.push_back(sourceValues[m_iLastFrame]);
}

UnsharedKeyExtractor& UnsharedKeyExtractor::operator=(const UnsharedKeyExtractor&) 
{ 
	ITASSERT(0); return *this; 
}


}	//namespace AnimProcessing
}	//namespace Tools
}	//namespace OrbisAnim
