/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/gameplay/ai/controller/animaction-controller.h"

class CoverActionPack;

/// --------------------------------------------------------------------------------------------------------------- ///
struct IAiInfectedController : public AnimActionController
{
	virtual bool IsPartialBusy() const = 0;

	virtual bool StartSleep() = 0;
	virtual bool SleepStir()  = 0;

	virtual bool WanderStir() = 0;

	virtual bool ThrowPartial() = 0;

	virtual bool IsPartialThrowActive() const = 0;

	virtual StringId64 GetNextThrowAnimId() const = 0;
	virtual StringId64 GetNextThrowProjectileSpawnAnimId() const = 0;

	virtual bool Throw(const Vector* pDir = nullptr) = 0;
	virtual void PickNextThrowAnim() = 0;

	virtual void InterruptPerformance() = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiInfectedController* CreateAiInfectedController();
