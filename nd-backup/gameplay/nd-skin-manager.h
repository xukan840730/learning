/*
 * Copyright (c) 2014 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ND_SKIN_MANAGER_H
#define ND_SKIN_MANAGER_H

#include "gamelib/level/want-load.h"

// manages replacing character models and attached props with unlocked skins
// to make it so the skinmanager isn't just filled with code specific to different pieces of game code
// we are using this to only manage things in ndlib/gamelib so that those libraries are game agnostic
class NdSkinManager
{
public:	

	// if you want addnewactors to work, pre must be called before looping
	// over all elements with remap
	virtual void PreRemapLevelMgrTodoList() = 0;
	virtual void RemapLevelMgrTodoListActor(String& actorName, StringId64& actorNameId) = 0;

	class ITodoListElementAdder
	{
	public:
		virtual void AddElement(const String& elementName, StringId64 elementNameId) = 0;
	};

	// must be called after remap has gotten a chance to go over all the elements
	// so that we can know whether we need to add more elements
	virtual void AddNewActorsToLevelMgrTodoList(ITodoListElementAdder& elementAdder) = 0;
	
	virtual void PreLevelSet() = 0;
	virtual bool PreventActorLoad(StringId64 actorNameId) = 0;
	virtual void GatherWantLevels(FixedWantLoadArray& array) = 0;
	virtual bool HasPendingChanges() = 0;
	virtual void ApplyChanges() = 0;
	virtual void ClearPendingChanges() = 0;

	// same as above, if you want AreExtraActorsDoneLoading to work, you must call pre
	// before running over all actors with remap
	virtual void PreActorRemap() = 0;
	virtual void RemapActorId(StringId64& actorNameId) = 0;
	virtual bool AreExtraActorsDoneLoading() const = 0;

	// pActorName can be nullptr if we don't care about whether the name begins with the common prefix
	virtual void RemapCharLevelNameId(StringId64& actorNameId, const char*& pActorName) = 0;

	virtual bool RemapLookId(StringId64& lookId, const Process* pProcess) = 0;
};

extern NdSkinManager* g_pNdSkinManager;

#endif //ND_SKIN_MANAGER_H