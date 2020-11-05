/*
* Copyright (c) 2014 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "packing.fxi"
#include "global-funcs.fxi"
#include "post-globals.fxi"
#include "global-const-buffers.fxi"
#include "tile-util.fxi"
#include "atomics.fxi"
#include "particle-cs.fxi"

#include "particle-ray-trace-cs-defines.fxi"




[NUM_THREADS(64, 1, 1)]
void CS_ParticlePreCompute(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleRayTraceSrt *pSrt : S_SRT_DATA)
{
	int iPart = dispatchId.x;

	if (iPart < pSrt->m_numParts)
	{
		ParticleInstance inst = pSrt->m_particleInstances[pSrt->m_particleIndicesOrig[ pSrt->m_numParts - iPart - 1]];

		// check if this whole block intersects this particle
		float4 p0 = float4(-0.5, 0.5, 0, 1);
		float4 p1 = float4(0.5, 0.5, 0, 1);
		float4 p2 = float4(0.5, -0.5, 0, 1);
		float4 p3 = float4(-0.5, -0.5, 0, 1);



		float2 p0uv = float2(0, 1);
		float2 p1uv = float2(1, 1);
		float2 p2uv = float2(1, 0);
		float2 p3uv = float2(0, 0);

		p0uv = p0uv * inst.texcoord.xy + inst.texcoord.zw;
		p1uv = p1uv * inst.texcoord.xy + inst.texcoord.zw;
		p2uv = p2uv * inst.texcoord.xy + inst.texcoord.zw;
		p3uv = p3uv * inst.texcoord.xy + inst.texcoord.zw;

		p0 = float4((mul(p0, inst.world).xyz + pSrt->m_altWorldOrigin), 1.0);
		p1 = float4((mul(p1, inst.world).xyz + pSrt->m_altWorldOrigin), 1.0);
		p2 = float4((mul(p2, inst.world).xyz + pSrt->m_altWorldOrigin), 1.0);
		p3 = float4((mul(p3, inst.world).xyz + pSrt->m_altWorldOrigin), 1.0);


		//p0 = float4((inst.world[3].xyz + pSrt->m_altWorldOrigin), 1.0);
		//p1 = float4((inst.world[3].xyz + pSrt->m_altWorldOrigin), 1.0);
		//p2 = float4((inst.world[3].xyz + pSrt->m_altWorldOrigin), 1.0);
		//p3 = float4((inst.world[3].xyz + pSrt->m_altWorldOrigin), 1.0);


		p0 = mul(p0, (pSrt->g_mVP));
		p1 = mul(p1, (pSrt->g_mVP));
		p2 = mul(p2, (pSrt->g_mVP));
		p3 = mul(p3, (pSrt->g_mVP));

		float halfResW = PRECOMPUTE_RESOLUTION_W_F / 2;
		float halfResH = PRECOMPUTE_RESOLUTION_H_F / 2;

		p0.x = p0.x / p0.w * halfResW + halfResW;
		p0.y = -p0.y / p0.w * halfResH + halfResH;
		p1.x = p1.x / p1.w * halfResW + halfResW;
		p1.y = -p1.y / p1.w * halfResH + halfResH;
		p2.x = p2.x / p2.w * halfResW + halfResW;
		p2.y = -p2.y / p2.w * halfResH + halfResH;
		p3.x = p3.x / p3.w * halfResW + halfResW;
		p3.y = -p3.y / p3.w * halfResH + halfResH;

		p0.z = p0.z / p0.w;
		p1.z = p1.z / p1.w;
		p2.z = p2.z / p2.w;
		p3.z = p3.z / p3.w;

		float4 p = float4(0.0, 0.0, 0, 1);
		p = float4((mul(p, inst.world).xyz + pSrt->m_altWorldOrigin), 1.0);
		p = mul(p, (pSrt->g_mVP));


		p.x = p.x / p.w * halfResW + halfResW;
		p.y = -p.y / p.w * halfResH + halfResH;




		PartQuadControl ctrl;

		// find barycentric coordinates for each corner of the screen
		float2 pos = float2(0, 0);
		float4 screen00_bc012 = FindBarycentric(p0, p1, p2, float2(0, 0));
		float2 screen00_uv = p0uv * screen00_bc012.x + p1uv * screen00_bc012.y + p2uv * screen00_bc012.z;
		float screen00_d = p0.z * screen00_bc012.x + p1.z * screen00_bc012.y + p2.z * screen00_bc012.z;
		float2 uv00 = screen00_uv;
		float depth00 = screen00_d;

		pos = float2(PRECOMPUTE_RESOLUTION_W_F - 1, 0);
		screen00_bc012 = FindBarycentric(p0, p1, p2, pos.xy);
		screen00_uv = p0uv * screen00_bc012.x + p1uv * screen00_bc012.y + p2uv * screen00_bc012.z;
		screen00_d = p0.z * screen00_bc012.x + p1.z * screen00_bc012.y + p2.z * screen00_bc012.z;

		float2 uv10 = screen00_uv - uv00;
		float depth10 = screen00_d - depth00;


		pos = float2(0, PRECOMPUTE_RESOLUTION_H_F - 1);
		screen00_bc012 = FindBarycentric(p0, p1, p2, pos.xy);
		screen00_uv = p0uv * screen00_bc012.x + p1uv * screen00_bc012.y + p2uv * screen00_bc012.z;
		screen00_d = p0.z * screen00_bc012.x + p1.z * screen00_bc012.y + p2.z * screen00_bc012.z;

		float2 uv01 = screen00_uv - uv00;
		float depth01 = screen00_d - depth00;

		// premultiply by inverse of screen size
		float2 factor2 = 1.0 / float2(PRECOMPUTE_RESOLUTION_W_F, PRECOMPUTE_RESOLUTION_H_F);
		uv10 = uv10 * factor2.x;
		uv01 = uv01 * factor2.y;
		depth10 = depth10 * factor2.x;
		depth01 = depth01 * factor2.y;

#if PACK_POS_SS
		ctrl.packedPosSsxy = PackFloat2ToUInt(p.x, p.y);
#else
		ctrl.posSsxy = p.xy;
#endif
		ctrl.scaleXAbs = 2.0f / abs(inst.invscale.x);

#if PACK_GRADIENT
		ctrl.packedGradient = PackFloat2ToUInt(float3(uv00, uv10.x), float3(uv10.y, uv01));
#else
		ctrl.uv00 = uv00;
		ctrl.uv10 = uv10;
		ctrl.uv01 = uv01;
#endif

#if CONSTANT_DEPTH
		ctrl.constantDepth = depth00;
#else

#if PACK_DEPTH
		ctrl.packedDepthGradient = PackFloat2ToUInt(float2(depth00, depth10), float2(depth01, 0));
#else
		ctrl.depth00 = depth00;
		ctrl.depth10 = depth10;
		ctrl.depth01 = depth01;
#endif
#endif

#if PACK_COLOR
		ctrl.packedColor = PackFloat2ToUInt(inst.color.xy, inst.color.zw);
#endif

		pSrt->m_particleQuadControls[iPart] = ctrl;

		pSrt->m_particleQuadControls[iPart + pSrt->m_numParts] = ctrl;
	}
}

groupshared uint g_sharedBits[8 * 64];

#if DoFroxelsInLowResPass
[NUM_THREADS(64, 1, 1)] // x : particle index, y is
void CS_ParticlePreComputeIntersectionsForFroxels(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleRayTraceSrt *pSrt : S_SRT_DATA)
#else
[NUM_THREADS(64, 1, 1)] // x : particle index, y is
void CS_ParticlePreComputeIntersections0(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleRayTraceSrt *pSrt : S_SRT_DATA)
#endif
{
	// positions are in half resolution. we multiply by 2 because each occupancy cell has 2x2 froxels or 8x8 tiles for rendering
#if DoFroxelsInLowResPass
	// each tile is 8x8 froxels
	uint2 pos = (pSrt->m_posOffset / 2) + groupId * uint2(FogFroxelGridSizeNativeRes / 2 * 8 * 2, FogFroxelGridSizeNativeRes / 2 * 8); // we process 2 tiles at the same time in x
	int2 pos1 = pos + uint2(FogFroxelGridSizeNativeRes / 2 * 8, 0); // second tile
#else
	// TODO: the numbers here should be using macro definitons like WAFEFRONT_QUAD_W_H or OCCUPANCY_CELL_W
	uint2 pos = (pSrt->m_posOffset / 2) + groupId * uint2(16 * 2, 16); // the reason it is 16*2 is because we are processing two tiles at the same time
	int2 pos1 = pos + uint2(16, 0); // second tile
#endif

	// each thread checks one particle for this group. each group corresponds to 2d block on screen

	// preliminary test..
	int numTests = (pSrt->m_numParts + 63) / 64;
	int myIndex = groupThreadId.x;

	ParticleQuadOccupancy combinedResults;
	ParticleQuadOccupancy combinedResults1;
	bool anyIntersected = false;
	bool anyIntersected1 = false;
	int numIntersected = 0;
	int numIntersected1 = 0;
	for (int iTest = 0; iTest < numTests; ++iTest)
	{
		bool intersecting = false;
		bool intersecting1 = false;
		
		int iPart = iTest * 64 + myIndex;
		if (iPart < pSrt->m_numParts)
		{
			//ParticleInstance inst = pSrt->m_particleInstances[pSrt->m_numParts - iPart - 1];

#if usePrecomputeData

			PartQuadControl _ctrl = pSrt->m_particleQuadControlsRO[iPart];

#if PACK_GRADIENT
			float2 uv00 = float2(f16tof32(_ctrl.packedGradient.x), f16tof32(_ctrl.packedGradient.y));
			float2 uv10 = float2(f16tof32(_ctrl.packedGradient.z), f16tof32(_ctrl.packedGradient.x >> 16));
			float2 uv01 = float2(f16tof32(_ctrl.packedGradient.y >> 16), f16tof32(_ctrl.packedGradient.z >> 16));
#else
			float2 uv00 = _ctrl.uv00;
			float2 uv10 = _ctrl.uv10;
			float2 uv01 = _ctrl.uv01;
#endif
#if DoFroxelsInLowResPass
			uint2 posMax = pos + uint2(FogFroxelGridSizeNativeRes / 2 * 8, FogFroxelGridSizeNativeRes / 2 * 8);
#else
			uint2 posMax = pos + uint2(OCCUPANCY_CELL_W_NATIVE_RES /2, OCCUPANCY_CELL_W_NATIVE_RES /2); // TODO: why are we divind by 2 here? shouldnt it be just the width and height? these values seem to be in native resolution
#endif
			{

#if PACK_POS_SS
				float2 posSsxy = float2(f16tof32(_ctrl.packedPosSsxy), f16tof32(_ctrl.packedPosSsxy >> 16));
				float2 partCenter = posSsxy;
#else
				float2 partCenter = _ctrl.posSsxy;
#endif
				float2 closestPt = max(min(partCenter, posMax), pos);

				float2 uv = uv00 + uv10 * closestPt.x + uv01 * closestPt.y; // uv of wavefront center
				float2 toCenterOfParticle = float2(0.5f, 0.5f) - uv;

				float d2 = toCenterOfParticle.x * toCenterOfParticle.x + toCenterOfParticle.y * toCenterOfParticle.y;

				if (d2 < 0.25)
				{
					intersecting = true;

#if USE_READLANES_FOR_DEPTH_GRAD
					testCtrl.depth00 = _ctrl.depth00;
					testCtrl.depth10 = _ctrl.depth10;
					testCtrl.depth01 = _ctrl.depth01;
#endif
#if USE_READLANES_FOR_PART_INST
					inst = pSrt->m_particleInstances[pSrt->m_numParts - iTestPart - 1];
#endif
				}


				// test next one
#if DoFroxelsInLowResPass
				posMax = pos1 + uint2(FogFroxelGridSizeNativeRes / 2 * 8, FogFroxelGridSizeNativeRes / 2 * 8);
#else
				posMax = pos1 + uint2(OCCUPANCY_CELL_W_NATIVE_RES / 2, OCCUPANCY_CELL_H_NATIVE_RES / 2);
#endif
				closestPt = max(min(partCenter, posMax), pos1);
				uv = uv00 + uv10 * closestPt.x + uv01 * closestPt.y; // uv of wavefront center
				toCenterOfParticle = float2(0.5f, 0.5f) - uv;
				d2 = toCenterOfParticle.x * toCenterOfParticle.x + toCenterOfParticle.y * toCenterOfParticle.y;

				if (d2 < 0.25)
				{
					intersecting1 = true;
				}

				//pSrt->m_destTexture0[int2(factor2)] = float4(sign(toCenterOfParticleSS) / 2.0 + 0.5f, 0, 0);
			}

			//float4 screen00_bc012 = FindBarycentric(p0, p1, p2, float2(0, 0) /* pos.xy */);
			//float2 screen00_uv = p0uv * screen00_bc012.x + p1uv * screen00_bc012.y + p2uv * screen00_bc012.z;
			//uv = screen00_uv;

#if EACH_PARTICLE_FULL_SCREEN
			intersecting = true;
			intersecting1 = true;
#endif

#endif
		}

		ulong accumulatedResults = __v_cmp_eq_u32(intersecting, true);
		ulong accumulatedResults1 = __v_cmp_eq_u32(intersecting1, true);

		numIntersected += __s_bcnt1_i32_b64(accumulatedResults);
		numIntersected1 += __s_bcnt1_i32_b64(accumulatedResults1);

		if (accumulatedResults > 0)
			anyIntersected = true;
		
		if (accumulatedResults1 > 0)
			anyIntersected1 = true;

		//if (myIndex == 0)
		{
			combinedResults.occupancy[iTest * 2 + 0] = uint(accumulatedResults);
			combinedResults.occupancy[iTest * 2 + 1] = uint(__s_lshr_b64(accumulatedResults, 32));


			combinedResults1.occupancy[iTest * 2 + 0] = uint(accumulatedResults1);
			combinedResults1.occupancy[iTest * 2 + 1] = uint(__s_lshr_b64(accumulatedResults1, 32));

			//combinedResults.m_occupancy64[iTest] = accumulatedResults;
			//combinedResults.m_occupancy64[iTest] = (accumulatedResults >> 32) | (accumulatedResults << 32);
			
			
		}
	} // for itest

	if (myIndex == 0 && anyIntersected)
	{
		//combinedResults.m_locationX = pos.x;
		//combinedResults.m_locationY = pos.y;

		combinedResults.m_locationXY = pos.x | (pos.y << 16);
		combinedResults.m_resDropIndex = 255; // unreachable


#if SORT_INTERSECTIONS_INTO_BUCKETS
		if (numIntersected > 32)
		{
			uint hitIndex = NdAtomicIncrement(pSrt->m_gdsOffset_1);
			pSrt->m_particleOccupancy_1[hitIndex] = combinedResults;
		}
		else// if (numIntersected > 16)
		{
			uint hitIndex = NdAtomicIncrement(pSrt->m_gdsOffset_2);
			pSrt->m_particleOccupancy_2[hitIndex] = combinedResults;
		}
		//else
		//{
		//	uint hitIndex = NdAtomicIncrement(pSrt->m_gdsOffset_2);
		//	pSrt->m_particleOccupancy_2[hitIndex] = combinedResults;
		//}
#else
		{
			uint hitIndex = NdAtomicIncrement(pSrt->m_gdsOffset_0);
			pSrt->m_particleOccupancy_0[hitIndex] = combinedResults;
		}
#endif
	}

	if (myIndex == 0 && anyIntersected1)
	{
		
		combinedResults1.m_locationXY = pos1.x | (pos1.y << 16);
		combinedResults1.m_resDropIndex = 255; // unreachable

#if SORT_INTERSECTIONS_INTO_BUCKETS
		if (numIntersected > 32)
		{
			uint hitIndex = NdAtomicIncrement(pSrt->m_gdsOffset_1);
			pSrt->m_particleOccupancy_1[hitIndex] = combinedResults1;
		}
		else// if (numIntersected > 16)
		{
			uint hitIndex = NdAtomicIncrement(pSrt->m_gdsOffset_2);
			pSrt->m_particleOccupancy_2[hitIndex] = combinedResults1;
		}
		//else
		//{
		//	uint hitIndex = NdAtomicIncrement(pSrt->m_gdsOffset_2);
		//	pSrt->m_particleOccupancy_2[hitIndex] = combinedResults1;
		//}
#else
		{
			uint hitIndex = NdAtomicIncrement(pSrt->m_gdsOffset_0);
			pSrt->m_particleOccupancy_0[hitIndex] = combinedResults1;
		}
#endif
	}
	//if (numIntersected == 0)
	//	pSrt->m_destTexture0[pos - uint2(2, 2)] = float4(0, 1, 1, 0.0);
	//else
	//	pSrt->m_destTexture0[pos - uint2(2, 2)] = float4(0, 0, numIntersected / 32.0, 0.0);
}




[NUM_THREADS(16, 16, 1)] // x : particle index, y is
void CS_UpscaleRayParticles(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleRayTraceReadOnlySrt *pSrt : S_SRT_DATA)
{
	int dispatchIndex = groupId.x;

	ParticleQuadOccupancyUlong myOcc = pSrt->m_particleOccupancy[dispatchIndex];
	
	uint2 groupPos = uint2(myOcc.m_locationXY & 0x0000FFFF, (myOcc.m_locationXY >> 16));

	//uint2 groupPos = groupId * GROUP_SAMPLE_WIDTH_HEIGHT;
	
	uint2 posHalfRes = groupPos + groupThreadId.xy;

	uint2 posIn16x16 = uint2(posHalfRes.x % 16, posHalfRes.y % 16);

	float2 uvQuarterRes = posHalfRes / float2(1920.0f / 2, 1080.0f / 2);

	float2 quarterResHalfPixel = float2(0.5f, 0.5f) / float2(1920.0f / 4, 1080.0f / 4);

	float2 uvQuarterResAdjScaled = uvQuarterRes + quarterResHalfPixel - (quarterResHalfPixel) * float2(posIn16x16.xy / float2(15, 15));

	float2 uvQuarterResAdj = uvQuarterRes + quarterResHalfPixel;

	float4 lowerResC00 = pSrt->m_srcTextureLowerRes.SampleLevel(pSrt->m_linearSampler, uvQuarterResAdjScaled, 0).rgba;
	//float4 lowerResC00 = pSrt->m_srcTextureLowerRes.SampleLevel(pSrt->m_linearSampler, uvQuarterResAdj, 0).rgba;

	

	float4 c00 = pSrt->m_destTexture0[posHalfRes];

	ForwardBlendForwardBlendedColor(c00, lowerResC00, pSrt);

#if USE_DEBUG_CHECKS
	if (pSrt->m_flags & FLAG_RENDER_STATS)
	{
		c00.w = 0;
	}
	
#endif


	//pSrt->m_destTextureHighRes[posHalfRes] = lowerResC00;
	//pSrt->m_destTextureHighRes[posHalfRes] = c00;

	pSrt->m_destTexture0[posHalfRes] = c00;
	//pSrt->m_destTexture0[posHalfRes] = lowerResC00;

#if 0
	//float2 fPos = (groupPos + uint2(GROUP_SAMPLE_WIDTH_HEIGHT-1, GROUP_SAMPLE_WIDTH_HEIGHT-1) * resolution) / float2(1920.0f, 1080.0f);// + float2(0.5f, 0.5f) / float2(1920.0f / 4, 1080.0f / 4);
	float2 fGroupPos = groupPos / float2(1920.0f, 1080.0f);// +float2(0.5f, 0.5f) / float2(1920.0f / 4, 1080.0f / 4);

	//float2 fPos = groupPos / float2(1920.0f, 1080.0f) + float2(0.5f, 0.5f) / float2(1920.0f / 4, 1080.0f / 4);

	float2 lowestResHalfPixel = float2(0.5f, 0.5f) / float2(1920.0f / 4, 1080.0f / 4);
	float2 lowestResQuarterPixel = float2(0.5f, 0.5f) * lowestResHalfPixel;

	float2 fPos = pos / float2(1920.0f, 1080.0f) + float2(0.5f, 0.5f) / float2(1920.0f / 4, 1080.0f / 4) - (lowestResHalfPixel + lowestResQuarterPixel) * float2(groupThreadId.xy / float2(7, 7));


		//fPos.y = 1.0 - fPos.y;



		int topXoffset = 0; // (resolution / 2) * (groupId.y  * GROUP_SAMPLE_WIDTH_HEIGHT + groupThreadId.y) % resolution;
	int bottomXoffset = 0; // ((resolution / 2) * (groupId.y  * GROUP_SAMPLE_WIDTH_HEIGHT + groupThreadId.y + 1)) % resolution;

	float4 c00 = pSrt->m_destTexture0[uint2(pos.x / 2, pos.y / 2)];

		float4 lowerResC00 = pSrt->m_srcTextureLowerRes.SampleLevel(pSrt->m_linearSampler, fPos, 0).rgba;

		ForwardBlendForwardBlendedColor(c00, lowerResC00, pSrt);

	float4 c10 = pSrt->m_destTexture0[uint2(pos.x / 2 + 1, pos.y / 2)];
		float4 lowerResC10 = pSrt->m_srcTextureLowerRes.SampleLevel(pSrt->m_linearSampler, fPos + float2(lowestResQuarterPixel.x, 0), 0).rgba;
		ForwardBlendForwardBlendedColor(c10, lowerResC10, pSrt);


	float4 c01 = pSrt->m_destTexture0[uint2(pos.x / 2, pos.y / 2 + 1)];
		float4 lowerResC01 = pSrt->m_srcTextureLowerRes.SampleLevel(pSrt->m_linearSampler, fPos + float2(0, lowestResQuarterPixel.y), 0).rgba;
		ForwardBlendForwardBlendedColor(c01, lowerResC01, pSrt);

	float4 c11 = pSrt->m_destTexture0[uint2(pos.x / 2 + 1, pos.y / 2 + 1)];
		float4 lowerResC11 = pSrt->m_srcTextureLowerRes.SampleLevel(pSrt->m_linearSampler, fPos + float2(lowestResQuarterPixel.x, lowestResQuarterPixel.y), 0).rgba;
		ForwardBlendForwardBlendedColor(c11, lowerResC11, pSrt);



#endif


#if 0

	const uint resolution = pSrt->m_resolution;

	uint2 groupPos = groupId * GROUP_SAMPLE_WIDTH_HEIGHT * resolution;

	uint2 pos = groupId * GROUP_SAMPLE_WIDTH_HEIGHT * resolution + groupThreadId.xy * resolution;

	//float2 fPos = (groupPos + uint2(GROUP_SAMPLE_WIDTH_HEIGHT-1, GROUP_SAMPLE_WIDTH_HEIGHT-1) * resolution) / float2(1920.0f, 1080.0f);// + float2(0.5f, 0.5f) / float2(1920.0f / 4, 1080.0f / 4);
	float2 fGroupPos = groupPos / float2(1920.0f, 1080.0f);// +float2(0.5f, 0.5f) / float2(1920.0f / 4, 1080.0f / 4);

	//float2 fPos = groupPos / float2(1920.0f, 1080.0f) + float2(0.5f, 0.5f) / float2(1920.0f / 4, 1080.0f / 4);

	float2 lowestResHalfPixel = float2(0.5f, 0.5f) / float2(1920.0f / 4, 1080.0f / 4);
	float2 lowestResQuarterPixel = float2(0.5f, 0.5f) * lowestResHalfPixel;

	float2 fPos = pos / float2(1920.0f, 1080.0f) + float2(0.5f, 0.5f) / float2(1920.0f / 4, 1080.0f / 4) - (lowestResHalfPixel + lowestResQuarterPixel) * float2(groupThreadId.xy / float2(7, 7));

	
	//fPos.y = 1.0 - fPos.y;
	


	int topXoffset = 0; // (resolution / 2) * (groupId.y  * GROUP_SAMPLE_WIDTH_HEIGHT + groupThreadId.y) % resolution;
	int bottomXoffset = 0; // ((resolution / 2) * (groupId.y  * GROUP_SAMPLE_WIDTH_HEIGHT + groupThreadId.y + 1)) % resolution;

	float4 c00 = pSrt->m_destTexture0[uint2(pos.x / 2, pos.y / 2)];

	float4 lowerResC00 = pSrt->m_srcTextureLowerRes.SampleLevel(pSrt->m_linearSampler, fPos, 0).rgba;

	ForwardBlendForwardBlendedColor(c00, lowerResC00, pSrt);

	float4 c10 = pSrt->m_destTexture0[uint2(pos.x/2 + 1, pos.y/2)];
	float4 lowerResC10 = pSrt->m_srcTextureLowerRes.SampleLevel(pSrt->m_linearSampler, fPos + float2(lowestResQuarterPixel.x, 0), 0).rgba;
	ForwardBlendForwardBlendedColor(c10, lowerResC10, pSrt);


	float4 c01 = pSrt->m_destTexture0[uint2(pos.x/2, pos.y/2 +1)];
	float4 lowerResC01 = pSrt->m_srcTextureLowerRes.SampleLevel(pSrt->m_linearSampler, fPos + float2(0, lowestResQuarterPixel.y), 0).rgba;
	ForwardBlendForwardBlendedColor(c01, lowerResC01, pSrt);

	float4 c11 = pSrt->m_destTexture0[uint2(pos.x / 2 + 1, pos.y / 2 + 1)];
	float4 lowerResC11 = pSrt->m_srcTextureLowerRes.SampleLevel(pSrt->m_linearSampler, fPos + float2(lowestResQuarterPixel.x, lowestResQuarterPixel.y), 0).rgba;
	ForwardBlendForwardBlendedColor(c11, lowerResC11, pSrt);



	if (resolution > 2)
		_SCE_BREAK();

	// fill in the quad with the same value
	for (int x = 0; x < resolution; ++x)
	{
		float xblend = float(x) / float(resolution);
		float4 cTop = lerp(c00, c10, xblend);
			float4 cBottom = lerp(c01, c11, xblend);

			for (int y = 0; y < resolution; ++y)
			{
				float yblend = float(y) / float(resolution);
				float4 c = lerp(cTop, cBottom, yblend);

					//if (pSrt->m_flags & FLAG_SMOOTH_UPSCALE)
						pSrt->m_destTextureHighRes[uint2(pos.x + x /* + y/2 */, pos.y + y)] = c;
					//else
					//pSrt->m_destTextureHighRes[uint2(pos.x + x /* + y/2 */, pos.y + y)] = c00;
			}
	}
#endif
}
//
//   #      #
//    \      \
//     \---0--\
//      \      \
//       #      #

[NUM_THREADS(32, 32, 1)] // x : particle index, y is
void CS_SkewUpscaleRayParticles(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleRayTraceReadOnlySrt *pSrt : S_SRT_DATA)
{
	// upscaling to full resolution

	int dispatchIndex = groupId.x;

	ParticleQuadOccupancyUlong myOcc = pSrt->m_particleOccupancy[dispatchIndex];

	//uint2 groupPos = uint2(myOcc.m_locationX, myOcc.m_locationY); // this position is in half res
	uint2 groupPos = uint2(myOcc.m_locationXY & 0x0000FFFF, (myOcc.m_locationXY >> 16));


	groupPos = groupPos * 2;
	
	//uint2 groupPos = groupId * GROUP_SAMPLE_WIDTH_HEIGHT;

	uint2 posFullRes = groupPos + groupThreadId.xy;

	float2 uvHalfRes = posFullRes / float2(1920.0f, 1080.0f);

	float4 lowerResC00 = pSrt->m_srcTextureHalfRes.SampleLevel(pSrt->m_linearSampler, uvHalfRes, 0).rgba;
	//float4 lowerResC00 = pSrt->m_srcTextureLowerRes.SampleLevel(pSrt->m_linearSampler, uvQuarterResAdj, 0).rgba;

	//pSrt->m_destTextureHighRes[posHalfRes] = lowerResC00;
	pSrt->m_destTextureHighRes[posFullRes] = lowerResC00;
}


[NUM_THREADS(8, 8, 1)] // x : particle index, y is
void CS_UpscaleRayParticlesHalfToFullResFullScreen(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleRayTraceReadOnlySrt *pSrt : S_SRT_DATA)
{
	//uint2 groupPos = groupId * GROUP_SAMPLE_WIDTH_HEIGHT;

	uint2 posFullRes = dispatchId.xy;

	float2 uvHalfRes = posFullRes / float2(1920.0f, 1080.0f);

	float4 lowerResC00 = pSrt->m_srcTextureHalfRes.SampleLevel(pSrt->m_linearSampler, uvHalfRes, 0).rgba;
	//float4 lowerResC00 = pSrt->m_srcTextureLowerRes.SampleLevel(pSrt->m_linearSampler, uvQuarterResAdj, 0).rgba;

	//pSrt->m_destTextureHighRes[posHalfRes] = lowerResC00;
	pSrt->m_destTextureHighRes[posFullRes] = lowerResC00;
}

/*
[NUM_THREADS(GROUP_SAMPLE_WIDTH_HEIGHT, GROUP_SAMPLE_WIDTH_HEIGHT, 1)] // x : particle index, y is
void CS_UpscaleRayParticles2(const uint2 dispatchId : SV_DispatchThreadID,
const uint3 groupThreadId : SV_GroupThreadID,
const uint2 groupId : SV_GroupID,
ParticleRayTraceSrt *pSrt : S_SRT_DATA)
{
const uint resolution = pSrt->m_resolution;

uint2 pos = groupId * GROUP_SAMPLE_WIDTH_HEIGHT * resolution + groupThreadId.xy * resolution;

float4 cCenter = pSrt->m_destTexture[uint2(pos.x, pos.y)];
float4 cLeft = pSrt->m_destTexture[uint2(pos.x - resolution, pos.y)];
float4 cRight = pSrt->m_destTexture[uint2(pos.x + resolution, pos.y)];
float4 cBottom = pSrt->m_destTexture[uint2(pos.x, pos.y + resolution)];
float4 cTop = pSrt->m_destTexture[uint2(pos.x, pos.y - resolution)];


for (int y = -resolution; y <= 0; ++y)
{
int xStep = resolution + y; // 0, 1, 2, ..
for (int x = 0 - xStep; x <= 0 + xStep; ++x)
{
float vertBlend = float(resolution + y) / float(resolution); // 0.0, 0.25, 0.5, ..

}

}


// fill in the quad with the same value
for (int x = 0; x < resolution; ++x)
{
float xblend = float(x) / float(resolution);
float4 cTop = lerp(c00, c10, xblend);
float4 cBottom = lerp(c01, c11, xblend);

for (int y = 0; y < resolution; ++y)
{
float yblend = float(y) / float(resolution);
float4 c = lerp(cTop, cBottom, yblend);

if (pSrt->m_flags & FLAG_SMOOTH_UPSCALE)
pSrt->m_destTexture[uint2(pos.x + x, pos.y + y)] = c;
else
pSrt->m_destTexture[uint2(pos.x + x, pos.y + y)] = c00;
}
}
}
*/




[NUM_THREADS(8, 8, 1)] // x : particle index, y is
void CS_CompositeRayTraceParticlesToMainRt(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	uint2 dispatchThreadId : SV_DispatchThreadID,
	RayTraceParticleCompositeSrt *pSrt : S_SRT_DATA)
{
	//float2 uv = (dispatchThreadId.xy + float2(0.5, 0.5)) * srt->params.zw;
	int2 destCoord = int2(pSrt->params.x + dispatchThreadId.x, dispatchThreadId.y);

		if (destCoord.x < pSrt->params.z)
		{
			float4 destColor = pSrt->dst_color[destCoord];
			float4 srcColor = pSrt->src_color[int2(dispatchThreadId.xy)];
			
			// srcColor is already pre-blended with its alpha
			destColor.rgb = destColor.rgb * srcColor.a + srcColor.rgb;
			
			//destColor.rgb = srcColor.rgb ;
			//destColor.rgb = srcColor.a;
			pSrt->dst_color[destCoord] = destColor;
		}
}

