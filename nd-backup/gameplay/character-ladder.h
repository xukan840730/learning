/*
 * Copyright (c) 2016 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/feature/feature-db-ref.h"
#include "gamelib/gameplay/background-ladder.h"
#include "gamelib/gameplay/nd-subsystem-anim-action.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ICharacterLadderInterface;

/// --------------------------------------------------------------------------------------------------------------- ///
class ICharacterLadder : public NdSubsystemAnimController
{
public:
	static CONST_EXPR float kLadderRungSpacing = 0.3f;

	enum class LadderState
	{
		kNone,
		kEnter,
		kIdle,
		kMove,
		kExit,
	};

	enum class LadderEnterState
	{
		kInvalid,
		kBottom,
		kTop,
		kTopSide,
		kScript,
		kSwim,
		kJumpLanding,
	};

	enum class LadderExitState
	{
		kInvalid,
		kFallBack,
		kFallDown,
		kClimbUp,
		kClimbDown,
		kMove,
		kSubsystem,
	};

	struct LadderSpawnInfo : public SubsystemSpawnInfo
	{
		using SubsystemSpawnInfo::SubsystemSpawnInfo;

		StringId64				m_ladderInterfaceType = INVALID_STRING_ID_64;
		LadderEnterState		m_enterState = LadderEnterState::kInvalid;
		FeatureEdgeReference	m_edge;
		BackgroundLadderHandle	m_hBgLadder;
		float					m_blendInTime = -1.0f;
	};

	struct InputData
	{
		float m_targetMoveSpeed = 0.0f;		// -1.0f to 1.0f for up/down move speed
		bool m_dropFromLadder = false;		// Request drop to fall
		bool m_sprintActive = false;
	};

	virtual LadderState GetLadderState() const = 0;
	virtual LadderEnterState GetLadderEnterState() = 0;
	virtual LadderExitState GetLadderExitState() = 0;
	virtual void ExitLadderState(LadderExitState exitState, NdSubsystemAnimAction* pExitAction = nullptr) = 0;

	virtual Locator GetInterpolatedLadderGrabLoc() const = 0;
	virtual EdgeInfo GetCurrentEdge() const = 0;

	virtual bool IsAutoclimbActive() const = 0;

	virtual void ForceMoveSpeedThisFrame(float speed) = 0;

	virtual ICharacterLadderInterface* GetInterface() = 0;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class ICharacterLadderInterface : public NdSubsystem
{
public:
	typedef NdSubsystem ParentClass;

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override
	{
		ParentClass::Relocate(deltaPos, lowerBound, upperBound);
		RelocatePointer(m_pLadder, deltaPos, lowerBound, upperBound);
	}

	virtual const ICharacterLadder* GetLadderController() const { return m_pLadder; }
	virtual ICharacterLadder* GetLadderController() { return m_pLadder; }
	virtual void SetLadderController(ICharacterLadder* pLadder, const ICharacterLadder::LadderSpawnInfo& spawnInfo)
	{
		m_pLadder = pLadder;
	}
	virtual void GetInput(ICharacterLadder::InputData* pInputData) {}
	virtual void NotifyExitLadderState(ICharacterLadder::LadderExitState exitState) {}
	virtual void NotifyExitSubsystem(NdSubsystemAnimAction* pExitSys) {}
	virtual void NotifyExitClimbOver() {}
	virtual void NotifyCurrentEdge(FeatureEdgeReference edge) {}
	virtual float GetClimbSpeedMult() { return 1.0f; }

	virtual bool ShouldExitTopRailing() { return false; }

	// Max height to allow dropping from a ladder without pushing circle
	virtual float GetClimbOffBottomMaxHeight() { return kLargeFloat; }

	virtual Point GetCameraLookAtPoint(const Point& defaultPt) const { return defaultPt; }
	virtual Point GetCameraSafePoint(const Point& defaultPt) const { return defaultPt; }

	virtual int GatherEdgesSphereUnsorted(ListArray<EdgeInfo>& edges,
										  const Sphere& sphere,
										  FeatureEdge::Flags requiredEdgeFlags) const = 0;

	virtual StringId64 GetLedgeClimbUpInterfaceTypeId() const = 0;
	virtual float GetDropHeightFloor() const { return 0.5f; }

private:
	ICharacterLadder* m_pLadder = nullptr;
};

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsSameLadderEdge(FeatureEdgeReference ladderEdge, FeatureEdgeReference edge);
EdgeInfo GetLadderEdgeCenter(FeatureEdgeReference edge);
EdgeInfo GetNextLadderEdge(const ListArray<EdgeInfo>& nearbyEdges, EdgeInfo ladderEdge, int steps);
