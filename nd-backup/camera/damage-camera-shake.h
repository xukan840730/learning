/*
* Copyright (c) 2013 Naughty Dog, Inc. 
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#ifndef DAMAGE_CAMERA_SHAKE_H
#define DAMAGE_CAMERA_SHAKE_H

#include "ndlib/util/tracker.h"
#include "corelib/math/vec2.h"
#include "corelib/math/locator.h"

class DamageMovement
{
public:
	void Init(Vec2 vel, float springConst);
	void Update(float deltaTime);
	Vec2 GetDelta() const;
	bool IsDone() const;
	Vec2 GetOffset() const;
	void Scale(float factor);

private:
	float m_magnitude;
	float m_delta;
	float m_springConst;
	Vec2 m_dir;
	SpringTracker<float> m_spring;
};

class DamageMovementShake
{
public:
	DamageMovementShake();
	void Reset();
	void Update();

	Locator GetLocator( Locator loc, float blend = 1.0f, bool allowAmbientShake = true);
	void AddDamageMovement(Vec2 vel, float springConst, float maxDeflection);
	Vec2 GetMovementDeltaDeg() const { return m_damageMovementDelta; }
	float GetRollAngle() const;

private:
	static const int	kMaxNumDamageMovements = 16;
	DamageMovement		m_damageMovement[kMaxNumDamageMovements];
	int					m_numDamageMovements;
	float				m_maxAllowedDeflection;
	float				m_highWaterMark;
	Vec2				m_damageMovementDelta;
	Vec2				m_damageMovementTotal;
};

#endif //DAMAGE_CAMERA_SHAKE_H
