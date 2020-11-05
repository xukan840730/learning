/*
 * Copyright (c) 2003 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

/*! \file zone.h
   \brief Container describing a circular zone.
*/

#ifndef NDLIB_AI_ZONE_H
#define NDLIB_AI_ZONE_H

#include "gamelib/region/region.h"
#include "ndlib/process/bound-frame.h"
#include "ndlib/render/util/prim.h"

class ScreenSpaceTextPrinter;
struct Color;

///
/// Class Zone:
/// This class contains a center position, xz-radius and y-threshold that describe a cylindrical zone aligned with the y-axis.
/// The member functions allow setting, querying this container and testing if a position is in the zone. 
///
class Zone
{
public:
	/// Constructors.
	Zone();
	Zone(BoundFrame frame, float r);
	Zone(BoundFrame frame, float r, float height);
#ifndef NDI_ARCH_SPU
	Zone(StringId64 regionNameId);
#endif

#ifndef NDI_ARCH_SPU
	void ClearAll();
#endif

	/// Set and query the position of the zone.
	const BoundFrame GetBoundFrame() const;

	void SetTranslation( Point positionWs );
	void SetRadius( float radius );
	void SetHeight( float height );

	float GetRadius() const;
	float GetHeight() const;
	float GetYThreshold() const { return GetHeight(); }

	/// Test if the position is in the zone.
	bool InZone(Point_arg positionWs, Point* pClosestPointWs=nullptr, float radius = 0.5f) const;
	bool IsInfinite() const { return (m_radius >= kInfiniteRadius); }
	bool IsRegion() const { return m_regionHandle.GetName() != INVALID_STRING_ID_64; }
	bool IsValid() const { return IsInfinite() || GetRadius() > 0.0f || GetRegion(); }

	const Region* GetRegion() const { return m_regionHandle.GetTarget(); }
	RegionHandle GetRegionHandle() const { return m_regionHandle; }
	void InvalidateRegionHandleCache() const { m_regionHandle.InvalidateCachedTarget(); }
#ifndef NDI_ARCH_SPU
	void SetRegionByNameId(StringId64 regionNameId);
	void SetRegion(const Region* pRegion);
#endif

#ifndef NDI_ARCH_SPU
	void DebugDraw(ScreenSpaceTextPrinter* printer, Point npcPos, bool isLockedToZone, Color color, StringId64 zoneId, DebugPrimTime tt = kPrimDuration1FrameAuto) const;
#endif

	static const float kInfiniteYThreshold;
	static const float kInfiniteRadius;

	static bool AreZonesEquivlanet(const Zone& a, const Zone& b, const float tolerance = 1.0f);

private:
	RegionHandle m_regionHandle;
	BoundFrame m_frame;
	float m_radius;
	float m_height;
};

#endif	//NDLIB_AI_ZONE_H
