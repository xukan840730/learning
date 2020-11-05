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

#define kMIPSPACING 2

float4 SampleWorldSpaceHeightVectorField(ParticleComputeJobSrt *pSrt, float3 p_ws, float3 t_center, float3 t_size, uint numMips)
{
	//xform to bbox space
	float3 uvw; 
	uvw.xz = p_ws.xz - t_center.xz;
	uvw.xz = (uvw.xz + t_size.xz * 0.5f) / t_size.xz;

	//return 0 outside of bbox
	if(uvw.x < 0 || uvw.z < 0 || uvw.x > 1 || uvw.z > 1 )
		return float4( 0 , -1.0, 0, 0);

	//sample rg8 heightmap and convert to ws
	float2 height_tex = pSrt->m_materialTexture1.SampleLevel(pSrt->m_linearSampler, uvw.xz, 0).xy;
	float height16 = height_tex.x + height_tex.y/255.0;
	float height_ws = t_center.y - (t_size.y * 0.5) + (height16 * t_size.y);
	float height_dist = numMips * kMIPSPACING;
	float height_max = height_ws + height_dist;
	// return 0 above/below height range
	if (p_ws.y < height_ws || p_ws.y > t_center.y+t_size.y*0.5)
		return float4( 0 , -1.0, 0, 0);

	//heightmap space 
	uvw.y = p_ws.y - height_ws / height_dist;

	//get mips and sample
	float mip = clamp(floor(uvw.y * numMips), 0, numMips);
	float blend = frac(uvw.y * numMips);
	float3 tex_0 = pSrt->m_materialTexture0.SampleLevel(pSrt->m_linearSampler, uvw.xz, mip).xyz;
	float3 tex_1 = pSrt->m_materialTexture0.SampleLevel(pSrt->m_linearSampler, uvw.xz, mip+1).xyz;

	float3 vel = lerp(tex_0, tex_1, blend) * 2 - 1;

	return float4(vel, 1.0);
}

float4 SampleTextureAtOrigin(ParticleComputeJobSrt *pSrt, float3 p_ws, float size, float mips)
{
	float2 texUv = saturate((p_ws.xz + (size * 0.5))/size);
	float mip = saturate((p_ws.y + (size * 0.5))/size)*mips;
	float base = floor(mip);
	float blend = frac(mip);
	float3 tex_0 = pSrt->m_materialTexture0.SampleLevel(pSrt->m_linearSampler, texUv, base).xyz ;
	float3 tex_1 = pSrt->m_materialTexture0.SampleLevel(pSrt->m_linearSampler, texUv, base+1).xyz ;
	float3 result = lerp(tex_0* 2.0f - 1.0f, tex_1* 2.0f - 1.0f, blend) ;
	return float4(result, 1.0);
}

float ScreenDepthVsFromNDC(ParticleComputeJobSrt *pSrt, float3 p_ndc)
{
	uint2 sspos = uint2((p_ndc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (p_ndc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);
	float screenDepthVs = GetLinearDepth(pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos], pSrt->m_depthParams);
	return screenDepthVs;
}

void ResolveCollision(ParticleComputeJobSrt *pSrt, inout ParticleStateV0 state)
{
	// calculate expected depth
	float4 p_h = mul(float4(state.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
	float3 p_ndc = p_h.xyz / p_h.w;

	if (p_ndc.x < -1 || p_ndc.y < -1 || p_ndc.x > 1 || p_ndc > 1 || p_ndc.z < -1)
	{
		//ignore particles outside screen
		return;
	}
	float screenDepthVs = ScreenDepthVsFromNDC(pSrt, p_ndc);

	if(p_h.w < screenDepthVs)
	{
		//ignore particles infront of depth
		return;
	}

	if((p_h.w - screenDepthVs) > 1)
	{
		//ignore depth not near the particle
		return;
	}


	float3 backstep_ws = state.m_pos - state.m_scale * pSrt->m_delta;
	float4 backstep_h = mul(float4(backstep_ws, 1), pSrt->m_pPassDataSrt->g_mVP);
	float3 backstep_ndc = backstep_h.xyz / backstep_h.w;

	if (backstep_ndc.x < -1 || backstep_ndc.y < -1 || backstep_ndc.x > 1 || backstep_ndc > 1 || backstep_ndc.z < -1)
	{
		//ignore particles outside the frustum last step
		return;	
	}

	float backstepScreenDepth = ScreenDepthVsFromNDC(pSrt, backstep_ndc);

	if(backstep_h.w > backstepScreenDepth)
	{
		//ignore particles behind depth last step
		return;
	}

	//move back and adjust velocity
	state.m_pos = backstep_ws;
	state.m_scale = lerp(state.m_scale, float3(0,length(state.m_scale)*0.6,0), 0.5);
	if(length(state.m_scale) < 0.1)
		state.m_flags0 = 1;

}


//===========================================================================
//Remove old particles and swap states to new list
//===========================================================================
[NUM_THREADS(64, 1, 1)]
void CS_ParticleWindCullSwap(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{

	//Set the counter
	uint gdsOffsetOld = pSrt->m_gdsOffsetCounterOld;
	uint numOldParticles = NdAtomicGetValue(gdsOffsetOld);

	if (dispatchId.x >= numOldParticles)
		return; // todo: dispatch only the number of threads = number of particles

	// grab my particle
	ParticleStateV0 state = pSrt->m_particleStatesOld[dispatchId.x];

	//kill below bbox
	if (state.m_pos.y < (pSrt->m_rootComputeVec2.y - (pSrt->m_rootComputeVec3.y * 0.5)))
		return;

	float age = (pSrt->m_time - state.m_birthTime);

	// if the age isn't maxed or the game is paused, copy the data and increment the counter
	if (age <= state.m_lifeTime || pSrt->m_delta < 0.00001)
	{
		// write the state out to new buffer
		uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
		uint particleIndex = NdAtomicIncrement(gdsOffsetNew);

		//state.m_rotation = float3(0,1,0);
		//state.m_speed = pSrt->m_rootComputeVec0.z;

		pSrt->m_particleStates[particleIndex] = state;
		//Note: This works because the counter is zero'd before this job runs
		//Particles that aren't valid are skipped
	}

}

//===========================================================================
//Remove update particle positions, do collisions
//===========================================================================
[NUM_THREADS(64, 1, 1)]
void CS_ParticleWindUpdate(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	//Get the current particle
	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint numParticles = NdAtomicGetValue(gdsOffsetNew);
	if (dispatchId.x >= numParticles)
		return;

	ParticleStateV0 state = pSrt->m_particleStates[dispatchId.x];

	if (state.m_flags0 > 0)
		return;

	//color 
	state.m_rotation = float3(0,1,0);//1-(age/state.m_lifeTime);

	//scale
	state.m_speed = pSrt->m_rootComputeVec0.z;


	//gather forces
	float3 force = 0;

	 //gravity
	force.y += -2.5f;

	//vector field force
	float4 result = SampleWorldSpaceHeightVectorField(pSrt, state.m_pos, pSrt->m_rootComputeVec2.xyz, pSrt->m_rootComputeVec3.xyz, pSrt->m_rootComputeVec1.x);
	force +=  result.xyz * pSrt->m_rootComputeVec1.y *result.w;

	//drag
	float speed = length(state.m_scale);
	force -= normalize(state.m_scale) * speed * speed * 0.5;

	
	//velocity blend
	//state.m_scale = lerp(state.m_scale.xyz, result.xyz * pSrt->m_rootComputeVec1.y, lerp(pSrt->m_delta, 1, sqrt(length(result.xyz)*result.w))*state.m_id);
	
	//integrate velocity
	state.m_scale += force * pSrt->m_delta * state.m_id;


	//integrate positions
	state.m_pos += state.m_scale * pSrt->m_delta;

	//screenspace collisions
	ResolveCollision(pSrt, state);

	float blend = sqrt(length(result.xyz));
	state.m_rotation = lerp(1.0, float3(1,0,0), blend);

	pSrt->m_particleStates[dispatchId.x] = state;

}


void SpawnParticleInBox(ParticleComputeJobSrt *pSrt, uint particleIndex, uint2 dispatchId)
{

	// get the original particle to use as a bbox
	uint origParticleIndex = pSrt->m_particleIndicesOrig[0];
	ParticleInstance originalParticle = pSrt->m_particleInstancesOrig[origParticleIndex];
	originalParticle.world[3].xyz = originalParticle.world[3].xyz + pSrt->m_altWorldOrigin.xyz;

	//random position -1 to 1 in object space
	float3 positionWS;
	positionWS.x = GetRandom(pSrt->m_gameTicks, dispatchId.x, 1) - 0.5;	
	positionWS.y = GetRandom(pSrt->m_gameTicks, dispatchId.x, 2) - 0.5;	
	positionWS.z = GetRandom(pSrt->m_gameTicks, dispatchId.x, 3) - 0.5;	

	positionWS = mul(float4(positionWS.xyz, 1), originalParticle.world);

	float3 velocity = 0;
	//velocity.x = GetRandom(pSrt->m_gameTicks, dispatchId.x, 4) * 2 - 1;	
	//velocity.y = GetRandom(pSrt->m_gameTicks, dispatchId.x, 5) * 2 - 1;	
	//velocity.z = GetRandom(pSrt->m_gameTicks, dispatchId.x, 6) * 2 - 1;

	ParticleStateV0 state = ParticleStateV0(0);
	state.m_birthTime = pSrt->m_time;
	state.m_pos = positionWS;
	state.m_lifeTime = pSrt->m_rootComputeVec0.y;
	state.m_scale = velocity;
	state.m_speed = 0;
	state.m_id = 0.7+0.45*GetRandom(pSrt->m_gameTicks, dispatchId.x, 4);
	pSrt->m_particleStates[particleIndex] = state;
}

//======================
//Spawn new particles!
//======================
[NUM_THREADS(64, 1, 1)]
void CS_ParticleWindSpawnNew(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{

	//Spawn a number of particles per frame, rootvec1.x = parts per second
	float probability = GetRandom(pSrt->m_gameTicks, dispatchId.x, 0);
	float condition =  (pSrt->m_rootComputeVec0.x * pSrt->m_delta)/*parts per frame*/ / 64.0 /*total threads*/;

	if (probability > condition)
	{
		return;
	}

	//If particle is spawning, first attempt to increment counter, if out of space, cancel
	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint particleIndex = NdAtomicIncrement(gdsOffsetNew);

	uint size, stride;
	pSrt->m_particleStates.GetDimensions(size, stride);

	if (particleIndex >= size)
	{
		// decrement back
		NdAtomicDecrement(gdsOffsetNew);
		return;
	}

	SpawnParticleInBox(pSrt, particleIndex, dispatchId);

}

//========================================================
//create renderables
//========================================================
[NUM_THREADS(64, 1, 1)]
void CS_ParticleWindGenerateRenderables(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	//Get the current particle
	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint numParticles = NdAtomicGetValue(gdsOffsetNew);
	if (dispatchId.x >= numParticles)
		return;

	ParticleStateV0 state = pSrt->m_particleStates[dispatchId.x];

	float4x4 g_identity = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };
	float3   pos 		= state.m_pos;

	ParticleInstance inst = ParticleInstance(0);
	inst.world = g_identity;					// The object-to-world matrix
	inst.color = float4(1, 1, 1, 1);			// The particle's color
	inst.texcoord = float4(1, 1, 0, 0);			// Texture coordinate scale and bias (uv = uv0 * texcoord.xy + texcoord.zw)
	inst.userh = float4(0, 0, 0, 0);			// User attributes (used to be half data type)
	inst.userf = float4(0, 0, 0, 0);			// User attributes
	inst.partvars = float4(0, 0, 0, 0);			// Contains age, normalized age, ribbon distance, and frame number
	inst.invscale = float4(1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f);		// Reciprocal of the particle's half-scale vector

	float age = (pSrt->m_time - state.m_birthTime);
	float3 toCam = pSrt->m_cameraDirWs;
	//float3 dir = float3(0,0,1);//


	float3 dir = -normalize(state.m_scale);
	dir = lerp( float3(0,1,0), dir, length(state.m_scale)>0.001);

	inst.world = TransformFromLookAt(toCam, dir, pos, true);



	inst.color.xyz = state.m_rotation;

	float3 scale = state.m_speed.xxx;
	inst.world[0].xyz = inst.world[0].xyz * scale.x;
	inst.world[1].xyz = inst.world[1].xyz * scale.y;
	inst.world[2].xyz = inst.world[2].xyz * scale.z;
	inst.invscale = float4(1.0f / (scale.x * 0.5), 1.0f / (scale.y * 0.5), 1.0f / (scale.z * 0.5), 1.0f / 1);

	float4 posOffest = mul(float4(float3(0,0,0), 1), inst.world);
	// modify position based on camera
	inst.world[3].xyz = posOffest.xyz - pSrt->m_altWorldOrigin.xyz;

	inst.prevWorld = inst.world;
	pSrt->m_particleInstances[dispatchId.x] = inst;
	pSrt->m_particleIndices[dispatchId.x] = dispatchId.x;

}
