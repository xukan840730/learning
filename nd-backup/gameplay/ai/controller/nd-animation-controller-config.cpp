/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "ndlib/render/util/screen-space-text-printer.h"

#include "gamelib/gameplay/ai/controller/nd-animation-controller-config.h"

/// --------------------------------------------------------------------------------------------------------------- ///
NdAnimControllerConfig::NdAnimControllerConfig()
{
	m_cover.m_entryDefsSetId		= INVALID_STRING_ID_64;
	m_perch.m_entryDefsSetId		= INVALID_STRING_ID_64;
	m_perch.m_exitDefsSetId			= INVALID_STRING_ID_64;
	m_cover.m_exitDefsSetId			= INVALID_STRING_ID_64;
	m_cover.m_maxFacingDiffAngleDeg = 25.0f;
	m_cover.m_coverEntranceTransitionGroup = SID("enter-cover-default");
	m_cover.m_standingAimStepOutOnly	   = false;

	m_perch.m_maxFacingDiffAngleDeg = 25.0f;
	m_perch.m_perchEntranceTransitionGroup = SID("enter-perch-default");

	m_hitReaction.m_hitReactionSetId   = INVALID_STRING_ID_64;
	m_hitReaction.m_deathReactionSetId = INVALID_STRING_ID_64;

	m_scriptOverrideHitReaction.m_hitReactionSetId	 = INVALID_STRING_ID_64;
	m_scriptOverrideHitReaction.m_deathReactionSetId = INVALID_STRING_ID_64;

	m_actionPack.m_tapCharacterId = INVALID_STRING_ID_64;
	m_actionPack.m_capCharacterId = INVALID_STRING_ID_64;

	m_move.m_speedScale = 1.0f;

	m_ragdoll.m_configId = INVALID_STRING_ID_64;

	m_search.m_waitSetId	 = INVALID_STRING_ID_64;
	m_search.m_cornerCheckId = INVALID_STRING_ID_64;
	m_search.m_proneCheckId	 = INVALID_STRING_ID_64;
	m_search.m_grassCheckId	 = INVALID_STRING_ID_64;
	m_search.m_ledgeCheckId	 = INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdAnimControllerConfig::DebugDraw(ScreenSpaceTextPrinter* pPrinter) const
{
	STRIP_IN_FINAL_BUILD;

	ScreenSpaceTextPrinter& printer = *pPrinter;

	const Color titleColor = kColorYellow;
	const Color labelColor = kColorGray;
	const Color valueColor = kColorWhite;

	printer.PrintTextNoCr(labelColor, "  Corner Check");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_search.m_cornerCheckId));
	printer.PrintTextNoCr(labelColor, "  Prone Check");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_search.m_proneCheckId));
	printer.PrintTextNoCr(labelColor, "  Grass Check ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_search.m_grassCheckId));
	printer.PrintTextNoCr(labelColor, "  Ledge Check ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_search.m_ledgeCheckId));
	printer.PrintTextNoCr(labelColor, "  Wait Set ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_search.m_waitSetId));
	printer.PrintText(titleColor, "Search");

	printer.PrintTextNoCr(labelColor, "  Config ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_ragdoll.m_configId));
	printer.PrintText(titleColor, "Ragdoll");

	printer.PrintTextNoCr(labelColor, "  Speed Scale ");
	printer.PrintText(valueColor, "%0.1f", m_move.m_speedScale);
	printer.PrintText(titleColor, "Move");

	printer.PrintTextNoCr(labelColor, "  Cap Id ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_actionPack.m_capCharacterId));
	printer.PrintTextNoCr(labelColor, "  Tap Id ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_actionPack.m_tapCharacterId));
	printer.PrintText(titleColor, "Action Pack");

	printer.PrintTextNoCr(labelColor, "  Death ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_hitReaction.m_deathReactionSetId));
	printer.PrintTextNoCr(labelColor, "  Hit ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_hitReaction.m_hitReactionSetId));
	printer.PrintText(titleColor, "Hit Reaction");

	printer.PrintTextNoCr(labelColor, "  Transition Group ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_cover.m_coverEntranceTransitionGroup));
	printer.PrintTextNoCr(labelColor, "  Max Face Diff Angle ");
	printer.PrintText(valueColor, "%3.1f", m_cover.m_maxFacingDiffAngleDeg);
	printer.PrintTextNoCr(labelColor, "  Exit Def ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_cover.m_exitDefsSetId));
	printer.PrintTextNoCr(labelColor, "  Entry Def ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_cover.m_entryDefsSetId));
	printer.PrintText(titleColor, "Cover");

	printer.PrintTextNoCr(labelColor, "  Transition Group ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_perch.m_perchEntranceTransitionGroup));
	printer.PrintTextNoCr(labelColor, "  Max Face Diff Angle ");
	printer.PrintText(valueColor, "%3.1f", m_perch.m_maxFacingDiffAngleDeg);
	printer.PrintTextNoCr(labelColor, "  Entry Def ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_perch.m_entryDefsSetId));
	printer.PrintTextNoCr(labelColor, "  Exit Def ");
	printer.PrintText(valueColor, "%s", DevKitOnly_StringIdToString(m_perch.m_exitDefsSetId));
	printer.PrintText(titleColor, "Perch");
}
