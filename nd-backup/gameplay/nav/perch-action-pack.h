/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/gameplay/nav/action-pack.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class FeatureEdge;
class Level;
FWD_DECL_PROCESS_HANDLE(NavCharacter);
class NavMesh;
class NavPoly;

/// --------------------------------------------------------------------------------------------------------------- ///
struct PerchDefinition
{
	StringId64 m_perchType;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class SpawnerPerchActionPackDef
{
public:
	Locator m_locPs;
	StringId64 m_bindSpawnerNameId;
};

class FeatureDb;

/// --------------------------------------------------------------------------------------------------------------- ///
/// Class PerchActionPack: Perch point action pack.
/// --------------------------------------------------------------------------------------------------------------- ///
class PerchActionPack : public ActionPack
{
public:
	DECLARE_ACTION_PACK_TYPE(PerchActionPack);

	static const float kPerchOffsetAmount;

	static bool CanRegisterSelf(const PerchDefinition& PerchDef,
								const ActionPackRegistrationParams& params,
								const BoundFrame& apLoc,
								const NavMesh** ppNavMeshOut = nullptr,
								const NavPoly** ppNavPolyOut = nullptr);

	static const NavPoly* FindRegisterNavPoly(const BoundFrame& apLoc,
											  const PerchDefinition& PerchDef,
											  const ActionPackRegistrationParams& params,
											  Point* pNavPolyPointWs = nullptr,
											  const NavMesh** ppNavMeshOut = nullptr,
											  const NavPoly** ppNavPolyOut = nullptr);

	PerchActionPack(const BoundFrame& apLoc,
					const EntitySpawner* pSpawner,
					const PerchDefinition& perchDef,
					I32 bodyJoint);

	PerchActionPack(const BoundFrame& apLoc,
					F32 registrationDist,
					const Level* pLevel,
					const PerchDefinition& perchDef,
					I32 bodyJoint);

	PerchActionPack(const BoundFrame& apLoc, F32 registrationDist, const Level* pAllocLevel, const Level* pRegLevel, const PerchDefinition& perchDef, I32 bodyJoint);

	virtual void DebugDraw(DebugPrimTime tt = kPrimDuration1FrameAuto) const override;
	virtual bool IsAvailableFor(const Process* pNavChar) const override;

	void DebugDrawRegistrationProbes() const;
	StringId64 GetPerchType() const { return m_definition.m_perchType;  }
	void SetPerchType(StringId64 type) { m_definition.m_perchType = type; }

	const Vector GetPerchDirectionWs() const;
	const Vector GetPerchDirectionPs() const;

	I32 GetBodyJoint() const { return m_bodyJoint; }

	const PerchDefinition& GetDefinition() const { return m_definition; }

	static size_t MakePerchDefinition(const Locator& edgeSpace,
									  const FeatureEdge& edge,
									  PerchDefinition* pPerchDef,
									  Locator* pApLoc,
									  const FeatureDb* pFeatureDb);

	bool TryAddBlockingNavChar(NavCharacter* pBlockingNavChar);
	const NavCharacter* GetBlockingNavChar() const;

protected:
	virtual Vector GetDefaultEntryOffsetLs() const override;
	virtual bool RegisterInternal() override;

private:
	I32					m_bodyJoint; // joint of the rigid body this cover is based on or -1 if none
	PerchDefinition		m_definition;
	NavCharacterHandle m_hBlockingNavChar;
	TimeFrame			m_navCharBlockTime;
};

const int kMaxPerchesPerEdge = 100;
