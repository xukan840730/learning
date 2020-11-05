
/*
* Copyright (c) 2013 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/camera/damage-camera-shake.h"

#include "ndlib/process/clock.h"

Vec2 DamageMovement::GetDelta() const
{
	return m_dir*m_delta;
}

void DamageMovement::Init( Vec2 vel , float springConst )
{
	m_dir = Normalize(vel);
	m_magnitude = 0.0f;
	m_delta = 0.0f;
	m_springConst = springConst;
	m_spring.Reset();
	m_spring.m_speed = Length(vel);
}

void DamageMovement::Update( float deltaTime )
{	
	float prevMag = m_magnitude;
	m_magnitude = m_spring.Track(m_magnitude, 0.0f, deltaTime, m_springConst);
	m_delta = m_magnitude - prevMag;
}

bool DamageMovement::IsDone() const
{
	return m_magnitude < 0.01f && m_spring.m_speed < 0.01f;
}

Vec2 DamageMovement::GetOffset() const
{
	return m_dir*m_magnitude;
}

void DamageMovement::Scale( float factor )
{
	m_magnitude *= factor;
	m_delta *= factor;
	m_spring.Reset();
}



void DamageMovementShake::Reset()
{
	m_numDamageMovements = 0;
	m_damageMovementDelta = kZero;
	m_damageMovementTotal = kZero;
}

void DamageMovementShake::Update()
{
	float dt = GetProcessDeltaTime();

	Vec2 prevTotal = m_damageMovementTotal;

	m_damageMovementDelta = kZero;
	m_damageMovementTotal = kZero;
	for (int i=0; i<m_numDamageMovements; i++)
	{
		m_damageMovement[i].Update(dt);

		m_damageMovementDelta += m_damageMovement[i].GetDelta();
		m_damageMovementTotal += m_damageMovement[i].GetOffset();		

		if (m_damageMovement[i].IsDone())
		{
			m_damageMovement[i] = m_damageMovement[m_numDamageMovements-1];
			m_numDamageMovements--;
			i--;
			continue;
		}
	}
	
	float currentDeflection = Length(m_damageMovementTotal);
	if (currentDeflection > m_maxAllowedDeflection)
	{
		float scaleFactor = m_maxAllowedDeflection/currentDeflection;
		m_damageMovementTotal = m_damageMovementTotal * scaleFactor;
		m_damageMovementDelta = m_damageMovementTotal - prevTotal;
		for (int i=0; i<m_numDamageMovements; i++)
		{
			m_damageMovement[i].Scale(scaleFactor);
		}
	}
	m_highWaterMark = Max(Length(m_damageMovementTotal), m_highWaterMark);
	//MsgCon("Shake high water: %f\n", m_highWaterMark);
}

Locator DamageMovementShake::GetLocator( Locator loc, float blend /*= 1.0f*/, bool allowAmbientShake /*= true*/ )
{
	if (allowAmbientShake)
	{
		float rollAngle = GetRollAngle()*blend;
		Quat adjustment = QuatFromAxisAngle(kUnitXAxis, DEGREES_TO_RADIANS(m_damageMovementTotal.y*blend))*QuatFromAxisAngle(kUnitYAxis, DEGREES_TO_RADIANS(m_damageMovementTotal.x*blend))*QuatFromAxisAngle(kUnitZAxis, rollAngle);
		return Locator(loc.GetTranslation(), loc.GetRotation()*adjustment);
	}
	else
	{
		return loc;
	}	
}

void DamageMovementShake::AddDamageMovement(Vec2 vel, float springConst, float maxDeflection)
{
	if (m_numDamageMovements < kMaxNumDamageMovements)
	{
		m_damageMovement[m_numDamageMovements].Init(vel, springConst);
		m_numDamageMovements++;
	}
	m_maxAllowedDeflection = maxDeflection;
}

DamageMovementShake::DamageMovementShake()
{
	Reset();
	m_maxAllowedDeflection = kLargestFloat;
	m_highWaterMark = 0;
}

float DamageMovementShake::GetRollAngle() const
{
	return -DEGREES_TO_RADIANS(m_damageMovementTotal.x)/2.0f;
}


