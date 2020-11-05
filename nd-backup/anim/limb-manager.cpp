/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/limb-manager.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/artitem.h"

#include "ndlib/nd-config.h"
#include "ndlib/nd-frame-state.h"

#include "corelib/util/bigsort.h"

static void SwapBitsInWord(LimbLockBits& word, const LimbLockBits bitA, const LimbLockBits bitB)
{
	const bool bitAValue = word & bitA;
	const bool bitBValue = word & bitB;

	word = (word & ~bitA) | (bitBValue ? bitA : 0);
	word = (word & ~bitB) | (bitAValue ? bitB : 0);
}

LimbLockBits FlipLimbLockBits(const LimbLockBits bits)
{
	LimbLockBits flippedBits = bits;

	SwapBitsInWord(flippedBits, kLockArmL, kLockArmR);

	return flippedBits;
}

bool LimbLock::operator==(const LimbLock& other) const
{
	return
		(m_lockIndex == other.m_lockIndex) &&
		(m_lockCounter == other.m_lockCounter) &&
		(m_limbs == other.m_limbs);
}

static I64 GetCurGameFrame()
{
	return EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;
}

struct LimbLockRecord
{
	LimbLock m_lock;
	I64 m_frame = -1;
	LimbLockRequest m_request;
	LimbLockPriority m_priority = -1;

	bool IsValid() const { return m_lock.IsValid(); }

	bool IsCurrent() const
	{
		if (!IsValid())
			return false;

		return GetCurGameFrame() - 1 <= m_frame;
	}

	LimbLockPriority Priority() const
	{
		return m_priority;
	}

	StringId64 Subsystem() const
	{
		return m_request.m_subsystem;
	}
};

const U32 kMaxLocks = 10;

bool IsIn(StringId64 subsystem, const StringId64* subsystemsToOverrule)
{
	if (subsystem != INVALID_STRING_ID_64)
	{
		for (int i = 0; i < LimbLockRequest::kNumSubsystems; ++i)
		{
			if (subsystem == subsystemsToOverrule[i])
				return true;
		}
	}

	return false;
}

bool Coexist(StringId64 subsystem, const StringId64* subsystemsToCoexistWith)
{
	if (subsystem != INVALID_STRING_ID_64)
	{
		for (int i = 0; i < LimbLockRequest::kNumSubsystems; ++i)
		{
			if (subsystem == subsystemsToCoexistWith[i])
				return true;
		}
	}

	return false;
}

I32 SortByPriority(const LimbLockRecord* pRecordA, const LimbLockRecord* pRecordB)
{
	const LimbLockPriority A = pRecordA->Priority();
	const LimbLockPriority B = pRecordB->Priority();

	return
		A < B ? -1 :
		A > B ? +1 :
		0;
}

class LimbManager : public ILimbManager
{
public:
	LimbLockRecord m_locks[kMaxLocks];

	U32 m_lockCounter;

	LimbManagerConfig* m_pConfig;

	LimbManager()
		: m_lockCounter(0)
		, m_pConfig(nullptr)
	{}

	virtual void Init(LimbManagerConfig* pConfig) override
	{
		m_pConfig = pConfig;
	}

	virtual void Relocate(const ptrdiff_t offset_bytes, const uintptr_t lowerBound, const uintptr_t upperBound) override
	{
		RelocatePointer(m_pConfig, offset_bytes, lowerBound, upperBound);
	}

	LimbLockRecord* AllocRecord(const bool incrementCounter)
	{
		for (U32 i = 0; i < kMaxLocks; ++i)
		{
			LimbLockRecord& lock = m_locks[i];
			if (!lock.IsCurrent())
			{
				if (incrementCounter)
				{
					++m_lockCounter;
				}

				lock = LimbLockRecord();
				lock.m_lock.m_lockIndex = i;
				lock.m_lock.m_lockCounter = m_lockCounter;
				lock.m_frame = GetCurGameFrame();

				return &lock;
			}
		}

		return nullptr;
	}

	virtual bool WouldGetLock(const LimbLockRequest& request, LimbLockDebugInfo* pOutDebugInfo) override
	{
		LimbLockRequest requestCopy(request);

		requestCopy.m_dryRun = true;

		return GetLock(requestCopy, pOutDebugInfo).IsValid();
	}

	bool MutualCoexist(const LimbLockRequest& requestA, const LimbLockRequest& requestB) const
	{
		const LimbLockPriority priorityA = m_pConfig->GetPriorityFor(requestA);
		const LimbLockPriority priorityB = m_pConfig->GetPriorityFor(requestB);

		return
			(priorityB < priorityA || Coexist(requestA.m_subsystem, requestB.m_subsystemsToCoexistWith)) &&
			(priorityA < priorityB || Coexist(requestB.m_subsystem, requestA.m_subsystemsToCoexistWith));
	}

	LimbLock GetLock(const LimbLockRequest& request, LimbLockDebugInfo* pOutDebugInfo) override
	{
		if (request.m_limbs == 0)
		{
			// Return a lock that looks valid. It doesn't matter.

			LimbLock ret;
			ret.m_lockIndex = 0;

			return ret;
		}

		const LimbLockPriority requestPriority = m_pConfig->GetPriorityFor(request);

		for (U32 i = 0; i < kMaxLocks; ++i)
		{
			const LimbLockRecord& lock = m_locks[i];
			if (lock.IsCurrent() && lock.Priority() >= requestPriority)
			{
				if (lock.m_lock.m_limbs & request.m_limbs)
				{
					if (!IsIn(lock.Subsystem(), request.m_subsystemsToOverrule))
					{
						if (!MutualCoexist(request, lock.m_request))
						{
							if (pOutDebugInfo) pOutDebugInfo->m_overrulingSubsystem = lock.Subsystem();
							return LimbLock();
						}
					}
				}
			}
		}

		LimbLockRecord* pRet = AllocRecord(!request.m_dryRun);
		if (!pRet)
		{
			return LimbLock();
		}

		if (!request.m_dryRun)
		{
			// We acquired the lock; Blow away all locks that other subsystems had on these limbs
			for (U32 i = 0; i < kMaxLocks; ++i)
			{
				LimbLockRecord& lock = m_locks[i];
				if (lock.IsCurrent() && (lock.m_lock.m_limbs & request.m_limbs) && !MutualCoexist(request, lock.m_request))
				{
					lock = LimbLockRecord();
				}
			}
		}

		pRet->m_lock.m_limbs = request.m_limbs;
		pRet->m_request = request;
		pRet->m_priority = requestPriority;

		LimbLock ret = pRet->m_lock;

		if (request.m_dryRun)
		{
			*pRet = LimbLockRecord();
		}

		return ret;
	}

	bool HoldLock(const LimbLock& lock, LimbLockDebugInfo* pOutDebugInfo) override
	{
		if (lock.m_limbs == 0)
		{
			return true;
		}

		const I32 i = lock.m_lockIndex;
		if (0 <= i && i < kMaxLocks)
		{
			if (m_locks[i].IsCurrent() && m_locks[i].m_lock == lock)
			{
				m_locks[i].m_frame = GetCurGameFrame();

				return true;
			}
		}

		if (pOutDebugInfo)
		{
			for (U32 j = 0; j < kMaxLocks; ++j)
			{
				if (m_locks[j].IsCurrent() && (m_locks[j].m_lock.m_limbs & lock.m_limbs))
				{
					pOutDebugInfo->m_overrulingSubsystem = m_locks[j].Subsystem();
					break;
				}
			}
		}

		return false;
	}

	void ReleaseLock(const LimbLock& lock) override
	{
		const I32 i = lock.m_lockIndex;
		if (0 <= i && i < kMaxLocks)
		{
			if (m_locks[i].IsCurrent() && m_locks[i].m_lock == lock)
			{
				m_locks[i] = LimbLockRecord();
			}
		}
	}

	void DebugDraw(const NdGameObject& owner) override
	{
		const char* bitNames[] =
		{
			"ArmL",
			"ArmR",
		};

		STATIC_ASSERT(ARRAY_COUNT(bitNames) == kNumLimbLockBits);

		MsgCon("Limb lock bits for %s: ", owner.GetName());

		for (U32 i = 0; i < kNumLimbLockBits; ++i)
		{
			const LimbLockBits bit = static_cast<LimbLockBits>(1 << i);

			FixedArray<const LimbLockRecord*, kMaxLocks> lockingRecords;
			for (U32 j = 0; j < kMaxLocks; ++j)
			{
				if (m_locks[j].IsCurrent() && (m_locks[j].m_lock.m_limbs & bit))
				{
					lockingRecords.PushBack(&(m_locks[j]));
				}
			}

			QuickSort(lockingRecords.Data(), lockingRecords.Size(), SortByPriority);

			if (!lockingRecords.Empty())
			{
				MsgCon("%s(", bitNames[i]);

				bool first = true;
				for (const LimbLockRecord* pRecord : lockingRecords)
				{
					MsgCon("%s%s", first ? "" : " ", DevKitOnly_StringIdToString(pRecord->Subsystem()));
					first = false;
				}

				MsgCon(") ");
			}
		}

		MsgCon("\n");
	}

	ptrdiff_t GetSize() const override
	{
		return sizeof(*this);
	}
};

ILimbManager* CreateLimbManager()
{
	return NDI_NEW LimbManager;
}

LimbLockBits GetLimbLockBitsForAnim(const ArtItemAnim* pAnim, const NdGameObject* pObject)
{
	LimbLockBits bits = static_cast<LimbLockBits>(0x0);

	const ndanim::ValidBits* pValidBits = ndanim::GetValidBitsArray(pAnim->m_pClipData, 0);
	const ndanim::ValidBits validBits = *pValidBits;

	const StringId64 leftArmJoints[] =
	{
		SID("l_shoulder"),
		SID("l_elbow"),
		SID("l_wrist"),
		SID("l_hand_prop_attachment"),
	};

	const StringId64 rightArmJoints[] =
	{
		SID("r_shoulder"),
		SID("r_elbow"),
		SID("r_wrist"),
		SID("r_hand_prop_attachment"),
	};

	for (int i = 0; i < ARRAY_COUNT(leftArmJoints); ++i)
	{
		const StringId64 joint = leftArmJoints[i];

		const I32F jointIndex = pObject->FindJointIndex(joint);
		if (jointIndex >= 0)
		{
			if (validBits.IsBitSet(jointIndex))
			{
				bits = static_cast<LimbLockBits>(bits | kLockArmL);
				break;
			}
		}
	}

	for (int i = 0; i < ARRAY_COUNT(rightArmJoints); ++i)
	{
		const StringId64 joint = rightArmJoints[i];

		const I32F jointIndex = pObject->FindJointIndex(joint);
		if (jointIndex >= 0)
		{
			if (validBits.IsBitSet(jointIndex))
			{
				bits = static_cast<LimbLockBits>(bits | kLockArmR);
				break;
			}
		}
	}

	return bits;
}
