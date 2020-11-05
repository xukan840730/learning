/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/math/gamemath.h"

#include "ndlib/anim/anim-action.h"
#include "ndlib/util/tracker.h"

#include "gamelib/state-script/ss-animate.h"
#include "gamelib/gameplay/ai/controller/animaction-controller.h"
#include "gamelib/gameplay/character-leg-ik.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class Npc;
struct SsAnimateParams;
namespace DC
{
	struct AnimNpcScriptInfo;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class AiScriptController : public SsAnimateController, public AnimActionController
{
public:
	explicit AiScriptController(NdGameObject& npc,
								StringId64 layerId,
								bool additive		= false,
								LayerType layerType = kStateLayer);

	// Overridden from AnimActionController
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void UpdateStatus() override;

	bool CanPushPlayer() const { return !m_noPushPlayer; }

	// Overridden from SsAnimateController
	virtual bool IsBusy() const override;

protected:
	Npc& GetCharacter() const;

	bool m_isBusy;
	bool m_noPushPlayer;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AiSimpleScriptController : public AiScriptController
{
public:
	explicit AiSimpleScriptController(NdGameObject& npc, StringId64 layerId, bool additive);

	virtual void UpdateStatus() override;
	virtual bool ShouldInterruptNavigation() const override { return IsBusy(); }

protected:
	virtual bool IsBusy() const override;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AiFullBodyScriptController : public AiScriptController
{
public:
	explicit AiFullBodyScriptController(NdGameObject& npc);

	virtual void Reset() override;

	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;
	virtual void UpdateProcedural() override;
	virtual bool ShouldInterruptNavigation() const override { return IsBusy(); }

	virtual bool	IsPlaying() const override;
	virtual Error	RequestStartAnimation(const SsAnimateParams& params, MutableProcessHandle hSourceProcess) override;

protected:
	DC::AnimNpcScriptInfo&			GetNpcScriptInfo() const;

private:
	virtual DC::AnimScriptInfo&		HookGetScriptInfo() const override;
	virtual StringId64				HookGetPluggableStateId(const SsAnimateParams& params, bool isLooping) const override;
	virtual bool					HookEnterScriptState() override;
	virtual bool					HookFadeToState(const SsAnimateParams& params,
													StringId64 stateId,
													bool additive,
													BoundFrame* pBoundFrame,
													StateChangeRequest::ID& resultChangeId) override;
	virtual void					HookExitScriptState(bool holdCharactersInScriptState) override;
	virtual void					HookFadeOutLayer() override;
	virtual void					OnAnimationDone() override;
	virtual bool					HookIsInScriptState() const override;
	virtual CameraAnimationPriority	HookGetCameraUpdatePriority() override { return kCameraAnimationPriorityNpc; }
};
