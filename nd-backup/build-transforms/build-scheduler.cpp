/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/build-transforms/build-scheduler.h"

#include "common/util/stringutil.h"
#include "common/util/timer.h"

#include "tools/libs/libdb2/db2-level.h"
#include "tools/libs/toolsutil/color-display.h"
#include "tools/libs/toolsutil/strfunc.h"

#include "tools/pipeline3/build/build-transforms/build-transform-context.h"
#include "tools/pipeline3/build/build-transforms/build-transform-scheduler-log.h"
#include "tools/pipeline3/build/util/dependency-database-manager.h"
#include "tools/pipeline3/common/blobs/blob-cache.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/blobs/native-file-storage.h"
#include "tools/pipeline3/toolversion.h"

//#pragma optimize("", off) // uncomment when debugging in release mode

using std::map;
using std::set;
using std::string;
using std::stringstream;
using std::vector;

/// --------------------------------------------------------------------------------------------------------------- ///
BuildScheduler::BuildScheduler(const BuildSchedulerConfig& schedulerConfig)
	: m_config(schedulerConfig)
	, m_buildStatus(BuildStatus::kOK)
	, m_startedCount(0)
	, m_completedCount(0)
	, m_updatedOutputs()
	, m_uniqueXforms()
	, m_Xforms()
{}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildScheduler::~BuildScheduler()
{
	for (int i = 0; i < (int)AssetType::kNumTypes; ++i)
	{
		for (const std::pair<string, const BuildTransformContext*>& context : m_assetContexts[i])
		{
			if (context.second)
				delete context.second;
		}
		m_assetContexts[i].clear();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool BuildScheduler::AddBuildTransform(BuildTransform* pXform, const BuildTransformContext* pContext)
{
	vector<const BuildTransformContext*> contextList;
	contextList.push_back(pContext);

	return AddBuildTransform(pXform, contextList);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ValidateUniqueBucket(const BuildTransform* pXform)
{
	const std::vector<TransformOutput>& outputs = pXform->GetOutputs();
	const std::string xformTypeName = pXform->GetTypeName();

	// Verify that each build transform is writing to unique output folders
	std::string primaryOutputBucket;
	std::map<std::string, std::string> outputBucketOwner;
	for (const TransformOutput& output : outputs)
	{
		const std::string outputBucket = GetBucketFromBuildPath(output.m_path);

		if (primaryOutputBucket.empty())
		{
			primaryOutputBucket = outputBucket;
		}
		else if (primaryOutputBucket != outputBucket)
		{
			IWARN("Transform '%s' has multiple buckets: '%s' and '%s'", xformTypeName.c_str(), primaryOutputBucket.c_str(), outputBucket.c_str());
		}

		auto findIter = outputBucketOwner.find(outputBucket);
		if (findIter != outputBucketOwner.end())
		{
			if (findIter->second == xformTypeName)
				continue;

			if (outputBucket == LEVELDATAFOLDER || outputBucket == PAKFOLDER || outputBucket == ACTORFOLDER)
			{
				// commenting out this warning because artists think it's smoething they did wrong
				IWARN("Transforms '%s' and '%s' are using the same output bucket '%s'", findIter->second.c_str(), xformTypeName.c_str(), outputBucket.c_str());
				continue;
			}

			// We have a conflicting use of a bucket. Please resolve!
			IABORT("Transforms '%s' and '%s' are using the same output bucket '%s'", findIter->second.c_str(), xformTypeName.c_str(), outputBucket.c_str());
		}
		else
		{
			outputBucketOwner[outputBucket] = xformTypeName;
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
bool BuildScheduler::AddBuildTransform(BuildTransform* pXform, const vector<const BuildTransformContext*>& contextList)
{
	// We should have a function AddBuildTransform taking a unique pointer to a transform as parameter. But for now it is good enough.
	m_Xforms.push_back(std::unique_ptr<BuildTransform>(pXform));

//	ValidateUniqueBucket(pXform);
	
	const std::vector<TransformOutput>& outputs = pXform->GetOutputs();
	if (!outputs.empty())
	{
		const TransformOutput &firstOutput = outputs[0];
		auto iter = m_outputToXform.find(firstOutput.m_path.AsPrefixedPath());
		if (iter != m_outputToXform.end())
		{
			// there already exists a transform with this same unique first output path, so this transform is a duplicate.
			// check to make sure the duplicate transform has the same outputs as the original, and if so, add the additional
			// build context to the transform.

			BuildTransform *pExistingTransform = iter->second;

			const std::vector<TransformOutput> &existingOutputs = pExistingTransform->GetOutputs();

			bool allOutputsTheSame = true;
			if (existingOutputs.size() != outputs.size())
			{
				allOutputsTheSame = false;
			}

			if (allOutputsTheSame)
			{
				for (int i = 0; i < existingOutputs.size(); i++)
				{
					if (existingOutputs[i].m_path.AsPrefixedPath() != outputs[i].m_path.AsPrefixedPath())
					{
						allOutputsTheSame = false;
						break;
					}
				}
			}

			if (allOutputsTheSame)
			{
				// otherwise, this is a definite duplicate (all outputs are the same)
				const vector<const BuildTransformContext*>& existingContextList = GetTransformContexts(pExistingTransform);

				vector<const BuildTransformContext*> contextToAddList;

				// check if we have to add a new context to this transform
				for (const BuildTransformContext* pNewContext : contextList)
				{
					bool found = false;
					for (const BuildTransformContext* pExistingContext : existingContextList)
					{
						if (pExistingContext == pNewContext)
						{
							found = true;
							break;
						}
					}

					if (!found)
						contextToAddList.push_back(pNewContext);
				}

				vector<const BuildTransformContext*>& existingTransformContextList = m_transformContexts[pExistingTransform];
				for (const BuildTransformContext* pContextToAdd : contextToAddList)
				{
					existingTransformContextList.push_back(pContextToAdd);
				}

				// upgrade existing eval mode if necessary
				if (pXform->GetEvaluationMode() == BuildTransform::EvaluationMode::kNormal && 
					pExistingTransform->GetEvaluationMode() == BuildTransform::EvaluationMode::kDisabled)
					pExistingTransform->SetEvaluationMode(BuildTransform::EvaluationMode::kNormal);
				else if (pXform->GetEvaluationMode() == BuildTransform::EvaluationMode::kForced && 
					pExistingTransform->GetEvaluationMode() != BuildTransform::EvaluationMode::kForced)
					pExistingTransform->SetEvaluationMode(BuildTransform::EvaluationMode::kForced);

				return false;
			}
			else
			{
				IERR("Multiple instantiations of transform '%s' with same inputs specify different outputs", pXform->GetTypeName().c_str());
				for (int i = 0; i < pExistingTransform->GetOutputs().size(); ++i)
				{
					IERR("Existing Xform Output %d: %s", i, pExistingTransform->GetOutputs()[i].m_path.AsPrefixedPath().c_str());
				}
				for (int i = 0; i < outputs.size(); ++i)
				{
					IERR("New Xform Output %d: %s", i, outputs[i].m_path.AsPrefixedPath().c_str());
				}
				IABORT("Abort!");
			}
		}
	}

	// Ok, it is unique
	m_newXforms.push_back(pXform);
	m_uniqueXforms.push_back(pXform);
	for (const TransformOutput& newOutput : outputs)
		m_outputToXform[newOutput.m_path.AsPrefixedPath()] = pXform;

	pXform->SetScheduler(this);

	// Stat tracking
	m_transformInfo[pXform] = BuildTransformSchedulerInfo();

	for (const BuildTransformContext* pContext : contextList)
		m_transformContexts[pXform].push_back(pContext);

	// Should we validate this transform?
	bool validateTransform = false;
	if (m_config.m_validate)
	{
		validateTransform = true;
	}
	else if (!validateTransform && !m_config.m_validateOutputs.empty())
	{
		// If any of the outputs fall in the category of the validation sets, then validate this transform
		for (const TransformOutput& output : outputs)
		{
			const std::string& prefixPath = output.m_path.AsPrefixedPath();
			for (const std::string& validateOutput : m_config.m_validateOutputs)
			{
				if (prefixPath.find(validateOutput) != std::string::npos)
				{
					validateTransform = true;
					break;
				}
			}

			if (validateTransform)
				break;
		}
	}

	if (validateTransform)
	{
		m_transformInfo[pXform].m_validate = true;
	}

	// Disable all transforms that shouldn't be run
	if (!m_config.m_onlyExecuteOutputs.empty())
	{
		// If any of the outputs fall in the category of the validation sets, then validate this transform
		bool disabledTransform = false;
		for (const TransformOutput& output : outputs)
		{
			const std::string& prefixPath = output.m_path.AsPrefixedPath();
			for (const std::string& onlyExecuteOutput : m_config.m_onlyExecuteOutputs)
			{
				if (prefixPath.find(onlyExecuteOutput) == std::string::npos)
				{
					pXform->DisableEvaluation();
					disabledTransform = true;
					break;
				}
			}

			if (disabledTransform)
				break;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const std::vector<BuildTransform*>& BuildScheduler::GetAllTransforms()
{
	return m_uniqueXforms;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const std::vector<const BuildTransform*>& BuildScheduler::GetAllTransforms() const 
{
	auto ret = (std::vector<const BuildTransform*>*)(&m_uniqueXforms);
	return *ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::CheckSndbsWaitingXforms()
{
	for (SnDbsWaitItem& waitItem : m_snDbsWaitingBuildXforms)
	{
		const SnDbs::JobResult res = SnDbs::GetJobResult(waitItem.m_projectName, waitItem.m_jobId);

		if ((res.m_status == SnDbs::kPending) || (res.m_status == SnDbs::kNotFound))
			continue;

		waitItem.m_complete = true;

		BuildTransform* pWaitingTransform = waitItem.GetWaitingXform();

		SchedulerResumeItem resumeItem(SchedulerWaitItem::kSnDbs, waitItem.GetSequenceId(), nullptr, nullptr, nullptr);

		resumeItem.m_snDbsResult = res;

		BuildTransformSchedulerInfo& info = m_transformInfo[pWaitingTransform];

		info.m_resumeItem = resumeItem;
		info.m_stdOutAndErr.AppendFormat("\n\n=== [ SN-DBS Command on client '%s' Status: %s ] ==================================================\n",
																	   res.m_where.c_str(),
																	   SnDbs::GetJobStatusStr(res.m_status));

		info.m_stdOutAndErr.AppendFormat("  Command: %s\n", res.m_command.c_str());

		if (!res.m_hostName.empty())
		{
			info.m_stdOutAndErr.AppendFormat("  Host: %s (%s)\n", res.m_hostName.c_str(), res.m_hostIp.c_str());
		}

		info.m_stdOutAndErr.AppendFormat("  Start: %s\n", ctime(&res.m_startTime));
		info.m_stdOutAndErr.AppendFormat("  End:   %s\n", ctime(&res.m_endTime));
		info.m_stdOutAndErr.AppendFormat("  Duration:   %0.2f seconds\n", float(res.m_endTime - res.m_startTime) / 1000.0f);

		if (!res.m_stdErr.empty())
		{
			info.m_stdOutAndErr.Append("\n\n=== [ Job Std Err ] ===================================================\n");
			std::vector<std::string> errLines = SplitLines(res.m_stdErr);
			for (const std::string& line : errLines)
			{
				info.m_stdOutAndErr.AppendFormat("ERROR: %s\n", line.c_str());
			}
		}

		info.m_stdOutAndErr.Append("\n\n=== [ Job Std Out ] ===================================================\n");
		info.m_stdOutAndErr.Append(res.m_stdOut);
		info.m_stdOutAndErr.Append("\n\n=======================================================================\n");

		m_newXforms.push_back(pWaitingTransform);
	}

	std::vector<SnDbsWaitItem>::iterator last = std::remove_if(m_snDbsWaitingBuildXforms.begin(), m_snDbsWaitingBuildXforms.end(),
															   [](const SnDbsWaitItem& waitItem) { return waitItem.m_complete; });
	m_snDbsWaitingBuildXforms.resize(last - m_snDbsWaitingBuildXforms.begin());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::CheckFarmJobWaitingXforms()
{
	// Check xforms waiting for the farm
	std::vector<FarmJobId> completedJobs;
	m_farm.getDoneJobs(completedJobs);
	auto curWaitIter = m_farmWaitingBuildXforms.begin();
	while (curWaitIter != m_farmWaitingBuildXforms.end())
	{
		FarmWaitItem waitItem = *curWaitIter;

		if (std::find(completedJobs.begin(), completedJobs.end(), waitItem.GetFarmJobId()) != completedJobs.end())
		{
			auto pWaitingTransform = waitItem.GetWaitingXform();

			curWaitIter = m_farmWaitingBuildXforms.erase(curWaitIter); // [WARNING] remove xform from the waiting list early, because if we reach this point the threadpool workHandle is already invalid
																	   // [WARNING] and any subsequent reuse would yield a crash. It used to br removed at the end of the if
																	   // [WARNING] but the code between the beginning and the end was throwing a non fatal exception, and skipped the removal
	

			// First, let's extract any warnings/errors from the job output
			const Farm::Job& job = m_farm.getJob(waitItem.GetFarmJobId());
			LogTrace("Farm Job Completed - %llu [exitcode: %d]", job.m_jobId, job.m_exitcode);

			std::vector<std::string> warnings;
			std::vector<std::string> errors;
			BuildTransform::ParseJobOutput(job.m_output, warnings, errors, pWaitingTransform->m_outputContentHashes);

			m_transformInfo[pWaitingTransform].m_stdOutAndErr.Append("\n\n=== [ Farm Command on client '");
			m_transformInfo[pWaitingTransform].m_stdOutAndErr.Append(job.m_client);
			m_transformInfo[pWaitingTransform].m_stdOutAndErr.Append("'] ==================================================\n");
			m_transformInfo[pWaitingTransform].m_stdOutAndErr.Append(job.m_command);
			m_transformInfo[pWaitingTransform].m_stdOutAndErr.Append("\n\n=== [ Farm Output - BEGIN ] ===========================================\n");
			m_transformInfo[pWaitingTransform].m_stdOutAndErr.Append(job.m_output);
			m_transformInfo[pWaitingTransform].m_stdOutAndErr.Append("\n\n=== [ Farm Output - END ] =============================================\n");

			// If the job failed, fail the transform immediately
			if (job.m_exitcode)
			{
				// if we have retries, rekick the job and add a new wait item
				bool retrySuccessful = false;

				if (waitItem.GetNumRetries())
				{
					FarmJobId jobId = m_farm.submitJob(waitItem.GetCommandLine(), waitItem.GetReqMemory(), waitItem.GetNumThreads(), false, false, waitItem.GetRemotable());
					if (jobId != FarmJobId::kInvalidFarmjobId)
					{
						retrySuccessful = true;
					
						FarmWaitItem newWaitItem(waitItem.GetWaitingXform(), jobId, waitItem.GetCommandLine(), waitItem.GetReqMemory(), waitItem.GetNumThreads(), waitItem.GetNumRetries() - 1, waitItem.GetRemotable());
						m_farmWaitingBuildXforms.insert(m_farmWaitingBuildXforms.begin(), newWaitItem);

						INOTE("Warning: job failed, retrying\n");
						LogTrace("Build Transform %s [%016X] farm job %lld failed, retrying with jobid %lld", pWaitingTransform->GetTypeName().c_str(), (uintptr_t)pWaitingTransform, waitItem.GetFarmJobId(), jobId.AsU64());
					}
					else
					{
						pWaitingTransform->AddErrorMessage("Error: job retry failed!");
					}
				}

				if (!retrySuccessful)
				{
					//Do not push the errors and warnings from the job to the transform here. OnBuildTransformFailed will do it.

					if (errors.empty())
					{
						pWaitingTransform->AddErrorMessage("Error: Executable returned an error code without printing an error message. Exit code " + toolsutils::ToString(job.m_exitcode));
					}

					pWaitingTransform->OnJobError();

					pWaitingTransform->AddDepMismatch("Farm job failed");
					OnBuildTransformFailed(pWaitingTransform);
				}
			}
			else
			{
				const SchedulerResumeItem resumeItem( SchedulerWaitItem::kFarm, waitItem.GetSequenceId(), &job, nullptr, nullptr );
				m_transformInfo[pWaitingTransform].m_farmExecutionTime = job.m_duration;
				m_transformInfo[pWaitingTransform].m_resumeItem = resumeItem;
				m_newXforms.push_back(pWaitingTransform);
			}

		}
		else
		{
			curWaitIter++;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::CheckThreadPoolWaitingXforms()
{
	// Check if any jobs have completed
	auto curWaitIter = m_threadpoolWaitingBuildXforms.begin();
	while (curWaitIter != m_threadpoolWaitingBuildXforms.end())
	{
		ThreadPoolWaitItem waitItem = *curWaitIter;

		bool completed = NdThreadPool::WaitWorkItem(waitItem.GetWorkItemhandle(), 0);
		if (completed)
		{
			auto pWaitingTransform = curWaitIter->GetWaitingXform();

			const SchedulerResumeItem resumeItem( SchedulerWaitItem::kThreadJob, waitItem.GetSequenceId(), nullptr, toolsutils::getSafeJob(waitItem.GetWorkItemhandle()), nullptr );
			m_transformInfo[pWaitingTransform].m_resumeItem = resumeItem;
			m_newXforms.push_back(pWaitingTransform);

			curWaitIter = m_threadpoolWaitingBuildXforms.erase(curWaitIter);	// [WARNING] remove xform from the waiting list early, because if we reach this point the threadpool workHandle is already invalid
																				// [WARNING] and any subsequent reuse would yield a crash. It used to br removed at the end of the if
																				// [WARNING] but the code between the beginning and the end was throwing a non fatal exception, and skipped the removal
		}
		else
		{
			curWaitIter++;
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::CheckTransformsWaitingXforms()
{
	auto curWaitIter = m_transformsWaitingBuildXforms.begin();
	while (curWaitIter != m_transformsWaitingBuildXforms.end())
	{
		auto waitItem = *curWaitIter;
		auto waitedOnTransformIt = m_transformInfo.find(waitItem.GetWaitedOnTransform());
		if (waitedOnTransformIt != m_transformInfo.end())
		{
			auto pWaitingTransform = curWaitIter->GetWaitingXform();

			const SchedulerResumeItem resumeItem( SchedulerWaitItem::kTransform, waitItem.GetSequenceId(), nullptr, nullptr, waitItem.GetWaitedOnTransform() );
			m_transformInfo[pWaitingTransform].m_resumeItem = resumeItem;
			m_newXforms.push_back(pWaitingTransform);

			curWaitIter = m_transformsWaitingBuildXforms.erase(curWaitIter);	// [WARNING] remove xform from the waiting list early, because if we reach this point the threadpool workHandle is already invalid
																				// [WARNING] and any subsequent reuse would yield a crash. It used to br removed at the end of the if
																				// [WARNING] but the code between the beginning and the end was throwing a non fatal exception, and skipped the removal
		}
		else
		{
			curWaitIter++;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::Log(const char* fmt, ...) const
{
	va_list args;
	va_start(args, fmt);
	std::string msg;
	toolsutils::s_sprintf_args(msg, fmt, args);
	va_end(args);

	char timeBuf[16];
	sprintf_s(timeBuf, "[%.3f] ", GetSecondsSinceAppStart());

	INOTE_VERBOSE("%s", (timeBuf + msg).c_str());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::LogTrace(const char* fmt, ...) const
{
	if (m_config.m_tracingEnabled)
	{
		va_list args;
		va_start(args, fmt);
		std::string msg;
		toolsutils::s_sprintf_args(msg, fmt, args);
		va_end(args);

		char timeBuf[16];
		sprintf_s(timeBuf, "[%.3f] ", GetSecondsSinceAppStart());

		INOTE_VERBOSE("%s", (timeBuf + msg).c_str());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::PrintTransformStatus(const BuildTransform* pXform, BuildTransformStatus status)
{
	char descBuffer[256];
	const int kStatusAlign = 110;
	const int kTextAlign = 33;
	size_t typeNameLen = pXform->GetTypeName().size();
	snprintf(descBuffer, 256, "[%s%s%s]                     ", CsiBrightWhite(), typeNameLen > 18 ? pXform->GetTypeName().c_str() + (typeNameLen - 18) : pXform->GetTypeName().c_str(), CsiNormal());

	const char* pStatusTextColorStr = CsiGreen();
	const char* pStatusTextStr = "OK";

	switch (status)
	{
	case BuildTransformStatus::kWaitingInputs:
		pStatusTextColorStr = CsiBlue();
		pStatusTextStr = "WAITING";
		break;
	case BuildTransformStatus::kFailed:
		pStatusTextColorStr = CsiRed();
		pStatusTextStr = "FAILED";
		break;
	case BuildTransformStatus::kOutputsUpdated:
		if (pXform->HasValidationError())
		{
			pStatusTextColorStr = CsiRed();
			pStatusTextStr = "FAILED VALIDATION";
		}
		break;
	}

	const TransformOutput& output = pXform->GetFirstOutput();
	const std::string buildDescr = output.m_path.AsRelativePath();
	const size_t buildDescrLen = buildDescr.size();
	int offset = snprintf(descBuffer + kTextAlign, 256 - kTextAlign, "%s", buildDescrLen > 75 ? buildDescr.c_str() + (buildDescrLen - 75) : buildDescr.c_str());
	memset(descBuffer + kTextAlign + offset, ' ', 256 - kTextAlign - offset);
	snprintf(
		descBuffer + kStatusAlign,
		256 - kStatusAlign,
		"%s%s%s",
		pStatusTextColorStr,
		pStatusTextStr,
		CsiNormal());
	INOTE("%s", descBuffer);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::PopulateInputContentHashes(BuildTransform* pXform) const
{
	bool abort = false;
	for (auto& input : pXform->m_inputs)
	{
		if (input.m_type == TransformInput::kSourceFile)
			continue;

		DataHash contentHash;
		bool preSuccess = m_contentHashCollection.GetContentHash(input.m_file.GetBuildPath(), &contentHash);
		if (preSuccess)
		{
			// Update the input file to now also have the proper content hash
			input.m_file = BuildFile(input.m_file.GetBuildPath(), contentHash);
		}

		if (!preSuccess)
		{
			IERR("Unable to find content hash of INPUT file %s for transform %s", input.m_file.AsAbsolutePath().c_str(), pXform->GetTypeName().c_str());
			abort = true;
		}
	}
	if (abort)
	{
		IABORT("");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::RegisterContentHash(const BuildPath &path, const DataHash &hash)
{
	m_contentHashCollection.RegisterContentHash(path, hash);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool BuildScheduler::GatherOutputContentHashes(const BuildTransform* pXform)
{
	bool fail = false;
	for (auto& output : pXform->GetOutputs())
	{
		DataHash contentHash;
		if (pXform->m_outputContentHashes.GetContentHash(output.m_path, &contentHash))
		{
			if (!output.m_path.AsPrefixedPath().empty() && m_contentHashCollection.RegisterContentHash(output.m_path, contentHash))
				LogTrace("Registering content hash for '%s' [%s]", output.m_path.AsPrefixedPath().c_str(), contentHash.AsText().c_str());
		}
		else
		{
			IERR("Unable to find content hash of OUTPUT file %s for transform %s", output.m_path.AsPrefixedPath().c_str(), pXform->GetTypeName().c_str());
			fail = true;
		}
	}
	if (fail)
	{
		IERR("BuildTransform '%s' succeeded but did not register content hashes for its outputs listed above", pXform->GetTypeName().c_str());
		return false;
	}

	return true;
}


/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::AddSubtreeToContext(const BuildTransformContext *pContext, const BuildTransform *pXform, std::set<const BuildTransform *> &checkedTransforms)
{
	checkedTransforms.insert(pXform);

	// add pXform to context
	std::vector<const BuildTransformContext *>& xformContextList = m_transformContexts[pXform];

	bool alreadyPartOfContext = false;
	for (int i = 0; i < xformContextList.size(); i++)
	{
		if (xformContextList[i] == pContext)
		{
			alreadyPartOfContext = true;
			break;
		}
	}

	if (!alreadyPartOfContext)
		xformContextList.push_back(pContext);

	const bool hasAnyTransformFailed = !m_failedOutputs.empty();

	// now add all antecedent xforms to the context
	for (const TransformInput &input : pXform->GetInputs())
	{
		BuildPath inputPath = input.m_file.GetBuildPath();

		if (input.m_type == TransformInput::kHashedResource)
		{
			auto iter = m_outputToXform.find(inputPath.AsPrefixedPath());
			if (iter == m_outputToXform.end())
			{
				if (!hasAnyTransformFailed)
					IERR("Could not find transform that writes '%s'", inputPath.AsPrefixedPath().c_str());
				continue;
			}

			BuildTransform *pParentXform = iter->second;
			if (checkedTransforms.find(pParentXform) != checkedTransforms.end())
				continue;

			AddSubtreeToContext(pContext, pParentXform, checkedTransforms);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::PushContextsToParents()
{
	for (int assetTypeIndex = 1; assetTypeIndex < (int)AssetType::kNumTypes; ++assetTypeIndex)
	{
		for (const auto& entry : m_assetContexts[assetTypeIndex])
		{
			const BuildTransformContext *pContext = entry.second;

			std::set<const BuildTransform *> checkedTransforms;

			for (BuildTransform *pXform : m_uniqueXforms)
			{
				if (checkedTransforms.find(pXform) != checkedTransforms.end())
					continue;

				auto iter = m_transformContexts.find(pXform);
				if (iter == m_transformContexts.end())
					continue;	// should never happen?

				std::vector<const BuildTransformContext *>& xformContextList = iter->second;

				for (int i = 0; i < xformContextList.size(); i++)
				{
					if (xformContextList[i] == pContext)
					{
						AddSubtreeToContext(pContext, pXform, checkedTransforms);
						break;
					}
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::PreTransformEvaluate(BuildTransform* pXform)
{
	auto& transformInfo = m_transformInfo[pXform];
	// Make sure that the folders for all outputs exist to prevent spurious "can't open output file" errors
	for (const auto& output : pXform->GetOutputs())
	{
		if (PathConverter::IsRelativePath(output.m_path.AsAbsolutePath()))
			continue; 
	}

	Log("Evaluating Build Transform - %s [%016X]", pXform->GetTypeName().c_str(), (uintptr_t)pXform);

	// Detach the internal log
	DisableInternalLogCapture();

	// Attach the transform specific log
	IMsg::ChannelId stdoutLogChannelId = IMsg::ChannelManager::Instance()->AttachCaptureBuffer(IMsg::kVstdout, &transformInfo.m_stdOutAndErr);
	IMsg::ChannelId stderrLogChannelId = IMsg::ChannelManager::Instance()->AttachCaptureBuffer(IMsg::kVstderr, &transformInfo.m_stdOutAndErr);
	IMsg::ChannelManager::Instance()->SetVerbosityFilter(stdoutLogChannelId, pXform->GetLogVerbosity());
	IMsg::ChannelManager::Instance()->SetVerbosityFilter(stderrLogChannelId, pXform->GetLogVerbosity());

	time_t now;
	time(&now);
	INOTE_VERBOSE("Transform executed at %s\n", ctime(&now), 0);

	const std::vector<const BuildTransformContext*>& contexts = GetTransformContexts(pXform);
	for (const BuildTransformContext* context : contexts)
	{
		INOTE_VERBOSE("Transform executed in bid %lld", context->GetBuildId());
	}
	INOTE_VERBOSE("\n");

	// Let's make sure that there are no spurious DataStore writes (.d and .log files in particular) that get associated with this transform
	DataStore::GetWrittenDataHashes().clear();

	transformInfo.m_evaluateStartTime = GetSecondsSinceAppStart();
	transformInfo.m_startOrder = 1 + m_startedCount++;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::PostTransformEvaluate(BuildTransform* pXform, BuildTransformStatus status)
{
	auto& transformInfo = m_transformInfo[pXform];
	transformInfo.m_status = status;

	auto& writtenHashes = DataStore::GetWrittenDataHashes();
	for (const auto& entry : writtenHashes)
	{
		pXform->m_outputContentHashes.RegisterContentHash(entry.first, entry.second);
	}
	writtenHashes.clear();

	if (status == BuildTransformStatus::kOutputsUpdated)
	{
		OnBuildTransformOutputsUpdated(pXform);
	}
	else if (status == BuildTransformStatus::kResumeNeeded)
	{
		// ValidateTransformInWaitList(pXform)
	}
	else if (status == BuildTransformStatus::kFailed)
	{
		OnBuildTransformFailed(pXform);
	}

	transformInfo.m_evaluateEndTime = GetSecondsSinceAppStart();

	// Detach after status handling to allow capturing of registration and validation errors
	IMsg::ChannelManager::Instance()->DetachCaptureBuffer(&transformInfo.m_stdOutAndErr);

	// Re-attach the internal log
	EnableInternalLogCapture();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::PreTransformResume(BuildTransform* pXform)
{
	Log("Resuming Evaluation of Build Transform - %s [%016X]", pXform->GetTypeName().c_str(), (uintptr_t)pXform);

	auto& transformInfo = m_transformInfo[pXform];

	// Detach the internal log
	DisableInternalLogCapture();

	// Capture the output of the transform and then parse it to extract the generated content hashes
	IMsg::ChannelId stdoutLogChannelId = IMsg::ChannelManager::Instance()->AttachCaptureBuffer(IMsg::kVstdout, &transformInfo.m_stdOutAndErr);
	IMsg::ChannelId stderrLogChannelId = IMsg::ChannelManager::Instance()->AttachCaptureBuffer(IMsg::kVstderr, &transformInfo.m_stdOutAndErr);
	IMsg::ChannelManager::Instance()->SetVerbosityFilter(stdoutLogChannelId, pXform->GetLogVerbosity());
	IMsg::ChannelManager::Instance()->SetVerbosityFilter(stderrLogChannelId, pXform->GetLogVerbosity());

	// Let's make sure that there are no spurious DataStore writes (.d and .log files in particular) that get associated with this transform
	DataStore::GetWrittenDataHashes().clear();

	transformInfo.m_resumeStartTime = GetSecondsSinceAppStart();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::PostTransformResume(BuildTransform* pXform, BuildTransformStatus status)
{
	auto& transformInfo = m_transformInfo[pXform];
	transformInfo.m_status = status;
	transformInfo.m_completionOrder = m_completedCount++;

	auto& writtenHashes = DataStore::GetWrittenDataHashes();
	for (const auto& entry : writtenHashes)
	{
		pXform->m_outputContentHashes.RegisterContentHash(entry.first, entry.second);
	}
	writtenHashes.clear();

	if (status == BuildTransformStatus::kOutputsUpdated)
	{
		OnBuildTransformOutputsUpdated(pXform);
	}
	else if (status == BuildTransformStatus::kFailed)
	{
		OnBuildTransformFailed(pXform);
	}

	transformInfo.m_resumeEndTime = GetSecondsSinceAppStart();

	// Detach after status handling to allow capturing of registration and validation errors
	IMsg::ChannelManager::Instance()->DetachCaptureBuffer(&transformInfo.m_stdOutAndErr);

	// Re-attach the internal log
	EnableInternalLogCapture();
}


static std::string MyBuildTimeString(int64_t value)
{
	char timeString[128];
	time_t& timeValue = value;
	tm  localtime;
	if (_localtime64_s(&localtime, &timeValue) == 0)
	{
		size_t length = strftime(timeString, 128, "%Y-%m-%d %H:%M:%S", &localtime);
		snprintf(timeString + length, 128 - length, "/(%lld)", value);
		return std::string(timeString);
	}
	else
		return "BAD-TIME-VALUE";
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::OnBuildTransformOutputsUpdated(BuildTransform* pXform)
{
	Log("OnOutputsUpdated - %s", pXform->GetTypeName().c_str());

	pXform->m_hasUpdatedOutputs = true;

	auto& transformInfo = m_transformInfo[pXform];
	transformInfo.m_completionOrder = m_completedCount++;

	//////////////////////////////////////////////////////////////////////
	//Make sure this transform has succeeded							//
	//////////////////////////////////////////////////////////////////////

	if (!pXform->GetErrorMessages().empty())
	{
		IABORT("BuildTransform '%s' claims to have succeeded but it generated errors.", pXform->GetTypeName().c_str());
	}

	if (pXform->GetDependencyMode() == BuildTransform::DependencyMode::kIgnoreDependency)
	{
		// HACK - To allow transforms with 'Ignore Deps' to still have unique hashes that we tie the
		// logs and dep files too we just add the build ID to the dependencies. :)
		const BuildTransformContext* pContext = m_transformContexts[pXform].front();
		pXform->m_preEvaluateDependencies.SetInt("_SchedulerIgnoreDepsBuildId", pContext->GetBuildId());
	}

	if (!GatherOutputContentHashes(pXform))
	{
		pXform->AddErrorMessage("Error: Transform returned an success but did not register all outputs");

		// Ooops, the transform says it completed fine but there is actually an error
		OnBuildTransformFailed(pXform);
		return;
	}

	// For jobs with retries, strip out previous errors for parsing.
	// We still want them in the log but we don't want to fail because of them.

	// Extract the content hashes that were generated (it prints to stdout every time a hash is generated)
	std::vector<std::string> warnings;
	std::vector<std::string> errors;

	std::string jobOutput = transformInfo.m_stdOutAndErr.GetText();

	size_t match = jobOutput.rfind("[ Farm Output - BEGIN ]");
	if (match != std::string::npos)
	{
		jobOutput = jobOutput.substr(match);
	}

	BuildTransform::ParseJobOutput(jobOutput, warnings, errors, pXform->m_outputContentHashes);
	if (!errors.empty())
	{
		for (auto warning : warnings)
			pXform->AddErrorMessage("Warning: " + warning);
		for (auto error : errors)
			pXform->AddErrorMessage("Error: " + error);

		pXform->AddErrorMessage("Error: Transform returned an success but printed an error message");

		// Ooops, the transform says it completed fine but there is actually an error
		OnBuildTransformFailed(pXform);
		return;
	}

	//////////////////////////////////////////////////////////////////////
	//The transform succeeded so I can register dependencies safely		//
	//////////////////////////////////////////////////////////////////////

	const DataHash finalDepHash = RegisterDependencies(pXform, transformInfo);

	// Register all associations
	for (const TransformOutput& output : pXform->GetOutputs())
	{
		// TODO: Register a memory channel and capture all INOTE's and parse here to extract content hashes
		// Register the output files as completed
		m_updatedOutputs.insert(output.m_path.AsPrefixedPath());

		// Ensure that we have a content hash for all outputs 
		if (m_outputToXform.find(output.m_path.AsPrefixedPath()) == m_outputToXform.end())
			m_outputToXform[output.m_path.AsPrefixedPath()] = pXform;

		// Register all output hashes for this transform
		DataHash outputContentHash;
		GetContentHashCollection().GetContentHash(output.m_path, &outputContentHash);

		// Associate the outputs with the complete set of inputs (finalDep)
		RegisterAssociationResult res = DataStore::RegisterAssociation(finalDepHash, output.m_path, outputContentHash);
		if (res == RegisterAssociationResult::kError)
		{
			if (output.m_flags & TransformOutput::kNondeterministic)
			{
				PrintAssociationErrors(IMsg::kWarn);
			}
			else
			{
				PrintAssociationErrors();
				IABORT("");
			}
		}
	}

	PrintTransformStatus(pXform, BuildTransformStatus::kOutputsUpdated);

	Log("OnOutputsUpdated COMPLETED - %s", pXform->GetTypeName().c_str());

	// Write the log
	const BuildPath logFilePath = GetLogFilePath(pXform);
	const std::string& logString = m_transformInfo[pXform].m_stdOutAndErr.GetText();
	DataHash logHash;
	DataStore::WriteData(logFilePath, logString, &logHash, DataStorage::kAllowAsyncUpload);
	const RegisterAssociationResult result = DataStore::RegisterAssociation(finalDepHash, logFilePath, logHash);
	// If there are errors for registering log associations, we don't error out.
	if (result == RegisterAssociationResult::kError)
	{
		PrintAssociationErrors(IMsg::kWarn);
	}
	else if (result == RegisterAssociationResult::kValidationError)
	{
		PrintValidationErrors(IMsg::kWarn);
	}
	m_contentHashCollection.RegisterContentHash(logFilePath, logHash);

	// Write asset dependencies
	pXform->WriteAssetDependenciesToDataStore(finalDepHash);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DataHash BuildScheduler::RegisterDependencies(BuildTransform* pXform, BuildTransformSchedulerInfo &transformInfo)
{
	LogTrace("Dependency registration started");

	const BuildPath& firstOutput = pXform->GetFirstOutputPath();

	// Parse all discovered dependencies, get fileTime or fileTimesHash
	const auto& discoveredDeps = pXform->GetDiscoveredDependencies();
	std::map<std::string, FileIO::FileTime> discoveredDepTimes;
	std::map<std::string, DataHash> discoveredWildcardDepHashes;

	std::vector<std::string> filenamesToCheck;

	for (const auto& dep : discoveredDeps)
	{
		const std::string prefixedPath = dep.m_path.AsPrefixedPath();
		const std::string absPath = dep.m_path.AsAbsolutePath();

		if (prefixedPath.find("*.") != std::string::npos)				// This is a wild card dependency
		{
			const std::string dirPath = FileIO::getDirPath(absPath);
			const std::string allowedExtension = FileIO::getExt(absPath);
			if (allowedExtension == "*" || allowedExtension == "")
			{
				IABORT("You need to use an extension when using wild card dependencies. Not '*.*'");
			}

			std::map<std::string, FileIO::FileTime> fileTimeMap;
			FileIO::readDirFileTimes(dirPath.c_str(), fileTimeMap);
			FileDateCache& fdc = GetFileDateCache();
			std::vector<time_t> fileTimes;
			fileTimes.reserve(fileTimeMap.size());
			for (auto iter = fileTimeMap.begin(); iter != fileTimeMap.end(); iter++)
			{
				const std::string foundFileAbsPath = dirPath + iter->first;
				if (FileIO::getExt(foundFileAbsPath) == allowedExtension)
				{
					fdc.addFileTime(foundFileAbsPath, iter->second);
					fileTimes.push_back(iter->second.mStats.st_mtime);
					INOTE_VERBOSE("Wild card source: %s, %s", foundFileAbsPath.c_str(), MyBuildTimeString(iter->second.mStats.st_mtime).c_str());
				}
			}

			const DataHash fileTimesHash = ToDataHash(&fileTimes[0], fileTimes.size() * sizeof(time_t));
			discoveredWildcardDepHashes[prefixedPath] = fileTimesHash;
			INOTE_VERBOSE("Wild card hash: %s", fileTimesHash.AsText().c_str());
		}
		else															// This is a regular file dependency
		{
			filenamesToCheck.push_back(absPath);
		}
	}

	if (!filenamesToCheck.empty())
	{
		const U64 startTick = TimerGetRawCount();
		std::vector<FileIO::FileTime> fileTimes;
		GetFileDateCache().readMultipleFileTimes(filenamesToCheck, fileTimes);
		const U64 endTick = TimerGetRawCount();

		LogTrace("Read %d file times in %.3f ms", filenamesToCheck.size(), TimerGetTime(startTick, endTick) * 1000.0f);

		for (int ii = 0; ii < filenamesToCheck.size(); ii++)
		{
			const std::string &absPath = filenamesToCheck[ii];
			FileIO::FileTime time = fileTimes[ii];
			const bool success = time.mError == Err::kOK;
			if (!success)
			{
				INOTE_VERBOSE("Discovered input file '%s' could not be read during timestamp extraction", absPath.c_str());
			}
			else
			{
				const std::string prefixedPath = PathConverter::ToPrefixedPath(absPath);
				discoveredDepTimes[prefixedPath] = time;
			}
		}
	}

	int currentDepDepth = 0;
	int dependencyIndex = 0;
	toolsutils::SimpleDependency currDependencies = pXform->GetPreEvaluateDependencies();
	std::map<std::string, std::string> addedDeps;
	do
	{
		addedDeps.clear();

		const std::string currDependenciesJson = AsJsonString(currDependencies);
		const DataHash currDependenciesKeyHash = ToDataHash(currDependenciesJson);	// Dependencies keyHash of the current level, which is the contentHash of the previous level.

		// Now, add all dependencies for this level
		for (const auto& dep : discoveredDeps)
		{
			if (currentDepDepth == dep.m_depthLevel)
			{
				string discoveredDepPrefixedPath = dep.m_path.AsPrefixedPath();
				char keyNameBuffer[1024];
				sprintf(keyNameBuffer, "discoveredDep-%d", dependencyIndex);
				currDependencies.SetInputFilename(keyNameBuffer, discoveredDepPrefixedPath);

				addedDeps[keyNameBuffer] = discoveredDepPrefixedPath;
				dependencyIndex++;
			}
		}

		// Write out the dependencies WITHOUT the additional timestamps (wrote '0' as the time above)
		const std::string updatedDependenciesJson = AsJsonString(currDependencies);
		const BuildPath depthLevelDepFilePath = firstOutput + "." + toolsutils::ToString(currentDepDepth) + ".d";
		DataHash depsNoTimeStampContentHash;
		DataStore::WriteData(depthLevelDepFilePath, updatedDependenciesJson, &depsNoTimeStampContentHash, DataStorage::kAllowCaching | DataStorage::kAllowAsyncUpload);

		// Now, add all the timestamps for this dep level
		for (const auto& addedDep : addedDeps)
		{
			const std::string& keyName = addedDep.first;
			const std::string& discoveredDepPrefixedPath = addedDep.second;

			const auto iter1 = discoveredDepTimes.find(discoveredDepPrefixedPath);
			if (iter1 != discoveredDepTimes.end())
			{
				currDependencies.SetInputFilenameAndTimeStamp(keyName, discoveredDepPrefixedPath, iter1->second.mStats.st_mtime);
			}
			else
			{
				// Was this possibly a wild card dependency?
				const auto iter2 = discoveredWildcardDepHashes.find(discoveredDepPrefixedPath);
				if (iter2 != discoveredWildcardDepHashes.end())
				{
					currDependencies.SetInputFilenameAndHash(keyName, discoveredDepPrefixedPath, iter2->second);
				}
				else
				{
					currDependencies.AddMissingInputFile(keyName, discoveredDepPrefixedPath);
				}
			}
		}

		// We register an association between the time stamp version of level X dependencies with the dependencies
		// for level X + 1 without time stamps.
		RegisterAssociationResult res = DataStore::RegisterAssociation(currDependenciesKeyHash, depthLevelDepFilePath, depsNoTimeStampContentHash);
		if (res == RegisterAssociationResult::kError)
		{
			PrintAssociationErrors();
			IABORT("");
		}

		currentDepDepth++;
	} while (!addedDeps.empty());

	// Register the final dependencies.
	const std::string finalDepsJson = AsJsonString(currDependencies);
	const DataHash finalDepHash = ToDataHash(finalDepsJson);
	const BuildPath depFilePath = GetDependenciesFilePath(pXform);
	m_contentHashCollection.RegisterContentHash(depFilePath, finalDepHash);

	LogTrace("Dependency registration completed");

	return finalDepHash;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::OnBuildTransformFailed(BuildTransform* pXform, BuildTransformStatus failStatus)
{
	auto& transformInfo = m_transformInfo[pXform];
	transformInfo.m_completionOrder = m_completedCount++;
	transformInfo.m_status = failStatus;
	PrintTransformStatus(pXform, failStatus);

	for (const TransformOutput& output : pXform->GetOutputs())
	{
		m_failedOutputs.insert(output.m_path.AsPrefixedPath());
	}

	const std::string& logString = m_transformInfo[pXform].m_stdOutAndErr.GetText();
	std::vector<std::string> warnings;
	std::vector<std::string> errors;
	ContentHashCollection chc;
	BuildTransform::ParseJobOutput(logString, warnings, errors, chc);
	for (auto warning : warnings)
		pXform->AddErrorMessage("Warning: " + warning);
	for (auto error : errors)
		pXform->AddErrorMessage("Error: " + error);

	if (pXform->GetErrorMessages().empty())
	{
		IABORT("BuildTransform '%s' failed but is not reporting why.", pXform->GetTypeName().c_str());
	}
	else
	{
		for each (const auto& error in pXform->GetErrorMessages())
		{
			m_transformInfo[pXform].m_stdOutAndErr.Append(error);
		}
	}
	
	{
		toolsutils::SimpleDependency dependencies = pXform->GetPreEvaluateDependencies();
		const auto& discoveredDeps = pXform->GetDiscoveredDependencies();
		int iDep = 0;
		for (const auto& dep : discoveredDeps)
		{
			char keyNameBuffer[1024];
			sprintf(keyNameBuffer, "discoveredDep-%d", iDep);

			FileIO::FileTime fileTime;
			bool success = GetFileDateCache().readFileTime(dep.m_path.AsAbsolutePath().c_str(), fileTime);
			if (!success)
			{
				INOTE_VERBOSE("Discovered input file '%s' could not be read during timestamp extraction", dep.m_path.AsAbsolutePath().c_str());
				dependencies.AddMissingInputFile(keyNameBuffer, dep.m_path.AsPrefixedPath());
			}
			else
			{
				dependencies.SetInputFilenameAndTimeStamp(keyNameBuffer, dep.m_path.AsPrefixedPath(), fileTime.mStats.st_mtime);
			}
			++iDep;
		}
		const std::string postEvalDepJson = AsJsonString(dependencies);

		// Write the dependencies to the datastore and register it in the content hash collection so it is added to the graph. But don't
		// register an association for the .d file. We don't want associations on .d files for failed transforms.
		const BuildPath depFilePath = GetDependenciesFilePath(pXform);
		DataHash postEvalDepHash;
		DataStore::WriteData(depFilePath, postEvalDepJson, &postEvalDepHash, DataStorage::kAllowCaching | DataStorage::kAllowAsyncUpload);
		m_contentHashCollection.RegisterContentHash(depFilePath, postEvalDepHash);

		// Log
		const BuildPath logFilePath = GetLogFilePath(pXform);
		DataHash logHash;
		DataStore::WriteData(logFilePath, logString, &logHash, DataStorage::kAllowAsyncUpload);

		DataStore::RegisterAssociation(postEvalDepHash, logFilePath, logHash);
		m_contentHashCollection.RegisterContentHash(logFilePath, logHash);

		for (auto& output : pXform->GetOutputs())
		{
			if ((output.m_flags & TransformOutput::kOutputOnFailure) == 0)
			{
				continue;
			}

			DataHash contentHash;
			if (pXform->m_outputContentHashes.GetContentHash(output.m_path, &contentHash))
			{
				if (!output.m_path.AsPrefixedPath().empty() && m_contentHashCollection.RegisterContentHash(output.m_path, contentHash))
					LogTrace("Registering content hash for '%s' [%s]", output.m_path.AsPrefixedPath().c_str(), contentHash.AsText().c_str());
			}
			else
			{
				IERR("Unable to find content hash of OUTPUT file %s for transform %s", output.m_path.AsPrefixedPath().c_str(), pXform->GetTypeName().c_str());
			}
		}

		// Asset dependencies
		pXform->WriteAssetDependenciesToDataStore(postEvalDepHash);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool BuildScheduler::AreTransformsWaiting() const
{
	return !m_farmWaitingBuildXforms.empty() || !m_threadpoolWaitingBuildXforms.empty()
		   || !m_transformsWaitingBuildXforms.empty() || !m_snDbsWaitingBuildXforms.empty();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool HasAnyInputFailed(const BuildTransform* pXform, const std::set<std::string>& failedOutputs)
{
	bool atLeastOneInputHasFailed = false;
	for (const TransformInput& input : pXform->GetInputs())
	{
		if (input.m_type == TransformInput::kSourceFile)
			continue;

		if (std::find(failedOutputs.begin(), failedOutputs.end(), input.m_file.AsPrefixedPath()) != failedOutputs.end())
		{
			atLeastOneInputHasFailed = true;
			break;
		}
	}

	return atLeastOneInputHasFailed;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool BuildScheduler::CheckDependencies(BuildTransform *pXform, DataHash &outFinalDepHash)
{
	LogTrace("Checking dependencies");

	bool requiresEvaluation = false;

	DataHash finalDepHash;
	bool success = ResolveFinalDepHash(pXform, finalDepHash);

	// Check if any of the dependencies failed, hence we need to evaluate the transform
	if (success)
	{
		// Let's verify that all outputs exist in the data store
		LogTrace("Verify the existence of all transform outputs");
		for (const auto& output : pXform->GetOutputs())
		{
			DataHash contentHash;
			const ResolveAssociationResult res = DataStore::ResolveAssociation(finalDepHash, output.m_path, contentHash);
			if (res == ResolveAssociationResult::kFound)
			{
				if (!DataStore::DoesDataExist(BuildFile(output.m_path, contentHash)))
				{
					pXform->AddDepMismatch("Output file '" + output.m_path.AsPrefixedPath() + "' is missing");
					requiresEvaluation = true;
					break;
				}
			}
			else if (res == ResolveAssociationResult::kNotFound)
			{
				pXform->AddDepMismatch("No output was registered for '" + output.m_path.AsPrefixedPath() + "'");
				requiresEvaluation = true;
				break;
			}
			else if (res == ResolveAssociationResult::kError)
			{
				PrintAssociationErrors();
				IABORT("");
			}
		}

		if (!requiresEvaluation)
			outFinalDepHash = finalDepHash;
	}
	else
	{
		requiresEvaluation = true;
	}

	return requiresEvaluation;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void GetInputFilenameKeysWithTimestamp(const std::map<std::string, std::string>& dependencies, std::vector<std::string>& keys)
{
	for (map<string, string>::const_iterator it = dependencies.begin(); it != dependencies.end(); ++it)
	{
		const std::string& key = it->first;
		size_t prefixPos = key.find(toolsutils::SimpleDependency::INPUT_PREFIX);
		if (prefixPos != std::string::npos)
		{
			// Exclude inputs that have a content hash or timestamp
			const size_t contentHashPostfixPos = key.find(toolsutils::SimpleDependency::CONTENTHASH_POSTFIX);
			if (contentHashPostfixPos != std::string::npos)
				continue;
			const size_t timestampPostfixPos = key.find(toolsutils::SimpleDependency::TIMESTAMP_POSTFIX);
			if (timestampPostfixPos != std::string::npos)
				continue;

			// Hack until we have bumped all versions
			const size_t missingFilePos = key.find(toolsutils::SimpleDependency::MISSING_FILE);
			if (missingFilePos != std::string::npos)
				continue;

			const std::string rootKey = key.substr(strlen(toolsutils::SimpleDependency::INPUT_PREFIX));

			// Ignore the keys that have a corresponding content hash key
			const auto it = dependencies.find(toolsutils::SimpleDependency::INPUT_PREFIX + rootKey + toolsutils::SimpleDependency::CONTENTHASH_POSTFIX);
			if (it == dependencies.end())
				keys.push_back(rootKey);
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
static void UpdateInputFileTimestamps(const BuildTransform* pXform, BuildScheduler* pScheduler, toolsutils::SimpleDependency& deps)
{
	// Check all the references
	std::vector<std::string> dependencyNames;
	GetInputFilenameKeysWithTimestamp(deps.GetDependencies(), dependencyNames);

	for (size_t depNameIndex = 0; depNameIndex < dependencyNames.size(); ++depNameIndex)
	{
		const std::string& depName = dependencyNames[depNameIndex];

		// Add the dependency
		const std::string loadedRefFilePath = deps.GetValue(toolsutils::SimpleDependency::INPUT_PREFIX + depName);
		const std::string loadedRefDiskPath = PathConverter::ToAbsolutePath(loadedRefFilePath);

		int64_t time;
		if (pScheduler->GetFileTime(pXform, BuildPath(loadedRefDiskPath), time))
		{
			deps.SetInputFilenameAndTimeStamp(depName, loadedRefFilePath, time);
		}
		else
		{
			// Could this have been a wild card dependency?
			if (loadedRefDiskPath.find("*.") != std::string::npos)
			{
				// This is a wild card dependency
				const std::string absPath = loadedRefDiskPath;
				const std::string dirPath = FileIO::getDirPath(absPath);
				const std::string allowedExtension = FileIO::getExt(absPath);
				if (allowedExtension == "*" || allowedExtension == "")
				{
					IABORT("You need to use an extension when using wild card dependencies. Not '*.*'");
				}

				std::map<std::string, FileIO::FileTime> fileTimeMap;
				FileIO::readDirFileTimes(dirPath.c_str(), fileTimeMap);
				FileDateCache& fdc = pScheduler->GetFileDateCache();
				std::vector<time_t> fileTimes;
				fileTimes.reserve(fileTimeMap.size());
				for (auto iter = fileTimeMap.begin(); iter != fileTimeMap.end(); iter++)
				{
					const std::string foundFileAbsPath = dirPath + iter->first;
					if (FileIO::getExt(foundFileAbsPath) == allowedExtension)
					{
						fdc.addFileTime(foundFileAbsPath, iter->second);
						fileTimes.push_back(iter->second.mStats.st_mtime);
						INOTE_VERBOSE("Wild card source: %s, %s", foundFileAbsPath.c_str(), MyBuildTimeString(iter->second.mStats.st_mtime).c_str());
					}
				}

				DataHash fileTimesHash = ToDataHash(&fileTimes[0], fileTimes.size() * sizeof(time_t));
				deps.SetInputFilenameAndHash(depName, loadedRefFilePath, fileTimesHash);
				INOTE_VERBOSE("Wild card hash: %s", fileTimesHash.AsText().c_str());
			}
			else
			{
				deps.AddMissingInputFile(depName, loadedRefFilePath);
			}
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
bool BuildScheduler::ResolveFinalDepHash(const BuildTransform* pXform, DataHash& finalDepHash)
{
	// Recursively resolve the transform dependencies.
	const BuildPath& firstOutput = pXform->GetFirstOutputPath();

	bool depFullyResolved = false;
	toolsutils::SimpleDependency currDependencies = pXform->GetPreEvaluateDependencies();			// Dependencies content of the previous level.

	// Recursively resolve the dependency hashes
	int currentDepDepth = 0;
	std::string currDependenciesJson;
	DataHash currDependenciesKeyHash;
	std::vector<DataHash> dependencyKeyHashes;
	while (true)
	{
		currDependenciesJson = AsJsonString(currDependencies);
		currDependenciesKeyHash = ToDataHash(currDependenciesJson);

		dependencyKeyHashes.push_back(currDependenciesKeyHash);

		const BuildPath curDepDepthPath = firstOutput + "." + toolsutils::ToString(currentDepDepth) + ".d";

		// Resolve the dependencies contentHash of the current level.
		LogTrace("Resolving dependencies with keyHash (%s)", currDependenciesKeyHash.AsText().c_str());

		// If no dependency file exist, then no additional dependencies were discovered during evaluation and the preEvalHash is used
		DataHash associatedDepKeyHash;
		ResolveAssociationResult result = DataStore::ResolveAssociation(currDependenciesKeyHash, curDepDepthPath, associatedDepKeyHash);
		if (result == ResolveAssociationResult::kError)
		{
			PrintAssociationErrors();
			IABORT("");
		}

		if (result == ResolveAssociationResult::kNotFound)											// If the association was not found, break out.
		{
			LogTrace("No registered association was not found for dep key '%s#%s'. This means that we need to evaluate this transform", curDepDepthPath.AsPrefixedPath().c_str(), currDependenciesKeyHash.AsText().c_str());
			break;
		}

		LogTrace("Resolved dependencies to contentHash (%s)", currDependenciesKeyHash.AsText().c_str());

		if (currDependenciesKeyHash == associatedDepKeyHash)										// If the resolved contentHash is the same as the keyHash, break out.
		{
			// If no discovered dependencies exist, the preEvalDependencies is used.
			LogTrace("Resolved to same depHash. Breaking out.");
			depFullyResolved = true;
			break;
		}

		BuildFile dependencyFile(curDepDepthPath, associatedDepKeyHash);
		std::string nextDepJson;
		DataStore::ReadData(dependencyFile, nextDepJson);

		// Add all missing dependencies and their timestamps.
		if (!FromJsonString(currDependencies, nextDepJson))
		{
			IERR("Failed to parse dependency file with hash '%s'", dependencyFile.AsPrefixedPath().c_str(), dependencyFile.GetContentHash().AsText().c_str());
		}

		LogTrace("Update input timestamps");
		UpdateInputFileTimestamps(pXform, this, currDependencies);				// Hmm... nasty dependencies on SourceView here that requires Transform and Scheduler...
		currentDepDepth++;
	}

	// Only if we fully resolved the final dependency do we return something valid
	if (depFullyResolved)
	{
		finalDepHash = currDependenciesKeyHash;
	}

	return depFullyResolved;
}


/// --------------------------------------------------------------------------------------------------------------- ///
bool BuildScheduler::ShouldForceTransformEvaluation(const BuildTransform* pXform)
{
	return pXform->GetEvaluationMode() == BuildTransform::EvaluationMode::kForced ||
		   m_transformInfo[pXform].m_validate;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool BuildScheduler::GetFileTime(const BuildTransform* pXform, const BuildPath& path, int64_t& time)
{
	auto& transformInfo = m_transformInfo[pXform];
	if (transformInfo.m_sourceAssetViewStatus != SourceAssetViewStatus::kIgnoreSourceAssetView)
	{
		if (m_sourceAssetView.GetFileTime(path, time))
		{
			transformInfo.m_sourceAssetViewStatus = SourceAssetViewStatus::kUsingSourceAssetView;
			return true;
		}
	}

	FileIO::FileTime fileTime;
	if (GetFileDateCache().readFileTime(path.AsAbsolutePath().c_str(), fileTime))
	{
		time = fileTime.mStats.st_mtime;
		return true;
	}

	time = 0;

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::ExecuteTransform(BuildTransform* pXform)
{
	auto& transformInfo = m_transformInfo[pXform];

	Log("Executing Transform of type '%s' - '%s'", pXform->GetTypeName().c_str(), pXform->GetFirstOutputPath().AsPrefixedPath().c_str());

	if (transformInfo.m_status == BuildTransformStatus::kWaitingInputs)
	{
		if (!m_sourceAssetView.IsEmpty() &&
			!ShouldForceTransformEvaluation(pXform) &&
			pXform->GetDependencyMode() == BuildTransform::DependencyMode::kDependencyChecked)
		{
			m_transformInfo[pXform].m_sourceAssetViewStatus = SourceAssetViewStatus::kUseSourceAssetView;
		}

		PopulateInputContentHashes(pXform);

		// Always pre-populate the preEvaluationDependencies with the inputs/outputs of the transform
		{
			toolsutils::SimpleDependency& preEvalDep = pXform->GetPreEvaluateDependencies();

			int index = 1;
			for (const TransformInput& input : pXform->GetInputs())
			{
				if (input.m_type == TransformInput::kSourceFile)
				{
					int64_t fileTime;
					GetFileTime(pXform, input.m_file.GetBuildPath(), fileTime);
					preEvalDep.SetInputFilenameAndTimeStamp("xformInput-" + toolsutils::ToString(index), input.m_file.AsPrefixedPath(), fileTime);
				}
				else
				{
					preEvalDep.SetInputFilenameAndHash("xformInput-" + toolsutils::ToString(index), input.m_file.AsPrefixedPath(), input.m_file.GetContentHash());
				}
				index++;
			}

			index = 1;
			for (const TransformOutput& output : pXform->GetOutputs())
			{
				preEvalDep.SetOutputFilename("xformOutput-" + toolsutils::ToString(index), output.m_path.AsPrefixedPath());
				index++;
			}
		}

		DataHash postEvalDepHash;
		bool requiresEvaluation = false;

		if (pXform->GetDependencyMode() == BuildTransform::DependencyMode::kDependencyChecked)
		{
			bool checkDeps = false;

			if (ShouldForceTransformEvaluation(pXform))
			{
				pXform->AddDepMismatch("Forced update");
				requiresEvaluation = true;
			}
			else if (pXform->GetEvaluationMode() == BuildTransform::EvaluationMode::kDisabled)
			{
				// fall back on dependency checked if global input hash does not exist
				const std::string& xformUniqueStr = pXform->GetOutputConfigString();
				const bool hasPostEvalHash = DataStore::RetrieveDisabledTransformKeyHash(xformUniqueStr, postEvalDepHash); // Find the previous global build preEvalDepHash
				if (hasPostEvalHash)
				{
					pXform->AddDepMismatch("Disabled");
				}
				else
				{
					checkDeps = true;
				}
			}
			else if (pXform->GetEvaluationMode() == BuildTransform::EvaluationMode::kNormal)
			{
				checkDeps = true;
			}

 			size_t currDepMismatchLen = 0;
			if (checkDeps)
			{
 				currDepMismatchLen = pXform->GetDepMismatches().size();
				requiresEvaluation = CheckDependencies(pXform, postEvalDepHash);
			}

			// If we're using the source asset view but still need to evaluate the transform then revert to using real file times.
			if (requiresEvaluation && transformInfo.m_sourceAssetViewStatus == SourceAssetViewStatus::kUsingSourceAssetView)
			{
				transformInfo.m_sourceAssetViewStatus = SourceAssetViewStatus::kIgnoreSourceAssetView;
				toolsutils::SimpleDependency& preEvalDep = pXform->GetPreEvaluateDependencies();

				int index = 1;
				for (const TransformInput& input : pXform->GetInputs())
				{
					if (input.m_type == TransformInput::kSourceFile)
					{
						int64_t fileTime;
						GetFileTime(pXform, input.m_file.GetBuildPath(), fileTime);
						preEvalDep.SetInputFilenameAndTimeStamp("xformInput-" + toolsutils::ToString(index), input.m_file.AsPrefixedPath(), fileTime);
					}
					index++;
				}

				if (checkDeps)
				{
					if (currDepMismatchLen > pXform->GetDepMismatches().size())
					{
						pXform->GetDepMismatches().resize(currDepMismatchLen);			// Why is this here?!?!?! Joe?
					}

					requiresEvaluation = CheckDependencies(pXform, postEvalDepHash);
				}
			}
		}
		else
		{
			pXform->AddDepMismatch("Ignore Deps");
			requiresEvaluation = true;
		}

		// Ok, we have now decided whether or not this transform needs to run...
		if (requiresEvaluation)
		{
			const BuildPath& firstOutput = pXform->GetFirstOutputPath();
			const string firstOutputPrefixedPath = firstOutput.AsPrefixedPath();

			PreTransformEvaluate(pXform);

			BuildStatus buildStatus = BuildStatus::kOK;
			BuildTransformStatus status = BuildTransformStatus::kOutputsUpdated;
			try
			{
				status = pXform->Evaluate();
			}
			catch (IMessageAbortException ex)
			{
				stringstream stream;
				stream << ex.what() << " - " << ex.m_filename << " " << ex.m_lineno;
				pXform->AddErrorMessage(stream.str());
				status = BuildTransformStatus::kFailed;
				buildStatus = BuildStatus::kErrorOccurred;
			}
			catch (ndi::Exception ex)
			{
				pXform->AddErrorMessage(ex.getMessage());
				status = BuildTransformStatus::kFailed;
				buildStatus = BuildStatus::kErrorOccurred;
			}
			catch (std::exception ex)
			{
				stringstream stream;
				pXform->AddErrorMessage(ex.what());
				status = BuildTransformStatus::kFailed;
				buildStatus = BuildStatus::kErrorOccurred;
			}
			catch (...)
			{
				pXform->AddErrorMessage("Exception thrown during Evaluate");
				status = BuildTransformStatus::kFailed;
				buildStatus = BuildStatus::kErrorOccurred;
			}
			PostTransformEvaluate(pXform, status);
		}
		else
		{
			const BuildPath depFilePath = GetDependenciesFilePath(pXform);

			// Register the final dependencies as an association with itself.
			// This is only needed for the look-up of the .d file for the web log
			m_contentHashCollection.RegisterContentHash(depFilePath, postEvalDepHash);

			// If no evaluation is required we still need to propagate the hashes for the outputs.
			// Let's read in the .md5 files and populate the output hashes.
			bool firstOutput = true;
			for (const auto& output : pXform->GetOutputs())
			{
				DataHash contentHash;
				ResolveAssociationResult res = DataStore::ResolveAssociation(postEvalDepHash, output.m_path, contentHash);
				if (res == ResolveAssociationResult::kError)
				{
					PrintAssociationErrors();
					IABORT("");
				}
				else if(res == ResolveAssociationResult::kNotFound)
				{
					IABORT("We detected the transform %s should be skipped but we failed to find an association for %s#%s.", pXform->GetTypeName().c_str(), output.m_path.AsPrefixedPath().c_str(), postEvalDepHash.AsText().c_str());
				}
				pXform->RegisterOutputContentHash(output.m_path, contentHash);

				if (firstOutput)
				{
					// Extract the log hashes as well. Zero-byte logs are not saved.
					const BuildPath logFilePath = GetLogFilePath(pXform);
					res = DataStore::ResolveAssociation(postEvalDepHash, logFilePath, contentHash);
					if (res == ResolveAssociationResult::kError)
					{
						PrintAssociationErrors();
						IABORT("Failed to resolve association for: %s.", logFilePath);
					}
					else if (res == ResolveAssociationResult::kFound)
					{
						m_contentHashCollection.RegisterContentHash(logFilePath, contentHash);
					}

					// Extract the assetd hash.
					const BuildPath assetdFilePath = GetAssetDependenciesFilePath(pXform);
					res = DataStore::ResolveAssociation(postEvalDepHash, assetdFilePath, contentHash);
					if (res == ResolveAssociationResult::kError)
					{
						PrintAssociationErrors();
						IABORT("Failed to resolve association for: %s.", assetdFilePath);
					}
					else if (res == ResolveAssociationResult::kFound)
					{
						m_contentHashCollection.RegisterContentHash(assetdFilePath, contentHash);
					}
				}
				firstOutput = false;
			}

			for (const auto& output : pXform->GetOutputs())
			{
				m_updatedOutputs.insert(output.m_path.AsPrefixedPath());
			}

			if (!GatherOutputContentHashes(pXform))
			{
				// we should never get here. the above code should error out if an output can't be found
				IABORT("Failed to gather output content hashes from skipped transform! Didn't we just resolve these??");
			}

			transformInfo.m_status = BuildTransformStatus::kOutputsUpdated;
		}
	}
	else if (transformInfo.m_status == BuildTransformStatus::kResumeNeeded)
	{
		PreTransformResume(pXform);
		BuildStatus buildStatus = BuildStatus::kOK;
		BuildTransformStatus status = BuildTransformStatus::kOutputsUpdated;
		try 
		{
			const SchedulerResumeItem& resumeItem = transformInfo.m_resumeItem;
			status = pXform->ResumeEvaluation(resumeItem);
		}
		catch(...)
		{
			pXform->AddErrorMessage("Exception thrown during Evaluate");
			status = BuildTransformStatus::kFailed;
			buildStatus = BuildStatus::kErrorOccurred;
		}
		PostTransformResume(pXform, status);
		if (transformInfo.m_resumeItem.m_threadPoolJob)
		{
			delete transformInfo.m_resumeItem.m_threadPoolJob;
			transformInfo.m_resumeItem.m_threadPoolJob = nullptr;
		}
	}

	if (transformInfo.m_status == BuildTransformStatus::kOutputsUpdated)
	{
		// replicate outputs
		if (!m_config.m_noReplicate &&
			!m_config.m_validate &&
			m_config.m_validateOutputs.empty() ||
			m_config.m_replicateManifest)
		{
			ReplicateTransformOutputs(pXform);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::WakeUpWaitingTransforms()
{
	CheckFarmJobWaitingXforms();
	CheckThreadPoolWaitingXforms();
	CheckTransformsWaitingXforms();
	CheckSndbsWaitingXforms();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::EnableInternalLogCapture()
{
	IMsg::ChannelId stdoutLogChannelId = IMsg::ChannelManager::Instance()->AttachCaptureBuffer(IMsg::kVstdout, &m_internalLog);
	IMsg::ChannelId stderrLogChannelId = IMsg::ChannelManager::Instance()->AttachCaptureBuffer(IMsg::kVstderr, &m_internalLog);
	IMsg::ChannelManager::Instance()->SetVerbosityFilter(stdoutLogChannelId, IMsg::kVerbose);
	IMsg::ChannelManager::Instance()->SetVerbosityFilter(stderrLogChannelId, IMsg::kVerbose);
}

void BuildScheduler::DisableInternalLogCapture()
{
	IMsg::ChannelManager::Instance()->DetachCaptureBuffer(&m_internalLog);
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildStatus BuildScheduler::Evaluate(const Farm::Configuration& farmConfig, bool addSchedulerLog, const std::string& commandLine)
{
	if (m_buildStatus != BuildStatus::kOK)
		return m_buildStatus;

	EnableInternalLogCapture();

	Log("Command line : %s", commandLine.c_str());
	Log("Starting Evaluation");
	
	const U64 startTick = TimerGetRawCount();

	m_farm.Configure(farmConfig);

	while (!m_schedulableXforms.empty() || !m_newXforms.empty() || AreTransformsWaiting())
	{
		Log("Scheduling loop iteration begun");

		// Add any new transforms into the scheduler and batch request global 
		// input hashes from all disabled xforms
		if (!m_newXforms.empty())
		{
			Log("Scheduling %d new transforms.", m_newXforms.size());
			m_schedulableXforms.insert(m_schedulableXforms.end(), m_newXforms.begin(), m_newXforms.end());

			m_newXforms.clear();
		}

		// Let's just iterate over the current transforms. Any new additions will be added and processed next iteration
		bool progressWasMade = false;
		std::vector<BuildTransform*>::iterator curIter = m_schedulableXforms.begin();
		while (curIter != m_schedulableXforms.end())
		{
			BuildTransform* pXform = *curIter;

			// First, make sure that not one of our inputs have failed
			const bool atLeastOneInputHasFailed = HasAnyInputFailed(pXform, m_failedOutputs);
			if (atLeastOneInputHasFailed)
			{
				// We need to report all outputs as failed to prevent further execution
				for (const auto& output : pXform->GetOutputs())
				{
					m_failedOutputs.insert(output.m_path.AsPrefixedPath());
				}

				// Now, mark this transform as failed
  				pXform->AddErrorMessage("Dependent input files failed to update\n");
				for (const auto& input : pXform->GetInputs())
				{
					if (std::find(m_updatedOutputs.begin(), m_updatedOutputs.end(), input.m_file.AsPrefixedPath()) == m_updatedOutputs.end())
					{
						pXform->AddErrorMessage("Missing Input: " + input.m_file.AsPrefixedPath());
					}
				}
				OnBuildTransformFailed(pXform, BuildTransformStatus::kWaitingInputs);

				// We always remove the item if it failed
				curIter = m_schedulableXforms.erase(curIter);

				continue;
			}

			// Check if all source inputs exist
			bool allSourceInputsExist = true;
			for (const auto& input : pXform->GetInputs())
			{
				if (input.m_type == TransformInput::kSourceFile)
				{
					FileIO::FileTime inputFileTime;
					bool success = GetFileDateCache().readFileTime(input.m_file.AsAbsolutePath().c_str(), inputFileTime);
					if (!success)
					{
						pXform->AddErrorMessage("Input file '" + input.m_file.AsAbsolutePath() + "' is missing for '" + pXform->GetTypeName());
						allSourceInputsExist = false;
						break;
					}
				}
			}
			if (!allSourceInputsExist)
			{
				OnBuildTransformFailed(pXform);

				// We always remove the item if it failed
				curIter = m_schedulableXforms.erase(curIter);
				continue;
			}

			// Check if all inputs are available
			bool inputsAvailable = true;
			for (const auto& input : pXform->GetInputs())
			{
				if (input.m_type != TransformInput::kSourceFile)
				{
					if (std::find(m_updatedOutputs.begin(), m_updatedOutputs.end(), input.m_file.AsPrefixedPath()) == m_updatedOutputs.end())
					{
						inputsAvailable = false;
						break;
					}
				}
			}

			if (inputsAvailable)
			{
				ExecuteTransform(pXform);

				// We always remove the item if it has evaluated. If it needs to run again, then it should be in the 'waiting' list
				curIter = m_schedulableXforms.erase(curIter);
				progressWasMade = true;
			}
			else
			{
				curIter++;
			}
		}

		WakeUpWaitingTransforms();
		
		// Wait for external builds. Don't spin-loop while that is going on
		if (!progressWasMade && m_newXforms.empty() && AreTransformsWaiting())
		{
			Sleep(500);
		}

		// Ensure that we are making progress
		if (!progressWasMade && !m_schedulableXforms.empty() && m_newXforms.empty() && !AreTransformsWaiting())
		{
			const bool hasPriorTransformFailed = !m_failedOutputs.empty();
			std::set<BuildTransform *> waiting;
			for (auto pPendingXform : m_schedulableXforms)
			{
				bool missingInput = false;
				for (const auto &inputFile : pPendingXform->GetInputs())
				{
					if (inputFile.m_type == TransformInput::kSourceFile)
					{
						if (!FileIO::fileExists(inputFile.m_file.AsAbsolutePath().c_str()))
						{
							pPendingXform->AddErrorMessage("Missing Source Input: " + inputFile.m_file.AsPrefixedPath() + "\n");
							IERR(("Missing SOURCE Input: " + inputFile.m_file.AsPrefixedPath() + "\n").c_str());
							missingInput = true;
						}
					}
					else
					{
						BuildTransform *pSource = nullptr;
						for (auto pPotentialSource : m_uniqueXforms)
						{
							for (const auto &outputFile : pPotentialSource->GetOutputs())
							{
								if (outputFile.m_path.AsPrefixedPath() == inputFile.m_file.AsPrefixedPath())
								{
									pSource = pPotentialSource;
									break;
								}
							}
						}

						bool waitingOnPending = false;
						for (auto pPotentialPending : m_schedulableXforms)
						{
							if (pPotentialPending == pPendingXform)
							{
								waitingOnPending = true;
								break;
							}
						}

						if (pSource == nullptr)
						{
							missingInput = true;
							pPendingXform->AddErrorMessage("Missing UNPROVIDED Input: " + inputFile.m_file.AsPrefixedPath() + "\n");
							if (!hasPriorTransformFailed)
								IERR(("Missing UNPROVIDED Input: " + inputFile.m_file.AsPrefixedPath() + "\n").c_str());
							//else
							//	IERR(("Missing UNPROVIDED Input: " + inputFile.m_file.AsPrefixedPath() + "\n").c_str());
						}
						else if (waitingOnPending)
						{
							missingInput = true;
							pPendingXform->AddErrorMessage("Missing PENDING Input: " + inputFile.m_file.AsPrefixedPath() + "\n");
						//	IERR_VERBOSE(("Missing PENDING Input: " + inputFile.m_file.AsPrefixedPath() + "\n").c_str());
						}
						else
						{
							pPendingXform->AddErrorMessage("Missing FAILED (??) Input: " + inputFile.m_file.AsPrefixedPath() + "\n");
							if (hasPriorTransformFailed)
								IERR_VERBOSE(("Missing FAILED Input: " + inputFile.m_file.AsPrefixedPath() + "\n").c_str());
							else
								IERR(("Missing FAILED Input: " + inputFile.m_file.AsPrefixedPath() + "\n").c_str());
						}
					}
				}

				OnBuildTransformFailed(pPendingXform, BuildTransformStatus::kWaitingInputs);
			}

			/*
			for (auto pPendingXform : m_schedulableXforms)
			{
				for (const auto& inputFile : pPendingXform->GetInputs())
				{
					if (std::find(m_updatedOutputs.begin(), m_updatedOutputs.end(), inputFile.AsPrefixedPath()) == m_updatedOutputs.end())
					{
						pPendingXform->AddErrorMessage("Missing Input: " + inputFile.AsPrefixedPath());
					}
				}

				OnBuildTransformFailed(pPendingXform, BuildTransformStatus::kWaitingInputs);
			}
			*/
			m_buildStatus = BuildStatus::kErrorOccurred;
			break;
		}
	}

	DataStore::CommitChanges();

	// Prevent further evaluations once an error has been detected
	if (HasErrors())
	{
		m_buildStatus = BuildStatus::kErrorOccurred;
	}
	else
	{
		const U64 endTick = TimerGetRawCount();
		float elapsedSec = ConvertTicksToSeconds(endTick - startTick);
		Log("Completed %d Build Transforms in %.3f seconds", m_uniqueXforms.size(), elapsedSec);
	}

	Log("Evaluation Completed");

	PushContextsToParents();

	if (addSchedulerLog)
	{
		// Fake a transform that will contain the execution log of the build scheduler itself
		BuildTransform_SchedulerLog* pLogXform = new BuildTransform_SchedulerLog(m_internalLog);
		pLogXform->SetOutput(TransformOutput(std::string(PathPrefix::BUILD_INTERMEDIATE) + "common/scheduler/log"));
		m_transformInfo[pLogXform] = BuildTransformSchedulerInfo();
		for (int assetTypeIndex = 1; assetTypeIndex < (int)AssetType::kNumTypes; ++assetTypeIndex)
			for (const auto& entry : m_assetContexts[assetTypeIndex])
				m_transformContexts[pLogXform].push_back(entry.second);
		pLogXform->SetScheduler(this);
		m_Xforms.push_back(std::unique_ptr<BuildTransform>(pLogXform));
		m_uniqueXforms.push_back(pLogXform);


		// Validation errors will not trigger a non-successful commit of associations. We will associate the errors with
		// each transform as well as the scheduler transform for now.
		const auto& validationErrors = DataStore::GetValidationErrors();
		if (!validationErrors.empty())
		{
			// Transfer the errors into the transform
			for (const auto& err : validationErrors)
			{
				auto iter = m_outputToXform.find(err.first);
				if (iter == m_outputToXform.end())
				{
					const size_t outputPathLen = err.first.size();
					if (err.first.rfind(".d") == outputPathLen - 2)
					{
						// Ok, this is a .d file. Try to map it to the first output of the transform
						iter = m_outputToXform.find(err.first.substr(0, outputPathLen - 4));		// The .d files are now stored as '.0.d', '.1.d' and so on.
					}

					if (iter == m_outputToXform.end())
					{
						IABORT("A validation error for output '%s' was not able to be associated back to a transform. This is a logic bug!", err.first.c_str());
					}
				}

				// It's too late to add it to the transform log at this point. All we can do is flag the transform as failing validation
				// and it will show up as purple on the web page
				iter->second->AddValidationErrorMessage(err.second);		// Make the transform indicate validation error
				pLogXform->AddValidationErrorMessage(err.second);			// Make the scheduler log indicate validation error
				INOTE_VERBOSE("%s", err.second.c_str());					// Make the scheduler log contain the validation error
			}
		}

		DisableInternalLogCapture();

		ExecuteTransform(pLogXform);
		m_transformInfo[pLogXform].m_startOrder = 0;
	}

	return m_buildStatus;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U64 BuildScheduler::RegisterFarmWaitItem(BuildTransform* pXform, FarmJobId farmJobId)
{
	// Validate that the job ID is valid
	const Farm::Job& job = m_farm.getJob(farmJobId);
	if (job.m_jobId != farmJobId)
	{
		IABORT("BuildTransform '%s' is attempting to wait on a job that was never added to this FarmSession.", pXform->GetTypeName().c_str());
	}

	FarmWaitItem waitItem(pXform, farmJobId);
	m_farmWaitingBuildXforms.insert(m_farmWaitingBuildXforms.begin(), waitItem);

	// set farm execution time in scheduler info, to flag this as a farm job!
	m_transformInfo[pXform].m_farmExecutionTime = 0;

	Log("Build Transform %s [%016X] has kicked and is now waiting for farm job %lld", pXform->GetTypeName().c_str(), (uintptr_t)pXform, farmJobId.AsU64());
	return waitItem.GetSequenceId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
U64 BuildScheduler::RegisterThreadPoolWaitItem(BuildTransform* pXform, WorkItemHandle workHandle)
{
	ThreadPoolWaitItem waitItem(pXform,workHandle);
	m_threadpoolWaitingBuildXforms.insert(m_threadpoolWaitingBuildXforms.begin(), waitItem); 
	return waitItem.GetSequenceId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
U64 BuildScheduler::RegisterTransformPoolWaitItem(BuildTransform* pXform, BuildTransform* pWaitedTransform)
{
	TransformWaitItem waitItem(pXform, pWaitedTransform);
	m_transformsWaitingBuildXforms.insert(m_transformsWaitingBuildXforms.begin(), waitItem);
	return waitItem.GetSequenceId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
U64 BuildScheduler::RegisterSnDbsWaitItem(BuildTransform* pXform,
										  const std::string& projectName,
										  const std::string& jobId)
{
	SnDbsWaitItem waitItem(pXform, projectName, jobId);
	m_snDbsWaitingBuildXforms.insert(m_snDbsWaitingBuildXforms.begin(), waitItem);
	return waitItem.GetSequenceId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
U64 BuildScheduler::SubmitFarmJob(BuildTransform* pXform,
								  const std::string& commandLine,
								  unsigned long long requiredMemory,
								  unsigned int numThreads,
								  unsigned int numRetries,
								  bool remotable)
{
	FarmJobId jobId = m_farm.submitJob(commandLine, requiredMemory, numThreads, false, false, remotable);
	if (jobId == FarmJobId::kInvalidFarmjobId)
	{
		IABORT("An error occurred when submitting a farm job.");
	}

	const Farm::Job& job = m_farm.getJob(jobId);

	FarmWaitItem waitItem(pXform, jobId, commandLine, requiredMemory, numThreads, numRetries, remotable);
	m_farmWaitingBuildXforms.insert(m_farmWaitingBuildXforms.begin(), waitItem);

	// set farm execution time in scheduler info, to flag this as a farm job!
	m_transformInfo[pXform].m_farmExecutionTime = 0;

	Log("Build Transform %s [%016X] has kicked and is now waiting for farm job %lld, %d retries", pXform->GetTypeName().c_str(), (uintptr_t)pXform, jobId.AsU64(), numRetries);
	return waitItem.GetSequenceId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
FileDateCache& BuildScheduler::GetFileDateCache()
{
	return m_fileDateCache;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Farm& BuildScheduler::GetFarmSession()
{
	return m_farm;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Farm& BuildScheduler::GetFarmSession() const
{
	return m_farm;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool BuildScheduler::IsBusy() const
{
	if (!m_newXforms.empty())
		return true;

	// Any transform still waiting to run?
	auto cur = m_transformInfo.begin();
	auto end = m_transformInfo.end();
	while (cur != end)
	{
		if (cur->second.m_status == BuildTransformStatus::kWaitingInputs)
			return true;
		
		cur++;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool BuildScheduler::HasErrors() const
{
	for (const BuildTransform* transform : m_uniqueXforms)
	{
		if (transform->HasError())
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool BuildScheduler::HasValidationErrors() const
{
	for (const BuildTransform* transform : m_uniqueXforms)
	{
		if (transform->HasValidationError())
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const BuildTransformSchedulerInfo* BuildScheduler::GetTransformInfo(const BuildTransform* pXform) const
{
	auto where = m_transformInfo.find(pXform);
	if (where != m_transformInfo.end())
	{
		return &where->second;
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const vector<const BuildTransformContext*>& BuildScheduler::GetTransformContexts(const BuildTransform* pXform) const
{
	map<const BuildTransform*, vector<const BuildTransformContext*>>::const_iterator it = m_transformContexts.find(pXform);
	if (it != m_transformContexts.cend())
		return it->second;

	IABORT("transform has no context");
	ALWAYS_ASSERT(false);

	return m_transformContexts.end()->second; //compilation trick, we should never ever reach this
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildScheduler::RegisterAssetContext(const std::string& assetName,
										  AssetType assetType,
										  const BuildTransformContext* pContext)
{
	m_assetContexts[(uint32_t)assetType][assetName] = pContext;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const BuildTransformContext* BuildScheduler::GetAssetContext(const std::string& assetName, AssetType assetType) const
{
	const auto& assetContexts = m_assetContexts[(int)assetType];
	map<string, const BuildTransformContext*>::const_iterator it = assetContexts.find(assetName);
	if (it != assetContexts.cend())
		return it->second;

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const std::map<std::string, const BuildTransformContext*>& BuildScheduler::GetAssetContexts(AssetType assetType) const
{
	return m_assetContexts[(int)assetType];
}

/// --------------------------------------------------------------------------------------------------------------- ///
const std::vector<const BuildTransform*> BuildScheduler::GetContextTransforms(const BuildTransformContext* pContext) const
{
	// Find all the transforms used by this context
	std::vector<const BuildTransform*> contextXforms;
	for (const BuildTransform* pXform : GetAllTransforms())
	{
		const vector<const BuildTransformContext*>& xformContextList = GetTransformContexts(pXform);
		for (const BuildTransformContext* pXformContext : xformContextList)
		{
			if (pXformContext->Matches(pContext))
			{
				contextXforms.push_back(pXform);
			}
		}
	}

	return contextXforms;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool BuildScheduler::ReplicateTransformOutputs(const BuildTransform* transform) const
{
	NativeFileStorage storage;
	const vector<TransformOutput>& outputs = transform->GetOutputs();
	for (const TransformOutput& output : outputs)
	{
		const bool shouldReplicate = output.m_flags & TransformOutput::kReplicate;
		const bool includeInManifest = output.m_flags & TransformOutput::kIncludeInManifest;
		if (!shouldReplicate &&
			!(m_config.m_replicateManifest && includeInManifest))
			continue;

		DataHash outputHash;
		if (!m_contentHashCollection.GetContentHash(output.m_path, &outputHash))
		{
			IERR("Failed to retrieve the hash of %s. This file won't be replicated", output.m_path.AsPrefixedPath().c_str());
			return false;
		}

		const std::string destPath = output.m_path.AsAbsolutePath();

		if (FileIO::fileExists(destPath.c_str()))
		{
			DataHash existingHash;
			if (ReadContentHashFromFile(existingHash, destPath + ".md5").Succeeded() && (outputHash == existingHash))
			{
				INOTE_VERBOSE("Replicating transform output '%s' but it already has the same content hash '%s', skipping!\n",
							  output.m_path.AsAbsolutePath().c_str(),
							  existingHash.AsText().c_str());
				continue;
			}

			// ensure existing file is writable
			if (FileIO::updatePermissions(destPath.c_str()).Failed())
			{
				IABORT("Failed to update permissions for file '%s'\n", destPath.c_str());
			}
		}

		BuildFile file(output.m_path, outputHash);

		string fileContent;
		DataStore::ReadData(file, fileContent);

		OutputBlob blob(fileContent.data(), fileContent.length());

		//Do not check if the file exists here cause the NativeFileStorage doesn't check if the md5 is correct, it only checks if a file with the same filename exists.
		//It means DoesFileExists can return true even if the file content is different.

		const BuildPath& path = file.GetBuildPath();

		bool writeResult = false;
		if (!m_config.m_local)
		{
			FileLockJanitor fileLock(path.AsPrefixedPath() + "-replicate");
			writeResult = storage.WriteData(path, outputHash, blob, 0);
		}
		else
		{
			writeResult = storage.WriteData(path, outputHash, blob, 0);
		}

		if (!writeResult)
		{
			IERR("Failed to replicate output file %s.", output.m_path.AsPrefixedPath().c_str());
			return false;
		}

		Log("Replicated output %s[%s].", file.AsPrefixedPath().c_str(), file.GetContentHash().AsText().c_str());
	}

	return true;
}


/// --------------------------------------------------------------------------------------------------------------- ///
const std::vector<std::pair<std::string, int>> ExtractLoadedReferencesFromMaya3ndbOutput(BuildScheduler& buildScheduler, FarmJobId jobId)
{
	std::vector<std::pair<std::string, int>> loadedReferences;
	const std::string maya3ndbJobOutput = buildScheduler.GetFarmSession().getJobOutput(jobId);
	if (maya3ndbJobOutput.empty())
	{
		IABORT("Oh, crap. The farm didn't log any output when extracting collision data from the Maya scene. Try re-running BA, but contact a tools programmer if you keep getting this error message.");
	}

	bool searchMore = false;
	size_t initialSearchPos = 0;
	do
	{
		searchMore = false;

		size_t findPos = maya3ndbJobOutput.find("Loaded reference", initialSearchPos);
		if (findPos != std::string::npos)
		{
			const size_t lineEndPos = maya3ndbJobOutput.find_first_of("\n", findPos);
			const std::string outputLine = maya3ndbJobOutput.substr(findPos, lineEndPos - findPos);
			initialSearchPos = lineEndPos + 1;

			// The reference paths are output by Maya3Ndb within brackets '[refpath]'
			const size_t bracketStartPos = outputLine.find_first_of("[");
			const size_t bracketEndPos = outputLine.find_first_of("]", bracketStartPos);
			const std::string referencePath = outputLine.substr(bracketStartPos + 1, bracketEndPos - bracketStartPos - 1);

			// Strip out the username from the path 
			const size_t relativePathStart = referencePath.find("/art/");
			if (relativePathStart == std::string::npos)
			{
				IABORT("The reference path did not contain '/art/' which it has to [%s].", referencePath.c_str());
			}

			const std::string relativeReferencePath = referencePath.substr(relativePathStart + 1);

			// Figure out the reference depth
			const std::string referenceDepthStr = outputLine.substr(bracketEndPos + strlen("depth:") + 2);
			const int referenceDepth = atoi(referenceDepthStr.c_str());

			// Save it off
			loadedReferences.push_back(std::pair<std::string, int>(relativeReferencePath, referenceDepth));

			searchMore = true;
		}

	} while (searchMore);

	return loadedReferences;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildSchedulerConfig::BuildSchedulerConfig()
{
	m_local = false;
	m_validate = false;
	m_tracingEnabled = false;
	m_noReplicate = false;
}

BuildSchedulerConfig::BuildSchedulerConfig(const ToolParams& tool)
	: BuildSchedulerConfig()
{
	m_local = tool.m_local;
	m_validate = tool.m_validate;
	m_validateOutputs = tool.m_validateOutputs;
	m_onlyExecuteOutputs = tool.m_onlyExecuteOutputs;
	m_noReplicate = tool.m_noReplicate;
	m_replicateManifest = tool.m_replicateManifest;
	m_tracingEnabled = tool.m_schedulerTrace;
	m_userName = tool.m_userName;
}
