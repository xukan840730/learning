/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/frame-params.h"
#include "ndlib/ndphys/pat.h"
#include "ndlib/render/ngen/mesh-raycaster-job.h"

#include "gamelib/gameplay/character.h"

/// --------------------------------------------------------------------------------------------------------------- ///
#ifndef ASSERT_LEG_INDEX_VALID
#define ASSERT_LEG_INDEX_VALID(legIndex)                                                                               \
	do                                                                                                                 \
	{                                                                                                                  \
		ANIM_ASSERTF(legIndex >= 0, ("invalid legIndex: %d", legIndex));                                               \
		int maxIndex = kMaxNumRays;                                                                                    \
		ANIM_ASSERTF(legIndex < maxIndex,                                                                              \
					 ("invalid legIndex: %d (max %d) [quadruped: %s kickPredictiveRays: %s]",                          \
					  legIndex,                                                                                        \
					  maxIndex,                                                                                        \
					  m_isQuadruped ? "YES" : "NO",                                                                    \
					  m_usePredictiveRays ? "YES" : "NO"));                                                            \
	} while (false)
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
class LegRaycaster;

/// --------------------------------------------------------------------------------------------------------------- ///
struct LegRayCastInfo
{
	LegRayCastInfo();

	Vector m_dir;
	float m_length;
	float m_probeStartAdjust;
	bool m_projectToAlign;
	bool m_useLengthForCollisionCast;
	float m_capsuleRadius = -1.0f;
	float m_probeRadius;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct LegRayCollisionProbe
{
	Point m_start;
	Point m_end;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class LegRaycaster
{
public:
	enum Mode
	{
		kModeDefault,
		kModeEdge,
		kModeWallRope,
		kModeInvalid,
	};

	static const char* GetModeStr(Mode m)
	{
		switch (m)
		{
		case kModeDefault:	return "default";
		case kModeEdge:		return "edge";
		case kModeWallRope:	return "wall-rope";
		}

		return "<invalid>";
	}

	struct Setup
	{
		Point m_posPs = kInvalidPoint;
		uintptr_t m_userData = 0;
		bool m_disabled = true;
	};

	struct Results
	{
		Results()
		{
			m_point.SetInvalid();
		}

		BoundFrame m_point;
		Point m_slatPoint = kInvalidPoint;

		RigidBodyHandle m_hRayHitBody = nullptr;
		TimeFrame m_time		 = TimeFrameNegInfinity();
		I64 m_surfaceFrameNumber = -1;
		uintptr_t m_userData	 = 0;
		U32 m_hitProcessId		 = 0;
		Pat m_pat = Pat(0);

		Mode m_mode = kModeInvalid;

		bool m_valid	 = false;
		bool m_hitWater	 = false;
		bool m_hitGround = false;
		bool m_meshRaycastResult = false;
		bool m_hasSlatPoint		 = false;
	};

	class AudioGround
	{
		friend class LegRaycaster;

	public:
		void Reset()
		{
			m_probeValid = false;
			m_numContacts = 0;
		}

		float GuessPlatformHeight() const
		{
			if (!m_probeValid || m_numContacts <= 1)
			{
				return 0.0f;
			}

			//return m_contacts[0].m_contactPoint.Y() - m_contacts[1].m_contactPoint.Y();
			return m_contacts[0].Y() - m_contacts[1].Y();
		}

	public:
		int m_numContacts;
		//ShapeCastContact m_contacts[2];
		Vector m_normals[2];
		Point m_contacts[2];

	private:
		bool m_probeValid;
	};

	class PredictOnStairs
	{
		friend class LegRaycaster;

	public:
		enum Result
		{
			kInvalid, 
			kHitStairs, 
			kHitGround
		};

		PredictOnStairs()
		{
			Reset();
		}

		void TryCommitResult(TimeFrame curTime, Result result, Vector normalWs);

		void Reset()
		{
			m_result = kInvalid;
			m_normalWs = kUnitYAxis;

			m_startFlipTime = Seconds(-1.0f);	// Not flipping
			
			m_probeValid = false;
		}

	public:
		Result m_result;
		Vector m_normalWs;

	private:
		static bool IsResultDifferentEnough(Result result0,
											const Vector_arg normalWs0,
											Result result1,
											const Vector_arg normalWs1);

	private:
		Result m_startFlipResult;
		Vector m_startFlipNormalWs;
		TimeFrame m_startFlipTime;

		bool m_probeValid;
	};

	static const Results kInvalidResults;

	LegRaycaster();

	void SetCharacter(const Character* pChar, bool forceQuadruped = false);

	void Init();
	void EnterNewParentSpace(const Transform& matOldToNew, const Locator& oldParentSpace, const Locator& newParentSpace);

	bool IsFootprintSurface();

	bool IsQuadruped() const { return m_isQuadruped; }

	void UpdateRayCastMode();

	Mode GetRayCastMode() const	{ return m_raycastMode; }
	bool IsValid() const		{ return m_oneRaycastDone; }

	void SetProbePointsPs(Point_arg leftPosPs, Point_arg rightPosPs);
	void GetProbePointsPs(Point& leftPosPsOut, Point& rightPosPsOut) const;

	void SetProbeUserData(const uintptr_t* pUserData, U32F userDataCount);

	void SetUsePredictiveRays(bool enable) { m_usePredictiveRays = enable; }
	bool ShouldUsePredictiveRays() const { return m_usePredictiveRays; }

	// Quadruped (maybe) version
	void SetProbePointsPs(Point_arg backLeftPosPs,
						  Point_arg backRightPosPs,
						  Point_arg frontLeftPosPs,
						  Point_arg frontRightPosPs,
						  bool frontLeftValid  = true,
						  bool frontRightValid = true);

	void GetProbePointsPs(Point& backLeftPosPsOut,
						  Point& backRightPosPsOut,
						  Point& frontLeftPosPsOut,
						  Point& FrontRightPosPsOut) const;

	const Results& GetProbeResults(U32 index, float meshCollisionThreshold = -1.0f) const;

	const Results& GetPredictiveProbeResults(U32F index, float meshCollisionThreshold = -1.0f) const
	{
		ASSERT_LEG_INDEX_VALID(index);
		return GetProbeResults(index + kQuadLegCount, meshCollisionThreshold);
	}

	const Results& GetCollisionRaycastResults(U32 index) const
	{
		ASSERT_LEG_INDEX_VALID(index);
		if (ResultsValid(m_colResults[index]))
			return m_colResults[index];
		else
			return kInvalidResults;
	}

	const PredictOnStairs& GetPredictOnStairs() const { return m_predictOnStairs; }
	const AudioGround& GetAudioGround() const { return m_audioGround; }

	const Results& GetPredictiveCollisionRaycastResults(U32F index) const
	{
		ASSERT_LEG_INDEX_VALID(index);
		return GetCollisionRaycastResults(index + kQuadLegCount);
	}

	Point GetSetupPosPs(int index) const
	{
		ASSERT_LEG_INDEX_VALID(index);
		return m_setup[index].m_posPs;
	}

	void ClearResults();

	void UseMeshRayCasts(bool useMrc, U32 ignorePid);

	void KickCollisionProbe();
	void CollectCollisionProbeResults();

	void SetPlaneFilter(const BoundFrame& planeBf);

	void KickMeshRaycasts(Character* const pChar, bool disableMeshIk);

	void SetMinValidTime(TimeFrame validTime) { m_minValidTime = validTime; }

	bool IsCollisionPointValid(Point_arg p) const;

	void InvalidateMeshRayResult(U32 index);
	void InvalidateCollisionResult(U32 index);

	static const Clock* GetClock();

	static CONST_EXPR U32F kMaxNumRays = kQuadLegCount * 2;

	/// --------------------------------------------------------------------------------------------------------------- ///
	struct MeshRayCastContext
	{
		MutableCharacterHandle	m_hCharacter;
		Locator			m_parentSpace;
		Binding			m_binding;
		TimeFrame		m_time;
		uintptr_t		m_userData;
		int				m_leg;
		Mode			m_mode;
	};
	void MeshRaycastCallback(MeshProbe::CallbackObject const* pObject,
							 const MeshProbe::Probe& probeReq,
							 const MeshRayCastContext* pContext);

private:
	JOB_ENTRY_POINT_CLASS_DECLARE(LegRaycaster, CollisionProbeJob);
	void DoCollisionProbeInternal(LegRayCollisionProbe& legProbe, int legIndex, float radius, bool debug = false);

	int GetLegCount() const { return m_isQuadruped ? kQuadLegCount : kLegCount; }

	bool ResultsValid(const Results& r) const
	{
		if (!r.m_valid)
			return false;
		if (r.m_time < m_minValidTime)
			return false;
		if (r.m_mode != m_raycastMode)
			return false;
		if (!r.m_hitGround)
			return false;
		return true;
	}

	Vector GetPredictiveRayStartAdjustment() const;

	bool ConstrainPointToNavMesh(Point_arg pt, const class SimpleNavControl* pNavControl, Point& outConstrainedPt) const;

	CharacterHandle m_hSelf;

	BoundFrame m_planeFilterLocator;

	Mode m_raycastMode;
	I64 m_mrcMinValidFrame;
	U32 m_meshRayIgnorePid;		// -1 means ignore nothing.

	// * 2 so we can kick predictive raycasts to handle how quickly horses move
	// if this uses too much space we can make a seperate horse leg raycaster

	ndjob::CounterHandle m_pCollisionProbesJobCounter;

	TimeFrame m_minValidTime;
	TimeFrame m_collisionProbeKickTime;

	Setup m_setup[kMaxNumRays];
	Results m_mrcResults[kMaxNumRays];
	Results m_colResults[kMaxNumRays];
	PredictOnStairs m_predictOnStairs;

	AudioGround m_audioGround;

	bool m_planeFilterValid;
	bool m_isQuadruped;
	bool m_usePredictiveRays;
	bool m_useMeshRaycasts;
	bool m_oneRaycastDone;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct LegRayCollisionData
{
	LegRaycaster* m_pLegRaycaster;
	LegRayCollisionProbe m_legProbes[LegRaycaster::kMaxNumRays];
	LegRayCollisionProbe m_predictOnStairsProbe;
	LegRayCollisionProbe m_audioGroundProbe;

	float m_probeRadius = 0.0f;

	bool m_debugIk = false;
	bool m_debugPredictOnStairs = false;
	bool m_debugAudioGround = false;
};