/*
* Copyright (c) 2018 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "gamelib/gameplay/nd-attachment-ctrl.h"
#include "gamelib/gameplay/nd-game-object.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/process-mgr.h"

extern bool g_drawAttachableObjectParent;
bool g_debugShowAttachableRequest = false;

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachmentCtrl::ParentAttachOffset::Update(float dt, I64 currFN, const Locator& parentAttachLocWs, const Locator& parentLocWs)
{
	if (m_fadeTime > 0.0f)
	{
		if (m_frameNumberUnpaused != currFN)
		{
			// calculate m_sourceOffset
			if (!m_sourceOffsetValid)
			{
				Locator startLocWs = parentLocWs.TransformLocator(m_startLocLs);
				m_sourceOffset = parentAttachLocWs.UntransformLocator(startLocWs);
				m_sourceOffsetValid = true;
			}

			m_fade = Limit01(m_fade + dt / m_fadeTime);

			Locator newSourceOffset = m_sourceOffset;

			if (LengthSqr(m_selfVelWs) > FLT_EPSILON)
			{
				const Vector initVel = m_selfVelWs;
				const Vector fadeOutVel = (1.0f - m_fade) * initVel; // when fade reaches 1.0, self-velocity completely fades out.
				const float timeElapsed = m_fade * m_fadeTime;
				const Vector deltaWs = (initVel + fadeOutVel) / 2.f * timeElapsed; // linearly
				const Vector deltaLs = parentAttachLocWs.UntransformVector(deltaWs);
				newSourceOffset.Move(deltaLs);
			}

			m_offset = Lerp(newSourceOffset, m_desiredOffset, m_fade);

			if (m_fade == 1.0f)
			{
				m_fadeTime = 0.0f;
				m_fade = 0.0f;
			}
		}
	}
	m_frameNumberUnpaused = currFN;
	GAMEPLAY_ASSERT(IsFinite(m_offset));
}

/// --------------------------------------------------------------------------------------------------------------- ///
AttachmentCtrl::AttachmentCtrl(NdGameObject& self)
{
	m_hSelf = self;
	m_hParent = MutableNdGameObjectHandle();
	Process* pOriginalParent = self.GetParentProcess();
	m_pOriginalParentTree	 = (pOriginalParent && !pOriginalParent->IsProcess())
								? pOriginalParent
								: EngineComponents::GetProcessMgr()->m_pDefaultTree; // must be a process-tree.

	m_parentAttachOffset = ParentAttachOffset();
	m_parentAttachIndex = AttachIndex::kInvalid;
	m_attachJointIndex = kInvalidJointIndex;
	m_attached = false;

	m_fadeToAttach = FadeToAttach();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachmentCtrl::AttachToObject(const char* sourceFile,
									U32F sourceLine,
									const char* sourceFunc,
									StringId64 parentObjectId,
									StringId64 parentAttachPoint,
									const Locator& attachOffset,
									StringId64 myJointId,
									bool changeBucketThisFrame)
{
	NdGameObject* pParent = NdGameObject::FromProcess(EngineComponents::GetProcessMgr()->LookupProcessByUserId(parentObjectId));
	return AttachToObject(sourceFile, sourceLine, sourceFunc, pParent, parentAttachPoint, attachOffset, myJointId, changeBucketThisFrame);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachmentCtrl::AttachToObject(const char* sourceFile,
									U32F sourceLine,
									const char* sourceFunc,
									MutableNdGameObjectHandle hParent,
									StringId64 parentAttachPoint,
									const Locator& attachOffset,
									StringId64 myJointId,
									bool changeBucketThisFrame)
{
	NdGameObject* pParent = hParent.ToMutableProcess();
	const NdGameObject* pSelf = m_hSelf.ToProcess();

	if (!pParent || !pSelf)
	{
		return false;
	}

	AttachIndex parentAttachIndex = AttachIndex::kInvalid;
	const AttachSystem* pParentAs = pParent->GetAttachSystem();

	if (pParentAs && parentAttachPoint != INVALID_STRING_ID_64)
	{
		if (!pParentAs->FindPointIndexById(&parentAttachIndex, parentAttachPoint))
		{
			MsgWarn("Failed to find attach point '%s' when trying to attach '%s' of '%s' to '%s'\n",
					DevKitOnly_StringIdToString(parentAttachPoint),
					DevKitOnly_StringIdToString(myJointId),
					pSelf->GetName(),
					pParent->GetName());

			parentAttachIndex = AttachIndex::kInvalid;
		}
	}

	I32 myJointIndex = kInvalidJointIndex;
	if (myJointId != INVALID_STRING_ID_64)
	{
		myJointIndex = pSelf->FindJointIndex(myJointId);

		if (myJointIndex == kInvalidJointIndex)
		{
			MsgConScriptError("Attachable %s has no joint named %s.",
							  DevKitOnly_StringIdToString(pSelf->GetLookId()),
							  DevKitOnly_StringIdToString(myJointId));
		}
	}

	return AttachToObject(sourceFile,
						  sourceLine,
						  sourceFunc,
						  pParent,
						  parentAttachIndex,
						  attachOffset,
						  myJointIndex,
						  changeBucketThisFrame);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachmentCtrl::AttachToObject(const char* sourceFile,
									U32F sourceLine,
									const char* sourceFunc,
									MutableNdGameObjectHandle hParent,
									AttachIndex parentAttachIndex,
									const Locator& attachOffset,
									I32 myJointIndex,
									bool changeBucketThisFrame)
{
	m_hParent = MutableNdGameObjectHandle(hParent);
	if (hParent.HandleValid())
	{
		const NdGameObject* pParent = hParent.ToProcess();
		NdGameObject* pSelf = m_hSelf.ToMutableProcess();
		if (pSelf != nullptr)
		{
			NdGameObject& self = *pSelf;
			m_parentAttachIndex = parentAttachIndex;
			m_attachJointIndex = myJointIndex;
			SetParentAttachOffset(attachOffset);
			m_attached = true;
			m_detachFadeOut.Reset();

			pSelf->OnAttach();

			// figure out my bucket.
			Process* pMyParent = self.GetParentProcess();
			{
				Process* pPendingParent = EngineComponents::GetProcessMgr()->GetPendingParentRequest(self.GetProcessId());
				if (pPendingParent != nullptr)
					pMyParent = pPendingParent;
			}
			ProcessBucket myBucket = EngineComponents::GetProcessMgr()->GetProcessBucket(*pMyParent);

			ProcessBucket parentBucket = EngineComponents::GetProcessMgr()->GetProcessBucket(*pParent);
			GAMEPLAY_ASSERT(parentBucket < kProcessBucketEffect);

			// Attach to at least one bucket later but not before AttachBucket and also not before bucket I'm already in 
			ProcessBucket targetBucket = (ProcessBucket)Max(myBucket, Max(parentBucket+1, kProcessBucketAttach));
			if (changeBucketThisFrame)
			{
				// when an attachable object's spawned, just change it immediately. or it can be updated in default tree and attach tree twice.
				self.ChangeParentProcess(EngineComponents::GetProcessMgr()->m_pBucketSubtree[targetBucket]);
			}
			else
			{
				self.ChangeParentProcessNextFrame(EngineComponents::GetProcessMgr()->m_pBucketSubtree[targetBucket]);
			}

			m_sourceFile = sourceFile;
			m_sourceLine = sourceLine;
			m_sourceFunc = sourceFunc;

			if (FALSE_IN_FINAL_BUILD(g_debugShowAttachableRequest))
			{
				if (DebugSelection::Get().IsProcessSelected(m_hSelf))
				{
					MsgCon("%s AttachToObject, from %s, %d, %s\n", DevKitOnly_StringIdToString(pSelf->GetUserId()), m_sourceFile, m_sourceLine, m_sourceFunc);
				}
			}

			return true;
		}
	}
	else
	{
		Detach();
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachmentCtrl::BlendToAttach(const char* sourceFile,
								   U32F sourceLine,
								   const char* sourceFunc,
								   MutableNdGameObjectHandle hParent,
								   StringId64 parentAttachPoint,
								   const Locator& selfLocWs,
								   const Locator& desiredOffset,
								   float blendTime,
								   bool fadeOutVelWs /* = false */,
								   bool changeBucketThisFrame)
{
	NdGameObject* pParent = hParent.ToMutableProcess();
	const NdGameObject* pSelf = m_hSelf.ToProcess();

	if (!pParent || !pSelf)
	{
		return false;
	}

	AttachIndex parentAttachIndex = AttachIndex::kInvalid;
	const AttachSystem* pParentAs = pParent->GetAttachSystem();

	StringId64 myJointId = INVALID_STRING_ID_64; // TODO:

	if (pParentAs && parentAttachPoint != INVALID_STRING_ID_64)
	{
		if (!pParentAs->FindPointIndexById(&parentAttachIndex, parentAttachPoint))
		{
			MsgWarn("Failed to find attach point '%s' when trying to attach '%s' of '%s' to '%s'\n",
				DevKitOnly_StringIdToString(parentAttachPoint),
				DevKitOnly_StringIdToString(myJointId),
				pSelf->GetName(),
				pParent->GetName());

			parentAttachIndex = AttachIndex::kInvalid;
		}
	}

	I32 myJointIndex = kInvalidJointIndex;
	if (myJointId != INVALID_STRING_ID_64)
	{
		myJointIndex = pSelf->FindJointIndex(myJointId);

		if (myJointIndex == kInvalidJointIndex)
		{
			MsgConScriptError("Attachable %s has no joint named %s.",
				DevKitOnly_StringIdToString(pSelf->GetLookId()),
				DevKitOnly_StringIdToString(myJointId));
		}
	}

	return BlendToAttach(sourceFile,
						 sourceLine,
						 sourceFunc,
						 pParent,
						 parentAttachIndex,
						 selfLocWs,
						 desiredOffset,
						 blendTime,
						 fadeOutVelWs,
						 //myJointIndex, // TODO: not working yet.
						 changeBucketThisFrame);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachmentCtrl::BlendToAttach(const char* sourceFile,
								   U32F sourceLine,
								   const char* sourceFunc,
								   MutableNdGameObjectHandle hParent,
								   AttachIndex parentAttachIndex,
								   const Locator& selfLocWs,
								   const Locator& desiredOffset,
								   float blendTime,
								   bool fadeOutVelWs /* = false */,
								   bool changeBucketThisFrame)
{
	m_hParent = MutableNdGameObjectHandle(hParent);
	if (hParent.HandleValid())
	{
		const NdGameObject* pParent = hParent.ToProcess();
		NdGameObject* pSelf = m_hSelf.ToMutableProcess();
		if (pSelf != nullptr)
		{
			NdGameObject& self = *pSelf;
			m_parentAttachIndex = parentAttachIndex;
			m_attachJointIndex = kInvalidJointIndex; // TODO:
			BlendToParentAttachOffset(selfLocWs, desiredOffset, blendTime, fadeOutVelWs);
			m_attached = true;
			m_detachFadeOut.Reset();

			pSelf->OnAttach();

			// figure out my bucket.
			Process* pMyParent = self.GetParentProcess();
			{
				Process* pPendingParent = EngineComponents::GetProcessMgr()->GetPendingParentRequest(self.GetProcessId());
				if (pPendingParent != nullptr)
					pMyParent = pPendingParent;
			}

			Process* pParentParent = pParent->GetParentProcess();
			Process* pAttachTree = EngineComponents::GetProcessMgr()->m_pAttachTree;
			Process* pSubAttachTree = EngineComponents::GetProcessMgr()->m_pSubAttachTree;
			Process* pEffectTree = EngineComponents::GetProcessMgr()->m_pEffectTree;

			if (pParentParent == pAttachTree && pMyParent != pSubAttachTree)
			{
				if (changeBucketThisFrame)
				{
					// when an attachable object's spawned, just change its to attach tree immediately. or it can be updated in default tree and attach tree twice.
					self.ChangeParentProcess(pSubAttachTree);
				}
				else
				{
					self.ChangeParentProcessNextFrame(pSubAttachTree);
				}
			}
			else if (pParentParent == pSubAttachTree && pMyParent != pEffectTree)
			{
				if (changeBucketThisFrame)
				{
					// when an attachable object's spawned, just change its to attach tree immediately. or it can be updated in default tree and attach tree twice.
					self.ChangeParentProcess(pEffectTree);
				}
				else
				{
					self.ChangeParentProcessNextFrame(pEffectTree);
				}
			}
			else if (pParentParent != pAttachTree && pMyParent != pAttachTree)
			{
				if (changeBucketThisFrame)
				{
					// when an attachable object's spawned, just change its to attach tree immediately. or it can be updated in default tree and attach tree twice.
					self.ChangeParentProcess(pAttachTree);
				}
				else
				{
					self.ChangeParentProcessNextFrame(pAttachTree);
				}
			}

			m_sourceFile = sourceFile;
			m_sourceLine = sourceLine;
			m_sourceFunc = sourceFunc;

			if (FALSE_IN_FINAL_BUILD(g_debugShowAttachableRequest))
			{
				if (DebugSelection::Get().IsProcessSelected(m_hSelf))
				{
					MsgCon("%s BlendToAttach, from %s, %d, %s\n", DevKitOnly_StringIdToString(pSelf->GetUserId()), m_sourceFile, m_sourceLine, m_sourceFunc);
				}
			}

			return true;
		}
	}
	else
	{
		Detach();
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachmentCtrl::ChangeAttachOffset(const char* sourceFile, U32F sourceLine, const char* sourceFunc, 
										const Locator& desiredOffset, 
										float blendTime)
{
	if (m_hParent.HandleValid() && m_attached)
	{
		const NdGameObject* pSelf = m_hSelf.ToMutableProcess();
		if (pSelf != nullptr)
		{
			BlendToParentAttachOffset(desiredOffset, blendTime);

			m_sourceFile = sourceFile;
			m_sourceLine = sourceLine;
			m_sourceFunc = sourceFunc;

			if (FALSE_IN_FINAL_BUILD(g_debugShowAttachableRequest))
			{
				if (DebugSelection::Get().IsProcessSelected(m_hSelf))
				{
					MsgCon("%s ChangeAttachOffset, from %s, %d, %s\n", DevKitOnly_StringIdToString(pSelf->GetUserId()), m_sourceFile, m_sourceLine, m_sourceFunc);
				}
			}

			return true;
		}
	}
	return false;
}

//---------------------------------------------------------------------------------------------------------------//
void AttachmentCtrl::Detach(float fadeOutTime)
{
	Maybe<Locator> currLoc;
	if (fadeOutTime > 0.f)
	{
		Locator testLoc;
		if (DetermineAlignWs(&testLoc))
		{
			currLoc = testLoc;
		}
	}

	m_parentAttachIndex = AttachIndex::kInvalid;
	m_attachJointIndex = kInvalidJointIndex;
	m_attached = false;

	NdGameObject* pSelf = m_hSelf.ToMutableProcess();
	if (pSelf)
	{
		pSelf->OnDetach();

		Process* pOriginalParentTree = m_pOriginalParentTree;
		if (pSelf->GetParentProcess() != pOriginalParentTree)
		{
			pSelf->ChangeParentProcessNextFrame(pOriginalParentTree);
		}
	}

	if (currLoc.Valid())
	{
		m_detachFadeOut.m_startLoc = currLoc.Get();
		m_detachFadeOut.m_startTime = pSelf->GetCurTime();
		m_detachFadeOut.m_fadeOutTime = fadeOutTime;
	}
	else
	{
		m_detachFadeOut.Reset();
	}

	m_hParent = nullptr;
}

//---------------------------------------------------------------------------------------------------------------//
void AttachmentCtrl::AbortDetachFadeOut()
{
	m_detachFadeOut.Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ValidateLocator(const Locator& loc, float range, const char* strDesc)
{
	STRIP_IN_FINAL_BUILD;

	GAMEPLAY_ASSERTF(IsFinite(loc), ("%s not finite", strDesc));
	GAMEPLAY_ASSERTF(LengthSqr(loc.GetTranslation()) < Sqr(range), ("%s translation out of range: %s", strDesc, PrettyPrint(loc.GetTranslation())));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachmentCtrl::SetParentAttachOffset(const Locator& offset)
{
	ValidateLocator(offset, 50000.f, "ParentAttachOffset");

	m_parentAttachOffset.m_offset = offset;
	m_parentAttachOffset.m_desiredOffset = offset;
	m_parentAttachOffset.m_sourceOffset = Locator(kIdentity);
	m_parentAttachOffset.m_sourceOffsetValid = true;
	m_parentAttachOffset.m_startLocLs = Locator(kIdentity);
	m_parentAttachOffset.m_selfVelWs = kZero;
	m_parentAttachOffset.m_fadeTime = 0.0f;
	m_parentAttachOffset.m_fade = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator AttachmentCtrl::GetAttachLocation(const Locator& parentAttachLocWs, bool dontAdvance)
{
	const NdGameObject* pSelf = m_hSelf.ToProcess();
	if (!pSelf)
		return parentAttachLocWs;

	const float dt = GetProcessDeltaTime();
	const I64 currFN = EngineComponents::GetNdFrameState()->m_gameFrameNumberUnpaused;

	if (!dontAdvance)
	{
		const NdGameObject* pParent = m_hParent.ToProcess();
		m_parentAttachOffset.Update(dt, currFN, parentAttachLocWs, pParent?pParent->GetLocator():Locator(kIdentity));
	}

	bool debugDraw = pSelf->NeedDebugParenting() && !dontAdvance;

	if (m_attachJointIndex == kInvalidJointIndex)
	{
		const Locator finalLocWs = parentAttachLocWs.TransformLocator(m_parentAttachOffset.m_offset);

		if (FALSE_IN_FINAL_BUILD(debugDraw))
		{
			DebugDrawParenting(parentAttachLocWs, finalLocWs);
		}

		return finalLocWs;
	}

	const JointCache* pJointCache = &pSelf->GetAnimData()->m_jointCache;

	// Determine the joint-space to model-space transform of the attach joint.
	const ndanim::JointParams& rootJointParams = pJointCache->GetJointParamsLs(0);
	const Locator& rootLoc = pJointCache->GetJointLocatorWs(0);
	const Locator& jointLoc = m_attachJointIndex != kInvalidJointIndex ? pJointCache->GetJointLocatorWs(m_attachJointIndex) : rootLoc;

	ValidateLocator(rootLoc, 50000.0f, "Root Locator");
	ValidateLocator(jointLoc, 50000.0f, "Joint Locator");

	const Locator rootJointLocls = Locator(rootJointParams.m_trans, rootJointParams.m_quat);
	const Locator alignToJointOs = rootJointLocls.TransformLocator(rootLoc.UntransformLocator(jointLoc));

	// We'll offset the world-space attach locator by the inverse of this transform, which
	// is effectively a model-space to joint-space transform.
	const Locator jointToAlignOs = Inverse(alignToJointOs);

	ValidateLocator(m_parentAttachOffset.m_offset, 50000.0f, "ParentAttachOffset");
	const Locator parentAttachToAlign = m_parentAttachOffset.m_offset.TransformLocator(jointToAlignOs);

	// Return the world-space attach location, offset by the parent attach offset and by the
	// location of the attach joint in model space.
	Locator desiredLoc = parentAttachLocWs.TransformLocator(parentAttachToAlign);

	// Make sure the quat is normalized, after all that removing scale, transforming and inverting, it may not be.
	desiredLoc.SetRot(Normalize(desiredLoc.Rot()));

	//g_prim.Draw(DebugCoordAxes(desiredLoc, 1.0f, PrimAttrib(0), 1.0f));

	if (!dontAdvance)
		m_fadeToAttach.Update(dt, currFN);

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		DebugDrawParenting(parentAttachLocWs, desiredLoc);
	}

	if (m_fadeToAttach.m_fade == 0.0f)
	{
		return desiredLoc;
	}
	else
	{
		ValidateLocator(m_fadeToAttach.m_initialLoc.GetLocator(), 50000.0f, "Fade to Attach Init Loc");

		//g_prim.Draw(DebugCoordAxes(m_fadeToAttachInitialLoc.GetLocator(), 0.3f), kPrimDuration1FramePauseable);
		//g_prim.Draw(DebugCross(desiredLoc.GetPosition(), 0.2f, kColorRed, PrimAttrib(0)), kPrimDuration1FramePauseable);

		Locator lerpLoc = Lerp(desiredLoc, m_fadeToAttach.m_initialLoc.GetLocator(), m_fadeToAttach.m_fade);

		// Normalize the quat to ensure the Lerp didn't mess with it.
		lerpLoc.SetRot(Normalize(lerpLoc.Rot()));

		return lerpLoc;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AttachmentCtrl::DetermineAlignWs(Locator* pLocWsOut)
{
	if (m_attached && pLocWsOut)
	{
		const NdGameObject* pParentObj = m_hParent.ToProcess();
		if (pParentObj)
		{
			const AttachSystem* pAttach = pParentObj->GetAttachSystem();

			const Locator parentAttachLoc = (!pAttach || m_parentAttachIndex == AttachIndex::kInvalid)
				? pParentObj->GetLocator()
				: pAttach->GetLocator(m_parentAttachIndex);

			// TRACK DOWN CRASH!
			ALWAYS_ASSERTF(Length(parentAttachLoc.GetTranslation()) < 90000.f,
						   ("attachable: (%s) out of range, parent:(%s), parent-attach-loc: (%s)",
							DevKitOnly_StringIdToString(m_hSelf.GetUserId()),
							DevKitOnly_StringIdToString(m_hParent.GetUserId()),
							PrettyPrint(parentAttachLoc.GetTranslation())));

			ValidateLocator(parentAttachLoc, 50000.0f, "Parent attach");

			const Locator alignWs = GetAttachLocation(parentAttachLoc);

			ValidateLocator(alignWs, 100000.0f, "Final location");

			*pLocWsOut = alignWs;

			return true;
		}
	}
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Locator AttachmentCtrl::CalcParentAttachOffset(const Locator& desiredAlignWs) const
{
	ASSERT(m_attached);
	if (m_attached)
	{
		const NdGameObject* pParentObj = m_hParent.ToProcess();
		if (pParentObj)
		{
			const AttachSystem* pAttach = pParentObj->GetAttachSystem();

			const Locator parentAttachLoc = (!pAttach || m_parentAttachIndex == AttachIndex::kInvalid)
				? pParentObj->GetLocator()
				: pAttach->GetLocator(m_parentAttachIndex);

			ValidateLocator(parentAttachLoc, 50000.0f, "Parent attach");

			return parentAttachLoc.UntransformLocator(desiredAlignWs);
		}
	}

	return Locator(kIdentity);
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AttachmentCtrl::GetParentAttachPoint() const
{
	if (m_parentAttachIndex != AttachIndex::kInvalid)
	{
		const NdGameObject* pParentObj = m_hParent.ToProcess();
		if (pParentObj)
		{
			const AttachSystem* pAttach = pParentObj->GetAttachSystem();
			return pAttach ? pAttach->GetNameId(m_parentAttachIndex) : INVALID_STRING_ID_64;
		}
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 AttachmentCtrl::GetAttachJointName() const
{
	if (m_attachJointIndex != kInvalidJointIndex)
	{
		const NdGameObject* pSelf = m_hSelf.ToProcess();;
		if (pSelf)
		{
			const AnimControl* pAnimCtrl = pSelf->GetAnimControl();
			return pAnimCtrl ? pAnimCtrl->GetJointSid(m_attachJointIndex) : INVALID_STRING_ID_64;
		}
	}

	return INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachmentCtrl::SetFadeToAttach(float blendTime, const BoundFrame& initialLoc)
{
	if (blendTime > 0.0f)
	{
		m_fadeToAttach.m_fade = 1.0f;
		m_fadeToAttach.m_time = blendTime;
		m_fadeToAttach.m_initialLoc = initialLoc;
	}
	else
	{
		m_fadeToAttach.m_fade = 0.0f;
		m_fadeToAttach.m_time = blendTime;
		m_fadeToAttach.m_initialLoc = initialLoc;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachmentCtrl::BlendToParentAttachOffset(const Locator& selfLocWs,
											   const Locator& desiredOffset,
											   float blendTime,
											   bool fadeOutVelWs)
{
	if (blendTime > 0.0f)
	{
		const NdGameObject* pParent = m_hParent.ToProcess();
		m_parentAttachOffset.m_desiredOffset = desiredOffset;
		m_parentAttachOffset.m_sourceOffset = Locator(kIdentity);
		m_parentAttachOffset.m_sourceOffsetValid = false;
		m_parentAttachOffset.m_startLocLs = pParent?pParent->GetLocator().UntransformLocator(selfLocWs):selfLocWs;
		if (fadeOutVelWs)
		{
			const NdGameObject* pSelf = m_hSelf.ToProcess();
			m_parentAttachOffset.m_selfVelWs = pSelf ? pSelf->GetVelocityWs() : kZero;
		}
		else
		{
			m_parentAttachOffset.m_selfVelWs = kZero;
		}
		m_parentAttachOffset.m_fadeTime = blendTime;
		m_parentAttachOffset.m_fade = 0.0f;
	}
	else
	{
		SetParentAttachOffset(desiredOffset);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachmentCtrl::BlendToParentAttachOffset(const Locator& desiredOffset, float blendTime)
{
	if (blendTime > 0.f)
	{
		ValidateLocator(desiredOffset, 50000.f, "ParentAttachOffset");

		const Locator& currOffset = m_parentAttachOffset.m_offset;

		m_parentAttachOffset.m_desiredOffset = desiredOffset;
		m_parentAttachOffset.m_sourceOffset = currOffset;
		m_parentAttachOffset.m_sourceOffsetValid = true;
		m_parentAttachOffset.m_startLocLs = Locator(kIdentity);
		m_parentAttachOffset.m_selfVelWs = kZero;
		m_parentAttachOffset.m_fadeTime = blendTime;
		m_parentAttachOffset.m_fade = 0.0f;
	}
	else
	{
		SetParentAttachOffset(desiredOffset);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachmentCtrl::HandleAttachEvent(Event& event)
{
	NdGameObject* pParentObj = NdGameObject::FromProcess(event.Get(0).GetMutableProcess());
	StringId64 attachPointName = event.Get(1).GetStringId();

	const NdGameObject* pSelf = m_hSelf.ToProcess();

	if (event.GetNumParams() >= 3)
	{
		const Locator parentAttachOffset = event.Get(2).GetLocator();

		if (event.GetNumParams() >= 4)
		{
			float blendTime = event.Get(3).GetFloat();

			if (blendTime > 0.0f)
			{
				const Locator startLoc = pSelf->GetLocator();
				BlendToAttach(FILE_LINE_FUNC, pParentObj, attachPointName, startLoc, parentAttachOffset, blendTime);
			}
			else
			{
				AttachToObject(FILE_LINE_FUNC, pParentObj, attachPointName, parentAttachOffset);
			}
		}
		else
		{
			AttachToObject(FILE_LINE_FUNC, pParentObj, attachPointName, parentAttachOffset);
		}
	}
	else
	{
		AttachToObject(FILE_LINE_FUNC, pParentObj, attachPointName);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachmentCtrl::DebugDrawParenting(const Locator& parentAttachLoc, const Locator& debugDrawLoc) const
{
	STRIP_IN_FINAL_BUILD;

	const NdGameObject* pSelf = m_hSelf.ToProcess();
	if (!pSelf)
		return;

	const NdGameObject* pParentGameObject = m_hParent.ToProcess();
	const AttachSystem* pAttach = pParentGameObject ? pParentGameObject->GetAttachSystem() : nullptr;

	AttachIndex parentAttachIndex = GetParentAttachIndex();
	I32 attachJointIndex = GetAttachJointIndex();

	const char* pDebugName = pSelf->GetDebugParentingName();

	//if (!IsClose(parentAttachLoc, debugDrawLoc, 0.0001f, 0.9999f))
	{
		g_prim.Draw(DebugCoordAxes(parentAttachLoc, 0.15f, PrimAttrib(kPrimEnableHiddenLineAlpha), 5.f));
		g_prim.Draw(DebugLine(parentAttachLoc.GetTranslation(), debugDrawLoc.GetTranslation(), kColorWhite, 1.0f, PrimAttrib(kPrimEnableHiddenLineAlpha)));
	}

	if (!pAttach || parentAttachIndex == AttachIndex::kInvalid)
	{
		if (attachJointIndex != kInvalidJointIndex)
		{
			g_prim.Draw(DebugCoordAxesLabeled(debugDrawLoc,
				StringBuilder<128>("%s (%s) attached to %s's align",
					pDebugName,
					DevKitOnly_StringIdToString(GetAttachJointName()),
					DevKitOnly_StringIdToString(m_hParent.GetUserId())).c_str(),
				0.3f, kPrimEnableHiddenLineAlpha, 2.f, kColorWhite, 0.7f));
		}
		else
		{
			g_prim.Draw(DebugCoordAxesLabeled(debugDrawLoc,
				StringBuilder<128>("%s attached to %s's align",
					pDebugName,
					DevKitOnly_StringIdToString(m_hParent.GetUserId())).c_str(),
				0.3f, kPrimEnableHiddenLineAlpha, 2.f, kColorWhite, 0.7f));
		}
	}
	else
	{
		if (attachJointIndex != kInvalidJointIndex)
		{
			StringId64 attachToJointId = pAttach->GetPointSpec(parentAttachIndex).m_nameId;
			g_prim.Draw(DebugCoordAxesLabeled(debugDrawLoc,
				StringBuilder<128>("%s (%s) attached to %s's %s",
					pDebugName,
					DevKitOnly_StringIdToString(GetAttachJointName()),
					DevKitOnly_StringIdToString(m_hParent.GetUserId()),
					DevKitOnly_StringIdToString(attachToJointId)).c_str(),
				0.3f, kPrimEnableHiddenLineAlpha, 2.f, kColorWhite, 0.7f));
		}
		else
		{
			StringId64 attachToJointId = pAttach->GetPointSpec(parentAttachIndex).m_nameId;
			g_prim.Draw(DebugCoordAxesLabeled(debugDrawLoc,
				StringBuilder<128>("%s attached to %s's %s",
					pDebugName,
					DevKitOnly_StringIdToString(m_hParent.GetUserId()),
					DevKitOnly_StringIdToString(attachToJointId)).c_str(),
				0.3f, kPrimEnableHiddenLineAlpha, 2.f, kColorWhite, 0.7f));
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AttachmentCtrl::DebugPrint() const
{
	STRIP_IN_FINAL_BUILD;

	const NdGameObject* pSelf = m_hSelf.ToProcess();
	if (!pSelf)
		return;

	//const NdGameObject* pParentGameObject = m_hParent.ToProcess();
	//const AttachSystem* pAttach = pParentGameObject ? pParentGameObject->GetAttachSystem() : nullptr;

	MsgCon("AttachmentCtrl: desired-offset:%s, source-offset:%s, current-offset:%s, fade-time:%f, current-fade:%f\n",
		   PrettyPrint(m_parentAttachOffset.m_desiredOffset.GetTranslation()),
		   PrettyPrint(m_parentAttachOffset.m_sourceOffset.GetTranslation()),
		   PrettyPrint(m_parentAttachOffset.m_offset.GetTranslation()),
		   m_parentAttachOffset.m_fadeTime,
		   m_parentAttachOffset.m_fade);
}
