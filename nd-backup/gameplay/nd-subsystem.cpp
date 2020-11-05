/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nd-subsystem.h"

#include "corelib/memory/memory-map.h"

#include "ndlib/memory/relocatable-heap.h"

#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/nd-subsystem-anim-action.h"
#include "gamelib/gameplay/nd-subsystem-mgr.h"

/// --------------------------------------------------------------------------------------------------------------- ///
bool g_ndSubsystemDebugDrawTree = false;
bool g_ndSubsystemDebugDrawTreeFileLine = false;
bool g_ndSubsystemDebugDrawInAnimTree = true;
bool g_ndSubsystemDebugSubsystemHeap = false;

RelocatableHeap* NdSubsystem::s_pRelocatableHeap = nullptr;

TYPE_FACTORY_REGISTER_BASE(NdSubsystem);

/// --------------------------------------------------------------------------------------------------------------- ///
SubsystemSpawnInfo::SubsystemSpawnInfo()
	: m_type(INVALID_STRING_ID_64)
	, m_pOwner(nullptr)
	, m_pParent(nullptr)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
SubsystemSpawnInfo::SubsystemSpawnInfo(StringId64 type, NdGameObject* pOwner)
	: m_type(type)
	, m_pOwner(pOwner)
	, m_pParent(nullptr)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
SubsystemSpawnInfo::SubsystemSpawnInfo(StringId64 type, NdSubsystem* pParent)
	: m_type(type)
	, m_pOwner(pParent->GetOwnerGameObject())
	, m_pParent(pParent)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void NdSubsystem::Initialize()
{
	U32 size = Memory::GetSize(ALLOCATION_SUBSYSTEM_HEAP);
	U8* pMem = NDI_NEW (kAllocAppCpu, kAlign128) U8[size];

	HeapAllocator allocInst;
	allocInst.InitAsHeap(pMem, size);
	Memory::PushAllocator(&allocInst, FILE_LINE_FUNC);
	s_pRelocatableHeap = NDI_NEW RelocatableHeap;
	s_pRelocatableHeap->Init(allocInst.GetHeapArena().GetCurrent(), allocInst.GetHeapArena().GetFreeSize(), 1024, 1, 0);
	Memory::PopAllocator();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
U32 NdSubsystem::GenerateUniqueId()
{
	static NdAtomic32 s_globalPid(0);

	U32 uniqueId = (U32)s_globalPid.Add(1);
	if (uniqueId == kInvalidSubsystemId)
		uniqueId = (U32)s_globalPid.Add(1);

	return uniqueId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
NdSubsystem* NdSubsystem::Create(Alloc allocType,
								 const SubsystemSpawnInfo& spawnInfo,
								 const char* sourceFile,
								 U32F sourceLine,
								 const char* sourceFunc)
{
	//MsgOut("NdSubsystem::Create (%d) - %s, %s\n", GetCurrentFrameNumber(), spawnInfo.m_pOwner->GetName(), DevKitOnly_StringIdToString(spawnInfo.m_type));
	if (spawnInfo.m_pSpawnErrorOut)
	{
		*spawnInfo.m_pSpawnErrorOut = Err::kOK;
	}

#ifdef RBRONER
	SYSTEM_ASSERTF(spawnInfo.m_pOwner, ("Subsystem owner not specified.\n"));
#endif

	if (spawnInfo.m_pOwner == nullptr)
		return nullptr;

	NdSubsystemMgr* pSsMgr = spawnInfo.m_pOwner->GetSubsystemMgr();
	if (pSsMgr == nullptr)
		return nullptr;


	HeapAllocator subsystemInitHeap;
	RelocatableHeapRecord* pRec = nullptr;
	if (allocType == Alloc::kSubsystemHeap)
	{
		pRec = s_pRelocatableHeap->AllocBlock(64 * 1024);
		if (pRec == nullptr)
			DumpSubsystemHeap();
		SYSTEM_ASSERTF(pRec, ("Out of memory in Subsystem Heap.\n"));

		subsystemInitHeap.InitAsHeap(pRec->m_pMutableMem, pRec->m_allocatedSize);
		Memory::PushAllocator(&subsystemInitHeap, FILE_LINE_FUNC);
	}
	else
	{
		// If we're allocating from the Init of another Process/Subsystem, we need to acquire their relocatable heap rec
		if (spawnInfo.m_pParent)
			pRec = spawnInfo.m_pParent->GetHeapRecord();
		else if (spawnInfo.m_pOwner)
			pRec = spawnInfo.m_pOwner->GetHeapRecord();

		SYSTEM_ASSERTF(pRec, ("Heap record not found.\n"));
	}

	const TypeFactory::Record* pType = g_typeFactory.GetRecord(spawnInfo.m_type);
	SYSTEM_ASSERTF(pType, ("Subsystem not found: %s.\n", DevKitOnly_StringIdToString(spawnInfo.m_type)));

	NdSubsystem* pSys = (NdSubsystem*)pType->Create(nullptr, nullptr);
	SYSTEM_ASSERTF(pSys, ("Subsystem not found: %s.\n", DevKitOnly_StringIdToString(spawnInfo.m_type)));

	pSys->m_debugFile = sourceFile;
	pSys->m_debugLine = sourceLine;
	pSys->m_debugFunc = sourceFunc;

	// Setup the heap record first, so handles are assigned properly (e.g. in SetParent() and AddSubsystem())
	pSys->SetHeapRecord(pRec);

	if (allocType == Alloc::kSubsystemHeap)
	{
		pRec->m_pItem = pSys;
	}

	// Set NdSubsystem data before Init, so it's valid during Init
	pSys->SetOwner(spawnInfo.m_pOwner);
	pSys->SetParent(spawnInfo.m_pParent);
	pSys->SetAlloc(allocType);
	pSys->SetType(spawnInfo.m_type);
	pSys->m_pType = pType;

	if (FALSE_IN_FINAL_BUILD(Memory::IsDebugMemoryAvailable()))
	{
		// Allocate from debug mem so it doesn't relocate, since it's used for profiling info
		int len = snprintf(nullptr, 0, "%s <%d>", DevKitOnly_StringIdToString(spawnInfo.m_type), pSys->GetSubsystemId());
		char* nameStr = NDI_NEW(kAllocDebug) char[len + 1];
		snprintf(nameStr, len + 1, "%s <%d>", DevKitOnly_StringIdToString(spawnInfo.m_type), pSys->GetSubsystemId());
		pSys->SetDebugName(nameStr);
	}

	// Add to Mgr before Init, so children subsystems created in Init are added after this one
	SYSTEM_ASSERTF(pSsMgr, ("Subsystem Mgr not found.\n"));

	bool addSubsystemResult = pSsMgr->AddSubsystem(pSys);
	SYSTEM_ASSERTF(addSubsystemResult, ("Failed to add subsystem.\n"));

	const Err res = pSys->Init(spawnInfo);

	if (spawnInfo.m_pSpawnErrorOut)
	{
		*spawnInfo.m_pSpawnErrorOut = res;
	}

	if (res.Failed())
	{
		pSys->Kill();
		pSys = nullptr;
	}
	else if (pSys->IsAnimAction())
	{
		GAMEPLAY_ASSERTF(static_cast<NdSubsystemAnimAction*>(pSys)->GetActionState()
								!= NdSubsystemAnimAction::ActionState::kInvalid,
							("NdSubsystemAnimAction initialized without being bound to an Anim State\n"));
	}

	if (allocType == Alloc::kSubsystemHeap)
	{
		if (pRec)
			s_pRelocatableHeap->ShrinkBlock(pRec, subsystemInitHeap.GetHeapArena().GetAllocatedSize());
		Memory::PopAllocator();
	}

	return pSys;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void NdSubsystem::Destroy(NdSubsystem* pSys)
{
	RelocatableHeapRecord* pRec = pSys->GetHeapRecord();
	HeapAllocator subsystemInitHeap;

	bool usingSubsystemHeap = (pSys->GetAlloc() == Alloc::kSubsystemHeap);
	if (usingSubsystemHeap)
	{
		subsystemInitHeap.InitAsHeap(pRec->m_pMutableMem, pRec->m_allocatedSize);
		Memory::PushAllocator(&subsystemInitHeap, FILE_LINE_FUNC);
	}

	NDI_DELETE pSys;

	if (usingSubsystemHeap)
	{
		Memory::PopAllocator();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void NdSubsystem::Free(NdSubsystem* pSys)
{
	if (pSys->GetAlloc() == Alloc::kSubsystemHeap)
	{
		RelocatableHeapRecord* pRec = pSys->GetHeapRecord();
		s_pRelocatableHeap->FreeBlock(pRec);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
RelocatableHeap* NdSubsystem::GetRelocatableHeap()
{
	return s_pRelocatableHeap;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void NdSubsystem::CompactHeap()
{
	s_pRelocatableHeap->Update();

	if (g_ndSubsystemDebugSubsystemHeap)
		NdSubsystem::DebugPrintHeap();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void NdSubsystem::DebugPrintHeap()
{
	s_pRelocatableHeap->Validate();
	MsgCon("Subsystem Heap Active Record Count: %d\n", s_pRelocatableHeap->GetActiveRecordCount());
	MsgCon("Subsystem Heap Free Mem: %d\n", s_pRelocatableHeap->GetTotalFreeMemory());
	MsgCon("Subsystem Heap Avail Mem: %d\n", s_pRelocatableHeap->GetTotalAvailMemory());
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
const char* NdSubsystem::GetUpdatePassText(UpdatePass updatePass)
{
	switch (updatePass)
	{
	case kPreProcessUpdate:			return "PreProcessUpdate";
	case kUpdate:					return "Update";
	case kPostAnimUpdate:			return "PostAnimUpdate";
	case kPostRootLocatorUpdate:	return "PostRootLocatorUpdate";
	case kPostAnimBlending:			return "PostAnimBlending";
	case kPostJointUpdate:			return "PostJointUpdate";
	}

	return "Unknown";
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystem::NdSubsystem()
{
	m_subsystemId = GenerateUniqueId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystem::~NdSubsystem()
{
	// Unlink from parent
	SetParent(nullptr);

	if (m_debugName)
	{
		NDI_DELETE [] m_debugName;
		m_debugName = nullptr;
	}

	if (m_completionCounter)
	{
		WaitForCounterAndFree(m_completionCounter);
		m_completionCounter = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdSubsystem::Init(const SubsystemSpawnInfo& info)
{
	GAMEPLAY_ASSERT(BoundOwnerTypeValidate());

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystem::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_debugName, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystem::RelocateOwner(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pOwner, deltaPos, lowerBound, upperBound);
	m_dependencySys.Relocate(deltaPos, lowerBound, upperBound);
	GAMEPLAY_ASSERT(m_pOwner != nullptr);
}
/// --------------------------------------------------------------------------------------------------------------- ///
const NdSubsystemMgr* NdSubsystem::GetSubsystemMgr() const
{
	return GetOwnerGameObject()->GetSubsystemMgr();
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemMgr* NdSubsystem::GetSubsystemMgr()
{
	return GetOwnerGameObject()->GetSubsystemMgr();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystem::SetParent(NdSubsystem* pParent)
{
	NdSubsystem* pParentCurr = m_hParent.ToSubsystem();
	if (pParentCurr)
	{
		NdSubsystem* pNext = m_hSiblingNext.ToSubsystem();
		NdSubsystem* pPrev = m_hSiblingPrev.ToSubsystem();

		if (this == pParentCurr->m_hChild.ToSubsystem())
			pParentCurr->m_hChild = pNext;
		if (pPrev)
		{
			pPrev->m_hSiblingNext = pNext;
			m_hSiblingPrev = nullptr;
		}
		if (pNext)
		{
			pNext->m_hSiblingPrev = pPrev;
			m_hSiblingNext = nullptr;
		}
	}
	

	m_hParent = pParent;

	// Put the new child to the head of the list
	if (pParent)
	{
		NdSubsystem* pChild = pParent->m_hChild.ToSubsystem();
		if (pChild)
			pChild->m_hSiblingPrev = this;

		m_hSiblingNext = pParent->m_hChild;
		pParent->m_hChild = this;

	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystem* NdSubsystem::GetParent()
{
	return m_hParent.ToSubsystem();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystem::SetOwner(NdGameObject* pOwner)
{
	GAMEPLAY_ASSERT(pOwner != nullptr);
	m_pOwner = pOwner;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Character* NdSubsystem::GetOwnerCharacter()
{
	if (!m_pOwner)
	{
		DumpSubsystemHeap();
		GAMEPLAY_ASSERT(false);
	}
	return Character::FromProcess(m_pOwner);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Character* NdSubsystem::GetOwnerCharacter() const
{
	if (!m_pOwner)
		DumpSubsystemHeap();
	return Character::FromProcess(m_pOwner);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystem::Kill()
{
	if (m_subsystemState == State::kKilled)
		return;

	//MsgOut("NdSubsystem::Kill (%d) - %s, %s\n", GetCurrentFrameNumber(), m_pOwner->GetName(), DevKitOnly_StringIdToString(m_subsystemType));
	m_subsystemState = State::kKilled;
	OnKilled();

	NdSubsystem* pChild = m_hChild.ToSubsystem();
	while (pChild)
	{
		NdSubsystem* pNext = pChild->m_hSiblingNext.ToSubsystem();
		pChild->Kill();
		pChild = pNext;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystem::UpdateType NdSubsystem::SubsystemUpdateType(UpdatePass updatePass)
{
	switch (updatePass)
	{
	case kPreProcessUpdate:			return SubsystemPreProcessUpdateMacroType();
	case kUpdate:					return SubsystemUpdateMacroType();
	case kPostAnimUpdate:			return SubsystemPostAnimUpdateMacroType();
	case kPostRootLocatorUpdate:	return SubsystemPostRootLocatorUpdateMacroType();
	case kPostAnimBlending:			return SubsystemPostAnimBlendingMacroType();
	case kPostJointUpdate:			return SubsystemPostJointUpdateMacroType();
	};

	return UpdateType::kNone;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystem::SubsystemUpdate(UpdatePass updatePass)
{
	switch (updatePass)
	{
	case kPreProcessUpdate:
		SubsystemPreProcessUpdateMacro();
		return;
	case kUpdate:
		SubsystemUpdateMacro();
		return;
	case kPostAnimUpdate:
		SubsystemPostAnimUpdateMacro();
		return;
	case kPostRootLocatorUpdate:
		SubsystemPostRootLocatorUpdateMacro();
		return;
	case kPostAnimBlending:
		SubsystemPostAnimBlendingMacro();
		return;
	case kPostJointUpdate:
		SubsystemPostJointUpdateMacro();
		return;
	};
}

/// --------------------------------------------------------------------------------------------------------------- ///
const StringId64* NdSubsystem::SubsystemUpdateGetDependencies(UpdatePass updatePass)
{
	switch (updatePass)
	{
	case kPreProcessUpdate:			return SubsystemPreProcessUpdateMacroGetDependencies();
	case kUpdate:					return SubsystemUpdateMacroGetDependencies();
	case kPostAnimUpdate:			return SubsystemPostAnimUpdateMacroGetDependencies();
	case kPostRootLocatorUpdate:	return SubsystemPostRootLocatorUpdateMacroGetDependencies();
	case kPostAnimBlending:			return SubsystemPostAnimBlendingMacroGetDependencies();
	case kPostJointUpdate:			return SubsystemPostJointUpdateMacroGetDependencies();
	};

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int NdSubsystem::SubsystemUpdateGetDependencyCount(UpdatePass updatePass)
{
	switch (updatePass)
	{
	case kPreProcessUpdate:			return SubsystemPreProcessUpdateMacroGetDependencyCount();
	case kUpdate:					return SubsystemUpdateMacroGetDependencyCount();
	case kPostAnimUpdate:			return SubsystemPostAnimUpdateMacroGetDependencyCount();
	case kPostRootLocatorUpdate:	return SubsystemPostRootLocatorUpdateMacroGetDependencyCount();
	case kPostAnimBlending:			return SubsystemPostAnimBlendingMacroGetDependencyCount();
	case kPostJointUpdate:			return SubsystemPostJointUpdateMacroGetDependencyCount();
	};

	return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdSubsystem::SubsystemUpdateCheckDependency(UpdatePass updatePass, StringId64 subsystemType)
{
	const StringId64* pDeps = SubsystemUpdateGetDependencies(updatePass);
	int depCount = SubsystemUpdateGetDependencyCount(updatePass);
	for (int iDep = 0; iDep < depCount; iDep++)
	{
		if (subsystemType == pDeps[iDep])
		{
			return true;
		}
	}

	return false;
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystem::DumpSubsystemHeap()
{
	MsgOut("\nSubsystem Relocatable Heap\n");
	MsgOut("========================================\n");
	s_pRelocatableHeap->DebugPrintRecordList(*GetMsgOutput(kMsgOut));
	MsgOut("========================================\n\n");
}
