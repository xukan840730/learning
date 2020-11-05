/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/math/pretty-math.h"
#include "ndlib/process/bound-frame.h"

#include "gamelib/gameplay/ai/agent/motion-config.h"
#include "gamelib/gameplay/nav/action-pack-handle.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "gamelib/scriptx/h/nav-character-defines.h"
#include "gamelib/spline/catmull-rom.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class EntitySpawner;

FWD_DECL_PROCESS_HANDLE(Character);

/// --------------------------------------------------------------------------------------------------------------- ///
enum NavGoalReachedType
{
	kNavGoalReachedTypeStop,
	kNavGoalReachedTypeContinue,
	kNavGoalReachedTypeMax
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavMoveArgs
{
	static CONST_EXPR F32 kIgnorePlayerOnGoalRadius = 3.0f;

	Vector m_goalFaceDirPs	   = kZero;

	StringId64 m_mtSubcategory = INVALID_STRING_ID_64;
	StringId64 m_destAnimId	   = INVALID_STRING_ID_64;
	StringId64 m_performanceId = INVALID_STRING_ID_64;

	CharacterHandle m_hChaseChar;
	F32 m_distBeforeChaseChar = 0.0f;

	MotionType m_motionType	   = kMotionTypeMax;
	NavGoalReachedType m_goalReachedType = kNavGoalReachedTypeStop;
	DC::NpcStrafe m_strafeMode		= DC::kNpcStrafeNever;

	U32 m_allowedTraversalSkillMask = -1;
	F32 m_destAnimPhase	  = 0.0f;
	F32 m_goalRadius		  = 0.0f;
	F32 m_pathRadius		  = -1.0f;

	uintptr_t m_apUserData	  = 0;
	bool m_goalFaceDirValid	  = false;
	bool m_ignorePlayerOnGoal = false;
	bool m_recklessStopping	  = false;
	bool m_slowInsteadOfAutoStop	  = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NavCommand
{
public:
	// 6 degrees
	static CONST_EXPR F32 kGoalDirEqualityDotThresh = 0.99452189536f;
	static CONST_EXPR F32 kStopGoalDirEqualityDotThresh = 0.52532198881f;

	enum Status
	{
		kStatusAwaitingCommands,  // idle essentially (no goal, no path, not navigating)
		kStatusCommandPending,	  // nav command pending (received but not processed)
		kStatusCommandInProgress, // nav command in progress
		kStatusCommandSucceeded,  // nav command succeeded
		kStatusCommandFailed,	  // nav command failed (we cannot move)
	};

	static const char* GetStatusName(Status i)
	{
		switch (i)
		{
		case kStatusAwaitingCommands:
			return "Awaiting Commands";
		case kStatusCommandPending:
			return "Command Pending";
		case kStatusCommandInProgress:
			return "Command In Progress";
		case kStatusCommandSucceeded:
			return "Command Succeeded";
		case kStatusCommandFailed:
			return "Command Failed";
		}

		return "<unknown status>";
	}

	static const char* GetGoalReachedTypeName(NavGoalReachedType i)
	{
		switch (i)
		{
		case kNavGoalReachedTypeStop:
			return "Stop";
		case kNavGoalReachedTypeContinue:
			return "Continue";
		}

		return "<unknown goal reached type>";
	}

	enum Type
	{
		kNone,
		kStopAndStand,
		kMoveToLocation,
		kMoveToActionPack,
		kMoveAlongSpline,
		kMoveInDirection,
		kSteerToLocation,
	};

	static const char* GetTypeName(Type i, bool withFacing = false)
	{
		switch (i)
		{
		case kNone:
			return "None";
		case kStopAndStand:
			return withFacing ? "StopAndFace" : "StopAndStand";
		case kMoveToLocation:
			return "MoveToLocation";
		case kMoveToActionPack:
			return "MoveToActionPack";
		case kMoveAlongSpline:
			return "MoveAlongSpline";
		case kMoveInDirection:
			return "MoveInDirection";
		case kSteerToLocation:
			return "SteerToLocation";
		}

		return "<unknown>";
	}

	NavCommand() : m_type(kNone) { Reset(); }

	void Reset()
	{
		m_type		   = kNone;
		m_moveArgs	   = NavMoveArgs();
		m_goalLocation = NavLocation();
		m_hGoalActionPack = nullptr;
		m_hPathSpline	  = nullptr;
		m_splineArcStart  = 0.0f;
		m_splineArcGoal	  = 0.0f;
		m_splineArcStep	  = 2.0f;
		m_moveDirPs		  = kZero;
		m_steerRateDps	  = 0.0f;

		m_sourceFile = "";
		m_sourceLine = -1;
		m_sourceFunc = "";
	}

	// Accessors
	bool IsValid() const { return m_type != kNone; }
	Type GetType() const { return m_type; }
	MotionType GetMotionType() const { return m_moveArgs.m_motionType; }
	StringId64 GetMtSubcategory() const { return m_moveArgs.m_mtSubcategory; }
	const NavLocation& GetGoalLocation() const { return m_goalLocation; }
	float GetGoalRadius() const { return m_moveArgs.m_goalRadius; }
	ActionPack* GetGoalActionPack() const { return m_hGoalActionPack.ToActionPack(); }
	NavGoalReachedType GetGoalReachedType() const { return m_moveArgs.m_goalReachedType; }
	uintptr_t GetActionPackUserData() const { return m_moveArgs.m_apUserData; }
	StringId64 GetDestAnimId() const { return m_moveArgs.m_destAnimId; }
	float GetDestAnimPhase() const { return m_moveArgs.m_destAnimPhase; }
	U32 GetGoalActionPackMgrId() const { return m_hGoalActionPack.GetMgrId(); }

	const CatmullRom* GetPathSpline() const { return m_hPathSpline.ToCatmullRom(); }
	float GetSplineArcStart() const { return m_splineArcStart; }
	float GetSplineArcGoal() const { return m_splineArcGoal; }
	float GetSplineArcStep() const { return m_splineArcStep; }
	float GetSplineAdavanceStartTolerance() const { return m_splineAdvanceStartTolerance; }

	NavMoveArgs& GetMoveArgs() { return m_moveArgs; }
	const NavMoveArgs& GetMoveArgs() const { return m_moveArgs; }

	Vector GetMoveDirPs() const { return m_moveDirPs; }
	float GetSteerRateDps() const { return m_steerRateDps; }

	// Configuration functions
	void AsStopAndStand(float goalRadius, const char* sourceFile, U32F sourceLine, const char* sourceFunc)
	{
		Reset();

		m_type = kStopAndStand;
		m_moveArgs.m_goalRadius = goalRadius;

		m_sourceFile = sourceFile;
		m_sourceLine = sourceLine;
		m_sourceFunc = sourceFunc;
	}

	void AsStopAndFace(Vector_arg faceDirPs,
					   float goalRadius,
					   const char* sourceFile,
					   U32F sourceLine,
					   const char* sourceFunc)
	{
		Reset();

		m_type = kStopAndStand;
		m_moveArgs.m_goalRadius		  = goalRadius;
		m_moveArgs.m_goalFaceDirPs	  = faceDirPs;
		m_moveArgs.m_goalFaceDirValid = true;

		m_sourceFile = sourceFile;
		m_sourceLine = sourceLine;
		m_sourceFunc = sourceFunc;
	}

	void AsMoveToLocation(const NavLocation& dest,
						  const NavMoveArgs& args,
						  const char* sourceFile,
						  U32F sourceLine,
						  const char* sourceFunc)
	{
		Reset();

		NAV_ASSERTF(IsReasonable(dest.GetPosPs()),
					("Unreasonable destination from %s %s:%d", sourceFunc, sourceFile, sourceLine));

		m_type		   = kMoveToLocation;
		m_goalLocation = dest;
		m_moveArgs	   = args;

		m_sourceFile = sourceFile;
		m_sourceLine = sourceLine;
		m_sourceFunc = sourceFunc;
	}

	void AsMoveToActionPack(ActionPack* pGoalActionPack,
							const NavMoveArgs& args,
							const char* sourceFile,
							U32F sourceLine,
							const char* sourceFunc)
	{
		Reset();

		m_type = kMoveToActionPack;
		m_hGoalActionPack = pGoalActionPack;
		m_moveArgs		  = args;

		m_sourceFile = sourceFile;
		m_sourceLine = sourceLine;
		m_sourceFunc = sourceFunc;
	}

	void AsMoveAlongSpline(const CatmullRom* pSpline,
						   float arcStart,
						   float arcGoal,
						   float arcStep,
						   float advanceStartTolerance,
						   const NavMoveArgs& args,
						   const char* sourceFile,
						   U32F sourceLine,
						   const char* sourceFunc)
	{
		Reset();

		m_type		  = kMoveAlongSpline;
		m_hPathSpline = pSpline;
		m_splineArcStart = arcStart;
		m_splineArcGoal	 = arcGoal;
		m_splineArcStep	 = arcStep;
		m_splineAdvanceStartTolerance = advanceStartTolerance;
		m_moveArgs = args;

		m_sourceFile = sourceFile;
		m_sourceLine = sourceLine;
		m_sourceFunc = sourceFunc;
	}

	void AsMoveInDirectionPs(Vector_arg moveDirPs,
							 const NavMoveArgs& args,
							 const char* sourceFile,
							 U32F sourceLine,
							 const char* sourceFunc)
	{
		Reset();

		m_type		= kMoveInDirection;
		m_moveArgs	= args;
		m_moveDirPs = moveDirPs;

		m_sourceFile = sourceFile;
		m_sourceLine = sourceLine;
		m_sourceFunc = sourceFunc;
	}

	void AsSteerToLocation(const NavLocation& dest,
						   float steerRateDps,
						   const NavMoveArgs& args,
						   const char* sourceFile,
						   U32F sourceLine,
						   const char* sourceFunc)
	{
		Reset();

		m_type = kSteerToLocation;
		m_goalLocation = dest;
		m_moveArgs = args;
		m_steerRateDps = steerRateDps;

		m_sourceFile = sourceFile;
		m_sourceLine = sourceLine;
		m_sourceFunc = sourceFunc;
	}

	void UpdateMoveDirPs(Vector_arg newMoveDirPs)
	{
		NAV_ASSERT(m_type == kMoveInDirection);
		NAV_ASSERT(IsReasonable(newMoveDirPs));

		m_moveDirPs = newMoveDirPs;
	}

	void UpdateSteerRateDps(float steerRateDps)
	{
		NAV_ASSERT(m_type == kSteerToLocation);
		NAV_ASSERT(IsReasonable(steerRateDps));
		
		m_steerRateDps = steerRateDps;
	}

	const char* GetTypeName() const { return GetTypeName(GetType(), m_moveArgs.m_goalFaceDirValid); }
	void DebugPrint(DoutBase* pDout, bool withSource) const;

	const char* GetSourceFile() const { return m_sourceFile; }
	U32F GetSourceLine() const { return m_sourceLine; }
	const char* GetSourceFunc() const { return m_sourceFunc; }

	bool PathFindRequired() const
	{
		bool wantPathFind = false;

		switch (m_type)
		{
		case kMoveToLocation:
		case kMoveToActionPack:
		case kMoveAlongSpline:
		case kSteerToLocation:
			wantPathFind = true;
			break;
		}

		return wantPathFind;
	}

	bool HasSameGoalFaceDir(const NavCommand& rhs) const
	{
		if (!m_moveArgs.m_goalFaceDirValid && !rhs.m_moveArgs.m_goalFaceDirValid)
		{
			return true;
		}

		if (m_moveArgs.m_goalFaceDirValid != rhs.m_moveArgs.m_goalFaceDirValid)
		{
			return false;
		}

		const float dotP = Dot(m_moveArgs.m_goalFaceDirPs, rhs.m_moveArgs.m_goalFaceDirPs);

		if (m_type == kStopAndStand && rhs.m_type == kStopAndStand)
		{
			return dotP > kStopGoalDirEqualityDotThresh;
		}
		else
		{
			return dotP > kGoalDirEqualityDotThresh;
		}
	}

private:
	Type m_type;
	NavMoveArgs m_moveArgs;
	NavLocation m_goalLocation;
	ActionPackHandle m_hGoalActionPack;
	CatmullRom::Handle m_hPathSpline;
	float m_splineArcStart;
	float m_splineArcGoal;
	float m_splineArcStep;
	float m_splineAdvanceStartTolerance;
	float m_steerRateDps;
	Vector m_moveDirPs;

	const char* m_sourceFile;
	const char* m_sourceFunc;
	U32F m_sourceLine;
};
