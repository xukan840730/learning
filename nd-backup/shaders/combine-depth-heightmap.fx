//--------------------------------------------------------------------------------------
// File: combine-depth-heightmap-cs.fx
//
// Copyright (c) Naughty Dog Inc. All rights reserved.
//--------------------------------------------------------------------------------------

#include "global-funcs.fxi"
#include "ShaderFastMathLib.h"
#include "ssct.fxi"
#include "packing.fxi"

static const uint THREAD_GROUP_SIZE_XY = 8;

struct CombineDepthWithHeightmapSrt
{
	RWTexture2D<float>		rwt_depthWithHeightmap;
	Texture2D<float>		tex_depth;
	Texture2D<uint4>		tex_gBuffer1;
	float4					screenToViewParams;
	float					farDist;
};

float CombineDepthWithHeightmap(in CombineDepthWithHeightmapSrt *pSrt, in int2 screenCoords)
{
	// load depth
	float depthBufferZ = pSrt->tex_depth[screenCoords].x;

	// linearize depth
	float linearDepth = GetLinearDepth(depthBufferZ, pSrt->screenToViewParams);

	// load height-map depth delta
	bool heightmapEnabled;
	float heightmapDepthDelta;
	uint4 sample1 = pSrt->tex_gBuffer1[screenCoords];
	DecodeHeightmapDepthDelta(sample1, heightmapDepthDelta, heightmapEnabled);
	
	// combine linear depth with depth delta
	linearDepth -= heightmapDepthDelta;
	
	// We skip SSAO/ SSS for pixels that are on the far clip plane (i.e. for sky). To be able to skip SSAO/ SSS for pixels
	// that don't have the MASK_BIT_SPECIAL_HEIGHTMAP_ENABLE set, we use the negative far clip distance, so we can still
	// differentiate them from the sky pixels.
	if (!heightmapEnabled)
		linearDepth = -pSrt->farDist; // explicit specification instead of using GetLinearDepth() to avoid precision issues

	return linearDepth;
}

[NUM_THREADS(THREAD_GROUP_SIZE_XY, THREAD_GROUP_SIZE_XY, 1)]
void CS_CombineDepthWithHeightmap(uint2 dispatchThreadId : S_DISPATCH_THREAD_ID, CombineDepthWithHeightmapSrt *pSrt : S_SRT_DATA)
{
	float result = CombineDepthWithHeightmap(pSrt, (int2)dispatchThreadId.xy);
	pSrt->rwt_depthWithHeightmap[dispatchThreadId.xy] = result;
}
