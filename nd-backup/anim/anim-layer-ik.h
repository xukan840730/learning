/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimCmdGenLayerContext;
class AnimCmdList;

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimStateLayer_BlendedIkCb_PreBlend(const AnimStateLayer* pStateLayer,
										 const AnimCmdGenLayerContext& context,
										 AnimCmdList* pAnimCmdList,
										 SkeletonId skelId,
										 I32F leftInstace,
										 I32F rightInstance,
										 I32F outputInstance,
										 ndanim::BlendMode blendMode,
										 uintptr_t userData);

void AnimStateLayer_BlendedIkCb_PostBlend(const AnimStateLayer* pStateLayer,
										  const AnimCmdGenLayerContext& context,
										  AnimCmdList* pAnimCmdList,
										  SkeletonId skelId,
										  I32F leftInstace,
										  I32F rightInstance,
										  I32F outputInstance,
										  ndanim::BlendMode blendMode,
										  uintptr_t userData);
