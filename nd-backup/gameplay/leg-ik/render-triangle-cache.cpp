/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/leg-ik/render-triangle-cache.h"

#include "corelib/containers/hashtable.h"
#include "corelib/util/hashfunctions.h"
#include "ndlib/render/util/prim.h"

#include <Common/Base/hkBase.h>
#include <Geometry/Internal/Algorithms/RayCast/hkcdRayCastTriangle.h>

class Material;
class FgInstance;
class MeshInstance;
class SubMeshInstance;
struct Background;

RenderTriangleCache::RenderTriangle::RenderTriangle( const MeshProbe::CallbackObject* pObj )
{
	m_pos[0] = Point(pObj->m_probeResults[0].m_vertexWs0);
	m_pos[1] = Point(pObj->m_probeResults[0].m_vertexWs1);
	m_pos[2] = Point(pObj->m_probeResults[0].m_vertexWs2);

	struct IdObj
	{
		//Background const *		m_pBackground;
		//FgInstance const *		m_pFgInstance;
		//MeshInstance const *	m_pInstance;
		//SubMeshInstance const *	m_pSubMesh;
		//BoundMaterial const *	m_pMaterial;
		U64						m_triangle;
	};

	IdObj id;
	//id.m_pBackground = pObj->m_probeResults[0].m_pBackground;
	//id.m_pFgInstance = pObj->m_probeResults[0].m_pFgInstance;
	//id.m_pInstance = pObj->m_probeResults[0].m_pInstance;
	//id.m_pSubMesh = pObj->m_probeResults[0].m_pSubMesh;
	//id.m_pMaterial = pObj->m_probeResults[0].m_pMaterial;
	id.m_triangle = pObj->m_probeResults[0].m_triangle;

	m_hash = HashT<IdObj>()(id);

	m_priority = 0;
}

RenderTriangleCache::RenderTriangleCache()
	: m_maxPriority(0)
{

}

void RenderTriangleCache::Init( int maxNumTriangles )
{
	m_hashTable.Init(maxNumTriangles, FILE_LINE_FUNC);
}

void RenderTriangleCache::AddTriangle( const MeshProbe::CallbackObject* pObj )
{
	PROFILE(Processes, RenderTriangleCache_AddTriangle);
	if (!pObj || pObj->m_probeResults[0].m_fgProcessId != 0)
	{
		return;
	}
	RenderTriangle triangle(pObj);
	//We may already have this.
	{
		RenderTriangleHash::Iterator it = m_hashTable.Find(triangle.GetId());
		if (it != m_hashTable.End())
		{
			it->m_data.m_priority = ++m_maxPriority;
			return;
		}
	}
	if (m_hashTable.IsFull())
	{
		PROFILE(Processes, RenderTriangleCache_RemoveTriangle);
		U32 minPriority = m_maxPriority;
		RenderTriangleHash::Iterator minPriorityIT;
		for (RenderTriangleHash::Iterator it = m_hashTable.Begin(); it != m_hashTable.End(); ++it)
		{
			if (it->m_data.m_priority < minPriority)
			{
				minPriority = it->m_data.m_priority;
				minPriorityIT = it;
			}
		}
		if (minPriorityIT != m_hashTable.End())
		{
			m_hashTable.Erase(minPriorityIT);
		}
	}
	
	{
		triangle.m_priority = ++m_maxPriority;
		m_hashTable.Add(triangle.GetId(), triangle);
	}
}

RenderTriangleCache::RayCastResult RenderTriangleCache::RayCast( Point_arg p, Vector_arg v ) const
{
	PROFILE(Processes, RenderTriangleCache_RayCast);
	RenderTriangleCache::RayCastResult result;
	result.m_tt = -1.0f;
	RenderTriangleHash::ConstIterator hitTri;
	float bestTT = -1.0f;
	for (RenderTriangleHash::ConstIterator it = m_hashTable.Begin(); it != m_hashTable.End(); ++it)
	{
		float currentIntersection = RayCastTriangle(p, v, it->m_data);
		if( currentIntersection >= 0.0f && (bestTT < 0.0f || bestTT > currentIntersection))
		{
			hitTri = it;
			bestTT = currentIntersection;
		}
	}

	if (bestTT >= 0.0f)
	{
		result.m_tt = bestTT;
		result.m_pos = p + v*bestTT;
		result.m_normal = hitTri->m_data.GetNormal();
	}

	return result;
}

void RenderTriangleCache::Relocate( ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound )
{
	m_hashTable.Relocate(offset_bytes, lowerBound, upperBound);
}

void RenderTriangleCache::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	PROFILE(Processes, RenderTriangleCache_DebugDraw);
	for (RenderTriangleHash::ConstIterator it = m_hashTable.Begin(); it != m_hashTable.End(); ++it)
	{
		const RenderTriangle* pTri = &it->m_data;
		g_prim.Draw(DebugTriangle(pTri->m_pos[0], pTri->m_pos[1], pTri->m_pos[2], kColorGreen, PrimAttrib(kPrimEnableWireframe)), kPrimDuration1FramePauseable);
	}
	m_hashTable.PrintAnalysis(kMsgConPauseable);
}

float RenderTriangleCache::RayCastTriangle( Point_arg p, Vector_arg c, const RenderTriangle& tri )
{
	hkcdRay ray;
	ray.setOriginDirection(hkVector4(p.QuadwordValue()),hkVector4(c.QuadwordValue()));

#if HAVOKVER >= 0x2016
	hkSimdReal fraction(1.0f);
	hkVector4 nonUnitNormalOut;
	hkcdRayCastResult result = hkcdRayCastTriangle::fastUsingZeroTolerance(ray, 
		hkVector4(tri.m_pos[0].QuadwordValue()), hkVector4(tri.m_pos[1].QuadwordValue()), hkVector4(tri.m_pos[2].QuadwordValue()),
		0, fraction, nonUnitNormalOut);
	if (result.isHit())
	{
		return fraction;
	}
#else
	hkSimdReal fraction(-1.0f);
	if (hkcdRayTriangleIntersect(ray,
		hkVector4(tri.m_pos[0].QuadwordValue()), hkVector4(tri.m_pos[1].QuadwordValue()), hkVector4(tri.m_pos[2].QuadwordValue()),
		&fraction))
	{
		return fraction;
	}
#endif
	return -1.0f;
}
