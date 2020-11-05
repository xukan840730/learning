/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef TURRET_CONTROLLER_H
#define TURRET_CONTROLLER_H

#include "gamelib/gameplay/ai/controller/action-pack-controller.h"

class TurretActionPack;
class Turret;

/// --------------------------------------------------------------------------------------------------------------- ///
class IAiTurretController : public ActionPackController
{
public:
	// Commands.
	virtual void Reload(Turret* pTurret) = 0;
	virtual void Fire() = 0;
	virtual bool TryAbortReload() = 0;

	virtual void StepLeft() = 0;
	virtual void StepRight() = 0;

	virtual void SetAimPositionWs(Point_arg aimPosWs, Vector_arg aimVelWs) = 0;

	virtual bool IsInTurretState() const = 0;
};
/// --------------------------------------------------------------------------------------------------------------- ///
IAiTurretController* CreateAiTurretController();

#endif	// TURRET_CONTROLLER_H
