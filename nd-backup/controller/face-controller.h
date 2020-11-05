/*
 * Copyright (c) 2009 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef _FACE_CONTROLLER_H_
#define _FACE_CONTROLLER_H_

#include "gamelib/gameplay/ai/controller/animaction-controller.h"

class IAiFaceController : public AnimActionController
{
public:
	virtual void MeleeOverrideFacialAnimation(StringId64 animId, F32 duration) = 0;
	virtual void ClearMeleeOverride() = 0;
};

IAiFaceController* CreateAiFaceController();

#endif // _FACE_CONTROLLER_H_
