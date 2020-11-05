#include "build-transform-material-expander.h"

#include "icelib/itscene/cgfxshader.h"
#include "icelib/itscene/scenedb.h"
#include "icelib/itscene/ndbwriter.h"
#include "icelib/itscene/numesh.h"
#include "icelib/ndb/ndbbasictemplates.h"
#include "icelib/ndb/ndbstdvector.h"
#include "tools/libs/libdb2/db2-actor.h"
#include "tools/libs/libdb2/db2-commontypes.h"
#include "tools/libs/thread/threadpool.h"
#include "tools/libs/libmatdb/shaderinfo.h"
#include "tools/pipeline3/build/level/build-transform-cluster-instances.h"
#include "tools/pipeline3/build/util/dependency-database-manager.h"
#include "tools/pipeline3/build/util/material-dependencies.h"
#include "tools/pipeline3/common/4_gameflags.h"
#include "tools/pipeline3/common/4_shaders.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/material-common.h"
#include "tools/pipeline3/common/material/material_exporter.h"
#include "tools/pipeline3/common/mesh-attribute-compression.h"
#include "tools/pipeline3/common/path-prefix.h"
#include "tools/pipeline3/common/scene-util.h"
#include "tools/pipeline3/shcomp4/material_feature_filter.h"
#include "common/hashes/bighash.h"

#include "build-transform-context.h"
#include "build-transform-materials.h"

#include <memory>

//#pragma optimize("", off)

#define ASYNC

using std::pair;
using std::string;
using std::stringstream;
using std::vector;
using std::unique_ptr;

using namespace std::placeholders;

static std::string GetPrefixedMaterialPath(const std::string &materialPath)
{
	if (materialPath.empty())
		return "";

	size_t pos = materialPath.find(':');
	const std::string realMaterialPath = materialPath.substr(0, pos);
	const std::string prefixedMaterialPath = PathPrefix::GAMEDB + std::string("matdb/") + realMaterialPath + ".material.xml";

	return prefixedMaterialPath;
}

MaterialExpanderTransform::MaterialExpanderTransform(const BuildTransformContext *const pContext, 
													const std::string& actorName, 
													size_t actorLod)
	: BuildTransform("MatExpander", pContext)
	, m_dependenciesMaterialsList()
	, m_dependenciesExtraMaterialList()
	, m_shadersToBuild()
	, m_shaderToUniqueShader()
{
	PopulatePreEvalDependencies(actorName, actorLod, "", "");
}

MaterialExpanderTransform::MaterialExpanderTransform(const BuildTransformContext *const pContext, 
													const std::string& levelName, 
													const std::string& mayaFilename)
	: BuildTransform("MatExpander", pContext)
	, m_dependenciesMaterialsList()
	, m_dependenciesExtraMaterialList()
	, m_mayaFilename(mayaFilename)
	, m_shadersToBuild()
	, m_shaderToUniqueShader()
{
	PopulatePreEvalDependencies("", -1, levelName, mayaFilename);
}

MaterialExpanderTransform::~MaterialExpanderTransform()
{}

void MaterialExpanderTransform::PopulatePreEvalDependencies(const std::string& actorName, 
															size_t actorLod, 
															const std::string& levelName,
															const std::string& mayaFilename)
{
	if (!actorName.empty())
	{
		const libdb2::Actor*  pDbActor = libdb2::GetActor(actorName, actorLod);

		m_preEvaluateDependencies.SetBool("isLevel", false);
		m_preEvaluateDependencies.SetString("actorName", actorName);
		m_preEvaluateDependencies.SetInt("actorLod", actorLod);

		m_preEvaluateDependencies.SetString("materialRemaps", pDbActor->m_materialRemapList.Xml());
		m_preEvaluateDependencies.SetString("extraMaterials", pDbActor->m_extraMaterialsList.Xml());

		m_preEvaluateDependencies.SetString("gameFlags", pDbActor->m_gameFlagsInfo.m_gameFlags);

		if (pDbActor->m_geometry.Loaded())
		{
			const libdb2::Geometry& geometry = pDbActor->m_geometry;

			m_preEvaluateDependencies.SetBool("useBakedForegroundLighting", geometry.m_useBakedForegroundLighting);				//Used in InitializeFeatureFilter
			m_preEvaluateDependencies.SetBool("lightmapsOverride", geometry.m_lightmapsOverride.m_enabled);						//Used in InitializeFeatureFilter
			m_preEvaluateDependencies.SetInt("lightmapsOverrideSourceCount", geometry.m_lightmapsOverride.m_sources.size());	//Used in InitializeFeatureFilter

			for (int ii = 0; ii < geometry.m_shaderfeature.size(); ++ii)
			{
				const libdb2::GeometryTag_ShaderFeature& feature = geometry.m_shaderfeature[ii];

				stringstream ss;
				ss << "shaderFeatures-" << ii;
				m_preEvaluateDependencies.SetString(ss.str(), feature.m_value);			//Used in InitializeFeatureFilter
			}
		}
	}
	else if (!levelName.empty())
	{
		const libdb2::Level* pDbLevel(libdb2::GetLevel(levelName));
		if (!pDbLevel->Loaded())
		{
			IABORT("Unknown level %s\n", levelName.c_str());
		}

		m_preEvaluateDependencies.SetBool("isLevel", true);
		m_preEvaluateDependencies.SetString("levelName", levelName);

		m_preEvaluateDependencies.SetBool("useLightmaps", true);
		m_preEvaluateDependencies.SetString("mayaFilename", m_mayaFilename);
		m_preEvaluateDependencies.SetString("materialRemaps", pDbLevel->m_materialRemapList.Xml());
		m_preEvaluateDependencies.SetString("extraMaterials", pDbLevel->m_extraMaterialsList.Xml());

		m_preEvaluateDependencies.SetBool("designerLightingMode", pDbLevel->m_lightingSettings.m_designerLightingMode);
		m_preEvaluateDependencies.SetInt("lightrigCount", pDbLevel->m_lightingSettings.m_lightAtgis.size());

		for (int ii = 0; ii < pDbLevel->m_shaderFeatures.size(); ++ii)
		{
			const libdb2::ShaderFeature& feature = pDbLevel->m_shaderFeatures[ii];

			stringstream ss;
			ss << "shaderFeatures-" << ii;
			m_preEvaluateDependencies.SetString(ss.str(), feature.m_name + "-" + feature.m_action);
		}
	}

	m_preEvaluateDependencies.SetBool("enableVertexCompression", m_pContext->m_toolParams.m_enableVertexCompression);


	RegisterMatDbDependencies(m_preEvaluateDependencies);
}

BuildTransformStatus MaterialExpanderTransform::Evaluate()
{
#ifndef ASYNC
	ExecuteMaterialExpander_Thread(nullptr);
	return Internal_ResumeEvaluation();
#else
	const WorkItemHandle handle = ExecuteAsyncMaterialExpander();
	RegisterThreadPoolWaitItem(handle);

	return BuildTransformStatus::kResumeNeeded;
#endif
}

BuildTransformStatus MaterialExpanderTransform::ResumeEvaluation(const SchedulerResumeItem& resumeItem)
{
	return Internal_ResumeEvaluation();
}

void MaterialExpanderTransform::LoadGeoNdb(ITSCENE::SceneDb* actorGeoNdb) const
{
	if (!DoesInputExist("GeoNdb"))
		return;

	const BuildFile& file = GetInputFile("GeoNdb");

	NdbStream geoNdbStream;
	DataStore::ReadData(file, geoNdbStream);

	bool success = LoadSceneNdb(geoNdbStream, actorGeoNdb);
	if (!success)
	{
		IABORT("Failed to load ndb %s[%s].", file.AsPrefixedPath().c_str(), file.GetContentHash().AsText().c_str());
	}	
}

void MaterialExpanderTransform::LoadMaterialRemapNdb(std::vector<std::string>& instanceMaterialRemaps) const
{
	const string materialRemapInputNickname = "MaterialRemapsNdb";
	if (DoesInputExist(materialRemapInputNickname))
	{
		const BuildFile& materialRemapInput = GetInputFile(materialRemapInputNickname);
		NdbStream matRemapStream;
		DataStore::ReadData(materialRemapInput, matRemapStream);
		NdbRead(matRemapStream, "m_instanceMaterialRemaps", instanceMaterialRemaps);
	}
}

void MaterialExpanderTransform::LoadAutoActorsNdb(std::vector<std::string>& autoActors) const
{
	const string autoActorsInputNickname = "AutoActorsNdb";
	if (DoesInputExist(autoActorsInputNickname))
	{
		const BuildFile& autoActorsInput = GetInputFile(autoActorsInputNickname);
		NdbStream autoActorsStream;
		DataStore::ReadData(autoActorsInput, autoActorsStream);
		NdbRead(autoActorsStream, "m_autoActors", autoActors);
	}
}

void MaterialExpanderTransform::LoadRemovableMaterialsNdb(std::vector<std::string>& removableMaterials) const
{
	const string removableMaterialsInputNickname = "RemovableMaterialsNdb";
	if (DoesInputExist(removableMaterialsInputNickname))
	{
		const BuildFile& removableMaterialsInput = GetInputFile(removableMaterialsInputNickname);
		NdbStream removableMaterialsStream;
		DataStore::ReadData(removableMaterialsInput, removableMaterialsStream);
		NdbRead(removableMaterialsStream, "m_removableMaterials", removableMaterials);
	}
}

void MaterialExpanderTransform::LoadAttributeCompressionSettingsNdb(AttributeCompressionSettingsTable& attribCompressionSettingsTable) const
{
	const string attribCompressionSettingsInputNickname = "AttributeCompressionSettingsNdb";
	if (DoesInputExist(attribCompressionSettingsInputNickname))
	{
		const BuildFile& attribCompressionSettingsInput = GetInputFile(attribCompressionSettingsInputNickname);

		NdbStream attribCompressionSettingsStream;
		DataStore::ReadData(attribCompressionSettingsInput, attribCompressionSettingsStream);
		NdbRead(attribCompressionSettingsStream, "attribCompressionSettings", attribCompressionSettingsTable);
	}
}

void MaterialExpanderTransform::LoadInstanceClustersNdb(InstanceClusters& clusters) const
{
	const string  instanceClustersInputNickname = "InstanceClustersNdb";
	if (DoesInputExist(instanceClustersInputNickname))
	{
		NdbStream instanceClustersStream;
		DataStore::ReadData(GetInputFile(instanceClustersInputNickname), instanceClustersStream);
		NdbRead(instanceClustersStream, "instanceClusters", clusters);
	}
}

void MaterialExpanderTransform::RegisterMaterialsInPostEvalDependencies(const std::vector<ITSCENE::CgfxShader *>& shadersList, matdb::MatDb& matDb)
{
	MaterialDependenciesCollector collector;
	for (const ITSCENE::CgfxShader* shader : shadersList)
		collector.Run(*shader, matDb, std::bind(&MaterialExpanderTransform::MaterialDependenciesCollectorCallback, this, _1, _2, _3, _4));
}

std::vector<ITSCENE::CgfxShader*> MaterialExpanderTransform::CollectActorShaders(const ITSCENE::SceneDb& dbScene, 
																				const libdb2::MaterialRemapList& matRemapList, 
																				const libdb2::MaterialList& extraMatList, 
																				const std::vector<std::string>& instanceMaterialRemaps, 
																				bool makeEverythingWhite, 
																				matdb::MatDb& matDbCopy)
{
	std::vector<ITSCENE::CgfxShader*> actorShaders;
	for (auto pShader : dbScene.m_cgfxShaders)
	{
		const matdb::ShaderInfo* pShaderInfo = matDbCopy.GetShaderInfo(pShader->GetShaderPath());	// we have to do this in order to fix malformed shader paths
		if (pShaderInfo)
			pShader->SetShaderPath(pShaderInfo->GetRawPath());

		actorShaders.push_back(new ITSCENE::CgfxShader(*(pShader)));
	}

	ShaderRemaps remaps;
	GetShaderRemaps(remaps, matRemapList);

	vector<string> missingRemappedMaterial;
	RemapShaders(actorShaders, remaps, matDbCopy.GetDbRoot(), missingRemappedMaterial);

	//Add missing remapped material in the dependencies. If I don't register them, only the original material will be in the dependencies. If then the remapped material is added,
	// I won't detect it and won't re execute this transform.
	for (const string& missingMaterial : missingRemappedMaterial)
	{
		BuildPath path(missingMaterial);
		DiscoveredDependency dep(path, 0);
		RegisterDiscoveredDependency(dep);
	}
	
	//TODO : split RemoveMissingShadersAndInjectExtras into 2 functions
	RemoveMissingShadersAndInjectExtras(actorShaders, extraMatList, instanceMaterialRemaps, matDbCopy, makeEverythingWhite);

	RegisterMaterialsInPostEvalDependencies(actorShaders, matDbCopy);

	for (const ITSCENE::CgfxShader* pShader : actorShaders)
	{
		AddMaterialDependency(pShader);
	}

	for (const libdb2::Material& extraMaterial : extraMatList)
	{
		AddExtraMaterialDependency(extraMaterial);
	}

	return actorShaders;
}

void MaterialExpanderTransform::WriteOutput(const vector<ITSCENE::CgfxShader*>& shadersList, const vector<int>& shaderToUniqueShader)
{
	NdbStream ndbStream;
	ndbStream.OpenForWriting(Ndb::kBinaryStream);
	NdbWrite(ndbStream, "m_actorShaders", shadersList);
	NdbWrite(ndbStream, "m_shaderToUniqueShader", shaderToUniqueShader);

	const bool isLevel = m_preEvaluateDependencies.GetBool("isLevel");
	if (isLevel)
	{
		NdbWrite(ndbStream, "m_mayaFilename", m_mayaFilename);
	}

	ndbStream.Close();

	const BuildPath& path = GetOutputPath("shaderlist");
	DataStore::WriteData(path, ndbStream);
}

void MaterialExpanderTransform::WriteMatFlags(const std::map<std::string, U32>& matFlags)
{
	// write out material flags
	NdbStream ndbStream;
	ndbStream.OpenForWriting(Ndb::kBinaryStream);
	NdbWrite(ndbStream, "m_matFlags", matFlags);
	ndbStream.Close();

	const BuildPath& path = GetOutputPath("matflags");
	DataStore::WriteData(path, ndbStream);
}


void MaterialExpanderTransform::AddMaterialDependency(const ITSCENE::CgfxShader* pShader)
{
	const char *pPropStrs[] = {
		"shaderPath",
		"shaderPath_toolProcessed_BeforeRemap",
		"shaderPath_toolProcessed_BeforeMissing"
	};

	const int numProps = (sizeof pPropStrs) / (sizeof pPropStrs[0]);
	for (int i = 0; i < numProps; i++)
	{
		if (const std::string *pPath = pShader->m_propList.Value(pPropStrs[i]))
		{
			const std::string prefixedMaterialPath = GetPrefixedMaterialPath(*pPath);
			if (prefixedMaterialPath.empty())
				return;

			m_dependenciesMaterialsList.push_back(prefixedMaterialPath);
		}
	}
}

void MaterialExpanderTransform::AddExtraMaterialDependency(const libdb2::Material& material)
{
	const std::string prefixedMaterialPath = GetPrefixedMaterialPath(material.m_name);
	if (prefixedMaterialPath.empty())
		return;

	m_dependenciesExtraMaterialList.push_back(prefixedMaterialPath);
}

void MaterialExpanderTransform::AddReplacementMaterialDependency(const ITSCENE::CgfxShader* pShader)
{
	const std::string prefixedMaterialPath = GetPrefixedMaterialPath(pShader->GetShaderPath());
	if (prefixedMaterialPath.empty())
		return;

	vector<string>::const_iterator it = std::find_if(m_dependenciesExtraMaterialList.cbegin(), m_dependenciesExtraMaterialList.cend(), [&prefixedMaterialPath](const string& extraMaterial) { return extraMaterial == prefixedMaterialPath; });
	if (it != m_dependenciesExtraMaterialList.cend())
		return;

	AddMaterialDependency(pShader);
}

void MaterialExpanderTransform::RegisterDependencies(const std::string& diskPath)
{
	const string transformOutput = GetFirstOutputPath().AsPrefixedPath();
	
	string reference = "";
	const bool isLevel = m_preEvaluateDependencies.GetBool("isLevel");
	if (!isLevel)
	{
		const std::string& actorName = m_preEvaluateDependencies.GetValue("actorName");
		const size_t actorLod = m_preEvaluateDependencies.GetInt("actorLod");

		const libdb2::Actor* pDbActor = libdb2::GetActor(actorName, actorLod);
		if (!pDbActor->m_geometry.m_sceneFile.empty())
		{
			reference = PathPrefix::BAM + pDbActor->m_geometry.m_sceneFile;
		}
	}
	else
	{
		reference = PathPrefix::BAM + m_mayaFilename;
	}
	
	for (const string& materialFile : m_dependenciesMaterialsList)
	{
		m_assetDeps.AddAssetDependency(materialFile.c_str(), reference.c_str(), "material");
	}

	const string extraMatReference = diskPath;
	for (const string& extraMaterialDependency : m_dependenciesExtraMaterialList)
	{
		m_assetDeps.AddAssetDependency(extraMaterialDependency.c_str(), extraMatReference.c_str(), "material");
	}
}

void MaterialExpanderTransform::RemoveDuplicatedShaders(ITSCENE::CgfxShaderList& shadersList, vector<int>& remapShaders)
{
	//make a list of unique shaders and a map from original list to unique list
	ITSCENE::CgfxShaderList uniqueShaderList;
	for (int ii = 0; ii < shadersList.size(); ++ii)
	{
		ITSCENE::CgfxShader* shader = shadersList[ii];
		const string& shaderName = shader->GetShaderPath() + material_exporter::MaterialExporter::GetShaderHash(*shader).AsString();
		ITSCENE::CgfxShaderList::const_iterator it = std::find_if(uniqueShaderList.cbegin(), uniqueShaderList.cend(), [&shaderName](const ITSCENE::CgfxShader* shader){ return shaderName == shader->GetShaderPath() + material_exporter::MaterialExporter::GetShaderHash(*shader).AsString(); });
		if (it != uniqueShaderList.cend())
		{
			int remapIndex = it - uniqueShaderList.cbegin();
			remapShaders.push_back(remapIndex);

			delete shader;
			shadersList[ii] = nullptr;
		}
		else
		{
			remapShaders.push_back(uniqueShaderList.size());
			uniqueShaderList.push_back(shader);
		}
	}

	shadersList.clear();
	shadersList.insert(shadersList.begin(), uniqueShaderList.begin(), uniqueShaderList.end());
}

void MaterialExpanderTransform::InitializeMaterialFlags(std::map<std::string, U32>& matFlags, const ITSCENE::CgfxShaderList& shadersList, matdb::MatDb& matDb)
{
	for (auto& shader : shadersList)
	{
		const std::string* path = shader->m_propList.Value("shaderPath_matflags");
		if (path && !path->empty())
		{
			const matdb::ShaderInfo * info = matDb.GetShaderInfo(*path);
			U32 shaderFlag = (info) ? material_exporter::MaterialExporter::GetMaterialFlags(info) : 0;

			matFlags[*path] = shaderFlag;
			shader->m_propList.Del("shaderPath_matflags");
		}
	}
}

void MaterialExpanderTransform::MaterialDependenciesCollectorCallback(const std::string& resource, const std::string& referencedFrom, const std::string& type, int level)
{
	BuildPath path(resource);
	DiscoveredDependency dep(path, level);
	RegisterDiscoveredDependency(dep);
}

void MaterialExpanderTransform::ExecuteMaterialExpander_Thread(const toolsutils::SafeJobBase *const parameter)
{
	const libdb2::Actor* pDbActor = nullptr;
	const libdb2::Level* pDbLevel = nullptr;

	const bool isLevel = m_preEvaluateDependencies.GetBool("isLevel");
	const bool enableVertexCompression = m_preEvaluateDependencies.GetBool("enableVertexCompression");
	if (!isLevel)
	{
		const std::string& actorName = m_preEvaluateDependencies.GetValue("actorName");
		const size_t actorLod = m_preEvaluateDependencies.GetInt("actorLod");
		pDbActor = libdb2::GetActor(actorName, actorLod);
	}
	else
	{
		const std::string& levelName = m_preEvaluateDependencies.GetValue("levelName");
		pDbLevel = libdb2::GetLevel(levelName);
		if (!pDbLevel->Loaded())
		{
//			delete pDbLevel;
			IABORT("Unknown level %s\n", levelName.c_str());
		}
	}

	unique_ptr<ITSCENE::SceneDb> geoNdb(new ITSCENE::SceneDb());
	LoadGeoNdb(geoNdb.get());

	// Save the shader paths before we go crazy with remapping
	for (auto shader : geoNdb->m_cgfxShaders)
	{
		shader->m_propList.Add("shaderPath_matflags", shader->GetShaderPath());
	}

	std::vector<std::string> autoActors;
	LoadAutoActorsNdb(autoActors);

	std::vector<std::string> removableMaterials;
	LoadRemovableMaterialsNdb(removableMaterials);

	std::vector<std::string> instanceMaterialRemaps;
	LoadMaterialRemapNdb(instanceMaterialRemaps);
	std::vector<std::string> relevantRemaps;
	std::string autoActorName;
	for (UINT i = 0; i < instanceMaterialRemaps.size(); i += 2)
	{
		if (instanceMaterialRemaps[i + 1] == m_mayaFilename)
		{
			relevantRemaps.push_back(instanceMaterialRemaps[i]);
			size_t fgPos = instanceMaterialRemaps[i].find("(fg)");	// actor name follows
			if (fgPos != std::string::npos && autoActorName == "")
				autoActorName = instanceMaterialRemaps[i].substr(fgPos + sizeof("(fg)") - 1, instanceMaterialRemaps[i].find_last_of(":") - instanceMaterialRemaps[i].find(")") - 1);	// sizeof("(fg)") is 5, including terminator
		}
	}

	AttributeCompressionSettingsTable attribCompressionSettingsTable;
	LoadAttributeCompressionSettingsNdb(attribCompressionSettingsTable);

	InstanceClusters clusters;
	LoadInstanceClustersNdb(clusters);

	if (isLevel)
	{
		for (UINT i = 0; i < autoActors.size(); i += 2)
		{
			if (autoActors[i] == m_mayaFilename)	// all instances of this prototype are auto actors, so remove their shaders since they exists in the actor, to save memory
			{
				geoNdb->m_cgfxShaders.clear();
				break;
			}
		}

		for (UINT i = 0; i < removableMaterials.size(); i += 2)
		{
			if (removableMaterials[i + 1] == m_mayaFilename)
			{
				for (UINT iShader = 0; iShader < geoNdb->m_cgfxShaders.size(); iShader++)
				{
					if (geoNdb->m_cgfxShaders[iShader]->GetShaderPath() == removableMaterials[i])
					{
						geoNdb->m_cgfxShaders[iShader]->m_propList.Add("RemovedRemapSrc", "true");
						break;
					}
				}
			}
		}
	}

	const libdb2::Actor* pDbAutoActor = nullptr;
	if (autoActorName != "")
	{
		pDbAutoActor = libdb2::GetActor(autoActorName, size_t(0));
		RegisterDiscoveredDependency(BuildPath(pDbAutoActor->DiskPath()), 0);
	}

	if (geoNdb->m_cgfxShaders.size() > 0)
		RemovedUnusedShaders(geoNdb.get());

	matdb::MatDb matdbCopy = *m_pContext->m_toolParams.m_matdb;
	bool makeEverythingWhite =  m_pContext->m_toolParams.m_makeEverythingWhite;

	const libdb2::MaterialRemapList* pMatRemapList = nullptr;
	const libdb2::MaterialList* pExtraMatList = nullptr;
	material_exporter::FeatureFilter filters;
	std::string diskPath;

	if (pDbActor)
	{
		pMatRemapList = &pDbActor->m_materialRemapList;
		pExtraMatList = &pDbActor->m_extraMaterialsList;

		bool vertCompression = false;
		if (enableVertexCompression)
		{
			GameFlags gameFlags = GameFlagsFromJsonString(pDbActor->m_gameFlagsInfo.m_gameFlags,
														  m_preEvaluateDependencies.GetValue("actorName"));
			vertCompression = !HasSoftwareAttributeCompressionLevel(gameFlags) ||
							  ReadSoftwareAttributeCompressionLevel(gameFlags) > kSwAttribCompressionNone;
		}

		InitializeFeatureFilter(*pDbActor, vertCompression, filters);

		diskPath = pDbActor->DiskPath();
	}
	else if (pDbLevel)
	{
		pMatRemapList = &pDbLevel->m_materialRemapList;
		pExtraMatList = &pDbLevel->m_extraMaterialsList;

		bool vertCompression = false;
		if (enableVertexCompression)
		{
			if (attribCompressionSettingsTable.m_prototypesWithSwCompression.find(m_mayaFilename) != attribCompressionSettingsTable.m_prototypesWithSwCompression.end())
			{
				vertCompression = true;
			}
		}

		InitializeFeatureFilter(*pDbLevel, vertCompression, filters);

		diskPath = pDbLevel->DiskPath();
	}

	InstanceClusters::PrototypeInfo clusterPrototypeInfo = clusters.GetPrototypeInfo(m_mayaFilename);
	if (clusterPrototypeInfo.m_numClusteredInstances > 0)
	{
		filters.AddFeatureFilter({ "ENABLE_HARDWARE_INSTANCING", material_exporter::FeatureFilter::kFilterAdd });
	}
	else
	{
		filters.AddFeatureFilter({ "ENABLE_HARDWARE_INSTANCING", material_exporter::FeatureFilter::kFilterRemove });
	}

	m_shadersToBuild = CollectActorShaders(*geoNdb,
		*pMatRemapList,
		*pExtraMatList,
		relevantRemaps,
		makeEverythingWhite,
		matdbCopy);

	if (pDbAutoActor)
	{
		const libdb2::GeometryTag_ShaderFeatureList &shaderFeatrueList = pDbAutoActor->m_geometry.m_shaderfeature;
		for (libdb2::GeometryTag_ShaderFeatureList::const_iterator it = shaderFeatrueList.begin(), itEnd = shaderFeatrueList.end(); it != itEnd; ++it)
		{
			const libdb2::GeometryTag_ShaderFeature &shaderFeature = (*it);
			filters.AddFeatureFilter(std::make_pair(shaderFeature.m_value, material_exporter::FeatureFilter::kFilterAddFg));
		}
	}

	// Populates the shaders propList with information found in the material database
	pipeline3::InitShadersWithMaterialDatabase(m_shadersToBuild, filters, &matdbCopy, false);

	//register post dependencies again here cause InitShadersWithMaterialDatabase will replace unknown material with default material
	RegisterMaterialsInPostEvalDependencies(m_shadersToBuild, matdbCopy);

	RemoveDuplicatedShaders(m_shadersToBuild, m_shaderToUniqueShader);

	InitializeMaterialFlags(m_matFlags, m_shadersToBuild, matdbCopy);

	RegisterDependencies(diskPath);

//	delete pDbActor;
//	delete pDbLevel;
}

void MaterialExpanderTransform::ExecuteMaterialExpander()
{
	ExecuteMaterialExpander_Thread(nullptr);
}

WorkItemHandle MaterialExpanderTransform::ExecuteAsyncMaterialExpander()
{
	return toolsutils::QueueWorkItem([this](const toolsutils::SafeJobBase *const job) { ExecuteMaterialExpander_Thread(job); }, "MaterialExpand", nullptr);
}

BuildTransformStatus MaterialExpanderTransform::Internal_ResumeEvaluation()
{
	WriteOutput(m_shadersToBuild, m_shaderToUniqueShader);
	WriteMatFlags(m_matFlags);

	for (auto shader : m_shadersToBuild)
	{
		delete shader;
	}

	m_shadersToBuild.clear();
	m_shaderToUniqueShader.clear();
	m_matFlags.clear();

	return BuildTransformStatus::kOutputsUpdated;
}