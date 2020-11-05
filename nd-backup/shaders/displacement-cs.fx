//
// Compute Shader - Quad Tree instancing block and sampling
//

#include "global-funcs.fxi"
#include "water-funcs.fxi"

#define kMaxThreadX 300
#define kMaxThreadY 1
#define kMaxThreadZ 1

#define kMaxQueryPoints  300 

// Same as in water-mgr.h
struct DisplacementInfo
{
	int    m_numSamples;
	int    m_pad1;
	int    m_pad2;
	int    m_pad3;
	// The size of the arrays are the max for each category
	float4 m_points[kMaxQueryPoints]; // x,z (point), x,y,z  W= mask!!!!
	float4 m_dstPoints[kMaxQueryPoints];
};

// Should be the same as DisplacementResult in water-mgr.h
struct DisplacementResult
{
	float4 m_position;
	float4 m_normal;
	float2 m_flow;
	float2 m_velocity;
	float  m_strain;
	uint   m_status;
	uint   m_hasResult;
	float  m_wpY;
};


struct DisplacementPartialData
{
	float2 m_basePosition;   // x,z point and u,v
	float2 m_uv;
	float4 m_freqMask;

	float2 m_flow;
	float  m_height;
	float  m_amplitude;
};

struct DisplacementSrtData
{
	DisplacementInfo                     *    m_dispConsts;
	QuadBlockConstantInfo                *    m_waveConsts;
	RWStructuredBuffer<DisplacementResult>		m_result;
	RWStructuredBuffer<DisplacementPartialData> m_partialData;
	TexturesAndSamplers                  *    m_texturesAndSamplers;
};

struct DisplacementSrt
{
	DisplacementSrtData * m_data;
};

void GetPartialData(DisplacementPartialData partial, out float2 pt, out float2 uv, out TextureData txData)
{
	pt                     = partial.m_basePosition;
	uv                     = partial.m_uv;
	txData.m_freqMask      = partial.m_freqMask; 
	txData.m_height        = partial.m_height; 
	txData.m_flow.xy       = partial.m_flow;
	txData.m_flow.z        = partial.m_amplitude;
	txData.m_heightNormal = 0;
}

float GetWaterPropertiesOffset(float2 xz, QuadBlockConstantInfo * waveConsts, TexturesAndSamplers *textures, SamplerState textureSampler)
{

	float2 uvOffset = waveConsts->m_waterPropertiesScaleOffset.xy;
	float  uvScale  = waveConsts->m_waterPropertiesScaleOffset.z;
	float2 uv = 1.0 - (((xz - (uvOffset - uvScale)) / (2.0 * uvScale) ));
	float y = textures->m_displacementTx.SampleLevel(textureSampler, uv, 0).y;
	return (waveConsts->m_useWaterProperties && (uv.x >= 0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0)) ? ((y - waveConsts->m_waterPropertiesOffsetMinimum) * waveConsts->m_waterPropertiesDisplacementOffsetScale) : 0;
}

//
// BASE
[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_DisplacementClear( uint3 groupThread : SV_GroupThreadId,  DisplacementSrt srt : S_SRT_DATA)
{
	int index = groupThread.x;   
	DisplacementInfo      *dispConsts = srt.m_data->m_dispConsts;
	float4 disp = 0;

	if (index >= kMaxQueryPoints)
		return;

	srt.m_data->m_result[index].m_position = float4(0, 1000, 0, 0);
	srt.m_data->m_result[index].m_normal = 0;
	srt.m_data->m_result[index].m_flow = 0;
	srt.m_data->m_result[index].m_velocity = 0;
	srt.m_data->m_result[index].m_strain = 0;
	srt.m_data->m_result[index].m_status = kNotActive; // by default all queries are not active unless they succed
	srt.m_data->m_result[index].m_hasResult = 0;
	srt.m_data->m_result[index].m_wpY = 0;
}

//
// BASE
[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_DisplacementBase( uint3 groupThread : SV_GroupThreadId,  DisplacementSrt srt : S_SRT_DATA)
{
	int index = groupThread.x;   
	DisplacementInfo      *dispConsts = srt.m_data->m_dispConsts;
	float4 disp = 0;
	QuadBlockConstantInfo *waveConsts = srt.m_data->m_waveConsts;
	TexturesAndSamplers *textures = srt.m_data->m_texturesAndSamplers;
	SamplerState     textureSampler = srt.m_data->m_texturesAndSamplers->g_linearClampSampler;

	if (index >= dispConsts->m_numSamples)
		return;

	[branch] 
	if (waveConsts->m_failQueries) {
	 	// Fail the query due to bad data (e.g. bad textures)
	 	srt.m_data->m_result[index].m_status = kFailed | kBadData;
	 	return;
	}


	// If we have already a valid result, skip computations
	[branch]	
	if (srt.m_data->m_result[index].m_hasResult & kHasResult) {
		return;
	}


	if (srt.m_data->m_result[index].m_status == (kCulledOut | kFailed)) {
		// Reset the status if previous state was culled out
		// This means the Cs job is being executed again against another tree
		srt.m_data->m_result[index].m_status = kNotActive;
	}

	float4 referencePt = dispConsts->m_points[index];
	float4 qt = float4(referencePt.xyz, 1.0);
	
	float2 uv;
	GetTextureCoords(qt.x, qt.z, waveConsts, textures, textureSampler, uv.x, uv.y);

	TextureData txData;
	GetTextureData(referencePt.xz, uv, 1, waveConsts, textures, textureSampler, txData);

	[branch]
	if (txData.m_alpha < 0.1 || (waveConsts->m_hasBbox && (qt.x < waveConsts->m_bboxMin.x  || qt.x > waveConsts->m_bboxMax.x ||
														   qt.z < waveConsts->m_bboxMin.z  || qt.z > waveConsts->m_bboxMax.z))) 
	{
		srt.m_data->m_result[index].m_status = kCulledOut | kFailed;
		return;
	}
	int numIn =0;
	int numOut=0;
	if (waveConsts->m_numClippingPlanes > 0) {
		for (int i=0; i < waveConsts->m_numClippingPlanes; i++) {
			if (dot(qt, waveConsts->m_clippingPlane[i]) < 0) {
				numIn++;
			} else {
				numOut++;
			}
		}
		if ((!waveConsts->m_invertClippingPlanes && numOut == 0) || (waveConsts->m_invertClippingPlanes && numIn > 0))  {
			srt.m_data->m_result[index].m_status = kCulledOut | kFailed;
		 	return;
		}
	}

	int imask = asuint(referencePt.w);
	txData.m_freqMask = float4((imask & 0x08) ? txData.m_freqMask.x : 0, (imask & 0x04) ? txData.m_freqMask.y : 0, (imask & 0x02) ? txData.m_freqMask.z : 0, (imask & 0x01) ? txData.m_freqMask.w : 0);

	// Store partial results from texture data
	DisplacementPartialData partial;

	srt.m_data->m_result[index].m_position.xyz = referencePt.xyz;
	srt.m_data->m_result[index].m_position.w = txData.m_flow.z * saturate(dot(1, txData.m_freqMask));
	srt.m_data->m_result[index].m_normal.w = txData.m_alpha;
	
	partial.m_basePosition  = referencePt.xz;  
	partial.m_uv            = uv;
	partial.m_freqMask      = txData.m_freqMask;
	partial.m_height        = txData.m_height;
#ifdef DISABLE_WAVE_DEFORMATION
	partial.m_flow          = 0;
	partial.m_amplitude     = 0;
#else
	partial.m_flow          = txData.m_flow.xy;
	partial.m_amplitude     = txData.m_flow.z;
#endif
	srt.m_data->m_partialData[index] = partial;

	// srt.m_data->m_result[index].m_position.xyz = float3(referencePt.x, waveConsts->m_offset, referencePt.z);
	// srt.m_data->m_result[index].m_hasResult = kHasResult;
	// srt.m_data->m_result[index].m_status = kOk;
	
	// Note we ignore from textures:
	// heightnormal   (does anybody uses it??)
	// the alpha at this point is not needed, we already culled out
}



[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_PointDisplacementPass1( uint3 groupThread : SV_GroupThreadId,  DisplacementSrt srt : S_SRT_DATA)
{
	int index = groupThread.x;   
	DisplacementInfo      *dispConsts = srt.m_data->m_dispConsts;


	if (index >= dispConsts->m_numSamples)
		return;

	// Early out
	// do we already have a result
	[branch]
	if ((srt.m_data->m_result[index].m_hasResult & kHasResult) || (srt.m_data->m_result[index].m_status & (kCulledOut | kFailed))) {
		return;
	}

	float4 disp = 0;
	QuadBlockConstantInfo *waveConsts = srt.m_data->m_waveConsts;
	TexturesAndSamplers *textures = srt.m_data->m_texturesAndSamplers;
	SamplerState     textureSampler = srt.m_data->m_texturesAndSamplers->g_linearClampSampler;

	float4 referencePt = dispConsts->m_points[index];
	
	// Get precomputed data
	TextureData txData;
	float2 pt;
	float2 uv;
	GetPartialData(srt.m_data->m_partialData[index], pt, uv, txData);
	
	GridStructure grids;
	LoadGrid(grids, srt.m_data->m_texturesAndSamplers, waveConsts->m_octave);

	WaveDisplacement waveOut;
	GetDisplacement(pt.x, pt.y, uv.x, uv.y, txData, waveConsts, 
					srt.m_data->m_texturesAndSamplers,
					srt.m_data->m_texturesAndSamplers->g_linearClampSampler,
					grids,
					float2(1,0),
					false, // do NOT use for rendering
					waveOut);


	// add the water properties buffer
	float wpY = GetWaterPropertiesOffset(referencePt.xz, waveConsts, textures, textureSampler);
	waveOut.m_position.y += wpY;

	srt.m_data->m_result[index].m_position.xyz = float3(waveOut.m_position);
	srt.m_data->m_result[index].m_normal.xyz   = waveOut.m_normal.xyz;
	srt.m_data->m_result[index].m_flow     = 2.0 * (waveOut.m_flow - .5);
	srt.m_data->m_result[index].m_velocity = waveOut.m_velocity.xz;
	srt.m_data->m_result[index].m_strain   = waveOut.m_strain;
	srt.m_data->m_result[index].m_wpY      = wpY;
	srt.m_data->m_result[index].m_status   = kOk; 
	srt.m_data->m_result[index].m_hasResult  = ((waveConsts->m_useFlags & kUseFlow) || waveConsts->m_blendValue > 0) ? kHasPartialResult : kHasResult;
	// the result depends if we are blending or doing flow (i.e. we need to do another pass thru the samples)
}

[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_PointDisplacementPass2( uint3 groupThread : SV_GroupThreadId,  DisplacementSrt srt : S_SRT_DATA)
{
	int index = groupThread.x;   
	DisplacementInfo      *dispConsts = srt.m_data->m_dispConsts;

	if (index >= dispConsts->m_numSamples)
		return;

	// Early out
	[branch]
	if ((srt.m_data->m_result[index].m_hasResult & kHasResult) || (srt.m_data->m_result[index].m_status & (kCulledOut | kFailed))) {
		return;
	}
	
	float4 disp = 0;
	QuadBlockConstantInfo *waveConsts = srt.m_data->m_waveConsts;
	TexturesAndSamplers *textures = srt.m_data->m_texturesAndSamplers;
	SamplerState     textureSampler = srt.m_data->m_texturesAndSamplers->g_linearClampSampler;
	
	float4 referencePt = dispConsts->m_points[index];
	
	// Get precomputed data
	TextureData txData;
	float2 pt;
	float2 uv;
	GetPartialData(srt.m_data->m_partialData[index], pt, uv, txData);
	
	GridStructure grids;
	LoadGrid(grids, srt.m_data->m_texturesAndSamplers, waveConsts->m_octave);

	WaveDisplacement waveOut;
	GetDisplacement(pt.x, pt.y, uv.x, uv.y, txData, waveConsts, 
					srt.m_data->m_texturesAndSamplers,
					srt.m_data->m_texturesAndSamplers->g_linearClampSampler,
					grids,
					float2(0,1),
					false, // do NOT use for rendering
					waveOut);

	float2 blend;
	blend.y = waveOut.m_blend.x;
	blend.x = 1.0 - blend.y;

	// add the water properties buffer
	float wpY = GetWaterPropertiesOffset(referencePt.xz, waveConsts, textures, textureSampler);
	waveOut.m_position.y += wpY;

	
	// Result
	srt.m_data->m_result[index].m_position.xyz = blend.x * srt.m_data->m_result[index].m_position.xyz + blend.y * waveOut.m_position;
	srt.m_data->m_result[index].m_normal.xyz   = normalize(blend.x * srt.m_data->m_result[index].m_normal.xyz   + blend.y * waveOut.m_normal.xyz);
	srt.m_data->m_result[index].m_flow         = blend.x * srt.m_data->m_result[index].m_flow         + blend.y * 2.0 * (waveOut.m_flow - .5);
	srt.m_data->m_result[index].m_velocity     = blend.x * srt.m_data->m_result[index].m_velocity     + blend.y * waveOut.m_velocity.xz;
	srt.m_data->m_result[index].m_strain       = blend.x * srt.m_data->m_result[index].m_strain       + blend.y * waveOut.m_strain;
	srt.m_data->m_result[index].m_wpY          = blend.x * srt.m_data->m_result[index].m_wpY          + blend.y * wpY;
	srt.m_data->m_result[index].m_hasResult    = kHasResult;
	srt.m_data->m_result[index].m_status   = kOk;
}




// Vertical Displacement
[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_VerticalDisplacementPass1( uint3 groupThread : SV_GroupThreadId,  DisplacementSrt srt : S_SRT_DATA)
{
	int index = groupThread.x;   
	DisplacementInfo      *dispConsts = srt.m_data->m_dispConsts;
	float4 disp = 0;

	if (index >= dispConsts->m_numSamples)
		return;

	// Early out
	[branch]
	if ((srt.m_data->m_result[index].m_hasResult & kHasResult) || (srt.m_data->m_result[index].m_status & (kCulledOut | kFailed))) {
		return;
	}

	QuadBlockConstantInfo *waveConsts = srt.m_data->m_waveConsts;
	TexturesAndSamplers *textures = srt.m_data->m_texturesAndSamplers;
	SamplerState     textureSampler = srt.m_data->m_texturesAndSamplers->g_linearClampSampler;

	float4 referencePt = dispConsts->m_points[index];
	
	// Get precomputed data
	TextureData txData;
	float2 pt;
	float2 uv;
	GetPartialData(srt.m_data->m_partialData[index], pt, uv, txData);
	
	GridStructure grids;
	LoadGrid(grids, srt.m_data->m_texturesAndSamplers, waveConsts->m_octave);


	// Very arbitrary max number of iterations
	int numIter = 8;
	int iter = 0;

	WaveDisplacement waveOut;
	waveOut.m_status  = kOk;
	waveOut.m_blend   = float2(1,0);
	waveOut.m_strain  = 0;
	while (iter < numIter) {

		GetTextureCoords(pt.x, pt.y, waveConsts, textures, textureSampler, uv.x, uv.y);

		GetDisplacement(pt.x, pt.y, uv.x, uv.y, txData, waveConsts, 
						srt.m_data->m_texturesAndSamplers,
						srt.m_data->m_texturesAndSamplers->g_linearClampSampler,
						grids,
						float2(1,0),
						false, // do NOT use for rendering
						waveOut);

		// compare from original distance
		float2 diff = waveOut.m_position.xz - referencePt.xz; 
		float d = length(diff);
		if (d < .001)
			break;
		pt.xy = pt.xy - diff;
		iter++;
	}

	// add the water properties buffer
	float wpY = GetWaterPropertiesOffset(referencePt.xz, waveConsts, textures, textureSampler);
	waveOut.m_position.y += wpY;
	
	srt.m_data->m_result[index].m_position.xyz = float3(referencePt.x, waveOut.m_position.y, referencePt.z);
	srt.m_data->m_result[index].m_normal.xyz = float3(0,1,0);
	srt.m_data->m_result[index].m_flow     = 2.0 * (waveOut.m_flow - .5);
	srt.m_data->m_result[index].m_velocity = waveOut.m_velocity.xz;
	srt.m_data->m_result[index].m_strain   = waveOut.m_strain;
	srt.m_data->m_result[index].m_wpY      = wpY;
	srt.m_data->m_result[index].m_status   = kOk; 
	srt.m_data->m_result[index].m_hasResult  = ((waveConsts->m_useFlags & kUseFlow) || waveConsts->m_blendValue > 0) ? kHasPartialResult : kHasResult;
	// the result depends if we are blending or doing flow (i.e. we need to do another pass thru the samples)

}

[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_VerticalDisplacementPass2( uint3 groupThread : SV_GroupThreadId,  DisplacementSrt srt : S_SRT_DATA)
{
	int index = groupThread.x;   
	DisplacementInfo      *dispConsts = srt.m_data->m_dispConsts;
	float4 disp = 0;

	if (index >= dispConsts->m_numSamples)
		return;

	// Early out

	[branch]
	if ((srt.m_data->m_result[index].m_hasResult & kHasResult) || (srt.m_data->m_result[index].m_status & (kCulledOut | kFailed))) {
		return;
	}

	QuadBlockConstantInfo *waveConsts = srt.m_data->m_waveConsts;
	TexturesAndSamplers *textures     = srt.m_data->m_texturesAndSamplers;
	SamplerState     textureSampler   = srt.m_data->m_texturesAndSamplers->g_linearClampSampler;

	float4 referencePt = dispConsts->m_points[index];
	
	// Get precomputed data
	TextureData txData;
	float2 pt;
	float2 uv;
	GetPartialData(srt.m_data->m_partialData[index], pt, uv, txData);
	
	GridStructure grids;
	LoadGrid(grids, srt.m_data->m_texturesAndSamplers, waveConsts->m_octave);

	// Very arbitrary max number of iterations
	int numIter = 8;
	int iter = 0;

	WaveDisplacement waveOut;
	waveOut.m_status = kOk;
	waveOut.m_blend  = float2(0, 1);
	waveOut.m_strain = 0;
	while (iter < numIter) {

		GetTextureCoords(pt.x, pt.y, waveConsts, textures, textureSampler, uv.x, uv.y);

		GetDisplacement(pt.x, pt.y, uv.x, uv.y, txData, waveConsts, 
						srt.m_data->m_texturesAndSamplers,
						srt.m_data->m_texturesAndSamplers->g_linearClampSampler,
						grids,
						float2(0,1),
						false, // do NOT use for rendering
						waveOut);

		// compare from original distance
		float2 diff = waveOut.m_position.xz - referencePt.xz; 
		float d = length(diff);
		if (d < .001)
			break;
		pt.xy = pt.xy - diff;
		iter++;
	}

	float2 blend;
	blend.y = waveOut.m_blend.x;
	blend.x = 1.0 - blend.y;

	srt.m_data->m_result[index].m_normal = 0;

	// add the water properties buffer
	float wpY = GetWaterPropertiesOffset(referencePt.xz, waveConsts, textures, textureSampler);
	waveOut.m_position.y += wpY;

	float3 position = float3(referencePt.x, waveOut.m_position.y, referencePt.z);

	// Result
	srt.m_data->m_result[index].m_position.xyz = blend.x * srt.m_data->m_result[index].m_position.xyz + blend.y * position;
	srt.m_data->m_result[index].m_normal.xyz   = normalize(blend.x * srt.m_data->m_result[index].m_normal.xyz   + blend.y * waveOut.m_normal.xyz);
	srt.m_data->m_result[index].m_flow         = blend.x * srt.m_data->m_result[index].m_flow         + blend.y * 2.0 * (waveOut.m_flow - .5);
	srt.m_data->m_result[index].m_velocity     = blend.x * srt.m_data->m_result[index].m_velocity     + blend.y * waveOut.m_velocity.xz;
	srt.m_data->m_result[index].m_strain       = blend.x * srt.m_data->m_result[index].m_strain       + blend.y * waveOut.m_strain;
	srt.m_data->m_result[index].m_wpY          = blend.x * srt.m_data->m_result[index].m_wpY          + blend.y * wpY;
	srt.m_data->m_result[index].m_status       = kOk;
	srt.m_data->m_result[index].m_hasResult    = kHasResult;

}


void RayDisplacement( uint index,  DisplacementSrt srt, bool doPass2)
{
	DisplacementInfo      *dispConsts = srt.m_data->m_dispConsts;
	float4 disp = 0;

	if (index >= dispConsts->m_numSamples)
		return;

	if (srt.m_data->m_result[index].m_hasResult & kHasResult) {
		return;
	}
	
	QuadBlockConstantInfo *waveConsts = srt.m_data->m_waveConsts;
	TexturesAndSamplers   *textures   = srt.m_data->m_texturesAndSamplers;
	SamplerState     textureSampler   = srt.m_data->m_texturesAndSamplers->g_linearClampSampler;

	float3 srcPt = dispConsts->m_points[index].xyz;
	float3 dstPt = dispConsts->m_dstPoints[index].xyz;
	
	float3 dir = (dstPt - srcPt);
	float3 dirn = normalize(dir);

	// Get precomputed data
	TextureData txData;
	float2 pt;
	float2 uv;
	//	GetPartialData(srt.m_data->m_partialData[index], pt, uv, txData);

	GridStructure grids1;
	GridStructure grids2;
	LoadGrid(grids1, srt.m_data->m_texturesAndSamplers, waveConsts->m_octave);
	LoadGrid(grids2, srt.m_data->m_texturesAndSamplers, waveConsts->m_octave);

	// Very arbitrary max number of iterations
	int numIter = 8;
	int iter = 0;

	WaveDisplacement waveOut1;
	waveOut1.m_status = kOk;
	waveOut1.m_blend  = float2(0, 1);
	waveOut1.m_strain = 0;
	WaveDisplacement waveOut2;
	waveOut2.m_status = kOk;
	waveOut2.m_blend  = float2(0, 1);
	waveOut2.m_strain = 0;	

	float2 blend = float2(1,0);

	int	  done		 = 0;
	bool  underWater = false;
	bool  firstState = false;
	float param		 = 0;
	int	  count		 = 0;
	float rayTolerance = .01;
	float ptTolerance = .001;

	bool  startBisection  = false;
	int   numIterations = 0;
	int   numMaxIterations = 13;
	float rayStep	 = 1.0 / ((float) numMaxIterations);

	uint result = kCulledOut | kFailed;

	// rayStep = 1.0;

	float3 finalPt = 0;
	int maxCount = 10;
	param = 0;
	float wpY=0;
	while (!done && param >= 0 && param <=1 && numIterations < numMaxIterations)
	{
		float3 referencePt = srcPt + param * dir;
		pt = referencePt.xz;

		iter = 0;
		while (iter < numIter) {
			// set uv coordinates for each iteration
			GetTextureCoords(pt.x, pt.y, waveConsts, textures, textureSampler, uv.x, uv.y);

			GetTextureData(pt.xy, uv, 1, waveConsts, textures, textureSampler, txData);
			
			GetDisplacement(pt.x, pt.y, uv.x, uv.y, txData, waveConsts, 
							srt.m_data->m_texturesAndSamplers,
							srt.m_data->m_texturesAndSamplers->g_linearClampSampler,
							grids1,
							float2(1,0),
							false, // do NOT use for rendering
							waveOut1);

			if (doPass2) {
				GetDisplacement(pt.x, pt.y, uv.x, uv.y, txData, waveConsts, 
								srt.m_data->m_texturesAndSamplers,
								srt.m_data->m_texturesAndSamplers->g_linearClampSampler,
								grids2,
								float2(0,1),
								false, // do NOT use for rendering
								waveOut2);


				blend.y = waveOut2.m_blend.x;
				blend.x = 1.0 - blend.y;

				finalPt = blend.x * waveOut1.m_position + blend.y * waveOut2.m_position;

			} else {
				finalPt = waveOut1.m_position;
			}

			// compare from original distance
			float2 diff = finalPt.xz - referencePt.xz; 
			float d = length(diff);
			if (d < ptTolerance) {
				result = kOk;
				break;
			}
			pt.xy = pt.xy - diff;
			iter++;
		}
		// project under line
		finalPt.xz = referencePt.xz;

		// add the water properties buffer
		wpY = GetWaterPropertiesOffset(referencePt.xz, waveConsts, textures, textureSampler);
		finalPt.y += wpY;
		
		underWater = referencePt.y <= finalPt.y;

		// If this is the first event, record what is our original state
		// If we started above water, then we need to know when we go under water
		// If we started under water, then we need to know when we go above water
		firstState = (numIterations == 0)? underWater : firstState; 

		float3 diff = finalPt - referencePt;
		// If we within some distance of the surface, then stop
		if (length(diff) < rayTolerance) {
			break;
		}
	
		// We start the bisection as soon as we find the first change of state event
		// We limit the amount of bisections
		startBisection = (firstState != underWater)? true : startBisection;

		// if we are underwater then we divide the step
		rayStep = (startBisection)? rayStep / 2.0 : rayStep;

		// With bisection should we go forwards of backwards
		param += (firstState != underWater)? -rayStep : rayStep;

		numIterations++;
	}
	
	// Final pass, check that point is inside the area
	GetTextureCoords(finalPt.x, finalPt.z, waveConsts, textures, textureSampler, uv.x, uv.y);
	GetTextureData(finalPt.xz, uv, 1, waveConsts, textures, textureSampler, txData);

	
	[branch]
	if (txData.m_alpha < 0.1 || (waveConsts->m_hasBbox && (finalPt.x < waveConsts->m_bboxMin.x  || finalPt.x > waveConsts->m_bboxMax.x ||
														   finalPt.z < waveConsts->m_bboxMin.z  || finalPt.z > waveConsts->m_bboxMax.z))) 
	{
		srt.m_data->m_result[index].m_status = kCulledOut | kFailed;
		return;
	}


	int numIn =0;
	int numOut=0;
	if (waveConsts->m_numClippingPlanes > 0) {
		float4 cpt = float4(finalPt.xyz, 1.0);
		for (int i=0; i < waveConsts->m_numClippingPlanes; i++) {
			if (dot(cpt, waveConsts->m_clippingPlane[i]) < 0) {
				numIn++;
			} else {
				numOut++;
			}
		}
		if ((!waveConsts->m_invertClippingPlanes && numOut == 0) || (waveConsts->m_invertClippingPlanes && numIn > 0))  {
		 	srt.m_data->m_result[index].m_status = kCulledOut | kFailed;
		 	return;
		}
	}

	// Result
	srt.m_data->m_result[index].m_position.xyz = finalPt;
	srt.m_data->m_result[index].m_position.w   = txData.m_flow.z * saturate(dot(1, txData.m_freqMask));
	srt.m_data->m_result[index].m_wpY          = wpY;
	srt.m_data->m_result[index].m_hasResult    =
		(!doPass2 && ((waveConsts->m_useFlags & kUseFlow) || waveConsts->m_blendValue > 0)) ? kHasPartialResult : kHasResult;
	srt.m_data->m_result[index].m_status       = kOk;
}


[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_RayDisplacementPass1( uint3 groupThread : SV_GroupThreadId,  DisplacementSrt srt : S_SRT_DATA)
{
	int index = groupThread.x;   
	RayDisplacement( index,  srt, false);

}

[numthreads(kMaxThreadX, kMaxThreadY, kMaxThreadZ)]
void Cs_RayDisplacementPass2( uint3 groupThread : SV_GroupThreadId,  DisplacementSrt srt : S_SRT_DATA)
{
	int index = groupThread.x;   
	RayDisplacement( index,  srt, true);
}

