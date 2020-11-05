/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#pragma once

#include "corelib/system/read-write-atomic-lock.h"
#include "corelib/util/bit-array.h"

#include "ndlib/io/pak-structs.h"

class Level;

class Voxelization : public ResItem
{
private:
	static const I32 kVoxelsPerAxis = 8;
	static const F32 kBigVoxelSize;
	static const F32 kSmallVoxelSize;
	static const I32 kVoxelsPerBigVoxel = kVoxelsPerAxis * kVoxelsPerAxis * kVoxelsPerAxis;

	typedef BitArray<kVoxelsPerBigVoxel> BigVoxel;

	struct Voxel
	{
		Voxel() { }

		Voxel(I32 _x, I32 _y, I32 _z)
		{
			x = _x;
			y = _y;
			z = _z;
		}

		Voxel(Point_arg posLs)
		{
			x = floorf(posLs.X() / kSmallVoxelSize);
			y = floorf(posLs.Y() / kSmallVoxelSize);
			z = floorf(posLs.Z() / kSmallVoxelSize);
		}

		Point ToPoint() const
		{
			return Point(kOrigin) + Vector(x, y, z) * kSmallVoxelSize;
		}

		I32 x, y, z;
	};

public:
	bool IsLoggedIn() { return m_managerId != ~0U; }
	
	void Login(Level* pLevel);
	void Logout(Level* pLevel);
	
	void DebugDraw() const;

	bool Raycast(Point_arg fromWs, Point_arg toWs, Point* pHitPos = nullptr, bool debug = false) const;

	StringId64 GetLevelId() const { return m_levelId; }
	void SetLevelId(StringId64 nameId) { m_levelId = nameId; }

	U32 GetManagerId() const { return m_managerId; }
	void SetManagerId(U32 managerId) { m_managerId = managerId; }

private:
	inline Point WorldToLocal(Point_arg posWs) const
	{
		Point posLs = posWs + (Point(kOrigin) - m_minPosWs);
		posLs = Clamp(posLs, Point(kOrigin), Point(kOrigin) + Vector(m_count[0], m_count[1], m_count[2]) * kBigVoxelSize);
		return posLs;
	}

	inline Point LocalToWorld(Point_arg posLs) const
	{
		return posLs + (m_minPosWs - Point(kOrigin));
	}

	inline U32 GetBigVoxelIdx(U32 x, U32 y, U32 z) const
	{
		const U32& xCount = m_count[0];
		const U32& yCount = m_count[1];
		const U32& zCount = m_count[2];

		if (x>=xCount)
			return ~0U;

		if (y>=yCount)
			return ~0U;

		if (z>=zCount)
			return ~0U;

		return x + (y * xCount) + (z * xCount * yCount);
	}

	inline static U32 GetSmallVoxelIdx(U32 x, U32 y, U32 z)
	{
		return x + (y * kVoxelsPerAxis) + (z * kVoxelsPerAxis * kVoxelsPerAxis);
	}

	bool GetIndices(const Voxel& voxel, I32* pBigVoxelIdx, I32* pSmallVoxelIdx) const;

private:
	StringId64	m_levelId;
	U32			m_managerId;

	U32			m_numBigVoxels;
	Point		m_minPosWs;
	Point		m_maxPosWs;
	U32			m_count[3];
	BigVoxel*	m_ppBigVoxels[];

	// actual voxel data stored after this!
};

class NpcVoxelMgr
{
	static const U32 kMaxVoxelizations = 32;
	//typedef BitArray<kMaxVoxelizations> VoxelizationBits;
	typedef BitArray32 VoxelizationBits;

public:
	NpcVoxelMgr();

	void DebugDraw() const;
	bool Raycast(Point_arg fromWs, Point_arg toWs, Point* pHitPos=nullptr, bool debug=false) const;

	void AddVoxelization(Voxelization* pVoxelization, const Level* pLevel);
	void RemoveVoxelization(Voxelization* pVoxelization);

private:
	mutable NdRwAtomicLock64	m_lock;
	VoxelizationBits	m_usedVoxelizations;
	Voxelization*		m_voxelizations[kMaxVoxelizations];
};

class VoxelRayCastJob
{
private:
	struct ProbeData
	{
		Point	m_inStartWs;
		Point	m_inEndWs;

		bool	m_inValid : 1;
		bool	m_outHit : 1;

		Point	m_outHitPosWs;

		void Clear()
		{
			m_outHit = false;
			m_outHitPosWs = kOrigin;
		}
	};

public:
	enum EFlags
	{
		kVoxelCastSynchronous	= 1u << 0,
	};

public:
	VoxelRayCastJob();

	void Open(U32 numProbes, U16 nFlags = 0);
	void Kick(const char* sourceFile, U32 sourceLine, const char* srcFunc, U32 actualNumProbes = 0);
	void Wait();
	void Close();

	bool	IsOpen() const { return m_bOpen; }

	U32 	NumProbes() const { return m_numProbes; }

	void	SetProbeExtents(U32 iProbe, Point_arg start, Point_arg end);

	Point	GetProbeStart(U32 iProbe) const;
	Point	GetProbeEnd(U32 iProbe) const;
	Vector	GetProbeUnitDir(U32 iProbe) const;
	Vector	GetProbeDir(U32 iProbe) const;

	bool	IsContactValid(U32 iProbe) const;
	F32		GetContactT(U32 iProbe) const;
	F32		GetContactLength(U32 iProbe) const;
	Point	GetContactPoint(U32 iProbe) const;

private:
	CLASS_JOB_ENTRY_POINT_DEFINITION(DoRayCastJob);

	void	DoRayCasts();

private:
	bool		m_bOpen : 1;
	bool		m_bDone : 1;
	bool		m_bKicked : 1;

	U16			m_openFlags;

	U32			m_numProbes;
	ProbeData*	m_pProbes;

	ndjob::CounterHandle	m_pCounter;

	const char*		m_file;
	const char*		m_func;
	U32				m_line;

private:
	// Prevent copy construction and assignment
	VoxelRayCastJob(const VoxelRayCastJob&);
	VoxelRayCastJob& operator= (const VoxelRayCastJob& rhs);
};

extern NpcVoxelMgr g_npcVoxelMgr;
