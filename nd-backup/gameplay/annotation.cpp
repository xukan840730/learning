/*
 * Copyright (c) 2018 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/annotation.h"

#include "ndlib/camera/camera-final.h"
#include "ndlib/process/spawn-info.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/level/entity-spawner.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ProcessSpawnInfo;

PROCESS_REGISTER(Annotation, Process);

/// --------------------------------------------------------------------------------------------------------------- ///
Err Annotation::Init(const ProcessSpawnInfo& spawn)
{
	const SpawnInfo& info = static_cast<const SpawnInfo&>(spawn);
	m_pos  = info.m_pSpawner->GetWorldSpaceLocator().Pos();
	m_text = info.GetData<String>(SID("text"), String()).GetString();
	m_drawDistance = info.GetData<float>(SID("draw-distance"), 15.0f);

	Err result = Process::Init(info);
	SetAllowThreadedUpdate(true);
	if (result.Succeeded())
		GoActive();
	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class Annotation::Active : public Process::State
{
public:
	BIND_TO_PROCESS(Annotation);

	virtual void Update() override
	{
		if (Self().m_text && Dist(g_mainCameraInfo[0].GetPosition(), Self().m_pos) < Self().m_drawDistance)
			g_prim.Draw(DebugString(Self().m_pos, Self().m_text));
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
STATE_REGISTER(Annotation, Active, kPriorityNormal);
