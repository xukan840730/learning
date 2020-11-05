/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform.h"
#include "tools/libs/bigstreamwriter/bigstreamwriter.h"

namespace libdb2
{
	class Actor;
}

class BuildTransform_Cinematic : public BuildTransform
{
public:
	BuildTransform_Cinematic(const BuildTransformContext *const pContext, 
							const std::string& actorName);

	BuildTransformStatus Evaluate() override;

public:

	U32 m_metadataMemoryUsage;
};

class BuildTransform_CinematicSequence : public BuildTransform
{
public:
	BuildTransform_CinematicSequence(const BuildTransformContext *const pContext, 
									const std::string& actorName);

	BuildTransformStatus Evaluate() override;

private:
	#define		CINEMATIC_VERSION			(0x3A700001)	// Jason's birthday (3/10/1970)

	U32 WriteSequenceItems(BigStreamWriter &stream, 
						const BuildTransformContext *const pContext, 
						const libdb2::Actor *const pDbActor);

public:

	U32 m_metadataMemoryUsage;
};

void BuildModuleCinematic_Configure(const BuildTransformContext *const pContext, 
									const libdb2::Actor *const pDbActor, 
									std::vector<std::string>& arrBoFiles);
