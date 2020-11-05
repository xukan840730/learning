/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/feature/feature-db.h"
#include "gamelib/gameplay/nav/action-pack.h"

/// --------------------------------------------------------------------------------------------------------------- ///
struct LeapApDefinition
{
	BoundFrame			m_loc;
	Point				m_regPtLs;
	NavPolyHandle		m_hRegPoly;
	FeatureEdge::Flags	m_featureFlags;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class LeapActionPack : public ActionPack
{
public:
	DECLARE_ACTION_PACK_TYPE(LeapActionPack);

	typedef ActionPack ParentClass;

	static size_t GenerateLeapApsFromEdge(const Level* pLevel,
										  const FeatureEdge& edge,
										  LeapApDefinition* pDefsOut,
										  size_t maxDefsOut);

	LeapActionPack(const Level* pLevel, const LeapApDefinition& def);
	LeapActionPack(const Level* pAllocLevel, const Level* pRegLevel, const LeapApDefinition& def);

	virtual void DebugDraw(DebugPrimTime tt = kPrimDuration1FrameAuto) const override;

	virtual Point GetDefaultEntryPointWs(Scalar_arg offset) const override;
	virtual Point GetDefaultEntryPointPs(Scalar_arg offset) const override;
	virtual BoundFrame GetDefaultEntryBoundFrame(Scalar_arg offset) const override;

protected:
	virtual bool RegisterInternal() override;
	virtual void UnregisterInternal() override;

private:
	LeapApDefinition m_def;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void GenerateLeapApsFromNavMesh(const NavMesh* pNavMesh);
