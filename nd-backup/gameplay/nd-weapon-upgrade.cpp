/*
* Copyright (c) 2017 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "ndlib/net/nd-net-info.h"
#include "ndlib/net/nd-net-game-manager.h"
#include "gamelib/facts/fact-manager.h"
#include "gamelib/gameplay/nd-weapon-upgrade.h"
#include "gamelib/script/nd-script-utils.h"

/// --------------------------------------------------------------------------------------------------------------- ///
// WeaponUpgradesInfo
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::Map* WeaponUpgradesInfo::GetNumericUpgradesMap()
{
	if (g_ndConfig.m_pNetInfo->IsNetActive())
	{
		return g_ndConfig.m_pNetInfo->m_pNetGameManager->GetNumericWeaponUpgradesMap();
	}
	else
	{
		static ScriptPointer<DC::Map> s_pWeaponUpgrades(SID("*weapon-upgrades*"));
		return s_pWeaponUpgrades;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::Map* WeaponUpgradesInfo::LookupWeaponUpgradeMap(StringId64 weaponId)
{
	const DC::Map* pMap = GetNumericUpgradesMap();
	if (pMap)
	{
		return ScriptManager::MapLookup<DC::Map>(pMap, weaponId);
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::WeaponUpgradeSet* WeaponUpgradesInfo::LookupUpgradeSet(StringId64 weaponId, StringId64 upgradeTypeId)
{
	const DC::Map* pUpgradeMap = LookupWeaponUpgradeMap(weaponId);
	if (pUpgradeMap)
		return LookupUpgradeSet(pUpgradeMap, upgradeTypeId);

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::WeaponUpgradeSet* WeaponUpgradesInfo::LookupUpgradeSet(StringId64 weaponId, DC::WeaponUpgradeType upgType)
{
	const DC::Map* pUpgradeMap = LookupWeaponUpgradeMap(weaponId);
	if (pUpgradeMap)
		return LookupUpgradeSet(pUpgradeMap, upgType);

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::WeaponUpgradeSet* WeaponUpgradesInfo::LookupUpgradeSet(const DC::Map* pMap, StringId64 upgradeTypeId)
{
	return ScriptManager::MapLookup<DC::WeaponUpgradeSet>(pMap, upgradeTypeId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::WeaponUpgradeSet* WeaponUpgradesInfo::LookupUpgradeSet(const DC::Map* pMap, DC::WeaponUpgradeType upgType)
{
	ASSERT(upgType > DC::kWeaponUpgradeTypeNone && upgType < DC::kWeaponUpgradeTypeNumUpgradeTypes);
	StringId64 upgradeTypeId = WeaponUpgradesInfo::WeaponUpgradeTypeToStringId(upgType);
	return LookupUpgradeSet(pMap, upgradeTypeId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WeaponUpgradesInfo::UpgradeToLevel(StringId64 weaponDefId, I8 level)
{
	const DC::Map* pUpgradeMap = LookupWeaponUpgradeMap(weaponDefId);
	if (pUpgradeMap)
	{
		for (U32 iUpgrade = 0; iUpgrade < pUpgradeMap->m_count; iUpgrade++)
		{
			StringId64 upgradeTypeId = pUpgradeMap->m_keys[iUpgrade];
			const DC::WeaponUpgradeSet* pUpgradeSet = LookupUpgradeSet(pUpgradeMap, upgradeTypeId);
			if (pUpgradeSet)
			{
				const I32 upgradeLevel = Min((I32)level, pUpgradeSet->m_numLevels);
				SetLevel(pUpgradeSet->m_type, upgradeLevel);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 WeaponUpgradesInfo::GetUpgradeFloatValue(StringId64 weaponDefId, DC::WeaponUpgradeType upgType, F32 defVal) const
{
	I8 level = GetLevel(upgType);
	if (level > 0)
	{
		const DC::WeaponUpgradeSet* pSet = LookupUpgradeSet(weaponDefId, upgType);
		if (pSet)
		{
			level = CheckUpgradeLevel(pSet, level, weaponDefId, upgType);
			if (level > 0)
			{
				ASSERT(pSet->m_type == upgType);
				return pSet->m_levels[level - 1].m_valueFloat;
			}
		}
	}
	return defVal;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32 WeaponUpgradesInfo::GetUpgradeIntValue(StringId64 weaponDefId, DC::WeaponUpgradeType upgType, I32 defVal) const
{
	I8 level = GetLevel(upgType);
	if (level > 0)
	{
		const DC::WeaponUpgradeSet* pSet = LookupUpgradeSet(weaponDefId, upgType);
		if (pSet)
		{
			level = CheckUpgradeLevel(pSet, level, weaponDefId, upgType);
			if (level > 0)
			{
				return pSet->m_levels[level - 1].m_valueInt;
			}
		}
	}
	return defVal;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool WeaponUpgradesInfo::GetUpgradeBoolValue(StringId64 weaponDefId, DC::WeaponUpgradeType upgType, bool defVal) const
{
	I32 ival = GetUpgradeIntValue(weaponDefId, upgType, -1);
	if (ival < 0)
		return defVal;
	return ival != 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 WeaponUpgradesInfo::WeaponUpgradeTypeToStringId(DC::WeaponUpgradeType type)
{
	ASSERT(type > DC::kWeaponUpgradeTypeNone && type < DC::kWeaponUpgradeTypeNumUpgradeTypes);
	return GetDcEnumSymbol(SID("*weapon-upgrade-type-names*"), type);
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::WeaponUpgradeType WeaponUpgradesInfo::StringIdToWeaponUpgradeType(StringId64 weaponUpgradeTypeName)
{
	I32 ret = GetDcEnumFromSymbol(SID("*weapon-upgrade-type-names*"), weaponUpgradeTypeName);
	return (DC::WeaponUpgradeType)ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WeaponUpgradesInfo::DebugShow() const
{
	for (int ii = 0; ii < ARRAY_ELEMENT_COUNT(m_levels); ii++)
	{
		if (m_levels[ii] > 0)
		{
			StringId64 upgradeTypeName = WeaponUpgradeTypeToStringId((DC::WeaponUpgradeType)ii);
			MsgCon("%s level: %d\n", DevKitOnly_StringIdToString(upgradeTypeName), m_levels[ii]);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/// WeaponUpgradeBundles
/// --------------------------------------------------------------------------------------------------------------- ///

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::Map* WeaponUpgrades::GetWeaponUpgradeBundlesMap()
{
	if (g_ndConfig.m_pNetInfo->IsNetActive())
	{
		return g_ndConfig.m_pNetInfo->m_pNetGameManager->GetWeaponUpgradeBundlesMap();
	}
	else
	{
		static ScriptPointer<DC::Map> s_weaponUpgradeBundles(SID("*weapon-upgrade-bundles*"));
		return s_weaponUpgradeBundles;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::WeaponUpgradeBundle* WeaponUpgrades::LookupUpgradeBundle(StringId64 weaponId, StringId64 bundleId, bool disallowNull)
{
	if (EngineComponents::GetNdGameInfo()->GetOverallDifficulty() == DC::kDifficultySettingGrounded)
	{
		static ScriptPointer<DC::Map> s_weaponUpgradeBundles(SID("*weapon-upgrade-bundles-grounded-override*"));
		const DC::Map* pMap = s_weaponUpgradeBundles.GetTyped();
		if (pMap != nullptr)
		{
			const DC::WeaponUpgradeBundles* pBundles = ScriptManager::MapLookup<DC::WeaponUpgradeBundles>(pMap, weaponId);
			if (pBundles && pBundles->m_upgradeBundles)
			{
				for (int ii = 0; ii < pBundles->m_upgradeBundles->m_count; ii++)
				{
					if (pBundles->m_upgradeBundles->m_array[ii].m_bundleId == bundleId)
						return &pBundles->m_upgradeBundles->m_array[ii];
				}
			}
		}
	}

	const DC::Map* pMap = GetWeaponUpgradeBundlesMap();
	if (pMap)
	{
		const DC::WeaponUpgradeBundles* pBundles = ScriptManager::MapLookup<DC::WeaponUpgradeBundles>(pMap, weaponId);
		if (pBundles && pBundles->m_upgradeBundles)
		{
			for (int ii = 0; ii < pBundles->m_upgradeBundles->m_count; ii++)
			{
				if (pBundles->m_upgradeBundles->m_array[ii].m_bundleId == bundleId)
					return &pBundles->m_upgradeBundles->m_array[ii];
			}
		}
	}
	if (disallowNull)
	{
		static DC::WeaponUpgradeBundle s_nullUpgradeBundle;
		static bool s_nullUpgradeBundleInited = false;
		if (!s_nullUpgradeBundleInited)
		{
			s_nullUpgradeBundleInited = true;

			static DC::CraftingItemArray s_nullCraftingArray;
			s_nullUpgradeBundle.m_resources = &s_nullCraftingArray;
		}
		return &s_nullUpgradeBundle;
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WeaponUpgrades::GetRawUpgradeBundles(StringId64 weaponId, ListArray<StringId64>& outBundles)
{
	outBundles.Clear();

	const DC::Map* pMap = GetWeaponUpgradeBundlesMap();
	if (pMap)
	{
		const DC::WeaponUpgradeBundles* pBundles = ScriptManager::MapLookup<DC::WeaponUpgradeBundles>(pMap, weaponId);
		if (pBundles && pBundles->m_upgradeBundles)
		{
			for (int i = 0; i < pBundles->m_upgradeBundles->m_count; i++)
				outBundles.PushBack(pBundles->m_upgradeBundles->m_array[i].m_bundleId);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::WeaponVisualMods* WeaponUpgrades::GetDefaultVisualMods(StringId64 weaponId)
{
	const DC::Map* pMap = GetWeaponUpgradeBundlesMap();
	if (pMap)
	{
		const DC::WeaponUpgradeBundles* pBundles = ScriptManager::MapLookup<DC::WeaponUpgradeBundles>(pMap, weaponId);
		if (pBundles)
		{
			return pBundles->m_defaultStateMods;
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32 WeaponUpgrades::FindBundleIndex(StringId64 bundleId, const ListArray<StringId64>& rawBundles)
{
	for (int ii = 0; ii < rawBundles.Size(); ii++)
		if (rawBundles[ii] == bundleId)
			return ii;

	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool WeaponUpgrades::IsBundleUpgraded(StringId64 bundleId, const UpgradeBits& upgradeBits) const
{
	StringId64* blocks = STACK_ALLOC(StringId64, 64);
	ListArray<StringId64> rawBundles(64, blocks);
	WeaponUpgrades::GetRawUpgradeBundles(m_weaponDefId, rawBundles);

	I32 bundleIndex = FindBundleIndex(bundleId, rawBundles);
	return bundleIndex != -1 ? ((upgradeBits & (1ULL << bundleIndex)) != 0) : false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WeaponUpgrades::GetUpgradeBundles(ListArray<StringId64>& outBundles, const UpgradeBits& upgradeBits) const
{
	GetUpgradeBundles(m_weaponDefId, upgradeBits, outBundles);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WeaponUpgrades::GetUpgradeBundles(StringId64 weaponId, const UpgradeBits& upgradeBits, ListArray<StringId64>& outBundles)
{
	outBundles.Clear();

	if (upgradeBits != 0)
	{
		StringId64* blocks = STACK_ALLOC(StringId64, 64);
		ListArray<StringId64> rawBundles(64, blocks);
		WeaponUpgrades::GetRawUpgradeBundles(weaponId, rawBundles);

		for (int ii = 0; ii < rawBundles.Size(); ii++)
		{
			if ((upgradeBits & (1ULL << ii)) != 0)
			{
				outBundles.PushBack(rawBundles[ii]);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WeaponUpgrades::AddUpgradeBundle(StringId64 bundleId)
{
	//if (IsBundleUpgraded(bundleId, upgradeBits))
	//	return;

	const DC::WeaponUpgradeBundle* pNewBundle = LookupUpgradeBundle(m_weaponDefId, bundleId);
	if (pNewBundle)
	{
		m_lastAppliedUpgradeBundle = bundleId;
		if (pNewBundle->m_upgradeRefs)
		{
			for (int ii = 0; ii < pNewBundle->m_upgradeRefs->m_count; ii++)
			{
				const DC::WeaponUpgradeRef& upgradeRef = pNewBundle->m_upgradeRefs->m_array[ii];
				SetLevel(upgradeRef.m_upgradeType, upgradeRef.m_level);
			}
		}
	}
	else
	{
		MsgConScriptError("Couldn't find upgrade-bundle %s for %s!\n", 
			DevKitOnly_StringIdToString(bundleId),
			DevKitOnly_StringIdToString(m_weaponDefId));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WeaponUpgrades::RmvUpgradeBundle(StringId64 bundleId)
{
	//if (!IsBundleUpgraded(bundleId, upgradeBits))
	//	return;

	const DC::WeaponUpgradeBundle* pBundleToRemove = LookupUpgradeBundle(m_weaponDefId, bundleId);
	if (pBundleToRemove)
	{
		m_lastAppliedUpgradeBundle = INVALID_STRING_ID_64;
		if (pBundleToRemove->m_upgradeRefs)
		{
			for (int ii = 0; ii < pBundleToRemove->m_upgradeRefs->m_count; ii++)
			{
				const DC::WeaponUpgradeRef& upgradeRef = pBundleToRemove->m_upgradeRefs->m_array[ii];
				I8 newLevel = Max(upgradeRef.m_level - 1, 0);
				SetLevel(upgradeRef.m_upgradeType, newLevel);
			}
		}
	}
	else
	{
		MsgConScriptError("Couldn't find upgrade-bundle %s for %s!\n", 
			DevKitOnly_StringIdToString(bundleId),
			DevKitOnly_StringIdToString(m_weaponDefId));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WeaponUpgrades::GetDefaultVisualMods(StringId64 weaponId, ListArray<JointMod>& jointMods, ListArray<ShaderParams>& outShaderParams)
{
	FactDictionary::NodeType jointModsDictMem[64];
	FactDictionary jointModsDict(64, jointModsDictMem);

	FactDictionary::NodeType shaderDictMem[64];
	FactDictionary shaderDict(64, shaderDictMem);

	const DC::WeaponVisualMods* pDefaultVisualMods = GetDefaultVisualMods(weaponId);
	if (pDefaultVisualMods)
	{
		if (pDefaultVisualMods->m_jointMods)
		{
			for (int ii = 0; ii < pDefaultVisualMods->m_jointMods->m_count; ii++)
			{
				const DC::WeaponModsJoint& def = pDefaultVisualMods->m_jointMods->m_array[ii];
				jointModsDict.Set(def.m_jointName, def.m_toggle);
			}
		}

		if (pDefaultVisualMods->m_shaderMods)
		{
			for (int ii = 0; ii < pDefaultVisualMods->m_shaderMods->m_count; ii++)
			{
				const DC::WeaponModsShaderBlend& def = pDefaultVisualMods->m_shaderMods->m_array[ii];
				shaderDict.Set(def.m_name, def.m_value);
			}
		}
	}

	{
		const FactDictionary *const pDict = &jointModsDict;
		
		AtomicLockJanitorRead jj(pDict->GetRwLock(), FILE_LINE_FUNC);
		for (FactDictionary::ConstIterator it = pDict->Begin(); it != pDict->End(); ++it)
		{
			StringId64 jointId = it->first;
			bool on = it->second.GetBool();

			JointMod jointMod;
			jointMod.jointId = jointId;
			jointMod.m_on = on;
			jointMods.PushBack(jointMod);
		}
	}

	{
		const FactDictionary *const pDict = &shaderDict;
			
		AtomicLockJanitorRead jj(pDict->GetRwLock(), FILE_LINE_FUNC);
		for (FactDictionary::ConstIterator it = pDict->Begin(); it != pDict->End(); ++it)
		{
			StringId64 paramId = it->first;
			float value = it->second.GetFloat();

			ShaderParams newParams;
			newParams.paramId = paramId;
			newParams.value = value;
			outShaderParams.PushBack(newParams);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WeaponUpgrades::AddJointMods(ListArray<JointMod>& inoutMods, const JointMod& newMod)
{
	bool found = false;
	for (int kk = 0; kk < inoutMods.Size(); kk++)
	{
		if (inoutMods[kk].jointId == newMod.jointId)
		{
			inoutMods[kk].m_on = newMod.m_on;
			found = true;
		}
	}

	if (!found && !inoutMods.IsFull())
	{
		inoutMods.PushBack(newMod);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WeaponUpgrades::GetJointModifications(ListArray<JointMod>& jointMods, const UpgradeBits& upgradeBits, const ListArray<StringId64>* pSuppressedUpgrades) const
{
	FactDictionary::NodeType dictMem[64];
	FactDictionary dict(64, dictMem);

	const DC::WeaponVisualMods* pDefaultVisualMods = GetDefaultVisualMods(m_weaponDefId);
	if (pDefaultVisualMods && pDefaultVisualMods->m_jointMods)
	{
		for (int ii = 0; ii < pDefaultVisualMods->m_jointMods->m_count; ii++)
		{
			const DC::WeaponModsJoint& def = pDefaultVisualMods->m_jointMods->m_array[ii];
			dict.Set(def.m_jointName, def.m_toggle);
		}
	}

	StringId64* blocks = STACK_ALLOC(StringId64, 64);
	ListArray<StringId64> rawBundles(64, blocks);
	WeaponUpgrades::GetRawUpgradeBundles(m_weaponDefId, rawBundles);

	for (I32 iBundle = 0; iBundle < rawBundles.Size(); iBundle++)
	{
		if ((upgradeBits & (1ULL << iBundle)) != 0)
		{
			const StringId64 upgradeId = rawBundles[iBundle];

			if (pSuppressedUpgrades != nullptr && Contains(*pSuppressedUpgrades, upgradeId))
				continue;

			const DC::WeaponUpgradeBundle* pBundle = LookupUpgradeBundle(m_weaponDefId, upgradeId);
			if (pBundle && pBundle->m_visualMods && pBundle->m_visualMods->m_jointMods)
			{
				for (int ii = 0; ii < pBundle->m_visualMods->m_jointMods->m_count; ii++)
				{
					const DC::WeaponModsJoint& def = pBundle->m_visualMods->m_jointMods->m_array[ii];
					dict.Set(def.m_jointName, def.m_toggle);
				}
			}
		}
	}

	{
		const FactDictionary *const pDict = &dict;
		
		AtomicLockJanitorRead jj(pDict->GetRwLock(), FILE_LINE_FUNC);
		for (FactDictionary::ConstIterator it = pDict->Begin(); it != pDict->End(); ++it)
		{
			StringId64 jointId = it->first;
			bool on = it->second.GetBool();

			JointMod jointMod;
			jointMod.jointId = jointId;
			jointMod.m_on = on;
			jointMods.PushBack(jointMod);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WeaponUpgrades::GetShaderParams(ListArray<ShaderParams>& outShaderParams, const UpgradeBits& upgradeBits, const ListArray<StringId64>* pSuppressedUpgrades) const
{
	FactDictionary::NodeType dictMem[64];
	FactDictionary dict(64, dictMem);

	const DC::WeaponVisualMods* pDefaultVisualMods = GetDefaultVisualMods(m_weaponDefId);
	if (pDefaultVisualMods && pDefaultVisualMods->m_shaderMods)
	{
		for (int ii = 0; ii < pDefaultVisualMods->m_shaderMods->m_count; ii++)
		{
			const DC::WeaponModsShaderBlend& def = pDefaultVisualMods->m_shaderMods->m_array[ii];
			dict.Set(def.m_name, def.m_value);
		}
	}

	StringId64* blocks = STACK_ALLOC(StringId64, 64);
	ListArray<StringId64> rawBundles(64, blocks);
	WeaponUpgrades::GetRawUpgradeBundles(m_weaponDefId, rawBundles);

	for (I32 iBundle = 0; iBundle < rawBundles.Size(); iBundle++)
	{
		if ((upgradeBits & (1ULL << iBundle)) != 0)
		{
			const StringId64 upgradeId = rawBundles[iBundle];

			if (pSuppressedUpgrades != nullptr && Contains(*pSuppressedUpgrades, upgradeId))
				continue;

			const DC::WeaponUpgradeBundle* pBundle = LookupUpgradeBundle(m_weaponDefId, upgradeId);
			if (pBundle && pBundle->m_visualMods && pBundle->m_visualMods->m_shaderMods)
			{
				for (int ii = 0; ii < pBundle->m_visualMods->m_shaderMods->m_count; ii++)
				{
					const DC::WeaponModsShaderBlend& def = pBundle->m_visualMods->m_shaderMods->m_array[ii];
					dict.Set(def.m_name, def.m_value);
				}
			}
		}
	}

	{
		const FactDictionary *const pDict = &dict;
		
		AtomicLockJanitorRead jj(pDict->GetRwLock(), FILE_LINE_FUNC);
		for (FactDictionary::ConstIterator it = pDict->Begin(); it != pDict->End(); ++it)
		{
			StringId64 paramId = it->first;
			float value = it->second.GetFloat();

			ShaderParams newParams;
			newParams.paramId = paramId;
			newParams.value = value;
			outShaderParams.PushBack(newParams);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
WeaponUpgrades::UpgradeBits WeaponUpgrades::ToUpgradeBit(StringId64 weaponId, StringId64 bundleId)
{
	StringId64* blocks = STACK_ALLOC(StringId64, 64);
	ListArray<StringId64> rawBundles(64, blocks);
	WeaponUpgrades::GetRawUpgradeBundles(weaponId, rawBundles);

	U64 upgradeBits = 0;

	for (int ii = 0; ii < rawBundles.Size(); ii++)
	{
		if (rawBundles[ii] == bundleId)
		{
			upgradeBits |= (1ULL << ii);
			break;
		}
	}

	return upgradeBits;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WeaponUpgrades::UpgradeBitsToBundles(StringId64 weaponId, UpgradeBits upgradeBits, ListArray<StringId64>& outBundles)
{
	if (upgradeBits != 0)
	{
		StringId64* blocks = STACK_ALLOC(StringId64, 64);
		ListArray<StringId64> rawBundles(64, blocks);
		WeaponUpgrades::GetRawUpgradeBundles(weaponId, rawBundles);

		for (int ii = 0; ii < rawBundles.Size(); ii++)
		{
			if ((upgradeBits & (1ULL << ii)) != 0)
				outBundles.PushBack(rawBundles[ii]);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WeaponUpgrades::DebugShow(const UpgradeBits& upgradeBits) const
{
	MsgCon("------------------------------------\n");
	MsgCon("Weapon Upgrades: %s, bits:0x%llx\n", DevKitOnly_StringIdToString(m_weaponDefId), upgradeBits);

	StringId64* blocks = STACK_ALLOC(StringId64, 64);
	ListArray<StringId64> rawBundles(64, blocks);
	WeaponUpgrades::GetRawUpgradeBundles(m_weaponDefId, rawBundles);

	for (int ii = 0; ii < rawBundles.Size(); ii++)
	{
		if ((upgradeBits & (1ULL << ii)) != 0)
		{ 
			MsgCon("Upgraded Bundle: %s\n", DevKitOnly_StringIdToString(rawBundles[ii]));
		}
	}

	{
		JointMod* pBlock = STACK_ALLOC(JointMod, 64);
		ListArray<JointMod> jointMods(64, pBlock);
		GetJointModifications(jointMods, upgradeBits);

		MsgCon("Joint Mods:\n");
		for (int ii = 0; ii < jointMods.Size(); ii++)
		{
			if (jointMods[ii].m_on)
				MsgCon("(%s on) ", DevKitOnly_StringIdToString(jointMods[ii].jointId));
			else
				MsgCon("(%s off) ", DevKitOnly_StringIdToString(jointMods[ii].jointId));
		}
		if (jointMods.Size() > 0)
			MsgCon("\n");
	}

	{
		WeaponUpgrades::ShaderParams* pBlocks = STACK_ALLOC(WeaponUpgrades::ShaderParams, 16);
		ListArray<WeaponUpgrades::ShaderParams> shaderParams(16, pBlocks);
		GetShaderParams(shaderParams, upgradeBits);

		MsgCon("Shader Params:\n");
		for (int ii = 0; ii < shaderParams.Size(); ii++)
		{
			MsgCon("(%s %f) ", DevKitOnly_StringIdToString(shaderParams[ii].paramId), shaderParams[ii].value);
		}
		if (shaderParams.Size() > 0)
			MsgCon("\n");
	}

	ParentClass::DebugShow();

	MsgCon("------------------------------------\n");
}

/// --------------------------------------------------------------------------------------------------------------- ///
WeaponUpgrades::UpgradeBits WeaponUpgrades::GetFullUpgradeBits(StringId64 weaponId)
{
	UpgradeBits bits = 0;
	if (weaponId == INVALID_STRING_ID_64)
		return bits;
	
	const DC::Map* pMap = GetWeaponUpgradeBundlesMap();
	if (pMap)
	{
		const DC::WeaponUpgradeBundles* pBundles = ScriptManager::MapLookup<DC::WeaponUpgradeBundles>(pMap, weaponId);
		if (pBundles && pBundles->m_upgradeBundles)
		{
			for (int kk = 0; kk < pBundles->m_upgradeBundles->m_count; kk++)
			{
				const DC::WeaponUpgradeBundle& bundle = pBundles->m_upgradeBundles->m_array[kk];
				if (bundle.m_resources != nullptr)
				{
					bits |= 1ULL << kk;
				}
			}
		}
	}

	return bits;
}