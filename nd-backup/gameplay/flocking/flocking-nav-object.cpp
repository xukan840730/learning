/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/flocking/flocking-nav-object.h"

#include "gamelib/gameplay/flocking/flocking-agent.h"
#include "gamelib/gameplay/flocking/flocking-config.h"
#include "gamelib/gameplay/flocking/flocking-mgr.h"

namespace Flocking
{
	Err FlockingTestNavObject::Init(const ProcessSpawnInfo& spawn)
	{
		const Err err = ParentClass::Init(spawn);
		m_allowDisableUpdates = false;

		{	// Flocking
			m_pFlockingAgent = AddFlockingAgent(GetTranslation(), 
												GetLocalZ(GetRotation()), 
												SID("*flocking-2d-params-farm-sheeps*"));
		}

		return err;
	}

	void FlockingTestNavObject::OnKillProcess()
	{
		ParentClass::OnKillProcess();

		{	// Flocking
			RemoveFlockingAgent(m_pFlockingAgent);
		}
	}

	void FlockingTestNavObject::PlaceHolder()
	{
	}

	/// --------------------------------------------------------------------------------------------------------------- ///
	class FlockingTestNavObject::Active : public FlockingTestNavObject::ParentClass::Active
	{
	public:
		BIND_TO_PROCESS(FlockingTestNavObject);

		virtual void Update() override;
	};

	/// --------------------------------------------------------------------------------------------------------------- ///
	void FlockingTestNavObject::Active::Update()
	{
		ParentClass::Active::Update();

		FlockingTestNavObject& pp = Self();

		{	// Flocking
			/*const float dt = GetProcessDeltaTime();
			pp.m_pFlockingAgent->UpdateMotion(dt);*/
		}
	}

	PROCESS_REGISTER(FlockingTestNavObject, NdSimpleObject);
	STATE_REGISTER(FlockingTestNavObject, Active, kPriorityNormal);
}
