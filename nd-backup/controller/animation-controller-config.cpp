/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "game/ai/controller/animation-controller-config.h"

#include "ndlib/render/util/screen-space-text-printer.h"

/// --------------------------------------------------------------------------------------------------------------- ///
AnimControllerConfig::AnimControllerConfig()
{
	m_gameCover.m_transferSetId				= INVALID_STRING_ID_64;
	m_gameCover.m_coverIdleIkData			= INVALID_STRING_ID_64;

	m_gameHitReaction.m_dodgeReactionSetId = INVALID_STRING_ID_64;

	for (U32F ii = 0; ii < HitReactionConfig::kMaxSetIds; ++ii)
	{
		m_gameHitReaction.m_setIds[ii] = INVALID_STRING_ID_64;
	}

	m_combat.m_wildFireSetId = INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimControllerConfig::DebugDraw(ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	ParentClass::DebugDraw(pPrinter);

	ScreenSpaceTextPrinter& printer = *pPrinter;

	const Color titleColor = kColorYellow;
	const Color labelColor = kColorGray;
	const Color valueColor = kColorWhite;

	printer.PrintTextNoCr(labelColor, "  Wild Fire ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_combat.m_wildFireSetId));
	printer.PrintText(titleColor, "Combat");

	for (U32F ii = 0; ii < HitReactionConfig::kMaxSetIds; ++ii)
	{
		printer.PrintTextNoCr(labelColor, "  Set[%u] ", ii);
		printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_gameHitReaction.m_setIds[ii]));
	}

	printer.PrintTextNoCr(labelColor, "  Dodge ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_gameHitReaction.m_dodgeReactionSetId));
	printer.PrintText(titleColor, "Hit Reaction");

	printer.PrintTextNoCr(labelColor, "  Idle Ik Data ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_gameCover.m_coverIdleIkData));
	printer.PrintTextNoCr(labelColor, "  Transfer ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_gameCover.m_transferSetId));
	printer.PrintText(titleColor, "Cover");
}
