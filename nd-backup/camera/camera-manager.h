/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

///
/// For further documentation, see http://wiki-dog:8080/ndi/Big/Camera
///

#ifndef CAMERA_MANAGER_H
#define CAMERA_MANAGER_H

#include "corelib/containers/linkednode.h"
#include "corelib/math/locator.h"
#include "corelib/util/angle.h"
#include "corelib/util/timeframe.h"

#include "ndlib/camera/camera-interface.h"
#include "ndlib/camera/camera-config.h"
#include "ndlib/camera/camera-final.h"
#include "ndlib/camera/camera-location.h"
#include "ndlib/camera/camera-option.h"
#include "ndlib/camera/camera-start-info.h"
#include "ndlib/camera/camera-blend.h"
#include "ndlib/process/process.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/ndphys/water-detector.h"
#include "gamelib/ndphys/vehicle/phys-vehicle-winch.h"


class Joypad;
class CameraNode;
class CameraControl;
class CameraManager;
struct CameraJoypad;
struct SceneCameraTable;
class RegionControl;
class SceneWindow;
class CameraLookAtController;
struct CameraRequest;

// -------------------------------------------------------------------------------------------------
// Camera Start Info Storage
// -------------------------------------------------------------------------------------------------

// Reserves room to be used by classes derived from CameraStartInfo.  No derived
// class may use more than this amount of storage - increase if necessary.
struct CameraStartInfoStorage : public CameraStartInfo
{
	// The camera infos are bigger on x64
	U32	m_derivedClassStorage[160];
};

// -------------------------------------------------------------------------------------------------
// Scene Table
// -------------------------------------------------------------------------------------------------

class CameraSceneTable : public LinkedNode<CameraSceneTable>
{
public:
	SceneCameraTable * m_table;	//@relo external
};

// -------------------------------------------------------------------------------------------------
// ICameraRequestExtension: extension interface for a CameraRequest
// -------------------------------------------------------------------------------------------------

class ICameraRequestExtension
{
public:
	virtual ~ICameraRequestExtension();
	virtual void UpdateCameraRequest(CameraManager& mgr, CameraRequest& request) = 0;
	virtual bool ShouldReevaluateCameraRequests(CameraManager& mgr, CameraRequest& request) { return false; }
	virtual bool HasSources() = 0;
	virtual void IncrementReferenceCount() { }
	virtual int DecrementReferenceCount() { return 0; }
};

// -------------------------------------------------------------------------------------------------
// CameraRequest
// -------------------------------------------------------------------------------------------------

struct CameraRequest : public LinkedNode<CameraRequest, CameraRequest*>
{
public:
	struct AssociationId
	{
	public:
		AssociationId()
			: m_id(INVALID_STRING_ID_64)
			, m_camIdx(0)
		{}

		StringId64	m_id;
		I32			m_camIdx;	// camera index is for animated camera, from the same source object, it can have different camera index.

		bool operator == (const AssociationId& b) const
		{
			return m_id == b.m_id && m_camIdx == b.m_camIdx;
		}
		bool operator != (const AssociationId& b) const
		{
			return m_id != b.m_id || m_camIdx != b.m_camIdx;
		}
	};

	CameraId							m_cameraId;		// id of camera we want to start
	AssociationId						m_associationId; // uniquely identifies this camera for purposes of disabling/abandoning
	CameraBlendInfo						m_blendInfo;
	MutableCameraControlHandle			m_hCamera;		// this request's camera, if currently running
	ICameraRequestExtension*			m_pExtension;
	bool								m_isActive;		// does this request contain real data?
	bool								m_isAbandoned;
private:
	bool								m_isDormant;	// dormant requests remain in the persistent request list, but act is if they're not there
public:
	CameraStartInfoStorage				m_startInfo;	// big enough for any derived class of CameraStartInfo

	// for debugging.
	const char*							m_file;
	int									m_line;
	const char*							m_func;

	CameraRequest()
		: m_cameraId(kCameraNone)
		, m_hCamera(nullptr)
		, m_pExtension(nullptr)
		, m_isActive(false)
		, m_isAbandoned(false)
		, m_isDormant(false)
		, m_file(nullptr)
		, m_line(0)
		, m_func(nullptr)
	{}

	void Clear()
	{
		if (m_pExtension != nullptr)
			m_pExtension->DecrementReferenceCount();
		m_pExtension = nullptr;

		m_cameraId = kCameraNone; //@kCameraNone

		m_associationId = AssociationId();
		m_blendInfo = CameraBlendInfo();
		m_hCamera = nullptr;
		m_isActive = false;
		m_isAbandoned = false;
		m_isDormant = false;

		m_file = nullptr;
		m_line = 0;
		m_func = nullptr;
	}

	void Init(CameraId cameraId, AssociationId associationId, const CameraBlendInfo* pBlendInfo, const CameraStartInfo* pStartInfo, NdGameObjectHandle hDefaultFocusObject);
	void Reinit(CameraId cameraId, const CameraStartInfo* pStartInfo);
	CameraPriority GetPriority() const;
	CameraRank GetRank() const;
	AssociationId GetAssociationId() const { return m_associationId; }
	bool IsDormant() const { return m_isDormant; }

	friend class CameraManager;
};

// -------------------------------------------------------------------------------------------------
// Game-Specific CameraManager Customizations
// -------------------------------------------------------------------------------------------------

class ICameraManagerCustom
{
public:
	// Hook into CameraManager::Init().
	virtual void Init(CameraManager& self) { }

	// Hook into CameraManager::ShutDown().
	virtual void ShutDown(CameraManager& self) { }

	// Hook into CameraManager::PreProcessUpdate().
	virtual void PreProcessUpdate(CameraManager& self) { }

	// Hook into CameraManager::AdjustToWaterSurface().
	virtual CameraLocation AdjustToWaterSurface(CameraManager& self, const CameraLocation &camLoc, const CamWaterPlane& waterPlane) { return camLoc; }

	// Hook into CameraManager::AdjustToWind().
	virtual CameraLocation AdjustToWind(CameraManager& self, const CameraLocation &camLoc) { return camLoc; }

	virtual bool IsCamUnderwater() const { return false; }

	// This function is called whenever RequestCamera() or RequestPersistentCamera() is called.
	// The function should return true to allow the request, or false to abort it.
	// The function is also free to alter the request in some way, as it sees fit.
	//
	// IMPORTANT: The CameraStartInfo contained within the CameraRequest is actually an instance
	// of CameraStartInfoStorage, so it is large enough to contain *any* derived CameraStartInfo.
	// This is helpful when you change the CameraId of the request, and the new CameraId requires
	// a *different* kind of CameraStartInfo than what was originally requested.
	virtual bool OnRequestCamera(CameraManager& self,
								 CameraRequest& request,
								 bool isPersistent) { return true; }

	// This function is called on every non-abandoned persistent CameraRequest during the search
	// for the best (highest priority) persistent request.  This function can return false to
	// suppress a particular kind of persistent camera under appropriate circumstances, even when
	// that request's priority would normally have caused it to be selected as "best".
	virtual bool IsPersistentRequestValid(CameraManager& self,
										  const CameraRequest& persistentRequest,
										  const CameraRequest* pCurrentRequest) { return true; }

};

class CameraInterfaceGamelib : public CameraInterface
{
public:
	virtual void NotifyCameraCut(const char* file, int line, const char* func) override;
};

// -------------------------------------------------------------------------------------------------
// CameraManager
// -------------------------------------------------------------------------------------------------

FWD_DECL_PROCESS_HANDLE(CameraManager);

class CameraManager : public Process
{
	typedef Process ParentClass;

	STATE_DECLARE_OVERRIDE(Active);

public:
	struct CameraResult
	{
		bool	isManualOnTop;			// is manual camera on top of stack.
		bool	isNoManualUnderwater;	// is non manual camera is below water.
	};

	typedef U32 CameraManagerFlags;

	static const CameraManagerFlags kDebugShowCameras		= (1 << 0);	// show on msgcon
	static const CameraManagerFlags kDebugDrawCameras		= (1 << 1);	// draw in 3D
	static const CameraManagerFlags kDebugShowLocator		= (1 << 2);
	static const CameraManagerFlags kEnableUpdateWhenPaused	= (1 << 3);
	static const CameraManagerFlags kGameDebugDrawCameras	= (1 << 4);	// draw in 3D (controlled by the game)
	static const CameraManagerFlags kDebugShowCamerasShort	= (1 << 5);	// show on msgcon
	static const CameraManagerFlags kDebugShowRotationSpeed	= (1 << 6); // show rotation speed on msgcon
	static const CameraManagerFlags kDebugShowOverlay		= (1 << 7); // show current overlay on msgcon
	static const CameraManagerFlags kDebugShowRequestFunc	= (1 << 8); // show camera is requested by which func.
	static const CameraManagerFlags kDebugShowCameraLocator = (1 << 9); // print camera locators
	static const CameraManagerFlags kDebugShowOverlayDebug	= (1 << 10); // show debug for which camera overlay gets chosen on msgcon

	typedef LinkedList<CameraNode>	CameraNodeList;

	enum class ExcludeAbandoned { kInclude = 0, kExclude = 1 };
	enum class ReapAbandoned { kDontReap = 0,  kReap = 1 };
	enum class IncludeDormant { kExclude = 0, kInclude = 1 };

	CameraManager();
	virtual ~CameraManager() override;

	// Static Interface

	static void Init(ICameraManagerCustom** pCustoms);
	static void ShutDown();
	static CameraManagerHandle GetHandle();
	static CameraManagerHandle GetGlobalHandle(int index);
	static CameraManager* GetPtr();
	static CameraManager* GetGlobalPtr(int index);
	static CameraManager& Get();
	static CameraManager& GetGlobal(int index);
	static CameraControl* CreateControlFromName(int playerIndex, StringId64 typeNameId, const CameraStartInfo* pStartInfo);
	static bool IsOnScreen(int playerIndex, Point pos, float radius = 0.0f);
	static Point GetClosestPointOnScreen(int playerIndex, Point_arg pos, float radius = 0.0f);
	static Scalar TargetAngle(int playerIndex, Point pos);
	static void OverrideNearPlaneDist(float nearPlaneDist, U32F priority);		// Only for one frame! Higher or equal priority overrides the value.
	static void SetDefaultNearPlaneDist(float nearPlaneDist);						// Sets default for the CameraControl(s) which don't have a preferred near-plane dist from their UpdateLocation
	static float GetDefaultNearPlaneDist() { return CameraManager::s_nearPlaneDistDefault; }

	// Process Overrides

	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound) override;

	virtual ProcessSnapshot* AllocateSnapshot() const override;
	virtual void RefreshSnapshot(ProcessSnapshot* pSnapshot) const override;

	// Queries

	int GetPlayerIndex() const { return m_playerIndex; }
	RegionControl * GetRegionControl() const { return m_pRegionControl; }

	CameraManagerFlags GetFlags() const { return m_flags; }
	void SetFlag(CameraManagerFlags mask) { m_flags |= mask; }
	void ClearFlag(CameraManagerFlags mask) { m_flags &= ~mask; }
	void ToggleFlag(CameraManagerFlags mask) { m_flags ^= mask; }

	const Vector& GetVelocity() const { return m_velocity; }

	bool DidCameraCut() const { return (m_bCameraCutThisFrame || m_bCameraCutLastFrame || m_bCameraCutNextFrame); }
	bool DidCameraCutThisFrame() const { return m_bCameraCutThisFrame; }
	bool DidCameraCutLastFrame() const { return m_bCameraCutLastFrame; }
	bool ShouldShowCameras() const;
	bool MaintainCameraForBlend() const;
	const CameraLocation& GetNoManualLocation() const { return m_noManualLocation; }
	Locator GetNoManualLocator() const { return m_noManualLocation.GetLocator(); }
	F32 GetNoManualFov() const { return m_noManualLocation.GetFov(); }
	Locator GetNoAdjustToWaterLocator() const { return m_noAdjustToWaterLocation.GetLocator(); }
	CamWaterPlane GetWaterPlane() const { return m_waterPlane; }
	void InvalidateCameraWaterQueries();
	void ForceQueryWaterAt(Point_arg queryPos);

	void SetStartLocator(const Locator& startLocator) { m_startLocator = startLocator; }
	const Locator& GetStartLocator() const { return m_startLocator; }

	const Locator& GetDeltaLocator() const { return m_deltaLocator; }
	const Locator& GetDeltaLocatorUnpaused() const { return m_deltaLocatorUnpaused; }

	CameraId GetCurrentCameraId(bool includeTopCamera = false) const;
	const CameraControl* GetCurrentCamera(bool includeTopCamera = false) const
	{
		return const_cast<CameraManager*>(this)->GetCurrentCameraMutable(includeTopCamera);
	}
	CameraControl* GetCurrentCameraMutable(bool includeTopCamera = false) const;

	CameraControl* GetCurrentCameraIncludingOverride() const;
	// find previous camera on the stack.
	CameraControl* GetPreviousCamera(const CameraControl* pCameraControl) const;
	// find next camera on the stack.
	const CameraControl* GetNextCamera(const CameraControl* pCameraControl) const;
	const CameraControl* GetNextCameraNoLock(const CameraControl* pCameraControl) const;

	CameraControl* CameraGetByName(StringId64 nameId);
	const CameraControl* GetTopmostCameraById(CameraId cameraId) const;
	const CameraControl* GetTopmostCameraByKind(const TypeFactory::Record &kind) const;
	bool IsCurrentCameraReachingPitchLimit() const;		// this func is not great, but we have to ship the game.

	typedef bool FindCameraFunc(const CameraControl* pCamera, uintptr_t userData);
	const CameraControl* FindCamera(FindCameraFunc* pFunc, uintptr_t userData) const;

	bool IsInputDisabledWhenCameraFadeOutBlended() const;

	NdGameObject* GetDefaultFocusObject() const;

	const CameraNodeList& GetActiveCameraStack() const { return *m_pCameraNodes; }
	bool CameraStackEmpty() const;
	bool IsAnimated() const;

	// Level Login/Logout

	void AddSceneCameraTable(SceneCameraTable* cameraTable, const Level * pLevel);
	void RemoveSceneCameraTable(SceneCameraTable * table);

	// *** CAMERA REQUEST INTERFACE ***

	CameraControl* RequestCamera(const char* file, U32 line, const char* func,
								 CameraId cameraId, const CameraBlendInfo* pBlendInfo = nullptr,
								 const CameraStartInfo* pStartInfo = nullptr);

	bool RequestPersistentCamera(CameraId cameraId,
								 CameraRequest::AssociationId assoicationId,
								 const CameraBlendInfo* pBlendInfo = nullptr,
								 const CameraStartInfo* pStartInfo = nullptr);

	bool DisablePersistentCamera(StringId64 nameId, 
								 const CameraBlendInfo* pBlendInfo = nullptr, 
								 const AutoGenScriptParams* pAutoGenScriptParams = nullptr,
								 const Vector* pNextCameraInitDir = nullptr,
								 bool disableNextCameraInputUntil70 = false);

	void DisableAllPersistentCameras(const CameraBlendInfo* pBlendInfo = nullptr);
	bool AbandonPersistentCamera(StringId64 nameId);
	void AbandonAllPersistentCameras();
	void SetPersistentCameraDormant(CameraRequest& request, bool dormant, const CameraBlendInfo* pBlendInfo = nullptr);
	void ClearAllPersistentCameras(); // HACK called when spawning the player
	void ClearAllGameCameras(); // kill everything but the top and bottom cameras when spawning the player

	//bool IsPersistentCameraRequested(); // not needed, and has some ramifications for animated cameras, so let's not implement unless it's really needed

	typedef bool FindCameraRequestFunc(const CameraRequest* pRequest, uintptr_t userData);
	CameraRequest* FindPersistentCameraRequest(FindCameraRequestFunc* pFunc,
											   uintptr_t userData,
											   ExcludeAbandoned excludeAbandonedCameras = ExcludeAbandoned::kInclude,
											   IncludeDormant includeDormantCameras = IncludeDormant::kInclude);

	// Camera Management

	void PreProcessUpdate();
	void PrePlayerAnimUpdate();
	void PrepareCameras();
	void DOFUpdate();
	void UpdateCuts();
	void RunCameras(SceneWindow const *pSceneWindow);
	void UpdateFinalCameras();	// this function is the only place g_mainCameraInfo is set!
	CameraResult GetCameraResult() { return m_cameraResult;  }
	const CameraFinalInfo& GetUpdatedCameraInfo() const { return m_workingCamera; }
	void ReevaluateCameraRequests();

	static void DebugDrawCamera(const CameraLocation &cameraLocation, F32 aspectRatio, Color color1 = kColorGreen, Color color2 = kColorRed, DebugPrimTime tt = kPrimDuration1FrameAuto);

	void PostRenderUpdate();
	void ResetActors();
	void ResetGameCameras(bool dontClearPersistentCams = false);

	void TeleportCurrent(const Locator & loc, bool teleportTopManualCamera = true);
	void TeleportManualCamera(const Locator & loc);
	void NotifyCameraCut(const char* file, int line, const char* func);
	void NotifyCameraCutNextFrame(const char* file, int line, const char* func);
	void NotifyCameraCutIn2Frames(const char* file, int line, const char* func);
	void NotifyCameraCutIn3Frames(const char* file, int line, const char* func);
	void SetFOV(float fov);

	void SuppressDisplayOfNextFrame();
	bool IsThisFrameDisplaySuppressed() const;

	// Primary Joypad Assignment (secondary joypad, if available, always controls manual cam)
	enum class JoypadControl
	{
		kGame,
		kManualCamera,
	};
	void SetPrimaryJoypadControl(JoypadControl ctrl);
	JoypadControl GetPrimaryJoypadControl() const { return m_primaryJoypadControl; }
	I32 GetInputPadIndex() const;

	CameraLookAtController* GetLookAtController() { return m_pLookAtController; }

	// water detection
	//void WaterPatchKickQueries();
	//void WaterPatchGatherQueries();

	// Game Camera

	RenderCamera GetGameRenderCamera();

	// Manual Camera

	void ActivateManualCamera();
	void DeactivateManualCamera();
	bool ToggleManualCamera();
	const CameraControl* GetManualCamera() const;
	bool IsManualCameraActive() const { return (GetManualCamera() != nullptr); }

	// Misc
	void AddPostCameraUpdateProcess(MutableProcessHandle hProc);
	void RemovePostCameraUpdateProcess(ProcessHandle hProc);
	bool FindPostCameraUpdateProcess(ProcessHandle hProc);

	// Near Plane Dist.
	float GetNearPlaneDist() const { return m_nearPlaneDist; }

	// Debugging
	void DebugShowCameras() const;
	void DeciPublishCameraLocation(bool forcePublish = false) const;

	void ClearBlendInfoOverride()
	{
		m_blendInfoOverride[kBlendInfoPriHigh] = CameraBlendInfo();
		m_blendInfoOverride[kBlendInfoPriLow] = CameraBlendInfo();
	}

	const ICameraManagerCustom* GetCustom() const { return m_pCustom; }

private:
	void ResetPerFrameOptions();

private:
	ICameraManagerCustom* m_pCustom = nullptr;

	typedef LinkedList<CameraSceneTable>		CameraSceneTableList;
	typedef LinkedList<CameraRequest>			CameraRequestList;

	CameraNodeList*			m_pCameraNodes;
	CameraSceneTableList*	m_pSceneCameras;
	const CameraFinalInfo*	m_pFinalInfo;					//@relo external
	const CameraFinalInfo*	m_pFinalInfoAfterCrossFade;		//@relo external

	CameraFinalInfo			m_workingCamera;				// All the location blending result from RunCamera goes, 
	CameraFinalInfo			m_workingCameraAfterCrossFade;	//   into these two

	CameraManagerFlags m_flags;

	I32 m_playerIndex;
	JoypadControl m_primaryJoypadControl;
	RegionControl * m_pRegionControl;
	Vector m_velocity;
	bool m_bCameraCutIn3Frames;
	bool m_bCameraCutIn2Frames;
	bool m_bCameraCutNextFrame;
	bool m_bCameraCutThisFrame;
	bool m_bCameraCutLastFrame;
	bool m_bSuppressNextFrameDisplay;
	bool m_bSuppressThisFrameDisplay;
	bool m_bStartingFirstCamera;
	bool m_reevaluateCameraRequests;
	CameraBlendInfo m_reevaluateBlendInfo;
	CameraLocation m_noManualLocation;
	CameraLocation m_noAdjustToWaterLocation;
	Locator m_startLocator;
	Locator m_deltaLocator;
	Locator m_deltaLocatorUnpaused;
	Point m_cameraStartPos;
	Point m_cameraStartTarget;

	bool m_currCamReachPitchLimit;

	MutableCameraControlHandle m_hManualCamera;

	NdAtomicLock m_cameraRequestLock;
	mutable NdRwAtomicLock64 m_cameraNodeLock;

	CameraRequest m_currentRequest;
	CameraRequestList* m_pPersistentRequests;
	enum EBlendInfoOverridePriority { kBlendInfoPriHigh, kBlendInfoPriLow, kBlendInfoPriCount };
	CameraBlendInfo m_blendInfoOverride[kBlendInfoPriCount];

	CameraLookAtController* m_pLookAtController;

	//WaterDisplacementPatch m_waterPatch;
	WaterQuery::DispComputeHandle m_hAsyncWaterDetector;

	WaterDetectorDisplacement m_dispWaterDetector;
	WaterDetectorColl2 m_collWaterDetector;
	CamWaterPlane m_waterPlane;

	I64 m_forceQueryWaterFN = -1;
	Point m_forceQueryWaterPos = kOrigin; // m_forceQueryWaterPos: only valid when m_forceQueryWaterFN is currGameFrameUnpaused

	NdAtomicLock m_postCameraUpdateLock;
	static const int kMaxNumPostCameraUpdateProcesses = 4;
	MutableProcessHandle m_postCameraUpdateProcesses[kMaxNumPostCameraUpdateProcesses];

	DebugPosVecPlot m_debugCameraTrajectoryPlot;

	CameraResult m_cameraResult;

	void InitCustom(ICameraManagerCustom* pCustom);
	void ShutdownCustom();

	Joypad& GetJoypad() const;

	// if pInsertAfterCam is set, insert pNewCamera after it.
	void StartCameraInternal(CameraControl* pNewCamera, CameraRank rank, CameraBlendInfo blendInfo, const CameraControl* pInsertAfterCam = nullptr);

	// start auto-generate camera.
	CameraControl* StartAutoGenCamera(const AutoGenCamParams& params,
									  bool isOutro,
									  CameraControl* pCurrCamera,
									  StringId64 baseSettingsId,
									  const CameraBlendInfo* pBlendInfo = nullptr,
									  bool distBlendImmediate = false,
									  bool disableInputUntil70 = false);

	// start regular time-based dist-remap camera.
	CameraControl* StartDistRemapTop(const DistCameraParams& params,
									 CameraControl* pDistCameBase,
									 StringId64 baseSettingsId,
									 const CameraBlendInfo* pBlendInfo = nullptr);

	CameraControl* StartDistRemapBase(CameraRequest* pRequest, CameraControl* pCurrCamera, const CameraBlendInfo* pBlendInfo);
	const CameraControl* SearchBackwardsAndMatch(const CameraControl* pPrevControl, 
												 const CameraRequest* pBestRequest,
												 bool breakIfTypetMatch) const;

	void UpdateBlend(bool* pManualInBlend, bool* pSuppressJoypadDuringBlend);
	void UpdateLocation(bool suppressJoypadDuringBlend);

	typedef CameraNode* const CameraNodeConstPtr;
	CameraLocation BlendLocation(const CameraNode* pNode, 
								 const CameraNodeConstPtr* apNodesTbl, 
								 const I32F nodeCount,
								 const bool currentlyBeforeCrossFade, 
								 const bool returnBeforeCrossFade);
	void KickAsyncWaterDetector();
	CamWaterPlane GatherAsyncWaterDetector();
	CameraLocation AdjustToWaterSurface(CameraManager& self, const CameraLocation &camLoc, const CamWaterPlane& waterPlane);
	CameraLocation AdjustToWind(CameraManager& self, const CameraLocation &camLoc);
	bool HasCrossFade(CameraNode* nodeTable[], I32F nodeCount);
	float GetCrossFadeBlend(CameraNode* nodeTable[], I32F nodeCount);

	CameraRequest* GetBestPersistentRequest(ExcludeAbandoned excludeAbandonedCameras = ExcludeAbandoned::kInclude, const CameraBlendInfo* pBlendInfo = nullptr);
	bool IsRequestValid(const CameraRequest& persistentRequest, IncludeDormant includeDormantCameras = IncludeDormant::kExclude);
	CameraControl* EvaluateCameraRequests(const CameraBlendInfo* pBlendInfo = nullptr);
	void ClearReevaluate();

	// TryPushCameraPair: try push blended by dist-to-obj camera pair 
	CameraControl* TryPushStrafeCameraPair(CameraControl* pCurCamera, CameraRequest* pBestRequest);

	void ReapCameras();
	void ReapPersistentCameraRequests(ReapAbandoned reapAbandoned = ReapAbandoned::kDontReap);
	void ReapPersistentCameraRequestsForAssociationId(CameraRequest::AssociationId associationId);

	CameraRequestList::Iterator FreePersistentRequest(CameraRequest* pRequest);
	void FreePersistentRequestUnused(CameraRequest* pRequest);

	// Near Plane Distance
	float			m_nearPlaneDist						= 0.24f;
	static float	s_nearPlaneDistDefault;									// This value gets used if CameraControl::UpdateLocation return with an invalid (<= 0) near-plane distance

	struct NearPlaneDistOverride
	{
		struct Slot
		{
			float		m_value = -1.0f;
			U32F		m_priority = 0;
			I64			m_frameNumberUnpaused = -1;
		};
		Slot m_slots[2];

		void SetOverride(float nearPlaneDist, U32F priority, I64 frameNumber);
		float Get() const;
	};
	NearPlaneDistOverride m_nearPlaneDistOverride;

	void SetNearPlaneOverride(float nearPlaneDist, U32F priority);

	friend class CameraManager::Active;
};

// -------------------------------------------------------------------------------------------------
// Macros and Free Functions
// -------------------------------------------------------------------------------------------------

Locator GameCameraLocator();
CameraLocation GameCameraInfo();
Locator GameGlobalCameraLocator(int playerIndex);
Locator GameGlobalCameraUnpausedDelta();
I64 GameCameraFrameNumber(); // GameCameraLocator is from which frame.
inline Vector GameCameraDirection() { return GetLocalZ(GameCameraLocator().GetRotation()); }
Vector GameCameraDirectionXz();
inline Vector GameGlobalCameraDirection(int playerIndex) { return GetLocalZ(GameGlobalCameraLocator(playerIndex).GetRotation()); }

Point GetViewCenterFromCamLoc(int playerIndex, const Locator& camLoc, float scaleAlongViewDir = 1.0f);
Point GetViewCenterFromCamLoc(int playerIndex, Point_arg basePos, Vector_arg viewDir, float scaleAlongViewDir = 1.0f);

// -------------------------------------------------------------------------------------------------
// Globals
// -------------------------------------------------------------------------------------------------

extern CameraOptions g_cameraOptions;
extern bool g_cameraApplyWantLoad;
extern float g_floatCameraBlend;

// -------------------------------------------------------------------------------------------------
// Snapshot
// -------------------------------------------------------------------------------------------------

class CameraManagerSnapshot : public ProcessSnapshot
{
public:
	PROCESS_SNAPSHOT_DECLARE(CameraManagerSnapshot, ProcessSnapshot);

	explicit CameraManagerSnapshot(const Process* pOwner, const StringId64 typeId)
		: ParentClass(pOwner, typeId)
	{
	}

	const CameraLocation& GetNoManualInfo() const { return m_noManualInfo; }
	const Locator& GetNoManualLocator() const { return m_noManualInfo.GetLocator(); }
	const Locator& GetNoManualDeltaUnpaused() const { return m_noManualDeltaUnpaused; }
	const I64 GetFrameNumber() const { return m_gameFrameNumber; }

	CameraLocation m_noManualInfo;
	Locator		m_noManualDeltaUnpaused;
	I64			m_gameFrameNumber = -1;
};

#endif // CAMERAMANAGER_H
