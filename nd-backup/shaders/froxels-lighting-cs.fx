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
#include "tetra-shadows.fxi"

#define ND_PSSL 1
#define USE_EXPANDED_COMPRESSED_PROBE 1
#include "vol-probe-lighting-cs.fx"
#define PROBE_DATA_ALREADY_DEFINED
#include "particle-ray-trace-cs-defines.fxi"
#include "particle-ray-trace-cs-ps.fxi"

void swap(inout float x, inout float y)
{
	float t = x;
	x = y;
	y = t;
}

#define TRIANGLE_TEXTURE_XY_DIM_F 64.0
#define TRIANGLE_TEXTURE_Z_DIM_F 96.0
#define TEXTURE_FACTOR 4.0

#define CIRCLE_TEXTURE_FACTOR (1.0/2.0)
float EvalTriangleCoord(VolumetricsFogSrt *pSrt, float2 coord)
{
	float x = coord.x;// *2.0;
	float y = coord.y;// *2.0; // -0.5 .. 0.5
	float slope = 0.5f;

	float coneFactor = saturate((slope * x - abs(y)) / (slope * x));
	float distanceFromStart = sqrt(x*x + y * y);
	float fallOffFactor = saturate(1.0 - distanceFromStart); // x could be more than 1.0
	fallOffFactor = pSrt->m_triangleFalloffPower > 0 ? pow(fallOffFactor, pSrt->m_triangleFalloffPower) : 1.0;

	float falloffQuadratic = saturate(1.12 / (1 + pow(3 * distanceFromStart, 2)) - 0.12);
	falloffQuadratic = pSrt->m_triangleFalloffPower > 0 ? pow(falloffQuadratic, pSrt->m_triangleFalloffPower) : 1.0;
	// true/false
	float inside = abs(y) < slope * x && x <= 1.0;

	float tooClose = saturate((x - (pSrt->m_triangleTooCloseDist - 0.2)) / 0.2);

	// storing 1-falloffIntegral
	//return 1.0 - inside;
	float coneEdgeStart = 1.0f;

	float coneEdge = saturate(coneFactor / coneEdgeStart);
	//return inside;
	//return coneFactor;

	coneEdge = pSrt->m_triangleConePower > 0 ? pow(coneEdge, pSrt->m_triangleConePower) : 1.0;

	return inside * falloffQuadratic * coneEdge * tooClose;
	// no cone
	//return inside * fallOffFactor * fallOffFactor * tooClose;

	//return 1.0 - fallOffFactor * fallOffFactor;
	//return 1.0 - fallOffFactor * fallOffFactor;
	//return saturate(x); // distance from origin as 0..1
}


float EvalTriangleCoord3D(VolumetricsFogSrt *pSrt, float2 coord, float3 triFw, float3 triUp)
{
	float x = coord.x;// *2.0;
	float y = coord.y;// *2.0; // -0.5 .. 0.5
	float slope = 0.5f;

	float3 pos = triFw * x + triUp * y;

	float3 dir = normalize(pos);

	float distanceFromStart = length(pos);

	float falloffQuadratic = saturate(1.12 / (1 + pow(3 * distanceFromStart, 2)) - 0.12);
	falloffQuadratic = pSrt->m_triangleFalloffPower > 0 ? pow(falloffQuadratic, pSrt->m_triangleFalloffPower) : 1.0;


	float angleToCone = acos(clamp(dot(float3(0, 0, 1), dir), -1.0, 1.0));

	float kConeHalfAngle = atan(0.5);

	float coneFactor = saturate((kConeHalfAngle - angleToCone) / kConeHalfAngle);

	coneFactor = pSrt->m_triangleConePower > 0 ? pow(coneFactor, pSrt->m_triangleConePower) : 1.0;
	//coneFactor = min(coneFactor, 0.95);
	float tooClose = saturate((x - (pSrt->m_triangleTooCloseDist - 0.2)) / 0.2);

	//return angleToCone < kConeHalfAngle;
	return coneFactor * falloffQuadratic * tooClose;

}

[NUM_THREADS(8, 8, 1)] //
void CS_VolumetricsCreateTriangleTextures(const uint3 _dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	uint3 dispatchId = _dispatchId;
	dispatchId.z += pSrt->m_trianglezCoordOffset;

	float coordAlongX = (dispatchId.y + 0.5) / TRIANGLE_TEXTURE_XY_DIM_F;
	float coordAlongY = (dispatchId.x + 0.5) / TRIANGLE_TEXTURE_XY_DIM_F - 0.5f;

	float totalSlicesHalfAngle = atan(0.5);
	float totalSlicesAngle = totalSlicesHalfAngle * 2;

	float sliceStep = totalSlicesHalfAngle / (VOLUMETRICS_MAX_CONE_SLICES - 1);

	// IMPORTANT: first slice is always bound as black texture, since it is the edge of cone. we don't need to generate it
	int iSlice = dispatchId.z / int(TRIANGLE_TEXTURE_Z_DIM_F) + 1; 
	float sliceAngle = -totalSlicesHalfAngle + sliceStep * iSlice; // going from outer edge towards the center

	float kConeHeight = 1.0;
	float kConeBaseR = 0.5f;
	float kConeSideLen = sqrt(kConeHeight * kConeHeight + kConeBaseR * kConeBaseR);

	float h = tan(abs(sliceAngle)) * kConeHeight;
	float triHeight = sqrt(h * h + kConeHeight * kConeHeight);

	float3 coneDir = float3(0, 0, 1);
	float3 coneBase = float3(0, 0, 1);
	float3 triBase = coneBase + float3(0, 1, 0) * h;
	float3 triFw = triBase - float3(0, 0, 0);
	float3 triUp = dot(triFw, coneDir) < 0.999 ? cross(triFw, coneDir) : float3(1.0, 0, 0);
	float triBaseR = sqrt(max(kConeSideLen * kConeSideLen - triHeight * triHeight, 0));
	triUp = length(triUp) > 0.0001 ? normalize(triUp) * triBaseR * 2.0 : triUp; // scale it such that going to 0.5 goes to the edge of cone

	// we start angles pointing into light source

	float2 rayDir = float2(-1, 0);

	// now rotate it depending on what angle we have

	int iIntegralDirAngle = dispatchId.z % int(TRIANGLE_TEXTURE_Z_DIM_F);
	
	float angle = 2.0 * 3.1415926 / (TRIANGLE_TEXTURE_Z_DIM_F /* - 1.0*/) * iIntegralDirAngle; // we start and end at same angle


	rayDir.x = -cos(angle);
	rayDir.y = sin(angle);

	// now we march backwards to find out how much has accumulated up to this location


	float stepLen = 1 / TRIANGLE_TEXTURE_XY_DIM_F / 4.0;

	float2 currentCoord = float2(coordAlongX, coordAlongY);
	
	float accum = 0;
	while (currentCoord.x >= 0.0 && currentCoord.x <= 1.0 && currentCoord.y >= -0.5 && currentCoord.y <= 0.5)
	{
		// evaluate current location
		accum += EvalTriangleCoord3D(pSrt, currentCoord, triFw, triUp) * stepLen;

		currentCoord = currentCoord - rayDir * stepLen;
	}

	// write the result into the texture
	pSrt->m_triangleTextures[iSlice][int3(dispatchId.xy, iIntegralDirAngle)] = sqrt(accum * TEXTURE_FACTOR); // makes small numbers bigger to use more of range
}

[NUM_THREADS(8, 8, 1)] //
void CS_VolumetricsCreateTriangleTexture(const uint3 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	float coordAlongX = (dispatchId.y + 0.5) / TRIANGLE_TEXTURE_XY_DIM_F;
	float coordAlongY = (dispatchId.x + 0.5) / TRIANGLE_TEXTURE_XY_DIM_F - 0.5f;

	// we start angles pointing into light source

	float2 rayDir = float2(-1, 0);

	// now rotate it depending on what angle we have

	float angle = 2.0 * 3.1415926 / (TRIANGLE_TEXTURE_Z_DIM_F /* - 1.0*/) * dispatchId.z; // we start and end at same angle


	rayDir.x = -cos(angle);
	rayDir.y = sin(angle);

	// now we march backwards to find out how much has accumulated up to this location


	float stepLen = 1 / TRIANGLE_TEXTURE_XY_DIM_F / 4.0;

	float2 currentCoord = float2(coordAlongX, coordAlongY);

	float accum = 0;
	while (currentCoord.x >= 0.0 && currentCoord.x <= 1.0 && currentCoord.y >= -0.5 && currentCoord.y <= 0.5)
	{
		// evaluate current location
		accum += EvalTriangleCoord(pSrt, currentCoord) * stepLen;

		currentCoord = currentCoord - rayDir * stepLen;
	}

	// write the result into the texture
	pSrt->m_triangleTexture[dispatchId] = accum;
}

float EvalCircleCoord(VolumetricsFogSrt *pSrt, float2 coord, uint param)
{
	float falloffPower = (param == 0) ? (pSrt->m_circleFalloffPower) : (param == 1 ? pSrt->m_circlePart0FalloffPower : pSrt->m_circlePart1FalloffPower);
	float falloffStart = (param == 0) ? (pSrt->m_circleFalloffStart) : (param == 1 ? pSrt->m_circlePart0FalloffStart : pSrt->m_circlePart1FalloffStart);
	float falloffEnd = (param == 0) ? (pSrt->m_circleFalloffEnd) : (param == 1 ? pSrt->m_circlePart0FalloffEnd : pSrt->m_circlePart1FalloffEnd);

	float x = coord.x;// -0.5 .. 0.5
	float y = coord.y;// 
	float r = 0.5f;

	float distanceFromCenter = sqrt(x*x + (y) * (y)) * 2; // 0..1

	distanceFromCenter = saturate(max(0, distanceFromCenter - falloffStart) / (falloffEnd - falloffStart)); // rescale to 0..1 starting at falloff start

	float falloffQuadratic = saturate(1.12 / (1 + pow(3 * distanceFromCenter, 2)) - 0.12);
	falloffQuadratic = falloffPower > 0 ? pow(falloffQuadratic, falloffPower) : 1.0;
	// true/false
	float inside = distanceFromCenter < 1.0;

	//return 1.0;
	//return inside;
	
	return inside * falloffQuadratic;
}


[NUM_THREADS(8, 8, 1)] //
void CS_VolumetricsCreateCircleTexture(const uint3 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	float coordAlongX = (dispatchId.x + 0.5) / TRIANGLE_TEXTURE_XY_DIM_F - 0.5f;
	float coordAlongY = (dispatchId.y + 0.5) / TRIANGLE_TEXTURE_XY_DIM_F - 0.5f;

	// we start angles pointing into light source

	float2 rayDir = float2(0, 1);

	

	float stepLen = 1 / TRIANGLE_TEXTURE_XY_DIM_F / 4.0;

	float2 currentCoord = float2(coordAlongX, coordAlongY);

	float accum = 0;
	while (currentCoord.y >= -0.5 && currentCoord.y <= 0.5 && currentCoord.x >= -0.5 && currentCoord.x <= 0.5)
	{
		// evaluate current location
		accum += EvalCircleCoord(pSrt, currentCoord, 0) * stepLen;

		currentCoord = currentCoord - rayDir * stepLen;
	}

	// write the result into the texture
	pSrt->m_circleTexture[dispatchId.xy] = sqrt(accum * CIRCLE_TEXTURE_FACTOR); // values range 0..2, mutliply by smal number to fit inside 0..1
}

[NUM_THREADS(8, 8, 1)] //
void CS_VolumetricsCreateCircleTexturePart0(const uint3 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	float coordAlongX = (dispatchId.x + 0.5) / TRIANGLE_TEXTURE_XY_DIM_F - 0.5f;
	float coordAlongY = (dispatchId.y + 0.5) / TRIANGLE_TEXTURE_XY_DIM_F - 0.5f;

	// we start angles pointing into light source

	float2 rayDir = float2(0, 1);



	float stepLen = 1 / TRIANGLE_TEXTURE_XY_DIM_F / 4.0;

	float2 currentCoord = float2(coordAlongX, coordAlongY);

	float accum = 0;
	while (currentCoord.y >= -0.5 && currentCoord.y <= 0.5 && currentCoord.x >= -0.5 && currentCoord.x <= 0.5)
	{
		// evaluate current location
		accum += EvalCircleCoord(pSrt, currentCoord, 1) * stepLen;

		currentCoord = currentCoord - rayDir * stepLen;
	}

	// write the result into the texture
	pSrt->m_circlePart0Texture[dispatchId.xy] = sqrt(accum * CIRCLE_TEXTURE_FACTOR); // values range 0..2, mutliply by smal number to fit inside 0..1
}

[NUM_THREADS(8, 8, 1)] //
void CS_VolumetricsCreateCircleTexturePart1(const uint3 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	float coordAlongX = (dispatchId.x + 0.5) / TRIANGLE_TEXTURE_XY_DIM_F - 0.5f;
	float coordAlongY = (dispatchId.y + 0.5) / TRIANGLE_TEXTURE_XY_DIM_F - 0.5f;

	// we start angles pointing into light source

	float2 rayDir = float2(0, 1);



	float stepLen = 1 / TRIANGLE_TEXTURE_XY_DIM_F / 4.0;

	float2 currentCoord = float2(coordAlongX, coordAlongY);

	float accum = 0;
	while (currentCoord.y >= -0.5 && currentCoord.y <= 0.5 && currentCoord.x >= -0.5 && currentCoord.x <= 0.5)
	{
		// evaluate current location
		accum += EvalCircleCoord(pSrt, currentCoord, 2) * stepLen;

		currentCoord = currentCoord - rayDir * stepLen;
	}

	// write the result into the texture
	pSrt->m_circlePart1Texture[dispatchId.xy] = sqrt(accum /* * CIRCLE_TEXTURE_FACTOR*/); // values range 0..2, mutliply by smal number to fit inside 0..1
}

float4 asfloat4(uint4 v)
{
	return float4(asfloat(v.x), asfloat(v.y), asfloat(v.z), asfloat(v.w));
}

void GenerateTileFrustum(uint2 groupId, float minDepth, float maxDepth, VolumetricsFogSrt *pSrt, out float4 planes[6])
{
	// Get the VS position on the near plane of the tile top left corner
	float3 tileStartVS = pSrt->m_topLeftCornerVS + float3(groupId*pSrt->m_tileSizeVS, 0);
	float3 tileEndVS = tileStartVS + float3(pSrt->m_tileSizeVS, 0);

	planes[0] = (float4(normalize(cross(tileStartVS, float3(0, 1, 0))), 0));
	planes[1] = (float4(normalize(cross(tileEndVS, float3(0, -1, 0))), 0));
	planes[2] = (float4(normalize(cross(tileStartVS, float3(-1, 0, 0))), 0));
	planes[3] = (float4(normalize(cross(tileEndVS, float3(1, 0, 0))), 0));
	// The compiler is smart enough to optimize all the unnecessary math by having 0 components
	// below. This simplifying the code and removing the need to specially handle the near and far planes
	planes[4] = float4(0, 0, 1, minDepth);
	planes[5] = float4(0, 0, -1, -maxDepth);
}

[NUM_THREADS(8, 8, 1)] //
void CS_VolumetricsCreateSSLights(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	uint2 groupPos = uint2(8, 8) * groupId.xy;

	uint2 pos = groupPos + groupThreadId.xy;

	uint flatGroupThreadId = groupThreadId.y * 8 + groupThreadId.x;

#if FROXELS_CULL_PARTICLES
	int lastSliceToEverBeSampled = min(63, int(pSrt->m_srcVolumetricsScreenSpaceInfo[pos.xy].x) + 1);

	float viewZEnd = FroxelZSliceToCameraDistExp(lastSliceToEverBeSampled + 1.0, pSrt->m_fogGridOffset);

	float4 planes[6];
	GenerateTileFrustum(pos, 0, viewZEnd, pSrt, planes);

	uint partIntersectionMask = 0;

	
	// for each particle, do culling
	for (int iPart = 0; iPart < pSrt->m_numPartVolumes; ++iPart)
	{
		uint intersects = (1 << iPart);

		VolumetricFogVolume lightDesc = pSrt->m_volPartsRO[iPart];

		//switch (lightType)
		{
			//case kPointLight:
			{
				// Simple frustum sphere intersection
				for (uint i = 0; i < 6; ++i)
				{
					float distanceToPlane = dot(planes[i].xyz, lightDesc.m_viewPos) -
						planes[i].w;

					// TODO: Investigate early exit? Quite likely that all the lights don't intersect
					// some tiles
					if (distanceToPlane < -lightDesc.m_radius)
					{
						intersects = 0;
					}
				}
			}
			//break;
		}

		partIntersectionMask = partIntersectionMask | intersects;
	}

#endif


	uint froxelFlatIndex = mul24(pSrt->m_numFroxelsXY.x, pos.y) + pos.x;

	uint numTilesInWidth = (pSrt->m_numFroxelsXY.x + 7) / 8;

	uint tileFlatIndex = mul24(numTilesInWidth, groupId.y) + groupId.x;

	VolumetrciSSLightFroxelData resFroxelData;
	VolumetrciSSLightTileData resLightTileData;

	float2 pixelPosNormNoJit = float2(pos.x + 0.5, pos.y + 0.5) * float2(pSrt->m_numFroxelsXInv, pSrt->m_numFroxelsXInv * 16.0 / 9.0);

	uint2 pixelPos00 = pos * uint2(pSrt->m_froxelSizeU, pSrt->m_froxelSizeU);
	uint2 pixelPos11 = pixelPos00 + uint2(pSrt->m_froxelSizeU, pSrt->m_froxelSizeU) - uint2(1, 1);
	pixelPos11.x = min(pixelPos11.x, pSrt->m_screenWU-1);

	uint lightIntersectionMask = 0;
	#if FROXELS_CULL_PARTICLES
	lightIntersectionMask = partIntersectionMask;
	#endif

	// we don't want to early out from the whole compute shader if we are out of bound, instead we are going to stay alive with valid result values (no intersection) to make sure that cross lane operations have defined behavior for the hwole tile
	bool validFroxel = pos.x < pSrt->m_numFroxelsXY.x && pos.y < pSrt->m_numFroxelsXY.y;
	
	// bit 0: shadowed vs not shadowed
	// bit 1 : point light
	// bit 2 : spotlight or projector

	uint lightTypeMask = 0; 
	#if !FROXELS_CULL_PARTICLES




	if (pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_numLights && validFroxel)
	{
		uint tileX0 = pixelPos00.x / TILE_SIZE;
		uint tileX1 = pixelPos11.x / TILE_SIZE;
		uint tileY0 = pixelPos00.y / TILE_SIZE;
		uint tileY1 = pixelPos11.y / TILE_SIZE;


		uint combinedList = 0;

		for (uint tileY = tileY0; tileY <= tileY1; ++tileY)
		{
			for (uint tileX = tileX0; tileX <= tileX1; ++tileX)
			{
				uint tileIndex = mul24(tileY, uint(pSrt->m_screenWU) / TILE_SIZE) + tileX;

				uint numLights = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_numLightsPerTileBufferRO[tileIndex];

				if (numLights)
				{
					//haveAnyLights = true;
				}

				for (uint i = 0; i < numLights; ++i)
				{
					uint lightIndex = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_lightsPerTileBufferRO[tileIndex*MAX_LIGHTS_PER_TILE + i];

					lightIntersectionMask = lightIntersectionMask | (1 << lightIndex);

					const LightDesc lightDesc = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_lights[lightIndex];

					if (lightDesc.m_localShadowTextureIdx != kInvalidLocalShadowTextureIndex)
					{
						// have a shadow
						//haveShadowLights = true;

						lightTypeMask = lightTypeMask | (1 << 0); // 
					}
				}
			}
		}
	}
	#endif
	// now do parallel regression to combine masks for whole tile
	uint combinedLightIntersectionMask = 0;

	{
		uint combinedInt_01 = (lightIntersectionMask | LaneSwizzle(lightIntersectionMask, 0x1f, 0x00, 0x01));
		uint combinedInt_02 = (combinedInt_01 | LaneSwizzle(combinedInt_01, 0x1f, 0x00, 0x02));
		uint combinedInt_04 = (combinedInt_02 | LaneSwizzle(combinedInt_02, 0x1f, 0x00, 0x04));
		uint combinedInt_08 = (combinedInt_04 | LaneSwizzle(combinedInt_04, 0x1f, 0x00, 0x08));
		uint combinedInt_10 = (combinedInt_08 | LaneSwizzle(combinedInt_08, 0x1f, 0x00, 0x10));
		uint combinedInt = (ReadLane(combinedInt_10, 0x00) | ReadLane(combinedInt_10, 0x20));

		combinedLightIntersectionMask = combinedInt;
	}

	uint combinedLightTypeMask = 0;
	
	#if !FROXELS_CULL_PARTICLES
	{
		uint combinedInt_01 = (lightTypeMask | LaneSwizzle(lightTypeMask, 0x1f, 0x00, 0x01));
		uint combinedInt_02 = (combinedInt_01 | LaneSwizzle(combinedInt_01, 0x1f, 0x00, 0x02));
		uint combinedInt_04 = (combinedInt_02 | LaneSwizzle(combinedInt_02, 0x1f, 0x00, 0x04));
		uint combinedInt_08 = (combinedInt_04 | LaneSwizzle(combinedInt_04, 0x1f, 0x00, 0x08));
		uint combinedInt_10 = (combinedInt_08 | LaneSwizzle(combinedInt_08, 0x1f, 0x00, 0x10));
		uint combinedInt = (ReadLane(combinedInt_10, 0x00) | ReadLane(combinedInt_10, 0x20));

		combinedLightTypeMask = combinedInt;
	}
	// turn all lights on
	//combinedLightIntersectionMask = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_numLights > 0 ? (1 << pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_numLights) - 1 : 0;
	#endif
	

	


	// now we can iterate through light sources and pre-calculate per-froxel data to be used in lighting
	// ...
	// and create light type mask too

	// calculate the ray along froxel line
	float3 posVs0 = float3(0, 0, 0);
	float3 posVs1 = float3(((pixelPosNormNoJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * 1.0;

	float froxelLineLengthVs = length(posVs1 - posVs0);
	float3 froxelLineDirVs = normalize(posVs1);


	float3 posWs0 = mul(float4(posVs0, 1), pSrt->m_mVInv).xyz;
	float3 posWs1 = mul(float4(posVs1, 1), pSrt->m_mVInv).xyz;

	float3 altPosWs0 = posWs0 - pSrt->m_altWorldPos;
	float3 altPosWs1 = posWs1 - pSrt->m_altWorldPos;
	float3 altCamPosWs0 = pSrt->m_camPos - pSrt->m_altWorldPos; // should be 0,0,0


	if (pos.x == pSrt->m_numFroxelsXY.x / 2 && pos.y == pSrt->m_numFroxelsXY.y / 2)
	{
		//SCE_BREAK();
	}




	float minIntersectionZ = 1000000.0f;
	float maxIntersectionZ = -1000000.0f;

	uint intersectionMask = combinedLightIntersectionMask;
	combinedLightIntersectionMask = 0; // we will construct the new one that will make sure we clamp at max number of lights
	combinedLightTypeMask = 0;

	ulong occupancyMask = 0;

	uint numLights = __s_bcnt1_i32_b32(intersectionMask);
	uint froxelLightIndex = 0;
	while (intersectionMask && 

#if FROXELS_CULL_PARTICLES
		froxelLightIndex < VOLUMETRICS_MAX_SS_PARTS
#else
		froxelLightIndex < pSrt->m_numVolLights
#endif
	)
	{
		int lightIndex = __s_ff1_i32_b32(intersectionMask);

		intersectionMask = __s_andn2_b32(intersectionMask, __s_lshl_b32(1, lightIndex));

		lightTypeMask = 0;
		#if !FROXELS_CULL_PARTICLES
		const LightDesc lightDesc = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_lights[lightIndex];
		NewVolumetricsLightDesc newLightDesc = pSrt->m_newVolLightDescRO[lightIndex];

		uint lightType = lightDesc.m_typeData & LIGHT_TYPE_BITS;
		uint isOrtho = lightDesc.m_typeData & LIGHT_IS_ORTHO_BITS;

		uint isOrthoProjector = (lightType == kProjectorLight) && isOrtho;

		if (lightDesc.m_localShadowTextureIdx != kInvalidLocalShadowTextureIndex)
		{
			// have a shadow
			//haveShadowLights = true;

			lightTypeMask = lightTypeMask | FROXEL_LIGHT_MASK_SHADOWED; // 

		}

		if (newLightDesc.m_flags & NEW_VOL_LIGHT_FLAG_USE_DEPTH_VARIANCE_DARKENING)
		{
			lightTypeMask = lightTypeMask | FROXEL_LIGHT_MASK_USE_DEPTH_VARIANCE_DARKENING; // 
		}

		if (newLightDesc.m_flags & NEW_VOL_LIGHT_FLAG_USE_EXPANDED_FROXEL_SAMPLING)
		{
			lightTypeMask = lightTypeMask | FROXEL_LIGHT_MASK_USE_EXPANDED_FROXEL_SAMPLING; // 
		}

		if (newLightDesc.m_flags & NEW_VOL_LIGHT_FLAG_USE_DEPTH_BUFFER_VARIANCE_SHADOWING)
		{
			lightTypeMask = lightTypeMask | FROXEL_LIGHT_MASK_USE_DEPTH_BUFFER_VARIANCE_SHADOWING; // 
		}

		#else
		VolumetricFogVolume lightDesc = pSrt->m_volPartsRO[lightIndex];
		#endif

		float kInvalidTime = 10000.0f;
		float finalT0 = kInvalidTime;
		float finalT1 = kInvalidTime;
		float finalDistance = kInvalidTime;
		float goodDirFactor = 1.0;
		float maxIntensityOnSegment = 1.0;
		float maxIntensityOnSegmentMin = 0.0;
		float maxIntensityOnSegmentMax = 1.0;

		float2 finalUv0 = float2(0.0f, 0.0f);
		float2 finalUv1 = float2(0.0f, 0.0f);
		float2 finalMinUv = float2(0.0f, 0.0f);
		float2 finalMaxUv = float2(0.0f, 0.0f);

		float finalAngleCoord = 0;
		float2 finalSlerpFactors = float2(0.0f, 0.0f);
		float finalRoundedAngleCoord = 0;
		float lightDirToPlaneAngle = 0;
		float4 finalUpVector = float4(0, 0, 0, 0);
		float finalSliceCoord = 0;
		float finalAvgValue = 0;

		#if !FROXELS_CULL_PARTICLES
		const float3 lightPosWs = lightDesc.m_worldPos;
		const float3 lightPosVs = lightDesc.m_viewPos;
		const float3 lightDirWs = lightDesc.m_worldDir;
		const float3 lightDirVs = lightDesc.m_viewDir;
		const float3 lightEndPosVs = lightDesc.m_viewPos + lightDesc.m_viewDir * lightDesc.m_radius;
		



		float difToLightToEndLight = dot(normalize(lightPosVs), normalize(lightEndPosVs));
		float angleLightToEndLight = acos(clamp(difToLightToEndLight, -1.0, 1.0));

		float applyOppositeTest = saturate((0.9995 - difToLightToEndLight) / 0.001);

		applyOppositeTest *= (lightDirVs.z > 0);

		float3 lightSpaceUp = cross(normalize(lightPosVs), normalize(lightEndPosVs));
		
		float3 lightSpaceLeftScreenAligned = cross(lightSpaceUp, float3(0, 0, 1));
		lightSpaceLeftScreenAligned = normalize(lightSpaceLeftScreenAligned);

		float3 lightSpaceLeft = cross(lightSpaceUp, lightDirVs);
		lightSpaceLeft = normalize(lightSpaceLeft);
		

		#else
		const float3 lightPosVs = lightDesc.m_viewPos;

		#endif

		float3 camCrossToLight = (cross(float3(0, 0, 1), normalize(lightPosVs)));

		float3 camCrossFroxelLine = (cross(float3(0, 0, 1), froxelLineDirVs));


		float3 posVsLightRel = float3(((pixelPosNormNoJit)* pSrt->m_viewSpaceXyParams.xy + pSrt->m_viewSpaceXyParams.zw), 1.0f) * lightPosVs.z;

		
		
		// we will work with normalized view vector along froxel line. then intersection time = distance
		float3 toPt0 = posVs0 - lightPosVs;
		float3 toPt1 = froxelLineDirVs - lightPosVs;

		

		
		// vector perpendicular to a triangle going through the origin of the light and in plane with froxel line
		float3 newUp = cross(normalize(toPt0), normalize(toPt1));
		#if !FROXELS_CULL_PARTICLES
		if ((lightType == kSpotLight || lightType == kProjectorLight) && validFroxel && !isOrthoProjector)
		{

			lightTypeMask = lightTypeMask | FROXEL_LIGHT_MASK_SPOT_LIGHT; // 

			float3 opposideSideVector = cross(camCrossToLight, lightDirVs);
			// true or false
			//float onOpposideSide = dot(camCrossToLight, camCrossFroxelLine) > 0 && length(camCrossFroxelLine) > length(camCrossToLight);
			//float onOpposideSide = dot(opposideSideVector, froxelLineDirVs) > 0;
			float onOpposideSide = 0;
			//float onOpposideSide = (dot(camCrossToLight, camCrossFroxelLine) > 0) * saturate(saturate(length(camCrossFroxelLine) - length(camCrossToLight)) * 10);// saturate(dot(camCrossToLight, camCrossFroxelLine)); // 0..1 value on how much on opposide side we are


			float3 correctCrossDir = cross(float3(0, 0, 1), lightDirVs);

			//(cross(normalize(lightPosVs), froxelLineDirVs)
			float froxelLineToLightDirAngle = acos(abs(clamp(dot(froxelLineDirVs, lightDirVs), -1.0, 1.0)));

			goodDirFactor = saturate(length(newUp) / pSrt->m_debugControls2.w);

			//if (length(newUp) < 0.01)
			//{
			//	float3 tmpUp = cross(normalize(toPt0), lightDirVs);
			//	newUp = lerp(normalize(tmpUp), normalize(newUp), asin(length(newUp)) / asin(0.01));
			//}
			//newUp = cross(newUp, lightDirVs);
			
			float lightConeHeight = lightDesc.m_radius;

			float lightConeHalfAngle = newLightDesc.m_coneAngle / 2.0;

			float lightConeSideLen = lightConeHeight / cos(lightConeHalfAngle);

			float lightConeBaseHalfWidth = tan(lightConeHalfAngle) * lightConeHeight;

			float3 lightSpaceEdgeAlongCamera = lightEndPosVs + lightSpaceLeft * lightConeBaseHalfWidth;


			// calculate the interscetion of ray from light to cmaera into base of light source

			float3 projectedBaseCenter;
			{
				float3 baseNorm = -lightDirVs;
				float3 basePos = lightEndPosVs;

				float approachRate = dot(float3(0, 0, 0) - lightPosVs, lightDirVs);
				float approachTime = lightConeHeight / approachRate;
				projectedBaseCenter = (float3(0, 0, 0) - lightPosVs) * approachTime;
			}

			
			//newUp = cross(normalize(lightSpaceEdgeAlongCamera - lightPosVs), normalize(toPt1));

			float cameraInsideLightAngle = acos(clamp(dot(normalize(float3(0, 0, 0) - lightPosVs), lightDirVs), 0.0, 1.0));

			float cameraInsideLight = max(saturate((lightConeHalfAngle - cameraInsideLightAngle) / lightConeHalfAngle), 0.0);
			{

				float difLightEdgeToEndLight = dot(normalize(lightEndPosVs + lightSpaceLeft * lightConeBaseHalfWidth), normalize(lightEndPosVs));
				float angleLightEdgeToEndLight = acos(clamp(difLightEdgeToEndLight, -1.0, 1.0));

				applyOppositeTest = angleLightToEndLight > 0.3 * angleLightEdgeToEndLight;
				applyOppositeTest = saturate(angleLightToEndLight / (0.3 * angleLightEdgeToEndLight));

				applyOppositeTest *= (lightDirVs.z > 0);

				onOpposideSide = ((-dot(lightSpaceLeftScreenAligned, normalize(posVsLightRel - lightPosVs))) - -0.25) > 0;
				
				// scale the brightness down when looking along the light
				onOpposideSide = saturate(((-dot(lightSpaceLeftScreenAligned, normalize(posVsLightRel - lightPosVs))) - -0.25) / 1.25);
				onOpposideSide = sqrt(onOpposideSide);

				// when looking at light
				//onOpposideSide = ((-dot(lightSpaceLeftScreenAligned, normalize(posVsLightRel - lightPosVs))) - -0.0) > 0;
				//
				//onOpposideSide = saturate(((-dot(lightSpaceLeftScreenAligned, normalize(posVsLightRel - lightPosVs))) - 0.5) / 0.5);
				
				
				//
				//float3 tmpUp = cross(toPt0, lightDirVs);
				//newUp = lerp(newUp, tmpUp, onOpposideSide);
				//
				//onOpposideSide = 0;
				//
				
				//onOpposideSide = froxelLineToLightDirAngle < 5 * 3.1415 / 180;

				onOpposideSide *= applyOppositeTest;
			}

			float3 newLeft = cross(newUp, lightDirVs);

			float3 newFw = cross(newLeft, newUp);

			finalUpVector.xyz = newLeft;

			// we now have orthogonal basis for a plane intersecting the forxel line and light origin

			float3 triangleX = normalize(newFw);
			float3 triangleY = normalize(newLeft);


			// project froxel line pts into the XY coordinates of triangle

			float p0x = dot(toPt0, triangleX);
			float p0y = dot(toPt0, triangleY);

			float p1x = dot(toPt1, triangleX);
			float p1y = dot(toPt1, triangleY);

			
			lightDirToPlaneAngle = acos(saturate(dot(lightDirVs, triangleX)));

			// choose slice based on this angle
			float anglePercent = saturate(lightDirToPlaneAngle / lightConeHalfAngle);

			// todo: this will need to change if we use texture arrays, because 0 in here means exactly first slice while 0 in sampling means between first and last, will need to add 0.5 texel
			finalSliceCoord = (VOLUMETRICS_MAX_CONE_SLICES - 1.0) * (1.0 - anglePercent);


			maxIntensityOnSegment = max(saturate((lightConeHalfAngle - lightDirToPlaneAngle) / lightConeHalfAngle), pSrt->m_debugControls2.z);

			//goodDirFactor = maxIntensityOnSegment;

			//maxIntensityOnSegment = pSrt->m_sliceConePower > 0 ? pow(maxIntensityOnSegment, pSrt->m_sliceConePower) : 1.0;
			//maxIntensityOnSegment = min(maxIntensityOnSegment, pSrt->m_maxSliceConeIntensity); // this allows us to avoid an artifact of a very bright middle plane splitting light cone

			{
				maxIntensityOnSegment = newLightDesc.m_penumbraIntensity * pow(saturate((maxIntensityOnSegment - newLightDesc.m_penumbraAnglePercent) / newLightDesc.m_penumbraFalloff), 2.0);
			}




			//maxIntensityOnSegment = 1.0;

			//maxIntensityOnSegment *= maxIntensityOnSegment;

			float maxIntensityBasedOnFroxelLineDir = max(saturate((lightConeHalfAngle - froxelLineToLightDirAngle) / lightConeHalfAngle), 0.0);

			//maxIntensityBasedOnFroxelLineDir *= maxIntensityBasedOnFroxelLineDir;
			
			float triangleXLen = lightConeHeight / cos(lightDirToPlaneAngle);

			float triangleYHalfLen = sqrt(lightConeSideLen * lightConeSideLen - triangleXLen * triangleXLen);

			float triangleSlope = triangleYHalfLen / triangleXLen;

			float triangleSlopeAngle = atan(triangleSlope);

			
			float nominator0 = triangleSlope * p0x - p0y;
			float denom0 = (p1y - p0y) -  triangleSlope * (p1x - p0x);
			float t0 = abs(denom0) > 0.0001 ? nominator0 / denom0 : kInvalidTime;
			float intX0 = p0x + (p1x - p0x) * t0;
			if (t0 != kInvalidTime)
				t0 = (intX0 >= 0.0f && intX0 <= triangleXLen) ? t0 : kInvalidTime;

			float nominator1 = (-triangleSlope) * p0x - p0y;
			float denom1 = (p1y - p0y) - (-triangleSlope) * (p1x - p0x);
			float t1 = abs(denom1) > 0.0001 ? nominator1 / denom1 : kInvalidTime;
			float intX1 = p0x + (p1x - p0x) * t1;
			if (t1 != kInvalidTime)
				t1 = (intX1 >= 0.0f && intX1 <= triangleXLen) ? t1 : kInvalidTime;

			float nominator2 = triangleXLen - p0x;
			float denom2 = p1x - p0x;
			float t2 = abs(denom2) > 0.0001 ? nominator2 / denom2 : kInvalidTime;
			float intY2 = p0y + (p1y - p0y) * t2;

			float coneBaseHitTime = kInvalidTime;
			if (t2 != kInvalidTime)
			{
				t2 = abs(intY2) < triangleSlope * triangleXLen ? t2 : kInvalidTime;
				coneBaseHitTime = t2;
			}

			// now sort the times
			if (t0 > t1) swap(t0, t1); // t1 is bigger than t0 now
			if (t1 > t2) swap(t1, t2); // t2 is biggest now
			if (t0 > t1) swap(t0, t1); // t0 is smallest now


			// we now have 3 sorted collision times, including invalid values if collision is not inside of the triangle

			// 3 valid collision times are possible but two would match.
			// only 2 distinct collision times are possible. two collisions might be at the same location in which case line touches a triangle vertex
			// having only one collision should be impossible (line can't intersect just one side)

			// now swap if first two are the same
			if (t1 - t0 < 0.001)
				swap(t1, t2); // t0 and t1 were the same, swap t1 and t2



			
			if (t0 != kInvalidTime && t1 != kInvalidTime)
			{
				// we have two distinct times. they should be either both valid or both bad. also they should not be the same. we check both just in case
				finalT0 = t0;
				finalT1 = t1;

				// lets also find intersection time with x axis (y = 0) and see how close we get to center of the cone

				float denom3 = p1y - p0y;
				float t3 = finalT0; // assign starting point
				if (abs(denom3) > 0.0001)
				{
					t3 = -p0y / denom3;
				}
				float3 intersectionPointVs1 = finalT1 * froxelLineDirVs;


				//newUp = cross(normalize(lightSpaceEdgeAlongCamera - lightPosVs), normalize(toPt1));


				float dot1 = dot(lightDirVs, normalize(intersectionPointVs1 - lightPosVs));

				float maxIntensityForClosestToAxisPoint = 1.0;
				bool crossCenter = true;

				if (t3 > finalT0 && t3 < finalT1 && t3 > 0 /* && t1 != coneBaseHitTime */)
				{
					// the line intersects middle of the triangle, we are guranteed to reach max intensity on the triangle

					
				}
				else
				{
					crossCenter = false;
					float3 intersectionPointVs0 = max(finalT0, 0) * froxelLineDirVs;

					// otherwise check which point is closer to center axis and that will be the max intensity
					//float dot0 = dot(triangleX, normalize(intersectionPointVs0 - lightPosVs));
					//float dot1 = dot(triangleX, normalize(intersectionPointVs1 - lightPosVs));
					//
					//float lightDirToClosestToAxisPoint = acos(clamp(max(dot0, dot1), -1.0, 1.0));
					//
					//maxIntensityForClosestToAxisPoint = max(saturate((triangleSlopeAngle - lightDirToClosestToAxisPoint) / triangleSlopeAngle), 0.0);

					float dot0 = dot(lightDirVs, normalize(intersectionPointVs0 - lightPosVs));
					
					float lightDirToClosestToAxisPoint = acos(clamp(max(dot0, dot1), -1.0, 1.0));
					//float lightDirToClosestToAxisPoint = acos(clamp(dot1, -1.0, 1.0));

					maxIntensityForClosestToAxisPoint = max(saturate((lightConeHalfAngle - lightDirToClosestToAxisPoint) / lightConeHalfAngle), pSrt->m_debugControls2.z);
					
					//maxIntensityForClosestToAxisPoint = pSrt->m_sliceConePower > 0 ? pow(maxIntensityForClosestToAxisPoint, pSrt->m_sliceConePower) : 1.0;
					//maxIntensityForClosestToAxisPoint = min(maxIntensityForClosestToAxisPoint, pSrt->m_maxSliceConeIntensity);
					
					{
						maxIntensityForClosestToAxisPoint = newLightDesc.m_penumbraIntensity * pow(saturate((maxIntensityForClosestToAxisPoint - newLightDesc.m_penumbraAnglePercent) / newLightDesc.m_penumbraFalloff), 2.0);
						
					}



					float minLightDirToClosestToAxisPoint = acos(clamp(min(dot0, dot1), -1.0, 1.0));

					float minIntensityForClosestToAxisPoint = max(saturate((lightConeHalfAngle - minLightDirToClosestToAxisPoint) / lightConeHalfAngle), 0.0);
					//minIntensityForClosestToAxisPoint = pSrt->m_sliceConePower > 0 ? pow(minIntensityForClosestToAxisPoint, pSrt->m_sliceConePower) : 1.0;
					minIntensityForClosestToAxisPoint = newLightDesc.m_penumbraIntensity * saturate((minIntensityForClosestToAxisPoint - newLightDesc.m_penumbraAnglePercent) / newLightDesc.m_penumbraFalloff);

					minIntensityForClosestToAxisPoint = min(minIntensityForClosestToAxisPoint, pSrt->m_maxSliceConeIntensity);


					maxIntensityOnSegmentMin = minIntensityForClosestToAxisPoint;

					maxIntensityOnSegmentMax = maxIntensityForClosestToAxisPoint;
					//maxIntensityForClosestToAxisPoint = 1;
					float smaller = maxIntensityForClosestToAxisPoint / maxIntensityOnSegment;
					
					maxIntensityOnSegment = maxIntensityForClosestToAxisPoint;

					//maxIntensityOnSegment = 1;
					//maxIntensityOnSegment = lerp(minIntensityForClosestToAxisPoint, maxIntensityForClosestToAxisPoint, 0.5f);
				}
				//maxIntensityOnSegment *= (1.0 + 1.0 - saturate((dot(lightSpaceLeft, triangleY)) * 100));

				if (length(lightSpaceEdgeAlongCamera - intersectionPointVs1) < 1.0)
				{
					//maxIntensityOnSegment = 0;
				}

				//float onOpposideSide = saturate(dot(opposideSideVector, normalize(firstIntersectionPointVs - lightPosVs)) * 20);
				//onOpposideSide = 1.0;

				// the one including the factor for culling out bad part
				//maxIntensityOnSegment = lerp(maxIntensityOnSegment, maxIntensityBasedOnFroxelLineDir, onOpposideSide);
				
				//maxIntensityOnSegment = maxIntensityOnSegment * maxIntensityForClosestToAxisPoint;
				

				
				// also compute triangle lookup texture uvs
				intX0 = p0x + (p1x - p0x) * t0;
				float intY0 = p0y + (p1y - p0y) * t0;

				float v0 = intX0 / triangleXLen;

			
				float u0 = intY0 / (triangleSlope * triangleXLen); // will give us -1.0 .. 1.0 in y axis space
				
				intX1 = p0x + (p1x - p0x) * t1;
				float intY1 = p0y + (p1y - p0y) * t1;

				float v1 = intX1 / triangleXLen;
				
				float u1 = intY1 / (triangleSlope * triangleXLen); // will give us -1.0 .. 1.0 in y axis space
				
																   //u0 *= 0.5f; // scale to bigger texture whre triangle is half not full texture
																   //u0 *= 1.25f;
				u0 = u0 * 0.5f + 0.5f;

				//u1 *= 0.5f;
				//u1 *= 1.25f;
				u1 = u1 * 0.5f + 0.5f;


				finalUv0 = float2(u0, v0);
				finalUv1 = float2(u1, v1);

				finalDistance = length(float2(intX0, intY0) - float2(intX1, intY1));

				float2 uvDir = finalUv1 - finalUv0;
				float uvLen = length(uvDir);
				uvDir = uvLen > 0.00001 ? uvDir / uvLen : float2(0.0, 0.0);

				float uvAngle = acos(dot(uvDir, float2(0, -1)));

				//finalUpVector.x = dot(uvDir, float2(0, -1));
				//finalUpVector.yz = uvDir;

				if (uvDir.x < 0)
					uvAngle = 2 * 3.1415926 - uvAngle;

				finalAngleCoord = uvAngle / (2 * 3.1415926);

				// check average intensity we get in this direction
				finalAngleCoord = fmod(finalAngleCoord + 0.5f, 1.0);

				//float accum1 = pSrt->m_triangleTextureRO.SampleLevel((pSrt->m_linearSampler), float3((finalUv1), finalAngleCoord /*0.28*/), 0).x;
				//float accum0 = pSrt->m_triangleTextureRO.SampleLevel((pSrt->m_linearSampler), float3((finalUv0), finalAngleCoord /*0.28*/), 0).x;

				//float avgIntensity = max(abs(accum1 - accum0), 0) / uvLen;

				//maxIntensityOnSegment = avgIntensity;
#if 0
				// we have intersection points, now map it to some triangle
				float3 intersectionPointVs0 = max(finalT0, 0) * froxelLineDirVs;

				// build two planes, each going through center of light source and intersection point
				float3 planeNorm0 = cross(lightDirVs, intersectionPointVs0 - lightPosVs);
				float3 triUp0 = normalize(cross(planeNorm0, lightDirVs));
				float3 planeNorm1 = -cross(lightDirVs, intersectionPointVs1 - lightPosVs);
				float3 triUp1 = normalize(cross(planeNorm1, lightDirVs));

				// project into that triangle
				float triX0 = dot(intersectionPointVs0 - lightPosVs, lightDirVs);
				float triY0 = dot(intersectionPointVs0 - lightPosVs, triUp0);

				v0 = triX0 / lightConeHeight;
				u0 = triY0 / lightConeBaseHalfWidth;


				float triX1 = dot(intersectionPointVs1 - lightPosVs, lightDirVs);
				float triY1 = dot(intersectionPointVs1 - lightPosVs, triUp1);

				v1 = triX1 / lightConeHeight;
				u1 = triY1 / lightConeBaseHalfWidth;

				bool both = (u0 > 0.0) == (u1 > 0.0);
				if (crossCenter == both)
				{
					// one U should be > 0.5 one less
					//if (u1 > 0.0)
					//	u1 = - u1;
					//else
					//	u1 = - u1;
				}
				else
				{

				}

				// little different approach. build two planes but have them go through projected center

				//planeNorm0 = cross((projectedBaseCenter - lightPosVs), intersectionPointVs0 - lightPosVs);
				//triUp0 = normalize(cross(planeNorm0, lightDirVs));
				//float3 triFw0 = normalize(cross(triUp0, planeNorm0));
				//
				//planeNorm1 = cross((projectedBaseCenter - lightPosVs), intersectionPointVs1 - lightPosVs);
				//triUp1 = normalize(cross(planeNorm1, lightDirVs));
				//float3 triFw1 = normalize(cross(triUp1, planeNorm1));
				//
				//// project into that triangle
				//triX0 = dot(intersectionPointVs0 - lightPosVs, triFw0);
				//triY0 = dot(intersectionPointVs0 - lightPosVs, triUp0);
				//
				//v0 = triX0 / lightConeHeight;
				//u0 = triY0 / lightConeBaseHalfWidth;
				//
				//
				//triX1 = dot(intersectionPointVs1 - lightPosVs, triFw1);
				//triY1 = dot(intersectionPointVs1 - lightPosVs, triUp1);
				//
				//v1 = triX1 / lightConeHeight;
				//u1 = triY1 / lightConeBaseHalfWidth;




				float sameDir = dot(triUp1, triUp0);

				//sameDir = max(sameDir - pSrt->m_debugControls2.y, 0) / (1.0 - pSrt->m_debugControls2.y);


				//maxIntensityOnSegment *= (1.0 - pSrt->m_debugControls2.y * saturate((dot(triangleY, lightSpaceLeft) + pSrt->m_debugControls2.z) / (1 + pSrt->m_debugControls2.z)));


				// check how close we are to edge


				// skew u1 a little bit

				//u0 *= lerp(1.0, 1.0 - pSrt->m_debugControls2.z, sameDir);

				

				//u1 *= sameDir > 0 ? (1.0 - pow(sameDir, 1.0 + pSrt->m_debugControls2.y)) * pSrt->m_debugControls2.z : 1.0;

				//maxIntensityOnSegment = pow(max(sameDir, 0), 1.0 + pSrt->m_debugControls2.y);

				//maxIntensityOnSegment += max(sameDir, 0) > 0 ? (1.0 - maxIntensityOnSegment) * pow(max(sameDir, 0), 1.0 + pSrt->m_debugControls2.y) : 0.0;

				//maxIntensityOnSegment = goodDirFactor; // pow(maxIntensityOnSegment, lerp(1.0 - pSrt->m_debugControls2.y, 1.0, goodDirFactor));

				float towardsCenter = u0 > 0 ? -1 : 1.0;
				float uOffset = 0; // (1.0 - goodDirFactor) * pSrt->m_debugControls2.y * towardsCenter;
				u0 += uOffset;
				u1 += uOffset;
				//u0 *= lerp(1.0 - pSrt->m_debugControls2.z, 1.0, goodDirFactor);
				//u1 *= lerp(1.0 - pSrt->m_debugControls2.z, 1.0, goodDirFactor);

				//u0 *= 0.5f; // scale to bigger texture whre triangle is half not full texture
				//u0 *= 1.25f;
				u0 = u0 * 0.5f + 0.5f;

				//u1 *= 0.5f;
				//u1 *= 1.25f;
				u1 = u1 * 0.5f + 0.5f;
#endif



				float closeFactor = max(saturate(1.0 - v0), saturate(1.0 - v1));
				closeFactor *= closeFactor;
				closeFactor = 1.0; // we don't need to do this because the triangle integral already has quadratic deistance falloff built-in
				
				// maxIntensityOnSegment = lerp(maxIntensityOnSegment, 1.0, saturate(1.0 - v0)); // closer we are, less the plane offset matters
				
				//maxIntensityOnSegment = lerp(1.0, maxIntensityOnSegment, goodDirFactor);

				maxIntensityOnSegment *= closeFactor;

				finalUpVector = float4(intX0, intY0, intX1, intY1);

				//maxIntensityOnSegment = closeFactor;

				//finalUpVector = float4(u0, v0, u1, v1);

				//v0 *= 0.5f;
				//v1 *= 0.5f;

				finalUv0 = float2(u0, v0);
				finalUv1 = float2(u1, v1);

				finalDistance = length(float2(intX0, intY0) - float2(intX1, intY1));
				
				uvDir = finalUv1 - finalUv0;
				uvLen = length(uvDir);
				uvDir = uvLen > 0.00001 ? uvDir / uvLen : float2(0.0, 0.0);

				uvAngle = acos(dot(uvDir, float2(0, -1)));

				float halfTexel = 2.0 / TRIANGLE_TEXTURE_XY_DIM_F;
				//finalUv0 -= uvDir * float2(halfTexel, halfTexel);
				//finalUv1 += uvDir * float2(halfTexel, halfTexel);


				//finalUpVector.x = dot(uvDir, float2(0, -1));
				//finalUpVector.yz = uvDir;

				if (uvDir.x < 0)
					uvAngle = 2 * 3.1415926 - uvAngle;

				float texelAngle = (2 * 3.1415926) / TRIANGLE_TEXTURE_Z_DIM_F;
				finalAngleCoord = (uvAngle / texelAngle + 0.5) / TRIANGLE_TEXTURE_Z_DIM_F;

				float alternateAngleCoord = fmod(finalAngleCoord + 0.5, 1.0);

				if (finalAngleCoord > 0.25 && finalAngleCoord < 0.75)
				{
					finalAngleCoord = fmod(finalAngleCoord + 0.5, 1.0);
				}

				// check where starting froxel is
				float viewDist0 = 0;
				// uvs at camera, and end of intesrection of froxel line and light source
				float2 uvTotal0 = finalUv0 + (max(viewDist0, finalT0) - finalT0) / (finalT1 - finalT0) * (finalUv1 - finalUv0);
				float2 uvTotal1 = finalUv0 + (max(viewDist0, finalT1) - finalT0) / (finalT1 - finalT0) * (finalUv1 - finalUv0);

				if (uvTotal0.x > (0.5 - uvTotal0.y * 0.1))
				{
					//finalAngleCoord = fmod(finalAngleCoord + 0.5, 1.0);
				}


				//finalAngleCoord = uvAngle / (2 * 3.1415926); // 0 .. 1

				 

				// we now have a direction we want to sample, but we wnat to keep directions always poiting away from base of the conbe for better sampling results
				// i.e. we always sample in direction from base of cone to the origin of cone
				// so we want to keep our angle -90 < a < 90


				//if (finalAngleCoord > 3.1415926 / 2.0 && finalAngleCoord < 3.1415926 * 3.0 / 4.0)
				//{
				//	// we want to flip the angle
				//	if (finalAngleCoord <= 3.1415926)
				//	{
				//		finalAngleCoord += 3.1415926;
				//	}
				//	else
				//	{
				//		finalAngleCoord -= 3.1415926;
				//	}
				//}

				// to aovid having weird issue with sampling border we want to always sample direction that goes backwards to our direction

				// also find min and max uvs to sample on this uv line, because we want to allow to sample a little bit outside to  make it work better with low res texture, but we can't clamp to 
				// edge of texture because clamping claps each value seprately instead of intesrecting uv direction with edge
				{
					float uvDx = finalUv1.x - finalUv0.x;
					float uvDy = finalUv1.y - finalUv0.y;

					float ut0 = kInvalidTime;
					// intersect with y = 0
					float ut1 = kInvalidTime;

					// intersect with x = 1
					float ut2 = kInvalidTime;
					// intersect with x = 0
					float ut3 = kInvalidTime;

					if (abs(uvDy) < 0.00001 && abs(uvDx) < 0.00001)
					{

					}
					else if (abs(uvDy) < 0.00001)
					{
						// flat line

						// intersect with x = 1
						ut0 = (1.0 - finalUv0.x) / uvDx;
						// intersect with x = 0
						ut1 = (0.0 - finalUv0.x) / uvDx;

						if (ut0 > ut1) swap(ut0, ut1); // t1 is bigger than t0 now

					}
					else if (abs(uvDx) < 0.00001)
					{
						// vertical line
						// intersect with y = 1
						ut0 = (1.0 - finalUv0.y) / uvDy;
						// intersect with y = 0
						ut1 = (0.0 - finalUv0.y) / uvDy;

						if (ut0 > ut1) swap(ut0, ut1); // t1 is bigger than t0 now
					}
					else
					{
						// general case
						// intersect with y = 1
						ut0 = (1.0 - finalUv0.y) / uvDy;
						// intersect with y = 0
						ut1 = (0.0 - finalUv0.y) / uvDy;

						// intersect with x = 1
						ut2 = (1.0 - finalUv0.x) / uvDx;
						// intersect with x = 0
						ut3 = (0.0 - finalUv0.x) / uvDx;


						if (ut0 > ut1) swap(ut0, ut1); // t1 is bigger than t0 now
						if (ut1 > ut2) swap(ut1, ut2); // t2 is biggest of t0 t1 t2
						if (ut2 > ut3) swap(ut2, ut3); // t3 is biggest of t0 t1 t2 t3

						// now sort bottom 3

						if (ut0 > ut1) swap(ut0, ut1); // t1 is bigger than t0 now
						if (ut1 > ut2) swap(ut1, ut2); // t2 is biggest now
						if (ut0 > ut1) swap(ut0, ut1); // t0 is smallest now

						// we care about the two middle numbers

						ut0 = ut1;
						ut1 = ut2;
					}

					finalMinUv = finalUv0 + (finalUv1 - finalUv0) * ut0;
					finalMaxUv = finalUv0 + (finalUv1 - finalUv0) * ut1;

					
				}
				

				// check average intensity we get in this direction
			
				{
					float2 uv0 = uvTotal0;
					float2 uv1 = uvTotal1;
				
					uv0 = clamp(uv0, min(finalMinUv, finalMaxUv), max(finalMinUv, finalMaxUv));
					uv1 = clamp(uv1, min(finalMinUv, finalMaxUv), max(finalMinUv, finalMaxUv));
				
					Texture3D<float> integralTexture = pSrt->m_triangleTextureRO;
					float accum1 = 0;
					float accum0 = 0;
				
					int iFinalSliceCoord = int(floor(finalSliceCoord));
				
					ulong exec = __s_read_exec();
					do {
						// We first pick the first active lane index
						uint first_bit = __s_ff1_i32_b64(exec);
						// We know take the value inside this lane
						int uniform_SliceCoord = __v_readlane_b32(iFinalSliceCoord, first_bit);
						// We create a mask which is 1 for all lanes that contain this value
						ulong lane_mask = __v_cmp_eq_u32(iFinalSliceCoord, uniform_SliceCoord);
						// We use the predicate trick to override the exec mask and create a
						// execution block such as exec == lane_mask
						if (__v_cndmask_b32(0, 1, lane_mask)) {
				
							integralTexture = pSrt->m_triangleTexturesRO[uniform_SliceCoord];
							Texture3D<float> integralTexture1 = pSrt->m_triangleTexturesRO[min(uniform_SliceCoord + 1, VOLUMETRICS_MAX_CONE_SLICES - 1)];

							float accum0Slice0 = integralTexture.SampleLevel(SetSampleModeClampToLastTexelZWrap(pSrt->m_linearSampler), float3((uv0), finalAngleCoord), 0).x;
							float accum0Slice0Alternate = integralTexture.SampleLevel(SetSampleModeClampToLastTexelZWrap(pSrt->m_linearSampler), float3((uv0), alternateAngleCoord), 0).x;

							if (accum0Slice0Alternate < accum0Slice0)
							{
							//	finalAngleCoord = alternateAngleCoord;
							//	accum0Slice0 = accum0Slice0Alternate;
							}

				
							float accum1Slice0 = integralTexture.SampleLevel(SetSampleModeClampToLastTexelZWrap(pSrt->m_linearSampler), float3((uv1), finalAngleCoord), 0).x;
				
							float accum1Slice1 = integralTexture1.SampleLevel(SetSampleModeClampToLastTexelZWrap(pSrt->m_linearSampler), float3((uv1), finalAngleCoord), 0).x;
							float accum0Slice1 = integralTexture1.SampleLevel(SetSampleModeClampToLastTexelZWrap(pSrt->m_linearSampler), float3((uv0), finalAngleCoord), 0).x;
				
							// technically we should square here, but htis is how interpolation will work if we switch to mips
							accum1 = lerp(accum1Slice0, accum1Slice1, frac(finalSliceCoord));
							accum0 = lerp(accum0Slice0, accum0Slice1, frac(finalSliceCoord));
				
				
						}
						// Since we are finished with this lane, we update the execution mask
						exec &= ~lane_mask;
						// When all lanes are processed, exec will be zero and we can exit
					} while (exec != 0);
				
				
					float integral = max(abs(accum1 * accum1 - accum0 * accum0) / TEXTURE_FACTOR, 0);// *length(uv1 - uv0) / length(uvTotal1 - uvTotal0);
				
					float intensityFromIntegral = integral / length(uv1 - uv0); // length(uvTotal1 - uvTotal0);
				
					finalAvgValue = intensityFromIntegral;
				}



				float coord = finalAngleCoord * TRIANGLE_TEXTURE_Z_DIM_F - 0.5;

				int iCoord0 = int(floor(coord));
				float fraction = coord - iCoord0;
				int iCoord1 = iCoord0 + 1;


				// generate slerp factors

				float angle0 = 2.0 * 3.1415926 / (TRIANGLE_TEXTURE_Z_DIM_F - 1.0) * (iCoord0); // we start and end at same angle
				float angle1 = 2.0 * 3.1415926 / (TRIANGLE_TEXTURE_Z_DIM_F - 1.0) * (iCoord1); // we start and end at same angle

				float rayDirX0 = -cos(angle0);
				float rayDirY0 = sin(angle0);
				float rayDirX1 = -cos(angle1);
				float rayDirY1 = sin(angle1);

				float t = fraction;

				float cosOmega = dot(float2(rayDirX0, rayDirY0), float2(rayDirX1, rayDirY1));

				finalRoundedAngleCoord = (iCoord0 + 0.5) / TRIANGLE_TEXTURE_Z_DIM_F;

				//if (cosOmega < 0.001)
				//{
				//	finalSlerpFactors.x = 1 - t;
				//	finalSlerpFactors.y = t;
				//
				//}
				//else
				{
					float omega = acos(clamp(cosOmega, -1.0, 1.0));

					finalSlerpFactors.x = sin((1 - t) * omega) / sin(omega);
					finalSlerpFactors.y = sin(t * omega) / sin(omega);

				}






				// now choose a specific direction and readjust our coordinates to be along that, to avoid banding
#if 0
				float coord = finalAngleCoord * TRIANGLE_TEXTURE_Z_DIM_F;
				
				int iCoord = round(coord);

				// now get a fixed up direction
				float angle = 2.0 * 3.1415926 / (TRIANGLE_TEXTURE_Z_DIM_F - 1.0) * (iCoord); // we start and end at same angle

				float rayDirX = -cos(angle);
				float rayDirY = sin(angle);

				float newDisp = dot(finalUv1 - finalUv0, float2(rayDirY, rayDirX));

				finalUv1 = finalUv0 + newDisp * float2(rayDirY, rayDirX);
				finalAngleCoord = angle / (2 * 3.1415926);
#endif

				
				{

					#if !FROXELS_CULL_PARTICLES
					float froxelDepthZ0 = dot(froxelLineDirVs, float3(0, 0, 1)) * finalT0;

					float froxelSliceFloat0 = CameraLinearDepthToFroxelZSliceExpWithOffset(froxelDepthZ0, pSrt->m_runtimeGridOffset);

					float oneSliceDepth = DepthDerivativeAtSlice(froxelSliceFloat0);

					finalT0 += newLightDesc.m_depthShift * oneSliceDepth * (1.0 / froxelLineDirVs.z);
					finalT1 += newLightDesc.m_depthShift * oneSliceDepth * (1.0 / froxelLineDirVs.z);
					#endif
				}
			}

			// now store the view distance to start and end
		}

		float planeStart[6] = { 0,0,0,0,0,0 };
		float planeRate[6] = { 0,0,0,0,0,0 };

		if (isOrthoProjector && validFroxel)
		{
			lightTypeMask = lightTypeMask | FROXEL_LIGHT_MASK_ORTHO_LIGHT; // 


			// the lightis a box. we convert froxel line into the space of the box and find intersections with -1,-1,-1 - 1,1,1 cube

			float3 cameraLightSpacePos0;
			cameraLightSpacePos0.x = dot(lightDesc.m_altWorldToLightMatrix[0], float4(altCamPosWs0, 1.0f));
			cameraLightSpacePos0.y = dot(lightDesc.m_altWorldToLightMatrix[1], float4(altCamPosWs0, 1.0f));
			cameraLightSpacePos0.z = dot(lightDesc.m_altWorldToLightMatrix[2], float4(altCamPosWs0, 1.0f));



			float3 lightSpacePos0;
			lightSpacePos0.x = dot(lightDesc.m_altWorldToLightMatrix[0], float4(altPosWs0, 1.0f));
			lightSpacePos0.y = dot(lightDesc.m_altWorldToLightMatrix[1], float4(altPosWs0, 1.0f));
			lightSpacePos0.z = dot(lightDesc.m_altWorldToLightMatrix[2], float4(altPosWs0, 1.0f));


			float3 lightSpacePos1;
			lightSpacePos1.x = dot(lightDesc.m_altWorldToLightMatrix[0], float4(altPosWs1, 1.0f));
			lightSpacePos1.y = dot(lightDesc.m_altWorldToLightMatrix[1], float4(altPosWs1, 1.0f));
			lightSpacePos1.z = dot(lightDesc.m_altWorldToLightMatrix[2], float4(altPosWs1, 1.0f));

			float froxelLineLengthLightSpace = length(lightSpacePos1 - lightSpacePos0);

			float froxelLenVsToLightSpaceRatio = froxelLineLengthVs / froxelLineLengthLightSpace;

			float3 froxelLineDirLightSpace = froxelLineLengthLightSpace > 0.0001 ? (lightSpacePos1 - lightSpacePos0) / froxelLineLengthLightSpace : 0;

			float t0 = kInvalidTime;
			float t1 = kInvalidTime;

			if (abs(cameraLightSpacePos0.x) <= 1.0 && abs(cameraLightSpacePos0.y) <= 1.0 && cameraLightSpacePos0.z >= 0.0 && cameraLightSpacePos0.z <= 1.0)
			{
				t0 = 0.0f;
			}

		

			// first test x,z plane
			
			{
				float3 normal0 = float3(0, 1, 0);											float3 normal1 = float3(0, -1, 0);
				float distToPlane0 = dot(lightSpacePos0 - float3(0, 1, 0), normal0);		float distToPlane1 = dot(lightSpacePos0 - float3(0, -1, 0), normal1);
				float approachVel0 = -dot(normal0, froxelLineDirLightSpace);				float approachVel1 = -dot(normal1, froxelLineDirLightSpace);
				float intersectionTime0 = abs(approachVel0) > 0.0001 ? distToPlane0 / approachVel0 : kInvalidTime;
				float intersectionTime1 = abs(approachVel1) > 0.0001 ? distToPlane1 / approachVel1 : kInvalidTime;
				float3 intersectionPt0 = lightSpacePos0 + intersectionTime0 * froxelLineDirLightSpace;
				float3 intersectionPt1 = lightSpacePos0 + intersectionTime1 * froxelLineDirLightSpace;

				float intersectionTimeVs0 = froxelLenVsToLightSpaceRatio * intersectionTime0;
				float intersectionTimeVs1 = froxelLenVsToLightSpaceRatio * intersectionTime1;

				planeStart[0] = distToPlane0;
				planeRate[0] = -approachVel0 / froxelLenVsToLightSpaceRatio;

				if (intersectionTimeVs0 > 0 && abs(intersectionPt0.x) <= 1.0 && intersectionPt0.z >= 0.0 && intersectionPt0.z <= 1.0)
				{
					// valid point
					if (t0 != kInvalidTime)
					{
						// aready have a good time
						t1 = intersectionTimeVs0;

						if (t0 > t1) swap(t0, t1);
					}
					else
					{
						t0 = intersectionTimeVs0;
					}
				}

				if (intersectionTimeVs1 > 0 && abs(intersectionPt1.x) <= 1.0 && intersectionPt1.z >= 0.0 && intersectionPt1.z <= 1.0)
				{
					// valid point
					if (t0 != kInvalidTime)
					{
						if (t1 != kInvalidTime)
						{
							t0 = min(t0, intersectionTimeVs0);
							t1 = max(t1, intersectionTimeVs0);
						}
						else
						{
							// aready have a good time
							t1 = intersectionTimeVs1;
							if (t0 > t1) swap(t0, t1);
						}
					}
					else
					{
						t0 = intersectionTimeVs1;
					}
				}
			}

			// now test y,z plane
			{
				float3 normal0 = float3(1, 0, 0);
				float3 normal1 = float3(-1, 0, 0);
				float distToPlane0 = dot(lightSpacePos0 - float3(1, 0, 0), normal0);
				float distToPlane1 = dot(lightSpacePos0 - float3(-1, 0, 0), normal1);
				float approachVel0 = -dot(normal0, froxelLineDirLightSpace);
				float approachVel1 = -dot(normal1, froxelLineDirLightSpace);
				float intersectionTime0 = abs(approachVel0) > 0.0001 ? distToPlane0 / approachVel0 : kInvalidTime;
				float intersectionTime1 = abs(approachVel1) > 0.0001 ? distToPlane1 / approachVel1 : kInvalidTime;
				float3 intersectionPt0 = lightSpacePos0 + intersectionTime0 * froxelLineDirLightSpace;
				float3 intersectionPt1 = lightSpacePos0 + intersectionTime1 * froxelLineDirLightSpace;

				float intersectionTimeVs0 = froxelLenVsToLightSpaceRatio * intersectionTime0;
				float intersectionTimeVs1 = froxelLenVsToLightSpaceRatio * intersectionTime1;
				
				planeStart[1] = distToPlane0;
				planeRate[1] = -approachVel0 / froxelLenVsToLightSpaceRatio;


				if (intersectionTimeVs0 > 0 && abs(intersectionPt0.y) <= 1.0 && intersectionPt0.z >= 0.0 && intersectionPt0.z <= 1.0)
				{
					// valid point
					if (t0 != kInvalidTime)
					{
						if (t1 != kInvalidTime)
						{
							t0 = min(t0, intersectionTimeVs0);
							t1 = max(t1, intersectionTimeVs0);
						}
						else
						{
							// aready have a good time
							t1 = intersectionTimeVs0;
							if (t0 > t1) swap(t0, t1);
						}
					}
					else
					{
						t0 = intersectionTimeVs0;
					}
				}

				if (intersectionTimeVs1 > 0 && abs(intersectionPt1.y) <= 1.0 && intersectionPt1.z >= 0.0 && intersectionPt1.z <= 1.0)
				{
					// valid point
					if (t0 != kInvalidTime)
					{
						if (t1 != kInvalidTime)
						{
							t0 = min(t0, intersectionTimeVs1);
							t1 = max(t1, intersectionTimeVs1);
						}
						else
						{
							// aready have a good time
							t1 = intersectionTimeVs1;
							if (t0 > t1) swap(t0, t1);
						}
					}
					else
					{
						t0 = intersectionTimeVs1;
					}
				}
			}

			// now test x, y plane
			{
				float3 normal0 = float3(0, 0, 1);
				float3 normal1 = float3(0, 0, -1);
				float distToPlane0 = dot(lightSpacePos0 - float3(0, 0, 1), normal0);
				float distToPlane1 = dot(lightSpacePos0 - float3(0, 0, 0), normal1);
				float approachVel0 = -dot(normal0, froxelLineDirLightSpace);
				float approachVel1 = -dot(normal1, froxelLineDirLightSpace);
				float intersectionTime0 = abs(approachVel0) > 0.0001 ? distToPlane0 / approachVel0 : kInvalidTime;
				float intersectionTime1 = abs(approachVel1) > 0.0001 ? distToPlane1 / approachVel1 : kInvalidTime;
				float3 intersectionPt0 = lightSpacePos0 + intersectionTime0 * froxelLineDirLightSpace;
				float3 intersectionPt1 = lightSpacePos0 + intersectionTime1 * froxelLineDirLightSpace;

				float intersectionTimeVs0 = froxelLenVsToLightSpaceRatio * intersectionTime0;
				float intersectionTimeVs1 = froxelLenVsToLightSpaceRatio * intersectionTime1;

				planeStart[2] = distToPlane0;
				planeRate[2] = -approachVel0 / froxelLenVsToLightSpaceRatio;

				if (intersectionTimeVs0 > 0 && abs(intersectionPt0.x) <= 1.0 && abs(intersectionPt0.y) <= 1.0)
				{
					// valid point
					if (t0 != kInvalidTime)
					{
						if (t1 != kInvalidTime)
						{
							t0 = min(t0, intersectionTimeVs0);
							t1 = max(t1, intersectionTimeVs0);
						}
						else
						{
							// aready have a good time
							t1 = intersectionTimeVs0;
							if (t0 > t1) swap(t0, t1);
						}
					}
					else
					{
						t0 = intersectionTimeVs0;
					}
				}

				if (intersectionTimeVs1 > 0 && abs(intersectionPt1.x) <= 1.0 && abs(intersectionPt1.y) <= 1.0)
				{
					// valid point
					if (t0 != kInvalidTime)
					{
						if (t1 != kInvalidTime)
						{
							t0 = min(t0, intersectionTimeVs1);
							t1 = max(t1, intersectionTimeVs1);
						}
						else
						{
							// aready have a good time
							t1 = intersectionTimeVs1;
							if (t0 > t1) swap(t0, t1);
						}
					}
					else
					{
						t0 = intersectionTimeVs1;
					}
				}
			}

			if (t0 != kInvalidTime && t1 != kInvalidTime)
			{
				// we have two distinct times. they should be either both valid or both bad. also they should not be the same. we check both just in case
				finalT0 = t0;
				finalT1 = t1;



				#if !FROXELS_CULL_PARTICLES
					float froxelDepthZ0 = dot(froxelLineDirVs, float3(0, 0, 1)) * finalT0;

					float froxelSliceFloat0 = CameraLinearDepthToFroxelZSliceExpWithOffset(froxelDepthZ0, pSrt->m_runtimeGridOffset);

					float oneSliceDepth = DepthDerivativeAtSlice(froxelSliceFloat0);

					finalT0 += newLightDesc.m_depthShift * oneSliceDepth * (1.0 / froxelLineDirVs.z);
					finalT1 += newLightDesc.m_depthShift * oneSliceDepth * (1.0 / froxelLineDirVs.z);


					planeStart[0] -= planeRate[0] * newLightDesc.m_depthShift * oneSliceDepth * (1.0 / froxelLineDirVs.z);
					planeStart[1] -= planeRate[1] * newLightDesc.m_depthShift * oneSliceDepth * (1.0 / froxelLineDirVs.z);
					planeStart[2] -= planeRate[2] * newLightDesc.m_depthShift * oneSliceDepth * (1.0 / froxelLineDirVs.z);
					


				#endif
			}

		}
		
		#endif
		#if !FROXELS_CULL_PARTICLES
		if (lightType == kPointLight && validFroxel)
		#else
		if (validFroxel)
		#endif
		{

			lightTypeMask = lightTypeMask | FROXEL_LIGHT_MASK_POINT_LIGHT; // 

			// new up is the normal to a plane going through light source and intersection points
			// we make forward vector a direction of the froxel line

			float3 newFw = froxelLineDirVs; // normalized already

			float3 newLeft = cross(newUp, newFw);

			newLeft = normalize(newLeft);

			// project froxel line points into our basis

			float p0x = dot(toPt0, newLeft);
			float p0y = dot(toPt0, newFw);

			float p1x = dot(toPt1, newLeft);
			float p1y = dot(toPt1, newFw);

			float dx = p1x - p0x;
			float dy = p1y - p0y;

			// solve quadratic
			float radius = lightDesc.m_radius;

			float C = p0x * p0x + p0y * p0y - radius * radius;
			float B = 2 * p0x * dx + 2 * p0y * dy;
			float A = dx * dx + dy * dy;

			float det = B * B - 4 * A * C;

			if (det > 0)
			{
				float t0 = (-B + sqrt(det)) / (2 * A);
				float t1 = (-B - sqrt(det)) / (2 * A);

				if (t0 > t1) swap(t0, t1);

				if (t1 - t0 > 0.001)
				{
					// have a solution

					finalT0 = t0;
					finalT1 = t1;

					float midT = (t0 + t1) * 0.5f;

					float midX = p0x + dx * midT;
					float midY = p0y + dy * midT;

					float distToCenter = sqrt(midX * midX + midY * midY);

					maxIntensityOnSegment = saturate(1.0 - distToCenter / radius);

					//maxIntensityOnSegment = maxIntensityOnSegment * maxIntensityOnSegment;

					#if !FROXELS_CULL_PARTICLES
					{
						maxIntensityOnSegment = newLightDesc.m_penumbraIntensity * pow(saturate((maxIntensityOnSegment - newLightDesc.m_penumbraAnglePercent) / newLightDesc.m_penumbraFalloff), 2.0);
					}
					#endif



					float intX0 = p0x + dx * t0;
					float intY0 = p0y + dy * t0;

					float u0 = intX0 / lightDesc.m_radius; // -1.0 .. 1.0
					u0 = u0 * 0.5 + 0.5f; // 0..1

					float v0 = intY0 / lightDesc.m_radius; // -1.0 .. 1.0
					v0 = v0 * 0.5 + 0.5f; // 0..1

					float intX1 = p0x + dx * t1;
					float intY1 = p0y + dy * t1;

					float u1 = intX1 / lightDesc.m_radius; // -1.0 .. 1.0
					u1 = u1 * 0.5 + 0.5f; // 0..1

					float v1 = intY1 / lightDesc.m_radius; // -1.0 .. 1.0
					v1 = v1 * 0.5 + 0.5f; // 0..1

					finalUv0 = float2(u0, v0);
					finalUv1 = float2(u1, v1);

					finalDistance = length(float2(intX0, intY0) - float2(intX1, intY1));

					#if !FROXELS_CULL_PARTICLES
						float froxelDepthZ0 = dot(froxelLineDirVs, float3(0, 0, 1)) * finalT0;
					
						float froxelSliceFloat0 = CameraLinearDepthToFroxelZSliceExpWithOffset(froxelDepthZ0, pSrt->m_runtimeGridOffset);

						float oneSliceDepth = DepthDerivativeAtSlice(froxelSliceFloat0);

						finalT0 += newLightDesc.m_depthShift * oneSliceDepth * (1.0 / froxelLineDirVs.z);
						finalT1 += newLightDesc.m_depthShift * oneSliceDepth * (1.0 / froxelLineDirVs.z);
					#endif
				}
			}

#if 0
			float3 newFw = toPt0; // choose vector from center to pt0 to be forward, while the up vector is perpendicular to plane of triangle from center of light source to two points of our froxel line

			float3 newLeft = cross(newUp, newFw);

			float3 triangleX = normalize(newFw);
			float3 triangleY = normalize(newLeft);


			// project froxel line pts into the XY coordinates of triangle

			float p0x = dot(toPt0, triangleX);
			float p0y = dot(toPt0, triangleY);

			float p1x = dot(toPt1, triangleX);
			float p1y = dot(toPt1, triangleY);

			float dx = p1x - p0x;
			float dy = p1y - p0y;

			// solve quadratic
			float radius = lightDesc.m_radius;

			float C = p0x * p0x + p0y * p0y - radius * radius;
			float B = 2 * p0x * dx + 2 * p0y + dy;
			float A = dx * dx + dy * dy;

			float det = B * B - 4 * A * C;

			if (det > 0)
			{
				float t0 = (-B + sqrt(det)) / (2 * A);
				float t1 = (-B - sqrt(det)) / (2 * A);

				if (t0 > t1) swap(t0, t1);

				if (t1 - t0 > 0.001)
				{
					// have a solution

					finalT0 = t0;
					finalT1 = t1;

					float midT = (t0 + t1) * 0.5f;

					float midX = p0x + dx * midT;
					float midY = p0y + dy * midT;

					float distToCenter = sqrt(midX * midX + midY * midY);
					
					maxIntensityOnSegment = saturate(1.0 - distToCenter / radius);

					maxIntensityOnSegment = maxIntensityOnSegment * maxIntensityOnSegment;
					
				}
			}
#endif

		}

		// we will write out this light source result for froxels only if some froxels actually intersect with it
		int froxelStart = 62;
		int froxelEnd = 0;

		bool intersected = finalT1 != kInvalidTime;
		if (intersected)
		{
			minIntersectionZ = min(minIntersectionZ, finalT0);
			maxIntersectionZ = max(maxIntersectionZ, finalT1);


			// make bitmask of which froxelDepths light is intersecting
			// first we need to conver the times from lengths along froxel line to lengths along z

			float froxelDepth0 = dot(froxelLineDirVs, float3(0, 0, 1)) * finalT0;
			float froxelDepth1 = dot(froxelLineDirVs, float3(0, 0, 1)) * finalT1;

			float froxelSliceFloat0 = CameraLinearDepthToFroxelZSliceExpWithOffset(froxelDepth0, pSrt->m_runtimeGridOffset);
			float froxelSliceFloat1 = CameraLinearDepthToFroxelZSliceExpWithOffset(froxelDepth1, pSrt->m_runtimeGridOffset);

			froxelStart = clamp(int(floor(froxelSliceFloat0)), 0, 62);
			froxelEnd = clamp(int(floor(froxelSliceFloat1)), froxelStart, 62);


			ulong maskZeroToEnd = __v_lshl_b64(1, froxelEnd + 1) - 1;
			ulong maskZeroToStart = froxelStart > 0 ? __v_lshl_b64(1, froxelStart) - 1 : 0;

			occupancyMask = occupancyMask | (maskZeroToEnd - maskZeroToStart);


			ulong thisLightMask = (maskZeroToEnd - maskZeroToStart);
			
		}
		

		ulong lane_mask = __v_cmp_eq_u32(intersected, true);

		if (lane_mask)
		{

			{
				// combine this light's mask accross all threads per tile

				int combinedMin;
				int combinedMax;

				{
					int maxZ_01 = max(froxelEnd, LaneSwizzle(froxelEnd, 0x1f, 0x00, 0x01));
					int maxZ_02 = max(maxZ_01, LaneSwizzle(maxZ_01, 0x1f, 0x00, 0x02));
					int maxZ_04 = max(maxZ_02, LaneSwizzle(maxZ_02, 0x1f, 0x00, 0x04));
					int maxZ_08 = max(maxZ_04, LaneSwizzle(maxZ_04, 0x1f, 0x00, 0x08));
					int maxZ_10 = max(maxZ_08, LaneSwizzle(maxZ_08, 0x1f, 0x00, 0x10));
					int maxZ = max(ReadLane(maxZ_10, 0x00), ReadLane(maxZ_10, 0x20));

					combinedMax = maxZ;
				}

				{
					int minZ_01 = min(froxelStart, LaneSwizzle(froxelStart, 0x1f, 0x00, 0x01));
					int minZ_02 = min(minZ_01, LaneSwizzle(minZ_01, 0x1f, 0x00, 0x02));
					int minZ_04 = min(minZ_02, LaneSwizzle(minZ_02, 0x1f, 0x00, 0x04));
					int minZ_08 = min(minZ_04, LaneSwizzle(minZ_04, 0x1f, 0x00, 0x08));
					int minZ_10 = min(minZ_08, LaneSwizzle(minZ_08, 0x1f, 0x00, 0x10));
					int minZ = min(ReadLane(minZ_10, 0x00), ReadLane(minZ_10, 0x20));

					combinedMin = minZ;
				}

				// for each slice do OR of this light and destiantion mask 

				//for (int iSlice = combinedMin; iSlice <= combinedMax; ++iSlice)
				uint flatThreadId = groupThreadId.y * 8 + groupThreadId.x;
				if (flatThreadId >= combinedMin && flatThreadId <= combinedMax)
				{
#if FROXELS_CULL_PARTICLES

#else
					int numFroxelGroupsX = (pSrt->m_numFroxelsXY.x + 7) / 8;
					int numFroxelGroupsY = (pSrt->m_numFroxelsXY.y + 7) / 8;
					
					pSrt->m_volumetricSSLightsTileBuffer[tileFlatIndex + mul24(pSrt->m_numFroxelsGroupsInSlice /* numFroxelGroupsX * numFroxelGroupsY */, (flatThreadId + 1))].m_intersectionMask |= __s_lshl_b32(1, lightIndex);
					pSrt->m_volumetricSSLightsTileBuffer[tileFlatIndex + mul24(pSrt->m_numFroxelsGroupsInSlice /* numFroxelGroupsX * numFroxelGroupsY */, (flatThreadId + 1))].m_lightTypeMask |= lightTypeMask; // todo: do the shadowing

					// can also store whether we need to check if the froxel is beyond last sampled
#endif
				}

			}



			combinedLightIntersectionMask = __s_or_b32(combinedLightIntersectionMask, __s_lshl_b32(1, lightIndex));
			combinedLightTypeMask = combinedLightTypeMask | lightTypeMask;

			float4 data0;
			float4 data1;
			float4 data2;
			float4 data3;

			data0.xyzw = float4(finalT0, finalT1, maxIntensityOnSegment, finalAngleCoord);
			data1.xyzw = float4(finalUv0, (finalUv1 - finalUv0) / finalDistance); // scale by distance for easy offset calculation
																				  //pSrt->m_volumetricSSLightsFroxelBuffer[froxelFlatIndex].m_lights[froxelLightIndex].m_data2.xyzw = float4(goodDirFactor, finalRoundedAngleCoord, maxIntensityOnSegmentMin, maxIntensityOnSegmentMax); // scale by distance for easy offset calculation
																				  //data2.xyzw = float4(finalUpVector); // scale by distance for easy offset calculation
																				  //data2.xyzw = float4(finalUv1, finalSliceCoord, 0); // scale by distance for easy offset calculation
																				  // lets say we dont care about v part of uv since issues come from looking perpendicular to light
			data2.xyzw = float4(finalSliceCoord, finalAvgValue, 0, 0);
			data3.xyzw = float4(finalMinUv, finalMaxUv); // just spot light
			#if !FROXELS_CULL_PARTICLES
			if (isOrthoProjector)
			{
				data2.zw = float2(planeStart[2], planeRate[2]);
				data3.xyzw = float4(planeStart[0], planeRate[0], planeStart[1], planeRate[1]);
			}
			#endif

#if VOLUMETRICS_COMPRESS_SS_LIGHTS
			uint4 cdata0 = uint4(__v_cvt_pkrtz_f16_f32(data0.x, data0.y), __v_cvt_pkrtz_f16_f32(data0.z, data0.w), __v_cvt_pkrtz_f16_f32(data1.x, data1.y), __v_cvt_pkrtz_f16_f32(data1.z, data1.w));
			uint4 cdata1 = uint4(__v_cvt_pkrtz_f16_f32(data2.x, data2.y), __v_cvt_pkrtz_f16_f32(data2.z, data2.w), __v_cvt_pkrtz_f16_f32(data3.x, data3.y), __v_cvt_pkrtz_f16_f32(data3.z, data3.w));

			data0 = asfloat4(cdata0);
			data1 = asfloat4(cdata1);
#endif
			if (validFroxel)
			{

				#if FROXELS_CULL_PARTICLES
				froxelLightIndex = lightIndex;

				#if FOG_TEXTURE_DENSITY_ONLY
				pSrt->m_volumetricSSPartsFroxelBuffer[tileFlatIndex * VOLUMETRICS_MAX_SS_PARTS + froxelLightIndex].m_data0[flatGroupThreadId].xyzw = data0;
				pSrt->m_volumetricSSPartsFroxelBuffer[tileFlatIndex * VOLUMETRICS_MAX_SS_PARTS + froxelLightIndex].m_data1[flatGroupThreadId].xyzw = data1;
				#else
				pSrt->m_volumetricSSPartsFroxelBuffer[froxelFlatIndex].m_lights[froxelLightIndex].m_data0.xyzw = data0;
				pSrt->m_volumetricSSPartsFroxelBuffer[froxelFlatIndex].m_lights[froxelLightIndex].m_data1.xyzw = data1;
				#endif

				#if !VOLUMETRICS_COMPRESS_SS_LIGHTS
				pSrt->m_volumetricSSPartsFroxelBuffer[tileFlatIndex * VOLUMETRICS_MAX_SS_PARTS + froxelLightIndex].m_lights[froxelLightIndex].m_data2.xyzw = data2;
				pSrt->m_volumetricSSPartsFroxelBuffer[tileFlatIndex * VOLUMETRICS_MAX_SS_PARTS + froxelLightIndex].m_lights[froxelLightIndex].m_data3.xyzw = data3;
				#endif

				#else
				froxelLightIndex = lightIndex;
				#if FOG_TEXTURE_DENSITY_ONLY
				pSrt->m_volumetricSSLightsFroxelBuffer[mul24(tileFlatIndex, pSrt->m_numVolLights) + froxelLightIndex].m_data0[flatGroupThreadId].xyzw = data0;
				pSrt->m_volumetricSSLightsFroxelBuffer[mul24(tileFlatIndex, pSrt->m_numVolLights) + froxelLightIndex].m_data1[flatGroupThreadId].xyzw = data1;
				#else
				pSrt->m_volumetricSSLightsFroxelBuffer[froxelFlatIndex].m_lights[froxelLightIndex].m_data0.xyzw = data0;
				pSrt->m_volumetricSSLightsFroxelBuffer[froxelFlatIndex].m_lights[froxelLightIndex].m_data1.xyzw = data1;
				#endif
				#if !VOLUMETRICS_COMPRESS_SS_LIGHTS
				pSrt->m_volumetricSSLightsFroxelBuffer[froxelFlatIndex].m_lights[froxelLightIndex].m_data2.xyzw = data2;
				pSrt->m_volumetricSSLightsFroxelBuffer[froxelFlatIndex].m_lights[froxelLightIndex].m_data3.xyzw = data3;
				#endif

				#endif
			}

			froxelLightIndex += 1;
		}
	} // for each light

	#if FROXELS_CULL_PARTICLES
	pSrt->m_volumetricSSPartsTileBuffer[tileFlatIndex].m_intersectionMask = combinedLightIntersectionMask;
	pSrt->m_volumetricSSPartsTileBuffer[tileFlatIndex].m_lightTypeMask = combinedLightTypeMask;
	#else
	pSrt->m_volumetricSSLightsTileBuffer[tileFlatIndex].m_intersectionMask = combinedLightIntersectionMask;
	pSrt->m_volumetricSSLightsTileBuffer[tileFlatIndex].m_lightTypeMask = combinedLightTypeMask;
	#endif
	// parallel reduction to find min max of interection times in whole group

	float combinedMin;
	float combinedMax;
	
	{
		float maxZ_01 = max(maxIntersectionZ, LaneSwizzle(maxIntersectionZ, 0x1f, 0x00, 0x01));
		float maxZ_02 = max(maxZ_01, LaneSwizzle(maxZ_01, 0x1f, 0x00, 0x02));
		float maxZ_04 = max(maxZ_02, LaneSwizzle(maxZ_02, 0x1f, 0x00, 0x04));
		float maxZ_08 = max(maxZ_04, LaneSwizzle(maxZ_04, 0x1f, 0x00, 0x08));
		float maxZ_10 = max(maxZ_08, LaneSwizzle(maxZ_08, 0x1f, 0x00, 0x10));
		float maxZ = max(ReadLane(maxZ_10, 0x00), ReadLane(maxZ_10, 0x20));
	
		combinedMax = maxZ;
	}
	
	{
		float minZ_01 = min(minIntersectionZ, LaneSwizzle(minIntersectionZ, 0x1f, 0x00, 0x01));
		float minZ_02 = min(minZ_01, LaneSwizzle(minZ_01, 0x1f, 0x00, 0x02));
		float minZ_04 = min(minZ_02, LaneSwizzle(minZ_02, 0x1f, 0x00, 0x04));
		float minZ_08 = min(minZ_04, LaneSwizzle(minZ_04, 0x1f, 0x00, 0x08));
		float minZ_10 = min(minZ_08, LaneSwizzle(minZ_08, 0x1f, 0x00, 0x10));
		float minZ = min(ReadLane(minZ_10, 0x00), ReadLane(minZ_10, 0x20));
	
		combinedMin = minZ;
	}

	uint occupancyLo = uint(occupancyMask & 0x00000000FFFFFFFF);
	uint occupancyHi = uint(__v_lshr_b64(occupancyMask, 32) & 0x00000000FFFFFFFF);

	uint combinedOccupancyLo = 0;
	uint combinedOccupancyHi = 0;
	{
		uint combinedInt_01 = (occupancyLo | LaneSwizzle(occupancyLo, 0x1f, 0x00, 0x01));
		uint combinedInt_02 = (combinedInt_01 | LaneSwizzle(combinedInt_01, 0x1f, 0x00, 0x02));
		uint combinedInt_04 = (combinedInt_02 | LaneSwizzle(combinedInt_02, 0x1f, 0x00, 0x04));
		uint combinedInt_08 = (combinedInt_04 | LaneSwizzle(combinedInt_04, 0x1f, 0x00, 0x08));
		uint combinedInt_10 = (combinedInt_08 | LaneSwizzle(combinedInt_08, 0x1f, 0x00, 0x10));
		uint combinedInt = (ReadLane(combinedInt_10, 0x00) | ReadLane(combinedInt_10, 0x20));

		combinedOccupancyLo = combinedInt;
	}
	{
		uint combinedInt_01 = (occupancyHi | LaneSwizzle(occupancyHi, 0x1f, 0x00, 0x01));
		uint combinedInt_02 = (combinedInt_01 | LaneSwizzle(combinedInt_01, 0x1f, 0x00, 0x02));
		uint combinedInt_04 = (combinedInt_02 | LaneSwizzle(combinedInt_02, 0x1f, 0x00, 0x04));
		uint combinedInt_08 = (combinedInt_04 | LaneSwizzle(combinedInt_04, 0x1f, 0x00, 0x08));
		uint combinedInt_10 = (combinedInt_08 | LaneSwizzle(combinedInt_08, 0x1f, 0x00, 0x10));
		uint combinedInt = (ReadLane(combinedInt_10, 0x00) | ReadLane(combinedInt_10, 0x20));

		combinedOccupancyHi = combinedInt;
	}

	// we use separate extra buffer because it is read in tile classification pass, not lighting pass
	ulong combinedOccupancy = __v_lshl_b64(combinedOccupancyHi, 32) | ulong(combinedOccupancyLo);
	#if FROXELS_CULL_PARTICLES
	pSrt->m_volumetricSSPartsTileBufferExtra[tileFlatIndex].m_minZ = combinedMin;
	pSrt->m_volumetricSSPartsTileBufferExtra[tileFlatIndex].m_maxZ = combinedMax;

	pSrt->m_volumetricSSPartsTileBufferExtra[tileFlatIndex].m_lightOccupancyBits = combinedOccupancy;
	#else

	pSrt->m_volumetricSSLightsTileBufferExtra[tileFlatIndex].m_minZ = combinedMin;
	pSrt->m_volumetricSSLightsTileBufferExtra[tileFlatIndex].m_maxZ = combinedMax;

	pSrt->m_volumetricSSLightsTileBufferExtra[tileFlatIndex].m_lightOccupancyBits = combinedOccupancy;
	#endif
}

struct AdditionalLightSrt
{
	uint lightIndex;
	uint size;
	float invSize;
	float param0;
	float param1;
};

struct VolumetricsFogSrtExpWrap
{
	VolumetricsFogSrt *pSrt;
	AdditionalLightSrt *pAddlSrt;
};


float SpotGetLinearDepth(float ndcDepth, LightDesc lightDesc)
{
	uint lightType = lightDesc.m_typeData & LIGHT_TYPE_BITS;
	uint isOrtho = lightDesc.m_typeData & LIGHT_IS_ORTHO_BITS;
	uint isOrthoProjector = (lightType == kProjectorLight) && isOrtho;

	float linearDepth = lightDesc.m_depthOffset / (((ndcDepth - lightDesc.m_depthOffsetConst) / lightDesc.m_depthOffsetSlope) - lightDesc.m_depthScale);

	if (isOrthoProjector)
		linearDepth = (ndcDepth - lightDesc.m_depthOffsetConst) / lightDesc.m_depthOffsetSlope;

	float linearepthNorm = saturate(linearDepth / (FROXELS_SPOT_HALF_RANGE * 2)); // max 32 meters 0..1 value

	return linearepthNorm;
}

/*
	float depth;
	if (lightType == kProjectorLight && isOrtho)
	{
		ndcDepth = linDepth * lightDesc.m_depthOffsetSlope + lightDesc.m_depthOffsetConst;
		...
		linDepth = (ndcDepth - lightDesc.m_depthOffsetConst) / lightDesc.m_depthOffsetSlope;
	}
	else
	{
		lightSpacePos.xy /= lightSpacePos.z;

		//lightSpacePos.xy = shadowUvLerp;

		lightSpacePos.z += -froxelLineAlongLightDir * pSrt->m_spotShadowOffsetAlongDir;

		ndcDepth = (lightDesc.m_depthScale + lightDesc.m_depthOffset / linDepth) * lightDesc.m_depthOffsetSlope + lightDesc.m_depthOffsetConst;
		..
		(ndcDepth - lightDesc.m_depthOffsetConst) / lightDesc.m_depthOffsetSlope - lightDesc.m_depthScale =  lightDesc.m_depthOffset / linDepth
		.. 
		linDepth = lightDesc.m_depthOffset / ((ndcDepth - lightDesc.m_depthOffsetConst) / lightDesc.m_depthOffsetSlope - lightDesc.m_depthScale)
	}
*/

float SpotDepthToStoredValue(float ndcDepth, LightDesc lightDesc, float expShadowConstant, float reverseShadow)
{
	uint lightType = lightDesc.m_typeData & LIGHT_TYPE_BITS;
	uint isOrtho = lightDesc.m_typeData & LIGHT_IS_ORTHO_BITS;
	uint isOrthoProjector = (lightType == kProjectorLight) && isOrtho;

#if FROXELS_USE_EXP_SHADOWS_FOR_SPOT_LIGHTS

	// need to convert stored depth to linear depth

	// this is how we go from linear to stored depth:
	// depth = (lightDesc.m_depthScale + lightDesc.m_depthOffset / linearDepth) * lightDesc.m_depthOffsetSlope + lightDesc.m_depthOffsetConst;

	// depth - lightDesc.m_depthOffsetConst = (lightDesc.m_depthScale + lightDesc.m_depthOffset / linearDepth) * lightDesc.m_depthOffsetSlope
	// (depth - lightDesc.m_depthOffsetConst) / lightDesc.m_depthOffsetSlope = (lightDesc.m_depthScale + lightDesc.m_depthOffset / linearDepth)
	// (depth - lightDesc.m_depthOffsetConst) / lightDesc.m_depthOffsetSlope = lightDesc.m_depthScale + lightDesc.m_depthOffset / linearDepth
	// [(depth - lightDesc.m_depthOffsetConst) / lightDesc.m_depthOffsetSlope] - lightDesc.m_depthScale = lightDesc.m_depthOffset / linearDepth
	// linearDepth = lightDesc.m_depthOffset / ([(depth - lightDesc.m_depthOffsetConst) / lightDesc.m_depthOffsetSlope] - lightDesc.m_depthScale)

	float linearDepth = lightDesc.m_depthOffset / (((ndcDepth - lightDesc.m_depthOffsetConst) / lightDesc.m_depthOffsetSlope) - lightDesc.m_depthScale);

	if (isOrthoProjector)
		linearDepth = (ndcDepth - lightDesc.m_depthOffsetConst) / lightDesc.m_depthOffsetSlope;

	float linearepthNorm = saturate(linearDepth / (FROXELS_SPOT_HALF_RANGE * 2)); // max 32 meters 0..1 value


	// keep as 0..1
	// linearepthNorm = linearepthNorm * 2.0 - 1.0; // -1..1
	
	//shadowExpValue = min(pSrt->m_shadowDistOffset, shadowExpValue);
	
	#if FROXELS_ALWAYS_REVERSE_SHADOWS_FOR_SPOT_LIGHTS
		linearepthNorm *= -1;
	#else
		linearepthNorm *= (reverseShadow > 0.0f ? -1 : 1);
	#endif

	float expMap = exp2(linearepthNorm * expShadowConstant);

	return expMap;

#else
	return ndcDepth;
#endif
}


[NUM_THREADS(8, 8, 1)] //
void CS_ConvertSpotShadowToExponential(const uint3 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrtExpWrap srtWrap : S_SRT_DATA)
{

	VolumetricsFogSrt *pSrt = srtWrap.pSrt;

	const LightDesc lightDesc = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_lights[srtWrap.pAddlSrt->lightIndex];

	
	uint bufferId = lightDesc.m_localShadowTextureIdx;
	uint2 pos = dispatchId.xy;
	uint3 destPos = uint3(pos, bufferId);

	float ndcDepth = pSrt->m_pLocalShadowTextures->Load(pos, bufferId);

	pSrt->m_destShadowSpotExpArray[destPos] = SpotDepthToStoredValue(ndcDepth, lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow);
}


[NUM_THREADS(8, 8, 1)] //
void CS_ConvertSpotShadowToExponential2x2To1x1(const uint3 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrtExpWrap srtWrap : S_SRT_DATA)
{

	VolumetricsFogSrt *pSrt = srtWrap.pSrt;

	const LightDesc lightDesc = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_lights[srtWrap.pAddlSrt->lightIndex];

	uint bufferId = lightDesc.m_localShadowTextureIdx;
	uint2 pos = dispatchId.xy;
	uint3 destPos = uint3(pos, bufferId);
	uint2 srcPos = pos * 2;

	float ndcDepth0 = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(0, 0), bufferId);
	float ndcDepth1 = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(1, 0), bufferId);
	float ndcDepth2 = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(0, 1), bufferId);
	float ndcDepth3 = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(1, 1), bufferId);

	float destDepth0 = SpotDepthToStoredValue(ndcDepth0, lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow);
	float destDepth1 = SpotDepthToStoredValue(ndcDepth1, lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow);
	float destDepth2 = SpotDepthToStoredValue(ndcDepth2, lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow);
	float destDepth3 = SpotDepthToStoredValue(ndcDepth3, lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow);

	float destDepthLinear0 = SpotGetLinearDepth(ndcDepth0, lightDesc);
	float destDepthLinear1 = SpotGetLinearDepth(ndcDepth1, lightDesc);
	float destDepthLinear2 = SpotGetLinearDepth(ndcDepth2, lightDesc);
	float destDepthLinear3 = SpotGetLinearDepth(ndcDepth3, lightDesc);


	float destDepth = destDepth0 * 0.25 + destDepth1 * 0.25 + destDepth2 * 0.25 + destDepth3 * 0.25;

#if !FROXELS_USE_EXP_SHADOWS_FOR_SPOT_LIGHTS
	destDepth = min(destDepth0, min(destDepth1, min(destDepth2, destDepth3)));
#endif

	pSrt->m_destShadowSpotExpArray[destPos] = destDepth;


	float destDepthAvg = destDepthLinear0 + destDepthLinear1 + destDepthLinear2 + destDepthLinear3;

	destDepthAvg /= 4;

	float destDepthSqrAvg = destDepthLinear0 * destDepthLinear0 + destDepthLinear1 * destDepthLinear1 + destDepthLinear2 * destDepthLinear2 + destDepthLinear3 * destDepthLinear3;

	destDepthSqrAvg /= 4;

	pSrt->m_destShadowSpotExpArray[uint3(destPos.xy, pSrt->m_numLocalShadows)] = (destDepthAvg);
	pSrt->m_destShadowSpotExpArray[uint3(destPos.xy, pSrt->m_numLocalShadows + 1)] = (destDepthSqrAvg);
}

[NUM_THREADS(8, 8, 1)] //
void CS_ConvertSpotShadowToExponential4x4To1x1(const uint3 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrtExpWrap srtWrap : S_SRT_DATA)
{

	VolumetricsFogSrt *pSrt = srtWrap.pSrt;

	const LightDesc lightDesc = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_lights[srtWrap.pAddlSrt->lightIndex];

	uint bufferId = lightDesc.m_localShadowTextureIdx;
	uint2 pos = dispatchId.xy;
	uint3 destPos = uint3(pos, bufferId);
	uint2 srcPos = pos * 4;

	float ndcDepth0  = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(0, 0), bufferId);
	float ndcDepth1  = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(1, 0), bufferId);
	float ndcDepth2  = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(2, 0), bufferId);
	float ndcDepth3  = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(3, 0), bufferId);
	float ndcDepth4  = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(0, 1), bufferId);
	float ndcDepth5  = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(1, 1), bufferId);
	float ndcDepth6  = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(2, 1), bufferId);
	float ndcDepth7  = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(3, 1), bufferId);
	float ndcDepth8  = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(0, 2), bufferId);
	float ndcDepth9  = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(1, 2), bufferId);
	float ndcDepth10 = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(2, 2), bufferId);
	float ndcDepth11 = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(3, 2), bufferId);
	float ndcDepth12 = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(0, 3), bufferId);
	float ndcDepth13 = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(1, 3), bufferId);
	float ndcDepth14 = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(2, 3), bufferId);
	float ndcDepth15 = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(3, 3), bufferId);
	
	float k = 1.0 / 16.0;

	float destDepth0  = SpotDepthToStoredValue(ndcDepth0,  lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow) * k;
	float destDepth1  = SpotDepthToStoredValue(ndcDepth1,  lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow) * k;
	float destDepth2  = SpotDepthToStoredValue(ndcDepth2,  lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow) * k;
	float destDepth3  = SpotDepthToStoredValue(ndcDepth3,  lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow) * k;
	float destDepth4  = SpotDepthToStoredValue(ndcDepth4,  lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow) * k;
	float destDepth5  = SpotDepthToStoredValue(ndcDepth5,  lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow) * k;
	float destDepth6  = SpotDepthToStoredValue(ndcDepth6,  lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow) * k;
	float destDepth7  = SpotDepthToStoredValue(ndcDepth7,  lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow) * k;
	float destDepth8  = SpotDepthToStoredValue(ndcDepth8,  lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow) * k;
	float destDepth9  = SpotDepthToStoredValue(ndcDepth9,  lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow) * k;
	float destDepth10 = SpotDepthToStoredValue(ndcDepth10, lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow) * k;
	float destDepth11 = SpotDepthToStoredValue(ndcDepth11, lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow) * k;
	float destDepth12 = SpotDepthToStoredValue(ndcDepth12, lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow) * k;
	float destDepth13 = SpotDepthToStoredValue(ndcDepth13, lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow) * k;
	float destDepth14 = SpotDepthToStoredValue(ndcDepth14, lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow) * k;
	float destDepth15 = SpotDepthToStoredValue(ndcDepth15, lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow) * k;

	float destDepthLinear0 = SpotGetLinearDepth(ndcDepth0, lightDesc);
	float destDepthLinear1 = SpotGetLinearDepth(ndcDepth1, lightDesc);
	float destDepthLinear2 = SpotGetLinearDepth(ndcDepth2, lightDesc);
	float destDepthLinear3 = SpotGetLinearDepth(ndcDepth3, lightDesc);
	float destDepthLinear4 = SpotGetLinearDepth(ndcDepth4, lightDesc);
	float destDepthLinear5 = SpotGetLinearDepth(ndcDepth5, lightDesc);
	float destDepthLinear6 = SpotGetLinearDepth(ndcDepth6, lightDesc);
	float destDepthLinear7 = SpotGetLinearDepth(ndcDepth7, lightDesc);
	float destDepthLinear8 = SpotGetLinearDepth(ndcDepth8, lightDesc);
	float destDepthLinear9 = SpotGetLinearDepth(ndcDepth9, lightDesc);
	float destDepthLinear10 = SpotGetLinearDepth(ndcDepth10, lightDesc);
	float destDepthLinear11 = SpotGetLinearDepth(ndcDepth11, lightDesc);
	float destDepthLinear12 = SpotGetLinearDepth(ndcDepth12, lightDesc);
	float destDepthLinear13 = SpotGetLinearDepth(ndcDepth13, lightDesc);
	float destDepthLinear14 = SpotGetLinearDepth(ndcDepth14, lightDesc);
	float destDepthLinear15 = SpotGetLinearDepth(ndcDepth15, lightDesc);

	float destDepthAvg = destDepthLinear0 + destDepthLinear1 + destDepthLinear2 + destDepthLinear3
		+ destDepthLinear4 + destDepthLinear5 + destDepthLinear6 + destDepthLinear7
		+ destDepthLinear8 + destDepthLinear9 + destDepthLinear10 + destDepthLinear11
		+ destDepthLinear12 + destDepthLinear13 + destDepthLinear14 + destDepthLinear15;

	destDepthAvg /= 16;

	float destDepthSqrAvg = destDepthLinear0 * destDepthLinear0 + destDepthLinear1 * destDepthLinear1 + destDepthLinear2 * destDepthLinear2 + destDepthLinear3 * destDepthLinear3
		+ destDepthLinear4 * destDepthLinear4 + destDepthLinear5 * destDepthLinear5 + destDepthLinear6 * destDepthLinear6 + destDepthLinear7 * destDepthLinear7
		+ destDepthLinear8 * destDepthLinear8 + destDepthLinear9 * destDepthLinear9 + destDepthLinear10 * destDepthLinear10 + destDepthLinear11 * destDepthLinear11
		+ destDepthLinear12 * destDepthLinear12 + destDepthLinear13 * destDepthLinear13 + destDepthLinear14 * destDepthLinear14 + destDepthLinear15 * destDepthLinear15;

	destDepthSqrAvg /= 16;

	float destDepth = destDepth0 + destDepth1 + destDepth2 + destDepth3
		+ destDepth4 + destDepth5 + destDepth6 + destDepth7
		+ destDepth8 + destDepth9 + destDepth10 + destDepth11
		+ destDepth12 + destDepth13 + destDepth14 + destDepth15;


	k = 1.0 / 4;

	#if !FROXELS_USE_EXP_SHADOWS_FOR_SPOT_LIGHTS
	k = 1;
	#endif
	float maxQ0 = k * SpotDepthToStoredValue(min(ndcDepth0,  min(ndcDepth1,  min(ndcDepth4, ndcDepth5))), lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow);
	float maxQ1 = k * SpotDepthToStoredValue(min(ndcDepth2,  min(ndcDepth3,  min(ndcDepth6, ndcDepth7))), lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow);
	float maxQ2 = k * SpotDepthToStoredValue(min(ndcDepth8,  min(ndcDepth9,  min(ndcDepth12, ndcDepth13))), lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow);
	float maxQ3 = k * SpotDepthToStoredValue(min(ndcDepth10, min(ndcDepth11, min(ndcDepth14, ndcDepth15))), lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow);

	destDepth = maxQ0 + maxQ1 + maxQ2 + maxQ3;

#if !FROXELS_USE_EXP_SHADOWS_FOR_SPOT_LIGHTS
	destDepth = min(maxQ0, min(maxQ1, min(maxQ2, maxQ3)));
#endif

	pSrt->m_destShadowSpotExpArray[destPos] = destDepth;
	pSrt->m_destShadowSpotExpArray[uint3(destPos.xy, pSrt->m_numLocalShadows)] = (destDepthAvg);
	pSrt->m_destShadowSpotExpArray[uint3(destPos.xy, pSrt->m_numLocalShadows+1)] = (destDepthSqrAvg);
}

[NUM_THREADS(8, 8, 1)] //
void CS_ConvertSpotShadowToExponential8x8To1x1(const uint3 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrtExpWrap srtWrap : S_SRT_DATA)
{

	VolumetricsFogSrt *pSrt = srtWrap.pSrt;

	const LightDesc lightDesc = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_lights[srtWrap.pAddlSrt->lightIndex];

	uint bufferId = lightDesc.m_localShadowTextureIdx;
	uint2 pos = dispatchId.xy;

	int kSize = 8;

	uint3 destPos = uint3(pos, bufferId);
	uint2 srcPos = pos * kSize;
	float destDepth = 0;
	float minDepth = 0.0;
	for (int y = 0; y < kSize; ++y)
	{
		for (int x = 0; x < kSize; ++x)
		{
			float ndcDepth = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(x, y), bufferId);
			minDepth = max(minDepth, ndcDepth);
		}
	}

	destDepth += SpotDepthToStoredValue(minDepth, lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow);

	pSrt->m_destShadowSpotExpArray[destPos] = destDepth;
}


[NUM_THREADS(8, 8, 1)] //
void CS_ConvertSpotShadowToExponential16x16To1x1(const uint3 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrtExpWrap srtWrap : S_SRT_DATA)
{
	VolumetricsFogSrt *pSrt = srtWrap.pSrt;

	const LightDesc lightDesc = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_lights[srtWrap.pAddlSrt->lightIndex];

	uint bufferId = lightDesc.m_localShadowTextureIdx;
	uint2 pos = dispatchId.xy;

	int kSize = 16;

	uint3 destPos = uint3(pos, bufferId);
	uint2 srcPos = pos * kSize;
	float destDepth = 0;
	float minDepth = 0.0;
	for (int y = 0; y < kSize; ++y)
	{
		for (int x = 0; x < kSize; ++x)
		{
			float ndcDepth = pSrt->m_pLocalShadowTextures->Load(srcPos + uint2(x, y), bufferId);
			minDepth = max(minDepth, ndcDepth);
		}
	}

	destDepth += SpotDepthToStoredValue(minDepth, lightDesc, pSrt->m_spotLightExpShadowConstant, pSrt->m_reverseShadow);

	pSrt->m_destShadowSpotExpArray[destPos] = destDepth;
}


[NUM_THREADS(8, 8, 1)] //
void CS_DilateSpotShadow0(const uint3 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrtExpWrap srtWrap : S_SRT_DATA)
{

	VolumetricsFogSrt *pSrt = srtWrap.pSrt;

	const LightDesc lightDesc = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_lights[srtWrap.pAddlSrt->lightIndex];

	uint bufferId = lightDesc.m_localShadowTextureIdx;
	uint2 pos = dispatchId.xy;

	int kSteps = srtWrap.pAddlSrt->param0;
	
	float maxShadow = 1.0f;
	uint spotShadowDimX, spotShadowDimY, spotShadowDimZ;
	pSrt->m_shadowSpotExpArray.GetDimensionsFast(spotShadowDimX, spotShadowDimY, spotShadowDimZ);

	for (int iy = -kSteps; iy <= kSteps; iy++)
	{
		for (int ix = -kSteps; ix <= kSteps; ix++)
		{
			float shadowVal = pSrt->m_shadowSpotExpArray[int3(max(int2(0, 0), min(int2(spotShadowDimX, spotShadowDimY), int2(pos + int2(ix, iy)))), bufferId)];
			maxShadow = min(maxShadow, shadowVal);

			//maxShadow += shadowVal / ((kSteps*2+1) * (kSteps*2+1));
		}
	}

	pSrt->m_destShadowSpotExpArray[uint3(pos, pSrt->m_numLocalShadows)] = maxShadow;
}

[NUM_THREADS(8, 8, 1)] //
void CS_DilateSpotShadow1(const uint3 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrtExpWrap srtWrap : S_SRT_DATA)
{

	VolumetricsFogSrt *pSrt = srtWrap.pSrt;

	const LightDesc lightDesc = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_lights[srtWrap.pAddlSrt->lightIndex];

	uint bufferId = lightDesc.m_localShadowTextureIdx;
	uint2 pos = dispatchId.xy;

	pSrt->m_destShadowSpotExpArray[uint3(pos, bufferId)] = pSrt->m_shadowSpotExpArray[uint3(pos, pSrt->m_numLocalShadows)];

	// will need to invvalidate L1 after this
}

[NUM_THREADS(8, 8, 1)] //
void CS_DownSampleSpotDepthVar(const uint3 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrtExpWrap srtWrap : S_SRT_DATA)
{

	VolumetricsFogSrt *pSrt = srtWrap.pSrt;

	const LightDesc lightDesc = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_lights[srtWrap.pAddlSrt->lightIndex];

	uint bufferId = lightDesc.m_localShadowTextureIdx;
	uint2 pos = dispatchId.xy;

	int kSteps = 4;
	float depthAvg = 0;
	float depthSqrAvg = 0;
	for (int iy = 0; iy < kSteps; iy++)
	{
		for (int ix = 0; ix < kSteps; ix++)
		{
			depthAvg += pSrt->m_shadowSpotExpArray[uint3(pos * kSteps + uint2(ix, iy), pSrt->m_numLocalShadows)].x / (kSteps * kSteps);
			depthSqrAvg += pSrt->m_shadowSpotExpArray[uint3(pos * kSteps + uint2(ix, iy), pSrt->m_numLocalShadows + 1)].x / (kSteps * kSteps);
		}
	}
	float kSize = 64;

	//float2 srcCoord = float2((pos + float2(0.5, 0.5)) / float2(kSize));
	//
	//float3 shadowTexCoord0 = float3(srcCoord, pSrt->m_numLocalShadows);
	//float depthAvg = pSrt->m_shadowSpotExpArray.SampleLevel(pSrt->m_linearSampler, shadowTexCoord0, 0).x;
	//float3 shadowTexCoord1 = float3(srcCoord, pSrt->m_numLocalShadows + 1);
	//float depthSqrAvg = pSrt->m_shadowSpotExpArray.SampleLevel(pSrt->m_linearSampler, shadowTexCoord1, 0).x;

	pSrt->m_destShadowSpotDepthVarArray[uint3(pos, bufferId)] = float2(depthAvg, depthSqrAvg);

}


[NUM_THREADS(8, 8, 1)] //
void CS_BlurSpotDepthVarX(const uint3 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrtExpWrap srtWrap : S_SRT_DATA)
{

	VolumetricsFogSrt *pSrt = srtWrap.pSrt;

	const LightDesc lightDesc = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_lights[srtWrap.pAddlSrt->lightIndex];

	uint bufferId = lightDesc.m_localShadowTextureIdx;
	uint2 pos = dispatchId.xy;

	float kSize = 64;

	
	
	int Width = 64;

	int2 coord = dispatchId.xy;

	// should be safe to access the read because l1 should not have any of this memory in it

	float2 v0 = pSrt->m_shadowSpotDepthVarArray[uint3(coord, bufferId)];

	float2 vm1 = pSrt->m_shadowSpotDepthVarArray[uint3(max(coord.x - 1, 0), coord.y, bufferId)];
	float2 vm2 = pSrt->m_shadowSpotDepthVarArray[uint3(max(coord.x - 2, 0), coord.y, bufferId)];
	float2 vp1 = pSrt->m_shadowSpotDepthVarArray[uint3(min(coord.x + 1, Width - 1), coord.y, bufferId)];
	float2 vp2 = pSrt->m_shadowSpotDepthVarArray[uint3(min(coord.x + 2, Width - 1), coord.y, bufferId)];

	float2 res3 = v0 * 0.44198 + vm1 * 0.27901 + vp1 * 0.27901;

	float2 res5 = v0 * 0.38774 + vm1 * 0.24477 + vp1 * 0.24477 + vm2 * 0.06136 + vp2 * 0.06136;

	//pSrt->m_destVolumetricsDepthInfo[uint3(coord, 2)] = res3;
	pSrt->m_destShadowSpotDepthVarArray[uint3(coord, pSrt->m_numLocalShadows)] = res5;
}




[NUM_THREADS(8, 8, 1)] //
void CS_BlurSpotDepthVarY(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrtExpWrap srtWrap : S_SRT_DATA)
{
	int2 coord = dispatchId.xy;

	VolumetricsFogSrt *pSrt = srtWrap.pSrt;

	const LightDesc lightDesc = pSrt->m_pVolumetricLightsComputeSrt->m_lights.m_lights[srtWrap.pAddlSrt->lightIndex];

	uint bufferId = lightDesc.m_localShadowTextureIdx;
	uint2 pos = dispatchId.xy;


	int Height = 64;

	// should be safe to access the read because l1 should not have any of this memory in it

	float2 v0 = pSrt->m_shadowSpotDepthVarArray[uint3(coord, pSrt->m_numLocalShadows)];
	float2 vm1 = pSrt->m_shadowSpotDepthVarArray[uint3(coord.x, max(coord.y - 1, 0), pSrt->m_numLocalShadows)];
	float2 vm2 = pSrt->m_shadowSpotDepthVarArray[uint3(coord.x, max(coord.y - 2, 0), pSrt->m_numLocalShadows)];
	float2 vp1 = pSrt->m_shadowSpotDepthVarArray[uint3(coord.x, min(coord.y + 1, Height - 1), pSrt->m_numLocalShadows)];
	float2 vp2 = pSrt->m_shadowSpotDepthVarArray[uint3(coord.x, min(coord.y + 2, Height - 1), pSrt->m_numLocalShadows)];

	float2 res3 = v0 * 0.44198 + vm1 * 0.27901 + vp1 * 0.27901;

	float2 res5 = v0 * 0.38774 + vm1 * 0.24477 + vp1 * 0.24477 + vm2 * 0.06136 + vp2 * 0.06136;

	//pSrt->m_destVolumetricsDepthInfo[uint3(coord, 3)] = res3;

	pSrt->m_destShadowSpotDepthVarArray[uint3(coord, bufferId)] = res5; // after this we have to invalidate L1 because we have read this texture before
}


