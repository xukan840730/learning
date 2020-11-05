/*
* Copyright (c) 2005 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "animprocessingstructs.h"

namespace OrbisAnim {
namespace Tools {
namespace AnimProcessing {

using ITGEOM::Vec3Array;
using ITGEOM::QuatArray;

/// Key extractor for uniform keyframe compression
class UniformKeyExtractor : public KeyExtractor
{
public:
	UniformKeyExtractor(UniformKeyExtractor const& e);
	UniformKeyExtractor(AnimationClipSourceData const& sourceData);
	virtual void GetScaleValues(unsigned iJointAnimIndex, Vec3Array& values) const;
	virtual void GetRotationValues(unsigned iJointAnimIndex, QuatArray& values) const;
	virtual void GetTranslationValues(unsigned iJointAnimIndex, Vec3Array& values) const;
	virtual void GetFloatValues(unsigned iFloatAnimIndex, std::vector<float>& values) const;
	virtual void GetScaleValues(unsigned iJointAnimIndex, Vec3Array& values, FrameArray& frames) const;
	virtual void GetRotationValues(unsigned iJointAnimIndex, QuatArray& values, FrameArray& frames) const;
	virtual void GetTranslationValues(unsigned iJointAnimIndex, Vec3Array& values, FrameArray& frames) const;
	virtual void GetFloatValues(unsigned iFloatAnimIndex, std::vector<float>& values, FrameArray& frames) const;
private:
	void GetFrameArray(FrameArray& frames) const;
	UniformKeyExtractor& operator=(const UniformKeyExtractor&);

	AnimationClipSourceData const& m_sourceData;
	unsigned m_numOutputFrames;
};

/// Key extractor for shared non-uniform keyframe compression,
/// which defines a set of key frames shared by all channels of an animation,
/// allowing some full key frames to be dropped.
class SharedKeyExtractor : public KeyExtractor
{
public:
	SharedKeyExtractor(SharedKeyExtractor const& e);
	SharedKeyExtractor(AnimationClipSourceData const& sourceData, FrameArray const& sharedKeys);
	virtual void GetScaleValues(unsigned iJointAnimIndex, Vec3Array& values) const;
	virtual void GetRotationValues(unsigned iJointAnimIndex, QuatArray& values) const;
	virtual void GetTranslationValues(unsigned iJointAnimIndex, Vec3Array& values) const;
	virtual void GetFloatValues(unsigned iFloatAnimIndex, std::vector<float>& values) const;
	virtual void GetScaleValues(unsigned iJointAnimIndex, Vec3Array& values, FrameArray& frames) const;
	virtual void GetRotationValues(unsigned iJointAnimIndex, QuatArray& values, FrameArray& frames) const;
	virtual void GetTranslationValues(unsigned iJointAnimIndex, Vec3Array& values, FrameArray& frames) const;
	virtual void GetFloatValues(unsigned iFloatAnimIndex, std::vector<float>& values, FrameArray& frames) const;
private:
	void GetFrameArray(FrameArray& frames) const;
	SharedKeyExtractor& operator=(const SharedKeyExtractor&);

	AnimationClipSourceData const& m_sourceData;
	FrameArray const& m_sharedKeys;
	unsigned m_numOutputFrames;
	unsigned m_iLastFrame;
};

/// Key extractor for unshared non-uniform keyframe compression,
/// which defines a separate set of key frames for each scale, rotation, translation,
/// and float channel, allowing key frames to be dropped independently for each.
class UnsharedKeyExtractor : public KeyExtractor
{
public:
	UnsharedKeyExtractor(UnsharedKeyExtractor const& e);
	UnsharedKeyExtractor(AnimationHierarchyDesc const& hierarchyDesc,
						 AnimationBinding const& binding,
						 AnimationClipSourceData const& sourceData,
						 AnimationClipUnsharedKeyFrameBlockDesc const& unsharedKeys);
	virtual void GetScaleValues(unsigned iJointAnimIndex, Vec3Array& values) const;
	virtual void GetRotationValues(unsigned iJointAnimIndex, QuatArray& values) const;
	virtual void GetTranslationValues(unsigned iJointAnimIndex, Vec3Array& values) const;
	virtual void GetFloatValues(unsigned iFloatAnimIndex, std::vector<float>& values) const;
	virtual void GetScaleValues(unsigned iJointAnimIndex, Vec3Array& values, FrameArray& frames) const;
	virtual void GetRotationValues(unsigned iJointAnimIndex, QuatArray& values, FrameArray& frames) const;
	virtual void GetTranslationValues(unsigned iJointAnimIndex, Vec3Array& values, FrameArray& frames) const;
	virtual void GetFloatValues(unsigned iFloatAnimIndex, std::vector<float>& values, FrameArray& frames) const;
private:
	UnsharedKeyExtractor& operator=(const UnsharedKeyExtractor&);

	AnimationClipSourceData const& m_sourceData;
	std::vector<AnimationClipUnsharedKeyFrameBlockDesc::BlockArray const*> m_jointAnimIdToBlockArray;
	std::vector<AnimationClipUnsharedKeyFrameBlockDesc::BlockArray const*> m_floatAnimIdToBlockArray;
	std::vector<unsigned> m_jointAnimIdToLocalIndex;
	std::vector<unsigned> m_floatAnimIdToLocalIndex;
	unsigned m_iLastFrame;
};

}	//namespace AnimProcessing
}	//namespace Tools
}	//namespace OrbisAnim

