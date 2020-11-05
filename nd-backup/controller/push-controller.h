/*
* Copyright (c) 2015 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#ifndef PUSH_CONTROLLER_H
#define PUSH_CONTROLLER_H

#include "gamelib/gameplay/ai/controller/animaction-controller.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class IAiPushController : public AnimActionController
{
public:
	virtual void RequestPush(const BoundFrame& grabAp, MutableNdGameObjectHandle hMainPusher) = 0;
	virtual void StopPush() = 0;
	virtual bool IsPushing() const = 0;
	virtual void UpdateInput(Vector_arg pushDirection, float pushIntensity, float animSpeedScale) = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
IAiPushController* CreateAiPushController();

#endif // PUSH_CONTROLLER_H
