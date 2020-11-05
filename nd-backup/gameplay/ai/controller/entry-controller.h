/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/anim/anim-action.h"

#include "gamelib/gameplay/ai/controller/action-pack-controller.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AiEntryController : public ActionPackController
{
public:
	typedef ActionPackController ParentClass;

	virtual void Enter(const ActionPackResolveInput& input,
					   ActionPack* pActionPack,
					   const ActionPackEntryDef& entryDef) override;
	virtual void Exit(const PathWaypointsEx* pExitPathPs) override;
	virtual bool RequestAbortAction() override;

	virtual bool ResolveEntry(const ActionPackResolveInput& input,
							  const ActionPack* pActionPack,
							  ActionPackEntryDef* pDefOut) const override;

	virtual bool ResolveDefaultEntry(const ActionPackResolveInput& input,
									 const ActionPack* pActionPack,
									 ActionPackEntryDef* pDefOut) const override;

	virtual bool UpdateEntry(const ActionPackResolveInput& input,
							 const ActionPack* pActionPack,
							 ActionPackEntryDef* pDefOut) const override;

	virtual void RequestAnimations() override;
	virtual void UpdateStatus() override;
	virtual bool IsBusy() const override;

	virtual void DebugDrawEntries(const ActionPackResolveInput& input, const ActionPack* pActionPack) const override;

private:

	AnimActionWithSelfBlend m_animAction;

	Locator m_finalAlignPs;
	Vector m_targetVelPs;
};

/// --------------------------------------------------------------------------------------------------------------- ///
AiEntryController* CreateAiEntryController();
