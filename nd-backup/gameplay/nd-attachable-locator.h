/*
* Copyright (c) 2018 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "ndlib/process/process.h"
#include "ndlib/process/bound-frame.h"

//--------------------------------------------------------------------------------------//
// AttachableLocator: it could be either boundframe, or attach-point on object
//--------------------------------------------------------------------------------------//
struct AttachableLocator
{
	AttachableLocator()
		: m_boundFrameValid(false)
		, m_boundFrame(BoundFrame(kIdentity))
		, m_hObj(MutableNdGameObjectHandle())
		, m_objAttachPoint(INVALID_STRING_ID_64)
	{}

	AttachableLocator(const BoundFrame& bf)
		: m_boundFrameValid(true)
		, m_boundFrame(bf)
		, m_hObj(MutableNdGameObjectHandle())
		, m_objAttachPoint(INVALID_STRING_ID_64)
	{}

	AttachableLocator(MutableNdGameObjectHandle hTargetObj, StringId64 targetAttachPoint)
		: m_boundFrameValid(false)
		, m_boundFrame(BoundFrame(kIdentity))
		, m_hObj(hTargetObj)
		, m_objAttachPoint(targetAttachPoint)
	{}

	void Clear()
	{
		m_boundFrameValid = false;
		m_boundFrame = BoundFrame(kIdentity);
		m_hObj = MutableNdGameObjectHandle();
		m_objAttachPoint = INVALID_STRING_ID_64;
	}

	bool Valid() const { return m_hObj.HandleValid() || m_boundFrameValid; }

	void SetTargetObject(MutableNdGameObjectHandle hTargetObj, StringId64 attachPoint = INVALID_STRING_ID_64)
	{
		m_hObj = hTargetObj;
		m_objAttachPoint = attachPoint;
	}
	void SetTargetBoundFrame(const BoundFrame& bf) { m_boundFrame = bf; m_boundFrameValid = true; }

	Locator GetLocator() const;

	bool m_boundFrameValid;
	BoundFrame m_boundFrame;
	MutableNdGameObjectHandle m_hObj;
	StringId64 m_objAttachPoint;
};
