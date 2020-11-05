/* SCE CONFIDENTIAL
* $PSLibId$
* Copyright (C) 2015 Sony Computer Entertainment Inc.
* All Rights Reserved.
*/

#pragma once

#include <vector>
#include "icelib/geom/cgvec.h"
#include "icelib/geom/cgquat.h"
#include "hermitespline.h"

namespace OrbisAnim {
namespace Tools {
namespace AnimProcessing {

// Animated channel types
enum ChannelType
{
	kChannelTypeScale = 0,		// vec3
	kChannelTypeRotation,		// vec4 (quaternion)
	kChannelTypeTranslation,	// vec3
	kChannelTypeScalar,			// float
	kNumChannelTypes,
	kNumJointChannels = 3
};

// anim sample type capable of holding key values of up to 4 floats
class AnimSample
{
public:
	// default constructor
	AnimSample() {}

	// copy constructors
	AnimSample(ITGEOM::Vec3 const& v) { m_v[0] = v[0]; m_v[1] = v[1]; m_v[2] = v[2]; m_v[3] = 0; }
	AnimSample(ITGEOM::Quat const& q) { m_v[0] = q[0]; m_v[1] = q[1]; m_v[2] = q[2]; m_v[3] = q[3]; }
	AnimSample(float f) { m_v[0] = f; m_v[1] = m_v[2] = m_v[3] = 0; }

	// copy by assignment
	AnimSample& operator=(ITGEOM::Vec3 const& v) { m_v[0] = v[0]; m_v[1] = v[1]; m_v[2] = v[2]; m_v[3] = 0; return *this; }
	AnimSample& operator=(ITGEOM::Quat const& q) { m_v[0] = q[0]; m_v[1] = q[1]; m_v[2] = q[2]; m_v[3] = q[3]; return *this; }
	AnimSample& operator=(float f) { m_v[0] = f; m_v[1] = m_v[2] = m_v[3] = 0; return *this; }

	// type-cast operators for conversion to different types
	operator ITGEOM::Vec3() const { return ITGEOM::Vec3(m_v[0], m_v[1], m_v[2]); }
	operator ITGEOM::Quat() const { return ITGEOM::Quat(m_v[0], m_v[1], m_v[2], m_v[3]); }
	operator float() const { return m_v[0]; }

	// equality within tolerance
	bool IsEqual(AnimSample const& other, float const tolerance);

private:
	float	m_v[4];
};

// abstract base for animated attributes / channels (scale, rotate, translate, float)
class Anim
{
public:
	virtual Anim* Clone() const = 0;
	virtual bool DetermineIfConstant(float const tolerance) = 0;
	virtual bool IsEqual(AnimSample const sample, float const tolerance) = 0;
	virtual bool IsConstant() const = 0;
	virtual void SetToConstant(AnimSample constSample) = 0;
};

class SampledAnim : public Anim
{
public:
	template<typename T>
	SampledAnim(ChannelType chanType, std::vector<T> const& samples);

	template<typename T>
	SampledAnim(ChannelType chanType, T const& constSample);

	void GetSamples(std::vector<ITGEOM::Vec3>& samples);
	void GetSamples(std::vector<ITGEOM::Quat>& samples);
	void GetSamples(std::vector<float>& samples);
	size_t GetNumSamples() const;
	void SetNumSamples(size_t numSamples);
	SampledAnim* Clone() const;
	bool DetermineIfConstant(float const tolerance);
	bool IsEqual(AnimSample const sample, float const tolerance);
	bool IsConstant() const;
	void SetToConstant(AnimSample constSample);
	AnimSample& operator[](size_t index);
	AnimSample const& operator[](size_t index) const;

private:
	bool DetermineIfConstantVec3(float const tolerance);
	bool DetermineIfConstantQuat(float const tolerance);
	bool DetermineIfConstantFloat(float const tolerance);

	ChannelType				m_type;
	std::vector<AnimSample>	m_samples;
};

template<typename T>
inline SampledAnim::SampledAnim(ChannelType chanType, std::vector<T> const& samples)
	: m_type(chanType)
{
	m_samples.assign(samples.begin(), samples.end());
}

template<typename T>
inline SampledAnim::SampledAnim(ChannelType chanType, T const& constSample)
	: m_type(chanType)
{
	m_samples.assign(1, constSample);
}

inline void SampledAnim::GetSamples(std::vector<ITGEOM::Vec3>& samples)
{
	samples.resize(m_samples.size());
	// use std::transform to resolve ambiguity between constructors
	// Vec3(float) and Vec3(Vec3). AnimSample has cast operators for both
	std::transform (std::begin(m_samples), std::end(m_samples), std::begin(samples),
		[](AnimSample x) { return x.operator ITGEOM::Vec3(); });
}

inline void SampledAnim::GetSamples(std::vector<ITGEOM::Quat>& samples)
{
	samples.assign(m_samples.begin(), m_samples.end());
}

inline void SampledAnim::GetSamples(std::vector<float>& samples)
{
	samples.assign(m_samples.begin(), m_samples.end());
}

inline size_t SampledAnim::GetNumSamples() const
{
	return m_samples.size();
}

inline void SampledAnim::SetNumSamples(size_t numSamples)
{
	m_samples.resize(numSamples);
}

inline SampledAnim* SampledAnim::Clone() const
{
	return new SampledAnim(*this);
}

inline bool SampledAnim::IsConstant() const
{
	return m_samples.size() == 1;
}

inline void SampledAnim::SetToConstant(AnimSample constSample)
{
	m_samples.assign(1, constSample);
}

inline AnimSample& SampledAnim::operator[](size_t index)
{
	return m_samples[index];
}

inline AnimSample const& SampledAnim::operator[](size_t index) const
{
	return m_samples[index];
}

//-------------------------------------------------------------------------------------------------------

// A spline based anim
class SplineAnim : public Anim
{
public:
	SplineAnim(std::vector<HermiteSpline> const& splines);
	SplineAnim(HermiteSpline const& spline);

	SplineAnim* Clone() const;
	bool DetermineIfConstant(float const tolerance);
	bool IsConstant() const;

	bool IsEqual(AnimSample const sample, float const tolerance);
	void SetToConstant(AnimSample constSample);

public:
	std::vector<HermiteSpline>	m_splines;
};

inline SplineAnim::SplineAnim(std::vector<HermiteSpline> const& splines)
	: m_splines(splines)
{
}

inline SplineAnim::SplineAnim(HermiteSpline const& spline)
{
	m_splines.push_back(spline);
}

inline SplineAnim* SplineAnim::Clone() const
{
	return new SplineAnim(*this);
}

inline bool SplineAnim::DetermineIfConstant(float const /*tolerance*/)
{
	return false;
}

inline bool SplineAnim::IsConstant() const
{
	return false;
}

inline bool SplineAnim::IsEqual(AnimSample const /*sample*/, float const /*tolerance*/)
{
	return false;
}

inline void SplineAnim::SetToConstant(AnimSample /*constSample*/)
{
}

//-------------------------------------------------------------------------------------------------------

} // AnimProcessing
} // Tools
} // OrbisAnim
