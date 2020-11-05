/* SCE CONFIDENTIAL
* $PSLibId$
* Copyright (C) 2015 Sony Computer Entertainment Inc.
* All Rights Reserved.
*/

#pragma once

#include <vector>
#include "icelib/common/error.h"

namespace OrbisAnim {
namespace Tools {
namespace AnimProcessing {

class HermiteSpline
{
public:
	struct Vector2
	{
		Vector2() {}
		explicit Vector2(float f) : m_x(f), m_y(f) {}
		explicit Vector2(float x, float y) : m_x(x), m_y(y) {}
		Vector2 operator*(float f) const { return Vector2(m_x * f, m_y * f); }
		float gradient() const { return m_x == 0.0f ? 0.0f : m_y / m_x; }
		float getX() const { return m_x; }
		float getY() const { return m_y; }
		float m_x;
		float m_y;
	};

	typedef Vector2 const& Vector2_arg;

	struct Knot	// aka control point
	{
		Vector2	m_position;
		Vector2	m_iVelocity;
		Vector2	m_oVelocity;
	};

	enum Infinity
	{
		kConstant = 0,
		kCycle,
		kOffset,
		kOscillate,
		kLinear, 
		kNone
	};

	HermiteSpline(Infinity preInfinity = kConstant, Infinity postInfinity = kConstant);
	~HermiteSpline();

	void addKnot(Knot& knot);
	size_t getNumKnots() const;
	Knot const& getKnot(size_t i) const;

	void setInfinity(Infinity both);
	void setPreInfinity(Infinity pre);
	void setPostInfinity(Infinity post);

	Infinity getPreInfinity() const;
	Infinity getPostInfinity() const;

	Vector2 evaluate(float t);					// direct spline evaluation
	float evaluateAsScalarAnim(float t);		// spline as scalar anim curve

private:

	bool validateAsAnimCurve() const;			// true if this can be evaluated as an anim curve (x monotonically increases)
	size_t findSegment(float t);

	// standard Hermite curve segment evaluation
	// a & d are curve end points and u & v are respective out & in velocities
	template<typename T> 
	T evaluateCurve(float t, T& a, T& u, T& d, T& v);		

	std::vector<Knot>	m_knots;
	Infinity			m_preInfinity;
	Infinity			m_postInfinity;
};

inline size_t HermiteSpline::getNumKnots() const
{
	return m_knots.size();
}

inline HermiteSpline::Knot const& HermiteSpline::getKnot(size_t i) const
{
	ITASSERT(i < getNumKnots());
	return m_knots[i];
}

inline void HermiteSpline::setInfinity(Infinity both)
{
	m_preInfinity = m_postInfinity = both;
}

inline void HermiteSpline::setPreInfinity(Infinity pre)
{
	m_preInfinity = pre;
}

inline void HermiteSpline::setPostInfinity(Infinity post)
{
	m_postInfinity = post;
}

inline HermiteSpline::Infinity HermiteSpline::getPreInfinity() const
{
	return m_preInfinity;
}

inline HermiteSpline::Infinity HermiteSpline::getPostInfinity() const
{
	return m_postInfinity;
}

} // AnimProcessing
} // Tools
} // OrbisAnim
