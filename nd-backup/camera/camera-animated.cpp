/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/camera/camera-animated.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/anim-simple-instance.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/camera/camera-interface.h"
#include "ndlib/lights/dynamic-light-game-interface.h"
#include "ndlib/lights/runtime-lights.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/render/dev/render-options.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/settings/priority.h"
#include "ndlib/settings/render-settings.h"
#include "ndlib/settings/settings.h"

#include "gamelib/camera/camera-animation-handle.h"
#include "gamelib/camera/camera-control.h"
#include "gamelib/camera/camera-manager.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/ndphys/rigid-body.h"
#include "gamelib/render/light/light-update.h"

#include <float.h>

AnimatedCameraOptions g_animatedCameraOptions;

// To allow updating of IGC lights during development.
const I32 kMaxUpdatedIGCLights = 32;

#if !FINAL_BUILD
	extern bool g_debugAnimatedCamera;
#else
	static const bool g_debugAnimatedCamera = false;
#endif

struct AnimatedLightUpdateInfo
{
	I32 m_index;
	LightUpdateData m_info;
};

static I32 s_numUpdatedLightInfo = 0;
static AnimatedLightUpdateInfo s_animatedLightUpdateInfo[kMaxUpdatedIGCLights];

static void UpdateCameraAnimatedLights(AnimControl& animControl, const BoundFrame& frame, int cameraIndex);

FINAL_CONST I64 g_debugOnlyCameraPriority = kCameraAnimationPriorityUnset;
bool g_debugDrawStateScriptCamera = false;
bool g_debugDrawStateScriptCameraLs = false;

FROM_PROCESS_DEFINE(CameraControlAnimated);
PROCESS_REGISTER(CameraControlAnimated, CameraControl);
CAMERA_REGISTER_RANKED_TYPED(kCameraAnimated, CameraControlAnimated, CameraAnimatedStartInfo, CameraPriority::Class::kDesigner, CameraRank::Normal, CameraConfigAnimated);

PROCESS_REGISTER(CameraControlAnimatedOverride, CameraControlAnimated);
CAMERA_REGISTER_RANKED_TYPED(kCameraAnimatedOverride, CameraControlAnimatedOverride, CameraAnimatedStartInfo, CameraPriority(CameraPriority::Class::kGameplay, CameraPriority::LevelMax), CameraRank::Override, CameraConfigAnimated);

const F32 CameraAnimation::kInvalidCutChannelValue = NDI_FLT_MAX;

static const char* Spaces(I32F num)
{
	const I32F kMaxSpaces = 31;
	static char s_spaces[kMaxSpaces + 1];
	if (num > kMaxSpaces)
		num = kMaxSpaces;
	I32F i;
	for (i = 0; i < num; ++i)
		s_spaces[i] = ' ';
	s_spaces[i] = '\0';
	return s_spaces;
}

static bool FindAnimatedCamera(const CameraControl* pCamera, uintptr_t userData)
{
	if (pCamera->GetCameraId().ToConfig().IsAnimated())
	{
		const StringId64 ownerId = static_cast<StringId64>(userData);
		const CameraControlAnimated* pAnimCam = PunPtr<const CameraControlAnimated*>(pCamera);
		if (pAnimCam->m_ownerId == ownerId)
		{
			return true;
		}
	}
	return false;
}

static bool FindAnimatedCameraRequest(const CameraRequest* pRequest, uintptr_t userData)
{
	if (pRequest->m_cameraId.ToConfig().IsAnimated())
	{
		const StringId64 ownerId = static_cast<StringId64>(userData);

		const CameraAnimatedStartInfo* pStartInfo = PunPtr<const CameraAnimatedStartInfo*>(&pRequest->m_startInfo);

		if (pStartInfo->m_ownerId == ownerId)
		{
			return true;
		}
	}
	return false;
}

void AddPersistentCameraAnimationSource(int playerIndex, const CameraAnimationHandle& hCameraAnimSource, int cameraIndex)
{
	CameraRequestExtensionAnimated* pExt = nullptr;
	if (!g_animatedCameraOptions.m_useOldExtension)
	{
		const CameraControl* pCamera = CameraManager::GetGlobal(playerIndex).FindCamera(FindAnimatedCamera, (uintptr_t)ToBlindPtr(hCameraAnimSource.GetOwnerId()));
		if (pCamera)
		{
			const CameraControlAnimated* pAnimCam = PunPtr<const CameraControlAnimated*>(pCamera);
			pExt = pAnimCam->m_pExtension;
			if (cameraIndex == -1)
			{
				cameraIndex = pAnimCam->m_cameraIndex;
			}
		}
		else
		{
			if (cameraIndex == -1)
			{
				cameraIndex = 0;
			}
		}
	}
	else
	{
		if (cameraIndex == -1)
		{
			cameraIndex = 0;
		}
		CameraRequest* pRequest = CameraManager::GetGlobal(playerIndex).FindPersistentCameraRequest(FindAnimatedCameraRequest, (uintptr_t)ToBlindPtr(hCameraAnimSource.GetOwnerId()));
		if (pRequest)
		{
			pExt = PunPtr<CameraRequestExtensionAnimated*>(pRequest->m_pExtension);
		}
	}

	if (pExt)
	{
		pExt->AddCameraAnimSource(playerIndex, hCameraAnimSource, cameraIndex);
	}

	if (playerIndex == 0 && !EngineComponents::GetNdGameInfo()->m_isHeadless)
	{
		if (const NdGameObject* pObj = hCameraAnimSource.GetInstanceHandle().GetGameObject())
		{
			UpdateCameraAnimatedLights(*pObj->GetAnimControl(), pObj->GetBoundFrame(), cameraIndex);
		}
	}
}

// -----------------------------------------------------------------------------------------------------------
// CameraAnimatedLookTracker
// -----------------------------------------------------------------------------------------------------------
void CameraAnimatedLookTracker::Update(Point lookAtPoint, const Locator &origLocator, bool wasCut, Locator &modifiedLocator)
{
	Vector up = GetLocalY(origLocator);
	Vector right = GetLocalX(origLocator);

	Vector toLookAtPoint = SafeNormalize(lookAtPoint - origLocator.GetTranslation(), GetLocalZ(origLocator));
	Vector direction = GetLocalZ(origLocator);

	//g_prim.Draw(DebugSphere(lookAtPoint, 0.1f, kColorGreen));
	//g_prim.Draw(DebugArrow(origLocator.GetTranslation(), direction, kColorRed));
	//g_prim.Draw(DebugArrow(origLocator.GetTranslation(), toLookAtPoint, kColorGreen));

	if (m_firstFrame || wasCut)
	{
		m_lastDirection = direction;
		m_angleTracker.Reset();
	}

	float angle = Acos(MinMax((float)Dot(toLookAtPoint, m_lastDirection), -1.0f, 1.0f));
	angle = Max(Abs(angle) - DEGREES_TO_RADIANS(0.5f), 0.0f) * Sign(angle);

	const float springConstant = 20.0f;
	const float newAngle = m_angleTracker.Track(angle, 0.0f, EngineComponents::GetNdFrameState()->GetClock(kGameClock)->GetDeltaTimeInSeconds(), springConstant);

	if (angle > 0.0001f)
	{
		float tt = Abs(newAngle / angle);

		direction = (Sin(newAngle * (1.0f - tt)) / Sin(newAngle)) * m_lastDirection + (Sin(newAngle * tt) / Sin(newAngle)) * toLookAtPoint;
	}
	else
	{
		direction = toLookAtPoint;
	}

	
	//g_prim.Draw(DebugArrow(origLocator.GetTranslation(), direction, kColorMagenta));

	m_lastDirection = direction;
	Quat desiredRotation = QuatFromLookAt(direction, up);

	Locator desiredLocator(origLocator);
	desiredLocator.SetRot(desiredRotation);

	modifiedLocator = desiredLocator;
	m_firstFrame = false;
}

// -----------------------------------------------------------------------------------------------------------
// CameraAnimBlender
// -----------------------------------------------------------------------------------------------------------

template<class BLEND_DATA>
class CameraAnimBlenderBase : public AnimStateLayer::InstanceBlender<BLEND_DATA>
{
protected:
	int									m_cameraIndex;
	const NdGameObject*					m_pGameObject;
	const AnimStateLayer*				m_pLayer;
	const CameraAnimationHandleList&	m_sourceList;

public:
	CameraAnimBlenderBase(int cameraIndex, const NdGameObject* pObj, const AnimStateLayer* pLayer, const CameraAnimationHandleList& sourceList)
		: m_cameraIndex(cameraIndex)
		, m_pGameObject(pObj)
		, m_pLayer(pLayer)
		, m_sourceList(sourceList)
	{}

	const CameraAnimationHandle* GetAnimSourceForInstance(const AnimStateInstance* pInstance)
	{
		AnimInstanceHandle hAnim(m_pGameObject, m_pLayer, pInstance);
		// brute force, change if this is slow
		for (int i = 0; i < m_sourceList.Size(); ++i)
		{
			if (hAnim == m_sourceList.GetSource(i).GetInstanceHandle())
			{
				return &m_sourceList.GetSource(i);
			}
		}
		return nullptr;
	}
};

// -----------------------------------------------------------------------------------------------------------

struct CameraAnimBlendStatus
{
	CameraAnimBlendStatus() : m_valid(false), m_blendedWithInvalid(false) { }

	bool m_valid;
	bool m_blendedWithInvalid;
};

typedef pair<CameraAnimation, CameraAnimBlendStatus> CameraAnimBlendData;

class CameraAnimBlender : public CameraAnimBlenderBase<CameraAnimBlendData>
{
private:
	//const float* m_pDeltaTimeInSecs = nullptr;

public:
	CameraAnimBlender(int cameraIndex, const NdGameObject* pObj, const AnimStateLayer* pLayer, const CameraAnimationHandleList& sourceList)
		: CameraAnimBlenderBase(cameraIndex, pObj, pLayer, sourceList)
	{}

	virtual CameraAnimBlendData GetDefaultData() const override
	{
		return CameraAnimBlendData();
	}

	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, CameraAnimBlendData* pDataOut) override
	{
		pDataOut->second.m_valid = false;
		// Find the animation source that matches this instance
		if (const CameraAnimationHandle* phSrc = GetAnimSourceForInstance(pInstance))
		{
			CameraAnimation cameraAnim = phSrc->GetCameraAnimation(m_cameraIndex);
			if (cameraAnim.m_isValid)
			{
#ifndef FINAL_BUILD
				const NdGameObject* pObj = phSrc->GetInstanceHandle().GetGameObject();
				cameraAnim.DebugDraw(phSrc->GetPriority(), pObj ? DevKitOnly_StringIdToStringOrHex(pObj->GetUserId()) : "??");
#endif
				pDataOut->first = cameraAnim;
				pDataOut->second.m_valid = true;
				return true;
			}
		}
		return false;
	}

	virtual CameraAnimBlendData BlendData(const CameraAnimBlendData& leftData, const CameraAnimBlendData& rightData, float masterFade, float animFade, float motionFade) override
	{
		CameraAnimBlendData result;
		if (rightData.second.m_valid)
		{
			result = rightData;
			if (leftData.second.m_valid)
			{
				CameraAnimation::Blend(result.first, leftData.first, rightData.first, motionFade);
			}
		}
		else if (leftData.second.m_valid)
		{
			result = leftData;
		}

		return result;
	}
};

// -----------------------------------------------------------------------------------------------------------

typedef pair<bool, CameraAnimBlendStatus> CameraIsDormantBlendData;

class CameraIsDormantBlender : public CameraAnimBlenderBase<CameraIsDormantBlendData>
{
public:
	CameraIsDormantBlender(int cameraIndex, const NdGameObject* pObj, const AnimStateLayer* pLayer, const CameraAnimationHandleList& sourceList)
		: CameraAnimBlenderBase(cameraIndex, pObj, pLayer, sourceList)
	{ }

	virtual CameraIsDormantBlendData GetDefaultData() const override
	{
		return CameraIsDormantBlendData();
	}

	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, CameraIsDormantBlendData* pDataOut) override
	{
		pDataOut->second.m_valid = false;
		// Find the animation source that matches this instance
		if (const CameraAnimationHandle* phSrc = GetAnimSourceForInstance(pInstance))
		{
			pDataOut->first = phSrc->IsDormant(m_cameraIndex, &pDataOut->second.m_valid);

			return pDataOut->second.m_valid;
		}
		return false;
	}

	virtual CameraIsDormantBlendData BlendData(const CameraIsDormantBlendData& leftData, const CameraIsDormantBlendData& rightData, float masterFade, float animFade, float motionFade) override
	{
		CameraIsDormantBlendData result;
		if (rightData.second.m_valid)
		{
			result = rightData;
			if (leftData.second.m_valid)
			{
				// pick the left or the right if motionFade is at its extremes; otherwise we're dormant only if both anims say we're dormant
				result.first = ((motionFade == 0.0f) ? leftData.first : ((motionFade == 1.0f) ? rightData.first : (leftData.first && rightData.first)));
			}
		}
		else if (leftData.second.m_valid)
		{
			result = leftData;
		}

		return result;
	}
};

// -----------------------------------------------------------------------------------------------------------
// CameraRequestExtensionAnimated
// -----------------------------------------------------------------------------------------------------------

void CameraRequestExtensionAnimated::Init()
{
	// set up the request's extension -- called every time a request is created
	m_sourceList.Reset();
	m_refCount = 0;
	m_cameraIndex = -1;
	m_hSourceGo = nullptr;
}

void CameraRequestExtensionAnimated::Destroy()
{
	// tear down the request's extension -- called every time a request is retired
	ASSERT(m_refCount == 0);
}

CameraRequestExtensionAnimated::~CameraRequestExtensionAnimated()
{
	// never do anything in the destructor -- it is never called
}

void CameraRequestExtensionAnimated::IncrementReferenceCount()
{
	++m_refCount;
}

int CameraRequestExtensionAnimated::DecrementReferenceCount()
{
	if (m_refCount > 0)
	{
		--m_refCount;
		if (m_refCount == 0)
		{
			CameraConfig& config = kCameraAnimated;
			config.DestroyCameraRequestExtension(this);
		}
	}
	else
	{
		ASSERT(m_refCount > 0);
	}

	return m_refCount;
}

void CameraRequestExtensionAnimated::UpdateCameraRequest(CameraManager& mgr, CameraRequest& request)
{
	const CameraAnimatedStartInfo& startInfo = *PunPtr<const CameraAnimatedStartInfo*>(&request.m_startInfo);
	const bool dormant = IsDormant(startInfo.m_cameraIndex);

	// FOR NOW we always use the same blend-in or blend-out, every time the camera becomes dormant or wakes from dormant.
	// We could imagine an array of blend infos, one for each transition during a camera animation, provided by script.
	// Or we could imagine some mechanism by which the animators could specify each transition's blend time via a float
	// channel in the anim or something. We'll burn that bridge when we get to it...
	const CameraBlendInfo& blendInfo = (dormant) ? startInfo.m_blendOutInfo : request.m_blendInfo;

	mgr.SetPersistentCameraDormant(request, dormant, &blendInfo);
}

bool CameraRequestExtensionAnimated::HasSources()
{
	return m_sourceList.Size() > 0;
}

void CameraRequestExtensionAnimated::AddCameraAnimSource(int playerIndex, const CameraAnimationHandle& hCameraAnimSource, int cameraIndex)
{
	if (m_cameraIndex != cameraIndex)
		m_sourceList.Reset();

	m_cameraIndex = cameraIndex;

	if (!m_sourceList.UpdateIfContains(hCameraAnimSource))
	{
		m_sourceList.AddSource(hCameraAnimSource);

		if (g_debugAnimatedCamera && !EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
			MsgCamera("%05u: AddCameraAnimSource\n", (U32)EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused);

		if (m_sourceList.Size() == 1)
		{
			// Only do this for the *first* camera source, thus guaranteeing that we'll only ever evaluate the animation of
			// the game object whole update we're *currently* running. If we don't do this, we may end up trying to evaluate
			// the animation of some other random game object that may be in the process of updating (in another job/fiber).
			// This prevents rare crashes, and still accomplishes what we want: To know RIGHT NOW whether or not the camera
			// animation is dormant. Yes, this is a hack, but it works.
			CameraManager::GetGlobal(playerIndex).ReevaluateCameraRequests();
		}
	}
}

bool CameraRequestExtensionAnimated::ExtractAnimation(CameraAnimation& outputAnim, 
													  const CameraAnimationHandleList& sourceList, 
													  int cameraIndex, 
													  NdGameObjectHandle* phSourceGo)
{
	bool isAnimValid = false;

	const NdGameObject* pSourceGo = (phSourceGo) ? phSourceGo->ToProcess() : nullptr;

	// Apply animation from one of the sources
	{
		if (g_animatedCameraOptions.m_showSources)
		{
			MsgConPauseable("Animated Camera sources:\n");
			for (int iSource = 0; iSource < sourceList.Size(); ++iSource)
			{
				const CameraAnimationHandle& hCurAnimSource = sourceList.GetSource(iSource);
				hCurAnimSource.DebugPrint();
			}
		}

		if (g_debugAnimatedCamera && !EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
			MsgCamera("%05u: CameraRequestExtensionAnimated::ExtractAnimation (%d sources)\n", (U32)EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused, sourceList.Size());

		// Get the top priority source and get blended parameters from it.
		if (sourceList.Size() > 0)
		{
			const NdGameObject* pPrevObj = nullptr;
			for (int i = sourceList.Size() - 1; i >= 0; --i)
			{
				const CameraAnimationHandle& hCurAnimSource = sourceList.GetSource(i);

				if (g_debugOnlyCameraPriority != kCameraAnimationPriorityUnset
				&&  hCurAnimSource.GetPriority() != g_debugOnlyCameraPriority)
					continue;

				const NdGameObject* pCurObj = hCurAnimSource.GetInstanceHandle().GetGameObject();
				if (pCurObj != pPrevObj)
				{
					const AnimLayer* pLayer = pCurObj->GetAnimControl()->GetLayerById(hCurAnimSource.GetInstanceHandle().LayerId());
					ASSERT(pLayer);
					if (pLayer->GetType() == kAnimLayerTypeSimple)
					{
						CameraAnimation cameraAnim = hCurAnimSource.GetCameraAnimation(cameraIndex);
						if (cameraAnim.m_isValid
						&&  (pSourceGo == nullptr || pCurObj == pSourceGo))
						{
#ifndef FINAL_BUILD
							cameraAnim.DebugDraw(hCurAnimSource.GetPriority(), pCurObj ? DevKitOnly_StringIdToStringOrHex(pCurObj->GetUserId()) : "???");
#endif
							if (phSourceGo)
								*phSourceGo = NdGameObjectHandle(pCurObj);
							outputAnim = cameraAnim;
							isAnimValid = true;
							break;
						}
					}
					else if (pLayer->GetType() == kAnimLayerTypeState)
					{
						const AnimStateLayer* pStateLayer = static_cast<const AnimStateLayer*>(pLayer);
						CameraAnimBlender blender(cameraIndex, pCurObj, pStateLayer, sourceList);
						CameraAnimBlendData blendedData = blender.BlendForward(pStateLayer, CameraAnimBlendData());
						if (blendedData.second.m_valid
						&&  (pSourceGo == nullptr || pCurObj == pSourceGo))
						{
							if (phSourceGo)
								*phSourceGo = NdGameObjectHandle(pCurObj);
							outputAnim = blendedData.first;
							isAnimValid = true;
							break;
						}
					}
				}
				pPrevObj = pCurObj;
			}
		}
	}

	return isAnimValid;
}

bool CameraRequestExtensionAnimated::ExtractAnimation(CameraAnimation& outputAnim, int cameraIndex, bool noFailOver)
{
	const bool wantFailOver = g_animatedCameraOptions.m_forceFailOver || (!noFailOver && !g_animatedCameraOptions.m_disallowFailOver);
	return ExtractAnimation(outputAnim, m_sourceList, cameraIndex, (wantFailOver) ? nullptr : &m_hSourceGo);
}

bool CameraRequestExtensionAnimated::ExtractAnimationConst(CameraAnimation& outputAnim, int cameraIndex) const
{
	return ExtractAnimation(outputAnim, m_sourceList, cameraIndex, nullptr);
}

AnimInstanceHandle CameraRequestExtensionAnimated::GetAnimSourceInstance(const CameraAnimationHandleList& sourceList, int cameraIndex)
{
	const NdGameObject* pSourceGo = nullptr;

	// Apply animation from one of the sources
	{
		// Get the top priority source and get blended parameters from it.
		if (sourceList.Size() > 0)
		{
			const NdGameObject* pPrevObj = nullptr;
			for (int i = sourceList.Size() - 1; i >= 0; --i)
			{
				const CameraAnimationHandle& hCurAnimSource = sourceList.GetSource(i);

				if (g_debugOnlyCameraPriority != kCameraAnimationPriorityUnset
					&& hCurAnimSource.GetPriority() != g_debugOnlyCameraPriority)
					continue;

				const NdGameObject* pCurObj = hCurAnimSource.GetInstanceHandle().GetGameObject();
				if (pCurObj != nullptr)
				{
					if (pCurObj != pPrevObj)
					{
						return hCurAnimSource.GetInstanceHandle();
					}
				}
				pPrevObj = pCurObj;
			}
		}
	}

	return AnimInstanceHandle();
}

AnimInstanceHandle CameraRequestExtensionAnimated::GetAnimSourceInstance(int cameraIndex) const
{
	return GetAnimSourceInstance(m_sourceList, cameraIndex);
}

bool CameraRequestExtensionAnimated::IsDormant(int cameraIndex)
{
	bool isDormant = true;

	// Update the animation source list
	UpdateSourceList();

	// Get the top priority source and get blended parameters from it.
	if (m_sourceList.Size() > 0)
	{
		const NdGameObject* pPrevObj = nullptr;
		for (int i = m_sourceList.Size() - 1; i >= 0; --i)
		{
			const CameraAnimationHandle& hCurAnimSource = m_sourceList.GetSource(i);

			if (g_debugOnlyCameraPriority != kCameraAnimationPriorityUnset
			&&  hCurAnimSource.GetPriority() != g_debugOnlyCameraPriority)
				continue;

			const NdGameObject* pCurObj = hCurAnimSource.GetInstanceHandle().GetGameObject();
			if (pCurObj != nullptr)
			{
				if (const AnimControl* pAnimControl = pCurObj->GetAnimControl())
				{
					if (const AnimLayer* pLayer = pAnimControl->GetLayerById(hCurAnimSource.GetInstanceHandle().LayerId()))
					{
						if (pCurObj != pPrevObj)
						{
							ASSERT(pLayer);
							if (pLayer->GetType() == kAnimLayerTypeSimple)
							{
								bool valid = false;
								bool dormant = hCurAnimSource.IsDormant(cameraIndex, &valid);
								if (valid)
								{
									isDormant = dormant;
									break;
								}
							}
							else if (pLayer->GetType() == kAnimLayerTypeState)
							{
								const AnimStateLayer* pStateLayer = static_cast<const AnimStateLayer*>(pLayer);
								CameraIsDormantBlender blender(cameraIndex, pCurObj, pStateLayer, m_sourceList);
								CameraIsDormantBlendData blendedData = blender.BlendForward(pStateLayer, CameraIsDormantBlendData());
								if (blendedData.second.m_valid)
								{
									isDormant = blendedData.first;
									break;
								}
							}
						}
					}
				}
			}
			pPrevObj = pCurObj;
		}
	}
	else
	{
		// if I have no animation sources, act as though I'm not dormant
		isDormant = false;
	}

	if (g_debugAnimatedCamera && !EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
		MsgCamera("%05u: IsDormant == %s (%d sources)\n", (U32)EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused, isDormant ? "true" : "false", m_sourceList.Size());

	return isDormant;
}

// -----------------------------------------------------------------------------------------------------------
void CameraRequestExtensionAnimated::UpdateSourceList()
{
	// Update the animation source list
	m_sourceList.RemoveInvalidSources();
	m_sourceList.Sort();
}

// -----------------------------------------------------------------------------------------------------------
// CameraConfigAnimated
// -----------------------------------------------------------------------------------------------------------

CameraRequestExtensionAnimated				CameraConfigAnimated::s_pool[CameraConfigAnimated::kCapacity];
BitArray<CameraConfigAnimated::kCapacity>	CameraConfigAnimated::s_poolAllocatedBits(false);
size_t										CameraConfigAnimated::s_poolHighWater = 0;

CameraConfigAnimated::~CameraConfigAnimated()
{
}

ICameraRequestExtension* CameraConfigAnimated::CreateCameraRequestExtension()
{
	CameraRequestExtensionAnimated* pExt = nullptr;

	U64 iFree = s_poolAllocatedBits.FindFirstClearBit();
	ALWAYS_ASSERTF(iFree < kCapacity, ("CameraConfigAnimated: out of camera request extensions -- increase kCapacity"));
	if (iFree < kCapacity)
	{
		s_poolAllocatedBits.SetBit(iFree);
		pExt = &s_pool[iFree];
		pExt->Init();

		size_t numBits = s_poolAllocatedBits.CountSetBits();
		if (s_poolHighWater < numBits)
			s_poolHighWater = numBits;
	}

	return pExt;
}

void CameraConfigAnimated::DestroyCameraRequestExtension(ICameraRequestExtension* pExtRaw)
{
	if (pExtRaw)
	{
		CameraRequestExtensionAnimated* pExt = PunPtr<CameraRequestExtensionAnimated*>(pExtRaw);
		ALWAYS_ASSERT(pExt >= &s_pool[0] && pExt < &s_pool[kCapacity]);
		pExt->Destroy();
		U64 iExt = pExt - &s_pool[0];
		s_poolAllocatedBits.ClearBit(iExt);
	}
}

// -----------------------------------------------------------------------------------------------------------
// CameraControlAnimated
// -----------------------------------------------------------------------------------------------------------

inline float GetFov(const CameraAnimatedStartInfo& startInfo)
{
	return (startInfo.m_fov != CameraStartInfo::kUseDefaultFov) ? startInfo.m_fov : 75.0f;
}

CameraBlendInfo CameraControlAnimated::CameraStart(const CameraStartInfo& baseStartInfo)
{
	const CameraAnimatedStartInfo& startInfo = *PunPtr<const CameraAnimatedStartInfo*>(&baseStartInfo);

	m_pExtension = PunPtr<CameraRequestExtensionAnimated*>(startInfo.m_pExtension);
	if (m_pExtension)
		m_pExtension->IncrementReferenceCount();

	const RigidBody* pBindTarget = nullptr;
	m_hFocusObj = startInfo.GetFocusObject();
	m_flags.m_needsFocus = m_hFocusObj.HandleValid();
	m_flags.m_noDefaultDofBehavior = true;
	m_scaleLocator = startInfo.m_scaleLocator;
	// Can't use GameCameraLocator here because the override cameras are not included
	m_cameraIndex = startInfo.m_cameraIndex;
	m_ownerId = startInfo.m_ownerId;
	m_prevCutChannel = CameraAnimation::kInvalidCutChannelValue;
	m_focusCentered = startInfo.m_focusCentered;
	m_enableLookAround = startInfo.m_enableLookAround;
	m_lookAtAttachPoint = startInfo.m_lookAtAttachPoint;
	m_noFailOver = startInfo.m_noFailOver;
	//m_enableAlwaysMove = startInfo.m_enableAlwaysMove; // NO LONGER SUPPORTED
	m_offset = startInfo.m_offset;
	m_usedByAnimationDirector = startInfo.m_usedByAnimationDirector;
	m_animId = startInfo.m_animId;
	m_focusAttachPoint = startInfo.m_focusAttachPoint;

	m_cameraSpeed = 0.f;
	m_startFov = 0.0f;

	const CameraControl* pPrevCamera = CameraManager::GetGlobal(m_playerIndex).GetCurrentCamera();
	if (pPrevCamera && startInfo.m_useAnalogZoom)
	{
		m_startFov = pPrevCamera->GetCameraLocation().GetFov();
		m_analogFovTt = 0.0f;
	}

	if (startInfo.m_hAnimSource.IsValid()
	&&  startInfo.m_hAnimSource.GetOwnerId() == m_ownerId)
	{
		m_hAnimSource = startInfo.m_hAnimSource;
	}

	m_finalAnimation.Init( BoundFrame(g_mainCameraInfo[GetPlayerIndex()].GetLocator(), Binding(pBindTarget)),
	                       GetFov(startInfo) );

	CameraControl::CameraStart(baseStartInfo);

	// Reset our DECI lights
	if (!g_renderOptions.m_bPersistAnimationLightToolUpdates)
	{
		s_numUpdatedLightInfo = 0;
	}

	return Seconds(0.3f);
}

Err CameraControlAnimated::Init(const ProcessSpawnInfo& processSpawnInfo)
{
	const CameraControlSpawnInfo& spawnInfo = *PunPtr<const CameraControlSpawnInfo*>(&processSpawnInfo);

	m_cameraControl.SetPlayerIndex(spawnInfo.m_playerIndex);
	return CameraControl::Init(spawnInfo);
}

void CameraControlAnimated::OnKillProcess()
{
	s_numUpdatedLightInfo = 0;

	if (m_pExtension)
		m_pExtension->DecrementReferenceCount();
	m_pExtension = nullptr;

	ParentClass::OnKillProcess();
}

bool CameraControlAnimated::IsEquivalentTo(const CameraStartInfo& baseStartInfo) const
{
	if (!CameraControl::IsEquivalentTo(baseStartInfo))
		return false;

	const CameraAnimatedStartInfo& startInfo = *PunPtr<const CameraAnimatedStartInfo*>(&baseStartInfo);

	if (m_cameraIndex != startInfo.m_cameraIndex
	||  (m_ownerId != INVALID_STRING_ID_64 && m_ownerId != startInfo.m_ownerId))
		return false;

	if (startInfo.m_enableLookAround != m_enableLookAround)
		return false;
	if (startInfo.m_noFailOver != m_noFailOver)
		return false;

	if (startInfo.m_hAnimSource.IsValid() && m_hAnimSource.IsValid())
	{
		if (m_hAnimSource != startInfo.m_hAnimSource)
			return false;
	}

	if (m_usedByAnimationDirector && !startInfo.m_usedByAnimationDirector)
	{
		return false;
	}

	return true; // passed all tests
}

void CameraControlAnimated::UpdateCuts (const CameraInstanceInfo& info)
{
	CameraAnimation finalAnimation = m_finalAnimation;
	bool isAnimValid = false;

	if (m_hAnimSource.IsValid())
	{
		// maintain this list so that blending between anims blends camera as well
		m_sourceHandleList.RemoveInvalidSources();
		if (!m_sourceHandleList.Contains(m_hAnimSource))
			m_sourceHandleList.AddSource(m_hAnimSource);

		// Update the animation source list
		m_sourceHandleList.RemoveInvalidSources();
		m_sourceHandleList.Sort();

		CameraRequestExtensionAnimated::ExtractAnimation(finalAnimation, m_sourceHandleList, m_cameraIndex);

		isAnimValid = true;
	}
	else
	{
		CameraRequestExtensionAnimated* pExt;
		if (!g_animatedCameraOptions.m_useOldExtension)
		{
			pExt = m_pExtension;
		}
		else
		{
			CameraRequest* pRequest = CameraManager::GetGlobal(m_playerIndex).FindPersistentCameraRequest(FindAnimatedCameraRequest, (uintptr_t)ToBlindPtr(m_ownerId));
			pExt = (pRequest) ? PunPtr<CameraRequestExtensionAnimated*>(pRequest->m_pExtension) : nullptr;
		}

		if (pExt)
		{
			pExt->UpdateSourceList();
			isAnimValid = pExt->ExtractAnimation(finalAnimation, m_cameraIndex, m_noFailOver);
		}
	}

	SettingSetPers(SID("animated-camera"), &EngineComponents::GetNdFrameState()->GetClock(kGameClock)->m_relativeSpeed, finalAnimation.m_slowmoChannel, kCameraPriority, 1.0f);
	SettingSetPers(SID("animated-camera"), &EngineComponents::GetNdFrameState()->GetClock(kGameClock)->m_relativeSpeed, finalAnimation.m_slowmoChannel, kCameraPriority, 1.0f);

	// force a cut if the camera's scale.z channel changes, no matter what
	if (!g_animOptions.m_disableCameraCuts && finalAnimation.m_cutChannel != CameraAnimation::kInvalidCutChannelValue && m_prevCutChannel != CameraAnimation::kInvalidCutChannelValue && Abs(m_prevCutChannel - finalAnimation.m_cutChannel) > 0.5f)
	{
		OMIT_FROM_FINAL_BUILD(if (g_animOptions.m_debugCameraCutsSuperVerbose) MsgCinematic("%05u: CUT generated by CameraAnimated\n", (U32)EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused);)
		finalAnimation.m_cut = true;
	}

	if (isAnimValid)
	{
		ApplyCameraAnimation(finalAnimation); // updates m_finalAnimation

		if (m_usedByAnimationDirector)
		{
			m_lastAnimId = finalAnimation.m_phaseAnimName;
			if (GetProcessDeltaTime() > 0.0f)
			{
				m_lastFps = (finalAnimation.m_frameIndex - m_lastFrameIndex) / GetProcessDeltaTime();
			}

			m_prevFrameIndex = m_lastFrameIndex = finalAnimation.m_frameIndex;
			//MsgCon("Anim: %s     - FPS: %.2f\n", DevKitOnly_StringIdToStringOrHex(m_lastAnimId), m_lastFps);
			m_lastAnimBoundFrame = finalAnimation.m_cameraFrameWs;

			const NdGameObject* pFocusObj = GetFocusObject().ToProcess();
			if (pFocusObj)
			{
				m_lastFocusPos = pFocusObj->GetTranslation();
			}
		}
	}
	/*else if (m_usedByAnimationDirector && GetFocusObject())
	{
		Locator cameraOut, cameraOutOrig;
		StringId64 channelId = StringId64ConcatInteger(SID("apReference-camera"), m_cameraIndex + 1);
		AnimControl *pAnimControl = GetFocusObject()->GetAnimControl();

		F32 phase = 0.0f;
		F32 lastAnimPhase = 0.0f;
		if (const ArtItemAnim* pAnim = pAnimControl->LookupAnim(m_lastAnimId).ToArtItem())
		{
			m_prevFrameIndex += GetProcessDeltaTime() * 30.0f;
			F32 numFrames = GetDuration(pAnim)*30.0f;
			phase = Limit01(m_prevFrameIndex / numFrames);
			lastAnimPhase = Limit01(m_lastFrameIndex / numFrames);
		}

		bool found = EvaluateChannelInAnim(pAnimControl, m_lastAnimId, channelId, phase, &cameraOut);
		found = found && EvaluateChannelInAnim(pAnimControl, m_lastAnimId, channelId, lastAnimPhase, &cameraOutOrig);
		if (found)
		{
			Locator newFrameDelta = cameraOutOrig.UntransformLocator(cameraOut);
			Locator newFrame = m_lastAnimBoundFrame.GetLocator().TransformLocator(newFrameDelta);
			Vector deltaFo = GetFocusObject()->GetTranslation() - m_lastFocusPos;
			newFrame.Move(deltaFo);
			m_finalAnimation.m_cameraFrameWs.SetLocator(newFrame);
		}
	}*/
	else
	{
		//MsgConScriptError("Failed to find Camera-Anim in %s, camera-index: %d\n", DevKitOnly_StringIdToString(m_animId), m_cameraIndex);
	}

	// register camera cuts with the camera manager, so that other subsystems can respond appropriately
	if (finalAnimation.m_cut)
	{
		CameraManager::GetGlobal(GetPlayerIndex()).NotifyCameraCut(FILE_LINE_FUNC); // technically redundant -- animation evaluation already notifies cuts -- but good in case we ever have a "pure" camera animation
	}
}

///-----------------------------------------------------------------------------------------///
AnimInstanceHandle CameraControlAnimated::GetAnimSourceInstance() const
{
	if (m_hAnimSource.IsValid())
	{
		return CameraRequestExtensionAnimated::GetAnimSourceInstance(m_sourceHandleList, m_cameraIndex);
	}
	else
	{
		CameraRequestExtensionAnimated* pExt;
		if (!g_animatedCameraOptions.m_useOldExtension)
		{
			pExt = m_pExtension;
		}
		else
		{
			CameraRequest* pRequest = CameraManager::GetGlobal(m_playerIndex).FindPersistentCameraRequest(FindAnimatedCameraRequest, (uintptr_t)ToBlindPtr(m_ownerId));
			pExt = (pRequest) ? PunPtr<CameraRequestExtensionAnimated*>(pRequest->m_pExtension) : nullptr;
		}

		if (pExt)
		{
			return pExt->GetAnimSourceInstance(m_cameraIndex);
		}
	}

	return AnimInstanceHandle();
}

void CameraAnimation::DebugDraw(CameraAnimationPriority priority, const char* name) const
{
	STRIP_IN_FINAL_BUILD;

	if (g_debugDrawStateScriptCamera)
	{
		const CameraAnimation& animation = *this;

		const char* priorityString;
		Color color;
		Vector stringOffset(kZero);
		static F32 kStringOffset = 0.2f;

		switch (priority)
		{
		case kCameraAnimationPriorityCamera:
			priorityString = "CAM";
			color = (animation.m_cutCount != 0) ? kColorOrange : kColorGreen;
			break;
		case kCameraAnimationPriorityPlayer:
			priorityString = "PLR";
			color = (animation.m_cutCount != 0) ? kColorCyan : kColorBlue;
			break;
		case kCameraAnimationPriorityNpc:
			priorityString = "NPC";
			color = (animation.m_cutCount != 0) ? kColorYellow : kColorRed;
			stringOffset += Scalar(kStringOffset) * Vector(kUnitYAxis);
			break;
		default:
			priorityString = "OBJ";
			color = (animation.m_cutCount != 0) ? kColorWhite : Color(0.5f, 0.5f, 0.5f, 1.0f);
			stringOffset += Scalar(2.0f * kStringOffset) * Vector(kUnitYAxis);
			break;
		}

		char label[512];
		snprintf(label, sizeof(label)-1, "%05d:%.2f: %s%s%d %s (fov=%.2f c=%.4f%s) (sx=%.4f sy=%.4f sz=%.4f)",
			EngineComponents::GetNdFrameState()->m_gameFrameNumber,
			animation.m_frameIndex,
			priorityString, Spaces(animation.m_cameraIndex + 1), (animation.m_cameraIndex + 1),
			name,
			animation.m_fov,
			animation.m_cutChannel,
			((animation.m_cutCount != 0) ? " CUT!" : ""),
			(F32)animation.m_rawSqt.m_scale.X(), (F32)animation.m_rawSqt.m_scale.Y(), (F32)animation.m_rawSqt.m_scale.Z());
		label[sizeof(label)-1] = '\0';

		static F32 kDebugDrawCameraUpdatesTime_sec = 8.0f/30.0f;
		g_prim.Draw( DebugCoordAxes(animation.m_cameraFrameWs.GetLocator(), 1.0f, PrimAttrib(0)),
						Seconds(kDebugDrawCameraUpdatesTime_sec));
		if (animation.m_hasFocalPoint)
		{
			g_prim.Draw( DebugCross(animation.m_cameraFocalPointWs.GetTranslationWs(), 0.5f, kColorRed, PrimAttrib(0)),
							Seconds(kDebugDrawCameraUpdatesTime_sec));
			g_prim.Draw( DebugString(animation.m_cameraFocalPointWs.GetTranslationWs(), "focal point", kColorRed),
							Seconds(kDebugDrawCameraUpdatesTime_sec));
		}
		g_prim.Draw( DebugString(animation.m_cameraFrameWs.GetTranslation() + stringOffset, label, color, 1.0f),
						Seconds(kDebugDrawCameraUpdatesTime_sec));

		if (g_debugDrawStateScriptCameraLs && priority == kCameraAnimationPriorityPlayer)
		{
			//g_prim.Draw( DebugCoordAxes(cameraLocLs, 0.5f, PrimAttrib(0), 1.0f), Seconds(kDebugDrawCameraUpdatesTime_sec));
			//g_prim.Draw( DebugString(cameraLocLs.GetTranslation(), label, color, 1.0f), Seconds(kDebugDrawCameraUpdatesTime_sec));
		}

		MsgScript("%s\n", label);
	}
}

Locator CameraControlAnimated::ModifyAnimatedLocator(float *pFov, const CameraInstanceInfo &info, const Locator &loc, bool wasCut)
{
	Locator modifiedLoc = loc;
	const NdGameObject* pFocusObj = m_hFocusObj.ToProcess();
	if (m_enableLookAround && pFocusObj)
	{
		if (m_focusCentered)
		{
			Point lookAtPoint = modifiedLoc.GetTranslation() + GetLocalZ(modifiedLoc.GetRotation()) * 2.0f;

			modifiedLoc = m_cameraControl.GetLocator(info.m_joyPad, modifiedLoc, lookAtPoint, GetLocalZ(info.m_prevLocation.GetLocator().Rot()), m_finalAnimation.m_fov);
			modifiedLoc.Move(m_offset + m_proceduralOffset);
		}
		else
		{
			Point lookAtPoint = pFocusObj->GetSite(SID("LookAtPos")).GetTranslation();
			AttachIndex attachindex;
			bool foundHead = pFocusObj->GetAttachSystem()->FindPointIndexById(&attachindex, SID("targHead"));
			if (foundHead)
				lookAtPoint = pFocusObj->GetAttachSystem()->GetAttachPosition(attachindex);

			modifiedLoc = m_cameraControl.GetLocator(info.m_joyPad, modifiedLoc, lookAtPoint, GetLocalZ(info.m_prevLocation.GetLocator().Rot()), m_finalAnimation.m_fov, m_scaleLocator, wasCut);
			modifiedLoc.Move(m_offset + m_proceduralOffset);
		}
	}
	else if (m_focusAttachPoint != INVALID_STRING_ID_64 && pFocusObj)
	{
		AttachIndex attachIndex;
		const bool foundPoint = pFocusObj->GetAttachSystem()->FindPointIndexById(&attachIndex, m_focusAttachPoint);
		if (foundPoint)
		{
			const Point lookAtPoint = pFocusObj->GetAttachSystem()->GetAttachPosition(attachIndex);

			m_lookTracker.Update(lookAtPoint, loc, wasCut, modifiedLoc);

			m_finalAnimation.m_dof.focalPlaneDist = Length(modifiedLoc.GetTranslation() - lookAtPoint);
		}
	}

	if (g_cameraOptions.m_uprightAnimatedCamera)
	{
		Vector fwd = GetLocalZ(modifiedLoc.GetRotation());
		Quat uprightRot = QuatFromLookAt(fwd, kUnitYAxis);
		modifiedLoc.SetRot(uprightRot);
	}

	return modifiedLoc;
}

bool CameraControlAnimated::GetRunWhenPaused() const
{
	return m_flags.m_runWhenPaused || FALSE_IN_FINAL_BUILD(HasDofRenderSettingsTweak());
}

void CameraControlAnimated::DOFUpdate(const CameraInstanceInfo& info)
{
	DC::RenderSettings &rs = GetRenderSettingsForPlayer(m_playerIndex);

	if (!m_finalAnimation.m_dof.IsSuppressed() && g_renderOptions.m_renderSettingsApply && !g_cameraOptions.m_disableIgcDof)
	{
		SettingSetDefault(&g_renderOptions.m_gameplayForceEnableDof, false);
		SettingSetPers(SID("gameplay-force-dof"), &g_renderOptions.m_gameplayForceEnableDof, true, kCutsceneCameraPriority, 1.f);

		U32F priority = kCutsceneCameraPriority - info.m_stackIndex;
		float blend = info.m_blend;

		// These are the NEW DOF method values; we use these!!
		SettingSet(&g_renderOptions.m_dofBlendFactor, 1.0f, priority, 1.0f, this);	// We want Background Blur disabled for cinematic cameras
		SettingSet(&rs.m_post.m_dofBackgroundBlurScale, m_finalAnimation.m_dof.dofBgCocScale, FALSE_IN_FINAL_BUILD(HasRenderSettingsTweak(SID("post:dof-background-blur-scale"))) ? kCutsceneCameraRenderSettingsTweakPriority : priority, blend, this);
		SettingSet(&rs.m_post.m_dofForegroundBlurScale, m_finalAnimation.m_dof.dofFgCocScale, FALSE_IN_FINAL_BUILD(HasRenderSettingsTweak(SID("post:dof-foreground-blur-scale"))) ? kCutsceneCameraRenderSettingsTweakPriority : priority, blend, this);
		SettingSet(&rs.m_post.m_dofBackgroundBlurThresold, m_finalAnimation.m_dof.dofBgCocThreshold, FALSE_IN_FINAL_BUILD(HasRenderSettingsTweak(SID("post:dof-background-blur-thresold"))) ? kCutsceneCameraRenderSettingsTweakPriority : priority, blend, this);
		SettingSet(&rs.m_post.m_dofForegroundBlurThresold, m_finalAnimation.m_dof.dofFgCocThreshold, FALSE_IN_FINAL_BUILD(HasRenderSettingsTweak(SID("post:dof-foreground-blur-thresold"))) ? kCutsceneCameraRenderSettingsTweakPriority : priority, blend, this);
		SettingSet(&rs.m_post.m_dofFilmWidth, m_finalAnimation.m_dof.filmWidth, FALSE_IN_FINAL_BUILD(HasRenderSettingsTweak(SID("post:dof-film-width"))) ? kCutsceneCameraRenderSettingsTweakPriority : priority, blend, this);
		SettingSet(&rs.m_post.m_dofFNumber, m_finalAnimation.m_dof.fNumber, FALSE_IN_FINAL_BUILD(HasRenderSettingsTweak(SID("post:dof-f-number"))) ? kCutsceneCameraRenderSettingsTweakPriority : priority, blend, this);
		SettingSet(&rs.m_post.m_dofLensFocalLength, m_finalAnimation.m_dof.focalLen, FALSE_IN_FINAL_BUILD(HasRenderSettingsTweak(SID("post:dof-lens-focal-length"))) ? kCutsceneCameraRenderSettingsTweakPriority : priority, blend, this);
		SettingSet(&rs.m_post.m_dofFocusPlaneDist, m_finalAnimation.m_dof.focalPlaneDist, FALSE_IN_FINAL_BUILD(HasRenderSettingsTweak(SID("post:dof-focus-plane-dist"))) ? kCutsceneCameraRenderSettingsTweakPriority : priority, blend, this);
	}
	else
	{
		CameraControl::DOFUpdate(info);
	}
}

///-----------------------------------------------------------------------------------------///
Locator CameraControlAnimated::GetAnimatedCamLocator() const
{
	Locator loc = m_finalAnimation.m_cameraFrameWs.GetLocator();
	loc.Move(m_offset + m_proceduralOffset);

	return loc;
}

void CameraControlAnimated::BlendToOffset(Vector_arg offset)
{
	m_proceduralOffset = m_offsetTracker.Track(m_proceduralOffset, offset, GetProcessDeltaTime(), 6.0f);
}

CameraLocation CameraControlAnimated::UpdateLocation(const CameraInstanceInfo& info)
{
	if (g_debugAnimatedCamera && !EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
		MsgCamera("%05u: UpdateLocation\n", (U32)EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused);

	bool wasCut = m_finalAnimation.m_cut;
	OMIT_FROM_FINAL_BUILD(if (g_animOptions.m_debugCameraCutsSuperVerbose) MsgCinematic("%05u: cleared CUT flag by CameraControlAnimated::UpdateLocation() [wasCut = %d]\n", (U32)EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused, wasCut);)
	m_finalAnimation.m_cut = false;
	m_prevCutChannel = m_finalAnimation.m_cutChannel;

	float fov = m_finalAnimation.m_fov;

	if (m_startFov > 0.0f)
	{
		F32 l2Amt = EngineComponents::GetNdFrameState()->GetJoypad(DC::kJoypadPadPlayer1 + m_playerIndex)->GetL2Analog();
		m_analogFovTt = m_fovTracker.Track(m_analogFovTt, l2Amt, GetProcessDeltaTime(), 10.0f);
		fov = Lerp(m_startFov, fov, m_analogFovTt);
	}

	Locator loc = GetAnimatedCamLocator();
	loc = ModifyAnimatedLocator(&fov, info, loc, wasCut);
	loc = m_cameraShake.GetLocator(GetPlayerIndex(), loc, 1.0f, false);

	if (GetProcessDeltaTime() > 0.f)
	{
		const Quat rotDelta = Conjugate(info.m_prevLocation.GetLocator().GetRotation()) * loc.GetRotation();

		Vec4 rAxis;
		float rAngle;
		rotDelta.GetAxisAndAngle(rAxis, rAngle);

		m_cameraSpeed = RADIANS_TO_DEGREES(rAngle) / GetProcessDeltaTime();
	}

	NdGameObject* pPlayer = EngineComponents::GetNdGameInfo()->GetPlayerGameObject();
	if (g_cameraOptions.m_pivotBlendMeleeCameras && pPlayer)
	{
		Vector toPlayer = pPlayer->GetTranslation() - loc.GetTranslation();
		F32 dot = Dot(toPlayer, GetLocalZ(loc.GetRotation()));
		Point targetPoint = loc.GetTranslation() + dot * GetLocalZ(loc.GetRotation());

		return CameraLocation(loc, targetPoint, fov);
	}

	if (pPlayer)
	{
		Locator vertigoLoc = loc;
		BoxedValue result = SendEvent(SID("player-focus-apply-vertigo-offset"), pPlayer, &vertigoLoc);
		if (result.IsValid())
			loc = result.GetLocator();

		float vertigoFov = fov;
		result = SendEvent(SID("player-focus-apply-vertigo-fov"), pPlayer, vertigoFov);
		if (result.IsValid())
			fov = result.GetAsF32(fov);
	}

	return CameraLocation(loc, fov);
}

// -----------------------------------------------------------------------------------------------------------
// Camera Animation Structs
// -----------------------------------------------------------------------------------------------------------

static CameraDofRaw s_defaultCameraDof;

/*static*/ void CameraAnimation::Init()
{
	// determine default settings to use when Clear()ing a CameraAnimation.
	const DC::RenderSettings* pSettings = ScriptManager::LookupInModule<DC::RenderSettings>(SID("*default-render-settings*"), SID("render-settings"), nullptr);
	if (pSettings)
	{
		s_defaultCameraDof.SetFromRenderSettings(*pSettings);
		s_defaultCameraDof.suppressDof = 1.0f; // >= 0.5f means "suppressed"
	}
}

void CameraDofRaw::SetFromRenderSettings(const DC::RenderSettings& rs)
{
	dofBgCocScale = rs.m_post.m_dofBackgroundBlurScale;
	dofFgCocScale = rs.m_post.m_dofForegroundBlurScale;
	dofBgCocThreshold = rs.m_post.m_dofBackgroundBlurThresold;
	dofFgCocThreshold = rs.m_post.m_dofForegroundBlurThresold;
	filmWidth = rs.m_post.m_dofFilmWidth;
	fNumber = rs.m_post.m_dofFNumber;
	focalLen = rs.m_post.m_dofLensFocalLength;
	focalPlaneDist = rs.m_post.m_dofFocusPlaneDist;
	suppressDof = 0.0f; // >= 0.5f means "suppressed"
}

void CameraDofRaw::Clear()
{
	*this = s_defaultCameraDof; // ok since we don't add data members to CameraDofRaw
}

void CameraDofRaw::Lerp(const CameraDofRaw& a, const CameraDofRaw& b, float blend)
{
	dofBgCocScale		= ::Lerp(a.dofBgCocScale,		b.dofBgCocScale,		blend);
	dofFgCocScale		= ::Lerp(a.dofFgCocScale,		b.dofFgCocScale,		blend);
	dofBgCocThreshold	= ::Lerp(a.dofBgCocThreshold,	b.dofBgCocThreshold,	blend);
	dofFgCocThreshold	= ::Lerp(a.dofFgCocThreshold,	b.dofFgCocThreshold,	blend);
	filmWidth			= ::Lerp(a.filmWidth,			b.filmWidth,			blend);
	fNumber				= ::Lerp(a.fNumber,				b.fNumber,				blend);
	focalLen			= ::Lerp(a.focalLen,			b.focalLen,				blend);
	focalPlaneDist		= ::Lerp(a.focalPlaneDist,		b.focalPlaneDist,		blend);
}

void CameraAnimation::Clear()
{
	m_cameraFrameWs = BoundFrame(kIdentity);
	m_cameraFocalPointWs = BoundFrame(kIdentity);
	m_fov = 0.0f;
	m_rawSqt.m_quat = Quat(kIdentity);
	m_rawSqt.m_scale = Vector(1.0f, 1.0f, 1.0f);
	m_rawSqt.m_trans = Point(kZero);
	m_isValid = false;
	m_hasFocalPoint = false;

	m_dof.Clear();

	m_cameraIndex = 0u;
	m_numContributingStates = 0u;
	m_animStateName = INVALID_STRING_ID_64;
	m_frameIndex = -1.0f;					// frame index at which camera data was extracted
	m_cutCount = 0u;
	m_cutChannel = -1.0f;
	m_cut = false;

	m_activeChannel = 1.0f;
	m_slowmoChannel = 1.0f;
}

// -----------------------------------------------------------------------------------------------------------
// AnimInstanceAdaptor: provides a consistent API for both types of AnimInstance
// -----------------------------------------------------------------------------------------------------------

struct AnimInstanceAdaptor
{
	AnimInstanceAdaptor(AnimSimpleInstance& simpleInst)
		: m_pStateInst(nullptr)
		, m_pSimpleInst(&simpleInst)
	{
	}
	AnimInstanceAdaptor(AnimStateInstance& stateInst)
		: m_pStateInst(&stateInst)
		, m_pSimpleInst(nullptr)
	{
	}

	F32			PrevPhase() const		{ return (m_pStateInst)	? m_pStateInst->PrevPhase()		: m_pSimpleInst->GetPrevPhase(); }
	F32			PrevMayaFrame() const	{ return (m_pStateInst)	? m_pStateInst->PrevMayaFrame()	: m_pSimpleInst->GetPrevMayaFrame(); }
	F32			Phase() const			{ return (m_pStateInst)	? m_pStateInst->Phase()			: m_pSimpleInst->GetPhase(); }
	F32			MayaFrame() const		{ return (m_pStateInst)	? m_pStateInst->MayaFrame()		: m_pSimpleInst->GetMayaFrame(); }
	F32			GetFrameCount() const	{ return (m_pStateInst)	? m_pStateInst->GetFrameCount()	: m_pSimpleInst->GetFrameCount(); }
	BoundFrame	GetCurrentApLoc() const	{ return (m_pStateInst) ? m_pStateInst->GetApLocator()	: m_pSimpleInst->GetApOrigin(); }
	StringId64	GetAnimId() const		{ return (m_pStateInst) ? m_pStateInst->GetPhaseAnim()	: m_pSimpleInst->GetAnimId(); }
	StringId64	GetApRefId() const		{ return (m_pStateInst) ? m_pStateInst->GetApRefChannelId() : m_pSimpleInst->GetApChannelName();}

	F32	PhaseToMayaFrame(F32 phase) const
	{
		return (m_pStateInst) ? m_pStateInst->PhaseToMayaFrame(phase) : m_pSimpleInst->PhaseToMayaFrame(phase);
	}

	bool ShouldUseApRef() const
	{
		if (m_pStateInst != nullptr)
			return m_pStateInst->IsApLocatorActive();
		else
			return m_pSimpleInst->IsAlignedToActionPackOrigin();
	}

	U32F EvaluateChannels(const StringId64* channelNames,
						  U32F numChannels,
						  F32 phase,
						  ndanim::JointParams* outChannelJoints,
						  bool wantRawScale,
						  AnimCameraCutInfo* pCameraCutInfo,
						  DC::AnimFlipMode flipMode) const
	{
		ANIM_ASSERT(m_pStateInst || m_pSimpleInst);

		if (m_pStateInst)
		{
			AnimStateEvalParams params;
			params.m_wantRawScale = wantRawScale;
			params.m_pCameraCutInfo = pCameraCutInfo;
			params.m_flipMode = flipMode;

			return m_pStateInst->EvaluateChannels(channelNames,
												  numChannels,
												  outChannelJoints,
												  params);
		}
		else if (m_pSimpleInst)
		{
			EvaluateChannelParams params;
			params.m_mirror = flipMode == DC::kAnimFlipModeAlways;
			params.m_wantRawScale	= wantRawScale;
			params.m_pCameraCutInfo = pCameraCutInfo;
			params.m_phase = phase;

			return m_pSimpleInst->EvaluateChannels(channelNames,
												   numChannels,
												   outChannelJoints,
												   params);
		}

		return 0;
	}

private:
	AnimInstanceAdaptor(); // default ctor disabled
	AnimStateInstance*	m_pStateInst;
	AnimSimpleInstance*	m_pSimpleInst;
};

// -----------------------------------------------------------------------------------------------------------
// Camera Animation Functions
// -----------------------------------------------------------------------------------------------------------
static bool EvaluateCameraAnimationChannel(AnimInstanceAdaptor& animInst,
										   float phase,
										   const StringId64 channelId,
										   const Locator& baseAlignLoc,
										   const bool returnLocalSpace,
										   CameraAnimationChannel& channel,
										   DC::AnimFlipMode flipMode,
										   int cameraIndex)
{
	// First evaluate the channel both at the previous phase, and at a phase that corresponds to the
	// *next* discrete frame at or past the current phase.
	const float curPhase = MinMax01(phase);

	ndanim::JointParams cameraSqt;

	AnimCameraCutInfo cameraCutInfo;
	cameraCutInfo.m_cameraIndex = cameraIndex;
	cameraCutInfo.m_didCameraCut = false;

	U32F cameraEvaluatedChannels = animInst.EvaluateChannels(&channelId, 1, curPhase, &cameraSqt, true, &cameraCutInfo, flipMode);
	if (cameraEvaluatedChannels != 0u)
	{
		channel.cutChannel = static_cast<F32>(cameraSqt.m_scale.Z());

		// Look for camera cut markers in the m_scale.Z() channel.
		if (cameraCutInfo.m_didCameraCut)
		{
			++channel.cutCount;
		}
		else
		{
			ASSERT(cameraEvaluatedChannels != 0);	// can set a breakpoint here
		}

		Locator alignLoc = baseAlignLoc;

		ndanim::JointParams aprefSqt;
		StringId64 apRefChannel = animInst.GetApRefId();
		U32F aprefEvaluatedChannels = animInst.EvaluateChannels(&apRefChannel, 1, curPhase, &aprefSqt, false, nullptr, flipMode);
		if (aprefEvaluatedChannels != 0u && animInst.ShouldUseApRef() && !returnLocalSpace)
		{
			Locator aprefLoc(aprefSqt.m_trans, aprefSqt.m_quat);
			const Locator aprefInvLoc = Inverse(aprefLoc);
			alignLoc = animInst.GetCurrentApLoc().GetLocator().TransformLocator(aprefInvLoc);
		}

		Locator cameraLoc(cameraSqt.m_trans, cameraSqt.m_quat);
		Locator finalLocWs = alignLoc.TransformLocator(cameraLoc);

		// Return the resulting joint pose.
		channel.sqt.m_trans = finalLocWs.GetTranslation();
		channel.sqt.m_quat = finalLocWs.GetRotation();
		channel.sqt.m_scale = cameraSqt.m_scale;
		channel.frameIndex = animInst.PhaseToMayaFrame(curPhase);
		return true;
	}

	return false;
}

// -----------------------------------------------------------------------------------------------------------
struct CameraChannelPair
{
	CameraChannelPair()
	{
		m_sqt.m_quat = Quat(kIdentity);
		m_sqt.m_scale = Vector(1.0f, 1.0f, 1.0f);
		m_sqt.m_trans = kOrigin;
		m_valid = false;
	}

	CameraChannelPair(const ndanim::JointParams& jp)
	{
		m_sqt = jp;
		m_valid = true;
	}

	ndanim::JointParams m_sqt;
	bool m_valid;
};

// -----------------------------------------------------------------------------------------------------------
static bool GetCameraAnimationFromAnimStateInstance(
	const AnimStateInstance* pInstance,
	const CameraAnimationChannelRequest& request,
	CameraChannelPair& resultChannelPair,
	CameraAnimationChannel& resultChannel)
{
	AnimInstanceAdaptor animInst(*const_cast<AnimStateInstance*>(pInstance));

	DC::AnimFlipMode flipMode = request.pFlip ? (*request.pFlip)(request, pInstance->GetStateName()) : DC::kAnimFlipModeFromInstance;

	CameraChannelPair p;
	CameraAnimationChannel currentChannel;

	const float phase = animInst.Phase();
	//if (request.m_deltaTimeInSecs.Valid())
	//{
	//	float duration = pInstance->GetDuration();
	//	if (duration != 0.f)
	//	{
	//		phase += request.m_deltaTimeInSecs.Get() / duration; // only works for animation steps uniformly
	//		phase = MinMax01(phase);
	//	}
	//}

	bool bChannelsEvaluated = EvaluateCameraAnimationChannel(animInst, 
															 phase, 
															 request.channelId, 
															 request.baseAlignLoc, 
															 request.returnLocalSpace, 
															 currentChannel, 
															 flipMode, 
															 request.cameraIndex);

	if (bChannelsEvaluated)
	{
		CameraAnimationChannelRequest requestCopy = request;
		requestCopy.pAnimStateInstance = pInstance;
		if (!request.pFilter || (*request.pFilter)(requestCopy, currentChannel, pInstance->GetStateName()))
		{
			if (resultChannel.numContributingStates == 0u)
			{
				resultChannel.sqt = currentChannel.sqt;
				resultChannel.frameIndex = currentChannel.frameIndex;
				resultChannel.animStateName = pInstance->GetStateName(); // FIXME: returns a single state name even if there's a blend going on
				resultChannel.phaseAnimName = pInstance->GetPhaseAnim();
				resultChannel.cutChannel = currentChannel.cutChannel;
			}

			p.m_valid = true;
			++resultChannel.numContributingStates;
			resultChannel.cutCount += currentChannel.cutCount;
		}
	}

	p.m_sqt = currentChannel.sqt;

	resultChannelPair = p;

	return true;
}

// -----------------------------------------------------------------------------------------------------------
class CameraChannelBlender : public AnimStateLayer::InstanceBlender<CameraChannelPair>
{
public:
	CameraChannelBlender(const CameraAnimationChannelRequest& request) : m_request(request) { m_channel.Clear(); }

	virtual CameraChannelPair GetDefaultData() const override { return CameraChannelPair(); }

	bool GetDataForInstance(const AnimStateInstance* pInstance, CameraChannelPair* pDataOut) override
	{
		return GetCameraAnimationFromAnimStateInstance(pInstance, m_request, *pDataOut, m_channel);
	}

	virtual CameraChannelPair BlendData(const CameraChannelPair& leftData, const CameraChannelPair& rightData, float masterFade, float animFade, float motionFade) override
	{
		CameraChannelPair blended = leftData;
		if (rightData.m_valid)
		{
			if (leftData.m_valid && m_channel.numContributingStates > 1)
			{
				AnimChannelJointBlend(&blended.m_sqt, leftData.m_sqt, rightData.m_sqt, motionFade);
			}
			else
			{
				AnimChannelJointBlend(&blended.m_sqt, leftData.m_sqt, rightData.m_sqt, 1.0f);
				blended.m_valid = true;
			}
		}

		if (!m_request.blendActiveChannel)
		{
			if (!rightData.m_valid)
			{
				blended.m_sqt.m_trans.SetX(0.0f);
			}
		}

		return blended;
	}

	CameraAnimationChannel m_channel;
	CameraAnimationChannelRequest m_request;
};

static void GetAnimChannelFromSimpleInstance(const AnimSimpleInstance* pInst, const CameraAnimationChannelRequest& request, CameraAnimationChannel& channel)
{
	ALWAYS_ASSERT(pInst);
	AnimInstanceAdaptor animInst(*const_cast<AnimSimpleInstance*>(pInst));

	DC::AnimFlipMode flipMode = request.pFlip ? (*request.pFlip)(request, SID("*simple-layer*")) : DC::kAnimFlipModeFromInstance;

	// calculate phase
	const float phase = pInst->GetPhase();
	//if (request.m_deltaTimeInSecs.Valid())
	//{
	//	float duration = pInst->GetDuration();
	//	if (duration != 0.f)
	//	{
	//		phase += request.m_deltaTimeInSecs.Get() / duration; // only works for animation steps uniformly
	//		phase = MinMax01(phase);
	//	}
	//}

	CameraAnimationChannel currentChannel;
	bool bChannelsEvaluated = EvaluateCameraAnimationChannel(animInst, 
															 phase, 
															 request.channelId, 
															 request.baseAlignLoc,
															 request.returnLocalSpace, 
															 currentChannel, 
															 flipMode, 
															 request.cameraIndex);

	if (bChannelsEvaluated)
	{
		if (!request.pFilter || (*request.pFilter)(request, currentChannel, SID("*simple-layer*")))
		{
			channel = currentChannel;
			channel.numContributingStates = 1;
			channel.animStateName = SID("*simple-layer*"); // special "state" name indicating that the data came from a simple layer
			channel.phaseAnimName = pInst->GetAnimId();
		}
	}
}

bool GetCameraAnimationChannel(const CameraAnimationChannelRequest& request, CameraAnimationChannel& channel)
{
	// these defaults are returned only if no camera animation data were found
	channel.Clear();

	if (request.pAnimStateInstance != nullptr)
	{
		CameraChannelPair resultChannelPair;
		GetCameraAnimationFromAnimStateInstance(
			request.pAnimStateInstance,
			request,
			resultChannelPair,
			channel);
		return (channel.numContributingStates != 0u);
	}

	if (request.pAnimSimpleInstance != nullptr)
	{
		GetAnimChannelFromSimpleInstance(request.pAnimSimpleInstance, request, channel);
		return (channel.numContributingStates != 0u);
	}

	ASSERT(request.pAnimControl);

	const AnimLayer* pAnimLayer = request.pAnimControl->GetLayerById(SID("base"));
	if (pAnimLayer)
	{
		if (pAnimLayer->GetType() == kAnimLayerTypeSimple)
		{
			const AnimSimpleLayer* pBaseLayer = static_cast<const AnimSimpleLayer*>(pAnimLayer);
			const AnimSimpleInstance* pInst = pBaseLayer->CurrentInstance();
			ALWAYS_ASSERT(pInst);
			GetAnimChannelFromSimpleInstance(pInst, request, channel);
		}
		else
		{
			ALWAYS_ASSERT(pAnimLayer->GetType() == kAnimLayerTypeState);
			const AnimStateLayer* pBaseLayer = static_cast<const AnimStateLayer*>(pAnimLayer);

			CameraChannelPair chanPair(channel.sqt);
			CameraChannelBlender blender(request);
			CameraChannelPair blended = blender.BlendForward(pBaseLayer, chanPair);
			blender.m_channel.sqt = blended.m_sqt;

			channel = blender.m_channel;
		}
	}

	return (channel.numContributingStates != 0u);
}

float GetFovFromRawCameraScale(Vector_arg cameraScale)
{
	// Conversion from Maya's focal length to field of view angle is based on assumption
	// of 35mm camera, whose film plane is 36mm wide.  So given focal length f and film
	// plane width d = 36.0f, the focal angle in radians is:
	//     angle_rad = 2 arctan(d/2f)
	// (http://en.wikipedia.org/wiki/Angle_of_view)
	const float kDefaultFov = 75.0f;
	float animatedFocalLength = static_cast<float>(cameraScale.X());
	if (animatedFocalLength <= 0.0f)
		animatedFocalLength = 23.458f;
	float animatedFov = RADIANS_TO_DEGREES(2.0f * atan2f(36.0f, (2.0f * animatedFocalLength)));
	float pctDefault = static_cast<float>(cameraScale.Y());	// 1.0f = use kDefaultFov, 0.0f = use animatedFov

	return LerpScale(0.0f, 1.0f, animatedFov, kDefaultFov, pctDefault);
}

bool GetCameraAnimation(const CameraAnimationRequest& request, CameraAnimation& animation)
{
	animation.Clear();
	animation.m_cameraIndex = request.cameraIndex;

	// find the current main camera's SQT in align space -- use as the neutral transform for blending
//	Locator neutralLoc(request.frame.GetLocator().UntransformLocator(g_mainCameraInfo[0].GetLocator()));
//	neutralLoc.SetRotation(neutralLoc.GetRotation() * QuatFromAxisAngle(Vector(SMath::kUnitYAxis), DEGREES_TO_RADIANS(-180.0f)));

	// extract the locator and field of view data
	CameraAnimationChannelRequest cameraRequest;
	cameraRequest.cameraIndex = request.cameraIndex;
	cameraRequest.pAnimControl = request.pAnimControl;
	cameraRequest.pAnimStateInstance = request.pAnimStateInstance;
	cameraRequest.pAnimSimpleInstance = request.pAnimSimpleInstance;
	cameraRequest.baseAlignLoc = request.frame.GetLocator();
	cameraRequest.channelId = StringId64ConcatInteger(SID("apReference-camera"), request.cameraIndex + 1);
	cameraRequest.returnLocalSpace = false;
	cameraRequest.pFilter = request.pFilter;
	cameraRequest.pFlip = request.pFlip;
	cameraRequest.pFilterContext = request.pFilterContext;

	CameraAnimationChannel cameraChannel;
	if (GetCameraAnimationChannel(cameraRequest, cameraChannel))
	{
		// Pull the other apReference channels...

		CameraAnimationChannelRequest dofRequest = cameraRequest;
		dofRequest.baseAlignLoc = Locator(kIdentity);
		dofRequest.channelId = StringId64ConcatInteger(SID("apReference-dof"), request.cameraIndex + 1);
		dofRequest.returnLocalSpace = true;
		dofRequest.pFilter = nullptr;			// no need to re-run the filter
		dofRequest.pFlip = CameraAnimationChannelFlipFuncNeverFlip;			// never flip this channel. it makes no sense
		CameraAnimationChannel dofChannel;
		const bool dofChannelValid = GetCameraAnimationChannel(dofRequest, dofChannel);

		CameraAnimationChannelRequest stereoRequest = dofRequest;
		stereoRequest.channelId = StringId64ConcatInteger(SID("apReference-stereo"), request.cameraIndex + 1);
		CameraAnimationChannel stereoChannel;
		const bool stereoChannelValid = GetCameraAnimationChannel(stereoRequest, stereoChannel);

		CameraAnimationChannelRequest extraRequest = dofRequest;
		extraRequest.channelId = StringId64ConcatInteger(SID("apReference-extra"), request.cameraIndex + 1);
		extraRequest.blendActiveChannel = false;
		CameraAnimationChannel extraChannel;
		const bool extraChannelValid = GetCameraAnimationChannel(extraRequest, extraChannel);

		CameraAnimationChannelRequest focusRequest = cameraRequest; // NB: not exactly sure why, but for this one we DON'T want local space or an identity baseAlignLoc
		focusRequest.channelId = StringId64ConcatInteger(SID("apReference-focalpoint"), request.cameraIndex + 1);
		CameraAnimationChannel focusChannel;
		const bool focusChannelValid = GetCameraAnimationChannel(focusRequest, focusChannel);

		// extract base camera data
		ASSERT(cameraChannel.numContributingStates != 0u);
		animation.m_isValid = true;
		animation.m_cameraFrameWs = BoundFrame(Locator(cameraChannel.sqt.m_trans, cameraChannel.sqt.m_quat), request.frame.GetBinding());
		animation.m_cameraFrameWs.SetRotation(animation.m_cameraFrameWs.GetRotation() * QuatFromAxisAngle(Vector(SMath::kUnitYAxis), DEGREES_TO_RADIANS(180.0f)));
		animation.m_fov = GetFovFromRawCameraScale(cameraChannel.sqt.m_scale);

		// extract DOF settings (NEW DOF SYSTEM)
		if (dofChannelValid && extraChannelValid)
		{
			animation.m_dof.dofBgCocScale		= dofChannel.sqt.m_scale.X();
			animation.m_dof.dofFgCocScale		= dofChannel.sqt.m_trans.Y();
			animation.m_dof.dofBgCocThreshold	= dofChannel.sqt.m_scale.Y();
			animation.m_dof.dofFgCocThreshold	= dofChannel.sqt.m_trans.Z();
			animation.m_dof.filmWidth			= extraChannel.sqt.m_trans.Y();
			animation.m_dof.fNumber			= extraChannel.sqt.m_trans.Z();
			animation.m_dof.focalLen			= cameraChannel.sqt.m_scale.X();
			animation.m_dof.focalPlaneDist	= dofChannel.sqt.m_trans.X();
			animation.m_dof.suppressDof		= dofChannel.sqt.m_scale.Z(); // if >= 0.5f, suppress animated DOF
		}

		// extract the active and slow mo data
		if (extraChannelValid)
		{
			animation.m_activeChannel = Abs(extraChannel.sqt.m_trans.X());
			animation.m_slowmoChannel = EngineComponents::GetNdGameInfo()->m_enableCameraSlomo ? Max(static_cast<float>(Abs(extraChannel.sqt.m_scale.X())), 0.1f) : 1.0f;
		}

		// extract the stereo data
		if (stereoChannelValid)
		{
			animation.m_distortion = stereoChannel.sqt.m_trans.X();
			animation.m_zeroPlaneDist = stereoChannel.sqt.m_trans.Y();
			if (stereoChannel.sqt.m_trans.Z() >= 0.5f)
			{
				// disable 3D params
				animation.m_distortion = -1.0f;
				animation.m_zeroPlaneDist = -1.0f;
			}
		}

		// extract the focal point
		if (focusChannelValid)
		{
			animation.m_cameraFocalPointWs = BoundFrame(Locator(focusChannel.sqt.m_trans, focusChannel.sqt.m_quat), request.frame.GetBinding());
			animation.m_hasFocalPoint = true;
		}

		// transfer other relevant information from the camera channel into the final result
		animation.m_rawSqt = cameraChannel.sqt;
		animation.m_numContributingStates = cameraChannel.numContributingStates;
		animation.m_animStateName = cameraChannel.animStateName;
		animation.m_phaseAnimName = cameraChannel.phaseAnimName;
		animation.m_frameIndex = cameraChannel.frameIndex;
		animation.m_cutCount = cameraChannel.cutCount;
		animation.m_cutChannel = cameraChannel.cutChannel;

		#if !FINAL_BUILD
			if (g_animOptions.m_debugCameraCutsSuperVerbose)
				MsgCinematic("%05u: animation.m_cutCount = %u [from cameraChannel.cutCount in GetCameraAnimation()]\n", (U32)EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused, animation.m_cutCount);
		#endif
	}

	return (animation.m_numContributingStates != 0u);
}

bool IsCameraAnimationAvailable(const CameraAnimationRequest& request)
{
	// extract the locator and field of view data
	CameraAnimationChannelRequest cameraRequest;
	cameraRequest.cameraIndex = request.cameraIndex;
	cameraRequest.pAnimControl = request.pAnimControl;
	cameraRequest.pAnimStateInstance = request.pAnimStateInstance;
	cameraRequest.pAnimSimpleInstance = request.pAnimSimpleInstance;
	cameraRequest.baseAlignLoc = request.frame.GetLocator();
	cameraRequest.channelId = StringId64ConcatInteger(SID("apReference-camera"), request.cameraIndex + 1);
	cameraRequest.returnLocalSpace = false;
	cameraRequest.pFilter = request.pFilter;
	cameraRequest.pFlip = request.pFlip;
	cameraRequest.pFilterContext = request.pFilterContext;

	CameraAnimationChannel cameraChannel;
	if (GetCameraAnimationChannel(cameraRequest, cameraChannel)) // FIXME: this is terribly inefficient, but it's an easy solution for now
	{
		return true;
	}

	return false;
}

CameraAnimatedStartInfo CameraControlAnimated::GetStartInfo() const
{
	CameraAnimatedStartInfo startInfo;
	startInfo.m_cameraIndex = m_cameraIndex;
	startInfo.m_ownerId = m_ownerId;
	startInfo.m_enableLookAround = m_enableLookAround;
	startInfo.m_lookAtAttachPoint = m_lookAtAttachPoint;
	startInfo.m_noFailOver = m_noFailOver;
	//startInfo.m_enableAlwaysMove = m_enableAlwaysMove; // NO LONGER SUPPORTED
	startInfo.m_scaleLocator = m_scaleLocator;
	startInfo.m_animId = m_animId;
	startInfo.m_focusAttachPoint = m_focusAttachPoint;
	return startInfo;
}

void CameraAnimation::Init(const BoundFrame& frame, float fov)
{
	m_cameraFrameWs = frame;
	m_fov = fov;

	m_dof.Clear();
	m_cutChannel = kInvalidCutChannelValue;
	m_cut = false;
}

void CameraAnimation::Blend(CameraAnimation& result, const CameraAnimation& a, const CameraAnimation& b, float blend)
{
	// Copy over unblended parameters (threshold based on blend factor)
	ASSERT(a.m_isValid && b.m_isValid);
	result.m_isValid = true;
	if (blend <= 0.5f)
	{
		result.m_rawSqt = a.m_rawSqt; // only used for debugging post-blend anyway
		result.m_cameraIndex = a.m_cameraIndex;
		result.m_numContributingStates = a.m_numContributingStates;
		result.m_animStateName = a.m_animStateName;
		result.m_phaseAnimName = a.m_phaseAnimName;
		result.m_frameIndex = a.m_frameIndex;
	}
	else
	{
		result.m_rawSqt = b.m_rawSqt; // only used for debugging post-blend anyway
		result.m_cameraIndex = b.m_cameraIndex;
		result.m_numContributingStates = b.m_numContributingStates;
		result.m_animStateName = b.m_animStateName;
		result.m_phaseAnimName = b.m_phaseAnimName;
		result.m_frameIndex = b.m_frameIndex;
	}

	// Lerp simple parameters
	result.m_cameraFrameWs = BoundFrame( Lerp(a.m_cameraFrameWs.GetLocator(), b.m_cameraFrameWs.GetLocator(), blend), b.m_cameraFrameWs.GetBinding());
	result.m_fov = Lerp(a.m_fov, b.m_fov, blend);
	result.m_distortion = Lerp(a.m_distortion, b.m_distortion, blend);
	result.m_zeroPlaneDist = Lerp(a.m_zeroPlaneDist, b.m_zeroPlaneDist, blend);
	result.m_activeChannel = Lerp(a.m_activeChannel, b.m_activeChannel, blend);
	result.m_slowmoChannel = Lerp(a.m_slowmoChannel, b.m_slowmoChannel, blend);

	// Camera cut blending? Doesn't really make sense...
	ASSERT(a.m_cut == b.m_cut);
	result.m_cutChannel = Lerp(a.m_cutChannel, b.m_cutChannel, blend);
	result.m_cut = a.m_cut || b.m_cut;
	OMIT_FROM_FINAL_BUILD(if (g_animOptions.m_debugCameraCutsSuperVerbose) MsgCinematic("%05u: cut = %d [CameraAnimation::Blend()]\n", (U32)EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused, result.m_cut);)

	// Lerp DOF
	const DC::RenderSettings& rs = g_renderSettings[0];
	CameraDof dof[2];
	if (!a.m_dof.IsSuppressed())
	{
		dof[0] = a.m_dof;
	}
	else
	{
		dof[0].SetFromRenderSettings(rs);
	}
	if (!b.m_dof.IsSuppressed())
	{
		dof[1] = b.m_dof;
	}
	else
	{
		dof[1].SetFromRenderSettings(rs);
	}
	result.m_dof.Lerp(dof[0], dof[1], blend);

	// Lerp the focal point
	if (a.m_hasFocalPoint && b.m_hasFocalPoint)
	{
		result.m_hasFocalPoint = true;
		result.m_cameraFocalPointWs = BoundFrame( Lerp(a.m_cameraFocalPointWs.GetLocator(), b.m_cameraFocalPointWs.GetLocator(), blend), b.m_cameraFocalPointWs.GetBinding());
	}
	else if (a.m_hasFocalPoint)
	{
		result.m_hasFocalPoint = a.m_hasFocalPoint;
		result.m_cameraFocalPointWs = a.m_cameraFocalPointWs;
	}
	else if (b.m_hasFocalPoint)
	{
		result.m_hasFocalPoint = b.m_hasFocalPoint;
		result.m_cameraFocalPointWs = b.m_cameraFocalPointWs;
	}
}

void CameraControlAnimated::FillDebugText(StringBuilder<1024>& sb, bool shortList, const DebugShowParams& params) const
{
	const CameraLocation& loc = GetCameraLocation();
	Point pt = loc.GetLocator().GetTranslation();
	Quat rot = loc.GetLocator().GetRotation();
	const char* typeName = GetTypeName();
	const char* cameraIdName = GetCameraId().ToConfig().GetCameraIdName();

	StringId64 animIdToUse = m_animId ? m_animId : m_finalAnimation.m_phaseAnimName;
	const char* settingsName = DevKitOnly_StringIdToString(animIdToUse);
	const char* secondSettingsName = (m_animId != m_finalAnimation.m_phaseAnimName && m_animId) ? DevKitOnly_StringIdToString(m_finalAnimation.m_phaseAnimName):"same";

	int len = 0;
	if (shortList)
	{
		F32 timePassed = Min(GetStateTimePassed().ToSeconds(), m_blendInfo.m_time.ToSeconds());
		sb.format("%24.24s (%1.2f) [%2.2f/%2.2f] %4.1f (%s->%s)\n",
			typeName,
			m_normalBlend,
			timePassed,
			m_blendInfo.m_time.ToSeconds(),
			loc.GetFov(),
			settingsName, secondSettingsName);
	}
	else
	{
		sb.format(" -- %24.24s [%5u] (%1.2f, %1.2f, %1.2f, %10.10s [%1.1s %1.2f])",
			cameraIdName,
			GetProcessId(),
			m_blend,
			m_normalBlend,
			m_selfFadeOut.m_currFade,
			CameraBlendTypeToString(m_blendInfo.m_type),
			m_blendInfo.m_locationBlendTime >= kTimeFrameZero ? "T" : " ",
			GetLocationBlend());

		if (params.printLocator)
		{
			sb.append_format(" (% 7.2f, % 7.2f, % 7.2f : % 4.2f, % 4.2f, % 4.2f, % 4.2f)",
				(float)pt.X(), (float)pt.Y(), (float)pt.Z(),
				(float)rot.X(), (float)rot.Y(), (float)rot.Z(), (float)rot.W());
		}

		sb.append_format(" %4.1f %5.2f %4.1f %4.1f (%s->%s)%s [cam %d]\n",
			loc.GetFov(),
			loc.GetNearPlaneDist(),
			loc.GetDistortion(),
			loc.GetZeroPlaneDist(),
			settingsName,
			secondSettingsName,
			(IsPhotoMode() ? " (photo)" : ""),
			m_cameraIndex + 1);
	}

	if (params.showRequestFunc && m_func != nullptr)
	{
		sb.append_format("                %s, line:%d\n", m_func, m_line);
	}
}

void CameraControlAnimated::ApplyCameraAnimation(const CameraAnimation& anim)
{
	m_finalAnimation = anim;
}

void SetAnimatedLightParameters(I32 lightIndex, const LightUpdateData &data)
{
	// See if this is a new update or one we already know of
	for (U32F i = 0; i < s_numUpdatedLightInfo; ++i)
	{
		if (s_animatedLightUpdateInfo[i].m_index == lightIndex)
		{
			// Got it!
			s_animatedLightUpdateInfo[i].m_info = data;
			return;
		}
	}

	// It's a new one, add it if we can!
	if (s_numUpdatedLightInfo < kMaxUpdatedIGCLights)
	{
		s_animatedLightUpdateInfo[s_numUpdatedLightInfo].m_index = lightIndex;
		s_animatedLightUpdateInfo[s_numUpdatedLightInfo].m_info = data;
		++s_numUpdatedLightInfo;
	}
	else
	{
		MsgWarn("TOO MANY DECI LIGHT UPDATES! MAXIMUM IS %d! IF YOU NEED MORE, CONTACT NATHAN AND DYLAN!\n", kMaxUpdatedIGCLights);
	}
}

static void UpdateCameraAnimatedLights(AnimControl& animControl, const BoundFrame& frame, int cameraIndex)
{
	U32 iCurLight = 0;
	while (1)
	{
		CameraAnimationChannelRequest lightPositionRequest;
		lightPositionRequest.cameraIndex = cameraIndex;
		lightPositionRequest.pAnimControl = &animControl;
		lightPositionRequest.baseAlignLoc = Locator(kIdentity);
		lightPositionRequest.channelId = StringId64ConcatInteger(SID("apReference-light"), iCurLight + 1);
		lightPositionRequest.returnLocalSpace = false;
		CameraAnimationChannel lightPostionChannel;

		CameraAnimationChannelRequest lightParamsRequest;
		lightPositionRequest.cameraIndex = cameraIndex;
		lightParamsRequest.pAnimControl = &animControl;
		lightParamsRequest.baseAlignLoc = Locator(kIdentity);
		lightParamsRequest.channelId = StringId64ConcatInteger(SID("apReference-light-params"), iCurLight + 1);
		lightParamsRequest.returnLocalSpace = true;
		lightParamsRequest.pFilter = nullptr;
		lightParamsRequest.pFlip = CameraAnimationChannelFlipFuncNeverFlip;
		CameraAnimationChannel lightParamsChannel;

		if (GetCameraAnimationChannel(lightPositionRequest, lightPostionChannel) && GetCameraAnimationChannel(lightParamsRequest, lightParamsChannel))
		{
			if (DynamicGameLight * dLight = IDynamicLight::AddSingleFrame(EngineComponents::GetNdFrameState()->m_gameFrameNumber))
			{
				// Use the information from the animation first, then possibly update that information with the DECI info
				dLight->m_lightBoundFrame = BoundFrame(Locator(lightPostionChannel.sqt.m_trans, lightPostionChannel.sqt.m_quat), frame.GetBinding());
				dLight->m_lightBoundFrame.SetRotation(dLight->m_lightBoundFrame.GetRotation() * QuatFromAxisAngle(Vector(SMath::kUnitYAxis), DEGREES_TO_RADIANS(180.0f)));

				dLight->m_lightColor = Color(lightPostionChannel.sqt.m_scale.X(), lightPostionChannel.sqt.m_scale.Y(), lightPostionChannel.sqt.m_scale.Z(), 1.0f);

				dLight->m_radius = lightParamsChannel.sqt.m_trans.X();
				if (lightParamsChannel.sqt.m_trans.Y() > 0.0f)
				{
					dLight->m_shadowType = kSpotShadow;
					dLight->m_type = kSpotLight;
					dLight->m_coneAngle = DEGREES_TO_RADIANS(lightParamsChannel.sqt.m_trans.Y());
					dLight->m_penumbraAngle = DEGREES_TO_RADIANS(lightParamsChannel.sqt.m_scale.Z()) * g_renderSettings[0].m_shadow.m_spotLightPenumbraRatio;
				}
				else
				{
					dLight->m_shadowType = kOmniShadow;
					dLight->m_type = kPointLight;
				}

				if (lightParamsChannel.sqt.m_trans.Z() == 0.0f)
					dLight->m_shadowType = kNoShadow;

				dLight->m_lightIntensity = lightParamsChannel.sqt.m_scale.X();

				switch((I32)lightParamsChannel.sqt.m_scale.Y())
				{
				case 0:
					dLight->m_falloffType = kNoFalloff;
					break;
				case 1:
					dLight->m_falloffType = kLinearFalloff;
					break;
				case 2:
					dLight->m_falloffType = kQuadraticFalloff;
					break;
				case 3:
					dLight->m_falloffType = kCubicFallloff;
					break;
				}

				dLight->m_uniqueLightId = StringId64ConcatInteger(SID("igc-camera-light"), iCurLight + 1);

				// If we're updating using the DECI info, apply it here!
				for (U32F i = 0; i < s_numUpdatedLightInfo; ++i)
				{
					if (s_animatedLightUpdateInfo[i].m_index == iCurLight)
					{
						const LightUpdateData &deciData = s_animatedLightUpdateInfo[i].m_info;

						Point pos(deciData.m_pos[0], deciData.m_pos[1], deciData.m_pos[2]);
						Vector lightDir(deciData.m_dir[0], deciData.m_dir[1], deciData.m_dir[2]);
						Quat rot = QuatFromLookAt(lightDir, kUnitYAxis);

						dLight->m_lightBoundFrame = BoundFrame(pos, rot, frame.GetBinding());
						dLight->m_lightColor = Color(deciData.m_color[0], deciData.m_color[1], deciData.m_color[2]);
						dLight->m_lightIntensity = deciData.m_intensity;
						dLight->m_radius = deciData.m_radius;
						dLight->m_coneAngle = deciData.m_spotConeAngle;
						dLight->m_penumbraAngle = deciData.m_spotPenumbraAngle;

						// TODO: Spot light drop off?

						break;
					}
				}
			}
			++iCurLight;
		}
		else
			break;
	}
}

void PopulateAnimatedCameraMenu(DMENU::Menu* pSubMenu, I64* pSceneCameraID)
{
	// This is bad because disableAniamtedCamera isn't a setting by default
	//DMENU::ItemBool* pItemBool = NDI_NEW DMENU::ItemBool("Disable Player Animated Camera", DMENU::ToggleSettingsBool, &g_cameraOptions.m_disableAnimatedCamera);
	//pSubMenu->PushBackItem(pItemBool);

	DMENU::ItemBool* pItemBool = NDI_NEW DMENU::ItemBool("Disable DOF in IGCs", &g_cameraOptions.m_disableIgcDof);
	pSubMenu->PushBackItem(pItemBool);
	OMIT_FROM_FINAL_BUILD(pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Disable New Cam Extension", &g_animatedCameraOptions.m_useOldExtension)));
	OMIT_FROM_FINAL_BUILD(pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Disable dummy-camera-object", &g_animatedCameraOptions.m_disableDummyCameraObjects)));
	OMIT_FROM_FINAL_BUILD(pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Force Fail-Over", &g_animatedCameraOptions.m_forceFailOver)));
	OMIT_FROM_FINAL_BUILD(pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Disallow Fail-Over", &g_animatedCameraOptions.m_disallowFailOver)));

	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemDivider);

	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Show Animated Camera", &g_debugDrawStateScriptCamera));

	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Show Animated Camera (Local Space)", &g_debugDrawStateScriptCameraLs));

	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemInteger("Animated Scene Camera ID:", pSceneCameraID));

	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Debug Camera Cuts", &g_animOptions.m_debugCameraCuts));
	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Debug Camera Cuts (Verbose)", &g_animOptions.m_debugCameraCutsVerbose));
	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Debug Camera Cuts (Excruciating)", &g_animOptions.m_debugCameraCutsSuperVerbose));

	OMIT_FROM_FINAL_BUILD(pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Show Camera Sources", &g_animatedCameraOptions.m_showSources)));
	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Retire Out-of-Date Camera Sources", &g_animatedCameraOptions.m_retireOldCameraSources));

#if !FINAL_BUILD
	DMENU::ItemInteger* pItemInt = NDI_NEW DMENU::ItemInteger("Only Apply Cameras of Priority (-1 = apply all)", &g_debugOnlyCameraPriority);
	pItemInt->m_value = -1;
	pItemInt->SetRange((int)kCameraAnimationPriorityUnset, (int)kCameraAnimationPriorityCount);
	pSubMenu->PushBackItem(pItemInt);
#endif
}


SCRIPT_FUNC("upright-animated-camera", DcUprightAnimatedCamera)
{
	SettingSetDefault(&g_cameraOptions.m_uprightAnimatedCamera, false);
	SettingSetPers(SID("script"), &g_cameraOptions.m_uprightAnimatedCamera, true, kPlayerModePriority);
	return ScriptValue(0);
}


