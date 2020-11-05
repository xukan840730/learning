/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/flocking/flocking-agent.h"

#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/flocking/flocking-mgr.h"
#include "gamelib/gameplay/flocking/flocking-obstacle.h"
#include "gamelib/gameplay/nav/path-waypoints.h"
#include "gamelib/spline/catmull-rom.h"

namespace Flocking
{
	#define RVO_INFINITY (9e9f)

	static float kLenEpsilon = 0.001f;
	static float kLenEpsilonSqr = kLenEpsilon * kLenEpsilon;

	static inline Vector GetDebugDrawOffset()
	{
		return Vector(0.0f, 0.05f, 0.0f); 
	}

	float TimeToCollideRvoRef(const Point_arg p, 
							const Vector_arg v, 
							const Point_arg p2, 
							float r, 
							bool rvoCollided) 
	{
		const Vector ba = p2 - p;
		const float rr = r * r;
		
		float t;

		const float det = v.X() * ba.Z() - v.Z() * ba.X();
		const float detSqr = det * det;
		const float vv = LengthSqr(v);

		const float discr = -detSqr + rr * vv;
		if (discr > 0.0f) 
		{
			const float dot = Dot(v, ba);
			const float d = sqrt(discr);

			if (rvoCollided)
			{
				t = (dot + d) / vv;

				if (t < 0.0f) 
				{
					t = -RVO_INFINITY;
				}
			}
			else 
			{
				t = (dot - d) / vv;

				if (t < 0.0f) 
				{
					t = RVO_INFINITY;
				}
			}
		}
		else 
		{
			if (rvoCollided) 
			{
				t = -RVO_INFINITY;
			}
			else 
			{
				t = RVO_INFINITY;
			}
		}
		
		return t;
	}

	FlockingGlobalConfig g_flockingConfig;

	float TimeToCollideRvo2Ref(const Point_arg p, 
							const Vector_arg v, 
							const Point_arg a, 
							const Point_arg b, 
							bool rvoCollided)
	{
		const Vector ab = b - a;

		const float d0 = v.X() * ab.Z() - v.Z() * ab.X();
		if (d0 == 0.0f)	// ray and line are collinear
		{
			if (rvoCollided) 
			{
				return -RVO_INFINITY;
			}
			else 
			{
				return RVO_INFINITY;
			}
		}

		const float invD0 = 1.0f / d0;

		const Vector pa = a - p;

		const float d1 = pa.X() * ab.Z() - pa.Z() * ab.X();
		const float t = d1 * invD0;

		const float d2 = -pa.X() * v.Z() + pa.Z() * v.X();
		const float s = d2 * -invD0;

		if (t < 0.0f || 
			s < 0.0f || 
			s > 1.0f) 
		{
			if (rvoCollided)
			{
				return -RVO_INFINITY;
			}
			else 
			{
				return RVO_INFINITY;
			}
		}
		
		return t;
	}

	float TimeToCollideAgentRvoRef(float t, 
								float timeStep,
								const Vector_arg velocity,
								float maxSpeedSqr,
								bool rvoCollided)
	{
		if (rvoCollided)
		{
			const float lenSqr = LengthSqr(velocity);
			return -ceilf(t / timeStep) - (lenSqr / maxSpeedSqr);	// NOTE: what doest this mean?
		}
		
		return t;
	}

	float TimeToCollideObstacleRvoRef(float ta,
									float tb,
									float tn,
									float tnn,
									float timeStep,
									const Vector_arg velocity,
									float maxSpeedSqr, 
									float maxAccelerationSqr,
									bool rvoCollided)
	{
		const float lenSqr = LengthSqr(velocity);

		if (rvoCollided)
		{
			const float tMax = fmax(fmax(fmax(ta, tb), tn), tnn);
			return -ceilf(tMax / timeStep) - (lenSqr / maxSpeedSqr);	// NOTE: what doest this mean?
		}

		const float tMin = fmin(fmin(fmin(ta, tb), tn), tnn);
		if (tMin < timeStep ||
			(tMin * tMin) < lenSqr / maxAccelerationSqr)
		{
			return tMin;
		}
			
		return RVO_INFINITY; // No time penalty
	}

	void FlockingRvoNeighbors::Reset(int maxNumNeighbors)
	{
		AllocateJanitor kk(kAllocSingleGameFrame, FILE_LINE_FUNC);

		m_pNeighbors = NDI_NEW FlockingRvoNeighbor[maxNumNeighbors];
		m_numNeighbors = 0;
	}

	static int SearchInsertPosition(const FlockingRvoNeighbors *const pNeighbors, const FlockingRvoNeighbor *const pTargetNeighbor)
	{
		int s = 0;
		int e = pNeighbors->m_numNeighbors - 1;

		while (s <= e)
		{
			const int m = s + (e - s) / 2;
			const FlockingRvoNeighbor *const pNeighbor = &pNeighbors->m_pNeighbors[m];

			if (pNeighbor->m_distXzSqr == pTargetNeighbor->m_distXzSqr)
			{
				return m;
			}
			else if (pNeighbor->m_distXzSqr < pTargetNeighbor->m_distXzSqr)
			{
				s = m + 1;
			}
			else
			{
				e = m - 1;
			}
		}

		return e + 1;
	}

	void FlockingRvoNeighbors::Insert(int maxNumNeighbors, const FlockingRvoNeighbor& neighbor)
	{
		// Insert, keep neighbors sorted
		GAMEPLAY_ASSERT(m_numNeighbors <= maxNumNeighbors);

		if (m_numNeighbors == maxNumNeighbors)
		{
			m_numNeighbors--;	// Full, erase the last neighbor
		}

		const int position = SearchInsertPosition(this, &neighbor);
		GAMEPLAY_ASSERT(0 <= position);
		GAMEPLAY_ASSERT(position < maxNumNeighbors);

		for (int curr = m_numNeighbors; curr > position; curr--)
		{
			const int prev = curr - 1;
			m_pNeighbors[curr] = m_pNeighbors[prev];
		}

		m_pNeighbors[position] = neighbor;
		m_numNeighbors++;
	}

	void FlockingAgent::Init(int idx, const Point_arg pos, const Vector_arg forward, const StringId64 flockingParamsId)
	{
		m_state = kNatural;
		m_isEnabled = true;

		m_params = ScriptPointer<DC::Flocking2dParams>(flockingParamsId);

		m_pos = pos;
		m_forward = forward;
		m_velocity = kZero;

		m_assignedWanderRegionId = INVALID_STRING_ID_64;
		m_pWanderRegion = nullptr;
		m_ignorePlayerRegionBlocking = false;

		m_fleeTargetPos = kOrigin;
		m_fleeTargetFacing = kUnitZAxis;
		m_isFleeTargetValid = false;
		m_forceFlee = false;

		m_queueMoveTo = Locator(kIdentity);
		m_queueMoveToBehavior = kLoose;
		m_queueMoveToMoveDir = kZero;
		m_queueMoveToMotion = kFast;
		m_pQueueMoveToSpline = nullptr;
		m_queueMoveToIsBlocked = false;

		m_rvoNeighbors.Clear();
		m_rvoCollided = false;

		m_idx = idx;
		m_isSelected = false;
	}

	const char* GetStateName(const FlockingAgent *const pFlockingAgent)
	{
		if (!pFlockingAgent->IsEnabled())
		{
			return "kDisabled";
		}

		switch (pFlockingAgent->GetState())
		{
		case kNatural:
			return "kNatural";
		case kFlee:
			return "kFlee";
		case kFleeAlong:
			return "kFleeAlong";
		case kQueueMoveTo:
			return "kQueueMoveTo";
		default:
			GAMEPLAY_ASSERT(false);
			break;
		}

		return "kUnknown";
	}

	void FlockingAgent::SetVelocity(const Vector_arg velocity)
	{
		const float kSpeedEpsilon = 0.015f;
		const float kSpeedEpsilonSqr = kSpeedEpsilon * kSpeedEpsilon;

		const float speedSqr = LengthSqr(velocity);
		if (speedSqr < kSpeedEpsilonSqr)
		{
			m_velocity = kZero;
		}
		else
		{
			m_velocity = velocity;
		}
	}

	void FlockingAgent::SetQueueMoveToSpline(const CatmullRom *const pQueueMoveToSpline)
	{
		if (pQueueMoveToSpline && 
			m_pQueueMoveToSpline)
		{
			const Point posXz = Point(m_pos.X(), 0.0f, m_pos.Z());

			const Point closestPt0 = m_pQueueMoveToSpline->FindClosestPointOnSpline(m_pos);
			const Point closestPt0Xz = Point(closestPt0.X(), 0.0f, closestPt0.Z());
			const Point closestPt1 = pQueueMoveToSpline->FindClosestPointOnSpline(m_pos);
			const Point closestPt1Xz = Point(closestPt1.X(), 0.0f, closestPt1.Z());

			const float distXz0 = DistSqr(closestPt0Xz, posXz);
			const float distXz1 = DistSqr(closestPt1Xz, posXz);
			
			const float kJumpSplineThreshold = 0.7f;
			const float diff = distXz0 - distXz1;
			if (diff >= kJumpSplineThreshold)
			{
				m_pQueueMoveToSpline = pQueueMoveToSpline;
			}
		}
		else
		{
			m_pQueueMoveToSpline = pQueueMoveToSpline;
		}
	}

	Vector FlockingAgent::ComputeAlignment(const Vector_arg fleeToTargetDir, const Vector_arg debugDrawOffset) const
	{
		GAMEPLAY_ASSERT(m_state != kNatural);

		Vector alignment = kZero;
		if (m_params->m_disableAlignment)
		{
			return alignment;
		}

		const Vector forwardXz = Normalize(Vector(m_forward.X(), 0.0f, m_forward.Z()));

		FlockingAgent* ppAgents[kMaxFlockingAgents];
		U32F numAgents;
		FindAllFlockingAgents(ppAgents, numAgents);

		const float visionRad = DegreesToRadians(m_params->m_alignmentVisionDeg);

		const float neighborR = m_params->m_alignmentNeighborRadius * (1.0f + frand(0.0f, 0.25f));
		const float neighborRr = neighborR * neighborR;

		const Point pos = GetPosition();
		const Point posXz = Point(pos.X(), 0.0f, pos.Z());
		const Point posDraw = pos + debugDrawOffset;
		if (FALSE_IN_FINAL_BUILD(g_flockingConfig.m_debugSimulation) /*&&
			m_isSelected*/)
		{
			if (m_params->m_alignmentVisionDeg < 180.0f)
			{
				DebugDrawFlatCone(posDraw, forwardXz, kUnitYAxis, visionRad, neighborR, kColorGreenTrans, PrimAttrib(0), kPrimDuration1FramePauseable);
			}
			else
			{
				g_prim.Draw(DebugCircle(posDraw, kUnitYAxis, neighborR, kColorGreenTrans), kPrimDuration1FramePauseable);
			}
		}

		float count = 0.0f;

		for (U32F iAgent = 0; iAgent < numAgents; iAgent++)
		{
			FlockingAgent *const pOtherAgent = ppAgents[iAgent];

			if (pOtherAgent == this ||
				pOtherAgent->GetState() == kNatural || 
				pOtherAgent->GetState() == kQueueMoveTo)
			{
				continue;
			}

			if (GetState() != kFleeAlong)
			{
				const Vector otherVelocity = pOtherAgent->GetVelocity();
				if (Dot(fleeToTargetDir, otherVelocity) <= 0.0f)
				{
					continue;
				}
			}

			const Point otherPos = pOtherAgent->GetPosition();
			const Point otherPosXz = Point(otherPos.X(), 0.0f, otherPos.Z());

			const Vector toNeighborXz = otherPosXz - posXz;
			const float distXzSqr = LengthSqr(toNeighborXz);
			if (distXzSqr > neighborRr)
			{
				continue;
			}

			if (m_state == kFlee && // kFleeAlong doesnt consider vision angle
				m_params->m_alignmentVisionDeg < 180.0f)
			{
				const Vector toNeighborXzDir = SafeNormalize(toNeighborXz, kZero);
				const float d = Clamp((F32)Dot(forwardXz, toNeighborXzDir), -1.0f, 1.0f);
				if (Acos(d) > visionRad)
				{
					continue;
				}
			}

			if (FALSE_IN_FINAL_BUILD(g_flockingConfig.m_debugSimulation) /*&&
				m_isSelected*/)
			{
				const Point otherPosDraw = otherPos + debugDrawOffset;
				g_prim.Draw(DebugLine(posDraw, otherPosDraw, kColorGreenTrans), kPrimDuration1FramePauseable);
			}

			alignment += pOtherAgent->m_velocity;
			count += 1.0f;
		}

		if (count > 0.0f &&
			LengthSqr(alignment) >= kLenEpsilonSqr)
		{
			alignment = Normalize(alignment / count);
		}

		return alignment;
	}

	Vector FlockingAgent::ComputeCohesion(const Vector_arg fleeToTargetDir, const Vector_arg debugDrawOffset) const
	{
		GAMEPLAY_ASSERT(m_state != kNatural);

		Vector cohesion = kZero;
		if (m_params->m_disableCohesion)
		{
			return cohesion;
		}

		const Vector forwardXz = Normalize(Vector(m_forward.X(), 0.0f, m_forward.Z()));
		const float visionRad = DegreesToRadians(m_params->m_cohesionVisionDeg);

		FlockingAgent* ppAgents[kMaxFlockingAgents];
		U32F numAgents;
		FindAllFlockingAgents(ppAgents, numAgents);

		const Point pos = GetPosition();
		const Point posXz = Point(pos.X(), 0.0f, pos.Z());
		const Point posDraw = pos + debugDrawOffset;

		const float minRr = m_params->m_cohesionMinRadius * m_params->m_cohesionMinRadius;
		const float maxR = m_params->m_cohesionMaxRadius * (1.0f + frand(0.0f, 0.25f));
		const float maxRr = maxR * maxR;
		if (FALSE_IN_FINAL_BUILD(g_flockingConfig.m_debugSimulation) /*&&
			m_isSelected*/)
		{
			if (m_params->m_cohesionVisionDeg < 180.0f)
			{
				DebugDrawFlatCone(pos + debugDrawOffset, forwardXz, kUnitYAxis, visionRad, maxR, kColorCyanTrans, PrimAttrib(0), kPrimDuration1FramePauseable, m_params->m_cohesionMinRadius);
			}
			else
			{
				g_prim.Draw(DebugCircle(pos + debugDrawOffset, kUnitYAxis, m_params->m_cohesionMinRadius, kColorCyanTrans), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugCircle(pos + debugDrawOffset, kUnitYAxis, maxR, kColorCyanTrans), kPrimDuration1FramePauseable);
			}
		}

		float count = 0.0f;

		for (U32F iAgent = 0; iAgent < numAgents; iAgent++)
		{
			FlockingAgent *const pOtherAgent = ppAgents[iAgent];

			if (pOtherAgent == this ||
				pOtherAgent->GetState() == kNatural || 
				pOtherAgent->GetState() == kQueueMoveTo)
			{
				continue;
			}

			if (GetState() != kFleeAlong)
			{
				const Vector otherVelocity = pOtherAgent->GetVelocity();
				if (Dot(fleeToTargetDir, otherVelocity) <= 0.0f)
				{
					continue;
				}
			}

			const Point otherPos = pOtherAgent->GetPosition();
			const Point otherPosXz = Point(otherPos.X(), 0.0f, otherPos.Z());

			const Vector toNeighborXz = otherPosXz - posXz;
			const float distXzSqr = LengthSqr(toNeighborXz);
			if (distXzSqr < minRr || 
				distXzSqr > maxRr)
			{
				continue;
			}

			if (m_params->m_cohesionVisionDeg < 180.0f)
			{
				const Vector toNeighborXzDir = SafeNormalize(toNeighborXz, kZero);
				const float d = Clamp((F32)Dot(forwardXz, toNeighborXzDir), -1.0f, 1.0f);
				if (Acos(d) > visionRad)
				{
					continue;
				}
			}

			if (FALSE_IN_FINAL_BUILD(g_flockingConfig.m_debugSimulation) /*&&
				m_isSelected*/)
			{
				const Point otherPosDraw = otherPos + debugDrawOffset;
				g_prim.Draw(DebugLine(posDraw, otherPosDraw, kColorCyanTrans), kPrimDuration1FramePauseable);
			}

			cohesion += toNeighborXz;
			count += 1.0f;
		}

		if (count > 0.0f)
		{
			cohesion = SafeNormalize(cohesion / count, kZero);
		}

		return cohesion;
	}

	Vector FlockingAgent::ComputeSeperation(float seperationNeighborRadius, const Vector_arg debugDrawOffset) const
	{
		Vector seperation = kZero;
		if (m_params->m_disableSeperation)
		{
			return seperation;
		}

		const Vector forwardXz = Normalize(Vector(m_forward.X(), 0.0f, m_forward.Z()));

		FlockingAgent* ppAgents[kMaxFlockingAgents];
		U32F numAgents;
		FindAllFlockingAgents(ppAgents, numAgents);

		const float visionRad = DegreesToRadians(m_params->m_seperationVisionDeg);

		const Point pos = GetPosition();
		const Point posXz = Point(pos.X(), 0.0f, pos.Z());
		const Point posDraw = pos + debugDrawOffset;

		const float neighborR = seperationNeighborRadius * (1.0f + frand(0.0f, 0.25f));
		const float neighborRr = neighborR * neighborR;
		if (FALSE_IN_FINAL_BUILD(g_flockingConfig.m_debugSimulation) /*&&
			m_isSelected*/)
		{
			if (m_params->m_seperationVisionDeg < 180.0f)
			{
				DebugDrawFlatCone(posDraw, forwardXz, kUnitYAxis, visionRad, neighborR, kColorMagentaTrans, PrimAttrib(0), kPrimDuration1FramePauseable);
			}
			else
			{
				g_prim.Draw(DebugCircle(posDraw, kUnitYAxis, neighborR, kColorMagentaTrans), kPrimDuration1FramePauseable);
			}
		}

		float count = 0.0f;

		for (U32F iAgent = 0; iAgent < numAgents; iAgent++)
		{
			FlockingAgent *const pOtherAgent = ppAgents[iAgent];

			if (pOtherAgent == this || 
				!pOtherAgent->IsEnabled())
			{
				continue;
			}

			const Point otherPos = pOtherAgent->GetPosition();
			const Point otherPosXz = Point(otherPos.X(), 0.0f, otherPos.Z());

			const Vector toNeighborXz = otherPosXz - posXz;
			const float distXzSqr = LengthSqr(toNeighborXz);
			if (distXzSqr > neighborRr)
			{
				continue;
			}

			if (m_params->m_seperationVisionDeg < 180.0f)
			{
				const Vector toNeighborXzDir = SafeNormalize(toNeighborXz, kZero);
				const float d = Clamp((F32)Dot(forwardXz, toNeighborXzDir), -1.0f, 1.0f);
				if (Acos(d) > visionRad)
				{
					continue;
				}
			}

			if (FALSE_IN_FINAL_BUILD(g_flockingConfig.m_debugSimulation) /*&&
				m_isSelected*/)
			{
				const Point otherPosDraw = otherPos + debugDrawOffset;
				g_prim.Draw(DebugLine(posDraw, otherPosDraw, kColorMagentaTrans), kPrimDuration1FramePauseable);
			}

			seperation -= toNeighborXz;
			count += 1.0f;
		}

		if (count > 0.0f)
		{
			seperation = SafeNormalize(seperation / count, kZero);
		}

		return seperation;
	}

	void FlockingAgent::ComputeNeighborsRvo(RvoAgentConfig rvoAgentConfig)
	{
		const int numMaxRvoNeighbors = GetRvoMaxNeighbors();

		{	// Reset every simulation step
			m_rvoCollided = false;
			m_rvoNeighbors.Reset(numMaxRvoNeighbors);
		}

		const Point pos = GetPosition();
		const Point posXz = Point(pos.X(), 0.0f, pos.Z());
		const Point posDraw = pos + GetDebugDrawOffset();

		const float r = GetParams()->m_rvoRadius;

		const float rvoNeighborR = GetRvoNeighborRadius();
		const float rvoNeighborRr = rvoNeighborR * rvoNeighborR;

		{	// Obstacles
			FlockingRvoObstacle* ppRvoObstacles[kMaxFlockingRvoObstacles];
			U32F numRvoObstacles;
			FindAllFlockingRvoObstacles(ppRvoObstacles, numRvoObstacles);

			const float rr = r * r;

			for (U32F iObstacle = 0; iObstacle < numRvoObstacles; iObstacle++)
			{
				FlockingRvoObstacle *const pObstacle = ppRvoObstacles[iObstacle];

				const Point aXz = Point(pObstacle->m_p0.X(), 0.0f, pObstacle->m_p0.Z());
				const Point bXz = Point(pObstacle->m_p1.X(), 0.0f, pObstacle->m_p1.Z());

				const Vector abXz = bXz - aXz;
				const Vector acXz = posXz - aXz;

				Point closestPtXz;
				const float c0 = Dot(acXz, abXz);

				if (c0 <= 0.0f)
				{
					closestPtXz = aXz;
				}
				else
				{
					const float c1 = Dot(abXz, abXz);

					if (c0 >= c1)
					{
						closestPtXz = bXz;
					}
					else
					{
						closestPtXz = Lerp(aXz, bXz, c0 / c1);
					}
				}

				const float distXzSqr = DistSqr(posXz, closestPtXz);
				if (distXzSqr > rvoNeighborRr)
				{
					continue;
				}

				bool toInsert = false;

				if (distXzSqr < rr)	// Insert if colliding
				{
					if (!m_rvoCollided)	// From now on we only want colliding obstacles
					{
						m_rvoCollided = true;
						m_rvoNeighbors.Clear();
					}

					toInsert = true;
				}
				else if (!m_rvoCollided)// Insert only non colliding obstacles
				{
					toInsert = true;
				}

				if (toInsert)
				{
					FlockingRvoNeighbor rvoNeighbor;
					rvoNeighbor.m_type = kObstacle;
					rvoNeighbor.m_distXzSqr = distXzSqr;
					rvoNeighbor.m_pObstacle = pObstacle;
#ifndef FINAL_BUILD
					rvoNeighbor.m_closestPt = closestPtXz;
#endif

					m_rvoNeighbors.Insert(numMaxRvoNeighbors, rvoNeighbor);
				}
			}
		}

		FlockingAgent* ppAgents[kMaxFlockingAgents];
		U32F numAgents;
		FindAllFlockingAgents(ppAgents, numAgents);

		if (rvoAgentConfig != kIgnore && 
			!m_rvoCollided)		// If colliding with obstacles, prioritize depenetration
		{	// Agents
			for (U32F iAgent = 0; iAgent < numAgents; iAgent++)
			{
				FlockingAgent *const pOtherAgent = ppAgents[iAgent];

				if (pOtherAgent == this)
				{
					continue;
				}

				const Point otherPos = pOtherAgent->GetPosition();
				const Point otherPosXz = Point(otherPos.X(), 0.0f, otherPos.Z());

				const float distXzSqr = DistSqr(posXz, otherPosXz);
				if (distXzSqr > rvoNeighborRr)
				{
					continue;
				}

				bool toInsert = false;

				const float combinedR = r + pOtherAgent->GetParams()->m_rvoRadius;
				const float combinedRr = combinedR * combinedR;

				if (distXzSqr < combinedRr)	// Insert if colliding
				{
					if (!m_rvoCollided)	// From now on we only want colliding obstacle neighbors
					{
						m_rvoCollided = true;
						m_rvoNeighbors.Clear();
					}

					toInsert = true;
				}
				else if (!m_rvoCollided)// Insert only non colliding agent neighbors
				{
					toInsert = true;
				}

				if (toInsert)
				{
					FlockingRvoNeighbor rvoNeighbor;
					rvoNeighbor.m_type = kAgent;
					rvoNeighbor.m_distXzSqr = distXzSqr;
					rvoNeighbor.m_pAgent = pOtherAgent;
#ifndef FINAL_BUILD
					rvoNeighbor.m_closestPt = otherPosXz;
#endif

					m_rvoNeighbors.Insert(numMaxRvoNeighbors, rvoNeighbor);
				}
			}
		}

#ifndef FINAL_BUILD
		// NOTE: dont draw this in DebugDraw() as DebugDraw() is called before ComputeNeighborsRvo() each frame.
		if (g_flockingConfig.m_debugSimulation && 
			g_flockingConfig.m_debugRvoNeighbors)
		{
			const Color neighborColor = m_rvoCollided ? kColorRedTrans : kColorMagentaTrans;
			g_prim.Draw(DebugCircle(posXz, kUnitYAxis, rvoNeighborR, neighborColor), kPrimDuration1FramePauseable);

			const int numNeighbors = m_rvoNeighbors.m_numNeighbors;
			for (int iNeighbor = 0; iNeighbor < numNeighbors; iNeighbor++)
			{
				const FlockingRvoNeighbor *const pNeighbor = &m_rvoNeighbors.m_pNeighbors[iNeighbor];
				GAMEPLAY_ASSERT(pNeighbor->m_type == kAgent || pNeighbor->m_type == kObstacle);

				g_prim.Draw(DebugArrow(posXz, pNeighbor->m_closestPt, neighborColor), kPrimDuration1FramePauseable);
				g_prim.Draw(DebugString(AveragePos(posXz, pNeighbor->m_closestPt), StringBuilder<8>("%d: %3.3f", iNeighbor, Sqrt(pNeighbor->m_distXzSqr)).c_str(), neighborColor, 0.5f), kPrimDuration1FramePauseable);
			}
		}
#endif
	}

	Vector FlockingAgent::ComputeNewVelocityRvo(const Vector_arg desiredVelocity, RvoAgentConfig rvoAgentConfig)
	{
		const Point pos = GetPosition();

		if (FALSE_IN_FINAL_BUILD(g_flockingConfig.m_debugSimulation))
		{
			const Point posXz = Point(pos.X(), 0.0f, pos.Z());

			const Color drawColor = m_isSelected ? kColorGreen : kColorWhiteTrans;

			g_prim.Draw(DebugCircle(pos, kUnitYAxis, GetParams()->m_rvoRadius, drawColor), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCircle(posXz, kUnitYAxis, GetParams()->m_rvoRadius, drawColor), kPrimDuration1FramePauseable);

			Vector offset = GetForward() * GetParams()->m_rvoRadius;
			g_prim.Draw(DebugLine(posXz + offset, pos + offset, drawColor));

			offset = Rotate(QuatFromAxisAngle(kUnitYAxis, 90.0f), offset);
			g_prim.Draw(DebugLine(posXz + offset, pos + offset, drawColor));

			offset = Rotate(QuatFromAxisAngle(kUnitYAxis, 90.0f), offset);
			g_prim.Draw(DebugLine(posXz + offset, pos + offset, drawColor));

			offset = Rotate(QuatFromAxisAngle(kUnitYAxis, 90.0f), offset);
			g_prim.Draw(DebugLine(posXz + offset, pos + offset, drawColor));
		}

		if (m_params->m_disableRvo)
		{
			return desiredVelocity;
		}

		Vector newVelocity = kZero;

		ComputeNeighborsRvo(rvoAgentConfig);

		const float timeStep = g_flockingConfig.m_simulationTimeStep;

		const float r = GetParams()->m_rvoRadius;

		const float maxSpeed = m_params->m_rvoMaxSpeed;
		const float m2 = 2.0f * maxSpeed;
		const float mm = maxSpeed * maxSpeed;
		
		/*if (FALSE_IN_FINAL_BUILD(g_flockingConfig.m_debugSimulation) &&
			m_isSelected)
		{
			const Point drawPos = pos + GetDebugDrawOffset();
			g_prim.Draw(DebugCircle(drawPos, kUnitYAxis, maxSpeed, kColorCyanTrans));
		}*/

		const float maxAcceleration = m_params->m_rvoMaxAcceleration;
		const float aa = maxAcceleration * maxAcceleration;

		const float w = GetRvoTimePenaltyWeight();

		float minPenalty = RVO_INFINITY;
		const int numSamples = GetRvoVelocitySamples();
		for (int iSample = 0; iSample < numSamples; iSample++)
		{
			Vector velocitySample;
			if (iSample == 0)	// Make sure the desired velocity is always considered
			{
				velocitySample = desiredVelocity;
			}
			else	// Randomly sample velocity within the circle centered at current pos with radius maxSpeed.
			{
				do
				{
					const float x = frand(-maxSpeed, m2);	// [-maxSpeed, maxSpeed)
					const float z = frand(-maxSpeed, m2);

					velocitySample = Vector(x, 0.0f, z);
				}
				while (LengthSqr(velocitySample) > mm);
			}

			/*if (FALSE_IN_FINAL_BUILD(g_flockingConfig.m_debugSimulation) &&
				m_isSelected)
			{
				const Point drawPos = pos + GetDebugDrawOffset();
				g_prim.Draw(DebugArrow(drawPos, drawPos + velocitySample, kColorCyanTrans));
			}*/

			const float distPenalty = m_rvoCollided ?
									0.0f :	// If already colliding ignore dist penalty, prioritize depenetration
									static_cast<float>(Length(desiredVelocity - velocitySample));

			float tcSmallest = RVO_INFINITY;	// Smallest time to collide among all neighbors

			const int numNeighbors = m_rvoNeighbors.m_numNeighbors;
			for (int iNeighbor = 0; iNeighbor < numNeighbors; iNeighbor++)
			{
				float tcNeighbor = RVO_INFINITY;

				const FlockingRvoNeighbor *const pNeighbor = &m_rvoNeighbors.m_pNeighbors[iNeighbor];
				if (pNeighbor->m_type == kAgent)
				{
					const FlockingAgent *const pAgentNeighbor = pNeighbor->m_pAgent;
					
					const Vector vAb = 2.0f * velocitySample - m_velocity - pAgentNeighbor->m_velocity;	// Relative velocity
					const float t = TimeToCollideRvoRef(pos, vAb, pAgentNeighbor->GetPosition(), pAgentNeighbor->GetParams()->m_rvoRadius + r, m_rvoCollided);

					tcNeighbor = TimeToCollideAgentRvoRef(t, timeStep, velocitySample, mm, m_rvoCollided);
				}
				else
				{
					const FlockingRvoObstacle *const pObstacle = pNeighbor->m_pObstacle;
					const Vector n = pObstacle->m_normalXz;
					const Vector nOffset = n * r;

					const Point p0Xz = Point(pObstacle->m_p0.X(), 0.0f, pObstacle->m_p0.Z());
					const Point p1Xz = Point(pObstacle->m_p1.X(), 0.0f, pObstacle->m_p1.Z());

					const float ta = TimeToCollideRvoRef(pos, velocitySample, p0Xz, r, m_rvoCollided);
					const float tb = TimeToCollideRvoRef(pos, velocitySample, p1Xz, r, m_rvoCollided);
					const float tn = TimeToCollideRvo2Ref(pos, velocitySample, p0Xz + nOffset, p1Xz + nOffset, m_rvoCollided);
					const float tnn = TimeToCollideRvo2Ref(pos, velocitySample, p0Xz - nOffset, p1Xz - nOffset, m_rvoCollided);

					tcNeighbor = TimeToCollideObstacleRvoRef(ta, tb, tn, tnn, timeStep, velocitySample, mm, aa, m_rvoCollided);
				}

				if (tcNeighbor < tcSmallest)
				{
					tcSmallest = tcNeighbor;

					const float penalty = w / tcNeighbor + distPenalty;
					if (penalty >= minPenalty)
					{
						break;	// Because neighbors are sorted based on distances, so no better penalty can be obtained for this velocity sample
					}
				}
			}

			const float penalty = w / tcSmallest + distPenalty;
			if (penalty < minPenalty)
			{
				minPenalty = penalty;
				newVelocity = velocitySample;
			}
		}

		return newVelocity;
	}

	void FlockingAgent::DebugDraw()
	{
		/*{
			const float speed = Length(m_velocity);
			MsgConPauseable("  %s%d: spd: %3.3f\n",
							GetTextColorString(m_isSelected ? kTextColorGreen : kTextColorNormal), 
							m_idx,
							speed);
		}*/

		const Point pos = GetPosition();
		const Color drawColor = m_isSelected ? kColorGreen : kColorWhiteTrans;
		g_prim.Draw(DebugCross(pos, 0.15f, 1.0f, drawColor, PrimAttrib(kPrimDisableDepthTest), StringBuilder<32>("%d: %s", m_idx, GetStateName(this)).c_str(), 0.55f), kPrimDuration1FramePauseable);
	}
}
