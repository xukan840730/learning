/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef _NDLIB_WATERFALL_EFFECT_SPAWNER_H_
#define _NDLIB_WATERFALL_EFFECT_SPAWNER_H_

#include "gamelib/gameplay/particle-spawner.h"
#include "gamelib/ndphys/collision-cast.h"

class ProcessSpawnInfo;

class WaterfallEffectSpawner : public ParticleSpawner
{
public:
	typedef ParticleSpawner ParentClass;

	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual void ProcessUpdate() override;
	
private:
	float				m_height;
	RayCastJob			m_rayJob;
};

#endif // #ifndef _NDLIB_WATERFALL_EFFECT_SPAWNER_H_
