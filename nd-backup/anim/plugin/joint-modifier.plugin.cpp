/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include <orbisanim/structs.h>

#include "ndlib/anim/joint-modifiers/joint-modifier-data.h"
#include "ndlib/anim/joint-modifiers/joint-modifiers.h"

class JointSet;

void SolveEyeIk(JointSet* pJointSet, const Transform* pObjXformWs, const JointModifierData* pData);
void SolveWeaponGripIk(JointSet* pJointSet, const Transform* pObjXformWs, JointModifierData* pData);
void SolveStrideScaleIk(JointSet* pJointSet, const Transform* pObjXformWs, const JointModifierData* pData);
void SolveNetStrafeIk(JointSet* pJointSet, const Transform* pObjXformWs, const JointModifierData* pData);
void SolveWeaponMod(JointSet* pJointSet, const Transform* pObjXformWs, const JointModifierData* pData);
void ApplyJointLimits(JointSet* pJointSet, const Transform* pObjXformWs, const JointModifierData* pData);

#ifdef ANIM_DEBUG
/// --------------------------------------------------------------------------------------------------------------- ///
static void ValidateObjectTransform(const SMath::Transform* pObjXform)
{
	const SMath::Transform curXform(*pObjXform);
	ANIM_ASSERT(IsOrthogonal(&curXform));
	const Point objWsPos = curXform.GetTranslation();
	ANIM_ASSERT(Length(objWsPos) < 100000.0f);
	ANIM_ASSERT(IsFinite(objWsPos));
}
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
OrbisAnim::Status JointModifierAnimPluginCallback(OrbisAnim::SegmentContext* pSegmentContext,
												  JointModifierData* pData,
												  const Transform* pObjXform,
												  U16 ikType,
												  JointSet* pPluginJointSet)
{
#ifdef ANIM_DEBUG
	ValidateObjectTransform(pObjXform);
#endif

	switch (ikType)
	{
	case kEyeModifier:
		SolveEyeIk(pPluginJointSet, pObjXform, pData);
		break;

	case kWeaponGripModifier:
		if (pData->IsWeaponIkEnabled())
		{
			SolveWeaponGripIk(pPluginJointSet, pObjXform, pData);
		}
		break;

	case kStrideModifier:
		SolveStrideScaleIk(pPluginJointSet, pObjXform, pData);
		break;

	case kWeaponModModifier:
		SolveWeaponMod(pPluginJointSet, pObjXform, pData);
		break;

	case kNetStrafe:
		SolveNetStrafeIk(pPluginJointSet, pObjXform, pData);
		break;
	}

#ifdef ANIM_DEBUG
	ValidateObjectTransform(pObjXform);
#endif

	return OrbisAnim::kSuccess;
}

