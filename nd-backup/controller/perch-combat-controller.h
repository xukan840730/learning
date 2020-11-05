/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef PERCH_COMBAT_CONTROLLER_H
#define PERCH_COMBAT_CONTROLLER_H

#include "gamelib/gameplay/ai/controller/action-pack-controller.h"
#include "gamelib/gameplay/nav/perch-action-pack.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class IAiPerchController : public ActionPackController
{
public:
	virtual bool RequestInvestigateFront() = 0;
	virtual bool RequestInvestigateDown() = 0;
	virtual bool RequestInvestigateDeadBody() = 0;
	virtual bool IsBusyInvestigating() const = 0;

	virtual void SetDebugPerchApEnterIndex(I32F val) = 0;
	virtual I32F GetPerchApEnterAnimCount(const PerchActionPack* pPerchAp) const = 0;

	virtual void NotifyDemeanorChange() = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiPerchController* CreateAiPerchController();

#endif	//PERCH_COMBAT_CONTROLLER_H
