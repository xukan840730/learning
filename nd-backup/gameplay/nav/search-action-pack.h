/*
 * Copyright (c) 2019 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/gameplay/nav/action-pack-handle.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class SearchActionPack : public ActionPack
{
public:
	enum class Mode
	{
		kInvalid,
		kCornerCheck,
		kDoorCheck,
		kPullout,
	};

	static const char* GetModeStr(Mode m)
	{
		switch (m)
		{
		case Mode::kCornerCheck: return "CornerCheck";
		case Mode::kDoorCheck: return "DoorCheck";
		case Mode::kPullout: return "Pullout";
		}

		return "<invalid>";
	}

	DECLARE_ACTION_PACK_TYPE(SearchActionPack);
	typedef ActionPack ParentClass;

	SearchActionPack();
	SearchActionPack(const ActionPack* pSourceAp, const BoundFrame& bfLoc, const NavLocation& navLoc, Point_arg regPosLs);

	virtual void DebugDraw(DebugPrimTime tt = kPrimDuration1FrameAuto) const override;

	Mode GetMode() const { return m_mode; }
	void SetExitGoalLoc(const NavLocation& loc) { m_exitGoalLoc = loc; }
	NavLocation GetNavLoc() const { return m_navLoc; }
	NavLocation GetExitGoalLoc() const { return m_exitGoalLoc; }
	Vector GetCoverWallDirPs() const { return m_wallDirPs; }
	Vector GetCoverApDirPs() const { return m_apDirPs; }

	bool IsCornerCheck() const { return m_mode == Mode::kCornerCheck; }
	bool IsDoorCheck() const { return m_mode == Mode::kPullout; }
	bool IsPullout() const { return m_mode == Mode::kPullout; }

private:
	static Mode DetermineMode(const ActionPack* pSourceAp);

	virtual bool HasNavMeshClearance(const NavLocation& navLoc,
									 bool debugDraw = false,
									 DebugPrimTime tt = kPrimDuration1FramePauseable) const override
	{
		return true;
	}

	virtual bool RegisterInternal() override;

	ActionPackHandle m_hSourceAp;
	NavLocation m_navLoc;
	NavLocation m_exitGoalLoc;
	Mode m_mode;

	// cached so it's not necessary to dereference the source AP to access (for covers only)
	Vector m_wallDirPs;
	Vector m_apDirPs;
};
