
/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/process/process-component-handle.h"
#include "ndlib/render/ngen/meshraycaster.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/util/maybe.h"

#include "gamelib/gameplay/ai/convex-hull-xz.h"

class GroundModel
{
public:
	GroundModel();
	void SetOwner(MutableProcessHandle hOwner);
	bool IsValid() const;
	Maybe<Point> ProjectPointToGround(Point_arg posWs) const;
	Maybe<Vector> GetGroundNormalAtPos(Point_arg posWs) const;
	void FindGround(Point_arg startPos, Point_arg endPos, Point_arg alignPos);
	void FindGroundForceCol(Point_arg startPos, Point_arg endPos, Point_arg alignPos);

	void DebugDraw() const;
	void DebugDraw(Color c, DebugPrimTime time) const;

	void EnforcePointOnGround(Point_arg pos);
	Point GetClosestPointOnGround(Point_arg pos);

	void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) {}

private:
	static const int kNumRays = 5;

	struct ProbeResult
	{
		void Clear()
		{
			m_contact		= Point(kInvalidPoint);
			m_normal		= Vector(kInvalidVector);
			m_segmentPos[0] = m_segmentPos[1] = Point(kInvalidPoint);
			m_valid		   = false;
			m_segmentValid = false;
		}

		Point m_contact;
		Vector m_normal;
		Point m_segmentPos[2];
		bool m_valid;
		bool m_segmentValid;
	};

	struct PendingRayProbes
	{
		ProbeResult m_aResults[kNumRays];
		BitArray<kNumRays> m_resultsValid;
		Point m_startPos;
		Point m_endPos;
		Point m_alignPos;
		Transform m_worldToSegment;
		Transform m_segmentToWorld;
		I64 m_frameNumber;

		PendingRayProbes() { Reset(); }

		void Reset()
		{
			m_frameNumber = 0;
			m_resultsValid.ClearAllBits();
			m_startPos = kOrigin;
			m_endPos   = kOrigin;
		}
	};

	struct CallbackContext
	{
		I64 m_frameNumber;
		int m_probeIndex;
		ProcessComponentHandle<GroundModel> m_hGroundModel;
	};

	struct HullCastResult
	{
		Point m_pos		= kInvalidPoint;
		Vector m_normal = kInvalidVector;
		bool m_valid	= false;
	};

	STATIC_ASSERT(sizeof(CallbackContext) <= sizeof(MeshRayCastJob::CallbackContext));

	static void MeshrayCallbackFunc(MeshProbe::CallbackObject const* pObject, MeshRayCastJob::CallbackContext* pContext, const MeshProbe::Probe& probeReq);

	void ConstructGroundFromContacts(Point_arg startPos,
									 Point_arg endPos,
									 const int numRays,
									 const ProbeResult* probes,
									 Point_arg& alignPos);
	HullCastResult CastRayToHull(Point_arg pos, Vector_arg dir) const;

	int GetFreePendingRayProbes() const;
	int GetNextCompleteRayProbes() const;

	void DoFindGround(Point_arg startPos,
					  Point_arg endPos,
					  Point_arg alignPos,
					  const int numRays,
					  const bool useMeshRays);

	MutableProcessHandle m_hOwner;
	Transform m_worldToHull;
	ConvexHullXZ m_hull;
	Plane m_minPlane;
	Plane m_maxPlane;
	float m_minX;
	float m_maxX;
	bool m_valid;
	int m_numMeshRaySetsDone;
	PendingRayProbes m_aPendingProbes[6];
	bool m_hullFromRenderable;
};
