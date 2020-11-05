/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/linked-list.h"
#include "corelib/system/read-write-atomic-lock.h"
#include "corelib/system/synchronized.h"

#include "ndlib/anim/anim-instance.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"

#include "gamelib/anim/subsystem-ik-node.h"
#include "gamelib/gameplay/nd-subsystem-anim-action.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NdGameObject;
class NdSubsystem;

namespace DC 
{
	struct AnimState;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class NdSubsystemMgr
{
private:
	struct AnimStateInstanceBinding;

	typedef NdSubsystem::SubsystemListPool	SubsystemListPool;
	typedef NdSubsystem::SubsystemList		SubsystemList;

	NdGameObject*				m_pOwner = nullptr;
	
	// Master list of all subsystems, in order of being added
	struct SubsystemMasterList
	{
		NdSubsystemHandle*			m_pSubsystems = nullptr;
		int							m_numSubsystems = 0;
		int							m_maxSubsystems = 0;
	};
	Synchronized<SubsystemMasterList, NdRwAtomicLock64> m_subsystemMasterList;	

	struct SubsystemAnimBindingList
	{
		AnimStateInstanceBinding*	m_pInstanceBindings;
		int							m_numInstanceBindings = 0;
		int							m_maxInstanceBindings = 0;
	};
	Synchronized<SubsystemAnimBindingList, NdRwAtomicLock64> m_subsystemAnimBindingList;

	ndjob::CounterHandle		m_subsystemUpdateGlobalCounter;

	SubsystemListPool			m_subsystemListPool;	// Syncronized internally

	struct SubsystemUpdateLists
	{
		SubsystemList				m_subsystemList[NdSubsystem::kUpdatePassCount];
	};
	Synchronized<SubsystemUpdateLists, NdRwAtomicLock64> m_subsystemUpdateLists;

	Synchronized<SubsystemList, NdRwAtomicLock64> m_subsystemAddList;
	Synchronized<SubsystemList, NdRwAtomicLock64> m_subsystemAnimActionList;

	NdSubsystemAnimController* m_pSubsystemControllerDefault = nullptr;

	bool m_isShutdown = false;
	bool m_debugAnimTransitions = false;

public:
	typedef void InstanceActionCallback(AnimStateInstance* pInstance,
										NdSubsystemAnimAction* pAction,
										uintptr_t userData);

	~NdSubsystemMgr();

	void Init(NdGameObject* pOwner, int maxSubsystems, int maxInstanceBindings);
	void Destroy();
	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	// Kill all subsystems, and prevent adding any new ones
	void Shutdown();

	NdGameObject* GetOwner();
	const NdGameObject* GetOwner() const;

	void CleanDeadSubsystems();
	void PreProcessUpdate();
	void Update();
	void PostAnimUpdate();
	void PostRootLocatorUpdate();
	void PostAnimBlending();
	void PostJointUpdate();

	void SendEvent(Event& event) const;
	void SendEvent(const AnimStateInstance* pInst, Event& event) const;
	void SendEvent(NdSubsystem* pSys, Event& event) const;

	BoxedValue SendEvent(StringId64 msg,
						 const BoxedValue& a0 = BoxedValue(),
						 const BoxedValue& a1 = BoxedValue(),
						 const BoxedValue& a2 = BoxedValue(),
						 const BoxedValue& a3 = BoxedValue()) const;

	BoxedValue SendEvent(const AnimStateInstance* pInst, StringId64 msg,
						 const BoxedValue& a0 = BoxedValue(),
						 const BoxedValue& a1 = BoxedValue(),
						 const BoxedValue& a2 = BoxedValue(),
						 const BoxedValue& a3 = BoxedValue()) const;

	BoxedValue SendEvent(NdSubsystem* pSys, StringId64 msg,
						 const BoxedValue& a0 = BoxedValue(),
						 const BoxedValue& a1 = BoxedValue(),
						 const BoxedValue& a2 = BoxedValue(),
						 const BoxedValue& a3 = BoxedValue()) const;

	bool AddSubsystem(NdSubsystem* pSubsystem);

	NdSubsystem* FindSubsystem(StringId64 subsystemType, I32 index = 0) const;
	NdSubsystem* FindSubsystemById(U32 id) const;
	NdSubsystemAnimAction* FindSubsystemAnimActionById(U32 id) const;
	NdSubsystemAnimController* FindSubsystemAnimControllerById(U32 id) const;

	NdSubsystemAnimAction* FindSubsystemAnimAction(StringId64 subsystemType) const;
	// Active Controller as determined by the Layer
	NdSubsystemAnimController* GetActiveSubsystemController(StringId64 layerId = SID("base"),
															StringId64 type	   = INVALID_STRING_ID_64) const;
	// Controller bound to current top instance on specified layer
	NdSubsystemAnimController* GetTopSubsystemController(StringId64 layerId = SID("base"),
														 StringId64 type	= INVALID_STRING_ID_64) const;

	NdSubsystemAnimAction* FindBoundSubsystemAnimAction(const AnimStateInstance* pInst, StringId64 subsystemType) const;
	NdSubsystemAnimAction* FindBoundSubsystemAnimActionByType(const AnimStateInstance* pInst,
															  StringId64 subsystemType) const;

	int BindToAnimRequest(NdSubsystemAnimAction* pAction,
						  StateChangeRequest::ID requestId,
						  StringId64 layerId = INVALID_STRING_ID_64);
	int BindToAnimSetup(NdSubsystemAnimAction* pAction,
						StateChangeRequest::ID requestId,
						AnimStateInstance::ID instId,
						StringId64 layerId = INVALID_STRING_ID_64);
	int BindToInstance(NdSubsystemAnimAction* pAction,
						AnimStateInstance* pInst);
	int AutoBind(NdSubsystemAnimAction* pAction,
						StringId64 layerId = INVALID_STRING_ID_64);
	void InstancePrepare(StringId64 layerId,
						 StateChangeRequest::ID requestId,
						 AnimStateInstance::ID instId,
						 bool isTop,
						 const DC::AnimState* pAnimState,
						 FadeToStateParams* pParams);
	void InstanceCreate(AnimStateInstance* pInst);
	void InstanceDestroy(AnimStateInstance* pInst);
	void InstancePendingChange(StringId64 layerId, StateChangeRequest::ID requestId, StringId64 changeId, int changeType);
	bool InstanceAlignFunc(const AnimStateInstance* pInst,
						   const BoundFrame& prevAlign,
						   const BoundFrame& currAlign,
						   const Locator& apAlignDelta,
						   BoundFrame* pAlignOut,
						   bool debugDraw);
	void InstanceIkFunc(const AnimStateInstance* pInst,
						AnimPluginContext* pPluginContext,
						const AnimSnapshotNodeSubsystemIK::SubsystemIkPluginParams* pParams);

	void ForEachInstanceAction(AnimStateInstance* pInst, InstanceActionCallback* pCallback, uintptr_t userData);

	void InstanceDebugPrintFunc(const AnimStateInstance* pInst, StringId64 debugType, IStringBuilder* pText);
	void HandleTriggeredEffect(const EffectAnimInfo* pEffectAnimInfo);

	NdSubsystemAnimAction::InstanceIterator GetInstanceIterator(U32 subsystemId, int startIndex) const;
	AnimStateInstance* GetInstance(const NdSubsystemAnimAction::InstanceIterator& it) const;
	NdSubsystemAnimAction::InstanceIterator NextIterator(const NdSubsystemAnimAction::InstanceIterator& it) const;

	bool IsSubsystemBoundToAnimInstance(const AnimStateInstance* pInst, const NdSubsystemAnimAction* pAction) const;

	void EnterNewParentSpace(const Transform& matOldToNew, const Locator& oldParentSpace, const Locator& newParentSpace);

	void Validate();

	void DebugPrint() const;

	void DebugPrintTree() const;
	void DebugPrintTreeDepthFirst(NdSubsystem* pSys, int depth) const;

	void EnableAnimTransitionDebug(bool enable) { m_debugAnimTransitions = enable; }
	void DebugPrintAnimTransitionDebug() const;

private:
	CLASS_JOB_ENTRY_POINT_DEFINITION(SubsystemMgrUpdatePassAsync);
	static void DoSubsystemPreProcessUpdate(NdSubsystem* pSubsystem);
	static void DoSubsystemUpdate(NdSubsystem* pSubsystem);
	static void DoSubsystemPostAnimUpdate(NdSubsystem* pSubsystem);
	static void DoSubsystemPostRootLocatorUpdate(NdSubsystem* pSubsystem);
	static void DoSubsystemPostAnimBlending(NdSubsystem* pSubsystem);
	static void DoSubsystemPostJointUpdate(NdSubsystem* pSubsystem);
	void DoUpdatePass(NdSubsystem::UpdatePass updatePass);

	void CleanDeadSubsystemUpdateLists(NdSubsystem* pSubsystem);

	AnimStateInstanceBinding* GetInstanceBinding(SubsystemAnimBindingList& animBindingList, int* pIndex = nullptr);

	void AddSubsystemUpdateFuncs();
	void AddSubsystemUpdateFuncs(NdSubsystem* pSubsystem,
								 NdSubsystem::UpdatePass pass,
								 SubsystemUpdateLists& subsystemLists);
	SubsystemList::Iterator AddSubsystemUpdateNodeDep(NdSubsystem::UpdatePass pass,
													  SubsystemList& tempList,
													  SubsystemList::Iterator itInitial,
													  SubsystemList::Iterator itCurr,
													  int level);
};


/// --------------------------------------------------------------------------------------------------------------- ///
class NdSubsystemControllerDefault : public NdSubsystemAnimController
{
	typedef NdSubsystemAnimController ParentClass;

public:
	Err Init(const SubsystemSpawnInfo& info) override;
	virtual void RequestRefreshAnimState(const FadeToStateParams* pFadeToStateParams = nullptr,
										 bool allowStompOfInitialBlend = true) override;
};
