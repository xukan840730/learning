#pragma once

#include "build-transform.h"

class BuildTransformContext;
namespace libdb2
{
	class Actor;
	class Anim;
};

class BuildTransform_ActorTxt : public BuildTransform
{
public:
	BuildTransform_ActorTxt(const BuildTransformContext *const pContext, 
							const std::string& animActorTxtEntries);
	
	BuildTransformStatus Evaluate() override;
};

void BuildTransformActorTxt_Configure(const libdb2::Actor *const pDbActor, 
									const BuildTransformContext *const pContext, 
									const std::vector<std::string>& pakFilenames,
									const std::string& animActorTxtEntries, 
									const std::vector<std::string>& soundBankRefsBoFiles,
									U32 numLightSkels);