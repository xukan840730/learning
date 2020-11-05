/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/particle-spawner.h"

#include "ndlib/process/event.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/render/particle/particle-debug.h"

#include "gamelib/level/entity-spawner.h"
#include "gamelib/render/particle/particle.h"
#include "gamelib/render/particle/particle-rt-mgr.h"

/// --------------------------------------------------------------------------------------------------------------- ///
PROCESS_REGISTER(ParticleSpawner, NdLocatableObject);
PROCESS_REGISTER_ALIAS(PartSpawner, ParticleSpawner); // backwards compat

FROM_PROCESS_DEFINE(ParticleSpawner);

/// --------------------------------------------------------------------------------------------------------------- ///
Err ParticleSpawner::Init(const ProcessSpawnInfo& spawn)
{
	m_hidden = false;
	m_autoSpawn = true;
	m_particleSpawned = false;
	m_killOnError = false;
	m_refreshCounter = -1;
	m_partGroupId = INVALID_STRING_ID_64;
	m_killSpawnerWhenParticleDies = true;

	m_cameraAttach = false;
	m_cameraOrient = false;
	m_linger = false;

	m_gpuSimpleEmitter = false;
	m_gpuFlagsChecked = false;
	m_gpuSimpleEmitterAdded = false;
	m_uniqueGpuIndex = 0;

	m_color = Vector(1.0f, 1.0f, 1.0f);
	m_alpha = 1.0f;
	m_timeScale = 1.0f;

	ChangeParentProcess(EngineComponents::GetProcessMgr()->m_pEffectTree);

	Err result = ParentClass::Init(spawn);
	if (result.Failed())
	{
		return result;
	}

	SetAllowThreadedUpdate(true);
	SetUserId(spawn.BareNameId(GetUserBareId()), spawn.NamespaceId(GetUserNamespaceId()), spawn.NameId(GetUserId()));

	m_partGroupId = spawn.GetData<StringId64>(SID("particle-group"), INVALID_STRING_ID_64);

	if (m_partGroupId == SID("snow-height-map"))
	{
		// Save the current value of the refresh counter to compare against
		//MsgOut("Spawning!\n");
	}

	if (m_partGroupId == INVALID_STRING_ID_64)
		return Err::kErrBadData;

	m_cameraAttach = spawn.GetData<bool>(SID("camera-attach"), false);
	m_cameraOrient = spawn.GetData<bool>(SID("camera-orient"), false);
	m_linger = spawn.GetData<bool>(SID("particle-linger"), false);

	m_color = spawn.GetData<Vector>(SID("color"), Vector(1.0f, 1.0f, 1.0f));
	m_alpha = spawn.GetData<float>(SID("alpha"), 1.0f);
	m_timeScale = 1.0f;
	
	m_effectScale = spawn.GetData<Vector>(SID("effect-scale"), Vector(1.0f, 1.0f, 1.0f));
	Scalar oldScale = spawn.GetData<F32>(SID("sprite-scale"), Scalar(1.0f));
	m_spriteScale = spawn.GetData<Vector>(SID("sprite-xyz-scale"), Vector(oldScale, oldScale, oldScale));

	m_gpuSimpleEmitter = spawn.GetData<bool>(SID("gpu-simple-emitter"), false);

	m_effectRateMultiplier = spawn.GetData<F32>(SID("rateMult"), 25.0f);
	m_effectLifespan = spawn.GetData<F32>(SID("lifespan"), 0.0f);

	m_deformationGeneration = 0;
	m_rtDeformation = false;
	m_gpuSimpleEmitter = m_gpuSimpleEmitter || g_particleMgr.IsSimpleGpuEmitter(m_partGroupId, m_rtDeformation);


	if (m_gpuSimpleEmitter)
	{
		m_gpuVectors[0] = spawn.GetData<Vector>(SID("gpu-vector-0"), Vector(0.0f, 0.0f, 0.0f));
	}

	if (m_partGroupId == SID("snow-height-map") && m_autoSpawn)
	{
		// Save the current value of the refresh counter to compare against
		m_refreshCounter = g_particleMgr.GetRefreshCounter();

		SpawnMyParticle();
	}

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ParticleSpawner::~ParticleSpawner()
{
	if (m_gpuSimpleEmitter)
	{
		if (m_gpuSimpleEmitterAdded)
		{
			g_particleMgr.RemovePlaceholderEmitter(m_partGroupId, m_uniqueGpuIndex);
			m_gpuSimpleEmitterAdded = false;
			m_uniqueGpuIndex = -1;
		}
	}
	else
	{
		KillParticle(m_hParticle, m_linger);
		m_hParticle = ParticleHandle();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ParticleSpawner::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ParticleSpawner::ProcessUpdate()
{
	if (m_partGroupId == INVALID_STRING_ID_64)
		return;

	bool refresh = m_refreshCounter != g_particleMgr.GetRefreshCounter();

	// If hidden OR the refresh counter has changed, kill the particle if we currently have one
	if (m_hidden || refresh)
	{
		// Kill our particle if it exists
		if (m_gpuSimpleEmitter)
		{
			if (m_gpuSimpleEmitterAdded)
			{
				g_particleMgr.RemovePlaceholderEmitter(m_partGroupId, m_uniqueGpuIndex);
				m_gpuSimpleEmitterAdded = false;
				m_uniqueGpuIndex = -1;
			}
		}
		else
		{
			if (m_hParticle.IsValid())
			{
				KillParticle(m_hParticle);
				m_hParticle = ParticleHandle();

				m_refreshCounter = g_particleMgr.GetRefreshCounter();

				m_particleSpawned = false;
			}
		}
	}

	if (!m_hidden)
	{
		// We aren't hidden, so attempt to spawn the particle if it has not been spawned yet

		if (m_gpuSimpleEmitter)
		{

			if (m_autoSpawn && !m_gpuSimpleEmitterAdded)
			{
				// Save the current value of the refresh counter to compare against
				m_refreshCounter = g_particleMgr.GetRefreshCounter();

				SpawnMyParticle();
			}

			if (m_gpuSimpleEmitterAdded && m_killSpawnerWhenParticleDies)
			{
				// If the particle spawned already but is no longer alive, KILL THIS PROCESS!!!
				// but only do it if we want to kill this process when particle dies. sometimes we want o keep it around because
				// particle could be respawned

				// placeholder doesn't die
				/*
				if (!m_hParticle.IsAlive())
				{
					KillProcess(this);
				}
				else
				{
					// Update the particle's position to match ours
					g_particleMgr.SetLocation(m_hParticle, m_boundFrame);
				}
				*/
			}
		}
		else
		{
			if (m_autoSpawn && !m_hParticle.IsValid())
			{
				// Save the current value of the refresh counter to compare against
				m_refreshCounter = g_particleMgr.GetRefreshCounter();

				SpawnMyParticle();
			}


			if (m_hParticle.IsValid() && m_killSpawnerWhenParticleDies)
			{
				// If the particle spawned already but is no longer alive, KILL THIS PROCESS!!!
				// but only do it if we want to kill this process when particle dies. sometimes we want o keep it around because
				// particle could be respawned

				if (!m_hParticle.IsAlive())
				{
					KillProcess(this);
				}
				else
				{
					// Update the particle's position to match ours
					g_particleMgr.SetLocation(m_hParticle, m_boundFrame);
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ParticleSpawner::SpawnMyParticle()
{
	if (m_gpuSimpleEmitter)
	{
		ParticleEmitterEntry *pEntry = nullptr;
		m_uniqueGpuIndex = g_particleMgr.AddPlaceholderEmitter(m_partGroupId, pEntry);

		if (pEntry)
		{
			Mat44 m44 = BuildTransform(GetLocator().GetRotation(), GetLocator().GetTranslation().GetVec4());

			Matrix3x3 m33;
			m33.SetRow(0, Vector(m44.GetRow(0)));
			m33.SetRow(1, Vector(m44.GetRow(1)));
			m33.SetRow(2, Vector(m44.GetRow(2)));

			pEntry->m_rot = m33.Transposed();
			pEntry->m_pos = Vec3(m44.GetRow(3));

			pEntry->m_scale = m_effectScale;
			pEntry->m_rate = m_effectRateMultiplier;
			pEntry->m_lifespan = m_effectLifespan;
			pEntry->m_alpha = m_alpha;

			pEntry->m_gpuVectors[0] = Vec3(m_gpuVectors[0]);
		}

		m_gpuSimpleEmitterAdded = m_uniqueGpuIndex != -1;
	}
	else
	{
		if (m_rtDeformation)
		{
			if (m_deformationGeneration == g_particleRtMgr.GetRTDefomrationGeneration())
				return; // no point in trying to spawn. no render targets were added
			m_deformationGeneration = g_particleRtMgr.GetRTDefomrationGeneration();
		}

		m_hParticle = SpawnParticleEvent(m_boundFrame, m_partGroupId, this);
		if (m_hParticle.IsValid())
		{
			g_particleMgr.SetColor(m_hParticle, m_color);
			g_particleMgr.SetAlpha(m_hParticle, m_alpha);
			g_particleMgr.SetTimeScale(m_hParticle, m_timeScale);
			if (m_cameraAttach || m_cameraOrient) // don't reset default values that came from group description, if spawner doesn't have the flags set
			{
				g_particleMgr.SetCameraAttached(m_hParticle, m_cameraAttach, m_cameraOrient);
			}

			m_particleSpawned = true;
		}
		else
		{
			if (g_particleMgr.GetDebugInfoRef().enableSpawnErrors)
			{
				STRIP_IN_FINAL_BUILD;

				// this is to turn off spawning in the event of an error
				if (m_pSpawner)
				{
					m_pSpawner->GetFlags().m_birthError = true;
				}
			}

			if (m_killOnError)
			{
				KillProcess(this);
			}
		}
	}
	
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ParticleSpawner::EventHandler(Event& event)
{
	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("Show"):
		{
			if (!m_hParticle.IsValid())
			{
				m_hidden = false;
			}
		}
		break;
	case SID_VAL("Hide"):
		{
			if (m_hParticle.IsValid())
			{
				m_hidden = true;
			}
		}
		break;
	case SID_VAL("KillParticle"):
		{
			if (m_hParticle.IsValid())
			{
				KillParticle(m_hParticle, event.Get(0).GetBool());
			}
		}
		break;
	case SID_VAL("SetColor"):
		{
			m_color = event.Get(0).GetVector();
			if (m_hParticle.IsValid())
			{
				g_particleMgr.SetColor(m_hParticle, m_color);
			}
		}
		break;
	case SID_VAL("SetAlpha"):
		{
			m_alpha = event.Get(0).GetFloat();
			if (m_hParticle.IsValid())
			{
				g_particleMgr.SetAlpha(m_hParticle, m_alpha);
			}
		}
		break;
	case SID_VAL("SetTimeScale"):
		{
			m_timeScale = event.Get(0).GetFloat();
			if (m_hParticle.IsValid())
			{
				g_particleMgr.SetTimeScale(m_hParticle, m_timeScale);
			}
		}
		break;
	case SID_VAL("SetCameraAttached"):
		{
			m_cameraAttach = event.Get(0).GetBool();
			m_cameraOrient = event.Get(1).GetBool();
			if (m_hParticle.IsValid())
			{
				g_particleMgr.SetCameraAttached(m_hParticle, m_cameraAttach, m_cameraOrient);
			}
		}
		break;
	case SID_VAL("SetFloat"):
		{
			// Caching and restoring values TBD
			StringId64 varId = event.Get(0).GetStringId();
			F32 value = event.Get(1).GetFloat();
			if (m_hParticle.IsValid())
			{
				g_particleMgr.SetFloat(m_hParticle, varId, value);
			}
		}
		break;
	case SID_VAL("SetVector"):
		{
			// Caching and restoring values TBD
			StringId64 varId = event.Get(0).GetStringId();
			Vector value = event.Get(1).GetVector();
			if (m_hParticle.IsValid())
			{
				g_particleMgr.SetVector(m_hParticle, varId, value);
			}
		}
		break;
	case SID_VAL("KillOnError"):
		{
			m_killOnError = event.Get(0).GetBool();
		}
		break;
	default:
		ParentClass::EventHandler(event);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ParticleSpawner::DebugShowProcess(ScreenSpaceTextPrinter& printer) const
{
	STRIP_IN_FINAL_BUILD;
	if (g_particleMgr.GetDebugInfoRef().showProcesses)
	{
		ParentClass::DebugShowProcess(printer);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ParticleSpawner::OnAddParentRigidBody(const RigidBody* pOldParentBody,
										   Locator oldParentSpace,
										   const RigidBody* pNewParentBody)
{
	ParentClass::OnAddParentRigidBody(pOldParentBody, oldParentSpace, pNewParentBody);

	// We have reparented, reparent the effect also ...
	if (m_hParticle.IsValid())
	{
		g_particleMgr.SetLocation(m_hParticle, m_boundFrame);
	}
}
