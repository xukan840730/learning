/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include <vector>
#include <set>
#include <string>

#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-animation.h"
#include "tools/libs/exporters/bigexporter.h"


struct AnimStreamHeader
{
	int											m_framesPerBlock;
	int											m_numSlotsInStream;
	int											m_numBlocks;
	std::string									m_streamName;

	std::vector<int>							m_blockSizes;					// [numBlocks]
	std::vector<SkeletonId>						m_skelIds;						// [numSlots]
	std::vector<std::string>					m_animNames;					// [numSlots]
	std::vector<int>							m_interleavedBlockSizes;		// [numBlocks / numSlotsInStream]
};

//--------------------------------------------------------------------------------------------------------------------//
class BuildTransform_AnimStreamStm : public BuildTransform
{
public:

	BuildTransform_AnimStreamStm(const BuildTransformContext* pContext, std::vector<StreamBoEntry>& streamBoList/*, std::vector<AnimStreamHeader>& streamHeaders*/) : 
		BuildTransform("AnimStreamStm", pContext),
		m_streamEntries(streamBoList)
	{}

	BuildTransformStatus Evaluate() override;

private:

	const std::vector<StreamBoEntry> m_streamEntries;
};

const int kNumFramesPerStreamingChunk = 30;

inline std::string MakeStreamAnimName(const std::string& name, int index)
{
	std::stringstream stream;
	stream << name << "-chunk-" << index;

	return stream.str();
}

inline std::string MakeStreamAnimNameFinal(const std::string& name)
{
	std::stringstream stream;
	stream << name << "-chunk-last";

	return stream.str();
}



