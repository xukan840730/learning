/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */
#include "gamelib/camera/camera-animation-handle.h"
#include "gamelib/camera/camera-animated.h"
#include "corelib/util/bigsort.h"
#include "ndlib/nd-frame-state.h"

//CameraAnimationSource

/// --------------------------------------------------------------------------------------------------------------- ///
CameraAnimationHandle::CameraAnimationHandle(const NdGameObject* pSourceGo, StringId64 ownerId, CameraAnimationPriority priority, bool sortCameraAnimsByTopInstance)
{
	//m_hInstance = AnimInstanceHandle();
	m_priority = kCameraAnimationPriorityDefault;
	m_ownerId = INVALID_STRING_ID_64;
	m_pFlip = nullptr;
	m_pFilter = nullptr;
	//m_hFilterContext = ProcessHandle();

	// NB: the priority only matters when there are multiple anim sources driving the camera

	if (pSourceGo)
	{
		const AnimControl* pAnimControl = pSourceGo->GetAnimControl();
		if (pAnimControl)
		{
			// This function covers the most common use case. If you want something custom, just write it yourself.
			const AnimStateLayer* pStateLayer = pAnimControl->GetBaseStateLayer();
			const AnimSimpleLayer* pSimpleLayer = pAnimControl->GetSimpleLayerById(SID("base"));

			if (pStateLayer)
			{
				const AnimStateInstance* pInst = pStateLayer->CurrentStateInstance();
				m_hInstance = AnimInstanceHandle(pSourceGo, pStateLayer, pInst, sortCameraAnimsByTopInstance);
				m_priority = priority;
				m_ownerId = ownerId;
			}
			else if (pSimpleLayer)
			{
				const AnimSimpleInstance* pSimpleInstance = pSimpleLayer->CurrentInstance();
				m_hInstance = AnimInstanceHandle(pSourceGo, pSimpleLayer, pSimpleInstance, sortCameraAnimsByTopInstance);
				m_priority = priority;
				m_ownerId = ownerId;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
const CameraAnimation CameraAnimationHandle::GetCameraAnimation(U32 cameraIndex) const
{
	CameraAnimation result;

	const AnimStateInstance* pInstance = m_hInstance.GetStateInstance();
	const AnimSimpleInstance* pSimpleInstance = m_hInstance.GetSimpleInstance();

	if (pInstance != nullptr || pSimpleInstance != nullptr)
	{
		const NdGameObject* pGameObject = m_hInstance.GetGameObject();

		CameraAnimationRequest request;
		request.frame = pGameObject->GetBoundFrame();
		request.pAnimStateInstance = pInstance;
		request.pAnimSimpleInstance = pSimpleInstance;
		request.cameraIndex = cameraIndex;
		request.pFilter = m_pFilter;
		request.pFilterContext = m_hFilterContext.ToProcess();
		request.pFlip = m_pFlip;

		::GetCameraAnimation(request, result);
	}

	return result;
}

DC::AnimFlipMode CameraAnimationChannelFlipFuncNeverFlip(const CameraAnimationChannelRequest& request, StringId64 animStateName)
{
	return DC::kAnimFlipModeNever;
}

void CameraAnimationHandle::Update()
{
	m_lastUpdatedFrameNo = EngineComponents::GetNdFrameState()->m_gameFrameNumber;
}

bool CameraAnimationHandle::IsDormant(U32 cameraIndex, bool *pIsValid) const
{
	const AnimStateInstance* pInstance = m_hInstance.GetStateInstance();
	const AnimSimpleInstance* pSimpleInstance = m_hInstance.GetSimpleInstance();

	if (pIsValid)
		*pIsValid = false;

	if (pInstance != nullptr || pSimpleInstance != nullptr)
	{
		const NdGameObject* pGameObject = m_hInstance.GetGameObject();

		CameraAnimationChannelRequest extraRequest;
		extraRequest.cameraIndex = cameraIndex;
		extraRequest.pAnimControl = pGameObject->GetAnimControl();
		extraRequest.pAnimStateInstance = pInstance;
		extraRequest.pAnimSimpleInstance = pSimpleInstance;
		extraRequest.baseAlignLoc = Locator(kIdentity);
		extraRequest.channelId = StringId64ConcatInteger(SID("apReference-extra"), cameraIndex + 1);
		extraRequest.returnLocalSpace = true;
		extraRequest.pFilter = nullptr; //m_pFilter;
		extraRequest.pFilterContext = nullptr; //m_hFilterContext.ToProcess()
		extraRequest.pFlip = CameraAnimationChannelFlipFuncNeverFlip;			// never flip this channel. it makes no sense
		extraRequest.blendActiveChannel = false;

		CameraAnimationChannel extraChannel;
		const bool extraChannelValid = GetCameraAnimationChannel(extraRequest, extraChannel);
		if (extraChannelValid)
		{
			if (pIsValid)
				*pIsValid = true;

			// extract the active and slow mo data
			float activeChannel = Abs(extraChannel.sqt.m_trans.X());
			if (activeChannel < ANIMATED_CAMERA_ENABLE_THRESHOLD)
				return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
int CameraAnimationHandle::Compare(const CameraAnimationHandle& hA, const CameraAnimationHandle& hB)
{
	const bool aTop = hA.IsTopInstance();
	const bool bTop = hB.IsTopInstance();

	const int aNetPriority = ((aTop ? 1 : 0) << 30) | ((hA.m_priority & 0x3) << 28) | (int)(hA.m_lastUpdatedFrameNo & 0x0FFFFFFFLL);
	const int bNetPriority = ((bTop ? 1 : 0) << 30) | ((hB.m_priority & 0x3) << 28) | (int)(hB.m_lastUpdatedFrameNo & 0x0FFFFFFFLL);

	const int diff = aNetPriority - bNetPriority;

	return diff;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraAnimationHandle::DebugPrint() const
{
	STRIP_IN_FINAL_BUILD;
	const char* priorities[] = { "Default", "Npc", "Player", "Camera" };
	ALWAYS_ASSERT(ARRAY_COUNT(priorities) == ((int)kCameraAnimationPriorityCount));
	const char* stateName = "Invalid";
	U32 id = 0;
	if (m_hInstance.GetStateInstance() != nullptr)
	{
		stateName = DevKitOnly_StringIdToString(m_hInstance.GetStateInstance()->GetStateName());
		id = m_hInstance.GetStateInstance()->GetId().GetValue();
	}

	MsgConPauseable("Object: %s, Priority: %s state: %s ownerId: %s inst-id: %i last-updated: %lld\n",
					m_hInstance.GetGameObject()->GetName(),
					priorities[GetPriority()],
					stateName,
					DevKitOnly_StringIdToString(GetOwnerId()),
					id,
					m_lastUpdatedFrameNo);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraAnimationHandleList::AddSource( const CameraAnimationHandle& hAnimSource )
{
	if (m_numAnimSources < kCapacity)
	{
		m_ahAnimSource[m_numAnimSources] = hAnimSource;
		m_ahAnimSource[m_numAnimSources].Update();
		m_numAnimSources++;
	}
	else if (!m_overflowWarningPrinted)
	{
		MsgConScriptError("WARNING: Too many objects are sending camera animations each frame.\n");
		m_overflowWarningPrinted = true;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CameraAnimationHandleList::Contains( const CameraAnimationHandle& hAnimSource ) const
{
	for (int iAnim = 0; iAnim < m_numAnimSources; ++iAnim)
	{
		if (m_ahAnimSource[iAnim] == hAnimSource)
			return true;
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CameraAnimationHandleList::UpdateIfContains( const CameraAnimationHandle& hAnimSource )
{
	for (int iAnim = 0; iAnim < m_numAnimSources; ++iAnim)
	{
		if (m_ahAnimSource[iAnim] == hAnimSource)
		{
			m_ahAnimSource[iAnim].Update();
			return true;
		}
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CameraAnimationHandleList::Equals( const CameraAnimationHandle& hAnimSource ) const
{
	return (m_numAnimSources == 1 && m_ahAnimSource[0] == hAnimSource);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool CameraAnimationHandleList::Equals( const CameraAnimationHandleList& other ) const
{
	for (int iAnim = 0; iAnim < m_numAnimSources; ++iAnim)
	{
		//Ignore invalid anims
		if (m_ahAnimSource[iAnim].IsValid()
		&&  !other.Contains(m_ahAnimSource[iAnim]))
		{
			return false;
		}
	}
	for (int iAnim = 0; iAnim < other.m_numAnimSources; ++iAnim)
	{
		//Ignore invalid anims
		if (other.m_ahAnimSource[iAnim].IsValid()
		&&  !Contains(other.m_ahAnimSource[iAnim]))
		{
			return false;
		}
	}
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraAnimationHandleList::RemoveInvalidSources()
{
	const I64 gameFrameNumber = EngineComponents::GetNdFrameState()->m_gameFrameNumber;
	for (int i = m_numAnimSources - 1; i >= 0; --i)
	{
		const I64 framesSinceUpdate = m_ahAnimSource[i].GetAge(gameFrameNumber);
		if (!m_ahAnimSource[i].IsValid() || (g_animatedCameraOptions.m_retireOldCameraSources && framesSinceUpdate > 1))
		{
			m_ahAnimSource[i] = m_ahAnimSource[m_numAnimSources - 1];
			m_numAnimSources--;
		}
	}
	m_overflowWarningPrinted = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void CameraAnimationHandleList::Sort()
{
	QuickSort(m_ahAnimSource, m_numAnimSources, CameraAnimationHandle::Compare);
}
