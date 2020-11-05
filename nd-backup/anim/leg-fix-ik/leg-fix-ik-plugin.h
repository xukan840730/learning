/*
 * Copyright (c) 2011 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimCmdList;
class AnimSnapshotNodeBlend;
struct AnimCmdGenInfo;
struct IkChain;
struct IkChainSetupData;
struct LegIkJoints;

/// --------------------------------------------------------------------------------------------------------------- ///
struct LegFixIkPluginCallbackArg
{
	char m_magic[8];
	float m_blend[2];

	LegFixIkPluginCallbackArg();
	
	void SetMagic();
	bool CheckMagic() const;
};


/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(8) LegFixIkPluginData
{
	I32 m_blendedInstance;
	I32 m_baseInstance;
	LegFixIkPluginCallbackArg* m_pArg;
};


/// --------------------------------------------------------------------------------------------------------------- ///
struct LegFixIkPluginParams
{
	Transform m_objXform;
	ndanim::JointParams* m_pJointParamsLs;
	const ndanim::JointParams* m_pJointParamsPreAdditiveLs;
	const IkChainSetupData* m_apLegIkChainSetup[2];
	const LegIkJoints* m_apLegJoints[2];
	const LegFixIkPluginCallbackArg* m_pArg;
};

/// --------------------------------------------------------------------------------------------------------------- ///
extern OrbisAnim::Status LegFixIkPluginCallback(const LegFixIkPluginParams* pParams);

/// --------------------------------------------------------------------------------------------------------------- ///
extern void SolveLegIK(const Transform& objXform,
					   IkChain& ikChain,
					   const LegIkJoints* pLegJoints,
					   Point_arg goalPos,
					   Scalar_arg kneeLimitMin,
					   Scalar_arg kneeLimitMax,
					   bool debugDraw);
