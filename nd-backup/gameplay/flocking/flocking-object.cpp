/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "ndlib/profiling/profiling.h"

#include "corelib/system/atomic-lock.h"
#include "corelib/util/bit-array.h"

#include "ndlib/anim/nd-anim-util.h"
#include "gamelib/ndphys/simple-probe.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/nd-game-info.h"
#include "gamelib/level/level-mgr.h"
#include "gamelib/gameplay/nd-simple-object.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-simple-instance.h"
#include "gamelib/gameplay/flocking/flocking-object.h"

// For scripting
#include "gamelib/script/nd-script-arg-iterator.h"
#include "ndlib/script/script-manager.h"
#include "gamelib/state-script/ss-manager.h"
#include "gamelib/state-script/ss-context.h"
#include "gamelib/state-script/ss-instance.h"
#include "gamelib/scriptx/h/flocking-defines.h"
#include "gamelib/scriptx/h/animal-behavior-defines.h"

bool g_debugFlock = false;
I32 g_debugFlockIndex = -1;

class FlockingRepulsor : public NdLocatableObject
{
	typedef NdLocatableObject ParentClass;

public:
	virtual Err Init(const ProcessSpawnInfo& spawn) override
	{
		m_isPlane = spawn.GetData(SID("is-plane"), 0) == 1;
		m_distMin = spawn.GetData(SID("min-distance"), 0.0f);
		m_distMax = spawn.GetData(SID("max-distance"), 10.0f);
		m_repulsorStrength = spawn.GetData(SID("strength"), 10.0f);
		RegisterRepulsor(this);
		return ParentClass::Init(spawn);
	}

	virtual void OnKillProcess() override
	{
		UnregisterRepulsor(this);
		ParentClass::OnKillProcess();
	}

	Vector GetRepulsion(Point_arg pos) const
	{
		if (m_isPlane)
		{
			Plane p(GetTranslation(), GetLocalY(GetRotation()));
			F32 dist = p.Dist(pos);
			F32 tt = LerpScale(m_distMin, m_distMax, 1.0f, 0.0f, dist);
			tt *= tt;
			return GetLocalY(GetRotation()) * tt * m_repulsorStrength;
		}
		else
		{
			Scalar dist;
			Vector dir = SafeNormalize(pos - GetTranslation(), Vector(kZero), dist);

			F32 tt = LerpScale(m_distMin, m_distMax, 1.0f, 0.0f, (F32)dist);
			tt *= tt;
			return dir * tt * m_repulsorStrength;
		}
	}

	bool m_isPlane;
	float m_distMin;
	float m_distMax;
	float m_repulsorStrength;
};

PROCESS_REGISTER(FlockingRepulsor, NdLocatableObject);

class FlockManager : public FlockingInterface
{
	static const I32 kMaxBoids = 512;

	struct Boid
	{
		MutableNdGameObjectHandle m_hBoid;
		StringId64 m_groupId;
		StringId64 m_splineId;
		F32 m_arcLength;
		bool m_broadcastEvent;
		Point m_boidPos;
		Vector m_boidVel;
	};

	Boid m_boids[kMaxBoids];
	//I32 m_numBoids;

	ExternalBitArray m_validBoidsMask;
	U64 m_boidMaskBuffer[kMaxBoids / 64];

	TypedProcessHandle<FlockingRepulsor> m_hRepulsors[4];

	Vector CohesionNoLock(const Boid& thisBoid, Point_arg pos, Vector_arg vel, F32 cohesionFactor) const
	{
		Point centerOfMass(kZero);

		F32 count = 0.0f;
		for (int i = m_validBoidsMask.FindFirstSetBit(); i != ~0ULL; i = m_validBoidsMask.FindNextSetBit(i))
		{
			const Boid &otherBoid = m_boids[i];
			if (otherBoid.m_groupId != thisBoid.m_groupId)
				continue;

			count += 1.0f;
			centerOfMass += (otherBoid.m_boidPos - Point(0, 0, 0));
		}

		if (count > 0.0f)
		{
			Vector originToCenter = centerOfMass - Point(0, 0, 0);
			originToCenter *= (1.0f / count);
			centerOfMass = Point(0, 0, 0) + originToCenter;
		}

		Vector toCenter = centerOfMass - pos;
		return toCenter * cohesionFactor;
	}

	Vector SkyAdjust(const Boid& thisBoid, Point_arg pos, Vector_arg vel, F32 skyHeight, F32 skyRadius, F32 skyThickness) const
	{
		Vector toMagnet(kZero);

		const EntitySpawner* pSpawner = EngineComponents::GetLevelMgr()->LookupEntitySpawnerByBareNameId(thisBoid.m_groupId);
		if (!pSpawner)
			return Vector(kZero);

		const Point refPt = pSpawner->GetWorldSpaceLocator().GetTranslation() + Vector(0,skyHeight,0);

		Vector toRef = refPt - pos;
		F32 yDist = toRef.Y();
		Vector toRefFlat = toRef;
		toRefFlat.SetY(0.0f);

		F32 xzDist = LengthXz(toRefFlat);

		F32 dist = Dist(refPt, pos);
		if (xzDist > skyRadius * 0.5f)
		{
			F32 strength = LerpScale(skyRadius * 0.5f, skyRadius, 0.0f, 15.0f, xzDist);
			toMagnet = SafeNormalize(toRefFlat, Vector(kZero)) * strength;
		}

		if (Abs(yDist) > skyThickness * 0.5f)
		{
			F32 strength = LerpScale(skyThickness * 0.5f, skyThickness, 0.0f, 15.0f, Abs(yDist));
			if (Sign(vel.Y()) == Sign(yDist))
			{
				strength = 0.0f;
			}
			toMagnet.SetY(Sign(yDist) * strength);
		}

		return toMagnet;
	}

	Vector SplineCohesionNoLock(const Boid& thisBoid, Point_arg pos, Vector_arg vel, F32 splineCohesionFactor, bool debugDraw) const
	{
		Point centerOfMass(kZero);
		F32 arcLengthCenter = thisBoid.m_arcLength;
		CatmullRom *pSpline = g_splineManager.FindByName(thisBoid.m_splineId);

		if (!pSpline)
			return Vector(kZero);

		F32 totalArcLength = pSpline->GetTotalArcLength();

		F32 count = 0.0f;
		for (int i = m_validBoidsMask.FindFirstSetBit(); i != ~0ULL; i = m_validBoidsMask.FindNextSetBit(i))
		{
			const Boid &otherBoid = m_boids[i];
			if (otherBoid.m_groupId != thisBoid.m_groupId)
				continue;

			F32 arcLenDiff = otherBoid.m_arcLength - thisBoid.m_arcLength;
			if (pSpline->IsLooped())
			{
				if (Abs(arcLenDiff) > (totalArcLength * 0.5f))
				{
					if (arcLenDiff < 0.0f)
					{
						arcLenDiff = arcLenDiff + totalArcLength;
					}
					else if (arcLenDiff > 0.0f)
					{
						arcLenDiff = arcLenDiff - totalArcLength;
					}
				}
			}
			count += 1.0f;

			arcLengthCenter += arcLenDiff;
			/*
			Locator splineSpace;
			splineSpace.SetTranslation(pSpline->EvaluatePointAtArcLength(m_arcLengths[i]));
			splineSpace.SetRotation(QuatFromLookAt(pSpline->EvaluateTangentAtArcLength(m_arcLengths[i]), kUnitYAxis));
			Point splineSpacePos = splineSpace.UntransformPoint(m_hBoids[i].ToProcess()->GetTranslation());

			centerOfMass += (splineSpacePos - Point(0,0,0))/m_numBoids;
			*/
		}

		if (count > 0.0f)
		{
			arcLengthCenter /= count;
		}
		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			//MsgCon("Center of Mass Z: %f\n", (F32)centerOfMass.Z());
			Point targetPoint = pSpline->EvaluatePointAtArcLength(arcLengthCenter);
			g_prim.Draw(DebugSphere(targetPoint, 0.1f, kColorYellow));
		}

		F32 arcLengthDiff = arcLengthCenter - thisBoid.m_arcLength;
		if (Abs(arcLengthDiff) > (totalArcLength * 0.5f))
		{
			arcLengthDiff = -(thisBoid.m_arcLength + totalArcLength - arcLengthCenter);
		}

		centerOfMass.SetZ(centerOfMass.Z() + arcLengthDiff);

		Locator mySplineSpace;
		mySplineSpace.SetTranslation(pSpline->EvaluatePointAtArcLength(thisBoid.m_arcLength));
		mySplineSpace.SetRotation(QuatFromLookAt(pSpline->EvaluateTangentAtArcLength(thisBoid.m_arcLength), kUnitYAxis));
		centerOfMass = mySplineSpace.TransformPoint(centerOfMass);
		Vector toCenter = centerOfMass - pos;

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			g_prim.Draw(DebugLine(pos, centerOfMass, kColorYellow, kColorYellow));
		}

		return toCenter * splineCohesionFactor;
	}

	Vector SeparationNoLock(const Boid& thisBoid, Point_arg pos, Vector_arg vel, F32 separationDist, F32 repelFactor, F32 playerSeparationDist, F32 playerRepelFactor)
	{
		Vector repel(kZero);

		for (int i = m_validBoidsMask.FindFirstSetBit(); i != ~0ULL; i = m_validBoidsMask.FindNextSetBit(i))
		{
			const Boid &otherBoid = m_boids[i];
			if (otherBoid.m_groupId != thisBoid.m_groupId)
				continue;

			Point birdPos = otherBoid.m_boidPos;
			F32 dist = Dist(birdPos, pos);
			if (dist < separationDist && dist > 0.0f)
			{
				repel -= Normalize(birdPos - pos) / dist * (separationDist - dist);
			}
		}

		repel = repel * repelFactor;

		if (EngineComponents::GetNdGameInfo()->GetPlayerGameObject())
		{
			Point playerPos = EngineComponents::GetNdGameInfo()->GetPlayerGameObject()->GetTranslation();
			F32 dist = Dist(playerPos, pos);
			dist = LerpScale(playerSeparationDist * 0.5f, playerSeparationDist, 0.01f, playerSeparationDist, dist);
			if (dist < playerSeparationDist && dist > 0.0f)
			{
				repel -= (Normalize(playerPos - pos) / dist * (separationDist - dist)) * playerRepelFactor;
			}
		}

		return repel;
	}

	Vector AlignmentNoLock(const Boid& thisBoid, Point pos, Vector vel, F32 alignFactor)
	{
		Vector averageVel(kZero);
		F32 count = 0.0f;

		for (int i = m_validBoidsMask.FindFirstSetBit(); i != ~0ULL; i = m_validBoidsMask.FindNextSetBit(i))
		{
			const Boid &otherBoid = m_boids[i];
			if (otherBoid.m_groupId != thisBoid.m_groupId)
				continue;

			count += 1.0f;
			averageVel += otherBoid.m_boidVel;
		}

		if (count > 0.0f)
			averageVel /= count;

		return averageVel * alignFactor;
	}

	Vector SplineFollow(Boid& thisBoid, Point pos, Vector vel, F32 splineFactor, bool debugDraw)
	{
		const CatmullRom *pSpline = g_splineManager.FindByName(thisBoid.m_splineId);
		if (!pSpline)
			return Vector(kZero);

		F32 distTravelled = Length(vel * GetProcessDeltaTime());
		CatmullRom::LocalParameter oldParam = pSpline->ArcLengthToLocalParam(thisBoid.m_arcLength);
		I32 segment = oldParam.m_iSegment;
		F32 arcLength = pSpline->FindArcLengthClosestToPoint(pos, &segment);
		arcLength += distTravelled;
		if (arcLength < (thisBoid.m_arcLength + distTravelled * 0.25f))
		{
			arcLength = thisBoid.m_arcLength + distTravelled * 0.25f;
		}
		else if (arcLength >(thisBoid.m_arcLength + distTravelled * 1.25f))
		{
			arcLength = thisBoid.m_arcLength + distTravelled * 1.25f;
		}

		if (arcLength > pSpline->GetTotalArcLength())
		{
			if (pSpline->IsLooped())
			{
				arcLength -= pSpline->GetTotalArcLength();
			}
			else
			{
				arcLength = pSpline->GetTotalArcLength();
				if (!thisBoid.m_broadcastEvent)
				{
					PostEvent(SID("reached-spline-end"), thisBoid.m_hBoid);
					thisBoid.m_broadcastEvent = true;
				}
			}
		}

		thisBoid.m_arcLength = arcLength;

		CatmullRom::LocalParameter param = pSpline->ArcLengthToLocalParam(arcLength);

		Point splinePos;
		Vector deriv, tangent;
		pSpline->EvaluateLocal(param.m_iSegment, param.m_u, splinePos, deriv, tangent);

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			g_prim.Draw(DebugCross(splinePos, 0.2f, 0.1f));
			g_prim.Draw(DebugLine(splinePos, tangent, kColorWhite, kColorRed));
			g_prim.Draw(DebugLine(splinePos, pos, kColorGreen, kColorGreen));
		}

		return tangent * splineFactor;
	}

	Vector SplineAttract(Boid& thisBoid, Point_arg pos, Vector_arg vel, F32 maxSplineDist, F32 splineAttractFactor)
	{
		CatmullRom *pSpline = g_splineManager.FindByName(thisBoid.m_splineId);
		if (!pSpline)
			return Vector(kZero);

		//const Point targetPos = pSpline->FindClosestPointOnSpline(pos);
		const Point targetPos = pSpline->EvaluatePointAtArcLength(thisBoid.m_arcLength);

		Vector toSpline = targetPos - pos;
		Scalar dist;
		toSpline = SafeNormalize(toSpline, toSpline, dist);

		GAMEPLAY_ASSERT(IsFinite(toSpline));
		/*F32 mult = dist / m_maxSplineDist;
		return toSpline * mult * m_splineAttractFactor;*/

		if (dist > maxSplineDist * 0.5f)
		{
			GAMEPLAY_ASSERT(maxSplineDist > 0.0f);
			F32 mult = (dist - (maxSplineDist * 0.5f)) / (maxSplineDist * 0.5f);
			return toSpline * mult * splineAttractFactor;
			/*
			F32 dot = Dot(toSpline, vel);
			if (dot < 0.0f)
			{
				return toSpline * -dot * mult;
			}
			*/
		}

		return Vector(kZero);
	}

	bool m_init;

	mutable NdAtomicLock m_boidLock;

public:
	void Init()
	{
		m_init = true;
	}

	FlockManager()
		: m_boidLock()
	{
		m_init = false;
		//m_numBoids = 0;

		ALWAYS_ASSERT(ExternalBitArray::DetermineNumBlocks(kMaxBoids) * sizeof(U64) == sizeof(m_boidMaskBuffer));
		m_validBoidsMask.Init(kMaxBoids, m_boidMaskBuffer);
	}

	virtual void AddBoid(NdGameObject *pBoid, StringId64 groupOrSplineId, bool isSpline) override
	{
		if (!m_init)
			Init();

		AtomicLockJanitor jj(&m_boidLock, FILE_LINE_FUNC);

		I32 numBoids = GetNumBoids();
		if (numBoids >= kMaxBoids)
		{
			MsgConPersistent("Too many birds in the air at once! Past limit of %d\n", kMaxBoids);
			return;
		}

		I32 newIndex = m_validBoidsMask.FindFirstClearBit();
		Boid &thisBoid = m_boids[newIndex];
		m_validBoidsMask.SetBit(newIndex);

		if (isSpline)
		{
			const CatmullRom *pSpline = g_splineManager.FindByName(groupOrSplineId);
			if (!pSpline)
				return;

			F32 arcLength = pSpline->FindArcLengthClosestToPoint(pBoid->GetTranslation());
			thisBoid.m_arcLength = arcLength;
			thisBoid.m_splineId = groupOrSplineId;
		}
		else
		{
			thisBoid.m_splineId = INVALID_STRING_ID_64;
		}

		thisBoid.m_groupId = groupOrSplineId;
		thisBoid.m_broadcastEvent = false;
		thisBoid.m_boidPos = pBoid->GetTranslation();
		thisBoid.m_boidVel = pBoid->GetVelocityWs();
		thisBoid.m_hBoid = pBoid;
		//m_numBoids++;

	}

	virtual void RemoveBoid(NdGameObject *pBoid) override
	{
		AtomicLockJanitor jj(&m_boidLock, FILE_LINE_FUNC);

		I32 boidIndex = FindBoidIndexNoLock(pBoid);
		if (boidIndex >= 0)
		{
			//m_boids[boidIndex].Clear();
			m_validBoidsMask.ClearBit(boidIndex);
			//m_numBoids--;
		}
	}

	virtual void UpdatePosOnSpline(const NdGameObject* pBoid, Point_arg newPos, Vector_arg newVel) override
	{
		AtomicLockJanitor jj(&m_boidLock, FILE_LINE_FUNC);

		I32 boidIndex = FindBoidIndexNoLock(pBoid);
		if (boidIndex < 0)
			return;

		Boid &thisBoid = m_boids[boidIndex];

		const CatmullRom *pSpline = g_splineManager.FindByName(thisBoid.m_splineId);
		if (!pSpline)
			return;

		F32 arcLength = pSpline->FindArcLengthClosestToPoint(newPos);
		thisBoid.m_arcLength = arcLength;
		thisBoid.m_boidPos = newPos;
		thisBoid.m_boidVel = newVel;
	}

	void AddRepulsor(Process* pRep)
	{
		for (int i = 0; i < 4; i++)
		{
			if (!m_hRepulsors[i].HandleValid())
			{
				m_hRepulsors[i] = (FlockingRepulsor*)pRep;
				return;
			}
		}

		GAMEPLAY_ASSERTF(false, ("Cannot have more than 4 repulsors!"));
	}

	void RemoveRepulsor(Process* pRep)
	{
		for (int i = 0; i < 4; i++)
		{
			if (m_hRepulsors[i].ToProcess() == pRep)
			{
				m_hRepulsors[i] = nullptr;
				return;
			}
		}

		GAMEPLAY_ASSERTF(false, ("Cannot find repulsor!"));
	}

	I32 FindBoidIndexNoLock(const NdGameObject* pBoid) const
	{
		for (int i = m_validBoidsMask.FindFirstSetBit(); i != ~0ULL; i = m_validBoidsMask.FindNextSetBit(i))
		{
			if (pBoid == m_boids[i].m_hBoid.ToProcess())
				return i;
		}
		return -1;
	}

	I32 FindBoidIndex(const NdGameObject* pBoid) const
	{
		AtomicLockJanitor jj(&m_boidLock, FILE_LINE_FUNC);
		return FindBoidIndexNoLock(pBoid);
	}

	I32 GetNumBoids() const
	{
		return m_validBoidsMask.CountSetBits();
	}

	virtual Vector GetAccelForPosition(const NdGameObject* pSelf, Point_arg pos, Vector_arg vel, const DC::FlockingVars *pVars, const DC::AnimalBehaviorSettings* pAnimalSettings) override
	{
		I32 boidNum = FindBoidIndex(pSelf);
		if (boidNum == -1)
			return Vector(kZero);

		Boid &thisBoid = m_boids[boidNum];

		thisBoid.m_boidPos = pos;
		thisBoid.m_boidVel = vel;

		const CatmullRom *pSpline = g_splineManager.FindByName(thisBoid.m_splineId);

		StringId64 settingsId = pSpline ? pSpline->GetTagData<StringId64>(SID("settings"), INVALID_STRING_ID_64):INVALID_STRING_ID_64;

		if (settingsId !=INVALID_STRING_ID_64 || pVars)
		{
			if (!pVars)
				pVars = ScriptManager::Lookup<DC::FlockingVars>(settingsId, nullptr);

			if (!pVars)
				return Vector(kZero);

			ALWAYS_ASSERT(pVars);
		}
		else
		{
			MsgScriptErr("Trying to process a flocking object with invalid settings!\n");
			return Vector(kZero);
		}

		Vector splineCohesion(kZero);
		Vector splineFollow(kZero);
		Vector splineAttract(kZero);
		Vector skyAdjust(kZero);

		Vector cohesion = CohesionNoLock(thisBoid, pos, vel, pVars->m_cohesionFactor);
		Vector separation = SeparationNoLock(thisBoid, pos, vel, pVars->m_separationDist, pVars->m_repelFactor, pVars->m_playerSeparationDist, pVars->m_playerRepelFactor);
		Vector alignment = AlignmentNoLock(thisBoid, pos, vel, pVars->m_alignFactor);

		bool debugDraw = g_debugFlock && (g_debugFlockIndex == boidNum || g_debugFlockIndex == -1);

		if (thisBoid.m_splineId != INVALID_STRING_ID_64)
		{ 
			splineCohesion = (pVars->m_splineCohesionFactor > 0.0f) ? SplineCohesionNoLock(thisBoid, pos, vel, pVars->m_splineCohesionFactor, debugDraw) : Vector(kZero);
			splineFollow = SplineFollow(thisBoid, pos, vel, pVars->m_splineFactor, debugDraw);
			splineAttract = SplineAttract(thisBoid, pos, vel, pVars->m_maxSplineDist, pVars->m_splineAttractFactor);
		}
		else if (pAnimalSettings)
		{
			skyAdjust = SkyAdjust(thisBoid, pos, vel, pAnimalSettings->m_avoidHumanHeight, pAnimalSettings->m_avoidHumanRadius, pAnimalSettings->m_skyThickness);
		}

		Vector repulsorAdjust(kZero);
		for (int i = 0; i < 4; i++)
		{
			if (const FlockingRepulsor* pRep = m_hRepulsors[i].ToProcess())
			{
				repulsorAdjust += pRep->GetRepulsion(pos);
			}
		}

		Vector velDir = SafeNormalize(vel, vel);
		F32 velSquared = Dot(vel, vel);

		Vector total = cohesion + splineCohesion + separation + alignment + splineFollow + splineAttract + skyAdjust + -pVars->m_drag * velSquared * velDir + repulsorAdjust;
		if (Length(total) > pVars->m_maxAccel)
		{
			total *= pVars->m_maxAccel / Length(total);
		}

		if (pVars->m_noVerticalMovement)
		{
			total.SetY(0.0f);
		}

		GAMEPLAY_ASSERT(IsFinite(total));

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			g_prim.Draw(DebugString(pos, pSelf->GetName()));
			g_prim.Draw(DebugLine(pos, total, kColorCyan, kColorCyan));
			g_prim.Draw(DebugLine(pos, cohesion, kColorYellow, kColorBlack));
			g_prim.Draw(DebugLine(pos, splineCohesion, kColorOrange, kColorWhite));
			g_prim.Draw(DebugLine(pos, splineFollow, kColorRed, kColorCyan));
		}

		return total;// + (splineAttract / GetProcessDeltaTime());
	}

	virtual Vector CalcTakeoffDirection(const NdGameObject* pSelf, Point_arg selfPos) const override
	{
		I32 boidNum = FindBoidIndex(pSelf);
		if (boidNum == -1)
			return Vector(kZero);

		const Boid &thisBoid = m_boids[boidNum];

		const CatmullRom *pSpline = g_splineManager.FindByName(thisBoid.m_splineId);
		if (!pSpline)
			return Vector(kZero);

		const float gparam = pSpline->FindGlobalParamClosestToPoint(selfPos);
		const Vector direction = pSpline->EvaluateTangentGlobal(gparam);
		return direction;
	}
};

FlockManager g_flockManager;

//----------------------------------------------------------------------------------------//
// Flocking Controller.
//----------------------------------------------------------------------------------------//
bool FlockingCtrl::Init(StringId64 settingsId)
{
	m_velocity = Vector(kZero);
	m_accel = Vector(kZero);
	m_flocking = false;

	m_flockingVars = ScriptPointer<DC::FlockingVars>(settingsId, SID("flocking"));
	if (!m_flockingVars)
		return false;

	return true;
}

//----------------------------------------------------------------------------------------//
float FlockingCtrl::GetMaxSpeed() const
{
	return m_flockingVars->m_maxVelocity;
}

//----------------------------------------------------------------------------------------//
void FlockingCtrl::CalculateAccelAndVelocity(NdGameObject* pSelf, const DC::AnimalBehaviorSettings* pAnimalSettings)
{
	if (!m_flocking)
		return;

	m_accel = g_flockManager.GetAccelForPosition(pSelf, pSelf->GetTranslation(), m_velocity, m_flockingVars, pAnimalSettings);

	F32 maxVelocity = m_flockingVars->m_maxVelocity;

	m_velocity += m_accel * GetProcessDeltaTime();

	if (Length(m_velocity) > maxVelocity)
	{
		m_velocity *= maxVelocity / Length(m_velocity);
	}

	GAMEPLAY_ASSERT(IsFinite(m_velocity));
}

//----------------------------------------------------------------------------------------//
Vector FlockingCtrl::CalcTakeoffDirection(const NdGameObject* pSelf, Point_arg selfPos) const
{
	Vector direction = g_flockManager.CalcTakeoffDirection(pSelf, selfPos);
	GAMEPLAY_ASSERT(IsFinite(direction));
	return direction;
}

//----------------------------------------------------------------------------------------//
void FlockingCtrl::UpdatePosOnSpline(const NdGameObject* pSelf, Point_arg newPos, Vector_arg newVel)
{
	g_flockManager.UpdatePosOnSpline(pSelf, newPos, newVel);
}

//----------------------------------------------------------------------------------------//
Err FlockingObject::Init(const ProcessSpawnInfo& spawn)
{
	Err err = ParentClass::Init(spawn);
	StringId64 settingsId = spawn.GetData<StringId64>(SID("flocking-settings"), SID("*flocking-default*"));

	if (!m_flockingCtrl.Init(settingsId))
		return Err::kErrBadData;

	return err;
}

void FlockingObject::OnKillProcess()
{
	ParentClass::OnKillProcess();
	g_flockManager.RemoveBoid(this);
}

///------------------------------------------------------------------------------------------///
void FlockingObject::StartFlocking()
{
	m_flockingCtrl.Enable();
}

///------------------------------------------------------------------------------------------///
void FlockingObject::UpdateFlocking()
{
	m_flockingCtrl.CalculateAccelAndVelocity(this);
	AdjustTranslation(m_flockingCtrl.GetVelocity() * GetProcessDeltaTime());
}

///------------------------------------------------------------------------------------------///
void FlockingObject::UpdatePosOnSpline(Point_arg newPos, Vector_arg newVel)
{
	m_flockingCtrl.UpdatePosOnSpline(this, newPos, newVel);
}

///------------------------------------------------------------------------------------------///
void FlockingObject::EventHandler(Event& event)
{
	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("start-flocking"):
		StartFlocking();
		break;
	}

	ParentClass::EventHandler(event);
}

void FlockingObject::PostAnimUpdate_Async()
{
	ParentClass::PostAnimUpdate_Async();
	if (!m_flockingCtrl.IsEnabled())
		return;

	UpdateFlocking();
}

PROCESS_REGISTER(FlockingObject, NdSimpleObject);

void AddFlockingBoid(NdGameObject *pBoid, StringId64 refPoint)
{
	g_flockManager.AddBoid(pBoid, refPoint, false);
}

void RemoveFlockingBoid(NdGameObject *pBoid)
{
	g_flockManager.RemoveBoid(pBoid);
}

void RegisterRepulsor(Process* pRepulsor)
{
	g_flockManager.AddRepulsor(pRepulsor);
}

void UnregisterRepulsor(Process* pRepulsor)
{
	g_flockManager.RemoveRepulsor(pRepulsor);
}

SCRIPT_FUNC("flocking-add", DcFlockingAdd)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject *pBoid = args.NextGameObject();
	StringId64 flockSpline = args.NextStringId();

	if (pBoid)
	{
		if (!pBoid->IsKindOf(SID("FlockingObject")))
		{
			MsgScriptErr("flocking-add called on non-flocking object %s\n", pBoid->GetName());
			return ScriptValue(0);
		}
		CatmullRom *pSpline = g_splineManager.FindByName(flockSpline);
		if (!pSpline)
		{
			MsgScriptErr("Cannot find flocking spline %s\n", DevKitOnly_StringIdToString(flockSpline));
			return ScriptValue(0);
		}

		SendEvent(SID("start-flocking"), pBoid);
		g_flockManager.AddBoid(pBoid, flockSpline, true);
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("is-bird?", DcIsBirdP)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject *pBoid = args.NextGameObject();

	if (pBoid)
	{
		if (pBoid->IsKindOf(SID("FlockingBird")))
		{
			return ScriptValue(true);
		}
	}

	return ScriptValue(false);
}