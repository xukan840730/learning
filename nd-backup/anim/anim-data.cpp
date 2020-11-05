/*
 * Copyright (c) 2014 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/anim-data.h"

#include "corelib/math/sphere.h"
#include "corelib/memory/memory-map.h"
#include "corelib/memory/relocate.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/ik/joint-limits.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/profiling/profiling.h"

#include "gamelib/level/art-item-skeleton.h"

/// --------------------------------------------------------------------------------------------------------------- ///
Vec4* g_pIdentityBoneMats; //[kMaxIdentityJoints * 3];
AnimDataOptimization g_animOptimization;

#define USE_STATIC_PERSISTENT_DATA 1

#if USE_STATIC_PERSISTENT_DATA
static CONST_EXPR size_t kPersistentDataBlockSize = 2 * 1024;
static CONST_EXPR size_t kMaxPersistentDataEntries = 256;

class PersistentDataBuf
{
public:
	PersistentDataBuf()
		: m_lock(0)
	{
		m_usedBlocks.ClearAllBits();
	}

	void* Allocate()
	{
		NdAtomic64Janitor lock(&m_lock);
		
		const U32F iFree = m_usedBlocks.FindFirstClearBit();
		//ANIM_ASSERT(iFree < kMaxPersistentDataEntries);
		
		if (iFree >= kMaxPersistentDataEntries)
			return nullptr;

		m_usedBlocks.SetBit(iFree);
		return m_dataBlocks[iFree];
	}

	void Free(void* ptr)
	{
		if (!ptr)
			return;

		NdAtomic64Janitor lock(&m_lock);

		I32F iFound = -1;
		for (U32F iEntry = 0; iEntry < kMaxPersistentDataEntries; ++iEntry)
		{
			if (m_dataBlocks[iEntry] == ptr)
			{
				iFound = iEntry;
				break;
			}
		}

		ANIM_ASSERT(iFound >= 0);
		if (iFound < 0)
			return;

		m_usedBlocks.ClearBit(iFound);
	}

	void DebugDraw() const
	{
		STRIP_IN_FINAL_BUILD;

		MsgCon("Persistent Anim Data: %d / %d entries\n", m_usedBlocks.CountSetBits(), kMaxPersistentDataEntries);
	}

private:
	NdAtomic64 m_lock;
	BitArray<kMaxPersistentDataEntries> m_usedBlocks;

	U8 m_dataBlocks[kMaxPersistentDataEntries][kPersistentDataBlockSize];
};

static PersistentDataBuf s_persistentDataBuf;
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void FgAnimData::StartUp()
{
	const size_t bytesAvail = Memory::GetSize(ALLOCATION_FG_ANIM_IDENTITY_MATS);
	const size_t bytesNeeded = AlignSize(sizeof(Vec4) * kMaxIdentityJoints * 3, kAlign16);
	ANIM_ASSERT(bytesNeeded <= bytesAvail);
	g_pIdentityBoneMats = NDI_NEW(kAllocGlobal, kAlign16) Vec4[kMaxIdentityJoints * 3];
	ANIM_ASSERT(g_pIdentityBoneMats);

	const Vec4 kIdentRow0 = VEC4_LC(1.0f, 0.0f, 0.0f, 0.0f);
	const Vec4 kIdentRow1 = VEC4_LC(0.0f, 1.0f, 0.0f, 0.0f);
	const Vec4 kIdentRow2 = VEC4_LC(0.0f, 0.0f, 1.0f, 0.0f);

	for (U32F i = 0; i < kMaxIdentityJoints; ++i)
	{
		Vec4* pMtx = &g_pIdentityBoneMats[i * 3];
		pMtx[0] = kIdentRow0;
		pMtx[1] = kIdentRow1;
		pMtx[2] = kIdentRow2;
	}
}
/// --------------------------------------------------------------------------------------------------------------- ///
/* static */
void FgAnimData::DebugDraw()
{
	STRIP_IN_FINAL_BUILD;

#if USE_STATIC_PERSISTENT_DATA
	if (g_animOptions.m_printPersistentDataStats)
	{
		s_persistentDataBuf.DebugDraw();
	}
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
FgAnimData::FgAnimData()
{
	ANIM_ASSERT(IsPointerAligned(this, kAlign128));
	
	m_pPersistentData = nullptr;

	Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FgAnimData::Init(const ArtItemSkeletonHandle skelHandle,
					  const Transform& xform,
					  JointCache::ConfigType configType /* = JointCache::kConfigNormal */)
{
	PROFILE(Animation, AnimDataInit);

	const ArtItemSkeleton* pSkel = skelHandle.ToArtItem();

	// Setup the skeleton and the joint information
	m_pJointFlipData = pSkel->m_pJointFlipData;			// Caching internal asset pointer - No no
	m_pSkeleton = pSkel->m_pAnimHierarchy;				// Caching internal asset pointer - No no
	m_pJointDescs = pSkel->m_pJointDescs;				// Caching internal asset pointer - No no
	m_scale = Vector(1.0f, 1.0f, 1.0f);
	m_jointCache.Init(skelHandle, xform, configType);

	ChangeAnimConfig(kAnimConfigSimpleAnimation);

	m_curSkelHandle = skelHandle;
	ResourceTable::IncrementRefCount(m_curSkelHandle.ToArtItem());

	m_animateSkelHandle = skelHandle;
	ResourceTable::IncrementRefCount(m_animateSkelHandle.ToArtItem());

	m_pBoundingInfo = NDI_NEW (kAlign16) BoundingData;
	m_pBoundingInfo->m_jointBoundingSphere = Sphere(xform.GetTranslation(), 0.0f);
	m_boundingSphereExcludeJoints[0] = -1;
	m_boundingSphereExcludeJoints[1] = -1;
	m_visVolIndex = -2; // Uninitialized
	
	Transform scaleXform(kIdentity);
	scaleXform.SetScale(m_scale);
	m_objXform = scaleXform * xform;
	m_allowLodCull = true;

	m_pPluginJointSet = nullptr;
	m_disabledDeltaTime = 0.f;

	if (pSkel->m_persistentDataSize)
	{
		// Initialize the persistent rig node data.
#if USE_STATIC_PERSISTENT_DATA
		ANIM_ASSERTF(pSkel->m_persistentDataSize < kPersistentDataBlockSize,
					 ("Need to bump kPersistentDataBlockSize to at least %d bytes", pSkel->m_persistentDataSize));

		m_pPersistentData = s_persistentDataBuf.Allocate();
#else
		m_pPersistentData = NDI_NEW(kAlign16) U8[pSkel->m_persistentDataSize];
#endif
		if (m_pPersistentData)
		{
			memcpy(m_pPersistentData, pSkel->m_pInitialPersistentData, pSkel->m_persistentDataSize);
		}
		else
		{
			MsgWarn("No memory space available to allocate buffer for persistent data [%d bytes]\n", pSkel->m_persistentDataSize);
		}
	}
	else
	{
		m_pPersistentData = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FgAnimData::Reset()
{
	PROFILE(Animation, AnimDataReset);

	m_flags = (kEnableAsyncPostAnimUpdate | kEnableAsyncPostAnimBlending | kEnableAsyncPostJointUpdate | kAllowAutomaticObjXformUpdate);

	m_animConfig = kAnimConfigNoAnimation;
	m_animSourceMode[0] = kAnimSourceModeNone;
	m_animSourceMode[1] = kAnimSourceModeNone;
	m_animResultMode[0] = kAnimResultNone;
	m_animResultMode[1] = kAnimResultNone;
	m_animClockScale = 1.0f;
	m_disabledDeltaTime = 0.f;

	m_pAnimControl = nullptr;

	m_scale = Vector(1.0f, 1.0f, 1.0f);

	m_visSphereJointIndex = -1;
	m_pBoundingInfo = nullptr;

	m_jointCache = JointCache();

	m_objXform = Transform(kIdentity);

	m_maxNumDrivenInstances = 0;
	m_numDrivenInstances = 0;
	m_pDrivenInstanceIndices = 0;
	m_clothBoundingBoxMult = 1.0f;
	m_earlyDeferredSegmentMaskGame = 0;
	m_earlyDeferredSegmentMaskRender = 0;

	m_pMotionBlurBoneMats = nullptr;
	m_pMotionBlurXforms = nullptr;

	m_pPluginParams = nullptr;

	m_userAnimationPassCallback[0] = nullptr;
	m_userAnimationPassCallback[1] = nullptr;

	m_pJointLimits = nullptr;

	m_extraAnimJobFlags = 0;

	m_useLargeAnimCommandList = false;

	m_useBoundingBox = false;

	m_pPluginJointSet = nullptr;
	m_pRopeSkinningData = nullptr;

	m_forceDeferredAnimation = false;

	m_animLod = DC::kAnimLodNormal;

#if USE_STATIC_PERSISTENT_DATA
	s_persistentDataBuf.Free(m_pPersistentData);
#endif
	m_pPersistentData = nullptr;

	if (m_deferredAnimLock.Valid())
	{
		m_deferredAnimLock.Destroy();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FgAnimData::OnFree()
{
#if USE_STATIC_PERSISTENT_DATA
	s_persistentDataBuf.Free(m_pPersistentData);
#endif
	m_pPersistentData = nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FgAnimData::Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_pAnimControl, delta, lowerBound, upperBound);

	m_jointCache.Relocate(delta, lowerBound, upperBound);

	RelocatePointer(m_pBoundingInfo, delta, lowerBound, upperBound);
	RelocatePointer(m_pDrivenInstanceIndices, delta, lowerBound, upperBound);

	// And now the whole array
	RelocatePointer(m_pPluginParams, delta, lowerBound, upperBound);
	DeepRelocatePointer(m_pJointLimits, delta, lowerBound, upperBound);
	RelocatePointer(m_pPluginJointSet, delta, lowerBound, upperBound);

#if !USE_STATIC_PERSISTENT_DATA
	RelocatePointer(m_pPersistentData, delta, lowerBound, upperBound);
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FgAnimData::ResetPersistentRigData()
{
	if (m_pPersistentData)
	{
		if (const ArtItemSkeleton* pSkel = m_curSkelHandle.ToArtItem())
		{
			memcpy(m_pPersistentData, pSkel->m_pInitialPersistentData, pSkel->m_persistentDataSize);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FgAnimData::OnTeleport()
{
	DisableMotionBlurThisFrame();

	ResetPersistentRigData();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FgAnimData::ChangeAnimConfig(AnimConfig config)
{
	// If we do certain change in anim config after anim step and on the next frame we are paused, we may get wrong skinning matrices during the pause
	// because the new config expects some data in joint cache to be valid and they are not
	// So we delay the change until *before* non-paused anim step

	m_animConfig = static_cast<U8>(config);

	if (m_jointCache.GetInBindPose())
	{
		// If joint cache has just been initialized we're fine to do it right now
		// In fact it's crucial for Init to work
		PerformAnimConfigChange();
		return;
	}

	m_flags |= kAnimConfigChangePending;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FgAnimData::PerformAnimConfigChange()
{
	switch(m_animConfig)
	{
	case kAnimConfigNoAnimationForCloth:
		{
			SetAnimSourceMode(0, kAnimSourceModeNone);
			SetAnimResultMode(0, kAnimResultJointParamsLs);

			SetAnimSourceMode(1, kAnimSourceModeJointParams);
			SetAnimResultMode(1, kAnimResultJointTransformsAndSkinningMatrices);
		}
		break;

	case kAnimConfigNoAnimation:
		{
			SetAnimSourceMode(0, kAnimSourceModeNone);
			SetAnimResultMode(0, kAnimResultNone);

			SetAnimSourceMode(1, kAnimSourceModeJointParams);
			SetAnimResultMode(1, kAnimResultJointTransformsAndSkinningMatrices);
		}
		break;

	case kAnimConfigSimpleAnimation:
		{
			SetAnimSourceMode(0, kAnimSourceModeClipData);
			SetAnimResultMode(0, kAnimResultJointTransformsAndSkinningMatrices);

			SetAnimSourceMode(1, kAnimSourceModeNone); 
			SetAnimResultMode(1, kAnimResultNone);
		}
		break;

	case kAnimConfigComplexAnimation:
		{
			SetAnimSourceMode(0, kAnimSourceModeClipData);
			SetAnimResultMode(0, kAnimResultJointParamsLs);

			SetAnimSourceMode(1, kAnimSourceModeJointParams);
			SetAnimResultMode(1, kAnimResultJointTransformsAndSkinningMatrices);
		}
		break;

	case kAnimConfigComplexWithSdkJointsInFirstPass:
		{
			SetAnimSourceMode(0, kAnimSourceModeClipData);
			SetAnimResultMode(0, kAnimResultJointTransformsAndJointParams);

			SetAnimSourceMode(1, kAnimSourceModeJointParams);
			SetAnimResultMode(1, kAnimResultJointTransformsAndSkinningMatrices);
		}
		break;

	default:
		{
			ANIM_ASSERT(false);
		}
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FgAnimData::CheckPendingAnimConfigChange()
{
	if (m_flags & kAnimConfigChangePending)
	{
		PerformAnimConfigChange();
		m_flags &= ~kAnimConfigChangePending;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
FgAnimData::AnimSourceMode FgAnimData::GetAnimSourceMode(U32F pass) const
{
	ANIM_ASSERT(pass < 2);
	return m_animSourceMode[pass];
}

/// --------------------------------------------------------------------------------------------------------------- ///
FgAnimData::AnimResultMode FgAnimData::GetAnimResultMode(U32F pass) const
{
	ANIM_ASSERT(pass < 2);
	return m_animResultMode[pass];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FgAnimData::SetAnimSourceMode(U32F pass, AnimSourceMode animSourceMode)
{
	// We can not use joint params as the input unless they exist
	if (animSourceMode & kAnimSourceModeJointParams)
	{
		ANIM_ASSERT(m_jointCache.GetJointParamsLs());
	}

	if (m_animSourceMode[pass] != animSourceMode)
	{
		m_animSourceMode[pass] = animSourceMode;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FgAnimData::SetAnimResultMode(U32F pass, AnimResultMode resultMode)
{
	// We can not output joint params unless we have storage for them
	if (resultMode & kAnimResultJointParamsLs)
	{
		ANIM_ASSERT(m_jointCache.GetJointParamsLs());
	}

	if (m_animResultMode[pass] != resultMode)
	{
		m_animResultMode[pass] = resultMode;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 FgAnimData::GetJointSid(U32 iJoint) const
{
	const ArtItemSkeleton* pSkel = m_curSkelHandle.ToArtItem();
	if (iJoint < pSkel->m_numTotalJoints)
	{
		return pSkel->m_pJointDescs[iJoint].m_nameId;
	}
	else
	{
		return INVALID_STRING_ID_64;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
I16 FgAnimData::FindJoint(StringId64 jid, bool useAnimateSkel/* = false*/) const
{
	const ArtItemSkeleton* pAnimSkel = m_animateSkelHandle.ToArtItem();
	if (pAnimSkel && useAnimateSkel)
	{
		return ::FindJoint(pAnimSkel->m_pJointDescs, pAnimSkel->m_numGameplayJoints, jid);
	}
	else
	{
		const ArtItemSkeleton* pSkel = m_curSkelHandle.ToArtItem();
		return ::FindJoint(pSkel->m_pJointDescs, pSkel->m_numGameplayJoints, jid);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FgAnimData::SetXform(const Transform& transform, bool invalidateJointCacheWsLocs)
{
	ANIM_ASSERT(IsFinite(transform));
	ANIM_ASSERT(Length(transform.GetTranslation()) < 100000.0f);

	m_scale = Vector(Length(transform.GetXAxis()), Length(transform.GetYAxis()), Length(transform.GetZAxis()));
	m_objXform = transform;

	m_jointCache.SetObjXForm(m_objXform, invalidateJointCacheWsLocs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void FgAnimData::SetXform(const Locator& loc, bool invalidateJointCacheWsLocs)
{
	const Transform newXform = loc.AsTransform();
	ANIM_ASSERT(IsFinite(newXform));
	ANIM_ASSERT(Length(newXform.GetTranslation()) < 100000.0f);
	ANIM_ASSERT(IsFinite(m_scale));

	Transform scaleXform;
	scaleXform.SetScale(m_scale);
	m_objXform = scaleXform * newXform;

	m_jointCache.SetObjXForm(m_objXform, invalidateJointCacheWsLocs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator FgAnimData::GetXform() const
{
	Vector scaleInv(Select(Recip(m_scale), Vector(kZero), Simd::CompareEQ(m_scale.QuadwordValue(), Simd::GetVecAllZero()))); // Safe inversion, sets zero for a component of scale that is zero
	Transform invScaleXform;
	invScaleXform.SetScale(scaleInv);
	Transform newXform = invScaleXform * m_objXform;

	ANIM_ASSERT(IsOrthoNormal(&newXform));
	ANIM_ASSERT(Length(newXform.GetTranslation()) < 100000.0f);
	ANIM_ASSERT(IsFinite(newXform.GetTranslation()));

	return Locator(newXform);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* FgAnimData::GetOutputControlName(I32F iOutput) const
{
	const ArtItemSkeleton* pSkel = m_curSkelHandle.ToArtItem();
	const U32F numFloatOutputs = pSkel->m_pAnimHierarchy->m_numOutputControls;
	if (iOutput < 0 || iOutput >= numFloatOutputs)
		return nullptr;

	const SkelComponentDesc* pOutputDesc = &pSkel->m_pFloatDescs[iOutput];

	const char* shortName = pOutputDesc->m_pName;
	for (int i = strlen(shortName) - 1; i > 0; --i)
	{
		if (shortName[i] == '.')
		{
			shortName = shortName + i + 1;
			break;
		}
	}

	return shortName;
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnimDataOptimization::AnimDataOptimization()
{
	memset(this, 0, sizeof(*this));

	// something is broken with the visibility so disable this for now. should be fixed/fixable soon...
	//  (we're not calling m_fgCull.SetInstanceVisibilityFlags on windows right now)
#ifdef NDI_PLAT_WIN
	m_disableBindPoseVisibilityOpt = true;
#endif

	// Disable these optimizations for now as they prevent us from seeing the real costs of anim/render. CGY
	m_disableBindPoseOpt = true;
	m_disableBindPoseWaitForJobsOpt = true;

	
	// mess with these to try out various performance scenarios...
	m_testopt_SkipWaitForJobs = false;		// never call WaitForJobs on disabled game objects (breaks things!)
	m_testopt_SkipFastAnimUpdate = false;	// don't call FastAnimUpdate AT ALL on disabled game objects (breaks things!)
	m_testopt_SkipPhysUpdate = false;		// skip the CompositeBody::FastAnimUpdate() to see how much time we spend there (breaks things!)
	m_testopt_SkipShimmer = false;			// skip UpdateShimmer() to see how much time it takes (breaks shimmer!)
	m_testopt_SkipPhysFxInBindPose = false;	// don't run PhysFx for objects in bind pose (a potential real optimization)
	m_testopt_SkipPhysFx = false;			// don't run PhysFx at all to see how long it takes (breaks things!)
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnimDataOptimization::Stats::Reset()
{
	STRIP_IN_FINAL_BUILD;

	F32 fNumFastObjects = m_numFastObjects > 0 ? (F32)m_numFastObjects : 1.0f;

	if (g_animOptimization.m_drawDisabledUpdates)
	{
		MsgCon("-------------\n");
		MsgCon("# Fast Objs  %04u Bind Pose  %04u  Visible  %04u\n", m_numFastObjects, m_numBpObjects, m_numVisibleObjects);
		MsgCon("# Propagate  %04u\n", m_numPropagateBoneMats);
		MsgCon("\n");
		MsgCon("# Composite  %04u (%.1f pct)\n", m_numCompositeBodyObjects,			100.0f * (F32)m_numCompositeBodyObjects / fNumFastObjects);
		MsgCon("#  Identity  %04u (%.1f pct)\n", m_numIdentityComposites,			100.0f * (F32)m_numIdentityComposites / fNumFastObjects);
		MsgCon("#  Active    %04u (%.1f pct)\n", m_numActiveComposites,				100.0f * (F32)m_numActiveComposites / fNumFastObjects);
		MsgCon("#  Inactive  %04u (%.1f pct)\n", m_numInactiveComposites,			100.0f * (F32)m_numInactiveComposites / fNumFastObjects);
		MsgCon("\n");
		MsgCon("# Non-Comp   %04u (%.1f pct)\n", m_numNonCompositeObjects,			100.0f * (F32)m_numNonCompositeObjects / fNumFastObjects);
		MsgCon("#  Loc Chgd  %04u (%.1f pct)\n", m_numLocatorChangedObjects,		100.0f * (F32)m_numLocatorChangedObjects / fNumFastObjects);
		MsgCon("#  Identity  %04u (%.1f pct)\n", m_numIdentityObjects,				100.0f * (F32)m_numIdentityObjects / fNumFastObjects);
		MsgCon("\n");
		MsgCon("# BP ProcUp  %04u (%.1f pct) / %04u\n", m_numBp_ProcessUpdate,		100.0f * (F32)m_numBp_ProcessUpdate / (F32)m_num_ProcessUpdate, m_num_ProcessUpdate);
		MsgCon("# BP AnimUp  %04u (%.1f pct) / %04u\n", m_numBp_AnimationUpdate,	100.0f * (F32)m_numBp_AnimationUpdate / (F32)m_num_AnimationUpdate, m_num_AnimationUpdate);
		MsgCon("# BP APass01 %04u (%.1f pct) / %04u\n", m_numBp_AnimPass01,			100.0f * (F32)m_numBp_AnimPass01 / (F32)m_num_AnimPass01, m_num_AnimPass01);
		MsgCon("# BP ACmdLst %04u (%.1f pct) / %04u\n", m_numBp_AnimCmdList,		100.0f * (F32)m_numBp_AnimCmdList / (F32)m_num_AnimCmdList, m_num_AnimCmdList);
		MsgCon("\n");
		MsgCon("Max I Joints %04u\n", m_maxIdentityJoints);
		MsgCon("-------------\n");
	}

	memset(this, 0, sizeof(*this));
}


/// --------------------------------------------------------------------------------------------------------------- ///
void FillOutputControlsPerSegmentInfo(OutputControlsPerSegment& outputControlInfo, const ArtItemSkeleton* pSkel)
{
	OutputControlsPerSegment outputControlsPerSegment;
	U32 outputControlBaseForSegment = 0;
	U32 numOutputControls = 0;
	outputControlInfo.m_numSegments = pSkel->m_numSegments;
	ANIM_ASSERT(pSkel->m_numSegments <= OutputControlsPerSegment::kMaxSegments);
	for (U32 segmentIndex = 0; segmentIndex < pSkel->m_numSegments; ++segmentIndex, outputControlBaseForSegment += numOutputControls)
	{
		numOutputControls = ndanim::GetNumOutputControlsInSegment(pSkel->m_pAnimHierarchy, segmentIndex);
		outputControlInfo.m_numOutputControlsPerSegment[segmentIndex] = numOutputControls;
		outputControlInfo.m_outputOffsetPerSegment[segmentIndex] = outputControlBaseForSegment;
		outputControlBaseForSegment += numOutputControls;
	}
}
