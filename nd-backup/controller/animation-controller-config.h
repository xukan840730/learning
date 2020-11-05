/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef ANIMATION_CONTROLLER_CONFIG_H
#define ANIMATION_CONTROLLER_CONFIG_H

#include "gamelib/gameplay/ai/controller/nd-animation-controller-config.h"

/// --------------------------------------------------------------------------------------------------------------- ///
// AnimControllerConfig: Configuration params for anim controllers
/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimControllerConfig : public NdAnimControllerConfig
{
	typedef NdAnimControllerConfig ParentClass;

	AnimControllerConfig();
	virtual void DebugDraw(ScreenSpaceTextPrinter* pPrinter) const override;

	struct CoverConfig
	{
		StringId64	m_transferSetId;
		StringId64	m_coverIdleIkData;
	}
	m_gameCover;

	struct HitReactionConfig
	{
		static const U32 kMaxSetIds = 2;

		StringId64	m_dodgeReactionSetId;
		StringId64  m_diveReactionSetId;
		StringId64	m_setIds[kMaxSetIds];
	}
	m_gameHitReaction;

	struct CombatConfig
	{
		StringId64	m_wildFireSetId;
	}
	m_combat;
};

#endif	//ANIMATION_CONTROLLER_CONFIG_H
