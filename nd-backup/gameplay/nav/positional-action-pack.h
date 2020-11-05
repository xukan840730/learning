/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "gamelib/gameplay/nav/action-pack.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class Level;
FWD_DECL_PROCESS_HANDLE(NavCharacter);
class NavMesh;
class NavPoly;

/// --------------------------------------------------------------------------------------------------------------- ///
class SpawnerPositionalActionPackDef
{
public:
	Locator m_locPs;
	StringId64 m_bindSpawnerNameId;
};

class FeatureDb;

/// --------------------------------------------------------------------------------------------------------------- ///
/// Class PositionalActionPack: Perch point action pack.
/// --------------------------------------------------------------------------------------------------------------- ///
const int kMaxPostsForPositionalActionPack = 64;

class PositionalActionPack : public ActionPack
{
public:
	DECLARE_ACTION_PACK_TYPE(PositionalActionPack);

	static bool CanRegisterSelf(const ActionPackRegistrationParams& params,
								const BoundFrame& apLoc,
								const NavMesh** ppNavMeshOut = nullptr,
								const NavPoly** ppNavPolyOut = nullptr);

	static const NavPoly* FindRegisterNavPoly(const BoundFrame& apLoc,
											  const ActionPackRegistrationParams& params,
											  Point* pNavPolyPointWs = nullptr,
											  const NavMesh** ppNavMeshOut = nullptr,
											  const NavPoly** ppNavPolyOut = nullptr);

	PositionalActionPack(const BoundFrame& apLoc,
					const EntitySpawner* pSpawner);

	PositionalActionPack(const BoundFrame& apLoc,
					F32 registrationDist,
					const Level* pLevel);

	virtual void DebugDraw(DebugPrimTime tt = kPrimDuration1FrameAuto) const override;
	virtual bool IsAvailableFor(const Process* pNavChar) const override;

	void DebugDrawRegistrationProbes() const;

	bool TryAddBlockingNavChar(NavCharacter* pBlockingNavChar);
	const NavCharacter* GetBlockingNavChar() const;

	void AddPostId(U16 postId);
	void ClearPostIds();
	I32 GetNumPosts() const { return m_numPosts; }
	U16 GetPostId(int index)
	{
		NAV_ASSERT(index < m_numPosts);
		return m_postIds[index];
	}

protected:
	virtual bool RegisterInternal() override;

private:
	NavCharacterHandle m_hBlockingNavChar;
	TimeFrame			m_navCharBlockTime;
	int					m_numPosts;
	U16					m_postIds[kMaxPostsForPositionalActionPack];
};
