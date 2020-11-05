/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/system/recursive-atomic-lock.h"
#include "corelib/util/bit-array.h"

#include "ndlib/text/stringid-selection.h"

#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-mesh.h"

 /// --------------------------------------------------------------------------------------------------------------- ///
class Level;
struct FindBestNavMeshParams;

/// --------------------------------------------------------------------------------------------------------------- ///
class NavMeshGlobalLock
{
public:
	NavMeshGlobalLock()
		: m_pLockCounter(nullptr)
#if ENABLE_ND_LOCKED_BY
		, m_pLockedBy(nullptr)
#endif
	{
	}

	void Initialize(const char* file, U32F line, const char* func);
	void Destroy();

	void AcquireReadLock(const char* file, U32F line, const char* func);
	void ReleaseReadLock();

	void AcquireWriteLock(const char* file, U32F line, const char* func);
	void ReleaseWriteLock();

	bool IsLocked() const;
	bool IsLockedForRead() const;
	bool IsLockedForWrite() const;

private:
	static CONST_EXPR I64	kWriteRequestSentinel = 1000000;
	static CONST_EXPR I64	kWriteActiveSentinel = 10000;
	ndjob::CounterHandle 	m_pLockCounter;

#if ENABLE_ND_LOCKED_BY
	NdLockedBy*			m_pLockedBy;
#endif
};

extern NavMeshGlobalLock g_navMeshGlobalLock;


/// --------------------------------------------------------------------------------------------------------------- ///
class NavMeshLockJanitor
{
public:
	NavMeshLockJanitor(bool readOnly, const char* file, U32F line, const char* func);
	~NavMeshLockJanitor();

private:
	NavMeshGlobalLock&	m_lock;
	bool m_readLock;
};


/// --------------------------------------------------------------------------------------------------------------- ///
class NavMeshWriteLockJanitor
{
public:
	NavMeshWriteLockJanitor(const char* file, U32F line, const char* func);
	~NavMeshWriteLockJanitor();

private:
	NavMeshGlobalLock&	m_lock;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NavMeshReadLockJanitor
{
public:
	NavMeshReadLockJanitor(const char* file, U32F line, const char* func);
	~NavMeshReadLockJanitor();

private:
	NavMeshGlobalLock&	m_lock;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class ALIGNED(128) NavMeshMgr
{
public:
	typedef void (*PFnLoginObserver)(const NavMesh*, Level*);
	typedef void (*PFnNotifyObserver)(const NavMesh*);

#ifdef HEADLESS_BUILD
	const static U32F kMaxNavMeshCount = 2048;
#else
	const static U32F kMaxNavMeshCount = 1024;
#endif
	typedef BitArray<kMaxNavMeshCount> NavMeshBits;

	NavMeshMgr();

	static NavMeshGlobalLock* GetGlobalLock() { return &g_navMeshGlobalLock; }

	void Init();
	void Update();
	void ShutDown();

	void AddLoginObserver(PFnLoginObserver fn);
	void AddRegisterObserver(PFnNotifyObserver fn);
	void AddUnregisterObserver(PFnNotifyObserver fn);
	void AddLogoutObserver(PFnNotifyObserver fn);

	bool AddNavMesh(NavMesh* pMeshToAdd);
	void SetRegistrationEnabled(NavMesh* pMesh, bool enabled);
	void RemoveNavMesh(NavMesh* pMeshToRemove);

	void OnLogin(NavMesh* pMesh, Level* pLevel);
	void OnRegister(NavMesh* pMesh);
	void OnUnregister(NavMesh* pMesh);
	void OnLogout(NavMesh* pMesh);

	U32F GetNumTotalNavMeshes() const
	{
		NAV_ASSERT(GetGlobalLock()->IsLocked());
		return m_usedNavMeshes.CountSetBits();
	}

	U32F GetNumRegisteredNavMeshes() const;
	U32F GetNavMeshList_IncludesUnregistered(NavMesh** navMeshList, U32F maxListLen) const;
	U32F GetNavMeshList(NavMeshHandle* navMeshList, U32F maxListLen) const;
	U32F GetNavMeshListFromAabbWs(Aabb_arg bboxWs, NavMesh** navMeshList, U32F maxListLen) const;
	U32F GetNavMeshListFromAabbWs(Aabb_arg bboxWs, NavMeshHandle* navMeshList, U32F maxListLen) const;
	const NavMesh* GetNavMeshByName(StringId64 nameId) const;
	const NavMesh* GetNavMeshAtIndex(U64 idx) const;

	NavMesh* FindNavMeshWs(Point_arg posWs, StringId64 bindSpawnerNameId = INVALID_STRING_ID_64) const;
	void FindNavMeshWs(FindBestNavMeshParams* pParams) const;

	void ChangeNavMeshBindSpawner(NavMesh* pMesh, StringId64 bindSpawnerNameId);

	NavMeshHandle FindNavMeshByName(StringId64 nameId) const;
	bool EnableNavMeshByName(StringId64 nameId, bool enabled);
	U32F EnableNavMeshesByLevel(StringId64 levelId, bool enabled);

	const NavMesh* LookupRegisteredNavMesh(NavMeshHandle hMesh) const;

	// Check if the levelId appear as an extra feature level in any of the existing navmeshes
	bool IsAnExtraFeatureLevel(StringId64 levelId) const;

	// if you know you have a valid nav manager ID and hold the navmesh read lock,
	// the only things we need to check are the unique ID (for ABA problem)
	// and that we're still registered.
	const NavMesh* UnsafeFastLookupNavMesh(NavManagerId id) const
	{
		const NavMeshEntry& entry = m_navMeshEntries[id.m_navMeshIndex];
		return entry.m_hNavMesh.GetManagerId().m_uniqueId == id.m_uniqueId && entry.m_registered ? entry.m_pNavMesh : nullptr;
	}
	const NavMesh* UnsafeFastLookupNavMesh(U16 navMeshIndex, U16 uniqueId) const
	{
		const NavMeshEntry& entry = m_navMeshEntries[navMeshIndex];
		return entry.m_hNavMesh.GetManagerId().m_uniqueId == uniqueId && entry.m_registered ? entry.m_pNavMesh : nullptr;
	}

	// if you're just walking meshes using nav mesh indices you just stored off, holding a nav mesh read lock the whole time,
	// you don't need to check unique IDs or registration either, so it's safe and fast
	// to directly query entries by navmeshIndex. obviously, use with care.
	const NavMesh* UnsafeFastLookupNavMesh(U16 navMeshIndex) const
	{
		return m_navMeshEntries[navMeshIndex].m_pNavMesh;
	}

	bool IsNavMeshHandleValid(NavMeshHandle hNavMesh) const;

	void UpdateNavMeshBBox(const NavMesh* pMesh);
	void DebugUnregisterAllNavMeshes();

	void SetMeshSelected(StringId64 navMeshId, bool selected);
	void ToggleMeshSelected(StringId64 navMeshId);
	bool IsMeshSelected(StringId64 navMeshId) const;
	bool IsMeshOrNoneSelected(StringId64 navMeshId) const;
	U32F GetMeshSelectedCount() const;
	void ClearMeshSelection();
	U32F GetSelectedNavMeshes(const NavMesh** apMeshesOut, U32F maxMeshesOut) const;

	bool Failed() const { return false; }

	bool DoAnyNavMeshesHaveErrors() const;

	U32 FindOverlappingHeightMaps(const Point& obs, U32 maxResults, NavMeshHandle* results) const;

private:
	static inline U32F GetSlotIndex(const NavManagerId mgrId)	{ return mgrId.m_navMeshIndex; }
	static inline U32F GetPolyId(const NavManagerId mgrId)		{ return mgrId.m_iPoly; }
	static inline U32F GetUniqueId(const NavManagerId mgrId)	{ return mgrId.m_uniqueId; }

	U32F LookupNavMeshEntryByName(StringId64 nameId) const;

	void AddLevelToApRegistrationQueue(StringId64 levelId);
	void RemoveLevelFromApRegistrationQueue(StringId64 levelId);

	const static U32 kSignatureValue = 0xbadb100d;
	static const size_t kMaxObservers = 8;

	struct NavMeshEntry
	{
		Locator			m_loc;
		Vector			m_vecRadius;
		NavMesh*		m_pNavMesh;
		StringId64		m_bindSpawnerNameId;
		NavMeshHandle	m_hNavMesh;
		U32				m_signatureWord;
		bool			m_registered;
		U8				m_pad[3];

		inline bool IsPointInside(Point_arg ptWs, Scalar_arg radius) const
		{
			if (!m_pNavMesh)
				return false;

			const Point ptLs = m_pNavMesh->WorldToLocal(ptWs);
			const Vector absPtLs = Abs(ptLs - kOrigin);
			const Vector vecR = m_vecRadius + radius;
			return AllComponentsLessThanOrEqual(absPtLs, vecR);
		}
	};

	mutable NdRecursiveAtomicLock64	m_accessLock;

	NavMeshBits				m_usedNavMeshes;					// used navmesh entries
	NavMeshEntry			m_navMeshEntries[kMaxNavMeshCount];	// used entries not always contiguous (may have holes)

	U32						m_signatureWord;
	U32						m_idCounter;

	StringIdSelection		m_selection;
	StringId64*				m_pSelectionStorage;

	struct QueueEntry
	{
		StringId64 m_collisionLevelId = INVALID_STRING_ID_64;
		I32 m_refCount = 0;
	};

	QueueEntry				m_apLevelRegistrationQueue[kMaxNavMeshCount];
	NdAtomic64				m_apLevelRegistrationQueueCount;

	PFnLoginObserver		m_fnLoginObservers[kMaxObservers];
	PFnNotifyObserver		m_fnRegisterObservers[kMaxObservers];
	PFnNotifyObserver		m_fnUnregisterObservers[kMaxObservers];
	PFnNotifyObserver		m_fnLogoutObservers[kMaxObservers];

	U32						m_numLoginObservers;
	U32						m_numRegisterObservers;
	U32						m_numUnregisterObservers;
	U32						m_numLogoutObservers;

	NavMeshMgr(const NavMeshMgr&) {} // NOPE
};
