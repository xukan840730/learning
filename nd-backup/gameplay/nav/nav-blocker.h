/*
 * Copyright (c) 2007 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "gamelib/gameplay/nav/ap-handle-table.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class Locator;
class NavMesh;
class NavPoly;
class ProcessSpawnInfo;
struct NavMeshBlocker;

/// --------------------------------------------------------------------------------------------------------------- ///
class DynamicNavBlocker
{
public:
	friend class NavBlockerMgr;

	void Init(Process* pOwner, const NavPoly* pPoly, const char* sourceFile, U32F sourceLine, const char* sourceFunc);

	const NavPolyHandle& GetNavPolyHandle() const { return m_hPoly; }
	const NavPoly* GetNavPoly() const { return m_hPoly.ToNavPoly(); }
	const NavMesh* GetNavMesh() const { return m_hPoly.ToNavMesh(); }
	void SetNavPoly(const NavPoly* pPoly) { m_hPoly = pPoly; }

	Point GetPosPs() const { return m_posPs; }
	void SetPosPs(Point_arg posPs) { m_posPs = posPs; }

	U32F GetFactionId() const             { return m_faction; }
	void SetFactionId(U32F faction)
	{
		if (faction == (U32F)~0)
			faction = 0;
		m_faction = faction;
	}

	ProcessHandle GetOwner() const		{ return m_hOwner; }
	void SetOwner(ProcessHandle hOwner)	{ m_hOwner = hOwner; }

	U32F GetIgnoreProcessId() const		{ return m_ignoreProcessId; }
	void SetIgnoreProcessId(U32F id)	{ m_ignoreProcessId = id; }

	bool ShouldAffectEnemies() const { return m_shouldAffectEnemies; }
	bool ShouldAffectNonEnemies() const { return m_shouldAffectNonEnemies; }
	void SetShouldAffectEnemies(bool f) { m_shouldAffectEnemies = f; }
	void SetShouldAffectNonEnemies(bool f) { m_shouldAffectNonEnemies = f; }

	StringId64 GetBlockProcessType() const { return m_blockProcessType; }
	void SetBlockProcessType(StringId64 applyTo) {m_blockProcessType = applyTo; }

	// quad stuff
	void SetQuadFromRadius(float radius);
	void SetQuad(const Point* vertexList);
	const Point GetQuadVertex(U32F iV) const { return Point(m_vertsXzLs[iV].x, 0, m_vertsXzLs[iV].y); }
	const float GetQuadArea() const
	{
		const Vector v0 = Vector(m_vertsXzLs[1].x - m_vertsXzLs[0].x,
								 0.0f,
								 m_vertsXzLs[1].y - m_vertsXzLs[0].y);

		const Vector v1 = Vector(m_vertsXzLs[2].x - m_vertsXzLs[0].x,
								 0.0f,
								 m_vertsXzLs[2].y - m_vertsXzLs[0].y);

		return Length(Cross(v0, v1));
	}

	float GetBoundingRadius() const;
	bool ContainsPointPs(Point_arg posPs) const;

	void EnterNewParentSpace(const Transform& matOldToNew, const Locator& oldParentSpace, const Locator& newParentSpace);

	void DebugDraw() const;
	void DebugDrawShape(Color color, DebugPrimTime tt = kPrimDuration1FrameAuto) const;

private:
	ProcessHandle	m_hOwner;
	NavPolyHandle	m_hPoly;
	Point			m_posPs;
	Vec2			m_vertsXzLs[4];	// quad verts are relative to object's align
	U32				m_ignoreProcessId;
	U8				m_faction;
	StringId64		m_blockProcessType;

	union
	{
		struct
		{
			bool m_shouldAffectEnemies : 1;
			bool m_shouldAffectNonEnemies : 1;
		};
		U8 m_flags;
	};

	const char* m_sourceFile;
	U32			m_sourceLine;
	const char* m_sourceFunc;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class StaticNavBlocker
{
public:
	static const size_t kMaxNavPolyCount = 128;
	static const size_t kMaxNavMeshCount = 8;

	void Init(Process* pOwner,
			  const BoundFrame& loc,
			  StringId64 bindSpawnerId,
			  StringId64 dataTagId,
			  const ProcessSpawnInfo& spawnInfo,
			  const char* sourceFile,
			  U32F sourceLine,
			  const char* sourceFunc);

	MutableProcessHandle GetOwner() const { return m_hOwner; }

	Point GetCenterWs() const;

	void DebugDraw() const;

	void RequestEnabled(bool enabled);
	bool IsEnabledRequested() const { return m_enabledRequested; }
	bool IsEnabledActive() const { return m_enabledActive; }

	void ApplyEnabled();
	void RefreshNearbyCovers(ApHandleTable& apNavRefreshTable) const;

	void OnNavMeshRegistered(const NavMesh* pMesh);
	void OnNavMeshUnregistered(const NavMesh* pMesh);

	void DebugCheckValidity() const;

private:
	void BuildNavPolyList(const ProcessSpawnInfo& spawnInfo);

	BoundFrame					m_boundFrame;
	MutableProcessHandle		m_hOwner;
	StringId64					m_bindSpawnerId;
	NavPolyHandle				m_hPolys[kMaxNavPolyCount];
	NavMeshHandle				m_hMeshes[kMaxNavMeshCount];
	size_t						m_numPolys;
	size_t						m_numMeshes;

	const NavMeshBlocker*		m_pBlockerData;

	bool						m_enabledRequested;
	bool						m_enabledActive;
	mutable bool				m_triangulationError;

	const char*					m_sourceFile;
	U32							m_sourceLine;
	const char*					m_sourceFunc;
	Nav::StaticBlockageMask		m_blockageMask;
};
