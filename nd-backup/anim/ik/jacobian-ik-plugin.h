/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include <orbisanim/joints.h>
#include <orbisanim/util.h>

struct JacobianIkPluginData;

OrbisAnim::Status JacobianIkPluginCallback(const JacobianIkPluginData* pJacobianIkPlugin,
										   OrbisAnim::JointParams* pJointParamsLs,
										   const OrbisAnim::AnimHierarchySegment* pSegment,
										   OrbisAnim::ValidBits* pValidBitsOut);
