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
#include "particle-ray-trace-cs-ps.fxi"


void SetAlpha(
#if STORE_COLORS_PACKED
	inout uint2 destColorStorage,
#else
	inout float4 destColorStorage,
#endif

	float a)
{
	float4 destColor;

	ReadColor(destColor, destColorStorage);
	destColor.w = a;

	StoreColor(destColorStorage, destColor);
}
	
void SetColor(
#if STORE_COLORS_PACKED
	inout uint2 destColorStorage,
#else
	inout float4 destColorStorage,
#endif

	float4 c)
{
	float4 destColor;

	ReadColor(destColor, destColorStorage);
	destColor = c;

	StoreColor(destColorStorage, destColor);
}

void ComputeOnePixelResultSimple(inout PartRayTraceSetup setup, float ssdepth, float2 pos,
	inout float4 destColorStorage,
	inout int intersectedAnyFlags,
	out float4 resultPixel,
	uint flags, SamplerState linearSampler, Texture2D<float4> spriteAlpha,
	float alphaResDropThreshold, float alphaThreshold,
	uint index, bool doAlphaAccumCheck, bool matchThreads, bool combineThreads)
{
	float4 destColor = destColorStorage;

	setup.pos0 = pos;

	ComputeUv(setup);
	ComputeDepth(setup);
	ComputeColor(setup);

	bool depthOk = DepthCheck(setup, ssdepth);

#if DoFroxelsInLowResPass
	depthOk = 1;
#endif

	resultPixel = float4(0.0, 0.0, 0.0, 0.0);

	if (depthOk)
	{
		//float4 screen00_bc012 = FindBarycentric(p0, p1, p2, float2(0, 0) /* pos.xy */);
		//float2 screen00_uv = p0uv * screen00_bc012.x + p1uv * screen00_bc012.y + p2uv * screen00_bc012.z;
		//uv = screen00_uv;
		//if (depthOk && uv.x > 0 && uv.x < 1.0 && uv.y > 0 && uv.y < 1)
		float2 fromCenter = setup.uv - float2(0.5f, 0.5f);
		float d2 = fromCenter.x * fromCenter.x + fromCenter.y * fromCenter.y;

		if (d2 < 0.25)
		{
			//if (!matchThreads || (uint(__s_read_exec()) == 0x0000000F))
			{

				
#if 1
				resultPixel = EvaluatePixelShaderUV(setup.iPart, 0, setup.uv, setup.color, flags, linearSampler, spriteAlpha);
#endif

				//////////////////////////////////////
				// TESTING
				//////////////////////////////////
				//if (doDropResolution)
				//{
				//	resultPixel.rb = 0;
				//}
				//float posDepth = saturate(depth - ssdepth);
				//resultPixel = float4(posDepth > 0, posDepth > 0, posDepth > 0, 1.0f);

				//resultPixel = float4(1, 1, 1, 1);
				/////////////////////////////////////
				// END TESTING
				//////////////////////////////////////
				//if (resultPixel.a > 0.005f)
				{
					//if (pSrt->m_flags & FLAG_ADD_ALPHA_BIAS)
#if USE_DEBUG_CHECKS
					if (flags & FLAG_ALPHA_ACCUM_DISCARD)
#endif
					{
						//todo: we need to not do this for when there is no threshold or it is very small
						//resultPixel.a = saturate(resultPixel.a + resultPixel.a / (1 - pSrt->m_alphaThreshold) * pSrt->m_alphaThreshold); // adjust alpha so that we can use bigger threshold without edge artifacts
					}

					// blending result with destination
					// note, we are not doing anything for alpha = 0, because we don't want to intr0duce another branch
					// we assume it is unlikely that all threads will have alpha 0, so we will still spend blending cost
					ForwardBlendColor(destColor, resultPixel, flags);

					

					if (!doAlphaAccumCheck)
					{
						// when this, we check for res drop threshold
#if USE_DEBUG_CHECKS
						if (destColor.a < alphaResDropThreshold)
#else
						if (destColor.a < 0.5)
#endif
						{
							// very little of bg is seen anymore
							// set the flag
							intersectedAnyFlags = intersectedAnyFlags | (1 << index);
						}
					}

					if (doAlphaAccumCheck)
					{
#if USE_DEBUG_CHECKS
						if (destColor.a < alphaThreshold)
#else
						if (destColor.a < 0.02)
#endif
						{
							// very little of bg is seen anymore
							// set the flag
							intersectedAnyFlags = intersectedAnyFlags | (1 << (4 + index));
						}
					}


					// store it back
					destColorStorage = destColor;

				} // if writing pixel (alpha > 0.005)
			} // and intersecting
		} // if distance ok

		

	} // if depthOk

	if (combineThreads)
	{
		// read data from adjacent quads and combine colors
		LaneSwizzle(resultPixel.r, 0x1F, 0, 0x04); // that will grab corresponding pixel from a different quad
		LaneSwizzle(resultPixel.g, 0x1F, 0, 0x04); // that will grab corresponding pixel from a different quad
		LaneSwizzle(resultPixel.b, 0x1F, 0, 0x04); // that will grab corresponding pixel from a different quad
		LaneSwizzle(resultPixel.a, 0x1F, 0, 0x04); // that will grab corresponding pixel from a different quad

		ForwardBlendColor(destColor, resultPixel, flags);



		if (!doAlphaAccumCheck)
		{
			// when this, we check for res drop threshold
#if USE_DEBUG_CHECKS
			if (destColor.a < alphaResDropThreshold)
#else
			if (destColor.a < 0.5)
#endif
			{
				// very little of bg is seen anymore
				// set the flag
				intersectedAnyFlags = intersectedAnyFlags | (1 << index);
			}
		}

		if (doAlphaAccumCheck)
		{
#if USE_DEBUG_CHECKS
			if (destColor.a < alphaThreshold)
#else
			if (destColor.a < 0.02)
#endif
			{
				// very little of bg is seen anymore
				// set the flag
				intersectedAnyFlags = intersectedAnyFlags | (1 << (4 + index));
			}
		}


		destColorStorage = destColor;
	}
}

void ComputeOnePixelResult(inout PartRayTraceSetup setup, ParticleRayTraceReadOnlySrt *pSrt, float ssdepth, float2 pos,
#if STORE_COLORS_PACKED
	inout uint2 destColorStorage,
#else
	inout float4 destColorStorage,
#endif
	
	inout int intersectedAnyFlags,
	uint index, bool doAlphaAccumCheck)
{
	float4 destColor;

	ReadColor(destColor, destColorStorage);

	setup.pos0 = pos;

	ComputeUv(setup);
	ComputeDepth(setup);
	ComputeColor(setup);

	bool depthOk = DepthCheck(setup, ssdepth);

	if (depthOk)
	{
		//float4 screen00_bc012 = FindBarycentric(p0, p1, p2, float2(0, 0) /* pos.xy */);
		//float2 screen00_uv = p0uv * screen00_bc012.x + p1uv * screen00_bc012.y + p2uv * screen00_bc012.z;
		//uv = screen00_uv;
		//if (depthOk && uv.x > 0 && uv.x < 1.0 && uv.y > 0 && uv.y < 1)
		float2 fromCenter = setup.uv - float2(0.5f, 0.5f);
		float d2 = fromCenter.x * fromCenter.x + fromCenter.y * fromCenter.y;

		if (d2 < 0.25)
		{
			float4 resultPixel = //float4(0.1, 0.0, 0.0, 0.1);
#if 1
			EvaluatePixelShaderUV(setup.iPart, 0, setup.uv, setup.color, pSrt->m_flags, pSrt->m_linearSampler, pSrt->m_spriteAlpha);
#endif

			//////////////////////////////////////
			// TESTING
			//////////////////////////////////
			//if (doDropResolution)
			//{
			//	resultPixel.rb = 0;
			//}
			//float posDepth = saturate(depth - ssdepth);
			//resultPixel = float4(posDepth > 0, posDepth > 0, posDepth > 0, 1.0f);

			//resultPixel = float4(1, 1, 1, 1);
			/////////////////////////////////////
			// END TESTING
			//////////////////////////////////////
			//if (resultPixel.a > 0.005f)
			{
				//if (pSrt->m_flags & FLAG_ADD_ALPHA_BIAS)
#if USE_DEBUG_CHECKS
				if (pSrt->m_flags & FLAG_ALPHA_ACCUM_DISCARD)
#endif
				{
					//todo: we need to not do this for when there is no threshold or it is very small
					//resultPixel.a = saturate(resultPixel.a + resultPixel.a / (1 - pSrt->m_alphaThreshold) * pSrt->m_alphaThreshold); // adjust alpha so that we can use bigger threshold without edge artifacts
				}

				// blending result with destination
				// note, we are not doing anything for alpha = 0, because we don't want to intr0duce another branch
				// we assume it is unlikely that all threads will have alpha 0, so we will still spend blending cost
				ForwardBlendColor(destColor, resultPixel, pSrt->m_flags);

				if (!doAlphaAccumCheck)
				{
					// when this, we check for res drop threshold
#if USE_DEBUG_CHECKS
					if (destColor.a < pSrt->m_alphaResDropThreshold)
#else
					if (destColor.a < 0.5)
#endif
					{
						// very little of bg is seen anymore
						// set the flag
						intersectedAnyFlags = intersectedAnyFlags | (1 << index);
					}
				}

				if (doAlphaAccumCheck)
				{
#if USE_DEBUG_CHECKS
					if (destColor.a < pSrt->m_alphaThreshold)
#else
					if (destColor.a < 0.03)
#endif
					{
					// very little of bg is seen anymore
					// set the flag
						intersectedAnyFlags = intersectedAnyFlags | (1 << (4 + index));
					}
				}


				// store it back
				StoreColor(destColorStorage, destColor);

			} // if writing pixel (alpha > 0.005)
		} // and intersecting
	} // if depthOk
}


void ComputeOnePixelResultDebug(inout PartRayTraceSetup setup, ParticleRayTraceReadOnlySrt *pSrt, float ssdepth, float2 pos,
#if STORE_COLORS_PACKED
	inout uint2 destColorStorage,
#else
	inout float4 destColorStorage,
#endif

	inout int intersectedAnyFlags,
	uint index)
{
	float4 destColor;

	ReadColor(destColor, destColorStorage);

	setup.pos0 = pos;

	ComputeUv(setup);
	ComputeDepth(setup);
	ComputeColor(setup);

	bool depthOk = DepthCheck(setup, ssdepth);

	if (depthOk)
	{
		//float4 screen00_bc012 = FindBarycentric(p0, p1, p2, float2(0, 0) /* pos.xy */);
		//float2 screen00_uv = p0uv * screen00_bc012.x + p1uv * screen00_bc012.y + p2uv * screen00_bc012.z;
		//uv = screen00_uv;
		//if (depthOk && uv.x > 0 && uv.x < 1.0 && uv.y > 0 && uv.y < 1)
		float2 fromCenter = setup.uv - float2(0.5f, 0.5f);
		float d2 = fromCenter.x * fromCenter.x + fromCenter.y * fromCenter.y;

		if (d2 < 0.25)
		{
			float4 resultPixel = float4(0.0, 0.0, 1.0, 1.0);
#if 0
			EvaluatePixelShaderUV(setup.iPart, 0, setup.uv, setup.color, pSrt->m_flags, pSrt->m_linearSampler, pSrt->m_spriteAlpha);
#endif

			//////////////////////////////////////
			// TESTING
			//////////////////////////////////
			//if (doDropResolution)
			//{
			//	resultPixel.rb = 0;
			//}
			//float posDepth = saturate(depth - ssdepth);
			//resultPixel = float4(posDepth > 0, posDepth > 0, posDepth > 0, 1.0f);

			//resultPixel = float4(1, 1, 1, 1);
			/////////////////////////////////////
			// END TESTING
			//////////////////////////////////////
			if (resultPixel.a > 0.005f)
			{
				//if (pSrt->m_flags & FLAG_ADD_ALPHA_BIAS)
#if USE_DEBUG_CHECKS
				if (pSrt->m_flags & FLAG_ALPHA_ACCUM_DISCARD)
#endif
				{
					//todo: we need to not do this for when there is no threshold or it is very small
					//resultPixel.a = saturate(resultPixel.a + resultPixel.a / (1 - pSrt->m_alphaThreshold) * pSrt->m_alphaThreshold); // adjust alpha so that we can use bigger threshold without edge artifacts
				}

				// blending result with destination
				// note, we are not doing anything for alpha = 0, because we don't want to intr0duce another branch
				// we assume it is unlikely that all threads will have alpha 0, so we will still spend blending cost
				ForwardBlendColor(destColor, resultPixel, pSrt->m_flags);

				// store it back
				StoreColor(destColorStorage, destColor);


			} // if writing pixel (alpha > 0.005)
		} // and intersecting
	} // if depthOk
}



[NUM_THREADS(NUM_THREADS_IN_GROUP, 1, 1)] // x : particle index, y is
void CS_RayTraceParticlesOneWaveFront(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleRayTraceReadOnlySrt *pSrt : S_SRT_DATA)
{
#if USE_COOUPANCY_TESTS
	int dispatchIndex = groupId.x;

	ParticleQuadOccupancyUlong myOcc = pSrt->m_particleOccupancy[dispatchIndex];

	uint wfIndex = 0;
	uint inWfIndex = groupThreadId.x;
	uint inGroupIndex = inWfIndex;

	uint2 wfInGroup = uint2(wfIndex % GROUP_NUM_WAVEFRONTS_IN_W, wfIndex / GROUP_NUM_WAVEFRONTS_IN_W); // wavefront coordinate in group

	uint quadIndex = inWfIndex/ 4;
	uint2 quadPos = uint2(quadIndex % 4, quadIndex / 4) * 2;
	
	uint2 inWFQuadPos = quadPos + uint2((inWfIndex % 4) % 2, (inWfIndex % 4) / 2);

	//uint2 inWFQuadPos = uint2((inWfIndex % 8), (inWfIndex / 8));

	//uint2 groupPos = uint2(myOcc.m_locationX, myOcc.m_locationY); // offset of the whole group, in target resolution space
	uint2 groupPos = uint2(myOcc.m_locationXY & 0x0000FFFF, (myOcc.m_locationXY >> 16));

	float2 pos0 = groupPos + (wfInGroup * uint2(8, 8) + inWFQuadPos * uint2(1, 1));

	uint2 wfPosSS = groupPos + (wfInGroup * WAFEFRONT_QUAD_W_H);

	uint wfOffsetX = WAFEFRONT_QUAD_W_H;
	uint wfOffsetY = WAFEFRONT_QUAD_W_H;

	//pSrt->m_destTexture0[pos0] = float4(float(inWfIndex) / 64, 0, 0, 0);
	//return;
#endif

	int totalOcc = (pSrt->m_numParts > 0 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[0]) : 0)
		+ (pSrt->m_numParts > 64 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[1]) : 0)
		+ (pSrt->m_numParts > 128 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[2]) : 0);
		//+ (pSrt->m_numParts > 192 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[3]) : 0);

	//pSrt->m_destTexture0[pos0] = float4(totalOcc / 256.0, 0, 0, 0.0);
	//pSrt->m_destTexture0[pos0] = float4(quadIndex / 16.0, 0, 0, 0.0);

#if USE_CACHED_RES_DROP_INDEX
	//pSrt->m_destTexture0[pos0] = float4(myOcc.m_resDropIndex / 256.0, 0, 0, 0.0);
	//return;
#endif


	//pSrt->m_destTexture0[pos0] = pSrt->m_destTexture0[pos0] * float4(0.999, 0.999, 0.999, 0.999);
	//return;

	//TODO: might need to reenable this
	// this checks that destination is already opaque. since we are only testing one efect right now, this is never the case
	//if (dest.w < 0.01)
	//{
	// we can discard everything
	//	pSrt->m_destTexture0[pos] = float4(dest.xyz, 0.0);

	//	return;
	//}

#if STORE_COLORS_PACKED
	uint2 destArr[NUM_PIXELS_COVERED_X];
#else
	float4 destArr[NUM_PIXELS_COVERED_X];
#endif

	int numTests64 = (pSrt->m_numParts + 63) / 64;

#if DO_DEPTH
#if STORE_DEPTH_PACKED
	uint2 ssdepths;
#else
	float ssdepth[NUM_PIXELS_COVERED_X];// GetLinearDepth(pSrt->m_depthTexture[pos], pSrt->m_depthParams);
#endif
#endif

	int intersectedAnyFlags = 0;
	uint4 clrtsharplo = __get_tsharplo(pSrt->m_destTexture0);
	uint common_opts = __kImage_RO | __kImage_texture2d | __kImage_R128 ;
	uint common_opts_write = __kImage_texture2d | __kImage_R128;

	uint4 depthtsharplo = __get_tsharplo(pSrt->m_depthTexture);

#if READ_DEST_COLOR_DATA_RIGHT_AWAY
	StoreColor(destArr[0], float4(0, 0, 0, 1)); //  pSrt->m_destTexture0[pos0];
	DO_IF_NUM_PIX_MORE_THAN_1(StoreColor(destArr[1], float4(0, 0, 0, 1)));  // pSrt->m_destTexture0[uint2(pos0.x + wfOffsetX, pos0.y)];
	DO_IF_NUM_PIX_MORE_THAN_2(StoreColor(destArr[2], float4(0, 0, 0, 1)));  // pSrt->m_destTexture0[uint2(pos0.x + wfOffsetX, pos0.y)];
	DO_IF_NUM_PIX_MORE_THAN_3(StoreColor(destArr[3], float4(0, 0, 0, 1)));  // pSrt->m_destTexture0[uint2(pos0.x + wfOffsetX, pos0.y)];
#endif

#if READ_DEST_DEPTH_DATA_RIGHT_AWAY
	{
#if DO_DEPTH
		image_load_result_t result = __image_load_mip(uint4(pos0, 0, 0), 0, depthtsharplo, 0, common_opts);
		float ssdepth0 = bit_cast<float>(result.data);
		DO_IF_NUM_PIX_MORE_THAN_1(result = __image_load_mip(uint4((pos0.x + wfOffsetX), pos0.y, 0, 0), 0, depthtsharplo, 0, common_opts); float ssdepth1 = bit_cast<float>(result.data));
		DO_IF_NUM_PIX_MORE_THAN_2(result = __image_load_mip(uint4((pos0.x), (pos0.y + wfOffsetY), 0, 0), 0, depthtsharplo, 0, common_opts); float ssdepth2 = bit_cast<float>(result.data));
		DO_IF_NUM_PIX_MORE_THAN_3(result = __image_load_mip(uint4((pos0.x + wfOffsetX), (pos0.y + wfOffsetY), 0, 0), 0, depthtsharplo, 0, common_opts); float ssdepth3 = bit_cast<float>(result.data));

#if STORE_DEPTH_PACKED
		StoreDepths(ssdepths, ssdepth0, ssdepth1, ssdepth2, ssdepth3);
#else
		ssdepth[0] = ssdepth0;
		DO_IF_NUM_PIX_MORE_THAN_1(ssdepth[1] = ssdepth1);
		DO_IF_NUM_PIX_MORE_THAN_2(ssdepth[2] = ssdepth2);
		DO_IF_NUM_PIX_MORE_THAN_3(ssdepth[3] = ssdepth3);
#endif

#endif
	}
#endif
	

	if (false)
	{
		// all threads are opaque
		intersectedAnyFlags = 0xFFFFFFFF;
		//iTestX64 = 0x0000FFFF; // max it out, so that for loop exits
		//__occ = 0; // zero it out so that while loop exits

		// dump memory and re-read it in different order
		float4 c;
		ReadColor(c, destArr[0]);
		__image_store(asuint(c), uint4(pos0, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128);
		DO_IF_NUM_PIX_MORE_THAN_1(ReadColor(c, destArr[1]); __image_store(asuint(c), uint4(pos0.x + wfOffsetX, pos0.y, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128));
		DO_IF_NUM_PIX_MORE_THAN_2(ReadColor(c, destArr[2]); __image_store(asuint(c), uint4(pos0.x, pos0.y + wfOffsetY, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128));
		DO_IF_NUM_PIX_MORE_THAN_3(ReadColor(c, destArr[3]); __image_store(asuint(c), uint4(pos0.x + wfOffsetX, pos0.y + wfOffsetY, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128));

		// now we read it in adjacent order
		//pos0 = groupPos + inWFQuadPos * 2; // we scale our local coordinate by two because now we cover a bigger whole 16x16 block by one 8x8 wavefront
		pos0 = groupPos + inWFQuadPos * 2; // we scale our local coordinate by two because now we cover a bigger whole 16x16 block by one 8x8 wavefront
		pos0 = pos0 + inWFQuadPos / float2(7, 7) * 1.0;
		StoreColor(destArr[0], float4(0, 0, 0, 1));
	}

	for (int iTestX64 = 0; iTestX64 < numTests64 * 64; iTestX64 += 64)
	{
		ulong __occ = myOcc.m_occupancy64[iTestX64 / 64];

		while (__occ)
		{
			int iLocalPart = __s_ff1_i32_b64(__occ);
			//if (iPart == -1)
			//    break;

			//__s_bitset0_b64(__occ, iLocalPart);
			__occ = __s_andn2_b64(__occ, __s_lshl_b64(1, iLocalPart));

			//int occupancyBits = occ.occupancy[iTest];
			int iPart = iTestX64 + iLocalPart;

			{
				//if ((occ.occupancy[iTest] & leftoverMask) == 0)
				//	break;
				//ProcessParticle(parts[iPart], iPart, iPixel, pos, pSrt);

				//ulong bitMask = __s_lshl_b64(1, iPart % 64);
				//bool intersecting = /*occ.occupancy[iTest]*/ __occ & bitMask;

				//if (!intersecting)
				//	continue;

				//ulong exec = __s_read_exec();
				//uint first_bit = __s_ff1_i32_b64(exec);

				// only read data from first active lane
				//if (groupThreadId.x == first_bit)
				CachedPartQuadControl ctrl;

				//otherwise we can read some of the data from currently running threads
				PartQuadControl _ctrl = pSrt->m_particleQuadControls[iPart];

#if PACK_GRADIENT
				ctrl.uv00 = float2(f16tof32(_ctrl.packedGradient.x), f16tof32(_ctrl.packedGradient.y));
				ctrl.uv10 = float2(f16tof32(_ctrl.packedGradient.z), f16tof32(_ctrl.packedGradient.x >> 16));
				ctrl.uv01 = float2(f16tof32(_ctrl.packedGradient.y >> 16), f16tof32(_ctrl.packedGradient.z >> 16));
#else
				ctrl.uv00 = _ctrl.uv00; ctrl.uv10 = _ctrl.uv10; ctrl.uv01 = _ctrl.uv01;
#endif

#if CONSTANT_DEPTH
				ctrl.depth00 = _ctrl.constantDepth;
#else
	#if PACK_DEPTH
				ctrl.depth00 = f16tof32(_ctrl.packedDepthGradient.x); ctrl.depth10 = f16tof32(_ctrl.packedDepthGradient.y); ctrl.depth01 = f16tof32(_ctrl.packedDepthGradient.x >> 16);
	#else
				ctrl.depth00 = _ctrl.depth00; ctrl.depth10 = _ctrl.depth10; ctrl.depth01 = _ctrl.depth01;
	#endif
#endif


#if PACK_COLOR
				ctrl.packedColor = _ctrl.packedColor;
#endif

				PartRayTraceSetup setup;

				FillInUvParams(ctrl, setup);
				FillInDepthParams(ctrl, setup);
				setup.iPart = iPart;
#if PACK_COLOR
				setup.packedColor = ctrl.packedColor;
#else
				setup.instanceColor = inst.color;
#endif
#if DO_DEPTH
#if STORE_DEPTH_PACKED
				float ssdepth0 = GetDepth0(ssdepths);
				DO_IF_NUM_PIX_MORE_THAN_1(float ssdepth1 = GetDepth1(ssdepths));
				DO_IF_NUM_PIX_MORE_THAN_2(float ssdepth2 = GetDepth2(ssdepths));
				DO_IF_NUM_PIX_MORE_THAN_3(float ssdepth3 = GetDepth3(ssdepths));
#else
				float ssdepth0 = ssdepth[0];
				DO_IF_NUM_PIX_MORE_THAN_1(float ssdepth1 = ssdepth[1]);
				DO_IF_NUM_PIX_MORE_THAN_2(float ssdepth2 = ssdepth[2]);
				DO_IF_NUM_PIX_MORE_THAN_3(float ssdepth3 = ssdepth[3]);
#endif
#else
				float ssdepth0 = 0;
				DO_IF_NUM_PIX_MORE_THAN_1(float ssdepth1 = 0);
				DO_IF_NUM_PIX_MORE_THAN_2(float ssdepth2 = 0);
				DO_IF_NUM_PIX_MORE_THAN_3(float ssdepth3 = 0);
#endif

				//for (int ix = 0; ix < NUM_PIXELS_COVERED_X; ++ix)
				//{
				//	ComputeOnePixelResult(setup, pSrt, ssdepth[ix], float2(pos0.x + wfOffsetX * (ix % 2), pos0.y + wfOffsetY * (ix / 2)), destArr[ix], intersectedAnyFlags);
				//}
				if (ReadFirstLane(intersectedAnyFlags) < 0)
				{
					// we just need to do one step and duplicate 4 values
					ComputeOnePixelResult(setup, pSrt, ssdepth0, pos0, destArr[0], intersectedAnyFlags, 0, true);
					
					bool allOpaque = (intersectedAnyFlags) == 0xF0000010; // all 4 pixels are 1s, all are opaque
					
					ulong lane_mask = __v_cmp_eq_u32(allOpaque, true);
					
					//if (0xFFFFFFFFFFFFFFFF == lane_mask)
					//{
					//	iTestX64 = 0x0000FFFF; // max it out, so that for loop exits
					//	__occ = 0; // zero it out so that while loop exits
					//}
					
					if (allOpaque)
					{
						float4 commonColor;
						ReadColor(commonColor, destArr[0]);
						uint2 lowResPos = groupPos / 2 + inWFQuadPos;
#if USE_DEBUG_CHECKS
						if (pSrt->m_flags & FLAG_RENDER_STATS)
						{
							// since we want to show that we rendered less, we will store this data only in quarter of the destination pixels
							// reset alpha so that stats are drawn
							if (uint(pos0.x) % 8 < 4)
							{
								if (uint(pos0.y) % 8 < 4)
								{
									pSrt->m_destTextureLowerRes[lowResPos] = commonColor;
								}
							}
						}
						else
#endif
						{
							pSrt->m_destTextureLowerRes[lowResPos] = commonColor;
						}

						return;
					}
				}
				else
				{
					ComputeOnePixelResult(setup, pSrt, ssdepth0, pos0, destArr[0], intersectedAnyFlags, 0, false);
					DO_IF_NUM_PIX_MORE_THAN_1(ComputeOnePixelResult(setup, pSrt, ssdepth1, float2(pos0.x + wfOffsetX, pos0.y), destArr[1], intersectedAnyFlags, 1, false));
					DO_IF_NUM_PIX_MORE_THAN_2(ComputeOnePixelResult(setup, pSrt, ssdepth2, float2(pos0.x, pos0.y + wfOffsetY), destArr[2], intersectedAnyFlags, 2, false));
					DO_IF_NUM_PIX_MORE_THAN_3(ComputeOnePixelResult(setup, pSrt, ssdepth3, float2(pos0.x + wfOffsetX, pos0.y + wfOffsetY), destArr[3], intersectedAnyFlags, 3, false));
					
#if USE_CACHED_RES_DROP_INDEX
					if (iPart == myOcc.m_resDropIndex)
#else
					bool allDropRes = intersectedAnyFlags == 0x0000000F; // all 4 pixels are 1s, all are pretty opaque

					ulong exec = __s_read_exec();
					ulong lane_mask = __v_cmp_eq_u32(allDropRes, true);

					//if (all(allDropRes))
					//if (exec == lane_mask)
					if (0xFFFFFFFFFFFFFFFF == lane_mask)
#endif
					{
						// all threads are opaque
						intersectedAnyFlags = 0xF0000000;
						//iTestX64 = 0x0000FFFF; // max it out, so that for loop exits
						//__occ = 0; // zero it out so that while loop exits

						// dump memory and re-read it in different order
						float4 c;
						ReadColor(c, destArr[0]);
						__image_store(asuint(c), uint4(pos0, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128);
						DO_IF_NUM_PIX_MORE_THAN_1(ReadColor(c, destArr[1]); __image_store(asuint(c), uint4(pos0.x + wfOffsetX, pos0.y, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128));
						DO_IF_NUM_PIX_MORE_THAN_2(ReadColor(c, destArr[2]); __image_store(asuint(c), uint4(pos0.x, pos0.y + wfOffsetY, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128));
						DO_IF_NUM_PIX_MORE_THAN_3(ReadColor(c, destArr[3]); __image_store(asuint(c), uint4(pos0.x + wfOffsetX, pos0.y + wfOffsetY, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128));

						// now we read it in adjacent order
						pos0 = groupPos + inWFQuadPos * 2 ; // we scale our local coordinate by two because now we cover a bigger whole 16x16 block by one 8x8 wavefront
						pos0 = pos0 + inWFQuadPos / float2(7.0, 7.0);

						StoreColor(destArr[0], float4(0, 0, 0, 1));
					}
				}
			}
			//break; // just do one particle
		} // while loop going through 64 bit mask
	} // for loop to go through n 64 bit masks

#if USE_DEBUG_CHECKS
	if (pSrt->m_flags & FLAG_RENDER_STATS)
	{
		// reset alpha so that stats are drawn
		SetAlpha(destArr[0], 0);
		DO_IF_NUM_PIX_MORE_THAN_1(SetAlpha(destArr[1], 0));
		DO_IF_NUM_PIX_MORE_THAN_2(SetAlpha(destArr[2], 0));
		DO_IF_NUM_PIX_MORE_THAN_3(SetAlpha(destArr[3], 0));
	}
#endif

	if (intersectedAnyFlags >= 0x0)
	{
		// we did not drop resolution
		//pSrt->m_destTexture0[pos0] = destArr[0];


		// works 8W T#
		float4 c;
		ReadColor(c, destArr[0]);
		__image_store(asuint(c), uint4(pos0, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128);
		DO_IF_NUM_PIX_MORE_THAN_1(ReadColor(c, destArr[1]); __image_store(asuint(c), uint4(pos0.x + wfOffsetX, pos0.y, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128));
		DO_IF_NUM_PIX_MORE_THAN_2(ReadColor(c, destArr[2]); __image_store(asuint(c), uint4(pos0.x, pos0.y + wfOffsetY, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128));
		DO_IF_NUM_PIX_MORE_THAN_3(ReadColor(c, destArr[3]); __image_store(asuint(c), uint4(pos0.x + wfOffsetX, pos0.y + wfOffsetY, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128));

		// 4W T#
		//__image_store_mip(uint4(asuint(destArr[0].x), asuint(destArr[0].y), asuint(destArr[0].z), asuint(destArr[0].w)), uint4(destPos, 0, 0), 0, __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128 | __kImage_GLC);
		//__image_store(uint4(asuint(destArr[0].x), asuint(destArr[0].y), asuint(destArr[0].z), asuint(destArr[0].w)), uint4(destPos, 0, 0), __get_tsharplo(pSrt->m_destTexture0), __get_tsharphi(pSrt->m_destTexture0), __kImage_texture2d);
		//__image_store(uint4(asuint(0.5), asuint(0.0), asuint(0.0), asuint(0.5)), uint4(pos0, 0, 0), __get_tsharplo(pSrt->m_destTexture0), __get_tsharphi(pSrt->m_destTexture0), 0);
		//__image_store_mip( uint4(asuint(destArr[0].x), asuint(destArr[0].y),  asuint(destArr[0].z),  asuint(destArr[0].w)), uint4(pos0, 0, 0), 0, clrtsharplo, 0, common_opts_write);
		// we provide only one mip in the texture so there is no reason to use _mip version
		// note in assmebly we still provide 3 vgprs for coordinate, the last one is garbage since lod is not specified

	}
	else
	{
		// store the version from low resolution results
		// we need to blend up the results of one shader

		float4 commonColor;
		ReadColor(commonColor, destArr[0]);


		//StoreColor(destArr[0], pSrt->m_destTexture0[pos0]);
		//DO_IF_NUM_PIX_MORE_THAN_1(StoreColor(destArr[1], pSrt->m_destTexture0[uint2(pos0.x + resolution, pos0.y)]));
		//DO_IF_NUM_PIX_MORE_THAN_2(StoreColor(destArr[2], pSrt->m_destTexture0[uint2(pos0.x, pos0.y + resolution)]));
		//DO_IF_NUM_PIX_MORE_THAN_3(StoreColor(destArr[3], pSrt->m_destTexture0[uint2(pos0.x + resolution, pos0.y + resolution)]));

		//float4 c0 = pSrt->m_destTexture0[pos0];
		//ForwardBlendForwardBlendedColor(c0, commonColor, pSrt);

		uint2 lowResPos = groupPos/2 + inWFQuadPos;

#if USE_DEBUG_CHECKS
		if (pSrt->m_flags & FLAG_RENDER_STATS)
		{
			// since we want to show that we rendered less, we will store this data only in quarter of the destination pixels
			// reset alpha so that stats are drawn
			if (uint(pos0.x) % 8 < 4)
			{
				if (uint(pos0.y) % 8 < 4)
				{
					pSrt->m_destTextureLowerRes[lowResPos] = commonColor;
				}
			}
		}
		else
#endif
		{
			pSrt->m_destTextureLowerRes[lowResPos] = commonColor;
		}
	}
}

#if DoFroxelsInLowResPass

[NUM_THREADS(64, 1, 1)]
void CS_RayTraceParticlesOneWaveFrontLowResWithFroxels(const uint2 dispatchId : SV_DispatchThreadID,
const uint3 groupThreadId : SV_GroupThreadID,
const uint2 groupId : SV_GroupID,
ParticleRayTraceLowResClassificationSrt *pSrt : S_SRT_DATA)

#else

[NUM_THREADS(64, 1, 1)]
void CS_RayTraceParticlesOneWaveFrontLowRes(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleRayTraceLowResClassificationSrt *pSrt : S_SRT_DATA)

#endif
{
	int dispatchIndex = groupId.x;

	ParticleQuadOccupancyUlong myOcc = pSrt->m_particleOccupancy640RW[dispatchIndex];

	uint wfIndex = 0;
	uint inWfIndex = groupThreadId.x;
	uint inGroupIndex = inWfIndex;

	uint2 wfInGroup = uint2(wfIndex % GROUP_NUM_WAVEFRONTS_IN_W, wfIndex / GROUP_NUM_WAVEFRONTS_IN_W); // wavefront coordinate in group

	uint quadIndex = inWfIndex / 4;
	
	uint2 quadCoord = uint2(quadIndex % 4, quadIndex / 4); // (0, 0) .. (3, 3) // 16 quads, each quad 4x4 cells/froxels

	uint2 inWFQuadCoord = /*quadPos + */ uint2((inWfIndex % 4) % 2, (inWfIndex % 4) / 2); // (0,0) or (1,0) or (0,1) or (1,1)

	uint2 inWFCellCoord = quadCoord * uint2(2, 2) + inWFQuadCoord; // (0, 0) .. (7, 7) but swizzled

	uint2 inWFQuadPos = uint2((inWfIndex % 8), (inWfIndex / 8));
	
	//uint2 groupPos = uint2(myOcc.m_locationX, myOcc.m_locationY); // offset of the whole group, in target resolution space
	uint2 groupPos = uint2(myOcc.m_locationXY & 0x0000FFFF, (myOcc.m_locationXY >> 16));


	// groupPos is in target reolution. likely it is half resolution
	// each OccupancyQuad covers 16x16 in target resolution
	// so for target resolution = half resolution, each OccupancyQuad is 32x32 in native resolution

#if FogFroxelGridSize == 16
	// froxel grid is in native resolution x / 16  & native resolution y / 16, so each froxel grid cell represents 16x16 native pixels
	// so for half resolution, this groupPos is responsible for 4 froxels
#elif FogFroxelGridSize == 32
	// froxel grid is in native resolution x / 32  & native resolution y / 32, so each froxel grid cell represents 32x32 native pixels
	// so for half resolution, this groupPos is responsible for 1 froxel
#endif

#if DoFroxelsInLowResPass

	// in case of froxels, each occupancy cell is 2x2 froxels. so this thread is responsible for writing 4 values. each for different froxel
	
	// right now we use only the first 4 threads. each thread is one froxel

	// also we work in half resolution space, so froxel size is 8x8 or 6x6 instead of 16x16, 12x12
#if FogFroxelGridSizeNativeRes == 16
	float2 pos0 = groupPos + inWFCellCoord * float2(8, 8) + float2(3.5, 3.5); // still in half res
#elif FogFroxelGridSizeNativeRes == 12
	float2 pos0 = groupPos + inWFCellCoord * float2(6, 6) + float2(2.5, 2.5); // still in half res
#elif FogFroxelGridSizeNativeRes == 10
	float2 pos0 = groupPos + inWFCellCoord * float2(5, 5) + float2(2.0, 2.0); // still in half res
#elif FogFroxelGridSizeNativeRes == 8
	float2 pos0 = groupPos + inWFCellCoord * float2(4, 4) + float2(1.5, 1.5); // still in half res
#elif FogFroxelGridSizeNativeRes == 6
	float2 pos0 = groupPos + inWFCellCoord * float2(3, 3) + float2(1.0, 1.0); // still in half res
#elif FogFroxelGridSizeNativeRes == 5
	float2 pos0 = groupPos + inWFCellCoord * float2(2.5, 2.5) + float2(0.75, 0.75); // still in half res
#endif
	
#else
	
	float2 pos0 = groupPos + inWFQuadPos * float2(8, 8) + float2(3.5, 3.5); // but this is not swizzled?
#endif


	uint2 wfPosSS = groupPos + (wfInGroup * WAFEFRONT_QUAD_W_H);

	uint wfOffsetX = WAFEFRONT_QUAD_W_H;
	uint wfOffsetY = WAFEFRONT_QUAD_W_H;

	//pSrt->m_destTexture0[pos0] = float4(float(inWfIndex) / 64, 0, 0, 0);
	//return;
//#if COMBINE_MULTIPLE_PARTS_IN_CLASSIFY
//	if (groupThreadId.x >= 8)
//		return; 
//#else
//	if (groupThreadId.x >= 4)
//		return; // only use first quad for now
//#endif

	int iMyQuadIndex = groupThreadId.x / 4;

	// debug test, all low res
	//pSrt->m_particleOccupancy640RW[dispatchIndex].m_resDropIndex = 0;
	//return;


	int totalOcc = (pSrt->m_numParts > 0 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[0]) : 0)
		+ (pSrt->m_numParts > 64 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[1]) : 0)
		+ (pSrt->m_numParts > 128 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[2]) : 0);

	//+ (pSrt->m_numParts > 192 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[3]) : 0);

	//pSrt->m_destTexture0[pos0] = totalOcc == 0 ? float4(1, 1, 1, 0.0) : float4(0, 0, totalOcc / 1.0f, 0.0);
	//pSrt->m_destTexture0[pos0] = float4(quadIndex / 16.0, 0, 0, 0.0);

	//pSrt->m_destTexture0[pos0] = pSrt->m_destTexture0[pos0] * float4(0.999, 0.999, 0.999, 0.999);
	//return;

	//TODO: might need to reenable this
	// this checks that destination is already opaque. since we are only testing one efect right now, this is never the case
	//if (dest.w < 0.01)
	//{
	// we can discard everything
	//	pSrt->m_destTexture0[pos] = float4(dest.xyz, 0.0);

	//	return;
	//}

	float4 destArr[1];

	int numTests64 = (pSrt->m_numParts + 63) / 64;

	float ssdepth[1];// GetLinearDepth(pSrt->m_depthTexture[pos], pSrt->m_depthParams);

	int intersectedAnyFlags = 0;
	uint4 clrtsharplo = __get_tsharplo(pSrt->m_destTexture0);
	uint common_opts = __kImage_RO | __kImage_texture2d | __kImage_R128;
	uint common_opts_write = __kImage_texture2d | __kImage_R128;

	uint4 depthtsharplo = __get_tsharplo(pSrt->m_depthTexture);

	destArr[0] = float4(0, 0, 0, 1); //  pSrt->m_destTexture0[pos0];

	{
#if DO_DEPTH
		image_load_result_t result = __image_load_mip(uint4(pos0, 0, 0), 0, depthtsharplo, 0, common_opts);
		ssdepth[0] = bit_cast<float>(result.data);;
#endif
	}


	for (int iTestX64 = 0; iTestX64 < numTests64 * 64; iTestX64 += 64)
	{
		ulong __occ = myOcc.m_occupancy64[iTestX64 / 64];
		
		uint occLo = uint(__occ);
		uint occHi = uint(__v_lshr_b64(__occ, 32));
		occLo = ReadFirstLane(occLo);
		occHi = ReadFirstLane(occHi);
		
		
		__occ = occLo | __s_lshl_b64(occHi, 32); // need this right now because myOcc is coming from RW buffer so it is not read as a static operation, which causes divergence

		while (__occ)
		{
			int iLocalPart = __s_ff1_i32_b64(__occ);

			__occ = __s_andn2_b64(__occ, __s_lshl_b64(1, iLocalPart));

			int iPart = iTestX64 + iLocalPart;
#if COMBINE_MULTIPLE_PARTS_IN_CLASSIFY
			// and get next one if possible
			int iLocalPart1 = __s_ff1_i32_b64(__occ);
			int iPart1 = iTestX64 + iLocalPart1;

			if (iLocalPart1 != -1)
			{
				// case of both active

				__occ = __s_andn2_b64(__occ, __s_lshl_b64(1, iLocalPart1));
#if 1
				{

					CachedPartQuadControl ctrl;

					//otherwise we can read some of the data from currently running threads
					PartQuadControl _ctrl = pSrt->m_particleQuadControlsRO[iMyQuadIndex == 0 ? iPart : iPart1];

	#if PACK_GRADIENT
					ctrl.uv00 = float2(f16tof32(_ctrl.packedGradient.x), f16tof32(_ctrl.packedGradient.y));
					ctrl.uv10 = float2(f16tof32(_ctrl.packedGradient.z), f16tof32(_ctrl.packedGradient.x >> 16));
					ctrl.uv01 = float2(f16tof32(_ctrl.packedGradient.y >> 16), f16tof32(_ctrl.packedGradient.z >> 16));
	#else
					ctrl.uv00 = _ctrl.uv00; ctrl.uv10 = _ctrl.uv10; ctrl.uv01 = _ctrl.uv01;
	#endif

	#if CONSTANT_DEPTH
					ctrl.depth00 = _ctrl.constantDepth;
	#else
	#if PACK_DEPTH
					ctrl.depth00 = f16tof32(_ctrl.packedDepthGradient.x); ctrl.depth10 = f16tof32(_ctrl.packedDepthGradient.y); ctrl.depth01 = f16tof32(_ctrl.packedDepthGradient.x >> 16);
	#else
					ctrl.depth00 = _ctrl.depth00; ctrl.depth10 = _ctrl.depth10; ctrl.depth01 = _ctrl.depth01;
	#endif
	#endif


	#if PACK_COLOR
					ctrl.packedColor = _ctrl.packedColor;
	#endif

					PartRayTraceSetup setup;

					FillInUvParams(ctrl, setup);
					FillInDepthParams(ctrl, setup);
					setup.iPart = iMyQuadIndex == 0 ? iPart : iPart1;
	#if PACK_COLOR
					setup.packedColor = ctrl.packedColor;
	#else
					setup.instanceColor = inst.color;
	#endif

					float ssdepth0 = ssdepth[0];

					ComputeOnePixelResultSimple(setup, ssdepth0, pos0, destArr[0], intersectedAnyFlags, pSrt->m_flags, pSrt->m_linearSampler, pSrt->m_spriteAlpha, pSrt->m_alphaResDropThreshold, pSrt->m_alphaThreshold, 0, false, /*matchThreads=*/ true, /*combineThreads*/ true);
				
					// we need to combine the alpha of two pixels in store it in the result of first quad

					bool allDropRes = intersectedAnyFlags == 0x00000001; // all 4 pixels are 1s, all are pretty opaque

					ulong exec = __s_read_exec();
					ulong lane_mask = __v_cmp_eq_u32(allDropRes, true);

					//if (all(allDropRes))
					//if (exec == lane_mask)
					// her we need to check only first 4 threads since they combine all the colors
					if (0x0000000F == (uint(lane_mask) & 0x0000000F))
					{
						{
							//destArr[0] = float4(iPart / 32.0, 0, 0, 1);
							//float4 c = destArr[0];
							//__image_store(asuint(c), uint4(pos0, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128);
							pSrt->m_particleOccupancy640RW[dispatchIndex].m_resDropIndex = iPart1;
						}
						return; 
					}


				
				}
#endif
			}
			else if (iMyQuadIndex == 0)
#endif
			{
				{

					CachedPartQuadControl ctrl;

					//otherwise we can read some of the data from currently running threads
					PartQuadControl _ctrl = pSrt->m_particleQuadControlsRO[iPart];

	#if PACK_GRADIENT
					ctrl.uv00 = float2(f16tof32(_ctrl.packedGradient.x), f16tof32(_ctrl.packedGradient.y));
					ctrl.uv10 = float2(f16tof32(_ctrl.packedGradient.z), f16tof32(_ctrl.packedGradient.x >> 16));
					ctrl.uv01 = float2(f16tof32(_ctrl.packedGradient.y >> 16), f16tof32(_ctrl.packedGradient.z >> 16));
	#else
					ctrl.uv00 = _ctrl.uv00; ctrl.uv10 = _ctrl.uv10; ctrl.uv01 = _ctrl.uv01;
	#endif

	#if CONSTANT_DEPTH
					ctrl.depth00 = _ctrl.constantDepth;
	#else
	#if PACK_DEPTH
					ctrl.depth00 = f16tof32(_ctrl.packedDepthGradient.x); ctrl.depth10 = f16tof32(_ctrl.packedDepthGradient.y); ctrl.depth01 = f16tof32(_ctrl.packedDepthGradient.x >> 16);
	#else
					ctrl.depth00 = _ctrl.depth00; ctrl.depth10 = _ctrl.depth10; ctrl.depth01 = _ctrl.depth01;
	#endif
	#endif


	#if PACK_COLOR
					ctrl.packedColor = _ctrl.packedColor;
	#endif

					PartRayTraceSetup setup;

					FillInUvParams(ctrl, setup);
					FillInDepthParams(ctrl, setup);
					setup.iPart = iPart;
	#if PACK_COLOR
					setup.packedColor = ctrl.packedColor;
	#else
					setup.instanceColor = inst.color;
	#endif

					float ssdepth0 = ssdepth[0];
					float4 resultPixel;
					ComputeOnePixelResultSimple(setup, ssdepth0, pos0, destArr[0], intersectedAnyFlags, resultPixel, pSrt->m_flags, pSrt->m_linearSampler, pSrt->m_spriteAlpha, pSrt->m_alphaResDropThreshold, pSrt->m_alphaThreshold, 0, false, /*matchThreads=*/ true, /*combineThreads*/ false);
#if DoFroxelsInLowResPass
					// FROXEL BEGIN
#if (FogFroxelGridSizeNativeRes == 16) || (FogFroxelGridSizeNativeRes == 12) || (FogFroxelGridSizeNativeRes == 10) || (FogFroxelGridSizeNativeRes == 8) || (FogFroxelGridSizeNativeRes == 6) || (FogFroxelGridSizeNativeRes == 5) 
					// this thread covers one froxel line
					// this particle can affect multiple froxels
					// we will do simplest case, just accumulate the alpha into one coordinate
					uint3 froxelCoord = uint3(uint2(groupPos * 2 / FogFroxelGridSizeNativeRes) + inWFCellCoord, 0);
#endif
					// note ComputeDepth is called in ComputeOnePixelResultSimple
					float linDepth = GetLinearDepth(setup.depth, pSrt->m_depthParams);
					
					float ndcDepth = setup.depth;

					float froxelSliceFloat = CameraLinearDepthToFroxelZSliceExp(linDepth, pSrt->m_fogGridOffset);
					froxelCoord.z = froxelSliceFloat;

#if (FogFroxelGridSizeNativeRes == 16) || (FogFroxelGridSizeNativeRes == 12) || (FogFroxelGridSizeNativeRes == 10) || (FogFroxelGridSizeNativeRes == 8) || (FogFroxelGridSizeNativeRes == 6) || (FogFroxelGridSizeNativeRes == 5)
					// froxel grid is in native resolution x / 16  & native resolution y / 16, so each froxel grid cell represents 16x16 native pixels
					// so for half resolution, this groupPos is responsible for 4 froxels
					//float opacity = setup.color.a;
					float opacity = resultPixel.a;
#elif FogFroxelGridSizeNativeRes == 32
					// froxel grid is in native resolution x / 32  & native resolution y / 32, so each froxel grid cell represents 32x32 native pixels
					// so for half resolution, this groupPos is responsible for 1 froxel
					// IMPORTANT: we only write one value, not 4 values 
					// TODO: combine them all into one blended value

					float opacity = (ReadLane(resultPixel.a, 0) + ReadLane(resultPixel.a, 1) + ReadLane(resultPixel.a, 2) + ReadLane(resultPixel.a, 3)) / 4.0f;

					//float opacity = setup.color.a;

					if (groupThreadId.x == 0)
#endif

					{
						//inst.invscale = float4(1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f);		// Reciprocal of the particle's half-scale vector


						float2 fromCenter = setup.uv - float2(0.5f, 0.5f);
						float d2 = fromCenter.x * fromCenter.x + fromCenter.y * fromCenter.y;
						
						// auto opacity instead of texture lookup:
						//opacity = 1.0 - sqrt(d2) * 2.0;

						//int numSteps = (1.0 - d2) * 16;
						float numStepsFloat = (0.5 - sqrt(d2))* _ctrl.scaleXAbs;  //* 2.0 // this assumes depth of one froxel as 1m
						int numSteps = numStepsFloat;

						float lastLayerFade = 1.0;

						#if UseExponentialDepth
						float oneSliceDepth = DepthDerivativeAtSlice(froxelSliceFloat);
						
						numSteps = max(numStepsFloat / oneSliceDepth, 0);
						
						lastLayerFade = frac(numStepsFloat / oneSliceDepth);
						numSteps = ceil(numStepsFloat / oneSliceDepth);

						//numSteps = 0;
						#endif

						for (int zoff = -numSteps; zoff <= numSteps; zoff++)
							//for (int zoff = -1; zoff <= 1; zoff++)
						{
							//pSrt->m_destFogFroxels[uint3(froxelCoord.xy, froxelCoord.z + zoff)] = float3(1.5, 12.7, 0.5);

#if 1
							//float tileVolume = pSrt->m_fogTileVolumeFactor * linDepth * 1.0f; // assume tile is 1m deep


							float screenW = 2 * pSrt->m_fogTileVolumeFactor * linDepth;
							float screenH = screenW / 16.0 * 9.0f;

							float volume = screenW * screenH * 1.0f; // assume tile is 1m deep
#if UseExponentialDepth
							volume = volume * oneSliceDepth;
#endif

							float numFroxels = NumFroxelsXY;
							
							float tileVolume = volume / numFroxels;

							uint valueF16 = f32tof16(0.1);
							//valueF16 = valueF16 >> 4;
							//valueF16 = valueF16 << 11;

							const float kMaxParts = 256.0f;
							//const uint kMaxIntPerPart = 0x0000FFFF / 256;
							//uint kMaxIntPerPart = 0x0000FFFF / 32;



							float kMaxIntPerPart = 0x0000FFFF * pSrt->m_particleMaxAccumDensity;
							float kMaxIntPerPart8 = 0x000000FF * pSrt->m_particleMaxAccumDensity;



							//valueF16 = 0x00000001;

							//__image_atomic_add(valueF16, uint4(froxelCoord.xy, froxelCoord.z + zoff, 0),
							//	__get_tsharplo(pSrt->m_destFogFroxels), __get_tsharphi(pSrt->m_destFogFroxels),
							//	__kImage_texture3d);

							float fallOff = (1.0 - float(abs(zoff)) / (numSteps + 1));
							//float density = opacity * fallOff * tileVolume * 50;
							float density = opacity * fallOff * pSrt->m_particleFogContribution;
							
							if (abs(zoff) == numSteps)
								density *= lastLayerFade;

							uint opacityInt = density * kMaxIntPerPart8; //  min(uint(density * kMaxIntPerPart), kMaxIntPerPart);

							uint rInt =  min(kMaxIntPerPart8 * setup.color.r * density, kMaxIntPerPart8);
							uint gInt =  min(kMaxIntPerPart8 * setup.color.g * density, kMaxIntPerPart8);
							uint bInt =  min(kMaxIntPerPart8 * setup.color.b * density, kMaxIntPerPart8);

							uint rg = 0;
							uint ba = (opacityInt << 16) | bInt;

							rg = (gInt << 16) | rInt;

							ulong colorAdd = (ba << 32UL) | rg;

							//__image_atomic_add(colorAdd, uint4(froxelCoord.xy, froxelCoord.z + zoff, 0),
							//	__get_tsharplo(pSrt->m_destFogFroxels), __get_tsharphi(pSrt->m_destFogFroxels),
							//	__kImage_texture3d);

							//uint rgba8 = (opacityInt << 24) | (16 << 16) | (16 << 8) | (16 << 0);
							uint rgba8 = (opacityInt << 24) | (bInt << 16) | (gInt << 8) | (rInt << 0);

							// slow way of doing things
							uint3 coord3 = uint3(froxelCoord.xy, froxelCoord.z + zoff);

#define USE_OLD_BROKEN_WAY 1
							FogTextureData fogValDecompressed = ReadPackedValueFogTexture(pSrt->m_destFogFroxels, coord3);

							#if !USE_OLD_BROKEN_WAY
								float densityDecompressed = FogDensityFromData(fogValDecompressed);
								float3 fogColorDecompressed = FogColorFromData(fogValDecompressed);
							#else
								#if FOG_TEXTURE_DENSITY_ONLY
									float densityDecompressed = pSrt->m_destFogFroxels[coord3];
									float3 fogColorDecompressed = float3(0, 0, 0);;
								#else
									float4 texVal = pSrt->m_destFogFroxels[coord3];
									float densityDecompressed = texVal.a;
									float3 fogColorDecompressed = texVal.rgb;
								#endif
							#endif

							float densityUnpacked = UnpackFroxelDensity(densityDecompressed, pSrt->m_densityUnackFactor);

							#if FOG_TEXTURE_DENSITY_ONLY
								float3 fogColorUnpacked = float3(0, 0, 0);
							#else
								float3 fogColorUnpacked = UnpackFroxelColor(fogColorDecompressed.rgb, pSrt->m_fogExposure);
							#endif

							fogColorUnpacked.r += rInt / 255.0f;
							fogColorUnpacked.g += gInt / 255.0f;
							fogColorUnpacked.b += bInt / 255.0f;

							densityUnpacked += opacityInt / 255.0f;

							float3 fogColorPacked = PackFroxelColor(fogColorUnpacked.rgb, pSrt->m_fogExposure);
							float densityPacked = PackFroxelDensity(densityUnpacked, pSrt->m_densityPackFactor);

							// correct way
							#if !USE_OLD_BROKEN_WAY
							StorePackedValueFogTexture(pSrt->m_destFogFroxels, coord3, float4(fogColorPacked, densityPacked));
							#endif

							// broken way
							#if USE_OLD_BROKEN_WAY
							#if FOG_TEXTURE_DENSITY_ONLY
								pSrt->m_destFogFroxels[coord3] = densityPacked;
							#else
								pSrt->m_destFogFroxels[coord3] = float4(fogColorPacked, densityPacked);
							#endif
							#endif
							

							// atomic with possible overflow
							//__image_atomic_add(rgba8, uint4(froxelCoord.xy, froxelCoord.z + zoff, 0),
							//	__get_tsharplo(pSrt->m_destFogFroxels), __get_tsharphi(pSrt->m_destFogFroxels),
							//	__kImage_texture3d);



							//pSrt->m_destFogFroxels[uint3(froxelCoord.xy, froxelCoord.z + zoff)] = float4(1, 1, 1, 1);


							//__image_store(uint4(asuint(1.0), asuint(0.5), 0xFFFFFFFF, asuint(1.0)), uint4(froxelCoord.xy, froxelCoord.z + zoff, 0),
							//	__get_tsharplo(pSrt->m_destFogFroxels), __get_tsharphi(pSrt->m_destFogFroxels),
							//	__kImage_texture3d);

							//void __image_store(uint4 data, uint4 uv, uint4 tlo, uint4 thi, uint opts);
#endif
						}

					}
					// FROXEL END
#endif

					bool allDropRes = intersectedAnyFlags == 0x00000001; // all 4 pixels are 1s, all are pretty opaque

					ulong exec = __s_read_exec();
					ulong lane_mask = __v_cmp_eq_u32(allDropRes, true);

					//if (all(allDropRes))
					//if (exec == lane_mask)
					if (0x000000000000000F == lane_mask)
					{
						{
							//destArr[0] = float4(iPart / 32.0, 0, 0, 1);
							//float4 c = destArr[0];
							//__image_store(asuint(c), uint4(pos0, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128);
							pSrt->m_particleOccupancy640RW[dispatchIndex].m_resDropIndex = iPart;
						}
						//IMPORTANT:
						// if we don't do froxel stuff here, we can just early out because we found the place where we want to drop resolution
						// TODO: we need to add some flag to not adjust m_resDropIndex after we have foudn the drop resolution
						#if DoFroxelsInLowResPass

						#else
							return; 
						#endif
					}


				
				}
			}
			//break; // just do one particle
		} // while loop going through 64 bit mask
	} // for loop to go through n 64 bit masks

#if USE_DEBUG_CHECKS
	if (pSrt->m_flags & FLAG_RENDER_STATS)
	{
		// reset alpha so that stats are drawn
		destArr[0].a = 0;
	}
#endif

	// SHOULD NEVER GET HERE!
	//if (groupThreadId.x < 4)
	{
		//destArr[0] = float4(0.0, 1.0, 0, 1);
		//float4 c = destArr[0];
		//__image_store(asuint(c), uint4(pos0 + uint2(1, 1), 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128);
	}

	
}


[NUM_THREADS(64 * 4, 1, 1)] // x : particle index, y is
void CS_RayTraceParticlesHalfRes(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleRayTraceReadOnlySrt *pSrt : S_SRT_DATA)
{
	

	int dispatchIndex = groupId.x;

	ParticleQuadOccupancyUlong myOcc = pSrt->m_particleOccupancy[dispatchIndex];

	uint wfIndex = groupThreadId.x / 64;
	uint inWfIndex = groupThreadId.x % 64;
	uint inGroupIndex = inWfIndex;

	uint2 wfInGroup = uint2(wfIndex % 2, wfIndex / 2); // wavefront coordinate in group

	uint quadIndex = inWfIndex / 4;
	uint2 quadPos = uint2(quadIndex % 4, quadIndex / 4) * 2;

	uint2 inWFQuadPos = quadPos + uint2((inWfIndex % 4) % 2, (inWfIndex % 4) / 2);

	//uint2 inWFQuadPos = uint2((inWfIndex % 8), (inWfIndex / 8));

	//uint2 groupPos = uint2(myOcc.m_locationX, myOcc.m_locationY); // offset of the whole group, in target resolution space
	uint2 groupPos = uint2(myOcc.m_locationXY & 0x0000FFFF, (myOcc.m_locationXY >> 16));

	float2 pos0 = groupPos + wfInGroup * uint2(8, 8) + inWFQuadPos;

	//pSrt->m_destTexture0[pos0] = float4(float(inWfIndex) / 64, 0, 0, 0);
	//return;

	int totalOcc = (pSrt->m_numParts > 0 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[0]) : 0)
		+ (pSrt->m_numParts > 64 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[1]) : 0)
		+ (pSrt->m_numParts > 128 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[2]) : 0);
	//+ (pSrt->m_numParts > 192 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[3]) : 0);

	//pSrt->m_destTexture0[pos0] = float4(totalOcc / 192.0, 0, 0, 0.0);
	//pSrt->m_destTexture0[pos0] = float4(myOcc.m_resDropIndex / 32.0, 0, 0, 0.0);
	//pSrt->m_destTexture0[pos0] = float4(quadIndex / 16.0, 0, 0, 0.0);

	//pSrt->m_destTexture0[pos0] = pSrt->m_destTexture0[pos0] * float4(0.999, 0.999, 0.999, 0.999);
	//return;

	//TODO: might need to reenable this
	// this checks that destination is already opaque. since we are only testing one efect right now, this is never the case
	//if (dest.w < 0.01)
	//{
	// we can discard everything
	//	pSrt->m_destTexture0[pos] = float4(dest.xyz, 0.0);

	//	return;
	//}

	float4 destArr[1];

	int numTests64 = (pSrt->m_numParts + 63) / 64;

	float ssdepth[1];// GetLinearDepth(pSrt->m_depthTexture[pos], pSrt->m_depthParams);

	int intersectedAnyFlags = 0;
	uint4 clrtsharplo = __get_tsharplo(pSrt->m_destTexture0);
	uint common_opts = __kImage_RO | __kImage_texture2d | __kImage_R128;
	uint common_opts_write = __kImage_texture2d | __kImage_R128;

	uint4 depthtsharplo = __get_tsharplo(pSrt->m_depthTexture);

	destArr[0] = float4(0, 0, 0, 1); //  pSrt->m_destTexture0[pos0];

	{
#if DO_DEPTH
		image_load_result_t result = __image_load_mip(uint4(pos0, 0, 0), 0, depthtsharplo, 0, common_opts);
		ssdepth[0] = bit_cast<float>(result.data);;
#endif
	}

	// we should never even enter here. this is just for debug, we ultimately dont need this check

	//if (0 == myOcc.m_resDropIndex)
	{
		// this is the last particle we want to render
		{
//			float4 c = destArr[0];
//			__image_store(asuint(c), uint4(pos0, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128);
		}
//		return;
	}
	
	for (int iTestX64 = 0; iTestX64 < numTests64 * 64; iTestX64 += 64)
	{
		ulong __occ = myOcc.m_occupancy64[iTestX64 / 64];

		while (__occ)
		{
			int iLocalPart = __s_ff1_i32_b64(__occ);

			__occ = __s_andn2_b64(__occ, __s_lshl_b64(1, iLocalPart));

			int iPart = iTestX64 + iLocalPart;

			{

				CachedPartQuadControl ctrl;

				//otherwise we can read some of the data from currently running threads
				PartQuadControl _ctrl = pSrt->m_particleQuadControls[iPart];

#if PACK_GRADIENT
				ctrl.uv00 = float2(f16tof32(_ctrl.packedGradient.x), f16tof32(_ctrl.packedGradient.y));
				ctrl.uv10 = float2(f16tof32(_ctrl.packedGradient.z), f16tof32(_ctrl.packedGradient.x >> 16));
				ctrl.uv01 = float2(f16tof32(_ctrl.packedGradient.y >> 16), f16tof32(_ctrl.packedGradient.z >> 16));
#else
				ctrl.uv00 = _ctrl.uv00; ctrl.uv10 = _ctrl.uv10; ctrl.uv01 = _ctrl.uv01;
#endif

#if CONSTANT_DEPTH
				ctrl.depth00 = _ctrl.constantDepth;
#else
#if PACK_DEPTH
				ctrl.depth00 = f16tof32(_ctrl.packedDepthGradient.x); ctrl.depth10 = f16tof32(_ctrl.packedDepthGradient.y); ctrl.depth01 = f16tof32(_ctrl.packedDepthGradient.x >> 16);
#else
				ctrl.depth00 = _ctrl.depth00; ctrl.depth10 = _ctrl.depth10; ctrl.depth01 = _ctrl.depth01;
#endif
#endif


#if PACK_COLOR
				ctrl.packedColor = _ctrl.packedColor;
#endif

				PartRayTraceSetup setup;

				FillInUvParams(ctrl, setup);
				FillInDepthParams(ctrl, setup);
				setup.iPart = iPart;
#if PACK_COLOR
				setup.packedColor = ctrl.packedColor;
#else
				setup.instanceColor = inst.color;
#endif
				float4 resultPixel;
				ComputeOnePixelResultSimple(setup, ssdepth[0], pos0, destArr[0], intersectedAnyFlags, resultPixel, pSrt->m_flags, pSrt->m_linearSampler, pSrt->m_spriteAlpha, pSrt->m_alphaResDropThreshold, pSrt->m_alphaThreshold, 0, true, /*matchThreads=*/ false, /*combineThreads*/ false);

#if ALLOW_ALPHA_ACCUM_DISCARD && 0
				bool allOpaque = intersectedAnyFlags == 0x00000010; // all 4 pixels are 1s, all are pretty opaque

				ulong exec = __s_read_exec();
				ulong lane_mask = __v_cmp_eq_u32(allOpaque, true);

				if ((iPart == myOcc.m_resDropIndex) || allOpaque)
#else
				if (iPart == myOcc.m_resDropIndex)
#endif
				{
					// this is the last particle we want to render
					{
#if USE_DEBUG_CHECKS
						if (pSrt->m_flags & FLAG_RENDER_STATS)
						{
							// reset alpha so that stats are drawn
							destArr[0].a = 0;
						}
#endif
						float4 c = destArr[0];
						__image_store(asuint(c), uint4(pos0, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128);
					}
					return;
				}

				
				/*
				if (exec == lane_mask)
				{
					// all pixels are opaque, we can exit

					float4 c = destArr[0];

#if USE_DEBUG_CHECKS
					if (pSrt->m_flags & FLAG_RENDER_STATS)
					{
						// reset alpha so that stats are drawn
						destArr[0].a = 0;
					}
#endif
						
					float4 c = destArr[0];
					__image_store(asuint(c), uint4(pos0, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128);

					return;
				}
				*/
			}
			//break; // just do one particle
		} // while loop going through 64 bit mask
	} // for loop to go through n 64 bit masks

#if USE_DEBUG_CHECKS
	if (pSrt->m_flags & FLAG_RENDER_STATS)
	{
		// reset alpha so that stats are drawn
		destArr[0].a = 0;
	}
#endif

	float4 c = destArr[0];
	__image_store(asuint(c), uint4(pos0, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128);
}




[NUM_THREADS(64, 1, 1)] // x : particle index, y is
void CS_RayTraceParticlesQuarterRes(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleRayTraceReadOnlySrt *pSrt : S_SRT_DATA)
{
	int dispatchIndex = groupId.x;

	ParticleQuadOccupancyUlong myOcc = pSrt->m_particleOccupancy[dispatchIndex];

	uint wfIndex = groupThreadId.x / 64;
	uint inWfIndex = groupThreadId.x % 64;
	uint inGroupIndex = inWfIndex;

	uint2 wfInGroup = uint2(wfIndex % 2, wfIndex / 2); // wavefront coordinate in group

	uint quadIndex = inWfIndex / 4;
	uint2 quadPos = uint2(quadIndex % 4, quadIndex / 4) * 2;

	uint2 inWFQuadPos = quadPos + uint2((inWfIndex % 4) % 2, (inWfIndex % 4) / 2);

	//uint2 inWFQuadPos = uint2((inWfIndex % 8), (inWfIndex / 8));

	//uint2 groupPos = uint2(myOcc.m_locationX, myOcc.m_locationY); // offset of the whole group, in target resolution space
	uint2 groupPos = uint2(myOcc.m_locationXY & 0x0000FFFF, (myOcc.m_locationXY >> 16));

	float2 pos0 = groupPos + wfInGroup * uint2(8, 8) + inWFQuadPos;

	pos0 = groupPos + inWFQuadPos * 2; // we scale our local coordinate by two because now we cover a bigger whole 16x16 block by one 8x8 wavefront
	pos0 = pos0 + inWFQuadPos / float2(7.0, 7.0);


	//pSrt->m_destTexture0[pos0] = float4(float(inWfIndex) / 64, 0, 0, 0);
	//return;

	int totalOcc = (pSrt->m_numParts > 0 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[0]) : 0)
	+ (pSrt->m_numParts > 64 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[1]) : 0)
	+ (pSrt->m_numParts > 128 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[2]) : 0);
	//+ (pSrt->m_numParts > 192 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[3]) : 0);

	//pSrt->m_destTexture0[pos0] = float4(totalOcc / 192.0, 0, 0, 0.0);
	//pSrt->m_destTexture0[pos0] = float4(quadIndex / 16.0, 0, 0, 0.0);

	//pSrt->m_destTexture0[pos0] = pSrt->m_destTexture0[pos0] * float4(0.999, 0.999, 0.999, 0.999);
	//pSrt->m_destTextureLowerRes[groupPos / 2 + inWFQuadPos] = float4(totalOcc / 192.0, 0, 0, 0.0);
	//return;

	//TODO: might need to reenable this
	// this checks that destination is already opaque. since we are only testing one efect right now, this is never the case
	//if (dest.w < 0.01)
	//{
	// we can discard everything
	//	pSrt->m_destTexture0[pos] = float4(dest.xyz, 0.0);

	//	return;
	//}

	float4 destArr[1];

	int numTests64 = (pSrt->m_numParts + 63) / 64;

	float ssdepth[1];// GetLinearDepth(pSrt->m_depthTexture[pos], pSrt->m_depthParams);

	int intersectedAnyFlags = 0;
	uint4 clrtsharplo = __get_tsharplo(pSrt->m_destTexture0);
	uint common_opts = __kImage_RO | __kImage_texture2d | __kImage_R128;
	uint common_opts_write = __kImage_texture2d | __kImage_R128;

	uint4 depthtsharplo = __get_tsharplo(pSrt->m_depthTexture);

	destArr[0] = float4(0, 0, 0, 1); //  pSrt->m_destTexture0[pos0];

	{
#if DO_DEPTH
		// TODO: we need to sample quarter depth here
		image_load_result_t result = __image_load_mip(uint4(pos0, 0, 0), 0, depthtsharplo, 0, common_opts);
		ssdepth[0] = bit_cast<float>(result.data);
#endif
	}

	int iTestX64 = (myOcc.m_resDropIndex / 64) * 64;

	// myOcc.m_resDropIndex is teh last particle rendered at higher res, so we need to cancel that particle and all preceding particles in the current mask

	ulong __occ = myOcc.m_occupancy64[iTestX64 / 64];

	if (myOcc.m_resDropIndex == 255)
		return;
	
	int iLastLocalPart = myOcc.m_resDropIndex % 64; // iLastLocalPart is the last particle in current test that was rendered at higher resolution

	// this is the mask that represents all particles rendered so far
	// if last particle rendered was 0, we get mask 00000000000000001
	// if last particle was one we get 0000000000001, etc
	
	ulong maskUpToLastPart = __s_lshr_b64(0xFFFFFFFFFFFFFFFF, (63 - iLastLocalPart));
	__occ = __s_andn2_b64(__occ, maskUpToLastPart);

	while (true)
	{
		
		while (__occ)
		{
			int iLocalPart = __s_ff1_i32_b64(__occ);

			__occ = __s_andn2_b64(__occ, __s_lshl_b64(1, iLocalPart));

			int iPart = iTestX64 + iLocalPart;

			//if (numToSkip <= 0)
			{
				
				CachedPartQuadControl ctrl;

				//otherwise we can read some of the data from currently running threads
				PartQuadControl _ctrl = pSrt->m_particleQuadControls[iPart];

#if PACK_GRADIENT
				ctrl.uv00 = float2(f16tof32(_ctrl.packedGradient.x), f16tof32(_ctrl.packedGradient.y));
				ctrl.uv10 = float2(f16tof32(_ctrl.packedGradient.z), f16tof32(_ctrl.packedGradient.x >> 16));
				ctrl.uv01 = float2(f16tof32(_ctrl.packedGradient.y >> 16), f16tof32(_ctrl.packedGradient.z >> 16));
#else
				ctrl.uv00 = _ctrl.uv00; ctrl.uv10 = _ctrl.uv10; ctrl.uv01 = _ctrl.uv01;
#endif

#if CONSTANT_DEPTH
				ctrl.depth00 = _ctrl.constantDepth;
#else
#if PACK_DEPTH
				ctrl.depth00 = f16tof32(_ctrl.packedDepthGradient.x); ctrl.depth10 = f16tof32(_ctrl.packedDepthGradient.y); ctrl.depth01 = f16tof32(_ctrl.packedDepthGradient.x >> 16);
#else
				ctrl.depth00 = _ctrl.depth00; ctrl.depth10 = _ctrl.depth10; ctrl.depth01 = _ctrl.depth01;
#endif
#endif


#if PACK_COLOR
				ctrl.packedColor = _ctrl.packedColor;
#endif

				PartRayTraceSetup setup;

				FillInUvParams(ctrl, setup);
				FillInDepthParams(ctrl, setup);
				setup.iPart = iPart;
#if PACK_COLOR
				setup.packedColor = ctrl.packedColor;
#else
				setup.instanceColor = inst.color;
#endif
				float4 resultPixel;
				ComputeOnePixelResultSimple(setup, ssdepth[0], pos0, destArr[0], intersectedAnyFlags, resultPixel, pSrt->m_flags, pSrt->m_linearSampler, pSrt->m_spriteAlpha, pSrt->m_alphaResDropThreshold, pSrt->m_alphaThreshold, 0, false, /*matchThreads=*/ false, /*combineThreads*/ false);

#if ALLOW_ALPHA_ACCUM_DISCARD
				bool allOpaque = intersectedAnyFlags == 0x00000010; // all 4 pixels are 1s, all are pretty opaque

				ulong exec = __s_read_exec();
				ulong lane_mask = __v_cmp_eq_u32(allOpaque, true);

				if (allOpaque)
				{
					// all pixels are opaque, we can exit

					uint2 lowResPos = groupPos / 2 + inWFQuadPos;
					float4 c = destArr[0];

#if USE_DEBUG_CHECKS
					if (pSrt->m_flags & FLAG_RENDER_STATS)
					{
						// since we want to show that we rendered less, we will store this data only in quarter of the destination pixels
						// reset alpha so that stats are drawn
						if (uint(pos0.x) % 8 < 4)
						{
							if (uint(pos0.y) % 8 < 4)
							{
								pSrt->m_destTextureLowerRes[lowResPos] = c;
							}
						}
					}
					else
#endif
					{
						pSrt->m_destTextureLowerRes[lowResPos] = c;
					}
					return;
				}
#endif

			}

			//break; // just do one particle
		} // while loop going through 64 bit mask

		iTestX64 += 64;

		if (iTestX64 >= numTests64 * 64)
			break;

		__occ = myOcc.m_occupancy64[iTestX64 / 64];
	}

#if USE_DEBUG_CHECKS
	if (pSrt->m_flags & FLAG_RENDER_STATS)
	{
		// reset alpha so that stats are drawn
		destArr[0].a = 0;
	}
#endif

	uint2 lowResPos = groupPos / 2 + inWFQuadPos;

	float4 c = destArr[0];

#if USE_DEBUG_CHECKS
	if (pSrt->m_flags & FLAG_RENDER_STATS)
	{
		// since we want to show that we rendered less, we will store this data only in quarter of the destination pixels
		// reset alpha so that stats are drawn
		if (uint(pos0.x) % 8 < 4)
		{
			if (uint(pos0.y) % 8 < 4)
			{
				pSrt->m_destTextureLowerRes[lowResPos] = c;
			}
		}
	}
	else
#endif
	{
		pSrt->m_destTextureLowerRes[lowResPos] = c;
	}

}



void ComputeOneFogResultSimple(inout PartRayTraceSetup setup, float ssdepth, float2 pos,
	inout float4 destColorStorage,
	inout int intersectedAnyFlags,
	out float4 resultPixel,
	uint flags, SamplerState linearSampler, Texture2D<float4> spriteAlpha,
	float alphaResDropThreshold, float alphaThreshold,
	uint index, bool doAlphaAccumCheck, bool matchThreads, bool combineThreads)
{
	float4 destColor = destColorStorage;

	setup.pos0 = pos;

	ComputeUv(setup);
	ComputeDepth(setup);
	ComputeColor(setup);

	bool depthOk = true; // DepthCheck(setup, ssdepth);

	resultPixel = float4(0.0, 0.0, 0.0, 0.0);

	if (depthOk)
	{



#if 1
				resultPixel = float4(1.0, 0, 0, 0.1);
#endif

					// blending result with destination
					// note, we are not doing anything for alpha = 0, because we don't want to intr0duce another branch
					// we assume it is unlikely that all threads will have alpha 0, so we will still spend blending cost
					ForwardBlendColor(destColor, resultPixel, flags);


					if (!doAlphaAccumCheck)
					{
						// when this, we check for res drop threshold
#if USE_DEBUG_CHECKS
						if (destColor.a < alphaResDropThreshold)
#else
						if (destColor.a < 0.5)
#endif
						{
							// very little of bg is seen anymore
							// set the flag
							intersectedAnyFlags = intersectedAnyFlags | (1 << index);
						}
					}

					if (doAlphaAccumCheck)
					{
#if USE_DEBUG_CHECKS
						if (destColor.a < alphaThreshold)
#else
						if (destColor.a < 0.02)
#endif
						{
							// very little of bg is seen anymore
							// set the flag
							intersectedAnyFlags = intersectedAnyFlags | (1 << (4 + index));
						}
					}


					// store it back
					destColorStorage = destColor;
	} // if depthOk

	if (combineThreads)
	{
		// read data from adjacent quads and combine colors
		LaneSwizzle(resultPixel.r, 0x1F, 0, 0x04); // that will grab corresponding pixel from a different quad
		LaneSwizzle(resultPixel.g, 0x1F, 0, 0x04); // that will grab corresponding pixel from a different quad
		LaneSwizzle(resultPixel.b, 0x1F, 0, 0x04); // that will grab corresponding pixel from a different quad
		LaneSwizzle(resultPixel.a, 0x1F, 0, 0x04); // that will grab corresponding pixel from a different quad

		ForwardBlendColor(destColor, resultPixel, flags);



		if (!doAlphaAccumCheck)
		{
			// when this, we check for res drop threshold
#if USE_DEBUG_CHECKS
			if (destColor.a < alphaResDropThreshold)
#else
			if (destColor.a < 0.5)
#endif
			{
				// very little of bg is seen anymore
				// set the flag
				intersectedAnyFlags = intersectedAnyFlags | (1 << index);
			}
		}

		if (doAlphaAccumCheck)
		{
#if USE_DEBUG_CHECKS
			if (destColor.a < alphaThreshold)
#else
			if (destColor.a < 0.02)
#endif
			{
				// very little of bg is seen anymore
				// set the flag
				intersectedAnyFlags = intersectedAnyFlags | (1 << (4 + index));
			}
		}


		destColorStorage = destColor;
	}
}

//[NUM_THREADS(64 * 4, 1, 1)]
[NUM_THREADS(8, 8, 1)]
void CS_RayTraceFogHalfRes(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	VolumetricsApplyFroxelsToDepthSrt *pSrt : S_SRT_DATA)
{
	float2 pos0 = dispatchId.xy;

	float4 destArr[1];
		
	float ssdepth[1];// GetLinearDepth(pSrt->m_depthTexture[pos], pSrt->m_depthParams);

	int intersectedAnyFlags = 0;
	uint4 clrtsharplo = __get_tsharplo(pSrt->m_destTexture0);
	uint4 clrtsharphi = __get_tsharphi(pSrt->m_destTexture0);
	uint common_opts = __kImage_RO | __kImage_texture2d;

	uint4 depthtsharplo = __get_tsharplo(pSrt->m_opaqueDepthTexture);
	uint4 depthtsharphi = __get_tsharphi(pSrt->m_opaqueDepthTexture);

	destArr[0] = float4(0, 0, 0, 1); //  pSrt->m_destTexture0[pos0];

	{
		// todo: we need a third combination of neo + regular resolution of this shader to use this optimization
#if 1// IS_NEO_MODE
		image_load_result_t result = __image_load_mip(uint4(pos0, 0, 0), 0, depthtsharplo, depthtsharphi, common_opts);
		ssdepth[0] = bit_cast<float>(result.data);;
#else
		// with 1080p, we don't need extra stride
		image_load_result_t result = __image_load_mip(uint4(pos0, 0, 0), 0, depthtsharplo, 0, common_opts | __kImage_R128);
		ssdepth[0] = bit_cast<float>(result.data);;
#endif
	}
	{
		{
			PartRayTraceSetup setup;

			float3 uvw;
			uvw.xy = pos0.xy / float2(SCREEN_NATIVE_RES_W_F / 2.0, SCREEN_NATIVE_RES_H_F / 2.0);

			uvw.z = CameraLinearDepthToFroxelZCoordExp(GetLinearDepth(ssdepth[0], pSrt->m_depthParams), 0);

			float4 resultPixel;

			// note we shouldnt be doing this line because it is for sampling as SH , not direct accumulation
			//density = clamp(density - pSrt->g_particleFogControls0.x, pSrt->g_particleFogControls0.z, pSrt->g_particleFogControls0.w);


			resultPixel.rgba = pSrt->m_srcFogFroxelsTemp.SampleLevel(pSrt->m_linearSampler, uvw, 0).xyzw;

			float density = resultPixel.a; //  clamp(resultPixel.a, 0, 1); // note no reason to clamp since texture is unorm
			// in alpha we need to store how much of background we can see
			resultPixel.a = density;

			//if (density > 0.0001)
			//	resultPixel.rgb = resultPixel.rgb / density;

			//resultPixel.rgb = uvw.zzz;
			//resultPixel.a = 1.0;

			//resultPixel.a = 0.9;

			// blending result with destination
			// note, we are not doing anything for alpha = 0, because we don't want to introduce another branch
			// we assume it is unlikely that all threads will have alpha 0, so we will still spend blending cost
			//ForwardBlendColor(destArr[0], resultPixel, pSrt->m_flags);
			destArr[0] = resultPixel;

			//ComputeOneFogResultSimple(setup, ssdepth[0], pos0, destArr[0], intersectedAnyFlags, resultPixel, pSrt->m_flags, pSrt->m_linearSampler, pSrt->m_spriteAlpha, pSrt->m_alphaResDropThreshold, pSrt->m_alphaThreshold, 0, /*doAlphaAccumCheck=*/ false, /*matchThreads=*/ false, /*combineThreads*/ false);
		}
		//break; // just do one iteration

	} // for loop to go through n 64 bit masks

	float4 c = destArr[0];
	// todo: we need a third combination of neo + regular resolution of this shader to use this optimization
#if 1// IS_NEO_MODE
	__image_store(asuint(c), uint4(pos0, 0, 0), __get_tsharplo(pSrt->m_destTexture0), __get_tsharphi(pSrt->m_destTexture0), __kImage_texture2d);
#else
	__image_store(asuint(c), uint4(pos0, 0, 0), __get_tsharplo(pSrt->m_destTexture0), 0, __kImage_texture2d | __kImage_R128);
#endif
}


