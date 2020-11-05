/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/nav/action-pack.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ActionPackHandle
{
public:
	const static U32 kInvalidMgrId = 0xffffffff;

	ActionPackHandle() : m_mgrId(kInvalidMgrId) {}

	ActionPackHandle(const ActionPack* pAp);

	ActionPack* ToActionPack() const; // lookup registered action packs

	template <typename T>
	T* ToActionPack() const
	{
		ActionPack* pAp = ToActionPack();

		return T::FromActionPack(pAp);
	}

	bool HasTurnedInvalid() const;

	bool operator==(const ActionPackHandle& h) const { return h.m_mgrId == m_mgrId; }
	bool operator!=(const ActionPackHandle& h) const { return h.m_mgrId != m_mgrId; }

	bool IsValid() const { return m_mgrId != kInvalidMgrId; }

	void InitFromId(U32 mgrId) { m_mgrId = mgrId; }
	U32 GetMgrId() const { return m_mgrId; }

private:
	U32 m_mgrId;
};
