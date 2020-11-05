/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef NDPHYS_ROPE2_H
#define NDPHYS_ROPE2_H

#include "corelib/math/gamemath.h"
#include "corelib/math/aabb.h"
#include "gamelib/ndphys/collision-filter.h"
#include "ndlib/process/process.h"
#include "ndlib/render/ndgi/ndgi.h"
#include "ndlib/render/ngen/buffers.h"
#include "ndlib/ndphys/rigid-body-base.h"
#include "ndlib/ndphys/rope2-base.h"
#include "gamelib/ndphys/rope/rope2-col.h"
#include "gamelib/ndphys/rope/rope2-collider.h"
#include "gamelib/render/particle/particle-handle.h"

#if !FINAL_BUILD
#define ROPE_DEBUG_DRAW	1
#define ROPE_DEBUG_DRAW_DETAILED 0
#else
#define ROPE_DEBUG_DRAW	ALLOW_ROPE_DEBUGGER_IN_FINAL
#define ROPE_DEBUG_DRAW_DETAILED 0
#endif

#define MOVING_EDGES 0
#define MOVING_EDGES2 1

struct Rope2Collector;
class Rope2Debugger;
struct EdgeHit;
class hknpBoxShape;
class hknpCompressedMeshShape;
class hknpConvexPolytopeShape;
struct RopeMultiNewNode;
class HavokCastFilter;
struct RopeBonesData;
class Rope2PointCol;

struct Rope2InitData
{
	F32 m_length;
	F32 m_radius;
	F32 m_segmentLength;
	F32 m_minSegmentFraction;
	bool m_neverStrained;
	bool m_allowKeyStretchConstraints;
	bool m_withSavePos;
	bool m_enableSolverFriction;
	bool m_enablePostSolve;
	bool m_useTwistDir;

	Rope2InitData()
		: m_minSegmentFraction(0.5f)
		, m_neverStrained(false)
		, m_allowKeyStretchConstraints(false)
		, m_withSavePos(false)
		, m_enableSolverFriction(false)
		, m_enablePostSolve(false)
		, m_useTwistDir(false)
	{}
};

struct Rope2NetData
{
	static const int kMaxDataPoints = 8;

	U32 m_numPoints;
	Point m_points[kMaxDataPoints];

	Rope2NetData() : m_numPoints(0) { }
};

struct Rope2SoundDef
{
	Rope2SoundDef()
		: m_strainedSlideSound(INVALID_STRING_ID_64)
		, m_hitSound(INVALID_STRING_ID_64)
		, m_endHitSound(INVALID_STRING_ID_64)
		, m_slideSound(INVALID_STRING_ID_64)
		, m_endSlideSound(INVALID_STRING_ID_64)
		, m_edgeSlideSound(INVALID_STRING_ID_64)
		, m_strainedSoundDist(1.0f)
		, m_hitSoundDist(1.0f)
		, m_minHitSpeed(1.0f)
		, m_hitCoolDown(0.5f)
		, m_slideSoundDist(1.0f)
		, m_minSlideSpeed(1.0f)
		, m_slideCoolDown(1.0f)
		, m_minEdgeSlideStrength(0.1f)
		, m_edgeSlideCoolDown(0.5f)
		, m_endAudioConfig(false)
	{
	}

	StringId64 m_strainedSlideSound;
	StringId64 m_hitSound;
	StringId64 m_endHitSound;
	StringId64 m_slideSound;
	StringId64 m_endSlideSound;
	StringId64 m_edgeSlideSound;
	F32 m_strainedSoundDist;
	F32 m_hitSoundDist;
	F32 m_minHitSpeed;
	F32 m_hitCoolDown;
	F32 m_slideSoundDist;
	F32 m_minSlideSpeed;
	F32 m_slideCoolDown;
	F32 m_minEdgeSlideStrength;
	F32 m_edgeSlideCoolDown;
	bool m_endAudioConfig;
};

struct Rope2FxDef
{
	Rope2FxDef()
		: m_strainedSlideFx(INVALID_STRING_ID_64)
		, m_strainedSlideFxMud(INVALID_STRING_ID_64)
		, m_snowContactFx(INVALID_STRING_ID_64)
		, m_strainedFxDist(1.0f)
	{
	}

	StringId64 m_strainedSlideFx;
	StringId64 m_strainedSlideFxMud;
	StringId64 m_snowContactFx;
	F32 m_strainedFxDist;
};

static const F32 kEdgeTol = 0.001f;
static const F32 kEdgeTol2 = kEdgeTol * kEdgeTol;
static const Scalar kScEdgeTol(kEdgeTol);
static const Scalar kScEdgeTol2(kEdgeTol2);

class Rope2 : public Rope2Base
{
public:
	class Constraint
	{
	public:
		enum {
			kMaxNumPlanes = 4,
			kMaxNumEdges = 3
		};
		enum ConstraintFlags
		{
			kIsWorking1				= 0x01,
			kIsWorking2				= 0x02,
			kIsWorking3				= 0x04,
			kIsWorking4				= 0x08
		};
		U8 m_numPlanes;
		U8 m_firstNoEdgePlane;
		U8 m_numEdges;
		U8 m_flags;
		union 
		{
			U16 m_collisionFlags;
			struct 
			{
				U16 m_canGrappleFlags : 4;
				U16 m_snowFlags : 4;
				U16 m_frictionlessFlags : 4;
			};
		};
		U8 m_patSurface;
		U8 m_edgePlanes[kMaxNumEdges*2];
		SMath::Vec4 m_planes[kMaxNumPlanes];
		SMath::Vec4 m_biPlanes[kMaxNumEdges*2];
		RopeColliderHandle m_hCollider[kMaxNumPlanes];

		void Reset(bool bKeepWorkingFlags) { m_numPlanes = 0; m_firstNoEdgePlane = 0; m_numEdges = 0; m_flags = bKeepWorkingFlags ? m_flags : 0; m_collisionFlags = 0; m_patSurface = 0; }
		I32 AddPlane(const Vec4& plane, const RopeColliderHandle& hCollider);
		I32 AddEdge(I32& iPl0, I32& iPl1);
		void RemoveEdge(U32 iEdge, U32* pPlaneRemap = nullptr, U32 numPlaneRemaps = 0);
		void RemovePlane(U32 iPlane, U32* pRemap = nullptr, U32 numRemaps = 0);
		void RemoveConstraintBreakingPlane(U32 iPlane);
		Vec4 GetEdgePlane(U32 iEdge, const Point& refPos, U8& planeMask) const;
		Vec4 RelaxPos(Point &refPos, F32 radius);
		void RelaxVel(const Point& refPos, Vector& refVel, Rope2* pOwner, Scalar_arg scProporFriction, Scalar_arg scConstFriction) const;
		I32 GetOtherEdgePlane(U32 iPlane) const;

		bool GetLineSegmentEdgePoint(const Point& p0, const Point& p1, Point& edgePoint, F32& t);

		bool HasCollider(const RopeColliderHandle& hCollider);
		void PreCollision(Rope2* pRope, U32F iPoint, const Point& refPos, F32 radius, Collide::LayerMask excludeLayerMask, bool bKeepWorkingFlags);
		void SortPlanesForPersistency(const Constraint& prev, const Point& refPos);
		void FixCompetingConstraints(const Point& refPos, F32 radius);
		void SlowDepenetration(const Point& refPos, F32 radius, F32 dt);

		void Validate();
	};

	class EdgePoint
	{
	public:
		enum{
			kMaxNumEdges = 16
		};
		Point m_pos;
		F32 m_ropeDist;
		RopeNodeFlags m_flags;
		U8 m_internFlags;
#if ROPE_FREE_EDGES
		Constraint m_constr;
#endif
		U8 m_numEdges;
		U16 m_edgeIndices[kMaxNumEdges];
		U16 m_activeEdges;
		U16 m_activeEdges0;

		// Edge is "positive" if the normal points towards the beginning of the rope and the binormal towards the end
		U16 m_edgePositive;

		U16 m_slideOff;

		U8 m_numNewEdges;

		void Reset();
		bool GetEdgeActive(U32F ii) const { return (m_activeEdges >> ii) & 1U; }
		void SetEdgeActive(U32F ii, bool active) { if (active) m_activeEdges |= 1U << ii; else m_activeEdges &= ~(1U << ii); }
		bool GetEdgePositive(U32F ii) const { return (m_edgePositive >> ii) & 1U; }
		void SetEdgePositive(U32F ii, bool active) { if (active) m_edgePositive |= 1U << ii; else m_edgePositive &= ~(1U << ii); }
		bool GetEdgeSlideOff(U32F ii) const { return (m_slideOff >> ii) & 1U; }
		void SetEdgeSlideOff(U32F ii, bool active) { if (active) m_slideOff |= 1U << ii; else m_slideOff &= ~(1U << ii); }
		bool IsEdgeNew(U32F ii) const { return ii >= m_numEdges - m_numNewEdges; }
		void RemoveEdge(U32F ii);
		bool AddEdge(U16 edgeIndex, bool active);
		bool CopyEdge(const Rope2::EdgePoint& otherEPoint, U16 index);
		I32F FindEdge(U32F edgeIndex);
	};

	struct EdgeInfo
	{
		Point m_pnt;
		Vector m_vec;
		Scalar m_length;
		Vector m_normal;
		bool m_positive;
		RigidBodyHandle m_hRigidBody;
		hknpShapeKey m_shapeKey;
	};

	struct EdgePointInfo
	{
		U32F m_numEdges;
		EdgeInfo m_edges[EdgePoint::kMaxNumEdges];
	};

	enum { kMaxNumKeyPoints = 30 };

	enum InternEdgeFlags
	{
		kNodeFreeToStrained =	0x01,
		kNodeSplit =			0x02,
		kNodeSlideOffPending =	0x04,
		kNodeMerge =			0x08,
		kNodeNewActivation =	0x10,
	};

	static const char* NodeFlagToString(NodeFlags f);
	void NodeFlagsToString(RopeNodeFlags f, char* outText, I32 maxLen);

	// Edge intersection topology class
	enum EdgeIntersection
	{
		kIntNone = 0,					// is not edge intersection or is of unclassified type
		kIntInnerCorner,				// corner in which the rope is stuck
		kIntInnerCorner0,				// inner corner but the rope is moving away along edge 0
		kIntInnerCorner1,				// inner corner but the rope is moving away along edge 1
		kIntInnerCorner01,				// unstable inner corner, rope can move away along any of the two edges
		kIntOuterCorner01,				// corner over which the rope slides; first over edge 0 second edge 1
		kIntOuterCorner10,				// corner over which the rope slides; first over edge 0 second edge 1
		kIntInnerT0,					// edge 0 can be discarded
		kIntInnerT1,					// edge 1 can be discarded
		kIntSame,						// parallel and close edges so that they can be considered one and the same edge
	};

	typedef EdgeIntersection EdgeIntersectionMatrix[EdgePoint::kMaxNumEdges][EdgePoint::kMaxNumEdges];

	struct StrainedSound
	{
		RopeColEdgeId m_colEdgeId;
		Point m_pos;
		F32 m_ropeDist;
		MutableProcessHandle m_hSound;
	};

	struct HitSound
	{
		F32 m_coolDown;
		F32 m_ropeDist;
	};

	struct SlideSound
	{
		MutableProcessHandle m_hSfx;
		F32 m_coolDown;
	};

	struct EdgeSlideSound
	{
		MutableProcessHandle m_hSound;
		Point m_edgePos;
		//Vector m_edgeVec;
		F32 m_coolDown;
		F32 m_strength;
		U8 m_patSurface;
	};

	struct StrainedFx
	{
		RopeColEdgeId m_colEdgeId;
		Point m_pos;
		F32 m_ropeDist;
		U32 m_edgePointIndex;
		StringId64 m_fxId;
		ParticleHandle m_hFx;
	};

	enum
	{
		kMaxNumIgnoreCollisionBodies = 10,
		kMaxNumCustomColliders = 15,
		kMaxNumRigidBodyColliders = 5
	};

	float m_fLength;
	float m_fRadius; // rope radius
	float m_fMaxRadius; // rope max radius
	float m_fStrainedCollisionOffset; // how much get edge points after strained rope solve get offseted from collision
	float m_fDamping; // 0..1, per-second damping of the velocity of the rope, 0.2 is a good value
	float m_fViscousDamping; // unlimited; damps proportionally to Vel^2
	float m_fBendingStiffness; // 0..1, kind of "tau" value for bending
	float m_fFreeBendAngle; // min angle for the bending stiffness to kick in
	float m_fProporFriction;
	float m_fConstFriction;
	float m_fGravityFactor;
	float m_fAabbExpansion;
	Vector m_vGravityOffset;
	bool m_bAutoStrained;
	bool m_bNeverStrained;
	bool m_bSimpleRope;
	bool m_bDistanceConstraintEnabled;
	bool m_bAllowDistStretchConstraints;
	bool m_bNeverSleeps;
	bool m_bBuoyancy;
	bool m_bSolverFriction;
	bool m_bPostSolve;
	bool m_bCritical;
	bool m_bCustomCollidersColCache;

	float m_fSegmentLength; // the desired distance between nodes

	float m_fMassiveEndDist;
	float m_fMassiveEndRatio;

	float m_userDistanceConstraint;
	float m_userDistanceConstraintBlend;

	float m_fNumItersPerEdge;
	float m_fNumMultiGridItersPerEdge;

	float m_fBendingMinE;
	float m_fBendingMinMR;

	bool m_bInited;
	bool m_bSleeping;

	F32 m_minCharCollisionDist; // rope only collides with chars past this rope dist

	Collide::LayerMask m_layerMask;
	Collide::LayerMask m_strainedLayerMask;
	NdGameObjectHandle m_hFilterIgnoreObject;

	Rope2SoundDef m_soundDef;
	Rope2FxDef m_fxDef;

	F32 m_noHitSoundStartRopeDist;
	F32 m_noHitSoundEndRopeDist;

	const char * m_pDebugName; // for debugging only
	const Process* m_pOwner; // for debugging only

	static void Startup();

	Rope2();
	~Rope2();
	void Destroy();
	void Init(Rope2InitData& data);
	void InitExternSaveEdges();
	void InitSounds(const Rope2SoundDef* pDef);
	void InitFx(const Rope2FxDef* pDef);
	void InitForSimpleStrainedEdgeDetection(Rope2InitData& data);
	void InitRopeDebugger(bool onlyIfFree = false);
	void SetDebugger(Rope2Debugger* pDebugger);
	void InitStraightPose(const Point &ptStart, const Point &ptEnd);
	void ResetSim();
	bool IsInitialized() const { return m_bInited; }
	void ZeroVelocity() { CheckStepNotRunning(); memset(m_pVel, 0, sizeof(m_pVel[0]) * m_numPoints); }
	void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound);

	// Simple means there are no strained sections, no edge detection
	// and the position and flags of all keyframed points are not changing
	void SetSimple(bool b);

	void SetUseGpu(bool useGpu, bool emulOnCpu = false) { m_useGpu = useGpu; m_emulOnCpu = emulOnCpu; }
	void PreStep();
	void Step();
	void UpdatePaused();

	// Call this if you have set the position of all sim nodes manually and don't want to simulate
	void PreSetKeyframedPos();
	void StepKeyframed();

	void DebugDraw();

	void WakeUp();
	void GoToSleep();

	bool IsNodeKeyframed(U32 ii) const { CheckStepNotRunning(); return (m_pNodeFlags[ii]&kNodeKeyframed) != 0; }
	bool IsNodeStrained(U32 ii) const { CheckStepNotRunning(); return (m_pNodeFlags[ii]&kNodeStrained) != 0; }
	bool IsNodeEdge(U32 ii) const { CheckStepNotRunning(); return (m_pNodeFlags[ii]&kNodeEdge) != 0; }
	bool IsNodeLoose(U32 ii) const { CheckStepNotRunning(); return (m_pNodeFlags[ii]&(kNodeKeyframed|kNodeStrained)) == 0; }

	void SetKeyframedPos(F32 ropeDist, const Point& pos, RopeNodeFlags flags = 0);
	void SetKeyframedPos(F32 ropeDist, const Point& pos, const Vector& vel, RopeNodeFlags flags = 0);
	void SetKeyRadius(F32 ropeDist, F32 radius); // quite specific and not fully implemented yet
	void SetKeyTwistDir(F32 ropeDist, const Vector& dir);
	void ClearAllKeyframedPos() { CheckStepNotRunning(); m_numKeyPoints = 0; m_numKeyRadius = 0; }

	void SetSaveEdgesPos(Point_arg pos);
	void SetSaveEdgesPosRopeDist(F32 ropeDist);
	void SetSaveStartEdgesPos(Point_arg pos);
	bool HasSaveStartEdgesPos() const { return m_saveStartEdgePosSet; }
	void SetNumSaveEdges(U32F numEdges);
	void SetSaveEdge(U32F ii, const EdgePoint& ePoint);
	void SetNumExternSaveEdges(U32F numEdges);
	void SetExternSaveEdge(U32F ii, const EdgePoint& ePoint);

	I32F GetNumKeyPoints() const { return m_numKeyPoints; }
	Point GetKeyPos(U32F iKey) const { PHYSICS_ASSERT(iKey < m_numKeyPoints); return m_pKeyPos[iKey]; }
	Vector GetKeyVel(U32F iKey) const { PHYSICS_ASSERT(iKey < m_numKeyPoints); return m_pKeyVel[iKey]; }
	F32 GetKeyRopeDist(U32F iKey) const { PHYSICS_ASSERT(iKey < m_numKeyPoints); return m_pKeyRopeDist[iKey]; }
	RopeNodeFlags GetKeyNodeFlags(U32F iKey) const { PHYSICS_ASSERT(iKey < m_numKeyPoints); return m_pKeyNodeFlags[iKey]; }

	Point GetRoot() const { CheckStepNotRunning(); return m_pPos[0]; }
	Point GetEnd() const { CheckStepNotRunning(); return m_pPos[m_numPoints-1]; }
	Point GetPos(F32 ropeDist) const;
	void GetPosAndDir(F32 ropeDist, Point& posOut, Vector& dirOut, Vector_arg dirFallback = -Vector(kUnitYAxis)) const;
	void GetPosAndDirSmooth(F32 ropeDist, Point& posOut, Vector& dirOut, Vector_arg dirFallback) const;
	Vector GetVel(F32 ropeDist) const;
	void GetPosVelAndLast(F32 ropeDist, Point& pos, Point& lastPos, Vector& vel, Vector& lastVel) const;
	void GetPosAndVel(F32 ropeDist, Point& pos, Vector& vel) const;

	const RopeColCache& GetColCache() const { return m_colCache; }

	F32 Raycast(Point from, Point to, F32 tol, U32& pointIndex) const;
	Point ClosestPointOnRope(F32 minDist, const Segment& checkSeg, F32& retDist, F32* pRopeDistOut = nullptr) const;

	// GetSimPos and GetSimVel do not return const pointers for more flexibility but make sure you know what you're doing
	// if you modify these values
	I32F GetNumSimPoints() const { CheckStepNotRunning(); return m_numPoints; }
	const F32* GetSimRopeDist() const { CheckStepNotRunning(); return m_pRopeDist; }
	Point* GetSimPos() const { CheckStepNotRunning(); return m_pPos; }
	Vector* GetSimVel() const { CheckStepNotRunning(); return m_pVel; }
	const RopeNodeFlags* GetSimNodeFlags() const { CheckStepNotRunning(); return m_pNodeFlags; }
	I32F AddSimPoint(F32 ropeDist);
	void ResetSimPos(U32 ii, Point_arg pos);

	const Constraint& GetNodeConstraint(U32 ii) const { return m_pConstr[ii]; }

	// This is for clear teleport where we have unique frame of reference. Dynamics is preserved
	void Teleport(const Locator& loc);
	// This is more aggressive for when some attach point may have teleported and we just don't want to freak out
	void ResetDynamics();
	// For quick reeling in
	void ReelTeleport(F32 reelDist);

	Point GetStraightPos(F32 ropeDist) const;
	F32 GetStraightDist(F32 ropeDistFrom, F32 ropeDistTo);
	// In case you're about to keyframe points from and to
	F32 GetStraightDist(F32 ropeDistFrom, const Point& from, F32 ropeDistTo, const Point& to);

	I32F GetNumEdges() const { CheckStepNotRunning(); return m_numEdges; }
	const EdgePoint* GetEdges() const { CheckStepNotRunning(); return m_pEdges; }
	const EdgePoint& GetEdgeByIndex(I32 index) const { CheckStepNotRunning(); PHYSICS_ASSERT(index >= 0 && index < m_numEdges); return m_pEdges[index]; }
	void GetEdgeInfo(U32F iEdge, EdgePointInfo& info) const;
	I32 GetEdgeIndexByFlags(RopeNodeFlags flags) const;
	void SetNumEdges(U32F numEdges);
	I32F SetEdgePoint(F32 ropeDist, Point_arg pos, RopeNodeFlags flags = 0);
	Vector GetEdgePointCollisionNormal(U32F ii);
	Point GetEdgePosOffsetedFromCollision(U32F ii);

	I32F GetNumSaveEdges() const { return m_numSaveEdges; }
	const EdgePoint* GetSaveEdges() const { return m_pSaveEdges; }

	void GetAabb(Aabb& aabb) const;

	void AddIgnoreCollisionBody(const RigidBody* pBody);
	void RemoveIgnoreCollisionBody(const RigidBody* pBody);
	bool GetIgnoreCollision(const RigidBody* pBody) const;

	void AddCustomCollider(const RopeCollider* pCollider);
	void RemoveCustomCollider(const RopeCollider* pCollider);
	void RemoveAllCustomColliders();
	U32 GetNumCustomColliders() const { return m_numCustomColliders; }
	const RopeCollider* GetCustomCollider(U32F index) const { PHYSICS_ASSERT(index < m_numCustomColliders); return m_ppCustomColliders[index]; }

	void AddRigidBodyCollider(const RigidBodyHandle& hBody);
	void RemoveAllRigidBodyColliders() { CheckStepNotRunning(); m_numRigidBodyColliders = 0; }

	void SetAllowKeyStretchConstraints(bool allow);
	bool GetAllowKeyStretchConstraints() const { return m_bAllowKeyStretchConstraints; }

	void GetCollideFilter(bool bStrained, CollideFilter& filter) const;

	void WaitAndGatherAsyncSim();
	void CopyBackTwistDir();
	void PreProcessUpdate();

	void SetRopeSkinningData(RopeBonesData* pData) { m_pRopeSkinningData = pData; }
	void FillRopeNodesForSkinning(RopeBonesData* pRopeSkinningData);
	void FillDataForSkinningPreStep(RopeBonesData* pRopeSkinningData);
	void FillDataForSkinningPostStep(RopeBonesData* pRopeSkinningData, bool paused);

	ndjob::CounterHandle GetGpuWaitCounter() const { return m_pGpuWaitCounter; }

	static Rope2Debugger* GetCommonDebugger();

	bool GetTeleportThisFrame() const { return m_bTeleport; }

	void StepSimpleStrainedEdgeDetection(Point_arg startPos, Point_arg endPos);

	void UpdateSounds(NdGameObject* pOwner);
	void UpdateFx();
	void UpdateSnowFx();

	void CheckStepNotRunning() const;

private:
	struct LooseSectionData
	{
		Rope2* m_pRope;
		U32F m_start;
		U32F m_end;
		bool m_indep; // section is dependent (due to bending stiffness) if there is just one keyframed node at the end follow by other loose section)
		bool m_gpu;
	};

	struct RadiusKey
	{
		F32 m_ropeDist;
		F32 m_radius;
	};

private:
	static void InitPrimitiveEdges();

	void ResetNodeDebugInfo();

	void StepInnerSimple();
	void StepInner();
	void StepCleanup();

	void SetKeyframedPosWithFlags(F32 ropeDist, const Point& pos, const Vector& vel, RopeNodeFlags flags);
	void SortKeyframedPoints();
	void Discretize(bool updateLast);
	void StraightenStrainedSections();
	void RedistributeStrainedEdges(EdgePoint* pEdges, U32F numEdges);
	void GetDiscretizationUnstretchData(F32 ropeDistBase, Point& posBase, Vector& velBase, I32& iFirstNodeIndex, F32& maxUnstretchRopeDist) const;
	void GetPosVelAndClosestConstraint(F32 ropeDist, Point& pos, Vector& vel, I32& conIndex) const;
	void GetPosVelAndClosestConstraintWithUnstretch(F32 ropeDist, F32 baseRopeDist, const Point& basePos, const Vector& baseVel, U32 iFirstNode, F32 maxShift, Point& pos, Vector& vel, I32& conIndex) const;
	F32 GetInvRelMass(F32 ropeDist) const;
	F32 GetTensionFriction(F32 ropeDist) const;

	Vector GetKeyVel(F32 ropeDist) const;
	Vector GetEdgeVelocity(U32F edgeIndex);

	bool IsNodeKeyframedInt(U32 ii) const { return (m_pNodeFlags[ii]&kNodeKeyframed) != 0; }
	bool IsNodeStrainedInt(U32 ii) const { return (m_pNodeFlags[ii]&kNodeStrained) != 0; }
	bool IsNodeEdgeInt(U32 ii) const { return (m_pNodeFlags[ii]&kNodeEdge) != 0; }
	bool IsNodeLooseInt(U32 ii) const { return (m_pNodeFlags[ii]&(kNodeKeyframed|kNodeStrained)) == 0; }

	void AddColEdgeToConstraint(U32F ii, const RopeColEdge& edge0, const RopeColEdge& edge1);
	void AddColEdgePlaneToConstraint(U32F ii, const RopeColEdge& edge);
	void AddEdgesToConstraints(U32F iSegFirstEdge, U32F iSegLastEdge);
	void AddEdgesToConstraints();

	const Aabb& GetAabbSlacky() const { return m_aabbSlacky; }
	void UpdateAabb();
	void CalcAabbInSpace(const Locator& loc, Aabb& aabb) const;
	void CalcAabb(Aabb& aabb) const;
	void CalcAabbFromPoints(Aabb& aabb, Point_arg ptMin, Point_arg ptMax) const;
	void GetDynamicNodeCollideAabb(U32 ii, Aabb& aabb);
	void UpdateAabbEdges();

	void Collide();
	void CreateColCache();
	bool ValidateColCache();
	void PrepareClosestKeyData();
	VF32 CalcConstraintHash() const;
	void CollideWithColCacheCollider(const RopeCollider* pCollider, const RopeColliderHandle& hCollider, U32 triIndex, const ExternalBitArray* pTestPoints);
	void CollideWithCollider(const RopeCollider* pCollider, const RopeColliderHandle& hCollider);
	void CollideWithSphere(const RopeCollider* pCollider, const RopeColliderHandle& hCollider, const ExternalBitArray* pTestPoints = nullptr);
	void CollideWithPlane(const RopeCollider* pCollider, const RopeColliderHandle& hCollider);
	void CollidePointWithPlane(U32F iPoint, const Point& refPos, const RopeCollider* pCollider, const RopeColliderHandle& hCollider);
	void CollideWithCapsule(const RopeCollider* pCollider, const RopeColliderHandle& hCollider, const ExternalBitArray* pTestPoints = nullptr);
	void CollidePointWithCapsule(U32F iPoint, const Point& refPos, const RopeCollider* pCollider, const RopeColliderHandle& hCollider);
	void CollideWithBox(const RopeCollider* pCollider, const RopeColliderHandle& hCollider, const ExternalBitArray* pTestPoints = nullptr);
	void CollideWithBox(const RopeCollider* pCollider, const RopeColliderHandle& hCollider, const hknpBoxShape* pBox, const ExternalBitArray* pTestPoints = nullptr);
	void CollidePointWithBox(U32F iPoint, const Point& refPos, const RopeCollider* pCollider, const RopeColliderHandle& hCollider);
	void CollideWithTriangle(const RopeCollider* pCollider, const RopeColliderHandle& hCollider);
	void CollideWithTransformedTriangle(const RopeCollider* pCollider, const RopeColliderHandle& hCollider, Point_arg pt0, Point_arg pt1, Point_arg pt2, const ExternalBitArray* pTestPoints = nullptr);
	void CollideWithTransformedTriangle2(I16 triIndex, const RopeColliderHandle& hCollider, Point_arg pt0, Point_arg pt1, Point_arg pt2, const ExternalBitArray* pTestPoints);
	bool CollidePointWithConvexInner(U32F iPoint, const Point& refPos, const RopeCollider* pCollider, const RopeColliderHandle& hCollider, const hknpConvexPolytopeShape* pConvex, F32 distTol);
	void CollideWithConvex(const RopeCollider* pCollider, const RopeColliderHandle& hCollider, const hknpConvexPolytopeShape* pConvex, const ExternalBitArray* pTestPoints = nullptr);
	void CollidePointWithConvex(U32F iPoint, const Point& refPos, const RopeCollider* pCollider, const RopeColliderHandle& hCollider, const hknpConvexPolytopeShape* pConvex);
	void CollideWithColCacheConvex(const RopeCollider* pCollider, const RopeColliderHandle& hCollider, const hknpConvexPolytopeShape* pConvex, U32 triIndex, const ExternalBitArray* pTestPoints);

	void CollidePointWithCollider(U32F iPoint, const Point& refPos, const RopeCollider* pCollider, const RopeColliderHandle& hCollider);
	bool CheckConstraintPlane(U32F iPoint, Vec4_arg plane);

	static bool ColCacheTreeCollideCallback(U16 index, const ExternalBitArray* pBitArray, void* pUserData);
	void CollideWithColCache();

	void SelfCollision();
	void AddSelfCollisionConstraint(U32& firstFree, I16 iNode0, I16 iNode1);

	U32F StoreEdgeIds(RopeColEdgeId* pEdgeIds, const EdgePoint* pEdges, U32F numEdges);
	void RestoreEdgeIndices(RopeColEdgeId* pEdgeIds, EdgePoint* pEdges, U32F numEdges);

	void AddShapeEdgeCol(Point_arg p0, Point_arg p1, Vector_arg norm, I32F triIndex, U16 edgeIndex, const Aabb& aabb);
	void AddShapeDoubleEdgeCol(Point_arg p0, Point_arg p1, Vector_arg norm0, Vector_arg norm1, I32F triIndex, U16 edgeIndex, const Aabb& aabb);
	void AddShapeEdges(U32F numVertices, const Point* pVertices, U32F numFaces, const Vector* pFaces, const U32F* pNumFaceVertices, const U16* pVertexIndices,
		const Transform& xfm, const Transform& xfmWithScale, U32F iTri, const Aabb& aabb);
	void AddTriEdgeCol( const hknpShape* pShape, hknpShapeKey key, const Locator& loc, U32F iShape, const Aabb& aabb, const hknpCompressedMeshShape* pMesh, const ExternalBitArray* pOuterEdges);
	void CollideStrainedWithMesh(const hknpCompressedMeshShape* pMesh, const RopeColliderHandle& colShape, const HavokCastFilter* pFilter);
	void CollideStrainedWithShape(const hknpShape* pShape, const RopeColliderHandle& colShape, const Locator& loc);

	void CheckSleeping();

	Scalar GetKineticEnergy();
	Scalar GetMaxVel();
	Scalar GetMaxKeyframedVel();
	Scalar GetPotentialEnergy();

	JOB_ENTRY_POINT_CLASS_DECLARE(Rope2, SolveLooseSectionJob);
	void SolveLooseSection(U32F start, U32F end);
	void SolverStep();

	void RelaxPositions(U32 iStart, U32 iEnd);
	void RelaxPositionsFromConstraints(U32 iStart, U32 iEnd);
	void RelaxPositionsTowardsStraight(U32 iStart, U32 iEnd);
	Vec4 RelaxEdgePos(U32F i) { return RelaxEdgePos(i, i+1); }
	Vec4 RelaxEdgePos(U32F i, U32F j);
	Vec4 RelaxEdgePosOneSide(U32F iFixed, U32F ii);
	Vec4 RelaxEdgePosUnstretchOnly(U32F i, U32F j);
	Vec4 RelaxEdgePosOneSideUnstretchOnly(U32F iFixed, U32F ii);
	Vec4 RelaxPosExternalConstraints(U32F i);
	Vec4 RelaxPosDistanceConstraint(U32F ii);
	Vec4 RelaxPosDistanceConstraints(U32F iStart, U32F iEnd);
	Vec4 RelaxPosFrictionConstraint(U32F ii);
	Vec4 RelaxPosFrictionConstraints(U32F iStart, U32F iEnd);

	void PostSolve(U32F start, U32F end);

	void TimestepPositions();
	void TimestepPositions(U32F iStart, U32F iEnd, Scalar dt);

	Vec4 RelaxPositionsGridLevel(U32 iStart, U32 iEnd, U32* pGoups);
	U32F PrepareNextGridLevel(U32F iStart, U32F iEnd, const Point* pStartPos, U32F* pGroups0, U32F* pGroups1, U32F*& pGroups, U32F& numGroups);
	void RelaxPositionsMultiGrid(U32 iStart, U32 iEnd, F32 itersPerEdge);

	void RelaxPositionsForBending(U32 iStart, U32 iEnd);
	void RelaxVertexForBending(U32F i, const Scalar& scBendingStiffnes);
	void RelaxVertexForBending2Nodes(U32F iKeyframed, U32F i1, U32F i2, const Scalar& scBendingStiffnes);
	void RelaxVertexForBending1Node(U32F iKeyframed1, U32F iKeyframed2, U32F i, const Scalar& scBendingStiffnes, Scalar_arg kf);
	void RelaxVertexForBendingBoundary(U32F iKeyframed1, U32F iKeyframed2, const Scalar& scBendingStiffnes);

	void UpdateKeyframedVelocities();
	void TimestepVelocities();
	void TimestepVelocities(U32F iStart, U32F iEnd, Scalar dt);
	void BackstepVelocities(U32F iStart, U32F iEnd, Scalar dt);
	void BackstepVelocitiesDynamicFtl(U32F iStart, U32F iEnd, Scalar dt, Point* preUnstrechtPos);
	//void RelaxVelExternalConstraints(U32F i);
	void RelaxVelExternalConstraints();

	void CheckStateSane(U32F iStart, U32F iEnd);

	void CleanIgnoreCollisionBodies();

	void FindParabolicFit(U32F iStart, U32F iEnd, F32& x0, F32& y0, F32& A);
	void CreateDistanceConstraintsFromStrainedEdges(U32F iEdgeStart, U32F iEdgeEnd);
	void CreateDistanceConstraintsFromStrainedEdges();
	void CreateFrictionConstraint(U32 ii);
	void CreateFrictionConstraintFromTension(U32 ii);
	void CreateFrictionConstraints();

	void DisableRopeBreakingEdgeConstraints(U32F i0, U32F i1);

	void UpdateStrainedSounds(NdGameObject* pOwner);
	void UpdateHitSounds(NdGameObject* pOwner);
	void UpdateSlideSounds(NdGameObject* pOwner);
	void UpdateEdgeSlideSounds(NdGameObject* pOwner);

	// -------------------------------------------
	// Compute

	U32F PrepareNextGridLevelCompute(U32F numNodes, U32F nodesShift, U32F* pGroups0, U32F* pGroups1, U32F*& pGroups, U32F& numGroups, RopeMultiNewNode* pMultiNewNodes, U32F& numNewNodes);
	void PrepareMultiGridCompute(U32 iStart, U32 iEnd, U32F nodesShift, F32 itersPerEdge, U32F& numLevels, ndgi::Buffer& hInLevels, ndgi::Buffer& hInLevelIndices, ndgi::Buffer& hInLevelNewNodes);
	void RelaxPositionsCompute(U32 iStart, U32 iEnd);

	void InitComputeBuffers();
	void OpenCompute();
	void CloseCompute();
	void GatherCompute();
	void PostGatherCompute();
	void WaitAndGatherCompute();
	void AllocComputeEarly(U32F maxNumPoints);
	void ComputeCleanup();

	// -------------------------------------------
	// Edge detection

	bool PointEdgeCheck(const Point& refPos0, const Point& refPos1, const Point& nextPos, const Constraint& nextConstr,
		Point& edgePos, Constraint& edgeConstr);
	bool FindClosestStrainedEdgeCollision(const Point& startPos, F32 startRopeDist, const Point& endPos, F32 endRopeDist, U32F iStart, U32F iEnd,
		Point& edgePos, F32& edgeRopeDist, Constraint& edgeConstr);
	void ClasifyEdgeIntersection(const RopeColEdge* pEdge0, bool positive0, const RopeColEdge* pEdge1, bool positive1,
		const Point& prevRopePnt, const Point& nextRopePnt,
		Point& cornerPt, EdgeIntersection& intType);
	void SortOuterCornerEdges(const EdgePoint& ePoint, const EdgeIntersectionMatrix& intType, U8* pOuterCornerEdges, U32F& numOuterCornerEdges, U16& activeEdges);
	void StraightenUnfoldEdges(const RopeColEdge** pEdges, const U32F* pEdgePointIndices, EdgePoint* pEdgePoints, const bool* pPositive, U32F numEdges,
		const Point& prevRopePnt, const Point& nextRopePnt, F32 moveFricMult, Point* pPoints, bool* pSlideOff, bool& bMerge, bool& bCanSlideOff);
	bool UpdatePointActiveEdges(EdgePoint& ePoint, const Point& prevPnt, const Point& nextPnt, U16& invalidEdges);
	bool UpdatePointActiveNewEdges(EdgePoint& ePoint, const Point& prevPnt, const Point& nextPnt);
	void MergeEdgePoints(EdgePoint* pEdges, U32F& numEdges, U32F iFirst, U32F iSecond);
	bool CheckMergeEdgePoints(U32F& numEdges, EdgePoint* pEdges);
	bool CheckEdgeDistances(U32F& numEdges, EdgePoint* pEdges);
	void CheckDuplicateNeighborEdges(EdgePoint& ePoint1, EdgePoint& ePoint2);
	void SlideOffEdge(EdgePoint& ePoint, const Point& pos, U32F edgeIndex, const EdgeIntersectionMatrix& intType);
	void ProcessUnfoldEdgesGroup(U32F numUnfoldEdges, const U8* pUnfoldEdgeIndices, const U32F* pUnfoldEdgePointIndices,
		const bool* pUnfoldPositive, Point* pUnfoldPoints, U32F iPrevEdge, U32F iNextEdge,
		EdgePoint* pEdges, Point* pPosOut, Vector* pDir, EdgeIntersectionMatrix* pIntType, U32F& numEdges,
		Scalar& maxRes2, U32F& numInserted, bool& bMerge, bool& bSlidOff, bool& bInterrupt);
	void InsertEdgePoint(Rope2::EdgePoint* pEdges, U32F& numEdges, U32F iInsert, const Point& pos, F32 prevRopeDist, F32 nextRopeDist, Point* pPos0, Point* pPos1,
		const Scalar& t, const Scalar& u);
	void AddEdgeToEdgePoint(Rope2::EdgePoint* pEdges, U32F& numEdges, U32F iEdge, U16 edgeIndex, bool active, bool checkTrims);
	void CheckTrimNewEdge(U16 edgeIndex, EdgePoint* pEdges, U32F numEdges, Point* pPos);

	void StrainedEdgeDetection();
	void StrainedEdgeDetectionFromSlacky(EdgePoint* pEdges, U32F& numEdges, U32F edgeBufOffset, Point_arg startTargetPos, Point_arg endTargetPos, bool skipStepMove);
	void StrainedEdgeDetection(EdgePoint* pEdges, U32F& numEdges, U32F edgeBufOffset, Point_arg startTargetPos, Point_arg endTargetPos, bool skipStepMove);
	void InitStrainedSectionEdges(U32F iKeyEnd, EdgePoint* pEdges, U32F& numEdges);
	void InsertIntermediateEdgePoints(U32 iKey, const I32F* pOldEdgeIndex, const bool* pOldEdgeIndexExact, EdgePoint* pEdges, U32& numEdges, U32& iOldSim);
	void InsertIntermediateEdgePoints(U32 iPrevKey, F32 ropeDist, const I32F* pOldEdgeIndex, const bool* pOldEdgeIndexExact, EdgePoint* pEdges, U32& numEdges, U32& iOldSim);
	bool StraightenAlongEdges(U32F& numEdges, EdgePoint* pEdges, Point* pPosOut);
	void BackStepEdges(U32F numEdges, EdgePoint* pEdges, const Point* pPos0, const Point* pPos1);
	void DetermineNewEdgePositivness(U32F numEdges, EdgePoint* pEdges, const Point* pPrevPos);
	bool MoveStrainedCollideWithEdges(U32F& numEdges, EdgePoint* pEdges, Point* pPos0, Point* pPos1);
	bool RemoveObsoleteEdgePoints(U32F& numEdges, EdgePoint* pEdges);
	bool CheckEdgesLimits(U32F numEdges, EdgePoint* pEdges);
	bool CleanInactiveEdges(EdgePoint& ePoint);
	bool CleanInactiveEdges(U32F& numEdges, EdgePoint* pEdges);
	bool MovePointCollideWithEdges(EdgePoint* pEdges, U32F iEdge, U32F& numEdges, Point* pPos0, Point* pPos1, ExternalBitArray* pEdgesPrevStep);
	bool MoveSegmentCollideWithEdges(const EdgePoint& fixedEPoint, const EdgePoint& ePoint, Vector_arg dir, Scalar_arg dist,
		const ExternalBitArray& edgesToTest, ExternalBitArray* pEdgesPrevStep, const EdgePoint* pPrevEPoint, const EdgePoint* pNextEPoint, EdgeHit* pHits, U32F& numHits, I32F& firstHit);

	bool MoveEdgeCollideWithSegment(const RopeColEdge& edge, const Locator& locPrev, const Locator& loc, Point_arg pnt0, Point_arg pnt1, F32& t, F32& segT, F32& edgeT);
	bool MoveEdgeCollideWithRope(U32F edgeIndex, const Locator& locPrev, const Locator& loc, const EdgePoint* pEdges, U32F numEdges);
	void MoveEdgesCollideWithRope(ExternalBitArray& edges, const EdgePoint* pEdges, U32F numEdges);
	bool MoveEdgesCollideWithSegment(const ExternalBitArray& edges, const EdgePoint* pEdgePoints, U32F numEdges, U32F iEdge, I32F& earliestEdgeIndex, F32& earliestT, F32& earliestSegT, F32& earliestEdgeT);
	void SetPos1FromEdge(EdgePoint* pEdges, U32F iEdge, Point* pPos1, ExternalBitArray* pEdgesPrevStep);
	bool MovePointCollideWithEdgesSetPos1FromEdge(EdgePoint* pEdges, U32F iEdge, U32F& numEdges, Point* pPos0, Point* pPos1, ExternalBitArray* pEdgesPrevStep);
	void StepMoveEdgesAndRopeEndPoints(EdgePoint* pEdges, U32F& numEdges, Point_arg pos0, Point_arg pos1);

private:

	bool m_bAllowKeyStretchConstraints;

	U32F m_maxNumPoints;

	// Keyframed input
	U32F m_maxNumKeyPoints;
	U32F m_numKeyPoints;
	U32F m_numLastKeyPoints;
	Point *m_pKeyPos;
	Vector *m_pKeyVel;
	F32* m_pKeyRopeDist;
	RopeNodeFlags *m_pKeyNodeFlags;
	U32F m_numKeyRadius;
	RadiusKey* m_pKeyRadius;
	//Sphere* m_pDistConstr;
	F32* m_pKeyMoveDist;
	Vector* m_pKeyTwistDir;
	Point m_saveEdgePos;
	Point m_saveStartEdgePos;
	F32 m_savePosRopeDist;
	bool m_saveEdgePosSet;
	bool m_saveStartEdgePosSet;
	bool m_savePosRopeDistSet;

	// Sim output
	U32 m_numPoints;
	Point *m_pPos;
	Vector *m_pVel;
	Vector *m_pTwistDir;
	Point *m_pLastPos;
	Vector *m_pLastVel;
	F32* m_pRopeDist;
	F32* m_pRadius;
	F32* m_pInvRelMass;
	Constraint* m_pConstr;
	RopeNodeFlags *m_pNodeFlags;
	Aabb* m_pNodeAabb;
	F32* m_pTensionFriction;

	// Edge detection output
	U32 m_numEdges;
	EdgePoint* m_pEdges;
	U32 m_numSaveEdges;
	EdgePoint* m_pSaveEdges;
	U32 m_numExternSaveEdges;
	EdgePoint* m_pExternSaveEdges;

	// Helper buffers
	Rope2PointCol* m_pPointCol;
	I16* m_pPrevKeyIndex;
	I16* m_pNextKeyIndex;
	Vec4* m_pDistConstraints;
	Vector* m_pDistConstraintsVel;
	Vec4* m_pFrictionConstraints;
	F32* m_pFrictionConstConstraints;
	I16* m_pSelfCollision;

	U32 m_maxSelfCollision;
	U32 m_numSelfCollision;

	I32F m_firstDynamicPoint;
	I32F m_lastDynamicPoint;

	Aabb m_aabb;
	Aabb m_aabbSlacky;
	Rope2Collector* m_pCollector;

	U32F m_lastStraightenSubsteps;

	Scalar m_scStepTime;
	Scalar m_scInvStepTime;
	VF32 m_vConstraintHash;
	U32 m_numFramesCreep;
	bool m_bTeleport;
	bool m_bResetDynamics;

	RopeColCache m_colCache;

	RigidBodyHandle* m_pIgnoreCollisionBodies;
	U32F m_numIgnoreCollisionBodies;

	const RopeCollider* m_ppCustomColliders[kMaxNumCustomColliders];
	U32F m_numCustomColliders;

	RigidBodyHandle m_phRigidBodyColliders[kMaxNumRigidBodyColliders];
	U32F m_numRigidBodyColliders;

	U32F m_maxNumStrainedSounds;
	U32F m_numStrainedSounds;
	StrainedSound* m_pStrainedSounds;

	U32 m_numHitSounds;
	U32 m_numSlideSounds;
	HitSound* m_pHitSounds;
	SlideSound* m_pSlideSounds;

	static const U32 kMaxEdgeSlideSounds = 10;
	U32 m_numEdgeSlideSounds;
	EdgeSlideSound* m_pEdgeSlideSounds;

	U32F m_maxNumStrainedFx;
	U32F m_numStrainedFx;
	StrainedFx* m_pStrainedFx;
	static const I32 kMaxSnowFx = 30;
	I32 m_iFirstSnowFx;
	Point* m_pSnowFxPos;

	I64 m_stepJobId;

	// Debug only
	Rope2Debugger* m_pDebugger;
	Point* m_pDebugPreProjPos;
	Point* m_pDebugPreSlidePos;
	Constraint* m_pDebugPreSlideCon;
	F32* m_pDebugSlideTarget;
	F32* m_pDebugTension;
	bool* m_pDebugTensionBreak;

	friend struct Rope2Collector;
	friend class Rope2Debugger;
	friend class Rope2DumpViewer;
	friend class RopeCollider;
	friend class RopeColliderHandle;
	friend class Rope2PointCol;

private:
	bool m_useGpu;
	bool m_emulOnCpu;

	RopeBonesData* m_pRopeSkinningData;

	ndgi::Buffer m_hCsInBuffer;
	ndgi::Buffer m_hCsInOutBuffer;
	ndgi::Buffer m_hCsOutForSkinningBuffer;

	ndgi::ComputeContext* m_pCmpContext;
	ndjob::CounterHandle m_pGpuWaitCounter;

	static ndgi::ComputeQueue s_cmpQueue;
};

enum class EdgeOrientation
{
	kIndifferent = 0,
	kPositive = 1,
	kNegative = 2
};

bool CheckRopeEdgePat(const Rope2::EdgePointInfo& info, Pat patMask, EdgeOrientation orient = EdgeOrientation::kIndifferent);

#endif // NDPHYS_ROPE2_H

