/*
 * Copyright (c) 2011 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "corelib/math/basicmath.h"
#include "corelib/util/random.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/process/event.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/spawn-info.h"
#include "ndlib/render/draw-control.h"
#include "ndlib/render/listen-mode.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/script/script-callback.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/scriptx/h/dc-types.h"
#include "ndlib/settings/settings.h"

#include "gamelib/audio/nd-vox-controller-base.h"
#include "gamelib/camera/camera-manager.h"
#include "gamelib/containers/darray.h"
#include "gamelib/gameplay/ai/agent/nav-character-adapter.h"
#include "gamelib/gameplay/ai/agent/nav-character-adapter.inl"
#include "gamelib/gameplay/ai/agent/nav-character-util.h"
#include "gamelib/gameplay/ai/controller/animaction-controller.h"
#include "gamelib/gameplay/character-manager.h"
#include "gamelib/gameplay/flocking/flocking-agent.h"
#include "gamelib/gameplay/flocking/flocking-mgr.h"
#include "gamelib/gameplay/nav/action-pack-mgr.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/cover-action-pack.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/platform-control.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"
#include "gamelib/level/artitem.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/ndphys/composite-body.h"
#include "gamelib/ndphys/composite-ragdoll-controller.h"
#include "gamelib/script/nd-script-args.h"
#include "gamelib/script/nd-script-utils.h"
#include "gamelib/scriptx/h/group-defines.h"
#include "gamelib/spline/catmull-rom.h"
#include "gamelib/state-script/ss-context.h"
#include "gamelib/state-script/ss-instance.h"
#include "gamelib/state-script/ss-manager.h"

#include "common/ai/simple-npc.h"
#include "common/stats/stat-manager.h"

#include "game/ai/action-pack/cinematic-action-pack.h"
#include "game/ai/action-pack/turret-action-pack.h"
#include "game/ai/agent/animal-sfx-manager.h"
#include "game/ai/agent/npc-manager.h"
#include "game/ai/agent/npc-tactical.h"
#include "game/ai/agent/npc.h"
#include "game/ai/agent/relationship-manager.h"
#include "game/ai/base/ai-game-debug.h"
#include "game/ai/behavior/behavior-buddy-throw.h"
#include "game/ai/behavior/behavior-cover.h"
#include "game/ai/behavior/behavior-move-to.h"
#include "game/ai/behavior/behavior-shoot-moving.h"
#include "game/ai/behavior/behavior-shoot-stationary.h"
#include "game/ai/behavior/behavior-turn-to-face.h"
#include "game/ai/characters/buddy.h"
#include "game/ai/characters/dog.h"
#include "game/ai/characters/infected.h"
#include "game/ai/characters/infected-legless.h"
#include "game/ai/characters/pirate.h"
#include "game/ai/component/buddy-combat-logic.h"
#include "game/ai/component/prop-inventory.h"
#include "game/ai/component/vision.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/ai/controller/evade-controller.h"
#include "game/ai/controller/hit-controller.h"
#include "game/ai/controller/locomotion-controller.h"
#include "game/ai/controller/tap-controller.h"
#include "game/ai/controller/weapon-controller.h"
#include "game/ai/coordinator/coordinator-manager.h"
#include "game/ai/coordinator/encounter-coordinator.h"
#include "game/ai/coordinator/group-entity.h"
#include "game/ai/demeanor-helper.h"
#include "game/ai/knowledge/buddy-raycaster.h"
#include "game/ai/knowledge/entity.h"
#include "game/ai/knowledge/knowledge.h"
#include "game/ai/knowledge/stimulus.h"
#include "game/ai/knowledge/target-appraiser.h"
#include "game/ai/look-aim/look-aim-basic.h"
#include "game/ai/look-aim/look-aim-buddy.h"
#include "game/ai/look-aim/look-aim-search.h"
#include "game/ai/look-aim/look-aim-sniper.h"
#include "game/ai/look-aim/look-aim-watch.h"
#include "game/ai/look-aim/npc-look-aim.h"
#include "game/ai/melee/melee-behavior-condition.h"
#include "game/ai/reaction/hit-reaction.h"
#include "game/ai/requests/requests.h"
#include "game/ai/scent/scent-manager.h"
#include "game/ai/skill/investigate-skill.h"
#include "game/ai/skill/scripted-skill.h"
#include "game/ai/skill/skill-mgr.h"
#include "game/ai/skill/spline-network-skill.h"
#include "game/ai/skill/track-skill.h"
#include "game/audio/dialog-manager.h"
#include "game/character-util.h"
#include "game/event-attack.h"
#include "game/gameinfo.h"
#include "game/nav/nav-game-util.h"
#include "game/net/net-game-manager.h"
#include "game/player/melee/process-melee-action.h"
#include "game/player/player-targeting.h"
#include "game/player/player.h"
#include "game/player/player-shot-reaction.h"
#include "game/process-ragdoll.h"
#include "game/render/gui/hud.h"
#include "game/save/checkpoint.h"
#include "game/script-arg-iterator.h"
#include "game/script-args.h"
#include "game/script-funcs.h"
#include "game/scriptx/h/ai-weapon-defines.h"
#include "game/scriptx/h/anim-npc-info.h"
#include "game/scriptx/h/melee-defines.h"
#include "game/scriptx/h/task-defines.h"
#include "game/tasks/task-manager.h"
#include "game/tasks/task.h"
#include "game/vehicle-ctrl.h"
#include "game/weapon/process-weapon-base.h"
#include "game/weapon/process-weapon-strap.h"
#include "game/weapon/process-weapon.h"
#include "game/weapon/projectile-base.h"
#include "game/weapon/projectile-grenade.h"
#include "game/weapon/projectile-infected-pustule.h"
#include "game/weapon/projectile-process.h"
#include "game/weapon/projectile-throwable.h"
#include "game/weapon/turret.h"

bool g_debugAccessibilityPathfind = false;

//////////////////////////////////////////////////////////////////////////
// Helper functions
//////////////////////////////////////////////////////////////////////////

const char* DcTrueFalse(bool b)
{
	return b ? "#t" : "#f";
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Npc::ScriptCommand::WeaponBehavior ConvertDcMoveStyle(DC::NpcForceDirective directive)
{
	Npc::ScriptCommand::WeaponBehavior wb = Npc::ScriptCommand::kWeaponShoot;
	switch (directive)
	{
	case DC::kNpcForceDirectiveShoot:
		wb = Npc::ScriptCommand::kWeaponShoot;
		break;

	case DC::kNpcForceDirectiveAim:
		wb = Npc::ScriptCommand::kWeaponAim;
		break;

	case DC::kNpcForceDirectiveGunDown:
		wb = Npc::ScriptCommand::kWeaponDown;
		break;

	default:
		ASSERT(false);
		break;
	}
	return wb;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Npc* DcLookupNpcById(StringId64 npcNameId)
{
	return EngineComponents::GetNpcManager()->FindNpcByUserId(npcNameId);
}

//**********************************************************************************************************************
// Script functions
//**********************************************************************************************************************

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("c-spawn-npc", DcCSpawnNpc)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	StringId64 spawnerName		  = args.GetStringId(); // don't advance
	const EntitySpawner* pSpawner = args.NextSpawner();

	StringId64 posName = args.GetStringId(); // don't advance
	const EntitySpawner* pPosSpawner = args.NextSpawner();

	const bool spawnInCover		  = args.NextBoolean();
	const bool spawnWithoutWeapon = args.NextBoolean();

	bool isDead = pSpawner ? GameSave::IsCharacterDeadForCurrentCheckpoint(pSpawner->NameId()) : false;
	if (isDead)
	{
		return ScriptValue(0);
	}

	if (!pSpawner)
	{
		args.MsgScriptError("Failed to spawn \"%s\" not a valid spawner\n", DevKitOnly_StringIdToString(spawnerName));
		return ScriptValue(0);
	}
	else if (pSpawner->GetFlags().m_birthError)
	{
		args.MsgScriptError("Failed to spawn \"%s\" spawner has a birth error, check spawner\n",
							DevKitOnly_StringIdToString(spawnerName));
		return ScriptValue(0);
	}
	else if (!pSpawner->GetFlags().m_multiSpawner && pSpawner->GetProcess())
	{
		args.MsgScriptError("Failed to spawn \"%s\" not a multispawner and object already spawned\n",
							DevKitOnly_StringIdToString(spawnerName));
		return ScriptValue(0);
	}
	else
	{
		NpcSpawnInfo spawnInfo(SpawnInfo(pSpawner), posName);

		spawnInfo.m_spawnInCover = spawnInfo.m_spawnInCover || spawnInCover;

		if (spawnWithoutWeapon)
			spawnInfo.m_weaponLoadoutNameId = INVALID_STRING_ID_64;

		SsVerboseLog(1, "spawn-npc: Spawning npc '%s'", pSpawner->Name());
		if (pPosSpawner)
		{
			Locator loc	   = pPosSpawner->GetWorldSpaceLocator();
			Process* pProc = pSpawner->Spawn(&spawnInfo, &loc);
			if (pProc && pProc->IsKindOf(g_type_Npc))
			{
				AiLogScript((Npc*)pProc, "c-spawn-npc");
			}
			return ScriptValue(pProc);
		}
		else
		{
			Process* pProc = pSpawner->Spawn(&spawnInfo);
			if (pProc && pProc->IsKindOf(g_type_Npc))
			{
				AiLogScript((Npc*)pProc, "c-spawn-npc");
			}
			return ScriptValue(pProc);
		}
	}
	args.MsgScriptError("Failed to spawn \"%s\" due to unknown error\n", DevKitOnly_StringIdToString(spawnerName));
	return ScriptValue(0);
}

SCRIPT_FUNC("c-spawn-npc-with-look", DcCSpawnNpcWithLook)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	StringId64 spawnerName		  = args.GetStringId(); // don't advance
	const EntitySpawner* pSpawner = args.NextSpawner();

	StringId64 posName = args.GetStringId(); // don't advance
	const EntitySpawner* pPosSpawner = args.NextSpawner();

	StringId64 lookName = args.NextStringId(); // don't advance
	bool isDead			= pSpawner ? GameSave::IsCharacterDeadForCurrentCheckpoint(pSpawner->NameId()) : false;
	if (isDead)
	{
		return ScriptValue(0);
	}

	if (!pSpawner)
	{
		args.MsgScriptError("Failed to spawn \"%s\" not a valid spawner\n", DevKitOnly_StringIdToString(spawnerName));
		return ScriptValue(0);
	}
	else if (pSpawner->GetFlags().m_birthError)
	{
		args.MsgScriptError("Failed to spawn \"%s\" spawner has a birth error, check spawner\n",
							DevKitOnly_StringIdToString(spawnerName));
		return ScriptValue(0);
	}
	else if (!pSpawner->GetFlags().m_multiSpawner && pSpawner->GetProcess())
	{
		args.MsgScriptError("Failed to spawn \"%s\" not a multispawner and object already spawned\n",
							DevKitOnly_StringIdToString(spawnerName));
		return ScriptValue(0);
	}
	else
	{
		NpcSpawnInfo spawnInfo(SpawnInfo(pSpawner), posName);
		SsVerboseLog(1, "spawn-npc: Spawning npc '%s'", pSpawner->Name());
		ForceArtGroup fa;
		fa.m_artGroupId = lookName;

		if (pPosSpawner)
		{
			Locator loc	   = pPosSpawner->GetWorldSpaceLocator();
			Process* pProc = pSpawner->Spawn(&spawnInfo,
											 &loc,
											 false,
											 INVALID_STRING_ID_64,
											 INVALID_STRING_ID_64,
											 INVALID_STRING_ID_64,
											 &fa);
			if (pProc && pProc->IsKindOf(g_type_Npc))
			{
				AiLogScript((Npc*)pProc, "c-spawn-npc");
			}
			return ScriptValue(pProc);
		}
		else
		{
			Process* pProc = pSpawner->Spawn(&spawnInfo,
											 nullptr,
											 false,
											 INVALID_STRING_ID_64,
											 INVALID_STRING_ID_64,
											 INVALID_STRING_ID_64,
											 &fa);
			if (pProc && pProc->IsKindOf(g_type_Npc))
			{
				AiLogScript((Npc*)pProc, "c-spawn-npc");
			}
			return ScriptValue(pProc);
		}
	}
	args.MsgScriptError("Failed to spawn \"%s\" due to unknown error\n", DevKitOnly_StringIdToString(spawnerName));
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-spawner-get-look", DcNpcGetLook)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	const EntitySpawner* pSpawner = args.GetSpawner();
	if (pSpawner)
	{
		const char* artGroupName = pSpawner->GetArtGroupName();
		if (artGroupName)
		{
			return ScriptValue(StringToStringId64(artGroupName));
		}
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-num-looks-in-collection", DcGetNumLooksInCollection)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	StringId64 collectionId = args.GetStringId();

	const LookPointer pLook = GetLook(collectionId);

	int numLooksInCollection = GetNumLooksInCollection(pLook);

	return ScriptValue(numLooksInCollection);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-look-id-from-collection-index", DcGetLookIdFromCollectionIndex)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	StringId64 collectionId = args.NextStringId();
	I32 index = args.NextI32();

	const LookPointer pLook = GetLook(collectionId);

	if (index >= 0 && index < GetNumLooksInCollection(pLook))
		return ScriptValue(GetLookNameIdFromLookCollection(pLook, index));

	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-face-idle-emotion-from-task", DcGetFaceIdleEmotionFromTask)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	StringId64 name = args.NextStringId();

	return ScriptValue(TaskManager::GetEmotionForCharacter(name));
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-dodge-away-from", DcNpcDodgeAwayFrom)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc	= args.NextNpc();
	Point point = args.NextPoint();

	if (pNpc)
	{
		IAiDodgeController* pDodgeController = pNpc->GetAnimationControllers()->GetDodgeController();

		pDodgeController->RequestDodge(point, kZero, true);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-prevent-dodge", DcNpcPreventDodge)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		pNpc->PreventDodge();
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-dead-in-checkpoint?", DcNpcIsDeadInCheckpoint)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	StringId64 npcNameId = args.NextStringId();

	bool isDead = GameSave::IsCharacterDeadForCurrentCheckpoint(npcNameId);

	return ScriptValue(isDead);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-disable-has-ever-seen-updates/f", DcNpcDisableHasEverSeenUpdates)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		pNpc->m_disableEverSeenPlayerUpdates = true;
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-prevent-corpse-to-ragdoll/f", DcNpcPreventCorpseToRagdollF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		pNpc->PreventCorpseToRagdoll();
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-disable-all-corpse-to-ragdoll/f", DcNpcDisableAllCorpseToRagdollF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 0);

	SettingSetDefault(&g_aiGameOptions.m_disableSwitchingCorpseWithRagdollScripted, false);

	g_aiGameOptions.m_disableSwitchingCorpseWithRagdollScripted = true;
	SettingSetPers(SID("npc-disable-all-corpse-to-ragdoll/f"),
				   &g_aiGameOptions.m_disableSwitchingCorpseWithRagdollScripted,
				   true,
				   kScriptPriority);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("c-spawn-npc*", DcCSpawnNpcS)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	StringId64 spawnerName		  = args.GetStringId(); // don't advance
	const EntitySpawner* pSpawner = args.NextSpawner();

	bool isDead = GameSave::IsCharacterDeadForCurrentCheckpoint(pSpawner->NameId());

	if (isDead)
		return ScriptValue(0);

	StringId64 posName = args.GetStringId(); // don't advance
	const EntitySpawner* pPosSpawner = args.NextSpawner();

	const DC::ScriptLambda* pLmb = args.NextPointer<DC::ScriptLambda>();

	if (pSpawner && !pSpawner->GetFlags().m_birthError
		&& (pSpawner->GetFlags().m_multiSpawner || !pSpawner->GetProcess()))
	{
		NpcSpawnInfo spawnInfo(SpawnInfo(pSpawner), posName, pLmb);

		SsVerboseLog(1, "spawn-npc: Spawning npc '%s'", pSpawner->Name());
		if (pPosSpawner)
		{
			Locator loc = pPosSpawner->GetWorldSpaceLocator();
			return ScriptValue(pSpawner->Spawn(&spawnInfo, &loc));
		}
		else
		{
			return ScriptValue(pSpawner->Spawn(&spawnInfo));
		}
	}
	args.MsgScriptError("Failed to spawn \"%s\"\n", DevKitOnly_StringIdToString(spawnerName));
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
 *	(define-c-function point-at
 *		(
 *			(spawner		symbol)				;; name of character (spawner name) that should point
 *			(world-point	point)				;; fixed world-space point to point at
 *			(priority		cmd-priority (:default (cmd-priority cinematic)))			;; priority, defaults to 'cinematic'
 *		)
 *		none
 *	)
 */

SCRIPT_FUNC("point-at", DcPointAt)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Process* pObject	= args.NextProcess();
	const Point pointWS = args.NextPoint();
	ILookAtTracker::Priority commandPriority = (ILookAtTracker::Priority)args.NextU32();

	if (pObject)
	{
		if (pObject->IsKindOf(g_type_Npc))
			AiLogScript((Npc*)pObject, "point-at");

		SendEvent(SID("point-at"), pObject, &pointWS, commandPriority);
	}

	return ScriptValue(1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("suppress-look-at-poi-global", SuppressLookAtPoiGLobal)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 0);

	AtomicLockJanitorRead_Jls npcGroupLock(EngineComponents::GetNpcManager()->GetNpcGroupLock(), FILE_LINE_FUNC);

	for (MutableNpcHandle hNpc : EngineComponents::GetNpcManager()->GetNpcs())
	{
		Npc* pNpc = hNpc.ToMutableProcess();
		if (BuddyLookAtLogic* pBuddyLookAtLogic = pNpc->GetBuddyLookAtLogic())
		{
			pBuddyLookAtLogic->SuppressLookAtPoi();
		}
	}

	// TODO: suppress players

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-deal-flame-damage", DcNpcDealFlameDamage)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc  = args.NextNpc();
	I32 damage = args.NextI32();

	if (pNpc)
	{
		FlameAttackInfo attackInfo;
		attackInfo.m_catchOnFire = true;
		attackInfo.m_damageInfo.m_damageTotal		 = damage;
		attackInfo.m_attackerState.m_initialPosition = pNpc->GetBoundFrame();

		PostEventAttack(&attackInfo, pNpc);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-die", DcNpcDie)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc		  = args.NextNpc();
	bool bDontRagdoll = args.NextBoolean();
	bool bCountAsPlayerKill	  = args.NextBoolean();
	bool bNoHitReaction		  = args.NextBoolean();
	bool bIgnoreInvincibility = args.NextBoolean();

	if (pNpc)
	{
		SsVerboseLog(1,
					 "npc-die '%s' %s %s",
					 pNpc->GetName(),
					 bDontRagdoll ? "(dont-ragdoll)" : "",
					 bIgnoreInvincibility ? "(ignore-invincibility)" : "");

		if (bDontRagdoll)
		{
			pNpc->DisallowRagdoll();
		}

		if (!pNpc->IsDead() && bCountAsPlayerKill && GetPlayer())
		{
			EngineComponents::GetPlayerStatManager()->IncrementKill(*pNpc, *GetPlayer(), SID("script"), false);
		}

		if (!bCountAsPlayerKill)
		{
			pNpc->m_useGroundProbeToSpawnPickup = false;
		}

		pNpc->DealDamage(pNpc->GetHealthSystem()->GetMaxHealth(), false, bIgnoreInvincibility);
		IAiHitController* pHitController = pNpc->GetAnimationControllers()->GetHitController();
		if (!bNoHitReaction && pHitController)
		{
			HitDescription hitDesc;
			hitDesc.m_damage	  = pNpc->GetHealthSystem()->GetMaxHealth();
			hitDesc.m_directionWs = kUnitZAxis;
			pHitController->Die(hitDesc, true);
		}

		if (g_netInfo.IsNetActive())
		{
			Event event(SID("die"), pNpc);
			SendNetEventEvent(event, pNpc->GetUserId());
		}

		return ScriptValue(1);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-special-death-anim", DcNpcSetSpecialDeathAnim)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	Npc* pNpc = args.NextNpc();
	const StringId64 deathAnimId	 = args.NextStringId();
	const StringId64 deathLoopAnimId = args.NextStringId();

	bool success = false;

	if (pNpc)
	{
		NpcSpawnInfo& spawn		 = pNpc->GetSpawnInfo();
		spawn.m_specialDeathAnim = deathAnimId;
		spawn.m_deathLoopAnim	 = deathLoopAnimId;
	}

	return ScriptValue(success);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-enable-blood-pool", DcNpcEnableBloodPool)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc	 = args.NextNpc();
	bool bEnable = args.NextBoolean();

	if (pNpc)
	{
		pNpc->EnableBloodPoolSpawn(bEnable);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-ragdoll-allowed", DcNpcSetRagdollAllowed)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	Npc* pNpc = args.NextNpc();
	bool bRagdollAllowed = args.NextBoolean();
	bool bKeepCapsules	 = args.NextBoolean();

	if (pNpc)
	{
		if (bRagdollAllowed)
			pNpc->AllowRagdoll();
		else
		{
			pNpc->DisallowRagdoll();
			pNpc->SetKeepCollisionCapsulesWhenNotRagdolling(bKeepCapsules);
		}
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("look-at", DcLookAt)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	Process* pProcess	   = args.NextProcess();
	const Point worldPoint = args.NextPoint();
	const float timeOutSeconds = args.NextFloat();

	Npc* pNpc = Npc::FromProcess(pProcess);

	if (pNpc)
	{
		AiLogScript(pNpc, "look-at");

		LookAimPointWsParams params(worldPoint);
		pNpc->GetLookAim().SetLookMode(kLookAimPriorityScript,
									   AiLookAimRequest(SID("LookAimPointWs"), &params),
									   FILE_LINE_FUNC,
									   timeOutSeconds);
	}
	else if (pProcess)
	{
		SendEvent(SID("look-at"), pProcess, worldPoint, ILookAtTracker::kPriorityCinematic, timeOutSeconds);
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("look-at-object", DcLookAtObject)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	Process* pProcess = args.NextProcess();
	Npc* pNpc		  = Npc::FromProcess(pProcess);

	NdLocatableObject* pObject = args.NextLocatableObject();

	const StringId64 attachName = args.NextStringId();

	const float timeOutSeconds = args.NextFloat();

	if (!pObject)
		return ScriptValue(0);

	if (pNpc)
	{
		AiLogScript(pNpc, "look-at-object %s", pObject->GetName());

		LookAimLocatableParams params(pObject, attachName);
		pNpc->GetLookAim().SetLookMode(kLookAimPriorityScript,
									   AiLookAimRequest(SID("LookAimLocatable"), &params),
									   FILE_LINE_FUNC,
									   timeOutSeconds);
	}
	else if (pProcess)
	{
		NdGameObject* const pGameObject = NdGameObject::FromProcess(pObject);

		if (pGameObject)
		{
			ScriptArgIterator::AttachOrJointIndex aoj;
			if (args.GetAttachOrJointIndex(aoj, pGameObject, attachName))
			{
				Event evt(SID("look-at-object"),
						  nullptr,
						  static_cast<Process*>(pGameObject),
						  static_cast<U32>(aoj.attachIndex.GetValue()),
						  static_cast<I32>(aoj.jointIndex),
						  ILookAtTracker::kPriorityCinematic);

				evt.PushParam(timeOutSeconds);

				evt.Send(pProcess);
			}
		}
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("clear-look-at", DcClearLookAt)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	Process* pProcess = args.NextProcess();

	if (Npc* pNpc = Npc::FromProcess(pProcess))
	{
		AiLogScript(pNpc, "clear-look-at");

		pNpc->ClearLookAimMode(kLookAimPriorityScript, FILE_LINE_FUNC);
	}
	else
	{
		SendEvent(SID("clear-look-at"), pProcess, ILookAtTracker::kPriorityCinematic);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-look-aim-mode", DcNpcSetLookAimMode)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	const DC::LookAimMode lookAimMode = args.NextI32();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-set-look-aim-mode");

		switch (lookAimMode)
		{
		case DC::kLookAimModeNone:
			pNpc->SetLookAimMode(kLookAimPriorityScript, SID("LookAimNone"), FILE_LINE_FUNC);
			break;
		case DC::kLookAimModeNatural:
			pNpc->SetLookAimMode(kLookAimPriorityScript, SID("LookAimNatural"), FILE_LINE_FUNC);
			break;
		case DC::kLookAimModeBuddyPlayer:
			pNpc->SetLookAimMode(kLookAimPriorityScript, SID("LookAimBuddyPlayer"), FILE_LINE_FUNC);
			break;
		case DC::kLookAimModeBuddy:
			pNpc->SetLookAimMode(kLookAimPriorityScript, SID("LookAimBuddy"), FILE_LINE_FUNC);
			break;
		case DC::kLookAimModeCopyLookAt:
			pNpc->SetLookAimMode(kLookAimPriorityScript, SID("LookAimCopyLookAt"), FILE_LINE_FUNC);
			break;
		case DC::kLookAimModeCurrentFacing:
			pNpc->SetLookAimMode(kLookAimPriorityScript, SID("LookAimCurrentFacing"), FILE_LINE_FUNC);
			break;
		case DC::kLookAimModeLocatable:
			args.MsgScriptError("Use npc-set-look-aim-mode-object for type locatable\n");
			break;
		case DC::kLookAimModePath:
			pNpc->SetLookAimMode(kLookAimPriorityScript, SID("LookAimPath"), FILE_LINE_FUNC);
			break;
		case DC::kLookAimModePointWs:
			args.MsgScriptError("Use npc-set-look-aim-mode-point for type point-ws\n");
			break;
		case DC::kLookAimModeSearch:
			pNpc->SetLookAimMode(kLookAimPriorityScript, SID("LookAimSearch"), FILE_LINE_FUNC);
			break;
		case DC::kLookAimModeSniper:
			pNpc->SetLookAimMode(kLookAimPriorityScript, SID("LookAimSniper"), FILE_LINE_FUNC);
			break;
		case DC::kLookAimModeSplitBetweenAiEntities:
			{
				LookAimSplitBetweenAiEntitiesParams params(1.5f);
				pNpc->SetLookAimMode(kLookAimPriorityScript,
									 AiLookAimRequest(SID("LookAimSplitBetweenAiEntities"), &params),
									 FILE_LINE_FUNC);
			}
			break;
		case DC::kLookAimModeSplitBetweenRandomPointsInVisionCone:
			{
				LookAimSplitBetweenRandomPointsInVisionConeParams params(1.5f);
				pNpc->SetLookAimMode(kLookAimPriorityScript,
									 AiLookAimRequest(SID("LookAimSplitBetweenRandomPointsInVisionCone"), &params),
									 FILE_LINE_FUNC);
			}
			break;
		case DC::kLookAimModeTargetEntity:
			pNpc->SetLookAimMode(kLookAimPriorityScript, SID("LookAimTargetEntity"), FILE_LINE_FUNC);
			break;
		case DC::kLookAimModeTargetEntityOrPath:
			pNpc->SetLookAimMode(kLookAimPriorityScript, SID("LookAimTargetEntityOrPath"), FILE_LINE_FUNC);
			break;
		case DC::kLookAimModeVectorWs:
			args.MsgScriptError("Use npc-set-look-aim-mode-vector for type vector-ws\n");
			break;
		case DC::kLookAimModeWatch:
			pNpc->SetLookAimMode(kLookAimPriorityScript, SID("LookAimWatch"), FILE_LINE_FUNC);
			break;
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue DcNpcEnterNearestActionPackInternal(ScriptArgIterator& args,
													   ScriptValue* argv,
													   U32F apTypeMask,
													   const char* strWaitMsg)
{
	Npc* pNpc = args.NextNpc();
	const BoundFrame* pBoundFrame  = args.NextBoundFrame();
	DC::NpcMotionType dcMotionType = args.NextI32();
	bool usePerch		  = args.NextBoolean();
	MotionType motionType = DcMotionTypeToGame(dcMotionType);
	const StringId64 mtSubcategory = args.NextStringId();
	const bool tryToShoot = args.NextBoolean();

	if (pNpc && pBoundFrame)
	{
		const Point posWs = pBoundFrame->GetTranslationWs();
		AiLogScript(pNpc,
					"%s %s to %s near [%0.1f %0.1f %0.1f] (%s)",
					strWaitMsg ? strWaitMsg : "",
					GetMotionTypeName(motionType),
					"cover",
					float(posWs.X()),
					float(posWs.Y()),
					float(posWs.Z()),
					tryToShoot ? "try to shoot" : "don't shoot");

		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

		const Point posPs = pNpc->GetParentSpace().UntransformPoint(pBoundFrame->GetTranslationWs());

		if (strWaitMsg)
		{
			pGroupInst->WaitForTrackDone(strWaitMsg);
		}

		NavMoveArgs moveArgs;
		moveArgs.m_motionType	 = motionType;
		moveArgs.m_mtSubcategory = mtSubcategory;

		NavLocation navLoc = pNpc->AsReachableNavLocationWs(pBoundFrame->GetTranslation(), NavLocation::Type::kNavPoly);
		pNpc->ScriptCommandUseClosestAp(navLoc,
										usePerch ? ActionPack::kPerchActionPack : ActionPack::kCoverActionPack,
										moveArgs,
										INVALID_STRING_ID_64, // enterAnim
										tryToShoot,
										pGroupInst,
										pTrackInst ? pTrackInst->GetTrackIndex() : 0,
										(strWaitMsg != nullptr));

		if (strWaitMsg)
		{
			ScriptContinuation::Suspend(argv);
		}
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-enter-nearest-action-pack
		(
			(npc-name		symbol)
			(location		bound-frame)
			(motion-type	npc-motion-type	(:default (npc-motion-type run)))
			(mt-subcategory	symbol			(:default 0)
			(try-to-shoot	boolean			(:default #f))
		)
		none
	)
*/
SCRIPT_FUNC("npc-enter-nearest-action-pack", DcNpcEnterNearestActionPack)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	DcNpcEnterNearestActionPackInternal(args, argv, ~0, nullptr);
	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("are-enemy-npcs-spawning", DcAreEnemyNpcsSpawning)
{
	return ScriptValue(EngineComponents::GetLevelMgr()->AreEnemyNpcSpawnsPending());
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function wait-npc-enter-nearest-action-pack
		(
			(npc-name		symbol)
			(location		bound-frame)
			(motion-type	npc-motion-type	(:default (npc-motion-type run)))
			(mt-subcategory	symbol			(:default 0)
			(try-to-shoot	boolean			(:default #f))
		)
		none
	)
*/
SCRIPT_FUNC("wait-npc-enter-nearest-action-pack", DcWaitNpcEnterNearestActionPack)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	DcNpcEnterNearestActionPackInternal(args, argv, ~0, "wait-npc-enter-nearest-action-pack");
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-use-closest-perch
		(
			(npc-name		symbol)
			(location		bound-frame)
			(motion-type	npc-motion-type	(:default (npc-motion-type run)))
			(mt-subcategory	symbol			(:default 0))
			(enter-anim		symbol			(:default 0))
		none
	)
 */
SCRIPT_FUNC("npc-use-closest-perch", DcNpcUseClosestPerch)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	const BoundFrame* pBoundFrame  = args.NextBoundFrame();
	DC::NpcMotionType dcMotionType = args.NextI32();
	MotionType motionType	 = DcMotionTypeToGame(dcMotionType);
	StringId64 mtSubcategory = args.NextStringId();
	StringId64 enterAnim	 = args.NextStringId();

	if (pNpc && pBoundFrame)
	{
		AiLogScript(pNpc, "npc-use-closest-perch");
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();

		NavMoveArgs moveArgs;
		moveArgs.m_motionType	 = motionType;
		moveArgs.m_mtSubcategory = mtSubcategory;

		NavLocation destLoc = pNpc->AsReachableNavLocationWs(pBoundFrame->GetTranslation(), NavLocation::Type::kNavPoly);
		pNpc->ScriptCommandUseClosestAp(destLoc,
										ActionPack::kPerchActionPack,
										moveArgs,
										enterAnim,
										false,
										pGroupInst,
										0,
										false);
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function wait-npc-use-closest-perch
		(
			(npc-name		symbol)
			(location		bound-frame)
			(motion-type	npc-motion-type	(:default (npc-motion-type run)))
			(mt-subcategory	symbol			(:default 0))
			(enter-anim		symbol			(:default 0))
		none
	)
 */
SCRIPT_FUNC("wait-npc-use-closest-perch", DcWaitNpcUseClosestPerch)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	const BoundFrame* pBoundFrame  = args.NextBoundFrame();
	DC::NpcMotionType dcMotionType = args.NextI32();
	MotionType motionType	 = DcMotionTypeToGame(dcMotionType);
	StringId64 mtSubcategory = args.NextStringId();
	StringId64 enterAnim	 = args.NextStringId();

	if (pNpc && pBoundFrame)
	{
		AiLogScript(pNpc, "wait-npc-use-closest-perch");
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

		pGroupInst->WaitForTrackDone("wait-npc-use-closest-perch");

		NavMoveArgs moveArgs;
		moveArgs.m_motionType	 = motionType;
		moveArgs.m_mtSubcategory = mtSubcategory;

		NavLocation destLoc = pNpc->AsReachableNavLocationWs(pBoundFrame->GetTranslation(), NavLocation::Type::kNavPoly);
		pNpc->ScriptCommandUseClosestAp(destLoc,
										ActionPack::kPerchActionPack,
										moveArgs,
										enterAnim,
										false,
										pGroupInst,
										pTrackInst->GetTrackIndex(),
										true);

		ScriptContinuation::Suspend(argv);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	;; Tell an Npc to use a CAP in the world
	(define-c-function [wait-]npc-use-cinematic-action-pack
		(
			(npc-name		symbol)
			(cap-name		symbol)
			(motion-type	npc-motion-type	(:default (npc-motion-type walk)))
			(mt-subcategory	symbol			(:default 0))
		)
		none
	)
*/

static ScriptValue DcNpcUseCinematicActionPackInternal(ScriptArgIterator& args, bool bWait)
{
	const StringId64 npcName = args.GetStringId();
	Npc* pNpc = args.NextNpc();

	const StringId64 capName = args.NextStringId();
	const DC::NpcMotionType dcMotionType = args.NextI32();
	const MotionType motionType	   = DcMotionTypeToGame(dcMotionType);
	const StringId64 mtSubcategory = args.NextStringId();
	const StringId64 performanceId = args.NextStringId();

	if (!pNpc)
	{
		args.MsgScriptError("can't find NPC '%s\n", DevKitOnly_StringIdToString(npcName));
		return ScriptValue(0);
	}

	AiLogScript(pNpc,
				"%s: %s %s to %s",
				args.GetDcFunctionName(),
				pNpc->GetName(),
				GetMotionTypeName(motionType),
				DevKitOnly_StringIdToString(capName));

	SsInstance* pScriptInst = GetJobSsContext().GetScriptInstance();
	SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

	if (bWait && (!pScriptInst || !pGroupInst || !pTrackInst))
	{
		args.MsgScriptError("called on npc (%s) but script has no context, you probably ps3-evaled it\n",
							pNpc->GetName());
		return ScriptValue(0);
	}

	NavMoveArgs moveArgs;
	moveArgs.m_motionType	 = motionType;
	moveArgs.m_mtSubcategory = mtSubcategory;
	moveArgs.m_performanceId = performanceId;

	pNpc->ScriptCommandUseCap(capName, moveArgs, pGroupInst, pTrackInst ? pTrackInst->GetTrackIndex() : -1, bWait);

	if (bWait && pScriptInst && pGroupInst && pTrackInst)
	{
		pGroupInst->WaitForTrackDone("npc-use-cinematic-action-pack");
		ScriptContinuation::Suspend(args.GetArgArray());
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("npc-use-cinematic-action-pack", DcNpcUseCinematicActionPack)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	{
		Npc* pNpc = args.GetNpc();
		AiLogScript(pNpc, "npc-use-cinematic-action-pack");
	}

	return DcNpcUseCinematicActionPackInternal(args, false);
}

SCRIPT_FUNC("wait-npc-use-cinematic-action-pack", DcWaitNpcUseCinematicActionPack)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	{
		Npc* pNpc = args.GetNpc();
		AiLogScript(pNpc, "wait-npc-use-cinematic-action-pack");
	}

	return DcNpcUseCinematicActionPackInternal(args, true);
}

SCRIPT_FUNC("npc-is-using-cinematic-action-pack?", DcNpcIsUsingCinematicActionPackP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		if (pNpc->GetAnimationControllers()->GetCinematicController()->IsBusy())
			return ScriptValue(true);

		if (const ActionPack* pAp = pNpc->GetEnteredActionPack())
		{
			if (pAp->GetType() == ActionPack::kCinematicActionPack)
				return ScriptValue(true);
		}
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-injured?", DcNpcIsInjuredP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		if (pNpc->IsInjured())
			return ScriptValue(true);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-role-order", DcNpcGetRoleOrder)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc		  = args.NextNpc();
	StringId64 roleId = args.NextStringId();

	if (pNpc)
	{
		if (const AiEncounterCoordinator* pCoord = pNpc->GetEncounterCoordinator())
		{
			U32 position = pCoord->GetRolePositionForMember(pNpc, roleId);

			if (position == ~0U)
				return ScriptValue(-1);

			return ScriptValue(position);
		}
	}

	return ScriptValue(-1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue DcNpcTeleportToClosestPostInternal(ScriptArgIterator& args, ScriptValue* argv, bool bWait)
{
	Npc* pNpc = args.NextNpc();
	const BoundFrame* pBoundFrame		  = args.NextBoundFrame();
	const DC::AiPostTypeMask postTypeMask = args.NextI32();

	ALWAYS_ASSERTF(postTypeMask != 0, ("Must provide post-type-mask argument to npc-teleport-into-closest-post"));

	if (pNpc && pBoundFrame && postTypeMask != 0)
	{
		const Point posWs = pBoundFrame->GetTranslationWs();
		AiLogScript(pNpc,
					"%snpc-teleport-into-closest-post: near [%0.1f %0.1f %0.1f]",
					bWait ? "wait-" : "",
					float(posWs.X()),
					float(posWs.Y()),
					float(posWs.Z()));

		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

		if (bWait)
		{
			pGroupInst->WaitForTrackDone("wait-npc-teleport-into-closest-post");
		}

		const U16 postId = AiLibUtil::FindClosestPostWs(postTypeMask, pBoundFrame->GetTranslation(), 10.0f);

		pNpc->ScriptCommandTeleportToPost(postId, pGroupInst, pTrackInst ? pTrackInst->GetTrackIndex() : 0, bWait);

		if (bWait)
		{
			ScriptContinuation::Suspend(argv);
		}
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static CoverActionPack::PreferredDirection ActionPackPreferredDirectionDcToGame(DC::ActionPackPreferredDirection x)
{
	switch (x)
	{
	case DC::kActionPackPreferredDirectionPreferNone:
		return CoverActionPack::kPreferNone;
		break;
	case DC::kActionPackPreferredDirectionPreferLeft:
		return CoverActionPack::kPreferLeft;
		break;
	case DC::kActionPackPreferredDirectionPreferRight:
		return CoverActionPack::kPreferRight;
		break;
	default:
		AI_HALTF(("Unrecognized action pack preferred direction!\n"));
		break;
	}

	return CoverActionPack::kPreferNone;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue DcNpcUseClosestPostInternal(ScriptArgIterator& args, ScriptValue* argv, bool bWait)
{
	Npc* pNpc = args.NextNpc();

	const BoundFrame* pBoundFrame		  = args.NextBoundFrame();
	const DC::AiPostTypeMask postTypeMask = args.NextI32();
	const DC::NpcMotionType dcMotionType  = args.NextI32();
	const StringId64 mtSubcategory		  = args.NextStringId();

	const bool tryToShoot		 = args.NextBoolean();
	const bool drawWeapon		 = args.NextBoolean();
	const bool useLocatorDir	 = args.NextBoolean();
	const bool suppressPeek		 = args.NextBoolean();
	const bool disablePlayerPush = args.NextBoolean();
	const bool disablePlayerEvades = args.NextBoolean();
	const DC::ActionPackPreferredDirection coverFacingDir = args.NextI32();

	AI_ASSERTF(postTypeMask != 0, ("Must provide post-type-mask argument to npc-use-closest-post"));

	if (pNpc && pBoundFrame && postTypeMask != 0)
	{
		const MotionType motionType = DcMotionTypeToGame(dcMotionType);
		const Point posWs  = pBoundFrame->GetTranslationWs();
		const Vector dirWs = GetLocalZ(pBoundFrame->GetRotation());

		AiLogScript(pNpc,
					"%snpc-use-closest-post: near [%0.1f %0.1f %0.1f]",
					bWait ? "wait-" : "",
					float(posWs.X()),
					float(posWs.Y()),
					float(posWs.Z()));

		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();
		U32 trackIndex = pTrackInst ? pTrackInst->GetTrackIndex() : 0;

		if (bWait)
		{
			pGroupInst->WaitForTrackDone("wait-npc-use-closest-post");
		}

		NavMoveArgs moveArgs;
		moveArgs.m_motionType = motionType;
		moveArgs.m_mtSubcategory = mtSubcategory;
		moveArgs.m_apUserData = ActionPackPreferredDirectionDcToGame(coverFacingDir);

		const U16 postId = AiLibUtil::FindClosestPostWs(postTypeMask, posWs, 10.0f, useLocatorDir, dirWs);

		pNpc->ScriptCommandUsePost(postId,
								   moveArgs,
								   GetLocalZ(pBoundFrame->GetLocator()),
								   tryToShoot,
								   drawWeapon,
								   false,
								   pGroupInst,
								   trackIndex,
								   bWait);

		Npc::ScriptCommandInfo& cmd			= pNpc->GetScriptCommandInfo();
		cmd.m_command.m_suppressPeek		= suppressPeek;
		cmd.m_command.m_disablePlayerEvades = disablePlayerEvades;
		cmd.m_command.m_disablePlayerPush	= disablePlayerPush;

		if (bWait)
		{
			ScriptContinuation::Suspend(argv);
		}
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue DcNpcMoveToPostInternal(ScriptArgIterator& args, ScriptValue* argv, bool bWait, bool bSearch)
{
	Npc* pNpc = args.NextNpc();
	const DC::AiPost* pPost = args.NextPointer<DC::AiPost>();
	const DC::NpcMotionType dcMotionType = args.NextI32();
	const DC::NpcGoalReachedType dcGoalReachedType = args.NextI32();
	const StringId64 mtSubcategory = args.NextStringId();
	const bool tryToShoot = args.NextBoolean();
	const bool drawWeapon = args.NextBoolean();
	const bool strafe	  = args.NextBoolean();
	const Vector faceDir  = args.NextVector();
	const StringId64 performanceId = args.NextStringId();

	if (pNpc && pPost)
	{
		const MotionType motionType		   = DcMotionTypeToGame(dcMotionType);
		NavGoalReachedType goalReachedType = ConvertDcGoalReachedType(dcGoalReachedType);

		AiLogScript(pNpc, "%snpc-move-to-post: %d", bWait ? "wait-" : "", pPost->m_id);

		SsTrackGroupInstance* pGroupInst  = GetJobSsContext().GetGroupInstance();
		const SsTrackInstance* pTrackInst = GetJobSsContext().GetTrackInstance();
		const U32 trackIndex = pTrackInst ? pTrackInst->GetTrackIndex() : 0;

		if (bWait)
		{
			pGroupInst->WaitForTrackDone("wait-npc-use-closest-post");
		}

		NavMoveArgs moveArgs;
		moveArgs.m_motionType	   = motionType;
		moveArgs.m_mtSubcategory   = mtSubcategory;
		moveArgs.m_goalReachedType = goalReachedType;
		moveArgs.m_strafeMode	   = strafe ? DC::kNpcStrafeAlways : DC::kNpcStrafeNever;
		moveArgs.m_performanceId   = performanceId;

		pNpc->ScriptCommandUsePost(pPost->m_id,
								   moveArgs,
								   faceDir,
								   tryToShoot,
								   drawWeapon,
								   bSearch,
								   pGroupInst,
								   trackIndex,
								   bWait);

		if (bWait)
		{
			ScriptContinuation::Suspend(argv);
		}
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-teleport-into-closest-post
		(
			(npc-name			symbol)
			(location			bound-frame)
			(post-type-mask		ai-post-type-mask)
		)
		none
	)
*/
SCRIPT_FUNC("npc-teleport-into-closest-post", DcNpcTeleportIntoClosestPost)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	return DcNpcTeleportToClosestPostInternal(args, argv, false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function wait-npc-teleport-into-closest-post
		(
			(npc-name			symbol)
			(location			bound-frame)
			(post-type-mask		ai-post-type-mask)
		)
		none
	)
*/
SCRIPT_FUNC("wait-npc-teleport-into-closest-post", DcWaitNpcTeleportIntoClosestPost)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	return DcNpcTeleportToClosestPostInternal(args, argv, true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-use-closest-post
		(
			(npc-name			symbol)
			(location			bound-frame)
			(post-type-mask		ai-post-type-mask)
			(motion-type		npc-motion-type	(:default (npc-motion-type sprint)))
			(mt-subcategory		symbol			(:default 0))
			(try-to-shoot		boolean			(:default #f))
			(draw-weapon		boolean			(:default #t))
			(use-locator-dir    boolean         (:default #f))
			(suppress-peek		boolean			(:default #f))
			(cover-facing-dir	action-pack-facing-dir (:default (action-pack-facing-dir implied))) ;; if this is a cover-over low cover, the npc can face either direction
		)
		none
	)
*/
SCRIPT_FUNC("npc-use-closest-post", DcNpcUseClosestPost)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 10);

	return DcNpcUseClosestPostInternal(args, argv, false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function wait-npc-use-closest-post
		(
			(npc-name			symbol)
			(location			bound-frame)
			(post-type-mask		ai-post-type-mask)
			(motion-type		npc-motion-type	(:default (npc-motion-type sprint)))
			(mt-subcategory		symbol			(:default 0))
			(try-to-shoot		boolean			(:default #f))
			(draw-weapon		boolean			(:default #t))
			(use-locator-dir    boolean         (:default #f))
			(suppress-peek		boolean			(:default #f))
			(cover-facing-dir	action-pack-facing-dir (:default (action-pack-facing-dir implied))) ;; if this is a cover-over low cover, the npc can face either direction
		)
		none
	)
*/
SCRIPT_FUNC("wait-npc-use-closest-post", DcWaitNpcUseClosestPost)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 10);

	return DcNpcUseClosestPostInternal(args, argv, true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-move-to-post
		(
			(npc-name			symbol)
			(post				ai-post)
			(motion-type		npc-motion-type			(:default (npc-motion-type run)))
			(goal-reached-type	npc-goal-reached-type	(:default (npc-goal-reached-type stop)))
			(mt-subcategory		symbol					(:default 0))
			(try-to-shoot		boolean					(:default #f))
			(draw-weapon		boolean					(:default #t))
			(face-direction		vector					(:default *zero-vector*))
			(strafe				boolean					(:default #f))
		)
		none
	)
*/
SCRIPT_FUNC("npc-move-to-post", DcNpcMoveToPost)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	return DcNpcMoveToPostInternal(args, argv, false, false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function wait-npc-move-to-post
		(
			(npc-name			symbol)
			(post				ai-post)
			(motion-type		npc-motion-type			(:default (npc-motion-type run)))
			(goal-reached-type	npc-goal-reached-type	(:default (npc-goal-reached-type stop)))
			(mt-subcategory		symbol					(:default 0))
			(try-to-shoot		boolean					(:default #f))
			(draw-weapon		boolean					(:default #t))
			(strafe				boolean					(:default #f))
			(face-direction		vector					(:default *zero-vector*))
			(strafe				boolean					(:default #f))
		)
		none
	)
*/
SCRIPT_FUNC("wait-npc-move-to-post", DcWaitNpcMoveToPost)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	return DcNpcMoveToPostInternal(args, argv, true, false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-search-move-to-post
		(
			(npc-name			symbol)
			(post				ai-post)
			(motion-type		npc-motion-type			(:default (npc-motion-type run)))
			(goal-reached-type	npc-goal-reached-type	(:default (npc-goal-reached-type stop)))
			(mt-subcategory		symbol					(:default 0))
			(try-to-shoot		boolean					(:default #f))
			(draw-weapon		boolean					(:default #t))
			(strafe				boolean					(:default #f))
			(face-direction		vector					(:default *zero-vector*))
			(strafe				boolean					(:default #f))
		)
		none
	)
*/
SCRIPT_FUNC("npc-search-move-to-post", DcNpcSearchMoveToPost)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	return DcNpcMoveToPostInternal(args, argv, false, true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function wait-npc-search-move-to-post
		(
			(npc-name			symbol)
			(post				ai-post)
			(motion-type		npc-motion-type			(:default (npc-motion-type run)))
			(goal-reached-type	npc-goal-reached-type	(:default (npc-goal-reached-type stop)))
			(mt-subcategory		symbol					(:default 0))
			(try-to-shoot		boolean					(:default #f))
			(draw-weapon		boolean					(:default #t))
			(strafe				boolean					(:default #f))
			(face-direction		vector					(:default *zero-vector*))
			(strafe				boolean					(:default #f))
		)
		none
	)
*/
SCRIPT_FUNC("wait-npc-search-move-to-post", DcWaitNpcSearchMoveToPost)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	return DcNpcMoveToPostInternal(args, argv, true, true);
}



/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-reached-post?
	(
		(npc-name		symbol)
	)
	boolean
)
*/
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(bool,
								 DcNpcReachedPost,
								 (Npc* pNpc),
								 "npc-reached-post?")
{
	if (!pNpc)
		return false;

	return pNpc->IsAtAssignedPost();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-get-assigned-post
	(
		(npc-name		symbol)
	)
	ai-post
)
*/
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(DC::AiPost*,
								 DcNpcGetAssignedPost,
								 (Npc* pNpc),
								 "npc-get-assigned-post")
{
	if (!pNpc)
		return nullptr;

	const AiPost& assignedPost = pNpc->GetAssignedPost();
	if (!assignedPost.IsValid())
		return nullptr;

	// TODO cleaner conversion from AiPost to DC::AiPost (potentially just move AiPost to DC and use that everywhere!)
	DC::AiPost* pPost = NDI_NEW(kAllocSingleGameFrame, kAlign16) DC::AiPost;

	pPost->m_position = NDI_NEW(kAllocSingleGameFrame, kAlign16) Point(assignedPost.GetPositionWs());
	pPost->m_type	  = assignedPost.GetType();
	pPost->m_id		  = assignedPost.GetPostId();
	pPost->m_score	  = assignedPost.GetScore();

	return pPost;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	;; Tell an Npc to enable/disable friend check when throwing grenades
	(define-c-function npc-enable-grenade-friend-check
		(
			(npc-name symbol)
			(enable boolean)
		)
		none
	)
*/
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-weapon-loadout", DcNpcSetWeaponLoadout)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	StringId64 weaponLoadoutId = args.NextStringId();

	if (pNpc)
	{
		pNpc->SetWeaponLoadout(weaponLoadoutId);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-override-static-blockers/f", DcNpcOverrideStaticBlockersF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	DC::StaticBlockageMask mask = args.NextI32();

	if (pNpc)
	{
		pNpc->GetNavControl()->OverrideObeyedStaticBlockersFrame((Nav::StaticBlockageMask)mask);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-ignore-for-pathfinding", DcNpcSetIgnoreForPathfinding)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	NdGameObject* pObject = args.NextGameObject();

	if (pNpc && pObject)
	{
		pNpc->GetNavControl()->SetIgnoreNavBlocker(3, pObject);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-ignore-mesh-raycast-object", DcNpcSetIgnoreMeshRaycastObject)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	NdGameObject* pObject = args.NextGameObject();

	if (pNpc)
	{
		pNpc->SetMeshRayCastIgnoreProcessId(pObject ? pObject->GetProcessId() : -1);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-killed?", DcNpcKilledP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.GetNpc(); // don't advance
	const EntitySpawner* pSpawner = args.NextSpawner(kItemMissingOk);

	if (pNpc)
	{
		return ScriptValue(pNpc->IsDead());
	}
	else if (pSpawner)
	{
		return ScriptValue(pSpawner->GetFlags().m_killed);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-spawned?", DcNpcSpawnedP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	StringId64 npcId = args.NextStringId();
	Npc* pNpc		 = EngineComponents::GetNpcManager()->FindNpcByUserId(npcId);
	return ScriptValue(pNpc != nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-alive?", DcNpcAliveP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	StringId64 npcId = args.NextStringId();
	Npc* pNpc		 = EngineComponents::GetNpcManager()->FindNpcByUserId(npcId);
	return ScriptValue(pNpc && !pNpc->IsDead());
}

/// ---------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-infected-threat?", DcNpcInfectedThreat)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		return ScriptValue(pNpc->HasKnownInfectedThreats());
	}

	return ScriptValue(false);
}

/// ---------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-lobotomised?", DcNpcLobotomisedP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		return ScriptValue(pNpc->IsLobotomised());
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function request-path ((start point) (end point))
// 	int32)
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("request-path", DcNpcRequestPath)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pGo = args.NextGameObject();
	Point end		  = args.NextPoint();
	I32 result		  = 0;

	PathfindRequestHandle handle;
	handle.Invalidate();

	if (pGo)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		NavLocation startNavLoc = pGo->GetNavLocation();

		if (startNavLoc.IsValid())
		{
			Nav::FindSinglePathParams params;
			params.AddStartPosition(startNavLoc);
			if (pGo->IsKindOf(g_type_Buddy) || pGo->IsKindOf(g_type_PlayerBase))
				params.m_ignoreNodeExtraCost = true;

			if (const NavCharacter* pNavChar = NavCharacter::FromProcess(pGo))
			{
				if (const NavControl* pNavControl = pNavChar->GetNavControl())
				{
					params.ConstructPreferredPolys(pNavControl->GetCachedPathPolys());
				}
			}
			params.m_goal.SetWs(end);
			params.m_traversalSkillMask = -1;

			params.m_buildParams.m_smoothPath = Nav::kFullSmoothing;
			params.m_buildParams.m_truncateAfterTap = 0;
			params.m_playerBlockageCost = Nav::PlayerBlockageCost::kExpensive;
			handle = PathfindManager::Get().AddStaticRequest(SID("script-pathfind-request"), pGo, params);
		}
	}

	result = handle.GetRawHandle();
	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function request-path-with-radius ((start point) (end point))
// 	int32)
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("request-path-with-radius", DcNpcRequestPathWithRadius)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	NdGameObject* pGo = args.NextGameObject();
	Point end		  = args.NextPoint();
	float radius	  = args.NextFloat();
	bool dynamicSearch = args.NextBoolean();
	I32 result		   = 0;

	PathfindRequestHandle handle;
	handle.Invalidate();

	if (pGo)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		NavLocation startNavLoc = pGo->GetNavLocation();
		Nav::StaticBlockageMask staticBlockageMask = Nav::kStaticBlockageMaskNone;
		Player* pPlayer = Player::FromProcess(pGo);

		U32 traversalSkillMask = ~(1UL << DC::kTraversalSkillBoatRequired);

		if (pPlayer)
		{
			staticBlockageMask = Nav::kStaticBlockageMaskAccessibility;
			traversalSkillMask = ~(1UL << DC::kTraversalSkillExclusive1 | 1UL << DC::kTraversalSkillExclusive2
								   | 1UL << DC::kTraversalSkillExclusive3 | 1UL << DC::kTraversalSkillExclusive4
								   | 1UL << DC::kTraversalSkillBoatRequired);

			if (Horse* pHorse = pPlayer->GetHorse(false))
			{
				startNavLoc = pHorse->GetNavLocation();
				traversalSkillMask &= ~(1UL << DC::kTraversalSkillAccessibilityNoHorses);
				// Need to set horse-path-dist. This is going to be bad if there is a an optimal path that uses an avoidable no-horse tap
				// For that case mark the tap with accessibility-no-horse skill mask
				// staticBlockageMask = Nav::kStaticBlockageMaskHorse;
				// traversalSkillMask &= ~(1UL << DC::kTraversalSkillNoHorses);
			}
			else if (pPlayer->GetBoundBoat())
			{
				traversalSkillMask |= 1UL << DC::kTraversalSkillBoatRequired;
				traversalSkillMask &= ~(1UL << DC::kTraversalSkillNoBoats);
			}
		}
		else if (Npc* pNpc = Npc::FromProcess(pGo))
		{
			traversalSkillMask = pNpc->GetDefaultTraversalSkillMask();
			if (Horse* pHorse = pNpc->GetHorse())
			{
				startNavLoc = pHorse->GetNavLocation();
				staticBlockageMask = Nav::kStaticBlockageMaskHorse;
				traversalSkillMask &= ~(1UL << DC::kTraversalSkillNoHorses);
			}
			else
			{
				staticBlockageMask = pNpc->GetObeyedStaticBlockers();
			}
		}

		const NavMesh* pMesh = nullptr;
		const NavPoly* pPoly = startNavLoc.ToNavPoly(&pMesh);

		if (!startNavLoc.IsValid() || !pPoly || !pMesh
			|| !pPoly->PolyContainsPointLs(pMesh->WorldToLocal(startNavLoc.GetPosWs())))
		{
			if (pPlayer)
			{
				if (!pMesh)
					pMesh = pPlayer->GetLastValidNavLocation().ToNavMesh();

				if (pMesh)
				{
					Locator polyPs	  = pMesh->GetParentSpace();
					Point playerPosPs = polyPs.TransformPoint(pPlayer->GetTranslation());

					NavMesh::FindPointParams fpParams;
					fpParams.m_point = playerPosPs;
					fpParams.m_searchRadius			= 5.0f;
					fpParams.m_obeyedStaticBlockers = false;
					fpParams.m_crossLinks = false;

					pMesh->FindNearestPointPs(&fpParams);

					startNavLoc.SetPs(fpParams.m_nearestPoint, fpParams.m_pPoly);
				}
			}
		}

		if (startNavLoc.IsValid())
		{

			if (FALSE_IN_FINAL_BUILD(g_debugAccessibilityPathfind))
			{
				g_prim.Draw(DebugCross(startNavLoc.GetPosWs(), 0.4f, kColorOrange, PrimAttrib(), "LastValidPoly"),
							Seconds(3.0f));
			}

			if (const NavMesh* pStartMesh = startNavLoc.ToNavMesh())
			{
				NavMesh::ClearanceParams params;
				params.m_point		  = startNavLoc.GetPosPs();
				params.m_radius		  = 0.0f;
				params.m_dynamicProbe = false;
				params.m_obeyedStaticBlockers = staticBlockageMask;

				const NavMesh::ProbeResult res = pStartMesh->CheckClearancePs(&params);

				if (res != NavMesh::ProbeResult::kReachedGoal)
				{
					// Started inside a blocker.
					staticBlockageMask = 0;
				}
			}

			Nav::FindSinglePathParams params;
			params.AddStartPosition(startNavLoc);
			if (pGo->IsKindOf(g_type_Buddy))
				params.m_ignoreNodeExtraCost = true;
			if (const NavCharacter* pNavChar = NavCharacter::FromProcess(pGo))
			{
				if (const NavControl* pNavControl = pNavChar->GetNavControl())
				{
					params.ConstructPreferredPolys(pNavControl->GetCachedPathPolys());
					params.m_context.m_obeyedBlockers = pNavControl->GetCachedObeyedNavBlockers();
				}
			}

			if (pPlayer)
			{
				params.m_context.m_obeyedBlockers = pPlayer->BuildPlayerObeyedBlockerList();
			}

			params.m_goal.SetWs(end);
			params.m_traversalSkillMask = traversalSkillMask;
			params.m_factionMask		= BuildFactionMask(pGo->GetFactionId());

			params.m_buildParams.m_truncateAfterTap = 0;
			params.m_playerBlockageCost = Nav::PlayerBlockageCost::kFree;

			params.m_context.m_dynamicSearch = dynamicSearch;
			params.m_context.m_pathRadius	 = radius;
			params.m_context.m_obeyedStaticBlockers = staticBlockageMask;

			params.m_context.m_parentSpace	  = pMesh->GetParentSpace();
			params.m_buildParams.m_smoothPath = Nav::kFullSmoothing;
			params.m_buildParams.m_finalizePathWithProbes = true;
			params.m_buildParams.m_finalizeProbeMaxDist = 20.0f;

			//params.m_debugDrawSearch = true;
			//params.m_debugDrawTime = Seconds(2.0f);

			//Nav::DebugPrintFindSinglePathParams(params, kMsgOut);
			handle = PathfindManager::Get().AddStaticRequest(SID("script-pathfind-request"), pGo, params, false, pPlayer);
		}
	}

	result = handle.GetRawHandle();
	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function request-undirected-path-search ((object symbol))
// 	int32)
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("request-undirected-path-search", DcNpcRequestUndirectedPathSearch)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	const NdGameObject* const pGo = args.NextGameObject();

	PathfindRequestHandle handle;

	if (!pGo)
	{
		MsgConErr("Called request-undirected-path-search without a valid object\n");
		return ScriptValue(handle.GetRawHandle());
	}

	const Player* pPlayer = Player::FromProcess(pGo);
	const Npc* pNpc = Npc::FromProcess(pGo);
	const Horse* pHorse = pPlayer ? pPlayer->GetHorse(true) : Horse::FromProcess(pGo);

	Nav::FindUndirectedPathsParams params;
	params.m_factionMask = BuildFactionMask(pGo->GetFactionId());
	params.m_traversalSkillMask = ~(1UL << DC::kAiTraversalSkillBoatRequired);

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const NavLocation& startLoc = pHorse ? pHorse->GetNavLocation() : (pPlayer ? pPlayer->GetLastValidNavLocation() : pGo->GetNavLocation());
	if (!startLoc.IsValid())
		return ScriptValue(handle.GetRawHandle());

	params.AddStartPosition(startLoc);

	Nav::StaticBlockageMask staticBlockageMask = Nav::kStaticBlockageMaskNone;

	if (pNpc)
	{
		params.m_traversalSkillMask = pNpc->GetDefaultTraversalSkillMask();
		params.m_ignoreNodeExtraCost = pNpc->IsBuddyNpc();
	}

	if (pPlayer)
	{
		params.m_playerBlockageCost = Nav::PlayerBlockageCost::kFree;
		params.m_ignoreNodeExtraCost = true;
		staticBlockageMask |= Nav::kStaticBlockageMaskAccessibility;
	}

	if (pHorse)
	{
		staticBlockageMask |= Nav::kStaticBlockageMaskHorse;
	}
	else if (pNpc)
	{
		staticBlockageMask |= pNpc->GetObeyedStaticBlockers();
	}

	params.m_context.m_dynamicSearch = false;
	params.m_context.m_obeyedStaticBlockers = staticBlockageMask;

	const NavMesh* const pStartMesh = startLoc.ToNavMesh();
	SCRIPT_ASSERT(pStartMesh);
	if (!pStartMesh)
		return ScriptValue(handle.GetRawHandle());

	params.m_context.m_parentSpace = pStartMesh->GetParentSpace();
	params.m_context.m_ownerLocPs.SetPosition(startLoc.GetPosPs());

	handle = PathfindManager::Get().AddUndirectedRequest(SID("script-undirected-request"), pGo, params, false, true);

	return ScriptValue(handle.GetRawHandle());
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function delete-path-request ((handle int32))
// 	none)
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("delete-path-request", DcNpcDeletePathRequest)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	const I32 rawHandle = args.NextI32();

	if (rawHandle == -1)
		return ScriptValue(false);

	const PathfindRequestHandle hRequest(rawHandle);

	PathfindManager::Get().RemoveRequest(hRequest);

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function is-path-request-ready? ((int32 id))
// 	boolean)
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("is-path-request-ready?", DcIsPathRequestReady)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	I32 rawHandle = args.NextI32();

	PathfindRequestHandle handle(rawHandle);

	if (!handle.IsValid())
		return ScriptValue(false);

	const Nav::FindSinglePathResults* pSingleResults = nullptr;
	if (PathfindManager::Get().GetResults(handle, &pSingleResults))
	{
		if (pSingleResults->m_buildResults.m_goalFound)
			return ScriptValue(true);
	}

	const Nav::FindUndirectedPathsResults* pUndirectedResults = nullptr;
	if (PathfindManager::Get().GetResults(handle, &pUndirectedResults))
	{
		return ScriptValue(true);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function is-path-request-ready? ((int32 id))
// 	boolean)
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("is-path-request-error?", DcIsPathRequestError)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	I32 rawHandle = args.NextI32();

	PathfindRequestHandle handle(rawHandle);

	if (!handle.IsValid())
		return ScriptValue(true);

	const Nav::FindSinglePathResults* pSingleResults = nullptr;
	if (PathfindManager::Get().GetResults(handle, &pSingleResults))
	{
		if (!pSingleResults->m_buildResults.m_goalFound)
			return ScriptValue(true);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function approx-path-dist-from-undirected ((id int32) (object symbol))
// float)
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("approx-path-dist-from-undirected", DcApproxPathDistFromUndirected)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	const I32 rawHandle		  = args.NextI32();
	const NdGameObject* pObj = args.NextGameObject();

	if (!pObj)
		return ScriptValue(NDI_FLT_MAX);

	const PathfindRequestHandle pathHandle(rawHandle);
	if (!pathHandle.IsValid())
		return ScriptValue(NDI_FLT_MAX);

	NavLocation navLocation;

	NavCharacterAdapter pNavChar = NavCharacterAdapter::FromProcess(pObj);
	if (pNavChar->IsValid())
	{
		navLocation = pNavChar->GetNavLocation();
	}
	else if (const PlayerBase* pPlayer = PlayerBase::FromProcess(pObj))
	{
		navLocation = pPlayer->GetLastValidNavLocation();
	}
	// else Would have to do a FindNearestPointOnNavMesh

	NavMeshReadLockJanitor jj(FILE_LINE_FUNC);

	const float pathDist = PathfindManager::Get().GetFastApproxSmoothPathDistanceOnly(pathHandle, navLocation);

	return ScriptValue(pathDist);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function get-next-accessibility-waypoint ((int32 id))
// 	point)
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-next-accessibility-waypoint", DcGetNextAccessibilityWaypoint)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	I32 rawHandle = args.NextI32();
	float minDist = args.NextFloat();
	float maxDist = args.NextFloat();

	PathfindRequestHandle handle(rawHandle);

	DC::AccessibilityWaypoint* pResult = NDI_NEW(kAllocSingleFrame, kAlign16) DC::AccessibilityWaypoint;
	memset(pResult, 0, sizeof(DC::AccessibilityWaypoint));

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	bool validResponse = false;

	const Nav::FindSinglePathParams* pPathParams   = nullptr;
	const Nav::FindSinglePathResults* pPathResults = nullptr;

	const Player* pPlayer = GetPlayer();
	const Horse* pHorse	  = pPlayer && pPlayer->IsRidingHorse(false) ? pPlayer->GetHorse() : nullptr;
	const DriveableBoat* pBoat = pPlayer ? pPlayer->GetBoundBoat() : nullptr;

	// PathfindManager::Get().DebugDrawRequest(handle);

	PathfindManager::Get().GetParams(handle, &pPathParams);
	const Locator parentSpace = pPathParams ? pPathParams->m_context.m_parentSpace
											: (pHorse ? pHorse->GetParentSpace() : pPlayer->GetParentSpace());

	// If we're mid TAP, don't point us in the wrong direction
	const bool allowVaultWallY = pPlayer
		? DistSqr(pPlayer->GetTranslation(), pPlayer->GetLastValidNavLocation().GetPosWs()) < Sqr(0.5f)
		: false;

	if (PathfindManager::Get().GetResults(handle, &pPathResults))
	{
		PathWaypointsEx waypointsEx = pPathResults->m_buildResults.m_pathWaypointsPs;


		const Point startPoint		= waypointsEx.GetWaypoint(0);

		if (FALSE_IN_FINAL_BUILD(g_debugAccessibilityPathfind))
		{

			Locator loc = Locator(kIdentity);
			waypointsEx.DebugDraw(loc, false, pPathParams->m_context.m_pathRadius, PathWaypointsEx::ColorScheme(), Seconds(3.0f));
		}

		float vaultWallY = -NDI_FLT_MAX;
		if (pPathResults->m_buildResults.m_goalFound)
		{
			pResult->m_success	  = true;
			pResult->m_distToGoal = pPathResults->m_buildResults.m_length;

			Point prevPoint	   = startPoint;
			Point waypoint	   = startPoint;
			Point nextWaypoint = startPoint;

			int i = 1;
			for (; i < waypointsEx.GetWaypointCount(); i++)
			{
				const Point nextPoint = waypointsEx.GetWaypoint(i);

				float distSqr = DistSqr(nextPoint, startPoint);
				prevPoint	  = waypoint;
				waypoint	  = nextPoint;

				if (waypointsEx.GetNodeType(i) == NavPathNode::kNodeTypeActionPackEnter
					&& distSqr < Sqr(minDist))
				{
					if (const TraversalActionPack* pTap = waypointsEx.GetActionPackHandle(i)
						.ToActionPack<TraversalActionPack>())
					{
						const Vector tapDir = SafeNormalize(pTap->GetExitPointWs() - pTap->GetDefaultEntryPointWs(1.0f), GetLocalZ(pTap->GetLocatorWs()));
						Scalar legLen = minDist;
						const Vector legDir = SafeNormalize(waypoint - prevPoint, tapDir, legLen);

						const F32 dotLimit = LerpScale(0.0f, minDist, -0.4f, 0.8f, legLen);
						const F32 dotLegTap = Dot(legDir, tapDir);
						if (distSqr < 1.0f || dotLegTap > dotLimit)
							continue;
					}
				}

				if (waypointsEx.GetNodeType(i) == NavPathNode::kNodeTypeActionPackExit)
				{
					if (const TraversalActionPack* pTap = waypointsEx.GetActionPackHandle(i)
															  .ToActionPack<TraversalActionPack>())
					{
						if (allowVaultWallY)
							vaultWallY = prevPoint.Y() + pTap->GetVaultWallHeight();

						if (const FeatureEdgeReference* pEdge = pTap->GetEdgeRef())
						{
							static const F32 kCameraSideOffsetFactor = 0.3f;

							const Quat facingQuat	  = QuatFromLookAt(VectorXz(waypoint - startPoint), kUnitYAxis);
							const Vector cameraOffset = GetLocalX(facingQuat)
														* (pPlayer->m_gunSide == Player::kSideLeft
															   ? kCameraSideOffsetFactor
															   : -kCameraSideOffsetFactor);

							const Point p0		= pEdge->GetVert0();
							const Point p1		= pEdge->GetVert1();
							const Point testPos = startPoint + cameraOffset;
							Point closestPoint	= kInvalidPoint;
							DistPointSegment(testPos, p0, p1, &closestPoint);
							if (!AllComponentsEqual(closestPoint, kInvalidPoint))
							{
								prevPoint = waypoint;
								waypoint = closestPoint;

								if (FALSE_IN_FINAL_BUILD(g_debugAccessibilityPathfind))
								{
									const Vector nudge = Vector(0.0f, 0.3f, 0.0f);
									g_prim.Draw(DebugArrow(parentSpace.TransformPoint(p0 + nudge),
														   parentSpace.TransformPoint(closestPoint + nudge),
														   kColorMagenta,
														   0.3f,
														   PrimAttrib(),
														   "VarTap Adjust"),
												Seconds(3.0f));
									g_prim.Draw(DebugArrow(parentSpace.TransformPoint(p1 + nudge),
														   parentSpace.TransformPoint(closestPoint + nudge),
														   kColorMagenta,
														   0.3f),
												Seconds(3.0f));
								}
							}
						}
					}
				}

				if (i + 1 < waypointsEx.GetWaypointCount())
				{
					const int futureIdx		= i + 1;
					const Point futurePoint = waypointsEx.GetWaypoint(futureIdx);

					if (waypointsEx.GetNodeType(futureIdx) == NavPathNode::kNodeTypeActionPackEnter
						&& DistSqr(waypoint, futurePoint) < Sqr(0.4f) && DistSqr(futurePoint, startPoint) < Sqr(minDist))
					{
						continue;
					}
				}

				break;
			}

			nextWaypoint = waypoint;
			if (i + 1 < waypointsEx.GetWaypointCount())
			{
				nextWaypoint = waypointsEx.GetWaypoint(i + 1);
			}

			Point cameraAimPos = waypoint;

			Vector path = waypoint - startPoint;
			float len	= (float)Length(path);
			if (len > maxDist)
			{
				path *= maxDist / len;
				nextWaypoint = waypoint;
				waypoint	 = Point(startPoint + path);
				cameraAimPos = waypoint;
			}
			if (len < minDist && len > NDI_FLT_EPSILON)
			{
				path *= minDist / len;
				cameraAimPos = Point(startPoint + path);
			}

			const F32 yAdjust = pHorse
				? LerpScale(0.2f, 1.5f, 2.0f, 0.0f, cameraAimPos.Y() - startPoint.Y())
				: LerpScale(0.2f, 1.5f, 1.0f, 0.0f, cameraAimPos.Y() - startPoint.Y());
			cameraAimPos.SetY(Max(cameraAimPos.Y() + yAdjust, vaultWallY));

			pResult->m_waypoint		= NDI_NEW(kAllocSingleGameFrame, kAlign16) Point(parentSpace.TransformPoint(waypoint));
			pResult->m_nextWaypoint = NDI_NEW(kAllocSingleGameFrame, kAlign16) Point(parentSpace.TransformPoint(nextWaypoint));
			pResult->m_cameraAimPos = NDI_NEW(kAllocSingleGameFrame, kAlign16) Point(parentSpace.TransformPoint(cameraAimPos));
			pResult->m_horsePathValidDist = pHorse ? 0.0f : -1.0f;
			pResult->m_boatPathValidDist = pBoat ? 0.0f : -1.0f;

			if (pHorse || pBoat)
			{
				float& updateDist = pHorse ? pResult->m_horsePathValidDist : pResult->m_boatPathValidDist;
				prevPoint		  = waypointsEx.GetWaypoint(0);
				const NavLocation startLoc = waypointsEx.GetAsNavLocation(parentSpace, 0);
				const NavMesh* pMesh	   = startLoc.ToNavMesh();
				const NavPoly* pPoly	   = startLoc.ToNavPoly();

				for (int iPoint = 1; iPoint < waypointsEx.GetWaypointCount(); iPoint++)
				{
					const NavPathNode::NodeType nodeType =  waypointsEx.GetNodeType(iPoint);
					const Point nextPoint = waypointsEx.GetWaypoint(iPoint);

					if (nodeType == NavPathNode::kNodeTypeActionPackExit)
					{
						prevPoint = nextPoint;
						updateDist += 2.0f;

						const NavLocation tapLoc = waypointsEx.GetAsNavLocation(parentSpace, iPoint);
						pMesh	   = tapLoc.ToNavMesh();
						pPoly	   = tapLoc.ToNavPoly();
						continue;
					}

					if (pPoly)
					{
						if (pPoly->IsLink())
						{
							pPoly = pPoly->GetLinkedPoly(&pMesh);
							if (pPoly)
								pMesh = pPoly->GetNavMesh();
						}
					}

					if (!pMesh)
						break;

					const Locator meshPs = pMesh->GetParentSpace();

					NavMesh::ProbeParams params;
					params.m_pStartPoly	 = pPoly;
					params.m_start		 = meshPs.UntransformPoint(parentSpace.TransformPoint(prevPoint));
					params.m_move		 = meshPs.UntransformVector(parentSpace.TransformVector(nextPoint - prevPoint));
					params.m_probeRadius = 0.0f;
					params.m_obeyedBlockers.ClearAllBits();
					params.m_dynamicProbe = false;
					params.m_crossLinks = true;

					if (pHorse)
					{
						if (const NavControl* pNavControl = pHorse->GetNavControl())
						{
							params.m_obeyedBlockers = pNavControl->GetCachedObeyedNavBlockers();
							params.m_dynamicProbe = true;
						}

						params.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskHorse;
					}

					const NavMesh::ProbeResult res = pMesh->ProbePs(&params);

					const Point reachedPoint = params.m_endPoint;
					const bool clearLine	 = res == NavMesh::ProbeResult::kReachedGoal;

					const F32 legLength = Dist(prevPoint, reachedPoint);
					updateDist += legLength;

					if (!clearLine)
						break;

					if (updateDist > 20.0f)
						break;

					if (nodeType == NavPathNode::kNodeTypeActionPackEnter)
					{
						const TraversalActionPack* pTap = waypointsEx.GetActionPackHandle(iPoint)
															  .ToActionPack<TraversalActionPack>();
						AI_ASSERT(pTap);

						// Can we make it over?
						if (pBoat)
						{
							if ((pTap->GetTraversalSkillMask() & (1UL << DC::kTraversalSkillBoatRequired)) == 0)
							{
								break;
							}
						}
						else
						{
							AI_ASSERT(pHorse);
							if (pTap->GetTraversalSkillMask() & (1UL << DC::kTraversalSkillNoHorses))
							{
								break;
							}
						}
					}

					pPoly = params.m_pReachedPoly;
					pMesh = params.m_pReachedMesh;
					prevPoint = nextPoint;
				}

				if (FALSE_IN_FINAL_BUILD(g_debugAccessibilityPathfind))
				{
					if (!AllComponentsEqual(prevPoint, kInvalidPoint))
					{
						g_prim.Draw(DebugStringFmt(parentSpace.TransformPoint(*pResult->m_waypoint + Vector(0.0f, 0.4f, 0.0f)),
												   kColorGreen,
												   0.7f,
												   pHorse ? "horse dist: %.2f" : "boat dist: %.2f",
												   pHorse ? pResult->m_horsePathValidDist: pResult->m_boatPathValidDist),
									Seconds(3.0f));
					}
				}
			}
		}
	}

	if (FALSE_IN_FINAL_BUILD(g_debugAccessibilityPathfind))
	{
		g_prim.Draw(DebugCross(*pResult->m_waypoint, 0.3f, kColorWhite, PrimAttrib(), "Waypoint"), Seconds(3.0f));
		g_prim.Draw(DebugCross(*pResult->m_nextWaypoint, 0.3f, kColorBlue, PrimAttrib(), "NextWaypoint"), Seconds(3.0f));
		g_prim.Draw(DebugCross(*pResult->m_cameraAimPos, 0.3f, kColorRed, PrimAttrib(), "CameraLookAt"), Seconds(3.0f));

		if (pPathResults && pPathResults->m_buildResults.m_goalFound)
		{
			g_prim.Draw(DebugCross(parentSpace.TransformPoint(pPathResults->m_buildResults.m_pathWaypointsPs.GetEndWaypoint()),
								   0.3f,
								   kColorYellow,
								   PrimAttrib(),
								   "Goal"),
						Seconds(3.0f));
		}
	}
	return ScriptValue(pResult);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function get-next-path-waypoint ((int32 id))
// 	point)
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-next-path-waypoint", DcGetNextPathWaypoint)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	I32 rawHandle  = args.NextI32();
	float distance = args.NextFloat();

	PathfindRequestHandle handle(rawHandle);

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	const Nav::FindSinglePathResults* pResults = nullptr;
	if (PathfindManager::Get().GetResults(handle, &pResults))
	{
		if (pResults->m_buildResults.m_goalFound)
		{
			Point* pPoint = NDI_NEW(kAllocSingleGameFrame, kAlign16)
				Point(pResults->m_buildResults.m_pathWaypointsPs.GetPointAtDistance(distance));
			return ScriptValue(pPoint);
		}
	}

	return ScriptValue(NDI_NEW(kAllocSingleGameFrame, kAlign16) Point(kZero));
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-location-along-path", DcGetPathLocationInFuture)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc	   = args.NextNpc();
	float distance = args.NextFloat();

	Point* pPoint = NDI_NEW(kAllocSingleGameFrame, kAlign16) Point(kZero);

	if (pNpc)
	{
		*pPoint = pNpc->GetTranslation();
		const PathWaypointsEx* pPath = pNpc->GetPathPs();
		if (pPath)
		{
			*pPoint = pPath->GetPointAtDistance(distance);
		}
	}

	return ScriptValue(pPoint);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(bool, DcNpcPathUsesTapP, (Character & npcChar), "npc-path-uses-tap?")
{
	Npc& npc = static_cast<Npc&>(npcChar);
	const NavLocation destLoc = npc.GetFinalNavGoal();
	bool usesTap = false;
	return npc.ApproxPathUsesTap(destLoc, usesTap) && usesTap;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(bool, DcNpcPathToObjectUsesTapP, (Character & npcChar, NdGameObject & obj), "npc-path-to-object-uses-tap?")
{
	Npc& npc = static_cast<Npc&>(npcChar);
	const NavLocation destLoc = obj.GetNavLocation();
	bool usesTap = false;
	return npc.ApproxPathUsesTap(destLoc, usesTap) && usesTap;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(bool, DcPathGoesThroughFireP, (I32 rawHandle), "path-goes-through-fire?")
{
	PathfindRequestHandle handle(rawHandle);

	const Nav::FindSinglePathResults* pResults = nullptr;
	const Nav::FindSinglePathParams* pParams   = nullptr;

	PathfindManager& pfm = PathfindManager::Get();
	if (!pfm.GetResults(handle, &pResults) || !pfm.GetParams(handle, &pParams) || !pResults || !pParams)
	{
		return false;
	}

	if (!pResults->m_buildResults.m_goalFound || !pResults->m_buildResults.m_pathWaypointsPs.IsValid())
	{
		return false;
	}

	return AI::DoesPathGoThroughFire(&pResults->m_buildResults.m_pathWaypointsPs, pParams->m_context.m_parentSpace);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(bool, DcNpcPathGoesThroughFireP, (Character & npcChar), "npc-path-goes-through-fire?")
{
	Npc& npc = static_cast<Npc&>(npcChar);

	const IPathWaypoints* pPathPs = npc.GetPathPs();
	const Locator& parentSpace	  = npc.GetParentSpace();

	return AI::DoesPathGoThroughFire(pPathPs, parentSpace);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-enable-skill ((npc-name symbol) (skill-index ai-archetype-skill))
		none)
*/
SCRIPT_FUNC("npc-enable-skill", DcNpcEnableSkill)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	if (Npc* pNpc = args.NextNpc())
	{
		DC::AiArchetypeSkill skillToEnable = args.NextI32();

		if (skillToEnable < 0 || skillToEnable >= DC::kAiArchetypeSkillCount)
		{
			args.MsgScriptError("this function takes a skill, not a SID\n");
			return ScriptValue(0);
		}

		AiLogScript(pNpc, "npc-enable-skill (%s)", pNpc->GetSkillMgr()->GetSkillName(skillToEnable));
		pNpc->EnableSkill(skillToEnable);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-disable-skill ((npc-name symbol) (skill-index ai-archetype-skill))
		none)
*/
SCRIPT_FUNC("npc-disable-skill", DcNpcDisableSkill)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	if (Npc* pNpc = args.NextNpc())
	{
		DC::AiArchetypeSkill skillToDisable = args.NextI32();

		if (skillToDisable < 0 || skillToDisable >= DC::kAiArchetypeSkillCount)
		{
			args.MsgScriptError("this function takes a skill, not a SID\n");
			return ScriptValue(0);
		}

		AiLogScript(pNpc, "npc-disable-skill (%s)", pNpc->GetSkillMgr()->GetSkillName(skillToDisable));
		pNpc->DisableSkill(skillToDisable);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-skill-state", DcNpcGetSkillState)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	if (Npc* pNpc = args.NextNpc())
	{
		if (pNpc->GetActiveSkill())
			return ScriptValue(pNpc->GetActiveSkill()->GetStateId());
	}
	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-using-skill? ((npc-name symbol) (skill-index ai-archetype-skill))
boolean)
*/
SCRIPT_FUNC("npc-using-skill?", DcNpcUsingSkillP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	if (Npc* pNpc = args.NextNpc())
	{
		DC::AiArchetypeSkill skillToCheck = args.NextI32();
		if (pNpc->GetActiveSkill() && pNpc->GetActiveSkill() == pNpc->GetSkillMgr()->GetSkill(skillToCheck))
			return ScriptValue(true);
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-get-active-skill ((npc-name symbol))
	ai-archetype-skill)
*/
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(DC::AiArchetypeSkill,
								 DcNpcGetActiveSkill,
								 (Npc* pNpc),
								 "npc-get-active-skill")
{
	if (!pNpc)
		return DC::kAiArchetypeSkillNone;

	return pNpc->GetActiveSkillNum();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-stop-script ((npc-name symbol))
		none)
*/
SCRIPT_FUNC("npc-stop-script", DcNpcStopScript)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const bool stopIgc = args.NextBoolean();

	if (!pNpc)
		return ScriptValue(0);

	if (ScriptSkill* pScriptSkill = pNpc->GetActiveSkillAs<ScriptSkill>(SID("ScriptSkill")))
	{
		AiLogScript(pNpc, "npc-stop-script %s", DcTrueFalse(stopIgc));
		pScriptSkill->StopCommand(pNpc, stopIgc);
	}
	else
	{
		Npc::ScriptCommandInfo& cmd = pNpc->GetScriptCommandInfo();
		AiLogScript(pNpc, "npc-stop-script (outside skill) %s", DcTrueFalse(stopIgc));
		cmd.m_ssAction.Stop();
		cmd = Npc::ScriptCommandInfo();
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-traversal-skill-set", DcNpcSetTraversalSkillSet)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	if (Npc* pNpc = args.NextNpc())
	{
		const StringId64 skillSetId = args.NextStringId();
		AiLogScript(pNpc, "npc-set-traversal-skill-set %s", DevKitOnly_StringIdToString(skillSetId));
		pNpc->SetTraversalSkillSet(skillSetId);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-add-traversal-skill", DcNpcAddTraversalSkill)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	if (Npc* pNpc = args.NextNpc())
	{
		I32 skill = args.NextI32();
		AiLogScript(pNpc, "npc-add-traversal-skill %d", skill);
		pNpc->AddTraversalSkill(skill);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-remove-traversal-skill", DcNpcRemoveTraversalSkill)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	if (Npc* pNpc = args.NextNpc())
	{
		I32 skill = args.NextI32();
		AiLogScript(pNpc, "npc-remove-traversal-skill %d", skill);
		pNpc->RemoveTraversalSkill(skill);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-using-traversal-skill?", DcNpcUsingTraversalSkillP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	bool usingTraversalSkill = false;
	if (Npc* pNpc = args.NextNpc())
	{
		const I32 skillIndexToCheck = args.NextI32();
		if (const TraversalActionPack* pTap = pNpc->GetTraversalActionPack())
		{
			usingTraversalSkill = (pTap->GetTraversalSkillMask() & (1U << skillIndexToCheck)) != 0;
		}
	}
	return ScriptValue(usingTraversalSkill);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("wait-npc-patrol", DcWaitNpcPatrol)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	StringId64 patrolSplineId = args.NextStringId();

	if (pNpc && patrolSplineId != INVALID_STRING_ID_64)
	{
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

		pGroupInst->WaitForTrackDone("wait-npc-patrol");

		AiLogScript(pNpc, "wait-npc-patrol");
		pNpc->ScriptCommandPatrol(patrolSplineId,
								  pGroupInst,
								  pTrackInst ? pTrackInst->GetTrackIndex() : -1,
								  pGroupInst != nullptr);

		ScriptContinuation::Suspend(argv);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("net-npc-patrol", DcNetNpcPatrol)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	StringId64 patrolSplineId = args.NextStringId();

	if (pNpc && patrolSplineId != INVALID_STRING_ID_64)
	{
		if (!g_netInfo.IsNetActive())
		{
			args.MsgScriptError("called on %s, when net is not active!\n", pNpc->GetName());
		}
		else
		{
			SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
			SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();
			AiLogScript(pNpc, "net-npc-patrol");
			pNpc->ScriptCommandPatrol(patrolSplineId, pGroupInst, pTrackInst->GetTrackIndex(), false);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-ignore", DcNpcSetIgnore)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	if (Npc* pNpc = args.NextNpc())
	{
		bool ignore = args.NextBoolean();
		AiLogScript(pNpc, "npc-set-ignore %s", DcTrueFalse(ignore));
		pNpc->SetIgnoredByNpcs(ignore);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-ignore/f", DcNpcSetIgnoreF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	if (Npc* pNpc = args.NextNpc())
	{
		AiLogScript(pNpc, "npc-set-ignore/f");
		pNpc->SetIgnoredByNpcsForOneFrame();
	}
	return ScriptValue(0);
}

/*
(define-c-function npc-get-time-since-last-begging-for-life ((npc-name symbol))
	float)
*/
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-time-since-last-begging-for-life", DcNpcGetTimeSinceLastBeggingForLife)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	if (Npc* pNpc = args.NextNpc())
	{
		if (pNpc->GetLastBegForLifeTime() != TimeFrameNegInfinity())
		{
			return ScriptValue(ToSeconds(pNpc->GetTimeSinceLastBegForLife()));
		}
	}
	return ScriptValue(-1.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-untargetable-by-buddies/f", DcNpcSetUntargetableByBuddiesF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	if (Npc* pNpc = args.NextNpc())
	{
		AiLogScript(pNpc, "npc-set-untargetable-by-buddies/f");
		pNpc->SetUntargetableByBuddiesForOneFrame();
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-find-nav-mesh-y-threshold", DcNpcSetFindNavMeshYThreshold)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	if (Npc* pNpc = args.NextNpc())
	{
		float yThreshold = args.NextFloat();
		AiLogScript(pNpc, "npc-set-find-nav-mesh-y-threshold");
		pNpc->GetNavControl()->SetPathYThreshold(yThreshold);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-enable-character-collider", DcNpcEnableCharacterCollider)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	if (Npc* pNpc = args.NextNpc())
	{
		pNpc->EnableCharacterCollider();
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-disable-character-collider", DcNpcDisableCharacterCollider)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	if (Npc* pNpc = args.NextNpc())
	{
		pNpc->DisableCharacterCollider();
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-off-ground?", DcNpcIsOffGroundP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	const Npc* pNpc = args.NextNpc();
	const F32 minHeightDelta = args.NextFloat();

	if (pNpc)
	{
		if (pNpc->GetCurrentPat() == Pat(0))
		{
			return ScriptValue(true);
		}
		else
		{
			const F32 groundDelta = pNpc->GetTranslationPs().Y() - pNpc->GetFilteredGroundPosPs().Y();
			if (groundDelta >= minHeightDelta)
				return ScriptValue(true);
		}
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-zone-region", DcNpcSetZoneRegion)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const StringId64 regionNameId = args.NextRegionId();

	if (regionNameId == INVALID_STRING_ID_64)
	{
		args.MsgScriptError("%s: Invalid zone region\n", pNpc->GetName());
		return ScriptValue(0);
	}

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-set-zone-region");
		EngineComponents::GetNpcManager()->SetZoneFromRegion(pNpc, regionNameId);

		if (!pNpc->GetZone().IsValid())
		{
			args.MsgScriptError("%s: Invalid zone region (%s)\n",
								pNpc->GetName(),
								DevKitOnly_StringIdToString(regionNameId));
			return ScriptValue(0);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-zone-marker", DcNpcSetZoneMarker)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	Npc* pNpc = args.NextNpc();

	const EntitySpawner* pSpawner = args.NextSpawner();
	float radius = args.NextFloat();

	if (pSpawner)
	{
		AiLogScript(pNpc, "npc-set-zone-marker");
		EngineComponents::GetNpcManager()->SetZoneFromSpawner(pNpc, *pSpawner, radius);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-lock-to-zone", DcNpcLockToZone)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();
	AiLogScript(pNpc, "npc-lock-to-zone");
	EngineComponents::GetNpcManager()->SetLockToZone(pNpc, true);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-has-zone?", DcNpcHasZoneP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	bool result = false;

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		if (const ZoneInfo* pZoneInfo = EngineComponents::GetNpcManager()->GetZoneInfo(pNpc))
		{
			result = (pZoneInfo->m_zone.GetRadius() > 0.0f && !pZoneInfo->m_zone.IsInfinite())
					 || pZoneInfo->m_zone.GetRegion();
		}
	}

	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-clear-zone", DcNpcClearZone)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	AiLogScript(pNpc, "npc-clear-zone");
	EngineComponents::GetNpcManager()->ClearZone(pNpc);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-zone-center-and-radius", DcNpcSetZoneCenterAndRadius)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	StringId64 userId = args.GetStringId(); // don't advance
	Npc* pNpc		  = args.NextNpc();

	const EntitySpawner* pSpawner = args.NextSpawner();
	float radius = args.NextFloat();

	if (pSpawner)
	{
		AiLogScript(pNpc, "npc-set-zone-center-and-radius");
		EngineComponents::GetNpcManager()->SetZoneCenterAndRadius(pNpc, pSpawner->GetBoundFrame(), radius);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-gesture-name-from-speaker-direction-and-weapon-type", DcGetSearchComeOnGestureNameAndWeaponType)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 6);
	Npc* pSpeaker  = args.NextNpc();
	Npc* pListener = args.NextNpc();
	StringId64 leftPistolGestureNameId	= args.NextStringId();
	StringId64 rightPistolGestureNameId = args.NextStringId();
	StringId64 leftLgGestureNameId		= args.NextStringId();
	StringId64 rightLgGestureNameId		= args.NextStringId();

	if (pSpeaker != nullptr && pListener != nullptr && leftPistolGestureNameId != INVALID_STRING_ID_64
		&& rightPistolGestureNameId != INVALID_STRING_ID_64 && leftLgGestureNameId != INVALID_STRING_ID_64
		&& rightLgGestureNameId != INVALID_STRING_ID_64)
	{
		// find left/right side.
		const Point listenerInSpeakerLocal = pSpeaker->GetLocator().UntransformPoint(pListener->GetTranslation());

		StringId64 weaponId = pSpeaker->GetWeaponId();
		const DC::WeaponArtDef* pWeaponArtDef	= ProcessWeaponBase::GetWeaponArtDef(weaponId);
		const DC::WeaponAnimType weaponAnimEnum = pWeaponArtDef ? pWeaponArtDef->m_animType
																: DC::kWeaponAnimTypeNoWeapon;

		bool isLongGun = false;
		switch (weaponAnimEnum)
		{
		case DC::kWeaponAnimTypeRifle:
		case DC::kWeaponAnimTypeShotgun:
			// case DC::kWeaponAnimTypeRifle:
			isLongGun = true;
			break;
		}

		if (listenerInSpeakerLocal.X() > 0.f)
			return ScriptValue(isLongGun ? leftLgGestureNameId : leftPistolGestureNameId);
		else
			return ScriptValue(isLongGun ? rightLgGestureNameId : rightPistolGestureNameId);
	}
	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-in-combat?", DcNpcInCombatP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	ScriptValue val(0);

	if (Npc* pNpc = args.NextNpc())
	{
		const AiEntity* pEntity = pNpc->GetCurrentTargetEntity();
		const bool isDanger		= (pEntity && pEntity->IsThreatType());
		val = ScriptValue(isDanger);
	}

	return val;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-in-investigation?", DcNpcInInvestigationP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		const bool inInvestigation = pNpc->IsInvestigating();
		return ScriptValue(inInvestigation);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-in-investigation-role?", DcNpcInInvestigationRoleP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 4);

	Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(false);

	const bool isInvestigator = args.NextBoolean();
	const bool isWatcher = args.NextBoolean();
	const bool isSignaler = args.NextBoolean();
	const bool result = SendEvent(SID("in-investigation-role?"), pNpc, isInvestigator, isWatcher, isSignaler).GetAsBool(false);

	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-ideal-investigation-signaler?", DcNpcIsIdealInvestigationSignalerP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 4);

	const Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(false);

	const AiEncounterCoordinator* pCoord = pNpc->GetEncounterCoordinator();
	if (!pCoord)
		return ScriptValue(false);

	if (!pCoord->IsIdealForInvestigateRole(pNpc, SID("Signaler")))
		return ScriptValue(false);

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-movement-speed-scale", DcNpcSetMovementSpeedScale)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	if (Npc* pNpc = args.NextNpc())
	{
		DC::AnimNpcInfo* pActorInfo = pNpc->GetAnimControl()->Info<DC::AnimNpcInfo>();
		const float scale = args.NextFloat();
		pActorInfo->m_designerMovementSpeedScale = scale;
		AiLogScript(pNpc, "npc-set-movement-speed-scale %.2f", scale);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-zone-update-method", DcNpcSetZoneUpdateMethod)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();

	DC::NpcZoneUpdateMethod method = args.NextI32();

	AiLogScript(pNpc, "npc-set-zone-update-method");
	if (!EngineComponents::GetNpcManager()->SetZoneUpdateMethod(pNpc, method))
	{
		args.MsgScriptError("Failed to set non-static zone update method because zone is region.\n");
		return ScriptValue(0);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-zone-center-object", DcNpcSetZoneCenterObject)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	Npc* pNpc = args.NextNpc();

	NdGameObject* pObj = args.NextGameObject();
	float radius	   = args.NextFloat();

	if (pObj)
	{
		AiLogScript(pNpc, "npc-set-zone-center-object");
		EngineComponents::GetNpcManager()->SetZoneFollowObject(pNpc, pObj, radius);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-perfect-aim", DcNpcSetPerfectAim)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc		= args.NextNpc();
	bool perfectAim = args.NextBoolean();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-set-perfect-aim");
		pNpc->GetConfiguration().m_perfectAim = perfectAim;
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-enable-reload", DcNpcEnableReload)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc		  = args.NextNpc();
	bool enableReload = args.NextBoolean();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-enable-reload");
		pNpc->GetConfiguration().m_enableReload = enableReload;
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-reloading?", DcNpcIsReloadingP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-is-reloading?");
		if (const IAiWeaponController* pWeaponController = pNpc->GetAnimationControllers()->GetWeaponController())
		{
			return ScriptValue(pWeaponController->IsReloading());
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-enable-shooting", DcNpcEnableShooting)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc	= args.NextNpc();
	bool enable = args.NextBoolean();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-enable-shooting", DcTrueFalse(enable));
		pNpc->GetConfiguration().m_shootingInfo.m_dontShoot = !enable;
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-enable-head-shots", DcNpcEnableHeadshots)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc	= args.NextNpc();
	bool enable = args.NextBoolean();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-enable-head-shots", DcTrueFalse(enable));
		pNpc->EnableHeadshots(enable);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Process* ThrowProjectileInternal(Point_arg sourcePosWs,
										Point_arg destPosWs,
										StringId64 weaponId,
										StringId64 processId,
										float speedOverride	   = -1.f,
										NdGameObject* pBoundTo = nullptr)
{
	const DC::WeaponGameplayDef* pWeaponDef = ProcessWeaponBase::GetWeaponGameplayDef(weaponId);
	if (!pWeaponDef)
		return nullptr;

	const StringId64 bulletId = pWeaponDef->m_grenadeGameplayDef ? pWeaponDef->m_grenadeGameplayDef->m_bulletSettings
																 : weaponId;
	const DC::BulletSettings* pBulletSettings = ProcessWeaponBase::LookupBulletSettings(bulletId);

	const float speed = speedOverride > 0.0f || !pBulletSettings ? speedOverride : pBulletSettings->m_emitSpeed;

	const Vector velocity = ComputeProjectileVelocity(sourcePosWs, destPosWs, pBoundTo ? (speed * 0.7f) : speed);

	Process* pProcess = nullptr;
	// T2-TODO:
	// if (weaponId == SID("mine-launcher"))
	//{
	//	pProcess = EmitMineProcess(processId, pBoundTo, Locator(sourcePosWs), destPosWs, bulletId);
	//}
	// else
	if (processId == SID("ProjectileGrenade"))
	{
		pProcess = EmitProjectileGrenade(pBoundTo, Locator(sourcePosWs), destPosWs, nullptr, kLargeFloat, velocity, bulletId, false, weaponId, true);
	}
	else
	{
		pProcess = EmitProjectileProcess(processId, pBoundTo, Locator(sourcePosWs), destPosWs, velocity, bulletId, weaponId, true);

		if (ProjectileThrowable* pThrowable = ProjectileThrowable::FromProcess(pProcess))
		{
			pThrowable->MakeActive(velocity);
		}
	}

	return pProcess;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function throw-projectile ((source-pos point) (dest-pos point) (weapon-id symbol) (speed float))
	none)
*/
SCRIPT_FUNC("throw-projectile", DcThrowProjectile)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	const Point sourcePosWs	  = args.NextPoint();
	const Point destPosWs	  = args.NextPoint();
	const StringId64 weaponId = args.NextStringId();
	const float speed		  = args.NextFloat();

	Process* pProc = ThrowProjectileInternal(sourcePosWs, destPosWs, weaponId, SID("ProjectileThrowable"), speed);
	if (pProc)
		return ScriptValue(pProc->GetUserId());
	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function throw-grenade ((source-pos point) (dest-pos point) (weapon-id symbol))
	none)
*/
SCRIPT_FUNC("throw-grenade", DcThrowGrenade)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	const Point sourcePosWs	  = args.NextPoint();
	const Point destPosWs	  = args.NextPoint();
	const StringId64 weaponId = args.NextStringId();

	Process* pProc = ThrowProjectileInternal(sourcePosWs, destPosWs, weaponId, SID("ProjectileGrenade"));
	if (pProc)
		return ScriptValue(pProc->GetUserId());
	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function infected-throw-projectile ((infected-id symbol) (dest-pos point) (left-hand boolean))
	none)
*/
SCRIPT_FUNC("infected-throw-projectile", DcInfectedThrowProjectile)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	Infected* pInfected = Infected::FromProcess(args.NextNpc());
	const Point destPos = args.NextPoint();
	const bool leftHand = args.NextBoolean();

	if (!pInfected)
		return ScriptValue(0);

	const StringId64 attachId = leftHand ? SID("leftHand") : SID("rightHand");
	const AttachIndex attachIndex = pInfected->GetAttachSystem()->FindPointIndexById(attachId);
	const Point sourcePos = pInfected->GetAttachSystem()->GetLocator(attachIndex).GetTranslation();

	const F32 speed = pInfected->GetThrowArcParams(Npc::kThrowArcIndexGrenadeLow).m_projectileSpeed;
	const Vector velocity = ComputeProjectileVelocity(sourcePos, destPos, speed);

	pInfected->ThrowProjectile(sourcePos, destPos, velocity, leftHand);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-request-throw-grenade ((npc-name symbol) (target-pos point))
	none)
*/
SCRIPT_FUNC("npc-request-throw-grenade", DcNpcRequestThrowGrenade)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();
	const Point targetPosWs = args.NextPoint();
	bool ignoreProbe		= args.NextBoolean();

	AI_ASSERT(IsReasonable(targetPosWs));

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-request-throw-grenade");
		pNpc->RequestGrenadeThrow(targetPosWs, ignoreProbe);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-request-throw-grenade-at-player ((npc-name symbol))
	none)
*/
SCRIPT_FUNC("npc-request-throw-grenade-at-player", DcNpcRequestThrowGrenadeAtPlayer)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		bool ignoreProbe = args.NextBoolean();

		if (const NdGameObject* pPlayer = EngineComponents::GetGameInfo()->GetPlayerGameObject())
		{
			AiLogScript(pNpc, "npc-request-throw-grenade-at-player");

			// Target a point one meter in front of the player
			const Point targetPosWs = pPlayer->GetLocator().Pos()
									  + Normalize(pNpc->GetLocator().Pos() - pPlayer->GetLocator().Pos());

			AI_ASSERT(IsReasonable(targetPosWs));

			pNpc->RequestGrenadeThrow(targetPosWs, ignoreProbe);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-turret-ignore-range", DcNpcSetTurretIgnoreRange)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	NdGameObject* pEntityGo = args.NextGameObject();

	if (pEntityGo)
	{
		if (pEntityGo && pEntityGo->IsKindOf(SID("Turret")))
		{
			((Turret*)pEntityGo)->SetTurretIgnoreRange();
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-turret", DcNpcSetTurret)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	NdGameObject* pEntityGo = args.NextGameObject();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-set-turret");
		if (pEntityGo && pEntityGo->IsKindOf(SID("Turret")))
		{
			pNpc->SetTurret((Turret*)pEntityGo);
			((Turret*)pEntityGo)->SetTurretUser(pNpc);
		}
		else
		{
			pNpc->SetTurret(nullptr);
			pNpc->GetAnimationControllers()->GetTurretController()->Reset();
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
 *	(define-c-function npc-get-awareness ((npc-name symbol) (target-name symbol))
 *		ai-entity-awareness)
 */
SCRIPT_FUNC("npc-get-awareness", DcNpcGetAwareness)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	NdGameObject* pEntityGameObject = args.NextGameObject();

	if (pNpc && pEntityGameObject)
	{
		if (const AiKnowledge* pKnow = pNpc->GetKnowledge())
		{
			const AiEntity* pEntity = pKnow->GetEntity(pEntityGameObject);

			if (pEntity && pEntity->IsKnown())
			{
				return ScriptValue(pEntity->GetAwareness());
			}
		}
	}

	return ScriptValue(DC::kAiEntityAwarenessNone);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
 *	(define-c-function npc-get-time-in-awareness-of ((npc-name symbol) (target-name symbol))
 *		float)
 */
SCRIPT_FUNC("npc-get-time-in-awareness-of", DcNpcGetTimeInAwarenessOf)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	NdGameObject* pEntityGameObject = args.NextGameObject();

	if (pNpc && pEntityGameObject)
	{
		if (const AiKnowledge* pKnow = pNpc->GetKnowledge())
		{
			const AiEntity* pEntity = pKnow->GetEntity(pEntityGameObject);
			if (pEntity && pEntity->IsKnown())
			{
				return ScriptValue(ToSeconds(pEntity->GetTimeInAwareness()));
			}
		}
	}

	return ScriptValue(-1.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-make-aware-of", DcNpcMakeAwareOf)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	Npc* pNpc = args.NextNpc();
	NdGameObject* pEntityGo = args.NextGameObject();
	const DC::AiEntityAwareness awareness	= args.NextI32();
	const DC::NpcAwarenessOptions* pOptions = args.NextPointer<DC::NpcAwarenessOptions>();

	if (pNpc && pEntityGo)
	{
		Npc::MakeAwareOfParams params;
		params.m_bIgnoreDisabledPerception = true;

		AiLogScript(pNpc, "npc-make-aware-of %s", pEntityGo->GetName());
		pNpc->MakeAwareOf(pEntityGo, awareness, FILE_LINE_FUNC, pOptions, &params);
		return ScriptValue(true);
	}

	AiLogScript(pNpc, "npc-make-aware-of (failed to find object)");

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-make-aware-of-at", DcNpcMakeAwareOfAt)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 4);

	Npc* pNpc = args.NextNpc();
	NdGameObject* pEntityGo = args.NextGameObject();
	const Point posWs		= args.NextPoint();
	const DC::AiEntityAwareness awareness	= args.NextI32();
	const DC::NpcAwarenessOptions* pOptions = args.NextPointer<DC::NpcAwarenessOptions>();
	const bool updateVerified = args.NextBoolean();

	// TODO for now, we still need the target game object, because otherwise group entities implode

	if (pNpc && pEntityGo)
	{
		Npc::MakeAwareOfParams params;
		params.m_bIgnoreDisabledPerception = true;

		params.m_pPosWs			= &posWs;
		params.m_pVerifiedPosWs = updateVerified ? &posWs : nullptr;

		AiLogScript(pNpc, "npc-make-aware-of-at %s", pEntityGo->GetName());
		pNpc->MakeAwareOf(pEntityGo, awareness, FILE_LINE_FUNC, pOptions, &params);
		return ScriptValue(true);
	}

	AiLogScript(pNpc, "npc-make-aware-of-at (failed to find object)");

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-ping-distraction", DcNpcPingDistraction)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(0);

	const NdGameObject* pGo = args.NextGameObject();

	const bool seen = args.NextBoolean();

	AiKnowledge* pKnow = pNpc->GetKnowledge();

	AiStimulusInferred stimulus(FILE_LINE_FUNC);
	stimulus.m_hReportingObject	  = pNpc;
	stimulus.m_data.m_hGameObject = pGo;
	stimulus.m_pretendSeen		  = seen;
	stimulus.m_pretendHeard		  = !seen;
	stimulus.m_data.m_knownNavFrame.Set(pGo->GetTranslation());
	stimulus.m_data.m_positionUpdateTime = pNpc->GetCurTime();
	stimulus.m_awareness = DC::kAiEntityAwarenessPotential;

	pKnow->ApplyStimulus(stimulus);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-ping-distraction-at", DcNpcPingDistractionAt)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(0);

	const Point posWs = args.NextPoint();
	const bool seen	  = args.NextBoolean();

	AiKnowledge* pKnow = pNpc->GetKnowledge();

	AiStimulusInferred stimulus(FILE_LINE_FUNC);
	stimulus.m_hReportingObject = pNpc;
	stimulus.m_pretendSeen		= seen;
	stimulus.m_pretendHeard		= !seen;
	stimulus.m_data.m_knownNavFrame.Set(posWs);
	stimulus.m_data.m_positionUpdateTime = pNpc->GetCurTime();
	stimulus.m_awareness = DC::kAiEntityAwarenessPotential;

	pKnow->ApplyStimulus(stimulus);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-acquisition", DcNpcGetAcquisition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	const Npc* pNpc			= args.NextNpc();
	const NdGameObject* pGo = args.NextGameObject();

	if (!pNpc)
		return ScriptValue(-1.f);

	if (!pGo)
		return ScriptValue(-1.f);

	const AiKnowledge* pKnow = pNpc->GetKnowledge();
	if (!pKnow)
		return ScriptValue(-1.f);

	NdGameObjectHandle hGo = pGo;
	const F32 acquisition  = pKnow->GetAcquisition(hGo);
	return ScriptValue(acquisition);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-recognition", DcNpcGetRecognition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	const Npc* pNpc			= args.NextNpc();
	const NdGameObject* pGo = args.NextGameObject();

	if (!pNpc)
		return ScriptValue(-1.f);

	if (!pGo)
		return ScriptValue(-1.f);

	const AiKnowledge* pKnow = pNpc->GetKnowledge();
	if (!pKnow)
		return ScriptValue(-1.f);

	NdGameObjectHandle hGo = pGo;
	const F32 acquisition  = pKnow->GetRecognition(hGo);
	return ScriptValue(acquisition);
}

//(define-c-function npc-get-instantaneous-noticeability
//	(
//		(npc-name		symbol)
//	)
//	boolean
//)
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-instantaneous-noticeability", DcNpcGetInstantaneousNoticeability)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (const Npc* const pNpc = args.NextNpc())
	{
		return ScriptValue(pNpc->GetInstantNoticeabilityScore() >= Npc::kNoticeabilityThresh);
	}

	return ScriptValue(false);
}

//(define-c-function npc-get-sprung-noticeability
//	(
//		(npc-name		symbol)
//	)
//	boolean
//)
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-sprung-noticeability", DcNpcGetSprungNoticeability)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (const Npc* const pNpc = args.NextNpc())
	{
		return ScriptValue(pNpc->GetSprungNoticeabilityScore() >= Npc::kNoticeabilityThresh);
	}

	return ScriptValue(false);
}

//(define-c-function npc-get-time-since-player-last-noticed-me
//	(
//		(npc-name		symbol)
//	)
//	float
//)
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-time-since-player-last-noticed-me", DcNpcGetTimeSincePlayerLastNoticedMe)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (const Npc* const pNpc = args.NextNpc())
	{
		const TimeFrame lastNoticedMeTime = pNpc->GetPlayerLastNoticedMeTime();
		if (lastNoticedMeTime != TimeFrameNegInfinity())
		{
			return ScriptValue(ToSeconds(pNpc->GetTimeSince(lastNoticedMeTime)));
		}
	}

	return ScriptValue(-1.0f);
}

//;; seer must be either player or buddy
//;; target must be an enemy of the player
//(define-c-function get-time-since-predictive-peek-los
//	(
//		(character-seer symbol)
//		(character-target symbol)
//	)
//	float
//)
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-time-since-predictive-peek-los", DcNpcGetTimeSincePredictivePeekLos)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	const Character* const pSeer = args.NextCharacter();
	const Character* const pTarget = args.NextCharacter();

	if (pSeer && pTarget)
	{
		const TimeFrame lastPeekLosTime = EngineComponents::GetNpcManager()->GetBuddyRayCaster().GetLastPeekVisTime(pSeer, pTarget);
		if (lastPeekLosTime != TimeFrameNegInfinity())
		{
			return ScriptValue(ToSeconds(EngineComponents::GetFrameState()->GetClock(kGameClock)->GetTimePassed(lastPeekLosTime)));
		}
	}

	return ScriptValue(-1.0f);
}

//(define-c-function npc-get-where-i-was-when-player-last-noticed-me
//	(
//		(npc-name		symbol)
//	)
//	point
//)
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-where-i-was-when-player-last-noticed-me", DcNpcGetWhereIWasWhenPlayerLastNoticedMe)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Point posWs = kInvalidPoint;

	if (const Npc* const pNpc = args.NextNpc())
	{
		posWs = pNpc->GetWhereIWasWhenPlayerLastNoticedMe();
	}

	Point* pPos = NDI_NEW(kAllocSingleGameFrame, kAlign16) Point(posWs);
	if (args.AllocSucceeded(pPos))
		return ScriptValue(pPos);

	return ScriptValue(nullptr);
}


/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-override-acquisition", DcNpcSetOverrideAcquisition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);
	Npc* pNpc = args.NextNpc();
	const NdGameObject* pGo = args.NextGameObject();
	const F32 acquisition	= args.NextFloat();

	if (!pNpc)
		return ScriptValue(false);

	if (!pGo)
		return ScriptValue(false);

	AiKnowledge* pKnow = pNpc->GetKnowledge();
	if (!pKnow)
		return ScriptValue(false);

	NdGameObjectHandle hGo = pGo;
	pKnow->SetOverrideAcquisition(hGo, acquisition, false);
	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-clear-override-acquisition", DcNpcClearOverrideAcquisition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	const NdGameObject* pGo = args.NextGameObject();

	if (!pNpc)
		return ScriptValue(false);

	if (!pGo)
		return ScriptValue(false);

	AiKnowledge* pKnow = pNpc->GetKnowledge();
	if (!pKnow)
		return ScriptValue(false);

	NdGameObjectHandle hGo = pGo;
	pKnow->ClearOverrideAcquisition(hGo);
	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-override-recognition", DcNpcSetOverrideRecognition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);
	Npc* pNpc = args.NextNpc();
	const NdGameObject* pGo = args.NextGameObject();
	const F32 recognition	= args.NextFloat();

	if (!pNpc)
		return ScriptValue(false);

	if (!pGo)
		return ScriptValue(false);

	AiKnowledge* pKnow = pNpc->GetKnowledge();
	if (!pKnow)
		return ScriptValue(false);

	NdGameObjectHandle hGo = pGo;
	pKnow->SetOverrideRecognition(hGo, recognition);
	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-clear-override-recognition", DcNpcClearOverrideRecognition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	const NdGameObject* pGo = args.NextGameObject();

	if (!pNpc)
		return ScriptValue(false);

	if (!pGo)
		return ScriptValue(false);

	AiKnowledge* pKnow = pNpc->GetKnowledge();
	if (!pKnow)
		return ScriptValue(false);

	NdGameObjectHandle hGo = pGo;
	pKnow->ClearOverrideRecognition(hGo);
	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-max-acquisition", DcNpcSetMaxAcquisition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	const F32 acquisition = args.NextFloat();

	if (!pNpc)
		return ScriptValue(false);

	AiKnowledge* pKnow = pNpc->GetKnowledge();
	if (!pKnow)
		return ScriptValue(false);

	pKnow->SetMaxAcquisition(acquisition);
	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-clear-max-acquisition", DcNpcClearMaxAcquisition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue(false);

	AiKnowledge* pKnow = pNpc->GetKnowledge();
	if (!pKnow)
		return ScriptValue(false);

	pKnow->ClearMaxAcquisition();
	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-max-recognition", DcNpcSetMaxRecognition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	const F32 recognition = args.NextFloat();

	if (!pNpc)
		return ScriptValue(false);

	AiKnowledge* pKnow = pNpc->GetKnowledge();
	if (!pKnow)
		return ScriptValue(false);

	pKnow->SetMaxRecognition(recognition);
	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-clear-max-recognition", DcNpcClearMaxRecognition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue(false);

	AiKnowledge* pKnow = pNpc->GetKnowledge();
	if (!pKnow)
		return ScriptValue(false);

	pKnow->ClearMaxRecognition();
	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-can-play-pursue-dialog", DcNpcCanPlayPursueDialog)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue(false);

	const FactionId factionId = pNpc->GetFactionId();

	const AiEncounterCoordinator* pCoord = pNpc->GetEncounterCoordinator();
	if (!pCoord)
		return ScriptValue(false);

	const bool result = pCoord->GetThreatDetection().CanPlayPursueDialog(pNpc);

	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-acquisition-sense", DcNpcGetAcquisitionSense)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	const Npc* pNpc			= args.NextNpc();
	const NdGameObject* pGo = args.NextGameObject();
	DC::AiPerceptionSense sense = DC::kAiPerceptionSenseNone;

	if (!pNpc)
		return ScriptValue(sense);

	if (!pGo)
		return ScriptValue(sense);

	const AiKnowledge* pKnow = pNpc->GetKnowledge();
	if (!pKnow)
		return ScriptValue(sense);

	NdGameObjectHandle hGo = pGo;
	const F32 acquisition  = pKnow->GetAcquisition(hGo, &sense);

	return ScriptValue(sense);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-acquisition-no-smell", DcNpcGetAcquisitionNoSmell)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	const Npc* pNpc			= args.NextNpc();
	const NdGameObject* pGo = args.NextGameObject();

	if (!pNpc)
		return ScriptValue(0.f);

	if (!pGo)
		return ScriptValue(0.f);

	return ScriptValue(pNpc->GetAcquisition(pGo, DC::kAiPerceptionSenseSmell));
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-threat-level-for-entity", DcNpcGetThreatLevelForEntity)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const NdGameObject* pGo = args.NextGameObject();

	if (!pGo)
		return ScriptValue(0.f);

	NdGameObjectHandle hGo = pGo;

	AtomicLockJanitorRead_Jls npcGroupLock(EngineComponents::GetNpcManager()->GetNpcGroupLock(), FILE_LINE_FUNC);

	F32 maxThreat = 0.0f;

	for (NpcHandle hNpc : EngineComponents::GetNpcManager()->GetNpcs())
	{
		const Npc* pNpc = hNpc.ToProcess();
		if (pNpc)
		{
			const F32 threat = pNpc->GetThreatLevel(hGo);
			if (threat > maxThreat)
			{
				maxThreat = threat;
			}
		}
	}

	return ScriptValue(maxThreat);
}

SCRIPT_FUNC("get-npcs-with-highest-threat", DcGetNpcsWithHighestThreat)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const U32 maxNPCsToReturn = args.NextU32();
	const bool includeSmell = args.NextBoolean();
	if ( maxNPCsToReturn > 0 )
	{
		struct NpcThreatSortData
		{
			NpcHandle	m_hNpc;
			float		m_currentThreat;

			static int Compare(NpcThreatSortData& a, NpcThreatSortData& b)
			{
				return (I32)Signum(b.m_currentThreat - a.m_currentThreat); // sort from largest to smallest threat
			}
		};

		ListArray<NpcThreatSortData> sortedArray(EngineComponents::GetNpcManager()->GetNpcCount());

		// remember, keep the lock [on the npc array] as short as possible
		// (GetDArrayAllEnemyNpcs() calls GetNpcGroupLock() internally)
		{
			const PlayerHandle hPlayer = GetPlayerHandle();
			const DC::AiPerceptionSense exclusionSense = includeSmell
				? DC::kAiPerceptionSenseNone
				: DC::kAiPerceptionSenseSmell;
			const FactionId playerFaction(GetPlayerFaction());

			AtomicLockJanitorRead_Jls npcGroupLock(EngineComponents::GetNpcManager()->GetNpcGroupLock(), FILE_LINE_FUNC);
			for (NpcHandle hNpc : EngineComponents::GetNpcManager()->GetNpcs())
			{
				if (const Npc* pNpc = hNpc.ToProcess())
				{
					if (pNpc->IsDead())
					{
						continue;
					}
					if (!IsEnemy(pNpc->GetFactionId(), playerFaction))
					{
						continue;
					}
					sortedArray.push_back({ hNpc, pNpc->GetThreatLevel(hPlayer, exclusionSense) });
				}
			}
		}
		QuickSort(sortedArray.ArrayPointer(), sortedArray.Size(), NpcThreatSortData::Compare);

		OrphanedDArray* pResultArray = OrphanedDArray::Create(maxNPCsToReturn);
		if (pResultArray != nullptr)
		{
			for (const NpcThreatSortData& sortData : sortedArray)
			{
				if (pResultArray->GetElemCount() < maxNPCsToReturn)
				{
					pResultArray->AppendElem(sortData.m_hNpc.GetScriptId());
				}
				else
				{
					break;
				}
			}
			return ScriptValue(pResultArray->GetUserId());
		}
	}
	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-num-npcs-in-range-of-point", DcNpcGetNumNpcsInRangeOfPoint)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	const Point worldPoint = args.NextPoint();
	const F32 dist		   = args.NextFloat();

	I32 count = 0;

	AtomicLockJanitorRead_Jls npcGroupLock(EngineComponents::GetNpcManager()->GetNpcGroupLock(), FILE_LINE_FUNC);

	for (NpcHandle hNpc : EngineComponents::GetNpcManager()->GetNpcs())
	{
		const Npc* pNpc = hNpc.ToProcess();

		if (pNpc)
		{
			const float npcDist = Length(pNpc->GetTranslation() - worldPoint);

			if (npcDist <= dist)
			{
				count++;
			}
		}
	}

	return ScriptValue(count);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-faction-id", DcNpcGetFactionId)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	const Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		return ScriptValue(static_cast<U32>(pNpc->GetFactionId().GetRawFactionIndex()));
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-start-pursue", DcNpcStartPursue)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const FactionId factionId(static_cast<U32>(args.NextU32()));
	const BoundFrame* pBoundFrame = args.NextBoundFrame();

	if (pBoundFrame)
	{
		AiCoordinatorManager::Iterator iter = AiCoordinatorManager::Get().GetIterator();

		const Point posWs = pBoundFrame->GetTranslation();
		DC::NpcAwarenessOptions options = Npc::GetDefaultAwarenessOptions();

		options.m_blockSurprise = true;
		options.m_weaponType	= DC::kAiWeaponTypeFirearm;

		Npc::MakeAwareOfParams params;
		params.m_pPosWs = &posWs;

		for (U32 coordNum = iter.First(); coordNum < iter.End(); coordNum = iter.Advance())
		{
			AiEncounterCoordinator* pCoord = AiCoordinatorManager::Get().GetCoordinatorAs<AiEncounterCoordinator>(coordNum);
			if (pCoord && pCoord->GetFactionId() == factionId)
			{
				AiCharacterGroupIterator it	   = pCoord->GetAiCharacterGroup().BeginMembers();
				AiCharacterGroupIterator itEnd = pCoord->GetAiCharacterGroup().EndMembers();
				for (; it != itEnd; ++it)
				{
					Npc* pNpc = it.GetMemberAs<Npc>();

					if (!pNpc)
						continue;

					pNpc->MakeAwareOf(GetPlayer(), DC::kAiEntityAwarenessMissing, FILE_LINE_FUNC, &options, &params);
				}
			}
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-start-search", DcNpcStartSearch)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const FactionId factionId(static_cast<U32>(args.NextU32()));

	AiCoordinatorManager::Iterator iter = AiCoordinatorManager::Get().GetIterator();

	for (U32 i = iter.First(); i < iter.End(); i = iter.Advance())
	{
		const AiEncounterCoordinator* pCoord = AiCoordinatorManager::Get().GetCoordinatorAs<AiEncounterCoordinator>(i);
		if (pCoord && pCoord->GetFactionId() == factionId)
		{
			const AiCharacterGroup& group  = pCoord->GetAiCharacterGroup();
			AiCharacterGroupIterator it	   = group.BeginMembers();
			AiCharacterGroupIterator itEnd = group.EndMembers();

			for (; it != itEnd; ++it)
			{
				Npc* pNpc = it.GetMemberAs<Npc>();

				if (!pNpc)
					continue;

				pNpc->MakeAwareOf(GetPlayer(), DC::kAiEntityAwarenessLost, FILE_LINE_FUNC);
			}
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-stunned?", DcNpcIsStunnedP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	return ScriptValue(pNpc && pNpc->GetAnimationControllers()->GetHitController()->IsStunReactionPlaying());
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-time-since-stunned", DcNpcGetTimeSinceStunned)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue(0);

	const TimeFrame lastTimeStunned = pNpc->GetAnimationControllers()->GetHitController()->GetLastStunPlayingTime();
	if (lastTimeStunned == TimeFrameNegInfinity())
		return ScriptValue(lastTimeStunned.ToSeconds());

	const TimeFrame timeSinceStunned = pNpc->GetTimeSince(lastTimeStunned);
	return ScriptValue(timeSinceStunned.ToSeconds());
}

/*
(define-c-function npc-buddy-should-perform-quiet-stealth-kill? ((buddy symbol))
	boolean)
*/
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-buddy-should-perform-quiet-stealth-kill?", DcNpcBuddyShouldPerformQuietStealthKillP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* const pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(false);

	const BuddyCombatLogic* const pLogic = pNpc->GetBuddyCombatLogic();
	if (!pLogic)
		return ScriptValue(false);

	if (!pLogic->GetStealthKillVictim().HandleValid())
		return ScriptValue(false);

	AtomicLockJanitorRead_Jls npcGroupLock(EngineComponents::GetNpcManager()->GetNpcGroupLock(), FILE_LINE_FUNC);

	PlayerHandle hPlayer = GetPlayerHandle();

	for (NpcHandle hOtherNpc : EngineComponents::GetNpcManager()->GetNpcs())
	{
		const Npc* pOtherNpc = hOtherNpc.ToProcess();
		NAV_ASSERT(pOtherNpc);
		if (!pOtherNpc)
			continue;

		if (!IsEnemy(GetPlayerFaction(), pOtherNpc->GetFactionId()))
			continue;

		if (pOtherNpc->IsDead())
			continue;

		if (pOtherNpc->IsGrappled())
			continue;

		if (pOtherNpc->IsDoomed())
			continue;

		if (!pOtherNpc->CanBroadcastCommunication())
			continue;

		const AiEntity* pPlayerEntity = pOtherNpc->GetKnowledge()->GetEntity(hPlayer);
		if (!pPlayerEntity)
			continue;

		if (!pPlayerEntity->IsCertain())
			continue;

		return ScriptValue(false);
	}

	return ScriptValue(true);
}

/*
(define-c-function npc-buddy-get-time-since-ambient-started ((npc-name symbol))
	float)
*/
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-buddy-get-time-since-ambient-started", DcNpcBuddyGetTimeSinceAmbientStarted)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		if (const BuddyCombatLogic* const pLogic = pNpc->GetBuddyCombatLogic())
		{
			if (pLogic->IsAmbient())
			{
				const TimeFrame timeSinceAmbientStarted = pLogic->GetTimeSinceAmbientStarted(pNpc);
				return ScriptValue(ToSeconds(timeSinceAmbientStarted));
			}
		}
	}

	return ScriptValue(-1.0f);
}

/*
(define-c-function npc-buddy-get-time-since-last-ambient ((npc-name symbol))
	float)
*/
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-buddy-get-time-since-last-ambient", DcNpcBuddyGetTimeSinceLastAmbient)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		if (const BuddyCombatLogic* const pLogic = pNpc->GetBuddyCombatLogic())
		{
			const TimeFrame timeSinceLastAmbient = pLogic->GetTimeSinceLastAmbient(pNpc);
			return ScriptValue(ToSeconds(timeSinceLastAmbient));
		}
	}

	return ScriptValue(-1.0f);
}

/*
(define-c-function npc-buddy-get-time-since-stealth-started ((npc-name symbol))
	float)
*/
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-buddy-get-time-since-stealth-started", DcNpcBuddyGetTimeSinceStealthStarted)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		if (const BuddyCombatLogic* const pLogic = pNpc->GetBuddyCombatLogic())
		{
			if (pLogic->IsStealth())
			{
				const TimeFrame timeSinceStealthStarted = pLogic->GetTimeSinceStealthStarted(pNpc);
				return ScriptValue(ToSeconds(timeSinceStealthStarted));
			}
		}
	}

	return ScriptValue(-1.0f);
}

/*
(define-c-function npc-buddy-get-time-since-last-stealth ((npc-name symbol))
	float)
*/
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-buddy-get-time-since-last-stealth", DcNpcBuddyGetTimeSinceLastStealth)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		if (const BuddyCombatLogic* const pLogic = pNpc->GetBuddyCombatLogic())
		{
			const TimeFrame timeSinceLastStealth = pLogic->GetTimeSinceLastStealth(pNpc);
			return ScriptValue(ToSeconds(timeSinceLastStealth));
		}
	}

	return ScriptValue(-1.0f);
}

/*
(define-c-function npc-buddy-get-time-since-combat-started ((npc-name symbol))
	float)
*/
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-buddy-get-time-since-combat-started", DcNpcBuddyGetTimeSinceCombatStarted)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		if (const BuddyCombatLogic* const pLogic = pNpc->GetBuddyCombatLogic())
		{
			if (pLogic->IsCombat())
			{
				const TimeFrame timeSinceCombatStarted = pLogic->GetTimeSinceCombatStarted(pNpc);
				return ScriptValue(ToSeconds(timeSinceCombatStarted));
			}
		}
	}

	return ScriptValue(-1.0f);
}

/*
(define-c-function npc-buddy-get-time-since-last-combat ((npc-name symbol))
	float)
*/
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-buddy-get-time-since-last-combat", DcNpcBuddyGetTimeSinceLastCombat)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		if (const BuddyCombatLogic* const pLogic = pNpc->GetBuddyCombatLogic())
		{
			const TimeFrame timeSinceLastCombat = pLogic->GetTimeSinceLastCombat(pNpc);
			return ScriptValue(ToSeconds(timeSinceLastCombat));
		}
	}

	return ScriptValue(-1.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-buddy-get-time-since-head-last-underwater", DcNpcBuddyGetTimeSinceHeadLastUnderwater)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Buddy* pBuddy = Buddy::FromProcess(args.NextNpc());

	if (pBuddy)
	{
		const TimeFrame timeSinceHeadLastUnderwater = pBuddy->GetTimeSinceHeadLastUnderwater();
		return ScriptValue(ToSeconds(timeSinceHeadLastUnderwater));
	}

	return ScriptValue(-1.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-lead-get-seconds-spent-waiting-at-current-node", DcNpcLeadGetSecondsSpentWaitingAtCurrentNode)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		const TimeFrame startTime = pNpc->GetLeadWaitStartTime();
		if (startTime != TimeFrameNegInfinity())
		{
			return ScriptValue(ToSeconds(pNpc->GetCurTime() - startTime));
		}
	}

	return ScriptValue(-1.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-stunned-knee?", DcNpcIsStunnedKneeP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	return ScriptValue(pNpc && pNpc->GetAnimationControllers()->GetHitController()->IsStunKneeReactionPlaying());
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-stunned-by-smoke-or-stun-bomb?", DcNpcIsStunnedBySmokeOrStunBombP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	return ScriptValue(pNpc
					   && pNpc->GetAnimationControllers()->GetHitController()->IsStunReactionFromSmokeOrStunBombPlaying());
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("is-npc-in-checkpoint?", DcIsNpcInCheckpointP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	StringId64 npcName = args.NextStringId();

	if (GameSave::IsCharacterDeadForCurrentCheckpoint(npcName))
		return ScriptValue(true);

	BoundFrame frame;
	if (GameSave::GetCharacterBoundFrameForCurrentCheckpoint(npcName, &frame))
	{
		return ScriptValue(true);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("set-npc-loc-in-checkpoint", DcSetNpcLocInCheckpoint)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	StringId64 npcName = args.NextStringId();
	const BoundFrame* pBoundFrame = args.NextBoundFrame();

	if (pBoundFrame)
	{
		GameSave::SetCharacterBoundFrameForCurrentCheckpoint(npcName, *pBoundFrame);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-npc-loc-in-checkpoint", DcGetNpcLocInCheckpoint)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	StringId64 npcName = args.NextStringId();

	BoundFrame frame;
	if (GameSave::GetCharacterBoundFrameForCurrentCheckpoint(npcName, &frame))
	{
		BoundFrame* pBoundFrame = NDI_NEW(kAllocSingleGameFrame, kAlign16) BoundFrame(frame);

		return ScriptValue(pBoundFrame);
	}

	return ScriptValue(nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
 *	(define-c-function npc-can-see? ...)
 */
SCRIPT_FUNC("npc-can-see?", DcNpcCanSeeP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc		  = args.NextNpc();
	NdGameObject* pGo = args.NextGameObject();

	if (pNpc && pGo)
	{
		AiKnowledge* pKnow = pNpc->GetKnowledge();
		if (pKnow)
		{
			const AiEntity* pEntity = pKnow->GetEntity(pGo);
			if (pEntity)
			{
				bool canSee = (pEntity
								   ->IsObjectVisible(kAiVisibilityFull)); // PV2 TO DO support a visibility argument to this func
				return ScriptValue(canSee);
			}
		}
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
 *	(define-c-function npc-can-see-target? ...)
 */
SCRIPT_FUNC("npc-can-see-target?", DcNpcCanSeeTargetP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue(false);

	const AiKnowledge* pKnow = pNpc->GetKnowledge();
	if (!pKnow)
		return ScriptValue(false);

	const AiEntity* pEntity = pKnow->GetTargetEntity();
	if (!pEntity)
		return ScriptValue(false);

	const bool canSee = pEntity->IsObjectVisible(kAiVisibilityFull);

	return ScriptValue(canSee);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-demeanor-matrix", DcNpcSetDemeanorMatrix)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const StringId64 demeanorMatrixId = args.NextStringId();

	if (pNpc)
	{
		pNpc->GetArchetype().SetDemeanorMatrixId(demeanorMatrixId);
		pNpc->ConfigureDemeanor(false);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-demeanor", DcNpcSetDemeanor)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const DC::NpcDemeanor dcDemeanor = args.NextI32();
	const bool force = args.NextBoolean();

	const Demeanor newDemeanor = DemeanorFromDcDemeanor(dcDemeanor);

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-set-demeanor %s%s", force ? "FORCE " : "", pNpc->GetDemeanorName(newDemeanor));

		if (force)
			pNpc->ForceDemeanor(newDemeanor, AI_LOG);
		else
			pNpc->RequestDemeanor(newDemeanor, AI_LOG);

		pNpc->SetStartingDemeanor(newDemeanor);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("wait-npc-set-demeanor", DcWaitNpcSetDemeanor)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const DC::NpcDemeanor dcDemeanor = args.NextI32();

	const Demeanor newDemeanor = DemeanorFromDcDemeanor(dcDemeanor);

	if (pNpc)
	{
		AiLogScript(pNpc, "wait-npc-set-demeanor %s", pNpc->GetDemeanorName(newDemeanor));

		pNpc->RequestDemeanor(newDemeanor, AI_LOG);
		pNpc->SetStartingDemeanor(newDemeanor);

		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

		pNpc->ScriptCommandWaitForDemeanor(pGroupInst, pTrackInst ? pTrackInst->GetTrackIndex() : 0);

		// wait until the command is done
		if (pGroupInst && pTrackInst)
		{
			pGroupInst->WaitForTrackDone("wait-npc-set-demeanor");
			ScriptContinuation::Suspend(args.GetArgArray());
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("wait-npc-demeanor-up-to-date", DcWaitNpcDemeanorUpToDate)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		AiLogScript(pNpc, "wait-npc-demeanor-up-to-date");

		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

		pNpc->ScriptCommandWaitForDemeanor(pGroupInst, pTrackInst ? pTrackInst->GetTrackIndex() : 0);

		// wait until the command is done
		if (pGroupInst && pTrackInst)
		{
			pGroupInst->WaitForTrackDone("wait-npc-demeanor-up-to-date");
			ScriptContinuation::Suspend(args.GetArgArray());
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-demeanor", DcNpcGetDemeanor)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		const DC::NpcDemeanor dcDemeanor = DcDemeanorFromDemeanor(pNpc->GetCurrentDemeanor());
		return ScriptValue(dcDemeanor);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-teleport", DcNpcTeleport)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const EntitySpawner* pSpawner = args.NextSpawner();

	if (pNpc && pSpawner)
	{
		Binding binding = pSpawner->GetBinding();

		if (!pSpawner->HasParentSpawned())
		{
			args.MsgScriptError("Spawner %s's parent not spawned yet!\n", pSpawner->Name());
		}
		else if (binding.GetRigidBody() == nullptr && pSpawner->GetParentSpawner() != nullptr)
		{
			args.MsgScriptError("%s's parent %s has no collision!\n",
								pSpawner->Name(),
								pSpawner->GetParentSpawner()->Name());
		}

		BoundFrame* pBoundFrame = NDI_NEW(kAllocSingleGameFrame, kAlign16)
			BoundFrame(pSpawner->GetWorldSpaceLocator(), binding);

		if (args.AllocSucceeded(pBoundFrame))
		{
			AiLogScript(pNpc, "npc-teleport");
			pNpc->TeleportToBoundFrame(*pBoundFrame);
		}
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-teleport-to", DcNpcTeleportTo)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const BoundFrame* pBoundFrame = args.NextBoundFrame();

	if (pNpc && pBoundFrame)
	{
		AiLogScript(pNpc, "npc-teleport-to");
		pNpc->TeleportToBoundFrame(*pBoundFrame);
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
class MoveToParams
{
public:
	MoveToParams()
		: m_pBoundFrame(nullptr)
		, m_pPlaneOrientBoundFrame(nullptr)
		, m_dcMotionType(DC::kNpcMotionTypeRun)
		, m_mtSubcategory(INVALID_STRING_ID_64)
		, m_dcGoalReachedType(DC::kNpcGoalReachedTypeStop)
		, m_motionTypeSettings(INVALID_STRING_ID_64)
		, m_hFollower(nullptr)
		, m_wait(false)
		, m_bGoalOrient(false)
		, m_tryToShoot(false)
		, m_searchMove(false)
		, m_splineNameId(INVALID_STRING_ID_64)
		, m_splineStartPosWs(kOrigin)
		, m_splineGoalPosWs(kOrigin)
		, m_strafing(false)
		, m_performanceId(INVALID_STRING_ID_64)
	{
	}

	const BoundFrame* m_pBoundFrame;
	const BoundFrame* m_pPlaneOrientBoundFrame;
	DC::NpcMotionType m_dcMotionType;
	StringId64 m_mtSubcategory;
	DC::NpcGoalReachedType m_dcGoalReachedType;
	StringId64 m_motionTypeSettings;
	CharacterHandle m_hFollower;
	StringId64 m_performanceId;

	StringId64 m_splineNameId;
	Point m_splineStartPosWs;
	Point m_splineGoalPosWs;

	bool m_wait;
	bool m_bGoalOrient;
	bool m_tryToShoot;
	bool m_searchMove;
	bool m_strafing;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue DcNpcMoveToInternal(ScriptArgIterator& args, Npc* pNpc, const MoveToParams& params)
{
	if (!pNpc || !params.m_pBoundFrame)
	{
		return ScriptValue(1);
	}

	if (!params.m_pBoundFrame || !params.m_pBoundFrame->GetBinding().IsSignatureValid())
	{
		args.MsgScriptError("Npc '%s' told to move to invalid bound frame\n", pNpc->GetName());
		return ScriptValue(0);
	}

	const MotionType motionType = DcMotionTypeToGame(params.m_dcMotionType);

	if (motionType >= kMotionTypeMax)
	{
		args.MsgScriptError("Npc '%s' told to move with an invalid motion type!\n", pNpc->GetName());
		return ScriptValue(0);
	}

	NavGoalReachedType goalReachedType = ConvertDcGoalReachedType(params.m_dcGoalReachedType);
	const bool respectGoalFacing	   = (params.m_dcGoalReachedType == DC::kNpcGoalReachedTypeStopAndFace);

	const Point posWs = params.m_pBoundFrame->GetTranslationWs();
	SsVerboseLog(1,
				 "npc-move-to (%g,%g,%g) [%s, %s]",
				 float(posWs.X()),
				 float(posWs.Y()),
				 float(posWs.Z()),
				 GetMotionTypeName(motionType),
				 GetGoalReachedTypeName(goalReachedType));

	SsInstance* pScriptInst = GetJobSsContext().GetScriptInstance();
	SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

	if (params.m_wait && !(pScriptInst && pGroupInst && pTrackInst))
	{
		args.MsgScriptError("Called outside script context -- will not wait\n");
	}

	NavLocation navLoc = pNpc->AsReachableNavLocationWs(posWs, NavLocation::Type::kNavPoly);

	if (!pNpc->ValidateMoveToLocation(navLoc))
	{
		args.MsgScriptError("Npc '%s' told to move off the nav mesh/map! (%g,%g,%g)\n",
							pNpc->GetName(),
							float(posWs.X()),
							float(posWs.Y()),
							float(posWs.Z()));
		return ScriptValue(0);
	}

	float goalRadius = pNpc->GetMotionConfig().m_ignoreMoveDistance;
	MotionType goalMotionType = motionType;

	NavMoveArgs moveArgs;
	moveArgs.m_goalRadius	   = goalRadius;
	moveArgs.m_motionType	   = motionType;
	moveArgs.m_goalReachedType = goalReachedType;
	moveArgs.m_strafeMode	   = params.m_strafing ? DC::kNpcStrafeAlways : DC::kNpcStrafeNever;
	moveArgs.m_mtSubcategory   = params.m_mtSubcategory;
	moveArgs.m_performanceId   = params.m_performanceId;

	if (params.m_motionTypeSettings != INVALID_STRING_ID_64)
	{
		pNpc->ScriptCommandMoveToPosDynamicSpeed(navLoc,
												 GetLocalZ(params.m_pBoundFrame->GetRotation()),
												 params.m_pPlaneOrientBoundFrame, // may be nullptr
												 params.m_bGoalOrient,
												 params.m_motionTypeSettings,
												 params.m_hFollower,
												 moveArgs,
												 respectGoalFacing,
												 params.m_tryToShoot,
												 pGroupInst,
												 pTrackInst ? pTrackInst->GetTrackIndex() : 0,
												 params.m_wait);
	}
	else
	{
		pNpc->ScriptCommandMoveToPos(navLoc,
									 GetLocalZ(params.m_pBoundFrame->GetRotation()),
									 moveArgs,
									 goalMotionType,
									 respectGoalFacing,
									 params.m_tryToShoot,
									 params.m_searchMove,
									 pGroupInst,
									 pTrackInst ? pTrackInst->GetTrackIndex() : 0,
									 params.m_wait);
	}

	// Wait until the move-to is done.
	if (params.m_wait && pScriptInst && pGroupInst && pTrackInst)
	{
		pGroupInst->WaitForTrackDone("npc-move-to");
		ScriptContinuation::Suspend(args.GetArgArray());
	}

	return ScriptValue(1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue DcNpcMoveToEntryInternal(ScriptArgIterator& args, bool bWait)
{
	// required
	Npc* pNpc = args.NextNpc();
	const StringId64 animName = args.NextStringId();

	const BoundFrame* apRefBoundFrame = args.NextBoundFrame();
	if (!apRefBoundFrame)
	{
		args.MsgScriptError("Null bound frame\n");
		return ScriptValue(0);
	}

	// non-required
	const DC::NpcMotionType dcMotionType	   = args.NextI32();
	DC::NpcEntryReachedType dcEntryReachedType = args.NextI32();
	const bool mirror = args.NextBoolean();
	const StringId64 mtSubcategory = args.NextStringId();
	const float startFrame		   = args.NextFloat();
	const StringId64 apRefChannel  = args.NextStringId();
	const StringId64 entrySettingsId = args.NextStringId();

	if (!pNpc || animName == INVALID_STRING_ID_64)
	{
		return ScriptValue(0);
	}

	const AnimControl* pAnimControl = pNpc->GetAnimControl();

	const ArtItemAnim* pEntryAnim = pAnimControl ? pAnimControl->LookupAnim(animName).ToArtItem() : nullptr;

	if (!pEntryAnim)
	{
		args.MsgScriptError("Unable to find entry anim '%s' for object '%s'\n",
							DevKitOnly_StringIdToString(animName),
							pNpc->GetName());

		return ScriptValue(0);
	}

	const float animDuration = GetDuration(pEntryAnim);
	const float startPhase	 = (animDuration > NDI_FLT_EPSILON) ? Limit01(startFrame / (30.0f * animDuration)) : 0.0f;
	const SkeletonId skelId	 = pNpc->GetSkeletonId();

	// get animation entry locator
	Locator entryLocWs;

	if (!FindAlignFromApReference(skelId,
								  pEntryAnim,
								  startPhase,
								  apRefBoundFrame->GetLocatorWs(),
								  apRefChannel,
								  &entryLocWs,
								  mirror))
	{
		args.MsgScriptError("Unable to find entry location from animation '%s' for object '%s'\n",
							DevKitOnly_StringIdToString(animName),
							pNpc->GetName());

		return ScriptValue(0);
	}

	const float minMoveDist = pNpc->GetMotionConfig().m_ignoreMoveDistance;

	const Locator myLocWs	 = pNpc->GetLocator();
	const float xzDistErr	 = DistXz(entryLocWs.Pos(), myLocWs.Pos());
	const float yDistErr	 = Abs(entryLocWs.Pos().Y() - myLocWs.Pos().Y());
	const float rotDot		 = Dot(entryLocWs.Rot(), myLocWs.Rot());
	const bool distSatisfied = (xzDistErr < minMoveDist) && (yDistErr < 0.5f);
	const bool rotSatisfied	 = rotDot > 0.9f;

	if (distSatisfied && rotSatisfied)
	{
		AiLogNav(pNpc, "Completing move to entry immediately because I'm already there\n");
		return ScriptValue(0);
	}

	BoundFrame entryBoundFrame(entryLocWs, apRefBoundFrame->GetBinding());
	const Point entryPosWs = entryBoundFrame.GetTranslationWs();

	const MotionType motionType = DcMotionTypeToGame(dcMotionType);

	if (motionType >= kMotionTypeMax)
	{
		args.MsgScriptError("Called with invalid motion type '%s' (%d)\n",
							DC::GetNpcMotionTypeName(dcMotionType),
							dcMotionType);
		return ScriptValue(0);
	}

	// get arriving motion type and goal reached type
	MotionType arrivingMotionType	   = motionType;
	NavGoalReachedType goalReachedType = kNavGoalReachedTypeStop;
	switch (dcEntryReachedType)
	{
	case DC::kNpcEntryReachedTypeWalk:
		{
			goalReachedType	   = kNavGoalReachedTypeContinue;
			arrivingMotionType = kMotionTypeWalk;
		}
		break;
	case DC::kNpcEntryReachedTypeRun:
		{
			goalReachedType	   = kNavGoalReachedTypeContinue;
			arrivingMotionType = kMotionTypeRun;
		}
		break;
	case DC::kNpcEntryReachedTypeSprint:
		{
			goalReachedType	   = kNavGoalReachedTypeContinue;
			arrivingMotionType = kMotionTypeSprint;
		}
		break;
	case DC::kNpcEntryReachedTypeStop:
		{
			goalReachedType	   = kNavGoalReachedTypeStop;
			arrivingMotionType = motionType;
		}
		break;

	default:
		args.MsgScriptError("Called with unknown entry reached type '%s' (%d)\n",
							DC::GetNpcEntryReachedTypeName(dcEntryReachedType),
							dcEntryReachedType);
		return ScriptValue(0);

	}

	const bool respectGoalFacing = (kNavGoalReachedTypeStop == goalReachedType);

	SsVerboseLog(1,
				 "npc-move-to-entry (%g,%g,%g) [%s, %s]",
				 float(entryPosWs.X()),
				 float(entryPosWs.Y()),
				 float(entryPosWs.Z()),
				 GetMotionTypeName(motionType),
				 GetGoalReachedTypeName(goalReachedType));

	SsInstance* pScriptInst = GetJobSsContext().GetScriptInstance();
	SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

	if (bWait && !(pScriptInst && pGroupInst && pTrackInst))
	{
		args.MsgScriptError("Called outside script context -- will not wait\n");
	}

	const NavLocation destNavLoc = pNpc->AsReachableNavLocationWs(entryPosWs, NavLocation::Type::kNavPoly);

	if (!pNpc->ValidateMoveToLocation(destNavLoc))
	{
#if !FINAL_BUILD
		args.MsgScriptError("Npc '%s' told to move off the nav mesh/map! (%g,%g,%g)\n",
							pNpc->GetName(),
							float(entryPosWs.X()),
							float(entryPosWs.Y()),
							float(entryPosWs.Z()));

		g_prim.Draw(DebugCross(entryPosWs, 0.3f, kColorRed, kPrimEnableHiddenLineAlpha), Seconds(5.0f));
		g_prim.Draw(DebugLine(entryPosWs, pNpc->GetTranslation(), kColorRed, 4.0f, kPrimEnableHiddenLineAlpha),
					Seconds(5.0f));
		g_prim.Draw(DebugLine(entryPosWs,
							  apRefBoundFrame->GetTranslationWs(),
							  kColorRed,
							  4.0f,
							  kPrimEnableHiddenLineAlpha),
					Seconds(5.0f));
		g_prim.Draw(DebugCoordAxesLabeled(apRefBoundFrame->GetLocatorWs(),
										  "apRef",
										  0.3f,
										  kPrimEnableHiddenLineAlpha,
										  4.0f),
					Seconds(5.0f));
#endif
		return ScriptValue(0);
	}

	float goalRadius = pNpc->GetMotionConfig().m_ignoreMoveDistance;

	NavMoveArgs moveArgs;
	moveArgs.m_goalRadius	 = goalRadius;
	moveArgs.m_motionType	 = motionType;
	moveArgs.m_mtSubcategory = mtSubcategory;
	moveArgs.m_goalReachedType	  = goalReachedType;
	moveArgs.m_destAnimId		  = animName;
	moveArgs.m_destAnimPhase	  = startPhase;

	if (!g_aiGameOptions.m_buddy.m_enableScriptDynamicStopping || !pNpc->IsBuddyNpc())
	{
		moveArgs.m_ignorePlayerOnGoal = true;
	}

	if (!respectGoalFacing)
	{
		pNpc->SetNaturalFacePosition();
	}

	if (entrySettingsId != INVALID_STRING_ID_64)
	{
		pNpc->ScriptCommandMoveToEntry(destNavLoc,
									   moveArgs,
									   arrivingMotionType,
									   respectGoalFacing,
									   entrySettingsId,
									   entryBoundFrame,
									   pGroupInst,
									   pTrackInst ? pTrackInst->GetTrackIndex() : 0,
									   bWait);
	}
	else
	{
		pNpc->ScriptCommandMoveToPos(destNavLoc,
									 GetLocalZ(entryBoundFrame.GetRotation()),
									 moveArgs,
									 arrivingMotionType,
									 respectGoalFacing,
									 false,
									 false,
									 pGroupInst,
									 pTrackInst ? pTrackInst->GetTrackIndex() : 0,
									 bWait);
	}

	// Wait until the move-to is done.
	if (bWait && pScriptInst && pGroupInst && pTrackInst)
	{
		pGroupInst->WaitForTrackDone("npc-move-to-entry");
		ScriptContinuation::Suspend(args.GetArgArray());
	}

	return ScriptValue(1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	;; Move to a fixed point using dynamic motion type settings.
	(define-c-function npc-move-to-dynamic-speed
		(
			(npc-name			symbol)
			(location			bound-frame)
			(goal-reached-type	npc-goal-reached-type (:default (npc-goal-reached-type stop)))
			(settings-name		symbol		(:default '*default-ai-motion-type-settings*))
			(follow-name		symbol		(:default 'player))			;; who is following me
			(try-to-shoot		boolean		(:default #f))
			(strafe				boolean		(:default #f))
		)
		none
)
*/
SCRIPT_FUNC("npc-move-to-dynamic-speed", DcNpcMoveToDynamicSpeed)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	MoveToParams params;

	Npc* pNpc = args.NextNpc();
	params.m_pBoundFrame		= args.NextBoundFrame();
	params.m_dcGoalReachedType	= args.NextI32();
	params.m_motionTypeSettings = args.NextStringId();
	params.m_hFollower	= args.NextCharacter();
	params.m_tryToShoot = args.NextBoolean();
	params.m_wait		= false;
	params.m_strafing	= args.NextBoolean();
	params.m_performanceId = args.NextStringId();

	if (!params.m_hFollower.HandleValid())
	{
		args.MsgScriptError("Called with invalid follower\n");
		return ScriptValue(0);
	}

	if (params.m_motionTypeSettings == INVALID_STRING_ID_64)
	{
		args.MsgScriptError("Called with invalid motion type settings\n");
		return ScriptValue(0);
	}

	AiLogScript(pNpc, "npc-move-to-dynamic-speed");
	return DcNpcMoveToInternal(args, pNpc, params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function wait-npc-move-to-dynamic-speed
	(
		(npc-name			symbol)
		(location			bound-frame)
		(goal-reached-type	npc-goal-reached-type (:default (npc-goal-reached-type stop)))
		(settings-name		symbol		(:default '*default-ai-motion-type-settings*))
		(follow-name		symbol		(:default 'player))			;; who is following me
		(try-to-shoot		boolean		(:default #f))
		(strafe				boolean		(:default #f))
	)
	none
)
*/
SCRIPT_FUNC("wait-npc-move-to-dynamic-speed", DcNpcWaitMoveToDynamicSpeed)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	MoveToParams params;

	Npc* pNpc = args.NextNpc();
	params.m_pBoundFrame		= args.NextBoundFrame();
	params.m_dcGoalReachedType	= args.NextI32();
	params.m_motionTypeSettings = args.NextStringId();
	params.m_hFollower	= args.NextCharacter();
	params.m_tryToShoot = args.NextBoolean();
	params.m_wait		= true;
	params.m_strafing	= args.NextBoolean();
	params.m_performanceId = args.NextStringId();

	if (!params.m_hFollower.HandleValid())
	{
		args.MsgScriptError("Called with invalid follower\n");
		return ScriptValue(0);
	}

	if (params.m_motionTypeSettings == INVALID_STRING_ID_64)
	{
		args.MsgScriptError("Called with invalid motion type settings\n");
		return ScriptValue(0);
	}

	AiLogScript(pNpc, "wait-npc-move-to-dynamic-speed");
	return DcNpcMoveToInternal(args, pNpc, params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
;; Move to a fixed point using dynamic motion type settings, and using a plane for distance checking
(define-c-function npc-move-to-dynamic-speed-plane
	(
		(npc-name			symbol)
		(location			bound-frame)
		(plane-orient		bound-frame)
		(goal-reached-type	npc-goal-reached-type (:default (npc-goal-reached-type stop)))
		(settings-name		symbol		(:default '*default-ai-motion-type-settings*))
		(follow-name		symbol		(:default 'player))			;; who is following me
		(try-to-shoot		boolean		(:default #f))
		(strafe				boolean		(:default #f))
	)
	none
)
*/
SCRIPT_FUNC("npc-move-to-dynamic-speed-plane", DcNpcMoveToDynamicSpeedPlane)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 4);

	MoveToParams params;

	Npc* pNpc = args.NextNpc();
	params.m_pBoundFrame = args.NextBoundFrame();
	params.m_pPlaneOrientBoundFrame = args.NextBoundFrame();
	params.m_dcGoalReachedType		= args.NextI32();
	params.m_motionTypeSettings		= args.NextStringId();
	params.m_hFollower	= args.NextCharacter();
	params.m_tryToShoot = args.NextBoolean();
	params.m_wait		= false;
	params.m_strafing	= args.NextBoolean();
	params.m_performanceId = args.NextStringId();

	if (!params.m_hFollower.HandleValid())
	{
		args.MsgScriptError("Called with invalid follower\n");
		return ScriptValue(0);
	}

	if (params.m_motionTypeSettings == INVALID_STRING_ID_64)
	{
		args.MsgScriptError("Called with invalid motion type settings\n");
		return ScriptValue(0);
	}

	if (!params.m_pPlaneOrientBoundFrame)
	{
		args.MsgScriptError("Called with invalid plane orient bound frame\n");
		return ScriptValue(0);
	}

	AiLogScript(pNpc, "npc-move-to-dynamic-speed-plane");
	return DcNpcMoveToInternal(args, pNpc, params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function wait-npc-move-to-dynamic-speed-plane
	(
		(npc-name			symbol)
		(location			bound-frame)
		(plane-orient		bound-frame)
		(goal-reached-type	npc-goal-reached-type (:default (npc-goal-reached-type stop)))
		(settings-name		symbol		(:default '*default-ai-motion-type-settings*))
		(follow-name		symbol		(:default 'player))			;; who is following me
		(try-to-shoot		boolean		(:default #f))
		(strafe				boolean		(:default #f))
	)
	none
)
*/
SCRIPT_FUNC("wait-npc-move-to-dynamic-speed-plane", DcNpcWaitMoveToDynamicSpeedPlane)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 4);

	MoveToParams params;

	Npc* pNpc = args.NextNpc();
	params.m_pBoundFrame = args.NextBoundFrame();
	params.m_pPlaneOrientBoundFrame = args.NextBoundFrame();
	params.m_dcGoalReachedType		= args.NextI32();
	params.m_motionTypeSettings		= args.NextStringId();
	params.m_hFollower	= args.NextCharacter();
	params.m_tryToShoot = args.NextBoolean();
	params.m_wait		= true;
	params.m_strafing	= args.NextBoolean();
	params.m_performanceId = args.NextStringId();

	if (!params.m_hFollower.HandleValid())
	{
		args.MsgScriptError("Called with invalid follower\n");
		return ScriptValue(0);
	}

	if (params.m_motionTypeSettings == INVALID_STRING_ID_64)
	{
		args.MsgScriptError("Called with invalid motion type settings\n");
		return ScriptValue(0);
	}

	if (!params.m_pPlaneOrientBoundFrame)
	{
		args.MsgScriptError("Called with invalid plane orient bound frame\n");
		return ScriptValue(0);
	}

	AiLogScript(pNpc, "wait-npc-move-to-dynamic-speed-plane");
	return DcNpcMoveToInternal(args, pNpc, params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
;; Move to a fixed point using dynamic motion type settings, and using the goal for distance checking
(define-c-function npc-move-to-dynamic-speed-goal
	(
		(npc-name			symbol)
		(location			bound-frame)
		(goal-reached-type	npc-goal-reached-type (:default (npc-goal-reached-type stop)))
		(settings-name		symbol		(:default '*default-ai-motion-type-settings*))
		(follow-name		symbol		(:default 'player))			;; who is following me
		(try-to-shoot		boolean		(:default #f))
		(strafe				boolean		(:default #f))
	)
	none
)
*/
SCRIPT_FUNC("npc-move-to-dynamic-speed-goal", DcNpcMoveToDynamicSpeedGoal)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 4);

	MoveToParams params;

	Npc* pNpc = args.NextNpc();
	params.m_pBoundFrame		= args.NextBoundFrame();
	params.m_dcGoalReachedType	= args.NextI32();
	params.m_motionTypeSettings = args.NextStringId();
	params.m_hFollower	 = args.NextCharacter();
	params.m_tryToShoot	 = args.NextBoolean();
	params.m_wait		 = false;
	params.m_bGoalOrient = true;
	params.m_strafing	 = args.NextBoolean();
	params.m_performanceId = args.NextStringId();

	if (!params.m_hFollower.HandleValid())
	{
		args.MsgScriptError("Called with invalid follower\n");
		return ScriptValue(0);
	}

	if (params.m_motionTypeSettings == INVALID_STRING_ID_64)
	{
		args.MsgScriptError("Called with invalid motion type settings\n");
		return ScriptValue(0);
	}

	AiLogScript(pNpc, "npc-move-to-dynamic-speed-goal");
	return DcNpcMoveToInternal(args, pNpc, params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function wait-npc-move-to-dynamic-speed-goal
	(
		(npc-name			symbol)
		(location			bound-frame)
		(goal-reached-type	npc-goal-reached-type (:default (npc-goal-reached-type stop)))
		(settings-name		symbol		(:default '*default-ai-motion-type-settings*))
		(follow-name		symbol		(:default 'player))			;; who is following me
		(try-to-shoot		boolean		(:default #f))
		(strafe				boolean		(:default #f))
	)
	none
)
*/
SCRIPT_FUNC("wait-npc-move-to-dynamic-speed-goal", DcNpcWaitMoveToDynamicSpeedGoal)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 4);

	MoveToParams params;

	Npc* pNpc = args.NextNpc();
	params.m_pBoundFrame		= args.NextBoundFrame();
	params.m_dcGoalReachedType	= args.NextI32();
	params.m_motionTypeSettings = args.NextStringId();
	params.m_hFollower	 = args.NextCharacter();
	params.m_tryToShoot	 = args.NextBoolean();
	params.m_wait		 = true;
	params.m_bGoalOrient = true;
	params.m_strafing	 = args.NextBoolean();
	params.m_performanceId = args.NextStringId();

	if (!params.m_hFollower.HandleValid())
	{
		args.MsgScriptError("Called with invalid follower\n");
		return ScriptValue(0);
	}

	if (params.m_motionTypeSettings == INVALID_STRING_ID_64)
	{
		args.MsgScriptError("Called with invalid motion type settings\n");
		return ScriptValue(0);
	}

	AiLogScript(pNpc, "wait-npc-move-to-dynamic-speed-goal");
	return DcNpcMoveToInternal(args, pNpc, params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	;; Move to a fixed target point in world space. This will not work well if the target point is
	;; actually on a moving object like a train -- see (npc-move-to-spawner ...).
	(define-c-function npc-move-to
		(
			(npc-name			symbol)
			(location			bound-frame)
			(motion-type		npc-motion-type			(:default (npc-motion-type run)))
			(goal-reached-type	npc-goal-reached-type	(:default (npc-goal-reached-type stop)))
			(mt-subcategory		symbol					(:default 0))
			(try-to-shoot		boolean					(:default #f))
			(strafe				boolean		(:default #f))
		)
		none
	)
*/
SCRIPT_FUNC("npc-move-to", DcNpcMoveTo)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();

	MoveToParams params;
	params.m_pBoundFrame	   = args.NextBoundFrame();
	params.m_dcMotionType	   = args.NextI32();
	params.m_dcGoalReachedType = args.NextI32();
	params.m_mtSubcategory	   = args.NextStringId();
	params.m_tryToShoot		   = args.NextBoolean();
	params.m_wait	  = false;
	params.m_strafing = args.NextBoolean();
	params.m_performanceId = args.NextStringId();

	AiLogScript(pNpc, "npc-move-to");
	return DcNpcMoveToInternal(args, pNpc, params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-flock-set-flee-target", DcNpcFlockSetFleeTarget)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	Npc* const pNpc = args.NextNpc();
	Flocking::FlockingAgent* const pFlockingAgent = pNpc->GetFlockingAgent();
	if (!pFlockingAgent)
	{
		args.MsgScriptError("This npc cannot flock\n");
		return ScriptValue(0);
	}

	const BoundFrame* const pFleeTargetBf = args.NextBoundFrame();
	const Locator fleeTargetLoc = pFleeTargetBf->GetLocator();
	const bool forceFlee = args.NextBoolean();

	pFlockingAgent->SetFleeTarget(fleeTargetLoc.GetPosition(), GetLocalZ(fleeTargetLoc.GetRotation()), forceFlee);

	AiLogScript(pNpc, "npc-flock-set-flee-target");
	return ScriptValue(1);
}

SCRIPT_FUNC("npc-flock-set-enabled", DcNpcFlockSetEnabled)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* const pNpc = args.NextNpc();
	Flocking::FlockingAgent* const pFlockingAgent = pNpc->GetFlockingAgent();
	if (!pFlockingAgent)
	{
		args.MsgScriptError("This npc cannot flock\n");
		return ScriptValue(0);
	}

	pFlockingAgent->SetEnabled(args.NextBoolean());

	AiLogScript(pNpc, "npc-flock-set-enabled");
	return ScriptValue(1);
}

SCRIPT_FUNC("npc-flock-set-params", DcNpcFlockParams)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* const pNpc = args.NextNpc();
	Flocking::FlockingAgent* const pFlockingAgent = pNpc->GetFlockingAgent();
	if (!pFlockingAgent)
	{
		args.MsgScriptError("This npc cannot flock\n");
		return ScriptValue(0);
	}

	const StringId64 paramsNameId = args.NextStringId();
	pFlockingAgent->SetParams(paramsNameId);

	return ScriptValue(1);
}

SCRIPT_FUNC("npc-flock-queue-move-to", DcNpcFlockQueueMoveTo)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* const pNpc = args.NextNpc();
	Flocking::FlockingAgent* const pFlockingAgent = pNpc->GetFlockingAgent();
	if (!pFlockingAgent)
	{
		args.MsgScriptError("This npc cannot flock\n");
		return ScriptValue(0);
	}

	const BoundFrame* const pQueueMoveToTargetBf = args.NextBoundFrame();
	const Locator queueMoveToTargetLoc = pQueueMoveToTargetBf->GetLocator();
	pFlockingAgent->SetQueueMoveTo(queueMoveToTargetLoc);

	AiLogScript(pNpc, "npc-flock-queue-move-to");
	return ScriptValue(1);
}

SCRIPT_FUNC("npc-flock-queue-move-to-enable-wait", DcNpcFlockQueueMoveToEnableWait)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* const pNpc = args.NextNpc();
	Flocking::FlockingAgent* const pFlockingAgent = pNpc->GetFlockingAgent();
	if (!pFlockingAgent)
	{
		args.MsgScriptError("This npc cannot flock\n");
		return ScriptValue(0);
	}

	pFlockingAgent->SetQueueMoveToBehavior(Flocking::QueueMoveToBehavior::kPrecise);

	AiLogScript(pNpc, "npc-flock-queue-move-to-enable-wait");
	return ScriptValue(1);
}

SCRIPT_FUNC("npc-flock-queue-move-to-set-vision-params", DcNpcFlockQueueMoveToSetLooseVisionParams)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* const pNpc = args.NextNpc();
	Flocking::FlockingAgent* const pFlockingAgent = pNpc->GetFlockingAgent();
	if (!pFlockingAgent)
	{
		args.MsgScriptError("This npc cannot flock\n");
		return ScriptValue(0);
	}

	const StringId64 paramsNameId = args.NextStringId();
	pFlockingAgent->SetVisionParams(paramsNameId);

	return ScriptValue(1);
}


SCRIPT_FUNC("npc-flock-queue-move-to-set-spl", DcNpcFlockQueueMoveToSetSpline)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc *const pNpc = args.NextNpc();
	Flocking::FlockingAgent *const pFlockingAgent = pNpc->GetFlockingAgent();
	if (!pFlockingAgent)
	{
		args.MsgScriptError("This npc cannot flock\n");
		return ScriptValue(0);
	}

	const StringId64 splNameId = args.NextStringId();
	const CatmullRom *const pSpline = g_splineManager.FindByName(splNameId);
	pFlockingAgent->SetQueueMoveToSpline(pSpline);

	return ScriptValue(1);
}

SCRIPT_FUNC("npc-flock-assign-wander-region", DcNpcFlockAssignWanderRegion)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc *const pNpc = args.NextNpc();
	Flocking::FlockingAgent *const pFlockingAgent = pNpc->GetFlockingAgent();
	if (!pFlockingAgent)
	{
		args.MsgScriptError("This npc cannot flock\n");
		return ScriptValue(0);
	}

	const StringId64 regionNameId = args.NextStringId();

	pFlockingAgent->SetAssignedWanderRegion(regionNameId);

	return ScriptValue(1);
}

SCRIPT_FUNC("npc-flock-set-ignore-player-region-blocking", DcNpcFlockSetIgnorePlayerRegionBlocking)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc *const pNpc = args.NextNpc();
	Flocking::FlockingAgent *const pFlockingAgent = pNpc->GetFlockingAgent();
	if (!pFlockingAgent)
	{
		args.MsgScriptError("This npc cannot flock\n");
		return ScriptValue(0);
	}

	const bool ignorePlayerRegionBlocking = args.NextBoolean();

	pFlockingAgent->SetIgnorePlayerRegionBlocking(ignorePlayerRegionBlocking);

	return ScriptValue(1);
}

SCRIPT_FUNC("npc-flock-region-set-enabled", DcNpcFlockRegionSetEnabled)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	const StringId64 regionNameId = args.NextStringId();
	const bool isEnabled = args.NextBoolean();

	Flocking::SetRegionEnabled(regionNameId, isEnabled);

	return ScriptValue(1);
}

SCRIPT_FUNC("npc-flock-is-region-blocked?", DcNpcFlockIsRegionBlocked)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	const StringId64 regionNameId = args.NextStringId();
	const bool isBlocked = Flocking::IsRegionBlocked(regionNameId);

	return ScriptValue(isBlocked);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	;; Move to a fixed target point in world space. This will not work well if the target point is
	;; actually on a moving object like a train -- see (npc-move-to-spawner ...).
	(define-c-function wait-npc-move-to
		(
			(npc-name			symbol)
			(location			bound-frame)
			(motion-type		npc-motion-type			(:default (npc-motion-type run)))
			(goal-reached-type	npc-goal-reached-type	(:default (npc-goal-reached-type stop)))
			(mt-subcategory		symbol					(:default 0))
			(try-to-shoot		boolean					(:default #f))
			(strafe				boolean		(:default #f))
		)
		none
)
*/
SCRIPT_FUNC("wait-npc-move-to", DcWaitNpcMoveTo)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();

	MoveToParams params;
	params.m_pBoundFrame	   = args.NextBoundFrame();
	params.m_dcMotionType	   = args.NextI32();
	params.m_dcGoalReachedType = args.NextI32();
	params.m_mtSubcategory	   = args.NextStringId();
	params.m_tryToShoot		   = args.NextBoolean();
	params.m_wait	  = true;
	params.m_strafing = args.NextBoolean();
	params.m_performanceId = args.NextStringId();

	AiLogScript(pNpc, "wait-npc-move-to");
	return DcNpcMoveToInternal(args, pNpc, params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* Returns true iff there is a path from the character to the given point within the range of
the undirected pathsearch lookup table (~50 range).
*/
SCRIPT_FUNC("npc-can-path-to-bound-frame?", DcCanPathToBoundFrameP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	const Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(false);

	const BoundFrame* pBoundFrame = args.NextBoundFrame();
	if (!pBoundFrame)
		return ScriptValue(false);

	NavLocation navLocation = pNpc->AsReachableNavLocationWs(pBoundFrame->GetLocatorWs().GetPosition(),
															 NavLocation::Type::kNavPoly);
	if (!pNpc->CanPathTo(navLocation, nullptr))
		return ScriptValue(false);

	return ScriptValue(true);
}

SCRIPT_FUNC("npc-can-path-to-object?", DcCanPathToObject)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	const Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(false);

	const NdGameObject* pObj = args.NextGameObject();
	if (!pObj)
		return ScriptValue(false);

	if (!pNpc->CanPathTo(pObj->GetNavLocation()))
		return ScriptValue(false);

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* Returns approx path distance according to the undirected pathsearch lookup table (~50 range).
 */
SCRIPT_FUNC("approx-path-dist-to", DcApproxPathDistTo)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc	   = args.NextNpc();
	float pathDist = FLT_MIN;

	if (pNpc)
	{
		const BoundFrame* pBoundFrame = args.NextBoundFrame();
		NavLocation navLocation		  = pNpc->AsReachableNavLocationWs(pBoundFrame->GetLocatorWs().GetPosition(),
																   NavLocation::Type::kNavPoly);
		pNpc->GetApproxPathDistance(navLocation, pathDist);
	}

	return ScriptValue(pathDist);
}

/// --------------------------------------------------------------------------------------------------------------- ///
//;; Approx SMOOTHED path distance to BoundFrame. Only works within ~50 units of character. Returns a very large float
//;; if unable to find a path.
//(define-c-function npc-get-approx-smooth-path-dist-to
//	(
//		(npc-name	symbol)
//		(location	bound-frame)
//	)
//	float
//)
SCRIPT_FUNC("npc-get-approx-smooth-path-dist-to", DcNpcGetApproxSmoothPathDistTo)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc	   = args.NextNpc();
	float pathDist = NDI_FLT_MAX;

	if (pNpc)
	{
		const BoundFrame* pBoundFrame = args.NextBoundFrame();
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
		pathDist = pNpc->GetFastApproxSmoothPathDistanceOnly(pNpc->AsReachableNavLocationWs(pBoundFrame->GetLocatorWs()
																								.GetPosition(),
																							NavLocation::Type::kNavPoly));
	}

	return ScriptValue(pathDist);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue DcNpcCornerCheckInternal(ScriptArgIterator& args, bool prone, bool wait, bool usePost)
{
	Npc* pNpc = args.NextNpc();

	const BoundFrame* pCornerBf = nullptr;
	U32 postId = AiPostDef::kInvalidPostId;

	if (usePost)
		postId = args.NextU32();
	else
		pCornerBf = args.NextBoundFrame();

	const BoundFrame* pGoalBf = args.NextBoundFrame();
	const DC::NpcMotionType dcMotionType = args.NextI32();
	const MotionType motionType = DcMotionTypeToGame(dcMotionType);

	const DC::NpcDemeanor dcDemeanor = args.NextI32();
	const Demeanor demeanor = DemeanorFromDcDemeanor(dcDemeanor);

	if (!pNpc)
	{
		return ScriptValue(0);
	}

	SsInstance* pScriptInst = GetJobSsContext().GetScriptInstance();
	SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

	if (wait && !(pScriptInst && pGroupInst && pTrackInst))
	{
		args.MsgScriptError("Called outside script context -- will not wait\n");
	}

	NavLocation goalNavLoc;
	if (pGoalBf)
	{
		const Point goalPosWs = pGoalBf->GetTranslationWs();
		goalNavLoc = pNpc->AsReachableNavLocationWs(goalPosWs, NavLocation::Type::kNavPoly);

		if (!pNpc->ValidateMoveToLocation(goalNavLoc))
		{
			args.MsgScriptError("Npc '%s' given corner check goal off the nav mesh! (%g,%g,%g)\n",
								pNpc->GetName(),
								float(goalPosWs.X()),
								float(goalPosWs.Y()),
								float(goalPosWs.Z()));
			return ScriptValue(0);
		}
	}

	if (usePost)
	{
		pNpc->ScriptCommandCornerCheck(postId,
									   goalNavLoc,
									   motionType,
									   prone,
									   demeanor,
									   pGroupInst,
									   pTrackInst ? pTrackInst->GetTrackIndex() : 0,
									   wait);
	}
	else
	{
		if (!pCornerBf)
		{
			args.MsgScriptError("No corner boundframe specified\n");
			return ScriptValue(0);
		}

		const Point cornerPosWs	 = pCornerBf->GetTranslationWs();
		NavLocation cornerNavLoc = pNpc->AsReachableNavLocationWs(cornerPosWs, NavLocation::Type::kNavPoly);

		if (!pNpc->ValidateMoveToLocation(cornerNavLoc))
		{
			args.MsgScriptError("Npc '%s' told to corner check off the nav mesh! (%g,%g,%g)\n",
								pNpc->GetName(),
								float(cornerPosWs.X()),
								float(cornerPosWs.Y()),
								float(cornerPosWs.Z()));
			return ScriptValue(0);
		}

		pNpc->ScriptCommandCornerCheck(cornerNavLoc,
									   goalNavLoc,
									   motionType,
									   prone,
									   demeanor,
									   pGroupInst,
									   pTrackInst ? pTrackInst->GetTrackIndex() : 0,
									   wait);
	}

	// Wait until the corner check is done
	if (wait && pScriptInst && pGroupInst && pTrackInst)
	{
		AiLogScript(pNpc,
					"wait-npc-%s-check%s: %s\n",
					prone ? "prone" : "corner",
					usePost ? "-post" : "",
					pNpc->GetName());
		pGroupInst->WaitForTrackDone("npc-corner-check");
		ScriptContinuation::Suspend(args.GetArgArray());
	}

	return ScriptValue(1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-corner-check
		(
			(npc-name			symbol)
			(corner-location	bound-frame)	/ (post-id	int)
			(next-destination	bound-frame (:default #f))
			(motion-type		npc-motion-type			(:default (npc-motion-type walk)))
			(move-performance   symbol					(:default 0))
			(demeanor			npc-demeanor			(:default (npc-demeanor count)))
		)
		none
	)
*/

SCRIPT_FUNC("npc-corner-check", DcNpcCornerCheck)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	return DcNpcCornerCheckInternal(args, false, false, false);
}

SCRIPT_FUNC("npc-corner-check-post", DcNpcCornerCheckPost)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	return DcNpcCornerCheckInternal(args, false, false, true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function wait-npc-corner-check
		(
			(npc-name			symbol)
			(corner-location	bound-frame)
			(next-destination	bound-frame (:default #f))
			(motion-type		npc-motion-type			(:default (npc-motion-type walk)))
			(move-performance   symbol					(:default 0))
			(demeanor			npc-demeanor			(:default (npc-demeanor count)))
		)
		none
	)
*/
SCRIPT_FUNC("wait-npc-corner-check-c", DcWaitNpcCornerCheck)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	return DcNpcCornerCheckInternal(args, false, true, false);
}

SCRIPT_FUNC("wait-npc-corner-check-c-post", DcWaitNpcCornerCheckPost)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	return DcNpcCornerCheckInternal(args, false, true, true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-get-time-since-last-corner-check ((npc-name symbol))
		float)
*/
SCRIPT_FUNC("npc-get-time-since-last-corner-check", DcNpcGetTimeSinceLastCornerCheck)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(FLT_MAX);

	const AiSearchController* pSearchController = pNpc->GetAnimationControllers()->GetSearchController();
	if (!pSearchController)
		return ScriptValue(FLT_MAX);

	const TimeFrame lastCornerCheckTime = pSearchController->GetLastCornerCheckTime();
	if (lastCornerCheckTime == TimeFrameNegInfinity())
		return ScriptValue(FLT_MAX);

	const TimeFrame timeSinceLastCornerCheck = pNpc->GetTimeSince(lastCornerCheckTime);

	return ScriptValue(timeSinceLastCornerCheck.ToSeconds());
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-get-time-since-last-prone-check ((npc-name symbol))
		float)
*/
SCRIPT_FUNC("npc-get-time-since-last-prone-check", DcNpcGetTimeSinceLastProneCheck)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(FLT_MAX);

	const AiSearchController* pSearchController = pNpc->GetAnimationControllers()->GetSearchController();
	if (!pSearchController)
		return ScriptValue(FLT_MAX);

	const TimeFrame lastProneCheckTime = pSearchController->GetLastProneCheckTime();
	if (lastProneCheckTime == TimeFrameNegInfinity())
		return ScriptValue(FLT_MAX);

	const TimeFrame timeSinceLastProneCheck = pNpc->GetTimeSince(lastProneCheckTime);

	return ScriptValue(timeSinceLastProneCheck.ToSeconds());
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-prone-check
		(
			(npc-name			symbol)
			(prone-location		bound-frame)
			(next-destination	bound-frame (:default #f))
			(motion-type		npc-motion-type			(:default (npc-motion-type walk)))
			(move-performance   symbol					(:default 0))
			(demeanor			npc-demeanor			(:default (npc-demeanor count)))
		)
		none
	)
*/

SCRIPT_FUNC("npc-prone-check", DcNpcProneCheck)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	return DcNpcCornerCheckInternal(args, true, false, false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function wait-npc-prone-check
		(
			(npc-name			symbol)
			(prone-location		bound-frame)
			(next-destination	bound-frame (:default #f))
			(move-performance   symbol					(:default 0))
			(demeanor			npc-demeanor			(:default (npc-demeanor count)))
		)
		none
	)
*/
SCRIPT_FUNC("wait-npc-prone-check", DcWaitNpcProneCheck)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	return DcNpcCornerCheckInternal(args, true, true, false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-global-combat-params", DcNpcSetGlobalCombatParams)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	StringId64 paramsId = args.NextStringId();

	g_aiGameOptions.m_globalCombatParams = ScriptPointer<DC::GlobalCombatParams>(paramsId, SID("ai"));

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	;; Move to a fixed target point in world space. This will not work well if the target point is
	;; actually on a moving object like a train -- see (npc-move-to-spawner ...).
	(define-c-function wait-npc-move-to
		(
			(npc-name			symbol)
			(location			bound-frame)
			(motion-type		npc-motion-type (:default (npc-motion-type run)))
			(goal-reached-type	npc-goal-reached-type (:default (npc-goal-reached-type stop)))
		)
		none
)
*/
SCRIPT_FUNC("wait-npc", DcWaitNpc)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc  = args.NextNpc();
	float time = args.NextFloat();

	if (pNpc)
	{
		AiLogScript(pNpc, "wait-npc");
		SsInstance* pScriptInst = GetJobSsContext().GetScriptInstance();
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();
		pNpc->ScriptCommandWait(time, pGroupInst, pTrackInst ? pTrackInst->GetTrackIndex() : 0, true);
		// Wait until the move-to is done.
		if (pScriptInst && pGroupInst && pTrackInst)
		{
			pGroupInst->WaitForTrackDone("wait-npc");
			ScriptContinuation::Suspend(args.GetArgArray());
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue DcNpcMoveToSpawnerInternal(ScriptArgIterator& args, bool bWait)
{
	Npc* pNpc = args.NextNpc();

	StringId64 spawnerName		  = args.GetStringId(); // don't advance
	const EntitySpawner* pSpawner = args.NextSpawner();

	DC::NpcMotionType dcMotionType = args.NextI32();
	DC::NpcGoalReachedType dcGoalReachedType = args.NextI32();
	StringId64 mtSubcategory = args.NextStringId();
	bool tryToShoot = args.NextBoolean();
	bool searchMove = args.NextBoolean();
	bool strafing	= args.NextBoolean();
	StringId64 performanceId = args.NextStringId();

	MotionType motionType = DcMotionTypeToGame(dcMotionType);
	NavGoalReachedType goalReachedType = ConvertDcGoalReachedType(dcGoalReachedType);
	const bool respectGoalFacing	   = (dcGoalReachedType == DC::kNpcGoalReachedTypeStopAndFace);

	if (pNpc && pSpawner)
	{
		SsVerboseLog(1,
					 "npc-move-to-spawner '%s' [%s, %s]",
					 pSpawner->Name(),
					 GetMotionTypeName(motionType),
					 GetGoalReachedTypeName(goalReachedType));

		float goalRadius = pSpawner->GetData<float>(SID("zone-radius"), 0.0f);

		SsInstance* pScriptInst = GetJobSsContext().GetScriptInstance();
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

		if (bWait && !(pScriptInst && pGroupInst && pTrackInst))
		{
			args.MsgScriptError("Called outside script context -- will not wait\n");
		}

		NavLocation destLoc = pNpc->AsReachableNavLocationWs(pSpawner->GetWorldSpaceLocator().Pos(),
															 NavLocation::Type::kNavPoly);

		if (!pNpc->ValidateMoveToLocation(destLoc))
		{
			args.MsgScriptError("Npc '%s' told to move to spawner off the nav mesh/map! (%s)\n",
								pNpc->GetName(),
								pSpawner->Name());
			return ScriptValue(0);
		}

		if (bWait)
			AiLogScript(pNpc, "wait-npc-move-to-spawner %s", DevKitOnly_StringIdToString(spawnerName));
		else
			AiLogScript(pNpc, "npc-move-to-spawner %s", DevKitOnly_StringIdToString(spawnerName));

		NavMoveArgs moveArgs;
		moveArgs.m_goalRadius	   = goalRadius;
		moveArgs.m_motionType	   = motionType;
		moveArgs.m_goalReachedType = goalReachedType;
		moveArgs.m_mtSubcategory   = mtSubcategory;
		moveArgs.m_strafeMode	   = strafing ? DC::kNpcStrafeAlways : DC::kNpcStrafeNever;
		moveArgs.m_performanceId   = performanceId;

		pNpc->ScriptCommandMoveToSpawner(spawnerName,
										 moveArgs,
										 motionType,
										 respectGoalFacing,
										 tryToShoot,
										 searchMove,
										 pGroupInst,
										 pTrackInst ? pTrackInst->GetTrackIndex() : 0,
										 bWait);

		// Wait until the move-to is done.
		if (bWait && pScriptInst && pGroupInst && pTrackInst)
		{
			pGroupInst->WaitForTrackDone("npc-move-to-spawner");
			ScriptContinuation::Suspend(args.GetArgArray());
		}
	}

	return ScriptValue(1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	;; Move to a spawner. You'll want this one if the spawner is attached to a moving object,
	;; like a train.
	(define-c-function npc-move-to-spawner
		(
			(npc-name			symbol)
			(spawner-name		symbol)
			(motion-type		npc-motion-type			(:default (npc-motion-type run)))
			(goal-reached-type	npc-goal-reached-type	(:default (npc-goal-reached-type stop)))
			(mt-subcategory		symbol					(:default 0)
			(try-to-shoot		boolean					(:default #f))
			(search-move		boolean					(:default #f))
			(strafe				boolean					(:default #f))
		)
		none
	)
*/
SCRIPT_FUNC("npc-move-to-spawner", DcNpcMoveToSpawner)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	return DcNpcMoveToSpawnerInternal(args, false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	;; Move to a spawner. You'll want this one if the spawner is attached to a moving object,
	;; like a train.
	(define-c-function wait-npc-move-to-spawner
		(
			(npc-name			symbol)
			(spawner-name		symbol)
			(motion-type		npc-motion-type			(:default (npc-motion-type run)))
			(goal-reached-type	npc-goal-reached-type	(:default (npc-goal-reached-type stop)))
			(mt-subcategory		symbol					(:default 0)
			(try-to-shoot		boolean					(:default #f))
			(strafe				boolean					(:default #f))
		)
		none
	)
*/
SCRIPT_FUNC("wait-npc-move-to-spawner", DcWaitNpcMoveToSpawner)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	return DcNpcMoveToSpawnerInternal(args, true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-move-to-entry-frame", DcNpcMoveToEntryFrame)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);
	AiLogScript(args.GetNpc(), "npc-move-to-entry-frame");
	return DcNpcMoveToEntryInternal(args, /*wait=*/false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("wait-npc-move-to-entry-frame", DcWaitNpcMoveToEntryFrame)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);
	AiLogScript(args.GetNpc(), "wait-npc-move-to-entry-frame");
	return DcNpcMoveToEntryInternal(args, /* wait = */ true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("#%npc-set-idle-emotion", DcNpcSetIdleEmotion)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 4);
	Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(0);

	StringId64 emotion = args.NextStringId();
	float holdtime = args.NextFloat();
	float blend = args.NextFloat();
	float fade = args.NextFloat();

	const float kMaxHoldTime = 10000.0f;
	if (holdtime > kMaxHoldTime)
	{
		args.MsgScriptError("clamping holdtime from %1.2f to %1.2f to avoid overflow", holdtime, kMaxHoldTime);
		holdtime = kMaxHoldTime;
	}

	pNpc->GetEmotionControl()->SetEmotionalState(emotion, blend, kEmotionPriorityCharacterIdle, holdtime, fade);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-emotion", DcNpcSetEmotion)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);
	Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(0);

	StringId64 emotion = args.NextStringId();
	float holdtime	   = args.NextFloat();
	float blendtime	   = args.NextFloat();
	float fade = args.NextFloat();

	const float kMaxHoldTime = 10000.0f;
	if (holdtime > kMaxHoldTime)
	{
		args.MsgScriptError("clamping holdtime from %1.2f to %1.2f to avoid overflow", holdtime, kMaxHoldTime);
		holdtime = kMaxHoldTime;
	}

	pNpc->GetEmotionControl()->SetEmotionalState(emotion, blendtime, kEmotionPriorityScript, holdtime, fade);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-dialog-look-gesture-active", DcNpcIsDialogLookGestureActive)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.GetNpc();

	if (pNpc)
	{
		pNpc->m_dialogLook.IsGesturePlaying();
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-disable-dialog-look", DcNpcDisableDialogLook)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	float seconds = args.NextFloat();

	if (pNpc)
	{
		pNpc->m_dialogLook.DisableForOneFrame(pNpc->GetCurTime() + Seconds(seconds));
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-disable-dialog-look-gestures", DcNpcDisableDialogLookGestures)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	float seconds = args.NextFloat();

	if (pNpc)
	{
		pNpc->m_dialogLook.DisableGesturesForOneFrame(pNpc->GetCurTime() + Seconds(seconds));
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("player-disable-dialog-look", DcPlayerDisableDialogLook)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	float seconds = args.NextFloat();
	if (GetPlayer())
	{
		GetPlayer()->m_dialogLook.DisableForOneFrame(GetPlayer()->GetCurTime() + Seconds(seconds));
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("player-disable-dialog-look-gestures", DcPlayerDisableDialogLookGestures)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	float seconds = args.NextFloat();
	if (GetPlayer())
	{
		GetPlayer()->m_dialogLook.DisableGesturesForOneFrame(GetPlayer()->GetCurTime() + Seconds(seconds));
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("apply-momentum-hit-protection/f", DcApplyMomentumHitProtection)
{
	SettingSetDefault(&g_aiGameOptions.m_momentumHitProtection, false);
	SettingSetPers(SID("apply-momentum-hit-protection/f"), &g_aiGameOptions.m_momentumHitProtection, true, kPlayerModePriority, 1.0f, Seconds(0.1f));

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-override-depen-capsule-scale/f", DcNpcOverrideDepenCapsuleScale)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc	= args.NextNpc();
	float scale = args.NextFloat();

	if (pNpc)
	{
		pNpc->SetDepenCapsuleScaleScriptOverride(scale);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("player-suppress-shot-at", DcPlayerSuppressShotAt)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	float duration = args.NextFloat();

	if (GetPlayer())
	{
		GetPlayer()->SetUnhittableTime(GetPlayer()->GetCurTime() + Seconds(duration));
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("player-suppress-full-body-hit", DcPlayerSuppressFullBodyHit)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	float duration = args.NextFloat();

	if (GetPlayer())
	{
		GetPlayer()->SetForceNoBulletFullbodyTime(GetPlayer()->GetCurTime() + Seconds(duration));
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("player-suppress-hit", DcPlayerSuppressHit)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	float duration = args.NextFloat();

	if (GetPlayer())
	{
		GetPlayer()->SetForceNoBulletHitTime(GetPlayer()->GetCurTime() + Seconds(duration));
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("#%player-set-idle-emotion", DcPlayerSetIdleEmotion)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	StringId64 emotion = args.NextStringId();
	float holdTime = args.NextFloat();
	float blend = args.NextFloat();
	float fade = args.NextFloat();

	if (GetPlayer())
	{
		GetPlayer()->GetEmotionControl()->SetEmotionalState(emotion, blend, kEmotionPriorityCharacterIdle, holdTime, fade);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("player-set-emotion", DcPlayerSetEmotion)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	StringId64 emotion = args.NextStringId();
	float holdTime	   = args.NextFloat();
	float blend		   = args.NextFloat();
	float fade		   = args.NextFloat();

	if (GetPlayer())
	{
		GetPlayer()->GetEmotionControl()->SetEmotionalState(emotion, blend, kEmotionPriorityScript, holdTime, fade);
	}

	return ScriptValue(0);
}
/// --------------------------------------------------------------------------------------------------------------- ///
/*
	;; Move to a fixed target point in world space. This will not work well if the target point is
	;; actually on a moving object like a train -- see (npc-search-move-to-spawner ...).
	(define-c-function npc-search-move-to
		(
			(npc-name			symbol)
			(location			bound-frame)
			(motion-type		npc-motion-type			(:default (npc-motion-type walk)))
			(goal-reached-type	npc-goal-reached-type	(:default (npc-goal-reached-type stop)))
			(mt-subcategory		symbol					(:default 0))
			(strafe				boolean		(:default #f))
		)
		none
	)
*/
SCRIPT_FUNC("npc-search-move-to", DcNpcSearchMoveTO)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();

	MoveToParams params;
	params.m_pBoundFrame	   = args.NextBoundFrame();
	params.m_dcMotionType	   = args.NextI32();
	params.m_dcGoalReachedType = args.NextI32();
	params.m_mtSubcategory	   = args.NextStringId();
	params.m_searchMove		   = true;
	params.m_wait	  = false;
	params.m_strafing = args.NextBoolean();
	params.m_performanceId = args.NextStringId();

	AiLogScript(pNpc, "npc-search-move-to");
	return DcNpcMoveToInternal(args, pNpc, params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	;; Move to a fixed target point in world space. This will not work well if the target point is
	;; actually on a moving object like a train -- see (npc-search-move-to-spawner ...).
	(define-c-function wait-npc-search-move-to
		(
			(npc-name			symbol)
			(location			bound-frame)
			(motion-type		npc-motion-type			(:default (npc-motion-type walk)))
			(goal-reached-type	npc-goal-reached-type	(:default (npc-goal-reached-type stop)))
			(mt-subcategory		symbol					(:default 0))
			(try-to-shoot		boolean					(:default #f))
			(strafe				boolean		(:default #f))
		)
		none
)
*/
SCRIPT_FUNC("wait-npc-search-move-to", DcWaitNpcSearchMoveTO)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();

	MoveToParams params;
	params.m_pBoundFrame	   = args.NextBoundFrame();
	params.m_dcMotionType	   = args.NextI32();
	params.m_dcGoalReachedType = args.NextI32();
	params.m_mtSubcategory	   = args.NextStringId();
	params.m_searchMove		   = true;
	params.m_wait	  = true;
	params.m_strafing = args.NextBoolean();
	params.m_performanceId = args.NextStringId();

	AiLogScript(pNpc, "wait-npc-search-move-to");
	return DcNpcMoveToInternal(args, pNpc, params);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue DcNpcMoveThroughTapInternal(ScriptArgIterator& args, bool bWait)
{
	// required
	Npc* pNpc = args.NextNpc();
	ActionPack* pActionPack = args.NextActionPack();

	// not required
	const DC::NpcMotionType dcMotionType = args.NextI32();
	const StringId64 mtSubcategory		 = args.NextStringId();

	if (!pNpc || !pActionPack)
	{
		return ScriptValue(false);
	}

	if (pActionPack->GetType() != ActionPack::kTraversalActionPack)
	{
		args.MsgScriptError("%s is not a traversal action pack\n", pActionPack->GetName());
		return ScriptValue(false);
	}

	const MotionType motionType = DcMotionTypeToGame(dcMotionType);

	if (ActionPack* pReverseAp = pActionPack->GetReverseActionPack())
	{
		// if bidirectional, choose the closer entry
		if (DistSqr(pNpc->GetTranslation(), pReverseAp->GetDefaultEntryBoundFrame(SCALAR_LC(0.0f)).GetTranslationWs())
			< DistSqr(pNpc->GetTranslation(),
					  pActionPack->GetDefaultEntryBoundFrame(SCALAR_LC(0.0f)).GetTranslationWs()))
		{
			pActionPack = pReverseAp;
		}
	}

	TraversalActionPack* pTap = (TraversalActionPack*)pActionPack;

	SsInstance* pScriptInst = GetJobSsContext().GetScriptInstance();
	SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

	if (bWait && !(pScriptInst && pGroupInst && pTrackInst))
	{
		args.MsgScriptError("Called outside script context -- will not wait\n");
	}

	BoundFrame goal = pTap->GetBoundFrameInDestSpace();
	goal.SetTranslationWs(pTap->GetExitPointWs());
	bool useGoalFacing = false;

	SsVerboseLog(1,
				 "npc-move-through-tap %s (%g,%g,%g) [%s]",
				 pActionPack->GetName(),
				 float(goal.GetTranslationWs().X()),
				 float(goal.GetTranslationWs().Y()),
				 float(goal.GetTranslationWs().Z()),
				 GetMotionTypeName(motionType));

	NavMoveArgs moveArgs;
	moveArgs.m_goalRadius = 0.5f;
	moveArgs.m_motionType = motionType;
	moveArgs.m_mtSubcategory = mtSubcategory;

	NavLocation navLoc = pNpc->AsReachableNavLocationWs(goal.GetTranslation(), NavLocation::Type::kNavPoly);
	pNpc->ScriptCommandMoveToPos(navLoc,
								 GetLocalZ(goal.GetRotation()),
								 moveArgs,
								 motionType,
								 useGoalFacing,
								 false,
								 false,
								 pGroupInst,
								 pTrackInst ? pTrackInst->GetTrackIndex() : 0,
								 bWait);

	// Wait until the move-to is done.
	if (bWait && pScriptInst && pGroupInst && pTrackInst)
	{
		pGroupInst->WaitForTrackDone("npc-move-to-action-pack");
		ScriptContinuation::Suspend(args.GetArgArray());
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	;; Move through a tap
	(define-c-function npc-move-through-tap
		(
			(npc-name					symbol)
			(action-pack-spawner		symbol)
			(motion-type				npc-motion-type (:default (npc-motion-type run)))
			(mt-subcategory				symbol (:default 0)
		)
		none
	)
*/
SCRIPT_FUNC("npc-move-through-tap", DcNpcMoveThroughTap)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);
	AiLogScript(args.GetNpc(), "npc-move-through-tap");
	return DcNpcMoveThroughTapInternal(args, /*wait=*/false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	;; Move through a tap
	(define-c-function wait-npc-move-through-tap
		(
			(npc-name					symbol)
			(action-pack-spawner		symbol)
			(motion-type				npc-motion-type (:default (npc-motion-type run)))
			(mt-subcategory				symbol (:default 0)
		)
		none
	)
*/
SCRIPT_FUNC("wait-npc-move-through-tap", DcWaitNpcMoveThroughTap)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);
	AiLogScript(args.GetNpc(), "wait-npc-move-through-tap");
	return DcNpcMoveThroughTapInternal(args, /*wait=*/true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-override-grenade-arc-speed/f", DcNpcOverrideGrenadeArcSpeed)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		float speed = args.NextFloat();
		pNpc->OverrideGrenadeArcSpeed(speed);
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-drop-held-grenade", DcNpcDropHeldGrenade)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		pNpc->DropHeldGrenade();
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-spawn-held-grenade", DcNpcSpawnHeldGrenade)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	if (Npc* pNpc = args.NextNpc())
	{
		StringId64 grenadeId = args.NextStringId();
		pNpc->SpawnHeldGrenade(grenadeId);
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-stop-and-face", DcNpcStopAndFace)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	if (Npc* pNpc = args.NextNpc())
	{
		SsVerboseLog(1, "npc-stop");

		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst = GetJobSsContext().GetTrackInstance();

		const Point worldPoint = args.NextPoint();
		const Vector toTarget = worldPoint - pNpc->GetTranslation();

		AiLogScript(pNpc, "npc-stop");
		pNpc->ScriptCommandStopAndStand(pGroupInst, pTrackInst ? pTrackInst->GetTrackIndex() : -1, false);
		pNpc->StopAndFace(AsUnitVectorXz(toTarget, GetLocalZ(pNpc->GetRotation())), -1.0f, FILE_LINE_FUNC); // no, really.

		return ScriptValue(1);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-stop", DcNpcStop)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		SsVerboseLog(1, "npc-stop");

		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

		AiLogScript(pNpc, "npc-stop");
		pNpc->ScriptCommandStopAndStand(pGroupInst, pTrackInst ? pTrackInst->GetTrackIndex() : -1, false);
		pNpc->StopAndStand(-1.0f, FILE_LINE_FUNC); // no, really.

		return ScriptValue(1);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-reload-weapon", DcNpcReloadWeapon)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		bool force = args.NextBoolean();

		IAiWeaponController* pWeaponController = pNpc->GetAnimationControllers()->GetWeaponController();
		ASSERT(pWeaponController);
		if (!pWeaponController->IsBusy())
		{
			SsVerboseLog(1, "npc-reload-weapon");

			AiLogScript(pNpc, "npc-reload-weapon");
			if (IAiShootingLogic* pShootingLogic = pNpc->GetShootingLogic())
			{
				if (force)
				{
					pNpc->ReloadWeapon();
				}
				else
				{
					pShootingLogic->RequestReload();
				}
			}

			// TO DO: Wait until the reload is done?

			return ScriptValue(1);
		}
		else
		{
			args.MsgScriptError("Weapon controller is busy, cannot reload\n");
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-look-aim-at-locator", DcNpcLookAimAtPoint)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const BoundFrame* pBf = args.NextBoundFrame();
	const float timeOut	  = args.NextFloat();
	const bool setFacing  = args.NextBoolean();

	if (!pNpc || !pBf)
		return ScriptValue(false);

	SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

	AiLogScript(pNpc, "npc-look-aim-at-object");
	pNpc->ScriptCommandAimAt(pGroupInst,
							 pTrackInst ? pTrackInst->GetTrackIndex() : 0,
							 false,
							 nullptr,
							 nullptr,
							 INVALID_STRING_ID_64);

	AiLogScript(pNpc, "script-force-aim");

	LookAimPointWsParams params(pBf->GetTranslation());
	pNpc->SetLookAimMode(kLookAimPriorityScript, AiLookAimRequest(SID("LookAimPointWs"), &params), FILE_LINE_FUNC);
	pNpc->GetLookAim().SetFaceAimingEnabled(setFacing, FILE_LINE_FUNC);

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-look-aim-at-object", DcNpcLookAimAtObject)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc		  = args.NextNpc();
	NdGameObject* pGo = args.NextGameObject();
	const StringId64 attachId = args.NextStringId();
	const F32 timeout		  = args.NextFloat();
	const bool setFacing	  = args.NextBoolean();

	if (!pNpc || !pGo)
		return ScriptValue(false);

	SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

	AiLogScript(pNpc, "npc-look-aim-at-object");

	pNpc->ScriptCommandAimAt(pGroupInst,
							 pTrackInst ? pTrackInst->GetTrackIndex() : 0,
							 false,
							 nullptr,
							 pGo,
							 attachId);

	SendEvent(SID("script-force-aim"), pNpc);

	NpcLookAim& lookAim = pNpc->GetLookAim();

	LookAimObjectParams params(pGo);
	lookAim.SetLookAimMode(kLookAimPriorityScript, AiLookAimRequest(SID("LookAimObject"), &params), FILE_LINE_FUNC);
	lookAim.SetFaceAimingEnabled(setFacing, FILE_LINE_FUNC);

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-clear-look-aim-at", DcNpcClearLookAimAt)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

		AiLogScript(pNpc, "npc-stop-aiming");
		pNpc->ClearLookAimMode(kLookAimPriorityScript, FILE_LINE_FUNC);

		const Npc::ScriptCommandInfo& cmd = pNpc->GetScriptCommandInfo();
		// if (cmd.m_command.m_type == Npc::ScriptCommand::kCommandAimAt)
		{
			pNpc->ScriptCommandClearAimAt(pGroupInst, pTrackInst ? pTrackInst->GetTrackIndex() : 0, false);
			SendEvent(SID("script-clear-force-aim"), pNpc);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-aim-at-point", DcNpcAimAtPoint)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = Npc::FromProcess(args.NextProcess());
	const Point worldPoint	 = args.NextPoint();
	const F32 timeOutSeconds = args.NextFloat();
	const bool setFacing	 = args.NextBoolean();

	if (!pNpc)
		return ScriptValue(false);

	AiLogScript(pNpc, "npc-aim-at-point");

	SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

	BoundFrame frame(worldPoint);

	pNpc->ScriptCommandAimAt(pGroupInst,
							 pTrackInst ? pTrackInst->GetTrackIndex() : 0,
							 false,
							 &frame,
							 nullptr,
							 INVALID_STRING_ID_64);

	SendEvent(SID("script-force-aim"), pNpc);

	NpcLookAim& lookAim = pNpc->GetLookAim();

	LookAimPointWsParams params(worldPoint);
	lookAim.SetAimMode(kLookAimPriorityScript,
					   AiLookAimRequest(SID("LookAimPointWs"), &params),
					   FILE_LINE_FUNC,
					   timeOutSeconds);
	lookAim.SetFaceAimingEnabled(setFacing, FILE_LINE_FUNC);

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-aim-at-object", DcNpcAimAtObject)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = Npc::FromProcess(args.NextProcess());
	NdGameObject* pObject		= args.NextGameObject();
	const StringId64 attachName = args.NextStringId();
	const F32 timeOutSeconds	= args.NextFloat();
	const bool setFacing		= args.NextBoolean();

	if (!pObject || !pNpc)
	{
		return ScriptValue(false);
	}

	AiLogScript(pNpc, "npc-aim-at-object %s", pObject->GetName());

	SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

	pNpc->ScriptCommandAimAt(pGroupInst,
							 pTrackInst ? pTrackInst->GetTrackIndex() : 0,
							 false,
							 nullptr,
							 pObject,
							 INVALID_STRING_ID_64);

	SendEvent(SID("script-force-aim"), pNpc);

	NpcLookAim& lookAim = pNpc->GetLookAim();

	LookAimLocatableParams params(*pObject, attachName);
	lookAim.SetAimMode(kLookAimPriorityScript,
					   AiLookAimRequest(SID("LookAimLocatable"), &params),
					   FILE_LINE_FUNC,
					   timeOutSeconds);
	lookAim.SetFaceAimingEnabled(setFacing, FILE_LINE_FUNC);

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-clear-aim-at", DcNpcClearAimAt)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = Npc::FromProcess(args.NextProcess());

	if (!pNpc)
		return ScriptValue(false);

	AiLogScript(pNpc, "clear-aim-at");
	pNpc->GetLookAim().ClearAimMode(kLookAimPriorityScript, FILE_LINE_FUNC);

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-aiming-at-object?", DcNpcIsAimingAtObjectP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	const Npc* pNpc = Npc::FromProcess(args.NextProcess());
	const NdGameObject* pObject = args.NextGameObject();
	const F32 radius = args.NextFloat();

	if (!pNpc)
		return ScriptValue(false);

	if (!pObject)
		return ScriptValue(false);

	const Point point = pObject->GetTranslation()
						+ (pObject->IsKindOf(g_type_Character) ? Vector(0.0f, 1.0f, 0.0f) : Vector(kZero));

	const F32 dist	   = Dist(pNpc->GetTranslation(), point);
	const F32 angleDeg = RADIANS_TO_DEGREES(Atan(radius / dist));

	const bool result = pNpc->IsAimingFirearmWithinThresholdAt(point, angleDeg);

	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-aiming-at-point?", DcNpcIsAimingAtPointP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	const Npc* pNpc	  = Npc::FromProcess(args.NextProcess());
	const Point point = args.NextPoint();
	const F32 radius  = args.NextFloat();

	if (!pNpc)
		return ScriptValue(false);

	const F32 dist	   = Dist(pNpc->GetTranslation(), point);
	const F32 angleDeg = RADIANS_TO_DEGREES(Atan(radius / dist));

	const bool result = pNpc->IsAimingFirearmWithinThresholdAt(point, angleDeg);

	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-aiming-at-target?", DcNpcIsAimingAtTargetP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	const Npc* pNpc = Npc::FromProcess(args.NextProcess());

	if (!pNpc)
		return ScriptValue(false);

	const bool result = pNpc->IsAimingAtTarget();

	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-do-only-scripted-look-ats/f", DcNpcDoOnlyScriptedLookAts)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
		pNpc->DoOnlyScriptedLookAts();

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-look-at-point", DcNpcLookAtPoint)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = Npc::FromProcess(args.NextProcess());
	const Point worldPoint	 = args.NextPoint();
	const F32 timeOutSeconds = args.NextFloat();

	if (!pNpc)
		return ScriptValue(false);

	AiLogScript(pNpc, "npc-look-at-point");

	LookAimPointWsParams params(worldPoint);
	pNpc->GetLookAim().SetLookMode(kLookAimPriorityScript,
								   AiLookAimRequest(SID("LookAimPointWs"), &params),
								   FILE_LINE_FUNC,
								   timeOutSeconds);

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-look-at-object", DcNpcLookAtObject)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = Npc::FromProcess(args.NextProcess());
	const NdLocatableObject* pObject = args.NextLocatableObject();
	const StringId64 attachName		 = args.NextStringId();
	const F32 timeOutSeconds		 = args.NextFloat();

	if (!pObject)
		return ScriptValue(false);

	if (!pNpc)
		return ScriptValue(false);

	AiLogScript(pNpc, "npc-look-at-object %s", pObject->GetName());

	LookAimLocatableParams params(*pObject, attachName);
	pNpc->GetLookAim().SetLookMode(kLookAimPriorityScript,
								   AiLookAimRequest(SID("LookAimLocatable"), &params),
								   FILE_LINE_FUNC,
								   timeOutSeconds);

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-clear-look-at", DcNpcClearLookAt)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = Npc::FromProcess(args.NextProcess());

	if (!pNpc)
		return ScriptValue(false);

	AiLogScript(pNpc, "clear-look-at");
	pNpc->GetLookAim().ClearLookMode(kLookAimPriorityScript, FILE_LINE_FUNC);

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void NpcSupriseHelper(ScriptArgIterator& args, bool wait)
{
	Npc* pNpc = args.NextNpc();
	const BoundFrame* pBf	 = args.NextBoundFrame();
	const bool isBigSurprise = args.NextBoolean();

	if (!pNpc || !pBf)
		return;

	SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

	AiLogScript(pNpc, "wait-npc-surprise");
	pNpc->ScriptCommandSurprise(pGroupInst, pTrackInst ? pTrackInst->GetTrackIndex() : 0, pBf, wait, isBigSurprise);


	if (wait)
	{
		pGroupInst->WaitForTrackDone("npc-surprise");
		ScriptContinuation::Suspend(args.GetArgArray());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("wait-npc-surprise", DCWaitNpcSurprise)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	NpcSupriseHelper(args, true);
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-surprise", DCNpcSurprise)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	NpcSupriseHelper(args, false);
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-hearing-multiplier", DcNpcSetHearingMultiplier)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	const float factor = args.NextFloat();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-set-hearing-multiplier");
		pNpc->SetHearingMultiplier(factor);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-is-position-dynamically-blocked? ((npc-name symbol) (pos bound-frame))
	boolean)
*/
SCRIPT_FUNC("npc-is-position-dynamically-blocked?", DcNpcIsPositionDynamicallyBlockedP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const BoundFrame* pBoundFrame = args.NextBoundFrame();

	if (pNpc && pBoundFrame)
	{
		bool isBlocked	  = true;
		const Point posWs = pBoundFrame->GetTranslation();
		const Point posPs = pNpc->GetParentSpace().UntransformPoint(posWs);
		if (const NavControl* pNavCon = pNpc->GetNavControl())
		{
			isBlocked = pNavCon->IsPointDynamicallyBlockedPs(posPs);
		}

		return ScriptValue(isBlocked);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue DcNpcGroupAliveCount(int argc,
										ScriptValue* argv,
										ScriptFnAddr __SCRIPT_FUNCTION__,
										const char* commandName)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	StringId64 groupId;
	DArrayAdapter group = NextDArray(args, groupId);

	I32 aliveCount = 0;

	if (group.IsValid())
	{
		U32F count = group.GetElemCount();
		for (U32F index = 0; index < count; ++index)
		{
			StringId64 id = group.GetElemAt(index).GetAsStringId();

			const Npc* pNpc = EngineComponents::GetNpcManager()->FindNpcByUserId(id);

			if (pNpc && !pNpc->IsDead())
			{
				++aliveCount;
			}
		}
	}

	return ScriptValue(aliveCount);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-group-alive-count", DcNpcGroupAliveCount)
{
	return DcNpcGroupAliveCount(argc, argv, __SCRIPT_FUNCTION__, "npc-group-alive-count");
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-prefer-target
		(
			(npc-name	symbol)
			(target		symbol)
		)
		none
	)
*/
SCRIPT_FUNC("npc-prefer-target", DcNpcPreferTarget)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	NdGameObject* pTargetGameObject = args.NextGameObject();

	if (pNpc)
	{
		if (AiTargetAppraiser* pTargetAppraiser = pNpc->GetTargetAppraiser())
		{
			AiLogScript(pNpc, "npc-prefer-target");
			pTargetAppraiser->SetPreferredTarget(pTargetGameObject);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-nonprefer-target
		(
			(npc-name	symbol)
			(target		symbol)
		)
		none
	)
*/
SCRIPT_FUNC("npc-nonprefer-target", DcNpcNonpreferTarget)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	NdGameObject* pTargetGameObject = args.NextGameObject();

	if (pNpc)
	{
		if (AiTargetAppraiser* pTargetAppraiser = pNpc->GetTargetAppraiser())
		{
			AiLogScript(pNpc, "npc-nonprefer-target");
			pTargetAppraiser->SetNonpreferredTarget(pTargetGameObject);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-force-target
		(
			(npc-name	symbol)
			(target		symbol)
		)
		none
	)
*/
SCRIPT_FUNC("npc-force-target", DcNpcForceTarget)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	NdGameObjectHandle hTarget = args.NextGameObjectHandle();

	if (pNpc)
	{
		if (AiTargetAppraiser* pTargetAppraiser = pNpc->GetTargetAppraiser())
		{
			AiLogScript(pNpc, "npc-force-target");
			pTargetAppraiser->SetForcedTarget(hTarget);
		}
	}

	return ScriptValue(0);
}

//;; Unleash the Buddy to kill the specified victim NPC if they happen to attack them
//;; (therefore to increase the likelihood of demise you may wish to also npc-force-target)
//(define-c-function unleash-buddy-against-npc
//	(
//		(buddy		symbol)
//		(victim		symbol)
//	)
//	none
//)
SCRIPT_FUNC("unleash-buddy-against-npc", DcUnleashBuddyAgainstNpc)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	Buddy* pBuddy = Buddy::FromProcess(pNpc);
	Npc* pVictim  = args.NextNpc();

	if (pBuddy && pVictim)
	{
		if (BuddyCombatLogic* pLogic = pBuddy->GetBuddyCombatLogic())
		{
			AiLogScript(pBuddy, "unleash-buddy-against-npc");
			pLogic->ScriptUnleashUpon(pBuddy, pVictim);
		}
	}
	else
	{
		MsgConErr("unleash-buddy-against-npc called on invalid 'buddy' %s\n", DevKitOnly_StringIdToString(pNpc ? pNpc->GetUserId() : INVALID_STRING_ID_64));
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-drop-pickup", DcNpcDropPickup)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		SendEvent(SID("npc-drop-pickup"), pNpc);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-disable-drop-pickup-on-death", DcNpcDisableDropPickupOnDeath)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		SendEvent(SID("npc-disable-drop-pickup-on-death"), pNpc);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-drop-weapon
		(
			(npc-name string)
		)
		none
	)
*/
SCRIPT_FUNC("npc-drop-weapon", DcNpcDropWeapon)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	bool allowInteraction = args.NextBoolean();

	if (g_netInfo.IsNetActive() && g_netInfo.IsTo1())
		return ScriptValue(0);

	if (pNpc)
	{
		if (ProcessWeaponBase* pWeapon = pNpc->GetWeapon())
		{
			if (pWeapon->IsMeleeWeapon())
			{
				const DC::MeleeWeaponGameplayDef* pDef = pWeapon->GetMeleeWeaponGameplayDef();
				const bool isUnarmed = pDef ? pDef->m_isUnarmed : false;
				if (isUnarmed)
					return ScriptValue(0);
			}

			// shouldn't we respect the drop table?
			const Vector detachImpulse = pNpc->GetVelocityPs();
			pWeapon->RequestDetach(0.0f, detachImpulse, kZero, false, true);

			if (allowInteraction)
				SendEvent(SID("set-interaction-enabled"), pWeapon);
			else
				SendEvent(SID("set-interaction-disabled"), pWeapon);

			SendEvent(SID("weapon-dropped-by-npc"), pNpc, pWeapon);

			g_ssMgr.BroadcastEvent(SID("npc-weapon-dropped"), pWeapon->GetScriptId(), pNpc->GetScriptId(), pWeapon->GetWeaponDefId());

			if (pNpc->GetSpawner())
			{
				pWeapon->AssociateWithLevel(pNpc->GetSpawner()->GetLevel());
			}

			AiLogScript(pNpc, "npc-drop-weapon");
			if (PropInventory* pPropInv = pNpc->GetPropInventory())
			{
				pPropInv->ForgetProp(pWeapon);
			}

			ProcessWeapon* pFireWeapon = ProcessWeapon::FromProcess(pWeapon);
			if (pFireWeapon)
				pFireWeapon->SetTotalAmmo(pNpc->FindDroppedAmmo());
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-detach-item", DcNpcDetachItem)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	StringId64 itemProcId = args.NextStringId();

	if (pNpc)
	{
		if (PropInventory* pPropInv = pNpc->GetPropInventory())
		{
			for (int i = 0; i < pPropInv->GetNumProps(); i++)
			{
				MutableNdAttachableObjectHandle hItem = pPropInv->GetPropHandle(i);
				if (hItem.GetScriptId() == itemProcId)
				{
					pPropInv->ForgetProp(hItem.ToProcess());
					break;
				}
			}
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-get-weapon
		(
			(npc-name string)
		)
		symbol
	)
*/
SCRIPT_FUNC("npc-get-weapon", DcNpcGetWeapon)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc(true);
	StringId64 weaponId = INVALID_STRING_ID_64;

	if (pNpc)
	{
		ProcessWeaponBase* pWeapon = pNpc->GetWeapon();
		if (pWeapon)
		{
			weaponId = pWeapon->GetWeaponDefId();
		}
		else
		{
			pWeapon = pNpc->GetMostRecentWeaponInHand();
			if (pWeapon)
				weaponId = pWeapon->GetWeaponDefId();
		}
	}

	return ScriptValue(weaponId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-weapon-process-id", DcNpcGetWeaponProcessId)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		ProcessWeaponBase* pWeapon = pNpc->GetWeapon();
		if (pWeapon)
		{
			return ScriptValue(pWeapon->GetUserId());
		}
		else
		{
			pWeapon = pNpc->GetMostRecentWeaponInHand();
			if (pWeapon)
				return ScriptValue(pWeapon->GetUserId());
		}
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-weapon-process-id-from-weapon-id", DcNpcGetWeaponProcessIdFromWeaponId)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	StringId64 weaponId = args.NextStringId();

	if (pNpc)
	{
		if (const PropInventory* pPropInventory = pNpc->GetPropInventory())
		{
			for (int i = 0; i < pPropInventory->GetNumProps(); i++)
			{
				const NdAttachableObject* pObject = pPropInventory->GetPropHandle(i).ToProcess();
				if (const ProcessWeaponBase* pProcessWeaponBase = ProcessWeaponBase::FromProcess(pObject))
				{
					if (pProcessWeaponBase->GetWeaponDefId() == weaponId)
					{
						return ScriptValue(pProcessWeaponBase->GetUserId());
					}
				}
			}
		}
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-give-weapon", DcNpcGiveWeapon)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);
	Npc* pNpc = args.NextNpc();
	StringId64 weaponId = args.NextStringId();
	I32 ammo		 = args.NextI32();
	const bool force = args.NextBoolean();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-give-weapon %s %d", DevKitOnly_StringIdToString(weaponId), ammo);

		Npc::GiveWeaponParams gwParams;
		gwParams.m_ammo = ammo;
		gwParams.m_allowIfNoFirearmSkill = force;

		ProcessWeaponBase* pGivenWeapon = pNpc->GiveWeapon(weaponId, &gwParams);

		if (!pGivenWeapon)
		{
			args.MsgScriptError("Failed to give weapon %s to Npc %s\n",
								DevKitOnly_StringIdToString(weaponId),
								pNpc->GetName());
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool RequestDrawWeaponInternal(Npc& npc,
									  const ProcessWeaponBase* pWeapon,
									  DC::HolsterMode mode,
									  float blendTime,
									  DC::AnimCurveType curveType)
{
	if (!pWeapon)
	{
		return false;
	}

	IAiWeaponController* pWeaponController = npc.GetAnimationControllers()->GetWeaponController();
	if (!pWeaponController)
	{
		return false;
	}

	const DC::BlendParams blend = { blendTime, blendTime, curveType };

	if (!pWeaponController->RequestPrimaryWeapon(pWeapon->GetWeaponDefId()))
	{
		return false;
	}

	switch (mode)
	{
	case DC::kHolsterModeNormal:
	case DC::kHolsterModeFast:
	case DC::kHolsterModeVeryFast:
		pWeaponController->RequestGunState(kGunStateOut, false, &blend);
		break;

	case DC::kHolsterModeSlow:
		pWeaponController->RequestGunState(kGunStateOut, true, &blend);
		break;

	case DC::kHolsterModeImmediate:
	default:
		pWeaponController->ForceGunState(kGunStateOut, false, false, blendTime);
		break;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(bool,
								 DcNpcDrawFirearm,
								 (Npc& npc, DC::HolsterMode mode, float blendTime, DC::AnimCurveType curveType),
								 "npc-draw-firearm")
{
	const ProcessWeaponBase* pProcessWeaponBase = npc.GetFirearm();

	return RequestDrawWeaponInternal(npc, pProcessWeaponBase, mode, blendTime, curveType);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(bool,
								 DcNpcDrawPistol,
								 (Npc& npc, DC::HolsterMode mode, float blendTime, DC::AnimCurveType curveType),
								 "npc-draw-pistol")
{
	const ProcessWeaponBase* pProcessWeaponBase = npc.GetPistol();

	return RequestDrawWeaponInternal(npc, pProcessWeaponBase, mode, blendTime, curveType);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(bool,
								 DcNpcDrawLongGun,
								 (Npc& npc, DC::HolsterMode mode, float blendTime, DC::AnimCurveType curveType),
								 "npc-draw-long-gun")
{

	const ProcessWeaponBase* pProcessWeaponBase = npc.GetLongGun();

	return RequestDrawWeaponInternal(npc, pProcessWeaponBase, mode, blendTime, curveType);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(bool,
								 DcNpcDrawMeleeWeapon,
								 (Npc& npc, DC::HolsterMode mode, float blendTime, DC::AnimCurveType curveType),
								 "npc-draw-melee-weapon")
{
	const ProcessWeaponBase* pProcessWeaponBase = npc.GetMeleeWeapon();

	return RequestDrawWeaponInternal(npc, pProcessWeaponBase, mode, blendTime, curveType);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-weapon-visible", DcNpcSetWeaponVisible)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject* pNpcGo = args.NextGameObject();
	bool visible		 = args.NextBoolean();

	Npc* pNpc		 = Npc::FromProcess(pNpcGo);
	SimpleNpc* pSNpc = SimpleNpc::FromProcess(pNpcGo);
	ProcessWeaponBase* pWeapon = nullptr;

	if (pNpc)
	{
		pWeapon = pNpc->GetWeapon();
	}
	else if (pSNpc)
	{
		pWeapon = pSNpc->GetWeapon();
	}

	if (pWeapon && pWeapon->GetDrawControl())
	{
		SsVerboseLog(1, "npc-set-weapon-visible: Making npc %s's weapon visible", pNpcGo->GetName());
		if (visible)
		{
			pWeapon->GetDrawControl()->ShowAllMesh();
		}
		else
		{
			pWeapon->GetDrawControl()->HideAllMesh();
		}

		if (pNpc)
		{
			AiLogScript(pNpc, "npc-set-weapon-visible %s", DcTrueFalse(visible));
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	;; works for any character, including npcs and the player. negative value leaves original in place
	(define-c-function set-health-parameters
		(
			(char-name string)
			(regen-delay float :default -1.0)
			(regen-rate float :default -1.0)
			(max-health float :default -1.0)
			(new-health float :default -1.0)
		)
		none
	)
*/
SCRIPT_FUNC("start-health-regen", DcStartHealthRegen)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Character* pChar = args.NextCharacter();
	if (pChar)
	{
		pChar->GetHealthSystem()->StartHealthRegen();
	}
	return ScriptValue(0);
}

SCRIPT_FUNC("set-health-parameters", DcSetHealthParameters)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Character* pCharacter = args.NextCharacter();
	float regenDelay	  = args.NextFloat();
	float regenRate		  = args.NextFloat();
	I32 maxHealth		  = args.NextI32();
	I32 newHealth		  = args.NextI32();

	if (pCharacter)
	{
		IHealthSystem* pHs = pCharacter->GetHealthSystem();
		if (pCharacter->IsDead())
		{
			args.MsgScriptError("'%s' is already dead\n", pCharacter->GetName());
		}
		else if (pHs)
		{
			if (regenDelay >= 0.0f)
				pHs->SetRegenDelay(regenDelay);

			if (regenRate >= 0.0f)
				pHs->SetRegenRate(regenRate);

			if (maxHealth >= 0)
				pHs->SetMaxHealth(maxHealth);

			if (newHealth >= 0)
				pHs->SetHealth(newHealth);
		}
		else
		{
			args.MsgScriptError("'%s' has no health system\n", pCharacter->GetName());
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-num-medkits", DcNpcGetNumMedkits)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	I32 numMedkits = 0;
	if (pNpc)
	{
		numMedkits = pNpc->GetNumMedkits();
	}

	return ScriptValue(numMedkits);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-num-medkits", DcNpcSetNumMedkits)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc	   = args.NextNpc();
	I32 numMedkits = args.NextI32();

	if (pNpc)
	{
		pNpc->SetNumMedkits(numMedkits);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-change-vision-settings", DcNpcChangeVisionSettings)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	StringId64 settingsName = args.NextStringId();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-change-vision-settings");
		if (settingsName != INVALID_STRING_ID_64)
		{
			MsgAi("[%s] Changing to vision settings '%s'\n", pNpc->GetName(), DevKitOnly_StringIdToString(settingsName));
		}
		else
		{
			MsgAi("[%s] Changing vision settings to default only\n", pNpc->GetName());
		}

		bool success = false;
		if (AiVision* pVision = pNpc->GetVision())
		{
			success = pVision->ChangeVisionSettings(settingsName);
		}

		return ScriptValue(success);
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-change-smelling-settings", DcNpcChangeSmellingSettings)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	StringId64 settingsName = args.NextStringId();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-change-smelling-settings");
		if (settingsName != INVALID_STRING_ID_64)
		{
			MsgAi("[%s] Changing to smelling settings '%s'\n",
				  pNpc->GetName(),
				  DevKitOnly_StringIdToString(settingsName));
		}
		else
		{
			MsgAi("[%s] Changing smelling settings to default only\n", pNpc->GetName());
		}

		bool success = false;
		if (AiSmelling* pSmelling = pNpc->GetSmelling())
		{
			success = pSmelling->ChangeSmellingSettings(settingsName);
		}

		return ScriptValue(success);
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-feet-wet?", DcNpcFeetWetP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		return ScriptValue(pNpc->AreFeetWet());
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-request-break-glass-from-cover", DcNpcRequestBreakGlassFromCover)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		pNpc->RequestBreakGlass();
	}
	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-smelling-settings", DcNpcGetSmellingSettings)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	const DC::AiSmellingSettings* pSettings = nullptr;
	if (pNpc)
	{
		if (AiSmelling* pSmelling = pNpc->GetSmelling())
		{
			pSettings = pSmelling->GetSmellingSettings();
		}
	}
	return ScriptValue(pSettings);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-attach-to-battle-manager", DcNpcAttachToBattleManager)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	Process* pBattleMgr = args.NextProcess();

	if (pNpc && pBattleMgr)
	{
		AiLogScript(pNpc, "npc-attach-to-battle-manager");
		if (pNpc->IsDead())
		{
			// silently do nothing...
			return ScriptValue(false);
		}

		pNpc->SetOwningProcess(pBattleMgr);
		SendEventFrom(pNpc, SID("npc-attached"), pBattleMgr);

		return ScriptValue(true);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-set-accuracy-multiplier
		(
			(npc-name symbol)
			(multiplier float)
		)
		none
	)
*/
SCRIPT_FUNC("npc-set-accuracy-multiplier", DcNpcSetAccuracyMultiplier)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc	= Npc::FromProcess(args.NextProcess());
	float multi = args.NextFloat();

	if (pNpc)
	{
		pNpc->ForceAccuracyMultiplier(multi);
		return ScriptValue(true);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-in-melee-with-player?", DcNpcInMeleeWithPlayerP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	bool result = false;

	const ProcessMeleeAction* pNpcAction = pNpc ? pNpc->GetCurrentMeleeAction() : nullptr;
	if (pNpcAction
		&& pNpcAction->GetCharControlFor(pNpc)
		&& pNpcAction->GetCharControlFor(GetPlayer())
		&& pNpcAction->ShouldSyncAp())
	{
		result = true;
	}

	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-turn-to
	(
		(npc-name		symbol)
		(location		point)
		(motion-type	npc-motion-type	(:default (npc-motion-type run))
		(try-to-shoot	boolean			(:default #f))
	)
	none
)
*/
SCRIPT_FUNC("npc-turn-to", DcNpcTurnTo)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc		  = args.NextNpc();
	const Point posWs = args.NextPoint();
	const DC::NpcMotionType dcMotionType = args.NextI32();
	const MotionType motionType = DcMotionTypeToGame(dcMotionType);
	const bool tryToShoot		= args.NextBoolean();

	if (pNpc)
	{
		const Point pos = posWs;
		AiLogScript(pNpc, "npc-turn-to");

		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

		pNpc->ScriptCommandTurnTo(pos,
								  motionType,
								  tryToShoot,
								  pGroupInst,
								  pTrackInst ? pTrackInst->GetTrackIndex() : 0,
								  false);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function wait-npc-turn-to
	(
		(npc-name		symbol)
		(location		point)
		(motion-type	npc-motion-type	(:default (npc-motion-type run))
		(try-to-shoot	boolean			(:default #f))
	)
	none
)
*/
SCRIPT_FUNC("wait-npc-turn-to", DcWaitNpcTurnTo)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc		  = args.NextNpc();
	const Point posWs = args.NextPoint();
	const DC::NpcMotionType dcMotionType = args.NextI32();
	const MotionType motionType = DcMotionTypeToGame(dcMotionType);
	const bool tryToShoot		= args.NextBoolean();

	if (pNpc)
	{
		SsInstance* pScriptInst = GetJobSsContext().GetScriptInstance();
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

		if (!(pScriptInst && pGroupInst && pTrackInst))
		{
			args.MsgScriptError("Called outside script context -- will not wait\n");
		}

		const Point pos = posWs;

		AiLogScript(pNpc, "wait-npc-turn-to");
		pGroupInst->WaitForTrackDone("wait-npc-turn-to");
		pNpc->ScriptCommandTurnTo(pos,
								  motionType,
								  tryToShoot,
								  pGroupInst,
								  pTrackInst ? pTrackInst->GetTrackIndex() : 0,
								  true);
		ScriptContinuation::Suspend(argv);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
 * (define-c-function npc-play-facial-anim ((npc-name symbol) (facial-anim symbol) (fade-in-sec float (:default 0.2))
 *(fade-out-sec float (:default 0.2)) (low-priority (:default #f))) boolean)
 */
SCRIPT_FUNC("npc-play-facial-anim", DcNpcPlayFacialAnim)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	const StringId64 animName = args.NextStringId();
	const float fadeInSec	  = args.NextFloat();
	const float fadeOutSec	  = args.NextFloat();
	const bool lowPriority	  = args.NextBoolean();

	if (!pNpc)
		return ScriptValue(false);

	AiLogScript(pNpc, "npc-play-facial-anim");
	AnimControl* pAnimControl = pNpc->GetAnimControl();
	if (nullptr == pAnimControl->LookupAnim(animName).ToArtItem())
	{
		args.MsgScriptError("npc '%s' doesn't have anim: '%s'\n",
							pNpc->GetName(),
							DevKitOnly_StringIdToString(animName));
		return ScriptValue(false);
	}

	AnimSimpleLayer* pFaceLayer = nullptr;

	if (lowPriority)
	{
		pFaceLayer = pAnimControl->GetSimpleLayerById(SID("facial-partial-low"));
	}

	if (!pFaceLayer)
	{
		pFaceLayer = pAnimControl->GetSimpleLayerById(SID("facial-partial"));
	}

	AnimSimpleLayer::FadeRequestParams params;
	params.m_fadeTime  = fadeInSec;
	params.m_blendType = DC::kAnimCurveTypeUniformS;
	params.m_layerFadeOutParams.m_enabled	= true;
	params.m_layerFadeOutParams.m_blendType = DC::kAnimCurveTypeUniformS;
	params.m_layerFadeOutParams.m_fadeTime	= fadeOutSec;

	if (!pFaceLayer || !pFaceLayer->RequestFadeToAnim(animName, params))
	{
		args.MsgScriptError("npc '%s' failed to play anim: '%s'\n",
							pNpc->GetName(),
							DevKitOnly_StringIdToString(animName));
		return ScriptValue(false);
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-process-count", DcNpcProcessCount)
{
	return ScriptValue((int)EngineComponents::GetNpcManager()->GetNpcCount());
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-last-alive?", DcNpcIsLastAliveP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pRefNpc = args.NextNpc();
	if (!pRefNpc)
		return ScriptValue(false);

	AtomicLockJanitorRead_Jls npcGroupLock(EngineComponents::GetNpcManager()->GetNpcGroupLock(), FILE_LINE_FUNC);
	for (const MutableNpcHandle& hNpc : EngineComponents::GetNpcManager()->GetNpcs())
	{
		const Npc* pNpc = hNpc.ToProcess();
		if (pNpc && pNpc != pRefNpc && !pNpc->IsDead())
			return ScriptValue(false);
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-max-count", DcNpcMaxCount)
{
	return ScriptValue(kMaxNpcCount);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* (define-c-function suppress-move-performances ((npc-name symbol))
 *	boolean)
 */
SCRIPT_FUNC("suppress-move-performances", DcNpcSuppressMovePerformances)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue(INVALID_STRING_ID_64);

	pNpc->DisableMovePerformancesOneFrame();

	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* (define-c-function npc-get-move-performance ((npc-name symbol))
 *	boolean)
 */
SCRIPT_FUNC("npc-get-move-performance", DcNpcGetMovePerformance)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue(INVALID_STRING_ID_64);

	if (const IAiLocomotionController* pLocomotionController = pNpc->GetAnimationControllers()->GetLocomotionController())
	{
		if (!pLocomotionController->IsDoingMovePerformance())
			return ScriptValue(INVALID_STRING_ID_64);

		return ScriptValue(pLocomotionController->GetMovePerformanceId());
	}
	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* (define-c-function npc-is-in-scripted-anim? ((npc-name symbol))
 *	boolean)
 */
SCRIPT_FUNC("npc-is-in-scripted-anim?", DcNpcIsInScriptedAnimP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue(false);

	const bool inScriptedAnim = pNpc->IsInScriptedAnimationState();
	return ScriptValue(inScriptedAnim);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-set-force-drop-table ((npc-name symbol) (table-id symbol))
none)
*/
SCRIPT_FUNC("npc-set-force-drop-table", DcNpcSetForceDropTable)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	StringId64 tableId = args.NextStringId();

	if (pNpc)
	{
		pNpc->SetForceDropTable(tableId);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-get-gun-ammo-dropped ((npc-name symbol))
	int32)
*/
SCRIPT_FUNC("npc-get-gun-ammo-dropped", DcNpcGetGunAmmoDrop)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		return ScriptValue(pNpc->GetSpawnInfo().m_gunAmmoDropped);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-get-melee-type ((npc-name symbol))
ai-melee-type)
*/
SCRIPT_FUNC("npc-get-melee-type", DcNpcGetMeleeType)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	const DC::AiMeleeType* pMeleeType = nullptr;
#if 0 // T2-TODO
	if (pNpc)
	{
		if (IAiSyncActionController* pAISyncActionController = pNpc->GetAnimationControllers()->GetSyncActionController())
		{
			pMeleeType = pAISyncActionController->GetMeleeType();
		}
	}
#endif

	return ScriptValue(pMeleeType);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-flinch", DcNpcFlinch)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	if (Human* pHuman = Human::FromProcess(args.NextNpc()))
	{
		pHuman->TryPlayFlinch(pHuman->GetTranslation(), pHuman->GetTranslation());
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-motion-type", DcNpcGetMotionType)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	if (pNpc && pNpc->IsMoving())
	{
		return ScriptValue(GameMotionTypeToDc(pNpc->GetCurrentMotionType()));
	}
	return ScriptValue(DC::kNpcMotionTypeMax);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-mt-subcategory", DcNpcGetMtSubcategory)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	if (pNpc && pNpc->IsMoving())
	{
		return ScriptValue(pNpc->GetCurrentMtSubcategory());
	}
	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-moving?", DcNpcIsMovingP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		return ScriptValue(pNpc->IsMoving());
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-weapon-down", DcNpcWeaponDown)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-weapon-down");
		IAiWeaponController* pWeaponController = pNpc->GetAnimationControllers()->GetWeaponController();
		if (pWeaponController)
		{
			pWeaponController->RequestWeaponDown();
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-weapon-up", DcNpcWeaponUp)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-weapon-up");
		IAiWeaponController* pWeaponController = pNpc->GetAnimationControllers()->GetWeaponController();
		if (pWeaponController)
		{
			pWeaponController->RequestWeaponUp();
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-holster-weapon", DcNpcHolsterWeapon)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	Npc* pNpc = args.NextNpc();
	DC::HolsterMode mode = static_cast<DC::HolsterMode>(args.NextI32());
	const bool holster	 = args.NextBoolean();
	const float blendTime = args.NextFloat();
	const DC::AnimCurveType blendCurve = DC::AnimCurveType(args.NextI64());

	DC::BlendParams blend;
	blend.m_animFadeTime = blendTime;
	blend.m_curve = blendCurve;
	blend.m_motionFadeTime = -1.0f;

	if (pNpc)
	{
		if (IAiWeaponController* pWeaponController = pNpc->GetAnimationControllers()->GetWeaponController())
		{
			const GunState gunState = holster ? kGunStateHolstered : kGunStateOut;

			AiLogScript(pNpc,
						"npc-holster-weapon %s %d (blend: %0.3f %s)",
						DcTrueFalse(holster),
						mode,
						blendTime,
						DC::GetAnimCurveTypeName(blendCurve));

			switch (mode)
			{
			case DC::kHolsterModeNormal:
			case DC::kHolsterModeSlow:
				pNpc->RequestGunState(gunState, true, &blend);
				break;
			case DC::kHolsterModeVeryFast:
			case DC::kHolsterModeFast:
				pNpc->RequestGunState(gunState, false, &blend);
				break;
			case DC::kHolsterModeImmediate:
				pWeaponController->ForceGunState(gunState, false, true, blendTime);
				break;
			default:
				AI_HALTF(("Unknown holster mode %d", mode));
				break;
			};
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("wait-npc-holster-weapon", DcWaitNpcHolsterWeapon)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	Npc* pNpc = args.NextNpc();
	DC::HolsterMode mode = static_cast<DC::HolsterMode>(args.NextI32());
	bool holster		 = args.NextBoolean();
	const float blendTime = args.NextFloat();
	const DC::AnimCurveType blendCurve = DC::AnimCurveType(args.NextI64());

	DC::BlendParams blend;
	blend.m_animFadeTime = blendTime;
	blend.m_curve = blendCurve;
	blend.m_motionFadeTime = -1.0f;

	if (pNpc)
	{
		if (IAiWeaponController* pWeaponController = pNpc->GetAnimationControllers()->GetWeaponController())
		{
			const GunState gunState = holster ? kGunStateHolstered : kGunStateOut;

			AiLogScript(pNpc, "wait-npc-holster-weapon %s %d", DcTrueFalse(holster), mode);

			switch (mode)
			{
			case DC::kHolsterModeNormal:
			case DC::kHolsterModeSlow:
				pNpc->RequestGunState(gunState, true, &blend);
				break;
			case DC::kHolsterModeVeryFast:
			case DC::kHolsterModeFast:
				pNpc->RequestGunState(gunState, false, &blend);
				break;
			case DC::kHolsterModeImmediate:
				pWeaponController->ForceGunState(gunState, false, true, blendTime);
				break;
			default:
				AI_HALTF(("Unknown holster mode %d", mode));
				break;
			};

			if (mode != DC::kHolsterModeImmediate)
			{
				SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
				SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

				BitArray128 bitArrayExclude;
				bitArrayExclude.SetBit(kWeaponController);

				pGroupInst->WaitForTrackDone("wait-npc-holster-weapon");
				pNpc->ScriptCommandWaitForNotBusy(bitArrayExclude, pGroupInst, pTrackInst->GetTrackIndex());
				ScriptContinuation::Suspend(argv);
			}
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-holstering-weapon?", DcNpcIsHolsteringWeaponP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(false);

	const IAiWeaponController* pController = pNpc->GetAnimationControllers()->GetWeaponController();
	if (!pController)
		return ScriptValue(false);

	const bool result = pController->IsPendingGunState(kGunStateHolstered);
	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-unholstering-weapon?", DcNpcIsUnholsteringWeaponP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(false);

	const IAiWeaponController* pController = pNpc->GetAnimationControllers()->GetWeaponController();
	if (!pController)
		return ScriptValue(false);

	const bool result = pController->IsPendingGunState(kGunStateOut);
	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-has-firearm?", DcNpcHasFirearmP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		return ScriptValue(pNpc->HasFirearm());
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-has-melee-weapon?", DcNpcHasMeleeWeaponP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		return ScriptValue(pNpc->HasMeleeWeapon());
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-request-firearm", DcNpcRequestFirearm)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	if (pNpc && pNpc->HasFirearm())
	{
		pNpc->RequestDrawFirearm();
		return ScriptValue(true);
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-request-melee-weapon", DcNpcRequestMeleeWeapon)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	if (pNpc && pNpc->HasMeleeWeapon())
	{
		pNpc->RequestDrawMeleeWeapon();
		return ScriptValue(true);
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* (define-c-function npc-kill ((npc-name symbol))
 *	none)
 */
SCRIPT_FUNC("npc-kill", DcNpcKill)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();
	AiLogScript(pNpc, "npc-kill");
	EngineComponents::GetNpcManager()->KillNpc(pNpc);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function npc-enable-watch-spline ((npc-name symbol) (id symbol) (track-rate float)) none)
SCRIPT_FUNC("npc-enable-watch-spline", DcNpcEnableWatchSpline)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const StringId64 splineId = args.NextStringId();
	const F32 trackRate		  = args.NextFloat();

	if (pNpc)
	{
		pNpc->SetWatchSpline(splineId, trackRate);
	}

	return ScriptValue(0);
}

// (define-c-function npc-disable-watch-spline ((npc-name symbol)) none)
SCRIPT_FUNC("npc-disable-watch-spline", DcNpcDisableWatchSpline)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		pNpc->DisableWatchSpline();
	}

	return ScriptValue(0);
}

// (define-c-function npc-enable-watch-circle ((npc-name symbol) (center bound-frame) (radius float) (track-rate float) (delay float)) none)
SCRIPT_FUNC("npc-enable-watch-circle", DcNpcEnableWatchCircle)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	Npc* pNpc = args.NextNpc();
	const BoundFrame* pCenterBf = args.NextBoundFrame();
	const float radius	= args.NextFloat();
	const F32 trackRate = args.NextFloat();
	const F32 delay		= args.NextFloat();

	if (pNpc && pCenterBf)
	{
		pNpc->SetWatchSphere(Sphere(pCenterBf->GetTranslationWs(), radius), trackRate, delay);
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("npc-get-zone", DcNpcGetZone)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		if (const Region* pRegion = pNpc->GetZone().GetRegion())
		{
			return ScriptValue(pRegion->GetNameId());
		}
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

// (define-c-function npc-point-in-zone? ((npc-name symbol) (pos point)) boolean)
SCRIPT_FUNC("npc-point-in-zone?", DcNpcPointInZone)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc		= args.NextNpc();
	const Point pos = args.NextPoint();

	if (pNpc)
	{
		return ScriptValue(pNpc->IsPointInZoneWs(pos));
	}

	return ScriptValue(false);
}

// (define-c-function npc-in-zone? ((npc-name symbol)) boolean)
SCRIPT_FUNC("npc-in-zone?", DcNpcInZone)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc		= args.NextNpc();

	if (pNpc)
	{
		return ScriptValue(pNpc->IsInZone());
	}

	return ScriptValue(false);
}

// (define-c-function npc-disable-watch-circle ((npc-name symbol)) none)

/// --------------------------------------------------------------------------------------------------------------- ///
//  Infected
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function npc-set-infected-params ((npc-name symbol) (params-id symbol)) none)
SCRIPT_FUNC("npc-set-infected-params", DcNpcSetInfectedParams)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	StringId64 paramsId = args.NextStringId();

	if (pNpc && paramsId != INVALID_STRING_ID_64)
	{
		AiLogScript(pNpc, "npc-set-infected-params");
		pNpc->GetArchetype().SetInfectedParamsId(paramsId);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function npc-get-infected-params-id ((npc-name symbol)) none)
SCRIPT_FUNC("npc-get-infected-params-id", DcNpcGetInfectedParamsId)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(0);

	const StringId64 paramsId = pNpc->GetArchetype().GetInfectedParamsId();
	return ScriptValue(paramsId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function infected-allow-sleep ((npc-name symbol) (allow-sleep boolean)) none)
SCRIPT_FUNC("infected-allow-sleep", DcInfectedAllowSleep)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Infected* pInfected	  = Infected::FromProcess(args.NextNpc());
	const bool allowSleep = args.NextBoolean();

	if (pInfected)
	{
		const TimeFrame timeSinceSpawn = pInfected->GetTimeSince(pInfected->GetSpawnTime());
		if (timeSinceSpawn < Seconds(1.0f))
		{
			// disallow sleep if already woken up when checkpointed
			bool sleepAllowedInCheckpoint = false;
			const StringId64 infectedId = pInfected->GetUserId();
			const bool sleepAllowedInCheckpointValid = GameSave::CheckCharacterSleepAllowedInCurrentCheckpoint(infectedId, &sleepAllowedInCheckpoint);
			if (sleepAllowedInCheckpointValid && sleepAllowedInCheckpoint != allowSleep)
				return ScriptValue(0);
		}

		AiLogScript(pInfected, "infected-allow-sleep %s", DcTrueFalse(allowSleep));
		pInfected->AllowSleep(allowSleep);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function infected-is-asleep? ((npc-name symbol)) boolean)
SCRIPT_FUNC("infected-is-asleep?", DcInfectedIsAsleepP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Infected* pInfected = Infected::FromProcess(args.NextNpc());

	if (pInfected)
	{
		return ScriptValue(pInfected->IsSleeping());
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function infected-is-throwing? ((npc-name symbol)) boolean)
SCRIPT_FUNC("infected-is-throwing?", DcInfectedIsThrowingP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Infected* pInfected = Infected::FromProcess(args.NextNpc());

	if (pInfected)
	{
		return ScriptValue(pInfected->IsThrowing());
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function infected-explode-attack ((npc-name symbol)) boolean)
SCRIPT_FUNC("infected-explode-attack", DcInfectedExplodeAttack)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Infected* pInfected = Infected::FromProcess(args.NextNpc());
	const Vector offset = args.NextVector();

	if (pInfected)
	{
		pInfected->SpawnAttackExplosion(offset);

		return ScriptValue(true);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function infected-explode-attack ((npc-name symbol)) boolean)
SCRIPT_FUNC("infected-broadcast-explosion-request", DcInfectedBroadcastExplosionRequest)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Infected* pInfected = Infected::FromProcess(args.NextNpc());

	if (pInfected)
	{
		pInfected->BroadcastExplosionRequest();

		return ScriptValue(true);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function infected-explode-death ((npc-name symbol)) boolean)
SCRIPT_FUNC("infected-explode-death", DcInfectedExplodeDeath)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Infected* pInfected = Infected::FromProcess(args.NextNpc());

	if (pInfected)
	{
		AiLogCombat(pInfected, "Death explosion: infected-explode-death script function.\n");
		pInfected->SpawnDeathExplosion();
		return ScriptValue(true);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function register-no-legless-infected-region ((region-id)) none)
SCRIPT_FUNC("register-no-legless-infected-region", DcRegisterNoLeglessInfectedRegion)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const StringId64 regionId = args.NextStringId();

	RegisterNoLeglessInfectedRegion(regionId);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function unregister-no-legless-infected-region ((region-id)) none)
SCRIPT_FUNC("unregister-no-legless-infected-region", DcUnregisterNoLeglessInfectedRegion)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const StringId64 regionId = args.NextStringId();

	UnregisterNoLeglessInfectedRegion(regionId);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function infected-set-can-go-legless ((npc-name symbol) (can-go-legless boolean) none)
SCRIPT_FUNC("infected-set-can-go-legless", DcInfectedSetCanGoLegless)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Infected* pInfected = Infected::FromProcess(args.NextNpc());
	const bool canGoLegless = args.NextBoolean();
	if (!pInfected)
		return ScriptValue(0);

	pInfected->SetCanGoLegless(canGoLegless);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// (define-c-function infected-explode-death ((npc-name symbol)) boolean)
SCRIPT_FUNC("spawn-trap-mine", DcSpawnTrapMine)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);
	const StringId64 bulletSettingsId = args.NextStringId();
	Npc* pOwnerNpc = args.NextNpc();
	const BoundFrame* plantBf  = args.NextBoundFrame();
	const Locator plantLocator = plantBf->GetLocatorWs();

	if (!pOwnerNpc)
		return ScriptValue(INVALID_STRING_ID_64);

	// Spawn projectile
	ProjectileProcessSpawnInfo info;

	info.m_position = *plantBf;
	info.m_bulletArgs.m_hSource	 = MutableNdGameObjectHandle(pOwnerNpc);
	info.m_bulletArgs.m_weaponId = SID("trap-bomb");
	info.m_bulletArgs.m_ignoreCollisionDist = false;
	info.m_bulletArgs.m_settingsId = bulletSettingsId;
	info.m_thrownFromSafeDist	   = false;

	const DC::BulletSettings* pBulletSettings = ProcessWeaponBase::LookupBulletSettings(info.m_bulletArgs.m_settingsId);
	const StringId64 procType = (pBulletSettings != nullptr && pBulletSettings->m_processType != INVALID_STRING_ID_64)
									? pBulletSettings->m_processType
									: SID("ProjectileThrowable");

	SpawnInfo spawn(procType);
	spawn.m_pRoot	  = &plantLocator;
	spawn.m_pUserData = &info;
	spawn.m_pParent	  = EngineComponents::GetProcessMgr()->m_pAttachTree;

	ProjectileThrowable* pThrownObj = ProjectileThrowable::FromProcess(NewProcess(spawn));
	if (!pThrownObj)
		return ScriptValue(INVALID_STRING_ID_64);

	pThrownObj->PlantMine(plantBf->GetTranslationWs(),
						  GetLocalY(plantBf->GetRotationWs()),
						  0.0f,
						  plantBf->GetBinding().GetRigidBody());

	return ScriptValue(pThrownObj->GetUserId());
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void DcInfectedcBarkInternal(ScriptArgIterator& args, Infected* pInfected, Vector_arg direction, bool wait)
{
	SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();
	const U32 trackIndex = pTrackInst ? pTrackInst->GetTrackIndex() : -1;

	pInfected->ScriptCommandInfectedCanvass(pGroupInst, trackIndex, direction, wait);

	if (pGroupInst && wait)
	{
		pGroupInst->WaitForTrackDone("infected-canvass");
		ScriptContinuation::Suspend(args.GetArgArray());
	}
}

// (define-c-function infected-bark-forward ((npc-name symbol)) boolean)
SCRIPT_FUNC("infected-bark-forward", DcInfectedBarkForward)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Infected* pInfected = Infected::FromProcess(args.NextNpc());

	if (pInfected)
	{
		const Vector dir = GetLocalZ(pInfected->GetRotation());
		DcInfectedcBarkInternal(args, pInfected, dir, false);
		return ScriptValue(true);
	}

	return ScriptValue(false);
};

// (define-c-function infected-bark-at ((npc-name symbol) (bark-at-position point)) boolean)
SCRIPT_FUNC("infected-bark-at", DcInfectedBarkAt)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Infected* pInfected	 = Infected::FromProcess(args.NextNpc());
	const Point position = args.NextPoint();

	if (pInfected)
	{
		const Vector dir = SafeNormalize(position - pInfected->GetTranslation(), GetLocalZ(pInfected->GetRotation()));
		DcInfectedcBarkInternal(args, pInfected, dir, false);
		return ScriptValue(true);
	}

	return ScriptValue(false);
};

// (define-c-function infected-make-legless ((npc-name symbol) (dismember-leg-lt boolean) (dismember-leg-rt boolean) (dismember-leg-upper-lt boolean) (dismember-leg-upper-rt boolean) (passive boolean) (is-explosion boolean) (impulse vector)) none)
SCRIPT_FUNC("infected-make-legless", DcInfectedMakeLegless)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 7);
	Infected* pInfected	 = Infected::FromProcess(args.NextNpc());
	const bool dismemberLegLt = args.NextBoolean();
	const bool dismemberLegRt = args.NextBoolean();
	const bool dismemberLegUpperLt = args.NextBoolean();
	const bool dismemberLegUpperRt = args.NextBoolean();
	const bool passive = args.NextBoolean();
	const bool explosion = args.NextBoolean();

	if (pInfected)
	{
		pInfected->MakeLegless(dismemberLegLt, dismemberLegRt, dismemberLegUpperLt, dismemberLegUpperRt, passive, explosion);
	}

	return ScriptValue(0);
};

// (define-c-function wait-infected-bark-forward ((npc-name symbol) boolean)
SCRIPT_FUNC("wait-infected-bark-forward", DcWaitInfectedBarkForward)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Infected* pInfected = Infected::FromProcess(args.NextNpc());

	if (pInfected)
	{
		const Vector dir = GetLocalZ(pInfected->GetRotation());
		DcInfectedcBarkInternal(args, pInfected, dir, true);
		return ScriptValue(true);
	}

	return ScriptValue(false);
};

// (define-c-function wait-infected-bark-at ((npc-name symbol) (bark-at-position point)) boolean)
SCRIPT_FUNC("wait-infected-bark-at", DcWaitInfectedBarkAt)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Infected* pInfected	 = Infected::FromProcess(args.NextNpc());
	const Point position = args.NextPoint();

	if (pInfected)
	{
		const Vector dir = SafeNormalize(position - pInfected->GetTranslation(), GetLocalZ(pInfected->GetRotation()));
		DcInfectedcBarkInternal(args, pInfected, dir, true);
		return ScriptValue(true);
	}

	return ScriptValue(false);
};

SCRIPT_FUNC("point-is-in-infected-cloud?", DcPointIsInInfectedCloudP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Point pos	  = args.NextPoint();
	const bool result = InfectedPustuleCloudAtlas::Get().IsPointInCloud(pos);
	return ScriptValue(result);
};

SCRIPT_FUNC("get-num-infected-clouds", DcGetNumInfectedClouds)
{
	const U32 numClouds = InfectedPustuleCloudAtlas::Get().GetNumClouds();
	return ScriptValue(numClouds);
}

SCRIPT_FUNC("decay-all-infected-clouds", DcDecayAllInfectedClouds)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const F32 duration = args.NextFloat();
	InfectedPustuleCloudAtlas::Get().DecayAllClouds(duration);
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
//  NPC Behaviors
/// --------------------------------------------------------------------------------------------------------------- ///

static ScriptValue NpcDoBehaviorInternal(ScriptArgIterator& args,
										 Npc* pNpc,
										 StringId64 behaviorId,
										 AiBehaviorParams* pParams,
										 bool requestWait)
{
	if (pNpc && behaviorId != INVALID_STRING_ID_64)
	{
		const bool success = pNpc->RequestBehavior(AiLogicControl::kLogicPriorityScript,
												   behaviorId,
												   pParams,
												   FILE_LINE_FUNC);

		if (!success)
		{
			args.MsgScriptError("%s is not a valid behavior.\n", DevKitOnly_StringIdToString(behaviorId));
			return ScriptValue(false);
		}

		if (const AiBehavior* pBehavior = pNpc->GetBehavior())
		{
			bool wait = (requestWait && pBehavior->IsRunning());

			SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
			SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

			pNpc->BeginScriptBehavior(pGroupInst, pTrackInst ? pTrackInst->GetTrackIndex() : 0, wait);

			if (wait)
			{
				pGroupInst->WaitForTrackDone("wait-npc-do-behavior");
				ScriptContinuation::Suspend(args.GetArgArray());
			}
		}

		// We successfully started the behavior -- it may be complete or have failed, but we know it did *something*.
		return ScriptValue(true);
	}

	// Failure -- either the NPC or the AiBehavior is unavailable.
	return ScriptValue(false);
}
static ScriptValue NpcDoBehavior(ScriptArgIterator& args, bool requestWait)
{
	Npc* pNpc = args.NextNpc();
	StringId64 behaviorId	  = args.NextStringId();
	AiBehaviorParams* pParams = reinterpret_cast<AiBehaviorParams*>(const_cast<void*>(args.NextVoidPointer()));

	return NpcDoBehaviorInternal(args, pNpc, behaviorId, pParams, requestWait);
}
SCRIPT_FUNC("npc-do-behavior", DcNpcDoBehavior)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	AiLogScript(args.GetNpc(), "npc-do-behavior");
	return NpcDoBehavior(args, false);
}
SCRIPT_FUNC("wait-npc-do-behavior", DcWaitNpcDoBehavior)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	AiLogScript(args.GetNpc(), "wait-npc-do-behavior");
	return NpcDoBehavior(args, true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-stop-behavior", DcNpcStopBehavior)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		AiLogScript(pNpc, "npc-stop-behavior");
		pNpc->DestroyBehavior();
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-get-current-behavior ((npc-name symbol))
	symbol)
*/
SCRIPT_FUNC("npc-get-current-behavior", DcNpcGetCurrentBehavior)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	StringId64 behaviorId = INVALID_STRING_ID_64;

	if (const Npc* pNpc = args.NextNpc())
	{
		const AiBehavior* pBehavior = pNpc->GetBehavior();

		if (pBehavior)
		{
			behaviorId = pBehavior->GetTypeId();
		}
	}

	return ScriptValue(behaviorId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-behavior-done? ((npc-name symbol))
	boolean)
*/
SCRIPT_FUNC("npc-behavior-done?", DcNpcBehaviorDoneP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	bool done = false;

	if (const Npc* pNpc = args.NextNpc())
	{
		const AiBehavior* pBehavior = pNpc->GetBehavior();

		done = !pBehavior || pBehavior->IsDone();
	}

	return ScriptValue(done);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-buddy?", DcNpcIsBuddyP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	const Npc* pNpc = args.NextNpc(kItemMissingOk);

	return ScriptValue(pNpc && pNpc->IsBuddyNpc());
}

/*
(define-c-function npc-buddy-should-play-aborted-save-jumpback? ((buddy symbol))
	boolean)
*/
SCRIPT_FUNC("npc-buddy-should-play-aborted-save-jumpback?", DcNpcBuddyShouldPlayAbortedSaveJumpbackP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* const pNpc = args.NextNpc();

	if (pNpc)
	{
		if (BuddyCombatLogic* pLogic = pNpc->GetBuddyCombatLogic())
		{
			return ScriptValue(pLogic->ShouldPlayAbortedSaveJumpBack(pNpc));
		}
	}

	return ScriptValue(false);
}

/*
(define-c-function npc-buddy-should-abort-critical-grab? ((buddy symbol) (time-since-last-critical-grab float))
	boolean)

(define-c-function npc-buddy-can-enter-critical-grab? ((buddy symbol) (time-since-last-critical-grab float))
	boolean)

(define-c-function npc-buddy-good-time-for-critical-grab? ((buddy symbol) (time-since-last-critical-grab float))
	boolean)
*/
SCRIPT_FUNC("npc-buddy-should-abort-critical-grab?", DcNpcBuddyShouldAbortCriticalGrabP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* const pNpc = args.NextNpc();
	const float sec = args.NextFloat();

	SCRIPT_ASSERT(sec >= 0.0f);

	if (pNpc)
	{
		if (BuddyCombatLogic* pLogic = pNpc->GetBuddyCombatLogic())
		{
			return ScriptValue(pLogic->ShouldAbortCriticalGrab());
		}
	}

	return ScriptValue(true);
}
SCRIPT_FUNC("npc-buddy-can-enter-critical-grab?", DcNpcBuddyCanEnterCriticalGrabP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* const pNpc = args.NextNpc();
	const float sec = args.NextFloat();

	SCRIPT_ASSERT(sec >= 0.0f);

	if (pNpc)
	{
		if (BuddyCombatLogic* pLogic = pNpc->GetBuddyCombatLogic())
		{
			return ScriptValue(pLogic->CriticalGrabPermitted());
		}
	}

	return ScriptValue(true);
}
SCRIPT_FUNC("npc-buddy-good-time-for-critical-grab?", DcNpcBuddyGoodTimeForCriticalGrabP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* const pNpc = args.NextNpc();
	const float sec = args.NextFloat();

	SCRIPT_ASSERT(sec >= 0.0f);

	if (pNpc)
	{
		if (BuddyCombatLogic* pLogic = pNpc->GetBuddyCombatLogic())
		{
			return ScriptValue(pLogic->GoodTimeForCriticalGrab());
		}
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-melee-hold ((buddy symbol) (duration-in-seconds float)
	none)
*/
SCRIPT_FUNC("npc-buddy-melee-hold", DcNpcBuddyMeleeHold)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* const pNpc = args.NextNpc();
	const float sec = args.NextFloat();

	SCRIPT_ASSERT(sec >= 0.0f && sec <= 2.0f);

	if (pNpc)
	{
		if (BuddyCombatLogic* pLogic = pNpc->GetBuddyCombatLogic())
		{
			pLogic->MeleeHold(pNpc, Seconds(sec));
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-disable-urgent-callouts/f ((buddy symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-disable-urgent-callouts/f", DcNpcBuddyDisableUrgentCalloutsF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		if (BuddyCombatLogic* pLogic = pNpc->GetBuddyCombatLogic())
		{
			pLogic->ScriptDisableUrgentCallouts(pNpc);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-do-not-informational-call-out-a-specific-npc/f ((buddy symbol) (target symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-do-not-informational-call-out-a-specific-npc/f", DcNpcBuddyDoNotInformationalCalloutASpecificNpcF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pBuddy = args.NextNpc();
	Npc* pTarget = args.NextNpc();

	if (pBuddy && pTarget)
	{
		if (BuddyCombatLogic* pLogic = pBuddy->GetBuddyCombatLogic())
		{
			pLogic->ScriptSkipInformationalCalloutTarget(pBuddy, pTarget);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-do-not-urgent-call-out-a-specific-npc/f ((buddy symbol) (target symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-do-not-urgent-call-out-a-specific-npc/f", DcNpcBuddyDoNotUrgentCalloutASpecificNpcF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pBuddy = args.NextNpc();
	Npc* pTarget = args.NextNpc();

	if (pBuddy && pTarget)
	{
		if (BuddyCombatLogic* pLogic = pBuddy->GetBuddyCombatLogic())
		{
			pLogic->ScriptSkipUrgentCalloutTarget(pBuddy, pTarget);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-combat-request-long-gun-only/f ((npc symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-combat-request-long-gun-only/f", DcNpcBuddyCombatRequestLongGunOnlyF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		if (BuddyCombatLogic* pLogic = pNpc->GetBuddyCombatLogic())
		{
			pLogic->ScriptRequestLongGun(pNpc);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-combat-request-pistol-only/f ((npc symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-combat-request-pistol-only/f", DcNpcBuddyCombatRequestPistolOnlyF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		if (BuddyCombatLogic* pLogic = pNpc->GetBuddyCombatLogic())
		{
			pLogic->ScriptRequestPistol(pNpc);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function unleash-buddy/f ((npc symbol))
	none)
*/
SCRIPT_FUNC("unleash-buddy/f", DcUnleashBuddyF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		if (BuddyCombatLogic* pLogic = pNpc->GetBuddyCombatLogic())
		{
			pLogic->ScriptUnleashBuddy(pNpc);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-request-no-combat-delay/f ((npc symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-request-no-combat-delay/f", DcNpcBuddyRequestNoCombatDelay)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		if (BuddyCombatLogic* pLogic = pNpc->GetBuddyCombatLogic())
		{
			pLogic->ScriptRequestNoCombatDelay(pNpc);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-allow-vox-about-player-fall-even-when-scripted/f ((npc symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-allow-vox-about-player-fall-even-when-scripted/f", DcNpcBuddyAllowVoxAboutPlayerFallEvenWhenScriptedF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		if (BuddyCombatLogic* pLogic = pNpc->GetBuddyCombatLogic())
		{
			pLogic->ScriptAllowVoxAboutPlayerFallEvenWhenScripted(pNpc);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-debug-only-buddy-melee-test ((buddy symbol) (enemy symbol))
	none)
*/
SCRIPT_FUNC("npc-debug-only-buddy-melee-test", DcNpcDebugOnlyBuddyMeleeTest)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	if (Npc* const pBuddy = args.NextNpc())
	{
		if (BuddyCombatLogic* const pLogic = pBuddy->GetBuddyCombatLogic())
		{
			if (Npc* const pEnemy = args.NextNpc())
			{
				pLogic->DebugOnlyBuddyMeleeTest(pEnemy);
			}
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-disable-defend/f ((npc symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-disable-defend/f", DcNpcBuddyDisableDefendF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		if (BuddyCombatLogic* pLogic = pNpc->GetBuddyCombatLogic())
		{
			pLogic->ScriptDisableDefend(pNpc);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-disable-informational-callouts/f ((buddy symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-disable-informational-callouts/f", DcNpcBuddyDisableInformationalCalloutsF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		if (BuddyCombatLogic* pLogic = pNpc->GetBuddyCombatLogic())
		{
			pLogic->ScriptDisableInformationalCallouts(pNpc);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-request-informational-callout ((buddy symbol) (target symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-request-informational-callout", DcNpcBuddyRequestInformationalCallout)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	if (Npc* pBuddy = args.NextNpc())
	{
		if (Npc* pTarget = args.NextNpc())
		{
			if (BuddyCombatLogic* pLogic = pBuddy->GetBuddyCombatLogic())
			{
				pLogic->ScriptRequestInformationalCallout(pTarget);
			}
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-notify-disengaged-melee ((buddy symbol) (target symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-notify-disengaged-melee", DcNpcBuddyNotifyDisengagedMelee)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pBuddy	 = args.NextNpc();
	Npc* pTarget = args.NextNpc();

	if (pBuddy && pTarget)
	{
		if (BuddyCombatLogic* pLogic = pBuddy->GetBuddyCombatLogic())
		{
			pLogic->ScriptNotifyDisengagedMelee(pBuddy);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-notify-failed-enemy-grab ((buddy symbol) (enemy symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-notify-failed-enemy-grab", DcNpcBuddyNotifyBuddyFailedEnemyGrab)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pBuddy	 = args.NextNpc();
	Npc* pEnemy = args.NextNpc();

	if (pBuddy && pEnemy)
	{
		if (BuddyCombatLogic* pLogic = pBuddy->GetBuddyCombatLogic())
		{
			pLogic->NotifyEnemyGrabFailed(pBuddy, pEnemy);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-clear-informational-callout-request ((buddy symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-clear-informational-callout-request", DcNpcBuddyClearInformationalCalloutRequest)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pBuddy = args.NextNpc())
	{
		if (BuddyCombatLogic* pLogic = pBuddy->GetBuddyCombatLogic())
		{
			pLogic->ScriptCancelInformationalCalloutRequest();
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
;; if the player grabs 'player-victim' in stealth, buddy 'buddy' can try to stealth-kill 'buddy-victim'
;; DO NOT USE if you cannot guarantee (e.g. by scripting enemies) that this is safe.
;; NOTE that this is NOT commutative; calling this function with player-victim A and buddy-victim B
;; does not imply unleashing for player-victim B and buddy-victim A.
(define-c-function npc-buddy-unleash-stealth-kill/f ((buddy symbol) (player-victim symbol) (buddy-victim symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-unleash-stealth-kill/f", DcNpcBuddyUnleashStealthKillF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	Npc* pBuddy = args.NextNpc();
	Npc* pPlayerVictim = args.NextNpc();
	Npc* pBuddyVictim = args.NextNpc();

	AiLogScript(pBuddy, "npc-buddy-unleash-stealth-kill/f");
	if (pBuddy && pBuddy->IsBuddyNpc() && pPlayerVictim && pBuddyVictim)
	{
		if (BuddyCombatLogic* pLogic = pBuddy->GetBuddyCombatLogic())
		{
			pLogic->AddScriptedStealthKillPair(pBuddy, pBuddyVictim, pPlayerVictim);
		}
	}

	return ScriptValue(0);
}

static void DcBuddyLookAimNotifyLookAtPointHelper(Npc* pBuddyNpc, Point ptWs, float minDur, float maxDur, bool restrictToLookCone, bool instant)
{
	AiLogScript(pBuddyNpc, "npc-buddy-look-aim-notify-look-at-point");

	if (pBuddyNpc && pBuddyNpc->IsKindOf(g_type_Buddy))
	{
		if (BuddyLookAtLogic* const pLal = pBuddyNpc->GetBuddyLookAtLogic())
		{
			pLal->NotifyLookAtPoint(
				pBuddyNpc,
				ptWs,
				Seconds(RandomFloatRange(1.1f, 1.4f)),
				BuddyLookAtLogic::kLookPriorityNormal,
				restrictToLookCone ? BuddyLookAtLogic::kLookConePoint : BuddyLookAtLogic::kLookConeNone,
				instant);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-look-aim-notify-look-at-point ((buddy-npc symbol) (point point) (only-if-within-look-cone boolean (:default #f)) (immediate boolean (:default #f)))
	none)
*/
SCRIPT_FUNC("npc-buddy-look-aim-notify-look-at-point", DcNpcBuddyLookAimNotifyLookAtPoint)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 4);

	Npc* pBuddyNpc = args.NextNpc();
	const Point ptWs = args.NextPoint();
	const bool restrictToLookCone = args.NextBoolean();
	const bool instant = args.NextBoolean();

	DcBuddyLookAimNotifyLookAtPointHelper(pBuddyNpc, ptWs, 0.8f, 1.1f, restrictToLookCone, instant);

	return ScriptValue(0);
}

/*
(define-c-function npc-buddy-look-aim-notify-look-at-point-dur ((buddy-npc symbol) (point point) (min-dur float) (max-dur float) (only-if-within-look-cone boolean (:default #f)) (immediate boolean (:default #f)))
	none)
*/
SCRIPT_FUNC("npc-buddy-look-aim-notify-look-at-point-dur", DcNpcBuddyLookAimNotifyLookAtPointDur)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 6);

	Npc* pBuddyNpc = args.NextNpc();
	const Point ptWs = args.NextPoint();
	const float minDur = args.NextFloat();
	const float maxDur = args.NextFloat();
	const bool restrictToLookCone = args.NextBoolean();
	const bool instant = args.NextBoolean();

	DcBuddyLookAimNotifyLookAtPointHelper(pBuddyNpc, ptWs, minDur, maxDur, restrictToLookCone, instant);

	return ScriptValue(0);
}

static void DcBuddyLookAimNotifyLookAtPlayerHelper(Npc* pBuddyNpc, float minDur, float maxDur, bool restrictToLookCone, bool instant)
{
	AiLogScript(pBuddyNpc, "npc-buddy-look-aim-notify-look-at-player");

	if (pBuddyNpc && GetPlayer())
	{
		if (BuddyLookAtLogic* const pLal = pBuddyNpc->GetBuddyLookAtLogic())
		{
			pLal->NotifyLookAtPlayer(
				pBuddyNpc,
				Seconds(RandomFloatRange(minDur, maxDur)),
				BuddyLookAtLogic::kLookPriorityNormal,
				restrictToLookCone ? BuddyLookAtLogic::kLookConePlayer : BuddyLookAtLogic::kLookConeNone,
				instant);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-look-aim-notify-look-at-player ((buddy-npc symbol) (only-if-within-look-cone boolean (:default #f)) (immediate boolean (:default #f)))
	none)
*/
SCRIPT_FUNC("npc-buddy-look-aim-notify-look-at-player", DcNpcBuddyLookAimNotifyLookAtPlayer)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	Npc* pBuddyNpc = args.NextNpc();
	const bool restrictToLookCone = args.NextBoolean();
	const bool instant = args.NextBoolean();

	DcBuddyLookAimNotifyLookAtPlayerHelper(pBuddyNpc, 1.1f, 1.4f, restrictToLookCone, instant);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-look-aim-notify-look-at-player-dur ((buddy-npc symbol) (min-dur float) (max-dur float) (only-if-within-look-cone boolean (:default #f)) (immediate boolean (:default #f)))
	none)
*/
SCRIPT_FUNC("npc-buddy-look-aim-notify-look-at-player-dur", DcNpcBuddyLookAimNotifyLookAtPlayerDur)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 5);

	Npc* pBuddyNpc = args.NextNpc();
	const float minDur = args.NextFloat();
	const float maxDur = args.NextFloat();
	const bool restrictToLookCone = args.NextBoolean();
	const bool instant = args.NextBoolean();

	DcBuddyLookAimNotifyLookAtPlayerHelper(pBuddyNpc, minDur, maxDur, restrictToLookCone, instant);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-request-conversation-follow/f ((buddy-npc symbol) (follow-settings-id symbol) (immediate boolean :default #f))
	none)
*/
SCRIPT_FUNC("npc-buddy-request-conversation-follow/f", DcNpcBuddyRequestConversationFollowF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	Npc* pBuddyNpc = args.NextNpc();
	StringId64 followSettingsId = args.NextStringId();
	bool immediate = args.NextBoolean();

	AiLogScript(pBuddyNpc, "npc-buddy-request-conversation-follow/f");

	if (pBuddyNpc && pBuddyNpc->IsKindOf(g_type_Buddy) && followSettingsId != INVALID_STRING_ID_64)
	{
		if (BuddyCombatLogic* pLogic = pBuddyNpc->GetBuddyCombatLogic())
		{
			pLogic->RequestConversationFollow(followSettingsId, immediate);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-request-throw-at-npc ((buddy-npc symbol) (target-npc symbol) (item-id symbol) (speed float (:default 24.0)) (track-target boolean (:default #t)) (force-scripted-takeover boolean (:default #f)))
	none)
*/
SCRIPT_FUNC("npc-buddy-request-throw-at-npc", DcNpcBuddyRequestThrowAtNpc)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 6);

	Npc* pBuddyNpc = args.NextNpc();
	Npc* pTargetNpc = args.NextNpc();
	StringId64 itemId = args.NextStringId();
	float speed = args.NextFloat();
	bool trackTarget = args.NextBoolean();
	bool forceScriptedTakeover = args.NextBoolean();

	AiLogScript(pBuddyNpc, "npc-buddy-request-throw-at-npc");

	if (pBuddyNpc && pBuddyNpc->IsKindOf(g_type_Buddy) && pTargetNpc && itemId != INVALID_STRING_ID_64 && speed > 10.0f && speed < 30.0f)
	{
		if (BuddyCombatLogic* pLogic = pBuddyNpc->GetBuddyCombatLogic())
		{
			BehaviorBuddyThrowParams params;
			params.m_itemId = itemId;
			params.m_trackTarget = trackTarget;
			params.m_bIsPoint = false;
			params.m_hTargetNpc = pTargetNpc;
			params.m_targetWs = kInvalidPoint;
			params.m_speed = speed;
			params.m_scripted = true;
			params.m_forceScriptedTakeover = forceScriptedTakeover;

			pLogic->SetScriptedThrowRequest(params);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-request-throw-at-point ((buddy-npc symbol) (target point) (item-id symbol) (speed float (:default 24.0)) (force-scripted-takeover boolean (:default #f)))
	none)
*/
SCRIPT_FUNC("npc-buddy-request-throw-at-point", DcNpcBuddyRequestThrowAtPoint)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 5);

	Npc* pBuddyNpc = args.NextNpc();
	Point targetWs = args.NextPoint();
	StringId64 itemId = args.NextStringId();
	float speed = args.NextFloat();
	bool forceScriptedTakeover = args.NextBoolean();

	AiLogScript(pBuddyNpc, "npc-buddy-request-throw-at-point");

	if (pBuddyNpc && pBuddyNpc->IsKindOf(g_type_Buddy) && IsReasonable(targetWs) && itemId != INVALID_STRING_ID_64 && speed > 10.0f && speed < 30.0f)
	{
		if (BuddyCombatLogic* pLogic = pBuddyNpc->GetBuddyCombatLogic())
		{
			BehaviorBuddyThrowParams params;
			params.m_itemId = itemId;
			params.m_bIsPoint = true;
			params.m_targetWs = targetWs;
			params.m_speed = speed;
			params.m_scripted = true;
			params.m_forceScriptedTakeover = forceScriptedTakeover;

			pLogic->SetScriptedThrowRequest(params);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-clear-throw-request ((source-npc symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-clear-throw-request", DcNpcBuddyCancelRequestedThrow)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pBuddyNpc = args.NextNpc();

	AiLogScript(pBuddyNpc, "npc-buddy-clear-throw-request");
	if (pBuddyNpc && pBuddyNpc->IsKindOf(g_type_Buddy))
	{
		if (BuddyCombatLogic* pLogic = pBuddyNpc->GetBuddyCombatLogic())
		{
			pLogic->ClearScriptedThrowRequest();
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-remove-kill-token ((buddy-npc symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-remove-kill-token", DcNpcBuddyRemoveKillToken)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pBuddy = args.NextNpc();

	AiLogScript(pBuddy, "npc-buddy-remove-kill-token");
	if (pBuddy)
	{
		if (BuddyCombatLogic* pLogic = pBuddy->GetBuddyCombatLogic())
		{
			pLogic->RemoveKillToken(pBuddy);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-remove-kill-token ((buddy-npc symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-give-kill-token", DcNpcBuddyGrantKillToken)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pBuddy = args.NextNpc();

	AiLogScript(pBuddy, "npc-buddy-give-kill-token");
	if (pBuddy)
	{
		if (BuddyCombatLogic* pLogic = pBuddy->GetBuddyCombatLogic())
		{
			pLogic->GiveKillToken(pBuddy);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-notify-melee-kill ((buddy-npc symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-notify-melee-kill", DcNpcBuddyNotifyMeleeKill)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc *const pBuddy = args.NextNpc();
	AiLogScript(pBuddy, "npc-buddy-notify-melee-kill");

	if (pBuddy)
	{
		if (BuddyCombatLogic *const pLogic = pBuddy->GetBuddyCombatLogic())
		{
			pLogic->NotifyBuddyMeleeKill(pBuddy);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-buddy-has-kill-token? ((buddy-npc symbol))
	none)
*/
SCRIPT_FUNC("npc-buddy-has-kill-token?", DcNpcBuddyHasKillTokenP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pBuddy = args.NextNpc();

	//AiLogScript(pBuddy, "npc-buddy-has-kill-token?");
	if (pBuddy)
	{
		if (BuddyCombatLogic* pLogic = pBuddy->GetBuddyCombatLogic())
		{
			return ScriptValue(pLogic->HasKillToken(pBuddy));
		}
	}

	return ScriptValue(false);
}

/////// --------------------------------------------------------------------------------------------------------------- ///
/*
	(defenum npc-script-mode (
		ambient-only			;; script commands will only override ambient-level skills like idle and patrol, if a
   combat skill wishes to take over then the script skill gets ejected complete-control		;; script commands override
   both ambient and combat level skills, effectively taking complete control of a character for the duration
	))

	(defenum npc-script-resume-mode (
		resume-on-complete		;; the script skill will exit and resume normal AI skills once the last script command
   finishes wait-on-complete		;; the script skill will persist and stay idling, preventing any lower priority
   behaviors from taking over until the npc is told to exit
	))

	(define-c-function npc-set-script-mode ((npc-name symbol) (npc-script-mode mode) (npc-script-resume-mode
   resume-mode)) none)
*/
SCRIPT_FUNC("npc-set-script-mode", DcNpcSetScriptMode)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	const DC::NpcScriptMode scriptMode		 = args.NextI32();
	const DC::NpcScriptResumeMode resumeMode = args.NextI32();

	if (!pNpc)
		return ScriptValue(0);

	SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();
	const U32 trackIndex = pTrackInst ? pTrackInst->GetTrackIndex() : 0;

	const bool waitOnComplete = resumeMode == DC::kNpcScriptResumeModeWaitOnComplete;
	switch (scriptMode)
	{
	case DC::kNpcScriptModeCompleteControl:
		pNpc->ScriptRequestDisableAi(waitOnComplete, /*shouldStopPerception=*/false, pGroupInst, trackIndex);
		break;
	case DC::kNpcScriptModeCompleteControlNoPerception:
		pNpc->ScriptRequestDisableAi(waitOnComplete, /*shouldStopPerception=*/true, pGroupInst, trackIndex);
		break;

	case DC::kNpcScriptModeAmbientOnly:
		pNpc->ScriptRequestEnableAi(waitOnComplete, false, pGroupInst, trackIndex);
		break;

	default:
		args.MsgScriptError("invalid script mode (%d) passed to npc '%s'\n", scriptMode, pNpc->GetName());
		break;
	}

	AiLogScript(pNpc,
				"npc-set-script-mode %s/%s",
				GetDcEnumName(SID("*npc-script-mode*"), scriptMode),
				GetDcEnumName(SID("*npc-script-resume-mode*"), resumeMode));

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-script-mode", DcNpcGetScriptMode)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	const Npc* pNpc = args.NextNpc();
	if (!pNpc)
	{
		args.MsgScriptError("npc-get-script-mode failed (NPC invalid)\n");
		return ScriptValue(DC::kNpcScriptModeAmbientOnly);
	}
	bool isAiDisabled		= pNpc->IsAiDisabled();
	bool perceptionDisabled = pNpc->GetKnowledge()->IsPerceptionDisabled();

	if (isAiDisabled)
	{
		if (perceptionDisabled)
			return ScriptValue(DC::kNpcScriptModeCompleteControlNoPerception);
		else
			return ScriptValue(DC::kNpcScriptModeCompleteControl);
	}
	else
	{
		return ScriptValue(DC::kNpcScriptModeAmbientOnly);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-enable-perception", DcNpcEnablePerception)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		pNpc->GetKnowledge()->SetDisablePerception(false);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-disable-perception", DcNpcDisablePerception)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		pNpc->GetKnowledge()->SetDisablePerception(true);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-perception-disabled?", DcNpcIsPerceptionDisabledP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	const Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		return ScriptValue(pNpc->GetKnowledge()->IsPerceptionDisabled());
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-archetype-id", DcNpcGetArchetypeId)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	const Npc* pNpc		   = args.NextNpc();
	StringId64 archetypeId = INVALID_STRING_ID_64;
	if (pNpc)
	{
		archetypeId = pNpc->GetArchetype().GetNameId();
	}

	return ScriptValue(archetypeId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-character-class", DcNpcGetCharacterClass)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const NdGameObject* pGo = args.NextGameObject();
	if (pGo)
	{
		if (const Npc* pNpc = Npc::FromProcess(pGo))
			return ScriptValue(pNpc->GetArchetype().GetInfo()->m_characterClass);
		else if (const ProcessRagdoll* pRagdoll = ProcessRagdoll::FromProcess(pGo))
			return ScriptValue(pRagdoll->GetNpcCharacterClass());
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-archetype", DcNpcGetArchetype)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();
	const DC::AiArchetype* pArchetype = nullptr;
	if (pNpc)
	{
		pArchetype = pNpc->GetArchetype().GetInfo();
	}

	if (!pArchetype)
	{
		pArchetype = ScriptManager::LookupInModule<DC::AiArchetype>(SID("*null-ai-archetype*"), SID("ai-archetypes"));
		GAMEPLAY_ASSERTF(pArchetype, ("*null-ai-archetype* not found in ai-archetypes.bin"));
	}

	return ScriptValue(pArchetype);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-clear-request-movement", DcNpcClearRequestMovement)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		pNpc->GetAiMoveRequestSlot()->ClearRequest();
		pNpc->GetAiMoveRequestSlot()->m_blame = GetCurrentScriptSourceLocation();

		AiLogScript(pNpc, "npc-clear-request-movement");
		return ScriptValue(true);
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-request-hold", DcNpcRequestHold)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc	   = args.NextNpc();
	F32 holdRadius = args.NextFloat();

	if (pNpc)
	{
		AiHoldRequest request(holdRadius);
		pNpc->GetAiMoveRequestSlot()->SetRequest(request);
		pNpc->GetAiMoveRequestSlot()->m_blame = GetCurrentScriptSourceLocation();

		AiLogScript(pNpc, "npc-request-hold");
		return ScriptValue(false);
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-set-combat-follow-group
		(
			(npc-name		symbol)
			(group-id		symbol)
		)
		none
	)
*/
SCRIPT_FUNC("npc-set-combat-follow-group", DcNpcSetCombatFollowGroup)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	StringId64 groupId = args.NextStringId();

	if (pNpc && groupId != INVALID_STRING_ID_64)
	{
		if (pNpc->GetAiMoveRequestSlot())
		{
			AiFollowRequest* pFollowRequest = const_cast<AiFollowRequest*>(pNpc->GetAiMoveRequestSlot()
																			   ->GetFollowRequest());
			if (pFollowRequest)
			{
				pFollowRequest->m_groupId = groupId;
				return ScriptValue(true);
			}
			else
			{
				args.MsgScriptError("NPC not currently in a follow request.\n");
			}
		}
		else
		{
			args.MsgScriptError("Unable to get follow request slot.\n");
		}
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void RequestWaitHelper(Npc* pNpc, ScriptValue* argv, const char* trackName)
{
	AI_ASSERT(pNpc);

	SsTrackInstance* pTrackInst		   = GetJobSsContext().GetTrackInstance();
	SsTrackGroupInstance* pGroupInst   = GetJobSsContext().GetGroupInstance();
	SsTrackGroupProcess* pGroupProcess = pGroupInst ? pGroupInst->GetTrackGroupProcess() : nullptr;

	if (pTrackInst && pGroupInst && pGroupProcess)
	{
		if (pNpc->GetAiMoveRequestSlot()->ListenForNpcReachedRequestedState(pNpc,
																			*pGroupProcess,
																			pTrackInst->GetTrackIndex()))
		{
			pGroupInst->WaitForTrackDone(trackName);
			ScriptContinuation::Suspend(argv);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void RequestFollowHelper(ScriptArgIterator& args, ScriptValue* argv, bool wait)
{
	Npc* pNpc = args.NextNpc();
	const Character* pLeader = args.NextCharacter();

	if (pNpc && pLeader)
	{
		const DC::AiAmbientMode mode = args.NextI32();
		const StringId64 settingsId  = args.NextStringId();
		const StringId64 regionId	 = args.NextStringId();

		const AiFollowRequest request(pLeader, settingsId, mode, regionId);

		pNpc->GetAiMoveRequestSlot()->SetRequest(request);
		pNpc->GetAiMoveRequestSlot()->m_blame = GetCurrentScriptSourceLocation();

		if (wait)
		{
			RequestWaitHelper(pNpc, argv, "wait-npc-request-follow");
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void RequestSplineLeadHelper(ScriptArgIterator& args, ScriptValue* argv, bool wait)
{
	Npc* pNpc = args.NextNpc();
	const StringId64 splineId	= args.NextStringId();
	const Character* pCharacter = args.NextCharacter();
	const StringId64 settingsId = args.NextStringId();
	const DC::AiAmbientMode ambientMode = (DC::AiAmbientMode)args.NextI32();
	const bool longGun = args.NextBoolean();

	if (pNpc && pCharacter && splineId != INVALID_STRING_ID_64)
	{
		const CatmullRom* pSpline = g_splineManager.FindByName(splineId);
		if (!pSpline)
		{
			SetColor(kMsgConPersistent, kColorRed);
			MsgConPersistent("Can't find lead spline'%s'\n", DevKitOnly_StringIdToString(splineId));
			SetColor(kMsgConPersistent, kColorWhite);
		}

		const AiLeadRequest request(pCharacter, splineId, settingsId, ambientMode, longGun);

		pNpc->GetAiMoveRequestSlot()->SetRequest(request);
		pNpc->GetAiMoveRequestSlot()->m_blame = GetCurrentScriptSourceLocation();

		if (wait)
		{
			RequestWaitHelper(pNpc, argv, "wait-npc-request-lead-spline");
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void RequestExploreHelper(ScriptArgIterator& args, ScriptValue* argv, bool wait)
{
	Npc* pNpc = args.NextNpc();
	const StringId64 regionId		= args.NextStringId();
	const StringId64 groupId		= args.NextStringId();
	const StringId64 postSelectorId = args.NextStringId();
	const float maxDistance			= args.NextFloat();
	const DC::AiAmbientMode ambientMode = (DC::AiAmbientMode)args.NextI32();
	const StringId64 followSettingsId = args.NextStringId();
	const bool longGun = args.NextBoolean();

	if (pNpc)
	{
		const AiExploreRequest request(postSelectorId, regionId, maxDistance, groupId, ambientMode, longGun, followSettingsId, wait);

		pNpc->GetAiMoveRequestSlot()->SetRequest(request);
		pNpc->GetAiMoveRequestSlot()->m_blame = GetCurrentScriptSourceLocation();

		if (wait)
		{
			RequestWaitHelper(pNpc, argv, "wait-npc-request-explore");
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-request-explore", DcNpcRequestExplore)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 5);
	AiLogScript(args.GetNpc(), "npc-request-explore");
	RequestExploreHelper(args, argv, false);
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("wait-npc-request-explore", DcWaitNpcRequestExplore)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 5);
	AiLogScript(args.GetNpc(), "wait-npc-request-explore");
	RequestExploreHelper(args, argv, true);
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-request-follow", DcNpcRequestFollow)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	AiLogScript(pNpc, "npc-request-follow");
	if (pNpc)
	{
		CharacterHandle hChar = args.NextCharacter();
		const DC::AiAmbientMode ambientMode = args.NextI32();
		const StringId64 settingsId = args.NextStringId();
		const StringId64 regionId = args.NextStringId();
		const bool longGun = args.NextBoolean();
		const AiFollowRequest request(hChar, settingsId, ambientMode, regionId, longGun);

		pNpc->GetAiMoveRequestSlot()->SetRequest(request);

		pNpc->GetAiMoveRequestSlot()->m_blame = GetCurrentScriptSourceLocation();
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-request-stick-drive", DcNpcRequestStickDrive)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 4);
	Npc* pNpc = args.NextNpc();
	NdGameObjectHandle hRefrenceGo = args.NextGameObjectHandle();
	const Point finalDest = args.NextPoint();
	const Point desiredPos = args.NextPoint();
	const StringId64 settingsId = args.NextStringId();

	if (pNpc)
	{
		if (!hRefrenceGo.HandleValid())
		{
			MsgConErr("called npc-request-stick-drive on invalid reference object!");
			return ScriptValue(false);
		}

		AiStickDriveRequest request(hRefrenceGo, finalDest);
		request.m_desiredPosWs = desiredPos;
		request.m_settingsId = settingsId;

		if (FALSE_IN_FINAL_BUILD(pNpc->GetAiMoveRequestSlot()->GetStickDriveRequest()))
		{
			AiLogScript(pNpc, "npc-request-stick-drive");
		}

		pNpc->GetAiMoveRequestSlot()->SetRequest(request);
		pNpc->GetAiMoveRequestSlot()->m_blame = GetCurrentScriptSourceLocation();
	}
	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-dialog-look-settings", DcNpcSetDialogLookSettings)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	StringId64 settingsId = args.NextStringId();
	if (pNpc)
	{
		pNpc->m_dialogLook.SetSettingsId(settingsId);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-current-lead-settings", DcNpcGetCurrentLeadSettings)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (const Npc* const pNpc = args.NextNpc())
	{
		if (const AiMoveRequestSlot* const pReqSlot = pNpc->GetAiMoveRequestSlot())
		{
			if (const AiLeadRequest* const pLeadReq = pReqSlot->GetLeadRequest())
			{
				return ScriptValue(pLeadReq->m_settingsId);
			}
		}
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-request-lead-spline", DcNpcRequestSplineLead)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 4);
	AiLogScript(args.GetNpc(), "npc-request-lead-spline");
	RequestSplineLeadHelper(args, argv, false);
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("wait-npc-request-lead-spline", DcWaitNpcRequestSplineLead)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 4);
	AiLogScript(args.GetNpc(), "wait-npc-request-lead-spline");
	RequestSplineLeadHelper(args, argv, true);
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-request-dont-attack", DcNpcRequestDontAttack)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-request-dont-attack");
		pNpc->GetAiAttackRequestSlot()->SetRequest(AiDontAttackRequest());
		pNpc->GetAiAttackRequestSlot()->m_blame = GetCurrentScriptSourceLocation();
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-request-always-attack", DcNpcRequestAlwaysAttack)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-request-always-attack");
		pNpc->GetAiAttackRequestSlot()->SetRequest(AiAlwaysAttackRequest());
		pNpc->GetAiAttackRequestSlot()->m_blame = GetCurrentScriptSourceLocation();
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-request-default-attack", DcNpcRequestDefaultAttack)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-request-default-attack");
		pNpc->GetAiAttackRequestSlot()->ClearRequest();
		pNpc->GetAiAttackRequestSlot()->m_blame = GetCurrentScriptSourceLocation();
		return ScriptValue(false);
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-fire-once-at-position ((npc-name symbol) (target-pos bound-frame))
		none)
*/
SCRIPT_FUNC("npc-fire-once-at-position", DcNpcFireOnceAtPosition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	const BoundFrame* pBf = args.NextBoundFrame();
	const U32 damage	  = args.NextU32();
	const U32 shotType	  = args.NextU32();

	if (!pNpc || !pBf)
		return ScriptValue(false);

	AiLogScript(pNpc, "npc-fire-once-at-position");
	SendEvent(SID("fire-once-at-position"),
			  pNpc,
			  BoxedValue(pBf->GetTranslationWs()),
			  BoxedValue(damage),
			  BoxedValue(shotType));

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-allow-combat-remarks", DcNpcAllowCombatRemarks)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	bool val  = args.NextBoolean();
	if (pNpc)
	{
		AiLogScript(pNpc, "npc-allow-combat-remarks %s", DcTrueFalse(val));
		pNpc->SetCombatRemarksEnabled(val);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-faction-allow-combat-remarks", DcNpcFactionAllowCombatRemarks)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	DC::DcFaction dcFaction = args.NextU32();
	bool val = args.NextBoolean();

	FactionId faction = DcFactionToFactionId(dcFaction);
	if (g_factionMgr.IsValid(faction))
	{
		EngineComponents::GetDialogManager()->SetCombatRemarksEnabled(faction, val);
	}
	else
	{
		args.MsgScriptError("Invalid faction %d (%s) -- did you pass a SID by accident?\n",
							(I32)dcFaction,
							DevKitOnly_StringIdToStringOrHex(StringId64(dcFaction)));
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-buddy-allow-item-remarks", DcNpcBuddyAllowItemRemarks)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	bool val  = args.NextBoolean();
	if (pNpc)
	{
		// AiLogScript(pNpc, "npc-buddy-allow-item-remarks %s", DcTrueFalse(val));
		// pNpc->SetItemRemarksEnabled(val); // item remarks were cut from T1, so we don't have them in BIG4 either (yet)
		AiLogScript(pNpc, "WARNING: item remarks are not yet supported");
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-in-cover? ((npc-name symbol))
	boolean)
*/
SCRIPT_FUNC("npc-in-cover?", DcNpcInCoverP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(false);

	if (pNpc->IsNavigationInProgress())
		return ScriptValue(false);

	const ActionPack* pAp = pNpc->GetEnteredActionPack();
	if (!pAp || (pAp->GetType() != ActionPack::kCoverActionPack))
		return ScriptValue(false);

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-dead-and-not-charred?", DcNpcDeadAndNotCharredP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pGob = args.NextGameObject();

	bool result = false;
#if 0 // T2-TODO
	if (const Npc* pNpc = Npc::FromProcess(pGob))
	{
		result =  pNpc->IsDead() && !pNpc->m_isCharred;
	}
	else if (const ProcessRagdoll* pRagDoll = ProcessRagdoll::FromProcess(pGob))
	{
		result = !pRagDoll->IsCharred();
	}
#endif
	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-charred?", DcNpcCharredP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pGob = args.NextGameObject();

	bool result = false;
#if 0 // T2-TODO
	if (const Npc* pNpc = Npc::FromProcess(pGob))
	{
		result =  pNpc->IsDead() && pNpc->m_isCharred;
	}
	else if (const ProcessRagdoll* pRagDoll = ProcessRagdoll::FromProcess(pGob))
	{
		result = pRagDoll->IsCharred();
	}
#endif
	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///

SCRIPT_REGISTER_FUNC_AUTO_PARAMS(float,
								 DcNpcGetPercentOnscreen,
								 (NdGameObject * pNpc, DC::TargetableSpotMask spotMask),
								 "npc-get-percent-onscreen")
{
	if (!pNpc)
		return 0.f;

	const Player* pPlayer = GetPlayer();
	if (!pPlayer)
		return 0.f;

	const Targetable* pTargetable = pNpc->GetTargetable();
	if (!pTargetable)
		return 0.f;

	const TargetInfo* pTargetInfo = pPlayer->m_pTargetingMgr->GetTargetInfo(pTargetable);
	if (!pTargetInfo)
		return 0.f;

	if (spotMask == ~0U)
		spotMask = DC::kTargetableSpotMaskCount - 1; // :|

	const U32 numSpots = Min(pTargetable->GetNumValidSpots(), (U32)CountSetBits(spotMask));

	if (numSpots == 0)
		return 0.f;

	U32 numVisible = 0;
	for (U32 spotNum = 0; spotNum < pTargetInfo->m_numVisibleSpots; ++spotNum)
	{
		if (!pTargetInfo->m_spotsOnscreen[spotNum])
			continue;

		if ((spotMask & (1 << pTargetInfo->m_spots[spotNum])) == 0)
			continue;

		numVisible++;
	}

	const F32 percentVisible = (F32)numVisible / (F32)numSpots;

	return percentVisible;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-force-lod", DcNpcForceLod0)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	DC::AnimLod lod = (DC::AnimLod)args.NextI32();

	if (pNpc)
	{
		pNpc->ForceLodOneFrame(lod);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-lod", DcNpcGetLOD)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		return ScriptValue(pNpc->GetAnimLod());
	}
	return ScriptValue(DC::kAnimLodNormal);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("debug-only-use-flank-path-cost-func", DcDebugOnlyUseFlankPathCostFunc)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		pNpc->OverridePathCost(Npc::kPathCostFlank);
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("debug-only-clear-flank-path-cost-func", DcDebugOnlyClearFlankPathCostFunc)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		pNpc->ClearOverridePathCost();
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-time-since-offscreen", DcNpcGetTimeSinceOffscreen)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	F32 result = 0.0f;
	if (pNpc)
	{
		result = pNpc->GetTimeSince(pNpc->GetLastOnScreenTime(0)).ToSeconds();
	}

	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
;; for move commands already in progress this allows you to change what
;; motion type is requested of the locomotion system
(define-c-function npc-set-requested-motion-type
	(
		(npc-name		symbol)
		(motion-type	npc-motion-type)
	)
	boolean
)
*/
SCRIPT_FUNC("npc-set-requested-motion-type", DcNpcSetRequestedMotionType)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const DC::NpcMotionType dcMotionType = args.NextI32();
	const MotionType motionType = DcMotionTypeToGame(dcMotionType);

	if (pNpc)
	{

		Npc::ScriptCommandInfo& cmd = pNpc->GetScriptCommandInfo();

		if (cmd.m_command.m_moveArgs.m_motionType != motionType)
		{
			AiLogScript(pNpc, "npc-set-requested-motion-type %s", GetMotionTypeName(motionType));
			cmd.m_command.m_moveArgs.m_motionType = motionType;
			cmd.m_command.m_arrivalMotionType	  = motionType;
			pNpc->SetRequestedMotionType(motionType);
		}

		return ScriptValue(true);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
;; for move commands already in progress this allows you to change what
;; motion type is requested of the locomotion system during arrival
(define-c-function npc-set-arrival-motion-type
	(
		(npc-name		symbol)
		(motion-type	npc-motion-type)
	)
	boolean
)
*/
SCRIPT_FUNC("npc-set-arrival-motion-type", DcNpcSetArrivalMotionType)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const DC::NpcMotionType dcMotionType = args.NextI32();
	const MotionType motionType = DcMotionTypeToGame(dcMotionType);

	if (pNpc)
	{
		Npc::ScriptCommandInfo& cmd = pNpc->GetScriptCommandInfo();

		if (cmd.m_command.m_arrivalMotionType != motionType)
		{
			cmd.m_command.m_arrivalMotionType = motionType;
			AiLogScript(pNpc, "npc-set-arrival-motion-type %s", GetMotionTypeName(motionType));
		}

		return ScriptValue(true);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
;; for move commands already in progress this allows you to change the arrival
;; distance
(define-c-function npc-set-arrival-distance
	(
		(npc-name	symbol)
		(float		arrival-distance)
	)
	boolean
)
*/
SCRIPT_FUNC("npc-set-arrival-distance", DcNpcSetArrivalDistance)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc		  = args.NextNpc();
	float arrivalDist = args.NextFloat();

	if (pNpc)
	{
		Npc::ScriptCommandInfo& cmd = pNpc->GetScriptCommandInfo();
		if (cmd.m_command.m_arriveDistance != arrivalDist)
		{
			AiLogScript(pNpc, "npc-set-arrival-distance");
			cmd.m_command.m_arriveDistance = arrivalDist;
		}
		return ScriptValue(true);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
;; force scripted move-to's to push the player
(define-c-function npc-set-force-player-push
	(
		(npc-name	symbol)
		(enable		boolean)
	)
	none
)
*/
SCRIPT_FUNC("npc-set-force-player-push", DcNpcSetForcePlayerPush)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	Npc* pNpc	= args.NextNpc();
	bool enable = args.NextBoolean();
	float inflateRadius = args.NextFloat();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-set-force-player-push %s", DcTrueFalse(enable));
		pNpc->SetForcePlayerPush(enable, inflateRadius, FILE_LINE_FUNC);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
;; allow/disallow NPCs player evasion
(define-c-function npc-set-player-evade
	(
		(npc-name	symbol)
		(enable		boolean)
	)
	none
)
*/
SCRIPT_FUNC("npc-set-player-evade", DcNpcSetPlayerEvade)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc	= args.NextNpc();
	bool enable = args.NextBoolean();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-set-player-evade %s", DcTrueFalse(enable));
		pNpc->SetPlayerEvade(enable);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function is-idle-performance-allowed? ((npc-name symbol))
	boolean)
*/
SCRIPT_FUNC("npc-is-idle-performance-allowed?", DcNpcIsIdlePerformanceAllowed)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(false);

	const bool isAllowed = pNpc->GetAnimationControllers()->GetLocomotionController()->IsIdlePerformanceAllowed();
	return ScriptValue(isAllowed);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-navigation-in-progress? ((npc-name symbol))
	boolean)
*/
SCRIPT_FUNC("npc-navigation-in-progress?", DcNpcNavigationInProgressP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(false);

	const bool inProgress = pNpc->IsNavigationInProgress();
	return ScriptValue(inProgress);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-in-stealth-vegetation? ((npc-name symbol))
	boolean)
*/
SCRIPT_FUNC("npc-in-stealth-vegetation?", DcNpcInStealthVegetationP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();
	return ScriptValue(pNpc && pNpc->IsInStealthVegetation());
}

SCRIPT_FUNC("npc-in-wade-water?", DcNpcInWadeWater)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();
	return ScriptValue(pNpc && pNpc->GetNavControl()->NavMeshForcesWade());
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-in-tap?", DcNpcInTapP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	bool result = false;
	if (pNpc)
	{
		if (ActionPack* pPack = pNpc->GetEnteredActionPack())
		{
			result = pPack->GetType() == ActionPack::kTraversalActionPack;
		}
	}

	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-reserved-tap?", DcNpcReservedTapP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	bool result = false;
	if (pNpc)
	{
		if (ActionPack* pPack = pNpc->GetReservedActionPack())
		{
			result = pPack->GetType() == ActionPack::kTraversalActionPack;
		}
	}

	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-at-nav-destination?", DcNpcAtNavDestinationP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	const Npc* pNpc	 = args.NextNpc();
	const F32 radius = args.NextFloat();
	if (!pNpc)
		return ScriptValue(false);

	const F32 distSqr = DistSqr(pNpc->GetTranslation(), pNpc->GetDestinationWs());
	return ScriptValue(distSqr < Sqr(radius));
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-nav-destination", DcNpcGetNavDestination)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc	 = args.NextNpc();
	if (!pNpc)
		return ScriptValue(nullptr);

	Point* pPos = NDI_NEW(kAllocSingleGameFrame, kAlign16) Point(pNpc->GetDestinationWs());

	return ScriptValue(pPos);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-entered-ap", DcNpcGetEntereAp)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(INVALID_STRING_ID_64);

	const ActionPack* pPack = pNpc->GetEnteredActionPack();
	if (!pPack)
		return ScriptValue(INVALID_STRING_ID_64);

	return ScriptValue(pPack->GetSpawnerId());
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-get-entered-ap-type ((npc-name symbol))
	symbol)
*/
SCRIPT_FUNC("npc-get-entered-ap-type", DcNpcGetEnteredApType)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(INVALID_STRING_ID_64);

	const ActionPack* pAp = pNpc->GetEnteredActionPack();
	if (!pAp)
		return ScriptValue(INVALID_STRING_ID_64);

	return ScriptValue(pAp->GetDcTypeNameId());
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue NpcScriptSetPrimaryWeapon(Npc* pNpc, const StringId64 weaponName, bool force)
{
	if (!pNpc)
		return ScriptValue(false);

	PropInventory* pPropInventory = pNpc->GetPropInventory();
	if (!pPropInventory)
		return ScriptValue(false);

	AnimationControllers* pAnimControllers = pNpc->GetAnimationControllers();
	IAiWeaponController* pWeaponController = pAnimControllers ? pAnimControllers->GetWeaponController() : nullptr;

	bool res = false;

	if (pWeaponController)
	{
		if (force)
		{
			res = pWeaponController->ForcePrimaryWeapon(weaponName);
		}
		else
		{
			res = pWeaponController->RequestPrimaryWeapon(weaponName);
		}
	}

	return ScriptValue(res);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-get-firearm-name ((npc-name symbol))
	symbol)
*/
SCRIPT_FUNC("npc-get-firearm-name", DcNpcGetFirearmName)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	const Npc* pNpc = args.NextNpc();
	const PropInventory* pPropInv = pNpc ? pNpc->GetPropInventory() : nullptr;
	const U32F numProps = pPropInv ? pPropInv->GetNumProps() : 0;

	StringId64 firearmId = INVALID_STRING_ID_64;

	for (U32F iProp = 0; iProp < numProps; ++iProp)
	{
		const ProcessWeaponBase* pWeapon = ProcessWeaponBase::FromProcess(pPropInv->GetPropObject(iProp));

		if (!pWeapon)
			continue;

		if (nullptr == pWeapon->GetFirearmArtDef())
			continue;

		firearmId = pWeapon->GetWeaponDefId();
		break;
	}

	return ScriptValue(firearmId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-get-melee-weapon-name ((npc-name symbol))
	symbol)
*/
SCRIPT_FUNC("npc-get-melee-weapon-name", DcNpcGetMeleeWeaponName)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	const Npc* pNpc = args.NextNpc();
	const PropInventory* pPropInv = pNpc ? pNpc->GetPropInventory() : nullptr;
	const U32F numProps = pPropInv ? pPropInv->GetNumProps() : 0;

	StringId64 meleeId = INVALID_STRING_ID_64;

	for (U32F iProp = 0; iProp < numProps; ++iProp)
	{
		const ProcessWeaponBase* pWeapon = ProcessWeaponBase::FromProcess(pPropInv->GetPropObject(iProp));

		if (!pWeapon)
			continue;

		if (!pWeapon->IsMeleeWeapon())
			continue;

		meleeId = pWeapon->GetWeaponDefId();
		break;
	}

	return ScriptValue(meleeId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-set-primary-weapon ((npc-name symbol) (primary-weapon-name symbol) (force boolean :default #f))
	boolean)
*/
SCRIPT_FUNC("npc-set-primary-weapon", DcNpcSetPrimaryWeapon)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	const StringId64 weaponName = args.NextStringId();
	const bool force = args.NextBoolean();

	return NpcScriptSetPrimaryWeapon(pNpc, weaponName, force);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-get-weapon-loadout-id ((npc-name symbol))
symbol)
*/
SCRIPT_FUNC("npc-get-weapon-loadout-id", DcNpcGetWeaponLoadout)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		StringId64 weaponLoadoutId = pNpc->GetWeaponLoadoutId();

		return ScriptValue(weaponLoadoutId);
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-is-in-cover? ((npc-name symbol))
	boolean)
*/
SCRIPT_FUNC("npc-is-in-cover?", DcNpcIsInCoverP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	bool result = false;
	if (pNpc)
	{
		if (ActionPack* pAp = pNpc->GetEnteredActionPack())
		{
			result = pAp->GetType() == ActionPack::kCoverActionPack;
		}
	}

	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* (define-c-function npc-enable-dodges ((npc-name symbol) (enable boolean))
 *		none)
 */
SCRIPT_FUNC("npc-enable-dodges", DcNpcEnableDodges)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc	= args.NextNpc();
	bool enable = args.NextBoolean();

	if (pNpc)
		pNpc->EnableDodges(enable);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* (define-c-function "npc-enable-hit-reactions ((npc-name symbol) (enable boolean))
 *		boolean)
 */
SCRIPT_FUNC("npc-enable-hit-reactions", DcNpcEnableHitReactions)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc	= args.NextNpc();
	bool enable = args.NextBoolean();

	if (!pNpc)
		return ScriptValue(false);

	pNpc->EnableHitReactions(enable);

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* (define-c-function npc-change-prop-parent ((npc-name symbol) (prop-id symbol) (attach-id symbol))
 *		boolean)
 */
SCRIPT_FUNC("npc-change-prop-parent", DcNpcChangePropParent)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);
	Npc* pNpc = args.NextNpc();
	const StringId64 propId	  = args.NextStringId();
	const StringId64 attachId = args.NextStringId();

	if (!pNpc)
	{
		return ScriptValue(false);
	}

	if (PropControl* pPropInventory = pNpc->GetPropInventory())
	{
		const I32F propIndex = pPropInventory->FindPropIndexByName(propId);
		if (propIndex >= 0)
		{
			if (NdAttachableObject* pProp = pPropInventory->GetPropObject(propIndex))
			{
				pProp->ChangeAttachPoint(FILE_LINE_FUNC, attachId);
			}
		}
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("all-enemy-npcs-die", DcAllEnemyNpcsDie)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 0);
	bool bDespawn = args.NextBoolean();

	if (!g_netInfo.IsNetActive() || g_netGameManager.IsHost())
	{
		SsVerboseLog(1, "all-enemy-npcs-die");
		EngineComponents::GetNpcManager()->KillAllEnemyNpcs(bDespawn);
	}
	return ScriptValue(1);
	;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-fire-at-object", DcNpcFireAtObject)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 6);

	Npc* pNpc = args.NextNpc();
	NdGameObject* pTargetGo			= args.NextGameObject();
	DC::AiFireMode fireMode			= (DC::AiFireMode)args.NextI32();
	DC::AiAccuracyMode accuracyMode = (DC::AiAccuracyMode)args.NextI32();
	DC::AiFiringFlags firingFlags	= (DC::AiFiringFlags)args.NextU32();
	StringId64 targetAttachPoint	= args.NextStringId();

	if (pNpc)
	{
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();

		pNpc->ScriptCommandFireAtObject(pTargetGo,
										fireMode,
										accuracyMode,
										firingFlags,
										targetAttachPoint,
										pGroupInst,
										false);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-disable-move-performance-poi-type/f", DcNpcDisableMovePerformancePoiType)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	StringId64 type = args.NextStringId();

	if (pNpc)
	{
		pNpc->AddMovePerformanceIgnoreType(type);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-move-performance-exclusive-poi-type/f", DcNpcMovePerformanceExclusivePoiType)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	StringId64 type = args.NextStringId();

	if (pNpc)
	{
		pNpc->AddMovePerformanceExclusiveType(type);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-move-performance-poi", DcNpcGetMovePerformancePoi)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		return ScriptValue(pNpc->GetLastMovePerformancePoiNameId());
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-try-ellie-boss-throw-grenade-skill/f", DcNpcTryEllieBossThrowGrenadeSkill)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		pNpc->SetEllieBossThrowGrenade(true);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-are-throw-probes-clear?", DcNpcAreThrowProbesClearP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		if (pNpc->IsHighGrenadeThrowValid() || pNpc->IsLowGrenadeThrowValid())
			return ScriptValue(true);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-throw-probe-target-point", DcNpcGetThrowProbeTargetPoint)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		Point* pPosition = NDI_NEW(kAllocSingleGameFrame) Point(pNpc->GetGrenadeTargetPos());

		return ScriptValue(pPosition);
	}

	return ScriptValue(nullptr);
}


/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-throw-grenade-at-target", DcNpcThrowGrenadeAtTarget)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst = GetJobSsContext().GetTrackInstance();

		pNpc->ScriptCommandThrowGrenade(pGroupInst,
			pTrackInst ? pTrackInst->GetTrackIndex() : 0,
			kZero,
			true,
			false);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("wait-npc-throw-grenade-at-target", DcWaitNpcThrowGrenadeAtTarget)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst = GetJobSsContext().GetTrackInstance();

		pNpc->ScriptCommandThrowGrenade(pGroupInst, pTrackInst->GetTrackIndex(), kZero, true, true);

		pGroupInst->WaitForTrackDone("npc-throw-grenade-at-target");
		ScriptContinuation::Suspend(args.GetArgArray());
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-throw-grenade-at-locator", DcNpcThrowGrenadeAtLocator)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const BoundFrame* pBoundFrame = args.NextBoundFrame();

	if (pNpc)
	{
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst = GetJobSsContext().GetTrackInstance();

		pNpc->ScriptCommandThrowGrenade(pGroupInst,
										pTrackInst ? pTrackInst->GetTrackIndex() : 0,
										pBoundFrame->GetTranslation(),
										false,
										false);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("wait-npc-throw-grenade-at-locator", DcWaitNpcThrowGrenadeAtLocator)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const BoundFrame* pBoundFrame = args.NextBoundFrame();

	if (pNpc)
	{
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst = GetJobSsContext().GetTrackInstance();

		pNpc->ScriptCommandThrowGrenade(pGroupInst, pTrackInst->GetTrackIndex(), pBoundFrame->GetTranslation(), false, true);

		pGroupInst->WaitForTrackDone("npc-throw-grenade-at-locator");
		ScriptContinuation::Suspend(args.GetArgArray());
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-fire-at-locator", DcNpcFireAtLocator)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 5);

	Npc* pNpc = args.NextNpc();
	const BoundFrame* pBoundFrame	= args.NextBoundFrame();
	DC::AiFireMode fireMode			= (DC::AiFireMode)args.NextI32();
	DC::AiAccuracyMode accuracyMode = (DC::AiAccuracyMode)args.NextI32();
	DC::AiFiringFlags firingFlags	= (DC::AiAccuracyMode)args.NextU32();

	if (pNpc)
	{
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

		pNpc->ScriptCommandFireAtBoundFrame(pBoundFrame,
											fireMode,
											accuracyMode,
											firingFlags,
											pGroupInst,
											pTrackInst->GetTrackIndex(),
											false);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-cancel-fire-command", DcNpcCancelFireCommand)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

		pNpc->ScriptCommandCancel(pGroupInst, pTrackInst->GetTrackIndex(), false);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
// SCRIPT_FUNC("npc-fire-at-target", DcNpcFireAtTarget)
//{
//	GAME_SCRIPT_ARG_ITERATOR(args, 6);
//
//	Npc* pNpc = args.NextNpc();
//	DC::AiFireMode fireMode = (DC::AiFireMode)args.NextI32();
//	DC::AiAccuracyMode accuracyMode = (DC::AiFireMode)args.NextI32();
//
//	if (pNpc)
//	{
//		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
//		SsTrackInstance* pTrackInst = GetJobSsContext().GetTrackInstance();
//
//		pNpc->ScriptCommandFireAtTarget(fireMode, accuracyMode, pGroupInst, pTrackInst->GetTrackIndex(), false);
//	}
//
//	return ScriptValue(0);
//}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("wait-npc-leave-cinematic-action-pack", DcWaitNpcLeaveCinematicAp)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

		pGroupInst->WaitForTrackDone("wait-npc-leave-cinematic-action-pack");
		pNpc->ScriptCommandStopAndStand(pGroupInst, pTrackInst->GetTrackIndex(), true);
		ScriptContinuation::Suspend(argv);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-climb-to-pos
		(
			(npc-name symbol)
			(bf bound-frame)
		)
		none
	)
*/
SCRIPT_FUNC("npc-climb-to-pos", DcNpcClimbToPos)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const BoundFrame* pBoundFrame = args.NextBoundFrame();
	const MotionType motionType	  = DcMotionTypeToGame(args.NextI32());

	if (pNpc && pBoundFrame)
	{
		AiLogScript(pNpc, "npc-climb-to-pos");

		const SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		const SsTrackInstance* pTrackInst	   = GetJobSsContext().GetTrackInstance();
		const U32 trackIndex = pTrackInst ? pTrackInst->GetTrackIndex() : 0;

#if ENABLE_NAV_LEDGES
		NavLocation navLoc = pNpc->AsReachableNavLocationWs(pBoundFrame->GetTranslation(), NavLocation::Type::kNavLedge);
		NavMoveArgs moveArgs(pNpc->GetMotionConfig().m_minimumGoalRadius, motionType);

		pNpc->ScriptCommandMoveToPos(navLoc,
									 GetLocalZ(pBoundFrame->GetRotation()),
									 moveArgs,
									 motionType,
									 true,
									 false,
									 false,
									 pGroupInst,
									 trackIndex,
									 false);
#endif // ENABLE_NAV_LEDGES
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function wait-npc-climb-to-pos
		(
			(npc-name symbol)
			(bf bound-frame)
		)
		none
	)
*/
SCRIPT_FUNC("wait-npc-climb-to-pos", DcWaitNpcClimbToPos)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const BoundFrame* pBoundFrame = args.NextBoundFrame();
	const MotionType motionType	  = DcMotionTypeToGame(args.NextI32());

	if (pNpc && pBoundFrame)
	{
		AiLogScript(pNpc, "wait-npc-climb-to-pos");

		SsInstance* pScriptInst = GetJobSsContext().GetScriptInstance();
		const SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		const SsTrackInstance* pTrackInst	   = GetJobSsContext().GetTrackInstance();
		const U32 trackIndex = pTrackInst ? pTrackInst->GetTrackIndex() : 0;

#if ENABLE_NAV_LEDGES
		NavLocation navLoc = pNpc->AsReachableNavLocationWs(pBoundFrame->GetTranslation(), NavLocation::Type::kNavLedge);
		NavMoveArgs moveArgs(pNpc->GetMotionConfig().m_minimumGoalRadius, motionType);

		pNpc->ScriptCommandMoveToPos(navLoc,
									 GetLocalZ(pBoundFrame->GetRotation()),
									 moveArgs,
									 motionType,
									 true,
									 false,
									 false,
									 pGroupInst,
									 trackIndex,
									 true);
#endif // ENABLE_NAV_LEDGES

		if (pScriptInst && pGroupInst && pTrackInst)
		{
			pGroupInst->WaitForTrackDone("wait-npc-climb-to-pos");
			ScriptContinuation::Suspend(args.GetArgArray());
		}
		else
		{
			args.MsgScriptError("Called outside script context -- will not wait\n");
		}
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-vehicle-stop
		(
			(npc-name symbol)
		)
		none
	)
*/
SCRIPT_FUNC("npc-vehicle-stop", DcNpcVehicleStop)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-vehicle-stop");

		const SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		const SsTrackInstance* pTrackInst	   = GetJobSsContext().GetTrackInstance();
		const U32 trackIndex = pTrackInst ? pTrackInst->GetTrackIndex() : 0;

		pNpc->ScriptCommandVehicleStop(pGroupInst, trackIndex, false);
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-vehicle-shoot
		(
			(npc-name symbol)
		)
		none
	)
*/
SCRIPT_FUNC("npc-vehicle-shoot", DcNpcVehicleShoot)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		AiLogScript(pNpc, "npc-vehicle-shoot");

		const SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		const SsTrackInstance* pTrackInst	   = GetJobSsContext().GetTrackInstance();
		const U32 trackIndex = pTrackInst ? pTrackInst->GetTrackIndex() : 0;

		pNpc->ScriptCommandVehicleShoot(pGroupInst, trackIndex, false);
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function wait-npc-vehicle-ready-jump
		(
			(npc-name symbol)
		)
		none
	)
*/
SCRIPT_FUNC("wait-npc-vehicle-ready-jump", DcWaitNpcVehicleReadyJump)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		AiLogScript(pNpc, "wait-npc-vehicle-ready-jump");

		SsInstance* pScriptInst = GetJobSsContext().GetScriptInstance();
		const SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		const SsTrackInstance* pTrackInst	   = GetJobSsContext().GetTrackInstance();
		const U32 trackIndex = pTrackInst ? pTrackInst->GetTrackIndex() : 0;

		pNpc->ScriptCommandVehicleReadyJump(pGroupInst, trackIndex, true);

		if (pScriptInst && pGroupInst && pTrackInst)
		{
			pGroupInst->WaitForTrackDone("wait-npc-vehicle-ready-jump");
			ScriptContinuation::Suspend(args.GetArgArray());
		}
		else
		{
			args.MsgScriptError("Called outside script context -- will not wait\n");
		}
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function wait-npc-vehicle-jump
		(
			(npc-name symbol)
		)
		none
	)
*/
SCRIPT_FUNC("wait-npc-vehicle-jump", DcWaitNpcVehicleJump)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		AiLogScript(pNpc, "wait-npc-vehicle-jump");

		SsInstance* pScriptInst = GetJobSsContext().GetScriptInstance();
		const SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		const SsTrackInstance* pTrackInst	   = GetJobSsContext().GetTrackInstance();
		const U32 trackIndex = pTrackInst ? pTrackInst->GetTrackIndex() : 0;

		pNpc->ScriptCommandVehicleJump(pGroupInst, trackIndex, true);

		if (pScriptInst && pGroupInst && pTrackInst)
		{
			pGroupInst->WaitForTrackDone("wait-npc-vehicle-jump");
			ScriptContinuation::Suspend(args.GetArgArray());
		}
		else
		{
			args.MsgScriptError("Called outside script context -- will not wait\n");
		}
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-patrol-route", DcNpcSetPatrolRoute)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	if (Npc* pNpc = args.NextNpc())
	{
		StringId64 patrolName = args.NextStringId();
		I32F iStartPoint	  = args.NextI32();
		pNpc->SetPatrolRoute(patrolName, INVALID_STRING_ID_64, iStartPoint);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-spline-network", DcNpcSetSplineNetwork)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	if (Npc* pNpc = args.NextNpc())
	{
		StringId64 networkName = args.NextStringId();
		pNpc->SetSplineNetwork(networkName);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-spline-network-settings", DcNpcSetSplineNetworkSettings)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	if (Npc* pNpc = args.NextNpc())
	{
		StringId64 settingsId = args.NextStringId();
		pNpc->SetSplineNetworkSettings(settingsId);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-spline-network-target-point/f", DcNpcSetSplineNetworkTargetPointF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc	   = args.NextNpc();
	const Point pt = args.NextPoint();
	if (pNpc)
	{
		SplineNetworkSkill* pSkill = pNpc->GetActiveSkillAs<SplineNetworkSkill>(SID("SplineNetworkSkill"));
		if (!pSkill)
		{
			// args.MsgScriptError("We are trying to set a spline network target point on NPC %s who is not in the SplineNetwork skill. Please tell Harold if this needs to be supported\n", DevKitOnly_StringIdToString(pNpc->GetUserId()));
			return ScriptValue(0);
		}
		pSkill->ScriptSetSearchDestinationOverridePt(pt);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("horse-set-orphaned-spline-network-movement", DcHorseSetOrphanedSplineNetworkMovement)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc	   = args.NextNpc();
	const DC::NpcMotionType motionType = args.NextI32();
	const StringId64 mtSubcat = args.NextStringId();

	if (pNpc)
	{
		SplineNetworkSkill* pSkill = pNpc->GetActiveSkillAs<SplineNetworkSkill>(SID("SplineNetworkSkill"));
		if (!pSkill)
		{
			return ScriptValue(0);
		}
		pSkill->ScriptSetOrphanedMovement(motionType, mtSubcat);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-patrol-route", DcNpcGetPatrolRoute)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	if (Npc* pNpc = args.NextNpc())
	{
		const StringId64 patrolName = pNpc->GetPatrolRouteId();
		return ScriptValue(patrolName);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-only-damage-onscreen-targets", DcNpcSetOnlyDamageOnscreenTargets)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	if (Npc* pNpc = args.NextNpc())
	{
		// 		pNpc->GetConfiguration().m_onlyDamageOnscreenTargets = args.NextBoolean();
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
 *	(define-c-function npc-is-aware-of? ((npc-name symbol) (contact-name symbol)
 *		boolean)
 */
SCRIPT_FUNC("npc-is-aware-of?", DcNpcIsAwareOfP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	NdGameObject* pEntityGameObject = args.NextGameObject();

	if (pNpc && pEntityGameObject)
	{
		const AiKnowledge* pKnow = pNpc->GetKnowledge();
		const AiEntity* pEntity	 = pKnow->GetEntity(pEntityGameObject);

		if (!pEntity || !pEntity->IsKnown())
			return ScriptValue(false);

		return ScriptValue(true);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-target", DcNpcGetTarget)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		return ScriptValue(pNpc->GetCurrentTargetProcessHandle().GetUserId());
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-disable-hungarian-targeting", DcNpcDisableHungarianTargeting)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Human* pNpc = Human::FromProcess(args.NextNpc());

	if (pNpc)
	{
		pNpc->SetDisableHungarianTargeting(true);

	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-enable-hungarian-targeting", DcNpcEnableHungarianTargeting)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Human* pNpc = Human::FromProcess(args.NextNpc());

	if (pNpc)
	{
		pNpc->SetDisableHungarianTargeting(false);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
 *	(define-c-function npc-target-missing? ((npc-name symbol) (contact-name symbol))
 *		boolean)
 */
SCRIPT_FUNC("npc-target-missing?", DcNpcTargetMissing)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	NdGameObject* pEntityGameObject = args.NextGameObject();

	if (pNpc && pEntityGameObject)
	{
		const AiKnowledge* pKnow = pNpc->GetKnowledge();
		const AiEntity* pEntity	 = pKnow->GetEntity(pEntityGameObject);

		if (pEntity)
			return ScriptValue(pEntity->IsMissing());
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
 *	(define-c-function npc-target-lost? ((npc-name symbol) (target symbol))
 *		boolean)
 */
SCRIPT_FUNC("npc-target-lost?", DcNpcTargetLostP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	NdGameObject* pEntityGameObject = args.NextGameObject();

	if (pNpc && pEntityGameObject)
	{
		const AiKnowledge* pKnow = pNpc->GetKnowledge();
		const AiEntity* pEntity	 = pKnow->GetEntity(pEntityGameObject);

		if (pEntity)
			return ScriptValue(pEntity->IsLost());
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
 *	(define-c-function faction-entity-missing? ((faction dc-faction) (contact-name symbol))
 *		boolean)
 */
SCRIPT_FUNC("faction-entity-missing?", DcFactionEntityMissing)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	const DC::DcFaction dcFaction = args.NextU32();
	NdGameObjectHandle hGo		  = args.NextGameObjectHandle();

	const FactionId factionId = DcFactionToFactionId(dcFaction);
	if (!g_factionMgr.IsValid(factionId))
	{
		args.MsgScriptError("Invalid faction %d (%s) -- did you pass a SID by accident?\n",
							(I32)dcFaction,
							DevKitOnly_StringIdToStringOrHex(StringId64(dcFaction)));
		return ScriptValue(false);
	}

	if (!hGo.HandleValid())
		return ScriptValue(false);

	AiCoordinatorManager::Iterator iter = AiCoordinatorManager::Get().GetIterator();

	for (U32 i = iter.First(); i < iter.End(); i = iter.Advance())
	{
		AiEncounterCoordinator* pCoord = AiCoordinatorManager::Get().GetCoordinatorAs<AiEncounterCoordinator>(i);
		if (pCoord && pCoord->GetFactionId() == factionId)
		{
			const AiCharacterGroup npcs = pCoord->GetAiCharacterGroup();
			for (AiCharacterGroupIterator itNpc = npcs.BeginMembers(); itNpc != npcs.EndMembers(); ++itNpc)
			{
				const Npc* pNpc = itNpc.GetMemberAs<Npc>();
				if (!pNpc)
					continue;

				const AiEntity* pEntity = pNpc->GetKnowledge()->GetEntity(hGo);
				if (!pEntity)
					continue;

				if (pEntity->IsMissing())
					continue;

				return ScriptValue(false);
			}
		}
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
 *	(define-c-function faction-investigating? ((faction dc-faction))
 *		boolean)
 */
SCRIPT_FUNC("faction-investigating?", DcFactionInvestigatingP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const DC::DcFaction dcFaction = args.NextU32();

	const FactionId factionId = DcFactionToFactionId(dcFaction);
	if (!g_factionMgr.IsValid(factionId))
	{
		args.MsgScriptError("Invalid faction %d (%s) -- did you pass a SID by accident?\n",
							(I32)dcFaction,
							DevKitOnly_StringIdToStringOrHex(StringId64(dcFaction)));
		return ScriptValue(false);
	}

	bool result = false;

	AiCoordinatorManager::Iterator itCoord = AiCoordinatorManager::Get().GetIterator();
	for (U32 iCoord = itCoord.First(); iCoord < itCoord.End(); iCoord = itCoord.Advance())
	{
		AiEncounterCoordinator* pCoord = AiCoordinatorManager::Get().GetCoordinatorAs<AiEncounterCoordinator>(iCoord);
		if (!pCoord || pCoord->GetFactionId() != factionId)
			continue;

		const AiCharacterGroup npcs = pCoord->GetAiCharacterGroup();
		for (AiCharacterGroupIterator itNpc = npcs.BeginMembers(); itNpc != npcs.EndMembers(); ++itNpc)
		{
			const Npc* pNpc = itNpc.GetMemberAs<Npc>();
			if (!pNpc || !pNpc->IsInvestigating())
				continue;

			result = true;
			break;
		}
	}

	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
 *	(define-c-function npc-set-can-cause-combat-tension ((npc-name symbol) (enabled boolean))
 *		none)
 */
SCRIPT_FUNC("npc-set-can-cause-combat-tension", DcNpcSetCanCauseCombatTension)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(0);

	const bool enabled = args.NextBoolean();
	pNpc->SetCanCauseCombatTension(enabled);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
 *	(define-c-function npc-set-excluded-from-tension-calculation ((npc-name symbol) (excluded boolean))
 *		none)
 */
SCRIPT_FUNC("npc-set-excluded-from-tension-calculation", DcNpcSetExcludedFromTensionCalculation)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(0);

	const bool excluded = args.NextBoolean();
	pNpc->SetExcludedFromTensionCalculation(excluded);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
 *	(define-c-function faction-entity-lost? ((faction dc-faction) (contact-name symbol))
 *		boolean)
 */
SCRIPT_FUNC("faction-entity-lost?", DcGroupEntityLostP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	const DC::DcFaction dcFaction = args.NextU32();
	NdGameObjectHandle hGo		  = args.NextGameObjectHandle();

	const FactionId factionId = DcFactionToFactionId(dcFaction);
	if (!g_factionMgr.IsValid(factionId))
	{
		args.MsgScriptError("Invalid faction %d (%s) -- did you pass a SID by accident?\n",
							(I32)dcFaction,
							DevKitOnly_StringIdToStringOrHex(StringId64(dcFaction)));
		return ScriptValue(false);
	}

	if (!hGo.HandleValid())
	{
		return ScriptValue(false);
	}

	AiCoordinatorManager::Iterator itCoord = AiCoordinatorManager::Get().GetIterator();
	for (U32 i = itCoord.First(); i < itCoord.End(); i = itCoord.Advance())
	{
		AiEncounterCoordinator* pCoord = AiCoordinatorManager::Get().GetCoordinatorAs<AiEncounterCoordinator>(i);
		if (!pCoord || pCoord->GetFactionId() != factionId)
			continue;

		const AiCharacterGroup& charGroup = pCoord->GetAiCharacterGroup();
		for (AiCharacterGroupIterator itChar = charGroup.BeginMembers(); itChar != charGroup.EndMembers(); ++itChar)
		{
			const Npc* pNpc = itChar.GetMemberAs<Npc>();
			if (!pNpc || pNpc->IsDead())
				continue;

			const AiKnowledge* pKnow = pNpc->GetKnowledge();
			if (!pKnow)
				continue;

			const AiEntity* pEntity = pKnow->GetEntity(hGo);
			if (!pEntity)
				continue;

			if (!pEntity->IsLost())
				continue;

			return ScriptValue(true);
		}
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-group-entity-position", DcGetGroupEntityPosition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	const DC::DcFaction dcFaction = args.NextU32();
	NdGameObjectHandle hGo		  = args.NextGameObjectHandle();

	const FactionId factionId = DcFactionToFactionId(dcFaction);
	if (!g_factionMgr.IsValid(factionId))
	{
		args.MsgScriptError("Invalid faction %d (%s) -- did you pass a SID by accident?\n",
							(I32)dcFaction,
							DevKitOnly_StringIdToStringOrHex(StringId64(dcFaction)));
		return ScriptValue(0);
	}

	if (!hGo.HandleValid())
	{
		return ScriptValue(0);
	}

	TimeFrame bestUpdateTime	= TimeFrameNegInfinity();
	const AiEntity* pBestEntity = nullptr;

	AtomicLockJanitorRead_Jls npcGroupLock(EngineComponents::GetNpcManager()->GetNpcGroupLock(), FILE_LINE_FUNC);
	for (const MutableNpcHandle& hNpc : EngineComponents::GetNpcManager()->GetNpcs())
	{
		const Npc* pNpc = hNpc.ToProcess();
		if (!pNpc || pNpc->IsDead())
			continue;

		const AiKnowledge* pKnow = pNpc->GetKnowledge();
		if (!pKnow)
			continue;

		const AiEntity* pEntity = pKnow->GetEntity(hGo);
		if (!pEntity || !pEntity->IsValid() || !pEntity->IsPositionValid())
			continue;

		const TimeFrame updateTime = pEntity->GetLastUpdateTime();
		if (updateTime <= bestUpdateTime)
			continue;

		bestUpdateTime = updateTime;
		pBestEntity	   = pEntity;
	}

	if (pBestEntity)
	{
		Point* pPosition = NDI_NEW(kAllocSingleGameFrame) Point(pBestEntity->GetPositionWs());
		return ScriptValue(pPosition);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-entity-position", DcNpcGetEntityPosition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	const Npc* pNpc = args.NextNpc();
	const NdGameObjectHandle hTarget = args.NextGameObjectHandle();

	if (!pNpc || !hTarget.HandleValid())
		return ScriptValue(nullptr);

	const AiKnowledge* pKnow = pNpc->GetKnowledge();
	if (!pKnow)
		return ScriptValue(nullptr);

	const AiEntity* pEntity = pKnow->GetEntity(hTarget);
	if (!pEntity)
		return ScriptValue(nullptr);

	if (!pEntity->IsPositionValid())
		return ScriptValue(nullptr);

	Point* pPosition = NDI_NEW(kAllocSingleGameFrame) Point(pEntity->GetPositionWs());
	return ScriptValue(pPosition);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-entity-velocity", DcNpcGetEntityVelocity)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	const Npc* pNpc = args.NextNpc();
	const NdGameObjectHandle hTarget = args.NextGameObjectHandle();

	if (!pNpc || !hTarget.HandleValid())
		return ScriptValue(nullptr);

	const AiKnowledge* pKnow = pNpc->GetKnowledge();
	if (!pKnow)
		return ScriptValue(nullptr);

	const AiEntity* pEntity = pKnow->GetEntity(hTarget);
	if (!pEntity)
		return ScriptValue(nullptr);

	Vector* pVector = NDI_NEW(kAllocSingleGameFrame) Vector(pEntity->GetVelocityWs());
	return ScriptValue(pVector);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-target-entity-position", DcNpcGetTargetEntityPosition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue(nullptr);

	const AiEntity* pEntity = pNpc->GetCurrentTargetEntity();
	if (!pEntity)
		return ScriptValue(nullptr);

	Point* pPosition = NDI_NEW(kAllocSingleGameFrame) Point(pEntity->GetPositionWs());
	return ScriptValue(pPosition);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-target-entity-has-uncover-line-of-sight", DcNpcGetTargetEntityHasUncoverLineOfSight)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue(INVALID_STRING_ID_64);

	const AiEntity* pEntity = pNpc->GetCurrentTargetEntity();
	if (!pEntity)
		return ScriptValue(INVALID_STRING_ID_64);

	return ScriptValue(pEntity->IsUncovered());
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-target-entity-has-line-of-sight", DcNpcGetTargetEntityHasLineOfSight)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue(INVALID_STRING_ID_64);

	const AiEntity* pEntity = pNpc->GetCurrentTargetEntity();
	if (!pEntity)
		return ScriptValue(INVALID_STRING_ID_64);

	return ScriptValue(pEntity->HasLOSToEntity());
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-target-entity-time-since-seen-or-heard", DcNpcGetTargetEntityTimeSinceSeenOrHeard)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue(INVALID_STRING_ID_64);

	const AiEntity* pEntity = pNpc->GetCurrentTargetEntity();
	if (!pEntity)
		return ScriptValue(INVALID_STRING_ID_64);

	return ScriptValue(pEntity->GetTimeSinceLastSeenOrHeard());
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-target-entity-object", DcNpcGetTargetEntityObject)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue(INVALID_STRING_ID_64);

	const AiEntity* pEntity = pNpc->GetCurrentTargetEntity();
	if (!pEntity)
		return ScriptValue(INVALID_STRING_ID_64);

	return ScriptValue(pEntity->ToGameObjectHandle().GetUserId());
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-target-entity-awareness", DcNpcGetTargetEntityAwareness)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue(DC::kAiEntityAwarenessNone);

	const AiEntity* pEntity = pNpc->GetCurrentTargetEntity();
	if (!pEntity)
		return ScriptValue(DC::kAiEntityAwarenessNone);

	return ScriptValue(pEntity->GetAwareness());
}

/// --------------------------------------------------------------------------------------------------------------- ///

SCRIPT_REGISTER_FUNC_AUTO_PARAMS(bool, DcNpcHasInvestigateEntity, (Npc & npc), "npc-has-investigate-entity?")
{
	const AiEntity* pEntity = npc.GetCurrentInvestigateEntity();
	return (pEntity != nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-investigate-entity-position", DcNpcGetInvestigateEntityPosition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue(nullptr);

	const AiEntity* pEntity = pNpc->GetCurrentInvestigateEntity();
	if (!pEntity)
		return ScriptValue(nullptr);

	Point* pPosition = NDI_NEW(kAllocSingleGameFrame) Point(pEntity->GetPositionWs());
	return ScriptValue(pPosition);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-investigate-entity-object", DcNpcGetInvestigateEntityObject)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue(INVALID_STRING_ID_64);

	const AiEntity* pEntity = pNpc->GetCurrentInvestigateEntity();
	if (!pEntity)
		return ScriptValue(INVALID_STRING_ID_64);

	return ScriptValue(pEntity->ToGameObjectHandle().GetUserId());
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-investigate-entity-investigatable-time", DcNpcGetInvestigateEntityInvestigatableTime)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue(-1.0f);

	const AiEntity* pEntity = pNpc->GetCurrentInvestigateEntity();
	if (!pEntity)
		return ScriptValue(-1.0f);

	const F32 time = pEntity->GetTimeInAwareness().ToSeconds();

	return ScriptValue(time);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(F32, DcNpcGetTargetRating, (Npc* pNpc, NdGameObject* pTarget), "npc-get-target-rating")
{
	if (!pNpc)
		return AiTargetAppraiser::kWorstTargetRating;

	if (!pTarget)
		return AiTargetAppraiser::kWorstTargetRating;

	const AiEntity* pEntity = pNpc->GetKnowledge()->GetEntity(pTarget);
	if (!pEntity)
		return AiTargetAppraiser::kWorstTargetRating;

	AiGroupEntityHandle hTarget = pEntity->GetGroupEntityHandle();

	F32 targetRating = AiTargetAppraiser::kWorstTargetRating;
	pNpc->GetTargetAppraiser()->RateTargets(1, &targetRating, &hTarget, nullptr);
	return AiTargetAppraiser::kBestTargetRating - targetRating;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-fire-weapon", DcNpcFireWeapon)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pGo = args.NextGameObject();
	U32 damage		  = args.NextU32();
	bool allowTracer  = args.NextBoolean();

	if (Npc* pNpc = Npc::FromProcess(pGo))
	{
		SsVerboseLog(1, "npc-fire-weapon");

		pNpc->FireInDirectionOfWeapon(damage, false, nullptr, allowTracer);

		// TO DO: Wait until the fire is done?

		return ScriptValue(1);
	}
	else if (SimpleNpc* pSNpc = SimpleNpc::FromProcess(pGo))
	{
		pSNpc->FireInDirectionOfWeapon(damage, false, nullptr, allowTracer);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue DcNpcTryToWildFireInternal(int argc, ScriptValue* argv, ScriptFnAddr __SCRIPT_FUNCTION__, bool bWait)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const BoundFrame* pTargetBoundFrame = args.NextBoundFrame();

	if (pNpc && pTargetBoundFrame)
	{
		SsVerboseLog(1, "wait-npc-try-to-wild-fire");

		SsInstance* pScriptInst = GetJobSsContext().GetScriptInstance();
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();
		if (bWait && !(pScriptInst && pGroupInst && pTrackInst))
		{
			args.MsgScriptError("Called outside script context -- will not wait\n");
		}

		if (bWait && pGroupInst)
		{
			pGroupInst->WaitForTrackDone("wait-npc-try-to-wild-fire");
		}

		// T2-TODO pNpc->ScriptCommandWildFire(*pTargetBoundFrame, pGroupInst, pTrackInst->GetTrackIndex(), bWait);

		if (bWait)
		{
			ScriptContinuation::Suspend(argv);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue DcNpcTryToWildFireSpawnerInternal(int argc,
													 ScriptValue* argv,
													 ScriptFnAddr __SCRIPT_FUNCTION__,
													 bool bWait)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const EntitySpawner* pTargetSpawner = args.NextSpawner();

	if (pNpc && pTargetSpawner)
	{
		SsVerboseLog(1, "wait-npc-try-to-wild-fire-spawner");

		SsInstance* pScriptInst = GetJobSsContext().GetScriptInstance();
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();
		if (bWait && !(pScriptInst && pGroupInst && pTrackInst))
		{
			args.MsgScriptError("Called outside script context -- will not wait\n");
		}

		if (bWait && pGroupInst)
		{
			pGroupInst->WaitForTrackDone("wait-npc-try-to-wild-fire-spawner");
		}

		// T2-TODO pNpc->ScriptCommandWildFire(pTargetSpawner, pGroupInst, pTrackInst->GetTrackIndex(), bWait);

		if (bWait)
		{
			ScriptContinuation::Suspend(argv);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-face-point", DcNpcFace)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc		 = args.NextNpc();
	const Vec3 posWs = args.NextVec3();

	if (pNpc)
	{
		pNpc->SetFacePositionWs(Point(posWs.x, posWs.y, posWs.z));
		return ScriptValue(1);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-group-find-first-alive", DcNpcGroupFindFirstAlive)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	StringId64 groupId;
	DArrayAdapter group = NextDArray(args, groupId);

	if (group.IsValid())
	{
		U32F count = group.GetElemCount();
		for (U32F index = 0; index < count; ++index)
		{
			StringId64 id = group.GetElemAt(index).GetAsStringId();

			const EntitySpawner* pSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByNameId(id);
			if (pSpawner)
			{
				Process* pProcess = pSpawner->GetProcess();
				if (pProcess && pProcess->IsKindOf(g_type_Character))
				{
					Character* pCharacter = static_cast<Character*>(pProcess);

					bool bDead = pCharacter->IsDead();
					if (!bDead)
					{
						return ScriptValue(id);
					}
				}
			}
		}
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-remove-all-ragdolls", DcRemoveAllRagdolls)
{
	ProcessRagdoll::KillAllRagdolls();
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-remove-weapon
		(
			(npc-name string)
		)
		none
	)
*/
SCRIPT_FUNC("npc-destroy-weapon", DcNpcDestroyWeapon)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		if (ProcessWeaponBase* pWeapon = pNpc->GetWeapon())
		{
			if (PropInventory* pPropInv = pNpc->GetPropInventory())
			{
				pPropInv->ForgetProp(pWeapon);
			}
			KillProcess(pWeapon);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-set-comm-group-id ((npc-name string) (group-id string))
	  none)
 */
SCRIPT_FUNC("npc-set-comm-group-id", DcNpcSetCommGroupId)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	StringId64 groupName = args.NextStringId();

	if (pNpc)
	{
		AI_ASSERTF(!pNpc->WasSpawnedCheap(), ("Cheap NPCs currently don't support (npc-set-comm-group-id)"));

		pNpc->RemoveEncounterCoordinator();
		pNpc->SetCommGroupId(groupName);
		pNpc->GetKnowledge()->DestroyEntities();

		const I32 coordinatorId = AiCoordinatorManager::GetAs<AiGameCoordinatorManager>()
									  .FindBestEncounterCoordinator(pNpc);
		AI_ASSERT(coordinatorId != -1);
		pNpc->SetEncounterCoordinator(coordinatorId);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-get-comm-group-id ((npc-name string) (group-id string))
none)
*/
SCRIPT_FUNC("npc-get-comm-group-id", DcNpcGetCommGroupId)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		return ScriptValue(pNpc->GetCommGroupId());
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-player-scent-position", DcGetPlayerScentPosition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	int index = args.NextI32();

	Point* pRetPoint = NDI_NEW(kAllocSingleGameFrame, kAlign16) Point(kZero);

	// @TODO SERVER-PLAYER: dogs dont work with multiple players it seems
	if (GetPlayer() == nullptr)
	{
		args.MsgScriptError("get-player-scent-position called but there is no player!\n");
		return ScriptValue(pRetPoint);
	}

	if (ScentGenerator* pScentGenerator = ScentManager::Get().GetScentGenerator(GetPlayer()->GetUserId()))
	{
		*pRetPoint = pScentGenerator->GetScentPosition(index);
	}

	return ScriptValue(pRetPoint);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("get-player-scent-age", DcGetPlayerScentAge)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	int index = args.NextI32();

	// @TODO SERVER-PLAYER:
	if (GetPlayer() == nullptr)
	{
		args.MsgScriptError("get-player-scent-position called but there is no player!\n");
		return ScriptValue(kLargestFloat);
	}

	if (ScentGenerator* pScentGenerator = ScentManager::Get().GetScentGenerator(GetPlayer()->GetUserId()))
	{
		ScriptValue(pScentGenerator->GetScentAge(index));
	}

	return ScriptValue(NDI_FLT_MAX);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-pet-owner-partner", DcNpcGetPetOwnerPartner)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	const Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		if (const Npc* pPartner = GetPetOwnerPartner(pNpc))
		{
			return ScriptValue(pPartner->GetUserId());
		}
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-has-scent?", DcNpcHasScent)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	bool hasScent = false;

	const Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		if (const AiSmelling* pSmelling = pNpc->GetSmelling())
		{
			hasScent = pSmelling->GetSmellingMode() != kSmellingModeDisabled && pSmelling->HasNewScent();
		}
	}

	return ScriptValue(hasScent);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("reset-track-trail", DcResetTrackTrail)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		if (TrackSkill* pTrack = pNpc->GetActiveSkillAs<TrackSkill>(SID("TrackSkill")))
			pTrack->ResetTrail();
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-has-active-scent?", DcNpcHasActiveScent)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		if (AiSmelling* pSmelling = pNpc->GetSmelling())
		{
			SniffResult lastSniffResult = pSmelling->GetLastSniffResult();

			if (lastSniffResult.IsValid() && lastSniffResult.m_sourceIndex == kOwnerScentIdx
				&& lastSniffResult.GetAge() < Seconds(0.3f))
			{
				// Hack for mourn skill
				return ScriptValue(true);
			}

			return ScriptValue(pSmelling->HasNewScent());
		}
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-remaining-track-time", DcNpcGetRemainingTrackTime)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		if (TrackSkill* pTrack = pNpc->GetActiveSkillAs<TrackSkill>(SID("TrackSkill")))
		{
			if (AiSmelling* pSmelling = pNpc->GetSmelling())
			{
				return ScriptValue((pSmelling->GetTrackExpireTime() - pNpc->GetCurTime()).ToSeconds());
			}
		}
	}

	return ScriptValue(-1.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-last-scent-age", DcGetActiveScentAge)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		if (AiSmelling* pSmelling = pNpc->GetSmelling())
		{
			SniffResult lastSniffResult = pSmelling->GetLastSniffResult();

			if (lastSniffResult.IsValid())
				return ScriptValue(lastSniffResult.GetAge().ToSeconds());
		}
	}

	return ScriptValue(-1.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-relationship", DcNpcSetRelationiship)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 4);

	Npc* pNpc = args.NextNpc();
	StringId64 relationshipName = args.NextStringId();
	DC::AiRelationshipType relationshipType = args.NextI32();
	DC::AiRelationshipMemberType memberType = args.NextI32();
	bool spawnLeash = args.NextBoolean();

	RelationshipManager::Get().Register(pNpc, relationshipName, relationshipType, memberType, spawnLeash);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-clear-relationship", DcNpcClearRelationiship)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	DC::AiRelationshipType relationshipType = args.NextI32();

	RelationshipManager::Get().Unregister(pNpc, relationshipType);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-has-stimulus-from-object?", DcNpcHasStimulusFromObject)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	Npc* pNpc = args.NextNpc();
	NdGameObject* pObj = args.NextGameObject();
	DC::AiPerceptionSense sense = args.NextI32();

	if (pNpc)
	{
		if (AiKnowledge* pKnowledge = pNpc->GetKnowledge())
		{
			if (const AiEntity* pEntity = pKnowledge->GetEntity(pObj))
			{
				return ScriptValue(pEntity->HasActiveStimulus(sense));
			}
		}
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-can-broadcast-communication?", DcNpcCanBroadcastCommunication)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{

		return ScriptValue(pNpc->CanBroadcastCommunication());
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("dog-request-door-open", DcDogRequestDoorOpen)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc		 = args.NextNpc();
	const Point dest = args.NextPoint();

	Dog* pDog = Dog::FromProcess(pNpc);

	if (!pDog)
	{
		if (Human* pHuman = Human::FromProcess(pNpc))
		{
			pDog = Dog::FromProcess(pHuman->GetPet().ToMutableProcess());
		}
	}

	if (pDog)
	{
		NavLocation navLoc = pDog->AsReachableNavLocationWs(dest, NavLocation::Type::kNavPoly);
		pDog->CheckForDoorBlockage(navLoc);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("request-leash-state", DcRequestLeashState)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const U32 leashState = args.NextU32();

	ASSERT(leashState < DC::kLeashStateCount);

	RelationshipManager::Get().RequestLeashAiState(pNpc, leashState);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("set-npc-leash-killed-when-not-attached", DcSetNpcKilledWhenAttached)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	bool bKill = args.NextBoolean();

	if (Human* pHuman = Human::FromProcess(pNpc))
	{
		if (ProcessLeash* pLeash = pHuman->GetLeash().ToMutableProcess())
		{
			pLeash->SetSuicideWhenNotAttached(bKill);
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("spawn-attached-leash", DcSpawnAttachedLeash)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	const EntitySpawner* pSpawner = args.NextSpawner();
	Npc* pNpc  = args.NextNpc();
	F32 length = args.NextFloat();

	if (pSpawner && pNpc)
	{
		ProcessLeash* pLeash = SpawnLeashAttachedInWorld(pSpawner, pNpc, length);
		return ScriptValue(pLeash ? pLeash->GetUserId() : INVALID_STRING_ID_64);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("detach-leash", DcDetachLeash)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	ProcessLeash* pLeash = ProcessLeash::FromProcess(args.NextProcess());
	if (pLeash)
		pLeash->m_leashState &= ~ProcessLeash::kLeashAttached;

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("dog-enable-flee-noise-maker", DcEnableFleeNoiseMaker)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc		  = args.NextNpc();
	const bool enable = args.NextBoolean();

	if (Dog* pDog = Dog::FromProcess(pNpc))
	{
		pDog->EnableFleeNoiseMaker(enable);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("dog-suppress-combat-start-bark", DcDogSupressCombatStartBark)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const TimeFrame until = EngineComponents::GetFrameState()->GetClock(kGameClock)->GetCurTime() + Seconds(args.NextFloat());
	DogSfxManager::SuppressCombatStartBark(until);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("dog-or-horse-override-sfx-loop/f", DcDogOrHorseOverrideSfxLoopF)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc		= args.NextNpc();
	StringId64 loopId = args.NextStringId();

	if (Dog* pDog = Dog::FromProcess(pNpc))
	{
		if (DogSfxManager* pSfxMgr = pDog->GetDogSfxManager())
		{
			pSfxMgr->SetScriptedLoopOverride(loopId);
		}
	}
	else if (Horse* pHorse = Horse::FromProcess(pNpc))
	{
		if (HorseSfxManager* pSfxMgr = pHorse->GetHorseSfxManager())
		{
			pSfxMgr->SetScriptedLoopOverride(loopId);
		}
	}

	return ScriptValue(0);
}
/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-force-door-kick", DcForceDoorKick)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc		= args.NextNpc();
	bool shouldKick = args.NextBoolean();

	if (Pirate* pPirate = Pirate::FromProcess(pNpc))
	{
		pPirate->SetAlwaysKickDoor(shouldKick);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("suppress-track-performance", DcSuppressTrackPerformance)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc		 = args.NextNpc();
	const float time = args.NextFloat();

	if (Dog* pDog = Dog::FromProcess(pNpc))
	{
		pDog->SuppressTrackPerformance(time);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-using-traversal-action-pack?", DcNpcUsingTraversalActionPackP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		return ScriptValue(pNpc->IsUsingTraversalActionPack());
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-teleport-to-ap
		(
			(npc-name					symbol)
			(action-pack-spawner		symbol)
			(delay-frames				int32		(:default 0))
			(play-entire-enter-anim		boolean		(:default #t))
		)
		none
	)
*/
SCRIPT_FUNC("npc-teleport-to-ap", DcNpcTeleportToAp)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);
	Npc* pNpc = args.NextNpc();
	ActionPack* pActionPack		   = args.NextActionPack();
	const I32F delayFrames		   = args.NextI32();
	const bool playEntireEnterAnim = args.NextBoolean();

	if (!pNpc || !pActionPack)
		return ScriptValue(false);

	AiLogScript(pNpc, "npc-teleport-to-ap");

	Npc::ApTeleportParams params;
	params.m_pAp		 = pActionPack;
	params.m_delayFrames = delayFrames;
	params.m_playEntireEnterAnim = playEntireEnterAnim;

	pNpc->RequestTeleportIntoActionPack(params);

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* (define-c-function npc-get-ground-projected-locator
 *	bound-frame)
 */
SCRIPT_FUNC("npc-get-ground-projected-locator", DcGetGroundProjectedLocator)
{
	BoundFrame* bf = nullptr;

	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	if (Npc* pNpc = args.NextNpc())
	{
		bf = NDI_NEW(kAllocSingleGameFrame, kAlign16) BoundFrame;
		BoundFrame npcBf = pNpc->GetBoundFrame();
		Locator npcLocPs = npcBf.GetLocatorPs();
		npcLocPs.SetTranslation(pNpc->GetGroundPosPs());
		npcBf.SetLocatorPs(npcLocPs);
		*bf = npcBf;
	}

	return ScriptValue(bf);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-nav-mesh", DcNpcGetNavMesh)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		if (const NavMesh* pMesh = pNpc->GetNavControl()->GetNavMesh())
		{
			return ScriptValue(pMesh->GetNameId());
		}
	}

	return ScriptValue(nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-last-good-position", DcNpcGetLastGoodPosition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		BoundFrame* pRetLoc = NDI_NEW(kAllocSingleGameFrame, kAlign16)
			BoundFrame(pNpc->GetNavControl()->GetLastGoodNavLocation().GetPosWs(), kIdentity, Binding());
		return ScriptValue(pRetLoc);
	}

	return ScriptValue(nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-suppress-threat-indicator", DcNpcSuppressThreatIndicator)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		NdScriptArgIterator::SuppressNpcScriptLog();
		pNpc->SuppressThreatIndicator(Seconds(0.1f));
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-suppress-threat-audio", DcNpcSuppressThreatAudio)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		NdScriptArgIterator::SuppressNpcScriptLog();
		pNpc->SuppressThreatAudio(Seconds(0.1f));
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("suppress-spot-player-stinger", DcSuppressSpotPlayerStinger)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 0);

	AiCoordinatorManager::Iterator iter = AiCoordinatorManager::Get().GetIterator();

	for (U32 i = iter.First(); i < iter.End(); i = iter.Advance())
	{
		AiEncounterCoordinator* pCoord = AiCoordinatorManager::Get().GetCoordinatorAs<AiEncounterCoordinator>(i);
		if (pCoord)
		{
			SuppressSpotPlayerStinger(Seconds(0.1f));
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("spot-player-stinger-pending?", DcSpotPlayerStingerPending)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 0);

	AtomicLockJanitorRead_Jls npcGroupLock(EngineComponents::GetNpcManager()->GetNpcGroupLock(), FILE_LINE_FUNC);

	PlayerHandle hPlayer = GetPlayerHandle();

	for (NpcHandle hNpc : EngineComponents::GetNpcManager()->GetNpcs())
	{
		const Npc* pNpc = hNpc.ToProcess();
		NAV_ASSERT(pNpc);
		if (!pNpc)
			continue;

		if (!IsEnemy(GetPlayerFaction(), pNpc->GetFactionId()))
			continue;

		if (pNpc->IsDead())
			continue;

		if (pNpc->IsGrappled())
			continue;

		if (pNpc->IsDoomed())
			continue;

		if (pNpc->GetFactDict()->Get(SID("being-held-up")).GetBool())
			continue;

		if (AiEncounterCoordinator* pCoord = pNpc->GetEncounterCoordinator())
		{
			if (pCoord->HasIdealMemberForRole(pNpc, SID("Spotter")))
			{
				return ScriptValue(true);
			}
		}
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("suppress-spot-player-dialog", DcSuppressSpotPlayerDialog)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 0);

	AiCoordinatorManager::Iterator iter = AiCoordinatorManager::Get().GetIterator();

	for (U32 i = iter.First(); i < iter.End(); i = iter.Advance())
	{
		AiEncounterCoordinator* pCoord = AiCoordinatorManager::Get().GetCoordinatorAs<AiEncounterCoordinator>(i);
		if (pCoord)
		{
			SuppressSpotPlayerDialog(Seconds(0.1f));
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("task-should-show-threat-indicators?", DcTaskShouldShowThreatIndicatorsP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 0);

	if (const GameTaskSubnode* pActiveSubnode = g_taskGraphMgr.GetActiveContinueSubnode())
	{
		const bool taskAllow = !(pActiveSubnode->m_flags & DC::kGameTaskNodeFlagDisableThreatIndicator);
		return ScriptValue(taskAllow);
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-should-show-threat-indicator?", DcNpcShouldShowThreatIndicatorP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	if (pNpc)
	{
		return ScriptValue(pNpc->IsThreatIndicatorEnabled());
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-suppress-perception", DcNpcSuppressPerception)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	const DC::AiEntityType entityType = args.NextI32();
	if (pNpc)
	{
		pNpc->GetKnowledge()->SuppressPerception(entityType, true);
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-blind", DcNpcSetBlind)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc		 = args.NextNpc();
	const bool blind = args.NextBoolean();

	if (pNpc)
	{
		pNpc->m_scriptedBlind = blind;
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("set-snowball-chance-to-hit-multiplier/f", DcSetSnowballChancetoHitMultiplier)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	float mult = args.NextFloat();

	SettingSetDefault(&g_aiGameOptions.m_snowballChanceToHitMultiplier, 1.0f);
	SettingSetPers(SID("set-snowball-chance-to-hit-multiplier/f"), &g_aiGameOptions.m_snowballChanceToHitMultiplier, mult, kPlayerModePriority, 1.0f, Seconds(0.1f));

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("disable-explore-turn-to/f", DcDisableExploreTurnTo)
{
	SettingSetDefault(&g_aiGameOptions.m_disableExploreTurnTo, false);
	SettingSetPers(SID("disable-explore-turn-to/f"), &g_aiGameOptions.m_disableExploreTurnTo, true, kPlayerModePriority, 1.0f, Seconds(0.1f));

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("freeze-combat-vector/f", DcFreezeCombatVector)
{
	SettingSetDefault(&g_aiGameOptions.m_freezeCombatVector, false);
	SettingSetPers(SID("freeze-combat-vector/f"), &g_aiGameOptions.m_freezeCombatVector, true, kPlayerModePriority, 1.0f, Seconds(0.1f));

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("player-phys-fx-events/f", DcPlayerPhysFxEvents)
{
	SettingSetDefault(&g_aiGameOptions.m_forcePlayerPhysFxEvents, false);
	SettingSetPers(SID("player-phys-fx-events/f"), &g_aiGameOptions.m_forcePlayerPhysFxEvents, true, kPlayerModePriority, 1.0f, Seconds(0.1f));

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-deaf", DcNpcSetDeaf)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc		= args.NextNpc();
	const bool deaf = args.NextBoolean();

	if (pNpc)
	{
		pNpc->m_scriptedDeaf = deaf;
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("disable-buddy-teleport", DcDisableBuddyTeleport)
{
	SettingSetDefault(&g_aiGameOptions.m_scriptDisableBuddyTeleport, false);
	SettingSetPers(SID("dark-npcs-see-flashlight"),
				   &g_aiGameOptions.m_scriptDisableBuddyTeleport,
				   true,
				   kPlayerModePriority,
				   1.0f,
				   Seconds(0.1f));

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function dark-npcs-see-flashlight
	(
	)
	none
)
*/
SCRIPT_FUNC("dark-npcs-see-flashlight", DcDarkNpcsSeeFlashlight)
{
#if 0 // T2-TODO
	SettingSetDefault(&g_aiGameOptions.m_darkNpcsSeeFlashlight, false);
	SettingSetPers(SID("dark-npcs-see-flashlight"), &g_aiGameOptions.m_darkNpcsSeeFlashlight, true, kPlayerModePriority, 1.0f, Seconds(0.1f));
#endif

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-has-long-weapon?", DcNpcHasLongWeapon)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (const Character* pChar = args.NextCharacter())
	{
		if (const ProcessWeaponBase* pWeapon = pChar->GetWeaponInHand())
		{
			switch (pWeapon->GetWeaponAnimType())
			{
			case DC::kWeaponAnimTypeRifle:
			case DC::kWeaponAnimTypeShotgun:
			case DC::kWeaponAnimTypeMelee1h:
			case DC::kWeaponAnimTypeMelee2h:
			case DC::kWeaponAnimTypeBow:
				return ScriptValue(true);
			}
		}
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-has-long-gun?", DcNpcHasLongGun)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (const Character* pChar = args.NextCharacter())
	{
		if (const ProcessWeaponBase* pWeapon = pChar->GetWeaponInHand())
		{
			switch (pWeapon->GetWeaponAnimType())
			{
			case DC::kWeaponAnimTypeRifle:
			case DC::kWeaponAnimTypeShotgun:
			case DC::kWeaponAnimTypeBow:
				return ScriptValue(true);
			}
		}
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-has-weapon-in-hand?", DcNpcHasWeaponInHand)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (const Character* const pChar = args.NextCharacter())
	{
		if (const ProcessWeaponBase* const pWeapon = pChar->GetWeaponInHand())
		{
			return ScriptValue(true);
		}
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-has-short-gun?", DcNpcHasShortGun)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (const Character* pChar = args.NextCharacter())
	{
		if (const ProcessWeaponBase* pWeapon = pChar->GetWeaponInHand())
		{
			switch (pWeapon->GetWeaponAnimType())
			{
			case DC::kWeaponAnimTypePistol:
				return ScriptValue(true);
			}
		}
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("is-npc?", DcIsNpcP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	//StringId64 npcId = args.NextStringId();
	//Npc* pNpc = EngineComponents::GetNpcManager()->FindNpcByUserId(npcId);

	Npc* pNpc = args.NextNpc(true);

	// for all your breakpoint needs
	if (pNpc)
	{
		return ScriptValue(true);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("is-horse?", DcIsHorseP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Horse* pHorse = args.NextHorse(true);
	return ScriptValue(pHorse != nullptr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-hard-point", DcNpcSetHardPoint)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	bool success = false;
	Npc* pNpc	 = args.NextNpc();

	if (pNpc)
	{
		const StringId64 spawnerName = args.NextStringId();
		success = pNpc->GetTactical()->SetScriptedHardPoint(spawnerName);
	}

	return ScriptValue(success);
}

SCRIPT_FUNC("npc-clear-hard-point", DcNpcClearHardPoint)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	bool success = false;
	Npc* pNpc	 = args.NextNpc();

	if (pNpc)
	{
		pNpc->GetTactical()->ClearScriptedHardPoint();
		success = true;
	}

	return ScriptValue(success);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-melee-behavior-condition", DcNpcSetMeleeBehaviorCondition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	MeleeBehaviorCondition* pMeleeBehaviorCondition = pNpc ? pNpc->GetMeleeBehaviorCondition() : nullptr;

	if (pMeleeBehaviorCondition)
	{
		const DC::MeleeBehaviorConditionEnum condition = args.NextI32();
		pMeleeBehaviorCondition->SetConditionOverride(condition, true);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-melee-behavior-condition-active?", DcNpcIsMeleeBehaviorConditionActiveP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);
	bool result = false;
	Npc* pNpc	= args.NextNpc();
	MeleeBehaviorCondition* pMeleeBehaviorCondition = pNpc ? pNpc->GetMeleeBehaviorCondition() : nullptr;

	if (pMeleeBehaviorCondition)
	{
		const DC::MeleeBehaviorConditionEnum vulnerability = args.NextI32();
		const float timeThreshold = args.NextFloat();
		result = pMeleeBehaviorCondition->IsActive(vulnerability, timeThreshold);
	}

	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-forced-melee-behavior", DcNpcSetForcedMeleeBehavior)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	const StringId64 behaviorId = args.NextStringId();

	if (pNpc)
	{
		pNpc->SetForcedMeleeBehaviorId(behaviorId);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-clear-forced-melee-behavior", DcNpcClearForcedMeleeBehavior)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		pNpc->SetForcedMeleeBehaviorId(INVALID_STRING_ID_64);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-forced-melee-behavior", DcNpcGetForcedMeleeBehavior)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	StringId64 result = INVALID_STRING_ID_64;
	if (pNpc)
	{
		result = pNpc->GetForcedMeleeBehaviorId();
	}

	return ScriptValue(result);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-get-limb-state", DcNpcGetLimbState)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		const DC::NpcLimbState state = pNpc->GetLimbState();
		return ScriptValue(state);
	}

	return ScriptValue(-1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-limb-state", DcNpcSetLimbState)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	const DC::NpcLimbState state = args.NextI32();

	if (pNpc)
	{
		pNpc->SetLimbState(state);
		return ScriptValue(true);
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("wants-to-get-in-car", DcWantsToGetInCar)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (!pNpc)
	{
		return ScriptValue(0);
	}

	ScriptSkill::NotifyWantsToGetInCar(pNpc);
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("override-move-to-vehicle-motion-type", DcOverrideMoveToVehicleMotionType)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const DC::NpcMotionType motionType = args.NextI32();

	ScriptSkill::OverrideMoveToVehicleMotionType(motionType);

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("suppress-drive-reverse-anims", DcSuppressDriveReverseAnims)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Character* pDriver = args.NextCharacter();

	if (!pDriver)
	{
		return ScriptValue(0);
	}

	SendEvent(SID("drive-event"), pDriver, SID("suppress-reverse-anims"));

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static ScriptValue DcNpcMoveAlongSplineInternal(ScriptArgIterator& args,
												Npc* pNpc,
												const MoveToParams& params,
												float splineStep = -1.0f)
{
	if (!pNpc || (params.m_splineNameId == INVALID_STRING_ID_64))
	{
		return ScriptValue(false);
	}

	const CatmullRom* pSpline = g_splineManager.FindByName(params.m_splineNameId);

	const MotionType motionType = DcMotionTypeToGame(params.m_dcMotionType);
	const NavGoalReachedType goalReachedType = ConvertDcGoalReachedType(params.m_dcGoalReachedType);

	SsVerboseLog(1,
				 "npc-move-along-spline (%s [%0.3f->%0.3f]) [%s, %s]",
				 DevKitOnly_StringIdToString(params.m_splineNameId),
				 GetMotionTypeName(motionType),
				 GetGoalReachedTypeName(goalReachedType));

	if (!pSpline)
	{
		args.MsgScriptError("Spline '%s' not found\n", DevKitOnly_StringIdToString(params.m_splineNameId));
		return ScriptValue(false);
	}

	const float arcMax	 = pSpline->GetTotalArcLength();
	const float arcStart = Limit(pSpline->FindArcLengthClosestToPoint(params.m_splineStartPosWs), 0.0f, arcMax);
	const float arcGoal	 = Limit(pSpline->FindArcLengthClosestToPoint(params.m_splineGoalPosWs), 0.0f, arcMax);

	float arcStep = splineStep > 0.0f ? splineStep : Sign(arcGoal - arcStart) * 1.0f;

	if (pSpline->IsLooped())
	{
		const float arcTotal = pSpline->GetTotalArcLength();
		if (Abs(arcGoal - arcStart) > (arcTotal * 0.5f))
		{
			// shorter to go the other way
			arcStep *= -1.0f;
		}
	}

	SsInstance* pScriptInst = GetJobSsContext().GetScriptInstance();
	SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

	if (params.m_wait && !(pScriptInst && pGroupInst && pTrackInst))
	{
		args.MsgScriptError("Called outside script context -- will not wait\n");
	}

	float goalRadius = pNpc->GetMotionConfig().m_ignoreMoveDistance;
	MotionType goalMotionType = motionType;

	NavMoveArgs moveArgs;
	moveArgs.m_goalRadius	   = goalRadius;
	moveArgs.m_motionType	   = motionType;
	moveArgs.m_goalReachedType = goalReachedType;
	moveArgs.m_mtSubcategory   = params.m_mtSubcategory;
	moveArgs.m_performanceId   = params.m_performanceId;

	pNpc->ScriptCommandMoveAlongSpline(pSpline,
									   arcStart,
									   arcGoal,
									   arcStep,
									   moveArgs,
									   params.m_tryToShoot,
									   pGroupInst,
									   pTrackInst ? pTrackInst->GetTrackIndex() : 0,
									   params.m_wait);

	// Wait until the move-to is done.
	if (params.m_wait && pScriptInst && pGroupInst && pTrackInst)
	{
		pGroupInst->WaitForTrackDone("npc-move-to");
		ScriptContinuation::Suspend(args.GetArgArray());
	}

	return ScriptValue(1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-move-along-spline", DcNpcMoveAlongSpline)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();

	MoveToParams params;
	params.m_splineNameId	   = args.NextStringId();
	params.m_splineStartPosWs  = args.NextPoint();
	params.m_splineGoalPosWs   = args.NextPoint();
	params.m_dcMotionType	   = args.NextI32();
	params.m_dcGoalReachedType = args.NextI32();
	params.m_mtSubcategory	   = args.NextStringId();
	params.m_tryToShoot		   = args.NextBoolean();
	params.m_wait	  = false;
	params.m_strafing = args.NextBoolean();
	params.m_performanceId = args.NextStringId();
	float splineStep	   = args.NextFloat();

	AiLogScript(pNpc, "npc-move-to");
	return DcNpcMoveAlongSplineInternal(args, pNpc, params, splineStep);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("wait-npc-move-along-spline", DcWaitNpcMoveAlongSpline)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();

	MoveToParams params;
	params.m_splineNameId	   = args.NextStringId();
	params.m_splineStartPosWs  = args.NextPoint();
	params.m_splineGoalPosWs   = args.NextPoint();
	params.m_dcMotionType	   = args.NextI32();
	params.m_dcGoalReachedType = args.NextI32();
	params.m_mtSubcategory	   = args.NextStringId();
	params.m_tryToShoot		   = args.NextBoolean();
	params.m_wait	  = true;
	params.m_strafing = args.NextBoolean();
	params.m_performanceId = args.NextStringId();
	float splineStep	   = args.NextFloat();

	AiLogScript(pNpc, "wait-npc-move-to");
	return DcNpcMoveAlongSplineInternal(args, pNpc, params, splineStep);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-move-in-direction", DcNpcMoveInDirection)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	const Vector dirWs	  = args.NextVector();
	MotionType motionType = DcMotionTypeToGame(args.NextI32());
	const StringId64 mtSubcategory = args.NextStringId();
	const bool tryToShoot	= args.NextBoolean();
	const bool wantToStrafe = args.NextBoolean();

	if (!pNpc)
	{
		return ScriptValue(false);
	}

	const Vector dirPs = pNpc->GetParentSpace().UntransformVector(SafeNormalize(dirWs, GetLocalZ(pNpc->GetRotation())));

	NavMoveArgs moveArgs;
	moveArgs.m_motionType	 = motionType;
	moveArgs.m_mtSubcategory = mtSubcategory;
	moveArgs.m_strafeMode	 = wantToStrafe ? DC::kNpcStrafeAlways : DC::kNpcStrafeNever;

	SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

	pNpc->ScriptCommandMoveInDirection(dirPs,
									   moveArgs,
									   wantToStrafe,
									   pGroupInst,
									   pTrackInst ? pTrackInst->GetTrackIndex() : 0);

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(const BoundFrame*,
								 DcGetMeleeStrafeStepLocator,
								 (Character & p, const BoundFrame* start, const BoundFrame* end, float phase),
								 "get-melee-quick-step-locator")
{
	if (p.IsKindOf(g_type_Npc))
	{
		const Npc* pNpc = static_cast<Npc*>(&p);
		const NdGameObject* pTarget = pNpc->GetCurrentTargetProcess();

		if (!pTarget)
			return end;

		const Point targetPos = pTarget->GetTranslation();
		const Point startPos  = start->GetTranslation();

		const Point endPos = end->GetTranslation();
		const Vector endToTargetDir = SafeNormalize(targetPos - endPos, kUnitZAxis);

		const Quat startRot = start->GetRotation();
		const Quat finalRot = QuatFromLookAt(endToTargetDir, kUnitYAxis);

		const Quat desiredRot = Slerp(startRot, finalRot, phase);

		BoundFrame* pBf = NDI_NEW(kAllocSingleGameFrame, kAlign16) BoundFrame(*start);
		pBf->SetTranslation(endPos);
		pBf->SetRotation(desiredRot);

		return pBf;
	}

	return end;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(bool,
								 DcIsAdditiveHitReactionBlockingP,
								 (Character & p),
								 "is-additive-hit-reaction-blocking?")
{
	if (p.IsKindOf(g_type_Npc))
	{
		const Npc* pNpc = static_cast<Npc*>(&p);
		bool blocking	= pNpc->GetAnimationControllers()->GetHitController()->IsAdditiveHitReactionPausingMelee();

		return blocking;
	}

	return false;
}

SCRIPT_FUNC("npc-stun-all-npcs-in-radius", DcNpcStunAllNpcsInRadius)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);

	Explosion::sStunParams stunParams;
	stunParams.m_bOverrideStunSettings = true;
	stunParams.m_stunImpactPoint	   = args.NextPoint();
	stunParams.m_stunRadius = args.NextFloat();
	stunParams.m_stunTime	= args.NextFloat();
	stunParams.m_hitReactionSourceType = args.NextU32();
	stunParams.m_bIgnoreBuddies		   = args.NextBoolean();

	Explosion::SpawnStunExplosion(stunParams);

	return ScriptValue(0);
}

SCRIPT_FUNC("npc-stun", DcNpcStun)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc	 = args.NextNpc();
	F32 stunTime = args.NextFloat();
	DC::HitReactionSourceType hitReactionSourceType = args.NextU32();
	if (!pNpc)
		return ScriptValue(0);

	if (!pNpc->IsStunEnabled() || !pNpc->GetArchetype().AllowStun())
	{
		MsgConErr("Called npc-stun on %s, who does not allow stun\n", DevKitOnly_StringIdToString(pNpc->GetUserId()));
		return ScriptValue(0);
	}

	pNpc->GetAnimationControllers()->GetHitController()->Stun(stunTime, hitReactionSourceType);

	return ScriptValue(0);
}

SCRIPT_FUNC("npc-enable-stun", DcNpcEnableStun)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc	= args.NextNpc();
	bool enable = args.NextBoolean();

	if (pNpc)
	{
		pNpc->SetIsStunEnabled(enable);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-ambient-rim-light-enabled", DcNpcSetAmbientRimLightEnabled)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	Npc* pNpc = args.NextNpc();
	bool bEnableAmbientRimLight = args.NextBoolean();

	if (pNpc)
	{
		pNpc->m_enableAmbientRimLight = bEnableAmbientRimLight;
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-simple-wetness-enabled", DcNpcSetSimpleWetnessEnabled)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject* pNpcGo = args.NextGameObject();
	bool wetnessEnabled = args.NextBoolean();

	SimpleNpc* pSNpc = SimpleNpc::FromProcess(pNpcGo);
	if (pSNpc)
	{
		pSNpc->SetSimpleWetnessEnabled(wetnessEnabled);
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("npc-set-incoming-damage-multiplier", DcNpcSetIncomingDamageMultiplier)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	const float multiplier = args.NextFloat();

	if (pNpc)
	{
		if (IHealthSystem* pHealthSystem = pNpc->GetHealthSystem())
		{
			pHealthSystem->SetProtoDamageMultiplier(multiplier);
		}
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("npc-take-damage-from-position", DcNpcTakeDamageFromPosition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);
	Character* pChar = args.NextCharacter();
	const BoundFrame* pBoundFrame = args.NextBoundFrame();
	I32 damage = args.NextI32();
	DC::ProjectileType projectileType = args.NextI32();
	StringId64 impactAttachId = args.NextStringId();
	Point impactPoint = args.NextPoint();

	if (pChar && pBoundFrame)
	{
		ProjectileAttackInfo dummyInfo;

		dummyInfo.m_explosive = false;
		dummyInfo.m_impulseStrength = 0.0f;
		dummyInfo.m_projectileType = projectileType;
		dummyInfo.m_armorPiercing = true;
		dummyInfo.m_damageInfo.m_damageTotal = damage;
		dummyInfo.m_hSourceGameObj = nullptr;
		dummyInfo.m_type = DC::kAttackTypeProjectile;

		if (impactAttachId != INVALID_STRING_ID_64)
			dummyInfo.m_damageInfo.m_damagePoints[0].m_impactPt = pChar->GetAttachSystem()->GetAttachPositionById(impactAttachId);
		else
			dummyInfo.m_damageInfo.m_damagePoints[0].m_impactPt = impactPoint;

		Vector dir = SafeNormalize(dummyInfo.m_damageInfo.m_damagePoints[0].m_impactPt - pBoundFrame->GetTranslationWs(), kUnitZAxis);
		dummyInfo.m_damageInfo.m_damagePoints[0].m_impactDir = dir;
		dummyInfo.m_damageInfo.m_damagePoints[0].m_damage = damage;
		dummyInfo.m_damageInfo.m_numPoints = 1;


		SendEventAttack(&dummyInfo, pChar);
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("npc-set-on-fire", DcNpcSetOnFire)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
		pNpc->SetIsOnFire();

	return ScriptValue(0);
}

SCRIPT_FUNC("npc-put-out-fire", DcNpcPutOutFire)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Character* const pCharacter = args.NextCharacter();

	if (Npc* const pNpc = Npc::FromProcess(pCharacter))
	{
		pNpc->PutOutFire();
	}
	else if (ProcessRagdoll* const pRagdoll = ProcessRagdoll::FromProcess(pCharacter))
	{
		pRagdoll->PutOutFire();
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("npc-is-on-fire?", DcNpcIsOnFireP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	bool onFire = false;
	if (pNpc)
	{
		onFire = pNpc->IsOnFire();
	}

	return ScriptValue(onFire);
}

SCRIPT_FUNC("npc-is-cheap?", DcNpcIsCheapP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	return ScriptValue(pNpc && pNpc->IsCheap());
}

SCRIPT_FUNC("get-horde-spawn-count", DcGetHordeSpawnCount)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 0);
	extern I32 g_hordeSpawnCount;
	return ScriptValue(g_hordeSpawnCount);
}

SCRIPT_FUNC("npc-is-dog?", DcNpcIsDog)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc(true);

	bool isDog = false;
	if (pNpc)
	{
		isDog = pNpc->IsKindOf(SID("Dog"));
	}

	return ScriptValue(isDog);
}

SCRIPT_FUNC("npc-is-dog-owner?", DcNpcIsDogOwner)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc(true);

	bool isOwner  = false;
	bool petValid = false;

	AtomicLockJanitorRead jj(RelationshipManager::GetLock(), FILE_LINE_FUNC);
	const RelationshipManager::RelationshipInfo* pRelationship;
	if (RelationshipManager::Get().GetRelationship(pNpc, DC::kAiRelationshipTypeOwnerPet, pRelationship))
	{
		for (const RelationshipManager::RelationshipMember& member : pRelationship->m_members)
		{
			switch (member.m_memberType)
			{
			case DC::kAiRelationshipMemberTypeOwner:
				isOwner = member.m_hNpc.GetUserId() == pNpc->GetUserId();
				break;
			case DC::kAiRelationshipMemberTypePet:
				const Npc* pPet = member.m_hNpc.ToProcess();
				petValid		= pPet && !pPet->IsDead();
				break;
			}
		}
	}

	return ScriptValue(isOwner && petValid);
}

SCRIPT_FUNC("char-snapshot-get-object-locator", DcCharSnapshotGetObjectLocator)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const CharacterSnapshot* pSnapshot = args.NextPointer<const CharacterSnapshot>();

	if (pSnapshot)
	{

		BoundFrame* pRetLoc = NDI_NEW(kAllocSingleGameFrame, kAlign16) BoundFrame(pSnapshot->GetBoundFrame());

		return ScriptValue(pRetLoc);
	}

	return ScriptValue(nullptr);
}

SCRIPT_FUNC("char-snapshot-get-object-position", DcCharSnapshotGetObjectPosition)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	const CharacterSnapshot* pSnapshot = args.NextPointer<const CharacterSnapshot>();

	if (pSnapshot)
	{

		Point* pRetLoc = NDI_NEW(kAllocSingleGameFrame, kAlign16) Point(pSnapshot->GetTranslation());

		return ScriptValue(pRetLoc);
	}

	return ScriptValue(nullptr);
}

// doesn't work?
SCRIPT_FUNC("npc-get-in-vehicle", DcNpcGetInVehicle)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(0);
	DriveableVehicle* pDriveableVehicle = args.NextDriveableVehicle();
	if (pDriveableVehicle)
	{
		SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
		SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

		EnterVehicleArgs enterArgs;
		VehicleHandle hVehicle(pDriveableVehicle);
		enterArgs.m_vehicle	  = hVehicle;
		enterArgs.m_entryType = DC::kCarEntryTypeNormal;
		enterArgs.m_role	  = DC::kCarRiderRolePassengerBack;
		enterArgs.m_dryRun	  = false;
		enterArgs.m_spotId	  = SID("back");
		enterArgs.m_pFromJump = nullptr;
		pNpc->ScriptCommandMoveToAndEnterVehicle(pGroupInst, pTrackInst ? pTrackInst->GetTrackIndex() : 0, enterArgs);
		return ScriptValue(1);
	}
	return ScriptValue(0);
}

// npc get on horse func
SCRIPT_FUNC("npc-force-on-horse", DcNpcForceOnHorse)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 3);
	Npc* pNpc	  = args.NextNpc();
	Horse* pHorse = args.NextHorse();
	if (!pNpc)
	{
		args.MsgScriptError("not a valid NPC!\n");
		return ScriptValue(0);
	}
	else if (!pHorse)
	{
		args.MsgScriptError("not a valid horse!\n");
		return ScriptValue(0);
	}

	DC::HorseRiderPosition DcRiderPos	 = args.NextI32();
	HorseDefines::RiderPosition riderPos = HorseDefines::kRiderCount;
	if (DcRiderPos == DC::kHorseRiderPositionAuto)
		riderPos = pHorse->GetAvailableRiderPosition();
	else if (DcRiderPos == DC::kHorseRiderPositionFront)
		riderPos = HorseDefines::kRiderFront;
	else if (DcRiderPos == DC::kHorseRiderPositionBack)
		riderPos = HorseDefines::kRiderBack;
	if (riderPos == HorseDefines::kRiderCount)
	{
		args.MsgScriptError("horse is full!\n");
		return ScriptValue(0); // horse is full!
	}

	pHorse->SetNpcRiderImmediately(pNpc, FILE_LINE_FUNC, riderPos, true);
	return ScriptValue(0);
}

SCRIPT_FUNC("npc-force-off-horse", DcNpcForceOffHorse)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(0);
	Buddy* pBuddy = Buddy::FromProcess(pNpc);
	if (pBuddy)
	{
		pBuddy->CancelStartOnHorse();
	}
	bool skipAnim = !args.NextBoolean();
	pNpc->GetAnimationControllers()->GetRideHorseController()->DismountHorse(skipAnim);
	return ScriptValue(0);
}

SCRIPT_FUNC("wait-buddy-get-off-horse", DcWaitBudyGetOffHorse)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(0);

	SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

	if (pNpc->GetAnimationControllers()->GetRideHorseController()->WaitDismountHorse(pGroupInst,
																					 pTrackInst
																						 ? pTrackInst->GetTrackIndex()
																						 : 0))
	{
		SsTrackGroupInstance::WaitForTrackDone("wait-buddy-get-off-horse");
		ScriptContinuation::Suspend(argv);
	}
	return ScriptValue(0);
}

SCRIPT_FUNC("buddy-get-on-horse", DcBuddyGetOnHorse)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 5);
	Npc* pNpc	  = args.NextNpc();
	Horse* pHorse = args.NextHorse();
	if (!pNpc || !pHorse)
		return ScriptValue(0);

	DC::HorseRiderPosition DcRiderPos = args.NextI32();
	bool playAnim = args.NextBoolean();
	bool forceCameraCut = args.NextBoolean();

	// pHorse->SetNpcRiderImmediately(pNpc, FILE_LINE_FUNC, HorseDefines::kRiderBack);
	// return ScriptValue(1);

	SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

	EnterHorseArgs enterArgs;
	MutableHorseHandle hHorse(pHorse);
	HorseDefines::RiderPosition riderPos = HorseDefines::kRiderCount;
	if (DcRiderPos == DC::kHorseRiderPositionAuto)
		riderPos = pHorse->GetAvailableRiderPosition();
	else if (DcRiderPos == DC::kHorseRiderPositionFront)
		riderPos = HorseDefines::kRiderFront;
	else if (DcRiderPos == DC::kHorseRiderPositionBack)
		riderPos = HorseDefines::kRiderBack;

	if (riderPos == HorseDefines::kRiderCount)
		return ScriptValue(0); // horse is full!

	enterArgs.m_hHorse	 = hHorse;
	enterArgs.m_riderPos = riderPos;
	enterArgs.m_forceCameraCut = forceCameraCut;

	if (playAnim)
		pNpc->ScriptCommandMoveToAndEnterHorse(pGroupInst, pTrackInst ? pTrackInst->GetTrackIndex() : 0, enterArgs);
	else
		pHorse->SetNpcRiderImmediately(pNpc, FILE_LINE_FUNC, riderPos, true);
	return ScriptValue(0);
}

SCRIPT_FUNC("wait-buddy-get-on-horse", DcWaitBuddyGetOnHorse)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 4);
	Npc* pNpc	  = args.NextNpc();
	Horse* pHorse = args.NextHorse();
	DC::HorseRiderPosition DcRiderPos = args.NextI32();
	bool forceCameraCut = args.NextBoolean();

	if (!pNpc || !pHorse)
		return ScriptValue(0);


	SsTrackGroupInstance* pGroupInst = GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst		 = GetJobSsContext().GetTrackInstance();

	EnterHorseArgs enterArgs;
	MutableHorseHandle hHorse(pHorse);
	HorseDefines::RiderPosition riderPos = HorseDefines::kRiderCount;
	if (DcRiderPos == DC::kHorseRiderPositionAuto)
		riderPos = pHorse->GetAvailableRiderPosition();
	else if (DcRiderPos == DC::kHorseRiderPositionFront)
		riderPos = HorseDefines::kRiderFront;
	else if (DcRiderPos == DC::kHorseRiderPositionBack)
		riderPos = HorseDefines::kRiderBack;

	if (riderPos == HorseDefines::kRiderCount)
		return ScriptValue(0); // horse is full!
	enterArgs.m_hHorse	 = hHorse;
	enterArgs.m_riderPos = riderPos;
	enterArgs.m_forceCameraCut = forceCameraCut;

	if (pNpc->GetAnimationControllers()->GetRideHorseController()->WaitMountHorse(pGroupInst,
																				  pTrackInst
																					  ? pTrackInst->GetTrackIndex()
																					  : 0,
																				  enterArgs))
	{
		SsTrackGroupInstance::WaitForTrackDone("wait-buddy-get-on-horse");
		ScriptContinuation::Suspend(argv);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-disable-horse-aim-switch-sides-timeout", DcNpcDisableHorseAimSwitchSeatsTimeout)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue();

	IAiRideHorseController* pRideHorse = pNpc->GetAnimationControllers()->GetRideHorseController();
	HorseAimStateMachine* pHASM = pRideHorse ? pRideHorse->GetAimStateMachine() : nullptr;

	if (!pHASM)
		return ScriptValue();

	pHASM->ScriptDisableSwitchSidesTimeout(true);
	return ScriptValue();
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-enable-horse-aim-switch-sides-timeout", DcNpcEnableHorseAimSwitchSeatsTimeout)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (!pNpc)
		return ScriptValue();

	IAiRideHorseController* pRideHorse = pNpc->GetAnimationControllers()->GetRideHorseController();
	HorseAimStateMachine* pHASM = pRideHorse ? pRideHorse->GetAimStateMachine() : nullptr;

	if (!pHASM)
		return ScriptValue();

	pHASM->ScriptDisableSwitchSidesTimeout(false);
	return ScriptValue();
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("buddy-offer-gift", DcBuddyOfferGift)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc && pNpc->IsBuddyNpc())
	{
		Buddy* pBuddy = Buddy::FromProcess(pNpc);
		pBuddy->SetAllowBuddyGifting(true);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-custom-post-selector-bound-frame", DcSetCustomPostSelectorBoundFrame)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	const BoundFrame* pBoundFrame = args.NextBoundFrame();

	if (pNpc && pBoundFrame)
	{
		pNpc->SetCustomPostSelectorBoundFrame(*pBoundFrame);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("buddy-stop-gifting", DcBuddyStopGifting)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc && pNpc->IsBuddyNpc())
	{
		Buddy* pBuddy = Buddy::FromProcess(pNpc);
		pBuddy->SetAllowBuddyGifting(false);
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(bool, DcIsStunHitReactionPlayingP, (Character & p), "is-stun-hit-reaction-playing?")
{
	if (p.IsKindOf(g_type_Npc))
	{
		const Npc* pNpc = static_cast<Npc*>(&p);
		bool blocking	= pNpc->GetAnimationControllers()->GetHitController()->IsStunReactionPlaying();

		return blocking;
	}
	else if (Player *pPlayer = Player::FromProcess(&p))
	{
		if (pPlayer->m_pShotReactionController->IsBusy() && pPlayer->m_pShotReactionController->GetLastHitReaction())
		{
			DC::PlayerHitReactionStrengthType strengthType = pPlayer->m_pShotReactionController->GetLastHitReaction()->m_strengthType;
			return strengthType & (DC::kPlayerHitReactionStrengthTypeWeakStun | DC::kPlayerHitReactionStrengthTypeStrongStun);
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-full-body-hit-reaction-playing?", DcIsFullBodyHitReactionPlaying)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc	  = args.NextNpc();
	DC::HitReactionStateMask mask = (DC::HitReactionStateMask) args.NextU64();

	if (pNpc)
	{
		if (IAiHitController* pHitController = pNpc->GetAnimationControllers()->GetHitController())
		{
			return ScriptValue(pHitController->IsFullBodyReactionPlaying(mask));
		}
	}
	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(bool, DcIsMeleeHitReactionPlayingP, (Character & p), "is-melee-hit-reaction-playing?")
{
	if (p.IsKindOf(g_type_Npc))
	{
		const Npc* pNpc = static_cast<Npc*>(&p);
		bool blocking	= false;

		if (pNpc->GetCurrentMeleeAction() && pNpc->GetCurrentMeleeAction()->GetDefender() == pNpc
			&& pNpc->GetCurrentMeleeAction()->IsHitReaction())
		{
			blocking = true;
		}

		return blocking;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(DC::AiPost*,
								 DcNpcGetIdealPost,
								 (Character & p, DC::AiPostSelector selector),
								 "npc-get-ideal-post")
{
	if (!p.IsKindOf(g_type_Npc))
		return nullptr;

	const Npc* pNpc = static_cast<Npc*>(&p);

	AiPost post;
	const bool success = pNpc->GetIdealPost(selector, post);
	if (!success)
		return nullptr;

	if (!post.IsValid())
		return nullptr;

	DC::AiPost* pPost = NDI_NEW(kAllocSingleGameFrame, kAlign16) DC::AiPost;

	pPost->m_position = NDI_NEW(kAllocSingleGameFrame, kAlign16) Point(post.GetPositionWs());
	pPost->m_type	  = post.GetType();
	pPost->m_id		  = post.GetPostId();
	pPost->m_score	  = post.GetScore();
	pPost->m_spawnerId = post.GetPostDef().GetSpawnerId();

	return pPost;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("ai-get-post", DcAiGetPost)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	U32 postId = args.NextU32();

	if (!AiPostMgr::Get().IsPostIdValid(postId))
		return ScriptValue(nullptr);

	const AiPostDef& postDef = AiPostMgr::Get().GetPostDef(postId);
	DC::AiPost* pPost		 = NDI_NEW(kAllocSingleGameFrame, kAlign16) DC::AiPost;
	if (args.AllocSucceeded(pPost))
	{
		pPost->m_position = NDI_NEW(kAllocSingleGameFrame, kAlign16) Point(postDef.GetPositionWs());
		pPost->m_type	  = postDef.GetType();
		pPost->m_id		  = postId;
		pPost->m_spawnerId = postDef.GetSpawnerId();
	}
	return ScriptValue(pPost);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("ai-get-cover-post-animate-exit-type", DcAiGetCoverPostType)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	const U32 postId = args.NextU32();

	if (!AiPostMgr::Get().IsPostIdValid(postId))
		return ScriptValue(DC::kAnimateExitNone);

	const AiPostDef& postDef = AiPostMgr::Get().GetPostDef(postId);
	const CoverActionPack* pCover = postDef.GetActionPack().ToActionPack<CoverActionPack>();
	if (!pCover)
		return ScriptValue(DC::kAnimateExitNone);

	switch(pCover->GetDefinition().m_coverType)
	{
	case CoverDefinition::kCoverCrouchLeft:		return ScriptValue(DC::kAnimateExitCoverLowLeft);
	case CoverDefinition::kCoverCrouchRight:	return ScriptValue(DC::kAnimateExitCoverLowRight);
	case CoverDefinition::kCoverStandLeft:		return ScriptValue(DC::kAnimateExitCoverHighLeft);
	case CoverDefinition::kCoverStandRight:		return ScriptValue(DC::kAnimateExitCoverHighRight);
	}

	return ScriptValue(DC::kAnimateExitNone);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(void,
								 DcNpcOverridePostSelectorSetId,
								 (Npc& p, StringId64 selectorSetId),
								 "npc-override-post-selector-set-id")
{
	p.OverridePostSelectorSetId(AiLogicControl::kLogicPriorityScript, selectorSetId, FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(void, DcNpcClearPostSelectorSetId, (Npc& p), "npc-clear-post-selector-set-id")
{
	p.ClearPostSelectorSetId(AiLogicControl::kLogicPriorityScript, FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(StringId64, DcGetPostSelectorSetId, (Character & p), "npc-get-post-selector-set-id")
{
	if (!p.IsKindOf(g_type_Npc))
		return INVALID_STRING_ID_64;

	Npc* pNpc = static_cast<Npc*>(&p);
	return pNpc->GetPostSelectorSetId();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function get-closest-post-id
		(
			(location			bound-frame)
			(post-type-mask		ai-post-type-mask)
		)
		none
	)
*/
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(U32,
								 DcGetClosestPostId,
								 (const BoundFrame* pBoundFrame, DC::AiPostTypeMask postTypeMask),
								 "get-closest-post-id")
{
	if (!pBoundFrame)
	{
		return AiPostDef::kInvalidPostId;
	}

	const U16 postId = AiLibUtil::FindClosestPostWs(postTypeMask, pBoundFrame->GetTranslation(), 10.0f);
	return postId;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(void,
								 DcNpcMirrorLayer,
								 (NdGameObject & npc, NdGameObject& target, StringId64 remapId, F32 blend),
								 "npc-mirror-layer")
{
	if (Npc* pNpc = Npc::FromProcess(&npc))
	{
		//pNpc->MirrorLayer(nullptr, INVALID_STRING_ID_64, INVALID_STRING_ID_64, blend);
		pNpc->MirrorLayer(&target, remapId, SID("base"), blend);
	}
	else if (SimpleNpc* pSNpc = SimpleNpc::FromProcess(&npc))
	{
		//pSNpc->MirrorLayer(nullptr, INVALID_STRING_ID_64, INVALID_STRING_ID_64, blend);
		pSNpc->MirrorLayer(&target, remapId, SID("base"), blend);
	}

}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(void, DcNpcStopMirrorLayer, (NdGameObject & npc, F32 blend), "npc-stop-mirror-layer")
{
	if (Npc* pNpc = Npc::FromProcess(&npc))
	{
		pNpc->MirrorLayer(nullptr, INVALID_STRING_ID_64, INVALID_STRING_ID_64, blend);
	}
	else if (SimpleNpc* pSNpc = SimpleNpc::FromProcess(&npc))
	{
		pSNpc->MirrorLayer(nullptr, INVALID_STRING_ID_64, INVALID_STRING_ID_64, blend);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-set-hit-reaction-set-id ((npc-name symbol) (set-id symbol))
		none)
*/
SCRIPT_FUNC("npc-set-hit-reaction-set-id", DcNpcSetHitReactionSetId)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	const StringId64 setId = args.NextStringId();

	if (pNpc)
	{
		NdAnimControllerConfig* pConfig = pNpc->GetAnimControllerConfig();

		pConfig->m_scriptOverrideHitReaction.m_hitReactionSetId = setId;
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	(define-c-function npc-set-death-reaction-set-id ((npc-name symbol) (set-id symbol))
		none)
*/
SCRIPT_FUNC("npc-set-death-reaction-set-id", DcNpcSetDeathReactionSetId)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();
	const StringId64 setId = args.NextStringId();

	if (pNpc)
	{
		NdAnimControllerConfig* pConfig = pNpc->GetAnimControllerConfig();

		pConfig->m_scriptOverrideHitReaction.m_deathReactionSetId = setId;
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-get-cloth-process
(
(npc-name string)
)
none
)
*/
SCRIPT_FUNC("npc-get-cloth-process", DcNpcGetClothProcess)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);
	Npc* pNpc = args.NextNpc();

	if (pNpc)
	{
		if (const PropInventory* pPropInv = pNpc->GetPropInventory())
		{
			I32F propIndex = pPropInv->GetPropByType(DC::kNdPropTypeCloth);
			if (propIndex >= 0)
			{
				if (const NdAttachableObject* pObj = pPropInv->GetPropObject(propIndex))
				{
					return ScriptValue(pObj->GetUserId());
				}
			}
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-set-health-bar-visible ((npc symbol) (show? boolean))
	none)
*/
SCRIPT_FUNC("npc-set-health-bar-visible", DcNpcSetHealthBarVisible)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc	 = args.NextNpc();
	bool visible = args.NextBoolean();

	if (pNpc)
	{
		pNpc->SetHealthBarEnabled(visible);
	}
	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
(define-c-function npc-set-health-bar-tint ((npc symbol) (tint color))
	none)
*/
SCRIPT_FUNC("npc-set-health-bar-tint", DcNpcSetHealthBarTint)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc  = args.NextNpc();
	Color tint = args.NextColor();

	if (pNpc)
	{
		bool success = pNpc->SetHealthBarTint(tint);
		if (!success)
			args.MsgScriptError("Could not set tint of %s's health bar, most likely they do not have one! (see npc-show-health-bar)\n",
								DevKitOnly_StringIdToString(pNpc->GetUserId()));
	}
	return ScriptValue(0);
}

/*
(define-c-function npc-visible-in-listen-mode? ((npc-name symbol) (allow-listen-mode-disabled? boolean (:default #f)))
	boolean)
*/

SCRIPT_FUNC("npc-visible-in-listen-mode?", DcNpcVisibleInListenMode)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	bool allowWhenListenModeDisabled = args.NextBoolean();
	if (!pNpc)
		return ScriptValue(false);

	bool result = g_listenModeMgr.IsVisibleInListenMode(pNpc, allowWhenListenModeDisabled);
	return ScriptValue(result);
}

/*
(define-c-function npc-get-estimated-listen-mode-reveal-intensity ((npc-name symbol) (allow-listen-mode-disabled? boolean (:default #f)))
	float)
*/

SCRIPT_FUNC("npc-get-estimated-listen-mode-reveal-intensity", DcNpcGetEstimatedListenModeRevealIntensity)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	bool allowWhenListenModeDisabled = args.NextBoolean();
	if (!pNpc)
		return ScriptValue(0.0f);

	if (!g_listenModeMgr.IsVisibleInListenMode(pNpc, allowWhenListenModeDisabled))
		return ScriptValue(0.0f);

	const float rawIntensity = g_listenModeMgr.GetEstimatedIntensity(pNpc->GetProcessId(), false);
	const float intensity = LerpScale(0.0f, 0.40f, 0.0f, 1.0f, rawIntensity);
	return ScriptValue(intensity);
}

SCRIPT_FUNC("npc-set-listen-mode-sounds-settings", DcNpcSetListenModeSoundsSettings)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	StringId64 settingsId = args.NextStringId();

	if (pNpc)
		pNpc->SetListenModeSoundsSettings(settingsId);

	return ScriptValue(0);
}

SCRIPT_FUNC("listen-mode-set-buddy-draw-mode", DcListenModeSetBuddyDrawMode)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pObject = args.NextGameObject();
	bool useBuddyDraw	  = args.NextBoolean();

	if (IDrawControl* pDrawControl = pObject->GetDrawControl())
	{
		if (useBuddyDraw)
		{
			pDrawControl->OrInstanceFlag(FgInstance::kListenModeBuddyDraw);
		}
		else
		{
			pDrawControl->ClearInstanceFlag(FgInstance::kListenModeBuddyDraw);
		}
	}

	if (Npc* pNpc = Npc::FromProcess(pObject))
	{
		if (PropInventory* pPropInventory = pNpc->GetPropInventory())
		{
			for (int iProp = 0; iProp < pPropInventory->GetNumProps(); iProp++)
			{
				if (NdGameObject* pProp = pPropInventory->GetPropObject(iProp))
				{
					if (IDrawControl* pPropDraw = pProp->GetDrawControl())
					{
						if (useBuddyDraw)
						{
							pPropDraw->OrInstanceFlag(FgInstance::kListenModeBuddyDraw);
						}
						else
						{
							pPropDraw->ClearInstanceFlag(FgInstance::kListenModeBuddyDraw);
						}
					}
				}
			}
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("character-get-vox-name", DcCharacterGetVoxName)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pGo = args.NextGameObject();
	if (const Character* pCharacter = Character::FromProcess(pGo))
	{
		return ScriptValue(pCharacter->GetCharacterName());
	}
	else if (SimpleNpc* pSNpc = SimpleNpc::FromProcess(pGo))
	{
		return ScriptValue(pSNpc->GetCharacterName());
	}
	return ScriptValue("");
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-reset-knowledge-debug-only-beau-hack", DcNpcResetKnowledgeDebugOnlyBeauHack)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

#ifdef FINAL_BUILD
	ALWAYS_ASSERTF(false,
				   ("npc-reset-knowledge-debug-only-beau-hack is for debug only and shouldn't be used in final game"));
#endif

	Npc* pNpc = args.NextNpc();
	if (!pNpc)
		return ScriptValue(false);

	AiKnowledge* pKnow = pNpc->GetKnowledge();
	if (!pKnow)
		return ScriptValue(false);

	pKnow->DestroyEntities();

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool NpcRequestIdlePerformanceInternal(Npc* pNpc, StringId64 animId, bool wait, IStringBuilder* pErrStr)
{
	if (!pNpc)
	{
		return false;
	}

	if (animId == INVALID_STRING_ID_64)
	{
		return false;
	}

	INavAnimController* pNavAnimController = pNpc->GetActiveNavAnimController();
	if (!pNavAnimController)
	{
		return false;
	}

	SsAction waitAction;
	SsAction* pWaitAction = nullptr;

	if (wait)
	{
		const SsContext& jobSsContext = GetJobSsContext();

		if (SsTrackGroupInstance* pGroupInst = jobSsContext.GetGroupInstance())
		{
			SsTrackInstance* pTrackInst = jobSsContext.GetTrackInstance();
			const U32F trackIndex		= pTrackInst ? pTrackInst->GetTrackIndex() : 0;
			MutableProcessHandle hTgProcess = pGroupInst->GetTrackGroupProcessHandle();

			waitAction.SetOwner(pNpc);
			waitAction.Start(hTgProcess, trackIndex);
			pWaitAction = &waitAction;

			pGroupInst->WaitForTrackDone("wait-npc-request-idle-performance");
		}
	}

	INavAnimController::IdlePerfParams params;
	params.m_pWaitAction = pWaitAction;
	params.m_fadeTime = 1.0f; // [2018-02-18] per michal

	const bool success = pNavAnimController->RequestIdlePerformance(animId, params, pErrStr);

	if (pWaitAction && !success)
	{
		pWaitAction->Stop();
	}

	return success;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-request-idle-performance", DcNpcRequestIdlePerformance)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	StringBuilder<256> errStr;

	Npc* pNpc = args.NextNpc();
	const StringId64 animId = args.NextStringId();
	const bool success		= NpcRequestIdlePerformanceInternal(pNpc, animId, false, &errStr);

	if (!success)
	{
		args.MsgScriptError("Npc '%s' failed to play idle performance '%s': %s\n",
							pNpc ? pNpc->GetName() : "<null>",
							DevKitOnly_StringIdToString(animId),
							errStr.c_str());
	}

	return ScriptValue(success);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("wait-npc-request-idle-performance", DcWaitNpcRequestIdlePerformance)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);

	StringBuilder<256> errStr;

	Npc* pNpc = args.NextNpc();
	const StringId64 animId = args.NextStringId();
	const bool success		= NpcRequestIdlePerformanceInternal(pNpc, animId, true, &errStr);

	if (!success)
	{
		args.MsgScriptError("Npc '%s' failed to play idle performance '%s': %s\n",
							pNpc->GetName(),
							DevKitOnly_StringIdToString(animId),
							errStr.c_str());
	}

	return ScriptValue(success);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-is-playing-idle-performance?", DcNpcIsPlayingIdlePerformanceP)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	const Npc* pNpc = args.NextNpc();
	const INavAnimController* pNavAnimController = pNpc ? pNpc->GetActiveNavAnimController() : nullptr;

	const bool playing = pNavAnimController ? pNavAnimController->IsPlayingIdlePerformance() : false;

	return ScriptValue(playing);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-estimate-remaining-travel-time", DcNpcEstimateRemainingTravelTime)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	const Npc* pNpc = args.NextNpc();
	if (!pNpc || !pNpc->IsNavigationInProgress())
	{
		return ScriptValue(-1.0f);
	}

	const INavAnimController* pNavController = pNpc->GetActiveNavAnimController();

	const float timeEst = pNavController ? pNavController->EstimateRemainingTravelTime() : -1.0f;

	return ScriptValue(timeEst);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(void,
								 TargetAddScriptedNpcTargetWeight,
								 (NdGameObject * pObj, StringId64 id, F32 weight),
								 "target-add-scripted-npc-target-weight")
{
	if (PlayerBase* pPlayer = PlayerBase::FromProcess(pObj))
	{
		return pPlayer->AddScriptedNpcTargetWeight(id, weight);
	}
	else if (pObj)
	{
		MsgConScriptError("Scripted Npc Target Weight Only Supports Players or NetCharacters, (%s) type %s unsupported",
						  DevKitOnly_StringIdToString(pObj->GetUserId()),
						  pObj->GetTypeName());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(F32,
								 TargetGetScriptedNpcTargetWeight,
								 (NdGameObject * pObj, StringId64 id),
								 "target-get-scripted-npc-target-weight")
{
	if (const PlayerBase* pPlayer = PlayerBase::FromProcess(pObj))
	{
		F32 weight = 0.0f;
		if (pPlayer->FindScriptedNpcTargetWeightById(id, &weight))
		{
			return weight;
		}
		return 0.0f;
	}
	else if (pObj)
	{
		MsgConScriptError("Scripted Npc Target Weight Only Supports Players or NetCharacters, (%s) type %s unsupported",
						  DevKitOnly_StringIdToString(pObj->GetUserId()),
						  pObj->GetTypeName());
	}

	return 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(void,
								 TargetClearScriptedNpcTargetWeight,
								 (NdGameObject * pObj, StringId64 id),
								 "target-clear-scripted-npc-target-weight")
{
	if (PlayerBase* pPlayer = PlayerBase::FromProcess(pObj))
	{
		return pPlayer->ClearScriptedNpcTargetWeight(id);
	}
	else if (pObj)
	{
		MsgConScriptError("Scripted Npc Target Weight Only Supports Players or NetCharacters, (%s) type %s unsupported",
						  DevKitOnly_StringIdToString(pObj->GetUserId()),
						  pObj->GetTypeName());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(F32, NpcGetTimeSincePlayerRaycastPassed, (), "npc-get-time-since-player-raycast-passed")
{
	return EngineComponents::GetNpcManager()->GetTimeSincePlayerVisionRaycastClear();
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("holster-npc-weapon-physically", DcHolsterNpcWeaponPhysically)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	NdGameObject* pWeaponObj = args.NextGameObject();
	float weaponBlendTime = args.NextFloat();

	if (pNpc != nullptr && pWeaponObj != nullptr)
	{
		ProcessWeaponBase* pWeapon = ProcessWeaponBase::FromProcess(pWeaponObj);
		if (pWeapon != nullptr)
		{
			IAiWeaponController* pWeaponController = pNpc->GetAnimationControllers()->GetWeaponController();
			if (pWeaponController != nullptr)
			{
				pWeaponController->EnforceGunState(pWeapon, kGunStateHolstered, false, weaponBlendTime, true);
			}
		}
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-set-allow-adjust-to-other-chars", DcNpcSetAllowAdjustToOtherChars)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 2);
	Npc* pNpc = args.NextNpc();
	const bool allowAdj = args.NextBoolean();

	if (pNpc)
	{
		pNpc->SetAdjustToOtherCharacters(allowAdj);
	}

	return ScriptValue(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("npc-abandon-interrupted-nav-command", DcNpcAbandonInterruptedNavCommand)
{
	GAME_SCRIPT_ARG_ITERATOR(args, 1);

	if (Npc* pNpc = args.NextNpc())
	{
		pNpc->AbandonInterruptedNavCommand();
	}

	return ScriptValue(false);
}
