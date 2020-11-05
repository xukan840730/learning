/*
* Copyright (c) 2018 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform.h"

struct BuildTransformSpawnDesc
{
	std::string m_name;
	BuildTransform::EvaluationMode m_evalMode;
	std::vector<TransformInput> m_inputs;
	std::vector<TransformOutput> m_outputs;

	BuildTransformSpawnDesc(const std::string name);
	virtual ~BuildTransformSpawnDesc() {}

	void Read(NdbStream &stream, const char *pSymName);
	void Write(NdbStream &stream, const char *pSymName);

	virtual BuildTransform *CreateTransform(const BuildTransformContext *pContext) const = 0;
	virtual void ReadExtra(NdbStream &stream, const char *pSymData) {}
	virtual void WriteExtra(NdbStream &stream, const char *pSymData) {}
};

template <typename T> struct GenericSpawnDesc : public BuildTransformSpawnDesc
{
	GenericSpawnDesc(const std::string &name)
		: BuildTransformSpawnDesc(name)
	{
	}

	virtual BuildTransform *CreateTransform(const BuildTransformContext *pContext) const override
	{
		return new T(*pContext);
	}
};

#define NEW_GENERIC_SPAWN_DESC(name) new GenericSpawnDesc<name>(#name)

class TransformSpawnList
{
	std::vector<BuildTransformSpawnDesc *> m_list;

public:
	~TransformSpawnList();

	static int Version() { return 5; }

	void AddTransform(BuildTransformSpawnDesc *pDesc);		// takes ownership of pDesc

	void SpawnList(const BuildTransformContext *pContext, BuildTransform::EvaluationMode evalMode) const;
	void WriteSpawnList(BuildPath path) const;
};

class BuildTransformSpawner : public BuildTransform
{
	const BuildTransformContext &m_context;

public:
	BuildTransformSpawner(const BuildTransformContext &context);

	static TransformOutput CreateOutputPath(const TransformInput &input);

	virtual BuildTransformStatus Evaluate() override;
};