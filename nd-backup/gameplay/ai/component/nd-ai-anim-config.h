/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/script/script-manager.h"

#include "gamelib/scriptx/h/nd-ai-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class TextPrinter;

/// --------------------------------------------------------------------------------------------------------------- ///
class NdAiAnimationConfig
{
public:
	NdAiAnimationConfig();

	void UpdateActiveConfig(StringId64 matrixId, StringId64 classId, U32 weaponIndex);
	void DebugPrint(TextPrinter* pPrinter) const;

	const char* GetOverlayBaseName() const { return Get().m_overlayBaseName; }
	float GetHitReactionTimeoutSec() const { return Get().m_hitReactionTimeout; }
	float GetMovementSpeedScale() const { return Get().m_movementSpeedScale; }

	StringId64 GetDodgeReactionSetId() const	{ return Get().m_dodgeReactionSet; }
	StringId64 GetDiveReactionSetId() const	    { return Get().m_diveReactionSet; }
	StringId64 GetMovingDodgesSetId() const		{ return Get().m_movingDodgesSet; }
	StringId64 GetHitReactionSetId() const		{ return Get().m_hitReactionSet; }
	StringId64 GetDeathReactionSetId() const	{ return Get().m_deathReactionSet; }
	StringId64 GetWildFireSetId() const			{ return Get().m_wildFireSet; }
	StringId64 GetTapCharacterId() const		{ return Get().m_tapSet; }
	StringId64 GetCapCharacterId() const		{ return Get().m_capSet; }
	StringId64 GetCoverEntrySetId() const		{ return Get().m_coverEntrySet; }
	StringId64 GetPerchEntrySetId() const		{ return Get().m_perchEntrySet; }
	StringId64 GetPerchExitSetId() const		{ return Get().m_perchExitSet; }
	StringId64 GetCoverExitSetId() const		{ return Get().m_coverExitSet; }
	StringId64 GetCoverTransferSetId() const	{ return Get().m_coverTransferSet; }

	// search
	StringId64 GetSearchWaitSetId() const		{ return Get().m_searchWaitSet ; }
	StringId64 GetCornerCheckSetId() const		{ return Get().m_cornerCheckSet; }
	StringId64 GetProneCheckSetId()	const		{ return Get().m_proneCheckSet; }
	StringId64 GetGrassCheckSetId() const		{ return Get().m_grassCheckSet ; }
	StringId64 GetLedgeCheckSetId() const		{ return Get().m_ledgeCheckSet ; }

private:
	const DC::AiAnimationConfig& Get() const;

	ScriptPointer<DC::AiAnimationConfig> m_configPtr;
};
