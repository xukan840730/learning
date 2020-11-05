/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/ai/zone.h"

#include "corelib/math/cylinder.h"
#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/level/entity-spawner.h"
#include "ndlib/render/util/screen-space-text-printer.h"

const float Zone::kInfiniteYThreshold = NDI_FLT_MAX;
const float Zone::kInfiniteRadius = NDI_FLT_MAX;

const float kZoneHeight = 1.0f;

Zone::Zone()
	: m_frame(kIdentity),
	  m_radius(kInfiniteRadius),
	  m_height(kZoneHeight)
{
}

Zone::Zone(BoundFrame frame, float r)
	: m_frame(frame),
	  m_radius (r),
	  m_height(kZoneHeight)
{
}

Zone::Zone(BoundFrame frame, float r, float height)
	: m_frame(frame),
	m_radius (r),
	m_height(height)
{
}

#ifndef NDI_ARCH_SPU
Zone::Zone(StringId64 regionNameId)
	: m_frame(kIdentity),
	  m_radius(0.0f),
	  m_height(kZoneHeight)
{
	m_regionHandle.SetByName(regionNameId);
}
#endif

#ifndef NDI_ARCH_SPU
void Zone::ClearAll()
{
	m_frame = kIdentity;
	m_radius = kInfiniteRadius;
	m_height = kZoneHeight;
	m_regionHandle = nullptr;
}
#endif

#ifndef NDI_ARCH_SPU
void Zone::SetRegionByNameId(StringId64 regionNameId)
{
	m_frame = kIdentity;
	m_radius = 0.f;
	m_height = kZoneHeight;
	m_regionHandle.SetByName(regionNameId);
}

void Zone::SetRegion(const Region* pRegion)
{
	m_frame = kIdentity;
	m_radius = 0.f;
	m_height = kZoneHeight;
	m_regionHandle = pRegion;
}
#endif

const BoundFrame Zone::GetBoundFrame() const
{
#ifndef NDI_ARCH_SPU
	const Region* pRegion = m_regionHandle.GetTarget();
	if (pRegion)
	{
		Point centroid = kZero;
		pRegion->DetermineCentroid(centroid);

//		DebugDrawSphere(centroid, 5.0f, kColorOrange);

		BoundFrame frame(kIdentity);
		const EntitySpawner *pSpawner = pRegion->GetBindSpawner();
		if (pSpawner)
			frame = pSpawner->GetBoundFrame();

		AI_ASSERT(IsFinite(centroid));

		frame.SetTranslation(centroid);
		return frame;
	}
#endif

	return m_frame;
}

float Zone::GetHeight() const
{
#ifndef NDI_ARCH_SPU
	const Region* pRegion = m_regionHandle.GetTarget();
	if (pRegion)
	{
		return pRegion->GetHeight();
	}
	else
#endif
	{
		return m_height;
	}
}

void Zone::SetRadius(float radius)
{
#ifndef NDI_ARCH_SPU
	AI_ASSERT(m_regionHandle.GetTarget() == nullptr);
#endif
	m_radius = radius;
}

void Zone::SetHeight(float height)
{
#ifndef NDI_ARCH_SPU
	AI_ASSERT(m_regionHandle.GetTarget() == nullptr);
#endif
	m_height = height;
}

bool Zone::InZone(Point_arg positionWs, Point* pClosestPointWs/*=nullptr*/, float radius/*=0.5f*/) const
{
	if (IsInfinite())
		return true;

	if (pClosestPointWs)
		*pClosestPointWs = kOrigin;

#ifndef NDI_ARCH_SPU
	const Region* pRegion = m_regionHandle.GetTarget();
	if (pRegion)
	{
		return pRegion->IsInsideWithClosest(positionWs, radius, pClosestPointWs);
	}
	else
#endif
	{
		if ((LengthXzSqr(positionWs - GetBoundFrame().GetTranslation()) > Sqr(m_radius)) ||
			(Abs(positionWs.Y() - GetBoundFrame().GetTranslation().Y()) > m_height))
		{
			if (pClosestPointWs)
				*pClosestPointWs = GetBoundFrame().GetTranslation();
			return false;
		}

		return true;
	}
}

void Zone::SetTranslation( Point positionWs )
{
#ifndef NDI_ARCH_SPU
	AI_ASSERT(m_regionHandle.GetTarget() == nullptr);
#endif
	m_frame.SetTranslation(positionWs);
}

float Zone::GetRadius() const
{
#ifndef NDI_ARCH_SPU
	if (m_regionHandle.GetTarget() != nullptr)
	{
		return m_regionHandle.GetTarget()->GetRegionSphere().GetRadius();
	}
#endif

	return m_radius;
}

#ifndef NDI_ARCH_SPU
void Zone::DebugDraw(ScreenSpaceTextPrinter* pPrinter, Point npcPos, bool isLockedToZone, Color color, StringId64 zoneId, DebugPrimTime tt) const
{
	STRIP_IN_FINAL_BUILD;

	if (pPrinter != nullptr)
	{
		pPrinter->PrintText(color, "ai-zone: %s%s%s",
			m_regionHandle.GetTarget() != nullptr ? DevKitOnly_StringIdToStringOrHex(m_regionHandle.GetName()) : zoneId != INVALID_STRING_ID_64 ? DevKitOnly_StringIdToStringOrHex(zoneId) : "no zone",
			isLockedToZone ? "(locked)" : "(unlocked)",
			InZone(npcPos) ? "(inside)" : "(outside)");
	}

	if (m_regionHandle.GetTarget() != nullptr)
	{
		if (const Region* pAiRegion = m_regionHandle.GetTarget())
		{
			pAiRegion->DebugDraw(tt);
		}
	}
	else
	{
		DebugDrawCylinder(Cylinder(GetBoundFrame().GetTranslation(), m_radius, 1.0f), color, PrimAttrib(), tt);

		const bool isZoneInfinite = IsInfinite();
		if (!isZoneInfinite)
		{
			DebugDrawCylinder(Cylinder(GetBoundFrame().GetTranslation(), m_radius, 1.0f), kColorOrange, PrimAttrib(), tt);
		}
	}
}
#endif

bool Zone::AreZonesEquivlanet(const Zone& a, const Zone& b, const float tolerance)
{
	const bool aInf = a.IsInfinite();
	const bool bInf = b.IsInfinite();
	if(aInf || bInf)
		return aInf == bInf;

#ifndef NDI_ARCH_SPU
	if (a.m_regionHandle.GetTarget() != b.m_regionHandle.GetTarget())
		return false;
#endif

	if(Abs(a.m_radius - b.m_radius) > tolerance)
	{
		return false;
	}

	if(DistSqr(a.GetBoundFrame().GetTranslation(), b.GetBoundFrame().GetTranslation()) > tolerance*tolerance)
	{
		return false;
	}

	return true;
}
