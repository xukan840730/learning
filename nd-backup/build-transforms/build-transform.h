/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "common/imsg/msg.h"

#include "tools/libs/winutil/filedatecache.h"

#include "tools/libs/toolsutil/farm.h"
#include "tools/libs/toolsutil/threadpool-helper.h"
#include "tools/libs/toolsutil/simple-dependency.h"

#include "tools/pipeline3/common/blobs/build-file.h"

#include "icelib/ndb/ndbmemorystream-helpers.h"		// DataHash
#include "tools/pipeline3/build/build-transforms/contenthash-collection.h"
#include "tools/pipeline3/build/build-transforms/build-structs.h"
#include "tools/pipeline3/build/build-transforms/build-scheduler-wait-item.h"
#include "tools/pipeline3/build/build-transforms/build-transform-input-output.h"
#include "tools/pipeline3/build/util/dependency-database-manager.h"

#include <vector>
#include <string>
#include <exception>

//#pragma optimize("", off)
/// --------------------------------------------------------------------------------------------------------------- ///
class BuildScheduler;
class BuildTransformContext;

struct DiscoveredDependency
{
	DiscoveredDependency() : m_depthLevel(0)	{}
	DiscoveredDependency(const BuildPath& path, int depthLevel) : m_path(path), m_depthLevel(depthLevel){ }

	bool operator < (const DiscoveredDependency& rhs) const
	{
		return m_path.AsPrefixedPath() < rhs.m_path.AsPrefixedPath();
	}

	BuildPath m_path;
	int m_depthLevel;
};


/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform
{
public:
	enum class EvaluationMode
	{
		kNormal,
		kDisabled,
		kForced,
	};

	enum class DependencyMode
	{
		kDependencyChecked,
		kIgnoreDependency,
	};

	BuildTransform(const std::string& typeName, const BuildTransformContext* pContext = nullptr)
		: m_pContext(pContext)
		, m_typeName(typeName)
		, m_dependencyMode(DependencyMode::kDependencyChecked)
		, m_evaluationMode(EvaluationMode::kNormal)
		, m_hasUpdatedOutputs(false)
		, m_scheduler(nullptr)
	{}

	virtual ~BuildTransform()
	{}

	// Evaluations - Return 'kOutputsUpdated' if the outputs are, well, updated.
	virtual BuildTransformStatus Evaluate() = 0;				// Transform step. Return 'true' if the outputs are valid
	virtual BuildTransformStatus ResumeEvaluation(const SchedulerResumeItem& resumeItem)					{ return BuildTransformStatus::kOutputsUpdated; }
	virtual void OnJobError() {};

	void SetInput(const TransformInput& input);
	void SetInputs(const std::vector<TransformInput>& inputs);
	void SetOutput(const TransformOutput& output);
	void SetOutputs(const std::vector<TransformOutput>& outputs);

	void AddInput(const TransformInput &input);
	void AddOutput(const TransformOutput &output);

	const std::string& GetTypeName() const						{ return m_typeName; } 
	virtual IMsg::Verbosity GetLogVerbosity() const				{ return IMsg::kVerbose; }

	bool DoesInputExist(const std::string& nickname) const
	{
		for (const auto& input : m_inputs)
		{
			if (input.m_nickName == nickname)
			{
				return true;
			}
		}

		return false;
	}

	const std::vector<TransformInput>& GetInputs() const		{ return m_inputs; };
	const std::vector<TransformOutput>& GetOutputs() const		{ return m_outputs; };

	const TransformInput& GetInput(const std::string& nickname) const
	{
		for (const auto& input : m_inputs)
		{
			if (input.m_nickName == nickname)
				return input;
		}

		IABORT("Input '%s' not found for transform '%s'", nickname.c_str(), GetTypeName().c_str());
		static TransformInput bf;
		return bf;
	}

	bool HasOutput(const std::string& nickname) const
	{
		for (const auto& output : m_outputs)
		{
			if (output.m_nickName == nickname)
				return true;
		}

		return false;
	}

	const TransformOutput& GetOutput(const std::string& nickname) const
	{
		for (const auto& output : m_outputs)
		{
			if (output.m_nickName == nickname)
				return output;
		}

		IABORT("Output '%s' not found for transform '%s'", nickname.c_str(), GetTypeName().c_str());
		static TransformOutput bf;
		return bf;
	}

	const BuildFile& GetInputFile(const std::string& nickname) const
	{
		const TransformInput& input = GetInput(nickname);
		return input.m_file;
	}

	const BuildPath& GetOutputPath(const std::string& nickname) const
	{
		const TransformOutput& output = GetOutput(nickname);
		return output.m_path;
	}

	const TransformInput& GetFirstInput() const		{ return m_inputs[0]; };
	const TransformOutput& GetFirstOutput() const	{ return m_outputs[0]; };
	const BuildFile& GetFirstInputFile() const		{ return m_inputs[0].m_file; };
	const BuildPath& GetFirstOutputPath() const		{ return m_outputs[0].m_path; };

	bool HasAnyOutput() const { return !m_outputs.empty(); }

	std::string GetOutputConfigString() const;

	void AddDepMismatch(const std::string& depMismatch)			{ m_depMismatches.push_back(depMismatch); };
	std::vector<std::string>& GetDepMismatches()				{ return m_depMismatches; };
	const std::vector<std::string>& GetDepMismatches() const	{ return m_depMismatches; };

	void EnableForcedEvaluation()								{ m_evaluationMode = EvaluationMode::kForced; };
	void DisableEvaluation()									{ m_evaluationMode = EvaluationMode::kDisabled; };
	EvaluationMode GetEvaluationMode() const					{ return m_evaluationMode; }
	
	DependencyMode GetDependencyMode() const					{ return m_dependencyMode; }

	static void ParseJobOutput(const std::string& buildOutput, 
							   std::vector<std::string>& warnings, 
							   std::vector<std::string>& errors, 
							   ContentHashCollection& contentHashes);

	void AddErrorMessage(const std::string& errorMsg)					{ m_errorMessages.push_back(errorMsg); }
	const std::vector<std::string>& GetErrorMessages() const			{ return m_errorMessages; }
	bool HasError() const												{ return m_errorMessages.size() != 0; }

	void AddValidationErrorMessage(const std::string& errorMsg)			{ m_validationErrorMessages.push_back(errorMsg); }
	const std::vector<std::string>& GetValidationErrorMessages() const	{ return m_validationErrorMessages; }
	bool HasValidationError() const										{ return m_validationErrorMessages.size() != 0; }

	bool HasUpdatedOutputs() const { return m_hasUpdatedOutputs; }

	toolsutils::SimpleDependency& GetPreEvaluateDependencies()					{ return m_preEvaluateDependencies; }
	const toolsutils::SimpleDependency& GetPreEvaluateDependencies() const		{ return m_preEvaluateDependencies; }
	const std::set<DiscoveredDependency>& GetDiscoveredDependencies() const		{ return m_discoveredDependencies; }

	const DependencyDataBaseManager::AssetDependencies& GetAssetDependencies() const	{ return m_assetDeps; }
	DependencyDataBaseManager::AssetDependencies& GetAssetDependencies()				{ return m_assetDeps; }
	void WriteAssetDependenciesToDataStore(const DataHash& keyHash) const;

protected:
	std::vector<std::string> m_depMismatches;		// Dependency mismatches
	std::vector<std::string> m_errorMessages;
	std::vector<std::string> m_validationErrorMessages;

	toolsutils::SimpleDependency m_preEvaluateDependencies;

	DependencyDataBaseManager::AssetDependencies m_assetDeps;

protected:
	void SetDependencyMode(DependencyMode depMode) { m_dependencyMode = depMode; }
	void SetEvaluationMode(EvaluationMode evalMode) { m_evaluationMode = evalMode; };

	// Register the discovered dependency and at what depth in the recursion it was found.
	// It is IMPORTANT to set the proper depth or dependencies will not be properly tracked
	void RegisterDiscoveredDependency(const BuildPath& depFilePath, int depDepth);
	void RegisterDiscoveredDependency(const DiscoveredDependency& newDependency);
	void RegisterOutputContentHash(const BuildPath& path, const DataHash& contentHash);

	void AddBuildTransform(BuildTransform* newTransform) const;

	uint64_t RegisterThreadPoolWaitItem(WorkItemHandle workHandle);
	uint64_t RegisterFarmWaitItem(FarmJobId jobId);

	Farm& GetFarmSession() const;

protected:
	const BuildTransformContext* m_pContext;

private:
	friend class BuildScheduler;
	void SetScheduler(BuildScheduler* scheduler);

private:
	std::string m_typeName;
	std::vector<TransformInput> m_inputs;
	std::vector<TransformOutput> m_outputs;

	std::set<DiscoveredDependency> m_discoveredDependencies;

	ContentHashCollection m_outputContentHashes;

	DependencyMode m_dependencyMode;
	EvaluationMode m_evaluationMode;

	bool m_hasUpdatedOutputs;

	BuildScheduler* m_scheduler; //that's the scheduler containing this transform
};

inline const BuildPath GetDependenciesFilePath(const BuildTransform* pXform) { return pXform->GetFirstOutputPath() + ".d"; }
inline const BuildPath GetLogFilePath(const BuildTransform* pXform) { return pXform->GetFirstOutputPath() + ".log"; }
inline const BuildPath GetAssetDependenciesFilePath(const BuildTransform* pXform) { return pXform->GetFirstOutputPath() + ".assetd"; }

/// --------------------------------------------------------------------------------------------------------------- ///
// Return the full path of all files found
void GatherFilesInFolderRecursively(std::set<std::string>& foundFiles,
									const std::string& searchPath,
									const std::vector<std::string>& allowedExtensions,
									FileDateCache& fdc);

/// --------------------------------------------------------------------------------------------------------------- ///
// Return a map of all files found where the filename is the key and the full path is the value.
// NOTE: Duplicate filenames is different folders will not be represented in this map!
void GatherFilesInFolderRecursivelyAsMap(std::map<std::string, std::string>& fileMap,
										 const std::string& searchPath,
										 const std::vector<std::string>& allowedExtensions,
										 FileDateCache& fdc);
