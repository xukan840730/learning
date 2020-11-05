/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/list-array.h"

#include "ndlib/process/process.h"
#include "ndlib/render/look.h"

#include "gamelib/scriptx/h/nd-prop-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class NdAttachableObject;
class NdGameObject;
class ProcessSpawnInfo;
struct NdAttachableInfo;
class SpawnInfo;

namespace DC
{
	struct Map;
}

/// --------------------------------------------------------------------------------------------------------------- ///
FWD_DECL_PROCESS_HANDLE(NdAttachableObject);

/// --------------------------------------------------------------------------------------------------------------- ///
CREATE_IDENTIFIER_TYPE(PropInventoryId, U32);
#define INVALID_PROPINV_ID PropInventoryId(0)

/// --------------------------------------------------------------------------------------------------------------- ///
class NdPropControl
{
public:
	NdPropControl();
	virtual ~NdPropControl();

	void RequestPropsFromLook(const ResolvedLook& resolvedLook, NdGameObject* pOwner);
	bool Init(NdGameObject* pOwner, const ProcessSpawnInfo& spawnInfo, U32F extraCapacity, 
				U8 additionalPropCount, StringId64* additionalProps);
	bool Init(NdGameObject* pOwner, const ProcessSpawnInfo& spawnInfo, U32F extraCapacity);
	void SpawnProps(); // typically called immediately after Init() -- should probably roll this into Init()
	void Destroy();

	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);

	I32F GetPropByType(const DC::NdPropType type) const;
	I32F FindPropIndexByName(const StringId64 nameId) const;

	U32F GetNumProps() const { return m_storedProps.size(); }
	U32F GetMaxProps() const { return m_storedProps.capacity(); }

	NdAttachableObject* GetPropObject(I32F index) const;
	MutableNdAttachableObjectHandle GetPropHandle(I32F index) const;

	NdAttachableObject* GetPropObjectByNameId(StringId64 nameId) const;
	MutableNdAttachableObjectHandle GetPropHandleByNameId(StringId64 nameId) const;

	DC::NdPropType GetPropType(I32F index) const;

	const DC::NdProp* GetPropDefinition(I32F index) const;

	NdAttachableObject* SpawnPropFromDefinition(const DC::NdProp* pProp, DC::PropFlag extraFlags = 0);

	void SpawnProp(StringId64 meshId);
	void DestroyProp(StringId64 meshId);
	void HideProp(StringId64 meshId);
	void RevealProp(StringId64 meshId);

	NdAttachableObject* SpawnPropByNameId(StringId64 nameId, DC::PropFlag extraFlags = 0);
	void DestroyPropByNameId(StringId64 nameId);
	void HidePropByNameId(StringId64 nameId);
	void RevealPropByNameId(StringId64 nameId);

	StringId64 GetMeshIdByType(const DC::NdPropType type) const;
	void SpawnPropByType(const DC::NdPropType type);
	void DestroyPropByType(const DC::NdPropType type);
	void HidePropByType(const DC::NdPropType type);
	void RevealPropByType(const DC::NdPropType type);

	PropInventoryId GiveProp(NdAttachableObject* pProp, const DC::NdPropType type);
	PropInventoryId GiveProp(NdAttachableObject* pProp, const DC::NdProp* pDefinition);
	void ForgetProp(I32F index); // stops tracking the prop process (usually this is done if the prop has been manually detached)
	void ForgetProp(const NdAttachableObject* pProp);

	StringId64 GetPropNameId(int index) const;
	PropInventoryId GetPropUid(I32F index) const;
	I32F GetPropIndexByUid(PropInventoryId propUid) const;
	I32F GetWeaponIndexByWeaponId(const StringId64 weaponId) const;

	bool IsFull() const { return m_storedProps.IsFull(); }

	void AddPropToSpawnList(const DC::NdProp* pProp);

	virtual StringId64 OverrideArtGroup(StringId64 artGroupId) const { return artGroupId; }

	void RebootCloths();
	void MultipleRebootCloths(I32 numFrames);

	void UpdateTransforms(const Locator& oldLoc, const Locator& newLoc);

protected:
	struct StoredProp
	{
		StoredProp();

		const NdAttachableObject* ToProcess() const { return m_hProcess.ToProcess(); }
		NdAttachableObject* ToMutableProcess() { return m_hProcess.ToMutableProcess(); }

		PropInventoryId m_uid;
		MutableNdAttachableObjectHandle m_hProcess;
		DC::NdPropType m_type;
		const DC::NdProp* m_pDefinition;
	};

	typedef ListArray<StoredProp> StoredPropArray;

	NdAttachableObject* SpawnProp(StoredProp& prop, DC::PropFlag extraFlags = 0);
	void DestroyProp(StoredProp& prop);
	void HideProp(StoredProp& prop);
	void RevealProp(StoredProp& prop);

	const NdGameObject* Self() const
	{
		const NdGameObject* pSelf = m_hOwner.ToProcess();
		GAMEPLAY_ASSERT(pSelf);
		return pSelf;
	}

	NdGameObject* Self()
	{
		NdGameObject* pSelf = m_hOwner.ToMutableProcess();
		GAMEPLAY_ASSERT(pSelf);
		return pSelf;
	}

	static void GenerateUniqueId(const DC::NdProp* pProp, const Process* pOwner, NdAttachableInfo& attachInfo);
	void BuildPropSpawnList(const DC::Map* pPropMap, StringId64* aResolvedPropId, int numProps);
	virtual StringId64 GetGameplayPropSetId(const SpawnInfo& spawn) const;
	virtual bool DontRagdollIgnore(DC::NdPropType propType) const;

	virtual NdAttachableObject* SpawnPropInternal(const DC::NdProp* pProp, const NdAttachableInfo& attachInfo, bool useSubAttachBucket = false);

	U32 m_numPropOrSetIds : 31;
	U32 m_ghostingID : 1;
	StringId64 m_aPropOrSetId[ResolvedLook::kMaxPropOrSetIds + 1];
	U32 m_capacity;
	StoredPropArray m_storedProps;
	MutableNdGameObjectHandle m_hOwner;
};
