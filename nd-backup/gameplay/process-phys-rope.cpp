/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/process-phys-rope.h"

#include "corelib/containers/list-array.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/bigsort.h"

#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-mgr.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/anim-snapshot-node.h"
#include "ndlib/anim/attach-system.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/fx/fxmgr.h"
#include "ndlib/io/joypad.h"
#include "ndlib/nd-config.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/ndphys/pat.h"
#include "ndlib/ndphys/rigid-body-base.h"
#include "ndlib/net/nd-net-info.h"
#include "ndlib/process/debug-selection.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/process/process.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/draw-control.h"
#include "ndlib/render/interface/fg-geometry.h"
#include "ndlib/render/interface/fg-instance.h"
#include "ndlib/render/ngen/mesh.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/render/render-mgr.h"
#include "ndlib/render/scene-window.h"
#include "ndlib/render/look.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/scriptx/h/render-settings-defines.h"
#include "ndlib/tools-shared/patdefs.h"

#include "gamelib/camera/camera-manager.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/gameplay/nd-effect-control.h"
#include "gamelib/ndphys/composite-body.h"
#include "gamelib/ndphys/havok-collision-cast.h"
#include "gamelib/ndphys/havok-internal.h"
#include "gamelib/ndphys/havok-rope-constraint.h"
#include "gamelib/ndphys/havok-util.h"
#include "gamelib/ndphys/havok.h"
#include "gamelib/ndphys/rigid-body.h"
#include "gamelib/ndphys/rope/rope-mgr.h"
#include "gamelib/ndphys/rope/rope2-col.h"
#include "gamelib/ndphys/rope/rope2.h"
#include "gamelib/spline/catmull-rom.h"
#include "gamelib/tasks/nd-task-manager.h"
#include "gamelib/scriptx/h/phys-ropes-defines.h"

#include <Physics/Physics/Collide/Shape/Convex/Capsule/hknpCapsuleShape.h>

class EffectAnimInfo;
class ResolvedLook;
class hkpRigidBody;

F32 const kHandSlideSpeed = 2.0f;
static F32 g_mouseGrabDist;

F32 g_windUpdateDist = 3.0f;
bool g_debugDrawEffPin = false;
extern bool g_checkPatWinchAttachable;

bool g_printCharPointsOnRope = false;

/// --------------------------------------------------------------------------------------------------------------- ///
static const StringId64 s_ropeChannelNames[] =
{
	SID("Rope.RightHandOn"),
	SID("Rope.LeftHandOn"),
	SID("Rope.RightHandFirst"),
	SID("Rope.Slack"),
	SID("Rope.SlackBetweenHands"),
	SID("Rope.SlackToPin"),
	SID("Rope.LeftThighOn"),
	SID("Rope.LeftFootOn")
};
static const size_t kNumRopeChannels = ARRAY_COUNT(s_ropeChannelNames);

/// --------------------------------------------------------------------------------------------------------------- ///
struct RopeChannelsValue
{
	RopeChannelsValue()
		: m_value(0.0f)
		, m_blend(0.0f)
	{}

	F32 m_value;
	F32 m_blend;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct RopeChannels
{
	RopeChannelsValue m_channels[kNumRopeChannels];
};

/// --------------------------------------------------------------------------------------------------------------- ///
class RopeChannelsBlender : public AnimStateLayer::InstanceBlender<RopeChannels>
{
public:
	RopeChannelsBlender()
	{
	}

	virtual RopeChannels GetDefaultData() const override { return RopeChannels(); }

	virtual bool GetDataForInstance(const AnimStateInstance* pInstance, RopeChannels* pDataOut) override
	{
		F32 result[kNumRopeChannels];
		U32 channelsEvaluated = pInstance->EvaluateFloatChannels(s_ropeChannelNames, kNumRopeChannels, result);

		bool flipped = pInstance->IsFlipped();
		if (!flipped)
		{
			// @@JS This is a bad hack. We need to check that individually for each node in the tree and flip the channels correctly before blending them through the tree
			// This is just a quick hack fix for the demo
			if (const AnimSnapshotNode* pAnimNode = pInstance->GetRootSnapshotNode()->FindFirstNodeOfKind(&pInstance->GetAnimStateSnapshot(), SID("AnimSnapshotNodeAnimation")))
			{
				flipped = pAnimNode->IsFlipped();
			}
		}
		if (flipped)
		{
			Swap(result[0], result[1]);
			result[2] = 1.0f - result[2];
		}

		for (U32F ii = 0; ii<kNumRopeChannels; ii++)
		{
			pDataOut->m_channels[ii].m_value = result[ii];
			pDataOut->m_channels[ii].m_blend = (channelsEvaluated & (1U << ii)) ? 1.0f : 0.0f;
		}

		return true;
	}

	virtual RopeChannels BlendData(const RopeChannels& leftData, const RopeChannels& rightData, float masterFade, float animFade, float motionFade) override
	{
		RopeChannels result;
		for (U32F ii = 0; ii<kNumRopeChannels; ii++)
		{
			F32 leftBlend = leftData.m_channels[ii].m_blend * (1.0f - animFade);
			F32 rightBlend = rightData.m_channels[ii].m_blend * animFade;
			F32 blend = leftBlend + rightBlend;
			result.m_channels[ii].m_value = blend == 0.0f ? 0.0f : (leftBlend * leftData.m_channels[ii].m_value + rightBlend * rightData.m_channels[ii].m_value) / blend;
			result.m_channels[ii].m_blend = blend;
		}
		return result;
	}

	RopeChannels BlendDataInIfPresent(const RopeChannels& leftData, const RopeChannels& rightData, float masterFade, float animFade, float motionFade)
	{
		RopeChannels result;
		for (U32F ii = 0; ii<kNumRopeChannels; ii++)
		{
			F32 rightBlend = rightData.m_channels[ii].m_blend * animFade;
			result.m_channels[ii].m_value = Lerp(leftData.m_channels[ii].m_value, rightData.m_channels[ii].m_value, rightBlend);
			result.m_channels[ii].m_blend = leftData.m_channels[ii].m_blend + rightBlend * (1.0f - leftData.m_channels[ii].m_blend);
		}
		return result;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessRopeCollider::Reset(const FgAnimData* pAnimData, StringId64 jointSid)
{
	m_joint = pAnimData->FindJoint(jointSid);
	ASSERT(m_joint >= 0);
	Locator jointLoc = m_joint >= 0 ? pAnimData->m_jointCache.GetJointLocatorWs(m_joint) : Locator(kIdentity);
	m_collider.ResetLoc(jointLoc.TransformLocator(m_locLs));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessRopeCollider::Update(const FgAnimData* pAnimData, bool teleport)
{
	Locator jointLoc = m_joint >= 0 ? pAnimData->m_jointCache.GetJointLocatorWs(m_joint) : Locator(kIdentity);
	jointLoc.SetRotation(Normalize(jointLoc.GetRotation()));
	m_collider.UpdateLoc(jointLoc.TransformLocator(m_locLs), teleport, HavokGetInvDeltaTime());
}

PROCESS_REGISTER_ALLOC_SIZE(ProcessPhysRope, NdDrawableObject, 512 * 1024);
PROCESS_REGISTER_ALIAS_ALLOC_SIZE(ProcessPhysRopeLarge, ProcessPhysRope, 1536 * 1024); // supports the typical setup of up to 14m long rope with 0.056 sim segments
STATE_REGISTER(ProcessPhysRope, Active, kPriorityNormal);

FROM_PROCESS_DEFINE(ProcessPhysRope);

/// --------------------------------------------------------------------------------------------------------------- ///
ProcessPhysRope::ProcessPhysRope()
	: m_pRopeMeshInfo(nullptr)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err ProcessPhysRope::Init(const ProcessSpawnInfo& spawn)
{
	m_draggedRopeDist = -1.0f;
	m_flags = 0;
	m_tempLayerMaskTimeout = 0.0f;
	m_blendToGrab = 0.0f;
	m_bAttached = true;
	m_bInited = false;
	m_usedLength = -1.0f;
	m_hCharacter = nullptr;
	m_windFactor = 0.0f;
	m_physBlend = 1.0f;
	m_desiredPhysBlend = 1.0f;
	m_physBlendTime = 0.0f;
	m_pStepRopeCounter = nullptr;
	m_pJointRopeDist = nullptr;
	m_bEdgesKeyframed = false;
	m_bEnableAsyncRopeStep = true;
	m_dampingWhenNoCharacter = 0.1f;
	m_pRopeBonesData = nullptr;
	m_prevObjXform = GetLocator().AsTransform().GetMat44();
	m_grabRopeDistSet = false;

	m_anchorEdgeIndex = -1;
	m_handEdgeIndex = -1;
	m_straightDistToAnchor = 0.0f;

	m_reelInTime = 0.0f;

	m_pRopeConstraint = nullptr;

	m_bUsable = !spawn.GetData<bool>(SID("rope-start-unusable"), false);
	m_bScriptUsable = true;
	F32 ropeRadius = spawn.GetData<F32>(SID("rope-radius"), -1.0f);
	F32 ropeLength = spawn.GetData<F32>(SID("rope-length"), 0.0f);
	F32 segmentLength = spawn.GetData<F32>(SID("rope-sim-segment"), 0.0f);
	F32 bendingStiffness = spawn.GetData<F32>(SID("rope-bending-stiffness"), -1.0f);
	m_windFactor = spawn.GetData<F32>(SID("rope-wind-factor"), 0.0f);
	bool spawnInPoint = spawn.GetData<bool>(SID("rope-spawn-in-point"), false);
	bool stretchSkinning = spawn.GetData<bool>(SID("rope-stretch-skinning"), false);
	bool buoyancy = spawn.GetData<bool>(SID("rope-buoyancy"), false);
	m_allowSwingTutorial = spawn.GetData<bool>(SID("allow-swing-tutorial"), false);

	bool spawnInAnimPose = spawn.GetData<bool>(SID("rope-spawn-in-anim-pose"), false);
	if (spawnInAnimPose)
	{
		m_physBlend = 0.0f;
		m_physBlendTime = 0.001f;
	}

	// Put into the "attach" tree so that ropes get updated after the player and NPCs.
	// This enables rope grabs to look correct (i.e. not be one frame off).
	ProcessBucket oldBucket = EngineComponents::GetProcessMgr()->GetProcessBucket(*this);
	if (oldBucket < kProcessBucketAttach)
	{
		ChangeParentProcess( EngineComponents::GetProcessMgr()->m_pAttachTree );
	}

	Err result = NdDrawableObject::Init(spawn);
	if (!result.Succeeded())
	{
		return result;
	}

	m_probeJointIndex = 0;

	if (GetDrawControl()->GetNumMeshes() > 0)
	{
		FgInstance* pFgInst = GetDrawControl()->GetMesh(0).m_lod[0].m_pInstance;
		if (pFgInst && pFgInst->m_pFgGeometry)
		{
			m_probeJointIndex = pFgInst->m_pFgGeometry->GetProbeJointIndex();
		}
	}

	ChangeAnimConfig(FgAnimData::kAnimConfigComplexAnimation);
	GetAnimData()->m_userAnimationPassCallback[1] = CustomAnimPass1;

	const JointCache& jointCache = GetAnimData()->m_jointCache;

	m_firstJoint = 0;
	m_lastJoint = jointCache.GetNumAnimatedJoints()-1;

	bool bNeverStrained = false;
	bool bAllowStretchConstraints = true;
	bool bWithSavePos = true;

	RopeInfo* pInfo = (RopeInfo*)spawn.m_pUserData;
	if (pInfo)
	{
		if (pInfo->m_firstJointId != INVALID_STRING_ID_64)
		{
			I32 iJoint = GetAnimData()->FindJoint(pInfo->m_firstJointId);
			ASSERT(iJoint >= 0);
			if (iJoint >= 0)
			{
				m_firstJoint = iJoint;
			}
		}
		if (pInfo->m_lastJointId != INVALID_STRING_ID_64)
		{
			I32 iJoint = GetAnimData()->FindJoint(pInfo->m_lastJointId);
			ASSERT(iJoint >= 0 && iJoint > m_firstJoint);
			if (iJoint >= 0 && iJoint > m_firstJoint)
			{
				m_lastJoint = iJoint;
			}
		}

		if (pInfo->m_radius > 0.0f)
			ropeRadius = pInfo->m_radius;

		if (pInfo->m_length > 0.0f)
			ropeLength = pInfo->m_length;

		if (pInfo->m_segmentLength > 0.0f)
			segmentLength = pInfo->m_segmentLength;

		buoyancy = pInfo->m_bBuoyancy;

		bNeverStrained = pInfo->m_bNeverStrained;
		bWithSavePos = pInfo->m_bWithSavePos;
	}

	U32F numJoints = m_lastJoint - m_firstJoint + 1;
	PHYSICS_ASSERT(numJoints > 1);

	m_pJointRopeDist = NDI_NEW F32[numJoints];

	{
		F32 bindPoseLength = 0.0f;
		Transform xfm;
		jointCache.GetBindPoseJointXform(xfm, m_firstJoint);
		Point lastPos = xfm.GetTranslation();
		m_pJointRopeDist[0] = 0.0f;
		for (U32F ii = m_firstJoint + 1; ii<=m_lastJoint; ii++)
		{
			jointCache.GetBindPoseJointXform(xfm, ii);
			Point pos = xfm.GetTranslation();
			bindPoseLength += Dist(pos, lastPos);
			m_pJointRopeDist[ii-m_firstJoint] = bindPoseLength;
			lastPos = pos;
		}

		// Deal with explicitly set length
		if (ropeLength > 0.0f)
		{
			if (stretchSkinning || ropeLength >= bindPoseLength)
			{
				F32 bindStretch = ropeLength/bindPoseLength;
				for (U32F ii = m_firstJoint + 1; ii<=m_lastJoint; ii++)
				{
					m_pJointRopeDist[ii-m_firstJoint] *= bindStretch;
				}
			}
			else
			{
				// Rope is set to be shorter
				U32F ii;
				for (ii = m_firstJoint + 1; ii <= m_lastJoint; ii++)
				{
					if (m_pJointRopeDist[ii - m_firstJoint] > ropeLength)
					{
						break;
					}
				}
				if (m_pDrawControl->GetFlag(IDrawControl::kRopeSkinning))
				{
					// Rope skinning -> we can just ignore all the remaining joints
					m_lastJoint = ii-1;
				}
				else
				{
					// Normal skinning -> make sure joint rope dist is within rope length
					for (; ii <= m_lastJoint; ii++)
					{
						m_pJointRopeDist[ii - m_firstJoint] = ropeLength;
					}
					// And set used length for the joint scaling trick
					m_usedLength = ropeLength - 0.001f;
				}
			}
		}
		else
		{
			ropeLength = bindPoseLength;
		}

		if (segmentLength <= 0.0f)
			segmentLength = bindPoseLength / (F32)(numJoints - 1);
	}

	if (ropeRadius < 0.0f)
	{
		ropeRadius = kRopeDefaultRadius; //m_pRopeMeshInfo ? m_pRopeMeshInfo->m_meshRadius : kRopeDefaultRadius;
	}

	{
		Rope2InitData initData;
		initData.m_length = ropeLength;
		initData.m_radius = ropeRadius;
		initData.m_segmentLength = segmentLength;
		initData.m_neverStrained = bNeverStrained;
		initData.m_withSavePos = bWithSavePos;
		initData.m_enableSolverFriction = pInfo && pInfo->m_bEnableSolverFriction;
		initData.m_enablePostSolve = pInfo && pInfo->m_bEnablePostSolve;
		initData.m_useTwistDir = pInfo && pInfo->m_bUseTwistDir;
		if (pInfo && pInfo->m_minSegmentFraction > 0.0f)
			initData.m_minSegmentFraction =  pInfo->m_minSegmentFraction;
		m_rope.Init(initData);
	}

	m_rope.m_pOwner = this;
	m_rope.m_fStrainedCollisionOffset = m_pRopeMeshInfo ? Min(ropeRadius, m_pRopeMeshInfo->m_meshRadius) : ropeRadius;

	if (bendingStiffness >= 0.0f)
		m_rope.m_fBendingStiffness = bendingStiffness;
	m_rope.m_bBuoyancy = buoyancy;

	if (!m_pDrawControl->GetFlag(IDrawControl::kRopeSkinning))
	{
		// For now never go on GPU if we are not rope skinned because we would need to wait for the GPU result synchronously. Not good.
		m_rope.SetUseGpu(false);
	}

	if (pInfo)
	{
		if (pInfo->m_bWithExternSavePos)
			m_rope.InitExternSaveEdges();

		// Convert sound ids using audio mesh config
		if (const IEffectControl* pRopeEffCtrl = GetEffectControl())
		{
			pInfo->m_soundDef.m_hitSound = pRopeEffCtrl->GetSoundForMeshEffect(this, SID("rope-material"), pInfo->m_soundDef.m_hitSound);
			pInfo->m_soundDef.m_slideSound = pRopeEffCtrl->GetSoundForMeshEffect(this, SID("rope-material"), pInfo->m_soundDef.m_slideSound);
			pInfo->m_soundDef.m_edgeSlideSound = pRopeEffCtrl->GetSoundForMeshEffect(this, SID("rope-material"), pInfo->m_soundDef.m_edgeSlideSound);
			if (pInfo->m_soundDef.m_endAudioConfig)
			{
				pInfo->m_soundDef.m_endHitSound = pRopeEffCtrl->GetSoundForMeshEffect(this, SID("rope-material"), pInfo->m_soundDef.m_endHitSound);
				pInfo->m_soundDef.m_endSlideSound = pRopeEffCtrl->GetSoundForMeshEffect(this, SID("rope-material"), pInfo->m_soundDef.m_endSlideSound);
			}
		}
		else
		{
			pInfo->m_soundDef.m_hitSound = INVALID_STRING_ID_64;
			pInfo->m_soundDef.m_edgeSlideSound = INVALID_STRING_ID_64;
			if (pInfo->m_soundDef.m_endAudioConfig)
			{
				pInfo->m_soundDef.m_endHitSound = INVALID_STRING_ID_64;
			}
		}

		m_rope.InitSounds(&pInfo->m_soundDef);
		m_rope.InitFx(&pInfo->m_fxDef);
	}

	if (StringId64 splineId = spawn.GetData<StringId64>(SID("start-rope-spline"), INVALID_STRING_ID_64))
	{
		const CatmullRom* pSpline = g_splineManager.FindByName(splineId);
		if (pSpline)
		{
			SetSimToSpline(pSpline, false, 0.0f, m_rope.m_fLength, spawn.GetData<bool>(SID("start-rope-spline-stretch"), false));
		}
		else
		{
			MsgConScriptError("Rope Spline %s not found for %s\n", DevKitOnly_StringIdToString(splineId), DevKitOnly_StringIdToString(GetUserId()));
			SetSimToBindPose();
		}
	}
	else if (pInfo && pInfo->m_endPointValid)
	{
		SetSimToLine(GetLocator().GetTranslation(), pInfo->m_endPoint, 0.0f, m_rope.m_fLength);
	}
	else if (spawnInPoint)
	{
		SetSimToLine(GetLocator().GetTranslation(), GetLocator().GetTranslation(), 0.0f, m_rope.m_fLength);
	}
	else
	{
		SetSimToBindPose();
	}
	m_rope.ResetSim();

	m_rope.m_bAutoStrained = spawn.GetData<bool>(SID("rope-auto-strained"), false);
	//m_rope.m_bSleeping = !spawn.GetData<bool>(SID("rope-start-active"), false);

#if !FINAL_BUILD
	m_rope.m_pDebugName = DevKitOnly_StringIdToString(GetLookId());
#else	//!FINAL_BUILD
	m_rope.m_pDebugName = nullptr;
#endif	//!FINAL_BUILD

	g_ropeMgr.RegisterRope(&m_rope);

	// Make sure mesh ray casts are not done against my PmInstance (foot IK ray casts and bullet decals).
	m_pDrawControl->SetInstanceFlag(FgInstance::kDisableMeshRayCasts | FgInstance::kNoProjection);

	// Special collider to prevent rope going through hand while in free rope
	//m_grabCollider.SetChildShape(new hkpBoxShape(hkVector4(1.0f, 0.5f, 0.5f)));

	SetCharacterClimbsAsWallRopeInternal(true);

	for (U32F ii = 0; ii < kNumCharColliders; ii++)
	{
		m_rope.AddCustomCollider(&m_charColliders[ii].m_collider);
		m_charColliders[ii].m_collider.m_enabled = false;
	}

	m_top = m_rope.GetRoot();

	m_firstCharacterKeyRopeDist = 0.0f;
	m_firstCharacterKeyPos = GetTranslation();

	m_bInited = true;

	return result;
}

Err ProcessPhysRope::InitializeDrawControl(const ProcessSpawnInfo& spawn, const ResolvedLook& resolvedLook)
{
	m_pRopeMeshInfo = ScriptManager::LookupInModule<const DC::RopeMeshInfo>(resolvedLook.m_lookOrMeshId, SID("phys-ropes"), nullptr);
	if (!m_pRopeMeshInfo && resolvedLook.m_numMeshes == 1)
	{
		m_pRopeMeshInfo = ScriptManager::LookupInModule<const DC::RopeMeshInfo>(resolvedLook.m_aMeshDesc[0].m_meshId, SID("phys-ropes"), nullptr);
	}

	bool bRopeSkinning = m_pRopeMeshInfo != nullptr;
	RopeInfo* pInfo = (RopeInfo*) spawn.m_pUserData;
	if (pInfo)
	{
		bRopeSkinning = bRopeSkinning && pInfo->m_bRopeSkinning;
	}
	else
	{
		bRopeSkinning = bRopeSkinning && spawn.GetData<bool>(SID("rope-skinning"), true);
	}


	m_pDrawControl->SetFlag(IDrawControl::kRopeSkinning, bRopeSkinning);

	const Err result = ParentClass::InitializeDrawControl(spawn, resolvedLook);

	if (pInfo && pInfo->m_bGhosting)
		GetDrawControl()->SetInstanceFlag(FgInstance::kGhostingID);
	else
		GetDrawControl()->ClearInstanceFlag(FgInstance::kGhostingID);

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::EnableAnimation()
{
	ParentClass::EnableAnimation();
	ChangeAnimConfig(FgAnimData::kAnimConfigComplexAnimation);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::ResetCharPoints()
{
	memset(m_charPointsOnRope, 0, kCharPointCount * sizeof(m_charPointsOnRope[0]));
	m_charPointsOnRope[kRightHand].m_attachSid = SID("targRopeRHand");
	m_charPointsOnRope[kLeftHand].m_attachSid = SID("targRopeLHand");
	m_charPointsOnRope[kLeftThigh].m_attachSid = SID("targRopeLThigh");
	m_charPointsOnRope[kLeftFoot].m_attachSid = SID("targRopeLAnkle");
	m_rightHandIsFirstExternal = false;
	m_rightHandIsFirstSet = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	if (m_bInited)
		g_ropeMgr.RelocateRope(&m_rope, delta, lowerBound, upperBound);
	m_rope.Relocate(delta, lowerBound, upperBound);
	RelocatePointer(m_pJointRopeDist, delta, lowerBound, upperBound);

	ParentClass::Relocate(delta, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetTeleport()
{
	SetSimToBindPose();
	m_rope.ResetSim();
	m_rope.Teleport(Locator(kIdentity));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetSimToBindPose()
{
	const Locator& align = GetLocator();
	const JointCache& jointCache = GetAnimData()->m_jointCache;
	U32F baseJoint = GetFirstJoint();
	U32F endJoint = GetLastJoint();

	Transform xfm;
	jointCache.GetBindPoseJointXform(xfm, baseJoint);
	Point lastPos = align.TransformPoint(xfm.GetTranslation());
	Point pos = lastPos;
	m_rope.ResetSimPos(0, lastPos);
	U32F joint = 1;
	for (U32F ii = 1; ii<m_rope.GetNumSimPoints(); ii++)
	{
		F32 ropeDist = m_rope.GetSimRopeDist()[ii];
		while (m_pJointRopeDist[joint] < ropeDist && joint < endJoint)
		{
			joint++;
			lastPos = pos;
			jointCache.GetBindPoseJointXform(xfm, baseJoint+joint);
			pos = align.TransformPoint(xfm.GetTranslation());
		}
		F32 t = (ropeDist-m_pJointRopeDist[joint-1])/(m_pJointRopeDist[joint]-m_pJointRopeDist[joint-1]);

		m_rope.ResetSimPos(ii, Lerp(lastPos, pos, t));
	}

	// Clear edges if any
	if (!m_rope.m_bSimpleRope)
	{
		m_rope.SetNumEdges(0);
	}
	if (m_rope.GetSaveEdges())
	{
		m_rope.SetNumSaveEdges(0);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetSimToAnimPose()
{
	const JointCache& jointCache = GetAnimData()->m_jointCache;
	U32F baseJoint = GetFirstJoint();

	U32F joint = 1;
	m_rope.ResetSimPos(0, jointCache.GetJointLocatorWs(baseJoint).GetTranslation());
	for (U32F ii = 1; ii<m_rope.GetNumSimPoints(); ii++)
	{
		F32 ropeDist = m_rope.GetSimRopeDist()[ii];
		while (m_pJointRopeDist[joint] < ropeDist)
		{
			joint++;
			ASSERT(joint+baseJoint <= GetLastJoint());
		}
		Point lastPosWs = jointCache.GetJointLocatorWs(baseJoint+joint-1).GetTranslation();
		Point posWs = jointCache.GetJointLocatorWs(baseJoint+joint).GetTranslation();
		F32 t = (ropeDist-m_pJointRopeDist[joint-1])/(m_pJointRopeDist[joint]-m_pJointRopeDist[joint-1]);
		m_rope.ResetSimPos(ii, Lerp(lastPosWs, posWs, t));
	}

	// Clear edges if any
	if (!m_rope.m_bSimpleRope)
	{
		m_rope.SetNumEdges(0);
	}
	if (m_rope.GetSaveEdges())
	{
		m_rope.SetNumSaveEdges(0);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float ProcessPhysRope::SetSimToSpline(const CatmullRom* pSpline, bool reversed, F32 startRopeDist, F32 endRopeDist, bool stretch)
{
	F32 splineLen = pSpline->GetTotalArcLength();
	for (U32F ii = 0; ii < m_rope.GetNumSimPoints(); ii++)
	{
		F32 ropeDist = m_rope.GetSimRopeDist()[ii];
		if (ropeDist < startRopeDist)
		{
			continue;
		}
		if (ropeDist > endRopeDist)
		{
			break;
		}
		F32 splineDist;
		if (stretch)
		{
			Scalar tt = (ropeDist-startRopeDist) / (endRopeDist-startRopeDist);
			splineDist = tt * splineLen;
		}
		else
		{
			splineDist = ropeDist - startRopeDist;
			splineDist = Min(splineDist, splineLen);
		}
		Point pos = pSpline->EvaluatePointAtArcLength(reversed ? (splineLen - splineDist) : splineDist);
		m_rope.ResetSimPos(ii, pos);
	}

	// Clear edges if any
	if (!m_rope.m_bSimpleRope)
	{
		m_rope.SetNumEdges(0);
	}
	if (m_rope.GetSaveEdges())
	{
		m_rope.SetNumSaveEdges(0);
	}

	return Min(endRopeDist, Min(startRopeDist+splineLen, m_rope.m_fLength));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetSimToLine(Point_arg startPt, Point_arg endPt, F32 startRopeDist, F32 endRopeDist)
{
	for (U32F ii = 0; ii<m_rope.GetNumSimPoints(); ii++)
	{
		F32 ropeDist = m_rope.GetSimRopeDist()[ii];
		if (ropeDist < startRopeDist)
		{
			continue;
		}
		if (ropeDist > endRopeDist)
		{
			break;
		}
		Scalar tt = (ropeDist-startRopeDist) / (endRopeDist-startRopeDist);
		Point pos = Lerp(startPt, endPt, tt);
		m_rope.ResetSimPos(ii, pos);
	}

	// Clear edges if any
	if (!m_rope.m_bSimpleRope)
	{
		m_rope.SetNumEdges(0);
	}
	if (m_rope.GetSaveEdges())
	{
		m_rope.SetNumSaveEdges(0);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetSimToPoint(Point_arg pt, F32 startRopeDist, F32 endRopeDist)
{
	for (U32F ii = 0; ii<m_rope.GetNumSimPoints(); ii++)
	{
		F32 ropeDist = m_rope.GetSimRopeDist()[ii];
		if (ropeDist < startRopeDist)
		{
			continue;
		}
		if (ropeDist > endRopeDist)
		{
			break;
		}
		m_rope.ResetSimPos(ii, pt);
	}

	// Clear edges if any
	if (!m_rope.m_bSimpleRope)
	{
		m_rope.SetNumEdges(0);
	}
	if (m_rope.GetSaveEdges())
	{
		m_rope.SetNumSaveEdges(0);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::DebugDrawAnimBlend()
{
	STRIP_IN_FINAL_BUILD;

	const JointCache& jointCache = GetAnimData()->m_jointCache;

	U32F baseJoint = GetFirstJoint();

	{
		I32F endJoint = GetLastJoint();
		do
		{
			g_prim.Draw(DebugCoordAxes(jointCache.GetJointLocatorWs(endJoint), m_rope.m_fRadius*2.0f));
			if (endJoint == baseJoint)
				break;
			endJoint = jointCache.GetParentJoint(endJoint);
		} while (endJoint >= 0);
	}

	U32F joint = 1;
	Point pos = jointCache.GetJointLocatorWs(baseJoint).GetTranslation();
	DebugDrawSphere(pos, m_rope.m_fRadius, kColorRed, kPrimDuration1FramePauseable);
	Scalar dist = kZero;
	for (U32F ii = 1; ii<m_rope.GetNumSimPoints(); ii++)
	{
		F32 ropeDist = m_rope.GetSimRopeDist()[ii];
		while (m_pJointRopeDist[joint] < ropeDist)
		{
			joint++;
			ASSERT(baseJoint+joint <= GetLastJoint());
		}
		Point lastPosWs = jointCache.GetJointLocatorWs(baseJoint+joint-1).GetTranslation();
		Point posWs = jointCache.GetJointLocatorWs(baseJoint+joint).GetTranslation();
		F32 t = (ropeDist-m_pJointRopeDist[joint-1])/(m_pJointRopeDist[joint]-m_pJointRopeDist[joint-1]);
		Point newPos = Lerp(lastPosWs, posWs, t);
		dist += Dist(pos, newPos);
		pos = newPos;
		DebugDrawSphere(pos, m_rope.m_fRadius, kColorRed, kPrimDuration1FramePauseable);
	}

	//printf("Rope stretch: %f\n", (F32)dist/m_rope.m_fLength);

	char buf[10];
	snprintf(buf, 10, "%.2f", MinMax01(1.0f-m_physBlend));
	DebugDrawString(GetTranslation(), buf, kColorWhite, kPrimDuration1FramePauseable);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetKeyframedFromAnim(F32 ropeDist, U32 flags)
{
	const JointCache& jointCache = GetAnimData()->m_jointCache;

	U32F baseJoint = GetFirstJoint();
	U32F joint = 1;
	while (m_pJointRopeDist[joint] < ropeDist)
	{
		joint++;
	}
	Point lastPosWs = jointCache.GetJointLocatorWs(baseJoint+joint-1).GetTranslation();
	Point posWs = jointCache.GetJointLocatorWs(baseJoint+joint).GetTranslation();
	F32 t = (ropeDist-m_pJointRopeDist[joint-1])/(m_pJointRopeDist[joint]-m_pJointRopeDist[joint-1]);
	Point pos = Lerp(lastPosWs, posWs, t);

	m_rope.SetKeyframedPos(ropeDist, pos, flags);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::WakeUp()
{
	m_rope.WakeUp();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::EventHandler(Event& event)
{
	NdDrawableObject::EventHandler(event);

	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("make-unusable"):
		{
			ScriptMakeUsable(false);
		}
		break;
	case SID_VAL("make-usable"):
		{
		ScriptMakeUsable(true);
		}
		break;
	case SID_VAL("update-visibility-for-parent"):
		InheritVisibilityFromParent();
		break;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::OnKillProcess()
{
	if (m_pStepRopeCounter)
		WaitStepRope();

	if (m_bInited)
	{
		g_ropeMgr.UnregisterRope(&m_rope);
		m_rope.Destroy();
	}

	for (U32F ii = 0 ; ii<kNumCharColliders; ii++)
	{
		m_charColliders[ii].Destroy();
	}
	if (m_grabCollider.m_pShape)
		m_grabCollider.m_pShape->removeReference();

	if (m_pRopeConstraint)
	{
		m_pRopeConstraint->Destroy();
		m_pRopeConstraint->removeReference();
		m_pRopeConstraint = nullptr;
	}

	NdDrawableObject::OnKillProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::Active::Enter()
{
	//ALWAYS_ASSERT(Self().GetAnimControl());
	//Self().GetAnimControl()->GetSimpleLayerById(SID("base"))->RequestFadeToAnim(SID("idle"));

	NdDrawableObject::Active::Enter();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::MakeUsable(bool usable)
{
	// This flag is queried by the player to see if he can grab the rope or not.
	// We need do nothing but cache the flag.
	m_bUsable = usable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ProcessPhysRope::IsUsable() const
{
	if (!m_bUsable || !m_bScriptUsable)
	{
		return false;
	}

	if (const Character* pChar = m_hCharacter.ToProcess())
	{
		if (!pChar->IsKindOf(SID("Player")))
		{
			return false;
		}
	}

	return true;
}

void ProcessPhysRope::ScriptMakeUsable(bool usable)
{
	// This flag is queried by the player to see if he can grab the rope or not.
	// We need do nothing but cache the flag.
	m_bScriptUsable = usable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT_CLASS_DEFINE(ProcessPhysRope, StepRopeJob)
{
	ProcessPhysRope* pRopeProc = reinterpret_cast<ProcessPhysRope*>(jobParam);
	pRopeProc->StepRope(true);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::PreStepRope()
{
	m_top = m_rope.GetRoot();
	for (U32 ii = 0; ii<m_numQueryPoints; ii++)
	{
		m_queryPoints[ii] = m_rope.GetPos(m_queryRopeDist[ii]);
	}

	if (m_physBlend > 0.0f)
	{
		PreStepRopeSkinning();
		m_rope.PreStep();
	}

	if (m_physBlend > 0.0f)
	{
		// This is one frame behind
		m_rope.UpdateSounds(this);
		m_rope.UpdateFx();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// You can use this once you're done keyframing the rope for this frame
// But you take all the responsability for not touching the rope data while it's simulation is running async
void ProcessPhysRope::KickStepRope()
{
	if (m_physBlend == 0.0f)
		return;

#if !OPT_OUT_JOINTS_WHEN_ROPE_SKINNED
	if (m_pDrawControl->GetFlag(IDrawControl::kRopeSkinning))
	{
		// This rope step may run all the way till the rendering frame.
		// We only need joint cache for some gameplay code possibly reading joint position (interactables apparently)
		// so we update joint cache before the step. It will be one frame late but hope that's fine.
		if (!m_rope.m_bSleeping)
		{
			UpdateJointCacheFromRope();
		}
	}
#endif

	PreStepRope();

	PHYSICS_ASSERT(!m_pStepRopeCounter);
	if (m_pStepRopeCounter)
	{
		// This shouldn't happen... but if it does (which it did) we handle it gracefully
		ndjob::WaitForCounterAndFree(m_pStepRopeCounter);
		m_pStepRopeCounter = nullptr;
	}
	ALWAYS_ASSERT(!m_stepRopeDoneEarly);
	ndjob::JobDecl jobDecl(StepRopeJob, (uintptr_t)this);
	ndjob::RunJobs(&jobDecl, 1, &m_pStepRopeCounter, FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::WaitStepRope()
{
	if (m_physBlend == 0.0f)
		return;

	ALWAYS_ASSERT(m_pStepRopeCounter);
	ndjob::WaitForCounterAndFree(m_pStepRopeCounter);
	m_pStepRopeCounter = nullptr;

	PostStepRope();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::WaitStepRopeEarly()
{
	if (m_physBlend == 0.0f)
		return;

	WaitStepRope();
	m_stepRopeDoneEarly = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::CheckStepNotRunning() const
{
	STRIP_IN_FINAL_BUILD;

	if (!m_pStepRopeCounter || m_pStepRopeCounter->GetValue() == 0)
		return;

	// If we're running the step we can only read data from the step job itself
	// otherwise it's race condition
	ndjob::JobDecl jobDecl;
	bool isJob = ndjob::GetActiveJobDecl(&jobDecl);
	ALWAYS_ASSERT(isJob && jobDecl.m_pStartFunc == StepRopeJob);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::StepRope(bool async)
{
	ProcessMouse();

	F32 collidedLen;
	Point lastCollisionPnt;
	GetCollidedRopeInfo(collidedLen, lastCollisionPnt);

	if (m_bAttached && (m_rope.GetNumKeyPoints() == 0 || m_rope.GetKeyRopeDist(0) != 0.0f))
	{
		m_rope.SetKeyframedPos(0.0f, GetTranslation(), kAttachPoint);
	}

	AttachRopeToCharacter();

	if (m_bEndAttachment)
	{
		if (m_rope.GetNumKeyPoints() == 0 || m_rope.GetKeyRopeDist(m_rope.GetNumKeyPoints()-1) < m_rope.m_fLength - 0.05f)
		{
			Rope2::RopeNodeFlags flags = 0;
			if (m_bEndAttachmentWithDir)
			{
				m_rope.SetKeyframedPos(m_rope.m_fLength-0.05f, m_endAttachment.GetLocator().TransformPoint(Point(0.0f, 0.0f, 0.02f)));
				flags = Rope2::kNodeKeyframedSeg;
			}
			m_rope.SetKeyframedPos(m_rope.m_fLength, m_endAttachment.GetTranslation(), flags);
		}
	}

	UpdateCharacterCollision(lastCollisionPnt);

	UpdateWind();

	m_rope.Step();

	PostStepRopeSkinning(false);

	// Somethings are better reset each frame
	m_rope.ClearAllKeyframedPos();

	// Update rope collision info
	GetCharacterCollidedStraightenRopeInfo(m_collidedStraigthenRopeLen, m_anchorEdgeIndex, m_handEdgeIndex);
	UpdateStraightDistToAnchorInner();

	// Take care of constrained body
	RigidBody* pConstrainedBody = m_hConstrainedBody.ToBody();
	if ((pConstrainedBody && m_anchorEdgeIndex >= 0 && m_handEdgeIndex > m_anchorEdgeIndex) || m_pRopeConstraint)
	{
		// Need to do this in frame sync end because of constraint creation/destruction
		g_ropeMgr.RegisterForFrameSyncEnd(this);
	}

	if (m_hAnchorSwingImpulseBody.HandleValid())
		ApplySwingImpulseOnAnchor();

	if (m_tempLayerMaskTimeout > 0.0f)
	{
		m_tempLayerMaskTimeout -= GetClock()->GetDeltaTimeInSeconds();
		if (m_tempLayerMaskTimeout <= 0.0f)
		{
			m_tempLayerMaskTimeout = 0.0f;
			ClearTempLayerMask();
		}
	}

	m_firstStepDone = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::PostStepRope()
{
	if (g_ropeMgr.m_debugDrawAnimBlend) // && m_physBlend < 1.0f)
		DebugDrawAnimBlend();
};

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::UpdateJointCacheFromRope()
{
	JointCache& jointCache = GetAnimData()->m_jointCache;
	const U32F startJoint = GetFirstJoint();
	const U32F endJoint = GetLastJoint();
	const Locator alignLoc = GetLocator();

	Point ropePos;
	Vector ropeDir = -Vector(kUnitYAxis);

	const Scalar physBlend = m_physBlend;
	ndanim::JointParams animJP;

	bool bPastUsedLength = false;

	const F32 scale = GetUniformScale();

	for(I32F ii = startJoint; ii <= endJoint; ii++)
	{
		F32 ropeDist = m_pJointRopeDist[ii-startJoint];
		Vector ropeDirFallback = ropeDir;
		m_rope.GetPosAndDirSmooth(ropeDist, ropePos, ropeDir, ropeDirFallback);

		I32F parent = jointCache.GetParentJoint(ii);
		Locator parentLocWs = parent >= 0 ? jointCache.GetJointLocatorWs(parent) : alignLoc;

		animJP = jointCache.GetJointParamsLs(ii);

		// Blend position in world space
		Point posWs = Lerp(parentLocWs.TransformPoint(kOrigin + scale * (animJP.m_trans - kOrigin)), ropePos, physBlend);

		// Blend rotation in local space while maintaining the rotation around Y from animation
		Vector ropeDirLocal = Unrotate(parentLocWs.GetRotation(), ropeDir);
		Quat anim2ropeRot = QuatFromVectors(-GetLocalY(animJP.m_quat), ropeDirLocal);
		Quat quatRopeLs = Normalize(anim2ropeRot * animJP.m_quat);
		Quat quatLs = Slerp(animJP.m_quat, quatRopeLs, physBlend);

		Locator locWs(posWs, parentLocWs.GetRotation() * quatLs);
		jointCache.OverwriteJointLocatorWs(ii, locWs);

		if (bPastUsedLength)
			// We did one more joint past the used length, now we can bail out
			break;
		bPastUsedLength = (m_usedLength >= 0.0f) & (ropeDist > m_usedLength);
	}

	// Update Ws locators of joints that are not driven by rope and could be children of rope driven joints
	ndanim::JointParams params;
	for (U32F ii = endJoint+1; ii<jointCache.GetNumAnimatedJoints(); ii++)
	{
		I32F parent = jointCache.GetParentJoint(ii);
		Locator parentLoc = parent >= 0 ? jointCache.GetJointLocatorWs(parent) : alignLoc;
		params = jointCache.GetJointParamsLs(ii);
		Locator loc(kOrigin + scale * (params.m_trans - kOrigin), params.m_quat);
		jointCache.OverwriteJointLocatorWs(ii, parentLoc.TransformLocator(loc));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ProcessPhysRope::UpdateRope(bool bAnimating)
{
	bool bMoved = false;
	if (m_physBlend == 0.0f)
	{
		PreStepRope();
		PreStepRopeSkinning();
		m_rope.PreSetKeyframedPos();
		SetSimToAnimPose();
		m_rope.StepKeyframed();
		PostStepRopeSkinning(false);
		PostStepRope();
		m_rope.ClearAllKeyframedPos();
	}
	else
	{
		if (m_pDrawControl->GetFlag(IDrawControl::kRopeSkinning) && m_bEnableAsyncRopeStep)
		{
			if (!m_stepRopeDoneEarly)
			{
				if (!m_pStepRopeCounter)
					KickStepRope();
				g_ropeMgr.RegisterForPreNoRefRigidBodySync(this);
			}
			bMoved = !m_rope.m_bSleeping;
		}
		else
		{
			if (!m_stepRopeDoneEarly)
			{
				if (m_pStepRopeCounter)
					WaitStepRope();
				else
				{
					PreStepRope();
					StepRope();
					PostStepRope();
				}
			}

			if (!m_rope.m_bSleeping || bAnimating)
			{
				bMoved = true;
				UpdateJointCacheFromRope();
			}
		}
	}

	m_stepRopeDoneEarly = false;

	//Update the phys blend
	{
		if (m_physBlendTime > 0.f)
		{
			Seek(m_physBlend, m_desiredPhysBlend, FromUPS(1.0f/m_physBlendTime));
		}
		else
		{
			m_physBlend = m_desiredPhysBlend;
		}
	}

	return bMoved;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::PreStepRopeSkinning()
{
	STRIP_IN_RENDERLESS_MODE;

	m_pAnimData->m_pRopeSkinningData = nullptr;
	if (m_pDrawControl->GetFlag(IDrawControl::kRopeSkinning))
	{
		m_pAnimData->m_pRopeSkinningData = NDI_NEW(kAllocDoubleGameFrame) RopeSkinningData;
		m_pRopeBonesData = &m_pAnimData->m_pRopeSkinningData->m_ropeBonesData;

		m_pRopeBonesData->m_prevObjXform = m_prevObjXform;

		const Locator align2world = GetLocator();
		m_pRopeBonesData->m_objXform = align2world.AsTransform().GetMat44();

		m_prevObjXform = m_pRopeBonesData->m_objXform;

		m_rope.SetRopeSkinningData(m_pRopeBonesData);
		m_rope.FillDataForSkinningPreStep(m_pRopeBonesData);

		m_pRopeBonesData->m_pRopeGpuSimOut = nullptr;

		m_pAnimData->m_pRopeSkinningData->m_pDataReadyCounter = ndjob::AllocateCounter(FILE_LINE_FUNC, 1);
		RopeSkinning::AddData(m_pAnimData->m_pRopeSkinningData);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::PostStepRopeSkinning(bool paused)
{
	STRIP_IN_RENDERLESS_MODE;

	if (m_pDrawControl->GetFlag(IDrawControl::kRopeSkinning))
	{
		PHYSICS_ASSERT(m_pRopeBonesData);
		m_rope.FillDataForSkinningPostStep(m_pRopeBonesData, paused);
		m_pRopeBonesData->m_meshRadius = m_pRopeMeshInfo->m_meshRadius;
		m_pRopeBonesData->m_numMeshSegments = m_pRopeMeshInfo->m_numMeshSegments;
		m_pAnimData->m_pRopeSkinningData->m_numMeshSegments = m_pRopeMeshInfo->m_numMeshSegments;
		m_pAnimData->m_pRopeSkinningData->m_meshSegmentLength = m_pRopeMeshInfo->m_meshLength / m_pRopeMeshInfo->m_numMeshSegments;
		m_pAnimData->m_pRopeSkinningData->m_textureVRate = m_pRopeMeshInfo->m_textureVRate;
		m_pAnimData->m_pRopeSkinningData->m_pGpuDoneCounter = m_rope.GetGpuWaitCounter();
		m_pAnimData->m_pRopeSkinningData->m_pDataReadyCounter->Decrement();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::UpdateUsedLengthSkinning()
{
	FgAnimData* pAnimData = GetAnimData();

	const AnimExecutionContext* pCtx = GetAnimExecutionContext(pAnimData);

	// Scale to zero rope joints after the used length
	if (m_usedLength >= 0.0f && m_usedLength < m_rope.m_fLength)
	{
		const U32F startJoint = GetFirstJoint();
		const U32F endJoint = GetLastJoint();
		I32F jointIndex;
		for(jointIndex = startJoint; jointIndex <= endJoint; jointIndex++)
		{
			F32 ropeDist = m_pJointRopeDist[jointIndex-startJoint];
			if (ropeDist > m_usedLength)
				break;
		}

		if (jointIndex <= endJoint)
		{
			Transform align2worldInv = GetLocator().AsInverseTransform();
			Transform scaleInv;
			scaleInv.SetScale(Recip(pAnimData->m_scale));
			align2worldInv = align2worldInv * scaleInv;
			const Mat44& align2worldInvMat44 = align2worldInv.GetMat44();

			Locator locWs = pAnimData->m_jointCache.GetJointLocatorWs(jointIndex);
			const Mat34& invBindPose = ndanim::GetInverseBindPoseTable(pAnimData->m_pSkeleton)[jointIndex];
			Mat44 xfmWs = locWs.AsTransform().GetMat44();
			xfmWs.SetRow(0, Vec4(kZero));
			xfmWs.SetRow(1, Vec4(kZero));
			xfmWs.SetRow(2, Vec4(kZero));
			Mat34 newJointMatrix(invBindPose.GetMat44()*xfmWs*align2worldInvMat44);

			// The first scaled to zero joint needs to maintain its position
			for (U32F row = 0; row < 3 ; row++)
			{
				pCtx->m_pAllSkinningBoneMats[jointIndex*3 + row] = newJointMatrix[row];
			}

			// Others can just be plain scaled to zero
			for (U32F row = (jointIndex+1)*3; row < (endJoint+1)*3 ; row++)
			{
				pCtx->m_pAllSkinningBoneMats[row] *= Vec4(kZero);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::PostAnimBlending_Async()
{
	if (HavokNotPaused())
	{
		UpdateRope(true);
	}
	ParentClass::PostAnimBlending_Async();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::PostJointUpdate_Async()
{
	ParentClass::PostJointUpdate_Async();

	if (CanDisableUpdates() && !IsSimpleAnimating())
	{
		DisableUpdates();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ProcessPhysRope::DisableAnimation()
{
	if (!ParentClass::DisableAnimation())
	{
		return false;
	}
	EngineComponents::GetAnimMgr()->SetAnimationDisabled(m_pAnimData, FastAnimDataUpdate);
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// static
void ProcessPhysRope::CustomAnimPass1(FgAnimData* pAnimData, F32 dt)
{
	Process* pProcess = pAnimData->m_hProcess.ToMutableProcess();
	ProcessPhysRope* pRopeProc = ProcessPhysRope::FromProcess(pProcess);

	if (dt == 0.0f)
	{
		// If paused, the Update has not been called at all so we need to setup the rope skinning data
		pRopeProc->PreStepRopeSkinning();
		pRopeProc->PostStepRopeSkinning(true);
	}

#if OPT_OUT_JOINTS_WHEN_ROPE_SKINNED
	if (pRopeProc->m_pDrawControl->GetFlag(IDrawControl::kRopeSkinning))
	{
		pRopeProc->UpdateAnimDataForRopeSkinning(pAnimData);
	}
	else
#endif
	{
		// This function comes handy although it could be done slightly more efficiently
		pRopeProc->AnimDataRecalcBoundAndSkinning(pRopeProc->GetAnimData());
		pRopeProc->UpdateUsedLengthSkinning();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::DoFastAnimDataUpdate(FgAnimData* pAnimData, F32 dt, bool bMoved)
{
	PROFILE(Processes, DoFastAnimDataUpdate);

//#if !FINAL_BUILD
//	if (IDrawControl* pDrawCtrl = GetDrawControl())
//	{
//		const bool isSelected = DebugSelection::Get().IsProcessSelected(this);
//		pDrawCtrl->SetObjectSelected(isSelected);
//	}
//#endif

	const Locator alignLoc = GetLocator();
	m_prevLocator = alignLoc;

	JointCache* pJointCache = &pAnimData->m_jointCache;
	ndanim::JointParams params;

	if (dt > 0.0f)
	{
		if (m_physBlend > 0.0f && !bMoved)
		{
			const F32 scale = GetUniformScale();
			I32F baseJoint = GetFirstJoint();
			// Update Ws locators of joints that are not driven by rope (the ones that could be parent of any of the rope-driven joint)
			for (U32F ii = 0; ii<baseJoint; ii++)
			{
				I32F parent = pJointCache->GetParentJoint(ii);
				Locator parentLoc = parent >= 0 ? pJointCache->GetJointLocatorWs(parent) : alignLoc;
				params = pJointCache->GetJointParamsLs(ii);
				Locator loc(kOrigin + scale * (params.m_trans - kOrigin), params.m_quat);
				pJointCache->OverwriteJointLocatorWs(ii, parentLoc.TransformLocator(loc));
			}
		}

		bMoved |= UpdateRope(false);
	}
	else
	{
		PreStepRopeSkinning();
		PostStepRopeSkinning(true);
	}

#if OPT_OUT_JOINTS_WHEN_ROPE_SKINNED
	if (m_pDrawControl->GetFlag(IDrawControl::kRopeSkinning))
	{
		UpdateAnimDataForRopeSkinning(pAnimData);
	}
	else
#endif
	if (bMoved)
	{
		FastAnimDataUpdateBoundAndSkinning(pAnimData);
		UpdateUsedLengthSkinning();
	}
	else
	{
		pAnimData->SetXform(alignLoc);

		// Update BSphere Ws
		if (!(pAnimData->m_flags & FgAnimData::kDisableBSphereCompute))
		{
			UpdateBSphereWs(pAnimData, alignLoc);
		}

		PropagateSkinningBoneMats(pAnimData);
	}

	ManuallyRefreshSnapshot();

#ifndef FINAL_BUILD
	GetAnimExecutionContext(m_pAnimData)->Validate();
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
// static
void ProcessPhysRope::FastAnimDataUpdate(FgAnimData* pAnimData, F32 dt)
{
	Process* pProcess = pAnimData->m_hProcess.ToMutableProcess();
	ProcessPhysRope* pRopeProc = ProcessPhysRope::FromProcess(pProcess);
	pRopeProc->DoFastAnimDataUpdate(pAnimData, dt, false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::UpdateAnimDataForRopeSkinning(FgAnimData* pAnimData)
{
	const Locator align2world = GetLocator();
	pAnimData->SetXform(align2world, false);

	{
		Point minPosOs(kLargestFloat, kLargestFloat, kLargestFloat);
		Point maxPosOs(-kLargestFloat, -kLargestFloat, -kLargestFloat);

		for (U32F ii = 0; ii < m_rope.GetNumSimPoints(); ii++)
		{
			Point pos = m_rope.GetSimPos()[ii];
			Point posOs = align2world.UntransformPoint(pos);

			minPosOs = Min(minPosOs, posOs);
			maxPosOs = Max(maxPosOs, posOs);
		}

		F32 paddingRadius = 0.1f;
		const Vector pad = Vector(paddingRadius, paddingRadius, paddingRadius);

		minPosOs -= pad;
		maxPosOs += pad;

		const Point centerPosOs = AveragePos(minPosOs, maxPosOs);
		const Vector diagonal(maxPosOs - minPosOs);

		Sphere sphereWs;
		sphereWs.SetCenter(align2world.TransformPoint(Point(centerPosOs)));
		sphereWs.SetRadius(Length(diagonal) * 0.5f);

		pAnimData->m_pBoundingInfo->m_jointBoundingSphere = sphereWs;
		pAnimData->m_pBoundingInfo->m_aabb = Aabb(Point(minPosOs), Point(maxPosOs));
	}

	// We still need to do this even if we won't use the skinning matrices for anything
	// Assert would fire all over the place
	PropagateSkinningBoneMats(pAnimData);

	if (m_probeJointIndex >= 0)
	{
		// We need to setup the skinning bone of this joint so that lighting works as expected
		F32 ropeDist = m_pJointRopeDist[m_probeJointIndex-GetFirstJoint()];
		Point probePos = m_rope.GetPos(Min(ropeDist, m_rope.m_fLength));

		Transform bindPoseXform;
		m_pAnimData->m_jointCache.GetBindPoseJointXform(bindPoseXform, m_probeJointIndex);
		Mat44 bindPose44 = bindPoseXform.GetMat44();
		Mat44 probeJoint44Ws = bindPose44 * align2world.AsTransform().GetMat44();
		probeJoint44Ws.SetRow(3, probePos.GetVec4());

		const Mat34& invBindPose = ndanim::GetInverseBindPoseTable(m_pAnimData->m_pSkeleton)[m_probeJointIndex];
		Mat34 probeJoint34(invBindPose.GetMat44() * probeJoint44Ws * align2world.AsInverseTransform().GetMat44());

		const AnimExecutionContext* pCtx = GetAnimExecutionContext(pAnimData);
		pCtx->m_pAllSkinningBoneMats[m_probeJointIndex*3] = probeJoint34.GetRow(0);
		pCtx->m_pAllSkinningBoneMats[m_probeJointIndex*3+1] = probeJoint34.GetRow(1);
		pCtx->m_pAllSkinningBoneMats[m_probeJointIndex*3+2] = probeJoint34.GetRow(2);
	}

	pAnimData->SetJointsMoved();
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ProcessPhysRope::MeetsCriteriaToDebugRecord(bool& outRecordAsPlayer) const
{
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::FrameSyncEnd()
{
	// Take care of constrained body
	RigidBody* pConstrainedBody = m_hConstrainedBody.ToBody();
	if (pConstrainedBody && m_anchorEdgeIndex >= 0 && m_handEdgeIndex > m_anchorEdgeIndex)
	{
		I32F bodyEdgeIndex = -1;
		I32F firstEdgeFromBody = -1;
		F32 constraintRopeLen;

		if (m_bodyConstrainedAtAnchor)
		{
			// Find the first edge that is not on (or very close to) the constrained body
			U32F iFirstEdge;
			for (iFirstEdge = m_anchorEdgeIndex + 1; iFirstEdge<m_handEdgeIndex; iFirstEdge++)
			{
				bool isConstrainedBody = false;
				Rope2::EdgePointInfo eInfo;
				m_rope.GetEdgeInfo(iFirstEdge, eInfo);
				for (U32F ii = 0; ii<eInfo.m_numEdges; ii++)
				{
					if (eInfo.m_edges[ii].m_hRigidBody.ToBody() == pConstrainedBody)
					{
						isConstrainedBody = true;
						break;
					}
				}

				if (!isConstrainedBody)
				{
					if (Dist(m_rope.GetEdges()[iFirstEdge-1].m_pos, m_rope.GetEdges()[iFirstEdge].m_pos) > 0.05f)
					{
						break;
					}
				}
			}

			firstEdgeFromBody = iFirstEdge;
			bodyEdgeIndex = iFirstEdge-1; // this is the last edge point on the pulled object

			F32 handToFirstEdgeRopeLen = 0;
			for (U32F ii = iFirstEdge+1; ii<=m_handEdgeIndex; ii++)
				handToFirstEdgeRopeLen += Dist(m_rope.GetEdges()[ii-1].m_pos, m_rope.GetEdges()[ii].m_pos);
			constraintRopeLen = m_rope.GetEdges()[m_handEdgeIndex].m_ropeDist - handToFirstEdgeRopeLen - m_rope.GetEdges()[bodyEdgeIndex].m_ropeDist;
		}
		else
		{
			// Body is constrained at the "hand" edge
			// Find the first edge that is not on (or very close to) the constrained body
			U32F iFirstEdge;
			for (iFirstEdge = m_handEdgeIndex - 1; iFirstEdge>m_anchorEdgeIndex; iFirstEdge--)
			{
				bool isConstrainedBody = false;
				Rope2::EdgePointInfo eInfo;
				m_rope.GetEdgeInfo(iFirstEdge, eInfo);
				for (U32F ii = 0; ii<eInfo.m_numEdges; ii++)
				{
					if (eInfo.m_edges[ii].m_hRigidBody.ToBody() == pConstrainedBody)
					{
						isConstrainedBody = true;
						break;
					}
				}

				if (!isConstrainedBody)
				{
					if (Dist(m_rope.GetEdges()[iFirstEdge-1].m_pos, m_rope.GetEdges()[iFirstEdge].m_pos) > 0.05f)
					{
						break;
					}
				}
			}

			firstEdgeFromBody = iFirstEdge;
			bodyEdgeIndex = iFirstEdge+1; // this is the last edge point on the pulled object

			F32 firstEdgeToAnchorRopeLen = 0;
			U32F anchorIndex = iFirstEdge; // anchor or the first keyframed point going up the rope
			for (; anchorIndex>m_anchorEdgeIndex && (m_rope.GetEdges()[anchorIndex].m_flags & Rope2::kNodeKeyframed) == 0; anchorIndex--)
			{
				firstEdgeToAnchorRopeLen += Dist(m_rope.GetEdges()[anchorIndex].m_pos, m_rope.GetEdges()[anchorIndex-1].m_pos);
			}
			constraintRopeLen = m_rope.GetEdges()[bodyEdgeIndex].m_ropeDist - firstEdgeToAnchorRopeLen - m_rope.GetEdges()[anchorIndex].m_ropeDist;
		}

		Point firstEdgePnt = m_rope.GetEdges()[firstEdgeFromBody].m_pos;
		Point bodyPnt = m_rope.GetEdges()[bodyEdgeIndex].m_pos;
		Scalar pullDist;
		Vector pullVec = Normalize(firstEdgePnt - bodyPnt, pullDist);

		if (pullDist < 0.04f)
		{
			if (m_pRopeConstraint)
			{
				m_pRopeConstraint->Destroy();
				m_pRopeConstraint->removeReference();
			}
			m_pRopeConstraint = nullptr;
		}
		else
		{
			if (!m_pRopeConstraint)
				m_pRopeConstraint = new HavokRopeConstraint();

			pConstrainedBody->Touched();


#if 1
			// For now just recreate the constraint each frame ignoring the apparent velocity of the firstEdgePnt.
			// Seems like the constraint works fine without it at least in "normal" use cases.
			m_pRopeConstraint->Destroy();
#endif
			Point anchorLs = pConstrainedBody->GetLocatorCm().UntransformPoint(bodyPnt);
			//Point anchorLs = m_constrainedBodyPivot;

			//const F32 kLn100 = 4.605f;
			//F32 f = Ln(MinMax((F32)pullVec.Y() * 1.5f, 0.0f, 1.0f) * 99.0f + 1.0f) / kLn100;
			//F32 strength = Lerp(m_constrainedBodyPullStrength, 0.0001f, f);
			F32 strength = (F32)pullVec.Y() > m_constrainedBodyPullMaxCosY ? 0.001f : m_constrainedBodyPullStrength;
			//printf("Strength = %.4f\n", strength);
#if HAVOKVER < 0x2016
			m_pRopeConstraint->Create(pConstrainedBody->GetHavokBody(), anchorLs, firstEdgePnt, strength, Max(0.001f, constraintRopeLen));
#else
			m_pRopeConstraint->Create(pConstrainedBody->GetHavokBodyId(), anchorLs, firstEdgePnt, strength, Max(0.001f, constraintRopeLen));
#endif
		}
	}
	else
	{
		if (m_pRopeConstraint)
		{
			m_pRopeConstraint->Destroy();
			m_pRopeConstraint->removeReference();
		}
		m_pRopeConstraint = nullptr;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::ProcessMouse()
{
	STRIP_IN_FINAL_BUILD;

	if (m_rope.m_bSimpleRope)
		// Simple rope does not support mouse picking
		return;

	Mouse &mouse = EngineComponents::GetNdFrameState()->m_mouse;
	RenderCamera const &cam = GetRenderCamera(0);

	float fMarkerRadius = 0.01f;
	if (m_draggedRopeDist >= 0.0f)
	{
		// find a point in the camera's left/up plane going through current refPos
		Vec3 pos, dir;
		cam.VirtualScreenToWorld(pos, dir, mouse.m_position.X(), mouse.m_position.Y());
		Point newPos = cam.m_position + Vector(dir) * g_mouseGrabDist;
		g_prim.Draw( DebugSphere(Sphere(newPos, fMarkerRadius * g_mouseGrabDist), g_colorCyan));
		m_rope.SetKeyframedPos(m_draggedRopeDist, newPos);
		if (!(mouse.m_buttons & kMouseButtonLeft))
		{
			//if (!(mouse.m_buttons & kMouseButtonRight))
			//	m_rope.m_pNodeFlags[m_nDragged] &= ~Rope2::kNodeKeyframed;
			m_draggedRopeDist = -1.0f;
		}
		m_rope.WakeUp();
	}
	else
	{
		Scalar scBestSqR(100.0f);
		F32 bestRopeDist = -1.0f;
		F32 ropeDist = 0.0f;
		while (1)
		{
			Point ropePos = m_rope.GetPos(ropeDist);
			if (IsReasonable(ropePos) && cam.IsPointInFrustum(ropePos))
			{
				F32 sX, sY;
				cam.WorldToVirtualScreen(sX, sY, ropePos);
				Scalar scSqR = Sqr(sX-mouse.m_position.X()) + Sqr(sY-mouse.m_position.Y());
				if (scSqR < scBestSqR)
				{
					scBestSqR = scSqR;
					bestRopeDist = ropeDist;
				}
			}
			if (ropeDist == m_rope.m_fLength)
				break;
			ropeDist += m_rope.m_fSegmentLength;
			ropeDist = Min(ropeDist, m_rope.m_fLength);
		}
		if (bestRopeDist >= 0.0f)
		{
			// select this object
			F32 grabDist = Dist(m_rope.GetPos(bestRopeDist), cam.m_position);
			g_prim.Draw( DebugSphere(Sphere(m_rope.GetPos(bestRopeDist), grabDist * fMarkerRadius), g_colorYellow));

			if (mouse.m_buttonsPressed & kMouseButtonLeft)
			{
				m_draggedRopeDist = bestRopeDist;
				g_mouseGrabDist = grabDist;
				m_rope.SetKeyframedPos(m_draggedRopeDist, m_rope.GetPos(bestRopeDist));
				m_rope.InitRopeDebugger();
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetActiveCharacter(Character* pChar, bool persistant)
{
	PHYSICS_ASSERT(pChar);
	if (m_hCharacter.GetProcessId() == pChar->GetProcessId())
	{
		m_characterIsPersistant = persistant; // in case this has changed
		return;
	}

	if (m_hCharacter.HandleValid())
	{
		ClearActiveCharacter();
	}

	if (pChar)
	{
		ClearTempLayerMask();
		EnableCollideWithCharacters(false);
		m_rope.m_fViscousDamping = 0.001f;
	}

	m_hCharacter = pChar;

	m_characterIsPersistant = persistant;
	m_charCollidersReset = true;

	SetCharacterClimbsAsWallRope(true);
	for (U32F ii = 0; ii<kNumCharColliders; ii++)
	{
		m_charColliders[ii].m_collider.m_enabled = true;
	}
	//m_grabCollider.m_enabled = false;
	//m_rope.AddCustomCollider(&m_grabCollider);

	ResetCharPoints();

	m_rightHandOn = 0.0f;
	m_leftHandOn = 0.0f;
	m_rightHandIsFirstSetVal = 0.0f;
	m_slack = 0.0f;
	m_slackBetweenHands = 0.0f;
	m_charHasControlAnimChannels = false;

	m_reelInTime = 0.0f;
	m_anchorEdgeIndex = -1;
	m_straightDistToAnchor = 0.0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::ClearActiveCharacter()
{
	if (m_hCharacter.HandleValid())
	{
		m_grabRopeDistSet = false;
		m_rope.m_fViscousDamping = m_dampingWhenNoCharacter;
	}
	m_hCharacter = nullptr;

	for (U32F ii = 0; ii < kNumCharColliders; ii++)
	{
		m_charColliders[ii].m_collider.m_enabled = false;
	}
	m_grabColliderOn = false;

	ResetUsedLength();

	ClearTempLayerMask();
	EnableCollideWithCharacters(true);
	SetTempLayerMask(m_rope.m_layerMask & ~Collide::kLayerMaskCharacter, 1.0f);

	m_rope.m_minCharCollisionDist = 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetCharacterClimbsAsWallRope(bool b)
{
	if (m_characterClimbsAsWallRope == b)
		return;

	SetCharacterClimbsAsWallRopeInternal(b);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetCharacterClimbsAsWallRopeInternal(bool b)
{
	for (U32F ii = 0; ii<kNumCharColliders; ii++)
	{
		m_charColliders[ii].Destroy();
	}

	if (b)
	{
		m_charColliders[kColliderTrunk].Init(
			hknpCapsuleShape::createCapsuleShape(hkVector4(0.0f, 0.225f, 0.0f), hkVector4(0.0f, -0.225f, 0.0f), 0.2f),
			Locator(Point(0.0f, -0.01f, 0.027f), Quat(80.0f * PI / 180.0f, 0.0f * PI / 180.0f, 0.0f * PI / 180.0f)));
		m_charColliders[kColliderPelvis].Init(
			hknpCapsuleShape::createCapsuleShape(hkVector4(0.04f, 0.0f, 0.0f), hkVector4(-0.04f, 0.0f, 0.0f), 0.16f),
			Locator(Point(0.0f, -0.05f, 0.05f), Quat(0.0f * PI / 180.0f, 0.0f * PI / 180.0f, 0.0f * PI / 180.0f)));
		m_charColliders[kColliderRUpperLeg].Init(
			hknpCapsuleShape::createCapsuleShape(hkVector4(0.0f, 0.0f, 0.2f), hkVector4(0.0f, 0.0f, -0.16f), 0.10f),
			Locator(Point(0.0f, -0.011f, -0.24f), Quat(-10.0f * PI / 180.0f, 0.0f * PI / 180.0f, 0.0f * PI / 180.0f)));
		m_charColliders[kColliderLUpperLeg].Init(
			hknpCapsuleShape::createCapsuleShape(hkVector4(0.0f, 0.0f, 0.2f), hkVector4(0.0f, 0.0f, -0.16f), 0.10f),
			Locator(Point(0.0f, 0.011f, 0.25f), Quat(-6.0f * PI / 180.0f, 0.0f * PI / 180.0f, 0.0f * PI / 180.0f)));
		//m_charColliders[kColliderRShoulder].Init(
		//	hknpCapsuleShape::createCapsuleShape(hkVector4(0.0f, 0.16f, 0.0f), hkVector4(0.0f, -0.16f, 0.0f), 0.09f),
		//	Locator(Point(-0.01f, 0.027f, -0.105f), Quat(90.0f * PI / 180.0f, 0.0f * PI / 180.0f, 0.0f * PI / 180.0f)));
		//m_charColliders[kColliderLShoulder].Init(
		//	hknpCapsuleShape::createCapsuleShape(hkVector4(0.0f, 0.18f, 0.0f), hkVector4(0.0f, -0.18f, 0.0f), 0.09f),
		//	Locator(Point(0.01f, -0.027f, 0.105f), Quat(90.0f * PI / 180.0f, 0.0f * PI / 180.0f, 0.0f * PI / 180.0f)));
		//m_charColliders[kColliderRElbow].Init(
		//	hknpCapsuleShape::createCapsuleShape(hkVector4(0.0f, 0.0f, 0.11f), hkVector4(0.0f, 0.0f, -0.11f), 0.06f),
		//	Locator(Point(0.0f, 0.0f, -0.089f), Quat(0.0f * PI / 180.0f, 0.0f * PI / 180.0f, 0.0f * PI / 180.0f)));
		//m_charColliders[kColliderLElbow].Init(
		//	hknpCapsuleShape::createCapsuleShape(hkVector4(0.0f, 0.0f, 0.125f), hkVector4(0.0f, 0.0f, -0.125f), 0.06f),
		//	Locator(Point(0.0f, 0.0f, 0.089f), Quat(0.0f * PI / 180.0f, 0.0f * PI / 180.0f, 0.0f * PI / 180.0f)));
		//m_charColliders[kColliderRKnee].Init(
		//	hknpCapsuleShape::createCapsuleShape(hkVector4(0.0f, 0.0f, 0.19f), hkVector4(0.0f, 0.0f, -0.10f), 0.09f),
		//	Locator(Point(0.0f, -0.04f, -0.2f), Quat(0.0f * PI / 180.0f, 0.0f * PI / 180.0f, 0.0f * PI / 180.0f)));
		//m_charColliders[kColliderLKnee].Init(
		//	hknpCapsuleShape::createCapsuleShape(hkVector4(0.0f, 0.0f, 0.2f), hkVector4(0.0f, 0.0f, -0.2f), 0.09f),
		//	Locator(Point(0.0f, 0.04f, 0.2f), Quat(0.0f * PI / 180.0f, 0.0f * PI / 180.0f, 0.0f * PI / 180.0f)));
	}
	else
	{
		m_charColliders[kColliderTrunk].Init(
			hknpCapsuleShape::createCapsuleShape(hkVector4(0.0f, 0.0f, 0.225f), hkVector4(0.0f, 0.05f, -0.225f), 0.18f),
			Locator(kOrigin, Quat(kIdentity)));
		m_charColliders[kColliderPelvis].Init(
			hknpCapsuleShape::createCapsuleShape(hkVector4(0.04f, 0.02f, 0.0f), hkVector4(-0.04f, 0.02f, 0.0f), 0.14f),
			Locator(kOrigin, Quat(kIdentity)));
		m_charColliders[kColliderRUpperLeg].Init(
			hknpCapsuleShape::createCapsuleShape(hkVector4(-0.12f, -0.0f, -0.1f), hkVector4(-0.3f, -0.3f, -0.5f), 0.10f),
			Locator(kOrigin, Quat(kIdentity)));
		m_charColliders[kColliderLUpperLeg].Init(
			hknpCapsuleShape::createCapsuleShape(hkVector4(+0.12f, -0.0f, -0.1f), hkVector4(+0.3f, -0.3f, -0.5f), 0.10f),
			Locator(kOrigin, Quat(kIdentity)));
	}

	m_charCollidersReset = true;
	m_characterClimbsAsWallRope = b;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetGrabColliderEnabled(bool b)
{
	if (m_grabColliderDisableOverride)
		return;

	if (m_grabColliderOn != b)
	{
		m_grabColliderOn = b;
		m_grabCollider.m_enabled = b;
		if (m_grabColliderOn)
		{
			m_grabColliderInvalid = false;
			m_grabColliderReset = true;
			m_grabColliderBlendIn = 0.0f;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::UpdateCharacterCollision(Point_arg top)
{
	if (const Character* pChar = m_hCharacter.ToProcess())
	{
		if (m_charCollidersReset)
		{
			m_charColliders[kColliderTrunk].Reset(pChar->GetAnimData(), SID("spined"));
			m_charColliders[kColliderPelvis].Reset(pChar->GetAnimData(), SID("pelvis"));
			m_charColliders[kColliderRUpperLeg].Reset(pChar->GetAnimData(), m_characterClimbsAsWallRope ? SID("r_upper_leg") : SID("pelvis"));
			m_charColliders[kColliderLUpperLeg].Reset(pChar->GetAnimData(), m_characterClimbsAsWallRope ? SID("l_upper_leg") : SID("pelvis"));

			//m_charColliders[kColliderRShoulder].Reset(pChar->GetAnimData(), SID("r_shoulder"));
			//m_charColliders[kColliderLShoulder].Reset(pChar->GetAnimData(), SID("l_shoulder"));
			//m_charColliders[kColliderRElbow].Reset(pChar->GetAnimData(), SID("r_elbow"));
			//m_charColliders[kColliderLElbow].Reset(pChar->GetAnimData(), SID("l_elbow"));
			//m_charColliders[kColliderRKnee].Reset(pChar->GetAnimData(), SID("r_knee"));
			//m_charColliders[kColliderLKnee].Reset(pChar->GetAnimData(), SID("l_knee"));
			m_charCollidersReset = false;
		}
		else
		{
			for (U32F ii = 0; ii<kNumCharColliders; ii++)
			{
				m_charColliders[ii].Update(pChar->GetAnimData(), m_rope.GetTeleportThisFrame());
			}
		}

#if 0
		if (m_grabColliderOn)
		{
			U32F firstHandIndex = m_rightHandIsFirst ? kRightHand : kLeftHand;
			U32F secondHandIndex = m_rightHandIsFirst ? kLeftHand : kRightHand;
			const CharPointOnRope& firstHand = m_charPointsOnRope[firstHandIndex];
			const CharPointOnRope& secondHand = m_charPointsOnRope[secondHandIndex];

			Locator colliderLoc;
			bool colliderValid = false;
			if (firstHand.m_isOn && secondHand.m_isOn)
			{
				Point grabPos = firstHand.m_loc.GetTranslation();
				Vector toTop = SafeNormalize(top - grabPos, kUnitYAxis);
				Vector zdir = GetLocalZ(pChar->GetLocator().GetRotation());
				Quat rot = QuatFromLookAtDirs(zdir, toTop, false);
				Scalar lowerGrabZCorrect = Min(Dot(secondHand.m_loc.GetTranslation() - grabPos, zdir), Scalar(kZero));
				Point pos = grabPos + Rotate(rot, Vector(0.0f, 0.0f, -0.5f + lowerGrabZCorrect - m_rope.m_fRadius));
				colliderLoc = Locator(pos, rot);

				// For certain pose the grab collider just does not work and can cause more damage than good
				// @@JS: Even better would be to make the collider slide-in from safe once it's valid which in that case could be determined by the pin point being well under the box
				Point headPos = m_charColliders[kColliderTrunk].m_collider.m_loc.TransformPoint(Point(0.0f, 0.225f, 0.0f));
				Point headPosLs = colliderLoc.UntransformPoint(headPos);
				colliderValid = headPosLs.Y() <= -0.4f;
			}

			if (!colliderValid)
			{
				if (!m_grabColliderInvalid)
				{
					m_grabCollider.m_enabled = false;
					m_grabColliderInvalid = true;
				}
			}
			else
			{
				if (m_grabColliderInvalid)
				{
					m_grabCollider.m_enabled = true;
					m_grabColliderInvalid = false;
					m_grabColliderReset = true;
					m_grabColliderBlendIn = 0.0f;
				}

				m_grabColliderBlendIn = Min(1.0f, m_grabColliderBlendIn + g_havok.m_dt * 5.0f);
				colliderLoc.SetPosition(colliderLoc.GetPosition() - GetLocalZ(colliderLoc.GetRotation()) * (1.0f - m_grabColliderBlendIn));

				if (m_grabColliderReset)
					m_grabCollider.ResetLoc(colliderLoc);
				else
					m_grabCollider.UpdateLoc(colliderLoc, m_rope.GetTeleportThisFrame());
				m_grabColliderReset = false;
			}
		}
#endif
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::EnableHandCollision(const Character* pChar)
{
	U32F bodyIndex;
	const CompositeBody* pCompositeBody = pChar->GetCompositeBody();
	if (pCompositeBody)
	{
		//bodyIndex = pCompositeBody->FindBodyIndexByJointSid(SID("r_elbow"));
		//if (bodyIndex != CompositeBody::kInvalidBodyIndex)
		//	m_rope.RemoveIgnoreCollisionBody(pCompositeBody->GetBody(bodyIndex));
		//bodyIndex = pCompositeBody->FindBodyIndexByJointSid(SID("l_elbow"));
		//if (bodyIndex != CompositeBody::kInvalidBodyIndex)
		//	m_rope.RemoveIgnoreCollisionBody(pCompositeBody->GetBody(bodyIndex));
		bodyIndex = pCompositeBody->FindBodyIndexByJointSid(SID("r_palm"));
		if (bodyIndex != CompositeBody::kInvalidBodyIndex)
			m_rope.RemoveIgnoreCollisionBody(pCompositeBody->GetBody(bodyIndex));
		bodyIndex = pCompositeBody->FindBodyIndexByJointSid(SID("l_palm"));
		if (bodyIndex != CompositeBody::kInvalidBodyIndex)
			m_rope.RemoveIgnoreCollisionBody(pCompositeBody->GetBody(bodyIndex));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::DisableHandCollision(const Character* pChar)
{
	U32F bodyIndex;
	const CompositeBody* pCompositeBody = pChar->GetCompositeBody();
	if (pCompositeBody)
	{
		//bodyIndex = pCompositeBody->FindBodyIndexByJointSid(SID("r_elbow"));
		//if (bodyIndex != CompositeBody::kInvalidBodyIndex)
		//	m_rope.AddIgnoreCollisionBody(pCompositeBody->GetBody(bodyIndex));
		//bodyIndex = pCompositeBody->FindBodyIndexByJointSid(SID("l_elbow"));
		//if (bodyIndex != CompositeBody::kInvalidBodyIndex)
		//	m_rope.AddIgnoreCollisionBody(pCompositeBody->GetBody(bodyIndex));
		bodyIndex = pCompositeBody->FindBodyIndexByJointSid(SID("r_wrist"));
		if (bodyIndex != CompositeBody::kInvalidBodyIndex)
			m_rope.AddIgnoreCollisionBody(pCompositeBody->GetBody(bodyIndex));
		bodyIndex = pCompositeBody->FindBodyIndexByJointSid(SID("l_wrist"));
		if (bodyIndex != CompositeBody::kInvalidBodyIndex)
			m_rope.AddIgnoreCollisionBody(pCompositeBody->GetBody(bodyIndex));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetGrabPoints(Character* pChar, Point_arg grab0, Point_arg grab1, bool grabCollider, bool blend)
{
	// Set this as the rope of the character
	// Later before we get rid of the old Grab system we should do this in some high level code
	SetActiveCharacter(pChar, false);
	pChar->SetRope(this);
	SetGrabColliderEnabled(grabCollider);

	if (grab0.Y() > grab1.Y())
	{
		m_grabPoint[kGrabUpper] = grab0;
		m_grabPoint[kGrabLower] = grab1;
	}
	else
	{
		m_grabPoint[kGrabUpper] = grab1;
		m_grabPoint[kGrabLower] = grab0;
	}

	m_grabPointsValid = true;
	if (!blend)
		m_blendToGrab = 1.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::ClearGrabPoints()
{
	m_grabPointsValid = false;
	m_blendToGrab = 0.0f;
	if (!m_characterIsPersistant && m_hCharacter.HandleValid())
	{
		// Later before we get rid of the old Grab system we should do this in some high level code
		m_hCharacter.ToMutableProcess()->SetRope(nullptr);
	}
	SetGrabColliderEnabled(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static F32 FracPositive(F32 val)
{
	F32 fl = Floor(val);
	return val - fl;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::ReelOutRope(F32 oldUsedLen, F32 newUsedLen, const Locator& oldPinLoc, const Locator& newPinLoc, const Vector& handMoveIn, F32 oldhandRopeDist, F32 newHandRopeDist)
{
	if (oldUsedLen >= 0.0f && oldUsedLen < newUsedLen)
	{
		// The piece of rope that was stored is now out
		// We put it in nice circular coil to avoid ugly kinks
		F32 newLen = newUsedLen - oldUsedLen;
		F32 maxCoil = PI * 0.3f;
		U32 numCoils = (U32)(newLen / maxCoil) + 1;
		F32 coilLen = newLen / numCoils;
		F32 coilRad = coilLen / (2.0f * PI);

		Vector pinVel = (newPinLoc.GetTranslation() - oldPinLoc.GetTranslation()) * g_havok.m_invDt;

		F32 nodeRopeDist = oldUsedLen + m_rope.m_fSegmentLength;
		while (nodeRopeDist <= newUsedLen - m_rope.m_fSegmentLength)
		{
			I32F iSim = m_rope.AddSimPoint(nodeRopeDist);
			if (iSim >= 0)
			{
				nodeRopeDist = m_rope.GetSimRopeDist()[iSim];
				F32 angle = FracPositive((nodeRopeDist - oldUsedLen) / coilLen) * 2.0f * PI;
				F32 s = Sin(angle);
				F32 c = Cos(angle);
				Point p = kOrigin + Vector(0.0f, -1.0f+c, s) * coilRad;
				m_rope.GetSimPos()[iSim] = oldPinLoc.TransformPoint(p); // + pinVel * g_havok.m_dt;
				m_rope.GetSimVel()[iSim] = pinVel; // + Vector(0.0f, -10.0f, 0.0f); // - GetLocalZ(newPinLoc.GetRotation()) * newLen * g_havok.m_invDt;

				F32 copyRopeDist = Max(0.0f, oldUsedLen - (newUsedLen - nodeRopeDist));
				//m_rope.GetPosAndVel(copyRopeDist, m_rope.GetSimPos()[iSim], m_rope.GetSimVel()[iSim]);
				//m_rope.GetSimPos()[iSim] += pinVel * g_havok.m_dt;
				//m_rope.GetSimVel()[iSim] = pinVel; // - GetLocalZ(pinLoc.GetRotation()) * newLen * g_havok.m_invDt;
				//DebugDrawCross(pinLoc.TransformPoint(p), 0.02f, kColorGreen);
			}
			//if (nodeRopeDist >= newUsedLen - 0.001f)
			//	break;
			nodeRopeDist += m_rope.m_fSegmentLength;
			nodeRopeDist = Min(nodeRopeDist, newUsedLen);
		}

		/*F32 handTeleDist = Max(0.0f, newHandRopeDist - oldhandRopeDist);
		F32 pinTeleDist = Max(0.0f, newLen);

		Scalar pinMoveLen;
		Vector pinMove = SafeNormalize(newPinLoc.GetTranslation() - oldPinLoc.GetTranslation(), kZero, pinMoveLen);
		pinMove *= Min((F32)pinMoveLen, pinTeleDist);

		Scalar handMoveLen;
		Vector handMove = SafeNormalize(handMoveIn, kZero, handMoveLen);
		handMove *= Min((F32)handMoveLen, pinTeleDist);

		for (U32F ii = 0; ii<m_rope.GetNumSimPoints(); ii++)
		{
			F32 ropeDist = m_rope.GetSimRopeDist()[ii];
			if (ropeDist > newHandRopeDist && ropeDist < newUsedLen)
			{
				Scalar t = (ropeDist - newHandRopeDist) / (newUsedLen - newHandRopeDist);
				F32 teleDist = Lerp(handTeleDist, pinTeleDist, (F32)t);
				Point pos;
				Vector vel;
				m_rope.GetPosAndVel(ropeDist - teleDist, pos, vel);
				pos += Lerp(handMove, pinMove, t);
				m_rope.GetSimPos()[ii] = pos;
				m_rope.GetSimVel()[ii] = vel;
			}
		}*/
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::PinRope(const Locator& pinLoc, const Locator& oldPinLoc, F32 ropeDist, Point_arg storeOffset, Rope2::RopeNodeFlags flags)
{
	CheckStepNotRunning();

	if (ropeDist > m_rope.m_fLength)
		return;

	const Point storePoint = pinLoc.TransformPoint(storeOffset);
	const float offsetLen = Length(storeOffset - kOrigin);

	Vector pinVel = (pinLoc.GetTranslation() - oldPinLoc.GetTranslation()) * g_havok.m_invDt;
	m_rope.SetKeyframedPos(ropeDist, pinLoc.GetTranslation(), pinVel, flags);
	if (ropeDist + offsetLen - 0.01f < m_rope.m_fLength)
	{
		if (offsetLen > 0.001f && ropeDist + offsetLen < m_rope.m_fLength)
			m_rope.SetKeyframedPos(ropeDist + offsetLen, storePoint, pinVel, Rope2::kNodeKeyframedSeg);
		m_rope.SetKeyframedPos(m_rope.m_fLength, storePoint, pinVel, Rope2::kNodeKeyframedSeg);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 ProcessPhysRope::GetReelInDist(F32 newHandRopeDist, F32 pct)
{
	CheckStepNotRunning();

	F32 oldHandRopeDist = newHandRopeDist;
	for (I32F ii = 0; ii<m_rope.GetNumEdges(); ii++)
	{
		if ((m_rope.GetEdges()[ii].m_flags & kFirstCharPoint) != 0)
		{
			oldHandRopeDist = m_rope.GetEdges()[ii].m_ropeDist;
			break;
		}
	}

	return pct * (oldHandRopeDist - newHandRopeDist);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::ReelTeleport(F32 newHandRopeDist, F32 pct)
{
	F32 reelDist = GetReelInDist(newHandRopeDist, pct);
	if (reelDist > 0.0f)
		m_rope.ReelTeleport(reelDist);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetBodyConstrainedAtAnchor(RigidBodyHandle hBody, bool atAnchor, Point_arg bodyPointLs, F32 pullStrength, F32 pullStrengthMaxCosY, F32 pullStrengthY)
{
	m_hConstrainedBody = hBody;
	m_bodyConstrainedAtAnchor = atAnchor;
	m_constrainedBodyPivot = bodyPointLs;
	m_constrainedBodyPullStrength = pullStrength;
	m_constrainedBodyPullMaxCosY = pullStrengthMaxCosY;
	m_constrainedBodyPullStrengthY = pullStrengthY;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::EnableCollideWithCharacters(bool collide)
{
	CheckStepNotRunning();

	if (collide)
	{
		m_rope.m_layerMask |= Collide::kLayerMaskCharacterAndProp;
	}
	else
	{
		m_rope.m_layerMask &= ~Collide::kLayerMaskCharacterAndProp;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetTempLayerMask(Collide::LayerMask tempLayerMask, F32 timeoutSec)
{
	CheckStepNotRunning();

	ALWAYS_ASSERTF(!m_tempLayerMaskActive, ("%s: Attempt to set temp layer mask more than once", GetName()));
	ALWAYS_ASSERTF(!m_layerMaskCached, ("%s: Attempt to set temp layer mask while grab points valid", GetName()));

	// Temporarily filter out the requested layers.
	m_nCachedLayerMask = m_rope.m_layerMask;
	m_rope.m_layerMask &= tempLayerMask;
	m_tempLayerMaskActive = true;

	// NOTE: 0.0f means "infinite - no timeout", but right now we don't allow it
	if (timeoutSec > 0.0f)
		m_tempLayerMaskTimeout = timeoutSec;
	else
		m_tempLayerMaskTimeout = kRopeTempLayerMaskDefaultTimeout;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::ClearTempLayerMask()
{
	CheckStepNotRunning();

	if (m_tempLayerMaskActive)
	{
		m_rope.m_layerMask = m_nCachedLayerMask;
		m_tempLayerMaskActive = false;
	}
	m_tempLayerMaskTimeout = 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetCollisionIgnoreObject(NdGameObjectHandle hIgnoreObj)
{
	m_rope.m_hFilterIgnoreObject = hIgnoreObj;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::GetCollidedRopeInfo(F32& len, Point& lastCollisionPoint) const
{
	CheckStepNotRunning();

	for (I32F iHand = m_rope.GetNumEdges()-1; iHand >= 0; iHand--)
	{
		if ((m_rope.GetEdges()[iHand].m_flags & kFirstCharPoint) != 0)
		{
			if (iHand > 0 && (m_rope.GetEdges()[iHand-1].m_flags & Rope2::kNodeKeyframed) == 0)
			{
				len = m_rope.GetEdges()[iHand-1].m_ropeDist;
				lastCollisionPoint = m_rope.GetEdges()[iHand-1].m_pos;
				return;
			}
			break;
		}
	}

	if (m_anchorEdgeIndex >= 0)
	{
		const Rope2::EdgePoint& ePoint = m_rope.GetEdges()[m_anchorEdgeIndex];
		len = ePoint.m_ropeDist;
		lastCollisionPoint = ePoint.m_pos;
	}
	else
	{
		len = 0.0f;
		lastCollisionPoint = m_rope.GetRoot();
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
RigidBody* ProcessPhysRope::GetPinRigidBody() const
{
	CheckStepNotRunning();

	for (I32F iHand = m_rope.GetNumEdges() - 1; iHand >= 0; iHand--)
	{
		if ((m_rope.GetEdges()[iHand].m_flags & kFirstCharPoint) != 0)
		{
			if (iHand > 0 && (m_rope.GetEdges()[iHand - 1].m_flags & Rope2::kNodeKeyframed) == 0)
			{
				Rope2::EdgePointInfo info;
				m_rope.GetEdgeInfo(iHand - 1, info);
				if (info.m_numEdges)
					return info.m_edges[0].m_hRigidBody.ToBody();

				return nullptr;
			}
			break;
		}
	}

	if (m_anchorEdgeIndex >= 0)
	{
		Rope2::EdgePointInfo info;
		m_rope.GetEdgeInfo(m_anchorEdgeIndex, info);
		if (info.m_numEdges)
			return info.m_edges[0].m_hRigidBody.ToBody();
	}

	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ProcessPhysRope::GetCharacterCollidedStraightenRopeInfo(F32& len, I32F& anchorEdgeIndex, I32F& handEdgeIndex) const
{
	bool edges = GetCollidedStraightenRopeInfo(len, anchorEdgeIndex, handEdgeIndex, Rope2::kNodeKeyframed, kFirstCharPoint);
	if (handEdgeIndex >= 0 && anchorEdgeIndex >= 0)
	{
		return edges;
	}

	len = 0.0f;
	anchorEdgeIndex = -1;
	handEdgeIndex = -1;

	if (m_rope.GetNumSaveEdges() >= 2)
	{
		// We don't have hand on the rope but we can still use save edges to see if the rope collides

		F32 ropeDist = m_rope.GetSaveEdges()[0].m_ropeDist;
		for (I32 iAnchor = 0; iAnchor<m_rope.GetNumEdges(); iAnchor++)
		{
			if (m_rope.GetEdges()[iAnchor].m_ropeDist == ropeDist && (m_rope.GetEdges()[iAnchor].m_flags & Rope2::kNodeKeyframed) != 0)
			{
				anchorEdgeIndex = iAnchor;
				break;
			}
			if (m_rope.GetEdges()[iAnchor].m_ropeDist > ropeDist)
			{
				break;
			}
		}

		for (I32 ii = 2; ii<m_rope.GetNumSaveEdges()-1; ii++)
		{
			len += Dist(m_rope.GetSaveEdges()[ii].m_pos, m_rope.GetSaveEdges()[ii-1].m_pos);
		}
	}

	return m_rope.GetNumSaveEdges() > 2;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ProcessPhysRope::GetCollidedStraightenRopeInfo(F32& len, I32F& topEdgeIndex, I32F& bottomEdgeIndex, Rope2::RopeNodeFlags topFlags, Rope2::RopeNodeFlags bottomFlags) const
{
	CheckStepNotRunning();

	len = 0.0f;
	topEdgeIndex = -1;
	bottomEdgeIndex = -1;

	for (I32F iHand = m_rope.GetNumEdges()-1; iHand >= 0; iHand--)
	{
		if ((m_rope.GetEdges()[iHand].m_flags & bottomFlags) != 0)
		{
			bottomEdgeIndex = iHand;

			if (iHand == 0)
			{
				// Top not found
				return false;
			}

			if ((m_rope.GetEdges()[iHand-1].m_flags & topFlags) != 0)
			{
				// No edges, just straight line
				topEdgeIndex = iHand-1;
				return false;
			}

			for (I32F iEdge = iHand-2; iEdge >= 0; iEdge--)
			{
				if ((m_rope.GetEdges()[iEdge].m_flags & topFlags) != 0)
				{
					topEdgeIndex = iEdge;
					return true;
				}

				len += Dist(m_rope.GetEdges()[iEdge+1].m_pos, m_rope.GetEdges()[iEdge].m_pos);
			}

			// Top not found
			return false;
		}
	}

	// No bottom found
	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::GetCollidedPoints(I32 numPoints, Point *pointArray, Vector *edgeArray) const
{
	CheckStepNotRunning();

	I32 pointCount = 0;
	for (I32F i=0;i<numPoints;i++)
	{
		pointArray[i] = m_rope.GetPos(0);
		edgeArray[i] = Vector(kZero);
	}

	I32 anchor = -1;
	I32 hand = -1;
	for (I32F iHand = m_rope.GetNumEdges()-1; iHand >= 0; iHand--)
	{
		if ((m_rope.GetEdges()[iHand].m_flags & kFirstCharPoint) != 0)
		{
			if (iHand == 0)
			{
				// Root not keyframed
				return;
			}

			if ((m_rope.GetEdges()[iHand-1].m_flags & Rope2::kNodeKeyframed) != 0)
			{
				for (I32F i=0;i<numPoints;i++)
				{
					pointArray[i] = m_rope.GetEdges()[iHand-1].m_pos;
					I32F edgeIndex = -1;
					for (U32F ii = 0; ii<m_rope.GetEdges()[iHand-1].m_numEdges; ii++)
					{
						if (m_rope.GetEdges()[iHand-1].GetEdgeActive(ii))
						{
							edgeIndex = m_rope.GetEdges()[iHand-1].m_edgeIndices[ii];
							break;
						}
					}
					if (edgeIndex > -1)
					{
						Vector edgeDir(m_rope.GetColCache().m_pEdges[edgeIndex].m_vec);
						edgeArray[i] = SafeNormalize(edgeDir, edgeDir);;
					}
				}
				return;
			}

			hand = iHand - 1;

			for (I32F iAnchor = iHand-2; iAnchor >= 0; iAnchor--)
			{
				if ((m_rope.GetEdges()[iAnchor].m_flags & Rope2::kNodeKeyframed) != 0)
				{
					anchor = iAnchor;
					break;
				}
			}

			break;
		}
	}

	if (anchor < 0)
		return;

	for (I32F iEdge = anchor;iEdge <= hand;iEdge++)
	{
		if (pointCount >= numPoints)
		{
			pointCount--;
		}
		pointArray[pointCount++] = m_rope.GetEdges()[iEdge].m_pos;

		I32F edgeIndex = -1;
		for (U32F ii = 0; ii<m_rope.GetEdges()[iEdge].m_numEdges; ii++)
		{
			if (m_rope.GetEdges()[iEdge].GetEdgeActive(ii))
			{
				edgeIndex = m_rope.GetEdges()[iEdge].m_edgeIndices[ii];
				break;
			}
		}
		if (edgeIndex > -1)
		{
			Vector edgeDir(m_rope.GetColCache().m_pEdges[edgeIndex].m_vec);
			edgeArray[pointCount-1] = SafeNormalize(edgeDir, edgeDir);
		}
	}

	if (pointCount > 0)
	{
		for (I32F i=pointCount;i<numPoints;i++)
		{
			pointArray[i] = pointArray[pointCount-1];
			edgeArray[i] = edgeArray[pointCount-1];
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ProcessPhysRope::HasPin() const
{
	CheckStepNotRunning();
	if (m_anchorEdgeIndex < 0)
	{
		return false;
	}
	else if (m_handEdgeIndex >= 0)
	{
		return m_handEdgeIndex-m_anchorEdgeIndex > 1;
	}
	return m_rope.GetNumSaveEdges() > 2;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point ProcessPhysRope::GetPinPoint() const
{
	CheckStepNotRunning();
	if (m_anchorEdgeIndex < 0)
	{
		return m_rope.GetPos(0.0f);
	}
	else if (m_handEdgeIndex >= 0)
	{
		return m_rope.GetEdges()[m_handEdgeIndex-1].m_pos;
	}
	else if (m_rope.GetNumSaveEdges() > 2)
	{
		return m_rope.GetSaveEdges()[m_rope.GetNumSaveEdges()-2].m_pos;
	}
	return m_rope.GetEdges()[m_anchorEdgeIndex].m_pos;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 ProcessPhysRope::GetDistFromAnchorToPin() const
{
	CheckStepNotRunning();
	if (m_anchorEdgeIndex >= 0 && m_handEdgeIndex >= 0 && m_handEdgeIndex-m_anchorEdgeIndex > 1)
	{
		return Dist(m_rope.GetEdges()[m_anchorEdgeIndex].m_pos, m_rope.GetEdges()[m_anchorEdgeIndex+1].m_pos) + m_collidedStraigthenRopeLen;
	}
	else if (m_rope.GetNumSaveEdges() > 2)
	{
		return Dist(m_rope.GetSaveEdges()[0].m_pos, m_rope.GetSaveEdges()[1].m_pos) + m_collidedStraigthenRopeLen;
	}
	else
	{
		return 0.0f;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point ProcessPhysRope::GetEffectivePinPoint(Vector swingDir) const
{
	CheckStepNotRunning();

	I32F iHand;
	for (iHand = m_rope.GetNumEdges()-1; iHand >= 0; iHand--)
	{
		if ((m_rope.GetEdges()[iHand].m_flags & kFirstCharPoint) != 0)
			break;
	}

	if (iHand <= 0)
	{
		// Root not keyframed
		return m_rope.GetRoot();
	}

	Point handPnt = m_rope.GetEdges()[iHand].m_pos;
	Point pnt = handPnt;
	Vector firstSegDir(kZero);
	F32 radius = 0.0f;
	F32 swingFactor = 1.0f;
	I32F iEdge = iHand-1;
	for (; iEdge >= 0; iEdge--)
	{
		const Rope2::EdgePoint& ePoint = m_rope.GetEdges()[iEdge];
		Vector segVec = pnt - ePoint.m_pos;
		Scalar segLen;
		Vector segDir = SafeNormalize(segVec, kZero, segLen);
		radius += segLen * swingFactor;
		if (ePoint.m_flags & (Rope2::kNodeEdgeCorner|Rope2::kNodeKeyframed))
		{
			if (AllComponentsEqual(firstSegDir, kZero))
				firstSegDir = segDir;
			break;
		}

		I32F edgeIndex = -1;
		for (U32F ii = 0; ii<ePoint.m_numEdges; ii++)
		{
			if (ePoint.GetEdgeActive(ii))
			{
				edgeIndex = ePoint.m_edgeIndices[ii];
				break;
			}
		}

		if (edgeIndex >= 0)
		{
			Vector swingNorm = SafeNormalize(Cross(swingDir, segDir), kZero);
			if (!AllComponentsEqual(swingNorm, kZero))
			{
				// Swing dir perpendicular to the segDir
				Vector swingDirPerpSeg = Cross(segDir, swingNorm);

				Vector edgeDir(m_rope.GetColCache().m_pEdges[edgeIndex].m_vec);
				Vector edgeSwingNorm = SafeNormalize(Cross(edgeDir, segDir), kZero);
				if (!AllComponentsEqual(swingNorm, kZero))
				{
					// Edge dir perpendicular to the segDir
					Vector edgeDirPerpSeg = Cross(segDir, edgeSwingNorm);
					swingFactor *= Abs(Dot(edgeDirPerpSeg, swingDirPerpSeg));
					if (AllComponentsEqual(firstSegDir, kZero))
					{
						firstSegDir = segDir;
					}
					else
					{
						swingFactor *= Dot(swingDir, swingDirPerpSeg);
					}
					swingDir = edgeDir;
				}
			}
		}

		if (ePoint.m_flags & Rope2::kNodeKeyframed)
			break;

		pnt = ePoint.m_pos;
	}

	Point resPnt = handPnt - radius * firstSegDir;
	PHYSICS_ASSERTF(IsFinite(resPnt) && LengthSqr(resPnt - kOrigin) < 10000.0f * 10000.0f, ("Unstable result from GetEffectivePinPoint. iHand=%i, iEdge=%i, radius=%f, fSegDir=(%f,%f,%f)", iHand, iEdge, (float)radius, (float)firstSegDir.X(), (float)firstSegDir.Y(), (float)firstSegDir.Z()));
	return resPnt;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point ProcessPhysRope::ClosestPointOnRope(const Point& point, F32* pRopeDistOut) const
{
	float dist;
	return ClosestPointOnRope(0.0f, Segment(point, point), dist, pRopeDistOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point ProcessPhysRope::ClosestPointOnRope(F32 minDist, const Point& point, F32* pRopeDistOut) const
{
	float dist;
	return ClosestPointOnRope(minDist, Segment(point, point), dist, pRopeDistOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point ProcessPhysRope::ClosestPointOnRope(const Segment& checkSeg, float& retDist, F32* pRopeDistOut) const
{
	CheckStepNotRunning();
	return m_rope.ClosestPointOnRope(0.0f, checkSeg, retDist, pRopeDistOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const Point ProcessPhysRope::ClosestPointOnRope(F32 minDist, const Segment& checkSeg, float& retDist, F32* pRopeDistOut) const
{
	CheckStepNotRunning();
	return m_rope.ClosestPointOnRope(minDist, checkSeg, retDist, pRopeDistOut);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::GetGrabPoints(Point& grab1out, Point& grab2out)
{
	grab1out = m_grabPoint[0];
	grab2out = m_grabPoint[1];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetKeyframedRange(F32 ropeDistStart, F32 ropeDistEnd, Point pos, Vector vel)
{
	CheckStepNotRunning();
	m_rope.SetKeyframedPos(ropeDistStart, pos, vel, Rope2::kNodeKeyedVelocity);
	m_rope.SetKeyframedPos(ropeDistEnd, pos, vel, Rope2::kNodeKeyedVelocity|Rope2::kNodeKeyframedSeg);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsProcessPhysRope(const Process& proc)
{
	return (proc.IsType(SID("ProcessPhysRope")) || proc.IsType(SID("WallRope")) || proc.IsType(SID("ProcessFreeRope")));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::UpdateWind()
{
	PROFILE(Havok, UpdateWind);

	if (m_windFactor > 0.0f)
	{
		m_rope.WakeUp();

		const SceneWindow& sceneWindow = g_renderMgr.m_sceneWindow[0];
		const DC::RenderSettings* pRenderSettings = sceneWindow.GetRenderSettings();
		F32 time = ToSeconds(GetProcessClock()->GetCurTime());

		F32 lastUpdateRopeDist = -g_windUpdateDist;
		Vector windVec(kZero);
		for (I32F ii = 1; ii<m_rope.GetNumSimPoints(); ii++)
		{
			if ((m_rope.GetSimNodeFlags()[ii] & Rope2::kNodeKeyframed) == 0)
			{
				if (m_rope.GetSimRopeDist()[ii]-lastUpdateRopeDist > g_windUpdateDist)
				{
					Point ropePos = m_rope.GetSimPos()[ii];
					Vec3 wind = g_fxMgr.GetWindSimple(&pRenderSettings->m_wind, Vec3(ropePos.X(), ropePos.Y(), ropePos.Z()), time);
					windVec = Vector(wind.X(), wind.Y(), wind.Z());
					lastUpdateRopeDist = m_rope.GetSimRopeDist()[ii];
				}
				Vector dv = m_windFactor * windVec * (m_rope.GetSimRopeDist()[ii] - m_rope.GetSimRopeDist()[ii-1]);
				m_rope.GetSimVel()[ii] += dv;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetPhysBlend(F32 target, F32 time)
{
	m_desiredPhysBlend = Limit01(target);
	m_physBlendTime = time;
	if (time <= 0.0f)
		m_physBlend = m_desiredPhysBlend;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void AddWrappedGameObject(const Rope2& rope, const Rope2::EdgePointInfo& info, ListArray<NdGameObjectHandle>& outWrappedObjs)
{
	if (outWrappedObjs.Size() >= outWrappedObjs.Capacity())
		return;

	for (U32F e = 0; e < info.m_numEdges; e++)
	{
		const RigidBody* pRigidBody = info.m_edges[e].m_hRigidBody.ToBody();
		if (pRigidBody != nullptr && pRigidBody->GetOwnerHandle().HandleValid())
		{
			// check if existed in array.
			bool found = false;
			NdGameObjectHandle hObj = pRigidBody->GetOwnerHandle();
			if (Contains(outWrappedObjs, hObj))
			{
				found = true;
				break;
			}

			if (found)
				continue;

			outWrappedObjs.PushBack(hObj);

			if (outWrappedObjs.Size() >= outWrappedObjs.Capacity())
				break;
		}
	}

	ASSERT(outWrappedObjs.Size() <= outWrappedObjs.Capacity());
}

/// --------------------------------------------------------------------------------------------------------------- ///
const RigidBody* GetMostContributingRigidBodyFromEdgeInfo(const Rope2::EdgePointInfo& info)
{
	struct RigidBodyHelper
	{
		RigidBodyHelper()
			: m_count(0)
		{}

		static I32 Compare(const RigidBodyHelper& a, const RigidBodyHelper& b)
		{
			if (a.m_count > b.m_count)
				return -1;
			else if (a.m_count < b.m_count)
				return +1;
			else
				return 0;
		}

		const RigidBody* m_pBody;
		I32 m_count;
	};

	static const U32F kMaxNumSortHelpers = 8;
	RigidBodyHelper arrHelpers[kMaxNumSortHelpers];
	I32 numHelpers = 0;

	for (U32F e = 0; e < info.m_numEdges; e++)
	{
		const RigidBody* pRigidBody = info.m_edges[e].m_hRigidBody.ToBody();
		if (pRigidBody != nullptr)
		{
			bool existed = false;
			// first check already existed?
			for (U32F ii = 0; ii < numHelpers; ii++)
			{
				if (arrHelpers[ii].m_pBody == pRigidBody)
				{
					arrHelpers[ii].m_count++;
					existed = true;
					break;
				}
			}

			// add a new entry.
			if (!existed && numHelpers < kMaxNumSortHelpers)
			{
				arrHelpers[numHelpers].m_pBody = pRigidBody;
				arrHelpers[numHelpers].m_count = 1;
				numHelpers++;
			}
		}
	}

	if (numHelpers > 0)
	{
		// do the sorting
		QuickSort(arrHelpers, numHelpers, RigidBodyHelper::Compare);
		return arrHelpers[0].m_pBody;
	}
	else
	{
		return nullptr;
	}
}

bool ProcessPhysRope::CheckEdgePat(U32 iEdge, Pat patMask, EdgeOrientation orient) const
{
	Rope2::EdgePointInfo info;
	m_rope.GetEdgeInfo(iEdge, info);
	return CheckRopeEdgePat(info, patMask, orient);
}

/// --------------------------------------------------------------------------------------------------------------- ///
//void ProcessPhysRope::UpdateStraightDistToAnchor()
//{
//	CheckStepNotRunning();
//	UpdateStraightDistToAnchorInner();
//}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::UpdateStraightDistToAnchorInner()
{
	Point handPos;
	if (m_handEdgeIndex >= 0)
	{
		if (m_handEdgeIndex+1 < m_rope.GetNumEdges())
		{
			handPos = Lerp(m_rope.GetEdges()[m_handEdgeIndex].m_pos, m_rope.GetEdges()[m_handEdgeIndex+1].m_pos, kFirstHandUpOffset/(kFirstHandUpOffset+kFirstHandDownOffset));
		}
		else
		{
			handPos = m_rope.GetEdges()[m_handEdgeIndex].m_pos;
		}
	}
	else if (m_rope.GetNumSaveEdges() > 1)
	{
		handPos = m_rope.GetSaveEdges()[m_rope.GetNumSaveEdges()-1].m_pos;
	}
	else
	{
		handPos = m_rope.GetPos(0.0f);
	}
	Point anchorPos = m_anchorEdgeIndex >= 0 ? m_rope.GetEdges()[m_anchorEdgeIndex].m_pos : m_rope.GetPos(0.0f);
	UpdateStraightDistToAnchorInner(anchorPos, handPos);
}


/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::UpdateStraightDistToAnchorPreStep(const Point& handPos)
{
	Point anchorPos;
	U32F numKeysSoFar = m_rope.GetNumKeyPoints();
	if (numKeysSoFar == 0)
	{
		anchorPos = GetTranslation();
	}
	else
	{
		anchorPos = m_rope.GetKeyPos(numKeysSoFar-1);
	}

	UpdateStraightDistToAnchorInner(anchorPos, handPos);
}

/// --------------------------------------------------------------------------------------------------------------- ///
//void ProcessPhysRope::UpdateStraightDistToAnchor(Point_arg anchorPos, Point_arg handPos)
//{
//	CheckStepNotRunning();
//	UpdateStraightDistToAnchorInner(anchorPos, handPos);
//}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::UpdateStraightDistToAnchorInner(Point_arg anchorPos, Point_arg handPos)
{
	if (!m_bAttached)
	{
		m_lastEdgePoint = m_rope.GetPos(0.0f);
		m_straightDistToAnchor = Dist(m_lastEdgePoint, handPos);
		return;
	}

	F32 dist;
	Point lastEdgePoint;
	if (HasPin())
	{
		Point firstEdgePoint;
		if (m_handEdgeIndex >= 0)
		{
			firstEdgePoint = m_rope.GetEdges()[m_anchorEdgeIndex+1].m_pos;
			lastEdgePoint = m_rope.GetEdges()[m_handEdgeIndex-1].m_pos;
		}
		else
		{
			ASSERT(m_rope.GetNumSaveEdges() > 2);
			firstEdgePoint = m_rope.GetSaveEdges()[1].m_pos;
			lastEdgePoint = m_rope.GetSaveEdges()[m_rope.GetNumSaveEdges()-2].m_pos;
		}
		dist = Dist(anchorPos, firstEdgePoint) + m_collidedStraigthenRopeLen + Dist(handPos, lastEdgePoint);
	}
	else
	{
		lastEdgePoint = anchorPos;
		dist = Dist(anchorPos, handPos);
	}

	// Since edges now always maintain strained version of the rope we don't need the blends bellow
	//if (iRopeAnchorEdge < 0 || (m_rope.GetEdges()[iRopeHandEdge].m_flags & Rope2::kNodeStrained))
	{
		m_straightDistToAnchor = dist;
		m_lastEdgePoint = lastEdgePoint;
		return;
	}

	/*
	Point prevAnchorPos = m_rope.GetEdges()[iRopeAnchorEdge].m_pos;
	Point prevHandPos = m_rope.GetEdges()[iRopeHandEdge].m_pos;

	// We can only change the distance if we move or the anchor moves
	// This is because the ropeCollidedLen can change discontinuously
	F32 grabMove = Dist(prevHandPos, handPos);
	F32 anchorMove = Dist(prevAnchorPos, anchorPos);
	Seek(m_straightDistToAnchor, dist, 2.0f*(grabMove+anchorMove));
	Seek(m_lastEdgePoint, lastEdgePoint, 2.0f*(grabMove+anchorMove));
	*/
}

/// --------------------------------------------------------------------------------------------------------------- ///
// This will force the right hand to be first
void ProcessPhysRope::SetRightHandIsFirst(bool rightHandIsFirst)
{
	m_rightHandIsFirstRequest = rightHandIsFirst;
	m_rightHandIsFirstExternal = true;
	m_rightHandIsFirstSet = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// This will reset back to the default behavior
void ProcessPhysRope::ClearRightHandIsFirst()
{
	m_rightHandIsFirstExternal = false;
	m_rightHandIsFirstSet = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetFirstHandLock(bool on)
{
	if (!m_firstHandLock)
		m_rightHandLockRopeDistSet = false;
	m_firstHandLock = on;
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Collision check to prevent keyframing rope into collision
// static
void ProcessPhysRope::CheckCharPointLocatorCollision(Locator& loc, Point_arg savePos, F32 upDist, F32 downDist)
{
	F32 radius = downDist + upDist;
	F32 zOffset = upDist - downDist;
	Point target = loc.TransformPoint(Point(0.0f, 0.0f, zOffset));
	ShapeCastContact cnt;
	//DebugDrawLine(savePos, target, kColorWhite, kPrimDuration1FramePauseable);
	U32 numCnt = HavokSphereCastJob::Probe(savePos, target, radius, 1, &cnt, CollideFilter(Collide::kLayerMaskRope, Pat(1ULL << Pat::kRopeThroughShift)));
	if (numCnt)
	{
		Vector probeVec = target - savePos;
		Vector correctVec = probeVec * (cnt.m_t - 1.0f);
		Point hitPnt = target + correctVec;

		//DebugDrawLine(savePos, hitPnt, kColorRed, kPrimDuration1FramePauseable);
		//DebugDrawSphere(hitPnt, radius, kColorRed, kPrimDuration1FramePauseable);
		//g_prim.Draw( DebugCoordAxes(loc, 0.1f, PrimAttrib(), 2.0, kColorWhite), kPrimDuration1FramePauseable);

		loc.SetTranslation(loc.GetTranslation() + correctVec);

		//g_prim.Draw( DebugCoordAxes(loc, 0.1f), kPrimDuration1FramePauseable);
	}

	//HavokSphereCastJob::DebugDrawProbe(savePos, target, radius, numCnt, &cnt, ICollCastJob::DrawConfig(kPrimDuration1FramePauseable));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::KeyframeCharacterPoint(const CharPointOnRope& pointOld, CharPointOnRope& pointNew, F32& prevRopeDist, F32& ropeDist, const CharPointOnRope* pPrevOld, const CharPointOnRope* pPrevNew,
	F32 handRopeDistDiff, F32 maxSlideDist, F32 offsetUp, F32 offsetDown, U32 addFlags)
{
	Locator loc = pointNew.m_loc;

	// Figure out the rope dist
	if (!m_bAttached && !pPrevNew)
	{
		pointNew.m_ropeDist = ropeDist;
	}
	else
	{
		ropeDist += pPrevNew ? (F32)Dist(pPrevNew->m_loc.GetPosition(), pointNew.m_loc.GetPosition()) : 0.0f;
		F32 targetRopeDist = ropeDist; // + slack;
		targetRopeDist = Min(m_rope.m_fLength, targetRopeDist);
		if (!pointOld.m_isOn)
		{
			pointNew.m_ropeDist = targetRopeDist;
		}
		else if (!pointNew.m_lock)
		{
			F32 move = 0.0f;
			if (!pPrevNew)
			{
				move = handRopeDistDiff;
			}
			else if (pPrevOld->m_isOn)
			{
				move = Dist(pointNew.m_loc.GetPosition(), pPrevNew->m_loc.GetPosition()) - Dist(pointOld.m_loc.GetPosition(), pPrevOld->m_loc.GetPosition());
				move += pPrevNew->m_ropeDist - pPrevOld->m_ropeDist;
			}
			F32 dist = targetRopeDist - pointNew.m_ropeDist;
			if (dist < 0)
			{
				dist = Max(dist, move-maxSlideDist);
			}
			else
			{
				dist = Min(dist, move+maxSlideDist);
			}
			pointNew.m_ropeDist += dist;
			pointNew.m_ropeDist = MinMax(pointNew.m_ropeDist, ropeDist, m_rope.m_fLength);
		}
		ropeDist = pointNew.m_ropeDist;

		// Blend
		float dt = GetProcessDeltaTime();
		if (!pointOld.m_isOn)
		{
			pointNew.m_blendTime = 0.1f;
			pointNew.m_blendDist = Dist(loc.GetTranslation(), m_rope.GetPos(ropeDist) + m_rope.GetVel(ropeDist) * dt);
		}
		if (CameraManager::Get().DidCameraCutThisFrame())
		{
			pointNew.m_blendTime = 0.0f;
		}
		if (pointNew.m_blendTime > 0.0f)
		{
			F32 t = Max(0.0f, 1.0f - dt / pointNew.m_blendTime);
			pointNew.m_blendTime = t * pointNew.m_blendTime;
			pointNew.m_blendDist = t * pointNew.m_blendDist;

			Point ropePos;
			Vector ropeDir;
			m_rope.GetPosAndDir(ropeDist, ropePos, ropeDir, -GetLocalZ(loc.GetRotation()));
			ropePos += m_rope.GetVel(ropeDist) * dt;
			Locator ropeLoc(ropePos, QuatFromLookAt(-ropeDir, kUnitXAxis));
			F32 dist = Dist(loc.GetTranslation(), ropePos);
			F32 tLoc = Min(1.0f, pointNew.m_blendDist / dist);
			loc = Lerp(loc, ropeLoc, tLoc);
		}
	}
	ropeDist = Max(ropeDist, prevRopeDist+0.01f);

	// Keyframe it
	{
		F32 upDist = Min(ropeDist-prevRopeDist, offsetUp);
		F32 downDist = offsetDown;
		Point rightUpper = loc.TransformPoint(Point(0.0f, 0.0f, upDist));
		Point rightLower = loc.TransformPoint(Point(0.0f, 0.0f, -downDist));
		bool bStrained = true; //slack == 0.0f && (newSecond.m_ropeDist - newFirst.m_ropeDist) - handDist < 0.05f * handDist;
		m_rope.SetKeyframedPos(ropeDist-upDist, rightUpper, (bStrained ? (pPrevNew ? Rope2::kNodeKeyframedSeg : Rope2::kNodeKeyframed) : 0) | addFlags);
		//m_rope.SetKeyTwistDir(ropeDist-upDist, GetLocalY(pointNew.m_loc));
		F32 downRopeDist = Min(ropeDist+downDist, m_rope.m_fLength);
		m_rope.SetKeyframedPos(downRopeDist, rightLower, Rope2::kNodeKeyframedSeg);
		prevRopeDist = downRopeDist;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::AttachRopeToCharacter()
{
	const Character* pChar = m_hCharacter.ToProcess();
	if (!pChar)
		return;

	PROFILE(Rope, AttachRopeToCharacter);

	bool bCharAttached = false;
	CharPointOnRope newPoints[kCharPointCount];
	for (U32F ii = 0; ii<kCharPointCount; ii++)
	{
		newPoints[ii] = m_charPointsOnRope[ii];
		newPoints[ii].m_isOn = newPoints[ii].m_requestOn;
		if (newPoints[ii].m_isOn)
		{
			AttachIndex attachJoint = pChar->GetAttachSystem()->FindPointIndexById(newPoints[ii].m_attachSid);
			PHYSICS_ASSERT(attachJoint != AttachIndex::kInvalid);
			newPoints[ii].m_loc = pChar->GetAttachSystem()->GetLocator(attachJoint);
			bCharAttached = true;
			if (g_ropeMgr.m_debugPrintCharControls)
			{
				g_prim.Draw( DebugCoordAxes(newPoints[ii].m_loc, 0.1f), kPrimDuration1FramePauseable);
			}
		}
	}

#if SUPPORT_OLD_GRAB_POINTS
	if (!m_charHasControlAnimChannels && m_grabPointsValid)
	{
		// To support the old system for now
		bCharAttached = true;
	}
#endif

	Point savePos;
	{
		// Save point is at characters chest
		I32F saveJoint = pChar->GetAnimData()->FindJoint(SID("spined"));
		PHYSICS_ASSERT(saveJoint >= 0);
		savePos = pChar->GetAnimData()->m_jointCache.GetJointLocatorWs(saveJoint).GetTranslation();
	}

	if (m_handEdgeIndex < 0 && m_rope.GetNumSaveEdges() < 2)
	{
		// We have no hand and no save edges from previous frame. There is no way we can reliably keyframe hands on the rope
		// so for this frame we will only establish the save edges
		bCharAttached = false;
		if (!m_grabRopeDistSet)
		{
			// Ideally this would be set by user but if it wasn't we'll just get the point closest to the chest save pos
			ClosestPointOnRope(GetDistFromAnchorToPin(), savePos, &m_grabRopeDist);
			m_grabRopeDistSet = true;
		}
	}

	m_rope.SetSaveEdgesPos(savePos);
	if (m_grabRopeDistSet)
	{
		m_rope.SetSaveEdgesPosRopeDist(m_grabRopeDist);
		m_grabRopeDistSet = false;
	}

	if (!bCharAttached)
	{
		ResetUsedLength();
		return;
	}

	CharPointOnRope& newRight = newPoints[kRightHand];
	CharPointOnRope& newLeft = newPoints[kLeftHand];

	bool switchHands = false;
	if (!m_rightHandIsFirstSet && newRight.m_isOn && newLeft.m_isOn && (!m_charPointsOnRope[kRightHand].m_isOn || !m_charPointsOnRope[kLeftHand].m_isOn))
	{
		// We just put a hand on the rope. Guess which hand goes first. This should be pretty reliable.
		const Point rightPos = newRight.m_loc.GetTranslation();
		const Point leftPos = newLeft.m_loc.GetTranslation();
		const Vector rightDir = GetLocalZ(newRight.m_loc.GetRotation());
		const Vector leftDir = GetLocalZ(newLeft.m_loc.GetRotation());

		Vector rootRHandVec = GetTranslation() - rightPos;
		Vector rootLHandVec = GetTranslation() - leftPos;
		Scalar rootRDist = Length(rootRHandVec);
		Scalar rootLDist = Length(rootLHandVec);
		Scalar rootR = Dot(rootRHandVec, rightDir);
		Scalar rootL = Dot(rootLHandVec, leftDir);
		if (Min(rootRDist, rootLDist) < 0.2f && Abs(rootRDist - rootLDist) > 0.5f * Min(rootRDist, rootLDist))
		{
			m_rightHandIsFirst = rootRDist < rootLDist;
		}
		/*if (rootRDist < 0.03f)
			m_rightHandIsFirst = true;
		else if (rootLDist < 0.03f)
			m_rightHandIsFirst = false;
		else if (rootRDist < 0.15f && rootR/rootRDist > 0.8f)
			m_rightHandIsFirst = true;
		else if (rootLDist < 0.15f && rootL/rootLDist > 0.8f)
			m_rightHandIsFirst = false;*/
		else
		{
			Vector handVec = rightPos - leftPos;
			Scalar rightFirstR = Dot(handVec, rightDir);
			Scalar rightFirstL = Dot(handVec, leftDir);
			m_rightHandIsFirst = IsPositive(rightFirstR + rightFirstL);
		}
	}
	else if (!newRight.m_isOn && newLeft.m_isOn)
	{
		m_rightHandIsFirst = false;
	}
	else if (newRight.m_isOn && !newLeft.m_isOn)
	{
		m_rightHandIsFirst = true;
	}
	else if (m_rightHandIsFirstSet)
	{
		if (m_rightHandIsFirst != m_rightHandIsFirstRequest && newRight.m_isOn && newLeft.m_isOn && m_charPointsOnRope[kRightHand].m_isOn && m_charPointsOnRope[kLeftHand].m_isOn)
		{
			// We are being told to switch hands while both hands are on the rope the whole time
			// We will need to apply blend
			// @@JS: We should do that better than what we're doing now though. Try climbing up on free rope and stopping right during the first reach to see the problem.
			switchHands = true;
		}
		m_rightHandIsFirst = m_rightHandIsFirstRequest;
	}

#if SUPPORT_OLD_GRAB_POINTS
	if (!m_charHasControlAnimChannels && m_grabPointsValid)
	{
		// To support the old system for now
		newRight.m_isOn = m_grabPointsValid;
		newLeft.m_isOn = m_grabPointsValid;
		Scalar grabDist;
		Vector grabVec = SafeNormalize(m_grabPoint[kGrabUpper] - m_grabPoint[kGrabLower], kUnitZAxis, grabDist);
		Quat grabRot = QuatFromLookAt(grabVec, kUnitXAxis);
		newRight.m_loc.SetRotation(grabRot);
		newLeft.m_loc.SetRotation(grabRot);
		F32 d = Min((F32)grabDist, 0.05f);
		newRight.m_loc.SetTranslation(m_grabPoint[kGrabUpper] - d * grabVec);
		d = Min((F32)grabDist-d, 0.03f);
		newLeft.m_loc.SetTranslation(m_grabPoint[kGrabLower] + d * grabVec);
		m_rightHandIsFirst = true;
		m_slack = 0.0f;
		m_slackBetweenHands = 0.0f;
		switchHands = false;

		if (g_ropeMgr.m_debugDrawGrabPoints)
		{
			g_prim.Draw( DebugCross(m_grabPoint[kGrabUpper], 0.2f, kColorRed, PrimAttrib(0)), kPrimDuration1FramePauseable);
			g_prim.Draw( DebugCross(m_grabPoint[kGrabLower], 0.1f, kColorBlue, PrimAttrib(0)), kPrimDuration1FramePauseable);
		}
	}
#endif

	U32F firstHandIndex = m_rightHandIsFirst ? kRightHand : kLeftHand;
	U32F secondHandIndex = m_rightHandIsFirst ? kLeftHand : kRightHand;
	CharPointOnRope& oldFirst = m_charPointsOnRope[firstHandIndex];
	CharPointOnRope& newFirst = newPoints[firstHandIndex];
	CharPointOnRope& oldSecond = m_charPointsOnRope[secondHandIndex];
	CharPointOnRope& newSecond = newPoints[secondHandIndex];
	CharPointOnRope& oldThigh = m_charPointsOnRope[kLeftThigh];
	CharPointOnRope& newThigh = newPoints[kLeftThigh];
	CharPointOnRope& oldFoot = m_charPointsOnRope[kLeftFoot];
	CharPointOnRope& newFoot = newPoints[kLeftFoot];

	float dt = GetProcessDeltaTime();

	F32 ropeDist;
	F32 prevRopeDist = 0.0f;
	F32 anchorRopeDistChange = 0.0f;

	const F32 prevHandStraightRopeDist = GetHandStraightRopeDist();
	F32 handStraightRopeDist = prevHandStraightRopeDist;

	const F32 prevAnchorRopeDist = GetAnchorRopeDist();
	F32 anchorRopeDist = prevAnchorRopeDist;

	if (m_rope.GetNumKeyPoints() > 0)
	{
		anchorRopeDist = m_rope.GetKeyRopeDist(m_rope.GetNumKeyPoints()-1);
		prevRopeDist = anchorRopeDist;
		m_lastEdgePoint = m_rope.GetKeyPos(m_rope.GetNumKeyPoints()-1);
		anchorRopeDistChange = anchorRopeDist - prevAnchorRopeDist;
		m_rope.SetSaveStartEdgesPos(m_lastEdgePoint);
	}

	const Point firstHandPos = newFirst.m_isOn ? newFirst.m_loc.GetPosition() : savePos;

	bool limbColliderEnabled = true;
	if (m_bEdgesKeyframed && m_rope.GetNumKeyPoints() > 0)
	{
		ropeDist = prevRopeDist + Dist(m_lastEdgePoint, firstHandPos);
		m_reelInTime = 0.0f;
	}
	else if (m_bAttached)
	{
		UpdateStraightDistToAnchorPreStep(firstHandPos);
		handStraightRopeDist = anchorRopeDist + m_straightDistToAnchor;

		ropeDist = Max(prevRopeDist+0.01f, handStraightRopeDist);
		m_reelInTime = 0.0f;
	}
	else
	{
		m_lastEdgePoint = m_rope.GetPos(0.0f);
		m_straightDistToAnchor = Dist(m_lastEdgePoint, firstHandPos);
		ropeDist = oldFirst.m_isOn ? oldFirst.m_ropeDist : (oldSecond.m_isOn ? oldSecond.m_ropeDist : m_rope.m_fLength);
		prevRopeDist = 0.0f;
		if (m_reelInTime > 0.0f)
		{
			ropeDist = Max(0.0f, ropeDist * (1.0f - dt/m_reelInTime));
			m_reelInTime -= dt;
			// Reeling in is just too wild. We help the rope by ReelTeleporting it in
			ReelTeleport(ropeDist, 0.85f);
			limbColliderEnabled = false;
		}
	}
	ropeDist = Min(m_rope.m_fLength, ropeDist);

	if (!g_ndConfig.m_pNetInfo->IsNetActive())
	{
		// Pretty crazy stuff is going on as we reel-in the rope. Disabling collision with limb extremities helps a bit
		//EnableCharacterCollider(kColliderLElbow, limbColliderEnabled);
		//EnableCharacterCollider(kColliderRElbow, limbColliderEnabled);
		//EnableCharacterCollider(kColliderLKnee, limbColliderEnabled);
		//EnableCharacterCollider(kColliderRKnee, limbColliderEnabled);
	}

	CharPointOnRope* pPrevOld = nullptr;
	CharPointOnRope* pPrevNew = nullptr;

	F32 handRopeDistDiff = handStraightRopeDist - prevHandStraightRopeDist;

	if (newFirst.m_isOn && prevRopeDist < m_rope.m_fLength-0.1f)
	{
		pPrevNew = &newFirst;
		pPrevOld = &oldFirst;

		F32 minGrabEndOffset = 0.2f;
		if (newSecond.m_isOn)
		{
			minGrabEndOffset += Dist(newFirst.m_loc.GetTranslation(), newSecond.m_loc.GetTranslation()) + m_slackBetweenHands;
			if (newFoot.m_isOn)
			{
				minGrabEndOffset += Dist(newSecond.m_loc.GetTranslation(), newFoot.m_loc.GetTranslation());
			}
		}
		else if (newFoot.m_isOn)
		{
			minGrabEndOffset += Dist(newFirst.m_loc.GetTranslation(), newFoot.m_loc.GetTranslation());
		}

		bool bStrained = false;
		if (!m_bAttached)
		{
			newFirst.m_ropeDist = ropeDist;
		}
		else
		{
			newFirst.m_lock = m_firstHandLock;
			F32 firstTargetRopeDist = ropeDist + m_slack;
			firstTargetRopeDist = Min(m_rope.m_fLength-minGrabEndOffset, firstTargetRopeDist);
			if (!oldFirst.m_isOn)
			{
				newFirst.m_ropeDist = firstTargetRopeDist;
			}
			else if (!newFirst.m_lock)
			{
				F32 handMove = handRopeDistDiff;
				F32 dist = firstTargetRopeDist - newFirst.m_ropeDist;
				if (dist < 0)
				{
					F32 handMoveLimit = Min(handMove-kHandSlideSpeed*dt, 0.0f);
					dist = Max(dist, handMoveLimit);
				}
				else
				{
					F32 handMoveLimit = Max(handMove+kHandSlideSpeed*dt, 0.0f);
					dist = Min(dist, handMoveLimit);
				}
				newFirst.m_ropeDist += dist;
				newFirst.m_ropeDist = MinMax(newFirst.m_ropeDist, ropeDist, m_rope.m_fLength-minGrabEndOffset);
			}
			else
			{
				// Lock. But adjust the locked rope dist if anchor rope dist has changed (we're holding the rope taut while the hook is still wrapping around something)
				newFirst.m_ropeDist += anchorRopeDistChange;
				newFirst.m_ropeDist = Min(newFirst.m_ropeDist, m_rope.m_fLength-0.1f);
			}
			bStrained = m_slack <= 0.0f;
		}
		ASSERT(newFirst.m_ropeDist <= m_rope.m_fLength);
		ropeDist = newFirst.m_ropeDist;
		ropeDist = Max(ropeDist, prevRopeDist+0.01f);

		// Blend
		Locator firstLoc = newFirst.m_loc;
		if (m_firstStepDone && (!oldFirst.m_isOn || switchHands))
		{
			newFirst.m_blendTime = 0.1f;
			newFirst.m_blendDist = Dist(firstLoc.GetTranslation(), m_rope.GetPos(ropeDist) + m_rope.GetVel(ropeDist) * dt);
		}
		if (CameraManager::Get().DidCameraCutThisFrame())
		{
			newFirst.m_blendTime = 0.0f;
		}
		if (newFirst.m_blendTime > 0.0f)
		{
			F32 t = Max(0.0f, 1.0f - dt / newFirst.m_blendTime);
			newFirst.m_blendTime = t * newFirst.m_blendTime;
			newFirst.m_blendDist = t * newFirst.m_blendDist;

			Point ropePos;
			Vector ropeDir;
			m_rope.GetPosAndDir(ropeDist, ropePos, ropeDir, -GetLocalZ(firstLoc.GetRotation()));
			ropePos += m_rope.GetVel(ropeDist) * dt;
			Locator ropeLoc(ropePos, QuatFromLookAt(-ropeDir, kUnitXAxis));
			F32 dist = Dist(firstLoc.GetTranslation(), ropePos);
			F32 tLoc = Min(1.0f, newFirst.m_blendDist / dist);
			firstLoc = Lerp(firstLoc, ropeLoc, tLoc);
		}

		// Blend on releasing the previous first hand
		bool releasingHandKeyframed = false;
		if (!newSecond.m_isOn)
		{
			if (!bStrained || newSecond.m_ropeDist < prevRopeDist + 0.05f || oldSecond.m_ropeDist > ropeDist - 0.05f || CameraManager::Get().DidCameraCutThisFrame())
			{
				newSecond.m_blendTime = 0.0f;
			}
			else
			{
				Scalar toTopLen;
				Vector toTop = SafeNormalize(m_lastEdgePoint - firstLoc.GetTranslation(), kZero, toTopLen);
				F32 handDist = ropeDist - oldSecond.m_ropeDist;
				Point straightPos = firstLoc.GetTranslation() + handDist * toTop;
				if (handDist < toTopLen - 0.1f && handDist > 0.05f)
				{
					if (oldSecond.m_isOn)
					{
						newSecond.m_blendTime = 0.1f;
						newSecond.m_blendDist = Dist(straightPos, m_rope.GetPos(oldSecond.m_ropeDist) + m_rope.GetVel(oldSecond.m_ropeDist) * dt);
					}
					if (newSecond.m_blendTime > 0.0f)
					{
						F32 t = Max(0.0f, 1.0f - dt / newSecond.m_blendTime);
						newSecond.m_blendTime = t * newSecond.m_blendTime;
						newSecond.m_blendDist = t * newSecond.m_blendDist;

						if (newSecond.m_blendDist > 0.01f)
						{
							Point ropePos;
							Vector ropeDir;
							m_rope.GetPosAndDir(oldSecond.m_ropeDist, ropePos, ropeDir, -toTop);
							ropePos += m_rope.GetVel(oldSecond.m_ropeDist) * dt;
							Locator ropeLoc(ropePos, QuatFromLookAt(-ropeDir, kUnitXAxis));
							F32 dist = Dist(straightPos, ropePos);
							F32 tLoc = Min(1.0f, newSecond.m_blendDist / dist);
							Locator loc(straightPos, QuatFromLookAt(toTop, kUnitXAxis));
							loc = Lerp(loc, ropeLoc, tLoc);

							if (!m_bDisableHandRayCastCheck)
							{
								// Collision check to prevent keyframing rope into collision
								CheckCharPointLocatorCollision(loc, savePos, kSecondHandUpOffset, kSecondHandDownOffset);
							}

							// Keyframe the releasing hand (ex-first)
							{
								Point rightUpper = loc.TransformPoint(Point(0.0f, 0.0f, kSecondHandUpOffset));
								Point rightLower = loc.TransformPoint(Point(0.0f, 0.0f, -kSecondHandDownOffset));

								Rope2::RopeNodeFlags flags;
								if (m_bEdgesKeyframed)
								{
									flags = Rope2::kNodeKeyframedSeg;
								}
								else
								{
									flags = Rope2::kNodeStrained | (m_useSaveStrainedPos ? Rope2::kNodeUseSavePos : Rope2::kNodeNoEdgeDetection);
								}
								flags |= kFirstCharPoint;

								m_firstCharacterKeyRopeDist = oldSecond.m_ropeDist-kSecondHandUpOffset;
								m_firstCharacterKeyPos = rightUpper;

								m_rope.SetKeyframedPos(oldSecond.m_ropeDist-kSecondHandUpOffset, rightUpper, flags);
								//m_rope.SetKeyTwistDir(oldSecond.m_ropeDist-kSecondHandUpOffset, GetLocalY(loc));
								m_rope.SetKeyframedPos(oldSecond.m_ropeDist+kSecondHandDownOffset, rightLower, Rope2::kNodeKeyframedSeg);
								releasingHandKeyframed = true;
							}
						}
					}
				}
			}
		}

		// Keyframe the first hand
		{
			Rope2::RopeNodeFlags flags;
			if (releasingHandKeyframed)
			{
				flags = Rope2::kNodeKeyframedSeg;
			}
			else
			{
				if (!m_bDisableHandRayCastCheck)
				{
					// Collision check to prevent keyframing rope into collision
					CheckCharPointLocatorCollision(firstLoc, savePos, kFirstHandUpOffset, kFirstHandDownOffset);
				}

				if (m_bEdgesKeyframed)
					flags = Rope2::kNodeKeyframedSeg;
				else
					flags = (bStrained ? Rope2::kNodeStrained : 0) | (m_useSaveStrainedPos ? Rope2::kNodeUseSavePos : 0);
				flags |= kFirstCharPoint;
			}

			F32 upDist = Min(ropeDist-prevRopeDist, kFirstHandUpOffset);
			F32 downDist = kFirstHandDownOffset;
			Point rightUpper = firstLoc.TransformPoint(Point(0.0f, 0.0f, upDist));
			Point rightLower = firstLoc.TransformPoint(Point(0.0f, 0.0f, -downDist));
			m_rope.SetKeyframedPos(ropeDist-upDist, rightUpper, flags);
			//m_rope.SetKeyTwistDir(ropeDist-upDist, GetLocalY(firstLoc));
			F32 downRopeDist = Min(ropeDist+downDist, m_rope.m_fLength);
			m_rope.SetKeyframedPos(downRopeDist, rightLower, Rope2::kNodeKeyframedSeg);
			prevRopeDist = downRopeDist;

			if (!releasingHandKeyframed)
			{
				m_firstCharacterKeyRopeDist = ropeDist-upDist;
				m_firstCharacterKeyPos = rightUpper;
			}
		}

		if (newSecond.m_isOn && prevRopeDist < m_rope.m_fLength-0.1f)
		{
			pPrevNew = &newSecond;
			pPrevOld = &oldSecond;

			newSecond.m_lock = false;
			F32 handDist = Dist(newFirst.m_loc.GetPosition(), newSecond.m_loc.GetPosition());
			ropeDist += handDist;
			F32 secondTargetRopeDist = ropeDist + m_slackBetweenHands;
			secondTargetRopeDist = Min(m_rope.m_fLength, secondTargetRopeDist);
			if (!oldSecond.m_isOn)
			{
				newSecond.m_ropeDist = secondTargetRopeDist;
			}
			else if (!newSecond.m_lock)
			{
				F32 handMove = 0.0f;
				if (oldFirst.m_isOn)
				{
					handMove = Dist(newSecond.m_loc.GetPosition(), newFirst.m_loc.GetPosition()) - Dist(oldSecond.m_loc.GetPosition(), oldFirst.m_loc.GetPosition());
					handMove += newFirst.m_ropeDist - oldFirst.m_ropeDist;
				}
				F32 dist = secondTargetRopeDist - newSecond.m_ropeDist;
				if (dist < 0)
				{
					F32 handMoveLimit = Min(handMove-kHandSlideSpeed*dt, 0.0f);
					dist = Max(dist, handMoveLimit);
				}
				else
				{
					F32 handMoveLimit = Max(handMove+kHandSlideSpeed*dt, 0.0f);
					dist = Min(dist, handMoveLimit);
				}
				newSecond.m_ropeDist += dist;
				newSecond.m_ropeDist = MinMax(newSecond.m_ropeDist, ropeDist, m_rope.m_fLength);
			}
			ASSERT(newSecond.m_ropeDist <= m_rope.m_fLength);
			ropeDist = newSecond.m_ropeDist;
			ropeDist = Max(ropeDist, prevRopeDist+0.01f);

			// Blend
			Locator secondLoc = newSecond.m_loc;
			if (m_firstStepDone && (!oldSecond.m_isOn || switchHands))
			{
				newSecond.m_blendTime = 0.1f;
				newSecond.m_blendDist = Dist(secondLoc.GetTranslation(), m_rope.GetPos(ropeDist) + m_rope.GetVel(ropeDist) * dt);
			}
			if (CameraManager::Get().DidCameraCutThisFrame())
			{
				newSecond.m_blendTime = 0.0f;
			}
			if (newSecond.m_blendTime > 0.0f)
			{
				F32 t = Max(0.0f, 1.0f - dt / newSecond.m_blendTime);
				newSecond.m_blendTime = t * newSecond.m_blendTime;
				newSecond.m_blendDist = t * newSecond.m_blendDist;

				Point ropePos;
				Vector ropeDir;
				m_rope.GetPosAndDir(ropeDist, ropePos, ropeDir, -GetLocalZ(secondLoc.GetRotation()));
				ropePos += m_rope.GetVel(ropeDist) * dt;
				Locator ropeLoc(ropePos, QuatFromLookAt(-ropeDir, kUnitXAxis));
				F32 dist = Dist(secondLoc.GetTranslation(), ropePos);
				F32 tLoc = Min(1.0f, newSecond.m_blendDist / dist);
				secondLoc = Lerp(secondLoc, ropeLoc, tLoc);
			}

			// Keyframe the second hand
			{
				if (!m_bDisableHandRayCastCheck)
				{
					// Collision check to prevent keyframing rope into collision
					CheckCharPointLocatorCollision(secondLoc, savePos, kSecondHandUpOffset, kSecondHandDownOffset);
				}

				F32 upDist = Min(ropeDist-prevRopeDist, kSecondHandUpOffset);
				F32 downDist = kSecondHandDownOffset;
				Point rightUpper = secondLoc.TransformPoint(Point(0.0f, 0.0f, upDist));
				Point rightLower = secondLoc.TransformPoint(Point(0.0f, 0.0f, -downDist));
				bStrained = m_slackBetweenHands == 0.0f && (newSecond.m_ropeDist - newFirst.m_ropeDist) - handDist < 0.05f * handDist;
				m_rope.SetKeyframedPos(ropeDist-upDist, rightUpper, bStrained ? Rope2::kNodeKeyframedSeg : 0);
				//m_rope.SetKeyTwistDir(ropeDist-upDist, -GetLocalY(secondLoc));
				F32 downRopeDist = Min(ropeDist+downDist, m_rope.m_fLength);
				m_rope.SetKeyframedPos(downRopeDist, rightLower, Rope2::kNodeKeyframedSeg);
				prevRopeDist = downRopeDist;
			}

			if (m_firstHandLock && !m_rightHandIsFirst)
			{
				m_rightHandLockRopeDistSet = true;
			}
		}
	}

	U32 addFlags = newFirst.m_isOn ? 0 : Rope2::kNodeStrained | kFirstCharPoint | (m_useSaveStrainedPos ? Rope2::kNodeUseSavePos : 0);
	if (newThigh.m_isOn && prevRopeDist < m_rope.m_fLength-0.1f)
	{
		KeyframeCharacterPoint(oldThigh, newThigh, prevRopeDist, ropeDist, pPrevOld, pPrevNew, handRopeDistDiff, kHandSlideSpeed*dt, kSecondHandUpOffset, kSecondHandDownOffset, addFlags);
		pPrevOld = &oldThigh;
		pPrevNew = &newThigh;
		addFlags = 0;
	}

	if (newFoot.m_isOn && prevRopeDist < m_rope.m_fLength-0.1f)
	{
		KeyframeCharacterPoint(oldFoot, newFoot, prevRopeDist, ropeDist, pPrevOld, pPrevNew, handRopeDistDiff, kHandSlideSpeed*dt, kSecondHandUpOffset, kSecondHandDownOffset, addFlags);
		pPrevOld = &oldFoot;
		pPrevNew = &newFoot;
	}

	m_usedLength = -1.0f;

	if (g_printCharPointsOnRope)
	{
		MsgPriorityJanitor msgPrioJan(MSG_PRIO_PROC(kMsgPrioRope));
		const char* charName = DevKitOnly_StringIdToString(pChar->GetLookId());
		MsgConPauseable("%s Straight Hand Rope Dist: %.2f\n", charName, GetHandStraightRopeDist());
		MsgConPauseable("%s Rope First Hand: %.2f (%s)\n", charName, newFirst.m_isOn ? newFirst.m_ropeDist : -1.0f, m_rightHandIsFirstSet ? "right" : "left");
		MsgConPauseable("%s Rope Second Hand: %.2f (%s)\n", charName, newSecond.m_isOn ? newSecond.m_ropeDist : -1.0f, m_rightHandIsFirstSet ? "left" : "right");
	}

	// Copy back
	for (U32F ii = 0; ii<kCharPointCount; ii++)
	{
		m_charPointsOnRope[ii] = newPoints[ii];
	}

	m_rope.m_minCharCollisionDist = newFirst.m_isOn ? newFirst.m_ropeDist : 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ProcessPhysRope::GetAnimCustomChannelControlValue(StringId64 channelId, F32& valueOut) const
{
	if (!m_hCharacter.HandleValid())
		return false;

	switch (channelId.GetValue())
	{
		case SID_VAL("Rope.RightHandOn"):		valueOut = m_rightHandOn; return true;
		case SID_VAL("Rope.LeftHandOn"):		valueOut = m_leftHandOn; return true;
		case SID_VAL("Rope.RightHandFirst"):	valueOut = m_rightHandIsFirstSetVal; return true;
		case SID_VAL("Rope.Slack"):				valueOut = m_slack; return true;
		case SID_VAL("Rope.SlackBetweenHands"):	valueOut = m_slackBetweenHands; return true;
		case SID_VAL("Rope.SlackToPin"):		valueOut = 0.0; return false;
		case SID_VAL("Rope.LeftThighOn"):		valueOut = m_charPointsOnRope[kLeftThigh].m_requestOn; return true;
		case SID_VAL("Rope.LeftFootOn"):		valueOut = m_charPointsOnRope[kLeftFoot].m_requestOn; return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::UpdateCharacterRopeControl()
{
	m_charHasControlAnimChannels = false;

	const Character* pChar = m_hCharacter.ToProcess();
	if (!pChar)
		return;

	AnimControl* pAnimControl = pChar->GetAnimControl();

	if (AnimStateLayer* pBaseLayer = pAnimControl->GetBaseStateLayer())
	{
		RopeChannels ropeChannelsDefault;
		RopeChannelsBlender blender;
		RopeChannels ropeChannels = blender.BlendForward(pBaseLayer, ropeChannelsDefault);

		// Blend in other layers that have possibly rope related animation playing
		static const StringId64 s_layers[] =
		{
			SID("wall-reach-layer"),				// Prio 170
			SID("reach-layer"),				// Prio 170
			SID("ClaspPartial"),			// Prio 196
			SID("WeaponPartial"),			// Prio 196
			SID("WeaponPartialAlt"),			// Prio 196
			SID("grapple-grip-layer"),		// Prio 197
			SID("grapple-reel-layer"),		// Prio 197
			SID("grapple-unhook-layer"),	// Prio 197
			SID("gesture-1")				// Prio 203
		};
		const U32F kNumLayers = ARRAY_COUNT(s_layers);

		for (U32F ii = 0; ii < kNumLayers; ii++)
		{
			if (const AnimStateLayer* pLayer = static_cast<const AnimStateLayer*>(pAnimControl->GetLayerById(s_layers[ii])))
			{
				RopeChannels ropeChannelsLayer = blender.BlendForward(pLayer, ropeChannelsDefault);
				F32 fade = pLayer->GetCurrentFade();
				ropeChannels = blender.BlendDataInIfPresent(ropeChannels, ropeChannelsLayer, fade, fade, fade);
			}
		}

		m_charHasControlAnimChannels = ropeChannels.m_channels[0].m_blend > 0.0f;
		if (!m_charHasControlAnimChannels)
			return;

		//m_rightHandOn				= Lerp(m_charPointsOnRope[kRightHand].m_requestOn ? 1.0f : 0.0f, ropeChannels.m_channels[0].m_value, ropeChannels.m_channels[0].m_blend);
		m_rightHandOn				= Lerp(0.0f, ropeChannels.m_channels[0].m_value, ropeChannels.m_channels[0].m_blend);
		//m_leftHandOn				= Lerp(m_charPointsOnRope[kLeftHand].m_requestOn ? 1.0f : 0.0f, ropeChannels.m_channels[1].m_value, ropeChannels.m_channels[1].m_blend);
		m_leftHandOn				= Lerp(0.0f, ropeChannels.m_channels[1].m_value, ropeChannels.m_channels[1].m_blend);
		m_rightHandIsFirstSetVal	= ropeChannels.m_channels[2].m_value;
		F32 slack					= Lerp(m_slack, ropeChannels.m_channels[3].m_value, ropeChannels.m_channels[3].m_blend);
		F32 slackBetweenHands		= Lerp(m_slackBetweenHands, ropeChannels.m_channels[4].m_value, ropeChannels.m_channels[4].m_blend);
		F32 leftThighOn				= Lerp(0.0f, ropeChannels.m_channels[6].m_value, ropeChannels.m_channels[6].m_blend);
		F32 leftFoothOn				= Lerp(0.0f, ropeChannels.m_channels[7].m_value, ropeChannels.m_channels[7].m_blend);

		// @@JS: U4 Ship it hack!
		if (g_hTaskManager.ToProcess() && g_hTaskManager.ToProcess()->m_subNodeName == SID("orp-highroad-swing-rope") && pChar->IsType(SID("Player")))
		{
			slackBetweenHands = 0.0f;
		}

		SetRightHandOn(m_rightHandOn > 0.9f);
		SetLeftHandOn(m_leftHandOn > 0.9f);
		SetLeftThighOn(leftThighOn > 0.9f);
		SetLeftFootOn(leftFoothOn > 0.9f);

		if (ropeChannels.m_channels[2].m_blend > 0.5f)
		{
			m_rightHandIsFirstSet = true;
			m_rightHandIsFirstRequest = m_rightHandIsFirstSetVal > 0.5f;
		}
		else
		{
			m_rightHandIsFirstSet = m_rightHandIsFirstExternal;
			m_rightHandIsFirstSetVal = m_rightHandIsFirstSet ? 1.0f : 0.0f;
		}

		if (!m_slackOverrideAnim)
		{
			SetSlack(Max(0.0f, slack));
		}

		SetSlackBetweenHands(g_ndConfig.m_pNetInfo->IsNetActive() ? 0.0f : Max(0.0f, slackBetweenHands));

		if (FALSE_IN_FINAL_BUILD(g_ropeMgr.m_debugPrintCharControls))
		{
			MsgPriorityJanitor msgPrioJan(MSG_PRIO_PROC(kMsgPrioRope));
			const char* charName = DevKitOnly_StringIdToString(pChar->GetLookId());
			MsgConPauseable("%s Rope Right Hand On: %.2f (%.2f)\n", charName, ropeChannels.m_channels[0].m_value, ropeChannels.m_channels[0].m_blend);
			MsgConPauseable("%s Rope Left Hand On: %.2f (%.2f)\n", charName, ropeChannels.m_channels[1].m_value, ropeChannels.m_channels[1].m_blend);
			MsgConPauseable("%s Rope Right Hand First: %.2f (%.2f)\n", charName, ropeChannels.m_channels[2].m_value, ropeChannels.m_channels[2].m_blend);
			MsgConPauseable("%s Rope Slack: %.4f (%.2f)\n", charName, ropeChannels.m_channels[3].m_value, ropeChannels.m_channels[3].m_blend);
			MsgConPauseable("%s Rope Slack Between Hands: %.2f (%.2f)\n", charName, ropeChannels.m_channels[4].m_value, ropeChannels.m_channels[4].m_blend);
			MsgConPauseable("%s Rope Left Thigh On: %.2f (%.2f)\n", charName, ropeChannels.m_channels[6].m_value, ropeChannels.m_channels[6].m_blend);
			MsgConPauseable("%s Rope Left Foot On: %.2f (%.2f)\n", charName, ropeChannels.m_channels[7].m_value, ropeChannels.m_channels[7].m_blend);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::HandleTriggeredCharacterEffect(const EffectAnimInfo* pEffectAnimInfo, Character* pChar)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::SetApplySwingImpulseOnAnchor(RigidBodyHandle hAnchorBody, F32 impulseMult, F32 lockedRopedDist)
{
	m_hAnchorSwingImpulseBody = hAnchorBody;
	m_anchorSwingImpulseMult = impulseMult;
	m_anchorSwingImpulseRopedDist = lockedRopedDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::ApplySwingImpulseOnAnchor()
{
	if (m_handEdgeIndex < 1 || m_anchorEdgeIndex < 0)
	{
		return;
	}

	F32 distToAnchor = GetHandStraightDistToAnchor();
	if (distToAnchor < 0.5f * (m_anchorSwingImpulseRopedDist - 0.5f))
	{
		// @@JS: This is just crap
		// Rope is not strained, we are not actually pulling on it
		return;
	}

	Point handPos = m_rope.GetEdges()[m_handEdgeIndex].m_pos;
	Point anchorPos = m_rope.GetEdges()[m_anchorEdgeIndex].m_pos;

	Vector handDir;
	U32F iLastEdge = m_handEdgeIndex;
	do
	{
		iLastEdge--;
		handDir = SafeNormalize(m_rope.GetEdges()[iLastEdge].m_pos - handPos, kZero);
	} while (iLastEdge > m_anchorEdgeIndex && AllComponentsEqual(handDir, kZero));

	Vector anchorDir;
	U32F iFirstEdge = m_anchorEdgeIndex;
	do
	{
		iFirstEdge++;
		anchorDir = SafeNormalize(m_rope.GetEdges()[iFirstEdge].m_pos - anchorPos, kZero);
	} while (iFirstEdge < iLastEdge && AllComponentsEqual(anchorDir, kZero));

	F32 impulse = Max(Scalar(kZero), Dot(handDir, -g_havok.m_gravity)) * g_havok.m_dt * 80.0f * 2.0f; // Drake's mass 80kg, double for now for somw dynamic effects
	impulse *= m_anchorSwingImpulseMult;
	m_hAnchorSwingImpulseBody.ToBody()->ApplyPointImpulse(anchorPos, impulse * anchorDir);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::FillPhysRopeNetData(PhysRopeNetData *pData)
{
	// We'll send all edge points between the hook and the hand
	const I32F handEdgeIndex = GetHandEdgeIndex();
	const int maxGrapplePoints = Max(0, Min(handEdgeIndex - 1, I32F(Rope2NetData::kMaxDataPoints)));

	Rope2NetData &netPoints = pData->m_ropePoints;
	netPoints.m_numPoints = maxGrapplePoints;

	for (U32F i = 0; i < maxGrapplePoints; i++)
	{
		const int edge = (i == maxGrapplePoints - 1) ? (handEdgeIndex - 1) : i + 1;
		const Rope2::EdgePoint &ep = m_rope.GetEdges()[edge];
		netPoints.m_points[i] = ep.m_pos;
	}

	pData->m_rightHandOn = GetCharPointOnRope(ProcessPhysRope::kRightHand).m_isOn;
	pData->m_leftHandOn = GetCharPointOnRope(ProcessPhysRope::kLeftHand).m_isOn;
	pData->m_rightHandFirst = GetRightHandIsFirst();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessPhysRope::EstimateReelInPosAndDir(F32 dt, Point& posOut, Vector& dirOut)
{
	// This tries to estimate where the end point of the rope will be during reel in
	// We use this for positioning the hook because we can't wait for the sim to finish to set the locator of the hook
	// (ideally the hook would only need its position late in the rendering frame)
	F32 estRopeDist = 0.0f;
	if (m_reelInTime > 0.0f)
	{
		U32F firstHandIndex = m_rightHandIsFirst ? kRightHand : kLeftHand;
		U32F secondHandIndex = m_rightHandIsFirst ? kLeftHand : kRightHand;
		F32 ropeDist = m_charPointsOnRope[firstHandIndex].m_isOn ? m_charPointsOnRope[firstHandIndex].m_ropeDist : (m_charPointsOnRope[secondHandIndex].m_isOn ? m_charPointsOnRope[secondHandIndex].m_ropeDist : m_rope.m_fLength);
		ropeDist = Max(0.0f, ropeDist * (1.0f - dt/m_reelInTime));
		estRopeDist = GetReelInDist(ropeDist, 0.85f);
	}

	Vector vel;
	m_rope.GetPosAndVel(estRopeDist, posOut, vel);

	Point pos2;
	Vector vel2;
	m_rope.GetPosAndVel(estRopeDist+0.05f, pos2, vel2);

	{
		Vector vecGravity;
		HavokGetGravity(vecGravity);
		Vector vecIncVel = vecGravity * dt * m_rope.m_fGravityFactor;

		Scalar scViscousDamping(m_rope.m_fViscousDamping * dt);
		Scalar scOne(1.0f);

		vel += vecIncVel;
		vel *= RecipSqrt(scOne + LengthSqr(vel) * scViscousDamping);

		vel2 += vecIncVel;
		vel2 *= RecipSqrt(scOne + LengthSqr(vel2) * scViscousDamping);
	}

	posOut += vel * dt;
	pos2 += vel * dt;

	dirOut = SafeNormalize(posOut - pos2, kUnitYAxis);
}

void ProcessPhysRope::SetDampingWhenNoChar(F32 d)
{
	m_dampingWhenNoCharacter = d;
	if (!m_hCharacter.HandleValid())
	{
		m_rope.m_fViscousDamping = d;
	}
}

void ProcessPhysRope::SetRopeLength(F32 len, bool stretchSkinning)
{
	// This is really simplistic and not thought through but it seems to work fine if we're only making the rope shorter
	F32 f = len / m_rope.m_fLength;
	m_rope.m_fLength = len;
	if (stretchSkinning)
	{
		for(I32F ii = GetFirstJoint(); ii <= GetLastJoint(); ii++)
		{
			m_pJointRopeDist[ii-GetFirstJoint()] *= f;
		}
	}
}

void ProcessPhysRope::InheritVisibilityFromParent()
{
	const NdGameObject* pParent = m_hCharacter.ToProcess();
	IDrawControl* pDrawCtrl = GetDrawControl();
	const IDrawControl* pParentDrawCtrl = pParent ? pParent->GetDrawControl() : nullptr;

	if (pDrawCtrl) // IMPORTANT: call this even if pParentDrawCtrl == nullptr (to properly handle detachment)
	{
		pDrawCtrl->InheritVisibilityFrom(pParentDrawCtrl);
		//pDrawCtrl->InheritNearCameraFrom(pParentDrawCtrl);
		//pDrawCtrl->InheritDisableMainViewFrom(pParentDrawCtrl);
	}
}

void ProcessPhysRope::AddQueryPoint(F32 ropeDist)
{
	for (U32 ii = 0; ii<m_numQueryPoints; ii++)
	{
		if (Abs(m_queryRopeDist[ii]-ropeDist) < 0.01f)
		{
			return;
		}
	}
	PHYSICS_ASSERT(m_numQueryPoints < kMaxQueryPoints);
	m_queryRopeDist[m_numQueryPoints] = ropeDist;
	m_numQueryPoints++;
}

void ProcessPhysRope::RemoveQueryPoint(F32 ropeDist)
{
	for (U32 ii = 0; ii<m_numQueryPoints; ii++)
	{
		if (Abs(m_queryRopeDist[ii]-ropeDist) < 0.01f)
		{
			m_queryRopeDist[ii] = m_queryRopeDist[m_numQueryPoints-1];
			m_queryPoints[ii] = m_queryPoints[m_numQueryPoints-1];
			m_numQueryPoints--;
			return;
		}
	}
}

bool ProcessPhysRope::GetQueryPoint(F32 ropeDist, Point& p)
{
	for (U32 ii = 0; ii<m_numQueryPoints; ii++)
	{
		if (Abs(m_queryRopeDist[ii]-ropeDist) < 0.01f)
		{
			p = m_queryPoints[ii];
			return true;
		}
	}
	return false;
}

void ProcessPhysRope::SetEndAttachment(const BoundFrame& bf, bool withDir)
{
	m_endAttachment = bf;
	m_bEndAttachment = true;
	m_bEndAttachmentWithDir = withDir;
}

void ProcessPhysRope::ClearEndAttachment()
{
	m_bEndAttachment = false;
}


///////////////////////////////////////////////////////////
// WIP

/// --------------------------------------------------------------------------------------------------------------- ///
struct RopePieceInitData
{
	F32 m_radius;
	I32 m_firstJointIndex;
	I32 m_lastJointIndex;

	RopePieceInitData()
		: m_radius(-1.0f)
		, m_firstJointIndex(-1)
		, m_lastJointIndex(-1)
	{}
};

class RopePiece
{
public:
	void Init();
public:
	NdGameObject* m_pOwner;
	Rope2 m_rope;

	F32 m_physBlend;
	F32 m_desiredPhysBlend;
	F32 m_physBlendTime;

	U32 m_firstJoint;
	U32 m_lastJoint;

	F32* m_pJointRopeDist;
};
