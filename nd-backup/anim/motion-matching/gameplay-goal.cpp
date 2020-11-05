/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/motion-matching/gameplay-goal.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/anim/motion-matching/motion-matching.h"
#include "gamelib/level/art-item-anim.h"
#include "gamelib/scriptx/h/motion-matching-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static Vec4 LerpScale(const float ta, const float tb, const Vec4& a, const Vec4& b, const float t)
{
	const float alpha = Limit01((t - ta) / (tb - ta));
	return b * alpha + a * (1.0f - alpha);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimTrajectory::AnimTrajectory(size_t capacity)
	: m_positionTrajectory(capacity)
	, m_velTrajectory(capacity)
	, m_facingTrajectory(capacity)
	, m_yawSpeedTrajectory(capacity)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimTrajectory::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	m_positionTrajectory.Relocate(deltaPos, lowerBound, upperBound);
	m_velTrajectory.Relocate(deltaPos, lowerBound, upperBound);
	m_facingTrajectory.Relocate(deltaPos, lowerBound, upperBound);
	m_yawSpeedTrajectory.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimTrajectory::AnimTrajectory(const ListArray<AnimTrajectorySample>& samples)
	: m_positionTrajectory(samples.Size())
	, m_velTrajectory(samples.Size())
	, m_facingTrajectory(samples.Size())
	, m_yawSpeedTrajectory(samples.Size())
{
	for (const auto& g : samples)
	{
		Add(g);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimTrajectorySample AnimTrajectory::Get(float time) const
{
	AnimTrajectorySample g;
	
	g.SetTime(time);
	Maybe<Point> pos = m_positionTrajectory.Get(time);
	if (pos.Valid())
	{
		g.SetPosition(pos.Get());
	}

	Maybe<Vector> vel = m_velTrajectory.Get(time);
	if (vel.Valid())
	{
		g.SetVelocity(vel.Get());
	}

	Maybe<Vector> facing = m_facingTrajectory.Get(time);
	if (facing.Valid())
	{
		g.SetFacingDir(facing.Get());
	}

	Maybe<float> yawSpeed = m_yawSpeedTrajectory.Get(time);
	if (yawSpeed.Valid())
	{
		g.SetYawSpeed(yawSpeed.Get());
	}

	return g;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const AnimTrajectorySample AnimTrajectory::GetTail() const
{
	AnimTrajectorySample g;

	g.SetTime(m_positionTrajectory.LimitTime(kLargeFloat));

	Maybe<Point> pos = m_positionTrajectory.GetTail();
	if (pos.Valid())
	{
		g.SetPosition(Point(pos.Get()));
	}

	Maybe<Vector> vel = m_velTrajectory.GetTail();
	if (vel.Valid())
	{
		g.SetVelocity(Vector(vel.Get()));
	}

	Maybe<Vector> facing = m_facingTrajectory.GetTail();
	if (facing.Valid())
	{
		g.SetFacingDir(Vector(facing.Get()));
	}

	Maybe<float> yawSpeed = m_yawSpeedTrajectory.GetTail();
	if (yawSpeed.Valid())
	{
		g.SetYawSpeed(yawSpeed.Get());
	}

	return g;
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimTrajectory::GetTimeClosestTo(Point_arg p) const 
{
	const U32F numSamples = m_positionTrajectory.GetNumSamples();

	if (numSamples == 0)
	{
		return -1.0f;
	}

	if (numSamples == 1)
	{
		return m_positionTrajectory.GetSampleTime(0);
	}

	float bestDist = kLargeFloat;
	float bestTime = -1.0f;

	for (I32F iSample = 0; iSample < (numSamples - 1); ++iSample)
	{
		const Point p0 = m_positionTrajectory.GetSample(iSample);
		const Point p1 = m_positionTrajectory.GetSample(iSample + 1);

		Scalar tt;
		const float d = DistPointSegment(p, p0, p1, nullptr, &tt);

		if (d < bestDist)
		{
			const Scalar t0 = m_positionTrajectory.GetSampleTime(iSample);
			const Scalar t1 = m_positionTrajectory.GetSampleTime(iSample + 1);

			bestDist = d;
			bestTime = Lerp(t0, t1, tt);
		}
	}

	return bestTime;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimTrajectory::Add(const AnimTrajectorySample& g)
{
	if (g.IsPositionValid())
	{
		m_positionTrajectory.Add(g.GetPosition(), g.GetTime());
	}
	if (g.IsVelocityValid())
	{
		m_velTrajectory.Add(g.GetVelocity(), g.GetTime());
	}
	if (g.IsFacingValid())
	{
		m_facingTrajectory.Add(g.GetFacingDir(), g.GetTime());
	}
	if (g.IsYawSpeedValid())
	{
		m_yawSpeedTrajectory.Add(g.GetYawSpeed(), g.GetTime());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimTrajectory::AddFrom(const AnimTrajectory& srcTraj)
{
	for (U32F iSample = 0; iSample < srcTraj.m_positionTrajectory.GetNumSamples(); ++iSample)
	{
		const Point val = srcTraj.m_positionTrajectory.GetSample(iSample);
		const F32 time = srcTraj.m_positionTrajectory.GetSampleTime(iSample);

		m_positionTrajectory.Add(val, time);
	}

	for (U32F iSample = 0; iSample < srcTraj.m_velTrajectory.GetNumSamples(); ++iSample)
	{
		const Vector val = srcTraj.m_velTrajectory.GetSample(iSample);
		const F32 time = srcTraj.m_velTrajectory.GetSampleTime(iSample);

		m_velTrajectory.Add(val, time);
	}

	for (U32F iSample = 0; iSample < srcTraj.m_facingTrajectory.GetNumSamples(); ++iSample)
	{
		const Vector val = srcTraj.m_facingTrajectory.GetSample(iSample);
		const F32 time = srcTraj.m_facingTrajectory.GetSampleTime(iSample);

		m_facingTrajectory.Add(val, time);
	}

	for (U32F iSample = 0; iSample < srcTraj.m_yawSpeedTrajectory.GetNumSamples(); ++iSample)
	{
		const float val = srcTraj.m_yawSpeedTrajectory.GetSample(iSample);
		const F32 time = srcTraj.m_yawSpeedTrajectory.GetSampleTime(iSample);

		m_yawSpeedTrajectory.Add(val, time);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimTrajectory::GetMinTime() const
{
	const float minPos = m_positionTrajectory.GetMinTime();
	const float minVel = m_velTrajectory.GetMinTime();
	const float minFac = m_facingTrajectory.GetMinTime();
	const float minYaw = m_yawSpeedTrajectory.GetMinTime();
	
	return Min(Min(minPos, Min(minVel, minFac)), minYaw);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float AnimTrajectory::GetMaxTime() const
{
	const float maxPos = m_positionTrajectory.GetMaxTime();
	const float maxVel = m_velTrajectory.GetMaxTime();
	const float maxFac = m_facingTrajectory.GetMaxTime();
	const float maxYaw = m_yawSpeedTrajectory.GetMaxTime();

	return Max(Max(maxPos, Max(maxVel, maxFac)), maxYaw);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimTrajectory::DebugDraw(const Locator& charLoc, const float minTime, const float maxTime, int samples) const
{
	STRIP_IN_FINAL_BUILD;

	const float dt = (maxTime - minTime) / (samples - 1);
	float t		   = minTime;
	Maybe<Point> prevPos;
	for (int i = 0; i < samples; i++)
	{
		auto g = Get(t);
		if (g.IsPositionValid())
		{
			const Point posWs = charLoc.TransformPoint(g.GetPosition());
			g_prim.Draw(DebugSphere(posWs, 0.1f, kColorMagenta), kPrimDuration1Frame);
			if (prevPos.Valid())
			{
				g_prim.Draw(DebugLine(prevPos.Get(), posWs, kColorMagenta, kColorMagenta, 4.0f), kPrimDuration1Frame);
			}
			if (g.IsFacingValid())
			{
				const Vector facingWs = charLoc.TransformVector(g.GetFacingDir());
				g_prim.Draw(DebugArrow(posWs, posWs + facingWs * 0.1f, kColorMagenta), kPrimDuration1Frame);
			}
			if (g.IsYawSpeedValid())
			{
				g_prim.Draw(DebugString(posWs, StringBuilder<64>("%0.1fdeg/s", g.GetYawSpeed()).c_str(), kColorWhite, 0.5f));
			}
			prevPos = posWs;
		}

		t += dt;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimTrajectory::DebugDraw(const Locator& charLoc, DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	STRIP_IN_FINAL_BUILD;

	PrimServerWrapper ps(charLoc);
	ps.SetDuration(tt);


	const I32F numPosSamples = m_positionTrajectory.GetNumSamples();

	ps.EnableHiddenLineAlpha();

	for (I32F i = 0; i < numPosSamples; ++i)
	{
		const Point pos = Point(m_positionTrajectory.GetSample(i));
		const F32 time = m_positionTrajectory.GetSampleTime(i);

		ps.DrawCross(pos, 0.1f, kColorBlueTrans);
		ps.DrawString(pos, StringBuilder<256>("%0.2f", time).c_str(), kColorBlueTrans, 0.4f);
	}

	ps.DisableHiddenLineAlpha();
	ps.DisableDepthTest();
	ps.SetLineWidth(3.0f);

	for (I32F i = 0; i < (numPosSamples - 1); ++i)
	{
		const Point p0 = Point(m_positionTrajectory.GetSample(i));
		const Point p1 = Point(m_positionTrajectory.GetSample(i + 1));

		ps.DrawLine(p0, p1, kColorBlue);
	}
}
