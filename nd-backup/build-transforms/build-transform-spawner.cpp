/*
* Copyright (c) 2018 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "common/util/timer.h"

#include "tools/pipeline3/toolversion.h"
#include "tools/libs/libdb2/db2-actor.h"
#include "tools/libs/libdb2/db2-level.h"
#include "tools/libs/toolsutil/command-job.h"
#include "tools/libs/toolsutil/daemon-job-helper.h"
#include "tools/libs/toolsutil/ndbdep.h"

#include "tools/pipeline3/build/build-transforms/build-scheduler.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-file-list.h"
#include "tools/pipeline3/build/build-transforms/build-transform-spawner.h"
#include "tools/pipeline3/build/build-transforms/build-transform-material-extract.h"
#include "tools/pipeline3/build/level/build-transform-cluster-instances.h"
#include "tools/pipeline3/build/level/build-transform-maya3ndb.h"
/*#include "tools/pipeline3/build/level/build-transform-bl-geometry.h"*/
#include "tools/pipeline3/build/util/dependency-database-manager.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/options.h"
#include "tools/pipeline3/common/path-prefix.h"
#include "tools/pipeline3/common/path-converter.h"
#include "tools/pipeline3/common/push-to-farm.h"

#include "tools/pipeline3/build/build-transforms/build-transform-texture-build.h"
#include "tools/pipeline3/build/level/build-transform-nav-mesh.h"
#include "tools/pipeline3/build/level/build-transform-bl-collision.h"

//#pragma optimize("", off)

class SpawnDescCreator
{
public:
	virtual BuildTransformSpawnDesc *CreateSpawnDesc() = 0;
};

template <typename T> class TypedSpawnDescCreator : public SpawnDescCreator
{
public:
	TypedSpawnDescCreator<T>(const std::string &name)
	{
	}

	virtual BuildTransformSpawnDesc *CreateSpawnDesc() { return new T; }
};

template <typename U> class TypedSpawnDescCreator<GenericSpawnDesc<U>> : public SpawnDescCreator
{
	std::string m_name;
public:
	TypedSpawnDescCreator<GenericSpawnDesc<U>>(const std::string &name)
		: m_name(name)
	{
	}

	virtual BuildTransformSpawnDesc *CreateSpawnDesc() { return new GenericSpawnDesc<U>(m_name); }
};

std::map<std::string, SpawnDescCreator *> s_spawnDescHandlers;

void InitSpawnDescHandlers()
{
#define REG_SPAWN_DESC(name, tdesc) s_spawnDescHandlers[#name] = new TypedSpawnDescCreator<tdesc>(#name)

	REG_SPAWN_DESC(BuildTransformSpawner, GenericSpawnDesc<BuildTransformSpawner>);
	REG_SPAWN_DESC(BuildTransform_FileList, BuildTransform_FileListSpawnDesc);
	REG_SPAWN_DESC(Maya3NdbIndividualTransform, Maya3NdbIndividualTransformSpawnDesc);
	REG_SPAWN_DESC(Maya3NdbReadDep, Maya3NdbReadDepSpawnDesc);
	REG_SPAWN_DESC(Maya3NdbFullDep, Maya3NdbFullDepSpawnDesc);
	REG_SPAWN_DESC(MaterialExtractTransform, MaterialExtractTransformSpawnDesc);
	REG_SPAWN_DESC(TextureBuildTransform, TextureBuildTransformSpawnDesc);
	REG_SPAWN_DESC(BuildTransform_NavMesh, BuildTransform_NavMeshSpawnDesc);
	REG_SPAWN_DESC(BuildTransform_BlClusterInstances, BuildTransform_BlClusterInstancesSpawnDesc);
	REG_SPAWN_DESC(BuildTransform_BlCollisionExtract, BuildTransform_BlCollisionExtractSpawnDesc);

#undef REG_SPAWN_DESC
}

BuildTransformSpawnDesc *CreateSpawnDesc(const std::string &name)
{
	static bool initialized = false;
	if (!initialized)
	{
		InitSpawnDescHandlers();
		initialized = true;
	}
		
	auto iter = s_spawnDescHandlers.find(name);
	if (iter == s_spawnDescHandlers.end())
		IABORT("Unknown transform spawner type '%s'", name.c_str());

	return iter->second->CreateSpawnDesc();
}

void NdbRead(NdbStream &stream, const char *pSymName, TransformInput &v)
{
	NdbBegin begin(stream, pSymName, "TransformInput");

	int typeInt;
	NdbRead(stream, "m_file", v.m_file);
	NdbRead(stream, "m_nickName", v.m_nickName);
	NdbRead(stream, "m_type", typeInt);

	v.m_type = (TransformInput::Type)typeInt;
}

void NdbWrite(NdbStream &stream, const char *pSymName, const TransformInput &v)
{
	NdbBegin begin(stream, pSymName, "TransformInput");

	NdbWrite(stream, "m_file", v.m_file);
	NdbWrite(stream, "m_nickName", v.m_nickName);
	NdbWrite(stream, "m_type", (int)v.m_type);
}

void NdbRead(NdbStream &stream, const char *pSymName, TransformOutput &v)
{
	NdbBegin begin(stream, pSymName, "TransformOutput");

	NdbRead(stream, "m_path", v.m_path);
	NdbRead(stream, "m_nickName", v.m_nickName);
	NdbRead(stream, "m_flags", v.m_flags);
}

void NdbWrite(NdbStream &stream, const char *pSymName, const TransformOutput &v)
{
	NdbBegin begin(stream, pSymName, "TransformOutput");

	NdbWrite(stream, "m_path", v.m_path);
	NdbWrite(stream, "m_nickName", v.m_nickName);
	NdbWrite(stream, "m_flags", v.m_flags);
}

void BuildTransformSpawnDesc::Read(NdbStream &stream, const char *pSymName)
{
	NdbBegin begin(stream, pSymName, "BuildTransformSpawnDesc");

	int evalModeInt;
	NdbRead(stream, "m_name", m_name);
	NdbRead(stream, "m_evalMode", evalModeInt);
	NdbRead(stream, "m_inputs", m_inputs);
	NdbRead(stream, "m_outputs", m_outputs);

	m_evalMode = (BuildTransform::EvaluationMode)evalModeInt;

	ReadExtra(stream, "extra");
}

void BuildTransformSpawnDesc::Write(NdbStream &stream, const char *pSymName)
{
	NdbBegin begin(stream, pSymName, "BuildTransformSpawnDesc");

	NdbWrite(stream, "m_name", m_name);
	NdbWrite(stream, "m_evalMode", (int)m_evalMode);
	NdbWrite(stream, "m_inputs", m_inputs);
	NdbWrite(stream, "m_outputs", m_outputs);

	WriteExtra(stream, "extra");
}

TransformSpawnList::~TransformSpawnList()
{
	for (int ii = 0; ii < m_list.size(); ii++)
		delete m_list[ii];
}

void TransformSpawnList::AddTransform(BuildTransformSpawnDesc *pDesc)
{
	m_list.push_back(pDesc);
}

void TransformSpawnList::SpawnList(const BuildTransformContext *pContext, BuildTransform::EvaluationMode evalMode) const
{
	for (int i = 0; i < m_list.size(); i++)
	{
		const BuildTransformSpawnDesc *pDesc = m_list[i];

		BuildTransform *pTransform = pDesc->CreateTransform(pContext);

		pTransform->SetInputs(pDesc->m_inputs);
		pTransform->SetOutputs(pDesc->m_outputs);

		// set eval mode with spawn desc, then potentially override with passed in evalMode
		if (pDesc->m_evalMode == BuildTransform::EvaluationMode::kDisabled)
			pTransform->DisableEvaluation();
		else if (pDesc->m_evalMode == BuildTransform::EvaluationMode::kForced)
			pTransform->EnableForcedEvaluation();

		if (pTransform->GetEvaluationMode() == BuildTransform::EvaluationMode::kNormal)
		{
			if (evalMode == BuildTransform::EvaluationMode::kDisabled)
				pTransform->DisableEvaluation();
			else if (evalMode == BuildTransform::EvaluationMode::kForced)
				pTransform->EnableForcedEvaluation();
		}

		pContext->m_buildScheduler.AddBuildTransform(pTransform, pContext);
	}
}

void TransformSpawnList::WriteSpawnList(BuildPath path) const
{
	NdbStream stream;
	stream.OpenForWriting(Ndb::kBinaryStream);

	{
		std::vector<std::string> names;
		for (int i = 0; i < m_list.size(); i++)
		{
			names.push_back(m_list[i]->m_name);
		}

		NdbWrite(stream, "m_names", names);
	}

	{
		NdbBeginArray begin(stream, "m_list", "std::vector", Ndb::kMultiType, m_list.size());
		for (int i = 0; i < m_list.size(); i++)
		{
			m_list[i]->Write(stream, nullptr);
		}
	}
	
	stream.Close();

	DataStore::WriteData(path, stream);
}

// -------------------------------------------------------------------------------- //

BuildTransformSpawnDesc::BuildTransformSpawnDesc(const std::string name)
	: m_name(name)
	, m_evalMode(BuildTransform::EvaluationMode::kNormal)
{
}

BuildTransformSpawner::BuildTransformSpawner(const BuildTransformContext &context)
	: BuildTransform("TransformSpawner")
	, m_context(context)
{
	SetDependencyMode(BuildTransform::DependencyMode::kIgnoreDependency);
}

TransformOutput BuildTransformSpawner::CreateOutputPath(const TransformInput &input)
{
	const std::string prefixedPath = input.m_file.GetBuildPath().AsPrefixedPath();
	const std::string postPrefix = PathConverter::GetPostPrefix(prefixedPath);

	const std::string buildPath = toolsutils::GetCommonBuildPathNew();
	const std::string outputPath = buildPath + BUILD_TRANSFORM_SPAWNER_FOLDER + FileIO::separator + postPrefix;
	return TransformOutput(outputPath);
}

BuildTransformStatus BuildTransformSpawner::Evaluate()
{
	NdbStream spawnListNdb;
	DataStore::ReadData(GetInputFile("SpawnList"), spawnListNdb);

	std::vector<std::string> descNames;
	NdbRead(spawnListNdb, "m_names", descNames);

	spawnListNdb.SetUserData("context", const_cast<BuildTransformContext *>(&m_context));

	TransformSpawnList spawnList;
	{
		NdbBeginArray begin(spawnListNdb, "m_list", "std::vector");
		for (int i = 0; i < descNames.size(); i++)
		{
			BuildTransformSpawnDesc *pDesc = CreateSpawnDesc(descNames[i]);
			pDesc->Read(spawnListNdb, nullptr);

			spawnList.AddTransform(pDesc);
		}
	}

	spawnList.SpawnList(&m_context, GetEvaluationMode());
	
	DataStore::WriteData(GetOutputs()[0].m_path, "");

	return BuildTransformStatus::kOutputsUpdated;
}
