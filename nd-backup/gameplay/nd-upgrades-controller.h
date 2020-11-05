/*
* Copyright (c) 2017 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "corelib/util/bit-array.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/scriptx/h/nd-upgrades-defines.h"

#include "gamelib/save/ndsavedata.h"

//-----------------------------------------------------------------------------------------------//
// NdUpgradeData: a class to manage upgrades
//-----------------------------------------------------------------------------------------------//
class NdUpgradeData
{
public:

	static constexpr int kMaxNumUpgrades = 256;

	struct DisabledUpgrades
	{
		DisabledUpgrades()
		{
			memset(m_flags, 0, sizeof(m_flags));
		}

		bool m_flags[kMaxNumUpgrades];
	};

	typedef BitArray<kMaxNumUpgrades> UpgradeBits;
	UpgradeBits m_upgradeBits;

	// used for save/load upgrade manuals.
	// player's current upgrade manuals are stored in game-inventory.
	static const I32 kMaxNumUpgManuals = 12;
	StringId64 m_upgradeManuals[kMaxNumUpgManuals];

	NdUpgradeData() { Reset(); }

	void Reset()
	{
		m_upgradeBits.ClearAllBits();
		memset(m_upgradeManuals, 0, sizeof(m_upgradeManuals));
	}
	
	bool IsUpgraded(StringId64 upgradeDefId, StringId64 upgradeId, const DisabledUpgrades* pDisabled = nullptr) const;
	bool IsUpgraded(const DC::Map* pMap, StringId64 upgradeId, const DisabledUpgrades* pDisabled = nullptr) const;
	bool UpgradeExists(StringId64 upgradeDefId, StringId64 upgradeId) const;
	bool IsUpgradeAvailable(StringId64 upgradeDefId, StringId64 upgradeId) const;

	void GiveUpgrade(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
					 const DC::Map* pMap, StringId64 upgradeId, MutableNdGameObjectHandle hOwner);
	void GiveUpgrade(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
					 StringId64 upgradeDefId, StringId64 upgradeId, MutableNdGameObjectHandle hOwner);

	void RemoveUpgrade(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
					   const DC::Map* pMap, StringId64 upgradeId, MutableNdGameObjectHandle hOwner);
	void RemoveUpgrade(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
					   StringId64 upgradeDefId, StringId64 upgradeId, MutableNdGameObjectHandle hOwner);

	void RemoveAll(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
				   StringId64 upgradeDefId, MutableNdGameObjectHandle hOwner);
	void ApplyAll(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
				  StringId64 upgradeDefId, const NdUpgradeData& data, MutableNdGameObjectHandle hOwner);

	I32 GetActiveCount() const; // get how many upgrades are active.
	I32 GetAvailableCount(StringId64 upgradeDefId) const; // get how many upgrades are available.

	static StringId64 GetUpgradeIdByBitIndex(const DC::Map* pMap, int bitIdx);
	static StringId64 GetUpgradeIdByBitIndex(StringId64 upgradeDefId, int bitIdx);

	static I32 GetBitIndex(const DC::Map* pMap, StringId64 upgradeId);
	static I32 GetBitIndex(StringId64 upgradeDefId, StringId64 upgradeId);

	//-----------------------------------------------------------------------------------------//
	// upgrade definitions.
	//-----------------------------------------------------------------------------------------//
	static const DC::NdUpgrade* LookupUpgradeById(const DC::Map* pMap, StringId64 upgradeId);
	static const DC::NdUpgrade* LookupUpgradeById(StringId64 upgradeDefId, StringId64 upgradeId);

	static const DC::Map* GetUpgradeDefMap(StringId64 upgradeDefId);

	//-----------------------------------------------------------------------------------------//
	// upgrade-manuals.
	//-----------------------------------------------------------------------------------------//
	void AddUpgradeManual(StringId64 itemId);
	static const DC::SymbolArray* GetAllUpgradeManuals(bool isEllie);
	static bool IsUpgradeManual(StringId64 itemId);
	void CopyManualsFrom(const NdUpgradeData& other)
	{
		for (int kk = 0; kk < kMaxNumUpgManuals; kk++)
			m_upgradeManuals[kk] = other.m_upgradeManuals[kk];
	}

private:
	I32 FindManualIndexByItemId(StringId64 itemId) const;

public:

	//-----------------------------------------------------------------------------------------//
	// Save/Load
	//-----------------------------------------------------------------------------------------//
	void Write(GameSave::JsonBuilder& builder) const;
	void Read(GameSave::JsonValuePtr pValue);

	void UnitTestPopulate(int i);
	bool UnitTestCompare(const NdUpgradeData& other);

};