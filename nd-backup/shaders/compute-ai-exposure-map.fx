/*
* Copyright (c) 2018 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited.
*/

#include "exposure-map-shared-cpu-gpu-constants.h"

struct SrtData
{
	Texture2D<float>			m_heightMaps[kExposureMapMaxHeightMaps];
	int							m_hMapXOffsets[kExposureMapMaxHeightMaps];
	float						m_hMapYOffsets[kExposureMapMaxHeightMaps];
	int							m_hMapZOffsets[kExposureMapMaxHeightMaps];
};

struct Srt_ComputeAiExposureMap
{
	SrtData*					m_pData;

	RW_Texture2D<uint>			m_outputBuffer;
	Texture2D<float>			m_testHeightMap;

	int							m_obsX;
	float						m_obsY;
	int							m_obsZ;

	float						m_forwardX;
	float						m_forwardZ;

	float						m_fovFactorA;
	float						m_fovFactorB;
	float						m_fovFactorC;
	float						m_fovFactorD;

	int							m_startX;
	int							m_startZ;

	int							m_runX;
	int							m_runZ;
};

[numthreads(kExposureMapThreadsPerGroup, 1, 1)]
void Cs_ComputeAiExposureMap(Srt_ComputeAiExposureMap* srt : S_SRT_DATA, uint2 threadID : S_DISPATCH_THREAD_ID)
{
	// origin cell coordinates (x0, z0) on test map
	int x0 = srt->m_obsX;
	int z0 = srt->m_obsZ;

	// branchlessly extract cell deltas (dx, dz) from observer to destination cell
	// on the perimeter of the test rectangle, in test map space, from linear index (threadID.x)
	int idx = threadID.x;

	int dx, dz;
	{
		dx = min(srt->m_runX, idx) - x0 + srt->m_startX;
		idx -= min(srt->m_runX, idx);

		dz = min(srt->m_runZ, idx) - z0 + srt->m_startZ;
		idx -= min(srt->m_runZ, idx);

		dx -= min(srt->m_runX, idx);
		idx -= min(srt->m_runX, idx);

		dz -= idx;
	}

	// square of termination distance due to reaching edge of test rectangle
	float radSqr = (float)__v_mad_i32_i24(dx, dx, __v_mul_i32_i24(dz, dz));

	// bail if threadID.x larger than workload or if already at destination
	if (idx >= srt->m_runZ || !radSqr) return;

	const float cosTheta = (srt->m_forwardX * dx + srt->m_forwardZ * dz) * __v_rsq_f32(radSqr);

	// voxel traversal (except it's 2D, so... cover line?) error
	int err = abs(dx) - abs(dz);

	dx <<= 1;
	dz <<= 1;

	// terminate at edge of the visual field, if that comes first
	{
		// polar visual field model
		// https://www.desmos.com/calculator/mfwg3kcls4

		const float rad1 = srt->m_fovFactorA / (1.0f - srt->m_fovFactorB * cosTheta);
		if (rad1 > 0.0f)
		{
			radSqr = min(radSqr, rad1*rad1);
		}

		const float rad2 = srt->m_fovFactorC * __v_rsq_f32(1.0f - srt->m_fovFactorD * cosTheta);
		radSqr = min(radSqr, rad2*rad2);
	}

	// occluder heightmap index
	const uint m = threadID.y;

	// march until we enter the occluder heightmap
	{
		// dimensions of occluder heightmap
		uint occSizeX, occSizeZ;
		srt->m_pData->m_heightMaps[m].GetDimensionsFast(occSizeX, occSizeZ);

		// while outside occluder heightmap
		while (uint(x0 - srt->m_pData->m_hMapXOffsets[m]) >= occSizeX || uint(z0 - srt->m_pData->m_hMapZOffsets[m]) >= occSizeZ)
		{
			// write "exposed!" to test heightmap's slot for cell (x0, z0)
			AtomicOr(srt->m_outputBuffer[int2(x0, z0)], 1U << m);

			// squared distance from observer
			const float dSqr = (float)__v_mad_i32_i24(x0 - srt->m_obsX, x0 - srt->m_obsX, __v_mul_i32_i24(z0 - srt->m_obsZ, z0 - srt->m_obsZ));

			// traversal advance
			if (err > 0)
			{
				err -= abs(dz);
				x0 += med3(-1, dx, 1);
			}
			else
			{
				err += abs(dx);
				z0 += med3(-1, dz, 1);
			}

			// if outside radius or at destination
			if (dSqr >= radSqr) return;
		}
	}

	// lower shadow caster slopes; 2 shadowcasters tracked
	float2 shadowSlopesL = { 1.0f / 0.0f, 1.0f / 0.0f };

	// upper shadow caster slopes; 2 shadowcasters tracked
	float2 shadowSlopesU = { -1.0f / 0.0f, -1.0f / 0.0f };

	// previous cell's height (initialized to "hole")
	float prevOccHeight = srt->m_pData->m_hMapYOffsets[m];

	// which caster we're currently tracking
	// first caster we encounter will be a new caster, which will increment this to 0
	int iCaster = -1;

	for (;;)
	{
		// current cell is (x0, z0) on test map

		// occluder heightmap height (or 0 for "hole"), plus srt->m_pData->m_hMapYOffsets[m] to shift into test heightmap space
		const float occHeight = srt->m_pData->m_heightMaps[m][int2(x0 - srt->m_pData->m_hMapXOffsets[m], z0 - srt->m_pData->m_hMapZOffsets[m])] + srt->m_pData->m_hMapYOffsets[m];

		// test heightmap height (or 0 for "hole")
		const float testHeight = srt->m_testHeightMap[int2(x0, z0)];

		// squared distance from observer
		const float dSqr = (float)__v_mad_i32_i24(x0 - srt->m_obsX, x0 - srt->m_obsX, __v_mul_i32_i24(z0 - srt->m_obsZ, z0 - srt->m_obsZ));

		// inverse distance from observer
		const float dInv = __v_rsq_f32(dSqr);

		// if current cell is not a hole
		if (occHeight != srt->m_pData->m_hMapYOffsets[m])
		{
			// if the previous cell was a hole, this is the start of a new shadowcaster...
			if (prevOccHeight == srt->m_pData->m_hMapYOffsets[m])
			{
				// ...so advance the caster if it's the first; otherwise, replace the current one
				// (in this way we keep the FIRST and LAST shadowcasters seen, which produces
				// superior results compared to keeping the first two or the last two)
				iCaster = min(1, iCaster + 1);
				shadowSlopesL[1] = 1.0f / 0.0f;
				shadowSlopesU[1] = -1.0f / 0.0f;
			}

			// slope to this cell
			const float newSlope = (occHeight - srt->m_obsY) * dInv;

			// if starting a new caster or new low-water-mark for this caster, update lower slope accordingly
			if (newSlope < shadowSlopesL[iCaster]) shadowSlopesL[iCaster] = newSlope;

			// if starting a new caster or new high-water-mark for this caster, update upper slope accordingly
			if (newSlope > shadowSlopesU[iCaster]) shadowSlopesU[iCaster] = newSlope;
		}

		// set previous height to current height since we're about to advance to the next cell
		prevOccHeight = occHeight;

		// test slope, plus a variable amount to represent the height of a crouching target;
		// we cheat and keep it closer to the ground when near the observer so that an observer
		// standing right up against low cover cannot see a crouching target on the opposite side
		// of the cover, even though in reality they're totally visible
		const float crouchSlope = (testHeight - srt->m_obsY + med3(kExposureMapTuningMinCrouchHeight, kExposureMapTuningMaxCrouchHeight, (dSqr + kExposureMapTuningAddToSquaredDistance) / kExposureMapTuningDivideSquaredDistance) / 255.0f) * dInv;

		// if test heightmap doesn't have a hole and crouch height is not inside the shadow cone of either shadowcaster
		if (testHeight &&
			min(max(shadowSlopesL[0], crouchSlope), shadowSlopesU[0]) != crouchSlope &&
			min(max(shadowSlopesL[1], crouchSlope), shadowSlopesU[1]) != crouchSlope)
		{
			// write "exposed!" to test heightmap's slot for cell (x0, z0)
			AtomicOr(srt->m_outputBuffer[int2(x0, z0)], 1U << m);
		}

		// advance to next cell
		if (err > 0)
		{
			err -= abs(dz);
			x0 += med3(-1, dx, 1);
		}
		else
		{
			err += abs(dx);
			z0 += med3(-1, dz, 1);
		}
		// if outside radius or at destination
		if (dSqr >= radSqr) return;
	}
}
