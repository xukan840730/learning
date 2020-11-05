#include "build-transform-material-extract.h"

#include "icelib/itscene/cgfxshader.h"

#include "tools/libs/libdb2/db2-actor.h"
#include "tools/pipeline3/build/util/material-shader-map.h"
#include "tools/pipeline3/common/4_shaders.h"
#include "tools/pipeline3/common/4_shader_parameters.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/push-to-farm.h"
#include "tools/pipeline3/shcomp4/effect_metadata.h"
#include "tools/pipeline3/shcomp4/material_feature_filter.h"
#include "tools/pipeline3/build/build-transforms/build-scheduler.h"
#include "tools/pipeline3/build/util/material-shader-map.h"

#include "build-transform-context.h"
#include "build-transform-materials.h"

//#pragma optimize("", off)

using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

MaterialExtractTransformSpawnDesc::MaterialExtractTransformSpawnDesc()
	: BuildTransformSpawnDesc("MaterialExtractTransform")
{
}

BuildTransform* MaterialExtractTransformSpawnDesc::CreateTransform(const BuildTransformContext *pContext) const
{
	//Save what materials are using this shader
	for (const string& material : m_materials)
		MaterialShaderMap::Add(material, m_shaderHash);

	return new MaterialExtractTransform(pContext->m_toolParams);
}

void MaterialExtractTransformSpawnDesc::ReadExtra(NdbStream &stream, const char *pSymData)
{
	NdbRead(stream, "shaderHash", m_shaderHash);
	NdbRead(stream, "materialList", m_materials);
}

void MaterialExtractTransformSpawnDesc::WriteExtra(NdbStream &stream, const char *pSymData)
{
	NdbWrite(stream, "shaderHash", m_shaderHash);
	NdbWrite(stream, "materialList", m_materials);
}

const char* MaterialExtractTransform::INPUT_NICKNAME_SHADERS_LIST = "shaderlist";

MaterialExtractTransform::MaterialExtractTransform(const ToolParams &toolParams)
	: BuildTransform("MatExtract")
	, m_toolParams(toolParams)
	, m_dependenciesTexturesList()
{}

MaterialExtractTransform::~MaterialExtractTransform()
{}

BuildTransformStatus MaterialExtractTransform::Evaluate()
{
	const BuildFile& input = GetInputFile("shaderlist");
	NdbStream ndbStream;
	DataStore::ReadData(input, ndbStream);

	NdbRead(ndbStream, "m_shaderDescription", m_description);

	matdb::MatDb matdbCopy(*m_toolParams.m_matdb);
	ExportShaders(m_toolParams, matdbCopy, m_description);

	return BuildTransformStatus::kResumeNeeded;
}

BuildTransformStatus MaterialExtractTransform::ResumeEvaluation(const SchedulerResumeItem& resumeItem)
{
	const FarmSession::Job *pJob = dynamic_cast<const FarmSession::Job*>(resumeItem.m_farmJob);
	if (pJob->m_exitcode)
	{
		IERR("Error in shcomp4 job.");
		return BuildTransformStatus::kFailed;
	}

	RegisterShcompDependencies(pJob->m_output);
	
	return BuildTransformStatus::kOutputsUpdated;
}

void MaterialExtractTransform::OnJobError()
{
	const set<string> materialsList = MaterialShaderMap::Get(m_description.m_shaderHash);
	if (materialsList.size() == 0)
	{
		AddErrorMessage("Shader " + m_description.m_shaderHash + " failed to build but no materials references it. This shouldn't happen, contact tools dog.");
	}
	else
	{
		string errorMessage = "Shader " + m_description.m_shaderHash + " is referenced in materials : ";

		for (const string& material : materialsList)
			errorMessage += "\n   - " + material;

		AddErrorMessage(errorMessage);
	}
}

void MaterialExtractTransform::AddTextureDependency(const ITSCENE::CgfxShader* pShader, const material_compiler::CompiledEffect& effect, matdb::MatDb& matDb)
{
	matdb::ShaderInfo shaderInfo;
	if (!matDb.TryGetShaderAndResolveLayers(pShader->GetShaderPath(), shaderInfo))
		return;

	std::vector<material_compiler::SamplerDefinition> samplers;
	pipeline3::GetInstancedShaderSamplers(samplers, effect.m_effect);

	for (const material_compiler::SamplerDefinition& samplerDefinition : samplers)
	{
		const matdb::ShaderInfo::TextureRefInfo* pTexRef = shaderInfo.GetTextureRef(shaderInfo.GetTextureSetName(), samplerDefinition.m_textureObjectName);

		std::string filename = pTexRef ? pTexRef->m_filename : samplerDefinition.m_defaultTextureFilename;
		if (filename.empty())
			continue;

		std::transform(filename.begin(), filename.end(), filename.begin(), tolower);
		std::string prefixedTextureFilename = PathPrefix::BAM + filename;

		string materialPath = pShader->GetShaderPath();
		if (materialPath.empty())
			continue;

		size_t pos = materialPath.find(':');
		string realMaterialPath = materialPath.substr(0, pos);
		string prefixedMaterialPath = PathPrefix::GAMEDB;
		prefixedMaterialPath += "matdb/" + realMaterialPath + ".material.xml";

		m_dependenciesTexturesList.push_back(pair<string, string>(prefixedMaterialPath, prefixedTextureFilename));
	}
}

void MaterialExtractTransform::ExportShaders(const ToolParams& tool, matdb::MatDb& matDbCopy, const ShaderDescription& description)
{
	string shaderIncludePath = toolsutils::GetGameShaderPath();
	if (shaderIncludePath[shaderIncludePath.length() - 1] != '/')
		shaderIncludePath += "/";
	shaderIncludePath += "include";

	const string& shaderName = description.m_shaderName;
	BuildPath buildPath(description.m_buildPath);
	string strBuildPath = buildPath.AsAbsolutePath();
	const vector<string>& flagsList = description.m_flagsList;
	const vector<pair<string, string>>& parametersList = description.m_parametersList;
	std::vector<std::string> forcedUvs;
	if (description.m_forcedUvs & libdb2::Geometry::kForcedUv0)
		forcedUvs.push_back("uv0");

	if (description.m_forcedUvs & libdb2::Geometry::kForcedUv1)
		forcedUvs.push_back("uv1");

	if (description.m_forcedUvs & libdb2::Geometry::kForcedUv3)
		forcedUvs.push_back("uv3");

	if (description.m_forcedUvs & libdb2::Geometry::kForcedUv4)
		forcedUvs.push_back("uv4");

	material_exporter::FeatureFilter filters; //We have already filtered the features in the spawner so this object is useless now. It needs to be removed when bl has switch to the new system.
	Farm& farm = GetFarmSession();

	string output = GetOutput("compiledEffect").m_path.AsAbsolutePath();

	IASSERT(!tool.m_executablesPath.empty());
	FarmJobId jobId = pipeline3::BuildShader(filters, shaderName.c_str(), output.c_str(), shaderIncludePath.c_str(), flagsList, parametersList, tool.m_local, tool.m_noSrt, tool.m_localtools, farm, tool.m_executablesPath, tool.m_userName, forcedUvs);

	if (jobId == FarmJobId::kInvalidFarmjobId)
	{
		IABORT("An error occurred while kicking a shader job.");
	}

	RegisterFarmWaitItem(jobId);
}

void MaterialExtractTransform::RegisterShcompDependencies(const string& jobOutput)
{
	size_t offset = 0;

	const string PATTERN = "Loaded dependency [";
	size_t result = jobOutput.find(PATTERN, offset);
	while (result != string::npos)
	{
		size_t end = jobOutput.find("]", result);
		if (end == string::npos)
		{
			IABORT("Failed to parse job output. A loaded dependency doesn't end with the character ].");
		}

		string dependencyEntry(jobOutput.cbegin() + result + PATTERN.size(), jobOutput.cbegin() + end);

		size_t pos = dependencyEntry.find(' ');
		if (pos == string::npos)
		{
			IABORT("A dependency was found but no depth could be extracted.");
		}

		string strDepth = dependencyEntry.substr(0, pos);
		string dependencyFile = dependencyEntry.substr(pos + 1);

		if (dependencyFile.find("c:/temp") != 0)
		{
			const BuildPath refPath(dependencyFile);
			int depth = atoi(strDepth.c_str());
			RegisterDiscoveredDependency(refPath, depth);
		}

		offset = end + 1;

		result = jobOutput.find("Loaded dependency [", offset);
	}
}
