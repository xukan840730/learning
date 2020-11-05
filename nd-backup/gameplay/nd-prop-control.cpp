/*
 * Copyright (c) 2008 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nd-prop-control.h"

#include "corelib/containers/static-map.h"

#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/attach-system.h"
#include "ndlib/process/event.h"
#include "ndlib/process/spawn-info.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/draw-control.h"
#include "ndlib/render/interface/fg-instance.h"
#include "ndlib/render/look.h"
#include "ndlib/script/script-manager.h"

#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/nd-attachable-object.h"
#include "gamelib/gameplay/nd-effect-control.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/process-char-cloth.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/entitydb.h"
#include "gamelib/state-script/ss-animate.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static void MsgProp(const Process* pProc, const char* pStr, ...)
{
	va_list vargs;
	va_start(vargs, pStr);

	char buf[1024];
	vsnprintf(buf, 1024, pStr, vargs);
	va_end(vargs);

	TimeFrame curTime = pProc->GetCurTime();
	MsgUser5("Npc(pid-%d) (t=%-6.2f) %-20s ", U32(pProc->GetProcessId()), ToSeconds(curTime), pProc->GetName());
	MsgUser5(buf);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const DC::NdProp* LookupGlobalProp(StringId64 propId)
{
	const DC::Map* pPropMap = ScriptManager::Lookup<DC::Map>(SID("*global-props*"), nullptr);
	if (pPropMap)
	{
		const DC::PropPtr* pPropPtr = ScriptManager::MapLookup<const DC::PropPtr>(pPropMap, propId);
		if (pPropPtr)
		{
			return pPropPtr->m_prop;
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdPropControl::NdPropControl() : m_numPropOrSetIds(0), m_ghostingID(0), m_capacity(0)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdPropControl::~NdPropControl()
{
	// this is just here to establish a virtual destructor
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::RequestPropsFromLook(const ResolvedLook& resolvedLook, NdGameObject* pOwner)
{
	const bool isPlayer = (pOwner && pOwner->IsKindOf(SID("Player")));

	// cache off prop information from the ResolvedLook for later use in Init()
	m_numPropOrSetIds = 0;
	m_ghostingID = resolvedLook.m_ghostingIDSkipProps ? 0 : resolvedLook.m_ghostingID;
	for (int i = 0; i < resolvedLook.m_numPropOrSetIds; ++i)
	{
		if ((resolvedLook.m_aPropOrSetFlags[i] & ResolvedLook::kPropFlagPlayerOnly) && !isPlayer)
			continue;
		if ((resolvedLook.m_aPropOrSetFlags[i] & ResolvedLook::kPropFlagNonPlayerOnly) && isPlayer)
			continue;

		m_aPropOrSetId[m_numPropOrSetIds] = resolvedLook.m_aPropOrSetId[i];
		++m_numPropOrSetIds;
	}
	ASSERT(m_numPropOrSetIds < ARRAY_ELEMENT_COUNT(m_aPropOrSetId));
	ASSERT(m_numPropOrSetIds <= resolvedLook.m_numPropOrSetIds);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdPropControl::Init(NdGameObject* pOwner, const ProcessSpawnInfo& spawnInfo, U32F extraCapacity,
							U8 additionalPropCount, StringId64* additionalProps)
{
	ASSERT(m_numPropOrSetIds + additionalPropCount <= ResolvedLook::kMaxPropOrSetIds + 1);
	for (U8 i = 0; i < additionalPropCount; ++i)
	{
		m_aPropOrSetId[m_numPropOrSetIds] = additionalProps[i];
		++m_numPropOrSetIds;
	}

	return Init(pOwner, spawnInfo, extraCapacity);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdPropControl::Init(NdGameObject* pOwner, const ProcessSpawnInfo& spawnInfo, U32F extraCapacity)
{
	PROFILE(AI, PropInventory_Init);

	ASSERT(pOwner);
	m_hOwner = pOwner;

	const SpawnInfo& spawn = static_cast<const SpawnInfo&>(spawnInfo);

	// tack on the gameplay prop set id, if any
	ASSERT(m_numPropOrSetIds < ARRAY_ELEMENT_COUNT(m_aPropOrSetId));
	const StringId64 gameplayPropSetId = GetGameplayPropSetId(spawn);
	if (gameplayPropSetId != INVALID_STRING_ID_64 && m_numPropOrSetIds < ARRAY_ELEMENT_COUNT(m_aPropOrSetId) - 1)
	{
		m_aPropOrSetId[m_numPropOrSetIds] = gameplayPropSetId;
		++m_numPropOrSetIds;
	}
	ASSERT(m_numPropOrSetIds <= ARRAY_ELEMENT_COUNT(m_aPropOrSetId));

	const DC::Map* pPropMap = ScriptManager::Lookup<DC::Map>(SID("*global-props*"), nullptr);
	ALWAYS_ASSERT(pPropMap);

	if (pPropMap && !g_ndAiOptions.m_disableProps)
	{
		const int kMaxResolvedPropIds = 256;
		StringId64 aResolvedPropId[kMaxResolvedPropIds];

		// count props from all prop sets
		int numProps = 0;
		for (U32F iPropSet = 0; iPropSet < m_numPropOrSetIds && numProps < kMaxResolvedPropIds; ++iPropSet)
		{
			const StringId64 propOrSetId = m_aPropOrSetId[iPropSet];
			numProps += ResolvePropOrPropCollectionId(propOrSetId, &aResolvedPropId[numProps], kMaxResolvedPropIds - numProps);
		}

		// also count any props specified individually on the spawner
		if (spawn.m_pSpawner)
		{
			const EntityDB* pDb = spawn.m_pSpawner->GetEntityDB();
			if (pDb)
			{
				for (EntityDB::RecordMap::const_iterator it = pDb->GetFirst(SID("prop"));
					 it != pDb->End() && it->first == SID("prop") && numProps < kMaxResolvedPropIds;
					 ++it)
				{
					const EntityDB::Record* pRec = it->second;
					const StringId64 propId = pRec ? pRec->GetData<StringId64>(INVALID_STRING_ID_64) : INVALID_STRING_ID_64;

					const DC::PropPtr* pPropPtr = ScriptManager::MapLookup<const DC::PropPtr>(pPropMap, propId);
					if (pPropPtr && pPropPtr->m_prop)
					{
						aResolvedPropId[numProps++] = propId;
					}
				}
			}
		}

		m_capacity = numProps + extraCapacity;

		if (m_capacity > 0)
		{
			m_storedProps.Init(m_capacity, FILE_LINE_FUNC);

			BuildPropSpawnList(pPropMap, aResolvedPropId, numProps);
		}
	}
	else
	{
		m_capacity = 0;
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	m_storedProps.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdPropControl::GetGameplayPropSetId(const SpawnInfo& spawn) const
{
	if (spawn.m_pSpawner)
	{
		if (const EntityDB* pEntityDb = spawn.m_pSpawner->GetEntityDB())
		{
			const char * pStr = pEntityDb->GetData<String>(SID("prop-set"), String()).GetString();
			if (pStr)
				return StringToStringId64(pStr);
		}
	}
	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::BuildPropSpawnList(const DC::Map* pPropMap, StringId64* aResolvedPropId, int numProps)
{
	const NdGameObject* pOwner = Self();
	ALWAYS_ASSERT(pOwner);
	ALWAYS_ASSERT(pPropMap);

	// add props from all prop sets
	bool clothPropFound = false;
	for (U32F iProp = 0; iProp < numProps; ++iProp)
	{
		const StringId64 propId = aResolvedPropId[iProp];

		const DC::PropPtr* pPropPtr = ScriptManager::MapLookup<const DC::PropPtr>(pPropMap, propId);
		if (pPropPtr && pPropPtr->m_prop)
		{
			const DC::NdProp* pProp = pPropPtr->m_prop;
			AddPropToSpawnList(pProp);
			clothPropFound = clothPropFound || (pProp->m_type == DC::kNdPropTypeCloth);
		}
		else
		{
			MsgErr("Could not find a prop named '%s (0x%.8X)\n", DevKitOnly_StringIdToString(propId));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::SpawnProps()
{
	PROFILE(AI, PropInventory_SpawnProps);

	if (g_ndAiOptions.m_disableProps)
		return;

	const NdGameObject* pSelf = Self();
	const bool ignoreResetActors = (pSelf && pSelf->IgnoreResetActors());

	StoredPropArray::iterator it = m_storedProps.begin();
	while (it != m_storedProps.end())
	{
		StoredProp& prop = *it;
		if (!prop.m_pDefinition)
		{
			++it;
			continue;
		}

		if (prop.m_pDefinition->m_flags & DC::kPropFlagNoAutoSpawn)
		{
			++it;
			continue;
		}

		NdAttachableObject* pPropObject = SpawnPropFromDefinition(prop.m_pDefinition);
		if (!pPropObject)
		{
			it = m_storedProps.Erase(it);
		}
		else
		{
			pPropObject->SetIgnoreResetActors(ignoreResetActors);
			prop.m_hProcess = pPropObject;
			++it;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::Destroy()
{
	const int kMaxProps = 256;
	MutableProcessHandle propObjects[kMaxProps];
	U32 numProps = 0;
	for (StoredProp& prop : m_storedProps)
	{
		ALWAYS_ASSERT(numProps < kMaxProps);
		propObjects[numProps++] = prop.ToMutableProcess();
	}
	m_storedProps.Clear();

	for (U32 i = 0; i < numProps; ++i)
	{
		Process* pPropObject = propObjects[i].ToMutableProcess();
		if (pPropObject)
			KillProcess(pPropObject);
	}
	m_numPropOrSetIds = 0;
	m_ghostingID = 0;
	m_hOwner = MutableNdGameObjectHandle();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::SpawnProp(StringId64 meshId)
{
	for (StoredProp& prop : m_storedProps)
	{
		if (prop.m_pDefinition && StringToStringId64(prop.m_pDefinition->m_artGroup.GetString()) == meshId)
		{
			SpawnProp(prop);
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::DestroyProp(StringId64 meshId)
{
	for (StoredProp& prop : m_storedProps)
	{
		if (prop.m_pDefinition && StringToStringId64(prop.m_pDefinition->m_artGroup.GetString()) == meshId)
		{
			DestroyProp(prop);
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::HideProp(StringId64 meshId)
{
	for (StoredProp& prop : m_storedProps)
	{
		if (prop.m_pDefinition && StringToStringId64(prop.m_pDefinition->m_artGroup.GetString()) == meshId)
		{
			HideProp(prop);
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::RevealProp(StringId64 meshId)
{
	for (StoredProp& prop : m_storedProps)
	{
		if (prop.m_pDefinition && StringToStringId64(prop.m_pDefinition->m_artGroup.GetString()) == meshId)
		{
			RevealProp(prop);
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdAttachableObject* NdPropControl::SpawnProp(StoredProp& prop, DC::PropFlag extraFlags)
{
	if (!prop.m_hProcess.HandleValid() && prop.m_pDefinition)
	{
		NdAttachableObject* pPropObject = SpawnPropFromDefinition(prop.m_pDefinition, extraFlags);
		if (pPropObject)
		{
			prop.m_hProcess = pPropObject;
			return pPropObject;
		}
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::DestroyProp(StoredProp& prop)
{
	NdAttachableObject* pPropObject = prop.m_hProcess.ToMutableProcess();
	const DC::NdProp* pPropDef = prop.m_pDefinition;

	m_storedProps.Erase(&prop);

	if (pPropObject)
	{
		NdGameObject* pSelf = Self();
		IEffectControl* pEffectControl = pSelf ? pSelf->GetEffectControl() : nullptr;
		if (pEffectControl && pPropObject)
		{
			pEffectControl->OnPropRemoved(pPropObject, pPropDef);
		}

		KillProcess(pPropObject);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::HideProp(StoredProp& prop)
{
	const NdAttachableObject* pPropObject = prop.m_hProcess.ToProcess();
	if (pPropObject)
	{
		NdGameObject* pSelf = Self();
		IEffectControl* pEffectControl = pSelf ? pSelf->GetEffectControl() : nullptr;
		if (pEffectControl && pPropObject)
		{
			pEffectControl->OnPropRemoved(pPropObject, prop.m_pDefinition);
		}

		pPropObject->GetDrawControl()->HideObject();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::RevealProp(StoredProp& prop)
{
	const NdAttachableObject* pPropObject = prop.m_hProcess.ToProcess();
	if (pPropObject)
	{
		NdGameObject* pSelf = Self();
		IEffectControl* pEffectControl = pSelf ? pSelf->GetEffectControl() : nullptr;
		if (pEffectControl && pPropObject)
		{
			pEffectControl->OnPropAdded(pPropObject, prop.m_pDefinition);
		}

		pPropObject->GetDrawControl()->ShowObject();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdAttachableObject* NdPropControl::SpawnPropInternal(const DC::NdProp* pProp, const NdAttachableInfo& attachInfo, bool useSubAttachBucket /* = false*/)
{
	ALWAYS_ASSERT(pProp);

	NdGameObject* pSelf = Self();
	ResolvedLookSeed lookSeed(pSelf->GetLookId(),
							  pSelf->GetUserIdUsedForLook(),
							  pSelf->GetLookCollectionRandomizer(),
							  pSelf->GetLookTintRandomizer());
	NdAttachableObject* pPropObject = nullptr;

	Process* pParent = useSubAttachBucket ? EngineComponents::GetProcessMgr()->m_pSubAttachTree : nullptr;

	switch (pProp->m_type)
	{
	case DC::kNdPropTypeCloth:
		{
			CharClothInfo clothInfo;

			memcpy(&clothInfo, &attachInfo, sizeof(attachInfo));
			clothInfo.m_clothCollider					= pProp->m_clothCollider;
			clothInfo.m_jointSuffix						= pProp->m_jointSuffix;
			clothInfo.m_externalWindMul					= pProp->m_externalWindMul;
			clothInfo.m_baseMovementJoint				= pProp->m_baseMovementJoint;
			clothInfo.m_enableSkinning					= pProp->m_enableSkinning;
			clothInfo.m_enableSkinMoveDist				= pProp->m_enableSkinMoveDist;
			clothInfo.m_disableBendingForceDist			= pProp->m_disableBendingForceDist;
			clothInfo.m_disableSimulationMinDist		= pProp->m_disableSimulationMinDist;
			clothInfo.m_disableSimulationMaxDist		= pProp->m_disableSimulationMaxDist;
			clothInfo.m_orientedParticles				= pProp->m_orientedParticles;

			pPropObject = CreateAttachable(clothInfo, SID("ProcessCharCloth"), &lookSeed, pProp->m_tints, pParent);
		}
		break;

	//case DC::kNdPropTypeBackpack:
	//	{
	//		pPropObject = CreateAttachable(attachInfo, SID("ProcessBackPack"), &lookSeed, pProp->m_tints);
	//	}
	//	break;
	case DC::kNdPropTypeFlashlight:
		{
			pPropObject = CreateAttachable(attachInfo, SID("PropFlashlight"), nullptr, pProp->m_tints, pParent);
		}
		break;
	case DC::kNdPropTypeHorseReins:
		pPropObject = CreateAttachable(attachInfo, SID("HorseReinsRope"), &lookSeed, pProp->m_tints, pParent);
		break;
	case DC::kNdPropTypeGeneric:
	default:
		pPropObject = CreateAttachable(attachInfo, &lookSeed, pProp->m_tints, pParent);
		if (pPropObject && (pProp->m_anim != INVALID_STRING_ID_64))
		{
			SsAnimateParams playParams;
			playParams.m_nameId = pProp->m_anim;
			SendEventFrom(m_hOwner.ToMutableProcess(),
						  SID("play-animation"),
						  pPropObject,
						  BoxedSsAnimateParams(&playParams));
		}
		break;
	}

	if (pPropObject)
		pPropObject->SetUniformScale(pProp->m_scale);

	IEffectControl* pEffectControl = pSelf ? pSelf->GetEffectControl() : nullptr;
	if (pEffectControl && pPropObject)
	{
		pEffectControl->OnPropAdded(pPropObject, pProp);
	}

	if (pPropObject && pProp->m_needsShaderInstanceParams)
	{
		pPropObject->AllocateShaderInstanceParams();
	}

	return pPropObject;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F NdPropControl::GetPropByType(const DC::NdPropType type) const
{
	I32F index = 0;
	for (const StoredProp& prop : m_storedProps)
	{
		if (prop.m_type == type)
			return index;
		++index;
	}
	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdPropControl::GetPropNameId(int index) const
{
	if (index >= 0 && index < m_storedProps.Size())
	{
		const StoredProp& prop = m_storedProps[index];

		if (prop.m_pDefinition)
		{
			return StringToStringId64(prop.m_pDefinition->m_name);
		}

		if (NdAttachableObject* pProp = prop.m_hProcess.ToMutableProcess())
		{
			StringId64 weaponId = SendEvent(SID("get-weapon-id"), pProp).GetStringId();

			return weaponId;
		}

	}
	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F NdPropControl::FindPropIndexByName(const StringId64 nameId) const
{
	I32F index = 0;

	for (const StoredProp& prop : m_storedProps)
	{
		if (prop.m_pDefinition && prop.m_pDefinition->m_name)
		{
			const StringId64 propNameId = StringToStringId64(prop.m_pDefinition->m_name);

			if (propNameId == nameId)
			{
				return index;
			}
		}

		if (const NdAttachableObject* pProp = prop.m_hProcess.ToProcess())
		{
			if (pProp->GetLookId() == nameId)
			{
				return index;
			}
		}

		++index;
	}
	return -1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdAttachableObject* NdPropControl::GetPropObject(I32F index) const
{
	if (index >= 0 && index < m_storedProps.Size())
	{
		const StoredProp& prop = m_storedProps[index];
		return prop.m_hProcess.ToMutableProcess();
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MutableNdAttachableObjectHandle NdPropControl::GetPropHandle(I32F index) const
{
	if (index >= 0 && index < m_storedProps.Size())
	{
		const StoredProp& prop = m_storedProps[index];
		return prop.m_hProcess;
	}
	return MutableNdAttachableObjectHandle();
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdAttachableObject* NdPropControl::GetPropObjectByNameId(StringId64 nameId) const
{
	I32F index = FindPropIndexByName(nameId);
	if (index >= 0 && index < m_storedProps.Size())
	{
		const StoredProp& prop = m_storedProps[index];
		return prop.m_hProcess.ToMutableProcess();
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
MutableNdAttachableObjectHandle NdPropControl::GetPropHandleByNameId(StringId64 nameId) const
{
	I32F index = FindPropIndexByName(nameId);
	if (index >= 0 && index < m_storedProps.Size())
	{
		const StoredProp& prop = m_storedProps[index];
		return prop.m_hProcess;
	}
	return MutableNdAttachableObjectHandle();
}

/// --------------------------------------------------------------------------------------------------------------- ///
DC::NdPropType NdPropControl::GetPropType(I32F index) const
{
	if (index >= 0 && index < m_storedProps.Size())
	{
		const StoredProp& prop = m_storedProps[index];
		return prop.m_type;
	}
	return DC::kNdPropTypeGeneric;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::NdProp* NdPropControl::GetPropDefinition(I32F index) const
{
	if (index >= 0 && index < m_storedProps.Size())
	{
		const StoredProp& prop = m_storedProps[index];
		return prop.m_pDefinition;
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
PropInventoryId NdPropControl::GiveProp(NdAttachableObject* pProp, const DC::NdPropType type)
{
	ALWAYS_ASSERTF(!m_storedProps.IsFull(), ("Gave prop '%s' with full inventory", pProp->GetName()));
	if (m_storedProps.IsFull())
		return INVALID_PROPINV_ID;

	StoredProp prop;
	prop.m_type = type;
	prop.m_hProcess = pProp;
	m_storedProps.PushBack(prop);

	NdGameObject* pSelf = Self();
	IEffectControl* pEffectControl = pSelf ? pSelf->GetEffectControl() : nullptr;
	if (pEffectControl && pProp)
	{
		pEffectControl->OnPropAdded(pProp, prop.m_pDefinition);
	}

	return prop.m_uid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
PropInventoryId NdPropControl::GiveProp(NdAttachableObject* pProp, const DC::NdProp* pDefinition)
{
	ALWAYS_ASSERTF(!m_storedProps.IsFull(), ("Gave prop '%s' with full inventory", pProp->GetName()));
	if (m_storedProps.IsFull())
		return INVALID_PROPINV_ID;

	StoredProp prop;
	prop.m_pDefinition = pDefinition;
	prop.m_type = pDefinition->m_type;
	prop.m_hProcess = pProp;
	m_storedProps.PushBack(prop);

	NdGameObject* pSelf = Self();
	IEffectControl* pEffectControl = pSelf ? pSelf->GetEffectControl() : nullptr;
	if (pEffectControl && pProp)
	{
		pEffectControl->OnPropAdded(pProp, prop.m_pDefinition);
	}

	return prop.m_uid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::ForgetProp(I32F index)
{
	if (index >= 0 && index < m_storedProps.Size())
	{
		StoredProp& prop = m_storedProps[index];

		NdGameObject* pSelf = Self();
		IEffectControl* pEffectControl = pSelf ? pSelf->GetEffectControl() : nullptr;
		if (pEffectControl && prop.ToProcess())
		{
			pEffectControl->OnPropRemoved(prop.ToProcess(), prop.m_pDefinition);
		}

		m_storedProps.Erase(&m_storedProps[index]);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::ForgetProp(const NdAttachableObject* pProp)
{
	for (StoredPropArray::iterator it = m_storedProps.begin(); it != m_storedProps.end(); ++it)
	{
		StoredProp& prop = *it;
		if (prop.ToProcess() == pProp)
		{
			NdGameObject* pSelf = Self();
			IEffectControl* pEffectControl = pSelf ? pSelf->GetEffectControl() : nullptr;
			if (pEffectControl && pProp)
			{
				pEffectControl->OnPropRemoved(pProp, prop.m_pDefinition);
			}

			m_storedProps.Erase(it);
			break;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdPropControl::DontRagdollIgnore(DC::NdPropType propType) const
{
	switch (propType)
	{
	case DC::kNdPropTypeCloth:
		return true;
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::RebootCloths()
{
	for (StoredProp& prop : m_storedProps)
	{
		if (prop.m_type == DC::kNdPropTypeCloth)
		{
			Process* pPropObject = prop.ToMutableProcess();
			SendEvent(SID("reboot-cloth"), pPropObject);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::MultipleRebootCloths(I32 numFrames)
{
	for (StoredProp& prop : m_storedProps)
	{
		if (prop.m_type == DC::kNdPropTypeCloth)
		{
			Process* pPropObject = prop.ToMutableProcess();
			SendEvent(SID("multiple-reboot-cloth"), pPropObject, numFrames);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::AddPropToSpawnList(const DC::NdProp* pProp)
{
	ALWAYS_ASSERT(pProp);
	StoredProp prop;
	prop.m_pDefinition = pProp;
	prop.m_type = pProp->m_type;
	if (!m_storedProps.IsFull())
	{
		m_storedProps.push_back(prop);
	}
	else
	{
		MsgErr("Too many props for this configuration. Rejecting prop.\n");
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::GenerateUniqueId(const DC::NdProp* pProp, const Process* pOwner, NdAttachableInfo& attachInfo)
{
	if (pProp && !pProp->m_name.IsEmpty())
	{
		static I32 s_globalPropIndex = 1;

		char userName[64];
		snprintf(userName, sizeof(userName), "%s-%d", pProp->m_name, s_globalPropIndex++);
		attachInfo.m_userBareId = StringToStringId64(userName);
		attachInfo.m_userNamespaceId = pOwner ? pOwner->GetUserNamespaceId() : INVALID_STRING_ID_64;
		attachInfo.m_userId = StringId64Concat(attachInfo.m_userNamespaceId, userName);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::UpdateTransforms(const Locator& oldLoc, const Locator& newLoc)
{
	I64 frameNumber = GetCurrentFrameNumber();

	for (StoredProp& prop : m_storedProps)
	{
		NdAttachableObject* pPropObject = prop.ToMutableProcess();

		if (!pPropObject)
			continue;

		Locator propInLocSpace = oldLoc.UntransformLocator(pPropObject->GetLocator());
		Locator newPropInLocSpace = newLoc.TransformLocator(propInLocSpace);

		Transform newTransform = newLoc.AsTransform();

		if (IDrawControl* pDrawControl = pPropObject->GetDrawControl())
		{
			pDrawControl->OverrideInstanceTransform(newTransform, frameNumber);
		}

		if (FgAnimData* pAnimData = pPropObject->GetAnimData())
		{
			pAnimData->SetXform(newLoc);

			if (pAnimData->m_pMotionBlurXforms)
				pAnimData->m_pMotionBlurXforms->m_objectToWorld = newTransform.GetMat44();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdAttachableObject* NdPropControl::SpawnPropFromDefinition(const DC::NdProp* pProp, DC::PropFlag extraFlags)
{
	ALWAYS_ASSERT(pProp);

	DC::PropFlag flags = pProp->m_flags | extraFlags;

	NdAttachableInfo attachInfo;
	attachInfo.m_hParentProcessGo = m_hOwner;
	// give game code an opportunity to override specific art Ids, used by bonus outfits
	attachInfo.m_artGroupId       = OverrideArtGroup(StringToStringId64(pProp->m_artGroup));
	attachInfo.m_attachJoint      = pProp->m_attachJoint;
	attachInfo.m_parentAttach     = pProp->m_parentAttachName;
	attachInfo.m_attachOffset     = pProp->m_attachOffset;
	attachInfo.m_loc              = Locator(kIdentity);
	attachInfo.m_userBareId		  = INVALID_STRING_ID_64;
	attachInfo.m_userNamespaceId  = INVALID_STRING_ID_64;
	attachInfo.m_userId			  = INVALID_STRING_ID_64;
	attachInfo.m_animMap		  = pProp->m_animMap;
	attachInfo.m_jointSuffix	  = pProp->m_jointSuffix;
	attachInfo.m_customAttachPoints = pProp->m_customAttachPoints;
	attachInfo.m_isGoreHeadProp = flags & DC::kPropFlagIsGoreHeadProp;

	const NdGameObject* pOwner = m_hOwner.ToProcess();
	GenerateUniqueId(pProp, pOwner, attachInfo);

	const NdGameObject* pSelf = Self();
	if (const AttachSystem* pAttachSystem = pSelf->GetAttachSystem())
	{
		AttachIndex idx;
		if (pAttachSystem->FindPointIndexById(&idx, pProp->m_parentAttachName))
		{
			attachInfo.m_loc = pAttachSystem->GetLocator(idx);
		}
	}

	const bool useSubAttachTree = pOwner && EngineComponents::GetProcessMgr()->GetProcessBucket(*pOwner) == kProcessBucketAttach;

	NdAttachableObject* pPropObject = SpawnPropInternal(pProp, attachInfo, useSubAttachTree);

	if (!pPropObject)
	{
		MsgErr("Failed to spawn prop \"%s\"\n", DevKitOnly_StringIdToString(attachInfo.m_artGroupId));
		return nullptr;
	}

	if (FgAnimData* pAnimData = pPropObject->GetAnimData())
	{
		pAnimData->m_flags |= FgAnimData::kDisableVisOcclusion;
	}

	if (pProp->m_soundBounceHard != INVALID_STRING_ID_64 && pProp->m_soundBounceSoft != INVALID_STRING_ID_64)
	{
		pPropObject->InitSoundEffects(pProp->m_soundBounceThreshold,
									  pProp->m_soundTimeout,
									  pProp->m_soundBounceHard,
									  pProp->m_soundBounceSoft);
	}

	if (!pPropObject->GetAssociatedLevel())
	{
		pPropObject->AssociateWithLevel(m_hOwner.ToProcess()->GetAssociatedLevel());
	}

	if (flags & DC::kPropFlagSpawnHidden)
	{
		pPropObject->GetDrawControl()->HideMesh(attachInfo.m_artGroupId);
	}

	if (flags & DC::kPropFlagNoShowPlayerSpotLight)
	{
		pPropObject->GetDrawControl()->SetInstanceFlag(FgInstance::kNotShownInPlayerSpotLight);
	}

	// if the parent ignores ResetActors, so should its props
	if (pSelf->IgnoreResetActors() && pPropObject)
		pPropObject->SetIgnoreResetActors(true);

	return pPropObject;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdPropControl::GetMeshIdByType(const DC::NdPropType type) const
{
	I32F index = GetPropByType(type);
	if (index >= 0 && index < m_storedProps.Size() && m_storedProps[index].m_pDefinition)
	{
		return StringToStringId64(m_storedProps[index].m_pDefinition->m_artGroup.GetString());
	}
	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::SpawnPropByType(const DC::NdPropType type)
{
	SpawnProp(GetMeshIdByType(type));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::DestroyPropByType(const DC::NdPropType type)
{
	DestroyProp(GetMeshIdByType(type));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::HidePropByType(const DC::NdPropType type)
{
	HideProp(GetMeshIdByType(type));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::RevealPropByType(const DC::NdPropType type)
{
	RevealProp(GetMeshIdByType(type));
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdAttachableObject* NdPropControl::SpawnPropByNameId(StringId64 nameId, DC::PropFlag extraFlags)
{
	I32F index = FindPropIndexByName(nameId);
	if (index < 0)
	{
		if (!m_storedProps.IsFull())
		{
			if (const DC::NdProp* pProp = LookupGlobalProp(nameId))
			{
				StoredProp prop;
				prop.m_type = pProp->m_type;
				prop.m_pDefinition = pProp;
				m_storedProps.PushBack(prop);
				index = m_storedProps.Size() - 1;
			}
		}
	}
	if (index >= 0)
	{
		return SpawnProp(m_storedProps[index], extraFlags);
	}
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::DestroyPropByNameId(StringId64 nameId)
{
	I32F index = FindPropIndexByName(nameId);
	if (index >= 0)
	{
		DestroyProp(m_storedProps[index]);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::HidePropByNameId(StringId64 nameId)
{
	I32F index = FindPropIndexByName(nameId);
	if (index >= 0)
	{
		HideProp(m_storedProps[index]);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdPropControl::RevealPropByNameId(StringId64 nameId)
{
	I32F index = FindPropIndexByName(nameId);
	if (index >= 0)
	{
		RevealProp(m_storedProps[index]);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
PropInventoryId NdPropControl::GetPropUid(I32F index) const
{
	if (index >= 0 && index < m_storedProps.Size())
	{
		const StoredProp& prop = m_storedProps[index];
		return prop.m_uid;
	}

	return INVALID_PROPINV_ID;
}

/// --------------------------------------------------------------------------------------------------------------- ///
I32F NdPropControl::GetPropIndexByUid(PropInventoryId propUid) const
{
	I32F foundIndex = -1;
	I32F index		= 0;

	for (const StoredProp& prop : m_storedProps)
	{
		if (prop.m_uid == propUid)
		{
			foundIndex = index;
			break;
		}

		++index;
	}

	return foundIndex;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdPropControl::StoredProp::StoredProp()
{
	m_pDefinition = nullptr;
	m_hProcess	  = MutableNdAttachableObjectHandle();

	static NdAtomic32 s_propUidGenerator(1);

	m_uid = PropInventoryId(s_propUidGenerator.Add(1));

	if (s_propUidGenerator.Get() == INVALID_PROPINV_ID.GetValue())
	{
		s_propUidGenerator.Add(1);
	}
}
