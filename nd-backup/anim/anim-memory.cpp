/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-memory.h"

#include "ndlib/anim/anim-chain.h"
#include "ndlib/anim/anim-dummy-instance.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/process/clock.h"


/// --------------------------------------------------------------------------------------------------------------- ///
void InitializeAnimationSystem()
{
	SetAnimationClock(&EngineComponents::GetNdFrameState()->m_clock[kGameClock]);
	AnimChain::StartUp();
	AnimDummyInstance::StartUp();
}


/// --------------------------------------------------------------------------------------------------------------- ///
void ShutdownAnimationSystem()
{
	AnimDummyInstance::ShutDown();
	AnimChain::ShutDown();
}
