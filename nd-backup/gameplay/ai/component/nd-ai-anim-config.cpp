/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/ai/component/nd-ai-anim-config.h"

#include "ndlib/script/script-manager.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static DC::AiAnimationConfig s_defaultConfig;

/// --------------------------------------------------------------------------------------------------------------- ///
NdAiAnimationConfig::NdAiAnimationConfig()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
const DC::AiAnimationConfig& NdAiAnimationConfig::Get() const
{
	const DC::AiAnimationConfig* pConfig = m_configPtr;

	if (pConfig)
	{
		return *pConfig;
	}

	return s_defaultConfig;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAiAnimationConfig::UpdateActiveConfig(StringId64 matrixId, StringId64 classId, U32 weaponIndex)
{
	const DC::Map* pClassMatrix = ScriptManager::LookupInNamespace<DC::Map>(matrixId, SID("ai"), nullptr);
	if (!pClassMatrix)
		return;

	const DC::AiAnimConfigWeaponArray* pWeaponArray = ScriptManager::MapLookup<DC::AiAnimConfigWeaponArray>(pClassMatrix,
																											classId);
	if (!pWeaponArray || !pWeaponArray->m_array)
		return;

	if (weaponIndex >= pWeaponArray->m_count)
		return;

	const StringId64 newConfigId = pWeaponArray->m_array[weaponIndex];

	if (newConfigId != m_configPtr.GetId())
	{
		m_configPtr = ScriptPointer<DC::AiAnimationConfig>(newConfigId, SID("ai"));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAiAnimationConfig::DebugPrint(TextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	if (!pPrinter)
		return;
}
