/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef CHARACTER_LIP_SYNC_H
#define CHARACTER_LIP_SYNC_H

class AnimControl;

class LipSync
{
private:
	F32 m_blendIntensity;
	F32 m_prevIntensity;
public:
	LipSync();
	void Update(AnimControl* animControl, F32 intensity);
};

#endif // CHARACTER_LIP_SYNC_H
