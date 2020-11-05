
/*
 * Copyright (c) 2014 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ANIM_DATA_EVALUATION_H
#define ANIM_DATA_EVALUATION_H

class AnimStateSnapshot;
class ArtItemAnim;

class IAnimDataEval
{
public:
	class IAnimData
	{
	public:
		virtual ~IAnimData() {}
		
	};

	virtual ~IAnimDataEval() {}
	virtual IAnimData* EvaluateDataFromAnim(const ArtItemAnim* pAnim, float phase, const AnimStateSnapshot* pSnapshot) = 0;
	virtual IAnimData* Blend(IAnimData* pLeft, IAnimData* pRight, float blend) const = 0;
};


#endif
