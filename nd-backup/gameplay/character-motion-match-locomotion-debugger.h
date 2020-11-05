/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/list-array.h"
#include "corelib/containers/ringqueue.h"
#include "corelib/system/read-write-atomic-lock.h"
#include "corelib/system/synchronized.h"

#include "ndlib/process/process.h"
#include "ndlib/util/jaf-anim-recorder-manager.h"

#include "gamelib/gameplay/character-motion-match-locomotion.h"
#include "gamelib/gameplay/nd-subsystem.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class CharacterMotionMatchLocomotion;

/// --------------------------------------------------------------------------------------------------------------- ///
class CharacterMotionMatchLocomotionDebuggerProcess : public Process
{
public:
	STATE_DECLARE_OVERRIDE(Active);

	virtual Err Init(const ProcessSpawnInfo& info) override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	~CharacterMotionMatchLocomotionDebuggerProcess();
private:
	using ParentClass = Process;
	friend class CharacterMotionMatchLocomotionDebugger;

	struct DebugInfo
	{
		DebugInfo(CharacterMotionMatchLocomotion* pController)
			: m_hController(pController)
		{}

		TypedSubsystemHandle<CharacterMotionMatchLocomotion> m_hController;
		StaticRingQueue<CharacterMotionMatchLocomotion::RecordedState, JAFAnimRecorderManager::kMaxRecordedFrames> m_queue;
	};

	ListArray<DebugInfo*> m_controllerList;
};

FWD_DECL_PROCESS_HANDLE(CharacterMotionMatchLocomotionDebuggerProcess);

/// --------------------------------------------------------------------------------------------------------------- ///
class CharacterMotionMatchLocomotionDebugger
{
public:
	void RegisterController(CharacterMotionMatchLocomotion* pController);
	void UnregisterController(CharacterMotionMatchLocomotion* pController);
	void RecordData(CharacterMotionMatchLocomotion* pController, const CharacterMotionMatchLocomotion::RecordedState& state);

private:
	MutableCharacterMotionMatchLocomotionDebuggerProcessHandle m_hDebugger;
};

/// --------------------------------------------------------------------------------------------------------------- ///
extern Synchronized<CharacterMotionMatchLocomotionDebugger, NdRwAtomicLock64> g_motionMatchLocomotionDebugger;
