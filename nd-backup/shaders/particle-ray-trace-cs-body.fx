
{

	int totalOcc = (pSrt->m_numParts > 0 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[0]) : 0)
		+ (pSrt->m_numParts > 64 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[1]) : 0)
		+ (pSrt->m_numParts > 128 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[2]) : 0);
		//+ (pSrt->m_numParts > 192 ? __s_bcnt1_i32_b64(myOcc.m_occupancy64[3]) : 0);

	//pSrt->m_destTexture0[pos0] = float4(totalOcc / 256.0, 0, 0, 0.0);
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
					bool allDropRes = intersectedAnyFlags == 0x0000000F; // all 4 pixels are 1s, all are pretty opaque

					ulong exec = __s_read_exec();
					ulong lane_mask = __v_cmp_eq_u32(allDropRes, true);

					//if (all(allDropRes))
					//if (exec == lane_mask)
					if (0xFFFFFFFFFFFFFFFF == lane_mask)
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


