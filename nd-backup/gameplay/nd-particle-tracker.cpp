/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nd-particle-tracker.h"

#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/attach-system.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/process/event.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/process/process.h"
#include "ndlib/render/particle/particle-debug.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/render/particle/particle-root-data.h"

#include "gamelib/gameplay/nd-effect-control.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/render/particle/particle.h"
#include "gamelib/state-script/ss-action.h"
#include "gamelib/scriptx/h/effect-defines.h"

// called when process is about to be killed
void NdParticleTracker::OnKillProcess()
{
	KillParticle();

	// Tell the controlling process that I'm done playing the effect.
	m_ssAction.Stop();

	ParentClass::OnKillProcess();
}

void NdParticleTracker::KillParticle()
{
	if (m_numParticles)
	{
		for (int i = 0; i < m_numParticles; ++i)
		{
			::KillParticle(m_hParticles[i], m_linger);
			m_hParticles[i] = ParticleHandle();
		}
	}
	else
	{
		::KillParticle(m_hParticle, m_linger);
		m_hParticle = ParticleHandle();
	}
}

void NdParticleTracker::Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound)
{
	RelocatePointer(m_hParticles, offset_bytes, lowerBound, upperBound);

	ParentClass::Relocate(offset_bytes, lowerBound, upperBound);
}

void NdParticleTracker::SpawnParticleInternal(BoundFrame& bf, ParticleMeshAttachmentSpawnInfo *pMeshAttach)
{
	ProcessHandle hTracker = this;
	if (pMeshAttach)
	{
		pMeshAttach->m_hTrackerProcess = hTracker;
	}
	ParticleAssociation assoc = ParticleAssociation(m_objHandle);
	assoc.m_pMeshAttach = pMeshAttach;
	assoc.m_saveType = m_spawnInfo.m_saveType;

	m_hParticle = ::SpawnParticleEventWithAssociation(bf, m_partName, m_eventHandle, assoc);
}

void NdParticleTracker::SpawnParticleEffect(const Locator& loc, ParticleMeshAttachmentSpawnInfo *pMeshAttach)
{
	m_refreshCounter = g_particleMgr.GetRefreshCounter();

	BoundFrame bf(loc, Binding());
	SpawnParticleInternal(bf, pMeshAttach);

	if (!m_hParticle.IsValid())
	{
		if (!g_particleMgr.GetSuppressSpawning() && g_particleMgr.GetDebugInfoRef().enableMissingPartDisplay)
		{
			GoError("NdParticleTracker: Failed to spawn effect '%s': %s!\n",
					DevKitOnly_StringIdToString(m_partName), m_hParticle.GetErrorString());
		}
		KillProcess(this);
	}
}

void NdParticleTracker::SetFloat(StringId64 rootName, float rootVal)
{
	if (m_type == SID("part-effect"))
	{
		g_particleMgr.SetFloat(m_hParticle, rootName, rootVal);
	}

	if (m_type == SID("part-effect-table"))
	{
		for (int i = 0; i < m_numParticles; ++i)
		{
			g_particleMgr.SetFloat(m_hParticles[i], rootName, rootVal);
		}
	}
}

void NdParticleTracker::SpawnParticleTable()
{
	bool anyValid = false;
	m_refreshCounter = g_particleMgr.GetRefreshCounter();

	if (const DC::ParticleSpawnTable  *pTable = m_effTableScriptPointer)
	{
		if (const NdGameObject *pGameObj = m_objHandle.ToProcess())
		{
			for (int i = 0; i < pTable->m_spawnArrayCount; ++i)
			{
				Locator loc;

				const DC::ParticleSpawnTableEntry *pEntry = &pTable->m_spawnArray[i];

				if (GetCurrentLocator(&loc, pGameObj, m_boundFrame, pEntry))
				{
					BoundFrame bf(loc, Binding());

					ParticleAssociation assoc = ParticleAssociation(m_objHandle);

					m_hParticles[i] = ::SpawnParticleEventWithAssociation(bf, pEntry->m_particleName, m_eventHandle, assoc);

					if (!m_hParticles[i].IsValid())
					{
						if (!g_particleMgr.GetSuppressSpawning() && g_particleMgr.GetDebugInfoRef().enableMissingPartDisplay)
						{
							GoError("NdParticleTracker: Failed to spawn effect '%s': %s!\n",
								DevKitOnly_StringIdToString(pEntry->m_particleName), m_hParticles[i].GetErrorString());
						}
						//KillProcess(this);
					}
					else
					{
						anyValid = true;

						g_particleMgr.SetRootDataFromSpawnTableEntry(m_hParticles[i], pEntry);
					}
				}
			}
		}
	}
}

Err NdParticleTracker::Init(const ProcessSpawnInfo& spawn)
{
	m_objHandle = nullptr;
	m_bObjHandleValid = false;

	m_hParticles = nullptr;
	m_numParticles = 0;

	Err result = Process::Init( spawn );
	if (!result.Succeeded())
	{
		return result;
	}
	EffectControlSpawnInfo* info = (EffectControlSpawnInfo*) spawn.m_pUserData;
	m_spawnInfo = *info;
	m_eventHandle = MutableProcessHandle(m_spawnInfo.m_eventProcess);

	//BoxedValue* pUserParams = spawn.m_pUserParams;
	m_partName = info->m_name;
	if (m_partName == INVALID_STRING_ID_64)
	{
		GoError("Error in NdParticleTracker: nullptr particle group!\n");
		return Err::kErrBadData;
	}
	//if (m_partName == SID("sniper-scope-flare-1"))
	//{
	//	MsgOut("Our particle\n");
	//}
	m_objHandle = info->m_hGameObject;
	m_bObjHandleValid = m_objHandle.HandleValid();
	m_attachId = info->m_attachId;
	m_jointId = info->m_jointId;
	m_rootId = info->m_rootId;
	m_refreshCounter = 0;
	m_hParticle = ParticleHandle();
	m_endFrame = info->m_endFrame;
	m_ssAction.SetOwner(this);
	m_ssAction.Start(info->m_pControllingProcess, 0);
	m_boundFrame = info->m_boundFrame;
	m_linger = info->m_bLingeringDeath;
	m_bKillWhenPhysicalized = info->m_bKillWhenPhysicalized;
	m_elapsedTime = 0.0f;
	
	m_bPlayAtOrigin = info->m_effectFlags & DC::kEffectFlagsPlayAtOrigin;

	m_visibilityBeforePhotoModeSet = 0;
	m_visibilityBeforePhotoMode = 0;


	// Associate a level with the tracker
	if (const Process* pProc = info->m_hGameObject.ToProcess())
	{
		AssociateWithLevel(pProc->GetAssociatedLevel());
	}

	switch (info->m_type.GetValue())
	{
	case SID_VAL("part-effect"):
		m_type = info->m_type;
		break;
	case SID_VAL("part-effect-table"):
		m_type = info->m_type;
		break;
	default:
		GoError("NdParticleTracker: unknown type '%s'\n", DevKitOnly_StringIdToString(info->m_type));
		return Err::kErrBadData;
	}

	if (m_type == SID("part-effect"))
	{
		GetCurrentLocator(&m_currentLoc);
		SpawnParticleEffect(m_currentLoc, info->m_pMeshAttachmentInfo);
	}

	if (m_type == SID("part-effect-table"))
	{
		m_effTableScriptPointer = EffectTableScriptPointer(m_partName);

		if (const DC::ParticleSpawnTable  *pTable = m_effTableScriptPointer)
		{
			m_numParticles = pTable->m_spawnArrayCount;
			m_hParticles = NDI_NEW(kAlign8) ParticleHandle[m_numParticles];

			SpawnParticleTable();
		}
		else
		{
			return Err::kErrBadData;
		}
	}

	// Tell the controlling process that I've started playing the effect.
	m_ssAction.Notify(SID("parttracker-started"));

	SetAllowThreadedUpdate(true);

	return Err::kOK;
}

void NdParticleTracker::UpdateParticle()
{
	GetCurrentLocator(&m_currentLoc);

	if (m_refreshCounter != g_particleMgr.GetRefreshCounter())
	{
		// rcl - this is a debug kind of thing, when we call killallparticles the refreshCounter forces things to restart
		KillParticle();
		SpawnParticleEffect(m_currentLoc);
	}

	bool bKillParticle = false;

	if (m_bKillWhenPhysicalized)
	{
		if (const NdGameObject* pObj = m_objHandle.ToProcess())
		{
			if (pObj->GetPhysMotionType() == kRigidBodyMotionTypePhysicsDriven)
			{
				bKillParticle = true;
			}
		}
	}

	if (bKillParticle)
	{
		KillProcess(this);
	}

	if (IsParticleAlive(m_hParticle))
	{
		// Only update if we didn't have a detach timeout set or if it has not timed out yet.
		if (m_spawnInfo.m_detachTimeout == -1.0f || m_elapsedTime < m_spawnInfo.m_detachTimeout)
			g_particleMgr.SetLocation(m_hParticle, BoundFrame(m_currentLoc));

		g_particleMgr.SetVector(m_hParticle, SID("emitterposition"), m_currentLoc.GetTranslation() - Point(0, 0, 0));
	}
	else
	{
		// Dylan, fix me!!!
		// We don't kill trackers if in deterministic mode (cutscene capture) because they seem to get killed
		// on different frames each time
		if (!EngineComponents::GetNdGameInfo()->m_deterministic)
			KillProcess(this);
	}
}

void NdParticleTracker::UpdateParticleTable()
{
	const DC::ParticleSpawnTable  *pTable = m_effTableScriptPointer;

	if (!pTable)
	{
		KillProcess(this);
		return;
	}

	if (m_numParticles != pTable->m_spawnArrayCount)
	{
		KillProcess(this);
		return;
	}

	if (m_refreshCounter != g_particleMgr.GetRefreshCounter())
	{
		// rcl - this is a debug kind of thing, when we call killallparticles the refreshCounter forces things to restart
		KillParticle();
		SpawnParticleTable();
	}


	bool bKillParticle = false;

	if (m_bKillWhenPhysicalized)
	{
		if (const NdGameObject* pObj = m_objHandle.ToProcess())
		{
			if (pObj->GetPhysMotionType() == kRigidBodyMotionTypePhysicsDriven)
			{
				bKillParticle = true;
			}
		}
	}

	if (bKillParticle)
	{
		KillProcess(this);
		return;
	}

	bool anyAlive = false;
	for (int i = 0; i < m_numParticles; ++i)
	{
		if (IsParticleAlive(m_hParticles[i]))
		{

			if (const NdGameObject *pGameObj = m_objHandle.ToProcess())
			{
				Locator loc;

				const DC::ParticleSpawnTableEntry *pEntry = &pTable->m_spawnArray[i];

				if (GetCurrentLocator(&loc, pGameObj, m_boundFrame, pEntry))
				{
					// Only update if we didn't have a detach timeout set or if it has not timed out yet.
					if (m_spawnInfo.m_detachTimeout == -1.0f || m_elapsedTime < m_spawnInfo.m_detachTimeout)
						g_particleMgr.SetLocation(m_hParticles[i], BoundFrame(loc));

					g_particleMgr.SetVector(m_hParticles[i], SID("emitterposition"), loc.GetTranslation() - Point(0, 0, 0));
				}
			}
			anyAlive = true;
		}
	}

	if (!anyAlive)
	{
		// Dylan, fix me!!!
		// We don't kill trackers if in deterministic mode (cutscene capture) because they seem to get killed
		// on different frames each time
		if (!EngineComponents::GetNdGameInfo()->m_deterministic)
			KillProcess(this);
	}
}

static Locator GetAttachLocator(const NdGameObject* pGameObj, StringId64 jointId, StringId64 attachId, StringId64 effectGroupSid)
{
	Locator loc;

	ALWAYS_ASSERT(pGameObj);
	if (attachId != INVALID_STRING_ID_64)
	{
		AttachIndex index = AttachIndex::kInvalid;
		if (pGameObj->GetAttachSystem()->FindPointIndexById(&index, attachId))
		{
			loc = pGameObj->GetAttachSystem()->GetLocator(index);
		}
		else
		{
			MsgErr("[%s] NdParticleTracker: Failed finding attach point '%s' for effect group %s\n",
				pGameObj->GetName(),
				DevKitOnly_StringIdToStringOrHex(attachId),
				DevKitOnly_StringIdToString(effectGroupSid));
			loc = Locator(kIdentity);
		}
	}
	else if (jointId != INVALID_STRING_ID_64)
	{
		const FgAnimData* pAnimData = pGameObj->GetAnimData();
		int jointIndex = pAnimData->FindJoint(jointId);
		if (jointIndex < 0)
		{
			loc = pAnimData->m_jointCache.GetJointLocatorWs(0);

			/*MsgErr("[%s] NdParticleTracker::GetAttachLocator() failed: joint %s effect group %s\n",
				pGameObj->GetName(),
				DevKitOnly_StringIdToString(jointId),
				DevKitOnly_StringIdToString(effectGroupSid));*/
		}
		else
		{
			loc = pAnimData->m_jointCache.GetJointLocatorWs(jointIndex);
			
			//Locator locAlt = pAnimData->m_jointCache.GetJointLocatorWsExp(jointIndex);
			//
			//if (pGameObj->GetUserId() == SID("npc-3662") && jointIndex == 29)
			//{
			//	//ALWAYS_ASSERT(Dist(locAlt.GetTranslation(), loc.GetTranslation()) < 0.01f);
			//	if (!((Dist(locAlt.GetTranslation(), loc.GetTranslation()) < 0.01f)))
			//	{
			//		MsgOut("npc-3662 has bad transform joint 29 @ 0x%p\n", &pAnimData->m_jointCache.GetJointLocatorWs(jointIndex));
			//	}
			//}

		}
	}

	return loc;
}

bool NdParticleTracker::GetCurrentLocator (Locator* locOut)
{
	const NdGameObject *pGameObj = m_objHandle.ToProcess();

	Locator alignLoc = m_bPlayAtOrigin ? Locator(kIdentity) : ((pGameObj) ? pGameObj->GetLocator() : m_boundFrame.GetLocatorWs());
	Locator loc(alignLoc);
	Locator rootLoc(alignLoc);
	Locator aprefLoc;

	if (pGameObj)
	{
		if ((m_attachId != INVALID_STRING_ID_64) || (m_jointId != INVALID_STRING_ID_64))
		{
			loc = GetAttachLocator(pGameObj, m_jointId, m_attachId, m_partName);
			rootLoc = GetAttachLocator(pGameObj, m_rootId, INVALID_STRING_ID_64, m_partName);
		}
	}
	else
	{
		loc = alignLoc;
		rootLoc = alignLoc;
	}

	if (!(pGameObj && pGameObj->GetApOrigin(aprefLoc)))
		aprefLoc = alignLoc;

	EOffsetFrame fOffset = m_spawnInfo.m_offsetFrame;
	EOffsetFrame fOrient = m_spawnInfo.m_orientFrame;

	Quat offsetSpace = ((fOffset == kWorldFrame) ? Quat(kIdentity)        :
						(fOffset == kAlignFrame) ? alignLoc.GetRotation() :
						(fOffset == kApRefFrame) ? aprefLoc.GetRotation() :
						(fOffset == kRootFrame)  ? rootLoc.GetRotation()  :
												   loc.GetRotation());

	Quat orientSpace = ((fOrient == kWorldFrame) ? Quat(kIdentity)        :
						(fOrient == kAlignFrame) ? alignLoc.GetRotation() :
						(fOrient == kApRefFrame) ? aprefLoc.GetRotation() :
						(fOrient == kRootFrame)  ? rootLoc.GetRotation()  :
												   loc.GetRotation());

	Vector offset = Rotate(offsetSpace, m_spawnInfo.m_offset);
	locOut->Set(loc.Pos() + offset, SafeNormalize(orientSpace * m_spawnInfo.m_rot, Quat(kIdentity)));

	return true;
}

bool NdParticleTracker::GetCurrentLocator(Locator* locOut, const NdGameObject *pGameObj, const BoundFrame &boundFrame, const DC::ParticleSpawnTableEntry *pEntry)
{
	Locator alignLoc = (pGameObj) ? pGameObj->GetLocator() : boundFrame.GetLocatorWs();
	Locator loc(alignLoc);
	Locator rootLoc(alignLoc);
	Locator aprefLoc;

	if (pGameObj)
	{
		if (/* (m_attachId != INVALID_STRING_ID_64) || */ (pEntry->m_jointName != INVALID_STRING_ID_64))
		{
			loc = GetAttachLocator(pGameObj, pEntry->m_jointName, /* m_attachId*/ INVALID_STRING_ID_64, pEntry->m_particleName);
			rootLoc = GetAttachLocator(pGameObj, pEntry->m_root, INVALID_STRING_ID_64, pEntry->m_particleName);
		}
	}
	else
	{
		loc = alignLoc;
		rootLoc = alignLoc;
	}

	if (!(pGameObj && pGameObj->GetApOrigin(aprefLoc)))
		aprefLoc = alignLoc;

	EOffsetFrame fOffset = (EOffsetFrame)pEntry->m_align;
	EOffsetFrame fOrient = (EOffsetFrame)pEntry->m_orient;

	Quat offsetSpace = ((fOffset == kWorldFrame) ? Quat(kIdentity) :
		(fOffset == kAlignFrame) ? alignLoc.GetRotation() :
		(fOffset == kApRefFrame) ? aprefLoc.GetRotation() :
		(fOffset == kRootFrame) ? rootLoc.GetRotation() :
		loc.GetRotation());

	Quat orientSpace = ((fOrient == kWorldFrame) ? Quat(kIdentity) :
		(fOrient == kAlignFrame) ? alignLoc.GetRotation() :
		(fOrient == kApRefFrame) ? aprefLoc.GetRotation() :
		(fOrient == kRootFrame) ? rootLoc.GetRotation() :
		loc.GetRotation());

	Vector offset = Rotate(offsetSpace, pEntry->m_offset);

	Quat rot = Quat(pEntry->m_rot.X(), pEntry->m_rot.Y(), pEntry->m_rot.Z());

	locOut->Set(loc.Pos() + offset, SafeNormalize(orientSpace * rot, Quat(kIdentity)));

	return true;
}
void NdParticleTracker::EventHandler(Event& event)
{
	if (event.GetMessage() == SID("turn-off"))
	{
		if (!IsState(SID("Active")))
			KillProcess(this);
	}
	else
	{
		Process::EventHandler(event);
	}
}

U32F NdParticleTracker::GetMaxStateAllocSize()
{
	return 64;
}

void NdParticleTracker::ProcessDispatchError(const char* strMsg)
{
	if (g_particleMgr.GetEnable())
	{
		MsgErr(strMsg);
	}
	KillProcess(this);
}

void NdParticleTracker::ChangeOwner(NdGameObject* pNewOwner)
{
	m_objHandle = pNewOwner;
	m_bObjHandleValid = m_objHandle.HandleValid();

	if (!m_bObjHandleValid)
	{
		// If we aren't parented any more, don't allow the particle to update!
		m_type = SID("part-effect-static");
	}
}

PROCESS_REGISTER_ALLOC_SIZE(NdParticleTracker, Process, 32 * 1024);

// ---------------------------------------------------------------------------------------------------------
void NdParticleTracker::Active::EventHandler(Event& event)
{
	NdParticleTracker& pp = Self();

	if (event.GetMessage() == SID("KillPartTracker"))
	{
		EffectControlSpawnInfo* pSpawnInfo = event.Get(0).GetPtr<EffectControlSpawnInfo*>();
		if (pSpawnInfo && (pSpawnInfo->m_name != INVALID_STRING_ID_64))
		{
			bool bMatchingId = (pSpawnInfo->m_id == INVALID_STRING_ID_64) || (pp.m_spawnInfo.m_id == pSpawnInfo->m_id);
			bool bMatchingSpawnerId = (pp.m_spawnInfo.m_spawnerId == INVALID_STRING_ID_64) || (pp.m_spawnInfo.m_spawnerId == pSpawnInfo->m_spawnerId);

			if ((pp.m_type == pSpawnInfo->m_type) && (pp.m_partName == pSpawnInfo->m_name) && (bMatchingId == true) && (bMatchingSpawnerId == true))
			{
				pp.m_linger = pSpawnInfo->m_bLingeringDeath;
				KillProcess(&pp);
			}
		}
	}
	else if (event.GetMessage() == SID("turn-off"))
	{
		::KillParticle(pp.m_hParticle, true);
	}
	else if (event.GetMessage() == SID("update-visibility-for-parent"))
	{
		if (!pp.m_visibilityBeforePhotoModeSet)
		{
			pp.m_visibilityBeforePhotoMode = g_particleMgr.GetVisible(pp.m_hParticle);
			pp.m_visibilityBeforePhotoModeSet = true;
		}

		bool wantVisible = pp.m_visibilityBeforePhotoMode;
		if (const NdGameObject* pOwnerGameObject = pp.m_objHandle.ToProcess())
		{
			wantVisible = !pOwnerGameObject->GetPhotoModeHidden();
		}

		g_particleMgr.SetVisible(pp.m_hParticle, wantVisible);
	}
}

void NdParticleTracker::Active::Update()
{
	NdParticleTracker& pp = Self();
	pp.m_elapsedTime += pp.GetClock()->GetDeltaTimeInSeconds();

	// If my tracked process dies, kill this process too
	if (pp.m_bObjHandleValid && (!pp.m_objHandle.HandleValid() || !pp.m_objHandle.ToProcess()->AllowAttachedEffects()))
		KillProcess(&pp);

	switch (pp.m_type.GetValue())
	{
	case SID_VAL("part-effect"):
		pp.UpdateParticle();
		break;
	case SID_VAL("part-effect-table"):
		pp.UpdateParticleTable();
		break;

	case SID_VAL("part-effect-static"):
		if (!pp.m_hParticle.IsValid())
			KillProcess(pp);
		break;

	default:
		break;
	}
}

STATE_REGISTER(NdParticleTracker, Active, kPriorityNormal);
