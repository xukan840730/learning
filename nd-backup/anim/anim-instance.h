/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/process/bound-frame.h"

class AnimSimpleInstance;
class AnimStateInstance;
class AnimTable;

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimInstance
{
public:
	CREATE_IDENTIFIER_TYPE(ID, U32);

	explicit AnimInstance() {}

	virtual bool IsSimple() const = 0;

	AnimSimpleInstance* ToSimpleInstance()
	{
		return IsSimple() ? reinterpret_cast<AnimSimpleInstance*>(this) : nullptr;
	}
	AnimStateInstance* ToStateInstance() { return !IsSimple() ? reinterpret_cast<AnimStateInstance*>(this) : nullptr; }
	const AnimSimpleInstance* ToSimpleInstance() const
	{
		return IsSimple() ? reinterpret_cast<const AnimSimpleInstance*>(this) : nullptr;
	}
	const AnimStateInstance* ToStateInstance() const
	{
		return !IsSimple() ? reinterpret_cast<const AnimStateInstance*>(this) : nullptr;
	}

	virtual ID GetId() const		 = 0;
	virtual bool IsFlipped() const	 = 0;
	virtual F32 GetMayaFrame() const = 0;
	virtual F32 GetPrevMayaFrame() const = 0;
	virtual F32 GetPhase() const		 = 0;
	virtual F32 GetPrevPhase() const	 = 0;
	virtual U32 GetFrameCount() const	 = 0;
	virtual F32 GetDuration() const		 = 0;

	virtual const AnimTable* GetAnimTable() const = 0;

	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) = 0;

	virtual void SetApOrigin(const BoundFrame& apRef) = 0;
	virtual const BoundFrame& GetApOrigin() const	  = 0;

	virtual bool IsFrozen() const  = 0;
	virtual void SetFrozen(bool f) = 0;

	virtual void SetSkipPhaseUpdateThisFrame(bool f) = 0;

	virtual StringId64 GetLayerId() const = 0;

	virtual float GetFade() const = 0;

	// no, signatures differ
	//virtual void PhaseUpdate(F32 deltaTime);

	// no, signatures differ too much -- fix later if we really need this to be a common API
	//U32F EvaluateChannels(const StringId64* pChannelNames, U32F numChannels, F32 phase, ndanim::JointParams* pOutChannelJoints, bool mirror = false, bool wantRawScale = false, AnimCameraCutInfo* pCameraCutInfo = nullptr) const;
	//U32F EvaluateChannels(const StringId64* pChannelNames, U32F numChannels,            ndanim::JointParams* pOutChannelJoints,                      bool wantRawScale = false, AnimCameraCutInfo* pCameraCutInfo = nullptr, bool forceChannelBlending = false) const;
};

#define INVALID_ANIM_INSTANCE_ID AnimInstance::ID(0)
