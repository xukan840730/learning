/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef PLUGIN_SHARED_H
#define PLUGIN_SHARED_H

#include <orbisanim/joints.h>
#include "ndlib/anim/anim-debug.h"

namespace OrbisAnim
{
}

#ifdef ANIM_DEBUG
void ValidateObjectTransform(const Transform* pObjXform);
void ValidateJointTransforms(const OrbisAnim::JointTransform* pJointTransforms, U32 numJoints);
#endif

#endif // PLUGIN_SHARED_H
