/*
* Copyright (c) 2017 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/gameplay/nd-upgrades-controller.h"
#include "gamelib/state-script/ss-manager.h"

#include "corelib/util/json-dom-utils.h"

static ScriptPointer<DC::Map> s_upgradeDefMap;

//-----------------------------------------------------------------------------------------------//
// NdUpgradeData: a class to manage upgrades
//-----------------------------------------------------------------------------------------------//
const DC::Map* NdUpgradeData::GetUpgradeDefMap(StringId64 upgradeDefId)
{
	// This will buy us nothing if a static instance needs to represent multiple data, but will work fine for T2.
	if (!s_upgradeDefMap || s_upgradeDefMap.GetId() != upgradeDefId)
	{
		s_upgradeDefMap = ScriptPointer<DC::Map>(upgradeDefId, SID("player-upgrade-settings"));
	}

	GAMEPLAY_ASSERT(s_upgradeDefMap);
	return &(*s_upgradeDefMap);
}

//-----------------------------------------------------------------------------------------------//
const DC::NdUpgrade* NdUpgradeData::LookupUpgradeById(const DC::Map* pMap, StringId64 upgradeId)
{
	if (pMap != nullptr)
	{
		const DC::NdUpgrade* pUpgradeDef = ScriptManager::MapLookup<DC::NdUpgrade>(pMap, upgradeId);
		return pUpgradeDef;
	}
	return nullptr;
}

//-----------------------------------------------------------------------------------------------//
const DC::NdUpgrade* NdUpgradeData::LookupUpgradeById(StringId64 upgradeDefId, StringId64 upgradeId)
{
	const DC::Map* pMap = GetUpgradeDefMap(upgradeDefId);
	return LookupUpgradeById(pMap, upgradeId);
}

//-----------------------------------------------------------------------------------------------//
bool NdUpgradeData::IsUpgraded(StringId64 upgradeDefId, StringId64 upgradeId, const DisabledUpgrades* pDisabled) const
{
	const DC::Map* pMap = GetUpgradeDefMap(upgradeDefId);
	return IsUpgraded(pMap, upgradeId, pDisabled);
}

//-----------------------------------------------------------------------------------------------//
bool NdUpgradeData::IsUpgraded(const DC::Map* pMap, StringId64 upgradeId, const DisabledUpgrades* pDisabled) const
{
	if (upgradeId == INVALID_STRING_ID_64)
		return false;

	I32 index = GetBitIndex(pMap, upgradeId);
	if (index < 0)
		return false;

	if (pDisabled != nullptr && pDisabled->m_flags[index])
		return false;

	return m_upgradeBits.IsBitSet(index);
}

//-----------------------------------------------------------------------------------------------//
bool NdUpgradeData::UpgradeExists(StringId64 upgradeDefId, StringId64 upgradeId) const
{
	return LookupUpgradeById(upgradeDefId, upgradeId) != nullptr;
}

//-----------------------------------------------------------------------------------------------//
bool NdUpgradeData::IsUpgradeAvailable(StringId64 upgradeDefId, StringId64 upgradeId) const
{
	return UpgradeExists(upgradeDefId, upgradeId) && !IsUpgraded(upgradeDefId, upgradeId);
}

//-----------------------------------------------------------------------------------------------//
I32 NdUpgradeData::GetActiveCount() const
{
	return m_upgradeBits.CountSetBits();
}

//-----------------------------------------------------------------------------------------------//
I32 NdUpgradeData::GetAvailableCount(StringId64 upgradeDefId) const
{
	I32 count = 0;
	const DC::Map* pMap = GetUpgradeDefMap(upgradeDefId);
	if (pMap != nullptr)
	{
		for (int ii = 0; ii < pMap->m_count; ii++)
		{
			const StringId64 upgradeId = pMap->m_keys[ii];
			if (!IsUpgraded(pMap, upgradeId))
				count++;
		}
	}

	return count;
}

//-----------------------------------------------------------------------------------------------//
StringId64 NdUpgradeData::GetUpgradeIdByBitIndex(const DC::Map* pMap, int bitIdx)
{
	if (pMap != nullptr)
	{
		if (bitIdx >= 0 && bitIdx < pMap->m_count)
			return pMap->m_keys[bitIdx];
	}

	return INVALID_STRING_ID_64;
}

//-----------------------------------------------------------------------------------------------//
StringId64 NdUpgradeData::GetUpgradeIdByBitIndex(StringId64 upgradeDefId, int bitIdx)
{
	const DC::Map* pMap = GetUpgradeDefMap(upgradeDefId);
	return GetUpgradeIdByBitIndex(pMap, bitIdx);
}

//-----------------------------------------------------------------------------------------------//
I32 NdUpgradeData::GetBitIndex(const DC::Map* pMap, StringId64 upgradeId)
{
	if (pMap != nullptr)
	{
		for (int index = 0; index < pMap->m_count; index++)
		{
			if (pMap->m_keys[index] == upgradeId)
			{
				GAMEPLAY_ASSERT(index < kMaxNumUpgrades);
				return index;
			}
		}
	}

	return -1;
}

//-----------------------------------------------------------------------------------------------//
I32 NdUpgradeData::GetBitIndex(StringId64 upgradeDefId, StringId64 upgradeId)
{
	const DC::Map* pMap = GetUpgradeDefMap(upgradeDefId);
	return GetBitIndex(pMap, upgradeId);
}

bool g_debugUpgradeDataChange = false;

//-----------------------------------------------------------------------------------------------//
void NdUpgradeData::GiveUpgrade(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
								const DC::Map* pMap, StringId64 upgradeId, MutableNdGameObjectHandle hOwner)
{
	const DC::NdUpgrade* pUpgradeDef = LookupUpgradeById(pMap, upgradeId);
	if (pUpgradeDef != nullptr)
	{
		I32 bitIndex = GetBitIndex(pMap, upgradeId);
		GAMEPLAY_ASSERT(bitIndex >= 0);
		m_upgradeBits.SetBit(bitIndex);
		g_ssMgr.BroadcastEvent(SID("player-upgrade-enabled"), upgradeId);
		SendEvent(SID("give-upgrade"), hOwner, upgradeId);

		if (FALSE_IN_FINAL_BUILD(g_debugUpgradeDataChange))
		{
			const char* pOwnerName = DevKitOnly_StringIdToString(hOwner.GetUserId());
			MsgCon("give-upgrade: %s to %s, func:%s\n", 
				DevKitOnly_StringIdToString(upgradeId), 
				pOwnerName ? pOwnerName : "unknown",
				sourceFunc);
			MsgOut("give-upgrade: %s to %s, func:%s\n",
				DevKitOnly_StringIdToString(upgradeId),
				pOwnerName ? pOwnerName : "unknown",
				sourceFunc);
		}
	}
	else
	{
		MsgConErr("give-upgrade, couldn't find '%s'\n", DevKitOnly_StringIdToString(upgradeId));
	}
}

//-----------------------------------------------------------------------------------------------//
void NdUpgradeData::GiveUpgrade(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
								StringId64 upgradeDefId, StringId64 upgradeId, MutableNdGameObjectHandle hOwner)
{
	const DC::Map* pMap = GetUpgradeDefMap(upgradeDefId);
	GiveUpgrade(sourceFile, sourceLine, sourceFunc, pMap, upgradeId, hOwner);
}

//-----------------------------------------------------------------------------------------------//
void NdUpgradeData::RemoveUpgrade(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
								  const DC::Map* pMap, StringId64 upgradeId, MutableNdGameObjectHandle hOwner)
{
	const DC::NdUpgrade* pUpgradeDef = LookupUpgradeById(pMap, upgradeId);
	if (pUpgradeDef != nullptr)
	{
		I32 bitIndex = GetBitIndex(pMap, upgradeId);
		GAMEPLAY_ASSERT(bitIndex >= 0);
		m_upgradeBits.ClearBit(bitIndex);
		g_ssMgr.BroadcastEvent(SID("player-upgrade-removed"), upgradeId);
		SendEvent(SID("remove-upgrade"), hOwner, upgradeId);

		if (FALSE_IN_FINAL_BUILD(g_debugUpgradeDataChange))
		{
			const char* pOwnerName = DevKitOnly_StringIdToString(hOwner.GetUserId());
			MsgCon("remove-upgrade: %s to %s, func:%s\n",
				DevKitOnly_StringIdToString(upgradeId),
				pOwnerName ? pOwnerName : "unknown",
				sourceFunc);
			MsgOut("remove-upgrade: %s to %s, func:%s\n",
				DevKitOnly_StringIdToString(upgradeId),
				pOwnerName ? pOwnerName : "unknown",
				sourceFunc);
		}
	}
	else
	{
		MsgConErr("remove-upgrade, couldn't find '%s'\n", DevKitOnly_StringIdToString(upgradeId));
	}
}

//-----------------------------------------------------------------------------------------------//
void NdUpgradeData::RemoveUpgrade(const char* sourceFile, U32F sourceLine, const char* sourceFunc, 
								  StringId64 upgradeDefId, StringId64 upgradeId, MutableNdGameObjectHandle hOwner)
{
	const DC::Map* pMap = GetUpgradeDefMap(upgradeDefId);
	RemoveUpgrade(sourceFile, sourceLine, sourceFunc, pMap, upgradeId, hOwner);
}

//-----------------------------------------------------------------------------------------------//
void NdUpgradeData::RemoveAll(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
							  StringId64 upgradeDefId, MutableNdGameObjectHandle hOwner)
{
	const DC::Map* pMap = GetUpgradeDefMap(upgradeDefId);

	for (U64 ii = m_upgradeBits.FindFirstSetBit(); ii != ~0ULL; ii = m_upgradeBits.FindNextSetBit(ii))
	{
		StringId64 upgradeId = GetUpgradeIdByBitIndex(pMap, ii);
		RemoveUpgrade(sourceFile, sourceLine, sourceFunc, pMap, upgradeId, hOwner);
	}
}

//-----------------------------------------------------------------------------------------------//
void NdUpgradeData::ApplyAll(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
							 StringId64 upgradeDefId, const NdUpgradeData& data, MutableNdGameObjectHandle hOwner)
{
	const DC::Map* pMap = GetUpgradeDefMap(upgradeDefId);

	for (U64 i = data.m_upgradeBits.FindFirstSetBit(); i != ~0ULL; i = data.m_upgradeBits.FindNextSetBit(i))
	{
		StringId64 upgradeId = GetUpgradeIdByBitIndex(pMap, i);
		GiveUpgrade(sourceFile, sourceLine, sourceFunc, pMap, upgradeId, hOwner);
	}
}

//-----------------------------------------------------------------------------------------------//
// upgrade-manuals.
//-----------------------------------------------------------------------------------------------//
I32 NdUpgradeData::FindManualIndexByItemId(StringId64 itemId) const
{
	int index = -1;
	for (int kk = 0; kk < kMaxNumUpgrades; kk++)
	{
		if (m_upgradeManuals[kk] == itemId)
		{
			index = kk;
			break;
		}
	}
	return index;
}
//-----------------------------------------------------------------------------------------------//
void NdUpgradeData::AddUpgradeManual(StringId64 itemId)
{
	if (FindManualIndexByItemId(itemId) >= 0)
		return;

	for (int kk = 0; kk < kMaxNumUpgrades; kk++)
	{
		if (m_upgradeManuals[kk] == INVALID_STRING_ID_64)
		{
			m_upgradeManuals[kk] = itemId;
			break;
		}
	}
}

//-----------------------------------------------------------------------------------------------//
const DC::SymbolArray* NdUpgradeData::GetAllUpgradeManuals(bool isEllie)
{
	StringId64 groupId = isEllie ? SID("*ellie-upgrade-manuals*") : SID("*abby-upgrade-manuals*");
	const DC::SymbolArray* pManualArray = ScriptManager::Lookup<DC::SymbolArray>(groupId, nullptr);
	return pManualArray;
}

//-----------------------------------------------------------------------------------------------//
bool NdUpgradeData::IsUpgradeManual(StringId64 itemId)
{
	{
		const DC::SymbolArray* pArray = GetAllUpgradeManuals(true);
		if (pArray != nullptr)
		{
			for (int kk = 0; kk < pArray->m_count; kk++)
			{
				if (pArray->m_array[kk] == itemId)
					return true;
			}
		}
	}

	{
		const DC::SymbolArray* pArray = GetAllUpgradeManuals(false);
		if (pArray != nullptr)
		{
			for (int kk = 0; kk < pArray->m_count; kk++)
			{
				if (pArray->m_array[kk] == itemId)
					return true;
			}
		}
	}

	return false;
}

//-----------------------------------------------------------------------------------------------//
void NdUpgradeData::Write(GameSave::JsonBuilder& builder) const
{
	builder.BeginObject(2);

	//current upgrades
	builder.WriteKey(SID("bits"));		jdom::WriteBitArray(builder, m_upgradeBits);

	builder.WriteKey(SID("manuals"));
	jdom::WriteObjectArray(builder, m_upgradeManuals, kMaxNumUpgManuals, jdom::WriteStringId<GameSave::JsonBuilder>);

	//builder.WriteKey(SID("bars"));
	//	builder.BeginArray(kMaxNumBars);
	//	for (int i = 0; i < kMaxNumBars; ++i)
	//	{
	//		builder.WriteFloat(m_upgradeBars[i]);
	//	}
	//	builder.EndArray();

	//builder.WriteKey(SID("increaseBars"));
	//	builder.BeginArray(kMaxNumBars);
	//	for (int i = 0; i < kMaxNumBars; ++i)
	//	{
	//		builder.WriteFloat(m_increaseBars[i]);
	//	}
	//	builder.EndArray();

	builder.EndObject();
}

void NdUpgradeData::Read(GameSave::JsonValuePtr pValue)
{
	I32 dummy = 0;
	jdom::GetBitArrayFromKey(m_upgradeBits, pValue, SID("bits"));
	jdom::GetObjectArrayFromKey(m_upgradeManuals, dummy, kMaxNumUpgManuals, pValue, SID("manuals"), jdom::ReadStringId<GameSave::JsonValuePtr>);

	//GameSave::JsonValuePtr pArray = jdom::GetValueFromKey(pValue, SID("bars"));
	//if (pArray && pArray->Type() == jdom::EType::kArray)
	//{
	//	int actualCount = 0;
	//	const int count = pArray->NumElems();
	//	const int end = Min(count, kMaxNumBars);
	//	for (int i = 0; i < end; ++i)
	//	{
	//		if (actualCount < ARRAY_COUNT(m_upgradeBars))
	//		{
	//			GameSave::JsonValuePtr elem = pArray->Elem(i);
	//			m_upgradeBars[actualCount++] = elem->Float();
	//		}
	//	}
	//}

	//unspent points
	//GameSave::JsonValuePtr pIncreaseArray = jdom::GetValueFromKey(pValue, SID("increaseBars"));
	//if (pIncreaseArray && pIncreaseArray->Type() == jdom::EType::kArray)
	//{
	//	int actualCount = 0;
	//	const int count = pIncreaseArray->NumElems();
	//	const int end = Min(count, kMaxNumBars);
	//	for (int i = 0; i < end; ++i)
	//	{
	//		if (actualCount < ARRAY_COUNT(m_increaseBars))
	//		{
	//			GameSave::JsonValuePtr elem = pIncreaseArray->Elem(i);
	//			m_increaseBars[actualCount++] = elem->Float();
	//		}
	//	}
	//}
}

void NdUpgradeData::UnitTestPopulate(int i)
{
	m_upgradeBits.ClearAllBits();
	m_upgradeBits.SetBit(i);
	m_upgradeBits.SetBit(i + 32);

	//for (int j = 0; j < kMaxNumBars; ++j)
	//{
	//	m_upgradeBars[j] = (float)i * 2.0f;
	//	m_increaseBars[j] = (float)i * 2.0f;
	//}

}

bool NdUpgradeData::UnitTestCompare(const NdUpgradeData& other)
{
	UpgradeBits diff;
	diff.ClearAllBits();
	UpgradeBits::BitwiseXor(&diff, m_upgradeBits, other.m_upgradeBits);
	if (!diff.AreAllBitsClear())
		return false;

	//for (int j = 0; j < kMaxNumBars; ++j)
	//{
	//	const float delta = m_upgradeBars[j] - other.m_upgradeBars[j];
	//	if (delta > 1.0e-6f)
	//		return false;

	//	const float increaseDelta = m_increaseBars[j] - other.m_increaseBars[j];
	//	if (increaseDelta > 1.0e-6f)
	//		return false;
	//}

	return true;
}
