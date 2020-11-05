/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/level/entitydb.h"

#include "gamelib/scriptx/h/nd-ai-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class ActionPack;
class NavMesh;
class NavPolyEx;
class EntityDB;
struct NavMeshDrawFilter;
struct NavMeshGapRef;

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavPolyDistEntry
{
public:
	U32F	GetPolyIndex() const { return m_iPoly; }
	F32		GetDist() const;

private:
	U16 m_iPoly;
	U16 m_distU16;

	friend class NavMesh;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NavPoly
{
public:
	static const U32F kMaxVertexCount = 4;
	static const NavPolyId kNoAdjacent = NavPolyId(-1);
	static const U32 kMaxPolyCount = (1ULL << 16) - 1;
	static CONST_EXPR float kMaxPolyTableDist = 10.0f;

	// flags:
	// reverse the bitfield (win32 is backwards from gcc)
	struct Flags
	{
		union
		{
			struct
			{
				bool m_hasExGaps : 1;
				bool m_stealth : 1; // tool controlled
				bool _unused : 3;
				bool m_link : 1;	// user settable
				bool m_preLink : 1;	// tool controlled
				bool m_error : 1;	// tool controlled
			};
			U8 m_u8;
		};
	};

	NavPolyId		GetId() const { return m_id; }

	// I need to load the ID and 2 bytes past it directly into an XMM as fast as possible.
	// Can't do that without the pointer. Don't want to just use the poly pointer, even though
	// that works right now as m_id is the first member, so that we don't silently break if
	// m_id is ever moved.
	// - komar
	const NavPolyId* GetPointerToId() const { return &m_id; }

	NavManagerId	GetNavManagerId() const
	{
		NavManagerId navId = m_hNavMesh.GetManagerId();
		navId.m_iPoly = m_id;
		return navId;
	}

	const NavPolyId*	GetAdjacencyArray() const { return m_adjId; }
	const float*		GetVertexArray() const { return (float*)m_vertsLs; }
	U32F				GetAdjacentId(U32F i) const { NAV_ASSERT(i < kMaxVertexCount); return m_adjId[i]; }
	bool				IsAdjacentPoly(U32F polyId) const;
	bool				IsAdjacentPoly(const NavPoly* other) const;
	U32F				GetLinkId() const { return m_linkId; }
	U32F				GetPathNodeId() const { return m_pathNodeId; }
	U32F				GetVertexCount() const { return m_vertCount; }

	const Point GetVertex(U32F i) const { return GetVertexLs(i); }
	const Point GetNextVertex(U32F i) const { return GetVertexLs((i + 1) & 3); }

	float   		GetPathCostMultiplier() const { return m_pathCostMultiplier; }
	NavMeshHandle	GetNavMeshHandle() const { return m_hNavMesh; }
	Point			GetCentroid() const;
	Point			GetBBoxMinLs() const;
	Point			GetBBoxMaxLs() const;
	Point			GetBBoxMinPs() const;
	Point			GetBBoxMaxPs() const;
	Point			GetBBoxMinWs() const;
	Point			GetBBoxMaxWs() const;
	float			ComputeSignedAreaXz() const;

	ActionPack*		GetRegisteredActionPackList() const { return m_pRegistrationList; }
	void			RegisterActionPack(ActionPack* pActionPack);
	bool			UnregisterActionPack(ActionPack* pActionPack);
	void			RelocateActionPack(ActionPack* pActionPack, ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound);
	void			ValidateRegisteredActionPacks() const;

	const NavMesh*	GetNavMesh() const { return GetNavMeshHandle().ToNavMesh(); }

	const NavPoly*	GetLinkedPoly(const NavMesh** ppDestMeshOut = nullptr) const;
	NavPolyId		GetLinkedPolyId() const;
	bool			PolyContainsPointLsNoEpsilon(Point_arg ptLs) const;
	bool			PolyContainsPointLs(Point_arg ptLs, float epsilon = NDI_FLT_EPSILON) const;
	float			SignedDistPointPolyXzSqr(Point_arg ptLs) const;
	float			SignedDistPointPolyXz(Point_arg ptLs) const;
	const Scalar	FindNearestPointXzLs(Point* pOutLs, Point_arg inPtLs) const;
	const Scalar	FindNearestPointLs(Point* pOutLs, Point_arg inPtLs) const;
	const Scalar	FindNearestPointPs(Point* pOutPs, Point_arg inPtPs) const;

	// NB: Does *not* give you a point to be contained by the poly, just one projected onto its extrapolated surface!
	void			ProjectPointOntoSurfaceLs(Point* pOutLs, Vector* pNormalLs, Point_arg inPtLs) const;
	void			ProjectPointOntoSurfacePs(Point* pOutPs, Vector* pNormalPs, Point_arg inPtPs) const;

	// static debug drawing
	void			DebugDrawEdges(const Color& colInterior,
								   const Color& colBorder,
								   float yOffset,
								   float lineWidth = 3.0f,
								   DebugPrimTime tt = kPrimDuration1FrameAuto) const;

	void			DebugDraw(const Color& col,
							  const Color& colBlocked = kColorWhiteTrans,
							  float yOffset = 0.0f,
							  DebugPrimTime tt = kPrimDuration1FrameAuto) const;

	void			DebugDrawGaps(const Color& col, DebugPrimTime tt = kPrimDuration1FrameAuto) const;

	// dynamic debug drawing (requries nav mesh mgr read lock)
	void			DebugDrawExEdges(const Color& col,
									 const Color& colBoundary,
									 const Color& colBlocked,
									 float yOffset,
									 float lineWidth = 3.0f,
									 DebugPrimTime tt = kPrimDuration1FrameAuto,
									 const NavBlockerBits* pObeyedBlockers = nullptr) const;

	void			DebugDrawEx(const Color& col,
								const Color& colBlocked = kColorRedTrans,
								DebugPrimTime tt = kPrimDuration1FrameAuto,
								const NavBlockerBits* pObeyedBlockers = nullptr) const;

	void			DebugDrawExGaps(const Color& colObeyed,
									const Color& colIgnored,
									float yOffset,
									DebugPrimTime tt = kPrimDuration1FrameAuto,
									const NavBlockerBits* pObeyedBlockers = nullptr) const;

	bool			IsValid() const		{ return !m_flags.m_error; }
	bool			IsLink() const		{ return m_flags.m_link; }
	bool			IsPreLink() const	{ return m_flags.m_preLink; }
	bool			IsStealth() const	{ return m_flags.m_stealth; }
	const Flags&	GetFlags() const	{ return m_flags; }

	bool IsBlocked(const Nav::StaticBlockageMask blockageType = Nav::kStaticBlockageMaskAll) const
	{
		return (m_blockageMask & blockageType) != 0;
	}

	DC::StealthVegetationHeight GetStealthVegetationHeight() const;

	void			SetBlockageMask(const Nav::StaticBlockageMask blockageMask);
	Nav::StaticBlockageMask GetBlockageMask() const { return m_blockageMask; }
	void			SetStealth(bool f) { m_flags.m_stealth = f; }
	void			SetAdjacentId(U32F i, U32F adjId) { m_adjId[i] = adjId; }
	void			SetValid(bool v) { m_flags.m_error = !v; }

	bool			HasExGaps() const { return m_flags.m_hasExGaps; }
	void			SetHasExGaps(bool hasExGaps) { m_flags.m_hasExGaps = hasExGaps; }

	NavPolyEx*	GetNavPolyExList() const { return m_pExPolyList; }
	void AddNavPolyExToList(NavPolyEx* pNewPolyEx) const;
	void DetachExPolys() const { m_pExPolyList = nullptr; }

	const NavPolyDistEntry*	GetPolyDistList() const { return m_pPolyDistList; }

	const NavPolyEx* FindContainingPolyExLs(Point_arg pos) const;
	const NavPolyEx* FindContainingPolyExPs(Point_arg pos) const;

	const NavMeshGapRef* GetGapList() const { return m_pGapList; }

	bool HasLegalShape() const;

	// convenient access to the EntityDB, if it exists
	template<typename T>
	const T GetTagData(StringId64 recordName, const T& def) const
	{
		const EntityDB* pEntityDB = GetEntityDB();

		if (!pEntityDB)
			return def;

		const EntityDB::Record* pRecord = pEntityDB->GetRecord(recordName);
		return pRecord ? pRecord->GetData<T>(def) : def;
	}

	const EntityDB* GetEntityDB() const;
	void DebugOverrideEntityDB(const EntityDB* pDebugEntityDB);

	friend class NavMesh;
	friend class NavPathNodeMgr;
	friend class NavMeshBuilder;
	friend void NavMeshDebugDraw(const NavMesh* pNavMesh, const NavMeshDrawFilter& filter);

private:
	struct Vertex
	{
		float m_xyzData[3];
	};

	const Point GetVertexLs(U32F i) const
	{
		// slow
		// manually construct a Point with the 3 values x, y, z of the vertex data, which will actually create
		// a Point with v128 {x, y, z, z}:
		// return Point(m_vertsLs[i].m_xyzData[0], m_vertsLs[i].m_xyzData[1], m_vertsLs[i].m_xyzData[2]);


		// much faster: interpret the vertex as a Point even though it's only 3 floats long
		// don't worry, this is safe:
		// - the contents of W in a Point are undefined, so it doesn't matter what we put there
		// - m_vertsLs is in the middle of the structure, with more stuff after it, so it's safe to overread by 4 bytes
		//
		// memcpy is preferred over the simpler:
		//
		//     return *(Point*)(m_vertsLs + i);
		//
		// as it avoids undefined behavior (the aliasing) but produces same codegen.
		//
		// also: we still use a Point constructor rather than directly memcpy'ing into a Point object
		// in order to retain the optional SMATH quadword validation
		//
		// the performance of companion analysis is absolutely dependent on the efficiency of this, in fact
		// it is furthermore dependent on indirect addressing of the vertex DIRECTLY into an add operation:
		//
		//     const Point a = b + pPoly->GetVertex(i);
		//     ->
		//     vaddps xmm0,xmm1,xmmword ptr [rbx+rax*4+34h]
		//
		// so please do not modify (or check w/ Kareem)!
		//

		v128 vec;
		memcpy(&vec, m_vertsLs + i, sizeof(v128));
		return Point(vec);
	}

	void SetVertexLs(U32F i, Point_arg ptLs)
	{
		m_vertsLs[i].m_xyzData[0] = ptLs.X();
		m_vertsLs[i].m_xyzData[1] = ptLs.Y();
		m_vertsLs[i].m_xyzData[2] = ptLs.Z();
	}

	// offset 0
	NavPolyId	m_id;								// 2 bytes
#ifdef HEADLESS_BUILD
	// super hack, deprecate the smaller size path node ids and steal pad space later in the object.

	U16			m_oldPathNodeId;					// 2 bytes
#else
	U16			m_pathNodeId;						// 2 bytes
#endif

	U16			m_linkId;							// 2 bytes

	U8			m_vertCount;						// 1 byte
	Flags		m_flags;							// 1 byte

	// offset 8
	ActionPack*				m_pRegistrationList;	// 8 bytes : linked list of action packs registered to this poly
	const NavMeshGapRef*	m_pGapList;				// 8 bytes
	const NavPolyDistEntry*	m_pPolyDistList;		// 8 bytes

	// offset 32
	NavPolyId		m_adjId[kMaxVertexCount];		// 2*4=8 bytes;
	NavMeshHandle	m_hNavMesh;						// 8 bytes

	// offset 48
	float		m_pathCostMultiplier;				// 4 bytes
	Vertex		m_vertsLs[kMaxVertexCount];			// 48 bytes (12*4)

	// offset 100
	U16	m_iEntityDB;								// 2 bytes

#ifdef HEADLESS_BUILD
	// hack steal padding for the larger path node and link id so we don't have to reformat the nav poly in tools and rebuild all levels
	U8			m_midPad[6];

	U32			m_pathNodeId;						// 4 bytes
#else
	U8 m_midPad[112 - 102];
#endif

	// offset 112
	mutable NavPolyEx* m_pExPolyList;				// 8 bytes

	// offset 120
	Nav::StaticBlockageMask m_blockageMask;			// 2 bytes modified at runtime

	U8			m_pad[128 - 122];

	// total size: 128 bytes
};

/// --------------------------------------------------------------------------------------------------------------- ///
inline F32 NavPolyDistEntry::GetDist() const
{
	return (float(m_distU16) * NavPoly::kMaxPolyTableDist) / 65535.0f;
}
