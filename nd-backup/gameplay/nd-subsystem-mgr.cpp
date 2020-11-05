/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nd-subsystem-mgr.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-state.h"
#include "ndlib/netbridge/mail.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/anim/anim-instance-debug.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/nd-subsystem.h"

/// --------------------------------------------------------------------------------------------------------------- ///
// NdSubsystemMgr
/// --------------------------------------------------------------------------------------------------------------- ///
struct NdSubsystemMgr::AnimStateInstanceBinding
{
	NdSubsystemAnimActionHandle		m_hAction;
	U32								m_subsystemId;
	StringId64						m_layerId = INVALID_STRING_ID_64;
	StateChangeRequest::ID			m_requestId = StateChangeRequest::kInvalidId;
	AnimStateInstance::ID			m_instId = INVALID_ANIM_INSTANCE_ID;
	bool							m_destroyed = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemMgr::~NdSubsystemMgr()
{
	Destroy();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::Init(NdGameObject* pOwner, int maxSubsystems, int maxInstanceBindings)
{
	GAMEPLAY_ASSERT(pOwner != nullptr);
	m_pOwner = pOwner;

	{
		auto pMasterList = m_subsystemMasterList.WLock();	// Lock not actually needed
		pMasterList->m_pSubsystems = NDI_NEW NdSubsystemHandle[maxSubsystems];
		pMasterList->m_numSubsystems = 0;
		pMasterList->m_maxSubsystems = maxSubsystems;
	}

	{
		auto pAnimBindingList = m_subsystemAnimBindingList.WLock();		// Lock not actually needed
		pAnimBindingList->m_pInstanceBindings = NDI_NEW AnimStateInstanceBinding[maxInstanceBindings];
		pAnimBindingList->m_numInstanceBindings = 0;
		pAnimBindingList->m_maxInstanceBindings = maxInstanceBindings;
	}

	m_subsystemListPool.Allocate(2*maxSubsystems);
	m_subsystemAddList->SetAllocator(&m_subsystemListPool);
	m_subsystemAnimActionList->SetAllocator(&m_subsystemListPool);

	{
		auto pSubsystemLists = m_subsystemUpdateLists.WLock();
		for (int iPass=0; iPass<NdSubsystem::kUpdatePassCount; iPass++)
			pSubsystemLists->m_subsystemList[iPass].SetAllocator(&m_subsystemListPool);
	}

	StringId64 defaultControllerType = pOwner->GetDefaultSubsystemControllerType();
	if (defaultControllerType == INVALID_STRING_ID_64)
		defaultControllerType = SID("NdSubsystemControllerDefault");

	m_pSubsystemControllerDefault = (NdSubsystemAnimController*)
		NdSubsystem::Create(NdSubsystem::Alloc::kParentInit,
							SubsystemSpawnInfo(defaultControllerType, pOwner),
							FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::Destroy()
{
	Shutdown();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pOwner, deltaPos, lowerBound, upperBound);

	{
		// Update all subsystems with pointer to owner
		auto pMasterList = m_subsystemMasterList.WLock();	// Lock not actually needed (nothing else should be running during relocation)
		for (int i=0; i<pMasterList->m_numSubsystems; i++)
		{
			NdSubsystem* pSubsystem = pMasterList->m_pSubsystems[i].ToSubsystem();
			if (pSubsystem)
				pSubsystem->RelocateOwner(deltaPos, lowerBound, upperBound);
		}

		RelocatePointer(pMasterList->m_pSubsystems, deltaPos, lowerBound, upperBound);
	}

	{
		auto pAnimBindingList = m_subsystemAnimBindingList.WLock();	// Lock not actually needed (nothing else should be running during relocation)
		RelocatePointer(pAnimBindingList->m_pInstanceBindings, deltaPos, lowerBound, upperBound);
	}

	m_subsystemListPool.Relocate(deltaPos, lowerBound, upperBound);
	m_subsystemAddList->Relocate(deltaPos, lowerBound, upperBound);
	m_subsystemAnimActionList->Relocate(deltaPos, lowerBound, upperBound);
	{
		auto pSubsystemLists = m_subsystemUpdateLists.WLock();
		for (int iPass=0; iPass<NdSubsystem::kUpdatePassCount; iPass++)
			pSubsystemLists->m_subsystemList[iPass].Relocate(deltaPos, lowerBound, upperBound);
	}

	DeepRelocatePointer(m_pSubsystemControllerDefault, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::Shutdown()
{
	{
		auto pMasterList = m_subsystemMasterList.WLock();	// Lock not actually needed
		if (pMasterList->m_pSubsystems)
		{
			for (int i=pMasterList->m_numSubsystems-1; i>=0; i--)
			{
				NdSubsystem* pSubsystem = pMasterList->m_pSubsystems[i].ToSubsystem();
				GAMEPLAY_ASSERT(pSubsystem != nullptr);

				if (pSubsystem)
				{		
					pSubsystem->Kill();
					NdSubsystem::Destroy(pSubsystem);
				}
			}

			for (int i=0; i<pMasterList->m_numSubsystems; i++)
			{
				NdSubsystem* pSubsystem = pMasterList->m_pSubsystems[i].ToSubsystem();
				if (pSubsystem)
					NdSubsystem::Free(pSubsystem);
			}

			pMasterList->m_pSubsystems = nullptr;
			pMasterList->m_numSubsystems = 0;
			pMasterList->m_maxSubsystems = 0;
		}
	}
	
	{
		auto pAnimBindingList = m_subsystemAnimBindingList.WLock();	// Lock not actually needed
		if (pAnimBindingList->m_pInstanceBindings)
		{
			pAnimBindingList->m_pInstanceBindings = nullptr;
			pAnimBindingList->m_numInstanceBindings = 0;
			pAnimBindingList->m_maxInstanceBindings = 0;
		}
	}

	m_isShutdown = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject* NdSubsystemMgr::GetOwner()
{
	return m_pOwner;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const NdGameObject* NdSubsystemMgr::GetOwner() const
{
	return m_pOwner;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::CleanDeadSubsystems()
{
	PROFILE(Subsystem, NdSubsystemMgr_CleanDeadSubsystems);

	Validate();

	{
		// Kill any anim actions that were bound to an instance that failed to spawn
		SubsystemList sysKillList;
		sysKillList.SetAllocator(&m_subsystemListPool);

		{
			auto pAnimActionList = m_subsystemAnimActionList.WLock();

			SubsystemList::Iterator itAction = pAnimActionList->Begin();
			while (itAction != pAnimActionList->End())
			{
				// 
				NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(itAction->ToSubsystem());
				if (pAction->PendingRequestFailed())
					sysKillList.PushBack(pAction);

				++itAction;
			}
		}

		while (!sysKillList.IsEmpty())
		{
			NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(sysKillList.Front()->ToSubsystem());
			pAction->Kill();
			sysKillList.PopFront();
		}
	}


	{
		// Remove killed anim actions from list
		auto pAnimActionList = m_subsystemAnimActionList.WLock();

		SubsystemList::Iterator itAction = pAnimActionList->Begin();
		while (itAction != pAnimActionList->End())
		{
			auto itNext = itAction.GetNext();

			NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(itAction->ToSubsystem());
			if (pAction->GetSubsystemState() == NdSubsystem::State::kKilled)
				pAnimActionList->Remove(itAction);

			itAction = itNext;
		}
	}

	// Compact list if subsystems have killed themselves off
	{
		auto pAnimBindingList = m_subsystemAnimBindingList.WLock();	// Lock not actually needed (nothing else should be running during CleanDeadSubsystems())
		int iCurrBinding=0, currBindingCount=0;
		while (iCurrBinding<pAnimBindingList->m_numInstanceBindings)
		{
			NdSubsystemAnimAction* pAction = pAnimBindingList->m_pInstanceBindings[iCurrBinding].m_hAction.ToSubsystem();
			GAMEPLAY_ASSERT(pAction);

			AnimStateInstanceBinding* pBinding = &pAnimBindingList->m_pInstanceBindings[iCurrBinding];

			bool transitionFailed = false;
			if (pBinding->m_instId == INVALID_ANIM_INSTANCE_ID)
			{
				// Never keep binding if transition failed
				AnimStateLayer* pLayer = m_pOwner->GetAnimControl()->GetStateLayerById(pBinding->m_layerId);

				StateChangeRequest::StatusFlag status = StateChangeRequest::kStatusFlagInvalid;
				if (pLayer)
					status = pLayer->GetTransitionStatus(pBinding->m_requestId);

				const StateChangeRequest::StatusFlag failFlags = StateChangeRequest::kStatusFlagInvalid | StateChangeRequest::kStatusFlagQueueFull
					| StateChangeRequest::kStatusFlagFailed | StateChangeRequest::kStatusFlagIgnored;
				transitionFailed = (0 != (status & failFlags));
			}

			if (pAction->GetSubsystemState() != NdSubsystem::State::kKilled && !pBinding->m_destroyed && !transitionFailed)
			{
				if (currBindingCount != iCurrBinding)
					pAnimBindingList->m_pInstanceBindings[currBindingCount] = pAnimBindingList->m_pInstanceBindings[iCurrBinding];

				currBindingCount++;
			}

			iCurrBinding++;
		}
		pAnimBindingList->m_numInstanceBindings = currBindingCount;
	}

	{
		auto pMasterList = m_subsystemMasterList.WLock();	// Lock not actually needed (nothing else should be running during CleanDeadSubsystems())

		// Call destructor for all subsystems that are being destroyed before freeing memory (because we
		//  don't want to free a parent subsystem before destructing a child allocated from that memory)
		int subsystemDestroyed = false;
		for (int iSys=0; iSys<pMasterList->m_numSubsystems; iSys++)
		{
			NdSubsystem* pSubsystem = pMasterList->m_pSubsystems[iSys].ToSubsystem();
			GAMEPLAY_ASSERTF(pSubsystem, ("Subsystem handle invalid?"));

			if (pSubsystem)
			{
				if (pSubsystem->GetSubsystemState() == NdSubsystem::State::kKilled)
				{
					CleanDeadSubsystemUpdateLists(pSubsystem);
					NdSubsystem::Destroy(pSubsystem);
					subsystemDestroyed = true;
				}
			}
		}

		// Now free memory and compact Compact list if subsystems have killed themselves off
		if (subsystemDestroyed)
		{
			int currSubsystemCount=0, iSys=0;
			while (iSys < pMasterList->m_numSubsystems)
			{
				NdSubsystem* pSubsystem = pMasterList->m_pSubsystems[iSys].ToSubsystem();
				//GAMEPLAY_ASSERTF(pSubsystem, ("Subsystem handle invalid?"));

				if (pSubsystem)
				{
					if (pSubsystem->GetSubsystemState() == NdSubsystem::State::kKilled)
					{
						NdSubsystem::Free(pSubsystem);
					}
					else
					{
						if (currSubsystemCount != iSys)
						{
							pMasterList->m_pSubsystems[currSubsystemCount] = pMasterList->m_pSubsystems[iSys];
							pMasterList->m_pSubsystems[iSys] = nullptr;
						}

						currSubsystemCount++;
					}
				}

				iSys++;
			}
			pMasterList->m_numSubsystems = currSubsystemCount;
		}
		//MsgCon("Num Subsystems: %d\n", m_numSubsystems);
	}

	Validate();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::CleanDeadSubsystemUpdateLists(NdSubsystem* pSubsystem)
{
	auto pSubsystemLists = m_subsystemUpdateLists.WLock();

	for (int iPass = 0; iPass < NdSubsystem::kUpdatePassCount; iPass++)
	{
		for (auto itSubsystem = pSubsystemLists->m_subsystemList[iPass].Begin();
			 itSubsystem != pSubsystemLists->m_subsystemList[iPass].End();
			 ++itSubsystem)
		{
			if (pSubsystem == itSubsystem->ToSubsystem())
			{
				pSubsystemLists->m_subsystemList[iPass].Remove(itSubsystem);
				break;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void NdSubsystemMgr::DoSubsystemPreProcessUpdate(NdSubsystem* pSubsystem)
{
	PROFILE_STRING(Subsystem, SubsystemPreProcessUpdateAsync, (pSubsystem->GetName()));
	pSubsystem->SubsystemPreProcessUpdateMacro();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void NdSubsystemMgr::DoSubsystemUpdate(NdSubsystem* pSubsystem)
{
	PROFILE_STRING(Subsystem, SubsystemUpdateAsync, (pSubsystem->GetName()));
	pSubsystem->SubsystemUpdateMacro();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void NdSubsystemMgr::DoSubsystemPostAnimUpdate(NdSubsystem* pSubsystem)
{
	PROFILE_STRING(Subsystem, SubsystemPostAnimUpdateAsync, (pSubsystem->GetName()));
	pSubsystem->SubsystemPostAnimUpdateMacro();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void NdSubsystemMgr::DoSubsystemPostRootLocatorUpdate(NdSubsystem* pSubsystem)
{
	PROFILE_STRING(Subsystem, SubsystemPostRootLocatorUpdateAsync, (pSubsystem->GetName()));
	pSubsystem->SubsystemPostRootLocatorUpdateMacro();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void NdSubsystemMgr::DoSubsystemPostAnimBlending(NdSubsystem* pSubsystem)
{
	PROFILE_STRING(Subsystem, SubsystemPostAnimBlendingAsync, (pSubsystem->GetName()));
	pSubsystem->SubsystemPostAnimBlendingMacro();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void NdSubsystemMgr::DoSubsystemPostJointUpdate(NdSubsystem* pSubsystem)
{
	PROFILE_STRING(Subsystem, SubsystemPostJointUpdateAsync, (pSubsystem->GetName()));
	pSubsystem->SubsystemPostJointUpdateMacro();
}

/// --------------------------------------------------------------------------------------------------------------- ///
CLASS_JOB_ENTRY_POINT_IMPLEMENTATION(NdSubsystemMgr, SubsystemMgrUpdatePassAsync)
{
	NdSubsystem* pSubsystem = (NdSubsystem*)jobParam;

	// Wait for subsystems we're dependent on
	for (NdSubsystemHandle hSys : pSubsystem->m_dependencySys)
	{
		NdSubsystem* pDepSys = hSys.ToSubsystem();
		GAMEPLAY_ASSERT(pDepSys->m_completionCounter != nullptr);
		WaitForCounter(pDepSys->m_completionCounter);
	}
	pSubsystem->m_dependencySys.Clear();

	switch (pSubsystem->m_currUpdatePass)
	{
	case NdSubsystem::kPreProcessUpdate:
		DoSubsystemPreProcessUpdate(pSubsystem);
		break;
	case NdSubsystem::kUpdate:
		DoSubsystemUpdate(pSubsystem);
		break;
	case NdSubsystem::kPostAnimUpdate:
		DoSubsystemPostAnimUpdate(pSubsystem);
		break;
	case NdSubsystem::kPostRootLocatorUpdate:
		DoSubsystemPostRootLocatorUpdate(pSubsystem);
		break;
	case NdSubsystem::kPostAnimBlending:
		DoSubsystemPostAnimBlending(pSubsystem);
		break;
	case NdSubsystem::kPostJointUpdate:
		DoSubsystemPostJointUpdate(pSubsystem);
		break;
	}

	GAMEPLAY_ASSERT(pSubsystem->m_completionCounter != nullptr);
	pSubsystem->m_completionCounter->Decrement();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::DoUpdatePass(NdSubsystem::UpdatePass updatePass)
{
	Validate();

	AddSubsystemUpdateFuncs();

	m_subsystemUpdateGlobalCounter = nullptr;

	{
		auto pSubsystemLists = m_subsystemUpdateLists.RLock();
		for (auto itSys = pSubsystemLists->m_subsystemList[updatePass].Begin(); itSys != pSubsystemLists->m_subsystemList[updatePass].End(); ++itSys)
		{
			NdSubsystem* pSubsystem = itSys->ToSubsystem();
			GAMEPLAY_ASSERT(pSubsystem != nullptr);
			if (pSubsystem && pSubsystem->GetSubsystemState() == NdSubsystem::State::kActive)
			{
				if (pSubsystem->SubsystemUpdateType(updatePass) == NdSubsystem::UpdateType::kAsynchronous)
				{
					// Asyncronous update, setup counter and other subsystem data needed in job func
					pSubsystem->m_currUpdatePass = updatePass;
					if (!pSubsystem->m_completionCounter)
						pSubsystem->m_completionCounter = ndjob::AllocateCounter(FILE_LINE_FUNC, 0);

					GAMEPLAY_ASSERT(pSubsystem->m_completionCounter->GetValue() == 0);
					pSubsystem->m_completionCounter->SetValue(1);

					// Make a list of async jobs we depend on (synchronous jobs we depend come earlier in the list and will already be done)
					if (pSubsystem->m_dependencySys.GetNodePool() == nullptr)
					{
						pSubsystem->m_dependencySys.SetAllocator(&m_subsystemListPool);
					}

					const StringId64* pDeps = pSubsystem->SubsystemUpdateGetDependencies(updatePass);
					int numDeps = pSubsystem->SubsystemUpdateGetDependencyCount(updatePass);
					for (int iDep = 0; iDep < numDeps; iDep++)
					{
						auto itPrev = itSys.GetPrev();
						while (itPrev != pSubsystemLists->m_subsystemList[updatePass].REnd())
						{
							NdSubsystem* pSysCurr = itPrev->ToSubsystem();
							if (pDeps[iDep] == pSysCurr->GetType() && pSysCurr->SubsystemUpdateType(updatePass) == NdSubsystem::UpdateType::kAsynchronous)
							{
								pSubsystem->m_dependencySys.PushBack(pSysCurr);
								break;
							}

							--itPrev;
						}
					}

					ndjob::JobDecl jobDecl(SubsystemMgrUpdatePassAsync, (uintptr_t)pSubsystem);

					if (!m_subsystemUpdateGlobalCounter)
					{
						m_subsystemUpdateGlobalCounter = ndjob::AllocateCounter(FILE_LINE_FUNC, 0);
					}

					jobDecl.m_associatedCounter = m_subsystemUpdateGlobalCounter;

					// If we have a list of dependencies, add the last two as the two dependent counters. If there are more than two,
					// we'll wait in the job func.
					if (!pSubsystem->m_dependencySys.IsEmpty())
					{
						NdSubsystem* pSysDep = pSubsystem->m_dependencySys.Back()->ToSubsystem();
						jobDecl.m_dependentCounter = pSysDep->m_completionCounter;
						pSubsystem->m_dependencySys.PopBack();
					}

					if (!pSubsystem->m_dependencySys.IsEmpty())
					{
						NdSubsystem* pSysDep = pSubsystem->m_dependencySys.Back()->ToSubsystem();
						jobDecl.m_dependentCounter2 = pSysDep->m_completionCounter;
						pSubsystem->m_dependencySys.PopBack();
					}

					m_subsystemUpdateGlobalCounter->Increment();

					ndjob::RunJobs(&jobDecl, 1, nullptr, FILE_LINE_FUNC);
				}
				else
				{
					// Synchronous update - Wait on any async jobs we're dependent on (synchronous jobs we depend come earlier in the list and on will already be done)
					const StringId64* pDeps = pSubsystem->SubsystemUpdateGetDependencies(updatePass);
					int numDeps = pSubsystem->SubsystemUpdateGetDependencyCount(updatePass);
					for (int iDep=0; iDep<numDeps; iDep++)
					{
						auto itPrev = itSys.GetPrev();
						while (itPrev != pSubsystemLists->m_subsystemList[updatePass].REnd())
						{
							NdSubsystem* pSysCurr = itPrev->ToSubsystem();
							if (pDeps[iDep] == pSysCurr->GetType())
							{
								if (pSysCurr->SubsystemUpdateType(updatePass) == NdSubsystem::UpdateType::kAsynchronous)
								{
									GAMEPLAY_ASSERT(pSysCurr->m_completionCounter != nullptr);
									WaitForCounter(pSysCurr->m_completionCounter);
								}
							}

							--itPrev;
						}
					}

					// Do the update
					PROFILE_STRING(Subsystem, SubsystemUpdateSynchronous, (pSubsystem->GetName()));
					pSubsystem->SubsystemUpdate(updatePass);
				}
			}
		}
	}

	if (m_subsystemUpdateGlobalCounter)
	{
		ndjob::WaitForCounterAndFree(m_subsystemUpdateGlobalCounter);
		m_subsystemUpdateGlobalCounter = nullptr;
	}

	Validate();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::PreProcessUpdate()
{
	PROFILE(Subsystem, NdSubsystemMgr_PreProcessUpdate);

	DoUpdatePass(NdSubsystem::kPreProcessUpdate);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::Update()
{
	PROFILE(Subsystem, NdSubsystemMgr_Update);

	DoUpdatePass(NdSubsystem::kUpdate);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::PostAnimUpdate()
{
	PROFILE(Subsystem, NdSubsystemMgr_PostAnimUpdate);

	DoUpdatePass(NdSubsystem::kPostAnimUpdate);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::PostRootLocatorUpdate()
{
	PROFILE(Subsystem, NdSubsystemMgr_PostRootLocatorUpdate);

	DoUpdatePass(NdSubsystem::kPostRootLocatorUpdate);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::PostAnimBlending()
{
	PROFILE(Subsystem, NdSubsystemMgr_PostAnimBlending);

	DoUpdatePass(NdSubsystem::kPostAnimBlending);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::PostJointUpdate()
{
	PROFILE(Subsystem, NdSubsystemMgr_PostJointUpdate);

	DoUpdatePass(NdSubsystem::kPostJointUpdate);

	// Wait on async jobs here, so we're not running anything during subsystem relocation
	if (m_subsystemUpdateGlobalCounter)
	{
		ndjob::WaitForCounterAndFree(m_subsystemUpdateGlobalCounter);
		m_subsystemUpdateGlobalCounter = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::SendEvent(Event& event) const
{
	// Copy master list because so we don't have m_subsystemMasterList locked during the callback, which might also try to lock
	ScopedTempAllocator alloc(FILE_LINE_FUNC);
	NdSubsystemHandle* pMasterListCopy = nullptr;
	int size = 0;

	{
		auto pMasterList = m_subsystemMasterList.RLock();	// Lock needed, could be called during AddSubsystem()
		size = pMasterList->m_numSubsystems;
		pMasterListCopy = NDI_NEW NdSubsystemHandle[size];
		for (int i = 0; i < size; i++)
		{
			pMasterListCopy[i] = pMasterList->m_pSubsystems[i];
		}
	}

	for (int i = 0; i < size; i++)
	{
		NdSubsystem* pSubsystem = pMasterListCopy[i].ToSubsystem();
		if (pSubsystem->IsAlive())
			pSubsystem->EventHandler(event);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::SendEvent(const AnimStateInstance* pInst, Event& event) const
{
	ScopedTempAllocator alloc(FILE_LINE_FUNC);
	NdSubsystemHandle* pAnimActionListCopy = nullptr;
	int size = 0;

	{
		auto pAnimActionList = m_subsystemAnimActionList.RLock();
		for (auto hSys : pAnimActionList.GetUnsafeReference())
			size++;

		pAnimActionListCopy = NDI_NEW NdSubsystemHandle[size];

		int index = 0;
		for (auto hSys : pAnimActionList.GetUnsafeReference())
		{
			pAnimActionListCopy[index++] = hSys;
		}
	}

	for (int i = 0; i < size; i++)
	{
		NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(pAnimActionListCopy[i].ToSubsystem());

		if (pAction->IsAlive())
		{
			if (IsSubsystemBoundToAnimInstance(pInst, pAction))
				pAction->EventHandler(event);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::SendEvent(NdSubsystem* pSys, Event& event) const
{
	if (pSys->IsAlive())
		pSys->EventHandler(event);
}

/// --------------------------------------------------------------------------------------------------------------- ///
BoxedValue NdSubsystemMgr::SendEvent(StringId64 msg,
									 const BoxedValue& a0 /* = BoxedValue() */,
									 const BoxedValue& a1 /* = BoxedValue() */,
									 const BoxedValue& a2 /* = BoxedValue() */,
									 const BoxedValue& a3 /* = BoxedValue() */) const
{
	Event evt(msg, nullptr, a0, a1, a2, a3);
	SendEvent(evt);
	return evt.GetResponse();
}

BoxedValue NdSubsystemMgr::SendEvent(const AnimStateInstance* pInst, StringId64 msg,
									 const BoxedValue& a0 /* = BoxedValue() */,
									 const BoxedValue& a1 /* = BoxedValue() */,
									 const BoxedValue& a2 /* = BoxedValue() */,
									 const BoxedValue& a3 /* = BoxedValue() */) const
{
	Event evt(msg, nullptr, a0, a1, a2, a3);
	SendEvent(pInst, evt);
	return evt.GetResponse();
}

BoxedValue NdSubsystemMgr::SendEvent(NdSubsystem* pSys, StringId64 msg,
									 const BoxedValue& a0 /* = BoxedValue() */,
									 const BoxedValue& a1 /* = BoxedValue() */,
									 const BoxedValue& a2 /* = BoxedValue() */,
									 const BoxedValue& a3 /* = BoxedValue() */) const
{
	Event evt(msg, nullptr, a0, a1, a2, a3);
	SendEvent(pSys, evt);
	return evt.GetResponse();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdSubsystemMgr::AddSubsystem(NdSubsystem* pSubsystem)
{
	PROFILE(Subsystem, NdSubsystemMgr_AddSubsystem);

	GAMEPLAY_ASSERT(!m_isShutdown);

	GAMEPLAY_ASSERT(m_pOwner && m_pOwner == pSubsystem->GetOwnerGameObject());
	//pSubsystem->SetOwner(m_pOwner);

	{
		auto pMasterList = m_subsystemMasterList.WLock();	// Lock needed, could be called during AddSubsystem()
		SYSTEM_ASSERTF(pMasterList->m_numSubsystems < pMasterList->m_maxSubsystems, ("Too many subsystems (%d). Increase m_maxSubsystems.\n", pMasterList->m_numSubsystems));
		if (pMasterList->m_numSubsystems < pMasterList->m_maxSubsystems)
		{
			pMasterList->m_pSubsystems[pMasterList->m_numSubsystems++] = pSubsystem;
		}
		else
		{
			return false;
		}
	}

	m_subsystemAddList->PushBack(pSubsystem);
	if (pSubsystem->IsAnimAction())
		m_subsystemAnimActionList->PushBack(pSubsystem);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::AddSubsystemUpdateFuncs()
{
	auto pSubsystemAddList = m_subsystemAddList.WLock();
	auto pSubsystemLists = m_subsystemUpdateLists.WLock();

	while (!pSubsystemAddList->IsEmpty())
	{
		NdSubsystem* pSubsystem = pSubsystemAddList->Front()->ToSubsystem();
		pSubsystemAddList->PopFront();

		if (pSubsystem)
		{
			for (int iPass = 0; iPass < NdSubsystem::kUpdatePassCount; iPass++)
				AddSubsystemUpdateFuncs(pSubsystem, (NdSubsystem::UpdatePass)iPass, pSubsystemLists.GetUnsafeReference());
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::AddSubsystemUpdateFuncs(NdSubsystem* pSubsystem,
											 NdSubsystem::UpdatePass pass,
											 SubsystemUpdateLists& subsystemLists)
{
	PROFILE(Subsystem, NdSubsystemMgr_AddSubsystemUpdateFuncs);

	SubsystemList& updateList = subsystemLists.m_subsystemList[pass];

	NdSubsystem::UpdateType updateType = pSubsystem->SubsystemUpdateType(pass);

	// First just try to insert into the list
	bool valid = true;
	if (updateType != NdSubsystem::UpdateType::kNone)
	{
		bool synchronousFound = false;
		auto itFirstSynchronous = updateList.Begin();

		// Does any other subsystem depend on this one? If so, make sure this one is inserted first.
		auto itSys = updateList.Begin();
		while (itSys != updateList.End())
		{
			NdSubsystem* pCurrSubsystem = itSys->ToSubsystem();

			if (!synchronousFound)
			{
				itFirstSynchronous = itSys;
				if (pCurrSubsystem->SubsystemUpdateType(pass) == NdSubsystem::UpdateType::kSynchronous)
					synchronousFound = true;
			}

			if (pCurrSubsystem->SubsystemUpdateCheckDependency(pass, pSubsystem->GetType()))
				break;

			++itSys;
		}

		auto itInsert = itSys;

		if (pSubsystem->SubsystemUpdateType(pass) == NdSubsystem::UpdateType::kAsynchronous)
		{
			while (itInsert != itFirstSynchronous)
			{
				auto itPrev = itInsert.GetPrev();
				NdSubsystem* pPrevSubsystem = itPrev->ToSubsystem();
				if (pSubsystem->SubsystemUpdateCheckDependency(pass, pPrevSubsystem->GetType()))
					break;

				itInsert = itPrev;
			}
		}

		updateList.InsertBefore(itInsert, pSubsystem);

		// Make sure it doesn't come before other Subsystems it depends on
		while (itSys != updateList.End())
		{
			NdSubsystem* pCurrSubsystem = itSys->ToSubsystem();
			if (pSubsystem->SubsystemUpdateCheckDependency(pass, pCurrSubsystem->GetType()))
			{
				valid = false;
				break;
			}
			++itSys;
		}

	}

	// Failed insert in the correct order. Remove all and reinsert in dependency order
	if (!valid)
	{
		// Move into temp list
		SubsystemList tempList;
		tempList.AquireList(&updateList);
	
		// First push all Subsystems with no dependencies
		auto itSys = tempList.Begin();
		while (itSys != tempList.End())
		{
			auto itCurr = itSys++;	// Advance, but save prev iterator

			NdSubsystem* pCurrSubsystem = itCurr->ToSubsystem();
			if (pCurrSubsystem->SubsystemUpdateGetDependencyCount(pass) == 0)
			{
				updateList.PushBack(*itCurr);
				tempList.Remove(itCurr);
			}
		}

		// Go through remaining subsystems and insert in dependency order
		while(!tempList.IsEmpty())
		{
			auto itAdd = AddSubsystemUpdateNodeDep(pass, tempList, tempList.Front(), tempList.Front(), 0);

			updateList.PushBack(*itAdd);
			tempList.Remove(itAdd);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemMgr::SubsystemList::Iterator NdSubsystemMgr::AddSubsystemUpdateNodeDep(NdSubsystem::UpdatePass pass,
																				  SubsystemList& tempList,
																				  SubsystemList::Iterator itInitial,
																				  SubsystemList::Iterator itCurr,
																				  int level)
{
	NdSubsystem* pCurrSys = itCurr->ToSubsystem();

	// If we're 32 levels deep, assume circular dependency chain
	GAMEPLAY_ASSERTF(level < 32,
					 ("SubsystemMgr - Circular dependency chain starting with subsystem update Pass: %d, Type: %s\n",
					  (int)pass,
					  DevKitOnly_StringIdToString(pCurrSys->GetType())));
	if (level >= 32)
		return itCurr;

	int depCount = pCurrSys->SubsystemUpdateGetDependencyCount(pass);
	const StringId64* pDepList = pCurrSys->SubsystemUpdateGetDependencies(pass);

	for (int iDep=0; iDep<depCount; iDep++)
	{
		const StringId64 depSysId = pDepList[iDep];

		auto itDepTest = tempList.Begin();
		while (itDepTest != tempList.End())
		{
			NdSubsystem* pSysDepTest = itDepTest->ToSubsystem();
			if (depSysId == pSysDepTest->GetType())
				return AddSubsystemUpdateNodeDep(pass, tempList, itInitial, itDepTest, level+1);
		}
	}

	return itCurr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystem* NdSubsystemMgr::FindSubsystem(StringId64 subsystemType, I32 index) const
{
	int count = 0;

	auto pMasterList = m_subsystemMasterList.RLock();	// Lock needed, could be called during AddSubsystem()
	for (int i=pMasterList->m_numSubsystems-1; i>=0; i--)
	{
		NdSubsystem* pSubsystem = pMasterList->m_pSubsystems[i].ToSubsystem();
		if (pSubsystem && pSubsystem->GetType() == subsystemType)
		{
			if (count == index)
				return pSubsystem;

			count++;
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystem* NdSubsystemMgr::FindSubsystemById(U32 id) const
{
	if (id == FadeToStateParams::kInvalidSubsystemId)
		return nullptr;

	auto pMasterList = m_subsystemMasterList.RLock();	// Lock needed, could be called during AddSubsystem()
	for (int i=0; i<pMasterList->m_numSubsystems; i++)
	{
		NdSubsystem* pSys = pMasterList->m_pSubsystems[i].ToSubsystem();
		if (pSys && pSys->GetSubsystemId() == id)
		{
			return pSys;
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemAnimAction* NdSubsystemMgr::FindSubsystemAnimActionById(U32 id) const
{
	if (id == FadeToStateParams::kInvalidSubsystemId)
		return nullptr;

	auto pAnimActionList = m_subsystemAnimActionList.RLock();
	for (auto hSys : pAnimActionList.GetUnsafeReference())
	{
		NdSubsystem* pSys = hSys.ToSubsystem();
		if (pSys && pSys->GetSubsystemId() == id)
			return static_cast<NdSubsystemAnimAction*>(pSys);
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemAnimController* NdSubsystemMgr::FindSubsystemAnimControllerById(U32 id) const
{
	if (id == FadeToStateParams::kInvalidSubsystemId)
		return nullptr;

	auto pAnimActionList = m_subsystemAnimActionList.RLock();
	for (auto hSys : pAnimActionList.GetUnsafeReference())
	{
		NdSubsystem* pSys = hSys.ToSubsystem();
		if (pSys && pSys->GetSubsystemId() == id)
		{
			if (pSys->IsKindOf(g_type_NdSubsystemAnimController))
				return static_cast<NdSubsystemAnimController*>(pSys);
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemAnimAction* NdSubsystemMgr::FindBoundSubsystemAnimAction(const AnimStateInstance* pInst,
																	StringId64 subsystemType) const
{
	auto pAnimBindingList = m_subsystemAnimBindingList.RLock();	// Lock needed

	for (int i=0; i<pAnimBindingList->m_numInstanceBindings; i++)
	{
		AnimStateInstanceBinding* pBinding = &pAnimBindingList->m_pInstanceBindings[i];
		NdSubsystemAnimAction* pAction = pBinding->m_hAction.ToSubsystem();

		if (subsystemType == pAction->GetType() && pBinding->m_instId == pInst->GetId() && pBinding->m_layerId == pInst->GetLayerId())
			return pAction;
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemAnimAction* NdSubsystemMgr::FindBoundSubsystemAnimActionByType(const AnimStateInstance* pInst,
																	StringId64 subsystemType) const
{
	auto pAnimBindingList = m_subsystemAnimBindingList.RLock();	// Lock needed

	for (int i=0; i<pAnimBindingList->m_numInstanceBindings; i++)
	{
		AnimStateInstanceBinding* pBinding = &pAnimBindingList->m_pInstanceBindings[i];
		if (pBinding->m_instId == pInst->GetId() && pBinding->m_layerId == pInst->GetLayerId())
		{
			NdSubsystemAnimAction* pAction = pBinding->m_hAction.ToSubsystem();

			if (pAction->IsKindOf(subsystemType))
				return pAction;
		}
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemAnimAction* NdSubsystemMgr::FindSubsystemAnimAction(StringId64 subsystemType) const
{
	NdSubsystem* pSubsystem = FindSubsystem(subsystemType);
	if (pSubsystem && pSubsystem->IsAnimAction())
		return static_cast<NdSubsystemAnimAction*>(pSubsystem);

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemAnimController* NdSubsystemMgr::GetActiveSubsystemController(StringId64 layerId, StringId64 type) const
{
	NdSubsystemAnimController* pController = (type == INVALID_STRING_ID_64) ? m_pSubsystemControllerDefault : nullptr;

	AnimStateLayer* pLayer = GetOwner()->GetAnimControl()->GetStateLayerById(layerId);
	if (pLayer)
	{
		const U32 activeControllerId = pLayer->GetActiveSubsystemControllerId();
		NdSubsystem* pSys = FindSubsystemById(activeControllerId);
		if (pSys)
		{
			if (type == INVALID_STRING_ID_64 || pSys->IsKindOf(type))
				pController = static_cast<NdSubsystemAnimController*>(pSys);
		}
	}

	return pController;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemAnimController* NdSubsystemMgr::GetTopSubsystemController(StringId64 layerId, StringId64 type) const
{
	NdSubsystemAnimController* pController = (type == INVALID_STRING_ID_64) ? m_pSubsystemControllerDefault : nullptr;

	AnimStateLayer* pLayer = GetOwner()->GetAnimControl()->GetStateLayerById(layerId);
	if (pLayer)
	{
		AnimStateInstance* pInst = pLayer->CurrentStateInstance();

		const U32 activeControllerId = pInst->GetSubsystemControllerId();
		NdSubsystem* pSys = FindSubsystemById(activeControllerId);
		if (pSys)
		{
			if (type == INVALID_STRING_ID_64 || pSys->IsKindOf(type))
				pController = static_cast<NdSubsystemAnimController*>(pSys);
		}
	}

	return pController;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemMgr::AnimStateInstanceBinding* NdSubsystemMgr::GetInstanceBinding(SubsystemAnimBindingList& animBindingList,
																			 int* pIndex)
{
	ALWAYS_ASSERT(animBindingList.m_numInstanceBindings < animBindingList.m_maxInstanceBindings);
	if (pIndex)
		*pIndex = animBindingList.m_numInstanceBindings;
	AnimStateInstanceBinding* pBinding = &animBindingList.m_pInstanceBindings[animBindingList.m_numInstanceBindings++];
	*pBinding = AnimStateInstanceBinding();
	return pBinding;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int NdSubsystemMgr::BindToAnimRequest(NdSubsystemAnimAction* pAction,
									  StateChangeRequest::ID requestId,
									  StringId64 layerId)
{
	auto pAnimBindingList = m_subsystemAnimBindingList.WLock();	// Lock needed

	GAMEPLAY_ASSERT(pAction->GetActionState() == NdSubsystemAnimAction::ActionState::kInvalid || pAction->GetActionState() == NdSubsystemAnimAction::ActionState::kUnattached);
	GAMEPLAY_ASSERT(requestId != StateChangeRequest::kInvalidId);

	int index;
	AnimStateInstanceBinding* pBinding = GetInstanceBinding(pAnimBindingList.GetUnsafeReference(), &index);
	pBinding->m_hAction = pAction;
	pBinding->m_subsystemId = pAction->GetSubsystemId();
	pBinding->m_layerId = (layerId == INVALID_STRING_ID_64) ? SID("base") : layerId;
	pBinding->m_requestId = requestId;

	pAction->SetActionState(NdSubsystemAnimAction::ActionState::kPending);
	pAction->m_layerId = pBinding->m_layerId;
	pAction->m_requestId = pBinding->m_requestId;

	return index;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int NdSubsystemMgr::BindToAnimSetup(NdSubsystemAnimAction* pAction,
									StateChangeRequest::ID requestId,
									AnimStateInstance::ID instId,
									StringId64 layerId)
{
	auto pAnimBindingList = m_subsystemAnimBindingList.WLock();	// Lock needed

	GAMEPLAY_ASSERT(pAction->GetActionState() == NdSubsystemAnimAction::ActionState::kUnattached);

	int index;
	AnimStateInstanceBinding* pBinding = GetInstanceBinding(pAnimBindingList.GetUnsafeReference(), &index);
	pBinding->m_hAction = pAction;
	pBinding->m_subsystemId = pAction->GetSubsystemId();
	pBinding->m_layerId = (layerId == INVALID_STRING_ID_64) ? SID("base") : layerId;
	pBinding->m_requestId = requestId;
	pBinding->m_instId = instId;

	pAction->SetActionState(NdSubsystemAnimAction::ActionState::kPending);
	pAction->m_layerId = pBinding->m_layerId;
	pAction->m_requestId = pBinding->m_requestId;

	return index;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int NdSubsystemMgr::BindToInstance(NdSubsystemAnimAction* pAction, AnimStateInstance* pInst)
{
	auto pAnimBindingList = m_subsystemAnimBindingList.WLock();	// Lock needed

	GAMEPLAY_ASSERT(pAction->GetActionState() == NdSubsystemAnimAction::ActionState::kInvalid || pAction->GetActionState() == NdSubsystemAnimAction::ActionState::kUnattached);

	int index;
	AnimStateInstanceBinding* pBinding = GetInstanceBinding(pAnimBindingList.GetUnsafeReference(), &index);
	pBinding->m_hAction = pAction;
	pBinding->m_subsystemId = pAction->GetSubsystemId();
	pBinding->m_layerId = pInst->GetLayer()->GetName();
	pBinding->m_requestId = StateChangeRequest::kInvalidId;
	pBinding->m_instId = pInst->GetId();
	GAMEPLAY_ASSERT(pBinding->m_instId !=INVALID_ANIM_INSTANCE_ID);

	bool isTop = pInst == pInst->GetLayer()->CurrentStateInstance();
	NdSubsystemAnimAction::ActionState actionState = isTop ? NdSubsystemAnimAction::ActionState::kTop : NdSubsystemAnimAction::ActionState::kExiting;
	if (actionState == NdSubsystemAnimAction::ActionState::kExiting)
		pAction->m_debugExitState = SID("-");
	pAction->SetActionState(actionState);
	pAction->m_layerId = pBinding->m_layerId;
	pAction->m_requestId = StateChangeRequest::kInvalidId;

	pAction->UpdateTopInstance(pInst);

	return index;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int NdSubsystemMgr::AutoBind(NdSubsystemAnimAction* pAction, StringId64 layerId)
{
	auto pAnimBindingList = m_subsystemAnimBindingList.WLock();	// Lock needed

	GAMEPLAY_ASSERT(pAction->GetActionState() == NdSubsystemAnimAction::ActionState::kInvalid || pAction->GetActionState() == NdSubsystemAnimAction::ActionState::kUnattached);

	int index;
	AnimStateInstanceBinding* pBinding = GetInstanceBinding(pAnimBindingList.GetUnsafeReference(), &index);
	pBinding->m_hAction = pAction;
	pBinding->m_subsystemId = pAction->GetSubsystemId();
	pBinding->m_layerId = (layerId == INVALID_STRING_ID_64) ? SID("base") : layerId;
	pBinding->m_requestId = StateChangeRequest::kInvalidId;
	pBinding->m_instId = INVALID_ANIM_INSTANCE_ID;

	pAction->SetActionState(NdSubsystemAnimAction::ActionState::kAutoAttach);
	pAction->m_layerId = pBinding->m_layerId;
	pAction->m_requestId = StateChangeRequest::kInvalidId;

	return index;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::InstancePrepare(StringId64 layerId,
									 StateChangeRequest::ID requestId,
									 AnimStateInstance::ID instId,
									 bool isTop,
									 const DC::AnimState* pAnimState,
									 FadeToStateParams* pParams)
{
	AnimStateLayer* pLayer = m_pOwner->GetAnimControl()->GetStateLayerById(layerId);

	const DC::SubsystemStateInfo* pSubSysStateInfo = AnimStateLookupStateInfo<DC::SubsystemStateInfo>(pAnimState, SID("subsystem"));

#ifndef FINAL_BUILD
	StringId64 subsystemControllerType = pSubSysStateInfo ? pSubSysStateInfo->m_subsystemController : INVALID_STRING_ID_64; 
	if (pParams->m_subsystemControllerId != 0)
	{
		NdSubsystem* pSysController = FindSubsystemById(pParams->m_subsystemControllerId);
		StringId64 sysControllerType = pSysController ? pSysController->GetType() : INVALID_STRING_ID_64;
		//MAIL_ASSERT(Subsystem, sysControllerType == subsystemControllerType, ("Subsystem controller mismatch, Anim State: %s, State Ctrl: %s, Params: %s",
		//	DevKitOnly_StringIdToStringOrHex(pAnimState->m_phaseAnimName),
		//	DevKitOnly_StringIdToStringOrHex(subsystemControllerType),
		//	DevKitOnly_StringIdToStringOrHex(sysControllerType)));
		#ifdef RBRONER
			GAMEPLAY_ASSERT(pSysController && pSysController->GetType() == subsystemControllerType);
		#endif
	}
	else
	{
		//MAIL_ASSERT(Subsystem, subsystemControllerType == INVALID_STRING_ID_64, ("Subsystem controller mismatch, Anim State: %s, State: %s, Params: %s",
		//	DevKitOnly_StringIdToStringOrHex(pAnimState->m_phaseAnimName),
		//	DevKitOnly_StringIdToStringOrHex(subsystemControllerType),
		//	DevKitOnly_StringIdToStringOrHex(INVALID_STRING_ID_64)));
		#ifdef RBRONER
			GAMEPLAY_ASSERT(subsystemControllerType == INVALID_STRING_ID_64);
		#endif
	}
#endif

	// If this is an auto transition (we know because requestId == kInvalidId), then we never got a PendingChange
	//   event. So we need to do some of that work here.
	if (requestId == StateChangeRequest::kInvalidId)
	{
		if (FALSE_IN_FINAL_BUILD(m_debugAnimTransitions))
		{
			// Create Debug action, and bind it
			NdAnimActionDebugSubsystem* pAction = (NdAnimActionDebugSubsystem*)
				NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap,
									SubsystemSpawnInfo(SID("NdAnimActionDebugSubsystem"), GetOwner()),
									FILE_LINE_FUNC);

			pAction->m_markedForBindingIndex = BindToAnimSetup(pAction, requestId, instId, layerId);
			pAction->InstanceAutoTransition(layerId);
		}
	}

	// Create subsystems declared in state info
	{
		if (pSubSysStateInfo && pSubSysStateInfo->m_subsystems)
		{
			// Gather all top anim actions
			SubsystemList topActionList;
			topActionList.SetAllocator(&m_subsystemListPool);
			{
				auto pAnimActionList = m_subsystemAnimActionList.RLock();
				for (auto hSys : pAnimActionList.GetUnsafeReference())
				{
					NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(hSys.ToSubsystem());
					GAMEPLAY_ASSERT(pAction != nullptr);

					if (pAction->IsTop() && pAction->GetLayerId() == layerId && !pAction->IsKilled())
						topActionList.PushBack(pAction);
				}
			}

			// Also gather actions already bound to the pending transition
			SubsystemList boundActionList;
			boundActionList.SetAllocator(&m_subsystemListPool);
			{
				auto pAnimBindingList = m_subsystemAnimBindingList.WLock();	// Lock needed

				for (int i=0; i<pAnimBindingList->m_numInstanceBindings; i++)
				{
					AnimStateInstanceBinding* pBinding = &pAnimBindingList->m_pInstanceBindings[i];
					if (requestId != StateChangeRequest::kInvalidId && pBinding->m_requestId == requestId && pBinding->m_layerId == layerId)
					{
						NdSubsystemAnimAction* pAction = pBinding->m_hAction.ToSubsystem();
						if (pAction && !pAction->IsKilled())
							boundActionList.PushBack(pAction);
					}
				}
			}

			for (int i = 0; i < pSubSysStateInfo->m_subsystems->m_count; i++)
			{
				StringId64 subsystemId = pSubSysStateInfo->m_subsystems->m_array[i];

				// Don't create if this type of Action is already attached to this instance
				bool shouldSkip = false;

				for (auto hSys : boundActionList)
				{
					NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(hSys.ToSubsystem());
					GAMEPLAY_ASSERT(pAction != nullptr);

					if (pAction->GetType() == subsystemId)
						shouldSkip = true;
				}

				// Don't create if this type of Action is already attached to the top instance, and will bind to the new one.
				for (auto hSys : topActionList)
				{
					NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(hSys.ToSubsystem());
					GAMEPLAY_ASSERT(pAction != nullptr);

					if (pAction->GetLayerId() == layerId && pAction->GetType() == subsystemId && pAction->ShouldBindToNewInstance(pAnimState, pParams))
						shouldSkip = true;
				}

				if (shouldSkip)
					continue;

				SubsystemSpawnInfo spawnInfo(subsystemId, GetOwner());
				spawnInfo.m_spawnedFromStateInfo = true;
				spawnInfo.m_subsystemControllerId = pParams->m_subsystemControllerId;
				NdSubsystem* pSubsystem = NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap, spawnInfo, FILE_LINE_FUNC);
				if (pSubsystem)
				{
					GAMEPLAY_ASSERT(pSubsystem->IsAnimAction());
					NdSubsystemAnimAction *pAction = static_cast<NdSubsystemAnimAction*>(pSubsystem);
					GAMEPLAY_ASSERT(pAction->IsUnattached());
					pAction->m_markedForBindingIndex = BindToAnimSetup(pAction, requestId, instId, layerId);
				}
			}
		}
	}

	// Mark pending Subsystems that are bound to the new instance
	{
		auto pAnimBindingList = m_subsystemAnimBindingList.RLock();	// Lock needed
		for (int i = 0; i < pAnimBindingList->m_numInstanceBindings; i++)
		{
			AnimStateInstanceBinding* pBinding = &pAnimBindingList->m_pInstanceBindings[i];
			if (layerId == pBinding->m_layerId && pBinding->m_instId == INVALID_ANIM_INSTANCE_ID)
			{
				if (pBinding->m_requestId == StateChangeRequest::kInvalidId)
				{
					NdSubsystemAnimAction* pAction = pBinding->m_hAction.ToSubsystem();
					GAMEPLAY_ASSERT(pAction && pAction->IsAutoAttach());
					GAMEPLAY_ASSERT(!pBinding->m_destroyed);

					pAction->m_markedForBindingIndex = i;
				}
				else if (requestId == pBinding->m_requestId)
				{
					NdSubsystemAnimAction* pAction = pBinding->m_hAction.ToSubsystem();
					GAMEPLAY_ASSERT(pAction && pAction->IsPending());
					GAMEPLAY_ASSERT(!pBinding->m_destroyed);

					pAction->m_markedForBindingIndex = i;
				}
			}
		}
	}

	SubsystemList sysCallbackPrep;
	SubsystemList sysCallbackReplace;

	sysCallbackPrep.SetAllocator(&m_subsystemListPool);
	sysCallbackReplace.SetAllocator(&m_subsystemListPool);

	{
		auto locks = AquireLocks(m_subsystemAnimActionList.AsConst(), m_subsystemAnimBindingList);
		auto& pAnimActionList = locks.first;
		auto& pAnimBindingList = locks.second;

		const SubsystemList& animActionList = pAnimActionList.GetUnsafeReference();
		SubsystemAnimBindingList& animBindingList = pAnimBindingList.GetUnsafeReference();

		// Go through all AnimAction subsystems and do the setup the bindings
		for (auto hSys : animActionList)
		{
			NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(hSys.ToSubsystem());
			GAMEPLAY_ASSERT(pAction != nullptr);

			if (pAction->GetLayerId() == layerId)
			{
				AnimStateInstanceBinding* pBinding = nullptr;
				if (pAction->m_markedForBindingIndex >= 0)
				{
					// Pending subsystem that was marked. It's will be bound to the new Top instance
					GAMEPLAY_ASSERT(pAction->IsPending() || pAction->IsAutoAttach());
					pBinding = &pAnimBindingList->m_pInstanceBindings[pAction->m_markedForBindingIndex];
					pAction->m_markedForBindingIndex = -1;
					pAction->SetActionState(NdSubsystemAnimAction::ActionState::kTop);
				}
				else if (pAction->IsTop())
				{
					// This subsystem is bound to the current top instance. Check if it should be bound to the new one.
					if (pAction->ShouldBindToNewInstance(pAnimState, pParams))
					{
						pBinding = GetInstanceBinding(animBindingList);
					}
					else if (isTop && pAction->IsTop())
					{
						pAction->SetActionState(NdSubsystemAnimAction::ActionState::kExiting);
						pAction->m_debugExitState = pAnimState ? pAnimState->m_name.m_symbol : SID("--");
						sysCallbackReplace.PushBack(pAction);
					}
				}
				else if (pAction->GetSubsystemId() == pParams->m_subsystemControllerId)
				{
					if (pAction->ShouldBindToNewInstance(pAnimState, pParams))
					{
						pBinding = GetInstanceBinding(animBindingList);
						pAction->SetActionState(NdSubsystemAnimAction::ActionState::kTop);
					}
				}

				if (pBinding)
				{
					// Setup the bind info, and pass the Prepare event to the subsystem.
					pBinding->m_hAction = pAction;
					pBinding->m_subsystemId = pAction->GetSubsystemId();
					pBinding->m_layerId = layerId;
					pBinding->m_requestId = requestId;
					pBinding->m_instId = instId;
					GAMEPLAY_ASSERT(instId !=INVALID_ANIM_INSTANCE_ID);
					GAMEPLAY_ASSERT(!pBinding->m_destroyed);

					if (pAction->GetSubsystemState() == NdSubsystem::State::kActive)
					{
						sysCallbackPrep.PushBack(pAction);
					}
				}
			}
		}
	}

	// Call callbacks outside of m_subsystemMasterList/m_subsystemAnimBindingList locks
	while (!sysCallbackReplace.IsEmpty())
	{
		NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(sysCallbackReplace.Front()->ToSubsystem());
		pAction->InstanceReplace(pAnimState, pParams);
		sysCallbackReplace.PopFront();
	}

	while (!sysCallbackPrep.IsEmpty())
	{
		NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(sysCallbackPrep.Front()->ToSubsystem());
		pAction->InstancePrepare(pAnimState, pParams);
		sysCallbackPrep.PopFront();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::InstanceCreate(AnimStateInstance* pInst)
{
	SubsystemList sysCallbackList;
	sysCallbackList.SetAllocator(&m_subsystemListPool);

	{
		auto pAnimBindingList = m_subsystemAnimBindingList.RLock();	// Lock needed

		// Pass the Create event to all subsystems bound to this instance
		for (int i = 0; i < pAnimBindingList->m_numInstanceBindings; i++)
		{
			AnimStateInstanceBinding* pBinding = &pAnimBindingList->m_pInstanceBindings[i];
			if (pBinding->m_instId == pInst->GetId() && pBinding->m_layerId == pInst->GetLayer()->GetName())
			{
				GAMEPLAY_ASSERT(!pBinding->m_destroyed);

				NdSubsystemAnimAction* pAction = pBinding->m_hAction.ToSubsystem();
				GAMEPLAY_ASSERT(pAction != nullptr);

				if (pAction->GetSubsystemState() == NdSubsystem::State::kActive)
					sysCallbackList.PushBack(pAction);
			}
		}
	}

	// Call callbacks outside of m_subsystemAnimBindingList lock
	while (!sysCallbackList.IsEmpty())
	{
		NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(sysCallbackList.Front()->ToSubsystem());
		pAction->UpdateTopInstance(pInst);
		pAction->InstanceCreate(pInst);
		sysCallbackList.PopFront();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::InstanceDestroy(AnimStateInstance* pInst)
{
	SubsystemList sysCallbackList;
	sysCallbackList.SetAllocator(&m_subsystemListPool);

	SubsystemList sysKillList;
	sysKillList.SetAllocator(&m_subsystemListPool);

	{
		auto pAnimBindingList = m_subsystemAnimBindingList.WLock();	// Lock needed

		// Pass the Destroy event to all subsystems bound to this instance, and kill the subsystems if it's no longer bound to any instances
		for (int i=0; i<pAnimBindingList->m_numInstanceBindings; i++)
		{
			AnimStateInstanceBinding* pBinding = &pAnimBindingList->m_pInstanceBindings[i];
			if (pBinding->m_instId == pInst->GetId() && pBinding->m_layerId == pInst->GetLayer()->GetName())
			{
				NdSubsystemAnimAction* pAction = pBinding->m_hAction.ToSubsystem();
				GAMEPLAY_ASSERT(pAction != nullptr);

				if (pAction->GetSubsystemState() == NdSubsystem::State::kActive)
					sysCallbackList.PushBack(pAction);

				bool finalInstance = true;
				for (int j=0; j<pAnimBindingList->m_numInstanceBindings; j++)
				{
					if (i == j)
						continue;

					AnimStateInstanceBinding* pOtherBinding = &pAnimBindingList->m_pInstanceBindings[j];
					if (!pOtherBinding->m_destroyed && pAction == pOtherBinding->m_hAction.ToSubsystem())
						finalInstance = false;
				}

				if (finalInstance)
				{
					bool shouldKill = true;
					if (pAction->IsKindOf(SID("NdSubsystemAnimController")))
					{
						// Some anim controllers are persistent, and only get destroyed with the Owner
						NdSubsystemAnimController* pController = static_cast<NdSubsystemAnimController*>(pAction);
						if (pController->IsPersistent())
							shouldKill = false;
					}

					if (shouldKill)
						sysKillList.PushBack(pAction);
					else
						pAction->SetActionState(NdSubsystemAnimAction::ActionState::kUnattached);
				}

				pBinding->m_destroyed = true;
			}
		}
	}

	// Call callbacks outside of m_subsystemAnimBindingList lock
	while (!sysCallbackList.IsEmpty())
	{
		NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(sysCallbackList.Front()->ToSubsystem());
		pAction->InstanceDestroy(pInst);
		sysCallbackList.PopFront();
	}

	// Call callbacks outside of m_subsystemAnimBindingList lock
	while (!sysKillList.IsEmpty())
	{
		NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(sysKillList.Front()->ToSubsystem());
		pAction->Kill();
		sysKillList.PopFront();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::InstancePendingChange(StringId64 layerId,
										   StateChangeRequest::ID requestId,
										   StringId64 changeId,
										   int changeType)
{
	if (FALSE_IN_FINAL_BUILD(m_debugAnimTransitions))
	{
		NdAnimActionDebugSubsystem* pAction = (NdAnimActionDebugSubsystem*)
			NdSubsystem::Create(NdSubsystem::Alloc::kSubsystemHeap,
								SubsystemSpawnInfo(SID("NdAnimActionDebugSubsystem"), GetOwner()),
								FILE_LINE_FUNC);

		if (pAction)
		{
			pAction->InstancePendingChange(layerId, requestId, changeId, changeType);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdSubsystemMgr::InstanceAlignFunc(const AnimStateInstance* pInst,
									   const BoundFrame& prevAlign,
									   const BoundFrame& currAlign,
									   const Locator& apAlignDelta,
									   BoundFrame* pAlignOut,
									   bool debugDraw)
{
	// Gather Subsystems bound to this inst
	SubsystemList sysCallbackList;
	sysCallbackList.SetAllocator(&m_subsystemListPool);

	{
		auto pAnimBindingList = m_subsystemAnimBindingList.RLock();	// Lock needed

		for (int i = pAnimBindingList->m_numInstanceBindings - 1; i >= 0; i--)
		{
			AnimStateInstanceBinding* pBinding = &pAnimBindingList->m_pInstanceBindings[i];
			if (pBinding->m_instId == pInst->GetId() && pBinding->m_layerId == pInst->GetLayer()->GetName())
			{
				GAMEPLAY_ASSERT(!pBinding->m_destroyed);

				NdSubsystemAnimAction* pAction = pBinding->m_hAction.ToSubsystem();
				GAMEPLAY_ASSERT(pAction != nullptr);

				if (pAction->GetSubsystemState() == NdSubsystem::State::kActive)
					sysCallbackList.PushBack(pAction);
			}
		}
	}

	// Call callbacks outside of m_subsystemAnimBindingList lock
	bool ret = false;
	while (!sysCallbackList.IsEmpty())
	{
		if (!ret)
		{
			NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(sysCallbackList.Front()->ToSubsystem());
			ret = pAction->InstanceAlignFunc(pInst, prevAlign, currAlign, apAlignDelta, pAlignOut, debugDraw);
		}
		sysCallbackList.PopFront();
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::InstanceIkFunc(const AnimStateInstance* pInst,
									AnimPluginContext* pPluginContext,
									const AnimSnapshotNodeSubsystemIK::SubsystemIkPluginParams* pParams)
{
	// Gather Subsystems bound to this inst
	SubsystemList sysCallbackList;
	sysCallbackList.SetAllocator(&m_subsystemListPool);

	{
		auto pAnimBindingList = m_subsystemAnimBindingList.RLock();	// Lock needed

		if (pParams->m_subsystemType != INVALID_STRING_ID_64)
		{
			const StringId64 layerId = pParams->m_subsystemLayer;

			// Callback to a specific subsystem instead of those bound to this inst
			auto pAnimActionList = m_subsystemAnimActionList.RLock();
			for (auto hSys : pAnimActionList.GetUnsafeReference())
			{
				NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(hSys.ToSubsystem());
				GAMEPLAY_ASSERT(pAction != nullptr);

				if (pAction->GetType() == pParams->m_subsystemType && (layerId == INVALID_STRING_ID_64 || layerId == pAction->GetLayerId()))
					sysCallbackList.PushBack(pAction);
			}
		}
		else
		{
			for (int i = pAnimBindingList->m_numInstanceBindings - 1; i >= 0; i--)
			{
				AnimStateInstanceBinding* pBinding = &pAnimBindingList->m_pInstanceBindings[i];
				if (pBinding->m_destroyed)
					continue;

				NdSubsystemAnimAction* pAction = pBinding->m_hAction.ToSubsystem();
				GAMEPLAY_ASSERT(pAction != nullptr);

				if (pBinding->m_instId == pInst->GetId() && pBinding->m_layerId == pInst->GetLayer()->GetName())
				{
					if (pAction->GetSubsystemState() == NdSubsystem::State::kActive)
						sysCallbackList.PushBack(pAction);
				}
			}
		}
	}

	// Call callbacks outside of m_subsystemAnimBindingList lock
	while (!sysCallbackList.IsEmpty())
	{
		NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(sysCallbackList.Front()->ToSubsystem());
		pAction->InstanceIkFunc(pInst, pPluginContext, pParams);
		sysCallbackList.PopFront();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::ForEachInstanceAction(AnimStateInstance* pInst,
										   InstanceActionCallback* pCallback,
										   uintptr_t userData)
{
	if (!pCallback || !pInst)
		return;

	// Gather Subsystems bound to this inst
	SubsystemList sysCallbackList;
	sysCallbackList.SetAllocator(&m_subsystemListPool);

	{
		auto pAnimBindingList = m_subsystemAnimBindingList.RLock();	// Lock needed

		for (int i = pAnimBindingList->m_numInstanceBindings - 1; i >= 0; i--)
		{
			AnimStateInstanceBinding* pBinding = &pAnimBindingList->m_pInstanceBindings[i];
			if (pBinding->m_instId == pInst->GetId() && pBinding->m_layerId == pInst->GetLayer()->GetName())
			{
				GAMEPLAY_ASSERT(!pBinding->m_destroyed);

				NdSubsystemAnimAction* pAction = pBinding->m_hAction.ToSubsystem();
				GAMEPLAY_ASSERT(pAction != nullptr);

				if (pAction->GetSubsystemState() == NdSubsystem::State::kActive)
					sysCallbackList.PushBack(pAction);
			}
		}
	}

	// Call callbacks outside of m_subsystemAnimBindingList lock
	while (!sysCallbackList.IsEmpty())
	{
		NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(sysCallbackList.Front()->ToSubsystem());
		(*pCallback)(pInst, pAction, userData);
		sysCallbackList.PopFront();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::InstanceDebugPrintFunc(const AnimStateInstance* pInst, StringId64 debugType, IStringBuilder* pText)
{
	if (!g_ndSubsystemDebugDrawInAnimTree)
		return;

	auto pAnimBindingList = m_subsystemAnimBindingList.RLock();	// Lock needed

	bool foundSubsystem = false;

	auto pAnimActionList = m_subsystemAnimActionList.RLock();
	for (auto hSys : pAnimActionList.GetUnsafeReference())
	{
		NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(hSys.ToSubsystem());
		GAMEPLAY_ASSERT(pAction != nullptr);

		if (IsSubsystemBoundToAnimInstance(pInst, pAction))
		{
			if (pAction->GetType() == SID("NdAnimActionDebugSubsystem"))
				continue;

			if (debugType == SID("SubsystemList") && pAction->IsKindOf(SID("NdSubsystemAnimController")))
				continue;

			if (debugType == SID("SubsystemController") && !pAction->IsKindOf(SID("NdSubsystemAnimController")))
				continue;

			if (foundSubsystem)
				pText->append(", ");

			StringBuilder<256> desc;
			if (pAction->GetAnimControlDebugText(pInst, &desc))
			{
				pText->append(desc.c_str());
			}
			else
			{
				pText->append(pAction->GetName());
			}

			foundSubsystem = true;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::HandleTriggeredEffect(const EffectAnimInfo* pEffectAnimInfo)
{
	SubsystemList sysCallbackList;
	sysCallbackList.SetAllocator(&m_subsystemListPool);

	{
		auto pAnimBindingList = m_subsystemAnimBindingList.RLock();	// Lock needed

		// Pass the Create event to all subsystems bound to this instance
		for (int i = 0; i < pAnimBindingList->m_numInstanceBindings; i++)
		{
			AnimStateInstanceBinding* pBinding = &pAnimBindingList->m_pInstanceBindings[i];
			if (pBinding->m_instId == pEffectAnimInfo->m_instId && pBinding->m_layerId == pEffectAnimInfo->m_layerId)
			{
				NdSubsystemAnimAction* pAction = pBinding->m_hAction.ToSubsystem();
				GAMEPLAY_ASSERT(pAction != nullptr);

				if (pAction->GetSubsystemState() == NdSubsystem::State::kActive)
					sysCallbackList.PushBack(pAction);
			}
		}
	}

	// Call callbacks outside of m_subsystemAnimBindingList lock
	while (!sysCallbackList.IsEmpty())
	{
		NdSubsystemAnimAction* pAction = static_cast<NdSubsystemAnimAction*>(sysCallbackList.Front()->ToSubsystem());
		pAction->HandleTriggeredEffect(pEffectAnimInfo);
		sysCallbackList.PopFront();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemAnimAction::InstanceIterator NdSubsystemMgr::GetInstanceIterator(U32 subsystemId, int startIndex) const
{
	auto pAnimBindingList = m_subsystemAnimBindingList.RLock();	// Lock needed

	for (int i = startIndex; i < pAnimBindingList->m_numInstanceBindings; i++)
	{
		AnimStateInstanceBinding* pBinding = &pAnimBindingList->m_pInstanceBindings[i];
		if (pBinding->m_subsystemId == subsystemId && pBinding->m_instId != INVALID_ANIM_INSTANCE_ID && !pBinding->m_destroyed)
			return NdSubsystemAnimAction::InstanceIterator(m_pOwner, subsystemId, i, pBinding->m_instId);
	}

	return NdSubsystemAnimAction::InstanceIterator();
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimStateInstance* NdSubsystemMgr::GetInstance(const NdSubsystemAnimAction::InstanceIterator& it) const
{
	if (it.m_subsystemMgrBindingIndex < 0)
		return nullptr;

	auto pAnimBindingList = m_subsystemAnimBindingList.RLock();	// Lock needed

	GAMEPLAY_ASSERT(this == it.m_pOwner->GetSubsystemMgr());
	GAMEPLAY_ASSERT(it.m_subsystemMgrBindingIndex < pAnimBindingList->m_numInstanceBindings);

	AnimStateInstanceBinding* pBinding = &pAnimBindingList->m_pInstanceBindings[it.m_subsystemMgrBindingIndex];
	GAMEPLAY_ASSERT(it.m_subsystemId == pBinding->m_subsystemId);
	GAMEPLAY_ASSERT(it.m_instId == pBinding->m_instId);

	AnimStateLayer* pLayer = m_pOwner->GetAnimControl()->GetStateLayerById(pBinding->m_layerId);
	GAMEPLAY_ASSERT(pLayer != nullptr);

	AnimStateInstance* pInst = pLayer->GetInstanceById(pBinding->m_instId);

	return pInst;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdSubsystemAnimAction::InstanceIterator NdSubsystemMgr::NextIterator(const NdSubsystemAnimAction::InstanceIterator& it) const
{
	if (it.m_subsystemMgrBindingIndex < 0)
		return NdSubsystemAnimAction::InstanceIterator();

	auto pAnimBindingList = m_subsystemAnimBindingList.RLock();	// Lock needed

	GAMEPLAY_ASSERT(this == it.m_pOwner->GetSubsystemMgr());
	GAMEPLAY_ASSERT(it.m_subsystemMgrBindingIndex < pAnimBindingList->m_numInstanceBindings);

	AnimStateInstanceBinding* pBinding = &pAnimBindingList->m_pInstanceBindings[it.m_subsystemMgrBindingIndex];
	GAMEPLAY_ASSERT(it.m_subsystemId == pBinding->m_subsystemId);
	GAMEPLAY_ASSERT(it.m_instId == pBinding->m_instId);

	return GetInstanceIterator(it.m_subsystemId, it.m_subsystemMgrBindingIndex+1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdSubsystemMgr::IsSubsystemBoundToAnimInstance(const AnimStateInstance* pInst,
													const NdSubsystemAnimAction* pAction) const
{
	if (pInst->GetLayerId() != pAction->GetLayerId())
		return false;

	const AnimStateInstance::ID instId = pInst->GetId();
	auto pAnimBindingList = m_subsystemAnimBindingList.RLock();	// Lock needed
	for (int i = 0; i < pAnimBindingList->m_numInstanceBindings; i++)
	{
		if (pAnimBindingList->m_pInstanceBindings[i].m_instId == instId &&
			pAnimBindingList->m_pInstanceBindings[i].m_destroyed == false &&
			pAnimBindingList->m_pInstanceBindings[i].m_hAction.ToSubsystem() == pAction)
		{
			return true;
		}
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::Validate()
{
#ifdef ND_SUBSYSTEM_VALIDATION
	auto pAnimBindingList = m_subsystemAnimBindingList.RLock();

	auto pMasterList = m_subsystemMasterList.RLock();
	for (int i=0; i<pMasterList->m_numSubsystems; i++)
	{
		NdSubsystem* pSys = pMasterList->m_pSubsystems[i].ToSubsystem();
		if (pSys->GetOwnerGameObject() == nullptr)
			NdSubsystem::DumpSubsystemHeap();
		GAMEPLAY_ASSERT(pSys->GetOwnerGameObject() != nullptr);
	}

#ifdef RBRONER
	//if (pAnimBindingList->m_numInstanceBindings > 0)
	//{
	//	MsgCon("Subsystem Mgr Bindings: %d\n", pAnimBindingList->m_numInstanceBindings);
	//}

#endif
	for (int i = 0; i < pAnimBindingList->m_numInstanceBindings; i++)
	{
		AnimStateInstanceBinding* pBinding = &pAnimBindingList->m_pInstanceBindings[i];
		NdSubsystemAnimAction* pAction = pBinding->m_hAction.ToSubsystem();
		GAMEPLAY_ASSERT(pAction);

		if (pBinding->m_destroyed || pAction->GetSubsystemState() == NdSubsystem::State::kKilled)
			continue;

		GAMEPLAY_ASSERT(pAction->GetActionState() != NdSubsystemAnimAction::ActionState::kInvalid);

		if (pAction->GetActionState() == NdSubsystemAnimAction::ActionState::kPending)
		{
			GAMEPLAY_ASSERT(pBinding->m_layerId != INVALID_STRING_ID_64);
			GAMEPLAY_ASSERT(pBinding->m_requestId != StateChangeRequest::kInvalidId);
			GAMEPLAY_ASSERT(pBinding->m_instId == INVALID_ANIM_INSTANCE_ID);
		}

		if (pAction->GetActionState() == NdSubsystemAnimAction::ActionState::kTop)
		{
			GAMEPLAY_ASSERT(pBinding->m_layerId != INVALID_STRING_ID_64);
			GAMEPLAY_ASSERT(pBinding->m_instId != INVALID_ANIM_INSTANCE_ID);
		}
	}
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::DebugPrint() const
{
	STRIP_IN_FINAL_BUILD;

	if (m_subsystemMasterList->m_numSubsystems == 0)
		return;

	const NdSubsystemAnimController* pSys = GetOwner()->GetActiveSubsystemController();
	MsgCon("Active Subsystem Controller: %s\n", pSys ? pSys->GetName() : "<none>");

	AnimStateLayer* pBaseLayer = GetOwner()->GetAnimControl()->GetBaseStateLayer();
	U32 activeBaseSubsystemId = pBaseLayer->GetActiveSubsystemControllerId();

	if (activeBaseSubsystemId == NdSubsystem::kInvalidSubsystemId)
	{
		MsgCon("Base Layer Active Subsystem: <none>\n");
	}
	else
	{
		NdSubsystem* pBaseLayerSys = FindSubsystemById(activeBaseSubsystemId);
		if (pBaseLayerSys)
			MsgCon("Base Layer Active Subsystem: %s\n", pBaseLayerSys->GetName());
		else
			MsgCon("Base Layer Active Subsystem: <%i> (not found)\n", activeBaseSubsystemId);
	}

	DebugPrintTree();
	DebugPrintAnimTransitionDebug();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::DebugPrintTree() const
{
	STRIP_IN_FINAL_BUILD;

	auto pMasterList = m_subsystemMasterList.RLock();

	MsgCon("----------------------------------------\n");
	for (int i = 0; i < pMasterList->m_numSubsystems; i++)
	{
		NdSubsystem* pSys = pMasterList->m_pSubsystems[i].ToSubsystem();
		GAMEPLAY_ASSERT(pSys);

		if (pSys->GetParent() == nullptr && !pSys->m_debugPrintMark)
			DebugPrintTreeDepthFirst(pSys, 0);
	}
	MsgCon("----------------------------------------\n");

	for (int i = 0; i < pMasterList->m_numSubsystems; i++)
	{
		NdSubsystem* pSys = pMasterList->m_pSubsystems[i].ToSubsystem();
		pSys->m_debugPrintMark = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::DebugPrintTreeDepthFirst(NdSubsystem* pSys, int depth) const
{
	STRIP_IN_FINAL_BUILD;

	if (pSys->GetSubsystemState() == NdSubsystem::State::kKilled)
		return;

	if (pSys->GetType() == SID("NdAnimActionDebugSubsystem"))
		return;

	StringBuilder<1024> desc;

	for (int i = 0; i < depth; i++)
	{
		desc.append("  ");
	}

	desc.append(pSys->GetName());

	StringBuilder<1024> desc2;
	if (pSys->GetQuickDebugText(&desc2))
	{
		desc.append_format(" %s", desc2.c_str());
	}

	if (g_ndSubsystemDebugDrawTreeFileLine)
	{
		desc.append_format(" - %s (%d)", pSys->m_debugFile, pSys->m_debugLine);
	}

	desc.append("\n");

	Color color = pSys->GetQuickDebugColor();
	SetColor(kMsgCon, color.ToAbgr8());
	MsgCon(desc.c_str());
	SetColor(kMsgCon, kColorWhite.ToAbgr8());

	GAMEPLAY_ASSERT(pSys->m_debugPrintMark == false);
	pSys->m_debugPrintMark = true;

	NdSubsystem* pChildSys = pSys->m_hChild.ToSubsystem();
	while (pChildSys)
	{
		DebugPrintTreeDepthFirst(pChildSys, depth + 1);
		pChildSys = pChildSys->m_hSiblingNext.ToSubsystem();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::DebugPrintAnimTransitionDebug() const
{
	auto pMasterList = m_subsystemMasterList.RLock();

	int numDebugSubsystems = 0;
	for (int i = pMasterList->m_numSubsystems - 1; i >= 0; i--)
	{
		NdSubsystem* pSys = pMasterList->m_pSubsystems[i].ToSubsystem();
		if (pSys->GetSubsystemState() == NdSubsystem::State::kKilled)
			continue;

		if (pSys->GetType() == SID("NdAnimActionDebugSubsystem"))
		{
			if (numDebugSubsystems == 0)
				MsgCon("----------------------------------------\n");
			numDebugSubsystems++;

			NdAnimActionDebugSubsystem* pAction = static_cast<NdAnimActionDebugSubsystem*>(pSys);

			if (pAction->IsPending())
			{
				if (pAction->m_changeType == kPendingChangeFadeToState)
				{
					MsgCon("Queued FadeToStade: %s\n", DevKitOnly_StringIdToString(pAction->m_changeId));
				}
				else if (pAction->m_changeType == kPendingChangeRequestTransition)
				{
					MsgCon("Queued RequestTransition: %s\n", DevKitOnly_StringIdToString(pAction->m_changeId));
				}
				else
				{
					MsgCon("Queued: ?\n");
				}
			}
			else
			{
				AnimStateInstance* pInst = pAction->GetInstanceStart();

				MsgCon("AnimState: %s\n", pInst ? DevKitOnly_StringIdToString( pInst->GetStateName() ) : "<null>");

				if (pAction->m_changeType == kPendingChangeFadeToState)
				{
					MsgCon("  From FadeToStade: %s\n", DevKitOnly_StringIdToString(pAction->m_changeId));
				}
				else if (pAction->m_changeType == kPendingChangeRequestTransition)
				{
					MsgCon("  From RequestTransition: %s\n", DevKitOnly_StringIdToString(pAction->m_changeId));
				}
				else
				{
					MsgCon("  From Auto Transition\n");
				}
			}
		}
	}

	if (numDebugSubsystems > 0)
		MsgCon("----------------------------------------\n");
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemMgr::EnterNewParentSpace(const Transform& matOldToNew,
										 const Locator& oldParentSpace,
										 const Locator& newParentSpace)
{
	auto pMasterList = m_subsystemMasterList.WLock();

	for (int i = 0; i < pMasterList->m_numSubsystems; i++)
	{
		NdSubsystem* pSubsystem = pMasterList->m_pSubsystems[i].ToSubsystem();
		if (pSubsystem)
		{
			pSubsystem->EnterNewParentSpace(matOldToNew, oldParentSpace, newParentSpace);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdSubsystemControllerDefault::Init(const SubsystemSpawnInfo& info)
{
	Err result = ParentClass::Init(info);
	if (result.Failed())
		return result;

	SetActionState(ActionState::kUnattached);
	SetPersistent(true);

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdSubsystemControllerDefault::RequestRefreshAnimState(const FadeToStateParams* pFadeToStateParams, bool allowStompOfInitialBlend)
{
	const StringId64 layerId = GetLayerId();

	NdGameObject* pGo = GetOwnerGameObject();
	AnimControl* pAnimControl = pGo ? pGo->GetAnimControl() : nullptr;
	AnimStateLayer* pLayer = pAnimControl ? pAnimControl->GetStateLayerById(layerId) : nullptr;

	AnimStateInstance* pInst = pLayer->CurrentStateInstance();
	U32 controllerId = pInst->GetSubsystemControllerId();
	const U32 subSysId = GetSubsystemId();

#ifdef RBRONER
	GAMEPLAY_ASSERT(controllerId == 0 || subSysId == controllerId);
#endif

	if (controllerId != 0 && subSysId != controllerId)
		return;

	if (pLayer && !pLayer->AreTransitionsPending())
	{

		if (pLayer->IsTransitionValid(SID("self")))
		{
			FadeToStateParams params;
			params.m_subsystemControllerId = subSysId;
			pLayer->RequestTransition(SID("self"), &params);
		}
		else
		{
			const StringId64 oldState = pLayer->CurrentStateId();
			const float phase = pLayer->CurrentStateInstance()->Phase();

			FadeToStateParams params;
			params.m_stateStartPhase = phase;
			params.m_animFadeTime = 0.2f;
			params.m_motionFadeTime = 0.2f;

			if (pFadeToStateParams)
			{
				params = *pFadeToStateParams;
			}

			params.m_subsystemControllerId = subSysId;

			pLayer->FadeToState(oldState, params);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
TYPE_FACTORY_REGISTER(NdSubsystemControllerDefault, NdSubsystemAnimController);
