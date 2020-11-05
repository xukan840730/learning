/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/anim/gesture-cache.h"

#include "corelib/containers/hashtable.h"
#include "corelib/math/adaptive-precision.h"
#include "corelib/memory/allocator-heap.h"
#include "corelib/memory/memory-map.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/system/read-write-atomic-lock.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-mgr.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/math/delaunay.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/netbridge/mail.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/anim/nd-gesture-util.h"
#include "gamelib/level/art-item-anim.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/scriptx/h/nd-gesture-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
GestureCache g_gestureCache;

static const StringId64 kApRefGestureDirId = SID("apReference-gestureDir");
static const StringId64 kApRefGestureDirFcX = SID("gestureDir.euler_x");
static const StringId64 kApRefGestureDirFcY = SID("gestureDir.euler_y");
static const StringId64 kApRefGestureDirFcZ = SID("gestureDir.euler_z");
static const StringId64 kApRefGestureDirFc[] = { kApRefGestureDirFcX,
												 kApRefGestureDirFcY,
												 kApRefGestureDirFcZ,
												 INVALID_STRING_ID_64 };

static const StringId64 kApRefNoBlendId = SID("apReference-noBlend");
static const float kLinearAngleThresholdDeg = 3.0f;

/// --------------------------------------------------------------------------------------------------------------- ///
static U32 ComputeNumAnimsToAddForPair(const ArtItemAnim* pAnim,
									   const DC::GestureAnimPair& animPair,
									   float* pFramesOut,
									   U32 maxFramesOut)
{
	if ((maxFramesOut == 0) || !pAnim || !pFramesOut)
	{
		return 0;
	}

	if (!animPair.m_frameRange)
	{
		pFramesOut[0] = -1.0f;
		return 1;
	}

	const I64 maxValidFrame = pAnim->m_pClipData->m_numTotalFrames;

	const I64 frameStart = Min(maxValidFrame, Max(I64(animPair.m_frameRange->m_val0), I64(0)));

	const I64 numFrames = Min(Min(maxValidFrame + 1, I64(animPair.m_frameRange->m_val1)) - frameStart,
							  I64(maxFramesOut));

	for (I64 i = 0; i < numFrames; ++i)
	{
		pFramesOut[i] = frameStart + i;
	}

	return numFrames;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static SphericalCoords GetGestureDirForAnim(const GestureCache::CacheKey& key, 
											const GestureCache::CacheData* pData, 
											const GestureCache::CachedAnim& anim)
{
	const GestureCache::CachedSpace& space = pData->m_animSpace;

	SphericalCoords ret = anim.m_dir;

	Locator gestureLoc = Locator(kIdentity);

	const float evalPhase = Limit01(anim.m_phase);

	const ArtItemAnim* pAnim = anim.m_hSlerpAnim.ToArtItem();
	if (!pAnim)
	{
		pAnim = anim.m_hAddAnim.ToArtItem();
	}

	bool fcValid = false;

	if (pAnim && anim.m_hasGestureDirFc)
	{
		EvaluateChannelParams params;
		params.m_pAnim = pAnim;
		params.m_phase = evalPhase;

		float ex = 0.0f;
		float ey = 0.0f;
		float ez = 0.0f;

		bool valid = true;

		params.m_channelNameId = kApRefGestureDirFcX;
		valid = valid && EvaluateCompressedFloatChannel(&params, &ex);

		params.m_channelNameId = kApRefGestureDirFcY;
		valid = valid && EvaluateCompressedFloatChannel(&params, &ey);
		
		params.m_channelNameId = kApRefGestureDirFcZ;
		valid = valid && EvaluateCompressedFloatChannel(&params, &ez);

		if (valid)
		{
			Locator gestureSpace(kIdentity);

			if ((space.m_channelId != INVALID_STRING_ID_64)
				&& EvaluateChannelInAnim(key.m_skelId, pAnim, space.m_channelId, evalPhase, &gestureSpace))
			{
				gestureSpace = gestureSpace.TransformLocator(space.m_offsetLs);
			}

			float sx = 0.0f;
			float sy = 0.0f;
			float sz = 0.0f;
			gestureSpace.Rot().GetEulerAngles(sx, sy, sz, Quat::RotationOrder::kZXY);

			const float thetaSign = key.m_flipped ? 1.0f : -1.0f;

			ret = SphericalCoords::FromThetaPhi(thetaSign * (ey - sy), -1.0f * (ex - sx));

			fcValid = true;
		}
	}

	if (!fcValid && pAnim && anim.m_hasGestureDirAp
		&& EvaluateChannelInAnim(key.m_skelId, pAnim, kApRefGestureDirId, evalPhase, &gestureLoc))
	{
		Locator gestureSpace(kIdentity);

		if ((space.m_channelId != INVALID_STRING_ID_64)
			&& EvaluateChannelInAnim(key.m_skelId, pAnim, space.m_channelId, evalPhase, &gestureSpace))
		{
			gestureLoc = gestureSpace.TransformLocator(space.m_offsetLs).UntransformLocator(gestureLoc);
		}

		ret = Gesture::ApRefToCoords(gestureLoc.Rot(), key.m_flipped);
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ShouldAddExtraNode(const GestureCache::CacheData* pData, U32 sourceIndex, const SphericalCoords& newDir)
{
	const float desTheta = newDir.Theta();
	const float desPhi = newDir.Phi();

	bool alreadyCovered = false;

	for (U32F iPair = 0; iPair < pData->m_numGestureAnims; ++iPair)
	{
		if (iPair == sourceIndex)
			continue;

		const GestureCache::CachedAnim& anim = pData->m_cachedAnims[iPair];
		
		const float deltaTheta = Abs(anim.m_dir.Theta() - desTheta);
		const float deltaPhi = Abs(anim.m_dir.Phi() - desPhi);

		if ((deltaTheta < 45.0f) && (deltaPhi < 45.0f))
		{
			alreadyCovered = true;
			break;
		}
	}

	return !alreadyCovered;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void AddExtraNodes(GestureCache::CacheData* pData)
{
	const U32 originalNumAnims = pData->m_numGestureAnims;

	for (U32F iPair = 0; iPair < originalNumAnims; ++iPair)
	{
		const GestureCache::CachedAnim& anim = pData->m_cachedAnims[iPair];

		const float theta = anim.m_dir.Theta();
		const float phi = anim.m_dir.Phi();

		if (Abs(phi) <= 85.0f)
		{
			continue;
		}

		GestureCache::CachedAnim newAnim = anim;

		float newTheta[2] ={0.0f, 0.0f};

		if (Abs(theta) < 45.0f)
		{
			newTheta[0] = -90.0f;
			newTheta[1] = 90.0f;
		}
		else if (theta >= 45.0f)
		{
			newTheta[0] = -90.0f;
			newTheta[1] = 0.0f;
		}
		else if (theta <= -45.0f)
		{
			newTheta[1] = 0.0f;
			newTheta[0] = 90.0f;
		}

		for (int i = 0; i < 2; ++i)
		{
			newAnim.m_dir = SphericalCoords::FromThetaPhi(newTheta[i], phi);
			newAnim.m_extra = true;

			if (ShouldAddExtraNode(pData, iPair, newAnim.m_dir))
			{
				if (pData->m_numGestureAnims >= Gesture::kMaxGestureNodeAnims)
				{
					return;
				}

				pData->m_cachedAnims[pData->m_numGestureAnims] = newAnim;
				++pData->m_numGestureAnims;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool GetNoBlendDirForAnim(const SkeletonId skelId, const ArtItemAnim* pAnim, bool flipped, Angle* pOutAngle) 
{
	if (!pAnim)
	{
		return false;
	}

	ndanim::JointParams noBlendParams;
	if (!EvaluateChannelInAnim(skelId, pAnim, kApRefNoBlendId, 0.0f, &noBlendParams))
	{
		return false;
	}

	const SphericalCoords noBlendCoords = Gesture::ApRefToCoords(noBlendParams.m_quat, flipped);

	*pOutAngle = Angle(noBlendCoords.Theta());
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ShouldRejectTriangle(const TriangleIndices* pIndices,
								 const U32F iTri,
								 const Vec2* pPoints,
								 const U32F numPoints)
{
	const U32F iA = pIndices[iTri].m_iA;
	const U32F iB = pIndices[iTri].m_iB;
	const U32F iC = pIndices[iTri].m_iC;

	if ((iA >= numPoints) || (iB >= numPoints) || (iC >= numPoints))
		return true;

	const Point posA = Point(pPoints[iA].x, pPoints[iA].y, 0.0f);
	const Point posB = Point(pPoints[iB].x, pPoints[iB].y, 0.0f);
	const Point posC = Point(pPoints[iC].x, pPoints[iC].y, 0.0f);

	const Vector vBA = posA - posB;
	const Vector vBC = posC - posB;

	const Vector crossP = Cross(vBA, vBC);
	const float triArea = 0.5f * Length(crossP);

	if (triArea < 0.1f)
		return true;

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static GestureCache::CachedSpace CacheSpaceFromDcDef(const DC::GestureSpaceDef* pDcDef)
{
	GestureCache::CachedSpace ret;

	ret.m_channelId = INVALID_STRING_ID_64;
	ret.m_jointId	= INVALID_STRING_ID_64;
	ret.m_offsetLs	= kIdentity;

	if (pDcDef)
	{
		ret.m_channelId = pDcDef->m_channelId;
		ret.m_jointId = pDcDef->m_jointId;

		if (pDcDef->m_offset)
		{
			ret.m_offsetLs = *pDcDef->m_offset;
		}
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const Clock* GetGestureClock()
{
	 return EngineComponents::GetNdFrameState()->GetClock(kGameClock);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureCache::Init(size_t maxEntries)
{
	AllocateJanitor jj(kAllocGestureCache, FILE_LINE_FUNC);

	Memory::Allocator* pAllocator = Memory::GetAllocator(kAllocGestureCache);

	const size_t initialFree = pAllocator->GetFreeSize();

	m_keyTable.Init(maxEntries, FILE_LINE_FUNC);
	m_dataHeap.Init(maxEntries, sizeof(CacheData), kAllocInvalid, kAlign16, FILE_LINE_FUNC);

	const size_t postFree = pAllocator->GetFreeSize();

	MsgAnim("GestureCache allocated %d bytes\n", initialFree - postFree);

	m_animSentinal.Refresh();

	m_rebuildRequested = false;
	m_debugEraseInProgress = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool FindUnusedEntry(U8* pElement, uintptr_t userData, bool free)
{
	if (!pElement || free)
		return true;

	GestureCache::CacheData** ppDataOut = (GestureCache::CacheData**)userData;

	GestureCache::CacheData* pData = (GestureCache::CacheData*)pElement;

	if (pData->m_referenceCount.Get() == 0)
	{
		const Clock* pClock = GetGestureClock();

		if (pClock->TimePassed(Frames(3), pData->m_creationTime))
		{
			*ppDataOut = pData;
			return false;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool FindBadEntry(U8* pElement, uintptr_t userData, bool free)
{
	if (!pElement || free)
		return true;

	GestureCache::CacheData** ppDataOut = (GestureCache::CacheData**)userData;

	GestureCache::CacheData* pData = (GestureCache::CacheData*)pElement;

	if (pData->m_isBad)
	{
		if (pData->m_referenceCount.Get() == 0)
		{
			*ppDataOut = pData;
			return false;
		}
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void AnimForceRefreshSnapshots(FgAnimData* pAnimData, uintptr_t data)
{
	if (pAnimData && pAnimData->m_pAnimControl)
	{
		pAnimData->m_pAnimControl->ReloadScriptData();
		pAnimData->m_pAnimControl->NotifyAnimTableUpdated();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
bool GestureCache::RefreshEntries(U8* pElement, uintptr_t userData, bool free)
{
	if (!pElement || free)
		return true;

	GestureCache::CacheData* pData = (GestureCache::CacheData*)pElement;
	GestureCache& gestureCache = *(GestureCache*)userData;

	gestureCache.RefreshData(pData);

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureCache::Update()
{
	// if we're at a certain capacity percentage try evicting a zero ref entry every frame
	{
		AtomicLockJanitorRead readLock(&m_tableLock, FILE_LINE_FUNC);
		const float fillRate = float(m_dataHeap.GetNumElementsUsed()) / float(Max(m_dataHeap.GetNumElements(), I64(1)));

		const float startPercentage = g_animOptions.m_gestureCache.m_evictionTriggerMax;
		const float stopPercentage = g_animOptions.m_gestureCache.m_evictionTriggerMin;

		if (fillRate >= startPercentage)
		{
			m_evictionActive = true;
		}
		else if (fillRate <= stopPercentage)
		{
			m_evictionActive = false;
		}
	}

	if (m_evictionActive || FALSE_IN_FINAL_BUILD(g_animOptions.m_gestureCache.m_alwaysTryEviction))
	{
		AtomicLockJanitorWrite writeLock(&m_tableLock, FILE_LINE_FUNC);
		GestureCache::CacheData* pDataToFree = nullptr;
		m_dataHeap.ForEachElement(FindUnusedEntry, (uintptr_t)&pDataToFree);

		if (pDataToFree)
		{
			EraseData(pDataToFree);
		}
		else
		{
			m_evictionActive = false;
		}
	}

	if (!m_animSentinal.IsValid())
	{
		PROFILE_ACCUM(GestureCache_RefreshEntries);
		AtomicLockJanitorWrite writeLock(&m_tableLock, FILE_LINE_FUNC);

		m_dataHeap.ForEachElement(RefreshEntries, (uintptr_t)this);
		m_animSentinal.Refresh();
	}

	if (FALSE_IN_FINAL_BUILD(m_rebuildRequested || g_animOptions.m_gestureCache.m_rebuildCache))
	{
		{
			AtomicLockJanitorWrite writeLock(&m_tableLock, FILE_LINE_FUNC);

			m_keyTable.Clear();
			m_dataHeap.Clear();
		}

		m_debugEraseInProgress = true;
		EngineComponents::GetAnimMgr()->ForAllUsedAnimData(AnimForceRefreshSnapshots, 0);
		m_debugEraseInProgress = false;

		m_rebuildRequested = false;
		g_animOptions.m_gestureCache.m_rebuildCache = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureCache::Shutdown()
{
	AllocateJanitor jj(kAllocGestureCache, FILE_LINE_FUNC);

	AtomicLockJanitorWrite writeLock(&m_tableLock, FILE_LINE_FUNC);

	m_keyTable.Destroy(FILE_LINE_FUNC);
	m_dataHeap.Shutdown();
}

/// --------------------------------------------------------------------------------------------------------------- ///
GestureCache::CacheData* GestureCache::TryCacheData(const CacheKey& key,
													const DC::GestureDef* pDcGesture,
													U32 hierarchyId)
{
	PROFILE_ACCUM(GestureCache_TryCacheData);

	if (!pDcGesture)
		return nullptr;

	CacheData* pData = nullptr;
	bool construct = false;

	{
		AtomicLockJanitorRead readLock(&m_tableLock, FILE_LINE_FUNC);

		KeyTable::ConstIterator itr = m_keyTable.Find(key);

		if (itr != m_keyTable.End())
		{
			const I32F dataIndex = itr->m_data;
			ANIM_ASSERT((dataIndex >= 0) && (dataIndex < m_dataHeap.GetNumElements()));

			pData = (CacheData*)m_dataHeap.GetElement(dataIndex);
			ANIM_ASSERT(pData);
		}
	}

	if (!pData)
	{
		AtomicLockJanitorWrite writeLock(&m_tableLock, FILE_LINE_FUNC);
	
		// sanity check to see if a competing thread got here before we did
		KeyTable::ConstIterator itr = m_keyTable.Find(key);

		if (itr != m_keyTable.End())
		{
			const I32F dataIndex = itr->m_data;
			ANIM_ASSERT((dataIndex >= 0) && (dataIndex < m_dataHeap.GetNumElements()));

			pData = (CacheData*)m_dataHeap.GetElement(dataIndex);
		}
		else
		{
			pData = (CacheData*)m_dataHeap.Alloc();
			if (!pData)
			{
				// cache is full!
				return nullptr;
			}

			NDI_NEW (&pData->m_dataLock) NdRwAtomicLock64;

			// prevent anyone else from modifying this data until we construct it (but still let us release the table write lock)
			pData->m_dataLock.AcquireWriteLock(FILE_LINE_FUNC);
			pData->m_creationTime = GetGestureClock()->GetCurTime();

			construct = true;

			const I64 newBlockIndex = m_dataHeap.GetBlockIndex(pData);
			m_keyTable.Set(key, newBlockIndex);
		}
	}

	ANIM_ASSERT(pData);

	if (construct)
	{
		ConstructData(key, pDcGesture, hierarchyId, pData);

		pData->m_dataLock.ReleaseWriteLock(FILE_LINE_FUNC); // release the hounds!
	}

	pData->m_dataLock.AcquireReadLock(FILE_LINE_FUNC);
	pData->m_referenceCount.Add(1);

	return pData;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureCache::ReleaseData(CacheData* pData)
{
	pData->m_referenceCount.Add(-1);
	pData->m_dataLock.ReleaseReadLock(FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void CacheAnim(const GestureCache::CacheKey& key,
					  const DC::GestureAnimPair& animPair,
					  ArtItemAnimHandle hSlerpAnim,
					  StringId64 slerpAnimId,
					  ArtItemAnimHandle hAddAnim,
					  StringId64 addAnimId,
					  GestureCache::CacheData* pData)
{
	ANIM_ASSERT((slerpAnimId != INVALID_STRING_ID_64) || (addAnimId != INVALID_STRING_ID_64));

	const ArtItemAnim* pSlerpAnim = hSlerpAnim.ToArtItem();
	const ArtItemAnim* pAddAnim = hAddAnim.ToArtItem();

	if (slerpAnimId != INVALID_STRING_ID_64 && !pSlerpAnim)
	{
		pData->m_isBad = true;
		return;
	}

	if ((addAnimId != INVALID_STRING_ID_64) & !pAddAnim)
	{
		pData->m_isBad = true;
		return;
	}

	const ArtItemAnim* pAnim = pSlerpAnim ? pSlerpAnim : pAddAnim;

	ANIM_ASSERT(pAnim);

	float framesForEntries[Gesture::kMaxGestureNodeAnims];
	const U32F numAddedAnims = ComputeNumAnimsToAddForPair(pAnim,
														   animPair,
														   framesForEntries,
														   Gesture::kMaxGestureNodeAnims);

	for (U32F iAdded = 0; iAdded < numAddedAnims; ++iAdded)
	{
		const float theta = Limit(key.m_flipped ? -animPair.m_hAngle : animPair.m_hAngle, -179.5f, 179.5f);
		const float phi = Limit(animPair.m_vAngle, -89.5f, 89.5f);

		if (pData->m_numGestureAnims >= Gesture::kMaxGestureNodeAnims)
		{
			break;
		}

		GestureCache::CachedAnim& newAnim = pData->m_cachedAnims[pData->m_numGestureAnims];

		newAnim.m_hSlerpAnim	= hSlerpAnim;
		newAnim.m_slerpBaseName = slerpAnimId;
		newAnim.m_hAddAnim		= hAddAnim;
		newAnim.m_addBaseName	= addAnimId;

		newAnim.m_hasGestureDirAp = AnimHasChannel(pAnim, kApRefGestureDirId);
		newAnim.m_hasGestureDirFc = AnimHasChannels(pAnim, kApRefGestureDirFc);

		newAnim.m_frame = framesForEntries[iAdded];
		newAnim.m_phase = -1.0f;
		newAnim.m_dir = SphericalCoords::FromThetaPhi(theta, phi);
		newAnim.m_extra = false;
		newAnim.m_wrapAround = false;

		++pData->m_numGestureAnims;

		if (pData->m_wrap360)
		{
			newAnim = pData->m_cachedAnims[pData->m_numGestureAnims - 1];
			newAnim.m_wrapAround = true;
			++pData->m_numGestureAnims;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool DoLookup(GestureCache::CachedAnim& anim,
					 bool slerp,
					 SkeletonId skelId,
					 U32 hierarchyId,
					 StringId64& lastAnimId,
					 ArtItemAnimHandle& hLastLookup)
{
	const StringId64 animId = slerp ? anim.m_slerpBaseName : anim.m_addBaseName;

	if (animId == INVALID_STRING_ID_64)
	{
		return true;
	}

	ArtItemAnimHandle hAnim;

	if (animId == lastAnimId)
	{
		hAnim = hLastLookup;
	}
	else
	{
		hAnim = AnimMasterTable::LookupAnim(skelId, hierarchyId, animId);

		lastAnimId = animId;
		hLastLookup = hAnim;
	}

	const ArtItemAnim* pAnim = hAnim.ToArtItem();

	if (slerp)
	{
		anim.m_hSlerpAnim = hAnim;
	}
	else
	{
		anim.m_hAddAnim = hAnim;
	}

	anim.m_hasGestureDirAp = anim.m_hasGestureDirAp || AnimHasChannel(pAnim, kApRefGestureDirId);
	anim.m_hasGestureDirFc = anim.m_hasGestureDirFc || AnimHasChannels(pAnim, kApRefGestureDirFc);

	return pAnim != nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureCache::ConstructData(const CacheKey& key, const DC::GestureDef* pDcGesture, U32 hierarchyId, CacheData* pData)
{
	PROFILE_ACCUM(GestureCache_ConstructData);

	ANIM_ASSERT(pData->m_dataLock.IsLockedForWrite());
	ANIM_ASSERT(key.m_altIndex != Gesture::kAlternativeIndexUnspecified);

	const DC::GestureAnims* pDcGestureAnims = Gesture::GetGestureAnims(pDcGesture, key.m_altIndex);

	pData->m_referenceCount.Set(0);
	pData->m_key = key;
	pData->m_numGestureAnims = 0;

	if (pDcGesture->m_type != INVALID_STRING_ID_64)
	{
		pData->m_typeId = pDcGesture->m_type;
	}
	else
	{
		pData->m_typeId = key.m_gestureId;
	}

	pData->m_animSpace.m_channelId = INVALID_STRING_ID_64;
	pData->m_animSpace.m_jointId   = INVALID_STRING_ID_64;
	pData->m_animSpace.m_offsetLs  = kIdentity;

	pData->m_originSpace.m_channelId = INVALID_STRING_ID_64;
	pData->m_originSpace.m_jointId	 = INVALID_STRING_ID_64;
	pData->m_originSpace.m_offsetLs	 = kIdentity;

	pData->m_feedbackSpace.m_channelId = INVALID_STRING_ID_64;
	pData->m_feedbackSpace.m_jointId   = INVALID_STRING_ID_64;
	pData->m_feedbackSpace.m_offsetLs  = kIdentity;

	pData->m_isBad = false;
	pData->m_applyJointLimits	  = pDcGesture->m_applyJointLimits;
	pData->m_useDetachedPhase	  = pDcGesture->m_useDetachedPhase;
	pData->m_hasNoBlendDir		  = false;
	pData->m_hasDuplicateBlendDir = false;
	pData->m_numBlendTris		  = 0;
	pData->m_numIslands	 = 0;
	pData->m_hierarchyId = hierarchyId;
	pData->m_wrap360	 = pDcGesture->m_wrap360;
	pData->m_hyperRange	 = false;

	pData->m_featherBlendId = pDcGestureAnims->m_featherBlend;

	pData->m_animSpace	   = CacheSpaceFromDcDef(pDcGesture->m_gestureSpace);
	pData->m_originSpace   = CacheSpaceFromDcDef(pDcGesture->m_originSpace);
	pData->m_feedbackSpace = CacheSpaceFromDcDef(pDcGesture->m_feedbackSpace);

	pData->m_lowLodAnimId = pDcGesture->m_lowLodPoseAnim;

	RefreshConstruction(pData, pDcGesture);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureCache::RefreshConstruction(CacheData* pData, const DC::GestureDef* pDcGesture)
{
	ANIM_ASSERT(pData->m_dataLock.IsLocked());

	const CacheKey& key = pData->m_key;

	pData->m_isBad		   = false;
	pData->m_hasNoBlendDir = false;
	pData->m_hasDuplicateBlendDir = false;
	pData->m_numBlendTris		  = 0;
	pData->m_numIslands		 = 0;
	pData->m_numGestureAnims = 0;

	const DC::GestureAnims* pDcGestureAnims = Gesture::GetGestureAnims(pDcGesture, key.m_altIndex);

	StringId64 lastLookupIds[2] = { INVALID_STRING_ID_64, INVALID_STRING_ID_64 };
	ArtItemAnimHandle hLastAnims[2];

	for (U32F inputPair = 0; inputPair < pDcGestureAnims->m_numAnimPairs; ++inputPair)
	{
		const DC::GestureAnimPair& animPair = pDcGestureAnims->m_animPairs[inputPair];

		StringId64 animIds[2] = { INVALID_STRING_ID_64, INVALID_STRING_ID_64 };
		ArtItemAnimHandle hAnims[2];

		switch (key.m_gestureNodeType)
		{
		case Gesture::AnimType::kAdditive:
			animIds[1] = animPair.m_additiveAnim;
			break;

		case Gesture::AnimType::kSlerp:
			animIds[0] = animPair.m_partialAnim;
			break;

		case Gesture::AnimType::kCombo:
			animIds[0] = animPair.m_partialAnim;
			animIds[1] = animPair.m_additiveAnim;
			break;
		}

		for (U32F iAnim = 0; iAnim < ARRAY_COUNT(animIds); ++iAnim)
		{
			const StringId64 animId = animIds[iAnim];

			if (animId == INVALID_STRING_ID_64)
			{
				continue;
			}

			if (animId == lastLookupIds[iAnim])
			{
				hAnims[iAnim] = hLastAnims[iAnim];
			}
			else
			{
				hAnims[iAnim] = AnimMasterTable::LookupAnim(key.m_skelId, pData->m_hierarchyId, animId);

				lastLookupIds[iAnim] = animId;
				hLastAnims[iAnim] = hAnims[iAnim];
			}
		}

		CacheAnim(key, animPair, hAnims[0], animIds[0], hAnims[1], animIds[1], pData);

		if (pData->m_numGestureAnims >= Gesture::kMaxGestureNodeAnims || pData->m_isBad)
		{
			break;
		}
	}

	if (pData->m_lowLodAnimId && !pData->m_isBad)
	{
		pData->m_hLowLodAnim = AnimMasterTable::LookupAnim(key.m_skelId, pData->m_hierarchyId, pData->m_lowLodAnimId);
	}
	else
	{
		pData->m_hLowLodAnim = ArtItemAnimHandle();
	}

	// HACK! fix ArtItemAnimHandle.ToArtItem() returns garbage pointer after anim logouts.
	if (pData->m_isBad)
	{
		// maybe just set pData->m_numGestureAnims = 0; ?
		for (int i = 0; i < pData->m_numGestureAnims; i++)
		{
			if (pData->m_cachedAnims[i].m_slerpBaseName != INVALID_STRING_ID_64)
				pData->m_cachedAnims[i].m_hSlerpAnim = ArtItemAnimHandle();

			if (pData->m_cachedAnims[i].m_addBaseName != INVALID_STRING_ID_64)
				pData->m_cachedAnims[i].m_hAddAnim = ArtItemAnimHandle();
		}
	}

	AddExtraNodes(pData);

	RefreshPhasesAndDirections(pData);

	RefreshNoBlendDir(pData);

	pData->m_animSentinal.Refresh();

	ConstructBlendTriangles(pData, pDcGestureAnims, pDcGesture);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureCache::EraseData(CacheData* pData)
{
	KeyTable::Iterator keyItr = m_keyTable.Find(pData->m_key);

	ANIM_ASSERT(keyItr != m_keyTable.End());

	if (keyItr != m_keyTable.End())
	{
		m_keyTable.Erase(keyItr);
		m_dataHeap.Free(pData);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureCache::RefreshData(CacheData* pData)
{
	PROFILE_ACCUM(GestureCache_RefreshData);

	if (!pData)
		return;

	if (!pData->m_animSentinal.IsValid())
	{
		RefreshAnims(pData);

		RefreshPhasesAndDirections(pData);

		pData->m_animSentinal.Refresh();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureCache::RefreshAnims(CacheData* pData)
{
	const CacheKey& key = pData->m_key;

	StringId64 lastLookupAnimIds[2] = { INVALID_STRING_ID_64, INVALID_STRING_ID_64 };
	ArtItemAnimHandle lastLookupAnims[2];

	if (pData->m_isBad)
	{
		if (const DC::GestureDef* pDcDef = Gesture::LookupGesture(key.m_gestureId))
		{
			RefreshConstruction(pData, pDcDef);
		}
	}
	else
	{
		for (U32F iPair = 0; iPair < pData->m_numGestureAnims; ++iPair)
		{
			GestureCache::CachedAnim& anim = pData->m_cachedAnims[iPair];

			anim.m_hSlerpAnim	   = ArtItemAnimHandle();
			anim.m_hAddAnim		   = ArtItemAnimHandle();
			anim.m_hasGestureDirAp = false;
			anim.m_hasGestureDirFc = false;

			const bool slerpValid = DoLookup(anim,
											 true,
											 key.m_skelId,
											 pData->m_hierarchyId,
											 lastLookupAnimIds[0],
											 lastLookupAnims[0]);

			const bool addValid = DoLookup(anim,
										   false,
										   key.m_skelId,
										   pData->m_hierarchyId,
										   lastLookupAnimIds[1],
										   lastLookupAnims[1]);

			const bool valid = slerpValid && addValid;

			if (!valid && !pData->m_isBad)
			{
				pData->m_isBad = true;
			}
		}

		if (pData->m_lowLodAnimId)
		{
			pData->m_hLowLodAnim = AnimMasterTable::LookupAnim(key.m_skelId, pData->m_hierarchyId, pData->m_lowLodAnimId);
		}
		else
		{
			pData->m_hLowLodAnim = ArtItemAnimHandle();
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureCache::RefreshPhasesAndDirections(CacheData* pData)
{
	const CacheKey& key = pData->m_key;

	SphericalCoords minAngle = SphericalCoords::FromThetaPhi(kLargeFloat, kLargeFloat);
	SphericalCoords maxAngle = SphericalCoords::FromThetaPhi(-kLargeFloat, -kLargeFloat);

	for (U32F iPair = 0; iPair < pData->m_numGestureAnims; ++iPair)
	{
		CachedAnim& gestureAnim = pData->m_cachedAnims[iPair];
		const ArtItemAnim* pAnim = gestureAnim.m_hSlerpAnim.ToArtItem();

		if (!pAnim)
		{
			pAnim = gestureAnim.m_hAddAnim.ToArtItem();
		}

		if (pAnim)
		{
			gestureAnim.m_phase = GetClipPhaseForMayaFrame(pAnim->m_pClipData, gestureAnim.m_frame);
		}
		else
		{
			gestureAnim.m_phase = -1.0f;
		}

		if (!gestureAnim.m_extra)
		{
			gestureAnim.m_dir = GetGestureDirForAnim(key, pData, gestureAnim);

			if (gestureAnim.m_wrapAround)
			{
				if (gestureAnim.m_dir.Theta() < 0.0f)
				{
					gestureAnim.m_dir.Theta() += 360.0f;
				}
				else
				{
					gestureAnim.m_dir.Theta() -= 360.0f;
				}
			}
		}

		minAngle = SphericalCoords::Min(minAngle, gestureAnim.m_dir);
		maxAngle = SphericalCoords::Max(maxAngle, gestureAnim.m_dir);
	}

	pData->m_hyperRange = (minAngle.Theta() < -179.0f) || (minAngle.Phi() < -89.0f) || (maxAngle.Theta() > 179.0f)
						  || (maxAngle.Phi() > 89.0f);
}

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void GestureCache::FixupManualTriangleWinding(TriangleIndicesU8& tri, const CacheData* pData)
{
	
	const CachedAnim& anim0 = pData->m_cachedAnims[tri.m_indices[0]];
	const CachedAnim& anim1 = pData->m_cachedAnims[tri.m_indices[1]];
	const CachedAnim& anim2 = pData->m_cachedAnims[tri.m_indices[2]];

	const Vec2 p0 = anim0.m_dir.AsVec2();
	const Vec2 p1 = anim1.m_dir.AsVec2();
	const Vec2 p2 = anim2.m_dir.AsVec2();

	const TriangleWinding w = ComputeTriangleWinding(p0, p1, p2);

	if (w != kWindingCW)
	{
		const U8 temp = tri.m_iB;
		tri.m_iB = tri.m_iC;
		tri.m_iC = temp;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureCache::ConstructBlendTriangles(CacheData* pData,
										   const DC::GestureAnims* pDcGestureAnims,
										   const DC::GestureDef* pDcGesture)
{
	pData->m_numBlendTris = 0;

	if (!pDcGesture)
	{
		return;
	}

	if (const DC::GestureMesh* pManualMesh = pDcGestureAnims ? pDcGestureAnims->m_manualMesh : nullptr)
	{
		for (int iTri = 0; iTri < pManualMesh->m_size; ++iTri)
		{
			const I32F i0 = pManualMesh->m_indices[(iTri * 3) + 0];
			const I32F i1 = pManualMesh->m_indices[(iTri * 3) + 1];
			const I32F i2 = pManualMesh->m_indices[(iTri * 3) + 2];

			if (((i0 < 0) || (i0 >= pData->m_numGestureAnims)) ||
				((i1 < 0) || (i1 >= pData->m_numGestureAnims)) ||
				((i2 < 0) || (i2 >= pData->m_numGestureAnims)))
			{
				pData->m_isBad = true;
				pData->m_numBlendTris = 0;
				return;
			}

			TriangleIndicesU8& manualTri = pData->m_blendTris[pData->m_numBlendTris];

			manualTri.m_indices[0] = i0;
			manualTri.m_indices[1] = i1;
			manualTri.m_indices[2] = i2;

			FixupManualTriangleWinding(manualTri, pData);

			pData->m_blendTriIslandIndex[0] = 0;

			++pData->m_numBlendTris;
		}

		pData->m_numIslands = 1;
	}
	else
	{
		AppendTrianglesForGesture(pData, pDcGesture);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureCache::AppendTrianglesForGesture(CacheData* pData, const DC::GestureDef* pDcGesture)
{
	if (pDcGesture->m_forceLinear)
	{
		return;
	}

	if (pData->m_numGestureAnims < 3)
	{
		return;
	}

	if (GestureAnimsAreLinear(pData))
	{
		return;
	}

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	Vec2* inputPoints = NDI_NEW Vec2[pData->m_numGestureAnims];
	for (U32F iPair = 0; iPair < pData->m_numGestureAnims; ++iPair)
	{
		inputPoints[iPair] = pData->m_cachedAnims[iPair].m_dir.AsVec2();
	}

	if (pData->m_hasNoBlendDir)
	{
		ListArray<int> leftSubset(pData->m_numGestureAnims);
		ListArray<int> rightSubset(pData->m_numGestureAnims);

		const float noBlendDirDegrees = pData->m_noBlendDir.ToDegrees();

		for (int i = 0; i < pData->m_numGestureAnims; ++i)
		{
			ListArray<int>* pIndicesArray = nullptr;

			if (inputPoints[i].X() < noBlendDirDegrees)
			{
				pIndicesArray = &leftSubset;
			}
			else
			{
				pIndicesArray = &rightSubset;
			}

			if (pIndicesArray)
			{
				pIndicesArray->push_back(i);
			}
		}

		AppendTrianglesForSubsetOfAnims(pData, leftSubset, inputPoints, 0);
		AppendTrianglesForSubsetOfAnims(pData, rightSubset, inputPoints, 1);
		pData->m_numIslands = 2;
	}
	else
	{
		ListArray<int> subsetIndices(pData->m_numGestureAnims);
		for (int i = 0; i < pData->m_numGestureAnims; ++i)
		{
			subsetIndices.push_back(i);
		}

		AppendTrianglesForSubsetOfAnims(pData, subsetIndices, inputPoints, 0);
		pData->m_numIslands = 1;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureCache::AppendTrianglesForSubsetOfAnims(CacheData* pData,
												   const ListArray<int>& indices,
												   const Vec2* inputPoints,
												   int islandIndex)
{
	ScopedTempAllocator jj(FILE_LINE_FUNC);

	const U32 numIndices = indices.size();

	Vec2* subsetInputPoints = NDI_NEW Vec2[numIndices];
	for (int i = 0; i < numIndices; ++i)
	{
		subsetInputPoints[i] = inputPoints[indices[i]];
	}

	TriangleIndices* triIndices32 = NDI_NEW TriangleIndices[kMaxBlendTriangles];

	DelaunayParams dtParams;
	dtParams.m_pPoints = subsetInputPoints;
	dtParams.m_numPoints = numIndices;
	dtParams.m_duplicatePointTolerance = 0.0001f;

	const U32F numGeneratedTris = GenerateDelaunay2d(DelaunayMethod::kIncrementalConstrained,
													 dtParams,
													 triIndices32,
													 kMaxBlendTriangles);

	pData->m_hasDuplicateBlendDir = dtParams.m_duplicatePoints;

	for (U32 i = 0; i < numGeneratedTris; ++i)
	{
		TriangleIndices& tri = triIndices32[i];

		for (U32 j = 0; j < 3; ++j)
		{
			I32& ind = tri.m_indices[j];

			ind = indices[ind];
		}
	}

	AppendTriangles(pData, triIndices32, numGeneratedTris, inputPoints, islandIndex);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureCache::AppendTriangles(CacheData* pData,
								   const TriangleIndices* triIndices32, 
								   const U32F numGeneratedTris, 
								   const Vec2* inputPoints, 
								   int islandIndex)
{
	for (U32F iTri = 0; iTri < numGeneratedTris; ++iTri)
	{
		ANIM_ASSERTF(pData->m_numBlendTris < kMaxBlendTriangles, ("Out of space in m_blendTris"));

		if (ShouldRejectTriangle(triIndices32, iTri, inputPoints, pData->m_numGestureAnims))
		{
			continue;
		}

		for (U32F index = 0; index < 3; ++index)
		{
			ANIM_ASSERTF(triIndices32[iTri].m_indices[index] < pData->m_numGestureAnims, 
						   ("Deluanay came back with bad tri index %d [tri: %d / %d] [gesture: '%s']", 
						   triIndices32[iTri].m_indices[index], 
						   int(iTri),
						   pData->m_numBlendTris,
						   DevKitOnly_StringIdToString(pData->m_key.m_gestureId)));

			pData->m_blendTris[pData->m_numBlendTris].m_indices[index] = triIndices32[iTri].m_indices[index];
		}

		pData->m_blendTriIslandIndex[pData->m_numBlendTris] = islandIndex;

		++pData->m_numBlendTris;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool GestureCache::GestureAnimsAreLinear(CacheData* pData) const
{
	if (!pData)
		return false;

	// If the v-angles are all within a small range, this is a linear gesture.

	if (pData->m_numGestureAnims >= 1)
	{
		float minVAngle = NDI_FLT_MAX;
		float maxVAngle = -NDI_FLT_MAX;

		for (U32 i = 0; i < pData->m_numGestureAnims; ++i)
		{
			const CachedAnim& animPair = pData->m_cachedAnims[i];

			const SphericalCoords dir = animPair.m_dir;

			const float vAngle = dir.Phi();

			minVAngle = Min(minVAngle, vAngle);
			maxVAngle = Max(maxVAngle, vAngle);
		}

		const float vAngleRange = maxVAngle - minVAngle;

		return 0.0f <= vAngleRange && vAngleRange <= kLinearAngleThresholdDeg;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool GestureCache::RefreshNoBlendDir(CacheData* pData) const
{
	if (!pData)
	{
		return false;
	}

	pData->m_hasNoBlendDir = false;

	for (I32 i = 0; i < pData->m_numGestureAnims; ++i)
	{
		const CachedAnim& anim = pData->m_cachedAnims[i];

		const ArtItemAnim* pAnim = anim.m_hSlerpAnim.ToArtItem();

		if (!pAnim)
		{
			pAnim = anim.m_hAddAnim.ToArtItem();
		}

		Angle noBlendDir;
		if (GetNoBlendDirForAnim(pData->m_key.m_skelId, pAnim, pData->m_key.m_flipped, &noBlendDir))
		{
			pData->m_noBlendDir = noBlendDir;
			pData->m_hasNoBlendDir = true;

			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct RefCountHistogram
{
	U64 m_refCounts[4]		= { 0, 0, 0, 0 };
	U64 m_badCount = 0;
	MsgOutput m_debugOutput = kMsgNull;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool CollectRefCounts(const U8* pElement, uintptr_t userData, bool free)
{
	STRIP_IN_FINAL_BUILD_VALUE(false);

	if (!pElement || free)
		return true;

	GestureCache::CacheData* pData = (GestureCache::CacheData*)pElement;
	RefCountHistogram& histogram = *(RefCountHistogram*)userData;

	const int refCountVal = pData->m_referenceCount.Get();

	if (refCountVal < 0)
		return true; // bad things have happened
	
	if (pData->m_isBad)
	{
		++histogram.m_badCount;
	}

	const U32F histoIndex = Min(refCountVal, int(3));
	histogram.m_refCounts[histoIndex]++;

	if ((histogram.m_debugOutput != kMsgNull) &&
		(!g_animOptions.m_gestureCache.m_onlyPrintActiveEntries || (refCountVal > 0)))
	{
		StringBuilder<512> desc;
		desc.append_format("  %s (%s",
						   DevKitOnly_StringIdToString(pData->m_key.m_gestureId),
						   Gesture::GetGestureAnimTypeStr(pData->m_key.m_gestureNodeType));

		if (pData->m_key.m_flipped)
		{
			desc.append_format("-flipped");
		}

		switch (pData->m_key.m_altIndex)
		{
		case Gesture::kAlternativeIndexUnspecified:
			desc.append_format(", alt: unspecified");
			break;
		case Gesture::kAlternativeIndexNone:
			break;
		default:
			desc.append_format(", alt: %d", pData->m_key.m_altIndex);
			break;
		}

		desc.append_format(") [0x%.8x] <0x%llx>", pData->m_key.m_skelId.GetValue(), pData->m_key.m_u64);

		if (pData->m_isBad)
		{
			desc.append_format("BAD ");
		}
		else
		{
			desc.append_format(": %d anims / %d tris ", pData->m_numGestureAnims, pData->m_numBlendTris);

			if (pData->m_numIslands > 1)
			{
				desc.append_format("[%d isl] ", pData->m_numIslands);
			}
		}
		
		desc.append_format("(ref count: %d)\n", refCountVal);

		PrintTo(histogram.m_debugOutput, desc.c_str());
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GestureCache::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	if (!g_animOptions.m_gestureCache.m_debugPrint)
	{
		return;
	}

	AtomicLockJanitorRead readLock(&m_tableLock, FILE_LINE_FUNC);

	const U32F numUsedEntries = m_dataHeap.GetNumElementsUsed();
	const U32F numTotalEntries = m_dataHeap.GetNumElements();

	RefCountHistogram histogram;

	m_dataHeap.ForEachElement(CollectRefCounts, (uintptr_t)&histogram);

	const U32F numActiveEntries = histogram.m_refCounts[1] + histogram.m_refCounts[2] + histogram.m_refCounts[3];
	const U32F numBadEntries = histogram.m_badCount;
	const float activePercent = (float(numActiveEntries) / float(numTotalEntries));
	const float allocatedPercent = (float(numUsedEntries) / float(numTotalEntries));
	const float badPercent = (float(numBadEntries) / float(numTotalEntries));

	MsgCon("---------------------------------------------------------\n");
	MsgCon("Gesture Cache [%d blocks]%s:\n", numTotalEntries, m_evictionActive ? " (Eviction ACTIVE)" : "");
	MsgCon("  %3d Active    (%0.2f%%)\n", numActiveEntries, 100.0f * activePercent);
	MsgCon("  %3d Allocated (%0.2f%%)\n", numUsedEntries, 100.0f * allocatedPercent);
	MsgCon("  %3d Bad       (%0.2f%%)\n", numBadEntries, 100.0f * badPercent);
	MsgCon("  Ref Counts:\n");

	for (U32F i = 0; i < 3; ++i)
	{
		MsgCon("   [%d]  : %3d entr%s\n", i, histogram.m_refCounts[i], (histogram.m_refCounts[i] == 1) ? "y" : "ies");
	}

	MsgCon("   [3+] : %3d entr%s\n", histogram.m_refCounts[3], (histogram.m_refCounts[3] == 1) ? "y" : "ies");

	if (g_animOptions.m_gestureCache.m_dumpCachedEntries)
	{
		histogram.m_debugOutput = kMsgAnim;
		g_animOptions.m_gestureCache.m_dumpCachedEntries = false;
	}
	else if (g_animOptions.m_gestureCache.m_debugPrintDetails)
	{
		histogram.m_debugOutput = kMsgCon;
	}

	if (histogram.m_debugOutput != kMsgNull)
	{
		PrintTo(histogram.m_debugOutput, "---------------------------------------------------------\n");
		PrintTo(histogram.m_debugOutput, "Gesture Cache Entries:\n");
	}

	m_dataHeap.ForEachElement(CollectRefCounts, (uintptr_t)&histogram);

	MsgCon("---------------------------------------------------------\n");
}
