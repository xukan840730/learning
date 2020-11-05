/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#ifndef NDPHYS_ROPE2_COLLECTOR_H 
#define NDPHYS_ROPE2_COLLECTOR_H 

#include "gamelib/ndphys/havok-collision-cast.h"
#include "gamelib/ndphys/havok-game-cast-filter.h"
#include "corelib/math/aabb.h"

class RopeCollider;

struct Rope2Collector : public HavokAabbQueryCollector
{
	Rope2Collector();
	void BeginCast(Rope2* pRope, const CollideFilter& filter, bool bStrained);
	void BeginValidateCache(Rope2* pRope, const CollideFilter& filter);
	virtual bool AddBody(RigidBody* pBody) override;
	void AddCollider(const RopeCollider* pCollider, const RopeColliderHandle& hCollider);
	void EndCast();

	bool IsCacheValid() const { return m_bCacheValid; }

private:
	bool ValidateColliderInCache(const RigidBody* pBody);

	Rope2* m_pRope;
	HavokGameCastFilter* m_pFilter;
	bool m_bStrained;
	bool m_bValidateCache;
	bool m_bCacheValid;
};


#endif // NDPHYS_ROPE2_COLLECTOR_H   

