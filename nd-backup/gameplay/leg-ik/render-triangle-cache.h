/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef RENDER_TRIANGLE_CACHE_H
#define RENDER_TRIANGLE_CACHE_H

#include "corelib/containers/hashtable.h"
#include "ndlib/render/ngen/meshraycaster.h"

class RenderTriangleCache
{
public:
	struct RayCastResult
	{
		RayCastResult() : m_tt(-1.0f) {}
		Point m_pos;
		Vector m_normal;
		float m_tt;
	};

	RenderTriangleCache();

	void Init(int maxNumTriangles);
	void AddTriangle(const MeshProbe::CallbackObject* pObj);
	RayCastResult RayCast(Point_arg p, Vector_arg v) const;

	void DebugDraw() const;

	void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound);

private:
	class RenderTriangle
	{
	public:
		RenderTriangle(const MeshProbe::CallbackObject* pObj);
		U32 GetId() const { return m_hash;}
		Vector GetNormal() const { return SafeNormalize(Cross(m_pos[1] - m_pos[0], m_pos[2] - m_pos[0]), kUnitYAxis);}

		Point m_pos[3];
		U32	m_hash;
		U32 m_priority;
	};

	static float RayCastTriangle(Point_arg p, Vector_arg c, const RenderTriangle& tri);

	typedef HashTable<U32, RenderTriangle> RenderTriangleHash;

	RenderTriangleHash m_hashTable;
	U32 m_maxPriority;
};

#endif //RENDER_TRIANGLE_CACHE_H
