/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nav/action-pack-handle.h"
#include "gamelib/gameplay/nav/action-pack.h"
#include "gamelib/level/entity-spawner.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class FeatureCorner;
class Level;
class NavCharacter;
class NavMesh;
class NavPoly;
struct ProtectionFrustumAngles;
class Obb;

/// --------------------------------------------------------------------------------------------------------------- ///
struct CoverDefinition
{
	enum CoverType
	{
		kCoverStandLeft,   // standing cover peek to the left
		kCoverStandRight,  // standing cover peek to the right
		kCoverCrouchLeft,  // crouching cover peek to the left
		kCoverCrouchRight, // crouching cover peek to the right
		kCoverCrouchOver,  // crouching cover peek over the top
		kCoverTypeCount
	};

	static const char* GetCoverTypeName(CoverType ct)
	{
		switch (ct)
		{
		case kCoverStandLeft:	return "kCoverStandLeft";
		case kCoverStandRight:	return "kCoverStandRight";
		case kCoverCrouchLeft:	return "kCoverCrouchLeft";
		case kCoverCrouchRight:	return "kCoverCrouchRight";
		case kCoverCrouchOver:	return "kCoverCrouchOver";
		}

		return "<invalid>";
	}

	TimeFrame m_costUpdateTime;
	float m_tempCost;
	float m_height;
	CoverType m_coverType;
	bool m_canCornerCheck : 1;
	bool m_canStepOut : 1;
	bool m_srcCornerCanStepOut : 1;
	bool m_srcCornerCanAim : 1;
	bool m_canPeekOver : 1;
	bool m_standOnly : 1;
	bool m_nextToDoor : 1;

	bool IsLow() const { return (m_coverType > kCoverStandRight); }
	bool IsShort() const { return (m_coverType > kCoverCrouchOver); }
	bool IsCrouch() const { return IsLow() && !IsShort(); }
	bool CanAim() const { return m_srcCornerCanAim; }
	bool CanOnlyStand() const { return m_standOnly; }
	bool CanStepOut() const { return m_canStepOut; }
	bool CanPeekLeft() const
	{
		return (m_coverType == kCoverStandLeft) || (m_coverType == kCoverCrouchLeft);
	}
	bool CanPeekRight() const
	{
		return (m_coverType == kCoverStandRight) || (m_coverType == kCoverCrouchRight);
	}
	bool CanPeekSide() const { return CanPeekLeft() || CanPeekRight(); }
	bool CanPeekOver() const { return m_canPeekOver; }
};

/// --------------------------------------------------------------------------------------------------------------- ///
class SpawnerCoverDef
{
public:
	Locator m_locPs;
	StringId64 m_bindSpawnerNameId;
	CoverDefinition::CoverType m_coverType;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class SpawnerLoginChunkCoverDef : public SpawnerLoginChunkNode
{
public:
	SpawnerLoginChunkCoverDef(const SpawnerCoverDef& coverDef) : m_coverDef(coverDef) {}

	virtual const SpawnerCoverDef* GetCoverDef() const override { return &m_coverDef; }

private:
	SpawnerCoverDef m_coverDef;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct CoverFrustum
{
public:
	Vector m_planeNormUpperWs;
	Vector m_planeNormLowerWs;
	Vector m_planeNormLeftWs;
	Vector m_planeNormRightWs;

	bool IsVectorInFrustum(Vector_arg toTargetWs) const
	{
		const Vec4 vDots(Dot(toTargetWs, m_planeNormUpperWs),
						 Dot(toTargetWs, m_planeNormLowerWs),
						 Dot(toTargetWs, m_planeNormLeftWs),
						 Dot(toTargetWs, m_planeNormRightWs));

		return AllComponentsGreaterThan(vDots, Vec4(kZero));
	}

	Scalar GetVectorOutsideFrustumSine(Vector_arg toTargetWs) const
	{
		// returns the sine of the largest angle made between the vector and the outside of the frustum;
		// if the vector lies INSIDE the frustum, the sine returned is NEGATIVE

		const Scalar sine0 = SCALAR_LC(-1.0f) * Dot(toTargetWs, m_planeNormUpperWs);
		const Scalar sine1 = SCALAR_LC(-1.0f) * Dot(toTargetWs, m_planeNormLowerWs);
		const Scalar sine2 = SCALAR_LC(-1.0f) * Dot(toTargetWs, m_planeNormLeftWs);
		const Scalar sine3 = SCALAR_LC(-1.0f) * Dot(toTargetWs, m_planeNormRightWs);

		const Scalar worstSine = Max(Max(Max(sine0, sine1), sine2), sine3);

		return worstSine;
	}

	void DebugDraw(Point_arg frustumPosWs, F32 cullDist, const Color colors[4], DebugPrimTime tt) const;
};

FWD_DECL_PROCESS_HANDLE(NavCharacter);

/// --------------------------------------------------------------------------------------------------------------- ///
/// Class CoverActionPack: Cover point action pack.
/// --------------------------------------------------------------------------------------------------------------- ///
class CoverActionPack : public ActionPack
{
public:
	DECLARE_ACTION_PACK_TYPE(CoverActionPack);

	static CONST_EXPR float kCoverOffsetAmount = 0.25f;

	enum BlockingDirection
	{
		kBlockingOver,
		kBlockingSide,
		kBlockingCount
	};

	enum PreferredDirection
	{
		kPreferNone,
		kPreferLeft,
		kPreferRight
	};

	static const NavPoly* CanRegisterSelf(const CoverDefinition& coverDef,
										  const ActionPackRegistrationParams& params,
										  const BoundFrame& apLoc,
										  Point* pNavPolyPointWs,
										  bool debugDraw);

	static const NavPoly* FindRegisterNavPoly(const BoundFrame& apLoc,
											  const CoverDefinition& coverDef,
											  const ActionPackRegistrationParams& params,
											  Point* pNavPolyPointWs,
											  const NavMesh** ppNavMeshOut,
											  bool debugDraw);

	CoverActionPack(const BoundFrame& apLoc,
					const EntitySpawner* pSpawner,
					const CoverDefinition& cover,
					I32 bodyJoint);

	CoverActionPack(const BoundFrame& apLoc,
					F32 registrationDist,
					const Level* pLevel,
					const CoverDefinition& cover,
					I32 bodyJoint);

	CoverActionPack(const BoundFrame& apLoc,
					F32 registrationDist,
					const Level* pAllocLevel,
					const Level* pRegLevel,
					const CoverDefinition& cover,
					I32 bodyJoint);

	virtual void Reset() override;
	virtual void DebugDraw(DebugPrimTime tt = kPrimDuration1FrameAuto) const override;
	virtual void DebugDrawRegistrationFailure() const override;
	virtual void AdjustGameTime(TimeDelta delta) override;
	void DebugDrawRegistrationProbes() const;
	void DebugDrawPointOutsideFrustumSine(Point_arg defenseTestPosWs, Color color, DebugPrimTime duration) const;
	I32 GetBodyJoint() const { return m_bodyJoint; }

	virtual const Point GetVisibilityPositionPs() const override;
	const Point GetIdleSidePositionWs() const;
	virtual const Point GetFireSidePositionWs() const override;
	virtual const Point GetFireOverPositionWs() const override;
	const Point GetOffsetOverPositionWs(float side, float up) const;
	const Point GetIdleSidePositionPs() const;
	virtual const Point GetFireSidePositionPs() const override;
	virtual const Point GetFireOverPositionPs() const override;
	virtual const Point GetCoverPositionPs() const override;

	const Point GetOffsetSidePositionWs(float offset, float up) const;
	const Point GetOffsetSidePositionPs(F32 offset) const;

	// Action pack implementation.
	void SetTempCost(float cost);
	const Vector GetCoverDirectionWs() const;
	const Vector GetCoverDirectionPs() const;

	bool HasCoverFeatureVerts() const { return m_hasCoverFeatureVerts; }
	Scalar GetPointOutsideFrustumSine(Point_arg testPosWs) const;
	const CoverFrustum& GetProtectionFrustum() const
	{
		ASSERT(m_hasProtectionFrustum);
		return m_protectionFrustum;
	}

	bool IsPointWsInProtectionFrustum(Point_arg testPointWs, Scalar_arg maxSinOffAngle = SCALAR_LC(0.0f)) const;
	Point GetProtectionPosWs() const;

	bool GetCoverFeatureExtents(Scalar& minXOffset, Scalar& maxXOffset) const;
	bool GetCoverFeatureVertsWs(Point& v0, Point& v1) const;
	void SetCoverFeatureVertsWs(Point_arg leftWs, Point_arg rightWs);
	const NavLocation& GetOccupantNavLoc() const
	{
		return m_occupantNavLoc;
	}
	void SetOccupantNavLoc(NavLocation& occupantNavLoc)
	{
		m_occupantNavLoc = occupantNavLoc;
	}

	// for left cover types, returns a vector to the right
	// for right cover types, returns a vector to the left
	// for over cover, returns zero vector
	const Vector GetWallDirectionWs() const;
	const Vector GetWallDirectionPs() const;

	const Point GetDefensivePosWs() const;
	const Point GetDefensivePosPs() const;

	const I32 GetNumNearbyCoverAps() const { return m_numNearbyCoverAps; }

	const CoverDefinition& GetDefinition() const { return m_definition; }

	static bool MakeCoverDefinition(const Locator& cornerSpace,
									const FeatureCorner& srcCorner,
									CoverDefinition* pCoverDef,
									Locator* pApLoc);

	bool TryAddBlockingNavChar(NavCharacter* pBlockingNavChar);
	const NavCharacter* GetBlockingNavChar() const;

	virtual bool HasNavMeshClearance(const NavLocation& navLoc,
									 bool debugDraw = false,
									 DebugPrimTime tt = kPrimDuration1FramePauseable) const override;
	virtual void RefreshNavMeshClearance() override;

	RigidBodyHandle GetBlockingRigidBody(BlockingDirection dir) const;

	virtual bool CheckRigidBodyIsBlocking(RigidBody* pBody, uintptr_t userData) override;
	bool CheckRigidBodyIsBlockingForDir(RigidBody* pBody, BlockingDirection iDir);
	virtual void RemoveBlockingRigidBody(const RigidBody* pBody) override;

	MutableNdGameObjectHandle GetDoor() { return m_hDoor; }
	NdGameObjectHandle GetDoor() const { return m_hDoor; }

	void SetDoor(NdGameObject* pDoor);
	void SetDoorIsBlocking(bool b)
	{
		ASSERT(m_hDoor.HandleValid());
		m_doorIsBlocking = b;
	}

	bool IsDoorBlocking() const { return m_doorIsBlocking; }
	bool AllowCornerCheck() const;

	virtual StringId64 GetDcTypeNameId() const override
	{
		const CoverDefinition& coverDef = GetDefinition();

		switch (coverDef.m_coverType)
		{
		case CoverDefinition::kCoverStandLeft:		return SID("cover-action-pack-stand-l");
		case CoverDefinition::kCoverStandRight:		return SID("cover-action-pack-stand-r");
		case CoverDefinition::kCoverCrouchLeft:		return SID("cover-action-pack-crouch-l");
		case CoverDefinition::kCoverCrouchRight:	return SID("cover-action-pack-crouch-r");
		case CoverDefinition::kCoverCrouchOver:		return SID("cover-action-pack-crouch-over");
		}

		return SID("cover-action-pack");
	}

protected:
	virtual Vector GetDefaultEntryOffsetLs() const override;
	virtual bool RegisterInternal() override;
	virtual void UnregisterInternal() override;

	virtual bool IsAvailableForInternal(const Process* pProcess) const override;

	void SearchForBlockingRigidBodies();
	bool GetCheckBlockingObb(BlockingDirection iDir, Obb& obbOut) const;
	void SetBlockingRigidBody(RigidBody* pBody, BlockingDirection iDir);
	void RemoveBlockingRigidBody(BlockingDirection iDir);

	void SearchForDoor();

private:
	void TryCollectFeatureDbInfo(const Level* pLevel);
	bool TryCollectFeatureDbInfoFromCover(const Level* pLevel);
	bool TryCollectFeatureDbInfoFromEdges(const Level* pLevel);
	void RequestNearbyCoverLinkageUpdate();

	void RefreshProtectionFrustum();
	void BuildProtectionFrustum(CoverFrustum* pFrustum, const ProtectionFrustumAngles& angles) const;

	CoverDefinition m_definition;
	ActionPackHandle m_adjacentCovers[2];

	// number of nearby cover APs within the adjacent cover radius
	// (even if they don't meet the requirements for being the best adjacent cover)
	// useful for determining whether a post is a dead end
	I32 m_numNearbyCoverAps;

	I32 m_bodyJoint; // joint of the rigid body this cover is based on or -1 if none
	Point m_coverFeatureVertsLs[2];
	CoverFrustum m_protectionFrustum;
	NavCharacterHandle m_hBlockingNavChar;
	RigidBodyHandle m_hBlockingRigidBody[2];
	MutableNdGameObjectHandle m_hDoor;
	TimeFrame m_navCharBlockTime;
	NavLocation m_occupantNavLoc;
	bool m_hasCoverFeatureVerts : 1;
	bool m_hasProtectionFrustum : 1;
	bool m_doorIsBlocking : 1;

	friend class CoverBlockingBodyCollector;
};
