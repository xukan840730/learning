//
// Compute Shader - Quad Tree instancing block and sampling
//

#ifndef DISABLE_WAVE_DEFORMATION
	//#define DISABLE_WAVE_DEFORMATION
#endif

#include "global-funcs.fxi"
#include "new-water-funcs.fxi"

#define kMaxThreadX 8
#define kMaxThreadY 8
#define kMaxThreadZ 1

#define kNumThreads (kMaxThreadX * kMaxThreadY * kMaxThreadZ)

#define kGridSizeX 17
#define kGridSizeY 17

#define kNumGrids (kGridSizeX * kGridSizeY)

#ifndef DISABLE_WAVE_DEFORMATION
	#define APPLY_WAVE_DEFORMATION
#endif

// All of the wave-particles will be 32x32
//#pragma warning (disable:7203)

//#pragma warning (default:7203)
//uint3 dispatchThreadId : SV_DispatchThreadID

void GetBoxCoordinates(uint3 groupIndex, uint3 groupThread, out int boxId, out int gridIdX, out int gridIdZ )
{
	uint threadIdx = groupIndex.x * kNumThreads + groupThread.y * kMaxThreadX + groupThread.x;
	uint gridIdx = threadIdx % kNumGrids;

	boxId = threadIdx / kNumGrids;
	gridIdX = gridIdx % kGridSizeX;
	gridIdZ = gridIdx / kGridSizeX;
}

// This appears to be unused
[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_QuadTreeEvaluate_New( uint3 groupIndex : SV_GroupID,  uint3 groupThread : SV_GroupThreadId,  SrtData srt : S_SRT_DATA)
{
	int boxId, gridIdX, gridIdZ;
	GetBoxCoordinates(groupIndex, groupThread, boxId, gridIdX, gridIdZ);

	QuadBlockConstantInfo *consts = srt.m_data->m_consts;
	TexturesAndSamplers *textures = srt.m_data->m_texturesAndSamplers;
	SamplerState     textureSampler = srt.m_data->m_texturesAndSamplers->g_linearClampSampler;

	int	sideBlock = consts->m_sideBlock;
	int	numBlocks = consts->m_numBlocks;
	int numVerticesPerBlock = consts->m_numVerticesPerBlock;

	// one thread per block
	if (boxId >= numBlocks) 
	 	return;

	int index      = boxId * numVerticesPerBlock; 
	int instanceId = boxId;  

	QuadBlockInstance *blocks = srt.m_data->m_blocks;	
	float4 instanceInfo     = blocks->m_info[instanceId].m_pos;
	float4 instanceLerpInfo = blocks->m_info[instanceId].m_lerp;

	float startLerpValue = instanceLerpInfo.y;
	float endLerpValue   = instanceLerpInfo.z;
	float2 xzOffset      = instanceInfo.xy + float2(.00001, .00001);
	float size           = instanceInfo.z;
	float fside          = consts->m_sideBlock - 1;
	float sizefside      = size / fside;

	float3 levelColors[11] = {
		{1,1,1},
		{1,0,0},
		{0,1,0},
		{0,0,1},
		{0,1,1},
		{1,0,1},
		{1,1,0},
		{0,1,1},
		{1,1,0},
		{1,.5,0},
		{.5,.5,.5}};

	// Using one thread per grid element
	float zi  = gridIdZ;  
	float xi  = gridIdX;  
	index += (gridIdZ * consts->m_sideBlock) + gridIdX;
	 
	// get the position offset by the box corner and scale by it's size
	float2 xz = xzOffset + float2(xi, zi) * sizefside;

	float d = length(consts->m_center.xyz - float3(xz.x, consts->m_offset, xz.y));
	float lerpValue = saturate((startLerpValue - d) / (startLerpValue - endLerpValue));

	if ((gridIdZ & 0x01) && lerpValue > .0f) {
		xz.y = xz.y - lerpValue * sizefside;
	}
	if ((gridIdX & 0x01) && lerpValue > .0f) {
		xz.x = xz.x - lerpValue * sizefside;	
	}

	WaveDisplacement waveOut;
	float4 freqMask = 1.0;
	GridStructure grids;
	LoadGrid(grids, srt.m_data->m_texturesAndSamplers, consts->m_octave);

	float2 uv;
	GetTextureCoords(xz.x, xz.y, consts, textures, textureSampler, uv.x, uv.y);

	TextureData txData;
	GetTextureData(xz, uv, 0, consts, textures, textureSampler, txData);

	GetDisplacement(xz.x, xz.y, uv.x, uv.y, txData, consts, 
					srt.m_data->m_texturesAndSamplers,
					srt.m_data->m_texturesAndSamplers->g_linearClampSampler,
					grids,
					float2(1,0),
					true, // use for rendering
					waveOut);

	srt.m_data->m_output[index].m_position.xyz = waveOut.m_position;
	srt.m_data->m_output[index].m_normal.xyz   = waveOut.m_normal;
	srt.m_data->m_output[index].m_color0       = float4(waveOut.m_flow.xy, saturate(waveOut.m_foam), saturate(waveOut.m_subsurface));
	srt.m_data->m_output[index].m_positionPrev.xyz  = waveOut.m_position;
	//srt.m_data->m_output[index].m_motionVector  = float4(waveOut.m_textureUV.xy, waveOut.m_height, 0);

	// debug
	srt.m_data->m_output[index].m_color1       = float4(levelColors[(int) instanceLerpInfo.w], lerpValue);
}



[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_QuadTreeBase_New( uint3 groupIndex : SV_GroupID,  uint3 groupThread : SV_GroupThreadId,  SrtData srt : S_SRT_DATA)
{
	int boxId, gridIdX, gridIdZ;
	GetBoxCoordinates(groupIndex, groupThread, boxId, gridIdX, gridIdZ);

	QuadBlockConstantInfo *consts = srt.m_data->m_consts;
	TexturesAndSamplers *textures = srt.m_data->m_texturesAndSamplers;
	SamplerState     textureSampler = srt.m_data->m_texturesAndSamplers->g_linearClampSampler;

	int	sideBlock = consts->m_sideBlock;
	int	numBlocks = consts->m_numBlocks;
	int numVerticesPerBlock = consts->m_numVerticesPerBlock;

	// one thread per block
	if (boxId >= numBlocks) 
	 	return;

	int index      = boxId * numVerticesPerBlock; 
	index += (gridIdZ * consts->m_sideBlock) + gridIdX;
	int instanceId = boxId;  

	QuadBlockInstance *blocks = srt.m_data->m_blocks;	
	float4 instanceInfo     = blocks->m_info[instanceId].m_pos;
	float4 instanceLerpInfo = blocks->m_info[instanceId].m_lerp;

	float startLerpValue = instanceLerpInfo.y;
	float endLerpValue   = instanceLerpInfo.z;
	float2 xzOffset      = instanceInfo.xy + float2(.00001, .00001);
	float size           = instanceInfo.z;
	float fside          = consts->m_sideBlock - 1;
	float sizefside      = size / fside;

	// Using one thread per grid element
	float zi  = gridIdZ;  
	float xi  = gridIdX;  

 
	// get the position offset by the box corner and scale by it's size
	float2 xz = xzOffset + float2(xi, zi) * sizefside;

	float d3 = length(consts->m_center.xyz - float3(xz.x, consts->m_offset, xz.y));
	float d2 = length(consts->m_center.xz - float2(xz.x, xz.y));
	float d  = (consts->m_use2dLod) ? d2 : d3;

	float lerpValue = saturate((startLerpValue - d) / (startLerpValue - endLerpValue));

	if ((gridIdZ & 0x01) && lerpValue > .0f) {
		xz.y = xz.y - lerpValue * sizefside;
	}
	if ((gridIdX & 0x01) && lerpValue > .0f) {
		xz.x = xz.x - lerpValue * sizefside;	
	}

	// Get texture coordinates if needed
	float2 uv = float2(.5,.5);
	GetTextureCoords(xz.x, xz.y, consts, textures, textureSampler, uv.x, uv.y);

	TextureData txData;
	GetTextureData(xz, uv, 0, consts, textures, textureSampler, txData);

	srt.m_data->m_output[index].m_basePosition = float4(xz.xy, uv.xy);
	srt.m_data->m_output[index].m_position     = float3(xz.x, consts->m_offset, xz.y);
#ifdef APPLY_WAVE_DEFORMATION
	srt.m_data->m_output[index].m_color0       = float4(txData.m_flow.xy, 0, 0);
	srt.m_data->m_output[index].m_freqMask     = txData.m_freqMask;
	srt.m_data->m_output[index].m_flow         = txData.m_flow;
#endif
	srt.m_data->m_output[index].m_height       = txData.m_height;
	srt.m_data->m_output[index].m_heightNormal = txData.m_heightNormal;
}

[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_QuadTreePass1_New( uint3 groupIndex : SV_GroupID,  uint3 groupThread : SV_GroupThreadId,  SrtData srt : S_SRT_DATA)
{
	int boxId, gridIdX, gridIdZ;
	GetBoxCoordinates(groupIndex, groupThread, boxId, gridIdX, gridIdZ);

	QuadBlockConstantInfo *consts = srt.m_data->m_consts;

	int	numBlocks = consts->m_numBlocks;
	int numVerticesPerBlock = consts->m_numVerticesPerBlock;

	// one thread per block
	if (boxId >= numBlocks) 
	 	return;

	int index      = boxId * numVerticesPerBlock; 
	index += (gridIdZ * consts->m_sideBlock) + gridIdX;

	int instanceId = boxId;  

	QuadBlockInstance *blocks = srt.m_data->m_blocks;	
	float4 instanceInfo     = blocks->m_info[instanceId].m_pos;
	float4 instanceLerpInfo = blocks->m_info[instanceId].m_lerp;

	float4 xzuv     = srt.m_data->m_output[index].m_basePosition;
	float4 freqMask = srt.m_data->m_output[index].m_color1;
	float2 xz = xzuv.xy;
	float2 uv = xzuv.zw;
	float lerpValue = 0.0;

	TextureData txData;
	txData.m_height   =	srt.m_data->m_output[index].m_height;
	txData.m_heightNormal = srt.m_data->m_output[index].m_heightNormal;

#ifdef APPLY_WAVE_DEFORMATION
	txData.m_freqMask =	srt.m_data->m_output[index].m_freqMask;
	txData.m_flow     =	srt.m_data->m_output[index].m_flow;

	WaveDisplacement waveOut;

	GridStructure grids;
	LoadGrid(grids, srt.m_data->m_texturesAndSamplers, consts->m_octave);

	GetDisplacement(xz.x, xz.y, uv.x, uv.y, txData, consts, 
					srt.m_data->m_texturesAndSamplers,
					srt.m_data->m_texturesAndSamplers->g_linearClampSampler,
					grids,
					float2(1,0),
					true, // use for rendering
					waveOut);

	srt.m_data->m_output[index].m_position.xyz = waveOut.m_position;
	srt.m_data->m_output[index].m_normal.xyz   = normalize(waveOut.m_normal);
	srt.m_data->m_output[index].m_color0       = float4(waveOut.m_flow.xy, saturate(waveOut.m_foam), saturate(waveOut.m_subsurface));
	srt.m_data->m_output[index].m_positionPrev.xyz = waveOut.m_position;

	srt.m_data->m_output[index].m_color1.x = waveOut.m_strain;
	srt.m_data->m_output[index].m_color1.y = waveOut.m_lerp;
#else
	float3 position;
	{
		float posY = consts->m_offset + consts->m_terrainMag * txData.m_height;
		position = float3(xz.x, posY, xz.y);
	}

	srt.m_data->m_output[index].m_position.xyz = position;
	srt.m_data->m_output[index].m_normal.xyz   = normalize(txData.m_heightNormal).xzy;
	srt.m_data->m_output[index].m_color0       = 0;
	srt.m_data->m_output[index].m_positionPrev.xyz = position;
	srt.m_data->m_output[index].m_color1.xy = 0;

#endif
}


// If active, it does flow NOT blending
[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_QuadTreePass2_New( uint3 groupIndex : SV_GroupID,  uint3 groupThread : SV_GroupThreadId,  SrtData srt : S_SRT_DATA)
{
	int boxId, gridIdX, gridIdZ;
	GetBoxCoordinates(groupIndex, groupThread, boxId, gridIdX, gridIdZ);

	QuadBlockConstantInfo *consts = srt.m_data->m_consts;

	int	numBlocks = consts->m_numBlocks;
	int numVerticesPerBlock = consts->m_numVerticesPerBlock;

	// one thread per block
	if (boxId >= numBlocks) 
	 	return;

	int index      = boxId * numVerticesPerBlock; 
	index += (gridIdZ * consts->m_sideBlock) + gridIdX;

	int instanceId = boxId;  

	QuadBlockInstance *blocks = srt.m_data->m_blocks;	
	float4 instanceInfo     = blocks->m_info[instanceId].m_pos;
	float4 instanceLerpInfo = blocks->m_info[instanceId].m_lerp;

	float2 xz = srt.m_data->m_output[index].m_basePosition.xy;
	float2 uv = srt.m_data->m_output[index].m_basePosition.zw;
	float  lerpValue = 0.0;

	TextureData txData;
	txData.m_height   =	srt.m_data->m_output[index].m_height;
	txData.m_heightNormal = srt.m_data->m_output[index].m_heightNormal;
#ifdef APPLY_WAVE_DEFORMATION
	txData.m_freqMask =	srt.m_data->m_output[index].m_freqMask;
	txData.m_flow     =	srt.m_data->m_output[index].m_flow;

	
	WaveDisplacement waveOut;

	GridStructure grids;
	LoadGrid(grids, srt.m_data->m_texturesAndSamplers, consts->m_octave);

	GetDisplacement(xz.x, xz.y, uv.x, uv.y, txData, consts, 
					srt.m_data->m_texturesAndSamplers,
					srt.m_data->m_texturesAndSamplers->g_linearClampSampler,
					grids,
					float2(0,1),
					true, // use for rendering
					waveOut);

	float2 blend;
	blend.y = waveOut.m_blend.x;
	blend.x = 1.0 - blend.y;

	srt.m_data->m_output[index].m_position.xyz = blend.x * srt.m_data->m_output[index].m_position.xyz + blend.y * waveOut.m_position;
	srt.m_data->m_output[index].m_normal.xyz   = normalize(blend.x * srt.m_data->m_output[index].m_normal.xyz + blend.y * waveOut.m_normal);
	srt.m_data->m_output[index].m_color0       = float4(waveOut.m_flow.xy, saturate(waveOut.m_foam), saturate(waveOut.m_subsurface));
	srt.m_data->m_output[index].m_color1.x = blend.x * srt.m_data->m_output[index].m_color1.x + blend.y * waveOut.m_strain;
#else
	float3 position;
	{
		float posY = consts->m_offset + consts->m_terrainMag * txData.m_height;
		position = float3(xz.x, posY, xz.y);
	}

	srt.m_data->m_output[index].m_position.xyz = position;
	srt.m_data->m_output[index].m_normal.xyz   = normalize(txData.m_heightNormal).xzy;
	srt.m_data->m_output[index].m_color0       = 0;
	srt.m_data->m_output[index].m_color1.xy    = 0;
#endif

}

[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_QuadTreePass3_New( uint3 groupIndex : SV_GroupID,  uint3 groupThread : SV_GroupThreadId,  SrtData srt : S_SRT_DATA)
{
	int boxId, gridIdX, gridIdZ;
	GetBoxCoordinates(groupIndex, groupThread, boxId, gridIdX, gridIdZ);

	QuadBlockConstantInfo *consts = srt.m_data->m_consts;

	int	numBlocks = consts->m_numBlocks;
	int numVerticesPerBlock = consts->m_numVerticesPerBlock;

	// one thread per block
	if (boxId >= numBlocks) 
	 	return;

	int index      = boxId * numVerticesPerBlock; 
	index += (gridIdZ * consts->m_sideBlock) + gridIdX;

	float2 xz = srt.m_data->m_output[index].m_basePosition.xy;
	float2 uv = srt.m_data->m_output[index].m_basePosition.zw;

	// It's is guaranteed that we should use ripples
	//	if (consts->m_useFlags & kUseRipples)
	{
		// ripples 
		TexturesAndSamplers   *textures = srt.m_data->m_texturesAndSamplers;
		SamplerState	       textureSampler = srt.m_data->m_texturesAndSamplers->g_linearClampSampler;
		 
		float2 vRipplePos = (xz.xy - consts->m_ripples.xz) / consts->m_ripples.w;
		float3 ripples = textures->m_ripplesTx.SampleLevel(textureSampler, vRipplePos.xy, 0).rgb;
		vRipplePos = (2.* vRipplePos) - float2(1,1);
		float rippleDist2 = dot(vRipplePos, vRipplePos); // add a nice extra padding and make it steeper
		float ripplesMask = saturate((1.0 - rippleDist2));
		float rippleHeight = ripplesMask *  (.1 * ripples.z + 8.0 * saturate((ripples.x - .5) + (ripples.y - .5)));
		srt.m_data->m_output[index].m_position.y += consts->m_rippleDisplacementScale * rippleHeight;
		srt.m_data->m_output[index].m_position.xz += ripplesMask * (consts->m_ripplePinchScale * (ripples.xy - .5));
		srt.m_data->m_output[index].m_normal.xz += .2 * ripplesMask * (ripples.xy);
		srt.m_data->m_output[index].m_color0.z += consts->m_rippleFoamRate * rippleHeight;
		srt.m_data->m_output[index].m_normal.xyz = normalize(srt.m_data->m_output[index].m_normal.xyz);
	}
}


// Only computes previous position. Used for motionvector
[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_QuadTreeMotionVectorPass1_New( uint3 groupIndex : SV_GroupID,  uint3 groupThread : SV_GroupThreadId,  SrtData srt : S_SRT_DATA)
{
	int boxId, gridIdX, gridIdZ;
	GetBoxCoordinates(groupIndex, groupThread, boxId, gridIdX, gridIdZ);

	QuadBlockConstantInfo *constsOrig = srt.m_data->m_consts;
	QuadBlockConstantInfo *consts = srt.m_data->m_constsPrev;

	int	numBlocks = constsOrig->m_numBlocks;
	int numVerticesPerBlock = consts->m_numVerticesPerBlock;

	// one thread per block
	if (boxId >= numBlocks) 
	 	return;

	int index      = boxId * numVerticesPerBlock; 
	index += (gridIdZ * consts->m_sideBlock) + gridIdX;

	int instanceId = boxId;  

	QuadBlockInstance *blocks = srt.m_data->m_blocks;	
	float4 instanceInfo     = blocks->m_info[instanceId].m_pos;
	float4 instanceLerpInfo = blocks->m_info[instanceId].m_lerp;

	float2 xz = srt.m_data->m_output[index].m_basePosition.xy;
	float2 uv = srt.m_data->m_output[index].m_basePosition.zw;

#ifdef APPLY_WAVE_DEFORMATION
	WaveDisplacement waveOut;

	TextureData txData;
	txData.m_freqMask =	srt.m_data->m_output[index].m_freqMask;
	txData.m_flow     =	srt.m_data->m_output[index].m_flow;
	txData.m_height   =	srt.m_data->m_output[index].m_height;
	txData.m_heightNormal = srt.m_data->m_output[index].m_heightNormal;


	
	GridStructure grids;
	LoadGridPrev(grids, srt.m_data->m_texturesAndSamplers, consts->m_octave);

	GetDisplacement(xz.x, xz.y, uv.x, uv.y, txData, consts, 
					srt.m_data->m_texturesAndSamplers,
					srt.m_data->m_texturesAndSamplers->g_linearClampSampler,
					grids,
					float2(1,0),
					true, // use for rendering
					waveOut);
	
	srt.m_data->m_output[index].m_positionPrev.xyz  = (consts->m_cameraTeleportedThisFrame)?
		srt.m_data->m_output[index].m_position.xyz : 
		waveOut.m_position;
#else
	srt.m_data->m_output[index].m_positionPrev.xyz = srt.m_data->m_output[index].m_position.xyz;
#endif
}

[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_QuadTreeMotionVectorPass2_New( uint3 groupIndex : SV_GroupID,  uint3 groupThread : SV_GroupThreadId,  SrtData srt : S_SRT_DATA)
{
	int boxId, gridIdX, gridIdZ;
	GetBoxCoordinates(groupIndex, groupThread, boxId, gridIdX, gridIdZ);

	QuadBlockConstantInfo *constsOrig = srt.m_data->m_consts;
	QuadBlockConstantInfo *consts = srt.m_data->m_constsPrev;

	int	numBlocks = constsOrig->m_numBlocks;
	int numVerticesPerBlock = consts->m_numVerticesPerBlock;

	// one thread per block
	if (boxId >= numBlocks) 
	 	return;

	int index      = boxId * numVerticesPerBlock; 
	index += (gridIdZ * consts->m_sideBlock) + gridIdX;

	int instanceId = boxId;  

	QuadBlockInstance *blocks = srt.m_data->m_blocks;	
	float4 instanceInfo     = blocks->m_info[instanceId].m_pos;
	float4 instanceLerpInfo = blocks->m_info[instanceId].m_lerp;

	float2 xz = srt.m_data->m_output[index].m_basePosition.xy;
	float2 uv = srt.m_data->m_output[index].m_basePosition.zw;
#ifdef APPLY_WAVE_DEFORMATION
	WaveDisplacement waveOut;

	TextureData txData;
	txData.m_freqMask =	srt.m_data->m_output[index].m_freqMask;
	txData.m_flow     =	srt.m_data->m_output[index].m_flow;
	txData.m_height   =	srt.m_data->m_output[index].m_height;
	txData.m_heightNormal = srt.m_data->m_output[index].m_heightNormal;

	
	GridStructure grids;
	LoadGridPrev(grids, srt.m_data->m_texturesAndSamplers, consts->m_octave);

	GetDisplacement(xz.x, xz.y, uv.x, uv.y, txData, consts, 
					srt.m_data->m_texturesAndSamplers,
					srt.m_data->m_texturesAndSamplers->g_linearClampSampler,
					grids,
					float2(0, 1),
					true, // use for rendering
					waveOut);

	float2 blend;
	blend.y = waveOut.m_blend.x;
	blend.x = 1.0 - blend.y;

	srt.m_data->m_output[index].m_positionPrev.xyz  =  (consts->m_cameraTeleportedThisFrame) ?
		srt.m_data->m_output[index].m_position.xyz :
		(blend.x * srt.m_data->m_output[index].m_positionPrev.xyz + blend.y * waveOut.m_position);
#else
	srt.m_data->m_output[index].m_positionPrev.xyz  = srt.m_data->m_output[index].m_position.xyz;
#endif

}

[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_QuadTreeMotionVectorPass3_New( uint3 groupIndex : SV_GroupID,  uint3 groupThread : SV_GroupThreadId,  SrtData srt : S_SRT_DATA)
{
	int boxId, gridIdX, gridIdZ;
	GetBoxCoordinates(groupIndex, groupThread, boxId, gridIdX, gridIdZ);

	QuadBlockConstantInfo *constsOrig = srt.m_data->m_consts;
	QuadBlockConstantInfo *consts = srt.m_data->m_constsPrev;

	int	numBlocks = constsOrig->m_numBlocks;
	int numVerticesPerBlock = consts->m_numVerticesPerBlock;

	// one thread per block
	if (boxId >= numBlocks) 
	 	return;

	int index      = boxId * numVerticesPerBlock; 
	index += (gridIdZ * consts->m_sideBlock) + gridIdX;

	float2 xz = srt.m_data->m_output[index].m_basePosition.xy;
	float2 uv = srt.m_data->m_output[index].m_basePosition.zw;

	if (consts->m_useFlags & kUseRipples)
	{
		// ripples 
		TexturesAndSamplers   *textures = srt.m_data->m_texturesAndSamplers;
		SamplerState	       textureSampler = srt.m_data->m_texturesAndSamplers->g_linearClampSampler;
		 
		float2 vRipplePos = (xz.xy - consts->m_ripples.xz) / consts->m_ripples.w;
		float3 ripples = textures->m_ripplesTx.SampleLevel(textureSampler, vRipplePos.xy, 0).rgb;
		vRipplePos = (2.* vRipplePos) - float2(1,1);
		float rippleDist2 = dot(vRipplePos, vRipplePos); // add a nice extra padding and make it steeper
		float ripplesMask = saturate((1.0 - rippleDist2));
		float rippleHeight = ripplesMask *  (.1 * ripples.z + 8.0 * saturate((ripples.x - .5) + (ripples.y - .5)));
		srt.m_data->m_output[index].m_positionPrev.y += consts->m_rippleDisplacementScale * rippleHeight;
		srt.m_data->m_output[index].m_positionPrev.xz += ripplesMask * (consts->m_ripplePinchScale * (ripples.xy - .5));
	}
}



[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_QuadTreeBaseHeightmap_New( uint3 groupIndex : SV_GroupID,  uint3 groupThread : SV_GroupThreadId,  SrtData srt : S_SRT_DATA)
{
	int boxId, gridIdX, gridIdZ;
	GetBoxCoordinates(groupIndex, groupThread, boxId, gridIdX, gridIdZ);

	QuadBlockConstantInfo *consts = srt.m_data->m_consts;
	TexturesAndSamplers *textures = srt.m_data->m_texturesAndSamplers;
	SamplerState     textureSampler = srt.m_data->m_texturesAndSamplers->g_linearClampSampler;

	int	sideBlock = consts->m_sideBlock;
	int	numBlocks = consts->m_numBlocks;
	int numVerticesPerBlock = consts->m_numVerticesPerBlock;

	// one thread per block
	if (boxId >= numBlocks) 
	 	return;

	int index      = boxId * numVerticesPerBlock; 
	index += (gridIdZ * consts->m_sideBlock) + gridIdX;
	int instanceId = boxId;  

	QuadBlockInstance *blocks = srt.m_data->m_blocks;	
	float4 instanceInfo     = blocks->m_info[instanceId].m_pos;
	float2 xzOffset      = instanceInfo.xy + float2(.00001, .00001);
	float  size           = instanceInfo.z;
	float  fside          = consts->m_sideBlock - 1;
	float  sizefside      = size / fside;

	// Using one thread per grid element
	float zi  = gridIdZ;  
	float xi  = gridIdX;  

 	// get the position offset by the box corner and scale by it's size
	float2 xz = xzOffset + float2(xi, zi) * sizefside;

	// no lerping

	// Get texture coordinates if needed
	float2 uv = float2(.5,.5);
	GetTextureCoords(xz.x, xz.y, consts, textures, textureSampler, uv.x, uv.y);

	TextureData txData;
	GetTextureData(xz, uv, 0, consts, textures, textureSampler, txData);

	float3 position = float3(xz.x, consts->m_offset + consts->m_disableWavesOffset + consts->m_terrainMag * txData.m_height, xz.y);

	srt.m_data->m_output[index].m_basePosition = float4(xz.xy, uv.xy);
	srt.m_data->m_output[index].m_position     = position;
	srt.m_data->m_output[index].m_positionPrev.xyz = position;
	srt.m_data->m_output[index].m_normal       = float3(0,1,0);
#ifdef APPLY_WAVE_DEFORMATION
	srt.m_data->m_output[index].m_color0       = float4(txData.m_flow.xy, 0, 0);
	srt.m_data->m_output[index].m_freqMask     = txData.m_freqMask;
	srt.m_data->m_output[index].m_flow         = txData.m_flow;
#else
	srt.m_data->m_output[index].m_color0       = 0;
#endif
}

[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_QuadTreeDebug_New( uint3 groupIndex : SV_GroupID,  uint3 groupThread : SV_GroupThreadId,  SrtData srt : S_SRT_DATA)
{
	int boxId, gridIdX, gridIdZ;
	GetBoxCoordinates(groupIndex, groupThread, boxId, gridIdX, gridIdZ);

	QuadBlockConstantInfo *consts = srt.m_data->m_consts;
	TexturesAndSamplers *textures = srt.m_data->m_texturesAndSamplers;
	SamplerState     textureSampler = srt.m_data->m_texturesAndSamplers->g_linearClampSampler;

	int	numBlocks = consts->m_numBlocks;
	int numVerticesPerBlock = consts->m_numVerticesPerBlock;

	// one thread per block
	if (boxId >= numBlocks) 
	 	return;

	int index      = boxId * numVerticesPerBlock; 
	index += (gridIdZ * consts->m_sideBlock) + gridIdX;

	int instanceId = boxId;  

	QuadBlockInstance *blocks = srt.m_data->m_blocks;	
	float4 instanceInfo     = blocks->m_info[instanceId].m_pos;
	float4 instanceLerpInfo = blocks->m_info[instanceId].m_lerp;

	float3 levelColors[11] = {
		{1,1,1},
		{1,0,0},
		{0,1,0},
		{.5,.5,.5},
		{0,0,1},
		{0,1,1},
		{1,0,1},
		{1,1,0},
		{0,1,1},
		{1,1,0},
		{1,.5,0}};

	float2 xz = srt.m_data->m_output[index].m_basePosition.xy;
	float2 uv = srt.m_data->m_output[index].m_basePosition.zw;	
	float4 freqMask = srt.m_data->m_output[index].m_color1;
	float  lerpValue = 0.0;

	TextureData txData;
	GetTextureData(xz, uv, 0, consts, textures, textureSampler, txData);

	
	WaveDisplacement waveOut;

	GridStructure grids;
	LoadGrid(grids, srt.m_data->m_texturesAndSamplers, consts->m_octave);

	GetDisplacement(xz.x, xz.y, uv.x, uv.y, txData, consts, 
					srt.m_data->m_texturesAndSamplers,
					srt.m_data->m_texturesAndSamplers->g_linearClampSampler,
					grids,
					float2(1,0),
					true, // use for rendering
					waveOut);

	float2 blend;
	blend.x = waveOut.m_blend.x;
	blend.y = 1.0 - blend.x;

#ifdef DISABLE_WAVE_DEFORMATION
	float2 out_flow 	= 0;
	float out_strain 	= 0;
	float out_lerp 		= 0;
	float out_amplitude = 0;
	float3 out_debug 	= float3(1,0,0);
#else
	float2 out_flow 	= waveOut.m_flow.xy;
	float out_strain 	= srt.m_data->m_output[index].m_color1.x;
	float out_lerp 		= waveOut.m_lerp;
	float out_amplitude = waveOut.m_amplitude;
	float3 out_debug 	= waveOut.m_debug.xyz;
#endif

	if (consts->m_showFlags & kShowHeight) {
		srt.m_data->m_output[index].m_color1.x   = txData.m_height;
	}
	if (consts->m_showFlags & kShowHeightNormal) {
		srt.m_data->m_output[index].m_color1.xyz   = txData.m_heightNormal;
	}
	if (consts->m_showFlags & kShowFlow) {
		srt.m_data->m_output[index].m_color1.xy  = out_flow;
	}
	if (consts->m_showFlags & kShowAlpha) {
		srt.m_data->m_output[index].m_color1.xyz  = waveOut.m_alpha;
	}
	if (consts->m_showFlags & kShowStrain) {
		srt.m_data->m_output[index].m_color1.xyz  = out_strain.xxx;
	}
	if (consts->m_showFlags & kShowDepthMasks) {
	 	srt.m_data->m_output[index].m_color1.xyz = float3(.1,.1,.4);  // make all vertices flat with color
	}
	if (consts->m_showFlags & kShowLerpDistance) {
		srt.m_data->m_output[index].m_color1.xyz = out_lerp;
	}
	if (consts->m_showFlags & kShowAmplitude) {
		srt.m_data->m_output[index].m_color1.xyz =out_amplitude;
	}
	if (consts->m_showFlags & kShowCheckerboard) {
		//srt.m_data->m_output[index].m_color1.xyz = out_debug;
		srt.m_data->m_output[index].m_color1.xyz = levelColors[(int)instanceLerpInfo.w];
	}
}
