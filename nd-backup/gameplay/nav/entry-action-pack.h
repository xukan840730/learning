/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/script/script-pointer.h"

#include "gamelib/gameplay/nav/action-pack.h"

/// --------------------------------------------------------------------------------------------------------------- ///
namespace DC
{
	struct EntryApTable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
class EntryActionPack : public ActionPack
{
public:
	DECLARE_ACTION_PACK_TYPE(EntryActionPack);
	typedef ActionPack ParentClass;

	EntryActionPack();
	EntryActionPack(const ScriptPointer<DC::EntryApTable>& dcDef,
					const BoundFrame& bfLoc,
					StringId64 destAnimId,
					float destAnimPhase);

	virtual void DebugDraw(DebugPrimTime tt = kPrimDuration1FrameAuto) const override;

	const DC::EntryApTable* GetDcDef() const { return m_def; }
	StringId64 GetDestAnimId() const { return m_destAnimId; }
	float GetDestAnimPhase() const { return m_destAnimPhase; }

	static I32F SelectEntryDefAnims(const DC::EntryApTable* pDcDef, StringId64 charId);

private:
	virtual bool HasNavMeshClearance(const NavLocation& navLoc,
									 bool debugDraw = false,
									 DebugPrimTime tt = kPrimDuration1FramePauseable) const override;

	ScriptPointer<DC::EntryApTable> m_def;
	StringId64 m_destAnimId;
	float m_destAnimPhase;
};
