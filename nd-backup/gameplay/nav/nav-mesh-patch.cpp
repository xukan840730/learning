/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/nav-mesh-patch.h"

#include "corelib/containers/fixedsizeheap.h"
#include "corelib/containers/hashtable.h"
#include "corelib/math/adaptive-precision.h"
#include "corelib/math/csg/poly-clipper.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/user.h"

#include "ndlib/math/delaunay.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/net/nd-net-info.h"
#include "ndlib/netbridge/mail.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-blocker-mgr.h"
#include "gamelib/gameplay/nav/nav-blocker.h"
#include "gamelib/gameplay/nav/nav-ex-data.h"
#include "gamelib/gameplay/nav/nav-mesh-gap-ex.h"
#include "gamelib/gameplay/nav/nav-mesh-gap.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-path-node-mgr.h"
#include "gamelib/gameplay/nav/nav-poly-ex.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/util/exception-handling.h"

#ifndef FINAL_BUILD
struct DebugBlockerInput
{
	int m_mgrIndex;
	Point m_pos;
	StringId64Storage m_meshId;
	U32F m_iPoly;
	Point m_v0;
	Point m_v1;
	Point m_v2;
	Point m_v3;
};

class DebugCombination
{
public:
	bool HasNext() const
	{
		return m_end < (m_n - 1);
	}

	bool Next(int n)
	{
		if (m_preR != m_r)
		{
			NAV_ASSERT(n > 0);
			NAV_ASSERT(n <= kMaxDynamicNavBlockerCount);

			for (int i = 0; i < n; i++)
			{
				m_indices[i] = i;
			}

			m_n = n;

			for (int i = 0; i < n; i++)
			{
				m_flags[i] = i < m_r ? true : false;
			}

			m_start = 0;
			m_end = m_r - 1;

			m_preR = m_r;

			m_rCount++;

			return true;
		}
		else if (HasNext())
		{
			if (m_start == m_end)
			{
				m_flags[m_end] = false;
				m_flags[m_end + 1] = true;

				m_start++;
				m_end++;
				while (m_end + 1 < m_n &&
					m_flags[m_end + 1])
				{
					m_end++;
				}
			}
			else
			{
				if (m_start == 0)
				{
					m_flags[m_end] = false;
					m_flags[m_end + 1] = true;

					m_end--;
				}
				else
				{
					m_flags[m_end + 1] = true;

					for (int i = m_start; i <= m_end; i++)
					{
						m_flags[i] = false;
					}

					for (int i = 0; i < m_end - m_start; i++)
					{
						m_flags[i] = true;
					}

					m_end = m_end - m_start - 1;
					m_start = 0;
				}
			}

			m_rCount++;

			return true;
		}
		
		m_r = (m_r == m_n) ? 1 : (m_r + 1);
		m_rCount = 0;

		return false;
	}

public:
	int m_indices[kMaxDynamicNavBlockerCount];
	bool m_flags[kMaxDynamicNavBlockerCount];
	int m_n;
	int m_preR		= 0;
	int m_r			= 1;
	int m_start;
	int m_end;
	int m_rCount	= 0;
};

DebugCombination s_debugCombination;
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename T>
struct HashT;

/// --------------------------------------------------------------------------------------------------------------- ///
typedef bool (*TriangleMergePredicate)(const TriangleIndices& tri0,
									   const TriangleIndices& tri1,
									   I32F iN0,
									   I32F iN1,
									   void* pUserData);

/// --------------------------------------------------------------------------------------------------------------- ///
struct PolyIndices
{
	I32 m_indices[4];
	U32 m_numVerts;
	I32 m_neighbors[4];
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavBlockerClipperEntry
{
	NavBlockerClipperEntry()
	{
		m_blockers.ClearAllBits();
		m_clipPoly.m_numVerts = 0;
	}

	NavBlockerBits m_blockers;
	Clip2D::Poly m_clipPoly;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct VertKey
{
	union
	{
		struct
		{
			I32 m_iBlocker;
			U32 m_iVert;
		};
		U64 m_u64;
	};

	bool operator==(const VertKey& rhs) const { return m_u64 == rhs.m_u64; }
};

/// --------------------------------------------------------------------------------------------------------------- ///
template <>
struct HashT<VertKey>
{
	inline uintptr_t operator()(const VertKey& key) const { return static_cast<uintptr_t>(key.m_u64); }
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct VertData
{
	VertData() : m_pVert(nullptr), m_originalIndex(-1) {}

	Vec2* m_pVert;
	I32 m_originalIndex;
};

typedef HashTable<VertKey, VertData, false> VertTable;
typedef VertTable::Iterator VertTableItr;
typedef VertTable::ConstIterator ConstVertTableItr;

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugDrawClipPoly(const Clip2D::Poly& p, float vertOffset, Color clr, DebugPrimTime tt = kPrimDuration1FrameAuto)
{
	if (p.m_numVerts == 0)
		return;

	const Vector vo = Vector(0.0f, vertOffset, 0.0f);

	for (U32F i = 0; i < p.m_numVerts; ++i)
	{
		const Point p0 = p.GetVertex(i);
		const Point p1 = p.GetNextVertex(i);

		g_prim.Draw(DebugArrow(p0 + vo, p1 + vo, clr, 0.05f), tt);

		const Point strPos = p0 + vo + Vector(0.0f, float(i + 3) * 0.015f, 0.0f);

		g_prim.Draw(DebugLine(p0 + vo, strPos, clr), tt);

		StringBuilder<1024> desc;

		desc.append_format("%d", i);

		if (p.m_verts[i].m_state != Clip2D::VertexState::kNone)
		{
			desc.append_format("-%s", GetVertexStateStr(p.m_verts[i].m_state));
		}

		if (p.m_verts[i].m_onNeighborBoundary)
		{
			desc.append_format("-t");
		}

		if (p.m_verts[i].m_neighborIndex >= 0)
		{
			const I32 iOtherVert  = p.m_verts[i].m_neighborIndex & 0xFFFF;
			const I32 iOtherEntry = ((p.m_verts[i].m_neighborIndex >> 16UL) & 0xFFFF) - 1;

			if (iOtherEntry >= 0)
			{
				desc.append_format(" (%d %d)", iOtherEntry, iOtherVert);
			}
			else
			{
				desc.append_format(" (%d)", p.m_verts[i].m_neighborIndex);
			}
		}

		g_prim.Draw(DebugString(strPos, desc.c_str(), clr, 0.75f), tt);

		if (p.m_verts[i].m_originalEdge >= 0)
		{
			g_prim.Draw(DebugString(Lerp(p0 + vo, p1 + vo, 0.5f),
									StringBuilder<32>("%d", p.m_verts[i].m_originalEdge).c_str(),
									clr,
									0.5f),
						tt);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Clip2D::Poly WorldToLocal(const NavMesh* pMesh, const Clip2D::Poly& polyWs)
{
	Clip2D::Poly polyLs = polyWs;

	for (U32F i = 0; i < polyWs.m_numVerts; ++i)
	{
		polyLs.m_verts[i].m_pos = pMesh->WorldToLocal(polyWs.m_verts[i].m_pos);
	}

	return polyLs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Clip2D::Poly LocalToWorld(const NavMesh* pMesh, const Clip2D::Poly& polyLs)
{
	Clip2D::Poly polyWs = polyLs;

	for (U32F i = 0; i < polyWs.m_numVerts; ++i)
	{
		polyWs.m_verts[i].m_pos = pMesh->LocalToWorld(polyLs.m_verts[i].m_pos);
	}

	return polyWs;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsPolyInList(const NavPoly* pPoly, const NavPoly** apPolyList, size_t listSize)
{
	for (U32F i = 0; i < listSize; ++i)
	{
		if (pPoly == apPolyList[i])
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsPolyMeshInList(const NavPoly* pPoly, const NavPoly** apPolyList, size_t listSize)
{
	const NavMeshHandle hPolyMesh = pPoly->GetNavMeshHandle();

	for (U32F i = 0; i < listSize; ++i)
	{
		if (hPolyMesh == apPolyList[i]->GetNavMeshHandle())
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static U32F GetIntersectingPolysForNavBlocker(const DynamicNavBlocker* pNavBlocker,
											  float blockerRadius,
											  NavPolyList* pResultsOut)
{
	PROFILE_AUTO(Navigation);

	if (!pNavBlocker || !pResultsOut)
		return 0;

	const NavPoly* pBlockerPoly = pNavBlocker->GetNavPoly();

	if (!pBlockerPoly)
		return 0;

	pResultsOut->m_numPolys = 0;

	const NavMesh* pBlockerMesh = pBlockerPoly->GetNavMesh();
	const Point blockerPosWs	= pBlockerMesh->ParentToWorld(pNavBlocker->GetPosPs());

	const float searchRadius = (blockerRadius + kMaxNavMeshGapDiameter) * 1.1f;

	static const size_t kMaxOpenListSize = 16;
	const NavPoly* startPolyOpenList[kMaxOpenListSize];
	float polyDistOpenList[kMaxOpenListSize];
	const NavPoly* startPolyClosedList[kMaxOpenListSize];
	U32F openListSize	= 0;
	U32F closedListSize = 0;

	startPolyOpenList[openListSize] = pBlockerPoly;
	polyDistOpenList[openListSize]	= 0.0f;
	++openListSize;

	while (openListSize > 0)
	{
		const NavPoly* pStartPoly = startPolyOpenList[openListSize - 1];
		const float startPolyDist = polyDistOpenList[openListSize - 1];
		NAV_ASSERT(openListSize > 0);
		openListSize--;

		NAV_ASSERT(closedListSize < kMaxOpenListSize);
		startPolyClosedList[closedListSize++] = pStartPoly;

		const NavMesh* pPolyMesh   = pStartPoly->GetNavMesh();
		const U32F polyCount	   = pPolyMesh->GetPolyCount();
		const Point searchOriginLs = pPolyMesh->WorldToLocal(blockerPosWs);

		{
			const NavPoly* pLinkedPoly = pStartPoly->IsLink() ? pStartPoly->GetLinkedPoly() : nullptr;
			if (pLinkedPoly)
			{
				if (!IsPolyMeshInList(pLinkedPoly, startPolyClosedList, closedListSize)
					&& !IsPolyMeshInList(pLinkedPoly, startPolyOpenList, openListSize))
				{
					NAV_ASSERT(openListSize < kMaxOpenListSize);
					startPolyOpenList[openListSize] = pLinkedPoly;
					polyDistOpenList[openListSize]	= startPolyDist;
					++openListSize;
				}
			}
			else if (pResultsOut->m_numPolys < NavPolyList::kMaxPolys)
			{
				NAV_ASSERT(!IsPolyInList(pStartPoly, pResultsOut->m_pPolys, pResultsOut->m_numPolys));
				NAV_ASSERT(pResultsOut->m_numPolys < NavPolyList::kMaxPolys);
				pResultsOut->m_pPolys[pResultsOut->m_numPolys]	  = pStartPoly;
				pResultsOut->m_distances[pResultsOut->m_numPolys] = startPolyDist;
				pResultsOut->m_numPolys++;
			}
			else
			{
				ASSERT(false);
				break;
			}
		}

		if (const NavPolyDistEntry* pDistList = pStartPoly->GetPolyDistList())
		{
			for (U32F i = 0; pDistList[i].GetDist() < searchRadius; ++i)
			{
				const U32F iPoly = pDistList[i].GetPolyIndex();
				if (iPoly >= polyCount)
					break;

				const NavPoly& poly = pPolyMesh->GetPoly(iPoly);

				if (!poly.IsValid())
					continue;

				Point closestPt = searchOriginLs;
				const float d	= poly.FindNearestPointXzLs(&closestPt, searchOriginLs);

				if (Abs(closestPt.Y() - searchOriginLs.Y()) > 2.0f)
					continue;

				if (d > searchRadius)
					continue;

				if (pResultsOut->m_numPolys < NavPolyList::kMaxPolys)
				{
					const NavPoly* pLinkedPoly = poly.IsLink() ? poly.GetLinkedPoly() : nullptr;
					if (pLinkedPoly)
					{
						if (!IsPolyMeshInList(pLinkedPoly, startPolyClosedList, closedListSize)
							&& !IsPolyMeshInList(pLinkedPoly, startPolyOpenList, openListSize))
						{
							NAV_ASSERT(openListSize < kMaxOpenListSize);
							startPolyOpenList[openListSize] = pLinkedPoly;
							polyDistOpenList[openListSize]	= d;
							++openListSize;
						}
					}
					else
					{
						NAV_ASSERT(!IsPolyInList(&poly, pResultsOut->m_pPolys, pResultsOut->m_numPolys));
						NAV_ASSERT(pResultsOut->m_numPolys < NavPolyList::kMaxPolys);
						pResultsOut->m_pPolys[pResultsOut->m_numPolys]	  = &poly;
						pResultsOut->m_distances[pResultsOut->m_numPolys] = d;
						pResultsOut->m_numPolys++;
					}
				}
				else
				{
					ASSERT(false);
					break;
				}
			}
		}
	}

	return pResultsOut->m_numPolys;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Clip2D::Poly MakeClipperPolyFromNavPolyLs(const NavPoly* pPoly)
{
	Clip2D::Poly p;

	p.m_numVerts = 0;

	if (nullptr == pPoly)
		return p;

	for (U32F iV = 0; iV < pPoly->GetVertexCount(); ++iV)
	{
		p.PushVertex(pPoly->GetVertex(iV), iV);
	}

	return p;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Clip2D::Poly MakeClipperPolyFromNavBlockerWs(const DynamicNavBlocker* pNavBlocker)
{
	Clip2D::Poly p;

	p.m_numVerts = 0;

	if (nullptr == pNavBlocker)
		return p;

	const NavMesh* pBlockerMesh = pNavBlocker->GetNavMesh();
	if (nullptr == pBlockerMesh)
		return p;

	const Point posPs = pNavBlocker->GetPosPs();

	for (U32F iV = 0; iV < 4; ++iV)
	{
		const Point vertPosPs = posPs + (pNavBlocker->GetQuadVertex(iV) - kOrigin);
		const Point vertPosWs = pBlockerMesh->ParentToWorld(vertPosPs);

		p.PushVertex(vertPosWs, 100 + iV);
	}

	p.FixupWinding();

	return p;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool GetSharedEdge(U32F iTri0, U32F iTri1, const TriangleIndices* pTriangles, I32F* pN0Out, I32F* pN1Out)
{
	if (!pN0Out || !pN1Out)
		return false;

	const TriangleIndices& tri0 = pTriangles[iTri0];
	const TriangleIndices& tri1 = pTriangles[iTri1];

	I32F iN0 = -1;
	I32F iN1 = -1;

	for (U32F iN = 0; iN < 3; ++iN)
	{
		if (tri0.m_neighborTris[iN] == iTri1)
		{
			NAV_ASSERT((iN0 < 0) || (iN0 == iN));
			iN0 = iN;
		}
		if (tri1.m_neighborTris[iN] == iTri0)
		{
			NAV_ASSERT((iN1 < 0) || (iN1 == iN));
			iN1 = iN;
		}
	}

	if ((iN0 < 0) || (iN1 < 0))
		return false;

	*pN0Out = iN0;
	*pN1Out = iN1;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool CanMergeTriangles(U32F iTri0,
							  U32F iTri1,
							  I32F iN0,
							  I32F iN1,
							  const TriangleIndices* pTriangles,
							  U32F numTriangles,
							  const Vec2* pVerts,
							  const U32* pSegments,
							  const I32* piOrgBoundaries,
							  U32 segmentCount)
{
	if ((iN0 < 0) || (iN1 < 0))
		return false;

	const TriangleIndices& tri0 = pTriangles[iTri0];
	const TriangleIndices& tri1 = pTriangles[iTri1];

	Vec2 quad[4];

	I32F indices[4];
	indices[0] = tri0.m_indices[iN0];
	indices[1] = tri1.m_indices[(iN1 + 2) % 3];
	indices[2] = tri1.m_indices[iN1];
	indices[3] = tri0.m_indices[(iN0 + 2) % 3];

	for (U32F i = 0; i < 4; ++i)
	{
		const I32F iVert   = indices[i];
		const I32F iVertN  = indices[(i + 1) % 4];
		const I32F iVertNN = indices[(i + 2) % 4];

		for (U32F iSeg = 0; iSeg < segmentCount; ++iSeg)
		{
			if (piOrgBoundaries[iSeg] < 0)
				continue;

			const U32F segVert0 = pSegments[(iSeg * 2) + 0];
			const U32F segVert1 = pSegments[(iSeg * 2) + 1];

			const bool segMatches = ((iVert == segVert0) && (iVertN == segVert1))
									|| ((iVert == segVert1) && (iVertN == segVert0));

			if (!segMatches)
			{
				continue;
			}

			for (U32F iOtherSeg = 0; iOtherSeg < segmentCount; ++iOtherSeg)
			{
				if (iOtherSeg == iSeg)
					continue;

				if (piOrgBoundaries[iOtherSeg] != piOrgBoundaries[iSeg])
					continue;

				const U32F segVertN0 = pSegments[(iOtherSeg * 2) + 0];
				const U32F segVertN1 = pSegments[(iOtherSeg * 2) + 1];

				const bool otherSegMatches = ((iVertN == segVertN0) && (iVertNN == segVertN1))
											 || ((iVertN == segVertN1) && (iVertNN == segVertN0));

				if (!otherSegMatches)
				{
					continue;
				}

				return false;
			}
		}
	}

	quad[0] = pVerts[indices[0]];
	quad[1] = pVerts[indices[1]];
	quad[2] = pVerts[indices[2]];
	quad[3] = pVerts[indices[3]];

	bool canMerge = true;

	for (U32F i = 0; i < 4; ++i)
	{
		float twiceArea = 0.0f;
		const TriangleWinding tw = ComputeTriangleWinding(quad[i], quad[(i + 1) % 4], quad[(i + 2) % 4], &twiceArea);

		if (tw != kWindingCW)
		{
			canMerge = false;
			break;
		}

		if (Abs(twiceArea) < kSmallFloat)
		{
			canMerge = false;
			break;
		}
	}

	return canMerge;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool MergeTriangles(U32F iTri0,
						   U32F iTri1,
						   const TriangleIndices* pTriangles,
						   U32F numTriangles,
						   PolyIndices* pPolyOut)
{
	const TriangleIndices& tri0 = pTriangles[iTri0];
	const TriangleIndices& tri1 = pTriangles[iTri1];

	I32F iN0 = -1;
	I32F iN1 = -1;

	for (U32F iN = 0; iN < 3; ++iN)
	{
		if (tri0.m_neighborTris[iN] == iTri1)
		{
			NAV_ASSERT(iN0 < 0);
			iN0 = iN;
		}
		if (tri1.m_neighborTris[iN] == iTri0)
		{
			NAV_ASSERT(iN1 < 0);
			iN1 = iN;
		}
	}

	NAV_ASSERT(iN0 >= 0);
	NAV_ASSERT(iN1 >= 0);

	if ((iN0 < 0) || (iN1 < 0))
		return false;

	PolyIndices ret;
	ret.m_indices[0] = tri0.m_indices[iN0];
	ret.m_indices[1] = tri1.m_indices[(iN1 + 2) % 3];
	ret.m_indices[2] = tri1.m_indices[iN1];
	ret.m_indices[3] = tri0.m_indices[(iN0 + 2) % 3];

	ret.m_neighbors[0] = tri1.m_neighborTris[(iN1 + 1) % 3];
	ret.m_neighbors[1] = tri1.m_neighborTris[(iN1 + 2) % 3];
	ret.m_neighbors[2] = tri0.m_neighborTris[(iN0 + 1) % 3];
	ret.m_neighbors[3] = tri0.m_neighborTris[(iN0 + 2) % 3];

	ret.m_numVerts = 4;

	*pPolyOut = ret;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
typedef HashTable<U32F, U32F> PrimitiveMapTable;

/// --------------------------------------------------------------------------------------------------------------- ///
typedef HashTable<U32F, U32F> VertMapTable;

/// --------------------------------------------------------------------------------------------------------------- ///
class IndexMapTable		// Replacement for the use of hash table.
{
public:
	static const U32F kInvalidIndex = UINT_MAX;

	void Init(const U32F size)
	{
		m_size = size;

		if (m_size > 0)
		{
			m_buffer = NDI_NEW U32F[m_size];
		}
		else
		{
			m_buffer = nullptr;
		}
	}

	void Clear()
	{
		for (U32F i = 0; i < m_size; i++)
		{
			m_buffer[i] = kInvalidIndex;
		}
	}

	void Add(U32F fromIndex, U32F toIndex)
	{
		NAV_ASSERT(fromIndex < m_size);

		m_buffer[fromIndex] = toIndex;
	}

	U32F Find(U32F fromIndex) const
	{
		NAV_ASSERT(fromIndex < m_size);

		return m_buffer[fromIndex];
	}

	U32F Size() const
	{
		return m_size;
	}

private:
	U32F* m_buffer;
	U32F m_size;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class FeatureKey
{
public:
	union
	{
		struct
		{
			U32 m_subKey0;
			U32 m_subKey1;
		};
		U64 m_u64;
	};

public:
	FeatureKey() {}

	FeatureKey(U32 subKey0, U32 subKey1) : m_subKey0(subKey0), m_subKey1(subKey1) {}

	bool operator==(const FeatureKey& rhs) const { return m_u64 == rhs.m_u64; }
};

/// --------------------------------------------------------------------------------------------------------------- ///
template <>
struct HashT<FeatureKey>
{
	inline uintptr_t operator()(const FeatureKey& key) const { return static_cast<uintptr_t>(key.m_u64); }
};

/// --------------------------------------------------------------------------------------------------------------- ///
typedef HashTable<FeatureKey, U32F> SegmentMapTable;

#ifndef FINAL_BUILD
static void OnNavMeshPatchFailure(const NavMesh* const pNavMeshMother,
								  const char* const sourceFile,
								  const U32F sourceLine,
								  const char* const sourceFunc)
{
	MsgOut("\n\n=============================[ Nav Mesh Patch Failure ]=============================\n");
	MsgOut("NavMesh: '%s' [level: '%s']\n",
					   pNavMeshMother->GetName(),
					   DevKitOnly_StringIdToString(pNavMeshMother->GetLevelId()));

	MsgOut("\n======================================================================================\n");

	MailNavMeshReportTo("ryan_huai@naughtydog.com", "Nav Mesh Patch Failure");

	NAV_ASSERTF(false, ("[ Nav Mesh Patch Failure ]: %s, %d, %s", sourceFile, sourceLine, sourceFunc));
}
#endif

static void NavMeshPatchAssert(const bool assertOn,
							   const NavMesh* const pNavMeshMother,
							   const char* const sourceFile,
							   const U32F sourceLine,
							   const char* const sourceFunc)
{
#ifndef FINAL_BUILD
	if (!assertOn)
	{
		OnNavMeshPatchFailure(pNavMeshMother, sourceFile, sourceLine, sourceFunc);
	}
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
static const float kNavPolyVertMergeEpsilon	   = 0.0001f;
static const float kNavPolyVertMergeEpsilonSqr = kNavPolyVertMergeEpsilon * kNavPolyVertMergeEpsilon;
static const float kIntersectionSnapEpsilon	   = 0.0001f;
static const float kIntersectionSnapEpsilonSqr = kIntersectionSnapEpsilon * kIntersectionSnapEpsilon;
static const float kQuadTriangleAreaEpsilon	   = 0.000002f; // kSmallFloat causes jittering
static const U32F kMaxNumIntersections		   = 512;		// Should be enough
static const U32F kMaxNumNavPolyBatches = 5;	// Load balance
static const U32F kNavPolyBatchSize = 10;

/// --------------------------------------------------------------------------------------------------------------- ///
static U32F TryMergeTriangles2(const NavMesh* const pNavMeshMother,
							   const TriangleIndices* const pTriangles,
							   const U32F numTriangles,
							   bool* const pValidTris,
							   const Vec2* const pVerts,
							   const IndexMapTable& vertPackedToGlobal,
							   const ExternalBitArray* const pGVO,
							   ExternalBitArray& dummyOwners0,
							   ExternalBitArray& dummyOwners1,
							   ExternalBitArray& dummyOwners2,
							   PolyIndices* const pPolysOut,
							   const U32F maxPolysOut)
{
	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	ExternalBitArray canMerge;
	const U32F numCanMergeBits	 = numTriangles * 3;
	const U32F numCanMergeBlocks = ExternalBitArray::DetermineNumBlocks(numCanMergeBits);
	U64* const pCanMergeBlocks	 = NDI_NEW U64[numCanMergeBlocks];

	canMerge.Init(numCanMergeBits, pCanMergeBlocks);
	canMerge.ClearAllBits();

	// Check for possible merges with neighbors
	for (U32F triIdx0 = 0; triIdx0 < numTriangles; triIdx0++)
	{
		if (!pValidTris[triIdx0]) // Degenerated
		{
			continue;
		}

		const TriangleIndices& tri0 = pTriangles[triIdx0];

		// For each neighbor
		for (U32F iNeighbor = 0; iNeighbor < 3; iNeighbor++)
		{
			const I32F triIdx1 = tri0.m_neighborTris[iNeighbor];

			if (triIdx1 < 0 || triIdx1 >= numTriangles) // Has no neighbor
			{
				continue;
			}

			if (!pValidTris[triIdx1]) // Neighbor degenerated
			{
				continue;
			}

			if (triIdx0 <= triIdx1) // One directional merge
			{
				continue;
			}

			const TriangleIndices& tri1 = pTriangles[triIdx1];

			const I32 triPackedIdx00 = tri0.m_indices[0];
			const I32 triPackedIdx01 = tri0.m_indices[1];
			const I32 triPackedIdx02 = tri0.m_indices[2];

			const U32F tri0GlobalIndices[3] = { vertPackedToGlobal.Find(triPackedIdx00),
												vertPackedToGlobal.Find(triPackedIdx01),
												vertPackedToGlobal.Find(triPackedIdx02) };

			NAV_ASSERT(tri0GlobalIndices[0] != IndexMapTable::kInvalidIndex);
			NAV_ASSERT(tri0GlobalIndices[1] != IndexMapTable::kInvalidIndex);
			NAV_ASSERT(tri0GlobalIndices[2] != IndexMapTable::kInvalidIndex);

			const I32 triPackedIdx10 = tri1.m_indices[0];
			const I32 triPackedIdx11 = tri1.m_indices[1];
			const I32 triPackedIdx12 = tri1.m_indices[2];

			const U32F tri1GlobalIndices[3] = { vertPackedToGlobal.Find(triPackedIdx10),
												vertPackedToGlobal.Find(triPackedIdx11),
												vertPackedToGlobal.Find(triPackedIdx12) };

			NAV_ASSERT(tri1GlobalIndices[0] != IndexMapTable::kInvalidIndex);
			NAV_ASSERT(tri1GlobalIndices[1] != IndexMapTable::kInvalidIndex);
			NAV_ASSERT(tri1GlobalIndices[2] != IndexMapTable::kInvalidIndex);

			// Don't allow blockers to merge with non-blockers
			ExternalBitArray::BitwiseAnd(&dummyOwners0, pGVO[tri0GlobalIndices[0]], pGVO[tri0GlobalIndices[1]]);
			ExternalBitArray::BitwiseAnd(&dummyOwners0, dummyOwners0, pGVO[tri0GlobalIndices[2]]);

			ExternalBitArray::BitwiseAnd(&dummyOwners1, pGVO[tri1GlobalIndices[0]], pGVO[tri1GlobalIndices[1]]);
			ExternalBitArray::BitwiseAnd(&dummyOwners1, dummyOwners1, pGVO[tri1GlobalIndices[2]]);

			ExternalBitArray::BitwiseXor(&dummyOwners2, dummyOwners0, dummyOwners1);
			if (!dummyOwners2.AreAllBitsClear())
			{
				continue;
			}

			I32F iN0 = -1;
			I32F iN1 = -1;
			GetSharedEdge(triIdx0, triIdx1, pTriangles, &iN0, &iN1);
			if (iN0 < 0 || iN0 > 3 || iN1 < 0 || iN1 > 3)
			{
#ifndef FINAL_BUILD
				OnNavMeshPatchFailure(pNavMeshMother, FILE_LINE_FUNC);
#endif
				continue;
			}

			// Check quad
			I32F indices[4];
			Vec2 quad[4];

			indices[0] = tri0.m_indices[iN0];
			indices[1] = tri1.m_indices[(iN1 + 2) % 3];
			indices[2] = tri1.m_indices[iN1];
			indices[3] = tri0.m_indices[(iN0 + 2) % 3];

			quad[0] = pVerts[indices[0]];
			quad[1] = pVerts[indices[1]];
			quad[2] = pVerts[indices[2]];
			quad[3] = pVerts[indices[3]];

			bool quadValid = true;
			for (U32F i = 0; i < 4; ++i)
			{
				float twiceArea = 0.0f;
				const TriangleWinding tw = ComputeTriangleWinding(quad[i],
																  quad[(i + 1) % 4],
																  quad[(i + 2) % 4],
																  &twiceArea);

				if (Abs(twiceArea) < kQuadTriangleAreaEpsilon)
				{
					quadValid = false;
					break;
				}

				if (tw != kWindingCW)
				{
					quadValid = false;
					break;
				}
			}

			if (!quadValid)
			{
				continue;
			}

			canMerge.SetBit(triIdx0 * 3 + iNeighbor);

			for (U32F iOtherNeigh = 0; iOtherNeigh < 3; ++iOtherNeigh)
			{
				if (tri1.m_neighborTris[iOtherNeigh] == triIdx0)
				{
					canMerge.SetBit((triIdx1 * 3) + iOtherNeigh);
				}
			}
		}
	}

	ExternalBitArray merged;
	const U32F numMergedBlocks = ExternalBitArray::DetermineNumBlocks(numTriangles);
	U64* const pMergedBlocks   = NDI_NEW U64[numMergedBlocks];

	merged.Init(numTriangles, pMergedBlocks);
	merged.ClearAllBits();

	I32* const pTriPolyMapping = NDI_NEW I32[numTriangles];

	// memset(pTriPolyMapping, -1, sizeof(I32) * numTriangles);
	for (U32F i = 0; i < numTriangles; ++i)
	{
		pTriPolyMapping[i] = -1;
	}

	U32F numPolysOut = 0;
	U32F numMerges	 = 0;

	// on the first iteration of this loop, we only merge triangles that only have 1 option for
	// potential merges.  We continue to make passes over all the triangles until no merges are
	// possible, then we raise the merge-count-threshold and try again, until no more merges are
	// possible.
	for (U32F iMergeCountPass = 1; iMergeCountPass < 4; ++iMergeCountPass)
	{
		while (true)
		{
			U32F mergeCountThisPass = 0;

			for (U32F iTri = 0; iTri < numTriangles; ++iTri)
			{
				if (numPolysOut >= maxPolysOut)
					break;

				if (merged.IsBitSet(iTri))
					continue;

				U32F potentialMergeCount = 0;
				I32F iMerge = -1;

				// for each neighbor
				for (int iNeigh = 0; iNeigh < 3; ++iNeigh)
				{
					const I32F neighborIndex = pTriangles[iTri].m_neighborTris[iNeigh];
					if ((neighborIndex < 0) || (neighborIndex >= numTriangles))
						continue;

					if (merged.IsBitSet(neighborIndex))
						continue;

					if (canMerge.IsBitSet((iTri * 3) + iNeigh))
					{
						++potentialMergeCount;
						iMerge = iNeigh;
					}
				}

				if (potentialMergeCount > 0 && potentialMergeCount <= iMergeCountPass)
				{
					const I32F neighborIndex = pTriangles[iTri].m_neighborTris[iMerge];

					ASSERT(neighborIndex >= 0 && neighborIndex < numTriangles);

					if (neighborIndex < 0 || neighborIndex >= numTriangles)
						continue;

					if (!MergeTriangles(iTri, neighborIndex, pTriangles, numTriangles, &pPolysOut[numPolysOut]))
						continue;

					pTriPolyMapping[iTri] = numPolysOut;
					pTriPolyMapping[neighborIndex] = numPolysOut;
					++numPolysOut;

					merged.SetBit(iTri);
					merged.SetBit(neighborIndex);
					++mergeCountThisPass;
					++numMerges;
				}
			}

			if (mergeCountThisPass == 0)
			{
				break;
			}
		}
	}

	for (U64 iNonMerged = merged.FindFirstClearBit(); iNonMerged < numTriangles;
		 iNonMerged		= merged.FindNextClearBit(iNonMerged))
	{
		if (numPolysOut >= maxPolysOut)
			break;

		PolyIndices& poly = pPolysOut[numPolysOut];
		const TriangleIndices& srcTri = pTriangles[iNonMerged];

		if (!pValidTris[iNonMerged])
			continue;

		poly.m_indices[0]	= srcTri.m_indices[0];
		poly.m_indices[1]	= srcTri.m_indices[1];
		poly.m_indices[2]	= srcTri.m_indices[2];
		poly.m_indices[3]	= srcTri.m_indices[2];
		poly.m_neighbors[0] = srcTri.m_neighborTris[0];
		poly.m_neighbors[1] = srcTri.m_neighborTris[1];
		poly.m_neighbors[2] = srcTri.m_neighborTris[2];
		poly.m_neighbors[3] = srcTri.m_neighborTris[2];
		poly.m_numVerts		= 3;

		ASSERT(pTriPolyMapping[iNonMerged] < 0);
		pTriPolyMapping[iNonMerged] = numPolysOut;

		++numPolysOut;
	}

	for (U64 iPolyOut = 0; iPolyOut < numPolysOut; ++iPolyOut)
	{
		PolyIndices& poly = pPolysOut[iPolyOut];

		for (U32F iN = 0; iN < poly.m_numVerts; ++iN)
		{
			const I32F triangleIndex = poly.m_neighbors[iN];
			const I32F polyIndex	 = (triangleIndex >= 0) ? pTriPolyMapping[triangleIndex] : -1;
			poly.m_neighbors[iN]	 = polyIndex;
		}
	}

	// NAV_ASSERT(numPolysOut + numMerges == numTriangles);

	return numPolysOut;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static U32F TryMergeTriangles(const TriangleIndices* pTriangles,
							  U32F numTriangles,
							  bool* pValidTris,
							  const Vec2* pPoints,
							  const U32* pSegments,
							  const I32* piOrgBoundaries,
							  U32 segmentCount,
							  PolyIndices* pPolysOut,
							  U32F maxPolysOut,
							  TriangleMergePredicate pFnMergePredicate = nullptr,
							  void* pMergeTestUserData = nullptr)
{
	if (!pTriangles || (0 == numTriangles) || !pPolysOut || (0 == maxPolysOut))
		return 0;

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	ExternalBitArray canMerge;
	const U32F numCanMergeBits	 = numTriangles * 3;
	const U32F numCanMergeBlocks = ExternalBitArray::DetermineNumBlocks(numCanMergeBits);
	U64* pCanMergeBlocks		 = NDI_NEW U64[numCanMergeBlocks];

	canMerge.Init(numCanMergeBits, pCanMergeBlocks);
	canMerge.ClearAllBits();

	// check for possible merges with neighbors
	for (U32F iTri = 0; iTri < numTriangles; ++iTri)
	{
		if (!pValidTris[iTri])
			continue;

		const TriangleIndices& tri = pTriangles[iTri];

		// for each neighbor
		for (U32F iNeigh = 0; iNeigh < 3; ++iNeigh)
		{
			const I32F neighborIndex = tri.m_neighborTris[iNeigh];

			if ((neighborIndex < 0) || (neighborIndex >= numTriangles))
				continue;

			// if iOtherTri is valid and less than iTri,
			if (neighborIndex >= iTri)
				continue;

			if (!pValidTris[neighborIndex])
				continue;

			I32F iN0 = -1;
			I32F iN1 = -1;
			if (!GetSharedEdge(iTri, neighborIndex, pTriangles, &iN0, &iN1))
				continue;

			// don't allow blockers to merge with non-blockers
			if (pFnMergePredicate && !pFnMergePredicate(tri, pTriangles[neighborIndex], iN0, iN1, pMergeTestUserData))
				continue;

			const TriangleIndices& neighborTri = pTriangles[neighborIndex];
			if (!CanMergeTriangles(iTri,
								   neighborIndex,
								   iN0,
								   iN1,
								   pTriangles,
								   numTriangles,
								   pPoints,
								   pSegments,
								   piOrgBoundaries,
								   segmentCount))
			{
				continue;
			}

			canMerge.SetBit((iTri * 3) + iNeigh);

			for (U32F iOtherNeigh = 0; iOtherNeigh < 3; ++iOtherNeigh)
			{
				if (neighborTri.m_neighborTris[iOtherNeigh] == iTri)
				{
					canMerge.SetBit((neighborIndex * 3) + iOtherNeigh);
				}
			}
		}
	}

	ExternalBitArray merged;

	const U32F numMergedBlocks = ExternalBitArray::DetermineNumBlocks(numTriangles);
	U64* pMergedBlocks		   = NDI_NEW U64[numMergedBlocks];

	merged.Init(numTriangles, pMergedBlocks);
	merged.ClearAllBits();

	I32* pTriPolyMapping = NDI_NEW I32[numTriangles];

	// memset(pTriPolyMapping, -1, sizeof(I32) * numTriangles);
	for (U32F i = 0; i < numTriangles; ++i)
	{
		pTriPolyMapping[i] = -1;
	}

	U32F numPolysOut = 0;
	U32F numMerges	 = 0;

	// on the first iteration of this loop, we only merge triangles that only have 1 option for
	// potential merges.  We continue to make passes over all the triangles until no merges are
	// possible, then we raise the merge-count-threshold and try again, until no more merges are
	// possible.
	for (U32F iMergeCountPass = 1; iMergeCountPass < 4; ++iMergeCountPass)
	{
		while (true)
		{
			U32F mergeCountThisPass = 0;

			for (U32F iTri = 0; iTri < numTriangles; ++iTri)
			{
				if (numPolysOut >= maxPolysOut)
					break;

				if (merged.IsBitSet(iTri))
					continue;

				U32F potentialMergeCount = 0;
				I32F iMerge = -1;

				// for each neighbor
				for (int iNeigh = 0; iNeigh < 3; ++iNeigh)
				{
					const I32F neighborIndex = pTriangles[iTri].m_neighborTris[iNeigh];
					if ((neighborIndex < 0) || (neighborIndex >= numTriangles))
						continue;

					if (merged.IsBitSet(neighborIndex))
						continue;

					if (canMerge.IsBitSet((iTri * 3) + iNeigh))
					{
						++potentialMergeCount;
						iMerge = iNeigh;
					}
				}

				if (potentialMergeCount > 0 && potentialMergeCount <= iMergeCountPass)
				{
					const I32F neighborIndex = pTriangles[iTri].m_neighborTris[iMerge];

					ASSERT(neighborIndex >= 0 && neighborIndex < numTriangles);

					if (neighborIndex < 0 || neighborIndex >= numTriangles)
						continue;

					if (!MergeTriangles(iTri, neighborIndex, pTriangles, numTriangles, &pPolysOut[numPolysOut]))
						continue;

					pTriPolyMapping[iTri] = numPolysOut;
					pTriPolyMapping[neighborIndex] = numPolysOut;
					++numPolysOut;

					merged.SetBit(iTri);
					merged.SetBit(neighborIndex);
					++mergeCountThisPass;
					++numMerges;
				}
			}

			if (mergeCountThisPass == 0)
			{
				break;
			}
		}
	}

	for (U64 iNonMerged = merged.FindFirstClearBit(); iNonMerged < numTriangles;
		 iNonMerged		= merged.FindNextClearBit(iNonMerged))
	{
		if (numPolysOut >= maxPolysOut)
			break;

		PolyIndices& poly = pPolysOut[numPolysOut];
		const TriangleIndices& srcTri = pTriangles[iNonMerged];

		if (!pValidTris[iNonMerged])
			continue;

		poly.m_indices[0]	= srcTri.m_indices[0];
		poly.m_indices[1]	= srcTri.m_indices[1];
		poly.m_indices[2]	= srcTri.m_indices[2];
		poly.m_indices[3]	= srcTri.m_indices[2];
		poly.m_neighbors[0] = srcTri.m_neighborTris[0];
		poly.m_neighbors[1] = srcTri.m_neighborTris[1];
		poly.m_neighbors[2] = srcTri.m_neighborTris[2];
		poly.m_neighbors[3] = srcTri.m_neighborTris[2];
		poly.m_numVerts		= 3;

		ASSERT(pTriPolyMapping[iNonMerged] < 0);
		pTriPolyMapping[iNonMerged] = numPolysOut;

		++numPolysOut;
	}

	for (U64 iPolyOut = 0; iPolyOut < numPolysOut; ++iPolyOut)
	{
		PolyIndices& poly = pPolysOut[iPolyOut];

		for (U32F iN = 0; iN < poly.m_numVerts; ++iN)
		{
			const I32F triangleIndex = poly.m_neighbors[iN];
			const I32F polyIndex	 = (triangleIndex >= 0) ? pTriPolyMapping[triangleIndex] : -1;
			poly.m_neighbors[iN]	 = polyIndex;
		}
	}

	// NAV_ASSERT(numPolysOut + numMerges == numTriangles);

	return numPolysOut;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static U32F EliminateInvalidTriangles(const NavMesh* const pNavMeshMother,
									  const U32F numValidVerts,
									  TriangleIndices* const pTriangles,
									  const U32F numTriangles,
									  const IndexMapTable& vertPackedToGlobal,
									  const U32F vIntersectionGlobalIdx,
									  const U32F numUniqueNavPolySegs,
									  const U32F invalidOrgBoundaryKey,
									  const I16* const pOrgBoundaryTable,
									  bool* pValidTrisOut)
{
	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	const U32F numOrgBoundaryBits = numUniqueNavPolySegs;
	const U32F numOwnerBlocks	  = ExternalBitArray::DetermineNumBlocks(numOrgBoundaryBits);

	U64* const pOrgBoundaryBlocks = NDI_NEW U64[numOwnerBlocks * (numValidVerts + 1)];
	ExternalBitArray* const pOrgBoundary = NDI_NEW ExternalBitArray[(numValidVerts + 1)];
	for (U32F vPackedIdx = 0; vPackedIdx <= numValidVerts; vPackedIdx++)
	{
		ExternalBitArray& orgBoundary = pOrgBoundary[vPackedIdx];

		const U32F blockIdx = vPackedIdx * numOwnerBlocks;
		orgBoundary.Init(numOrgBoundaryBits, &pOrgBoundaryBlocks[blockIdx]);
		orgBoundary.ClearAllBits();
	}

	// Fill in pOrgBoundaryBlocks
	for (U32F iTri = 0; iTri < numTriangles; iTri++)
	{
		const TriangleIndices& tri = pTriangles[iTri];

		const I32 vPackedIdx0 = tri.m_indices[0];
		const I32 vPackedIdx1 = tri.m_indices[1];
		const I32 vPackedIdx2 = tri.m_indices[2];

		NavMeshPatchAssert(vPackedIdx0 != vPackedIdx1 && vPackedIdx1 != vPackedIdx2 && vPackedIdx2 != vPackedIdx0,
						pNavMeshMother,
						FILE_LINE_FUNC);

		NavMeshPatchAssert(vPackedIdx0 < vertPackedToGlobal.Size() && vPackedIdx1 < vertPackedToGlobal.Size() && vPackedIdx2 < vertPackedToGlobal.Size(),
						pNavMeshMother,
						FILE_LINE_FUNC);

		const U32F vGlobalIdx0 = vertPackedToGlobal.Find(vPackedIdx0);
		const U32F vGlobalIdx1 = vertPackedToGlobal.Find(vPackedIdx1);
		const U32F vGlobalIdx2 = vertPackedToGlobal.Find(vPackedIdx2);

		NAV_ASSERT(vGlobalIdx0 != IndexMapTable::kInvalidIndex);
		NAV_ASSERT(vGlobalIdx1 != IndexMapTable::kInvalidIndex);
		NAV_ASSERT(vGlobalIdx2 != IndexMapTable::kInvalidIndex);

		const U32F tableIdx01 = vGlobalIdx0 * vIntersectionGlobalIdx + vGlobalIdx1;
		const U32F tableIdx12 = vGlobalIdx1 * vIntersectionGlobalIdx + vGlobalIdx2;
		const U32F tableIdx20 = vGlobalIdx2 * vIntersectionGlobalIdx + vGlobalIdx0;

		const I16 orgBoundary01 = pOrgBoundaryTable[tableIdx01];
		const I16 orgBoundary12 = pOrgBoundaryTable[tableIdx12];
		const I16 orgBoundary20 = pOrgBoundaryTable[tableIdx20];

		const bool isOrgBoundaryValid01 = orgBoundary01 >= 0 && orgBoundary01 < invalidOrgBoundaryKey;
		const bool isOrgBoundaryValid12 = orgBoundary12 >= 0 && orgBoundary12 < invalidOrgBoundaryKey;
		const bool isOrgBoundaryValid20 = orgBoundary20 >= 0 && orgBoundary20 < invalidOrgBoundaryKey;

		// Set original boundary for each vert
		if (isOrgBoundaryValid01)
		{
			pOrgBoundary[vPackedIdx0].SetBit(orgBoundary01);
			pOrgBoundary[vPackedIdx1].SetBit(orgBoundary01);
		}

		if (isOrgBoundaryValid12)
		{
			pOrgBoundary[vPackedIdx1].SetBit(orgBoundary12);
			pOrgBoundary[vPackedIdx2].SetBit(orgBoundary12);
		}

		if (isOrgBoundaryValid20)
		{
			pOrgBoundary[vPackedIdx2].SetBit(orgBoundary20);
			pOrgBoundary[vPackedIdx0].SetBit(orgBoundary20);
		}
	}

	ExternalBitArray& dummyOrgBoundary = pOrgBoundary[numValidVerts];

	U32F numTotalInvalidTriangles = 0;

	while (true)
	{
		U32F numInvalidTriangles = 0;

		for (U32F iTri = 0; iTri < numTriangles; iTri++)
		{
			if (!pValidTrisOut[iTri])
			{
				continue;
			}

			const TriangleIndices& tri = pTriangles[iTri];

			const I32 vPackedIdx0 = tri.m_indices[0];
			const I32 vPackedIdx1 = tri.m_indices[1];
			const I32 vPackedIdx2 = tri.m_indices[2];

			ExternalBitArray::BitwiseAnd(&dummyOrgBoundary, pOrgBoundary[vPackedIdx0], pOrgBoundary[vPackedIdx1]);
			ExternalBitArray::BitwiseAnd(&dummyOrgBoundary, dummyOrgBoundary, pOrgBoundary[vPackedIdx2]);

			if (!dummyOrgBoundary.AreAllBitsClear())
			{
				pValidTrisOut[iTri] = false; // All verts collinear
				numInvalidTriangles++;
				continue;
			}

			const U32F vGlobalIdx0 = vertPackedToGlobal.Find(vPackedIdx0);
			const U32F vGlobalIdx1 = vertPackedToGlobal.Find(vPackedIdx1);
			const U32F vGlobalIdx2 = vertPackedToGlobal.Find(vPackedIdx2);

			NAV_ASSERT(vGlobalIdx0 != IndexMapTable::kInvalidIndex);
			NAV_ASSERT(vGlobalIdx1 != IndexMapTable::kInvalidIndex);
			NAV_ASSERT(vGlobalIdx2 != IndexMapTable::kInvalidIndex);

			const U32F tableIdx01 = vGlobalIdx0 * vIntersectionGlobalIdx + vGlobalIdx1;
			const U32F tableIdx12 = vGlobalIdx1 * vIntersectionGlobalIdx + vGlobalIdx2;
			const U32F tableIdx20 = vGlobalIdx2 * vIntersectionGlobalIdx + vGlobalIdx0;

			const I16 orgBoundary01 = pOrgBoundaryTable[tableIdx01];
			const I16 orgBoundary12 = pOrgBoundaryTable[tableIdx12];
			const I16 orgBoundary20 = pOrgBoundaryTable[tableIdx20];

			const bool isOrgBoundaryValid01 = orgBoundary01 >= 0 && orgBoundary01 < invalidOrgBoundaryKey;
			const bool isOrgBoundaryValid12 = orgBoundary12 >= 0 && orgBoundary12 < invalidOrgBoundaryKey;
			const bool isOrgBoundaryValid20 = orgBoundary20 >= 0 && orgBoundary20 < invalidOrgBoundaryKey;

			const bool invalidNeighbor01 = tri.m_neighborTris[0] < 0 || !pValidTrisOut[tri.m_neighborTris[0]];
			const bool invalidNeighbor12 = tri.m_neighborTris[1] < 0 || !pValidTrisOut[tri.m_neighborTris[1]];
			const bool invalidNeighbor20 = tri.m_neighborTris[2] < 0 || !pValidTrisOut[tri.m_neighborTris[2]];

			if (invalidNeighbor01 &&	// Has no neighbor triangle
				!isOrgBoundaryValid01)	// Has no original boundary
			{
				pValidTrisOut[iTri] = false; // Exterior triangle
				numInvalidTriangles++;
				continue;
			}

			if (invalidNeighbor12 &&	// Has no neighbor triangle
				!isOrgBoundaryValid12)	// Has no original boundary
			{
				pValidTrisOut[iTri] = false; // Exterior triangle
				numInvalidTriangles++;
				continue;
			}

			if (invalidNeighbor20 &&	// Has no neighbor triangle
				!isOrgBoundaryValid20)	// Has no original boundary
			{
				pValidTrisOut[iTri] = false; // Exterior triangle
				numInvalidTriangles++;
				continue;
			}
		}

		if (!numInvalidTriangles)
		{
			break;
		}

		numTotalInvalidTriangles += numInvalidTriangles;
	}

	NAV_ASSERT(numTotalInvalidTriangles <= numTriangles);

	return numTriangles - numTotalInvalidTriangles;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static U32F EliminateZeroAreaTriangles(TriangleIndices* pTriangles,
									   U32F numTriangles,
									   const Vec2* pVerts,
									   bool* pValidTrisOut)
{
	if (!pTriangles || (numTriangles == 0) || !pVerts || !pValidTrisOut)
	{
		return 0;
	}

	const float kMinTriangleArea = 0.001f;
	U32F validCount = 0;

	for (U32F iTri = 0; iTri < numTriangles; ++iTri)
	{
		const TriangleIndices& tri = pTriangles[iTri];

		if ((tri.m_indices[0] != tri.m_indices[1]) || (tri.m_indices[0] != tri.m_indices[2]))
		{
			++validCount;
			pValidTrisOut[iTri] = true;
			continue;
		}

		pValidTrisOut[iTri] = false;

		/*
				for (U32F iV0 = 0; iV0 < 3; ++iV0)
				{
					const I32F iN = tri.m_neighborTris[iV0];
					if (iN < 0)
						continue;

					TriangleIndices& neighborTri = pTriangles[iN];

					for (U32F iV1 = 0; iV1 < 3; ++iV1)
					{
						if (neighborTri.m_neighborTris[iV1] == iTri)
						{
							neighborTri.m_neighborTris[iV1] = -1;
						}
					}
				}
		*/
	}

	return validCount;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static U32F CountInsideVerts(const NavBlockerClipperEntry* pBlockerPolysLs, U32F numEntries)
{
	U32F count = 0;
	for (U32F iBlocker = 0; iBlocker < numEntries; ++iBlocker)
	{
		const Clip2D::Poly p = pBlockerPolysLs[iBlocker].m_clipPoly;

		for (U32F iV = 0; iV < p.m_numVerts; ++iV)
		{
			const Clip2D::Vertex& v = p.m_verts[iV];
			if (v.m_state == Clip2D::VertexState::kOutside)
				continue;
			++count;
		}
	}
	return count;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool TriangleMergeTestFunc(const TriangleIndices& tri0,
								  const TriangleIndices& tri1,
								  I32F iN0,
								  I32F iN1,
								  void* pUserData)
{
	if (iN0 < 0 || iN0 >= 3 || iN1 < 0 || iN1 >= 3)
		return false;

	NavBlockerBits* pVertBlockage = (NavBlockerBits*)pUserData;

#if 0
	const I32F oddIndex0 = tri0.m_indices[(iN0 + 2) % 3];
	const I32F oddIndex1 = tri1.m_indices[(iN1 + 2) % 3];

	const NavBlockerBits& bits0 = pVertBlockage[oddIndex0];
	const NavBlockerBits& bits1 = pVertBlockage[oddIndex1];

	NavBlockerBits res;
	NavBlockerBits::BitwiseXor(&res, bits0, bits1);
#else
	NavBlockerBits triBits0;
	NavBlockerBits::BitwiseAnd(&triBits0, pVertBlockage[tri0.m_indices[0]], pVertBlockage[tri0.m_indices[1]]);
	NavBlockerBits::BitwiseAnd(&triBits0, triBits0, pVertBlockage[tri0.m_indices[2]]);

	NavBlockerBits triBits1;
	NavBlockerBits::BitwiseAnd(&triBits1, pVertBlockage[tri1.m_indices[0]], pVertBlockage[tri1.m_indices[1]]);
	NavBlockerBits::BitwiseAnd(&triBits1, triBits1, pVertBlockage[tri1.m_indices[2]]);

	NavBlockerBits res;
	NavBlockerBits::BitwiseXor(&res, triBits0, triBits1);
#endif

	return res.AreAllBitsClear();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static VertKey GetTopLevelVertKey(U32F iEntry, U32F iV, const NavBlockerClipperEntry* pBlockerPolysLs, U32F numEntries)
{
	VertKey k;

	k.m_iBlocker = iEntry + 1;
	k.m_iVert	 = iV;

	if (!pBlockerPolysLs || (iEntry >= numEntries))
		return k;

	while (pBlockerPolysLs[k.m_iBlocker - 1].m_clipPoly.m_verts[k.m_iVert].m_neighborIndex >= 0)
	{
		const Clip2D::Vertex& v = pBlockerPolysLs[k.m_iBlocker - 1].m_clipPoly.m_verts[k.m_iVert];

		// decode neighbor information
		const I32 iOtherVert  = v.m_neighborIndex & 0xFFFF;
		const I32 iOtherEntry = ((v.m_neighborIndex >> 16UL) & 0xFFFF) - 1;

		k.m_iBlocker = iOtherEntry + 1;
		k.m_iVert	 = iOtherVert;

		if (k.m_iBlocker == 0)
			break;
	}
	return k;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static I32F GetNeighborIndexFor(const NavPolyEx* pSourceEx, const NavPoly* pDest)
{
	if (!pSourceEx || !pDest)
		return -1;

	const NavManagerId destId = pDest->GetNavManagerId();

	I32F ret = -1;

	for (U32F iV = 0; iV < pSourceEx->m_numVerts; ++iV)
	{
		if (pSourceEx->m_adjPolys[iV] == destId)
		{
			ret = iV;
			break;
		}
	}

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float RateNeighborConnection(const NavPolyEx* pPolyExA, const I32F iNA, const NavPolyEx* pPolyExB, const I32F iNB)
{
	if (!pPolyExA || !pPolyExB || (iNA < 0) || (iNA >= pPolyExA->m_numVerts) || (iNB < 0)
		|| (iNB >= pPolyExB->m_numVerts))
		return kLargeFloat;

	const NavMesh* pMeshA = pPolyExA->GetNavMesh();
	const NavMesh* pMeshB = pPolyExB->GetNavMesh();

	const Point posA0 = pMeshA->LocalToWorld(pPolyExA->GetVertex(iNA));
	const Point posA1 = pMeshA->LocalToWorld(pPolyExA->GetNextVertex(iNA));
	const Point posB0 = pMeshB->LocalToWorld(pPolyExB->GetVertex(iNB));
	const Point posB1 = pMeshB->LocalToWorld(pPolyExB->GetNextVertex(iNB));

	const float rating0 = DistSqr(posA0, posB0) + DistSqr(posA1, posB1);
	const float rating1 = DistSqr(posA1, posB0) + DistSqr(posA0, posB1);

	const float rating = Min(rating0, rating1);

	return rating;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void FixupExNeighborsBetweenPolys(const NavPoly* pPolyA, const NavPoly* pPolyB)
{
	NavPolyEx* pExListA = pPolyA ? pPolyA->GetNavPolyExList() : nullptr;
	NavPolyEx* pExListB = pPolyB ? pPolyB->GetNavPolyExList() : nullptr;

	if (!pExListA || !pExListB)
		return;

	for (NavPolyEx* pPolyExA = pExListA; pPolyExA; pPolyExA = pPolyExA->m_pNext)
	{
		const I32F iNA = GetNeighborIndexFor(pPolyExA, pPolyB);
		if (iNA < 0)
			continue;

		NavPolyEx* pBestNeighbor = nullptr;
		float bestRating		 = kLargeFloat;
		I32F bestNeighborIndex	 = -1;

		for (NavPolyEx* pPolyExB = pExListB; pPolyExB; pPolyExB = pPolyExB->m_pNext)
		{
			const I32F iNB = GetNeighborIndexFor(pPolyExB, pPolyA);
			if (iNB < 0)
				continue;

			const float rating = RateNeighborConnection(pPolyExA, iNA, pPolyExB, iNB);

			if (rating < bestRating)
			{
				bestRating		  = rating;
				pBestNeighbor	  = pPolyExB;
				bestNeighborIndex = iNB;
			}
		}

		if (pBestNeighbor)
		{
			pBestNeighbor->m_adjPolys[bestNeighborIndex] = pPolyExA->GetNavManagerId();
			pPolyExA->m_adjPolys[iNA] = pBestNeighbor->GetNavManagerId();
		}
	}
}

static int QueueIncrement(int idx, int queueArraySize)
{
	NAV_ASSERT(queueArraySize > 0);
	NAV_ASSERT(idx >= 0 && idx < queueArraySize);

	return (idx + 1) % queueArraySize;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void BuildNavPolyBlockerIslands(const NavPolyBlockerIntersections* const pIntersections,
									   NavPolyBlockerIslands* const pIslands)
{
	PROFILE_AUTO(Navigation);

	NAV_ASSERT(pIntersections && pIslands);

	const U32F numIntersections = pIntersections->Size();
	if (numIntersections)
	{
		// bfs visit flags
		int* const pBfsDist = NDI_NEW int[numIntersections];
		for (U32F i = 0; i < numIntersections; i++)
		{
			pBfsDist[i] = -1;
		}

		// Simple queue implemented as array
		const size_t queueArraySize = numIntersections + 1; // The last entry doesnt store anything.
		const NavPoly** polyQueue	= NDI_NEW const NavPoly * [queueArraySize];
		U32F* idxQueue = NDI_NEW U32F[queueArraySize];
		int queueFront;
		int queueEnd;

		for (U32F navPolyGlobalIdx = 0; navPolyGlobalIdx < numIntersections; navPolyGlobalIdx++)
		{
			if (pBfsDist[navPolyGlobalIdx] >= 0)
			{
				continue;
			}

			// This is the source node of a new island, bfs to populate the island
			NAV_ASSERT(pIslands->m_numIslands < NavPolyBlockerIslands::kMaxIslands);
			NavPolyBlockerIslands::Island& island = pIslands->m_pIslands[pIslands->m_numIslands];

			// Reset the island
			island.m_numNavPolys = 0;
			island.m_navBlockers.ClearAllBits();

			// Reset the queue
			queueFront = 0;
			queueEnd   = 0;

			const NavPoly* const pSrcNavPoly = pIntersections->m_pNavPolys[navPolyGlobalIdx];

			// Enqueue src nav poly
			NAV_ASSERT(QueueIncrement(queueEnd, queueArraySize) != queueFront); // Queue not full
			polyQueue[queueEnd] = pSrcNavPoly;
			idxQueue[queueEnd]	= navPolyGlobalIdx;
			queueEnd = QueueIncrement(queueEnd, queueArraySize);

			// Visit this node, means this node has already been added to the queue.
			pBfsDist[navPolyGlobalIdx] = 0;

			while (queueFront != queueEnd) // Queue not empty
			{
				// Dequeue
				const NavPoly* const pNavPoly0 = polyQueue[queueFront];
				const NavMesh* const pNavMesh0 = pNavPoly0->GetNavMesh();

				const U32F navPolyGlobalIdx0 = idxQueue[queueFront];
				queueFront = QueueIncrement(queueFront, queueArraySize);

#ifndef FINAL_BUILD
				if (g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawIslands)
				{
					PrimServerWrapper ps(pNavMesh0->GetParentSpace());

					pNavPoly0->DebugDrawEdges(kColorMagentaTrans, kColorMagentaTrans, 0.15f);

					const Point centerLs = pNavPoly0->GetCentroid();
					const Point centerWs = pNavMesh0->LocalToWorld(centerLs);
					ps.DrawString(centerWs,
								  StringBuilder<32>("%d:%d:%d:%d",
													pIslands->m_numIslands,
													island.m_numNavPolys,
													pBfsDist[navPolyGlobalIdx0],
													pNavPoly0->GetId())
									  .c_str(),
								  kColorMagentaTrans,
								  0.75f);
				}
#endif
				island.m_navPolys[island.m_numNavPolys++] = navPolyGlobalIdx0;

				const NavBlockerBits& navPolyToBlockers = pIntersections->m_pNavPolyToBlockers[navPolyGlobalIdx0];
				NavBlockerBits::BitwiseOr(&island.m_navBlockers, island.m_navBlockers, navPolyToBlockers);

				const U32F edgeCount0 = pNavPoly0->GetVertexCount();
				for (U32F iEdge0 = 0; iEdge0 < edgeCount0; iEdge0++)
				{
					const U32F adjPolyId0 = pNavPoly0->GetAdjacentId(iEdge0);
					if (adjPolyId0 != NavPoly::kNoAdjacent)
					{
						U32F navPolyGlobalIdx1 = numIntersections;

						// Check if the adjacent nav poly intersects any nav blockers
						const NavPoly* pNavPoly1 = &pNavMesh0->GetPoly(adjPolyId0);
						if (pNavPoly1->IsLink())
						{
							pNavPoly1 = pNavPoly1->GetLinkedPoly();
							if (!pNavPoly1 || !pNavPoly1->IsValid())
							{
								continue; // Bad link, "hard" boundary
							}
						}

						for (U32F i = 0; i < numIntersections; i++)
						{
							if (pIntersections->m_pNavPolys[i] == pNavPoly1)
							{
								navPolyGlobalIdx1 = i;
								break;
							}
						}

						if (navPolyGlobalIdx1 < numIntersections
							&&								   // The (linked) adjacent nav poly intersects nav blocker(s)
							pBfsDist[navPolyGlobalIdx1] == -1) // Not yet visited
						{
							// Enqueue the adjacent nav poly
							NAV_ASSERT(QueueIncrement(queueEnd, queueArraySize) != queueFront); // Queue not full

							polyQueue[queueEnd] = pNavPoly1;
							idxQueue[queueEnd]	= navPolyGlobalIdx1;
							queueEnd = QueueIncrement(queueEnd, queueArraySize);

							// Visit this node, means this node has already been added to the queue.
							pBfsDist[navPolyGlobalIdx1] = pBfsDist[navPolyGlobalIdx0] + 1;
						}
					}
				}
			}

			pIslands->m_numIslands++;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void BuildNavPolyBlockerIntersections(const NavMeshPatchInput& patchInput,
											 NavPolyBlockerIntersections* const pIntersections,
											 NavPolyList* const pPolyResults)
{
	PROFILE_AUTO(Navigation);

	// for each blocker get the list of nav polys it touches
	for (U32F iBlocker = 0; iBlocker < patchInput.m_numBlockers; ++iBlocker)
	{
		const DynamicNavBlocker* pNavBlocker = &patchInput.m_pBlockers[iBlocker];

		if (nullptr == pNavBlocker->GetNavPoly())
		{
			continue;
		}

		const I32F blockerIndex = patchInput.m_pMgrIndices[iBlocker];

		if ((blockerIndex < 0) || (blockerIndex >= kMaxDynamicNavBlockerCount))
		{
			continue;
		}

		const float blockerRadius = pNavBlocker->GetBoundingRadius() * 1.1f;
		if (blockerRadius < kSmallFloat)
		{
			continue;
		}

		const U32F numPolys = GetIntersectingPolysForNavBlocker(pNavBlocker, blockerRadius, &pPolyResults[iBlocker]);

		if (!numPolys)
		{
			continue;
		}

		const NavPolyList& results = pPolyResults[iBlocker];
		for (U32F iPoly = 0; iPoly < numPolys; ++iPoly)
		{
			const NavPoly* pPoly = results.m_pPolys[iPoly];
			if (!pPoly || pPoly->IsLink())
			{
				continue;
			}

			// Note that we keep the further polys around for gap generation later
			if (results.m_distances[iPoly] > blockerRadius)
			{
				continue;
			}

			const float xzArea = Abs(pPoly->ComputeSignedAreaXz());
			if (xzArea < NDI_FLT_EPSILON)
			{
				continue;
			}

			pIntersections->AddEntry(pPoly, iBlocker);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool TryMergeNavBlockers(U64 iBlocker,
								const NavMeshPatchInput* pPatchInput,
								NavBlockerBits& blockerBits,
								NavBlockerBits& remainingBlockers)
{
	static const float kEpsilon = 0.001f;

	blockerBits.ClearAllBits();

	const DynamicNavBlocker* pBlocker = &pPatchInput->m_pBlockers[iBlocker];

	const I32F navBlockerIndex = pPatchInput->m_pMgrIndices[iBlocker];

	if (navBlockerIndex >= 0)
	{
		blockerBits.SetBit(navBlockerIndex);
	}

	for (U64 iOtherBlocker = remainingBlockers.FindFirstSetBit(); iOtherBlocker < kMaxDynamicNavBlockerCount;
		 iOtherBlocker	   = remainingBlockers.FindNextSetBit(iOtherBlocker))
	{
		if (iOtherBlocker == iBlocker)
			continue;

		const DynamicNavBlocker* pOtherBlocker = &pPatchInput->m_pBlockers[iOtherBlocker];

		const I32F otherNavBlockerIndex = pPatchInput->m_pMgrIndices[iOtherBlocker];
		if (otherNavBlockerIndex < 0 || otherNavBlockerIndex >= kMaxDynamicNavBlockerCount)
			continue;

		const Point v00 = pBlocker->GetQuadVertex(0);
		const Point v01 = pBlocker->GetQuadVertex(1);
		const Point v02 = pBlocker->GetQuadVertex(2);
		const Point v03 = pBlocker->GetQuadVertex(3);

		const float d00 = Dist(v00, v02);
		const float d01 = Dist(v01, v03);

		const Point v10 = pOtherBlocker->GetQuadVertex(0);
		const Point v11 = pOtherBlocker->GetQuadVertex(1);
		const Point v12 = pOtherBlocker->GetQuadVertex(2);
		const Point v13 = pOtherBlocker->GetQuadVertex(3);
		const float d10 = Dist(v10, v12);
		const float d11 = Dist(v11, v13);

		if ((Abs(d00 - d10) > kEpsilon) || (Abs(d01 - d11) > kEpsilon))
			continue;

		if (DistXz(pBlocker->GetPosPs(), pOtherBlocker->GetPosPs()) > kEpsilon)
			continue;

		blockerBits.SetBit(otherNavBlockerIndex);
		remainingBlockers.ClearBit(iOtherBlocker);
	}

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static U32F IntersectBlockerNavPoly(const NavMesh* pMesh,
									const NavPoly* pNavPoly, // For debug drawing for now.
									const NavBlockerBits& blockerBits,
									const NavMeshPatchInput* pPatchInput,
									const Clip2D::Poly* pBlockerClipPolysWs,
									Clip2D::Poly* pNavClipPolyLsOut, // Modified(clipped) by the blocker clip polys.
									NavBlockerClipperEntry* pBlockerClipperPolysLsOut,
									U32F maxEntriesOut,
									NavBlockerBits* pInitialPolyBlockageOut)
{
	// PROFILE_ACCUM(IntersectBlockerNavPoly);

	NavBlockerMgr& nbMgr = NavBlockerMgr::Get();
	NavBlockerBits remainingBlockers = blockerBits;

	U32F numEntries = 0;

	for (U64 iBlocker = remainingBlockers.FindFirstSetBit(); iBlocker < kMaxDynamicNavBlockerCount;
		 iBlocker	  = remainingBlockers.FindNextSetBit(iBlocker))
	{
		const I32F navBlockerIndex = pPatchInput->m_pMgrIndices[iBlocker];
		if (navBlockerIndex < 0 || navBlockerIndex >= kMaxDynamicNavBlockerCount)
		{
			continue;
		}

		NavBlockerBits entryBits;
		entryBits.ClearAllBits();

		if (!TryMergeNavBlockers(iBlocker, pPatchInput, entryBits, remainingBlockers))
		{
			continue;
		}

		const Clip2D::Poly& blockerClipPolyWs = pBlockerClipPolysWs[iBlocker];
		Clip2D::Poly blockerClipPolyLs		  = WorldToLocal(pMesh, blockerClipPolyWs);

		const U32F initialPolyVertCount = pNavClipPolyLsOut->m_numVerts;

		{
			// PROFILE_ACCUM(IntersectBlockerNavPoly_Intersect);
			Clip2D::IntersectPolys(pNavClipPolyLsOut, &blockerClipPolyLs);

			const bool debugDrawIntersect = false;
			if (FALSE_IN_FINAL_BUILD(debugDrawIntersect)) // Debug draw the results of intersecting nav clip poly and blocker clip poly
			{
				const U64 debugDrawBlockerIndex = 0;
				const int debugDrawNavPolyId	= -1;

				if (navBlockerIndex == debugDrawBlockerIndex
					&& (debugDrawNavPolyId < 0 || pNavPoly->GetId() == static_cast<ushort>(debugDrawNavPolyId)))
				{
					const DebugPrimTime tt = kPrimDuration1Frame;

					DebugDrawClipPoly(LocalToWorld(pMesh, (*pNavClipPolyLsOut)), 0.0f, kColorMagenta, tt);
					DebugDrawClipPoly(LocalToWorld(pMesh, blockerClipPolyLs), 0.0f, kColorCyan, tt);
				}
			}
		}

		for (U32F iV = 0; iV < pNavClipPolyLsOut->m_numVerts; ++iV)
		{
			const Clip2D::Vertex& vert = pNavClipPolyLsOut->m_verts[iV];
			if ((vert.m_state == Clip2D::VertexState::kOutside) && !vert.m_onNeighborBoundary)
			{
				continue;
			}

			if (iV >= initialPolyVertCount)
			{
				pInitialPolyBlockageOut[iV] = entryBits;
			}
			else
			{
				NavBlockerBits::BitwiseOr(&pInitialPolyBlockageOut[iV], pInitialPolyBlockageOut[iV], entryBits);
			}
		}

		// check to see if any of our newly generated verts lay inside previous blocker polys
		for (U32F iV = initialPolyVertCount; iV < pNavClipPolyLsOut->m_numVerts; ++iV)
		{
			const Clip2D::Vertex& vert = pNavClipPolyLsOut->m_verts[iV];

			for (U32F iPrevEntry = 0; iPrevEntry < numEntries; ++iPrevEntry)
			{
				bool onBoundary = false;
				if (Clip2D::IsPointInside(vert.m_pos, pBlockerClipperPolysLsOut[iPrevEntry].m_clipPoly, &onBoundary)
					|| onBoundary)
				{
					NavBlockerBits::BitwiseOr(&pInitialPolyBlockageOut[iV],
											  pInitialPolyBlockageOut[iV],
											  pBlockerClipperPolysLsOut[iPrevEntry].m_blockers);
				}
			}
		}

		pBlockerClipperPolysLsOut[numEntries].m_blockers = entryBits;
		pBlockerClipperPolysLsOut[numEntries].m_clipPoly = blockerClipPolyLs;
		++numEntries;

		if (numEntries >= maxEntriesOut)
		{
			break;
		}
	}

	return numEntries;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void IntersectBlockerBlocker(NavBlockerClipperEntry* pBlockerPolysLs, U32F numBlockerPolys)
{
	for (U32F iBlocker = 1; iBlocker < numBlockerPolys; ++iBlocker)
	{
		Clip2D::Poly* pBlockerPoly = &pBlockerPolysLs[iBlocker].m_clipPoly;

		for (U32F iOtherBlocker = 0; iOtherBlocker < iBlocker; ++iOtherBlocker)
		{
			Clip2D::Poly* pOtherBlockerPoly = &pBlockerPolysLs[iOtherBlocker].m_clipPoly;

			const Clip2D::Poly orgBlocker	   = *pBlockerPoly;
			const Clip2D::Poly orgOtherBlocker = *pOtherBlockerPoly;

			const BitArray64 intersections = Clip2D::IntersectPolys(pBlockerPoly, pOtherBlockerPoly, false);

			for (U32F i = 0; i < orgBlocker.m_numVerts; ++i)
			{
				if (intersections.IsBitSet(i))
					continue;

				pBlockerPoly->m_verts[i].m_neighborIndex	  = orgBlocker.m_verts[i].m_neighborIndex;
				pBlockerPoly->m_verts[i].m_onNeighborBoundary = orgBlocker.m_verts[i].m_onNeighborBoundary;
			}

			for (U32F i = 0; i < orgOtherBlocker.m_numVerts; ++i)
			{
				pOtherBlockerPoly->m_verts[i].m_neighborIndex	   = orgOtherBlocker.m_verts[i].m_neighborIndex;
				pOtherBlockerPoly->m_verts[i].m_onNeighborBoundary = orgOtherBlocker.m_verts[i].m_onNeighborBoundary;
			}

			for (U64 iIntersection = intersections.FindFirstSetBit(); iIntersection < 64;
				 iIntersection	   = intersections.FindNextSetBit(iIntersection))
			{
				const I32 iS = iIntersection;
				const I32 iC = pBlockerPoly->m_verts[iIntersection].m_neighborIndex;
				NAV_ASSERT(iC >= 0 && iC < pOtherBlockerPoly->m_numVerts);

				// encode blocker-vs-blocker neighbor information and restore
				// original neighbor indices because we will care about
				// them in ClipNavPolyFromBlockers/GetTopLevelVertKey
				pBlockerPoly->m_verts[iS].m_neighborIndex = iC | ((1 + iOtherBlocker) << 16);

				pOtherBlockerPoly->m_verts[iC].m_neighborIndex = -1;
				pOtherBlockerPoly->m_verts[iC].m_onNeighborBoundary = false;

				if (iC < orgOtherBlocker.m_numVerts)
				{
					pOtherBlockerPoly->m_verts[iC].m_neighborIndex		= orgOtherBlocker.m_verts[iC].m_neighborIndex;
					pOtherBlockerPoly->m_verts[iC].m_onNeighborBoundary = orgOtherBlocker.m_verts[iC]
																			  .m_onNeighborBoundary;
				}

				if ((pOtherBlockerPoly->m_verts[iC].m_neighborIndex < 0) && (iS < orgBlocker.m_numVerts))
				{
					pOtherBlockerPoly->m_verts[iC].m_neighborIndex		= orgBlocker.m_verts[iS].m_neighborIndex;
					pOtherBlockerPoly->m_verts[iC].m_onNeighborBoundary = orgBlocker.m_verts[iS].m_onNeighborBoundary;
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool VertAlreadyExists(const Vec2& newVert, const Vec2* pVerts, U32F vertCount)
{
	for (U32F i = 0; i < vertCount; ++i)
	{
		const float d = Length(newVert - pVerts[i]);

		if (d < NDI_FLT_EPSILON)
		{
			return true;
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool ClipNavPolyFromBlockers(const NavMesh* pMesh,
									const NavPoly* pPoly,
									const Clip2D::Poly& navClipPolyLs,
									const NavBlockerBits* pInitialNavPolyBlockage,
									const NavBlockerClipperEntry* pBlockerPolysLs,
									U32F numEntries)
{
	if (!pMesh || !pPoly || !pBlockerPolysLs || (0 == numEntries))
	{
		return false;
	}

	const U32F polyVertCount   = navClipPolyLs.m_numVerts;
	const U32F maxBlockerVerts = CountInsideVerts(pBlockerPolysLs, numEntries);
	const U32F maxVerts		   = polyVertCount + maxBlockerVerts;

	VertTable vtable;
	vtable.Init(2 * maxVerts, FILE_LINE_FUNC);

	Vec2* pVerts = NDI_NEW Vec2[maxVerts];
	NavBlockerBits* pVertBlockage = NDI_NEW NavBlockerBits[maxVerts];

	// memset(pVertBlockage, 0, sizeof(NavBlockerBits) * maxVerts);

	U32F vertCount	  = 0;
	U32F dupVertCount = 0;

	U64* pVisitedNavBlocks = NDI_NEW U64[ExternalBitArray::DetermineNumBlocks(navClipPolyLs.m_numVerts)];
	ExternalBitArray visitedNavVerts;
	visitedNavVerts.Init(navClipPolyLs.m_numVerts, pVisitedNavBlocks);

	bool abort = false;

	for (U32F iV = 0; iV < polyVertCount; ++iV)
	{
		const Clip2D::Vertex& vert = navClipPolyLs.m_verts[iV];

		const Point v	   = vert.m_pos;
		const Vec2 newVert = Vec2(v.X(), v.Z());

		if (FALSE_IN_FINAL_BUILD(VertAlreadyExists(newVert, pVerts, vertCount)))
		{
#ifdef JBELLOMY
			const DebugPrimTime tt = kPrimDuration1Frame;
			const float vo		   = float(pPoly->GetId() % 10) * 0.1f;

			const Point dupPos = pMesh->LocalToWorld(v) + Vector(0.0f, 1.0f, 0.0f);

			g_prim.Draw(DebugCross(dupPos, 0.1f, kColorCyan), tt);
			g_prim.Draw(DebugString(dupPos, StringBuilder<32>("%d", (int)vertCount).c_str(), kColorCyan, 0.75f), tt);
			g_prim.Draw(DebugSphere(dupPos, 0.1f, kColorMagenta), tt);

			g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchInput = true;
			g_navOptions.m_navMeshPatch.m_serialNavMeshPatching   = true;
#else
			// if this happens please show cowboy (or save a crash dump) thanks!
			ASSERT(false);
#endif
			abort = true;
			continue;
		}

		pVerts[vertCount] = newVert;

		VertKey k;
		k.m_iBlocker = 0;
		k.m_iVert	 = iV;

		VertData d;
		d.m_pVert		  = &pVerts[vertCount];
		d.m_originalIndex = vertCount;

		pVertBlockage[vertCount] = pInitialNavPolyBlockage[iV];

		visitedNavVerts.SetBit(iV);

		vtable.Add(k, d);
		++vertCount;
		++dupVertCount;
	}

	if (FALSE_IN_FINAL_BUILD(abort))
	{
		return false;
	}

	for (U32F iEntry = 0; iEntry < numEntries; ++iEntry)
	{
		const Clip2D::Poly& blockerPoly = pBlockerPolysLs[iEntry].m_clipPoly;

		for (U32F iV = 0; iV < blockerPoly.m_numVerts; ++iV)
		{
			const Clip2D::Vertex& v = blockerPoly.m_verts[iV];
			if (v.m_state == Clip2D::VertexState::kOutside)
			{
				continue;
			}

			++dupVertCount;

			const VertKey k = GetTopLevelVertKey(iEntry, iV, pBlockerPolysLs, numEntries);

			VertTableItr itr = vtable.Find(k);

			if (itr != vtable.End())
			{
				const U32F orgIndex = itr->m_data.m_originalIndex;
				NavBlockerBits& originalBlockage = pVertBlockage[orgIndex];
				NavBlockerBits::BitwiseOr(&originalBlockage, originalBlockage, pBlockerPolysLs[iEntry].m_blockers);
				continue;
			}

			const Vec2 newVert = Vec2(v.m_pos.X(), v.m_pos.Z());

			if (FALSE_IN_FINAL_BUILD(VertAlreadyExists(newVert, pVerts, vertCount)))
			{
#ifdef JBELLOMY
				const DebugPrimTime tt = kPrimDuration1Frame;
				const float vo		   = float(pPoly->GetId() % 10) * 0.1f;

				const Point dupPos = pMesh->LocalToWorld(v.m_pos)
									 + Vector(0.0f, 1.0f + (float(iEntry + 1) * 0.5f), 0.0f);

				g_prim.Draw(DebugCross(dupPos, 0.1f, kColorCyan), tt);
				g_prim.Draw(DebugString(dupPos, StringBuilder<32>("%d", (int)vertCount).c_str(), kColorCyan, 0.75f), tt);
				g_prim.Draw(DebugSphere(dupPos, 0.1f, kColorMagenta), tt);

				g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchInput = true;
				g_navOptions.m_navMeshPatch.m_serialNavMeshPatching   = true;
#else
				// if this happens please show cowboy (or save a crash dump) thanks!
				ASSERT(false);
#endif
				abort = true;
			}

			pVerts[vertCount] = newVert;

			VertData d;
			d.m_pVert		  = &pVerts[vertCount];
			d.m_originalIndex = vertCount;

			NavBlockerBits& vertBlockage = pVertBlockage[vertCount];

			vertBlockage = pBlockerPolysLs[iEntry].m_blockers;

			for (U32F iOtherEntry = 0; iOtherEntry < numEntries; ++iOtherEntry)
			{
				if (iOtherEntry == iEntry)
				{
					continue;
				}

				bool onBoundary = false;
				if (Clip2D::IsPointInside(v.m_pos, pBlockerPolysLs[iOtherEntry].m_clipPoly, &onBoundary) || onBoundary)
				{
					NavBlockerBits::BitwiseOr(&vertBlockage, vertBlockage, pBlockerPolysLs[iOtherEntry].m_blockers);
				}
			}

			ASSERT(vtable.Find(k) == vtable.End());
			vtable.Add(k, d);
			++vertCount;
		}
	}

	U32* pSegments	 = NDI_NEW U32[dupVertCount * 2];
	U32 segmentCount = 0;

	I32* piOrgBoundaries = NDI_NEW I32[dupVertCount];

	for (U32F iV = 0; iV < polyVertCount; ++iV)
	{
		const U32F iVN = navClipPolyLs.GetNextVertexIndex(iV);
		NAV_ASSERT(iVN < navClipPolyLs.m_numVerts);

		pSegments[(segmentCount * 2) + 0] = iV;
		pSegments[(segmentCount * 2) + 1] = iVN;

		const I32 piOrgBoundary			= navClipPolyLs.m_verts[iV].m_originalEdge;
		piOrgBoundaries[segmentCount++] = piOrgBoundary;

		const bool debugDrawOrgBoundary = false;
		if (FALSE_IN_FINAL_BUILD(debugDrawOrgBoundary))
		{
			// Debug draw original boudaries
			const Point vLs		= navClipPolyLs.GetVertex(iV);
			const Point vNextLs = navClipPolyLs.GetVertex(iVN);

			const Point vWs		= pMesh->LocalToWorld(Point(vLs.X(), vLs.Y() + 0.15f, vLs.Z()));
			const Point vNextWs = pMesh->LocalToWorld(Point(vNextLs.X(), vNextLs.Y() + 0.15f, vNextLs.Z()));

			g_prim.Draw(DebugString(AveragePos(vWs, vNextWs),
									StringBuilder<64>("%d", piOrgBoundary).c_str(),
									kColorBlue,
									0.75f),
						kPrimDuration1Frame);
		}
	}

	for (U32F iEntry = 0; iEntry < numEntries; ++iEntry)
	{
		const Clip2D::Poly p = pBlockerPolysLs[iEntry].m_clipPoly;

		for (U32F iV = 0; iV < p.m_numVerts; ++iV)
		{
			const Clip2D::Vertex& v = p.m_verts[iV];
			if (v.m_state == Clip2D::VertexState::kOutside)
				continue;

			const VertKey k = GetTopLevelVertKey(iEntry, iV, pBlockerPolysLs, numEntries);

			VertTableItr itr0 = vtable.Find(k);

			if (itr0 == vtable.End())
				continue;

			const VertKey kn = GetTopLevelVertKey(iEntry, p.m_verts[iV].m_next, pBlockerPolysLs, numEntries);

			VertTableItr itr1 = vtable.Find(kn);

			if (itr1 == vtable.End())
				continue;

			const U32F i0 = itr0->m_data.m_originalIndex;
			const U32F i1 = itr1->m_data.m_originalIndex;

			NAV_ASSERT(i0 < vertCount);
			NAV_ASSERT(i1 < vertCount);

			pSegments[(segmentCount * 2) + 0] = i0;
			pSegments[(segmentCount * 2) + 1] = i1;
			piOrgBoundaries[segmentCount]	  = -1;
			++segmentCount;
		}
	}

	NAV_ASSERT(segmentCount <= dupVertCount);

	const bool debugDrawSegments = false;		 // pPoly->GetId() == 26;
	if (FALSE_IN_FINAL_BUILD(debugDrawSegments)) // Debug draw segments
	{
		const int debugDrawNavPolyId = -1;
		if (debugDrawNavPolyId < 0 || pPoly->GetId() == static_cast<ushort>(debugDrawNavPolyId))
		{
			for (U32F iSeg = 0; iSeg < segmentCount; ++iSeg)
			{
				const I32F i0 = pSegments[(iSeg * 2) + 0];
				const I32F i1 = pSegments[(iSeg * 2) + 1];

				const Point p0 = pMesh->LocalToWorld(Point(pVerts[i0].x, float(iSeg) * 0.01f, pVerts[i0].y));
				const Point p1 = pMesh->LocalToWorld(Point(pVerts[i1].x, float(iSeg) * 0.01f, pVerts[i1].y));

				g_prim.Draw(DebugLine(p0, p1, kColorMagenta));
				g_prim.Draw(DebugCross(p0, 0.025f, kColorMagenta));
				g_prim.Draw(DebugString(p0, StringBuilder<32>("%d", (int)iSeg).c_str(), kColorMagenta, 0.5f));

				if (piOrgBoundaries[iSeg] >= 0)
				{
					g_prim.Draw(DebugString(AveragePos(p0, p1),
											StringBuilder<32>("%d : %d", (int)iSeg, piOrgBoundaries[iSeg]).c_str(),
											kColorMagenta,
											0.5f));
				}
				else
				{
					g_prim.Draw(DebugString(AveragePos(p0, p1),
											StringBuilder<32>("%d", (int)iSeg).c_str(),
											kColorMagenta,
											0.5f));
				}
			}
		}
	}

	const bool debugDrawVertBlockers = false;
	if (FALSE_IN_FINAL_BUILD(debugDrawVertBlockers)) // Debug draw vertex blocker(s)
	{
		for (U32F iV = 0; iV < vertCount; ++iV)
		{
			const Point posWs = pMesh->LocalToWorld(Point(pVerts[iV].x, 0.2f, pVerts[iV].y));
			StringBuilder<256> str;

			const NavBlockerBits& blockage = pVertBlockage[iV];
			U64 iBlocker = blockage.FindFirstSetBit();

			if (iBlocker < kMaxDynamicNavBlockerCount)
			{
				str.append_format("%d : %d", (int)iV, iBlocker);

				for (iBlocker = blockage.FindNextSetBit(iBlocker); iBlocker < kMaxDynamicNavBlockerCount;
					 iBlocker = blockage.FindNextSetBit(iBlocker))
				{
					str.append_format(", %d", iBlocker);
				}

				g_prim.Draw(DebugString(posWs, str.c_str(), kColorRedTrans, 0.65f));
			}
			else
			{
				str.append_format("%d", (int)iV);

				g_prim.Draw(DebugString(posWs, str.c_str(), kColorGreenTrans, 0.65f));
			}
		}
	}

	if (FALSE_IN_FINAL_BUILD(abort))
	{
		return false;
	}

	DelaunayParams dtParams;
	dtParams.m_pPoints		   = pVerts;
	dtParams.m_numPoints	   = vertCount;
	dtParams.m_pSegmentIndices = pSegments;
	dtParams.m_numSegments	   = segmentCount;
	dtParams.m_localToWorldSpace = pMesh->GetOriginWs();

	const U32F maxTriangles		= (vertCount * 2) + 1;
	TriangleIndices* pTriangles = NDI_NEW TriangleIndices[maxTriangles];
	U32F numGenTriangles		= 0;

	{
		PROFILE_ACCUM(GenTriangles);

		numGenTriangles = GenerateDelaunay2d(DelaunayMethod::kIncrementalConstrained, dtParams, pTriangles, maxTriangles);
	}

	const bool debugDrawDelaunay2D = false;
	if (FALSE_IN_FINAL_BUILD(debugDrawDelaunay2D)) // Debug draw delaunay triangulation results.
	{
		const int debugDrawNavPolyId = -1;
		if (debugDrawNavPolyId < 0 || pPoly->GetId() == static_cast<ushort>(debugDrawNavPolyId))
		{
			for (U32F iTri = 0; iTri < numGenTriangles; ++iTri)
			{
				const TriangleIndices& tri = pTriangles[iTri];

				const Point v0 = Point(pVerts[tri.m_iA].x, 0.0f, pVerts[tri.m_iA].y);
				const Point v1 = Point(pVerts[tri.m_iB].x, 0.0f, pVerts[tri.m_iB].y);
				const Point v2 = Point(pVerts[tri.m_iC].x, 0.0f, pVerts[tri.m_iC].y);

				const Point v0Ws = pMesh->LocalToWorld(v0);
				const Point v1Ws = pMesh->LocalToWorld(v1);
				const Point v2Ws = pMesh->LocalToWorld(v2);

				const DebugPrimTime tt = kPrimDuration1Frame;

				g_prim.Draw(DebugString(AveragePos(v0Ws, v1Ws, v2Ws),
										StringBuilder<64>("%d", (int)iTri).c_str(),
										kColorOrange,
										0.75f),
							tt);

				g_prim.Draw(DebugLine(v0Ws, v1Ws, kColorOrange), tt);
				g_prim.Draw(DebugLine(v1Ws, v2Ws, kColorOrange), tt);
				g_prim.Draw(DebugLine(v2Ws, v0Ws, kColorOrange), tt);
			}
		}
	}

	bool* validTris			= NDI_NEW bool[numGenTriangles];
	const U32F numTriangles = EliminateZeroAreaTriangles(pTriangles, numGenTriangles, pVerts, validTris);

	PolyIndices* pPolyIndices = NDI_NEW PolyIndices[numTriangles];
	const U32F numPolys		  = TryMergeTriangles(pTriangles,
											  numGenTriangles,
											  validTris,
											  pVerts,
											  pSegments,
											  piOrgBoundaries,
											  segmentCount,
											  pPolyIndices,
											  maxTriangles,
											  TriangleMergeTestFunc,
											  pVertBlockage);

	NavPolyEx** ppNewExPolys = NDI_NEW NavPolyEx * [numPolys];
	// memset(ppNewExPolys, 0, sizeof(NavPolyEx*) * numPolys);

	// allocate ex polys
	{
		PROFILE_ACCUM(AllocateExPolys);

#if 1
		g_navExData.AllocateNavPolyExArray(pPoly, ppNewExPolys, numPolys);
#else
		for (U32F iPoly = 0; iPoly < numPolys; ++iPoly)
		{
			NavPolyEx* pNewPolyEx = g_navExData.AllocateNavPolyEx(pPoly);
			NAV_ASSERT(pNewPolyEx);
			ppNewExPolys[iPoly] = pNewPolyEx;
		}
#endif
	}

	// establish links to neighbors
	{
		for (U32F iPoly = 0; iPoly < numPolys; ++iPoly)
		{
			NavPolyEx* pNewPolyEx = ppNewExPolys[iPoly];
			if (!pNewPolyEx)
				continue;

			pNewPolyEx->m_blockerBits.SetAllBits();

			const PolyIndices& srcPoly = pPolyIndices[iPoly];
			for (U32F iV = 0; iV < srcPoly.m_numVerts; ++iV)
			{
				// set position info
				const I32F iVert	  = srcPoly.m_indices[iV];
				const Point testPosLs = Point(pVerts[iVert].x, 0.0f, pVerts[iVert].y);
				Vector normalLs		  = kUnitYAxis;

				pPoly->ProjectPointOntoSurfaceLs(&pNewPolyEx->m_vertsLs[iV], &normalLs, testPosLs);

				// set blocker info
				NavBlockerBits::BitwiseAnd(&pNewPolyEx->m_blockerBits, pNewPolyEx->m_blockerBits, pVertBlockage[iVert]);

				// setup neighbors
				const I32F neighborIndex = srcPoly.m_neighbors[iV];
				if ((neighborIndex >= 0) && (neighborIndex < numPolys))
				{
					if (ppNewExPolys[neighborIndex])
					{
						pNewPolyEx->m_adjPolys[iV] = pPoly->GetNavManagerId();
						pNewPolyEx->m_adjPolys[iV].m_iPolyEx = ppNewExPolys[neighborIndex]->GetId();
					}
					else
					{
						pNewPolyEx->m_adjPolys[iV].Invalidate();
					}
				}
				else if (iVert < polyVertCount)
				{
					const U32F adjId = pPoly->GetAdjacentId(navClipPolyLs.m_verts[iVert].m_originalEdge);
					if (adjId != NavPoly::kNoAdjacent)
					{
						const NavPoly* pAdjPoly		   = &pMesh->GetPoly(adjId);
						const NavPoly* pNeighborToLink = pAdjPoly->IsLink() ? pAdjPoly->GetLinkedPoly() : pAdjPoly;

						if (pNeighborToLink && pNeighborToLink->IsValid())
						{
							pNewPolyEx->m_adjPolys[iV] = pNeighborToLink->GetNavManagerId();
						}
					}
				}
			}

			pNewPolyEx->m_numVerts = srcPoly.m_numVerts;

			if (pNewPolyEx->m_numVerts == 3)
			{
				pNewPolyEx->m_vertsLs[3] = pNewPolyEx->m_vertsLs[2];
			}
		}
	}

#ifdef JBELLOMY
	{
		for (U32F iPoly = 0; iPoly < numPolys; ++iPoly)
		{
			const NavPolyEx* pPolyEx = ppNewExPolys[iPoly];
			if (!pPolyEx)
				continue;

			for (U32F iV = 0; iV < pPolyEx->GetVertexCount(); ++iV)
			{
				const Point v0 = pPolyEx->GetVertex(iV);
				const Point v1 = pPolyEx->GetNextVertex(iV);

				ASSERT(pPoly->PolyContainsPointLs(v0, 0.1f));
				ASSERT(pPoly->PolyContainsPointLs(v1, 0.1f));
				ASSERT(pPoly->PolyContainsPointLs(AveragePos(v0, v1), 0.1f));
			}
		}
	}
#endif

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ValidatePolyNeighbors(const NavPoly* pPoly, const NavMesh* pMesh)
{
	STRIP_IN_FINAL_BUILD;

	const NavPolyHandle hSrcPoly = NavPolyHandle(pPoly->GetNavManagerId());

	for (const NavPolyEx* pPolyEx = pPoly->GetNavPolyExList(); pPolyEx; pPolyEx = pPolyEx->GetNextPolyInList())
	{
		for (U32F iSrcEdge = 0; iSrcEdge < pPolyEx->GetVertexCount(); ++iSrcEdge)
		{
			const NavPolyHandle hAdjPoly = NavPolyHandle(pPolyEx->GetAdjacentPolyId(iSrcEdge));

			if (hSrcPoly == hAdjPoly)
				continue;

			const NavMesh* pAdjMesh = nullptr;
			const NavPoly* pAdjPoly = hAdjPoly.ToNavPoly(&pAdjMesh);

			const NavPolyEx* pAdjPolyEx = pAdjPoly ? pAdjPoly->GetNavPolyExList() : nullptr;
			if (!pAdjPolyEx)
				continue;

			bool found = false;

			for (; pAdjPolyEx; pAdjPolyEx = pAdjPolyEx->GetNextPolyInList())
			{
				for (U32F iEdge = 0; iEdge < pAdjPolyEx->GetVertexCount(); ++iEdge)
				{
					const NavPolyHandle hIncPoly = NavPolyHandle(pAdjPolyEx->GetAdjacentPolyId(iEdge));

					if (hIncPoly == hSrcPoly)
					{
						found = true;
						break;
					}
				}

			}

			if (!found)
			{
				g_ndConfig.m_pDMenuMgr->SetProgPause(true);
				g_ndConfig.m_pDMenuMgr->SetProgPauseLock(true);
				g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchInput = true;
				g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchProcessing = true;

				MsgErr("NavPoly %d [mesh: %s] has adjacent patched poly %d [mesh: %s] with no ex polys that connect back to it!",
					   pPoly->GetId(),
					   pMesh->GetName(),
					   pAdjPoly->GetId(),
					   pAdjMesh->GetName());

#if !FINAL_BUILD
				OnNavMeshPatchFailure(pMesh, FILE_LINE_FUNC);
#endif
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void FixupExNeighbors(NavPolyBlockerIntersections* const pIntersections)
{
	PROFILE_AUTO(Navigation);

	const U32F numNavPolys = pIntersections->Size();
	for (U32F iNavPoly = 0; iNavPoly < numNavPolys; iNavPoly++)
	{
		const NavPoly* const pPoly = pIntersections->m_pNavPolys[iNavPoly];
		if (!pPoly->GetNavPolyExList())
		{
			continue;
		}

		if (!pPoly->IsValid())
		{
			continue;
		}

		const NavMesh* pMesh = pPoly->GetNavMesh();

		for (U32F iV = 0; iV < pPoly->GetVertexCount(); ++iV)
		{
			const U32F adjId = pPoly->GetAdjacentId(iV);
			if (adjId == NavPoly::kNoAdjacent)
				continue;

			const NavPoly* pNeighborPoly = &pMesh->GetPoly(adjId);
			const NavPoly* pPolyToLink	 = pNeighborPoly->IsLink() ? pNeighborPoly->GetLinkedPoly() : pNeighborPoly;

			if (nullptr == pPolyToLink)
				continue;

			if (nullptr == pPolyToLink->GetNavPolyExList())
				continue;

			if (!pPolyToLink->IsValid())
				continue;

			FixupExNeighborsBetweenPolys(pPoly, pPolyToLink);
		}
	}

	if (FALSE_IN_FINAL_BUILD(true))
	{
		for (U32F iNavPoly = 0; iNavPoly < numNavPolys; iNavPoly++)
		{
			const NavPoly* pPoly = pIntersections->m_pNavPolys[iNavPoly];
			const NavMesh* pMesh = pPoly->GetNavMesh();

			ValidatePolyNeighbors(pPoly, pMesh);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void GenerateExPathNodes(const NavPolyBlockerIntersections* const pIntersections,
								const NavPolyBlockerIslands* const pIslands)
{
	PROFILE_AUTO(Navigation);
	// PROFILE_ACCUM(GenerateExPathNodes);

	for (U32F i = 0; i < pIslands->m_numIslands; i++)
	{
		const NavPolyBlockerIslands::Island& island = pIslands->m_pIslands[i];

		for (U32F navPolyLocalIdx = 0; navPolyLocalIdx < island.m_numNavPolys; navPolyLocalIdx++)
		{
			const U32F navPolyGlobalIdx	  = island.m_navPolys[navPolyLocalIdx];
			const NavPoly* const pNavPoly = pIntersections->m_pNavPolys[navPolyGlobalIdx];
			g_navPathNodeMgr.AddExNodesFromNavPoly(pNavPoly);
		}
	}

	for (U32F i = 0; i < pIslands->m_numIslands; i++)
	{
		const NavPolyBlockerIslands::Island& island = pIslands->m_pIslands[i];

		for (U32F navPolyLocalIdx = 0; navPolyLocalIdx < island.m_numNavPolys; navPolyLocalIdx++)
		{
			const U32F navPolyGlobalIdx	  = island.m_navPolys[navPolyLocalIdx];
			const NavPoly* const pNavPoly = pIntersections->m_pNavPolys[navPolyGlobalIdx];
			g_navPathNodeMgr.AddExLinksFromNavPoly(pNavPoly);
		}
	}

	g_navPathNodeMgr.Validate();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void GenerateExPathNodes(NavPolyBlockerIntersections* pIntersections)
{
	PROFILE_AUTO(Navigation);
	// PROFILE_ACCUM(GenerateExPathNodes);

	const size_t count = pIntersections->Size();
	for (U32F i = 0; i < count; ++i)
	{
		const NavPoly* const pPoly = pIntersections->m_pNavPolys[i];
		g_navPathNodeMgr.AddExNodesFromNavPoly(pPoly);
	}

	for (U32F i = 0; i < count; ++i)
	{
		const NavPoly* const pPoly = pIntersections->m_pNavPolys[i];
		g_navPathNodeMgr.AddExLinksFromNavPoly(pPoly);
	}

	g_navPathNodeMgr.Validate();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void FlagExternalBlockerVerts(const NavMesh* pMesh,
									 const NavPoly* pPoly,
									 NavBlockerClipperEntry* pBlockerEntriesLs,
									 U32F numEntries)
{
	for (U32F iEntry = 0; iEntry < numEntries; ++iEntry)
	{
		Clip2D::Poly& blockerPoly = pBlockerEntriesLs[iEntry].m_clipPoly;

		for (U32F iV = 0; iV < blockerPoly.m_numVerts; ++iV)
		{
			Clip2D::Vertex& v = blockerPoly.m_verts[iV];
			if (v.m_state == Clip2D::VertexState::kOutside)
			{
				continue;
			}

			if (!pPoly->PolyContainsPointLs(v.m_pos, 0.01f))
			{
				v.m_state = Clip2D::VertexState::kOutside;
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
typedef HashTable<FeatureKey, FeatureKey, false> SharedEdgeTable;

/// --------------------------------------------------------------------------------------------------------------- ///
struct SegmentNeighbor
{
	NavManagerId m_id;
	U32F m_vIdx;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct SegmentNeighbors
{
	SegmentNeighbor m_neighbor0;
	SegmentNeighbor m_neighbor1;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct SegmentPoint
{
	U32F m_idx;
	float m_t;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class SegmentPoints
{
public:
	void AddPoint(const SegmentPoint& intersection)
	{
		NAV_ASSERT(m_pts);
		NAV_ASSERT(m_count < kMaxPoints);

		for (U32F i = 0; i < m_count; i++)
		{
			const SegmentPoint& pt = m_pts[i];
			if (pt.m_idx == intersection.m_idx)
			{
				return;
			}
		}

		m_pts[m_count++] = intersection; // Add intersection

		for (U32F curr = m_count - 1; curr > 0; curr--) // Keep it sorted
		{
			const U32F prev = curr - 1;
			if (m_pts[curr].m_t < m_pts[prev].m_t)
			{
				SegmentPoint temp = m_pts[prev];
				m_pts[prev]		  = m_pts[curr];
				m_pts[curr]		  = temp;
			}
		}
	}

public:
	static const U32F kMaxPoints = 32; // Should be enough
	SegmentPoint m_pts[kMaxPoints];
	U32F m_count;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static void IntersectSegSegXz(const U32F s0Idx,
							  const U32F s1Idx,
							  const U32F vLocalIdx00,
							  const U32F vLocalIdx01,
							  U32F vLocalIdx10,
							  U32F vLocalIdx11,
							  IndexMapTable& vertGlobalToLocal,
							  IndexMapTable& vertLocalToGlobal,
							  const ExternalBitArray& s0Owners,
							  const ExternalBitArray& s1Owners,
							  const U32F numOrgVerts,
							  Point* const pOutGVP,
							  ExternalBitArray* const pOutGVO,
							  U32F& outIntersectionWriteIdx,
							  U32F& outVIntersectionGlobalIdx,
							  ExternalBitArray* const pOutVertsTags,
							  I32F* const pOutVertsMergeTable,
							  SegmentPoints* const pOutSegmentTable)
{
	PROFILE(Navigation, IntersectSegSegXz);

	static const Scalar kParallelEpsilon = SCALAR_LC(0.0001f);
	static const Scalar zero = kZero;
	static const Scalar kOne = SCALAR_LC(1.0f);

	const U32F vGlobalIdx00 = vertLocalToGlobal.Find(vLocalIdx00);
	const U32F vGlobalIdx01 = vertLocalToGlobal.Find(vLocalIdx01);
	U32F vGlobalIdx10		= vertLocalToGlobal.Find(vLocalIdx10);
	U32F vGlobalIdx11		= vertLocalToGlobal.Find(vLocalIdx11);

	const Point& s00 = pOutGVP[vGlobalIdx00];
	const Point& s01 = pOutGVP[vGlobalIdx01];
	Point s10		 = pOutGVP[vGlobalIdx10];
	Point s11		 = pOutGVP[vGlobalIdx11];

	const Vector u = s01 - s00;
	Vector v	   = s11 - s10;

	const ExternalBitArray& s00Tags = pOutVertsTags[vLocalIdx00];
	const ExternalBitArray& s01Tags = pOutVertsTags[vLocalIdx01];
	const ExternalBitArray& s10Tags = pOutVertsTags[vLocalIdx10];
	const ExternalBitArray& s11Tags = pOutVertsTags[vLocalIdx11];

	const bool s00OnS1 = s00Tags.IsBitSet(s1Idx); // s00 on s1
	const bool s01OnS1 = s01Tags.IsBitSet(s1Idx); // s01 on s1
	bool s10OnS0	   = s10Tags.IsBitSet(s0Idx); // s10 on s0
	bool s11OnS0	   = s11Tags.IsBitSet(s0Idx); // s11 on s0

	SegmentPoints& seg0 = pOutSegmentTable[s0Idx];
	SegmentPoints& seg1 = pOutSegmentTable[s1Idx];

	const bool intersectAtEndPts = s10OnS0 || s11OnS0 || s00OnS1 || s01OnS1;
	bool oppoDir = false;

	const Scalar crossXz = u.X() * v.Z() - u.Z() * v.X();
	if (Abs(crossXz) < kParallelEpsilon) // Parallel
	{
		if (!intersectAtEndPts)
		{
			return; // Not overlapping
		}

		oppoDir = Dot(u, v) < zero;
		if (oppoDir) // Opposite direction, swap verts of s1
		{
			U32F temp	= vLocalIdx10;
			vLocalIdx10 = vLocalIdx11;
			vLocalIdx11 = temp;

			temp		 = vGlobalIdx10;
			vGlobalIdx10 = vGlobalIdx11;
			vGlobalIdx11 = temp;

			s10 = pOutGVP[vGlobalIdx10];
			s11 = pOutGVP[vGlobalIdx11];

			s10OnS0 = s11Tags.IsBitSet(s0Idx); // s10 on s0
			s11OnS0 = s10Tags.IsBitSet(s0Idx); // s11 on s0

			v = -v;
		}
	}

	if (intersectAtEndPts)
	{
		const bool s00CutS1 = vLocalIdx00 != vLocalIdx10 && vLocalIdx00 != vLocalIdx11 && s00OnS1;
		const bool s01CutS1 = vLocalIdx01 != vLocalIdx10 && vLocalIdx01 != vLocalIdx11 && s01OnS1;
		const bool s10CutS0 = vLocalIdx10 != vLocalIdx00 && vLocalIdx10 != vLocalIdx01 && s10OnS0;
		const bool s11CutS0 = vLocalIdx11 != vLocalIdx00 && vLocalIdx11 != vLocalIdx01 && s11OnS0;

		// ---------------------------------------------------------------------------------
		// NOTE: when adding points below, should we ALSO TRY to merge along s0 and s1?
		// ---------------------------------------------------------------------------------
		if (s00CutS1 || s01CutS1)
		{
			const Scalar vv = Dot(v, v);

			if (s00CutS1)
			{
				const Vector c	= s00 - s10;
				const Scalar d	= Dot(c, v);
				const Scalar t1 = AccurateDiv(d, vv);

				if (oppoDir)
				{
					seg1.AddPoint({ vLocalIdx00, kOne - t1 });
				}
				else
				{
					seg1.AddPoint({ vLocalIdx00, t1 });
				}
			}

			if (s01CutS1)
			{
				const Vector c	= s01 - s10;
				const Scalar d	= Dot(c, v);
				const Scalar t1 = AccurateDiv(d, vv);

				if (oppoDir)
				{
					seg1.AddPoint({ vLocalIdx01, kOne - t1 });
				}
				else
				{
					seg1.AddPoint({ vLocalIdx01, t1 });
				}
			}
		}

		if (s10CutS0 || s11CutS0)
		{
			const Scalar uu = Dot(u, u);

			if (s10CutS0)
			{
				const Vector c	= s10 - s00;
				const Scalar d	= Dot(c, u);
				const Scalar t0 = AccurateDiv(d, uu);

				seg0.AddPoint({ vLocalIdx10, t0 });
			}

			if (s11CutS0)
			{
				const Vector c	= s11 - s00;
				const Scalar d	= Dot(c, u);
				const Scalar t0 = AccurateDiv(d, uu);

				seg0.AddPoint({ vLocalIdx11, t0 });
			}
		}

		return;
	}

	const Scalar denom = AccurateDiv(kOne, crossXz);
	const Vector w	   = s00 - s10;

	const Scalar s	   = v.X() * w.Z() - v.Z() * w.X();
	const Scalar t0 = s * denom;
	if (t0 < zero || t0 > kOne)
	{
		return;
	}

	const Scalar t = u.X() * w.Z() - u.Z() * w.X();
	const Scalar t1 = t * denom;
	if (t1 < zero || t1 > kOne)
	{
		return;
	}

	const Point vMs = Lerp(s00, s01, t0);

	// Merge along s0 if possible
	for (I32F i = 1; i < seg0.m_count - 1; i++)
	{
		const SegmentPoint& pt = seg0.m_pts[i];

		const U32F vLocalIdx0  = pt.m_idx;
		const U32F vGlobalIdx0 = vertLocalToGlobal.Find(vLocalIdx0);

		const Point& v0Ms = pOutGVP[vGlobalIdx0];

		if (DistXzSqr(v0Ms, vMs) < kIntersectionSnapEpsilonSqr)
		{
			pOutVertsTags[vLocalIdx0].SetBit(s1Idx);
			ExternalBitArray::BitwiseOr(&pOutGVO[vLocalIdx0], pOutGVO[vLocalIdx0], s1Owners);

			seg1.AddPoint({ vLocalIdx0, t1 });

			return;
		}
	}

	// Merge along s1 if possible
	for (I32F i = 1; i < seg1.m_count - 1; i++)
	{
		const SegmentPoint& pt = seg1.m_pts[i];

		const U32F vLocalIdx1  = pt.m_idx;
		const U32F vGlobalIdx1 = vertLocalToGlobal.Find(vLocalIdx1);

		const Point& v1Ms = pOutGVP[vGlobalIdx1];

		if (DistXzSqr(v1Ms, vMs) < kIntersectionSnapEpsilonSqr)
		{
			pOutVertsTags[vLocalIdx1].SetBit(s0Idx);
			ExternalBitArray::BitwiseOr(&pOutGVO[vLocalIdx1], pOutGVO[vLocalIdx1], s0Owners);

			seg0.AddPoint({ vLocalIdx1, t0 });

			return;
		}
	}

	pOutGVP[outVIntersectionGlobalIdx] = vMs;

	const U32F vLocalIdx = numOrgVerts + outIntersectionWriteIdx;

	vertGlobalToLocal.Add(outVIntersectionGlobalIdx, vLocalIdx);
	vertLocalToGlobal.Add(vLocalIdx, outVIntersectionGlobalIdx);

	pOutVertsTags[vLocalIdx].SetBit(s0Idx);
	pOutVertsTags[vLocalIdx].SetBit(s1Idx);

	ExternalBitArray::BitwiseOr(&pOutGVO[outVIntersectionGlobalIdx], s0Owners, s1Owners);

	seg0.AddPoint({ vLocalIdx, t0 });
	seg1.AddPoint({ vLocalIdx, t1 });

	pOutVertsMergeTable[vLocalIdx] = vLocalIdx;

	outIntersectionWriteIdx++;
	outVIntersectionGlobalIdx++;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool CanSnapToSegment2(const Point& p,
							const Point& a,
							const Point& b, 
							const float snapDistSqr)
{
	const Vector ab = VectorXz(b - a);
	const Vector ap = VectorXz(p - a);
	const Scalar lenSqr = LengthSqr(ab);

	Scalar distSqr;

	if (lenSqr < NDI_FLT_EPSILON)
	{
		distSqr = LengthSqr(ap);
	}
	else
	{
		const Scalar t = Limit01(AccurateDiv(Dot(ab, ap), lenSqr));
		const Vector v = ap - t * ab;
		
		distSqr = LengthXzSqr(v);
	}

	return distSqr <= snapDistSqr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool CanSnapToSegment(const Point& p,
							 const Point& a,
							 const Point& b,
							 const float snapDistSqr,
							 float& outDistSqr,
							 Vector& outAb,
							 float& outAbLenSqr,
							 float& outT)
{
	outAb = VectorXz(b - a);
	const Vector ap = VectorXz(p - a);

	const Scalar lenSqr = LengthSqr(outAb);
	if (lenSqr <= snapDistSqr)
	{
		outDistSqr = LengthSqr(ap);
		outAbLenSqr = 0.0f;
		outT = SCALAR_LC(0.0f);
	}
	else
	{
		const Scalar t = Limit01(AccurateDiv(Dot(outAb, ap), lenSqr));
		const Vector v = ap - t * outAb;
		
		outDistSqr = LengthXzSqr(v);
		outAbLenSqr = (float)lenSqr;
		outT = t;
	}

	return outDistSqr <= snapDistSqr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool CanIntersect(const ExternalBitArray& owners0,
						 const ExternalBitArray& owners1,
						 const ExternalBitArray& canIntersect)
{
	const U64 numOwners = owners0.GetMaxBitCount();
	NAV_ASSERT(numOwners == owners1.GetMaxBitCount());

	for (U64 owner0 = owners0.FindFirstSetBit(); owner0 < numOwners; owner0 = owners0.FindNextSetBit(owner0))
	{
		for (U64 owner1 = owners1.FindFirstSetBit(); owner1 < numOwners; owner1 = owners1.FindNextSetBit(owner1))
		{
			const U64 intersectBit = owner0 * numOwners + owner1;
			if (canIntersect.IsBitSet(intersectBit))
			{
				return true;
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void OwnersToStr(const NavPolyBlockerIslands::Island* const pIsland,
						const U32F numNavBlockers,
						const ExternalBitArray& owners,
						char ownersStr[])
{
	const U32F numNavPolys = pIsland->m_numNavPolys;

	U32F writeIdx = 0;
	for (U32F i = 0; i < numNavPolys; i++)
	{
		if (owners.IsBitSet(i))
		{
			ownersStr[writeIdx++] = '1';
		}
		else
		{
			ownersStr[writeIdx++] = '0';
		}
	}
	ownersStr[writeIdx++] = ',';

	for (U32F i = numNavPolys; i < numNavPolys + numNavBlockers; i++)
	{
		if (owners.IsBitSet(i))
		{
			ownersStr[writeIdx++] = '1';
		}
		else
		{
			ownersStr[writeIdx++] = '0';
		}
	}
	ownersStr[writeIdx++] = 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void IntersectSegments(const NavPolyBlockerIslands::Island* const pIsland,
							  const U32F numNavBlockers,
							  const NavMesh* const pNavMeshMother,
							  const ExternalBitArray& canIntersect,
							  Point* const pOutGVP,
							  ExternalBitArray* const pOutGVO,
							  const U32F numSubjectVerts,
							  const U32F numClipperVerts,
							  const U32* const pSubjectSegs,
							  const U32F numSubjectSegs,
							  const U32* const pClipperSegs,
							  const U32F numClipperSegs,
							  U32F& outVIntersectionGlobalIdx,
							  U32F& outNumVerts,
							  U32** pOutSegs,
							  U32F& outNumSegs,
							  U32F& outInvalidOrgBoundaryKey,
							  I16** pOutOrgBoundaryTable,
							  const I32F intersectOrder)	// For debug drawing
{
	PROFILE(Navigation, IntersectSegments);

	PrimServerWrapper ps(pNavMeshMother->GetParentSpace());

	// ---------------------------------------------------------------------------------
	// NOTE: can this stomp past the memory of pGVP and pGVO? Better assert here.
	// ---------------------------------------------------------------------------------
	const U32F numOrgVerts = numSubjectVerts + numClipperVerts;
	const U32F numMaxVerts = numOrgVerts + kMaxNumIntersections;

	// Map verts and segments between global and local
	U32F idx0;
	U32F idx1;
	U32F vLocalIdx0;
	U32F vLocalIdx1;
	U32F vGlobalIdx0;
	U32F vGlobalIdx1;
	U32F vMergedIdx0;
	U32F vMergedIdx1;

	AllocateJanitor kk(kAllocSingleGameFrame, FILE_LINE_FUNC);

	IndexMapTable vertGlobalToLocal;
	IndexMapTable vertLocalToGlobal;

	vertGlobalToLocal.Init(numMaxVerts);
	vertLocalToGlobal.Init(numMaxVerts);

	vertGlobalToLocal.Clear();
	vertLocalToGlobal.Clear();

	// Unique subject segments + unique clipper segments
	const U32F numOrgSegs = numSubjectSegs + numClipperSegs;
	U32F* const pOrgSegs  = NDI_NEW U32F[2 * numOrgSegs];

	const U32F numOwnerBits	  = pOutGVO[0].GetMaxBitCount();
	const U32F numOwnerBlocks = ExternalBitArray::DetermineNumBlocks(numOwnerBits);

	U64* const pSegOwnerBlocks = NDI_NEW U64[2 * numOwnerBlocks];
	ExternalBitArray dummyOwners0;
	ExternalBitArray dummyOwners1;
	dummyOwners0.Init(numOwnerBits, &pSegOwnerBlocks[0]);
	dummyOwners1.Init(numOwnerBits, &pSegOwnerBlocks[numOwnerBlocks]);

	// Verts tags
	const U32F numVertTagBlocks = ExternalBitArray::DetermineNumBlocks(numOrgSegs);
	ExternalBitArray* const pVertsTags = NDI_NEW ExternalBitArray[numMaxVerts + 1];
	U64* const pVertTagBlocks = NDI_NEW U64[numVertTagBlocks * (numMaxVerts + 1)];
	for (vLocalIdx0 = 0; vLocalIdx0 <= numMaxVerts; vLocalIdx0++)
	{
		ExternalBitArray& vertTags = pVertsTags[vLocalIdx0];

		const U32F blockIdx = vLocalIdx0 * numVertTagBlocks;
		vertTags.Init(numOrgSegs, &pVertTagBlocks[blockIdx]);
		vertTags.ClearAllBits();
	}

	U32F vertWriteIdx = 0;
	U32F segWriteIdx  = 0;
	for (U32F iSegSubject = 0; iSegSubject < numSubjectSegs; iSegSubject++)
	{
		idx0 = iSegSubject * 2;
		idx1 = idx0 + 1;

		vGlobalIdx0 = pSubjectSegs[idx0];
		vGlobalIdx1 = pSubjectSegs[idx1];

		const U32F index0 = vertGlobalToLocal.Find(vGlobalIdx0);
		if (index0 == IndexMapTable::kInvalidIndex)
		{
			vLocalIdx0 = vertWriteIdx++;
			vertGlobalToLocal.Add(vGlobalIdx0, vLocalIdx0);
			vertLocalToGlobal.Add(vLocalIdx0, vGlobalIdx0);
		}
		else
		{
			vLocalIdx0 = index0;
		}

		const U32F index1 = vertGlobalToLocal.Find(vGlobalIdx1);
		if (index1 == IndexMapTable::kInvalidIndex)
		{
			vLocalIdx1 = vertWriteIdx++;
			vertGlobalToLocal.Add(vGlobalIdx1, vLocalIdx1);
			vertLocalToGlobal.Add(vLocalIdx1, vGlobalIdx1);
		}
		else
		{
			vLocalIdx1 = index1;
		}

		idx0 = segWriteIdx * 2;
		idx1 = idx0 + 1;
		pOrgSegs[idx0] = vLocalIdx0;
		pOrgSegs[idx1] = vLocalIdx1;

		segWriteIdx++;

		pVertsTags[vLocalIdx0].SetBit(iSegSubject);
		pVertsTags[vLocalIdx1].SetBit(iSegSubject);
	}

	for (U32F iSegClipper = 0; iSegClipper < numClipperSegs; iSegClipper++)
	{
		idx0 = iSegClipper * 2;
		idx1 = idx0 + 1;

		vGlobalIdx0 = pClipperSegs[idx0];
		vGlobalIdx1 = pClipperSegs[idx1];

		const U32F index0 = vertGlobalToLocal.Find(vGlobalIdx0);
		if (index0 == IndexMapTable::kInvalidIndex)
		{
			vLocalIdx0 = vertWriteIdx++;
			vertGlobalToLocal.Add(vGlobalIdx0, vLocalIdx0);
			vertLocalToGlobal.Add(vLocalIdx0, vGlobalIdx0);
		}
		else
		{
			vLocalIdx0 = index0;
		}

		const U32F index1 = vertGlobalToLocal.Find(vGlobalIdx1);
		if (index1 == IndexMapTable::kInvalidIndex)
		{
			vLocalIdx1 = vertWriteIdx++;
			vertGlobalToLocal.Add(vGlobalIdx1, vLocalIdx1);
			vertLocalToGlobal.Add(vLocalIdx1, vGlobalIdx1);
		}
		else
		{
			vLocalIdx1 = index1;
		}

		idx0 = segWriteIdx * 2;
		idx1 = idx0 + 1;
		pOrgSegs[idx0] = vLocalIdx0;
		pOrgSegs[idx1] = vLocalIdx1;

		segWriteIdx++;

		pVertsTags[vLocalIdx0].SetBit(numSubjectSegs + iSegClipper);
		pVertsTags[vLocalIdx1].SetBit(numSubjectSegs + iSegClipper);
	}

	NAV_ASSERT(segWriteIdx == numOrgSegs);

	// Vert merge information
	I32F *const pVertsMergeTable = NDI_NEW I32F[numMaxVerts];
	for (vLocalIdx0 = 0; vLocalIdx0 < numMaxVerts; vLocalIdx0++)
	{
		if (vLocalIdx0 < numSubjectVerts)
		{
			pVertsMergeTable[vLocalIdx0] = vLocalIdx0; // There is no need to merge verts of the subject
		}
		else
		{
			pVertsMergeTable[vLocalIdx0] = -1; // Unprocessed
		}
	}

	const float snapDistSqr = Sqr(g_navOptions.m_navMeshPatch.m_navMeshPatchSnapDist);
	
	U32F intersectionWriteIdx = 0;
	{
		PROFILE(Navigation, SnapClipperVerts);
		
		// Snap clipper verts to subject verts/segments
		for (U32F vLocalIdx01 = numSubjectVerts; vLocalIdx01 < numOrgVerts; vLocalIdx01++)
		{
			NAV_ASSERT(pVertsMergeTable[vLocalIdx01] == -1);

			const U32F vGlobalIdx01 = vertLocalToGlobal.Find(vLocalIdx01);

			const Point& v01Ms = pOutGVP[vGlobalIdx01];
			const ExternalBitArray& vertOwners01 = pOutGVO[vGlobalIdx01];

			// Find the closest snap point on subject segments
			U32F iSnapSeg = numSubjectSegs;
			
			float closestDistSqr = FLT_MAX;
			Vector closestSnapAb;
			float closestSnapAbLenSqr = FLT_MAX;
			float closestSnapT = FLT_MAX;

			for (U32F iSeg = 0; iSeg < numSubjectSegs; iSeg++)
			{
				idx0 = iSeg * 2;
				idx1 = idx0 + 1;

				const U32F vLocalIdx10 = pOrgSegs[idx0];
				const U32F vLocalIdx11 = pOrgSegs[idx1];

				const U32F vGlobalIdx10 = vertLocalToGlobal.Find(vLocalIdx10);
				const U32F vGlobalIdx11 = vertLocalToGlobal.Find(vLocalIdx11);

				ExternalBitArray::BitwiseAnd(&dummyOwners0, pOutGVO[vGlobalIdx10], pOutGVO[vGlobalIdx11]);
				if (!CanIntersect(vertOwners01, dummyOwners0, canIntersect))
				{
					//MsgCon("SnapClipperVerts: skipped %d -> [%d, %d]\n", vLocalIdx01, vLocalIdx10, vLocalIdx11);
					continue;
				}

				//MsgCon("SnapClipperVerts:     ran %d -> [%d, %d]\n", vLocalIdx01, vLocalIdx10, vLocalIdx11);

				const Point& v10Ms = pOutGVP[vGlobalIdx10];
				const Point& v11Ms = pOutGVP[vGlobalIdx11];

				float distSqr;
				Vector ab;
				float abLenSqr;
				float t;
				if (CanSnapToSegment(v01Ms, v10Ms, v11Ms, snapDistSqr, distSqr, ab, abLenSqr, t) &&
					distSqr < closestDistSqr)
				{
					iSnapSeg = iSeg;

					closestDistSqr = distSqr;
					closestSnapAb = ab;
					closestSnapAbLenSqr = abLenSqr;
					closestSnapT = t;

					ExternalBitArray::Copy(&dummyOwners1, dummyOwners0);
				}
			}

			if (iSnapSeg < numSubjectSegs)
			{
				idx0 = iSnapSeg * 2;
				idx1 = idx0 + 1;

				float kSnapTEpsilon;
				if (closestSnapAbLenSqr <= snapDistSqr)
				{
					kSnapTEpsilon = 0.0002f;
				}
				else
				{
					const float abLen = sqrt(closestSnapAbLenSqr);
					kSnapTEpsilon = g_navOptions.m_navMeshPatch.m_navMeshPatchSnapDist / abLen;
				}

				if (Abs(closestSnapT) < kSnapTEpsilon) // Snap to segment vert 10
				{
					const U32F vLocalIdx10 = pOrgSegs[idx0];
					const U32F vGlobalIdx10 = vertLocalToGlobal.Find(vLocalIdx10);

					ExternalBitArray::BitwiseOr(&pVertsTags[vLocalIdx10], pVertsTags[vLocalIdx01], pVertsTags[vLocalIdx10]);
					ExternalBitArray::BitwiseOr(&pOutGVO[vGlobalIdx10], pOutGVO[vGlobalIdx01], pOutGVO[vGlobalIdx10]);

					pVertsMergeTable[vLocalIdx01] = vLocalIdx10;
				}
				else if (abs(closestSnapT - 1.0f) < kSnapTEpsilon)	// Snap to segment vert 11
				{
					const U32F vLocalIdx11 = pOrgSegs[idx1];
					const U32F vGlobalIdx11 = vertLocalToGlobal.Find(vLocalIdx11);

					ExternalBitArray::BitwiseOr(&pVertsTags[vLocalIdx11], pVertsTags[vLocalIdx01], pVertsTags[vLocalIdx11]);
					ExternalBitArray::BitwiseOr(&pOutGVO[vGlobalIdx11], pOutGVO[vGlobalIdx01], pOutGVO[vGlobalIdx11]);

					pVertsMergeTable[vLocalIdx01] = vLocalIdx11;
				}
				else	// Snap to the middle of segment
				{
					const U32F vLocalIdx10 = pOrgSegs[idx0];
					const U32F vGlobalIdx10 = vertLocalToGlobal.Find(vLocalIdx10);
					
					const Point& a = pOutGVP[vGlobalIdx10];
					const Point closestSnapMs = a + (closestSnapT * closestSnapAb);

					// Multiple clipper verts could snap to the same point.
					const U32F newVSnapLocalIdx = numOrgVerts + intersectionWriteIdx;
					U32F vLocalIdx00 = newVSnapLocalIdx;
					U32F vGlobalIdx00 = outVIntersectionGlobalIdx;

					for (U32F vSnapLocalIdx = numOrgVerts; vSnapLocalIdx < newVSnapLocalIdx; vSnapLocalIdx++)
					{
						if (pVertsTags[vSnapLocalIdx].IsBitSet(iSnapSeg))
						{
							const U32F vSnapGlobalIdx = vertLocalToGlobal.Find(vSnapLocalIdx);
							const Point& vSnapMs = pOutGVP[vSnapGlobalIdx];
							if (DistXzSqr(closestSnapMs, vSnapMs) < snapDistSqr)
							{
								vLocalIdx00 = vSnapLocalIdx;
								vGlobalIdx00 = vSnapGlobalIdx;
								break;
							}
						}
					}

					if (vLocalIdx00 < newVSnapLocalIdx)
					{
						ExternalBitArray::BitwiseOr(&pVertsTags[vLocalIdx00], pVertsTags[vLocalIdx00], pVertsTags[vLocalIdx01]);
						ExternalBitArray::BitwiseOr(&pOutGVO[vGlobalIdx00], pOutGVO[vGlobalIdx00], vertOwners01);

						NAV_ASSERT(pVertsMergeTable[vLocalIdx00] == vLocalIdx00);
						pVertsMergeTable[vLocalIdx01] = vLocalIdx00;
					}
					else
					{
						pOutGVP[vGlobalIdx00] = closestSnapMs;

						ExternalBitArray::BitwiseOr(&pOutGVO[vGlobalIdx00], dummyOwners1, pOutGVO[vGlobalIdx01]);

						pVertsTags[vLocalIdx00].SetBit(iSnapSeg);
						ExternalBitArray::BitwiseOr(&pVertsTags[vLocalIdx00], pVertsTags[vLocalIdx00], pVertsTags[vLocalIdx01]);

						vertGlobalToLocal.Add(vGlobalIdx00, vLocalIdx00);
						vertLocalToGlobal.Add(vLocalIdx00, vGlobalIdx00);
						pVertsMergeTable[vLocalIdx00] = vLocalIdx00;

						pVertsMergeTable[vLocalIdx01] = vLocalIdx00;

						intersectionWriteIdx++;
						outVIntersectionGlobalIdx++;
					}
				}
			}
			else	// Doesnt snap to any subject vert/segment
			{
				pVertsMergeTable[vLocalIdx01] = vLocalIdx01;
			}
		}
	}

	{
		PROFILE(Navigation, TagSubjectVerts);

		// Tag subject verts against clipper segments
		for (U32F vLocalIdx = 0; vLocalIdx < numSubjectVerts; vLocalIdx++)
		{
			NAV_ASSERT(pVertsMergeTable[vLocalIdx] == vLocalIdx);

			const U32F vGlobalIdx = vertLocalToGlobal.Find(vLocalIdx);

			const Point& vMs = pOutGVP[vGlobalIdx];
			ExternalBitArray& vertOwners = pOutGVO[vGlobalIdx];
			ExternalBitArray& vertTags = pVertsTags[vLocalIdx];

			for (U32F iSeg = numSubjectSegs; iSeg < numOrgSegs; iSeg++)
			{
				idx0 = iSeg * 2;
				idx1 = idx0 + 1;

				vLocalIdx0 = pOrgSegs[idx0];
				vLocalIdx1 = pOrgSegs[idx1];

				vMergedIdx0 = pVertsMergeTable[vLocalIdx0];
				vMergedIdx1 = pVertsMergeTable[vLocalIdx1];

				vGlobalIdx0 = vertLocalToGlobal.Find(vMergedIdx0);
				vGlobalIdx1 = vertLocalToGlobal.Find(vMergedIdx1);

				ExternalBitArray::BitwiseAnd(&dummyOwners0, pOutGVO[vGlobalIdx0], pOutGVO[vGlobalIdx1]);
				if (!CanIntersect(vertOwners, dummyOwners0, canIntersect))
				{
					//MsgCon("TagSubjectVerts: skipped %d -> [%d, %d]\n", vLocalIdx, vLocalIdx0, vLocalIdx1);
					continue;
				}

				if (vertTags.IsBitSet(iSeg)) // Already tagged
				{
					continue;
				}

				//MsgCon("TagSubjectVerts:     ran %d -> [%d, %d]\n", vLocalIdx, vLocalIdx0, vLocalIdx1);

				const Point& v0Ms = pOutGVP[vGlobalIdx0];

				bool tagToClipperSeg = false;
				if (vMergedIdx0 == vMergedIdx1) // Zero length clipper segment after merge/snap
				{
					tagToClipperSeg = DistXzSqr(vMs, v0Ms) < snapDistSqr;
				}
				else
				{
					const Point& v1Ms = pOutGVP[vGlobalIdx1];
					tagToClipperSeg = CanSnapToSegment2(vMs, v0Ms, v1Ms, snapDistSqr);
				}

				if (tagToClipperSeg)
				{
					ExternalBitArray::BitwiseOr(&vertOwners, vertOwners, dummyOwners0);
					vertTags.SetBit(iSeg);
				}
			}
		}
	}

	SegmentPoints* const pSegmentTable = NDI_NEW SegmentPoints[numOrgSegs];
	{
		PROFILE(Navigation, BuildSegmentTable);

		// Build the segment table
		for (U32F iSeg = 0; iSeg < numOrgSegs; iSeg++)
		{
			SegmentPoints& seg = pSegmentTable[iSeg];
			seg.m_count = 0;

			idx0 = iSeg * 2;
			idx1 = idx0 + 1;

			vLocalIdx0 = pOrgSegs[idx0];
			vLocalIdx1 = pOrgSegs[idx1];

			seg.AddPoint({ vLocalIdx0, 0.0f });
			seg.AddPoint({ vLocalIdx1, 1.0f });
		}

		// Intersect subject segments and clipper segments
		for (U32F iSeg0 = 0; iSeg0 < numSubjectSegs; iSeg0++)
		{
			const U32F idx00 = iSeg0 * 2;
			const U32F idx01 = idx00 + 1;

			const U32F vLocalIdx00 = pOrgSegs[idx00];
			const U32F vLocalIdx01 = pOrgSegs[idx01];

			NAV_ASSERT(pVertsMergeTable[vLocalIdx00] == vLocalIdx00); // We never merge/snap subject verts
			NAV_ASSERT(pVertsMergeTable[vLocalIdx01] == vLocalIdx01);

			const U32F vGlobalIdx00 = vertLocalToGlobal.Find(vLocalIdx00);
			const U32F vGlobalIdx01 = vertLocalToGlobal.Find(vLocalIdx01);

			ExternalBitArray::BitwiseAnd(&dummyOwners0, pOutGVO[vGlobalIdx00], pOutGVO[vGlobalIdx01]);

			for (U32F iSeg1 = numSubjectSegs; iSeg1 < numOrgSegs; iSeg1++)
			{
				const U32 idx10 = iSeg1 * 2;
				const U32 idx11 = idx10 + 1;

				const U32 vLocalIdx10 = pOrgSegs[idx10];
				const U32 vLocalIdx11 = pOrgSegs[idx11];

				const U32 vMergedIdx10 = pVertsMergeTable[vLocalIdx10];
				const U32 vMergedIdx11 = pVertsMergeTable[vLocalIdx11];

				const U32F vGlobalIdx10 = vertLocalToGlobal.Find(vMergedIdx10);
				const U32F vGlobalIdx11 = vertLocalToGlobal.Find(vMergedIdx11);

				ExternalBitArray::BitwiseAnd(&dummyOwners1, pOutGVO[vGlobalIdx10], pOutGVO[vGlobalIdx11]);
				if (!CanIntersect(dummyOwners0, dummyOwners1, canIntersect))
				{
					//MsgCon("BuildSegmentTable: skipped [%d, %d] -> [%d, %d]\n", vLocalIdx00, vLocalIdx01, vLocalIdx10, vLocalIdx11);
					continue;
				}

				if (vMergedIdx10 == vMergedIdx11) // Skip zero length segment
				{
					continue;
				}

				//MsgCon("BuildSegmentTable:     ran [%d, %d] -> [%d, %d]\n", vLocalIdx00, vLocalIdx01, vLocalIdx10, vLocalIdx11);

				IntersectSegSegXz(iSeg0,
								  iSeg1,
								  vLocalIdx00,
								  vLocalIdx01,
								  vMergedIdx10,
								  vMergedIdx11,
								  vertGlobalToLocal,
								  vertLocalToGlobal,
								  dummyOwners0,
								  dummyOwners1,
								  numOrgVerts,
								  pOutGVP,
								  pOutGVO,
								  intersectionWriteIdx,
								  outVIntersectionGlobalIdx,
								  pVertsTags,
								  pVertsMergeTable,
								  pSegmentTable);
			}
		}
		NAV_ASSERT(intersectionWriteIdx < kMaxNumIntersections);
	}

	{
		PROFILE(Navigation, GatherOutputs);

		const U32F numVerts = numOrgVerts + intersectionWriteIdx;
		ExternalBitArray& dummyVertTags = pVertsTags[numVerts];

		outNumVerts = 0;
		for (U32F vLocalIdx = 0; vLocalIdx < numVerts; vLocalIdx++)
		{
			const U32F dupOf = pVertsMergeTable[vLocalIdx];
			NAV_ASSERT(dupOf != -1);
			if (dupOf != vLocalIdx) // Has been merged
			{
				continue;
			}

			outNumVerts++;
		}

		U32F numMaxSegs = 0;
		for (U32F iSeg = 0; iSeg < numOrgSegs; iSeg++)
		{
			const SegmentPoints& seg = pSegmentTable[iSeg];
			numMaxSegs += (seg.m_count - 1);
		}

		outInvalidOrgBoundaryKey = numMaxSegs;
		(*pOutSegs) = NDI_NEW U32[numMaxSegs * 2];

		// Segment original boundaries
		const U32F orgBoundaryTableSize = outVIntersectionGlobalIdx * outVIntersectionGlobalIdx;
		(*pOutOrgBoundaryTable) = NDI_NEW I16[orgBoundaryTableSize];
		for (U32F i = 0; i < orgBoundaryTableSize; i++)
		{
			(*pOutOrgBoundaryTable)[i] = outInvalidOrgBoundaryKey; // Means not a valid segment
		}

		segWriteIdx = 0;
		for (U32F i = 0; i < numOrgSegs; i++)
		{
			const SegmentPoints& seg = pSegmentTable[i];
			const I32F ptCount = seg.m_count;

			const I32F orgBoundary = i < numSubjectSegs ? 
									i :	// Subject segment
									-1;	// Clipper segment

			for (U32F ptIdx0 = 0; ptIdx0 < ptCount - 1; ptIdx0++)
			{
				const U32F ptIdx1 = ptIdx0 + 1;

				vLocalIdx0 = seg.m_pts[ptIdx0].m_idx;
				vLocalIdx1 = seg.m_pts[ptIdx1].m_idx;
				NAV_ASSERT(vLocalIdx0 != vLocalIdx1);

				vMergedIdx0 = pVertsMergeTable[vLocalIdx0];
				vMergedIdx1 = pVertsMergeTable[vLocalIdx1];

				if (vMergedIdx0 == vMergedIdx1)
				{
					NAV_ASSERT(i >= numSubjectSegs);
					continue; // Skip zero length segment
				}

				vGlobalIdx0 = vertLocalToGlobal.Find(vMergedIdx0);
				vGlobalIdx1 = vertLocalToGlobal.Find(vMergedIdx1);

				const U32F s0TableIdx = vGlobalIdx0 * outVIntersectionGlobalIdx + vGlobalIdx1;
				const U32F s1TableIdx = vGlobalIdx1 * outVIntersectionGlobalIdx + vGlobalIdx0;

				NAV_ASSERT((*pOutOrgBoundaryTable)[s0TableIdx] == (*pOutOrgBoundaryTable)[s1TableIdx]);

				if ((*pOutOrgBoundaryTable)[s0TableIdx] == outInvalidOrgBoundaryKey) // This segment has not been added
				{
					NAV_ASSERT((*pOutOrgBoundaryTable)[s0TableIdx] == outInvalidOrgBoundaryKey);

					if (i >= numSubjectSegs) // Clipper segment
					{
						const ExternalBitArray& vert0Tags = pVertsTags[vMergedIdx0];
						const ExternalBitArray& vert1Tags = pVertsTags[vMergedIdx1];
						ExternalBitArray::BitwiseAnd(&dummyVertTags, vert0Tags, vert1Tags);

						const U64 firstSetBit = dummyVertTags.FindFirstSetBit();
						if (firstSetBit < numSubjectSegs) // Skip if both End point touching a subject segment
						{
							continue;
						}
					}

					(*pOutSegs)[segWriteIdx++] = vGlobalIdx0;
					(*pOutSegs)[segWriteIdx++] = vGlobalIdx1;

					(*pOutOrgBoundaryTable)[s0TableIdx] = orgBoundary;
					(*pOutOrgBoundaryTable)[s1TableIdx] = orgBoundary;
				}
				else
				{
					NAV_ASSERT((*pOutOrgBoundaryTable)[s0TableIdx] != outInvalidOrgBoundaryKey);
				}
			}
		}

		outNumSegs = segWriteIdx / 2;
	}

#ifndef FINAL_BUILD
	const U32F numIntersections	 = pIsland->m_navBlockers.CountSetBits();
	const bool drawIntersections = g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawIntersections >= 0 && 
								(g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawIntersections >= numIntersections || 
								g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawIntersections == intersectOrder);

	if (drawIntersections)
	{
		for (U32F vLocalIdx = 0; vLocalIdx < numOrgVerts; vLocalIdx++)
		{
			const U32F vGlobalIdx = vertLocalToGlobal.Find(vLocalIdx);

			const Point& vMs = pOutGVP[vGlobalIdx];
			if (vLocalIdx < numSubjectVerts)
			{
				const Point vWs = pNavMeshMother->LocalToWorld(Point(vMs.X(),
															pIsland->m_drawYOffsetMs[NavPolyBlockerIslands::Island::IslandDebugDraw::kDrawIntersections],
															vMs.Z()));

				ps.DrawString(vWs, StringBuilder<16>("%d", vLocalIdx).c_str(), kColorBlueTrans, 0.75f);
			}
			else
			{
				const U32F vMergedIdx = pVertsMergeTable[vLocalIdx];
				const bool hasMerged = vLocalIdx != vMergedIdx;
				const bool drawMerged = hasMerged && g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawMerged;

				const Point vWs = pNavMeshMother->LocalToWorld(Point(vMs.X(),
															pIsland->m_drawYOffsetMs[NavPolyBlockerIslands::Island::IslandDebugDraw::kDrawIntersections] + 0.05f,
															vMs.Z()));

				if (drawMerged)
				{
					ps.DrawString(vWs, StringBuilder<16>("%d", vLocalIdx).c_str(), kColorYellowTrans, 0.75f);
				}
				else
				{
					ps.DrawString(vWs, StringBuilder<16>("%d", vLocalIdx).c_str(), kColorMagentaTrans, 0.75f);
				}
			}
		}

		for (U32F iSeg = 0; iSeg < numSubjectSegs; iSeg++) // Draw subject
		{
			idx0 = iSeg * 2;
			idx1 = idx0 + 1;

			vLocalIdx0 =   pOrgSegs[idx0];
			vLocalIdx1 = pOrgSegs[idx1];

			vGlobalIdx0 = vertLocalToGlobal.Find(vLocalIdx0);
			vGlobalIdx1 = vertLocalToGlobal.Find(vLocalIdx1);

			const Point& v0Ms = pOutGVP[vGlobalIdx0];
			const Point& v1Ms = pOutGVP[vGlobalIdx1];

			const Point v0Ws = pNavMeshMother->LocalToWorld(Point(v0Ms.X(),
															pIsland->m_drawYOffsetMs[NavPolyBlockerIslands::Island::IslandDebugDraw::kDrawIntersections],
															v0Ms.Z()));
			const Point v1Ws = pNavMeshMother->LocalToWorld(Point(v1Ms.X(),
															pIsland->m_drawYOffsetMs[NavPolyBlockerIslands::Island::IslandDebugDraw::kDrawIntersections],
															v1Ms.Z()));

			// ps.DrawArrow(v0Ws, v1Ws, 0.2f, kColorBlue);
			ps.DrawLine(v0Ws, v1Ws, kColorBlue);
			ps.DrawFlatCapsule(v0Ws, v1Ws, g_navOptions.m_navMeshPatch.m_navMeshPatchSnapDist, kColorGrayTrans);
			ps.DrawString(AveragePos(v0Ws, v1Ws), StringBuilder<16>("%d", iSeg).c_str(), kColorBlueTrans, 0.75f);
		}

		for (U32F iSeg = numSubjectSegs; iSeg < numOrgSegs; iSeg++)
		{
			idx0 = iSeg * 2;
			idx1 = idx0 + 1;

			vLocalIdx0 = pOrgSegs[idx0];
			vLocalIdx1 = pOrgSegs[idx1];

			vMergedIdx0 = pVertsMergeTable[vLocalIdx0];
			vMergedIdx1 = pVertsMergeTable[vLocalIdx1];

			const bool hasMerged  = vLocalIdx0 != vMergedIdx0 || vLocalIdx1 != vMergedIdx1;
			const bool drawMerged = hasMerged && g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawMerged;
			if (drawMerged)
			{
				vGlobalIdx0 = vertLocalToGlobal.Find(vMergedIdx0);
				vGlobalIdx1 = vertLocalToGlobal.Find(vMergedIdx1);
			}
			else
			{
				vGlobalIdx0 = vertLocalToGlobal.Find(vLocalIdx0);
				vGlobalIdx1 = vertLocalToGlobal.Find(vLocalIdx1);
			}

			const Point& v0Ms = pOutGVP[vGlobalIdx0];
			const Point& v1Ms = pOutGVP[vGlobalIdx1];

			const Point v0Ws = pNavMeshMother->LocalToWorld(Point(v0Ms.X(),
															pIsland->m_drawYOffsetMs[NavPolyBlockerIslands::Island::IslandDebugDraw::kDrawIntersections] + 0.05f,
															v0Ms.Z()));
			const Point v1Ws = pNavMeshMother->LocalToWorld(Point(v1Ms.X(),
															pIsland->m_drawYOffsetMs[NavPolyBlockerIslands::Island::IslandDebugDraw::kDrawIntersections] + 0.05f,
															v1Ms.Z()));

			if (drawMerged)
			{
				// ps.DrawArrow(v0Ws, v1Ws, 0.2f, kColorPink);
				ps.DrawLine(v0Ws, v1Ws, kColorYellow);
				ps.DrawFlatCapsule(v0Ws, v1Ws, g_navOptions.m_navMeshPatch.m_navMeshPatchSnapDist, kColorGrayTrans);
				ps.DrawString(AveragePos(v0Ws, v1Ws), StringBuilder<16>("%d", iSeg).c_str(), kColorYellowTrans, 0.75f);
			}
			else
			{
				// ps.DrawArrow(v0Ws, v1Ws, 0.2f, kColorMagenta);
				ps.DrawLine(v0Ws, v1Ws, kColorMagenta);
				ps.DrawFlatCapsule(v0Ws, v1Ws, g_navOptions.m_navMeshPatch.m_navMeshPatchSnapDist, kColorGrayTrans);
				ps.DrawString(AveragePos(v0Ws, v1Ws), StringBuilder<16>("%d", iSeg).c_str(), kColorMagentaTrans, 0.75f);
			}
		}

		if (g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawVerts || 
			g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawSegs)
		{
			char* const ownersStr = NDI_NEW(kAllocDebug) char[pOutGVO[0].GetMaxBitCount() + 1 + 1];

			IndexMapTable vertGlobalToPacked;
			vertGlobalToPacked.Init(numMaxVerts);
			vertGlobalToPacked.Clear();

			vertWriteIdx = 0;
			for (U32F iSeg = 0; iSeg < outNumSegs; iSeg++)
			{
				idx0 = iSeg * 2;
				idx1 = idx0 + 1;

				vGlobalIdx0 = (*pOutSegs)[idx0];
				vGlobalIdx1 = (*pOutSegs)[idx1];

				const Point& v0Ms = pOutGVP[vGlobalIdx0];
				const Point v0Ws  = pNavMeshMother->LocalToWorld(Point(v0Ms.X(),
																pIsland->m_drawYOffsetMs[NavPolyBlockerIslands::Island::IslandDebugDraw::kDrawVertsSegs],
																v0Ms.Z()));

				const Point& v1Ms = pOutGVP[vGlobalIdx1];
				const Point v1Ws  = pNavMeshMother->LocalToWorld(Point(v1Ms.X(),
																pIsland->m_drawYOffsetMs[NavPolyBlockerIslands::Island::IslandDebugDraw::kDrawVertsSegs],
																v1Ms.Z()));

				// ps.DrawArrow(v0Ws, v1Ws, 0.2f, kColorCyan);
				ps.DrawLine(v0Ws, v1Ws, kColorCyan);

				U32F vPackedIdx;
				U32F vLocalIdx;
				U32F index = vertGlobalToPacked.Find(vGlobalIdx0);
				if (index == IndexMapTable::kInvalidIndex)
				{
					vertGlobalToPacked.Add(vGlobalIdx0, vertWriteIdx);
					vPackedIdx = vertWriteIdx++;
					vLocalIdx  = vertGlobalToLocal.Find(vGlobalIdx0);

					if (g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawVerts)
					{
						const bool drawVertOwners = g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawVertOwners >= 0 && 
													(g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawVertOwners >= outNumVerts || 
													g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawVertOwners == vPackedIdx);

						if (drawVertOwners)
						{
							const ExternalBitArray& vert0Owners = pOutGVO[vGlobalIdx0];

							OwnersToStr(pIsland, numNavBlockers, vert0Owners, ownersStr);
							ps.DrawString(v0Ws,
										  StringBuilder<128>("%d:%d:%d\n%s\n",
															 vPackedIdx,
															 vLocalIdx,
															 vGlobalIdx0,
															 ownersStr)
											  .c_str(),
										  kColorCyan,
										  0.75f);
						}
						else
						{
							ps.DrawString(v0Ws,
										  StringBuilder<128>("%d:%d:%d", vPackedIdx, vLocalIdx, vGlobalIdx0).c_str(),
										  kColorCyan,
										  0.75f);
						}
					}
				}

				index = vertGlobalToPacked.Find(vGlobalIdx1);
				if (index == IndexMapTable::kInvalidIndex)
				{
					vertGlobalToPacked.Add(vGlobalIdx1, vertWriteIdx);
					vPackedIdx = vertWriteIdx++;
					vLocalIdx  = vertGlobalToLocal.Find(vGlobalIdx1);

					if (g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawVerts)
					{
						const bool drawVertOwners = g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawVertOwners >= 0
													&& (g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawVertOwners
															>= outNumVerts
														|| g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawVertOwners
															   == vPackedIdx);

						if (drawVertOwners)
						{
							const ExternalBitArray& vert1Owners = pOutGVO[vGlobalIdx1];

							OwnersToStr(pIsland, numNavBlockers, vert1Owners, ownersStr);
							ps.DrawString(v1Ws,
										  StringBuilder<128>("%d:%d:%d\n%s\n",
															 vPackedIdx,
															 vLocalIdx,
															 vGlobalIdx1,
															 ownersStr)
											  .c_str(),
										  kColorCyan,
										  0.75f);
						}
						else
						{
							ps.DrawString(v1Ws,
										  StringBuilder<128>("%d:%d:%d", vPackedIdx, vLocalIdx, vGlobalIdx1).c_str(),
										  kColorCyan,
										  0.75f);
						}
					}
				}

				if (g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawSegs)
				{
					const U32F sTableIdx  = vGlobalIdx0 * outVIntersectionGlobalIdx + vGlobalIdx1;
					const I32 orgBoundary = (*pOutOrgBoundaryTable)[sTableIdx];
					NAV_ASSERT(orgBoundary != outInvalidOrgBoundaryKey);

					const bool drawSegOwners = g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawSegOwners >= 0
											   && (g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawSegOwners >= outNumSegs
												   || g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawSegOwners == iSeg);
					if (drawSegOwners)
					{
						const ExternalBitArray& vert0Owners = pOutGVO[vGlobalIdx0];
						const ExternalBitArray& vert1Owners = pOutGVO[vGlobalIdx1];
						ExternalBitArray::BitwiseAnd(&dummyOwners0, vert0Owners, vert1Owners);

						OwnersToStr(pIsland, numNavBlockers, dummyOwners0, ownersStr);
						ps.DrawString(AveragePos(v0Ws, v1Ws),
									  StringBuilder<128>("%d:%d\n%s\n", iSeg, orgBoundary, ownersStr).c_str(),
									  kColorCyanTrans,
									  0.75f);
					}
					else
					{
						ps.DrawString(AveragePos(v0Ws, v1Ws),
									  StringBuilder<128>("%d:%d", iSeg, orgBoundary).c_str(),
									  kColorCyanTrans,
									  0.75f);
					}
				}
			}
		}
	}
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsPointInsideClipPoly(Point_arg pt, const Clip2D::Poly& poly)
{
	bool inside = false;

	const float x = pt.X();
	const float z = pt.Z();

	U32F iV0 = 0;
	do
	{
		U32F iV1;
		if (poly.m_verts[iV0].m_next < 0)
		{
			iV1 = 0;
		}
		else
		{
			iV1 = poly.m_verts[iV0].m_next;
		}

		const Point p0 = poly.m_verts[iV0].m_pos;
		const Point p1 = poly.m_verts[iV1].m_pos;

		const float p0x = p0.X();
		const float p0z = p0.Z();
		const float p1x = p1.X();
		const float p1z = p1.Z();

		if ((((p1z <= z) && (z < p0z)) || ((p0z <= z) && (z < p1z)))
			&& (x < (p0x - p1x) * (z - p1z) / (p0z - p1z) + p1x))
		{
			inside = !inside;
		}

		iV0 = iV1;

	} while (iV0 != 0);

	return inside;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void BuildNavPolys(const NavPolyBlockerIntersections* const pIntersections,
						  const NavPolyBlockerIslands::Island* const pIsland,
						  const NavMesh* const pNavMeshMother,
						  const Clip2D::Poly* const pNavBlockerNavPolysMs,
						  const U32F numNavBlockers,
						  const ExternalBitArray& canIntersect,
						  Point* const pOutGVP,
						  ExternalBitArray* const pOutGVO,
						  U32F& outGVWriteIdx,
						  U32F* const pOutNavPolySegIndices,
						  FeatureKey* const pOutNavPolySegVertKeys,
						  U32F& outNumUniqueNavPolySegs,
						  Clip2D::Poly* const pOutNavPolyClipPolysMs)
{
	PROFILE(Navigation, BuildNavPolys);

	const U32F numNavPolys	 = pIsland->m_numNavPolys;
	const U32F numPrimitives = numNavPolys + numNavBlockers;

	const U32F numNavPolyVerts = numNavPolys * 4;

	for (U32F navPolyLocalIdx = 0; navPolyLocalIdx < numNavPolys; navPolyLocalIdx++)
	{
		const U32F navPolyGlobalIdx	  = pIsland->m_navPolys[navPolyLocalIdx];
		const NavPoly* const pNavPoly = pIntersections->m_pNavPolys[navPolyGlobalIdx];
		const NavMesh* const pNavMesh = pNavPoly->GetNavMesh();

		Clip2D::Poly& navPolyClipPolyMs = pOutNavPolyClipPolysMs[navPolyLocalIdx];

		const U32F vertCount = pNavPoly->GetVertexCount();
		for (U32F iV = 0; iV < vertCount; iV++)
		{
			const Point& vLs = pNavPoly->GetVertex(iV);
			Point vMs;

			const U32F vIdx = navPolyLocalIdx * 4 + iV;
			if (pNavMesh == pNavMeshMother)
			{
				vMs = Point(vLs.X(), vLs.Y(), vLs.Z());
			}
			else
			{
				const Point vWs = pNavMesh->LocalToWorld(vLs);
				vMs = pNavMeshMother->WorldToLocal(vWs);
			}

			pOutGVP[vIdx] = vMs;

			pOutGVO[vIdx].ClearAllBits();
			pOutGVO[vIdx].SetBit(navPolyLocalIdx);

			const U32F pos0 = navPolyLocalIdx;
			for (U32F navBlockerLocalIdx = 0; navBlockerLocalIdx < numNavBlockers; navBlockerLocalIdx++)
			{
				const U32F pos1			= numNavPolys + navBlockerLocalIdx;
				const U32F intersectBit = pos0 * numPrimitives + pos1;
				if (!canIntersect.IsBitSet(intersectBit))
				{
					continue;
				}

				if (IsPointInsideClipPoly(vMs, pNavBlockerNavPolysMs[navBlockerLocalIdx]))
				{
					pOutGVO[vIdx].SetBit(numNavPolys + navBlockerLocalIdx);
				}
			}

			navPolyClipPolyMs.PushVertex(vMs, iV);
		}
	}

	// Merge nav poly verts based on proximity
	I32F* const pVertsMergeTable = NDI_NEW I32F[numNavPolyVerts];
	for (U32F vIdx = 0; vIdx < numNavPolyVerts; vIdx++)
	{
		pVertsMergeTable[vIdx] = -1; // Unprocessed
	}

	for (U32F navPolyLocalIdx0 = 0; navPolyLocalIdx0 < numNavPolys; navPolyLocalIdx0++)
	{
		const U32F navPolyGlobalIdx0   = pIsland->m_navPolys[navPolyLocalIdx0];
		const NavPoly* const pNavPoly0 = pIntersections->m_pNavPolys[navPolyGlobalIdx0];

		const U32F vertCount0 = pNavPoly0->GetVertexCount();
		for (U32F iV0 = 0; iV0 < vertCount0; iV0++)
		{
			const U32F vIdx0 = navPolyLocalIdx0 * 4 + iV0;
			if (pVertsMergeTable[vIdx0] != -1) // vIdx0 has already been merged
			{
				NAV_ASSERT(pVertsMergeTable[vIdx0] != vIdx0);
				continue;
			}

			pVertsMergeTable[vIdx0] = vIdx0; // First time discover vIdx0

			const Point& v0Ms = pOutGVP[vIdx0];

			for (U32F navPolyLocalIdx1 = navPolyLocalIdx0 + 1; navPolyLocalIdx1 < numNavPolys; navPolyLocalIdx1++)
			{
				const U32F navPolyGlobalIdx1   = pIsland->m_navPolys[navPolyLocalIdx1];
				const NavPoly* const pNavPoly1 = pIntersections->m_pNavPolys[navPolyGlobalIdx1];

				const U32F vertCount1 = pNavPoly1->GetVertexCount();
				for (U32F iV1 = 0; iV1 < vertCount1; iV1++)
				{
					const U32F vIdx1 = navPolyLocalIdx1 * 4 + iV1;
					if (pVertsMergeTable[vIdx1] != -1) // vIdx1 has already been merged
					{
						NAV_ASSERT(pVertsMergeTable[vIdx1] != vIdx1);
						continue;
					}

					const Point& v1Ms = pOutGVP[vIdx1];

					const float distSqr
						= DistSqr(v0Ms,
								  v1Ms); // This is so that nav poly verts with different ys, such as staircase nav polys, wont be merged.
					if (distSqr
						< kNavPolyVertMergeEpsilonSqr) // Nav poly verts are binary identical, same should be for linked nav polys, see ConnectLink(...) in nav mesh login for details.
					{
						pVertsMergeTable[vIdx1] = vIdx0;
					}
				}
			}
		}
	}

	I32F* const pVertsPackedIdxTable = NDI_NEW I32F[numNavPolyVerts];
	for (U32F vIdx = 0; vIdx < numNavPolyVerts; vIdx++)
	{
		const I32F dupOf = pVertsMergeTable[vIdx];
		if (dupOf == -1) // Invalid nav poly vert
		{
			continue;
		}

		if (dupOf != vIdx) // Has been merged
		{
			ExternalBitArray::BitwiseOr(&pOutGVO[dupOf], pOutGVO[dupOf], pOutGVO[vIdx]);
			continue;
		}

		pVertsPackedIdxTable[vIdx] = outGVWriteIdx;
		outGVWriteIdx++;
	}

	for (U32F vIdx = 0; vIdx < numNavPolyVerts; vIdx++)
	{
		const I32F dupOf = pVertsMergeTable[vIdx];
		if (dupOf == -1) // Invalid nav poly vert
		{
			continue;
		}

		if (dupOf == vIdx) // Not a dup
		{
			const U32F vPackedIdx = pVertsPackedIdxTable[vIdx];
			pOutGVP[vPackedIdx]	  = Point(pOutGVP[vIdx].X(), 0.0f, pOutGVP[vIdx].Z()); // Drop y
			ExternalBitArray::Copy(&pOutGVO[vPackedIdx], pOutGVO[vIdx]);
			continue;
		}

		pVertsPackedIdxTable[vIdx] = pVertsPackedIdxTable[dupOf];
	}

	// Shared edges information
	const U32F numNavPolySegs = numNavPolyVerts;
	SharedEdgeTable sharedEdgeTable;
	sharedEdgeTable.Init(numNavPolySegs, FILE_LINE_FUNC);
	sharedEdgeTable.Clear();

	// Build unique nav poly segments and the shared edge table
	outNumUniqueNavPolySegs = 0;
	for (U32F navPolyLocalIdx0 = 0; navPolyLocalIdx0 < numNavPolys; navPolyLocalIdx0++)
	{
		const U32F navPolyGlobalIdx0   = pIsland->m_navPolys[navPolyLocalIdx0];
		const NavPoly* const pNavPoly0 = pIntersections->m_pNavPolys[navPolyGlobalIdx0];
		const NavMesh* const pNavMesh0 = pNavPoly0->GetNavMesh();

		const U32F edgeCount0 = pNavPoly0->GetVertexCount();
		for (U32F e0Idx = 0; e0Idx < edgeCount0; e0Idx++)
		{
			const U32F e0v0Idx = e0Idx;
			const U32F e0v1Idx = (e0Idx + 1) % edgeCount0;

			const FeatureKey v0Key = { navPolyLocalIdx0, e0v0Idx };
			const FeatureKey v1Key = { navPolyLocalIdx0, e0v1Idx };

			const U32F vIdx0 = v0Key.m_subKey0 * 4 + v0Key.m_subKey1;
			const U32F vIdx1 = v1Key.m_subKey0 * 4 + v1Key.m_subKey1;

			const U32F vPackedIdx0 = pVertsPackedIdxTable[vIdx0];
			const U32F vPackedIdx1 = pVertsPackedIdxTable[vIdx1];

			const U32F idx0 = outNumUniqueNavPolySegs * 2;
			const U32F idx1 = idx0 + 1;

			const U32F adjPolyId0 = pNavPoly0->GetAdjacentId(e0Idx);
			if (adjPolyId0 == NavPoly::kNoAdjacent) // Not adjacent to any nav poly, hard boundary segment
			{
				pOutNavPolySegIndices[idx0] = vPackedIdx0;
				pOutNavPolySegIndices[idx1] = vPackedIdx1;

				pOutNavPolySegVertKeys[idx0] = v0Key;
				pOutNavPolySegVertKeys[idx1] = v1Key;

				outNumUniqueNavPolySegs++;

				continue;
			}

			const NavPoly* pNavPoly1 = &pNavMesh0->GetPoly(adjPolyId0);
			if (pNavPoly1->IsLink())
			{
				pNavPoly1 = pNavPoly1->GetLinkedPoly();
				if (!pNavPoly1 || !pNavPoly1->IsValid()) // Bad link, "hard" boundary segment
				{
					pOutNavPolySegIndices[idx0] = vPackedIdx0;
					pOutNavPolySegIndices[idx1] = vPackedIdx1;

					pOutNavPolySegVertKeys[idx0] = v0Key;
					pOutNavPolySegVertKeys[idx1] = v1Key;

					outNumUniqueNavPolySegs++;

					continue;
				}
			}

			// Check if the adjacent nav poly is also on this island
			U32F foundNavPolyLocalIdx1 = numNavPolys;
			for (U32F navPolyLocalIdx1 = 0; navPolyLocalIdx1 < numNavPolys; navPolyLocalIdx1++)
			{
				const U32F navPolyGlobalIdx1	   = pIsland->m_navPolys[navPolyLocalIdx1];
				const NavPoly* const pOtherNavPoly = pIntersections->m_pNavPolys[navPolyGlobalIdx1];
				if (pOtherNavPoly == pNavPoly1)
				{
					foundNavPolyLocalIdx1 = navPolyLocalIdx1;
					break;
				}
			}

			if (foundNavPolyLocalIdx1
				== numNavPolys) // The adjacent nav poly doesnt belong to this island, soft boundary segment
			{
				pOutNavPolySegIndices[idx0] = vPackedIdx0;
				pOutNavPolySegIndices[idx1] = vPackedIdx1;

				pOutNavPolySegVertKeys[idx0] = v0Key;
				pOutNavPolySegVertKeys[idx1] = v1Key;

				outNumUniqueNavPolySegs++;

				continue;
			}

			// This is a shared edge
			const FeatureKey currEdgeKey = { navPolyLocalIdx0, e0Idx };
			if (sharedEdgeTable.Find(currEdgeKey) == sharedEdgeTable.end())
			{
				// Find the shared edge on the adjacent nav poly
				const NavMesh* const pNavMesh1 = pNavPoly1->GetNavMesh();

				const U32F edgeCount1 = pNavPoly1->GetVertexCount();
				U32F e1Idx = edgeCount1;
				for (U32F i = 0; i < edgeCount1; i++)
				{
					const U32F adjPolyId1 = pNavPoly1->GetAdjacentId(i);
					if (adjPolyId1 == NavPoly::kNoAdjacent) // Not adjacent to any nav poly
					{
						continue;
					}

					const NavPoly* pOtherNavPoly = &pNavMesh1->GetPoly(adjPolyId1);
					if (pOtherNavPoly->IsLink())
					{
						pOtherNavPoly = pOtherNavPoly->GetLinkedPoly();
					}

					if (pOtherNavPoly == pNavPoly0)
					{
						e1Idx = i;
						break;
					}
				}
				NavMeshPatchAssert(e1Idx != edgeCount1, pNavMeshMother, FILE_LINE_FUNC);

				// Found a unique segment
				pOutNavPolySegIndices[idx0] = vPackedIdx0;
				pOutNavPolySegIndices[idx1] = vPackedIdx1;

				pOutNavPolySegVertKeys[idx0] = v0Key;
				pOutNavPolySegVertKeys[idx1] = v1Key;

				outNumUniqueNavPolySegs++;

				// Found a pair of shared edges
				const FeatureKey adjEdgeKey = { static_cast<U32F>(foundNavPolyLocalIdx1), e1Idx };
				NAV_ASSERT(sharedEdgeTable.Find(adjEdgeKey) == sharedEdgeTable.end());

				sharedEdgeTable.Add(currEdgeKey, adjEdgeKey);
				sharedEdgeTable.Add(adjEdgeKey, currEdgeKey);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
#ifndef FINAL_BUILD
static void CalculateDebugDrawYOffsets(const NavPolyBlockerIntersections* const pIntersections,
									   const NavMesh* const pNavMeshMother,
									   NavPolyBlockerIslands::Island* const pIsland)
{
	float yOffsetMs = -FLT_MAX;

	for (U32F navPolyLocalIdx = 0; navPolyLocalIdx < pIsland->m_numNavPolys; navPolyLocalIdx++)
	{
		const U32F navPolyGlobalIdx	  = pIsland->m_navPolys[navPolyLocalIdx];
		const NavPoly* const pNavPoly = pIntersections->m_pNavPolys[navPolyGlobalIdx];
		const NavMesh* const pNavMesh = pNavPoly->GetNavMesh();

		const U32F vertCount = pNavPoly->GetVertexCount();
		for (U32F iV = 0; iV < vertCount; iV++)
		{
			const Point& vLs = pNavPoly->GetVertex(iV);
			float yMs;

			if (pNavMesh == pNavMeshMother)
			{
				yMs = vLs.Y();
			}
			else
			{
				const Point vWs = pNavMesh->LocalToWorld(vLs);
				const Point vMs = pNavMeshMother->WorldToLocal(vWs);
				yMs = vMs.Y();
			}

			if (yMs > yOffsetMs)
			{
				yOffsetMs = yMs;
			}
		}
	}

	yOffsetMs += 0.15f;

	if (g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawIntersections >= 0)
	{
		yOffsetMs += 0.15f;
		pIsland->m_drawYOffsetMs[NavPolyBlockerIslands::Island::kDrawIntersections] = yOffsetMs;
	}

	if (g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawVerts
		|| g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawSegs)
	{
		yOffsetMs += 0.15f;
		pIsland->m_drawYOffsetMs[NavPolyBlockerIslands::Island::kDrawVertsSegs] = yOffsetMs;
	}

	if (g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawTriangulation >= 0)
	{
		yOffsetMs += 0.15f;
		pIsland->m_drawYOffsetMs[NavPolyBlockerIslands::Island::kDrawTriangulation] = yOffsetMs;
	}

	if (g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawPoly >= 0)
	{
		yOffsetMs += 0.15f;
		pIsland->m_drawYOffsetMs[NavPolyBlockerIslands::Island::kDrawPolys] = yOffsetMs;
	}
}
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
static void ProcessIslandNavPoly(const NavPolyBlockerIntersections* const pIntersections,
								 const NavPolyBlockerIslands::Island* const pIsland,
								 const U32F numNavBlockers,
								 const PrimitiveMapTable* const pNavBlockerLocalToGlobal,
								 const NavMeshPatchInput* const pPatchInput,
								 const NavMesh* const pNavMeshMother,
								 const U32F navPolyLocalIdx,
								 const Point* const pGVP,
								 const ExternalBitArray* const pGVO,
								 const U32F numVerts,
								 const U32* const pSegs,
								 const U32F numSegs,
								 const U32F vIntersectionGlobalIdx,
								 const U32F numUniqueNavPolySegs,
								 const U32F invalidOrgBoundaryKey,
								 const I16* const pOrgBoundaryTable,
								 const SegmentMapTable* const pSegIdxTable,
								 SegmentNeighbors* const pSegNeighbors)
{
	PROFILE_ACCUM(ProcessIslandNavPoly);

	if (g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsNavPolyIdx >= 0 &&
		g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsNavPolyIdx != navPolyLocalIdx)
	{
		return;
	}

	AllocateJanitor kk(kAllocSingleGameFrame, FILE_LINE_FUNC);

	// Pack global verts and segments
	Vec2* const pPackedVerts = NDI_NEW Vec2[numVerts];
	U32* const pPackedSegs	 = NDI_NEW U32[numSegs * 2];

	VertMapTable vertGlocalToPacked;
	IndexMapTable vertPackedToGlobal;

	vertGlocalToPacked.Init(numVerts, FILE_LINE_FUNC);
	vertPackedToGlobal.Init(numVerts);

	vertGlocalToPacked.Clear();
	vertPackedToGlobal.Clear();

	const U32F numNavPolys	 = pIsland->m_numNavPolys;
	const U32F numPrimitives = numNavPolys + numNavBlockers;

	const U32F numOwnerBits	  = numPrimitives;
	const U32F numOwnerBlocks = ExternalBitArray::DetermineNumBlocks(numOwnerBits);

	U64* const pSegOwnerBlocks = NDI_NEW U64[3 * numOwnerBlocks];
	ExternalBitArray dummyOwners0;
	ExternalBitArray dummyOwners1;
	ExternalBitArray dummyOwners2;
	dummyOwners0.Init(numOwnerBits, &pSegOwnerBlocks[0 /* * numOwnerBlocks*/]);
	dummyOwners1.Init(numOwnerBits, &pSegOwnerBlocks[/*1 * */ numOwnerBlocks]);
	dummyOwners2.Init(numOwnerBits, &pSegOwnerBlocks[2 * numOwnerBlocks]);

	const U32F navPolyGlobalIdx = pIsland->m_navPolys[navPolyLocalIdx];
	const NavBlockerBits& navPolyToBlockers = pIntersections->m_pNavPolyToBlockers[navPolyGlobalIdx];

	U32F numValidVerts = 0;
	U32F segWriteIdx   = 0;

	{	// Pack nav poly verts and segments
		PROFILE(Navigation, Pack);

		for (U32F iSeg = 0; iSeg < numSegs; iSeg++)
		{
			const U32F idx0 = iSeg * 2;
			const U32F idx1 = idx0 + 1;

			const U32F vGlobalIdx0 = pSegs[idx0];
			const U32F vGlobalIdx1 = pSegs[idx1];

			ExternalBitArray::BitwiseAnd(&dummyOwners0, pGVO[vGlobalIdx0], pGVO[vGlobalIdx1]);
			if (!dummyOwners0.IsBitSet(navPolyLocalIdx))
			{
				continue;
			}

			U32F vPackedIdx0;
			U32F vPackedIdx1;

			VertMapTable::ConstIterator iter0 = vertGlocalToPacked.Find(vGlobalIdx0);
			if (iter0 == vertGlocalToPacked.End())
			{
				const Point& v0Ms = pGVP[vGlobalIdx0];
				vPackedIdx0 = numValidVerts++;
				pPackedVerts[vPackedIdx0] = Vec2(v0Ms.X(), v0Ms.Z());

				vertGlocalToPacked.Add(vGlobalIdx0, vPackedIdx0);
				vertPackedToGlobal.Add(vPackedIdx0, vGlobalIdx0);
			}
			else
			{
				vPackedIdx0 = iter0->m_data;
			}

			VertMapTable::ConstIterator iter1 = vertGlocalToPacked.Find(vGlobalIdx1);
			if (iter1 == vertGlocalToPacked.End())
			{
				const Point& v1Ms = pGVP[vGlobalIdx1];
				vPackedIdx1 = numValidVerts++;
				pPackedVerts[vPackedIdx1] = Vec2(v1Ms.X(), v1Ms.Z());

				vertGlocalToPacked.Add(vGlobalIdx1, vPackedIdx1);
				vertPackedToGlobal.Add(vPackedIdx1, vGlobalIdx1);
			}
			else
			{
				vPackedIdx1 = iter1->m_data;
			}

			pPackedSegs[segWriteIdx++] = vPackedIdx0;
			pPackedSegs[segWriteIdx++] = vPackedIdx1;
		}
	}

	const U32F maxTriangles = (numVerts * 2) + 1;
	TriangleIndices* const pTriangles = NDI_NEW TriangleIndices[maxTriangles];
	I32F numGenTriangles = 0;
	
	{
		PROFILE(Navigation, Triangulate);

		const U32F numValidSegs = segWriteIdx / 2;

		DelaunayParams dtParams;
		dtParams.m_pPoints = pPackedVerts;
		dtParams.m_numPoints = numValidVerts;
		dtParams.m_pSegmentIndices = pPackedSegs;
		dtParams.m_numSegments = numValidSegs;
		dtParams.m_localToWorldSpace = pNavMeshMother->GetOriginWs();
		dtParams.m_keepDegenerateTris = true; // It is very import because I dont want any hole in the triangulation

		numGenTriangles = GenerateDelaunay2d(DelaunayMethod::kIncrementalConstrained, 
											dtParams, 
											pTriangles, 
											maxTriangles);
	}

#ifndef FINAL_BUILD
	PrimServerWrapper ps(pNavMeshMother->GetParentSpace());

	for (int iTri = 0; iTri < numGenTriangles; iTri++)
	{
		const bool drawTriangle = g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawTriangulation == iTri
								  || g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawTriangulation >= numGenTriangles;

		if (drawTriangle)
		{
			const TriangleIndices& tri = pTriangles[iTri];

			const Point v0 = Point(pPackedVerts[tri.m_iA].X(),
								   pIsland->m_drawYOffsetMs
									   [NavPolyBlockerIslands::Island::IslandDebugDraw::kDrawTriangulation],
								   pPackedVerts[tri.m_iA].Y());
			const Point v1 = Point(pPackedVerts[tri.m_iB].X(),
								   pIsland->m_drawYOffsetMs
									   [NavPolyBlockerIslands::Island::IslandDebugDraw::kDrawTriangulation],
								   pPackedVerts[tri.m_iB].Y());
			const Point v2 = Point(pPackedVerts[tri.m_iC].X(),
								   pIsland->m_drawYOffsetMs
									   [NavPolyBlockerIslands::Island::IslandDebugDraw::kDrawTriangulation],
								   pPackedVerts[tri.m_iC].Y());

			const Point v0Ws = pNavMeshMother->LocalToWorld(v0);
			const Point v1Ws = pNavMeshMother->LocalToWorld(v1);
			const Point v2Ws = pNavMeshMother->LocalToWorld(v2);

			if (tri.m_neighborTris[0] < 0)
			{
				ps.DrawLine(v0Ws, v1Ws, kColorPurple);
			}
			else
			{
				ps.DrawLine(v0Ws, v1Ws, kColorYellow);
			}

			if (tri.m_neighborTris[1] < 0)
			{
				ps.DrawLine(v1Ws, v2Ws, kColorPurple);
			}
			else
			{
				ps.DrawLine(v1Ws, v2Ws, kColorYellow);
			}

			if (tri.m_neighborTris[2] < 0)
			{
				ps.DrawLine(v2Ws, v0Ws, kColorPurple);
			}
			else
			{
				ps.DrawLine(v2Ws, v0Ws, kColorYellow);
			}

			ps.DrawString(AveragePos(v0Ws, v1Ws, v2Ws), StringBuilder<64>("%d", iTri).c_str(), kColorYellowTrans, 0.75f);
		}
	}
#endif

	bool* const pTriValid = NDI_NEW bool[numGenTriangles];
	U32F numTriangles;

	PolyIndices* pPolyIndices;
	int numPolys;

	{
		PROFILE(Navigation, ProcessTriangles);

		for (U32F iTri = 0; iTri < numGenTriangles; iTri++)
		{
			pTriValid[iTri] = true;
		}

		numTriangles = EliminateInvalidTriangles(pNavMeshMother, 
												numValidVerts,
												pTriangles,
												numGenTriangles,
												vertPackedToGlobal,
												vIntersectionGlobalIdx,
												numUniqueNavPolySegs,
												invalidOrgBoundaryKey,
												pOrgBoundaryTable,
												pTriValid);

		// Merge triangles into polys
		pPolyIndices = NDI_NEW PolyIndices[numTriangles];

		numPolys = TryMergeTriangles2(pNavMeshMother,
									  pTriangles,
									  numGenTriangles,
									  pTriValid,
									  pPackedVerts,
									  vertPackedToGlobal,
									  pGVO,
									  dummyOwners0,
									  dummyOwners1,
									  dummyOwners2,
									  pPolyIndices,
									  maxTriangles);
	}

	const NavPoly* const pNavPoly = pIntersections->m_pNavPolys[navPolyGlobalIdx];
	const NavMesh* const pNavMesh = pNavPoly->GetNavMesh();

	NavPolyEx** ppNewExPolys = NDI_NEW NavPolyEx*[numPolys];
	{ // Allocate ex polys
		PROFILE_ACCUM(AllocateExPolys);
		g_navExData.AllocateNavPolyExArray(pNavPoly, ppNewExPolys, numPolys);
	}

	NavBlockerBits vBlockage;
	for (int iPoly = 0; iPoly < numPolys; iPoly++)
	{
		const PolyIndices& poly0 = pPolyIndices[iPoly];

		NavPolyEx* const pPolyEx0 = ppNewExPolys[iPoly];
		NAV_ASSERT(pPolyEx0);
		NavPolyEx& exPoly0 = (*pPolyEx0);

		exPoly0.m_blockerBits.SetAllBits();

		const PolyIndices& polyIndices = pPolyIndices[iPoly];
		for (U32F iV0 = 0; iV0 < polyIndices.m_numVerts; iV0++)
		{
			const U32F iV1 = (iV0 + 1) == polyIndices.m_numVerts ?
							0 : 
							iV0 + 1;

			// Set position
			const I32F vPackedIdx0 = poly0.m_indices[iV0];
			const U32F vGlobalIdx0 = vertPackedToGlobal.Find(vPackedIdx0);

			Point v0Ms = pGVP[vGlobalIdx0];
			Point v0Ws = pNavMeshMother->LocalToWorld(v0Ms);
			Point v0Ls = pNavMesh->WorldToLocal(v0Ws);

			Vector normalLs = kUnitYAxis;
			pNavPoly->ProjectPointOntoSurfaceLs(&exPoly0.m_vertsLs[iV0], &normalLs, v0Ls);

			// Set blockage
			vBlockage.ClearAllBits();
			const ExternalBitArray& vertOwners = pGVO[vGlobalIdx0];
			for (U32F navBlockerLocalIdx = 0; navBlockerLocalIdx < numNavBlockers; navBlockerLocalIdx++)
			{
				if (vertOwners.IsBitSet(numNavPolys + navBlockerLocalIdx))
				{
					const U32F navBlockerGlobalIdx = pNavBlockerLocalToGlobal->Find(navBlockerLocalIdx)->m_data;
					const I32F blockerMgrIdx	   = pPatchInput->m_pMgrIndices[navBlockerGlobalIdx];

					vBlockage.SetBit(blockerMgrIdx);
				}
			}
			NavBlockerBits::BitwiseAnd(&exPoly0.m_blockerBits, exPoly0.m_blockerBits, vBlockage);

			// Set neighbors, not done yet
			const I32F iPoly1 = polyIndices.m_neighbors[iV0];
			if (iPoly1 >= 0) // Has a neighbor, interior poly edge
			{
				const NavPolyEx* const pPolyEx1 = ppNewExPolys[iPoly1];
				NAV_ASSERT(pPolyEx1);
				const NavPolyEx& exPoly1 = (*pPolyEx1);

				exPoly0.m_adjPolys[iV0] = pNavPoly->GetNavManagerId();
				exPoly0.m_adjPolys[iV0].m_iPolyEx = exPoly1.GetId();
			}
			else
			{
				const I32F vPackedIdx1 = poly0.m_indices[iV1];
				const U32F vGlobalIdx1 = vertPackedToGlobal.Find(vPackedIdx1);

				SegmentNeighbors* pSegNeighbor;
				if (vGlobalIdx0 < vGlobalIdx1)
				{
					const FeatureKey segKey = { vGlobalIdx0, vGlobalIdx1 };
					const SegmentMapTable::ConstIterator iter = pSegIdxTable->Find(segKey);
					if (iter == pSegIdxTable->End())
					{
#ifndef FINAL_BUILD
						OnNavMeshPatchFailure(pNavMeshMother, FILE_LINE_FUNC);
#endif
						continue;
					}

					const U32F iSeg = iter->m_data;
					SegmentNeighbor& neighbor0 = pSegNeighbors[iSeg].m_neighbor0;
					neighbor0.m_id = pNavPoly->GetNavManagerId();
					neighbor0.m_id.m_iPolyEx = exPoly0.GetId();

					neighbor0.m_vIdx = iV0;
				}
				else
				{
					const FeatureKey segKey = { vGlobalIdx1, vGlobalIdx0 };
					const SegmentMapTable::ConstIterator iter = pSegIdxTable->Find(segKey);
					if (iter == pSegIdxTable->End())
					{
#ifndef FINAL_BUILD
						OnNavMeshPatchFailure(pNavMeshMother, FILE_LINE_FUNC);
#endif
						continue;
					}

					const U32F iSeg = iter->m_data;
					SegmentNeighbor& neighbor1 = pSegNeighbors[iSeg].m_neighbor1;
					neighbor1.m_id = pNavPoly->GetNavManagerId();
					neighbor1.m_id.m_iPolyEx = exPoly0.GetId();

					neighbor1.m_vIdx = iV0;
				}
			}
		}

		exPoly0.m_numVerts = polyIndices.m_numVerts;
		if (exPoly0.m_numVerts == 3)
		{
			exPoly0.m_vertsLs[3] = exPoly0.m_vertsLs[2];
		}

#ifndef FINAL_BUILD
		const bool drawPoly = g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawPoly == iPoly
							  || g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawPoly >= numPolys;

		if (drawPoly)
		{
			I32F v0Idx		 = poly0.m_indices[0];
			I32F v1Idx		 = poly0.m_indices[1];
			const I32F v2Idx = poly0.m_indices[2];

			Point v0Ms		 = Point(pPackedVerts[v0Idx].x, 0.0f, pPackedVerts[v0Idx].y);
			Point v1Ms		 = Point(pPackedVerts[v1Idx].x, 0.0f, pPackedVerts[v1Idx].y);
			const Point v2Ms = Point(pPackedVerts[v2Idx].x, 0.0f, pPackedVerts[v2Idx].y);

			Point centerMs = kOrigin;
			if (poly0.m_numVerts == 3)
			{
				centerMs = AveragePos(v0Ms, v1Ms, v2Ms);
			}
			else
			{
				const I32F v3Idx = poly0.m_indices[3];
				const Point v3Ms = Point(pPackedVerts[v3Idx].x, 0.0f, pPackedVerts[v3Idx].y);

				centerMs = AveragePos(v0Ms, v1Ms, v2Ms, v3Ms);
			}

			const Point centerWs = pNavMeshMother
									   ->LocalToWorld(Point(centerMs.X(),
															pIsland->m_drawYOffsetMs
																[NavPolyBlockerIslands::Island::IslandDebugDraw::kDrawPolys],
															centerMs.Z()));
			ps.DrawString(centerWs, StringBuilder<32>("%d:%d", iPoly, exPoly0.GetId()).c_str(), kColorOrangeTrans, 0.75f);

			for (U32F j = 0; j < poly0.m_numVerts; j++)
			{
				v0Idx = poly0.m_indices[j];

				const I32F next = (j + 1) >= poly0.m_numVerts ? 0 : (j + 1);
				v1Idx = poly0.m_indices[next];

				v0Ms = Point(pPackedVerts[v0Idx].x,
							 pIsland->m_drawYOffsetMs[NavPolyBlockerIslands::Island::IslandDebugDraw::kDrawPolys],
							 pPackedVerts[v0Idx].y);
				v1Ms = Point(pPackedVerts[v1Idx].x,
							 pIsland->m_drawYOffsetMs[NavPolyBlockerIslands::Island::IslandDebugDraw::kDrawPolys],
							 pPackedVerts[v1Idx].y);

				const Point v0Ws = pNavMeshMother->LocalToWorld(v0Ms);
				const Point v1Ws = pNavMeshMother->LocalToWorld(v1Ms);

				if (poly0.m_neighbors[j] < 0)
				{
					ps.DrawLine(v0Ws, v1Ws, kColorBlue);
				}
				else
				{
					ps.DrawLine(v0Ws, v1Ws, kColorOrange);
				}
			}
		}
#endif
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ProcessIslandNavPolysBatch(const NavPolyBlockerIntersections* const pIntersections,
									   const NavPolyBlockerIslands::Island* const pIsland,
									   const U32F numNavBlockers,
									   const PrimitiveMapTable* const pNavBlockerLocalToGlobal,
									   const NavMeshPatchInput* const pPatchInput,
									   const NavMesh* const pNavMeshMother,
									   const U32F navPolyLocalStartIdx,
									   const U32F navPolyLocalEndIdx,
									   const Point* const pGVP,
									   const ExternalBitArray* const pGVO,
									   const U32F numVerts,
									   const U32* const pSegs,
									   const U32F numSegs,
									   const U32F vIntersectionGlobalIdx,
									   const U32F numUniqueNavPolySegs,
									   const U32F invalidOrgBoundaryKey,
									   const I16* const pOrgBoundaryTable,
									   const SegmentMapTable* const pSegIdxTable,
									   SegmentNeighbors* const pSegNeighbors)
{
	PROFILE(Navigation, ProcessIslandNavPolysBatch);

	NAV_ASSERT(0 <= navPolyLocalStartIdx && navPolyLocalStartIdx < pIsland->m_numNavPolys);
	NAV_ASSERT(0 <= navPolyLocalEndIdx && navPolyLocalEndIdx < pIsland->m_numNavPolys);

	for (U32F iPoly = navPolyLocalStartIdx; iPoly <= navPolyLocalEndIdx; iPoly++)
	{
		ProcessIslandNavPoly(pIntersections,
							 pIsland,
							 numNavBlockers,
							 pNavBlockerLocalToGlobal,
							 pPatchInput,
							 pNavMeshMother,
							 iPoly,
							 pGVP,
							 pGVO,
							 numVerts,
							 pSegs,
							 numSegs,
							 vIntersectionGlobalIdx,
							 numUniqueNavPolySegs,
							 invalidOrgBoundaryKey,
							 pOrgBoundaryTable,
							 pSegIdxTable,
							 pSegNeighbors);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct ProcessIslandNavPolysBatchJobData
{
	const NavPolyBlockerIntersections* m_pIntersections;
	const NavPolyBlockerIslands::Island* m_pIsland;
	U32F m_numNavBlockers;
	const PrimitiveMapTable* m_pNavBlockerLocalToGlobal;
	const NavMeshPatchInput* m_pPatchInput;
	const NavMesh* m_pNavMeshMother;
	U32F m_navPolyLocalStartIdx;
	U32F m_navPolyLocalEndIdx;
	const Point* m_pGVP;
	U32F m_numVerts;
	const ExternalBitArray* m_pGVO;
	const U32* m_pSegs;
	U32F m_numSegs;
	U32F m_vIntersectionGlobalIdx;
	U32F m_numUniqueNavPolySegs;
	U32F m_invalidOrgBoundaryKey;
	const I16* m_pOrgBoundaryTable;
	const SegmentMapTable* m_pSegIdxTable;
	SegmentNeighbors* m_pSegNeighbors;
	mutable ndjob::CounterHandle m_pManuallyAssociatedCounter;
	mutable U64 m_duration;
};

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT(ProcessIslandNavPolysBatchEntryPoint)
{
	const ProcessIslandNavPolysBatchJobData* const pData = (const ProcessIslandNavPolysBatchJobData*)jobParam;

	const U64 startTime = TimerGetRawCount();

	ProcessIslandNavPolysBatch(pData->m_pIntersections,
							   pData->m_pIsland,
							   pData->m_numNavBlockers,
							   pData->m_pNavBlockerLocalToGlobal,
							   pData->m_pPatchInput,
							   pData->m_pNavMeshMother,
							   pData->m_navPolyLocalStartIdx,
							   pData->m_navPolyLocalEndIdx,
							   pData->m_pGVP,
							   pData->m_pGVO,
							   pData->m_numVerts,
							   pData->m_pSegs,
							   pData->m_numSegs,
							   pData->m_vIntersectionGlobalIdx,
							   pData->m_numUniqueNavPolySegs,
							   pData->m_invalidOrgBoundaryKey,
							   pData->m_pOrgBoundaryTable,
							   pData->m_pSegIdxTable,
							   pData->m_pSegNeighbors);

	const U64 endTime = TimerGetRawCount();
	pData->m_duration = endTime - startTime;

	pData->m_pManuallyAssociatedCounter->Decrement();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void CalculateNumBatches(const U32F numNavPolys, U32F& outNumBatches, U32F& outBatchSize)
{
	const float fNumNavPolys = static_cast<float>(numNavPolys);
	const float fIdealNumBatches = ceilf(fNumNavPolys / static_cast<float>(kNavPolyBatchSize));
	const float fNumBatches = Min(fIdealNumBatches, static_cast<float>(kMaxNumNavPolyBatches));
	
	outNumBatches = static_cast<U32F>(fNumBatches);
	NAV_ASSERT(outNumBatches >= 1);

	outBatchSize = static_cast<U32F>(ceilf(fNumNavPolys / fNumBatches));
	NAV_ASSERT(outBatchSize >= 1);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ProcessIslandNavPolys(const NavPolyBlockerIntersections* const pIntersections,
								  const NavPolyBlockerIslands::Island* const pIsland,
								  const U32F numNavBlockers,
								  const PrimitiveMapTable* const pNavBlockerLocalToGlobal,
								  const NavMeshPatchInput* const pPatchInput,
								  const NavMesh* const pNavMeshMother,
								  const Point* const pGVP,
								  const ExternalBitArray* const pGVO,
								  const U32F numVerts,
								  const U32* const pSegs,
								  const U32F numSegs,
								  const U32F vIntersectionGlobalIdx,
								  const U32F numUniqueNavPolySegs,
								  const U32F invalidOrgBoundaryKey,
								  const I16* const pOrgBoundaryTable,
								  const SegmentMapTable& segIdxTable,
								  SegmentNeighbors* const pOutSegNeighbors)
{
	const U32F numNavPolys = pIsland->m_numNavPolys;
	if (!numNavPolys)
	{
		return;
	}

	if (numNavPolys <= kNavPolyBatchSize)	// No need to kick any batch job, do it in place
	{
		ProcessIslandNavPolysBatch(pIntersections,
								   pIsland,
								   numNavBlockers,
								   pNavBlockerLocalToGlobal,
								   pPatchInput,
								   pNavMeshMother,
								   0,
								   numNavPolys - 1,
								   pGVP,
								   pGVO,
								   numVerts,
								   pSegs,
								   numSegs,
								   vIntersectionGlobalIdx,
								   numUniqueNavPolySegs,
								   invalidOrgBoundaryKey,
								   pOrgBoundaryTable,
								   &segIdxTable,
								   pOutSegNeighbors);
	}
	else	// Kick one job per batch
	{
		U32F numBatches;
		U32F batchSize;
		CalculateNumBatches(numNavPolys, numBatches, batchSize);

		const U64 affinity = FALSE_IN_FINAL_BUILD(g_navOptions.m_navMeshPatch.m_serialNavMeshPatching) ?
												(1ULL << ndjob::GetCurrentWorkerThreadIndex()) :
												ndjob::Affinity::kMask_Cluster0;

		const ndjob::Priority priority = FALSE_IN_FINAL_BUILD(g_navOptions.m_navMeshPatch.m_serialNavMeshPatching) ?
															ndjob::Priority::kHighest :
															ndjob::GetActiveJobPriority();

		const ndjob::JobArrayHandle jobArray = ndjob::BeginJobArray(numBatches, priority, affinity);
		const ndjob::CounterHandle pJobCounter = ndjob::AllocateCounter(FILE_LINE_FUNC, numBatches);

		AllocateJanitor kk(kAllocSingleGameFrame, FILE_LINE_FUNC);

		ProcessIslandNavPolysBatchJobData* const pJobParams = NDI_NEW ProcessIslandNavPolysBatchJobData[numBatches];
		ndjob::JobDecl* const pProcessJobDecls = STACK_ALLOC_ALIGNED(ndjob::JobDecl, numBatches, kAlign64);

		for (U32F iBatch = 0; iBatch < numBatches; iBatch++)
		{
			const U32F iPolyStart = iBatch * batchSize;
			const U32F iPolyNextStart = iPolyStart + batchSize;

			const U32F iPolyEnd = iPolyNextStart < numNavPolys ?
								iPolyNextStart - 1 :
								numNavPolys - 1;

			ProcessIslandNavPolysBatchJobData& jobParams = pJobParams[iBatch];

			jobParams.m_pIntersections = pIntersections;
			jobParams.m_pIsland = pIsland;
			jobParams.m_numNavBlockers = numNavBlockers;
			jobParams.m_pNavBlockerLocalToGlobal = pNavBlockerLocalToGlobal;
			jobParams.m_pPatchInput = pPatchInput;
			jobParams.m_pNavMeshMother = pNavMeshMother;
			jobParams.m_navPolyLocalStartIdx = iPolyStart;
			jobParams.m_navPolyLocalEndIdx = iPolyEnd;
			jobParams.m_pGVP = pGVP;
			jobParams.m_pGVO = pGVO;
			jobParams.m_numVerts = numVerts;
			jobParams.m_pSegs = pSegs;
			jobParams.m_numSegs = numSegs;
			jobParams.m_vIntersectionGlobalIdx = vIntersectionGlobalIdx;
			jobParams.m_numUniqueNavPolySegs = numUniqueNavPolySegs;
			jobParams.m_invalidOrgBoundaryKey = invalidOrgBoundaryKey;
			jobParams.m_pOrgBoundaryTable = pOrgBoundaryTable;
			jobParams.m_pSegIdxTable = &segIdxTable;
			jobParams.m_pSegNeighbors = pOutSegNeighbors;
			jobParams.m_pManuallyAssociatedCounter = pJobCounter;
			jobParams.m_duration = 0;

			pProcessJobDecls[iBatch] = ndjob::JobDecl(ProcessIslandNavPolysBatchEntryPoint, (uintptr_t)&pJobParams[iBatch]);
			pProcessJobDecls[iBatch].m_flags = ndjob::kRequireLargeStack | ndjob::kDisallowSleep;
		}

		ndjob::AddJobs(jobArray, pProcessJobDecls, numBatches);
		ndjob::CommitJobArray(jobArray);

		ndjob::WaitForCounter(pJobCounter);
		ndjob::FreeCounter(pJobCounter);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ProcessIsland(const NavPolyBlockerIntersections* const pIntersections,
						  const NavPolyBlockerIslands* const pIslands,
						  const U32F islandIdx,
						  const NavMeshPatchInput* const pPatchInput)
{
	PROFILE_ACCUM(ProcessIsland);

	if (g_navOptions.m_navMeshPatch.m_navMeshPatchIslandIdx >= 0 &&
		g_navOptions.m_navMeshPatch.m_navMeshPatchIslandIdx != islandIdx)
	{
		return;
	}

	AllocateJanitor kk(kAllocSingleGameFrame, FILE_LINE_FUNC);

	const NavPolyBlockerIslands::Island* const pIsland = &pIslands->m_pIslands[islandIdx];

	// Map primitives from global(intersections) to local(island)
	const U32F numNavPolys = pIsland->m_numNavPolys;

	U32F navPolyGlobalIdx;
	U32F navPolyLocalIdx;

	const U64 numMaxNavBlockers = pIsland->m_navBlockers.CountSetBits();

	PrimitiveMapTable navBlockerLocalToGlobal;
	navBlockerLocalToGlobal.Init(numMaxNavBlockers, FILE_LINE_FUNC);

	U32F navBlockerGlobalIdx;
	U32F navBlockerLocalIdx = 0;
	for (navBlockerGlobalIdx = pIsland->m_navBlockers.FindFirstSetBit(); navBlockerGlobalIdx < kMaxNavBlockers;
		 navBlockerGlobalIdx = pIsland->m_navBlockers.FindNextSetBit(navBlockerGlobalIdx))
	{
		const DynamicNavBlocker* const pNavBlocker = &pPatchInput->m_pBlockers[navBlockerGlobalIdx];
		NAV_ASSERT(pNavBlocker);

		const NavMesh* const pNavBlockerMesh = pNavBlocker->GetNavMesh();
		if (pNavBlockerMesh) // Skip nav blockers without a valid nav mesh.
		{
			navBlockerLocalToGlobal.Add(navBlockerLocalIdx, navBlockerGlobalIdx);
			navBlockerLocalIdx++;
		}
	}
	const U64 numNavBlockers = navBlockerLocalIdx;

	// Solve everything in the mother's space
	navPolyGlobalIdx = pIsland->m_navPolys[0];
	const NavPoly* const pNavPolyMother = pIntersections->m_pNavPolys[navPolyGlobalIdx];
	const NavMesh* const pNavMeshMother = pNavPolyMother->GetNavMesh();

#ifndef FINAL_BUILD
	CalculateDebugDrawYOffsets(pIntersections, pNavMeshMother, const_cast<NavPolyBlockerIslands::Island*>(pIsland));
#endif

	// Calculate primitive-blocker intersect-ability, as not every primitive intersects every blocker
	const U32F numPrimitives = numNavPolys + numNavBlockers;

	const U32F numCanIntersectBits = numPrimitives * numPrimitives;
	const U32F numCanIntersectBlockers = ExternalBitArray::DetermineNumBlocks(numCanIntersectBits);
	U64* const pCanIntersectBlocks	   = NDI_NEW U64[numCanIntersectBlockers];
	ExternalBitArray canIntersect;
	canIntersect.Init(numCanIntersectBits, pCanIntersectBlocks);
	canIntersect.ClearAllBits();

	for (navPolyLocalIdx = 0; navPolyLocalIdx < numNavPolys; navPolyLocalIdx++)
	{
		navPolyGlobalIdx = pIsland->m_navPolys[navPolyLocalIdx];
		const NavBlockerBits& navPolyToBlockers = pIntersections->m_pNavPolyToBlockers[navPolyGlobalIdx];

		for (U32F navBlockerGlobalIdx0 = navPolyToBlockers.FindFirstSetBit(); navBlockerGlobalIdx0 < kMaxNavBlockers;
			 navBlockerGlobalIdx0	   = navPolyToBlockers.FindNextSetBit(navBlockerGlobalIdx0))
		{
			U32F foundNavBlockerLocalIdx0 = numNavBlockers;
			for (navBlockerLocalIdx = 0; navBlockerLocalIdx < numNavBlockers; navBlockerLocalIdx++)
			{
				navBlockerGlobalIdx = navBlockerLocalToGlobal.Find(navBlockerLocalIdx)->m_data;
				if (navBlockerGlobalIdx == navBlockerGlobalIdx0)
				{
					foundNavBlockerLocalIdx0 = navBlockerLocalIdx;
					break;
				}
			}

			if (foundNavBlockerLocalIdx0 == numNavBlockers) // foundNavBlockerLocalIdx0 not on island
			{
				continue;
			}

			U32F pos0 = navPolyLocalIdx;
			U32F pos1 = numNavPolys + foundNavBlockerLocalIdx0;

			U32F intersectBit0 = pos0 * numPrimitives + pos1; // Nav poly-nav blocker intersect
			U32F intersectBit1 = pos1 * numPrimitives + pos0; // Nav poly-nav blocker intersect
			canIntersect.SetBit(intersectBit0);
			canIntersect.SetBit(intersectBit1);

			for (U32F navBlockerGlobalIdx1 = navPolyToBlockers.FindFirstSetBit(); navBlockerGlobalIdx1 < kMaxNavBlockers;
				 navBlockerGlobalIdx1	   = navPolyToBlockers.FindNextSetBit(navBlockerGlobalIdx1))
			{
				if (navBlockerGlobalIdx0 == navBlockerGlobalIdx1) // Same blocker
				{
					continue;
				}

				U32F foundNavBlockerLocalIdx1 = numNavBlockers;
				for (navBlockerLocalIdx = 0; navBlockerLocalIdx < numNavBlockers; navBlockerLocalIdx++)
				{
					navBlockerGlobalIdx = navBlockerLocalToGlobal.Find(navBlockerLocalIdx)->m_data;
					if (navBlockerGlobalIdx == navBlockerGlobalIdx1)
					{
						foundNavBlockerLocalIdx1 = navBlockerLocalIdx;
						break;
					}
				}

				if (foundNavBlockerLocalIdx1 == numNavBlockers) // foundNavBlockerLocalIdx1 not on island
				{
					continue;
				}

				pos0 = numNavPolys + foundNavBlockerLocalIdx0;
				pos1 = numNavPolys + foundNavBlockerLocalIdx1;

				intersectBit1 = pos0 * numPrimitives + pos1; // Nav blocker-nav blocker intersect
				intersectBit1 = pos1 * numPrimitives + pos0; // Nav blocker-nav blocker intersect
				canIntersect.SetBit(intersectBit0);
				canIntersect.SetBit(intersectBit1);
			}
		}
	}

	// Global vert positions(GVP)
	const U32F numNavPolyVerts	  = numNavPolys * 4;
	const U32F numNavBlockerVerts = numNavBlockers * 4;
	const U32F numMaxVerts		  = numNavPolyVerts + numNavBlockerVerts + kMaxNumIntersections;

	Point* const pGVP = NDI_NEW Point[numMaxVerts];

	// Global vert owners(GVO)
	const U32F numOwnerBits	  = numPrimitives;
	const U32F numOwnerBlocks = ExternalBitArray::DetermineNumBlocks(numOwnerBits);

	U64* const pVertOwnerBlocks	 = NDI_NEW U64[numOwnerBlocks * numMaxVerts];
	ExternalBitArray* const pGVO = NDI_NEW ExternalBitArray[numMaxVerts];
	for (U32F vGlobalIdx = 0; vGlobalIdx < numMaxVerts; vGlobalIdx++)
	{
		ExternalBitArray& vertOwners = pGVO[vGlobalIdx];

		const U32F blockIdx = vGlobalIdx * numOwnerBlocks;
		vertOwners.Init(numOwnerBits, &pVertOwnerBlocks[blockIdx]);
	}

	// Unique nav blocker clip polys
	Clip2D::Poly* const pNavBlockerClipPolysMs = NDI_NEW Clip2D::Poly[numNavBlockers];

	for (navBlockerLocalIdx = 0; navBlockerLocalIdx < numNavBlockers; navBlockerLocalIdx++)
	{
		navBlockerGlobalIdx = navBlockerLocalToGlobal.Find(navBlockerLocalIdx)->m_data;
		const DynamicNavBlocker* const pNavBlocker = &pPatchInput->m_pBlockers[navBlockerGlobalIdx];
		NAV_ASSERT(pNavBlocker);
		const NavMesh* const pNavBlockerMesh = pNavBlocker->GetNavMesh();

		const Point posPs = pNavBlocker->GetPosPs();

		Clip2D::Poly& navBlockerClipPolyMs = pNavBlockerClipPolysMs[navBlockerLocalIdx];

		U32F iV0;
		for (iV0 = 0; iV0 < 4; iV0++)
		{
			const Point vPs = posPs + (pNavBlocker->GetQuadVertex(iV0) - kOrigin);
			Point vMs;

			if (pNavMeshMother == pNavBlockerMesh)
			{
				vMs = pNavMeshMother->ParentToLocal(vPs);
			}
			else
			{
				const Point vWs = pNavBlockerMesh->ParentToWorld(vPs);
				vMs = pNavMeshMother->WorldToLocal(vWs);
			}

			navBlockerClipPolyMs.PushVertex(vMs, 100 + iV0);
		}

		navBlockerClipPolyMs.FixupWinding();
	}

	// Unique nav poly segments
	const U32F numNavPolySegs	  = numNavPolyVerts;
	U32* const pNavPolySegIndices = NDI_NEW U32F[numNavPolySegs * 2];
	FeatureKey* const pNavPolySegVertKeys = NDI_NEW FeatureKey[numNavPolySegs * 2];
	U32F numUniqueNavPolySegs = 0;

	// Unique nav poly clip polys
	Clip2D::Poly* const pNavPolyClipPolysMs = NDI_NEW Clip2D::Poly[numNavPolys];

	U32F gvWriteIdx = 0;
	BuildNavPolys(pIntersections,
				  pIsland,
				  pNavMeshMother,
				  pNavBlockerClipPolysMs,
				  numNavBlockers,
				  canIntersect,
				  pGVP,
				  pGVO,
				  gvWriteIdx,
				  pNavPolySegIndices,
				  pNavPolySegVertKeys,
				  numUniqueNavPolySegs,
				  pNavPolyClipPolysMs);

	PrimServerWrapper ps(pNavMeshMother->GetParentSpace());

	const U32F numUniqueNavPolyVerts = gvWriteIdx;

	// Unique nav blocker segments
	const U32F numNavBlockerSegs	 = numNavBlockerVerts;
	U32* const pNavBlockerSegIndices = NDI_NEW U32F[numNavBlockerSegs * 2];
	U32F numUniqueNavBlockerSegs	 = 0;

	for (U32F navBlockerLocalIdx0 = 0; navBlockerLocalIdx0 < numNavBlockers; navBlockerLocalIdx0++)
	{
		const Clip2D::Poly& navBlockerClipPolyMs = pNavBlockerClipPolysMs[navBlockerLocalIdx0];

		U32F iV0 = 0;
		for (U32F c = 0; c < 4; c++)
		{
			const U32F iV1		   = navBlockerClipPolyMs.GetNextVertexIndex(iV0);
			const U32F vGlobalIdx0 = numUniqueNavPolyVerts + (navBlockerLocalIdx0 * 4) + iV0;
			const U32F vGlobalIdx1 = numUniqueNavPolyVerts + (navBlockerLocalIdx0 * 4) + iV1;

			const U32F primitiveIdx = numNavPolys + navBlockerLocalIdx0;

			const Point& vMs  = navBlockerClipPolyMs.GetVertex(iV0);
			pGVP[vGlobalIdx0] = Point(vMs.X(), 0.0f, vMs.Z()); // Drop y

			pGVO[vGlobalIdx0].ClearAllBits();
			pGVO[vGlobalIdx0].SetBit(primitiveIdx);

			U32F pos0;
			U32F pos1;
			U32F intersectBit;

			pos0 = numNavPolys + navBlockerLocalIdx0;
			for (U32F navBlockerLocalIdx1 = 0; navBlockerLocalIdx1 < numNavBlockers; navBlockerLocalIdx1++)
			{
				if (navBlockerLocalIdx0 == navBlockerLocalIdx1)
				{
					continue;
				}

				pos1		 = numNavPolys + navBlockerLocalIdx1;
				intersectBit = pos0 * numPrimitives + pos1;
				if (!canIntersect.IsBitSet(intersectBit))
				{
					continue;
				}

				if (IsPointInsideClipPoly(vMs, pNavBlockerClipPolysMs[navBlockerLocalIdx1]))
				{
					pGVO[vGlobalIdx0].SetBit(pos1);
				}
			}

			gvWriteIdx++;

			const U32F idx0 = numUniqueNavBlockerSegs * 2;
			const U32F idx1 = idx0 + 1;

			pNavBlockerSegIndices[idx0] = vGlobalIdx0;
			pNavBlockerSegIndices[idx1] = vGlobalIdx1;

			numUniqueNavBlockerSegs++;

			iV0 = iV1;
		}
	}

	// Intersect nav blockers
	U32F numPreNavBlockerVerts	  = 4;
	const U32* pPreNavBlockerSegs = &pNavBlockerSegIndices[0];
	U32F numPreNavBlockerSegs	  = 4;

	U32F invalidOrgBoundaryKey;
	I16* pOrgBoundaryTable;

	const U32F numOrgVerts		= numUniqueNavPolyVerts + numNavBlockerVerts;
	U32F vIntersectionGlobalIdx = numOrgVerts;

	I32F intersectOrder = 0;

	for (navBlockerLocalIdx = 1; navBlockerLocalIdx < numNavBlockers; navBlockerLocalIdx++)
	{
		const U32* pCurrNavBlockerSegs = &pNavBlockerSegIndices[navBlockerLocalIdx * 4 * 2];

		U32F numBlockerVerts;
		U32* pBlockerSegs;
		U32F numBlockerSegs;
		IntersectSegments(pIsland,
						  numNavBlockers,
						  pNavMeshMother,
						  canIntersect,
						  pGVP,
						  pGVO,
						  numPreNavBlockerVerts,
						  4,
						  pPreNavBlockerSegs,
						  numPreNavBlockerSegs,
						  pCurrNavBlockerSegs,
						  4,
						  vIntersectionGlobalIdx,
						  numBlockerVerts,
						  &pBlockerSegs,
						  numBlockerSegs,
						  invalidOrgBoundaryKey,
						  &pOrgBoundaryTable,
						  intersectOrder);

		numPreNavBlockerVerts = numBlockerVerts;
		pPreNavBlockerSegs	  = pBlockerSegs;
		numPreNavBlockerSegs  = numBlockerSegs;
		intersectOrder++;

		NAV_ASSERT((vIntersectionGlobalIdx - numOrgVerts) < kMaxNumIntersections);
	}

	// Expand the owners of nav blocker verts
	U64* const pDummyOwnerBlocks = NDI_NEW U64[numOwnerBlocks];
	ExternalBitArray dummyOwners;
	dummyOwners.Init(numOwnerBits, &pDummyOwnerBlocks[0]);

	for (U32F iSeg = 0; iSeg < numPreNavBlockerSegs; iSeg++)
	{
		const U32F idx0 = iSeg * 2;

		const U32F vGlobalIdx0 = pPreNavBlockerSegs[idx0];

		const Point& vMs = pGVP[vGlobalIdx0];
		ExternalBitArray& vertOwners = pGVO[vGlobalIdx0];

		for (navPolyLocalIdx = 0; navPolyLocalIdx < numNavPolys; navPolyLocalIdx++)
		{
			if (vertOwners.IsBitSet(navPolyLocalIdx))
			{
				continue;
			}

			dummyOwners.ClearAllBits();
			dummyOwners.SetBit(navPolyLocalIdx);
			if (!CanIntersect(dummyOwners, vertOwners, canIntersect))
			{
				continue;
			}

			if (IsPointInsideClipPoly(vMs, pNavPolyClipPolysMs[navPolyLocalIdx]))
			{
				pGVO[vGlobalIdx0].SetBit(navPolyLocalIdx);
			}
		}
	}

	// Intersect nav polys and nav blockers
	U32F numVerts;
	U32* pSegs;
	U32F numSegs;
	IntersectSegments(pIsland,
					  numNavBlockers,
					  pNavMeshMother,
					  canIntersect,
					  pGVP,
					  pGVO,
					  numUniqueNavPolyVerts,
					  numPreNavBlockerVerts,
					  pNavPolySegIndices,
					  numUniqueNavPolySegs,
					  pPreNavBlockerSegs,
					  numPreNavBlockerSegs,
					  vIntersectionGlobalIdx,
					  numVerts,
					  &pSegs,
					  numSegs,
					  invalidOrgBoundaryKey,
					  &pOrgBoundaryTable,
					  intersectOrder);

	intersectOrder++;

	SegmentMapTable segIdxTable;
	segIdxTable.Init(numSegs, FILE_LINE_FUNC);
	segIdxTable.Clear();

	SegmentNeighbors* const pSegNeighbors = NDI_NEW SegmentNeighbors[numSegs];

	for (U32F iSeg = 0; iSeg < numSegs; iSeg++)
	{
		const U32F idx0 = iSeg * 2;
		const U32F idx1 = idx0 + 1;

		const U32F vGlobalIdx0 = pSegs[idx0];
		const U32F vGlobalIdx1 = pSegs[idx1];

		if (vGlobalIdx0 < vGlobalIdx1)
		{
			segIdxTable.Add({ vGlobalIdx0, vGlobalIdx1 }, iSeg);
		}
		else
		{
			segIdxTable.Add({ vGlobalIdx1, vGlobalIdx0 }, iSeg);
		}

		SegmentNeighbor& neighbor0 = pSegNeighbors[iSeg].m_neighbor0;
		neighbor0.m_id.Invalidate();

		SegmentNeighbor& neighbor1 = pSegNeighbors[iSeg].m_neighbor1;
		neighbor1.m_id.Invalidate();
	}

	ProcessIslandNavPolys(pIntersections,
						  pIsland,
						  numNavBlockers,
						  &navBlockerLocalToGlobal,
						  pPatchInput,
						  pNavMeshMother,
						  pGVP,
						  pGVO,
						  numVerts,
						  pSegs,
						  numSegs,
						  vIntersectionGlobalIdx,
						  numUniqueNavPolySegs,
						  invalidOrgBoundaryKey,
						  pOrgBoundaryTable,
						  segIdxTable,
						  pSegNeighbors);

	// Fix up ex poly neighbors for nav poly boundaries
	for (U32F iSeg = 0; iSeg < numSegs; iSeg++)
	{
		const SegmentNeighbor& neighbor0 = pSegNeighbors[iSeg].m_neighbor0;
		const SegmentNeighbor& neighbor1 = pSegNeighbors[iSeg].m_neighbor1;

		if (!neighbor0.m_id.IsValid() && !neighbor1.m_id.IsValid())
		{
			continue; // Interior nav poly edge
		}

		if (!neighbor0.m_id.IsValid() || // neighbor1 has no neighbor ex poly
			!neighbor1.m_id.IsValid())	 // neighbor0 has no neighbor ex poly
		{
			NavPolyEx* pExPoly;
			U32F vIdx;
			if (!neighbor0.m_id.IsValid())
			{
				pExPoly = g_navExData.GetNavPolyExFromId(neighbor1.m_id.m_iPolyEx);
				vIdx	= neighbor1.m_vIdx;
			}
			else
			{
				pExPoly = g_navExData.GetNavPolyExFromId(neighbor0.m_id.m_iPolyEx);
				vIdx	= neighbor0.m_vIdx;
			}
			NAV_ASSERT(pExPoly);

			const U32F idx0 = iSeg * 2;
			const U32F idx1 = idx0 + 1;

			const U32F vGlobalIdx0 = pSegs[idx0];
			const U32F vGlobalIdx1 = pSegs[idx1];

			const U32F sTableIdx  = vGlobalIdx0 * vIntersectionGlobalIdx + vGlobalIdx1;
			const I32 orgBoundary = pOrgBoundaryTable[sTableIdx];
			if (orgBoundary < 0 || orgBoundary == invalidOrgBoundaryKey)
			{
#ifndef FINAL_BUILD
				OnNavMeshPatchFailure(pNavMeshMother, FILE_LINE_FUNC);
#endif
				pExPoly->m_adjPolys[vIdx].Invalidate();
				continue;
			}

			// Use the first vert key of this original boundary
			const FeatureKey foundVertKey = pNavPolySegVertKeys[orgBoundary * 2];

			navPolyGlobalIdx = pIsland->m_navPolys[foundVertKey.m_subKey0];
			const NavPoly* const pFoundNavPoly = pIntersections->m_pNavPolys[navPolyGlobalIdx];
			const U32F adjId = pFoundNavPoly->GetAdjacentId(foundVertKey.m_subKey1);

			if (adjId != NavPoly::kNoAdjacent)
			{
				const NavMesh* const pFoundNavMesh = pFoundNavPoly->GetNavMesh();
				const NavPoly* const pAdjPoly	   = &pFoundNavMesh->GetPoly(adjId);

				const NavPoly* const pNeighborToLink = pAdjPoly->IsLink() ? pAdjPoly->GetLinkedPoly() : pAdjPoly;
				if (pNeighborToLink && pNeighborToLink->IsValid())
				{
					pExPoly->m_adjPolys[vIdx] = pNeighborToLink->GetNavManagerId();
				}
				else
				{
					pExPoly->m_adjPolys[vIdx].Invalidate();
				}
			}
			else
			{
				pExPoly->m_adjPolys[vIdx].Invalidate();
			}
		}
		else // Connect neighbor0 and neighbor1
		{
			NavPolyEx* const pExPoly0 = g_navExData.GetNavPolyExFromId(neighbor0.m_id.m_iPolyEx);
			NavPolyEx* const pExPoly1 = g_navExData.GetNavPolyExFromId(neighbor1.m_id.m_iPolyEx);
			NAV_ASSERT(pExPoly0);
			NAV_ASSERT(pExPoly1);

			pExPoly0->m_adjPolys[neighbor0.m_vIdx] = neighbor1.m_id;
			pExPoly1->m_adjPolys[neighbor1.m_vIdx] = neighbor0.m_id;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ProcessIntersection(NavPolyHandle hPoly,
								const NavBlockerBits& blockerBits,
								const NavMeshPatchInput* pPatchInput,
								const Clip2D::Poly* pBlockerClipPolysWs)
{
	PROFILE_ACCUM(ProcessIntersection);

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	const NavPoly* pPoly = hPoly.ToNavPoly();
	const NavMesh* pMesh = pPoly->GetNavMesh();

	NAV_ASSERT(pPoly);
	NAV_ASSERT(pMesh);
	if (!pPoly || !pMesh)
		return;

	const U32F maxEntries = blockerBits.CountSetBits();
	NavBlockerClipperEntry* pBlockerClipperEntriesLs = NDI_NEW NavBlockerClipperEntry[maxEntries];

	Clip2D::Poly navClipPolyLs = MakeClipperPolyFromNavPolyLs(pPoly);

	NavBlockerBits initialPolyBlockage[Clip2D::Poly::kMaxVerts];
	// memset(initialPolyBlockage, 0, sizeof(NavBlockerBits) * Clip2D::Poly::kMaxVerts);

	for (U32F iV = 0; iV < navClipPolyLs.m_numVerts; ++iV)
	{
		initialPolyBlockage[iV].ClearAllBits();
	}

	const U32F numEntries = IntersectBlockerNavPoly(pMesh,
													pPoly,
													blockerBits,
													pPatchInput,
													pBlockerClipPolysWs,
													&navClipPolyLs,
													pBlockerClipperEntriesLs,
													maxEntries,
													initialPolyBlockage);

	IntersectBlockerBlocker(pBlockerClipperEntriesLs, numEntries);

	FlagExternalBlockerVerts(pMesh, pPoly, pBlockerClipperEntriesLs, numEntries);

	const bool success = ClipNavPolyFromBlockers(pMesh,
												 pPoly,
												 navClipPolyLs,
												 initialPolyBlockage,
												 pBlockerClipperEntriesLs,
												 numEntries);

#ifndef JBELLOMY
	if (FALSE_IN_FINAL_BUILD(!success))
	{
		MsgOut("===============================================\n");
		MsgOut("Processing poly %d%s\n", pPoly->GetId(), success ? "" : " FAILED");
		MsgOut("NavMesh %s\n", pMesh->GetName());
		MsgOut("Level %s\n", DevKitOnly_StringIdToString(pMesh->GetLevelId()));

		MsgOut("Blocker Indices: ");
		for (U64 iBlocker = blockerBits.FindFirstSetBit(); iBlocker < kMaxDynamicNavBlockerCount;
			 iBlocker	  = blockerBits.FindNextSetBit(iBlocker))
		{
			MsgOut("  %d : (mgr idx: %d)\n", iBlocker, pPatchInput->m_pMgrIndices[iBlocker]);
		}
		MsgOut("===============================================\n");

		MAIL_ASSERT(Navigation, success, ("Nav mesh patching failed (see tty)"));
	}
#endif

	if (FALSE_IN_FINAL_BUILD(!success))
	{
		DebugPrimTime tt = kPrimDuration1Frame;

		DebugDrawClipPoly(LocalToWorld(pMesh, navClipPolyLs), 1.0f, kColorGreen, tt);

		for (U32F i = 0; i < numEntries; ++i)
		{
			const float vo = 1.0f + (float(i + 1) * 0.5f);

			const Point v0Ls = pBlockerClipperEntriesLs[i].m_clipPoly.GetVertex(0) + Vector(0.0f, vo, 0.0f);
			g_prim.Draw(DebugString(pMesh->LocalToWorld(v0Ls), StringBuilder<64>("%d", (int)i).c_str()));

			DebugDrawClipPoly(LocalToWorld(pMesh, pBlockerClipperEntriesLs[i].m_clipPoly), vo, AI::IndexToColor(i), tt);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct ProcessIslandJobData
{
	const NavPolyBlockerIntersections* m_pIntersections;
	const NavPolyBlockerIslands* m_pIslands;
	const NavMeshPatchInput* m_pPatchInput;
	U32F m_islandIdx;
	mutable ndjob::CounterHandle m_pManuallyAssociatedCounter;
	mutable U64 m_duration;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ProcessIntersectionJobData
{
	NavPolyHandle m_hPoly;
	NavBlockerBits m_blockerBits;
	const NavMeshPatchInput* m_pPatchInput;
	const Clip2D::Poly* m_pBlockerClipPolysWs;
	mutable ndjob::CounterHandle m_pManuallyAssociatedCounter;
	mutable U64 m_duration;
};

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT(ProcessIslandJobEntryPoint)
{
	const ProcessIslandJobData* const pData = (const ProcessIslandJobData*)jobParam;

	const U64 startTime = TimerGetRawCount();
	ProcessIsland(pData->m_pIntersections, pData->m_pIslands, pData->m_islandIdx, pData->m_pPatchInput);
	const U64 endTime = TimerGetRawCount();

	pData->m_duration = endTime - startTime;

	pData->m_pManuallyAssociatedCounter->Decrement();
}

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT(ProcessIntersectionJobEntryPoint)
{
	const ProcessIntersectionJobData* const pData = (const ProcessIntersectionJobData*)jobParam;

	const U64 startTime = TimerGetRawCount();

	ProcessIntersection(pData->m_hPoly, pData->m_blockerBits, pData->m_pPatchInput, pData->m_pBlockerClipPolysWs);

	const U64 endTime = TimerGetRawCount();
	pData->m_duration = endTime - startTime;

	pData->m_pManuallyAssociatedCounter->Decrement();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ProcessIslands(const NavPolyBlockerIntersections& intersections,
						   const NavPolyBlockerIslands& islands,
						   const NavMeshPatchInput& patchInput,
						   U64* const pDurationOut)
{
	const U32F numIslands = islands.m_numIslands;
	if (!numIslands)
	{
		return;
	}

	const U64 affinity = FALSE_IN_FINAL_BUILD(g_navOptions.m_navMeshPatch.m_serialNavMeshPatching)
							 ? (1ULL << ndjob::GetCurrentWorkerThreadIndex())
							 : ndjob::Affinity::kMask_Cluster0;

	const ndjob::Priority priority = FALSE_IN_FINAL_BUILD(g_navOptions.m_navMeshPatch.m_serialNavMeshPatching)
										 ? ndjob::Priority::kHighest
										 : ndjob::GetActiveJobPriority();

	const ndjob::JobArrayHandle jobArray   = ndjob::BeginJobArray(numIslands, priority, affinity);
	const ndjob::CounterHandle pJobCounter = ndjob::AllocateCounter(FILE_LINE_FUNC, numIslands);

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	ProcessIslandJobData* const pJobParams = NDI_NEW ProcessIslandJobData[numIslands];
	ndjob::JobDecl* const pProcessJobDecls = STACK_ALLOC_ALIGNED(ndjob::JobDecl, numIslands, kAlign64);

	for (U32F i = 0; i < numIslands; ++i)
	{
		pJobParams[i].m_pIntersections = &intersections;
		pJobParams[i].m_pIslands	   = &islands;
		pJobParams[i].m_islandIdx	   = i;
		pJobParams[i].m_pPatchInput	   = &patchInput;
		pJobParams[i].m_pManuallyAssociatedCounter = pJobCounter;
		pJobParams[i].m_duration = 0;

		pProcessJobDecls[i]			= ndjob::JobDecl(ProcessIslandJobEntryPoint, (uintptr_t)&pJobParams[i]);

		const NavPolyBlockerIslands::Island& island = islands.m_pIslands[i];
		const U32F numNavPolys = island.m_numNavPolys;
		
		if (numNavPolys <= kNavPolyBatchSize)	// No need to kick any batch job, so disallow sleep.
		{
			pProcessJobDecls[i].m_flags = ndjob::kRequireLargeStack | ndjob::kDisallowSleep;
		}
		else	// Will split nav polys into batches.
		{
			pProcessJobDecls[i].m_flags = ndjob::kRequireLargeStack /*| ndjob::kDisallowSleep*/;
		}
	}

	ndjob::AddJobs(jobArray, pProcessJobDecls, numIslands);
	ndjob::CommitJobArray(jobArray);

	ndjob::WaitForCounter(pJobCounter);
	ndjob::FreeCounter(pJobCounter);

	if (FALSE_IN_FINAL_BUILD(g_navMeshDrawFilter.m_printNavBlockerStats))
	{
		U64 durationTotal = 0;
		for (U32F i = 0; i < numIslands; ++i)
		{
			durationTotal += pJobParams[i].m_duration;
		}

		(*pDurationOut) = durationTotal;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static NavBlockerBits ProcessIntersections(const NavPolyBlockerIntersections& intersections,
										   const NavMeshPatchInput& patchInput,
										   const Clip2D::Poly* pBlockerClipPolysWs,
										   U64* pDurationOut)
{
	NavBlockerBits blockersThatClippedPolys;
	blockersThatClippedPolys.ClearAllBits();

	const size_t numIntersections = intersections.Size();

	if (0 == numIntersections)
	{
		return blockersThatClippedPolys;
	}

	const U64 affinity = FALSE_IN_FINAL_BUILD(g_navOptions.m_navMeshPatch.m_serialNavMeshPatching)
							 ? (1ULL << ndjob::GetCurrentWorkerThreadIndex())
							 : ndjob::Affinity::kMask_Cluster0;

	const ndjob::Priority priority = FALSE_IN_FINAL_BUILD(g_navOptions.m_navMeshPatch.m_serialNavMeshPatching)
										 ? ndjob::Priority::kHighest
										 : ndjob::GetActiveJobPriority();

	ndjob::JobArrayHandle jobArray	 = ndjob::BeginJobArray(numIntersections, priority, affinity);
	ndjob::CounterHandle pJobCounter = ndjob::AllocateCounter(FILE_LINE_FUNC, numIntersections);

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);
	ProcessIntersectionJobData* pJobParams = NDI_NEW ProcessIntersectionJobData[numIntersections];

	ndjob::JobDecl* pProcessJobDecls = STACK_ALLOC_ALIGNED(ndjob::JobDecl, numIntersections, kAlign64);

	U32F count = 0;

	for (U32F i = 0; i < numIntersections; ++i)
	{
		const NavPoly* const pPoly = intersections.m_pNavPolys[i];
		const NavBlockerBits& navPolyToBlockers = intersections.m_pNavPolyToBlockers[i];
		NavBlockerBits::BitwiseOr(&blockersThatClippedPolys, blockersThatClippedPolys, navPolyToBlockers);

		pJobParams[count].m_blockerBits = navPolyToBlockers;
		pJobParams[count].m_hPoly		= pPoly;
		pJobParams[count].m_pPatchInput = &patchInput;
		pJobParams[count].m_pBlockerClipPolysWs		   = pBlockerClipPolysWs;
		pJobParams[count].m_pManuallyAssociatedCounter = pJobCounter;
		pJobParams[count].m_duration = 0;

		pProcessJobDecls[count] = ndjob::JobDecl(ProcessIntersectionJobEntryPoint, (uintptr_t)&pJobParams[count]);
		pProcessJobDecls[count].m_flags = ndjob::kRequireLargeStack | ndjob::kDisallowSleep;
		// pProcessJobDecls[count].m_associatedCounter = pJobCounter;

		++count;
	}

	ndjob::AddJobs(jobArray, pProcessJobDecls, numIntersections);

	ndjob::CommitJobArray(jobArray);

	ndjob::WaitForCounter(pJobCounter);

	ndjob::FreeCounter(pJobCounter);

	if (FALSE_IN_FINAL_BUILD(g_navMeshDrawFilter.m_printNavBlockerStats))
	{
		U64 durationTotal = 0;
		for (U32F i = 0; i < numIntersections; ++i)
		{
			durationTotal += pJobParams[i].m_duration;
		}
		*pDurationOut = durationTotal;
	}

	return blockersThatClippedPolys;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GenerateNavMeshPatch(const NavMeshPatchInput& patchInput)
{
	PROFILE_ACCUM(GenerateNavMeshPatch);
	PROFILE_AUTO(Navigation);

	if (FALSE_IN_FINAL_BUILD(g_navOptions.m_navMeshPatch.m_debugPrintNavMeshPatchBlockers))
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		MsgOut("===============================================\n");
		MsgOut("NavMeshPatch Input Blockers:\n");
		MsgOut("---------\n");

		DebugPrintNavMeshPatchInput(GetMsgOutput(kMsgOut), patchInput);

		MsgOut("===============================================\n");
	}

	NavMeshWriteLockJanitor writeLock(FILE_LINE_FUNC);

	const U64 startTime = TimerGetRawCount();

	U64 processDuration = 0;

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	{
		g_navExData.ResetNavPolyExHeap();

		NavPolyBlockerIntersections& intersections = g_navExData.GetPolyBlockerIntersections();
		intersections.Reset();

		Clip2D::Poly* pBlockerClipPolysWs = nullptr;
		NavPolyList* pBlockerPolys		  = nullptr;

		if (patchInput.m_numBlockers > 0)
		{
			pBlockerClipPolysWs = NDI_NEW Clip2D::Poly[patchInput.m_numBlockers];
			pBlockerPolys		= NDI_NEW NavPolyList[patchInput.m_numBlockers];
		}

		for (U32F iBlocker = 0; iBlocker < patchInput.m_numBlockers; ++iBlocker)
		{
			const DynamicNavBlocker* pBlocker = &patchInput.m_pBlockers[iBlocker];
			if (!pBlocker)
			{
				continue;
			}

			pBlockerClipPolysWs[iBlocker] = MakeClipperPolyFromNavBlockerWs(pBlocker);
		}

		// pre pass, build the navpoly-blocker intersects.
		BuildNavPolyBlockerIntersections(patchInput, &intersections, pBlockerPolys);

		if (TRUE_IN_FINAL_BUILD(g_navOptions.m_navMeshPatch.m_navMeshPatchIslands))	// Patch by islands
		{
			NavPolyBlockerIslands& islands = g_navExData.GetNavPolyBlockerIslands();
			islands.Reset();

			// Build navpoly-blocker islands from the intersections, an island consists of a bunch of connected nav polys and their intersecting nav blockers
			BuildNavPolyBlockerIslands(&intersections, &islands);

			ProcessIslands(intersections, islands, patchInput, &processDuration);

#ifndef FINAL_BUILD
			if (g_navOptions.m_navMeshPatch.m_navMeshPatchIslandsDrawIslands)
			{
				U32F totalIslandNavPolys = 0;
				float avgIslandNavPolys = 0.0f;

				const U32F numIslands = islands.m_numIslands;
				for (U32F i = 0; i < numIslands; i++)
				{
					const NavPolyBlockerIslands::Island* const pIsland = &islands.m_pIslands[i];
					totalIslandNavPolys += pIsland->m_numNavPolys;

					if (pIsland->m_numNavPolys <= kNavPolyBatchSize)
					{
						MsgCon("Island %d: %d nav polys\n", 
							i, 
							pIsland->m_numNavPolys);
					}
					else
					{
						U32F numBatches;
						U32F batchSize;
						CalculateNumBatches(pIsland->m_numNavPolys, numBatches, batchSize);

						MsgCon("Island %d: %d nav polys, %d(%d) batches\n", 
							i, 
							pIsland->m_numNavPolys, 
							numBatches, 
							batchSize);
					}
				}

				if (totalIslandNavPolys > 0)
				{
					avgIslandNavPolys = static_cast<float>(totalIslandNavPolys) / static_cast<float>(numIslands);
				}

				MsgCon("Total: %d, avg: %3.3f nav polys\n", totalIslandNavPolys, avgIslandNavPolys);
			}
#endif

			if (FALSE_IN_FINAL_BUILD(true))
			{
				const U32F numNavPolys = intersections.Size();
				for (U32F iNavPoly = 0; iNavPoly < numNavPolys; iNavPoly++)
				{
					const NavPoly* pPoly = intersections.m_pNavPolys[iNavPoly];
					const NavMesh* pMesh = pPoly->GetNavMesh();

					ValidatePolyNeighbors(pPoly, pMesh);
				}
			}

			// Add path nodes & linkages
			GenerateExPathNodes(&intersections, &islands);

			// Finally generate any gap data we might need for radial path finding
			NavBlockerBits blockersThatClippedPolys;
			blockersThatClippedPolys.ClearAllBits();

			for (U32F i = 0; i < islands.m_numIslands; i++)
			{
				const NavPolyBlockerIslands::Island& island = islands.m_pIslands[i];
				NavBlockerBits::BitwiseOr(&blockersThatClippedPolys, blockersThatClippedPolys, island.m_navBlockers);
			}

			GenerateNavMeshExGaps(patchInput,
								  pBlockerClipPolysWs,
								  pBlockerPolys,
								  blockersThatClippedPolys,
								  kMaxNavMeshGapDiameter);

#ifndef FINAL_BUILD
			g_navExData.ValidateExNeighbors();
#endif
		}
		else // Patch by intersections
		{
			// first pass, intersect and re-triangulate nav polys
			NavBlockerBits blockersThatClippedPolys = ProcessIntersections(intersections,
																		   patchInput,
																		   pBlockerClipPolysWs,
																		   &processDuration);

			// second pass, fixup neighbors
			FixupExNeighbors(&intersections);

			// third pass, add path nodes & linkages
			GenerateExPathNodes(&intersections);

			// finally generate any gap data we might need for radial path finding
			GenerateNavMeshExGaps(patchInput,
								  pBlockerClipPolysWs,
								  pBlockerPolys,
								  blockersThatClippedPolys,
								  kMaxNavMeshGapDiameter);

			// g_navExData.ValidateExNeighbors();

			if (FALSE_IN_FINAL_BUILD(g_navMeshDrawFilter.m_printNavBlockerStats))
			{
				const U64 endTime	   = TimerGetRawCount();
				const float baseTimeMs = ConvertTicksToMilliseconds(endTime - startTime);
				const float intersectionMs = ConvertTicksToMilliseconds(processDuration);
				const float totalTimeMs	   = baseTimeMs + intersectionMs;

				MsgCon("----------------------------------------\n");
				MsgCon("NavMesh Patching:\n");
				MsgCon("Num NavBlockers: %d\n", blockersThatClippedPolys.CountSetBits());
				MsgCon("Num Intersections: %d\n", intersections.Size());
				MsgCon("Total Time: %0.3fms\n", totalTimeMs);
				MsgCon("    Base Time: %0.3fms\n", baseTimeMs);
				MsgCon("    Intersections: %0.3fms\n", intersectionMs);
				MsgCon("----------------------------------------\n");
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void SetBlockerFromDebugPrint(DynamicNavBlocker& blocker,
									 Point_arg posPs,
									 StringId64Storage sidVal,
									 U32F iPoly,
									 Point_arg v0,
									 Point_arg v1,
									 Point_arg v2,
									 Point_arg v3)
{
	const NavMeshMgr& nmMgr = *EngineComponents::GetNavMeshMgr();

	blocker.SetPosPs(posPs);
	blocker.SetNavPoly(nullptr);

	if (const NavMesh* pMesh = nmMgr.FindNavMeshByName(StringId64(sidVal)).ToNavMesh())
	{
		if (iPoly < pMesh->GetPolyCount())
		{
			blocker.SetNavPoly(&pMesh->GetPoly(iPoly));
		}
	}

	Point quad[4];
	quad[0] = v0;
	quad[1] = v1;
	quad[2] = v2;
	quad[3] = v3;

	blocker.SetQuad(quad);
}

/// --------------------------------------------------------------------------------------------------------------- ///
#ifndef FINAL_BUILD
static bool ConstructDebugBlocker(DynamicNavBlocker& blocker, const DebugBlockerInput& blockerInput)
{
	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);
	
	const Point quad[4] = { blockerInput.m_v0, 
							blockerInput.m_v1, 
							blockerInput.m_v2, 
							blockerInput.m_v3 };

	const NavMesh* const pMesh = EngineComponents::GetNavMeshMgr()->GetNavMeshByName(StringId64(blockerInput.m_meshId));
	if (!pMesh)
	{
		return false;
	}

	blocker.SetNavPoly(&pMesh->GetPoly(blockerInput.m_iPoly));
	blocker.SetPosPs(blockerInput.m_pos);
	blocker.SetQuad(quad);

	return true;
}
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT(GenerateNavMeshPatchJobEntryPoint)
{
	NavBlockerMgr& nbMgr = NavBlockerMgr::Get();

	if (g_navPathNodeMgr.Failed())
	{
		return;
	}

	if (g_ndConfig.m_pNetInfo->IsNetActive() && !g_ndConfig.m_pNetInfo->IsTo1())
	{
		return;
	}

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);

	NavMeshPatchInput& patchInput = g_navExData.GetNavMeshPatchInputBuffer();

	static bool overflowed		= false;
	static size_t overflowCount = 0;

	if (!FALSE_IN_FINAL_BUILD(g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchInput))
	{
		NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

		size_t newCount		= 0;
		size_t desiredCount = 0;

		for (const DynamicNavBlocker& blocker : nbMgr.GetDynamicBlockers())
		{
			if (blocker.GetQuadArea() < 0.01f)
				continue;

			++desiredCount;

			if (newCount < patchInput.m_maxBlockers)
			{
				patchInput.m_pBlockers[newCount]   = blocker;
				patchInput.m_pMgrIndices[newCount] = nbMgr.GetNavBlockerIndex(&blocker);
				++newCount;
			}
			else
			{
				overflowed	  = true;
				overflowCount = Max(overflowCount, desiredCount);
			}
		}

		patchInput.m_numBlockers = newCount;
	}

#ifndef FINAL_BUILD
	if (false)
	{
		static DebugBlockerInput s_debugBlockers[kMaxDynamicNavBlockerCount];

		int n = 0;
		s_debugBlockers[n++] = { 0, Point(219.17535f,   13.50082f,  -61.01211f), 0xe271c715bd8491d3, 884, Point(-0.05000f,    0.00000f,   -0.05000f), Point(0.05000f,    0.00000f,   -0.05000f), Point(0.05000f,    0.00000f,    0.05000f), Point(-0.05000f,    0.00000f,    0.05000f) };
		s_debugBlockers[n++] = { 1, Point(232.63063f,   12.76412f,  -84.39417f), 0xe271c715bd8491d3, 1221, Point(-0.05004f,    0.00000f,   -0.05004f), Point(0.05004f,    0.00000f,   -0.05004f), Point(0.05004f,    0.00000f,    0.05004f), Point(-0.05004f,    0.00000f,    0.05004f) };
		s_debugBlockers[n++] = { 2, Point(234.56590f,   12.97148f,  -89.28140f), 0xe271c715bd8491d3, 551, Point(-0.05000f,    0.00000f,   -0.05000f), Point(0.05000f,    0.00000f,   -0.05000f), Point(0.05000f,    0.00000f,    0.05000f), Point(-0.05000f,    0.00000f,    0.05000f) };
		s_debugBlockers[n++] = { 3, Point(234.45604f,   12.90922f,  -91.23635f), 0xe271c715bd8491d3, 507, Point(-0.05000f,    0.00000f,   -0.05000f), Point(0.05000f,    0.00000f,   -0.05000f), Point(0.05000f,    0.00000f,    0.05000f), Point(-0.05000f,    0.00000f,    0.05000f) };
		s_debugBlockers[n++] = { 4, Point(234.93942f,   12.89886f,  -91.83688f), 0xe271c715bd8491d3, 507, Point(-0.50000f,    0.00000f,   -0.50000f), Point(0.50000f,    0.00000f,   -0.50000f), Point(0.50000f,    0.00000f,    0.50000f), Point(-0.50000f,    0.00000f,    0.50000f) };
		s_debugBlockers[n++] = { 6, Point(232.49400f,   12.89016f,  -74.76398f), 0xe271c715bd8491d3, 91, Point(-0.12500f,    0.00000f,   -0.12500f), Point(0.12500f,    0.00000f,   -0.12500f), Point(0.12500f,    0.00000f,    0.12500f), Point(-0.12500f,    0.00000f,    0.12500f) };
		s_debugBlockers[n++] = { 7, Point(233.10086f,   12.81935f,  -74.07298f), 0xe271c715bd8491d3, 84, Point(-0.12500f,    0.00000f,   -0.12500f), Point(0.12500f,    0.00000f,   -0.12500f), Point(0.12500f,    0.00000f,    0.12500f), Point(-0.12500f,    0.00000f,    0.12500f) };
		s_debugBlockers[n++] = { 8, Point(214.76300f,   12.72447f,  -85.32146f), 0xe271c715bd8491d3, 448, Point(-0.12500f,    0.00000f,   -0.12500f), Point(0.12500f,    0.00000f,   -0.12500f), Point(0.12500f,    0.00000f,    0.12500f), Point(-0.12500f,    0.00000f,    0.12500f) };
		s_debugBlockers[n++] = { 9, Point(237.46474f,   12.67830f,  -65.28870f), 0xe271c715bd8491d3, 1134, Point(-0.12500f,    0.00000f,   -0.12500f), Point(0.12500f,    0.00000f,   -0.12500f), Point(0.12500f,    0.00000f,    0.12500f), Point(-0.12500f,    0.00000f,    0.12500f) };
		s_debugBlockers[n++] = { 10, Point(219.66371f,   12.61193f,  -86.06552f), 0xe271c715bd8491d3, 19, Point(-0.12500f,    0.00000f,   -0.12500f), Point(0.12500f,    0.00000f,   -0.12500f), Point(0.12500f,    0.00000f,    0.12500f), Point(-0.12500f,    0.00000f,    0.12500f) };

		const bool* pFlags = nullptr;
		if (false)	// Do this once you know which combination fails
		{
			bool flags[kMaxDynamicNavBlockerCount];
			for (int i = 0; i < n; i++)
			{
				flags[i] = false;
			}

			flags[0] = true;
			flags[1] = true;
			flags[2] = true;

			pFlags = &flags[0];
		}
/*
		else if (s_debugCombination.Next(n))
		{
			pFlags = &s_debugCombination.m_flags[0];
		}
*/

		int numBlockers = 0;
		if (pFlags)
		{
			GetMsgOutput(kMsgOut)->Printf("DebugBlockerCombination(%d, %d) - %d:", s_debugCombination.m_n, s_debugCombination.m_r, s_debugCombination.m_rCount);

			for (int i = 0; i < n; i++)
			{
				if (pFlags[i] &&
					ConstructDebugBlocker(patchInput.m_pBlockers[numBlockers], s_debugBlockers[i]))
				{
					patchInput.m_pMgrIndices[numBlockers] = s_debugBlockers[i].m_mgrIndex;
					numBlockers++;

					GetMsgOutput(kMsgOut)->Printf(" %d", i);
				}
			}
			GetMsgOutput(kMsgOut)->Printf("\n");
		}
		else
		{
			for (int i = 0; i < n; i++)
			{
				if (!ConstructDebugBlocker(patchInput.m_pBlockers[numBlockers], s_debugBlockers[i]))
					continue;

				patchInput.m_pMgrIndices[numBlockers] = s_debugBlockers[i].m_mgrIndex;
				numBlockers++;
			}
		}

		patchInput.m_numBlockers = numBlockers;
	}
#endif

	if (overflowed)
	{
		g_prim.Draw(DebugString2D(Vec2(0.2f, 0.1f),
								  kDebug2DNormalizedCoords,
								  StringBuilder<1024>("Too many nav blockers for dyn. nav mesh (%d).\n Please reduce the amount of nav blockers in your level.",
													  overflowCount)
									  .c_str(),
								  kColorRed,
								  1.0f));
		return;
	}
	else if (!FALSE_IN_FINAL_BUILD(g_navOptions.m_navMeshPatch.m_freezeNavMeshPatchProcessing))
	{
		GenerateNavMeshPatch(patchInput);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void RunGenerateNavMeshPatchJob(ndjob::CounterHandle pDependentCounter, ndjob::CounterHandle pDependentCounter2)
{
	// DoTimingTest();

	static ndjob::JobDecl patchJobDecl(GenerateNavMeshPatchJobEntryPoint, 0);
	ndjob::CounterHandle pJobCounter = nullptr;

	if (FALSE_IN_FINAL_BUILD(g_navOptions.m_navMeshPatch.m_serialNavMeshPatching))
	{
		patchJobDecl.m_flags |= ndjob::kLockToCoreOnceExecuting;
	}

#ifdef HEADLESS_BUILD
	patchJobDecl.m_flags |= ndjob::kRequireLargeStack;
#endif

	patchJobDecl.m_dependentCounter	 = pDependentCounter;
	patchJobDecl.m_dependentCounter2 = pDependentCounter2;
	patchJobDecl.m_associatedCounter = g_navExData.GetWaitForPatchCounter();
	NAV_ASSERT(!patchJobDecl.m_associatedCounter || (patchJobDecl.m_associatedCounter->GetValue() == 1));

	ndjob::JobArrayHandle jobArray = ndjob::BeginJobArray(1, ndjob::Priority::kGameFrameBelowNormal);
	ndjob::AddJobs(jobArray, &patchJobDecl, 1);
	ndjob::CommitJobArray(jobArray);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugPrintNavMeshPatchInput(DoutBase* pOutput, const NavMeshPatchInput& patchInput)
{
	STRIP_IN_FINAL_BUILD;

	if (!pOutput)
	{
		return;
	}

	if (g_navOptions.m_navMeshPatch.m_navMeshPatchIslands)
	{
		pOutput->Printf("Patching by islands\n");
	}
	else
	{
		pOutput->Printf("Patching by intersections\n");
	}

	for (U32F iBlocker = 0; iBlocker < patchInput.m_numBlockers; ++iBlocker)
	{
		const DynamicNavBlocker* const pBlocker = &patchInput.m_pBlockers[iBlocker];
		if (!pBlocker)
		{
			continue;
		}

		const U32F ppFlags = kPrettyPrintAppendF | kPrettyPrintInsertCommas | kPrettyPrintUseParens;

		const NavPoly* const pBlockerPoly = pBlocker->GetNavPoly();
		if (!pBlockerPoly)
		{
			continue;
		}

		const NavMesh* const pBlockerMesh = pBlocker->GetNavMesh();

		pOutput->Printf("s_debugBlockers[n++] = { %d, ", patchInput.m_pMgrIndices[iBlocker]);
		pOutput->Printf("Point%s, 0x%.16llx, %d, ",
						PrettyPrint(pBlocker->GetPosPs(), ppFlags),
						pBlockerMesh ? pBlockerMesh->GetNameId().GetValue() : 0ULL,
						pBlockerPoly ? (int)pBlockerPoly->GetId() : 0);

		pOutput->Printf("Point%s, Point%s, Point%s, Point%s };\n",
						PrettyPrint(pBlocker->GetQuadVertex(0), ppFlags),
						PrettyPrint(pBlocker->GetQuadVertex(1), ppFlags),
						PrettyPrint(pBlocker->GetQuadVertex(2), ppFlags),
						PrettyPrint(pBlocker->GetQuadVertex(3), ppFlags));
	}
}

