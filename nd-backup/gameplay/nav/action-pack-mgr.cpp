/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/action-pack-mgr.h"

#include "corelib/math/segment-util.h"

#include "ndlib/profiling/profiling.h"

#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/cover-action-pack.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-ledge-graph.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/nd-action-pack-util.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/level/level.h"
#include "gamelib/region/region.h"
#include "gamelib/spline/catmull-rom.h"

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPackMgr ActionPackMgr::m_singleton;
bool g_skipApObserversOnUnregister = false; // take me out after we optimize AiPostMgr::UnregisterActionPack

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPackMutex::ActionPackMutex()
	: m_nameId(INVALID_STRING_ID_64)
	, m_ownerRefCount(0)
	, m_userRefCount(0)
{
	m_hEnabledAp = ActionPackHandle();

	for (U32F iAp = 0; iAp < kMaxActionPackCount; ++iAp)
	{
		m_hOwnerList[iAp] = ActionPackHandle();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMutex::Init(StringId64 nameId)
{
	m_nameId = nameId;
	m_ownerRefCount = 0;
	m_userRefCount = 0;
	m_hEnabledAp = ActionPackHandle();
	for (U32F iAp = 0; iAp < kMaxActionPackCount; ++iAp)
	{
		m_hOwnerList[iAp] = ActionPackHandle();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMutex::Reset()
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	if (FALSE_IN_FINAL_BUILD(g_ndAiOptions.m_aggressiveApMutexValidation))
	{
		ValidateNoLock();
	}

	m_userRefCount = 0;

	for (U32F i = 0; i < kMaxUserCount; ++i)
	{
		m_userList[i] = INVALID_STRING_ID_64;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackMutex::IsValid() const
{
	return m_ownerRefCount > 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMutex::Validate() const
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);
	ValidateNoLock();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMutex::ValidateNoLock() const
{
	U32F numValidOwners = 0;

	for (U32F iAp = 0; iAp < kMaxActionPackCount; ++iAp)
	{
		if (m_hOwnerList[iAp].ToActionPack())
		{
			++numValidOwners;
		}
	}

	NAV_ASSERT(numValidOwners == m_ownerRefCount);

	U32F numValidUsers = 0;

	for (U32F iUser = 0; iUser < kMaxUserCount; ++iUser)
	{
		if (m_userList[iUser])
		{
			++numValidUsers;
		}
	}

	NAV_ASSERT(numValidUsers == m_userRefCount);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMutex::AddOwnerRef(ActionPack* pAp)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	NAV_ASSERT(pAp);
	if (!pAp)
		return;

	if (m_ownerRefCount >= kMaxActionPackCount)
	{
		MsgConErr("ActionPack Mutex '%s' has too many registered Aps (trying to add '%s' '%s', max count is %d)\n",
				  DevKitOnly_StringIdToString(m_nameId),
				  DevKitOnly_StringIdToString(pAp->GetSpawnerId()),
				  pAp->GetName(),
				  I32(kMaxActionPackCount));
		MsgConErr("   Previous Owners:\n");

		for (U32F iAp = 0; iAp < kMaxActionPackCount; ++iAp)
		{
			if (ActionPack* pOwnerAp = m_hOwnerList[iAp].ToActionPack())
			{
				MsgConErr("   - %s %s\n", DevKitOnly_StringIdToString(pOwnerAp->GetSpawnerId()), pAp->GetName());
			}
		}

		return;
	}

	bool wasAdded = false;
	for (U32F iAp = 0; iAp < kMaxActionPackCount; ++iAp)
	{
		ActionPack* pOwnerAp = m_hOwnerList[iAp].ToActionPack();
		if (pOwnerAp == nullptr || pOwnerAp == pAp)
		{
			m_hOwnerList[iAp] = pAp;
			wasAdded = true;
			++m_ownerRefCount;
			break;
		}
	}

	NAV_ASSERT(wasAdded);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMutex::RemoveOwnerRef(ActionPack* pAp)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	NAV_ASSERT(pAp);
	if (!pAp)
		return;
	NAV_ASSERT(m_ownerRefCount > 0);
	bool wasRemoved = false;
	for (U32F iAp = 0; iAp < kMaxActionPackCount; ++iAp)
	{
		if (m_hOwnerList[iAp] == pAp)
		{
			m_hOwnerList[iAp] = ActionPackHandle();
			wasRemoved = true;
			--m_ownerRefCount;
			break;
		}
	}
	NAV_ASSERT(wasRemoved);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackMutex::AddUserRef(Process* pChar)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	if (FALSE_IN_FINAL_BUILD(g_ndAiOptions.m_aggressiveApMutexValidation))
	{
		ValidateNoLock();
	}

	NAV_ASSERT(pChar);
	const StringId64 newUserId = pChar->GetUserId();

	bool alreadyExists = false;

	for (U32F iUser = 0; iUser < kMaxUserCount; ++iUser)
	{
		if (m_userList[iUser] == newUserId)
		{
			alreadyExists = true;
			break;
		}
	}

	bool wasAdded = false;

	if (!alreadyExists)
	{
		for (U32F iUser = 0; iUser < kMaxUserCount; ++iUser)
		{
			if (!m_userList[iUser])
			{
				m_userList[iUser] = newUserId;
				wasAdded = true;
				break;
			}
		}
	}

	if (wasAdded)
	{
		++m_userRefCount;
	}

	if (FALSE_IN_FINAL_BUILD(g_ndAiOptions.m_aggressiveApMutexValidation))
	{
		NAV_ASSERT(wasAdded || alreadyExists);

		ValidateNoLock();
	}

	return wasAdded || alreadyExists;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackMutex::TryAddUserRef(ActionPack* pAp, Process* pChar)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	if (!TryEnableUnsafe(pAp, pChar))
	{
		return false;
	}

	if (FALSE_IN_FINAL_BUILD(g_ndAiOptions.m_aggressiveApMutexValidation))
	{
		ValidateNoLock();
	}

	NAV_ASSERT(pChar);
	const StringId64 newUserId = pChar->GetUserId();

	bool alreadyExists = false;

	for (U32F iUser = 0; iUser < kMaxUserCount; ++iUser)
	{
		if (m_userList[iUser] == newUserId)
		{
			alreadyExists = true;
			break;
		}
	}

	bool wasAdded = false;

	if (!alreadyExists)
	{
		for (U32F iUser = 0; iUser < kMaxUserCount; ++iUser)
		{
			if (!m_userList[iUser])
			{
				m_userList[iUser] = newUserId;
				wasAdded = true;
				break;
			}
		}
	}

	if (wasAdded)
	{
		++m_userRefCount;
	}

	if (FALSE_IN_FINAL_BUILD(g_ndAiOptions.m_aggressiveApMutexValidation))
	{
		NAV_ASSERT(wasAdded || alreadyExists);

		ValidateNoLock();
	}

	return wasAdded || alreadyExists;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackMutex::RemoveUserRef(Process* pChar)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	NAV_ASSERT(pChar);

	if (FALSE_IN_FINAL_BUILD(g_ndAiOptions.m_aggressiveApMutexValidation))
	{
		ValidateNoLock();
	}

	const StringId64 userId = pChar->GetUserId();

	bool wasRemoved = false;
	for (U32F iUser = 0; iUser < kMaxUserCount; ++iUser)
	{
		if (m_userList[iUser] == userId)
		{
			m_userList[iUser] = INVALID_STRING_ID_64;
			wasRemoved = true;
			break;
		}
	}

	if (wasRemoved)
	{
		--m_userRefCount;
	}

	if (FALSE_IN_FINAL_BUILD(g_ndAiOptions.m_aggressiveApMutexValidation))
	{
		NAV_ASSERT(wasRemoved);

		ValidateNoLock();
	}

	return wasRemoved;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// IsAvailable(ActionPack* pWantAp)
//
// Check if the given action pack is available to be reserved.  The mutex arbitrates amongst 2 or more action packs,
// of which only one can be used at a time.  The mutex keeps track of which of its action packs is enabled and how
// many users are using the enabled action pack.
// The mutex will only be available if:
//     - the enabled ap is not reserved, or reserved by the requesting process (only one ap may be reserved at a time),
//     - AND, the action pack we want to reserve is the enabled one OR there are no users
//
//	NEW: Directional Valve Mode: "infinite" AP reservations makes TAPs usable by everyone unconditionally*
//		* only for the same AP (e.g. everyone can use the jump down as long as no one is doing a jump up)
//		** capped by maximum user count just so we don't break validation, but non-trivial odds the infected
//			horde will bump into this limitation
//
bool ActionPackMutex::IsAvailable(const ActionPack* pWantAp, const Process* pProcess) const
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	return IsAvailableUnsafe(pWantAp, pProcess);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackMutex::IsAvailableUnsafe(const ActionPack* pWantAp, const Process* pProcess) const
{
	if (FALSE_IN_FINAL_BUILD(g_ndAiOptions.m_aggressiveApMutexValidation))
	{
		ValidateNoLock();
	}

	bool isAvail = true;
	if (ActionPack* pEnabledAp = m_hEnabledAp.ToActionPack())
	{
		const bool enabledApIsWantAp = (pEnabledAp == pWantAp);
		const bool hasUsers = m_userRefCount > 0;
		const bool enabledApIsValid = enabledApIsWantAp || !hasUsers;

		if (enabledApIsValid && m_directionalValveEnabled
			&& TRUE_IN_FINAL_BUILD(!g_navCharOptions.m_traversal.m_disableUnboundedReservations))
		{
			isAvail = (m_userRefCount < kMaxUserCount) || IsUserUnsafe(pProcess);
		}
		else if (const Process* pReservationHolder = pEnabledAp->GetReservationHolder())
		{
			if (pReservationHolder != pProcess)
			{
				// the enabled action pack is reserved by someone else, not available
				isAvail = false;
			}
		}
		else if (hasUsers)
		{
			// there are users, only the enabled action pack is available
			if (pEnabledAp != pWantAp)
			{
				isAvail = false;
			}
		}
	}
	return isAvail;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackMutex::IsUser(const Process* pProcess) const
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	return IsUserUnsafe(pProcess);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackMutex::IsUserUnsafe(const Process* pProcess) const
{
	NAV_ASSERT(pProcess);
	const StringId64 userId = pProcess->GetUserId();

	for (U32F i = 0; i < kMaxUserCount; ++i)
	{
		if (m_userList[i] == userId)
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackMutex::TryEnable(const ActionPack* pAp, const Process* pProcess)
{
	AtomicLockJanitor lock(&m_accessLock, FILE_LINE_FUNC);

	return TryEnableUnsafe(pAp, pProcess);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackMutex::TryEnableUnsafe(const ActionPack* pAp, const Process* pProcess)
{
	if (FALSE_IN_FINAL_BUILD(g_ndAiOptions.m_aggressiveApMutexValidation))
	{
		ValidateNoLock();
	}

	bool enabled = false;
	if (IsAvailableUnsafe(pAp, pProcess))
	{
		MsgAp("ActionPackMutex::TryEnable: Enabling AP %s (for process '%s')\n", pAp->GetName(), pProcess->GetName());
		enabled = true;
		m_hEnabledAp = const_cast<ActionPack*>(pAp);
	}

	return enabled;
}

//////////////////////////////////////////////////////////////////////////
///
///  class ActionPackMgr
///
//////////////////////////////////////////////////////////////////////////

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPackMgr::ActionPackMgr()
	: m_managerLock(JlsFixedIndex::kActionPackMgr, SID("ActionPackMgr"))
{
	m_usedMap.ClearAllBits();
	m_regMap.ClearAllBits();
	m_updateMap.ClearAllBits();
	m_pendingReg.ClearAllBits();
	m_pendingUnreg.ClearAllBits();
	m_apsWithOwners.ClearAllBits();
	m_dirtyTapLinkages.ClearAllBits();

	m_pActionPack = nullptr;
	m_apType = nullptr;
	m_idGen = 0;
	m_registeredActionPackCount = 0;
	m_actionPackCount = 0;
	m_peakActionPackCount = 0;
	m_peakMutexCount = 0;
	m_maxActionPackMutexCount = 0;
	m_mutexList = nullptr;

	m_fnRegisterObserver = nullptr;

	for (int i = 0; i < kMaxUnregisterObservers; ++i)
	{
		m_fnUnregisterObserver[i] = nullptr;
	}

	m_fnLoginObserver = nullptr;
	m_fnLogoutObserver = nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::Init()
{
	AllocateJanitor jj(kAllocNpcGlobals, FILE_LINE_FUNC);

	m_pActionPack = NDI_NEW ActionPack*[kMaxActionPackCount];
	NAV_ASSERT(m_pActionPack);

	memset(m_pActionPack, 0, sizeof(ActionPack*)*kMaxActionPackCount);

	m_apType = NDI_NEW U8[kMaxActionPackCount];
	NAV_ASSERT(m_apType);
	memset(m_apType, 0xff, kMaxActionPackCount);

	m_maxActionPackMutexCount = 1024;

	m_mutexList = NDI_NEW ActionPackMutex[m_maxActionPackMutexCount];
	NAV_ASSERT(m_mutexList);

	m_fnLoginObserver = nullptr;
	m_fnLogoutObserver = nullptr;
	m_fnRegisterObserver = nullptr;

	for (U32F i = 0; i < kMaxUnregisterObservers; i++)
	{
		m_fnUnregisterObserver[i] = nullptr;
	}
}

struct LevelApRefCount
{
	const Level *m_pLevel;
	I32 m_refCount;

	static I32 Compare(const LevelApRefCount &a, const LevelApRefCount &b)
	{
		return strcmp(a.m_pLevel->GetName(), b.m_pLevel->GetName());
	}
};

struct ProcessApRefCount
{
	const Process *m_pProcess;
	I32 m_refCount;
	static I32 Compare(const ProcessApRefCount &a, const ProcessApRefCount &b)
	{
		return strcmp(a.m_pProcess->GetName(), b.m_pProcess->GetName());
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::Update()
{
	PROFILE(AI, ActionPackMgr_Update);

	const U64 startTick = TimerGetRawCount();

	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

	AtomicLockJanitorWrite_Jls apWriteLock(GetLock(), FILE_LINE_FUNC);

	if (!m_dirtyTapLinkages.AreAllBitsClear())
	{
		UpdateTraversalActionPackLinkages();
	}

	g_navPathNodeMgr.Update();

	{
		PROFILE(AI, ActionPackMgr_Update_StaleDet);

		for (U32F iSlotIndex : m_apsWithOwners)
		{
			if (ActionPack* pAp = m_pActionPack[iSlotIndex])
			{
				const ProcessHandle hOwner = pAp->GetOwnerProcessHandle();
				if (!hOwner.Assigned())
					continue;
				if (hOwner.HandleValid())
					continue;

				// we had a valid owner, but he went away, nuke the ActionPack
				if (pAp->IsDynamic())
				{
					pAp->Logout();
				}
				else if (m_regMap.IsBitSet(iSlotIndex))
				{
					m_pendingReg.ClearBit(iSlotIndex);
					m_pendingUnreg.ClearBit(iSlotIndex);

					pAp->UnregisterInternal();

					Unregister(pAp);
				}
			}
		}
	}

	{
		PROFILE(AI, ActionPackMgr_Update_Unreg);

		for (U32F iSlotIndex : m_pendingUnreg)
		{
			if (ActionPack* pAp = m_pActionPack[iSlotIndex])
			{
				pAp->UnregisterInternal();

				Unregister(pAp);
			}
		}
		m_pendingUnreg.ClearAllBits();
	}

	if (!m_pendingReg.AreAllBitsClear())
	{
		PROFILE(AI, ActionPackMgr_Update_Reg);
		PROFILE_ACCUM(APMgr_Update_Register);

		U32F numRegistered = 0;
		ActionPackBits regDone(false);

		for (U32F iSlotIndex : m_pendingReg)
		{
			NAV_ASSERT(!m_regMap.IsBitSet(iSlotIndex));

			ActionPack* pAp = m_pActionPack[iSlotIndex];
			NAV_ASSERT(pAp);
			const U32F iMgrId = pAp->GetMgrId();
			NAV_ASSERT(iMgrId != kInvalidMgrId);
			const U32F iMgrSlotIndex = GetSlotIndex(iMgrId);
			NAV_ASSERT(iMgrSlotIndex < kMaxActionPackCount);
			NAV_ASSERT(iSlotIndex == iMgrSlotIndex);

			if (pAp->RegisterInternal())
			{
				Register(pAp);
			}

			regDone.SetBit(iSlotIndex);

			++numRegistered;

			if (numRegistered >= g_ndAiOptions.m_numActionPacksLoggedInPerFrame)
			{
				break;
			}
		}

		ActionPackBits::BitwiseAndComp(&m_pendingReg, m_pendingReg, regDone);
	}

	{
		PROFILE(AI, ActionPackMgr_Update_Update);

		ActionPackBits updateMap;
		ActionPackBits::BitwiseAnd(&updateMap, m_updateMap, m_regMap);

		ActionPackBits::Iterator bitIter(updateMap);

		for (U32F iSlotIndex = bitIter.First(); iSlotIndex < kMaxActionPackCount; iSlotIndex = bitIter.Advance())
		{
			PROFILE(AI, ActionPackMgr_Update_Ap);

			ActionPack* pAp = m_pActionPack[iSlotIndex];

			NAV_ASSERT(m_regMap.IsBitSet(iSlotIndex));

			if (pAp->NeedsUpdate())
			{
				pAp->Update();
			}

			m_updateMap.AssignBit(iSlotIndex, pAp->NeedsUpdate());
		}
	}

	U32 numUsedMutexes = 0;

	if (FALSE_IN_FINAL_BUILD(true))
	{
		PROFILE(Navigation, ApMgr_MutexCounting);

		for (U32F iMutex = 0; iMutex < m_maxActionPackMutexCount; ++iMutex)
		{
			//m_mutexList[iMutex].Validate();

			if (m_mutexList[iMutex].IsValid())
			{
				++numUsedMutexes;
			}
		}

		m_peakMutexCount = Max(m_peakMutexCount, numUsedMutexes);
	}

	NAV_ASSERT(m_actionPackCount == m_usedMap.CountSetBits());
	NAV_ASSERT(m_registeredActionPackCount == m_regMap.CountSetBits());

	const U64 endTick = TimerGetRawCount();

	const NavMeshDrawFilter& filter = g_navMeshDrawFilter;
	if (FALSE_IN_FINAL_BUILD(filter.m_displayApManager))
	{
		const size_t numUsed = m_usedMap.CountSetBits();
		const size_t numRegistered = m_regMap.CountSetBits();
		const size_t numPendingReg = m_pendingReg.CountSetBits();
		const size_t numPendingUnreg = m_pendingUnreg.CountSetBits();

		U32F counts[ActionPack::kActionPackCount + 1] = { 0 };
		GetTypeCounts(counts);

		const float durationMs = ConvertTicksToSeconds(endTick - startTick) * 1000.0f;

		MsgCon("ActionPackMgr:\n");
		MsgCon(" Used: %d (Peak: %d)\n", numUsed, m_peakActionPackCount);
		MsgCon(" Mutexes: %d (Peak: %d)\n", numUsedMutexes, m_peakMutexCount);
		MsgCon(" Registered: %d\n", numRegistered);
		for (U32F i = 0; i < ActionPack::kActionPackCount; ++i)
		{
			MsgCon("  %s : %d\n", ActionPack::GetTypeName(ActionPack::Type(i)), counts[i]);
		}
		MsgCon("  bad: %d\n", counts[ActionPack::kActionPackCount]);

		MsgCon(" Pending Reg: %d\n", numPendingReg);
		MsgCon(" Pending Unreg: %d\n", numPendingUnreg);
		MsgCon(" Update Time: %0.3fms\n", durationMs);
	}

	if (FALSE_IN_FINAL_BUILD(filter.m_drawApRegFailure))
	{
		PROFILE(AI, ActionPackMgr_Update_DrawRegFail);

		ActionPackBits failedRegs;
		ActionPackBits::BitwiseAndComp(&failedRegs, m_usedMap, m_regMap);
		ActionPackBits::Iterator bitIter(failedRegs);
		for (U32F iSlotIndex = bitIter.First(); iSlotIndex < kMaxActionPackCount; iSlotIndex = bitIter.Advance())
		{
			ActionPack* pAp = m_pActionPack[iSlotIndex];
			if (!pAp)
				continue;

			const U32F typeMask = 1ULL << pAp->GetType();

			if (pAp->IsCorrupted())
			{
				NAV_ASSERTF(false, ("ActionPack in slot %d has been corrupted (memory stomp?)", iSlotIndex));
			}
			else if ((typeMask & g_navMeshDrawFilter.m_apRegFailDrawMask) != 0)
			{
				pAp->DebugDrawRegistrationFailure();
			}
		}

		const bool drawCovers = (g_navMeshDrawFilter.m_apRegFailDrawMask & (1ULL << ActionPack::kCoverActionPack)) != 0;
		const bool drawPerch = (g_navMeshDrawFilter.m_apRegFailDrawMask & (1ULL << ActionPack::kPerchActionPack)) != 0;

		const LevelArray& allLevels = EngineComponents::GetLevelMgr()->GetLevels();
		for (const Level* pLevel : allLevels)
		{
			if (!pLevel)
				continue;

			if (drawCovers)
			{
				pLevel->DrawCoverRegistrationFailures();
			}
			if (drawPerch)
			{
				pLevel->DrawPerchRegistrationFailures();
			}
		}
	}

	if (FALSE_IN_FINAL_BUILD(filter.m_drawApRegDistribution))
	{
		ActionPackBits::Iterator bitIter(m_usedMap);

		I32 refCountNumLevels = 0;
		static const I32 kMaxNumRefCountLevels = 32;
		LevelApRefCount refCountLevels[kMaxNumRefCountLevels];

		I32 refCountNumProcesses = 0;
		static const I32 kMaxNumRefCountProcesses = 32;
		ProcessApRefCount refCountProcesses[kMaxNumRefCountProcesses];

		for (U32F iSlotIndex = bitIter.First(); iSlotIndex < kMaxActionPackCount; iSlotIndex = bitIter.Advance())
		{
			ActionPack* pAp = m_pActionPack[iSlotIndex];
			if (!pAp)
				continue;

			if (const Process* pProcess = pAp->m_hOwnerProcess.ToProcess())
			{
				bool foundProcess = false;
				for (U32 i = 0; i < kMaxNumRefCountProcesses; ++i)
				{
					if (refCountProcesses[i].m_pProcess == pProcess)
					{
						refCountProcesses[i].m_refCount++;
						foundProcess = true;
						break;
					}
				}

				ALWAYS_ASSERT(refCountNumProcesses < kMaxNumRefCountProcesses);

				if (!foundProcess)
				{
					refCountProcesses[refCountNumProcesses].m_pProcess = pProcess;
					refCountProcesses[refCountNumProcesses].m_refCount = 1;
					refCountNumProcesses++;
				}
			}
			else
			{
				bool foundLevel = false;
				for (U32 i = 0; i < kMaxNumRefCountLevels; ++i)
				{
					if (refCountLevels[i].m_pLevel == pAp->GetAllocLevel())
					{
						refCountLevels[i].m_refCount++;
						foundLevel = true;
						break;
					}
				}

				ALWAYS_ASSERT(refCountNumLevels < kMaxNumRefCountLevels);

				if (!foundLevel)
				{
					refCountLevels[refCountNumLevels].m_pLevel = pAp->GetAllocLevel();
					refCountLevels[refCountNumLevels].m_refCount = 1;
					refCountNumLevels++;
				}
			}
		}

		QuickSort(refCountLevels, refCountNumLevels, LevelApRefCount::Compare);
		QuickSort(refCountProcesses, refCountNumProcesses, ProcessApRefCount::Compare);

		MsgCon("Num Levels With Aps: %d\n", refCountNumLevels);
		I32 totalRefsFromLevels = 0;
		for (U32 i = 0; i < refCountNumLevels; ++i)
		{
			MsgCon("  %s: %d\n", refCountLevels[i].m_pLevel->GetName(), refCountLevels[i].m_refCount);

			totalRefsFromLevels += refCountLevels[i].m_refCount;
		}
		MsgCon("  Total From Levels: %d\n", totalRefsFromLevels);

		MsgCon("Num Processes With Aps: %d\n", refCountNumProcesses);
		I32 totalRefsFromProcesses = 0;
		for (U32 i = 0; i < refCountNumProcesses; ++i)
		{
			MsgCon("  %s: %d\n", refCountProcesses[i].m_pProcess->GetName(), refCountProcesses[i].m_refCount);

			totalRefsFromProcesses += refCountProcesses[i].m_refCount;
		}
		MsgCon("  Total From Processes: %d\n", totalRefsFromProcesses);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::LoginActionPack(ActionPack* pActionPack)
{
	AtomicLockJanitorWrite_Jls writeLock(GetLock(), FILE_LINE_FUNC);

	NAV_ASSERT(!pActionPack->IsCorrupted());
	NAV_ASSERT(pActionPack->GetMgrId() == ActionPackMgr::kInvalidMgrId);
	U32F iSlotIndex = m_usedMap.FindFirstClearBit();
	if (iSlotIndex < kMaxActionPackCount)
	{
		U32F iUniqueId = m_idGen++;
		m_usedMap.SetBit(iSlotIndex);
		m_regMap.ClearBit(iSlotIndex);
		m_updateMap.ClearBit(iSlotIndex);
		U32F iMgrId = MakeMgrId(iSlotIndex, iUniqueId);
		m_pActionPack[iSlotIndex] = pActionPack;
		m_apType[iSlotIndex] = pActionPack->GetType();
		pActionPack->SetMgrId(iMgrId);
		++m_actionPackCount;
		m_peakActionPackCount = Max(m_peakActionPackCount, m_actionPackCount);

		if (m_fnLoginObserver)
			m_fnLoginObserver(pActionPack);
	}
	else
	{
		U32F actionPackCountByType[ActionPack::kActionPackCount];
		U32F badTypeCount = 0;
		U32F unknownTypeCount = 0;
		for (I32F iType = 0; iType < ActionPack::kActionPackCount; ++iType)
		{
			actionPackCountByType[iType] = 0;
		}
		for (iSlotIndex = 0; iSlotIndex < kMaxActionPackCount; ++iSlotIndex)
		{
			if (m_usedMap.IsBitSet(iSlotIndex))
			{
				if (ActionPack* pAp = m_pActionPack[iSlotIndex])
				{
					if (pAp->IsCorrupted())
					{
						badTypeCount++;
					}
					else
					{
						const U32F apType = pAp->GetType();
						if (apType < ActionPack::kActionPackCount)
						{
							actionPackCountByType[apType]++;
						}
						else
						{
							unknownTypeCount++;
						}
					}
				}
				else
				{
					badTypeCount++;
				}
			}
		}

		StringBuilder<1024> counts;

		for (int i = 0; i < ActionPack::kActionPackCount; ++i)
		{
			counts.append_format(", %s %d", ActionPack::GetShortTypeName(ActionPack::Type(i)), actionPackCountByType[i]);
		}

		NAV_ASSERTF(false,
					("ActionPackMgr: ran out of slots, increase kMaxActionPackCount (or we have a leak), alloc-count %d, reg-count %d%s, unk %d, bad %d",
					 (int)m_usedMap.CountSetBits(),
					 (int)m_regMap.CountSetBits(),
					 counts.c_str(),
					 unknownTypeCount,
					 badTypeCount));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::SetUpdatesEnabled(ActionPack* pAp, bool enabled)
{
	const U32F iMgrId = pAp->GetMgrId();
	NAV_ASSERT(iMgrId != kInvalidMgrId);
	const U32F iSlotIndex = GetSlotIndex(iMgrId);
	NAV_ASSERT(iSlotIndex < kMaxActionPackCount);
	if (iSlotIndex < kMaxActionPackCount)
	{
		if (enabled)
		{
			m_updateMap.SetBit(iSlotIndex);
		}
		else
		{
			m_updateMap.ClearBit(iSlotIndex);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::RelocateActionPack(ActionPack* pAp, ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	if (!pAp)
		return;

	AtomicLockJanitorWrite_Jls writeLock(GetLock(), FILE_LINE_FUNC);

	const U32F iMgrId = pAp->GetMgrId();
	if (iMgrId == kInvalidMgrId)
		return;

	const U32F iSlotIndex = GetSlotIndex(iMgrId);
	NAV_ASSERT(iSlotIndex < kMaxActionPackCount);
	if (iSlotIndex >= kMaxActionPackCount)
		return;

	NAV_ASSERT(m_usedMap.IsBitSet(iSlotIndex));  // should be logged in
	//NAV_ASSERT(m_regMap.IsBitSet(iSlotIndex));   // should be registered
	NAV_ASSERT(m_pActionPack[iSlotIndex] == pAp);

	RelocatePointer(m_pActionPack[iSlotIndex], deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::LogoutActionPack(ActionPack* pAp)
{
	AtomicLockJanitorWrite_Jls writeLock(GetLock(), FILE_LINE_FUNC);

	if (m_fnLogoutObserver)
		m_fnLogoutObserver(pAp);

	const U32F iMgrId = pAp->GetMgrId();
	NAV_ASSERT(iMgrId != kInvalidMgrId);
	const U32F iSlotIndex = GetSlotIndex(iMgrId);

	NAV_ASSERT(iSlotIndex < kMaxActionPackCount);
	if (iSlotIndex < kMaxActionPackCount)
	{
		//NAV_ASSERTF(!m_regMap.IsBitSet(iSlotIndex), ("Logging out still-registered AP %s '%s'", pAp->GetTypeName(), pAp->GetName()));
		NAV_ASSERT(m_usedMap.IsBitSet(iSlotIndex));

		if (m_usedMap.IsBitSet(iSlotIndex))
		{
			if (m_regMap.IsBitSet(iSlotIndex))
			{
				Unregister(pAp);
			}

			NAV_ASSERT(!m_regMap.IsBitSet(iSlotIndex));
			m_pendingReg.ClearBit(iSlotIndex);
			m_pendingUnreg.ClearBit(iSlotIndex);
			m_usedMap.ClearBit(iSlotIndex);
			m_apsWithOwners.ClearBit(iSlotIndex);
			m_pActionPack[iSlotIndex] = nullptr;
			m_apType[iSlotIndex] = 0xff;
			--m_actionPackCount;
		}
	}
	pAp->SetMgrId(kInvalidMgrId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::LogoutLevel(const Level* pLevel)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForWrite());

	if (!m_pActionPack)
		return;

	for (U32F iSlotIndex = 0; iSlotIndex < kMaxActionPackCount; ++iSlotIndex)
	{
		if (!m_usedMap.IsBitSet(iSlotIndex))
			continue;

		if (ActionPack* pAp = m_pActionPack[iSlotIndex])
		{
			if (pAp->IsCorrupted())
			{
				NAV_ASSERTF(false, ("ActionPack data corruption detected while logging out level '%s'\n", pLevel->GetName()));
			}
			else if (pAp->GetAllocLevel() == pLevel)
			{
				pAp->Logout();
				m_pActionPack[iSlotIndex] = nullptr;
			}
		}
	}

	if (true)
	{
		// There is an infrequent crash that occurs in Nav::FindReachablePolys.  Adding an ASSERT narrowed it to a corrupt TAP (traversal action pack).
		// The problem seems to be that there are a few TAPs that span levels.  If such a TAP is created and registered and linked to the dest level
		// and then the dest level is logged out, the TAP is now pointing to invalid data.  If a Nav::FindReachablePolys search (or perhaps a static
		// path find) is performed it will probably crash on the invalid data.
		// The following code is to remove the pointers to data in the level that is logging out
		const StringId64 levelId = pLevel->GetNameId();
		// for each slot containing a registered action pack,
		ActionPackBits::Iterator bitIter(m_regMap);

		for (U32F iSlotIndex = bitIter.First(); iSlotIndex < kMaxActionPackCount; iSlotIndex = bitIter.Advance())
		{
			// if it is a TAP
			if (m_apType[iSlotIndex] == ActionPack::kTraversalActionPack)
			{
				if (TraversalActionPack* pTap = static_cast<TraversalActionPack*>(m_pActionPack[iSlotIndex]))
				{
					NAV_ASSERT(pTap->GetType() == ActionPack::kTraversalActionPack);
					// if the dest nav mesh exists,
					if (const NavMesh* pDestNavMesh = pTap->GetDestNavLocation().ToNavMesh())
					{
						// if the dest nav mesh is in the level that is logging out,
						if (pDestNavMesh->GetLevelId() == levelId)
						{
							// clear out dest pointers
							pTap->ClearNavDestUnsafe();
						}
					}
#if ENABLE_NAV_LEDGES
					else if (const NavLedgeGraph* pDestLedgeGraph = pTap->GetDestNavLocation().ToNavLedgeGraph())
					{
						// if the dest nav mesh is in the level that is logging out,
						if (pDestLedgeGraph->GetLevelId() == levelId)
						{
							// clear out dest pointers
							pTap->ClearNavDestUnsafe();
						}
					}
#endif // ENABLE_NAV_LEDGES
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::RequestRegistration(ActionPack* pAp)
{
	AtomicLockJanitorWrite_Jls writeLock(GetLock(), FILE_LINE_FUNC);

	const U32F iMgrId = pAp->GetMgrId();
	NAV_ASSERT(iMgrId != kInvalidMgrId);
	const U32F iSlotIndex = GetSlotIndex(iMgrId);
	NAV_ASSERT(iSlotIndex < kMaxActionPackCount);
	if (iSlotIndex < kMaxActionPackCount)
	{
		NAV_ASSERT(m_usedMap.IsBitSet(iSlotIndex));  // should be logged in

		if (m_regMap.IsBitSet(iSlotIndex))
		{
			m_pendingUnreg.SetBit(iSlotIndex);
		}
		else
		{
			m_pendingUnreg.ClearBit(iSlotIndex);
		}

		m_pendingReg.SetBit(iSlotIndex);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::RequestUnregistration(ActionPack* pAp)
{
	AtomicLockJanitorWrite_Jls writeLock(GetLock(), FILE_LINE_FUNC);

	const U32F iMgrId = pAp->GetMgrId();
	NAV_ASSERT(iMgrId != kInvalidMgrId);
	const U32F iSlotIndex = GetSlotIndex(iMgrId);
	NAV_ASSERT(iSlotIndex < kMaxActionPackCount);
	if (iSlotIndex < kMaxActionPackCount)
	{
		NAV_ASSERT(m_usedMap.IsBitSet(iSlotIndex));  // should be logged in
		NAV_ASSERT(m_regMap.IsBitSet(iSlotIndex) || m_pendingReg.IsBitSet(iSlotIndex));

		m_pendingReg.ClearBit(iSlotIndex);

		if (m_regMap.IsBitSet(iSlotIndex))
		{
			m_pendingUnreg.SetBit(iSlotIndex);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::RequestUpdateTraversalActionPackLinkages()
{
	AtomicLockJanitorWrite_Jls writeLock(GetLock(), FILE_LINE_FUNC);

	m_dirtyTapLinkages = m_usedMap;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackMgr::IsRegisteredOrPending(U32F iMgrId) const
{
	AtomicLockJanitorWrite_Jls readLock(GetLock(), FILE_LINE_FUNC);

	bool valid = false;
	const U32F iSlotIndex = GetSlotIndex(iMgrId);

	if (iSlotIndex < kMaxActionPackCount)
	{
		if (m_regMap.IsBitSet(iSlotIndex))
		{
			valid = true;
		}
		else if (m_pendingReg.IsBitSet(iSlotIndex))
		{
			valid = true;
		}
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackMgr::IsPendingUnregistration(U32F iMgrId) const
{
	AtomicLockJanitorWrite_Jls readLock(GetLock(), FILE_LINE_FUNC);

	bool valid = false;
	const U32F iSlotIndex = GetSlotIndex(iMgrId);

	if (iSlotIndex < kMaxActionPackCount)
	{
		if (m_pendingUnreg.IsBitSet(iSlotIndex))
		{
			valid = true;
		}
	}

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackMgr::HasPendingRegistration() const
{
	AtomicLockJanitorWrite_Jls readLock(GetLock(), FILE_LINE_FUNC);

	return !m_pendingReg.AreAllBitsClear();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackMgr::HasPendingRegistration(const Level* pLevel) const
{
	AtomicLockJanitorWrite_Jls readLock(GetLock(), FILE_LINE_FUNC);

	for (U32F iSlotIndex : m_pendingReg)
	{
		ActionPack* pAp = m_pActionPack[iSlotIndex];

		if (pAp && pAp->GetRegistrationLevel() == pLevel)
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackMgr::ProcessPendingRegistration(const Level* pLevel)
{
	if (!HasPendingRegistration(pLevel))
	{
		return true;
	}

	NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();
	NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

	AtomicLockJanitorWrite_Jls apWriteLock(GetLock(), FILE_LINE_FUNC);

	PROFILE(AI, ActionPackMgr_LevelReg);

	U32F numRegistered = 0;
	ActionPackBits regDone(false);

	for (U32F iSlotIndex : m_pendingReg)
	{
		ActionPack* pAp = m_pActionPack[iSlotIndex];
		NAV_ASSERT(pAp);
		const U32F iMgrId = pAp->GetMgrId();
		NAV_ASSERT(iMgrId != kInvalidMgrId);
		const U32F iMgrSlotIndex = GetSlotIndex(iMgrId);
		NAV_ASSERT(iMgrSlotIndex < kMaxActionPackCount);
		NAV_ASSERT(iSlotIndex == iMgrSlotIndex);

		if (pAp->GetRegistrationLevel() != pLevel)
		{
			continue;
		}

		NAV_ASSERTF(!m_regMap.IsBitSet(iSlotIndex),
					("Slot %d already registered (pending unreg: %s)",
					 iSlotIndex,
					 TrueFalse(m_pendingUnreg.IsBitSet(iSlotIndex))));

		if (pAp->RegisterInternal())
		{
			Register(pAp);
		}

		regDone.SetBit(iSlotIndex);

		++numRegistered;

		if (numRegistered >= g_ndAiOptions.m_numActionPacksLoggedInPerFrame)
		{
			break;
		}
	}

	ActionPackBits::BitwiseAndComp(&m_pendingReg, m_pendingReg, regDone);

	for (U32F iSlotIndex : m_pendingReg)
	{
		ActionPack* pAp = m_pActionPack[iSlotIndex];

		if (pAp && pAp->GetRegistrationLevel() == pLevel)
		{
			return false;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::AdjustGameTime(TimeDelta delta)
{
	for (I32F iSlotIndex = 0; iSlotIndex < kMaxActionPackCount; ++iSlotIndex)
	{
		if (!m_usedMap.IsBitSet(iSlotIndex))
			continue;

		ActionPack* pAp = m_pActionPack[iSlotIndex];
		if (!pAp)
			continue;

		pAp->AdjustGameTime(delta);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::Register(ActionPack* pAp)
{
	NAV_ASSERT(m_managerLock.IsLockedForWrite());

	U32F iMgrId = pAp->GetMgrId();
	NAV_ASSERT(iMgrId != kInvalidMgrId);
	U32F iSlotIndex = GetSlotIndex(iMgrId);
	NAV_ASSERT(iSlotIndex < kMaxActionPackCount);
	if (iSlotIndex < kMaxActionPackCount)
	{
		NAV_ASSERT(m_usedMap.IsBitSet(iSlotIndex));  // should be logged in
		NAV_ASSERT(!m_regMap.IsBitSet(iSlotIndex));  // should not be registered

		m_regMap.SetBit(iSlotIndex);
		m_pendingReg.ClearBit(iSlotIndex);

		if (pAp->NeedsUpdate())
		{
			m_updateMap.SetBit(iSlotIndex);
		}
		else
		{
			m_updateMap.ClearBit(iSlotIndex);
		}

		++m_registeredActionPackCount;

		if (m_fnRegisterObserver)
		{
			m_fnRegisterObserver(pAp);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::Unregister(ActionPack* pAp)
{
	NAV_ASSERT(m_managerLock.IsLockedForWrite());

	if (!g_skipApObserversOnUnregister)
	{
		PROFILE(Level, ApMgrUnregister_Observers);
		for (U32 i = 0; i < kMaxUnregisterObservers; i++)
		{
			if (m_fnUnregisterObserver[i])
			{
				m_fnUnregisterObserver[i](pAp);
			}
		}
	}

	const U32F iMgrId = pAp->GetMgrId();
	NAV_ASSERT(iMgrId != kInvalidMgrId);
	const U32F iSlotIndex = GetSlotIndex(iMgrId);
	NAV_ASSERT(iSlotIndex < kMaxActionPackCount);
	if (iSlotIndex < kMaxActionPackCount)
	{
		NAV_ASSERT(m_usedMap.IsBitSet(iSlotIndex));  // should be logged in
		NAV_ASSERT(m_regMap.IsBitSet(iSlotIndex));   // should be registered

		m_regMap.ClearBit(iSlotIndex);
		m_updateMap.ClearBit(iSlotIndex);
		m_pendingUnreg.ClearBit(iSlotIndex);
		--m_registeredActionPackCount;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPack* ActionPackMgr::LookupLoggedInActionPack(U32F iMgrId)
{
	ActionPack* pAp = nullptr;
	const U32F iSlotIndex = GetSlotIndex(iMgrId);
	if (iSlotIndex < kMaxActionPackCount)
	{
		if (m_usedMap.IsBitSet(iSlotIndex))
		{
			pAp = m_pActionPack[iSlotIndex];
		}
	}
	return pAp;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPack* ActionPackMgr::LookupRegisteredActionPack(U32F iMgrId)
{
	ActionPack* pAp = nullptr;
	const U32F iSlotIndex = GetSlotIndex(iMgrId);
	if (iSlotIndex < kMaxActionPackCount)
	{
		//NAV_ASSERT(m_usedMap.IsBitSet(iSlotIndex));
		if (m_regMap.IsBitSet(iSlotIndex))
		{
			pAp = m_pActionPack[iSlotIndex];
			if (pAp && pAp->GetMgrId() != iMgrId)
			{
				// handle is stale
				pAp = nullptr;
			}
		}
	}
	return pAp;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPack* ActionPackMgr::LookupActionPackByName(StringId64 nameId)
{
	ActionPack* pActionPack = nullptr;

	const EntitySpawner* pSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByBareNameId(nameId);
	if (pSpawner)
	{
		pActionPack = GetActionPackFromSpawner(pSpawner);
	}
	else
	{
		CatmullRom* pSpline = g_splineManager.FindByName(nameId, CatmullRomManager::kIncludeDisabled);
		if (pSpline)
		{
			pActionPack = static_cast<ActionPack*>(pSpline->m_pUserData);
		}
	}

	return pActionPack;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPackMutex* ActionPackMgr::LookupActionPackMutexByName(StringId64 nameId)
{
	ActionPackMutex* pMyMutex = nullptr;
	// search for existing mutex
	for (U32F iMutex = 0; iMutex < m_maxActionPackMutexCount; ++iMutex)
	{
		ActionPackMutex* pMutex = &m_mutexList[iMutex];
		if (pMutex->GetNameId() == nameId)
		{
			pMyMutex = pMutex;
			break;
		}
	}
	// if not found,
	if (pMyMutex == nullptr)
	{
		// allocate a new one
		for (U32F iMutex = 0; iMutex < m_maxActionPackMutexCount; ++iMutex)
		{
			ActionPackMutex* pMutex = &m_mutexList[iMutex];
			if (!pMutex->IsValid())
			{
				pMyMutex = pMutex;
				pMyMutex->Init(nameId);
				break;
			}
		}
	}

	if (pMyMutex == nullptr)
	{
		static bool messagePrinted = false;
		if (!messagePrinted)
		{
			MsgConErr("ActionPackMgr: Out of ActionPack mutexes - Increase m_maxActionPackMutexCount.\n");
			messagePrinted = true;
		}
	}

	return pMyMutex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::FlagProcessOwnership(ActionPack* pAp)
{
	AtomicLockJanitorWrite_Jls writeLock(GetLock(), FILE_LINE_FUNC);

	const U32F iMgrId = pAp->GetMgrId();
	NAV_ASSERT(iMgrId != kInvalidMgrId);
	const U32F iSlotIndex = GetSlotIndex(iMgrId);
	NAV_ASSERT(iSlotIndex < kMaxActionPackCount);
	if (iSlotIndex < kMaxActionPackCount)
	{
		m_apsWithOwners.SetBit(iSlotIndex);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::ClearProcessOwnership(ActionPack* pAp)
{
	AtomicLockJanitorWrite_Jls writeLock(GetLock(), FILE_LINE_FUNC);

	const U32F iMgrId = pAp->GetMgrId();
	NAV_ASSERT(iMgrId != kInvalidMgrId);
	const U32F iSlotIndex = GetSlotIndex(iMgrId);
	NAV_ASSERT(iSlotIndex < kMaxActionPackCount);
	if (iSlotIndex < kMaxActionPackCount)
	{
		m_apsWithOwners.ClearBit(iSlotIndex);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ActionPackMgr::GetListOfOwnedActionPacks(const Process* pOwnerProcess,
											  ActionPack** __restrict ppOutList,
											  U32F maxOutCount) const
{
	AtomicLockJanitorRead_Jls readLock(GetLock(), FILE_LINE_FUNC);

	ProcessHandle hOwner = ProcessHandle(pOwnerProcess);
	U32F numOut = 0;

	ActionPackBits::Iterator bitIter(m_usedMap);
	for (U32F iSlotIndex = bitIter.First(); iSlotIndex < kMaxActionPackCount; iSlotIndex = bitIter.Advance())
	{
		ActionPack* pAp = m_pActionPack[iSlotIndex];
		if (!pAp)
			continue;

		const ProcessHandle hApOwner = pAp->GetOwnerProcessHandle();
		if (hApOwner != hOwner)
			continue;

		if (numOut >= maxOutCount)
			break;

		ppOutList[numOut++] = pAp;
	}

	return numOut;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::InvalidateAllActionPackHandles()
{
	AtomicLockJanitorWrite_Jls writeLock(GetLock(), FILE_LINE_FUNC);

	for (U32F iSlotIndex = 0; iSlotIndex < kMaxActionPackCount; ++iSlotIndex)
	{
		if (ActionPack* pAp = m_pActionPack[iSlotIndex])
		{
			U32F iUniqueId = m_idGen++;
			pAp->SetMgrId(MakeMgrId(iSlotIndex, iUniqueId));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::ResetAll()
{
	AtomicLockJanitorWrite_Jls writeLock(GetLock(), FILE_LINE_FUNC);

	ActionPackBits::Iterator bitIter(m_regMap);

	for (U32F iSlotIndex = bitIter.First(); iSlotIndex < kMaxActionPackCount; iSlotIndex = bitIter.Advance())
	{
		if (ActionPack* pAp = m_pActionPack[iSlotIndex])
		{
			pAp->Reset();
		}
	}

	for (U32F iMutex = 0; iMutex < m_maxActionPackMutexCount; ++iMutex)
	{
		ActionPackMutex* pMutex = &m_mutexList[iMutex];

		pMutex->Reset();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::RegisterRegisterObserver(PFnNotifyObserver fn)
{
	NAV_ASSERT(!m_fnRegisterObserver);
	m_fnRegisterObserver = fn;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::RegisterUnregisterObserver(PFnNotifyObserver fn)
{
	for (U32 i = 0; i < kMaxUnregisterObservers; i++)
	{
		if (!m_fnUnregisterObserver[i])
		{
			m_fnUnregisterObserver[i] = fn;
			return;
		}
	}
	NAV_ASSERTF(false, ("Trying to register too many APMgr unregister observers"));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::RegisterLoginObserver(PFnNotifyObserver fn)
{
	NAV_ASSERT(!m_fnLoginObserver);
	m_fnLoginObserver = fn;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::RegisterLogoutObserver(PFnNotifyObserver fn)
{
	NAV_ASSERT(!m_fnLogoutObserver);
	m_fnLogoutObserver = fn;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::UpdateTraversalActionPackLinkages()
{
	PROFILE(AI, ApMgr_UpdateTapLinks);
	PROFILE_ACCUM(UpdateTraversalActionPackLinkages);

	static CONST_EXPR U32F kNumCleanTapsPerFrame = 5;

	ActionPackBits cleanedTaps(false);
	U32F numCleaned = 0;

	for (U32F iSlotIndex : m_dirtyTapLinkages)
	{
		cleanedTaps.SetBit(iSlotIndex);

		if (m_apType[iSlotIndex] != ActionPack::kTraversalActionPack)
			continue;

		ActionPack* pAp = m_pActionPack[iSlotIndex];
		if (!pAp)
			continue;

		NAV_ASSERT(pAp->GetType() == ActionPack::kTraversalActionPack);
		TraversalActionPack* pTap = static_cast<TraversalActionPack*>(pAp);

		if (pAp->IsRegistered())
		{
			pTap->UpdateNavDestUnsafe();
		}
		else if (!m_pendingReg.IsBitSet(iSlotIndex))
		{
			const bool hasParentSpawner = pAp->GetParentSpawnerId() != INVALID_STRING_ID_64;
			const bool hasOwnerProcess = pAp->GetOwnerProcessHandle().ToProcess() != nullptr;

			if (hasParentSpawner == hasOwnerProcess)
			{
				if (pAp->RegisterInternal())
				{
					Register(pAp);
				}
			}
		}

		if (pTap->IsRegistered())
		{
			pTap->SearchForBlockingRigidBodies();
		}

		++numCleaned;

		if (numCleaned >= kNumCleanTapsPerFrame)
			break;
	}

	ActionPackBits::BitwiseAndComp(&m_dirtyTapLinkages, m_dirtyTapLinkages, cleanedTaps);

	if (FALSE_IN_FINAL_BUILD(g_navMeshDrawFilter.m_displayApManager))
	{
		MsgCon("Updated %d tap linkages (%d remain)\n", numCleaned, m_dirtyTapLinkages.CountSetBits());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ActionPackMgr::FindActionPacksBySpawnerId(ActionPack** __restrict ppOutList, U32F maxOutCount, StringId64 spawnerId) const
{
	PROFILE(AI, ApMgr_FindApsBySpawnerId);

	U32F outCount = 0;

	for (U32F iSlotIndex = 0; iSlotIndex < kMaxActionPackCount; ++iSlotIndex)
	{
		if (ActionPack* pAp = m_pActionPack[iSlotIndex])
		{
			if (pAp->GetSpawnerId() == spawnerId)
			{
				if (outCount >= maxOutCount)
				{
					break;
				}

				ppOutList[outCount++] = pAp;
			}
		}
	}

	return outCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ActionPackMgr::FindActionPacksByType(ActionPack** __restrict ppOutList, U32F maxOutCount, I32F apType) const
{
	PROFILE(AI, ApMgr_FindApsByType);
	U32F outCount = 0;
	ActionPackBits::Iterator bitIter(m_regMap);

	for (U32F iSlotIndex = bitIter.First(); iSlotIndex < kMaxActionPackCount; iSlotIndex = bitIter.Advance())
	{
		if (m_apType[iSlotIndex] == apType)
		{
			if (ActionPack* pAp = m_pActionPack[iSlotIndex])
			{
				NAV_ASSERT(pAp->GetType() == apType);

				if (!pAp->IsRegistered())
				{
					continue; // hidden from searches if not registered
				}

				if (outCount >= maxOutCount)
				{
					break;
				}

				ppOutList[outCount++] = pAp;
			}
		}
	}
	return outCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// NOTE: does not handle parent spacing!
// NOTE: will not return nullptr APs, but may return APs that do not have a valid registered NavLocation
U32 ActionPackMgr::FindActionPacksByTypeInRadiusFast(ActionPack** __restrict ppOutList,
												     U32 maxOutCount,
												     I32 apType,
												     const Point searchOriginWs,
												     float searchRadius) const
{
	PROFILE(AI, ApMgr_FindApsByTypeInRadiusFast);

	if (maxOutCount == 0)
		return 0;

	AtomicLockJanitorRead_Jls lock(GetLock(), FILE_LINE_FUNC);
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	const U8* __restrict const pApType = m_apType;
	ActionPack* const* __restrict const pActionPack = m_pActionPack;

	const float searchRadiusSqr = searchRadius * searchRadius;
	const __m128 c = searchOriginWs.QuadwordValue();
	U32 outCount = 0;

	for (const U64 i : m_regMap)
	{
		if (pApType[i] == (U8)apType)
		{
			ActionPack* pAp = pActionPack[i];
			//NAV_ASSERT(pAp && pAp->GetType() == apType);

			const __m128 d = _mm_sub_ps(c, pAp->GetRegisteredNavLocationPosPs().QuadwordValue());
			const __m128 dSqr = _mm_mul_ps(d, d);
			const __m128 dSqrXY = _mm_hadd_ps(dSqr, dSqr);
			const __m128 dSqrZ = _mm_shuffle_ps(dSqr, dSqr, 238);
			const __m128 dSqrXYZ = _mm_add_ss(dSqrXY, dSqrZ);

			if (LIKELY(_mm_cvtss_f32(dSqrXYZ) <= searchRadiusSqr))
			{
				ppOutList[outCount++] = pAp;
				if (UNLIKELY(outCount >= maxOutCount))
					return outCount;
			}
		}
	}

	return outCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ActionPackMgr::FindActionPacksByTypeInRadius(ActionPack** __restrict ppOutList,
												  U32F maxOutCount,
												  I32F apType,
												  Point_arg searchOriginWs,
												  float searchRadius) const
{
	PROFILE(AI, ApMgr_FindApsByTypeInRadius);

	AtomicLockJanitorRead_Jls lock(GetLock(), FILE_LINE_FUNC);

	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	U32F outCount = 0;
	const F32 searchRadiusSqr = Sqr(searchRadius);

	ActionPackBits::Iterator bitIter(m_regMap);

	for (U32F iSlotIndex = bitIter.First(); iSlotIndex < kMaxActionPackCount; iSlotIndex = bitIter.Advance())
	{
		if (m_apType[iSlotIndex] != apType)
			continue;

		ActionPack* pAp = m_pActionPack[iSlotIndex];

		if (!pAp)
			continue;

		if (outCount >= maxOutCount)
			break;

		NAV_ASSERT(pAp->GetType() == apType);

		if (!pAp->GetRegisteredNavLocation().IsValid())
		{
			continue; // if not on nav mesh or graph, it is hidden from searches
		}

		const Point apPosWs = pAp->GetRegistrationPointWs();
		const float dist = DistSqr(apPosWs, searchOriginWs);

		if (dist <= searchRadiusSqr)
		{
			ppOutList[outCount++] = pAp;
		}
	}
	return outCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ActionPackMgr::FindActionPacksByTypeMaskInRadius(ActionPack** __restrict ppOutList,
													  U32F maxOutCount,
													  U32F typeMask,
													  Point_arg searchOriginWs,
													  float searchRadius,
													  float entryOffset) const
{
	PROFILE_AUTO(AI);

	AtomicLockJanitorRead_Jls lock(GetLock(), FILE_LINE_FUNC);

	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	U32F outCount = 0;

	for (U32F iSlotIndex : m_regMap)
	{
		const U32F apTypeMask = 1ULL << m_apType[iSlotIndex];

		if ((apTypeMask & typeMask) == 0)
			continue;

		ActionPack* pAp = m_pActionPack[iSlotIndex];

		if (!pAp)
			continue;

		if (outCount >= maxOutCount)
			break;

		if (pAp->GetRegisteredNavLocation().IsNull())
		{
			continue; // if not on nav mesh or graph, it is hidden from searches
		}

		const float dist = pAp->DistToPointWs(searchOriginWs, entryOffset);

		if (dist <= searchRadius)
		{
			ppOutList[outCount++] = pAp;
		}
	}

	return outCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ActionPackMgr::FindActionPacksByTypeOnSegment(ActionPack** __restrict ppOutList,
												   U32F maxOutCount,
												   I32F apType,
												   const Segment& searchSegmentWs,
												   float searchRadius) const
{
	PROFILE(AI, ApMgr_FindApsByTypeOnSegment);

	AtomicLockJanitorRead_Jls lock(GetLock(), FILE_LINE_FUNC);

	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLocked());

	U32F outCount = 0;
	const F32 searchRadiusSqr = Sqr(searchRadius);

	ActionPackBits::Iterator bitIter(m_regMap);

	for (U32F iSlotIndex = bitIter.First(); iSlotIndex < kMaxActionPackCount; iSlotIndex = bitIter.Advance())
	{
		if (m_apType[iSlotIndex] != apType)
			continue;

		ActionPack* pAp = m_pActionPack[iSlotIndex];

		if (!pAp)
			continue;

		if (outCount >= maxOutCount)
			break;

		NAV_ASSERT(pAp->GetType() == apType);

		if (!pAp->GetRegisteredNavLocation().IsValid())
		{
			continue; // if not on nav mesh or graph, it is hidden from searches
		}

		const Point apPosWs = pAp->GetRegistrationPointWs();
		const float dist = DistPointSegment(apPosWs, searchSegmentWs);

		if (dist <= searchRadiusSqr)
		{
			ppOutList[outCount++] = pAp;
		}
	}
	return outCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::FindClosestActionPackByType(ActionPack** __restrict pOut, I32F apType, Point_arg pos, Process* pChar) const
{
	PROFILE(AI, ApMgr_FindClosestApByType);

	AtomicLockJanitorRead_Jls lock(GetLock(), FILE_LINE_FUNC);

	float closestDist = NDI_FLT_MAX;
	*pOut = nullptr;
	ActionPackBits::Iterator bitIter(m_regMap);

	for (U32F iSlotIndex = bitIter.First(); iSlotIndex < kMaxActionPackCount; iSlotIndex = bitIter.Advance())
	{
		if (m_apType[iSlotIndex] == apType)
		{
			if (ActionPack* pAp = m_pActionPack[iSlotIndex])
			{
				NAV_ASSERT(pAp->GetType() == apType);

				if (!pAp->IsRegistered())
				{
					continue; // hidden from searches if not registered
				}

				if (!pAp->IsReservedBy(pChar))
				{
					if (!pAp->IsAvailable())
						continue;

					if (pAp->IsReserved())
						continue;
				}

				const Point apPos = pAp->GetRegistrationPointWs();
				const float dist = Dist(apPos, pos);
				if (dist < closestDist)
				{
					closestDist = dist;
					*pOut = pAp;
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ActionPackMgr::FindActionPacksByTypeInRegion(ActionPack** __restrict ppOutList,
												  U32F maxOutCount,
												  I32F apType,
												  const Region* pRegion) const
{
	PROFILE(AI, ApMgr_FindApsByTypeInRadius);

	if (!pRegion)
		return 0;

	AtomicLockJanitorRead_Jls lock(GetLock(), FILE_LINE_FUNC);

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	U32F outCount = 0;

	ActionPackBits::Iterator bitIter(m_regMap);

	for (U32F iSlotIndex = bitIter.First(); iSlotIndex < kMaxActionPackCount; iSlotIndex = bitIter.Advance())
	{
		if (m_apType[iSlotIndex] != apType)
			continue;

		ActionPack* pAp = m_pActionPack[iSlotIndex];

		if (!pAp)
			continue;

		if (outCount >= maxOutCount)
			break;

		NAV_ASSERT(pAp->GetType() == apType);

		if (!pAp->GetRegisteredNavLocation().IsValid())
		{
			continue; // if not on nav mesh or graph, it is hidden from searches
		}

		const Point apPosWs = pAp->GetRegistrationPointWs();
		if (pRegion->IsInside(apPosWs, 0.1f))
		{
			ppOutList[outCount++] = pAp;
		}
	}
	return outCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ActionPackMgr::FindActionPacksByTypeInRegion(ActionPack** __restrict ppOutList,
												  U32F maxOutCount,
												  I32F apType,
												  StringId64 regionId) const
{
	const Region* pRegion = EngineComponents::GetLevelMgr()->GetRegionByName(regionId);
	return FindActionPacksByTypeInRegion(ppOutList, maxOutCount, apType, pRegion);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::ForAllActionPacks(VisitActionPack callback, uintptr_t data, I32F slotStride /* = -1 */) const
{
	AtomicLockJanitorRead_Jls lock(GetLock(), FILE_LINE_FUNC);

	ActionPackBits::Iterator bitIter(m_regMap);
	for (U32F iSlotIndex = bitIter.First(); iSlotIndex < kMaxActionPackCount; iSlotIndex = bitIter.Advance())
	{
		if ((slotStride != -1) && ((iSlotIndex % 4) != slotStride))
			continue;

		ActionPack* pAp = m_pActionPack[iSlotIndex];

		if (!pAp)
			continue;

		if (!callback(m_apType[iSlotIndex], m_pActionPack[iSlotIndex], data))
		{
			return;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ActionPackMgr::GetRegisteredActionPackCount() const
{
	return m_regMap.CountSetBits();
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F ActionPackMgr::GetAllocatedActionPackCount() const
{
	return m_usedMap.CountSetBits();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::ReRegisterActionPacksForLevel(const Level* pLevel)
{
	if (!pLevel)
		return;

	AtomicLockJanitorWrite_Jls writeLock(GetLock(), FILE_LINE_FUNC);

	ActionPackBits failedRegs;
	ActionPackBits::BitwiseAndComp(&failedRegs, m_usedMap, m_regMap);
	ActionPackBits::Iterator bitIter(failedRegs);

	for (U32F iSlotIndex = bitIter.First(); iSlotIndex < kMaxActionPackCount; iSlotIndex = bitIter.Advance())
	{
		ActionPack* pAp = m_pActionPack[iSlotIndex];
		if (!pAp)
			continue;

		if (pAp->GetAllocLevel() != pLevel)
			continue;

		m_pendingReg.SetBit(iSlotIndex);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::DebugCheckForCorruption()
{
	STRIP_IN_FINAL_BUILD;

	for (U32F iSlotIndex = 0; iSlotIndex < kMaxActionPackCount; ++iSlotIndex)
	{
		if (!m_usedMap.IsBitSet(iSlotIndex))
			continue;

		if (ActionPack* pAp = m_pActionPack[iSlotIndex])
		{
			NAV_ASSERTF(!pAp->IsCorrupted(), ("ActionPack in slot %d is corrupted", int(iSlotIndex)));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ActionPackMgr::GetTypeCounts(U32F* pCountArray) const
{
	if (!pCountArray)
		return;

	for (I32F iType = 0; iType < ActionPack::kActionPackCount + 1; ++iType)
	{
		pCountArray[iType] = 0;
	}

	for (U32F iSlotIndex = 0; iSlotIndex < kMaxActionPackCount; ++iSlotIndex)
	{
		if (m_usedMap.IsBitSet(iSlotIndex))
		{
			if (ActionPack* pAp = m_pActionPack[iSlotIndex])
			{
				if (pAp->IsCorrupted())
				{
					pCountArray[ActionPack::kActionPackCount]++;
				}
				else
				{
					const U32F apType = pAp->GetType();
					if (apType < ActionPack::kActionPackCount)
					{
						pCountArray[apType]++;
					}
					else
					{
						pCountArray[ActionPack::kActionPackCount]++;
					}
				}
			}
			else
			{
				pCountArray[ActionPack::kActionPackCount]++;
			}
		}
	}
}
