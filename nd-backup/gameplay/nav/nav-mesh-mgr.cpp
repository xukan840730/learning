/*
 * Copyright (c) 2003 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-mesh-mgr.h"

#include "corelib/containers/hashtable.h"
#include "corelib/system/atomic-lock-priv.h"

#include "ndlib/memory/allocator-levelmem.h"
#include "ndlib/profiling/profiling.h"

#include "gamelib/gameplay/nav/action-pack-mgr.h"
#include "gamelib/gameplay/nav/leap-action-pack.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-defines.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/level/level.h"

/// --------------------------------------------------------------------------------------------------------------- ///
NavMeshGlobalLock g_navMeshGlobalLock;

/// --------------------------------------------------------------------------------------------------------------- ///
static inline const NavManagerId MakeNavMeshMgrId(U32F iSlotIndex, U32F iUniqueId)
{
	NavManagerId mgrId;
	mgrId.m_navMeshIndex = iSlotIndex;
	mgrId.m_iPoly = 0;
	mgrId.m_uniqueId  = iUniqueId;
	return mgrId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshGlobalLock::Initialize(const char* file, U32F line, const char* func)
{
	m_pLockCounter = ndjob::AllocateCounter(file, line, func, 0, ndjob::kWaitRestrictionNone);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshGlobalLock::Destroy()
{
	if (m_pLockCounter)
	{
		ndjob::WaitForCounterAndFree(m_pLockCounter);
		m_pLockCounter = nullptr;
	}

#if ENABLE_ND_LOCKED_BY
	if (m_pLockedBy)
		m_pLockedBy = m_pLockedBy->Free();
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshGlobalLock::AcquireReadLock(const char* file, U32F line, const char* func)
{
	if (JlsLock_TryReaquireReadLock(JlsFixedIndex::kNavMeshMgrGlobalLock, SID_VAL("NavMeshGlobalLock")))
		return;

	PROFILE_ACCUM(NM_AcquireRead);

	SYSTEM_ASSERTF(ndjob::GetActiveJobId() >= 0, ("NavMeshGlobalLock not supported outside job system"));

	while (true)
	{
		ndjob::WaitForCounter(m_pLockCounter, kWriteRequestSentinel - 1, ndjob::kWaitLessThanOrEqual);

		if (m_pLockCounter->Add_NoNotify(1) < kWriteRequestSentinel)
		{
			break;
		}
		else
		{
			m_pLockCounter->Add(-1);
		}
	}

	JlsLock_RegisterReadOwner(JlsFixedIndex::kNavMeshMgrGlobalLock, SID_VAL("NavMeshGlobalLock"));
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32 NavMeshMgr::FindOverlappingHeightMaps(const Point& obs, U32 maxResults, NavMeshHandle* results) const
{
	U32F navMeshCount = 0;
	for (U32F iNavMesh = 0; iNavMesh < kMaxNavMeshCount && navMeshCount < maxResults; ++iNavMesh)
	{
		const NavMesh* pNavMesh = m_navMeshEntries[iNavMesh].m_pNavMesh;
		if (pNavMesh && pNavMesh->GetHeightMap())
		{
			Aabb heightMeshBb = pNavMesh->GetHeightMap()->GetAabbLs();
			if (heightMeshBb.ContainsPoint(obs))
				results[navMeshCount++] = pNavMesh;
		}
	}

	return navMeshCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshGlobalLock::ReleaseReadLock()
{
	if (JlsLock_ReleaseReadLock(JlsFixedIndex::kNavMeshMgrGlobalLock, SID_VAL("NavMeshGlobalLock")))
		return;

	m_pLockCounter->Add_NoNotify(-1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshGlobalLock::AcquireWriteLock(const char* file, U32F line, const char* func)
{
	if (JlsLock_TryReaquireWriteLock(JlsFixedIndex::kNavMeshMgrGlobalLock, SID_VAL("NavMeshGlobalLock")))
		return;

	PROFILE_ACCUM(NM_AcquireWrite);

	SYSTEM_ASSERTF(ndjob::GetActiveJobId() >= 0, ("NavMeshGlobalLock not supported outside job system"));

	m_pLockCounter->Add_NoNotify(kWriteRequestSentinel);

	while ((m_pLockCounter->Add_NoNotify(kWriteActiveSentinel) % kWriteRequestSentinel) != 0)
	{
		m_pLockCounter->Add_NoNotify(-kWriteActiveSentinel);
	
		EMIT_PAUSE_INSTRUCTION();
	}

	JlsLock_RegisterWriteOwner(JlsFixedIndex::kNavMeshMgrGlobalLock, SID_VAL("NavMeshGlobalLock"));

#if ENABLE_ND_LOCKED_BY
	NAV_ASSERT(!m_pLockedBy);
	m_pLockedBy = NdLockedBy::Alloc(file, line, func, NdLockedBy::kClientJobRwMutex);
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshGlobalLock::ReleaseWriteLock()
{
	if (JlsLock_ReleaseWriteLock(JlsFixedIndex::kNavMeshMgrGlobalLock, SID_VAL("NavMeshGlobalLock")))
		return;

#if ENABLE_ND_LOCKED_BY
	if (m_pLockedBy)
	{
		m_pLockedBy = m_pLockedBy->Free();
	}
#endif

	m_pLockCounter->Add(-(kWriteActiveSentinel + kWriteRequestSentinel));
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshGlobalLock::IsLocked() const
{
	return m_pLockCounter->GetValue() > 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshGlobalLock::IsLockedForRead() const
{
	const I64 lockVal = m_pLockCounter->GetValue();
	return (lockVal % kWriteActiveSentinel) > 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshGlobalLock::IsLockedForWrite() const
{
	return (m_pLockCounter->GetValue() >= kWriteActiveSentinel);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMeshLockJanitor::NavMeshLockJanitor(bool readOnly, const char* file, U32F line, const char* func)
	: m_lock(*EngineComponents::GetNavMeshMgr()->GetGlobalLock()), m_readLock(readOnly)
{
	if (m_readLock)
	{
		m_lock.AcquireReadLock(file, line, func);
	}
	else
	{
		m_lock.AcquireWriteLock(file, line, func);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMeshLockJanitor::~NavMeshLockJanitor()
{
	if (m_readLock)
	{
		m_lock.ReleaseReadLock();
	}
	else
	{
		m_lock.ReleaseWriteLock();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMeshWriteLockJanitor::NavMeshWriteLockJanitor(const char* file, U32F line, const char* func)
	: m_lock(*EngineComponents::GetNavMeshMgr()->GetGlobalLock())
{
	m_lock.AcquireWriteLock(file, line, func);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMeshWriteLockJanitor::~NavMeshWriteLockJanitor()
{
	m_lock.ReleaseWriteLock();
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMeshReadLockJanitor::NavMeshReadLockJanitor(const char* file, U32F line, const char* func)
	: m_lock(*EngineComponents::GetNavMeshMgr()->GetGlobalLock())
{
	//PROFILE_ACCUM(NM_ReadLockJanitor);
	m_lock.AcquireReadLock(file, line, func);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMeshReadLockJanitor::~NavMeshReadLockJanitor()
{
	m_lock.ReleaseReadLock();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void NavError(const char* strMsg, ...)
{
	char strBuffer[512];
	va_list args;
	va_start(args, strMsg);
	vsnprintf(strBuffer, 512, strMsg, args);
	va_end(args);
	MsgErr("Nav: ");
	MsgErr(strBuffer);
	MsgConErr(strBuffer);
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMeshMgr::NavMeshMgr()
: m_accessLock()
{
	memset(m_navMeshEntries, 0, sizeof(m_navMeshEntries));

	m_idCounter = 1;
	m_usedNavMeshes.ClearAllBits();

	for (U32F i = 0; i < kMaxObservers; ++i)
	{
		m_fnLoginObservers[i] = nullptr;
		m_fnRegisterObservers[i] = nullptr;
		m_fnUnregisterObservers[i] = nullptr;
		m_fnLogoutObservers[i] = nullptr;
	}

	m_numLoginObservers = 0;
	m_numRegisterObservers = 0;
	m_numUnregisterObservers = 0;
	m_numLogoutObservers = 0;

	m_pSelectionStorage = nullptr;
	m_signatureWord = kSignatureValue;

	for (U32F i = 0; i < kMaxNavMeshCount; ++i)
	{
		NavMeshEntry& entry = m_navMeshEntries[i];
		entry.m_signatureWord = kSignatureValue;
	}

	m_apLevelRegistrationQueueCount.Set(0);

	for (U32F i = 0; i < kMaxNavMeshCount; ++i)
	{
		m_apLevelRegistrationQueue[i] = QueueEntry();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::AddLoginObserver(PFnLoginObserver fn)
{
	NAV_ASSERTF(m_numLoginObservers < kMaxObservers, ("Increase NavMeshMgr::kMaxObservers."));
	m_fnLoginObservers[m_numLoginObservers++] = fn;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::AddRegisterObserver(PFnNotifyObserver fn)
{
	NAV_ASSERTF(m_numRegisterObservers < kMaxObservers, ("Increase NavMeshMgr::kMaxObservers."));
	m_fnRegisterObservers[m_numRegisterObservers++] = fn;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::AddUnregisterObserver(PFnNotifyObserver fn)
{
	NAV_ASSERTF(m_numUnregisterObservers < kMaxObservers, ("Increase NavMeshMgr::kMaxObservers."));
	m_fnUnregisterObservers[m_numUnregisterObservers++] = fn;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::AddLogoutObserver(PFnNotifyObserver fn)
{
	NAV_ASSERTF(m_numLogoutObservers < kMaxObservers, ("Increase NavMeshMgr::kMaxObservers."));
	m_fnLogoutObservers[m_numLogoutObservers++] = fn;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::OnLogin(NavMesh* pMesh, Level* pLevel)
{
	PROFILE(Navigation, NavMesh_OnLogin);

	for (U32F i = 0; i < m_numLoginObservers; ++i)
	{
		m_fnLoginObservers[i](pMesh, pLevel);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::OnRegister(NavMesh* pMesh)
{
	PROFILE(Navigation, NavMesh_OnRegister);

	for (U32F i = 0; i < m_numRegisterObservers; ++i)
	{
		m_fnRegisterObservers[i](pMesh);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::OnUnregister(NavMesh* pMesh)
{
	PROFILE(Navigation, NavMesh_OnUnegister);

	for (U32F i = 0; i < m_numUnregisterObservers; ++i)
	{
		m_fnUnregisterObservers[i](pMesh);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::OnLogout(NavMesh* pMesh)
{
	PROFILE(Navigation, NavMesh_OnLogout);

	for (U32F i = 0; i < m_numLogoutObservers; ++i)
	{
		m_fnLogoutObservers[i](pMesh);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavMesh* NavMeshMgr::LookupRegisteredNavMesh(NavMeshHandle hMesh) const
{
	NAV_ASSERT(GetGlobalLock()->IsLocked());

	const NavManagerId navId = hMesh.GetManagerId();
	if (!navId.IsValid())
		return nullptr;

	const NavMesh* pMesh = nullptr;
	const U32F iMesh = GetSlotIndex(hMesh.GetManagerId());
	NAV_ASSERT(iMesh < kMaxNavMeshCount);

	if (iMesh < kMaxNavMeshCount)
	{
		const NavMeshEntry& entry = m_navMeshEntries[iMesh];
		NAV_ASSERT((entry.m_pNavMesh == nullptr) || (entry.m_registered == entry.m_pNavMesh->IsRegistered()));

		if ((entry.m_hNavMesh == hMesh) && entry.m_pNavMesh && entry.m_registered)
		{
			NAV_ASSERT(m_usedNavMeshes.IsBitSet(iMesh));

			pMesh = entry.m_pNavMesh;
		}
	}

	return pMesh;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshMgr::IsAnExtraFeatureLevel(StringId64 levelId) const
{
	NAV_ASSERT(GetGlobalLock()->IsLocked());
	RecursiveAtomicLockJanitor64 jj(&m_accessLock, FILE_LINE_FUNC);

	for (U64 iEntry : m_usedNavMeshes)
	{
		const NavMeshEntry& entry = m_navMeshEntries[iEntry];
		const NavMesh* pNavMesh = entry.m_pNavMesh;

		NAV_ASSERT(pNavMesh);

		const EntityDB* pEntDb = pNavMesh->GetEntityDB();
		const U32F numExtraLevels = pEntDb ? pEntDb->GetRecordArraySize(SID("extra-feature-level")) : 0;

		if (numExtraLevels > 0)
		{
			StringId64* aExtraLevels = STACK_ALLOC(StringId64, numExtraLevels);

			pEntDb->GetDataArray(SID("extra-feature-level"), aExtraLevels, numExtraLevels, INVALID_STRING_ID_64);

			for (U32F ii = 0; ii < numExtraLevels; ++ii)
			{
				if (aExtraLevels[ii] == levelId)
					return true;
			}
		}

	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshMgr::AddNavMesh(NavMesh* pMeshToAdd)
{
	RecursiveAtomicLockJanitor64 jj(&m_accessLock, FILE_LINE_FUNC);

	NAV_ASSERT(pMeshToAdd);
	NAV_ASSERT(GetGlobalLock()->IsLockedForWrite());

	bool success = false;

	if (true)
	{
		// check for duplicate nav meshes
		for (U64 iEntry : m_usedNavMeshes)
		{
			const NavMeshEntry& entry = m_navMeshEntries[iEntry];

			if (NavMesh* pMesh = entry.m_pNavMesh)
			{
				NAV_ASSERT(pMesh != pMeshToAdd);
				if (pMesh->GetNameId() == pMeshToAdd->GetNameId() && pMeshToAdd->GetNameId() != INVALID_STRING_ID_64)
				{
					NavError("Two navmeshes are named %s, from level %s and level %s\n",
							 pMeshToAdd->GetName(),
							 DevKitOnly_StringIdToStringOrHex(pMesh->GetLevelId()),
							 DevKitOnly_StringIdToStringOrHex(pMeshToAdd->GetLevelId()));

					pMesh->Unregister();

					return false;
				}
			}
		}
	}

	const U32 iFreeEntry = m_usedNavMeshes.FindFirstClearBit();

	// did we find a free entry?
	if (iFreeEntry < kMaxNavMeshCount)
	{
		const NavManagerId mgrId = MakeNavMeshMgrId(iFreeEntry, m_idCounter);
		NavMeshEntry& entry = m_navMeshEntries[iFreeEntry];

		entry.m_pNavMesh = pMeshToAdd;
		entry.m_bindSpawnerNameId = pMeshToAdd->GetBindSpawnerNameId();
		entry.m_hNavMesh.m_managerId = mgrId;
		pMeshToAdd->m_managerId = mgrId;
		UpdateNavMeshBBox(pMeshToAdd);
		entry.m_registered = false;
		m_idCounter++;

		m_usedNavMeshes.SetBit(iFreeEntry);

		// disallow unique id of 0, this allows kInvalidMgrId to be 0
		if ((m_idCounter & 0xFFFF) == 0)
		{
			m_idCounter++;
		}

		success = true;
	}
	else
	{
		NavError("NavMeshMgr is full (%d entries): Failed to register navmesh (%s, from level %s)\n",
				  (int) kMaxNavMeshCount,
				  pMeshToAdd->GetName(),
				  DevKitOnly_StringIdToStringOrHex(pMeshToAdd->GetLevelId()));
	}


	if (success)
	{
		const StringId64 collisionLevelId = pMeshToAdd->GetCollisionLevelId();

		if (collisionLevelId != INVALID_STRING_ID_64)
		{
			AddLevelToApRegistrationQueue(collisionLevelId);
		}

		const EntityDB* pEntDb = pMeshToAdd->GetEntityDB();
		const U32F numExtraLevels = pEntDb ? pEntDb->GetRecordArraySize(SID("extra-feature-level")) : 0;

		if (numExtraLevels > 0)
		{
			StringId64* aExtraLevels = STACK_ALLOC(StringId64, numExtraLevels);

			pEntDb->GetDataArray(SID("extra-feature-level"), aExtraLevels, numExtraLevels, INVALID_STRING_ID_64);

			for (U32F i = 0; i < numExtraLevels; ++i)
			{
				AddLevelToApRegistrationQueue(aExtraLevels[i]);
			}
		}
	}

	return success;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::AddLevelToApRegistrationQueue(StringId64 levelId)
{
	const U64 curCount = m_apLevelRegistrationQueueCount.Get();

	if (curCount >= kMaxNavMeshCount)
		return;

	if (levelId == INVALID_STRING_ID_64)
		return;

	Level* pCollisionLevel = nullptr;
	pCollisionLevel = EngineComponents::GetLevelMgr()->GetLevel(levelId);
	if(!pCollisionLevel)
		pCollisionLevel = EngineComponents::GetLevelMgr()->GetTransitionalLevel(levelId);

	if (!pCollisionLevel)
		return;

	if (!pCollisionLevel->HasLogginStepRun()) // The loggin step didn't run yet so no need to reset AP registration
		return;

	pCollisionLevel->SetAlwaysRegisterAps(true);

	//MsgAi("[NmMgr] Resetting collision level '%s' ap progress\n", DevKitOnly_StringIdToString(levelId));

	pCollisionLevel->ResetApRegistrationProgress();

	bool existing = false;

	for (U32F i = 0; i < curCount; ++i)
	{
		if (m_apLevelRegistrationQueue[i].m_collisionLevelId == levelId)
		{
			m_apLevelRegistrationQueue[i].m_refCount++;
			existing = true;

			MsgAi("[NmMgr] Referencing collision level '%s' %d\n", DevKitOnly_StringIdToString(levelId), m_apLevelRegistrationQueue[i].m_refCount);
			break;
		}
	}

	if (!existing)
	{
		MsgAi("[NmMgr] Adding collision level '%s'\n", DevKitOnly_StringIdToString(levelId));

		m_apLevelRegistrationQueue[curCount].m_collisionLevelId = levelId;
		m_apLevelRegistrationQueue[curCount].m_refCount = 1;
		m_apLevelRegistrationQueueCount.Add(1);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::SetRegistrationEnabled(NavMesh* pMesh, bool enabled)
{
	RecursiveAtomicLockJanitor64 jj(&m_accessLock, FILE_LINE_FUNC);

	NAV_ASSERT(GetGlobalLock()->IsLockedForWrite());
	NAV_ASSERT(pMesh);

	if (!pMesh)
		return;

	const U32F iEntry = GetSlotIndex(pMesh->GetManagerId());

	if (iEntry < kMaxNavMeshCount)
	{
		NavMeshEntry& entry = m_navMeshEntries[iEntry];

		if (entry.m_pNavMesh == pMesh)
		{
			entry.m_registered = enabled;

			pMesh->m_gameData.m_flags.m_registered = enabled;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::RemoveNavMesh(NavMesh* pMeshToRemove)
{
	RecursiveAtomicLockJanitor64 jj(&m_accessLock, FILE_LINE_FUNC);

	NAV_ASSERT(GetGlobalLock()->IsLockedForWrite());
	NAV_ASSERT(pMeshToRemove);

	const U32F iMesh = GetSlotIndex(pMeshToRemove->m_managerId);
	NAV_ASSERT(iMesh < kMaxNavMeshCount);

	const NavMeshEntry& entry = m_navMeshEntries[iMesh];

	NAV_ASSERT(entry.m_pNavMesh == pMeshToRemove);
	NAV_ASSERT(m_usedNavMeshes.IsBitSet(iMesh));

	if (iMesh < kMaxNavMeshCount && entry.m_pNavMesh == pMeshToRemove)
	{
		m_navMeshEntries[iMesh].m_pNavMesh = nullptr;
		m_navMeshEntries[iMesh].m_hNavMesh = NavMeshHandle();
		pMeshToRemove->m_managerId.Invalidate();
		m_usedNavMeshes.ClearBit(iMesh);
	}

	const StringId64 collisionLevelId = pMeshToRemove->GetCollisionLevelId();

	if (collisionLevelId != INVALID_STRING_ID_64)
	{
		RemoveLevelFromApRegistrationQueue(collisionLevelId);
	}

	const EntityDB* pEntDb = pMeshToRemove->GetEntityDB();
	const U32F numExtraLevels = pEntDb ? pEntDb->GetRecordArraySize(SID("extra-feature-level")) : 0;

	if (numExtraLevels > 0)
	{
		StringId64* aExtraLevels = STACK_ALLOC(StringId64, numExtraLevels);

		pEntDb->GetDataArray(SID("extra-feature-level"), aExtraLevels, numExtraLevels, INVALID_STRING_ID_64);

		for (U32F i = 0; i < numExtraLevels; ++i)
		{
			RemoveLevelFromApRegistrationQueue(aExtraLevels[i]);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::RemoveLevelFromApRegistrationQueue(StringId64 levelId)
{
	if (levelId == INVALID_STRING_ID_64)
		return;

	const U64 curCount = m_apLevelRegistrationQueueCount.Get();
	U64 newCount = curCount;
	for (I32F i = curCount - 1; i >= 0; --i)
	{
		if (m_apLevelRegistrationQueue[i].m_collisionLevelId != levelId)
			continue;

		m_apLevelRegistrationQueue[i].m_refCount--;

		NAV_ASSERT(m_apLevelRegistrationQueue[i].m_refCount >= 0);

		if (m_apLevelRegistrationQueue[i].m_refCount <= 0)
		{
			MsgAi("[NmMgr] Removing collision level '%s'\n", DevKitOnly_StringIdToString(levelId));

			if (i >= (kMaxNavMeshCount - 1))
			{
				m_apLevelRegistrationQueue[i] = QueueEntry();
			}
			else
			{
				m_apLevelRegistrationQueue[i] = m_apLevelRegistrationQueue[newCount];
			}
			--newCount;
		}
		else
		{
			MsgAi("[NmMgr] De-referencing collision level '%s' %d\n",
				  DevKitOnly_StringIdToString(levelId),
				  m_apLevelRegistrationQueue[i].m_refCount);
		}
	}
	m_apLevelRegistrationQueueCount.Set(newCount);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::ChangeNavMeshBindSpawner(NavMesh* pMesh, StringId64 bindSpawnerNameId)
{
	RecursiveAtomicLockJanitor64 jj(&m_accessLock, FILE_LINE_FUNC);

	NAV_ASSERT(GetGlobalLock()->IsLockedForWrite());
	NAV_ASSERT(pMesh);

	const U32F iMesh = GetSlotIndex(pMesh->m_managerId);
	NAV_ASSERT(iMesh < kMaxNavMeshCount);

	const NavMeshEntry& entry = m_navMeshEntries[iMesh];
	NAV_ASSERT(entry.m_pNavMesh == pMesh);
	NAV_ASSERT(m_usedNavMeshes.IsBitSet(iMesh));
	
	if (iMesh < kMaxNavMeshCount && entry.m_pNavMesh == pMesh)
	{
		m_navMeshEntries[iMesh].m_bindSpawnerNameId = bindSpawnerNameId;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMesh* NavMeshMgr::FindNavMeshWs(Point_arg posWs, StringId64 bindSpawnerNameId /* = INVALID_STRING_ID_64 */) const
{
	NAV_ASSERT(GetGlobalLock()->IsLocked());

	FindBestNavMeshParams params;
	params.m_pointWs = posWs;
	params.m_bindSpawnerNameId = bindSpawnerNameId;
	FindNavMeshWs(&params);
	return const_cast<NavMesh*>(params.m_pNavMesh);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::UpdateNavMeshBBox(const NavMesh* pNavMesh)
{
	NAV_ASSERT(GetGlobalLock()->IsLocked());

	NAV_ASSERT(pNavMesh);
	NAV_ASSERT(pNavMesh->IsValid());
	U32F iMesh = GetSlotIndex(pNavMesh->m_managerId);
	NAV_ASSERT(iMesh < kMaxNavMeshCount);
	NavMeshEntry& entry = m_navMeshEntries[iMesh];
	NAV_ASSERT(entry.m_pNavMesh == pNavMesh);
	
	entry.m_loc = pNavMesh->GetOriginWs();
	entry.m_vecRadius = pNavMesh->m_vecRadius;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavMeshMgr::LookupNavMeshEntryByName(StringId64 nameId) const
{
	NAV_ASSERT(GetGlobalLock()->IsLocked());

	U32F iEntry = kMaxNavMeshCount;

	for (U64 iMesh : m_usedNavMeshes)
	{
		const NavMeshEntry& entry = m_navMeshEntries[iMesh];
		NAV_ASSERT(entry.m_pNavMesh);

		if (entry.m_pNavMesh->GetNameId() == nameId)
		{
			iEntry = iMesh;
			break;
		}
	}
	return iEntry;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NavMeshHandle NavMeshMgr::FindNavMeshByName(StringId64 nameId) const
{
	NAV_ASSERT(GetGlobalLock()->IsLocked());

	NavMeshHandle hNavMesh;
	U32F iEntry = LookupNavMeshEntryByName(nameId);
	if (iEntry < kMaxNavMeshCount)
	{
		const NavMeshEntry& entry = m_navMeshEntries[iEntry];
		hNavMesh = entry.m_hNavMesh;
	}
	return hNavMesh;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshMgr::EnableNavMeshByName(StringId64 nameId, bool enabled)
{
	NAV_ASSERT(GetGlobalLock()->IsLockedForWrite());

	bool success = false;

	const U32F iEntry = LookupNavMeshEntryByName(nameId);
	if ((iEntry < kMaxNavMeshCount) && m_navMeshEntries[iEntry].m_pNavMesh)
	{
		success = true;
		if (m_navMeshEntries[iEntry].m_registered != enabled)
		{
			if (enabled)
			{
				m_navMeshEntries[iEntry].m_pNavMesh->Register();
			}
			else
			{
				m_navMeshEntries[iEntry].m_pNavMesh->Unregister();
			}
		}
	}

	return success;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavMeshMgr::EnableNavMeshesByLevel(StringId64 levelId, bool enabled)
{
	NAV_ASSERT(GetGlobalLock()->IsLockedForWrite());

	U32F count = 0;

	for (U64 iEntry : m_usedNavMeshes)
	{
		NavMeshEntry& entry = m_navMeshEntries[iEntry];
		if (!entry.m_pNavMesh)
			continue;

		if (entry.m_pNavMesh->GetLevelId() == levelId)
		{
			++count;

			if (m_navMeshEntries[iEntry].m_registered != enabled)
			{
				if (enabled)
				{
					m_navMeshEntries[iEntry].m_pNavMesh->Register();
				}
				else
				{
					m_navMeshEntries[iEntry].m_pNavMesh->Unregister();
				}
			}
		}
	}

	return count;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::DebugUnregisterAllNavMeshes()
{
	NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

	for (U64 iEntry : m_usedNavMeshes)
	{
		NavMeshEntry& entry = m_navMeshEntries[iEntry];
		NavMesh* pMesh = entry.m_pNavMesh;

		NAV_ASSERT(pMesh);

		pMesh->Logout(nullptr);  // calls UnregisterNavMesh()
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::Init()
{
	g_navMeshGlobalLock.Initialize(FILE_LINE_FUNC);

	if (Memory::IsDebugMemoryAvailable())
	{
		AllocateJanitor jj(kAllocDebug, FILE_LINE_FUNC);

		static const U32 kMaxSelectedNavMeshes = 32;
		m_pSelectionStorage = NDI_NEW(kAllocDebug) StringId64[kMaxSelectedNavMeshes];
		m_selection.InitSelection(kMaxSelectedNavMeshes, m_pSelectionStorage);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::Update()
{
	const U64 curCount = m_apLevelRegistrationQueueCount.Get();
	if (curCount > 0)
	{
		RecursiveAtomicLockJanitor64 lock(&m_accessLock, FILE_LINE_FUNC);

		ActionPackMgr& apMgr = ActionPackMgr::Get();

		float remainingTime = 0.002f; // 2ms in seconds
		const U64 beginTick = TimerGetRawCount();

		U64 newCount = curCount;

		for (I32F i = curCount - 1; i >= 0; --i)
		{
			const QueueEntry& entry = m_apLevelRegistrationQueue[i];
			Level* pCollisionLevel = EngineComponents::GetLevelMgr()->GetLevel(entry.m_collisionLevelId);

			if (!pCollisionLevel || !pCollisionLevel->IsLoaded())
				continue;
			
			if (pCollisionLevel->AreActionPacksRegistered())
			{
				//MsgOut("[NmMgr] Collision level '%s' already complete, removing\n", pCollisionLevel->GetName());

				// aps are already registered or the level login will handle it 
				if (i >= (kMaxNavMeshCount - 1))
				{
					m_apLevelRegistrationQueue[i] = QueueEntry();
				}
				else
				{
					m_apLevelRegistrationQueue[i] = m_apLevelRegistrationQueue[newCount];
				}
				newCount--;
				continue;
			}

			{
				NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
				if (pCollisionLevel->RegisterActionPacks(remainingTime))
				{
					//MsgOut("[NmMgr] Completing collision level '%s'\n", pCollisionLevel->GetName());

					apMgr.ReRegisterActionPacksForLevel(pCollisionLevel);

					if (i >= (kMaxNavMeshCount - 1))
					{
						m_apLevelRegistrationQueue[i] = QueueEntry();
					}
					else
					{
						m_apLevelRegistrationQueue[i] = m_apLevelRegistrationQueue[newCount];
					}
					newCount--;
				}
			}

			const U64 curTick = TimerGetRawCount();
			const float elapsedTime = ConvertTicksToSeconds(curTick - beginTick);
			remainingTime -= elapsedTime;
			if (elapsedTime < 0.0f)
				break;
		}

		m_apLevelRegistrationQueueCount.Set(newCount);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::ShutDown()
{
	g_navMeshGlobalLock.Destroy();

	if (m_pSelectionStorage)
	{
		m_selection.ResetStorage();
		NDI_DELETE_ARRAY_CONTEXT(kAllocDebug, m_pSelectionStorage);
		m_pSelectionStorage = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::SetMeshSelected(StringId64 navMeshId, bool selected)
{
	if (selected)
		m_selection.SelectId(navMeshId);
	else
		m_selection.DeselectId(navMeshId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::ToggleMeshSelected(StringId64 navMeshId)
{
	const bool selected = IsMeshSelected(navMeshId);
	SetMeshSelected(navMeshId, !selected);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshMgr::IsMeshSelected(StringId64 navMeshId) const
{
	return m_selection.IsIdSelected(navMeshId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshMgr::IsMeshOrNoneSelected(StringId64 navMeshId) const
{
	return (m_selection.GetCount() == 0) || IsMeshSelected(navMeshId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavMeshMgr::GetMeshSelectedCount() const
{
	return m_selection.GetCount();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::ClearMeshSelection()
{
	m_selection.DeselectAll();
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavMeshMgr::GetSelectedNavMeshes(const NavMesh** apMeshesOut, U32F maxMeshesOut) const
{
	NAV_ASSERT(GetGlobalLock()->IsLocked());

	U32F navMeshCount = 0;

	for (U64 iEntry : m_usedNavMeshes)
	{
		if (navMeshCount >= maxMeshesOut)
			break;

		const NavMeshEntry& entry = m_navMeshEntries[iEntry];
		NAV_ASSERT(entry.m_pNavMesh);

		if (m_selection.IsIdSelected(entry.m_pNavMesh->GetNameId()))
		{
			apMeshesOut[navMeshCount++] = entry.m_pNavMesh;
		}
	}

	return navMeshCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool LevelIdMatches(const NavMesh* pMesh, const FindBestNavMeshParams* pParams)
{
	if (pParams->m_requiredLevelId == INVALID_STRING_ID_64)
		return true;

	if (pMesh->GetLevelId() == pParams->m_requiredLevelId)
		return true;

	if (pMesh->GetCollisionLevelId() == pParams->m_requiredLevelId)
		return true;

	const EntityDB* pEntDb = pMesh->GetEntityDB();
	if (!pEntDb)
		return true;

	const U32F numExtraLevels = pEntDb->GetRecordArraySize(SID("extra-feature-level"));

	if (numExtraLevels > 0)
	{
		StringId64* aExtraLevels = STACK_ALLOC(StringId64, numExtraLevels);

		pEntDb->GetDataArray(SID("extra-feature-level"), aExtraLevels, numExtraLevels, INVALID_STRING_ID_64);

		for (U32F i = 0; i < numExtraLevels; ++i)
		{
			if (aExtraLevels[i] == pParams->m_requiredLevelId)
			{
				return true;
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NavMeshMgr::FindNavMeshWs(FindBestNavMeshParams* pParams) const
{
	PROFILE(AI, FindNavMesh);

	NAV_ASSERT(GetGlobalLock()->IsLocked());

	U32F navMeshCount = 0;
	NavMeshHandle navMeshList[kMaxNavMeshCount];
	const Point posWs = pParams->m_pointWs;
	const Scalar cullRadius = Max(pParams->m_cullDist, pParams->m_yThreshold);

	{
		PROFILE(AI, BuildNavMeshList);

		for (U64 iMesh : m_usedNavMeshes)
		{
			const NavMeshEntry& entry = m_navMeshEntries[iMesh];

			NAV_ASSERT(!entry.m_hNavMesh.IsNull());

			if (!entry.m_registered)
				continue;

			if (!LevelIdMatches(entry.m_pNavMesh, pParams))
				continue;

			const bool bindMatch = (pParams->m_bindSpawnerNameId == Nav::kMatchAllBindSpawnerNameId)
								   || (entry.m_bindSpawnerNameId == pParams->m_bindSpawnerNameId);
			if (!bindMatch)
				continue;

			if (!pParams->m_swimMeshAllowed
				&& (entry.m_pNavMesh->NavMeshForcesSwim() || entry.m_pNavMesh->NavMeshForcesDive()))
				continue;

			if (!entry.IsPointInside(posWs, cullRadius))
				continue;

			navMeshList[navMeshCount++] = entry.m_hNavMesh;
		}
	}

	{
		NAV_ASSERT(pParams->m_cullDist >= 0.0f);

		const NavMesh* pBestMesh = nullptr;
		const NavPoly* pBestPoly = nullptr;

		Point bestPos(kZero);
		Scalar cullXZ2 = Sqr(pParams->m_cullDist) + 0.001f;
		Scalar cullY = Abs(pParams->m_yThreshold) + 0.001f;
		Scalar bestDist2 = SCALAR_LC(kLargeFloat);
		const Scalar kXZWeight2 = Sqr(8.0f);  // we care more about XZ than Y so we weight XZ distance squared by this squared factor
		float bboxCullDist = Max(pParams->m_cullDist, pParams->m_yThreshold);
		NavMesh::FindPointParams findPolyParams;

		findPolyParams.m_searchRadius = pParams->m_cullDist;
		findPolyParams.m_obeyedStaticBlockers = pParams->m_obeyedStaticBlockers;

		for (I32F i = 0; i < navMeshCount; ++i)
		{
			NavMeshHandle hMesh = navMeshList[i];
			
			if (const NavMesh* pNavMesh = LookupRegisteredNavMesh(hMesh))
			{
				if (!pNavMesh->IsRegistered())
					continue;

				NAV_ASSERT(pNavMesh->PointInBoundingBoxWs(posWs, bboxCullDist + 0.01f));

				Point pos = pNavMesh->WorldToParent(posWs);
				findPolyParams.m_point = pos;
				pNavMesh->FindNearestPointPs(&findPolyParams);

				if (findPolyParams.m_pPoly)
				{
					Scalar distY = Abs(findPolyParams.m_nearestPoint.Y() - pos.Y());
					Scalar distXZ2 = DistXzSqr(findPolyParams.m_nearestPoint, pos);
					if (distXZ2 <= cullXZ2 && distY <= cullY)
					{
						Scalar dist2 = distXZ2*kXZWeight2 + Sqr(distY);
						if (findPolyParams.m_pPoly->IsLink() /* && findPolyParams.m_pPoly->GetLinkedPoly()*/)
						{
							dist2 += SCALAR_LC(1.0f);  // discourage choosing nav mesh where closest poly is a link
						}
						if (dist2 < bestDist2)
						{
							bestDist2 = dist2;
							pBestMesh = pNavMesh;
							pBestPoly = findPolyParams.m_pPoly;
							bestPos = pNavMesh->ParentToWorld(findPolyParams.m_nearestPoint);
						}
					}
				}
			}
		}

		NAV_ASSERT(!pBestPoly || pBestPoly->PolyContainsPointLs(pBestMesh->WorldToLocal(bestPos), 0.01f));

		pParams->m_pNavMesh = pBestMesh;
		pParams->m_pNavPoly = pBestPoly;
		pParams->m_nearestPointWs = bestPos;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshMgr::IsNavMeshHandleValid(NavMeshHandle hNavMesh) const
{
	NAV_ASSERT(GetGlobalLock()->IsLocked());

	// check if the handle is valid without triggering a DMA on SPU or looking at the NavMesh itself
	const U32F iEntry = GetSlotIndex(hNavMesh.GetManagerId());

	if (!hNavMesh.IsNull() && iEntry < kMaxNavMeshCount)
	{
		const NavMeshEntry& entry = m_navMeshEntries[iEntry];
		if (entry.m_hNavMesh == hNavMesh && entry.m_pNavMesh && entry.m_registered)
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavMeshMgr::GetNumRegisteredNavMeshes() const
{
	NAV_ASSERT(GetGlobalLock()->IsLocked());

	NavMeshBits::Iterator iter(m_usedNavMeshes);

	U32F count = 0;

	for (U64 iEntry : m_usedNavMeshes)
	{
		if (m_navMeshEntries[iEntry].m_registered)
		{
			++count;
		}
	}

	return count;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// WARNING: this function can return NavMeshes that are not registered,
// and therefore are not actually valid if round-tripped through a NavMeshHandle!
//
// Some clients want this behavior so they can, for example, turn on/off unregistered meshes, or draw them,
// so the function is provided, but use with care.
U32F NavMeshMgr::GetNavMeshList_IncludesUnregistered(NavMesh** navMeshList, U32F maxListLen) const
{
	NAV_ASSERT(GetGlobalLock()->IsLocked());

	U32F navMeshCount = 0;

	for (U64 iEntry : m_usedNavMeshes)
	{
		if (navMeshCount >= maxListLen)
			break;

		const NavMeshEntry& entry = m_navMeshEntries[iEntry];
		NAV_ASSERT(entry.m_pNavMesh);

		navMeshList[navMeshCount++] = entry.m_pNavMesh;
	}
	return navMeshCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavMeshMgr::GetNavMeshList(NavMeshHandle* navMeshList, U32F maxListLen) const
{
	NAV_ASSERT(GetGlobalLock()->IsLocked());

	U32F navMeshCount = 0;

	for (U64 iEntry : m_usedNavMeshes)
	{
		if (navMeshCount >= maxListLen)
			break;

		const NavMeshEntry& entry = m_navMeshEntries[iEntry];
		NAV_ASSERT(entry.m_pNavMesh);

		navMeshList[navMeshCount++] = entry.m_hNavMesh;
	}
	return navMeshCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// WARNING: this function can return NavMeshes that are not registered,
// and therefore are not actually valid if round-tripped through a NavMeshHandle!
U32F NavMeshMgr::GetNavMeshListFromAabbWs(Aabb_arg bboxWs, NavMesh** navMeshList, U32F maxListLen) const
{
	NAV_ASSERT(GetGlobalLock()->IsLocked());

	U32F navMeshCount = 0;

	for (U64 iEntry : m_usedNavMeshes)
	{
		if (navMeshCount >= maxListLen)
			break;

		const NavMeshEntry& entry = m_navMeshEntries[iEntry];
		NAV_ASSERT(entry.m_pNavMesh);
		if (!entry.m_pNavMesh)
			continue;

		const Aabb meshBoxWs = entry.m_pNavMesh->GetAabbWs();

		if (meshBoxWs.IntersectAabb(bboxWs))
		{
			navMeshList[navMeshCount++] = entry.m_pNavMesh;
		}
	}

	return navMeshCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NavMeshMgr::GetNavMeshListFromAabbWs(Aabb_arg bboxWs, NavMeshHandle* navMeshList, U32F maxListLen) const
{
	NAV_ASSERT(GetGlobalLock()->IsLocked());

	U32F navMeshCount = 0;

	for (U64 iEntry : m_usedNavMeshes)
	{
		if (navMeshCount >= maxListLen)
			break;

		const NavMeshEntry& entry = m_navMeshEntries[iEntry];
		NAV_ASSERT(entry.m_pNavMesh);
		if (!entry.m_pNavMesh)
			continue;

		const Aabb meshBoxWs = entry.m_pNavMesh->GetAabbWs();

		if (meshBoxWs.IntersectAabb(bboxWs))
		{
			navMeshList[navMeshCount++] = entry.m_hNavMesh;
		}
	}

	return navMeshCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NavMesh* NavMeshMgr::GetNavMeshByName(StringId64 nameId) const
{
	NAV_ASSERT(GetGlobalLock()->IsLocked());

	for (U64 iEntry : m_usedNavMeshes)
	{
		const NavMeshEntry& entry = m_navMeshEntries[iEntry];
		NAV_ASSERT(entry.m_pNavMesh);
		if (!entry.m_pNavMesh)
			continue;

		if (entry.m_pNavMesh->GetNameId() == nameId)
		{
			return entry.m_pNavMesh;
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// NOTE: the index is not a fully unique identifier! In some cases this is fine, but if you're looking to hold
// onto a particular NavMesh and later get that same NavMesh back, use a NavMeshHandle. In the time between when you
// first save off the index and when you call this function, that NavMesh could be unloaded, and another NavMesh loaded
// in and given the same index. NavMeshHandles store an additional unique ID that prevents this danger.
const NavMesh* NavMeshMgr::GetNavMeshAtIndex(U64 idx) const
{
	NAV_ASSERT(GetGlobalLock()->IsLocked());
	return m_navMeshEntries[idx].m_pNavMesh;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NavMeshMgr::DoAnyNavMeshesHaveErrors() const
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	for (U64 iEntry : m_usedNavMeshes)
	{
		const NavMeshEntry& entry = m_navMeshEntries[iEntry];

		if (entry.m_pNavMesh && entry.m_pNavMesh->HasErrorPolys())
		{
			return true;
		}
	}

	return false;
}
