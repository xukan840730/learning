/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/job/job-mutex.h"
#include "corelib/math/aabb.h"

#include "ndlib/anim/bounding-data.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/process/process.h"
#include "ndlib/resource/resource-table.h"
#include "ndlib/scriptx/h/dc-types.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimControl;
class JointLimits;
class JointSet;
struct JointFlipData;
struct RopeSkinningData;
struct SkelComponentDesc;

namespace ndanim
{
	struct JointHierarchy;
}

namespace ndgi
{
	struct Label32;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct MotionBlurTransforms
{
	Mat44 m_objectToWorld;             // Needed in case we pause.
	Mat44 m_objectToWorldLastFrame;
};


/// --------------------------------------------------------------------------------------------------------------- ///
struct OutputControlsPerSegment
{
	enum { kMaxSegments = 32 };
	U32 m_numSegments;
	U16 m_numOutputControlsPerSegment[kMaxSegments];
	U16 m_outputOffsetPerSegment[kMaxSegments];
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(128) FgAnimData
{
	enum
	{
		kDisableBSphereCompute				= 1 << 0,
		kDisableAnimation					= 1 << 1,
		kDisableVisOcclusion				= 1 << 2,
		kEnableAsyncPostAnimUpdate			= 1 << 3,
		kEnableAsyncPostAnimBlending		= 1 << 4,
		kEnableAsyncPostJointUpdate			= 1 << 5,


		kAllowAutomaticObjXformUpdate		= 1 << 6,		// ??
		kJointParamsLsDirty					= 1 << 7,		// Optimization?
		kTestIsInBindPose					= 1 << 8,		// ???
		kIsInBindPose						= 1 << 9,		// Optimization
		kDisabledAnimData					= 1 << 10,		// Optimization
		kParallelDisabledUpdate				= 1 << 11,		// If animation disabled the update can be run parallel to the character buckets

		kJointsMoved						= 1 << 12,

		kRunningAnimationPass				= 1 << 13,		// Latch for when we are running this object's animation pass
		kEnableClothUpdate					= 1 << 14,
		kRequireHiresAnimGeo				= 1 << 15,		// This animation requires the hires mesh LOD chain

		kAccumulateDisabledDeltaTime		= 1 << 16,		// If animation is disabled, we will accumulate dt and apply it when re-enabled
		kForceAnimateAllSegments			= 1 << 17,		// Force this object to animate all segments

		kCameraCutNotificationsEnabled		= 1 << 18,		// Notify camera system whenever camera cuts are detected
		kCameraCutsDisabled					= 1 << 19,		// Respect camera cut markup in animations (i.e., disable anim lerp between cut keys)

		kDisableMotionBlurThisFrame			= 1 << 20,

		kAnimConfigChangePending			= 1 << 21,

		kForceNextPausedEvaluation			= 1 << 22,		// force render matrices to be created next execution, even if gameplay is paused

		kRegisterObjIdsThisFrame			= 1 << 23,
	};

	// This governs the general animation steps taken for this object
	// This setting will update the animMode and animResultMode after each animation pass.
	enum AnimConfig
	{
		// No animation... all joint data is taken from the joint cache to
		// generate render matrices.
		kAnimConfigNoAnimation,

		// No animation... all joint data is taken from the joint cache to
		// generate render matrices. Only difference from above is that it
		// seems to just ensure that the local-space joint params exist...
		kAnimConfigNoAnimationForCloth,

		// One pass, after Process::Update() that will generate
		// world space transforms and render matrices.
		kAnimConfigSimpleAnimation,

		// Two passes, one after Process::Update() that will generate local-
		// space joint params. Words space locators can then be requested and will be 
		// converted on demand if they are not sdk joints. The second
		// pass will use what is in the joint cache to generate new world-
		// space transforms as well as render matrices.
		kAnimConfigComplexAnimation,

		// Two passes, one after Process::Update() that will generate local-
		// space joint params as well as world-space transforms for all base semgnet joints. 
		// The second pass will use what is in the joint cache to generate new world-
		// space transforms as well as render matrices.
		kAnimConfigComplexWithSdkJointsInFirstPass,
	};

	enum
	{
		kAnimSourceModeNone,				    // Do not do anything at all this pass.
		kAnimSourceModeClipData,			    // Execute the anim control or anim instance to evaluate clip data.
		kAnimSourceModeJointParams,			    // Pulls joint data directly from the local space joints in the anim execution context from pass 0
	};
	typedef U8 AnimSourceMode;

	enum
	{
		kAnimResultNone					= 0,
		kAnimResultJointTransformsOs	= (1 << 0),
		kAnimResultJointParamsLs		= (1 << 1),
		kAnimResultSkinningMatricesOs   = (1 << 2),

		// Helper enums...
		kAnimResultJointTransformsAndJointParams		= kAnimResultJointTransformsOs | kAnimResultJointParamsLs,
		kAnimResultJointTransformsAndSkinningMatrices  = kAnimResultJointTransformsOs | kAnimResultSkinningMatricesOs,
	};
	typedef U8 AnimResultMode;

	typedef void (*UserAnimationPassFunctor)(FgAnimData* pAnimData, F32 dt);

	struct PluginParams
	{
		StringId64	m_pluginName;
		void*		m_pPluginData;
		U32			m_pluginDataSize;
		bool		m_enabled : 1;
	};

	static void StartUp();
	static void DebugDraw();

	FgAnimData();
	void Init(const ArtItemSkeletonHandle skelHandle,
			  const Transform& xform,
			  JointCache::ConfigType configType = JointCache::kConfigNormal);
	void Reset();
	void OnFree();

	void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound);
	void ResetPersistentRigData();
	void OnTeleport();

	StringId64 GetJointSid(U32 iJoint) const;
	I16 FindJoint(StringId64 jid, bool useAnimateSkel = false) const;

	void ChangeAnimConfig(AnimConfig config);
	void PerformAnimConfigChange();
	void CheckPendingAnimConfigChange();
	AnimConfig GetAnimConfig() const { return static_cast<AnimConfig>(m_animConfig); }
	AnimSourceMode GetAnimSourceMode(U32F pass) const;
	AnimResultMode GetAnimResultMode(U32F pass) const;
	void SetAnimSourceMode(U32F pass, AnimSourceMode animSourceMode);
	void SetAnimResultMode(U32F pass, AnimResultMode resultMode);

	void SetXform(const Locator& loc, bool invalidateJointCacheWsLocs = true);
	void SetXform(const Transform& transform, bool invalidateJointCacheWsLocs = true);
	Locator GetXform() const;
	bool AllowLodCull() const { return m_allowLodCull; }

	bool SetJointsMoved()		{ return m_flags |= kJointsMoved; }
	bool HasJointsMoved() const	{ return 0 != (m_flags & kJointsMoved); }
	void ClearJointsMoved()		{ m_flags &= ~kJointsMoved; }

	void SetParallelDisabledUpdate() { m_flags |= kParallelDisabledUpdate; }
	void ClearParallelDisabledUpdate() { m_flags &= ~kParallelDisabledUpdate; }
	bool GetParallelDisabledUpdate() const { return 0 != (m_flags & kParallelDisabledUpdate); }

	bool IsDisabledAndNotParallel() const
	{
		return (m_flags & (kDisabledAnimData | kParallelDisabledUpdate)) == (kDisabledAnimData);
	}
	bool IsDisabledAndUpdatesInParallel() const
	{
		return (m_flags & (kDisabledAnimData | kParallelDisabledUpdate))
			   == (kDisabledAnimData | kParallelDisabledUpdate);
	}

	void SetRequireHiresAnimGeo() { m_flags |= kRequireHiresAnimGeo; }
	void ClearRequireHiresAnimGeo() { m_flags &= ~kRequireHiresAnimGeo; }
	bool RequiresHiresAnimGeo() const { return 0 != (m_flags & kRequireHiresAnimGeo); }

	void SetRunningAnimationPass(bool running)
	{
		if (running)
			m_flags |= kRunningAnimationPass;
		else
			m_flags &= ~kRunningAnimationPass;
	}

	const char* GetOutputControlName(I32F iOutput) const;

	Transform GetRenderTransform() const { return m_objXform; }

	bool AreCameraCutsDisabled() const { return (m_flags & kCameraCutsDisabled); }
	void SetCameraCutsDisabled(bool disable)
	{
		if (disable)
		{
			m_flags |= kCameraCutsDisabled;
		}
		else
		{
			m_flags &= ~kCameraCutsDisabled;
		}
	}

	bool AreCameraCutNotificationsEnabled() const { return (m_flags & kCameraCutNotificationsEnabled); }
	void SetCameraCutNotificationsEnabled(bool enable)
	{
		if (enable)
		{
			m_flags |= kCameraCutNotificationsEnabled;
		}
		else
		{
			m_flags &= ~kCameraCutNotificationsEnabled;
		}
	}

	void DisableMotionBlurThisFrame() { m_flags |= kDisableMotionBlurThisFrame; }

	U32									m_flags;
	U8									m_animConfig;
	AnimSourceMode						m_animSourceMode[2];
	AnimResultMode						m_animResultMode[2];
	float								m_animClockScale;

	AnimControl*						m_pAnimControl;			// Relo - Owned by NdGameObject
	ArtItemSkeletonHandle				m_curSkelHandle;
	const ndanim::JointHierarchy*		m_pSkeleton;			// No relo - Resides in the level heap

	JointCache							m_jointCache;			// Relo - 5 pointers (Mem owned by NdGameObject)
	const JointFlipData*				m_pJointFlipData;		// No relo
	const SkelComponentDesc*			m_pJointDescs;			// No relo
	Vector								m_scale;

	Vec4								m_visSphere;			// offset + radius of the vis Sphere
	I32									m_visSphereJointIndex;	// joint to attach bounding sphere/aabb to
	Aabb								m_visAabb;				// visibility AABB calculated and provided by the tools
	F32									m_dynamicBoundingBoxPad;
	bool								m_useBoundingBox;

	U64									m_padding;
	ArtItemSkeletonHandle				m_animateSkelHandle;

	MutableProcessHandle				m_hProcess;
	Transform							m_objXform;
	bool								m_allowLodCull;

	BoundingData*						m_pBoundingInfo;
	U32									m_maxNumDrivenInstances;
	U32									m_numDrivenInstances;
	U32*								m_pDrivenInstanceIndices;

	Vec4*								m_pMotionBlurBoneMats;
	MotionBlurTransforms*				m_pMotionBlurXforms;

	float								m_clothBoundingBoxMult;					// Giant hack for T1PS4 to increase size of bounding boxes for cloth, since the joint positions at the time of animation are incorrect

	U32									m_earlyDeferredSegmentMaskGame;				// Mask of deferred segments that we think we will need to update processes in upcoming buckets
	U32									m_earlyDeferredSegmentMaskRender;			// Mask of deferred segments that we think we will need for rendering

	I16									m_boundingSphereExcludeJoints[2];
	PluginParams*						m_pPluginParams;

	ndjob::CounterHandle				m_pWaitCounter;
	UserAnimationPassFunctor			m_userAnimationPassCallback[2];
	I16									m_visVolIndex;

	U32									m_extraAnimJobFlags;
	JointLimits*						m_pJointLimits;
	JointSet*							m_pPluginJointSet;
	RopeSkinningData*					m_pRopeSkinningData;						// Extra data needed for special "rope skinning"

	bool								m_useLargeAnimCommandList;
	bool								m_forceDeferredAnimation;
	DC::AnimLod							m_animLod;

	F32									m_disabledDeltaTime;

	void*								m_pPersistentData;

	ndjob::Mutex						m_deferredAnimLock;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static const I32 kMaxIdentityJoints = 225;
extern Vec4* g_pIdentityBoneMats; // [kMaxIdentityJoints * 3]

// sanity-check the bindpose optimization, but easily take out the asserts for full performance
#if 0
#define BINDPOSE_ASSERT(expr)			ANIM_ASSERT(expr)
#define BINDPOSE_ASSERTF(expr, msg)		ANIM_ASSERTF(expr, msg)
#else
#define BINDPOSE_ASSERT(expr)
#define BINDPOSE_ASSERTF(expr, msg)
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimDataOptimization
{
	bool m_drawDisabledUpdates;
	bool m_disableBindPoseOpt;
	bool m_disableBindPoseWaitForJobsOpt;
	bool m_disableBindPoseVisibilityOpt;

	bool m_testopt_SkipWaitForJobs;
	bool m_testopt_SkipFastAnimUpdate;
	bool m_testopt_SkipPhysUpdate;
	bool m_testopt_SkipShimmer;
	bool m_testopt_SkipPhysFxInBindPose;
	bool m_testopt_SkipPhysFx;

	// stats
	struct Stats
	{
		U32 m_numFastObjects;
		U32 m_numBpObjects;
		U32 m_numVisibleObjects;
		U32 m_numPropagateBoneMats;

		U32 m_numCompositeBodyObjects;
		U32 m_numIdentityComposites;
		U32 m_numInactiveComposites;
		U32 m_numActiveComposites;

		U32 m_numNonCompositeObjects;
		U32 m_numLocatorChangedObjects;
		U32 m_numIdentityObjects;

		U32 m_maxIdentityJoints;

		U32 m_num_ProcessUpdate;
		U32 m_num_AnimationUpdate;
		U32 m_num_AnimPass01;
		U32 m_num_AnimCmdList;
		U32 m_num_PhysFx;
		U32 m_numBp_ProcessUpdate;
		U32 m_numBp_AnimationUpdate;
		U32 m_numBp_AnimPass01;
		U32 m_numBp_AnimCmdList;
		U32 m_numBp_DrawPass01;

		void Reset();
	};
	Stats m_stats;

	AnimDataOptimization();
};

extern AnimDataOptimization g_animOptimization;

void FillOutputControlsPerSegmentInfo(OutputControlsPerSegment& outputControlInfo, const ArtItemSkeleton* pSkel);
