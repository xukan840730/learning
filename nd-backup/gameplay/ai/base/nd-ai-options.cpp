/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/ai/base/nd-ai-options.h"

#include "corelib/util/string-cache.h"

#include "ndlib/debug/nd-dmenu.h"

#include "gamelib/gameplay/ai/base/nd-ai-debug.h"

/// --------------------------------------------------------------------------------------------------------------- ///
NavOptions g_navOptions;
NavCharOptions g_navCharOptions;
NdAiOptions g_ndAiOptions;

U32 g_defaultLogChannels = (1U << kNpcLogChannelGeneral) | (1U << kNpcLogChannelNav) | (1U << kNpcLogChannelNavDetails)
						   | (1U << kNpcLogChannelAnim) | (1U << kNpcLogChannelAnimDetails)
						   | (1U << kNpcLogChannelSkill) | (1U << kNpcLogChannelSkillDetails)
						   | (1U << kNpcLogChannelBehavior);

/// --------------------------------------------------------------------------------------------------------------- ///
static DMENU::ItemEnumPair s_apAnimDebugModes[] =
{
	DMENU::ItemEnumPair("None", NavCharOptions::kApAnimDebugModeNone),
	DMENU::ItemEnumPair("Entry Anim (Chosen)", NavCharOptions::kApAnimDebugModeEnterAnimsChosen),
	DMENU::ItemEnumPair("Entry Anims (Valid Only)", NavCharOptions::kApAnimDebugModeEnterAnimsValid),
	DMENU::ItemEnumPair("Entry Anims (All)", NavCharOptions::kApAnimDebugModeEnterAnimsAll),
	DMENU::ItemEnumPair("Exit Anims (Chosen)", NavCharOptions::kApAnimDebugModeExitAnimsChosen),
	DMENU::ItemEnumPair("Exit Anims (Valid Only)", NavCharOptions::kApAnimDebugModeExitAnimsValid),
	DMENU::ItemEnumPair("Exit Anims (All)", NavCharOptions::kApAnimDebugModeExitAnimsAll),
	DMENU::ItemEnumPair()
};

/// --------------------------------------------------------------------------------------------------------------- ///
static DMENU::ItemEnumPair s_apMotionTypeFilters[] =
{
	DMENU::ItemEnumPair("None", kMotionTypeMax),
	DMENU::ItemEnumPair("Current", -1),
	DMENU::ItemEnumPair("Walk", kMotionTypeWalk),
	DMENU::ItemEnumPair("Run", kMotionTypeRun),
	DMENU::ItemEnumPair("Sprint", kMotionTypeSprint),
	DMENU::ItemEnumPair()
};

/// --------------------------------------------------------------------------------------------------------------- ///
static I64 OnEditKnownMtSubcategory(DMENU::Item& item, DMENU::Message message, I64 desiredValue, I64 oldValue)
{
	if (message != DMENU::kExecute)
		return oldValue;

	return desiredValue;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CreateApAnimDebugMenu(DMENU::Menu* pMenu, const char* apTypeStr, NavCharOptions::ApControllerOptions& options)
{
	STRIP_IN_FINAL_BUILD;

	pMenu->PushBackItem(NDI_NEW DMENU::ItemEnum(StringBuilder<256>("%s Debug Mode", apTypeStr).c_str(),
												s_apAnimDebugModes,
												DMENU::EditInt,
												&options.m_debugMode));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool(StringBuilder<256>("%s Verbose Anim Debug", apTypeStr).c_str(),
												&options.m_verboseAnimDebug));

	pMenu->PushBackItem(NDI_NEW DMENU::ItemEnum(StringBuilder<256>("%s MotionType Filter", apTypeStr).c_str(),
												s_apMotionTypeFilters,
												DMENU::EditInt,
												&options.m_motionTypeFilter));

	if (const DC::SymbolArray* pKnownMts = ScriptManager::LookupInNamespace<DC::SymbolArray>(SID("*known-mt-subcategories*"),
																							 SID("ai"),
																							 nullptr))
	{
		DMENU::ItemEnumPair* pMtSubcatEnums = NDI_NEW DMENU::ItemEnumPair[pKnownMts->m_count + 2];

		pMtSubcatEnums[0] = DMENU::ItemEnumPair("None", INVALID_STRING_ID_64.GetValue());

		for (U32F i = 0; i < pKnownMts->m_count; ++i)
		{
			pMtSubcatEnums[i + 1] = DMENU::ItemEnumPair(DevKitOnly_StringIdToString(pKnownMts->m_array[i]),
														pKnownMts->m_array[i].GetValue());
		}

		pMtSubcatEnums[pKnownMts->m_count + 1] = DMENU::ItemEnumPair();

		pMenu->PushBackItem(NDI_NEW DMENU::ItemEnum(StringBuilder<256>("%s Mt Subcategory Filter", apTypeStr).c_str(),
													pMtSubcatEnums,
													DMENU::EditInt64,
													&options.m_mtSubcategoryFilter));
	}

	pMenu->PushBackItem(NDI_NEW DMENU::ItemBool(StringBuilder<256>("%s Force Default", apTypeStr).c_str(),
												&options.m_forceDefault));
}
