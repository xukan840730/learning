/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/ringqueue.h"

#include "ndlib/util/tracker.h"

#include "gamelib/spline/catmull-rom.h"

// -------------------------------------------------------------------------------------------------
// Free Functions
// -------------------------------------------------------------------------------------------------

void RegisterSplineTrackerScriptFunctions();

// -------------------------------------------------------------------------------------------------
// SplineTracker: Tracks an object's position along a spline "rail".
// -------------------------------------------------------------------------------------------------

class SplineTracker
{
public:
	enum TransferType { kNone, kSplit, kJoin, kLaneChange, kReverse };
	struct TransferRequest
	{
		CatmullRom::Handle	m_hTransferSpline;		// spline to which to transfer
		TransferType		m_transferType;
		F32					m_realTransferDistance_m;
		CatmullRom::Handle	m_hDistanceSpline;		// spline along which realTransferDistance_m is measured

		TransferRequest() : m_hTransferSpline(nullptr), m_transferType(kNone), m_realTransferDistance_m(0.0f), m_hDistanceSpline(nullptr) { }
	};

	SplineTracker();
	explicit SplineTracker(const CatmullRom& spline, F32 startDistance_m = 0.0f);

	void SetOwner(Process* pOwner) { m_hOwner = pOwner; }
	MutableProcessHandle GetOwnerHandle() const { return m_hOwner; }
	const Process* GetOwner() const { return m_hOwner.ToProcess(); }

	void ClearTransferQueue();
	void SetLabel(const char* label) { m_label = label; }
	const char* GetLabel() const { return m_label ? m_label : ""; }

	virtual StringId64 GetType() const;

	void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound);

	void SetRelaxedTransferMode(bool relaxed) { m_bRelaxedTransfers = relaxed; }
	bool IsInRelaxedTransferMode() const { return m_bRelaxedTransfers; }

	// Returns the current spline.
	const CatmullRom* GetSpline() const;
	StringId64 GetSplineId() const;
	const CatmullRom* GetNextSpline() const;
	StringId64 GetNextSplineId() const;
	I32 GetSplineSegmentHint() const { return m_iSegmentHint; }
	virtual F32 GetCurrentSpeed() const { ALWAYS_ASSERT(false); return 0.0f; }
	CatmullRom::LocalParameter GetCurrentSegment() const;

	// Set the current spline. The optional second argument is used to define the equivalence
	// between virtual distance and real distance along this spline.
	virtual void SetSpline(const CatmullRom* pSpline, bool loop = false);

	// Transfer Type: Are we splitting off from our track (at the start of the new track), or
	// joining another track (at the end of the current track)?
	static TransferType FromDcTransferType(int dcType);
	static TransferType FromStringIdTransferType(StringId64 typeId);
	static const char* TransferTypeToString(TransferType type);

	// Transfer to a new spline. When the tracker reaches the entry point of the new spline,
	// it will smoothly transfer onto it. If the tracker is currently past the entry point,
	// it will immeidately "pop" over to the new spline.
	bool BuildTransferRequestTo(TransferRequest& transfer,
								const CatmullRom* pNewSpline,
								TransferType type,
								const CatmullRom* pCurSplineAtTimeOfTransfer);
	bool ShouldTransfer(const TransferRequest& transfer, F32& rVirtualToReal_m) const;
	bool RequestTransferTo(const CatmullRom* pSpline, TransferType type);
	bool IsTransferPending() const;
	bool IsTransferInProgress() const;
	CatmullRom* GetPendingTransferSpline(I32F index = 0) const;
	CatmullRom* GetLastTransferSpline() const;
	StringId64 GetLastTransferSplineId() const;

	bool ForceTransferTo(const CatmullRom* pSpline, float splineDist=-1.0f);

	// Control the blend distance for transfers.
	void SetTransferBlendDistance_m(F32 blendDistance_m);
	F32 GetTransferBlendDistance_m() const { return m_transferBlendDistance_m; }

	// Returns total distance of primary spline.
	F32 GetTotalDistance_m() const;

	// Current position along the spline.
	F32 GetCurrentVirtualDistance_m() const;
	void SetCurrentVirtualDistance_m(F32 distance_m);
	void SetCurrentVirtualDistanceFromPoint(Point_arg posWs);
	void UpdateCurrentVirtualDistance_mpf(F32 speed_mpf);

	// Get the current real distance on the current spline, clamped to or modulo the length of the
	// spline.  Use this when the returned value is to be used as an arc length on the spline.
	F32 GetCurrentRealDistance_m(bool adjustToValidRange = true) const;

	// Conversions between virtual and real distances.
	F32 VirtualFromRealDistance_m(F32 realDistance_m) const;
	F32 RealFromVirtualDistance_m(F32 virtualDistance_m, bool adjustToValidRange = true) const;

	// Reset virtual distance to be equivalent to real distance on the current spline.
	// Doing this periodically, when no transfers are pending, eliminates loss of floating-point
	// precision for numerically large virtual distances.
	void ResetVirtualDistance();

	// Current world-space position and tangent vector.
	Point GetCurrentPoint() const;
	Vector GetCurrentTangent() const;

	//get tangent this many meters ahead
	Vector GetFutureTangent(float distAhead) const;

	// Debug drawing.
	void DebugDraw(bool force = false) const;
	void DebugDrawTransfer(const TransferRequest& curTransfer) const;

	F32 GetVirtualToReal_m() const { return m_virtualToReal_m; }

private:
	static const U32	kMaxTransferRequests = 3;
	typedef StaticRingQueue< TransferRequest, (kMaxTransferRequests + 1) >	TransferQueue;

	struct Blend
	{
		const CatmullRom*	m_pSplineA;
		F32					m_realDistanceA_m;
		I32*				m_piSegmentHintA;
		const CatmullRom*	m_pSplineB;
		F32					m_realDistanceB_m;
		I32*				m_piSegmentHintB;
		F32					m_blendA2B;

		Blend() : m_pSplineA(nullptr), m_piSegmentHintA(nullptr), m_pSplineB(nullptr), m_piSegmentHintB(nullptr) { }
	};

	MutableProcessHandle	m_hOwner;
	const char*				m_label;
	mutable Point			m_curPoint;
	mutable Vector			m_curTangent;
	CatmullRom::Handle		m_hSpline;				// current spline
	F32						m_virtualDistance_m;	// current virtual distance along spline, in meters
	F32						m_virtualToReal_m;		// add this value to virtual distance to obtain real distance
	mutable I32 			m_iSegmentHint;
	mutable I32 			m_iOtherSegmentHint;
	union
	{
		U32					m_validFlags;
		struct
		{
			mutable bool	m_bBlendValid : 1;
			mutable bool	m_bCurPointValid : 1;
			mutable bool	m_bCurTangentValid : 1;
			mutable bool	m_bRelaxedTransfers : 1;
		};
	};
	TransferQueue			m_transferQueue;		// ring queue of transfer requests -- keeps one previous request
	mutable Blend			m_blend;

	F32						m_transferBlendDistance_m;

	friend class			TrainTwoPointSplineTracker;

	bool SetupTransferRequest(const CatmullRom* pInitialSpline,
							  const CatmullRom* pNewSpline,
							  TransferType type,
							  TransferRequest* transfer);
	void TransferToSpline(const CatmullRom* pSpline, F32 virtualToReal_m);
	I32F GetCurrentTransferIndex() const;
	I32F GetPreviousTransferIndex() const;
	F32 DetermineDistancePastTransferPoint_m(const TransferRequest& transfer) const;
	bool CheckForTransfer();
	bool CheckForAutoTransferSpline();
	const Blend& GetCurrentBlend() const;
	void GetFutureBlend(float futureDistanceDelta, Blend* const pOutBlend) const;
};

inline I32F SplineTracker::GetCurrentTransferIndex() const
{
	return m_transferQueue.GetOldestRawIndex();
}

inline I32F SplineTracker::GetPreviousTransferIndex() const
{
	I32F iCurrent = m_transferQueue.GetOldestRawIndex();
	if (iCurrent >= 0)
	{
		I32F iPrevious = iCurrent - 1;
		if (iPrevious < 0)
		{
			iPrevious = static_cast<I32F>(kMaxTransferRequests);
		}
		return iPrevious;
	}
	else
	{
		return -1;
	}
}

// -------------------------------------------------------------------------------------------------
// AutoSplineTracker: Tracks an object's position along a spline "rail" automatically, given a
// travel speed and max acceleration parameter.
// -------------------------------------------------------------------------------------------------

class AutoSplineTracker : public SplineTracker
{
public:
	AutoSplineTracker();
	explicit AutoSplineTracker(const CatmullRom& spline,
							   F32 targetSpeed_mps	= 0.0f,
							   F32 initialSpeed_mps = 0.0f,
							   F32 startDistance_m	= 0.0f);

	virtual StringId64 GetType() const override;

	virtual void SetSpline(const CatmullRom* pSpline, bool loop = false) override;

	void SetTargetSpeed(F32 speed_mps);
	F32 GetTargetSpeed() const;

	void SetCurrentSpeed(F32 speed_mps);
	virtual F32 GetCurrentSpeed() const override;

	void SetAcceleration(F32 accel);
	F32 GetAcceleration() const;

	void Update(bool* pbCrossedEndOfSpline = nullptr);

private:
	SpringTracker<F32> m_speedSpring;
	F32 m_targetSpeed_mps;
	F32 m_currentSpeed_mps;
	F32 m_acceleration;
	bool m_loop;
};

inline F32 AutoSplineTracker::GetTargetSpeed() const
{
	return m_targetSpeed_mps;
}

inline F32 AutoSplineTracker::GetCurrentSpeed() const
{
	return m_currentSpeed_mps;
}

inline F32 AutoSplineTracker::GetAcceleration() const
{
	return m_acceleration;
}
