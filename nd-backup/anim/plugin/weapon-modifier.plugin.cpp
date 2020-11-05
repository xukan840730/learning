/*
 * Copyright (c) 2009 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "corelib/math/locator.h"
#include "ndlib/anim/ik/joint-chain.h"
#include "ndlib/anim/joint-modifiers/joint-modifier-data.h"


/// --------------------------------------------------------------------------------------------------------------- ///
void SolveWeaponMod(JointSet* pJointSet, const Transform* pObjXformWs, const JointModifierData* pData)
{
	const JointModifierData::HideJointData& data = *pData->GetWeaponModData();

	for (I32F i = 0; i < data.m_numHiddenJoints; ++i)
	{
		pJointSet->SetJointScaleIndex(data.m_hiddenJointIndices[i], kZero);
	}

	if (data.m_magazineIndex != 0)
	{
		if (data.m_hideMagazine)
		{
			pJointSet->SetJointScaleIndex(data.m_magazineIndex, kZero);
		}
		else if (data.m_attachMagToParent)
		{
			const Locator objLocWs(*pObjXformWs);
			const Locator magLocOs = objLocWs.UntransformLocator(data.m_wsMagazineLocator);
			pJointSet->SetJointLocWsIndex(data.m_magazineIndex, magLocOs);
		}
	}

	if (data.m_burstSelectorIndex != 0)
	{
		if (Abs(data.m_burstSelectorAngle) > 0.0f)
		{
			pJointSet->PostRotateJointLs(data.m_burstSelectorIndex + 1, QuatFromAxisAngle(kUnitXAxis, data.m_burstSelectorAngle), false);
		}
	}

	if (data.m_barrelIndex != 0)
	{
		const Quat rotation = QuatFromAxisAngle(kUnitZAxis, DegreesToRadians(data.m_barrelRot));
		pJointSet->PostRotateJointLsIndex(data.m_barrelIndex, rotation);
	}
}
