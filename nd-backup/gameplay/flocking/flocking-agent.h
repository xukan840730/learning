/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef FLOCKING_AGENT_H
#define FLOCKING_AGENT_H

#include "ndlib/script/script-pointer.h"

#include "gamelib/gameplay/flocking/flocking-config.h"
#include "gamelib/scriptx/h/flocking-defines.h"

class IPathWaypoints;
class CatmullRom;

namespace Flocking
{
	class FlockingAgent;
	class FlockingRvoObstacle;

	enum FlockingRvoNeighborType
	{
		kAgent,
		kObstacle, 
	};

	struct FlockingRvoNeighbor
	{
		FlockingRvoNeighborType m_type;
		float m_distXzSqr;
		union
		{
			const FlockingAgent* m_pAgent;
			const FlockingRvoObstacle* m_pObstacle;
		};

#ifndef FINALB_BUILD
		Point m_closestPt;
#endif
	};

	class FlockingRvoNeighbors
	{
	public:
		FlockingRvoNeighbors() : m_pNeighbors(nullptr)
							, m_numNeighbors(0)
		{
		}

	public:
		void Reset(int maxNumNeighbors);
		void Clear()
		{
			m_numNeighbors = 0;
		}

		void Insert(int maxNumNeighbors, const FlockingRvoNeighbor& neighbor);

	public:
		FlockingRvoNeighbor* m_pNeighbors;
		int m_numNeighbors;
	};

	enum FlockingAgentState
	{
		kNatural, 
		kFlee, 
		kFleeAlong, 
		kQueueMoveTo, 
	};

	enum RvoAgentConfig
	{
		kNormal, 
		kIgnore, 
	};

	enum QueueMoveToBehavior
	{
		kLoose,
		kPrecise
	};

	enum QueueMoveToMotion
	{
		kStop, 
		kSlow, 
		kFast, 
		kFastImmediate,	// Set by the flocking manager to command flock skill to move immediately.
	};

	class FlockingAgent
	{
	public:
		static CONST_EXPR float kPlayerThreatRadius = 6.5f;

	public:
		void Init(int idx, const Point_arg pos, const Vector_arg forward, const StringId64 flockingParamsId);

		const ScriptPointer<DC::Flocking2dParams> GetParams() const { return m_params; }
		void SetParams(const StringId64 paramsNameId) 
		{
			m_params = ScriptPointer<DC::Flocking2dParams>(paramsNameId); 
			GAMEPLAY_ASSERT(m_params.Valid());
		}

		const ScriptPointer<DC::Flocking2dVisionParams> GetVisionParams() const { return m_visionParams; }
		void SetVisionParams(const StringId64 visionParamsNameId) 
		{ 
			m_visionParams = ScriptPointer<DC::Flocking2dVisionParams>(visionParamsNameId); 
			GAMEPLAY_ASSERT(m_visionParams.Valid());
		}

		bool IsEnabled() const { return m_isEnabled; }
		void SetEnabled(bool isEnabled) { m_isEnabled = isEnabled; }

		FlockingAgentState GetState() const { return m_state; }
		void SetState(FlockingAgentState state) { m_state = state; }

		Point GetPosition() const { return m_pos; }
		void SetPosition(const Point_arg pos) { m_pos = pos; }

		Vector GetForward() const { return m_forward; }
		void SetForward(const Vector_arg forward) { m_forward = forward; }

		Vector GetVelocity() const { return m_velocity; }
		void SetVelocity(const Vector_arg velocity);

		StringId64 GetAssignedWanderRegion() const { return m_assignedWanderRegionId; }
		void SetAssignedWanderRegion(const StringId64 assignedRegionId) { m_assignedWanderRegionId = assignedRegionId; }
		void SetIgnorePlayerRegionBlocking(const bool ignorePlayerRegionBlocking) { m_ignorePlayerRegionBlocking = ignorePlayerRegionBlocking; }
		void SetWanderRegion(const Region *const pRegion) { m_pWanderRegion = pRegion; }
		const Region* GetWanderRegion() const { return m_pWanderRegion; }
		bool GetIgnorePlayerRegionBlocking() const { return m_ignorePlayerRegionBlocking; }

		void SetQueueMoveTo(const Locator& loc) 
		{ 
			m_state = kQueueMoveTo; 
			m_queueMoveTo = loc;
			m_queueMoveToBehavior = kLoose; 
			m_queueMoveToMoveDir = m_forward;
			m_queueMoveToMotion = kFast; 
		}
		Point GetQueueMoveTo() const { GAMEPLAY_ASSERT(m_state == kQueueMoveTo); return m_queueMoveTo.GetPosition(); }
		Quat GetQueueMoveToRotation() const { GAMEPLAY_ASSERT(m_state == kQueueMoveTo); return m_queueMoveTo.GetRotation(); }
		void SetQueueMoveToBehavior(QueueMoveToBehavior queueMoveToBehavior) { m_queueMoveToBehavior = queueMoveToBehavior; }
		QueueMoveToBehavior GetQueueMoveToBehavior() const { return m_queueMoveToBehavior; }
		void SetQueueMoveToMoveDir(const Vector_arg queueMoveToMoveDirWs) { m_queueMoveToMoveDir = queueMoveToMoveDirWs; }
		Vector GetQueueMoveToMoveDir() const { return m_queueMoveToMoveDir; }
		void SetQueueMoveToMotion(QueueMoveToMotion queueMoveToMotion) { m_queueMoveToMotion = queueMoveToMotion; }
		QueueMoveToMotion GetQueueMoveToMotion() const { GAMEPLAY_ASSERT(m_state == kQueueMoveTo); return m_queueMoveToMotion; }
		const CatmullRom* GetQueueMoveToSpline() const { return m_pQueueMoveToSpline; }
		void SetQueueMoveToSpline(const CatmullRom *const pQueueMoveToSpline);
		void SetQueueMoveToIsBlocked(bool isBlocked) { m_queueMoveToIsBlocked = isBlocked; }
		bool GetQueueMoveToIsBlocked() const { return m_queueMoveToIsBlocked; }

		bool GetFleeTarget(Point& outPos, Vector& outFacing) const { outPos = m_fleeTargetPos; outFacing = m_fleeTargetFacing; return m_isFleeTargetValid; }
		bool GetForceFlee() const { return m_forceFlee; }
		void SetFleeTarget(const Point_arg pos, const Vector_arg facing, bool forceFlee) { m_fleeTargetPos = pos; m_fleeTargetFacing = facing; m_isFleeTargetValid = true; m_forceFlee = forceFlee; }

		int GetRvoMaxNeighbors() const { return g_flockingConfig.m_rvoMaxNeighbors; }
		float GetRvoNeighborRadius() const { return g_flockingConfig.m_rvoNeighborRadius; }

		int GetRvoVelocitySamples() const { return g_flockingConfig.m_rvoVelocitySamples; }
		float GetRvoTimePenaltyWeight() const { return g_flockingConfig.m_rvoTimePenaltyWeight; }

		void SetSelected(bool isSelected) { m_isSelected = isSelected; }
		bool GetSelected() const { return m_isSelected; }

		Vector ComputeAlignment(const Vector_arg fleeToTargetDir, const Vector_arg debugDrawOffset) const;
		Vector ComputeCohesion(const Vector_arg fleeToTargetDir, const Vector_arg debugDrawOffset) const;
		Vector ComputeSeperation(float seperationNeighborRadius, const Vector_arg debugDrawOffset) const;
		Vector ComputeNewVelocityRvo(const Vector_arg desiredVelocity, RvoAgentConfig rvoAgentConfig);

		int GetIdx() const { return m_idx; }
		void DebugDraw();

	protected: 
		void ComputeNeighborsRvo(RvoAgentConfig rvoAgentConfig);

	protected:
		FlockingAgentState m_state;
		bool m_isEnabled;

		Point m_pos;
		Vector m_forward;
		Vector m_velocity;

		StringId64 m_assignedWanderRegionId;
		const Region* m_pWanderRegion;
		bool m_ignorePlayerRegionBlocking;

		Point m_fleeTargetPos;
		Vector m_fleeTargetFacing;
		bool m_isFleeTargetValid;
		bool m_forceFlee;

		Locator m_queueMoveTo;
		QueueMoveToBehavior m_queueMoveToBehavior;
		Vector m_queueMoveToMoveDir;
		QueueMoveToMotion m_queueMoveToMotion;
		const CatmullRom* m_pQueueMoveToSpline;
		bool m_queueMoveToIsBlocked;	// Is blocked by player

		FlockingRvoNeighbors m_rvoNeighbors;
		bool m_rvoCollided;

		ScriptPointer<DC::Flocking2dParams> m_params;
		ScriptPointer<DC::Flocking2dVisionParams> m_visionParams;

		int m_idx;			// Debugging
		bool m_isSelected;	// Debugging
	};

	float TimeToCollideRvoRef(const Point_arg p,
							const Vector_arg v,
							const Point_arg p2,
							float r,
							bool rvoCollided);

	float TimeToCollideRvo2Ref(const Point_arg p,
							const Vector_arg v,
							const Point_arg a,
							const Point_arg b,
							bool rvoCollided);

	float TimeToCollideAgentRvoRef(float t, 
								float timeStep, 
								const Vector_arg velocity, 
								float maxSpeedSqr, 
								bool rvoCollided);

	float TimeToCollideObstacleRvoRef(float ta, 
									float tb, 
									float tn, 
									float tnn, 
									float timeStep, 
									const Vector_arg velocity, 
									float maxSpeedSqr, 
									float maxAccelerationSqr, 
									bool rvoCollided);
}

#endif // FLOCKING_AGENT_H


