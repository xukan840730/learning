/*
 * Copyright (c) 2003 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */
/// --------------------------------------------------------------------------------------------------------------- ///

#ifndef ND_ANIM_STRUCTS_H
#define ND_ANIM_STRUCTS_H

struct JointOverrideData
{
	enum ComponentOverrideMasks
	{
		kOverrideTranslationMask	= (1 << 0),
		kOverrideRotationMask		= (1 << 1),
		kOverrideScaleMask			= (1 << 2),
	};

	static const I16 kInvalidIndex = -1;

	U32 m_numJoints;
	U32 m_maxJoints;

	I16*	m_pJointIndex;
	U8*		m_pComponentFlags;
	Point*	m_pJointTrans;
	Quat*	m_pJointRot;
	float*	m_pJointScale;
};

#endif // ND_ANIM_STRUCTS_H

