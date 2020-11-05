/*
 * Copyright (c) 2018 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/gameplay/nav/action-pack-handle.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/spline/catmull-rom.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class PulloutActionPack : public ActionPack
{
public:
	DECLARE_ACTION_PACK_TYPE(PulloutActionPack);

	PulloutActionPack(const BoundFrame& loc, const CatmullRom* pSpline, const Level* pLevel);

	virtual Vector GetDefaultEntryOffsetLs() const override;

	StringId64 GetGroupId() const;

private:
	CatmullRom::Handle m_hSpline;
};
