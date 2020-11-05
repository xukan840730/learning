#pragma once

#include "build-transform.h"

class BuildTransformContext;
namespace libdb2
{
	class Actor;
	class Anim;
};

class BuildTransform_ParseActor : public BuildTransform
{
public:
	BuildTransform_ParseActor(const std::string& actorName, 
							const BuildTransformContext *const pContext);

	BuildTransformStatus Evaluate() override;

private:
	bool LoadXmlFiles(const libdb2::Actor *const pDbActor,
					std::vector<const libdb2::Actor*>& actorList);
	void CreateBuildTransforms(const libdb2::Actor *const pDbActor,
							const std::vector<const libdb2::Actor*>& actorList);
	void WriteOutputs(const std::vector<const libdb2::Actor*>& actorList);

	void AddAssetDependencies(const libdb2::Actor *const pDbActor);
	void AddDiscoveredDependencies(const libdb2::Actor *const pDbActor);

	void ExpandActor(const libdb2::Actor *const pActor);
	void ExpandAnimation(const libdb2::Anim& animation);

private:
	std::string m_actorName;
	mutable std::set<std::string> m_expandedFiles;
};


