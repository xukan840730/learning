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
#include "quat.fxi"

#define ND_PSSL

#include "particle-cs.fxi"

#include "particle-ray-trace-cs-defines.fxi"


#define kCellSize 256

//adding some global variables here

#define kFps (45.0f) // set the fps
#define kFlipframes (16.0f) // how many frames in your flipbook
#define kVariance (.05f) // how much time in seconds to vary the start frame per flipbook loop
#define kSpriteScale (1.0f) // global scale on particles
#define kOffsety (0.5f) // you can push sprites up or down along their Y axis here
#define kReverseOffset (-1.0f)

#define kSplashMaxRoughness (0.7) // particles wont spawn above this roughness value

#define kSplashRoughVal (0.3)
#define kSplashMediumVal (0.1)

#define kMotionStateFreeFallAttach 0
#define kMotionStateAttached 1
#define kMotionStateDeadFreeFall 2
#define kMotionStateFreeFallCollide 3

#define kRenderStateDefault 0 // ribbons only
#define kRenderStateInvisible 1 // ribbons only
#define kRenderStateBlue 2 // ribbons only

#define kRenderStateUnderWater 3
#define kRenderStateAboveWater 4


// render state has 7 bits
uint GetRenderState(ParticleStateV0 state)
{
	uint renderState = (state.m_flags1 & 0x000F0000) >> 16;

	return renderState;
}

// render state has 7 bits
void SetRenderState(inout ParticleStateV0 state, uint rs)
{
	state.m_flags1 = (state.m_flags1 & 0xFFF0FFFF) | (rs << 16);
}
// render state has 7 bits
uint GetRenderState(SnowParticleState state)
{
	uint renderState = (state.m_flags1 & 0x000F0000) >> 16;

	return renderState;
}

// render state has 7 bits
void SetRenderState(inout SnowParticleState state, uint rs)
{
	state.m_flags1 = (state.m_flags1 & 0xFFF0FFFF) | (rs << 16);
}


uint GetFlags0(SnowParticleState state)
{
	return state.m_flags0 & 0x000000FF;
}

uint GetFlags0(ParticleStateV0 state)
{
	return state.m_flags0 & 0x000000FF;
}

void SetFlags0(inout ParticleStateV0 state, uint rs)
{
	state.m_flags0 = (state.m_flags0 & 0xFFFFFF00) | (rs & 0x00FF);
}

void SetUintNorm(inout ParticleStateV0 state, uint norm)
{
	state.m_flags0 = (state.m_flags0 & 0x000000FF) | (norm << 8);
}

uint GetUintNorm(in ParticleStateV0 state)
{
	return (state.m_flags0 & 0xFFFFFF00) >> 8;
}

float3 UnpackUintNorm(uint norm)
{
	float x = (int(norm & 0x000000FF) - 127) / 127.0;
	float y = (int((norm & 0x0000FF00) >> 8) - 127) / 127.0;
	float z = (int((norm & 0x00FF0000) >> 16) - 127) / 127.0;

	return normalize(float3(x, y, z));
}

#define kHaveUvsBit (1 << 23)
#define kStartedAtHeadBit (1 << 22)
#define kIsInfected (1 << 21)
#define kHaveSkinningBit (1 << 20)


#define kMaterialMaskSkin (1 << 24)
#define kMaterialMaskHair (1 << 25)
#define kMaterialMaskAlphaDepth (1 << 26)
#define kMaterialMaskSpawnChildRibbons (1 << 27)
#define kMaterialMaskSpawnRTSplats (1 << 28)
#define kMaterialMaskSpawnTrackDecals (1 << 29)





#define ENABLE_DEBUG_FEED_BACK 1
#define ENABLE_DEBUG_FEED_BACK_NEW_DIE 0


float Random(float2 seed)
{
	// We need irrationals for pseudo randomness.
	const float2 r = float2(
		23.1406926327792690, // e^pi (Gelfond's constant)
		2.6651441426902251); // 2^sqrt(2) (Gelfond-Schneider constant)
	return frac(cos(fmod(123456789.0, 1e-7 + 256.0 * dot(seed, r))));

}

float GetLinearDepthError(float z, float2 params)
{
	float lin0 = GetLinearDepth(z, params);

	// flip last matissa bit 
	float z1 = asfloat(asuint(z) ^ 0x00000001);

	float lin1 = GetLinearDepth(z1, params);

	return abs(lin0 - lin1);
}

uint2 NdcToScreenSpacePos(ParticleComputeJobSrt *pSrt, float2 ndc)
{
	uint2 sspos = uint2(floor(float2((ndc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (ndc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y)));

	return sspos;
}

float ProbabilityBasedOnDistance(float dist)
{
	float factor = clamp(((dist) / 40.0), 0.0f, 1.0f);
	float probability = lerp(0.0, 0.4, factor * factor);

	return probability;
}

float ProbabilityBasedOnDistanceExp(float dist)
{
	//float factor = clamp(((dist) / 10.0), 0.0f, 1.0f);
	float probability = clamp(lerp(0.00, 1.0, exp(dist - 4)), 0.0f, 0.7f); // 2 * (x-4) exponential curve based on distance shifted by 4 meters

	return probability / 5.0f;
}


[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputePreProcessScreenQuadsForRainSplashes(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// grab my particle
	uint2 myCellPos = groupId * uint2(pSrt->m_gridCellW, pSrt->m_gridCellH);

	uint myCellIndex = (pSrt->m_screenResolution.x / pSrt->m_gridCellW) * groupId.y + groupId.x;

	// do 64 random samples in the cell to generate data about this cell. then use this data for spawning splashes in this cell
	float rand0 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 0); //  float((pSrt->m_gameTicks * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);
	float rand1 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 1); //float(((pSrt->m_gameTicks ^ 0x735fac31) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);
	

	uint2 sspos = myCellPos + uint2(pSrt->m_gridCellW * rand0, pSrt->m_gridCellH * rand1);
	float depthVs = GetLinearDepth(pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos], pSrt->m_depthParams);

	// calculate spawn probability based on distance 0.5 being maximum
	
	float probability = ProbabilityBasedOnDistanceExp(depthVs);
	
	probability = 1.0f;
	
	if (groupThreadId.x == 0)
	{
		pSrt->m_dataBuffer[myCellIndex].m_data0.x = probability;
	}

	//float3 posNdc = float3(rand0 * 2 - 1, -(rand1 * 2 - 1), pSrt->m_pPassDataSrt->m_primaryDepthTexture[sspos]);
}

// this pass removes all old particles and compresses into new list

#include "particle-compute-funcs.fxi"

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeUpdateCompress(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// grab my particle
	uint gdsOffsetOld = pSrt->m_gdsOffsetCounterOld;
	uint numOldParticles = NdAtomicGetValue(gdsOffsetOld);

	if (dispatchId.x >= numOldParticles)
		return; // todo: dispatch only the number of threads = number of particles

	ParticleStateV0 state = pSrt->m_particleStatesOld[dispatchId.x];

	float age = (pSrt->m_time - state.m_birthTime);

#if TRACK_INDICES
	float lifeTime = f16tof32(asuint(state.m_lifeTime));
#else
	float lifeTime = state.m_lifeTime;
#endif

	if (pSrt->m_delta < 0.00001)
	{
		// paused frame just copy over the data

		// write the state out to new buffer
		uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
		uint particleIndex = NdAtomicIncrement(gdsOffsetNew);

		pSrt->m_particleStates[particleIndex] = state;

	}
	else if (age > lifeTime)
	{
		return;
	}
	else
	{
		UpdateTrackingParticle(pSrt, 
			state.m_pos,    /*position*/
			state.m_scale,  /*velocity*/
			state.m_flags1, /*errorState*/
			state.m_flags0  /*prevStencil*/);
		
		if (state.m_flags1 >= kMaxErrorFrames)
		{
			return;
		}

		// write the state out to new buffer
		uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
		uint particleIndex = NdAtomicIncrement(gdsOffsetNew);

		pSrt->m_particleStates[particleIndex] = state;
	}
}

float GetExponentialDistanceFactor(float depthVs)
{
	return saturate(exp((depthVs - 2.0) / 40.0) - 1.0);
}

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeSpawnNew(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{

	/*
	uint myCellIndex = (pSrt->m_screenResolution.x / pSrt->m_gridCellW) * groupId.y + groupId.x;
	float probability = pSrt->m_dataBuffer[myCellIndex].m_data0.x;
	*/

	// check if match the texture
	// each group is ran per screen space quad

	uint2 myCellPos = groupId * uint2(pSrt->m_gridCellW, pSrt->m_gridCellH);

	// do 64 random samples in the cell to generate data about this cell. then use this data for spawning splashes in this cell
	float rand0 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 3);  // float((pSrt->m_gameTicks * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);
	float rand1 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 4);  // float(((pSrt->m_gameTicks ^ 0x735fac31) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);

	uint2 sspos = myCellPos + uint2(pSrt->m_gridCellW * rand0, pSrt->m_gridCellH * rand1);

	float3 posNdc = float3(sspos.x / pSrt->m_screenResolution.x * 2 - 1, -(sspos.y / pSrt->m_screenResolution.y * 2 - 1), pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos]);

	float4 posH = mul(float4(posNdc, 1), pSrt->m_pPassDataSrt->m_mAltVPInv);
	float3 posWs = posH.xyz / posH.w;
	posWs += pSrt->m_altWorldOrigin.xyz;

	float depthVs = GetLinearDepth(pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos], pSrt->m_depthParams);

	if (depthVs > pSrt->m_rootComputeVec2.z)
	{
		return;
	}
	
	float myProbabilityNumber = GetRandom(pSrt->m_gameTicks, dispatchId.x, 5);

	float probability = GetExponentialDistanceFactor(depthVs) * pSrt->m_rootComputeVec1.x;

	if (myProbabilityNumber >= probability)
	{
		// decided not to add
		return;
	}

	const uint4 sample0 =  pSrt->m_pPassDataSrt->m_gbuffer0[sspos];
	float roughness = UnpackByte0(sample0.w) / 255.f;
	if (roughness > kSplashMaxRoughness)
	{
		return;
	}

	const uint4 sample1 = pSrt->m_pPassDataSrt->m_gbuffer1[sspos];
	uint matMaskSample = pSrt->m_pPassDataSrt->m_materialMaskBuffer[sspos];

	if (matMaskSample & (MASK_BIT_HAIR) && (sample1.w & MASK_BIT_EXTRA_CHARACTER))
	{
		return;
	}

	// lookup the normal
	float3 depthNormalWS = CalculateDepthNormal(pSrt, sspos, true);
	float3 invertedRainDir = -1 * pSrt->m_rainDir.xyz;

	float nDotR = dot(depthNormalWS, invertedRainDir);
	if (nDotR < 0.5)
	{
		return;
	}

	// rain occluders
	float4 samplePos = float4(posWs, 1.0);
	float3 rainPos;
	rainPos.x = dot(pSrt->m_pCommonParticleComputeSrt->m_occlusionParameters[0], samplePos);
	rainPos.y = dot(pSrt->m_pCommonParticleComputeSrt->m_occlusionParameters[1], samplePos);
	rainPos.z = dot(pSrt->m_pCommonParticleComputeSrt->m_occlusionParameters[2], samplePos);
	if (pSrt->m_pPassDataSrt->m_rainOccluders.Sample(pSrt->m_linearSampler, rainPos.xy).r < rainPos.z)
	{
		return;
	}


	// grab my particle
	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint particleIndex = NdAtomicIncrement(gdsOffsetNew);

	uint size, stride;
	pSrt->m_particleStates.GetDimensions(size, stride);

	if (particleIndex >= size)
	{
		// decrement back
		NdAtomicDecrement(gdsOffsetNew);
		return; // can't add new particles
	}

	// first map to screen position from last frame
	float2 motionVectorSNorm = pSrt->m_pPassDataSrt->m_motionVector[sspos];
	int2 motionVector = int2(motionVectorSNorm * pSrt->m_screenResolution);
	float2 lastFrameSSposFloat = float2(sspos) + motionVector;
	int2 lastFrameSSpos = int2(float2(sspos) + motionVector);

	// check that the stencil bits match so that we don't pick up some random pixel
	if (pSrt->m_pPassDataSrt->m_lastFrameOpaquePlusAlphaStencil[lastFrameSSpos] != pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil[sspos])
	{
		// decrement back
		NdAtomicDecrement(gdsOffsetNew);

		return;
	}

	ParticleStateV0 state = ParticleStateV0(0);

	state.m_birthTime = pSrt->m_time;
	state.m_pos = posWs;
	state.m_flags0 = pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil[sspos]; // cache stencil bits

#if 0
	float3 posNdcUnjittered = posNdc;
	posNdcUnjittered.xy -= pSrt->m_projectionJitterOffsets.xy;

	float3 lastFramePosNdcUnjittered = float3(posNdcUnjittered.x + motionVectorSNorm.x * 2, posNdcUnjittered.y - motionVectorSNorm.y * 2, pSrt->m_pPassDataSrt->m_lastFrameOpaquePlusAlphaDepthTexture[lastFrameSSpos]);
	float3 lastFramePosNdc = lastFramePosNdcUnjittered;
	lastFramePosNdc.xy += pSrt->m_projectionJitterOffsets.zw;

	float4 lastFramePosH = mul(float4(lastFramePosNdc, 1), pSrt->m_pPassDataSrt->m_mLastFrameVPInv);
	float3 lastFramePosWs = lastFramePosH.xyz / lastFramePosH.w;

	state.m_scale = (posWs - lastFramePosWs) / pSrt->m_delta; // we store velocity in m_scale
#endif

	float spriteScale = (GetRandom(pSrt->m_gameTicks, dispatchId.x, 7) * 0.1 + 0.25) * kSpriteScale; 
	spriteScale *= sign(GetRandom(pSrt->m_gameTicks, dispatchId.x, 8)*2-1);
	
	//float channelPick = GetRandom(pSrt->m_gameTicks, dispatchId.x, 9)*3;
	float anotherRand = GetRandom(pSrt->m_gameTicks, dispatchId.x, 9);

	state.m_data = asfloat(PackFloat2ToUInt(spriteScale, anotherRand )); // pack scale and channel pick 
	
	state.m_speed = (abs(depthNormalWS.y) < (.75))? kReverseOffset : 0; //check to see if facing up or down, and make a value we can use to offset Y pos with


	//create a random float for normal detection variance
	//check if normals are facing up, if so apply flipbook offset to pick another splash variation
	//store slantoffset in state.m_id if normals face sideways
/*
	float normalVariance = GetRandom(pSrt->m_gameTicks, dispatchId.x, 5)/4;
	float slantoffset = (nDotR < 0.75)? kFlipframes : 0;
	state.m_id = (GetRandom(pSrt->m_gameTicks, dispatchId.x, 50) * kVariance) + slantoffset/kFps;


	float r01 = saturate(roughness/kSplashMaxRoughness);
	state.m_id = (r01 > .8)? kFlipframes * 2: (r01 > 0.4)? kFlipframes : 0;

	//if slantoffset has been added to state.m_id & surface is slanted remove slantoffset from variance
	float minusVariance = ((state.m_id) > kVariance)? state.m_id - slantoffset/kFps : state.m_id;
	state.m_lifeTime = kFlipframes / kFps;// - minusVariance; 
*/

	float var = GetRandom(pSrt->m_gameTicks, dispatchId.x, 50) ;
	float r01 = saturate((roughness/kSplashMaxRoughness)+(var*0.14-0.07));
	state.m_id = (r01 > kSplashRoughVal)? kFlipframes * 2: (r01 > kSplashMediumVal)? kFlipframes : 0;
	state.m_lifeTime = kFlipframes / kFps - (var * var * kVariance); 
	
	// direction is half vector between the nromal and rain direction
	float3 newDir = (depthNormalWS + -pSrt->m_rainDir.xyz + float3(0,1,0)) / 3;
	//newDir = lerp(newDir, float3(0,1,0), 0.5);
	newDir = normalize(newDir);
	
	state.m_rotation.xyz = newDir;
	//state.m_rotation.xyz = depthNormalWS;

	pSrt->m_particleStates[particleIndex] = state;
}

HintEmitterInfo EmitterFromHintShort(GenericEmitterHint hint)
{
	HintEmitterInfo info = HintEmitterInfo(0);
	info.pos = hint.m_pos;
	info.scale = hint.m_scale;

	info.rotation = QuatToTbn(DecodeTbnQuatCayley(hint.m_rotation));
	info.rotation[0] *= info.scale.x;
	info.rotation[1] *= info.scale.y;
	info.rotation[2] *= info.scale.z;	
	info.rate = hint.m_data0.x;

	return info;
}

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeSpawnRainSplashesFromHints(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	uint gdsOffsetOther = pSrt->m_gdsOffsetOther;
	if (gdsOffsetOther == 0)
		return;

	uint numEmitterHints = NdAtomicGetValue(gdsOffsetOther);

	if (numEmitterHints == 0)
		return;

	RWStructuredBuffer<GenericEmitterHint> destHintBuffer = __create_buffer<RWStructuredBuffer<GenericEmitterHint> >(__get_vsharp(pSrt->m_particleStatesOther));

	int emitterId = int(dispatchId % int(numEmitterHints));
	GenericEmitterHint hint = destHintBuffer[emitterId];

	float rate = hint.m_data0.x;
	rate = max(0, rate - (groupId.x * 64 / numEmitterHints));

	float emitterDistance = length( hint.m_pos.xyz - pSrt->m_cameraPosWs.xyz);
	float probability = saturate(exp((emitterDistance - 0.0) / 40.0) - 1.0) * pSrt->m_rootComputeVec1.x;
	rate *= probability;	

	int numPartsToSpawnThisFrame;
	{
		float interval = 1/rate;
		float modTime = pSrt->m_time % interval;
		float modTimeMinusDelta = modTime - pSrt->m_delta;
		numPartsToSpawnThisFrame = (modTimeMinusDelta < 0) ? 1 + floor(abs(modTimeMinusDelta)/interval) : 0;
	}

	if ((float(dispatchId) / numEmitterHints) >= numPartsToSpawnThisFrame)
	{
		return;
	}

	HintEmitterInfo info = EmitterFromHintShort(hint);

	float4x4 emitterTransform = 0;
	emitterTransform[0].xyz = info.rotation[0];
	emitterTransform[1].xyz = info.rotation[1];
	emitterTransform[2].xyz = info.rotation[2];
	emitterTransform[3] = float4(info.pos, 1.0);

	float4 rands;
	rands.x = GetRandom(pSrt->m_gameTicks, dispatchId.x, 1);
	rands.y = GetRandom(pSrt->m_gameTicks, dispatchId.x, 2);
	rands.z = GetRandom(pSrt->m_gameTicks, dispatchId.x, 3);
	rands.w = GetRandom(pSrt->m_gameTicks, dispatchId.x, 4);

	float3 position = float3(rands.x, rands.y, rands.z)-0.5; 
	position *= info.scale.xyz;
	position = mul(float4(position, 1.0), emitterTransform).xyz; 
	DepthPosInfo depthPos = DPISetup(position, pSrt);

	uint stencil = pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil[depthPos.posSS];

	bool onlyCharacters = hint.m_data0.y;

	const uint4 sample1 = pSrt->m_pPassDataSrt->m_gbuffer1[depthPos.posSS];
	bool isChar = sample1.w & MASK_BIT_EXTRA_CHARACTER;

	if ( onlyCharacters && !isChar)
	{
		return;
	}


	DPISampleDepth(depthPos, pSrt, true);
	DPIGetDepthPos(depthPos, pSrt);


	// volume test
	float3 depthPosES = depthPos.depthPosWS - info.pos;
	float3x3 rot = 0;
	rot[0].xyz = info.rotation[0];
	rot[1].xyz = info.rotation[1];
	rot[2].xyz = info.rotation[2];	
	depthPosES = mul(float4(depthPosES, 1.0), transpose(rot)).xyz;
	depthPosES *= 1.0/info.scale.xyz;
	if (any(abs(depthPosES) > 0.5))
	{
		return;
	}

/*
	// primitive volume test
	if (length(depthPos.depthPosWS - position) > (info.scale.x + info.scale.y + info.scale.z) / 3)
	{
		return;
	}
*/
	position = depthPos.depthPosWS;
	
	// from previous func ...
	uint matMaskSample = pSrt->m_pPassDataSrt->m_materialMaskBuffer[depthPos.posSS];
	bool isHair = matMaskSample & (MASK_BIT_HAIR);

	if (isHair && isChar)
	{
		return;
	}

	// lookup the normal
	float3 depthNormalWS = CalculateDepthNormal(pSrt, depthPos.posSS, true);
	float3 invertedRainDir = -1 * pSrt->m_rainDir.xyz;
	float nDotR = dot(depthNormalWS, invertedRainDir);
	if (nDotR < 0.25)
	{
		return;
	}

	// rain occluders
	float4 samplePos = float4(position, 1.0);
	float3 rainPos;
	rainPos.x = dot(pSrt->m_pCommonParticleComputeSrt->m_occlusionParameters[0], samplePos);
	rainPos.y = dot(pSrt->m_pCommonParticleComputeSrt->m_occlusionParameters[1], samplePos);
	rainPos.z = dot(pSrt->m_pCommonParticleComputeSrt->m_occlusionParameters[2], samplePos);
	if (pSrt->m_pPassDataSrt->m_rainOccluders.Sample(pSrt->m_linearSampler, rainPos.xy).r < rainPos.z)
	{
		return;
	}
	// ...

	ParticleStateV0 state = ParticleStateV0(0);
	state.m_pos.xyz = position;
	state.m_flags0 = stencil;
	state.m_birthTime = pSrt->m_time;

	const uint4 sample0 =  pSrt->m_pPassDataSrt->m_gbuffer0[depthPos.posSS];
	float roughness = UnpackByte0(sample0.w) / 255.f;
	float anotherRand = GetRandom(pSrt->m_gameTicks, dispatchId.x, 1337);//saturate(roughness/kSplashMaxRoughness)*2.9;

	// from previous func ...
	float spriteScale = (GetRandom(pSrt->m_gameTicks, dispatchId.x, 7) * 0.1 + 0.25) * kSpriteScale; 
	spriteScale *= sign(GetRandom(pSrt->m_gameTicks, dispatchId.x, 8)*2-1);
	//float anotherRand = GetRandom(pSrt->m_gameTicks, dispatchId.x, 9)*3;
	state.m_data = asfloat(PackFloat2ToUInt(spriteScale, anotherRand )); 


	float var = GetRandom(pSrt->m_gameTicks, dispatchId.x, 50) ;
	float r01 = saturate((roughness/kSplashMaxRoughness)+(var*0.3-0.15));
	state.m_id = (r01 > kSplashRoughVal)? kFlipframes * 2: (r01 > kSplashMediumVal)? kFlipframes : 0;
	state.m_lifeTime = kFlipframes / kFps - (var * var * kVariance); 



	// ...

	state.m_rotation.xyz = float3(0,1,0);

	// grab my particle
	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint particleIndex = NdAtomicIncrement(gdsOffsetNew);

	uint size, stride;
	pSrt->m_particleStates.GetDimensions(size, stride);

	if (particleIndex >= size)
	{
		// decrement back
		NdAtomicDecrement(gdsOffsetNew);
		return; // can't add new particles
	}

	pSrt->m_particleStates[particleIndex] = state;

}


[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeCopyNew(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	
	// grab my particle
	uint gdsOffsetOther = pSrt->m_gdsOffsetOther;
	uint numOtherParticles = NdAtomicGetValue(gdsOffsetOther);


	if (groupThreadId.x >= numOtherParticles)
		return;

	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint particleIndex = NdAtomicIncrement(gdsOffsetNew);

	uint size, stride;
	pSrt->m_particleStates.GetDimensions(size, stride);

	if (particleIndex >= size)
	{
		// decrement back
		NdAtomicDecrement(gdsOffsetNew);

		return; // can't add new particles
	}

	ParticleStateV0 otherState = pSrt->m_particleStatesOther[groupThreadId.x];

	ParticleStateV0 state = ParticleStateV0(0);


	state.m_birthTime = pSrt->m_time;

	state.m_pos = otherState.m_pos;


	//state.m_pos = state.m_pos + setup.normalWS * 0.5f;

	state.m_speed = 0;

	state.m_lifeTime = kFlipframes / kFps;

	state.m_rotation.xyz = otherState.m_rotation.xyz;
	state.m_id = otherState.m_id;

	state.m_scale = 0; // otherState.m_scale;

	//state.m_rotation.xyz = setup.normalWS;
	//state.m_rotation.xyz = -pSrt->m_rainDir.xyz;

	pSrt->m_particleStates[particleIndex] = state;
}


[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeJobDispatchIndirectPrepare(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobDispatchIndirectPrepareSrt *pSrt : S_SRT_DATA)
{
	uint gdsOffset = pSrt->m_gdsOffsetCounter;
	uint val = NdAtomicGetValue(gdsOffset);

	if (dispatchId.x == 0)
	{
		pSrt->m_dispathInidrectArgs[0] = uint3((val + 63) / 64, 1, 1);
	}
}

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeJobCounterEliminator(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	if (dispatchId.x == 0)
	{
		uint gdsOffset = pSrt->m_gdsOffsetOtherUseCases;
		uint val = NdAtomicIncrement(gdsOffset);

		if (val >= (*pSrt->m_pNumOtherBufferUses)-1)
		{
			// we can clear the state buffer counter since we are last job to touch it
			NdAtomicSetValue(0, pSrt->m_gdsOffsetOther); // we have now processed the list
		}
	}
}


[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeGenerateRenderables(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// grab my particle

	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint numParticles = NdAtomicGetValue(gdsOffsetNew);

	if (dispatchId.x >= numParticles)
		return; // todo: dispatch only the number of threads = number fo particles


	ParticleStateV0 state = pSrt->m_particleStates[dispatchId.x];

	float4x4	g_identity = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };
	float3 kUnitYAxis = { 0.0f, 1.0f, 0.0f };
	
	float3 pos = state.m_pos;

	// calculate ndc position

	float4 posH = mul(float4(pos, 1), pSrt->m_pPassDataSrt->g_mVP);
	float3 posNdc = posH.xyz / posH.w;

	float3 uvw;

	uvw.x = posNdc.x / 2.0f + 0.5f;
	uvw.y = posNdc.y / 2.0f + 0.5f;
	uvw.y = 1.0 - uvw.y;

	// posH.w is linear depth
	uvw.z = CameraLinearDepthToFroxelZCoordExp(posH.w, 0);

	float fogContrib = pSrt->m_rootComputeVec1.y;
	float fogScaleContrib = pSrt->m_rootComputeVec1.z;

	float4 fog = pSrt->m_pPassDataSrt->m_fogAccumTexture.SampleLevel(pSrt->m_linearSampler, uvw, 0).xyzw;

	// todo: better checks:
	if (!isfinite(fog.x) || !isfinite(fog.y) || !isfinite(fog.z))// || posH.w > 50.0f)
	{
		fog.xyzw = float4(0, 0, 0, 0);
	}
	if (fog.x > 0.9 || fog.y > 0.9 || fog.y > 0.9)
	{
		fog.xyzw = float4(0, 0, 0, 0);
	}

	ParticleInstance inst = ParticleInstance(0);
	uint origParticleIndex = pSrt->m_particleIndicesOrig[0];
	ParticleInstance originalParticle = pSrt->m_particleInstancesOrig[origParticleIndex];
	
	inst.color = originalParticle.color;

	// fog has alpha premultiplied into it. so we can just take teh color of fog
	//inst.color.xyz = fog.xyz * fogContrib * 512.0 + inst.color;


	// distance boost 
	inst.color.a *= lerp(1,pSrt->m_rootComputeVec2.y, GetExponentialDistanceFactor(posH.w));

	float fogScale = 1.0; // +fog.x * fogScaleContrib * 100;

	inst.world = g_identity;			// The object-to-world matrix
	


													//inst.color.xyz *= (state.m_speed > 2)? float3(1,0,0) : (state.m_speed > 1)? float3(0,1,0) : float3(1,1,0);
	inst.texcoord = float4(1, 1, 0, 0);		// Texture coordinate scale and bias (uv = uv0 * texcoord.xy + texcoord.zw)
	inst.userh = float4(0, 0, 0, 0);			// User attributes (used to be half data type)
	inst.userf = float4(0, 0, 0, 0);			// User attributes
	inst.partvars = float4(0, 0, 0, 0);		// Contains age, normalized age, ribbon distance, and frame number
	inst.invscale = float4(1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f);		// Reciprocal of the particle's half-scale vector

	float age = (pSrt->m_time - state.m_birthTime);
	inst.userh.x = age * kFps + state.m_id;
	
	float anotherRand = f16tof32(asuint(state.m_data) >> 16);
	//state.m_speed = state_m_speed_temp;

	float state_m_data_temp = f16tof32(asuint(state.m_data));
	state.m_data = state_m_data_temp;

	//inst.userh.w = state.m_speed;
	
	// use camera vector as direction
	float3 toCam = pSrt->m_cameraDirWs;
	inst.world = TransformFromLookAt(toCam, state.m_rotation.xyz , pos, true);

	float3 scale = float3(pSrt->m_rootComputeVec0.xyz) *  float3(state.m_data,abs(state.m_data),1);

	float distScale = 1/posH.w;
	distScale = (distScale > pSrt->m_rootComputeVec2.x) ? distScale : pSrt->m_rootComputeVec2.x;
	scale *= distScale * posH.w;

	inst.world[0].xyz = inst.world[0].xyz * scale.x;
	inst.world[1].xyz = inst.world[1].xyz * scale.y;
	inst.world[2].xyz = inst.world[2].xyz * scale.z;

	inst.invscale = float4(1.0f / (scale.x * 0.5), 1.0f / (scale.y * 0.5), 1.0f / (scale.z * 0.5), 1.0f / 1);

	//float4 posOffest = inst.world[3];
	float4 posOffest = mul(float4(float3(0,kOffsety*anotherRand,0), 1), inst.world);
	// modify position based on camera
	inst.world[3].xyz = posOffest.xyz - pSrt->m_altWorldOrigin.xyz;

	//partMat.SetRow(3, MulPointMatrix(offset.GetVec4(), partMat));

	inst.prevWorld = inst.world;		// Last frame's object-to-world matrix

	pSrt->m_particleInstances[dispatchId.x] = inst;
	pSrt->m_particleIndices[dispatchId.x] = dispatchId.x;
}


struct StickyParticleParams
{
	bool allowSkin;
	bool onlyOnSkin;
	bool opaqueAlphaDepthStencil;
};

StickyParticleParams DefaultStickyParticleParams()
{
	StickyParticleParams res;
	res.allowSkin = true;
	res.onlyOnSkin = false;
	res.opaqueAlphaDepthStencil = true;
	return res;
}

bool NewStickyParticleState(uint motionState, uint2 sspos, float3 posNdc, float3 posWs, uint defaultFlags1, uint stencil, ParticleComputeJobSrt *pSrt, uint2 dispatchId, uint particleId,
	bool checkInsideFirstParticle, float birthTimeOffset, float spawnData, int threadIdInLane, float lifetime, out ParticleStateV0 newState, bool normalHalfWayWithRain,
	float3 inheritedVelocity, bool isSnow, StickyParticleParams spawnParams, float defaultSpeed)
{
	bool allowSkin = spawnParams.allowSkin;

	Texture2D<uint> lastFrameStencilTextureToUse = spawnParams.opaqueAlphaDepthStencil ? pSrt->m_pPassDataSrt->m_lastFrameOpaquePlusAlphaStencil : pSrt->m_pPassDataSrt->m_lastFramePrimaryStencil;
	Texture2D<float> prevFrameDepthBufferToUse = spawnParams.opaqueAlphaDepthStencil ? pSrt->m_pPassDataSrt->m_lastFrameOpaquePlusAlphaDepthTexture : pSrt->m_pPassDataSrt->m_lastFramePrimaryDepth;

	newState = ParticleStateV0(0);

	if (checkInsideFirstParticle && true)
	{
		// we use original aprticle to check whether we want to be spawning things
		uint origParticleIndex = pSrt->m_particleIndicesOrig[0];
		ParticleInstance originalParticle = pSrt->m_particleInstancesOrig[origParticleIndex];
		originalParticle.world[3].xyz = originalParticle.world[3].xyz + pSrt->m_altWorldOrigin.xyz;


		// build inverse of the particle matrix

		float3x3	g_identity = { { 1, 0, 0 },{ 0, 1, 0 },{ 0, 0, 1 } };
		float3 kUnitYAxis = { 0.0f, 1.0f, 0.0f };
		float3x3 partInv = g_identity;

		//inst.world[0].xyz = inst.world[0].xyz * scale.x;
		//inst.world[1].xyz = inst.world[1].xyz * scale.y;
		//inst.world[2].xyz = inst.world[2].xyz * scale.z;

		//inst.invscale = float4(1.0f / (scale.x * 0.5), 1.0f / (scale.y * 0.5), 1.0f / (scale.z * 0.5), 1.0f / 1);

		//float4 posOffest = mul(float4(pSrt->m_renderOffset.xyz, 1), inst.world);

		float3 posWsParticleSpace = posWs - originalParticle.world[3].xyz;

		// modify position based on camera
		//partInv[0].xyz = originalParticle.world[0].xyz * originalParticle.invscale.x * 0.5f;
		//partInv[1].xyz = originalParticle.world[1].xyz * originalParticle.invscale.y * 0.5f;
		//partInv[2].xyz = originalParticle.world[2].xyz * originalParticle.invscale.z * 0.5f;

		float scaleX = 2.0f / originalParticle.invscale.x;
		float scaleY = 2.0f / originalParticle.invscale.y;
		float scaleZ = 2.0f / originalParticle.invscale.z;
		partInv[0].xyz = originalParticle.world[0].xyz / scaleX / scaleX;
		partInv[1].xyz = originalParticle.world[1].xyz / scaleY / scaleY;// *originalParticle.invscale.y * 0.5f;
		partInv[2].xyz = originalParticle.world[2].xyz / scaleZ / scaleZ;// *originalParticle.invscale.z * 0.5f;

		// put current position in the space of the original particle
		posWsParticleSpace = mul(partInv, posWsParticleSpace).xyz;

		if (posWsParticleSpace.x > 0.5 || posWsParticleSpace.x < -0.5 || posWsParticleSpace.y > 0.5 || posWsParticleSpace.y < -0.5 || posWsParticleSpace.z > 0.5 || posWsParticleSpace.z < -0.5)
		{
			return false;
		}
	}

	// lookup the normal
	const uint4 sample0 = pSrt->m_pPassDataSrt->m_gbuffer0[sspos];

	BrdfParameters brdfParameters = (BrdfParameters)0;
	Setup setup = (Setup)0;
	UnpackGBuffer(sample0, 0, brdfParameters, setup);

	// we wan't to only allow surfaces that don't face away from the rain direction
	float dirDot = dot(setup.normalWS, -pSrt->m_rainDir.xyz);
	float minAngleCos = cos(3.1415 / 180.0 * 60.0);
	if (dirDot <= minAngleCos)
	{
		//return;
	}

	// grab my particle

	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint size, stride;
	pSrt->m_particleStates.GetDimensions(size, stride);

	//_SCE_BREAK();

	// predict motion from motion vector


	// first map to screen position from last frame
	float2 motionVectorSNorm = pSrt->m_pPassDataSrt->m_motionVector[sspos];
	int2 motionVector = int2(motionVectorSNorm * pSrt->m_screenResolution);
	float2 lastFrameSSposFloat = float2(sspos)+motionVector;
	int2 lastFrameSSpos = int2(float2(sspos)+motionVector);

	// check that the stencil bits match so that we don't pick up some random pixel

	if (motionState == kMotionStateAttached)
	{
		// for state when we are attached

		if (lastFrameStencilTextureToUse[lastFrameSSpos] != stencil)
		{
			return false;
		}
	}



	float3 posNdcUnjittered = posNdc;
	posNdcUnjittered.xy -= pSrt->m_projectionJitterOffsets.xy;

	float3 lastFramePosNdcUnjittered = float3(posNdcUnjittered.x + motionVectorSNorm.x * 2, posNdcUnjittered.y - motionVectorSNorm.y * 2, prevFrameDepthBufferToUse[lastFrameSSpos]);
	float3 lastFramePosNdc = lastFramePosNdcUnjittered;
	lastFramePosNdc.xy += pSrt->m_projectionJitterOffsets.zw;

	float4 lastFramePosH = mul(float4(lastFramePosNdc, 1), pSrt->m_pPassDataSrt->m_mLastFrameVPInv);
	float3 lastFramePosWs = lastFramePosH.xyz / lastFramePosH.w;

	ParticleStateV0 state = ParticleStateV0(0);

	const uint4 sample1 = pSrt->m_pPassDataSrt->m_gbuffer1[sspos];

	uint materialMaskSample = pSrt->m_pPassDataSrt->m_materialMaskBuffer[sspos];
	UnpackMaterialMask(materialMaskSample, sample1, setup.materialMask);

	state.m_flags1 = defaultFlags1;
	uint stencilMask = 0;

	if (setup.materialMask.isCharacter)
	{
		// turn off opaque+alpha tracking on characters. just use regular depth.
		

		state.m_flags1 = state.m_flags1 & ~kMaterialMaskAlphaDepth;
	}


	stencilMask = ((setup.materialMask.hasSkin ? kMaterialMaskSkin : 0));// | (setup.materialMask.hasHair ? kMaterialMaskHair : 0));

	if (setup.materialMask.hasSkin)
	{
		//_SCE_BREAK();
	}
	
	if (spawnParams.onlyOnSkin && !setup.materialMask.hasSkin)
		return false;

	if (!allowSkin && (setup.materialMask.hasSkin || setup.materialMask.hasHair || setup.materialMask.hasEyes || setup.materialMask.hasTeeth))
	{
		return false;
	}

	if (!isSnow)
	{
		stencilMask =  stencilMask | (setup.materialMask.hasHair ? kMaterialMaskHair : 0);
	}

	state.m_scale = (posWs - lastFramePosWs) / pSrt->m_delta; // we store velocity in scale


	float rand3 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 3);  // float(((pSrt->m_gameTicks ^ 0x317ac35f) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);

	state.m_birthTime = pSrt->m_time + birthTimeOffset;

	// Test 2: spawn particles in random screenspace and lookup depth
	//float depthVs = GetLinearDepth(pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos], pSrt->m_depthParams);
	state.m_flags1 = state.m_flags1 | (motionState << 8);

	if (motionState == kMotionStateFreeFallAttach || motionState == kMotionStateDeadFreeFall || motionState == kMotionStateFreeFallCollide)
	{
		// spawn half way between camera and posWs
		//posWs = lerp(pSrt->m_cameraPosWs.xyz, posWs, 0.2);

		// no stencil
		// no masks
		stencilMask = 0;
		stencil = 0;

		state.m_scale = inheritedVelocity;
	}

	state.m_flags1 = state.m_flags1 | stencilMask | stencil;


	state.m_pos = posWs;
	state.m_id = particleId;





	//state.m_pos = state.m_pos + setup.normalWS * 0.5f;

	// for bubble we want to project rain direction onto the flat plane of the pixel

	//float3 rainDirForFlat = pSrt->m_rainDir.y > -0.99 ? pSrt->m_rainDir

	float3 flatX = cross(setup.normalWS, pSrt->m_rainDir);
	float3 flatZ = cross(flatX, setup.normalWS);
	float3 moveDir = flatZ;

	// or we could try 

	state.m_speed = defaultSpeed;
	
	
#if TRACK_INDICES
	state.m_lifeTime = asfloat(f32tof16(lifetime));
#else
	state.m_lifeTime = lifetime;
#endif

	
	// 1
								 // re-enable for bubble
								 //state.m_speed = rand3 * 0.2f;
								 //state.m_scale = state.m_scale + moveDir; // store move direction in scale 
								 //float rand4 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 4);
								 //state.m_lifeTime = 0.2f + rand4 * 0.2;

								 /*
								 state.m_speed = rand3 * 0.1f;

								 // find a random direction in xz plane perpendicular to normal
								 float angle = rand2 * 2 * 3.1415;

								 float3 dirFlat = float3(cos(angle), 0, sin(angle));

								 float3 moveDir = normalize(abs(setup.normalWS.y) > 0.01 ? cross(setup.normalWS, dirFlat) : cross(setup.normalWS, dirFlat.xzy));

								 state.m_scale = moveDir; // store move direction in scale
								 */


								 // direction is half vector between the nromal and rain direction
	float3 newDir = setup.normalWS;

	if (normalHalfWayWithRain)
	{
		newDir = (newDir + -pSrt->m_rainDir.xyz) / 2;
		newDir = normalize(newDir);
	}

	float3 depthNormal = CalculateDepthNormal(pSrt, sspos, spawnParams.opaqueAlphaDepthStencil);

	//todo: choose which one to use.

	newDir = depthNormal;

	state.m_rotation.xyz = newDir;

	if (motionState == kMotionStateDeadFreeFall)
	{
		// i dont think this is needed at all
		state.m_rotation.x = 0;
	}

	if (motionState == kMotionStateAttached)
	{
		// a) in case of attached, this stores the normal
		// b) except if we use tracking indices, this will encode barycentric data
		// it will be overriden outside of this function
		state.m_rotation.xyz = newDir;
	}


	state.m_data = spawnData; // 0 or -0.5 means just spawned

							  //state.m_rotation.xyz = setup.normalWS;
							  //state.m_rotation.xyz = -pSrt->m_rainDir.xyz;
	newState = state;

	return true;
}


bool AddParticleState(ParticleStateV0 newState, ParticleComputeJobSrt *pSrt, bool useProvidedIndex, int providedParticleIndex, out int resultIndex, int customSize)
{
	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint size, stride;
	pSrt->m_particleStates.GetDimensions(size, stride);

	if (customSize)
		size = customSize;


	uint particleIndex;
	resultIndex = -1;
	if (!useProvidedIndex)
	{
		particleIndex = NdAtomicIncrement(gdsOffsetNew);
		if (particleIndex >= size)
		{
			// decrement back
			NdAtomicDecrement(gdsOffsetNew);

			return false; // can't add new particles
		}
	}
	else
	{
		particleIndex = providedParticleIndex; // index was supplied
	}

	if (useProvidedIndex)
	{
		// this is the case where we are provided an index to write to already
		// also this is a special case where we allow only one thread to succeed
		//if (threadIdInLane == ReadFirstLane(threadIdInLane))
		{
			pSrt->m_particleStates[particleIndex] = newState;
		}
	}
	else
	{
		pSrt->m_particleStates[particleIndex] = newState;
	}
	resultIndex = particleIndex;

	return true;
}


bool NewStickyParticle(uint motionState, uint2 sspos, float3 posNdc, float3 posWs, uint defaultFlags1, uint stencil, ParticleComputeJobSrt *pSrt, uint2 dispatchId, uint particleId,
	bool checkInsideFirstParticle, float birthTimeOffset, float spawnData, bool useProvidedIndex,  int providedParticleIndex, int threadIdInLane, float lifetime, out ParticleStateV0 newState, bool normalHalfWayWithRain,
	float3 inheritedVelocity, bool isSnow, StickyParticleParams spawnParams, float defaultSpeed, out int resultIndex, int customSize)
{
	// have particle index to write to

	if (NewStickyParticleState(motionState, sspos, posNdc, posWs, defaultFlags1, stencil, pSrt, dispatchId, particleId,
		checkInsideFirstParticle, birthTimeOffset, spawnData, threadIdInLane, lifetime, newState, normalHalfWayWithRain, inheritedVelocity, isSnow, spawnParams, defaultSpeed))
	{
		// state created successfully
		return AddParticleState(newState, pSrt, useProvidedIndex, providedParticleIndex, resultIndex, customSize); // might fail if no space left
	}
	resultIndex = -1;
	return false;
}


bool NewStandardParticle(float3 posWs, float3 velocityWs, ParticleComputeJobSrt *pSrt, uint2 dispatchId, uint particleId, float birthTimeOffset, float spawnData, float lifetime, int flags1)
{

	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint size, stride;
	pSrt->m_particleStates.GetDimensions(size, stride);

	uint particleIndex;
	
	particleIndex = NdAtomicIncrement(gdsOffsetNew); //Increment the index so we can write a new particle. Then see if we've actually tried to create one too many particles.
	if (particleIndex >= size)
	{
		// decrement back
		NdAtomicDecrement(gdsOffsetNew);

		return false; // can't add new particles
	}

	//_SCE_BREAK();


	
	ParticleStateV0 state = ParticleStateV0(0);



	state.m_birthTime = pSrt->m_time + birthTimeOffset;

	state.m_scale = float3(0, 0, 0);
	state.m_flags1 = flags1;


	state.m_pos = posWs;
	state.m_scale = velocityWs;
	state.m_id = particleId;




	state.m_speed = 1;
	state.m_lifeTime = lifetime; // 1

//	float3 newDir = (setup.normalWS + -pSrt->m_rainDir.xyz) / 2;
	float3 newDir = normalize(pSrt->m_rainDir.xyz);

	state.m_rotation.xyz = newDir;
	state.m_data = spawnData; // 0 or -0.5 means just spawned


	pSrt->m_particleStates[particleIndex] = state; //Here's where the new particle's state is added to the state buffer.
	return true;
}

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeBubbleSpawnNew(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	uint myCellIndex = (pSrt->m_screenResolution.x / pSrt->m_gridCellW) * groupId.y + groupId.x;

	float probability = pSrt->m_dataBuffer[myCellIndex].m_data0.x;

	// check if match the texture
	// each group is ran per screen space quad

	uint2 myCellPos = groupId * uint2(pSrt->m_gridCellW, pSrt->m_gridCellH);

		// do 64 random samples in the cell to generate data about this cell. then use this data for spawning splashes in this cell
	float rand0 = GetRandom(pSrt->m_gameTicks/* + float(bit_cast<uint>(pSrt))*/, dispatchId.x, 0);  // float((pSrt->m_gameTicks * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);
	float rand1 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 1);  // float(((pSrt->m_gameTicks ^ 0x735fac31) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);


	uint2 sspos = myCellPos + uint2(pSrt->m_gridCellW * rand0, pSrt->m_gridCellH * rand1);

	float depthVs = GetLinearDepth(pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos], pSrt->m_depthParams);

	probability = ProbabilityBasedOnDistance(depthVs) * 10 * pSrt->m_rootComputeVec1.x;

	float rand2 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 2);  // float(((pSrt->m_gameTicks ^ 0xac31735f) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);

	if (groupThreadId.x + rand2 >= probability * 64.0f)
	{
		// decided not to add
		return;
	}

	if (groupThreadId.x > 0)
	{
	//	return;
	}

	//_SCE_BREAK();

	// we use original aprticle to check whether we want to be spawning things
	uint origParticleIndex = pSrt->m_particleIndicesOrig[0];
	ParticleInstance originalParticle = pSrt->m_particleInstancesOrig[origParticleIndex];
	originalParticle.world[3].xyz = originalParticle.world[3].xyz + pSrt->m_altWorldOrigin.xyz;

	float3 topPosWs = mul(float4(rand0 - 0.5, 0.5, rand1 - 0.5, 1.0), originalParticle.world).xyz;
	
	float4 topPosH = mul(float4(topPosWs, 1), pSrt->m_pPassDataSrt->g_mVP);
	float3 topPosNdc = topPosH.xyz / topPosH.w;

	uint2 topSspos = uint2((topPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (topPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);



	// build inverse of the particle matrix

//	float3x3	g_identity = { { 1, 0, 0 },{ 0, 1, 0 },{ 0, 0, 1 } };
//	float3 kUnitYAxis = { 0.0f, 1.0f, 0.0f };
//	float3x3 partInv = g_identity;
//
//	float3 posWsParticleSpace = posWs - originalParticle.world[3].xyz;
//
//	// modify position based on camera
//	//partInv[0].xyz = originalParticle.world[0].xyz * originalParticle.invscale.x * 0.5f;
//	//partInv[1].xyz = originalParticle.world[1].xyz * originalParticle.invscale.y * 0.5f;
//	//partInv[2].xyz = originalParticle.world[2].xyz * originalParticle.invscale.z * 0.5f;
//
//	float scaleX = 2.0f / originalParticle.invscale.x;
//	float scaleY = 2.0f / originalParticle.invscale.y;
//	float scaleZ = 2.0f / originalParticle.invscale.z;
//	partInv[0].xyz = originalParticle.world[0].xyz / scaleX / scaleX;
//	partInv[1].xyz = originalParticle.world[1].xyz / scaleY / scaleY;// *originalParticle.invscale.y * 0.5f;
//	partInv[2].xyz = originalParticle.world[2].xyz / scaleZ / scaleZ;// *originalParticle.invscale.z * 0.5f;






	uint stencil = pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil[sspos];
	if (stencil & 0x20) // check fg stencil bit
	{
		//return; // don't allow on fg
	}

	uint gdsOffsetId = pSrt->m_gdsOffsetIdCounter;
	uint particleId = uint(NdAtomicIncrement(gdsOffsetId)); // we rely on ribbons to die before we can spawn them again. Todo: use the data buffer to read how many particles we have in ribbon already


	// will add particle to the atomic buffer or retrun not doing anything
	float3 posNdc = float3(sspos.x / pSrt->m_screenResolution.x * 2 - 1, -(sspos.y / pSrt->m_screenResolution.y * 2 - 1), pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos]);
	float4 posH = mul(float4(posNdc, 1), pSrt->m_pPassDataSrt->m_mAltVPInv);
	float3 posWs = posH.xyz / posH.w;
	posWs += pSrt->m_altWorldOrigin.xyz;

	ParticleStateV0 addedState;
	int resultIndex;

	StickyParticleParams spawnParams = DefaultStickyParticleParams();
	spawnParams.allowSkin = false;
	
	NewStickyParticle(kMotionStateFreeFallAttach, topSspos, topPosNdc, topPosWs, 0, stencil, pSrt, dispatchId, particleId, 
		/*checkInsideFirstParticle=*/ false, /*birthTimeOffset=*/ 0.001f, /*spawnData*/ rand1 * 3.1415 * 8, /*useProvidedIndex*/ false, 
		/*providedParticleIndex=*/ -1, groupThreadId.x, /*lifetime=*/ 20.0, addedState, /*normalHalfWayWithRain=*/ false,
		 /*inheritedVelocity=*/ float3(0, 0, 0), /*isSnow*/ false, spawnParams, /*defaultSpeed*/ 1.0f, resultIndex, /*customSize*/ 0);


	//NewStickyParticle(kMotionStateFreeFallAttach, sspos, posNdc, posWs, 0, stencil, pSrt, dispatchId, particleId, /*checkInsideFirstParticle=*/ false, /*birthTimeOffset=*/ 0.001f, /*spawnData*/ -0.5f, /*useProvidedIndex*/ false, /*providedParticleIndex=*/ -1, groupThreadId.x, /*lifetime=*/ 10.0, /*defaultSpeed*/ 1.0f, /*customSize*/ 0);
}

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeStickySnowSpawnNew(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	uint myCellIndex = (pSrt->m_screenResolution.x / pSrt->m_gridCellW) * groupId.y + groupId.x;

	float probability = pSrt->m_dataBuffer[myCellIndex].m_data0.x;

	// check if match the texture
	// each group is ran per screen space quad

	uint2 myCellPos = groupId * uint2(pSrt->m_gridCellW, pSrt->m_gridCellH);

	// do 64 random samples in the cell to generate data about this cell. then use this data for spawning splashes in this cell
	float rand0 = GetRandom(pSrt->m_gameTicks/* + float(bit_cast<uint>(pSrt))*/, dispatchId.x, 0);  // float((pSrt->m_gameTicks * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);
	float rand1 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 1);  // float(((pSrt->m_gameTicks ^ 0x735fac31) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);


	uint2 sspos = myCellPos + uint2(pSrt->m_gridCellW * rand0, pSrt->m_gridCellH * rand1);

	float depthVs = GetLinearDepth(pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos], pSrt->m_depthParams);

	probability = pSrt->m_rootComputeVec1.x;

	float rand2 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 2);  // float(((pSrt->m_gameTicks ^ 0xac31735f) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);
	float rand3 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 3);  // float(((pSrt->m_gameTicks ^ 0xac31735f) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);

	if (groupThreadId.x + rand2 >= probability * 64.0f)
	{
		// decided not to add
		//return;
	}

	//if (groupThreadId.x > 0)
	//{
	//	return;
	//}
	//if (groupThreadId.x ==	 0)
	//{
	//	return;
	//}
	uint cpuEmitterListSize, cpuEmitterListStride;
	pSrt->m_cpuEmitterList.GetDimensions(cpuEmitterListSize, cpuEmitterListStride);

	// maybe we should filter outemitters that are far away, but for now consider all of them
	int mySimpleEmitter = cpuEmitterListSize > 0 ? (dispatchId % cpuEmitterListSize) : -1;

	float3x3 emissionRot;
	float3 emissionPos;
	float3 topPosWs;
	float3 fwAxis;
#if 1
	if (mySimpleEmitter >= 0)
	{
		ParticleEmitterEntry emitter = pSrt->m_cpuEmitterList[mySimpleEmitter];

		/*
		float distToCamera = length(emitter.m_pos - pSrt->m_cameraPosWs.xyz);

		float rate = emitter.m_rate;

		rate *= LinStep(pSrt->m_pCpuEmitterComputeCustomData->m_vec0.y, pSrt->m_pCpuEmitterComputeCustomData->m_vec0.x, distToCamera);

		if (rate < 0.001)
			return;
		*/
		
		float scaleX = emitter.m_scale.x;
		float scaleY = emitter.m_scale.y;
		float scaleZ = emitter.m_scale.z;
		

		//hint.m_scale *= 0.01f;

		emissionRot[0] = emitter.m_rot[0].xyz * scaleX;
		emissionRot[1] = emitter.m_rot[1].xyz * scaleY;
		emissionRot[2] = emitter.m_rot[2].xyz * scaleZ;

		emissionPos = emitter.m_pos.xyz;

		float4x4 emissionM;
		emissionM[0] = float4(emissionRot[0], 0.0f);
		emissionM[1] = float4(emissionRot[1], 0.0f);
		emissionM[2] = float4(emissionRot[2], 0.0f);
		emissionM[3] = float4(emissionPos, 1.0f);


		//topPosWs = mul(float4(rand0, rand1, 1.0, 1.0), emissionM).xyz;

		topPosWs = mul(float4(rand0 - 0.5, rand1 - 0.5, 0.5, 1.0), emissionM).xyz;


		fwAxis = emitter.m_rot[2].xyz;

		//topPosWs += fwAxis;
	}
	else
#endif
	{
		// take data from the particle

		// we use original particle to check whether we want to be spawning things
		uint origParticleIndex = pSrt->m_particleIndicesOrig[0];
		ParticleInstance originalParticle = pSrt->m_particleInstancesOrig[origParticleIndex];
		originalParticle.world[3].xyz = originalParticle.world[3].xyz + pSrt->m_altWorldOrigin.xyz;

		emissionRot[0] = originalParticle.world[0].xyz;
		emissionRot[1] = originalParticle.world[1].xyz;
		emissionRot[2] = originalParticle.world[2].xyz;

		emissionPos = originalParticle.world[3].xyz;
		 
		topPosWs = mul(float4(rand0 - 0.5, 0.5, rand1 - 0.5, 1.0), originalParticle.world).xyz;

		float scaleY = 2.0f / originalParticle.invscale.y;

		fwAxis = -originalParticle.world[1].xyz / scaleY;
	}

	//_SCE_BREAK();

	
	
	//topPosWs += fwAxis;// test, offsets fw by one meter from top plane of particle.

	float4 topPosH = mul(float4(topPosWs, 1), pSrt->m_pPassDataSrt->g_mVP);
	float3 topPosNdc = topPosH.xyz / topPosH.w;

	uint2 topSspos = uint2((topPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (topPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);



	// build inverse of the particle matrix

	//	float3x3	g_identity = { { 1, 0, 0 },{ 0, 1, 0 },{ 0, 0, 1 } };
	//	float3 kUnitYAxis = { 0.0f, 1.0f, 0.0f };
	//	float3x3 partInv = g_identity;
	//
	//	float3 posWsParticleSpace = posWs - originalParticle.world[3].xyz;
	//
	//	// modify position based on camera
	//	//partInv[0].xyz = originalParticle.world[0].xyz * originalParticle.invscale.x * 0.5f;
	//	//partInv[1].xyz = originalParticle.world[1].xyz * originalParticle.invscale.y * 0.5f;
	//	//partInv[2].xyz = originalParticle.world[2].xyz * originalParticle.invscale.z * 0.5f;
	//
	//	float scaleX = 2.0f / originalParticle.invscale.x;
	//	float scaleY = 2.0f / originalParticle.invscale.y;
	//	float scaleZ = 2.0f / originalParticle.invscale.z;
	//	partInv[0].xyz = originalParticle.world[0].xyz / scaleX / scaleX;
	//	partInv[1].xyz = originalParticle.world[1].xyz / scaleY / scaleY;// *originalParticle.invscale.y * 0.5f;
	//	partInv[2].xyz = originalParticle.world[2].xyz / scaleZ / scaleZ;// *originalParticle.invscale.z * 0.5f;






	uint stencil = pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil[sspos];
	if (stencil & 0x20) // check fg stencil bit
	{
		//return; // don't allow on fg
	}

	uint gdsOffsetId = pSrt->m_gdsOffsetIdCounter;
	uint particleId = uint(NdAtomicIncrement(gdsOffsetId)); // we rely on ribbons to die before we can spawn them again. Todo: use the data buffer to read how many particles we have in ribbon already

	ParticleStateV0 addedState;
	int resultIndex;
		
	float3 inheritedVelocity = fwAxis * 2; // pSrt->m_rootComputeVec1.y;

	StickyParticleParams spawnParams = DefaultStickyParticleParams();
	spawnParams.allowSkin = false;
	float3 inheritedVelocityCompressed = float3(0, 0, 0);
	inheritedVelocityCompressed.x = asfloat(f32tof16(inheritedVelocity.x) | (f32tof16(inheritedVelocity.y) << 16));
	inheritedVelocityCompressed.y = asfloat(f32tof16(inheritedVelocity.z));

	NewStickyParticle(kMotionStateFreeFallAttach, topSspos, topPosNdc, topPosWs, 0, stencil, pSrt, dispatchId, particleId, 
		/*checkInsideFirstParticle=*/ true, /*birthTimeOffset=*/ 0.001f, /*spawnData*/ rand3, /*useProvidedIndex*/ false,
		/*providedParticleIndex=*/ -1, groupThreadId.x, /*lifetime=*/ 20.0, addedState, /*normalHalfWayWithRain=*/ false, 
		inheritedVelocityCompressed, /*isSnow*/ true, spawnParams, /*defaultSpeed*/ 1.0f, resultIndex, /*customSize*/ 0);
}

//


//
float3 Gravity(FieldState fs)
{
	float g = fs.magnitude; 

	
	return float3(0,-g,0);


}

float3 Drag(FieldState fs, const float3 velocity)
{
	float magnitude = fs.magnitude; //replace with magnitudefrom particle
	float3 dragForce = velocity * -1 * magnitude;
	return dragForce;
}


uint3 ForwardCsModeDefaultKeyGetIndices(uint2 key)
{
	return uint3((key.x >> 1) & 0xffff, key.y >> 16, key.y & 0xffff);
}

uint GetObjectId(ParticleComputeJobSrt *pSrt, uint2 sspos)
{
	uint objId;
	if (pSrt->m_isNeoMode)
	{
		uint objectAndPrimIds = pSrt->m_pPassDataSrt->m_objectId[sspos].x;
		objId = (objectAndPrimIds & 0xfffe0000) >> 17;
	}
	else
	{
		uint2 objectAndPrimIds = pSrt->m_pPassDataSrt->m_objectId[sspos];
		objId = objectAndPrimIds.x >> 17;
	}
	
	return objId;
}

// need to sync with game code!
//typedef I32 Look2BodyPart;
//const Look2BodyPart kLook2BodyPartHead = 0x0;
//const Look2BodyPart kLook2BodyPartTorso = 0x1;
//const Look2BodyPart kLook2BodyPartLegs = 0x2;
//const Look2BodyPart kLook2BodyPartBackpack = 0x3;
#define kLook2BodyPartHead 0x0
#define kLook2BodyPartTorso 0x1
#define kLook2BodyPartLegs 0x2
#define kLook2BodyPartBackpack 0x3

#define kProcTypePlayer 0x0
#define kProcTypeInfected 0x1

struct FindProcessResults
{
	int m_foundIndex; // index of process record found
	uint3 m_indices;
	float3 m_baricentrics;
	float2 m_uv;
	float3 m_posWs;
	float3 m_bindPosWs;
	float3 m_bindPosLs;
	uint m_rtId;
	int m_meshId; // 24 bits of submesh id
	int m_bodyPart;
	int m_procType;
	float m_bloodMapValue;
	float3 m_fNorm;
	uint m_uNorm;

};

// if there is a success, only oen htread returns res.m_foundIndex != -1;

FindProcessResults FindProcessMeshTriangleBaricentrics(ParticleComputeJobSrt *pSrt, uint2 sspos, float3 posWs, uint threadId)
{
	FindProcessResults res = FindProcessResults(0);
	res.m_foundIndex = -1;

	// we have 64 threads running, aand sgpr for position. now lets check if we can find an object based on obj id
	uint numMappings = *pSrt->m_pNumObjIdMappings;
	uint numObjIds = min(kMaxTrackedObjects, numMappings);

	uint numIters = (numObjIds + 63) / 64;

	int foundIndex = -1;

	uint3 indices = uint3(0, 0, 0);
	uint objId;
	uint primitiveId = 0;
	bool isNeo = pSrt->m_isNeoMode;

	if (!isNeo)
	{
		uint2 objectAndPrimIds = pSrt->m_pPassDataSrt->m_objectId[sspos];
		indices = ForwardCsModeDefaultKeyGetIndices(objectAndPrimIds);

		uint facing = objectAndPrimIds.x & 0x1;
		objId = objectAndPrimIds.x >> 17;
	}
	else
	{
		uint objectAndPrimIds = pSrt->m_pPassDataSrt->m_objectId[sspos].x;
		primitiveId = objectAndPrimIds & 0xffff;

		// will get these later
		//indices = indexBuffer[primitiveId * 3 + 0];
		//indices = indexBuffer[primitiveId * 3 + 1];
		//indices = indexBuffer[primitiveId * 3 + 2];

		objId = (objectAndPrimIds & 0xfffe0000) >> 17;
	}


	uint vertex0 = indices.x;
	uint vertex1 = indices.y;
	uint vertex2 = indices.z;


	for (int iIter = 0; iIter < numIters; ++iIter)
	{
		int indexToCheck = iIter * 64 + threadId;
		if (indexToCheck < numObjIds)
		{


			if (GetObjectId(pSrt->m_objectIdMappings[indexToCheck]) == objId)
			{
				foundIndex = indexToCheck;
			}
		}
	}
	int firstGoodThread = __s_ff1_i32_b64(__v_cmp_ne_i32(foundIndex, -1));

	uint4 vsharp = __get_vsharp(pSrt->m_particleFeedBackHeaderData);

	
	//if (threadId == 0)
	//{
	//	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	//	if (prevIndex < kMaxFeedBackEvents)
	//	{
	//		// can add new data
	//		pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, objId, firstGoodThread, numMappings /*ribbonId*/);
	//		pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
	//		pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(posWs, 0.0f);
	//		pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(0.0f, 0.0f, 0.0f, 0.0f);
	//	}
	//}

	
	if (foundIndex != -1)
	{
		// some threads might have succeeded up to here


		{
			// read one and compare to what we found




			{
				// found it
				// now enable just one thread

				uint mapIndex = ReadFirstLane(foundIndex);

				// enabling only one (first) succeeding thread
				if (threadId == ReadFirstLane(threadId))
				{
					//uint triIndex = objId & 0x000000FF; // totally wrong
					//

					uint i0 = vertex0;
					uint i1 = vertex1;
					uint i2 = vertex2;

					if (isNeo)
					{
						i0 = pSrt->m_objectIdMappings[mapIndex].m_indexBuffer[primitiveId * 3 + 0];
						i1 = pSrt->m_objectIdMappings[mapIndex].m_indexBuffer[primitiveId * 3 + 1];
						i2 = pSrt->m_objectIdMappings[mapIndex].m_indexBuffer[primitiveId * 3 + 2];
					}

					res.m_indices = uint3(i0, i1, i2);

					uint instanceId = 0; // pSrt->m_objectIdMappings[mapIndex].m_instanceId;

					float2 uv0 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i0, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);
					float2 uv1 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i1, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);
					float2 uv2 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i2, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);

					uint indexOffset = 0;

					indexOffset = instanceId * pSrt->m_objectIdMappings[mapIndex].m_numVertices;

					float3 posLs0 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i0 + indexOffset);
					float3 posLs1 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i1 + indexOffset);
					float3 posLs2 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i2 + indexOffset);

					float3 posWs0 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs0, 1.0)).xyz;
					float3 posWs1 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs1, 1.0)).xyz;
					float3 posWs2 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs2, 1.0)).xyz;
					//uv = ReadFirstLane(uv);

					float3 bindPosLs0 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i0 + indexOffset);
					float3 bindPosLs1 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i1 + indexOffset);
					float3 bindPosLs2 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i2 + indexOffset);

					float3 bindPosWs0 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(bindPosLs0, 1.0)).xyz;
					float3 bindPosWs1 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(bindPosLs1, 1.0)).xyz;
					float3 bindPosWs2 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(bindPosLs2, 1.0)).xyz;



					// calculate barycentric
					float totalArea = length(cross(posWs1 - posWs0, posWs2 - posWs0)) / 2.0;
					float area0 = length(cross(posWs1 - posWs, posWs2 - posWs)) / 2.0;
					float area1 = length(cross(posWs0 - posWs, posWs2 - posWs)) / 2.0;
					float b0 = area0 / totalArea;
					float b1 = area1 / totalArea;
					float b2 = 1.0 - b0 - b1;

					float2 resultUv = uv0 * b0 + uv1 * b1 + uv2 * b2;

					float3 resultPosWs = posWs0 * b0 + posWs1 * b1 + posWs2 * b2;
					float3 resultBindPosWs = bindPosWs0 * b0 + bindPosWs1 * b1 + bindPosWs2 * b2;
					float3 resultBindPosLs = bindPosLs0 * b0 + bindPosLs1 * b1 + bindPosLs2 * b2;

					res.m_foundIndex = foundIndex;

					res.m_uv = resultUv;

					res.m_posWs = resultPosWs;
					res.m_bindPosWs = resultBindPosWs;
					res.m_bindPosLs = resultBindPosLs;

					res.m_rtId = GetRtId(pSrt->m_objectIdMappings[mapIndex]);
					res.m_meshId = GetMeshId(pSrt->m_objectIdMappings[mapIndex]);
					res.m_bodyPart = GetBodyPart(pSrt->m_objectIdMappings[mapIndex]);
					res.m_procType = GetProcessType(pSrt->m_objectIdMappings[mapIndex]);
					res.m_baricentrics = float3(b0, b1, b2);

					res.m_fNorm = normalize(cross(posWs1 - posWs0, posWs2 - posWs0));
					uint nx = (127 * (res.m_fNorm.x + 1));
					uint ny = (127 * (res.m_fNorm.y + 1));
					uint nz = (127 * (res.m_fNorm.z + 1));
					res.m_uNorm = (nx) | (ny << 8) | (nz << 16);

				}
			}
		}

	}

	return res;
}


// has to be called by 64 threads, but those threads can provide different positions
FindProcessResults FindProcessMeshTriangleBaricentricsDivergent(ParticleComputeJobSrt *pSrt, bool doChecks, uint2 sspos, float3 posWs, uint threadId,
	bool checkBlood, float bloodThreshold, bool allowHead)
{

	FindProcessResults res = FindProcessResults(0);
	res.m_foundIndex = -1;

	// we have 64 threads running, aand sgpr for position. now lets check if we can find an object based on obj id
	uint numMappings = *pSrt->m_pNumObjIdMappings;
	uint numObjIds = min(kMaxTrackedObjects, numMappings);

	uint numIters = (numObjIds + 63) / 64;

	uint3 indices = uint3(0, 0, 0);
	uint objId;
	uint primitiveId = 0;
	bool isNeo = pSrt->m_isNeoMode;

	if (doChecks)
	{
		if (!isNeo)
		{
			uint2 objectAndPrimIds = pSrt->m_pPassDataSrt->m_objectId[sspos];
			indices = ForwardCsModeDefaultKeyGetIndices(objectAndPrimIds);

			uint facing = objectAndPrimIds.x & 0x1;
			objId = objectAndPrimIds.x >> 17;
		}
		else
		{
			uint objectAndPrimIds = pSrt->m_pPassDataSrt->m_objectId[sspos].x;
			primitiveId = objectAndPrimIds & 0xffff;

			// will get these later
			//indices = indexBuffer[primitiveId * 3 + 0];
			//indices = indexBuffer[primitiveId * 3 + 1];
			//indices = indexBuffer[primitiveId * 3 + 2];

			objId = (objectAndPrimIds & 0xfffe0000) >> 17;
		}
	}
	else
	{
		objId = -1; // special obj id will never succeed
	}

	uint vertex0 = indices.x;
	uint vertex1 = indices.y;
	uint vertex2 = indices.z;


	// at this point some threads might be looking for different obj ids, so we group the by that id
	uint storedObjectId = 0;
	int objIdIndex = 0;

	ulong exec = __s_read_exec();

	uint processed = 0; // these threads obj ids have not been looked at

	// we go trhough each thread and compact list of obj ids, by storing them into lane registers

	while (true)
	{
		ulong unprocessed_mask = __v_cmp_eq_u32(processed, 0);
		int firstThread = __s_ff1_i32_b64(unprocessed_mask);

		if (firstThread == -1)
			break;

		uint uniform_objId = ReadLane(objId, asuint(firstThread));

		if (uniform_objId == objId)
		{
			processed = 1;
		}

		WriteLane(storedObjectId, uniform_objId, asuint(objIdIndex));

		objIdIndex += 1;
	}

	// back to 64 threads. and we know we need to do objIdIndex iterations (if all threads landed on same mesh, it is just one iteration)

	for (int iObjIdIter = 0; iObjIdIter < objIdIndex; ++iObjIdIter)
	{
		int foundIndex = -1;

		uint targetObjId = ReadLane(storedObjectId, asuint(iObjIdIter));
		for (int iIter = 0; iIter < numIters; ++iIter)
		{
			int indexToCheck = iIter * 64 + threadId;
			if (indexToCheck < numObjIds)
			{
				if (GetObjectId(pSrt->m_objectIdMappings[indexToCheck]) == targetObjId)
				{
					foundIndex = indexToCheck;
				}
			}
		}
		
		int firstGoodThread = __s_ff1_i32_b64(__v_cmp_ne_i32(foundIndex, -1));

		if (firstGoodThread != -1)
		{
			// we have one thread that found it. we can now resume all threads that care about this obj id, and they will lookup their data
			// at this point we still have 64 threads

			foundIndex = ReadLane(foundIndex, asuint(firstGoodThread));
			// now all threads have the right mapping index

			// now filter threads that care about it

			if (targetObjId == objId)
			{
				uint i0 = vertex0;
				uint i1 = vertex1;
				uint i2 = vertex2;

				uint mapIndex = foundIndex;

				if (isNeo)
				{
					i0 = pSrt->m_objectIdMappings[mapIndex].m_indexBuffer[primitiveId * 3 + 0];
					i1 = pSrt->m_objectIdMappings[mapIndex].m_indexBuffer[primitiveId * 3 + 1];
					i2 = pSrt->m_objectIdMappings[mapIndex].m_indexBuffer[primitiveId * 3 + 2];
				}

				res.m_indices = uint3(i0, i1, i2);

				uint instanceId = 0; // pSrt->m_objectIdMappings[mapIndex].m_instanceId;

				float2 uv0 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i0, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);
				float2 uv1 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i1, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);
				float2 uv2 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i2, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);

				uint indexOffset = 0;

				indexOffset = instanceId * pSrt->m_objectIdMappings[mapIndex].m_numVertices;

				float3 posLs0 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i0 + indexOffset);
				float3 posLs1 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i1 + indexOffset);
				float3 posLs2 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i2 + indexOffset);

				float3 posWs0 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs0, 1.0)).xyz;
				float3 posWs1 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs1, 1.0)).xyz;
				float3 posWs2 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs2, 1.0)).xyz;
				//uv = ReadFirstLane(uv);

				float3 bindPosLs0 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i0 + indexOffset);
				float3 bindPosLs1 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i1 + indexOffset);
				float3 bindPosLs2 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i2 + indexOffset);

				float3 bindPosWs0 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(bindPosLs0, 1.0)).xyz;
				float3 bindPosWs1 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(bindPosLs1, 1.0)).xyz;
				float3 bindPosWs2 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(bindPosLs2, 1.0)).xyz;



				// calculate barycentric
				float totalArea = length(cross(posWs1 - posWs0, posWs2 - posWs0)) / 2.0;
				float area0 = length(cross(posWs1 - posWs, posWs2 - posWs)) / 2.0;
				float area1 = length(cross(posWs0 - posWs, posWs2 - posWs)) / 2.0;
				float b0 = area0 / totalArea;
				float b1 = area1 / totalArea;
				float b2 = 1.0 - b0 - b1;

				float2 resultUv = uv0 * b0 + uv1 * b1 + uv2 * b2;

				float3 resultPosWs = posWs0 * b0 + posWs1 * b1 + posWs2 * b2;
				float3 resultBindPosWs = bindPosWs0 * b0 + bindPosWs1 * b1 + bindPosWs2 * b2;
				float3 resultBindPosLs = bindPosLs0 * b0 + bindPosLs1 * b1 + bindPosLs2 * b2;

				res.m_foundIndex = -1;
					
				res.m_uv = resultUv;

				res.m_posWs = resultPosWs;
				res.m_bindPosWs = resultBindPosWs;
				res.m_bindPosLs = resultBindPosLs;

				res.m_rtId = GetRtId(pSrt->m_objectIdMappings[mapIndex]);
				res.m_meshId = GetMeshId(pSrt->m_objectIdMappings[mapIndex]);
				res.m_bodyPart = GetBodyPart(pSrt->m_objectIdMappings[mapIndex]);
				res.m_procType = GetProcessType(pSrt->m_objectIdMappings[mapIndex]);
				res.m_baricentrics = float3(b0, b1, b2);

				res.m_fNorm = normalize(cross(posWs1 - posWs0, posWs2 - posWs0));
				uint nx = (127 * (res.m_fNorm.x + 1));
				uint ny = (127 * (res.m_fNorm.y + 1));
				uint nz = (127 * (res.m_fNorm.z + 1));
				res.m_uNorm = (nx) | (ny << 8) | (nz << 16);

				// blood map value
				if (checkBlood)
				{
					int bodyPart = res.m_bodyPart;
					if (allowHead || bodyPart != kLook2BodyPartHead)
					{
						// only allow to spawn where there is blood
						if (res.m_rtId != 255)
						{
							// check particle render target and pick up color
							
							int rtId = res.m_rtId; // ReadFirstLane(findProc.m_rtId);
							float textureBloodMapValue = pSrt->m_pParticleRTs->m_textures[rtId].SampleLevel(pSrt->m_linearSampler, res.m_uv, 0).x;

							if (textureBloodMapValue > bloodThreshold)
							{
								res.m_foundIndex = foundIndex;
							}
						}
					}
				}
				else
				{
					res.m_foundIndex = foundIndex;
				}
			}
		}
	}

	return res;
}

FindProcessResults RecomputeMeshTriangleBaricentricsDivergent(ParticleComputeJobSrt *pSrt, bool doChecks, uint threadId, uint threadMeshId24, uint3 indices, float2 barys)
{
	float bary0 = barys.x;
	float bary1 = barys.y;

	FindProcessResults res = FindProcessResults(0);
	res.m_foundIndex = -1;

	// we have 64 threads running, aand sgpr for position. now lets check if we can find an object based on obj id
	uint numMappings = *pSrt->m_pNumObjIdMappings;
	uint numObjIds = min(kMaxTrackedObjects, numMappings);

	uint numIters = (numObjIds + 63) / 64;

	
	uint meshId;
	uint primitiveId = 0;
	bool isNeo = pSrt->m_isNeoMode;

	if (doChecks)
	{
		meshId = threadMeshId24;
		
	}
	else
	{
		meshId = -1; // special meshId will never succeed
	}

	uint vertex0 = indices.x;
	uint vertex1 = indices.y;
	uint vertex2 = indices.z;

	// at this point some threads might be looking for different obj ids, so we group the by that id
	uint storedMeshId = 0;
	int mesIdIndex = 0;

	ulong exec = __s_read_exec();

	uint processed = 0; // these threads obj ids have not been looked at

	// we go trhough each thread and compact list of obj ids, by storing them into lane registers

	while (true)
	{
		ulong unprocessed_mask = __v_cmp_eq_u32(processed, 0);
		int firstThread = __s_ff1_i32_b64(unprocessed_mask);

		if (firstThread == -1)
			break;

		uint uniform_meshId = ReadLane(meshId, asuint(firstThread));

		if (uniform_meshId == meshId)
		{
			processed = 1;
		}

		WriteLane(storedMeshId, uniform_meshId, asuint(mesIdIndex));

		mesIdIndex += 1;
	}

	// back to 64 threads. and we know we need to do objIdIndex iterations (if all threads landed on same mesh, it is just one iteration)

	for (int iMeshIdIter = 0; iMeshIdIter < mesIdIndex; ++iMeshIdIter)
	{
		int foundIndex = -1;

		uint targetMeshId = ReadLane(storedMeshId, asuint(iMeshIdIter));
		for (int iIter = 0; iIter < numIters; ++iIter)
		{
			int indexToCheck = iIter * 64 + threadId;
			if (indexToCheck < numObjIds)
			{
				if (GetMeshId(pSrt->m_objectIdMappings[indexToCheck]) == targetMeshId)
				{
					foundIndex = indexToCheck;
				}
			}
		}
		
		int firstGoodThread = __s_ff1_i32_b64(__v_cmp_ne_i32(foundIndex, -1));

		if (firstGoodThread != -1)
		{
			// we have one thread that found it. we can now resume all threads that care about this mesh id, and they will lookup their data
			// at this point we still have 64 threads

			foundIndex = ReadLane(foundIndex, asuint(firstGoodThread));

			uint mapIndex = foundIndex;

			// now all threads have the right mapping index

			// now filter threads that care about it

			if (targetMeshId == meshId)
			{
				uint i0 = vertex0;
				uint i1 = vertex1;
				uint i2 = vertex2;

				uint instanceId = 0; // pSrt->m_objectIdMappings[mapIndex].m_instanceId;

				float2 uv0 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i0, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);
				float2 uv1 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i1, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);
				float2 uv2 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i2, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);

				uint indexOffset = 0;

				indexOffset = instanceId * pSrt->m_objectIdMappings[mapIndex].m_numVertices;

				float3 posLs0 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i0 + indexOffset);
				float3 posLs1 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i1 + indexOffset);
				float3 posLs2 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i2 + indexOffset);

				float3 posWs0 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs0, 1.0)).xyz;
				float3 posWs1 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs1, 1.0)).xyz;
				float3 posWs2 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs2, 1.0)).xyz;
				//uv = ReadFirstLane(uv);

				float3 bindPosLs0 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_bindPoseVertexBuffer, i0 + indexOffset);
				float3 bindPosLs1 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_bindPoseVertexBuffer, i1 + indexOffset);
				float3 bindPosLs2 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_bindPoseVertexBuffer, i2 + indexOffset);

				float3 bindPosWs0 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(bindPosLs0, 1.0)).xyz;
				float3 bindPosWs1 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(bindPosLs1, 1.0)).xyz;
				float3 bindPosWs2 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(bindPosLs2, 1.0)).xyz;

				float b0 = bary0;
				float b1 = bary1;
				float b2 = 1.0 - b0 - b1;

				float2 resultUv = uv0 * b0 + uv1 * b1 + uv2 * b2;

				float3 resultPosWs = posWs0 * b0 + posWs1 * b1 + posWs2 * b2;
				float3 resultBindPosWs = bindPosWs0 * b0 + bindPosWs1 * b1 + bindPosWs2 * b2;
				float3 resultBindPosLs = bindPosLs0 * b0 + bindPosLs1 * b1 + bindPosLs2 * b2;

				res.m_uv = resultUv;

				res.m_posWs = resultPosWs;
				res.m_bindPosWs = resultBindPosWs;
				res.m_bindPosLs = resultBindPosLs;

				res.m_rtId = GetRtId(pSrt->m_objectIdMappings[mapIndex]);
				res.m_meshId = GetMeshId(pSrt->m_objectIdMappings[mapIndex]);
				res.m_bodyPart = GetBodyPart(pSrt->m_objectIdMappings[mapIndex]);
				res.m_procType = GetProcessType(pSrt->m_objectIdMappings[mapIndex]);
				res.m_baricentrics = float3(b0, b1, b2);

				res.m_fNorm = normalize(cross(posWs1 - posWs0, posWs2 - posWs0));
				uint nx = (127 * (res.m_fNorm.x + 1));
				uint ny = (127 * (res.m_fNorm.y + 1));
				uint nz = (127 * (res.m_fNorm.z + 1));
				res.m_uNorm = (nx) | (ny << 8) | (nz << 16);


				res.m_foundIndex = foundIndex;
			}
		}
	}

	return res;
}


bool GetHaveUvs(ParticleStateV0 state)
{
	return (state.m_flags1 & kHaveUvsBit) != 0;
}

void SetNoUvs(inout ParticleStateV0 state)
{
	state.m_flags1 = state.m_flags1 & ~kHaveUvsBit;
}

void SetHaveUvs(inout ParticleStateV0 state)
{
	state.m_flags1 = state.m_flags1 | kHaveUvsBit;
}

bool GetHaveSkinning(ParticleStateV0 state)
{
	return (state.m_flags1 & kHaveSkinningBit) != 0;
}

bool GetHaveSkinning(SnowParticleState state)
{
	return (state.m_flags1 & kHaveSkinningBit) != 0;
}

void SetNoSkinning(inout ParticleStateV0 state)
{
	state.m_flags1 = state.m_flags1 & ~kHaveSkinningBit;
}

void SetNoSkinning(inout SnowParticleState state)
{
	state.m_flags1 = state.m_flags1 & ~kHaveSkinningBit;
}

void SetHaveSkinning(inout ParticleStateV0 state)
{
	state.m_flags1 = state.m_flags1 | kHaveSkinningBit;
}

void SetHaveSkinning(inout SnowParticleState state)
{
	state.m_flags1 = state.m_flags1 | kHaveSkinningBit;
}

bool GetStartedAtHead(ParticleStateV0 state)
{
	return (state.m_flags1 & kStartedAtHeadBit) != 0;
}

void SetNotStartedAtHead(inout ParticleStateV0 state)
{
	state.m_flags1 = state.m_flags1 & ~kStartedAtHeadBit;
}

// this one breaks things
void SetStartedAtHead(inout ParticleStateV0 state)
{
	state.m_flags1 = state.m_flags1 | kStartedAtHeadBit;
}

bool GetIsInfected(ParticleStateV0 state)
{
	return (state.m_flags1 & kIsInfected) != 0;
}

void SetNotIsInfected(inout ParticleStateV0 state)
{
	state.m_flags1 = state.m_flags1 & ~kIsInfected;
}

void SetIsInfected(inout ParticleStateV0 state)
{
	state.m_flags1 = state.m_flags1 | kIsInfected;
}

void MarkStateInvalid(inout ParticleStateV0 state)
{
	SetFlags0(state, 100);
}

float3 DecodeBindPosFromRotation(float3 input)
{
	float3 rotation = float3(f16tof32(asuint(input.x)), f16tof32(asuint(input.x) >> 16), f16tof32(asuint(input.y) & 0x0000FFFF));

	return rotation;
}

float3 EncodeRotationFromBindPose(float3 input)
{
	float3 rotation = float3(asfloat(PackFloat2ToUInt(input.x, input.y)), asfloat(f32tof16(input.z)), 0.0f);

	return rotation;
}


float3 EncodeRotationFromBindPose(float3 input, float3 barycentrics, uint meshId24)
{
	uint baryAU8 = uint(barycentrics.x * 255) & 0x00FF;
	uint baryBU8 = uint(barycentrics.y * 255) & 0x00FF;

	float3 rotation = float3(asfloat(PackFloat2ToUInt(input.x, input.y)), asfloat(f32tof16(input.z) | (baryAU8 << 16) | (baryBU8 << 24)), asfloat(meshId24));
	return rotation;
}


uint DecodeMeshId24FromRotation(float3 input)
{
	return asuint(input.z) & 0x00FFFFFF;
}

float2 DecodeBarysFromRotation(float3 input)
{
	float2 barys;
	barys.x = ((asuint(input.y) >> 16) & 0x00FF) / 255.0f;
	barys.y = ((asuint(input.y) >> 24)) / 255.0f;
	return barys;
}


void AddDebugCrossEvent(uint4 vsharp, RWStructuredBuffer<ParticleFeedBackData> particleFeedBackData, float3 pos, float r, float4 c)
{
	#if ENABLE_DEBUG_FEED_BACK
	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	if (prevIndex < kMaxFeedBackEvents)
	{
		// can add new data
		particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebugCross, 0, 0, 0);
		particleFeedBackData[prevIndex].m_data1 = float4(pos, r);
		particleFeedBackData[prevIndex].m_data2 = float4(c);
		particleFeedBackData[prevIndex].m_data3 = float4(0.0f, 0.0f, 0.0f, 0.0f);
	}
	#endif
}


void AddDebugStickyParticleEvent(uint4 vsharp, RWStructuredBuffer<ParticleFeedBackData> particleFeedBackData, float3 pos, float3 prevPos, float r, float4 c, uint m_flags0, uint m_flags1, uint dispatchId, uint stateId, float birthTime)
{
	#if ENABLE_DEBUG_FEED_BACK
	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	if (prevIndex < kMaxFeedBackEvents)
	{
		// can add new data
		particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebugStickyParticle, m_flags0, dispatchId, stateId);
		particleFeedBackData[prevIndex].m_data1 = float4(pos, r);
		particleFeedBackData[prevIndex].m_data2 = float4(c.xyz, birthTime);
		particleFeedBackData[prevIndex].m_data3 = float4(prevPos, asfloat(m_flags1));
	}
	#endif
}

void AddDebugSetSentinelEvent(uint4 vsharp, RWStructuredBuffer<ParticleFeedBackData> particleFeedBackData, float3 pos, float3 prevPos, float r, float4 c, uint m_flags0, uint m_flags1, uint dispatchId, uint stateId, uint newStates)
{
#if ENABLE_DEBUG_FEED_BACK
	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	if (prevIndex < kMaxFeedBackEvents)
	{
		// can add new data
		particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebugSetSentinel, m_flags0, dispatchId, stateId);
		particleFeedBackData[prevIndex].m_data1 = float4(pos, r);
		particleFeedBackData[prevIndex].m_data2 = float4(c.xyz, asfloat(newStates));
		particleFeedBackData[prevIndex].m_data3 = float4(prevPos, asfloat(m_flags1));
	}
#endif
}


#define ParticleFailReason_StencilHorizontalDepth 0
#define ParticleFailReason_AddFail 1
#define ParticleFailReason_TooMuchBindPoseMotion 2
#define ParticleFailReason_MeshIdMismatch 3
#define ParticleFailReason_UnmappedMesh 4
#define ParticleFailReason_BodyPartMismatch 3
#define ParticleFailReason_SpawnFailProcess 6
#define ParticleFailReason_SpawnFailNoBloodBadBodyPart 7
#define ParticleFailReason_BodyPartHeadCheck 8

void AddDebugStickyParticleFailReasonEvent(uint4 vsharp, RWStructuredBuffer<ParticleFeedBackData> particleFeedBackData, float3 pos, float3 prevPos, float r, float4 c, uint m_flags0, uint m_flags1, uint dispatchId, uint stateId, uint failReason)
{
	#if ENABLE_DEBUG_FEED_BACK || ENABLE_DEBUG_FEED_BACK_NEW_DIE
	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	if (prevIndex < kMaxFeedBackEvents)
	{
		// can add new data
		particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebugStickyParticleFailReason, m_flags0, dispatchId, stateId);
		particleFeedBackData[prevIndex].m_data1 = float4(pos, r);
		particleFeedBackData[prevIndex].m_data2 = float4(c.xyz, asfloat(failReason));
		particleFeedBackData[prevIndex].m_data3 = float4(prevPos, asfloat(m_flags1));
	}
	#endif
}

void AddSpawnParticleEvent(uint4 vsharp, RWStructuredBuffer<ParticleFeedBackData> particleFeedBackData, float3 pos, uint id, float3 scale)
{
	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	if (prevIndex < kMaxFeedBackEvents)
	{
		// can add new data
		particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeSpawnParticle, id, 0, 0);
		particleFeedBackData[prevIndex].m_data1 = float4(pos, 0);
		particleFeedBackData[prevIndex].m_data2 = float4(scale, 0);
		particleFeedBackData[prevIndex].m_data3 = float4(0);
	}
}

void AddDebugNewStickyParticleEvent(uint4 vsharp, RWStructuredBuffer<ParticleFeedBackData> particleFeedBackData, float3 pos, float3 prevPos, float r, float4 c, uint m_flags0, uint m_flags1, uint dispatchId, uint stateId, float birthTime)
{
	#if ENABLE_DEBUG_FEED_BACK || ENABLE_DEBUG_FEED_BACK_NEW_DIE
	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	if (prevIndex < kMaxFeedBackEvents)
	{
		// can add new data
		particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebugStickyParticleNew, m_flags0, dispatchId, stateId);
		particleFeedBackData[prevIndex].m_data1 = float4(pos, r);
		particleFeedBackData[prevIndex].m_data2 = float4(c.xyz, birthTime);
		particleFeedBackData[prevIndex].m_data3 = float4(prevPos, asfloat(m_flags1));
	}
	#endif
}

void AddDebugNewStickyBaryDataParticleEvent(uint4 vsharp, RWStructuredBuffer<ParticleFeedBackData> particleFeedBackData, float3 pos, float3 prevPos, uint dispatchId, uint stateId, uint meshId, uint3 indices, float2 barys)
{
#if ENABLE_DEBUG_FEED_BACK || ENABLE_DEBUG_FEED_BACK_NEW_DIE
	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	if (prevIndex < kMaxFeedBackEvents)
	{
		// can add new data
		particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebugStickyParticleNewBaryData, dispatchId, meshId, stateId);
		particleFeedBackData[prevIndex].m_data1 = float4(pos, barys.x);
		particleFeedBackData[prevIndex].m_data2 = float4(prevPos, barys.y);
		particleFeedBackData[prevIndex].m_data3 = float4(asfloat(indices.x), asfloat(indices.y), asfloat(indices.z), 0);
	}
#endif
}

void AddDebugStickyBaryRemapParticleEvent(uint4 vsharp, RWStructuredBuffer<ParticleFeedBackData> particleFeedBackData, float3 pos, float3 prevPos, uint dispatchId, uint stateId, uint meshId, uint3 indices, float2 barys)
{
#if ENABLE_DEBUG_FEED_BACK || ENABLE_DEBUG_FEED_BACK_NEW_DIE
	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	if (prevIndex < kMaxFeedBackEvents)
	{
		// can add new data
		particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebugStickyParticleBaryRemap, dispatchId, meshId, stateId);
		particleFeedBackData[prevIndex].m_data1 = float4(pos, barys.x);
		particleFeedBackData[prevIndex].m_data2 = float4(prevPos, barys.y);
		particleFeedBackData[prevIndex].m_data3 = float4(asfloat(indices.x), asfloat(indices.y), asfloat(indices.z), 0);
	}
#endif
}

void AddDebugStickyParticlePostUpdate(uint4 vsharp, RWStructuredBuffer<ParticleFeedBackData> particleFeedBackData, uint ribbonId, ulong activeMask, ulong uvMask)
{
	#if ENABLE_DEBUG_FEED_BACK || ENABLE_DEBUG_FEED_BACK_NEW_DIE
	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	if (prevIndex < kMaxFeedBackEvents)
	{
		// can add new data
		particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebugStickyParticlePostUpdate, ribbonId, 0, 0);
		particleFeedBackData[prevIndex].m_data1 = float4(asfloat(uint(activeMask >> 32)), asfloat(uint(activeMask)), asfloat(uint(uvMask >> 32)), asfloat(uint(uvMask)));
		particleFeedBackData[prevIndex].m_data2 = float4(0);
		particleFeedBackData[prevIndex].m_data3 = float4(0);
	}
	#endif
}

void AddDebugStickyParticleRecomputed(uint4 vsharp, RWStructuredBuffer<ParticleFeedBackData> particleFeedBackData, float3 pos, float3 prevPos, uint dispatchId, uint stateId, uint meshId, uint3 indices, float2 barys, bool success)
{
	#if ENABLE_DEBUG_FEED_BACK || ENABLE_DEBUG_FEED_BACK_NEW_DIE
	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	if (prevIndex < kMaxFeedBackEvents)
	{
		// can add new data
		particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebugStickyParticleRecomputed, dispatchId, meshId, stateId);
		particleFeedBackData[prevIndex].m_data1 = float4(pos, barys.x);
		particleFeedBackData[prevIndex].m_data2 = float4(prevPos, barys.y);
		particleFeedBackData[prevIndex].m_data3 = float4(asfloat(indices.x), asfloat(indices.y), asfloat(indices.z), success);
	}
	#endif
}

void AddDebugNewRibbonStickyParticleEvent(uint4 vsharp, RWStructuredBuffer<ParticleFeedBackData> particleFeedBackData, float3 pos, float3 prevPos, float r, float4 c, uint m_flags0, uint m_flags1, uint dispatchId, uint stateId, float birthTime)
{
	#if ENABLE_DEBUG_FEED_BACK || ENABLE_DEBUG_FEED_BACK_NEW_DIE
	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	if (prevIndex < kMaxFeedBackEvents)
	{
		// can add new data
		particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebugStickyParticleNewRibbon, m_flags0, dispatchId, stateId);
		particleFeedBackData[prevIndex].m_data1 = float4(pos, r);
		particleFeedBackData[prevIndex].m_data2 = float4(c.xyz, birthTime);
		particleFeedBackData[prevIndex].m_data3 = float4(prevPos, asfloat(m_flags1));
	}
	#endif
}

void AddDebugRibbonStickyParticleTracking(uint4 vsharp, RWStructuredBuffer<ParticleFeedBackData> particleFeedBackData, float3 pos, float3 prevPos, float r, int trackingResult, float depthDifRightAway, float unexpectedOffsetLen)
{
	#if ENABLE_DEBUG_FEED_BACK
	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	if (prevIndex < kMaxFeedBackEvents)
	{
		// can add new data
		particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebugStickyParticleTracking, trackingResult, 0, 0);
		particleFeedBackData[prevIndex].m_data1 = float4(pos, r);
		particleFeedBackData[prevIndex].m_data2 = float4(depthDifRightAway, unexpectedOffsetLen, 0, 0);
		particleFeedBackData[prevIndex].m_data3 = float4(prevPos, 0);
	}
	#endif
}

void AddDebugDieStickyParticleEvent(uint4 vsharp, RWStructuredBuffer<ParticleFeedBackData> particleFeedBackData, float3 pos, float3 prevPos, float r, float4 c, uint m_flags0, uint m_flags1, uint dispatchId, uint stateId, float birthTime)
{
#if ENABLE_DEBUG_FEED_BACK || ENABLE_DEBUG_FEED_BACK_NEW_DIE
	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	if (prevIndex < kMaxFeedBackEvents)
	{
		// can add new data
		particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebugStickyParticleDie, m_flags0, dispatchId, stateId);
		particleFeedBackData[prevIndex].m_data1 = float4(pos, r);
		particleFeedBackData[prevIndex].m_data2 = float4(c.xyz, birthTime);
		particleFeedBackData[prevIndex].m_data3 = float4(prevPos, asfloat(m_flags1));
	}
#endif
}


void AddDebugCopyToOtherStickyParticleEvent(uint4 vsharp, RWStructuredBuffer<ParticleFeedBackData> particleFeedBackData, float3 pos, uint destinationIndex, float r, float4 c, uint m_flags0, uint m_flags1, uint dispatchId, uint stateId)
{
	#if ENABLE_DEBUG_FEED_BACK
	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	if (prevIndex < kMaxFeedBackEvents)
	{
		// can add new data
		particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebugStickyParticleCopyToStatic, m_flags0, dispatchId, stateId);
		particleFeedBackData[prevIndex].m_data1 = float4(pos, r);
		particleFeedBackData[prevIndex].m_data2 = float4(c);
		particleFeedBackData[prevIndex].m_data3 = float4(asfloat(destinationIndex), 0, 0, asfloat(m_flags1));
	}
	#endif
}

void AddDebugKillStickyParticleEventAfterCopy(uint4 vsharp, RWStructuredBuffer<ParticleFeedBackData> particleFeedBackData, float3 pos, uint destinationIndex, float r, float4 c, uint m_flags0, uint m_flags1, uint dispatchId, uint stateId)
{
	#if ENABLE_DEBUG_FEED_BACK
	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	if (prevIndex < kMaxFeedBackEvents)
	{
		// can add new data
		particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebugKillStickyParticleAfterCopy, m_flags0, dispatchId, stateId);
		particleFeedBackData[prevIndex].m_data1 = float4(pos, r);
		particleFeedBackData[prevIndex].m_data2 = float4(c);
		particleFeedBackData[prevIndex].m_data3 = float4(asfloat(destinationIndex), 0, 0, asfloat(m_flags1));
	}
	#endif
}

void AddFeedBackSpawn(ParticleComputeJobSrt *pSrt, uint2 sspos, float3 posWs, uint threadId, uint dispatchId, uint stateId,
	bool bloodMapTrail, float ribbonLength,
	bool bloodSplat, bool bloodDrip, float colorIntensity, float colorOpacity, bool haveCustomUv, float2 customUv)
{
	// we have 64 threads running, aand sgpr for position. now lets check if we can find an object based on obj id
	uint numMappings = *pSrt->m_pNumObjIdMappings;
	uint numObjIds = min(kMaxTrackedObjects, numMappings);

	uint numIters = (numObjIds + 63) / 64;

	int foundIndex = -1;

	uint3 indices = uint3(0,0,0);
	uint objId;
	uint primitiveId = 0;
	bool isNeo = pSrt->m_isNeoMode;

	if (!isNeo)
	{
		uint2 objectAndPrimIds = pSrt->m_pPassDataSrt->m_objectId[sspos];
		indices = ForwardCsModeDefaultKeyGetIndices(objectAndPrimIds);

		uint facing = objectAndPrimIds.x & 0x1;
		objId = objectAndPrimIds.x >> 17;
	}
	else
	{
		uint objectAndPrimIds = pSrt->m_pPassDataSrt->m_objectId[sspos].x;
		primitiveId = objectAndPrimIds & 0xffff;

		// will get these later
		//indices = indexBuffer[primitiveId * 3 + 0];
		//indices = indexBuffer[primitiveId * 3 + 1];
		//indices = indexBuffer[primitiveId * 3 + 2];

		objId = (objectAndPrimIds & 0xfffe0000) >> 17;
	}


	uint vertex0 = indices.x;
	uint vertex1 = indices.y;
	uint vertex2 = indices.z;

	
	for (int iIter = 0; iIter < numIters; ++iIter)
	{
		int indexToCheck = iIter * 64 + threadId;
		if (indexToCheck < numObjIds)
		{


			if (GetObjectId(pSrt->m_objectIdMappings[indexToCheck]) == objId)
			{
				foundIndex = indexToCheck;
			}
		}
	}
	int firstGoodThread = __s_ff1_i32_b64(__v_cmp_ne_i32(foundIndex, -1));

	uint4 vsharp = __get_vsharp(pSrt->m_particleFeedBackHeaderData);

	if (threadId == 0)
	{
		//uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
		//if (prevIndex < kMaxFeedBackEvents)
		//{
		//	// can add new data
		//	pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, objId, firstGoodThread, numMappings /*ribbonId*/);
		//	pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
		//	pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(posWs, 0.0f);
		//	pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(0.0f, 0.0f, 0.0f, 0.0f);
		//}
	}

	//if (threadId == 0 && bloodSplat && foundIndex != -1)
	//{
	//	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	//	if (prevIndex < kMaxFeedBackEvents)
	//	{
	//		// can add new data
	//		pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, objId, firstGoodThread, numMappings /*ribbonId*/);
	//		pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
	//		pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(posWs, 0.0f);
	//		pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(0.0f, 0.0f, 0.0f, 0.0f);
	//	}
	//}


	if (foundIndex != -1)
	{
		// some threads might have succeeded up to here


		{
			// read one and compare to what we found




			{
				// found it
				// now enable just one thread


				uint mapIndex = ReadFirstLane(foundIndex);

				// enabling only one (first) succeeding thread
				if (threadId == ReadFirstLane(threadId))
				{
					//uint triIndex = objId & 0x000000FF; // totally wrong
					//

					uint i0 = vertex0;
					uint i1 = vertex1;
					uint i2 = vertex2;

					if (isNeo)
					{
						i0 = pSrt->m_objectIdMappings[mapIndex].m_indexBuffer[primitiveId * 3 + 0];
						i1 = pSrt->m_objectIdMappings[mapIndex].m_indexBuffer[primitiveId * 3 + 1];
						i2 = pSrt->m_objectIdMappings[mapIndex].m_indexBuffer[primitiveId * 3 + 2];
					}

					uint instanceId = 0; // pSrt->m_objectIdMappings[mapIndex].m_instanceId;

					float2 uv0 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i0, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);
					float2 uv1 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i1, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);
					float2 uv2 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i2, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);

					uint indexOffset = 0;

					indexOffset = instanceId * pSrt->m_objectIdMappings[mapIndex].m_numVertices;

					float3 posLs0 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i0 + indexOffset);
					float3 posLs1 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i1 + indexOffset);
					float3 posLs2 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i2 + indexOffset);

					float3 posWs0 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs0, 1.0)).xyz;
					float3 posWs1 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs1, 1.0)).xyz;
					float3 posWs2 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs2, 1.0)).xyz;
					//uv = ReadFirstLane(uv);

					float3 bindPosLs0 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_bindPoseVertexBuffer, i0 + indexOffset);
					float3 bindPosLs1 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_bindPoseVertexBuffer, i1 + indexOffset);
					float3 bindPosLs2 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_bindPoseVertexBuffer, i2 + indexOffset);

					float3 bindPosWs0 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(bindPosLs0, 1.0)).xyz;
					float3 bindPosWs1 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(bindPosLs1, 1.0)).xyz;
					float3 bindPosWs2 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(bindPosLs2, 1.0)).xyz;




					// calculate barycentric
					float totalArea = length(cross(posWs1 - posWs0, posWs2 - posWs0)) / 2.0;
					float area0 = length(cross(posWs1 - posWs, posWs2 - posWs)) / 2.0;
					float area1 = length(cross(posWs0 - posWs, posWs2 - posWs)) / 2.0;
					float b0 = area0 / totalArea;
					float b1 = area1 / totalArea;
					float b2 = 1.0 - b0 - b1;

					float2 resultUv = uv0 * b0 + uv1 * b1 + uv2 * b2;

					float3 resultPosWs = posWs0 * b0 + posWs1 * b1 + posWs2 * b2;

					float3 resultBindPosWs = bindPosWs0 * b0 + bindPosWs1 * b1 + bindPosWs2 * b2;

					
					if (length(resultPosWs - posWs) < 0.05 || bloodSplat)
					{
						if (bloodMapTrail)
						{
							uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
							if (prevIndex < kMaxFeedBackEvents)
							{
								// can add new data
								pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeAdvanceRibbon, pSrt->m_objectIdMappings[mapIndex].m_processId, GetObjectId(pSrt->m_objectIdMappings[mapIndex]), stateId);
								pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(haveCustomUv ? customUv : resultUv, colorIntensity, colorOpacity);
								pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(length(resultPosWs - posWs), posWs);
								pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(asfloat(dispatchId), asfloat(pSrt->m_frameNumber), /* b0, b1,*/ ribbonLength, b2);
							}
							
							// only one thread active at this point. we are adding one event, not 64
							AddDebugCrossEvent(vsharp, pSrt->m_particleFeedBackData, resultBindPosWs, 0.1f, float4(0.0f, 0.0f, 1.0f, 1.0f));
						
						}
						if (bloodSplat)
						{
							uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
							if (prevIndex < kMaxFeedBackEvents)
							{
								// can add new data
								pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeSpawnBloodDecal, pSrt->m_objectIdMappings[mapIndex].m_processId, GetObjectId(pSrt->m_objectIdMappings[mapIndex]), stateId);
								pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(resultUv, colorIntensity, colorOpacity);
								pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(2.0f, resultPosWs - posWs);
								pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(3.0f, posWs);

							}
						}

						if (bloodDrip && pSrt->m_gdsOffsetOther)
						{
							// write into the special list of blood ribbon spawner

							uint gdsOffsetNew = pSrt->m_gdsOffsetOther;
							uint size, stride;
							pSrt->m_particleStatesOther.GetDimensions(size, stride);


							uint newInfoIndex = NdAtomicIncrement(gdsOffsetNew);
							if (newInfoIndex >= size)
							{
								// decrement back
								NdAtomicDecrement(gdsOffsetNew);

								// can't add new information
							}
							else
							{
								RWStructuredBuffer<RibbonSpawnHint> destHintBuffer = __create_buffer<RWStructuredBuffer<RibbonSpawnHint> >(__get_vsharp(pSrt->m_particleStatesOther));

								RibbonSpawnHint infoState = RibbonSpawnHint(0);

								infoState.m_pos = resultPosWs;
								//infoState.m_scale = state.m_scale;

								destHintBuffer[newInfoIndex] = infoState;
							}
						}
					}
				}
			}
		}

	}
}


void AddBakedFeedBackSpawn(ParticleComputeJobSrt *pSrt, uint2 sspos, float3 posWs, uint threadId, uint dispatchId, uint stateId,
	float ribbonLength, float colorIntensity, float colorOpacity, float ribbonScaleX, float2 customUv, uint3 indices, float bary0, float bary1, bool haveIndices, uint meshId24)
{
	// we have 64 threads running, aand sgpr for position. now lets check if we can find an object based on obj id
	uint numMappings = *pSrt->m_pNumObjIdMappings;
	uint numObjIds = min(kMaxTrackedObjects, numMappings);

	uint numIters = (numObjIds + 63) / 64;

	int foundIndex = -1;

	uint objId;
	uint primitiveId = 0;

	uint vertex0 = indices.x;
	uint vertex1 = indices.y;
	uint vertex2 = indices.z;


	for (int iIter = 0; iIter < numIters; ++iIter)
	{
		int indexToCheck = iIter * 64 + threadId;
		if (indexToCheck < numObjIds)
		{


			if (GetMeshId(pSrt->m_objectIdMappings[indexToCheck]) == meshId24)
			{
				foundIndex = indexToCheck;
			}
		}
	}
	int firstGoodThread = __s_ff1_i32_b64(__v_cmp_ne_i32(foundIndex, -1));

	uint4 vsharp = __get_vsharp(pSrt->m_particleFeedBackHeaderData);


	if (foundIndex != -1)
	{
		// some threads might have succeeded up to here


		{
			// read one and compare to what we found




			{
				// found it
				// now enable just one thread


				uint mapIndex = ReadFirstLane(foundIndex);

				// enabling only one (first) succeeding thread
				if (threadId == ReadFirstLane(threadId))
				{
					//uint triIndex = objId & 0x000000FF; // totally wrong
					//
					
					uint i0 = vertex0;
					uint i1 = vertex1;
					uint i2 = vertex2;

					uint instanceId = 0; // pSrt->m_objectIdMappings[mapIndex].m_instanceId;

					float2 uv0 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i0, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);
					float2 uv1 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i1, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);
					float2 uv2 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i2, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);

					uint indexOffset = 0;

					indexOffset = instanceId * pSrt->m_objectIdMappings[mapIndex].m_numVertices;

					float3 posLs0 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i0 + indexOffset);
					float3 posLs1 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i1 + indexOffset);
					float3 posLs2 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i2 + indexOffset);

					float3 posWs0 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs0, 1.0)).xyz;
					float3 posWs1 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs1, 1.0)).xyz;
					float3 posWs2 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs2, 1.0)).xyz;
					//uv = ReadFirstLane(uv);

					float3 bindPosLs0 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_bindPoseVertexBuffer, i0 + indexOffset);
					float3 bindPosLs1 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_bindPoseVertexBuffer, i1 + indexOffset);
					float3 bindPosLs2 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_bindPoseVertexBuffer, i2 + indexOffset);

					float3 bindPosWs0 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(bindPosLs0, 1.0)).xyz;
					float3 bindPosWs1 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(bindPosLs1, 1.0)).xyz;
					float3 bindPosWs2 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(bindPosLs2, 1.0)).xyz;

					float b0 = bary0;
					float b1 = bary1;
					float b2 = 1.0 - b0 - b1;

					
					float3 resultPosWs = posWs0 * b0 + posWs1 * b1 + posWs2 * b2;

					float3 resultBindPosWs = bindPosWs0 * b0 + bindPosWs1 * b1 + bindPosWs2 * b2;
					
					float2 resultUv = uv0 * b0 + uv1 * b1 + uv2 * b2;


					//if (length(resultPosWs - posWs) < 0.05 || bloodSplat)
					{
						//if (bloodMapTrail)
						{
							uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
							if (prevIndex < kMaxFeedBackEvents)
							{
								// can add new data
								pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeAdvanceRibbon, pSrt->m_objectIdMappings[mapIndex].m_processId, GetObjectId(pSrt->m_objectIdMappings[mapIndex]), stateId);
								pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(haveIndices ? resultUv : customUv, colorIntensity, colorOpacity);
								pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(ribbonScaleX, 0, 0, 0);
								pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(asfloat(dispatchId), asfloat(pSrt->m_frameNumber), /* b0, b1,*/ ribbonLength, 0);
							}
						}
					}
				}
			}
		}

	}
}

void AddCollisionFeedBackSpawn(ParticleComputeJobSrt *pSrt, uint2 sspos, float3 posWs, float3 normal, uint dispatchIdX, uint threadId, bool spawnDecal, bool spawnBloodMap)
{
	// we have 64 threads running, aand sgpr for position. now lets check if we can find an object based on obj id
	uint numMappings = *pSrt->m_pNumParticleMappings;
	uint numParticles = min(kMaxTrackedObjects, numMappings);

	uint numIters = (numParticles + 63) / 64;

	int foundIndex = -1;
	

	//for (int iIter = 0; iIter < numIters; ++iIter)
	//{
	//	int indexToCheck = iIter * 64 + threadId;
	//	if (indexToCheck < numObjIds)
	//	{
	//
	//
	//		if (pSrt->m_objectIdMappings[indexToCheck].m_objectId == objId)
	//		{
	//			foundIndex = indexToCheck;
	//		}
	//	}
	//}
	//int firstGoodThread = __s_ff1_i32_b64(__v_cmp_ne_i32(foundIndex, -1));

	uint4 vsharp = __get_vsharp(pSrt->m_particleFeedBackHeaderData);
	
	// in this implementation a thread processes everything so we don't need to check threadId
	//if (threadId == 0)


	//{
	//	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	//	if (prevIndex < kMaxFeedBackEvents)
	//	{
	//		// can add new data
	//		pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, 0, 0, 0 /*ribbonId*/);
	//		pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(0.0f, 0.0f, 0.0f, 0.0f);
	//		pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(posWs, 0.0f);
	//		pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(0.0f, 0.0f, 0.0f, 0.0f);
	//	}
	//}
	
	// bad brute force way for testing

	float minDist = 100000000.0f;

	if (spawnDecal)
	{
		for (int i = 0; i < numParticles; ++i)
		{
			ParticleReferenceData ref = pSrt->m_particleMappings[i];

			float3 partPos = ref.m_posWs;

			float kCloseDist = 0.25;

			float3 difV = partPos - posWs;
			float d2 = dot(difV, difV);

			if (d2 < minDist && d2 < kCloseDist * kCloseDist)
			{
				foundIndex = i;
				minDist = d2;
			}
		}

		if (foundIndex == -1)
		{
			// create feedback event

			uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
			if (prevIndex < kMaxFeedBackEvents)
			{
				// can add new data
				pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeSpawnBloodProjectedDecal, dispatchIdX, 0, 0);
				pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(posWs, 0.0f);
				pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(2.0f, 0.0f, 0.0f, 0.0f);
				pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(3.0f, 0.0f, 0.0f, 0.0f);
			}
		}
		else
		{
			// found particle to feed into

			uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
			if (prevIndex < kMaxFeedBackEvents)
			{
				// can add new data
				pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeGrowBloodProjectedDecal, dispatchIdX, 0, 0);

				uint4 h = pSrt->m_particleMappings[foundIndex].m_handleData;

				pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(asfloat(h.x), asfloat(h.y), asfloat(h.z), asfloat(h.w)); // encode handle
				pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(2.0f, 0.0f, 0.0f, 0.0f);
				pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(3.0f, 0.0f, 0.0f, 0.0f);
			}
		}
	}

	if (spawnBloodMap)
	{
		// look for fg mapped objects
		AddFeedBackSpawn(pSrt, sspos, posWs, threadId, dispatchIdX,  /*ribbon id not used for splat*/0, false, 0, true, /*bloodDrip*/ false, /*colorIntensity=*/1.0, /*colorOpacity=*/1.0, false, float2(0));

	}

	

	//if (foundIndex != -1)
	//{
	//	// some threads might have succeeded up to here
	//
	//
	//	{
	//		// read one and compare to what we found
	//
	//
	//
	//
	//		{
	//			// found it
	//			// now enable just one thread
	//
	//
	//			uint mapIndex = ReadFirstLane(foundIndex);
	//
	//			// enabling only one (first) succeeding thread
	//			if (threadId == ReadFirstLane(threadId))
	//			{
	//				//uint triIndex = objId & 0x000000FF; // totally wrong
	//				//
	//				//uint i0 = pSrt->m_objectIdMappings[mapIndex].m_indexBuffer[triIndex * 3 + 0];
	//				//uint i1 = pSrt->m_objectIdMappings[mapIndex].m_indexBuffer[triIndex * 3 + 1];
	//				//uint i2 = pSrt->m_objectIdMappings[mapIndex].m_indexBuffer[triIndex * 3 + 2];
	//
	//				uint i0 = vertex0;
	//				uint i1 = vertex1;
	//				uint i2 = vertex2;
	//
	//				uint instanceId = pSrt->m_objectIdMappings[mapIndex].m_instanceId;
	//
	//				float2 uv0 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i0, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);
	//				float2 uv1 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i1, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);
	//				float2 uv2 = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_texCoordBuffer, i2, instanceId, pSrt->m_objectIdMappings[mapIndex].m_numVertices);
	//
	//				uint indexOffset = 0;
	//
	//				indexOffset = instanceId * pSrt->m_objectIdMappings[mapIndex].m_numVertices;
	//
	//				float3 posLs0 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i0 + indexOffset);
	//				float3 posLs1 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i1 + indexOffset);
	//				float3 posLs2 = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_objectIdMappings[mapIndex].m_vertexBuffer, i2 + indexOffset);
	//
	//				float3 posWs0 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs0, 1.0)).xyz;
	//				float3 posWs1 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs1, 1.0)).xyz;
	//				float3 posWs2 = mul(pSrt->m_objectIdMappings[mapIndex].m_objToWorld, float4(posLs2, 1.0)).xyz;
	//				//uv = ReadFirstLane(uv);
	//
	//
	//
	//
	//				// calculate barycentric
	//				float totalArea = length(cross(posWs1 - posWs0, posWs2 - posWs0)) / 2.0;
	//				float area0 = length(cross(posWs1 - posWs, posWs2 - posWs)) / 2.0;
	//				float area1 = length(cross(posWs0 - posWs, posWs2 - posWs)) / 2.0;
	//				float b0 = area0 / totalArea;
	//				float b1 = area1 / totalArea;
	//				float b2 = 1.0 - b0 - b1;
	//
	//				float2 resultUv = uv0 * b0 + uv1 * b1 + uv2 * b2;
	//
	//				if (length(posWs0 - posWs) < 0.05)
	//				{
	//					uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	//					if (prevIndex < kMaxFeedBackEvents)
	//					{
	//						// can add new data
	//						pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeSpawnBloodDecal, pSrt->m_objectIdMappings[mapIndex].m_processId, pSrt->m_objectIdMappings[mapIndex].m_objectId, ribbonId_partId);
	//						pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(resultUv, 0.0f, 0.0f);
	//						pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(2.0f, posWs0 - posWs);
	//						pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(3.0f, b0, b1, b2);
	//
	//					}
	//
	//					prevIndex = __buffer_atomic_add(1, uint2(0, 0), vsharp, 0, 0);
	//					if (prevIndex < kMaxFeedBackEvents)
	//					{
	//						// can add new data
	//						pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeAdvanceRibbon, pSrt->m_objectIdMappings[mapIndex].m_processId, pSrt->m_objectIdMappings[mapIndex].m_objectId, ribbonId_partId);
	//						pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(resultUv, 0.0, 0.0f);
	//						pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(2.0f, posWs0 - posWs);
	//						pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(3.0f, b0, b1, b2);
	//
	//					}
	//				}
	//			}
	//		}
	//	}
	//
	//}
}



[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeWaterTrackerSpawnNew(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{

	if (pSrt->m_delta < 0.00001)
	{
	//	return;
	}

	//Texture2D<uint> stencilTextureToUse = pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil; //  
	Texture2D<uint> stencilTextureToUse = pSrt->m_pPassDataSrt->m_primaryStencil; //  pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil;

	Texture2D<float> depthBufferToUse = pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture;
	//Texture2D<float> depthBufferToUse = pSrt->m_pPassDataSrt->m_primaryDepthTexture;

	uint myCellIndex = (pSrt->m_screenResolution.x / pSrt->m_gridCellW) * groupId.y + groupId.x;

	float probability = pSrt->m_dataBuffer[myCellIndex].m_data0.x;

	int kNumTries = 2;

	// check if match the texture
	// each group is ran per screen space quad

	uint2 myCellPos = groupId * uint2(pSrt->m_gridCellW, pSrt->m_gridCellH);

	// do 64 random samples in the cell to generate data about this cell. then use this data for spawning splashes in this cell
	float rand0 = GetRandom(pSrt->m_gameTicks/* + float(bit_cast<uint>(pSrt))*/, dispatchId.x, 0);  // float((pSrt->m_gameTicks * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);
	float rand1 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 1);  // float(((pSrt->m_gameTicks ^ 0x735fac31) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);
	float rand2 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 2);  // float(((pSrt->m_gameTicks ^ 0x735fac31) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);



	//probability = ProbabilityBasedOnDistance(depthVs) * 10 * pSrt->m_rootComputeVec1.x;

	//float rand2 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 2);  // float(((pSrt->m_gameTicks ^ 0xac31735f) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);

	//if (groupThreadId.x + rand2 >= probability * 64.0f)
	//{
	//	// decided not to add
	//	return;
	//}

	if (groupThreadId.x > 0)
	{
		//	return;
	}

	int resultIndex = groupThreadId.x;

	//_SCE_BREAK();
	uint threadSucceess = (pSrt->m_particleStates[resultIndex].m_id == 0) && (resultIndex < 384); // find empty state



	// we use original aprticle to check whether we want to be spawning things
	uint origParticleIndex = pSrt->m_particleIndicesOrig[0];
	ParticleInstance originalParticle = pSrt->m_particleInstancesOrig[origParticleIndex];
	originalParticle.world[3].xyz = originalParticle.world[3].xyz + pSrt->m_altWorldOrigin;

	float3 randPosWs = mul(float4(rand0 - 0.5, rand1 - 0.5, rand2 - 0.5, 1.0), originalParticle.world).xyz;

	float4 randPosH = mul(float4(randPosWs, 1), pSrt->m_pPassDataSrt->g_mVP);
	float3 randPosNdc = randPosH.xyz / randPosH.w;

	//int threadSucceess = 1;
	//
	//if (abs(hintPosNdc.x) > 1.0 || abs(hintPosNdc.x) > 1.0)
	//{
	//	//threadSucceess = 0;
	//}

	// get screen space position of the hint
	//uint2 hintsspos = uint2((hintPosNdc.x * 0.5 + 0.5) * pSrt->m_screenResolution.x, (1.0 - (hintPosNdc.y * 0.5 + 0.5)) * pSrt->m_screenResolution.y);
	uint2 sspos = uint2(rand0 * pSrt->m_screenResolution.x, rand1 * pSrt->m_screenResolution.y);  // NdcToScreenSpacePos(pSrt, randPosNdc.xy);

	float3 posNdc = ScreenPosToNdc(pSrt, sspos, depthBufferToUse[sspos] /*m_primaryDepthTexture pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos]*/);

	float4 posH = mul(float4(posNdc, 1), pSrt->m_pPassDataSrt->m_mAltVPInv);
	float3 posWs = posH.xyz / posH.w;
	posWs += pSrt->m_altWorldOrigin;


	// build inverse of the particle matrix

	//	float3x3	g_identity = { { 1, 0, 0 },{ 0, 1, 0 },{ 0, 0, 1 } };
	//	float3 kUnitYAxis = { 0.0f, 1.0f, 0.0f };
	//	float3x3 partInv = g_identity;
	//
	//	float3 posWsParticleSpace = posWs - originalParticle.world[3].xyz;
	//
	//	// modify position based on camera
	//	//partInv[0].xyz = originalParticle.world[0].xyz * originalParticle.invscale.x * 0.5f;
	//	//partInv[1].xyz = originalParticle.world[1].xyz * originalParticle.invscale.y * 0.5f;
	//	//partInv[2].xyz = originalParticle.world[2].xyz * originalParticle.invscale.z * 0.5f;
	//
	//	float scaleX = 2.0f / originalParticle.invscale.x;
	//	float scaleY = 2.0f / originalParticle.invscale.y;
	//	float scaleZ = 2.0f / originalParticle.invscale.z;
	//	partInv[0].xyz = originalParticle.world[0].xyz / scaleX / scaleX;
	//	partInv[1].xyz = originalParticle.world[1].xyz / scaleY / scaleY;// *originalParticle.invscale.y * 0.5f;
	//	partInv[2].xyz = originalParticle.world[2].xyz / scaleZ / scaleZ;// *originalParticle.invscale.z * 0.5f;






	uint stencil = stencilTextureToUse[sspos]; // pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil[sspos];

	if ((stencil & 0x20) && (pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil[sspos] & 0x2)) // check fg stencil bit && water bit
	{
		//return; // don't allow on fg
	}
	else
	{
		threadSucceess = 0;
		//return; // dont allow of bg
	}

	uint gdsOffsetId = pSrt->m_gdsOffsetIdCounter;
	uint particleId = uint(NdAtomicIncrement(gdsOffsetId)); // unique identifier for this particle that will be used in spawner data for ribbons
	
	if (groupThreadId.x > 0)
	{
		//threadSucceess = 0;
	}

	
	FindProcessResults findProc = FindProcessMeshTriangleBaricentricsDivergent(pSrt, threadSucceess, sspos, posWs, groupThreadId.x, /*checkBlood*/false, 0, /*allowHead*/true);
	
	if (findProc.m_foundIndex != -1)
	{
		ParticleStateV0 addedState;
		
		StickyParticleParams spawnParams = DefaultStickyParticleParams();
		spawnParams.allowSkin = true;

		bool spawnedNew = NewStickyParticle(kMotionStateAttached, sspos, posNdc, posWs, 0, stencil, pSrt, dispatchId, particleId,
		/*checkInsideFirstParticle=*/ false, /*birthTimeOffset=*/ 0.001f, /*spawnData*/ rand1 * 3.1415 * 8, /*useProvidedIndex*/ true,
		/*providedParticleIndex=*/ resultIndex, groupThreadId.x, /*lifetime=*/ asfloat(f32tof16(5.0)), addedState, /*normalHalfWayWithRain=*/ false,
		/*inheritedVelocity=*/ float3(0, 0, 0), /*isSnow*/ false, spawnParams, /*defaultSpeed*/ 0.0f, resultIndex, /*customSize*/ 0);
		//NewStickyParticle(kMotionStateFreeFallAttach, sspos, posNdc, posWs, 0, stencil, pSrt, dispatchId, particleId, /*checkInsideFirstParticle=*/ false, /*birthTimeOffset=*/ 0.001f, /*spawnData*/ -0.5f, /*useProvidedIndex*/ false, /*providedParticleIndex=*/ -1, groupThreadId.x, /*lifetime=*/ 10.0, /*defaultSpeed*/ 1.0f, /*customSize*/ 0);

		if (spawnedNew)
		{
		#if TRACK_BIND_POSE
			SetRenderState(pSrt->m_particleStates[resultIndex], kRenderStateUnderWater);

			pSrt->m_particleStates[resultIndex].m_rotation = EncodeRotationFromBindPose(findProc.m_bindPosLs, findProc.m_baricentrics, findProc.m_meshId);

			#if TRACK_INDICES
			pSrt->m_particleStates[resultIndex].m_speed = asfloat((findProc.m_indices.x & 0x0000FFFF) | (findProc.m_indices.y << 16));
			pSrt->m_particleStates[resultIndex].m_lifeTime = asfloat(asuint(addedState.m_lifeTime) | (findProc.m_indices.z << 16));
			#endif

			AddDebugNewRibbonStickyParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, addedState.m_pos, posWs, 0.01f, float4(findProc.m_bindPosLs, 1.0f), addedState.m_flags0, addedState.m_flags1, dispatchId.x, addedState.m_id, addedState.m_birthTime);

			AddDebugNewStickyBaryDataParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, findProc.m_posWs, addedState.m_pos, dispatchId.x, addedState.m_id, findProc.m_meshId, findProc.m_indices, findProc.m_baricentrics.xy);
		#endif
		}
	}
}

struct StickyUpdateRes
{
	bool m_haveCollision;
	uint m_isFg;

	float3 m_pos;
	float3 m_normal;
	uint2 m_sspos;
	int m_newParticleIndex;
};

StickyUpdateRes DefaultStickyUpdateRes()
{
	StickyUpdateRes res = StickyUpdateRes(0);
	res.m_newParticleIndex = -1;

	return res;
}

StickyUpdateRes UpdateStickyParticleMotion(
	uint needsUpdate,
	const uint2 dispatchId,
	const uint3 groupThreadId,
	const uint2 groupId,
	ParticleComputeJobSrt *pSrt,
	inout SnowParticleState state,
	inout uint particleErrorState,
	uint particleIndex,
	uint prevStencil,
	uint motionState,
	inout uint frameCounter,
	const uint kNumOfFramesToRecover,
	float gravity,
	float turbulence,
	float terminalVelocity,
	bool isSnow,
	StickyParticleParams params
	)
{

	Texture2D<uint> stencilTextureToUse = params.opaqueAlphaDepthStencil ? pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil : pSrt->m_pPassDataSrt->m_primaryStencil;
	Texture2D<float> depthBufferToUse = params.opaqueAlphaDepthStencil ? pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture : pSrt->m_pPassDataSrt->m_primaryDepthTexture;

	Texture2D<float> prevFrameDepthBufferToUse = params.opaqueAlphaDepthStencil ? pSrt->m_pPassDataSrt->m_lastFrameOpaquePlusAlphaDepthTexture : pSrt->m_pPassDataSrt->m_lastFramePrimaryDepth;

	StickyUpdateRes res = StickyUpdateRes(0);
	res.m_newParticleIndex = -1;
	
	float age = (pSrt->m_time - state.m_birthTime);
	float rand1 = GetRandom(pSrt->m_gameTicks*0.01, dispatchId.x, 1); 
	float rand2 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 2);

	#if TRACK_INDICES
		float lifeTime = f16tof32(asuint(state.m_lifeTime));
	#else
		float lifeTime = state.m_lifeTime;
	#endif

	

	uint needsSkinningUpdate = needsUpdate && ((motionState == kMotionStateAttached) && !(age > lifeTime) && state.m_flags0 == 0) ? 1 : 0 && GetHaveSkinning(state);
	uint positionComputed = false;
	
	FindProcessResults findProcOrig = 0;

	uint3 indices = uint3(0);
	#if TRACK_INDICES
	indices.x = asuint(state.m_speed) & 0x0000FFFF;
	indices.y = asuint(state.m_speed) >> 16;
	indices.z = asuint(state.m_lifeTime) >> 16;
	uint meshId24 = DecodeMeshId24FromRotation(state.m_rotation);
	float2 barys = DecodeBarysFromRotation(state.m_rotation);

	{
		findProcOrig = RecomputeMeshTriangleBaricentricsDivergent(pSrt, needsSkinningUpdate, groupThreadId.x, meshId24, indices, barys);
		positionComputed = findProcOrig.m_foundIndex != -1;


		if (positionComputed)
		{
			if (length(findProcOrig.m_posWs - state.m_pos) > 3.0)
			{
				_SCE_BREAK();
			}

			AddDebugStickyParticleRecomputed(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, findProcOrig.m_posWs, state.m_pos, dispatchId.x, state.m_id, meshId24, indices, barys, true);
		}

		//positionComputed = false; // turn off while debugging
	}
	#endif

	uint needsFallingUpdate = needsUpdate && ((motionState == kMotionStateFreeFallAttach || motionState == kMotionStateDeadFreeFall || motionState == kMotionStateFreeFallCollide)
		&& !(age > lifeTime)) ? 1 : 0;


	
	uint2 sspos = uint2(0, 0);
	uint newParticleIndex = -1;

	if (needsFallingUpdate)
	{
		// flying downwards
		float terminalVelocty = -terminalVelocity;

		/*
		{
			float3 samplePos = (position * fs.freq) + fs.phase.xyz;
			float3 direction = Tex2DAs3D(pSrt->m_materialTexture0).SampleLevel(SetWrapSampleMode(pSrt->m_linearSampler), samplePos.xyz, 0).xyz;	
			float3 noiseBias = float3(-0.225,-0.235,-0.225); //removal of average bias in each axis
			direction += noiseBias;
		}
		*/

		
		float3 velocity = 0;

		if (motionState == kMotionStateFreeFallAttach)
		{
			// instantenous + m_scale=m_direction stores initial inherited velocity
			float cul = (state.m_data * 3.1415 * 8 + age) * pSrt->m_rootComputeVec3.z * turbulence * pSrt->m_delta;

			velocity.x =  pSrt->m_delta * sin(cul) * pSrt->m_rootComputeVec3.y + pSrt->m_rootComputeVec2.x;
			velocity.y =  -1 * abs(gravity * pSrt->m_rootComputeVec3.x * pSrt->m_delta + pSrt->m_rootComputeVec2.y);//-1 * abs(gravity) * pSrt->m_delta ;
			velocity.z =  pSrt->m_delta * cos(cul) * pSrt->m_rootComputeVec3.y + pSrt->m_rootComputeVec2.z;

			float3 inheritedVelocity;
			inheritedVelocity.x = f16tof32(asuint(state.m_direction.x));
			inheritedVelocity.y = f16tof32(asuint(state.m_direction.x) >> 16);
			inheritedVelocity.z = f16tof32(asuint(state.m_direction.y));

			velocity += inheritedVelocity;
		}
		else
		{
			// accumulated velocity
			state.m_direction.y = max(terminalVelocty, state.m_direction.y - gravity * pSrt->m_delta);
			velocity = state.m_direction;
		}

		//velocity is stored in top f16s
		state.m_direction.y = asfloat((asuint(state.m_direction.y) & 0x0000FFFF) | (f32tof16(velocity.x) << 16));
		state.m_direction.z = asfloat(f32tof16(velocity.y) | (f32tof16(velocity.z) << 16));


		// in case of death fall, just fall down
		//float3 dirVector = motionState == kMotionStateFreeFallAttach ? normalize(float3(0.2, -0.75, 0.2)) : normalize(float3(0.0, -1.0, 0.0));

		//if (motionState == kMotionStateFreeFallAttach)
		//{
		//	dirVector += state.m_scale;
		//
		//	dirVector.x += sin(state.m_data * 0.2) * 0.5;
		//	dirVector.z += cos(state.m_data * 0.2) * 0.5;
		//}
		float3 startOfFramePos = state.m_pos;

		//state.m_pos = state.m_pos + state.m_scale * state.m_speed * pSrt->m_delta; //  we encode direction in scale
		state.m_pos = state.m_pos + velocity * pSrt->m_delta; //  we encode direction in scale
		//state.m_data = state.m_data + pSrt->m_delta;
		//state.m_pos.y -= 0.05f;

		float3 endOfFramePos = state.m_pos;

		// now we need to check if we started colliding with geo. if so, we need to change state
		float4 newPosH = mul(float4(state.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 newPosNdc = newPosH.xyz / newPosH.w;

		sspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);
		//uint2 sspos = uint2((newPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (newPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);


		float expectedDepthLinear = GetLinearDepth(newPosNdc.z, pSrt->m_depthParams);
		float depthLinear = GetLinearDepth(depthBufferToUse[sspos], pSrt->m_depthParams);

		bool outOfBounds = false;

		if (abs(newPosNdc.x) > 1.0 || abs(newPosNdc.y) > 1.0 || newPosNdc.z < 0 || newPosNdc.z > 1)
		//if (sspos.x < 0 || sspos.y < 0 || sspos.x >= pSrt->m_screenResolution.x || sspos.y >= pSrt->m_screenResolution.y)
		{
			// new position is outside of view area. 

			depthLinear = 0.0;
			expectedDepthLinear = 10000.0;
			outOfBounds = true; // will not trigger collision events
			//float3x3	g_identity = { { 1, 0, 0 },{ 0, 1, 0 },{ 0, 0, 1 } };
			//float3 kUnitYAxis = { 0.0f, 1.0f, 0.0f };
		}

		if (motionState == kMotionStateDeadFreeFall)
		{
			// let it pass through
		//	depthLinear = 100000.0;
		//	expectedDepthLinear = 10000.0;
		}
		if (true)
		{
			float3x3	g_identity = { { 1, 0, 0 },{ 0, 1, 0 },{ 0, 0, 1 } };
			float3 kUnitYAxis = { 0.0f, 1.0f, 0.0f };

			// we use original aprticle to check whether we want to be spawning things
			uint origParticleIndex = pSrt->m_particleIndicesOrig[0];
			ParticleInstance originalParticle = pSrt->m_particleInstancesOrig[origParticleIndex];
			originalParticle.world[3].xyz = originalParticle.world[3].xyz + pSrt->m_altWorldOrigin.xyz;


			// build inverse of the particle matrix
			float3x3 partInv = g_identity;

			//inst.world[0].xyz = inst.world[0].xyz * scale.x;
			//inst.world[1].xyz = inst.world[1].xyz * scale.y;
			//inst.world[2].xyz = inst.world[2].xyz * scale.z;

			//inst.invscale = float4(1.0f / (scale.x * 0.5), 1.0f / (scale.y * 0.5), 1.0f / (scale.z * 0.5), 1.0f / 1);

			//float4 posOffest = mul(float4(pSrt->m_renderOffset.xyz, 1), inst.world);

			float3 posWsParticleSpace = state.m_pos - originalParticle.world[3].xyz;

			// modify position based on camera
			//partInv[0].xyz = originalParticle.world[0].xyz * originalParticle.invscale.x * 0.5f;
			//partInv[1].xyz = originalParticle.world[1].xyz * originalParticle.invscale.y * 0.5f;
			//partInv[2].xyz = originalParticle.world[2].xyz * originalParticle.invscale.z * 0.5f;

			float scaleX = 2.0f / originalParticle.invscale.x;
			float scaleY = 2.0f / originalParticle.invscale.y;
			float scaleZ = 2.0f / originalParticle.invscale.z;
			partInv[0].xyz = originalParticle.world[0].xyz / scaleX / scaleX;
			partInv[1].xyz = originalParticle.world[1].xyz / scaleY / scaleY;// *originalParticle.invscale.y * 0.5f;
			partInv[2].xyz = originalParticle.world[2].xyz / scaleZ / scaleZ;// *originalParticle.invscale.z * 0.5f;


			
			// put current position in the space of the original particle
			posWsParticleSpace = mul(partInv, posWsParticleSpace).xyz;

			if (posWsParticleSpace.x > 0.5 || posWsParticleSpace.x < -0.5 ||  posWsParticleSpace.y > 0.5 || posWsParticleSpace.y < -0.5 || posWsParticleSpace.z > 0.5 || posWsParticleSpace.z < -0.5)
			{
				depthLinear = 0;
				expectedDepthLinear = 10000;
				outOfBounds = true; // will not trigger collision events
				particleErrorState = kNumOfFramesToRecover;
			}
		}

		float amountBehindGeometry = (expectedDepthLinear - depthLinear); // if > 0 then we are behind geo

		//if (expectedDepthLinear - depthLinear > 0.5)
		//{
		//	// we are behind some geo. can die now
		//	particleErrorState = 100; // kNumOfFramesToRecover;
		//}
		//
		//if (motionState == kMotionStateDeadFreeFall && (expectedDepthLinear - depthLinear > 0.1))
		//{
		//	// we are behind some geo. can die now
		//	
		//	
		//	particleErrorState = 100; // kNumOfFramesToRecover;
		//}
		//
		//

		float depthBufferError = GetLinearDepthError(newPosNdc.z, pSrt->m_depthParams);

		float kCollisionAllowedDepthDif = 0.1f; // if particle becomes behind geo, but only by 10 cm, the partile was cose enough to the surface of the geometry

		if (!outOfBounds && (motionState == kMotionStateFreeFallAttach || motionState == kMotionStateFreeFallCollide) 
			// collision bounds check
			&& amountBehindGeometry >= 0.00f + depthBufferError /* && amountBehindGeometry <= kCollisionAllowedDepthDif + depthBufferError*/)
		{
			// first we check if we got behind geometry/
			// then we will try finding first time we got behind geometry and do a more robust check

			// recalculate new position
			uint newPartStencil = stencilTextureToUse[sspos];
		
			bool fgStencil = newPartStencil & 0x20;
			
			newPosNdc.z = depthBufferToUse[sspos];
			newPosH = mul(float4(newPosNdc, 1), pSrt->m_pPassDataSrt->m_mAltVPInv);
			float3 newPosWs = newPosH.xyz / newPosH.w;
			newPosWs += pSrt->m_altWorldOrigin;



			// try to find a better position by doing binary search

			float t = 0.5f;
			float3 interPosWs = lerp(startOfFramePos, endOfFramePos, t);

			float4 interPosH = mul(float4(interPosWs, 1), pSrt->m_pPassDataSrt->g_mVP);
			float3 interPosNdc = interPosH.xyz / interPosH.w;

			uint2 intersspos = NdcToScreenSpacePos(pSrt, interPosNdc.xy);
			
			float interExpectedDepthLinear = GetLinearDepth(interPosNdc.z, pSrt->m_depthParams);
			float interDepthLinear = GetLinearDepth(depthBufferToUse[intersspos], pSrt->m_depthParams);
			float interAmountBehindGeometry = (interExpectedDepthLinear - interDepthLinear); // if > 0 then we are behind geo
			if (interAmountBehindGeometry >= 0.00f + depthBufferError /* && interAmountBehindGeometry <= kCollisionAllowedDepthDif + depthBufferError*/)
			{
				// half way we already intersecting, so this position is more precise to use
				newPosWs = interPosWs;
				sspos = intersspos;
				newPosNdc = interPosNdc;

				newPosNdc.z = depthBufferToUse[intersspos];
				newPosH = mul(float4(newPosNdc, 1), pSrt->m_pPassDataSrt->m_mAltVPInv);
				newPosWs = newPosH.xyz / newPosH.w;
				newPosWs += pSrt->m_altWorldOrigin;
				amountBehindGeometry = interAmountBehindGeometry;

			}

			// we found the time we hid behind geo
			// lets check if we are not too deep behind geo
			bool okCollision = false;
			bool keepGoing = true;
			if (amountBehindGeometry <= kCollisionAllowedDepthDif)
			{
				// good
				float3 depthNormal = CalculateDepthNormal(pSrt, sspos, params.opaqueAlphaDepthStencil);
				if (dot(depthNormal, velocity) <= 0)
				{
					okCollision = true;
				}
				else
				{
					keepGoing = false;
				}
			}

			if (okCollision)
			{
				//if (motionState == kMotionStateFreeFallAttach)
				{
					state.m_pos = newPosWs;
				}

				if (motionState == kMotionStateFreeFallAttach)
				{
					ParticleStateV0 addedState;

					int resultIndex = -1;

					StickyParticleParams spawnParams = DefaultStickyParticleParams();
					spawnParams.allowSkin = false;

					bool spawnedNew = NewStickyParticle(kMotionStateAttached, sspos, newPosNdc, state.m_pos, 0, newPartStencil, pSrt,
						dispatchId, state.m_id, /*checkInsideFirstParticle=*/ false, /*birthTimeOffset=*/ 0.0f, /*spawn data*/ /*state.m_data*/ state.m_data,
						/*useProvidedIndex*/ false, /*providedParticleIndex=*/-1, groupThreadId.x, /*lifetime=*/ 5.0f,
						addedState, /*normalHalfWayWithRain=*/ false, /*inheritedVelocity=*/ float3(0, 0, 0), /*isSnow*/ isSnow, spawnParams, /*defaultSpeed*/ 1.0f, resultIndex, /*customSize*/ 0);

					newParticleIndex = resultIndex; // if this is not -1, we will calculate skinning mapping

					particleErrorState = kNumOfFramesToRecover;

					res.m_newParticleIndex = resultIndex;
				}
				else
				{
					res.m_haveCollision = true;
					// feedback into cpu to spawn decals
					float3 normal = float3(0, 1, 0);


					res.m_pos = newPosWs;
					res.m_sspos = sspos;
					res.m_normal = normal;
					res.m_isFg = fgStencil;

					//AddCollisionFeedBackSpawn(pSrt, sspos, state.m_pos, normal, dispatchId.x, groupThreadId.x, !fgStencil, fgStencil);

					particleErrorState = kNumOfFramesToRecover;
				}
			}
			
			if (keepGoing)
			{
				// went behind geometry, but too far behind geometry.
				// allow to fly further behind geometry - thinking we are in front of a thin wall/object and flying behind it
				//particleErrorState = kNumOfFramesToRecover;
			}
			else
			{
				particleErrorState = kNumOfFramesToRecover;
			}
		}
	}

	// all 64 threads active here
	// find skinning data for particles that have just attached
	{
		uint threadSucceess = newParticleIndex != -1;
		FindProcessResults findProc = FindProcessMeshTriangleBaricentricsDivergent(pSrt, threadSucceess, sspos, state.m_pos, groupThreadId.x, /*checkBlood*/false, 0, /*allowHead*/true);

		if (findProc.m_foundIndex != -1)
		{
		
#if TRACK_BIND_POSE
			pSrt->m_particleStates[newParticleIndex].m_rotation = EncodeRotationFromBindPose(findProc.m_bindPosLs, findProc.m_baricentrics, findProc.m_meshId);

#if TRACK_INDICES
			pSrt->m_particleStates[newParticleIndex].m_speed = asfloat((findProc.m_indices.x & 0x0000FFFF) | (findProc.m_indices.y << 16));
			pSrt->m_particleStates[newParticleIndex].m_lifeTime = asfloat(asuint(pSrt->m_particleStates[newParticleIndex].m_lifeTime) | (findProc.m_indices.z << 16));
			SetHaveSkinning(pSrt->m_particleStates[newParticleIndex]);
#endif

			AddDebugNewStickyBaryDataParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, findProc.m_posWs, pSrt->m_particleStates[newParticleIndex].m_pos, dispatchId.x, pSrt->m_particleStates[newParticleIndex].m_id, findProc.m_meshId, findProc.m_indices, findProc.m_baricentrics.xy);
#endif
		}
	}

	uint needsAttachedUpdate = /*!outOfBounds && */needsUpdate && ((motionState == kMotionStateAttached)
		&& !(age > lifeTime)) ? 1 : 0;

	if (needsAttachedUpdate)
	{
		// attached
		// continue life. run update:
		float3 prevPos = state.m_pos;

		if (positionComputed)
		{
			state.m_pos = findProcOrig.m_posWs;
			particleErrorState = 0;
		}

		if (!positionComputed)
		{
			SetNoSkinning(state);
			// state.m_scale // stores velocity. in case of attachment, the velocity is the velocity of surface it is attached to + local velocity of the particle
			
			#if TRACK_INDICES
				state.m_pos = state.m_pos + state.m_direction * /*state.m_speed * */ pSrt->m_delta; //  we encode direction in scale
			#else
				state.m_pos = state.m_pos + state.m_direction * state.m_speed * pSrt->m_delta; //  we encode direction in scale
			#endif
		}
		
		if (particleErrorState != 0)
		{
			// keep them around, different color though

			// if we are in erros state and try to contiue living, we don't move with our velocity
			// because we already errored before. using that velocity might get us in even worse situation
			// todo: maybe it is ok to keep moving at least for first error frame?
		
			//actually try using new position only ater a couple of failures
			if (particleErrorState > 2)
				state.m_pos = prevPos;
		

			//return;
		}

		// calculate expected depth
		float4 newPosH = mul(float4(state.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 newPosNdc = newPosH.xyz / newPosH.w;

		// sample depth buffer
		sspos = uint2((newPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (newPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);

		res.m_sspos = sspos;

		
		if (sspos.x >= pSrt->m_screenResolution.x || sspos.y >= pSrt->m_screenResolution.y)
		{
			// expected position of this particle is off screen, so we just kill it
			particleErrorState = kNumOfFramesToRecover;
		}

		// note we allow to try to recover for certain amount of frames
		uint origErrorState = particleErrorState;
		if (particleErrorState < kNumOfFramesToRecover)
		{
			particleErrorState = 0;
		}

		if (particleErrorState == 0)
		{
			// if here, then we are allowed to evaluate. we are either in purely good state or in bad state but within allowed frames to recover

			//state.m_birthTime = pSrt->m_time; // prolongue the life

			
			if (prevStencil != stencilTextureToUse[sspos])
			{
				// completely jumped off to something different
				if (!positionComputed)
					state.m_pos = prevPos; // move back, redo depth at position from before
				particleErrorState = 1;

				// will try again with old position and see if we can recover
				//return;
			}

			float depthNdc = depthBufferToUse[sspos];
			float expectedDepthNdc = newPosNdc.z;

			float expectedDepthLinear = GetLinearDepth(expectedDepthNdc, pSrt->m_depthParams);
			float depthLinear = GetLinearDepth(depthNdc, pSrt->m_depthParams);

			// since we reapply additional offset below we check distance for just very big displacement
			if (abs(depthLinear - expectedDepthLinear) > 0.1f)
			{
				// completely jumped off to something different (same as stencil)
				if (!positionComputed)
					state.m_pos = prevPos; // move back, redo depth at position from before
				particleErrorState = 1;

				// will try again with old position and see if we can recover
				//return;
			}

			// allow to recover
			if (particleErrorState != 0 && !positionComputed)
			{
				// we just jumped off somewhere bad, try using old position and redo the tests
				// calculate expected depth
				newPosH = mul(float4(state.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
				newPosNdc = newPosH.xyz / newPosH.w;

				// sample stencil and depth buffer
				sspos = uint2((newPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (newPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);

				res.m_sspos = sspos;

				bool ok = true;
				
				if (prevStencil != stencilTextureToUse[sspos])
				{
					// completely jumped off to something different, this is fatal
					// we don't change the state
					ok = false;

				}
				

				depthNdc = depthBufferToUse[sspos];
				expectedDepthNdc = newPosNdc.z;

				expectedDepthLinear = GetLinearDepth(expectedDepthNdc, pSrt->m_depthParams);
				depthLinear = GetLinearDepth(depthNdc, pSrt->m_depthParams);

				if (abs(depthLinear - expectedDepthLinear) > 0.1f)
				{
					// completely jumped off to something different, this is fatal
					// we don't change the state
					ok = false;
				}

				if (ok)
				{
					// prev position is fine, just stay there
					particleErrorState = 0;
					//origFlags = 0;
				}
			}

			if (particleErrorState == 0)
			{
				// good, readjust position to snap to background
				if (positionComputed)
				{
					// position is good and we have no erros checking stencil, depth, etc. so we just compute the velocity and are done
					state.m_direction = (state.m_pos - prevPos) / pSrt->m_delta; // we store velocity in scale
				}
				else
				{
					newPosNdc.z = depthNdc;

					newPosH = mul(float4(newPosNdc, 1), pSrt->m_pPassDataSrt->m_mAltVPInv);
				
					state.m_pos = newPosH.xyz / newPosH.w;
					state.m_pos += pSrt->m_altWorldOrigin;

					// recalculate velocity based on motion vector
					float2 motionVectorSNorm = pSrt->m_pPassDataSrt->m_motionVector[sspos];
					int2 motionVector = int2(motionVectorSNorm * pSrt->m_screenResolution);
					int2 lastFrameSSpos = int2(float2(sspos)+motionVector);

					float3 posNdcUnjittered = newPosNdc;
					posNdcUnjittered.xy -= pSrt->m_projectionJitterOffsets.xy;

					float3 lastFramePosNdcUnjittered = float3(posNdcUnjittered.x + motionVectorSNorm.x * 2, posNdcUnjittered.y - motionVectorSNorm.y * 2, prevFrameDepthBufferToUse[lastFrameSSpos]);
					float3 lastFramePosNdc = lastFramePosNdcUnjittered;
					lastFramePosNdc.xy += pSrt->m_projectionJitterOffsets.zw;

					float4 lastFramePosH = mul(float4(lastFramePosNdc, 1), pSrt->m_pPassDataSrt->m_mLastFrameAltVPInv);
					float3 lastFramePosWs = lastFramePosH.xyz / lastFramePosH.w;
					lastFramePosWs += pSrt->m_lastFrameAltWorldOrigin;

					// The code below is either
					// 1) stop any mor processing, use current position as final position and recompute the new velocity
					// 2) Compare previous position of current pixel with expected previous position (previous position of particle)
					//    and apply the difference and re compute the new current position with difference applied, then choose the new position vs recomputed new position
					//    this recomputing allows for tracking on objects that accelerate or decelerate

	#if 0
						// readjust the speed and let the particle live longer

					state.m_direction = (state.m_pos - lastFramePosWs) / pSrt->m_delta; // we store velocity in scale
	#else
					// this is the code to accomodate acceleration
					// 
					if (origErrorState)
					{
						prevPos = lastFramePosWs;
					}

					// no we compare previous position of this pixel with where this particle was last frame
					// if all is good, this offset should be 0
					// if it is not 0 means that the object moved at a different speed than we predicted
					float3 lastFramePosUnexpectedOffset = prevPos - lastFramePosWs;

					prevPos = state.m_pos;

					// we can now readjust our position by that offset and redo everything
					state.m_pos = state.m_pos + lastFramePosUnexpectedOffset;
					newPosH = mul(float4(state.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
					newPosNdc = newPosH.xyz / newPosH.w;

					// sample depth buffer
					sspos = uint2((newPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (newPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);

					depthNdc = depthBufferToUse[sspos];
					expectedDepthNdc = newPosNdc.z;

					expectedDepthLinear = GetLinearDepth(expectedDepthNdc, pSrt->m_depthParams);
					depthLinear = GetLinearDepth(depthNdc, pSrt->m_depthParams);

					if (abs(depthLinear - expectedDepthLinear) > 0.1f || prevStencil != stencilTextureToUse[sspos])
					{
						// readjusting made it much worse,  stay as before (first version was successful)
						state.m_pos = prevPos;
						state.m_direction = (state.m_pos - prevPos) / pSrt->m_delta; // we store velocity in scale

						sspos = res.m_sspos; // restore previous sspos
					}
					else
					{
						// good, readjust position

						res.m_sspos = sspos; // this is new sspos
						newPosNdc.z = depthNdc;

						newPosH = mul(float4(newPosNdc, 1), pSrt->m_pPassDataSrt->m_mAltVPInv);
						state.m_pos = newPosH.xyz / newPosH.w;
						state.m_pos += pSrt->m_altWorldOrigin.xyz;


						// recalculate velocity based on motion vector
						motionVectorSNorm = pSrt->m_pPassDataSrt->m_motionVector[sspos];
						motionVector = int2(motionVectorSNorm * pSrt->m_screenResolution);
						lastFrameSSpos = int2(float2(sspos)+motionVector);

						posNdcUnjittered = newPosNdc;
						posNdcUnjittered.xy -= pSrt->m_projectionJitterOffsets.xy;

						lastFramePosNdcUnjittered = float3(posNdcUnjittered.x + motionVectorSNorm.x * 2, posNdcUnjittered.y - motionVectorSNorm.y * 2, prevFrameDepthBufferToUse[lastFrameSSpos]);
						lastFramePosNdc = lastFramePosNdcUnjittered;
						lastFramePosNdc.xy += pSrt->m_projectionJitterOffsets.zw;

						lastFramePosH = mul(float4(lastFramePosNdc, 1), pSrt->m_pPassDataSrt->m_mLastFrameVPInv);
						lastFramePosWs = lastFramePosH.xyz / lastFramePosH.w;

						state.m_direction = (state.m_pos - lastFramePosWs) / pSrt->m_delta; // we store velocity in scale
					}
				}
#endif
			} // if error state == 0
			else
			{
				if (positionComputed)
				{
					// this means we had recomputed position but it is not visible right now. we can still keep the error at 0
					particleErrorState = 0;
					res.m_sspos.x = -1;
				}

			 }
		} // if initial error state == 0, that is error was less than max invalid frames

		if (particleErrorState != 0) // if != we either are on first frame of error or we continuing error state
		{
			particleErrorState += origErrorState; // accumulate error frames
		}

		frameCounter += 1.0f; // increment with each life frame

	} // if needs attached update

	uint needsReskinning = needsAttachedUpdate && !positionComputed && particleErrorState == 0;
	// all 64 threads active here
	// find skinning data for particles that have lost tracking due to lod switch
	{
		uint threadSucceess = needsReskinning;
		FindProcessResults findProc = FindProcessMeshTriangleBaricentricsDivergent(pSrt, threadSucceess, sspos, state.m_pos, groupThreadId.x, /*checkBlood*/false, 0, /*allowHead*/true);

		if (findProc.m_foundIndex != -1)
		{

#if TRACK_BIND_POSE
			state.m_rotation = EncodeRotationFromBindPose(findProc.m_bindPosLs, findProc.m_baricentrics, findProc.m_meshId);

#if TRACK_INDICES
			state.m_speed = asfloat((findProc.m_indices.x & 0x0000FFFF) | (findProc.m_indices.y << 16));
			state.m_lifeTime = asfloat((asuint(state.m_lifeTime) & 0x0000FFFF) | (findProc.m_indices.z << 16));
			SetHaveSkinning(state);
#endif

			AddDebugNewStickyBaryDataParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, findProc.m_posWs, state.m_pos, dispatchId.x, state.m_id, findProc.m_meshId, findProc.m_indices, findProc.m_baricentrics.xy);
#endif
		}
	}

	// todo: recompute attachment info
	
	if (needsUpdate && age > lifeTime)
	{
		// too old. discard by setting highest level of error accum
		particleErrorState = kNumOfFramesToRecover;

		return res;
	}
	return res;
}



[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeUpdateCopyState(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// grab my particle
	uint gdsOffsetOld = pSrt->m_gdsOffsetCounterOld;
	uint numOldParticles = NdAtomicGetValue(gdsOffsetOld);

	if (dispatchId.x >= numOldParticles)
		return; // todo: dispatch only the number of threads = number of particles

	ParticleStateV0 state = pSrt->m_particleStatesOld[dispatchId.x];

	// write the state out to new buffer
	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint particleIndex = NdAtomicIncrement(gdsOffsetNew);

	pSrt->m_particleStates[particleIndex] = state;
}



[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeStickySnowUpdateCompress(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// grab my particle
	uint gdsOffsetOld = pSrt->m_gdsOffsetCounterOld;
	uint numOldParticles = NdAtomicGetValue(gdsOffsetOld);

	bool needsUpdate = dispatchId.x < numOldParticles;

	if (dispatchId.x == 0)
	{
		//	_SCE_BREAK();
	}

	//if (dispatchId.x >= numOldParticles)
	//	return; // todo: dispatch only the number of threads = number of particles
	StickyUpdateRes updateRes = DefaultStickyUpdateRes();
	
	
		StructuredBuffer<SnowParticleState> castedBufferOld = __create_buffer< StructuredBuffer<SnowParticleState> >(__get_vsharp(pSrt->m_particleStatesOld));
		SnowParticleState state = castedBufferOld[dispatchId.x];
		//ParticleStateV0 state = pSrt->m_particleStatesOld[dispatchId.x];


		//if (state.m_flags0 >= 100)
		//{
		//	return; // too much accumulated error
		//}
		
		needsUpdate = needsUpdate && (state.m_flags0 < 100);

		uint oldFlags1 = state.m_flags1;
		uint motionState = (state.m_flags1 >> 8) & 0x000000FF;

		StickyParticleParams params = DefaultStickyParticleParams();

		updateRes = UpdateStickyParticleMotion(needsUpdate, dispatchId, groupThreadId, groupId, pSrt, /*inout ParticleStateV0 state*/ state,
			/*inout uint particleErrorState*/ state.m_flags0,
			/*uint particleIndex*/ dispatchId.x,
			/*uint prevStencil*/ state.m_flags1 & 0x000000FF,
			/*uint motionState*/ motionState,
			/*inout float frameCounter*/ state.m_id, /*const uint kNumOfFramesToRecover=*/ 10, 9.8, /*turbulence*/ 1.0, 1.5, /*isSnow*/ true, params);
	

	// if particle needs to be killed, the flgas0 will be kNumOfFramesToRecover = 10
	//if (state.m_flags0 >= 10 && state.m_flags0 < 100)
	if (state.m_flags0 > 0 && state.m_flags0 < 10)
	{
		//return; // too much accumulated error
		uint newMotionState = kMotionStateDeadFreeFall;
		state.m_flags1 = (state.m_flags1 & 0xFFFF00FF) | (newMotionState << 8);
		state.m_direction = float3(0, 0, 0); // zero it out, it is used for accumulating velocity with gravity
	}

	if (state.m_flags0 >= 10)
	{
		return; // too much accumulated error
	}

	if (!needsUpdate)
	{
		return; // this was never a valid particle
	}

	// write the state out to new buffer
	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint particleIndex = NdAtomicIncrement(gdsOffsetNew);
	
	RWStructuredBuffer<SnowParticleState> castedBuffer = __create_buffer< RWStructuredBuffer<SnowParticleState> >(__get_vsharp(pSrt->m_particleStates));
	castedBuffer[particleIndex] = state;
	//pSrt->m_particleStates[particleIndex] = state;
}


RibbonSpawnHint CreateRibbonSpawnHint(float3 pos, float3 scale, float3x3 rotationMatrix, float4 color, uint spawnerId16, uint ribbonType1, uint uniqueId, float age)
{


	RibbonSpawnHint infoState = RibbonSpawnHint(0);

	infoState.m_pos = pos.xyz;
	//infoState.m_scale = state.m_scale;
	//infoState.m_flags0 = pSrt->m_rootId;
	//infoState.m_flags1 = pSrt->m_rootUniqueid;


	//infoState.m_partMat3x3[0] = originalParticle.world[0].xyz;
	//infoState.m_partMat3x3[1] = originalParticle.world[1].xyz;
	//infoState.m_partMat3x3[2] = originalParticle.world[2].xyz;


	// compress the data so that we can fit more

	float scaleX = scale.x; // 2.0f / originalParticle.invscale.x;
	float scaleY = scale.y; // 2.0f / originalParticle.invscale.y;
	float scaleZ = scale.z; // 2.0f / originalParticle.invscale.z;

	float3 mat0 = rotationMatrix[0].xyz * scaleX;
	float3 mat1 = rotationMatrix[1].xyz * scaleY;
	float3 mat2 = rotationMatrix[2].xyz * scaleZ;




	uint3 packedm0xyzm1xyz = uint3(PackFloat2ToUInt(mat0.x, mat0.y), PackFloat2ToUInt(mat0.z, mat1.x), PackFloat2ToUInt(mat1.y, mat1.z));

	uint3 packedm2xyzsxyz = uint3(PackFloat2ToUInt(mat2.x, mat2.y), PackFloat2ToUInt(mat2.z, scaleX), PackFloat2ToUInt(scaleY, scaleZ));


	//infoState.m_scale = float3(scaleX, scaleY, scaleZ);
	infoState.m_packedm0xyzm1xyz = packedm0xyzm1xyz;
	infoState.m_packedm2xyzsxyz = packedm2xyzsxyz;


	infoState.m_packedColor = uint2(PackFloat2ToUInt(color.x, color.y), PackFloat2ToUInt(color.z, color.w));
	infoState.m_type1_spawnerId16 = (spawnerId16 & 0x0000FFFF) | (ribbonType1 << 16); // at this point type is not determined
	infoState.m_uniqueId = uniqueId;
	infoState.m_age = age;
	return infoState;

}

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeWaterTrackerUpdateCompress(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	if (pSrt->m_delta < 0.00001)
	{
		pSrt->m_particleStates[dispatchId.x] = pSrt->m_particleStatesOld[dispatchId.x];
		return;
	}


	// grab my particle
	uint gdsOffsetOld = pSrt->m_gdsOffsetCounterOld;
	uint numOldParticles = NdAtomicGetValue(gdsOffsetOld);

	//if (dispatchId.x >= numOldParticles)
	//	return; // todo: dispatch only the number of threads = number of particles

	StructuredBuffer<SnowParticleState> castedBufferOld = __create_buffer< StructuredBuffer<SnowParticleState> >(__get_vsharp(pSrt->m_particleStatesOld));
	SnowParticleState state = castedBufferOld[dispatchId.x];
	//ParticleStateV0 state = pSrt->m_particleStatesOld[dispatchId.x];
	const uint kNumOfFramesToRecover = 10;

	if (state.m_id == 0)
	{
		state.m_birthTime = -1000000.0; // will not update. too old
		state.m_lifeTime = 0.0; // will not update. too old
		state.m_flags0 = kNumOfFramesToRecover;
	}

	if (state.m_flags0 >= kNumOfFramesToRecover)
	{
	//	return; // too much accumulated error
	}

	uint oldFlags1 = state.m_flags1;
	uint motionState = (state.m_flags1 >> 8) & 0x000000FF;

	StickyParticleParams params = DefaultStickyParticleParams();
	
	params.opaqueAlphaDepthStencil = false; // we run update of these particle on opaque only

	StickyUpdateRes updateRes = UpdateStickyParticleMotion(/*needsUpdate*/ true, dispatchId, groupThreadId, groupId, pSrt, /*inout ParticleStateV0 state*/ state,
		/*inout uint particleErrorState*/ state.m_flags0,
		/*uint particleIndex*/ dispatchId.x,
		/*uint prevStencil*/ state.m_flags1 & 0x000000FF,
		/*uint motionState*/ motionState,
		/*inout float frameCounter*/ state.m_id, /*const uint kNumOfFramesToRecover=*/ kNumOfFramesToRecover, 9.8 * 0.25, /*turbulence*/ 1.0, 1.5, /*isSnow*/ true, params);

	//if (state.m_flags0 >= 10 && state.m_flags0 < 100)
	if (state.m_flags0 > 0 && state.m_flags0 < kNumOfFramesToRecover)
	{
		//return; // too much accumulated error
		uint newMotionState = kMotionStateDeadFreeFall;
		state.m_flags1 = (state.m_flags1 & 0xFFFF00FF) | (newMotionState << 8);
		state.m_direction.x = 0; // zero it out, is used for speed
	}

	if (state.m_flags0 >= kNumOfFramesToRecover)
	{
		state.m_id = 0; // too much accumulated error, let it be used by someone else
	}

	// write the state out to new buffer
	uint particleIndex = dispatchId.x;

	RWStructuredBuffer<SnowParticleState> castedBuffer = __create_buffer< RWStructuredBuffer<SnowParticleState> >(__get_vsharp(pSrt->m_particleStates));
	//pSrt->m_particleStates[particleIndex] = state;

	if (state.m_flags0 == 0 && updateRes.m_sspos.x != -1)
	{

		bool underWater = pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil[updateRes.m_sspos] & 0x2;

		if (GetRenderState(state) == kRenderStateAboveWater && underWater)
		{
			SetRenderState(state, kRenderStateUnderWater);

			AddSpawnParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, state.m_pos + float3(0, 0.1, 0), 1, float3(0.2, 0.2, 0.2));
		}
		else if (GetRenderState(state) == kRenderStateUnderWater && !underWater)
		{
			SetRenderState(state, kRenderStateAboveWater);
			// add a hint for spawning water ribbons

			uint gdsOffsetNew = pSrt->m_gdsOffsetOther;

			if (gdsOffsetNew != 0)
			{
				uint size, stride;
				pSrt->m_particleStatesOther.GetDimensions(size, stride);
				{
					// we use a additional buffer of particle states to write data to tell the otehr system to spawn particles. It doesn't have to be a buffer of states, but we just use it.

					uint newInfoIndex = NdAtomicIncrement(gdsOffsetNew);
					if (newInfoIndex >= size)
					{
						// decrement back
						NdAtomicDecrement(gdsOffsetNew);
					}
					else
					{
						RWStructuredBuffer<RibbonSpawnHint> destHintBuffer = __create_buffer<RWStructuredBuffer<RibbonSpawnHint> >(__get_vsharp(pSrt->m_particleStatesOther));

						float3x3 rot;
						rot[0] = float3(1, 0, 0);
						rot[1] = float3(0, 1, 0);
						rot[2] = float3(0, 0, 1);

						RibbonSpawnHint infoState = CreateRibbonSpawnHint(state.m_pos, float3(0.05, 0.05, 0.05), rot, float4(1, 1, 1, 1), /*spawner index*/ 128 + particleIndex, 0, /*uinique id in spawer*/ state.m_id, 0.0f);

						destHintBuffer[newInfoIndex] = infoState;
					}
				}
			}
		}
	}

	castedBuffer[particleIndex] = state;
}

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeBubbleUpdateCompress(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// grab my particle
	uint gdsOffsetOld = pSrt->m_gdsOffsetCounterOld;
	uint numOldParticles = NdAtomicGetValue(gdsOffsetOld);

	if (dispatchId.x >= numOldParticles)
		return; // todo: dispatch only the number of threads = number of particles

	StructuredBuffer<SnowParticleState> castedBufferOld = __create_buffer< StructuredBuffer<SnowParticleState> >(__get_vsharp(pSrt->m_particleStatesOld));
	SnowParticleState state = castedBufferOld[dispatchId.x];
	//ParticleStateV0 state = pSrt->m_particleStatesOld[dispatchId.x];

	if (state.m_flags0 >= 100)
	{
		return; // too much accumulated error
	}
	uint oldFlags1 = state.m_flags1;

	StickyParticleParams params = DefaultStickyParticleParams();

	UpdateStickyParticleMotion(/*needsUpdate*/ true, dispatchId, groupThreadId, groupId, pSrt, /*inout ParticleStateV0 state*/ state,
		/*inout uint particleErrorState*/ state.m_flags0,
		/*uint particleIndex*/ dispatchId.x,
		/*uint prevStencil*/ state.m_flags1 & 0x000000FF,
		/*uint motionState8*/ state.m_flags1 >> 8,
		/*inout float frameCounter*/ state.m_id, /*const uint kNumOfFramesToRecover=*/ 10, 9.8 * 0.25,/*turbulence*/  0.0, 1.5, /*isSnow*/ false, params);

	//if (state.m_flags0 >= 10 && state.m_flags0 < 100)
	if (state.m_flags0 > 0 && state.m_flags0 < 10)
	{
		//return; // too much accumulated error
		uint newMotionState = 2;
		state.m_flags1 = (state.m_flags1 & 0x000000FF) | (newMotionState << 8);
		state.m_direction.x = 0; // zero it out, is used for speed
	}
	
	if (state.m_flags0 >= 10 && state.m_flags0 < 100)
	{
		return; // too much accumulated error
	}

	// write the state out to new buffer
	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint particleIndex = NdAtomicIncrement(gdsOffsetNew);

	RWStructuredBuffer<SnowParticleState> castedBuffer = __create_buffer< RWStructuredBuffer<SnowParticleState> >(__get_vsharp(pSrt->m_particleStates));
	castedBuffer[particleIndex] = state;
	//pSrt->m_particleStates[particleIndex] = state;
}


[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeStickySnowGenerateRenderables(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// grab my particle

	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint numParticles = NdAtomicGetValue(gdsOffsetNew);


	if (dispatchId.x >= numParticles)
		return; // todo: dispatch only the number of threads = number fo particles

	ParticleStateV0 state = pSrt->m_particleStates[dispatchId.x];
	uint destinationIndex = dispatchId.x;


	float4x4	g_identity = { { 1, 0, 0, 0 },{ 0, 1, 0, 0 },{ 0, 0, 1, 0 },{ 0, 0, 0, 1 } };
	float3 kUnitYAxis = { 0.0f, 1.0f, 0.0f };


	float3 pos = state.m_pos;
	float nearfade = saturate(length(state.m_pos - pSrt->m_cameraPosWs.xyz)*0.75); // Simple Alpha Near Fade. 
	float snowage = saturate(pSrt->m_time - state.m_birthTime); // Fade in by Age
	ParticleInstance inst = ParticleInstance(0);

	inst.world = g_identity;			// The object-to-world matrix
	inst.color = float4(0.55, 0.55, 0.55, nearfade *snowage);			// The particle's color


	uint motionState = (state.m_flags1 >> 8) & 0x000000FF;

	if (motionState == 1)
	{
		//inst.color = float4(0, 1, 0, 1);
	}

	if (state.m_flags0 > 10)
	{
		//inst.color = float4(1, 0, 0, 1);
	}
	else if (state.m_flags0 > 0)
	{
		//inst.color = float4(0, 0, 1, 1);
	}

	

	if (GetHaveSkinning(state) && (motionState == kMotionStateAttached))
	{
		//inst.color = float4(0, 1, 0, 1);
	}

	float distortion = 0;
	if (motionState == 0)
	{
		distortion = 0.5f;
	}
	float threshold = 0.5;
	if (motionState == 0)
	{
		distortion = 0.0f;
	}
	inst.texcoord = float4(1, 1, 0, 0);		// Texture coordinate scale and bias (uv = uv0 * texcoord.xy + texcoord.zw)
	inst.userh = float4(0.0, 0.0, 0.0, distortion);			// User attributes (used to be half data type)
	inst.userf = float4(0.0, threshold, 0.0, 0.0);			// User attributes
	inst.partvars = float4(0, 0, 0, 0);		// Contains age, normalized age, ribbon distance, and frame number
	inst.invscale = float4(1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f);		// Reciprocal of the particle's half-scale vector

	float age = (pSrt->m_time - state.m_birthTime);

	inst.userh.x = state.m_data * 8; // age * kFps;


	float3 toCam = normalize(pSrt->m_cameraPosWs.xyz - pos);

	float coss = cos(state.m_data * 0.5);
	float sinn = sin(state.m_data * 0.5);

	float4x4 twist;
	twist[0].xyzw = float4(coss, sinn, 0, 0);
	twist[1].xyzw = float4(-sinn, coss, 0, 0);
	twist[2].xyzw = float4(0, 0, 1, 0);
	twist[3].xyzw = float4(0, 0, 0, 1);


	//inst.world = mul(twist, TransformFromLookAt(toCam, state.m_rotation.xyz, pos, true));

	inst.world = mul(twist, TransformFromLookAt(toCam, float3(0, 1, 0) /* state.m_rotation.xyz*/, pos, true));

	// for crawlers: use this for aligning the sprite with velocity direction
	//inst.world = TransformFromLookAtYFw(state.m_scale.xyz, state.m_rotation.xyz, pos, true);
	float distscale = saturate(length(state.m_pos - pSrt->m_cameraPosWs.xyz)*0.12 + 0.2);
	float3 scale = float3(pSrt->m_rootComputeVec0.xyz) * 0.5 * distscale;// *5;

	float3 velocity = float3(0, 0, 0);
	
	if (motionState == kMotionStateFreeFallAttach || motionState == kMotionStateDeadFreeFall || motionState == kMotionStateFreeFallCollide)
	{
		velocity.x = f16tof32(asuint(state.m_scale.y) >> 16);
		velocity.y = f16tof32(asuint(state.m_scale.z));
		velocity.z = f16tof32(asuint(state.m_scale.z) >> 16);

		inst.world = mul(twist, TransformFromLookAt(toCam, normalize(velocity) /* state.m_rotation.xyz*/, pos, false));
		float curSize = length(scale.xy);
		float distTravel = length(velocity * pSrt->m_delta);
		float scaleFactor = pSrt->m_delta > 0.001 ? distTravel / curSize * 1.0 : 1.0;
		scale.y *= scaleFactor;
		//scale.y *= length(velocity);;
	}

	if (motionState == kMotionStateAttached)
	{
		inst.color.a = nearfade; // no blend in by age
		inst.color.xyz = float3(0.35, 0.35, 0.35);
	}

	if (motionState == kMotionStateAttached || motionState == kMotionStateDeadFreeFall)
	{
		scale *= (0.5 + 0.5 * (1.0 - age / 5.0));

		inst.color.a *= (1.0 - age / 5.0);

	}


	inst.world[0].xyz = inst.world[0].xyz * scale.x;
	inst.world[1].xyz = inst.world[1].xyz * scale.y;
	inst.world[2].xyz = inst.world[2].xyz * scale.z;

	inst.invscale = float4(1.0f / (scale.x * 0.5), 1.0f / (scale.y * 0.5), 1.0f / (scale.z * 0.5), 1.0f / 1);

	float3 renderOffset = pSrt->m_renderOffset.xyz;
	renderOffset.x = -0.25f;
	renderOffset.z = 0.5f;
	float4 posOffest = mul(float4(renderOffset, 1), inst.world);

	// modify position based on camera
	inst.world[3].xyz = posOffest.xyz - pSrt->m_altWorldOrigin.xyz;

	//partMat.SetRow(3, MulPointMatrix(offset.GetVec4(), partMat));

	inst.prevWorld = inst.world;		// Last frame's object-to-world matrix

	pSrt->m_particleInstances[destinationIndex] = inst;
	pSrt->m_particleIndices[destinationIndex] = destinationIndex;
}

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeWaterTrackerGenerateRenderables(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// grab my particle

	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;

	//if (dispatchId.x >= numParticles)
	//	return; // todo: dispatch only the number of threads = number fo particles

	ParticleStateV0 state = pSrt->m_particleStates[dispatchId.x];
	
	if (state.m_id == 0)
	{
		return;
	}

	int renderIndex = NdAtomicIncrement(gdsOffsetNew); // and this counter is the one used for dispatching rendering

	
	uint destinationIndex = renderIndex;


	float4x4	g_identity = { { 1, 0, 0, 0 },{ 0, 1, 0, 0 },{ 0, 0, 1, 0 },{ 0, 0, 0, 1 } };
	float3 kUnitYAxis = { 0.0f, 1.0f, 0.0f };

	uint renderState = GetRenderState(state);

	float3 pos = state.m_pos;
	float nearfade = saturate(length(state.m_pos - pSrt->m_cameraPosWs.xyz)*0.75); // Simple Alpha Near Fade. 
	float snowage = saturate(pSrt->m_time - state.m_birthTime); // Fade in by Age
	ParticleInstance inst = ParticleInstance(0);

	inst.world = g_identity;			// The object-to-world matrix
	inst.color = float4(0.6, 0.6, 0.6, 1);			// The particle's color

	if (renderState == kRenderStateUnderWater)
	{
		inst.color = float4(0.0, 0.0, 0.6, 1);			// The particle's color
	}

	if (renderState == kRenderStateAboveWater)
	{
		inst.color = float4(0.0, 0.6, 0.0, 1);			// The particle's color
	}


	uint motionState = state.m_flags1 >> 8;

	if (motionState == 1)
	{
		//inst.color = float4(0, 1, 0, 1);
	}

	if (state.m_flags0 > 10)
	{
		//inst.color = float4(1, 0, 0, 1);
	}
	else if (state.m_flags0 > 0)
	{
		//inst.color = float4(0, 0, 1, 1);
	}
	float distortion = 0;
	if (motionState == 0)
	{
		distortion = 0.5f;
	}
	float threshold = 0.5;
	if (motionState == 0)
	{
		distortion = 0.0f;
	}
	inst.texcoord = float4(1, 1, 0, 0);		// Texture coordinate scale and bias (uv = uv0 * texcoord.xy + texcoord.zw)
	inst.userh = float4(0.0, 0.0, 0.0, distortion);			// User attributes (used to be half data type)
	inst.userf = float4(0.0, threshold, 0.0, 0.0);			// User attributes
	inst.partvars = float4(0, 0, 0, 0);		// Contains age, normalized age, ribbon distance, and frame number
	inst.invscale = float4(1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f);		// Reciprocal of the particle's half-scale vector

	float age = (pSrt->m_time - state.m_birthTime);

	inst.userh.x = age * kFps;


	float3 toCam = normalize(pSrt->m_cameraPosWs.xyz - pos);

	float coss = cos(state.m_data * 0.5);
	float sinn = sin(state.m_data * 0.5);

	float4x4 twist;
	twist[0].xyzw = float4(coss, sinn, 0, 0);
	twist[1].xyzw = float4(-sinn, coss, 0, 0);
	twist[2].xyzw = float4(0, 0, 1, 0);
	twist[3].xyzw = float4(0, 0, 0, 1);




	inst.world = mul(twist, TransformFromLookAt(toCam, float3(0, 1, 0) /* state.m_rotation.xyz*/, pos, true));

	// for crawlers: use this for aligning the sprite with velocity direction
	//inst.world = TransformFromLookAtYFw(state.m_scale.xyz, state.m_rotation.xyz, pos, true);
	float distscale = saturate(length(state.m_pos - pSrt->m_cameraPosWs.xyz)*0.12 + 0.2);
	float3 scale = float3(0.01, 0.01, 0.01);// float3(pSrt->m_rootComputeVec0.xyz) * 0.5 * distscale;

	//if (motionState == kMotionStateAttached || motionState == kMotionStateDeadFreeFall)
	//{
	//	scale *= (0.5 + 0.5 * (1.0 - age / 5.0));
	//
	//	inst.color.a *= (1.0 - age / 5.0);
	//
	//}


	inst.world[0].xyz = inst.world[0].xyz * scale.x;
	inst.world[1].xyz = inst.world[1].xyz * scale.y;
	inst.world[2].xyz = inst.world[2].xyz * scale.z;

	inst.invscale = float4(1.0f / (scale.x * 0.5), 1.0f / (scale.y * 0.5), 1.0f / (scale.z * 0.5), 1.0f / 1);

	float3 renderOffset = pSrt->m_renderOffset.xyz;
	renderOffset.x = -0.25f;
	renderOffset.z = 0.5f;
	float4 posOffest = mul(float4(renderOffset, 1), inst.world);

	// modify position based on camera
	inst.world[3].xyz = posOffest.xyz - pSrt->m_altWorldOrigin.xyz;

	//partMat.SetRow(3, MulPointMatrix(offset.GetVec4(), partMat));

	inst.prevWorld = inst.world;		// Last frame's object-to-world matrix

	pSrt->m_particleInstances[destinationIndex] = inst;
	pSrt->m_particleIndices[destinationIndex] = destinationIndex;
}
[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeBubbleGenerateRenderables(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// grab my particle

	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint numParticles = NdAtomicGetValue(gdsOffsetNew);


	if (dispatchId.x >= numParticles)
		return; // todo: dispatch only the number of threads = number fo particles

	ParticleStateV0 state = pSrt->m_particleStates[dispatchId.x];
	uint destinationIndex = dispatchId.x;


	float4x4	g_identity = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };
	float3 kUnitYAxis = { 0.0f, 1.0f, 0.0f };


	float3 pos = state.m_pos;

	ParticleInstance inst = ParticleInstance(0);

	inst.world = g_identity;			// The object-to-world matrix
	inst.color = float4(0.3, 0.3, 0.3, 1);			// The particle's color


	uint motionState = state.m_flags1 >> 8;

	if (motionState == 1)
	{
		//inst.color = float4(0, 1, 0, 1);
	}

	if (state.m_flags0 > 10)
	{
		//inst.color = float4(1, 0, 0, 1);
	}
	else if (state.m_flags0 > 0)
	{
		//inst.color = float4(0, 0, 1, 1);
	}
	float distortion = 0;
	if (motionState == 0)
	{
		distortion = 0.5f;
	}
	float threshold = 0.5;
	if (motionState == 0)
	{
		distortion = 0.0f;
	}
	inst.texcoord = float4(1, 1, 0, 0);		// Texture coordinate scale and bias (uv = uv0 * texcoord.xy + texcoord.zw)
	inst.userh = float4(0.0, 0.0, 0.0, distortion);			// User attributes (used to be half data type)
	inst.userf = float4(0.0, threshold, 0.0, 0.0);			// User attributes
	inst.partvars = float4(0, 0, 0, 0);		// Contains age, normalized age, ribbon distance, and frame number
	inst.invscale = float4(1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f);		// Reciprocal of the particle's half-scale vector

	float age = (pSrt->m_time - state.m_birthTime);

	inst.userh.x = age * kFps;


	float3 toCam = normalize(pSrt->m_cameraPosWs.xyz - pos);

	float coss = cos(state.m_data * 0.5);
	float sinn = sin(state.m_data * 0.5);

	float4x4 twist;
	twist[0].xyzw = float4(coss, sinn, 0, 0);
	twist[1].xyzw = float4(-sinn, coss, 0, 0);
	twist[2].xyzw = float4(0,0,1,0);
	twist[3].xyzw = float4(0, 0, 0, 1);




	inst.world = mul(twist, TransformFromLookAt(toCam, state.m_rotation.xyz, pos, true));
	
	// for crawlers: use this for aligning the sprite with velocity direction
	//inst.world = TransformFromLookAtYFw(state.m_scale.xyz, state.m_rotation.xyz, pos, true);

	float3 scale = float3(pSrt->m_rootComputeVec0.xyz) * 0.5;

	if (motionState == 1 || motionState == 2)
	{
		scale *= (0.5 + 0.5 * (1.0 - age / 5.0));

		inst.color.a *= (1.0 - age / 5.0);
	}


	inst.world[0].xyz = inst.world[0].xyz * scale.x;
	inst.world[1].xyz = inst.world[1].xyz * scale.y;
	inst.world[2].xyz = inst.world[2].xyz * scale.z;

	inst.invscale = float4(1.0f / (scale.x * 0.5), 1.0f / (scale.y * 0.5), 1.0f / (scale.z * 0.5), 1.0f / 1);

	float3 renderOffset = pSrt->m_renderOffset.xyz;
	renderOffset.x = -0.25f;
	renderOffset.z = 0.5f;
	float4 posOffest = mul(float4(renderOffset, 1), inst.world);

	// modify position based on camera
	inst.world[3].xyz = posOffest.xyz - pSrt->m_altWorldOrigin.xyz;

	//partMat.SetRow(3, MulPointMatrix(offset.GetVec4(), partMat));

	inst.prevWorld = inst.world;		// Last frame's object-to-world matrix

	pSrt->m_particleInstances[destinationIndex] = inst;
	pSrt->m_particleIndices[destinationIndex] = destinationIndex;
}



[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeGenerateTest(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// grab my particle

	ParticleStateV0 state = pSrt->m_particleStates[dispatchId.x];

	float age = (pSrt->m_time - state.m_birthTime);

	float rand0 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 0);  // float((pSrt->m_gameTicks * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);

	float rand1 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 1);  // float(((pSrt->m_gameTicks ^ 0x735fac31) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);
	float rand2 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 2);  // float(((pSrt->m_gameTicks ^ 0xac31735f) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);
	float rand3 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 3);  // float(((pSrt->m_gameTicks ^ 0x317ac35f) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);

	if (age > state.m_lifeTime)
	{
		// too old. should not draw, unless we choose to
		state.m_birthTime = pSrt->m_time;


		// Test 1: spawn particles at random 3d position and move down
		/*
		state.m_pos.x = rand0 * 10;
		state.m_pos.z = rand1 * 10;
		state.m_pos.y = 10;

		state.m_speed = 5.0f + rand2;
		state.m_lifeTime = 1.0f + rand3 * 2.0;
		*/

		// Test 2: spawn particles in random screenspace and lookup depth
		uint2 sspos = uint2(pSrt->m_screenResolution.x * rand0, pSrt->m_screenResolution.y * rand1);
		//float depthVs = GetLinearDepth(pSrt->m_pPassDataSrt->m_primaryDepthTexture[sspos], pSrt->m_depthParams);

		float3 posNdc = float3(rand0 * 2 - 1, -(rand1 * 2 - 1), pSrt->m_pPassDataSrt->m_primaryDepthTexture[sspos]);

		float4 posH = mul(float4(posNdc, 1), pSrt->m_pPassDataSrt->m_mAltVPInv);
		float3 posWs = posH.xyz / posH.w;
		posWs += pSrt->m_altWorldOrigin.xyz;
		
		state.m_pos = posWs;

		// lookup the normal
		const uint4 sample0 = pSrt->m_pPassDataSrt->m_gbuffer0[sspos];

		BrdfParameters brdfParameters = (BrdfParameters)0;
		Setup setup = (Setup)0;
		UnpackGBuffer(sample0, 0, brdfParameters, setup);

		//state.m_pos = state.m_pos + setup.normalWS * 0.5f;

		state.m_speed = -0.5;

		state.m_lifeTime = 1.1f + rand3 * 0.1;

		state.m_rotation.xyz = setup.normalWS;

	}
	else
	{
		// continue life
		state.m_pos.y = state.m_pos.y - state.m_speed * pSrt->m_delta;

	}

	pSrt->m_particleStates[dispatchId.x] = state;

	float4x4	g_identity = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };
	float3 kUnitYAxis = { 0.0f, 1.0f, 0.0f };

	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint particleIndex = NdAtomicIncrement(gdsOffsetNew);

	float3 pos = state.m_pos;

	ParticleInstance inst = ParticleInstance(0);

	inst.world = g_identity;			// The object-to-world matrix
	inst.color = float4(1,1,1,1);			// The particle's color
	inst.texcoord = float4(1, 1, 0, 0);		// Texture coordinate scale and bias (uv = uv0 * texcoord.xy + texcoord.zw)
	inst.userh = float4(0, 0, 0, 0);			// User attributes (used to be half data type)
	inst.userf = float4(0, 0, 0, 0);			// User attributes
	inst.partvars = float4(0,0,0,0);		// Contains age, normalized age, ribbon distance, and frame number
	inst.invscale = float4(1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f);		// Reciprocal of the particle's half-scale vector

	float scale = 0.5;

	float3 toCam = normalize(pSrt->m_cameraPosWs.xyz - pos);

	inst.world = TransformFromLookAt(toCam, state.m_rotation.xyz, pos, true);

	inst.world[0].xyz = inst.world[0].xyz * scale;
	inst.world[1].xyz = inst.world[1].xyz * scale;
	inst.world[2].xyz = inst.world[2].xyz * scale;

	inst.invscale = float4(1.0f / (scale * 0.5), 1.0f / (scale * 0.5), 1.0f / (scale * 0.5), 1.0f / (scale * 0.5));
	
	// modify position based on camera
	inst.world[3].xyz = inst.world[3].xyz - pSrt->m_altWorldOrigin;

	inst.prevWorld = inst.world;		// Last frame's object-to-world matrix

	pSrt->m_particleInstances[particleIndex] = inst;
	pSrt->m_particleIndices[particleIndex] = particleIndex;
}

// sample codes
/*

float ssdepth = pSrt->m_pPassDataSrt->m_primaryDepthTexture[pos]; // this is the depth after division by w
// can also get linear depth, that should return position




*/


[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeRenderSnapshotMakeList(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{


	// check if match the texture


	// do 64 random samples in the cell to generate data about this cell. then use this data for spawning splashes in this cell
	float rand0 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 0);
	float rand1 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 1);
	
	uint2 texWH;
	pSrt->m_pPassDataSrt->m_renderSnapshotDepthTexture.GetDimensions(texWH.x, texWH.y);
		
	
	uint2 sspos = uint2(texWH.x * rand0, texWH.y - texWH.y * rand1);

	float2 ssposNorm = float2(rand0 * 2 - 1, rand1 * 2 -1);

	float3 posNdc = float3(ssposNorm.xy, pSrt->m_pPassDataSrt->m_renderSnapshotDepthTexture[sspos]);

	if (posNdc.z > 0.99)
		return; // too far

	float4 posH = mul(float4(posNdc, 1), pSrt->m_renderSnapshotMVPInv);
	float3 posWs = posH.xyz / posH.w;


	float depthVs = GetLinearDepth(pSrt->m_pPassDataSrt->m_renderSnapshotDepthTexture[sspos], pSrt->m_renderSnapshotDepthViewdepthParams);

	// grab my particle


	float rand2 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 2);

	if (rand2 > pSrt->m_rootComputeVec1.x)
		return; // probability check

	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint particleIndex = NdAtomicIncrement(gdsOffsetNew);

	///if (particleIndex >= pSrt->m_rootComputeVec1.x) // m_computeVec1.x stores desired spawn rate
	//{
		// decrement back
	//	NdAtomicDecrement(gdsOffsetNew);

	//	return; // can't add new particles
	//}

	ParticleStateV0 state = ParticleStateV0(0);

	state.m_pos = posWs;
	state.m_rotation.xyz = float3(0, 1, 0);
	state.m_scale = float3(1, 1, 1);

	pSrt->m_particleStates[particleIndex] = state;
}
void SSCollision(inout float3 position, inout float3 velocity, ParticleComputeJobSrt *pSrt)
{


	float4 newPosH = mul(float4(position + velocity, 1), pSrt->m_pPassDataSrt->g_mVP);
	float3 newPosNdc = newPosH.xyz / newPosH.w;


	if (abs(newPosNdc.x) > 1.0 || abs(newPosNdc.y) > 1.0 || newPosNdc.z < 0 || newPosNdc.z > 1.0)
	{
		// the particle is outside of view frustum

		// we choose to just advance it forward
	}
	uint2 sspos = uint2((newPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (newPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);


	float particleDepthLinear = GetLinearDepth(newPosNdc.z, pSrt->m_depthParams);
	float depthLinear = GetLinearDepth(pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos], pSrt->m_depthParams);

	float depthDifference = depthLinear - particleDepthLinear;

	///////////////////////////////////////////////
	///  S A M P L E   G B U F F E R
	const uint4 sample0 = pSrt->m_pPassDataSrt->m_gbuffer0[sspos];
	const uint4 sample1 = pSrt->m_pPassDataSrt->m_gbuffer1[sspos];

	BrdfParameters brdfParameters = (BrdfParameters)0;
	Setup setup = (Setup)0;
	UnpackGBuffer(sample0, sample1, brdfParameters, setup);

	float3 reflectionVector = MaReflect(velocity, setup.normalWS);


	velocity = (depthDifference<0 && depthDifference>-0.25) ? reflectionVector *0.1 : velocity;

}

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeTestUpdate(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// grab my particle
	uint gdsOffsetOld = pSrt->m_gdsOffsetCounterOld;
	uint numOldParticles = NdAtomicGetValue(gdsOffsetOld);

	if (dispatchId.x >= numOldParticles)
		return; // todo: dispatch only the number of threads = number of particles

	StructuredBuffer<TestParticleState> castedBufferOld = __create_buffer< StructuredBuffer<TestParticleState> >(__get_vsharp(pSrt->m_particleStatesOld));
	TestParticleState state = castedBufferOld[dispatchId.x];
	//ParticleStateV0 state = pSrt->m_particleStatesOld[dispatchId.x];

	float age = (pSrt->m_time - state.m_birthTime);
	if (age > state.m_lifeSpan) // Has lived its lifespan. No need to add it to the buffer of living particles.
	{
		return;
	}


	float3 velocityPerSecond = float3(0,0,0);
	
	///////////////////////////////////////////////
	///  F O R C E S

	for (int fid = 0; fid < pSrt->m_numFields; ++fid)
	{
		FieldState fs = pSrt->m_fieldStates[fid];
		uint nodeId = fs.nodeId_fieldClass & 0x0000FFFF;
		if (nodeId == kGravityField)
		{
			velocityPerSecond += Gravity(fs) * 0.01;
		}
		else if (nodeId == kDragField)
		{
			velocityPerSecond += Drag(fs, state.m_velocity);
		}
	}

	// try to get to spawner
	uint parentPartId = state.m_flags1;
	uint numOtherParts;
	pSrt->m_particleIndicesOther.GetDimensions(numOtherParts);

	
	float3 toParent = float3(0, 0, 0);

	if (parentPartId < numOtherParts)
	{
		uint origParticleIndex = pSrt->m_particleIndicesOther[parentPartId];
		ParticleInstance originalParticle = pSrt->m_particleInstancesOther[origParticleIndex];

		originalParticle.world[3].xyz = originalParticle.world[3].xyz + pSrt->m_altWorldOrigin.xyz;

		toParent = normalize(originalParticle.world[3].xyz - state.m_pos);
	}
	//attarction to reference cpu particles
	//velocityPerSecond += toParent * 0.3 * sin((age + parentPartId) * 0.1);

	///////////////////////////////////////////////
	///  C U S T O M   C O D E


	state.m_velocity += velocityPerSecond * pSrt->m_delta;
	SSCollision(state.m_pos, state.m_velocity, pSrt);
	state.m_pos += state.m_velocity;

	// write the state out to new buffer
	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint particleIndex = NdAtomicIncrement(gdsOffsetNew);
	
	RWStructuredBuffer<TestParticleState> castedBuffer = __create_buffer< RWStructuredBuffer<TestParticleState> >(__get_vsharp(pSrt->m_particleStates));
	castedBuffer[particleIndex] = state;
	//pSrt->m_particleStates[particleIndex] = state;
}

void EmitterSpawn(EmitterState estate, const ParticleInstance emitterData, float rand0, float rand1, float rand2, inout float3 position, inout float3 velocity)
{
	velocity = float3(0,0,0);

	float3 positionLS = float3(rand0 - 0.5, rand1 - 0.5, rand2 - 0.5); //Local space position todo: implement other (non-cube) volume shapes
	//velocity += emitterData.color.rgb; //direction * directionalSpeed
	velocity += normalize(positionLS) * estate.awayCenterSpeed; //awayFromCenter
	velocity += float3(0,1,0) * estate.alongAxisSpeed; //alongAxis  Must be rotated by matrix

	//randomDirection
	//aroundAxis
	//speedRandom

	position = mul(float4(positionLS, 1.0), emitterData.world).xyz; //Convert position from local space to world space
}

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeTestSpawn(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	if (pSrt->m_numEmitterStates == 0)
		return;

	float rate = pSrt->m_emitterStates[0]->m_computeEmitterState.spawnRate;
	float interval = 1/rate;
	float modTime = pSrt->m_time % (1/rate);
	float modTimeMinusDelta = modTime - pSrt->m_delta;
	int numPartsToSpawnThisFrame = (modTimeMinusDelta < 0) ? 1 + floor(abs(modTimeMinusDelta)/interval) : 0;
	if( dispatchId < numPartsToSpawnThisFrame ) //should also calculate how many particles should be spawned and check if dispatchId is < that number
	{

		float rand0 = GetRandom(pSrt->m_gameTicks/* + float(bit_cast<uint>(pSrt))*/, dispatchId.x, 0);  // float((pSrt->m_gameTicks * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);
		float rand1 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 1);  // float(((pSrt->m_gameTicks ^ 0x735fac31) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);
		float rand2 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 2);  // float(((pSrt->m_gameTicks ^ 0x735fac31) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);
		float rand3 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 3);  // float(((pSrt->m_gameTicks ^ 0x735fac31) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);

		// we use original particle to check whether we want to be spawning things
		//uint origParticleIndex = pSrt->m_particleIndicesOrig[1];
		//ParticleInstance originalParticle = pSrt->m_particleInstancesOrig[origParticleIndex];

		uint dsize;
		pSrt->m_particleIndicesOther.GetDimensions(dsize);

		if (dsize == 0)
			return;

		uint randParticleId = (rand3 * dsize) % dsize;

		uint origParticleIndex = pSrt->m_particleIndicesOther[randParticleId];
		ParticleInstance originalParticle = pSrt->m_particleInstancesOther[origParticleIndex];

		originalParticle.world[3].xyz = originalParticle.world[3].xyz + pSrt->m_altWorldOrigin.xyz;

		//float3 topPosWs = mul(float4(rand0 - 0.5, 0.5, rand1 - 0.5, 1.0), originalParticle.world).xyz;

		float3 position = float3(0,0,0);
		float3 velocity = float3(0,0,0);

		if (pSrt->m_numEmitterStates > 0)
			EmitterSpawn(pSrt->m_emitterStates[0]->m_computeEmitterState, originalParticle, rand0, rand1, rand2, position, velocity);

		uint gdsOffsetId = pSrt->m_gdsOffsetIdCounter;
		uint particleId = uint(NdAtomicIncrement(gdsOffsetId));


		// Adds particle to the atomic buffer or retrun not doing anything
		NewStandardParticle( position, velocity, pSrt, dispatchId, particleId, /*birthTimeOffset=*/ 0.001f, /*spawnData*/ rand1 * 3.1415 * 8, /*lifetime=*/ 5.0, randParticleId);
	}
}



[NUM_THREADS(64, 1, 1)]
void CS_ParticleSampleRenderTargets(const uint3 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint3 groupId : SV_GroupID,
	ParticleRTSamplingSrt *pSrt : S_SRT_DATA)
{
	uint testIndex = groupId.x;
	uint sampleIndex = groupThreadId.x;


	Dc_PartSamplingUvs samplingUvs = pSrt->m_samplingProfiles[testIndex][sampleIndex];

	float sum = 0;
	for (int i = 0; i < samplingUvs.m_floatCount; ++i)
	{
		float2 myUv =samplingUvs.m_floats[i];
		float value = pSrt->m_textures[testIndex].SampleLevel(pSrt->m_linearSampler, myUv, 0).x;
		sum += value;
	}

	float avg = samplingUvs.m_floatCount > 1 ? (sum / samplingUvs.m_floatCount) : sum;

	uint resultIndex = pSrt->m_resultIndices[testIndex];

	pSrt->m_results[resultIndex].m_results[sampleIndex] = avg;
}





//#include "particle-ribbon-compute-jobs.fx"
//#include "particle-drops-compute-jobs.fx"
