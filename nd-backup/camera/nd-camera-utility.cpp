/*
* Copyright (c) 2019 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/camera/nd-camera-utility.h"

#include "corelib/math/intersection.h"

#include "gamelib/ndphys/simple-collision-cast.h"

bool ClipProbeAgainstPlane(SimpleCastProbe& probe, const Plane& plane)
{
	Scalar distOriginToPlane = -plane.GetD();
	Vector planeNormalWs = plane.GetNormal();
	Point pointOnPlaneWs = kOrigin + (distOriginToPlane * planeNormalWs);

	// if ray is moving away from the plane, there is no intersection
	Vector probeDir = probe.m_vec;
	Scalar probeLen;
	Vector probeUnitDir = SafeNormalize(probe.m_vec, Vector(kZero), probeLen);
	Scalar probeRadius = probe.m_radius;
	Scalar cosTheta = Dot(probeUnitDir, planeNormalWs);
	if (cosTheta >= SCALAR_LC(0.0f))
		return false;

	// check if the start point is already in collision
	Scalar distOriginToProbeStart = Dot(probe.m_pos - kOrigin, planeNormalWs);
	Scalar distProbeStartToPlane = distOriginToPlane - distOriginToProbeStart;
	if (Abs(distProbeStartToPlane) <= probeRadius)
	{
		// we don't care about any other members of m_cc except m_time!
		probe.m_cc.m_time = 0.0f;
		return true;
	}

	// if the probe is too short it'll give QNaN in LinePlaneIntersect
	// and since we already checked start point for collision, we're good
	if (probeLen < SCALAR_LC(0.001f))
		return false;

	// find intersection between the center line of the probe and the plane
	Point intersectWs, intersectClampedWs;
	Scalar t = LinePlaneIntersect(pointOnPlaneWs, planeNormalWs, probe.m_pos, probe.m_pos + probe.m_vec, &intersectClampedWs, &intersectWs);

	// edgeIntersectWs should contain the endpoint closest to the plane if it did not intersect the plane

	// now adjust t to account for the radius of the sphere
	Scalar s = t * probeLen;
	Scalar radiusPullBack = probeRadius * Recip(cosTheta); // will always be negative
	s += radiusPullBack;

	if (s < probeLen)
	{
		// we hit the plane
		float tPlane = s * SafeRecip(probeLen, kZero);
		if (tPlane < probe.m_cc.m_time || probe.m_cc.m_time < 0.0f)
			probe.m_cc.m_time = tPlane;
		return true;
	}

	return false;
}

void DSphere(Sphere const& sphere, Color const& color, char const* txt, float textScale)
{
	g_prim.Draw(DebugSphere(sphere, color), kPrimDuration1FramePauseable);
	g_prim.Draw(DebugString(sphere.GetCenter(), txt, color, textScale), kPrimDuration1FramePauseable);
}