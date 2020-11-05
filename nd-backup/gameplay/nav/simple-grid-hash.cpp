/*
 * Copyright (c) 2010 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/gameplay/nav/simple-grid-hash.h"

#include "corelib/math/mathutils.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/system/read-write-atomic-lock.h"

#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/gameplay/nav/simple-grid-hash-mgr.h"
#include "gamelib/gameplay/nd-game-object.h"

//static
SimpleGridHashId SimpleGridHashId::kInvalid(SimpleGridHashId::kInvalidIndex);

F32 g_sgridhashCellSizeConfig = 8.0f; // meters
Scalar g_sgridhashCellSize;

// ---------------------------------------------------------------------------------------------------------------
size_t SimpleGridHash::DetermineSizeInBytes(U32 numCells)
{
	size_t bytesNeeded = 0;

	// let's use a prime hash table size to minimize collisions
	static F32 hashTableSizeFactor = 1.0f;
	const U32 exactTableSize = (U32)((F32)numCells * hashTableSizeFactor);
	const U32 tableSize = NextLargestPrime(exactTableSize);

	bytesNeeded += Table::DetermineSizeInBytes(tableSize);
	
	size_t numBlocks = ExternalBitArray::DetermineNumBlocks(numCells);
	bytesNeeded += sizeof(U64) * numBlocks;

	bytesNeeded += sizeof(Cell) * numCells;

	return bytesNeeded;
}

// ---------------------------------------------------------------------------------------------------------------
void SimpleGridHash::InitNoLock(U32 numCells)
{
	g_sgridhashCellSize = g_sgridhashCellSizeConfig;

	// NOTE: We are allocating out of the ALLOCATION_SIMPLE_NPC memory buffer here.
	Memory::Allocator* pAlloc = Memory::TopAllocator();

	// let's use a prime hash table size to minimize collisions
	static F32 hashTableSizeFactor = 1.0f;
	const U32 exactTableSize = (U32)((F32)numCells * hashTableSizeFactor);
	const U32 tableSize = NextLargestPrime(exactTableSize);

	m_table.Init(tableSize, FILE_LINE_FUNC);

	size_t numBlocks = ExternalBitArray::DetermineNumBlocks(numCells);
	m_pUsedCellsMem = NDI_NEW U64[numBlocks];
	AI_ASSERTF(m_pUsedCellsMem != nullptr, ("SimpleGridHash: Something is wrong with the memory map, or our math"));

	m_usedCells.Init(numCells, m_pUsedCellsMem);

	m_aCellPool = NDI_NEW Cell[numCells];
	AI_ASSERTF(m_aCellPool != nullptr, ("SimpleGridHash: Something is wrong with the memory map, or our math"));

	for (U32F iCell = 0; iCell < numCells; ++iCell)
	{
		m_aCellPool[iCell].m_members.ClearAllBits();
	}

	m_numCellsAllocated = numCells;

	size_t freeBytesAfter = pAlloc->GetFreeSize();
	size_t usedBytes = pAlloc->GetMemSize() - freeBytesAfter;
	MsgOut("SimpleGridHash used %.3f kB\n", (float)usedBytes / 1024.0f);

	//UnitTest(tableSize);
}

//static
U32 SimpleGridHash::HashCoordinatesNoLock(Coord_arg coord)
{
	// First we'll map our signed coordinates onto NxN, the plane of Natural numbers then we'll map NxN onto N using
	// the Cantor pairing function (https://en.wikipedia.org/wiki/Pairing_function#Cantor_pairing_function).
	//
	// NOTE: To keep this within the bounds of a U32, we need to limit our coords to a max value such that its Cantor
	//       pairing fits in 32 bits... Turns out that's 23170.
	//
	// Assume naturalX and naturalZ have their max values "m."
	//   cantor = ((m + m) * (m + m + 1) / 2) + m
	//   cantor = (2m * (2m + 1) / 2) + m
	//   cantor = (2m * 2m + 2m) / 2 + m
	//   cantor = (4m^2 + 2m) / 2 + m
	//   cantor = 2m^2 + m + m
	//   cantor = 2m^2 + 2m
	//
	// If cantor is 32-bits then the max value is 2^32.
	//   2^32 = 2m^2 + 2m
	//   2m^2 + 2m - 2^32 = 0
	//
	// "m" is Natural so we choose (and truncate) the positive root.
	//   m = 46340
	//
	// "m" is the max Natural number which represents the maximum coordinate value.
	//   46340 = coord_max + offset
	//
	// Assuming the coord space is symmetrical offset must equal coord_max.
	//   46340 = coord_max + coord_max
	//   46340 = 2 * coord_max
	//   coord_max = 46340 / 2
	//   coord_max = 23170

	static const I64 kCoordMax = 23170;
	ASSERT(Abs(coord.m_iX) <= kCoordMax);
	ASSERT(Abs(coord.m_iZ) <= kCoordMax);
	const I64 naturalX = (I64)coord.m_iX + kCoordMax;
	const I64 naturalZ = (I64)coord.m_iZ + kCoordMax;
	I64 cantor = ((naturalX + naturalZ) * (naturalX + naturalZ + 1) / 2) + naturalZ;

	return (U32)cantor;
}

SimpleGridHash::Cell* SimpleGridHash::LookupCellInternalNoLock(Coord_arg coord, const bool createIfNotFound)
{
	Cell* pCell = nullptr;
	Cell* pTailCell = nullptr;

	U32 key = HashCoordinatesNoLock(coord);
	TableIterator it = m_table.Find(key);

	if (it != m_table.End())
	{
		pCell = it->m_data;

		while (pCell && (pCell->m_coord != coord))
		{
			pTailCell = pCell;
			pCell = pCell->m_pNext;
		}

		if (pCell)
		{
			ASSERT(pCell->m_coord == coord); // has to match at this point
			return pCell;
		}
	}

	if (createIfNotFound)
	{
		pCell = AllocCellNoLock();
		AI_ASSERTF(pCell, ("SimpleGridHash: Unable to allocate cell!"));
		if (pCell)
		{
			pCell->m_coord = coord;
			pCell->m_members.ClearAllBits();
			pCell->m_pNext = nullptr;

			if (pTailCell)
			{
				ASSERT(pTailCell->m_pNext == nullptr);
				pTailCell->m_pNext = pCell;
			}
			else
			{
				m_table.Add(key, pCell);
			}
		}
	}

	return pCell;
}

SimpleGridHash::Cell* SimpleGridHash::LookupCellNoLock(Coord_arg coord) const
{
	SimpleGridHash* mutable_this = const_cast<SimpleGridHash*>(this);
	return mutable_this->LookupCellInternalNoLock(coord, false);
}

SimpleGridHash::Cell* SimpleGridHash::LookupOrCreateCellNoLock(Coord_arg coord)
{
	return LookupCellInternalNoLock(coord, true);
}

SimpleGridHash::Cell* SimpleGridHash::AllocCellNoLock()
{
	Cell* pCell = nullptr;

	U64 iCell = m_usedCells.FindFirstClearBit();
	if (iCell != ~0ULL)
	{
		m_usedCells.SetBit(iCell);
		pCell = &m_aCellPool[iCell];
	}

	return pCell;
}

void SimpleGridHash::FreeCellNoLock(Cell* pVictim)
{
	ASSERT(pVictim);
	if (pVictim)
	{
		// find this cell in the hash table

		U32 key = HashCoordinatesNoLock(pVictim->m_coord);
		TableIterator it = m_table.Find(key);
		ASSERT(it != m_table.End());

		// walk the linked list of cells to find the victim

		Cell* pCell = it->m_data;
		Cell* pPrevCell = nullptr;

		while (pCell && pCell != pVictim)
		{
			pPrevCell = pCell;
			pCell = pCell->m_pNext;
		}

		// remove the victim from the linked list

		ASSERT(pCell == pVictim);
		if (pPrevCell)
		{
			pPrevCell->m_pNext = pVictim->m_pNext;
		}
		else if (pVictim->m_pNext)
		{
			// the "previous" cell is the hash table itself -- update the head pointer
			m_table.Set(key, pVictim->m_pNext);
		}
		else
		{
			// last cell on the linked list -- remove from table entirely
			m_table.Erase(it);
		}

		pVictim->m_pNext = nullptr; // for programmer sanity

		// finally, put the cell back on the free pool

		ptrdiff_t iCell = (pVictim - m_aCellPool);
		AI_ASSERT(m_numCellsAllocated == m_usedCells.GetMaxBitCount());
		AI_ASSERT(iCell >= 0 && iCell < m_usedCells.GetMaxBitCount());

		m_usedCells.ClearBit(iCell);
	}
}

//inline Scalar SymmetricRound(Scalar_arg val)
//{
//	const Scalar f = Floor(val + SCALAR_LC(0.5f));
//	const Scalar c = Ceil(val - SCALAR_LC(0.5f));
//	const VB32 isPositive = SMATH_VEC_CMPGE(val.QuadwordValue(), SMATH_VEC_SET_ZERO());
//	const Scalar rounded(SMATH_VEC_SEL(c.QuadwordValue(), f.QuadwordValue(), isPositive));
//	return rounded;
//}

//static
SimpleGridHash::Coord SimpleGridHash::CoordFromPositionNoLockWs(Point_arg posWs)
{
	// the integer coords represent the locations of cell lower-left corners (i.e. bounding box mins)
	Coord coord;
	const Scalar invCellSize = Recip(g_sgridhashCellSize);
	coord.m_iX = (I32)(F32)Floor(posWs.X() * invCellSize);
	coord.m_iZ = (I32)(F32)Floor(posWs.Z() * invCellSize);
	return coord;
}

//static
void SimpleGridHash::GetCellBoundsNoLockWs(Coord_arg coord, Point* pBoundsMinWs, Point* pBoundsMaxWs)
{
	const Scalar minX = Scalar((F32)coord.m_iX) * g_sgridhashCellSize;
	const Scalar minZ = Scalar((F32)coord.m_iZ) * g_sgridhashCellSize;

	if (pBoundsMinWs) *pBoundsMinWs = Point(minX, SCALAR_LC(0.0f), minZ);
	if (pBoundsMaxWs) *pBoundsMaxWs = Point(minX + g_sgridhashCellSize, SCALAR_LC(0.0f), minZ + g_sgridhashCellSize);
}

//static
Scalar SimpleGridHash::GetCellSize()
{
	return g_sgridhashCellSize;
}

bool SimpleGridHash::GetDebugInfoNoLock(Coord_arg coord, U32* piCell, const MemberBits** ppMembers) const
{
	Cell* pCell = LookupCellNoLock(coord);
	if (pCell)
	{
		ptrdiff_t iCell = (pCell - m_aCellPool);
		if (piCell)
			*piCell = (U32)iCell;
		if (ppMembers)
			*ppMembers = &pCell->m_members;
		return true;
	}
	return false;
}

void SimpleGridHash::UpdateMemberPositionNoLock(SimpleGridHashId memberId, const Coord* pCoordPrev, Coord_arg coordNew)
{
	Cell* pCellPrev = pCoordPrev ? LookupCellNoLock(*pCoordPrev) : nullptr;
	Cell* pCellNew = LookupOrCreateCellNoLock(coordNew);

	if (!pCellPrev || pCellPrev != pCellNew)
	{
		if (pCellPrev)
		{
			pCellPrev->m_members.ClearBit(memberId.GetRaw());

			if (pCellPrev->m_members.AreAllBitsClear())
			{
				// this cell no longer contains any members - retire it
				FreeCellNoLock(pCellPrev);
			}
		}
		if (pCellNew)
		{
			pCellNew->m_members.SetBit(memberId.GetRaw());
		}
	}
}

void SimpleGridHash::RemoveMemberNoLock(SimpleGridHashId memberId, Coord_arg coord)
{
	Cell* pCell = LookupCellNoLock(coord);
	if (pCell)
	{
		pCell->m_members.ClearBit(memberId.GetRaw());

		if (pCell->m_members.AreAllBitsClear())
		{
			// this cell no longer contains any members - retire it
			FreeCellNoLock(pCell);
		}
	}
}

void SimpleGridHash::RemoveAllMembersNoLock()
{
	m_usedCells.ClearAllBits();
	m_table.Clear();
}

void SimpleGridHash::GetMembersAtPositionAccumulateNoLock(Coord_arg coord, MemberBits* pResultBits)
{
	AI_ASSERT(pResultBits);

	Cell* pCell = LookupCellNoLock(coord);

	if (pCell)
	{
		MemberBits::BitwiseOr(pResultBits, *pResultBits, pCell->m_members);
	}
}

void SimpleGridHash::GetMembersWithinRadiusWs(Point_arg centerWs, const F32 radius, MemberBits* pResultBits)
{
	AtomicLockJanitorRead jj(&SimpleGridHashManager::s_lock, FILE_LINE_FUNC);

	AI_ASSERT(pResultBits);
	pResultBits->ClearAllBits();

	const Vector diagonal(radius, 0.0f, radius); // really the enclosing square (AABB), not the circle
	Coord min = CoordFromPositionNoLockWs(centerWs - diagonal);
	Coord max = CoordFromPositionNoLockWs(centerWs + diagonal);

	Coord coord;
	for (coord.m_iZ = min.m_iZ; coord.m_iZ <= max.m_iZ; ++coord.m_iZ)
	{
		for (coord.m_iX = min.m_iX; coord.m_iX <= max.m_iX; ++coord.m_iX)
		{
			GetMembersAtPositionAccumulateNoLock(coord, pResultBits);
		}
	}
}

void SimpleGridHash::GetMembersWithinRadiusWsNoLock(Point_arg centerWs, const F32 radius, MemberBits* pResultBits)
{
	AI_ASSERT(pResultBits);
	pResultBits->ClearAllBits();

	const Vector diagonal(radius, 0.0f, radius); // really the enclosing square (AABB), not the circle
	Coord min = CoordFromPositionNoLockWs(centerWs - diagonal);
	Coord max = CoordFromPositionNoLockWs(centerWs + diagonal);

	Coord coord;
	for (coord.m_iZ = min.m_iZ; coord.m_iZ <= max.m_iZ; ++coord.m_iZ)
	{
		for (coord.m_iX = min.m_iX; coord.m_iX <= max.m_iX; ++coord.m_iX)
		{
			GetMembersAtPositionAccumulateNoLock(coord, pResultBits);
		}
	}
}

bool SimpleGridHash::GetMembersFarthestFromPointWs(Point_arg pointWs, MemberBits* pResultBits, MemberBits* pExcludedCellBits)
{
	AtomicLockJanitorRead jj(&SimpleGridHashManager::s_lock, FILE_LINE_FUNC);
	return GetMembersFarthestFromPointWsNoLock(pointWs, pResultBits, pExcludedCellBits);
}

bool SimpleGridHash::GetMembersFarthestFromPointWsNoLock(Point_arg pointWs, MemberBits* pResultBits, MemberBits* pExcludedCellBits)
{
	AI_ASSERT(pResultBits);
	pResultBits->ClearAllBits();

	const Coord coord = CoordFromPositionNoLockWs(pointWs);
	Cell* const pPointCell = LookupCellNoLock(coord);
	float maxDistSqr = 0.0f;
	U64 iFarthestCell = kBitArrayInvalidIndex;

	for (U64 iCell = m_usedCells.FindFirstSetBit(); iCell != kBitArrayInvalidIndex; iCell = m_usedCells.FindNextSetBit(iCell))
	{
		if (pExcludedCellBits && pExcludedCellBits->IsBitSet(iCell))
			continue;

		Cell* const pCell = &m_aCellPool[iCell];
		if (!pCell->m_members.AreAllBitsClear())
		{
			Point boundsMinWs, boundsMaxWs;
			GetCellBoundsNoLockWs(pCell->m_coord, &boundsMinWs, &boundsMaxWs);

			const Point centroidWs = kOrigin + ((boundsMinWs - kOrigin) + (boundsMaxWs - kOrigin)) * SCALAR_LC(0.5f);
			const float distSqr = DistSqr(pointWs, centroidWs);

			if (maxDistSqr < distSqr)
			{
				maxDistSqr = distSqr;
				iFarthestCell = iCell;
			}
		}
	}

	if (iFarthestCell != kBitArrayInvalidIndex)
	{
		if (pExcludedCellBits)
			pExcludedCellBits->SetBit(iFarthestCell); // mark this cell as having been "visited" for subsequent calls

		Cell* const pCell = &m_aCellPool[iFarthestCell];
		MemberBits::Copy(pResultBits, pCell->m_members);
		return true;
	}

	return false;
}

// ---------------------------------------------------------------------------------------------------------------

void SimpleGridHash::Validate() const
{
	STRIP_IN_FINAL_BUILD;

	AtomicLockJanitorRead jj(&SimpleGridHashManager::s_lock, FILE_LINE_FUNC);

	ScopedTempAllocator alloc(FILE_LINE_FUNC);

	// walk the hash table and visit each cell, checking that each unique member id
	// appears only once, and also that each Cell in the hash table has a unique (iX, iZ)

	MemberBits uniqueMemberIds;
	uniqueMemberIds.ClearAllBits();

	AI_ASSERT(m_numCellsAllocated == m_usedCells.GetMaxBitCount());
	size_t numCellsAllocated = m_usedCells.GetMaxBitCount();
	Coord* aUniqueCoord = NDI_NEW Coord[numCellsAllocated];
	I32 numCells = 0;

	ConstTableIterator it = m_table.Begin();
	ConstTableIterator itEnd = m_table.End();

	for ( ; it != itEnd; ++it)
	{
		const Cell* pCell = it->m_data;
		AI_ASSERT(pCell);

		while (pCell)
		{
			// be sure the cell is allocated
			ptrdiff_t iCell = (pCell - m_aCellPool);
			AI_ASSERT(iCell >= 0 && iCell < m_usedCells.GetMaxBitCount());
			AI_ASSERT(m_usedCells.IsBitSet(iCell));

			// be sure each member appears in only one cell
			MemberBits check;
			MemberBits::BitwiseAnd(&check, uniqueMemberIds, pCell->m_members);
			AI_ASSERT(check.AreAllBitsClear());
			MemberBits::BitwiseOr(&uniqueMemberIds, uniqueMemberIds, pCell->m_members);

			// check for coordinate uniqueness
			for (I32 j = 0; j < numCells; ++j)
			{
				AI_ASSERT(aUniqueCoord[j] != pCell->m_coord);
			}
			aUniqueCoord[numCells] = pCell->m_coord;

			// on to the next cell
			++numCells;
			pCell = pCell->m_pNext;
		}
	}
}

void SimpleGridHash::GetStats(Stats* pStats) const
{
	STRIP_IN_FINAL_BUILD;

	AtomicLockJanitorRead jj(&SimpleGridHashManager::s_lock, FILE_LINE_FUNC);

	memset(pStats, 0, sizeof(*pStats));
	pStats->m_minCellsPerLinkedList = (U32)-1;
	pStats->m_totalHashBuckets = m_table.GetNumBuckets();

	AI_ASSERT(m_numCellsAllocated == m_usedCells.GetMaxBitCount());
	pStats->m_totalCells = m_usedCells.CountSetBits();

	ConstTableIterator it = m_table.Begin();
	ConstTableIterator itEnd = m_table.End();

	for ( ; it != itEnd; ++it)
	{
		const Cell* pCell = it->m_data;
		AI_ASSERT(pCell);

		pStats->m_numHashBucketsUsed++;

		U32 linkedListDepth = 0;
		while (pCell)
		{
			linkedListDepth++;
			pCell = pCell->m_pNext;
		}

		if (pStats->m_maxCellsPerLinkedList < linkedListDepth)
			pStats->m_maxCellsPerLinkedList = linkedListDepth;
		if (pStats->m_minCellsPerLinkedList > linkedListDepth)
			pStats->m_minCellsPerLinkedList = linkedListDepth;
	}
}

//static
void SimpleGridHash::UnitTest(U32 tableSize)
{
	STRIP_IN_FINAL_BUILD;

	AtomicLockJanitorWrite jj(&SimpleGridHashManager::s_lock, FILE_LINE_FUNC);

	static const F32 s_aafTestCoords[][3] =
	{
		{ 0.0f, 0.0f, 0.0f },
		{ 0.5f, 0.0f, 0.5f },
		{ -0.5f, 0.0f, 0.5f },
		{ 0.5f, 0.0f, -0.5f },
		{ -0.5f, 0.0f, -0.5f },
		{ 5.5f, 0.0f, 4.5f },
		{ -3.5f, 0.0f, 6.5f },
		{ 7.5f, 0.0f, -9.5f },
		{ -5.5f, 0.0f, -5.5f },
		{ 100.7f, 0.0f, -42.3f },
		{ 23.8f, 0.0f, -18.6f },
	};

	for (I32 i = 0; i < sizeof(s_aafTestCoords)/(3*sizeof(F32)); ++i)
	{
		Point p(s_aafTestCoords[i][0], s_aafTestCoords[i][1], s_aafTestCoords[i][2]);
		SimpleGridHash::Coord coord = SimpleGridHash::CoordFromPositionNoLockWs(p);
		U32 hash = SimpleGridHash::HashCoordinatesNoLock(coord);

		// <choose correct msg>("(%.2f, %.2f, %.2f) -> (%d, %d) -> %u (0x%08X) -mod-> %u (0x%08X)\n",
		// 	s_aafTestCoords[i][0], s_aafTestCoords[i][1], s_aafTestCoords[i][2],
		// 	coord.m_iX, coord.m_iZ, hash, hash, hash % tableSize, hash % tableSize);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Debug Drawing (could go into its own .cpp file)
// ---------------------------------------------------------------------------------------------------------------

#include "gamelib/gameplay/nav/nav-mesh-util.h"

void SimpleGridHash::DebugDrawCell(Coord_arg coord, Point_arg origin) const
{
	STRIP_IN_FINAL_BUILD;

	AtomicLockJanitorRead jj(&SimpleGridHashManager::s_lock, FILE_LINE_FUNC);

	U32 iCell = (U32)-1;
	const MemberBits* pMembers = nullptr;
	bool cellFound = GetDebugInfoNoLock(coord, &iCell, &pMembers);

	ASSERT(cellFound == (iCell != (U32)-1 && pMembers != nullptr));
	DebugDrawCellNoLock(coord, iCell, pMembers, origin);
}

void SimpleGridHash::DebugDrawCellNoLock(Coord_arg coord, U32 iCell, const MemberBits* pMembers, Point_arg origin) const
{
	STRIP_IN_FINAL_BUILD;

	SimpleGridHashManager& mgr = SimpleGridHashManager::Get();

	Point boundsMinWs, boundsMaxWs;
	GetCellBoundsNoLockWs(coord, &boundsMinWs, &boundsMaxWs);

	const Point centerWs = Lerp(boundsMinWs, boundsMaxWs, SCALAR_LC(0.5f));
	char text[1024];
	char* pTextEnd = &text[sizeof(text)];
	char* pText = text;

	if (pMembers)
	{
		ASSERT(iCell != (U32)-1);
		ASSERT(pMembers);
		PrimAttrib primAttrib(kPrimEnableHiddenLineAlpha);

		pText += snprintf(pText, pTextEnd - pText, "cell %d (%d, %d)\n", iCell, coord.m_iX, coord.m_iZ);

		U64 iBit = pMembers->FindFirstSetBit();
		for ( ; iBit != ~0ULL; iBit = pMembers->FindNextSetBit(iBit))
		{
			const SimpleGridHashManager::RegisteredObject* pMember = mgr.GetRecordNoLock(iBit);
			const NdGameObject* pGo = pMember ? pMember->m_info.m_hGo.ToProcess() : nullptr;

			pText += snprintf(pText, pTextEnd - pText, "%u: %s\n", (U32)iBit, pGo ? pGo->GetName() : "<null>");

			if (pMember)
			{
				if (pMember->m_info.m_radiusHard > 0.0f)
					g_prim.Draw(DebugCircle(pMember->m_info.m_posWs, kUnitYAxis, pMember->m_info.m_radiusSoft, kColorCyanTrans, primAttrib));
				if (pMember->m_info.m_radiusSoft > 0.0f)
					g_prim.Draw(DebugCircle(pMember->m_info.m_posWs, kUnitYAxis, pMember->m_info.m_radiusHard, kColorCyan, primAttrib));
			}
		}
	}
	else
	{
		pText += snprintf(pText, pTextEnd - pText, "(%d, %d) / vacant\n", coord.m_iX, coord.m_iZ);
	}

	g_prim.Draw(DebugString(centerWs + (origin - kOrigin) + VECTOR_LC(0.0f, 0.2f, 0.0f), text, kColorBlue, 0.7f));

	Transform xfm(origin);
	DrawWireframeBox(xfm, boundsMinWs, boundsMaxWs, pMembers ? kColorBlue : kColorDarkGray);
}

void SimpleGridHash::DebugDraw(Point_arg origin) const
{
	STRIP_IN_FINAL_BUILD;

	AtomicLockJanitorRead jj(&SimpleGridHashManager::s_lock, FILE_LINE_FUNC);

	// walk the hash table and draw each cell

	ConstTableIterator it = m_table.Begin();
	ConstTableIterator itEnd = m_table.End();

	for ( ; it != itEnd; ++it)
	{
		const Cell* pCell = it->m_data;
		AI_ASSERT(pCell);

		while (pCell)
		{
			ptrdiff_t iCell = (pCell - m_aCellPool);
			DebugDrawCellNoLock(pCell->m_coord, iCell, &pCell->m_members, origin);

			// on to the next cell
			pCell = pCell->m_pNext;
		}
	}
}
