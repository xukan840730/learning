/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/system/read-write-atomic-lock.h"

#include "ndlib/process/process.h"
#include "ndlib/render/ngen/compute-queue-mgr.h"
#include "ndlib/render/shaders/exposure-map-shared-cpu-gpu-constants.h"
#include "ndlib/util/bitmap-2d.h"

#include "gamelib/gameplay/nav/nav-assert.h"
#include "gamelib/gameplay/nav/nav-mesh-handle.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/dynamic-height-map.h"
#include "gamelib/level/level-spec.h"
#include "gamelib/scriptx/h/nd-ai-defines.h"

#ifndef FINAL_BUILD
#define ALLOW_CPU_EXPOSURE_MAP
#endif

// more constants in exposure-map-shared-cpu-gpu-constants.h
// and nav-mesh.h
CONST_EXPR I32 kExposureMapMaxObservers			= 16;
CONST_EXPR I32 kExposureMapObserversPerFrame	= 8;
CONST_EXPR I32 kExposureMapMaxAvoidSpheres		= 64 + 16 + 8 + 32 + 32;
CONST_EXPR I32 kExposureMapMaxDynamicHeightmaps = 128;

class PathWaypointsEx;
class NavLocation;
class NavMeshHeightMap;
class CompositeBody;

// The tools, in nav-mesh-height-map.cpp, write out m_stealth, m_onNavMesh, and heightmap data as bit arrays with 64-bit pitch alignment.
// This is matched in AiExposureData. If you change the pitch alignment, you'll need to change it in both places.

enum class ExposureMapType
{
	kStealthGrassNormal,
	kStealthGrassAlwaysUnexposed,
	kScratch,

	kCount
};

struct AiExposureData
{
	// a block is a U64
	static CONST_EXPR U32 GetNumBlocks(U32 sizeX, U32 sizeZ)
	{
		const U32 numU64sPerRow = (sizeX + 63) >> 6;
		const U32 totalU64s = numU64sPerRow * sizeZ;

		const U32 totalU128s = (totalU64s + 1) >> 1;

		// round up to nearest U128 so we can process the entire array
		// as 128-bit vecs without ever having to deal with a last U64
		// or accidentally read a U64 past the end
		return totalU128s << 1;
	}

	AiExposureData(const U64* pOnNavMesh, I32 sizeX, I32 sizeZ)
		: m_onNavMesh(pOnNavMesh)
		, m_sizeX(sizeX)
		, m_sizeZ(sizeZ)
		, m_originWs(kOrigin)
		, m_originX(0)
		, m_originZ(0)
		, m_pitch((sizeX + 63) & ~63)
		, m_blocks(GetNumBlocks(sizeX, sizeZ))
		, m_any{ false }
		, m_anyAvoid(false)
		, m_data{ nullptr }
		, m_avoid(nullptr)
		, m_threat(nullptr)
	{
		NAV_ASSERT(pOnNavMesh);
	}

	void AllocDataBlocks()
	{
		NAV_ASSERT(m_sizeX && m_sizeZ && m_blocks);

		// type-src pair maps
		{
			const U32 numBlocksPerType = DC::kExposureSourceCount * m_blocks;
			for (I32 iType = 0; iType < int(ExposureMapType::kCount); ++iType)
			{
				m_data[iType] = NDI_NEW(kAlign64) U64[numBlocksPerType];
				ALWAYS_ASSERT(m_data[iType]);
			}
		}

		// avoidance map
		m_avoid = NDI_NEW(kAlign64) U64[m_blocks];
		ALWAYS_ASSERT(m_avoid);

		// threat map
		m_threat = NDI_NEW(kAlign64) U64[m_blocks];
		ALWAYS_ASSERT(m_threat);
	}

	template <ExposureMapType type, DC::ExposureSource src>
	const U64* GetConstExprMap() const
	{
		return m_data[int(type)] + src * m_blocks;
	}

	const U64* GetMap(ExposureMapType type, DC::ExposureSource src) const
	{
		return m_data[int(type)] + src * m_blocks;
	}

	U64* GetMap(ExposureMapType type, DC::ExposureSource src)
	{
		return m_data[int(type)] + src * m_blocks;
	}

	const U64* GetAvoidMap() const 	{ return m_avoid; }
	U64* GetAvoidMap() { return m_avoid; }

	const U64* GetThreatMap() const { return m_threat; }
	U64* GetThreatMap() { return m_threat; }

	void ClearClientMaps()
	{
		for (I32 iSrc = 0; iSrc < DC::kExposureSourceCount; ++iSrc)
		{
			ClearMap(ExposureMapType::kStealthGrassNormal, iSrc);
			ClearMap(ExposureMapType::kStealthGrassAlwaysUnexposed, iSrc);
		}

		ClearMap(m_avoid);
		ClearMap(m_threat);
	}

	void ClearMap(U64* __restrict const pMap)
	{
		__m128i* __restrict data = (__m128i* __restrict)pMap;
		const __m128i v = _mm_setzero_si128();
		for (int i = 0; i < m_blocks >> 1; ++i)
		{
			data[i] = v;
		}
	}

	void ClearMap(ExposureMapType type, DC::ExposureSource src)
	{
		return ClearMap(GetMap(type, src));
	}

	void FillMap(U64* __restrict const pMap)
	{
		__m128i* __restrict data = (__m128i* __restrict)pMap;
		__m128i v = _mm_undefined_si128();
		v = _mm_cmpeq_epi32(v, v);
		for (int i = 0; i < m_blocks >> 1; ++i)
		{
			data[i] = v;
		}
	}

	void FillMap(ExposureMapType type, DC::ExposureSource src)
	{
		FillMap(GetMap(type, src));
	}

	bool IsExposed(const U64* __restrict const pMap, U32 x, U32 z) const
	{
		NAV_ASSERT(x < m_sizeX);
		NAV_ASSERT(z < m_sizeZ);

		const U32 index = z * m_pitch + x;
		return (pMap[index >> 6] >> (index & 63)) & 1;
	}

	bool IsExposed(ExposureMapType type, DC::ExposureSource src, U32 x, U32 z) const
	{
		return IsExposed(GetMap(type, src), x, z);
	}

	bool IsAvd(U32 x, U32 z) const
	{
		NAV_ASSERT(x < m_sizeX);
		NAV_ASSERT(z < m_sizeZ);

		const U32 index = z * m_pitch + x;
		return (m_avoid[index >> 6] >> (index & 63)) & 1;
	}

	bool IsTht(U32 x, U32 z) const
	{
		NAV_ASSERT(x < m_sizeX);
		NAV_ASSERT(z < m_sizeZ);

		const U32 index = z * m_pitch + x;
		return (m_threat[index >> 6] >> (index & 63)) & 1;
	}

	void SetBit(U64* __restrict const pMap, U32 x, U32 z)
	{
		NAV_ASSERT(x < m_sizeX);
		NAV_ASSERT(z < m_sizeZ);

		const U32 index = z * m_pitch + x;
		pMap[index >> 6] |= 1ULL << (index & 63);
	}

	void SetBit(ExposureMapType type, DC::ExposureSource src, U32 x, U32 z)
	{
		SetBit(GetMap(type, src), x, z);
	}

	void BitwiseAnd(U64* __restrict const pA, const U64* __restrict const pB)
	{
		__m128i* __restrict a = (__m128i* __restrict)pA;
		const __m128i* __restrict b = (const __m128i* __restrict)pB;

		for (int i = 0; i < m_blocks >> 1; ++i)
		{
			// use loadu not lddqu for a as it is being written back.
			_mm_storeu_si128(a + i, _mm_and_si128(_mm_loadu_si128(a + i), _mm_lddqu_si128(b + i)));
		}
	}

	void BitwiseOr(U64* __restrict const pA, const U64* __restrict const pB)
	{
		__m128i* __restrict a = (__m128i* __restrict)pA;
		const __m128i* __restrict b = (const __m128i* __restrict)pB;

		for (int i = 0; i < m_blocks >> 1; ++i)
		{
			// use loadu not lddqu for a as it is being written back.
			_mm_storeu_si128(a + i, _mm_or_si128(_mm_loadu_si128(a + i), _mm_lddqu_si128(b + i)));
		}
	}

	float IntegrateExp(const U64* __restrict const pMap, const Point pt0Ws, const Point pt1Ws) const;
	float IntegrateExp(ExposureMapType type, DC::ExposureSource src, const Point pt0Ws, const Point pt1Ws) const
	{
		return IntegrateExp(GetMap(type, src), pt0Ws, pt1Ws);
	}

	float IntegrateAvd(const Point pt0Ws, const Point pt1Ws) const;
	float IntegrateTht(const Point pt0Ws, const Point pt1Ws) const;

	void IntegrateExpAvd(const U64* __restrict const pMap, const Point pt0Ws, const Point pt1Ws, float& expLength, float& avdLength) const;
	void IntegrateExpAvd(ExposureMapType type, DC::ExposureSource src, const Point pt0Ws, const Point pt1Ws, float& expLength, float& avdLength) const
	{
		IntegrateExpAvd(GetMap(type, src), pt0Ws, pt1Ws, expLength, avdLength);
	}

	void IntegrateThtAvd(const Point pt0Ws, const Point pt1Ws, float& thtLength, float& avdLength) const;

	void DebugDraw(const NavMeshHeightMap* pHeightMap, ExposureMapType type, DC::ExposureSource src, bool drawDepthTest) const;

	// type-src pair maps
	U64* m_data[int(ExposureMapType::kCount)];

	// avoidance map
	U64* m_avoid;

	// threat map
	U64* m_threat;

	ndgi::Texture m_heightMapTex;
	ndgi::Texture m_scratchTex;
	Point m_originWs;
	I32 m_originX;
	I32 m_originZ;
	I32 m_sizeX;
	I32 m_sizeZ;
	U32 m_pitch;
	U32 m_blocks;
	const U64* m_onNavMesh;
	bool m_any[DC::kExposureSourceCount];
	bool m_anyAvoid;
};

// https://www.desmos.com/calculator/zxrobty9g4
struct ExposurePolarModel
{
	float m_a;
	float m_b;
	float m_c;
	float m_d;

	ExposurePolarModel() {}
	CONST_EXPR ExposurePolarModel(float a, float b, float c, float d) : m_a(a), m_b(b), m_c(c), m_d(d) {}
	CONST_EXPR ExposurePolarModel(float c, float d) : ExposurePolarModel(0.0f, 0.0f, c, d) {}

	void SetDisabled() { m_c = 0.0f; }
	bool IsEnabled() const { return m_c; }
};

struct ExposureMapObserver
{
	Locator m_locator;
	Locator m_thtLocator;
	NavPolyHandle m_hNavPoly;
	Vector m_eyeOffset;

	// tunable polar visual field model
	// https://www.desmos.com/calculator/zxrobty9g4

	// for main exposure field
	ExposurePolarModel m_exp;
	Point m_expBoundsWs[4];

	// for threat exposure
	ExposurePolarModel m_tht;
	Point m_thtBoundsWs[4];

	void ComputeBounds();

private:
	void ComputeBoundsFor(Point boundsWs[4], const ExposurePolarModel& model, const Locator& loc) const;
};

struct ExposureMapParams
{
	ExposureMapObserver m_observers[kExposureMapMaxObservers];
	U32 m_numObservers;

	ExposureMapParams() : m_numObservers(0) {}
};

struct ExposureSourceState
{
	ExposureSourceState()
		: m_numObs(0)
		, m_curObs(0)
		, m_numMeshes(0)
		, m_numDynamicHeightMaps(0)
	{
		m_meshBits.ClearAllBits();
	}

	ExposureMapParams m_pendingParams;
	ExposureMapObserver m_obs[kExposureMapMaxObservers];
	I32 m_obsX[kExposureMapMaxObservers];
	I32 m_obsZ[kExposureMapMaxObservers];
	NavMeshHandle m_hMeshes[kExposureMapMaxHeightMaps];
	DynamicHeightMap m_dynamicHeightMaps[kExposureMapMaxHeightMaps];
	NavMeshMgr::NavMeshBits m_meshBits;
	I32 m_numObs;
	I32 m_curObs;
	I32 m_numMeshes;
	I32 m_numDynamicHeightMaps;
};

struct ExposureMapState
{
	ExposureMapState()
		: m_exposureMapLock(JlsFixedIndex::kExposureMapLock, SID("ExposureMapLock"))
		, m_pComputeContext(nullptr)
		, m_pLastLabel(nullptr)
		, m_hJobCounter(nullptr)
		, m_numAvoidSpheres(0)
		, m_numDynamicHeightMaps(0)
		, m_curSrc(0)
		, m_enabled(true)
#ifdef ALLOW_CPU_EXPOSURE_MAP
		, m_useGpu(true)
#endif //ALLOW_CPU_EXPOSURE_MAP
	{
		m_avoidBits.ClearAllBits();
		m_threatBits.ClearAllBits();
	}

	ExposureSourceState m_srcState[DC::kExposureSourceCount];
	Sphere m_avoidSpheres[kExposureMapMaxAvoidSpheres];
	DynamicHeightMap m_dynamicHeightMaps[kExposureMapMaxDynamicHeightmaps];

	NavMeshMgr::NavMeshBits m_avoidBits;
	NavMeshMgr::NavMeshBits m_threatBits;

	ndgi::ComputeContext* m_pComputeContext;
	ndgi::Label32* m_pLastLabel;
	ndgi::ComputeQueue m_cmpQueue;

	ndjob::CounterHandle m_hJobCounter;

	NdRwAtomicLock64_Jls m_exposureMapLock;

	I32 m_numAvoidSpheres;
	I32 m_numDynamicHeightMaps;
	I32 m_curSrc;
	bool m_enabled;

#ifdef ALLOW_CPU_EXPOSURE_MAP
	bool m_useGpu;
#endif //ALLOW_CPU_EXPOSURE_MAP
};

class ExposureMapManager
{
public:
	static bool HeightmapRayQuery(const Point pt0Ws, const Point pt1Ws, Point* pHitWs = nullptr, const bool debugDraw = false);
	static bool ComputeBoundingSphereForDeadBodyAvoidance(Sphere& ret, const CompositeBody* const pCompBody);

	void Init();
	void Shutdown();
	void GatherJob();
	void WaitForJob();
	void GatherComputeContext();
	void EnqueueComputeContextWait(ndgi::DeviceContext* pContext);
	ndjob::CounterHandle KickJob();
	ndjob::CounterHandle GetJobCounterHandle() const { return m_state.m_hJobCounter; }
	void UpdateParams(DC::ExposureSource src, const ExposureMapParams& params);
	void UpdateAvoidSpheres(const Sphere* spheres, int numSpheres);
	void UpdateDynamicHeightMaps(const DynamicHeightMap* maps, int numMaps);
	int GetNumAvoidSpheres() const { return m_state.m_numAvoidSpheres; }
	NdRwAtomicLock64_Jls* GetExposureMapLock() { return &m_state.m_exposureMapLock; }

	bool IsAnythingNearbyExposed(NavLocation navLocation, DC::ExposureSource src) const;
	bool GetNavLocationOrNeighboringTht(NavLocation navLocation) const;
	bool GetNavLocationAvd(NavLocation navLocation) const;
	bool GetNavLocationTht(NavLocation navLocation) const;
	bool GetNavLocationExposed(NavLocation navLocation, ExposureMapType type, DC::ExposureSource src) const;
	void GetNavLocationExposed(NavLocation navLocation,
							   DC::ExposureSource src,
							   bool& isExposedWithStealthOccluded,
							   bool& isExposedWithStealthExposed) const;
	void NavLocationQuery(NavLocation navLocation, ExposureMapType type, DC::ExposureSource src, bool& exp, bool& avd, bool& tht) const;
	void NavLocationQuery(NavLocation navLocation, ExposureMapType type1, DC::ExposureSource src1, ExposureMapType type2, DC::ExposureSource src2, bool& exp1, bool& exp2, bool& avd, bool& tht) const;

	F32 CalculatePathExposure(const PathWaypointsEx* pPath,
							  ExposureMapType type,
							  DC::ExposureSource src,
							  F32 distancePenalty) const;

	F32 CalculatePathTht(const PathWaypointsEx* pPath,
						 F32 maxDist) const;

	F32 CalculatePathAvd(const PathWaypointsEx* pPath) const;

private:
	ExposureMapState m_state;

#if !FINAL_BUILD
	bool m_initialized = false;
#endif
};

extern ExposureMapManager g_exposureMapMgr;
