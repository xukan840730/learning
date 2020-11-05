/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/character-motion-match-locomotion-debugger.h"

#include "ndlib/process/process-mgr.h"
#include "ndlib/process/process-spawn-info.h"

#include "gamelib/gameplay/character-motion-match-locomotion.h"

/// --------------------------------------------------------------------------------------------------------------- ///
PROCESS_REGISTER(CharacterMotionMatchLocomotionDebuggerProcess, Process);

Synchronized<CharacterMotionMatchLocomotionDebugger, NdRwAtomicLock64> g_motionMatchLocomotionDebugger;

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotionDebugger::RegisterController(CharacterMotionMatchLocomotion* pController)
{
	STRIP_IN_FINAL_BUILD;

	if (!m_hDebugger.HandleValid())
	{
		ProcessSpawnInfo spawnInfo(SID("CharacterMotionMatchLocomotionDebuggerProcess"),
								   EngineComponents::GetProcessMgr()->m_pTaskTree);
		m_hDebugger = static_cast<CharacterMotionMatchLocomotionDebuggerProcess*>(NewProcess(spawnInfo));
	}

	if (CharacterMotionMatchLocomotionDebuggerProcess* pDebugger = m_hDebugger.ToMutableProcess())
	{
		AllocateJanitor jan(kAllocDebug, FILE_LINE_FUNC);

		const size_t allocSize	  = sizeof(CharacterMotionMatchLocomotionDebuggerProcess::DebugInfo);
		const Alignment alignment = Alignment(ALIGN_OF(CharacterMotionMatchLocomotionDebuggerProcess::DebugInfo));

		Memory::Allocator* pAllocator = Memory::TopAllocator();

		if (!pDebugger->m_controllerList.IsFull() && pAllocator && pAllocator->CanAllocate(allocSize, alignment))
		{
			CharacterMotionMatchLocomotionDebuggerProcess::DebugInfo* pDebugInfo = nullptr;
			pDebugInfo = NDI_NEW CharacterMotionMatchLocomotionDebuggerProcess::DebugInfo(pController);
			pDebugger->m_controllerList.PushBack(pDebugInfo);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotionDebugger::UnregisterController(CharacterMotionMatchLocomotion* pController)
{
	STRIP_IN_FINAL_BUILD;
	if (m_hDebugger.HandleValid())
	{
		auto& list = m_hDebugger.ToMutableProcess()->m_controllerList;
		for (int i = list.Size() - 1; i >= 0; --i)
		{
			if (list[i]->m_hController.ToSubsystem() == pController)
			{
				{
					AllocateJanitor jan(kAllocDebug, FILE_LINE_FUNC);
					NDI_DELETE list[i];
				}
				list.EraseAndMoveLast(&list[i]);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotionDebugger::RecordData(CharacterMotionMatchLocomotion* pController,
														const CharacterMotionMatchLocomotion::RecordedState& state)
{
	if (m_hDebugger.HandleValid())
	{
		auto& list = m_hDebugger.ToProcess()->m_controllerList;
		for (auto it = list.begin(); it != list.End(); ++it)
		{
			auto& pDebug = *it;
			if (pDebug->m_hController.ToSubsystem() == pController)
			{
				if (pDebug->m_queue.IsFull())
				{
					pDebug->m_queue.Drop(1);
				}
				pDebug->m_queue.Enqueue(state);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err CharacterMotionMatchLocomotionDebuggerProcess::Init(const ProcessSpawnInfo& info)
{
	auto result = ParentClass::Init(info);
	SetUpdateEnabled(true);
	SetStopWhenPaused(false);
	m_controllerList.Init(16, FILE_LINE_FUNC);
	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CharacterMotionMatchLocomotionDebuggerProcess::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	m_controllerList.Relocate(deltaPos, lowerBound, upperBound);
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
CharacterMotionMatchLocomotionDebuggerProcess::~CharacterMotionMatchLocomotionDebuggerProcess()
{
	AllocateJanitor jan(kAllocDebug, FILE_LINE_FUNC);
	for (int i = 0; i < m_controllerList.Size(); ++i)
	{
		NDI_DELETE m_controllerList[i];
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
class CharacterMotionMatchLocomotionDebuggerProcess::Active : public Process::State
{
public:
	BIND_TO_PROCESS(CharacterMotionMatchLocomotionDebuggerProcess);

	virtual void Update() override
	{
		CharacterMotionMatchLocomotionDebuggerProcess& debugger = Self();

		for (int i = 0; i < debugger.m_controllerList.Size(); ++i)
		{
			if (auto pController = debugger.m_controllerList[i]->m_hController.ToSubsystem())
			{
				if (g_ndConfig.m_pDMenuMgr->GetNumBackwardSteps() > 0)
				{
					const auto& queue = debugger.m_controllerList[i]->m_queue;
					int index = queue.GetCount() - g_ndConfig.m_pDMenuMgr->GetNumBackwardSteps();
					if (index >= 0)
					{
						pController->DebugUpdate(&queue.GetAt(index));
					}
					else
					{
						//The controller wasn't alive in the past so dont debug it
					}
				}
				else
				{
					pController->DebugUpdate(nullptr);
				}
			}
		}
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
STATE_REGISTER(CharacterMotionMatchLocomotionDebuggerProcess, Active, kPriorityNormal);
