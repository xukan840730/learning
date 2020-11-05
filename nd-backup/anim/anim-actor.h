/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ANIMACTOR_H
#define ANIMACTOR_H

#include "corelib/containers/map-array.h"

namespace DC
{
	struct AnimActor;
	struct AnimState;
	struct ActorStateList;
};

extern bool AnimActorLogin(const DC::AnimActor * actor);
extern const DC::AnimState* AnimActorFindState(const DC::AnimActor * actor, StringId64 stateId);

extern int AnimActorStateCount(const DC::AnimActor * actor);

class AnimActorStateIterator
{
private:
	const DC::AnimActor* m_pAnimActor;
	const DC::ActorStateList* m_pCurrentGroup;
	int m_groupIndex;
	int m_stateIndex;

public:
	AnimActorStateIterator(const DC::AnimActor* pAnimActor);
	const DC::AnimState* GetState();
	void Advance();
	bool AtEnd();
};

#endif // ANIMACTOR_H
