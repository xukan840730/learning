/*
* Copyright (c) 2018 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Interactive Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "tools/pipeline3/build/build-transforms/build-transform.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"

/// --------------------------------------------------------------------------------------------------------------- ///
namespace libdb2
{
	class Actor;
	class Anim;
}

//--------------------------------------------------------------------------------------------------------------------//
class BuildTransform_ExportLightAnims : public BuildTransform
{
public:
	BuildTransform_ExportLightAnims(const BuildTransformContext* pContext, int32_t startFrame, int32_t endFrame, U32 sampleRate, bool isLevel = false) : 
		BuildTransform("ExportLightAnims", pContext)
		, m_maya3NdbJobId(0)
		, m_startFrame(startFrame)
		, m_endFrame(endFrame)
		, m_sampleRate(sampleRate)
		, m_isLevel(isLevel)
	{
		PopulatePreEvalDependencies();
	}
	BuildTransformStatus Evaluate() override;
	BuildTransformStatus ResumeEvaluation(const SchedulerResumeItem& resumeItem) override;
	
private:
	void PopulatePreEvalDependencies();
	void KickExportLightAnimsJob(Farm& farm, const ToolParams& tool);

	FarmJobId m_maya3NdbJobId;
	int32_t m_startFrame;
	int32_t m_endFrame;
	U32 m_sampleRate;
	bool m_isLevel;
};

//--------------------------------------------------------------------------------------------------------------------//
class BuildTransform_LightAnimsBuildSpawner : public BuildTransform
{
public:
	BuildTransform_LightAnimsBuildSpawner(const BuildTransformContext *const pContext, 
										const std::string& actorName) 
										: BuildTransform("LightAnimSpawner", pContext)
										, m_actorName(actorName)
	{
		SetDependencyMode(DependencyMode::kIgnoreDependency);
		m_preEvaluateDependencies.SetString("actorName", m_actorName);
	}
	
	BuildTransformStatus Evaluate() override;
	
private:
	// doesn't work for general ndb to ma file name conversion, only for light anims which has naming convention
	const libdb2::Anim* SceneNdbToAnimDb(const std::string& ndbPath, const libdb2::Actor* pDbActor) const;

	const std::string m_actorName;
};

//--------------------------------------------------------------------------------------------------------------------//
class BuildTransform_LevelLightAnimTransformSpawner : public BuildTransform
{
public:
	BuildTransform_LevelLightAnimTransformSpawner(const BuildTransformContext *const pContext,
		const std::string& levelName)
		: BuildTransform("LevelLightAnimTransformSpawner", pContext)
		, m_levelName(levelName)
	{
		SetDependencyMode(DependencyMode::kIgnoreDependency);
	}

	BuildTransformStatus Evaluate() override;

private:
	const std::string m_levelName;
};

//--------------------------------------------------------------------------------------------------------------------//
class BuildTransform_LevelLightAnimDispatcher : public BuildTransform
{
public:
	BuildTransform_LevelLightAnimDispatcher(const BuildTransformContext *const pContext)
		: BuildTransform("LevelLightAnimDispatcher", pContext)
	{
		PopulatePreEvalDependencies();
	}

	BuildTransformStatus Evaluate() override;

private:
	void PopulatePreEvalDependencies();
};

//--------------------------------------------------------------------------------------------------------------------//
U32 BuildModuleLightAnim_Configure(const BuildTransformContext *const pContext, 
									const libdb2::Actor *const pDbActor);

extern const std::string UnderscoresToDashes(const std::string &path);