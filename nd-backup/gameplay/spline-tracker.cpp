/*
 * Copyright (c) 2003 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/spline-tracker.h"

#include "corelib/containers/static-map.h"
#include "corelib/memory/relocate.h"

#include "ndlib/nd-config.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/event.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/script/script-manager.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/entitydb.h"
#include "gamelib/script/nd-script-arg-iterator.h"
#include "gamelib/scriptx/h/nd-script-func-defines.h"
#include "gamelib/spline/catmull-rom.h"
#include "gamelib/state-script/ss-context.h"
#include "gamelib/state-script/ss-manager.h"
#include "gamelib/state-script/ss-track-group.h"
#include "gamelib/state-script/ss-track.h"

class SsInstance;

bool s_bCatchingUpIgcAfterScrub = false;
bool g_splineTrackerDumpTransfersToTty = false;
bool g_splineTrackerDumpTransfersToTtyDetailed = false;

SplineTracker* g_pSplineTrackerToDebug = nullptr;

// -------------------------------------------------------------------------------------------
// Types and Constants
// -------------------------------------------------------------------------------------------

#define ENABLE_DETAILED_PROFILE	0	// turn this on to get detailed profiling info

#if ENABLE_DETAILED_PROFILE
	#define PROFILE_DETAILED(A, B)	PROFILE(A, B)
#else
	#define PROFILE_DETAILED(A, B)
#endif

static /*const*/ F32 kMaxTransferGap = 100.0f;
static /*const*/ F32 kDefaultTransferBlendDistance_m = 50.0f;

// -------------------------------------------------------------------------------------------
// SplineTracker
// -------------------------------------------------------------------------------------------

/*static*/ SplineTracker::TransferType SplineTracker::FromDcTransferType(int dcType)
{
	switch (dcType)
	{
	case DC::kSplineTransferTypeSplit:			return kSplit;
	case DC::kSplineTransferTypeJoin:			return kJoin;
	case DC::kSplineTransferTypeLaneChange:		return kLaneChange;
	case DC::kSplineTransferTypeReverse:        return kReverse;
	case DC::kSplineTransferTypeNone:			return kNone;
	default:									return kNone;
	}
}

/*static*/ SplineTracker::TransferType SplineTracker::FromStringIdTransferType(StringId64 typeId)
{
	switch (typeId.GetValue())
	{
	case SID_VAL("split"):			return kSplit;
	case SID_VAL("join"):			return kJoin;
	case SID_VAL("lane-change"):	return kLaneChange;
	case SID_VAL("none"):			return kNone;
	default:					return kNone;
	}
}

/*static*/ const char* SplineTracker::TransferTypeToString(TransferType type)
{
	switch (type)
	{
	case kSplit:		return "kSplit";
	case kJoin:			return "kJoin";
	case kLaneChange:	return "kLaneChange";
	default:			return "kNone";
	}
}

SplineTracker::SplineTracker() :
	m_hSpline(nullptr),
	m_virtualDistance_m(0.0f),
	m_virtualToReal_m(0.0f),
	m_iSegmentHint(-1),
	m_iOtherSegmentHint(-1),
	m_validFlags(0),
	m_hOwner(),
	m_label(nullptr)
{
	m_transferBlendDistance_m = 0.0f;
}

SplineTracker::SplineTracker(const CatmullRom& spline, F32 startDistance_m) :
	m_hSpline(nullptr),
	m_virtualDistance_m(0.0f),
	m_virtualToReal_m(0.0f),
	m_validFlags(0),
	m_hOwner(),
	m_label(nullptr)
{
	SetSpline(&spline);
	SetCurrentVirtualDistance_m(startDistance_m);
}

StringId64 SplineTracker::GetType() const
{
	return SID("SplineTracker");
}

void SplineTracker::ClearTransferQueue()
{
	m_transferQueue.Reset();
}

void SplineTracker::Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_blend.m_piSegmentHintA, delta, lowerBound, upperBound);
	RelocatePointer(m_blend.m_piSegmentHintB, delta, lowerBound, upperBound);
	m_transferQueue.Relocate(delta, lowerBound, upperBound);
}

F32 SplineTracker::GetTotalDistance_m() const
{
	const CatmullRom* pSpline = m_hSpline.ToCatmullRom();
	if (pSpline)
	{
		return (F32)pSpline->GetTotalArcLength();
	}
	return 0.0f;
}

void SplineTracker::SetSpline(const CatmullRom* pSpline, bool notUsed)
{
	if (m_hSpline.ToCatmullRom() != pSpline)
		m_iSegmentHint = -1;

	m_hSpline = pSpline;
	m_virtualToReal_m = 0.0f;
	m_virtualDistance_m = 0.0f;
	ResetVirtualDistance();

	//m_bBlendValid = m_bCurPointValid = m_bCurTangentValid = false;
	m_validFlags = 0;

	CheckForAutoTransferSpline();
}

void SplineTracker::TransferToSpline(const CatmullRom* pSpline, F32 virtualToReal_m)
{
	const CatmullRom* pOldSpline = GetSpline();

	if (m_hSpline.ToCatmullRom() != pSpline)
		m_iSegmentHint = -1;

	m_hSpline = pSpline;
	m_virtualToReal_m = virtualToReal_m;

	//m_bBlendValid = m_bCurPointValid = m_bCurTangentValid = false;
	m_validFlags = 0;

#if !FINAL_BUILD
	if ((g_splineTrackerDumpTransfersToTty || g_splineTrackerDumpTransfersToTtyDetailed) && (!m_hOwner.HandleValid() || DebugSelection::Get().IsProcessOrNoneSelected(m_hOwner.ToProcess())))
	{
		const char* oldSplineName = (pOldSpline && pOldSpline->GetSplineData()) ? DevKitOnly_StringIdToString(pOldSpline->GetSplineData()->m_nameId) : "<null>";
		const char* newSplineName = (pSpline && pSpline->GetSplineData()) ? DevKitOnly_StringIdToString(pSpline->GetSplineData()->m_nameId) : "<null>";
		const char* ownerName = GetOwner() ? GetOwner()->GetName() : "<null>";

		MsgCinematic("%08x: Performed SplineTracker transfer: %s -> %s for owner %s %s (v = %g, v2r = %g)\n",
			EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused,
			oldSplineName, newSplineName,
			ownerName, GetLabel(), m_virtualDistance_m, m_virtualToReal_m);
	}
#endif

	CheckForAutoTransferSpline();
}

bool SplineTracker::SetupTransferRequest(const CatmullRom* pInitialSpline,
										 const CatmullRom* pNewSpline,
										 TransferType type,
										 TransferRequest* transfer)
{
	ALWAYS_ASSERT(pInitialSpline != nullptr);
	ALWAYS_ASSERT(pNewSpline != nullptr);
	ALWAYS_ASSERT(pNewSpline->GetSegmentCount() != 0);
	ALWAYS_ASSERT(transfer != nullptr);

	if (pNewSpline == pInitialSpline)
		return false;

	if (type == kSplit)
	{
		Point newTransferPoint(pNewSpline->GetControlPoint(0));
		F32 realTransferDistance_m = pInitialSpline->FindArcLengthClosestToPoint(newTransferPoint);

		// Check to be sure the new spline is close enough.
		Point transferPoint = pInitialSpline->EvaluatePointAtArcLength(realTransferDistance_m);
		Vector gapVector(newTransferPoint - transferPoint);
		Scalar gapSqr(LengthSqr(gapVector));
		Scalar maxGapSqr(kMaxTransferGap * kMaxTransferGap);
		if (IsPositive(gapSqr - maxGapSqr))
		{
			MsgConScriptError("transfer-to-spline: Gap between splines '%s' and '%s' is too big\n", pInitialSpline->GetBareName(), pNewSpline->GetBareName());
			if (!m_bRelaxedTransfers)
				return false;
		}

		// Set up the transfer.
		transfer->m_hTransferSpline = pNewSpline;
		transfer->m_transferType = type;
		transfer->m_realTransferDistance_m = realTransferDistance_m;
		transfer->m_hDistanceSpline = pInitialSpline;
	}
	else if (type == kLaneChange)
	{
		Point transferPoint(m_curPoint);
		F32 newRealTransferDistance_m = pNewSpline->FindArcLengthClosestToPoint(transferPoint) + m_transferBlendDistance_m;

		// Check to be sure the new spline is close enough.
		Point newTransferPoint = pNewSpline->EvaluatePointAtArcLength(newRealTransferDistance_m);
		Vector gapVector(newTransferPoint - transferPoint);
		Scalar gapSqr(LengthSqr(gapVector));
		Scalar maxGapSqr(kMaxTransferGap * kMaxTransferGap);
		if (IsPositive(gapSqr - maxGapSqr))
		{
			MsgConScriptError("transfer-to-spline: Gap between splines '%s' and '%s' is too big\n", pInitialSpline->GetBareName(), pNewSpline->GetBareName());
			if (!m_bRelaxedTransfers)
				return false;
		}

		// Set up the transfer.
		transfer->m_hTransferSpline = pNewSpline;
		transfer->m_transferType = type;
		transfer->m_realTransferDistance_m = newRealTransferDistance_m;
		transfer->m_hDistanceSpline = pNewSpline;
	}

	else if (type == kReverse)
	{
		//U32F iLastPoint = pNewSpline->GetControlPointCount();
		//ALWAYS_ASSERT(iLastPoint > 0);
		//iLastPoint -= 1;

		//Point transferPoint(pNewSpline->GetControlPoint(0));
		Point transferPoint(m_curPoint);
		F32 newRealTransferDistance_m = pNewSpline->FindArcLengthClosestToPointClamp(transferPoint, 0, pNewSpline->GetControlPointCount() - 1);

		// Check to be sure the new spline is close enough.
		Point newTransferPoint = pNewSpline->EvaluatePointAtArcLength(newRealTransferDistance_m);
		Vector gapVector(newTransferPoint - transferPoint);
		Scalar gapSqr(LengthSqr(gapVector));
		Scalar maxGapSqr(kMaxTransferGap * kMaxTransferGap);
		if (IsPositive(gapSqr - maxGapSqr))
		{
			MsgConScriptError("transfer-to-spline: Gap between splines '%s' and '%s' is too big\n", pInitialSpline->GetBareName(), pNewSpline->GetBareName());
			if (!m_bRelaxedTransfers)
				return false;
		}

		// Set up the transfer.
		transfer->m_hTransferSpline = pNewSpline;
		transfer->m_transferType = type;
		transfer->m_realTransferDistance_m = newRealTransferDistance_m;
		transfer->m_hDistanceSpline = pNewSpline;
	}

	else //if (type == kJoin)
	{
		U32F iLastPoint = pInitialSpline->GetControlPointCount();
		ALWAYS_ASSERT(iLastPoint > 0);
		iLastPoint -= 1;

		Point transferPoint(pInitialSpline->GetControlPoint(iLastPoint));
		F32 newRealTransferDistance_m = pNewSpline->FindArcLengthClosestToPointClamp(transferPoint, 0, pNewSpline->GetControlPointCount() - 1);

		// Check to be sure the new spline is close enough.
		Point newTransferPoint = pNewSpline->EvaluatePointAtArcLength(newRealTransferDistance_m);
		Vector gapVector(newTransferPoint - transferPoint);
		Scalar gapSqr(LengthSqr(gapVector));
		Scalar maxGapSqr(kMaxTransferGap * kMaxTransferGap);
		if (IsPositive(gapSqr - maxGapSqr))
		{
			MsgConScriptError("transfer-to-spline: Gap between splines '%s' and '%s' is too big\n", pInitialSpline->GetBareName(), pNewSpline->GetBareName());
			if (!m_bRelaxedTransfers)
				return false;
		}

		// Set up the transfer.
		transfer->m_hTransferSpline = pNewSpline;
		transfer->m_transferType = type;
		transfer->m_realTransferDistance_m = newRealTransferDistance_m;
		transfer->m_hDistanceSpline = pNewSpline;
	}

	//insert reverse change here

#if !FINAL_BUILD
	if ((g_splineTrackerDumpTransfersToTty || g_splineTrackerDumpTransfersToTtyDetailed) && (!m_hOwner.HandleValid() || DebugSelection::Get().IsProcessOrNoneSelected(m_hOwner.ToProcess())))
	{
		const CatmullRom* pCurSpline = GetSpline();
		const char* curSplineName = (pCurSpline && pCurSpline->GetSplineData()) ? DevKitOnly_StringIdToString(pCurSpline->GetSplineData()->m_nameId) : "<null>";
		const char* newSplineName = (pNewSpline && pNewSpline->GetSplineData()) ? DevKitOnly_StringIdToString(pNewSpline->GetSplineData()->m_nameId) : "<null>";
		const char* ownerName = GetOwner() ? GetOwner()->GetName() : "<null>";

		MsgCinematic("%08x: Set up SplineTracker transfer: %s -> %s (%s) for owner %s %s (v = %g, v2r = %g)\n",
			EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused,
			curSplineName, newSplineName, TransferTypeToString(transfer->m_transferType),
			ownerName, GetLabel(), m_virtualDistance_m, m_virtualToReal_m);
	}
#endif

	return true;
}

bool SplineTracker::BuildTransferRequestTo(TransferRequest& transfer,
										   const CatmullRom* pNewSpline,
										   TransferType type,
										   const CatmullRom* pCurSplineAtTimeOfTransfer)
{
	if (!pCurSplineAtTimeOfTransfer || type == kNone)
	{
		transfer.m_hDistanceSpline = nullptr;
		transfer.m_hTransferSpline = pNewSpline;
		transfer.m_transferType = kNone;
		transfer.m_realTransferDistance_m = 0.0f;
		return true;
	}
	else
	{
		const char* curSplineName = (pCurSplineAtTimeOfTransfer) ? pCurSplineAtTimeOfTransfer->GetBareName() : "<null>";
		const char* newSplineName = (pNewSpline) ? pNewSpline->GetBareName() : "<null>";

		if (pNewSpline != nullptr && pCurSplineAtTimeOfTransfer != nullptr && pNewSpline->GetSegmentCount() != 0)
		{
			// Determine the arc length on the current spline at which the new spline
			// branches off or joins up.
			return SetupTransferRequest(pCurSplineAtTimeOfTransfer, pNewSpline, type, &transfer);
		}
		else
		{
			if (pNewSpline == nullptr)
				MsgConScriptError("transfer-to-spline: New spline is null (cur spline is '%s')\n", curSplineName);
			else if (pCurSplineAtTimeOfTransfer == nullptr)
				MsgConScriptError("transfer-to-spline: Current spline is null (this should never happen!!!)\n");
			else if (pNewSpline->GetSegmentCount() == 0)
				MsgConScriptError("transfer-to-spline: New spline '%s' has no segments.\n", newSplineName);
			else if (m_transferQueue.GetCount() >= kMaxTransferRequests)
				MsgConScriptError("transfer-to-spline: Too many queued transfer requests (transfer to '%s').\n", newSplineName);
			else
				MsgConScriptError("transfer-to-spline: Unknown error ('%s' to '%s')\n", curSplineName, newSplineName);
		}
	}

	return false;
}

bool SplineTracker::RequestTransferTo(const CatmullRom* pNewSpline, TransferType type)
{
	TransferRequest transfer;
	// Determine what my current spline will be at the time of transfer. This is either my
	// current spline if no transfers are pending, or else the last-queued transfer spline.
	const CatmullRom* pCurSplineAtTimeOfTransfer = GetLastTransferSpline();
	if (BuildTransferRequestTo(transfer, pNewSpline, type, pCurSplineAtTimeOfTransfer))
	{
		m_transferQueue.Reset();

		if (transfer.m_transferType == kNone)
		{
			SetSpline(pNewSpline);
			return true;
		}
		else if (m_transferQueue.GetCount() < kMaxTransferRequests) // IMPORTANT so that we never overwrite the "previous" request!
		{
			m_transferQueue.Enqueue(transfer);

			if (type == kSplit)
			{
				// If we're already past the split-off point, attach to the new spline.
				CheckForTransfer();
			}

			return true;
		}
	}

	return false;
}

bool SplineTracker::IsTransferPending() const
{
	return (m_transferQueue.GetCount() != 0);
}

bool SplineTracker::IsTransferInProgress() const
{
	const Blend& blend = GetCurrentBlend();
	return (blend.m_blendA2B > 0.0f && blend.m_blendA2B < 1.0f);
}

CatmullRom* SplineTracker::GetPendingTransferSpline(I32F index) const
{
	TransferQueue::ConstIterator it	   = m_transferQueue.Begin();
	TransferQueue::ConstIterator itEnd = m_transferQueue.End();
	CatmullRom* pSpline = nullptr;
	for (I32F i = 0; i < index && it != itEnd; ++i, ++it)
	{
		const TransferRequest& req = *it;
		pSpline = const_cast<CatmullRom*>(req.m_hTransferSpline.ToCatmullRom());
	}
	return pSpline;
}

CatmullRom* SplineTracker::GetLastTransferSpline() const
{
	// Determine what my current spline will be at the time of transfer. This is either my
	// current spline if no transfers are pending, or else the last-queued transfer spline.
	const CatmullRom* pCurSplineAtTimeOfTransfer = m_hSpline.ToCatmullRom();
	if (m_transferQueue.GetCount() != 0)
	{
		I32F iNewest = m_transferQueue.GetNewestRawIndex();
		ALWAYS_ASSERT(iNewest >= 0);
		const TransferRequest& newestTransfer = m_transferQueue.GetAtRawIndex(iNewest);
		pCurSplineAtTimeOfTransfer = newestTransfer.m_hTransferSpline.ToCatmullRom();
	}
	return const_cast<CatmullRom*>(pCurSplineAtTimeOfTransfer);
}

StringId64 SplineTracker::GetLastTransferSplineId() const
{
	CatmullRom* pSpline = GetLastTransferSpline();
	return (pSpline && pSpline->GetSplineData()) ? pSpline->GetSplineData()->m_nameId : INVALID_STRING_ID_64;
}

bool SplineTracker::ForceTransferTo(const CatmullRom* pSpline, float splineDist)
{
	if (m_hSpline.ToCatmullRom() != pSpline)
		m_iSegmentHint = -1;

	ResetVirtualDistance();

	if (splineDist < 0.0f)
		splineDist = GetCurrentVirtualDistance_m();
	
	m_hSpline = pSpline;
	SetCurrentVirtualDistance_m(splineDist);
	CheckForAutoTransferSpline();

	m_validFlags = 0;
	return true;
}

/*static*/ void SplineTracker::SetTransferBlendDistance_m(F32 blendDistance_m)
{
	if (blendDistance_m < 0.0f)
	{
		blendDistance_m = 0.0f;
	}
	m_transferBlendDistance_m = blendDistance_m;
}

F32 SplineTracker::DetermineDistancePastTransferPoint_m(const TransferRequest& curTransfer) const
{
	PROFILE_DETAILED(Spline, SplnTrk_DistPastXfer);

	F32 distancePast_m = -1.0f; // negative values mean "not past transfer point, don't transfer yet"
	F32 realDistance_m = GetCurrentRealDistance_m(false);

	const CatmullRom* pSpline = m_hSpline.ToCatmullRom();

	if (pSpline)
	{
		switch (curTransfer.m_transferType)
		{
		case kSplit:
			{
				// Negative values mean "not past transfer point, don't transfer yet".
				ASSERT(curTransfer.m_hDistanceSpline.IsSameAs(m_hSpline));
				distancePast_m = realDistance_m - curTransfer.m_realTransferDistance_m;

				if (pSpline->IsLooped())
				{
					// Normalize distancePast_m so that it is measured relative to the split point.
					F32 circumference_m = pSpline->GetTotalArcLength();
					if (distancePast_m < 0.0f)
					{
						distancePast_m += circumference_m;
					}
					ALWAYS_ASSERT(distancePast_m >= 0.0f);

					// Now, if the distance past is more than 1/4 of the circumference, assume we're
					// actually located BEFORE the split, not after it.
					if (distancePast_m > 0.25f * circumference_m)
					{
						distancePast_m -= circumference_m;
					}
				}
			}
			break;

		case kLaneChange: 
			{
				const CatmullRom* pNewSpline = curTransfer.m_hDistanceSpline.ToCatmullRom();
				realDistance_m = pNewSpline->FindArcLengthClosestToPoint(pSpline->EvaluatePointAtArcLength(realDistance_m));

				// Negative values mean "not past transfer point, don't transfer yet".
				distancePast_m = realDistance_m - curTransfer.m_realTransferDistance_m;
				if (pNewSpline->IsLooped())
				{
					// Normalize distancePast_m so that it is measured relative to the split point.
					F32 circumference_m = pNewSpline->GetTotalArcLength();
					while (distancePast_m < 0.0f)
					{
						distancePast_m += circumference_m;
					}
					ALWAYS_ASSERT(distancePast_m >= 0.0f);

					// Now, if the distance past is more than 1/4 of the circumference, assume we're
					// actually located BEFORE the split, not after it.
					while (distancePast_m > 0.25f * circumference_m)
					{
						distancePast_m -= circumference_m;
					}
				}
			}
			break;
		case kJoin:
			{
				F32 totalDistance_m = pSpline->GetTotalArcLength();
				distancePast_m = realDistance_m - totalDistance_m;
			}
			break;

		case kReverse:
			{
				const CatmullRom* pNewSpline = curTransfer.m_hDistanceSpline.ToCatmullRom();
				realDistance_m = pNewSpline->FindArcLengthClosestToPoint(pSpline->EvaluatePointAtArcLength(realDistance_m));

				// Negative values mean "not past transfer point, don't transfer yet".
				distancePast_m = realDistance_m - curTransfer.m_realTransferDistance_m;
				if (pNewSpline->IsLooped())
				{
					// Normalize distancePast_m so that it is measured relative to the split point.
					F32 circumference_m = pNewSpline->GetTotalArcLength();
					while (distancePast_m < 0.0f)
					{
						distancePast_m += circumference_m;
					}
					ALWAYS_ASSERT(distancePast_m >= 0.0f);

					// Now, if the distance past is more than 1/4 of the circumference, assume we're
					// actually located BEFORE the split, not after it.
					while (distancePast_m > 0.25f * circumference_m)
					{
						distancePast_m -= circumference_m;
					}
				}
			}
			break;

		case kNone:
			break;
		default:
			break;
		}
	}

//	ALWAYS_ASSERT(distancePast_m >= 0.0f);

	return distancePast_m;
}

bool SplineTracker::ShouldTransfer(const TransferRequest& transfer, F32& rVirtualToReal_m) const
{
	rVirtualToReal_m = m_virtualToReal_m;

	if (!transfer.m_hTransferSpline.ToCatmullRom()
	||  transfer.m_hTransferSpline.ToCatmullRom() == GetSpline())
	{
		// Ignore and discard any transfer to my current spline or to an invalid spline.
		return false;
	}

	// If we're past the transfer point, attach to the new spline.
	F32 distancePast_m = DetermineDistancePastTransferPoint_m(transfer);
	F32 realDistance_m = kLargestFloat; // don't transfer
	switch (transfer.m_transferType)
	{
	case kSplit:
	case kLaneChange:
		// If we're splitting off onto a new track, then the distance past is exactly the real
		// distance along the new spline that is equivalent to our current virtual distance.
		if (distancePast_m >= 0.0f)
		{
			realDistance_m = distancePast_m;
		}
		break;
	case kJoin:
		// If we're joining onto a new track, then the distance past the transfer distance
		// (which itself is a real distance on the new spline) is the real distance that is
		// equivalent to our current virtual distance.
		if (distancePast_m >= 0.0f)
		{
			realDistance_m = transfer.m_realTransferDistance_m + distancePast_m;
		}
		break;
	case kNone: // switch to spline immediately (no fancy transfer)
	default:
		realDistance_m = 0.0f;
		break;
	}

	if (realDistance_m != kLargestFloat)
	{
		// Yes, we should transfer to the new spline. Return the virtual-to-real equivalence.
		F32 virtualToReal_m = realDistance_m - m_virtualDistance_m;
		rVirtualToReal_m = virtualToReal_m;

		return true;
	}

	return false;
}

bool SplineTracker::CheckForTransfer()
{
	PROFILE_DETAILED(Spline, SplnTrk_CheckForTransfer);

	I32F iCurrent = GetCurrentTransferIndex();
	bool dequeue = false;

	TransferRequest *pCurTransfer = nullptr;
	if (iCurrent >= 0)
	{
		pCurTransfer = &m_transferQueue.GetAtRawIndex(iCurrent);
		dequeue = true;
	}


	if (pCurTransfer)
	{
		if (!pCurTransfer->m_hTransferSpline.ToCatmullRom())
		{
			// Ignore and discard any transfer to my current spline or to an invalid spline.
			if (dequeue)
				m_transferQueue.Dequeue();
			return false;
		}

#if !FINAL_BUILD
		if (g_splineTrackerDumpTransfersToTtyDetailed && (!m_hOwner.HandleValid() || DebugSelection::Get().IsProcessOrNoneSelected(m_hOwner.ToProcess())))
		{
			const CatmullRom* pOldSpline = GetSpline();
			const CatmullRom* pNewSpline = pCurTransfer->m_hTransferSpline.ToCatmullRom();
			const char* oldSplineName = (pOldSpline && pOldSpline->GetSplineData()) ? DevKitOnly_StringIdToString(pOldSpline->GetSplineData()->m_nameId) : "<null>";
			const char* newSplineName = (pNewSpline && pNewSpline->GetSplineData()) ? DevKitOnly_StringIdToString(pNewSpline->GetSplineData()->m_nameId) : "<null>";
			const char* ownerName = GetOwner() ? GetOwner()->GetName() : "<null>";

			MsgCinematic("%08x: Checking for SplineTracker transfer: %s -> %s for owner %s %s (v = %g, v2r = %g)\n",
				EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused,
				oldSplineName, newSplineName,
				ownerName, GetLabel(), m_virtualDistance_m, m_virtualToReal_m);
		}
#endif

		// If we're past the transfer point, attach to the new spline.
		F32 distancePast_m = DetermineDistancePastTransferPoint_m(*pCurTransfer);
		if (distancePast_m >= 0.0f)
		{
			F32 realDistance_m;
			switch (pCurTransfer->m_transferType)
			{
			case kSplit:
				// If we're splitting off onto a new track, then the distance past is exactly the real
				// distance along the new spline that is equivalent to our current virtual distance.
				realDistance_m = distancePast_m;
				break;
			case kLaneChange:
			case kJoin:
				// If we're joining onto a new track, then the distance past the transfer distance
				// (which itself is a real distance on the new spline) is the real distance that is
				// equivalent to our current virtual distance.
				realDistance_m = pCurTransfer->m_realTransferDistance_m + distancePast_m;
				break;
			case kReverse:
				//todo harold
				realDistance_m = distancePast_m;
				break;
			case kNone:
			default:
				ALWAYS_ASSERT(false);
				realDistance_m = 0.0f;
				break;
			}

			// Transfer to the new spline.
			F32 virtualToReal_m = realDistance_m - m_virtualDistance_m;
			TransferToSpline(pCurTransfer->m_hTransferSpline.ToCatmullRom(), virtualToReal_m);

			// Dequeue the current transfer request, now that we've actually transferred.
			// IMPORTANT: The data is still on the queue, and can be accessed when we want to
			// look at the previous transfer request.
			if (dequeue)
				m_transferQueue.Dequeue();

			return true;
		}
	}

	return false;
}

bool SplineTracker::CheckForAutoTransferSpline()
{
	const CatmullRom* pSpline = m_hSpline.ToCatmullRom();
	if (pSpline)
	{
		StringId64 transferSpline = pSpline->GetTagData<StringId64>(SID("transfer-to-spline"), INVALID_STRING_ID_64);
		if (transferSpline != INVALID_STRING_ID_64)
		{
			StringId64 transferTypeId = pSpline->GetTagData<StringId64>(SID("spline-transfer-type"), SID("join"));
			CatmullRom *pNewSpline = g_splineManager.FindByName(transferSpline);
			ASSERTF(pNewSpline, ("Can't find transfer spline '%s' (tagged in spline '%s')", DevKitOnly_StringIdToString(transferSpline), pSpline->GetBareName()));

			TransferType transferType = FromStringIdTransferType(transferTypeId);
			ASSERTF(transferType != kNone, ("Invalid transfer type '%s' (tagged in spline '%s')", DevKitOnly_StringIdToString(transferTypeId), pSpline->GetBareName()));

			if (pNewSpline)
			{
				TransferRequest transfer;
				SetupTransferRequest(pSpline, pNewSpline, transferType, &transfer);
				m_transferQueue.Enqueue(transfer);

				return true;
			}
		}
	}

	return false;
}

const CatmullRom* SplineTracker::GetSpline() const
{
	return m_hSpline.ToCatmullRom();
}

StringId64 SplineTracker::GetSplineId() const
{
	const CatmullRom* pSpline = GetSpline();
	if (pSpline && pSpline->GetSplineData())
	{
		return pSpline->GetSplineData()->m_nameId;
	}
	return INVALID_STRING_ID_64;
}

CatmullRom::LocalParameter SplineTracker::GetCurrentSegment() const
{
	CatmullRom::LocalParameter localParam;

	const CatmullRom* pSpline = GetSpline();
	if (pSpline && pSpline->GetSplineData())
	{
		I32 iSegmentHint = m_iSegmentHint;
		localParam = pSpline->ArcLengthToLocalParam(GetCurrentRealDistance_m(), (iSegmentHint >= 0) ? &iSegmentHint : nullptr);
	}
	else
	{
		localParam.m_iSegment = CatmullRom::kInvalidSegmentIndex;
		localParam.m_u = -1.0f;
	}
	return localParam;
}

void SplineTracker::SetCurrentVirtualDistance_m(F32 distance_m)
{
	// First slam in the new, possibly out-of-range distance.
	m_virtualDistance_m = distance_m;

	//m_bBlendValid = m_bCurPointValid = m_bCurTangentValid = false;
	m_validFlags = 0;

#if !FINAL_BUILD
	if (g_splineTrackerDumpTransfersToTtyDetailed && (!m_hOwner.HandleValid() || DebugSelection::Get().IsProcessOrNoneSelected(m_hOwner.ToProcess())))
	{
		const CatmullRom* pCurSpline = GetSpline();
		const char* splineName = (pCurSpline && pCurSpline->GetSplineData()) ? DevKitOnly_StringIdToString(pCurSpline->GetSplineData()->m_nameId) : "<null>";
		const char* ownerName = GetOwner() ? GetOwner()->GetName() : "<null>";

		MsgCinematic("%08x: Setting virtual SplineTracker distance on %s for owner %s %s (v = %g, v2r = %g)\n",
			EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused,
			splineName,
			ownerName, GetLabel(), m_virtualDistance_m, m_virtualToReal_m);
	}
#endif

	// Check to see if this new distance will cause a transfer to another spline.
	CheckForTransfer();
}

void SplineTracker::UpdateCurrentVirtualDistance_mpf(F32 speed_mpf)
{
	// First slam in the new, possibly out-of-range distance.
	m_virtualDistance_m += speed_mpf;

	//m_bBlendValid = m_bCurPointValid = m_bCurTangentValid = false;
	m_validFlags = 0;

#if !FINAL_BUILD
	if (g_splineTrackerDumpTransfersToTtyDetailed && (!m_hOwner.HandleValid() || DebugSelection::Get().IsProcessOrNoneSelected(m_hOwner.ToProcess())))
	{
		const CatmullRom* pCurSpline = GetSpline();
		const char* splineName = (pCurSpline && pCurSpline->GetSplineData()) ? DevKitOnly_StringIdToString(pCurSpline->GetSplineData()->m_nameId) : "<null>";
		const char* ownerName = GetOwner() ? GetOwner()->GetName() : "<null>";

		MsgCinematic("%08x: Updating virtual SplineTracker distance on %s for owner %s %s (v = %g, v2r = %g, speed_mpf = %g)\n",
			EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused,
			splineName,
			ownerName, GetLabel(), m_virtualDistance_m, m_virtualToReal_m, speed_mpf);
	}
#endif

	// Check to see if this new distance will cause a transfer to another spline.
	CheckForTransfer();
}

void SplineTracker::SetCurrentVirtualDistanceFromPoint(Point_arg posWs)
{
	const CatmullRom* pSpline = GetSpline();
	if (pSpline && pSpline->GetSplineData())
	{
		F32 realDistance_m = pSpline->FindArcLengthClosestToPoint(posWs, &m_iSegmentHint);
		F32 virtualDistance_m = VirtualFromRealDistance_m(realDistance_m);
		SetCurrentVirtualDistance_m(virtualDistance_m);
	}
}

F32 SplineTracker::GetCurrentVirtualDistance_m() const
{
	return m_virtualDistance_m;
}

F32 SplineTracker::VirtualFromRealDistance_m(F32 realDistance_m) const
{
	F32 virtualDistance_m = realDistance_m - m_virtualToReal_m;
	return virtualDistance_m;
}

F32 SplineTracker::RealFromVirtualDistance_m(F32 virtualDistance_m, bool adjustToValidRange) const
{
	F32 realDistance_m = virtualDistance_m + m_virtualToReal_m;

	if (adjustToValidRange)
	{
		PROFILE_DETAILED(Spline, SplnTrk_AdjToValidRange);

		const CatmullRom* pSpline = m_hSpline.ToCatmullRom();
		if (pSpline && pSpline->IsLooped())
		{
			// Modulo the distance to the spline's valid range.
			F32 totalDistance_m = GetTotalDistance_m();
			realDistance_m = fmodf(realDistance_m, totalDistance_m);
			if (realDistance_m < 0.0f)
			{
				realDistance_m += totalDistance_m;
			}
		}
		else
		{
			// Clamp the distance to the spline.

			if (realDistance_m < 0.0f)
			{
				realDistance_m = 0.0f;
			}
			else
			{
				F32 totalDistance_m = GetTotalDistance_m();
				if (realDistance_m > totalDistance_m)
				{
					realDistance_m = totalDistance_m;
				}
			}
		}
	}

	return realDistance_m;
}

F32 SplineTracker::GetCurrentRealDistance_m(bool adjustToValidRange) const
{
	return RealFromVirtualDistance_m(m_virtualDistance_m, adjustToValidRange);
}

void SplineTracker::ResetVirtualDistance()
{
	ASSERT(!IsTransferInProgress());
	m_virtualDistance_m = GetCurrentRealDistance_m(true);
	m_virtualToReal_m = 0.0f;

	//m_bBlendValid = m_bCurPointValid = m_bCurTangentValid = false;
	m_validFlags = 0;
}

StringId64 SplineTracker::GetNextSplineId() const
{
	I32F iCurrent = GetCurrentTransferIndex();

	if (IsTransferInProgress() && m_transferBlendDistance_m > 0.01f && iCurrent >= 0)
	{
		const TransferRequest& curTransfer = m_transferQueue.GetAtRawIndex(iCurrent);
		if (curTransfer.m_transferType == kJoin || curTransfer.m_transferType == kLaneChange)
		{
			const CatmullRom* pNextSpline = curTransfer.m_hTransferSpline.ToCatmullRom();
			if (pNextSpline)
			{
				return pNextSpline->GetSplineData()->m_nameId;
			}
		}
	}
	return INVALID_STRING_ID_64;
}

const CatmullRom* SplineTracker::GetNextSpline() const
{
	I32F iCurrent = GetCurrentTransferIndex();

	if (IsTransferInProgress() && m_transferBlendDistance_m > 0.01f && iCurrent >= 0)
	{
		const TransferRequest& curTransfer = m_transferQueue.GetAtRawIndex(iCurrent);
		if (curTransfer.m_transferType == kJoin || curTransfer.m_transferType == kLaneChange)
		{
			const CatmullRom* pNextSpline = curTransfer.m_hTransferSpline.ToCatmullRom();
			if (pNextSpline)
			{
				return pNextSpline;
			}
		}
	}

	return nullptr;
}

const SplineTracker::Blend& SplineTracker::GetCurrentBlend() const
{
	PROFILE_DETAILED(Spline, SplnTrk_GetCurBlend);

	if (!m_bBlendValid)
	{
		m_bBlendValid = true;

		F32 curRealDistance_m = GetCurrentRealDistance_m();

		const CatmullRom* pSpline = m_hSpline.ToCatmullRom();

		m_blend.m_pSplineA = pSpline;
		m_blend.m_realDistanceA_m = curRealDistance_m;
		m_blend.m_piSegmentHintA = &m_iSegmentHint;

		m_blend.m_pSplineB = nullptr;
		m_blend.m_realDistanceB_m = 0.0f;
		m_blend.m_piSegmentHintB = nullptr;

		m_blend.m_blendA2B = 0.0f;

		if (pSpline)
		{
			I32F iPrevious = GetPreviousTransferIndex();
			I32F iCurrent  = GetCurrentTransferIndex();

			if (m_transferBlendDistance_m > 0.01f && iPrevious >= 0)
			{
				TransferRequest& prevTransfer = *const_cast<TransferRequest*>(&m_transferQueue.GetAtRawIndex(iPrevious));
				if (prevTransfer.m_transferType == kSplit)
				{
					// We want to m_blend from the old spline to the new AFTER a split.

					m_blend.m_pSplineA = prevTransfer.m_hDistanceSpline.ToCatmullRom();
					m_blend.m_realDistanceA_m = prevTransfer.m_realTransferDistance_m + curRealDistance_m;
					m_blend.m_piSegmentHintA = &m_iOtherSegmentHint;

					m_blend.m_pSplineB = pSpline;
					m_blend.m_realDistanceB_m = curRealDistance_m;
					m_blend.m_piSegmentHintB = &m_iSegmentHint;

					m_blend.m_blendA2B = curRealDistance_m / m_transferBlendDistance_m;
					if (m_blend.m_blendA2B >= 1.0f)
						prevTransfer.m_transferType = kNone; // retire the old transfer to avoid erroneously blending with it later
					m_blend.m_blendA2B = MinMax01(m_blend.m_blendA2B);

					// A linear ramp pulls the car too far off the new spline.
					m_blend.m_blendA2B = sqrtf(m_blend.m_blendA2B);

					//if (m_blend.m_blendA2B > 0.0f && m_blend.m_blendA2B < 1.0f)
					//{
					//	MsgUser12("0x%p: Blending after a SPLIT, m_blend = %g\n", this, m_blend.m_blendA2B);
					//}
				}
			}
			if (m_transferBlendDistance_m > 0.01f && iCurrent >= 0)
			{
				const TransferRequest& curTransfer = m_transferQueue.GetAtRawIndex(iCurrent);
				if (curTransfer.m_transferType == kJoin)
				{
					// We want to m_blend from the old spline to the new PRIOR TO a join.

					F32 curRealEndDistance_m = pSpline->GetTotalArcLength();
					F32 distanceBeforeEnd_m = curRealEndDistance_m - curRealDistance_m;

					m_blend.m_pSplineA = curTransfer.m_hTransferSpline.ToCatmullRom();
					m_blend.m_realDistanceA_m = curTransfer.m_realTransferDistance_m - distanceBeforeEnd_m;
					m_blend.m_piSegmentHintA = &m_iOtherSegmentHint;

					m_blend.m_pSplineB = pSpline;
					m_blend.m_realDistanceB_m = curRealDistance_m;
					m_blend.m_piSegmentHintB = &m_iSegmentHint;

					m_blend.m_blendA2B = distanceBeforeEnd_m / m_transferBlendDistance_m;
					m_blend.m_blendA2B = MinMax01(m_blend.m_blendA2B);

					// A linear ramp pulls the car too far off the current spline.
					m_blend.m_blendA2B = sqrtf(m_blend.m_blendA2B);

					//if (m_blend.m_blendA2B > 0.0f && m_blend.m_blendA2B < 1.0f)
					//{
					//	MsgUser12("0x%p: Blending prior to a JOIN, m_blend = %g\n", this, m_blend.m_blendA2B);
					//}
				}
				else if (curTransfer.m_transferType == kLaneChange)
				{
					F32 distancePast_m = DetermineDistancePastTransferPoint_m(curTransfer);

					m_blend.m_pSplineA = curTransfer.m_hDistanceSpline.ToCatmullRom();
					m_blend.m_realDistanceA_m = curTransfer.m_realTransferDistance_m + distancePast_m;
					m_blend.m_piSegmentHintA = &m_iOtherSegmentHint;

					m_blend.m_pSplineB = pSpline;
					m_blend.m_realDistanceB_m = curRealDistance_m;
					m_blend.m_piSegmentHintB = &m_iSegmentHint;

					m_blend.m_blendA2B = -distancePast_m / m_transferBlendDistance_m;
					m_blend.m_blendA2B = MinMax01(m_blend.m_blendA2B);
				}
			}
		}
	}
	return m_blend;
}

Point SplineTracker::GetCurrentPoint() const
{
	PROFILE(Spline, SplnTrk_GetCurPoint);

	if (!m_bCurPointValid)
	{
		m_bCurPointValid = true;

		if (m_hSpline.ToCatmullRom())
		{
			const Blend& blend = GetCurrentBlend(); //@DO ON PPU

			//@MOVE TO SPU - collect all relevant splines being tracked, kick off SPU job(s) to run EvaluatePointAtArcLength()
			//@ on each one, send back results which we cache for rest of frame; must run AFTER new virtual distance has been
			//@ set each frame - perhaps that function could queue to SPU jobs.

			if (blend.m_blendA2B > 0.0f && blend.m_blendA2B < 1.0f)
			{
				ALWAYS_ASSERT(blend.m_pSplineA && blend.m_pSplineB);
				Point A = blend.m_pSplineA->EvaluatePointAtArcLength(blend.m_realDistanceA_m, blend.m_piSegmentHintA);
				Point B = blend.m_pSplineB->EvaluatePointAtArcLength(blend.m_realDistanceB_m, blend.m_piSegmentHintB);
				m_curPoint = Lerp(A, B, blend.m_blendA2B);
			}
			else if (blend.m_blendA2B == 0.0f)
			{
				ALWAYS_ASSERT(blend.m_pSplineA);
				m_curPoint = blend.m_pSplineA->EvaluatePointAtArcLength(blend.m_realDistanceA_m, blend.m_piSegmentHintA);
			}
			else
			{
				ALWAYS_ASSERT(blend.m_pSplineB);
				ASSERT(blend.m_blendA2B == 1.0f);
				m_curPoint = blend.m_pSplineB->EvaluatePointAtArcLength(blend.m_realDistanceB_m, blend.m_piSegmentHintB);
			}
		}
		else
		{
			m_curPoint = Point(kOrigin);
		}
	}
	return m_curPoint;
}

Vector SplineTracker::GetCurrentTangent() const
{
	PROFILE(Spline, SplnTrk_GetCurTangent);

	if (!m_bCurTangentValid)
	{
		m_bCurTangentValid = true;

		if (m_hSpline.ToCatmullRom())
		{
			const Blend& blend = GetCurrentBlend();

			if (blend.m_blendA2B > 0.0f && blend.m_blendA2B < 1.0f)
			{
				ALWAYS_ASSERT(blend.m_pSplineA && blend.m_pSplineB);
				Vector A = blend.m_pSplineA->EvaluateTangentAtArcLength(blend.m_realDistanceA_m, blend.m_piSegmentHintA);
				Vector B = blend.m_pSplineB->EvaluateTangentAtArcLength(blend.m_realDistanceB_m, blend.m_piSegmentHintB);
				m_curTangent = Lerp(A, B, blend.m_blendA2B);
			}
			else if (blend.m_blendA2B == 0.0f)
			{
				ALWAYS_ASSERT(blend.m_pSplineA);
				m_curTangent = blend.m_pSplineA->EvaluateTangentAtArcLength(blend.m_realDistanceA_m, blend.m_piSegmentHintA);
			}
			else
			{
				ALWAYS_ASSERT(blend.m_pSplineB);
				ASSERT(blend.m_blendA2B == 1.0f);
				m_curTangent = blend.m_pSplineB->EvaluateTangentAtArcLength(blend.m_realDistanceB_m, blend.m_piSegmentHintB);
			}
		}
		else
		{
			m_curTangent = Vector(kUnitZAxis);
		}
	}
	return m_curTangent;
}

void SplineTracker::GetFutureBlend(float futureDistanceDelta, Blend* const pOutBlend) const
{
	PROFILE(Spline, SplnTrk_GetFutureBlend);
	ASSERT(pOutBlend);
	if (UNLIKELY(!pOutBlend))
		return;

	float futureDistance_m = GetCurrentRealDistance_m() + futureDistanceDelta;

	const CatmullRom* pSpline = m_hSpline.ToCatmullRom();

	pOutBlend->m_pSplineA = pSpline;
	pOutBlend->m_realDistanceA_m = futureDistance_m;
	pOutBlend->m_piSegmentHintA = nullptr; //ignored for now, fill it in if needed

	pOutBlend->m_pSplineB = nullptr;
	pOutBlend->m_realDistanceB_m = 0.0f;
	pOutBlend->m_piSegmentHintB = nullptr;

	pOutBlend->m_blendA2B = 0.0f;

	if (pSpline)
	{
		I32F iPrevious = GetPreviousTransferIndex();
		I32F iCurrent  = GetCurrentTransferIndex();

		//if we have transferred from another spline recently
		if (m_transferBlendDistance_m > 0.01f && iPrevious >= 0)
		{
			const TransferRequest& prevTransfer = m_transferQueue.GetAtRawIndex(iPrevious);
			if (prevTransfer.m_transferType == kSplit)
			{
				// We want to blend from the old spline to the new AFTER a split.
				pOutBlend->m_pSplineA = prevTransfer.m_hDistanceSpline.ToCatmullRom();
				pOutBlend->m_realDistanceA_m = prevTransfer.m_realTransferDistance_m + futureDistance_m;

				pOutBlend->m_pSplineB = pSpline;
				pOutBlend->m_realDistanceB_m = futureDistance_m;

				pOutBlend->m_blendA2B = sqrtf(MinMax01(futureDistance_m / m_transferBlendDistance_m));
			}
		}
		if (m_transferBlendDistance_m > 0.01f && iCurrent >= 0)
		{
			const TransferRequest& curTransfer = m_transferQueue.GetAtRawIndex(iCurrent);
			if (curTransfer.m_transferType == kJoin)
			{
				// We want to blend from the old spline to the new PRIOR to a join.
				F32 curRealEndDistance_m = pSpline->GetTotalArcLength();
				F32 distanceBeforeEnd_m = curRealEndDistance_m - futureDistance_m;

				pOutBlend->m_pSplineA = curTransfer.m_hTransferSpline.ToCatmullRom();
				pOutBlend->m_realDistanceA_m = curTransfer.m_realTransferDistance_m - distanceBeforeEnd_m;
				
				pOutBlend->m_pSplineB = pSpline;
				pOutBlend->m_realDistanceB_m = futureDistance_m;

				pOutBlend->m_blendA2B = sqrtf(MinMax01(distanceBeforeEnd_m / m_transferBlendDistance_m));
			}
			else if (curTransfer.m_transferType == kLaneChange)
			{
				F32 distancePast_m = DetermineDistancePastTransferPoint_m(curTransfer) + futureDistanceDelta;

				pOutBlend->m_pSplineA = curTransfer.m_hDistanceSpline.ToCatmullRom();
				pOutBlend->m_realDistanceA_m = curTransfer.m_realTransferDistance_m + distancePast_m;

				pOutBlend->m_pSplineB = pSpline;
				pOutBlend->m_realDistanceB_m = futureDistance_m;

				pOutBlend->m_blendA2B = MinMax01(-distancePast_m / m_transferBlendDistance_m);
			}
		}
	}
}

Vector SplineTracker::GetFutureTangent(float distAhead) const
{
	PROFILE(Spline, SplnTrk_GetFutureTangent);

	if (m_hSpline.ToCatmullRom())
	{
		Blend blend;
		GetFutureBlend(distAhead, &blend);

		if (blend.m_blendA2B > 0.0f && blend.m_blendA2B < 1.0f)
		{
			ALWAYS_ASSERT(blend.m_pSplineA && blend.m_pSplineB);
			Vector A = blend.m_pSplineA->EvaluateTangentAtArcLength(blend.m_realDistanceA_m, blend.m_piSegmentHintA);
			Vector B = blend.m_pSplineB->EvaluateTangentAtArcLength(blend.m_realDistanceB_m, blend.m_piSegmentHintB);
			return Lerp(A, B, blend.m_blendA2B);
		}
		else if (blend.m_blendA2B == 0.0f)
		{
			ALWAYS_ASSERT(blend.m_pSplineA);
			return blend.m_pSplineA->EvaluateTangentAtArcLength(blend.m_realDistanceA_m, blend.m_piSegmentHintA);
		}
		else
		{
			ALWAYS_ASSERT(blend.m_pSplineB);
			ASSERT(blend.m_blendA2B == 1.0f);
			return blend.m_pSplineB->EvaluateTangentAtArcLength(blend.m_realDistanceB_m, blend.m_piSegmentHintB);
		}
	}
	else
	{
		return Vector(kUnitZAxis);
	}
}

void SplineTracker::DebugDrawTransfer(const TransferRequest& curTransfer) const
{
	const CatmullRom* pDistanceSpline = curTransfer.m_hDistanceSpline.ToCatmullRom();
	if (pDistanceSpline)
	{
		Point pT(pDistanceSpline->EvaluatePointAtArcLength(curTransfer.m_realTransferDistance_m));
		if (m_label != nullptr && strcmp(m_label, "front") == 0)
			pT += Vector(kUnitYAxis) * SCALAR_LC(2.0f);

		g_prim.Draw( DebugCross(pT, 1.0f, kColorYellow, PrimAttrib(0)), kPrimDuration1FramePauseable);

		/*char message[256];
		snprintf(message, sizeof(message)-1, "%.2f -> %s @ %.2f (real %.2f)",
			-1.0f * DetermineDistancePastTransferPoint_m(curTransfer),
			(curTransfer.m_transferType == kSplit ? "SPLIT" : "JOIN"),
			VirtualFromRealDistance_m(curTransfer.m_realTransferDistance_m),
			curTransfer.m_realTransferDistance_m);
		message[sizeof(message)-1] = '\0';

		g_prim.Draw( DebugString(pT, message, kColorYellow ), kPrimDuration1FramePauseable );*/
	}
}

void SplineTracker::DebugDraw(bool force) const
{
	STRIP_IN_FINAL_BUILD;

	PROFILE_DETAILED(DrawDebug, TrainSplineTracker_DebugDraw);

	const CatmullRom* pSpline = m_hSpline.ToCatmullRom();
	if ((g_gameObjectDrawFilter.m_drawSplineTracker || force)
	&&  (!g_pSplineTrackerToDebug || this == g_pSplineTrackerToDebug) && pSpline && pSpline->GetSplineData())
	{
		Point p = GetCurrentPoint();
		Vector t = GetCurrentTangent();

		SMath::Quat q = SMath::QuatFromLookAt(t, SMath::kUnitYAxis);
		Locator loc(p, q);

		g_prim.Draw( DebugCoordAxes(Locator(loc), 5.0f, PrimAttrib(0)), kPrimDuration1FramePauseable );

		I32F iCurrent  = GetCurrentTransferIndex();
		if (iCurrent >= 0)
		{
			const TransferRequest& curTransfer = m_transferQueue.GetAtRawIndex(iCurrent);
			DebugDrawTransfer(curTransfer);
		}

		CatmullRom::DrawOptions drawOptions;
		drawOptions.m_drawPauseable = true;
		pSpline->Draw(&drawOptions);

		const char* splineName = DevKitOnly_StringIdToString(pSpline->GetSplineData()->m_nameId);
		g_prim.Draw(DebugString(p + Vector(kUnitYAxis), splineName, kColorWhite), kPrimDuration1FramePauseable);
		//g_prim.Draw(DebugSphere())
	}
}

// -------------------------------------------------------------------------------------------
// AutoSplineTracker
// -------------------------------------------------------------------------------------------

AutoSplineTracker::AutoSplineTracker() :
	SplineTracker(),
	m_speedSpring(),
	m_targetSpeed_mps(0.0f),
	m_currentSpeed_mps(0.0f),
	m_acceleration(1.0f)
{
}

StringId64 AutoSplineTracker::GetType() const
{
	return SID("AutoSplineTracker");
}

AutoSplineTracker::AutoSplineTracker(const CatmullRom& spline,
									 F32 targetSpeed_mps, F32 initialSpeed_mps,
									 F32 startDistance_m) :
	SplineTracker(spline, startDistance_m),
	m_speedSpring(),
	m_targetSpeed_mps(targetSpeed_mps),
	m_currentSpeed_mps(initialSpeed_mps),
	m_acceleration(1.0f)
{
}

void AutoSplineTracker::SetSpline(const CatmullRom* pSpline, bool loop)
{
	SplineTracker::SetSpline(pSpline);
	SetCurrentSpeed(0.0f);
	m_loop = loop;
}

void AutoSplineTracker::SetTargetSpeed(F32 speed_mps)
{
	m_targetSpeed_mps = speed_mps;
}

void AutoSplineTracker::SetCurrentSpeed(F32 speed_mps)
{
	m_targetSpeed_mps = m_currentSpeed_mps = speed_mps;
	m_speedSpring.Reset();
}

void AutoSplineTracker::SetAcceleration(F32 accel)
{
	m_acceleration = accel;
}

void AutoSplineTracker::Update(bool* pbCrossedEndOfSpline)
{
	// Apply acceleration spring.
	m_currentSpeed_mps = m_speedSpring.Track(m_currentSpeed_mps, m_targetSpeed_mps, GetProcessDeltaTime(), m_acceleration);

	// Update location along spline.
	F32 distance_m = GetCurrentVirtualDistance_m();
	F32 speed_mpf = FromUPS(m_currentSpeed_mps);
	F32 newDistance_m = distance_m + speed_mpf;
	F32 totalRealDistance_m = GetTotalDistance_m();
	SetCurrentVirtualDistance_m(newDistance_m);

	if (g_gameObjectDrawFilter.m_drawSplineTracker)
	{
		MsgCon("Spline dist:   %0.2f\n", newDistance_m);
		MsgCon("Target speed:  %0.2f\n", m_targetSpeed_mps);
		MsgCon("Current speed: %0.2f\n", m_currentSpeed_mps);
	}

	// Check for crossing the start or end of the spline.
	F32 virtualToReal_m = GetVirtualToReal_m();
	F32 realDistance_m = distance_m + virtualToReal_m;
	F32 newRealDistance_m = newDistance_m + virtualToReal_m;

	if (pbCrossedEndOfSpline)
		*pbCrossedEndOfSpline = (realDistance_m < totalRealDistance_m && newRealDistance_m >= totalRealDistance_m);


	if (m_loop)
	{
		newRealDistance_m = fmodf(newRealDistance_m, totalRealDistance_m);
		if (newRealDistance_m < 0.0f)
			newRealDistance_m += totalRealDistance_m;
	}

	if (newRealDistance_m < 0.0f)
	{
		SetCurrentVirtualDistance_m(-virtualToReal_m);
	}
	else if (newRealDistance_m > totalRealDistance_m)
	{
		SetCurrentVirtualDistance_m(totalRealDistance_m-virtualToReal_m);
	}
}

// -------------------------------------------------------------------------------------------
// Script Functions
// -------------------------------------------------------------------------------------------

SCRIPT_FUNC("spline-exists?", DcSplineExistsP)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	StringId64 id = args.NextStringId();
	return ScriptValue(g_splineManager.FindByName(id) != nullptr);
}

SCRIPT_FUNC("is-spline-looping?", DcIsSplineLoopingP)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	StringId64 id = args.NextStringId();
	CatmullRom* pSpline = g_splineManager.FindByName(id);
	if (pSpline)
	{
		return ScriptValue(pSpline->IsLooped());
	}

	return ScriptValue(false);
}

SCRIPT_FUNC("spline-activate", DcSplineActivate)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	CatmullRom* pSpline = args.NextCatmullRom(CatmullRomManager::kIncludeDisabled);
	bool bActive = args.NextBoolean();

	if (pSpline)
	{
		SsVerboseLog(1, "spline-activate '%s' -> %s", pSpline->GetBareName(), (bActive ? "#t" : "#f"));
		pSpline->m_flags.m_inactive = !bActive;

		return ScriptValue(1);
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("spline-distance-spawner", DcSplineDistanceSpawner)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	StringId64 spawnerName = args.NextStringId();
	StringId64 splineName = args.NextStringId();

	if (spawnerName != INVALID_STRING_ID_64
	&&  splineName != INVALID_STRING_ID_64)
	{
		F32 distance_m;

		if (CatmullRom::FindArcLengthClosestToSpawner(distance_m, splineName, spawnerName, "spline-distance-spawner"))
		{
			return ScriptValue(distance_m);
		}
		else
		{
			args.MsgScriptError("Unable to find spawner '%s'.\n", DevKitOnly_StringIdToString(spawnerName));
		}
	}
	else
	{
		args.MsgScriptError("null or empty spawner name.\n");
	}

	return ScriptValue(0.0f);
}

SCRIPT_FUNC("spline-distance-locator", DcSplineDistanceLocator)
{
	SCRIPT_ARG_ITERATOR(args, 3);

	CatmullRom* pSpline = args.NextCatmullRom();
	const BoundFrame *pBf = args.NextBoundFrame();
	bool carefully = args.NextBoolean();

	if (pBf && pSpline)
	{
		int startIndex = -1;
		F32 distance_m = pSpline->FindArcLengthClosestToPoint(pBf->GetTranslationWs(), &startIndex, carefully);
		return ScriptValue(distance_m);
	}

	return ScriptValue(0.0f);
}

SCRIPT_FUNC("spline-locator-at-distance", DcSplineLocatorAtDistance)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	CatmullRom* pSpline = args.NextCatmullRom();
	float distance_m = args.NextFloat();

	if (pSpline && distance_m != NDI_FLT_MAX)
	{
		// Determine translation and tangent at the given location along the spline.
		Point translation;
		Vector dummy, tangent;
		pSpline->EvaluateAtArcLength(distance_m, translation, dummy, tangent);

		// Calculate basis vectors.
		Vector iWs(kUnitXAxis), jWs(kUnitYAxis);
		Vector i(SafeNormalize(Cross(jWs, tangent), iWs));
		Vector j(SafeNormalize(Cross(tangent, i), jWs));

		// Build the matrix.
		Mat44 mtx(i.GetVec4(), j.GetVec4(), tangent.GetVec4(), translation.GetVec4());

		// Return it in bound frame format.
		BoundFrame * pFrame = NDI_NEW(kAllocSingleGameFrame, kAlign16) BoundFrame(Locator(mtx), Binding());
		ALWAYS_ASSERT(args.AllocSucceeded(pFrame));
		return ScriptValue(pFrame);
	}

	return ScriptValue(nullptr);
}

SCRIPT_FUNC("follow-spline", DcFollowSpline)
{
	SCRIPT_ARG_ITERATOR(args, 3);

	NdGameObject* pGo = args.NextGameObject();

	CatmullRom* pSpline = nullptr;
	StringId64 splineName = args.GetStringId(); // don't advance
	if (splineName != INVALID_STRING_ID_64)
		pSpline = args.NextCatmullRom(); // report error only if caller passes a non-zero spline name

	bool loop = args.NextBoolean();

	if (pGo)
	{
		BoxedValue result = SendEvent(SID("follow-spline"), pGo, splineName, loop);
		if (!result.IsValid())
		{
			SplineTracker * pTracker = pGo->GetSplineTracker();

			if (pTracker)
			{
				if (pTracker->GetType() == SID("AutoSplineTracker"))
				{
					AutoSplineTracker* pAutoTracker = PunPtr<AutoSplineTracker*>(pTracker);
					pAutoTracker->SetSpline(pSpline, loop);
				}
				else
				{
					pTracker->SetSpline(pSpline);
				}
				pGo->EnableUpdates();
				return ScriptValue(true);
			}
			else
			{
				args.MsgScriptError("object '%s' is missing can-track-spline=1 in Charter\n", pGo->GetName());
			}
		}
	}

	return ScriptValue(false);
}

SCRIPT_FUNC("set-spline-follow-speed", DcSetSplineFollowSpeed)
{
	SCRIPT_ARG_ITERATOR(args, 3);

	NdGameObject* pGo = args.NextGameObject();
	F32 speed_mps = args.NextFloat();
	bool bInstantaneous = args.NextBoolean();

	if (pGo)
	{
		BoxedValue result = SendEvent(SID("set-spline-follow-speed"), pGo, speed_mps, bInstantaneous);
		if (!result.IsValid())
		{
			AutoSplineTracker * pTracker = PunPtr<AutoSplineTracker*>(pGo->GetSplineTracker());

			if (pTracker)
			{
				if (pTracker->GetType() != SID("AutoSplineTracker"))
				{
					args.MsgScriptError("Object '%s' has a spline tracker, but it is not an AutoSplineTracker -- ask a programmer to help you out", pGo->GetName());
				}
				else
				{
					if (bInstantaneous)
					{
						pTracker->SetCurrentSpeed(speed_mps);
					}
					else
					{
						pTracker->SetTargetSpeed(speed_mps);
					}
				}
			}
			else
			{
				args.MsgScriptError("object '%s' is missing can-track-spline=1 in Charter\n", pGo->GetName());
			}
		}

		return ScriptValue(true);
	}

	return ScriptValue(false);
}

SCRIPT_FUNC("set-spline-follow-acceleration", DcSetSplineFollowAcceleration)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pGo = args.NextGameObject();
	F32 accel = args.NextFloat();

	if (pGo)
	{
		BoxedValue result = SendEvent(SID("set-spline-follow-acceleration"), pGo, accel);
		if (!result.IsValid())
		{
			AutoSplineTracker * pTracker = PunPtr<AutoSplineTracker*>(pGo->GetSplineTracker());

			if (pTracker)
			{
				if (pTracker->GetType() != SID("AutoSplineTracker"))
				{
					args.MsgScriptError("Object '%s' has a spline tracker, but it is not an AutoSplineTracker -- ask a programmer to help you out", pGo->GetName());
				}
				else
				{
					pTracker->SetAcceleration(accel);
				}
			}
			else
			{
				args.MsgScriptError("object '%s' is missing can-track-spline=1 in Charter\n", pGo->GetName());
			}
		}
		return ScriptValue(true);
	}

	return ScriptValue(false);
}

SCRIPT_FUNC("set-spline-follow-distance", DcSetSplineFollowDistance)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pGo = args.NextGameObject();
	F32 distance_m = args.NextFloat();

	if (pGo)
	{
		BoxedValue result = SendEvent(SID("set-spline-follow-distance"), pGo, distance_m);
		if (!result.IsValid())
		{
			SplineTracker * pTracker = pGo->GetSplineTracker();

			if (pTracker)
			{
				pTracker->SetCurrentVirtualDistance_m(distance_m);
			}
			else
			{
				args.MsgScriptError("object '%s' is missing can-track-spline=1 in Charter\n", pGo->GetName());
			}
		}
		return ScriptValue(true);
	}

	return ScriptValue(false);
}

SCRIPT_FUNC("get-object-spline", DcGetObjectSpline)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	Process* pProcess = args.NextProcess();
	
	if (pProcess)
	{
		BoxedValue boxedSplineId = SendEvent(SID("get-spline-id"), pProcess);
		if (boxedSplineId.IsValid())
		{
			return ScriptValue(boxedSplineId.GetStringId());
		}
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

SCRIPT_FUNC("transfer-to-spline", DcTransferToSpline)
{
	SCRIPT_ARG_ITERATOR(args, 3);

	Process* pProcess = args.NextProcess();
	StringId64 splineName = args.NextStringId();
	DC::SplineTransferType dcType = args.NextI32();
	SplineTracker::TransferType transferType = SplineTracker::FromDcTransferType(dcType);

	if (pProcess)
	{
		SendEvent(SID("transfer-to-spline"), pProcess, BoxedValue(splineName), BoxedValue((I32)transferType));
	}

	return ScriptValue(false);
}

SCRIPT_FUNC("spline-transfer-pending?", DcSplineTransferPendingP)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	NdGameObject* pGo = args.NextGameObject();

	if (pGo)
	{
		BoxedValue result = SendEvent(SID("spline-transfer-pending?"), pGo);
		if (result.IsValid())
		{
			return ScriptValue(result.GetAsBool());
		}
		else
		{
			SplineTracker * pTracker = pGo->GetSplineTracker();

			if (pTracker)
			{
				return ScriptValue(pTracker->IsTransferPending());
			}
			else
			{
				args.MsgScriptError("object '%s' is missing can-track-spline=1 in Charter\n", pGo->GetName());
			}
		}
	}

	return ScriptValue(false);
}

SCRIPT_FUNC("wait-spline-distance", DcWaitSplineDistance)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pGo = args.NextGameObject();
	F32 distance_m = args.NextFloat();

	SsInstance* pScriptInst				= args.GetContextScriptInstance();
	SsTrackGroupInstance* pGroupInst	= GetJobSsContext().GetGroupInstance();
	SsTrackInstance* pTrackInst			= GetJobSsContext().GetTrackInstance();

	if (pGo && pScriptInst && pGroupInst && pTrackInst && pGroupInst->GetTrackGroupProcess())
	{
		if (distance_m < NDI_FLT_MAX/2.0f)
		{
			bool waiting = pGo->NotifyAtSplineDistance(distance_m, *pGroupInst->GetTrackGroupProcess(), pTrackInst->GetTrackIndex());

			if (waiting)
			{
				SsVerboseLog(1, "wait-spline-distance '%s' %.4f meters", pGo->GetName(), distance_m);
				s_bCatchingUpIgcAfterScrub = false;		// HACK
				SsTrackGroupInstance::WaitForTrackDone("wait-spline-distance");
				ScriptContinuation::Suspend(argv);
			}
			else
			{
				// Either the request wasn't understood, or the object has already passed
				// this distance along its spline.  Don't wait.
				SsVerboseLog(1, "wait-spline-distance '%s' %.4f meters (already there or not on spline - not waiting)", pGo->GetName(), distance_m);
			}
		}
		else
		{
			args.MsgScriptError("Invalid distance - not waiting.\n");
		}
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("get-spline-distance", DcGetSplineDistance)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pGo = args.NextGameObject();

	if (pGo)
	{
		BoxedValue result = SendEvent(SID("get-spline-distance"), pGo);
		if (result.IsValid())
		{
			return ScriptValue(result.GetAsF32());
		}
		else
		{
			SplineTracker *pSplineTracker = pGo->GetSplineTracker();
			if (pSplineTracker)
			{
				return ScriptValue(pSplineTracker->GetCurrentRealDistance_m());
			}
			else
			{
				args.MsgScriptError("object '%s' is missing can-track-spline=1 in Charter\n", pGo->GetName());
			}
		}
	}

	return ScriptValue(-1.0f);
}

SCRIPT_FUNC("get-pending-spline-distance", DcGetPendingSplineDistance)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pGo = args.NextGameObject();

	if (pGo)
	{
		BoxedValue result = SendEvent(SID("get-pending-spline-distance"), pGo);
		if (result.IsValid())
		{
			return ScriptValue(result.GetAsF32());
		}
	}

	return ScriptValue(-1.0f);
}

SCRIPT_FUNC("get-spline-length", DcGetSplineLength)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	const CatmullRom* pSpline = args.NextCatmullRom();

	if (pSpline)
	{
		float arcLength = pSpline->GetTotalArcLength();
		return ScriptValue(arcLength);
	}
	
	return ScriptValue(0.0f);
}

SCRIPT_FUNC("get-spline-index-count", DcGetSplineIndexCount)
{
	SCRIPT_ARG_ITERATOR(args, 1);

	const CatmullRom* pSpline = args.NextCatmullRom();

	if (pSpline)
	{
		SplineData *pSplineData = const_cast<SplineData*>(pSpline->GetSplineData());
		if (pSplineData == nullptr)
		{
			args.MsgScriptError("Invalid spline data for spline '%s'\n", pSpline->GetBareName());
			return ScriptValue(0);
		}

		return ScriptValue(pSplineData->m_count);
	}
	
	return ScriptValue(0.0f);
}

SCRIPT_FUNC("set-spline-transfer-blend-distance", DcSetSplineTransferBlendDistance)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	NdGameObject* pGo = args.NextGameObject();
	float distance = args.NextFloat();

	if (pGo)
	{
		SplineTracker* pSplineTracker = pGo->GetSplineTracker();
		if (pSplineTracker)
		{
			pSplineTracker->SetTransferBlendDistance_m(distance);
		}
		else
		{
			args.MsgScriptError("object '%s' is missing can-track-spline=1 in Charter\n", pGo->GetName());
		}
	}
	
	return ScriptValue(0.0f);
}


SCRIPT_FUNC("get-spline-point", DcGetSplinePoint)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	CatmullRom *pSpline = args.NextCatmullRom();
	int index = args.NextI32();

	Point *pPos = NDI_NEW(kAllocSingleGameFrame, kAlign16) Point(kZero);

	if (pSpline && pPos)
	{
		SplineData *pSplineData = const_cast<SplineData*>(pSpline->GetSplineData());
		if (pSplineData == nullptr)
		{
			args.MsgScriptError("Invalid spline data for spline '%s'\n", pSpline->GetBareName());
			return ScriptValue(0);
		}

		if (index < 0 || index >= pSplineData->m_count)
		{
			args.MsgScriptError("Invalid index (%d) for spline '%s' with %d verts\n", index, pSpline->GetBareName(), pSplineData->m_count);
			return ScriptValue(0);
		}

		*pPos = pSpline->GetControlPointLocator(index).GetTranslation();
	}
	
	return ScriptValue(pPos);
}

SCRIPT_FUNC("set-spline-point", DcSetSplinePoint)
{
	SCRIPT_ARG_ITERATOR(args, 3);

	CatmullRom *pSpline = args.NextCatmullRom();
	int index = args.NextI32();
	Point pos = args.NextPoint();

	if (pSpline)
	{
		SplineData *pSplineData = const_cast<SplineData*>(pSpline->GetSplineData());
		if (pSplineData == nullptr)
		{
			args.MsgScriptError("Invalid spline data for spline '%s'\n", pSpline->GetBareName());
			return ScriptValue(0);
		}

		if (index < 0 || index >= pSplineData->m_count)
		{
			args.MsgScriptError("Invalid index (%d) for spline '%s' with %d verts\n", index, pSpline->GetBareName(), pSplineData->m_count);
			return ScriptValue(0);
		}

		// TODO: Need to fix for when spline is parented to something - RyanB
		pSplineData->m_verts[index].SetTranslation(pos);
		pSpline->RecomputeSplineData();
	}
	
	return ScriptValue(0);
}

SCRIPT_FUNC("get-spline-point-locator", DcGetSplinePointLocator)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	CatmullRom *pSpline = args.NextCatmullRom();
	int index = args.NextI32();

	BoundFrame *pLoc = NDI_NEW(kAllocSingleGameFrame, kAlign16) BoundFrame(kIdentity);

	if (pSpline && pLoc)
	{
		SplineData *pSplineData = const_cast<SplineData*>(pSpline->GetSplineData());
		if (pSplineData == nullptr)
		{
			args.MsgScriptError("Invalid spline data for spline '%s'\n", pSpline->GetBareName());
			return ScriptValue(0);
		}

		if (index < 0 || index >= pSplineData->m_count)
		{
			args.MsgScriptError("Invalid index (%d) for spline '%s' with %d verts\n", index, pSpline->GetBareName(), pSplineData->m_count);
			return ScriptValue(0);
		}

		
		*pLoc = pSpline->GetControlPointLocator(index);
	}

	return ScriptValue(pLoc);
}

SCRIPT_FUNC("set-spline-point-locator", DcSetSplinePointLocator)
{
	SCRIPT_ARG_ITERATOR(args, 3);

	CatmullRom *pSpline = args.NextCatmullRom();
	int index = args.NextI32();
	const BoundFrame *pLoc = args.NextBoundFrame();

	if (pSpline && pLoc)
	{
		SplineData *pSplineData = const_cast<SplineData*>(pSpline->GetSplineData());
		if (pSplineData == nullptr)
		{
			args.MsgScriptError("Invalid spline data for spline '%s'\n", pSpline->GetBareName());
			return ScriptValue(0);
		}

		if (index < 0 || index >= pSplineData->m_count)
		{
			args.MsgScriptError("Invalid index (%d) for spline '%s' with %d verts\n", index, pSpline->GetBareName(), pSplineData->m_count);
			return ScriptValue(0);
		}

		// TODO: Need to fix for when spline is parented to something - RyanB
		pSplineData->m_verts[index] = pLoc->GetLocator();
		pSpline->RecomputeSplineData();
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("set-spline-type", DcSetSplineType)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	CatmullRom *pSpline = args.NextCatmullRom();
	StringId64 splineType = args.NextStringId();

	if (pSpline)
	{
		SplineData *pSplineData = const_cast<SplineData*>(pSpline->GetSplineData());
		if (pSplineData == nullptr || pSplineData->m_pEntityDB == nullptr)
		{
			args.MsgScriptError("Spline %s: Invalid spline data\n", pSpline->GetBareName());
			return ScriptValue(0);
		}

		bool tagFound = false;
		EntityDB::RecordMap::const_iterator it = pSplineData->m_pEntityDB->Begin();
		EntityDB::RecordMap::const_iterator itEnd = pSplineData->m_pEntityDB->End();
		while (it != itEnd)
		{
			if (it->first == SID("type") || it->first == SID("Type") || it->first == SID("TYPE"))
			{
				*reinterpret_cast<StringId64*>(const_cast<void *>(it->second->m_pData)) = splineType;
				tagFound = true;
			}
			++it;
		}

		if (!tagFound)
		{
			args.MsgScriptError("Spline %s: No spline type tag found. Set spline type tag in Charter before calling set-spline-type.\n", pSpline->GetBareName());
			return ScriptValue(0);
		}

		pSplineData->m_type = splineType;
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("get-closest-spline-index", DcGetClosestSplineIndex)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	CatmullRom *pSpline = args.NextCatmullRom();
	const BoundFrame *pBoundFrame = args.NextBoundFrame();

	int closestIndex = 0;
	if (pSpline && pBoundFrame)
	{
		SplineData *pSplineData = const_cast<SplineData*>(pSpline->GetSplineData());
		if (pSplineData == nullptr)
		{
			args.MsgScriptError("Invalid spline data for spline '%s'\n", pSpline->GetBareName());
			return ScriptValue(0);
		}

		float closestLenSq = kLargestFloat;
		for (int i=0; i<pSplineData->m_count; i++)
		{
			float lenSq = LengthSqr(pBoundFrame->GetTranslation() - pSpline->GetControlPointLocator(i).GetTranslation());
			if (lenSq < closestLenSq)
			{
				closestIndex = i;
				closestLenSq = lenSq;
			}
		}
	}
	
	return ScriptValue(closestIndex);
}

SCRIPT_FUNC("get-closest-spline-point-to-segment", DcGetClosestSplinePointToSegemnt)
{
	SCRIPT_ARG_ITERATOR(args, 2);

	CatmullRom *pSpline = args.NextCatmullRom();
	const Point segStart = args.NextPoint();
	const Point segEnd = args.NextPoint();
	Point *pLoc = NDI_NEW(kAllocSingleGameFrame, kAlign16) Point(kOrigin);
	
	if (pSpline && pLoc)
	{
		*pLoc = pSpline->FindClosestPointOnSpline(Segment(segStart, segEnd));
		return ScriptValue(pLoc);
	}

	return ScriptValue(nullptr);
}



SCRIPT_FUNC("spline-tag-specified?", DcSplineTagSpecifiedP)
{
	SCRIPT_ARG_ITERATOR(args, 3);
	StringId64 tagName = args.NextStringId();
	StringId64 splineName = args.NextStringId();
	I32 index = args.NextI32();

	CatmullRom* pSpline = g_splineManager.FindByName(splineName);
	if (!pSpline)
	{
		SsVerboseLog(1, "spline-tag-specified?: Couldn't find spline %s %i", DevKitOnly_StringIdToString(splineName), index);
		return ScriptValue(false);
	}

	const SplineData* pData = pSpline->GetSplineData();
	if (!pData)
	{
		SsVerboseLog(1, "spline-tag-specified?: spline %s has no point data", DevKitOnly_StringIdToString(splineName));
		return ScriptValue(false);
	}

	if (index >= 0)
	{
		I32 propIndex = pData->GetPointPropIndex(tagName, 0xFFFFFFFF);
		if (propIndex < 0)
		{
			SsVerboseLog(1, "spline-tag-symbol: Couldn't find tag %s on spline %s", DevKitOnly_StringIdToString(tagName), DevKitOnly_StringIdToString(splineName));
			return ScriptValue(false);
		}

		const U8* pValue = pData->GetPointValue(propIndex, index);
		if (!pValue || *(U64*)pValue == SplinePointData::kInvalidPointDataValue)
		{
			SsVerboseLog(1, "spline-tag-symbol: tag %s not specified for spline %s point %i", DevKitOnly_StringIdToString(tagName), DevKitOnly_StringIdToString(splineName), index);
			return ScriptValue(false);
		}

		return ScriptValue(true);
	}
	else
	{
		return ScriptValue(pData->HasTagData(tagName));
	}
}

SCRIPT_FUNC("spline-tag-symbol", DcSplineTagSymbol)
{
	SCRIPT_ARG_ITERATOR(args, 3);
	StringId64 tagName = args.NextStringId();
	StringId64 splineName = args.NextStringId();
	I32 index = args.NextI32();

	CatmullRom* pSpline = g_splineManager.FindByName(splineName);
	if (!pSpline)
	{
		SsVerboseLog(1, "spline-tag-symbol: Couldn't find spline %s %i", DevKitOnly_StringIdToString(splineName), index);
		return ScriptValue(INVALID_STRING_ID_64);
	}

	const SplineData* pData = pSpline->GetSplineData();
	if (!pData)
	{
		SsVerboseLog(1, "spline-tag-symbol: spline %s has no point data", DevKitOnly_StringIdToString(splineName));
		return ScriptValue(INVALID_STRING_ID_64);
	}

	if (index >= 0)
	{
		I32 propIndex = pData->GetPointPropIndex(tagName, (1 << SplinePointData::kPropTypeSID) | (1 << SplinePointData::kPropTypeU32));
		if (propIndex < 0)
		{
			SsVerboseLog(1, "spline-tag-symbol: Couldn't find tag %s on spline %s", DevKitOnly_StringIdToString(tagName), DevKitOnly_StringIdToString(splineName));
			return ScriptValue(INVALID_STRING_ID_64);
		}

		const U8* pValue = pData->GetPointValue(propIndex, index);
		if (!pValue || *(U64*)pValue == SplinePointData::kInvalidPointDataValue)
		{
			SsVerboseLog(1, "spline-tag-symbol: tag %s not specified for spline %s point %i", DevKitOnly_StringIdToString(tagName), DevKitOnly_StringIdToString(splineName), index);
			return ScriptValue(INVALID_STRING_ID_64);
		}

		return ScriptValue(*(U32*)pValue);
	}
	else
	{
		return ScriptValue(pData->GetTagData<StringId64>(tagName, INVALID_STRING_ID_64));
	}

	return ScriptValue(INVALID_STRING_ID_64);
}

SCRIPT_FUNC("spline-tag-float", DcSplineTagFloat)
{
	SCRIPT_ARG_ITERATOR(args, 3);
	StringId64 tagName = args.NextStringId();
	StringId64 splineName = args.NextStringId();
	I32 index = args.NextI32();

	CatmullRom* pSpline = g_splineManager.FindByName(splineName);
	if (!pSpline)
	{
		SsVerboseLog(1, "spline-tag-float: Couldn't find spline %s %i", DevKitOnly_StringIdToString(splineName), index);
		return ScriptValue(0.0f);
	}

	const SplineData* pData = pSpline->GetSplineData();
	if (!pData)
	{
		SsVerboseLog(1, "spline-tag-float: spline %s has no point data", DevKitOnly_StringIdToString(splineName));
		return ScriptValue(0.0f);
	}	

	if (index >= 0)
	{
		I32 propIndex = pData->GetPointPropIndex(tagName, (1 << SplinePointData::kPropTypeF32));
		if (propIndex < 0)
		{
			SsVerboseLog(1, "spline-tag-symbol: Couldn't find tag %s on spline %s", DevKitOnly_StringIdToString(tagName), DevKitOnly_StringIdToString(splineName));
			return ScriptValue(INVALID_STRING_ID_64);
		}

		const U8* pValue = pData->GetPointValue(propIndex, index);
		if (!pValue || *(U64*)pValue == SplinePointData::kInvalidPointDataValue)
		{
			SsVerboseLog(1, "spline-tag-symbol: tag %s not specified for spline %s point %i", DevKitOnly_StringIdToString(tagName), DevKitOnly_StringIdToString(splineName), index);
			return ScriptValue(INVALID_STRING_ID_64);
		}

		return ScriptValue(*(U32*)pValue);
	}
	else
	{
		return ScriptValue(pData->GetTagData<float>(tagName, 0.0f));
	}

	return ScriptValue(0.0f);
}

SCRIPT_FUNC("spline-tag-int32", DcSplineTagInt32)
{
	SCRIPT_ARG_ITERATOR(args, 3);
	StringId64 tagName = args.NextStringId();
	StringId64 splineName = args.NextStringId();
	I32 index = args.NextI32();

	CatmullRom* pSpline = g_splineManager.FindByName(splineName);
	if (!pSpline)
	{
		SsVerboseLog(1, "spline-tag-int32: Couldn't find spline %s %i", DevKitOnly_StringIdToString(splineName), index);
		return ScriptValue(0);
	}

	const SplineData* pData = pSpline->GetSplineData();
	if (!pData)
	{
		SsVerboseLog(1, "spline-tag-int32: spline %s has no point data", DevKitOnly_StringIdToString(splineName));
		return ScriptValue(0);
	}

	if (index >= 0)
	{
		I32 propIndex = pData->GetPointPropIndex(tagName, (1 << SplinePointData::kPropTypeI32) | (1 << SplinePointData::kPropTypeU32));
		if (propIndex < 0)
		{
			SsVerboseLog(1, "spline-tag-symbol: Couldn't find tag %s on spline %s", DevKitOnly_StringIdToString(tagName), DevKitOnly_StringIdToString(splineName));
			return ScriptValue(INVALID_STRING_ID_64);
		}

		const U8* pValue = pData->GetPointValue(propIndex, index);
		if (!pValue || *(U64*)pValue == SplinePointData::kInvalidPointDataValue)
		{
			SsVerboseLog(1, "spline-tag-symbol: tag %s not specified for spline %s point %i", DevKitOnly_StringIdToString(tagName), DevKitOnly_StringIdToString(splineName), index);
			return ScriptValue(INVALID_STRING_ID_64);
		}

		return ScriptValue(*(U32*)pValue);
	}
	else
	{
		return ScriptValue(pData->GetTagData<float>(tagName, 0.0f));
	}

	return ScriptValue(0);
}
