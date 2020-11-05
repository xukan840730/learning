/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef _ND_CHARACTER_MELEE_IMPULSE_H_
#define _ND_CHARACTER_MELEE_IMPULSE_H_

#include "gamelib/ndphys/havok-collision-cast.h"

class Character;

class CharacterMeleeImpulse
{
public:
	struct MeleeImpulseData
	{
		Point m_lastPos;
		U32 m_jointIndex;
		F32 m_radius;
		F32 m_impulse;
	};

	CharacterMeleeImpulse();
	~CharacterMeleeImpulse();

	void AddJoint(U32 jointIndex, F32 radius, F32 impulse);
	void RemoveJoint(U32 jointIndex);
	void ResetJoints();

	void PostJointUpdate(const Character* pCharacter);

protected:
	static const U32 kMaxNumJoints = 12;
	U32 m_numJoints;
	MeleeImpulseData m_data[kMaxNumJoints];
	F32 m_lastImpulses[kMaxNumJoints];
	HavokSphereCastJob m_castJob;
};

#endif	// _ND_CHARACTER_MELEE_IMPULSE_H_