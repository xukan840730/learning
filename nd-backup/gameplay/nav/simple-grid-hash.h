/*
 * Copyright (c) 2010 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/hashtable.h"
#include "corelib/util/bit-array.h"
#include "corelib/util/equality.h"

// ------------------------------------------------------------------------------------------------------------------------
// SimpleGridHashId: Unique index assigned to each member object
// ------------------------------------------------------------------------------------------------------------------------

class SimpleGridHashId
{
public:
	static const I32 kInvalidIndex = -1;
	static SimpleGridHashId kInvalid;

	SimpleGridHashId() : m_index(kInvalidIndex) {}
	explicit SimpleGridHashId(I32 index) : m_index(index) {}

	I32 GetRaw() const { return m_index; }

	bool operator==(const SimpleGridHashId& rhs) const { return m_index == rhs.m_index; }
	bool operator!=(const SimpleGridHashId& rhs) const { return m_index != rhs.m_index; }

private:
	I32 m_index;
};

// ------------------------------------------------------------------------------------------------------------------------
// SimpleGridHash
// ------------------------------------------------------------------------------------------------------------------------

class SimpleGridHash
{
public:
	enum { kMaxMembers = 512 };

	typedef BitArray<kMaxMembers>	MemberBits;

	struct Coord
	{
		I32	m_iX;
		I32	m_iZ;

		bool operator==(const Coord& other) const
		{
			return (m_iX == other.m_iX && m_iZ == other.m_iZ);
		}

		bool operator!=(const Coord& other) const
		{
			return (m_iX != other.m_iX || m_iZ != other.m_iZ);
		}
	};

	typedef Coord Coord_arg;

	static size_t DetermineSizeInBytes(U32 maxNumCells);
	void InitNoLock(U32 maxNumCells);

	static Scalar GetCellSize();
	int GetCellCount() const { return (int)m_usedCells.CountSetBits(); }

	Coord UpdateMemberPositionNoLockWs(SimpleGridHashId memberId, const Coord* pCoordPrev, Point_arg newMemberPosWs);

	void RemoveMemberNoLock(SimpleGridHashId memberId, Coord_arg coord);
	void RemoveAllMembersNoLock();

	void GetMembersWithinRadiusWs(Point_arg centerWs, const F32 radius, MemberBits* pResultBits);
	void GetMembersWithinRadiusWsNoLock(Point_arg centerWs, const F32 radius, MemberBits* pResultBits);

	// get all members in the cell farthest away from the given point, optionally excluding the cells specified by *pExcludedCellBits
	// places all members of the farthest cell into *pResultBits, and optionally sets the bit of the farthest cell in *pExcludedCellBits
	// returns true if a farthest cell was found, false if no more cells are available
	bool GetMembersFarthestFromPointWs(Point_arg pointWs,
									   MemberBits* pResultBits,
									   MemberBits* pExcludedCellBits = nullptr);
	bool GetMembersFarthestFromPointWsNoLock(Point_arg pointWs,
											 MemberBits* pResultBits,
											 MemberBits* pExcludedCellBits = nullptr);

	void DebugDrawCell(Coord_arg coord, Point_arg origin) const;
	void DebugDraw(Point_arg origin) const;
	void Validate() const;

	struct Stats
	{
		U32		m_totalHashBuckets;
		U32		m_numHashBucketsUsed;

		U32		m_totalCells;
		U32		m_minCellsPerLinkedList;
		U32		m_maxCellsPerLinkedList;
	};
	void GetStats(Stats* pStats) const;

private:
	struct Cell
	{
		MemberBits m_members;
		Coord m_coord;
		Cell* m_pNext;
	};

	struct NoOpHashFn
	{
		// we're doing the hashing ourselves, so no point in rehashing here
		inline U32 operator () (const U32 key) const
		{
			return key;
		}
	};

	struct TableTraits
	{
		typedef U32 Key;
		typedef Cell* Data;
	
		typedef NoOpHashFn HashFn;
		typedef equality<U32, U32> KeyCompare;
		typedef op_assign<U32, U32> KeyAssign;
		typedef op_assign<Cell*, Cell*> DataAssign;
	
		static int GetBucketForKey(const Key& k, int numBuckets)
		{
			HashFn hasher;
			return hasher(k) % numBuckets;
		}
	};

	typedef HashTable<U32, Cell*, false, TableTraits>	Table;
	typedef Table::Iterator								TableIterator;
	typedef Table::ConstIterator						ConstTableIterator;

	static Coord CoordFromPositionNoLockWs(Point_arg posWs);
	static void GetCellBoundsNoLockWs(Coord_arg coord, Point* pBoundsMinWs, Point* pBoundsMaxWs);

	void  UpdateMemberPositionNoLock(SimpleGridHashId memberId, const Coord* pCoordPrev, Coord_arg coordNew);

	static U32 HashCoordinatesNoLock(Coord_arg coord);
	Cell* LookupCellInternalNoLock(Coord_arg coord, const bool createIfNotFound);
	Cell* LookupCellNoLock(Coord_arg coord) const;
	Cell* LookupOrCreateCellNoLock(Coord_arg coord);
	Cell* AllocCellNoLock();
	void FreeCellNoLock(Cell* pCell);
	void GetMembersAtPositionAccumulateNoLock(Coord_arg coord, MemberBits* pResultBits);
	void DebugDrawCellNoLock(Coord_arg coord, U32 iCell, const MemberBits* pMembers, Point_arg origin) const;
	bool GetDebugInfoNoLock(Coord_arg coord, U32* piCell, const MemberBits** ppMembers) const;
	static void UnitTest(U32 tableSize);

	Table m_table;
	Cell* m_aCellPool;
	U64* m_pUsedCellsMem;
	ExternalBitArray m_usedCells;
	U32 m_numCellsAllocated; // should match m_usedCells.GetMaxBitCount()

	friend class SimpleGridHashUnitTest;
};

// ------------------------------------------------------------------------------------------------------------------------
// Inlines
// ------------------------------------------------------------------------------------------------------------------------

inline SimpleGridHash::Coord SimpleGridHash::UpdateMemberPositionNoLockWs(SimpleGridHashId memberId,
																		  const Coord* pCoordPrev,
																		  Point_arg newMemberPosWs)
{
	Coord coordNew = CoordFromPositionNoLockWs(newMemberPosWs);
	if (!pCoordPrev || coordNew != *pCoordPrev)
	{
		UpdateMemberPositionNoLock(memberId, pCoordPrev, coordNew);
	}
	return coordNew;
}
