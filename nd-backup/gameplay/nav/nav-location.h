/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-handle.h"
#include "gamelib/gameplay/nav/nav-ledge-graph-handle.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"

#if ENABLE_NAV_LEDGES
class NavLedge;
class NavLedgeGraph;
#endif // ENABLE_NAV_LEDGES

class NavMesh;
class NavPoly;
class DoutMemChannels;

/// --------------------------------------------------------------------------------------------------------------- ///
class NavLocation : public NavHandle
{
public:
	typedef NavHandle ParentClass;

	NavLocation() : m_posPs(kInvalidPoint) {}

	bool IsValid() const;

	Point GetPosPs() const { return m_posPs; }
	Point GetPosWs() const;
	void UpdatePosPs(Point_arg posPs) { m_posPs = posPs; }

	void SetPs(Point_arg posPs, NavPolyHandle hNavPoly);
	void SetPs(Point_arg posPs, const NavPoly* pNavPoly);
	void SetPs(Point_arg posPs, const NavMesh* pNavMesh);
#if ENABLE_NAV_LEDGES
	void SetPs(Point_arg posPs, const NavLedge* pNavLedge);
	void SetPs(Point_arg posPs, NavLedgeHandle hNavLedge);
	void SetPs(Point_arg posPs, const NavLedgeGraph* pLedgeGraph);
#endif // ENABLE_NAV_LEDGES

	void SetPs(Point_arg posPs, const NavHandle& navHandle);

	void SetWs(Point_arg posWs);
	void SetWs(Point_arg posWs, NavPolyHandle hNavPoly);
	void SetWs(Point_arg posWs, const NavPoly* pNavPoly);
	void SetWs(Point_arg posWs, const NavMesh* pNavMesh);
#if ENABLE_NAV_LEDGES
	void SetWs(Point_arg posWs, NavLedgeHandle hNavLedge);
	void SetWs(Point_arg posWs, const NavLedge* pNavLedge);
	void SetWs(Point_arg posWs, const NavLedgeGraph* pLedgeGraph);
#endif // ENABLE_NAV_LEDGES

	void SetWs(Point_arg posWs, const NavHandle& navHandle);

	void DebugDraw(Color clr = kColorYellow, DebugPrimTime tt = kPrimDuration1FrameAuto) const;
	void DebugPrint(DoutBase* pDout) const;
	void DebugPrint(DoutMemChannels* pDout) const;

private:
	Point m_posPs;
};

/// --------------------------------------------------------------------------------------------------------------- ///
namespace NavUtil
{
	NavLocation ConstructNavLocation(Point_arg posWs,
									 NavLocation::Type navType,
									 F32 convolutionRadius,
									 Segment probeOffset  = Segment(kZero, kZero),
									 F32 findMeshRadius = 1.0f,
									 F32 findMeshYThreshold = 2.0f,
									 Nav::StaticBlockageMask staticBlockagemask = Nav::kStaticBlockageMaskAll);
	NavLocation TryAdjustNavLocationPs(NavLocation startingLoc, Vector_arg moveVecPs);
} // namespace NavUtil
