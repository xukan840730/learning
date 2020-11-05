/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/util/bit-array.h"

#include "ndlib/anim/anim-commands.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/process/process.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ArtItemSkeleton;
namespace ProcessUpdate
{
	class Manager;
	class Pipeline;
};
namespace ndanim 
{
	struct JointParams;
}


/// --------------------------------------------------------------------------------------------------------------- ///
class AnimMgr
{
public:

#ifdef HEADLESS_BUILD
	static const U32 kMaxAnimData = 4096;
#else
	static const U32 kMaxAnimData = 2244;
#endif

	static const U32 kNumBuckets = 9;

	static const U32 kMaxBucketObservers = 32;

	enum
	{
		kPostProcessUpdate,
		kPreAnimUpdate,
		kPostAnimUpdate,
		kPostAnimBlending,
		kPostJointUpdate,
		kNumAnimUpdateTypes
	};

	enum
	{
		kPostProcessUpdateMask = 1UL << kPostProcessUpdate,
		kPreAnimUpdateMask = 1UL << kPreAnimUpdate,
		kPostAnimUpdateMask = 1UL << kPostAnimUpdate,
		kPostAnimBlendingMask = 1UL << kPostAnimBlending,
		kPostJointUpdateMask = 1UL << kPostJointUpdate,
	};

	typedef BitArray<kMaxAnimData>	AnimDataBits;

	AnimMgr();
	void Init();
	void Shutdown();

	void RegisterAnimPhasePluginHandler(AnimPhasePluginCommandHandler* pAnimPhasePluginFunc);
	void RegisterRigPhasePluginHandler(RigPhasePluginCommandHandler* pRigPhasePluginFunc);
	AnimPhasePluginCommandHandler* GetAnimPhasePluginHandler() const;
	RigPhasePluginCommandHandler* GetRigPhasePluginHandler() const;

	//////////////////////////////////////////////////////////////////////////
	// Anim Data
	//////////////////////////////////////////////////////////////////////////
	FgAnimData* AllocateAnimData(U32F maxNumDrivenInstances, Process* pProcess);
	void FreeAnimData(FgAnimData* pElement);
	U32F GetNumAllocatedAnimData() const
	{
		return kMaxAnimData;
	}
	U32F GetNumUsedAnimData() const
	{
		return m_usedAnimData.CountSetBits();
	}

	ptrdiff_t GetAnimDataIndex(const FgAnimData* pElement) const
	{
		return pElement - m_pAnimDataArray;
	}

	const FgAnimData* GetAnimData(U32 index) const
	{
		return m_pAnimDataArray + index;
	}

	void SetAnimationDisabled(FgAnimData* pElement, FgAnimData::UserAnimationPassFunctor pDisabledFunc);
	void SetAnimationEnabled(FgAnimData* pElement);
	bool IsDisabled(const FgAnimData* pElement) const;

	void SetAnimationZombie(FgAnimData* pElement);

	void SetIsInBindPose(FgAnimData* pElement, bool isInBindPose);

	void SetVisible(FgAnimData* pElement, bool isVisible);
	bool IsVisible(const FgAnimData* pElement) const;
	void ClearAllVisibleBits();

	void KickEarlyDeferredAnimCmd(const FgAnimData* pAnimData, U32 segmentMaskGame, U32 segmentMaskRender);
	void WaitForAllEarlyDeferredAnimGameJobs();
	void WaitForAllEarlyDeferredAnimRenderJobs();

	void ChangeBucket(FgAnimData* pElement, U32F newBucket);
	U32F FindBucketForAnimData(const FgAnimData* pElement) const;

	// Update passes
	void FrameBegin();
	void BucketBegin(U32F bucket);

	// For quicker PrepareInstanceForRendering loops
	I32 GetTotalUsedAnimData(I32 bucket)
	{
		ALIGNED(128) AnimDataBits bits;
		AnimDataBits::BitwiseAndComp(&bits, m_bucketAnimData[bucket], m_newAnimData);
		AnimDataBits::BitwiseAndComp(&bits, bits, m_zombieAnimData);
		return bits.CountSetBits();
	}

	I32 GetLastUsedAnimIndex()
	{
		ALIGNED(128) AnimDataBits bits;
		AnimDataBits::BitwiseAndComp(&bits, m_usedAnimData, m_newAnimData);
		AnimDataBits::BitwiseAndComp(&bits, bits, m_zombieAnimData);
		return bits.FindLastSetBit();
	}

	I32 GetTotalUsedAnimData()
	{
		ALIGNED(128) AnimDataBits bits;
		AnimDataBits::BitwiseAndComp(&bits, m_usedAnimData, m_newAnimData);
		AnimDataBits::BitwiseAndComp(&bits, bits, m_zombieAnimData);
		return bits.CountSetBits();
	}

	void FillUsedAnimData(I32 bucket, FgAnimData** apAnimData)
	{
		ALIGNED(128) AnimDataBits bits;
		AnimDataBits::BitwiseAndComp(&bits, m_bucketAnimData[bucket], m_newAnimData);
		AnimDataBits::BitwiseAndComp(&bits, bits, m_zombieAnimData);

		AnimDataBits::Iterator bitIter(bits);
		U64 firstElementIndex = bitIter.First();
		I32 count = 0;
		if (firstElementIndex < kMaxAnimData)
		{
			// Ok we have at least one used element...
			U64 currentElementIndex = firstElementIndex;

			while (currentElementIndex < kMaxAnimData)
			{
				// This is a look-ahead index so that we can prefetch
				U64 nextElementIndex = bitIter.Advance();

				FgAnimData* pAnimData = m_pAnimDataArray + currentElementIndex;

				apAnimData[count++] = pAnimData;

				currentElementIndex = nextElementIndex;
			}
		}
	}

	// Iterator helper
	typedef void (*FgAnimDataFunctor)(FgAnimData* pAnimData, uintptr_t data);

	void ForAllUsedAnimDataInBlock(FgAnimDataFunctor funcPtr, U8 block, uintptr_t data = 0)
	{
		ALIGNED(128) AnimDataBits bits;
		ALIGNED(128) AnimDataBits blockMask;
		blockMask.SetAllBits();
		blockMask.SetBlock(block, 0);

		AnimDataBits::BitwiseAndComp(&bits, m_usedAnimData, m_newAnimData);
		AnimDataBits::BitwiseAndComp(&bits, bits, m_zombieAnimData);
		AnimDataBits::BitwiseAndComp(&bits, bits, blockMask);
		ForAllAnimData(bits, funcPtr, data);
	}

	void ForAllUsedAnimData(FgAnimDataFunctor funcPtr, uintptr_t data = 0)
	{
		ALIGNED(128) AnimDataBits bits;
		AnimDataBits::BitwiseAndComp(&bits, m_usedAnimData, m_newAnimData);
		AnimDataBits::BitwiseAndComp(&bits, bits, m_zombieAnimData);
		ForAllAnimData(bits, funcPtr, data);
	}

	void ForAllUsedAnimDataInBucket(U32F bucket, FgAnimDataFunctor funcPtr, uintptr_t data = 0)
	{
		ANIM_ASSERT(bucket < kNumBuckets);
		ALIGNED(128) AnimDataBits bits;
		AnimDataBits::BitwiseAndComp(&bits, m_bucketAnimData[bucket], m_newAnimData);
		AnimDataBits::BitwiseAndComp(&bits, bits, m_zombieAnimData);
		ForAllAnimData(bits, funcPtr, data);
	}

	void ForAllUsedAnimDataExcludeBucket(U32F bucket, FgAnimDataFunctor funcPtr, uintptr_t data = 0);


	void RegisterBucketObserver(Process* pProcess, U32F updateTypeMask)
	{
		AtomicLockJanitor jj(&m_spinLock, FILE_LINE_FUNC);
		for (U32F updateType = 0; updateType<kNumAnimUpdateTypes; updateType++)
		{
			if (updateTypeMask & (1UL << updateType))
			{
				for (U32F iObserver = 0; iObserver < m_numBucketObservers[updateType]; ++iObserver)
				{
					if (m_bucketObservers[iObserver][updateType].GetProcessId() == pProcess->GetProcessId())
						continue;
				}
				ANIM_ASSERT(m_numBucketObservers[updateType] < kMaxBucketObservers);
				m_bucketObservers[m_numBucketObservers[updateType]++][updateType] = pProcess;
			}
		}
	}
	U32	NumBucketObservers(U32F eventType) { return m_numBucketObservers[eventType]; }

	// Bucket locking interface
	void LockBucket(U32 bucket);
	void SetBucketLockExcludeAnimData(const FgAnimData* pAnimData);
	bool IsAnimDataLocked(const FgAnimData* pAnimData) const;

	void ExternalBucketEvent(U32F bucket, U32F eventType);

	// new update
	void GetAnimDataBits(U32F bucket, AnimDataBits& bits, AnimDataBits& disabledBits);
	void ForAllAnimData(ProcessUpdate::Manager& updateInfo, ProcessUpdate::Pipeline& pipe, U32 passId, FgAnimDataFunctor funcPtr, uintptr_t data);
	void AsyncUpdate(U32F bucket, ProcessUpdate::Manager& updateInfo, ProcessUpdate::Pipeline& pipe, bool gameplayIsPaused);
	void SyncUpdate(U32F bucket, ProcessUpdate::Manager& updateInfo, ProcessUpdate::Pipeline& pipe, bool gameplayIsPaused);

	void ClearBucketObservers();

	void SetParallelUpdateLock(bool val) { m_parallelUpdateLock = val; }
	bool GetParallelUpdateLock() { return m_parallelUpdateLock; }

private:

	void AnimateObjectsInBucket(U32F bucket, U32F pass, bool pauseAnimation, bool postRetargetPass);

	void ForAllAnimData(const AnimDataBits& bits, FgAnimDataFunctor funcPtr, uintptr_t data = 0);
	void NotifyBucketObservers(U32F bucket, U32F updateType);

	ALIGNED(128) AnimDataBits	m_newAnimData;					// Elements allocated partly through the frame which means we need to wait until next frame to animate/render it.
	ALIGNED(128) AnimDataBits	m_usedAnimData;					// Which elements in the element array are used
	ALIGNED(128) AnimDataBits	m_deletedAnimData[3 /*kMaxNumFramesInFlight*/];			// Freed elements that need to wait until the end of the frame before being reused.
	ALIGNED(128) AnimDataBits	m_disabledAnimData;				// Which elements in the array are currently in "no animation" mode.
	ALIGNED(128) AnimDataBits	m_zombieAnimData;				// Which elements in the array are currently in "no animation" mode.
	ALIGNED(128) AnimDataBits	m_bindPoseAnimData;				// Which elements in the array are currently in bind pose.
	ALIGNED(128) AnimDataBits	m_visibleAnimData;				// Which elements in the array are currently visible (per FG vis culling).
	ALIGNED(128) AnimDataBits	m_bucketAnimData[kNumBuckets];	// Elements in a bucket are listed here

	U32				m_lastAllocDataIndex;

	FgAnimData*		m_pAnimDataArray;

	MutableProcessHandle	m_bucketObservers[kMaxBucketObservers][kNumAnimUpdateTypes];
	U32				m_numBucketObservers[kNumAnimUpdateTypes];

	// Locking variables
	U32					m_bucketLock;
	const FgAnimData*	m_bucketLockExcludeAnimData;
	bool				m_parallelUpdateLock;

	NdAtomicLock		m_spinLock;

	AnimPhasePluginCommandHandler* m_pAnimPhasePluginFunc;
	RigPhasePluginCommandHandler* m_pRigPhasePluginFunc;

	ndjob::CounterHandle m_pEarlyDeferredAnimGameCounter;
	ndjob::CounterHandle m_pEarlyDeferredAnimRenderCounter;
};
STATIC_ASSERT(AnimMgr::AnimDataBits::Storage::kMaxBits >= AnimMgr::kMaxAnimData);


struct AnimFrameExecutionData
{
	AnimFrameExecutionData() : m_pAnimExecutionContexts(nullptr), m_pUsedAnimExecutionContexts(nullptr), m_lock(0) {}

	AnimExecutionContext*		m_pAnimExecutionContexts;						// [kMaxAnimData] *not* default-constructed
	AnimMgr::AnimDataBits*		m_pUsedAnimExecutionContexts;					// [kMaxAnimData] bits - Used or not used
	NdAtomic32					m_lock;
};

/// --------------------------------------------------------------------------------------------------------------- ///
//#define ENABLE_ANIMLOG
#ifdef ENABLE_ANIMLOG
extern char* GetNextAnimLog();
extern size_t GetAnimLogEntrySize();
template <typename... ARGS>
void ANIMLOG(char const* const format, ARGS const&... args)
{
	snprintf(GetNextAnimLog(), GetAnimLogEntrySize(), format, args...);
}
#else
template <typename... ARGS>
void ANIMLOG(char const* const format, ARGS const&... args)
{
}
#endif
