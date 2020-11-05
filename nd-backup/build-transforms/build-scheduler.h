/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "tools/libs/toolsutil/lock-session.h"
#include "tools/pipeline3/build/build-transforms/build-structs.h"
#include "tools/pipeline3/build/build-transforms/build-transform.h"
#include "tools/pipeline3/build/tool-params.h"
#include "tools/pipeline3/build/util/source-asset-view.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ToolParams;
class BuildTransformContext;

namespace libdb2
{
	class Level;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransformSchedulerInfo
{
public:
	BuildTransformSchedulerInfo()
	{
		m_status = BuildTransformStatus::kWaitingInputs;
		m_sourceAssetViewStatus = SourceAssetViewStatus::kIgnoreSourceAssetView;
		m_evaluateStartTime = 0.0f;
		m_evaluateEndTime = 0.0f;
		m_resumeStartTime = 0.0f;
		m_resumeEndTime = 0.0f;
		m_startOrder = 0;
		m_farmExecutionTime = -1;
		m_completionOrder = 0;
		m_validate = false;
	}
	BuildTransformStatus m_status;
	SourceAssetViewStatus m_sourceAssetViewStatus;
	float m_evaluateStartTime;
	float m_evaluateEndTime;
	float m_resumeStartTime;
	float m_resumeEndTime;
	time_t m_farmExecutionTime;
	int m_startOrder;
	size_t m_completionOrder;
	IMsg::CaptureBuffer m_stdOutAndErr;
	SchedulerResumeItem m_resumeItem;

	bool m_validate;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct BuildSchedulerConfig
{
	BuildSchedulerConfig();
	BuildSchedulerConfig(const ToolParams& tool);
	bool m_readFromBlobStore;
	bool m_writeToBlobStore;
	bool m_useWebStoreOnly;
	bool m_local;
	bool m_validate;
	bool m_noReplicate;
	bool m_replicateManifest;
	std::vector<std::string> m_validateOutputs;
	std::vector<std::string> m_onlyExecuteOutputs;
	bool m_tracingEnabled;
	std::string m_userName;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildScheduler
{
public:

	BuildScheduler(const BuildSchedulerConfig& schedulerConfig);
	~BuildScheduler();

	bool AddBuildTransform(BuildTransform* pXform, const BuildTransformContext* pContext);
	bool AddBuildTransform(BuildTransform* pXform, const std::vector<const BuildTransformContext*>& contextList);

	const std::vector<BuildTransform*>& GetAllTransforms();
	const std::vector<const BuildTransform*>& GetAllTransforms() const;

	BuildStatus Evaluate(const Farm::Configuration& farmConfig, const std::string& commandLine) { return Evaluate(farmConfig, false, commandLine); };
	BuildStatus Evaluate(const Farm::Configuration& farmConfig, bool addSchedulerLog, const std::string& commandLine);
	U64 RegisterFarmWaitItem(BuildTransform* pXform, FarmJobId farmJobId);
	U64 RegisterThreadPoolWaitItem(BuildTransform* pXform, WorkItemHandle workHandle);
	U64 RegisterTransformPoolWaitItem(BuildTransform* pXform, BuildTransform* pWaitedTransform);
	U64 RegisterSnDbsWaitItem(BuildTransform* pXform, const std::string& projectName, const std::string& jobId);

	U64 SubmitFarmJob(BuildTransform* pXform,
					  const std::string& commandLine,
					  unsigned long long requiredMemory,
					  unsigned int numThreads,
					  unsigned int numRetries,
					  bool remotable = false);

	FileDateCache& GetFileDateCache();
	Farm& GetFarmSession();
	const Farm& GetFarmSession() const;

	bool IsBusy() const;
	bool HasErrors() const;
	bool HasValidationErrors() const;

	const ContentHashCollection& GetContentHashCollection() const {	return m_contentHashCollection;	}
	void RegisterContentHash(const BuildPath &path, const DataHash &hash);
	const BuildTransformSchedulerInfo* GetTransformInfo(const BuildTransform* pXform) const;
	const std::vector<const BuildTransformContext*>& GetTransformContexts(const BuildTransform* pXform) const;

	void RegisterAssetContext(const std::string& assetName, AssetType assetType, const BuildTransformContext* pContext);
	const BuildTransformContext* GetAssetContext(const std::string& assetName, AssetType assetType) const;
	const std::map<std::string, const BuildTransformContext*>& GetAssetContexts(AssetType assetType) const;

	const std::vector<const BuildTransform*> GetContextTransforms(const BuildTransformContext* pContext) const;

	SourceAssetView& GetSourceAssetView() { return m_sourceAssetView; }
	const SourceAssetView& GetSourceAssetView() const { return m_sourceAssetView; }

	bool GetFileTime(const BuildTransform* pXform, const BuildPath& path, int64_t& time);

	void EnableInternalLogCapture();
	void DisableInternalLogCapture();

private:

	void ExecuteTransform(BuildTransform* pXform);
	void WakeUpWaitingTransforms();

	bool ShouldForceTransformEvaluation(const BuildTransform* pXform);
	bool CheckDependencies(BuildTransform *pXform, DataHash &postEvalDepHash);	// returns true if pXform requires eval

	bool ResolveFinalDepHash(const BuildTransform* pXform, DataHash& finalDepHash);

	void Log(const char* fmt, ...) const;
	void LogTrace(const char* fmt, ...) const;
	void PrintTransformStatus(const BuildTransform* pXform, BuildTransformStatus status);

	void PopulateInputContentHashes(BuildTransform* pXform) const;
	bool GatherOutputContentHashes(const BuildTransform* pXform);

	void AddSubtreeToContext(const BuildTransformContext *pContext, const BuildTransform *pXform, std::set<const BuildTransform *> &checkedTransforms);
	void PushContextsToParents();

	// Poll the farm and our thread pool to wake up waiting transforms
	bool AreTransformsWaiting() const;
	void CheckFarmJobWaitingXforms();			// The logic in these 3 functions is almost identical
	void CheckThreadPoolWaitingXforms();		// and it's getting subtler than it was.
	void CheckTransformsWaitingXforms();			// These are good candidates for merging/factoring out redundant code
	void CheckSndbsWaitingXforms();

	// Setup/Tear down surrounding a transform evaluation
	void PreTransformEvaluate(BuildTransform* pXform);
	void PostTransformEvaluate(BuildTransform* pXform, BuildTransformStatus status);
	void PreTransformResume(BuildTransform* pXform);
	void PostTransformResume(BuildTransform* pXform, BuildTransformStatus status);

	// Handle build transform return status
	void OnBuildTransformOutputsUpdated(BuildTransform* pXform);

	const DataHash RegisterDependencies(BuildTransform* pXform, BuildTransformSchedulerInfo &transformInfo);

	void OnBuildTransformFailed(BuildTransform* pXform, BuildTransformStatus failStatus = BuildTransformStatus::kFailed);

	bool ReplicateTransformOutputs(const BuildTransform *pXform) const;

	// Scheduling Data
	std::vector<BuildTransform*> m_schedulableXforms;
	std::vector<BuildTransform*> m_newXforms;
	std::vector<BuildTransform*> m_uniqueXforms; //List of unique transforms
	std::map<std::string, BuildTransform*> m_outputToXform;

	std::vector<std::unique_ptr<BuildTransform>> m_Xforms;	//Contains every transforms ever given to the scheduler. We can have duplicates in this lists.

	std::set<std::string> m_registeredOutputs;			// The set of all outputs currently registered. Used to prevent duplicated outputs
	std::set<std::string> m_updatedOutputs;
	std::set<std::string> m_failedOutputs;

	IMsg::CaptureBuffer m_internalLog;

	SourceAssetView m_sourceAssetView;

	// Wait Data
	std::vector<FarmWaitItem>			m_farmWaitingBuildXforms;				// Builds waiting on Farm builds
	std::vector<ThreadPoolWaitItem>		m_threadpoolWaitingBuildXforms;			// Builds waiting on the threadpool
	std::vector<TransformWaitItem>		m_transformsWaitingBuildXforms;			// Builds waiting on 'sub transforms'
	std::vector<SnDbsWaitItem>			m_snDbsWaitingBuildXforms;

	// Common Global State
	FileDateCache m_fileDateCache;
	Farm m_farm;
	ContentHashCollection m_contentHashCollection;
	BuildSchedulerConfig m_config;
	BuildStatus m_buildStatus;

	// Execution tracking
	std::map<const BuildTransform*, BuildTransformSchedulerInfo> m_transformInfo;
	std::map<const BuildTransform*, std::vector<const BuildTransformContext*>> m_transformContexts;
	std::map<std::string, const BuildTransformContext*> m_assetContexts[AssetType::kNumTypes]; //the key is the asset name

	std::map<std::string, int> m_waitingForLockCount;
	int m_startedCount;
	size_t m_completedCount;

	// Validation
	std::map<std::string, std::string> m_outputBucketOwner;		// Mapping of output buckets to transforms to ensure they don't share buckets
};

/// --------------------------------------------------------------------------------------------------------------- ///
// Helper function
const std::vector<std::pair<std::string, int>> ExtractLoadedReferencesFromMaya3ndbOutput(BuildScheduler& buildScheduler,
																						 FarmJobId jobId);
