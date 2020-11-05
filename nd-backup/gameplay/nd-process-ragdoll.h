/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ND_PROCESS_RAGDOLL_H
#define ND_PROCESS_RAGDOLL_H

#include "gamelib/gameplay/character.h"
#include "gamelib/gameplay/nav/nav-location.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/script/script-manager.h"

class CompositeBody;
class NdGameObject;
class ProcessSpawnInfo;
namespace DC {
struct CharacterCollision;
}  // namespace DC

// This class should hold common functionality for process ragdolls. There is just a little bit here for now but much more could be here
// to make maintenance easier.
// Feel free to bring code over if you know what you're doing.

/// --------------------------------------------------------------------------------------------------------------- ///
struct NdRagdollSpawnInfo
{
	NdRagdollSpawnInfo()
		: m_processToKillOnSpawn(nullptr)
		, m_pRagdollCompositeBodyToClone(nullptr)
		, m_specifiedPriority(-1)
		, m_associatedLevel(INVALID_STRING_ID_64)
		, m_gameTaskCombinedName(INVALID_STRING_ID_64)
		, m_spawnPoseAnim(INVALID_STRING_ID_64)
		, m_spawnPoseAnimLooping(false)
		, m_spawnedFromColdStorage(false)
		, m_physicalizeOnSpawn(true)
		, m_tryAttachParentProbe(true)
	{}

	CharacterHandle m_processToKillOnSpawn;
	CompositeBody* m_pRagdollCompositeBodyToClone;
	StringId64 m_associatedLevel;
	StringId64 m_gameTaskCombinedName;
	StringId64 m_spawnPoseAnim;
	bool m_spawnPoseAnimLooping;
	bool m_spawnedFromColdStorage;
	bool m_physicalizeOnSpawn;
	bool m_tryAttachParentProbe;
	I32 m_specifiedPriority; // priority that can be optionally set on a per-npc/body basis (default -1)
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NdProcessRagdoll : public Character
{
private: typedef Character ParentClass;

public:
	virtual Err Init(const ProcessSpawnInfo& info) override;
	virtual void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound) override;
	virtual void PreUpdate() override;

	virtual Locator GetSite(StringId64 nameId) const override;
	virtual StringId64 GetDefaultAmbientOccludersId() const override { return m_ambientOccludersId; }

	// if a ragdoll is in stealth vegetation, it's considered hidden in stealth vegetation
	virtual bool IsInStealthVegetation() const override { return m_isInStealthVegetation; }
	virtual bool IsHiddenInStealthVegetation() const override { return m_isInStealthVegetation; }

	virtual NavLocation GetNavLocation() const override { return m_navLocation; }

	virtual MeshProbe::SurfaceType GetCachedFootSurfaceType() const override { return m_cachedFootSurfaceType; }

	virtual bool IsClippingCameraPlane(bool useCollCastForBackpack, float* pNearestApproach = nullptr) const override;

	virtual bool ShouldUpdateSplashers() const override { return !IsRagdollAsleep(); }

protected:
	void CopyJointsFrom(const NdGameObject* pSrcObject);
	void CreateBasePoseNode(const DualSnapshotNode* pSourcePose);
	void PrepareDeferredBasePoseJoints();

	bool IsInPoseAnim() const;
	void DislodgeFromPoseAnim();
	bool StartPoseAnim(StringId64 poseAnim, bool looping);

	virtual void UpdateHighContrastModeForFactionChange() override {}

	float WaterDepth() const override;

	DualSnapshotNode m_sourcePose;

	bool m_isInStealthVegetationEvaluated;
	bool m_isInStealthVegetation;
	bool m_inLoopingPoseAnim;
	StringId64 m_ambientOccludersId;

	NavLocation m_navLocation;

	MeshProbe::SurfaceType m_cachedFootSurfaceType;

	ScriptPointer<DC::CharacterCollision> m_characterCollision;
};

PROCESS_DECLARE(NdProcessRagdoll);

#endif //ND_PROCESS_RAGDOLL_H
