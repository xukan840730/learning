
/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef CAMERA_ANIMATION_HANDLE_H
#define CAMERA_ANIMATION_HANDLE_H

#include "gamelib/anim/anim-instance-handle.h"
#include "corelib/containers/list-array.h"

struct CameraAnimationChannelRequest;
struct CameraAnimationChannel;
struct CameraAnimation;
class CameraControlAnimated;

// -----------------------------------------------------------------------------------------------------------------------
// Animated camera priorities. When more than one animation is driving the
// camera simultaneously, the highest priority driver "wins".
// -----------------------------------------------------------------------------------------------------------------------

enum CameraAnimationPriority
{
	kCameraAnimationPriorityUnset = -1,

	kCameraAnimationPriorityDefault,
	kCameraAnimationPriorityNpc,
	kCameraAnimationPriorityPlayer,
	kCameraAnimationPriorityCamera,

	kCameraAnimationPriorityCount
};

// -----------------------------------------------------------------------------------------------------------------------
// Camera animation channel callback. Allows code to decide whether or not
// a particular animation channel from a particular animation state should
// contribute.
// -----------------------------------------------------------------------------------------------------------------------

typedef bool CameraAnimationChannelFilterFunc(const CameraAnimationChannelRequest& request,
	const CameraAnimationChannel& channel,
	StringId64 animStateName);

typedef DC::AnimFlipMode CameraAnimationChannelFlipFunc(const CameraAnimationChannelRequest& request, StringId64 animStateName);

extern DC::AnimFlipMode CameraAnimationChannelFlipFuncNeverFlip(const CameraAnimationChannelRequest& request, StringId64 animStateName);

// -----------------------------------------------------------------------------------------------------------------------
// Handle to camera animation source data
// -----------------------------------------------------------------------------------------------------------------------

class CameraAnimationHandle
{
public:
	explicit CameraAnimationHandle(AnimInstanceHandle hInst = AnimInstanceHandle(), StringId64 ownerId = INVALID_STRING_ID_64, CameraAnimationPriority priority = kCameraAnimationPriorityDefault)
		: m_hInstance(hInst)
		, m_priority(priority)
		, m_ownerId(ownerId)
		, m_lastUpdatedFrameNo(0)
		, m_pFlip(nullptr)
		, m_pFilter(nullptr)
		, m_hFilterContext()
	{
	}

	// convenience constructor for common use-cases
	CameraAnimationHandle(const NdGameObject* pSourceGo, StringId64 ownerId, CameraAnimationPriority priority = kCameraAnimationPriorityDefault, bool sortCameraAnimsByTopInstance = false);

	bool operator == (const CameraAnimationHandle& rhs) const
	{
		return m_hInstance == rhs.m_hInstance && m_priority == rhs.m_priority && m_ownerId == rhs.m_ownerId;
	}
	bool operator != (const CameraAnimationHandle& rhs) const
	{
		return !(*this == rhs);
	}

	static int Compare(const CameraAnimationHandle& hA, const CameraAnimationHandle& hB);

	bool						IsValid() const { return (m_hInstance.GetSimpleInstance() != nullptr) || (m_hInstance.GetStateInstance() != nullptr); }

	const CameraAnimation		GetCameraAnimation(U32 cameraIndex) const;
	bool						IsDormant(U32 cameraIndex, bool *pIsValid = nullptr) const;

	const AnimInstanceHandle&	GetInstanceHandle() const { return m_hInstance; }
	CameraAnimationPriority		GetPriority()		const { return m_priority; }
	StringId64					GetOwnerId()		const { return m_ownerId; }
	bool						IsTopInstance()		const { return m_hInstance.IsTopInstance(); }
	I64							GetAge(I64 frame)	const { return frame - m_lastUpdatedFrameNo; }
	void						DebugPrint()		const;
	void						Update();

private:
	AnimInstanceHandle					m_hInstance;
	CameraAnimationPriority				m_priority;
	StringId64							m_ownerId;
	I64									m_lastUpdatedFrameNo;
public:
	ProcessHandle						m_hFilterContext;
	CameraAnimationChannelFilterFunc*	m_pFilter;
	CameraAnimationChannelFlipFunc*		m_pFlip;
};

// -----------------------------------------------------------------------------------------------------------------------
// A list of CameraAnimationHandles
// -----------------------------------------------------------------------------------------------------------------------

class CameraAnimationHandleList
{
private:
	static const int kCapacity = 16;
	CameraAnimationHandle m_ahAnimSource[kCapacity];
	int m_numAnimSources;
	bool m_overflowWarningPrinted;

public:
	CameraAnimationHandleList()
		: m_numAnimSources(0), m_overflowWarningPrinted(false)
	{
	}

	void Reset()
	{
		m_numAnimSources = 0;
	}

	int Size() const
	{
		return m_numAnimSources;
	}

	bool Equals(const CameraAnimationHandleList& other) const;
	bool Equals(const CameraAnimationHandle& hAnimSource) const;
	bool Contains(const CameraAnimationHandle& hAnimSource) const;
	bool UpdateIfContains(const CameraAnimationHandle& hAnimSource);
	void AddSource(const CameraAnimationHandle& hAnimSource);
	void RemoveInvalidSources();
	void Sort();
	const CameraAnimationHandle& GetSource(int i) const { ALWAYS_ASSERT(i < m_numAnimSources); return m_ahAnimSource[i];}
};

#endif //CAMERA_ANIMATION_HANDLE_H