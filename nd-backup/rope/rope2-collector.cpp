/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/ndphys/rope/rope2.h"
#include "gamelib/ndphys/rope/rope2-collector.h"
#include "gamelib/ndphys/rope/physvectormath.h"
#include "gamelib/ndphys/rope/physalignedarrayonstack.h"
#include "gamelib/ndphys/rope/rope-mgr.h"
#include "gamelib/ndphys/havok-game-cast-filter.h"
#include "ndlib/profiling/profiling.h"
#include "corelib/math/gamemath.h"
#include "corelib/math/aabb-util.h"
#include "ndlib/render/util/prim.h"
#include "gamelib/ndphys/rigid-body.h"
#include "gamelib/ndphys/havok-internal.h"
#include "physics/havokext/havok-shapetag-codec.h"

#include <Physics/Physics/Collide/Shape/Convex/Triangle/hknpTriangleShape.h>
#include <Physics/Physics/Collide/Shape/Composite/Compound/hknpCompoundShape.h>
#include <Physics/Physics/Collide/Shape/Composite/Mesh/Compressed/hknpCompressedMeshShape.h>
#include <Physics/Physics/Collide/Shape/hknpShapeCollector.h>
#include <Physics/Physics/Collide/Query/hknpCollisionQuery.h>
#include <Physics/Physics/Collide/Query/Collector/hknpAllHitsCollector.h>

Rope2Collector::Rope2Collector() :
	m_pRope(nullptr),
	m_pFilter(nullptr),
	m_bValidateCache(false)
{
}

void Rope2Collector::BeginCast(Rope2* pRope, const CollideFilter& filter, bool bStrained)
{
	ASSERT(m_pRope == nullptr);
	m_pRope = pRope;
	if (filter.m_useCustomFilter)
	{
		m_pFilter = filter.m_pCustomFilter;
		m_pFilter->addReference();
	}
	else
	{
		m_pFilter = new HavokGameCastFilter(filter.m_filterBase);
	}
	m_bStrained = bStrained;
	ASSERT(m_pRope != nullptr);
	m_bValidateCache = false;
}

void Rope2Collector::BeginValidateCache(Rope2* pRope, const CollideFilter& filter)
{
	BeginCast(pRope, filter, true);
	m_bValidateCache = true;
	m_bCacheValid = true;
}

void Rope2Collector::EndCast()
{
	m_pFilter->removeReference();
	m_pFilter = nullptr;
	m_pRope = nullptr;
}

bool Rope2Collector::AddBody(RigidBody* pBody)
{
	ASSERT(m_pRope);

	if (m_pRope->GetIgnoreCollision(pBody))
	{
		return true;
	}

	if (m_bValidateCache)
	{
		bool bValid = ValidateColliderInCache(pBody);
		if (!bValid)
		{
			m_bCacheValid = false;
			return false;
		}
		return true;
	}

	RopeColliderHandle hCollider(pBody);

	// Get shape from probe info since we know it's locked so it's safe to get it from there (otherwise we would need to lock havok world)
	RopeCollider collider;
	collider.FromRigidBodyAndShape(pBody, g_pRigidBodyProbeInfo[pBody->GetHandleIndex()].m_pShape, m_pRope, m_pRope->m_scInvStepTime);

	AddCollider(&collider, hCollider);

	return true;
}

class TriKeysCollector : public hknpCollisionQueryCollector
{
public:
	TriKeysCollector(hkArray<hknpShapeKey>* pKeyArray)
		: m_pKeyArray(pKeyArray)
	{}

	virtual void addHit( const hknpCollisionResult& hit ) HK_OVERRIDE
	{
		m_pKeyArray->pushBack(hit.m_hitBodyInfo.m_shapeKey);
	}

	hkArray<hknpShapeKey>* m_pKeyArray;
};

void Rope2Collector::AddCollider(const RopeCollider* pCollider, const RopeColliderHandle& hCollider)
{
	ASSERT(m_pRope);

	if (pCollider->m_pShape->getType() == hknpShapeType::COMPRESSED_MESH)
	{
		const hknpCompressedMeshShape* pMesh = static_cast<const hknpCompressedMeshShape*>(pCollider->m_pShape);

		if (m_bStrained)
		{
			m_pRope->CollideStrainedWithMesh(pMesh, hCollider, m_pFilter);
			return;
		}

		Aabb aabb;
		GetAabbInNewSpace(m_pRope->GetAabbSlacky(), pCollider->m_loc.AsTransform(), aabb); 

		hknpInplaceTriangleShape targetTriangle( 0.0f );
		hknpCollisionQueryContext queryContext( nullptr, targetTriangle.getTriangleShape() );
		queryContext.m_dispatcher = g_havok.m_pWorld->m_collisionQueryDispatcher;

		hknpAabbQuery query;
		query.m_shapeTagCodec = g_havok.m_pShapeTagCodec;
		query.m_filter = m_pFilter;
		query.m_aabb.m_min = hkVector4(aabb.m_min.QuadwordValue());
		query.m_aabb.m_max = hkVector4(aabb.m_max.QuadwordValue());
					
		hknpQueryFilterData filterData;

		hknpShapeQueryInfo queryShapeInfo;

		hknpShapeQueryInfo targetShapeInfo;
		targetShapeInfo.m_rootShape = pMesh;

		hkLocalArray<hknpShapeKey> hits(1000);
		{
			TriKeysCollector collector(&hits);
			collector.m_hints.orWith(hknpCollisionQueryCollector::HINT_FORCE_TRIANGLES);

			{
				PROFILE(Havok, queryAabb);
				pMesh->queryAabbImpl(&queryContext, query, queryShapeInfo, filterData, targetShapeInfo, &collector, nullptr);
			}
		}
		
		U32F ii = 0;
		while (ii < hits.getSize())
		{
			hknpShapeCollector collector;
			collector.m_internal.m_flags.orWith(hknpShapeCollector::HINT_FORCE_TRIANGLES);
			pMesh->getLeafShapes(&hits[ii], hits.getSize()-ii, &collector);
			for (U32F iTri = 0; iTri<collector.getNumShapes(); iTri++)
			{
				hknpInplaceTriangleShape triPrototype;
				const hknpShape* pChildShape = collector.getTriangleShape(iTri, triPrototype.getTriangleShape());
				RopeCollider childCollider = *pCollider;
				childCollider.SetChildShape(pChildShape);
				m_pRope->CollideWithCollider(&childCollider, hCollider);
			}
			ii += collector.getNumShapes();
		}
	}
	else if (pCollider->m_pShape->getType() == hknpShapeType::COMPOUND)
	{
		const hknpCompoundShape* pCompound = static_cast<const hknpCompoundShape*>(pCollider->m_pShape);
		for (hknpCompoundShape::InstanceFreeListArray::Iterator iter = pCompound->getShapeInstanceIterator(); iter.isValid(); iter.next())
		{
			if (!iter.getValue().isEnabled())
				continue;

			hknpQueryFilterData filterData;
			HavokShapeTagCodec::decode(iter.getValue().getShapeTag(), pCompound, iter.getValue().getShape(), &filterData);
			if (!m_pFilter->isLayerCollisionEnabled(filterData.m_collisionFilterInfo))
				continue;
			if (!m_pFilter->isPatCollisionEnabled(filterData.m_userData))
				continue;

			U32 listIndex = iter.getIndex().value();
			RopeColliderHandle hChildCollider(hCollider.GetRigidBodyHandle(), listIndex);
			RopeCollider childCollider(*pCollider, listIndex);
			AddCollider(&childCollider, hChildCollider);
		}
	}
	else
	{
		if (m_bStrained)
		{
			m_pRope->CollideStrainedWithShape(pCollider->m_pShape, hCollider, pCollider->m_loc);
		}
		else
			m_pRope->CollideWithCollider(pCollider, hCollider);
	}
}

bool Rope2Collector::ValidateColliderInCache(const RigidBody* pBody)
{
	ASSERT(m_pRope);

	for (U32 iShape = 0; iShape < m_pRope->m_colCache.m_numShapes; iShape++)
	{
		if (m_pRope->m_colCache.m_pShapes[iShape].GetRigidBody() == pBody)
		{
			return true;
		}
	}

	const hknpShape* pShape = pBody->GetHavokShape();

	if (pShape->getType() == hknpShapeType::COMPRESSED_MESH)
	{
		const hknpCompressedMeshShape* pMesh = static_cast<const hknpCompressedMeshShape*>(pShape);
		if (!GetMeshOuterEdges(pMesh))
		{
			return true;
		}

		return false;
	}
	else if (pShape->getType() == hknpShapeType::COMPOUND)
	{
		const hknpCompoundShape* pCompound = static_cast<const hknpCompoundShape*>(pShape);
		for (hknpCompoundShape::InstanceFreeListArray::Iterator iter = pCompound->getShapeInstanceIterator(); iter.isValid(); iter.next())
		{
			if (!iter.getValue().isEnabled())
				continue;

			hknpQueryFilterData filterData;
			HavokShapeTagCodec::decode(iter.getValue().getShapeTag(), pCompound, iter.getValue().getShape(), &filterData);
			if (!m_pFilter->isLayerCollisionEnabled(filterData.m_collisionFilterInfo))
				continue;
			if (!m_pFilter->isPatCollisionEnabled(filterData.m_userData))
				continue;

			return false;
		}
	}
	else
	{
		return false;
	}

	return true;
}
