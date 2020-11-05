/*
* Copyright (c) 2018 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/gameplay/nd-attachable-locator.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "ndlib/anim/attach-system.h"

Locator AttachableLocator::GetLocator() const
{
	if (m_hObj.HandleValid())
	{
		const NdGameObject* pTargetObj = m_hObj.ToProcess();
		if (m_objAttachPoint != INVALID_STRING_ID_64)
		{
			if (const AttachSystem* pAttach = pTargetObj->GetAttachSystem())
			{
				AttachIndex attachIndex = pAttach->FindPointIndexById(m_objAttachPoint);
				if (attachIndex != AttachIndex::kInvalid)
				{
					return pAttach->GetLocator(attachIndex);
				}
			}
		}
		return pTargetObj->GetLocator();
	}
	else if (m_boundFrameValid)
	{
		return m_boundFrame.GetLocator();
	}

	ASSERTF(false, ("Neither bound-frame or attach-point specified!\n"));
	return Locator(kOrigin);
}
