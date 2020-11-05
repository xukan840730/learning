/*
 * Copyright (c) 2018 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/build-transforms/build-transform-eff-generator.h"

#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/build/tool-params.h"

#include "tools/libs/writers/effect-table.h"

//#pragma optimize("", off) // uncomment when debugging in release mode

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_EffGenerator::BuildTransform_EffGenerator(const ToolParams& params)
	: BuildTransform("EffGenerator"), m_toolParams(params)
{
	SetDependencyMode(BuildTransform::DependencyMode::kIgnoreDependency);
}

/// --------------------------------------------------------------------------------------------------------------- ///
std::string BuildTransform_EffGenerator::AddNewEntry(const std::string& skelName, const std::string& animName)
{
	const EffectAnimTable* pTable = EffectMasterTable::GetTable(skelName);

	if (!pTable)
	{
		pTable = EffectMasterTable::LoadEffects(skelName, EffectMasterTable::kEffectsTypeArtGroup);
	}

	std::string effNdbFilename = "";

	const EffectAnim* pFoundEffAnim = pTable ? pTable->GetAnim(animName) : nullptr;

	if (pFoundEffAnim)
	{
		effNdbFilename = GetEffNdbFileName(skelName, animName);

		m_skelAnims[skelName].push_back(animName);

		AddOutput(TransformOutput(BuildPath(effNdbFilename), skelName + FileIO::separator + animName));
	}

	return effNdbFilename;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_EffGenerator::Evaluate()
{
	std::set<std::string> effFileList;

	for (const SkelAnimMapEntry& skelEntry : m_skelAnims)
	{
		const std::string& skelName = skelEntry.first;
		const std::vector<std::string>& animList = skelEntry.second;

		const EffectAnimTable* pTable = EffectMasterTable::GetTable(skelName);

		if (!pTable)
		{
			pTable = EffectMasterTable::LoadEffects(skelName, EffectMasterTable::kEffectsTypeArtGroup);
		}

		for (const std::string& effFile : pTable->GetFiles())
			effFileList.insert(effFile);

		for (const std::string& animName : animList)
		{
			const TransformOutput& output = GetOutput(skelName + FileIO::separator + animName);

			const EffectAnim* pFoundEffAnim = pTable ? pTable->GetAnim(animName) : nullptr;

			if (pFoundEffAnim)
			{
				INOTE_VERBOSE("[%s] %s serializing %d effects to %s\n",
							  skelName.c_str(),
							  animName.c_str(),
							  pFoundEffAnim->GetEffects().size(),
							  output.m_path.AsRelativePath().c_str());

				DataStore::WriteSerializedData(output.m_path, *pFoundEffAnim);
			}
		}
	}

	// Register all eff files as discovered dependencies. This transform always run so technically we don;t need discovered dependencies
	// but it is usefull to debug.
	for (const std::string& effFile : effFileList)
	{
		BuildPath effPath(effFile);
		RegisterDiscoveredDependency(effPath, 0);
	}

	return BuildTransformStatus::kOutputsUpdated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
std::string BuildTransform_EffGenerator::GetEffNdbFileName(const std::string& skelName, const std::string& animName) const
{
	const std::string effNdbFilename = m_toolParams.m_buildPathEffNdb + skelName + FileIO::separator
									   + animName + ".eff.ndb";

	return effNdbFilename;
}
