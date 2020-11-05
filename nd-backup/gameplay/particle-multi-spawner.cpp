/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/particle-multi-spawner.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/nd-frame-state.h"
#include "ndlib/process/event.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/particle/particle-debug.h"
#include "ndlib/water/waterflow.h"

#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/render/particle/particle-handle.h"
#include "gamelib/render/particle/particle.h"

PROCESS_REGISTER(ParticleMultiSpawner, NdLocatableObject);
PROCESS_REGISTER_ALIAS(PartMultiSpawner, ParticleMultiSpawner); // backwards compat

FROM_PROCESS_DEFINE(ParticleMultiSpawner);

Err ParticleMultiSpawner::Init(const ProcessSpawnInfo& spawn)
{
	Err result = ParentClass::Init(spawn);
	if (result.Failed())
	{
		return result;
	}

	m_hidden = 0;
	m_refreshCounter = -1;
	m_hParticle = 0;

	SetUserId(spawn.BareNameId(GetUserBareId()), spawn.NamespaceId(GetUserNamespaceId()), spawn.NameId(GetUserId()));
	I32F count = spawn.GetDataArraySize(SID("part-spawner"));

	if (!count)
		return Err::kErrBadData;

	m_cameraAttach = (spawn.GetData<bool>(SID("camera-attach"), false)) ? 1 : 0;
	m_cameraOrient = (spawn.GetData<bool>(SID("camera-orient"), false)) ? 1 : 0;
	m_linger = (spawn.GetData<bool>(SID("particle-linger"), false)) ? 1 : 0;
	m_trackWater = (spawn.GetData<bool>(SID("track-water"), false)) ? 1 : 0;
	m_spawnMoveUp = spawn.GetData<StringId64>(SID("spawn-move-up"), INVALID_STRING_ID_64);
	m_spawnMoveDown = spawn.GetData<StringId64>(SID("spawn-move-down"), INVALID_STRING_ID_64);
	m_spawnUpSpeed = spawn.GetData<float>(SID("spawn-up-speed"), 0.0f);
	m_spawnDownSpeed = spawn.GetData<float>(SID("spawn-down-speed"), 0.0f);

	m_color = Vector(1.0f, 1.0f, 1.0f); 
	m_alpha = 1.0f;
	m_timeScale = 1.0f;

	bool triggerSpawn = ((m_spawnMoveUp != INVALID_STRING_ID_64) || (m_spawnMoveDown != INVALID_STRING_ID_64));

	m_hParticle = NDI_NEW ParticleHandle[count];
	m_spawner = NDI_NEW const EntitySpawner*[count];
	m_flags = NDI_NEW U8[count];

	memset(m_hParticle, 0, sizeof(ParticleHandle)*count);
	memset(m_flags, 0, sizeof(U8)*count);

	// Get the spawner IDs into a temp allocator
	{
		ScopedTempAllocator jj(FILE_LINE_FUNC);
		StringId64* ids = NDI_NEW StringId64[count];

		m_numSpawners = spawn.GetDataArray<StringId64>(SID("part-spawner"), ids, count, INVALID_STRING_ID_64);
		for (U32F iSpawner = 0; iSpawner < m_numSpawners; iSpawner++)
		{
			m_spawner[iSpawner] = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByBareNameId(ids[iSpawner]);
		}
	}

	if (m_trackWater || triggerSpawn)
	{
		m_queryPoint = NDI_NEW Vec3[count];
	}

	if (triggerSpawn)
	{
		m_lastHeight = NDI_NEW F32[count];
	}

	for (U32F iSpawner = 0; iSpawner < m_numSpawners; iSpawner++)
	{
		const EntitySpawner* spawner = m_spawner[iSpawner];;
		if (spawner)
		{
			if (spawner->GetData<StringId64>(SID("particle-group"), INVALID_STRING_ID_64) != INVALID_STRING_ID_64)
				m_flags[iSpawner] = kHasPersistentParticle;

			if (m_trackWater || triggerSpawn)
				m_queryPoint[iSpawner] = spawner->GetWorldSpaceLocator().GetPosition();

			if (triggerSpawn)
				m_lastHeight[iSpawner] = m_queryPoint[iSpawner].Y();
		}
		else
			m_flags[iSpawner] = kInvalidSpawner;
	}

	return Err::kOK;
}

ParticleMultiSpawner::~ParticleMultiSpawner()
{
	for (U32F iSpawner = 0; iSpawner < m_numSpawners; iSpawner++)
	{
		KillParticle(m_hParticle[iSpawner], m_linger);
		m_hParticle[iSpawner] = ParticleHandle();
	}
}

void ParticleMultiSpawner::ProcessUpdate()
{
	PROFILE(Particles, ParticleMultiSpawner_Update);

	F32 dt = EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetDeltaTimeInSeconds();

	bool	refresh = (m_refreshCounter != g_particleMgr.GetRefreshCounter());
	bool	triggerSpawn = ((m_spawnMoveUp != INVALID_STRING_ID_64) || (m_spawnMoveDown != INVALID_STRING_ID_64));
	bool	killProcess = !triggerSpawn;
	bool	enableSpawnErrors = g_particleMgr.GetDebugInfoRef().enableSpawnErrors;

	if (!m_hidden)
	{
		bool doDisp = false;
		DisplacementResult * dinfo = nullptr;
		if ((m_trackWater || triggerSpawn) && (m_waterDisplacementIndex.m_index >= 0))
		{
			doDisp = g_waterMgr.GetDisplacement(m_waterDisplacementIndex, &dinfo);
		}

		for (U32F iSpawner = 0; iSpawner < m_numSpawners; iSpawner++)
		{
			const EntitySpawner* spawner = m_spawner[iSpawner];
			if (spawner == nullptr)
				continue;

			StringId64 sidSpawner = spawner->NameId();
			ParticleHandle&	hdl = m_hParticle[iSpawner];
			U8			  flags = m_flags[iSpawner];
			ASSERT(sidSpawner != INVALID_STRING_ID_64);

			if (m_flags[iSpawner] & kInvalidSpawner)
				continue;

			// Keep trying to spawn particles
			if ((!hdl.IsValid() || refresh) && (flags & kHasPersistentParticle))
			{
				hdl = SpawnParticleAtSpawnerEvent(sidSpawner, this);
				if (hdl.IsValid())
				{
					Vector color = spawner->GetData<Vector>(SID("color"), Vector(1.0f, 1.0f, 1.0f)); 
					F32 alpha = spawner->GetData<float>(SID("alpha"), 1.0f);

					g_particleMgr.SetColor(hdl, color * m_color);
					g_particleMgr.SetAlpha(hdl, alpha * m_alpha);
					g_particleMgr.SetTimeScale(hdl, m_timeScale);
					g_particleMgr.SetCameraAttached(hdl, m_cameraAttach, m_cameraOrient);
				}
				if (!hdl.IsValid() && enableSpawnErrors)
				{
					STRIP_IN_FINAL_BUILD;

					spawner->GetFlags().m_birthError = true;
				}
				killProcess = false;
			}

			if (hdl.IsAlive())
				killProcess = false;

			// Change the height of the particle based on the water underneath
			if ((m_trackWater || triggerSpawn) && doDisp)
			{
				Point pos = spawner->GetWorldSpaceLocator().GetPosition();
				Point waterPos(dinfo[iSpawner].position);
				Point spawnerPos(pos.X(), waterPos.Y(), pos.Z());
				Point queryPos(m_queryPoint[iSpawner].X(), waterPos.Y(), m_queryPoint[iSpawner].Z());
				BoundFrame bf(spawner->GetWorldSpaceLocator(), spawner->GetBinding());

				//g_prim.Draw(DebugSphere(waterPos, 0.25f, Color(0.0f, 0.75f, 0.5f)));
				//g_prim.Draw(DebugSphere(spawnerPos, 0.25f, Color(0.75f, 0.25f, 0.0f)));
				//g_prim.Draw(DebugSphere(queryPos, 0.25f, Color(0.75f, 0.0f, 0.25f)));

				if (hdl.IsValid() && m_trackWater)
				{
					ALWAYS_ASSERT(LengthSqr(spawnerPos) < Sqr(1e10f));
					bf.SetTranslationWs(spawnerPos);
					g_particleMgr.SetPos(m_hParticle[iSpawner], spawnerPos);
				}

				if (triggerSpawn)
				{
					F32 waterHeight = waterPos.Y();
					F32 lastHeight = m_lastHeight[iSpawner];
					F32 vel = (waterHeight - lastHeight) / dt;

					// Trigger spawns if needed
					bool newMoveDir = (vel > 0.0f);
					bool oldMoveDir = (m_flags[iSpawner] & kMoveDir);
					bool spawned = (m_flags[iSpawner] & kSpawned);

					// Reset spawn if we've changed direction
					if (newMoveDir != oldMoveDir)
						spawned = false;								

					if (!spawned)
					{
						bool passedSpawner = ((waterHeight - pos.Y()) * (lastHeight - pos.Y()) <= 0.0f);
						F32 spawnSpeed = (newMoveDir) ? m_spawnUpSpeed : m_spawnDownSpeed;
						StringId64 spawnId = (newMoveDir) ? m_spawnMoveUp : m_spawnMoveDown;

						if ((spawnId != INVALID_STRING_ID_64) && (Abs(vel) > spawnSpeed) && (m_trackWater || passedSpawner))
						{
							SpawnParticle(bf, spawnId);
							spawned = true;
						}
					}

					m_lastHeight[iSpawner] = waterHeight;
					m_flags[iSpawner] = ((newMoveDir) ? kMoveDir : 0) | ((spawned) ? kSpawned : 0);
				}

				// We can't get the height under our spawner location by plugging it into DisplacementQuery, so in order to get close
				// we try to sample the point on the water plane that is likely to be displaced to our xz location!

				Vec3 delta = Vec3(waterPos - pos); 
				m_queryPoint[iSpawner] -= delta;
			}
		}

		if (m_trackWater || triggerSpawn)
			g_waterMgr.DisplacementQuery(m_numSpawners, m_queryPoint, m_waterDisplacementIndex, kWaterQueryAll);

	}

	// If no particles are active, kill the process
	if (killProcess)
		KillProcess(this);

	// Reset refresh counter
	m_refreshCounter = g_particleMgr.GetRefreshCounter();
}

void ParticleMultiSpawner::EventHandler(Event& event)
{
	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("Show"):
		m_hidden = 0;
		break;
	case SID_VAL("Hide"):
		{
			for (U32F iSpawner = 0; iSpawner < m_numSpawners; iSpawner++)
			{
				ParticleHandle&	hdl = m_hParticle[iSpawner];

				KillParticle(hdl);
				hdl = ParticleHandle();
			}
			m_hidden = 1;
		}
		break;
	case SID_VAL("KillParticle"):
		{
			for (U32F iSpawner = 0; iSpawner < m_numSpawners; iSpawner++)
			{
				ParticleHandle&	hdl = m_hParticle[iSpawner];

				KillParticle(hdl, event.Get(0).GetBool());
			}
		}
		break;
	case SID_VAL("SetColor"):
		{
			m_color = event.Get(0).GetVector();
			for (U32F iSpawner = 0; iSpawner < m_numSpawners; iSpawner++)
			{
				ParticleHandle&	hdl = m_hParticle[iSpawner];
				if (hdl.IsAlive())
				{
					Vector color = m_spawner[iSpawner]->GetData<Vector>(SID("color"), Vector(1.0f, 1.0f, 1.0f)); 
					g_particleMgr.SetColor(hdl, color * m_color);
				}
			}
		}
		break;
	case SID_VAL("SetAlpha"):
		{
			m_alpha = event.Get(0).GetFloat();
			for (U32F iSpawner = 0; iSpawner < m_numSpawners; iSpawner++)
			{
				ParticleHandle&	hdl = m_hParticle[iSpawner];
				if (hdl.IsAlive())
				{
					F32 alpha = m_spawner[iSpawner]->GetData<float>(SID("alpha"), 1.0f);
					g_particleMgr.SetAlpha(hdl, alpha * m_alpha);
				}
			}
		}
		break;
	case SID_VAL("SetTimeScale"):
		{
			m_timeScale = event.Get(0).GetFloat();
			for (U32F iSpawner = 0; iSpawner < m_numSpawners; iSpawner++)
			{
				ParticleHandle&	hdl = m_hParticle[iSpawner];
				if (hdl.IsAlive())
				{
					g_particleMgr.SetTimeScale(hdl, m_timeScale);
				}
			}
		}
		break;
	case SID_VAL("SetCameraAttached"):
		{
			m_cameraAttach = event.Get(0).GetBool();
			m_cameraOrient = event.Get(1).GetBool();
			for (U32F iSpawner = 0; iSpawner < m_numSpawners; iSpawner++)
			{
				ParticleHandle&	hdl = m_hParticle[iSpawner];
				if (hdl.IsAlive())
				{
					g_particleMgr.SetCameraAttached(hdl, m_cameraAttach, m_cameraOrient);
				}
			}
		}
		break;
	case SID_VAL("SetFloat"):
		{
			// Caching and restoring values TBD
			StringId64 varId = event.Get(0).GetStringId();
			F32 value = event.Get(1).GetFloat();
			for (U32F iSpawner = 0; iSpawner < m_numSpawners; iSpawner++)
			{
				ParticleHandle&	hdl = m_hParticle[iSpawner];
				if (hdl.IsAlive())
				{
					g_particleMgr.SetFloat(hdl, varId, value);
				}
			}
		}
		break;
	case SID_VAL("SetVector"):
		{
			// Caching and restoring values TBD
			StringId64 varId = event.Get(0).GetStringId();
			Vector value = event.Get(1).GetVector();
			for (U32F iSpawner = 0; iSpawner < m_numSpawners; iSpawner++)
			{
				ParticleHandle&	hdl = m_hParticle[iSpawner];
				if (hdl.IsAlive())
				{
					g_particleMgr.SetVector(hdl, varId, value);
				}
			}
		}
		break;
	default:
		ParentClass::EventHandler(event);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ParticleMultiSpawner::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	bool triggerSpawn = ((m_spawnMoveUp != INVALID_STRING_ID_64) || (m_spawnMoveDown != INVALID_STRING_ID_64));

	RelocatePointer(m_hParticle, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_spawner, deltaPos, lowerBound, upperBound);
	RelocatePointer(m_flags, deltaPos, lowerBound, upperBound);
	if (m_trackWater || triggerSpawn)
	{
		RelocatePointer(m_queryPoint, deltaPos, lowerBound, upperBound);
	}
	if (triggerSpawn)
	{
		RelocatePointer(m_lastHeight, deltaPos, lowerBound, upperBound);
	}

	ParentClass::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ParticleMultiSpawner::DebugShowProcess(ScreenSpaceTextPrinter& printer) const
{
	STRIP_IN_FINAL_BUILD;
	if (g_particleMgr.GetDebugInfoRef().showProcesses)
	{
		ParentClass::DebugShowProcess(printer);
	}
}
