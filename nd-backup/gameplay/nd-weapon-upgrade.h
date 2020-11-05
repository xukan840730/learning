/*
* Copyright (c) 2017 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "ndlib/util/bitstream.h"

#include "gamelib/facts/fact-manager.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/scriptx/h/weapon-upgrades-defines.h"
#include "gamelib/state-script/ss-manager.h"

typedef U64 WeaponUpgradeMask;

/// --------------------------------------------------------------------------------------------------------------- ///
/// WeaponUpgradesInfo
/// --------------------------------------------------------------------------------------------------------------- ///
class WeaponUpgradesInfo
{
public:
	WeaponUpgradesInfo()
	{
		Reset();
	}

	void Reset()
	{
		memset(m_levels, 0, sizeof(m_levels));
	}

	WeaponUpgradeMask ToMask() const
	{
		WeaponUpgradeMask upgradeMask = 0;
		for (int i = 0; i < ARRAY_ELEMENT_COUNT(m_levels); i++)
		{
			if (m_levels[i] != 0)
				upgradeMask |= (1 << i);
		}
		return upgradeMask;
	}

	void SetLevel(DC::WeaponUpgradeType type, I8 level)
	{
		ASSERT(type > DC::kWeaponUpgradeTypeNone && type < DC::kWeaponUpgradeTypeNumUpgradeTypes);
		m_levels[type] = level;
	}

	I8 GetLevel(DC::WeaponUpgradeType type) const
	{
		ASSERT(type > DC::kWeaponUpgradeTypeNone && type < DC::kWeaponUpgradeTypeNumUpgradeTypes);
		return m_levels[type];
	}

	void UpgradeToLevel(StringId64 weaponDefId, I8 level); // upgrade all to level x.

	void CopyFrom(const WeaponUpgradesInfo& info)
	{
		memcpy(m_levels, info.m_levels, sizeof(m_levels));
	}

	void DebugShow() const;

public:
	static const DC::Map* GetNumericUpgradesMap();
	static const DC::Map* LookupWeaponUpgradeMap(StringId64 weaponId);
	static const DC::WeaponUpgradeSet* LookupUpgradeSet(StringId64 weaponId, DC::WeaponUpgradeType upgType);
	static const DC::WeaponUpgradeSet* LookupUpgradeSet(StringId64 weaponId, StringId64 upgradeTypeId);

protected:
	static const DC::WeaponUpgradeSet* LookupUpgradeSet(const DC::Map* pMap, StringId64 upgradeTypeId);
	static const DC::WeaponUpgradeSet* LookupUpgradeSet(const DC::Map* pMap, DC::WeaponUpgradeType upgType);

public:
	static I8 CheckUpgradeLevel(const DC::WeaponUpgradeSet* pSet, I8 level, StringId64 weaponDefId, DC::WeaponUpgradeType upgType)
	{
		if (!pSet)
			return level;

		if (level > pSet->m_numLevels)
		{
			MsgConScriptError("The upgrade %d level %i for weapon %s is higher than the actual number of defined upgrade levels!\n",
				upgType,
				(int)level,
				DevKitOnly_StringIdToString(weaponDefId));
			level = pSet->m_numLevels;
		}
		return level;
	}

	template<typename T>
	const T* GetUpgradeValue(StringId64 weaponDefId, DC::WeaponUpgradeType upgType) const
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
					if (FALSE_IN_FINAL_BUILD(true))
					{
						// validate the type
						StringId64 typeId = ScriptManager::TypeOf(pSet->m_levels[level - 1].m_blindData);
						switch (typeId.GetValue())
						{
						case SID_VAL("weapon-upgrade-reload-multiple"):		GAMEPLAY_ASSERT(sizeof(T) == sizeof(DC::WeaponUpgradeReloadMultiple)); break;
						case SID_VAL("weapon-upgrade-fire-rate"):			GAMEPLAY_ASSERT(sizeof(T) == sizeof(DC::WeaponUpgradeFireRate)); break;
						case SID_VAL("weapon-upgrade-net-spawn-ammo"):		GAMEPLAY_ASSERT(sizeof(T) == sizeof(DC::WeaponUpgradeNetSpawnAmmo)); break;
						case SID_VAL("weapon-upgrade-headshot"):			GAMEPLAY_ASSERT(sizeof(T) == sizeof(DC::WeaponUpgradeHeadshot)); break;
						case SID_VAL("weapon-upgrade-accuracy"):			GAMEPLAY_ASSERT(sizeof(T) == sizeof(DC::WeaponUpgradeAccuracy)); break;
						case SID_VAL("weapon-upgrade-recoil"):				GAMEPLAY_ASSERT(sizeof(T) == sizeof(DC::WeaponUpgradeRecoil)); break;
						case SID_VAL("weapon-upgrade-stability"):			GAMEPLAY_ASSERT(sizeof(T) == sizeof(DC::WeaponUpgradeStability)); break;
						//case SID_VAL("weapon-upgrade-blindfire"):			GAMEPLAY_ASSERT(sizeof(T) == sizeof(DC::WeaponUpgradeBlindfire)); break;
						case SID_VAL("weapon-upgrade-scope"):				GAMEPLAY_ASSERT(sizeof(T) == sizeof(DC::WeaponUpgradeScope)); break;
						case SID_VAL("weapon-upgrade-falloff"):				GAMEPLAY_ASSERT(sizeof(T) == sizeof(DC::WeaponUpgradeFalloff)); break;
						case SID_VAL("weapon-upgrade-silencer"):			GAMEPLAY_ASSERT(sizeof(T) == sizeof(DC::WeaponUpgradeSilencer)); break;
						case SID_VAL("weapon-upgrade-multi-fire"):			GAMEPLAY_ASSERT(sizeof(T) == sizeof(DC::WeaponUpgradeMultiFire)); break;
						case SID_VAL("weapon-upgrade-burst-fire"):			GAMEPLAY_ASSERT(sizeof(T) == sizeof(DC::WeaponUpgradeBurstFire)); break;
						case SID_VAL("weapon-upgrade-charge-curve"):		GAMEPLAY_ASSERT(sizeof(T) == sizeof(DC::WeaponUpgradeChargeCurve)); break;
						case SID_VAL("weapon-upgrade-sway"):				GAMEPLAY_ASSERT(sizeof(T) == sizeof(DC::WeaponUpgradeSway)); break;
						}
					}
					return static_cast<const T*>(pSet->m_levels[level - 1].m_blindData);
				}
			}
		}
		return nullptr;
	}

	F32 GetUpgradeFloatValue(StringId64 weaponDefId, DC::WeaponUpgradeType upgType, F32 defVal) const;
	I32 GetUpgradeIntValue(StringId64 weaponDefId, DC::WeaponUpgradeType upgType, I32 defVal) const;
	bool GetUpgradeBoolValue(StringId64 weaponDefId, DC::WeaponUpgradeType upgType, bool defVal) const;

	static StringId64 WeaponUpgradeTypeToStringId(DC::WeaponUpgradeType type);
	static DC::WeaponUpgradeType StringIdToWeaponUpgradeType(StringId64 weaponUpgradeTypeName);

private:
	I8 m_levels[DC::kWeaponUpgradeTypeNumUpgradeTypes];
};

/// --------------------------------------------------------------------------------------------------------------- ///
/// WeaponUpgrades
/// --------------------------------------------------------------------------------------------------------------- ///
class WeaponUpgrades : public WeaponUpgradesInfo
{
	typedef WeaponUpgradesInfo ParentClass;

public:

	typedef U64 UpgradeBits;

	WeaponUpgrades()
		: ParentClass()
		, m_weaponDefId(INVALID_STRING_ID_64)
	{
		Reset();
	}

	void Reset()
	{
		ParentClass::Reset();
		m_lastAppliedUpgradeBundle = INVALID_STRING_ID_64;
	}

	void Init(StringId64 weaponDefId)
	{
		m_weaponDefId = weaponDefId;
	}

	void Init(StringId64 weaponDefId, const WeaponUpgradesInfo& info)
	{
		m_weaponDefId = weaponDefId;
		ParentClass::CopyFrom(info);
	}

	void AddUpgradeBundle(StringId64 bundleId);
	void RmvUpgradeBundle(StringId64 bundleId);
	bool IsBundleUpgraded(StringId64 bundleId, const UpgradeBits& upgradeBits) const;
	void GetUpgradeBundles(ListArray<StringId64>& outBundles, const UpgradeBits& upgradeBits) const;
	static void GetUpgradeBundles(StringId64 weaponId, const UpgradeBits& upgradeBits, ListArray<StringId64>& outBundles);

	struct JointMod
	{
		StringId64 jointId;
		bool m_on;
	};
	void GetJointModifications(ListArray<JointMod>& outJoints, const UpgradeBits& upgradeBits, const ListArray<StringId64>* pSuppressedUpgrades = nullptr) const;
	static void AddJointMods(ListArray<JointMod>& inoutMods, const JointMod& newMod);

	struct ShaderParams
	{
		StringId64 paramId;
		float value;
	};
	void GetShaderParams(ListArray<ShaderParams>& outShaderParams, const UpgradeBits& upgradeBits, const ListArray<StringId64>* pSuppressedUpgrades = nullptr) const;

	void DebugShow(const UpgradeBits& upgradeBits) const;

	StringId64 GetLastAppliedUpgradeBundle() const { return m_lastAppliedUpgradeBundle; }

	void Read(BitStream* bitStream)
	{
		// starts at 1 because none is invalid
		for (int i = 1; i < DC::kWeaponUpgradeTypeNumUpgradeTypes; i++)
		{
			SetLevel(i, bitStream->ReadBits(2));
		}
		bitStream->Read(m_lastAppliedUpgradeBundle);
	}

	void Write(BitStream* bitStream) const
	{
		// starts at 1 because none is invalid
		for (int i = 1; i < DC::kWeaponUpgradeTypeNumUpgradeTypes; i++)
		{
			bitStream->WriteBits(GetLevel(i), 2);
		}
		bitStream->Write(m_lastAppliedUpgradeBundle);
	}

	void CopyFrom(const WeaponUpgrades& info)
	{
		ParentClass::CopyFrom(info);

		m_lastAppliedUpgradeBundle = info.GetLastAppliedUpgradeBundle();
	}

public:
	static const DC::Map* GetWeaponUpgradeBundlesMap();
	static const DC::WeaponUpgradeBundle* LookupUpgradeBundle(StringId64 weaponId, StringId64 bundleId, bool disallowNull = false);
	static void GetRawUpgradeBundles(StringId64 weaponId, ListArray<StringId64>& outBundles);
	static const DC::WeaponVisualMods* GetDefaultVisualMods(StringId64 weaponId);
	static void GetDefaultVisualMods(StringId64 weaponId, ListArray<JointMod>& jointMods, ListArray<ShaderParams>& outShaderParams);

	static UpgradeBits ToUpgradeBit(StringId64 weaponId, StringId64 bundleId);
	static void UpgradeBitsToBundles(StringId64 weaponId, UpgradeBits upgradeBits, ListArray<StringId64>& outBundles);

private:

	static I32 FindBundleIndex(StringId64 bundleId, const ListArray<StringId64>& rawBundles);

	StringId64 m_lastAppliedUpgradeBundle;
	StringId64 m_weaponDefId;

public:

	static UpgradeBits GetFullUpgradeBits(StringId64 weaponId);

};
