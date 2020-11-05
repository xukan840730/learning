/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/ndphys/rope/rope2.h"
#include "gamelib/ndphys/rope/rope2-internal.h"
#include "gamelib/ndphys/rope/rope2-collector.h"
#include "gamelib/ndphys/rope/rope-mgr.h"
#include "gamelib/ndphys/rope/physvectormath.h"
#include "gamelib/ndphys/rope/rope2-debug.h"
#include "gamelib/ndphys/rope/rope2-point-col.h"
#include "gamelib/ndphys/havok-internal.h"
#include "gamelib/ndphys/debugdraw/havok-debug-draw.h"
#include "ndlib/render/util/prim.h"
#include "gamelib/gameplay/nd-game-object.h"
//#include "gamelib/gameplay/process-phys-rope.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/process/debug-selection.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "gamelib/audio/sfx-process.h"
#include "gamelib/audio/arm.h"
#include "gamelib/render/particle/particle.h"

//#include <Collide/Shape/Convex/Capsule/hkpCapsuleShape>

#define SIN_45	0.70710678119f

float g_minSegmentFraction = 0.5f;

//float g_ropeMass1m = 1.0f;
//float g_ropeSegmentLength = 0.2f;
float g_damping = 0.3f;
float g_viscousDamping = 0.001f;

float g_dftlDamping = 0.8f;

float g_bendingStiffness = 0.1f;

float g_proporFriction = 1.0f;
float g_constFriction = 1.0f;

float g_proporSolverFriction = 0.0f;
float g_constSolverFriction = 0.001f;

float g_proporTensionFriction = 0.0f;
float g_constTensionFriction = 0.0f;
float g_proporSolverTensionFriction = 0.04f;
float g_constSolverTensionFriction = 0.01f;

F32 g_numItersPerEdge = 0.25f;
F32 g_numMultiGridItersPerEdge = 0.5f;

extern bool g_ropeUseGpu;
extern Scalar g_stepDt;

extern Vector kVecDown;

volatile F32 kRopeMaxSleepVel = 0.05f;
volatile F32 kRopeMinWakeupVel = 0.0001f;
volatile U32 kMinFrameCreep = 20;
volatile U32 kMaxFrameCreep = 45;

static const F32 kDebugPlaneSize = 0.1f;

bool g_straightenWithEdges = true;
bool g_straightenWithEdgesShortcutsOnly = true;

F32 g_freeBendAngle = 0.25f * PI / 0.15f; // 45deg. per 15cm

static const F32 kEndSoundRopeLength = 0.5f;

#if !FINAL_BUILD || ALLOW_ROPE_DEBUGGER_IN_FINAL
static Rope2Debugger* s_pRopeDebugger = nullptr;
#endif

bool s_enableRopeDebugger = true;

bool s_colCacheOverflow = false;

Rope2::Rope2()
	: m_bSimpleRope(false)
	, m_bInited(false)
	, m_bSleeping(false)
	, m_bCritical(false)
	, m_bCustomCollidersColCache(false)
	, m_pGpuWaitCounter(nullptr)
	, m_pCollector(nullptr)
	, m_pKeyTwistDir(nullptr)
	, m_pTwistDir(nullptr)
	, m_pPos(nullptr)
	, m_pVel(nullptr)
	, m_pLastPos(nullptr)
	, m_pConstr(nullptr)
	, m_pNodeFlags(nullptr)
	, m_pTensionFriction(nullptr)
	, m_pPointCol(nullptr)
	, m_pDistConstraints(nullptr)
	, m_pDistConstraintsVel(nullptr)
	, m_pFrictionConstraints(nullptr)
	, m_pFrictionConstConstraints(nullptr)
	, m_pPrevKeyIndex(nullptr)
	, m_pNextKeyIndex(nullptr)
	, m_numStrainedSounds(0)
	, m_pStrainedSounds(nullptr)
	, m_numHitSounds(0)
	, m_numSlideSounds(0)
	, m_pHitSounds(nullptr)
	, m_pSlideSounds(nullptr)
	, m_numEdgeSlideSounds(0)
	, m_pEdgeSlideSounds(nullptr)
	, m_numStrainedFx(0)
	, m_pStrainedFx(nullptr)
	, m_pDebugName(nullptr)
	, m_pOwner(nullptr)
	, m_pDebugger(nullptr)
	, m_pDebugPreProjPos(nullptr)
	, m_pDebugPreSlidePos(nullptr)
	, m_pDebugPreSlideCon(nullptr)
	, m_pDebugSlideTarget(nullptr)
	, m_pDebugTension(nullptr)
	, m_pDebugTensionBreak(nullptr)
	, m_useGpu(true)
	, m_emulOnCpu(false)
	, m_pCmpContext(nullptr)
	, m_bTeleport(false)
	, m_bResetDynamics(false)
	, m_stepJobId(-1)
{
}

Rope2::~Rope2()
{
	Destroy();
}

void Rope2::Destroy()
{
	if (m_pGpuWaitCounter)
	{
		g_ropeMgr.AddDelayedDestroyCounter(m_pGpuWaitCounter, 4);
		m_pGpuWaitCounter = nullptr;
	}
	if (m_pDebugger)
	{
		m_pDebugger->Reset(nullptr);
		m_pDebugger = nullptr;
	}
	for (U32F ii = 0; ii<m_numStrainedSounds; ii++)
	{
		KillProcess(m_pStrainedSounds[ii].m_hSound);
	}
	m_numStrainedSounds = 0;
	for (U32F ii = 0; ii<m_numStrainedFx; ii++)
	{
		KillParticle(m_pStrainedFx[ii].m_hFx, true);
		m_pStrainedFx[ii].m_hFx = ParticleHandle();
	}
	m_numStrainedFx = 0;

#if !FINAL_BUILD
	if (m_pDebugPreProjPos)
	{
		NDI_DELETE [] m_pDebugPreProjPos;
		NDI_DELETE [] m_pDebugPreSlidePos;
		NDI_DELETE [] m_pDebugPreSlideCon;
		NDI_DELETE [] m_pDebugSlideTarget;
		NDI_DELETE [] m_pDebugTension;
		NDI_DELETE [] m_pDebugTensionBreak;
		m_pDebugPreProjPos = nullptr;
	}
#endif
}


void Rope2::Init(Rope2InitData& data)
{
	PHYSICS_ASSERT(!m_bInited);

	m_fLength = data.m_length;
	m_fRadius = data.m_radius;
	m_fMaxRadius = data.m_radius;
	m_fStrainedCollisionOffset = data.m_radius;
	m_fSegmentLength = data.m_segmentLength;
	m_fDamping = g_damping;
	m_fViscousDamping = g_viscousDamping;
	m_fBendingStiffness = g_bendingStiffness;
	m_fFreeBendAngle = g_freeBendAngle;
	m_fProporFriction = g_proporFriction;
	m_fConstFriction = g_constFriction;
	m_fGravityFactor = EngineComponents::GetNdGameInfo()->m_zeroGMode ? 0.0f : 1.0f;
	m_vGravityOffset = kZero;
	m_fAabbExpansion = 1.0f;
	m_bAutoStrained = false;
	m_bNeverStrained = data.m_neverStrained;
	m_bAllowKeyStretchConstraints = data.m_allowKeyStretchConstraints;
	m_bAllowDistStretchConstraints = false;
	m_bNeverSleeps = false;
	m_bBuoyancy = false;
	m_bSolverFriction = data.m_enableSolverFriction || data.m_enablePostSolve;
	m_bPostSolve = data.m_enablePostSolve;
	m_minCharCollisionDist = 0.0f;
	m_scStepTime = m_scInvStepTime = kZero;
	m_fMassiveEndDist = -1.0f;
	m_fMassiveEndRatio = 1.0f;
	m_userDistanceConstraintBlend = 0.0f;
	m_bDistanceConstraintEnabled = true;

	m_fNumItersPerEdge = g_numItersPerEdge;
	m_fNumMultiGridItersPerEdge = g_numMultiGridItersPerEdge;

	F32 fMinSegmentLength = data.m_minSegmentFraction * data.m_segmentLength;
	m_maxNumPoints = (U32F)(data.m_length / fMinSegmentLength + 1.5f);
	m_maxNumKeyPoints = Min((U32F)kMaxNumKeyPoints, m_maxNumPoints);

	// these are allocated in the process heap of the owning process (ProcessPhysRope)
	m_pPos = NDI_NEW (kAlign16) Point[m_maxNumPoints];
	m_pVel = NDI_NEW (kAlign16) Vector[m_maxNumPoints];
	m_pLastPos = NDI_NEW (kAlign16) Point[m_maxNumPoints];
	m_pLastVel = NDI_NEW (kAlign16) Vector[m_maxNumPoints];
	m_pRopeDist = NDI_NEW (kAlign16) F32[m_maxNumPoints];
	m_pRadius = NDI_NEW (kAlign16) F32[m_maxNumPoints];
	m_pInvRelMass = NDI_NEW (kAlign16) F32[m_maxNumPoints];
	m_pConstr = NDI_NEW(kAlign16) Constraint[m_maxNumPoints];
	m_pNodeFlags = NDI_NEW(kAlign16) RopeNodeFlags[m_maxNumPoints];
	m_pKeyPos = NDI_NEW (kAlign16) Point[m_maxNumKeyPoints];
	m_pKeyVel = NDI_NEW (kAlign16) Vector[m_maxNumKeyPoints];
	m_pKeyRopeDist = NDI_NEW (kAlign16) F32[m_maxNumKeyPoints];
	m_pKeyNodeFlags = NDI_NEW(kAlign16) RopeNodeFlags[m_maxNumKeyPoints];
	m_pKeyRadius = NDI_NEW (kAlign16) RadiusKey[m_maxNumKeyPoints];
	m_pKeyMoveDist = NDI_NEW (kAlign16) F32[m_maxNumKeyPoints];
	m_pEdges = NDI_NEW(kAlign16) EdgePoint[m_maxNumPoints];
	m_pNodeAabb = NDI_NEW(kAlign16) Aabb[m_maxNumPoints];

	if (data.m_withSavePos)
	{
		PHYSICS_ASSERT(!data.m_neverStrained);
		m_pSaveEdges = NDI_NEW(kAlign16) EdgePoint[m_maxNumPoints];
		m_pDistConstraints = NDI_NEW(kAlign16) Vec4[m_maxNumPoints];
		m_pDistConstraintsVel = NDI_NEW(kAlign16) Vector[m_maxNumPoints];
		m_maxSelfCollision = Max(1u, Min(1600u, m_maxNumPoints * 4) / THREAD_COUNT) * THREAD_COUNT;
		m_pSelfCollision = NDI_NEW(kAlign16) I16[m_maxSelfCollision*2];
	}
	else
	{
		m_pSaveEdges = nullptr;
		m_pSelfCollision = nullptr;
		m_maxSelfCollision = 0;
	}

	if (!m_bAllowKeyStretchConstraints)
	{
		m_pPrevKeyIndex = NDI_NEW (kAlign16) I16[m_maxNumPoints];
		m_pNextKeyIndex = NDI_NEW (kAlign16) I16[m_maxNumPoints];
	}

	if (data.m_useTwistDir)
	{
		m_pKeyTwistDir = NDI_NEW (kAlign16) Vector[m_maxNumKeyPoints];
		m_pTwistDir = NDI_NEW Vector[m_maxNumPoints];
		for (U32 ii = 0; ii<m_maxNumPoints; ii++)
		{
			m_pTwistDir[ii] = kUnitYAxis;
		}
	}

	m_numSelfCollision = 0;

	m_numIgnoreCollisionBodies = 0;
	m_pIgnoreCollisionBodies = NDI_NEW RigidBodyHandle[kMaxNumIgnoreCollisionBodies];

	m_numCustomColliders = 0;
	m_numRigidBodyColliders = 0;

	m_pCollector = NDI_NEW Rope2Collector;

	m_colCache.Init(m_maxNumPoints, m_fLength);

	m_vConstraintHash = SMATH_VEC_SET_ZERO();
	m_numFramesCreep = 0;
	m_layerMask = Collide::kLayerMaskAll;
	m_strainedLayerMask = Collide::kLayerMaskAll;
	m_bSleeping = false;

	m_pRopeDist[0] = 0.0f;
	m_pRadius[0] = m_fRadius;
	m_pInvRelMass[0] = 1.0f;
	m_numPoints = 1;

	I32F numMidPoints = (U32F)(m_fLength / m_fSegmentLength + 0.5f) - 1;
	F32 segLen = m_fLength / (numMidPoints + 1);
	for (U32F ii = 0; ii<numMidPoints; ii++)
	{
		m_pRopeDist[m_numPoints] = (ii+1)*segLen;
		m_pRadius[m_numPoints] = m_fRadius;
		m_pInvRelMass[m_numPoints] = 1.0f;
		m_numPoints++;
	}

	m_pRopeDist[m_numPoints] = m_fLength;
	m_pRadius[m_numPoints] = m_fRadius;
	m_pInvRelMass[m_numPoints] = 1.0f;
	m_numPoints++;

	memset(m_pVel, 0, m_maxNumPoints*sizeof(m_pVel[0]));

	memset(m_pConstr, 0, sizeof(m_pConstr[0])*m_maxNumPoints);

	memset(m_pNodeFlags, 0, sizeof(m_pNodeFlags[0])*m_maxNumPoints);
	m_pNodeFlags[0] = kNodeKeyframed;

	if (m_bSolverFriction)
	{
		m_pFrictionConstraints = NDI_NEW(kAlign16) Vec4[m_maxNumPoints];
		m_pFrictionConstConstraints = NDI_NEW(kAlign16) F32[m_maxNumPoints];
		memset(m_pFrictionConstraints, 0, sizeof(m_pFrictionConstraints[0])*m_maxNumPoints);
		memset(m_pFrictionConstConstraints, 0, sizeof(m_pFrictionConstConstraints[0])*m_maxNumPoints);
	}

	if (m_bPostSolve)
	{
		m_pTensionFriction = NDI_NEW(kAlign16) F32[m_maxNumPoints];
		memset(m_pTensionFriction, 0, sizeof(m_pTensionFriction[0])*m_maxNumPoints);
	}

	m_numKeyPoints = 0;
	m_numLastKeyPoints = 0;
	m_numKeyRadius = 0;

	m_firstDynamicPoint = 0;
	m_lastDynamicPoint = m_numPoints-1;

	m_numEdges = 0;
	m_numSaveEdges = 0;
	m_saveEdgePosSet = false;
	m_savePosRopeDistSet = false;
	m_saveStartEdgePosSet = false;

	m_pExternSaveEdges = nullptr;
	m_numExternSaveEdges = 0;

	m_pRopeSkinningData = nullptr;

	m_pDebugName = "";

	m_bInited = true;

#if !FINAL_BUILD
	if (m_bPostSolve && Memory::IsDebugMemoryAvailable())
	{
		m_pDebugPreProjPos = NDI_NEW (kAllocDebug, kAlign16) Point[m_maxNumPoints];
		m_pDebugPreSlidePos = NDI_NEW (kAllocDebug, kAlign16) Point[m_maxNumPoints];
		m_pDebugPreSlideCon = NDI_NEW (kAllocDebug, kAlign16) Constraint[m_maxNumPoints];
		m_pDebugSlideTarget = NDI_NEW (kAllocDebug, kAlign16) F32[m_maxNumPoints];
		m_pDebugTension = NDI_NEW (kAllocDebug, kAlign16) F32[m_maxNumPoints];
		m_pDebugTensionBreak = NDI_NEW (kAllocDebug, kAlign16) bool[m_maxNumPoints];
		ResetNodeDebugInfo();
	}
#endif
}

void Rope2::InitExternSaveEdges()
{
	PHYSICS_ASSERT(!m_bNeverStrained && !m_pExternSaveEdges);
	m_pExternSaveEdges = NDI_NEW(kAlign16) EdgePoint[m_maxNumPoints];
	m_numExternSaveEdges = 0;
}

void Rope2::InitSounds(const Rope2SoundDef* pDef)
{
	m_soundDef = *pDef;
	if (m_soundDef.m_strainedSlideSound != INVALID_STRING_ID_64)
	{
		m_maxNumStrainedSounds = (U32F)(m_fLength / m_soundDef.m_strainedSoundDist) + 1;
		m_numStrainedSounds = 0;
		m_pStrainedSounds = NDI_NEW(kAlign16) StrainedSound[m_maxNumStrainedSounds];
	}
	if (m_soundDef.m_hitSound != INVALID_STRING_ID_64 || m_soundDef.m_endHitSound != INVALID_STRING_ID_64)
	{
		m_numHitSounds = 0;
		if (m_soundDef.m_hitSound != INVALID_STRING_ID_64)
		{
			F32 ropeLen = m_fLength;
			if (m_soundDef.m_endHitSound != INVALID_STRING_ID_64)
			{
				ropeLen -= kEndSoundRopeLength;
			}
			m_numHitSounds = (U32F)(ropeLen / m_soundDef.m_hitSoundDist) + 1;
		}
		if (m_soundDef.m_endHitSound != INVALID_STRING_ID_64)
		{
			m_numHitSounds++;
		}
		m_pHitSounds = NDI_NEW(kAlign16) HitSound[m_numHitSounds];
		memset(m_pHitSounds, 0, sizeof(m_pHitSounds[0])*m_numHitSounds);
	}
	if (m_soundDef.m_slideSound != INVALID_STRING_ID_64 || m_soundDef.m_endSlideSound != INVALID_STRING_ID_64)
	{
		m_numSlideSounds = 0;
		if (m_soundDef.m_slideSound != INVALID_STRING_ID_64)
		{
			F32 ropeLen = m_fLength;
			if (m_soundDef.m_endSlideSound != INVALID_STRING_ID_64)
			{
				ropeLen -= kEndSoundRopeLength;
			}
			m_numSlideSounds = (U32F)(ropeLen / m_soundDef.m_slideSoundDist) + 1;
		}
		if (m_soundDef.m_endSlideSound != INVALID_STRING_ID_64)
		{
			m_numSlideSounds++;
		}
		m_pSlideSounds = NDI_NEW(kAlign16) SlideSound[m_numSlideSounds];
		memset(m_pSlideSounds, 0, sizeof(m_pSlideSounds[0])*m_numSlideSounds);
	}
	if (m_soundDef.m_edgeSlideSound != INVALID_STRING_ID_64)
	{
		m_pEdgeSlideSounds = NDI_NEW(kAlign16) EdgeSlideSound[kMaxEdgeSlideSounds];
	}

	m_noHitSoundStartRopeDist = -1.0f;
	m_noHitSoundEndRopeDist = -1.0f;
}

void Rope2::InitFx(const Rope2FxDef* pDef)
{
	m_fxDef = *pDef;
	if (m_fxDef.m_strainedSlideFx != INVALID_STRING_ID_64)
	{
		m_maxNumStrainedFx = (U32F)(m_fLength / m_fxDef.m_strainedFxDist) + 1;
		m_numStrainedFx = 0;
		m_pStrainedFx = NDI_NEW(kAlign16) StrainedFx[m_maxNumStrainedFx];
	}
	if (m_fxDef.m_snowContactFx != INVALID_STRING_ID_64)
	{
		m_pSnowFxPos = NDI_NEW Point[kMaxSnowFx];
		m_iFirstSnowFx = -1;
	}
}

void Rope2::InitForSimpleStrainedEdgeDetection(Rope2InitData& data)
{
	PHYSICS_ASSERT(!m_bInited);

	m_fLength = data.m_length;
	m_fRadius = data.m_radius;
	m_fMaxRadius = data.m_radius;
	m_fStrainedCollisionOffset = data.m_radius;
	m_fSegmentLength = data.m_segmentLength;
	m_fDamping = g_damping;
	m_fViscousDamping = g_viscousDamping;
	m_fBendingStiffness = g_bendingStiffness;
	m_fFreeBendAngle = g_freeBendAngle;
	m_fProporFriction = g_proporFriction;
	m_fConstFriction = g_constFriction;
	m_fGravityFactor = EngineComponents::GetNdGameInfo()->m_zeroGMode ? 0.0f : 1.0f;
	m_vGravityOffset = kZero;
	m_fAabbExpansion = 1.0f;
	m_bAutoStrained = false;
	m_bNeverStrained = data.m_neverStrained;
	m_bAllowKeyStretchConstraints = data.m_allowKeyStretchConstraints;
	m_bAllowDistStretchConstraints = false;
	m_bNeverSleeps = false;
	m_bBuoyancy = false;
	m_bSolverFriction = data.m_enableSolverFriction || data.m_enablePostSolve;
	m_bPostSolve = data.m_enablePostSolve;
	m_minCharCollisionDist = 0.0f;
	m_scStepTime = m_scInvStepTime = kZero;
	m_fMassiveEndDist = -1.0f;
	m_fMassiveEndRatio = 1.0f;
	m_userDistanceConstraintBlend = 0.0f;

	m_fNumItersPerEdge = g_numItersPerEdge;
	m_fNumMultiGridItersPerEdge = g_numMultiGridItersPerEdge;

	F32 fMinSegmentLength = data.m_minSegmentFraction * data.m_segmentLength;
	m_maxNumPoints = (U32F)(data.m_length / fMinSegmentLength + 1.5f);
	m_maxNumKeyPoints = 0;

	// these are allocated in the process heap of the owning process (ProcessPhysRope)
	m_pPos = nullptr;
	m_pVel = nullptr;
	m_pLastPos = nullptr;
	m_pLastVel = nullptr;
	m_pRopeDist = nullptr;
	m_pRadius = nullptr;
	m_pInvRelMass = nullptr;
	m_pConstr = nullptr;
	m_pNodeFlags = nullptr;
	m_pKeyPos = nullptr;
	m_pKeyVel = nullptr;
	m_pKeyRopeDist = nullptr;
	m_pKeyNodeFlags = nullptr;
	m_pKeyRadius = nullptr;
	m_pKeyMoveDist = nullptr;
	m_pEdges = NDI_NEW(kAlign16) EdgePoint[m_maxNumPoints];
	m_pNodeAabb = nullptr;

	m_pSaveEdges = nullptr;
	m_pSelfCollision = nullptr;
	m_maxSelfCollision = 0;
	m_numSelfCollision = 0;

	m_numIgnoreCollisionBodies = 0;
	m_pIgnoreCollisionBodies = NDI_NEW RigidBodyHandle[kMaxNumIgnoreCollisionBodies];

	m_numCustomColliders = 0;
	m_numRigidBodyColliders = 0;

	m_pCollector = NDI_NEW Rope2Collector;

	m_colCache.Init(m_maxNumPoints, m_fLength);

	m_vConstraintHash = SMATH_VEC_SET_ZERO();
	m_numFramesCreep = 0;
	m_layerMask = Collide::kLayerMaskAll;
	m_strainedLayerMask = Collide::kLayerMaskAll;
	m_bSleeping = false;

	m_numPoints = 0;

	m_numKeyPoints = 0;
	m_numLastKeyPoints = 0;
	m_numKeyRadius = 0;

	m_firstDynamicPoint = 0;
	m_lastDynamicPoint = 0;

	m_numEdges = 0;
	m_numSaveEdges = 0;
	m_saveEdgePosSet = false;
	m_savePosRopeDistSet = false;
	m_saveStartEdgePosSet = false;

	m_pExternSaveEdges = nullptr;
	m_numExternSaveEdges = 0;

	m_pRopeSkinningData = nullptr;

	m_pDebugName = "";

	m_bInited = true;
}

void Rope2::SetDebugger(Rope2Debugger* pDebugger)
{
	if (m_pDebugger == pDebugger)
		return;

	m_pDebugger = pDebugger;
	m_pDebugger->Reset(this);
}

void Rope2::InitStraightPose(const Point &ptStart, const Point &ptEnd)
{
	Vector dir = ptEnd - ptStart;

	m_pPos[0] = ptStart;
	for (U32F ii = 1; ii<m_numPoints-1; ii++)
	{
		m_pPos[ii] = ptStart + m_pRopeDist[ii]/m_fLength*dir;
	}
	m_pPos[m_numPoints-1] = ptEnd;

	memcpy(m_pLastPos, m_pPos, m_numPoints*sizeof(m_pPos[0]));

	if (m_pTwistDir)
	{
		Vector twistDir = Cross(dir, dir.Y() > 0.9f ? Vector(kUnitXAxis) : Vector(kUnitYAxis));
		twistDir = Normalize(Cross(twistDir, dir));
		for (U32F ii = 0; ii<m_numPoints; ii++)
		{
			m_pTwistDir[ii] = twistDir;
		}
	}
}

void Rope2::ResetSim()
{
	CheckStepNotRunning();

	memcpy(m_pLastPos, m_pPos, m_numPoints*sizeof(m_pPos[0]));
	memset(m_pVel, 0, m_numPoints*sizeof(m_pVel[0]));
	memset(m_pLastVel, 0, m_numPoints*sizeof(m_pLastVel[0]));
}

void Rope2::ResetNodeDebugInfo()
{
#if !FINAL_BUILD
	if (m_pDebugPreProjPos)
	{
		memcpy(m_pDebugPreProjPos, m_pPos, m_numPoints*sizeof(m_pPos[0]));
		memcpy(m_pDebugPreSlidePos, m_pPos, m_numPoints*sizeof(m_pPos[0]));
		memcpy(m_pDebugPreSlideCon, m_pConstr, m_numPoints*sizeof(m_pConstr[0]));
		for (U32F ii = 0; ii<m_numPoints; ii++)
			m_pDebugSlideTarget[ii] = (F32)ii;
		memset(m_pDebugTension, 0, m_numPoints*sizeof(m_pDebugTension[0]));
		memset(m_pDebugTensionBreak, 0, m_numPoints*sizeof(m_pDebugTensionBreak[0]));
	}
#endif
}

void Rope2::Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	CheckStepNotRunning();

	RelocatePointer( m_pCollector,	delta, lowerBound, upperBound );
	RelocatePointer( m_pPos,		delta, lowerBound, upperBound );
	RelocatePointer( m_pVel,		delta, lowerBound, upperBound );
	RelocatePointer( m_pLastPos,	delta, lowerBound, upperBound );
	RelocatePointer( m_pLastVel,	delta, lowerBound, upperBound );
	RelocatePointer( m_pRopeDist,	delta, lowerBound, upperBound );
	RelocatePointer( m_pRadius,		delta, lowerBound, upperBound );
	RelocatePointer( m_pInvRelMass,	delta, lowerBound, upperBound );
	RelocatePointer( m_pConstr,		delta, lowerBound, upperBound );
	RelocatePointer( m_pNodeFlags,	delta, lowerBound, upperBound );
	RelocatePointer( m_pNodeAabb,	delta, lowerBound, upperBound );
	RelocatePointer( m_pTensionFriction, delta, lowerBound, upperBound );
	RelocatePointer( m_pKeyPos,		delta, lowerBound, upperBound );
	RelocatePointer( m_pKeyVel,		delta, lowerBound, upperBound );
	RelocatePointer( m_pKeyRopeDist, delta, lowerBound, upperBound );
	RelocatePointer( m_pKeyNodeFlags, delta, lowerBound, upperBound );
	RelocatePointer( m_pKeyRadius,	delta, lowerBound, upperBound );
	RelocatePointer( m_pKeyMoveDist, delta, lowerBound, upperBound );
	RelocatePointer( m_pKeyTwistDir, delta, lowerBound, upperBound );
	RelocatePointer( m_pTwistDir, delta, lowerBound, upperBound );
	RelocatePointer( m_pEdges,		delta, lowerBound, upperBound );
	RelocatePointer( m_pSaveEdges,	delta, lowerBound, upperBound );
	RelocatePointer( m_pExternSaveEdges, delta, lowerBound, upperBound );
	RelocatePointer( m_pIgnoreCollisionBodies, delta, lowerBound, upperBound );
	RelocatePointer( m_pDistConstraints, delta, lowerBound, upperBound );
	RelocatePointer( m_pDistConstraintsVel, delta, lowerBound, upperBound );
	RelocatePointer( m_pFrictionConstraints, delta, lowerBound, upperBound );
	RelocatePointer( m_pFrictionConstConstraints, delta, lowerBound, upperBound );
	RelocatePointer( m_pPrevKeyIndex, delta, lowerBound, upperBound );
	RelocatePointer( m_pNextKeyIndex, delta, lowerBound, upperBound );
	RelocatePointer( m_pStrainedSounds, delta, lowerBound, upperBound );
	RelocatePointer( m_pHitSounds, delta, lowerBound, upperBound );
	RelocatePointer( m_pSlideSounds, delta, lowerBound, upperBound );
	RelocatePointer( m_pEdgeSlideSounds, delta, lowerBound, upperBound );
	RelocatePointer( m_pStrainedFx, delta, lowerBound, upperBound );
	RelocatePointer( m_pSnowFxPos, delta, lowerBound, upperBound );
	RelocatePointer( m_pSelfCollision, delta, lowerBound, upperBound );
	m_colCache.Relocate(delta, lowerBound, upperBound);
	for (U32F ii = 0; ii < m_numCustomColliders; ii++)
		RelocatePointer(m_ppCustomColliders[ii], delta, lowerBound, upperBound);
	RelocatePointer(m_pOwner, delta, lowerBound, upperBound);
#if !FINAL_BUILD || ALLOW_ROPE_DEBUGGER_IN_FINAL
	if (m_pDebugger)
		m_pDebugger->Relocate(delta, lowerBound, upperBound);
#endif
}

void Rope2::InitRopeDebugger(bool onlyIfFree)
{
#if !FINAL_BUILD || ALLOW_ROPE_DEBUGGER_IN_FINAL
	if (Memory::IsDebugMemoryAvailable() && s_enableRopeDebugger)
	{
		if (!s_pRopeDebugger)
		{
			s_pRopeDebugger = NDI_NEW(kAllocDebug) Rope2Debugger;
			if (!s_pRopeDebugger->Init(this))
			{
				delete s_pRopeDebugger;
				return;
			}
		}
		else if (s_pRopeDebugger->GetRope() && s_pRopeDebugger->GetRope() != this)
		{
			if (onlyIfFree)
			{
				return;
			}
			s_pRopeDebugger->GetRope()->m_pDebugger = nullptr;
		}
		SetDebugger(s_pRopeDebugger);
	}
#endif
}

//static
Rope2Debugger* Rope2::GetCommonDebugger()
{
#if !FINAL_BUILD || ALLOW_ROPE_DEBUGGER_IN_FINAL
	return s_pRopeDebugger;
#else
	return nullptr;
#endif
}

Point Rope2::GetPos(F32 ropeDist) const
{
	CheckStepNotRunning();

	U32F ii = 1;
	while (ii < m_numPoints && ropeDist >= m_pRopeDist[ii])
		ii++;
	if (ii >= m_numPoints)
	{
		ALWAYS_ASSERT(ropeDist < m_pRopeDist[m_numPoints-1] + 0.01f); // given ropeDist is beyond the length of the rope
		return m_pPos[m_numPoints-1];
	}
	Point result = Lerp(m_pPos[ii-1], m_pPos[ii], (ropeDist-m_pRopeDist[ii-1])/(m_pRopeDist[ii]-m_pRopeDist[ii-1]));
	PHYSICS_ASSERT(IsFinite(result));
	return result;
}

void Rope2::GetPosVelAndLast(F32 ropeDist, Point& pos, Point& lastPos, Vector& vel, Vector& lastVel) const
{
	CheckStepNotRunning();

	U32F ii = 1;
	while (ii < m_numPoints && ropeDist >= m_pRopeDist[ii])
		ii++;
	if (ii >= m_numPoints)
	{
		ALWAYS_ASSERT(ropeDist < m_pRopeDist[m_numPoints-1] + 0.01f); // given ropeDist is beyond the length of the rope
		pos = m_pPos[m_numPoints-1];
		lastPos = m_pLastPos[m_numPoints-1];
		vel = m_pVel[m_numPoints-1];
		lastVel = m_pLastVel[m_numPoints-1];
	}
	else
	{
		Scalar t((ropeDist-m_pRopeDist[ii-1])/(m_pRopeDist[ii]-m_pRopeDist[ii-1]));
		pos = Lerp(m_pPos[ii-1], m_pPos[ii], t);
		lastPos = Lerp(m_pLastPos[ii-1], m_pLastPos[ii], t);
		vel = Lerp(m_pVel[ii-1], m_pVel[ii], t);
		lastVel = Lerp(m_pLastVel[ii-1], m_pLastVel[ii], t);
	}
	PHYSICS_ASSERT(IsFinite(pos));
	PHYSICS_ASSERT(IsFinite(lastPos));
	PHYSICS_ASSERT(IsFinite(vel));
	PHYSICS_ASSERT(IsFinite(lastVel));
}

void Rope2::GetPosAndVel(F32 ropeDist, Point& pos, Vector& vel) const
{
	CheckStepNotRunning();

	U32F ii = 1;
	while (ii < m_numPoints && ropeDist >= m_pRopeDist[ii])
		ii++;
	if (ii >= m_numPoints)
	{
		ALWAYS_ASSERT(ropeDist < m_pRopeDist[m_numPoints-1] + 0.01f); // given ropeDist is beyond the length of the rope
		pos = m_pPos[m_numPoints-1];
		vel = m_pVel[m_numPoints-1];
	}
	else
	{
		Scalar t((ropeDist-m_pRopeDist[ii-1])/(m_pRopeDist[ii]-m_pRopeDist[ii-1]));
		pos = Lerp(m_pPos[ii-1], m_pPos[ii], t);
		vel = Lerp(m_pVel[ii-1], m_pVel[ii], t);
	}
	PHYSICS_ASSERT(IsFinite(pos));
	PHYSICS_ASSERT(IsFinite(vel));
}

void Rope2::GetDiscretizationUnstretchData(F32 ropeDistBase, Point& posBase, Vector& velBase, I32& iFirstNodeIndex, F32& maxUnstretchRopeDist) const
{
	U32F ii = 1;
	while (ii < m_numPoints && ropeDistBase >= m_pRopeDist[ii])
		ii++;
	if (ii >= m_numPoints)
	{
		iFirstNodeIndex = -1;
		return;
	}

	Scalar t((ropeDistBase-m_pRopeDist[ii-1])/(m_pRopeDist[ii]-m_pRopeDist[ii-1]));
	posBase = Lerp(m_pPos[ii-1], m_pPos[ii], t);
	velBase = Lerp(m_pVel[ii-1], m_pVel[ii], t);
	iFirstNodeIndex = ii;
	PHYSICS_ASSERT(IsFinite(posBase));
	PHYSICS_ASSERT(IsFinite(velBase));

	maxUnstretchRopeDist = 0.0f;
	ii = iFirstNodeIndex;
	F32 dist = ropeDistBase + Dist(posBase, m_pPos[ii]);
	while (1)
	{
		if (m_pNodeFlags[ii] & kNodeKeyframed)
		{
			maxUnstretchRopeDist = Max(maxUnstretchRopeDist, dist-m_pRopeDist[ii]);
		}
		ii++;
		if (ii == m_numPoints)
			break;
		dist += Dist(m_pPos[ii-1], m_pPos[ii]);
	}
	if (maxUnstretchRopeDist <= 0.0)
		iFirstNodeIndex = -1;
}

void Rope2::GetPosVelAndClosestConstraint(F32 ropeDist, Point& pos, Vector& vel, I32& conIndex) const
{
	U32F ii = 1;
	while (ii < m_numPoints && ropeDist >= m_pRopeDist[ii])
		ii++;
	if (ii >= m_numPoints)
	{
		ALWAYS_ASSERT(ropeDist < m_pRopeDist[m_numPoints-1] + 0.01f); // given ropeDist is beyond the length of the rope
		pos = m_pPos[m_numPoints-1];
		vel = m_pVel[m_numPoints-1];
		conIndex = IsNodeLoose(m_numPoints-1) ? m_numPoints-1 : -1;
	}
	else
	{
		Scalar t((ropeDist-m_pRopeDist[ii-1])/(m_pRopeDist[ii]-m_pRopeDist[ii-1]));
		pos = Lerp(m_pPos[ii-1], m_pPos[ii], t);
		vel = Lerp(m_pVel[ii-1], m_pVel[ii], t);
		conIndex = t < Scalar(0.5f) ? ii-1 : ii;
		conIndex = IsNodeLoose(conIndex) ? conIndex : -1;
	}
	PHYSICS_ASSERT(IsFinite(pos));
	PHYSICS_ASSERT(IsFinite(vel));
}

void Rope2::GetPosVelAndClosestConstraintWithUnstretch(F32 ropeDist, F32 baseRopeDist, const Point& basePos, const Vector& baseVel, U32 iFirstNode, F32 maxShift, Point& pos, Vector& vel, I32& conIndex) const
{
	F32 dist = baseRopeDist;
	F32 prevRopeDist = baseRopeDist;
	Point prevPos = basePos;
	Vector prevVel = baseVel;

	U32F ii = iFirstNode;
	while (1)
	{
		if (ii >= m_numPoints)
		{
			ALWAYS_ASSERT(ropeDist < m_pRopeDist[m_numPoints-1] + 0.01f); // given ropeDist is beyond the length of the rope
			pos = m_pPos[m_numPoints-1];
			vel = m_pVel[m_numPoints-1];
			conIndex = IsNodeLoose(m_numPoints-1) ? m_numPoints-1 : -1;
			break;
		}

		F32 distNode = Dist(prevPos, m_pPos[ii]);
		dist += distNode;
		if (ropeDist < m_pRopeDist[ii] || (ropeDist < dist && ropeDist-maxShift < m_pRopeDist[ii]))
		{
			F32 tRopeDist = 1.0f;
			F32 tRopeDistShift = 0.0f;
			F32 dRopeDist = m_pRopeDist[ii]-prevRopeDist;
			if (dRopeDist > 0.0f)
			{
				tRopeDist = (ropeDist-prevRopeDist)/dRopeDist;
				tRopeDistShift = (ropeDist-maxShift-prevRopeDist)/dRopeDist;
			}
			F32 tDist;
			if (distNode > 0.0f)
			{
				tDist = (ropeDist - (dist - distNode)) / distNode;
			}
			else
			{
				tDist = ropeDist < dist ? 0.0f : 1.0f;
			}
			// If t based on 3d distance is smaller than t based on rope dist and within maxShift -> use it
			F32 t = Min(tRopeDist, Max(tDist, tRopeDistShift));

			pos = Lerp(prevPos, m_pPos[ii], t);
			vel = Lerp(prevVel, m_pVel[ii], t);
			conIndex = t < Scalar(0.5f) ? ii-1 : ii;
			conIndex = IsNodeLoose(conIndex) ? conIndex : -1;
			break;
		}

		prevRopeDist = m_pRopeDist[ii];
		prevPos = m_pPos[ii];
		prevVel = m_pVel[ii];
		ii++;
	}
	PHYSICS_ASSERT(IsFinite(pos));
	PHYSICS_ASSERT(IsFinite(vel));
}

void Rope2::GetPosAndDir(F32 ropeDist, Point& posOut, Vector& dirOut, Vector_arg dirFallback) const
{
	CheckStepNotRunning();

	U32F ii = 1;
	while (ii < m_numPoints && ropeDist >= m_pRopeDist[ii])
		ii++;
	if (ii >= m_numPoints)
	{
		ALWAYS_ASSERT(ropeDist < m_pRopeDist[m_numPoints-1] + 0.01f); // given ropeDist is beyond the length of the rope
		posOut = m_pPos[m_numPoints-1];
		dirOut = SafeNormalize(posOut - m_pPos[m_numPoints-2], dirFallback);
	}
	else
	{
		F32 d = m_pRopeDist[ii] - m_pRopeDist[ii-1];
		F32 f = (ropeDist-m_pRopeDist[ii-1])/d;
		posOut = Lerp(m_pPos[ii-1], m_pPos[ii], f);

		// Use 2nd order difference to calc the tangent direction
		Vector dir = SafeNormalize(m_pPos[ii] - m_pPos[ii-1], dirFallback);
		if (f > 0.5f)
		{
			if (ii < m_numPoints-1)
			{
				Vector dirNext = SafeNormalize(m_pPos[ii+1] - m_pPos[ii], dir);
				//F32 dNext = m_pRopeDist[ii+1] - m_pRopeDist[ii];
				//F32 fn = (f - 0.5f) * d / (0.5f * (d + dNext));
				dirOut = Lerp(dir, dirNext, f - 0.5f);
			}
			else
			{
				dirOut = dir;
			}
		}
		else
		{
			if (ii > 1)
			{
				Vector dirPrev = SafeNormalize(m_pPos[ii-1] - m_pPos[ii-2], dir);
				//F32 dPrev = m_pRopeDist[ii-1] - m_pRopeDist[ii-2];
				//F32 fn = (0.5f - f) * d / (0.5f * (d + dPrev));
				dirOut = Lerp(dirPrev, dir, f + 0.5f);
			}
			else
			{
				dirOut = dir;
			}
		}
		//dirOut = dir; // old way, 1st order
	}
	PHYSICS_ASSERT(IsFinite(posOut));
	PHYSICS_ASSERT(IsFinite(dirOut));
}

void Rope2::GetPosAndDirSmooth(F32 ropeDist, Point& posOut, Vector& dirOut, Vector_arg dirFallback) const
{
	CheckStepNotRunning();

	U32F ii = 1;
	while (ii < m_numPoints && ropeDist >= m_pRopeDist[ii])
		ii++;
	if (ii >= m_numPoints)
	{
		ALWAYS_ASSERT(ropeDist < m_pRopeDist[m_numPoints-1] + 0.01f); // given ropeDist is beyond the length of the rope
		ii = m_numPoints-1;
		ropeDist = m_pRopeDist[ii];
	}

	F32 s0 = m_pRopeDist[ii-1];
	F32 s1 = m_pRopeDist[ii];
	F32 d = s1 - s0;
	F32 f = (ropeDist-m_pRopeDist[ii-1])/d;

	Point p0 = m_pPos[ii-1];
	Point p1 = m_pPos[ii];

	Vector t0, t1;

#if CATMULLROM_POINTS

	if (ii-1 > 0)
	{
		t0 = m_pPos[ii] - m_pPos[ii-2];
		t0 /= Max(m_pRopeDist[ii] - m_pRopeDist[ii-2], (F32)Length(t0));
	}
	if (ii < m_numPoints-1)
	{
		t1 = m_pPos[ii+1] - m_pPos[ii-1];
		t1 /= Max(m_pRopeDist[ii+1] - m_pRopeDist[ii-1], (F32)Length(t1));
	}

	if (ii-1 == 0)
	{
		if (ii == m_numPoints-1)
		{
			t0 = p1-p0;
			t0 /= Max(d, (F32)Length(t0));
			t1 = t0;
		}
		else
		{
			Vector dir = SafeNormalize(p1-p0, kZero);
			Vector dev1 = t1 - dir * Dot(dir, t1);
			t0 = t1 - 2.0f * dev1;
		}
	}
	if (ii == m_numPoints-1)
	{
		Vector dir = SafeNormalize(p1-p0, kZero);
		Vector dev0 = t0 - dir * Dot(dir, t0);
		t1 = t0 - 2.0f * dev0;
	}

#else

	if ((f <= 0.5f && (ii-1 == 0 || (m_pNodeFlags[ii-1] & kNodeKeyframed))) ||
		(f > 0.5f && (ii == m_numPoints-1 || (m_pNodeFlags[ii] & kNodeKeyframed))) ||
		(m_pNodeFlags[ii] & kNodeStrained))
	{
		posOut = Lerp(p0, p1, f);
		dirOut = SafeNormalize(p1-p0, dirFallback);
		PHYSICS_ASSERT(IsFinite(posOut));
		PHYSICS_ASSERT(IsFinite(dirOut));
		return;
	}
	else
	{
		if (f <= 0.5f)
		{
			p0 = kOrigin + 0.5f * ((m_pPos[ii-2]-kOrigin) + (m_pPos[ii-1]-kOrigin));
			t0 = m_pPos[ii-1] - m_pPos[ii-2];
			//t0 /= Max(m_pRopeDist[ii-1] - m_pRopeDist[ii-2], (F32)Length(t0));
			//t0 = SafeNormalize(m_pPos[ii-1] - m_pPos[ii-2], kZero);
			p1 = kOrigin + 0.5f * ((m_pPos[ii-1]-kOrigin) + (m_pPos[ii]-kOrigin));
			t1 = m_pPos[ii] - m_pPos[ii-1];
			//t1 /= Max(m_pRopeDist[ii] - m_pRopeDist[ii-1], (F32)Length(t1));
			//t1 = SafeNormalize(m_pPos[ii] - m_pPos[ii-1], kZero);
			f = 0.5f + f;
		}
		else
		{
			p0 = kOrigin + 0.5f * ((m_pPos[ii-1]-kOrigin) + (m_pPos[ii]-kOrigin));
			t0 = m_pPos[ii] - m_pPos[ii-1];
			//t0 /= Max(m_pRopeDist[ii] - m_pRopeDist[ii-1], (F32)Length(t0));
			//t0 = SafeNormalize(m_pPos[ii] - m_pPos[ii-1], kZero);
			p1 = kOrigin + 0.5f * ((m_pPos[ii]-kOrigin) + (m_pPos[ii+1]-kOrigin));
			t1 = m_pPos[ii+1] - m_pPos[ii];
			//t1 /= Max(m_pRopeDist[ii+1] - m_pRopeDist[ii], (F32)Length(t1));
			//t1 = SafeNormalize(m_pPos[ii+1] - m_pPos[ii], kZero);
			f = f - 0.5f;
		}
	}
#endif

	//t0 *= 0.5f;
	//t1 *= 0.5f;

	F32 f2 = f*f;
	F32 f3 = f2*f;

	F32 h00 = 2.0f*f3 - 3.0f*f2 + 1.0f;
	F32 h10 = f3 - 2.0f*f2 + f;
	F32 h01 = -2.0f*f3 + 3.0f*f2;
	F32 h11 = f3 - f2;

	posOut = kOrigin + h00 * (p0-kOrigin) + h10 * t0 + h01 * (p1-kOrigin) + h11 * t1;

	// Derivative
	F32 h00d = 6.0f*f2 - 6.0f*f;
	F32 h10d = 3.0f*f2 - 4.0f*f + 1.0f;
	F32 h01d = -6.0f*f2 + 6.0f*f;
	F32 h11d = 3.0f*f2 - 2.0f*f;

	dirOut = h00d * (p0-kOrigin) + h10d * t0 + h01d * (p1-kOrigin) + h11d * t1;
	dirOut = SafeNormalize(dirOut, dirFallback);

	PHYSICS_ASSERT(IsFinite(posOut));
	PHYSICS_ASSERT(IsFinite(dirOut));
}

Vector Rope2::GetVel(F32 ropeDist) const
{
	CheckStepNotRunning();

	U32F ii = 1;
	while (ii < m_numPoints && ropeDist >= m_pRopeDist[ii])
		ii++;
	if (ii >= m_numPoints)
	{
		ALWAYS_ASSERT(ropeDist < m_pRopeDist[m_numPoints-1] + 0.01f); // given ropeDist is beyond the length of the rope
		return m_pVel[m_numPoints-1];
	}
	Vector result = Lerp(m_pVel[ii-1], m_pVel[ii], (ropeDist-m_pRopeDist[ii-1])/(m_pRopeDist[ii]-m_pRopeDist[ii-1]));
	PHYSICS_ASSERT(IsFinite(result));
	return result;
}

Vector Rope2::GetKeyVel(F32 ropeDist) const
{
	CheckStepNotRunning();

	U32F ii = 1;
	while (ii < m_numKeyPoints && ropeDist >= m_pKeyRopeDist[ii])
		ii++;
	if (ii >= m_numKeyPoints)
	{
		return m_pKeyVel[m_numKeyPoints-1];
	}
	Vector result = Lerp(m_pKeyVel[ii-1], m_pKeyVel[ii], (ropeDist-m_pKeyRopeDist[ii-1])/(m_pKeyRopeDist[ii]-m_pKeyRopeDist[ii-1]));
	PHYSICS_ASSERT(IsFinite(result));
	return result;
}

F32 Rope2::GetInvRelMass(F32 ropeDist) const
{
	U32F ii = 1;
	while (ii < m_numPoints && ropeDist >= m_pRopeDist[ii])
		ii++;
	if (ii >= m_numPoints)
	{
		PHYSICS_ASSERT(ropeDist < m_pRopeDist[m_numPoints-1] + 0.01f); // given ropeDist is beyond the length of the rope
		return m_pInvRelMass[m_numPoints-1];
	}
	F32 t = (ropeDist-m_pRopeDist[ii-1])/(m_pRopeDist[ii]-m_pRopeDist[ii-1]);
	//if (t == 0.0f)
	//{
	//	return m_pInvRelMass[ii-1]
	//}
	//else if (t == 1.0f)
	//{
	//	return m_pInvRelMass[ii]
	//}
	//else if (m_pInvRelMass[ii-1] == 0.0f)
	//{
	//	return m_pInvRelMass[ii];
	//}
	//else if (m_pInvRelMass[ii] == 0.0f)
	//{
	//	return m_pInvRelMass[ii-1];
	//}
	F32 res = Lerp(m_pInvRelMass[ii-1], m_pInvRelMass[ii], t);
	PHYSICS_ASSERT(IsFinite(res));
	return res;
}

F32 Rope2::GetTensionFriction(F32 ropeDist) const
{
	U32F ii = 1;
	while (ii < m_numPoints && ropeDist >= m_pRopeDist[ii])
		ii++;
	if (ii >= m_numPoints)
	{
		ALWAYS_ASSERT(ropeDist < m_pRopeDist[m_numPoints-1] + 0.01f); // given ropeDist is beyond the length of the rope
		return m_pTensionFriction[m_numPoints-1];
	}
	F32 t = (ropeDist-m_pRopeDist[ii-1])/(m_pRopeDist[ii]-m_pRopeDist[ii-1]);
	F32 res = Lerp(m_pTensionFriction[ii-1], m_pTensionFriction[ii], t);
	PHYSICS_ASSERT(IsFinite(res));
	return res;
}

void Rope2::Teleport(const Locator& loc)
{
	CheckStepNotRunning();

	Scalar dt = HavokGetDeltaTime();

	for (U32F ii = 0; ii<m_numPoints; ii++)
	{
		// We assume velocity stays the same (in some local space)
		m_pVel[ii] = loc.TransformVector(m_pVel[ii]);
		m_pLastVel[ii] = loc.TransformVector(m_pLastVel[ii]);

		// Now teleport positions and backstep so that in this frame step we will aim at the teleport target position
		Vector dpos = m_pVel[ii] * dt;
		Locator deltaLocWithBackstep(loc.GetTranslation() - dpos, loc.GetRotation());
		m_pPos[ii] = deltaLocWithBackstep.TransformPoint(m_pPos[ii]);
		m_pLastPos[ii] = m_pPos[ii] - dpos;
		//g_prim.Draw(DebugCross(m_pPos[ii], 0.03f, 0.1f, kColorGreen), Seconds(1.0f));

		// We also need to teleport constraints because we use them in PreCollision
		// @@JS: this is not completely correct. We should teleport each constraint in respect
		// to it's collider delta loc and also use its angular velocity for the backstep
		// But ... this should be good enough for our practical cases
		Constraint& con = m_pConstr[ii];
		for(U32F iPlane = 0; iPlane < con.m_numPlanes; ++iPlane)
		{
			Vector norm = Vector(con.m_planes[iPlane]);
			Point p = Point(kZero) - con.m_planes[iPlane].W() * norm;
			norm = deltaLocWithBackstep.TransformVector(norm);
			p = deltaLocWithBackstep.TransformPoint(p);
			Scalar D = -Dot(p-Point(kZero), norm);
			con.m_planes[iPlane] = Vec4(norm.X(), norm.Y(), norm.Z(), D);
		}
		for(U32F iBiPlane = 0; iBiPlane < con.m_numEdges*2; ++iBiPlane)
		{
			Vector norm = Vector(con.m_biPlanes[iBiPlane]);
			Point p = Point(kZero) - con.m_biPlanes[iBiPlane].W() * norm;
			norm = deltaLocWithBackstep.TransformVector(norm);
			p = deltaLocWithBackstep.TransformPoint(p);
			Scalar D = -Dot(p-Point(kZero), norm);
			con.m_biPlanes[iBiPlane] = Vec4(norm.X(), norm.Y(), norm.Z(), D);
		}
	}

	m_bTeleport = true;
}

void Rope2::ResetDynamics()
{
	m_bResetDynamics = true;
}

void Rope2::ReelTeleport(F32 reelDist)
{
	CheckStepNotRunning();

	for (U32F ii = 0; ii<m_numPoints; ii++)
	{
		F32 newDist = MinMax(m_pRopeDist[ii] + reelDist, 0.0f, m_fLength);
		GetPosVelAndLast(newDist, m_pPos[ii], m_pLastPos[ii], m_pVel[ii], m_pLastVel[ii]);
	}
}

void Rope2::SetKeyframedPosWithFlags(F32 ropeDist, const Point& pos, const Vector& vel, RopeNodeFlags flags)
{
	CheckStepNotRunning();
	PHYSICS_ASSERT(IsFinite(ropeDist) && ropeDist >= 0.f && ropeDist <= m_fLength);
	PHYSICS_ASSERT(IsFinite(pos) && Length(pos - kOrigin) < 100000.0f);
	PHYSICS_ASSERT(IsFinite(vel));
#if NET_CHAR_TEST
	if (m_bNeverStrained && (flags&kNodeStrained))
	{
		flags &= ~kNodeStrained;
		flags |= kNodeKeyframedSeg;
	}
#else
	PHYSICS_ASSERT(!m_bNeverStrained || (flags&kNodeStrained) == 0);
#endif

	PHYSICS_ASSERT(!(flags & kNodeStrained) || (flags & kNodeUserMarks)); // strained nodes now always need user mark
	U32F ii = 0;
	while (ii < m_numKeyPoints && ropeDist > m_pKeyRopeDist[ii]+kRopeDistKeyEps)
		ii++;
	if (ii >= m_numKeyPoints)
	{
		ALWAYS_ASSERT(m_numKeyPoints < m_maxNumKeyPoints);
		m_numKeyPoints++;
	}
	else if (Abs(ropeDist - m_pKeyRopeDist[ii]) > kRopeDistKeyEps)
	{
		ALWAYS_ASSERT(m_numKeyPoints < m_maxNumKeyPoints);
		memmove(m_pKeyRopeDist+ii+1, m_pKeyRopeDist+ii, (m_numKeyPoints-ii)*sizeof(m_pKeyRopeDist[0]));
		memmove(m_pKeyPos+ii+1, m_pKeyPos+ii, (m_numKeyPoints-ii)*sizeof(m_pKeyPos[0]));
		memmove(m_pKeyVel+ii+1, m_pKeyVel+ii, (m_numKeyPoints-ii)*sizeof(m_pKeyVel[0]));
		memmove(m_pKeyNodeFlags+ii+1, m_pKeyNodeFlags+ii, (m_numKeyPoints-ii)*sizeof(m_pKeyNodeFlags[0]));
		m_numKeyPoints++;
	}
	else
	{
		// We are basically overriding previous keyframed point because the new one is too close in ropeDist
		// This will need some better handling to be more user friendly
		if (ropeDist > m_pKeyRopeDist[ii])
		{
			// Ignore if this comes after
			return;
		}
	}

	m_pKeyRopeDist[ii] = ropeDist;
	m_pKeyPos[ii] = pos;
	m_pKeyVel[ii] = vel;
	m_pKeyNodeFlags[ii] = flags|kNodeKeyframed;
	m_pKeyMoveDist[ii] = Dist(pos, GetPos(ropeDist));
}

void Rope2::SetKeyframedPos(F32 ropeDist, const Point& pos, const Vector& vel, RopeNodeFlags flags)
{
	SetKeyframedPosWithFlags(ropeDist, pos, vel, flags|kNodeKeyedVelocity);
}

void Rope2::SetKeyframedPos(F32 ropeDist, const Point& pos, RopeNodeFlags flags)
{
	SetKeyframedPosWithFlags(ropeDist, pos, Vector(kZero), flags);
}

void Rope2::SetKeyRadius(F32 ropeDist, F32 radius)
{
	CheckStepNotRunning();
	PHYSICS_ASSERT(IsFinite(ropeDist) && IsFinite(radius) && ropeDist >= 0.f && ropeDist <= m_fLength);
	U32F ii = 0;
	while (ii < m_numKeyRadius && ropeDist > m_pKeyRadius[ii].m_ropeDist+kRopeDistKeyEps)
		ii++;
	if (ii >= m_numKeyRadius)
	{
		ALWAYS_ASSERT(m_numKeyRadius < m_maxNumKeyPoints);
		m_numKeyRadius++;
	}
	else if (Abs(ropeDist - m_pKeyRadius[ii].m_ropeDist) > kRopeDistKeyEps)
	{
		ALWAYS_ASSERT(m_numKeyRadius < m_maxNumKeyPoints);
		memmove(m_pKeyRadius+ii+1, m_pKeyRadius+ii, (m_numKeyRadius-ii)*sizeof(m_pKeyRadius[0]));
		m_numKeyRadius++;
	}
	else
	{
		// We are basically overriding previous keyframed point because the new one is too close in ropeDist
		// This will need some better handling to be more user friendly
		if (ropeDist > m_pKeyRadius[ii].m_ropeDist)
		{
			// Ignore if this comes after
			return;
		}
	}

	m_pKeyRadius[ii].m_ropeDist = ropeDist;
	m_pKeyRadius[ii].m_radius = radius;
}

void Rope2::SetKeyTwistDir(F32 ropeDist, const Vector& dir)
{
	PHYSICS_ASSERT(m_pKeyTwistDir);

	for (U32 ii = 0; ii<m_numKeyPoints; ii++)
	{
		if (Abs(ropeDist - m_pKeyRopeDist[ii]) < kRopeDistKeyEps)
		{
			m_pKeyTwistDir[ii] = dir;
			m_pKeyNodeFlags[ii] |= kNodeUserTwist;
			return;
		}
	}

	PHYSICS_ASSERTF(false, ("Trying to se twist dir at ropeDist that have not been keyframed"));
}

void Rope2::SetSaveStartEdgesPos(Point_arg pos)
{
	CheckStepNotRunning();
	m_saveStartEdgePos = pos;
	m_saveStartEdgePosSet = true;
}

void Rope2::SetSaveEdgesPos(Point_arg pos)
{
	CheckStepNotRunning();
	m_saveEdgePos = pos;
	m_saveEdgePosSet = true;
	m_bSleeping = false;
}

void Rope2::SetSaveEdgesPosRopeDist(F32 ropeDist)
{
	CheckStepNotRunning();
	m_savePosRopeDist = ropeDist;
	m_savePosRopeDistSet = true;
}

void Rope2::SetNumSaveEdges(U32F numEdges)
{
	CheckStepNotRunning();
	PHYSICS_ASSERT(m_pSaveEdges && numEdges < m_maxNumPoints);
	m_numSaveEdges = numEdges;
}

void Rope2::SetSaveEdge(U32F ii, const EdgePoint& ePoint)
{
	CheckStepNotRunning();
	PHYSICS_ASSERT(m_pSaveEdges && ii < m_numSaveEdges);
	m_pSaveEdges[ii] = ePoint;
}

void Rope2::SetNumExternSaveEdges(U32F numEdges)
{
	CheckStepNotRunning();
	PHYSICS_ASSERT(m_pExternSaveEdges && numEdges < m_maxNumPoints);
	m_numExternSaveEdges = numEdges;
}

void Rope2::SetExternSaveEdge(U32F ii, const EdgePoint& ePoint)
{
	CheckStepNotRunning();
	PHYSICS_ASSERT(m_pExternSaveEdges && ii < m_numExternSaveEdges);
	m_pExternSaveEdges[ii] = ePoint;
}

Point Rope2::GetStraightPos(F32 ropeDist) const
{
	CheckStepNotRunning();

	U32F ii = 1;
	while (ii < m_numEdges && ropeDist > m_pEdges[ii].m_ropeDist)
		ii++;
	if (ii >= m_numEdges)
	{
		// We're out straight rope data, just use normal rope
		// This is the case for example if the end of the rope is not keyframed and we grab on it
		// @@JS: We should probably maintain straight version of rope for the free end of rope too
		return GetPos(ropeDist);
	}
	Point result = Lerp(m_pEdges[ii-1].m_pos, m_pEdges[ii].m_pos, (ropeDist-m_pEdges[ii-1].m_ropeDist)/(m_pEdges[ii].m_ropeDist-m_pEdges[ii-1].m_ropeDist));
	PHYSICS_ASSERT(IsFinite(result));
	return result;
}

F32 Rope2::GetStraightDist(F32 ropeDistFrom, F32 ropeDistTo)
{
	return GetStraightDist(ropeDistFrom, GetPos(ropeDistFrom), ropeDistTo, GetPos(ropeDistTo));
}

F32 Rope2::GetStraightDist(F32 ropeDistFrom, const Point& from, F32 ropeDistTo, const Point& to)
{
	CheckStepNotRunning();

	Point lastPos = from;
	F32 dist = 0.0f;
	U32F ii = 0;
	while (ii < m_numEdges && m_pEdges[ii].m_ropeDist < ropeDistFrom)
		ii++;
	while (ii < m_numEdges && m_pEdges[ii].m_ropeDist < ropeDistTo)
	{
		dist += Dist(lastPos, m_pEdges[ii].m_pos);
		lastPos = m_pEdges[ii].m_pos;
		ii++;
	}
	dist += Dist(lastPos, to);

	return dist;
}

void Rope2::PreStep()
{
	CheckStepNotRunning();

	if (!HavokIsEnabled() || g_ropeMgr.m_disableStep)
		return;

	if (m_useGpu && g_ropeUseGpu)
	{
		U32F maxNumPoints = m_bSimpleRope ? m_numPoints : m_maxNumPoints;
		if (maxNumPoints >= 32)
		{
			// We need to allocate all the GameToGpuRing buffers now because Step can run parallel while we start rendering and swap that ring
			AllocComputeEarly(maxNumPoints);
		}
	}
}

void Rope2::Step()
{
	CheckStepNotRunning();

	if (!HavokIsEnabled() || g_ropeMgr.m_disableStep)
	{
		StepCleanup();
		return;
	}

	m_scStepTime = Scalar(HavokGetDeltaTime());
	if (m_scStepTime == Scalar(kZero))
	{
		StepCleanup();
		return;
	}
	m_scInvStepTime = 1.0f/m_scStepTime;

#if !FINAL_BUILD || ALLOW_ROPE_DEBUGGER_IN_FINAL
	if (m_pDebugger)
		m_pDebugger->Save();
#endif

	m_stepJobId = ndjob::GetActiveJobId();

	if (m_bSimpleRope)
	{
		StepInnerSimple();
	}
	else
	{
		StepInner();
	}

	StepCleanup();

	m_stepJobId = -1;

	m_saveEdgePosSet = false;
	m_savePosRopeDistSet = false;
	m_saveStartEdgePosSet = false;
	m_numExternSaveEdges = 0;
	m_bTeleport = false;
}

void Rope2::StepCleanup()
{
	ComputeCleanup();
}

void Rope2::UpdatePaused()
{
	CheckStepNotRunning();

#if !FINAL_BUILD || ALLOW_ROPE_DEBUGGER_IN_FINAL
	if (m_pDebugger)
		m_pDebugger->UpdatePaused();
#endif
}

void Rope2::SetSimple(bool b)
{
	CheckStepNotRunning();

	if (m_bSimpleRope != b)
	{
		m_bSimpleRope = b;
		if (m_bSimpleRope)
		{
			for (U32F ii = 0; ii<m_numKeyPoints; ii++)
			{
				m_pEdges[ii].Reset();
				m_pEdges[ii].m_pos = m_pKeyPos[ii];
				m_pEdges[ii].m_ropeDist = m_pKeyRopeDist[ii];
				m_pEdges[ii].m_flags = m_pKeyNodeFlags[ii];
			}
			m_numEdges = m_numKeyPoints;

			Discretize(false);

			if (!m_bAllowKeyStretchConstraints)
			{
				PHYSICS_ASSERT(m_pPrevKeyIndex && m_pNextKeyIndex);
				PrepareClosestKeyData();
			}

			m_bPostSolve = false;
		}
	}
}

void Rope2::StepInnerSimple()
{
	PROFILE(Rope, RopeStepSimple);

	m_lastStraightenSubsteps = 0;

	if (g_ropeMgr.m_neverSleep && m_bSleeping)
	{
		WakeUp();
	}

	if (!m_bSleeping)
	{
		memcpy(m_pLastPos, m_pPos, m_numPoints*sizeof(m_pPos[0]));
		memcpy(m_pLastVel, m_pVel, m_numPoints*sizeof(m_pVel[0]));

		PHYSICS_ASSERT(m_numKeyPoints == m_numEdges);
		U32F iNode = 0;
		for (U32F ii = 0; ii<m_numKeyPoints; ii++)
		{
			PHYSICS_ASSERT(m_pEdges[ii].m_ropeDist == m_pKeyRopeDist[ii]);
			PHYSICS_ASSERT(m_pEdges[ii].m_flags == m_pKeyNodeFlags[ii]);
			m_pEdges[ii].m_pos = m_pKeyPos[ii];
			while (m_pRopeDist[iNode] != m_pKeyRopeDist[ii])
			{
				iNode++;
				PHYSICS_ASSERT(iNode < m_numPoints);
			}
			m_pVel[iNode] = (m_pKeyNodeFlags[ii] & kNodeKeyedVelocity) ? m_pKeyVel[ii] : (m_pKeyPos[ii] - m_pPos[iNode]) * m_scInvStepTime;
			m_pPos[iNode] = m_pKeyPos[ii];
			PHYSICS_ASSERT(IsFinite(m_pVel[iNode]));
			PHYSICS_ASSERT(m_pNodeFlags[iNode] == m_pKeyNodeFlags[ii]);
		}

		TimestepVelocities();
		TimestepPositions();
		UpdateAabb();
		CreateColCache();
		Collide();
		SolverStep();
		CheckSleeping();
	}
	else
	{
		TimestepVelocities();
		TimestepPositions();

		if (!ValidateColCache())
		{
			m_bSleeping = false;
			CreateColCache();
		}

		Collide();
		CheckSleeping();
		if (!m_bSleeping)
		{
			memcpy(m_pLastPos, m_pPos, m_numPoints*sizeof(m_pPos[0]));
			memcpy(m_pLastVel, m_pVel, m_numPoints*sizeof(m_pVel[0]));

			PHYSICS_ASSERT(m_numKeyPoints == m_numEdges);
			U32F iNode = 0;
			for (U32F ii = 0; ii<m_numKeyPoints; ii++)
			{
				PHYSICS_ASSERT(m_pEdges[ii].m_ropeDist == m_pKeyRopeDist[ii]);
				PHYSICS_ASSERT(m_pEdges[ii].m_flags == m_pKeyNodeFlags[ii]);
				m_pEdges[ii].m_pos = m_pKeyPos[ii];
				while (m_pRopeDist[iNode] != m_pKeyRopeDist[ii])
				{
					iNode++;
					PHYSICS_ASSERT(iNode < m_numPoints);
				}
				m_pVel[iNode] = (m_pKeyNodeFlags[ii] & kNodeKeyedVelocity) ? m_pKeyVel[ii] : (m_pKeyPos[ii] - m_pPos[iNode]) * m_scInvStepTime;
				m_pPos[iNode] = m_pKeyPos[ii];
				PHYSICS_ASSERT(IsFinite(m_pVel[iNode]));
				PHYSICS_ASSERT(m_pNodeFlags[iNode] == m_pKeyNodeFlags[ii]);
			}

			SolverStep();
		}
		else
		{
			memset(m_pVel, 0, m_numPoints*sizeof(m_pVel[0]));
			memcpy(m_pPos, m_pLastPos, m_numPoints*sizeof(m_pPos[0]));
		}
	}

	if (m_bResetDynamics)
	{
		for (U32F ii = 0; ii<m_numPoints; ii++)
		{
			m_pVel[ii] = kZero;
		}
		m_bResetDynamics = false;
	}

	if (m_colCache.m_overflow && m_bCritical)
	{
		JAROS_ALWAYS_ASSERT(false);
		s_colCacheOverflow = true;
	}
}

void Rope2::StepInner()
{
	PROFILE(Rope, Rope_Step);

	m_lastStraightenSubsteps = 0;

	if (g_ropeMgr.m_neverSleep && m_bSleeping)
	{
		WakeUp();
	}

	if (!m_bSleeping)
	{
		UpdateAabb();
		CreateColCache();
		StrainedEdgeDetection();

		Discretize(true);

		CreateDistanceConstraintsFromStrainedEdges();
		CreateFrictionConstraints();

		TimestepVelocities();
		TimestepPositions();
		Collide();
		SelfCollision();

		SolverStep();
	}
	else
	{
		if (!ValidateColCache())
		{
			m_bSleeping = false;

			CreateColCache();
			StrainedEdgeDetection();

			Discretize(true);

			CreateDistanceConstraintsFromStrainedEdges();
			CreateFrictionConstraints();
		}

		TimestepVelocities();
		TimestepPositions();
		Collide();
		CheckSleeping();

		if (!m_bSleeping)
		{
			// We kept constraints working flags but since we woke up and we're going to solve, we reset them here
			for (U32 ii = m_firstDynamicPoint; ii<=m_lastDynamicPoint; ii++)
			{
				m_pConstr[ii].m_flags = 0;
			}

			SolverStep();
		}
		else
		{
			memset(m_pVel, 0, m_numPoints*sizeof(m_pVel[0]));
			memcpy(m_pPos, m_pLastPos, m_numPoints*sizeof(m_pPos[0]));
		}
	}

	if (m_bResetDynamics)
	{
		for (U32F ii = 0; ii<m_numPoints; ii++)
		{
			m_pVel[ii] = kZero;
		}
		m_bResetDynamics = false;
	}

	if (m_colCache.m_overflow && m_bCritical)
	{
		JAROS_ALWAYS_ASSERT(false);
		s_colCacheOverflow = true;
	}
}

void Rope2::PreSetKeyframedPos()
{
	memcpy(m_pLastPos, m_pPos, m_numPoints*sizeof(m_pPos[0]));
	memcpy(m_pLastVel, m_pVel, m_numPoints*sizeof(m_pVel[0]));
}

void Rope2::StepKeyframed()
{
	PROFILE(Rope, Rope_StepKeyframed);

	if (!HavokIsEnabled() || g_ropeMgr.m_disableStep)
		return;

	CheckStepNotRunning();

	m_scStepTime = Scalar(HavokGetDeltaTime());
	if (m_scStepTime == Scalar(kZero))
		return;
	m_scInvStepTime = 1.0f/m_scStepTime;

	memset(m_pConstr, 0, sizeof(Constraint)*m_numPoints);
	BackstepVelocities(0, m_numPoints-1, m_scStepTime);
	CheckSleeping();

	ResetNodeDebugInfo();

	m_bTeleport = false;
}

void Rope2::UpdateAabbEdges()
{
	Point ptMin = m_pEdges[0].m_pos;
	Point ptMax = m_pEdges[0].m_pos;

	for (U32F i = 1; i < m_numEdges; ++i)
	{
		Point ptPos = m_pEdges[i].m_pos;
		ptMin = Min(ptMin, ptPos);
		ptMax = Max(ptMax, ptPos);
	}

	CalcAabbFromPoints(m_aabb, ptMin, ptMax);

	m_aabb.Expand(m_fAabbExpansion); // @@ this should cover some extra space plus slowly moving objects for now. Plus some movement for slacky rope.
}

void Rope2::StepSimpleStrainedEdgeDetection(Point_arg startPos, Point_arg endPos)
{
	PROFILE(Rope, Rope_StepEdges);

	m_lastStraightenSubsteps = 0;

	if (m_strainedLayerMask != 0)
	{
		UpdateAabbEdges();
	}
	CreateColCache();
	StrainedEdgeDetection(m_pEdges, m_numEdges, 0, startPos, endPos, false);

	if (m_colCache.m_overflow && m_bCritical)
	{
		JAROS_ALWAYS_ASSERT(false);
		s_colCacheOverflow = true;
	}
}

void Rope2::CheckSleeping()
{
	if (m_bNeverSleeps)
	{
		m_bSleeping = false; // wake up
		m_numFramesCreep = 0;
		return;
	}

	// Check sleeping
	VF32 vConstraintHash = CalcConstraintHash();
	if (SMATH_VEC_ALL_COMPONENTS_EQ(vConstraintHash, m_vConstraintHash))
	{
		if (!m_bSleeping)
		{
			Scalar scMaxVel = GetMaxVel();
			if (scMaxVel < Scalar(kRopeMaxSleepVel))
			{
				m_numFramesCreep++;
				if (!g_ropeMgr.m_neverSleep
					&&  m_numFramesCreep >= kMinFrameCreep
					/*&&  m_numFramesCreep * (0.5f / kMaxFrameCreep) > scMaxVel*/)
				{
					GoToSleep();
				}
			}
			else
				m_numFramesCreep = 0;
		}
		else
		{
			// See if there is keyframed velocity that wakes us up
			Scalar scMaxVel = GetMaxKeyframedVel();
			if (scMaxVel > Scalar(kRopeMinWakeupVel))
			{
				m_bSleeping = false; // wake up
				m_numFramesCreep = 0;
			}
		}
	}
	else
	{
		m_bSleeping = false; // wake up
		m_numFramesCreep = 0;
		m_vConstraintHash = vConstraintHash;
	}
}

void RemoveNonKeyframedPoint(U32F& numPoints, Point* pPos, Vector* pVel, F32* pRopeDist, Rope2::RopeNodeFlags* pFlags, I32* pClosestCon, I32F& firstDynamicPoint, I32F& lastDynamicPoint)
{
	for (U32F ii = numPoints-1; ii>0; ii--)
	{
		if ((pFlags[ii] & Rope2::kNodeKeyframed) == 0)
		{
			memcpy(pPos+ii, pPos+ii+1, (numPoints-ii-1)*sizeof(pPos[0]));
			memcpy(pVel+ii, pVel+ii+1, (numPoints-ii-1)*sizeof(pVel[0]));
			memcpy(pRopeDist+ii, pRopeDist+ii+1, (numPoints-ii-1)*sizeof(pRopeDist[0]));
			memcpy(pFlags+ii, pFlags+ii+1, (numPoints-ii-1)*sizeof(pFlags[0]));
			memcpy(pClosestCon+ii, pClosestCon+ii+1, (numPoints-ii-1)*sizeof(pClosestCon[0]));
			if (ii < firstDynamicPoint)
				firstDynamicPoint--;
			if (ii < lastDynamicPoint)
				lastDynamicPoint--;
			numPoints--;
			return;
		}
	}
	ALWAYS_ASSERT(false); // There are really a lot of keyframed points here :)
}

void Rope2::Discretize(bool updateLast)
{
	PROFILE(Rope, Rope_Discretize);

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	Point* pPos = NDI_NEW Point[m_maxNumPoints];
	Vector* pVel = NDI_NEW Vector[m_maxNumPoints];
	F32* pRopeDist = NDI_NEW F32[m_maxNumPoints];
	I32* pClosestCon = NDI_NEW I32[m_maxNumPoints];
	RopeNodeFlags* pNodeFlags = NDI_NEW RopeNodeFlags[m_maxNumPoints];

	U32F iEdge = 0;
	U32F numPoints = 0;
	F32 ropeDist = 0.0f;

	bool firstDynamicSet = false;
	m_firstDynamicPoint = 0;
	m_lastDynamicPoint = -1;

	if (m_numEdges > 0 && m_pEdges[iEdge].m_ropeDist == 0.0f)
	{
		pPos[numPoints] = m_pEdges[iEdge].m_pos;
		pVel[numPoints] = (m_pEdges[iEdge].m_flags & kNodeKeyedVelocity) ? GetKeyVel(ropeDist) : (m_pEdges[iEdge].m_pos - GetPos(ropeDist)) * m_scInvStepTime;
		pNodeFlags[numPoints] = m_pEdges[iEdge].m_flags;
		if (updateLast)
		{
			m_pLastPos[numPoints] = m_pPos[0];
			m_pLastVel[numPoints] = m_pVel[0];
		}
		iEdge++;
	}
	else
	{
		pPos[numPoints] = m_pPos[0];
		pVel[numPoints] = m_pVel[0];
		pClosestCon[numPoints] = IsNodeLoose(0) ? 0 : -1;
		pNodeFlags[numPoints] = 0;
		if (updateLast)
		{
			m_pLastPos[numPoints] = m_pPos[0];
			m_pLastVel[numPoints] = m_pVel[0];
		}
		m_firstDynamicPoint = 0;
		firstDynamicSet = true;
	}
	PHYSICS_ASSERT(IsFinite(pPos[numPoints]));
	PHYSICS_ASSERT(IsFinite(pVel[numPoints]));
	pRopeDist[numPoints] = 0.0f;
	numPoints++;

	F32 nodeRopeDist = 0.0f;

	while (ropeDist < m_fLength-0.0001f)
	{
		F32 prevRopeDist = ropeDist;

		// Skip all non-strained edge nodes
		// Also all strained edges that have no active edge (they can make the rope look ugly because they will have no edge normal for offset)
		while (iEdge < m_numEdges && ((m_pEdges[iEdge].m_flags & (kNodeKeyframed|kNodeKeyframedSeg|kNodeStrained)) == 0 ||
			(((m_pEdges[iEdge].m_flags & (kNodeKeyframed|kNodeStrained)) == kNodeStrained) && m_pEdges[iEdge].m_activeEdges == 0)))
		{
			iEdge++;
		}

		Point prevPos;
		Vector prevVel;
		RopeNodeFlags flags;
		I32 closestCon = -1;

		F32 maxUnstretchRopeDist = 0.0f;
		I32 prevNodeIndexUnstretch = -1;

		if (iEdge < m_numEdges)
		{
			ropeDist = m_pEdges[iEdge].m_ropeDist;
			flags = m_pEdges[iEdge].m_flags;
		}
		else
		{
			ropeDist = m_fLength;
			flags = 0;
			if (pNodeFlags[numPoints-1] & kNodeKeyframed)
			{
				// If the last section of loose rope had previously a keyframe point, we will see if it got stretched and unstretch it now
				// to remove the bungee effect
				GetDiscretizationUnstretchData(prevRopeDist, prevPos, prevVel, prevNodeIndexUnstretch, maxUnstretchRopeDist);
			}
		}

		if ((flags & (kNodeStrained|kNodeKeyframedSeg)) == 0)
		{
			F32 dist = ropeDist - pRopeDist[numPoints-1];
			while (nodeRopeDist - prevRopeDist <= 0.01f)
			{
				nodeRopeDist += m_fSegmentLength;
			}
			while (ropeDist - nodeRopeDist > 0.01f)
			{
				pRopeDist[numPoints] = nodeRopeDist;
				nodeRopeDist += m_fSegmentLength;
				if (numPoints >= m_maxNumPoints)
				{
					// No more space, just skip this point
					ASSERT(false);
					break;
				}
				if (prevNodeIndexUnstretch >= 0)
				{
					GetPosVelAndClosestConstraintWithUnstretch(pRopeDist[numPoints], prevRopeDist, prevPos, prevVel, prevNodeIndexUnstretch, maxUnstretchRopeDist, pPos[numPoints], pVel[numPoints], pClosestCon[numPoints]);
				}
				else
				{
					GetPosVelAndClosestConstraint(pRopeDist[numPoints], pPos[numPoints], pVel[numPoints], pClosestCon[numPoints]);
				}
				if (!firstDynamicSet)
				{
					m_firstDynamicPoint = numPoints;
					firstDynamicSet = true;
				}
				m_lastDynamicPoint = numPoints;
				PHYSICS_ASSERT(IsFinite(pPos[numPoints]));
				PHYSICS_ASSERT(IsFinite(pVel[numPoints]));
				pNodeFlags[numPoints] = flags & kNodeEdgeDetection;
				if (updateLast)
				{
					m_pLastPos[numPoints] = pPos[numPoints];
					m_pLastVel[numPoints] = pVel[numPoints];
				}
				numPoints++;
				ALWAYS_ASSERT(numPoints <= m_maxNumPoints);
			}
		}

		if (numPoints >= m_maxNumPoints)
		{
			ASSERT(false);
			if ((flags & kNodeKeyframed) == 0)
			{
				// No more space, just skip this point
				iEdge++;
				continue;
			}
			// Its keyframed we don't want to skip ...
			RemoveNonKeyframedPoint(numPoints, pPos, pVel, pRopeDist, pNodeFlags, pClosestCon, m_firstDynamicPoint, m_lastDynamicPoint);
		}

		pRopeDist[numPoints] = ropeDist;
		if (iEdge < m_numEdges)
		{
			pPos[numPoints] = GetEdgePosOffsetedFromCollision(iEdge);
			pVel[numPoints] = (m_pEdges[iEdge].m_flags & kNodeKeyedVelocity) ? GetKeyVel(ropeDist) : (pPos[numPoints] - GetPos(ropeDist)) * m_scInvStepTime;
			pClosestCon[numPoints] = -1;
			if (updateLast)
			{
				GetPosAndVel(ropeDist, m_pLastPos[numPoints], m_pLastVel[numPoints]);
			}
			iEdge++;
		}
		else
		{
			if (prevNodeIndexUnstretch >= 0)
			{
				GetPosVelAndClosestConstraintWithUnstretch(ropeDist, prevRopeDist, prevPos, prevVel, prevNodeIndexUnstretch, maxUnstretchRopeDist, pPos[numPoints], pVel[numPoints], pClosestCon[numPoints]);
			}
			else
			{
				GetPosVelAndClosestConstraint(ropeDist, pPos[numPoints], pVel[numPoints], pClosestCon[numPoints]);
			}
			if (updateLast)
			{
				m_pLastPos[numPoints] = pPos[numPoints];
				m_pLastVel[numPoints] = pVel[numPoints];
			}
		}
		PHYSICS_ASSERT(IsFinite(pPos[numPoints]));
		PHYSICS_ASSERT(IsFinite(pVel[numPoints]));
		pNodeFlags[numPoints] = flags;
		if ((flags & (kNodeKeyframed|kNodeStrained)) == 0)
		{
			if (!firstDynamicSet)
			{
				m_firstDynamicPoint = numPoints;
				firstDynamicSet = true;
			}
			m_lastDynamicPoint = numPoints;
		}
		numPoints++;
		ALWAYS_ASSERT(numPoints <= m_maxNumPoints);
	}

	// @@JS
	// Set edge flags on nodes that are right before and after a free rope edge. We use this for multi grid.
	//{
	//	U32F iKey = 0;
	//	while (iKey < numKeyPoints && ((pKeyFlags[iKey] & (kNodeEdge|kNodeStrained|kNodeKeyframed)) != kNodeEdge))
	//	{
	//		iKey++;
	//	}
	//	F32 edgeRopeDist = iKey < numKeyPoints ? pKeyRopeDist[iKey] : m_fLength;
	//	U32F iNode = 0;
	//	while (iNode < numPoints-1)
	//	{
	//		if (pRopeDist[iNode+1] > edgeRopeDist)
	//		{
	//			if ((m_pNodeFlags[iNode] & (kNodeEdge|kNodeStrained|kNodeKeyframed)) == 0)
	//			{
	//				m_pNodeFlags[iNode] |= kNodeEdge;
	//			}
	//			if (pRopeDist[iNode] < edgeRopeDist)
	//			{
	//				iNode++;
	//				if ((m_pNodeFlags[iNode] & (kNodeEdge|kNodeStrained|kNodeKeyframed)) == 0)
	//				{
	//					m_pNodeFlags[iNode] |= kNodeEdge;
	//				}
	//			}
	//			while (iKey < numKeyPoints && (((pKeyFlags[iKey] & kNodeEdge) == 0) || pKeyRopeDist[iKey] <= pRopeDist[iNode]))
	//			{
	//				iKey++;
	//			}
	//			edgeRopeDist = iKey < numKeyPoints ? pKeyRopeDist[iKey] : m_fLength;
	//		}
	//		iNode++;
	//	}
	//}

	// Total game hacks to help rope drape as desired:
	// increase mass of nodes behind the drape point

	F32* pInvMass = NDI_NEW F32[numPoints];
	for (U32F ii = 0; ii<numPoints; ii++)
	{
		F32 invM;
		if (pNodeFlags[ii] & (kNodeKeyframed|kNodeStrained))
		{
			invM = 0.0f;
		}
		else
		{
			invM = m_fMassiveEndDist > 0.0f ? Min(1.0f, m_fMassiveEndRatio + (m_fLength - pRopeDist[ii]) / m_fMassiveEndDist * (1.0f - m_fMassiveEndRatio)) : 1.0f;
			F32 prevInvM = GetInvRelMass(pRopeDist[ii]);
			if (invM < prevInvM)
			{
				// Prevent adding energy when increasing mass of a node
				pVel[ii] *= invM/prevInvM;
			}
		}
		pInvMass[ii] = invM;
	}

	// .. and decrease gravity on the part of rope before the drape
	if (m_fMassiveEndDist > 0.0f)
	{
		for (U32 ii = 0; ii<numPoints-1; ii++)
		{
			if (m_fLength - pRopeDist[ii] < m_fMassiveEndDist)
				break;

			if ((pNodeFlags[ii] & kNodeKeyframed) == 0)
			{
				pVel[ii] += Vector(0.0f, 7.0f, 0.0f) * m_scStepTime;
			}
		}
	}

	if (m_pTwistDir)
	{
		Vector* pTwistDir = NDI_NEW Vector[numPoints];
		U32 iKey = 0;
		U32 iOld = 0;
		for (U32 ii = 0; ii<numPoints; ii++)
		{
			if (iKey<m_numKeyPoints && Abs(m_pKeyRopeDist[iKey]-pRopeDist[ii]) < kRopeDistKeyEps)
			{
				if (m_pKeyNodeFlags[iKey] & kNodeUserTwist)
				{
					pTwistDir[ii] = m_pKeyTwistDir[iKey];
					pNodeFlags[ii] |= kNodeUserTwist;
					iKey++;
					JAROS_ASSERT(Abs(1.0f-Length(pTwistDir[ii]))<0.001f);
					continue;
				}
				else if ((m_pKeyNodeFlags[iKey] & kNodeKeyframedSeg) && ii > 0 && (pNodeFlags[ii-1] & kNodeUserTwist))
				{
					pTwistDir[ii] = pTwistDir[ii-1];
					pNodeFlags[ii] |= kNodeUserTwist;
					iKey++;
					JAROS_ASSERT(Abs(1.0f-Length(pTwistDir[ii]))<0.001f);
					continue;
				}
				iKey++;
			}

			while (iOld+1<m_numPoints && m_pRopeDist[iOld+1] < pRopeDist[ii])
			{
				iOld++;
			}
			if (iOld+1<m_numPoints && m_pRopeDist[iOld+1] > m_pRopeDist[iOld])
			{
				F32 tt = (pRopeDist[ii]-m_pRopeDist[iOld])/(m_pRopeDist[iOld+1]-m_pRopeDist[iOld]);
				pTwistDir[ii] = SafeNormalize(Lerp(m_pTwistDir[iOld], m_pTwistDir[iOld+1], tt), tt > 0.5f ? m_pTwistDir[iOld+1] : m_pTwistDir[iOld]);
			}
			else
			{
				pTwistDir[ii] = m_pTwistDir[iOld];
			}
			JAROS_ASSERT(Abs(1.0f-Length(pTwistDir[ii]))<0.001f);
		}

		memcpy(m_pTwistDir, pTwistDir, numPoints*sizeof(m_pTwistDir[0]));
	}

	memcpy(m_pPos, pPos, numPoints*sizeof(m_pPos[0]));
	memcpy(m_pVel, pVel, numPoints*sizeof(m_pVel[0]));
	memcpy(m_pRopeDist, pRopeDist, numPoints*sizeof(m_pRopeDist[0]));
	memcpy(m_pNodeFlags, pNodeFlags, numPoints*sizeof(m_pNodeFlags[0]));
	memcpy(m_pInvRelMass, pInvMass, numPoints*sizeof(m_pInvRelMass[0]));
	m_numPoints = numPoints;

	if (m_pTensionFriction)
	{
		F32* pTenFric = NDI_NEW F32[numPoints];
		for (U32F ii = 0; ii<numPoints; ii++)
		{
			pTenFric[ii] = GetTensionFriction(pRopeDist[ii]);
		}
		memcpy(m_pTensionFriction, pTenFric, numPoints*sizeof(m_pTensionFriction[0]));
	}

	if (m_bResetDynamics)
	{
		for (U32F ii = 0; ii<m_numPoints; ii++)
		{
			m_pLastPos[ii] = m_pPos[ii];
			m_pVel[ii] = kZero;
			m_pLastVel[ii] = kZero;
		}
	}

	// Transfer old constraint to the corresponding closest points in new discretization
	{
		Constraint* pCon = NDI_NEW Constraint[m_lastDynamicPoint-m_firstDynamicPoint+1];
		for (I32F ii = m_firstDynamicPoint; ii<=m_lastDynamicPoint; ii++)
		{
			if (!IsNodeLoose(ii) || pClosestCon[ii] < 0)
			{
				pCon[ii-m_firstDynamicPoint].Reset(false);
			}
			else
			{
				pCon[ii-m_firstDynamicPoint] = m_pConstr[pClosestCon[ii]];
			}
		}
		memcpy(&m_pConstr[m_firstDynamicPoint], pCon, (m_lastDynamicPoint-m_firstDynamicPoint+1)*sizeof(Constraint));
	}

	m_fMaxRadius = m_fRadius;
	if (m_numKeyRadius == 0)
	{
		for (U32F ii = 0; ii<numPoints; ii++)
			m_pRadius[ii] = m_fRadius;
	}
	else
	{
		U32F iKey = 0;
		RadiusKey prevKey = { /*.m_ropeDist =*/ 0.0f, /*.m_radius =*/ m_fRadius };
		RadiusKey key = { /*.m_ropeDist =*/ 0.0f, /*.m_radius =*/ m_fRadius };
		for (U32F ii = 0; ii<numPoints; ii++)
		{
			while (iKey < m_numKeyRadius && m_pRopeDist[ii] >= key.m_ropeDist)
			{
				prevKey = key;
				key = m_pKeyRadius[iKey];
				iKey++;
				m_fMaxRadius = Max(m_fMaxRadius, key.m_radius);
			};
			if (key.m_ropeDist < m_fLength && m_pRopeDist[ii] >= key.m_ropeDist)
			{
				prevKey = key;
				key.m_ropeDist = m_fLength;
				key.m_radius = m_fRadius;
			}
			F32 fB = (m_pRopeDist[ii] - prevKey.m_ropeDist) / (key.m_ropeDist - prevKey.m_ropeDist);
			m_pRadius[ii] = ( 1.0f - fB) * prevKey.m_radius + fB * key.m_radius;
		}
	}
}

void Rope2::StraightenStrainedSections()
{
	for (U32F ii = 0; ii<m_numPoints; ii++)
	{
		if (IsNodeStrainedInt(ii) && !IsNodeKeyframedInt(ii))
		{
			m_pPos[ii] = GetStraightPos(m_pRopeDist[ii]);
			PHYSICS_ASSERT(IsFinite(m_pPos[ii]));
		}
	}
}

I32F Rope2::AddSimPoint(F32 ropeDist)
{
	CheckStepNotRunning();

	ASSERT(m_numPoints < m_maxNumPoints);
	if (m_numPoints >= m_maxNumPoints)
		return -1;

	for (I32F ii = m_numPoints-1; ii>=0; ii--)
	{
		if (m_pRopeDist[ii] <= ropeDist)
		{
			if (ropeDist - m_pRopeDist[ii] <= kRopeDistKeyEps)
			{
				return ii;
			}
			if (ii == m_numPoints-1)
			{
				m_pPos[ii+1] = m_pPos[ii];
				m_pVel[ii+1] = m_pVel[ii];
				m_pLastPos[ii+1] = m_pLastPos[ii];
				m_pLastVel[ii+1] = m_pLastVel[ii];
				m_pRadius[ii+1] = m_pRadius[ii];
				m_pNodeFlags[ii+1] = 0;
			}
			else
			{
				if (ii < m_numPoints-1 && m_pRopeDist[ii+1] - ropeDist <= kRopeDistKeyEps)
				{
					return ii+1;
				}
				Scalar t = (ropeDist - m_pRopeDist[ii]) / (m_pRopeDist[ii+1] - m_pRopeDist[ii]);
				memmove(m_pPos+ii+2, m_pPos+ii+1, (m_numPoints-ii-1)*sizeof(m_pPos[0]));
				m_pPos[ii+1] = Lerp(m_pPos[ii], m_pPos[ii+2], t);
				memmove(m_pVel+ii+2, m_pVel+ii+1, (m_numPoints-ii-1)*sizeof(m_pVel[0]));
				m_pVel[ii+1] = Lerp(m_pVel[ii], m_pVel[ii+2], t);
				memmove(m_pLastPos+ii+2, m_pLastPos+ii+1, (m_numPoints-ii-1)*sizeof(m_pLastPos[0]));
				m_pLastPos[ii+1] = Lerp(m_pLastPos[ii], m_pLastPos[ii+2], t);
				memmove(m_pLastVel+ii+2, m_pLastVel+ii+1, (m_numPoints-ii-1)*sizeof(m_pLastVel[0]));
				m_pLastVel[ii+1] = Lerp(m_pLastVel[ii], m_pLastVel[ii+2], t);
				memmove(m_pRopeDist+ii+2, m_pRopeDist+ii+1, (m_numPoints-ii-1)*sizeof(m_pRopeDist[0]));
				memmove(m_pRadius+ii+2, m_pRadius+ii+1, (m_numPoints-ii-1)*sizeof(m_pRadius[0]));
				m_pRadius[ii+1] = Lerp(m_pRadius[ii], m_pRadius[ii+2], (F32)t);
				memmove(m_pConstr+ii+2, m_pConstr+ii+1, (m_numPoints-ii-1)*sizeof(m_pConstr[0]));
				memmove(m_pNodeFlags+ii+2, m_pNodeFlags+ii+1, (m_numPoints-ii-1)*sizeof(m_pNodeFlags[0]));
				m_pNodeFlags[ii+1] = m_pNodeFlags[ii+2] & kNodeStrained;
			}
			m_pRopeDist[ii+1] = ropeDist;
			m_pConstr[ii+1].Reset(false);
			m_numPoints++;
			return ii+1;
		}
	}

	return -1;
}

void Rope2::ResetSimPos(U32 ii, Point_arg pos)
{
	CheckStepNotRunning();

	PHYSICS_ASSERT(ii < m_numPoints);
	m_pPos[ii] = pos;
	m_pLastPos[ii] = pos;
	m_pVel[ii] = kZero;
	m_pLastVel[ii] = kZero;
	m_pConstr[ii].Reset(false);

}

void Rope2::RedistributeStrainedEdges(EdgePoint* pEdges, U32F numEdges)
{
	I32F prevKeyframed = -1;
	for (U32F ii = 0; ii<numEdges; ii++)
	{
		if (pEdges[ii].m_flags & kNodeKeyframed)
		{
			if (prevKeyframed >= 0 && (ii-prevKeyframed > 1)) // && (pEdges[ii].m_flags & kNodeStrained))
			{
				F32 ropeLen = pEdges[ii].m_ropeDist - pEdges[prevKeyframed].m_ropeDist;
				F32 dist = 0.0f;
				for (U32F jj = prevKeyframed+1; jj<=ii; jj++)
				{
					dist += Dist(pEdges[jj].m_pos, pEdges[jj-1].m_pos);
				}

				ASSERT(dist > 0.0f);
				F32 distRatio = ropeLen / Max(0.0001f, dist);
				F32 dist1 = 0.0f;
				for (U32F jj = prevKeyframed+1; jj<ii; jj++)
				{
					dist1 += Dist(pEdges[jj].m_pos, pEdges[jj-1].m_pos);
					pEdges[jj].m_ropeDist = pEdges[prevKeyframed].m_ropeDist + dist1 * distRatio;
				}

			}
			prevKeyframed = ii;
		}
	}
}

Vector Rope2::GetEdgePointCollisionNormal(U32F ii)
{
	CheckStepNotRunning();

	if (ii == 0 || ii >= m_numEdges-1 || m_pEdges[ii].m_numEdges == 0)
		return kZero;

	if ((m_pEdges[ii].m_flags & (kNodeKeyframed|kNodeStrained)) == kNodeStrained)
	{
		Vector prevDir = SafeNormalize(m_pEdges[ii-1].m_pos - m_pEdges[ii].m_pos, kZero);
		Vector nextDir = SafeNormalize(m_pEdges[ii].m_pos - m_pEdges[ii+1].m_pos, kZero);
		Vector normSum(kZero);
		U32F numNorms = 0;
		for (U32F iEdge = 0; iEdge<m_pEdges[ii].m_numEdges; iEdge++)
		{
			if (m_pEdges[ii].GetEdgeActive(iEdge))
			{
				const RopeColEdge& edge = m_colCache.m_pEdges[m_pEdges[ii].m_edgeIndices[iEdge]];
				Vector edgeVec(edge.m_vec);
				edgeVec *= m_pEdges[ii].GetEdgePositive(iEdge) ? 1.0f : -1.0f;
				Vector prevDirPerp = prevDir - Dot(prevDir, edgeVec) * edgeVec;
				Vector normPrev = SafeNormalize(Cross(edgeVec, prevDirPerp), kZero);
				if (!AllComponentsEqual(normPrev, kZero))
				{
					normSum += normPrev;
					numNorms++;
				}
				Vector nextDirPerp = nextDir - Dot(nextDir, edgeVec) * edgeVec;
				Vector normNext = SafeNormalize(Cross(edgeVec, nextDirPerp), kZero);
				if (!AllComponentsEqual(normNext, kZero))
				{
					normSum += normNext;
					numNorms++;
				}
			}
		}
		if (numNorms > 0)
		{
			Scalar l;
			Vector norm = Normalize(normSum / numNorms, l);
			if (l > 0.001f)
				return norm;
		}
	}

	return kZero;
}

Point Rope2::GetEdgePosOffsetedFromCollision(U32F ii)
{
	CheckStepNotRunning();

	return m_pEdges[ii].m_pos + GetEdgePointCollisionNormal(ii) * m_fStrainedCollisionOffset;
}

void Rope2::GetCollideFilter(bool bStrained, CollideFilter& filter) const
{
	Collide::LayerMask layerMask = Collide::kLayerMaskRope;
	if (!bStrained)
	{
		layerMask |= Collide::kLayerMaskCharacterAndProp;
	}
	layerMask &= bStrained ? m_strainedLayerMask : m_layerMask; // remove any layers that the client code wants filtered out

	// Cant do this otherwise our constraint hash will change
	//if (m_bSleeping)
	//	layerMask &= ~Collide::kLayerMaskBackground; // watch only for FG collisions that might wake us up

	filter = CollideFilter(layerMask, Pat(Pat::kRopeThroughMask | Pat::kStealthVegetationMask));
	filter.SetIgnoreObj(m_hFilterIgnoreObject.ToProcess());
}

void Rope2::Collide()
{
	if (!g_ropeMgr.m_disableCollisions)
	{
		PROFILE(Rope, Rope_Collide);

		DebugPrimTime::NoRecordJanitor jNoRecord(m_pDebugger);

		ScopedTempAllocator jjAlloc(FILE_LINE_FUNC);

		// Constraint* pPrevCon = NDI_NEW Constraint[m_numPoints];
		// memcpy(pPrevCon, m_pConstr, m_numPoints*sizeof(pPrevCon[0]));

		// Aabbs
		m_aabbSlacky.SetEmpty();

		if (!m_bAllowKeyStretchConstraints)
		{
			PHYSICS_ASSERT(m_pPrevKeyIndex && m_pNextKeyIndex);
			if (!m_bSimpleRope)
			{
				PrepareClosestKeyData();
			}
		}

		for (I32F ii = m_firstDynamicPoint; ii<=m_lastDynamicPoint; ii++)
		{
			GetDynamicNodeCollideAabb(ii, m_pNodeAabb[ii]);
			m_aabbSlacky.m_max = Max(m_aabbSlacky.m_max, m_pNodeAabb[ii].m_max);
			m_aabbSlacky.m_min = Min(m_aabbSlacky.m_min, m_pNodeAabb[ii].m_min);
		}

		HavokWorldMarkForRead();

		{
			PROFILE(Rope, PreCollide);

			// We don't want PreCollide on things that go to col cache
			CollideFilter excludeFilter;
			GetCollideFilter(true, excludeFilter);

			for (I32F ii = 0; ii < m_numPoints; ii++)
			{
				if (!IsNodeStrainedInt(ii) && !IsNodeKeyframedInt(ii))
				{
					m_pConstr[ii].PreCollision(this, ii, m_pPos[ii], m_pRadius[ii], excludeFilter.GetLayerInclude(), m_bSleeping);
				}
				else
				{
					m_pConstr[ii].Reset(m_bSleeping);
				}
			}
		}

		for (U32F ii = 0; ii < m_numCustomColliders; ii++)
		{
			if (m_ppCustomColliders[ii]->m_enabled)
				CollideWithCollider(m_ppCustomColliders[ii], RopeColliderHandle(ii));
		}

		for (U32F ii = 0; ii < m_numRigidBodyColliders; ii++)
		{
			RopeColliderHandle hCollider(m_phRigidBodyColliders[ii]);
			if (hCollider.IsValid())
			{
				RopeCollider colliderBuffer;
				CollideWithCollider(hCollider.GetCollider(&colliderBuffer, this), hCollider);
			}
		}

		HavokWorldUnmarkForRead();

		if (m_layerMask != 0 && m_aabbSlacky.IsValid())
		{
			CollideFilter filter;
			GetCollideFilter(false, filter);

			if (m_aabb.IsValid()) // && m_aabbStrained.Contains(m_aabb)) // we'll just hope the strained aabb with 1m extra radius will be enough
			{
				CollideWithColCache();
				CollideFilter strainedFilter;
				GetCollideFilter(true, strainedFilter);
				filter.SetLayerInclude(filter.GetLayerInclude() & ~strainedFilter.GetLayerInclude());
			}
			else
			{
				if (m_numPoints > 100)
				{
					JAROS_ALWAYS_ASSERT(m_bSleeping); // not good for performance
				}
			}

			m_pCollector->BeginCast(this, filter, false);
			HavokAabbQuery(m_aabbSlacky.m_min, m_aabbSlacky.m_max, *m_pCollector, filter);
			m_pCollector->EndCast();
		}

		for (I32F ii = m_firstDynamicPoint+1; ii<=m_lastDynamicPoint; ii++)
		{
			DisableRopeBreakingEdgeConstraints(ii-1, ii);
		}

		for (I32F ii = m_firstDynamicPoint+1; ii<=m_lastDynamicPoint; ii++)
		{
			//m_pConstr[ii].SlowDepenetration(m_pPos[ii], m_pRadius[ii], m_scStepTime);
			m_pConstr[ii].FixCompetingConstraints(m_pPos[ii], m_pRadius[ii]);
			//m_pConstr[ii].SortPlanesForPersistency(pPrevCon[ii], m_pPos[ii]);
		}

		/*if (m_numEdges > 0 && !m_bNeverStrained)
		{
			// Use strained edges to get more robust constraints for slacky rope
			AddEdgesToConstraints();
		}*/
	}
	else
	{
		memset(m_pConstr, 0, sizeof(Constraint)*m_numPoints);
	}
}

void Rope2::CreateColCache()
{
	//if (m_bNeverStrained)
	//	return;

	//bool hasStrainedSection = false;
	//for (U32F ii = 0; ii<m_numKeyPoints; ii++)
	//{
	//	if ((m_pKeyNodeFlags[ii] & (kNodeStrained | kNodeKeyframedSeg)) == kNodeStrained)
	//	{
	//		hasStrainedSection = true;
	//		break;
	//	}
	//}

	//if (!hasStrainedSection)
	//	return;

	PROFILE(Rope, Rope_CreateColCache);

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	// Need to save active edge ids and restore the edge indices after the new col cache is in
	RopeColEdgeId* pEdgeIds = NDI_NEW RopeColEdgeId[m_numEdges*EdgePoint::kMaxNumEdges];
	StoreEdgeIds(pEdgeIds, m_pEdges, m_numEdges);
	RopeColEdgeId* pSaveEdgeIds = nullptr;
	if (m_pSaveEdges)
	{
		pSaveEdgeIds = NDI_NEW RopeColEdgeId[m_numSaveEdges*EdgePoint::kMaxNumEdges];
		StoreEdgeIds(pSaveEdgeIds, m_pSaveEdges, m_numSaveEdges);
	}
	RopeColEdgeId* pExternSaveEdgeIds = nullptr;
	if (m_pExternSaveEdges)
	{
		pExternSaveEdgeIds = NDI_NEW RopeColEdgeId[m_numExternSaveEdges*EdgePoint::kMaxNumEdges];
		StoreEdgeIds(pExternSaveEdgeIds, m_pExternSaveEdges, m_numExternSaveEdges);
	}

	for (U32F iEdgePoint = 0; iEdgePoint<m_numEdges; iEdgePoint++)
	{
		m_pEdges[iEdgePoint].m_numNewEdges = 0;
	}
	for (U32F iEdgePoint = 0; iEdgePoint<m_numSaveEdges; iEdgePoint++)
	{
		m_pSaveEdges[iEdgePoint].m_numNewEdges = 0;
	}
	for (U32F iEdgePoint = 0; iEdgePoint<m_numExternSaveEdges; iEdgePoint++)
	{
		m_pExternSaveEdges[iEdgePoint].m_numNewEdges = 0;
	}

	m_colCache.Reset();

	bool bAabbQuery = m_strainedLayerMask != 0 && m_aabb.IsValid();
	if (!g_ropeMgr.m_disableCollisions && (bAabbQuery || m_bCustomCollidersColCache))
	{
		m_colCache.StartBuild(m_aabb);

		CollideFilter filter;
		GetCollideFilter(true, filter);
		m_colCache.m_filter = filter;

		m_pCollector->BeginCast(this, filter, true);

		if (m_bCustomCollidersColCache)
		{
			for (U32 ii = 0; ii<m_numCustomColliders; ii++)
			{
				m_pCollector->AddCollider(m_ppCustomColliders[ii], RopeColliderHandle(ii));
			}
		}

		if (bAabbQuery)
		{
			HavokAabbQuery(m_aabb.m_min, m_aabb.m_max, *m_pCollector, filter);
		}

		m_pCollector->EndCast();

		m_colCache.EndBuild();
	}

	// Restore edge indices based on ids
	RestoreEdgeIndices(pEdgeIds, m_pEdges, m_numEdges);
	if (m_pSaveEdges)
	{
		RestoreEdgeIndices(pSaveEdgeIds, m_pSaveEdges, m_numSaveEdges);
	}
	if (m_pExternSaveEdges)
	{
		RestoreEdgeIndices(pExternSaveEdgeIds, m_pExternSaveEdges, m_numExternSaveEdges);
	}
}

bool Rope2::ValidateColCache()
{
	PROFILE(Rope, Rope_ValidateColCache);

	// Has any shape in the cache disappeared or moved?
	for (U32 iShape = 0; iShape < m_colCache.m_numShapes; iShape++)
	{
		const RopeColliderHandle& hCollider = m_colCache.m_pShapes[iShape];
		if (!hCollider.IsValid())
		{
			return false;
		}
		Locator loc = hCollider.GetLocator(this);
		Locator locPrev = hCollider.GetPrevLocator(this);
		if (loc != locPrev)
		{
			return false;
		}
	}

	if (m_colCache.m_numShapes == m_colCache.m_maxShapes)
	{
		// More does not fit anyway
		return true;
	}

	// Is there any new shape that should be in the cache nad is not?
	if (!g_ropeMgr.m_disableCollisions && m_strainedLayerMask != 0 && m_aabb.IsValid())
	{
		CollideFilter filter;
		GetCollideFilter(true, filter);

		m_pCollector->BeginValidateCache(this, filter);
		HavokAabbQuery(m_aabb.m_min, m_aabb.m_max, *m_pCollector, filter);
		m_pCollector->EndCast();

		return m_pCollector->IsCacheValid();
	}

	return true;
}

void Rope2::AddColEdgeToConstraint(U32F ii, const RopeColEdge& edge0, const RopeColEdge& edge1)
{
	//Vec4 norm04 = edge0.m_normal.GetVec4();
	//norm04.SetW(-Dot(edge0.m_normal, edge0.m_pnt - kOrigin) - m_pRadius[ii]);
	//Vector biNorm0 = Cross(Vector(edge0.m_vec), edge0.m_normal);
	//Vec4 biNorm04 = biNorm0.GetVec4();
	//biNorm04.SetW(-Dot(biNorm0, edge0.m_pnt - kOrigin));

	//Vec4 norm14 = edge1.m_normal.GetVec4();
	//norm14.SetW(-Dot(edge1.m_normal, edge1.m_pnt - kOrigin) - m_pRadius[ii]);
	//Vector biNorm1 = Cross(Vector(edge1.m_vec), edge1.m_normal);
	//Vec4 biNorm14 = biNorm1.GetVec4();
	//biNorm14.SetW(-Dot(biNorm1, edge1.m_pnt - kOrigin));

	//U32F iTri = edge0.m_triIndex;
	//U32F iShape = m_colCache.m_pTris[iTri].m_shapeIndex;
	//RopeColliderHandle hCollider = m_colCache.m_pShapes[iShape].m_hBody;
	//RopeCollider colliderBuffer;
	//const RopeCollider* pCollider = hCollider.GetCollider(&colliderBuffer, this);
	//Vector linVel = pCollider->m_linVel;
	//Vector angVel = pCollider->m_angVel;
	//Vector vel = linVel + Cross(angVel, m_pPos[ii] - pCollider->m_loc.GetTranslation());

	//m_pConstr[ii].AddOuterEdge(norm04, biNorm04, norm14, biNorm14, vel, hCollider);
}

void Rope2::AddColEdgePlaneToConstraint(U32F ii, const RopeColEdge& edge)
{
	//Vec4 norm4 = edge.m_normal.GetVec4();
	//norm4.SetW(-Dot(edge.m_normal, edge.m_pnt - kOrigin) - m_pRadius[ii]);

	//U32F iTri = edge.m_triIndex;
	//U32F iShape = m_colCache.m_pTris[iTri].m_shapeIndex;
	//RopeColliderHandle hCollider = m_colCache.m_pShapes[iShape].m_hBody;
	//RopeCollider colliderBuffer;
	//const RopeCollider* pCollider = hCollider.GetCollider(&colliderBuffer, this);
	//Vector linVel = pCollider->m_linVel;
	//Vector angVel = pCollider->m_angVel;
	//Vector vel = linVel + Cross(angVel, edge.m_pnt - pCollider->m_loc.GetTranslation());

	//m_pConstr[ii].Append(norm4, edge.m_pnt, vel, hCollider);
}

void Rope2::AddEdgesToConstraints(U32F iSegFirstEdge, U32F iSegLastEdge)
{
	HavokMarkForReadJanitor jj; // because reading the havok shape from colliders

	Scalar dist3d(kZero);
	for (U32F ii = iSegFirstEdge+1; ii<=iSegLastEdge; ii++)
	{
		dist3d += Dist(m_pEdges[ii].m_pos, m_pEdges[ii-1].m_pos);
	}

	F32 slack = Max(m_pEdges[iSegLastEdge].m_ropeDist - m_pEdges[iSegFirstEdge].m_ropeDist - (F32)dist3d, 0.1f);

	U32F ii;
	for (ii = 0; ii<m_numPoints; ii++)
	{
		if (m_pRopeDist[ii] >= m_pEdges[iSegFirstEdge].m_ropeDist)
			break;
	}

	U32F closestEdge = iSegFirstEdge+1;
	for (; m_pRopeDist[ii] <= m_pEdges[iSegLastEdge].m_ropeDist; ii++)
	{
		if (m_pNodeFlags[ii] & kNodeKeyframed)
			continue;

		F32 closestEdgeDist = Abs(m_pEdges[closestEdge].m_ropeDist - m_pRopeDist[ii]);
		while (closestEdge+1 < iSegLastEdge && Abs(m_pEdges[closestEdge+1].m_ropeDist - m_pRopeDist[ii]) < closestEdgeDist)
		{
			closestEdge++;
			closestEdgeDist = Abs(m_pEdges[closestEdge].m_ropeDist - m_pRopeDist[ii]);
		}

		if (closestEdgeDist < slack)
		{
			U32F iClosestPrev = closestEdge;
			U32F iClosestNext = closestEdge;
			F32 prevDist = closestEdgeDist;
			F32 nextDist = closestEdgeDist;

			U32F numConstrAdded = 0;
			while (numConstrAdded < 4 && (prevDist < slack || nextDist < slack))
			{
				U32F iEdgePoint = prevDist < nextDist ? iClosestPrev : iClosestNext;
				I32F iEdgePoint2 = m_pRopeDist[ii] < m_pEdges[iEdgePoint].m_ropeDist ? iEdgePoint-1 : iEdgePoint+1;
				ASSERT(iEdgePoint >= iSegFirstEdge && iEdgePoint2 <= iSegLastEdge);
				const EdgePoint& ePoint = m_pEdges[iEdgePoint];
				Vector dir = SafeNormalize(m_pEdges[iEdgePoint2].m_pos - ePoint.m_pos, kZero);
				ASSERT(!AllComponentsEqual(dir, kZero));

				I32F minEdgeIndex = -1;
				I32F maxEdgeIndex = -1;
				Scalar minDot = Scalar(FLT_MAX);
				Scalar maxDot = Scalar(-FLT_MAX);
				for (U32F iEdgeIndex = 0; iEdgeIndex<ePoint.m_numEdges; iEdgeIndex++)
				{
					const RopeColEdge& edge = m_colCache.m_pEdges[ePoint.m_edgeIndices[iEdgeIndex]];
					Scalar dt = Dot(dir, edge.m_normal);
					if (dt < minDot)
					{
						minEdgeIndex = iEdgeIndex;
						minDot = dt;
					}
					if (dt > maxDot)
					{
						maxEdgeIndex = iEdgeIndex;
						maxDot = dt;
					}
				}

				/*if (Min(prevDist, nextDist) > 0.2f)
				{
					if (maxEdgeIndex >= 0)
					{
						const RopeColEdge& edge = m_colCache.m_pEdges[ePoint.m_edgeIndices[maxEdgeIndex]];
						AddColEdgePlaneToConstraint(ii, edge);
						numConstrAdded++;
					}
				}
				else*/ if (minEdgeIndex >= 0 && maxEdgeIndex >= 0)
				{
					const RopeColEdge& minEdge = m_colCache.m_pEdges[ePoint.m_edgeIndices[minEdgeIndex]];
					const RopeColEdge& maxEdge = m_colCache.m_pEdges[ePoint.m_edgeIndices[maxEdgeIndex]];
					AddColEdgeToConstraint(ii, minEdge, maxEdge);
					numConstrAdded += 2;
				}

				bool movePrev = prevDist < nextDist;
				if (movePrev || iClosestNext == iClosestPrev)
				{
					iClosestPrev--;
					prevDist = m_pRopeDist[ii] - m_pEdges[iClosestPrev].m_ropeDist;
				}
				if (!movePrev)
				{
					iClosestNext++;
					nextDist = m_pEdges[iClosestNext].m_ropeDist - m_pRopeDist[ii];
				}

				if (iClosestPrev == iSegFirstEdge)
					prevDist = FLT_MAX;
				if (iClosestNext == iSegLastEdge)
					nextDist = FLT_MAX;
			}
		}
	}
}

void Rope2::AddEdgesToConstraints()
{
	U32F prevKey = 0;
	ASSERT(m_pEdges[0].m_flags & kNodeKeyframed);
	for (U32F ii = 1; ii<m_numEdges; ii++)
	{
		if (m_pEdges[ii].m_flags & kNodeKeyframed)
		{
			if (ii-prevKey > 1 && (m_pEdges[ii].m_flags & kNodeStrained) == 0)
			{
				AddEdgesToConstraints(prevKey, ii);
			}
			prevKey = ii;
		}
	}
}

U32F Rope2::StoreEdgeIds(RopeColEdgeId* pEdgeIds, const EdgePoint* pEdges, U32F numEdges)
{
	U32F iIdEdges = 0;
	for (U32F iEdgePoint = 0; iEdgePoint<numEdges; iEdgePoint++)
	{
		const EdgePoint& ePoint = pEdges[iEdgePoint];
		U32F iIdEdgesStart = iIdEdges;
		for (U32F iEdge = 0; iEdge<ePoint.m_numEdges; iEdge++)
		{
			m_colCache.GetEdgeId(ePoint.m_edgeIndices[iEdge], pEdgeIds[iIdEdges]);
			iIdEdges++;
		}
	}
	return iIdEdges;
}

void Rope2::RestoreEdgeIndices(RopeColEdgeId* pEdgeIds, EdgePoint* pEdges, U32F numEdges)
{
	PROFILE(Rope, RestoreEdgeIndices);

	// Restore edge indices based on ids
	U32F iIdEdges = 0;
	for (U32F iEdgePoint = 0; iEdgePoint<numEdges; iEdgePoint++)
	{
		EdgePoint& ePoint = pEdges[iEdgePoint];
		for (U32F iEdge = 0; iEdge<ePoint.m_numEdges; iEdge++)
		{
			Point pos1;
			I16 edgeIndex = m_colCache.FindEdgeIndex(pEdgeIds[iIdEdges], ePoint.m_pos, pos1, this);
			if (edgeIndex >= 0 && m_colCache.m_pEdges[edgeIndex].m_numExtraTrims > 0)
			{
				Point posOut;
				edgeIndex = m_colCache.FitPointToTrimmedEdge(edgeIndex, pos1, pEdgeIds[iIdEdges].m_startTriId, pEdgeIds[iIdEdges].m_endTriId, posOut);
			}
			ePoint.m_edgeIndices[iEdge] = edgeIndex >= 0 ? edgeIndex : 0xffff;
			iIdEdges++;
		}
	}

	// Remove invalid edges 0xffff
	for (U32F iEdgePoint = 0; iEdgePoint<numEdges; iEdgePoint++)
	{
		EdgePoint& ePoint = pEdges[iEdgePoint];
		U32 numEdgesValid = 0;
		U32F numEdgesNew = 0;
		for (U32F iEdge = 0; iEdge<ePoint.m_numEdges; iEdge++)
		{
			if (ePoint.m_edgeIndices[iEdge] != 0xffff)
			{
				ePoint.m_edgeIndices[numEdgesValid] = ePoint.m_edgeIndices[iEdge];
				ePoint.SetEdgePositive(numEdgesValid, ePoint.GetEdgePositive(iEdge));
				ePoint.SetEdgeActive(numEdgesValid, ePoint.GetEdgeActive(iEdge));
				numEdgesValid++;
				if (iEdge >= ePoint.m_numEdges - ePoint.m_numNewEdges)
					numEdgesNew++;
			}
		}
		ePoint.m_numEdges = numEdgesValid;
		ePoint.m_numNewEdges = numEdgesNew;
		ePoint.m_activeEdges &= (1U << numEdgesValid) - 1;
		ePoint.m_edgePositive &= (1U << numEdgesValid) - 1;
	}
}

void Rope2::PrepareClosestKeyData()
{
	// Fill in helper buffers that hold the closest prev and next key point
	I32F iPrevKey = -1;
	I32F iKey = 0;
	I32F iPrevKeyNode = -1;
	for (U32F ii = 0; ii<m_numPoints; ii++)
	{
		if (m_pNodeFlags[ii] & kNodeKeyframed)
		{
			while (m_pKeyRopeDist[iKey] < m_pRopeDist[ii])
			{
				iKey++;
				ASSERT(iKey < m_numKeyPoints);
			}
			m_pPrevKeyIndex[ii] = iKey;
			m_pNextKeyIndex[ii] = iKey;
			for (U32F jj = iPrevKeyNode+1; jj<ii; jj++)
			{
				m_pNextKeyIndex[jj] = iKey;
			}
			iPrevKeyNode = ii;
			iPrevKey = iKey;
		}
		else
		{
			m_pPrevKeyIndex[ii] = iPrevKey;
		}
	}
	for (U32F jj = iPrevKeyNode+1; jj<m_numPoints; jj++)
	{
		m_pNextKeyIndex[jj] = -1;
	}
}

VF32 Rope2::CalcConstraintHash()const
{
	PROFILE(Rope, Rope_GetHash);
	VF32 hash = SMATH_VEC_SET_ZERO();
	for(U32F i = 0; i < m_numPoints; ++i)
	{
		const VF32 *__restrict p = (const VF32*)(m_pConstr[i].m_planes);
		for (U32F iPlane = 0; iPlane < Constraint::kMaxNumPlanes; iPlane++)
		{
			hash = SMATH_VEC_XOR(SMATH_VEC_ROL(hash,1), p[iPlane]);
		}
	}
	return hash;
}

void Rope2::WakeUp()
{
	m_bSleeping = false;
	m_numFramesCreep = 0;
}

void Rope2::GoToSleep()
{
	m_bSleeping = true;
	memcpy(m_pLastPos, m_pPos, m_numPoints*sizeof(m_pPos[0]));
	memset(m_pVel, 0, m_numPoints*sizeof(m_pVel[0]));
	memset(m_pLastVel, 0, m_numPoints*sizeof(m_pLastVel[0]));
}

void Rope2::UpdateAabb()
{
	//if (m_bNeverStrained)
	//	return;

	PROFILE(Rope, Rope_UpdateAabbStrained);
	CheckStepNotRunning();
	CalcAabb(m_aabb);
}

void Rope2::CalcAabb(Aabb& aabb) const
{
	Point ptMin = m_pPos[0];
	Point ptMax = m_pPos[0];

	if (m_pSaveEdges && m_saveEdgePosSet)
	{
		ptMin = m_saveEdgePos;
		ptMax = m_saveEdgePos;
	}

	if (m_pSaveEdges && m_saveStartEdgePosSet)
	{
		ptMin = Min(ptMin, m_saveStartEdgePos);
		ptMax = Max(ptMax, m_saveStartEdgePos);
	}

	for (U32F i = 0; i < m_numEdges; ++i)
	{
		Point ptPos = m_pEdges[i].m_pos;
		ptMin = Min(ptMin, ptPos);
		ptMax = Max(ptMax, ptPos);
	}

	for (U32F i = 0; i < m_numSaveEdges; ++i)
	{
		Point ptPos = m_pSaveEdges[i].m_pos;
		ptMin = Min(ptMin, ptPos);
		ptMax = Max(ptMax, ptPos);
	}

	for (U32F i = 0; i < m_numExternSaveEdges; ++i)
	{
		Point ptPos = m_pExternSaveEdges[i].m_pos;
		ptMin = Min(ptMin, ptPos);
		ptMax = Max(ptMax, ptPos);
	}

	for (U32F i = 0; i < m_numKeyPoints; ++i)
	{
		Point ptPos = m_pKeyPos[i];
		ptMin = Min(ptMin, ptPos);
		ptMax = Max(ptMax, ptPos);
	}

	// We do this so that we can use the col cache also for slacky collision
	for (U32F i = 0; i < m_numPoints; ++i)
	{
		Point ptPos = m_pPos[i];
		ptMin = Min(ptMin, ptPos);
		ptMax = Max(ptMax, ptPos);
	}

	CalcAabbFromPoints(aabb, ptMin, ptMax);

	aabb.Expand(m_fAabbExpansion); // @@ this should cover some extra space plus slowly moving objects for now. Plus some movement for slacky rope.
}

void Rope2::CalcAabbInSpace(const Locator& loc, Aabb& aabb) const
{
	Locator locInv = Inverse(loc);

	Point ptMin = locInv.TransformPoint(m_pPos[0]);
	Point ptMax = ptMin;

	if (m_pSaveEdges && m_saveEdgePosSet)
	{
		ptMin = locInv.TransformPoint(m_saveEdgePos);
		ptMax = ptMin;
	}

	if (m_pSaveEdges && m_saveStartEdgePosSet)
	{
		Point ptPos = locInv.TransformPoint(m_saveStartEdgePos);
		ptMin = Min(ptMin, ptPos);
		ptMax = Max(ptMax, ptPos);
	}

	for (U32F i = 0; i < m_numEdges; ++i)
	{
		Point ptPos = locInv.TransformPoint(m_pEdges[i].m_pos);
		ptMin = Min(ptMin, ptPos);
		ptMax = Max(ptMax, ptPos);
	}

	for (U32F i = 0; i < m_numSaveEdges; ++i)
	{
		Point ptPos = locInv.TransformPoint(m_pSaveEdges[i].m_pos);
		ptMin = Min(ptMin, ptPos);
		ptMax = Max(ptMax, ptPos);
	}

	for (U32F i = 0; i < m_numExternSaveEdges; ++i)
	{
		Point ptPos = locInv.TransformPoint(m_pExternSaveEdges[i].m_pos);
		ptMin = Min(ptMin, ptPos);
		ptMax = Max(ptMax, ptPos);
	}

	for (U32F i = 0; i < m_numKeyPoints; ++i)
	{
		Point ptPos = locInv.TransformPoint(m_pKeyPos[i]);
		ptMin = Min(ptMin, ptPos);
		ptMax = Max(ptMax, ptPos);
	}

	// We do this so that we can use the col cache also for slacky collision
	for (U32F i = 0; i < m_numPoints; ++i)
	{
		Point ptPos = locInv.TransformPoint(m_pPos[i]);
		ptMin = Min(ptMin, ptPos);
		ptMax = Max(ptMax, ptPos);
	}

	CalcAabbFromPoints(aabb, ptMin, ptMax);

	aabb.Expand(m_fAabbExpansion); // @@ this should cover some extra space plus slowly moving objects for now
}

void EnlargeAabbToTouchSphere(Aabb& aabb, const Point& p, const Scalar& r)
{
	if (r == Scalar(FLT_MAX))
	{
		return;
	}
	if (r == Scalar(kZero))
	{
		aabb.IncludePoint(p);
		return;
	}
	Point pOnAabb = p;
	aabb.ClipPoint(pOnAabb);
	Vector v = p - pOnAabb;
	Scalar dist;
	Vector dir = Normalize(v, dist);
	if (dist > r)
	{
		Point pOnSphere = p - r * dir;
		aabb.IncludePoint(pOnSphere);
	}
}

void Rope2::GetDynamicNodeCollideAabb(U32 ii, Aabb& aabb)
{
	Point ptPos = m_pPos[ii];
	aabb.m_min = ptPos;
	aabb.m_max = ptPos;
	if (ii > 0)
		aabb.IncludePoint(m_pPos[ii-1]);
	if (ii < m_numPoints-1)
		aabb.IncludePoint(m_pPos[ii+1]);
	aabb.IncludePoint(m_pLastPos[ii]);
	//Point ptNextPos = ptPos + m_pVel[ii] * m_scStepTime;
	//aabb.IncludePoint(ptNextPos);
	aabb.Expand(m_fSegmentLength + m_pRadius[ii]);

	if (m_pDistConstraints)
	{
		EnlargeAabbToTouchSphere(aabb, Point(m_pDistConstraints[ii]), m_pDistConstraints[ii].W());
	}

	if (m_bAllowKeyStretchConstraints)
	{
		return;
	}

	I32F iPrevKey = m_pPrevKeyIndex[ii];
	if (iPrevKey >= 0)
	{
		EnlargeAabbToTouchSphere(aabb, m_pKeyPos[iPrevKey], Scalar(m_pRopeDist[ii] - m_pKeyRopeDist[iPrevKey]));
	}

	I32F iNextKey = m_pNextKeyIndex[ii];
	if (iNextKey >= 0)
	{
		EnlargeAabbToTouchSphere(aabb, m_pKeyPos[iNextKey], Scalar(m_pKeyRopeDist[iNextKey] - m_pRopeDist[ii]));
	}
}

void Rope2::CalcAabbFromPoints(Aabb& aabb, Point_arg ptMin, Point_arg ptMax) const
{
	aabb.SetEmpty();
	aabb.IncludePoint(ptMin);
	aabb.IncludePoint(ptMax);
	aabb.Expand(Scalar(m_fMaxRadius + 1e-3f));
}

void Rope2::GetEdgeInfo(U32F iEdge, EdgePointInfo& info) const
{
	CheckStepNotRunning();
	PHYSICS_ASSERT(iEdge < m_numEdges);
	EdgePoint& ePoint = m_pEdges[iEdge];
	info.m_numEdges = ePoint.m_numEdges;
	for (U32F ii = 0; ii<ePoint.m_numEdges; ii++)
	{
		EdgeInfo& eInfo = info.m_edges[ii];
		RopeColEdge& colEdge = m_colCache.m_pEdges[ePoint.m_edgeIndices[ii]];
		U16 triIndex = colEdge.m_triIndex;
		RopeColTri& colTri = m_colCache.m_pTris[triIndex];
		RopeColliderHandle& colShape = m_colCache.m_pShapes[colTri.m_shapeIndex];

		eInfo.m_pnt = colEdge.m_pnt;
		eInfo.m_vec = Vector(colEdge.m_vec);
		eInfo.m_length = colEdge.m_vec.W();
		eInfo.m_normal = colEdge.m_normal;
		eInfo.m_positive = ePoint.GetEdgePositive(ii);
		if (colShape.GetListIndex() >= 0)
		{
			ALWAYS_ASSERT(!colTri.m_key.isValid()); // a mesh inside a list shape? Yuck!
			eInfo.m_shapeKey = hknpShapeKey(colShape.GetListIndex());
		}
		else
		{
			eInfo.m_shapeKey = colTri.m_key;
		}
		eInfo.m_hRigidBody = colShape.GetRigidBody();
	}
}

I32 Rope2::GetEdgeIndexByFlags(RopeNodeFlags flags) const
{
	CheckStepNotRunning();

	for (U32F ii = 0; ii < m_numEdges; ii++)
	{
		const EdgePoint& ePoint = m_pEdges[ii];
		if ((ePoint.m_flags & flags) != 0)
			return ii;
	}

	return -1;
}

void Rope2::SetNumEdges(U32F numEdges)
{
	CheckStepNotRunning();
	PHYSICS_ASSERT(numEdges < m_maxNumPoints);
	m_numEdges = numEdges;
}

I32F Rope2::SetEdgePoint(F32 ropeDist, Point_arg pos, RopeNodeFlags flags)
{
	CheckStepNotRunning();

	EdgePoint* pEdges = m_pEdges;
	U32F& numEdges = m_numEdges;

	U32F iEdge = 0;
	while (iEdge < numEdges && pEdges[iEdge].m_ropeDist < ropeDist - 0.01f)
	{
		iEdge++;
	}
	if (iEdge < numEdges)
	{
		if (pEdges[iEdge].m_ropeDist > ropeDist + 0.01f)
		{
			ALWAYS_ASSERT(numEdges + 1 < m_maxNumPoints);
			memmove(pEdges + iEdge + 1, pEdges + iEdge, (numEdges - iEdge) * sizeof(pEdges[0]));
			numEdges++;
		}
		else
		{
			if (pEdges[iEdge].m_flags & kNodeKeyframed)
			{
				// Dont override keyframed edge
				return -1;
			}
		}
	}
	else
	{
		ALWAYS_ASSERT(numEdges + 1 < m_maxNumPoints);
		numEdges++;
	}
	pEdges[iEdge].Reset();
	pEdges[iEdge].m_pos = pos;
	pEdges[iEdge].m_ropeDist = ropeDist;
	pEdges[iEdge].m_flags = flags;
	return iEdge;
}

Scalar Rope2::GetKineticEnergy()
{
	Vector vecComponentSum(SMath::kZero);
	for(U32F i = 0; i < m_numPoints; ++i)
		vecComponentSum += m_pVel[i]*m_pVel[i];
	return SumComponentsXYZ0(vecComponentSum);
}

Scalar Rope2::GetPotentialEnergy()
{
	Vector vecGravity;
	HavokGetGravity(vecGravity);
	SMath::Vec4 vecSum(SMath::kZero);
	for(U32F i = 0; i < m_numPoints; ++i)
		vecSum -= vecGravity.GetVec4() * m_pPos[i].GetVec4(); // mgh, with m = 1
	return vecSum.Y();
}

Scalar Rope2::GetMaxVel()
{
	Vector vecComponentMax(SMath::kZero);
	for(U32F i = 0; i < m_numPoints; ++i)
		vecComponentMax = Max(vecComponentMax, Abs(m_pVel[i]));
	return SumComponentsXYZ0(vecComponentMax);
}

Scalar Rope2::GetMaxKeyframedVel()
{
	Vector vecComponentMax(SMath::kZero);
	Scalar scInvDt(0.033f);
	for(U32F i = 0; i < m_numKeyPoints; ++i)
	{
		vecComponentMax = Max(vecComponentMax, Abs(m_pKeyVel[i]));
		vecComponentMax = Max(vecComponentMax, Abs(m_pKeyPos[i] - GetPos(m_pKeyRopeDist[i]))*scInvDt);
	}
	return SumComponentsXYZ0(vecComponentMax);
}

void Rope2::DebugDraw()
{
#if FINAL_BUILD && !ALLOW_ROPE_DEBUGGER_IN_FINAL
	return;
#endif

	DebugPrimTime::NoRecordJanitor jNoRecord(m_pDebugger);

	DebugPrimTime tt = kPrimDuration1Frame; //kPrimDuration1FramePauseable

	//g_prim.Draw(DebugBox(m_aabb.m_min, m_aabb.m_max, kColorWhite, kPrimEnableWireframe), tt);
	//g_prim.Draw(DebugBox(m_aabbStrained.m_min, m_aabbStrained.m_max, kColorRed, kPrimEnableWireframe), tt);

	Point* pPos = m_pPos;
	Constraint* pConstr = m_pConstr;
	if (m_pDebugPreSlidePos && g_ropeMgr.m_debugDrawSlide)
	{
		pPos = m_pDebugPreSlidePos;
		pConstr = m_pDebugPreSlideCon;
	}

	if (g_ropeMgr.m_debugDrawNodes)
	{
		F32 realDist = 0.0f;
		for(U32F ii = 0; ii < m_numPoints; ++ii)
		{
			if (!g_ropeMgr.m_debugDrawSelectedIndex || ii == g_ropeMgr.m_selectedRopeIndex)
			{
				Color color = m_bSleeping ? kColorGray : kColorWhite;
				if (IsNodeKeyframedInt(ii))
					color = kColorYellow;
				else if (IsNodeEdgeInt(ii))
					color = kColorRed;
				else if (IsNodeStrainedInt(ii))
					color = kColorRed;
				if (ii > 0 && !g_ropeMgr.m_debugDrawSelectedIndex && !g_ropeMgr.m_debugDrawTension)
					g_prim.Draw(DebugLine(pPos[ii-1], pPos[ii], m_bSleeping ? kColorGray : kColorWhite, 1.0f), tt);
				g_prim.Draw(DebugSphere(pPos[ii], m_pRadius[ii], color), tt);
				if (g_ropeMgr.m_debugDrawRopeDist)
				{
					char jointLabel[64];
					if (g_ropeMgr.m_debugDrawRopeRealDist)
						snprintf(jointLabel, 63, "%.2f/%.2f", m_pRopeDist[ii], realDist);
					else
						snprintf(jointLabel, 63, "%.2f", m_pRopeDist[ii]);
					jointLabel[63] = '\0';
					g_prim.Draw( DebugString(pPos[ii], jointLabel, color, 0.4f), tt);
				}
				else if (g_ropeMgr.m_debugDrawRopeRealDist)
				{
					char jointLabel[64];
					snprintf(jointLabel, 63, "%.2f", realDist);
					jointLabel[63] = '\0';
					g_prim.Draw( DebugString(pPos[ii], jointLabel, color, 0.4f), tt);
				}
				else if (g_ropeMgr.m_debugDrawJointIndices)
				{
					char jointLabel[64];
					snprintf(jointLabel, 63, "%d", (I32)ii);
					jointLabel[63] = '\0';
					g_prim.Draw( DebugString(pPos[ii], jointLabel, color, 0.4f), tt);
				}
				else if (g_ropeMgr.m_debugDrawMass)
				{
					char massStr[64];
					if (m_pInvRelMass[ii] == 1.0f)
						snprintf(massStr, 63, "1");
					else
						snprintf(massStr, 63, "%.3f", m_pInvRelMass[ii]);
					massStr[63] = '\0';
					g_prim.Draw( DebugString(pPos[ii], massStr, color, 0.4f), tt);
				}
			}
			if (ii < m_numPoints-1)
			{
				realDist += Dist(pPos[ii], pPos[ii+1]);
			}
		}
	}

	if (g_ropeMgr.m_fDebugDrawLastPos)
	{
		for(U32F ii = 0; ii < m_numPoints; ++ii)
		{
			if (!g_ropeMgr.m_debugDrawSelectedIndex || ii == g_ropeMgr.m_selectedRopeIndex)
			{
				g_prim.Draw(DebugLine(m_pLastPos[ii], pPos[ii], kColorGray), tt);
				g_prim.Draw(DebugSphere(m_pLastPos[ii], 0.5f*m_pRadius[ii], kColorGray), tt);
			}
		}
	}

	if (g_ropeMgr.m_debugDrawNodeAabb)
	{
		for(U32F ii = 0; ii < m_numPoints; ++ii)
		{
			if (!g_ropeMgr.m_debugDrawSelectedIndex || ii == g_ropeMgr.m_selectedRopeIndex)
			{
				g_prim.Draw(DebugBox(m_pNodeAabb[ii], kColorWhite, kPrimEnableWireframe), tt);
			}
		}
	}

	if (g_ropeMgr.m_fDebugDrawVeloScale > 0.0f)
	{
		for(U32F ii = 0; ii < m_numPoints; ++ii)
		{
			if (!g_ropeMgr.m_debugDrawSelectedIndex || ii == g_ropeMgr.m_selectedRopeIndex)
			{
				g_prim.Draw(DebugLine(pPos[ii], pPos[ii] + g_ropeMgr.m_fDebugDrawVeloScale * m_pVel[ii], kColorRed, 1.0f), tt);
			}
		}
	}

	if (g_ropeMgr.m_debugDrawSolver)
	{
		for (U32 ii = 0; ii < m_numPoints; ii++)
		{
			if (!g_ropeMgr.m_debugDrawSelectedIndex || ii == g_ropeMgr.m_selectedRopeIndex)
			{
				for (I32 jj = 0; jj < pConstr[ii].m_numEdges; jj++)
				{
					U32 iPl0 = pConstr[ii].m_edgePlanes[jj*2];
					U32 iPl1 = pConstr[ii].m_edgePlanes[jj*2+1];

					U8 planeMask;
					Vec4 edgePlane = pConstr[ii].GetEdgePlane(jj, pPos[ii], planeMask);

					Color c = (pConstr[ii].m_flags & planeMask) == planeMask ? kColorOrange : kColorGray;

					if (planeMask == ((Rope2::Constraint::kIsWorking1 << iPl0) | (Rope2::Constraint::kIsWorking1 << iPl1)))
					{
						Vector norm(pConstr[ii].m_planes[iPl0]);
						Vector biNorm(pConstr[ii].m_biPlanes[jj*2]);
						Vector triNorm = Cross(norm, biNorm);

						Vec4 pt(pPos[ii].GetVec4());
						Scalar scHeight(Dot4(pt, pConstr[ii].m_planes[iPl0]));
						Scalar scBiHeight(Dot4(pt, pConstr[ii].m_biPlanes[jj*2]));
						Point ptOnEdge = pPos[ii] - scHeight * norm - scBiHeight * biNorm;
						Point ptOnPlane = ptOnEdge - kDebugPlaneSize * biNorm;

						g_prim.Draw(DebugLine(ptOnEdge, ptOnEdge + 0.15f * Vector(edgePlane), c), tt);
						g_prim.Draw(DebugLine(ptOnPlane + kDebugPlaneSize * biNorm - kDebugPlaneSize * triNorm, ptOnPlane + kDebugPlaneSize * biNorm + kDebugPlaneSize * triNorm, kColorBlue), tt);
						g_prim.Draw(DebugLine(ptOnPlane + kDebugPlaneSize * biNorm + kDebugPlaneSize * triNorm, ptOnPlane - kDebugPlaneSize * biNorm + kDebugPlaneSize * triNorm, kColorWhite), tt);
						g_prim.Draw(DebugLine(ptOnPlane - kDebugPlaneSize * biNorm + kDebugPlaneSize * triNorm, ptOnPlane - kDebugPlaneSize * biNorm - kDebugPlaneSize * triNorm, kColorWhite), tt);
						g_prim.Draw(DebugLine(ptOnPlane - kDebugPlaneSize * biNorm - kDebugPlaneSize * triNorm, ptOnPlane + kDebugPlaneSize * biNorm - kDebugPlaneSize * triNorm, kColorWhite), tt);

						Vector norm1(pConstr[ii].m_planes[iPl1]);
						Vector biNorm1(pConstr[ii].m_biPlanes[jj*2+1]);
						Vector triNorm1 = Cross(norm1, biNorm1);
						Point ptOnPlane1 = ptOnEdge - kDebugPlaneSize * biNorm1;

						g_prim.Draw(DebugLine(ptOnPlane1 + kDebugPlaneSize * biNorm1 - kDebugPlaneSize * triNorm1, ptOnPlane1 + kDebugPlaneSize * biNorm1 + kDebugPlaneSize * triNorm1, kColorBlue), tt);
						g_prim.Draw(DebugLine(ptOnPlane1 + kDebugPlaneSize * biNorm1 + kDebugPlaneSize * triNorm1, ptOnPlane1 - kDebugPlaneSize * biNorm1 + kDebugPlaneSize * triNorm1, kColorWhite), tt);
						g_prim.Draw(DebugLine(ptOnPlane1 - kDebugPlaneSize * biNorm1 + kDebugPlaneSize * triNorm1, ptOnPlane1 - kDebugPlaneSize * biNorm1 - kDebugPlaneSize * triNorm1, kColorWhite), tt);
						g_prim.Draw(DebugLine(ptOnPlane1 - kDebugPlaneSize * biNorm1 - kDebugPlaneSize * triNorm1, ptOnPlane1 + kDebugPlaneSize * biNorm1 - kDebugPlaneSize * triNorm1, kColorWhite), tt);
					}
					else if (planeMask == (Rope2::Constraint::kIsWorking1 << iPl0))
					{
						DebugDrawPlane(pConstr[ii].m_planes[iPl0], pPos[ii], kColorWhite, kDebugPlaneSize, c, 0.15f, PrimAttrib(), tt);
					}
					else
					{
						DebugDrawPlane(pConstr[ii].m_planes[iPl1], pPos[ii], kColorWhite, kDebugPlaneSize, c, 0.15f, PrimAttrib(), tt);
					}
				}
				for (I32 jj = pConstr[ii].m_firstNoEdgePlane; jj < pConstr[ii].m_numPlanes; jj++)
				{
					Color c = ((pConstr[ii].m_flags >> jj) & 1) ? kColorOrange : kColorGray;
					DebugDrawPlane(pConstr[ii].m_planes[jj], pPos[ii], kColorWhite, kDebugPlaneSize, c, 0.15f, PrimAttrib(), tt);
				}

				//DebugDrawSphere(m_pPos[ii], 0.01f, kColorBlue, tt);
			}
		}
	}

	if (g_ropeMgr.m_debugDrawConstraints && !g_ropeMgr.m_debugDrawSolver)
	{
		for(U32F ii = 0; ii < m_numPoints; ++ii)
		{
			if (!g_ropeMgr.m_debugDrawSelectedIndex || ii == g_ropeMgr.m_selectedRopeIndex)
			{
				for (I32 jj = 0; jj < pConstr[ii].m_numEdges; jj++)
				{
					U32 iPl0 = pConstr[ii].m_edgePlanes[jj*2];
					U32 iPl1 = pConstr[ii].m_edgePlanes[jj*2+1];

					Vector norm(pConstr[ii].m_planes[iPl0]);
					Vector biNorm(pConstr[ii].m_biPlanes[jj*2]);
					Vector triNorm = Cross(norm, biNorm);

					Vec4 pt(pPos[ii].GetVec4());
					Scalar scHeight(Dot4(pt, pConstr[ii].m_planes[iPl0]));
					Scalar scBiHeight(Dot4(pt, pConstr[ii].m_biPlanes[jj*2]));
					Point ptOnEdge = pPos[ii] - scHeight * norm - scBiHeight * biNorm;
					Point ptOnPlane = ptOnEdge - kDebugPlaneSize * biNorm;

					g_prim.Draw(DebugLine(ptOnEdge, ptOnEdge + 0.15f * norm, kColorOrange), tt);
					g_prim.Draw(DebugLine(ptOnPlane + kDebugPlaneSize * biNorm - kDebugPlaneSize * triNorm, ptOnPlane + kDebugPlaneSize * biNorm + kDebugPlaneSize * triNorm, kColorBlue), tt);
					g_prim.Draw(DebugLine(ptOnPlane + kDebugPlaneSize * biNorm + kDebugPlaneSize * triNorm, ptOnPlane - kDebugPlaneSize * biNorm + kDebugPlaneSize * triNorm, kColorWhite), tt);
					g_prim.Draw(DebugLine(ptOnPlane - kDebugPlaneSize * biNorm + kDebugPlaneSize * triNorm, ptOnPlane - kDebugPlaneSize * biNorm - kDebugPlaneSize * triNorm, kColorWhite), tt);
					g_prim.Draw(DebugLine(ptOnPlane - kDebugPlaneSize * biNorm - kDebugPlaneSize * triNorm, ptOnPlane + kDebugPlaneSize * biNorm - kDebugPlaneSize * triNorm, kColorWhite), tt);

					Vector norm1(pConstr[ii].m_planes[iPl1]);
					Vector biNorm1(pConstr[ii].m_biPlanes[jj*2+1]);
					Vector triNorm1 = Cross(norm1, biNorm1);
					Point ptOnPlane1 = ptOnEdge - kDebugPlaneSize * biNorm1;

					g_prim.Draw(DebugLine(ptOnEdge, ptOnEdge + 0.15f * norm1, kColorOrange), tt);
					g_prim.Draw(DebugLine(ptOnPlane1 + kDebugPlaneSize * biNorm1 - kDebugPlaneSize * triNorm1, ptOnPlane1 + kDebugPlaneSize * biNorm1 + kDebugPlaneSize * triNorm1, kColorBlue), tt);
					g_prim.Draw(DebugLine(ptOnPlane1 + kDebugPlaneSize * biNorm1 + kDebugPlaneSize * triNorm1, ptOnPlane1 - kDebugPlaneSize * biNorm1 + kDebugPlaneSize * triNorm1, kColorWhite), tt);
					g_prim.Draw(DebugLine(ptOnPlane1 - kDebugPlaneSize * biNorm1 + kDebugPlaneSize * triNorm1, ptOnPlane1 - kDebugPlaneSize * biNorm1 - kDebugPlaneSize * triNorm1, kColorWhite), tt);
					g_prim.Draw(DebugLine(ptOnPlane1 - kDebugPlaneSize * biNorm1 - kDebugPlaneSize * triNorm1, ptOnPlane1 + kDebugPlaneSize * biNorm1 - kDebugPlaneSize * triNorm1, kColorWhite), tt);
				}
				for (I32 jj = pConstr[ii].m_firstNoEdgePlane; jj < pConstr[ii].m_numPlanes; jj++)
				{
					DebugDrawPlane(pConstr[ii].m_planes[jj], pPos[ii], kColorWhite, kDebugPlaneSize, kColorOrange, 0.15f, PrimAttrib(), tt);
				}
			}
		}
	}

	if (g_ropeMgr.m_debugDrawDistConstraints && m_pDistConstraints)
	{
		for(U32F ii = 0; ii < m_numPoints; ++ii)
		{
			if (!g_ropeMgr.m_debugDrawSelectedIndex || ii == g_ropeMgr.m_selectedRopeIndex)
			{
				if ((F32)m_pDistConstraints[ii].W() != FLT_MAX)
				{
					DebugDrawCross(Point(m_pDistConstraints[ii]), 0.03f, kColorRed, tt);
					DebugDrawSphere(Point(m_pDistConstraints[ii]), m_pDistConstraints[ii].W(), kColorRed, tt);
				}
			}
		}
	}

	if (g_ropeMgr.m_debugDrawTension && m_pDebugTension)
	{
		Color color(1.0f, 1.0f-m_pDebugTension[0]/300.0f, 1.0f-m_pDebugTension[0]/300.0f);
		for (U32F ii = 1; ii < m_numPoints; ++ii)
		{
			Color color2(1.0f, 1.0f-m_pDebugTension[ii]/300.0f, 1.0f-m_pDebugTension[ii]/300.0f);
			g_prim.Draw(DebugLine(pPos[ii-1], pPos[ii], color, color2), tt);
			color = color2;

			char label[64];
			snprintf(label, 63, "%.0f", m_pDebugTension[ii]);
			label[63] = '\0';
			g_prim.Draw( DebugString(pPos[ii], label, color, 0.4f), tt);

			if (m_pDebugTensionBreak[ii])
			{
				DebugDrawSphere(pPos[ii], m_pRadius[ii]+0.01f, kColorPurple);
			}
		}
	}

	if (g_ropeMgr.m_debugDrawFriction && m_pTensionFriction)
	{
		for (U32F ii = 0; ii < m_numPoints; ++ii)
		{
			char label[64];
			snprintf(label, 63, "%.2f", m_pTensionFriction[ii]);
			label[63] = '\0';
			g_prim.Draw( DebugString(pPos[ii], label, kColorWhite, 0.4f), tt);

			if (m_pTensionFriction[ii] != 0.0f)
				DebugDrawSphere(Point(m_pFrictionConstraints[ii]), 0.01f, kColorYellow, tt);
		}
	}

	if (g_ropeMgr.m_debugDrawTwist && m_pTwistDir)
	{
		for (U32F ii = 0; ii < m_numPoints; ++ii)
		{
			if (!g_ropeMgr.m_debugDrawSelectedIndex || ii == g_ropeMgr.m_selectedRopeIndex)
			{
				DebugDrawLine(m_pPos[ii], m_pPos[ii]+0.1f*m_pTwistDir[ii], (m_pNodeFlags[ii] & kNodeUserTwist) ? kColorYellow : kColorWhite);
			}
		}
	}

	if (g_ropeMgr.m_debugDrawEdges)
	{
		F32 dist = 0.0f;
		for (U32F ii = 0; ii<m_numEdges; ii++)
		{
			const EdgePoint& currEdge = m_pEdges[ii];
			if (ii > 0)
			{
				const EdgePoint& prevEdge = m_pEdges[ii-1];
				DebugDrawLine(prevEdge.m_pos, currEdge.m_pos, (currEdge.m_flags & (kNodeStrained|kNodeEdgeDetection|kNodeUseSavePos)) ? kColorRed : kColorGray, tt);
				dist += Dist(prevEdge.m_pos, currEdge.m_pos);
			}

			Color color = kColorRed;
			if (currEdge.m_flags & kNodeKeyframed)
				color = kColorYellow;
			DebugDrawSphere(currEdge.m_pos, 0.015f, color, tt);
			if (!g_ropeMgr.m_debugDrawSelectedEdge || ii == g_ropeMgr.m_selectedRopeEdge)
			{
				if (g_ropeMgr.m_debugDrawSelectedEdge)
					DebugDrawCross(currEdge.m_pos, 0.07f, kColorRed, tt);
				// FIXME. Length check? Really?
				if (m_fLength >= g_ropeMgr.m_debugEdgeLengthThreshold)
				{
					StringBuilder<256> strBld("e%d", ii);
					if (g_ropeMgr.m_debugDrawEdgeDist)
					{
						strBld.append_format("(%.3f)", dist);
					}
					if (g_ropeMgr.m_debugDrawEdgeRopeDist)
					{
						strBld.append_format("(%.3f)", currEdge.m_ropeDist);
					}
					if (g_ropeMgr.m_debugDrawEdgeFlags)
					{
						char text[256];
						NodeFlagsToString((NodeFlags)currEdge.m_flags, text, sizeof(text));
						strBld.append(text);
					}
					if (strBld.length() > 4)
					{
						g_prim.Draw(DebugString(currEdge.m_pos, strBld.c_str(), kColorWhite, 0.5f));
					}
				}
				if (!g_ropeMgr.m_debugDrawColCacheEdges)
				{
					for (U32F jj = 0; jj<currEdge.m_numEdges; jj++)
					{
						if (g_ropeMgr.m_debugDrawInactiveEdges || currEdge.GetEdgeActive(jj))
							m_colCache.DebugDrawEdge(currEdge.m_edgeIndices[jj], currEdge.GetEdgeActive(jj), currEdge.GetEdgePositive(jj));
					}
				}
				//DebugDrawLine(currEdge.m_pos, currEdge.m_pos + GetEdgePointCollisionNormal(ii) * 0.3f, kColorYellow);
			}
		}
	}

	if (g_ropeMgr.m_debugDrawSaveEdges)
	{
		F32 dist = 0.0f;
		for (U32F ii = 0; ii<m_numSaveEdges; ii++)
		{
			const EdgePoint& currEdge = m_pSaveEdges[ii];
			if (ii > 0)
			{
				const EdgePoint& prevEdge = m_pSaveEdges[ii-1];
				DebugDrawLine(prevEdge.m_pos, currEdge.m_pos, kColorPurple, tt);
				dist += Dist(prevEdge.m_pos, currEdge.m_pos);
			}

			Color color = kColorPurple;
			if (currEdge.m_flags & kNodeKeyframed)
				color = kColorYellow;
			DebugDrawSphere(currEdge.m_pos, 0.015f, color, tt);
			if (!g_ropeMgr.m_debugDrawSelectedEdge || ii == g_ropeMgr.m_selectedRopeEdge)
			{
				if (g_ropeMgr.m_debugDrawSelectedEdge)
					DebugDrawCross(currEdge.m_pos, 0.07f, kColorPurple, tt);
				// FIXME. Length check? Really?
				if (m_fLength >= g_ropeMgr.m_debugEdgeLengthThreshold)
				{
					StringBuilder<256> strBld("e%d", ii);
					if (g_ropeMgr.m_debugDrawEdgeDist)
					{
						strBld.append_format("(%.3f)", dist);
					}
					if (g_ropeMgr.m_debugDrawEdgeRopeDist)
					{
						strBld.append_format("(%.3f)", currEdge.m_ropeDist);
					}
					if (g_ropeMgr.m_debugDrawEdgeFlags)
					{
						char text[256];
						NodeFlagsToString((NodeFlags)currEdge.m_flags, text, sizeof(text));
						strBld.append(text);
					}
					if (strBld.length() > 4)
					{
						g_prim.Draw(DebugString(currEdge.m_pos, strBld.c_str(), kColorWhite, 0.5f));
					}
				}
				if (!g_ropeMgr.m_debugDrawColCacheEdges)
				{
					for (U32F jj = 0; jj<currEdge.m_numEdges; jj++)
					{
						if (g_ropeMgr.m_debugDrawInactiveEdges || currEdge.GetEdgeActive(jj))
							m_colCache.DebugDrawEdge(currEdge.m_edgeIndices[jj], currEdge.GetEdgeActive(jj), currEdge.GetEdgePositive(jj));
					}
				}
			}
		}
	}

	if (g_ropeMgr.m_debugDrawAabb)
	{
		hkAabb havokAabb;
		havokAabb.m_min = hkVector4(m_aabbSlacky.m_min.QuadwordValue());
		havokAabb.m_max = hkVector4(m_aabbSlacky.m_max.QuadwordValue());

		hkTransform I;
		I.setIdentity();

		HavokDebugDrawAabb(havokAabb, I, kColorRed);
	}

	if (g_ropeMgr.m_debugDrawColliders && m_pDebugger)
	{
		m_pDebugger->DebugDrawColliders();
	}

	m_colCache.DebugDraw(this);
}

void Rope2::BackstepVelocities(U32F iStart, U32F iEnd, Scalar dt)
{
	Scalar invDt = 1.0f/dt;
	for(U32F ii = iStart; ii <= iEnd; ii++)
	{
		if (!IsNodeKeyframedInt(ii))
		{
			m_pVel[ii] = (m_pPos[ii] - m_pLastPos[ii]) * invDt;
		}
	}

	CheckStateSane(iStart, iEnd);
}

void Rope2::BackstepVelocitiesDynamicFtl(U32F iStart, U32F iEnd, Scalar dt, Point* preUnstrechtPos)
{
	Scalar invDt = 1.0f/dt;
	for(U32F ii = iStart; ii < iEnd; ii++)
	{
		if (!IsNodeKeyframedInt(ii))
		{
			m_pVel[ii] = (m_pPos[ii] - m_pLastPos[ii]) * invDt - g_dftlDamping * (m_pPos[ii+1] - preUnstrechtPos[ii+1]) * invDt;
		}
	}
	if (!IsNodeKeyframedInt(iEnd))
	{
		m_pVel[iEnd] = (m_pPos[iEnd] - m_pLastPos[iEnd]) * invDt;
	}

	CheckStateSane(iStart, iEnd);
}

void Rope2::RelaxVelExternalConstraints()
{
	HavokMarkForReadJanitor jj; // because we're reading velocities from collider RBs

	Scalar scProporFriction = Min(Scalar(1.0f), m_fProporFriction*m_scStepTime);
	Scalar scConstFriction = m_fConstFriction*m_scStepTime;
	Scalar scProporTensionFrictionDt = g_proporTensionFriction * m_scStepTime;
	Scalar scConstTensionFrictionDt = g_constTensionFriction * m_scStepTime;
	for(I32 ii = m_firstDynamicPoint; ii <= m_lastDynamicPoint; ii++)
	{
		if (IsNodeLooseInt(ii))
		{
			Scalar scNodeProporFriction = scProporFriction;
			Scalar scNodeConstFriction = scConstFriction;
			if (m_pTensionFriction)
			{
				scNodeProporFriction = Min(Scalar(1.0f), scNodeProporFriction + m_pTensionFriction[ii] * scProporTensionFrictionDt);
				scNodeConstFriction += m_pTensionFriction[ii] * scConstTensionFrictionDt;
			}
			m_pConstr[ii].RelaxVel(m_pPos[ii], m_pVel[ii], this, scNodeProporFriction, scNodeConstFriction);
		}
	}

	if (m_pDistConstraints)
	{
		for (I32 ii = m_firstDynamicPoint; ii <= m_lastDynamicPoint; ii++)
		{
			Point nextPos = m_pPos[ii] + m_pVel[ii] * m_scStepTime;
			Point nextP = Point(m_pDistConstraints[ii]) + m_pDistConstraintsVel[ii] * m_scStepTime;
			Vector v = nextPos - nextP;
			Scalar dist;
			v = SafeNormalize(v, kZero, dist);
			nextPos = nextP + Min(dist, m_pDistConstraints[ii].W()) * v;
			m_pVel[ii] = (nextPos - m_pPos[ii]) * m_scInvStepTime;
		}
	}

	CheckStateSane(m_firstDynamicPoint, m_lastDynamicPoint);
}

void Rope2::UpdateKeyframedVelocities()
{
	for(U32F ii = 0; ii < m_numPoints; ii++)
	{
		if (IsNodeKeyframedInt(ii))
		{
			m_pVel[ii] = (m_pPos[ii] - m_pLastPos[ii]) * m_scInvStepTime;
		}
	}

	CheckStateSane(0, m_numPoints);
}

void Rope2::TimestepVelocities()
{
	if (m_lastDynamicPoint < 0 || m_firstDynamicPoint < 0)
		return;

	// Do it for each loose section of rope separately so we can do "local space" damping more correctly
	U32F startNode = m_firstDynamicPoint > 0 ? m_firstDynamicPoint-1 : m_firstDynamicPoint;
	U32F numPoints = m_lastDynamicPoint < m_numPoints-1 ? m_lastDynamicPoint+2 : m_numPoints;
	while (startNode < numPoints-1)
	{
		while (startNode < numPoints && (m_pNodeFlags[startNode] & (kNodeStrained|kNodeKeyframed)) != 0)
			startNode++;
		if (startNode == numPoints)
			break; // end of rope
		if (startNode > 0)
			startNode--;
		U32F endNode = startNode+1;
		while (endNode < numPoints && (m_pNodeFlags[endNode] & (kNodeStrained|kNodeKeyframed)) == 0)
			endNode++;
		if (endNode == numPoints && startNode < numPoints-1)
		{
			endNode = numPoints-1;
		}
		if (endNode > startNode && endNode < numPoints)
		{
			TimestepVelocities(startNode, endNode, m_scStepTime);
		}
		startNode = endNode;
	}
}

void Rope2::TimestepVelocities(U32F iStart, U32F iEnd, Scalar dt)
{
	PROFILE(Rope, Rope_stepVel);

	Scalar scDamping = Min(Scalar(1.0f), m_fDamping*dt);
	Scalar scViscousDamping(m_fViscousDamping * dt);
	Scalar scViscousDampingUnderWater(0.5f * dt);
	Vector vecGravity;
	HavokGetGravity(vecGravity);
	Vector vecIncVel = (vecGravity * m_fGravityFactor + m_vGravityOffset) * dt;
	Scalar scIncVelUnderWater = dt * 0.1f;
	Scalar scOne(1.0f);

	// Find center of mass of this section of rope and it's linear velocity
	Vector distAccu = (m_pPos[iStart] - Point(kZero)) * (m_pRopeDist[iStart+1] - m_pRopeDist[iStart]);
	Vector veloAccu = m_pVel[iStart] * (m_pRopeDist[iStart+1] - m_pRopeDist[iStart]);
	for(U32F ii = iStart+1; ii < iEnd; ii++)
	{
		distAccu += (m_pPos[ii] - Point(kZero)) * (m_pRopeDist[ii+1] - m_pRopeDist[ii-1]);
		veloAccu += m_pVel[ii] * (m_pRopeDist[ii+1] - m_pRopeDist[ii-1]);
	}
	distAccu += (m_pPos[iEnd] - Point(kZero)) * (m_pRopeDist[iEnd] - m_pRopeDist[iEnd-1]);
	veloAccu += m_pVel[iEnd] * (m_pRopeDist[iEnd] - m_pRopeDist[iEnd-1]);

	Scalar doubleLen = (2.0f * (m_pRopeDist[iEnd] - m_pRopeDist[iStart]));
	Point cm = Point(kZero) + distAccu / doubleLen;
	Vector vel = veloAccu / doubleLen;

	// Find the angular velocity of this section of rope
	Vector angInertia(kZero);
	F32 inerTensor[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	for(U32F ii = iStart; ii <= iEnd; ii++)
	{
		Vector r = m_pPos[ii] - cm;
		Scalar m = ii == iStart ? (m_pRopeDist[iStart+1] - m_pRopeDist[iStart]) :
			(ii == iEnd ? (m_pRopeDist[iEnd] - m_pRopeDist[iEnd-1]) :
			(m_pRopeDist[ii+1] - m_pRopeDist[ii-1]));
		angInertia += Cross(r, m_pVel[ii] * m);
		Vector r2 = r*r;
		inerTensor[0] += (r2.Y() + r2.Z()) * m;
		inerTensor[1] += (-r.X()*r.Y()) * m;
		inerTensor[2] += (-r.X()*r.Z()) * m;
		inerTensor[3] += (r2.X() + r2.Z()) * m;
		inerTensor[4] += (-r.Y()*r.Z()) * m;
		inerTensor[5] += (r2.X() + r2.Y()) * m;
	}

	Vector angVel;
	F32 inerTensorInv[6];
	F32 det = inerTensor[0]*inerTensor[3]*inerTensor[5] - inerTensor[0]*inerTensor[4]*inerTensor[4] - inerTensor[3]*inerTensor[2]*inerTensor[2]
		- inerTensor[5]*inerTensor[1]*inerTensor[1] + 2.0f*inerTensor[1]*inerTensor[2]*inerTensor[4];
	if (det > 0.001f)
	{
		F32 detInv = 1.0f/det;
		inerTensorInv[0] = (inerTensor[3]*inerTensor[5]-inerTensor[4]*inerTensor[4]) * detInv;
		inerTensorInv[1] = (inerTensor[2]*inerTensor[4]-inerTensor[1]*inerTensor[5]) * detInv;
		inerTensorInv[2] = (inerTensor[1]*inerTensor[4]-inerTensor[3]*inerTensor[2]) * detInv;
		inerTensorInv[3] = (inerTensor[0]*inerTensor[5]-inerTensor[2]*inerTensor[2]) * detInv;
		inerTensorInv[4] = (inerTensor[1]*inerTensor[2]-inerTensor[0]*inerTensor[4]) * detInv;
		inerTensorInv[5] = (inerTensor[0]*inerTensor[3]-inerTensor[1]*inerTensor[1]) * detInv;

		angVel = Vector(
			inerTensorInv[0] * angInertia.X() + inerTensorInv[1] * angInertia.Y() + inerTensorInv[2] * angInertia.Z(),
			inerTensorInv[1] * angInertia.X() + inerTensorInv[3] * angInertia.Y() + inerTensorInv[4] * angInertia.Z(),
			inerTensorInv[2] * angInertia.X() + inerTensorInv[4] * angInertia.Y() + inerTensorInv[5] * angInertia.Z());
	}
	else
	{
		// Not correct!
		// We should fix it but it's probably not a big deal as this should only happen in rare moments when the rope is all in straight line
		angVel = kZero;
	}

	F32* pWaterY = STACK_ALLOC(F32, iEnd-iStart+1);
	if (m_bBuoyancy)
	{
		HavokRayCastJob rayCast;
		rayCast.Open(iEnd-iStart+1, 1, ICollCastJob::kCollCastSynchronous);
		for (U32F ii = 0; ii<iEnd-iStart+1; ii++)
		{
			rayCast.SetProbeExtents(ii, m_pPos[iStart+ii]-Vector(0.0f, m_fRadius, 0.0f), Vector(kUnitYAxis), 20.0f);
		}
		rayCast.SetFilterForAllProbes(CollideFilter(Collide::kLayerMaskWater));
		rayCast.Kick(FILE_LINE_FUNC);
		rayCast.Wait();
		for (U32F ii = 0; ii < iEnd - iStart + 1; ii++)
		{
			pWaterY[ii] = rayCast.IsContactValid(ii, 0) ? (F32)rayCast.GetContactPoint(ii, 0).Y() : -FLT_MAX;
		}
	}
	else
	{
		for (U32F ii = 0; ii < iEnd - iStart + 1; ii++)
		{
			pWaterY[ii] = -FLT_MAX;
		}
	}

	for(U32F ii = iStart; ii <= iEnd; ii++)
	{
		if (IsNodeLooseInt(ii))
		{
			Vector localVel = m_pVel[ii] - vel - Cross(angVel, m_pPos[ii] - cm);
			m_pVel[ii] -= localVel * scDamping;

			F32 waterMax = pWaterY[ii - iStart] + 0.25f * m_pRadius[ii];
			F32 waterMin = pWaterY[ii - iStart] - 0.25f * m_pRadius[ii];
			bool isUnderWater = m_pPos[ii].Y() <= waterMax;

			if (isUnderWater)
			{
				F32 dvy = LerpScale(waterMin, waterMax, (F32)scIncVelUnderWater, (F32)vecIncVel.Y(), (F32)m_pPos[ii].Y());
				F32 eqy = waterMin + (waterMax-waterMin) / (vecIncVel.Y() - scIncVelUnderWater) * (-scIncVelUnderWater);
				if (m_pPos[ii].Y() < eqy)
				{
					//ASSERT(dvy >= 0.0f);
					dvy = Min((eqy - m_pPos[ii].Y()) / dt - m_pVel[ii].Y(), dvy);
					dvy = Max(Scalar(kZero), dvy);
				}
				else
				{
					//ASSERT(dvy <= 0.0f);
					dvy = Max((eqy - m_pPos[ii].Y()) / dt - m_pVel[ii].Y(), dvy);
					dvy = Min(Scalar(kZero), dvy);
				}
				m_pVel[ii] += Vector(kZero, dvy, kZero);
			}
			else
			{
				m_pVel[ii] += vecIncVel;
			}

			m_pVel[ii] *= RecipSqrt(scOne + LengthSqr(m_pVel[ii]) * (isUnderWater ? scViscousDampingUnderWater : scViscousDamping));
		}
	}
}

void Rope2::TimestepPositions()
{
	if (m_lastDynamicPoint < 0 || m_firstDynamicPoint < 0)
		return;
	TimestepPositions((U32F)m_firstDynamicPoint, (U32F)m_lastDynamicPoint, m_scStepTime);
}

void Rope2::TimestepPositions(U32F iStart, U32F iEnd, Scalar dt)
{
	PROFILE(Rope, Rope_stepPos);
	for(U32F ii = iStart; ii <= iEnd; ii++)
	{
		if (IsNodeLooseInt(ii))
		{
			m_pPos[ii] += m_pVel[ii] * dt;
			PHYSICS_ASSERT(IsFinite(m_pPos[ii]));
		}
	}
}

void Rope2::GetAabb(Aabb& aabb) const
{
	CheckStepNotRunning();
	aabb = m_aabb;
	aabb.Join(m_aabbSlacky);
}

void Rope2::AddIgnoreCollisionBody(const RigidBody* pBody)
{
	CheckStepNotRunning();

	if (GetIgnoreCollision(pBody))
	{
		return;
	}
	if (m_numIgnoreCollisionBodies < kMaxNumIgnoreCollisionBodies)
	{
		CleanIgnoreCollisionBodies();
		ALWAYS_ASSERTF(m_numIgnoreCollisionBodies < kMaxNumIgnoreCollisionBodies, ("Too many Rope ignore collision bodies. Increase kMaxNumIgnoreCollisionBodies"));
	}
	m_pIgnoreCollisionBodies[m_numIgnoreCollisionBodies] = pBody;
	m_numIgnoreCollisionBodies++;
}

void Rope2::RemoveIgnoreCollisionBody(const RigidBody* pBody)
{
	CheckStepNotRunning();

	for (U32F ii = 0; ii<m_numIgnoreCollisionBodies; ii++)
	{
		if (m_pIgnoreCollisionBodies[ii].ToBody() == pBody)
		{
			m_numIgnoreCollisionBodies--;
			memmove(&m_pIgnoreCollisionBodies[ii], &m_pIgnoreCollisionBodies[ii+1], (m_numIgnoreCollisionBodies-ii)*sizeof(m_pIgnoreCollisionBodies[0]));
			return;
		}
	}
}

bool Rope2::GetIgnoreCollision(const RigidBody* pBody) const
{
	CheckStepNotRunning();

	for (U32F ii = 0; ii<m_numIgnoreCollisionBodies; ii++)
	{
		if (m_pIgnoreCollisionBodies[ii].ToBody() == pBody)
		{
			return true;
		}
	}
	return false;
}

void Rope2::CleanIgnoreCollisionBodies()
{
	CheckStepNotRunning();

	U32F ii = 0;
	while (ii<m_numIgnoreCollisionBodies)
	{
		if (m_pIgnoreCollisionBodies[ii].ToBody() == nullptr)
		{
			m_numIgnoreCollisionBodies--;
			memmove(&m_pIgnoreCollisionBodies[ii], &m_pIgnoreCollisionBodies[ii+1], (m_numIgnoreCollisionBodies-ii)*sizeof(m_pIgnoreCollisionBodies[0]));
		}
		else
		{
			ii++;
		}
	}
}

void Rope2::AddCustomCollider(const RopeCollider* pCollider)
{
	CheckStepNotRunning();

	ALWAYS_ASSERT(m_numCustomColliders < kMaxNumCustomColliders);
	m_ppCustomColliders[m_numCustomColliders] = pCollider;
	m_numCustomColliders++;
}

void Rope2::RemoveCustomCollider(const RopeCollider* pCollider)
{
	CheckStepNotRunning();

	U32F iCol;
	for (iCol = 0; iCol<m_numCustomColliders; iCol++)
	{
		if (m_ppCustomColliders[iCol] == pCollider)
			break;
	}
	ASSERT(iCol<m_numCustomColliders);
	if (iCol<m_numCustomColliders)
	{
		for (U32F ii = 0; ii<m_numPoints; ii++)
		{
			for (U32F iConstr = 0; iConstr<m_pConstr[ii].m_numPlanes; iConstr++)
			{
				m_pConstr[ii].m_hCollider[iConstr].AdjustIndexOnRemoval(iCol);
			}
		}
		memmove(m_ppCustomColliders+iCol, m_ppCustomColliders+iCol+1, (m_numCustomColliders-iCol-1)*sizeof(m_ppCustomColliders[0]));
		m_numCustomColliders--;
	}
}

void Rope2::RemoveAllCustomColliders()
{
	CheckStepNotRunning();

	for (U32F ii = 0; ii<m_numPoints; ii++)
	{
		for (U32F iConstr = 0; iConstr<m_pConstr[ii].m_numPlanes; iConstr++)
		{
			if (!m_pConstr[ii].m_hCollider[iConstr].IsRigidBody())
				m_pConstr[ii].m_hCollider[iConstr] = RopeColliderHandle();
		}
	}
	m_numCustomColliders = 0;
}

void Rope2::AddRigidBodyCollider(const RigidBodyHandle& hBody)
{
	CheckStepNotRunning();

	ALWAYS_ASSERT(m_numRigidBodyColliders < kMaxNumRigidBodyColliders);
	m_phRigidBodyColliders[m_numRigidBodyColliders] = hBody;
	m_numRigidBodyColliders++;
}

const char* Rope2::NodeFlagToString(NodeFlags f)
{
	switch (f)
	{
	case kNodeKeyframed:		return "KeyFramed";
	case kNodeStrained:			return "Strained";
	case kNodeEdge:				return "Edge";
	case kNodeKeyedVelocity:	return "KeyedVelocity";
	case kNodeKeyframedSeg:		return "KeyframedSeg";
	case kNodeEdgeDetection:	return "EdgeDetection";
	case kNodeEdgeCorner:		return "EdgeCorner";
	case kNodeNoEdgeDetection:	return "NoEdgeDetection";
	case kNodeUseSavePos:		return "UseSavePos";
	case kNodeUserMark1:		return "UserMark1";
	case kNodeUserMark2:		return "UserMark2";
	case kNodeUserMark3:		return "UserMark3";
	case kNodeUserMark4:		return "UserMark4";
	}

	return "???";
}

void Rope2::NodeFlagsToString(RopeNodeFlags f, char* outText, I32 maxLen)
{
	bool first = true;
	outText[0] = 0;

	for (U32F ii = kNodeFlagBegin; ii <= kNodeFlagEnd; ii = (ii << 1))
	{
		if (f & ii)
		{
			if (!first)
				strncat(outText, "|", maxLen - 1);

			strncat(outText, NodeFlagToString((NodeFlags)ii), maxLen - 1);

			first = false;
		}
	}
}

void Rope2::FindParabolicFit(U32F iStart, U32F iEnd, F32& x0, F32& y0, F32& A)
{
	F32 s = m_pRopeDist[iEnd] - m_pRopeDist[iStart];
	F32 h = (F32)(m_pPos[iEnd].Y() - m_pPos[iStart].Y());

	F32 lHor = 0.0f;
	F32 l = 0.0f;

	// ToDo: Find l and lHor
	ASSERT(false);

	// TEMP: since "Find l" is not implemented, prevent absolutely correct div-by-zero warning
	l = 0.01f;

	ASSERT(l > kScEdgeTol);
	s = Max(s, l + 0.1f); // some min slack needed

	// From rope strained into isosceles triangle
	F32 Amax = 0.5f/l * Sqrt(s*s/l*l - 1.0f);

	U32F numTest = (U32F)(l / m_fSegmentLength + 1.0f);
	F32 dxTest = lHor / (F32)numTest;

	F32 Amin = 0.0f;
	A = Amax;
	do
	{
		F32 x0a, x0b;
		bool res = SolveQuadratic(1.0f, lHor, 0.5f * lHor * lHor - h/2.0f*A, x0a, x0b);
		ASSERT(res);
		x0 = x0a > 0.0f && x0a < x0b ? x0a : x0b;
		ASSERT(x0 > 0.0f && x0 <= lHor);
		y0 = A * x0 * x0;
		F32 x1 = x0 - lHor;
		F32 y1 = A * x1 * x1;
		ASSERT(Abs(Abs(y1-y0)-h) < 0.01f);

		F32 sTest = 0.0f;
		F32 xTest = x0;
		F32 yTest = y0;
		for (U32 ii = 0; ii<numTest; ii++)
		{
			xTest -= dxTest;
			F32 yTest1 = A * xTest * xTest;
			F32 dyTest = yTest - yTest1;
			sTest += Sqrt(dxTest * dxTest + dyTest * dyTest);
			yTest = yTest1;
		}

		if (Abs(sTest - s) < 0.01f * s)
			break;

		if (sTest < s)
			Amin = A;
		else
			Amax = A;
		A = 0.5f * (Amin + Amax);
	} while (true);
}

Vector Rope2::GetEdgeVelocity(U32F edgeIndex)
{
	const EdgePoint& ePoint = m_pEdges[edgeIndex];
	if (ePoint.m_flags & kNodeKeyframed)
	{
		// Keyframed -> just get the node (keyframed) velocity
		return m_pVel[edgeIndex];
	}

	if (ePoint.m_numEdges == 0)
	{
		return m_pVel[edgeIndex];
	}

	// Get the velocity of the collider body
	RopeColEdge& colEdge0 = m_colCache.m_pEdges[ePoint.m_edgeIndices[0]];
	U16 triIndex0 = colEdge0.m_triIndex;
	RopeColTri& colTri0 = m_colCache.m_pTris[triIndex0];
	RopeColliderHandle& colShape0 = m_colCache.m_pShapes[colTri0.m_shapeIndex];

	Locator prevLoc, loc;
	if (colShape0.IsRigidBody())
	{
		const RigidBody* pBody = colShape0.GetRigidBody();
		prevLoc = pBody->GetPreviousLocatorCm();
		loc = pBody->GetLocatorCm();
	}
	else
	{
		const RopeCollider* pCollider = GetCustomCollider(colShape0.GetCustomIndex());
		prevLoc = pCollider->m_locPrev;
		loc = pCollider->m_loc;
	}
	Vector colliderVel = (ePoint.m_pos - prevLoc.TransformPoint(loc.UntransformPoint(ePoint.m_pos))) * m_scInvStepTime;

	// Now find out what is the sum of projections of edge normals into the direction of the velocity
	Vector velDir = SafeNormalize(colliderVel, kZero);
	Scalar proj = Max(Scalar(kZero), Dot(velDir, colEdge0.m_normal));
	Vector prevNorms[EdgePoint::kMaxNumEdges];
	U32F numPrevNorms = 0;
	for (U32F ii = 1; ii<ePoint.m_numEdges; ii++)
	{
		RopeColEdge& colEdge = m_colCache.m_pEdges[ePoint.m_edgeIndices[ii]];
		U16 triIndex = colEdge.m_triIndex;
		RopeColTri& colTri = m_colCache.m_pTris[triIndex];
		if (colTri.m_shapeIndex != colTri0.m_shapeIndex)
		{
			RopeColliderHandle& colShape = m_colCache.m_pShapes[colTri.m_shapeIndex];
			if (!RopeColliderHandle::AreCollidersAttached(colShape, colShape0))
			{
				JAROS_ASSERT(false); // again, edges from different rigid bodies in the same edge point, we're not handling that for now
				continue;
			}
		}

		Vector norm = colEdge.m_normal;
		for (U32F jj = 0; jj<numPrevNorms; jj++)
		{
			norm -= Dot(norm, prevNorms[jj]) * prevNorms[jj];
		}
		proj += Max(Scalar(kZero), Dot(velDir, norm));

		prevNorms[numPrevNorms++] = colEdge.m_normal;
	}

	return proj * colliderVel;
}

void Rope2::CreateDistanceConstraintsFromStrainedEdges(U32F iEdgeStart, U32F iEdgeEnd)
{
	PHYSICS_ASSERT(m_pDistConstraints);
	PHYSICS_ASSERT(m_pDistConstraintsVel);

	Scalar dist3d(kZero);
	for (U32F ii = iEdgeStart+1; ii<=iEdgeEnd; ii++)
	{
		dist3d += Dist(m_pEdges[ii].m_pos, m_pEdges[ii-1].m_pos);
	}
	Scalar dist3dSqr = Sqr(dist3d);

	Scalar s = m_pEdges[iEdgeEnd].m_ropeDist - m_pEdges[iEdgeStart].m_ropeDist;

	U32F iStart;
	for (iStart = 0; iStart<m_numPoints; iStart++)
	{
		if (m_pRopeDist[iStart] > m_pEdges[iEdgeStart].m_ropeDist)
			break;
	}

	F32 startMoveDist = -1.0f;
	F32 endMoveDist = -1.0f;
	for (U32 iKey = 0; iKey<m_numKeyPoints; iKey++)
	{
		if (startMoveDist < 0.0f && m_pKeyRopeDist[iKey] >= m_pEdges[iEdgeStart].m_ropeDist)
		{
			startMoveDist = m_pKeyMoveDist[iKey];
		}
		if (endMoveDist < 0.0f && m_pKeyRopeDist[iKey] >= m_pEdges[iEdgeEnd].m_ropeDist)
		{
			endMoveDist = m_pKeyMoveDist[iKey];
			break;
		}
	}
	PHYSICS_ASSERT(startMoveDist >= 0.0f && endMoveDist >= 0.0f);

	{
		U32F edgeIndex = iEdgeStart + 1;
		Scalar lacc(kZero);
		Scalar lsec = Dist(m_pEdges[edgeIndex-1].m_pos, m_pEdges[edgeIndex].m_pos);
		for (U32 ii = iStart; ii<m_numPoints && m_pRopeDist[ii]<m_pEdges[iEdgeEnd].m_ropeDist; ii++)
		{
			Scalar h, l1;
			Scalar s1 = m_pRopeDist[ii] - m_pEdges[iEdgeStart].m_ropeDist;
			if (s > dist3d)
			{
				Scalar s2 = s - s1;
				l1 = (dist3dSqr + Sqr(s1) - Sqr(s2)) / dist3d * 0.5f;
				if (l1 <= Scalar(kZero))
				{
					l1 = kZero;
					h = s1 + 0.03f;
				}
				else if (l1 >= dist3d)
				{
					l1 = dist3d;
					h = s2 + 0.03f;
				}
				else
				{
					h = Sqrt(Max(0.0f, Sqr(s1) - Sqr(l1))) + 0.03f;
				}
			}
			else
			{
				h = kZero;
				l1 = dist3d * s1 / s;
			}

			while (lacc+lsec < l1)
			{
				lacc += lsec;
				edgeIndex++;
				lsec = Dist(m_pEdges[edgeIndex-1].m_pos, m_pEdges[edgeIndex].m_pos);
			}

			Point p = lsec <= 0.00001f ? m_pEdges[edgeIndex].m_pos : Lerp(m_pEdges[edgeIndex-1].m_pos, m_pEdges[edgeIndex].m_pos, (l1-lacc)/lsec);
			Vector vel = lsec <= 0.00001f ? GetEdgeVelocity(edgeIndex) : Lerp(GetEdgeVelocity(edgeIndex-1), GetEdgeVelocity(edgeIndex), (l1-lacc)/lsec);
			h += Lerp(startMoveDist, endMoveDist, (F32)(s1/s));

			m_pDistConstraints[ii] = p.GetVec4();
			m_pDistConstraints[ii].SetW(h);
			PHYSICS_ASSERT(IsFinite(m_pDistConstraints[ii]));

			m_pDistConstraintsVel[ii] = vel;
			PHYSICS_ASSERT(IsFinite(m_pDistConstraintsVel[ii]));
		}
	}

	if (m_userDistanceConstraintBlend > 0.0f)
	{
		F32 parabolA = -m_userDistanceConstraint/Sqr(0.5f*s);
		F32 R = dist3d/s;
		U32 edgeIndex = iEdgeStart + 1;
		F32 lsec = Dist(m_pEdges[edgeIndex-1].m_pos, m_pEdges[edgeIndex].m_pos);
		F32 lacc = kZero;
		for (U32 ii = iStart; ii<m_numPoints && m_pRopeDist[ii]<m_pEdges[iEdgeEnd].m_ropeDist; ii++)
		{
			F32 s1 = m_pRopeDist[ii] - m_pEdges[iEdgeStart].m_ropeDist;
			F32 d = s1*R;
			while (d > lacc+lsec)
			{
				lacc += lsec;
				edgeIndex++;
				lsec = Dist(m_pEdges[edgeIndex-1].m_pos, m_pEdges[edgeIndex].m_pos);
			}

			F32 tLerp = (d-lacc)/lsec;
			Point p = Lerp(m_pEdges[edgeIndex-1].m_pos, m_pEdges[edgeIndex].m_pos, tLerp);
			Vector vel = Lerp(GetEdgeVelocity(edgeIndex-1), GetEdgeVelocity(edgeIndex), tLerp);
			F32 h = parabolA*Sqr(s1-0.5f*s) + m_userDistanceConstraint + 0.01f;
			h += Lerp(startMoveDist, endMoveDist, (F32)(s1/s));

			h = Lerp((F32)m_pDistConstraints[ii].W(), h, m_userDistanceConstraintBlend);
			m_pDistConstraints[ii] = Lerp(Point(m_pDistConstraints[ii]), p, m_userDistanceConstraintBlend).GetVec4();
			m_pDistConstraints[ii].SetW(h);
			PHYSICS_ASSERT(IsFinite(m_pDistConstraints[ii]));

			m_pDistConstraintsVel[ii] = Lerp(Vector(m_pDistConstraintsVel[ii]), vel, m_userDistanceConstraintBlend);
			PHYSICS_ASSERT(IsFinite(m_pDistConstraintsVel[ii]));
		}
	}
}

void Rope2::CreateDistanceConstraintsFromStrainedEdges()
{
	if (!m_pDistConstraints)
		return;

	for (U32F ii = 0; ii<m_numPoints; ii++)
	{
		m_pDistConstraints[ii] = Vec4(0.0f, 0.0f, 0.0f, FLT_MAX);
		m_pDistConstraintsVel[ii] = kZero;
	}

	if (m_numKeyPoints == 0 || !m_bDistanceConstraintEnabled)
	{
		return;
	}

	U32F prevKey = 0;
	ASSERT(m_pEdges[0].m_flags & kNodeKeyframed);
	for (U32F ii = 1; ii<m_numEdges; ii++)
	{
		if (m_pEdges[ii].m_flags & kNodeKeyframed)
		{
			// If we have edge detection but is not strained
			if (((m_pEdges[ii].m_flags & (kNodeEdgeDetection|kNodeUseSavePos)) != 0) && ((m_pEdges[ii].m_flags & kNodeStrained) == 0))
			{
				CreateDistanceConstraintsFromStrainedEdges(prevKey, ii);
			}
			prevKey = ii;
		}
	}
}

void Rope2::CreateFrictionConstraint(U32 ii)
{
	if (m_pConstr[ii].m_flags == 0)
	{
		m_pFrictionConstraints[ii] = Vec4(kZero);
		m_pFrictionConstConstraints[ii] = 0.0f;
		return;
	}

	Point pos0 = m_pPos[ii];
	Vector vel = kZero;

	// Out of collision
	m_pConstr[ii].RelaxPos(pos0, m_pRadius[ii]);

	// Add collision velo
	m_pConstr[ii].RelaxVel(pos0, vel, this, Scalar(1.0f), kZero);
	pos0 += vel * m_scStepTime;

	m_pFrictionConstraints[ii] = pos0.GetVec4();
	m_pFrictionConstraints[ii].SetW(g_proporSolverFriction);

	bool bStaticFriction = Length(m_pVel[ii]) < kRopeMaxSleepVel;
	m_pFrictionConstConstraints[ii] = bStaticFriction ? g_constSolverFriction : 0.0f;
}

void Rope2::CreateFrictionConstraintFromTension(U32 ii)
{
	Point pos0 = m_pPos[ii];
	Vector vel = kZero;

	// Out of collision
	m_pConstr[ii].RelaxPos(pos0, m_pRadius[ii]);

	// Add collision velo
	m_pConstr[ii].RelaxVel(pos0, vel, this, Scalar(1.0f), kZero);
	pos0 += vel * m_scStepTime;

	m_pFrictionConstraints[ii] = pos0.GetVec4();
	m_pFrictionConstraints[ii].SetW(Min(1.0f, g_proporSolverTensionFriction * m_pTensionFriction[ii]));

	bool bStaticFriction = Length(m_pVel[ii]) < kRopeMaxSleepVel;
	m_pFrictionConstConstraints[ii] = bStaticFriction ? g_constSolverTensionFriction * m_pTensionFriction[ii] : 0.0f;
}

void Rope2::CreateFrictionConstraints()
{
	if (!m_pFrictionConstraints)
		return;

	if (m_firstDynamicPoint > 0)
	{
		m_pFrictionConstraints[m_firstDynamicPoint-1] = Vec4(kZero);
		m_pFrictionConstConstraints[m_firstDynamicPoint-1] = 0.0f;
	}
	if (m_lastDynamicPoint < m_numPoints-1)
	{
		m_pFrictionConstraints[m_lastDynamicPoint+1] = Vec4(kZero);
		m_pFrictionConstConstraints[m_lastDynamicPoint+1] = 0.0f;
	}

	HavokMarkForReadJanitor jj; // because we're reading velocities from collider RBs

	if (m_pTensionFriction)
	{
		for (I32 ii = m_firstDynamicPoint; ii<=m_lastDynamicPoint; ii++)
		{
			if (IsNodeLoose(ii))
				CreateFrictionConstraintFromTension(ii);
			else
			{
				m_pFrictionConstraints[ii] = Vec4(kZero);
				m_pFrictionConstConstraints[ii] = 0.0f;
			}
		}
	}
	else
	{
		for (I32 ii = m_firstDynamicPoint; ii<=m_lastDynamicPoint; ii++)
		{
			if (IsNodeLoose(ii))
				CreateFrictionConstraint(ii);
			else
			{
				m_pFrictionConstraints[ii] = Vec4(kZero);
				m_pFrictionConstConstraints[ii] = 0.0f;
			}
		}
	}
}

void Rope2::DisableRopeBreakingEdgeConstraints(U32F i0, U32F i1)
{
	// Check constraint on 2 neighboring nodes and see if they have edges that will pull the rope apart (and possibly make the rope go through a thin feature)
	// In this case we will disable one of the planes on one of the nodes to prevent that

	Constraint& c0 = m_pConstr[i0];
	Constraint& c1 = m_pConstr[i1];
	if (c0.m_numEdges == 0 || c1.m_numEdges == 0)
	{
		return;
	}

	do
	{
		Scalar minDt = -0.1f;
		I32 removeI0 = -1;
		I32 removeI1 = -1;
		for (U32 iPl0 = 0; iPl0<c0.m_firstNoEdgePlane; iPl0++)
		{
			for (U32 iPl1 = 0; iPl1<c1.m_firstNoEdgePlane; iPl1++)
			{
				Scalar dt = Dot3(c0.m_planes[iPl0], c1.m_planes[iPl1]);
				if (dt < minDt)
				{
					bool planePresentInOtherCon = false;
					for (U32 jj = 0; jj<c1.m_numPlanes; jj++)
					{
						if (Rope2PointCol::ArePlanesSimilar(c0.m_planes[iPl0], c1.m_planes[jj], m_pPos[i0]))
						{
							planePresentInOtherCon = true;
							break;
						}
					}
					for (U32 jj = 0; jj<c0.m_numPlanes; jj++)
					{
						if (Rope2PointCol::ArePlanesSimilar(c1.m_planes[iPl1], c0.m_planes[jj], m_pPos[i0]))
						{
							planePresentInOtherCon = true;
							break;
						}
					}
					if (!planePresentInOtherCon)
					{
						removeI0 = iPl0;
						removeI1 = iPl1;
						minDt = dt;
					}
				}
			}
		}

		if (removeI0 >= 0)
		{
			Scalar height0 = Dot4(m_pLastPos[i0], c0.m_planes[removeI0]);
			Scalar height1 = Dot4(m_pLastPos[i1], c1.m_planes[removeI1]);
			if (height0 < height1)
			{
				c0.RemoveConstraintBreakingPlane(removeI0);
			}
			else
			{
				c1.RemoveConstraintBreakingPlane(removeI1);
			}
		}
		else
		{
			break;
		}
	} while(1);

	/*U32 ii = 0;
	while (ii<c0.m_numEdges)
	{
		U32 iPl0 = c0.m_edgePlanes[ii*2];
		U32 iPl1 = c0.m_edgePlanes[ii*2+1];
		U32 jj = 0;
		bool removed0 = false;
		while (jj<c1.m_numEdges)
		{
			U32 jPl0 = c1.m_edgePlanes[jj*2];
			U32 jPl1 = c1.m_edgePlanes[jj*2+1];

			Scalar dt00 = Dot3(c0.m_planes[iPl0], c1.m_planes[jPl0]);
			Scalar dt01 = Dot3(c0.m_planes[iPl0], c1.m_planes[jPl1]);
			Scalar dt10 = Dot3(c0.m_planes[iPl1], c1.m_planes[jPl0]);
			Scalar dt11 = Dot3(c0.m_planes[iPl1], c1.m_planes[jPl1]);
			Scalar dt0 = dt00 < dt01 ? dt00 : dt01;
			Scalar dt1 = dt10 < dt11 ? dt10 : dt11;
			Scalar dt = dt0 < dt1 ? dt0 : dt1;
			if (dt >= 0.0f) // the 0.0 here is just an arbitrary threshold
			{
				jj++;
				continue;
			}

			U32F jPlA = dt00 < dt01 ? jPl0 : jPl1;
			U32F jPlB = dt10 < dt11 ? jPl0 : jPl1;
			U32F iPl = dt0 < dt1 ? iPl0 : iPl1;
			U32F jPl = dt0 < dt1 ? jPlA : jPlB;

			Scalar height0 = Dot4(m_pLastPos[i0], c0.m_planes[iPl]);
			Scalar height1 = Dot4(m_pLastPos[i1], c1.m_planes[jPl]);
			if (height0 < height1)
			{
				c0.RemoveConstraintBreakingPlane(iPl);
				removed0 = true;
				break;
			}
			else
			{
				c1.RemoveConstraintBreakingPlane(jPl);
			}
		}

		if (!removed0)
		{
			ii++;
		}
	}*/
}

void Rope2::AddSelfCollisionConstraint(U32& firstFree, I16 iNode0, I16 iNode1)
{
	U32 iInsert = firstFree;
	bool swap = false;
	while (iInsert < m_numSelfCollision)
	{
		U32 iNextWave = iInsert+THREAD_COUNT;
		bool clash[2] = { false, false };
		for (; iInsert<iNextWave; iInsert++)
		{
			if (m_pSelfCollision[iInsert*2] == -1)
			{
				// Found a spot!
				break;
			}
			if ((m_pSelfCollision[iInsert*2] == iNode0) | (m_pSelfCollision[iInsert*2+1] == iNode1))
			{
				clash[0] = true;
				if (clash[1])
					break;
			}
			if ((m_pSelfCollision[iInsert*2] == iNode1) | (m_pSelfCollision[iInsert*2+1] == iNode0))
			{
				clash[1] = true;
				if (clash[0])
					break;
			}
		}
		if (m_pSelfCollision[iInsert*2] == -1)
		{
			swap = clash[0];
			if (iInsert-firstFree == THREAD_COUNT-1)
			{
				firstFree = iInsert+1;
			}
			break;
		}
		if (iInsert-firstFree == THREAD_COUNT)
		{
			firstFree = iInsert;
		}
		iInsert = iNextWave;
	}
	if (iInsert == m_numSelfCollision)
	{
		if (m_numSelfCollision < m_maxSelfCollision)
		{
			// Add a new wave
			m_numSelfCollision += THREAD_COUNT;
			// Fill up wave with -1
			for (U32 ii = iInsert+1; ii<m_numSelfCollision; ii++)
			{
				m_pSelfCollision[ii*2] = -1;
			}
		}
	}
	if (iInsert < m_maxSelfCollision)
	{
		m_pSelfCollision[iInsert*2] = swap ? iNode1 : iNode0;
		m_pSelfCollision[iInsert*2+1] = swap ? iNode0 : iNode1;
	}
}

class SelfGridHash
{
public:

	SelfGridHash(F32 cellSize, char* pMem, U32 memSize)
	{
		m_numBuckets = memSize / sizeof(I16);
		// Must be power of 2
		if (((m_numBuckets - 1) & m_numBuckets) != 0)
		{
			U32 lastSetBit = FindLastSetBitIndex(m_numBuckets);
			m_numBuckets &= (1U << lastSetBit);
		}
		m_cellSizeInv = 1.0f / cellSize;
		m_pBuckets = (I16*)pMem;
		memset(m_pBuckets, 0xff, sizeof(I16)*m_numBuckets); // fill with -1
	}

	Simd::VI32 GetCell(const Point& pos)
	{
		Vector vec0 = Vector(pos - SMath::kOrigin) * Scalar(m_cellSizeInv);
		Vector vec1(Simd::Floor(vec0.QuadwordValue()));
		ALWAYS_ASSERT(AllComponentsLessThan(Abs(vec1), Vector(SMATH_VEC_REPLICATE_FLOAT((F32)0x7ffffff0))));
		return Simd::ConvertVF32ToVI32(vec1.GetVec4().QuadwordValue()); // GetVec4 is here to make sure we have 0 in W
	}

	U32 HashCell(const Simd::VI32 cell)
	{
		Simd::RawVI32 r;
		r.vec = Simd::Mul(cell, Simd::MakeVI32(0x8da6b343, 0xd8163841, 0xcb1ab31f, 0)); // these are some large primes
		U32 h = r.raw[0] ^ (r.raw[1] ^ r.raw[2]);
		return h & (m_numBuckets - 1); // modulo knowing our m_numBuckets is power of 2
	}


	U32 HashPos(const Point& pos)
	{
		return HashCell(GetCell(pos));
	}

	void Insert(const Point& pos, I16 val)
	{
		U32 index = HashPos(pos);
		while (m_pBuckets[index] != -1)
		{
			index++;
			index = index == m_numBuckets ? 0 : index;
		}
		m_pBuckets[index] = val;
	}

	void SphereQuery(const Point& pos, F32 radius, ExternalBitArray* pBitArray)
	{
		typedef union {
			Simd::VI32	m_vi32;
			I32			m_raw[4];
		} VI32U;

		VI32U cellMin;
		VI32U cellMax;
		cellMin.m_vi32 = GetCell(pos - Vector(Scalar(radius)));
		cellMax.m_vi32 = GetCell(pos + Vector(Scalar(radius)));
		VI32U cell;
		for (cell.m_raw[0] = cellMin.m_raw[0]; cell.m_raw[0] <= cellMax.m_raw[0]; cell.m_raw[0]++)
		{
			for (cell.m_raw[1] = cellMin.m_raw[1]; cell.m_raw[1] <= cellMax.m_raw[1]; cell.m_raw[1]++)
			{
				for (cell.m_raw[2] = cellMin.m_raw[2]; cell.m_raw[2] <= cellMax.m_raw[2]; cell.m_raw[2]++)
				{
					U32 index = HashCell(cell.m_vi32);
					while (m_pBuckets[index] != -1)
					{
						pBitArray->SetBit(m_pBuckets[index]);
						index++;
						index = index == m_numBuckets ? 0 : index;
					}
				}
			}
		}
	}

	F32 m_cellSizeInv;
	U32 m_numBuckets;
	I16* m_pBuckets;
};

void Rope2::SelfCollision()
{
	PROFILE(Rope, SelfCollision);

	if (!m_pSelfCollision)
		return;

	m_numSelfCollision = 0;
	//return;

	U32 numPoints = m_lastDynamicPoint - m_firstDynamicPoint + 1;
	if (numPoints < 2)
		return;

	F32 d = 2.0f*m_fRadius;

	ScopedTempAllocator alloc(FILE_LINE_FUNC);

	ExternalBitArray bitArray;
	bitArray.InitNoAssign(numPoints, NDI_NEW U64[ExternalBitArray::DetermineNumBlocks(numPoints)]);

	U32 gridHashMemSize = alloc.GetFreeSize() - 16;
	char* pGridHashMem = NDI_NEW char[gridHashMemSize];
	PHYSICS_ASSERT(gridHashMemSize/sizeof(I16) > numPoints);
	SelfGridHash gridHash(d, pGridHashMem, gridHashMemSize);
	for (U32 ii = m_firstDynamicPoint; ii<=m_lastDynamicPoint; ii++)
	{
		gridHash.Insert(m_pPos[ii], ii-m_firstDynamicPoint);
	}

	U32 firstInsertFree = 0;
	for (U32 ii = m_firstDynamicPoint+1; ii<=m_lastDynamicPoint; ii++)
	{
		const Point pos = m_pPos[ii];
		bitArray.ClearAllBits();
		gridHash.SphereQuery(m_pPos[ii], m_fRadius, &bitArray);
		for (U32 jj = bitArray.FindFirstSetBit(); jj<ii-m_firstDynamicPoint; jj = bitArray.FindNextSetBit(jj))
		{
			F32 dist = Dist(pos, m_pPos[m_firstDynamicPoint+jj]);
			if (dist < d)
			{
				AddSelfCollisionConstraint(firstInsertFree, m_firstDynamicPoint+jj, ii);
			}
		}
	}
}

void Rope2::SetAllowKeyStretchConstraints(bool allow)
{
	PHYSICS_ASSERT(m_pPrevKeyIndex && m_pNextKeyIndex);
	m_bAllowKeyStretchConstraints = allow;
}

void Rope2::UpdateSounds(NdGameObject* pOwner)
{
	UpdateStrainedSounds(pOwner);
	UpdateHitSounds(pOwner);
	UpdateSlideSounds(pOwner);
	UpdateEdgeSlideSounds(pOwner);
}

void Rope2::UpdateStrainedSounds(NdGameObject* pOwner)
{
	if (!m_pStrainedSounds)
		return;

	if (m_bSleeping)
	{
		for (U32F ii = 0; ii<m_numStrainedSounds; ii++)
		{
			KillProcess(m_pStrainedSounds[ii].m_hSound);
		}
		m_numStrainedSounds = 0;
		return;
	}

	// Try matching the old sounds with new edges
	{
		U32F nextEdgePoint = 1;
		for (U32F ii = 0; ii<m_numStrainedSounds; ii++)
		{
			const RopeColliderHandle& shape = m_pStrainedSounds[ii].m_colEdgeId.m_shape;
			Locator loc = shape.GetLocator(this);
			Locator prevLoc = shape.GetPrevLocator(this);
			Point movedPos = loc.TransformPoint(prevLoc.UntransformPoint(m_pStrainedSounds[ii].m_pos));

			F32 closestDist2 = FLT_MAX;
			I32 closestEdge = -1;
			bool exactMatch = false;
			for (I32F iEdgePoint = nextEdgePoint; iEdgePoint<m_numEdges-1; iEdgePoint++)
			{
				const EdgePoint& ePoint = m_pEdges[iEdgePoint];
				if (ePoint.m_flags & kNodeStrained)
				{
					bool shapeMatch = false;
					for (U32F iEdge = 0; iEdge<ePoint.m_numEdges; iEdge++)
					{
						if (ePoint.GetEdgeActive(iEdge))
						{
							RopeColEdgeId edgeId;
							m_colCache.GetEdgeId(ePoint.m_edgeIndices[iEdge], edgeId);
							if (m_pStrainedSounds[ii].m_colEdgeId.m_shape == edgeId.m_shape)
							{
								if (m_pStrainedSounds[ii].m_colEdgeId.m_triKey == edgeId.m_triKey && m_pStrainedSounds[ii].m_colEdgeId.m_edgeIndex == edgeId.m_edgeIndex)
								{
									closestDist2 = 0.0f;
									closestEdge = iEdgePoint;
									exactMatch = true;
									break;
								}
								shapeMatch = true;
							}
						}
					}
					if (exactMatch)
					{
						break;
					}

					if (shapeMatch)
					{
						F32 dist2 = DistSqr(ePoint.m_pos, movedPos);
						if (dist2 < closestDist2)
						{
							closestDist2 = dist2;
							closestEdge = iEdgePoint;
						}
					}
				}
			}

			if (closestEdge >= 0)
			{
				const EdgePoint& ePoint = m_pEdges[closestEdge];
				m_pStrainedSounds[ii].m_pos = ePoint.m_pos;
				m_pStrainedSounds[ii].m_ropeDist = ePoint.m_ropeDist;
				if (!exactMatch)
				{
					RopeColEdgeId minId;
#if HAVOKVER < 0x2016
					minId.m_triKey = 0xffffffff;
#endif
					for (U32F iEdge = 0; iEdge<ePoint.m_numEdges; iEdge++)
					{
						if (ePoint.GetEdgeActive(iEdge))
						{
							RopeColEdgeId id;
							m_colCache.GetEdgeId(ePoint.m_edgeIndices[iEdge], id);
							if (id.m_triKey <= minId.m_triKey)
							{
								// For improved PAT consistency we always choose the edge that has min triangle key
								minId = id;
							}
						}
					}
					m_pStrainedSounds[ii].m_colEdgeId = minId;
				}
				nextEdgePoint = closestEdge+1;
			}
			else
			{
				m_pStrainedSounds[ii].m_ropeDist = -1.0f;
			}
		}
	}

	// Discard sounds that are too close to each other
	{
		F32 prevRopeDist = -FLT_MAX;
		for (U32F ii = 0; ii<m_numStrainedSounds; ii++)
		{
			if (m_pStrainedSounds[ii].m_ropeDist >= 0.0f)
			{
				if (m_pStrainedSounds[ii].m_ropeDist - prevRopeDist < m_soundDef.m_strainedSoundDist)
				{
					m_pStrainedSounds[ii].m_ropeDist = -1.0f;
				}
				else
				{
					prevRopeDist = m_pStrainedSounds[ii].m_ropeDist;
				}
			}
		}
	}

	// Recreate a new array of sounds and adding new ones where fit
	{
		ScopedTempAllocator jj(FILE_LINE_FUNC);
		StrainedSound* pSounds = NDI_NEW StrainedSound[m_maxNumStrainedSounds];
		U32F numSounds = 0;
		U32F iOldSound = 0;
		while (iOldSound < m_numStrainedSounds && m_pStrainedSounds[iOldSound].m_ropeDist < 0.0f)
			iOldSound++;
		F32 prevRopeDist = -FLT_MAX;
		F32 nextRopeDist = iOldSound < m_numStrainedSounds ? m_pStrainedSounds[iOldSound].m_ropeDist : FLT_MAX;
		for (I32F iEdgePoint = 1; iEdgePoint<m_numEdges-1; iEdgePoint++)
		{
			const EdgePoint& ePoint = m_pEdges[iEdgePoint];
			if ((ePoint.m_flags & kNodeStrained) && ePoint.m_activeEdges > 0 && ePoint.m_ropeDist - prevRopeDist > m_soundDef.m_strainedSoundDist && nextRopeDist - ePoint.m_ropeDist > m_soundDef.m_strainedSoundDist)
			{
				RopeColEdgeId minId;
#if HAVOKVER < 0x2016
				minId.m_triKey = 0xffffffff;
#endif
				for (U32F iEdge = 0; iEdge<ePoint.m_numEdges; iEdge++)
				{
					RopeColEdgeId id;
					m_colCache.GetEdgeId(ePoint.m_edgeIndices[iEdge], id);
					if (id.m_triKey <= minId.m_triKey)
					{
						// For improved PAT consistency we always choose the edge that has min triangle key
						minId = id;
					}
				}

				PHYSICS_ASSERT(numSounds < m_maxNumStrainedSounds);
				StrainedSound& sound = pSounds[numSounds];
				numSounds++;

				// Find an old unused sound or spawn a new one
				U32F iFreeSound;
				for (iFreeSound = 0; iFreeSound<m_numStrainedSounds; iFreeSound++)
				{
					if (m_pStrainedSounds[iFreeSound].m_ropeDist < 0.0f && m_pStrainedSounds[iFreeSound].m_hSound.HandleValid())
						break;
				}
				if (iFreeSound < m_numStrainedSounds)
				{
					sound.m_hSound = m_pStrainedSounds[iFreeSound].m_hSound;
					m_pStrainedSounds[iFreeSound].m_hSound = nullptr;
				}
				else
				{
					sound.m_hSound = NewProcess(SfxSpawnInfo(m_soundDef.m_strainedSlideSound, pOwner));
				}

				sound.m_colEdgeId = minId;
				sound.m_pos = ePoint.m_pos;
				sound.m_ropeDist = ePoint.m_ropeDist;
				prevRopeDist = ePoint.m_ropeDist;
			}
			else if (nextRopeDist - ePoint.m_ropeDist < m_soundDef.m_strainedSoundDist)
			{
				// Copy over old sound
				PHYSICS_ASSERT(numSounds < m_maxNumStrainedSounds);
				pSounds[numSounds] = m_pStrainedSounds[iOldSound];
				m_pStrainedSounds[iOldSound].m_hSound = nullptr;
				numSounds++;
				prevRopeDist = nextRopeDist;
				iOldSound++;
				while (iOldSound < m_numStrainedSounds && m_pStrainedSounds[iOldSound].m_ropeDist < 0.0f)
					iOldSound++;
				nextRopeDist = iOldSound < m_numStrainedSounds ? m_pStrainedSounds[iOldSound].m_ropeDist : FLT_MAX;
			}
		}

		// Kill discarded sounds
		for (U32F ii = 0; ii<m_numStrainedSounds; ii++)
		{
			if (m_pStrainedSounds[ii].m_hSound.HandleValid())
			{
				KillProcess(m_pStrainedSounds[ii].m_hSound);
			}
		}

		// Copy over the new array
		memcpy(m_pStrainedSounds, pSounds, numSounds * sizeof(pSounds[0]));
		m_numStrainedSounds = numSounds;
	}

	// Update sound params
	for (U32F ii = 0; ii<m_numStrainedSounds; ii++)
	{
		const RopeColEdgeId& edgeId = m_pStrainedSounds[ii].m_colEdgeId;
		const RopeColliderHandle& shape = edgeId.m_shape;

		SendEvent(SID("set-position"), m_pStrainedSounds[ii].m_hSound, m_pStrainedSounds[ii].m_pos);

		// Rope vel
		Point pos;
		Vector vel;
		GetPosAndVel(m_pStrainedSounds[ii].m_ropeDist, pos, vel);

		// Edge vel
		Locator loc = shape.GetLocator(this);
		Locator prevLoc = shape.GetPrevLocator(this);
		Point prevPos = prevLoc.TransformPoint(loc.UntransformPoint(m_pStrainedSounds[ii].m_pos));
		Vector edgeVel = (m_pStrainedSounds[ii].m_pos - prevPos) * m_scInvStepTime;

		vel -= edgeVel;
		SendEvent(SID("set-variable"), m_pStrainedSounds[ii].m_hSound, CcVar("speed"), (F32)Length(vel), 0U);

		Pat pat(0);
		if (const RigidBody* pBody = shape.GetRigidBody())
		{
			hknpShapeKey shapeKey;
			if (shape.GetListIndex() >= 0)
			{
				PHYSICS_ASSERT(!edgeId.m_triKey.isValid()); // a mesh inside a list shape? Yuck!
				shapeKey = hknpShapeKey(shape.GetListIndex());
			}
			else
			{
				shapeKey = edgeId.m_triKey;
			}
			pat = HavokGetPatFromHkRigidBody(pBody->GetHavokBody(), shapeKey);
		}
		SendEvent(SID("set-variable"), m_pStrainedSounds[ii].m_hSound, CcVar("surface-type"), (F32)pat.GetSurfaceType(), 0U);

		//{
		//	DebugDrawCross(m_pStrainedSounds[ii].m_pos, 0.05f, kColorWhite);
		//	char buff[100];
		//	sprintf(buff, "s=%.2f\nsurf=%s", (F32)Length(vel), Pat::GetSurfaceTypeName(pat.GetSurfaceType()));
		//	DebugDrawString(m_pStrainedSounds[ii].m_pos, buff, kColorWhite);
		//}
	}
}

void Rope2::UpdateHitSounds(NdGameObject* pOwner)
{
	if (!m_pHitSounds)
		return;

	if (m_bSleeping)
	{
		return;
	}

	F32 segRopeDist = 0.0f;
	F32 ropeDist = 0.0f;
	I32 iNode = 0;

	bool bHasEnd = m_soundDef.m_endHitSound != INVALID_STRING_ID_64;
	if (m_soundDef.m_hitSound != INVALID_STRING_ID_64)
	{
		F32 ropeLen = m_fLength;
		if (bHasEnd)
		{
			ropeLen -= kEndSoundRopeLength;
		}
		segRopeDist = ropeLen / (m_numHitSounds - (bHasEnd ? 1 : 0));
	}
	else
	{
		ropeDist = m_fLength - kEndSoundRopeLength;
		segRopeDist = kEndSoundRopeLength;
		while (iNode < m_numPoints && m_pRopeDist[iNode] < ropeDist)
			iNode++;
	}

	for (U32 ii = 0; ii<m_numHitSounds; ii++)
	{
		bool bIsEnd = bHasEnd && ii == m_numHitSounds-1;
		if (bIsEnd)
		{
			segRopeDist = kEndSoundRopeLength;
		}
		F32 ropeDist1 = ropeDist + segRopeDist;
		I32 iNode1 = iNode+1;
		while (iNode1 < m_numPoints && m_pRopeDist[iNode1] < ropeDist1)
			iNode1++;

		if (m_pHitSounds[ii].m_coolDown > 0.0f)
		{
			m_pHitSounds[ii].m_coolDown -= m_scStepTime;
		}
		else
		{
			F32 maxDVel2 = 0.0f;
			I32 maxNode = -1;
			U8 patSurface = 0;
			for (U32 jj = iNode; jj<iNode1; jj++)
			{
				if (IsNodeLooseInt(jj))
				{
					Vector vel = m_pLastVel[jj];
					m_pConstr[jj].RelaxVel(m_pPos[jj], vel, this, kZero, kZero);
					F32 dVel2 = LengthSqr(vel-m_pLastVel[jj]);
					if (dVel2 > maxDVel2)
					{
						maxDVel2 = dVel2;
						patSurface = m_pConstr[jj].m_patSurface;
						maxNode = jj;
					}
				}
			}

			F32 maxRopeDist = m_pRopeDist[maxNode];
			if (maxRopeDist < m_noHitSoundStartRopeDist || maxRopeDist > m_noHitSoundEndRopeDist)
			{
				// Also make sure we don't play 2 sounds too close to each other
				if (bIsEnd ||
					((ii == 0 || m_pHitSounds[ii-1].m_coolDown <= 0.0f || maxRopeDist-m_pHitSounds[ii-1].m_ropeDist > segRopeDist) && 
					(ii == m_numHitSounds-1 || m_pHitSounds[ii+1].m_coolDown <= 0.0f || m_pHitSounds[ii+1].m_ropeDist-maxRopeDist > segRopeDist)))
				{
					Point pos = bIsEnd ? m_pPos[m_numPoints-1] : m_pPos[maxNode];
					F32 impVel = Sqrt(maxDVel2);
					if (impVel > m_soundDef.m_minHitSpeed)
					{
						MutableProcessHandle hSound = NewProcess(SfxSpawnInfo(bIsEnd ? m_soundDef.m_endHitSound : m_soundDef.m_hitSound, pos, 0, pOwner));
						SendEvent(SID("set-variable"), hSound, CcVar("impact-velocity"), impVel, 0U);
						SendEvent(SID("set-variable"), hSound, CcVar("surface-type"), (F32)patSurface, 0U);
						m_pHitSounds[ii].m_coolDown = m_soundDef.m_hitCoolDown;
						m_pHitSounds[ii].m_ropeDist = bIsEnd ? m_fLength : maxRopeDist;
					}

					if (FALSE_IN_FINAL_BUILD(g_ropeMgr.m_debugDrawHitSounds))
					{
						if (impVel > 0.5f)
						{
							F32 debugTt = impVel > m_soundDef.m_minHitSpeed ? 1.0f : 0.1f;
							Color c = impVel > m_soundDef.m_minHitSpeed ? kColorGreen : kColorWhite;
							DebugDrawCross(pos, 0.03f, c, Seconds(debugTt));
							char strBuf[12];
							if (patSurface != 0)
							{
								snprintf(strBuf, 12, "%.2f (%s)", impVel, Pat::GetSurfaceTypeName((Pat::SurfaceType)patSurface));
							}
							else
							{
								snprintf(strBuf, 12, "%.2f", impVel);
							}
							DebugDrawString(pos, strBuf, c, Seconds(debugTt));
						}
					}
				}
			}
		}

		ropeDist = ropeDist1;
		iNode = iNode1;
	}
}

void Rope2::UpdateSlideSounds(NdGameObject* pOwner)
{
	if (!m_pSlideSounds)
		return;

	if (m_bSleeping)
	{
		for (U32 ii = 0; ii<m_numSlideSounds; ii++)
		{
			KillProcess(m_pSlideSounds[ii].m_hSfx);
		}
		return;
	}

	F32 segRopeDist = 0.0f;
	F32 ropeDist = 0.0f;
	I32 iNode = 0;

	bool bHasEnd = m_soundDef.m_endSlideSound != INVALID_STRING_ID_64;
	if (m_soundDef.m_slideSound != INVALID_STRING_ID_64)
	{
		F32 ropeLen = m_fLength;
		if (bHasEnd)
		{
			ropeLen -= kEndSoundRopeLength;
		}
		segRopeDist = ropeLen / (m_numSlideSounds - (bHasEnd ? 1 : 0));
	}
	else
	{
		ropeDist = m_fLength - kEndSoundRopeLength;
		segRopeDist = kEndSoundRopeLength;
		while (iNode < m_numPoints && m_pRopeDist[iNode] < ropeDist)
			iNode++;
	}

	for (U32 ii = 0; ii<m_numSlideSounds; ii++)
	{
		bool bIsEnd = bHasEnd && ii == m_numSlideSounds-1;
		if (bIsEnd)
		{
			segRopeDist = kEndSoundRopeLength;
		}
		F32 ropeDist1 = ropeDist + segRopeDist;
		I32 iNode1 = iNode+1;
		while (iNode1 < m_numPoints && m_pRopeDist[iNode1] < ropeDist1)
			iNode1++;

		F32 maxDVel2 = 0.0f;
		I32 maxNode = -1;
		U8 patSurface = 0;
		for (U32 jj = iNode; jj<iNode1; jj++)
		{
			if (IsNodeLooseInt(jj))
			{
				Vector vel = m_pLastVel[jj];
				m_pConstr[jj].RelaxVel(m_pPos[jj], vel, this, kZero, kZero);
				Vector noFrictionVel = vel;
				m_pConstr[jj].RelaxVel(m_pPos[jj], vel, this, 1.0f, 0.0f);
				F32 dVel2 = LengthSqr(vel-noFrictionVel);
				if (dVel2 > maxDVel2)
				{
					maxDVel2 = dVel2;
					patSurface = m_pConstr[jj].m_patSurface;
					maxNode = jj;
				}
			}
		}

		Point pos = bIsEnd ? m_pPos[m_numPoints-1] : m_pPos[(iNode+iNode1)/2];
		F32 slideVel = 0.0f;

		F32 maxRopeDist = m_pRopeDist[maxNode];
		if (maxRopeDist < m_noHitSoundStartRopeDist || maxRopeDist > m_noHitSoundEndRopeDist)
		{
			slideVel = Sqrt(maxDVel2);
			if (slideVel > m_soundDef.m_minSlideSpeed)
			{
				if (!m_pSlideSounds[ii].m_hSfx.HandleValid())
				{
					m_pSlideSounds[ii].m_hSfx = NewProcess(SfxSpawnInfo(bIsEnd ? m_soundDef.m_endSlideSound : m_soundDef.m_slideSound, pos, 0, pOwner));
				}
				m_pSlideSounds[ii].m_coolDown = 0.0f;
			}
		}

		m_pSlideSounds[ii].m_coolDown += m_scStepTime;
		if (m_pSlideSounds[ii].m_coolDown > m_soundDef.m_slideCoolDown)
		{
			KillProcess(m_pSlideSounds[ii].m_hSfx);
			m_pSlideSounds[ii].m_hSfx = nullptr;
		}
		if (m_pSlideSounds[ii].m_hSfx.HandleValid())
		{
			SendEvent(SID("set-position"), m_pSlideSounds[ii].m_hSfx, pos);
			SendEvent(SID("set-variable"), m_pSlideSounds[ii].m_hSfx, CcVar("slide-speed"), slideVel, 0U);
			SendEvent(SID("set-variable"), m_pSlideSounds[ii].m_hSfx, CcVar("surface-type"), (F32)patSurface, 0U);
		}

		if (FALSE_IN_FINAL_BUILD(g_ropeMgr.m_debugDrawSlideSounds))
		{
			if (slideVel > 0.01f)
			{
				Color c = slideVel > m_soundDef.m_minSlideSpeed ? kColorGreen : kColorWhite;
				DebugDrawCross(pos, 0.03f, c, kPrimDuration1FramePauseable);
				char strBuf[12];
				if (patSurface != 0)
				{
					snprintf(strBuf, 12, "%.2f (%s)", slideVel, Pat::GetSurfaceTypeName((Pat::SurfaceType)patSurface));
				}
				else
				{
					snprintf(strBuf, 12, "%.2f", slideVel);
				}
				DebugDrawString(pos, strBuf, c, kPrimDuration1FramePauseable);
			}
		}

		ropeDist = ropeDist1;
		iNode = iNode1;
	}
}

void Rope2::UpdateEdgeSlideSounds(NdGameObject* pOwner)
{
	if (!m_pEdgeSlideSounds)
		return;

	U32 iSfx = 0;
	while (iSfx<m_numEdgeSlideSounds)
	{
		if (!m_pEdgeSlideSounds[iSfx].m_hSound.HandleValid())
		{
			//ASSERT(m_pEdgeSlideSounds[iSfx].m_strength >= m_soundDef.m_minEdgeSlideStrength);
			m_pEdgeSlideSounds[iSfx].m_hSound = NewProcess(SfxSpawnInfo(m_soundDef.m_edgeSlideSound, pOwner));
		}

		if (m_pEdgeSlideSounds[iSfx].m_strength < m_soundDef.m_minEdgeSlideStrength)
		{
			m_pEdgeSlideSounds[iSfx].m_coolDown += m_scStepTime;
			if (m_pEdgeSlideSounds[iSfx].m_coolDown > m_soundDef.m_edgeSlideCoolDown)
			{
				KillProcess(m_pEdgeSlideSounds[iSfx].m_hSound);
				m_pEdgeSlideSounds[iSfx] = m_pEdgeSlideSounds[m_numEdgeSlideSounds-1];
				m_numEdgeSlideSounds--;
				continue;
			}
		}

		SendEvent(SID("set-position"), m_pEdgeSlideSounds[iSfx].m_hSound, m_pEdgeSlideSounds[iSfx].m_edgePos);
		SendEvent(SID("set-variable"), m_pEdgeSlideSounds[iSfx].m_hSound, CcVar("slide-strength"), m_pEdgeSlideSounds[iSfx].m_strength, 0U);
		SendEvent(SID("set-variable"), m_pEdgeSlideSounds[iSfx].m_hSound, CcVar("surface-type"), (F32)m_pEdgeSlideSounds[iSfx].m_patSurface, 0U);

		if (FALSE_IN_FINAL_BUILD(g_ropeMgr.m_debugDrawEdgeSlideSounds))
		{
			//DebugDrawLine(m_pEdgeSlideSounds[iSfx].m_edgePos - 0.25f*m_pEdgeSlideSounds[iSfx].m_edgeVec, m_pEdgeSlideSounds[iSfx].m_edgePos + 0.25f*m_pEdgeSlideSounds[iSfx].m_edgeVec, kColorRed, kPrimDuration1FramePauseable);
			DebugDrawCross(m_pEdgeSlideSounds[iSfx].m_edgePos, 0.1f, kColorRed, kPrimDuration1FramePauseable);
			if (m_pEdgeSlideSounds[iSfx].m_patSurface != 0)
			{
				g_prim.Draw(DebugStringFmt(m_pEdgeSlideSounds[iSfx].m_edgePos, kColorWhite, 0.7f, "%.3f (%s)", m_pEdgeSlideSounds[iSfx].m_strength, Pat::GetSurfaceTypeName((Pat::SurfaceType)m_pEdgeSlideSounds[iSfx].m_patSurface)), kPrimDuration1FramePauseable);
			}
			else
			{
				g_prim.Draw(DebugStringFmt(m_pEdgeSlideSounds[iSfx].m_edgePos, kColorWhite, 0.7f, "%.3f", m_pEdgeSlideSounds[iSfx].m_strength), kPrimDuration1FramePauseable);
			}
		}

		// Clear it now. Will be set to new value in PostSolve
		m_pEdgeSlideSounds[iSfx].m_strength = 0.0f;

		iSfx++;
	}
}

void Rope2::UpdateSnowFx()
{
	ScopedTempAllocator jj(FILE_LINE_FUNC);
	Point* pPos = NDI_NEW Point[kMaxSnowFx];
	memset(pPos, 0, sizeof(pPos[0])*kMaxSnowFx);

	I32 iFirstSnow = -1;
	for (I32 ii = 0; ii<m_numPoints; ii++)
	{
		bool nearCol = false;
		U8 snowFlags = m_pConstr[ii].m_snowFlags; // & m_pConstr[ii].m_flags;
		if (snowFlags)
		{
			for (U32 iEdge = 0; iEdge<m_pConstr[ii].m_numEdges; iEdge++)
			{
				if ((snowFlags & (1 << m_pConstr[ii].m_edgePlanes[iEdge*2])) || (snowFlags & (1 << m_pConstr[ii].m_edgePlanes[iEdge*2+1])))
				{
					U8 planeMask;
					Vec4 pl = m_pConstr[ii].GetEdgePlane(iEdge, m_pPos[ii], planeMask);
					nearCol = Dot4(pl, m_pPos[ii].GetVec4()) < 0.07f;
				}
			}
			for (U32 iPlane = m_pConstr[ii].m_firstNoEdgePlane; iPlane<m_pConstr[ii].m_numPlanes; iPlane++)
			{
				if (snowFlags & (1 << iPlane))
				{
					nearCol = Dot4(m_pConstr[ii].m_planes[iPlane], m_pPos[ii].GetVec4()) < 0.07f;
				}
			}
		}
		if (nearCol)
		{
			if (iFirstSnow < 0)
			{
				iFirstSnow = ii;
				if (m_iFirstSnowFx >= 0)
				{
					I32 dif = iFirstSnow - m_iFirstSnowFx;
					if (Abs(dif) < kMaxSnowFx)
					{
						if (dif > 0)
						{
							memcpy(pPos, m_pSnowFxPos+dif, (kMaxSnowFx-dif)*sizeof(pPos[0]));
						}
						else
						{
							memcpy(pPos-dif, m_pSnowFxPos, (kMaxSnowFx+dif)*sizeof(pPos[0]));
						}
					}
				}
			}
			else if (ii-iFirstSnow >= kMaxSnowFx)
			{
				break;
			}

			// Check if we already spawned nearby
			bool spawn = true;
			I32 iSnow = ii-iFirstSnow;
			for (I32 iOldCheck = Max(iSnow-2, 0); iOldCheck<=Min(iSnow+3, kMaxSnowFx); iOldCheck++)
			{
				if (DistSqr(m_pPos[ii], pPos[iOldCheck]) < Sqr(0.02f))
				{
					spawn = false;
					break;
				}
			}

			if (spawn)
			{
				SpawnParticle(m_pPos[ii], m_fxDef.m_snowContactFx);
				//DebugDrawCross(m_pPos[ii], 0.03f, kColorRed, Seconds(0.3f));
				pPos[iSnow] = m_pPos[ii];
			}
		}
	}

	memcpy(m_pSnowFxPos, pPos, kMaxSnowFx*sizeof(pPos[0]));
	m_iFirstSnowFx = iFirstSnow;
}

void Rope2::UpdateFx()
{
	// This is exact copy of the UpdateSound but for Fx. If this really will be the same we should refactor it into one func
	// But I suspect it will diverge soon

	if (m_fxDef.m_snowContactFx)
		UpdateSnowFx();

	if (!m_pStrainedFx)
		return;

	if (m_bSleeping)
	{
		for (U32F ii = 0; ii<m_numStrainedFx; ii++)
		{
			KillParticle(m_pStrainedFx[ii].m_hFx, true);
			m_pStrainedFx[ii].m_hFx = ParticleHandle();
		}
		m_numStrainedFx = 0;
		return;
	}

	// Try matching the old Fx with new edges
	{
		U32F nextEdgePoint = 1;
		for (U32F ii = 0; ii<m_numStrainedFx; ii++)
		{
			const RopeColliderHandle& shape = m_pStrainedFx[ii].m_colEdgeId.m_shape;
			Locator loc = shape.GetLocator(this);
			Locator prevLoc = shape.GetPrevLocator(this);
			Point movedPos = loc.TransformPoint(prevLoc.UntransformPoint(m_pStrainedFx[ii].m_pos));

			F32 closestDist2 = FLT_MAX;
			I32 closestEdge = -1;
			bool exactMatch = false;
			for (U32F iEdgePoint = nextEdgePoint; iEdgePoint<m_numEdges-1; iEdgePoint++)
			{
				const EdgePoint& ePoint = m_pEdges[iEdgePoint];
				if (ePoint.m_flags & kNodeStrained)
				{
					bool shapeMatch = false;
					for (U32F iEdge = 0; iEdge<ePoint.m_numEdges; iEdge++)
					{
						if (ePoint.GetEdgeActive(iEdge))
						{
							RopeColEdgeId edgeId;
							m_colCache.GetEdgeId(ePoint.m_edgeIndices[iEdge], edgeId);
							if (m_pStrainedFx[ii].m_colEdgeId.m_shape == edgeId.m_shape)
							{
								if (m_pStrainedFx[ii].m_colEdgeId.m_triKey == edgeId.m_triKey && m_pStrainedFx[ii].m_colEdgeId.m_edgeIndex == edgeId.m_edgeIndex)
								{
									closestDist2 = 0.0f;
									closestEdge = iEdgePoint;
									exactMatch = true;
									break;
								}
								shapeMatch = true;
							}
						}
					}
					if (exactMatch)
					{
						break;
					}

					if (shapeMatch)
					{
						F32 dist2 = DistSqr(ePoint.m_pos, movedPos);
						if (dist2 < closestDist2)
						{
							closestDist2 = dist2;
							closestEdge = iEdgePoint;
						}
					}
				}
			}

			if (closestEdge >= 0)
			{
				const EdgePoint& ePoint = m_pEdges[closestEdge];
				m_pStrainedFx[ii].m_pos = ePoint.m_pos;
				m_pStrainedFx[ii].m_ropeDist = ePoint.m_ropeDist;
				m_pStrainedFx[ii].m_edgePointIndex = closestEdge;
				if (!exactMatch)
				{
					RopeColEdgeId minId;
#if HAVOKVER < 0x2016
					minId.m_triKey = 0xffffffff;
#endif
					for (U32F iEdge = 0; iEdge<ePoint.m_numEdges; iEdge++)
					{
						if (ePoint.GetEdgeActive(iEdge))
						{
							RopeColEdgeId id;
							m_colCache.GetEdgeId(ePoint.m_edgeIndices[iEdge], id);
							if (id.m_triKey <= minId.m_triKey)
							{
								// For improved PAT consistency we always choose the edge that has min triangle key
								minId = id;
							}
						}
					}
					m_pStrainedFx[ii].m_colEdgeId = minId;
				}
				nextEdgePoint = closestEdge+1;
			}
			else
			{
				m_pStrainedFx[ii].m_ropeDist = -1.0f;
			}
		}
	}

	// Discard Fx that are too close to each other
	{
		F32 prevRopeDist = -FLT_MAX;
		for (U32F ii = 0; ii<m_numStrainedFx; ii++)
		{
			if (m_pStrainedFx[ii].m_ropeDist >= 0.0f)
			{
				if (m_pStrainedFx[ii].m_ropeDist - prevRopeDist < m_fxDef.m_strainedFxDist)
				{
					m_pStrainedFx[ii].m_ropeDist = -1.0f;
				}
				else
				{
					prevRopeDist = m_pStrainedFx[ii].m_ropeDist;
				}
			}
		}
	}

	// Recreate a new array of Fx and adding new ones where fit
	{
		ScopedTempAllocator jj(FILE_LINE_FUNC);
		StrainedFx* pFx = NDI_NEW StrainedFx[m_maxNumStrainedFx];
		U32F numFx = 0;
		U32F iOldFx = 0;
		while (iOldFx < m_numStrainedFx && m_pStrainedFx[iOldFx].m_ropeDist < 0.0f)
			iOldFx++;
		F32 prevRopeDist = -FLT_MAX;
		F32 nextRopeDist = iOldFx < m_numStrainedFx ? m_pStrainedFx[iOldFx].m_ropeDist : FLT_MAX;
		for (U32F iEdgePoint = 1; iEdgePoint<m_numEdges-1; iEdgePoint++)
		{
			const EdgePoint& ePoint = m_pEdges[iEdgePoint];
			if ((ePoint.m_flags & kNodeStrained) && ePoint.m_activeEdges > 0 && ePoint.m_ropeDist - prevRopeDist > m_fxDef.m_strainedFxDist && nextRopeDist - ePoint.m_ropeDist > m_fxDef.m_strainedFxDist)
			{
				RopeColEdgeId minId;
#if HAVOKVER < 0x2016
				minId.m_triKey = 0xffffffff;
#endif
				for (U32F iEdge = 0; iEdge<ePoint.m_numEdges; iEdge++)
				{
					RopeColEdgeId id;
					m_colCache.GetEdgeId(ePoint.m_edgeIndices[iEdge], id);
					if (id.m_triKey <= minId.m_triKey)
					{
						// For improved PAT consistency we always choose the edge that has min triangle key
						minId = id;
					}
				}

				PHYSICS_ASSERT(numFx < m_maxNumStrainedFx);
				StrainedFx& fx = pFx[numFx];
				numFx++;

				// Find an old unused sound or spawn a new one
				U32F iFreeFx;
				for (iFreeFx= 0; iFreeFx<m_numStrainedFx; iFreeFx++)
				{
					if (m_pStrainedFx[iFreeFx].m_ropeDist < 0.0f && m_pStrainedFx[iFreeFx].m_hFx.IsValid())
						break;
				}
				if (iFreeFx < m_numStrainedFx)
				{
					fx.m_hFx = m_pStrainedFx[iFreeFx].m_hFx;
					fx.m_fxId = m_pStrainedFx[iFreeFx].m_fxId;
				}

				fx.m_colEdgeId = minId;
				fx.m_pos = ePoint.m_pos;
				fx.m_ropeDist = ePoint.m_ropeDist;
				fx.m_edgePointIndex = iEdgePoint;
				prevRopeDist = ePoint.m_ropeDist;
			}
			else if (nextRopeDist - ePoint.m_ropeDist < m_fxDef.m_strainedFxDist)
			{
				// Copy over old sound
				PHYSICS_ASSERT(numFx < m_maxNumStrainedFx);
				pFx[numFx] = m_pStrainedFx[iOldFx];
				m_pStrainedFx[iOldFx].m_hFx = ParticleHandle();
				numFx++;
				prevRopeDist = nextRopeDist;
				iOldFx++;
				while (iOldFx < m_numStrainedFx && m_pStrainedFx[iOldFx].m_ropeDist < 0.0f)
					iOldFx++;
				nextRopeDist = iOldFx < m_numStrainedFx ? m_pStrainedFx[iOldFx].m_ropeDist : FLT_MAX;
			}
		}

		// Kill discarded Fx
		for (U32F ii = 0; ii<m_numStrainedFx; ii++)
		{
			if (m_pStrainedFx[ii].m_hFx.IsValid())
			{
				KillParticle(m_pStrainedFx[ii].m_hFx, true);
				m_pStrainedFx[ii].m_hFx = ParticleHandle();
			}
		}

		// Copy over the new array
		memcpy(m_pStrainedFx, pFx, numFx * sizeof(pFx[0]));
		m_numStrainedFx = numFx;
	}

	// Update sound params
	for (U32F ii = 0; ii<m_numStrainedFx; ii++)
	{
		StrainedFx& fx = m_pStrainedFx[ii];

		const RopeColEdgeId& edgeId = fx.m_colEdgeId;
		const RopeColliderHandle& shape = edgeId.m_shape;

		Vector normal = GetEdgePointCollisionNormal(fx.m_edgePointIndex);
		PHYSICS_ASSERT(fx.m_edgePointIndex < m_numEdges-1);
		Vector dir = SafeNormalize(m_pEdges[fx.m_edgePointIndex+1].m_pos - m_pEdges[fx.m_edgePointIndex].m_pos, -Vector(kUnitYAxis));

		Locator loc(fx.m_pos, QuatFromLookAtDirs(dir, normal));

		Pat pat(0);
		if (const RigidBody* pBody = shape.GetRigidBody())
		{
			hknpShapeKey shapeKey;
			if (shape.GetListIndex() >= 0)
			{
				PHYSICS_ASSERT(!edgeId.m_triKey.isValid()); // a mesh inside a list shape? Yuck!
				shapeKey = hknpShapeKey(shape.GetListIndex());
			}
			else
			{
				shapeKey = edgeId.m_triKey;
			}
			pat = HavokGetPatFromHkRigidBody(pBody->GetHavokBody(), shapeKey);
		}

		StringId64 fxId = ((m_fxDef.m_strainedSlideFxMud != INVALID_STRING_ID_64)
						   && (pat.GetSurfaceType() == Pat::kSurfaceTypeMud || pat.GetSurfaceType() == Pat::kSurfaceTypeMudSlippery)) ? m_fxDef.m_strainedSlideFxMud : m_fxDef.m_strainedSlideFx;
		if (fx.m_hFx.IsValid() && fx.m_fxId != fxId)
		{
			KillParticle(fx.m_hFx, true);
		}
		if (!fx.m_hFx.IsValid())
		{
			fx.m_hFx = SpawnParticle(loc, fxId);
			fx.m_fxId = fxId;
		}
		else
		{
			g_particleMgr.SetLocation(fx.m_hFx, loc);
		}

		// Rope vel
		Vector vel = GetVel(fx.m_ropeDist);

		// Edge vel
		Locator bodyLoc = shape.GetLocator(this);
		Locator prevBodyLoc = shape.GetPrevLocator(this);
		Point prevPos = prevBodyLoc.TransformPoint(bodyLoc.UntransformPoint(fx.m_pos));
		Vector edgeVel = (fx.m_pos - prevPos) * m_scInvStepTime;

		vel -= edgeVel;
		g_particleMgr.SetVector(fx.m_hFx, SID("ropevel"), vel);

		//{
		//	g_prim.Draw(DebugCoordAxes(loc, 0.05f));
		//	g_prim.Draw(DebugArrow(loc.GetTranslation(), loc.GetTranslation() + vel));
		//	char buff[100];
		//	sprintf(buff, "surf=%s", Pat::GetSurfaceTypeName(pat.GetSurfaceType()));
		//	DebugDrawString(fx.m_pos, buff, kColorWhite);
		//}
	}
}

void Rope2::CheckStepNotRunning() const
{
	STRIP_IN_FINAL_BUILD;

	if (m_stepJobId < 0)
		return;

	if (m_stepJobId == ndjob::GetActiveJobId())
		return;

	ndjob::JobDecl jobDecl;
	bool isJob = ndjob::GetActiveJobDecl(&jobDecl);
	if (jobDecl.m_pStartFunc == SolveLooseSectionJob)
		return;

	PHYSICS_ASSERT(false); // Invalid access to the rope data during rope step
}

bool CheckRopeEdgePat(const Rope2::EdgePointInfo& info, Pat patMask, EdgeOrientation orient)
{
	HavokMarkForReadJanitor jj;

	for (U32F ii = 0; ii < info.m_numEdges; ii++)
	{
		const Rope2::EdgeInfo& edgeInfo = info.m_edges[ii];
		if ((orient == EdgeOrientation::kPositive && !edgeInfo.m_positive) || (orient == EdgeOrientation::kNegative && edgeInfo.m_positive))
		{
			continue;
		}

		const RigidBody* pRigidBody = edgeInfo.m_hRigidBody.ToBody();
		if (pRigidBody != nullptr)
		{
			Pat pat = HavokGetPatFromHkRigidBody(pRigidBody->GetHavokBody(), edgeInfo.m_shapeKey);
			if (patMask.m_bits & pat.m_bits)
				return true;
		}
	}

	return false;
}

