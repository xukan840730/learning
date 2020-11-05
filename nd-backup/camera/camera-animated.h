/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef NDLIB_CAMERA_ANIMATED_H
#define NDLIB_CAMERA_ANIMATED_H

#include "ndlib/process/bound-frame.h"
#include "ndlib/scriptx/h/animation-script-types.h"

#include "gamelib/camera/camera-animation-handle.h"
#include "gamelib/camera/camera-control.h"
#include "gamelib/camera/camera-fixed.h"
#include "gamelib/camera/camera-shake.h"
#include "gamelib/camera/camera-manager.h"

class AnimControl;
namespace DMENU { class Menu; }

// ----------------------------------------------------------------------------------------------
// Globals and Constants
// ----------------------------------------------------------------------------------------------

extern bool g_debugDrawStateScriptCamera;
extern bool g_debugDrawStateScriptCameraLs;
extern bool g_enableNewAnimatedCameraExtension;

#define ANIMATED_CAMERA_ENABLE_THRESHOLD 0.5f		// value of CameraAnimation::activeChannel above which we consider the camera to be active

// ----------------------------------------------------------------------------------------------
// 
// ----------------------------------------------------------------------------------------------
struct CameraAnimatedLookTracker
{
	SpringTrackerAngle m_angleTracker;
	Vector m_lastDirection;
	bool m_firstFrame;

	CameraAnimatedLookTracker()
		: m_angleTracker()
		, m_lastDirection(kZero)
		, m_firstFrame(true)
	{

	}

	void Update(Point lookAtPoint, const Locator &origLocator, bool wasCut, Locator &modifiedLocator);
};

// ----------------------------------------------------------------------------------------------
// A request for a single channel of camera animation data
// ----------------------------------------------------------------------------------------------

struct CameraAnimationChannelRequest
{
	CameraAnimationChannelRequest() { Clear(); }
	void Clear()
	{
		baseAlignLoc = Locator(kIdentity);
		pAnimControl = nullptr;
		channelId = INVALID_STRING_ID_64;
		cameraIndex = 0;
		returnLocalSpace = false;
		pFilter = nullptr;
		blendActiveChannel = true;
		pFlip = nullptr;
		pFilterContext = nullptr;
		pAnimStateInstance = nullptr;
		pAnimSimpleInstance = nullptr;
	}

	Locator baseAlignLoc;
	AnimControl* pAnimControl;
	const AnimStateInstance* pAnimStateInstance;
	const AnimSimpleInstance* pAnimSimpleInstance;
	StringId64 channelId;
	int cameraIndex;
	bool returnLocalSpace;
	bool blendActiveChannel;
	CameraAnimationChannelFilterFunc* pFilter;
	CameraAnimationChannelFlipFunc* pFlip;
	const void* pFilterContext;
};

// ----------------------------------------------------------------------------------------------
// A single channel of camera animation data
// ----------------------------------------------------------------------------------------------

struct CameraAnimationChannel
{
	CameraAnimationChannel() { Clear(); }
	void Clear()
	{
		sqt.m_quat = Quat(kIdentity);
		sqt.m_scale = Vector(1.0f, 1.0f, 1.0f);
		sqt.m_trans = Point(kZero);

		numContributingStates = 0u;
		animStateName = INVALID_STRING_ID_64;
		phaseAnimName = INVALID_STRING_ID_64;
		frameIndex = -1.0f;
		cutCount = 0u;
		cutChannel = -1.0f;
	}

	ndanim::JointParams		sqt;					// scale-quaternion-translation

	U32						numContributingStates;	// number of states which contributed to the final camera anim data
	StringId64				animStateName;			// name of anim state from which camera data was extracted

	StringId64				phaseAnimName;			// name of the phase anim (for debugging)

	F32						frameIndex;				// frame index at which camera data was extracted
	U32						cutCount;				// number of "camera cut" markers encountered
	F32						cutChannel;				// actual value of the "camera cut" channel (scale.z)
};

// ----------------------------------------------------------------------------------------------
// A request for a full set of camera animation data
// ----------------------------------------------------------------------------------------------

struct CameraAnimationRequest
{
	CameraAnimationRequest() { Clear(); }
	void Clear()
	{
		frame = BoundFrame(kIdentity);
		pAnimControl = nullptr;
		cameraIndex = 0u;
		pFilter = nullptr;
		pFlip = nullptr;
		pFilterContext = nullptr;
		pAnimStateInstance = nullptr;
		pAnimSimpleInstance = nullptr;
	}

	BoundFrame frame;
	AnimControl* pAnimControl;
	const AnimStateInstance* pAnimStateInstance;
	const AnimSimpleInstance* pAnimSimpleInstance;
	int cameraIndex; // 0 for apReference-camera1, 1 for apReference-camera2, etc.
	CameraAnimationChannelFilterFunc* pFilter;
	CameraAnimationChannelFlipFunc* pFlip;
	const void* pFilterContext;
};

// ----------------------------------------------------------------------------------------------
// Depth of field camera animation data
// ----------------------------------------------------------------------------------------------

struct CameraDofRaw
{
	F32		dofBgCocScale;
	F32		dofFgCocScale;
	F32		dofBgCocThreshold;
	F32		dofFgCocThreshold;
	F32		filmWidth;
	F32		fNumber;
	F32		focalLen;
	F32		focalPlaneDist;
	F32		suppressDof;

	void	SetFromRenderSettings(const DC::RenderSettings& rs);
	void	Clear();
	bool	IsSuppressed() const { return (suppressDof >= 0.5f); }
	void	Lerp(const CameraDofRaw& a, const CameraDofRaw& b, float blend);
};

struct CameraDof : public CameraDofRaw
{
	CameraDof() { Clear(); }
};

// ----------------------------------------------------------------------------------------------
// Full camera animation data
// ----------------------------------------------------------------------------------------------

struct CameraAnimation
{
	static const F32 kInvalidCutChannelValue;
	static void Init();

	CameraAnimation() { Clear(); }
	void Clear();
	void Init(const BoundFrame& frame, float fov);
	static void Blend(CameraAnimation& result, const CameraAnimation& a, const CameraAnimation& b, float blend);
	void DebugDraw(CameraAnimationPriority priority, const char* name) const;

	BoundFrame				m_cameraFrameWs;			// final position and rotation of camera in world space
	BoundFrame				m_cameraFocalPointWs;		// final position of camera focal point in world space
	F32						m_fov;						// field of view angle
	F32						m_distortion;				// 3D distortion
	F32						m_zeroPlaneDist;			// 3D zero plane dist
	ndanim::JointParams		m_rawSqt;					// raw SQT (from CameraAnimationChannel)
	bool					m_isValid;					// are the cameraFrameWs and FOV fields valid?
	bool					m_hasFocalPoint;			// was the camera's focal point specified at all?
	bool					m_cut;						// did a cut occur this frame? (derived from cutChannel; see also cutCount)

	CameraDof				m_dof;						// depth of field information (valid when dof.isValid == true)

	I32						m_cameraIndex;				// camera index from which this data came
	U32						m_numContributingStates;	// number of states which contributed to the final camera anim data
	StringId64				m_animStateName;			// name of anim state from which camera data was extracted
	StringId64				m_phaseAnimName;			// name of the phase anim (debugging)

	F32						m_frameIndex;				// frame index at which camera data was extracted
	U32						m_cutCount;					// number of "camera cut" markers encountered
	F32						m_cutChannel;				// actual value of the "camera cut" channel (scale.z)

	F32						m_activeChannel;			// actual value of the "active camera" channel (translate.X of apReference-extra#)
	F32						m_slowmoChannel;			// actual value of the "slow mo" channel (scale.X of apReference-extra#)
};

// ----------------------------------------------------------------------------------------------
// Start info for CameraControlAnimated
// ----------------------------------------------------------------------------------------------

struct CameraAnimatedStartInfo : public CameraStartInfo
{
	CameraAnimationHandle m_hAnimSource;
	Vector m_offset;
	I32 m_cameraIndex;	// index specified in Maya (the 'N' in apReference-cameraN)
	StringId64 m_ownerId;
	CameraBlendInfo m_blendOutInfo;
	StringId64 m_animId;
	StringId64 m_focusAttachPoint;
	float m_scaleLocator;
	bool m_focusCentered : 1;
	bool m_enableLookAround : 1;
	bool m_lookAtAttachPoint : 1;
	bool m_noFailOver : 1;
	bool m_usedByAnimationDirector : 1;
	bool m_useAnalogZoom : 1;  // Use this camera to specify the fov for the analog zoom feature (-1 to turn off)

	CameraAnimatedStartInfo()
		: CameraStartInfo(SID("CameraAnimatedStartInfo"))
		, m_offset(kZero)
		, m_cameraIndex(0)
		, m_ownerId(INVALID_STRING_ID_64)
		, m_blendOutInfo() // defaults to "unspecified"
		, m_animId(INVALID_STRING_ID_64)
		, m_focusAttachPoint(INVALID_STRING_ID_64)
		, m_scaleLocator(1.0)
		, m_focusCentered(false)
		, m_enableLookAround(false)
		, m_lookAtAttachPoint(false)
		, m_noFailOver(false)
		, m_usedByAnimationDirector(false)
		, m_useAnalogZoom(false)
	{ }
};

// ----------------------------------------------------------------------------------------------
// CameraRequest extension for animated cameras
// ----------------------------------------------------------------------------------------------

class CameraRequestExtensionAnimated : public ICameraRequestExtension
{
public:
	virtual ~CameraRequestExtensionAnimated() override;
	virtual void UpdateCameraRequest(CameraManager& mgr, CameraRequest& request) override;
	virtual bool ShouldReevaluateCameraRequests(CameraManager& mgr, CameraRequest& request) override { return true; }
	virtual bool HasSources() override;
	virtual void IncrementReferenceCount() override;
	virtual int DecrementReferenceCount() override;

	void Init();
	void Destroy();

	void AddCameraAnimSource(int playerIndex, const CameraAnimationHandle& hCameraAnimSource, int cameraIndex); // add an animation source

	static bool ExtractAnimation(CameraAnimation& outputAnim, 
								 const CameraAnimationHandleList& sourceList, 
								 int cameraIndex, 
								 NdGameObjectHandle* phSourceGo = nullptr); // return the animation from the highest-priority animation source

	bool ExtractAnimation(CameraAnimation& outputAnim, int cameraIndex, bool noFailOver = false);

	// ExtractAnimationConst: evaluate camera animation and get future camera locator.
	bool ExtractAnimationConst(CameraAnimation& outputAnim, int cameraIndex) const;

	static AnimInstanceHandle GetAnimSourceInstance(const CameraAnimationHandleList& sourceList, int cameraIndex);
	AnimInstanceHandle GetAnimSourceInstance(int cameraIndex) const;

	bool IsDormant(int cameraIndex);

	void UpdateSourceList();

	//const CameraAnimationHandleList& GetAnimSources() const { return m_sourceList; }
	//CameraAnimationHandleList& GetAnimSources() { return m_sourceList; }

private:
	CameraAnimationHandleList	m_sourceList;
	I32							m_refCount;
	I32							m_cameraIndex;
	NdGameObjectHandle			m_hSourceGo;
};

// ----------------------------------------------------------------------------------------------
// Custom CameraConfig for animated cameras
// ----------------------------------------------------------------------------------------------

class CameraConfigAnimated : public CameraConfig
{
public:
	CameraConfigAnimated(const char* pCameraIdName,
				 const char* pProcessType,
				 CameraStartInfo& defaultStartInfo,
				 U32 startInfoSize,
				 CameraPriority priority,
				 CameraRank rank)
		: CameraConfig(pCameraIdName, pProcessType, defaultStartInfo, startInfoSize, priority, rank)
	{
	}

	virtual ~CameraConfigAnimated() override;

	virtual bool IsAnimated() const override { return true; }
	virtual ICameraRequestExtension* CreateCameraRequestExtension() override;
	virtual void DestroyCameraRequestExtension(ICameraRequestExtension* pExt) override;

private:
	static const int kCapacity = 16;
	static CameraRequestExtensionAnimated	s_pool[kCapacity];
	static BitArray<kCapacity>				s_poolAllocatedBits;
	static size_t							s_poolHighWater;
};

// ----------------------------------------------------------------------------------------------
// CameraControlAnimated
//
// An animated camera, driven by data located on apReference channels within an animation.
// Position, rotation and field of view data come from a channel called 'apReference-cameraX'
// where 'X' can be 1 thru N for up to N cameras. (We almost always use 'apReference-camera1'.)
// Optional depth of field data comes from a channel called 'apReference-dofX'.
// Optional focal point position comes from a channel called 'apReference-focalpointX'.
//     The focal point indicates the area of interest in a scene, and can potentially be used to aid in
//     adjusting the camera; e.g. to avoid collision.
//     This class does not do any such adjustment, but can be subclassed to do so.
// ----------------------------------------------------------------------------------------------

FWD_DECL_PROCESS_HANDLE(CameraControlAnimated);

class CameraControlAnimated : public CameraControl
{
private:
	typedef CameraControl ParentClass;

public:
	FROM_PROCESS_DECLARE(CameraControlAnimated);

	int m_cameraIndex;
	CameraShake m_cameraShake;
	StringId64 m_ownerId;
	float m_scaleLocator;
	float m_prevCutChannel;
	bool m_focusCentered;
	bool m_enableLookAround;
	bool m_lookAtAttachPoint;
	bool m_noFailOver;
	bool m_usedByAnimationDirector;
	Vector m_offset;
	Vector m_proceduralOffset = kZero;
	SpringTracker<Vector> m_offsetTracker;

	StringId64 m_animId;
	StringId64 m_uniqueId;
	StringId64 m_focusAttachPoint;

	CameraMoveRotate m_cameraControl;					// for m_enableLookAround
	CameraAnimatedLookTracker m_lookTracker;			// for m_lookAtAttachPoint

	CameraAnimationHandleList m_sourceHandleList;
	CameraAnimationHandle m_hAnimSource;
	CameraAnimation m_finalAnimation;

	CameraRequestExtensionAnimated* m_pExtension;

	// For animation director blending out
	BoundFrame m_lastAnimBoundFrame;
	StringId64 m_lastAnimId;
	F32 m_lastFrameIndex;
	F32 m_prevFrameIndex;
	F32 m_lastFps;
	Point m_lastFocusPos;

	F32 m_cameraSpeed;

	F32 m_startFov;

	F32 m_analogFovTt;
	SpringTracker<F32> m_fovTracker;

	// auto-gen-outro
	AutoGenScriptParams m_autoGenScriptParams; // if fade-out-dist is positive, auto-generate a strafe-camera from me and fade out by dist.
	float m_toAutoGenFadeOutTime = -1.f;

	Vector m_nextCameraInitDir = kZero;
	bool m_disableNextCameraInputUntil70 = false; // prevent next camera rotates the opposite of my direction and causes final blended camera pop.

public:
	virtual CameraBlendInfo CameraStart(const CameraStartInfo& baseStartInfo) override;
	bool IsEquivalentTo(const CameraStartInfo& baseStartInfo) const override;
	Err Init(const ProcessSpawnInfo& spawnInfo) override;

	virtual void UpdateCuts (const CameraInstanceInfo& info) override;

	virtual CameraLocation UpdateLocation(const CameraInstanceInfo& info) override;

	virtual void DOFUpdate(const CameraInstanceInfo& info) override;

	virtual void OnKillProcess() override;

	bool GetRunWhenPaused() const override;

	virtual bool AllowCameraLookAt() const override { return false; }

	float GetCameraSpeed() const { return m_cameraSpeed; }
	virtual bool IsStaticCamera() const override { return true; }

	void SetNextCameraAutoGen(const AutoGenScriptParams& scriptParams) { m_autoGenScriptParams = scriptParams; }
	const AutoGenScriptParams& GetNextCameraAutoGenScriptParams() const { return m_autoGenScriptParams; }

	void SetToAutoGenFadeOutTime(float fadeOutTime) { m_toAutoGenFadeOutTime = fadeOutTime; }
	float GetToAutoGenFadeOutTime() const { return m_toAutoGenFadeOutTime; }

	Maybe<Vector> GetNextCameraInitDir() const 
	{ 
		if (IsNormal(m_nextCameraInitDir))
			return m_nextCameraInitDir;
		else
			return MAYBE::kNothing;
	}
	void SetNextCameraInitDir(Vector_arg dir) { m_nextCameraInitDir = SafeNormalize(dir, Vector(kUnitZAxis)); }

	bool GetNextCameraInputDisabledUntil70() const { return m_disableNextCameraInputUntil70; }
	void SetNextCameraInputDisabledUntil70(bool f) { m_disableNextCameraInputUntil70 = f; }

	AnimInstanceHandle GetAnimSourceInstance() const;
	void BlendToOffset(Vector_arg offset);
	void SetOffset(Vector_arg offset) { m_offset = offset; }
protected:

	virtual void FillDebugText(StringBuilder<1024>& sb, bool shortList, const DebugShowParams& params) const override;

	// subclasses can override to grab additional info from CameraAnimation
	virtual void ApplyCameraAnimation(const CameraAnimation& anim);

	Locator GetAnimatedCamLocator() const;
	virtual Locator ModifyAnimatedLocator(float *pFov, const CameraInstanceInfo &info, const Locator &loc, bool wasCut);

	CameraAnimatedStartInfo GetStartInfo() const;
};

CAMERA_DECLARE_TYPED(kCameraAnimated, CameraConfigAnimated);
PROCESS_DECLARE(CameraControlAnimated);

// ----------------------------------------------------------------------------------------------
// CameraControlAnimatedOverride
// ----------------------------------------------------------------------------------------------

class CameraControlAnimatedOverride : public CameraControlAnimated
{
public:
	virtual bool KeepLowerCamerasAlive() const override { return true; }
	virtual bool AffectGameCameraLocator() const override { return false; }

};
CAMERA_DECLARE_TYPED(kCameraAnimatedOverride, CameraConfigAnimated);

// ----------------------------------------------------------------------------------------------
// API for extracting camera data from an animation
// ----------------------------------------------------------------------------------------------

bool	GetCameraAnimationChannel(const CameraAnimationChannelRequest& request, CameraAnimationChannel& channel);
bool	GetCameraAnimation(const CameraAnimationRequest& request, CameraAnimation& animation);
bool	IsCameraAnimationAvailable(const CameraAnimationRequest& request);
float   GetFovFromRawCameraScale(Vector_arg cameraScale);

// ----------------------------------------------------------------------------------------------
// Global Camera Animation Sources (for IGCs)
// ----------------------------------------------------------------------------------------------

void AddPersistentCameraAnimationSource(int playerIndex, const CameraAnimationHandle& hCameraAnimSource, int cameraIndex = 0);

// ----------------------------------------------------------------------------------------------
// Dev Menu
// ----------------------------------------------------------------------------------------------

struct AnimatedCameraOptions
{
	FINAL_CONST bool m_showSources = false;
	FINAL_CONST bool m_useOldExtension = false; // new extension system is where the camera extension is "owned" by the camera, not the camera request, so it can continue to update after the IGC/cine fades out
	FINAL_CONST bool m_forceFailOver = false;
	FINAL_CONST bool m_disallowFailOver = false;
	FINAL_CONST bool m_disableDummyCameraObjects = false;
	bool m_retireOldCameraSources = false;
};
extern AnimatedCameraOptions g_animatedCameraOptions;

void PopulateAnimatedCameraMenu(DMENU::Menu* pMenu, I64* pSceneCameraID);

#endif // NDLIB_CAMERA_ANIMATED_H

