/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/action-pack-handle.h"
#include "gamelib/gameplay/nav/action-pack-mgr.h"
#include "gamelib/gameplay/nav/action-pack.h"

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPackHandle::ActionPackHandle(const ActionPack* pAp)
{
#ifdef DEBUG_ACTION_PACK_HANDLE
	m_ppDebugActionPack = nullptr;
#endif
	m_mgrId = ActionPackMgr::kInvalidMgrId;

	if (pAp)
	{
		m_mgrId = pAp->GetMgrId();

#ifdef DEBUG_ACTION_PACK_HANDLE
		const U32F iSlotIndex = ActionPackMgr::GetSlotIndex(m_mgrId);
		if (iSlotIndex < ActionPackMgr::kMaxActionPackCount)
		{
			m_ppDebugActionPack = &ActionPackMgr::Get().m_pActionPack[iSlotIndex];
		}
#endif
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
ActionPack* ActionPackHandle::ToActionPack() const
{
	if (m_mgrId == ActionPackMgr::kInvalidMgrId)
		return nullptr;

	ActionPack* pAp = ActionPackMgr::Get().LookupRegisteredActionPack(m_mgrId);

	return pAp;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ActionPackHandle::HasTurnedInvalid() const
{
	return (m_mgrId != ActionPackMgr::kInvalidMgrId) && !ActionPackMgr::Get().IsRegisteredOrPending(m_mgrId);
}
