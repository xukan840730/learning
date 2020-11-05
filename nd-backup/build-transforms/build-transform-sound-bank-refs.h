/*
* Copyright (c) 2015 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include <vector>
#include <string>

#include "tools/pipeline3/build/build-transforms/build-transform.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"

namespace libdb2
{
	class Actor;
}


class BuildTransform_SoundBankReferences : public BuildTransform
{
public:
	BuildTransform_SoundBankReferences(const std::string& actorName, 
									const BuildTransformContext *const pContext,
									const std::vector<std::string>& soundBanks);

	BuildTransformStatus Evaluate() override;

private:
	std::vector<std::string> m_soundBanks;
};


void BuildTransformSoundBankRefs_Configure(const BuildTransformContext *const pContext,
										const libdb2::Actor *const pDbActor,
										const std::vector<const libdb2::Actor*>& actorList,
										std::vector<std::string>& arrBoFiles);
