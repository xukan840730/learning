/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/linked-list.h"

#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/util/common.h"

#define ND_SUBSYSTEM_H
#include "gamelib/gameplay/nd-subsystem-handle.h"
#undef ND_SUBSYSTEM_H

//#define ND_SUBSYSTEM_VALIDATION

/// --------------------------------------------------------------------------------------------------------------- ///
class Character;
class NdGameObject;
class NdSubsystem;
class NdSubsystemMgr;
class RelocatableHeap;
class Event;
struct FootPlantParams;

extern bool g_ndSubsystemDebugDrawTree;
extern bool g_ndSubsystemDebugDrawTreeFileLine;
extern bool g_ndSubsystemDebugDrawInAnimTree;
extern bool g_ndSubsystemDebugSubsystemHeap;

/// --------------------------------------------------------------------------------------------------------------- ///
struct SubsystemSpawnInfo
{
	SubsystemSpawnInfo();
	SubsystemSpawnInfo(StringId64 type, NdGameObject* pOwner = nullptr);
	SubsystemSpawnInfo(StringId64 type, NdSubsystem* pParent);

	StringId64 m_type		= INVALID_STRING_ID_64;
	NdGameObject* m_pOwner	= nullptr;
	NdSubsystem* m_pParent	= nullptr;
	const void* m_pUserData = nullptr;
	Err* m_pSpawnErrorOut		= nullptr;

	U32 m_subsystemControllerId = FadeToStateParams::kInvalidSubsystemId;
	StateChangeRequest::ID m_animActionRequestId = StateChangeRequest::kInvalidId;
	bool m_spawnedFromStateInfo = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
#define SUBSYSTEM_UPDATE_DECLARE_BASE_FUNCTIONS(UpdateFunc)                                                            \
	virtual UpdateType Subsystem##UpdateFunc##MacroType() { return UpdateType::kNone; }                                \
	virtual const StringId64* Subsystem##UpdateFunc##MacroGetDependencies() { return nullptr; }                        \
	virtual int Subsystem##UpdateFunc##MacroGetDependencyCount() { return 0; }                                         \
	virtual void Subsystem##UpdateFunc##Macro() {}

#define SUBSYSTEM_BIND(Type)                                                                                           \
	Type* GetOwner##Type() { return static_cast<Type*>(GetOwnerGameObject()); }                                        \
	const Type* GetOwner##Type() const { return static_cast<const Type*>(GetOwnerGameObject()); }                      \
	virtual bool BoundOwnerTypeValidate() const override { return GetOwnerGameObject()->IsKindOf(g_type_##Type); }

#define SUBSYSTEM_BIND_OWNER_PARENT(OwnerType, ParentType)                                                             \
	typedef ParentType ParentClass;                                                                                    \
	SUBSYSTEM_BIND(OwnerType);

/// --------------------------------------------------------------------------------------------------------------- ///
class NdSubsystem : public Relocatable
{
	typedef Relocatable ParentClass;

public:
	friend class NdSubsystemMgr;

	enum class Alloc
	{
		kSubsystemHeap,		// Allocate from the Subsystem relocatable heap
		kParentInit			// Allocate from the parent Process/Subsystem Init() func
	};

	enum class State
	{
		kActive,
		kInactive,
		kKilled,
	};

	static const char* GetStateStr(State s)
	{
		switch (s)
		{
		case State::kActive: return "Active";
		case State::kInactive: return "Inactive";
		case State::kKilled: return "Killed";
		}

		return "???";
	}

	enum class UpdateType
	{
		kNone,
		kSynchronous,
		kAsynchronous,
	};

	enum UpdatePass
	{
		kPreProcessUpdate,
		kUpdate,
		kPostAnimUpdate,
		kPostRootLocatorUpdate,
		kPostAnimBlending,
		kPostJointUpdate,
		kUpdatePassCount
	};

private:
	typedef DoublyLinkedList<NdSubsystemHandle>::Pool		SubsystemListPool;
	typedef DoublyLinkedList<NdSubsystemHandle>				SubsystemList;

public:
	static const U32 kInvalidSubsystemId = FadeToStateParams::kInvalidSubsystemId;

	static void Initialize();
	static U32 GenerateUniqueId();

	static NdSubsystem* Create(Alloc allocType,
							   const SubsystemSpawnInfo& spawnInfo,
							   const char* sourceFile,
							   U32F sourceLine,
							   const char* sourceFunc);

	static RelocatableHeap* GetRelocatableHeap();
	static void CompactHeap();
	static void DebugPrintHeap();

	static const char* GetUpdatePassText(UpdatePass updatePass);

private:
	static void Destroy(NdSubsystem* pSys);
	static void Free(NdSubsystem* pSys);

public:
	NdSubsystem();
	~NdSubsystem() override;
	virtual Err Init(const SubsystemSpawnInfo& info);
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void EnterNewParentSpace(const Transform& matOldToNew,
									 const Locator& oldParentSpace,
									 const Locator& newParentSpace)
	{
	}
	virtual void RelocateOwner(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);
	virtual void OnKilled() {}

	State GetSubsystemState() const { return m_subsystemState; }
	U32 GetSubsystemId() const { return m_subsystemId; }

	const NdSubsystemMgr* GetSubsystemMgr() const;
	NdSubsystemMgr* GetSubsystemMgr();

	void SetParent(NdSubsystem* pParent);
	NdSubsystem* GetParent();

	virtual bool BoundOwnerTypeValidate() const {return true;}
	void SetOwner(NdGameObject* pOwner);

	NdGameObject* GetOwnerGameObject()
	{
		if (!m_pOwner)
		{
			DumpSubsystemHeap();
			GAMEPLAY_ASSERT(false);
		}
		return m_pOwner;
	}
	const NdGameObject* GetOwnerGameObject() const
	{
		if (!m_pOwner)
		{
			DumpSubsystemHeap();
			GAMEPLAY_ASSERT(false);
		}
		return m_pOwner;
	}

	Character* GetOwnerCharacter();
	const Character* GetOwnerCharacter() const;

	void SetType(StringId64 type) { m_subsystemType = type; }
	StringId64 GetType() const { return m_subsystemType; }

	bool IsKindOf(const TypeFactory::Record& parentType) const	{ return m_pType->IsKindOf(parentType); }
	bool IsKindOf(StringId64 typeId) const						{ return m_pType->IsKindOf(typeId); } // slower version

	void SetAlloc(Alloc alloc) { m_alloc = alloc; }
	Alloc GetAlloc() const { return m_alloc; }

	virtual bool IsAnimAction() { return false; }

	void SetActive() { m_subsystemState = State::kActive; }
	void SetInactive() { m_subsystemState = State::kInactive; }
	void Kill();
	bool IsAlive() { return m_subsystemState != State::kKilled; }
	bool IsKilled() { return m_subsystemState == State::kKilled; }

	virtual void EventHandler(Event& event) {}

	virtual const char* GetName() const override { return m_debugName ? m_debugName : "<Unknown NdSubsystem>"; }
	virtual bool GetAnimControlDebugText(const AnimStateInstance* pInstance, IStringBuilder* pText) const { return false; }
	virtual bool GetQuickDebugText(IStringBuilder* pText) const { return false; }
	virtual Color GetQuickDebugColor() const { return kColorWhite; }

	virtual RelocatableHeapRecord* GetHeapRecord() const override { return m_pHeapRec; }
	void SetHeapRecord(RelocatableHeapRecord* pRec) { m_pHeapRec = pRec; }

	virtual void FillFootPlantParams(const AnimStateInstance* pInstance, FootPlantParams* pParamsOut) const {}
	virtual float GetGroundAdjustFactor(const AnimStateInstance* pInstance, float desired) const { return desired; }
	virtual float GetNoAdjustToNavFactor(const AnimStateInstance* pInstance, float desired) const { return desired; }
	virtual float GetLegIkEnabledFactor(const AnimStateInstance* pInstance, float desired) const { return desired; }

	static void DumpSubsystemHeap();

protected:
	SUBSYSTEM_UPDATE_DECLARE_BASE_FUNCTIONS(PreProcessUpdate);
	SUBSYSTEM_UPDATE_DECLARE_BASE_FUNCTIONS(Update);
	SUBSYSTEM_UPDATE_DECLARE_BASE_FUNCTIONS(PostAnimUpdate);
	SUBSYSTEM_UPDATE_DECLARE_BASE_FUNCTIONS(PostRootLocatorUpdate);
	SUBSYSTEM_UPDATE_DECLARE_BASE_FUNCTIONS(PostAnimBlending);
	SUBSYSTEM_UPDATE_DECLARE_BASE_FUNCTIONS(PostJointUpdate);

	UpdateType SubsystemUpdateType(UpdatePass updatePass);
	void SubsystemUpdate(UpdatePass updatePass);

	const StringId64* SubsystemUpdateGetDependencies(UpdatePass updatePass);
	int SubsystemUpdateGetDependencyCount(UpdatePass updatePass);
	bool SubsystemUpdateCheckDependency(UpdatePass updatePass, StringId64 subsystemType);

private:
	void SetDebugName(const char* name) { m_debugName = name; }

	// Keep direct pointer to the owning Process, because we may need to access the Process while it's
	// being killed, and the ProcessHandle is invalid. Owner updates pointer when it relocates.
	NdGameObject*		m_pOwner;
	StringId64			m_subsystemType = INVALID_STRING_ID_64;

	const char*			m_debugName = nullptr;
	const char*			m_debugFile = nullptr;
	int					m_debugLine = 0;
	const char*			m_debugFunc = nullptr;

	NdSubsystemHandle	m_hParent;
	NdSubsystemHandle	m_hChild;
	NdSubsystemHandle	m_hSiblingNext;
	NdSubsystemHandle	m_hSiblingPrev;

	U32					m_subsystemId = kInvalidSubsystemId;
	Alloc				m_alloc = Alloc::kSubsystemHeap;
	State				m_subsystemState = State::kActive;

	// Data used in async update
	UpdatePass				m_currUpdatePass;
	ndjob::CounterHandle	m_completionCounter;
	SubsystemList			m_dependencySys;

	const TypeFactory::Record*	m_pType = nullptr;
	RelocatableHeapRecord*		m_pHeapRec = nullptr;

	bool					m_debugPrintMark = false;

	static RelocatableHeap* s_pRelocatableHeap;
};

/// --------------------------------------------------------------------------------------------------------------- ///
#define SUBSYSTEM_UPDATE_DEPENDENCIES(UpdateFunc, ...)                                                                 \
	virtual const StringId64* Subsystem##UpdateFunc##MacroGetDependencies() override                                   \
	{                                                                                                                  \
		static const StringId64 s_dependencies[] = { __VA_ARGS__ };                                                    \
		return s_dependencies;                                                                                         \
	}                                                                                                                  \
	virtual int Subsystem##UpdateFunc##MacroGetDependencyCount() override                                              \
	{                                                                                                                  \
		static const StringId64 s_dependencies[] = { __VA_ARGS__ };                                                    \
		return SIZEOF_ARRAY(s_dependencies);                                                                           \
	}

/// --------------------------------------------------------------------------------------------------------------- ///
#define SUBSYSTEM_UPDATE(UpdateFunc)                                                                                   \
	virtual UpdateType Subsystem##UpdateFunc##MacroType() override { return UpdateType::kSynchronous; }                \
	virtual void Subsystem##UpdateFunc##Macro() override

#define SUBSYSTEM_UPDATE_ASYNC(UpdateFunc)                                                                             \
	virtual UpdateType Subsystem##UpdateFunc##MacroType() override                                                     \
	{                                                                                                                  \
		GAMEPLAY_ASSERT(ParentClass::Subsystem##UpdateFunc##MacroType() != UpdateType::kSynchronous);                  \
		return UpdateType::kAsynchronous;                                                                              \
	}                                                                                                                  \
	virtual void Subsystem##UpdateFunc##Macro() override

#define SUBSYSTEM_UPDATE_DEF(Class, UpdateFunc) void Class::Subsystem##UpdateFunc##Macro()

#define SUBSYSTEM_PARENT_UPDATE(UpdateFunc) ParentClass::Subsystem##UpdateFunc##Macro()

#define ND_SUBSYSTEM_H
#include "gamelib/gameplay/nd-subsystem-handle.inl"
#undef ND_SUBSYSTEM_H
