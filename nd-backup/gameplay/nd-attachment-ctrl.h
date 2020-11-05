/*
* Copyright (c) 2018 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include "ndlib/anim/attach-system.h"

class NdGameObject;
FWD_DECL_PROCESS_HANDLE(NdGameObject);

/// --------------------------------------------------------------------------------------------------------------- ///
class AttachmentCtrl
{
public:

	AttachmentCtrl(NdGameObject& self);

	bool AttachToObject(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
						StringId64 parentObjectId,
						StringId64 parentAttachPoint,
						const Locator& attachOffset = Locator(kIdentity),
						StringId64 myJointId = INVALID_STRING_ID_64,						
						bool changeBucketThisFrame = false);
	bool AttachToObject(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
						MutableNdGameObjectHandle hParent,
						StringId64 parentAttachPoint,
						const Locator& attachOffset = Locator(kIdentity),
						StringId64 myJointId = INVALID_STRING_ID_64,						
						bool changeBucketThisFrame = false);
	bool AttachToObject(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
						MutableNdGameObjectHandle hParent,
						AttachIndex parentAttachIndex = AttachIndex::kInvalid,
						const Locator& attachOffset = Locator(kIdentity),
						I32 myJointIndex = kInvalidJointIndex,						
						bool changeBucketThisFrame = false);

	bool BlendToAttach(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
					   MutableNdGameObjectHandle hParent,
					   StringId64 parentAttachPoint,
					   const Locator& selfLocWs,
					   const Locator& desiredOffset,
					   float blendTime,
					   bool fadeOutVelWs = false,
					   bool changeBucketThisFrame = false);

	bool BlendToAttach(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
					   MutableNdGameObjectHandle hParent, 
					   AttachIndex parentAttachIndex,
					   const Locator& selfLocWs, 
					   const Locator& desiredOffset,
					   float blendTime,
					   bool fadeOutVelWs = false,					   
					   bool changeBucketThisFrame = false);

	// assuming attach point doesn't change, blend to new attach-offset
	bool ChangeAttachOffset(const char* sourceFile, U32F sourceLine, const char* sourceFunc,
							const Locator& desiredOffset,
							float blendTime);

	void Detach(float fadeOutTime = 0.f);
	void AbortDetachFadeOut();

	bool IsAttached() const { return m_attached; }
	bool DetermineAlignWs(Locator* pLocWsOut);
	Locator GetAttachLocation(const Locator& parentAttachLocWs, bool dontAdvance = false);
	Locator CalcParentAttachOffset(const Locator& desiredAlignWs) const;

	struct DetachFadeOut
	{
		void Reset()
		{
			m_startLoc = kIdentity;
			m_startTime = TimeFrameZero();
			m_fadeOutTime = 0.f;
		}

		Locator			m_startLoc = kIdentity;
		TimeFrame		m_startTime;
		float			m_fadeOutTime = 0.f;
	};
	const DetachFadeOut& GetDetachFadeOut() const { return m_detachFadeOut; }

	NdGameObjectHandle GetParent() const { return m_hParent; }
	MutableNdGameObjectHandle GetParentMutable() const { return m_hParent; }

	void SetParentAttachOffset(const Locator& offset);
	Locator GetParentAttachOffset() const { return m_parentAttachOffset.m_offset; }
	StringId64 GetParentAttachPoint() const;
	AttachIndex GetParentAttachIndex() const { return m_parentAttachIndex; }

	float GetParentAttachOffsetBlendTime() const { return m_parentAttachOffset.m_fadeTime; }
	Locator GetDesiredParentAttachOffset() const { return m_parentAttachOffset.m_desiredOffset; }

	I32 GetAttachJointIndex() const { return m_attachJointIndex; }
	StringId64 GetAttachJointName() const;

	void SetFadeToAttach(float attachTime, const BoundFrame& initialLoc);
	float GetFadeToAttach() const { return m_fadeToAttach.m_fade; }

	void HandleAttachEvent(Event& event);

	//----------------------------------------------------------------------------------------//
	// DEBUGGING!
	//----------------------------------------------------------------------------------------//
	void DebugDrawParenting(const Locator& parentAttachLoc, const Locator& debugDrawLoc) const;
	void DebugPrint() const;

private:

	// fadeOutVelWs: we might need take my world-space velocity into account if it's big.
	void BlendToParentAttachOffset(const Locator& selfLocWs, const Locator& desiredOffset, float blendTime, bool fadeOutVelWs = false);

	// this func assume attach-point doesn't change.
	void BlendToParentAttachOffset(const Locator& desiredOffset, float blendTime);

	MutableNdGameObjectHandle m_hSelf;
	MutableNdGameObjectHandle m_hParent;
	// the parent is either default tree, or parent tree, and it shouldn't relocate
	// we can't use ProcessHandle, if the parent is a tree node, HandleValid() won't work
	Process*			m_pOriginalParentTree;
	AttachIndex			m_parentAttachIndex; // parent attach-point index.
	I32					m_attachJointIndex;	// my joint index. if valid, parent this joint to parent's attach-point
	bool				m_attached = false;

	const char*			m_sourceFile = nullptr;
	U32F				m_sourceLine = 0;
	const char*			m_sourceFunc = nullptr;

	struct FadeToAttach
	{
		FadeToAttach()
			: m_fade(0.0f)
			, m_time(0.0f)
			, m_initialLoc(BoundFrame(kIdentity))
			, m_frameNumberUnpaused(-1)
		{}

		void Update(float dt, I64 currFN)
		{
			if (m_fade > 0.0f)
			{
				if (m_frameNumberUnpaused != currFN)
				{
					GAMEPLAY_ASSERT(m_time != 0.0f);
					m_fade = Max(m_fade - dt / m_time, 0.0f);
				}
			}
			m_frameNumberUnpaused = currFN;
		}

		F32				m_fade;
		F32				m_time;
		BoundFrame		m_initialLoc;
		I64				m_frameNumberUnpaused; // only update once per frame// only update once per frame
	};
	FadeToAttach m_fadeToAttach;

	struct ParentAttachOffset
	{
		ParentAttachOffset()
			: m_offset(Locator(kIdentity))
			, m_sourceOffsetValid(false)
			, m_desiredOffset(Locator(kIdentity))
			, m_sourceOffset(Locator(kIdentity))
			, m_startLocLs(Locator(kIdentity))
			, m_selfVelWs(kZero)
			, m_fadeTime(0.0f)
			, m_fade(0.0f)
			, m_frameNumberUnpaused(-1)
		{}

		void Update(float dt, I64 currFN, const Locator& parentAttachLocWs, const Locator& parentLocWs);

		Locator			m_offset;

		// these are for offset blending.
		bool			m_sourceOffsetValid;
		Locator			m_desiredOffset;
		Locator			m_sourceOffset;
		Locator			m_startLocLs;
		Vector			m_selfVelWs;			// my world-space velocity when blend starts
		float			m_fadeTime;
		float			m_fade;
		I64				m_frameNumberUnpaused;	// only update once per frame
	};
	ParentAttachOffset	m_parentAttachOffset;

	DetachFadeOut m_detachFadeOut;
};
