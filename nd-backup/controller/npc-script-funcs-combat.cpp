/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "corelib/util/random.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/process/event.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/spawn-info.h"
#include "ndlib/render/draw-control.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/script/script-callback.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/scriptx/h/dc-types.h"
#include "ndlib/settings/settings.h"

#include "gamelib/audio/nd-vox-controller-base.h"
#include "gamelib/camera/camera-manager.h"
#include "gamelib/gameplay/ai/agent/nav-character-util.h"
#include "gamelib/gameplay/ai/controller/animaction-controller.h"
#include "gamelib/gameplay/character-manager.h"
#include "gamelib/gameplay/nav/action-pack-mgr.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/gameplay/nav/cover-action-pack.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"
#include "gamelib/gameplay/nav/nav-control.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/platform-control.h"
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

#include "common/stats/stat-manager.h"
#include "common/ai/simple-npc.h"

#include "game/weapon/turret.h"
#include "game/ai/action-pack/cinematic-action-pack.h"
#include "game/ai/action-pack/turret-action-pack.h"
#include "game/ai/agent/npc-manager.h"
#include "game/ai/agent/npc.h"
#include "game/ai/agent/npc-tactical.h"
#include "game/ai/base/ai-game-debug.h"
#include "game/ai/behavior/behavior-cover.h"
#include "game/ai/behavior/behavior-move-to.h"
#include "game/ai/behavior/behavior-shoot-moving.h"
#include "game/ai/behavior/behavior-shoot-stationary.h"
#include "game/ai/behavior/behavior-turn-to-face.h"
#include "game/ai/characters/buddy.h"
#include "game/ai/component/buddy-combat-logic.h"
#include "game/ai/look-aim/look-aim-buddy.h"
#include "game/ai/look-aim/look-aim-search.h"
#include "game/ai/look-aim/look-aim-sniper.h"
#include "game/ai/look-aim/look-aim-watch.h"
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
#include "game/ai/knowledge/entity.h"
#include "game/ai/knowledge/knowledge.h"
#include "game/ai/knowledge/target-appraiser.h"
#include "game/ai/melee/melee-behavior-condition.h"
#include "game/ai/requests/requests.h"
#include "game/ai/skill/investigate-skill.h"
#include "game/ai/skill/scripted-skill.h"
#include "game/ai/skill/skill-mgr.h"
#include "game/audio/dialog-manager.h"
#include "game/character-util.h"
#include "game/event-attack.h"
#include "game/gameinfo.h"
#include "game/nav/nav-game-util.h"
#include "game/net/net-game-manager.h"
#include "game/player/player-targeting.h"
#include "game/player/player.h"
#include "game/process-ragdoll.h"
#include "game/render/gui/hud.h"
#include "game/script-arg-iterator.h"
#include "game/script-funcs.h"
#include "game/scriptx/h/ai-weapon-defines.h"
#include "game/scriptx/h/anim-npc-info.h"
#include "game/scriptx/h/melee-defines.h"
#include "game/tasks/task-manager.h"
#include "game/tasks/task.h"
#include "game/vehicle-ctrl.h"
#include "game/weapon/process-weapon.h"
#include "game/weapon/process-weapon-base.h"
#include "game/weapon/projectile-base.h"
#include "game/weapon/projectile-throwable.h"
#include "game/weapon/process-weapon-strap.h"

//////////////////////////////////////////////////////////////////////////
// Combat Params Functions
//////////////////////////////////////////////////////////////////////////

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(void, DcNpcSetCombatParams, (Character& character, StringId64 combatParamsId), "npc-set-combat-params")
{
	if (Npc* pNpc = Npc::FromProcess(&character))
	{
		AiLogScript(pNpc, "npc-set-combat-params %s", DevKitOnly_StringIdToString(combatParamsId));
		pNpc->GetLogicControl().RequestControl(AiLogicControl::kLogicPriorityScript, SID("combat-params"), BoxedValue(combatParamsId), FILE_LINE_FUNC);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(void, DcNpcResetCombatParams, (Character& character), "npc-reset-combat-params")
{
	if (Npc* pNpc = Npc::FromProcess(&character))
	{
		AiLogScript(pNpc, "npc-reset-combat-params");
		pNpc->GetLogicControl().ClearControl(AiLogicControl::kLogicPriorityScript, SID("combat-params"), FILE_LINE_FUNC);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(StringId64, DcNpcGetCombatParams, (Character& character), "npc-get-combat-params-id")
{
	if (const Npc* pNpc = Npc::FromProcess(&character))
	{
		return pNpc->GetGameLogicControl().GetCombatParamsId();
	}

	return INVALID_STRING_ID_64;
}

//////////////////////////////////////////////////////////////////////////
// Sniper Params Functions
//////////////////////////////////////////////////////////////////////////

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(void, DcNpcSetSniperParams, (Character& character, StringId64 sniperParamsId), "npc-set-sniper-params")
{
	if (Npc* pNpc = Npc::FromProcess(&character))
	{
		AiLogScript(pNpc, "npc-set-sniper-params %s", DevKitOnly_StringIdToString(sniperParamsId));
		pNpc->GetLogicControl().RequestControl(AiLogicControl::kLogicPriorityScript, SID("sniper-params"), BoxedValue(sniperParamsId), FILE_LINE_FUNC);
	}
}

//////////////////////////////////////////////////////////////////////////
// Targeting Params Functions
//////////////////////////////////////////////////////////////////////////

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_REGISTER_FUNC_AUTO_PARAMS(void, DcNpcSetTargetingParams, (Character& character, StringId64 targetingParamsId), "npc-set-targeting-params")
{
	if (Npc* pNpc = Npc::FromProcess(&character))
	{
		AiLogScript(pNpc, "npc-set-targeting-params %s", DevKitOnly_StringIdToString(targetingParamsId));
		pNpc->GetLogicControl().RequestControl(AiLogicControl::kLogicPriorityScript, SID("targeting-params"), BoxedValue(targetingParamsId), FILE_LINE_FUNC);

		// if (AiTargetAppraiser* pAppraiser = pNpc->GetTargetAppraiser())
		// {
		// 	pAppraiser->SetTargetingParamsId(targetingParamsId);
		// }
	}
}

