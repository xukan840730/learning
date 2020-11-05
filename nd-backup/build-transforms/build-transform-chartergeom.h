#pragma once

#include "build-transform.h"

#include <string>

class BuildTransformContext;

class BuildTransform_CharterGeom : public BuildTransform
{
public:

	BuildTransform_CharterGeom(const BuildTransformContext* pContext, const char* actorName, const char* geometrySet);
	~BuildTransform_CharterGeom();

	BuildTransformStatus Evaluate() override;

private:
	void PopulatePreEvalDependencies();

	std::string m_actorName;
	std::string m_geometrySet;
};