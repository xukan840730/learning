/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "corelib/math/aabb.h"
#include "corelib/math/locator.h"
#include "corelib/math/obb.h"

#include "ndlib/io/pak-structs.h"
#include "ndlib/util/bit-array-grid-hash-def.h"

#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-blocker-defines.h"
#include "gamelib/gameplay/nav/nav-mesh-gap.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-poly.h"
#include "gamelib/level/entitydb.h"

//#if NDI_COMP_CLANG
//#pragma clang diagnostic push
//#pragma clang diagnostic error "-Wpadded"			// Disallow the compiler to pad data structures
//#endif

//////////////////////////////////////////////////////////////////////////
///
///  NOTE: the data structures defined here are written out by tools,
///        so any changes to the data formats need to be reflected in the
///        tools code as well.  The NavMesh versionId field should be
///        incremented if any incompatible changes are made.  Also the
///        game level version should be bumped as well.
///
//////////////////////////////////////////////////////////////////////////

/// --------------------------------------------------------------------------------------------------------------- ///
class RigidBody;
class BitArrayGridHash;
class Level;
class NavPolyEx;
struct AiExposureData;

CONST_EXPR F32 kHeightMapGridSpacing = 0.5f;
CONST_EXPR F32 kInvHeightMapGridSpacing = 1.0f / kHeightMapGridSpacing;
CONST_EXPR F32 kHeightMapHeightSpacing = 0.078125f;
CONST_EXPR F32 kInvHeightMapHeightSpacing = 1.0f / kHeightMapHeightSpacing;

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavPolyDistEntryPair
{
	U16 m_iPolyA;
	U16 m_iPolyB;
	F32 m_dist;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavMeshBlocker
{
	StringId64	m_blockerId;
	U64			m_numVerts;
	Point		m_aVertsLs[];
};

/// --------------------------------------------------------------------------------------------------------------- ///
// May want to store a table for each link containing the distance from each polygon to the link.
// This could be used to help path find between nav-meshes.
class NavMeshLink
{
public:
	friend class NavMesh;
	friend class NavMeshBuilder;

	StringId64		GetLinkId() const				{ return m_linkId; }
	NavMeshHandle	GetDestNavMesh() const			{ return m_hDestMesh; }
	U32F			GetSrcLinkPolyId() const		{ return m_srcLinkPolyId; }
	U32F			GetSrcPreLinkPolyId() const		{ return m_srcPreLinkPolyId; }
	U32F			GetDestLinkPolyId() const		{ return m_destLinkPolyId; }
	U32F			GetDestPreLinkPolyId() const	{ return m_destPreLinkPolyId; }

	// I need to load the poly ID and 2 bytes past it directly into an XMM as fast as possible.
	// Can't do that without the pointer.
	// - komar
	const NavPolyId* GetPointerToSrcLinkPolyId() const { return &m_srcLinkPolyId; }

private:
	StringId64 m_linkId;		// 8 bytes : tool, unique link-id for matching links between meshes
	StringId64 m_destMeshId;	// 8 bytes : tool, unique mesh-id for looking up the dest mesh
	NavMeshHandle m_hDestMesh;	// 8 bytes : nullptr at load time, after login, points to the dest mesh (if it is loaded)

	// Poly linkage
	// Note the src link poly overlaps the dest switch poly and the src switch poly overlaps the dest link poly
	NavPolyId m_srcLinkPolyId;		// 2 bytes : index of poly that triggers the switch to the other nav-mesh
	NavPolyId m_srcPreLinkPolyId;	// 2 bytes : index of poly adjacent to link poly in the source mesh
	NavPolyId m_destLinkPolyId;		// 2 bytes : 0xff at load time, after login the id of the link poly in the dest mesh (if loaded)
	NavPolyId m_destPreLinkPolyId;	// 2 bytes : 0xff at load time, after login the id of the poly adjacent to link poly in the dest mesh (if loaded)

	// total size : 32 bytes
};

/// --------------------------------------------------------------------------------------------------------------- ///
class NavMeshHeightMap
{
public:
	Point				m_bboxMinLs;
	U32					m_sizeX;
	U32					m_sizeZ;
	U32					m_sizeY;
	U32					m_tiledSize;
	U32					m_bitmapPitch;
	U32					m_pad;
	U64*				m_onNavMesh;
	U64*				m_isStealth;
	U8*					m_tiledData;
	U8					m_data[0]; //actual size is m_sizeX * m_sizeZ

	Aabb GetAabbLs() const
	{
		return Aabb(m_bboxMinLs,
					m_bboxMinLs
						+ Vector(m_sizeX * kHeightMapGridSpacing,
								 m_sizeY * kHeightMapHeightSpacing,
								 m_sizeZ * kHeightMapGridSpacing));
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
//
// NavMesh
//
// 3 coordinate systems are used:
//
//   - world space
//   - parent space
//   - navmesh local space
//
// world space is the shared global space (coordinates may get somewhat large)
// parent space may be the same thing as world space, or it may be a space that moves and rotates with a platform (most inputs and outputs are assumed to be in parent space)
// navmesh local space is the space that nav polys are defined in, navmesh local space is translated (but not rotated) from parent space
//
/// --------------------------------------------------------------------------------------------------------------- ///
class NavMesh : public ResItem
{
public:
	static CONST_EXPR U32 kVersionId = 17;
	static CONST_EXPR U32 kSignature = 0x4e61764d; // hex for "NavM"
	static CONST_EXPR size_t kNumGameDataBytes = 64; // must be >= sizeof(NavMeshGameData)

	enum class NpcStature : U8
	{
		kProne,
		kCrouch,
		kStand,

		kCount
	};

	static const char* NpcStatureToString(const NpcStature npcStature)
	{
		static CONST_EXPR const char* kNpcStatureStrings[] =
		{
			"kProne",
			"kCrouch",
			"kStand"
		};
		STATIC_ASSERT((int)NpcStature::kCount == ARRAY_COUNT(kNpcStatureStrings));
		NAV_ASSERT((int)npcStature >= 0);
		NAV_ASSERT((int)npcStature < (int)NpcStature::kCount);
		return kNpcStatureStrings[(int)npcStature];
	}

	struct NavMeshGameData
	{
		mutable NdAtomic64*	m_pOccupancyCount;

		U32 m_maxOccupancyCount; // max number of Npcs that should be using this at a time
		float m_navMeshAreaXz;
		NpcStature m_npcStature;

		union Flags
		{
			struct
			{
				bool m_registered			: 1;
				bool m_attached				: 1;
				bool m_noPosts				: 1;
				bool m_pathNodesRegistered	: 1;
				bool m_hasErrorPolys		: 1;

				bool m_navMeshForcesSwim	: 1;
				bool m_navMeshForcesDive	: 1;
				bool m_navMeshForcesWalk	: 1;
				bool m_navMeshForcesCrouch	: 1;
				bool m_navMeshForcesProne	: 1;

				U8 m_pad : 6;
			};
			U16 m_bits;
		};
		Flags m_flags;
		STATIC_ASSERT(sizeof(m_flags) == 2);
	};
	STATIC_ASSERT(kNumGameDataBytes >= sizeof(NavMeshGameData));

	enum BoundaryFlags
	{
		kBoundaryNone		 = 0,
		kBoundaryTypeStatic	 = 1U << 0,
		kBoundaryTypeDynamic = 1U << 1,
		kBoundaryTypeStealth = 1U << 2,
		kBoundaryTypeAll	 = kBoundaryTypeStatic | kBoundaryTypeDynamic | kBoundaryTypeStealth,
		kBoundaryInside		 = 1U << 3,
	};

	struct BaseProbeParams
	{
		void DebugPrint(MsgOutput chan) const;

		const NavPoly*		m_pStartPoly = nullptr;
		const NavPolyEx*	m_pStartPolyEx = nullptr;

		NavBlockerBits		m_obeyedBlockers = NavBlockerBits(false);

		Segment				m_capsuleSegment = Segment(kZero, kZero); // Turns from circle to capsule based.

		bool						m_dynamicProbe = false;
		bool						m_obeyStealthBoundary = false;
		bool						m_crossLinks = true;
		bool						m_debugDraw = false;
		Nav::StaticBlockageMask		m_obeyedStaticBlockers = Nav::kStaticBlockageMaskAll;
		NpcStature					m_minNpcStature = NpcStature::kProne;
	};

	struct FindPointParams : public BaseProbeParams
	{
		enum StealthOption
		{
			kAll,
			kStealthOnly,
			kNonStealthOnly
		};

		void DebugPrint(MsgOutput chan) const;

		// inputs
		Point			m_point = kOrigin;
		float			m_searchRadius = 0.0f;
		float			m_depenRadius = 0.0f;
		StealthOption	m_stealthOption = kAll;

		// outputs
		const NavPoly*		m_pPoly = nullptr;
		const NavPolyEx*	m_pPolyEx = nullptr;
		float				m_dist = 0.0f;
		Point				m_nearestPoint = kOrigin;
	};

	struct ProbeParams : public BaseProbeParams
	{
		void DebugPrint(MsgOutput chan) const;

		// inputs
		Point				m_start = kOrigin;
		Vector				m_move = kZero;
		float				m_probeRadius = 0.0f;
		float				m_polyContainsPointTolerance = NDI_FLT_EPSILON;

		// outputs
		const NavPoly*		m_pHitPoly = nullptr;
		const NavPolyEx*	m_pHitPolyEx = nullptr;
		const NavMesh*		m_pReachedMesh = nullptr;
		const NavPoly*		m_pReachedPoly = nullptr;
		const NavPolyEx*	m_pReachedPolyEx = nullptr;
		bool				m_hitEdge = false;
		BoundaryFlags		m_hitBoundaryFlags = kBoundaryNone;
		Point				m_endPoint = kOrigin;
		Vector				m_edgeNormal = kZero;
		Point				m_hitVert[2] = { kOrigin, kOrigin };
		Point				m_impactPoint = kOrigin;
		I32					m_hitEdgeIndex = -1;
	};

	struct ClearanceParams : public BaseProbeParams
	{
		void DebugPrint(MsgOutput chan) const;

		// inputs
		Point m_point  = kOrigin;
		float m_radius = 0.0f;

		// outputs
		bool m_hitEdge		= false;
		Point m_impactPoint = kOrigin;
	};

	void Login(Level* pLevel);
	void Register();
	void Unregister();
	void Logout(Level* pLevel);

	void UnregisterAllActionPacks();

	const char*			GetName() const					{ return m_strName; }
	StringId64			GetNameId() const				{ return m_nameId; }
	StringId64			GetBindSpawnerNameId() const	{ return m_bindSpawnerNameId; }
	StringId64			GetLevelId() const				{ return m_levelId; }
	U32F				GetPolyCount() const			{ return m_polyCount; }
	U32F				GetLinkCount() const			{ return m_linkCount; }
	const NavPoly*		UnsafeGetPolyFast(U32 i) const	{ return m_pPolyArray + i; }
	NavPoly&			GetPoly(U32F i) const			{ NAV_ASSERT(i < m_polyCount); return m_pPolyArray[i]; }
	const NavMeshLink&	GetLink(U32F i) const			{ NAV_ASSERT(i < m_linkCount); return m_linkArray[i]; }

	const BitArrayGridHash* GetPolyGridHash() const { return PunPtr<const BitArrayGridHash*>(&m_polyHash); }

	const Point GetBoundingBoxMinLs() const { return Point(kOrigin) - m_vecRadius; }
	const Point GetBoundingBoxMaxLs() const { return Point(kOrigin) + m_vecRadius; }
	const Obb GetObbPs() const { return Obb(GetOriginPs(), GetBoundingBoxMinLs(), GetBoundingBoxMaxLs()); }
	const Obb GetObbWs() const { return Obb(GetOriginWs(), GetBoundingBoxMinLs(), GetBoundingBoxMaxLs()); }
	const Aabb GetAabbLs() const { return Aabb(GetBoundingBoxMinLs(), GetBoundingBoxMaxLs()); }
	const Aabb GetAabbPs() const { return GetObbPs().GetEnclosingAabb(); }
	const Aabb GetAabbWs() const { return GetObbWs().GetEnclosingAabb(); }

	const Locator& GetOriginPs() const { return m_pBoundFrame->GetLocatorPs(); }
	const Locator GetOriginWs() const { return m_pBoundFrame->GetLocatorWs(); }

	const Vector GetHalfExtentsPs() const { return LocalToParent(m_vecRadius); }
	const Vector GetHalfExtentsWs() const { return LocalToWorld(m_vecRadius); }

	U32F GetMaxOccupancyCount() const	{ return m_gameData.m_maxOccupancyCount; }
	U32F GetOccupancyCount() const		{ return m_gameData.m_pOccupancyCount ? m_gameData.m_pOccupancyCount->Get() : 0; }
	bool IncOccupancyCount() const;
	bool DecOccupancyCount() const;
	bool AtMaxOccupancyCount() const;
	float GetSurfaceAreaXz() const { return m_gameData.m_navMeshAreaXz; }

	const Point ParentToLocal(Point_arg p) const { return GetOriginPs().UntransformPoint(p); }
	const Point LocalToParent(Point_arg p) const { return GetOriginPs().TransformPoint(p); }
	const Vector ParentToLocal(Vector_arg v) const { return GetOriginPs().UntransformVector(v); }
	const Vector LocalToParent(Vector_arg v) const { return GetOriginPs().TransformVector(v); }
	const Locator ParentToLocal(const Locator& l) const { return GetOriginPs().UntransformLocator(l); }
	const Locator LocalToParent(const Locator& l) const { return GetOriginPs().TransformLocator(l); }

	const Point WorldToParent(Point_arg p) const { return GetParentSpace().UntransformPoint(p); }
	const Point ParentToWorld(Point_arg p) const { return GetParentSpace().TransformPoint(p); }
	const Vector WorldToParent(Vector_arg v) const { return GetParentSpace().UntransformVector(v); }
	const Vector ParentToWorld(Vector_arg v) const { return GetParentSpace().TransformVector(v); }
	const Locator WorldToParent(const Locator& l) const { return GetParentSpace().UntransformLocator(l); }
	const Locator ParentToWorld(const Locator& l) const { return GetParentSpace().TransformLocator(l); }

	const Point WorldToLocal(Point_arg p) const { return GetOriginWs().UntransformPoint(p); }
	const Point LocalToWorld(Point_arg p) const { return GetOriginWs().TransformPoint(p); }
	const Vector WorldToLocal(Vector_arg v) const { return GetOriginWs().UntransformVector(v); }
	const Vector LocalToWorld(Vector_arg v) const { return GetOriginWs().TransformVector(v); }
	const Locator WorldToLocal(const Locator& l) const { return GetOriginWs().UntransformLocator(l); }
	const Locator LocalToWorld(const Locator& l) const { return GetOriginWs().TransformLocator(l); }

	const Locator& GetParentSpace() const { return m_pBoundFrame->GetParentSpace(); }

	bool PointInBoundingBoxWs(Point_arg posWs, Scalar_arg radius) const;
	void ConfigureParentSpace(const Locator& parentAlignWs, const RigidBody* pBindTarget);

	bool IsVersionCorrect() const { return m_versionId == kVersionId; }
	bool IsValid() const;
	bool IsLoggedIn() const { return m_managerId.IsValid(); }
	bool IsRegistered() const { return m_gameData.m_flags.m_registered; }
	bool IsAttached() const { return m_gameData.m_flags.m_attached; }
	bool IsNoPosts() const { return m_gameData.m_flags.m_noPosts; }
	void SetAttached(bool f) { m_gameData.m_flags.m_attached = f; }
	bool ArePathNodesRegistered() const { return m_gameData.m_flags.m_pathNodesRegistered; }
	bool HasErrorPolys() const { return m_gameData.m_flags.m_hasErrorPolys; }

	bool NavMeshForcesSwim() const;
	bool NavMeshForcesDive() const { return m_gameData.m_flags.m_navMeshForcesDive; }
	bool NavMeshForcesWalk() const { return m_gameData.m_flags.m_navMeshForcesWalk; }
	bool NavMeshForcesCrouch() const { return m_gameData.m_flags.m_navMeshForcesCrouch; }
	bool NavMeshForcesProne() const { return m_gameData.m_flags.m_navMeshForcesProne; }

	NpcStature GetNpcStature() const { return m_gameData.m_npcStature; }

	bool IsKnownStaticBlocker(StringId64 blockerId) const;
	U32F GetNumKnownStaticBlockers() const;
	StringId64 GetStaticNavBlockerId(U32F i) const
	{
		return m_pKnownStaticBlockers ? m_pKnownStaticBlockers[i] : INVALID_STRING_ID_64;
	}

	const NavMeshHeightMap* GetHeightMap() const { return m_pHeightMap; }

	const NavManagerId& GetManagerId() const { return m_managerId; }

	StringId64 GetCollisionLevelId() const;

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

	const EntityDB* GetEntityDB() const { return GetEntityDBByIndex(0); }
	void DebugOverrideEntityDB(const EntityDB* pDebugEntityDB);

	static DC::StealthVegetationHeight GetStealthVegetationHeight(const EntityDB* pEntityDB);

	U32F GetNumGaps() const { return m_numGaps; }
	const NavMeshGap& GetGap(U32F index) const
	{
		NAV_ASSERT(m_pGapArray);
		NAV_ASSERT(index < m_numGaps);
		return m_pGapArray[index];
	}

	//////////////////////////////////////////////////////////////////////////
	// probe & utility queries

	enum class ProbeResult
	{
		kReachedGoal,
		kHitEdge,
		kErrorStartedOffMesh,
		kErrorStartNotPassable
	};

	U32F GetPolyCircleIntersectionsLs(const NavPoly** ppPolys,
									  U32F maxPolyCount,
									  Point_arg ptLs,
									  Scalar_arg radius,
									  Point* const pPoints = nullptr) const;

	U32F GetPolyBoxIntersectionsLs(const NavPoly** ppPolys, U32F maxPolyCount, Aabb_arg bboxLs) const;
	U32F GetPolyBoxIntersectionsPs(const NavPoly** ppPolys, U32F maxPolyCount, Aabb_arg bboxPs) const;

	const NavPoly* FindContainingPolyLs(Point_arg ptLs,
										float yThreshold = 2.0f,
										Nav::StaticBlockageMask obeyedStaticBlockers = Nav::kStaticBlockageMaskAll) const;

	const NavPoly* FindContainingPolyPs(Point_arg ptPs,
										float yThreshold = 2.0f,
										Nav::StaticBlockageMask obeyedStaticBlockers = Nav::kStaticBlockageMaskAll) const;

	ProbeResult ProbeLs(ProbeParams* pParams) const;
	ProbeResult ProbePs(ProbeParams* pParams) const;

	// search for nearest point in reachable space
	void FindNearestPointLs(FindPointParams* pParams) const;
	void FindNearestPointPs(FindPointParams* pParams) const;

	ProbeResult CheckClearanceLs(ClearanceParams* pParams) const;
	ProbeResult CheckClearancePs(ClearanceParams* pParams) const;

	static BoundaryFlags IsBlockingEdge(const NavMesh* pMesh,
										const NavPoly* pPoly,
										const NavPolyEx* pPolyEx,
										U32F iEdge,
										const BaseProbeParams& params,
										const NavMesh** ppMeshOut = nullptr,
										const NavPoly** ppPolyOut = nullptr,
										const NavPolyEx** ppPolyExOut = nullptr);

	typedef void (*FnVisitNavPoly)(const NavPoly* pPoly, const NavPolyEx* pPolyEx, uintptr_t userData);

	void WalkPolysInLineLs(ProbeParams& params, FnVisitNavPoly visitFunc, uintptr_t userData) const;
	void WalkPolysInLinePs(ProbeParams& params, FnVisitNavPoly visitFunc, uintptr_t userData) const;

	//
	//////////////////////////////////////////////////////////////////////////

	friend class NavMeshMgr;
	friend class NavMeshHandle;
	friend class NavPolyHandle;
	friend class NavControl;
	friend class NavMeshBuilder;

private:
	bool ConnectLink(U32F iLink, NavMesh* pDestMesh);
	void DetachLink(U32F iLink);

	void PopulateNavMeshForceFlags();

	NavMesh::ProbeResult IntersectCapsuleLs(ProbeParams* pParams,
											const NavPoly* pContainingPoly,
											const NavPolyEx* pContainingPolyEx) const;

	const EntityDB* GetEntityDBByIndex(U32F iDb) const;

	void DebugCaptureProbeFailure(const ProbeParams& paramsLs, bool force = false) const;

	// qw1 (try and maintain quadword boundaries here for smaths)
	U32	m_signature;	// 4 bytes : should be kSignature or something is wrong
	U32	m_versionId;	// 4 bytes : data format version id
	U32	m_polyCount;	// 4 bytes
	U32	m_linkCount;	// 4 bytes

	// qw2
	StringId64	m_nameId;					// 8 bytes : nav-mesh id (globally unique)
	StringId64	m_levelId;					// 8 bytes

	// qw3
	StringId64		m_bindSpawnerNameId;	// 8 bytes : name-id of entity-spawner nav mesh should be bound to (must be in same level)
	NavManagerId	m_managerId;			// 8 bytes : id assigned to this mesh by NavMeshMgr

	// qw4
	const char*	m_strName;		// 8 bytes : a rose by any other
	NavPoly*	m_pPolyArray;	// 8 bytes

	// qw5,6
	Vector	m_vecRadius;				// 16 bytes
	Vector	m_originOffsetFromObject;	// 16 bytes : tools set this vector to help correctly position the nav mesh origin relative to a moving object's align

	// qw7-11
	BitArrayGridHashDef m_polyHash;				// 64 bytes

	// qw12
	NavMeshHeightMap*	m_pHeightMap;			// 8 bytes
	const EntityDB**	m_pEntityDBTable;		// 8 bytes

	// qw13
	const NavMeshGap*	m_pGapArray;			// 8 bytes
	const StringId64*	m_pKnownStaticBlockers;	// 8 bytes

	// qw14
	BoundFrame*			m_pBoundFrame;			// 8 bytes
	U32					m_numGaps;				// 4 bytes
	U32					m_numEntityDBs;			// 4 bytes

	// qw14-18
	union
	{
		U8 m_gameDataBytes[kNumGameDataBytes];
		NavMeshGameData m_gameData;
	};

public:
	AiExposureData*			m_exposure;
private:

	NavMeshLink				m_linkArray[0];

	friend class NavPoly;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavMeshProbeDebug
{
	const NavMesh* m_pNavMesh = nullptr;
	NavMesh::ProbeParams m_paramsLs;
	bool m_valid = false;
};

extern NavMeshProbeDebug g_navMeshProbeDebug;

/// --------------------------------------------------------------------------------------------------------------- ///
struct NavMeshDepenDebug
{
	const NavMesh* m_pNavMesh = nullptr;
	NavMesh::FindPointParams m_fpParamsPs;
	NavMesh::ClearanceParams m_cParamsPs;
	bool m_valid = false;
};
extern NavMeshDepenDebug g_navMeshDepenDebug;

bool MailNavMeshReportTo(const char* toAddr, const char* subject = nullptr);

//#if NDI_COMP_CLANG
//#pragma clang diagnostic pop
//#endif
