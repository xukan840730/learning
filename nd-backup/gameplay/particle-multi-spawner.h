/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/water/water-mgr.h"

#include "gamelib/gameplay/nd-locatable.h"

class EntitySpawner;
class ParticleHandle;
class ProcessSpawnInfo;

class ParticleMultiSpawner : public NdLocatableObject
{
private: typedef NdLocatableObject ParentClass;

public:
	FROM_PROCESS_DECLARE(ParticleMultiSpawner);

	ParticleMultiSpawner()
		: m_hParticle(nullptr)
		, m_spawner(nullptr)
		, m_queryPoint(nullptr)
		, m_lastHeight(nullptr)
		, m_flags(nullptr)
		, m_numSpawners(0)
	{
	}
	virtual ~ParticleMultiSpawner() override;

	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual void ProcessUpdate() override;
	virtual void EventHandler(Event& event) override;
	virtual U32F GetMaxStateAllocSize() override { return 0; } // stateless
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void DebugShowProcess(ScreenSpaceTextPrinter& printer) const override;

private:
	enum
	{
		kSpawned = 0x01,
		kMoveDir = 0x02,
		kHasPersistentParticle = 0x04,
		kInvalidSpawner		   = 0x08,
	};

	ParticleHandle* m_hParticle;
	const EntitySpawner** m_spawner;
	WaterMgr::DisplacementQueryInfo m_waterDisplacementIndex;
	Vec3* m_queryPoint;
	F32* m_lastHeight;
	U8* m_flags;

	Vector				m_color;
	F32					m_alpha;
	F32					m_timeScale;


	U32					m_numSpawners;
	U16					m_refreshCounter;

	U32					m_hidden		: 1;
	U32					m_cameraAttach	: 1;	// Camera attached particle
	U32					m_cameraOrient	: 1;	// Camera oriented particle
	U32					m_linger		: 1;
	U32					m_trackWater	: 1;

	StringId64			m_spawnMoveUp;
	StringId64			m_spawnMoveDown;
	F32					m_spawnUpSpeed;		// Spawn the up particle if moving faster than this when spawner is passed
	F32					m_spawnDownSpeed;	// Spawn the down particle if moving faster than this when spawner is passed
};

PROCESS_DECLARE(ParticleMultiSpawner);
