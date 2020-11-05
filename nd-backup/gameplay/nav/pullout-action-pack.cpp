/*
 * Copyright (c) 2018 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/pullout-action-pack.h"

/// --------------------------------------------------------------------------------------------------------------- ///
PulloutActionPack::PulloutActionPack(const BoundFrame& loc, const CatmullRom* pSpline, const Level* pLevel)
	: ActionPack(kPulloutActionPack, Point(0.0f, 0.0f, -0.5f), loc, pLevel)
{
	m_hSpline = pSpline;
}

StringId64 PulloutActionPack::GetGroupId() const
{
	const SplineData* pSpline = m_hSpline.ToSpline();
	if (!pSpline)
		return INVALID_STRING_ID_64;

	return pSpline->GetTagData(SID("group-id"), pSpline->m_nameId);
}

Vector PulloutActionPack::GetDefaultEntryOffsetLs() const
{
	return Vector(0.0f, 0.0f, -0.25f);
}


