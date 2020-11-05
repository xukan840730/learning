/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/simple-grid-hash-mgr.h"

#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/simple-grid-hash.h"
#include "gamelib/gameplay/nd-game-object.h"

/// --------------------------------------------------------------------------------------------------------------- ///
STATIC_ASSERT((U64)SimpleGridHashManager::kMaxRegisteredCount <= (U64)SimpleGridHash::kMaxMembers);

bool g_sghmEnableLogging = false;

#if !FINAL_BUILD
	#define SGH_ALWAYS_ASSERT(cond) AI_ASSERT(cond)
	#define SGH_ALWAYS_ASSERTF(cond, args) AI_ASSERTF(cond, args)
#else
	#define SGH_ALWAYS_ASSERT(cond)
	#define SGH_ALWAYS_ASSERTF(cond, args)
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
// SimpleGridHashManager
/// --------------------------------------------------------------------------------------------------------------- ///

//static
SimpleGridHashManager SimpleGridHashManager::s_singleton;
//static
NdRwAtomicLock64 SimpleGridHashManager::s_lock;

/// --------------------------------------------------------------------------------------------------------------- ///
SimpleGridHashManager::SimpleGridHashManager()
{
	m_pGridHash = nullptr;

	ResetNoLock();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleGridHashManager::Init()
{
	SGH_ALWAYS_ASSERT(m_pGridHash == nullptr);

	m_pGridHash = NDI_NEW SimpleGridHash;
	SGH_ALWAYS_ASSERTF(m_pGridHash, ("SimpleGridHashManager::Init(): Something is wrong with the memory map."));

	// in the worst case, each member is in a cell by itself, so that's the max number of cells we need
	m_pGridHash->InitNoLock(kMaxRegisteredCount);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleGridHashManager::Reset()
{
	AtomicLockJanitorWrite jj(&s_lock, FILE_LINE_FUNC);
	ResetNoLock();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleGridHashManager::ResetNoLock()
{
	for (I32F i = 0; i < kMaxRegisteredCount; ++i)
	{
		m_aRegisteredObject[i].m_info.m_hGo = nullptr;
		m_aRegisteredObject[i].m_info.m_userId = INVALID_STRING_ID_64;
	}

	m_members.ClearAllBits();

	if (m_pGridHash)
	{
		m_pGridHash->RemoveAllMembersNoLock();
	}

	if (g_sghmEnableLogging)
	{
		MsgUser2("SimpleGridHashManager: reset\n");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleGridHashManager::Lock()
{
	s_lock.AcquireReadLock(FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SimpleGridHashManager::Unlock()
{
	s_lock.ReleaseReadLock(FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SimpleGridHashId SimpleGridHashManager::Register(NdGameObject& go, F32 radiusSoft, F32 radiusHard, FactionId factionIdOverride)
{
	AtomicLockJanitorWrite jj(&s_lock, FILE_LINE_FUNC);

	SimpleGridHashId gridHashId = SimpleGridHashId::kInvalid;

	const I32F iFree = m_members.FindFirstClearBit();
	SGH_ALWAYS_ASSERTF(iFree >= 0, ("SimpleGridHashManager: too many objects registered! (max %d)", kMaxRegisteredCount));

	if (iFree >= 0)
	{
		SGH_ALWAYS_ASSERT(iFree < kMaxRegisteredCount);

		RegisteredObject& registeredObject = m_aRegisteredObject[iFree];
		SGH_ALWAYS_ASSERT(!registeredObject.m_info.m_hGo.HandleValid());

		m_members.SetBit(iFree);
		gridHashId = SimpleGridHashId(iFree);
		registeredObject.m_info.m_hGo = &go;

		SGH_ALWAYS_ASSERT(m_pGridHash);
		registeredObject.m_gridHashCoord = m_pGridHash->UpdateMemberPositionNoLockWs(gridHashId, nullptr, go.GetTranslation());

		if (factionIdOverride != FactionId::kInvalid)
			registeredObject.m_factionId = factionIdOverride;
		else
			registeredObject.m_factionId = go.GetFactionId();

		registeredObject.m_info.m_userId = go.GetUserId();
		registeredObject.m_info.m_posWs = go.GetTranslation();
		registeredObject.m_info.m_velWs = go.GetVelocityWs();
		registeredObject.m_info.m_radiusSoft = radiusSoft;
		registeredObject.m_info.m_radiusHard = radiusHard;

		if (g_sghmEnableLogging)
		{
			MsgUser2("SimpleGridHashManager: registered @ %2d: 0x%08X: %s\n", (int)iFree, (uintptr_t)&go, go.GetName());
		}
	}

	SGH_ALWAYS_ASSERTF(gridHashId.GetRaw() >= 0 && gridHashId.GetRaw() < kMaxRegisteredCount, ("SimpleGridHashManager: BOGUS grid hash id generated!"));
	return gridHashId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SimpleGridHashManager::Unregister(NdGameObject& go, SimpleGridHashId gridHashId)
{
	AtomicLockJanitorWrite jj(&s_lock, FILE_LINE_FUNC);

	I32F i = gridHashId.GetRaw();
	SGH_ALWAYS_ASSERT(gridHashId != SimpleGridHashId::kInvalid);
	SGH_ALWAYS_ASSERT(i >= 0 && i < kMaxRegisteredCount);

	RegisteredObject& registeredObject = m_aRegisteredObject[i];

	if (g_sghmEnableLogging)
	{
		MsgUser2("SimpleGridHashManager: Unregister @ %2d: 0x%08X: %s (0x%08X)\n",
				 (int)i,
				 (uintptr_t)&go,
				 go.GetName(),
				 (uintptr_t)registeredObject.m_info.m_hGo.ToProcess());
	}

	const NdGameObject* pGo = registeredObject.m_info.m_hGo.ToProcess();
	if (pGo != nullptr) // can be nullptr if Reset() gets called before all characters have had a chance to unregister!
	{
		NAV_ASSERTF(pGo == &go,
					("Unregistering 0x%08X (%s) but we have 0x%08X (%s) in its slot!",
					 (uintptr_t)&go,
					 go.GetName(),
					 (uintptr_t)pGo,
					 pGo ? pGo->GetName() : "<null>"));

		SGH_ALWAYS_ASSERT(m_members.IsBitSet(i));

		if (pGo == &go)
		{
			SGH_ALWAYS_ASSERT(m_pGridHash);
			m_pGridHash->RemoveMemberNoLock(gridHashId, registeredObject.m_gridHashCoord);

			m_members.ClearBit(i);
			registeredObject.m_info.m_hGo = nullptr;
			registeredObject.m_info.m_userId = INVALID_STRING_ID_64;

			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MutableNdGameObjectHandle SimpleGridHashManager::GetHandle(U32F index) const
{
	AtomicLockJanitorRead jj(&s_lock, FILE_LINE_FUNC);
	return GetHandleNoLock(index);
}

/// --------------------------------------------------------------------------------------------------------------- ///
MutableNdGameObjectHandle SimpleGridHashManager::GetHandleNoLock(U32F index) const
{
	SGH_ALWAYS_ASSERT(index < kMaxRegisteredCount);
	SGH_ALWAYS_ASSERT(m_members.IsBitSet(index)); // TO FIX // NO not always true -- it seems some SNPCs can unregister from the grid hash but still be in the SNPC manager's bit array!?!
	//
	// e.g. callstack in which this was the case:
	// big4.elf!SimpleGridHashManager::GetHandleNoLock(U32F index) Line 201 + 166 bytes	C++
	// big4.elf!SimpleGridHashManager::GetHandle(U32F index) Line 197 + 12 bytes	C++
	// big4.elf!SimpleNpcManager::GetHandle(U32F index) Line 521 + 20 bytes	C++
	// big4.elf!SimpleNpcManager::Iterator::Advance() Line 95 + 16 bytes	C++
	// big4.elf!SimpleNpcManager::SetupRaycastJob() Line 905 + 360 bytes	C++
	// big4.elf!SimpleNpcManager::PreRenderUpdate() Line 1025	C++
	// big4.elf!SimpleNpcManagerPreRenderUpdateJob(uintptr_t jobParam) Line 2553	C++

	return m_aRegisteredObject[index].m_info.m_hGo;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject* SimpleGridHashManager::Get(U32F index) const
{
	AtomicLockJanitorRead jj(&s_lock, FILE_LINE_FUNC);
	return GetNoLock(index);
}
/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject* SimpleGridHashManager::GetNoLock(U32F index) const
{
	SGH_ALWAYS_ASSERT(index < kMaxRegisteredCount);
	SGH_ALWAYS_ASSERT(m_members.IsBitSet(index));
	return m_aRegisteredObject[index].m_info.m_hGo.ToMutableProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject* SimpleGridHashManager::Get(SimpleGridHashId hashId) const
{
	AtomicLockJanitorRead jj(&s_lock, FILE_LINE_FUNC);

	if (hashId != SimpleGridHashId::kInvalid)
	{
		I32F index = hashId.GetRaw();
		SGH_ALWAYS_ASSERT(m_members.IsBitSet(index));
		SGH_ALWAYS_ASSERT(index >= 0 && index < kMaxRegisteredCount);
		return m_aRegisteredObject[index].m_info.m_hGo.ToMutableProcess();
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const SimpleGridHashManager::RegisteredObject* SimpleGridHashManager::GetRecord(U32F index) const
{
	AtomicLockJanitorRead jj(&s_lock, FILE_LINE_FUNC);

	SGH_ALWAYS_ASSERT(m_members.IsBitSet(index));
	SGH_ALWAYS_ASSERT(index < kMaxRegisteredCount);
	return &m_aRegisteredObject[index];
}

/// --------------------------------------------------------------------------------------------------------------- ///
const SimpleGridHashManager::RegisteredObject* SimpleGridHashManager::GetRecordNoLock(U32F index) const
{
	SGH_ALWAYS_ASSERT(m_members.IsBitSet(index));
	SGH_ALWAYS_ASSERT(index < kMaxRegisteredCount);
	return &m_aRegisteredObject[index];
}

/// --------------------------------------------------------------------------------------------------------------- ///
const SimpleGridHashManager::RegisteredObject* SimpleGridHashManager::GetRecord(SimpleGridHashId hashId) const
{
	AtomicLockJanitorRead jj(&s_lock, FILE_LINE_FUNC);
	return GetRecordNoLock(hashId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const SimpleGridHashManager::RegisteredObject* SimpleGridHashManager::GetRecordNoLock(SimpleGridHashId hashId) const
{
	if (hashId != SimpleGridHashId::kInvalid)
	{
		I32F index = hashId.GetRaw();
		SGH_ALWAYS_ASSERT(m_members.IsBitSet(index));
		SGH_ALWAYS_ASSERT(index >= 0 && index < kMaxRegisteredCount);
		return &m_aRegisteredObject[index];
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SimpleGridHashManager::GetGridHashCoord(SimpleGridHashId gridHashId, SimpleGridHash::Coord* pGridHashCoord)
{
	AtomicLockJanitorRead jj(&s_lock, FILE_LINE_FUNC);

	I32F i = gridHashId.GetRaw();
	if (i >= 0)
	{
		SGH_ALWAYS_ASSERT(i < kMaxRegisteredCount);
		SGH_ALWAYS_ASSERT(m_members.IsBitSet(i));
		SGH_ALWAYS_ASSERT(m_aRegisteredObject[i].m_info.m_hGo.HandleValid());
		if (pGridHashCoord)
			*pGridHashCoord = m_aRegisteredObject[i].m_gridHashCoord;
		return true;
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SimpleGridHashManager::UpdateGridHash(NdGameObject& self,
										   SimpleGridHashId gridHashId,
										   F32 radiusSoft,
										   F32 radiusHard)
{
	AtomicLockJanitorWrite jj(&s_lock, FILE_LINE_FUNC);

	I32F i = gridHashId.GetRaw();
	if (i >= 0 && m_aRegisteredObject[i].m_info.m_hGo.ToProcess() == &self)
	{
		SGH_ALWAYS_ASSERT(m_pGridHash);

		RegisteredObject& registeredObject = m_aRegisteredObject[i];

		registeredObject.m_gridHashCoord = m_pGridHash->UpdateMemberPositionNoLockWs(gridHashId,
																					 &registeredObject.m_gridHashCoord,
																					 self.GetTranslation());

		SGH_ALWAYS_ASSERT(m_members.IsBitSet(i));
		SGH_ALWAYS_ASSERT(registeredObject.m_info.m_userId == self.GetUserId());
		registeredObject.m_info.m_posWs = self.GetTranslation();
		registeredObject.m_info.m_velWs = self.GetVelocityWs();
		registeredObject.m_info.m_radiusSoft = radiusSoft;
		registeredObject.m_info.m_radiusHard = radiusHard;

		return true;
	}

	return false;
}
