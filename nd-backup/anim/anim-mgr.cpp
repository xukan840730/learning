/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-mgr.h"

#include "corelib/job/spawner-job.h"
#include "corelib/math/mathutils.h"
#include "corelib/memory/memory-map.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-mgr-jobs.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-streaming.h"
#include "ndlib/anim/anim-stream-manager.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/nd-anim-plugins.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/rig-nodes/rig-nodes.h"
#include "ndlib/frame-params.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/net/nd-net-game-manager.h"
#include "ndlib/net/nd-net-info.h"
#include "ndlib/process/event.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/process-update.h"
#include "ndlib/process/process.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/render-camera.h"

#include "gamelib/level/art-item-skeleton.h"

#include <orbisanim/anim_perf.h>
#include <orbisanim/options.h>
#include <orbisanim/commandblock.h>

/// --------------------------------------------------------------------------------------------------------------- ///

//#include "ndlib/nd-game-info.h"
#if ORBISANIM_ENABLE_CAPTURE_INSTRUMENTATION
OrbisAnim::Capture::ChunkFile* g_pChunkFile = nullptr;
OrbisAnim::Capture::Recorder* g_pRecorder = nullptr;
#endif

#ifdef ENABLE_ANIMLOG
class InMemoryLogger
{
public:
	enum
	{
		kLogEntrySize = 100,
		kLogsSize = 200 * 1024,
		kMaxLogs = kLogsSize / kLogEntrySize
	};
	char** m_logs;
	char* m_logData;
	size_t m_logIndex;
	mutable NdAtomicLock m_logLock;

	InMemoryLogger()
	{
		m_logData = NDI_NEW(kAllocDebug) char[kLogsSize];
		m_logs = NDI_NEW(kAllocDebug) char *[kMaxLogs];
		m_logIndex = 0;
		memset(m_logData, 0, sizeof(kLogsSize));
		for (size_t iLog = 0; iLog != kMaxLogs; ++iLog)
			m_logs[iLog] = m_logData + (kLogEntrySize * iLog);
	}

	char* GetNextLog()
	{
		m_logLock.AcquireLock(FILE_LINE_FUNC);
		char* ret = m_logs[m_logIndex];
		m_logIndex++;
		if (m_logIndex >= kMaxLogs)
			m_logIndex = 0;
		m_logLock.ReleaseLock();
		return ret;
	}
};

InMemoryLogger* g_pAnimLog = nullptr;

char* GetNextAnimLog()
{
	return g_pAnimLog->GetNextLog();
}

size_t GetAnimLogEntrySize()
{
	return InMemoryLogger::kLogEntrySize;
}
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
AnimMgr::AnimMgr()
	: m_pAnimDataArray(nullptr)
	, m_bucketLock(0xFFFFFFFF)
	, m_bucketLockExcludeAnimData(nullptr)
	, m_parallelUpdateLock(false)
{
	m_spinLock.m_atomic.Set(0);

	m_newAnimData.ClearAllBits();
	m_usedAnimData.ClearAllBits();
	m_disabledAnimData.ClearAllBits();
	m_zombieAnimData.ClearAllBits();
	m_visibleAnimData.ClearAllBits();

	for (int i = 0; i < kMaxNumFramesInFlight; ++i)
	{
		m_deletedAnimData[i].ClearAllBits();
	}

	for (U32F i = 0; i < kNumBuckets; ++i)
	{
		m_bucketAnimData[i].ClearAllBits();
	}

	ClearBucketObservers();
}

/// --------------------------------------------------------------------------------------------------------------- ///
#if ORBISANIM_PERF_ENABLED
class AnimPerfTracer : public OrbisAnim::Perf::Tracer
{
public:
	AnimPerfTracer() : OrbisAnim::Perf::Tracer(0)
	{
		memset(m_exectionTimes, 0.0f, sizeof(float) * OrbisAnim::CommandBlock::kNumCommands * kMaxNumFramesInFlight);
	}
	~AnimPerfTracer() {}

	virtual void StartCall(uint16_t cmd) override
	{
		if (cmd >= OrbisAnim::CommandBlock::kNumCommands)
			return;

		const U64 workerThreadIndex = ndjob::GetCurrentWorkerThreadIndex();

		m_startTicks[workerThreadIndex] = TimerGetRawCount();
	}

	virtual void StopCall(uint16_t cmd, uint16_t params) override
	{
		if (cmd >= OrbisAnim::CommandBlock::kNumCommands)
			return;

		const U64 endTick = TimerGetRawCount();
		const RenderFrameParams* pRenderFrameParams = GetCurrentRenderFrameParams();
		const I64 frameIndex = pRenderFrameParams ? (pRenderFrameParams->m_frameNumber + 1) % kMaxNumFramesInFlight : 0;

		const U64 workerThreadIndex = ndjob::GetCurrentWorkerThreadIndex();

		m_exectionTimes[frameIndex][cmd] += ConvertTicksToSeconds(m_startTicks[workerThreadIndex], endTick);
	}

	void Print() const
	{
		const RenderFrameParams* pRenderFrameParams = GetCurrentRenderFrameParams();
		const I64 frameIndex = pRenderFrameParams ? (pRenderFrameParams->m_frameNumber + 1) % kMaxNumFramesInFlight : 0;

		float totalTimeSec = 0.0f;
		float largestCmdTimeSec = 0.0f;

		for (int i = 0; i < OrbisAnim::CommandBlock::kNumCommands; ++i)
		{
			const float timeSec = m_exectionTimes[frameIndex][i];
			totalTimeSec += timeSec;
			largestCmdTimeSec = Max(largestCmdTimeSec, timeSec);
		}

		MsgCon("\n");
		for (int i = 0; i < OrbisAnim::CommandBlock::kNumCommands; ++i)
		{
			const float timeSec = m_exectionTimes[frameIndex][i];

			if (timeSec < NDI_FLT_EPSILON)
				continue;

			const float timePcnt = timeSec / largestCmdTimeSec;
			const Color timeColor = Slerp(kColorGreen, kColorRed, timePcnt);

			char colorBuf[128];

			MsgCon("%36s : %s%.3f ms%s\n",
				OrbisAnim::CommandBlock::GetCommandName(OrbisAnim::CommandBlock::Command(i)),
				GetTextColorString(timeColor, colorBuf),
				timeSec * 1000.0f,
				GetTextColorString(kTextColorNormal));
		}

		MsgCon("Total Anim Cmd Time: %.3f ms\n", totalTimeSec * 1000.0f);
	}

	void Reset()
	{
		const RenderFrameParams* pRenderFrameParams = GetCurrentRenderFrameParams();
		const I64 frameIndex = pRenderFrameParams ? (pRenderFrameParams->m_frameNumber + 1) % kMaxNumFramesInFlight : 0;

		memset(m_exectionTimes[frameIndex], 0.0f, sizeof(float) * OrbisAnim::CommandBlock::kNumCommands);
	}

	U64 m_startTicks[ndjob::kMaxWorkerThreads];
	float m_exectionTimes[kMaxNumFramesInFlight][OrbisAnim::CommandBlock::kNumCommands];
};

static AnimPerfTracer s_animPerfTracer;
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::Init()
{
	m_pAnimDataArray = NDI_NEW(Memory::MapId(ALLOCATION_FG_ANIM_DATA)) FgAnimData[kMaxAnimData];
#ifdef ENABLE_ANIMLOG
	g_pAnimLog = NDI_NEW(kAllocDebug) InMemoryLogger;
#endif
	// WIN OrbisAnim - TODO
	m_pAnimPhasePluginFunc = NdProcessAnimPhasePluginFunc;
	m_pRigPhasePluginFunc = NdProcessPostAnimPhasePluginFunc;

#if ORBISANIM_PERF_ENABLED
	OrbisAnim::CommandBlock::g_perfTracer = &s_animPerfTracer;
#endif

	m_pEarlyDeferredAnimGameCounter = ndjob::AllocateCounter(FILE_LINE_FUNC);
	m_pEarlyDeferredAnimRenderCounter = ndjob::AllocateCounter(FILE_LINE_FUNC);

	m_lastAllocDataIndex = kMaxAnimData;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::Shutdown()
{
	ndjob::FreeCounter(m_pEarlyDeferredAnimGameCounter);
	ndjob::FreeCounter(m_pEarlyDeferredAnimRenderCounter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::RegisterAnimPhasePluginHandler(AnimPhasePluginCommandHandler* pAnimPhasePluginFunc)
{
	m_pAnimPhasePluginFunc = pAnimPhasePluginFunc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::RegisterRigPhasePluginHandler(RigPhasePluginCommandHandler* pRigPhasePluginFunc)
{
	m_pRigPhasePluginFunc = pRigPhasePluginFunc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimPhasePluginCommandHandler* AnimMgr::GetAnimPhasePluginHandler() const
{
	return m_pAnimPhasePluginFunc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
RigPhasePluginCommandHandler* AnimMgr::GetRigPhasePluginHandler() const
{
	return m_pRigPhasePluginFunc;
}


/// --------------------------------------------------------------------------------------------------------------- ///
FgAnimData* AnimMgr::AllocateAnimData(U32F maxNumDrivenInstances, Process* pProcess)
{
	FgAnimData* pElement = nullptr;

	m_spinLock.AcquireLock(FILE_LINE_FUNC);

	// We need to create a new bit array with all occupied elements (new, used and deleted).
	AnimDataBits allocatedElements;
	AnimDataBits::BitwiseOr(&allocatedElements, m_usedAnimData, m_newAnimData);
	for (int i = 0; i < kMaxNumFramesInFlight; ++i)
	{
		AnimDataBits::BitwiseOr(&allocatedElements, allocatedElements, m_deletedAnimData[i]);
	}

	// Find an unused element
	U64 unusedIndex = kMaxAnimData;
	if (m_lastAllocDataIndex < kMaxAnimData)
	{
		unusedIndex = allocatedElements.FindNextClearBit(m_lastAllocDataIndex);
	}
	if (unusedIndex >= kMaxAnimData)
	{
		unusedIndex = allocatedElements.FindFirstClearBit();
	}
	if (unusedIndex < kMaxAnimData)
	{
		pElement = m_pAnimDataArray + unusedIndex;
		pElement->Reset();

		pElement->m_hProcess = pProcess;

		// Update the book-keeping tables
		m_newAnimData.SetBit(unusedIndex);
		m_disabledAnimData.ClearBit(unusedIndex);
		m_zombieAnimData.ClearBit(unusedIndex);
		m_visibleAnimData.ClearBit(unusedIndex); // these get cleared and reset every frame, but just to be pedantic

		// Init bucket
		ProcessBucket bucket = EngineComponents::GetProcessMgr()->GetProcessBucket(*pProcess);
		m_bucketAnimData[bucket == kProcessBucketUnknown ? kProcessBucketDefault : bucket].SetBit(unusedIndex);

		pElement->m_maxNumDrivenInstances = maxNumDrivenInstances;
		pElement->m_numDrivenInstances = 0;
		if (maxNumDrivenInstances > 0)
		{
			pElement->m_pDrivenInstanceIndices = NDI_NEW U32[maxNumDrivenInstances];
		}

		m_lastAllocDataIndex = unusedIndex;
	}

	m_spinLock.ReleaseLock();
	return pElement;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::FreeAnimData(FgAnimData* pElement)
{
	if (pElement)
	{
		pElement->OnFree();

		m_spinLock.AcquireLock(FILE_LINE_FUNC);
		const ptrdiff_t currentElementIndex = pElement - m_pAnimDataArray;

		// Clear it out from all buckets. It will only be in one
		// but without checking them all we don't know which one.
		// Simpler to clear them all.
		m_disabledAnimData.ClearBit(currentElementIndex);
		m_zombieAnimData.ClearBit(currentElementIndex);
		m_visibleAnimData.ClearBit(currentElementIndex); // these get cleared and reset every frame, but just to be pedantic
		m_newAnimData.ClearBit(currentElementIndex);
		m_usedAnimData.ClearBit(currentElementIndex);

		for (I32F ii = 0; ii < kNumBuckets; ++ii)
		{
			m_bucketAnimData[ii].ClearBit(currentElementIndex);
		}

		// Add it to the deleted element list to disallow the use of this element for the reminder of this frame.
		// It is being referenced by the rendering right now and should not be used.
		m_deletedAnimData[GetCurrentFrameNumber() % kMaxNumFramesInFlight].SetBit(currentElementIndex);
		m_spinLock.ReleaseLock();
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::SetIsInBindPose(FgAnimData* pElement, bool isInBindPose)
{
#if !BIND_POSE_OPTIM
	return;
#endif

	if (pElement)
	{
		if (isInBindPose)
		{
			pElement->m_flags |= FgAnimData::kIsInBindPose;
		}
		else
		{
			pElement->m_flags &= ~FgAnimData::kIsInBindPose;
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::SetAnimationDisabled(FgAnimData* pElement, FgAnimData::UserAnimationPassFunctor pDisabledFunc)
{
	if (pElement)
	{
		m_spinLock.AcquireLock(FILE_LINE_FUNC);
		const ptrdiff_t index = pElement - m_pAnimDataArray;
		pElement->m_userAnimationPassCallback[0] = pDisabledFunc;

		pElement->m_flags |= FgAnimData::kDisabledAnimData;
		m_disabledAnimData.SetBit(index);

		m_spinLock.ReleaseLock();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::SetAnimationEnabled(FgAnimData* pElement)
{
	if (pElement)
	{
		m_spinLock.AcquireLock(FILE_LINE_FUNC);
		const ptrdiff_t index = pElement - m_pAnimDataArray;
		pElement->m_userAnimationPassCallback[0] = nullptr;

		pElement->m_flags &= ~FgAnimData::kDisabledAnimData;
		m_disabledAnimData.ClearBit(index);

		m_spinLock.ReleaseLock();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::SetAnimationZombie(FgAnimData* pElement)
{
	if (pElement)
	{
		m_spinLock.AcquireLock(FILE_LINE_FUNC);
		const ptrdiff_t index = pElement - m_pAnimDataArray;

		m_zombieAnimData.SetBit(index);

		m_spinLock.ReleaseLock();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::SetVisible(FgAnimData* pElement, bool isVisible)
{
	if (pElement)
	{
		m_spinLock.AcquireLock(FILE_LINE_FUNC);
		const ptrdiff_t index = pElement - m_pAnimDataArray;

		if (isVisible)
		{
			m_visibleAnimData.SetBit(index);
		}
		else
		{
			m_visibleAnimData.ClearBit(index);
		}
		m_spinLock.ReleaseLock();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::ClearAllVisibleBits()
{
	m_spinLock.AcquireLock(FILE_LINE_FUNC);
	m_visibleAnimData.ClearAllBits();
	m_spinLock.ReleaseLock();
}



struct ProcessDeferredAnimCmdData
{
 	AnimExecutionContext* m_pAnimCtx;
 	const AnimExecutionContext* m_pPrevAnimCtx;
 	U32 m_requiredSegmentMask;
	bool m_isRender;
	ndjob::CounterHandle m_hCounterToFreeOnStart;
	ndjob::CounterHandle m_hCounterToDecrementOnEnd;
};
 
/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT(ProcessDeferredAnimCmdJob)
{
 	ProcessDeferredAnimCmdData* pJobData = (ProcessDeferredAnimCmdData*)jobParam;
 
	if (pJobData->m_hCounterToFreeOnStart)
	{
		ndjob::FreeCounter(pJobData->m_hCounterToFreeOnStart);
	}

	if (pJobData->m_isRender)
	{
		pJobData->m_pAnimCtx->m_pPluginJointSet = nullptr;	// Prevent any bad cases of accessing stale or relocated joint sets
	}

 	ProcessRequiredSegments(pJobData->m_requiredSegmentMask, AnimExecutionContext::kOutputSkinningMats | AnimExecutionContext::kOutputOutputControls, pJobData->m_pAnimCtx, pJobData->m_pPrevAnimCtx);

	if (pJobData->m_hCounterToDecrementOnEnd)
	{
		pJobData->m_hCounterToDecrementOnEnd->Decrement();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::KickEarlyDeferredAnimCmd(const FgAnimData* pAnimData, U32 segmentMaskGame, U32 segmentMaskRender)
{
 	AnimExecutionContext* pAnimCtx = GetAnimExecutionContext(pAnimData);
	const AnimExecutionContext* pPrevAnimCtx = GetPrevFrameAnimExecutionContext(pAnimData);

	U32 toBeDoneMask = ((1U << pAnimCtx->m_pSkel->m_numSegments) - 1) & ~(pAnimCtx->m_processedSegmentMask_SkinningMats & pAnimCtx->m_processedSegmentMask_OutputControls);
	segmentMaskGame &= toBeDoneMask;
	segmentMaskRender &= toBeDoneMask & ~segmentMaskGame;

	if ((segmentMaskGame|segmentMaskRender) == 0)
	{
		return;
	}

	ndjob::CounterHandle hGameCounter;
	if (segmentMaskGame)
	{
	 	ProcessDeferredAnimCmdData* pJobData = NDI_NEW(kAllocSingleFrame) ProcessDeferredAnimCmdData;
 		pJobData->m_pAnimCtx = pAnimCtx;
 		pJobData->m_pPrevAnimCtx = pPrevAnimCtx;
 		pJobData->m_requiredSegmentMask = segmentMaskGame;
		pJobData->m_isRender = false;
	 	m_pEarlyDeferredAnimGameCounter->Increment();
		pJobData->m_hCounterToDecrementOnEnd = m_pEarlyDeferredAnimGameCounter;
 		ndjob::JobDecl jobDecl(ProcessDeferredAnimCmdJob, (uintptr_t)pJobData);
		if (segmentMaskRender)
		{
			hGameCounter = ndjob::AllocateCounter(FILE_LINE_FUNC, 1);
			jobDecl.m_associatedCounter = hGameCounter;
		}
 		ndjob::RunJobs(&jobDecl, 1, nullptr, FILE_LINE_FUNC, ndjob::Priority::kGameFrameBelowNormal);
	}

	if (segmentMaskRender)
	{
	 	ProcessDeferredAnimCmdData* pJobData = NDI_NEW(kAllocGameToGpuRing) ProcessDeferredAnimCmdData;
 		pJobData->m_pAnimCtx = pAnimCtx;
 		pJobData->m_pPrevAnimCtx = pPrevAnimCtx;
 		pJobData->m_requiredSegmentMask = segmentMaskRender;
		pJobData->m_isRender = true;
		pJobData->m_hCounterToFreeOnStart = hGameCounter;
	 	m_pEarlyDeferredAnimRenderCounter->Increment();
		pJobData->m_hCounterToDecrementOnEnd = m_pEarlyDeferredAnimRenderCounter;

	 	ndjob::JobDecl jobDecl(ProcessDeferredAnimCmdJob, (uintptr_t)pJobData);
		jobDecl.m_dependentCounter = hGameCounter;
 		ndjob::RunJobs(&jobDecl, 1, nullptr, FILE_LINE_FUNC, ndjob::Priority::kLow);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::WaitForAllEarlyDeferredAnimGameJobs()
{
	ndjob::WaitForCounter(m_pEarlyDeferredAnimGameCounter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::WaitForAllEarlyDeferredAnimRenderJobs()
{
	ndjob::WaitForCounter(m_pEarlyDeferredAnimGameCounter);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::ChangeBucket(FgAnimData* pElement, U32F newBucket)
{
	ANIM_ASSERT(newBucket < kNumBuckets);

	if (pElement)
	{
		// Update the book-keeping tables
		const ptrdiff_t index = pElement - m_pAnimDataArray;

		// Bit arrays are not threadsafe! ChangeBucket() can race
		// with other RMWs from FreeAnimData() or AllocateAnimData().
		// TODO: check and clean up the public API of this file as it
		// has other shady bits.
		m_spinLock.AcquireLock(FILE_LINE_FUNC);

		// Clear it out from all buckets. It will only be in one
		// but without checking them all we don't know which one.
		// Simpler to clear them all.
		for (I32F ii = 0; ii < kNumBuckets; ++ii)
		{
			m_bucketAnimData[ii].ClearBit(index);
		}

		// Now add it to the proper bucket
		m_bucketAnimData[newBucket].SetBit(index);

		m_spinLock.ReleaseLock();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F AnimMgr::FindBucketForAnimData(const FgAnimData* pElement) const
{
	ANIM_ASSERT(pElement);

	// Get the index
	const ptrdiff_t index = pElement - m_pAnimDataArray;

	for (U32F ii = 0; ii < kNumBuckets; ++ii)
	{
		if (m_bucketAnimData[ii].IsBitSet(index))
			return ii;
	}

	return kNumBuckets;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void NotifyAnimTableUpdatedFunctor(FgAnimData* pAnimData, uintptr_t data)
{
	AnimControl* pAnimControl = pAnimData->m_pAnimControl;
	if (pAnimControl)
	{
		pAnimControl->NotifyAnimTableUpdated();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void SetupAnimFrameData()
{
	PROFILE(Animation, AnimFrameExecInit);

	// Init deferred anim buffers. Use kAllocDoubleGameToGpuRing because we look at the previous frame's data in the render frame
	RenderFrameParams* pParams = GetCurrentRenderFrameParams();

	AnimFrameExecutionData* pAnimFrameData = NDI_NEW(kAllocDoubleGameToGpuRing, kAlign32) AnimFrameExecutionData;

	pAnimFrameData->m_pAnimExecutionContexts
		= reinterpret_cast<AnimExecutionContext*>(NDI_NEW(kAllocDoubleGameToGpuRing, kAlign32)
													  U8[sizeof(AnimExecutionContext) * AnimMgr::kMaxAnimData]);

	pAnimFrameData->m_pUsedAnimExecutionContexts = NDI_NEW(kAllocDoubleGameToGpuRing, kAlign32) AnimMgr::AnimDataBits;
	pAnimFrameData->m_pUsedAnimExecutionContexts->ClearAllBits();

	pParams->m_pAnimFrameExecutionData = pAnimFrameData;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::FrameBegin()
{
	PROFILE(Animation, AnimMgr_FrameBegin);

	// Add all the new elements to the 'used elements' list.
	AnimDataBits::BitwiseOr(&m_usedAnimData, m_usedAnimData, m_newAnimData);
	m_newAnimData.ClearAllBits();

	const I64 frameIndex = GetCurrentFrameNumber();
	const I64 trailingFrameIndex = frameIndex - (kMaxNumFramesInFlight - 1);
	if (trailingFrameIndex >= 0)
	{
		// Ensure that the render frame is done which means we are done animating and therefore using the anim data.
		ANIM_ASSERT(GetRenderFrameParams(trailingFrameIndex)->m_renderFrameEndTick);

		// It is now safe to free the elements deleted last frame.
		m_deletedAnimData[trailingFrameIndex % kMaxNumFramesInFlight].ClearAllBits();
	}

	if (AnimMasterTable::WasAnimTableModifiedLastFrame())
	{
		ForAllAnimData(m_usedAnimData, NotifyAnimTableUpdatedFunctor, 0);
		EngineComponents::GetAnimStreamManager()->NotifyAnimTableUpdated();
	}

	// Update the little hack we have added
	AnimationTimeUpdate();

	SetupAnimFrameData();

	if (g_animOptions.m_displayPerformanceStats)
	{
		const I64 statsFrameIndex = (GetCurrentFrameNumber() + 1) % kMaxNumFramesInFlight;
		g_rigNodeStats[statsFrameIndex].Print();
		g_rigNodeStats[statsFrameIndex].Reset();

#if ORBISANIM_PERF_ENABLED
		s_animPerfTracer.Print();
		s_animPerfTracer.Reset();
#endif
	}

	extern bool g_dumpInfoPointPoser;
	extern bool g_dumpInfoRivetPlane;
	static bool latchedDumpInfoPointPoser = false;
	static bool latchedDumpInfoRivetPlane = false;
	if (latchedDumpInfoPointPoser)
	{
		latchedDumpInfoPointPoser = false;
		g_dumpInfoPointPoser = false;
	}
	if (latchedDumpInfoRivetPlane)
	{
		latchedDumpInfoRivetPlane = false;
		g_dumpInfoRivetPlane = false;
	}
	latchedDumpInfoPointPoser = g_dumpInfoPointPoser;
	latchedDumpInfoRivetPlane = g_dumpInfoRivetPlane;

#if ORBISANIM_ENABLE_CAPTURE_INSTRUMENTATION
	if (g_pRecorder)
	{
		time_t curTime;
		time(&curTime);
		struct tm* pCurrentTime = localtime(&curTime);
		char timeString[512];
		sprintf(timeString, "%04d%02d%02d_%02d%02d%02d", pCurrentTime->tm_year + 1900, pCurrentTime->tm_mon + 1, pCurrentTime->tm_mday, pCurrentTime->tm_hour, pCurrentTime->tm_min, pCurrentTime->tm_sec);

		char captureFileName[512];
		sprintf(captureFileName, "%s/captures/orbis-anim-capture-%s.cpt", EngineComponents::GetNdGameInfo()->m_pathDetails.m_localDir, timeString);
		g_pRecorder->chunkFile().save(captureFileName);
		NDI_DELETE_CONTEXT(kAllocDebug, g_pRecorder);
		g_pRecorder = nullptr;
		OrbisAnim::Capture::s_recorder = nullptr;
	}

	if (g_animOptions.m_captureThisFrame && g_pRecorder == nullptr)
	{
		// Setup capture buffers
		const U32 captureBufferSize = 32 * 1024 * 1024;
		void* captureBuffer = NDI_NEW(kAllocDebug, kAlign16) char[captureBufferSize];

		// create the recorder - this is a singleton
		g_pRecorder = NDI_NEW(kAllocDebug, kAlign16) OrbisAnim::Capture::Recorder((uintptr_t)captureBuffer, captureBufferSize);
		g_pRecorder->setEnabled(true);
		OrbisAnim::Capture::s_recorder = g_pRecorder;
		OrbisAnim::Capture::s_recorder->addFrameStart(0);
		g_animOptions.m_captureThisFrame = false;
	}
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::BucketBegin(U32F bucket)
{
	PROFILE(Animation, BucketBegin);

	ANIM_ASSERT(bucket < kNumBuckets);

	// Filter out all the new elements added to this bucket
	ALIGNED(128) AnimDataBits addedAnimData;
	AnimDataBits::BitwiseAnd(&addedAnimData, m_newAnimData, m_bucketAnimData[bucket]);

	// Add all the new elements in this bucket to the 'used elements' list and remove them from the 'new element' list.
	AnimDataBits::BitwiseXor(&m_newAnimData, m_newAnimData, addedAnimData);
	AnimDataBits::BitwiseOr(&m_usedAnimData, m_usedAnimData, addedAnimData);

#ifdef ANIM_STATS_ENABLED
	g_animFrameStats.m_curBucket = bucket;
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void AddAsyncDisabledUpdateDataFunctor(FgAnimData* pAnimData, uintptr_t data)
{
	AnimDataProcessingData* pProcessingData = (AnimDataProcessingData*)data;
	pProcessingData->m_ppAnimDataArray[pProcessingData->m_numAnimData++] = pAnimData;
}

struct SyncUpdateFunctorData
{
	AsyncUpdateJobData* m_pObjectAsyncUpdateData;
};

static void SyncAnimStepFunctor(FgAnimData* pAnimData, uintptr_t data)
{
	SyncUpdateFunctorData* pData = (SyncUpdateFunctorData*)data;

	AsyncUpdateJobData* pObjectAsyncUpdateData = pData->m_pObjectAsyncUpdateData;

	// Shouldn't be needed anymore
	pObjectAsyncUpdateData->m_pAnimData = pAnimData;
	pObjectAsyncUpdateData->m_pAnimControl = pAnimData->m_pAnimControl;

	// HACK HACK HACK for now. Should use an index instead
	pData->m_pObjectAsyncUpdateData = pData->m_pObjectAsyncUpdateData + 1;

	pObjectAsyncUpdateData->m_pObjectWaitCounter = nullptr;

	AsyncObjectUpdate_AnimStep_Job((uintptr_t)pObjectAsyncUpdateData);
}

static void SyncAnimBlendFunctor(FgAnimData* pAnimData, uintptr_t data)
{
	SyncUpdateFunctorData* pData = (SyncUpdateFunctorData*)data;

	AsyncUpdateJobData* pObjectAsyncUpdateData = pData->m_pObjectAsyncUpdateData;

	// HACK HACK HACK for now. Should use an index instead
	pData->m_pObjectAsyncUpdateData = pData->m_pObjectAsyncUpdateData + 1;

	AsyncObjectUpdate_AnimBlend_Job((uintptr_t)pObjectAsyncUpdateData);
}

static void SyncAnimJointBlendFunctor(FgAnimData* pAnimData, uintptr_t data)
{
	SyncUpdateFunctorData* pData = (SyncUpdateFunctorData*)data;

	AsyncUpdateJobData* pObjectAsyncUpdateData = pData->m_pObjectAsyncUpdateData;

	// HACK HACK HACK for now. Should use an index instead
	pData->m_pObjectAsyncUpdateData = pData->m_pObjectAsyncUpdateData + 1;

	AsyncObjectUpdate_JointBlend_Job((uintptr_t)pObjectAsyncUpdateData);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::ForAllAnimData(const AnimDataBits& bits, FgAnimDataFunctor funcPtr, uintptr_t data)
{
	AnimDataBits::Iterator bitIter(bits);
	U64 firstElementIndex = bitIter.First();

	if (firstElementIndex < kMaxAnimData)
	{
		// Ok we have at least one used element...
		U64 currentElementIndex = firstElementIndex;

		// Prefetch the draw data (not the PmInstance)
		Memory::PrefetchForLoad(m_pAnimDataArray + currentElementIndex);

		while (currentElementIndex < kMaxAnimData)
		{
			// This is a look-ahead index so that we can prefetch
			U64 nextElementIndex = bitIter.Advance();

			// If the next element is valid we can prefetch it
			if (nextElementIndex < kMaxAnimData)
			{
				// Prefetch the draw data (not the PmInstance)
				Memory::PrefetchForLoad(m_pAnimDataArray + nextElementIndex);
				Memory::PrefetchForStore(reinterpret_cast<U8*>(m_pAnimDataArray + nextElementIndex) + 128);	// PmInstance for Bounding Sphere
			}

			// There is a small chance that the callback below can generate a change which kills a process (region updates)
			// We don't want to call a killed process here. This should NOT be allowed but we have to ship!
			if (!m_deletedAnimData[GetCurrentFrameNumber() % kMaxNumFramesInFlight].IsBitSet(currentElementIndex))
			{
				FgAnimData* pAnimData = m_pAnimDataArray + currentElementIndex;

				// Perform per-element work here
				{
					ANIM_ASSERT(funcPtr);
					funcPtr(pAnimData, data);
				}
			}

			currentElementIndex = nextElementIndex;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::ForAllUsedAnimDataExcludeBucket(U32F bucket, FgAnimDataFunctor funcPtr, uintptr_t data)
{
	ANIM_ASSERT(bucket < kNumBuckets);
	ALIGNED(128) AnimDataBits bits;
	bits.ClearAllBits();
	for (int i = 0; i < kNumBuckets; i++)
	{
		if (i != bucket)
			AnimDataBits::BitwiseOr(&bits, bits, m_bucketAnimData[i]);
	}
	AnimDataBits::BitwiseAndComp(&bits, bits, m_newAnimData);
	AnimDataBits::BitwiseAndComp(&bits, bits, m_zombieAnimData);
	ForAllAnimData(bits, funcPtr, data);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::ExternalBucketEvent(U32F bucket, U32F eventType)
{
	if (m_numBucketObservers[eventType])
	{
		PROFILE(Animation, NotifyBucketObservers_External);
		Memory::PushAllocator(kAllocInvalid, FILE_LINE_FUNC);
		NotifyBucketObservers(bucket, eventType);
		Memory::PopAllocator();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::NotifyBucketObservers(U32F bucket, U32F updateType)
{
	for (U32F i = 0; i < m_numBucketObservers[updateType]; ++i)
	{
		Process* pProcess = m_bucketObservers[i][updateType].ToMutableProcess();
		if (pProcess && !pProcess->IsProcessDead())
		{
			GAMEPLAY_ASSERT(pProcess->GetPipelineId() == ProcessUpdate::kClassic);
			Process::ActivateContext(pProcess, FILE_LINE_FUNC);
			pProcess->BucketEventNotify(bucket, updateType);
			Process::DeactivateContext(FILE_LINE_FUNC);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::ClearBucketObservers()
{
	for (U32F ii = 0; ii < kNumAnimUpdateTypes; ii++)
	{
		m_numBucketObservers[ii] = 0;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::LockBucket(U32 bucket)
{
	m_bucketLock = bucket;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimMgr::SetBucketLockExcludeAnimData(const FgAnimData* pAnimData)
{
	m_bucketLockExcludeAnimData = pAnimData;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimMgr::IsAnimDataLocked(const FgAnimData* pAnimData) const
{
	if (!g_animOptions.m_enableAnimDataLocking)
		return false;

	if (pAnimData)
	{
		const ptrdiff_t index = pAnimData - m_pAnimDataArray;

		// Only test things that are in use
		if (!m_usedAnimData.IsBitSet(index))
			return false;

		ANIM_ASSERT(!m_newAnimData.IsBitSet(index));
		for (int i = 0; i < kMaxNumFramesInFlight; ++i)
		{
			ANIM_ASSERT(!m_deletedAnimData[i].IsBitSet(index));
		}

		if (pAnimData->IsDisabledAndUpdatesInParallel())
		{
			return m_parallelUpdateLock && (pAnimData != m_bucketLockExcludeAnimData);
		}
		else
		{
			return (FindBucketForAnimData(pAnimData) == m_bucketLock) && (pAnimData != m_bucketLockExcludeAnimData);
		}
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimMgr::IsVisible(const FgAnimData* pElement) const
{
	if (pElement)
	{
		const ptrdiff_t index = pElement - m_pAnimDataArray;
		return m_visibleAnimData.IsBitSet(index);
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnimMgr::IsDisabled(const FgAnimData* pElement) const
{
	if (pElement)
	{
		const ptrdiff_t index = pElement - m_pAnimDataArray;
		return m_disabledAnimData.IsBitSet(index);
	}
	return false;
}

/// =============================================================================================================== ///
void AnimMgr::GetAnimDataBits(U32F bucket, AnimDataBits& bits, AnimDataBits& disabledBits)
{
	PROFILE_PERSISTENT(Animation, GetAnimDataBits);

	const float animSysDeltaTime = GetAnimationSystemDeltaTime();

	// Iterate over all elements in this bucket except newly allocated elements
	ANIM_ASSERT(bucket < kNumBuckets);
	AnimDataBits::BitwiseAndComp(&bits, m_bucketAnimData[bucket], m_newAnimData);
	AnimDataBits::BitwiseAndComp(&bits, bits, m_disabledAnimData); // ... and omit elements that are not animating
	AnimDataBits::BitwiseAndComp(&bits, bits, m_zombieAnimData); // ... and omit elements that are not animating

	AnimDataBits::BitwiseAndComp(&disabledBits, m_bucketAnimData[bucket], m_newAnimData);
	AnimDataBits::BitwiseAnd(&disabledBits, disabledBits, m_disabledAnimData);
	AnimDataBits::BitwiseAndComp(&disabledBits, disabledBits, m_zombieAnimData);
}

void AnimMgr::ForAllAnimData(ProcessUpdate::Manager& updateInfo, ProcessUpdate::Pipeline& pipe, U32 passIndex, FgAnimDataFunctor funcPtr, uintptr_t data)
{
	ProcessUpdate::PassId passId = static_cast<ProcessUpdate::PassId>(passIndex);
	U64 iAnimData = pipe.GetNext(passId);
	FgAnimData* pAnimData = updateInfo.GetAnimData(iAnimData);
	while (iAnimData != kBitArrayInvalidIndex)
	{
		U32F animMgrIdx = EngineComponents::GetAnimMgr()->GetAnimDataIndex(pAnimData);

		U64 iNextAnimData = pipe.GetNext(passId);
		FgAnimData* pNextAnimData = updateInfo.GetAnimData(iNextAnimData);
		if (pNextAnimData)
		{
			Memory::PrefetchForLoad(pNextAnimData);
			Memory::PrefetchForStore(reinterpret_cast<U8*>(pNextAnimData) + 128);	// PmInstance for Bounding Sphere
		}

		// There is a small chance that the callback below can generate a change which kills a process (region updates)
		// We don't want to call a killed process here. This should NOT be allowed but we have to ship!
		if (!m_deletedAnimData[GetCurrentFrameNumber() % kMaxNumFramesInFlight].IsBitSet(animMgrIdx))
		{
			// Perform per-element work here
			{
				ANIM_ASSERT(funcPtr);
				funcPtr(pAnimData, data);
			}
		}

		pipe.FinishItem(passId, iAnimData);

		iAnimData = iNextAnimData;
		pAnimData = pNextAnimData;
	}
}

void AnimMgr::SyncUpdate(U32F bucket, ProcessUpdate::Manager& updateInfo, ProcessUpdate::Pipeline& pipe, bool gameplayIsPaused)
{
	PROFILE_PERSISTENT(Animation, SyncUpdate);

	const float animSysDeltaTime = GetAnimationSystemDeltaTime();

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	{
		ProcessUpdate::PipelineId	pipeId = pipe.GetPipelineId();
		bool						classicPipe = (pipeId == ProcessUpdate::kClassic);

		const U32F numAnimDataToUpdate = pipe.NumActive(ProcessUpdate::kAnimStep);
		const U32F numDisabledDataToUpdate = pipe.NumActive(ProcessUpdate::kAnimDisabled);
		if ((numAnimDataToUpdate == 0) && (numDisabledDataToUpdate == 0))
			return;

		AsyncUpdateJobData* pAsyncUpdateJobData = NDI_NEW(kAlign128) AsyncUpdateJobData[numAnimDataToUpdate];

		// HACK - Global data should be stored as const global data
		for (U64 i = 0; i < numAnimDataToUpdate; ++i)
		{
			pAsyncUpdateJobData[i].m_animSysDeltaTime = animSysDeltaTime;
			pAsyncUpdateJobData[i].m_gameplayIsPaused = gameplayIsPaused;
			pAsyncUpdateJobData[i].m_pAnimPhasePluginFunc = m_pAnimPhasePluginFunc;
			pAsyncUpdateJobData[i].m_pRigPhasePluginFunc = m_pRigPhasePluginFunc;
		}

		// Disallow processes handles from this bucket to be used
		//EngineComponents::GetAnimMgr()->LockBucket(bucket);

		if (classicPipe && m_numBucketObservers[kPreAnimUpdate] /*&& !gameplayIsPaused*/)
		{
			PROFILE(Animation, NotifyBucketObservers_PreAnimUpdate);
			NotifyBucketObservers(bucket, kPreAnimUpdate);
		}

		SyncUpdateFunctorData data;
		data.m_pObjectAsyncUpdateData = pAsyncUpdateJobData;
		ForAllAnimData(updateInfo, pipe, ProcessUpdate::kAnimStep, SyncAnimStepFunctor, reinterpret_cast<uintptr_t>(&data));

		if (classicPipe && m_numBucketObservers[kPostAnimUpdate] && !gameplayIsPaused)
		{
			PROFILE(Animation, NotifyBucketObservers_PostAnimUpdate);

			Memory::PushAllocator(kAllocInvalid, FILE_LINE_FUNC);
			NotifyBucketObservers(bucket, kPostAnimUpdate);
			Memory::PopAllocator();
		}

		data.m_pObjectAsyncUpdateData = pAsyncUpdateJobData;
		ForAllAnimData(updateInfo, pipe, ProcessUpdate::kAnimBlend, SyncAnimBlendFunctor, reinterpret_cast<uintptr_t>(&data));

		if (classicPipe && m_numBucketObservers[kPostAnimBlending] && !gameplayIsPaused)
		{
			PROFILE(Animation, NotifyBucketObservers_PostBlending);

			Memory::PushAllocator(kAllocInvalid, FILE_LINE_FUNC);
			NotifyBucketObservers(bucket, kPostAnimBlending);
			Memory::PopAllocator();
		}

		data.m_pObjectAsyncUpdateData = pAsyncUpdateJobData;
		ForAllAnimData(updateInfo, pipe, ProcessUpdate::kJointBlend, SyncAnimJointBlendFunctor, reinterpret_cast<uintptr_t>(&data));

		if (classicPipe && m_numBucketObservers[kPostJointUpdate] && !gameplayIsPaused)
		{
			PROFILE(Animation, NotifyBucketObservers_PostJointUpdate);

			Memory::PushAllocator(kAllocInvalid, FILE_LINE_FUNC);
			NotifyBucketObservers(bucket, kPostJointUpdate);
			Memory::PopAllocator();
		}

		// Now disabled objects
		if (numDisabledDataToUpdate > 0)
		{
			// Create data for synchronous update of disabled objects
			AnimDataProcessingData disabledProcessingData;
			disabledProcessingData.m_ppAnimDataArray = NDI_NEW FgAnimData*[kMaxAnimData];
			disabledProcessingData.m_numAnimData = 0;
			disabledProcessingData.m_cursor.Set(0);
			disabledProcessingData.m_deltaTime = animSysDeltaTime;

			ForAllAnimData(updateInfo, pipe, ProcessUpdate::kAnimDisabled, AddAsyncDisabledUpdateDataFunctor, reinterpret_cast<uintptr_t>(&disabledProcessingData));

			AnimateDisabledObjectJob((uintptr_t)&disabledProcessingData);
		}
	}

	// Allow processes handles from this bucket to be used again
	//EngineComponents::GetAnimMgr()->LockBucket(0xFFFFFFFF);

	// All bucket observers are cleared now as they need to re-register every frame
	// and only will work within the current bucket
	//ClearBucketObservers();
}

static const U32 kMaxDisabledBatchSize = 16;
static const U32 kMaxAnimWorkers = 6;

struct AnimWorkerData : public AsyncUpdateJobData
{
	ProcessUpdate::Sentinel		m_sentinel;
	ProcessUpdate::Manager*		m_pUpdateInfo;
	ProcessUpdate::Pipeline*	m_pPipeline;
	F32							m_deltaTime;
	U32							m_disabledBatchSize;
};

JOB_ENTRY_POINT(AnimUpdate_Job)
{
	const AsyncUpdateJobData* pJobData = (const AsyncUpdateJobData*)jobParam;
	FgAnimData* pAnimData = pJobData->m_pAnimData;

	AsyncObjectUpdate_AnimStep_Job(jobParam);
	if (pAnimData->m_hProcess.HandleValid())
		AsyncObjectUpdate_AnimBlend_Job(jobParam);
	if (pAnimData->m_hProcess.HandleValid())
		AsyncObjectUpdate_JointBlend_Job(jobParam);
}

JOB_ENTRY_POINT(AnimWorker)
{
	AnimWorkerData*				pData = reinterpret_cast<AnimWorkerData*>(jobParam);
	ProcessUpdate::Sentinel		sentinel;
	ProcessUpdate::Manager&		updateInfo = *pData->m_pUpdateInfo;
	ProcessUpdate::Pipeline&	pipe = *pData->m_pPipeline;
	ProcessUpdate::PassId		updateID = ProcessUpdate::kAnimStep;

	sentinel.Check();

	// copy async update data
	AsyncUpdateJobData		asyncUpdateData = *pData;

	U64 iLastAvailable = 0;
	U64 iAnimData = pipe.GetNext(updateID);
	FgAnimData* pAnimData = updateInfo.GetAnimData(iAnimData);
	while (iAnimData != kBitArrayInvalidIndex)
	{
		sentinel.Check();

		if (iAnimData >= iLastAvailable)
		{
			iLastAvailable = pipe.WaitForItem(updateID, iAnimData);
		}
		if (pipe.IsItemSet(updateID, iAnimData))	// skip if this bit has been cleared by process update
		{
			//ALWAYS_ASSERT(!EngineComponents::GetAnimMgr()->IsDisabled(pAnimData));

			if (pAnimData->m_hProcess.HandleValid())
			{
				asyncUpdateData.m_pAnimData = pAnimData;
				asyncUpdateData.m_pAnimControl = pAnimData->m_pAnimControl;

				if ((pAnimData->m_extraAnimJobFlags & ~ndjob::kRequireLargeStack) != 0)
				{
					ndjob::CounterHandle counter;
					ndjob::JobDecl asyncAnimJobDecl(AnimUpdate_Job, (uintptr_t)&asyncUpdateData);
					asyncAnimJobDecl.m_flags |= pAnimData->m_extraAnimJobFlags;
					ndjob::RunJobs(&asyncAnimJobDecl, 1, &counter, FILE_LINE_FUNC);
					if (counter)
						ndjob::WaitForCounterAndFree(counter);
				}
				else
				{
					AsyncObjectUpdate_AnimStep_Job((uintptr_t)&asyncUpdateData);
					if (pAnimData->m_hProcess.HandleValid())
						AsyncObjectUpdate_AnimBlend_Job((uintptr_t)&asyncUpdateData);
					if (pAnimData->m_hProcess.HandleValid())
						AsyncObjectUpdate_JointBlend_Job((uintptr_t)&asyncUpdateData);
				}
			}
			pipe.FinishItem(updateID, iAnimData);
			pipe.FinishItem(ProcessUpdate::kAnimBlend, iAnimData);
			pipe.FinishItem(ProcessUpdate::kJointBlend, iAnimData);
		}

		iAnimData = pipe.GetNext(updateID);
		pAnimData = updateInfo.GetAnimData(iAnimData);
	}
}

JOB_ENTRY_POINT(AnimStepWorker)
{
	AnimWorkerData*				pData = reinterpret_cast<AnimWorkerData*>(jobParam);
	ProcessUpdate::Sentinel&	sentinel = pData->m_sentinel;
	ProcessUpdate::Manager&		updateInfo = *pData->m_pUpdateInfo;
	ProcessUpdate::Pipeline&	pipe = *pData->m_pPipeline;
	ProcessUpdate::PassId		updateID = ProcessUpdate::kAnimStep;

	sentinel.Check();

	// copy async update data
	AsyncUpdateJobData		asyncUpdateData = *pData;

	U64 iLastAvailable = 0;
	U64 iAnimData = pipe.GetNext(updateID);
	FgAnimData* pAnimData = updateInfo.GetAnimData(iAnimData);
	while (iAnimData != kBitArrayInvalidIndex)
	{
		sentinel.Check();

		if (iAnimData >= iLastAvailable)
		{
			iLastAvailable = pipe.WaitForItem(updateID, iAnimData);
		}
		if (pipe.IsItemSet(updateID, iAnimData))	// skip if this bit has been cleared by process update
		{
			//ALWAYS_ASSERT(!EngineComponents::GetAnimMgr()->IsDisabled(pAnimData));

			if (pAnimData->m_hProcess.HandleValid())
			{
				asyncUpdateData.m_pAnimData = pAnimData;
				asyncUpdateData.m_pAnimControl = pAnimData->m_pAnimControl;

				if (pAnimData->m_extraAnimJobFlags)
				{
					ndjob::CounterHandle counter;
					ndjob::JobDecl asyncAnimJobDecl(AsyncObjectUpdate_AnimStep_Job, (uintptr_t)&asyncUpdateData);
					asyncAnimJobDecl.m_flags |= pAnimData->m_extraAnimJobFlags;
					ndjob::RunJobs(&asyncAnimJobDecl, 1, &counter, FILE_LINE_FUNC);
					if (counter)
						ndjob::WaitForCounterAndFree(counter);
				}
				else
				{
					AsyncObjectUpdate_AnimStep_Job((uintptr_t)&asyncUpdateData);
				}
			}
			pipe.FinishItem(updateID, iAnimData);
		}

		iAnimData = pipe.GetNext(updateID);
		pAnimData = updateInfo.GetAnimData(iAnimData);
	}
}

JOB_ENTRY_POINT(AnimBlendWorker)
{
	AnimWorkerData*				pData = reinterpret_cast<AnimWorkerData*>(jobParam);
	ProcessUpdate::Sentinel&	sentinel = pData->m_sentinel;
	ProcessUpdate::Manager&		updateInfo = *pData->m_pUpdateInfo;
	ProcessUpdate::Pipeline&	pipe = *pData->m_pPipeline;
	ProcessUpdate::PassId		updateID = ProcessUpdate::kAnimBlend;

	sentinel.Check();

	// copy async update data
	AsyncUpdateJobData		asyncUpdateData = *pData;

	U64 iLastAvailable = 0;
	U64 iAnimData = pipe.GetNext(updateID);
	FgAnimData* pAnimData = updateInfo.GetAnimData(iAnimData);
	while (iAnimData != kBitArrayInvalidIndex)
	{
		sentinel.Check();

		if (iAnimData >= iLastAvailable)
		{
			iLastAvailable = pipe.WaitForItem(updateID, iAnimData);
		}
		if (pipe.IsItemSet(updateID, iAnimData))	// skip if this bit has been cleared by process update
		{
			//ALWAYS_ASSERT(!EngineComponents::GetAnimMgr()->IsDisabled(pAnimData));

			if (pAnimData->m_hProcess.HandleValid())
			{
				asyncUpdateData.m_pAnimData = pAnimData;
				asyncUpdateData.m_pAnimControl = pAnimData->m_pAnimControl;

				if (pAnimData->m_extraAnimJobFlags)
				{
					ndjob::CounterHandle counter;
					ndjob::JobDecl asyncAnimJobDecl(AsyncObjectUpdate_AnimBlend_Job, (uintptr_t)&asyncUpdateData);
					asyncAnimJobDecl.m_flags |= pAnimData->m_extraAnimJobFlags;
					ndjob::RunJobs(&asyncAnimJobDecl, 1, &counter, FILE_LINE_FUNC);
					if (counter)
						ndjob::WaitForCounterAndFree(counter);
				}
				else
				{
					AsyncObjectUpdate_AnimBlend_Job((uintptr_t)&asyncUpdateData);
				}
			}
			pipe.FinishItem(updateID, iAnimData);
		}

		iAnimData = pipe.GetNext(updateID);
		pAnimData = updateInfo.GetAnimData(iAnimData);
	}
}

JOB_ENTRY_POINT(JointBlendWorker)
{
	AnimWorkerData*				pData = reinterpret_cast<AnimWorkerData*>(jobParam);
	ProcessUpdate::Sentinel&	sentinel = pData->m_sentinel;
	ProcessUpdate::Manager&		updateInfo = *pData->m_pUpdateInfo;
	ProcessUpdate::Pipeline&	pipe = *pData->m_pPipeline;
	ProcessUpdate::PassId		updateID = ProcessUpdate::kJointBlend;

	sentinel.Check();

	// copy async update data
	AsyncUpdateJobData		asyncUpdateData = *pData;

	U64 iLastAvailable = 0;
	U64 iAnimData = pipe.GetNext(updateID);
	FgAnimData* pAnimData = updateInfo.GetAnimData(iAnimData);
	while (iAnimData != kBitArrayInvalidIndex)
	{
		sentinel.Check();

		if (iAnimData >= iLastAvailable)
		{
			iLastAvailable = pipe.WaitForItem(updateID, iAnimData);
		}
		if (pipe.IsItemSet(updateID, iAnimData))	// skip if this bit has been cleared by process update
		{
			//ALWAYS_ASSERT(!EngineComponents::GetAnimMgr()->IsDisabled(pAnimData));

			if (pAnimData->m_hProcess.HandleValid())
			{
				asyncUpdateData.m_pAnimData = pAnimData;
				asyncUpdateData.m_pAnimControl = pAnimData->m_pAnimControl;

				if (pAnimData->m_extraAnimJobFlags)
				{
					ndjob::CounterHandle counter;
					ndjob::JobDecl asyncAnimJobDecl(AsyncObjectUpdate_JointBlend_Job, (uintptr_t)&asyncUpdateData);
					asyncAnimJobDecl.m_flags |= pAnimData->m_extraAnimJobFlags;
					ndjob::RunJobs(&asyncAnimJobDecl, 1, &counter, FILE_LINE_FUNC);
					if (counter)
						ndjob::WaitForCounterAndFree(counter);
				}
				else
				{
					AsyncObjectUpdate_JointBlend_Job((uintptr_t)&asyncUpdateData);
				}
			}
			pipe.FinishItem(updateID, iAnimData);
		}

		iAnimData = pipe.GetNext(updateID);
		pAnimData = updateInfo.GetAnimData(iAnimData);
	}
}

JOB_ENTRY_POINT(DisabledWorker)
{
	AnimWorkerData*				pData = reinterpret_cast<AnimWorkerData*>(jobParam);
	ProcessUpdate::Sentinel&	sentinel = pData->m_sentinel;
	ProcessUpdate::Manager&		updateInfo = *pData->m_pUpdateInfo;
	ProcessUpdate::Pipeline&	pipe = *pData->m_pPipeline;
	ProcessUpdate::PassId		updateID = ProcessUpdate::kAnimDisabled;

	sentinel.Check();

	const U32F batchSize = pData->m_disabledBatchSize;
	ASSERT(batchSize <= kMaxDisabledBatchSize);

	U64 iLastAvailable = 0;
	bool exitWorker = false;

	while (!exitWorker)
	{
		U16		pAnimDataIdx[kMaxDisabledBatchSize];
		U16		pFinishedItems[kMaxDisabledBatchSize];
		U32		numFinished = 0;

		U32F numItems = pipe.GetNextItems(updateID, pAnimDataIdx, batchSize);
		if (numItems < batchSize)
			exitWorker = true;

		for (U32F idx = 0; idx < numItems; idx++)
		{
			sentinel.Check();

			U64 iAnimData = pAnimDataIdx[idx];
			FgAnimData* pAnimData = updateInfo.GetAnimData(iAnimData);

			if (iAnimData >= iLastAvailable)
			{
				iLastAvailable = pipe.WaitForItem(updateID, iAnimData);
			}
			if (pipe.IsItemSet(updateID, iAnimData))	// skip if this bit has been cleared by process update
			{
				//ALWAYS_ASSERT(EngineComponents::GetAnimMgr()->IsDisabled(pAnimData));

				pFinishedItems[numFinished] = iAnimData;
				numFinished++;

				if (pAnimData->m_hProcess.HandleValid())
				{
					AnimateDisabledObject(pAnimData, pData->m_deltaTime);
				}
			}
		}

		if (numFinished)
			pipe.FinishItems(updateID, pFinishedItems, numFinished);

		ndjob::Yield();
	}
}

static void	RunWorkerJobs(ProcessUpdate::Manager& updateInfo, ProcessUpdate::Pipeline& pipe, ProcessUpdate::PassId pass, ndjob::JobStartFunc entryPoint, AnimWorkerData* animWorkerData, ndjob::Priority pri, U64 jobFLags, ndjob::CounterHandle pCounter, bool hardSync = false)
{
	ASSERT(pCounter);

	U32F numPassJobs = Min(pipe.NumActive(pass), kMaxAnimWorkers);
	if (!numPassJobs)
		return;

	ndjob::JobDecl* pJobDecls = NDI_NEW(kAlign64) ndjob::JobDecl[numPassJobs];

	pCounter->Add(numPassJobs);
	for (U32F iWorker = 0; iWorker < numPassJobs; ++iWorker)
	{
		pJobDecls[iWorker] = ndjob::JobDecl(entryPoint, (uintptr_t)animWorkerData);
		pJobDecls[iWorker].m_associatedCounter = pCounter;
		pJobDecls[iWorker].m_flags |= jobFLags;
	}
	ndjob::RunJobs(pJobDecls, numPassJobs, nullptr, FILE_LINE_FUNC, pri);

	if (hardSync && pCounter)
		ndjob::WaitForCounter(pCounter);
}

void AnimMgr::AsyncUpdate(U32F bucket, ProcessUpdate::Manager& updateInfo, ProcessUpdate::Pipeline& pipe, bool gameplayIsPaused)
{
	bool hardSync = EngineComponents::GetProcessMgr()->m_useHardSyncPoints;

	const float animSysDeltaTime = GetAnimationSystemDeltaTime();

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	// Disallow processes handles from this bucket to be used
	//EngineComponents::GetAnimMgr()->LockBucket(bucket);
	ANIMLOG("AsyncUpdate bucket %d", bucket);

	{
		ProcessUpdate::PipelineId	pipeId = pipe.GetPipelineId();
		bool						classicPipe = (pipeId == ProcessUpdate::kClassic);
		ndjob::CounterHandle		pCounter = pipe.GetAnimCounter();

		const U64 numAnimDataToUpdate = pipe.NumActive(ProcessUpdate::kAnimStep);
		const U64 numDisabledDataToUpdate = pipe.NumActive(ProcessUpdate::kAnimDisabled);
		if (!numAnimDataToUpdate && !numDisabledDataToUpdate)
			return;

		// Set up job data
		AnimWorkerData* animWorkerData = NDI_NEW(kAllocSingleFrame, kAlign16) AnimWorkerData;

		animWorkerData->m_pUpdateInfo = &updateInfo;
		animWorkerData->m_pPipeline = &pipe;
		animWorkerData->m_animSysDeltaTime = animSysDeltaTime;
		animWorkerData->m_disabledBatchSize = Min(kMaxDisabledBatchSize, ((U32)numDisabledDataToUpdate + kMaxAnimWorkers - 1) / kMaxAnimWorkers);
		animWorkerData->m_gameplayIsPaused = gameplayIsPaused;
		animWorkerData->m_pAnimPhasePluginFunc = m_pAnimPhasePluginFunc;
		animWorkerData->m_pRigPhasePluginFunc = m_pRigPhasePluginFunc;

		animWorkerData->m_deltaTime = animSysDeltaTime;

		if (classicPipe && m_numBucketObservers[kPreAnimUpdate] /*&& !gameplayIsPaused*/)
		{
			PROFILE(Animation, NotifyBucketObservers_PreAnimUpdate);
			NotifyBucketObservers(bucket, kPreAnimUpdate);
		}

		// TBD:
		// * job affinity and priority (kicking jobs in AsyncUpdate)

		bool animStepObservers = (classicPipe && m_numBucketObservers[kPostAnimUpdate] && !gameplayIsPaused);
		bool animBlendObservers = (classicPipe && m_numBucketObservers[kPostAnimBlending] && !gameplayIsPaused);
		bool jointBlendObservers = (classicPipe && m_numBucketObservers[kPostJointUpdate] && !gameplayIsPaused);

		if (!hardSync && !animStepObservers && !animBlendObservers && bucket != kProcessBucketCharacter)
		{
			// If we can we just do all the anim update for each object in one swipe to reduce job overhead
			// Not for characters though because their anim update is really long and some more granularity is actually good
			if (numAnimDataToUpdate)
			{
				ndjob::Priority pri = (pipeId == ProcessUpdate::kClassic) ? ndjob::Priority::kAboveNormal : ndjob::Priority::kGameFrameNormal;
				RunWorkerJobs(updateInfo, pipe, ProcessUpdate::kAnimStep, AnimWorker, animWorkerData, pri, ndjob::kRequireLargeStack, pCounter, jointBlendObservers);
			}
		}
		else
		{
			if (numAnimDataToUpdate)
			{
				ndjob::Priority pri = (pipeId == ProcessUpdate::kClassic) ? ndjob::Priority::kAboveNormal : ndjob::Priority::kGameFrameNormal;

				// AnimStep
				RunWorkerJobs(updateInfo, pipe, ProcessUpdate::kAnimStep, AnimStepWorker, animWorkerData, pri, 0, pCounter, hardSync || animStepObservers);
			}

			if (animStepObservers)
			{
				PROFILE(Animation, NotifyBucketObservers_PostAnimUpdate);

				Memory::PushAllocator(kAllocInvalid, FILE_LINE_FUNC);
				NotifyBucketObservers(bucket, kPostAnimUpdate);
				Memory::PopAllocator();
			}

			if (numAnimDataToUpdate)
			{
				ndjob::Priority pri = (pipeId == ProcessUpdate::kClassic) ? ndjob::Priority::kAboveNormal : ndjob::Priority::kGameFrameNormal;

				// AnimBlend
				RunWorkerJobs(updateInfo, pipe, ProcessUpdate::kAnimBlend, AnimBlendWorker, animWorkerData, pri, 0, pCounter, hardSync || animBlendObservers);
			}

			if (animBlendObservers)
			{
				PROFILE(Animation, NotifyBucketObservers_PostBlending);

				Memory::PushAllocator(kAllocInvalid, FILE_LINE_FUNC);
				NotifyBucketObservers(bucket, kPostAnimBlending);
				Memory::PopAllocator();
			}

			if (numAnimDataToUpdate)
			{
				ndjob::Priority pri = (pipeId == ProcessUpdate::kClassic) ? ndjob::Priority::kAboveNormal : ndjob::Priority::kGameFrameNormal;

				// JointBlend
				RunWorkerJobs(updateInfo, pipe, ProcessUpdate::kJointBlend, JointBlendWorker, animWorkerData, pri, 0, pCounter, hardSync || jointBlendObservers);
			}
		}

		if (jointBlendObservers)
		{
			PROFILE(Animation, NotifyBucketObservers_PostJointUpdate);

			Memory::PushAllocator(kAllocInvalid, FILE_LINE_FUNC);
			NotifyBucketObservers(bucket, kPostJointUpdate);
			Memory::PopAllocator();
		}

		if (numDisabledDataToUpdate)
		{
			// Disabled jobs in fast pipe and before character bucket will be allowe to spill over to following buckets with low priority
			ndjob::Priority pri = (pipeId == ProcessUpdate::kClassic || bucket >= kProcessBucketCharacter) ? ndjob::Priority::kGameFrameNormal : ndjob::Priority::kGameFrameLowest;

			// Disabled Jobs
			// TBD parallel update (after bucket finish)
			RunWorkerJobs(updateInfo, pipe, ProcessUpdate::kAnimDisabled, DisabledWorker, animWorkerData, pri, 0, pCounter, hardSync);
		}
	}

	// Allow processes handles from this bucket to be used again
	//EngineComponents::GetAnimMgr()->LockBucket(0xFFFFFFFF);

	// All bucket observers are cleared now as they need to re-register every frame
	// and only will work within the current bucket
	//ClearBucketObservers();
}
