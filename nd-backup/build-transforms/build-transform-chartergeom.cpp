#include "tools/pipeline3/build/build-transforms/build-transform-chartergeom.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"

#include "tools/libs/chartergeom/chartergeom.h"
#include "tools/pipeline3/toolversion.h"

#include "tools/pipeline3/build/tool-params.h"

using std::string;
using std::vector;

//#pragma optimize("", off)

BuildTransform_CharterGeom::BuildTransform_CharterGeom(const BuildTransformContext* pContext, const char* actorName, const char* geometrySet)
	: BuildTransform("CharterGeom", pContext)
	, m_actorName(actorName)
	, m_geometrySet(geometrySet)
{
	PopulatePreEvalDependencies();
}

BuildTransform_CharterGeom::~BuildTransform_CharterGeom()
{
}

void BuildTransform_CharterGeom::PopulatePreEvalDependencies()
{
	m_preEvaluateDependencies.SetString("m_geometrySet", m_geometrySet);
}

BuildTransformStatus BuildTransform_CharterGeom::Evaluate()
{
	const BuildFile& ndbFile = GetInputFile("GeoNdb");
	CharterGeom::Geom charterGeom(m_actorName.c_str(), ndbFile, m_geometrySet.c_str(), m_pContext->m_toolParams.m_local);
	if (!charterGeom.WriteCharterGeomFileForActor())
		return BuildTransformStatus::kFailed;

	return BuildTransformStatus::kOutputsUpdated;
}

