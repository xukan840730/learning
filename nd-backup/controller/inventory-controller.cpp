/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */


#include "game/ai/controller/inventory-controller.h"
#include "game/ai/controller/animation-controllers.h"
#include "game/player/game-inventory-set.h"

#include "gamelib/gameplay/ai/agent/nav-character.h"
#include "gamelib/gameplay/nav/cover-action-pack.h"
#include "gamelib/gameplay/nav/nav-state-machine.h"

#include "ndlib/anim/anim-action.h"

class AiInventoryController : public IAiInventoryController
{
private:
	AnimAction m_animAction;
	GameInventory m_gameInventory;

public:
	
	// methods //

	AiInventoryController(Character* pOwner)
		: m_animAction()
	{
		InventoryCheckpoint invCheckpoint(SID("*player-inventory-t2*"));
		m_gameInventory.Alloc();
		m_gameInventory.Init(&invCheckpoint, nullptr, nullptr, pOwner);
	}

	virtual bool IsBusy() const override
	{
		return false;
	}

	virtual void UpdateStatus() override
	{
		NavCharacter* pCharacter = GetCharacter();
		if(!pCharacter)
			return;
		m_animAction.Update(pCharacter->GetAnimControl());
	}

	virtual bool AddItem(StringId64 itemId, I32 count) override
	{
		return m_gameInventory.GiveItem(itemId, count);
	}
	virtual void RemoveItem(StringId64 itemId, I32 count) override
	{
		m_gameInventory.RemoveItem(itemId, count);
	}
	virtual I32 GetItemIndex(StringId64 itemId) const override
	{
		// T2-TODO:
		//return m_gameInventory.GetItemIndex(itemId);
		return 0;
	}
	virtual I32 GetItemCount(StringId64 itemId) const override
	{
		return m_gameInventory.GetItemCount(itemId);
	}
	virtual I32 GetItemCountByIndex(U32F index) const override
	{
		// T2-TODO:
		//return m_gameInventory.GetItemCountByIndex(index);
		return 0;
	}
	virtual StringId64 GetItemIdByIndex(U32F index) const override
	{
		return m_gameInventory.GetItemIdByIndex(index);
	}
	virtual U32 GetNumItems() const override
	{
		return m_gameInventory.GetNumItems();
	}
	virtual GameInventory& GetInventory() override
	{
		return m_gameInventory;
	}
	virtual const GameInventory& GetInventory() const override
	{
		return m_gameInventory;
	}
};
 

IAiInventoryController* CreateAiInventoryController(Character* pOwner)
{
	return NDI_NEW AiInventoryController(pOwner);
}
