/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef HORSE_JUMP_CONTROLLER_H
#define HORSE_JUMP_CONTROLLER_H

#include "ndlib/anim/anim-action.h"

#include "gamelib/gameplay/ai/controller/animaction-controller.h"

#include "game/player/jumps/player-jump-edge-filter.h"
#include "game/vehicle/horse-jump-defines.h"

class AnimStateLayer;
class AnimStateInstance;
class NdGameObject;
class NavCharacter;
class SimpleNavControl;
class NavControl;
class Horse;
class Player;

struct FadeToStateParams;
struct CanClimbProbe;
struct EdgeInfo;

class PreparedHorseJump
{
private:
	IHorseJump* m_pJump;
	HorseJumpEdgeRating m_edge;
	Point m_targetPt;
	Vector m_targetFacingDir;
	SphereCastJob m_groundFindJob;

	void Kick();
	void Gather();

public:
	PreparedHorseJump();
	void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound);
	void Reset();
	bool IsValid() const;
	void Prepare(IHorseJump* pHorseJump, const HorseJumpEdgeRating& edge, Point_arg moveToPt, Vector_arg desiredFacingDir);
	void DebugDraw() const;
	void Update();

	Point GetTargetPt() const { return m_targetPt; }
	Vector GetTargetFacingDir() const { return m_targetFacingDir; }
	const HorseJumpEdgeRating& GetEdgeRating() const { return m_edge; }
	IHorseJump* GetJump() const { return m_pJump; }
};

/// --------------------------------------------------------------------------------------------------------------- ///
class HorseJumpController : public AnimActionController
{
public:
	typedef AnimActionController ParentClass;

	static CONST_EXPR StringId64 kHorseJumpAnimStates[] =
	{
		SID("horse-mm-canter^vault-05m"),
		SID("horse-mm-canter^vault-1m"),
		SID("horse-mm-canter^vault-1-5m"),

		SID("horse-mm-walk^vault-1m"),
		SID("horse-mm-walk^vault-1-5m"),

		SID("horse-mm-idle^vault-1-5m"),
		SID("horse-mm-idle^vault-1m"),
		SID("horse-mm-idle^vault-05m"),

		SID("horse-mm-canter^vault-1m^jump-dn-2m"),
		SID("horse-mm-walk^vault-1m^jump-dn-2m"),
		SID("horse-mm-idle^vault-1m^jump-dn-2m"),

		SID("horse-mm-canter^jump-down-2m"),
		SID("horse-mm-canter^jump-down-1-5m"),
		SID("horse-mm-canter^jump-down-1m"),
		SID("horse-mm-canter^jump-down-05m"),

		SID("horse-mm-walk^jump-dn-2m"),
		SID("horse-mm-walk^jump-dn-1-5m"),
		SID("horse-mm-walk^jump-dn-1m"),
		SID("horse-mm-walk^jump-down-05m"),

		SID("horse-mm-idle^jump-dn-2m-idle"),
		SID("horse-mm-idle^jump-dn-1-5m-idle"),
		SID("horse-mm-idle^jump-dn-1m-idle"),
		SID("horse-mm-idle^jump-dn-05m-idle"),

		SID("horse-mm-canter^jump-up-1m"),
		SID("horse-mm-canter^jump-up-05m"),

		SID("horse-mm-walk^jump-up-05m"),

		SID("horse-mm-idle^jump-up-1m-idle"),
		SID("horse-mm-idle^jump-up-05m-idle"),
	};

	static CONST_EXPR U32F kNumJumpStates = ARRAY_COUNT(kHorseJumpAnimStates);

	HorseJumpController()
		: m_pHorse(nullptr)
		, m_pCurrJump(nullptr)
		, m_curJumpId(INVALID_STRING_ID_64)
		, m_pBestJump(nullptr)
		, m_jumpStartedThisFrame(false)
		, m_isJumping(false)
		, m_isMidair(false)
		, m_isJumpStarting(false)
		, m_longDropDownEnabled(false)
		, m_vaultDropDownEnabled(false)
		, m_regularVaultEnabled(false)
		, m_jumpUpDisabled(false)
		, m_inPermissiveVaultRegion(false)
		, m_bestJumpTooFar(false)
	{}

	virtual ~HorseJumpController();

	virtual void Init(NdGameObject* pCharacter, const SimpleNavControl* pNavControl) override;
	virtual void Init(NavCharacter* pNavChar, const NavControl* pNavControl) override;

	virtual void Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound) override;
	void OnHorseUpdate(); // I need this to update at a specific time in horse's update rather than when RequestAnimations is run
	virtual void UpdateStatus() override;
	// virtual void RequestAnimations() override; // replaced by OnHorseUpdate
	virtual bool IsBusy() const override { return m_jumpAction.IsValid() && !m_jumpAction.IsDone(); }
	virtual void Reset() override;
	virtual void Interrupt() override;
	virtual bool ShouldInterruptNavigation() const override { return IsJumping(); }

	void FadeToJump(StringId64 jumpId, FadeToStateParams params, float landPhase);

	bool JumpInFront(Point_arg contactPos, bool asSphereLineProbe = false, float searchRadius = 0.75f, bool requireJumpCommand = true, bool includeDropDown = true, bool includeNonDropDown = true) const;
	bool IsJumping() const;
	bool IsJumpStarting() const;
	bool DidJumpStartThisFrame() const { return m_jumpStartedThisFrame; }
	bool IsMidair() const;
	bool IsInJumpUp() const;
	bool IsInRunningJump() const;

	U32F GetVirtualWalls(VirtualCliffWall aOutWalls[], U32F capacity);
	const EdgeInfo* GetJumpEdges(U32F* pEdgeCount) const;

	float GetCameraYSpringConstant(float defaultValue);

	F32 GetControlBlend() const;
	bool JumpHasFullControl() const;

	//virtual void UpdateJumpAp(Vector_arg deltaPos, Quat_arg rot);
	void UpdateJumpAp(Vector_arg stickInput);

	IHorseJump* GetActiveJump() const;

	bool IsVaultDropDownEnabled() const { return m_vaultDropDownEnabled; }
	bool IsRegularVaultEnabled() const { return m_regularVaultEnabled; }
	bool IsLongDropDownEnabled() const { return true; }//{ return m_longDropDownEnabled; }
	bool IsJumpUpEnabled() const { return !m_jumpUpDisabled && !m_vaultDropDownEnabled; }
	bool IsInPermissiveVaultRegion() const { return m_inPermissiveVaultRegion; }
	bool IsInDistanceHackRegion() const { return m_inDistanceHackRegion; }

	bool IsJumpAnim(StringId64 animStateName) const;
	void EndJump();

	bool IsPlayingLongDropDown() const { return m_pCurrJump && m_pCurrJump->IsLongDropDown(); }

	bool IsJumpCommandActive() const;

	bool TryJump();

	bool HasPreparedJump() const;
	void ClearPreparedJump();
	Point GetPreparedJumpStartPos() const;
	Vector GetPreparedJumpFacingDir() const;
	bool TryPreparedJump();
	void DebugDrawPreparedJump() const { STRIP_IN_FINAL_BUILD; m_preparedJump.DebugDraw(); }

	bool WantToJump() { return m_wantToJump; }

	float GetGroundAdjustFactor() const { return IsMidair() ? 0.0f : 1.0f; }
	bool IsJumpEdgeNearby(float searchRadius) const { return m_distToNearestJumpEdge < searchRadius; }
	bool ShouldShrinkNavCapsule() const { return m_distToNearestJumpEdge < 3.0f; }

	bool IsInJumpAnim() const;

	bool AreRunningJumpsValid() const;
	bool AreRuningJumpsForceAllowed() const;
	void ScriptForceEnableRunningJump();

	void ScriptForceJumpSpeed(float desiredSpeed);
	float GetScriptedJumpSpeed() const; //negative if invalid

	bool WouldJumpTriggerWhenInputValid() const { return m_canJump; }

	void OnKillProcess();

	void ScriptForceJumpDir(Vector_arg dir);
	Maybe<Vector> GetForceJumpDir() const;

	void ScriptForceJumpDestination(Locator loc);
	Maybe<Locator> GetForceJumpDestination() const;

protected:
	const AnimStateInstance* FindJumpAnimState() const;

	bool CheckIsJumping(const AnimStateInstance** ppAnimState) const;
	bool GatherJumpBucket(EdgeList& edges, Vector_arg direction, HorseJumpBucket bucket, float& bestDistSqr, bool bucketIsValid); //bestDistSqr should be kLargeFloat when passed in to first bucket
	bool GatherJumps();


	void PrepareBucket(EdgeList& edges, Vector_arg direction, HorseJumpBucket bucket, Player* pPlayer);
	void KickBucket(EdgeList& edges, Vector_arg direction, HorseJumpBucket bucket, Player* pPlayer);

	void GatherValidJumpEdges();
	void GatherVirtualWalls();

	void KickCliffDetector();
	void KickJumpDetector();
	void Kick();

	void DoBestJump();
	void DoJumpInternal(IHorseJump* pJump);
	void PrepareAndMoveToJump(IHorseJump* pJump);
	void UpdateJump(const AnimStateInstance* pAnimState);
	void SetupDetectorFlags(IHorseJumpDetector* pDetector);
	F32 GetJumpPhase(const AnimStateInstance* pStateInstance) const;

	F32 GetBaseControlBlend() const;

	void InitInternal(Horse* pHorse);
	void UpdateJumpRegions();
	void UpdateJumpButton();

	void DebugDrawHorseHoofProbeSettings() const;

	void CheckForTypos() const;

	bool IsJumpDetectorEnabled() const;

	bool IsBucketValid(HorseJumpBucket bucket, bool kicking = false) const;
	bool IsBucketInputValid(HorseJumpBucket bucket) const;

	bool CanTryToJump() const;

protected:

	Horse* m_pHorse;
	HorseJumpList m_jumpList;
	JumpEdgeFilter m_filter;
	JumpEdgeFilter m_detectorFilter;
	JumpEdgeFilter m_cliffFilter;
	JumpEdgeFilter m_directionalCliffFilter;
	IHorseJump* m_pCurrJump;
	IHorseJump* m_pBestJump;
	I64 m_lastJumpButtonFrame;
	float m_distToNearestJumpEdge;
	bool m_jumpStartedThisFrame;
	bool m_isJumping;
	mutable bool m_isMidair;
	mutable bool m_isJumpStarting;
	bool m_longDropDownEnabled;
	bool m_jumpUpDisabled;
	bool m_vaultDropDownEnabled;
	bool m_regularVaultEnabled;
	bool m_inNoJumpRegion;
	bool m_inPermissiveVaultRegion;
	bool m_inDistanceHackRegion;
	bool m_bestJumpTooFar;
	bool m_wantToJump;
	bool m_canJump;

	StringId64 m_curJumpId;
	AnimActionWithSelfBlend m_jumpAction;
	PreparedHorseJump m_preparedJump;

	EdgeList m_edgeList;
	
	I64 m_forceAllowRunningJumpFrame;

	I64 m_scriptForceJumpSpeedFrame;
	float m_scriptedJumpSpeed;

	I64 m_forceJumpDirFrame;
	Vector m_forceJumpDir;

	I64 m_forceJumpDestinationFrame;
	Locator m_forceJumpDestination;

#ifndef FINAL_BUILD
	bool m_alreadyComplainedAboutBadEdges;
#endif
};

HorseJumpController* CreateHorseJumpController();

#endif // HORSE_JUMP_CONTROLLER_H
