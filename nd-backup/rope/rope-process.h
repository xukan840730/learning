/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef ROPE_PROCESS_H 
#define ROPE_PROCESS_H

#include "gamelib/gameplay/nd-locatable.h"
#include "gamelib/ndphys/rope/rope2.h"

class RopeProcess: public NdLocatableObject
{
private:
	typedef NdLocatableObject ParentClass;

public:
	STATE_DECLARE_OVERRIDE(Active);

	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound) override;
	void PostAnimBlending_Async();
	void PostJointUpdate_Async();
	void ProcessMouse();
	void OnKillProcess() override;
	virtual void EventHandler(Event& event) override;

	Rope2 m_rope;

protected:
	bool m_bInited;
	bool m_bPinnedStrained;
	F32 m_draggedRopeDist;
	F32 m_pinnedRopeDist;
	Point m_pinnedRopePoint;
};

PROCESS_DECLARE(RopeProcess);

#endif // ROPE_PROCESS_H 

