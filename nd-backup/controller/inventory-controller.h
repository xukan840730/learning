/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#ifndef AI_INVENTORY_CONTROLLER_H
#define AI_INVENTORY_CONTROLLER_H

#include "game/player/game-inventory.h"
#include "gamelib/gameplay/ai/controller/animaction-controller.h"

class IAiInventoryController : public AnimActionController
{
public:
	virtual bool AddItem(StringId64 itemId, I32 count) = 0; // returns actual amount added to count
	virtual void RemoveItem(StringId64 itemId, I32 count) = 0;

	virtual I32 GetItemIndex(StringId64 itemId) const = 0;
	virtual I32 GetItemCount(StringId64 itemId) const = 0;
	virtual I32 GetItemCountByIndex(U32F index) const = 0;
	virtual StringId64 GetItemIdByIndex(U32F index) const = 0;
	virtual U32 GetNumItems() const = 0;
	virtual GameInventory& GetInventory() = 0;
	virtual const GameInventory& GetInventory() const = 0;
};

IAiInventoryController* CreateAiInventoryController(Character* pOwner);

#endif // AI_INVENTORY_CONTROLLER_H

