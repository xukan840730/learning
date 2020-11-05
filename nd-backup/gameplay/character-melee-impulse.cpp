
#include "gamelib/gameplay/character-melee-impulse.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/ndphys/collision-cast-interface.h"
#include "gamelib/ndphys/collision-filter.h"
#include "gamelib/ndphys/composite-body.h"
#include "gamelib/ndphys/havok-util.h"
#include "gamelib/ndphys/rigid-body.h"
#include "gamelib/ndphys/destruction.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/ndphys/rigid-body-base.h"
#include "ndlib/render/util/prim.h"

bool g_debugMeleeImpulse = false;

CharacterMeleeImpulse::CharacterMeleeImpulse()
	: m_numJoints(0)
{
}

CharacterMeleeImpulse::~CharacterMeleeImpulse()
{
	m_castJob.Close();
}

void CharacterMeleeImpulse::AddJoint(U32 jointIndex, F32 radius, F32 impulse)
{
	ALWAYS_ASSERT(m_numJoints < kMaxNumJoints);
	MeleeImpulseData& data = m_data[m_numJoints++];
	data.m_jointIndex = jointIndex;
	data.m_radius = radius;
	data.m_impulse = impulse;
	data.m_lastPos = Point(NDI_NANF, NDI_NANF, NDI_NANF); // set to NaNs to indicate it's not valid yet
}

void CharacterMeleeImpulse::RemoveJoint(U32 jointIndex)
{
	for (U32F i = 0; i<m_numJoints; i++)
	{
		if (m_data[i].m_jointIndex == jointIndex)
		{
			m_data[i] = m_data[m_numJoints-1];
			m_numJoints--;
			return;
		}
	}
	ASSERT(false); // joint index was not in the list
}

void CharacterMeleeImpulse::ResetJoints()
{
	m_numJoints = 0;
}

void CharacterMeleeImpulse::PostJointUpdate(const Character* pCharacter)
{
	const U32F kMaxContactsPerProbe = 5;

	m_castJob.Wait();
	if (m_castJob.IsValid())
	{
		Locator platformMovedLoc(kIdentity);
		if (const RigidBody* pPlatform = pCharacter->GetPlatformBoundRigidBody())
		{
			platformMovedLoc = pPlatform->GetLocatorCm().TransformLocator(Inverse(pPlatform->GetPreviousLocatorCm()));
		}

		U32F numBodies = 0;
		U32F bodyIds[kMaxContactsPerProbe*kMaxNumJoints];

		U32F numProbes = m_castJob.NumProbes();
		for (U32F i = 0; i<numProbes; i++)
		{
			U32F numCon = m_castJob.NumContacts(i);
			for (U32F j = 0; j<numCon; j++)
			{
				RigidBody* pBody = m_castJob.GetContactObject(i, j).m_pRigidBody;
				if (pBody)
				{
					// Avoid applying impulse twice to the same body
					bool found = false;
					U32F bodyId = pBody->GetHandleId();
					for (U32F k = 0; k<numBodies; k++)
					{
						if (bodyId == bodyIds[k])
						{
							found = true;
							break;
						}
					}
					if (!found)
					{
						ASSERT(numBodies < kMaxContactsPerProbe*kMaxNumJoints);
						bodyIds[numBodies++] = bodyId;

						Point pnt = m_castJob.GetContactPoint(i, j) + m_castJob.GetContactLever(i, j);
						Vector dir = m_castJob.GetProbeUnitDir(i);
						HavokImpulseData impulseData(platformMovedLoc.TransformPoint(pnt), platformMovedLoc.TransformVector(dir), 
							m_lastImpulses[i], m_castJob.GetProbeRadius(i), 0.3f);
						impulseData.m_keyframedVelocity = Dist(m_castJob.GetProbeStart(i), m_castJob.GetProbeEnd(i)) * 30.0f; // just assume we are at 30FPS

						DebugBreakage(pBody, impulseData.m_destructionImpulse, impulseData.m_pos, false, "from CharacterMeleeImpulse\n");
						pBody->ApplyDestructionHit(impulseData);
					}
				}
			}
		}
	}
	m_castJob.Close();

	if (m_numJoints > 0)
	{
		Locator platformMovedLoc(kIdentity);
		if (const RigidBody* pPlatform = pCharacter->GetPlatformBoundRigidBody())
		{
			platformMovedLoc = pPlatform->GetLocatorCm().TransformLocator(Inverse(pPlatform->GetPreviousLocatorCm()));
		}

		const JointCache& jointCache = pCharacter->GetAnimData()->m_jointCache;
		m_castJob.Open(m_numJoints, kMaxContactsPerProbe, ICollCastJob::kCollCastAllowMultipleResults);
		U32F iProbe = 0;
		for (U32F i = 0; i<m_numJoints; i++)
		{
			MeleeImpulseData& data = m_data[i];
			Point pos = jointCache.GetJointLocatorWs(data.m_jointIndex).GetPosition();
			if (IsFinite(data.m_lastPos))
			{
				Point lastPos = platformMovedLoc.TransformPoint(data.m_lastPos);
				m_castJob.SetProbeExtents(iProbe, lastPos, pos);
				m_castJob.SetProbeRadius(iProbe, data.m_radius);
				m_castJob.SetProbeFilter(iProbe, CollideFilter(Collide::kLayerMaskFgBig | Collide::kLayerMaskSmall));
				m_lastImpulses[iProbe] = data.m_impulse;
				iProbe++;

				if (g_debugMeleeImpulse)
				{
					DebugDrawLine(lastPos, pos, kColorGreen, Seconds(1.0f), &pCharacter->GetBoundFrame());
					DebugDrawSphere(pos, data.m_radius, kColorGreen, Seconds(1.0f), &pCharacter->GetBoundFrame());
				}
			}

			data.m_lastPos = pos;
		}
		if (iProbe > 0)
			m_castJob.Kick(FILE_LINE_FUNC, iProbe);
		else
			m_castJob.Close();
	}
}

