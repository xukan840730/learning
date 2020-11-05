/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef PROCESS_PHYS_ROPE_H
#define PROCESS_PHYS_ROPE_H

#include "ndlib/util/jaf-anim-recorder.h"
#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/nd-drawable-object.h"
#include "gamelib/ndphys/collision-filter.h"
#include "gamelib/ndphys/rope/rope2-collider.h"
#include "gamelib/ndphys/rope/rope2.h"

class CatmullRom;
class EffectAnimInfo;
class HavokRopeConstraint;
class ProcessSpawnInfo;
class ResolvedLook;
class RigidBody;
struct FgAnimData;
template <typename T> class ListArray;

namespace DC
{
	struct RopeMeshInfo;
}

const F32 kRopeDefaultRadius = 0.02f;
const F32 kRopeTempLayerMaskDefaultTimeout = 10.0f; // fail-safe

struct PhysRopeNetData
{
	bool m_rightHandOn;
	bool m_leftHandOn;
	bool m_rightHandFirst;
	Rope2NetData m_ropePoints;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct RopeInfo
{
	bool m_bNeverStrained;
	bool m_bBuoyancy;
	bool m_bWithSavePos;
	bool m_bWithExternSavePos;
	bool m_bGhosting;	// if this rope causes ghosting with temporal AA, mark it in stencil buffer
	bool m_bRopeSkinning; // use special "rope skinning" algorithm
	bool m_bEnableSolverFriction;
	bool m_bEnablePostSolve;
	bool m_bUseTwistDir;
	Point m_endPoint;
	bool m_endPointValid;
	Vector m_wallNormal;
	F32 m_length;
	F32 m_segmentLength;
	F32 m_radius;
	F32 m_minSegmentFraction;
	StringId64 m_firstJointId;
	StringId64 m_lastJointId;

	Vector m_bottomToHolsterOffset;

	NdGameObjectHandle m_hParent;
	NdGameObjectHandle m_hAttachPntObj;

	StringId64 m_initSplineId;

	BoundFrame m_topBf;
	BoundFrame m_bottomBf;

	Rope2SoundDef m_soundDef;
	Rope2FxDef m_fxDef;

	RopeInfo()
		: m_bNeverStrained(false)
		, m_bBuoyancy(false)
		, m_bWithSavePos(true)
		, m_bWithExternSavePos(false)
		, m_bGhosting(true)
		, m_bRopeSkinning(true)
		, m_bEnableSolverFriction(false)
		, m_bEnablePostSolve(false)
		, m_bUseTwistDir(false)
		, m_endPointValid(false)
		, m_length(-1.0f)
		, m_segmentLength(-1.0f)
		, m_radius(-1.0f)
		, m_minSegmentFraction(-1.0f)
		, m_firstJointId(INVALID_STRING_ID_64)
		, m_lastJointId(INVALID_STRING_ID_64)
		, m_bottomToHolsterOffset(kZero)
		, m_hParent(nullptr)
		, m_initSplineId(INVALID_STRING_ID_64)
	{}
};

/// --------------------------------------------------------------------------------------------------------------- ///
class ProcessRopeCollider
{
public:

	Locator m_locLs;
	RopeCollider m_collider;
	I32F m_joint;

	ProcessRopeCollider() : m_joint(-1) {};

	void Init(const hknpShape* pShape, const Locator& locLs)
	{
		m_collider.m_pShape = pShape;
		m_locLs = locLs;
		m_joint = -1;

	}
	void Destroy()
	{
		if (m_collider.m_pShape)
			m_collider.m_pShape->removeReference();
	}
	void Reset(const FgAnimData* pAnimData, StringId64 jointSid);
	void Update(const FgAnimData* pAnimData, bool teleport);
};

/// --------------------------------------------------------------------------------------------------------------- ///
class ProcessPhysRope : public NdDrawableObject, public IAnimCustomChannelProvider
{
public:
	enum CharColliderNames
	{
		kColliderTrunk = 0,
		kColliderPelvis,
		kColliderLUpperLeg,
		kColliderRUpperLeg,
		kNumCharColliders,
		kXColliderLShoulder,
		kXColliderRShoulder,
		kXColliderLElbow,
		kXColliderRElbow,
		kXColliderLKnee,
		kXColliderRKnee,
	};

	struct CharPointOnRope
	{
		bool m_isOn;
		bool m_requestOn;
		bool m_lock; // can't be set by client for now
		F32 m_ropeDist;
		StringId64 m_attachSid;
		Locator m_loc;
		F32 m_blendDist;
		F32 m_blendTime;
	};

	enum CharPointIndex { kRightHand, kLeftHand, kLeftThigh, kLeftFoot, kCharPointCount };

	enum RopeUserMarks
	{
		kGrappleHook = Rope2::kNodeUserMark3,
		kAttachPoint = Rope2::kNodeUserMark2,
		kDrapePoint = Rope2::kNodeUserMark4,
		kFirstCharPoint = Rope2::kNodeUserMark1
	};

	static constexpr F32 kFirstHandUpOffset = 0.05f;
	static constexpr F32 kFirstHandDownOffset = 0.03f;
	static constexpr F32 kSecondHandUpOffset = 0.05f;
	static constexpr F32 kSecondHandDownOffset = 0.03f;

public:
	FROM_PROCESS_DECLARE(ProcessPhysRope);
	STATE_DECLARE_OVERRIDE(Active);

	ProcessPhysRope();
	virtual Err Init(const ProcessSpawnInfo& spawn) override;
	virtual Err InitializeDrawControl(const ProcessSpawnInfo& spawn, const ResolvedLook& resolvedLook) override;
	virtual void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound) override;
	void PostAnimBlending_Async() override;
	void PostJointUpdate_Async() override;
	void ProcessMouse();
	virtual void OnKillProcess() override;
	virtual void EventHandler(Event& event) override;

	virtual bool CanDisableUpdates() const { return true; }
	void EnableAnimation() override;
	bool DisableAnimation() override;

	void SetAsyncRopeStepEnabled(bool b) { m_bEnableAsyncRopeStep = b; }

	void SetTeleport() override;

	void SetKeyframedFromAnim(F32 ropeDist, U32 flags = 0);

	float SetSimToSpline(const CatmullRom* pSpline, bool reversed, F32 startRopeDist, F32 endRopeDist, bool stretch = false);
	void SetSimToLine(Point_arg startPt, Point_arg endPt, F32 startRopeDist, F32 endRopeDist);
	void SetSimToPoint(Point_arg pt, F32 startRopeDist, F32 endRopeDist);

	// You can use this once you're done keyframing the rope for this frame
	// But you take all the responsibility for not touching the rope data while it's simulation is running async
	void KickStepRope();
	void WaitStepRopeEarly();
	bool IsStepRunning() const { return m_pStepRopeCounter != nullptr; }
	void CheckStepNotRunning() const;
	virtual void PreStepRope();
	virtual void PostStepRope();
	void PreNoRefRigidBodySync() { WaitStepRope(); }
	void FrameSyncEnd();

	virtual bool MeetsCriteriaToDebugRecord(bool& outRecordAsPlayer) const override;

	void WakeUp();
	void MakeUsable(bool usable);
	void ScriptMakeUsable(bool usable);
	bool IsUsable() const;

	const Point ClosestPointOnRope(const Segment& checkSeg, float& dist, F32* pRopeDistOut = nullptr) const;
	const Point ClosestPointOnRope(F32 minDist, const Segment& checkSeg, float& dist, F32* pRopeDistOut = nullptr) const;
	const Point ClosestPointOnRope(const Point& point, F32* pRopeDistOut = nullptr) const;
	const Point ClosestPointOnRope(F32 minDist, const Point& point, F32* pRopeDistOut = nullptr) const;

	const Point GetTop() const { return m_top; }

	void SetAttached(bool b) { m_bAttached = b; }
	void SetEdgesKeyframed(bool b) { m_bEdgesKeyframed = b; }

	void SetActiveCharacter(Character* pChar, bool peristant = false);
	const Character* GetActiveCharacter() const { return m_hCharacter.ToProcess(); }
	void ClearActiveCharacter();
	void ChangeCharacterPersistent(bool b) { m_characterIsPersistant = b; }
	void SetCharacterClimbsAsWallRope(bool b);

	// ---------------------------------------------------------------------
	// Character collision
	void UpdateCharacterCollision(Point_arg top);
	void EnableCharacterCollider(CharColliderNames collider, bool enable) { m_charColliders[collider].m_collider.m_enabled = enable; }
	void EnableCollideWithCharacters(bool collide);
	void DisableHandCollision(const Character* pChar);
	void EnableHandCollision(const Character* pChar);
	void SetGrabColliderEnabled(bool b);

	// ---------------------------------------------------------------------
	// Control of the rope from character animation
	virtual void UpdateCharacterRopeControl();
	virtual void HandleTriggeredCharacterEffect(const EffectAnimInfo* pEffectAnimInfo, Character* pChar) override;

	// ---------------------------------------------------------------------
	// New interface for attaching the rope to the character

	void SetGrabRopeDist(F32 ropeDist) { m_grabRopeDist = ropeDist; m_grabRopeDistSet = true; }
	void SetRightHandOn(bool on) { m_charPointsOnRope[kRightHand].m_requestOn = on; }
	void SetLeftHandOn(bool on) { m_charPointsOnRope[kLeftHand].m_requestOn = on; }
	void SetLeftThighOn(bool on) { m_charPointsOnRope[kLeftThigh].m_requestOn = on; }
	void SetLeftFootOn(bool on) { m_charPointsOnRope[kLeftFoot].m_requestOn = on; }

	// This will force the right hand to be first
	void SetRightHandIsFirst(bool rightHandIsFirst);
	// This will reset back to the default behavior
	void ClearRightHandIsFirst();
	// Is the right hand first on rope? If not forced it returns the procedural result from last step
	bool GetRightHandIsFirst() const { return m_rightHandIsFirst; }

	void SetFirstHandLock(bool on);
	bool GetFirstHandLock() const { return m_firstHandLock; }

	void SetUseSaveStrainedPos(bool useSavePos) { m_useSaveStrainedPos = useSavePos; }

	void SetSlack(F32 slack, bool overrideAnim = false) { m_slack = slack; m_slackOverrideAnim = overrideAnim; }
	F32 GetSlack() const { return m_slack; }
	void SetSlackBetweenHands(F32 slack) { m_slackBetweenHands = slack; }
	F32 GetSlacBetweenHandsk() const { return m_slackBetweenHands; }
	void SetReelInTime(F32 time) { m_reelInTime = time; }
	F32 GetReelInTime() const { return m_reelInTime; }
	void ResetCharPoints();

	// Direct RW access
	CharPointOnRope& GetCharPointOnRope(U32F index) { ALWAYS_ASSERT(index < kCharPointCount); return m_charPointsOnRope[index]; }

	// ---------------------------------------------------------------------

	//void UpdateStraightDistToAnchor();
	//void UpdateStraightDistToAnchor(const Point& handPos);
	//void UpdateStraightDistToAnchor(Point_arg anchorPos, Point_arg handPos);

	void AttachRopeToCharacter();

	// Length of the rope used for the anchor (wrapped around something)
	F32 GetAnchorRopeDist() const { return m_anchorEdgeIndex >= 0 ? m_rope.GetEdges()[m_anchorEdgeIndex].m_ropeDist : 0.0f; }

	Point GetAnchorPoint() const { return m_anchorEdgeIndex >= 0 ? m_rope.GetEdges()[m_anchorEdgeIndex].m_pos : m_rope.GetPos(0.0f); }

	// Straightened distance from the anchor to the hand
	F32 GetHandStraightDistToAnchor() const { return m_straightDistToAnchor; }

	// Total straightened distance from root to the hand
	F32 GetHandStraightRopeDist() const { return GetAnchorRopeDist() + m_straightDistToAnchor; }

	// Distance from root along the rope (not straightened) to the hand. -1.0f if no hand is on.
	F32 GetHandRopeDist() const { return m_handEdgeIndex >= 0 ? m_rope.GetEdges()[m_handEdgeIndex].m_ropeDist + kFirstHandUpOffset : -1.0f; }

	I32F GetHandEdgeIndex() const { return m_handEdgeIndex; }

	void GetFirstCharacterKeyframe(F32& ropeDist, Point& pos) const { ropeDist = m_firstCharacterKeyRopeDist; pos = m_firstCharacterKeyPos; }

	// Is there a collision pin between anchor and hand?
	bool HasPin() const;

	// Is there a collision pin between anchor and hand?
	virtual Point GetPinPoint() const;

	// Total straightened distance from root to pin
	F32 GetPinStraightRopeDist() const { return GetAnchorRopeDist() + GetDistFromAnchorToPin(); }

	virtual F32 GetDistFromAnchorToPin() const;

	// Old deprecated interface for attaching the rope to the player
	void SetGrabPoints(Character* pChar, SMath::Point_arg grab1, SMath::Point_arg grab2, bool grabCollider, bool blend = true);
	void GetGrabPoints(Point& grab1out, Point& grab2out);
	void ClearGrabPoints();
	bool GrabPointsValid() { return m_grabPointsValid; }

	void ReelOutRope(F32 oldUsedLen, F32 newUsedLen, const Locator& oldPinLoc, const Locator& newPinLoc, const Vector& handMoveIn, F32 oldhandRopeDist, F32 newHandRopeDist);
	void PinRope(const Locator& pinLoc, const Locator& oldPinLoc, F32 ropeDist, Point_arg storeOffset, Rope2::RopeNodeFlags flags = 0);

	void ReelTeleport(F32 newHandRopeDist, F32 pct);
	F32 GetReelInDist(F32 newHandRopeDist, F32 pct);
	void EstimateReelInPosAndDir(F32 dt, Point& posOut, Vector& dirOut);

	// Makes given physics driven body to move as if attached to the rope and the anchored
	// Set NULL to remove the constraint
	void SetBodyConstrainedAtAnchor(RigidBodyHandle hBody, bool atAnchor, Point_arg bodyPointLs, F32 pullStrength, F32 pullStrengthMaxCosY = 1.0f, F32 pullStrengthY = 0.001f);
	void SetApplySwingImpulseOnAnchor(RigidBodyHandle hAnchorBody, F32 impulseMult, F32 lockedRopedDist);

	void SetTempLayerMask(Collide::LayerMask tempLayerMask, F32 timeoutSec = -1.0f); // timeout of 0.0f means "infinite", < 0.0f means "use default timeout"
	void ClearTempLayerMask();
	void SetCollisionIgnoreObject(NdGameObjectHandle hIgnoreObj);

	void SetKeyframedRange(F32 ropeDistStart, F32 ropeDistEnd, Point pos, Vector vel);

	bool GetCharacterCollidedStraightenRopeInfo(F32& len, I32F& anchorEdgeIndex, I32F& handEdgeIndex) const;
	void GetCollidedRopeInfo(F32& len, Point& lastCollisionPoint) const;
	RigidBody* GetPinRigidBody() const;
	bool GetCollidedStraightenRopeInfo(F32& len, I32F& firstEdgeIndex, I32F& lastEdgeIndex, Rope2::RopeNodeFlags topFlags, Rope2::RopeNodeFlags bottomFlags) const;
	void GetCollidedPoints(I32 numPoints, Point* pointArray, Vector* edgeArray) const;

	// Given direction in which we want to swing returns the effective pin point taking into account the edges and their direction
	Point GetEffectivePinPoint(Vector swingDir) const;

	void SetUsedLength(F32 len) { m_usedLength = len; }
	F32 GetUsedLength() const { return m_usedLength; }
	void ResetUsedLength() { m_usedLength = -1.0f; }

	U32F GetFirstJoint() const { return m_firstJoint; }
	U32F GetLastJoint() const { return m_lastJoint; }

	void SetPhysBlend(F32 target, F32 time);

	bool CheckEdgePat(U32 iEdge, Pat patMask, EdgeOrientation orient = EdgeOrientation::kIndifferent) const;

	void DebugDrawAnimBlend();

	virtual bool GetAnimCustomChannelControlValue(StringId64 channelId, F32& valueOut) const override;

	// E3 2015 Hack!
	void SetGrabColliderDisableOverrride(bool b) { m_grabColliderDisableOverride = b; }

	void FillPhysRopeNetData(PhysRopeNetData *pData);

	F32 GetRopeLength() const { return m_rope.m_fLength; }
	void SetRopeLength(F32 len, bool stretchSkinning);

	void SetDampingWhenNoChar(F32 d);

	void SetDisableHandRayCastCheck(bool b) { m_bDisableHandRayCastCheck = b; }

	void InheritVisibilityFromParent();

	void AddQueryPoint(F32 ropeDist);
	void RemoveQueryPoint(F32 ropeDist);
	bool GetQueryPoint(F32 ropeDist, Point& p);

	static void CheckCharPointLocatorCollision(Locator& loc, Point_arg savePos, F32 upDist, F32 downDist);

	bool GetAllowSwingTutorial() const { return m_allowSwingTutorial; }

	void SetEndAttachment(const BoundFrame& bf, bool withDir);
	void ClearEndAttachment();

	Rope2 m_rope;

protected:
	typedef NdDrawableObject ParentClass;

	JOB_ENTRY_POINT_CLASS_DECLARE(ProcessPhysRope, StepRopeJob);

	void StepRope(bool async = false);
	virtual bool UpdateRope(bool bAnimating);
	void UpdateUsedLengthSkinning();
	void UpdateJointCacheFromRope();

	void WaitStepRope();

	void PreStepRopeSkinning();
	void PostStepRopeSkinning(bool paused);
	void UpdateAnimDataForRopeSkinning(FgAnimData* pAnimData);

	static void CustomAnimPass1(FgAnimData* pAnimData, F32 dt);
	void DoFastAnimDataUpdate(FgAnimData* pAnimData, F32 dt, bool bMoved);
	static void FastAnimDataUpdate(FgAnimData* pAnimData, F32 dt);

	void SetSimToBindPose();
	void SetSimToAnimPose();

	enum GrabIndex { kGrabUpper, kGrabLower, kGrabCount };

	void UpdateWind();

	void SetCharacterClimbsAsWallRopeInternal(bool b);

	void ApplySwingImpulseOnAnchor();

	void UpdateStraightDistToAnchorInner();
	void UpdateStraightDistToAnchorInner(Point_arg anchorPos, Point_arg handPos);
	void UpdateStraightDistToAnchorPreStep(const Point& handPos);

	void KeyframeCharacterPoint(const CharPointOnRope& pointOld, CharPointOnRope& pointNew, F32& prevRopeDist, F32& ropeDist, const CharPointOnRope* pPrevOld, const CharPointOnRope* pPrevNew,
		F32 handRopeDistDiff, F32 maxSlideDist, F32 offsetUp, F32 offsetDown, U32 addFlags);

	union
	{
		U32	m_flags;
		struct
		{
			bool m_bAttached : 1;
			bool m_bUsable : 1;
			bool m_bScriptUsable : 1;
			bool m_bInited : 1;
			bool m_grabPointsValid : 1;
			bool m_grabColliderOn : 1;
			bool m_grabColliderInvalid : 1;
			bool m_charCollidersReset : 1;
			bool m_grabColliderReset : 1;
			bool m_characterIsPersistant : 1;
			bool m_tempLayerMaskActive : 1;
			bool m_layerMaskCached : 1;
			bool m_stepRopeDoneEarly : 1;
			bool m_rightHandIsFirst : 1;
			bool m_rightHandIsFirstRequest : 1;
			bool m_rightHandIsFirstSet : 1;
			bool m_rightHandIsFirstExternal : 1;
			bool m_firstHandLock : 1;
			bool m_useSaveStrainedPos : 1;
			bool m_charHasControlAnimChannels : 1;
			bool m_slackOverrideAnim : 1;
			bool m_grabColliderDisableOverride : 1;
			bool m_bEdgesKeyframed : 1;
			bool m_firstStepDone : 1;
			bool m_bEnableAsyncRopeStep : 1;
			bool m_bDisableHandRayCastCheck : 1;
			bool m_allowSwingTutorial : 1;
			bool m_characterClimbsAsWallRope : 1;
			bool m_bEndAttachment : 1;
			bool m_bEndAttachmentWithDir : 1;
		};
	};

	CharPointOnRope m_charPointsOnRope[kCharPointCount];

	// animated values
	F32 m_rightHandOn;
	F32 m_leftHandOn;
	F32 m_rightHandIsFirstSetVal;
	F32 m_slack;
	F32 m_slackBetweenHands;

	F32 m_reelInTime;

	F32 m_dampingWhenNoCharacter;

	I32F m_anchorEdgeIndex;
	I32F m_handEdgeIndex;
	F32 m_collidedStraigthenRopeLen;
	F32 m_straightDistToAnchor;
	Point m_lastEdgePoint;

	Point m_grabPoint[kGrabCount];
	F32 m_blendToGrab;

	F32 m_usedLength;

	MutableCharacterHandle m_hCharacter;

	RopeCollider m_grabCollider;
	ProcessRopeCollider m_charColliders[kNumCharColliders];
	F32 m_grabColliderBlendIn;

	bool m_grabRopeDistSet;
	F32 m_grabRopeDist;

	RigidBodyHandle m_hConstrainedBody;
	bool m_bodyConstrainedAtAnchor;
	Point m_constrainedBodyPivot;
	F32 m_constrainedBodyPullStrength;
	F32 m_constrainedBodyPullMaxCosY;
	F32 m_constrainedBodyPullStrengthY;
	HavokRopeConstraint* m_pRopeConstraint;

	RigidBodyHandle m_hAnchorSwingImpulseBody;
	F32 m_anchorSwingImpulseRopedDist;
	F32 m_anchorSwingImpulseMult;

	bool m_rightHandLockRopeDistSet;

	Collide::LayerMask m_nCachedLayerMask;
	F32 m_tempLayerMaskTimeout;

	F32 m_windFactor;

	F32 m_physBlend;
	F32 m_desiredPhysBlend;
	F32 m_physBlendTime;

	U32 m_firstJoint;
	U32 m_lastJoint;

	F32* m_pJointRopeDist;

	F32 m_draggedRopeDist;

	// For rope skinning
	const DC::RopeMeshInfo* m_pRopeMeshInfo;

	I32 m_probeJointIndex;

	ndjob::CounterHandle  m_pStepRopeCounter;

	Point m_top;

	RopeBonesData* m_pRopeBonesData;
	Mat44 m_prevObjXform;

	F32 m_firstCharacterKeyRopeDist;
	Point m_firstCharacterKeyPos;

	BoundFrame m_endAttachment;

	static const U32 kMaxQueryPoints = 4;
	U32 m_numQueryPoints = 0;
	F32 m_queryRopeDist[kMaxQueryPoints];
	Point m_queryPoints[kMaxQueryPoints];

	friend class RopeMgr;
};

PROCESS_DECLARE(ProcessPhysRope);

class ProcessPhysRope::Active : public NdDrawableObject::Active
{
	BIND_TO_PROCESS(ProcessPhysRope);
public:

	virtual void Enter() override;
};

/// --------------------------------------------------------------------------------------------------------------- ///
bool IsProcessPhysRope(const Process& proc);
const RigidBody* GetMostContributingRigidBodyFromEdgeInfo(const Rope2::EdgePointInfo& info);

#endif // PROCESS_PHYS_ROPE_H
