#pragma once

#include "build-transform.h"

#include <string>
#include <utility>
#include <vector>

#include "icelib/itscene/itscenetypes.h"
#include "tools/libs/libdb2/db2-commontypes.h"

namespace ITSCENE
{
	class CgfxShader;
	class SceneDb;
}

namespace libdb2
{
	class Actor;
	class Level;
	class Material;
}

namespace matdb
{
	class MatDb;
}

class AttributeCompressionSettingsTable;

class InstanceClusters;

class BuildTransformContext;
struct DiscoveredDependency;

class MaterialExpanderTransform : public BuildTransform
{
public:
	MaterialExpanderTransform(const BuildTransformContext *const context, 
							const std::string& actorName, 
							size_t actorLod);
	MaterialExpanderTransform(const BuildTransformContext *const context, 
							const std::string& levelName, 
							const std::string& mayaFilename);
	~MaterialExpanderTransform();

	BuildTransformStatus Evaluate() override;

	BuildTransformStatus ResumeEvaluation(const SchedulerResumeItem& resumeItem) override;

private:
	void PopulatePreEvalDependencies(const std::string& actorName,
									size_t actorLod,
									const std::string& levelName,
									const std::string& mayaFilename);

	void LoadGeoNdb(ITSCENE::SceneDb* actorGeoNdb) const;
	void LoadMaterialRemapNdb(std::vector<std::string>& instanceMaterialRemaps) const;
	void LoadAutoActorsNdb(std::vector<std::string>& autoActors) const;
	void LoadRemovableMaterialsNdb(std::vector<std::string>& removableMaterials) const;
	void LoadAttributeCompressionSettingsNdb(AttributeCompressionSettingsTable& attribCompressionSettingsTable) const;
	void LoadInstanceClustersNdb(InstanceClusters& clusters) const;

	std::vector<ITSCENE::CgfxShader*> CollectActorShaders(const ITSCENE::SceneDb& dbScene,
														const libdb2::MaterialRemapList& matRemapList,
														const libdb2::MaterialList& extraMatList,
														const std::vector<std::string>& instanceMaterialRemaps,
														bool makeEverythingWhite,
														matdb::MatDb& matDbCopy);

	void WriteOutput(const std::vector<ITSCENE::CgfxShader*>& shadersList, const std::vector<int>& shaderToUniqueShaderMap);
	void WriteMatFlags(const std::map<std::string, U32>& matFlags);

	void AddMaterialDependency(const ITSCENE::CgfxShader* pShader);
	void AddExtraMaterialDependency(const libdb2::Material& material);
	void AddReplacementMaterialDependency(const ITSCENE::CgfxShader* pShader);

	void RegisterDependencies(const std::string& diskPath);
	void RegisterMaterialsInPostEvalDependencies(const std::vector<ITSCENE::CgfxShader*>& shadersList, matdb::MatDb& matDb);

	void RemoveDuplicatedShaders(ITSCENE::CgfxShaderList& shadersList, std::vector<int>& shaderToUniqueShader);
	void InitializeMaterialFlags(std::map<std::string, U32>& matFlags, const ITSCENE::CgfxShaderList& shadersList, matdb::MatDb& matDb);

	void MaterialDependenciesCollectorCallback(const std::string& resource, const std::string& referencedFrom, const std::string& type, int level);

	void ExecuteMaterialExpander_Thread(const toolsutils::SafeJobBase *const parameter);
	void ExecuteMaterialExpander();
	WorkItemHandle ExecuteAsyncMaterialExpander();

	BuildTransformStatus Internal_ResumeEvaluation();

	const std::string m_mayaFilename;
	
	std::vector<std::string> m_dependenciesMaterialsList;
	std::vector<std::string> m_dependenciesExtraMaterialList;

	std::vector<ITSCENE::CgfxShader*> m_shadersToBuild;
	std::vector<int> m_shaderToUniqueShader;
	std::map<std::string, U32> m_matFlags;
};