/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/ai/exposure-map.h"

#include "corelib/job/job-system.h"
#include "corelib/job/job-util.h"
#include "corelib/math/basicmath.h"
#include "corelib/math/segment-util.h"

#include "ndlib/math/pretty-math.h"
#include "ndlib/net/nd-net-info.h"
#include "ndlib/render/ndgi/submit-queue.h"
#include "ndlib/render/ndgi/submit-utils.h"
#include "ndlib/render/post/post-shading-win.h"

#include "gamelib/gameplay/ai/base/nd-ai-options.h"
#include "gamelib/gameplay/character-manager.h"
#include "gamelib/gameplay/nav/nav-mesh-mgr.h"
#include "gamelib/gameplay/nav/nav-mesh.h"
#include "gamelib/gameplay/nav/nav-mesh-probe.h"
#include "gamelib/gameplay/nav/nav-mesh-util.h"
#include "gamelib/gameplay/nav/nav-path-find.h"
#include "gamelib/gameplay/nav/path-waypoints-ex.h"
#include "gamelib/gameplay/nav/dynamic-height-map.h"
#include "gamelib/gameplay/nav/traversal-action-pack.h"
#include "gamelib/ndphys/composite-body.h"

#include "ndlib/nd-frame-state.h"

static void DebugDrawModel(const ExposurePolarModel& model, const Locator& loc)
{
	CONST_EXPR int kNumSegs = 128;
	Point lastWs = kInvalidPoint;
	for (int i = 0; i < kNumSegs; ++i)
	{
		const float theta = i * PI_TIMES_2 / (kNumSegs - 1);
		const float sinTheta = Sin(theta);
		const float cosTheta = Cos(theta);

		const float r1 = model.m_a / (1.0f - model.m_b * cosTheta);
		const float r2 = model.m_c / Sqrt(1.0f - model.m_d * cosTheta);
		float r = r2;
		if (r1 > 0.0f)
			r = Min(r, r1);

		const Point curLs = Point(r * sinTheta, 0.0f, r * cosTheta);
		const Point curWs = loc.TransformPoint(curLs) + Vector(0.0f, 0.45f, 0.0f);

		if (!AllComponentsEqual(lastWs, kInvalidPoint))
		{
			g_prim.Draw(DebugLine(lastWs, curWs, kColorBlue, kColorBlue, 4.0f));
		}

		lastWs = curWs;
	}
}

static void ComputeBoundsLs(float& minX, float& minZ, float& maxX, float& maxZ, const ExposurePolarModel& model)
{
	const float a = model.m_a;
	const float b = model.m_b;
	const float c = model.m_c;
	const float d = model.m_d;

	maxX = d ? Sqrt(2.0f*c*c*(1.0f - Sqrt(1.0f - d * d)) / (d * d)) : c;
	minX = -maxX;

	maxZ = c / Sqrt(1.0f - d);
	minZ = Max(-a / (1.0f + b), -c / Sqrt(1.0f + d));
}

void DynamicHeightMap::GenerateTexture()
{
	AI_ASSERT(m_sizeX >= 2);
	AI_ASSERT(m_sizeZ >= 2);
	AI_ASSERT(m_sizeX * m_sizeZ <= kBufSize);

	OrbisTexture* const pOrbisTexture = NDI_NEW(kAllocSingleFrame, kAlign16) OrbisTexture;
	ALWAYS_ASSERT(pOrbisTexture);
	memset(pOrbisTexture, 0, sizeof(OrbisTexture));

	sce::Gnm::DataFormat format = sce::Gnm::kDataFormatR8Unorm;

	sce::Gnm::TileMode tileMode;
	{
		const auto ret = (sce::GpuAddress::computeSurfaceTileMode(&tileMode, sce::GpuAddress::kSurfaceTypeTextureFlat, format, 1));
		ALWAYS_ASSERT(ret == sce::GpuAddress::kStatusSuccess);
	}

	sce::Gnm::TextureSpec spec;
	spec.init();
	spec.m_textureType = sce::Gnm::kTextureType2d;
	spec.m_width = m_sizeX;
	spec.m_height = m_sizeZ;
	spec.m_depth = 1;
	spec.m_pitch = 0;
	spec.m_numMipLevels = 1;
	spec.m_numSlices = 1;
	spec.m_format = format;
	spec.m_tileModeHint = tileMode;
	spec.m_minGpuMode = sce::Gnm::getGpuMode();
	spec.m_numFragments = sce::Gnm::kNumFragments1;

	{
		const auto ret = pOrbisTexture->m_texture.init(&spec);
		ALWAYS_ASSERT(ret == SCE_GNM_OK);
	}

	sce::Gnm::SizeAlign sizeAlign = pOrbisTexture->m_texture.getSizeAlign();
	ALWAYS_ASSERT(sizeAlign.m_size);
	U8* tiledData = NDI_NEW(kAllocGameToGpuResourceRing, Alignment(sizeAlign.m_align)) U8[sizeAlign.m_size];
	ALWAYS_ASSERT(tiledData);

	U64 surfaceOffset = 0;
	U64 surfaceSize = 0;
	I32 errCode = sce::GpuAddress::computeTextureSurfaceOffsetAndSize(&surfaceOffset, &surfaceSize, &pOrbisTexture->m_texture, 0, 0);
	ALWAYS_ASSERT(errCode == 0);

	sce::GpuAddress::TilingParameters tilingParameters;
	tilingParameters.initFromTexture(&pOrbisTexture->m_texture, 0, 0);
	sce::GpuAddress::tileSurface(tiledData + surfaceOffset, m_data, &tilingParameters);

	// create texture
	const ndgi::Texture2dDesc desc(
		m_sizeX,
		m_sizeZ,
		1,
		1,
		ndgi::kR8unorm,
		ndgi::kBindDefault,
		ndgi::SampleDesc(1, 0),
		ndgi::kUsageImmutable,
		0);

	sce::Gnm::ResourceMemoryType memoryType = sce::Gnm::kResourceMemoryTypeRO;

	pOrbisTexture->m_texture.setBaseAddress(tiledData);
	pOrbisTexture->m_texture.setResourceMemoryType(memoryType);
	pOrbisTexture->m_texture.setMipLevelRange(0, desc.m_mipLevels - 1);
	pOrbisTexture->m_info.m_format = desc.m_format;
	pOrbisTexture->m_info.m_usage = desc.m_usage;
	pOrbisTexture->m_info.m_bindFlags = desc.m_bindFlags;
	pOrbisTexture->m_info.m_miscFlags = desc.m_miscFlags;
	pOrbisTexture->m_info.m_numTotalMips = desc.m_mipLevels;
	pOrbisTexture->m_size = sizeAlign.m_size;
	pOrbisTexture->m_info.m_alignment = sizeAlign.m_align;
	pOrbisTexture->m_memContext = kAllocGpuDynamicRenderTarget;

	m_tex.Set(pOrbisTexture);
	ALWAYS_ASSERT(m_tex.Valid());
}

void DynamicHeightMap::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	const Point drawOriginWs = m_bboxMinWs + 0.5f * Vector(kHeightMapGridSpacing, 0.0f, kHeightMapGridSpacing);
	const Color color = m_expSourceMask == (1U << DC::kExposureSourceFutureEnemyNpcs) ? kColorOrange : kColorRed;

	const Vector vScale(kHeightMapGridSpacing, kHeightMapHeightSpacing, kHeightMapGridSpacing);
	for (int iZ = 0; iZ < m_sizeZ; ++iZ)
	{
		U8 iPrevY = 0;
		Point ptPrevWs;
		bool prevHole = true;

		for (int iX = 0; iX < m_sizeX; ++iX)
		{
			const U32 idx = iZ * m_sizeX + iX;
			const U8 iY = m_data[idx];
			const bool hole = !iY;
			if (!hole)
			{
				Point ptWs = drawOriginWs + vScale * Vector(iX, iY, iZ);

				if (!prevHole)
				{
					g_prim.Draw(DebugLine(ptPrevWs, ptWs, color, color, 2.0f, kPrimEnableHiddenLineAlpha));
				}

				ptPrevWs = ptWs;
				iPrevY = iY;
			}
			prevHole = hole;
		}
	}

	if (g_navMeshDrawFilter.m_drawDynamicHeightMaps)
	{
		const Aabb& aabbWs = GetAabbWs();
		DrawWireframeBox(kIdentity, aabbWs.m_min, aabbWs.m_max, kColorBlue);
	}
}

void ExposureMapObserver::ComputeBoundsFor(Point boundsWs[4], const ExposurePolarModel& model, const Locator& loc) const
{
	float minX, minZ, maxX, maxZ;
	ComputeBoundsLs(minX, minZ, maxX, maxZ, model);

	CONST_EXPR float kPad = 1.0f;
	minX -= kPad;
	maxX += kPad;
	minZ -= kPad;
	maxZ += kPad;

	const Point boundsLs[4] = {
		{minX, 0.0f, minZ},
		{minX, 0.0f, maxZ},
		{maxX, 0.0f, maxZ},
		{maxX, 0.0f, minZ},
	};

	if (minX == -maxX && minZ == -maxZ && minX == minZ)
	{
		for (int i = 0; i < 4; ++i)
		{
			boundsWs[i] = PointFromXzAndY(loc.Pos() + AsVector(boundsLs[i]), 0.0f);
		}
	}
	else
	{
		for (int i = 0; i < 4; ++i)
		{
			boundsWs[i] = PointFromXzAndY(loc.TransformPoint(boundsLs[i]), 0.0f);
		}
	}
}

static bool QuadContainsPoint(const Point quad[4], Point p)
{
	const __m128 V0 = quad[0].QuadwordValue();
	const __m128 V1 = quad[1].QuadwordValue();
	const __m128 V2 = quad[2].QuadwordValue();
	const __m128 V3 = quad[3].QuadwordValue();
	const __m128 EV01 = _mm_shuffle_ps(_mm_sub_ps(V1, V0), _mm_sub_ps(V2, V1), 136);
	const __m128 EV23 = _mm_shuffle_ps(_mm_sub_ps(V3, V2), _mm_sub_ps(V0, V3), 136);
	const __m128 PV01s = _mm_shuffle_ps(_mm_sub_ps(V0, p.QuadwordValue()), _mm_sub_ps(V1, p.QuadwordValue()), 34);
	const __m128 PV23s = _mm_shuffle_ps(_mm_sub_ps(V2, p.QuadwordValue()), _mm_sub_ps(V3, p.QuadwordValue()), 34);
	const __m128 M01 = _mm_mul_ps(EV01, PV01s);
	const __m128 M23 = _mm_mul_ps(EV23, PV23s);
	const __m128 R = _mm_hsub_ps(M01, M23);
	return _mm_testz_ps(R, R);
}

static Aabb IntersectModelAabb(const Point modelBoundsWs[4], Aabb inAabb)
{
	for (int i = 0; i < 4; ++i)
		NAV_ASSERT(!modelBoundsWs[i].Y());

	inAabb.m_min.SetY(0.0f);
	inAabb.m_max.SetY(0.0f);

	const Point inMin = inAabb.m_min;
	const Point inMax = inAabb.m_max;

	const Point inAabbBoundsWs[4] = {
		{inMin.X(), 0.0f, inMin.Z()},
		{inMin.X(), 0.0f, inMax.Z()},
		{inMax.X(), 0.0f, inMax.Z()},
		{inMax.X(), 0.0f, inMin.Z()},
	};

	Aabb aabb;

	for (int i = 0; i < 4; ++i)
	{
		if (inAabb.ContainsPoint(modelBoundsWs[i]))
			aabb.IncludePoint(modelBoundsWs[i]);
	}

	for (int i = 0; i < 4; ++i)
	{
		if (QuadContainsPoint(modelBoundsWs, inAabbBoundsWs[i]))
			aabb.IncludePoint(inAabbBoundsWs[i]);
	}

	Point bPolar = modelBoundsWs[3];
	for (int iPolar = 0; iPolar < 4; ++iPolar)
	{
		const Point aPolar = modelBoundsWs[iPolar];

		Point bIn = inAabbBoundsWs[3];
		for (int iIn = 0; iIn < 4; ++iIn)
		{
			const Point aIn = inAabbBoundsWs[iIn];

			Scalar t0, t1;
			if (IntersectSegmentSegmentXz({ aPolar, bPolar }, { aIn, bIn }, t0, t1))
			{
				const Point intersectionPt = aPolar + t0 * (bPolar - aPolar);
				//g_prim.Draw(DebugSphere(intersectionPt + Vector(0.0f, 0.3f, 0.0f), 0.6f, kColorBlue));
				aabb.IncludePoint(intersectionPt);
			}

			bIn = aIn;
		}

		bPolar = aPolar;
	}

	return aabb;
}

void ExposureMapObserver::ComputeBounds()
{
	ComputeBoundsFor(m_expBoundsWs, m_exp, m_locator);
	ComputeBoundsFor(m_thtBoundsWs, m_tht, m_locator);
}

static bool NearestOnNavMeshExpQuery(const AiExposureData* __restrict const pData, const U64* __restrict const pMap, U32 x, U32 z)
{
	// exposure maps are a little larger than the largest extent of their navmesh,
	// so we can safely discard if we're within 1 cell of the border.
	// This will allow us to check the cell and its neighbors without having to border clamp.

	const U32 sizeX = pData->m_sizeX;
	const U32 sizeZ = pData->m_sizeZ;

	//NAV_ASSERT(sizeX >= 2 && sizeZ >= 2);

	if (U32(x - 1U) >= U32(sizeX - 2U) || U32(z - 1U) >= U32(sizeZ - 2U))
	{
		return false;
	}

	// okay, now we have x and z both > min_idx and < max_idx
	// we can safely add or sub 1 either way and not fall off
	// now we check the cell itself and each of its 4 cardinal neighbors until we find one that's on navmesh.
	// we return the value of that one's exposure cell.

	const U64* __restrict const pOnNavMesh = pData->m_onNavMesh;
	const U64 pitch = pData->m_pitch;

	// index common to both onNavMesh and exposure bitmaps
	const U64 baseIdx = z * pitch + x;

	// {x, z}
	{
		const U64 idx = baseIdx;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return pMap[idx >> 6] & (1ULL << (idx & 63U));
	}

	// {x + 1, z}
	{
		const U64 idx = baseIdx + 1;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return pMap[idx >> 6] & (1ULL << (idx & 63U));
	}

	// {x - 1, z}
	{
		const U64 idx = baseIdx - 1;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return pMap[idx >> 6] & (1ULL << (idx & 63U));
	}

	// {x, z + 1}
	{
		const U64 idx = baseIdx + pitch;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return pMap[idx >> 6] & (1ULL << (idx & 63U));
	}

	// {x, z - 1}
	{
		const U64 idx = baseIdx - pitch;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return pMap[idx >> 6] & (1ULL << (idx & 63U));
	}

	return false;
}

float AiExposureData::IntegrateExp(const U64* __restrict const pMap, const Point pt0Ws, const Point pt1Ws) const
{
	const Point originWs = m_originWs;

	const Point pt0Ls = AsPoint((pt0Ws - originWs) * kInvHeightMapGridSpacing);
	const Point pt1Ls = AsPoint((pt1Ws - originWs) * kInvHeightMapGridSpacing);

	int x0 = FastCvtToIntFloor(pt0Ls.X());
	int z0 = FastCvtToIntFloor(pt0Ls.Z());
	int x1 = FastCvtToIntFloor(pt1Ls.X());
	int z1 = FastCvtToIntFloor(pt1Ls.Z());

	int dX = x1 - x0;
	if (dX < 0)
		dX = -dX;

	int dZ = z1 - z0;
	if (dZ > 0)
		dZ = -dZ;

	const int sX = (x0 < x1) ? 1 : -1;
	const int sZ = (z0 < z1) ? 1 : -1;

	int err = dX + dZ;

	float exp = NearestOnNavMeshExpQuery(this, pMap, x0, z0) ? kHeightMapGridSpacing : 0.0f;

	while (x0 != x1 || z0 != z1)
	{
		int e2 = err << 1;
		if (e2 > dZ)
		{
			err += dZ;
			x0 += sX;
		}
		if (e2 < dX)
		{
			err += dX;
			z0 += sZ;
		}
		if (NearestOnNavMeshExpQuery(this, pMap, x0, z0))
			exp += kHeightMapGridSpacing;
	}

	return exp * Dist(pt0Ls, pt1Ls) / float(Max(dX, -dZ) + 1);
}

static bool NearestOnNavMeshThtQuery(const AiExposureData* __restrict const pData, U32 x, U32 z)
{
	// exposure maps are a little larger than the largest extent of their navmesh,
	// so we can safely discard if we're within 1 cell of the border.
	// This will allow us to check the cell and its neighbors without having to border clamp.

	const U32 sizeX = pData->m_sizeX;
	const U32 sizeZ = pData->m_sizeZ;

	//NAV_ASSERT(sizeX >= 2 && sizeZ >= 2);

	if (U32(x - 1U) >= U32(sizeX - 2U) || U32(z - 1U) >= U32(sizeZ - 2U))
	{
		return false;
	}

	// okay, now we have x and z both > min_idx and < max_idx
	// we can safely add or sub 1 either way and not fall off
	// now we check the cell itself and each of its 4 cardinal neighbors until we find one that's on navmesh.
	// we return the value of that one's exposure cell.

	const U64* __restrict const pOnNavMesh = pData->m_onNavMesh;
	const U64* __restrict const pTht = pData->m_threat;
	const U64 pitch = pData->m_pitch;

	// index common to both onNavMesh and exposure bitmaps
	const U64 baseIdx = z * pitch + x;

	// {x, z}
	{
		const U64 idx = baseIdx;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return pTht[idx >> 6] & (1ULL << (idx & 63U));
	}

	// {x + 1, z}
	{
		const U64 idx = baseIdx + 1;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return pTht[idx >> 6] & (1ULL << (idx & 63U));
	}

	// {x - 1, z}
	{
		const U64 idx = baseIdx - 1;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return pTht[idx >> 6] & (1ULL << (idx & 63U));
	}

	// {x, z + 1}
	{
		const U64 idx = baseIdx + pitch;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return pTht[idx >> 6] & (1ULL << (idx & 63U));
	}

	// {x, z - 1}
	{
		const U64 idx = baseIdx - pitch;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return pTht[idx >> 6] & (1ULL << (idx & 63U));
	}

	return false;
}

static bool NearestOnNavMeshAvdQuery(const AiExposureData* __restrict const pData, U32 x, U32 z)
{
	// exposure maps are a little larger than the largest extent of their navmesh,
	// so we can safely discard if we're within 1 cell of the border.
	// This will allow us to check the cell and its neighbors without having to border clamp.

	const U32 sizeX = pData->m_sizeX;
	const U32 sizeZ = pData->m_sizeZ;

	//NAV_ASSERT(sizeX >= 2 && sizeZ >= 2);

	if (U32(x - 1U) >= U32(sizeX - 2U) || U32(z - 1U) >= U32(sizeZ - 2U))
	{
		return false;
	}

	// okay, now we have x and z both > min_idx and < max_idx
	// we can safely add or sub 1 either way and not fall off
	// now we check the cell itself and each of its 4 cardinal neighbors until we find one that's on navmesh.
	// we return the value of that one's exposure cell.

	const U64* __restrict const pOnNavMesh = pData->m_onNavMesh;
	const U64* __restrict const pAvd = pData->m_avoid;
	const U64 pitch = pData->m_pitch;

	// index common to both onNavMesh and exposure bitmaps
	const U64 baseIdx = z * pitch + x;

	// {x, z}
	{
		const U64 idx = baseIdx;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return pAvd[idx >> 6] & (1ULL << (idx & 63U));
	}

	// {x + 1, z}
	{
		const U64 idx = baseIdx + 1;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return pAvd[idx >> 6] & (1ULL << (idx & 63U));
	}

	// {x - 1, z}
	{
		const U64 idx = baseIdx - 1;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return pAvd[idx >> 6] & (1ULL << (idx & 63U));
	}

	// {x, z + 1}
	{
		const U64 idx = baseIdx + pitch;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return pAvd[idx >> 6] & (1ULL << (idx & 63U));
	}

	// {x, z - 1}
	{
		const U64 idx = baseIdx - pitch;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return pAvd[idx >> 6] & (1ULL << (idx & 63U));
	}

	return false;
}

float AiExposureData::IntegrateAvd(const Point pt0Ws, const Point pt1Ws) const
{
	const Point originWs = m_originWs;

	const Point pt0Ls = AsPoint((pt0Ws - originWs) * kInvHeightMapGridSpacing);
	const Point pt1Ls = AsPoint((pt1Ws - originWs) * kInvHeightMapGridSpacing);

	int x0 = FastCvtToIntFloor(pt0Ls.X());
	int z0 = FastCvtToIntFloor(pt0Ls.Z());
	int x1 = FastCvtToIntFloor(pt1Ls.X());
	int z1 = FastCvtToIntFloor(pt1Ls.Z());

	int dX = x1 - x0;
	if (dX < 0)
		dX = -dX;

	int dZ = z1 - z0;
	if (dZ > 0)
		dZ = -dZ;

	const int sX = (x0 < x1) ? 1 : -1;
	const int sZ = (z0 < z1) ? 1 : -1;

	int err = dX + dZ;

	float avd = NearestOnNavMeshAvdQuery(this, x0, z0) ? kHeightMapGridSpacing : 0.0f;

	while (x0 != x1 || z0 != z1)
	{
		int e2 = err << 1;
		if (e2 > dZ)
		{
			err += dZ;
			x0 += sX;
		}
		if (e2 < dX)
		{
			err += dX;
			z0 += sZ;
		}
		if (NearestOnNavMeshAvdQuery(this, x0, z0))
			avd += kHeightMapGridSpacing;
	}

	return avd * Dist(pt0Ls, pt1Ls) / float(Max(dX, -dZ) + 1);
}

float AiExposureData::IntegrateTht(const Point pt0Ws, const Point pt1Ws) const
{
	const Point originWs = m_originWs;

	const Point pt0Ls = AsPoint((pt0Ws - originWs) * kInvHeightMapGridSpacing);
	const Point pt1Ls = AsPoint((pt1Ws - originWs) * kInvHeightMapGridSpacing);

	int x0 = FastCvtToIntFloor(pt0Ls.X());
	int z0 = FastCvtToIntFloor(pt0Ls.Z());
	int x1 = FastCvtToIntFloor(pt1Ls.X());
	int z1 = FastCvtToIntFloor(pt1Ls.Z());

	int dX = x1 - x0;
	if (dX < 0)
		dX = -dX;

	int dZ = z1 - z0;
	if (dZ > 0)
		dZ = -dZ;

	const int sX = (x0 < x1) ? 1 : -1;
	const int sZ = (z0 < z1) ? 1 : -1;

	int err = dX + dZ;

	float tht = NearestOnNavMeshThtQuery(this, x0, z0) ? kHeightMapGridSpacing : 0.0f;

	while (x0 != x1 || z0 != z1)
	{
		int e2 = err << 1;
		if (e2 > dZ)
		{
			err += dZ;
			x0 += sX;
		}
		if (e2 < dX)
		{
			err += dX;
			z0 += sZ;
		}
		if (NearestOnNavMeshThtQuery(this, x0, z0))
			tht += kHeightMapGridSpacing;
	}

	return tht * Dist(pt0Ls, pt1Ls) / float(Max(dX, -dZ) + 1);
}

struct ExpAvdPair
{
	bool exp;
	bool avd;
};

static ExpAvdPair NearestOnNavMeshExpAvdQuery(const AiExposureData* __restrict const pData, const U64* __restrict const pExp, U32 x, U32 z)
{
	// exposure maps are a little larger than the largest extent of their navmesh,
	// so we can safely discard if we're within 1 cell of the border.
	// This will allow us to check the cell and its neighbors without having to border clamp.

	const U32 sizeX = pData->m_sizeX;
	const U32 sizeZ = pData->m_sizeZ;

	//NAV_ASSERT(sizeX >= 2 && sizeZ >= 2);

	if (U32(x - 1U) >= U32(sizeX - 2U) || U32(z - 1U) >= U32(sizeZ - 2U))
	{
		return { false, false };
	}

	// okay, now we have x and z both > min_idx and < max_idx
	// we can safely add or sub 1 either way and not fall off
	// now we check the cell itself and each of its 4 cardinal neighbors until we find one that's on navmesh.
	// we return the value of that one's exposure cell.

	const U64* __restrict const pOnNavMesh = pData->m_onNavMesh;
	const U64* __restrict const pAvoid = pData->m_avoid;
	const U64 pitch = pData->m_pitch;

	// index common to both onNavMesh and exposure bitmaps
	const U64 baseIdx = z * pitch + x;

	// {x, z}
	{
		const U64 idx = baseIdx;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return { bool(pExp[idx >> 6] & (1ULL << (idx & 63U))), bool(pAvoid[idx >> 6] & (1ULL << (idx & 63U))) };
	}

	// {x + 1, z}
	{
		const U64 idx = baseIdx + 1;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return { bool(pExp[idx >> 6] & (1ULL << (idx & 63U))), bool(pAvoid[idx >> 6] & (1ULL << (idx & 63U))) };
	}

	// {x - 1, z}
	{
		const U64 idx = baseIdx - 1;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return { bool(pExp[idx >> 6] & (1ULL << (idx & 63U))), bool(pAvoid[idx >> 6] & (1ULL << (idx & 63U))) };
	}

	// {x, z + 1}
	{
		const U64 idx = baseIdx + pitch;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return { bool(pExp[idx >> 6] & (1ULL << (idx & 63U))), bool(pAvoid[idx >> 6] & (1ULL << (idx & 63U))) };
	}

	// {x, z - 1}
	{
		const U64 idx = baseIdx - pitch;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return { bool(pExp[idx >> 6] & (1ULL << (idx & 63U))), bool(pAvoid[idx >> 6] & (1ULL << (idx & 63U))) };
	}

	return { false, false };
}

struct ThtAvdPair
{
	bool tht;
	bool avd;
};

static ThtAvdPair NearestOnNavMeshThtAvdQuery(const AiExposureData* __restrict const pData, const U64* __restrict const pTht, U32 x, U32 z)
{
	// exposure maps are a little larger than the largest extent of their navmesh,
	// so we can safely discard if we're within 1 cell of the border.
	// This will allow us to check the cell and its neighbors without having to border clamp.

	const U32 sizeX = pData->m_sizeX;
	const U32 sizeZ = pData->m_sizeZ;

	//NAV_ASSERT(sizeX >= 2 && sizeZ >= 2);

	if (U32(x - 1U) >= U32(sizeX - 2U) || U32(z - 1U) >= U32(sizeZ - 2U))
	{
		return { false, false };
	}

	// okay, now we have x and z both > min_idx and < max_idx
	// we can safely add or sub 1 either way and not fall off
	// now we check the cell itself and each of its 4 cardinal neighbors until we find one that's on navmesh.
	// we return the value of that one's exposure cell.

	const U64* __restrict const pOnNavMesh = pData->m_onNavMesh;
	const U64* __restrict const pAvd = pData->m_avoid;
	const U64 pitch = pData->m_pitch;

	// index common to both onNavMesh and exposure bitmaps
	const U64 baseIdx = z * pitch + x;

	// {x, z}
	{
		const U64 idx = baseIdx;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return { bool(pTht[idx >> 6] & (1ULL << (idx & 63U))), bool(pAvd[idx >> 6] & (1ULL << (idx & 63U))) };
	}

	// {x + 1, z}
	{
		const U64 idx = baseIdx + 1;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return { bool(pTht[idx >> 6] & (1ULL << (idx & 63U))), bool(pAvd[idx >> 6] & (1ULL << (idx & 63U))) };
	}

	// {x - 1, z}
	{
		const U64 idx = baseIdx - 1;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return { bool(pTht[idx >> 6] & (1ULL << (idx & 63U))), bool(pAvd[idx >> 6] & (1ULL << (idx & 63U))) };
	}

	// {x, z + 1}
	{
		const U64 idx = baseIdx + pitch;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return { bool(pTht[idx >> 6] & (1ULL << (idx & 63U))), bool(pAvd[idx >> 6] & (1ULL << (idx & 63U))) };
	}

	// {x, z - 1}
	{
		const U64 idx = baseIdx - pitch;
		if (pOnNavMesh[idx >> 6] & (1ULL << (idx & 63U)))
			return { bool(pTht[idx >> 6] & (1ULL << (idx & 63U))), bool(pAvd[idx >> 6] & (1ULL << (idx & 63U))) };
	}

	return { false, false };
}

///-----------------------------------------------------------------------------------------------
void AiExposureData::IntegrateExpAvd(const U64* __restrict const pMap, const Point pt0Ws, const Point pt1Ws, float& expLength, float& avdLength) const
{
	const Point originWs = m_originWs;

	const Point pt0Ls = AsPoint((pt0Ws - originWs) * kInvHeightMapGridSpacing);
	const Point pt1Ls = AsPoint((pt1Ws - originWs) * kInvHeightMapGridSpacing);

	int x0 = FastCvtToIntFloor(pt0Ls.X());
	int z0 = FastCvtToIntFloor(pt0Ls.Z());
	int x1 = FastCvtToIntFloor(pt1Ls.X());
	int z1 = FastCvtToIntFloor(pt1Ls.Z());

	int dX = x1 - x0;
	if (dX < 0)
		dX = -dX;

	int dZ = z1 - z0;
	if (dZ > 0)
		dZ = -dZ;

	const int sX = (x0 < x1) ? 1 : -1;
	const int sZ = (z0 < z1) ? 1 : -1;

	int err = dX + dZ;

	ExpAvdPair results = NearestOnNavMeshExpAvdQuery(this, pMap, x0, z0);
	float exp = results.exp ? kHeightMapGridSpacing : 0.0f;
	float avd = results.avd ? kHeightMapGridSpacing : 0.0f;

	while (x0 != x1 || z0 != z1)
	{
		int e2 = err << 1;
		if (e2 > dZ)
		{
			err += dZ;
			x0 += sX;
		}
		if (e2 < dX)
		{
			err += dX;
			z0 += sZ;
		}
		results = NearestOnNavMeshExpAvdQuery(this, pMap, x0, z0);
		if (results.exp)
			exp += kHeightMapGridSpacing;
		if (results.avd)
			avd += kHeightMapGridSpacing;
	}

	const float s = Dist(pt0Ls, pt1Ls) / float(Max(dX, -dZ) + 1);
	expLength = exp * s;
	avdLength = avd * s;
}

///-----------------------------------------------------------------------------------------------
void AiExposureData::IntegrateThtAvd(const Point pt0Ws, const Point pt1Ws, float& thtLength, float& avdLength) const
{
	const Point originWs = m_originWs;

	const Point pt0Ls = AsPoint((pt0Ws - originWs) * kInvHeightMapGridSpacing);
	const Point pt1Ls = AsPoint((pt1Ws - originWs) * kInvHeightMapGridSpacing);

	int x0 = FastCvtToIntFloor(pt0Ls.X());
	int z0 = FastCvtToIntFloor(pt0Ls.Z());
	int x1 = FastCvtToIntFloor(pt1Ls.X());
	int z1 = FastCvtToIntFloor(pt1Ls.Z());

	int dX = x1 - x0;
	if (dX < 0)
		dX = -dX;

	int dZ = z1 - z0;
	if (dZ > 0)
		dZ = -dZ;

	const int sX = (x0 < x1) ? 1 : -1;
	const int sZ = (z0 < z1) ? 1 : -1;

	int err = dX + dZ;

	ThtAvdPair results = NearestOnNavMeshThtAvdQuery(this, GetThreatMap(), x0, z0);
	float tht = results.tht ? kHeightMapGridSpacing : 0.0f;
	float avd = results.avd ? kHeightMapGridSpacing : 0.0f;

	while (x0 != x1 || z0 != z1)
	{
		int e2 = err << 1;
		if (e2 > dZ)
		{
			err += dZ;
			x0 += sX;
		}
		if (e2 < dX)
		{
			err += dX;
			z0 += sZ;
		}
		results = NearestOnNavMeshThtAvdQuery(this, GetThreatMap(), x0, z0);
		if (results.tht)
			tht += kHeightMapGridSpacing;
		if (results.avd)
			avd += kHeightMapGridSpacing;
	}

	const float s = Dist(pt0Ls, pt1Ls) / float(Max(dX, -dZ) + 1);
	thtLength = tht * s;
	avdLength = avd * s;
}

///-----------------------------------------------------------------------------------------------
void AiExposureData::DebugDraw(const NavMeshHeightMap* pHeightMap, ExposureMapType type, DC::ExposureSource src, bool drawDepthTest) const
{
	AtomicLockJanitorRead_Jls exposureMapPointerLock(g_exposureMapMgr.GetExposureMapLock(), FILE_LINE_FUNC);

	Color colorPlr = kColorRedTrans;
	Color colorCur = kColorRedTrans;
	Color colorFut = kColorBlueTrans;
	Color colorBth = kColorPurpleTrans;
	Color colorAvd = kColorMagentaTrans;
	Color colorTht = kColorGreenTrans;

	colorCur.a = 0.5f;
	colorFut.a = 0.5f;
	colorBth.a = 0.5f;
	colorAvd.a = 0.4f;
	colorTht.a = 0.5f;

	const bool drawPlr = src == DC::kExposureSourcePlayer;
	const bool drawCur = src == DC::kExposureSourceEnemyNpcs || src >= DC::kExposureSourceCount || src == -4;
	const bool drawFut = src == DC::kExposureSourceFutureEnemyNpcs || src >= DC::kExposureSourceCount || src == -4;
	const bool drawAvd = src == -2;
	const bool drawTht = src == -3 || src == -4;

	const U64* __restrict const pPlr = GetMap(type, DC::kExposureSourcePlayer);
	const U64* __restrict const pCur = GetMap(type, DC::kExposureSourceEnemyNpcs);
	const U64* __restrict const pFut = GetMap(type, DC::kExposureSourceFutureEnemyNpcs);
	const U64* __restrict const pAvd = GetAvoidMap();
	const U64* __restrict const pTht = GetThreatMap();

	Transform tmLsToWs;
	tmLsToWs.SetScale(Vector(kHeightMapGridSpacing, kHeightMapHeightSpacing, kHeightMapGridSpacing));
	tmLsToWs.SetTranslation(m_originWs);

	const PrimAttrib primAttrib = drawDepthTest ? PrimAttrib(kPrimEnableDepthTest, kPrimDisableDepthWrite) : PrimAttrib(kPrimDisableDepthTest);

	for (I32 iZ = 0; iZ < m_sizeZ; ++iZ)
	{
		for (I32 iX = 0; iX < m_sizeX; ++iX)
		{
			const I32 sampleIndex = iZ * m_sizeX + iX;
			const U8 y = pHeightMap->m_data[sampleIndex];

			if (y)
			{
				const bool bPlr = drawPlr && IsExposed(pPlr, iX, iZ);
				const bool bCur = drawCur && IsExposed(pCur, iX, iZ);
				const bool bFut = drawFut && IsExposed(pFut, iX, iZ);
				const bool bAvd = drawAvd && IsExposed(pAvd, iX, iZ);
				const bool bTht = drawTht && IsExposed(pTht, iX, iZ);

				if (bPlr || bCur || bFut || bAvd || bTht)
				{
					Point v1 = Point(iX, y, iZ) * tmLsToWs;
					Point v2 = Point(iX, y, iZ + 1) * tmLsToWs;
					Point v3 = Point(iX + 1, y, iZ + 1) * tmLsToWs;
					Point v4 = Point(iX + 1, y, iZ) * tmLsToWs;

					if (drawDepthTest)
					{
						const Vector offset = Vector(0.0f, 0.1f, 0.0f);
						v1 += offset;
						v2 += offset;
						v3 += offset;
						v4 += offset;
					}

					g_prim.Draw(DebugQuad(
						v1,
						v2,
						v3,
						v4,
						bTht ? colorTht : bAvd ? colorAvd : bCur && bFut ? colorBth : bCur ? colorCur : bFut ? colorFut : colorPlr,
						primAttrib));
				}
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static F32 CostFuncScriptedBuddy(const Nav::PathFindContext&,
								 const NavPathNodeProxy& pathNode,
								 const NavPathNode::Link&,
								 Point_arg fromPosPs,
								 Point_arg toPosPs,
								 bool*)
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	return pathNode.IsActionPackNode() ? 10.0f : (float)Dist(fromPosPs, toPosPs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static F32 CostFuncSneak(const Nav::PathFindContext&,
						 const NavPathNodeProxy& pathNode,
						 const NavPathNode::Link&,
						 Point_arg fromPosPs,
						 Point_arg toPosPs,
						 bool*)
{
	NAV_ASSERT(g_exposureMapMgr.GetExposureMapLock()->IsLockedForRead());
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	F32 dist = Dist(fromPosPs, toPosPs);

	if (pathNode.IsActionPackNode())
	{
		NAV_ASSERT(pathNode.GetActionPack()->GetType() == ActionPack::kTraversalActionPack);

		const TraversalActionPack* pTap = static_cast<const TraversalActionPack*>(pathNode.GetActionPack());
		const NavLocation& entryNavLoc = pTap->GetSourceNavLocation();
		const NavLocation& exitNavLoc = pTap->GetDestNavLocation();

		const bool isDoor = pTap->IsDoorOpen();
		if (isDoor)
			dist += 3.5f;

		// for tAPs, allow stealth grass to count as exposed
		const bool entryExposed = g_exposureMapMgr.GetNavLocationExposed(entryNavLoc, ExposureMapType::kStealthGrassNormal, DC::kExposureSourceEnemyNpcs);
		const bool exitExposed = g_exposureMapMgr.GetNavLocationExposed(exitNavLoc, ExposureMapType::kStealthGrassNormal, DC::kExposureSourceEnemyNpcs);

		// weight the tAP based on how exposed its entry and exit are
		float expFactor = 1.0f;
		if (exitExposed)
		{
			expFactor = 2.25f;
		}
		else if (entryExposed)
		{
			expFactor = 4.5f;
		}

		float tapWeight = 3.8f;
		if (pTap->IsLadder())
		{
			tapWeight = 10.0f;
		}
		else if (pTap->IsJumpDown())
		{
			tapWeight = Abs(pTap->GetTraversalDeltaLs().Y()) > 2.0f ? 2.0f : 1.4f;
		}
		else if (pTap->IsJumpAcross())
		{
			tapWeight = 2.0f;
		}
		else if (pTap->IsJumpUp())
		{
			tapWeight = 2.8f;
		}

		return Max(tapWeight, tapWeight * dist * expFactor);
	}

	const NavMesh* pMesh = pathNode.GetNavMeshHandle().ToNavMesh();

	if (!pMesh || !pMesh->m_exposure)
	{
		return dist;
	}

	F32 exposedLength, avoidLength;
	pMesh->m_exposure->IntegrateExpAvd(ExposureMapType::kStealthGrassAlwaysUnexposed, DC::kExposureSourceEnemyNpcs, fromPosPs, toPosPs, exposedLength, avoidLength);

	return dist + 2.6f * exposedLength + 4.5f * avoidLength;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static F32 CostFuncSneakReducedExposure(const Nav::PathFindContext&,
										const NavPathNodeProxy& pathNode,
										const NavPathNode::Link&,
										Point_arg fromPosPs,
										Point_arg toPosPs,
										bool*)
{
	NAV_ASSERT(g_exposureMapMgr.GetExposureMapLock()->IsLockedForRead());
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	F32 dist = Dist(fromPosPs, toPosPs);

	if (pathNode.IsActionPackNode())
	{
		NAV_ASSERT(pathNode.GetActionPack()->GetType() == ActionPack::kTraversalActionPack);

		const TraversalActionPack* pTap = static_cast<const TraversalActionPack*>(pathNode.GetActionPack());
		const NavLocation& entryNavLoc = pTap->GetSourceNavLocation();
		const NavLocation& exitNavLoc = pTap->GetDestNavLocation();

		// for tAPs, allow stealth grass to count as exposed
		const bool entryExposed = g_exposureMapMgr.GetNavLocationExposed(entryNavLoc, ExposureMapType::kStealthGrassNormal, DC::kExposureSourceEnemyNpcs);
		const bool exitExposed = g_exposureMapMgr.GetNavLocationExposed(exitNavLoc, ExposureMapType::kStealthGrassNormal, DC::kExposureSourceEnemyNpcs);

		// weight the tAP based on how exposed its entry and exit are
		float expFactor = 1.0f;
		if (exitExposed)
		{
			expFactor = 2.25f;
		}
		else if (entryExposed)
		{
			expFactor = 4.5f;
		}

		float tapWeight = 3.5f;
		if (pTap->IsLadder())
		{
			tapWeight = 10.0f;
		}
		else if (pTap->IsJumpDown())
		{
			tapWeight = Abs(pTap->GetTraversalDeltaLs().Y()) > 2.0f ? 2.0f : 1.4f;
		}
		else if (pTap->IsJumpAcross())
		{
			tapWeight = 2.0f;
		}
		else if (pTap->IsJumpUp())
		{
			tapWeight = 2.9f;
		}
		return Max(tapWeight, tapWeight * dist * expFactor);
	}

	const NavMesh* pMesh = pathNode.GetNavMeshHandle().ToNavMesh();

	if (!pMesh || !pMesh->m_exposure)
	{
		return dist;
	}

	F32 exposedLength, avoidLength;
	pMesh->m_exposure->IntegrateExpAvd(ExposureMapType::kStealthGrassAlwaysUnexposed, DC::kExposureSourceEnemyNpcs, fromPosPs, toPosPs, exposedLength, avoidLength);

	return dist + 1.7f * exposedLength + 4.5f * avoidLength;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static F32 CostFuncBuddyCombat(const Nav::PathFindContext&,
							   const NavPathNodeProxy& pathNode,
							   const NavPathNode::Link&,
							   Point_arg fromPosPs,
							   Point_arg toPosPs,
							   bool*)
{
	NAV_ASSERT(g_exposureMapMgr.GetExposureMapLock()->IsLockedForRead());
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	F32 dist = Dist(fromPosPs, toPosPs);

	if (pathNode.IsActionPackNode())
	{
		NAV_ASSERT(pathNode.GetActionPack()->GetType() == ActionPack::kTraversalActionPack);

		const TraversalActionPack* pTap = static_cast<const TraversalActionPack*>(pathNode.GetActionPack());
		const NavLocation& entryNavLoc = pTap->GetSourceNavLocation();
		const NavLocation& exitNavLoc = pTap->GetDestNavLocation();

		// for tAPs ignore stealth grass
		const bool entryExposed = g_exposureMapMgr.GetNavLocationExposed(entryNavLoc, ExposureMapType::kStealthGrassNormal, DC::kExposureSourceEnemyNpcs);
		const bool exitExposed = g_exposureMapMgr.GetNavLocationExposed(exitNavLoc, ExposureMapType::kStealthGrassNormal, DC::kExposureSourceEnemyNpcs);

		// weight the tAP based on how exposed its entry and exit are
		float expFactor = 1.0f;
		if (entryExposed && exitExposed)
		{
			expFactor = 1.3f;
		}
		else if (exitExposed)
		{
			expFactor = 1.25f;
		}
		else if (entryExposed)
		{
			expFactor = 1.2f;
		}

		float tapWeight = 3.5f;
		if (pTap->IsLadder())
		{
			tapWeight = 10.0f;
		}
		else if (pTap->IsJumpDown())
		{
			tapWeight = Abs(pTap->GetTraversalDeltaLs().Y()) > 2.0f ? 2.0f : 1.5f;
		}
		else if (pTap->IsJumpAcross())
		{
			tapWeight = 2.0f;
		}
		else if (pTap->IsJumpUp())
		{
			tapWeight = 3.0f;
		}
		return Max(tapWeight, tapWeight * dist * expFactor);
	}

	const NavMesh* pMesh = pathNode.GetNavMeshHandle().ToNavMesh();

	if (!pMesh || !pMesh->m_exposure)
	{
		return dist;
	}

	F32 thtLength, avdLength;
	pMesh->m_exposure->IntegrateThtAvd(fromPosPs, toPosPs, thtLength, avdLength);
	const F32 exposedLength = pMesh->m_exposure->IntegrateExp(ExposureMapType::kStealthGrassAlwaysUnexposed, DC::kExposureSourceEnemyNpcs, fromPosPs, toPosPs);

	return dist + 1.1f * exposedLength + 0.3f * thtLength + 5.5f * avdLength;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static F32 CostFuncBuddyFollow(const Nav::PathFindContext&,
								const NavPathNodeProxy& pathNode,
								const NavPathNode::Link&,
								Point_arg fromPosPs,
								Point_arg toPosPs,
								bool*)
{
	NAV_ASSERT(g_exposureMapMgr.GetExposureMapLock()->IsLockedForRead());
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	const F32 dist = Dist(fromPosPs, toPosPs);

	if (pathNode.IsActionPackNode())
	{
		NAV_ASSERT(pathNode.GetActionPack()->GetType() == ActionPack::kTraversalActionPack);

		const TraversalActionPack* pTap = static_cast<const TraversalActionPack*>(pathNode.GetActionPack());

		float tapWeight = 3.5f;
		if (pTap->IsLadder())
		{
			tapWeight = 10.0f;
		}
		else if (pTap->IsJumpDown())
		{
			tapWeight = Abs(pTap->GetTraversalDeltaLs().Y()) > 2.0f ? 2.6f : 2.0f;
		}
		else if (pTap->IsJumpAcross())
		{
			tapWeight = 2.0f;
		}
		else if (pTap->IsJumpUp())
		{
			tapWeight = 5.0f;
		}
		return Max(tapWeight, tapWeight * dist);
	}

	const NavMesh* pMesh = pathNode.GetNavMeshHandle().ToNavMesh();

	if (!pMesh || !pMesh->m_exposure)
	{
		return dist;
	}

	const F32 avdLength = pMesh->m_exposure->IntegrateAvd(fromPosPs, toPosPs);

	return dist + 5.5f * avdLength;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static F32 CostFuncBuddyLead(const Nav::PathFindContext& context,
							 const NavPathNodeProxy& pathNode,
							 const NavPathNode::Link&,
							 Point_arg fromPosPs,
							 Point_arg toPosPs,
							 bool*)
{
	NAV_ASSERT(g_exposureMapMgr.GetExposureMapLock()->IsLockedForRead());
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	const F32 dist = Dist(fromPosPs, toPosPs);

	if (pathNode.IsActionPackNode())
	{
		NAV_ASSERT(pathNode.GetActionPack()->GetType() == ActionPack::kTraversalActionPack);

		const TraversalActionPack* pTap = static_cast<const TraversalActionPack*>(pathNode.GetActionPack());

		const float tapBaseCost = pTap->GetMgrId() == context.m_ownerReservedApMgrId ? pTap->ComputeBasePathCostForReserver() : pTap->ComputeBasePathCost();

		float tapWeight = 2.0f;
		if (pTap->IsJumpDown() && !context.m_isHorse)
		{
			tapWeight = Abs(pTap->GetTraversalDeltaLs().Y()) > 2.0f ? 2.0f : 1.3f;
		}
		return tapBaseCost + Max(tapWeight, tapWeight * dist);
	}

	const NavMesh* pMesh = pathNode.GetNavMeshHandle().ToNavMesh();

	if (!pMesh || !pMesh->m_exposure)
	{
		return dist;
	}

	const F32 avdLength = pMesh->m_exposure->IntegrateAvd(fromPosPs, toPosPs);

	return dist + 5.5f * avdLength;
}

/*static*/ bool ExposureMapManager::ComputeBoundingSphereForDeadBodyAvoidance(Sphere& ret, const CompositeBody* const pCompBody)
{
	float yAccum = 0.0f;
	int count = 0;

	const int numBodies = pCompBody->GetNumBodies();
	for (int i = 0; i < numBodies; ++i)
	{
		const RigidBody* const pBody = pCompBody->GetBody(i);
		PHYSICS_ASSERT(pBody);

		//if (pBody->GetMotionType() == kRigidBodyMotionTypeNonPhysical)
		//	continue;

		if (pBody->IsInvalid())
			continue;

		Sphere boundingSphere = pBody->GetBoundingSphere();

		Vec4 boundingVec4 = boundingSphere.GetVec4();
		yAccum += boundingVec4.Y();
		boundingVec4.SetY(0.0f);
		boundingSphere = Sphere(boundingVec4);

		if (count++ == 0)
			ret = boundingSphere;
		else
			ret.CombineSpheres(boundingSphere);
	}

	Vec4 retVec4 = ret.GetVec4();
	retVec4.SetY(yAccum / count);
	ret = Sphere(retVec4);

	return count;
}

void ExposureMapManager::UpdateAvoidSpheres(const Sphere* spheres, int numSpheres)
{
	NAV_ASSERT(numSpheres <= kExposureMapMaxAvoidSpheres);

	memcpy(m_state.m_avoidSpheres, spheres, numSpheres * sizeof(Sphere));
	m_state.m_numAvoidSpheres = numSpheres;
}

void ExposureMapManager::UpdateDynamicHeightMaps(const DynamicHeightMap* maps, int numMaps)
{
	NAV_ASSERT(numMaps <= kExposureMapMaxDynamicHeightmaps);

	memcpy(m_state.m_dynamicHeightMaps, maps, numMaps * sizeof(DynamicHeightMap));
	m_state.m_numDynamicHeightMaps = numMaps;

	if (FALSE_IN_FINAL_BUILD(g_navMeshDrawFilter.m_drawHeightMaps || g_navMeshDrawFilter.m_drawDynamicHeightMaps))
	{
		for (int iDyn = 0; iDyn < numMaps; ++iDyn)
		{
			maps[iDyn].DebugDraw();
		}
	}
}

///-----------------------------------------------------------------------------------------------
static void ConstructAuxMaps(ExposureMapState& state)
{
	PROFILE(AI, ExposureMapConstructAuxMaps);

	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());
	NAV_ASSERT(state.m_exposureMapLock.IsLockedForWrite());

	const NavMeshMgr* const pMgr = EngineComponents::GetNavMeshMgr();

	// get all navmeshes
	const NavMesh* results[LevelSpec::kMaxNavMeshesPerLevel + 1];
	U32 numResults = 0;
	{
		NavMeshHandle resultHandles[LevelSpec::kMaxNavMeshesPerLevel + 1];
		const U32 numResultHandles = pMgr->GetNavMeshList(resultHandles, ARRAY_COUNT(resultHandles));
		NAV_ASSERTF(numResultHandles < ARRAY_COUNT(resultHandles), ("Increase size of resultHandles array."));
		for (int i = 0; i < numResultHandles; ++i)
		{
			if (const NavMesh* const pNavMesh = resultHandles[i].ToNavMesh())
			{
				if (pNavMesh->m_exposure && pNavMesh->GetHeightMap())
					results[numResults++] = pNavMesh;
			}
		}
	}

	//// clearing meshes modified last frame
	// note that meshes may have changed since last frame, but at worst that means we clear an already-clear mesh.
	// the dangerous case would be that we fail to clear a dirty mesh, but that won't happen, because once given an index,
	// a navmesh will keep it until it's logged out. so the only failure mode that arises from having stale indices is
	// accidentally clearing a newly logged-in mesh that took the place of a logged-out, old, dirty mesh. which is fine.

	// avoid map
	{
		for (const U32 i : state.m_avoidBits)
		{
			const NavMesh* pMesh = pMgr->GetNavMeshAtIndex(i);
			if (!pMesh)
				continue;

			AiExposureData* pExposure = const_cast<AiExposureData*>(pMesh->m_exposure);
			if (!pExposure)
				continue;

			pExposure->m_anyAvoid = false;

			U64* __restrict const pAvoid = pExposure->GetAvoidMap();
			NAV_ASSERT(pAvoid);

			// clear map
			pExposure->ClearMap(pAvoid);
		}
		state.m_avoidBits.ClearAllBits();
	}

	//// create avoid map

	// for each sphere
	for (I32 iSphere = 0; iSphere < state.m_numAvoidSpheres; ++iSphere)
	{
		Sphere sphere = state.m_avoidSpheres[iSphere];
		const Aabb sphereAabb = sphere.GetAabb();

		// for each map
		for (int iMesh = 0; iMesh < numResults; ++iMesh)
		{
			const NavMesh* pMesh = results[iMesh];
			AiExposureData* pExposure = const_cast<AiExposureData*>(pMesh->m_exposure);
			const NavMeshHeightMap* pMap = pMesh->GetHeightMap();
			U64* __restrict const pAvoid = pExposure->GetAvoidMap();

			// only if Aabbs overlap
			const Vector localToParent = AsVector(pMesh->GetOriginPs().GetPosition());
			{
				Aabb mapAabbLs = pMap->GetAabbLs();
				Aabb mapAabbWs;
				mapAabbWs.IncludePoint(localToParent + mapAabbLs.m_min);
				mapAabbWs.IncludePoint(localToParent + mapAabbLs.m_max);
				//g_prim.Draw(DebugBox(mapAabbWs, kColorRed), kPrimDuration1FramePauseable);
				if (!mapAabbWs.IntersectAabb(sphereAabb))
					continue;
			}

			const Point mapOriginWs = localToParent + pMap->m_bboxMinLs;
			const Vector startWs = (sphereAabb.m_min - mapOriginWs) * kInvHeightMapGridSpacing;
			const Vector endWs = (sphereAabb.m_max - mapOriginWs) * kInvHeightMapGridSpacing;

			const I32 startX = MinMax(FastCvtToIntFloor(startWs.X()), 0, (I32)pMap->m_sizeX - 1);
			const I32 startZ = MinMax(FastCvtToIntFloor(startWs.Z()), 0, (I32)pMap->m_sizeZ - 1);

			const I32 endX = MinMax(FastCvtToIntFloor(endWs.X()), 0, (I32)pMap->m_sizeX - 1);
			const I32 endZ = MinMax(FastCvtToIntFloor(endWs.Z()), 0, (I32)pMap->m_sizeZ - 1);

			const Point sphereCenter = sphere.GetCenter();
			const Vector ofs = sphereCenter - mapOriginWs;
			float rad2 = sphere.GetRadius();
			rad2 *= rad2;

			bool anyAvoid = false;

			// for all potential cells
			for (int z = startZ; z <= endZ; ++z)
			{
				for (int x = startX; x <= endX; ++x)
				{
					const I32 y = (I32)pMap->m_data[z * pMap->m_sizeX + x];
					const Vector cellCenter((x + 0.5f) * kHeightMapGridSpacing, (y + 0.5f) * kHeightMapHeightSpacing, (z + 0.5f) * kHeightMapGridSpacing);
					Vector delta = cellCenter - ofs;
					delta *= Vector(1.0f, 2.0f, 1.0f);
					if (LengthSqr(delta) < rad2)
					{
						pExposure->SetBit(pAvoid, x, z);
						anyAvoid = true;
					}
				}
			}

			// we've modified this mesh. mark it for cleanup next frame.
			if (anyAvoid)
			{
				pExposure->m_anyAvoid = true;
				state.m_avoidBits.SetBit(pMesh->GetManagerId().m_navMeshIndex);
			}
		}
	}


	// threat map
	{
		for (const U32 i : state.m_threatBits)
		{
			const NavMesh* pMesh = pMgr->GetNavMeshAtIndex(i);
			if (!pMesh)
				continue;

			AiExposureData* pExposure = const_cast<AiExposureData*>(pMesh->m_exposure);
			if (!pExposure)
				continue;

			U64* __restrict const pThreat = pExposure->GetThreatMap();
			NAV_ASSERT(pThreat);

			// clear map
			pExposure->ClearMap(pThreat);
		}
		state.m_threatBits.ClearAllBits();
	}

	//// create threat map
	bool anyBuddies = false;
	{
		CharacterManager& mgr = CharacterManager::GetManager();
		AtomicLockJanitorRead_Jls charLock(mgr.GetLock(), FILE_LINE_FUNC);

		const int numChars = mgr.GetNumCharacters();
		for (int iIter = 0; iIter < numChars; ++iIter)
		{
			CharacterHandle hIter = mgr.GetCharacterHandle(iIter);
			const Character* pIter = hIter.ToProcess();
			if (!pIter)
				continue;
			if (pIter->IsDead())
				continue;
			if (!pIter->IsBuddyNpc())
				continue;
			anyBuddies = true;
			break;
		}
	}

	if (anyBuddies)
	{
		for (I32 iIter = 0; iIter < 2; ++iIter)
		{
			// for each observer
			const bool isFuture = iIter;

			const ExposureSourceState& srcState = state.m_srcState[isFuture ? DC::kExposureSourceFutureEnemyNpcs : DC::kExposureSourceEnemyNpcs];

			for (I32 iObs = 0; iObs < srcState.m_numObs; ++iObs)
			{
				const ExposureMapObserver& obs = srcState.m_obs[iObs];

				if (!obs.m_tht.IsEnabled())
					continue;

				const Vector forward = obs.m_thtLocator.TransformVector(kUnitZAxis);
				const Point obsWs = obs.m_thtLocator.GetPosition();

				const float obsMinY = obsWs.Y() - 2.9f;
				const float obsMaxY = obsWs.Y() + 2.9f;

				// for each map
				for (int iMesh = 0; iMesh < numResults; ++iMesh)
				{
					const NavMesh* pMesh = results[iMesh];
					AiExposureData* pExposure = const_cast<AiExposureData*>(pMesh->m_exposure);
					const NavMeshHeightMap* pMap = pMesh->GetHeightMap();
					U64* __restrict const pThreat = pExposure->GetThreatMap();

					const Vector localToParent = AsVector(pMesh->GetOriginPs().GetPosition());

					const Point mapOriginWs = localToParent + pMap->m_bboxMinLs;

					const U64* __restrict const pCurMap = pExposure->GetConstExprMap<ExposureMapType::kStealthGrassNormal, DC::kExposureSourceEnemyNpcs>();
					const U64* __restrict const pFutMap = pExposure->GetConstExprMap<ExposureMapType::kStealthGrassNormal, DC::kExposureSourceFutureEnemyNpcs>();
					const U64* __restrict const pSteMap = pMap->m_isStealth;

					Aabb mapAabbLs = pMap->GetAabbLs();
					Aabb mapAabbWs;

					mapAabbWs.IncludePoint(localToParent + mapAabbLs.m_min);
					mapAabbWs.IncludePoint(localToParent + mapAabbLs.m_max);

					if (obsMaxY < mapAabbWs.m_min.Y() || obsMinY > mapAabbWs.m_max.Y())
						continue;

					const Aabb& aabb = IntersectModelAabb(obs.m_thtBoundsWs, mapAabbWs);
					//g_prim.Draw(DebugBox(aabb, kColorRed), kPrimDuration1FramePauseable);
					if (!aabb.IsValid())
						continue;

					// we're (probably) gonna modify this mesh. mark it for cleanup next frame.
					state.m_threatBits.SetBit(pMesh->GetManagerId().m_navMeshIndex);

					const Vector startWs = (aabb.m_min - mapOriginWs) * kInvHeightMapGridSpacing;
					const Vector endWs = (aabb.m_max - mapOriginWs) * kInvHeightMapGridSpacing;

					const I32 startX = MinMax(FastCvtToIntFloor(startWs.X()), 0, (I32)pMap->m_sizeX - 1);
					const I32 startZ = MinMax(FastCvtToIntFloor(startWs.Z()), 0, (I32)pMap->m_sizeZ - 1);

					const I32 endX = MinMax(FastCvtToIntCeil(endWs.X()), 0, (I32)pMap->m_sizeX - 1);
					const I32 endZ = MinMax(FastCvtToIntCeil(endWs.Z()), 0, (I32)pMap->m_sizeZ - 1);

					const Vector ofs = obsWs - mapOriginWs;

					// for all potential cells
					const U32 pitch = pMap->m_bitmapPitch;
					for (int z = startZ; z <= endZ; ++z)
					{
						for (int x = startX; x <= endX; ++x)
						{
							const U32 index = z * pitch + x;
							if (isFuture ? ((pFutMap[index >> 6] >> (index & 63)) & 1) : ((pCurMap[index >> 6] >> (index & 63)) & 1))
							{
								const I32 y = (I32)pMap->m_data[z * pMap->m_sizeX + x];

								const Vector cellCenter((x + 0.5f) * kHeightMapGridSpacing, y * kHeightMapHeightSpacing, (z + 0.5f) * kHeightMapGridSpacing);
								const Vector delta = cellCenter - ofs;

								const float radSqr = LengthXzSqr(delta);
								const float cosTheta = DotXz(forward, delta) * _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(radSqr)));

								const bool isStealth = (pSteMap[index >> 6] >> (index & 63)) & 1;

								// polar curve
								float kC = obs.m_tht.m_c;
								float kD = obs.m_tht.m_d;
								if (isStealth)
								{
									kC = Min(kC, 1.65f);
									kD = Min(kD, 0.64f);
								}

								const float term = 1.0f - kD * cosTheta;
								const float radMax = kC * _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(term))) + kHeightMapGridSpacing;

								if (radSqr <= radMax * radMax && Abs(delta).Y() < 2.7f)
								{
									pExposure->SetBit(pThreat, x, z);
								}
							}
						}
					}
				}
			}
		}
	}
}

///-----------------------------------------------------------------------------------------------
static void GatherWorkload(ExposureMapState& state)
{
	PROFILE(AI, ExposureMapGatherWorkload);

	// the compute context (if it exists) must be waited on here,
	// and also prior to the end of the GPU frame
	// (whichever comes first)
	g_exposureMapMgr.GatherComputeContext();

	// take and hold lock while updating any maps that were completed this frame, plus the aux maps
	AtomicLockJanitorWrite_Jls exposureMapPointerLock(&state.m_exposureMapLock, FILE_LINE_FUNC);

	for (I32 iSrc = 0; iSrc < DC::kExposureSourceCount; ++iSrc)
	{
		ExposureSourceState& srcState = state.m_srcState[iSrc];

		bool invalid = false;
		for (I32 iMesh = 0; iMesh < srcState.m_numMeshes; ++iMesh)
		{
			if (!srcState.m_hMeshes[iMesh].IsValid())
			{
				invalid = true;
				break;
			}
		}

		if (invalid)
		{
			// invalid navmesh! discard all progress into this srcState, and restart it.
			srcState.m_curObs = 0;
			continue;
		}

		// did we complete this map source? if so, we can finalize and then present our new, updated exposure map to the world
		const bool srcComplete = srcState.m_curObs >= srcState.m_numObs;
		if (!srcComplete)
			continue;

		// the Shadowcaster algorithm does not mark the cell an observer is directly standing on as exposed.
		// manually do so here, if it's valid.
		for (I32 iObs = 0; iObs < srcState.m_numObs; ++iObs)
		{
			const NavMesh* pMesh = srcState.m_obs[iObs].m_hNavPoly.ToNavMesh();
			if (!pMesh)
				continue;

			const NavMeshHeightMap* pMap = pMesh->GetHeightMap();
			const U32 xIdx = srcState.m_obsX[iObs];
			const U32 zIdx = srcState.m_obsZ[iObs];
			if (xIdx < pMap->m_sizeX && zIdx < pMap->m_sizeZ)
			{
				pMesh->m_exposure->SetBit(ExposureMapType::kScratch, iSrc, xIdx, zIdx);
			}
		}

		// finalize all the meshes of this srcState

		for (I32 iMesh = 0; iMesh < srcState.m_numMeshes; ++iMesh)
		{
			const NavMesh* pMesh = srcState.m_hMeshes[iMesh].ToNavMesh();
			NAV_ASSERT(pMesh);

			const NavMeshHeightMap* pMap = pMesh->GetHeightMap();
			NAV_ASSERT(pMap);

			AiExposureData* pData = pMesh->m_exposure;
			NAV_ASSERT(pData);

			// copy scratch map into exposed map, masking off cells that are off navmesh
			{
				//PROFILE(AI, ExposureMapCopyScratch);
				const __m128i* __restrict const pA = (const __m128i* __restrict const)pData->GetMap(ExposureMapType::kScratch, iSrc);
				const __m128i* __restrict const pB = (const __m128i* __restrict const)pMap->m_onNavMesh;
				__m128i* __restrict const pDst = (__m128i* __restrict const)pData->GetMap(ExposureMapType::kStealthGrassNormal, iSrc);

				const U32 iEnd = pData->m_blocks >> 1;
				__m128 vAny = _mm_setzero_si128();
				for (int i = 0; i < iEnd; ++i)
				{
					const __m128 v = _mm_and_si128(_mm_loadu_si128(pA + i), _mm_loadu_si128(pB + i));
					vAny = _mm_or_si128(vAny, v);
					_mm_storeu_si128(pDst + i, v);
				}

				const bool bAny = U32(_mm_movemask_epi8(_mm_cmpeq_epi32(vAny, _mm_setzero_si128()))) != 0xffff;
				pData->m_any[iSrc] = bAny;
			}

			// make sure the vector math ain't broke
//#if defined(KOMAR) && !defined(FINAL_BUILD)
//			{
//				const U64* __restrict const pSrc = pData->GetMap(ExposureMapType::kStealthGrassNormal, iSrc);
//
//				bool bAny = false;
//				for (int x = 0; x < pData->m_sizeX; ++x)
//				{
//					for (int z = 0; z < pData->m_sizeZ; ++z)
//					{
//						if (pData->IsExposed(pSrc, x, z))
//						{
//							bAny = true;
//							goto label_loop_breakout;
//						}
//					}
//				}
//			label_loop_breakout:;
//				ALWAYS_ASSERT(bAny == pData->m_any[iSrc]);
//			}
//#endif

			// copy exposed map into occluded map, masking off cells that are in stealth grass
			{
				//PROFILE(AI, ExposureMapCopyOccluded);
				const __m128i* __restrict const pA = (const __m128i* __restrict const)pMap->m_isStealth;
				const __m128i* __restrict const pB = (const __m128i* __restrict const)pData->GetMap(ExposureMapType::kStealthGrassNormal, iSrc);
				__m128i* __restrict const pDst = (__m128i* __restrict const)pData->GetMap(ExposureMapType::kStealthGrassAlwaysUnexposed, iSrc);

				const U32 iEnd = pData->m_blocks >> 1;
				for (int i = 0; i < iEnd; ++i)
				{
					_mm_storeu_si128(pDst + i, _mm_andnot_si128(_mm_loadu_si128(pA + i), _mm_loadu_si128(pB + i)));
				}
			}
		}

		// restart this srcState
		srcState.m_curObs = 0;
	}

	if (!g_ndConfig.m_pNetInfo->IsNetActive())
		ConstructAuxMaps(state);
}

///-----------------------------------------------------------------------------------------------
static void UpdateTypeStateMeshBits(NavMeshMgr::NavMeshBits& meshBits, ExposureSourceState& srcState, I32 iSrc)
{
	NAV_ASSERT(iSrc >= 0);
	NAV_ASSERT(iSrc < DC::kExposureSourceCount);

	const NavMeshMgr& mgr = *EngineComponents::GetNavMeshMgr();

	// clear out the exposure maps of navmeshes we won't be updating this time around

	// get navmeshes present last time and NOT present this time (meshBits)
	NavMeshMgr::NavMeshBits onlyOldBits;
	NavMeshMgr::NavMeshBits::BitwiseAndComp(&onlyOldBits, srcState.m_meshBits, meshBits);
	for (const U32 i : onlyOldBits)
	{
		const NavMesh* pMesh = mgr.GetNavMeshAtIndex(i);
		if (pMesh)
		{
			pMesh->m_exposure->ClearMap(ExposureMapType::kStealthGrassNormal, iSrc);
			pMesh->m_exposure->ClearMap(ExposureMapType::kStealthGrassAlwaysUnexposed, iSrc);
			pMesh->m_exposure->m_any[iSrc] = false;
		}
	}

	// update the meshbits for this type
	srcState.m_meshBits = meshBits;
}

///-----------------------------------------------------------------------------------------------
static void InitTypeState(NavMeshMgr::NavMeshBits& meshBits, ExposureSourceState& srcState, I32 iSrc, const DynamicHeightMap* dynMaps, I32 numDynMaps)
{
	PROFILE(AI, ExposureMapPrepareNewWorkload);

	srcState.m_numMeshes = 0;
	srcState.m_curObs = 0;
	srcState.m_numObs = 0;

	NAV_ASSERTF(srcState.m_pendingParams.m_numObservers <= kExposureMapMaxObservers, ("Too many exposure map observers!"));

	// compute heightmap-space position (cell indices) of each observer
	// and drag them, kicking and screaming, away from the edges of their navmesh
	{
		PROFILE(AI, FindObservers);

		ScopedTempAllocator jj(FILE_LINE_FUNC);

		for (I32 iObs = 0; iObs < srcState.m_pendingParams.m_numObservers; ++iObs)
		{
			ExposureMapObserver obs = srcState.m_pendingParams.m_observers[iObs];

			if (!IsFinite(obs.m_locator))
			{
				NAV_ASSERT(false);
				continue;
			}

			NAV_ASSERT(obs.m_locator.Rot().Get(0) == 0.0f);
			NAV_ASSERT(obs.m_locator.Rot().Get(2) == 0.0f);

			const NavPoly* pObsNavPoly = obs.m_hNavPoly.ToNavPoly();
			if (!pObsNavPoly)
				continue;

			const NavMesh* pObsNavMesh = pObsNavPoly->GetNavMesh();
			NAV_ASSERT(pObsNavMesh);

			const NavMeshHeightMap* pObsHeightMap = pObsNavMesh->GetHeightMap();
			if (!pObsHeightMap)
				continue;

			Vector localToParent = AsVector(pObsNavMesh->GetOriginPs().GetPosition());

			Point obsPosWs = obs.m_locator.Pos();
			{
				NavMesh::FindPointParams params;
				params.m_pStartPoly = pObsNavPoly;
				params.m_crossLinks = true;
				params.m_depenRadius = params.m_searchRadius = 0.99f;
				params.m_dynamicProbe = false;
				params.m_obeyedBlockers.ClearAllBits();
				params.m_obeyedStaticBlockers = Nav::kStaticBlockageMaskNone;

				NavMeshDepenetrator2 probe;
				probe.Init(obsPosWs - localToParent, pObsNavMesh, params);
				probe.Execute(pObsNavMesh, pObsNavPoly, nullptr);

				const NavPoly* pNewPoly = probe.GetResolvedPoly();

				if (pNewPoly)
				{
					const NavMesh* pNewMesh = pNewPoly->GetNavMesh();
					if (const NavMeshHeightMap* pNewHeightMap = pNewMesh->GetHeightMap())
					{
						//if (iSrc == DC::kExposureSourceEnemyNpcs)
						//{
						//	char buf[128];
						//	sprintf(buf, "%s | %d", pObsNavMesh->GetName(), pObsNavPoly->GetId());
						//	g_prim.Draw(DebugString(obsPosWs, buf, 1, kColorRed));
						//}

						// successfully adjusted by depenetration probe, potentially onto new mesh, poly, heightmap,
						// localToParent offset, and point. Update all 5 quantities.

						// Ls of START mesh
						const Point groundPosWs = localToParent + probe.GetResolvedPosLs();

						pObsNavMesh = pNewMesh;
						pObsNavPoly = pNewPoly;
						pObsHeightMap = pNewHeightMap;
						localToParent = AsVector(pObsNavMesh->GetOriginPs().GetPosition());

						const Point oldPosWs = obsPosWs;

						// preserve old Y
						obsPosWs = PointFromXzAndY(groundPosWs, oldPosWs);
						obs.m_locator.SetPos(obsPosWs);

						//if (iSrc == DC::kExposureSourceEnemyNpcs)
						//{
						//	g_prim.Draw(DebugSphere(oldPosWs + obs.m_eyeOffset, 0.07f, kColorRed));
						//	g_prim.Draw(DebugSphere(obsPosWs + obs.m_eyeOffset, 0.1f, kColorGreen));
						//	g_prim.Draw(DebugArrow(oldPosWs + obs.m_eyeOffset, obsPosWs + obs.m_eyeOffset, kColorGreen));
						//	{
						//		char buf[128];
						//		sprintf(buf, "%s | %d", pObsNavMesh->GetName(), pObsNavPoly->GetId());
						//		g_prim.Draw(DebugString(obsPosWs - Vector(0.0f, 0.2f, 0.0f), buf, 1, kColorRed));
						//	}
						//}
					}
				}
			}

			const Point obsOriginWs = localToParent + pObsHeightMap->m_bboxMinLs;
			const Vector idealCoords = (obsPosWs - obsOriginWs) * kInvHeightMapGridSpacing;

			const I32 obsX = FastCvtToIntFloor(idealCoords.X());
			const I32 obsZ = FastCvtToIntFloor(idealCoords.Z());


			srcState.m_obsX[srcState.m_numObs] = obsX;
			srcState.m_obsZ[srcState.m_numObs] = obsZ;

			obs.ComputeBounds();
			//DebugDrawModel(obs.m_exp, obs.m_locator);

			srcState.m_obs[srcState.m_numObs] = obs;
			srcState.m_obs[srcState.m_numObs].m_hNavPoly = pObsNavPoly;

			if (++srcState.m_numObs >= kExposureMapMaxObservers)
				break;
		}
	}

	////////////////////////////////////////////////////////////////////
	// debug option to add some more observers to the player map that are identical to the player, just shifted
	// in space, for easy debugging
	if (FALSE_IN_FINAL_BUILD(iSrc == DC::kExposureSourcePlayer && srcState.m_numObs == 1))
	{
#ifdef KOMAR
		CONST_EXPR I32 dummyObserversToAdd = 0;
#else
		CONST_EXPR I32 dummyObserversToAdd = 0;
#endif
		for (I32 foo = 1; foo <= dummyObserversToAdd; ++foo)
		{
			ExposureMapObserver obs2 = srcState.m_obs[0];

			const Point obsPosWs = obs2.m_locator.Pos();
			const Point obsOriginWs = obs2.m_hNavPoly.ToNavMesh()->LocalToWorld(obs2.m_hNavPoly.ToNavMesh()->GetHeightMap()->m_bboxMinLs);
			const Vector idealCoords = (obsPosWs - obsOriginWs) * kInvHeightMapGridSpacing;

			I32 obsX = FastCvtToIntFloor(idealCoords.X());
			I32 obsZ = FastCvtToIntFloor(idealCoords.Z());

			srcState.m_obsX[srcState.m_numObs] = obsX + 10 * foo;
			srcState.m_obsZ[srcState.m_numObs] = obsZ - 2 * foo;

			obs2.m_locator.SetPos(obs2.m_locator.Pos() + Vector(5.0f * foo, 0.0f, -1.0f * foo));

			obs2.ComputeBounds();
			srcState.m_obs[srcState.m_numObs] = obs2;

			++srcState.m_numObs;
		}
	}
	////////////////////////////////////////////////////////////////////

	// gather navmeshes, in the following order, up to the max allowed:
	// - add the navmesh of each observer
	// - add all navmeshes whose AABBs overlap with any observer's vision radius AABB

	{
		PROFILE(AI, GatherHeightmaps);

		srcState.m_numMeshes = 0;

		// add the navmesh of each observer
		{
			PROFILE(AI, AddObsNavMeshes);

			for (I32 iObs = 0; iObs < srcState.m_numObs; ++iObs)
			{
				if (srcState.m_numMeshes >= kExposureMapMaxHeightMaps)
					break;

				const NavMesh* pMesh = srcState.m_obs[iObs].m_hNavPoly.ToNavMesh();

				if (!pMesh || !pMesh->m_exposure)
					continue;

				const U16 meshIdx = pMesh->GetManagerId().m_navMeshIndex;

				// don't add if we already have this one
				if (meshBits.IsBitSet(meshIdx))
					continue;

				meshBits.SetBit(meshIdx);
				srcState.m_hMeshes[srcState.m_numMeshes++] = pMesh;
			}
		}

		// get all navmeshes
		NavMeshHandle results[LevelSpec::kMaxNavMeshesPerLevel + 1];
		const U32 numResults = EngineComponents::GetNavMeshMgr()->GetNavMeshList(results, ARRAY_COUNT(results));
		NAV_ASSERTF(numResults < ARRAY_COUNT(results), ("Increase size of results array."));

		{
			PROFILE(AI, AddInAabb);

			// add navmeshes inside the bounding box of any observer
			for (I32 iResult = 0; iResult < numResults; ++iResult)
			{
				if (srcState.m_numMeshes >= kExposureMapMaxHeightMaps)
					break;

				const NavMesh* pMesh = results[iResult].ToNavMesh();

				if (!pMesh || !pMesh->m_exposure)
					continue;

				const U16 meshIdx = pMesh->GetManagerId().m_navMeshIndex;

				// don't add if we already have this one
				if (meshBits.IsBitSet(meshIdx))
					continue;

				Aabb meshAabb = pMesh->GetAabbWs();
				meshAabb.m_min.SetY(0.0f);
				meshAabb.m_max.SetY(0.0f);

				// don't add if we don't overlap with any observer's AABB
				for (I32 iObs = 0; iObs < srcState.m_numObs; ++iObs)
				{
					if (IntersectModelAabb(srcState.m_obs[iObs].m_expBoundsWs, meshAabb).IsValid())
						goto label_init_type_state_found_valid_mesh;
				}
				continue;

				label_init_type_state_found_valid_mesh:;

				meshBits.SetBit(meshIdx);
				srcState.m_hMeshes[srcState.m_numMeshes++] = pMesh;
			}
		}

		srcState.m_numDynamicHeightMaps = 0;

		// gather dynamic heightmaps
		{
			PROFILE(AI, AddDynamic);

			// add dynamic heightmaps inside the bounding box of any observer
			for (int iDyn = 0; iDyn < numDynMaps; ++iDyn)
			{
				if (srcState.m_numMeshes + srcState.m_numDynamicHeightMaps >= kExposureMapMaxHeightMaps)
					break;

				const DynamicHeightMap& dyn = dynMaps[iDyn];

				// don't add if this dynamic map shouldn't apply to this source type
				if ((dyn.m_expSourceMask & (1U << iSrc)) == 0U)
					continue;

				Aabb dynAabb = dyn.GetAabbWs();
				dynAabb.m_min.SetY(0.0f);
				dynAabb.m_max.SetY(0.0f);

				// don't add if we don't overlap with any observer's AABB
				for (I32 iObs = 0; iObs < srcState.m_numObs; ++iObs)
				{
					if (IntersectModelAabb(srcState.m_obs[iObs].m_expBoundsWs, dynAabb).IsValid())
						goto label_init_type_state_found_valid_dyn;
				}
				continue;

				label_init_type_state_found_valid_dyn:;

				srcState.m_dynamicHeightMaps[srcState.m_numDynamicHeightMaps++] = dyn;
			}
		}
	}

	ALWAYS_ASSERT(srcState.m_numMeshes + srcState.m_numDynamicHeightMaps <= kExposureMapMaxHeightMaps);


	PROFILE(AI, StoreAndClearHeightmaps);

	// clear the output bitmaps
	for (I32 iMesh = 0; iMesh < srcState.m_numMeshes; ++iMesh)
	{
		srcState.m_hMeshes[iMesh].ToNavMesh()->m_exposure->ClearMap(ExposureMapType::kScratch, iSrc);
	}

	// for debugging purposes: select navmeshes that have been included
	//if (iSrc == DC::kExposureSourcePlayer)
	//{
	//	EngineComponents::GetNavMeshMgr()->ClearMeshSelection();
	//	for (I32 iMesh = 0; iMesh < srcState.m_numMeshes; ++iMesh)
	//	{
	//		EngineComponents::GetNavMeshMgr()->SetMeshSelected(srcState.m_hMeshes[iMesh].ToNavMesh()->GetNameId(), true);
	//	}
	//}

	//// for debugging purposes: select navmeshes with m_any set
	//{
	//	// get all navmeshes
	//	NavMeshHandle results[LevelSpec::kMaxNavMeshesPerLevel + 1];
	//	const U32 numResults = EngineComponents::GetNavMeshMgr()->GetNavMeshList(results, ARRAY_COUNT(results));
	//	NAV_ASSERTF(numResults < ARRAY_COUNT(results), ("Increase size of results array."));

	//	EngineComponents::GetNavMeshMgr()->ClearMeshSelection();

	//	for (I32 iResult = 0; iResult < numResults; ++iResult)
	//	{
	//		const NavMesh* pMesh = results[iResult].ToNavMesh();

	//		if (!pMesh || !pMesh->m_exposure)
	//			continue;

	//		if (pMesh->m_exposure->m_any[DC::kExposureSourceEnemyNpcs] || pMesh->m_exposure->m_any[DC::kExposureSourceFutureEnemyNpcs])
	//		{
	//			EngineComponents::GetNavMeshMgr()->SetMeshSelected(pMesh->GetNameId(), true);
	//		}
	//	}
	//}
}

bool ExposureMapManager::HeightmapRayQuery(const Point pt0Ws,
										   const Point pt1Ws,
										   Point* pHitWs /* = nullptr */,
										   const bool debugDraw /* = false */)
{
	PROFILE(AI, HeightmapRayQuery);

	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());
	// NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	// for each heightmap:
	//		march from start to end
	//		if we get blocked, return false
	// return true

	NavMesh* meshes[LevelSpec::kMaxNavMeshesPerLevel + 1];
	const U32 numMeshes = EngineComponents::GetNavMeshMgr()->GetNavMeshList_IncludesUnregistered(meshes, ARRAY_COUNT(meshes));
	//NAV_ASSERTF(numMeshes < ARRAY_COUNT(meshes), ("Increase size of results array."));

	for (I32 iMesh = 0; iMesh < numMeshes; ++iMesh)
	{
		const NavMesh* pMesh = meshes[iMesh];
		if (!pMesh->IsRegistered())
			continue;
		const NavMeshHeightMap* pMap = pMesh->GetHeightMap();
		if (!pMap)
			continue;
		const NavMeshHeightMap& map = *pMap;
		const Vector localToParent = AsVector(pMesh->GetOriginPs().GetPosition());
		{
			Aabb aabbLs = map.GetAabbLs();
			Aabb aabbWs;
			aabbWs.IncludePoint(localToParent + aabbLs.m_min);
			aabbWs.IncludePoint(localToParent + aabbLs.m_max);
			//g_prim.Draw(DebugBox(aabbWs, kColorRed), kPrimDuration1FramePauseable);
			Scalar unused1, unused2;
			if (!aabbWs.IntersectSegment(pt0Ws, pt1Ws, unused1, unused2))
				continue;
		}

		const Point pt0Hs = AsPoint(pt0Ws - (localToParent + map.m_bboxMinLs));
		I32 x0 = FastCvtToIntFloor(pt0Hs.X() * kInvHeightMapGridSpacing);
		I32 y0 = FastCvtToIntFloor(pt0Hs.Y() * kInvHeightMapHeightSpacing);
		I32 z0 = FastCvtToIntFloor(pt0Hs.Z() * kInvHeightMapGridSpacing);

		const Point pt1Hs = AsPoint(pt1Ws - (localToParent + map.m_bboxMinLs));
		I32 x1 = FastCvtToIntFloor(pt1Hs.X() * kInvHeightMapGridSpacing);
		I32 y1 = FastCvtToIntFloor(pt1Hs.Y() * kInvHeightMapHeightSpacing);
		I32 z1 = FastCvtToIntFloor(pt1Hs.Z() * kInvHeightMapGridSpacing);

		// march from {x0, y0, z0} to {x1, y1, z1}
		float dx = pt1Hs.X() - pt0Hs.X();
		float dy = pt1Hs.Y() - pt0Hs.Y();
		float dz = pt1Hs.Z() - pt0Hs.Z();

		I32 sx = (x1 > x0) - (x1 < x0);
		I32 sy = (y1 > y0) - (y1 < y0);
		I32 sz = (z1 > z0) - (z1 < z0);

		float cx = x0 != x1 ? ((x0 + (dx > 0)) * kHeightMapGridSpacing - (float)pt0Hs.X()) / dx : NDI_FLT_MAX;
		float cy = y0 != y1 ? ((y0 + (dy > 0)) * kHeightMapHeightSpacing - (float)pt0Hs.Y()) / dy : NDI_FLT_MAX;
		float cz = z0 != z1 ? ((z0 + (dz > 0)) * kHeightMapGridSpacing   - (float)pt0Hs.Z()) / dz : NDI_FLT_MAX;

		dx = dx ? sx * kHeightMapGridSpacing / dx : NDI_FLT_MAX;
		dy = dy ? sy * kHeightMapHeightSpacing / dy : NDI_FLT_MAX;
		dz = dz ? sz * kHeightMapGridSpacing / dz : NDI_FLT_MAX;

		CONST_EXPR I32 kOut = -1;
		I32 c = (U32)x0 < map.m_sizeX && (U32)z0 < map.m_sizeZ ? map.m_data[z0*map.m_sizeX + x0] : kOut;

		//if (FALSE_IN_FINAL_BUILD(debugDraw))
		//{
		//	Vector coll(x0, y0 - 0.5f, z0);
		//	coll *= Vector(kHeightMapGridSpacing, kHeightMapHeightSpacing, kHeightMapGridSpacing);
		//	coll += AsVector(pMesh->LocalToWorld(map.m_bboxMinLs));
		//	const Point collPt = AsPoint(coll);
		//	//g_prim.Draw(DebugBox(collPt, collPt + Vector(kHeightMapGridSpacing, kHeightMapHeightSpacing, kHeightMapGridSpacing), kColorYellow, PrimAttrib(/*kPrimDisableDepthTest*//*, kPrimEnableWireframe*/)));
		//}

#ifndef FINAL_BUILD
		U64 debugCounter = 0;
#endif
		while (x0 != x1 || y0 != y1 || z0 != z1)
		{
#ifndef FINAL_BUILD
			++debugCounter;
#endif
			I32 lastx = x0, lasty = y0, lastz = z0;
			if (cx < cz)
			{
				x0 += sx;
				cx += dx;
				if (x0 == x1)
					cx = NDI_FLT_MAX;
			}
			else
			{
				z0 += sz;
				cz += dz;
				if (z0 == z1)
					cz = NDI_FLT_MAX;
			}

			// we've moved to a new cell horizontally: from (lastx, lastz) to (x0, z0)
			// now move y to the correct place
			const float yrel = dy && y0 != y1 ? (float)Ceil((Min(cx, cz) - cy) / dy) : 0.0f;
			NAV_ASSERT(yrel >= 0.0f);
			I32 steps = Abs(y1 - y0);
			if (yrel < steps)
				steps = yrel;
			//NAV_ASSERT(steps >= 0);
			y0 += steps * sy;
			cy += steps * dy;

			//if (FALSE_IN_FINAL_BUILD(debugDraw))
			//{
			//	Vector coll(x0, y0 - 0.5f, z0);
			//	coll *= Vector(kHeightMapGridSpacing, kHeightMapHeightSpacing, kHeightMapGridSpacing);
			//	coll += AsVector(pMesh->LocalToWorld(map.m_bboxMinLs));
			//	const Point collPt = AsPoint(coll);
			//	//g_prim.Draw(DebugBox(collPt, collPt + Vector(kHeightMapGridSpacing, kHeightMapHeightSpacing, kHeightMapGridSpacing), kColorBlue, PrimAttrib(/*kPrimDisableDepthTest*//*, kPrimEnableWireframe*/)));
			//}

			I32 d = (U32)x0 < map.m_sizeX && (U32)z0 < map.m_sizeZ ? map.m_data[z0*map.m_sizeX + x0] : kOut;

			if ((c > 0 && (((c <= y0) ^ (c < lasty)) || (d > 0 && ((c <= y0) ^ (d < y0))))))
			{
				if (pHitWs)
				{
					Vector coll(x0, y0 - 0.5f, z0);
					coll *= Vector(kHeightMapGridSpacing, kHeightMapHeightSpacing, kHeightMapGridSpacing);
					coll += AsVector(localToParent + map.m_bboxMinLs);
					const Vector dirWs = SafeNormalize(pt1Ws - pt0Ws, kUnitZAxis);
					const Point collPt = pt0Ws + Dot(AsPoint(coll) - pt0Ws, dirWs) * dirWs;
					*pHitWs = collPt;
				}

				if (FALSE_IN_FINAL_BUILD(debugDraw))
				{
					g_prim.Draw(DebugLine(pt0Ws, pt1Ws, kColorRed, kColorRed, 4.0f), kPrimDuration1FramePauseable);
					Vector coll(x0, y0 - 0.5f, z0);
					coll *= Vector(kHeightMapGridSpacing, kHeightMapHeightSpacing, kHeightMapGridSpacing);
					coll += AsVector(localToParent + map.m_bboxMinLs);
					const Vector dirWs = SafeNormalize(pt1Ws - pt0Ws, kUnitZAxis);
					const Point collPt = pt0Ws + Dot(AsPoint(coll) - pt0Ws, dirWs) * dirWs;
					g_prim.Draw(DebugCross(collPt, 0.2f, 1.0f), kPrimDuration1FramePauseable);
				}

				return false;
			}
			if (c != kOut && d == kOut)
			{
				break;
			}
#ifndef FINAL_BUILD
			NAV_ASSERT(debugCounter < 1000000);
#endif
			c = d;
		}
	}

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{
		g_prim.Draw(DebugLine(pt0Ws, pt1Ws, kColorGreen, kColorGreen, 4.0f), kPrimDuration1FramePauseable);
	}

	if (pHitWs)
		*pHitWs = pt1Ws;

	return true;
}

///-----------------------------------------------------------------------------------------------
JOB_ENTRY_POINT(ExposureMapJob)
{
	PROFILE(AI, ExposureMapJob);

	ExposureMapState& state = *(ExposureMapState*)jobParam;

	const bool prevEnableSetting = state.m_enabled;
	state.m_enabled = g_navCharOptions.m_bExposureMapEnabled; //@CHEAP could disable entire exposure map job if needed

	// if we were and still are disabled, stop here
	if (!prevEnableSetting && !state.m_enabled)
		return;

	NavMeshReadLockJanitor readLock(FILE_LINE_FUNC);

	// gather work from previous frame, if any, and clean up
	GatherWorkload(state);

	// if we've just been disabled, stop here
	if (!state.m_enabled || g_ndConfig.m_pNetInfo->IsNetActive())
		return;

	// if we're paused, stop here
	// NOTE: debugging only as we use a DoubleGameFrame allocator for the compute context.
	// that would have to become a Pausable allocator for this to be safe.
	//if (EngineComponents::GetNdFrameState()->GetClock(kGameClock)->IsPaused())
		//return;

	// switch between CPU and GPU now, after cleanup and before initing new map types
#ifdef ALLOW_CPU_EXPOSURE_MAP
	state.m_useGpu = g_navCharOptions.m_bExposureMapUseGPU;
#endif //ALLOW_CPU_EXPOSURE_MAP

	// initialize any types that need it
	for (I32 iSrc = 0; iSrc < DC::kExposureSourceCount; ++iSrc)
	{
		ExposureSourceState& srcState = state.m_srcState[iSrc];

		NAV_ASSERT(srcState.m_curObs >= 0);
		NAV_ASSERT(srcState.m_curObs < kExposureMapMaxObservers);

		// if this type needs initialization
		if (srcState.m_curObs == 0)
		{
			// new navmeshes to operate on
			NavMeshMgr::NavMeshBits meshBits;
			meshBits.ClearAllBits();
			InitTypeState(meshBits, srcState, iSrc, state.m_dynamicHeightMaps, state.m_numDynamicHeightMaps);
			UpdateTypeStateMeshBits(meshBits, srcState, iSrc);
		}
	}

	//  pick up where we left off with m_curSrc and kick kMaxObserversPerFrame type-observer pairs
	//  (or stop early if we wrap around)

	ALWAYS_ASSERT(state.m_pComputeContext == nullptr);
	ALWAYS_ASSERT(state.m_pLastLabel == nullptr);

	// prepare some pointers to navmeshes for speed now that we have a read lock
	const NavMesh* meshes[DC::kExposureSourceCount * kExposureMapMaxHeightMaps];
	for (I32 iSrc = 0; iSrc < DC::kExposureSourceCount; ++iSrc)
	{
		for (I32 iMesh = 0; iMesh < kExposureMapMaxHeightMaps; ++iMesh)
		{
			meshes[iSrc * kExposureMapMaxHeightMaps + iMesh] = state.m_srcState[iSrc].m_hMeshes[iMesh].ToNavMesh();
		}
	}

	// prepare data that is independent of observer (will be pointed to in the SRTs)
	struct SrtData
	{
		// input occluder heightmap textures for this src
		ndgi::TSharp m_heightMaps[kExposureMapMaxHeightMaps];

		// the offsets of all the occluder heightmaps for this src, relative to this test map
		I32 m_hMapXOffsets[kExposureMapMaxHeightMaps];
		F32 m_hMapYOffsets[kExposureMapMaxHeightMaps];
		I32 m_hMapZOffsets[kExposureMapMaxHeightMaps];
	};

	SrtData* srtData = NDI_NEW(kAllocGameToGpuRing) SrtData[DC::kExposureSourceCount * kExposureMapMaxHeightMaps];

	for (I32 iSrc = 0; iSrc < DC::kExposureSourceCount; ++iSrc)
	{
		ExposureSourceState& srcState = state.m_srcState[iSrc];

		// prepare this frame's dynamic heightmap textures
		for (I32 iDyn = 0; iDyn < srcState.m_numDynamicHeightMaps; ++iDyn)
		{
			DynamicHeightMap& dyn = srcState.m_dynamicHeightMaps[iDyn];
			dyn.GenerateTexture();
		}

		// for each test map, populate the necessary data
		for (I32 iTest = 0; iTest < srcState.m_numMeshes; ++iTest)
		{
			const NavMesh* pTestMesh = meshes[iSrc * kExposureMapMaxHeightMaps + iTest];
			const NavMeshHeightMap* pTestMap = pTestMesh->GetHeightMap();
			NAV_ASSERT(pTestMap);

			// the current heightmap's world space origin:
			const Vector localToParent = AsVector(pTestMesh->GetOriginPs().GetPosition());
			const Point curOriginWs = localToParent + pTestMap->m_bboxMinLs;

			SrtData& data = srtData[iSrc * kExposureMapMaxHeightMaps + iTest];

			for (I32 iOcc = 0; iOcc < srcState.m_numMeshes; ++iOcc)
			{
				const NavMesh* pOccMesh = meshes[iSrc * kExposureMapMaxHeightMaps + iOcc];
				const NavMeshHeightMap* pOccMap = pOccMesh->GetHeightMap();
				NAV_ASSERT(pOccMap);

				data.m_heightMaps[iOcc] = pOccMesh->m_exposure->m_heightMapTex.GetTSharp(ndgi::kReadOnly);

				const Vector occLocalToParent = AsVector(pOccMesh->GetOriginPs().GetPosition());
				const Point occOriginWs = occLocalToParent + pOccMap->m_bboxMinLs;
				const Vector offset = occOriginWs - curOriginWs;

				data.m_hMapXOffsets[iOcc] = FastCvtToIntRound(offset.X() * kInvHeightMapGridSpacing);
				data.m_hMapYOffsets[iOcc] = offset.Y() / (255.0f * kHeightMapHeightSpacing);
				data.m_hMapZOffsets[iOcc] = FastCvtToIntRound(offset.Z() * kInvHeightMapGridSpacing);
			}

			for (I32 iDyn = 0; iDyn < srcState.m_numDynamicHeightMaps; ++iDyn)
			{
				const I32 iOcc = srcState.m_numMeshes + iDyn;

				const DynamicHeightMap& dyn = srcState.m_dynamicHeightMaps[iDyn];

				AI_ASSERT(dyn.m_tex.Valid());
				data.m_heightMaps[iOcc] = dyn.m_tex.GetTSharp(ndgi::kReadOnly);

				const Point occOriginWs = dyn.m_bboxMinWs;
				const Vector offset = occOriginWs - curOriginWs;

				data.m_hMapXOffsets[iOcc] = FastCvtToIntRound(offset.X() * kInvHeightMapGridSpacing);
				data.m_hMapYOffsets[iOcc] = offset.Y() / (255.0f * kHeightMapHeightSpacing);
				data.m_hMapZOffsets[iOcc] = FastCvtToIntRound(offset.Z() * kInvHeightMapGridSpacing);
			}
		}
	}

	// yo, heard you like CRTP
	struct Srt_ComputeAiExposureMap : public ndgi::ValidSrt<Srt_ComputeAiExposureMap>
	{
		// common data
		SrtData* m_pData;

		// the output buffer to use for the current (a.k.a. test) heightmap
		ndgi::TSharp m_outputBuffer;

		// input test heightmap texture
		ndgi::TSharp m_testHeightMap;

		// the observer's position on the test heightmap
		I32 m_obsX;
		F32 m_obsY;
		I32 m_obsZ;

		// the x and z components of the current observer's forward vector
		F32 m_forwardX;
		F32 m_forwardZ;

		// tuning parameters for polar visual field model
		F32 m_fovFactorA;
		F32 m_fovFactorB;
		F32 m_fovFactorC;
		F32 m_fovFactorD;

		// the indices of the start X and Z cells
		I32 m_startX;
		I32 m_startZ;

		// the number of cells to run in each direction
		I32 m_runX;
		I32 m_runZ;

		void Validate(RenderFrameParams const* pRenderFrameParams, const GpuState* pGpuState)
		{
			//m_outputBuffer.Validate(pRenderFrameParams, pGpuState);
			//for (I32 iMap = 0; iMap < g_exposureMapMgr.GetNumHeightMaps(); ++iMap)
			//{
			//	m_heightMaps[iMap].Validate(pRenderFrameParams, pGpuState);
			//}
			//m_testHeightMap.Validate(pRenderFrameParams, pGpuState);
		}
	};

	I32 iSrt = 0;
	// create these unconditionally as they're used by the CPU too
	Srt_ComputeAiExposureMap* srts = NDI_NEW(kAllocGameToGpuRing) Srt_ComputeAiExposureMap[kExposureMapObserversPerFrame * kExposureMapMaxHeightMaps];
	ALWAYS_ASSERT(srts);

	const I32 dcbSize = 1280 * kExposureMapObserversPerFrame * kExposureMapMaxHeightMaps;

	const I32 origCurType = state.m_curSrc;

	I32 obsRemaining = kExposureMapObserversPerFrame;
	while (true)
	{
		ExposureSourceState& srcState = state.m_srcState[state.m_curSrc];

		NAV_ASSERT(srcState.m_curObs <= srcState.m_numObs);
		const I32 obsThisType = Min(obsRemaining, srcState.m_numObs - srcState.m_curObs);

		// nothing left to do in this type. advance to next one.
		if (obsThisType == 0)
		{
			// increment curType
			if (++state.m_curSrc >= DC::kExposureSourceCount)
				state.m_curSrc = 0;

			if (obsRemaining == 0)
			{
				// if we've consumed all observers for this frame, we abort
				break;
			}

			if (state.m_curSrc == origCurType)
			{
				// if we've advanced all the way back around,
				// we abort, processing fewer than kExposureMapObserversPerFrame this frame
				break;
			}

			continue;
		}

		const I32 iStartObs = srcState.m_curObs;
		NAV_ASSERT(iStartObs >= 0);
		NAV_ASSERT(iStartObs < srcState.m_numObs);

		const I32 iEndObs = iStartObs + obsThisType;
		NAV_ASSERT(iEndObs >= 0);
		NAV_ASSERT(iEndObs <= srcState.m_numObs);

		NAV_ASSERT(obsThisType > 0);

		// go ahead and commit these state updates as doing it in real time
		// in the double-for-loop would not be straightforward
		srcState.m_curObs = iEndObs;
		obsRemaining -= obsThisType;
		NAV_ASSERT(obsRemaining >= 0);

		//printf(">>> Frame %lld: Processing %d test map%s x %d occ map%s (%d static, %d dynamic) x %d observer%s (%d to %d) for %d (%s)\n",
		//	EngineComponents::GetNdFrameState()->m_gameFrameNumber,
		//	srcState.m_numMeshes, srcState.m_numMeshes == 1 ? "" : "s",
		//	srcState.m_numMeshes + srcState.m_numDynamicHeightMaps, srcState.m_numMeshes + srcState.m_numDynamicHeightMaps == 1 ? "" : "s",
		//	srcState.m_numMeshes,
		//	srcState.m_numDynamicHeightMaps,
		//	iEndObs - iStartObs, iEndObs - iStartObs == 1 ? "" : "s",
		//	iStartObs,
		//	iEndObs - 1,
		//	state.m_curSrc,
		//	DC::GetExposureSourceName(state.m_curSrc));

		// ------------- for each heightmap for which we want to create an exposure map ("test" map) ------------
		for (U32 iTest = 0; iTest < srcState.m_numMeshes; ++iTest)
		{
			// the NavMesh:
			const NavMesh& testNavMesh = *meshes[state.m_curSrc * kExposureMapMaxHeightMaps + iTest];

			Aabb testAabb = testNavMesh.GetAabbWs();
			testAabb.m_min.SetY(0.0f);
			testAabb.m_max.SetY(0.0f);

			// the heightmap:
			const NavMeshHeightMap& testHeightMap = *testNavMesh.GetHeightMap();

			// the output buffer for this heightmap:
			U64* __restrict const pOutputBuffer = testNavMesh.m_exposure->GetMap(ExposureMapType::kScratch, state.m_curSrc);

			const Vector localToParent = AsVector(testNavMesh.GetOriginPs().GetPosition());
			const Point originWs = localToParent + testHeightMap.m_bboxMinLs;
			testNavMesh.m_exposure->m_originWs = originWs;
			{
				const Vector originHs = AsVector(originWs) * kInvHeightMapGridSpacing;

				testNavMesh.m_exposure->m_originX = FastCvtToIntFloor(originHs.X());
				testNavMesh.m_exposure->m_originZ = FastCvtToIntFloor(originHs.Z());
			}

			// extra scratch space for the CPU version since it has to be all serial and boring and
			// do one observer at a time, and occluder heightmap at a time
			//
			// only allocated if not using GPU
			//
			// inner buffer is for a single observer for a single occluder heightmap
			// this gets bitwise AND'ed with the output buffer
			//
			// outer buffer is for a single observer
			// this gets bitwise OR'd with the output buffer
			//
			// output buffer therefore has a bit set if:
			// ANY ray that passed through that cell
			// from ANY observer
			// made it through ALL occluder heightmaps
			const ndgi::Texture* pScratchTex = nullptr;
#ifdef ALLOW_CPU_EXPOSURE_MAP
			U64* __restrict pScratchBufOuter = nullptr;
			U64* __restrict pScratchBufInner = nullptr;
			if (!state.m_useGpu)
			{
				const U32 numBlocks = AiExposureData::GetNumBlocks(testHeightMap.m_sizeX, testHeightMap.m_sizeZ);
				pScratchBufOuter = NDI_NEW(kAllocSingleGameFrame) U64[numBlocks << 1];
				pScratchBufInner = pScratchBufOuter + numBlocks;
			}
			else
#endif
			{
				pScratchTex = &testNavMesh.m_exposure->m_scratchTex;
			}

			// ------------- for each observer we need to process this frame ------------
			for (I32 iObs = iStartObs; iObs < iEndObs; ++iObs)
			{
				const ExposureMapObserver& obs = srcState.m_obs[iObs];
				const Point obsPosWs = obs.m_locator.GetPosition();
				const Point obsPosHs = AsPoint((obsPosWs - originWs) * kInvHeightMapGridSpacing);

				Srt_ComputeAiExposureMap* pSrt = &srts[iSrt++];

				pSrt->m_pData = &srtData[state.m_curSrc * kExposureMapMaxHeightMaps + iTest];

				pSrt->m_obsX = FastCvtToIntFloor(obsPosHs.X());
				pSrt->m_obsY = (obsPosWs + obs.m_eyeOffset - originWs).Y() / (255.0f * kHeightMapHeightSpacing);
				pSrt->m_obsZ = FastCvtToIntFloor(obsPosHs.Z());

				NAV_ASSERT(Abs(pSrt->m_obsX) < 100000);
				NAV_ASSERT(Abs(pSrt->m_obsZ) < 100000);

				// observer direction
				{
					const Vector forward = obs.m_locator.TransformVector(kUnitZAxis);
					pSrt->m_forwardX = forward.X();
					pSrt->m_forwardZ = forward.Z();
					pSrt->m_fovFactorA = obs.m_exp.m_a * kInvHeightMapGridSpacing;
					pSrt->m_fovFactorB = obs.m_exp.m_b;
					pSrt->m_fovFactorC = obs.m_exp.m_c * kInvHeightMapGridSpacing;
					pSrt->m_fovFactorD = obs.m_exp.m_d;
				}

				// aabb
				{

					// debug draw AABB
					//{
					//	Aabb debugAabb = testAabb;
					//	debugAabb.m_min.SetY(debugAabb.m_min.Y() + 0.1f);
					//	debugAabb.m_max.SetY(debugAabb.m_max.Y() + 0.1f);
					//	g_prim.Draw(DebugBox(debugAabb, kColorRedTrans));
					//	g_prim.Draw(DebugQuad(
					//		obs.m_expBoundsWs[0] + Vector(0.0f, 0.25f, 0.0f),
					//		obs.m_expBoundsWs[1] + Vector(0.0f, 0.25f, 0.0f),
					//		obs.m_expBoundsWs[2] + Vector(0.0f, 0.25f, 0.0f),
					//		obs.m_expBoundsWs[3] + Vector(0.0f, 0.25f, 0.0f),
					//		kColorGreenTrans));
					//}

					const Aabb& aabb = IntersectModelAabb(obs.m_expBoundsWs, testAabb);
					if (aabb.IsValid())
					{
						// debug draw AABB
						//{
						//	Aabb debugAabb = aabb;
						//	debugAabb.m_min.SetY(debugAabb.m_min.Y() + 0.20f);
						//	debugAabb.m_max.SetY(debugAabb.m_max.Y() + 0.20f);
						//	g_prim.Draw(DebugBox(debugAabb, kColorBlueTrans));
						//}

						const Vector startHs = (aabb.m_min - originWs) * kInvHeightMapGridSpacing;
						const int startX = FastCvtToIntFloor(startHs.X());
						const int startZ = FastCvtToIntFloor(startHs.Z());

						const Vector endHs = (aabb.m_max - originWs) * kInvHeightMapGridSpacing;
						const int endX = FastCvtToIntCeil(endHs.X());
						const int endZ = FastCvtToIntCeil(endHs.Z());

						pSrt->m_startX = startX;
						pSrt->m_startZ = startZ;

						// always do at least 2 cells in each dimension so the perimeter traversal is valid
						pSrt->m_runX = Max(1, endX - startX);
						pSrt->m_runZ = Max(1, endZ - startZ);
					}
					else
					{
						pSrt->m_runX = 0;
						pSrt->m_runZ = 0;
					}
				}

#ifdef ALLOW_CPU_EXPOSURE_MAP
				if (state.m_useGpu)
#endif
				{
					// GPU version: prepare GPU-specific SRT members and then dispatch the compute shader

					// only if we have a nonempty rectangle
					if (pSrt->m_runX)
					{
						pSrt->m_outputBuffer = pScratchTex->GetTSharp(ndgi::kGpuCoherent);

						pSrt->m_testHeightMap = meshes[state.m_curSrc * kExposureMapMaxHeightMaps + iTest]->m_exposure->m_heightMapTex.GetTSharp(ndgi::kReadOnly);

						if (!state.m_pComputeContext)
						{
							// first one:

							U8* dcb = NDI_NEW(kAllocGameToGpuRing, kAlign64) U8[dcbSize];
							ALWAYS_ASSERT(dcb);

							state.m_pComputeContext = NDI_NEW(kAllocDoubleGameFrame, kAlign64) ndgi::ComputeContext(dcb, dcbSize, "ComputeAiExposureMap");
							ALWAYS_ASSERT(state.m_pComputeContext);

							state.m_pComputeContext->Open();
						}
						else
						{
							// subsequent ones:

							// finish the previous dispatch with a write completion label, allocated in the DCB
							// as more are coming
							state.m_pLastLabel = state.m_pComputeContext->AllocateAndInitLabel32(0);
							state.m_pComputeContext->WriteLabel32AtEndOfCompute(state.m_pLastLabel, 1);

							// wait on the preceding dispatch
							state.m_pComputeContext->WaitOnLabel32Equal(state.m_pLastLabel, 1);
						}

						{
							// clear intermediate buffer

							struct ClearSrt : ndgi::ValidSrt<ClearSrt>
							{
								ndgi::TSharp m_tex;

								void Validate(const RenderFrameParams *pParams, const GpuState *pGpuState)
								{
									//m_tex.Validate(pParams, pGpuState);
								}
							};

							ClearSrt clearSrt;
							clearSrt.m_tex = pScratchTex->GetTSharp(ndgi::kGpuCoherent);
							state.m_pComputeContext->SetCsShader(g_postShading.GetCsByIndex(kCsComputeAiExposureMapClear));

							state.m_pComputeContext->SetCsSrt(&clearSrt, sizeof(ClearSrt));
							state.m_pComputeContext->SetCsSrtValidator(ClearSrt::ValidateSrt);

							// x direction: 64 threads per group, 1 element per thread
							// z direction: 1 thread per group, 4 elements per thread
							state.m_pComputeContext->Dispatch((testHeightMap.m_sizeX - 1) / 64 + 1,
								(testHeightMap.m_sizeZ - 1) / 4 + 1,
								1,
								&state.m_pLastLabel);
						}

						{
							// exposure step to compute intermediate exposure data

							// wait on the preceding dispatch
							state.m_pComputeContext->WaitOnLabel32Equal(state.m_pLastLabel, 1);

							state.m_pComputeContext->SetCsShader(g_postShading.GetCsByIndex(kCsComputeAiExposureMap));

							state.m_pComputeContext->SetCsSrt(&pSrt, sizeof(pSrt));

							// this is gone in final build
							state.m_pComputeContext->SetCsSrtValidator(Srt_ComputeAiExposureMap::ValidateSrt);

							// if you want to restrict the shader to CU 0
							// state.m_pComputeContext->SetComputeResourceManagement(0, 1);
							// state.m_pComputeContext->SetComputeResourceManagement(1, 1);

							// a thread for every cell on the perimeter of the test rectangle, for every occluder heightmap
							state.m_pComputeContext->Dispatch((2 * (pSrt->m_runX + pSrt->m_runZ) - 1) / kExposureMapThreadsPerGroup + 1,
								srcState.m_numMeshes + srcState.m_numDynamicHeightMaps,
								1,
								&state.m_pLastLabel);
						}

						{
							// reduction step to bitwise OR source data from U32 scratch tex into scratch bitmap

							// wait on the preceding dispatch
							state.m_pComputeContext->WaitOnLabel32Equal(state.m_pLastLabel, 1);

							struct ReduceSrt : ndgi::ValidSrt<ReduceSrt>
							{
								ndgi::TSharp m_in;
								ndgi::VSharp m_out;
								U32 m_mask;
								U32 m_pitch;

								void Validate(const RenderFrameParams *pParams, const GpuState *pGpuState)
								{
									//m_in.Validate(pParams, pGpuState);
									//m_out.Validate(pParams, pGpuState);
								}
							};

							ReduceSrt reduceSrt;
							reduceSrt.m_mask = (1ULL << (srcState.m_numMeshes + srcState.m_numDynamicHeightMaps)) - 1;
							reduceSrt.m_pitch = testHeightMap.m_bitmapPitch;

							// NOT kReadOnly as we re-use scratch buffer same frame and therefore we
							// want to bypass L1 to ensure we read latest data
							reduceSrt.m_in = pScratchTex->GetTSharp(ndgi::kGpuCoherent);
							reduceSrt.m_out.InitAsRegularBuffer(pOutputBuffer, 4, testNavMesh.m_exposure->m_blocks << 4);
							reduceSrt.m_out.SetResourceMemoryType(ndgi::kSystemCoherent);

							state.m_pComputeContext->SetCsShader(g_postShading.GetCsByIndex(kCsComputeAiExposureMapReduce));

							state.m_pComputeContext->SetCsSrt(&reduceSrt, sizeof(ReduceSrt));
							state.m_pComputeContext->SetCsSrtValidator(ReduceSrt::ValidateSrt);

							// x direction: 1 thread per group, 32 elements per thread (to write out whole 32-bit word)
							// z direction: 64 threads per group, 1 element per thread
							// do NOT allocate an out label. the very last out label needs to live somewhere other than
							// inside the DCB, so that we can wait on it next game frame as well as at end of GPU frame,
							// but we don't know in advance which dispatch will be the last, so,
							// defer that label allocation and writing of the WaitOnLabel command until later
							state.m_pComputeContext->Dispatch((testHeightMap.m_sizeX - 1) / 32 + 1,
								(testHeightMap.m_sizeZ - 1) / 64 + 1,
								1,
								(ndgi::Label32*)nullptr);
						}
					}
				}
#ifdef ALLOW_CPU_EXPOSURE_MAP
				else
				{
					// CPU version: just, you know, do all the work right now
					//
					// (note: not optimal structure for CPU, but designed to match the GPU version for ease of debugging)
					//

					// will be bitwise AND'ed against, so initialize by setting all
					testNavMesh.m_exposure->FillMap(pScratchBufOuter);

					U32 occSizesX[kExposureMapMaxHeightMaps];
					U32 occSizesZ[kExposureMapMaxHeightMaps];
					const U8* occDatas[kExposureMapMaxHeightMaps];

					for (I32 iMap = 0; iMap < srcState.m_numMeshes; ++iMap)
					{
						const NavMeshHeightMap* const pMap = meshes[state.m_curSrc * kExposureMapMaxHeightMaps + iMap]->GetHeightMap();
						occSizesX[iMap] = pMap->m_sizeX;
						occSizesZ[iMap] = pMap->m_sizeZ;
						occDatas[iMap] = pMap->m_data;
					}

					for (I32 iDyn = 0; iDyn < srcState.m_numDynamicHeightMaps; ++iDyn)
					{
						const I32 iMap = srcState.m_numMeshes + iDyn;
						const DynamicHeightMap& dyn = srcState.m_dynamicHeightMaps[iDyn];
						occSizesX[iMap] = dyn.m_sizeX;
						occSizesZ[iMap] = dyn.m_sizeZ;
						occDatas[iMap] = dyn.m_data;
					}

					for (I32 m = 0; m < srcState.m_numMeshes + srcState.m_numDynamicHeightMaps; ++m)
					{
						// will be *implicitly* bitwise OR'd against by starting all 0
						// and only having cells written to if any ray finds them visible
						testNavMesh.m_exposure->ClearMap(pScratchBufInner);

						for (I32 tidx = 0; tidx < 2 * (pSrt->m_runX + pSrt->m_runZ); ++tidx)
						{
							// origin cell coordinates (x0, z0) on test map
							int x0 = pSrt->m_obsX;
							int z0 = pSrt->m_obsZ;

							// branchlessly extract cell deltas (dx, dz) from observer to destination cell
							// on the perimeter of the test rectangle, in test map space, from linear index (tidx)
							I32 dx, dz;
							F32 radSqr;
							F32 cosTheta;
							{
								I32 idx = tidx;

								dx = Min(pSrt->m_runX, idx) - x0 + pSrt->m_startX;
								idx -= Min(pSrt->m_runX, idx);

								dz = Min(pSrt->m_runZ, idx) - z0 + pSrt->m_startZ;
								idx -= Min(pSrt->m_runZ, idx);

								dx -= Min(pSrt->m_runX, idx);
								idx -= Min(pSrt->m_runX, idx);

								dz -= idx;

								// square of termination distance due to reaching edge of test rectangle
								radSqr = dx * dx + dz * dz;

								// bail if threadID.x larger than workload or if already at destination
								if (idx >= pSrt->m_runZ || !radSqr)
								{
									goto next_shadow_ray;
								}

								cosTheta = (pSrt->m_forwardX * dx + pSrt->m_forwardZ * dz) / sqrt(radSqr);
							}

							{
								// voxel traversal (except it's 2D, so... cover line?) error
								I32 err = Abs(dx) - Abs(dz);

								dx <<= 1;
								dz <<= 1;

								// terminate at edge of the visual field, if that comes first
								{
									const float rad1 = pSrt->m_fovFactorA / (1.0f - pSrt->m_fovFactorB * cosTheta);
									if (rad1 > 0.0f)
									{
										radSqr = Min(radSqr, rad1*rad1);
									}

									const float rad2 = pSrt->m_fovFactorC / Sqrt(1.0f - pSrt->m_fovFactorD * cosTheta);
									radSqr = Min(radSqr, rad2*rad2);
								}

								NAV_ASSERT(radSqr > 0.0f);
								NAV_ASSERT(radSqr < 10000.0f);

								// march until we enter the occluder heightmap

								// dimensions of occluder heightmap
								const U32 occSizeX = occSizesX[m];
								const U32 occSizeZ = occSizesZ[m];
								const U8* __restrict const occData = occDatas[m];

								{

									// while outside occluder heightmap
									while (U32(x0 - pSrt->m_pData->m_hMapXOffsets[m]) >= occSizeX
										|| U32(z0 - pSrt->m_pData->m_hMapZOffsets[m]) >= occSizeZ)
									{
										// write "exposed!" to test heightmap's slot for cell (x0, z0)
										if (U32(x0) < testHeightMap.m_sizeX && U32(z0) < testHeightMap.m_sizeZ)
										{
											testNavMesh.m_exposure->SetBit(pScratchBufInner, x0, z0);
										}

										// squared distance from observer
										const float dSqr = (x0 - pSrt->m_obsX) * (x0 - pSrt->m_obsX)
											+ (z0 - pSrt->m_obsZ) * (z0 - pSrt->m_obsZ);

										// traversal advance
										if (err > 0)
										{
											err -= Abs(dz);
											x0 += dx > 0 ? 1 : -1;
										}
										else
										{
											err += Abs(dx);
											z0 += dz > 0 ? 1 : -1;
										}

										// if outside radius or at destination
										if (dSqr >= radSqr)
										{
											goto next_shadow_ray;
										}
									}
								}

								{
									// lower shadow caster slopes; 2 shadowcasters tracked
									F32 shadowSlopesL[2] = { 1.0f / 0.0f, 1.0f / 0.0f };

									// upper shadow caster slopes; 2 shadowcasters tracked
									F32 shadowSlopesU[2] = { -1.0f / 0.0f, -1.0f / 0.0f };

									// previous cell's height (initialized to "hole")
									F32 prevOccHeight = pSrt->m_pData->m_hMapYOffsets[m];

									// first caster we encounter will be a new caster, which will increment this to 0
									I32 iCaster = -1;

									for (;;)
									{
										// current cell is (x0, z0) on test map

										// occluder heightmap height (or 0 for "hole"), plus pSrt->m_pData->m_hMapYOffsets[m] to shift into test heightmap space
										float occHeight = pSrt->m_pData->m_hMapYOffsets[m];
										if (U32(x0 - pSrt->m_pData->m_hMapXOffsets[m]) < occSizeX
											&& U32(z0 - pSrt->m_pData->m_hMapZOffsets[m]) < occSizeZ)
										{
											const U32 idx = U32(z0 - pSrt->m_pData->m_hMapZOffsets[m])
												* occSizeX
												+ U32(x0 - pSrt->m_pData->m_hMapXOffsets[m]);
											occHeight += occData[idx] / 255.0f;
										}

										// test heightmap height (or 0 for "hole")
										float testHeight = 0.0f;
										if (U32(x0) < testHeightMap.m_sizeX && U32(z0) < testHeightMap.m_sizeZ)
										{
											testHeight = testHeightMap.m_data[z0 * testHeightMap.m_sizeX + x0]
												/ 255.0f;
										}

										// squared distance from observer
										const F32 dSqr = (pSrt->m_obsX - x0) * (pSrt->m_obsX - x0)
											+ (pSrt->m_obsZ - z0) * (pSrt->m_obsZ - z0);

										// inverse distance from observer
										const F32 dInv = 1.0f / sqrtf(dSqr);

										// if current cell is not a hole
										if (occHeight != pSrt->m_pData->m_hMapYOffsets[m])
										{
											// if the previous cell was a hole, this is the start of a new shadowcaster...
											if (prevOccHeight == pSrt->m_pData->m_hMapYOffsets[m])
											{
												// ...so advance the caster if it's the first; otherwise, replace the current one
												// (in this way we keep the FIRST and LAST shadowcasters seen, which produces
												// superior results compared to keeping the first two or the last two)
												iCaster = Min(1, iCaster + 1);
												shadowSlopesL[1] = 1.0f / 0.0f;
												shadowSlopesU[1] = -1.0f / 0.0f;
											}

											// slope to this cell
											const float newSlope = (occHeight - pSrt->m_obsY) * dInv;

											// if starting a new caster or new low-water-mark for this caster, update lower slope accordingly
											if (newSlope < shadowSlopesL[iCaster])
												shadowSlopesL[iCaster] = newSlope;

											// if starting a new caster or new high-water-mark for this caster, update upper slope accordingly
											if (newSlope > shadowSlopesU[iCaster])
												shadowSlopesU[iCaster] = newSlope;
										}

										// set previous height to current height since we're about to advance to the next cell
										prevOccHeight = occHeight;

										// test slope, plus a variable amount to represent the height of a crouching target;
										// we cheat and keep it closer to the ground when near the observer so that an
										// observer standing right up against low cover cannot see a crouching target on the
										// opposite side of the cover, even though in reality they're totally visible
										const F32 adjustedDist = (dSqr + kExposureMapTuningAddToSquaredDistance)
											/ kExposureMapTuningDivideSquaredDistance;
										const F32 crouchSlope = (testHeight - pSrt->m_obsY
											+ Max(kExposureMapTuningMinCrouchHeight,
												Min(kExposureMapTuningMaxCrouchHeight, adjustedDist))
											/ 255.0f)
											* dInv;

										// if test heightmap doesn't have a hole and crouch height is not inside the shadow cone of either shadowcaster
										if (testHeight
											&& Min(Max(shadowSlopesL[0], crouchSlope), shadowSlopesU[0]) != crouchSlope
											&& Min(Max(shadowSlopesL[1], crouchSlope), shadowSlopesU[1]) != crouchSlope)
										{
											// write "exposed!" to output buffer for cell (x0, z0)
											testNavMesh.m_exposure->SetBit(pScratchBufInner, x0, z0);
										}

										// advance to next cell
										if (err > 0)
										{
											err -= Abs(dz);
											x0 += dx > 0 ? 1 : -1;
										}
										else
										{
											err += Abs(dx);
											z0 += dz > 0 ? 1 : -1;
										}

										// if outside radius or at destination
										if (dSqr >= radSqr)
										{
											goto next_shadow_ray;
										}
									}
								}
							}
						next_shadow_ray:;
						}

						// for a cell to be exposed, it must be visible through ALL occluder heightmaps
						testNavMesh.m_exposure->BitwiseAnd(pScratchBufOuter, pScratchBufInner);
					}

					// for a cell to be exposed, it must be visible by ANY observer
					testNavMesh.m_exposure->BitwiseOr(pOutputBuffer, pScratchBufOuter);
				}
#endif //ALLOW_CPU_EXPOSURE_MAP
			}
		}
	}

	if (state.m_pComputeContext)
	{
		ALWAYS_ASSERT(state.m_pLastLabel);

		// finish the last dispatch with a write completion label,
		// allocated NOT in the DCB but out of DoubleGameToGpuRing,
		// so that we can wait on it next game frame as well as at end of GPU frame
		state.m_pLastLabel = NDI_NEW(kAllocDoubleGameToGpuRing, kAlign64) ndgi::Label32;
		ALWAYS_ASSERT(state.m_pLastLabel);
		state.m_pLastLabel->m_value = 0;
		state.m_pComputeContext->WriteLabel32AtEndOfCompute(state.m_pLastLabel, 1);

		const float dcbFrac = (float)state.m_pComputeContext->GetBufferUsedSize() / dcbSize;
		//printf(">>> DCB Used: %.3g%%\n", 100.0f * dcbFrac);
		NAV_ASSERT(dcbFrac < 0.92f);
		state.m_pComputeContext->Close(ndgi::kCacheActionWbL2Volatile, true);
		SubmitQueue<1> sq;
		sq.Add(*state.m_pComputeContext, state.m_cmpQueue, -1, SubmitFlags::kDisableValidation);
		ProcessSubmitQueue(&sq);
	}
}

///-----------------------------------------------------------------------------------------------
void ExposureMapManager::WaitForJob()
{
#if !FINAL_BUILD
	ALWAYS_ASSERT(m_initialized);
#endif

	if (m_state.m_hJobCounter)
	{
		ndjob::WaitForCounter(m_state.m_hJobCounter);
	}
}

///-----------------------------------------------------------------------------------------------
void ExposureMapManager::GatherJob()
{
#if !FINAL_BUILD
	ALWAYS_ASSERT(m_initialized);
#endif

	if (m_state.m_hJobCounter)
	{
		ndjob::WaitForCounterAndFree(m_state.m_hJobCounter);
		m_state.m_hJobCounter = nullptr;
	}
}

///-----------------------------------------------------------------------------------------------
void ExposureMapManager::EnqueueComputeContextWait(ndgi::DeviceContext* pContext)
{
#if !FINAL_BUILD
	ALWAYS_ASSERT(m_initialized);
#endif

	if (m_state.m_pComputeContext)
	{
		ALWAYS_ASSERT(m_state.m_pLastLabel);
		pContext->WaitOnLabel32Equal(m_state.m_pLastLabel, 1);
	}
}

///-----------------------------------------------------------------------------------------------
void ExposureMapManager::GatherComputeContext()
{
	PROFILE(AI, GatherComputeContext);

#if !FINAL_BUILD
	ALWAYS_ASSERT(m_initialized);
#endif

	if (m_state.m_pComputeContext)
	{
		// wait on the context
		m_state.m_pComputeContext->Wait();
		ALWAYS_ASSERT(m_state.m_pLastLabel->m_value == 1);
		m_state.m_pComputeContext->Release();

		m_state.m_pLastLabel = nullptr;

		// this was NEW'd but it's not necessary to call the destructor
		m_state.m_pComputeContext = nullptr;
	}
}

///-----------------------------------------------------------------------------------------------
ndjob::CounterHandle ExposureMapManager::KickJob()
{
#if !FINAL_BUILD
	ALWAYS_ASSERT(m_initialized);
#endif

	ndjob::JobDecl jobDecl(ExposureMapJob, reinterpret_cast<uintptr_t>(&m_state));
	ndjob::RunJobs(&jobDecl, 1, &m_state.m_hJobCounter, FILE_LINE_FUNC);

	return m_state.m_hJobCounter;
}

///-----------------------------------------------------------------------------------------------
void ExposureMapManager::UpdateParams(DC::ExposureSource type, const ExposureMapParams& params)
{
#if !FINAL_BUILD
	ALWAYS_ASSERT(m_initialized);
#endif

	m_state.m_srcState[type].m_pendingParams = params;
}

///-----------------------------------------------------------------------------------------------
static void LoginExposureMap(const NavMesh* pConstMesh, Level*)
{
	NavMesh* pMesh = const_cast<NavMesh*>(pConstMesh);

	if (!pMesh)
	{
		return;
	}
	const NavMeshHeightMap* pMap = pMesh->GetHeightMap();
	if (!pMap || !pMap->m_data || !pMap->m_tiledData || g_ndConfig.m_pNetInfo->IsNetActive())
	{
		return;
	}

	// allocate exposure data
	{
		ALWAYS_ASSERT(!pMesh->m_exposure);
		pMesh->m_exposure = NDI_NEW(kAlign64) AiExposureData(pMap->m_onNavMesh, pMap->m_sizeX, pMap->m_sizeZ);
		ALWAYS_ASSERT(pMesh->m_exposure);

		pMesh->m_exposure->AllocDataBlocks();
	}

	// clear out client-facing maps
	pMesh->m_exposure->ClearClientMaps();

	// create read-only heightmap texture and point to tiled data
	// memory is already allocated so 'kAllocGpuDynamicRenderTarget' passed to CreateTexture is just used
	// so .Release() doesn't try to free the memory on NavMesh logout. It's currently a no-op on level heap
	// allocations so it would be fine even if it did, but that may change, and anyway,
	// there's no reason to have it call NDI_DELETE unnecessarily.
	{
		const ndgi::Texture2dDesc desc(pMap->m_sizeX,
									   pMap->m_sizeZ,
									   1,
									   1,
									   ndgi::kR8unorm,
									   ndgi::kBindDefault,
									   ndgi::SampleDesc(1, 0),
									   ndgi::kUsageImmutable,
									   0);
		pMesh->m_exposure->m_heightMapTex = CreateTexture(kAllocGpuDynamicRenderTarget,
																pMap->m_tiledData,
																pMap->m_tiledSize,
																desc);
		ALWAYS_ASSERT(pMesh->m_exposure->m_heightMapTex.Valid());
	}


	// create scratch texture
	// this is done manually because we need to start creating the texture until we can ask it how large it needs to be,
	// then allocate that much space out of level memory, then finish creating the texture, pointing it to that base address.
	{
		const ndgi::Texture2dDesc desc(pMap->m_sizeX,
									   pMap->m_sizeZ,
									   1,
									   1,
									   ndgi::kR32u,
									   ndgi::kBindDefault,
									   ndgi::SampleDesc(1, 0),
									   ndgi::kUsageDefault,
									   0);

		sce::GpuAddress::SurfaceType type = sce::GpuAddress::kSurfaceTypeTextureFlat;
		sce::Gnm::DataFormat format		  = ndgi::orbis::GetFormat(desc.GetFormat());

		OrbisTexture newTexture;
		memset(&newTexture, 0, sizeof(OrbisTexture));

		sce::Gnm::NumFragments numFragments = ndgi::orbis::GetNumFragments(desc.m_sampleCount);
		U32 arraySize = desc.m_arraySize;

		sce::Gnm::TileMode tileMode;
		I32 ret = sce::GpuAddress::computeSurfaceTileMode(sce::Gnm::getGpuMode(),
														  &tileMode,
														  type,
														  format,
														  desc.m_sampleCount);
		NAV_ASSERT(ret == sce::GpuAddress::kStatusSuccess);

		sce::Gnm::TextureSpec spec;
		spec.init();
		spec.m_textureType	= sce::Gnm::kTextureType2dArray;
		spec.m_width		= desc.GetWidth();
		spec.m_height		= desc.GetHeight();
		spec.m_depth		= 1;
		spec.m_pitch		= 0;
		spec.m_numMipLevels = desc.m_mipLevels;
		spec.m_numSlices	= arraySize;
		spec.m_format		= format;
		spec.m_tileModeHint = tileMode;
		spec.m_minGpuMode	= sce::Gnm::getGpuMode();
		spec.m_numFragments = numFragments;
		ret = newTexture.m_texture.init(&spec);
		NAV_ASSERTF(ret == SCE_GNM_OK, ("Cannot initialize GNM texture! error=%08x\n", ret));

		sce::Gnm::SizeAlign sizeAlign = newTexture.m_texture.getSizeAlign();
		sizeAlign.m_size = (sizeAlign.m_size + 63) & ~63;
		sizeAlign.m_align = Max(sizeAlign.m_align, sce::Gnm::AlignmentType(64));

		U8* baseAddress = NDI_NEW (Alignment(sizeAlign.m_align)) U8[sizeAlign.m_size];
		memset(baseAddress, 0, sizeAlign.m_size);

		sce::Gnm::ResourceMemoryType memoryType = sce::Gnm::kResourceMemoryTypeGC;

		newTexture.m_texture.setBaseAddress(baseAddress);
		newTexture.m_texture.setResourceMemoryType(memoryType);
		newTexture.m_texture.setMipLevelRange(0, desc.m_mipLevels - 1);
		newTexture.m_info.m_format		 = desc.m_format;
		newTexture.m_info.m_usage		 = desc.m_usage;
		newTexture.m_info.m_bindFlags	 = desc.m_bindFlags;
		newTexture.m_info.m_miscFlags	 = desc.m_miscFlags;
		newTexture.m_info.m_numTotalMips = desc.m_mipLevels;
		newTexture.m_size = sizeAlign.m_size;
		newTexture.m_info.m_alignment = sizeAlign.m_align;
		newTexture.m_memContext		  = kAllocGpuDynamicRenderTarget;

		OrbisTexture* pTexture = g_orbisFactory.AddTexture(newTexture, kPersist);

		pMesh->m_exposure->m_scratchTex.Set(pTexture);
	}
}

///-----------------------------------------------------------------------------------------------
static void LogoutExposureMap(const NavMesh* pConstMesh)
{
	NavMesh* pMesh = const_cast<NavMesh*>(pConstMesh);

	if (!pMesh || !pMesh->m_exposure)
	{
		return;
	}

	// logout heightmap and scratch textures. We promise to no longer touch them. Their memory must remain, however,
	// until this frame has completely retired, because the GPU compute job may run late. It won't mind reading
	// garbage, but it should not write output into memory that's already been repurposed.
	//
	// the memory came out of the level heap and thence shall it return once more, once it is safe to do so.
	//
	pMesh->m_exposure->m_heightMapTex.Release();
	pMesh->m_exposure->m_scratchTex.Release();
}

///-----------------------------------------------------------------------------------------------
void ExposureMapManager::Init()
{
#if !FINAL_BUILD
	ALWAYS_ASSERT(!m_initialized);
#endif

	EngineComponents::GetNavMeshMgr()->AddLoginObserver(LoginExposureMap);
	EngineComponents::GetNavMeshMgr()->AddLogoutObserver(LogoutExposureMap);

	m_state.m_cmpQueue.Create(4096);
	g_computeQueueMgr.MapComputeQueue("exposure-map", &m_state.m_cmpQueue, 2, 7);

	Nav::PathCostMgr::Get().RegisterCostFunc(SID("buddy-combat"), CostFuncBuddyCombat);
	Nav::PathCostMgr::Get().RegisterCostFunc(SID("sneak"), CostFuncSneak);
	Nav::PathCostMgr::Get().RegisterCostFunc(SID("buddy-follow"), CostFuncBuddyFollow);
	Nav::PathCostMgr::Get().RegisterCostFunc(SID("buddy-lead"), CostFuncBuddyLead);
	Nav::PathCostMgr::Get().RegisterCostFunc(SID("scripted-buddy"), CostFuncScriptedBuddy);
	Nav::PathCostMgr::Get().RegisterCostFunc(SID("sneak-reduced-exposure"), CostFuncSneakReducedExposure);

#if !FINAL_BUILD
	m_initialized = true;
#endif
}

///-----------------------------------------------------------------------------------------------
void ExposureMapManager::Shutdown()
{
#if !FINAL_BUILD
	ALWAYS_ASSERT(m_initialized);
	m_initialized = false;
#endif

	g_computeQueueMgr.UnmapComputeQueue(&m_state.m_cmpQueue);
	m_state.m_cmpQueue.Release();
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 ExposureMapManager::CalculatePathTht(const PathWaypointsEx* pPath,
										 F32 maxDist) const
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());
	NAV_ASSERT(m_state.m_exposureMapLock.IsLockedForRead());

	if (!pPath || !pPath->IsValid())
	{
		return NDI_FLT_MAX;
	}

	F32 exposedLength = 0.0f;
	F32 totalLength = 0.0f;

	for (U32 i = 1; i < pPath->GetWaypointCount() && totalLength < maxDist; ++i)
	{
		const Point pt0Ws = pPath->GetWaypoint(i - 1);
		const Point pt1Ws = pPath->GetWaypoint(i);

		const float oldTotal = totalLength;
		const float addedDist = Dist(pt0Ws, pt1Ws);
		const float newTotal = totalLength + addedDist;

		const float t = Max(Min((maxDist - oldTotal) / addedDist, 1.0f), 0.0f);
		NAV_ASSERT(IsReasonable(t));
		const Point endWs = Lerp(pt0Ws, pt1Ws, t);

		const NavMesh* navMesh0 = NavMeshHandle(pPath->GetNavId(i - 1)).ToNavMesh();
		const NavMesh* navMesh1 = NavMeshHandle(pPath->GetNavId(i)).ToNavMesh();

		if (navMesh0 && navMesh0->m_exposure)
		{
			exposedLength += navMesh0->m_exposure->IntegrateTht(pt0Ws, endWs);
		}

		if (navMesh1 && navMesh0 != navMesh1 && navMesh1->m_exposure)
		{
			exposedLength += navMesh1->m_exposure->IntegrateTht(pt0Ws, endWs);
		}

		totalLength = newTotal;
	}

	return exposedLength;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 ExposureMapManager::CalculatePathAvd(const PathWaypointsEx* pPath) const
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());
	NAV_ASSERT(m_state.m_exposureMapLock.IsLockedForRead());

	if (!pPath || !pPath->IsValid())
	{
		return NDI_FLT_MAX;
	}

	F32 exposedLength = 0.0f;

	for (U32 i = 1; i < pPath->GetWaypointCount(); ++i)
	{
		const Point pt0Ws = pPath->GetWaypoint(i - 1);
		const Point pt1Ws = pPath->GetWaypoint(i);

		const NavMesh* navMesh0 = NavMeshHandle(pPath->GetNavId(i - 1)).ToNavMesh();
		const NavMesh* navMesh1 = NavMeshHandle(pPath->GetNavId(i)).ToNavMesh();

		if (navMesh0 && navMesh0->m_exposure)
		{
			exposedLength += navMesh0->m_exposure->IntegrateAvd(pt0Ws, pt1Ws);
		}

		if (navMesh1 && navMesh0 != navMesh1 && navMesh1->m_exposure)
		{
			exposedLength += navMesh1->m_exposure->IntegrateAvd(pt0Ws, pt1Ws);
		}
	}

	return exposedLength;
}

/// --------------------------------------------------------------------------------------------------------------- ///
F32 ExposureMapManager::CalculatePathExposure(const PathWaypointsEx* pPath,
											  ExposureMapType type,
											  DC::ExposureSource src,
											  F32 distancePenalty) const
{
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());
	NAV_ASSERT(m_state.m_exposureMapLock.IsLockedForRead());

	if (!pPath || !pPath->IsValid())
	{
		return NDI_FLT_MAX;
	}

	F32 exposedLength = 0.0f;
	F32 totalLength = 0.0f;

	for (U32 iWaypoint = 1; iWaypoint < pPath->GetWaypointCount(); ++iWaypoint)
	{
		const Point pt0Ws = pPath->GetWaypoint(iWaypoint - 1);
		const Point pt1Ws = pPath->GetWaypoint(iWaypoint);

		const NavMesh* navMesh0 = NavMeshHandle(pPath->GetNavId(iWaypoint - 1)).ToNavMesh();
		const NavMesh* navMesh1 = NavMeshHandle(pPath->GetNavId(iWaypoint)).ToNavMesh();

		if (navMesh0 && navMesh0->m_exposure)
		{
			exposedLength += (1.0f + totalLength * distancePenalty) * navMesh0->m_exposure->IntegrateExp(type, src, pt0Ws, pt1Ws);
		}

		if (navMesh1 && navMesh0 != navMesh1 && navMesh1->m_exposure)
		{
			exposedLength += (1.0f + totalLength * distancePenalty) * navMesh1->m_exposure->IntegrateExp(type, src, pt0Ws, pt1Ws);
		}

		totalLength += Dist(pt0Ws, pt1Ws);
	}

	return exposedLength;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsCellOnNavMesh(const U64* __restrict const pOnNavMesh, U32 pitch, I32 iX, I32 iZ)
{
	const U32 idx = iZ * pitch + iX;
	return (pOnNavMesh[idx >> 6] >> (idx & 63)) & 1;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool FindNearestValidExposureCell(const NavMesh* pMesh,
										 const Point posWs,
										 I32& x,
										 I32& z)
{
	if (!pMesh || !pMesh->m_exposure)
	{
		return false;
	}

	const NavMeshHeightMap* pHeightMap = pMesh->GetHeightMap();

	if (!pHeightMap)
	{
		return false;
	}

	const U32 pitch = pHeightMap->m_bitmapPitch;
	const U64* __restrict const pOnNavMesh = pHeightMap->m_onNavMesh;

	bool isPointExposed = false;

	{
		const Vector idealCoords = (posWs - pMesh->m_exposure->m_originWs) * kInvHeightMapGridSpacing;

		I32 iX = FastCvtToIntFloor(idealCoords.X());
		I32 iZ = FastCvtToIntFloor(idealCoords.Z());

		// instead of sorting candidates, just iterate through all and keep the best one
		float bestDist = NDI_FLT_MAX;
		I32 bestX = -1, bestZ = -1;
		for (I32 iCandidateX = -1; iCandidateX <= 1; ++iCandidateX)
		{
			for (I32 iCandidateZ = -1; iCandidateZ <= 1; ++iCandidateZ)
			{
				const U32 pointX = iX + iCandidateX;
				const U32 pointZ = iZ + iCandidateZ;
				const float fPointX = ((F32)pointX + 0.5f);
				const float fPointZ = ((F32)pointZ + 0.5f);
				if ((U32)(pointX) < pHeightMap->m_sizeX && (U32)(pointZ) < pHeightMap->m_sizeZ && IsCellOnNavMesh(pOnNavMesh, pitch, pointX, pointZ))
				{
					const F32 dx = idealCoords.X() - fPointX;
					const F32 dz = idealCoords.Z() - fPointZ;
					const float dist = dx * dx + dz * dz;
					if (dist < bestDist)
					{
						bestX = pointX;
						bestZ = pointZ;
						bestDist = dist;
					}
				}
			}
		}

		if (bestDist == NDI_FLT_MAX)
			return false;

		x = bestX;
		z = bestZ;
		return true;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ExposureMapManager::GetNavLocationExposed(NavLocation navLocation,
											   ExposureMapType type,
											   DC::ExposureSource src) const
{
	NAV_ASSERT(m_state.m_exposureMapLock.IsLockedForRead());
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	const NavMesh* pMesh = navLocation.ToNavMesh();

	I32 x, z;
	if (FindNearestValidExposureCell(pMesh, navLocation.GetPosWs(), x, z))
	{
		return pMesh->m_exposure->IsExposed(type, src, x, z);
	}
	else
	{
		// if we couldn't obtain valid exposure information, say we're exposed to be safe
		return true;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ExposureMapManager::NavLocationQuery(NavLocation navLocation, ExposureMapType type, DC::ExposureSource src, bool& exp, bool& avd, bool& tht) const
{
	NAV_ASSERT(m_state.m_exposureMapLock.IsLockedForRead());
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	const NavMesh* pMesh = navLocation.ToNavMesh();

	I32 x, z;
	if (FindNearestValidExposureCell(pMesh, navLocation.GetPosWs(), x, z))
	{
		const AiExposureData* pData = pMesh->m_exposure;
		exp = pData->IsExposed(type, src, x, z);
		avd = pData->IsAvd(x, z);
		tht = pData->IsTht(x, z);
	}
	else
	{
		// if we couldn't obtain valid exposure information, say we're exposed to be safe
		exp = true;
		avd = false;
		tht = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ExposureMapManager::NavLocationQuery(NavLocation navLocation, ExposureMapType type1, DC::ExposureSource src1, ExposureMapType type2, DC::ExposureSource src2, bool& exp1, bool& exp2, bool& avd, bool& tht) const
{
	NAV_ASSERT(m_state.m_exposureMapLock.IsLockedForRead());
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	const NavMesh* pMesh = navLocation.ToNavMesh();

	I32 x, z;
	if (FindNearestValidExposureCell(pMesh, navLocation.GetPosWs(), x, z))
	{
		const AiExposureData* pData = pMesh->m_exposure;
		exp1 = pData->IsExposed(type1, src1, x, z);
		exp2 = pData->IsExposed(type2, src2, x, z);
		avd = pData->IsAvd(x, z);
		tht = pData->IsTht(x, z);
	}
	else
	{
		// if we couldn't obtain valid exposure information, say we're exposed to be safe
		exp1 = true;
		exp2 = true;
		avd = false;
		tht = false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ExposureMapManager::GetNavLocationAvd(NavLocation navLocation) const
{
	NAV_ASSERT(m_state.m_exposureMapLock.IsLockedForRead());
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	const NavMesh* pMesh = navLocation.ToNavMesh();

	I32 x, z;
	if (FindNearestValidExposureCell(pMesh, navLocation.GetPosWs(), x, z))
	{
		return pMesh->m_exposure->IsAvd(x, z);
	}
	else
	{
		// if we couldn't obtain valid avoid information, say we're not avoid
		return false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ExposureMapManager::GetNavLocationTht(NavLocation navLocation) const
{
	NAV_ASSERT(m_state.m_exposureMapLock.IsLockedForRead());
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	const NavMesh* pMesh = navLocation.ToNavMesh();

	I32 x, z;
	if (FindNearestValidExposureCell(pMesh, navLocation.GetPosWs(), x, z))
	{
		return pMesh->m_exposure->IsTht(x, z);
	}
	else
	{
		// if we couldn't obtain valid tht information, say we're not tht
		return false;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ExposureMapManager::GetNavLocationOrNeighboringTht(NavLocation navLocation) const
{
	NAV_ASSERT(m_state.m_exposureMapLock.IsLockedForRead());
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	const Point posWs = navLocation.GetPosWs();
	const NavMesh* pMesh = navLocation.ToNavMesh();

	if (!pMesh || !pMesh->m_exposure)
		return false;

	const NavMeshHeightMap* pHeightMap = pMesh->GetHeightMap();

	if (!pHeightMap)
		return false;

	const U32 pitch = pHeightMap->m_bitmapPitch;
	const U64* __restrict const pOnNavMesh = pHeightMap->m_onNavMesh;

	const Vector idealCoords = (posWs - pMesh->m_exposure->m_originWs) * kInvHeightMapGridSpacing;

	I32 iX = FastCvtToIntFloor(idealCoords.X());
	I32 iZ = FastCvtToIntFloor(idealCoords.Z());

	for (I32 iCandidateX = -1; iCandidateX <= 1; ++iCandidateX)
	{
		for (I32 iCandidateZ = -1; iCandidateZ <= 1; ++iCandidateZ)
		{
			const U32 pointX = iX + iCandidateX;
			const U32 pointZ = iZ + iCandidateZ;
			if ((U32)(pointX) < pHeightMap->m_sizeX && (U32)(pointZ) < pHeightMap->m_sizeZ && IsCellOnNavMesh(pOnNavMesh, pitch, pointX, pointZ))
			{
				if (pMesh->m_exposure->IsTht(pointX, pointZ))
					return true;
			}
		}
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ExposureMapManager::GetNavLocationExposed(NavLocation navLocation,
											   DC::ExposureSource src,
											   bool& isExposedWithStealthOccluded,
											   bool& isExposedWithStealthExposed) const
{
	NAV_ASSERT(m_state.m_exposureMapLock.IsLockedForRead());
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	const NavMesh* pMesh = navLocation.ToNavMesh();

	I32 x, z;
	if (FindNearestValidExposureCell(pMesh, navLocation.GetPosWs(), x, z))
	{
		isExposedWithStealthOccluded = pMesh->m_exposure->IsExposed(ExposureMapType::kStealthGrassAlwaysUnexposed, src, x, z);
		isExposedWithStealthExposed  = pMesh->m_exposure->IsExposed(ExposureMapType::kStealthGrassNormal, src, x, z);

	}
	else
	{
		// if we couldn't obtain valid exposure information, say we're exposed to be safe
		isExposedWithStealthOccluded = true;
		isExposedWithStealthExposed	 = true;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ExposureMapManager::IsAnythingNearbyExposed(NavLocation navLocation, DC::ExposureSource src) const
{
	NAV_ASSERT(m_state.m_exposureMapLock.IsLockedForRead());
	NAV_ASSERT(NavMeshMgr::GetGlobalLock()->IsLockedForRead());

	const NavMesh* pMesh = navLocation.ToNavMesh();
	if (!pMesh)
		return false;

	const AiExposureData* pData = pMesh->m_exposure;
	if (!pData)
		return false;

	const NavMeshHeightMap* pHeightMap = pMesh->GetHeightMap();
	if (!pHeightMap)
		return false;

	Point posWs = navLocation.GetPosWs();
	const Vector idealCoords = (posWs - pData->m_originWs) * kInvHeightMapGridSpacing;
	const I32 iX = FastCvtToIntFloor(idealCoords.X());
	const I32 iZ = FastCvtToIntFloor(idealCoords.Z());

	const U64* __restrict const pMap = pData->GetMap(ExposureMapType::kStealthGrassAlwaysUnexposed, src);

	CONST_EXPR I32 NUM_CELLS = 12;
	for (I32 dz = -NUM_CELLS; dz <= NUM_CELLS; ++dz)
	{
		for (I32 dx = -NUM_CELLS; dx <= NUM_CELLS; ++dx)
		{
			if (dx * dx + dz * dz < NUM_CELLS * NUM_CELLS)
			{
				const U32 x = iX + dx;
				const U32 z = iZ + dz;
				if (x < pHeightMap->m_sizeX && z < pHeightMap->m_sizeZ)
				{
					if (pData->IsExposed(pMap, x, z))
						return true;
				}
			}
		}
	}

	return false;
}

ExposureMapManager g_exposureMapMgr;
