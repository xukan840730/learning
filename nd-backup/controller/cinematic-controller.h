/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/gameplay/ai/controller/nd-cinematic-controller.h"

#include "game/ai/action-pack/process-cinematic-action-pack.h"

class ActionPack;
class CinematicActionPack;

namespace DC
{
	struct CineActionPackDef;
	struct CineActionPackPerformancePartial;
	struct ApEntryItemList;
	struct CapAnimLoop;
} // namespace DC

/// --------------------------------------------------------------------------------------------------------------- ///
class IAiCinematicController : public INdAiCinematicController
{
public:
	virtual I32F GetCapEnterAnimCount(const CinematicActionPack* pCap) const = 0;
	virtual I32F GetCapExitAnimCount(const CinematicActionPack* pCap) const	 = 0;

	// Commands.
	 // auto mode is the way it should be... BehaviorUseCap is the old non-auto way!
	virtual void SetAutomaticMode(bool enableAutomaticMode) = 0;
	virtual bool StartUsing(bool first) = 0;
	virtual void StartWait(StringId64 waitAnimId, StringId64 waitPropAnimId = INVALID_STRING_ID_64) = 0;
	virtual bool IsLoopComplete() const = 0;
	virtual bool IsInValidStateToStartLoop() const = 0;

	virtual void PlayPartial(const CinematicActionPack* pActionPack,
							 const DC::CineActionPackPerformancePartial* pPerformancePartial) = 0;
	virtual bool IsPlaying() const = 0;
	virtual bool NoLegIk() const   = 0;
	virtual bool IsAbortRequested() const = 0;

	virtual void ForceAbortAction() = 0;

	virtual void SetCapProcess(MutableProcessCinematicActionPackHandle hCapProcess) = 0;

	virtual void TryDropProp(CinematicActionPack* pCineAp) = 0;

	static const DC::ApEntryItemList* GetApEntryListFor(const CinematicActionPack* pCineAp, const Process* pCharacter);
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiCinematicController* CreateAiCinematicController();
