/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/containers/darray.h"

#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/script/nd-script-arg-iterator.h"
#include "gamelib/scriptx/h/group-defines.h"
#include "gamelib/state-script/ss-context.h"
#include "gamelib/state-script/ss-instance.h"
#include "gamelib/state-script/ss-manager.h"
#include "gamelib/state-script/ss-track-group.h"
#include "gamelib/state-script/ss-track.h"
#include "ndlib/nd-config.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/spawn-info.h"
#include "ndlib/script/script-manager.h"

// -----------------------------------------------------------------------------------------------------------------------

SCRIPT_FUNC("group-spawned-count", DcGroupSpawnedCount)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	StringId64 groupId;
	DArrayAdapter darray = NextDArray(args, groupId);

	I32 spawnedCount = 0;

	if (darray.IsValid())
	{
		U32F count = darray.GetElemCount();
		for (U32F index = 0; index < count; ++index)
		{
			const StringId64 spawnerId = darray.GetElemAt(index).GetAsStringId();
			const EntitySpawner* pSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByBareNameId(spawnerId);
			if (pSpawner && pSpawner->GetProcess())
			{
				++spawnedCount;
			}
		}
	}

	return ScriptValue(spawnedCount);
}

// -----------------------------------------------------------------------------------------------------------------------

static bool IsFullySpawned(const DArrayAdapter& darray)
{
	if (darray.IsValid())
	{
		const int count = darray.GetElemCount();
		for (int index = 0; index < count; ++index)
		{
			bool spawned = false;
			StringId64 objectId = darray.GetElemAt(index).GetAsStringId();

			Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(objectId);
			if (pProc)
			{
				spawned = true;
			}
			else
			{
				const EntitySpawner* pSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByBareNameId(objectId);
				if (!pSpawner || pSpawner->IsFullySpawned() || !pSpawner->ShouldEverSpawn())
				{
					spawned = true;
				}
			}

			if (!spawned)
			{
				if (g_ssMgr.m_debugWaitForSpawned)
					MsgCon("group '%s: waiting for '%s to spawn\n", DevKitOnly_StringIdToString(darray.GetId()), DevKitOnly_StringIdToString(objectId));
				return false;
			}

			/*
			const EntitySpawner* pSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByNameId(objectId);
			if (pSpawner)
			{
				if (!pSpawner->IsFullySpawned())
				{
					if (g_debugIsFullySpawned)
						MsgCon("group '%s' waiting on spawner '%s'\n", DevKitOnly_StringIdToString(m_groupId), DevKitOnly_StringIdToString(objectId));
					return false;
				}
			}
			else
			{
				Process* pProc = EngineComponents::GetProcessMgr()->LookupProcessByUserId(objectId);
				if (!pProc)
				{
					if (g_debugIsFullySpawned)
						MsgCon("group '%s' waiting on process '%s'\n", DevKitOnly_StringIdToString(m_groupId), DevKitOnly_StringIdToString(objectId));
					return false;
				}
			}
			*/
		}

	}

	return true; // an empty/non-existent group should act as though it's fully spawned immediately, so scripts don't hang forever waiting
}

bool IsGroupFullySpawned(StringId64 groupId)
{
	DArrayAdapter darray(groupId);
	return IsFullySpawned(darray);
}

SCRIPT_FUNC("group-spawned?", DcGroupSpawnedP)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	StringId64 groupId;
	DArrayAdapter darray = NextDArray(args, groupId);

	return ScriptValue(IsFullySpawned(darray));
}
