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
#include "particle-ray-trace-cs-defines.fxi"
#include "particle-ray-trace-cs-ps.fxi"


float HTileToHiZ(uint htile)
{
	return (htile >> (14 + 4)) / float((1 << 14) - 1);
}

[NUM_THREADS(8, 8, 1)] //
void CS_AnalyzeHTileForFroxels3x2(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{

	uint2 groupPos = uint2(8, 8) * groupId.xy;

	uint2 blockId = groupPos + groupThreadId.xy;

	uint tileX0 = blockId.x * 3;
	uint tileX1 = blockId.x * 3 + 1;
	uint tileX2 = blockId.x * 3 + 2;

	uint tileY0 = blockId.y * 3;
	uint tileY1 = blockId.y * 3 + 1;
	uint tileY2 = blockId.y * 3 + 2;


	uint froxelX0 = blockId.x * 2;
	uint froxelX1 = blockId.x * 2 + 1;

	uint froxelY0 = blockId.y * 2;
	uint froxelY1 = blockId.y * 2 + 1;

	// we are going to loookup max distances and combine them and store them per froxel

	uint htileOffset00 = HTileOffsetInDwords(tileX0, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset10 = HTileOffsetInDwords(tileX1, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset20 = HTileOffsetInDwords(tileX2, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);


	uint htileOffset01 = HTileOffsetInDwords(tileX0, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset11 = HTileOffsetInDwords(tileX1, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset21 = HTileOffsetInDwords(tileX2, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);

	uint htileOffset02 = HTileOffsetInDwords(tileX0, tileY2, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset12 = HTileOffsetInDwords(tileX1, tileY2, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset22 = HTileOffsetInDwords(tileX2, tileY2, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);

	uint htile00 = pSrt->m_hTileRO[htileOffset00];
	uint htile10 = pSrt->m_hTileRO[htileOffset10];
	uint htile20 = pSrt->m_hTileRO[htileOffset20];

	uint htile01 = pSrt->m_hTileRO[htileOffset01];
	uint htile11 = pSrt->m_hTileRO[htileOffset11];
	uint htile21 = pSrt->m_hTileRO[htileOffset21];

	uint htile02 = pSrt->m_hTileRO[htileOffset02];
	uint htile12 = pSrt->m_hTileRO[htileOffset12];
	uint htile22 = pSrt->m_hTileRO[htileOffset22];

	float htileHiZ00 = HTileToHiZ(htile00);
	float htileHiZ10 = HTileToHiZ(htile10);
	float htileHiZ20 = HTileToHiZ(htile20);

	float htileHiZ01 = HTileToHiZ(htile01);
	float htileHiZ11 = HTileToHiZ(htile11);
	float htileHiZ21 = HTileToHiZ(htile21);

	float htileHiZ02 = HTileToHiZ(htile02);
	float htileHiZ12 = HTileToHiZ(htile12);
	float htileHiZ22 = HTileToHiZ(htile22);


	float froxelHiZ00 = max(max(htileHiZ00, htileHiZ10), max(htileHiZ01, htileHiZ11));
	float froxelHiZ01 = max(max(htileHiZ02, htileHiZ12), max(htileHiZ01, htileHiZ11));

	float froxelHiZ10 = max(max(htileHiZ10, htileHiZ20), max(htileHiZ11, htileHiZ21));
	float froxelHiZ11 = max(max(htileHiZ12, htileHiZ22), max(htileHiZ11, htileHiZ21));


	// now for reach one of the values, find the intersecting froxel


	// find the froxel that is intersecting with depth
	for (int froxelX = froxelX0; froxelX <= froxelX1; ++froxelX)
	{
		for (int froxelY = froxelY0; froxelY <= froxelY1; ++froxelY)
		{
			float depthVal;
			if (froxelX == froxelX0)
			{
				if (froxelY == froxelY0)
				{
					depthVal = froxelHiZ00;
				}
				else
				{
					depthVal = froxelHiZ01;
				}
			}
			else
			{
				if (froxelY == froxelY0)
				{
					depthVal = froxelHiZ10;
				}
				else
				{
					depthVal = froxelHiZ11;
				}
			}
			

			float linearDepth = max(pSrt->m_fixedDepth, GetLinearDepth(depthVal, pSrt->m_depthParams));

			float froxelSliceFloat = CameraLinearDepthToFroxelZSliceExp(linearDepth, pSrt->m_fogGridOffset);
			float froxelZ = froxelSliceFloat;

			uint3 froxelCoord = uint3(froxelX, froxelY, froxelZ);

			//pSrt->m_destPropertiesFroxels[froxelCoord].z = 1;

			pSrt->m_destVolumetricsScreenSpaceInfoOrig[froxelCoord.xy].x = froxelSliceFloat;
		}
	}
	
}

void StoreHiZHTile(VolumetricsFogSrt *pSrt, uint froxelX, uint froxelY, float depthVal)
{
	float linearDepth = max(pSrt->m_fixedDepth, GetLinearDepth(depthVal, pSrt->m_depthParams));

	float froxelSliceFloat = CameraLinearDepthToFroxelZSliceExp(linearDepth, pSrt->m_fogGridOffset);
	float froxelZ = froxelSliceFloat;

	uint3 froxelCoord = uint3(froxelX, froxelY, froxelZ);

	//pSrt->m_destPropertiesFroxels[froxelCoord].z = 1;

	pSrt->m_destVolumetricsScreenSpaceInfoOrig[froxelCoord.xy].x = froxelSliceFloat;
}


[NUM_THREADS(8, 8, 1)] //
// 3 * 8 = 4 * 6
void CS_AnalyzeHTileForFroxels3x4(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	// 4x4 froxels (6px) = 3x3 tiles

	uint2 groupPos = uint2(8, 8) * groupId.xy;

	uint2 blockId = groupPos + groupThreadId.xy;

	uint tileX0 = blockId.x * 3;
	uint tileX1 = blockId.x * 3 + 1;
	uint tileX2 = blockId.x * 3 + 2;

	uint tileY0 = blockId.y * 3;
	uint tileY1 = blockId.y * 3 + 1;
	uint tileY2 = blockId.y * 3 + 2;


	uint froxelX0 = blockId.x * 4;
	uint froxelX1 = blockId.x * 4 + 1;
	uint froxelX2 = blockId.x * 4 + 2;
	uint froxelX3 = blockId.x * 4 + 3;

	uint froxelY0 = blockId.y * 4;
	uint froxelY1 = blockId.y * 4 + 1;
	uint froxelY2 = blockId.y * 4 + 2;
	uint froxelY3 = blockId.y * 4 + 3;

	// we are going to loookup max distances and combine them and store them per froxel

	uint htileOffset00 = HTileOffsetInDwords(tileX0, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset10 = HTileOffsetInDwords(tileX1, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset20 = HTileOffsetInDwords(tileX2, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);


	uint htileOffset01 = HTileOffsetInDwords(tileX0, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset11 = HTileOffsetInDwords(tileX1, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset21 = HTileOffsetInDwords(tileX2, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);

	uint htileOffset02 = HTileOffsetInDwords(tileX0, tileY2, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset12 = HTileOffsetInDwords(tileX1, tileY2, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset22 = HTileOffsetInDwords(tileX2, tileY2, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);

	uint htile00 = pSrt->m_hTileRO[htileOffset00];
	uint htile10 = pSrt->m_hTileRO[htileOffset10];
	uint htile20 = pSrt->m_hTileRO[htileOffset20];

	uint htile01 = pSrt->m_hTileRO[htileOffset01];
	uint htile11 = pSrt->m_hTileRO[htileOffset11];
	uint htile21 = pSrt->m_hTileRO[htileOffset21];

	uint htile02 = pSrt->m_hTileRO[htileOffset02];
	uint htile12 = pSrt->m_hTileRO[htileOffset12];
	uint htile22 = pSrt->m_hTileRO[htileOffset22];

	float htileHiZ00 = HTileToHiZ(htile00);
	float htileHiZ10 = HTileToHiZ(htile10);
	float htileHiZ20 = HTileToHiZ(htile20);

	float htileHiZ01 = HTileToHiZ(htile01);
	float htileHiZ11 = HTileToHiZ(htile11);
	float htileHiZ21 = HTileToHiZ(htile21);

	float htileHiZ02 = HTileToHiZ(htile02);
	float htileHiZ12 = HTileToHiZ(htile12);
	float htileHiZ22 = HTileToHiZ(htile22);



	float froxelHiZ00 = htileHiZ00;
	float froxelHiZ01 = max(htileHiZ00, htileHiZ01);
	float froxelHiZ02 = max(htileHiZ01, htileHiZ02);
	float froxelHiZ03 = htileHiZ02;

	float froxelHiZ10 = max(htileHiZ00, htileHiZ10);
	float froxelHiZ11 = max(max(htileHiZ00, htileHiZ01), max(htileHiZ10, htileHiZ11));
	float froxelHiZ12 = max(max(htileHiZ01, htileHiZ02), max(htileHiZ11, htileHiZ12));
	float froxelHiZ13 = max(htileHiZ02, htileHiZ12);

	float froxelHiZ20 = max(htileHiZ10, htileHiZ20);
	float froxelHiZ21 = max(max(htileHiZ10, htileHiZ11), max(htileHiZ20, htileHiZ21));
	float froxelHiZ22 = max(max(htileHiZ11, htileHiZ12), max(htileHiZ21, htileHiZ22));
	float froxelHiZ23 = max(htileHiZ12, htileHiZ22);

	float froxelHiZ30 = htileHiZ20;
	float froxelHiZ31 = max(htileHiZ20, htileHiZ21);
	float froxelHiZ32 = max(htileHiZ21, htileHiZ22);
	float froxelHiZ33 = htileHiZ22;

	// now for reach one of the values, find the intersecting froxel

	StoreHiZHTile(pSrt, froxelX0, froxelY0, froxelHiZ00);
	StoreHiZHTile(pSrt, froxelX1, froxelY0, froxelHiZ10);
	StoreHiZHTile(pSrt, froxelX2, froxelY0, froxelHiZ20);
	StoreHiZHTile(pSrt, froxelX3, froxelY0, froxelHiZ30);

	StoreHiZHTile(pSrt, froxelX0, froxelY1, froxelHiZ01);
	StoreHiZHTile(pSrt, froxelX1, froxelY1, froxelHiZ11);
	StoreHiZHTile(pSrt, froxelX2, froxelY1, froxelHiZ21);
	StoreHiZHTile(pSrt, froxelX3, froxelY1, froxelHiZ31);

	StoreHiZHTile(pSrt, froxelX0, froxelY2, froxelHiZ02);
	StoreHiZHTile(pSrt, froxelX1, froxelY2, froxelHiZ12);
	StoreHiZHTile(pSrt, froxelX2, froxelY2, froxelHiZ22);
	StoreHiZHTile(pSrt, froxelX3, froxelY2, froxelHiZ32);

	StoreHiZHTile(pSrt, froxelX0, froxelY3, froxelHiZ03);
	StoreHiZHTile(pSrt, froxelX1, froxelY3, froxelHiZ13);
	StoreHiZHTile(pSrt, froxelX2, froxelY3, froxelHiZ23);
	StoreHiZHTile(pSrt, froxelX3, froxelY3, froxelHiZ33);
}

[NUM_THREADS(8, 8, 1)] //
void CS_AnalyzeHTileForFroxels5x4(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{

	uint2 groupPos = uint2(8, 8) * groupId.xy;

	uint2 blockId = groupPos + groupThreadId.xy;

	uint tileX0 = blockId.x * 5;
	uint tileX1 = blockId.x * 5 + 1;
	uint tileX2 = blockId.x * 5 + 2;
	uint tileX3 = blockId.x * 5 + 3;
	uint tileX4 = blockId.x * 5 + 4;

	uint tileY0 = blockId.y * 5;
	uint tileY1 = blockId.y * 5 + 1;
	uint tileY2 = blockId.y * 5 + 2;
	uint tileY3 = blockId.y * 5 + 3;
	uint tileY4 = blockId.y * 5 + 4;


	uint froxelX0 = blockId.x * 4;
	uint froxelX1 = blockId.x * 4 + 1;
	uint froxelX2 = blockId.x * 4 + 2;
	uint froxelX3 = blockId.x * 4 + 3;

	uint froxelY0 = blockId.y * 4;
	uint froxelY1 = blockId.y * 4 + 1;
	uint froxelY2 = blockId.y * 4 + 2;
	uint froxelY3 = blockId.y * 4 + 3;

	// we are going to loookup max distances and combine them and store them per froxel

	uint htileOffset00 = HTileOffsetInDwords(tileX0, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset10 = HTileOffsetInDwords(tileX1, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset20 = HTileOffsetInDwords(tileX2, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset30 = HTileOffsetInDwords(tileX3, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset40 = HTileOffsetInDwords(tileX4, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);


	uint htileOffset01 = HTileOffsetInDwords(tileX0, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset11 = HTileOffsetInDwords(tileX1, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset21 = HTileOffsetInDwords(tileX2, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset31 = HTileOffsetInDwords(tileX3, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset41 = HTileOffsetInDwords(tileX4, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);

	uint htileOffset02 = HTileOffsetInDwords(tileX0, tileY2, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset12 = HTileOffsetInDwords(tileX1, tileY2, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset22 = HTileOffsetInDwords(tileX2, tileY2, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset32 = HTileOffsetInDwords(tileX3, tileY2, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset42 = HTileOffsetInDwords(tileX4, tileY2, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);

	uint htileOffset03 = HTileOffsetInDwords(tileX0, tileY3, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset13 = HTileOffsetInDwords(tileX1, tileY3, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset23 = HTileOffsetInDwords(tileX2, tileY3, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset33 = HTileOffsetInDwords(tileX3, tileY3, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset43 = HTileOffsetInDwords(tileX4, tileY3, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);

	uint htileOffset04 = HTileOffsetInDwords(tileX0, tileY4, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset14 = HTileOffsetInDwords(tileX1, tileY4, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset24 = HTileOffsetInDwords(tileX2, tileY4, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset34 = HTileOffsetInDwords(tileX3, tileY4, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset44 = HTileOffsetInDwords(tileX4, tileY4, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);

	uint htile00 = pSrt->m_hTileRO[htileOffset00];
	uint htile10 = pSrt->m_hTileRO[htileOffset10];
	uint htile20 = pSrt->m_hTileRO[htileOffset20];
	uint htile30 = pSrt->m_hTileRO[htileOffset30];
	uint htile40 = pSrt->m_hTileRO[htileOffset40];

	uint htile01 = pSrt->m_hTileRO[htileOffset01];
	uint htile11 = pSrt->m_hTileRO[htileOffset11];
	uint htile21 = pSrt->m_hTileRO[htileOffset21];
	uint htile31 = pSrt->m_hTileRO[htileOffset31];
	uint htile41 = pSrt->m_hTileRO[htileOffset41];

	uint htile02 = pSrt->m_hTileRO[htileOffset02];
	uint htile12 = pSrt->m_hTileRO[htileOffset12];
	uint htile22 = pSrt->m_hTileRO[htileOffset22];
	uint htile32 = pSrt->m_hTileRO[htileOffset32];
	uint htile42 = pSrt->m_hTileRO[htileOffset42];

	uint htile03 = pSrt->m_hTileRO[htileOffset03];
	uint htile13 = pSrt->m_hTileRO[htileOffset13];
	uint htile23 = pSrt->m_hTileRO[htileOffset23];
	uint htile33 = pSrt->m_hTileRO[htileOffset33];
	uint htile43 = pSrt->m_hTileRO[htileOffset43];

	uint htile04 = pSrt->m_hTileRO[htileOffset04];
	uint htile14 = pSrt->m_hTileRO[htileOffset14];
	uint htile24 = pSrt->m_hTileRO[htileOffset24];
	uint htile34 = pSrt->m_hTileRO[htileOffset34];
	uint htile44 = pSrt->m_hTileRO[htileOffset44];

	float htileHiZ00 = HTileToHiZ(htile00);
	float htileHiZ10 = HTileToHiZ(htile10);
	float htileHiZ20 = HTileToHiZ(htile20);
	float htileHiZ30 = HTileToHiZ(htile30);
	float htileHiZ40 = HTileToHiZ(htile40);

	float htileHiZ01 = HTileToHiZ(htile01);
	float htileHiZ11 = HTileToHiZ(htile11);
	float htileHiZ21 = HTileToHiZ(htile21);
	float htileHiZ31 = HTileToHiZ(htile31);
	float htileHiZ41 = HTileToHiZ(htile41);

	float htileHiZ02 = HTileToHiZ(htile02);
	float htileHiZ12 = HTileToHiZ(htile12);
	float htileHiZ22 = HTileToHiZ(htile22);
	float htileHiZ32 = HTileToHiZ(htile32);
	float htileHiZ42 = HTileToHiZ(htile42);

	float htileHiZ03 = HTileToHiZ(htile03);
	float htileHiZ13 = HTileToHiZ(htile13);
	float htileHiZ23 = HTileToHiZ(htile23);
	float htileHiZ33 = HTileToHiZ(htile33);
	float htileHiZ43 = HTileToHiZ(htile43);

	float htileHiZ04 = HTileToHiZ(htile04);
	float htileHiZ14 = HTileToHiZ(htile14);
	float htileHiZ24 = HTileToHiZ(htile24);
	float htileHiZ34 = HTileToHiZ(htile34);
	float htileHiZ44 = HTileToHiZ(htile44);


	float froxelHiZ00 = max(max(htileHiZ00, htileHiZ10), max(htileHiZ01, htileHiZ11));
	float froxelHiZ01 = max(max(htileHiZ01, htileHiZ11), max(htileHiZ02, htileHiZ12));
	float froxelHiZ02 = max(max(htileHiZ02, htileHiZ12), max(htileHiZ03, htileHiZ13));
	float froxelHiZ03 = max(max(htileHiZ03, htileHiZ13), max(htileHiZ04, htileHiZ14));

	float froxelHiZ10 = max(max(htileHiZ10, htileHiZ20), max(htileHiZ11, htileHiZ21));
	float froxelHiZ11 = max(max(htileHiZ11, htileHiZ21), max(htileHiZ12, htileHiZ22));
	float froxelHiZ12 = max(max(htileHiZ12, htileHiZ22), max(htileHiZ13, htileHiZ23));
	float froxelHiZ13 = max(max(htileHiZ13, htileHiZ23), max(htileHiZ14, htileHiZ24));

	float froxelHiZ20 = max(max(htileHiZ20, htileHiZ30), max(htileHiZ21, htileHiZ31));
	float froxelHiZ21 = max(max(htileHiZ21, htileHiZ31), max(htileHiZ22, htileHiZ32));
	float froxelHiZ22 = max(max(htileHiZ22, htileHiZ32), max(htileHiZ23, htileHiZ33));
	float froxelHiZ23 = max(max(htileHiZ23, htileHiZ33), max(htileHiZ24, htileHiZ34));

	float froxelHiZ30 = max(max(htileHiZ30, htileHiZ40), max(htileHiZ31, htileHiZ41));
	float froxelHiZ31 = max(max(htileHiZ31, htileHiZ41), max(htileHiZ32, htileHiZ42));
	float froxelHiZ32 = max(max(htileHiZ32, htileHiZ42), max(htileHiZ33, htileHiZ43));
	float froxelHiZ33 = max(max(htileHiZ33, htileHiZ43), max(htileHiZ34, htileHiZ44));

	// now for reach one of the values, find the intersecting froxel

	StoreHiZHTile(pSrt, froxelX0, froxelY0, froxelHiZ00);
	StoreHiZHTile(pSrt, froxelX1, froxelY0, froxelHiZ10);
	StoreHiZHTile(pSrt, froxelX2, froxelY0, froxelHiZ20);
	StoreHiZHTile(pSrt, froxelX3, froxelY0, froxelHiZ30);

	StoreHiZHTile(pSrt, froxelX0, froxelY1, froxelHiZ01);
	StoreHiZHTile(pSrt, froxelX1, froxelY1, froxelHiZ11);
	StoreHiZHTile(pSrt, froxelX2, froxelY1, froxelHiZ21);
	StoreHiZHTile(pSrt, froxelX3, froxelY1, froxelHiZ31);

	StoreHiZHTile(pSrt, froxelX0, froxelY2, froxelHiZ02);
	StoreHiZHTile(pSrt, froxelX1, froxelY2, froxelHiZ12);
	StoreHiZHTile(pSrt, froxelX2, froxelY2, froxelHiZ22);
	StoreHiZHTile(pSrt, froxelX3, froxelY2, froxelHiZ32);

	StoreHiZHTile(pSrt, froxelX0, froxelY3, froxelHiZ03);
	StoreHiZHTile(pSrt, froxelX1, froxelY3, froxelHiZ13);
	StoreHiZHTile(pSrt, froxelX2, froxelY3, froxelHiZ23);
	StoreHiZHTile(pSrt, froxelX3, froxelY3, froxelHiZ33);
}


[NUM_THREADS(8, 8, 1)] //
void CS_AnalyzeHTileForFroxels5x8(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{

	uint2 groupPos = uint2(8, 8) * groupId.xy;

	uint2 blockId = groupPos + groupThreadId.xy;

	uint tileX0 = blockId.x * 5;
	uint tileX1 = blockId.x * 5 + 1;
	uint tileX2 = blockId.x * 5 + 2;
	uint tileX3 = blockId.x * 5 + 3;
	uint tileX4 = blockId.x * 5 + 4;

	uint tileY0 = blockId.y * 5;
	uint tileY1 = blockId.y * 5 + 1;
	uint tileY2 = blockId.y * 5 + 2;
	uint tileY3 = blockId.y * 5 + 3;
	uint tileY4 = blockId.y * 5 + 4;


	uint froxelX0 = blockId.x * 8;
	uint froxelX1 = blockId.x * 8 + 1;
	uint froxelX2 = blockId.x * 8 + 2;
	uint froxelX3 = blockId.x * 8 + 3;
	uint froxelX4 = blockId.x * 8 + 4;
	uint froxelX5 = blockId.x * 8 + 5;
	uint froxelX6 = blockId.x * 8 + 6;
	uint froxelX7 = blockId.x * 8 + 7;

	uint froxelY0 = blockId.y * 8;
	uint froxelY1 = blockId.y * 8 + 1;
	uint froxelY2 = blockId.y * 8 + 2;
	uint froxelY3 = blockId.y * 8 + 3;
	uint froxelY4 = blockId.y * 8 + 4;
	uint froxelY5 = blockId.y * 8 + 5;
	uint froxelY6 = blockId.y * 8 + 6;
	uint froxelY7 = blockId.y * 8 + 7;

	// we are going to loookup max distances and combine them and store them per froxel

	uint htileOffset00 = HTileOffsetInDwords(tileX0, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset10 = HTileOffsetInDwords(tileX1, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset20 = HTileOffsetInDwords(tileX2, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset30 = HTileOffsetInDwords(tileX3, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset40 = HTileOffsetInDwords(tileX4, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);

	uint htileOffset01 = HTileOffsetInDwords(tileX0, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset11 = HTileOffsetInDwords(tileX1, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset21 = HTileOffsetInDwords(tileX2, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset31 = HTileOffsetInDwords(tileX3, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset41 = HTileOffsetInDwords(tileX4, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);

	uint htileOffset02 = HTileOffsetInDwords(tileX0, tileY2, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset12 = HTileOffsetInDwords(tileX1, tileY2, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset22 = HTileOffsetInDwords(tileX2, tileY2, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset32 = HTileOffsetInDwords(tileX3, tileY2, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset42 = HTileOffsetInDwords(tileX4, tileY2, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);

	uint htileOffset03 = HTileOffsetInDwords(tileX0, tileY3, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset13 = HTileOffsetInDwords(tileX1, tileY3, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset23 = HTileOffsetInDwords(tileX2, tileY3, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset33 = HTileOffsetInDwords(tileX3, tileY3, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset43 = HTileOffsetInDwords(tileX4, tileY3, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);

	uint htileOffset04 = HTileOffsetInDwords(tileX0, tileY4, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset14 = HTileOffsetInDwords(tileX1, tileY4, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset24 = HTileOffsetInDwords(tileX2, tileY4, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset34 = HTileOffsetInDwords(tileX3, tileY4, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset44 = HTileOffsetInDwords(tileX4, tileY4, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);

	uint htile00 = pSrt->m_hTileRO[htileOffset00];
	uint htile10 = pSrt->m_hTileRO[htileOffset10];
	uint htile20 = pSrt->m_hTileRO[htileOffset20];
	uint htile30 = pSrt->m_hTileRO[htileOffset30];
	uint htile40 = pSrt->m_hTileRO[htileOffset40];

	uint htile01 = pSrt->m_hTileRO[htileOffset01];
	uint htile11 = pSrt->m_hTileRO[htileOffset11];
	uint htile21 = pSrt->m_hTileRO[htileOffset21];
	uint htile31 = pSrt->m_hTileRO[htileOffset31];
	uint htile41 = pSrt->m_hTileRO[htileOffset41];

	uint htile02 = pSrt->m_hTileRO[htileOffset02];
	uint htile12 = pSrt->m_hTileRO[htileOffset12];
	uint htile22 = pSrt->m_hTileRO[htileOffset22];
	uint htile32 = pSrt->m_hTileRO[htileOffset32];
	uint htile42 = pSrt->m_hTileRO[htileOffset42];

	uint htile03 = pSrt->m_hTileRO[htileOffset03];
	uint htile13 = pSrt->m_hTileRO[htileOffset13];
	uint htile23 = pSrt->m_hTileRO[htileOffset23];
	uint htile33 = pSrt->m_hTileRO[htileOffset33];
	uint htile43 = pSrt->m_hTileRO[htileOffset43];

	uint htile04 = pSrt->m_hTileRO[htileOffset04];
	uint htile14 = pSrt->m_hTileRO[htileOffset14];
	uint htile24 = pSrt->m_hTileRO[htileOffset24];
	uint htile34 = pSrt->m_hTileRO[htileOffset34];
	uint htile44 = pSrt->m_hTileRO[htileOffset44];

	float htileHiZ00 = HTileToHiZ(htile00);
	float htileHiZ10 = HTileToHiZ(htile10);
	float htileHiZ20 = HTileToHiZ(htile20);
	float htileHiZ30 = HTileToHiZ(htile30);
	float htileHiZ40 = HTileToHiZ(htile40);

	float htileHiZ01 = HTileToHiZ(htile01);
	float htileHiZ11 = HTileToHiZ(htile11);
	float htileHiZ21 = HTileToHiZ(htile21);
	float htileHiZ31 = HTileToHiZ(htile31);
	float htileHiZ41 = HTileToHiZ(htile41);

	float htileHiZ02 = HTileToHiZ(htile02);
	float htileHiZ12 = HTileToHiZ(htile12);
	float htileHiZ22 = HTileToHiZ(htile22);
	float htileHiZ32 = HTileToHiZ(htile32);
	float htileHiZ42 = HTileToHiZ(htile42);

	float htileHiZ03 = HTileToHiZ(htile03);
	float htileHiZ13 = HTileToHiZ(htile13);
	float htileHiZ23 = HTileToHiZ(htile23);
	float htileHiZ33 = HTileToHiZ(htile33);
	float htileHiZ43 = HTileToHiZ(htile43);

	float htileHiZ04 = HTileToHiZ(htile04);
	float htileHiZ14 = HTileToHiZ(htile14);
	float htileHiZ24 = HTileToHiZ(htile24);
	float htileHiZ34 = HTileToHiZ(htile34);
	float htileHiZ44 = HTileToHiZ(htile44);


	// Row 0 (nned htile 0 only)

	float rowHiZ0 = htileHiZ00;
	float rowHiZ1 = htileHiZ10;
	float rowHiZ2 = htileHiZ20;
	float rowHiZ3 = htileHiZ30;
	float rowHiZ4 = htileHiZ40;


	StoreHiZHTile(pSrt, froxelX0, froxelY0, rowHiZ0);
	StoreHiZHTile(pSrt, froxelX1, froxelY0, max(rowHiZ0, rowHiZ1));
	StoreHiZHTile(pSrt, froxelX2, froxelY0, rowHiZ1);
	StoreHiZHTile(pSrt, froxelX3, froxelY0, max(rowHiZ1, rowHiZ2));
	StoreHiZHTile(pSrt, froxelX4, froxelY0, max(rowHiZ2, rowHiZ3));
	StoreHiZHTile(pSrt, froxelX5, froxelY0, rowHiZ3);
	StoreHiZHTile(pSrt, froxelX6, froxelY0, max(rowHiZ3, rowHiZ4));
	StoreHiZHTile(pSrt, froxelX7, froxelY0, rowHiZ4);

	// Row 1 (need htile 0 and 1)

	rowHiZ0 = max(htileHiZ00, htileHiZ01);
	rowHiZ1 = max(htileHiZ10, htileHiZ11);
	rowHiZ2 = max(htileHiZ20, htileHiZ21);
	rowHiZ3 = max(htileHiZ30, htileHiZ31);
	rowHiZ4 = max(htileHiZ40, htileHiZ41);


	StoreHiZHTile(pSrt, froxelX0, froxelY1, rowHiZ0);
	StoreHiZHTile(pSrt, froxelX1, froxelY1, max(rowHiZ0, rowHiZ1));
	StoreHiZHTile(pSrt, froxelX2, froxelY1, rowHiZ1);
	StoreHiZHTile(pSrt, froxelX3, froxelY1, max(rowHiZ1, rowHiZ2));
	StoreHiZHTile(pSrt, froxelX4, froxelY1, max(rowHiZ2, rowHiZ3));
	StoreHiZHTile(pSrt, froxelX5, froxelY1, rowHiZ3);
	StoreHiZHTile(pSrt, froxelX6, froxelY1, max(rowHiZ3, rowHiZ4));
	StoreHiZHTile(pSrt, froxelX7, froxelY1, rowHiZ4);

	// Row 2 (need htile 1 only)

	rowHiZ0 = htileHiZ01;
	rowHiZ1 = htileHiZ11;
	rowHiZ2 = htileHiZ21;
	rowHiZ3 = htileHiZ31;
	rowHiZ4 = htileHiZ41;


	StoreHiZHTile(pSrt, froxelX0, froxelY2, rowHiZ0);
	StoreHiZHTile(pSrt, froxelX1, froxelY2, max(rowHiZ0, rowHiZ1));
	StoreHiZHTile(pSrt, froxelX2, froxelY2, rowHiZ1);
	StoreHiZHTile(pSrt, froxelX3, froxelY2, max(rowHiZ1, rowHiZ2));
	StoreHiZHTile(pSrt, froxelX4, froxelY2, max(rowHiZ2, rowHiZ3));
	StoreHiZHTile(pSrt, froxelX5, froxelY2, rowHiZ3);
	StoreHiZHTile(pSrt, froxelX6, froxelY2, max(rowHiZ3, rowHiZ4));
	StoreHiZHTile(pSrt, froxelX7, froxelY2, rowHiZ4);


	// Row 3 (need htile 1 and 2)

	rowHiZ0 = max(htileHiZ01, htileHiZ02);
	rowHiZ1 = max(htileHiZ11, htileHiZ12);
	rowHiZ2 = max(htileHiZ21, htileHiZ22);
	rowHiZ3 = max(htileHiZ31, htileHiZ32);
	rowHiZ4 = max(htileHiZ41, htileHiZ42);


	StoreHiZHTile(pSrt, froxelX0, froxelY3, rowHiZ0);
	StoreHiZHTile(pSrt, froxelX1, froxelY3, max(rowHiZ0, rowHiZ1));
	StoreHiZHTile(pSrt, froxelX2, froxelY3, rowHiZ1);
	StoreHiZHTile(pSrt, froxelX3, froxelY3, max(rowHiZ1, rowHiZ2));
	StoreHiZHTile(pSrt, froxelX4, froxelY3, max(rowHiZ2, rowHiZ3));
	StoreHiZHTile(pSrt, froxelX5, froxelY3, rowHiZ3);
	StoreHiZHTile(pSrt, froxelX6, froxelY3, max(rowHiZ3, rowHiZ4));
	StoreHiZHTile(pSrt, froxelX7, froxelY3, rowHiZ4);

	// Row 4 (need htile 2 and 3)

	rowHiZ0 = max(htileHiZ02, htileHiZ03);
	rowHiZ1 = max(htileHiZ12, htileHiZ13);
	rowHiZ2 = max(htileHiZ22, htileHiZ23);
	rowHiZ3 = max(htileHiZ32, htileHiZ33);
	rowHiZ4 = max(htileHiZ42, htileHiZ43);


	StoreHiZHTile(pSrt, froxelX0, froxelY4, rowHiZ0);
	StoreHiZHTile(pSrt, froxelX1, froxelY4, max(rowHiZ0, rowHiZ1));
	StoreHiZHTile(pSrt, froxelX2, froxelY4, rowHiZ1);
	StoreHiZHTile(pSrt, froxelX3, froxelY4, max(rowHiZ1, rowHiZ2));
	StoreHiZHTile(pSrt, froxelX4, froxelY4, max(rowHiZ2, rowHiZ3));
	StoreHiZHTile(pSrt, froxelX5, froxelY4, rowHiZ3);
	StoreHiZHTile(pSrt, froxelX6, froxelY4, max(rowHiZ3, rowHiZ4));
	StoreHiZHTile(pSrt, froxelX7, froxelY4, rowHiZ4);

	// Row 5 (need htile 3 only)

	rowHiZ0 = htileHiZ03;
	rowHiZ1 = htileHiZ13;
	rowHiZ2 = htileHiZ23;
	rowHiZ3 = htileHiZ33;
	rowHiZ4 = htileHiZ43;


	StoreHiZHTile(pSrt, froxelX0, froxelY5, rowHiZ0);
	StoreHiZHTile(pSrt, froxelX1, froxelY5, max(rowHiZ0, rowHiZ1));
	StoreHiZHTile(pSrt, froxelX2, froxelY5, rowHiZ1);
	StoreHiZHTile(pSrt, froxelX3, froxelY5, max(rowHiZ1, rowHiZ2));
	StoreHiZHTile(pSrt, froxelX4, froxelY5, max(rowHiZ2, rowHiZ3));
	StoreHiZHTile(pSrt, froxelX5, froxelY5, rowHiZ3);
	StoreHiZHTile(pSrt, froxelX6, froxelY5, max(rowHiZ3, rowHiZ4));
	StoreHiZHTile(pSrt, froxelX7, froxelY5, rowHiZ4);

	// Row 6 (need htile 3 and 4)

	rowHiZ0 = max(htileHiZ03, htileHiZ04);
	rowHiZ1 = max(htileHiZ13, htileHiZ14);
	rowHiZ2 = max(htileHiZ23, htileHiZ24);
	rowHiZ3 = max(htileHiZ33, htileHiZ34);
	rowHiZ4 = max(htileHiZ43, htileHiZ44);


	StoreHiZHTile(pSrt, froxelX0, froxelY6, rowHiZ0);
	StoreHiZHTile(pSrt, froxelX1, froxelY6, max(rowHiZ0, rowHiZ1));
	StoreHiZHTile(pSrt, froxelX2, froxelY6, rowHiZ1);
	StoreHiZHTile(pSrt, froxelX3, froxelY6, max(rowHiZ1, rowHiZ2));
	StoreHiZHTile(pSrt, froxelX4, froxelY6, max(rowHiZ2, rowHiZ3));
	StoreHiZHTile(pSrt, froxelX5, froxelY6, rowHiZ3);
	StoreHiZHTile(pSrt, froxelX6, froxelY6, max(rowHiZ3, rowHiZ4));
	StoreHiZHTile(pSrt, froxelX7, froxelY6, rowHiZ4);

	// Row 7 (need htile 4 only)

	rowHiZ0 = htileHiZ04;
	rowHiZ1 = htileHiZ14;
	rowHiZ2 = htileHiZ24;
	rowHiZ3 = htileHiZ34;
	rowHiZ4 = htileHiZ44;


	StoreHiZHTile(pSrt, froxelX0, froxelY7, rowHiZ0);
	StoreHiZHTile(pSrt, froxelX1, froxelY7, max(rowHiZ0, rowHiZ1));
	StoreHiZHTile(pSrt, froxelX2, froxelY7, rowHiZ1);
	StoreHiZHTile(pSrt, froxelX3, froxelY7, max(rowHiZ1, rowHiZ2));
	StoreHiZHTile(pSrt, froxelX4, froxelY7, max(rowHiZ2, rowHiZ3));
	StoreHiZHTile(pSrt, froxelX5, froxelY7, rowHiZ3);
	StoreHiZHTile(pSrt, froxelX6, froxelY7, max(rowHiZ3, rowHiZ4));
	StoreHiZHTile(pSrt, froxelX7, froxelY7, rowHiZ4);
}



[NUM_THREADS(8, 8, 1)] //
void CS_AnalyzeHTileForFroxelsOneHTileOneFroxel(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	uint2 groupPos = uint2(8, 8) * groupId.xy;

	uint2 blockId = groupPos + groupThreadId.xy;

	uint tileX0 = blockId.x;
	uint tileY0 = blockId.y;

	uint froxelX = blockId.x;
	uint froxelY = blockId.y;

	// we are going to loookup max distances and combine them and store them per froxel

	uint htileOffset00 = HTileOffsetInDwords(tileX0, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);

	uint htile00 = pSrt->m_hTileRO[htileOffset00];
	

	float htileHiZ00 = HTileToHiZ(htile00);

	float froxelHiZ = htileHiZ00;

	float depthVal = froxelHiZ;

	float linearDepth = max(pSrt->m_fixedDepth, GetLinearDepth(depthVal, pSrt->m_depthParams));

	float froxelSliceFloat = CameraLinearDepthToFroxelZSliceExp(linearDepth, pSrt->m_fogGridOffset);
	float froxelZ = froxelSliceFloat;

	uint3 froxelCoord = uint3(froxelX, froxelY, froxelZ);

	pSrt->m_destVolumetricsScreenSpaceInfoOrig[froxelCoord.xy].x = froxelSliceFloat;
}

[NUM_THREADS(8, 8, 1)] //
void CS_AnalyzeHTileForFroxelsDoubleHTileOneFroxel(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	// 2x2 htiles correspond to single destination tile

	uint2 groupPos = uint2(8, 8) * groupId.xy;

	uint2 blockId = groupPos + groupThreadId.xy;

	uint tileX0 = blockId.x * 2;
	uint tileX1 = blockId.x * 2 + 1;
	
	uint tileY0 = blockId.y * 2;
	uint tileY1 = blockId.y * 2 + 1;
	

	uint froxelX = blockId.x;
	uint froxelY = blockId.y;

	// we are going to loookup max distances and combine them and store them per froxel

	uint htileOffset00 = HTileOffsetInDwords(tileX0, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset10 = HTileOffsetInDwords(tileX1, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	

	uint htileOffset01 = HTileOffsetInDwords(tileX0, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	uint htileOffset11 = HTileOffsetInDwords(tileX1, tileY1, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);
	
	
	uint htile00 = pSrt->m_hTileRO[htileOffset00];
	uint htile10 = pSrt->m_hTileRO[htileOffset10];
	
	uint htile01 = pSrt->m_hTileRO[htileOffset01];
	uint htile11 = pSrt->m_hTileRO[htileOffset11];
	
	
	float htileHiZ00 = HTileToHiZ(htile00);
	float htileHiZ10 = HTileToHiZ(htile10);
	
	float htileHiZ01 = HTileToHiZ(htile01);
	float htileHiZ11 = HTileToHiZ(htile11);
	
	

	float froxelHiZ = max(max(htileHiZ00, htileHiZ10), max(htileHiZ01, htileHiZ11));
	

	// now for reach one of the values, find the intersecting froxel


	
	float depthVal = froxelHiZ;


	float linearDepth = max(pSrt->m_fixedDepth, GetLinearDepth(depthVal, pSrt->m_depthParams));

	float froxelSliceFloat = CameraLinearDepthToFroxelZSliceExp(linearDepth, pSrt->m_fogGridOffset);
	float froxelZ = froxelSliceFloat;

	uint3 froxelCoord = uint3(froxelX, froxelY, froxelZ);

	//pSrt->m_destPropertiesFroxels[froxelCoord].z = 1;

	pSrt->m_destVolumetricsScreenSpaceInfoOrig[froxelCoord.xy].x = froxelSliceFloat;



}


[NUM_THREADS(8, 8, 1)] //
void CS_AnalyzeHTileForFroxels1HTilex2Froxels(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	// 1x1 htiles correspond to 2x2 destination tile

	uint2 groupPos = uint2(8, 8) * groupId.xy;

	uint2 blockId = groupPos + groupThreadId.xy;

	uint tileX0 = blockId.x;
	uint tileY0 = blockId.y;
	
	uint froxelX0 = blockId.x * 2;
	uint froxelY0 = blockId.y * 2;


	// we are going to loookup max distances and combine them and store them per froxel

	uint htileOffset00 = HTileOffsetInDwords(tileX0, tileY0, (SCREEN_NATIVE_RES_W_U + 7) / 8, false, /*isNeo=*/ g_isNeoMode);

	uint htile00 = pSrt->m_hTileRO[htileOffset00];


	float htileHiZ00 = HTileToHiZ(htile00);

	float froxelHiZ = htileHiZ00;

	float depthVal = froxelHiZ;

	float linearDepth = max(pSrt->m_fixedDepth, GetLinearDepth(depthVal, pSrt->m_depthParams));

	float froxelSliceFloat = CameraLinearDepthToFroxelZSliceExp(linearDepth, pSrt->m_fogGridOffset);
	float froxelZ = froxelSliceFloat;

	uint3 froxelCoord = uint3(froxelX0, froxelY0, froxelZ);

	pSrt->m_destVolumetricsScreenSpaceInfoOrig[froxelCoord.xy + uint2(0, 0)].x = froxelSliceFloat;
	pSrt->m_destVolumetricsScreenSpaceInfoOrig[froxelCoord.xy + uint2(1, 0)].x = froxelSliceFloat;

	pSrt->m_destVolumetricsScreenSpaceInfoOrig[froxelCoord.xy + uint2(0, 1)].x = froxelSliceFloat;
	pSrt->m_destVolumetricsScreenSpaceInfoOrig[froxelCoord.xy + uint2(1, 1)].x = froxelSliceFloat;
}



[NUM_THREADS(64, 1, 1)] //
void CS_AnalyzeDepthBuffer(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	// each WF will go through all depth samples

#define ANALYZE_USE_HALF_RES 1

#if ANALYZE_USE_HALF_RES
#define ANALYZE_COMBINE 1
#define ANALYZE_USE_HALF_RES_VARIANCE 1
#endif

	uint numSamples = pSrt->m_froxelSizeU * pSrt->m_froxelSizeU;

	#if ANALYZE_USE_HALF_RES
		numSamples = pSrt->m_froxelSizeU / 2 * pSrt->m_froxelSizeU / 2;

		uint2 groupOffset = uint2(0, 0); 
		#if ANALYZE_COMBINE
		groupOffset = uint2(groupThreadId.x >= 32 ? 1 : 0, 0);
		#endif
	#endif

	uint numIters = (numSamples + 63) / 64;
	#if ANALYZE_COMBINE
	numIters = (numSamples + 31) / 32;
	#endif

	
	float linDepthClamp = 100.0f; // should it be more like the probe range?
	float threadSum = 0;
	float varSum = 0;

	for (int i = 0; i < numIters; ++i)
	{
		int myIndex = i * 64 + groupThreadId.x;
		#if ANALYZE_COMBINE
		myIndex = i * 32 + (groupThreadId.x % 32);
		#endif
		if (myIndex < numSamples)
		{
			uint2 coord = (groupId.xy) * uint2(pSrt->m_froxelSizeU, pSrt->m_froxelSizeU);
			coord.x += myIndex % pSrt->m_froxelSizeU;
			coord.y += myIndex / pSrt->m_froxelSizeU;

			#if ANALYZE_USE_HALF_RES
				coord = (groupId.xy 
					#if ANALYZE_COMBINE
					* uint2(2, 1)
					#endif
					+ groupOffset) * uint2(pSrt->m_froxelSizeU / 2, pSrt->m_froxelSizeU / 2);
				coord.x += myIndex % (pSrt->m_froxelSizeU / 2);
				coord.y += myIndex / (pSrt->m_froxelSizeU / 2);
			#endif

			float ndcDepth = pSrt->m_depthTexture[coord];

			#if ANALYZE_USE_HALF_RES
				ndcDepth = pSrt->m_depthTextureHalfRes[coord];
			#endif

			float linDepth = GetLinearDepth(ndcDepth, pSrt->m_depthParams);

			linDepth = min(linDepth, linDepthClamp);

			#if ANALYZE_USE_HALF_RES_VARIANCE
				float2 primaryDepthVarianceHalfRes = pSrt->m_primaryDepthVarianceHalfRes[coord];
				threadSum += primaryDepthVarianceHalfRes.x;
				varSum += primaryDepthVarianceHalfRes.y;
			#else
				threadSum += linDepth;
				varSum += linDepth * linDepth;
			#endif
		}
	}

	// now combine all together

	

	float combinedSum = 0.0f;

	{
		float combinedInt_01 = (threadSum + LaneSwizzle(threadSum, 0x1f, 0x00, 0x01));
		float combinedInt_02 = (combinedInt_01 + LaneSwizzle(combinedInt_01, 0x1f, 0x00, 0x02));
		float combinedInt_04 = (combinedInt_02 + LaneSwizzle(combinedInt_02, 0x1f, 0x00, 0x04));
		float combinedInt_08 = (combinedInt_04 + LaneSwizzle(combinedInt_04, 0x1f, 0x00, 0x08));
		float combinedInt_10 = (combinedInt_08 + LaneSwizzle(combinedInt_08, 0x1f, 0x00, 0x10));

		#if ANALYZE_COMBINE
			combinedSum = combinedInt_10;
		#else
			float combinedInt = (ReadLane(combinedInt_10, 0x00) + ReadLane(combinedInt_10, 0x20));
			combinedSum = combinedInt;
		#endif

	}

	// all threads have accumulated value now

	float avgDepth = combinedSum / numSamples;


	/*
	// now calculate the variance
	for (int i = 0; i < numIters; ++i)
	{
		int myIndex = i * 64 + groupThreadId.x;
		if (myIndex < numSamples)
		{
			uint2 coord = groupId.xy * uint2(pSrt->m_froxelSizeU, pSrt->m_froxelSizeU);
			coord.x += myIndex % pSrt->m_froxelSizeU;
			coord.y += myIndex / pSrt->m_froxelSizeU;
			float ndcDepth = pSrt->m_depthTexture[coord];
			float linDepth = GetLinearDepth(ndcDepth, pSrt->m_depthParams);

			linDepth = min(linDepth, linDepthClamp);

			//float dif = avgDepth - linDepth;

			//dif *= dif;

			//varSum += dif;

			varSum += linDepth * linDepth;
		}
	}
	*/
	float combinedVarSum = 0.0f;

	{
		float combinedInt_01 = (varSum + LaneSwizzle(varSum, 0x1f, 0x00, 0x01));
		float combinedInt_02 = (combinedInt_01 + LaneSwizzle(combinedInt_01, 0x1f, 0x00, 0x02));
		float combinedInt_04 = (combinedInt_02 + LaneSwizzle(combinedInt_02, 0x1f, 0x00, 0x04));
		float combinedInt_08 = (combinedInt_04 + LaneSwizzle(combinedInt_04, 0x1f, 0x00, 0x08));
		float combinedInt_10 = (combinedInt_08 + LaneSwizzle(combinedInt_08, 0x1f, 0x00, 0x10));
		float combinedInt = (ReadLane(combinedInt_10, 0x00) + ReadLane(combinedInt_10, 0x20));


#if ANALYZE_COMBINE
		combinedVarSum = combinedInt_10;
#else
		combinedVarSum = combinedInt;
#endif

		
	}

	//float variance = sqrt(combinedVarSum / numSamples);
	float variance = (combinedVarSum / numSamples);


#if ANALYZE_COMBINE
	if ((groupThreadId.x % 32) == 0)
	{
		pSrt->m_destVolumetricsDepthInfo[uint3(groupId.xy * uint2(2, 1) + groupOffset, 0)].x = avgDepth;
		pSrt->m_destVolumetricsDepthInfo[uint3(groupId.xy * uint2(2, 1) + groupOffset, 0)].y = variance;
	}
#else
	pSrt->m_destVolumetricsDepthInfo[uint3(groupId.xy, 0)].x = avgDepth;
	pSrt->m_destVolumetricsDepthInfo[uint3(groupId.xy, 0)].y = variance;
#endif
}



[NUM_THREADS(8, 8, 1)] //
void CS_AnalyzeDepthBufferDownsample(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	const uint kTileSize = 1;
	uint2 coord = groupId.xy * uint2(kTileSize, kTileSize) + groupThreadId.xy;

	uint numSamples = kTileSize * kTileSize;

	float mean = pSrt->m_destVolumetricsDepthInfo[uint3(coord, 0)].x;
	float depthSqr = pSrt->m_destVolumetricsDepthInfo[uint3(coord, 0)].y;

	if (groupThreadId.x >= kTileSize || groupThreadId.y >= kTileSize)
	{
		mean = 0;
		depthSqr = 0;
	}

	
	//if (coord.x > 192 || coord.y > 108)
	//{
	//	mean = 0;
	//	depthSqr = 0;
	//}

	float threadSum = mean;

	float combinedSum = 0.0f;
	{
		float combinedInt_01 = (threadSum + LaneSwizzle(threadSum, 0x1f, 0x00, 0x01));
		float combinedInt_02 = (combinedInt_01 + LaneSwizzle(combinedInt_01, 0x1f, 0x00, 0x02));
		float combinedInt_04 = (combinedInt_02 + LaneSwizzle(combinedInt_02, 0x1f, 0x00, 0x04));
		float combinedInt_08 = (combinedInt_04 + LaneSwizzle(combinedInt_04, 0x1f, 0x00, 0x08));
		float combinedInt_10 = (combinedInt_08 + LaneSwizzle(combinedInt_08, 0x1f, 0x00, 0x10));
		float combinedInt = (ReadLane(combinedInt_10, 0x00) + ReadLane(combinedInt_10, 0x20));

		combinedSum = combinedInt;
	}

	float newMean = combinedSum / numSamples;


	threadSum = depthSqr;

	combinedSum = 0.0f;
	{
		float combinedInt_01 = (threadSum + LaneSwizzle(threadSum, 0x1f, 0x00, 0x01));
		float combinedInt_02 = (combinedInt_01 + LaneSwizzle(combinedInt_01, 0x1f, 0x00, 0x02));
		float combinedInt_04 = (combinedInt_02 + LaneSwizzle(combinedInt_02, 0x1f, 0x00, 0x04));
		float combinedInt_08 = (combinedInt_04 + LaneSwizzle(combinedInt_04, 0x1f, 0x00, 0x08));
		float combinedInt_10 = (combinedInt_08 + LaneSwizzle(combinedInt_08, 0x1f, 0x00, 0x10));
		float combinedInt = (ReadLane(combinedInt_10, 0x00) + ReadLane(combinedInt_10, 0x20));

		combinedSum = combinedInt;
	}

	float newDepthSqr = combinedSum / numSamples;

	pSrt->m_destVolumetricsDepthInfo[uint3(groupId.xy, 1)].x = newMean;
	pSrt->m_destVolumetricsDepthInfo[uint3(groupId.xy, 1)].y = newDepthSqr;
}


[NUM_THREADS(8, 8, 1)] //
void CS_AnalyzeDepthBufferDownsampleForProbes(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	const uint kTileSize = 8;
	uint2 coord = groupId.xy * uint2(kTileSize, kTileSize) + groupThreadId.xy;

	uint numSamples = kTileSize * kTileSize;

	uint valid = ((coord.x < pSrt->m_numFroxelsXY.x) && (coord.y < pSrt->m_numFroxelsXY.y)) ? 1 : 0;

	float mean = pSrt->m_destVolumetricsDepthInfo[uint3(coord, 0)].x;
	float depthSqr = pSrt->m_destVolumetricsDepthInfo[uint3(coord, 0)].y;

	if (!valid)
	{
		// actually should just happen by reading out of bounds

		mean = 0;
		depthSqr = 0;
	}

	ulong lane_mask = __v_cmp_eq_u32(valid, 1);
	int numValid = __s_bcnt1_i32_b64(lane_mask);

	//not needed for 8
	//if (groupThreadId.x >= kTileSize || groupThreadId.y >= kTileSize)
	//{
	//	mean = 0;
	//	depthSqr = 0;
	//}

	float threadSum = mean;

	float combinedSum = 0.0f;
	{
		float combinedInt_01 = (threadSum + LaneSwizzle(threadSum, 0x1f, 0x00, 0x01));
		float combinedInt_02 = (combinedInt_01 + LaneSwizzle(combinedInt_01, 0x1f, 0x00, 0x02));
		float combinedInt_04 = (combinedInt_02 + LaneSwizzle(combinedInt_02, 0x1f, 0x00, 0x04));
		float combinedInt_08 = (combinedInt_04 + LaneSwizzle(combinedInt_04, 0x1f, 0x00, 0x08));
		float combinedInt_10 = (combinedInt_08 + LaneSwizzle(combinedInt_08, 0x1f, 0x00, 0x10));
		float combinedInt = (ReadLane(combinedInt_10, 0x00) + ReadLane(combinedInt_10, 0x20));

		combinedSum = combinedInt;
	}

	float newMean = combinedSum / numValid;


	threadSum = depthSqr;

	combinedSum = 0.0f;
	{
		float combinedInt_01 = (threadSum + LaneSwizzle(threadSum, 0x1f, 0x00, 0x01));
		float combinedInt_02 = (combinedInt_01 + LaneSwizzle(combinedInt_01, 0x1f, 0x00, 0x02));
		float combinedInt_04 = (combinedInt_02 + LaneSwizzle(combinedInt_02, 0x1f, 0x00, 0x04));
		float combinedInt_08 = (combinedInt_04 + LaneSwizzle(combinedInt_04, 0x1f, 0x00, 0x08));
		float combinedInt_10 = (combinedInt_08 + LaneSwizzle(combinedInt_08, 0x1f, 0x00, 0x10));
		float combinedInt = (ReadLane(combinedInt_10, 0x00) + ReadLane(combinedInt_10, 0x20));

		combinedSum = combinedInt;
	}

	float newDepthSqr = combinedSum / numValid;

	pSrt->m_destVolumetricsDepthInfo[uint3(groupId.xy, 4)].x = newMean;
	pSrt->m_destVolumetricsDepthInfo[uint3(groupId.xy, 4)].y = newDepthSqr;
}


[NUM_THREADS(8, 8, 1)] //
void CS_AnalyzeDepthBufferBlurX(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	int2 coord = dispatchId.xy;

	float2 v0 = pSrt->m_destVolumetricsDepthInfo[uint3(coord, 0)];

	if (coord.y >= pSrt->m_numFroxelsXY.y)
	{
		pSrt->m_destVolumetricsDepthInfo[uint3(coord, 2)] = float2(0, 0);
		return;
	}

	int Width = pSrt->m_numFroxelsXY.x;

	float2 vm1 = pSrt->m_destVolumetricsDepthInfo[uint3(max(coord.x - 1, 0), coord.y, 0)];
	float2 vm2 = pSrt->m_destVolumetricsDepthInfo[uint3(max(coord.x - 2, 0), coord.y, 0)];
	float2 vp1 = pSrt->m_destVolumetricsDepthInfo[uint3(min(coord.x + 1, Width - 1), coord.y, 0)];
	float2 vp2 = pSrt->m_destVolumetricsDepthInfo[uint3(min(coord.x + 2, Width - 1), coord.y, 0)];

	float2 res3 = v0 * 0.44198 + vm1 * 0.27901 + vp1 * 0.27901;



	float2 res5 = v0 * 0.38774 + vm1 * 0.24477 + vp1 * 0.24477 + vm2 * 0.06136 + vp2 * 0.06136;

	//pSrt->m_destVolumetricsDepthInfo[uint3(coord, 2)] = res3;
	pSrt->m_destVolumetricsDepthInfo[uint3(coord, 2)] = res5;
}



[NUM_THREADS(8, 8, 1)] //
void CS_AnalyzeDepthBufferBlurY(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	int2 coord = dispatchId.xy;

	int Height = pSrt->m_numFroxelsXY.y;

	if (coord.x >= pSrt->m_numFroxelsXY.x)
	{
		pSrt->m_destVolumetricsDepthInfo[uint3(coord, 3)] = float2(0, 0);
		return;
	}

	float2 v0 = pSrt->m_destVolumetricsDepthInfo[uint3(coord, 2)];

	float2 vm1 = pSrt->m_destVolumetricsDepthInfo[uint3(coord.x, max(coord.y - 1, 0), 2)];
	float2 vm2 = pSrt->m_destVolumetricsDepthInfo[uint3(coord.x, max(coord.y - 2, 0), 2)];
	float2 vp1 = pSrt->m_destVolumetricsDepthInfo[uint3(coord.x, min(coord.y + 1, Height - 1), 2)];
	float2 vp2 = pSrt->m_destVolumetricsDepthInfo[uint3(coord.x, min(coord.y + 2, Height - 1), 2)];

	float2 res3 = v0 * 0.44198 + vm1 * 0.27901 + vp1 * 0.27901;

	float2 res5 = v0 * 0.38774 + vm1 * 0.24477 + vp1 * 0.24477 + vm2 * 0.06136 + vp2 * 0.06136;

	//pSrt->m_destVolumetricsDepthInfo[uint3(coord, 3)] = res3;

	pSrt->m_destVolumetricsDepthInfo[uint3(coord, 3)] = res5;
}


[NUM_THREADS(8, 8, 1)] //
void CS_AnalyzeDepthBufferBlurForProbesX(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	int2 coord = dispatchId.xy;

	int Width = (pSrt->m_numFroxelsXY.x + 7) / 8;
	float2 v0 = pSrt->m_destVolumetricsDepthInfo[uint3(min(coord.x, Width - 1), coord.y, 4)];

	if (coord.y >= (pSrt->m_numFroxelsXY.y + 7) / 8)
	{
		pSrt->m_destVolumetricsDepthInfo[uint3(coord, 5)] = float2(0, 0);
		return;
	}

	float2 vm1 = pSrt->m_destVolumetricsDepthInfo[uint3(max(coord.x - 1, 0), coord.y, 4)];
	float2 vm2 = pSrt->m_destVolumetricsDepthInfo[uint3(max(coord.x - 2, 0), coord.y, 4)];
	float2 vp1 = pSrt->m_destVolumetricsDepthInfo[uint3(min(coord.x + 1, Width - 1), coord.y, 4)];
	float2 vp2 = pSrt->m_destVolumetricsDepthInfo[uint3(min(coord.x + 2, Width - 1), coord.y, 4)];
	


	float2 res3 = v0 * 0.44198 + vm1 * 0.27901 + vp1 * 0.27901;



	float2 res5 = v0 * 0.38774 + vm1 * 0.24477 + vp1 * 0.24477 + vm2 * 0.06136 + vp2 * 0.06136;

	//pSrt->m_destVolumetricsDepthInfo[uint3(coord, 2)] = res3;
	pSrt->m_destVolumetricsDepthInfo[uint3(coord, 5)] = res5;
}



[NUM_THREADS(8, 8, 1)] //
void CS_AnalyzeDepthBufferBlurForProbesY(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	VolumetricsFogSrt *pSrt : S_SRT_DATA)
{
	int2 coord = dispatchId.xy;

	int Height = (pSrt->m_numFroxelsXY.y + 7) / 8;

	if (coord.x >= (pSrt->m_numFroxelsXY.x + 7) / 8)
	{
		pSrt->m_destVolumetricsDepthInfo[uint3(coord, 6)] = float2(0, 0);
		return;
	}

	float2 v0 = pSrt->m_destVolumetricsDepthInfo[uint3(coord.x, min(coord.y, Height - 1), 5)];

	
	float2 vm1 = pSrt->m_destVolumetricsDepthInfo[uint3(coord.x, max(coord.y - 1, 0), 5)];
	float2 vm2 = pSrt->m_destVolumetricsDepthInfo[uint3(coord.x, max(coord.y - 2, 0), 5)];
	float2 vp1 = pSrt->m_destVolumetricsDepthInfo[uint3(coord.x, min(coord.y + 1, Height - 1), 5)];
	float2 vp2 = pSrt->m_destVolumetricsDepthInfo[uint3(coord.x, min(coord.y + 2, Height - 1), 5)];


	float2 res3 = v0 * 0.44198 + vm1 * 0.27901 + vp1 * 0.27901;

	float2 res5 = v0 * 0.38774 + vm1 * 0.24477 + vp1 * 0.24477 + vm2 * 0.06136 + vp2 * 0.06136;

	//pSrt->m_destVolumetricsDepthInfo[uint3(coord, 3)] = res3;

	pSrt->m_destVolumetricsDepthInfo[uint3(coord, 6)] = res5;
}

