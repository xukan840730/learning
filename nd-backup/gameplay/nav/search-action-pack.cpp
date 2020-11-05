/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/search-action-pack.h"

#include "gamelib/gameplay/nav/cover-action-pack.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"

/// --------------------------------------------------------------------------------------------------------------- ///
SearchActionPack::SearchActionPack() : ActionPack(kSearchActionPack), m_mode(Mode::kInvalid), m_wallDirPs(kInvalidVector), m_apDirPs(kInvalidVector) {}

/// --------------------------------------------------------------------------------------------------------------- ///
SearchActionPack::SearchActionPack(const ActionPack* pSourceAp,
								   const BoundFrame& bfLoc,
								   const NavLocation& navLoc,
								   Point_arg regPosLs)
	: ActionPack(kSearchActionPack, regPosLs, bfLoc, (const Level*)nullptr)
	, m_hSourceAp(pSourceAp)
	, m_navLoc(navLoc)
	, m_mode(DetermineMode(pSourceAp))
	, m_wallDirPs(kInvalidVector)
	, m_apDirPs(kInvalidVector)
{
	m_hRegisteredNavLoc = navLoc;

	if (const CoverActionPack* pCoverAp = CoverActionPack::FromActionPack(pSourceAp))
	{
		const CoverDefinition& coverDef = pCoverAp->GetDefinition();

		switch (coverDef.m_coverType)
		{
		case CoverDefinition::kCoverCrouchLeft:
		case CoverDefinition::kCoverStandLeft:
			m_loc.AdjustRotationPs(QuatFromAxisAngle(kUnitYAxis, PI_DIV_2 * -0.5f));
			break;

		case CoverDefinition::kCoverCrouchRight:
		case CoverDefinition::kCoverStandRight:
			m_loc.AdjustRotationPs(QuatFromAxisAngle(kUnitYAxis, PI_DIV_2 * 0.5f));
			break;
		}

		m_wallDirPs = pCoverAp->GetWallDirectionPs();
		m_apDirPs = GetLocalZ(pCoverAp->GetLocatorPs().GetRotation());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void SearchActionPack::DebugDraw(DebugPrimTime tt /* = kPrimDuration1FrameAuto */) const
{
	ParentClass::DebugDraw(tt);

	StringBuilder<256> desc;
	desc.format("SearchActionPack [%s]", GetModeStr(m_mode));

	if (const ActionPack* pSourceAp = m_hSourceAp.ToActionPack())
	{
		desc.append("\n");
		desc.append_format("source: %s", pSourceAp->GetName());

		pSourceAp->DebugDraw(tt);
	}

	//g_prim.Draw(DebugString(GetBoundFrame().GetTranslationWs(), desc.c_str(), kColorWhite, 0.5f), tt);
	g_prim.Draw(DebugCoordAxesLabeled(GetLocatorWs(), desc.c_str(), 0.5f, kPrimEnableHiddenLineAlpha, 4.0f, kColorWhite, 0.5f), tt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool SearchActionPack::RegisterInternal()
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	m_hRegisteredNavLoc = m_navLoc;

	return m_hRegisteredNavLoc.IsValid();
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
SearchActionPack::Mode SearchActionPack::DetermineMode(const ActionPack* pSourceAp)
{
	if (!pSourceAp)
		return Mode::kInvalid;

	Mode mode = Mode::kInvalid;

	switch (pSourceAp->GetType())
	{
	case ActionPack::kCoverActionPack:
		{
			const CoverActionPack* pCoverAp = CoverActionPack::FromActionPack(pSourceAp);
			NAV_ASSERT(pCoverAp);

			const CoverDefinition& coverDef = pCoverAp->GetDefinition();
			const bool isDoorCheck = !coverDef.m_canCornerCheck;

			if (isDoorCheck)
			{
				mode = SearchActionPack::Mode::kDoorCheck;
			}
			else
			{
				mode = SearchActionPack::Mode::kCornerCheck;
			}
		}
		break;

	case ActionPack::kPulloutActionPack:
	default:
		mode = SearchActionPack::Mode::kPullout;
		break;
	}

	return mode;
}
