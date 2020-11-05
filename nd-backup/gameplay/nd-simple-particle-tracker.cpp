/*
* Copyright (c)2008 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited.
*/

#include "gamelib/gameplay/nd-simple-particle-tracker.h"

#include "gamelib/render/particle/particle.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/particle/particle-debug.h"
#include "ndlib/render/util/prim.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "ndlib/log-stream/log-stream.h"
#include "gamelib/ndphys/rigid-body.h"

class NdGameObject;
class RigidBody;

bool g_debugParticleTracker = false;

NdSimpleParticleTracker::SpawnData::SpawnData() : m_particleGroupId(INVALID_STRING_ID_64), m_inheritVelocity(false), m_waitForRaycastResult(false), m_saveType(0)
{
}

const char* NdSimpleParticleTracker::GetPidString() const
{
	static char s_buf[32];
	snprintf(s_buf, sizeof(s_buf)-1, "%u", (U32)GetProcessId());
	return s_buf;
}

Err NdSimpleParticleTracker::Init(const ProcessSpawnInfo& spawn)
{
	Err result = Process::Init(spawn);
	if (!result.Succeeded())
	{
		return result;
	}

	SetAllowThreadedUpdate(true);
	ChangeParentProcess(EngineComponents::GetProcessMgr()->m_pEffectTree);

	m_firstFrame = true;
	m_waitingForRaycastResult = 0;

	SpawnData* pInfo = (SpawnData*)spawn.m_pUserData;
	ALWAYS_ASSERT(pInfo);

	ALWAYS_ASSERT(pInfo->m_particleOwner.HandleValid());

	m_cachedSpawnData = *pInfo;

	if (pInfo->m_waitForRaycastResult)
	{
		m_waitingForRaycastResult = 10; // wait up to 10 frames.
	}
	else
	{
		// spawn immediately
		bool spawned = SpawnParticleFromInfo(pInfo);
		if (!spawned)
		{
			return Err::kErrAbort;
		}
	}

	return Err::kOK;
}

bool NdSimpleParticleTracker::SpawnParticleFromInfo(NdSimpleParticleTracker::SpawnData *pInfo)
{
	const NdGameObject* pOwnerGameObject = pInfo->m_particleOwner.ToProcess();

	RigidBody *pBindRigidBody = pInfo->m_rigidBodyBind.ToBody();

	m_inheritVelocity = pInfo->m_inheritVelocity;

	Locator& loc = pInfo->m_particleLocator;
	m_frame = BoundFrame(loc, Binding(pBindRigidBody));

	ParticleAssociation assoc;
	assoc.m_hProcess = pInfo->m_particleOwner;
	assoc.m_saveType = pInfo->m_saveType;

	if (m_inheritVelocity)
	{
		m_hParticle = SpawnParticleWithAssociation(m_frame, pInfo->m_particleGroupId, assoc);
	}
	else
	{
		// If we don't want to track velocity, we cannot bind to the rigid body. Instead we have to update the position each frame ourselves
		BoundFrame bf(loc, Binding());
		m_hParticle = SpawnParticleWithAssociation(bf, pInfo->m_particleGroupId, assoc);
	}

	if (!m_hParticle.IsValid())
	{
		if (g_particleMgr.GetDebugInfoRef().enableMissingPartDisplay)
		{
			GoError("NdSimpleParticleTracker: Failed to spawn effect '%s': %s!\n",
				DevKitOnly_StringIdToString(pInfo->m_particleGroupId), m_hParticle.GetErrorString());
		}
		return false;
	}

	return true;
}

enum EffectBodyPart
{
	kTorso = 0,
	kArms,
	kLegs,
	kHead
};

static EffectBodyPart JointToEffectBodyPart(StringId64 jointSid)
{
	switch (jointSid.GetValue())
	{
	case SID_VAL("spined"): return kTorso;
	case SID_VAL("headb"): return kHead;
	case SID_VAL("l_shoulder"): return kArms;
	case SID_VAL("r_shoulder"): return kArms;
	case SID_VAL("l_elbow"): return kArms;
	case SID_VAL("r_elbow"): return kArms;
	case SID_VAL("l_upper_leg"): return kLegs;
	case SID_VAL("r_upper_leg"): return kLegs;
	case SID_VAL("l_knee"): return kLegs;
	case SID_VAL("r_knee"): return kLegs;
	default:return kTorso;
	}
}


void NdSimpleParticleTracker::SpawnParticleOnRaycastResult(float u, float v, StringId64 jointSid)
{
	if (m_waitingForRaycastResult && !IsProcessDead() && m_cachedSpawnData.m_particleOwner.HandleValid())
	{
		if (SpawnParticleFromInfo(&m_cachedSpawnData))
		{
			g_particleMgr.SetFloat(m_hParticle, SID("posU"), u);
			g_particleMgr.SetFloat(m_hParticle, SID("posV"), v);

			EffectBodyPart bodyPart = JointToEffectBodyPart(jointSid);
			g_particleMgr.SetFloat(m_hParticle, SID("bodyPart"), float(bodyPart));

			LogLineDirect(kLogStreamsGeneral, cs("Setting particles root vars uv [[]] [[]] bodyPart [[]]"), u, v, float(bodyPart));
		}
	}
}

bool NdSimpleParticleTracker::GetCurrentLocator(Locator* locOut)
{
	*locOut = m_frame.GetLocator();

	return true;
}


void NdSimpleParticleTracker::ProcessUpdate()
{
	PROFILE(Processes, ParticleTracker_ProcessUpdate);

	if (m_waitingForRaycastResult)
	{
		// we're waiting for raycast result.
		--m_waitingForRaycastResult;
	}

	if (!IsParticleAlive(m_hParticle) && !m_firstFrame)
	{
		// we will kill process if particle died. but only if this is not first frame and we are not waiting for raycast result to spawn particle
		KillProcess(this);
	}
	else
	{
		if (!m_inheritVelocity)
		{
			Point pos = m_frame.GetTranslation();
			Quat rot = m_frame.GetRotation();

			g_particleMgr.SetLocation(m_hParticle, pos, rot);
		}

		if (g_debugParticleTracker)
		{
			Point pos = m_frame.GetTranslation();
			g_prim.Draw(DebugCross(pos, 0.1f, kColorOrange));
			g_prim.Draw(DebugString(pos, GetPidString(), kColorOrange));
		}
	}

	if (!m_waitingForRaycastResult)
	{
		// keep ourselves at first frame until particle is actually spawned
		m_firstFrame = false;
	}
}

NdSimpleParticleTracker::~NdSimpleParticleTracker()
{
	KillParticle(m_hParticle);
	m_hParticle = ParticleHandle();
}

void NdSimpleParticleTracker::ProcessDispatchError(const char* strMsg)
{
	if (g_particleMgr.GetEnable())
	{
		Point pos = m_frame.GetTranslation();
		g_prim.Draw(DebugString(pos, strMsg, kColorRed), Seconds(1));
		MsgErr(strMsg);
	}
	KillProcess(this);
}

PROCESS_REGISTER_ALLOC_SIZE(NdSimpleParticleTracker, Process, 1024);
