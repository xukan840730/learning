/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/process-char-cloth.h"

#include "gamelib/audio/arm.h"
#include "gamelib/camera/camera-manager.h"
#include "gamelib/cinematic/cinematic-manager.h"
#include "gamelib/cinematic/cinematic-process.h"
#include "gamelib/gameplay/nd-prop-control.h"
#include "gamelib/ndphys/cloth-bg-job.h"
#include "gamelib/ndphys/cloth-body.h"
#include "gamelib/ndphys/cloth-collide.h"
#include "gamelib/ndphys/cloth-settings.h"
#include "gamelib/ndphys/cloth.h"
#include "gamelib/ndphys/composite-body.h"
#include "gamelib/ndphys/oriented-particles.h"
#include "gamelib/script/nd-script-arg-iterator.h"
#include "ndlib/anim/anim-chain.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/attach-system.h"
#include "ndlib/anim/camera-cut-manager.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/engine-components.h"
#include "ndlib/fx/fxmgr.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/process/spawn-info.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/render/util/text.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/settings/priority.h"
#include "ndlib/settings/settings.h"

class ArtItemAnim;
namespace DC {
struct ClothColliderProto;
}  // namespace DC


FROM_PROCESS_DEFINE(ProcessCharCloth);

/// --------------------------------------------------------------------------------------------------------------- ///
ProcessCharCloth::ProcessCharCloth()
	: m_pOParBody(nullptr)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
ProcessCharCloth::~ProcessCharCloth()
{
	if (m_bloodMaskHandle != kFxRenderMaskHandleInvalid)
	{
		g_fxMgr.ReleaseFxRenderMask(m_bloodMaskHandle);
		m_bloodMaskHandle = kFxRenderMaskHandleInvalid;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err ProcessCharCloth::Init(const ProcessSpawnInfo& info)
{
	PROFILE(Processes, ProcessCharCloth_Init);

	m_externalWindMul = 1.0f;
	m_enableSkinning = true;
	m_enableSkinMoveDist = false;
	m_windLerpCurrTime = m_windLerpTime = 0;
	m_windLerpStart = Vector(kZero);
	m_windLerpEnd = Vector(kZero);
	m_bloodMaskHandle = kFxRenderMaskHandleInvalid;

	if (const CharClothInfo* pClothInfo = static_cast<const CharClothInfo*>(info.m_pUserData))
	{
		m_externalWindMul = pClothInfo->m_externalWindMul;
		m_baseMovementJoint = pClothInfo->m_baseMovementJoint;
		m_enableSkinning = pClothInfo->m_enableSkinning;
		m_enableSkinMoveDist = pClothInfo->m_enableSkinMoveDist;
		m_charDisableBendingForceDist = pClothInfo->m_disableBendingForceDist;
		m_charDisableSimulationMinDist = pClothInfo->m_disableSimulationMinDist;
		m_charDisableSimulationMaxDist = pClothInfo->m_disableSimulationMaxDist;

		int jointSuffixLen = strlen(pClothInfo->m_jointSuffix);
		ALWAYS_ASSERTF(jointSuffixLen < kJointSuffixLen, ("Joint suffix too long ('%s'). Increase max joint suffix len.", pClothInfo->m_jointSuffix));
		strcpy(m_jointSuffix, pClothInfo->m_jointSuffix);

		m_attachJointId = pClothInfo->m_attachJoint;
		m_parentAttachId = pClothInfo->m_parentAttach;
	}

	Err err = ParentClass::Init(info);
	if (err.Failed())
		return err;

	if (GetAttachJointIndex() == kInvalidJointIndex)
	{
		MsgConErr("Cloth object '%s' could not find its attach-joint!\n", DevKitOnly_StringIdToString(GetLookId()));
		return Err::kErrBadData;
	}

	GetAnimControl()->EnableAsyncUpdate(FgAnimData::kEnableAsyncPostAnimBlending);

	m_forceCopyParentJoints = true; // to ensure joints are updated properly

	NdGameObject* pParentGameObject = GetParentGameObjectMutable();
	if (pParentGameObject && !m_isNameValid)
	{
		char name[512];
		snprintf(name, sizeof(name), "ProcessCharCloth (%s)-%d", pParentGameObject->GetName(), GetProcessId());
		SetName(name);
		SetUserId(name, pParentGameObject->GetUserNamespaceId());
	}

	if (m_goFlags.m_needsWetmaskInstanceTexture && pParentGameObject)
	{
		UseParentsWetMask();
	}

	if (pParentGameObject)
	{
		SetTurnReticleRed( pParentGameObject->TurnReticleRed() );
	}

	// @ASYNC
	SetAllowThreadedUpdate(true);

	return err;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err ProcessCharCloth::InitCollision(const ProcessSpawnInfo& info)
{
	const CharClothInfo* pClothInfo = static_cast<const CharClothInfo*>(info.m_pUserData);

	CompositeBodyInitInfo cinfo;
	cinfo.m_initialMotionType = kRigidBodyMotionTypeAuto;
	cinfo.m_pOwner = this;

	const DC::ClothColliderProto* pColliderProto = nullptr;

	FindClothPrototypes(cinfo);

	if (cinfo.m_numClothBodies)
	{
		m_clothCollider = pClothInfo->m_clothCollider;
		pColliderProto = ScriptManager::Lookup<DC::ClothColliderProto>(m_clothCollider, nullptr);
		if (pColliderProto == nullptr)
		{
			MsgConErr("Cloth collider not found: %s\n", DevKitOnly_StringIdToString(m_clothCollider));
		}


		for (U32F ii = 0; ii < cinfo.m_numClothBodies; ++ii)
		{
			cinfo.m_ppClothColliderProtos[ii] = pColliderProto; // NB: for now, ALL cloth bodies within our CompositeBody share the SAME cloth collider proto... this could change in the future
		}
	}
	cinfo.m_pClothColliderOwner = GetParentGameObjectMutable();

	int baseJoint = FindJointIndex(m_baseMovementJoint);
	if (baseJoint >= 0)
		cinfo.m_clothBaseJoint = baseJoint;

	if (!pClothInfo->m_orientedParticles)
	{
		InitCompositeBody(&cinfo);

		CompositeBody* pCompBody = GetCompositeBody();
		for (U32F ii = 0; pCompBody && ii < pCompBody->GetNumClothBodies(); ++ii)
		{
			pCompBody->GetClothBody(ii)->m_enableSkinning = m_enableSkinning;
			pCompBody->GetClothBody(ii)->m_enableSkinMoveDist = m_enableSkinMoveDist;
			pCompBody->GetClothBody(ii)->m_bodyDisableBendingForceDist = m_charDisableBendingForceDist;
			pCompBody->GetClothBody(ii)->m_bodyDisableSimulationMinDist = m_charDisableSimulationMinDist;
			pCompBody->GetClothBody(ii)->m_bodyDisableSimulationMaxDist = m_charDisableSimulationMaxDist;
		}

		if (m_pCompositeBody)
			m_pCompositeBody->SetUpdateClothColliderPos(false);
	}
	else if (cinfo.m_numClothBodies)
	{
		const DC::OrientedParticlesProto* pProto = ScriptManager::Lookup<DC::OrientedParticlesProto>(pClothInfo->m_artGroupId, nullptr);
		if (pProto == nullptr)
		{
			MsgConErr("Oriented particles proto not found: %s\n", DevKitOnly_StringIdToString(pClothInfo->m_artGroupId));
			return Err::kErrBadData;
		}

		m_pOParBody = NDI_NEW OParBody;
		m_pOParBody->Init(pProto, cinfo.m_ppClothProtos[0], GetAnimData(), Locator(kIdentity), GetLocator(), cinfo.m_ppClothColliderProtos[0], cinfo.m_pClothColliderOwner);
	}

	// We may have a regular RB in there too
	// The only case for this so far is backpack which we want to have a collision to push away vegetation or debris
	CompositeBody* pCompo = GetCompositeBody();
	if (pCompo)
	{
		pCompo->SetLayer(Collide::kLayerCharacterProp);
		pCompo->SetSoftContact(true);
	}

	return Err::kOK;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessCharCloth::OnKillProcess()
{
	ParentClass::OnKillProcess();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessCharCloth::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	ParentClass::Relocate(deltaPos, lowerBound, upperBound);
	DeepRelocatePointer(m_pOParBody, deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessCharCloth::UseParentsWetMask()
{
	NdGameObject* pParentGameObject = GetParentGameObjectMutable();
	if (!pParentGameObject)
	{
		return;
	}

	I32 parentWetMaskIndex = pParentGameObject->GetWetMaskIndex();
	I32 currentWetMaskIndex = GetWetMaskIndex();
	if (parentWetMaskIndex != currentWetMaskIndex)
	{
		if (currentWetMaskIndex != FxMgr::kInvalid)
		{
			g_fxMgr.RemoveWetMaskDependent(currentWetMaskIndex, GetProcessId());
		}

		SetWetMaskIndex(parentWetMaskIndex);
		SetFgInstanceFlag(FgInstance::kHasWetness);

		g_fxMgr.AddWetMaskDependent(parentWetMaskIndex, GetProcessId());
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessCharCloth::UpdateRoot(bool postCameraAdjust)
{
	PROFILE(Processes, ProcCharCloth_UpdateRoot);

	NdGameObject* pParentGameObject = GetParentGameObjectMutable();
	bool bParentIsPlayer = false;
	if (pParentGameObject)
	{
		// DB: Don't rely on functionality higher than our library!
		if (pParentGameObject->IsKindOf(SID("Player")))
		{
			bParentIsPlayer = true;
		}

		const Locator loc = pParentGameObject->GetLocator();
		ALWAYS_ASSERT(IsFinite(loc));
		ALWAYS_ASSERT(IsNormal(loc.GetRotation()));

		SetLocator(loc);
	}

	if (m_goFlags.m_needsWetmaskInstanceTexture && pParentGameObject)
	{
		UseParentsWetMask();
	}

	if (m_windLerpTime > 0)
	{
		m_windLerpCurrTime += GetProcessDeltaTime();
		F32 lerp = m_windLerpCurrTime / m_windLerpTime;
		if (m_windLerpCurrTime >= m_windLerpTime)
		{
			lerp = 1;
			m_windLerpCurrTime = m_windLerpTime = 0;
			m_windLerpStart = m_windLerpEnd;
		}
		Vector curWind = Lerp(m_windLerpStart, m_windLerpEnd, lerp);

		CompositeBody* pCompBody = GetCompositeBody();
		for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
		{
			pCompBody->GetClothBody(ii)->m_externalWind = m_externalWindMul * curWind;
		}

		if (bParentIsPlayer)
		{
			F32 fUnitWindStrength = (F32)Length(curWind) * (1.0f/5.0f);
			fUnitWindStrength = MinMax01(fUnitWindStrength);
			EngineComponents::GetAudioManager()->SetGlobalVariable("player-wind", fUnitWindStrength);
			EngineComponents::GetAudioManager()->SetGlobalRegister(63, (I32F)(fUnitWindStrength * 127.0f));
		}
	}

	m_cameraCutsUpdatedThisFrame = false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessCharCloth::PostJointUpdate_Async()
{
	PROFILE(Collision, CharacterCloth_PJU);

	if (IsProcessCharClothBg())
		return;



	CompositeBody* pCompBody = GetCompositeBody();
	U32F numClothBodies = (pCompBody) ? pCompBody->GetNumClothBodies() : 0;

	if (GetProcessDeltaTime() > 0.00001f && pCompBody && numClothBodies != 0)
	{
		if (!m_externalColliderObjectInitialized)
		{
			if (m_externalColliderObjectId != INVALID_STRING_ID_64 && m_externalColliderProtoId != INVALID_STRING_ID_64)
			{
				// keep doing this every frame until we find the external object, to avoid race conditions
				// on spawn, and to handle cases like the external object disappearing out from under us
				const NdGameObject* pExternalGo = NdGameObject::LookupGameObjectByUniqueId(m_externalColliderObjectId);
				const DC::ClothColliderProto* pExternalColliderProto = ScriptManager::Lookup<DC::ClothColliderProto>(m_externalColliderProtoId, nullptr);

				if (pExternalGo && pExternalColliderProto)
				{
					m_externalColliderObjectInitialized = true;

					for (U32F iCloth = 0; iCloth < numClothBodies; ++iCloth)
					{
						ClothBody* pCloth = pCompBody->GetClothBody(iCloth);

						if (pCloth)
						{
							pCloth->InitExternalColliderBindings(pExternalGo, pExternalColliderProto);
						}
					}
				}
			}
		}
		else
		{
			if (m_externalColliderObjectId == INVALID_STRING_ID_64 || m_externalColliderProtoId == INVALID_STRING_ID_64)
			{
				m_externalColliderObjectInitialized = false;

				for (U32F iCloth = 0; iCloth < numClothBodies; ++iCloth)
				{
					ClothBody* pCloth = pCompBody->GetClothBody(iCloth);

					if (pCloth)
					{
						pCloth->RemoveExternalColliderBindings();
					}
				}
			}
		}

		const NdGameObject* pParentGo = GetParentGameObject();
		if (pParentGo)
		{
			const Locator* pExternalJointLocators = nullptr;

			if (m_externalColliderObjectInitialized)
			{
				const NdGameObject* pExternalGo = NdGameObject::LookupGameObjectByUniqueId(m_externalColliderObjectId);
				if (pExternalGo && pExternalGo->GetAnimControl() && pExternalGo->GetAnimControl()->GetJointCache())
				{
					pExternalJointLocators = pExternalGo->GetAnimControl()->GetJointCache()->GetJointLocatorsWs();
				}
			}

			for (U32F ii=0; ii<pCompBody->GetNumClothBodies(); ++ii)
			{
				ClothBody* pCloth = pCompBody->GetClothBody(ii);

				if (pCloth->m_pCollider)
				{
					pCloth->m_pCollider->UpdatePos(pParentGo->GetLocator(), pCloth->GetBaseTransform(), pParentGo->GetAnimData(), nullptr, GetProcessDeltaTime());

					if (pExternalJointLocators)
					{
						pCloth->m_pCollider->UpdatePosExternal(pCloth->GetBaseTransform(), pExternalJointLocators);
					}

					extern bool g_forceDrapeSolve;
					if (g_forceDrapeSolve)
					{
						pCloth->m_pCollider->SetPrevToBindPose(pParentGo->GetAnimControl()->GetJointCache()->GetInverseBindPoses());
					}
				}
			}
		}
	}

	if (m_pOParBody)
	{
		m_pOParBody->UpdateFixedPointsFromSkinning(GetAnimData(), GetLocator());
		m_pOParBody->Step(GetLocator());
		m_pOParBody->UpdateJointCacheFromSim(&GetAnimData()->m_jointCache, GetLocator()); // we need this if we want to attach things to sim backpack joints
		m_pOParBody->UpdateSkinMatricesFromSim(GetAnimData(), GetLocator());
	}

	ParentClass::PostJointUpdate_Async();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessCharCloth::PostAnimBlending_Async()
{
	ParentClass::PostAnimBlending_Async();

	const NdGameObject* pAdditionalAttachObject = m_hAdditionalAttachObject.ToProcess();
	if (pAdditionalAttachObject)
	{
		StringId64 objectJointId = m_additionalAttachObjectJoint;
		StringId64 clothJointId = m_additionalAttachClothJoint;
		ASSERT(objectJointId != INVALID_STRING_ID_64 && clothJointId != INVALID_STRING_ID_64);

		I32F objectJoint = pAdditionalAttachObject->FindJointIndex(objectJointId);
		I32F clothJoint = FindJointIndex(clothJointId);
		ASSERT(objectJoint >= 0 && clothJoint >= 0);

		const JointCache* pObjectJointCache = pAdditionalAttachObject->GetAnimControl()->GetJointCache();
		JointCache* pClothJointCache = GetAnimControl()->GetJointCache();
		ASSERT(pObjectJointCache && pClothJointCache);

		I32F clothParentJoint = pClothJointCache->GetParentJoint(clothJoint);
		Locator objectJointLocatorWs = pObjectJointCache->GetJointLocatorWs(objectJoint);

		Point objectJointPos = objectJointLocatorWs.GetTranslation();
		objectJointPos.SetY(objectJointPos.Y() + m_additionalOffsetY);
		objectJointLocatorWs.SetTranslation(objectJointPos);

		const Locator& clothParentJointLocatorWs = pClothJointCache->GetJointLocatorWs(clothParentJoint);
		Locator clothJointLocatorLs = clothParentJointLocatorWs.UntransformLocator(objectJointLocatorWs);

		ndanim::JointParams clothJointParamsLs = pClothJointCache->GetDefaultLocalSpaceJoints()[clothJoint];

		if (m_additionalAttachBlend == -1.0f)
		{
			m_additionalAttachBlendPos = clothJointParamsLs.m_trans;
			m_additionalAttachBlend = 0.0f;
		}

		m_additionalAttachBlend += m_additionalAttachDetaching ? -FromUPS(1.0f) : FromUPS(1.0f);
		if (m_additionalAttachDetaching && m_additionalAttachBlend <= 0.0f)
		{
			m_hAdditionalAttachObject = nullptr;
			m_additionalAttachBlend = 0.0f;
		}
		m_additionalAttachBlend = MinMax01(m_additionalAttachBlend);

		if (m_additionalAttachDetaching)
			clothJointParamsLs.m_trans = Lerp(clothJointParamsLs.m_trans, m_additionalAttachBlendPos, m_additionalAttachBlend);
		else
			clothJointParamsLs.m_trans = Lerp(m_additionalAttachBlendPos, clothJointLocatorLs.Pos(), m_additionalAttachBlend);
		m_additionalAttachBlendPos = clothJointParamsLs.m_trans;

		// Don't rotate with attach joint for now
		//clothJointParamsLs.m_quat = clothJointLocatorLs.Rot();

		pClothJointCache->SetJointParamsLs(clothJoint, clothJointParamsLs);
		pClothJointCache->UpdateJointLocatorWs(clothJoint);
	}
}

void ProcessCharCloth::ClothUpdate_Async()
{
	ParentClass::ClothUpdate_Async();

	NdGameObject* pParentGameObject = GetParentGameObjectMutable();
	if (pParentGameObject)
	{
		SetTurnReticleRed( pParentGameObject->TurnReticleRed() );
	}

	bool cameraCutOccured = CameraManager::Get().DidCameraCutThisFrame();
	if (cameraCutOccured && m_cameraCutPrerollCloth.Valid())
	{
		if (FALSE_IN_FINAL_BUILD(g_clothSettings.m_cinematicPrerollDebug))
		{
			CompositeBody* pCompBody = GetCompositeBody();
			ClothBody* pClothBody = pCompBody ? pCompBody->GetClothBody(0) : nullptr;
			if (pClothBody)
			{
				const CinematicProcess* pCinProcess = CinematicManager::Get().GetPlayingCinematicProcess(INVALID_STRING_ID_64).ToProcess();
				if (pCinProcess)
				{
					AnimChain* pAnimChain = pCinProcess->GetMasterAnimChain();
					if (pAnimChain)
					{
						AnimChain::LocalTimeIndex localTime = pAnimChain->GetLocalTimeIndex();

						Locator baseLoc = pClothBody->GetBaseTransform();
						g_prim.Draw( DebugCross(baseLoc.Pos(), 0.1f, kColorRed, PrimAttrib(kPrimEnableWireframe, kPrimDisableDepthTest)), Seconds(2.0f) );
						g_prim.Draw( DebugString(baseLoc.Pos(), StringBuilder<256>("'%s': Copying from '%s', frame %.1f", GetName(), m_cameraCutPrerollCloth.ToProcess()->GetName(),
							localTime.AsMayaFrame()).c_str(), kFontCenter, kColorWhite, 0.5f), Seconds(2.0f) );
					}
				}
			}
		}

		KillProcess(m_cameraCutPrerollCloth.ToMutableProcess());
		m_cameraCutPrerollCloth = nullptr;
	}

}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessCharCloth::EventHandler(Event& event)
{
	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("set-additional-attach-object"):
		if (event.GetNumParams() >= 3)
		{
			NdGameObjectHandle attachObject = NdGameObject::FromProcess(event.Get(0).GetProcess());
			if (attachObject.ToProcess() == nullptr)
				m_additionalAttachDetaching = true;
			else
			{
				if (m_additionalAttachBlend >= 0 && m_hAdditionalAttachObject.ToProcess())
					m_additionalAttachBlend = 0.0f;
				else
					m_additionalAttachBlend = -1.0f;
				m_hAdditionalAttachObject = attachObject;
				m_additionalAttachDetaching = false;
				if (m_hAdditionalAttachObject.ToProcess())
				{
					m_additionalAttachObjectJoint = event.Get(1).GetStringId();
					m_additionalAttachClothJoint = event.Get(2).GetStringId();

					m_additionalOffsetY = 0.0f;
					if (event.GetNumParams() >= 4)
						m_additionalOffsetY = event.Get(3).GetFloat();
				}
			}
		}

		break;

	case SID_VAL("set-cloth-params"):
		//if (event.GetNumParams() >= 1)
		//{
		//	const ClothCutsceneParams* pParams = event.Get(0).GetPtr<const ClothCutsceneParams*>(); // CIN_TODO
		//	StringId64 actorName = pParams->m_actorName;
		//	StringId64 meshName = GetDrawControl()->GetMesh(0).m_lod[0].m_meshNameId;
		//	if (actorName == meshName)
		//	{
		//		CompositeBody* pCompBody = GetCompositeBody();
		//		for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
		//		{
		//			ClothBody* pClothBody = pCompBody->GetClothBody(ii);
		//			pClothBody->m_additiveWind = pParams->m_wind;
		//			pClothBody->m_additiveWindApplied = true;
		//			pClothBody->m_skinningLerp = pParams->m_skinningLerp;
		//		}
		//	}
		//}
		break;

	case SID_VAL("set-cloth-wind"):
		if (event.GetNumParams() >= 3)
		{
			F32 windX = event.Get(0).GetFloat();
			F32 windY = event.Get(1).GetFloat();
			F32 windZ = event.Get(2).GetFloat();

			Vector wind(windX, windY, windZ);
			m_windLerpEnd = m_windLerpStart = wind;
			m_windLerpCurrTime = m_windLerpTime = 0;

			CompositeBody* pCompBody = GetCompositeBody();
			for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
			{
				ClothBody* pClothBody = pCompBody->GetClothBody(ii);
				pClothBody->m_externalWind = m_externalWindMul * wind;
				pClothBody->m_externalWindApplied = true;
			}
		}
	break;

	case SID_VAL("lerp-cloth-wind"):
		if (event.GetNumParams() >= 4)
		{
			F32 windX = event.Get(0).GetFloat();
			F32 windY = event.Get(1).GetFloat();
			F32 windZ = event.Get(2).GetFloat();

			// if we are lerping, set start to current
			if (m_windLerpTime > 0)
			{
				m_windLerpStart = Lerp(m_windLerpStart, m_windLerpEnd, m_windLerpCurrTime/m_windLerpTime);
			}
			m_windLerpCurrTime = 0;
			m_windLerpTime = event.Get(3).GetFloat();

			m_windLerpEnd = Vector(windX, windY, windZ);

			CompositeBody* pCompBody = GetCompositeBody();
			for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
			{
				ClothBody* pClothBody = pCompBody->GetClothBody(ii);
				pClothBody->m_externalWind = m_externalWindMul * m_windLerpStart;
				pClothBody->m_externalWindApplied = true;
			}
		}
		break;

	case SID_VAL("is-underwater?"):
		{
			NdGameObject* pParentGo = GetParentGameObjectMutable();
			
			event.SetResponse( SendEvent(SID("is-underwater?"), pParentGo) );
			break;
		}
	break;

	case SID_VAL("disable-cloth-sim"):
		{
			CompositeBody* pCompBody = GetCompositeBody();
			for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
			{
				pCompBody->GetClothBody(ii)->m_disableSim = true;
			}
		}
	break;

	case SID_VAL("enable-cloth-sim"):
		{
			CompositeBody* pCompBody = GetCompositeBody();
			for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
			{
				pCompBody->GetClothBody(ii)->m_disableSim = false;
			}
		}
	break;

	case SID_VAL("set-cloth-world-wind-max"):
		if (event.GetNumParams() >= 1)
		{
			F32 worldWindMax = event.Get(0).GetFloat();
			CompositeBody* pCompBody = GetCompositeBody();
			for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
			{
				pCompBody->GetClothBody(ii)->m_worldWindMax = worldWindMax;
			}
		}
	break;

	case SID_VAL("set-cloth-world-scale"):
		if (event.GetNumParams() > 0)
		{
			StringId64 myLookSid = GetLookId();

			F32 worldScale          = event.Get(0).GetFloat();
			StringId64 desiredLookSid = (event.GetNumParams() > 1) ? event.Get(1).GetStringId() : INVALID_STRING_ID_64;

			if (myLookSid == desiredLookSid || desiredLookSid == INVALID_STRING_ID_64)
			{
				CompositeBody* pCompBody = GetCompositeBody();
				for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
				{
					pCompBody->GetClothBody(ii)->m_worldScaleOverride = worldScale;
				}
			}
		}
	break;

	case SID_VAL("clear-cloth-world-scale"):
		{
			CompositeBody* pCompBody = GetCompositeBody();
			for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
			{
				pCompBody->GetClothBody(ii)->m_worldScaleOverride = -1.0f;
			}
		}
	break;

	case SID_VAL("disable-anim-link-lengths"):
		{
			CompositeBody* pCompBody = GetCompositeBody();
			for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
			{
				pCompBody->GetClothBody(ii)->m_animControlledLinkLength = false;
			}
		}
	break;

	case SID_VAL("teleport-cloth"):
		{
			CompositeBody* pCompBody = GetCompositeBody();
			for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
			{
				pCompBody->GetClothBody(ii)->RequestTeleport();
			}
		}
	break;

	case SID_VAL("reboot-cloth"):
		{
			CompositeBody* pCompBody = GetCompositeBody();
			for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
			{
				pCompBody->GetClothBody(ii)->RequestReboot();
			}
		}
	break;

	case SID_VAL("multiple-reboot-cloth"):
		if (event.GetNumParams() >= 1)
		{
			U32 numFrames = event.Get(0).GetI32();

			CompositeBody* pCompBody = GetCompositeBody();
			for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
			{
				pCompBody->GetClothBody(ii)->RequestMultipleReboots(numFrames);
			}
		}
	break;

	case SID_VAL("print-cloth-info"):
		if (event.GetNumParams() >= 1)
		{
			StringId64 clothName = event.Get(0).GetStringId();

			MsgOut("%64s owns an instance of cloth %s\n", GetParentGameObject()->GetName(), DevKitOnly_StringIdToString(clothName));
		}
	break;

	case SID_VAL("cloth-connect-external-collider"):
		{
			ALWAYS_ASSERT(event.GetNumParams() == 2);

			StringId64 externalObjectId = event.Get(0).GetStringId();
			StringId64 colliderSid	  = event.Get(1).GetStringId();

			if (m_externalColliderObjectId != externalObjectId)
			{
				m_externalColliderObjectInitialized = false;
				m_externalColliderObjectId = externalObjectId;
				m_externalColliderProtoId = colliderSid;
			}
		}
		break;

	case SID_VAL("cloth-remove-external-collider"):
		m_externalColliderObjectId = INVALID_STRING_ID_64;
		m_externalColliderProtoId = INVALID_STRING_ID_64;
		break;

	case SID_VAL("enable-cloth-collider"):
		if (event.GetNumParams() > 1)
		{
			StringId64 myLookSid = GetLookId();

			StringId64 eitherVertexId   = event.Get(0).GetStringId();
			bool enabled              = event.Get(1).GetBool();
			StringId64 externalObjectId = (event.GetNumParams() > 2) ? event.Get(2).GetStringId() : INVALID_STRING_ID_64;
			StringId64 desiredLookSid   = (event.GetNumParams() > 3) ? event.Get(3).GetStringId() : INVALID_STRING_ID_64;

			if (externalObjectId != INVALID_STRING_ID_64)
			{
				m_externalColliderObjectId = externalObjectId;
				m_externalColliderObjectInitialized = false;
			}

			// NB: passing INVALID_STRING_ID_64 for desiredLookSid is fine as long as your character only has one piece of cloth
			if (myLookSid == desiredLookSid || desiredLookSid == INVALID_STRING_ID_64)
			{
				CompositeBody* pCompBody = GetCompositeBody();
				for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
				{
					ClothBody* pClothBody = pCompBody->GetClothBody(ii);
					ASSERT(pClothBody);
					pClothBody->SetColliderEdgeEnabled(eitherVertexId, enabled);
				}
			}
		}
		break;

	case SID_VAL("cloth-no-reboot-on-camera-cut"):
		{
			StringId64 myLookSid = GetLookId();
			StringId64 desiredLookSid = (event.GetNumParams() > 0) ? event.Get(0).GetStringId() : INVALID_STRING_ID_64;

			if (myLookSid == desiredLookSid || desiredLookSid == INVALID_STRING_ID_64)
			{
				CompositeBody* pCompBody = GetCompositeBody();
				for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
				{
					ClothBody* pClothBody = pCompBody->GetClothBody(ii);
					ASSERT(pClothBody);
					pClothBody->DisableCameraCutTeleportNextFrame();
				}
			}

		}
		break;

	case SID_VAL("cloth-spawn-preroll"):
		{
			ALWAYS_ASSERT(event.GetNumParams() == 3);
			StringId64 myLookSid = GetLookId();
			StringId64 animName = event.Get(0).GetStringId();
			U32 frameNum = event.Get(1).GetU32();
			Vector windVec = event.Get(2).GetVector();

			const NdGameObject* pGo = GetParentGameObject();
			const ArtItemAnim* pAnim = pGo->GetAnimControl()->LookupAnim(animName).ToArtItem();
			GAMEPLAY_ASSERT(pAnim);

			SpawnPreroll(pAnim, INVALID_STRING_ID_64, frameNum, windVec);
		}
		break;

	case SID_VAL("cloth-force-skinning-pct"):
		{
			StringId64 myLookSid = GetLookId();
			StringId64 desiredLookSid = (event.GetNumParams() > 0) ? event.Get(0).GetStringId() : INVALID_STRING_ID_64;
			float pct = (event.GetNumParams() > 1) ? event.Get(1).GetAsF32() : 0.0f;
			float blendTime = (event.GetNumParams() > 2) ? event.Get(2).GetAsF32() : 0.5f;

			if (myLookSid == desiredLookSid || desiredLookSid == INVALID_STRING_ID_64)
			{
				CompositeBody* pCompBody = GetCompositeBody();
				for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
				{
					ClothBody* pClothBody = pCompBody->GetClothBody(ii);
					ASSERT(pClothBody);
					pClothBody->OverrideSetting(ClothBody::kSettingForceSkinning, pct, blendTime);
				}
			}
		}
		break;

	case SID_VAL("cloth-min-world-movement"):
		{
			StringId64 myLookSid = GetLookId();
			StringId64 desiredLookSid = (event.GetNumParams() > 0) ? event.Get(0).GetStringId() : INVALID_STRING_ID_64;
			float worldMovement = (event.GetNumParams() > 1) ? event.Get(1).GetAsF32() : 0.0f;
			float blendTime = (event.GetNumParams() > 2) ? event.Get(2).GetAsF32() : 0.5f;

			if (myLookSid == desiredLookSid || desiredLookSid == INVALID_STRING_ID_64)
			{
				CompositeBody* pCompBody = GetCompositeBody();
				for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
				{
					ClothBody* pClothBody = pCompBody->GetClothBody(ii);
					ASSERT(pClothBody);
					pClothBody->OverrideSetting(ClothBody::kSettingMinWorldMovement, worldMovement, blendTime);
				}
			}
		}
		break;

	case SID_VAL("cloth-max-world-movement"):
		{
			StringId64 myLookSid = GetLookId();
			StringId64 desiredLookSid = (event.GetNumParams() > 0) ? event.Get(0).GetStringId() : INVALID_STRING_ID_64;
			float worldMovement = (event.GetNumParams() > 1) ? event.Get(1).GetAsF32() : 0.0f;
			float blendTime = (event.GetNumParams() > 2) ? event.Get(2).GetAsF32() : 0.5f;

			if (myLookSid == desiredLookSid || desiredLookSid == INVALID_STRING_ID_64)
			{
				CompositeBody* pCompBody = GetCompositeBody();
				for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
				{
					ClothBody* pClothBody = pCompBody->GetClothBody(ii);
					ASSERT(pClothBody);
					pClothBody->OverrideSetting(ClothBody::kSettingMaxWorldMovement, worldMovement, blendTime);
				}
			}
		}
		break;

	case SID_VAL("cloth-min-skin-dist"):
		{
			StringId64 myLookSid = GetLookId();
			StringId64 desiredLookSid = (event.GetNumParams() > 0) ? event.Get(0).GetStringId() : INVALID_STRING_ID_64;
			float skinDist = (event.GetNumParams() > 1) ? event.Get(1).GetAsF32() : 0.0f;
			float blendTime = (event.GetNumParams() > 2) ? event.Get(2).GetAsF32() : 0.5f;

			if (myLookSid == desiredLookSid || desiredLookSid == INVALID_STRING_ID_64)
			{
				CompositeBody* pCompBody = GetCompositeBody();
				for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
				{
					ClothBody* pClothBody = pCompBody->GetClothBody(ii);
					ASSERT(pClothBody);
					pClothBody->OverrideSetting(ClothBody::kSettingMinSkinDist, skinDist, blendTime);
				}
			}
		}
		break;

	case SID_VAL("cloth-max-skin-dist"):
		{
			StringId64 myLookSid = GetLookId();
			StringId64 desiredLookSid = (event.GetNumParams() > 0) ? event.Get(0).GetStringId() : INVALID_STRING_ID_64;
			float skinDist = (event.GetNumParams() > 1) ? event.Get(1).GetAsF32() : 0.0f;
			float blendTime = (event.GetNumParams() > 2) ? event.Get(2).GetAsF32() : 0.5f;

			if (myLookSid == desiredLookSid || desiredLookSid == INVALID_STRING_ID_64)
			{
				CompositeBody* pCompBody = GetCompositeBody();
				for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
				{
					ClothBody* pClothBody = pCompBody->GetClothBody(ii);
					ASSERT(pClothBody);
					pClothBody->OverrideSetting(ClothBody::kSettingMaxSkinDist, skinDist, blendTime);
				}
			}
		}
		break;

	case SID_VAL("cloth-allow-compress-pct"):
		{
			StringId64 myLookSid = GetLookId();
			StringId64 desiredLookSid = (event.GetNumParams() > 0) ? event.Get(0).GetStringId() : INVALID_STRING_ID_64;
			float pct = (event.GetNumParams() > 1) ? event.Get(1).GetAsF32() : 0.0f;
			float blendTime = (event.GetNumParams() > 2) ? event.Get(2).GetAsF32() : 0.5f;

			if (myLookSid == desiredLookSid || desiredLookSid == INVALID_STRING_ID_64)
			{
				CompositeBody* pCompBody = GetCompositeBody();
				for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
				{
					ClothBody* pClothBody = pCompBody->GetClothBody(ii);
					ASSERT(pClothBody);
					pClothBody->OverrideSetting(ClothBody::kSettingOverrideAllowCompress, pct, blendTime);
				}
			}
		}
		break;

	case SID_VAL("cloth-set-bending-stiffness"):
		{
			StringId64 myLookSid = GetLookId();
			StringId64 desiredLookSid = (event.GetNumParams() > 0) ? event.Get(0).GetStringId() : INVALID_STRING_ID_64;
			float bendingStiffness = (event.GetNumParams() > 1) ? event.Get(1).GetAsF32() : 0.0f;
			float blendTime = (event.GetNumParams() > 2) ? event.Get(2).GetAsF32() : 0.5f;

			if (myLookSid == desiredLookSid || desiredLookSid == INVALID_STRING_ID_64)
			{
				CompositeBody* pCompBody = GetCompositeBody();
				for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
				{
					ClothBody* pClothBody = pCompBody->GetClothBody(ii);
					ASSERT(pClothBody);
					pClothBody->OverrideSetting(ClothBody::kSettingSetBendingStiffness, bendingStiffness, blendTime);
				}
			}
		}
		break;

	case SID_VAL("cloth-set-collider-mult"):
		{
			StringId64 myLookSid = GetLookId();
			StringId64 desiredLookSid = (event.GetNumParams() > 0) ? event.Get(0).GetStringId() : INVALID_STRING_ID_64;
			float colliderMult = (event.GetNumParams() > 1) ? event.Get(1).GetAsF32() : 0.0f;
			float blendTime = (event.GetNumParams() > 2) ? event.Get(2).GetAsF32() : 0.5f;

			if (myLookSid == desiredLookSid || desiredLookSid == INVALID_STRING_ID_64)
			{
				CompositeBody* pCompBody = GetCompositeBody();
				for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
				{
					ClothBody* pClothBody = pCompBody->GetClothBody(ii);
					ASSERT(pClothBody);
					pClothBody->OverrideSetting(ClothBody::kSettingColliderMult, colliderMult, blendTime);
				}
			}
		}
		break;

	case SID_VAL("cloth-set-stretchiness-mult"):
		{
			StringId64 myLookSid = GetLookId();
			StringId64 desiredLookSid = (event.GetNumParams() > 0) ? event.Get(0).GetStringId() : INVALID_STRING_ID_64;
			float stretchinessMult = (event.GetNumParams() > 1) ? event.Get(1).GetAsF32() : 0.0f;
			float blendTime = (event.GetNumParams() > 2) ? event.Get(2).GetAsF32() : 0.5f;

			if (myLookSid == desiredLookSid || desiredLookSid == INVALID_STRING_ID_64)
			{
				CompositeBody* pCompBody = GetCompositeBody();
				for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
				{
					ClothBody* pClothBody = pCompBody->GetClothBody(ii);
					ASSERT(pClothBody);
					pClothBody->OverrideSetting(ClothBody::kSettingStretchinessMult, stretchinessMult, blendTime);
				}
			}
		}
		break;

	case SID_VAL("cloth-setting-disable-collision"):
		{
			StringId64 myLookSid = GetLookId();
			StringId64 desiredLookSid = (event.GetNumParams() > 0) ? event.Get(0).GetStringId() : INVALID_STRING_ID_64;
			float disableCollision = (event.GetNumParams() > 1) ? event.Get(1).GetAsF32() : 0.0f;
			//float blendTime = (event.GetNumParams() > 2) ? event.Get(2).GetAsF32() : 0.5f;

			if (myLookSid == desiredLookSid || desiredLookSid == INVALID_STRING_ID_64)
			{
				CompositeBody* pCompBody = GetCompositeBody();
				for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
				{
					ClothBody* pClothBody = pCompBody->GetClothBody(ii);
					ASSERT(pClothBody);
					pClothBody->OverrideSetting(ClothBody::kSettingDisableCollision, disableCollision, 0.0f);
				}
			}
		}
		break;

	case SID_VAL("cloth-setting-disable-reboot"):
		{
			StringId64 myLookSid = GetLookId();
			StringId64 desiredLookSid = (event.GetNumParams() > 0) ? event.Get(0).GetStringId() : INVALID_STRING_ID_64;
			float disableReboot = (event.GetNumParams() > 1) ? event.Get(1).GetAsF32() : 0.0f;
			//float blendTime = (event.GetNumParams() > 2) ? event.Get(2).GetAsF32() : 0.5f;

			if (myLookSid == desiredLookSid || desiredLookSid == INVALID_STRING_ID_64)
			{
				CompositeBody* pCompBody = GetCompositeBody();
				for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
				{
					ClothBody* pClothBody = pCompBody->GetClothBody(ii);
					ASSERT(pClothBody);
					pClothBody->OverrideSetting(ClothBody::kSettingDisableReboot, disableReboot, 0.0f);
				}
			}
		}
		break;

	default:
		ParentClass::EventHandler(event);
		break;
	}
}

void ProcessCharCloth::Show(int screenIndex /* = -1 */, bool secondary /* = false */)
{
	// Ignore this message if we have cloths meshes disabled in debug
	bool hidden = false;
	CompositeBody* pCompBody = GetCompositeBody();
	for (U32F ii = 0; pCompBody && ii < pCompBody->GetNumClothBodies(); ++ii)
	{
		ClothBody* pClothBody = pCompBody->GetClothBody(ii);
		hidden = hidden || pClothBody->m_hideRenderedMesh;
	}

	if (!hidden)
		ParentClass::Show(screenIndex, secondary);
}

void ProcessCharCloth::SpawnPreroll(const ArtItemAnim* pAnim, StringId64 apSpawner, U32 frameNum, Vector_arg windVec)
{
	if (!g_clothSettings.m_enableClothCinematicPreroll)
		return;

	NdGameObject* pParentGo = GetParentGameObjectMutable();

	U32 pid = Process::GenerateUniqueId();
	char pidBuf[16];
	sprintf(pidBuf, "-%d", pid);

	CharClothBgInfo clothInfo;
	clothInfo.m_hParentProcessGo			= pParentGo;
	clothInfo.m_artGroupId					= m_lookId;
	clothInfo.m_attachJoint					= m_attachJointId;
	clothInfo.m_parentAttach				= m_parentAttachId;
	clothInfo.m_attachOffset				= GetParentAttachOffset();
	clothInfo.m_loc							= GetLocator();
	clothInfo.m_userBareId					= StringId64Concat(GetUserId(), pidBuf);
	clothInfo.m_userId						= clothInfo.m_userBareId;
	clothInfo.m_clothCollider				= m_clothCollider;
	clothInfo.m_jointSuffix					= m_jointSuffix;
	clothInfo.m_externalWindMul				= m_externalWindMul;
	clothInfo.m_baseMovementJoint			= m_baseMovementJoint;
	clothInfo.m_enableSkinning				= m_enableSkinning;
	clothInfo.m_enableSkinMoveDist			= m_enableSkinMoveDist;
	clothInfo.m_disableBendingForceDist = m_charDisableBendingForceDist;
	clothInfo.m_disableSimulationMinDist = m_charDisableSimulationMinDist;
	clothInfo.m_disableSimulationMaxDist = m_charDisableSimulationMaxDist;
	clothInfo.m_pAnim						= pAnim;
	clothInfo.m_apSpawner					= apSpawner;
	clothInfo.m_animFrame					= frameNum;
	clothInfo.m_windVec						= windVec;

	NdAttachableObject* proc = nullptr;
	SpawnInfo spawn(SID("ProcessCharClothBg"));
	spawn.m_pUserData = &clothInfo;
	spawn.m_pTintArray = nullptr;
	spawn.m_pParentLookSeed = nullptr;

	if (m_cameraCutPrerollCloth.Valid())
		KillProcess(m_cameraCutPrerollCloth.ToMutableProcess());

	m_cameraCutPrerollCloth = static_cast<ProcessCharClothBg*>(NewProcess(spawn));

	CompositeBody* pCompBody = GetCompositeBody();
	for (U32F ii=0; pCompBody && ii<pCompBody->GetNumClothBodies(); ++ii)
	{
		ClothBody* pClothBody = pCompBody->GetClothBody(ii);
		ASSERT(pClothBody);
		pClothBody->m_cameraCutPrerollCloth = m_cameraCutPrerollCloth;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ProcessCharCloth::MeetsCriteriaToDebugRecord(bool& outRecordAsPlayer) const
{
	const NdGameObject* pParentGameObject = GetParentGameObject();

	if (pParentGameObject && pParentGameObject->IsKindOf(SID("Player")))
	{
		outRecordAsPlayer = true;
		return true;
	}

	return true;
}

PROCESS_REGISTER_ALLOC_SIZE(ProcessCharCloth, NdMultiAttachable, 512 * 1024);

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsCharClothEvent(const Event& evt)
{
	switch (evt.GetMessage().GetValue())
	{
	case SID_VAL("set-additional-attach-object"):
	case SID_VAL("set-cloth-params"):
	case SID_VAL("set-cloth-wind"):
	case SID_VAL("lerp-cloth-wind"):
	case SID_VAL("disable-cloth-sim"):
	case SID_VAL("enable-cloth-sim"):
	case SID_VAL("set-cloth-world-wind-max"):
	case SID_VAL("set-cloth-world-scale"):
	case SID_VAL("clear-cloth-world-scale"):
	case SID_VAL("disable-anim-link-lengths"):
	case SID_VAL("teleport-cloth"):
	case SID_VAL("reboot-cloth"):
	case SID_VAL("multiple-reboot-cloth"):
	case SID_VAL("print-cloth-info"):
	case SID_VAL("enable-cloth-collider"):
	case SID_VAL("cloth-connect-external-collider"):
	case SID_VAL("cloth-remove-external-collider"):
	case SID_VAL("cloth-no-reboot-on-camera-cut"):
	case SID_VAL("cloth-spawn-preroll"):
	case SID_VAL("cloth-allow-compress-pct"):
	case SID_VAL("cloth-force-skinning-pct"):
	case SID_VAL("cloth-min-world-movement"):
	case SID_VAL("cloth-max-world-movement"):
	case SID_VAL("cloth-min-skin-dist"):
	case SID_VAL("cloth-max-skin-dist"):
	case SID_VAL("cloth-set-bending-stiffness"):
	case SID_VAL("cloth-set-collider-mult"):
	case SID_VAL("cloth-set-stretchiness-mult"):
	case SID_VAL("cloth-setting-disable-collision"):
	case SID_VAL("cloth-setting-disable-reboot"):
		return true;

	default:
		return false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ForceLinkProcessCharCloth()
{
	// needed to avoid dead stripping of this entire translation unit
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("cloth-set-world-scale", DcClothSetWorldScale)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject* pGo = args.NextGameObject();
	F32 scale = args.NextFloat();
	StringId64 clothActorId = args.NextStringId();

	if (pGo && scale >= 0.0f)
	{
		SendEvent(SID("set-cloth-world-scale"), pGo, scale, clothActorId);
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("cloth-set-global-world-scale", DcClothSetGlobalWorldScale)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	F32 scale = args.NextFloat();

	if (scale >= 0.0f)
	{
		SettingSetDefault(&g_clothSettings.m_globalWorldScale, -1.0f);
		SettingSetPers(SID("cloth-set-global-world-scale"), &g_clothSettings.m_globalWorldScale, scale, kPlayerModePriority, 1.0f, Seconds(0.1f));
	}

	return ScriptValue(0);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("cloth-set-wind-scale", DcClothSetWindScale)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	F32 scale = args.NextFloat();

	if (scale >= 0.0f && !CinematicManager::Get().IsUpdateScriptsCinematicHiddenByTransitionMovie())
	{
		SettingSetDefault(&g_clothSettings.m_worldWindScale, 1.0f);
		SettingSetPers(SID("cloth-set-wind-scale"), &g_clothSettings.m_worldWindScale, scale, kPlayerModePriority, 1.0f, Seconds(0.1f));
	}

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("cloth-no-reboot-on-camera-cut", DcClothNoRebootOnCameraCut)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject* pGo = args.NextGameObject();
	StringId64 clothActorName = args.NextStringId();

	if (pGo)
	{
		SendEvent(SID("cloth-no-reboot-on-camera-cut"), pGo, clothActorName);
	}

	return ScriptValue(false);
}

bool ClothSpawnPreroll(StringId64 objectName, NdGameObject* pGo, StringId64 animName, StringId64 apSpawner, U32 frameNum, Vector windVec)
{
	if (pGo && GetProcessDeltaTime() > 0.0f)
	{
		const ArtItemAnim* pAnim = nullptr;
		int prerollFrameNum = frameNum;

		const CameraCutAnimEntry* pCameraCutEntry = EngineComponents::GetCameraCutManager()->FindAnimEntry(animName, frameNum);
		if (pCameraCutEntry)
		{
			pAnim = pCameraCutEntry->m_pArtItemAnim;
			prerollFrameNum = 0;
		}
		else
		{
			pAnim = pGo->GetAnimControl()->LookupAnim(animName).ToArtItem();
		}

		if (pAnim)
		{
			NdPropControl* pProps = pGo->GetNdPropControl();
			if (pProps)
			{
				for (int iProp=0; iProp<pProps->GetNumProps(); iProp++)
				{
					NdAttachableObject* pProp = pProps->GetPropObject(iProp);
					ProcessCharCloth* pClothProp = ProcessCharCloth::FromProcess(pProp);
					if (pClothProp)
						pClothProp->SpawnPreroll(pAnim, apSpawner, prerollFrameNum, kZero);
				}
			}
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("cloth-spawn-preroll", DcClothSpawnPreroll)
{
	SCRIPT_ARG_ITERATOR(args, 4);
	StringId64 objectName = args.GetStringId();
	NdGameObject* pGo = args.NextGameObject();
	StringId64 animName = args.NextStringId();
	U32 frameNum = args.NextU32();
	Vector windVec = args.NextVector();

	if (!ClothSpawnPreroll(objectName, pGo, animName, INVALID_STRING_ID_64, frameNum, windVec))
		args.MsgScriptError("cloth-spawn-preroll: Anim not found '%s'\n", DevKitOnly_StringIdToString(animName));

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("cloth-spawn-preroll-ap", DcClothSpawnPrerollAp)
{
	SCRIPT_ARG_ITERATOR(args, 4);
	StringId64 objectName = args.GetStringId();
	NdGameObject* pGo = args.NextGameObject();
	StringId64 animName = args.NextStringId();
	StringId64 apSpawner = args.NextStringId();
	U32 frameNum = args.NextU32();
	Vector windVec = args.NextVector();

	if (!ClothSpawnPreroll(objectName, pGo, animName, apSpawner, frameNum, windVec))
		args.MsgScriptError("cloth-spawn-preroll-ap: Anim not found '%s'\n", DevKitOnly_StringIdToString(animName));

	return ScriptValue(false);
}

/// --------------------------------------------------------------------------------------------------------------- ///
SCRIPT_FUNC("cloth-setting", DcClothSetting)
{
	SCRIPT_ARG_ITERATOR(args, 5);
	NdGameObject* pGo = args.NextGameObject();
	StringId64 setting = args.NextStringId();
	F32 value = args.NextFloat();
	F32 blendTime = args.NextFloat();
	StringId64 clothMesh = args.NextStringId();

	if (pGo)
	{
		SendEvent(setting, pGo, clothMesh, value, blendTime);
	}

	return ScriptValue(0);
}

