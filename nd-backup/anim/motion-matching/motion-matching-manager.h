/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-table.h"
#include "ndlib/script/script-manager.h"

#include "gamelib/anim/motion-matching/motion-matching-def.h"

/// --------------------------------------------------------------------------------------------------------------- ///
namespace DC
{
	struct MotionMatchingSet;
}

namespace DMENU
{
	class Component;
	class Menu;
}

class MotionMatchingSet;

/// --------------------------------------------------------------------------------------------------------------- ///
class MotionMatchingManager : public ScriptObserver
{
public:

	MotionMatchingManager();

	virtual void OnSymbolImported(StringId64 moduleId,
								  StringId64 symbol,
								  StringId64 type,
								  const void* pData,
								  const void* pOldData,
								  Memory::Context allocContext) override;
	virtual void OnSymbolUnloaded(StringId64 moduleId,
								  StringId64 symbol,
								  StringId64 type,
								  const void* pData,
								  Memory::Context allocContext) override;

	void Register(const ArtItemMotionMatchingSet* pSetItem);
	void Unregister(const ArtItemMotionMatchingSet* pSetItem);

	const ArtItemMotionMatchingSet* LookupArtItemMotionMatchingSetById(StringId64 id) const;
	const MotionMatchingSet* LookupMotionMatchingSetById(StringId64 id) const;

	bool DoesMotionMatchingSetExist(StringId64 id) const;
		
	DMENU::Menu* CreateMenu();
	DMENU::Menu* CreateSelectDebugAnimMenu();

private:
	using MMSetPtr = ScriptPointer<DC::MotionMatchingSet>;
	using MotionMatchingItemTable = HashTable<StringId64, const ArtItemMotionMatchingSet*>;

	static void InitSetsMenu(DMENU::Component* pComponent);
	static void InitSetMenu(DMENU::Component* pComponent);
	static void PopulateSelectAnimMenu(DMENU::Menu* pMenu);
	static void RepopulateSelectAnimMenu(DMENU::Component* pComponent);

	void UpdateMenu();
	
	mutable NdAtomicLock m_accessLock;
	ListArray<MMSetPtr> m_dcSets;
	MotionMatchingItemTable m_itemTable;
	DMENU::Menu* m_pDebugMenu = nullptr;
	DMENU::Menu* m_pSelectAnimMenu = nullptr;
};

extern MotionMatchingManager* g_pMotionMatchingMgr;
