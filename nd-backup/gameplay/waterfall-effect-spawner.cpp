/*
 * Copyright (c) 2013 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/waterfall-effect-spawner.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/ndphys/collision-cast.h"
#include "gamelib/ndphys/collision-filter.h"
#include "gamelib/render/particle/particle.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/process.h"
#include "ndlib/process/spawn-info.h"

class ProcessSpawnInfo;

Err WaterfallEffectSpawner::Init(const ProcessSpawnInfo& spawnInfo)
{
	SpawnInfo& spawn = (SpawnInfo&)spawnInfo;

	AssociateWithLevel(spawn.GetLevel());

	Err err = ParentClass::Init(spawn);

	if (err != Err::kOK)
		return err;

	m_height = spawn.GetData<float>(SID("height"), 0.0f);
				
	ChangeParentProcess( EngineComponents::GetProcessMgr()->m_pEffectTree );

	return err;
}

void WaterfallEffectSpawner::ProcessUpdate()
{
	ParentClass::ProcessUpdate();
		
	NdGameObject* pPlayerGo = EngineComponents::GetNdGameInfo()->GetPlayerGameObject();
	if (!pPlayerGo)
		return;

	if (Dist(pPlayerGo->GetTranslation(), GetTranslation()) > 30.0f)
		return;

	Point particleLoc = GetTranslation();
	if (m_rayJob.IsValid())
	{
		m_rayJob.Wait();
		if (m_rayJob.NumContacts(0))
		{
			particleLoc = m_rayJob.GetContactPoint(0,0);
		}
		m_rayJob.Close();
	}
		
	g_particleMgr.SetPos(m_hParticle, particleLoc);

	m_rayJob.Open(1, 1, 0);
	m_rayJob.SetFilterForAllProbes(CollideFilter(Collide::kLayerMaskGeneral | Collide::kLayerMaskPlayer | Collide::kLayerMaskNpc));
	m_rayJob.SetProbeExtents(0, GetTranslation() + Vector(0.0f,m_height,0.0f), GetTranslation());
	m_rayJob.Kick(FILE_LINE_FUNC, 1);
}

PROCESS_REGISTER(WaterfallEffectSpawner, ParticleSpawner);
PROCESS_REGISTER_ALIAS(ProcessWaterfallEffect, WaterfallEffectSpawner); // for backwards compatibility
