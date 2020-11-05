/*
 * Copyright (c) 2013 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/ik/ik-defs.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class ArmIkChainSetup;
class JointSet;
struct AnimCmdGenLayerContext;

/// --------------------------------------------------------------------------------------------------------------- ///
struct HandFixIkPluginCallbackArg
{
	float m_tt = 1.0f;
	bool m_handsToIk[kArmCount] = { true, true };
	bool m_flipped = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(8) HandFixIkPluginData
{
	I32 m_blendedInstance;
	I32 m_baseInstance;
	HandFixIkPluginCallbackArg m_arg;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct HandFixIkPluginParams
{
	JointSet* m_pJointSet = nullptr;
	ndanim::JointParams* m_pJointParamsLs = nullptr;
	const ndanim::JointParams* m_pJointParamsPreAdditiveLs = nullptr;
	OrbisAnim::ValidBits* m_pValidBitsOut = nullptr;
	const ArmIkChainSetup* m_apArmChains[kArmCount] = { nullptr };
	
	float m_tt = 1.0f;
	bool m_handsToIk[kArmCount] = { true, true };
	bool m_flipped = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
// == The callbacks that you set on the Animation Layer across which you want the hand-fix-ik plugin to operate. ==
void HandFixIk_PreBlendCallback(const AnimStateLayer* pStateLayer,
								const AnimCmdGenLayerContext& context,
								AnimCmdList* pAnimCmdList,
								SkeletonId skelId,
								I32F leftInstance,
								I32F rightInstance,
								I32F outputInstance,
								ndanim::BlendMode blendMode,
								uintptr_t userData);
void HandFixIk_PostBlendCallback(const AnimStateLayer* pStateLayer,
								 const AnimCmdGenLayerContext& context,
								 AnimCmdList* pAnimCmdList,
								 SkeletonId skelId,
								 I32F leftInstance,
								 I32F rightInstance,
								 I32F outputInstance,
								 ndanim::BlendMode blendMode,
								 uintptr_t userData);

/// --------------------------------------------------------------------------------------------------------------- ///
OrbisAnim::Status HandFixIkPluginCallback(const HandFixIkPluginParams* pParams);
