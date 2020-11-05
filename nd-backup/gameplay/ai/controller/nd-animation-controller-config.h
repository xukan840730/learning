/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

/// --------------------------------------------------------------------------------------------------------------- ///
class ScreenSpaceTextPrinter;

/// --------------------------------------------------------------------------------------------------------------- ///
// NdAnimControllerConfig: Configuration params for anim controllers
/// --------------------------------------------------------------------------------------------------------------- ///
struct NdAnimControllerConfig
{
	NdAnimControllerConfig();
	virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const;

	struct CoverConfig
	{
		StringId64	m_entryDefsSetId;
		StringId64	m_exitDefsSetId;
		F32			m_maxFacingDiffAngleDeg;
		StringId64	m_coverEntranceTransitionGroup;
		bool		m_standingAimStepOutOnly;
	}
	m_cover;

	struct PerchConfig
	{
		StringId64	m_entryDefsSetId;
		StringId64	m_exitDefsSetId;
		F32			m_maxFacingDiffAngleDeg;
		StringId64	m_perchEntranceTransitionGroup;
	}
	m_perch;

	struct HitReactionConfig
	{
		StringId64	m_hitReactionSetId;
		StringId64	m_deathReactionSetId;
	};
	HitReactionConfig m_hitReaction;
	HitReactionConfig m_scriptOverrideHitReaction;

	struct ActionPackConfig
	{
		StringId64	m_tapCharacterId;
		StringId64	m_capCharacterId;
	}
	m_actionPack;

	struct MoveConfig
	{
		F32			m_speedScale;
	}
	m_move;

	struct SearchConfig
	{
		StringId64	m_waitSetId;
		StringId64	m_cornerCheckId;
		StringId64	m_proneCheckId;
		StringId64	m_grassCheckId;
		StringId64	m_ledgeCheckId;
	}
	m_search;

	struct RagdollConfig
	{
		StringId64	m_configId;
	}
	m_ragdoll;
};
