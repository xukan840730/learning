/* SCE CONFIDENTIAL
* $PSLibId$
* Copyright (C) 2015 Sony Computer Entertainment Inc.
* All Rights Reserved.
*/

#include <cfloat>
#include "hermitespline.h"

namespace OrbisAnim {
namespace Tools {
namespace AnimProcessing {

template<typename T> static inline T max(T a, T b) { return (a > b) ? a : b; }
template<typename T> static inline T min(T a, T b) { return (a > b) ? b : a; }
template<typename T> static inline T clamp(T a, T lo, T hi) { return max(min(a, hi), lo); }

static bool allElemEqual(HermiteSpline::Vector2_arg vec0, HermiteSpline::Vector2_arg vec1)
{
	return vec0.m_x == vec1.m_x && vec0.m_y == vec1.m_y;
}

static float recipf(float f) 
{
	return 1.0f / f;
}

static HermiteSpline::Vector2 operator-(HermiteSpline::Vector2_arg a, HermiteSpline::Vector2_arg b) 
{
	return HermiteSpline::Vector2(a.m_x - b.m_x, a.m_y - b.m_y);
}

static HermiteSpline::Vector2 operator+(HermiteSpline::Vector2_arg a, HermiteSpline::Vector2_arg b)
{
	return HermiteSpline::Vector2(a.m_x + b.m_x, a.m_y + b.m_y);
}

static HermiteSpline::Vector2 operator*(float a, HermiteSpline::Vector2_arg b)
{
	return b * a;
}

HermiteSpline::HermiteSpline(Infinity preInfinity, Infinity postInfinity)
	: m_preInfinity(preInfinity)
	, m_postInfinity(postInfinity)
{
}

HermiteSpline::~HermiteSpline()
{
}

void HermiteSpline::addKnot(Knot& knot)
{
	m_knots.push_back(knot);
}

template<typename T> 
inline T HermiteSpline::evaluateCurve(float t, T& a, T& u, T& d, T& v)
{
	float s(1 - t);
	return s*s*(1 + 2 * t)*a + t*t*(1 + 2 * s)*d + s*s*t*u - s*t*t*v;
}

// basic evaluate using integer part of t as knot index & fractional part as distance along spline segment
// knots are interpreted as 2d points
HermiteSpline::Vector2 HermiteSpline::evaluate(float t)
{
	// assumes at least 2 knots
	ITASSERT(getNumKnots() > 1);

	// evaluate segment
	float fi;
	t = modff(t, &fi);
	size_t i = (size_t)fi;
	Vector2 a = m_knots[i + 0].m_position;
	Vector2 u = m_knots[i + 0].m_oVelocity;
	Vector2 d = m_knots[i + 1].m_position;
	Vector2 v = m_knots[i + 1].m_iVelocity;
	return evaluateCurve(float(t), a, u, d, v);
}

// check spline knot[n].pos.x >= knot[n-1].pos.x for all n
bool HermiteSpline::validateAsAnimCurve() const
{
	if (getNumKnots() < 2) {
		return true;
	}
	for (size_t i = 1; i < m_knots.size(); i++) {
		if (m_knots[i].m_position.getX() < m_knots[i-1].m_position.getX()) {
			return false;
		}
	}
	return true;
}

size_t HermiteSpline::findSegment(float t)
{
	size_t l = 0;
	size_t h = getNumKnots() - 1;
	while ((l + 1) < h) {
		size_t m = (l + h) >> 1;
		bool isLess = t < m_knots[m].m_position.getX();
		h = isLess ? m : h;
		l = isLess ? l : m;
	}
	return l;
}

// Maya style anim curve evaluation including infinities & step keys
// knots interpreted as scalar keys where x is key time & y is key value
float HermiteSpline::evaluateAsScalarAnim(float t)
{
	ITASSERT(validateAsAnimCurve());

	// a single knot means a constant / static anim curve
	if (getNumKnots() == 1) {
		return m_knots[0].m_position.getY();
	}

	float x(t);
	float y(0);

	size_t const numKnots = m_knots.size();

	float firstX = m_knots[0].m_position.getX();
	float lastX = m_knots[numKnots - 1].m_position.getX();

	bool isPre = firstX > x;
	bool isPost = x > lastX;
	Infinity infType = (isPre | isPost) ? (isPre ? m_preInfinity : m_postInfinity) : kNone;

	if (infType != kNone) {

		float firstY = m_knots[0].m_position.getY();
		float lastY = m_knots[numKnots - 1].m_position.getY();

		if (infType == kConstant) {
			return isPre ? firstY : lastY;
		}

		float cycleOffset = isPost ? x - lastX : firstX - x;
		if (infType == kLinear) {
			Vector2 tangentOut = m_knots[numKnots - 1].m_oVelocity;
			Vector2 tangentIn = m_knots[0].m_iVelocity;
			if (isPost) {
				return lastY + cycleOffset * tangentOut.gradient();
			} else {
				return firstY - cycleOffset * tangentIn.gradient();
			}
		}

		float rangeX = lastX - firstX;
		float invRangeX = recipf(rangeX);
		int numCyclesI = int(cycleOffset * invRangeX);
		float numCyclesF = float(numCyclesI);
		float nx = cycleOffset - numCyclesF * rangeX;

		if (infType == kCycle || infType == kOffset) {
			x = isPre ? lastX - nx : firstX + nx;
		}

		if (infType == kOscillate) {
			bool isEvenCycle = (numCyclesI & 1) == 0;
			x = (isEvenCycle ^ isPost) ? firstX + nx : lastX - nx;
		}

		// clamp to range in case of floating point error
		x = clamp(x, firstX, lastX);

		// y offset
		float rangeY = lastY - firstY;
		if (infType == kOffset) {
			y = numCyclesF * rangeY + rangeY;
			y = isPost ? y : -y;
		}
	}
	
	size_t i = findSegment(x);

	// check for step keys (note only need to set out velocity)
	if (allElemEqual(m_knots[i].m_oVelocity, Vector2(0.0f))) {		// step (return key i value)
		return m_knots[i].m_position.getY();
	} else if (allElemEqual(m_knots[i].m_oVelocity, Vector2(FLT_MAX))) {	// stepNext (return key i+1 value)
		ITASSERT(i+1 < numKnots);			// we expect a next key
		return m_knots[i+1].m_position.getY();	
	}

	// the following is based on the Maya anim engine code...

	float const kOneThird(1.f / 3.f);		// to convert to Bezier equivalent
	
	Vector2 a = m_knots[i + 0].m_position;
	Vector2 u = m_knots[i + 0].m_oVelocity * kOneThird;
	Vector2 d = m_knots[i + 1].m_position;
	Vector2 v = m_knots[i + 1].m_iVelocity * kOneThird;
	
	// gradient at start of curve
	float m1 = u.getX() != float(0) ? u.getY() * recipf(u.getX()) : float(0);
	
	// gradient at end of curve
	float m2 = v.getX() != float(0) ? v.getY() * recipf(v.getX()) : float(0);

	// difference between 2 key frames
	Vector2 da = d - a;
	float dx = da.getX();
	float dy = da.getY();

	float d1 = dx * m1;		// delta x by start tangent
	float d2 = dx * m2;		// delta x by end tangent

	float rdx = recipf(dx);

	// power basis? coefficients (t^3, t^2, t^1, t^0)
	float c0 = (d1 + d2 - dy - dy) * rdx * rdx * rdx;
	float c1 = (dy + dy + dy - d1 - d1 - d2) * rdx * rdx;
	float c2 = m1;
	float c3 = a.getY();

	// evaluate
	float tx = x - a.getX();
	float r = tx * (tx * (tx * c0 + c1) + c2) + c3;

	// don't forget the y offset
	return r + y;
}

} // AnimProcessing
} // Tools
} // OrbisAnim
