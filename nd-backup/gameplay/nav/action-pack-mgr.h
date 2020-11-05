/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "corelib/system/read-write-atomic-lock.h"
#include "corelib/util/bit-array.h"

#include "ndlib/process/process.h"

#include "gamelib/gameplay/nav/action-pack-handle.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ActionPack;
class Level;
class Region;
struct Segment;

/// --------------------------------------------------------------------------------------------------------------- ///
class ALIGNED(16) ActionPackMutex
{
public:
	ActionPackMutex();
	void Init(StringId64 nameId);
	void Reset();
	void AddOwnerRef(ActionPack* pAp);
	void RemoveOwnerRef(ActionPack* pAp);
	bool AddUserRef(Process* pChar);
	bool TryAddUserRef(ActionPack* pAp, Process* pChar);
	bool RemoveUserRef(Process* pChar);
	I32F GetUserCount() const { return m_userRefCount; }
	I32F GetOwnerCount() const { return m_ownerRefCount; }
	ActionPack* GetOwner(U32F index) const
	{
		return index < m_ownerRefCount ? m_hOwnerList[index].ToActionPack() : nullptr;
	}
	bool IsAvailable(const ActionPack* pAp, const Process* pProcess) const;
	bool IsUser(const Process* pProcess) const;
	bool TryEnable(const ActionPack* pAp, const Process* pProcess);
	ActionPack* GetEnabledActionPack() const { return m_hEnabledAp.ToActionPack(); }

	StringId64 GetNameId() const { return m_nameId; }
	bool IsValid() const; // is this mutex hooked up to any action packs?
	void Validate() const;
	void ValidateNoLock() const;

	void EnableDirectionalValve(bool enable) { m_directionalValveEnabled = enable; }

private:
	const static U32F kMaxActionPackCount = 4;
	const static U32F kMaxUserCount		  = 8;

	bool IsUserUnsafe(const Process* pProcess) const;
	bool IsAvailableUnsafe(const ActionPack* pWantAp, const Process* pProcess) const;
	bool TryEnableUnsafe(const ActionPack* pAp, const Process* pProcess);

	mutable NdAtomicLock m_accessLock;
	StringId64 m_nameId;
	I32 m_ownerRefCount;
	I32 m_userRefCount;
	ActionPackHandle m_hEnabledAp;
	StringId64 m_userList[kMaxUserCount];
	ActionPackHandle m_hOwnerList[kMaxActionPackCount];
	bool m_directionalValveEnabled;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class ActionPackMgr
{
public:
#ifdef HEADLESS_BUILD
	const static U32F kMaxActionPackCount = 12288;
#else
	const static U32F kMaxActionPackCount = 6144;
#endif

	const static U32F kInvalidMgrId = ActionPackHandle::kInvalidMgrId;
	typedef void (*PFnNotifyObserver)(ActionPack*);
	typedef bool VisitActionPack(I32F apType, ActionPack* pAp, uintptr_t data);

	static ActionPackMgr& Get() { return m_singleton; };

	ActionPackMgr();
	void Init();
	void Update();

	void LoginActionPack(ActionPack* pAp);
	void SetUpdatesEnabled(ActionPack* pAp, bool enabled);
	void RelocateActionPack(ActionPack* pAp, ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);
	void LogoutActionPack(ActionPack* pAp);
	void LogoutLevel(const Level* pLevel);

	// registration queue
	void RequestRegistration(ActionPack* pAp);
	void RequestUnregistration(ActionPack* pAp);
	void RequestUpdateTraversalActionPackLinkages();

	bool IsRegisteredOrPending(U32F iMgrId) const;
	bool IsPendingUnregistration(U32F iMgrId) const;

	bool HasPendingRegistration() const;
	bool HasPendingRegistration(const Level* pLevel) const;
	bool ProcessPendingRegistration(const Level* pLevel);

	ActionPack* LookupLoggedInActionPack(U32F iMgrId);
	ActionPack* LookupRegisteredActionPack(U32F iMgrId);
	ActionPack* LookupActionPackByName(StringId64 nameId);
	ActionPackMutex* LookupActionPackMutexByName(StringId64 nameId);

	void FlagProcessOwnership(ActionPack* pAp);
	void ClearProcessOwnership(ActionPack* pAp);
	U32F GetListOfOwnedActionPacks(const Process* pOwnerProcess,
								   ActionPack** __restrict ppOutList,
								   U32F maxOutCount) const;

	void InvalidateAllActionPackHandles();
	void ResetAll();

	U32F FindActionPacksBySpawnerId(ActionPack** __restrict ppOutList, U32F maxOutCount, StringId64 spawnerId) const;
	U32F FindActionPacksByType(ActionPack** __restrict ppOutList, U32F maxOutCount, I32F apType) const;
	U32F FindActionPacksByTypeInRadius(ActionPack** __restrict ppOutList,
									   U32F maxOutCount,
									   I32F apType,
									   Point_arg searchOriginWs,
									   float searchRadius) const;

	U32F FindActionPacksByTypeMaskInRadius(ActionPack** __restrict ppOutList,
										   U32F maxOutCount,
										   U32F typeMask,
										   Point_arg searchOriginWs,
										   float searchRadius,
										   float entryOffset) const;

	U32 FindActionPacksByTypeInRadiusFast(ActionPack** __restrict ppOutList,
										  U32 maxOutCount,
										  I32 apType,
										  const Point searchOriginWs,
										  float searchRadius) const;
	U32F FindActionPacksByTypeInRegion(ActionPack** __restrict ppOutList,
									   U32F maxOutCount,
									   I32F apType,
									   const Region* pRegion) const;
	U32F FindActionPacksByTypeInRegion(ActionPack** __restrict ppOutList, U32F maxOutCount, I32F apType, StringId64 regionId) const;
	U32F FindActionPacksByTypeOnSegment(ActionPack** __restrict ppOutList,
										U32F maxOutCount,
										I32F apType,
										const Segment& searchSegmentWs,
										float searchRadius) const;

	void ForAllActionPacks(VisitActionPack callback, uintptr_t data, I32F slotStride = -1) const;

	void FindClosestActionPackByType(ActionPack** __restrict pOut, I32F apType, Point_arg pos, Process* pChar) const;
	U32F GetRegisteredActionPackCount() const;
	U32F GetAllocatedActionPackCount() const;

	void ReRegisterActionPacksForLevel(const Level* pLevel);

	// register callbacks
	void RegisterRegisterObserver(PFnNotifyObserver fn);
	void RegisterUnregisterObserver(PFnNotifyObserver fn);

	void RegisterLoginObserver(PFnNotifyObserver fn);
	void RegisterLogoutObserver(PFnNotifyObserver fn);

	void AdjustGameTime(TimeDelta delta);
	void DebugCheckForCorruption();
	void GetTypeCounts(U32F* pCountArray) const;

	bool CanRegisterNewAp() const { return m_usedMap.FindFirstClearBit() < kMaxActionPackCount; }

	NdRwAtomicLock64_Jls* GetLock() const { return &m_managerLock; }

	friend class ActionPack;
	friend class ActionPackHandle;

private:
	void Register(ActionPack* pAp);
	void Unregister(ActionPack* pAp);

	typedef BitArray<kMaxActionPackCount> ActionPackBits;

	ActionPackBits m_usedMap;			// Map of allocated action pack slots
	ActionPackBits m_regMap;			// Map of registered action packs by slot
	ActionPackBits m_updateMap;			// Map of action packs that need to have their Update() called
	ActionPackBits m_pendingReg;		// Map of action packs to unregister next Update()
	ActionPackBits m_pendingUnreg;		// Map of action packs to unregister next Update()
	ActionPackBits m_apsWithOwners;		// Action packs whose lifetimes are tied to an owner process
	ActionPackBits m_dirtyTapLinkages;	// Action packs whose tap linkages need to be re-checked

	ActionPack** m_pActionPack; // Array of action pack pointers
	U8* m_apType;				// Array of action pack types

	U32 m_idGen;
	U32 m_registeredActionPackCount;
	U32 m_actionPackCount;
	U32 m_peakActionPackCount;
	U32 m_peakMutexCount;
	U32 m_maxActionPackMutexCount;
	ActionPackMutex* m_mutexList; // Array of action pack mutexes
	mutable NdRwAtomicLock64_Jls m_managerLock;

	static const U32 kMaxUnregisterObservers = 3;

	PFnNotifyObserver m_fnRegisterObserver;
	PFnNotifyObserver m_fnUnregisterObserver[kMaxUnregisterObservers];
	PFnNotifyObserver m_fnLoginObserver;
	PFnNotifyObserver m_fnLogoutObserver;

	static inline U32F MakeMgrId(U32F iSlotIndex, U32F iUniqueId) { return (iUniqueId & 0xffff) | (iSlotIndex << 16); }

	static inline U32F GetSlotIndex(U32F iMgrId) { return iMgrId >> 16; }

	static inline U32F GetUniqueId(U32F iMgrId) { return iMgrId & 0xffff; }

	void UpdateTraversalActionPackLinkages();
	static ActionPackMgr m_singleton;
};
