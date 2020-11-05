/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/list-array.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/util/maybe.h"

#include <algorithm>

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimTrajectorySample
{
public:
	AnimTrajectorySample() : m_posValid(false), m_velValid(false), m_facingValid(false), m_yawSpeedValid(false) {}

	void SetTime(float time) { m_timeInFutureSeconds = time; }

	void SetPosition(Point_arg p)
	{
		m_position = p;
		m_posValid = true;
	}

	void SetVelocity(Vector_arg v)
	{
		m_velocity = v;
		m_velValid = true;
	}

	void SetFacingDir(Vector_arg f)
	{
		m_facing	  = f;
		m_facingValid = true;
	}

	void SetYawSpeed(float f)
	{
		m_yawSpeed		= f;
		m_yawSpeedValid = true;
	}

	bool IsPositionValid() const { return m_posValid; }
	bool IsVelocityValid() const { return m_velValid; }
	bool IsFacingValid() const { return m_facingValid; }
	bool IsYawSpeedValid() const { return m_yawSpeedValid; }

	Point GetPosition() const
	{
		ANIM_ASSERT(m_posValid);
		return m_position;
	}

	Vector GetVelocity() const
	{
		ANIM_ASSERT(m_velValid);
		return m_velocity;
	}

	Vector GetFacingDir() const
	{
		ANIM_ASSERT(m_facingValid);
		return m_facing;
	}

	float GetYawSpeed() const
	{
		ANIM_ASSERT(m_yawSpeedValid);
		return m_yawSpeed;
	}

	float GetTime() const { return m_timeInFutureSeconds; }

private:
	Point m_position;
	Vector m_velocity;
	Vector m_facing;
	float m_yawSpeed;
	float m_timeInFutureSeconds;
	bool m_posValid;
	bool m_velValid;
	bool m_facingValid;
	bool m_yawSpeedValid;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimTrajectory
{
public:
	AnimTrajectory() {}
	AnimTrajectory(size_t capacity);
	AnimTrajectory(const ListArray<AnimTrajectorySample>& samples);
	
	void Clear()
	{
		m_positionTrajectory.Clear();
		m_velTrajectory.Clear();
		m_facingTrajectory.Clear();
		m_yawSpeedTrajectory.Clear();
	}

	bool IsEmpty() const
	{
		return m_positionTrajectory.GetNumSamples() == 0;
	}

	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	const AnimTrajectorySample Get(float time) const;
	const AnimTrajectorySample GetTail() const;

	float GetTimeClosestTo(Point_arg p) const;

	void Add(const AnimTrajectorySample& g);
	void AddFrom(const AnimTrajectory& srcTraj);
	
	bool UpdateTailFacing(Vector_arg dir)
	{
		return m_facingTrajectory.SetTail(dir);
	}

	float GetMinTime() const;
	float GetMaxTime() const;

	void DebugDraw(const Locator& charLoc, const float minTime, const float maxTime, int samples) const;
	void DebugDraw(const Locator& charLoc, DebugPrimTime tt = kPrimDuration1FrameAuto) const;

	size_t GetCapacity() const
	{
		return Max(Max(Max(m_positionTrajectory.GetCapacity(), m_velTrajectory.GetCapacity()),
					   m_facingTrajectory.GetCapacity()),
				   m_yawSpeedTrajectory.GetCapacity());
	}

private:
	template <class T>
	class TrajectoryChannel
	{
	public:
		TrajectoryChannel() {}

		TrajectoryChannel(size_t capacity)
		{
			m_samples.Init(capacity, FILE_LINE_FUNC);
			m_times.Init(capacity, FILE_LINE_FUNC);
		}

		void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
		{
			m_samples.Relocate(deltaPos, lowerBound, upperBound);
			m_times.Relocate(deltaPos, lowerBound, upperBound);
		}

		void Clear()
		{
			m_samples.Clear();
			m_times.Clear();
		}

		size_t GetCapacity() const { return m_samples.capacity(); }
		size_t GetNumSamples() const { return m_samples.size(); }

		Maybe<T> Get(float time) const
		{
			const size_t numSamples = m_times.Size();
			if (numSamples < 1)
			{
				return MAYBE::kNothing;
			}

			const float minTime = m_times[0];
			const float maxTime = m_times[numSamples - 1];

			const float epsilon = 0.1f;

			if (((time + epsilon) < minTime) || ((time - epsilon) > maxTime))
			{
				return MAYBE::kNothing;
			}

			if (numSamples == 1)
			{
				if (time * minTime >= 0.0f)
				{
					return m_samples[0];
				}
				else
				{
					// don't mix past and future values
					return MAYBE::kNothing;
				}
			}
			else if (numSamples == 2)
			{
				return LerpScale(m_times[0], m_times[1], m_samples[0], m_samples[1], time);
			}

			ListArray<F32>::const_iterator it = std::upper_bound(m_times.begin(), m_times.end(), time);
			int index = it - m_times.begin();

			int prevIndx = index - 1;
			if (prevIndx < 0)
			{
				index += -prevIndx;
				prevIndx = 0;
			}
			if (index > numSamples - 1)
			{
				int delta = index - (numSamples - 1);
				index -= delta;
				prevIndx -= delta;
			}

			ANIM_ASSERT(prevIndx >= 0 && prevIndx < m_times.Size());
			ANIM_ASSERT(index >= 0 && index < m_times.Size());

			return LerpScale(m_times[prevIndx], m_times[index], m_samples[prevIndx], m_samples[index], time);
		}

		T GetSample(I32F iSample) const
		{
			ANIM_ASSERT(iSample >= 0 && iSample < m_samples.size());
			return m_samples[iSample];
		}

		F32 GetSampleTime(I32F iSample) const
		{
			ANIM_ASSERT(iSample >= 0 && iSample < m_times.size());
			return m_times[iSample];
		}

		Maybe<T> GetTail() const
		{
			const size_t numSamples = m_times.Size();
			if (numSamples < 1)
			{
				return MAYBE::kNothing;
			}

			return m_samples[numSamples - 1];
		}

		float LimitTime(float time) const
		{
			const size_t numSamples = m_times.Size();
			if (numSamples < 1)
			{
				return time;
			}
			else if (numSamples == 1)
			{
				return m_times[0];
			}

			const float minTime = m_times[0];
			const float maxTime = m_times[numSamples - 1];

			return Limit(time, minTime, maxTime);
		}

		float GetMinTime() const
		{
			const size_t numSamples = m_times.Size();
			if (numSamples < 1)
			{
				return 0.0f;
			}

			return m_times[0];
		}

		float GetMaxTime() const
		{
			const size_t numSamples = m_times.Size();
			if (numSamples < 1)
			{
				return 0.0f;
			}

			return m_times[numSamples - 1];
		}


		void Add(const T& value, F32 time)
		{
			ListArray<F32>::iterator it = std::lower_bound(m_times.begin(), m_times.end(), time);

			const int index = it - m_times.begin();

			m_times.Insert(it, time);
			m_samples.Insert(m_samples.begin() + index, value);

			ANIM_ASSERT(std::is_sorted(m_times.begin(), m_times.end()));
		}

		bool SetTail(const T& value)
		{
			const size_t numSamples = m_samples.Size();
			if (numSamples <= 0)
				return false;

			m_samples[numSamples - 1] = value;

			return true;
		}

	private:
		ListArray<T> m_samples;
		ListArray<F32> m_times;
	};

	TrajectoryChannel<Point> m_positionTrajectory;
	TrajectoryChannel<Vector> m_velTrajectory;
	TrajectoryChannel<Vector> m_facingTrajectory;
	TrajectoryChannel<float> m_yawSpeedTrajectory;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimGoalLocator
{
	Locator m_loc		= kIdentity;
	StringId64 m_nameId = INVALID_STRING_ID_64;
};
