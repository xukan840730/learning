/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/ground-hug.h"

#include "gamelib/ndphys/collision-cast-interface.h"
#include "gamelib/ndphys/collision-filter.h"
#include "gamelib/ndphys/havok.h"
#include "gamelib/ndphys/rigid-body.h"
#include "gamelib/scriptx/h/ground-hug-defines.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-simple-instance.h"
#include "ndlib/anim/anim-simple-layer.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/debug/nd-dmenu.h"
#include "ndlib/nd-frame-state.h"
#include "ndlib/nd-game-info.h"
#include "ndlib/ndphys/rigid-body-base.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/tools-shared/patdefs.h"
#include "ndlib/water/rain-mgr.h"
#include "ndlib/water/water-mgr.h"
#include "ndlib/water/waterflow.h"
//#include "ndlib/debug/nd-dmenu.h"

#include "gamelib/fx/splashers.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/render/particle/particle.h"
#include "gamelib/script/nd-script-arg-iterator.h"



//-----------------------------------------------------------------------------
// Static
//-----------------------------------------------------------------------------
static StringId64 kSplasherWheelNames[GroundHugController::kMaxNumWheels] =
{
	SID("wheelFL"),			// kWheelFrontLeft,
	SID("wheelRL"),			// kWheelRearLeft,
	SID("wheelRR"),			// kWheelRearRight,
	SID("wheelFR"),			// kWheelFrontRight,
	INVALID_STRING_ID_64,		// kWheelAdditional1,
	INVALID_STRING_ID_64,		// kWheelAdditional2,
	INVALID_STRING_ID_64,		// kWheelAdditional3,
	INVALID_STRING_ID_64,		// kWheelAdditional4,
};

//-----------------------------------------------------------------------------
// GroundHugOptions
//-----------------------------------------------------------------------------

GroundHugOptions g_groundHugOptions;

GroundHugOptions::GroundHugOptions()
	: m_speedThresh(5.0f)
	, m_minSpeedForTireParticles_mps(3.0f)
	, m_wheelRotationSpeedScale(1.0f)
	, m_upSpringOverride(-1.0f)
	, m_upSpringStationaryOverride(-1.0f)
	, m_ySpringOverride(-1.0f)
	, m_ySpringStationaryOverride(-1.0f)

	, m_disable(false)
	, m_disableWheelParticles(false)
	, m_hugToSnow(true)
	, m_drawContacts(false)
	, m_drawContactsDetailed(false)
	, m_drawContactsOnTop(true)
	, m_drawWheels(false)
	, m_drawWheelsPreHug(false)
	, m_disableProceduralWheels(false)
	, m_drawRays(false)
	, m_debugWheelParticles(false)
	, m_drawSmoothedUpDir(false)
{
}

void GroundHugOptions::PopulateDevMenu(DMENU::Menu* pSubMenu)
{
	DMENU::ItemFloat* pFloat;

	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Disable Ground Hug", &m_disable));
	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Draw Ground Hug Contacts", &m_drawContacts));
	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Draw Ground Hug Contacts (Detailed)", &m_drawContactsDetailed));
	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Draw Ground Hug On Top", &m_drawContactsOnTop));
	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Draw Tire Particle Debug", &m_debugWheelParticles));
	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Disable Tire Particles", &m_disableWheelParticles));

	pFloat = NDI_NEW DMENU::ItemFloat("Ground Hug 'Up' Spring (Override DC)", DMENU::EditFloat, 6, "%7.1f", &m_upSpringOverride);
	pFloat->SetRangeAndStep(-1.0f, 1000.0f, 25.0f, 1.0f);
	pSubMenu->PushBackItem(pFloat);
	pFloat = NDI_NEW DMENU::ItemFloat("Ground Hug 'Up' Spring when Stationary (Override DC)", DMENU::EditFloat, 6, "%7.1f", &m_upSpringStationaryOverride);
	pFloat->SetRangeAndStep(-1.0f, 1000.0f, 25.0f, 1.0f);
	pSubMenu->PushBackItem(pFloat);

	pFloat = NDI_NEW DMENU::ItemFloat("Ground Hug 'Y' Spring (Override DC)", DMENU::EditFloat, 6, "%7.1f", &m_ySpringOverride);
	pFloat->SetRangeAndStep(-1.0f, 1000.0f, 25.0f, 1.0f);
	pSubMenu->PushBackItem(pFloat);
	pFloat = NDI_NEW DMENU::ItemFloat("Ground Hug 'Y' Spring when Stationary (Override DC)", DMENU::EditFloat, 6, "%7.1f", &m_ySpringStationaryOverride);
	pFloat->SetRangeAndStep(-1.0f, 1000.0f, 25.0f, 1.0f);
	pSubMenu->PushBackItem(pFloat);

	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Draw Ray Casts", &m_drawRays));
	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Draw Wheel Transforms", &m_drawWheels));
	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Draw Wheel Transforms (Pre Ground Hug)", &m_drawWheelsPreHug));
	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Disable Procedural Wheels", &m_disableProceduralWheels));
	pSubMenu->PushBackItem(NDI_NEW DMENU::ItemBool("Draw Smoothed Up Dir", &m_drawSmoothedUpDir));
}

//-----------------------------------------------------------------------------
// GroundHugManager
//-----------------------------------------------------------------------------
GroundHugManager g_hugGroundMgr;

GroundHugManager::GroundHugManager()
{
	m_numGroundHuggers = 0;
}

void GroundHugManager::AddGroundHugger(GroundHugController *pController)
{
	AtomicLockJanitor jj(&m_hugGroundLock, FILE_LINE_FUNC);

	ALWAYS_ASSERT(pController);
	ALWAYS_ASSERT(m_numGroundHuggers < kMaxGroundHuggers);

	m_aGroundHugControllers[m_numGroundHuggers++] = pController;
}

void GroundHugManager::RemoveGroundHugger(GroundHugController *pController)
{
	AtomicLockJanitor jj(&m_hugGroundLock, FILE_LINE_FUNC);

	ALWAYS_ASSERT(pController);

	for (int i=0; i<m_numGroundHuggers; i++)
	{
		if (m_aGroundHugControllers[i] == pController)
		{
			m_aGroundHugControllers[i] = m_aGroundHugControllers[--m_numGroundHuggers];
			return;
		}
	}

	ALWAYS_ASSERTF(0, ("GroundHugManager: Can't find GroundHugController to remove"));
}

void GroundHugManager::Relocate(GroundHugController *pController, ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	AtomicLockJanitor jj(&m_hugGroundLock, FILE_LINE_FUNC);

	for (int i=0; i<m_numGroundHuggers; i++)
	{
		if (pController == m_aGroundHugControllers[i])
		{
			RelocatePointer(m_aGroundHugControllers[i], delta, lowerBound, upperBound);
			ASSERT(delta == 0 || pController != m_aGroundHugControllers[i]);
		}
	}
}

RayCastJob & GroundHugManager::GetRayCastJob()
{
	return m_rayCastJob;
}

SphereCastJob & GroundHugManager::GetSphereCastJob()
{
	return m_sphereCastJob;
}

void GroundHugManager::PostRenderUpdate()
{
	PROFILE(GameLogic, HGM__PostRenderUpdate);

	if (EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
		return;

	SetUpCollCasts();
}

void GroundHugManager::SetUpCollCasts()
{
	AtomicLockJanitor jj(&m_hugGroundLock, FILE_LINE_FUNC);

	PROFILE(GameLogic, HGM_SetUpCollCasts);

	// Close old jobs first, if any.
	if (m_rayCastJob.IsValid())
	{
		m_rayCastJob.Close();
	}

	if (m_sphereCastJob.IsValid())
	{
		m_rayCastJob.Close();
	}

	int numGroundHuggers = m_numGroundHuggers;
	if (numGroundHuggers == 0)
	{
		return;
	}

	// Count up the number of rays and spheres we want to cast this frame.
	int rayCastCount = 0;
	int sphereCastCount = 0;
	for (int i = 0; i < numGroundHuggers; i++)
	{
		rayCastCount += m_aGroundHugControllers[i]->GetRayCastCount();
		sphereCastCount += m_aGroundHugControllers[i]->GetSphereCastCount();
	}


	// Open the jobs.
	if (rayCastCount != 0)
		m_rayCastJob.Open(rayCastCount, 1, 0, ICollCastJob::kClientVehicle);

	if (sphereCastCount != 0)
		m_sphereCastJob.Open(sphereCastCount, 1, 0, ICollCastJob::kClientVehicle);

	int rayIndex = 0;
	int sphereIndex = 0;
	for (int i = 0; i < numGroundHuggers; i++)
	{
		const NdGameObject* pObject = m_aGroundHugControllers[i]->GetGameObject();
		Locator objectLoc = pObject->GetLocator();
		if (m_aGroundHugControllers[i]->GetGroundHugInfo()->m_useSphereProbes)
		{
			m_aGroundHugControllers[i]->PopulateSphereCastJobNoLock(&sphereIndex, objectLoc);
		}
		else
		{
			m_aGroundHugControllers[i]->PopulateRayCastJobNoLock(&rayIndex, objectLoc);
		}
	}

	// Check that everyone lived up to their promises.
	ALWAYS_ASSERT(rayIndex == rayCastCount);
	ALWAYS_ASSERT(sphereIndex == sphereCastCount);

	// Kick the jobs.
	if (rayCastCount != 0)
		m_rayCastJob.Kick(FILE_LINE_FUNC);

	if (sphereCastCount != 0)
		m_sphereCastJob.Kick(FILE_LINE_FUNC);
}


//-----------------------------------------------------------------------------
// GroundHugController
//-----------------------------------------------------------------------------
GroundHugController::GroundHugController(NdGameObject* pObject, StringId64 hugGroundId)
	: m_pGroundHugInfo(hugGroundId)
	, m_groundHugId(hugGroundId)
{
	ALWAYS_ASSERT(pObject);
	ALWAYS_ASSERTF(m_pGroundHugInfo.Valid(), ("GroundHugController: Invalid ground hug info for object '%s'", pObject->GetName()));

	m_hObject = pObject;

	m_wheelRadius_m = 0.0f;
	m_numWheels = 0;
	m_numGroundHugWheels = 0;
	m_wheelRotateAxis = kUnitXAxis;
	m_finalizeWheelXfmsCounter = 2;

	m_ySpring = m_pGroundHugInfo->m_ySpring;
	m_ySpringStationary = m_pGroundHugInfo->m_ySpringStationary;
	m_upSpring = m_pGroundHugInfo->m_upSpring;
	m_upSpringStationary = m_pGroundHugInfo->m_upSpringStationary;

	m_enableExtraWaterHeightProbes = true;
	m_singleRayAnimStateInstanceIndex = 0;
	m_useSingleRayAtLocator = false;
	m_useSingleRayAtLocatorLastFrame = false;
	m_hasUsedSingleRayAtLocator = false;
	m_disabled = false;

	InitWheels();
}

F32 GroundHugController::GetFrontWheelZ()
{
	if (m_numWheels == 0)
		return 0.0f;

	return m_aWheel[kWheelFrontLeft].m_wheelToLocal.GetTranslation().Z();
}

F32 GroundHugController::GetRearWheelZ()
{
	if (m_numWheels == 0)
		return 0.0f;

	return m_aWheel[kWheelRearLeft].m_wheelToLocal.GetTranslation().Z();
}

void GroundHugController::InitWheelTransforms()
{
	ALWAYS_ASSERT(GetGameObject()->GetAnimControl());
	AnimControl& animControl = *GetGameObject()->GetAnimControl();
	ALWAYS_ASSERT(animControl.GetJointCache());
	JointCache& jointCache = *animControl.GetJointCache();

	const Locator locWs = GetGameObject()->GetLocator();

	ALWAYS_ASSERT(m_numWheels <= kMaxNumWheels);
	for (U32F iWheel = 0; iWheel < m_numWheels; ++iWheel)
	{
		m_aWheel[iWheel].m_angle_rad = 0.0f;

		I32F jointIndex = m_aWheel[iWheel].m_jointIndex;

		if (jointIndex >= 0)
		{
			const Locator& jointLocWs = jointCache.GetJointLocatorWs(jointIndex);
			Locator jointLocLs = locWs.UntransformLocator(jointLocWs);

			m_aWheel[iWheel].m_wheelToLocal = jointLocLs.AsTransform();
			m_aWheel[iWheel].m_localToWheel = jointLocLs.AsInverseTransform();
		}
	}

	// Because local space origin is on the ground and the wheel joint is at the center of the
	// wheel, the radius of the wheel is just the y coordinate of the wheel joint's local xfm.
	m_wheelRadius_m = m_aWheel[0].m_wheelToLocal.GetTranslation().Y();
}

void GroundHugController::InitWheels()
{
	const NdGameObject* pGo = GetGameObject();
	ALWAYS_ASSERT(pGo);

	if (GetGroundHugInfo()->m_wheels.m_count == 0)
	{
		m_numWheels = 0;
		return;
	}

	m_numWheels = GetGroundHugInfo()->m_wheels.m_count;
	ALWAYS_ASSERT(m_numWheels <= kMaxNumWheels);

	m_numGroundHugWheels = Min(m_numWheels, kMaxWheelRayCasts);

	ALWAYS_ASSERT(pGo->GetAnimControl());
	const AnimControl& animControl = *pGo->GetAnimControl();
	const FgAnimData& animData = animControl.GetAnimData();

	const SplasherSkeletonInfo* pSplasherSkel = pGo->GetSplasherSkeleton();

	for (U32F iWheel = 0; iWheel < m_numWheels; ++iWheel)
	{
		m_aWheel[iWheel].m_wheelIndex = iWheel;

		StringId64 jointNameId = GetGroundHugInfo()->m_wheels.m_array[iWheel].m_jointName;
		ALWAYS_ASSERT(jointNameId != INVALID_STRING_ID_64);

		m_aWheel[iWheel].m_jointIndex = animData.FindJoint(jointNameId);
		if (m_aWheel[iWheel].m_jointIndex < 0)
			MsgConScriptError("Can't find wheel '%s' for vehicle '%s'\n", DevKitOnly_StringIdToString(jointNameId), pGo->GetName());
		else
		{
			m_aWheel[iWheel].m_rotationSense = GetGroundHugInfo()->m_wheels.m_array[iWheel].m_rotationSense;
			m_aWheel[iWheel].m_updateOffset = GetGroundHugInfo()->m_updateWheelOffsets;

			m_aWheel[iWheel].m_bOnGround = false;
			m_aWheel[iWheel].m_hParticle = ParticleHandle();
			m_aWheel[iWheel].m_bParticleSpawnFailed = false;
			m_aWheel[iWheel].m_lastContactPat.m_bits = 0;
			m_aWheel[iWheel].m_contactNormal = kZero;
			m_aWheel[iWheel].m_contactPos = kZero;
		}

		m_aWheel[iWheel].m_splasherIndex = -1;
		if (pSplasherSkel)
			m_aWheel[iWheel].m_splasherIndex = pSplasherSkel->FindJointIndex(kSplasherWheelNames[iWheel]);
	}

	switch (GetGroundHugInfo()->m_wheelRotateAxis.GetValue())
	{
	case SID_VAL("x"):	m_wheelRotateAxis = Vector(kUnitXAxis); break;
	case SID_VAL("y"):	m_wheelRotateAxis = Vector(kUnitYAxis); break;
	case SID_VAL("z"):	m_wheelRotateAxis = Vector(kUnitZAxis); break;
	default:		m_wheelRotateAxis = Vector(kUnitXAxis); break;
	};

	ALWAYS_ASSERT(animControl.GetJointCache());
	const JointCache& jointCache = *animControl.GetJointCache();

	ALWAYS_ASSERT(m_numWheels <= kMaxNumWheels);
	for (U32F iWheel = 0; iWheel < m_numWheels; ++iWheel)
	{
		m_aWheel[iWheel].m_angle_rad = 0.0f;

		I32F jointIndex = m_aWheel[iWheel].m_jointIndex;

		if (jointIndex >= 0)
		{
			// use bind pose as initial guess, but the art is often wrong so we'll update this later
			jointCache.GetInverseBindPoseJointXform(m_aWheel[iWheel].m_localToWheel, jointIndex);
			m_aWheel[iWheel].m_wheelToLocal = Inverse(m_aWheel[iWheel].m_localToWheel);
		}
		else
		{
			m_aWheel[iWheel].m_wheelToLocal = m_aWheel[iWheel].m_localToWheel = Transform(kIdentity);
		}
	}

	// Because local space origin is on the ground and the wheel joint is at the center of the
	// wheel, the radius of the wheel is just the y coordinate of the wheel joint's local xfm.
	m_wheelRadius_m = m_aWheel[0].m_wheelToLocal.GetTranslation().Y();

	//InitWheelTransforms();
}

void GroundHugController::UpdateWheels()
{
	PROFILE(GameLogic, GroundHug_UpdateWheels);

	NdGameObject* pGo = GetGameObject();
	ALWAYS_ASSERT(pGo);

	// Wait a couple frames before finalizing the wheel transforms to ensure the joints have been updated
	m_finalizeWheelXfmsCounter--;
	if (m_finalizeWheelXfmsCounter < 0)
		m_finalizeWheelXfmsCounter = 0;
	else if (m_finalizeWheelXfmsCounter == 0)
		InitWheelTransforms();

	AnimControl* pAnimControl = pGo->GetAnimControl();

	if (!g_groundHugOptions.m_disableProceduralWheels && pAnimControl)
	{
		JointCache* pJointCache = pAnimControl->GetJointCache();
		if (pJointCache)
		{
			if (m_wheelRadius_m != 0.0f)
			{
				Vector v(pGo->GetVelocityWs());
				F32 dx = (F32)Length(v) * HavokGetDeltaTime();
				F32 deltaAngle_rad = dx / m_wheelRadius_m;
				static F32 kMaxDeltaAngle_rad = 1.5f; //PI_DIV_2; slightly smaller than 45 degrees looks better
				deltaAngle_rad = Min(deltaAngle_rad, kMaxDeltaAngle_rad);
				deltaAngle_rad *= g_groundHugOptions.m_wheelRotationSpeedScale;

				//ALWAYS_ASSERT(m_numWheels <= kMaxNumWheels);
				for (U32F iWheel = 0; iWheel < m_numWheels; ++iWheel)
				{
					Wheel &wheel = m_aWheel[iWheel];
					I32F jointIndex = wheel.m_jointIndex;

					if (jointIndex >= 0)
					{
						if (wheel.m_updateOffset)
						{
							const Locator& jointLocWs = pJointCache->GetJointLocatorWs(jointIndex);
							Locator jointLocLs = pGo->GetLocator().UntransformLocator(jointLocWs);

							m_aWheel[iWheel].m_wheelToLocal = jointLocLs.AsTransform();
							m_aWheel[iWheel].m_localToWheel = jointLocLs.AsInverseTransform();
						}

						if (wheel.m_rotationSense == 0.0f)
							continue;

						F32 wheelAngle_rad = wheel.m_angle_rad;

						F32 wheelDeltaAngle_rad = deltaAngle_rad * wheel.m_rotationSense;

						wheelAngle_rad += wheelDeltaAngle_rad;
						if (wheelAngle_rad > 2.0f*PI)
						{
							wheelAngle_rad -= 2.0f*PI;
						}

						wheel.m_angle_rad = wheelAngle_rad;

						Quat rotator = QuatFromAxisAngle(m_wheelRotateAxis, wheelAngle_rad);

						ndanim::JointParams jointParam = pJointCache->GetJointParamsLs(jointIndex);
						jointParam.m_quat = rotator;
						pJointCache->SetJointParamsLs(jointIndex, jointParam);

						if (FALSE_IN_FINAL_BUILD(g_groundHugOptions.m_drawWheels))
						{
							PrimAttrib primAttrib = (g_groundHugOptions.m_drawContactsOnTop) ? PrimAttrib(0) : PrimAttrib();

							const Locator locWs = pGo->GetLocator();

							Locator jointLocHs(Point(kOrigin), rotator);
							Locator jointLocLs = Locator(wheel.m_wheelToLocal).TransformLocator(jointLocHs);
							Locator jointLocWs = locWs.TransformLocator(jointLocLs);
							g_prim.Draw( DebugCoordAxes(jointLocWs, 1.0f, primAttrib) );

							const char* jointName = DevKitOnly_StringIdToString(GetGroundHugInfo()->m_wheels.m_array[iWheel].m_jointName);
							g_prim.Draw( DebugString(jointLocWs.GetTranslation(), jointName, kColorCyan, 0.7f) );
						}

					}
				}
			}
		}
	}
}


GroundHugController* GroundHugController::CreateGroundHugController(NdGameObject* pObject, StringId64 groundHugId)
{
	GroundHugController* pController = nullptr;

	ALWAYS_ASSERT(pObject);

	const DC::GroundHugInfo* pGroundHugInfo = ScriptManager::Lookup<DC::GroundHugInfo>(groundHugId, nullptr);
	if (!pGroundHugInfo)
		pGroundHugInfo = ScriptManager::LookupInNamespace<DC::GroundHugInfo>(groundHugId, SID("ai"), nullptr);
	ALWAYS_ASSERTF(pGroundHugInfo, ("GroundHugController: Cannot find hug info '%s for object '%s'", DevKitOnly_StringIdToStringOrHex(groundHugId), pObject->GetName()));

	if (pGroundHugInfo)
	{
		if (pGroundHugInfo->m_type == DC::kGroundHugTypeSolidGround)
			pController = NDI_NEW SolidGroundHugController(pObject, groundHugId);
		else if (pGroundHugInfo->m_type == DC::kGroundHugTypeDisplacementWater)
			pController = NDI_NEW WaterDisplacementHugController(pObject, groundHugId);
	}
	ALWAYS_ASSERTF(pController, ("GroundHugController: Invalid ground hug info for object '%s'", pObject->GetName()));

	return pController;
}

void GroundHugController::Wheel::UpdateParticles(GroundHugController& hugGroundController, const DC::GroundHugInfo* pGroundHugInfo)
{
	if (m_bOnGround && !g_groundHugOptions.m_disableWheelParticles && hugGroundController.GetObjectSpeed() > g_groundHugOptions.m_minSpeedForTireParticles_mps)
	{
		Scalar dt(HavokGetDeltaTime());

		bool frontWheel = m_wheelIndex == kWheelFrontLeft || m_wheelIndex == kWheelFrontRight;
		bool leftWheel = m_wheelIndex == kWheelFrontLeft || m_wheelIndex == kWheelRearLeft;

		const NdGameObject* pGo = hugGroundController.GetGameObject();

		Locator loc;
		loc.SetTranslation(m_contactPos);

		Vector forward = GetLocalZ(pGo->GetRotation());
		Quat rot = QuatFromLookAtDirs(forward, m_contactNormal, false);

		loc.SetRotation(rot);

		StringId64 particleId = INVALID_STRING_ID_64;
		if (pGroundHugInfo->m_wheelParticleGroups)
		{
			for(int i=0; i<pGroundHugInfo->m_wheelParticleGroups->m_count; i++)
			{
				if (pGroundHugInfo->m_wheelParticleGroups->m_array[i].m_pat == StringToStringId64(m_lastContactPat.GetSurfaceTypeName()))
				{
					if (frontWheel)
						if (leftWheel)
							particleId = pGroundHugInfo->m_wheelParticleGroups->m_array[i].m_particleGroupFl;
						else
							particleId = pGroundHugInfo->m_wheelParticleGroups->m_array[i].m_particleGroupFr;
					else
						if (leftWheel)
							particleId = pGroundHugInfo->m_wheelParticleGroups->m_array[i].m_particleGroupBl;
						else
							particleId = pGroundHugInfo->m_wheelParticleGroups->m_array[i].m_particleGroupBr;
					break;
				}
			}
		}

		//g_prim.Draw(DebugLine(loc.GetTranslation(), offsetY, kColorRed, 2.0f, PrimAttrib(0)));

		if (g_groundHugOptions.m_debugWheelParticles)
		{
			g_prim.Draw( DebugCoordAxes(loc, 0.5f) );

			char buf[80];
			sprintf(buf, "Pat: %s\nParticle: %s", m_lastContactPat.GetSurfaceTypeName(), particleId != INVALID_STRING_ID_64 ? DevKitOnly_StringIdToString(particleId) : "-");
			g_prim.Draw( DebugString(loc.GetTranslation(), buf, m_bParticleSpawnFailed ? kColorRed : kColorWhite ) );
		}

		if (IsParticleAlive(m_hParticle) && (particleId == INVALID_STRING_ID_64 || m_particleGroupId != particleId))
			KillParticle(m_hParticle, true);

		if (particleId != INVALID_STRING_ID_64)
		{
			BoundFrame bf(loc, pGo->GetBinding());
			if (!IsParticleAlive(m_hParticle))
			{
				m_hParticle = SpawnParticle(bf, particleId);
				m_particleGroupId = particleId;

				m_bParticleSpawnFailed = false;
				if (!m_hParticle.IsValid())
				{
					MsgScript("GroundHugController: Unable to find particle group '%s'.\n", DevKitOnly_StringIdToString(particleId));
					m_bParticleSpawnFailed = true;
					m_particleGroupId = INVALID_STRING_ID_64;
				}
			}
			else
			{
				g_particleMgr.SetLocation(m_hParticle, bf);
			}
		}
	}
	else
	{
		const U32 linger = 1;
		KillParticle(m_hParticle, linger);
		m_hParticle = ParticleHandle();
	}
}

float GroundHugController::GetObjectSpeed()
{
	BoxedValue result = SendEvent(SID("get-speed"), GetGameObject());

	float currentSpeed = 0.0f;
	if (result.IsValid())
		currentSpeed = result.GetFloat();

	return currentSpeed;
}


//-----------------------------------------------------------------------------
// SolidGroundHugController
//-----------------------------------------------------------------------------
SolidGroundHugController::SolidGroundHugController(NdGameObject* pObject, StringId64 hugGroundId)
	: GroundHugController(pObject, hugGroundId)
{
	ALWAYS_ASSERT(m_pGroundHugInfo->m_type == DC::kGroundHugTypeSolidGround);

	m_groundHugRayDir = kUnitYAxis;
	m_bGroundHugRaysCast = false;
	m_lastGoodNewUp = kUnitYAxis;
	m_newUp = kUnitYAxis;
	m_oldUp = kUnitYAxis;
	m_contactCount = 0;

	g_hugGroundMgr.AddGroundHugger(this);
}

void SolidGroundHugController::Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	g_hugGroundMgr.Relocate(this, delta, lowerBound, upperBound);
}

void SolidGroundHugController::OnKillProcess()
{
	for (U32F iWheel = 0; iWheel < m_numGroundHugWheels; ++iWheel)
	{
		KillParticle(m_aWheel[iWheel].m_hParticle, 0);
		m_aWheel[iWheel].m_hParticle = ParticleHandle();
	}

	g_hugGroundMgr.RemoveGroundHugger(this);
}

void SolidGroundHugController::Reset(const Vector* pNewUp /*= nullptr*/)
{
	const NdGameObject* pGo = GetGameObject();

	if (pNewUp)
	{
		m_newUp = m_oldUp = *pNewUp;
	}
	else
	{
		m_newUp = m_oldUp = pGo ? GetLocalY(pGo->GetLocator()) : kZero;
	}
	m_groundHugUpSpring.Reset();
	m_groundHugYSpring.Reset();
}

bool SolidGroundHugController::SphereGroundHug(const Locator& locPreGroundHug, Locator& locPostGroundHug)
{
	const NdGameObject* pGo = GetGameObject();
	if (!pGo)
		return false;

	bool bRaysCast = m_bGroundHugRaysCast;

	m_bGroundHugRaysCast = false;

	m_contactCount = 0;
	Point newPos = pGo->GetTranslation();
	Quat newRot = pGo->GetRotation();

	locPostGroundHug = locPreGroundHug;

	// don't inherit spring speed when we just switched to single ray mode
	if (!m_useSingleRayAtLocatorLastFrame && m_hasUsedSingleRayAtLocator)
	{
		m_groundHugYSpring.m_speed = 0.0f;
	}

	Locator oldLoc = pGo->GetLocator();

	// Needed if anim has no AP ref
	// NOTE: will STOMP align contribution from lower layers if animations on top layer doesn't have AP refs!
	/*
	AnimSimpleLayer* pBaseLayer = pGo->GetAnimControl()->GetSimpleLayerById(SID("base"));
	if (pBaseLayer && pBaseLayer->GetNumInstances() > 0)
	{
	AnimSimpleInstance* instance = pBaseLayer->GetInstance(0);
	if (instance->IsAlignedToActionPackOrigin())
	{
	BoundFrame bf = instance->GetApOrigin();
	oldLoc = bf.GetLocator();
	}
	}
	*/

	Point oldPos = oldLoc.GetTranslation();
	Quat oldRot(oldLoc.GetRotation());

	m_newUp = GetLocalY(oldRot);

	g_hugGroundMgr.m_hugGroundLock.AcquireLock(FILE_LINE_FUNC);

	SphereCastJob& sphereCastJob = g_hugGroundMgr.GetSphereCastJob();

	// Debug draw contacts if requested.
	PrimAttrib primAttrib = (g_groundHugOptions.m_drawContactsOnTop) ? PrimAttrib(0) : PrimAttrib();

	if (!bRaysCast || !sphereCastJob.IsValid())
	{
		g_hugGroundMgr.m_hugGroundLock.ReleaseLock();

		m_oldUp = m_newUp;
		//		pGo->SetLocator(locPostGroundHug); // needed?
	}
	else
	{
		// Make sure job is done.  Waiting multiple times is not a problem.
		sphereCastJob.Wait();

		if (FALSE_IN_FINAL_BUILD(g_groundHugOptions.m_drawRays))
		{
			sphereCastJob.DebugDraw(ICollCastJob::DrawConfig(kPrimDuration1FramePauseable));
		}

		// Determine:
		//   (a) my old pos/rot last frame (presumably on the ground), and
		//   (b) my new pos/rot on the rail (probably very much off the ground).

		const Point& railPos = locPreGroundHug.GetTranslation();
		const Quat& railRot = locPreGroundHug.GetRotation();
		Vector railFront = GetLocalZ(railRot);
		Vector railLeft = GetLocalX(railRot);
		Vector railUp = GetLocalY(railRot);
		ASSERT(IsNormal(railFront));
		ASSERT(IsNormal(railLeft));
		ASSERT(IsNormal(railUp));

		// Collect contact points.

		Contact aContact[kMaxNumWheels];

		//Vector tangent = pRC->m_vehicleSplineTracker.GetCurrentTangent(VehicleSplineTracker::kFront);
		//Vec3 tangent3(tangent);

		if (g_groundHugOptions.m_debugWheelParticles)
		{
			g_prim.Draw(DebugString(pGo->GetTranslation() + Vector(0.0f, 4.0f, 0.0f), DevKitOnly_StringIdToString(m_groundHugId)));
		}

		if (m_hasUsedSingleRayAtLocator)
		{
			if (sphereCastJob.IsContactValid(m_singleRayIndex, 0))
			{
				aContact[0].m_position = sphereCastJob.GetShapeContactPoint(m_singleRayIndex, 0);
				aContact[0].m_normal = sphereCastJob.GetContactNormal(m_singleRayIndex, 0);
				aContact[0].m_pDebugGo = sphereCastJob.GetContactObject(m_singleRayIndex, 0).m_hGameObject.ToProcess();
				m_contactCount = 1;
			}
		}
		else // regular routine
		{
			for (U32F iWheel = 0; iWheel < m_numGroundHugWheels; ++iWheel)
			{
				const U32F rayIndex = m_aWheel[iWheel].m_rayIndex;

				if (sphereCastJob.IsContactValid(rayIndex, 0))
				{
					aContact[m_contactCount].m_position = sphereCastJob.GetShapeContactPoint(rayIndex, 0);
					aContact[m_contactCount].m_normal = sphereCastJob.GetContactNormal(rayIndex, 0);
					aContact[m_contactCount].m_iWheel = iWheel;
					aContact[m_contactCount].m_pDebugGo = sphereCastJob.GetContactObject(rayIndex, 0).m_hGameObject.ToProcess();

					m_aWheel[iWheel].m_bOnGround = true;
					m_aWheel[iWheel].m_lastContactPat = sphereCastJob.GetContactPat(rayIndex, 0);
					m_aWheel[iWheel].m_contactPos = aContact[m_contactCount].m_position;
					m_aWheel[iWheel].m_contactNormal = aContact[m_contactCount].m_normal;

					++m_contactCount;
				}

				// Turn on or off particles if needed, and update them.
				m_aWheel[iWheel].UpdateParticles(*this, m_pGroundHugInfo);
			}
		} // end: regular routine

		if (m_contactCount == 4 && (Abs(aContact[0].m_position.Y() - aContact[1].m_position.Y()) > 2.0f ||
			Abs(aContact[0].m_position.Y() - aContact[2].m_position.Y()) > 2.0f ||
			Abs(aContact[0].m_position.Y() - aContact[3].m_position.Y()) > 2.0f))
		{
			int debug = 1;
		}

		if (FALSE_IN_FINAL_BUILD(g_groundHugOptions.m_drawContacts))
		{
			if (m_contactCount != 0)
			{
				Point prevPoint = aContact[0].m_position;

				for (I32F iContact = 1; iContact < m_contactCount; ++iContact)
				{
					g_prim.Draw(DebugLine(prevPoint, aContact[iContact].m_position, kColorYellow, 1.0f, primAttrib));
					g_prim.Draw(DebugLine(aContact[iContact].m_position, aContact[iContact].m_position + aContact[iContact].m_normal, kColorGreen, 1.0f, primAttrib));

					prevPoint = aContact[iContact].m_position;
				}

				if (m_contactCount > 1)
				{
					g_prim.Draw(DebugLine(prevPoint, aContact[0].m_position, kColorYellow, 1.0f, primAttrib));
				}
				g_prim.Draw(DebugLine(aContact[0].m_position, aContact[0].m_position + aContact[0].m_normal, kColorGreen, 1.0f, primAttrib));
			}

			Transform rootXfm = pGo->GetLocator().AsTransform();

			if (m_hasUsedSingleRayAtLocator)
			{
				const Point begin(sphereCastJob.GetProbeStart(m_singleRayIndex));
				g_prim.Draw(DebugCross(begin, 0.01f, kColorBlue, primAttrib));
			}
			else
			{
				for (U32F iWheel = 0; iWheel < m_numGroundHugWheels; ++iWheel)
				{
					const U32F rayIndex = m_aWheel[iWheel].m_rayIndex;
					const Point begin(sphereCastJob.GetProbeStart(rayIndex));

					g_prim.Draw(DebugCross(begin, 0.01f, (m_aWheel[iWheel].m_bOnGround) ? kColorBlue : kColorRed, primAttrib));

					// Draw original wheel locations.
					if (m_aWheel[iWheel].m_jointIndex >= 0)
					{
						const Transform wheelXfm = m_aWheel[iWheel].m_wheelToLocal * rootXfm;
						const Point wheelPos = wheelXfm.GetTranslation();
						g_prim.Draw(DebugCross(wheelPos, 0.01f, kColorMagenta, primAttrib));
					}
				}
			}
		}

		g_hugGroundMgr.m_hugGroundLock.ReleaseLock();

		// Now use the contact points to define a contact plane that will orient the vehicle.

		Point pointOnPlane;

		switch (m_contactCount)
		{
		case 0:
			//			MsgCon("Rail Vehicle %s can't find the ground!!!\n", GetGameObject()->GetName());
			// No contacts, just stick to spline (for jumps, for example).
			m_newUp = railUp;
			pointOnPlane = railPos;
			break;
		case 1:
			CalcVehicleUpVector_1(m_newUp, pointOnPlane, aContact, railUp);
			break;
		case 2:
			CalcVehicleUpVector_2(m_newUp, pointOnPlane, aContact, railFront, railLeft, railUp);
			break;
		case 3:
			CalcVehicleUpVector_3(m_newUp, pointOnPlane, aContact, railUp);
			break;
		case 4:
			CalcVehicleUpVector_4(m_newUp, pointOnPlane, aContact, railUp);
			break;
		default:
			ALWAYS_ASSERT(false);
			m_newUp = railUp;
			break;
		}

		ASSERT(IsNormal(m_newUp));

		float maxSlopeAngle = GetGroundHugInfo()->m_maxSlopeAngle;
		Scalar minCosine(cosf(DEGREES_TO_RADIANS(maxSlopeAngle)));	// ignore any inclination that's more than max slope angle degrees off vertical
		float newUpCos = Dot(m_newUp, kUnitYAxis);
		if (newUpCos < minCosine)
		{
			//enforce max slope angle
			m_newUp = railUp;
			locPostGroundHug = locPreGroundHug;
			return false;
			//pointOnPlane = railPos;
		}

		//MsgCon("new slope angle: %.3f\n", RADIANS_TO_DEGREES(Acos(newUpCos)));


		if (FALSE_IN_FINAL_BUILD(g_groundHugOptions.m_drawContactsDetailed && !g_groundHugOptions.m_drawSmoothedUpDir))
		{
			g_prim.Draw(DebugLine(pGo->GetTranslation(), m_newUp * 10.0f, kColorYellow, kColorRed));
		}

		Scalar cosineNewUp = Dot(m_newUp, kUnitYAxis);

		static F32 kMinSlideCosine = 0.2f;
		if (IsNegative(cosineNewUp - Scalar(kMinSlideCosine)))
		{
			m_newUp = m_lastGoodNewUp;
			cosineNewUp = Dot(m_newUp, kUnitYAxis);
		}
		else
		{
			m_lastGoodNewUp = m_newUp;
		}

		// Smooth the vehicle up vector a bit to avoid sudden pops.
		Vector newUpSmoothed;
		float upSpringEff = (g_groundHugOptions.m_upSpringOverride >= 0.0f) ? g_groundHugOptions.m_upSpringOverride : m_upSpring;
		float upSpringStationaryEff = (g_groundHugOptions.m_upSpringStationaryOverride >= 0.0f) ? g_groundHugOptions.m_upSpringStationaryOverride : m_upSpringStationary;
		if (upSpringEff > 0.0f)
		{
			// LERP between the normal spring and a very slow spring based on vehicle speed, to avoid strange behavior at slow speeds.
			F32 upSpring = LerpScale(0.0f, g_groundHugOptions.m_speedThresh, upSpringStationaryEff, upSpringEff, GetObjectSpeed());
			newUpSmoothed = m_groundHugUpSpring.Track(m_oldUp, m_newUp, GetProcessDeltaTime(), upSpring);
		}
		else
			newUpSmoothed = m_newUp;
		newUpSmoothed = SafeNormalize(newUpSmoothed, kUnitYAxis);

		// Detect sudden changes in vehicle "up" orientation; use them to trigger bump animations.

		// Generate the vehicle's rotation quat based on its forward vector from the spline and this new up vector.

		Vector newLeft = Cross(newUpSmoothed, railFront);
		Vector newFront = Cross(newLeft, newUpSmoothed);

		newFront = SafeNormalize(newFront, railFront);


		if (GetGroundHugInfo()->m_noRoll)
		{
			newLeft = Cross(kUnitYAxis, newFront);
			newUpSmoothed = Cross(newFront, newLeft);
			newUpSmoothed = SafeNormalize(newUpSmoothed, kUnitYAxis);
		}

		if (FALSE_IN_FINAL_BUILD(g_groundHugOptions.m_drawSmoothedUpDir))
		{
			g_prim.Draw(DebugLine(pGo->GetTranslation(), newUpSmoothed * 10.0f, kColorYellow, kColorRed));
		}

		newRot = QuatFromLookAt(newFront, newUpSmoothed);

		ALWAYS_ASSERT(IsNormal(newRot));

		// Debug draw old and new rotations.

		if (FALSE_IN_FINAL_BUILD(g_groundHugOptions.m_drawContactsDetailed))
		{
			g_prim.Draw(DebugCoordAxes(oldLoc, 2.0f, primAttrib, 1.0f));

			Locator intermedLoc = oldLoc;
			intermedLoc.SetRotation(newRot);
			g_prim.Draw(DebugCoordAxes(intermedLoc, 1.0f, primAttrib, 4.0f));
		}

		if (m_hasUsedSingleRayAtLocator && sphereCastJob.IsContactValid(m_singleRayIndex, 0))
		{
			newPos = PointFromXzAndY(railPos, aContact[0].m_position);
		}
		else
		{
			// Slide the vehicle down vertically so that its wheels touch the plane
			// defined by the contact points.

			Vector planeToVehicle = railPos - pointOnPlane;
			Scalar distanceToPlane = Dot(planeToVehicle, m_newUp);
			F32 maxTheoreticalSlide = GetGroundHugInfo()->m_groundProbeAboveWheel + GetGroundHugInfo()->m_groundProbeBelowWheel;
			//ASSERT((F32)distanceToPlane <= maxTheoreticalSlide);

			Scalar verticalDistance = distanceToPlane * Recip(cosineNewUp);
			Vector slide = Scalar(-1.0f) * verticalDistance * Vector(kUnitYAxis);
			//ASSERT((F32)Length(slide) <= maxTheoreticalSlide / kMinSlideCosine);

			ASSERT((F32)railPos.Y() < 10000.0f);

			newPos = railPos + slide;
		}

		// Smooth the y coordinate of the new locator to avoid sudden vertical jumps.

		float ySpringEff = (g_groundHugOptions.m_ySpringOverride >= 0.0f) ? g_groundHugOptions.m_ySpringOverride : m_ySpring;
		float ySpringStationaryEff = (g_groundHugOptions.m_ySpringStationaryOverride >= 0.0f) ? g_groundHugOptions.m_ySpringStationaryOverride : m_ySpringStationary;

		if (ySpringEff > 0.0f)
		{
			F32 oldPosY = oldPos.Y();

			// the vehicle is likely not aligned with the rail position on the XZ plane any more in single-ray mode
			// we need to use the AP ref to calibrate the vehicle's vertical position
			if (m_hasUsedSingleRayAtLocator)
			{
				if (AnimSimpleLayer* pLayer = pGo->GetAnimControl()->GetSimpleLayerById(SID("base")))
				{
					const U32 numInstances = pLayer->GetNumInstances();
					const U32 index = Min(m_singleRayAnimStateInstanceIndex, numInstances - 1);
					if (numInstances > 0)
					{
						oldPosY = pLayer->GetInstance(index)->GetApOrigin().GetTranslation().Y();
					}
				}
			}

			F32 newPosY = newPos.Y();

			// LERP between the normal spring and a very slow spring based on vehicle speed, to avoid strange behavior at slow speeds.
			F32 ySpring = LerpScale(0.0f, g_groundHugOptions.m_speedThresh, ySpringStationaryEff, ySpringEff, GetObjectSpeed());
			F32 newPosYSmoothed = m_groundHugYSpring.Track(oldPosY, newPosY, GetProcessDeltaTime(), ySpring);

			//			ALWAYS_ASSERT(fabsf(newPosYSmoothed) < 10000.0f);
			newPos.SetY(newPosYSmoothed);
		}

		// Set the new locator's translation and rotation.

		locPostGroundHug.SetTranslation(newPos);
		locPostGroundHug.SetRotation(newRot);

		GAMEPLAY_ASSERT(IsFinite(locPostGroundHug));

		//		pGo->SetLocator(locPostGroundHug); // needed?

		if (g_groundHugOptions.m_drawContactsDetailed)
		{
			g_prim.Draw(DebugCoordAxes(locPostGroundHug, 0.5f, primAttrib, 6.0f));

			g_prim.Draw(DebugLine(oldPos, newPos, kColorWhite, kColorRed, 1.0f, primAttrib));

			g_prim.Draw(DebugCross(pointOnPlane, 0.2f, kColorCyan, primAttrib));
		}

		m_oldUp = newUpSmoothed;
	}

	m_useSingleRayAtLocatorLastFrame = m_hasUsedSingleRayAtLocator;

	return (m_contactCount != 0);
}

bool SolidGroundHugController::GroundHug(const Locator& locPreGroundHug, Locator& locPostGroundHug)
{
	PROFILE(GameLogic, GroundHug_Work);

	if (GetGroundHugInfo()->m_useSphereProbes)
		return SphereGroundHug(locPreGroundHug, locPostGroundHug);

	const NdGameObject* pGo = GetGameObject();
	if (!pGo)
		return false;

	bool bRaysCast = m_bGroundHugRaysCast;
	m_bGroundHugRaysCast = false;

	m_contactCount = 0;
	Point newPos = pGo->GetTranslation();
	Quat newRot = pGo->GetRotation();

	locPostGroundHug = locPreGroundHug;

	// don't inherit spring speed when we just switched to single ray mode
	if (!m_useSingleRayAtLocatorLastFrame && m_hasUsedSingleRayAtLocator)
	{
		m_groundHugYSpring.m_speed = 0.0f;
	}

	Locator oldLoc = pGo->GetLocator();

	// Needed if anim has no AP ref
	// NOTE: will STOMP align contribution from lower layers if animations on top layer doesn't have AP refs!
	/*
	AnimSimpleLayer* pBaseLayer = pGo->GetAnimControl()->GetSimpleLayerById(SID("base"));
	if (pBaseLayer && pBaseLayer->GetNumInstances() > 0)
	{
		AnimSimpleInstance* instance = pBaseLayer->GetInstance(0);
		if (instance->IsAlignedToActionPackOrigin())
		{
			BoundFrame bf = instance->GetApOrigin();
			oldLoc = bf.GetLocator();
		}
	}
	*/

	Point oldPos =  oldLoc.GetTranslation();
	Quat oldRot(oldLoc.GetRotation());

	m_newUp = GetLocalY(oldRot);

	g_hugGroundMgr.m_hugGroundLock.AcquireLock(FILE_LINE_FUNC);

	RayCastJob & rayCastJob = g_hugGroundMgr.GetRayCastJob();

	if (!bRaysCast || !rayCastJob.IsValid())
	{
		g_hugGroundMgr.m_hugGroundLock.ReleaseLock();

		m_oldUp = m_newUp;
//		pGo->SetLocator(locPostGroundHug); // needed?
	}
	else
	{
		// Make sure job is done.  Waiting multiple times is not a problem.
		rayCastJob.Wait();

		if (FALSE_IN_FINAL_BUILD(g_groundHugOptions.m_drawRays))
		{
			rayCastJob.DebugDraw(ICollCastJob::DrawConfig(kPrimDuration1FramePauseable));
		}

		// Determine:
		//   (a) my old pos/rot last frame (presumably on the ground), and
		//   (b) my new pos/rot on the rail (probably very much off the ground).

		const Point& railPos = locPreGroundHug.GetTranslation();
		const Quat& railRot = locPreGroundHug.GetRotation();
		Vector railFront = GetLocalZ(railRot);
		Vector railLeft = GetLocalX(railRot);
		Vector railUp = GetLocalY(railRot);
		ASSERT(IsNormal(railFront));
		ASSERT(IsNormal(railLeft));
		ASSERT(IsNormal(railUp));

		// Collect contact points.

		Contact aContact[kMaxNumWheels];

		//Vector tangent = pRC->m_vehicleSplineTracker.GetCurrentTangent(VehicleSplineTracker::kFront);
		//Vec3 tangent3(tangent);

		if (g_groundHugOptions.m_debugWheelParticles)
		{
			g_prim.Draw( DebugString(pGo->GetTranslation() + Vector(0.0f, 4.0f, 0.0f), DevKitOnly_StringIdToString(m_groundHugId)) );
		}

		if (m_hasUsedSingleRayAtLocator)
		{
			if (rayCastJob.IsContactValid(m_singleRayIndex, 0))
			{
				aContact[0].m_position = rayCastJob.GetContactPoint(m_singleRayIndex, 0);
				aContact[0].m_normal = rayCastJob.GetContactNormal(m_singleRayIndex, 0);
				aContact[0].m_pDebugGo = rayCastJob.GetContactObject(m_singleRayIndex, 0).m_hGameObject.ToProcess();
				m_contactCount = 1;
			}
		}
		else // regular routine
		{
			for (U32F iWheel = 0; iWheel < m_numGroundHugWheels; ++iWheel)
			{
				const U32F rayIndex = m_aWheel[iWheel].m_rayIndex;

				if (rayCastJob.IsContactValid(rayIndex, 0))
				{
					aContact[m_contactCount].m_position = rayCastJob.GetContactPoint(rayIndex, 0);
					aContact[m_contactCount].m_normal = rayCastJob.GetContactNormal(rayIndex, 0);
					aContact[m_contactCount].m_iWheel = iWheel;
					aContact[m_contactCount].m_pDebugGo = rayCastJob.GetContactObject(rayIndex, 0).m_hGameObject.ToProcess();

					m_aWheel[iWheel].m_bOnGround = true;
					m_aWheel[iWheel].m_lastContactPat = rayCastJob.GetContactPat(rayIndex, 0);
					m_aWheel[iWheel].m_contactPos = aContact[m_contactCount].m_position;
					m_aWheel[iWheel].m_contactNormal = aContact[m_contactCount].m_normal;

					++m_contactCount;
				}

				// Turn on or off particles if needed, and update them.
				m_aWheel[iWheel].UpdateParticles(*this, m_pGroundHugInfo);
			}
		} // end: regular routine

		// Debug draw contacts if requested.

		PrimAttrib primAttrib = (g_groundHugOptions.m_drawContactsOnTop) ? PrimAttrib(0) : PrimAttrib();

		if (m_contactCount == 4 && (Abs(aContact[0].m_position.Y() - aContact[1].m_position.Y()) > 2.0f ||
			Abs(aContact[0].m_position.Y() - aContact[2].m_position.Y()) > 2.0f ||
			Abs(aContact[0].m_position.Y() - aContact[3].m_position.Y()) > 2.0f))
		{
			int debug = 1;
		}

		if (FALSE_IN_FINAL_BUILD(g_groundHugOptions.m_drawContacts))
		{
			if (m_contactCount != 0)
			{
				Point prevPoint = aContact[0].m_position;

				for (I32F iContact = 1; iContact < m_contactCount; ++iContact)
				{
					g_prim.Draw( DebugLine(prevPoint, aContact[iContact].m_position, kColorYellow, 1.0f, primAttrib) );
					g_prim.Draw( DebugLine(aContact[iContact].m_position, aContact[iContact].m_position + aContact[iContact].m_normal, kColorGreen, 1.0f, primAttrib) );

					prevPoint = aContact[iContact].m_position;
				}

				if (m_contactCount > 1)
				{
					g_prim.Draw( DebugLine(prevPoint, aContact[0].m_position, kColorYellow, 1.0f, primAttrib) );
				}
				g_prim.Draw( DebugLine(aContact[0].m_position, aContact[0].m_position + aContact[0].m_normal, kColorGreen, 1.0f, primAttrib) );
			}

			Transform rootXfm = pGo->GetLocator().AsTransform();

			if (m_hasUsedSingleRayAtLocator)
			{
				const Point begin(rayCastJob.GetProbeStart(m_singleRayIndex));
				g_prim.Draw(DebugCross(begin, 0.01f, kColorBlue, primAttrib));
			}
			else
			{
				for (U32F iWheel = 0; iWheel < m_numGroundHugWheels; ++iWheel)
				{
					const U32F rayIndex = m_aWheel[iWheel].m_rayIndex;
					const Point begin(rayCastJob.GetProbeStart(rayIndex));

					g_prim.Draw( DebugCross(begin, 0.01f, (m_aWheel[iWheel].m_bOnGround) ? kColorBlue : kColorRed, primAttrib));

					// Draw original wheel locations.
					if (m_aWheel[iWheel].m_jointIndex >= 0)
					{
						const Transform wheelXfm = m_aWheel[iWheel].m_wheelToLocal * rootXfm;
						const Point wheelPos = wheelXfm.GetTranslation();
						g_prim.Draw( DebugCross(wheelPos, 0.01f, kColorMagenta, primAttrib));
					}
				}
			}
		}

		g_hugGroundMgr.m_hugGroundLock.ReleaseLock();

		// Now use the contact points to define a contact plane that will orient the vehicle.

		Point pointOnPlane;

		switch (m_contactCount)
		{
		case 0:
//			MsgCon("Rail Vehicle %s can't find the ground!!!\n", GetGameObject()->GetName());
			// No contacts, just stick to spline (for jumps, for example).
			m_newUp = railUp;
			pointOnPlane = railPos;
			break;
		case 1:
			CalcVehicleUpVector_1(m_newUp, pointOnPlane, aContact, railUp);
			break;
		case 2:
			CalcVehicleUpVector_2(m_newUp, pointOnPlane, aContact, railFront, railLeft, railUp);
			break;
		case 3:
			CalcVehicleUpVector_3(m_newUp, pointOnPlane, aContact, railUp);
			break;
		case 4:
			CalcVehicleUpVector_4(m_newUp, pointOnPlane, aContact, railUp);
			break;
		default:
			ALWAYS_ASSERT(false);
			m_newUp = railUp;
			break;
		}

		ASSERT(IsNormal(m_newUp));

		float maxSlopeAngle = GetGroundHugInfo()->m_maxSlopeAngle;
		Scalar minCosine(cosf(DEGREES_TO_RADIANS(maxSlopeAngle)));	// ignore any inclination that's more than max slope angle degrees off vertical
		float newUpCos = Dot(m_newUp, kUnitYAxis);
		if (newUpCos < minCosine)
		{
			//enforce max slope angle
			m_newUp = railUp;
			locPostGroundHug = locPreGroundHug;
			return false;
			//pointOnPlane = railPos;
		}

		//MsgCon("new slope angle: %.3f\n", RADIANS_TO_DEGREES(Acos(newUpCos)));


		if (FALSE_IN_FINAL_BUILD(g_groundHugOptions.m_drawContactsDetailed))
		{
			g_prim.Draw(DebugLine(pGo->GetTranslation(), m_newUp * 10.0f, kColorYellow, kColorRed));
		}

		Scalar cosineNewUp = Dot(m_newUp, kUnitYAxis);

		static F32 kMinSlideCosine = 0.2f;
		if (IsNegative(cosineNewUp - Scalar(kMinSlideCosine)))
		{
			m_newUp = m_lastGoodNewUp;
			cosineNewUp = Dot(m_newUp, kUnitYAxis);
		}
		else
		{
			m_lastGoodNewUp = m_newUp;
		}

		// Smooth the vehicle up vector a bit to avoid sudden pops.
		Vector newUpSmoothed;
		float upSpringEff = (g_groundHugOptions.m_upSpringOverride >= 0.0f) ? g_groundHugOptions.m_upSpringOverride : m_upSpring;
		float upSpringStationaryEff = (g_groundHugOptions.m_upSpringStationaryOverride >= 0.0f) ? g_groundHugOptions.m_upSpringStationaryOverride : m_upSpringStationary;
		if (upSpringEff > 0.0f)
		{
			// LERP between the normal spring and a very slow spring based on vehicle speed, to avoid strange behavior at slow speeds.
			F32 upSpring = LerpScale(0.0f, g_groundHugOptions.m_speedThresh, upSpringStationaryEff, upSpringEff, GetObjectSpeed());
			newUpSmoothed = m_groundHugUpSpring.Track(m_oldUp, m_newUp, GetProcessDeltaTime(), upSpring);
		}
		else
			newUpSmoothed = m_newUp;
		newUpSmoothed = SafeNormalize(newUpSmoothed, kUnitYAxis);

		// Detect sudden changes in vehicle "up" orientation; use them to trigger bump animations.

		// Generate the vehicle's rotation quat based on its forward vector from the spline and this new up vector.

		Vector newLeft = Cross(newUpSmoothed, railFront);
		Vector newFront = Cross(newLeft, newUpSmoothed);

		newFront = SafeNormalize(newFront, railFront);


		if (GetGroundHugInfo()->m_noRoll)
		{
			newLeft = Cross(kUnitYAxis, newFront);
			newUpSmoothed = Cross(newFront, newLeft);
			newUpSmoothed = SafeNormalize(newUpSmoothed, kUnitYAxis);
		}

		newRot = QuatFromLookAt(newFront, newUpSmoothed);

		ALWAYS_ASSERT(IsNormal(newRot));

		// Debug draw old and new rotations.

		if (FALSE_IN_FINAL_BUILD(g_groundHugOptions.m_drawContactsDetailed))
		{
			g_prim.Draw( DebugCoordAxes(oldLoc, 2.0f, primAttrib, 1.0f) );

			Locator intermedLoc = oldLoc;
			intermedLoc.SetRotation(newRot);
			g_prim.Draw( DebugCoordAxes(intermedLoc, 1.0f, primAttrib, 4.0f) );
		}

		if (m_hasUsedSingleRayAtLocator && rayCastJob.IsContactValid(m_singleRayIndex, 0))
		{
			newPos = PointFromXzAndY(railPos, aContact[0].m_position);
		}
		else
		{
			// Slide the vehicle down vertically so that its wheels touch the plane
			// defined by the contact points.

			Vector planeToVehicle = railPos - pointOnPlane;
			Scalar distanceToPlane = Dot(planeToVehicle, m_newUp);
			F32 maxTheoreticalSlide = GetGroundHugInfo()->m_groundProbeAboveWheel + GetGroundHugInfo()->m_groundProbeBelowWheel;
			//ASSERT((F32)distanceToPlane <= maxTheoreticalSlide);

			Scalar verticalDistance = distanceToPlane * Recip(cosineNewUp);
			Vector slide = Scalar(-1.0f) * verticalDistance * Vector(kUnitYAxis);
			//ASSERT((F32)Length(slide) <= maxTheoreticalSlide / kMinSlideCosine);

			ASSERT((F32)railPos.Y() < 10000.0f);

			newPos = railPos + slide;
		}

		// Smooth the y coordinate of the new locator to avoid sudden vertical jumps.

		float ySpringEff = (g_groundHugOptions.m_ySpringOverride >= 0.0f) ? g_groundHugOptions.m_ySpringOverride : m_ySpring;
		float ySpringStationaryEff = (g_groundHugOptions.m_ySpringStationaryOverride >= 0.0f) ? g_groundHugOptions.m_ySpringStationaryOverride : m_ySpringStationary;

		if (ySpringEff > 0.0f)
		{
			F32 oldPosY = oldPos.Y();

			// the vehicle is likely not aligned with the rail position on the XZ plane any more in single-ray mode
			// we need to use the AP ref to calibrate the vehicle's vertical position
			if (m_hasUsedSingleRayAtLocator)
			{
				if (AnimSimpleLayer* pLayer = pGo->GetAnimControl()->GetSimpleLayerById(SID("base")))
				{
					const U32 numInstances = pLayer->GetNumInstances();
					const U32 index = Min(m_singleRayAnimStateInstanceIndex, numInstances - 1);
					if (numInstances > 0)
					{
						oldPosY = pLayer->GetInstance(index)->GetApOrigin().GetTranslation().Y();
					}
				}
			}

			F32 newPosY = newPos.Y();

			// LERP between the normal spring and a very slow spring based on vehicle speed, to avoid strange behavior at slow speeds.
			F32 ySpring = LerpScale(0.0f, g_groundHugOptions.m_speedThresh, ySpringStationaryEff, ySpringEff, GetObjectSpeed());
			F32 newPosYSmoothed = m_groundHugYSpring.Track(oldPosY, newPosY, GetProcessDeltaTime(), ySpring);

//			ALWAYS_ASSERT(fabsf(newPosYSmoothed) < 10000.0f);
			newPos.SetY(newPosYSmoothed);
		}

		// Set the new locator's translation and rotation.

		locPostGroundHug.SetTranslation(newPos);
		locPostGroundHug.SetRotation(newRot);

		GAMEPLAY_ASSERT(IsFinite(locPostGroundHug));

//		pGo->SetLocator(locPostGroundHug); // needed?

		if (g_groundHugOptions.m_drawContactsDetailed)
		{
			g_prim.Draw( DebugCoordAxes(locPostGroundHug, 0.5f, primAttrib, 6.0f) );

			g_prim.Draw( DebugLine(oldPos, newPos, kColorWhite, kColorRed, 1.0f, primAttrib) );

			g_prim.Draw( DebugCross(pointOnPlane, 0.2f, kColorCyan, primAttrib));
		}

		m_oldUp = newUpSmoothed;
	}

	m_useSingleRayAtLocatorLastFrame = m_hasUsedSingleRayAtLocator;

	return (m_contactCount != 0);
}

void SolidGroundHugController::CalcVehicleUpVector_1(Vector & newVehicleUp, Point & pointOnPlane,
	Contact aContact[], Vector_arg railUp)
{
	// Only one contact, just try to touch that one wheel to the ground but with pitch & yaw defined by the rail (no roll).
	newVehicleUp = railUp;
	pointOnPlane = aContact[0].m_position;
}

void SolidGroundHugController::CalcVehicleUpVector_2(Vector & newVehicleUp, Point & pointOnPlane,
	Contact aContact[],
	Vector_arg railFront, Vector_arg railLeft, Vector_arg railUp)
{
	// Find the delta between the two contact points.
	Vector delta = aContact[1].m_position - aContact[0].m_position;

	ALWAYS_ASSERT(aContact[0].m_iWheel < m_numWheels);
	ALWAYS_ASSERT(aContact[1].m_iWheel < m_numWheels);
	ALWAYS_ASSERT(aContact[0].m_iWheel < aContact[1].m_iWheel);
	ALWAYS_ASSERT(m_numGroundHugWheels >= 2 && m_numGroundHugWheels <= 4);

	// This delta may correspond to a front, back, diagonal front, diagonal back, or right vector,
	// depending on which wheels actually contacted the ground.
	//
	//    3-wheeled   4-wheeled
	//
	//        0	       0     3
	//        +	        +---+
	//       / \	    |   |
	//      /   \	    |   |
	//     +-----+	    +---+
	//    1       2    1     2

	U32 edgeDir = (aContact[0].m_iWheel << 4) | aContact[1].m_iWheel;
	Vector up;

	switch (edgeDir)
	{
	case 0x01:
	case 0x02:
		// delta points toward back of vehicle, so flip it to get front
		delta *= Scalar(-1.0f);
		delta = SafeNormalize(delta, railFront);
		up = Cross(delta, railLeft);
		newVehicleUp = SafeNormalize(up, railUp);
		break;
	case 0x03:
		ALWAYS_ASSERT(m_numGroundHugWheels == 4);
		FALLTHROUGH;
	case 0x12:
		// delta points toward right side of vehicle, so flip it to get left
		delta *= Scalar(-1.0f);
		delta = SafeNormalize(delta, railLeft);
		up = Cross(railFront, delta);
		newVehicleUp = SafeNormalize(up, railUp);
		break;
	case 0x13:
	case 0x23:
		// delta points toward front of vehicle
		ALWAYS_ASSERT(m_numGroundHugWheels == 4);
		delta = SafeNormalize(delta, railFront);
		up = Cross(delta, railLeft);
		newVehicleUp = SafeNormalize(up, railUp);
		break;
	default:
		ALWAYS_ASSERT(false);
		newVehicleUp = railUp;
	}

	pointOnPlane = aContact[0].m_position;
}

void SolidGroundHugController::CalcVehicleUpVector_3(Vector & newVehicleUp, Point & pointOnPlane,
	Contact aContact[], Vector_arg railUp, U32 i0, U32 i1, U32 i2)
{
	// NOTE: Wheels are always specified in counter-clockwise order (looking down on the vehicle).
	// Contacts are processed in wheel order.  So even if some wheels are not hitting anything and
	// so are skipped in the aContact[] array, this cross product should always be up, not down.

	Vector edge1 = aContact[i1].m_position - aContact[i0].m_position;
	Vector edge2 = aContact[i2].m_position - aContact[i0].m_position;

	Vector cross = Cross(edge1, edge2);

	if (cross.Y() < 0.0f)
	{
		cross = - cross;
	}

	newVehicleUp = SafeNormalize(cross, railUp);
	pointOnPlane = aContact[i0].m_position;
}

void SolidGroundHugController::CalcVehicleUpVector_4(Vector & newVehicleUp, Point & pointOnPlane,
	Contact aContact[], Vector_arg railUp)
{
	// A 4-wheeled vehicle yields 4 possible orientation planes, each defined by a different
	// triple of contact points.

	#if 1

		// Select the triple of points with the highest world-space Y values.

		Scalar minY(NDI_FLT_MAX);
		U32F iExclude = 0xFFFFFFFFu;

		for (U32F i = 0; i < 4; ++i)
		{
			Scalar y = aContact[i].m_position.Y();
			if (IsNegative(y - minY))
			{
				iExclude = i;
				minY = y;
			}
		}

		ALWAYS_ASSERT(iExclude != 0xFFFFFFFFu);

		U32F ai[3];
		for (U32F j = 0, i = 0; i < 4; ++i)
		{
			if (i != iExclude)
			{
				ALWAYS_ASSERT(j < 3);
				ai[j++] = i;
			}
		}

		CalcVehicleUpVector_3(newVehicleUp, pointOnPlane, aContact, railUp, ai[0], ai[1], ai[2]);

	#else

		// Find the most extreme deviation off of world y (but not TOO extreme).

		Vector aUp[4];
		Point aPoint[4];
		CalcVehicleUpVector_3(aUp[0], aPoint[0], aContact, railUp, 0, 1, 2);
		CalcVehicleUpVector_3(aUp[1], aPoint[1], aContact, railUp, 0, 1, 3);
		CalcVehicleUpVector_3(aUp[2], aPoint[2], aContact, railUp, 0, 2, 3);
		CalcVehicleUpVector_3(aUp[3], aPoint[3], aContact, railUp, 1, 2, 3);

		Scalar bestCosine(1.0f);
		float maxSlopeAngle = GetGroundHugInfo()->m_maxSlopeAngle;
		Scalar minCosine(cosf(DEGREES_TO_RADIANS(maxSlopeAngle)));	// ignore any inclination that's more than max slope angle degrees off vertical
		U32F iBest = 0xFFFFFFFFu;

		for (U32F i = 0; i < 4; ++i)
		{
			Scalar cosine = Dot(aUp[i], kUnitYAxis);
			if (IsNegative(cosine - bestCosine) && !IsNegative(cosine - minCosine))
			{
				bestCosine = cosine;
				iBest = i;
			}
		}

		if (iBest != 0xFFFFFFFFu)
		{
			newVehicleUp = aUp[iBest];
			pointOnPlane = aPoint[iBest];
		}
		else
		{
			// No inclination is acceptable, use last frame's up.
			newVehicleUp = railUp;
			pointOnPlane = aContact[0].m_position;
		}
	#endif
}


//--------------------------------------------------------------------------------------
U32F SolidGroundHugController::GetRayCastCount()
{
	if (g_groundHugOptions.m_disable || m_disabled)
		return 0;

	if (GetGroundHugInfo()->m_useSphereProbes)
		return 0;

	if (m_useSingleRayAtLocator)
		return 1;

	for (U32F iWheel = 0; iWheel < m_numGroundHugWheels; ++iWheel)
	{
		m_aWheel[iWheel].m_bOnGround = false;

		if (m_aWheel[iWheel].m_jointIndex < 0)
		{
			// Can't find one of our wheels - don't cast any rays.
			// Should never happen if the vehicles are set up correctly.
			return 0;
		}
	}

	return m_numGroundHugWheels;
}

U32F SolidGroundHugController::GetSphereCastCount()
{
	if (g_groundHugOptions.m_disable || m_disabled)
		return 0;

	if (!GetGroundHugInfo()->m_useSphereProbes)
		return 0;

	if (m_useSingleRayAtLocator)
		return 1;

	for (U32F iWheel = 0; iWheel < m_numGroundHugWheels; ++iWheel)
	{
		m_aWheel[iWheel].m_bOnGround = false;

		if (m_aWheel[iWheel].m_jointIndex < 0)
		{
			// Can't find one of our wheels - don't cast any rays.
			// Should never happen if the vehicles are set up correctly.
			return 0;
		}
	}

	return m_numGroundHugWheels;
}

void SolidGroundHugController::PopulateRayCastJobNoLock(int * pRayIndex, Locator objectLoc)
{
	PROFILE(GameLogic, GroundHug_PopulateRayCastJobNoLock);

	if (!GetRayCastCount())
	{
		return;
	}

	{
		// Estimate position for next frame
		Point pos = objectLoc.GetPosition();
		pos += GetObjectSpeed() * EngineComponents::GetNdFrameState()->m_deltaTime * GetLocalZ(objectLoc.GetRotation());
		objectLoc.SetPosition(pos);
	}

	const PrimAttrib primAttrib = (g_groundHugOptions.m_drawContactsOnTop) ? PrimAttrib(0) : PrimAttrib();

	ALWAYS_ASSERT(pRayIndex != nullptr);
	U32F rayIndex = *pRayIndex;

	RayCastJob & rayCastJob = g_hugGroundMgr.GetRayCastJob();

	if (g_groundHugOptions.m_drawRays)
	{
		g_prim.Draw( DebugCoordAxes(objectLoc, 2.0f, primAttrib, 1.0f) );
	}

	const Transform preGroundHugXfm = objectLoc.AsTransform();

	// Cast our rays along the vehicle's local "down" axis to maximize correlation of ray
	// hit points with the vehicle's wheel locations on the ground.

	const Vector rayDir = m_useSingleRayAtLocator ? -Vector(kUnitYAxis) : SafeNormalize(preGroundHugXfm.GetYAxis(), Vector(kUnitYAxis)) * Scalar(-1.0f);
	m_groundHugRayDir = rayDir;

	const Collide::LayerMask layerInclude = GetGroundHugInfo()->m_hugToForeground ? Collide::kLayerMaskGeneral : Collide::kLayerMaskBackground
		| (g_groundHugOptions.m_hugToWater ? Collide::kLayerMaskWater : 0)
		| (g_groundHugOptions.m_hugToSnow ? Collide::kLayerMaskSnow : 0);

	if (m_useSingleRayAtLocator)
	{
		const Point objectPos = objectLoc.GetPosition();
		const Point objectRayBegin = objectPos - Scalar(GetGroundHugInfo()->m_groundProbeAboveWheel) * rayDir;
		const Point objectRayEnd = objectPos + Scalar(GetGroundHugInfo()->m_groundProbeBelowWheel) * rayDir;

		rayCastJob.SetProbeExtents(rayIndex, objectRayBegin, objectRayEnd);
		rayCastJob.SetProbeFilter(rayIndex, CollideFilter(layerInclude, Pat(Pat::kPassThroughMask | Pat::kStealthVegetationMask | GetGroundHugInfo()->m_ignorePat), GetGameObject()));

		// Cache the ray index so we'll know where our results are later.
		m_singleRayIndex = rayIndex;
		++rayIndex;
	}
	else // regular ray setup
	{

		for (U32F iWheel = 0; iWheel < m_numGroundHugWheels; ++iWheel)
		{
			// If we can't find all our wheels, return without updating *pRayIndex.
			// Should never happen if the vehicles are set up correctly.
			if (m_aWheel[iWheel].m_jointIndex < 0)
				return;

			const Transform wheelXfm = m_aWheel[iWheel].m_wheelToLocal * preGroundHugXfm;
			const Point wheelPos = wheelXfm.GetTranslation();
			const Point begin = wheelPos - Scalar(GetGroundHugInfo()->m_groundProbeAboveWheel) * rayDir;
			const Point end = wheelPos + Scalar(GetGroundHugInfo()->m_groundProbeBelowWheel) * rayDir;

			rayCastJob.SetProbeExtents(rayIndex, begin, end);
			rayCastJob.SetProbeFilter(rayIndex, CollideFilter(layerInclude, Pat(Pat::kPassThroughMask | Pat::kStealthVegetationMask | GetGroundHugInfo()->m_ignorePat), GetGameObject()));

			if (FALSE_IN_FINAL_BUILD(g_groundHugOptions.m_drawWheelsPreHug))
			{
				const Locator jointLocLs(m_aWheel[iWheel].m_wheelToLocal);
				const Locator jointLocWs = objectLoc.TransformLocator(jointLocLs);
				g_prim.Draw( DebugCoordAxes(jointLocWs, 0.3f, primAttrib) );

				const char* jointName = DevKitOnly_StringIdToString(GetGroundHugInfo()->m_wheels.m_array[iWheel].m_jointName);
				g_prim.Draw( DebugString(jointLocWs.GetTranslation(), jointName, kColorGreen, 0.7f) );
			}

			// Cache the ray index so we'll know where our results are later.
			m_aWheel[iWheel].m_rayIndex = rayIndex;

			++rayIndex;
		}
		ALWAYS_ASSERT(rayIndex - *pRayIndex == m_numGroundHugWheels);
	} // end: regular ray setup

	*pRayIndex = (U32)rayIndex;
	m_bGroundHugRaysCast = true;

	m_hasUsedSingleRayAtLocator = m_useSingleRayAtLocator;
}

void SolidGroundHugController::PopulateSphereCastJobNoLock(int * pSphereIndex, Locator objectLoc)
{
	PROFILE(GameLogic, GroundHug_PopulateSphereCastJobNoLock);

	if (!GetSphereCastCount())
	{
		return;
	}

	{
		// Estimate position for next frame
		Point pos = objectLoc.GetPosition();
		pos += GetObjectSpeed() * EngineComponents::GetNdFrameState()->m_deltaTime * GetLocalZ(objectLoc.GetRotation());
		objectLoc.SetPosition(pos);
	}

	const PrimAttrib primAttrib = (g_groundHugOptions.m_drawContactsOnTop) ? PrimAttrib(0) : PrimAttrib();

	ALWAYS_ASSERT(pSphereIndex != nullptr);
	U32F sphereIndex = *pSphereIndex;

	SphereCastJob & sphereCastJob = g_hugGroundMgr.GetSphereCastJob();

	if (g_groundHugOptions.m_drawRays)
	{
		g_prim.Draw(DebugCoordAxes(objectLoc, 2.0f, primAttrib, 1.0f));
	}

	const Transform preGroundHugXfm = objectLoc.AsTransform();

	// Cast our rays along the vehicle's local "down" axis to maximize correlation of ray
	// hit points with the vehicle's wheel locations on the ground.

	const Vector rayDir = m_useSingleRayAtLocator ? -Vector(kUnitYAxis) : SafeNormalize(preGroundHugXfm.GetYAxis(), Vector(kUnitYAxis)) * Scalar(-1.0f);
	m_groundHugRayDir = rayDir;

	const Collide::LayerMask layerInclude = GetGroundHugInfo()->m_hugToForeground ? Collide::kLayerMaskGeneral : Collide::kLayerMaskBackground
		| (g_groundHugOptions.m_hugToWater ? Collide::kLayerMaskWater : 0)
		| (g_groundHugOptions.m_hugToSnow ? Collide::kLayerMaskSnow : 0);

	if (m_useSingleRayAtLocator)
	{
		const Point objectPos = objectLoc.GetPosition();
		const Point objectRayBegin = objectPos - Scalar(GetGroundHugInfo()->m_groundProbeAboveWheel) * rayDir;
		const Point objectRayEnd = objectPos + Scalar(GetGroundHugInfo()->m_groundProbeBelowWheel) * rayDir;
		sphereCastJob.SetProbeRadius(sphereIndex, GetGroundHugInfo()->m_sphereProbeRadius);
		sphereCastJob.SetProbeExtents(sphereIndex, objectRayBegin, objectRayEnd);
		sphereCastJob.SetProbeFilter(sphereIndex, CollideFilter(layerInclude, Pat(Pat::kPassThroughMask | Pat::kStealthVegetationMask | GetGroundHugInfo()->m_ignorePat), GetGameObject()));

		// Cache the ray index so we'll know where our results are later.
		m_singleRayIndex = sphereIndex;
		++sphereIndex;
	}
	else // regular ray setup
	{

		for (U32F iWheel = 0; iWheel < m_numGroundHugWheels; ++iWheel)
		{
			// If we can't find all our wheels, return without updating *pRayIndex.
			// Should never happen if the vehicles are set up correctly.
			if (m_aWheel[iWheel].m_jointIndex < 0)
				return;

			const Transform wheelXfm = m_aWheel[iWheel].m_wheelToLocal * preGroundHugXfm;
			const Point wheelPos = wheelXfm.GetTranslation();
			const Point begin = wheelPos - Scalar(GetGroundHugInfo()->m_groundProbeAboveWheel) * rayDir;
			const Point end = wheelPos + Scalar(GetGroundHugInfo()->m_groundProbeBelowWheel) * rayDir;

			sphereCastJob.SetProbeRadius(sphereIndex, GetGroundHugInfo()->m_sphereProbeRadius);
			sphereCastJob.SetProbeExtents(sphereIndex, begin, end);
			sphereCastJob.SetProbeFilter(sphereIndex, CollideFilter(layerInclude, Pat(Pat::kPassThroughMask | Pat::kStealthVegetationMask | GetGroundHugInfo()->m_ignorePat), GetGameObject()));

			if (FALSE_IN_FINAL_BUILD(g_groundHugOptions.m_drawWheelsPreHug))
			{
				const Locator jointLocLs(m_aWheel[iWheel].m_wheelToLocal);
				const Locator jointLocWs = objectLoc.TransformLocator(jointLocLs);
				g_prim.Draw(DebugCoordAxes(jointLocWs, 0.3f, primAttrib));

				const char* jointName = DevKitOnly_StringIdToString(GetGroundHugInfo()->m_wheels.m_array[iWheel].m_jointName);
				g_prim.Draw(DebugString(jointLocWs.GetTranslation(), jointName, kColorGreen, 0.7f));
			}

			// Cache the ray index so we'll know where our results are later.
			m_aWheel[iWheel].m_rayIndex = sphereIndex;

			++sphereIndex;
		}
		ALWAYS_ASSERT(sphereIndex - *pSphereIndex == m_numGroundHugWheels);
	} // end: regular ray setup

	*pSphereIndex = (U32)sphereIndex;
	m_bGroundHugRaysCast = true;

	m_hasUsedSingleRayAtLocator = m_useSingleRayAtLocator;
}

//-----------------------------------------------------------------------------
// WaterDisplacementHugController
//-----------------------------------------------------------------------------
WaterDisplacementHugController::WaterDisplacementHugController(NdGameObject* pObject, StringId64 hugGroundId)
	: GroundHugController(pObject, hugGroundId)
{
	ALWAYS_ASSERT(m_pGroundHugInfo->m_type == DC::kGroundHugTypeDisplacementWater);

	m_waterDisplacementIndex.Reset();

	m_initialized = false;
	m_debugOldVelY = 0.0f;
	m_debugMaxAccel = -1.0f;

	m_additionalHeightContacts = 8;
	m_numQueries = m_numGroundHugWheels + m_additionalHeightContacts;

	for (int i=0; i<m_numQueries; i++)
	{
		m_wheelPos[i] = Vec3(kZero);
		m_queryPoints[i] = Vec3(kZero);
		m_queryOffsets[i] = Vec3(kZero);
	}
}

void WaterDisplacementHugController::Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
}

void WaterDisplacementHugController::OnKillProcess()
{
}

void WaterDisplacementHugController::Reset(const Vector* pNewUp /*= nullptr*/)
{
	const NdGameObject* pGo = GetGameObject();

	if (pNewUp)
	{
		m_newUp = m_oldUp = *pNewUp;
	}
	else
	{
		m_newUp = m_oldUp = pGo ? GetLocalY(pGo->GetLocator()) : kZero;
	}
	m_groundHugUpSpring.Reset();
	m_groundHugYSpring.Reset();
}

bool WaterDisplacementHugController::GroundHug(const Locator& locPreGroundHug, Locator& locPostGroundHug)
{
	Point corners[4];
	int cornerMapping[4];

	locPostGroundHug = locPreGroundHug;

	if (!m_initialized)
	{
		m_groundHugUpSpring.Reset();
		m_groundHugYSpring.Reset();
		m_oldUp = kUnitYAxis;
		m_newUp = kUnitYAxis;
		m_oldPosY = locPreGroundHug.GetTranslation().Y();
		m_newPosY = m_oldPosY;
		m_localDisplacementXZ = kZero;
		m_initialized = true;
	}

	if (m_numGroundHugWheels == 0)
		return false;

	Transform preGroundHugXfm = locPreGroundHug.AsTransform();
	Transform wheelXfm;

	for (int i=0; i<m_numGroundHugWheels; i++)
	{
		if (m_aWheel[i].m_jointIndex < 0)
			return false;

		wheelXfm = m_aWheel[i].m_wheelToLocal * preGroundHugXfm;
		m_wheelPos[i] = wheelXfm.GetTranslation();
		//g_prim.Draw(DebugSphere(Point(m_wheelPos[i]), 0.25f, Color(1.0f, 1.0f, 0.0f), PrimAttrib(kPrimEnableWireframe, kPrimDisableDepthTest)));
	}

	if (m_additionalHeightContacts >= 2 && m_numGroundHugWheels >= 2)
	{
		Point frontCenter;
		if (m_numGroundHugWheels >= 4)
			frontCenter = 0.5f*(m_wheelPos[kWheelFrontLeft] + m_wheelPos[kWheelFrontRight]);
		else
			frontCenter = m_wheelPos[kWheelFrontLeft];

		Point backCenter;
		if (m_numGroundHugWheels >= 3)
			backCenter = 0.5f*(m_wheelPos[kWheelRearLeft] + m_wheelPos[kWheelRearRight]);
		else
			backCenter = m_wheelPos[kWheelRearLeft];

		Vector backToFront = frontCenter - backCenter;

		float pct = 0.0f;
		float pctIncrement = 1.0f/(m_additionalHeightContacts-1);
		for (int i=0; i<m_additionalHeightContacts; i++)
		{
			int offset = kMaxWheelRayCasts + i;

			m_wheelPos[offset] = backCenter + pct*backToFront;
			pct += pctIncrement;
		}
	}


	bool success = false;
	if (m_waterDisplacementIndex.m_index >= 0)
	{
		DisplacementResult *dinfo;
		success = g_waterMgr.GetDisplacement(m_waterDisplacementIndex, &dinfo);
		if (success)
		{
			Vec3 currSplinePos = Vec3(locPreGroundHug.GetTranslation());
			Vec3 displacementOffset = m_pGroundHugInfo->m_displacementXzPct*((Vec3)dinfo[m_numQueries].position - currSplinePos);
			displacementOffset.SetY(0.0f);
			for (int i=0; i<m_numQueries; i++)
			{
				Vec3 pos = (Vec3) dinfo[i].position;
				g_prim.Draw(DebugSphere(Point(pos), 0.25f, Color(0.0f, 0.75f, 0.5f)));
				m_queryOffsets[i] = pos - (m_wheelPos[i] + displacementOffset);
			}

			Vector avePos = kZero;
			for (int i=0; i<m_numGroundHugWheels; i++)
			{
				Vec3 pos = (Vec3) dinfo[i].position;
				avePos += pos;
			}
			avePos *= (1.0f/m_numGroundHugWheels);

			m_oldPosY = m_newPosY;

			Point pointOnPlane = Point(kZero) + avePos;
			//g_prim.Draw(DebugSphere(pointOnPlane, 0.25f, kColorRed, PrimAttrib(kPrimEnableWireframe, kPrimDisableDepthTest)));

			Vector pos0 = (Vec3) dinfo[0].position;
			Vector pos1 = (Vec3) dinfo[1].position;
			Vector pos2 = (Vec3) dinfo[2].position;
			Vector pos3 = (Vec3) dinfo[3].position;

			Vector upDir;
			switch (m_numGroundHugWheels)
			{
				case 1:
					upDir = kUnitYAxis;
					break;

				case 2:
				{
					Vector forwardDir = Vector(pos0 - pos1);
					Vector rightDir = Cross(forwardDir, kUnitYAxis);
					upDir = Cross(rightDir, forwardDir);
					upDir = SafeNormalize(upDir, kUnitYAxis);
					ASSERT(upDir.Y() >= 0.0f);
					break;
				}

				case 3:
				{
					Vector sideA = Vector(pos0 - pos1);
					Vector sideB = Vector(pos0 - pos2);
					upDir = Cross(sideA, sideB);
					upDir = SafeNormalize(upDir, kUnitYAxis);
					if (upDir.Y() < 0.0f)
						upDir = -upDir;
					break;
				}

				case 4:
				{
					Vector diagA = Vector(pos3 - pos1);
					Vector diagB = Vector(pos0 - pos2);
					upDir = Cross(diagA, diagB);
					upDir = SafeNormalize(upDir, kUnitYAxis);
					if (upDir.Y() < 0.0f)
						upDir = -upDir;
					break;
				}
			}

			if (m_enableExtraWaterHeightProbes)
			{
				float maxDot = 0.0f;
				for (int i=0; i<m_additionalHeightContacts; i++)
				{
					int offset = m_numGroundHugWheels + i;
					Vector toContact = Point(dinfo[offset].position) - pointOnPlane;
					float dot = Dot(toContact, upDir);
					if (dot > maxDot)
						maxDot = dot;
				}
				pointOnPlane += maxDot*upDir;
			}


			Point pos = locPreGroundHug.GetTranslation() + m_localDisplacementXZ;
			m_localDisplacementXZ = displacementOffset;

			float planeD = -Dot(upDir, pointOnPlane - Point(kZero));
			float posY = -(upDir.X()*pos.X() + upDir.Z()*pos.Z() + planeD)/upDir.Y();	// Solve for Y in plane equation

			posY += m_pGroundHugInfo->m_offsetY;

			float ySpringEff = (g_groundHugOptions.m_ySpringOverride >= 0.0f) ? g_groundHugOptions.m_ySpringOverride : m_ySpring;
			if (ySpringEff > 0.0f)
				m_newPosY = m_groundHugYSpring.Track(m_newPosY, posY, GetProcessDeltaTime(), ySpringEff);
			else
				m_newPosY = posY;

			pos.SetY(m_newPosY);

			float upSpringEff = (g_groundHugOptions.m_upSpringOverride >= 0.0f) ? g_groundHugOptions.m_upSpringOverride : m_upSpring;

			m_oldUp = m_newUp;
			if (upSpringEff > 0.0f)
				m_newUp = m_groundHugUpSpring.Track(m_newUp, upDir, GetProcessDeltaTime(), upSpringEff);
			else
				m_newUp = upDir;

			Vector forwardDir = GetLocalZ(locPreGroundHug.GetRotation());
			Vector rightDir = Cross(forwardDir, m_newUp);
			forwardDir = SafeNormalize(Cross(m_newUp, rightDir), forwardDir);

			Quat rot = QuatFromLookAt(forwardDir, m_newUp);

			//g_prim.Draw(DebugSphere(pos, 0.25f, kColorGreen, PrimAttrib(kPrimEnableWireframe, kPrimDisableDepthTest)));

			ALWAYS_ASSERT(IsFinite(pos));
			ALWAYS_ASSERT(IsFinite(rot));
			locPostGroundHug.SetTranslation(pos);
			locPostGroundHug.SetRotation(rot);

			NdGameObject* pPlayer = EngineComponents::GetNdGameInfo()->GetPlayerGameObject();
			if (pPlayer)
			{
				const RigidBody *pRB = pPlayer->GetBoundFrame().GetBinding().GetRigidBody();

				if (pRB && pRB->GetOwner() == m_hObject.ToProcess())
				{
					MsgCon("Hug ground height: %.2f\n", (float)avePos.Y());

					float dt = GetProcessDeltaTime();
					if (dt > 0.0f)
					{
						float vel = (m_newPosY - m_oldPosY)/dt;

						float accel = (vel - m_debugOldVelY)/dt;

						MsgCon("Hug ground Y vel: %.2f\n", vel);
						MsgCon("Hug ground Y accel: %.2f\n", accel);
						m_debugOldVelY = vel;
					}
				}
			}
		}
	}
	else //hack because water displacement doesn't seem to work in T2
	{

	}
	for (int i = 0; i < m_numQueries; i++)
		m_queryPoints[i] -= Vec3(m_queryOffsets[i]);
	m_queryPoints[m_numQueries] = locPreGroundHug.GetPosition();
	g_waterMgr.DisplacementQuery(m_numQueries + 1, m_queryPoints, m_waterDisplacementIndex, kWaterQueryAll & (~kWaterQueryExclusionBox));

	if (m_pGroundHugInfo->m_displacementWaterOccluder)
	{
		const NdGameObject* pGo = GetGameObject();
		// Setup occluder here...
		Sphere sphere = pGo->GetBoundingSphere();

		Point  center = sphere.GetCenter();
		Scalar radius = sphere.GetRadius();
		Vec4  c = center.GetVec4();
		F32   r = radius;
		g_rainMgr.SetRainOccluder(sphere);
	}




	return true;
}

//-----------------------------------------------------------------------------
// Script Funcs
//-----------------------------------------------------------------------------

SCRIPT_FUNC("ground-hug-enable", DcGroundHugEnable)
{
	SCRIPT_ARG_ITERATOR(args, 2);
	NdGameObject*  pGo = args.NextGameObject();
	bool enable = args.NextBoolean();

	if (pGo)
	{
		GroundHugController * pGroundHugController = pGo->GetGroundHugController();
		if (pGroundHugController)
			pGroundHugController->SetEnabled(enable);
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("ground-hug-set-springs", DcGroundHugSetSprings)
{
	SCRIPT_ARG_ITERATOR(args, 3);
	NdGameObject* pGo = args.NextGameObject();
	float ySpring = args.NextFloat();
	float upSpring = args.NextFloat();
	float ySpringStationary = args.NextFloat();
	float upSpringStationary = args.NextFloat();

	if (pGo)
	{
		GroundHugController* pController = pGo->GetGroundHugController();
		if (pController)
			pController->SetSpringConstants(ySpring, upSpring, ySpringStationary, upSpringStationary);
	}

	return ScriptValue(0);
}

SCRIPT_FUNC("ground-hug-reset-hack", DcGroundHugResetHack)
{
	SCRIPT_ARG_ITERATOR(args, 1);
	NdGameObject* pGo = args.NextGameObject();

	if (pGo)
	{
		SendEvent(SID("ground-hug-reset-hack"), pGo);
	}

	return ScriptValue(0);
}

