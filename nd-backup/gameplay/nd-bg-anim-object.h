/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef _ND_BG_ANIM_OBJECT_H_
#define _ND_BG_ANIM_OBJECT_H_ 

#include "gamelib/gameplay/nd-simple-object.h"

class AnimControl;
class ProcessSpawnInfo;

// --------------------------------------------------------------------------------------------------

struct AttachedFgMapping;
struct Background;

class NdBgAnimObject : public NdSimpleObject
{
public:
	typedef NdSimpleObject ParentClass;

	STATE_DECLARE_OVERRIDE(Active);

	NdBgAnimObject() {}
	
	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual Err SetupAnimControl(AnimControl* pAnimControl) override;
	virtual Err PostInit(const ProcessSpawnInfo& spawn) override;
	virtual void PostAnimBlending_Async() override;
	
protected:
	StringId64 m_tagId;

	static const int kMaxNumMappings = 500;
	int m_numMappings;

	const Background* m_pBackground[kMaxNumMappings];
	StringId64 m_backgroundNameId[kMaxNumMappings];
	const AttachedFgMapping* m_pAttachedFgMapping[kMaxNumMappings];
	int m_jointIndex[kMaxNumMappings];
	int m_iFgMap[kMaxNumMappings];
};

class NdBgAnimObject::Active : public NdBgAnimObject::ParentClass::Active
{
public:
	typedef NdBgAnimObject::ParentClass::Active ParentClass;
	BIND_TO_PROCESS(NdBgAnimObject);
};

// --------------------------------------------------------------------------------------------------

void ClearBatchUpdateListForAllBgAnimObjects();
void KickBatchUpdateAllBgAnimObjects();
void WaitForBatchUpdateAllBgAnimObjects();

#endif // _ND_BG_ANIM_OBJECT_H_ 


