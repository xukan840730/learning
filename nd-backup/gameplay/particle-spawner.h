/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/nd-locatable.h"
#include "gamelib/render/particle/particle-handle.h"

class ProcessSpawnInfo;
class RigidBody;

class ParticleSpawner : public NdLocatableObject
{
private:
	typedef NdLocatableObject ParentClass;

public:
	FROM_PROCESS_DECLARE(ParticleSpawner);

	ParticleSpawner() { }
	virtual ~ParticleSpawner() override;

	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual void ProcessUpdate() override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void EventHandler(Event& event) override;
	virtual U32F GetMaxStateAllocSize() override { return 0; }  // stateless
	virtual void DebugShowProcess(ScreenSpaceTextPrinter& printer) const override;
	virtual void OnAddParentRigidBody(const RigidBody* pOldParentBody, Locator oldParentSpace, const RigidBody* pNewParentBody) override;

	inline ParticleHandle GetParticle() const { return m_hParticle; }

protected:
	void SpawnMyParticle();

	StringId64		m_partGroupId;
	U16				m_refreshCounter;
	ParticleHandle	m_hParticle;
	bool			m_hidden;
	bool			m_cameraAttach;	// Camera attached particle
	bool			m_cameraOrient;	// Camera oriented particle
	bool			m_linger;
	bool			m_particleSpawned;
	bool			m_autoSpawn;
	bool			m_killOnError;
	bool			m_killSpawnerWhenParticleDies;
	bool			m_gpuSimpleEmitter;
	bool			m_gpuFlagsChecked;
	bool			m_gpuSimpleEmitterAdded;
	bool			m_rtDeformation;
	U32				m_deformationGeneration;
	Vec3			m_color;
	F32				m_alpha;
	F32				m_timeScale;
	I32				m_uniqueGpuIndex;
	F32				m_effectRateMultiplier;
	F32				m_effectLifespan;
	Vec3			m_effectScale;
	Vec3			m_spriteScale;
	Vec3			m_gpuVectors[1];
};

PROCESS_DECLARE(ParticleSpawner);
