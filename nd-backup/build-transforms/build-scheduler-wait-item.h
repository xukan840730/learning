/*
 * Copyright (c) 2017 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "tools/libs/toolsutil/farm.h"
#include "tools/libs/toolsutil/simple-dependency.h"
#include "tools/libs/toolsutil/sndbs.h"
#include "tools/libs/toolsutil/threadpool-helper.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform;

/// --------------------------------------------------------------------------------------------------------------- ///
struct SchedulerWaitItem
{
	enum EType { kNone, kFarm, kThreadJob, kTransform, kSnDbs };
	EType			GetType()				const { return m_type; }
	BuildTransform* GetWaitingXform()		const { return m_pBuildXform; }
	U64				GetSequenceId()			const { return m_sequenceId; }

	FarmJobId		GetFarmJobId()			const { ALWAYS_ASSERT_THROW(m_type == kFarm, ""); return  m_farmJobId; }
	WorkItemHandle	GetWorkItemhandle()		const { ALWAYS_ASSERT_THROW(m_type == kThreadJob, ""); return m_workHandle; }
	BuildTransform*	GetWaitedOnTransform()	const { ALWAYS_ASSERT_THROW(m_type == kTransform, ""); return m_pWaitedOnTransform; }

protected:
	static U64 SeqGen()
	{ 
		static U64 counter = 0; 
		return counter++; 
	}
	SchedulerWaitItem(EType type,
					  BuildTransform* pBuildTransform,
					  FarmJobId farmJobId,
					  WorkItemHandle workHandle,
					  BuildTransform* pWaitingTransform)
		: m_type(type)
		, m_pBuildXform(pBuildTransform)
		, m_sequenceId(SeqGen())
		, m_farmJobId(farmJobId)
		, m_workHandle(workHandle)
		, m_pWaitedOnTransform(pWaitingTransform)
	{
	}

	EType				m_type;
	U64					m_sequenceId;
	BuildTransform*		m_pBuildXform;

	FarmJobId			m_farmJobId;			//kFarm
	WorkItemHandle		m_workHandle; 			//kThreadJob
	BuildTransform*		m_pWaitedOnTransform;	//kTransform
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ThreadPoolWaitItem : public SchedulerWaitItem
{
	ThreadPoolWaitItem(BuildTransform* pBuildXform, WorkItemHandle workHandle)
		: SchedulerWaitItem(kThreadJob, pBuildXform, FarmJobId::kInvalidFarmjobId, workHandle, nullptr)
	{
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct FarmWaitItem : public SchedulerWaitItem
{
	FarmWaitItem(BuildTransform* pBuildXform, FarmJobId farmJobId)
		: SchedulerWaitItem(kFarm, pBuildXform, farmJobId, WorkItemHandle(), nullptr)
		, m_commandLine("")
		, m_requiredMemory(0)
		, m_numThreads(0)
		, m_retries(0)
		, m_remotable(false)
	{
	}

	FarmWaitItem(BuildTransform* pBuildXform,
				 FarmJobId farmJobId,
				 const std::string& commandLine,
				 unsigned long long requiredMemory = 0,
				 unsigned int numthreads = 0,
				 U32 numRetries = 0,
				 bool remotable = false)
		: SchedulerWaitItem(kFarm, pBuildXform, farmJobId, WorkItemHandle(), nullptr)
		, m_commandLine(commandLine)
		, m_requiredMemory(requiredMemory)
		, m_numThreads(numthreads)
		, m_retries(numRetries)
		, m_remotable(remotable)
	{
	}

	const std::string		GetCommandLine() const { return m_commandLine; }
	U32						GetReqMemory() const { return m_requiredMemory; }
	U32						GetNumThreads() const { return m_numThreads; }
	U32						GetNumRetries() const { return m_retries; }
	bool					GetRemotable() const { return m_remotable; }

protected:
	U32					m_retries;
	std::string			m_commandLine;
	unsigned long long	m_requiredMemory;
	unsigned int		m_numThreads;
	bool				m_remotable;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct TransformWaitItem : public SchedulerWaitItem
{
	TransformWaitItem(BuildTransform* pBuildXform, BuildTransform* pWaitedOnTransform)
		: SchedulerWaitItem(kTransform, pBuildXform, FarmJobId::kInvalidFarmjobId, WorkItemHandle(), pWaitedOnTransform)
	{
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
class SnDbsWaitItem : public SchedulerWaitItem
{
public:
	SnDbsWaitItem()
		: SchedulerWaitItem(kSnDbs, nullptr, FarmJobId::kInvalidFarmjobId, WorkItemHandle(), nullptr)
		, m_complete(false)
	{
	}

	SnDbsWaitItem(BuildTransform* pBuildXform, const std::string& projectName, const std::string& jobId)
		: SchedulerWaitItem(kSnDbs, pBuildXform, FarmJobId::kInvalidFarmjobId, WorkItemHandle(), nullptr)
		, m_projectName(projectName)
		, m_jobId(jobId)
		, m_complete(false)
	{
	}

	std::string m_projectName;
	std::string m_jobId;
	bool m_complete;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct SchedulerResumeItem
{
	SchedulerResumeItem()
	{
		m_type = SchedulerWaitItem::kNone;
		m_farmJob = nullptr;
		m_threadPoolJob = nullptr;
		m_waitedTransform = nullptr;
		m_sequenceId = ~U64(0);
	}

	SchedulerResumeItem(SchedulerWaitItem::EType type,
						U64 sequenceId,
						const Farm::Job* farmJob,
						const toolsutils::SafeJobBase* threadPoolJob,
						const BuildTransform* waitedTransform)
		: m_type(type)
		, m_sequenceId(sequenceId)
		, m_farmJob(farmJob)
		, m_threadPoolJob(threadPoolJob)
		, m_waitedTransform(waitedTransform)
	{
	}

	SchedulerWaitItem::EType		m_type;
	U64								m_sequenceId;
	const Farm::Job*				m_farmJob;
	const toolsutils::SafeJobBase*	m_threadPoolJob;
	const BuildTransform*			m_waitedTransform;
	SnDbs::JobResult				m_snDbsResult;
};
