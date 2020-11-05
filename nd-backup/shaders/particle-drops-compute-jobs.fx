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
void CS_ParticleComputeDropsSpawnNew(const uint2 dispatchId : SV_DispatchThreadID,
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

	if (groupThreadId.x >= 0)
	{
			return;
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
	uint particleId = uint(NdAtomicIncrement(gdsOffsetId)) % 32; // we rely on ribbons to die before we can spawn them again. Todo: use the data buffer to read how many particles we have in ribbon already


																			   // will add particle to the atomic buffer or retrun not doing anything
	float3 posNdc = float3(sspos.x / pSrt->m_screenResolution.x * 2 - 1, -(sspos.y / pSrt->m_screenResolution.y * 2 - 1), pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos]);
	float4 posH = mul(float4(posNdc, 1), pSrt->m_pPassDataSrt->m_mAltVPInv);
	float3 posWs = posH.xyz / posH.w;
	posWs += pSrt->m_altWorldOrigin;

	ParticleStateV0 addedState;
	int resultIndex;

	StickyParticleParams spawnParams = DefaultStickyParticleParams();
	spawnParams.allowSkin = true;

	NewStickyParticle(kMotionStateFreeFallCollide, topSspos, topPosNdc, topPosWs, 0, stencil, pSrt, dispatchId, particleId, 
		/*checkInsideFirstParticle=*/ false, /*birthTimeOffset=*/ 0.001f, /*spawnData*/ rand1 * 3.1415 * 8, 
		/*useProvidedIndex*/ false, /*providedParticleIndex=*/ -1, groupThreadId.x, /*lifetime=*/ 20.0, addedState, 
		/*normalHalfWayWithRain=*/ false, /*inheritedVelocity=*/ float3(0, 0, 0), /*isSnow*/ false, spawnParams, /*defaultSpeed*/ 1.0f, resultIndex, /*customSize*/ 0);

	//NewStickyParticle(0, sspos, posNdc, posWs, 0, stencil, pSrt, dispatchId, particleId, /*checkInsideFirstParticle=*/ false, /*birthTimeOffset=*/ 0.001f, /*spawnData*/ -0.5f, /*useProvidedIndex*/ false, /*providedParticleIndex=*/ -1, groupThreadId.x, /*lifetime=*/ 10.0, /*defaultSpeed*/ 1.0f, /*customSize*/ 0);
}



[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeDropsSpawnNewFromList(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// check if we have anything available in the list

	uint gdsOffsetOther = pSrt->m_gdsOffsetOther;
	uint numHints = NdAtomicGetValue(gdsOffsetOther);


	if (dispatchId.x >= numHints)
		return;


	ParticleStateV0 hint = pSrt->m_particleStatesOther[dispatchId.x];

	// project the hint into screen space and from there just do the normal spawning


	float4 hintPosH = mul(float4(hint.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
	float3 hintPosNdc = hintPosH.xyz / hintPosH.w;

	// get screen space position of the hint
	uint2 hintsspos = uint2((hintPosNdc.x * 0.5 + 0.5) * pSrt->m_screenResolution.x, (1.0 - (hintPosNdc.y * 0.5 + 0.5)) * pSrt->m_screenResolution.y);


	// do 64 random samples in the cell to generate data about this cell. then use this data for spawning splashes in this cell
	float rand0 = GetRandom(pSrt->m_gameTicks/* + float(bit_cast<uint>(pSrt))*/, dispatchId.x, 0);  // float((pSrt->m_gameTicks * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);
	float rand1 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 1);  // float(((pSrt->m_gameTicks ^ 0x735fac31) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);


	//uint2 myCellPos = groupId * uint2(pSrt->m_gridCellW, pSrt->m_gridCellH);
	uint2 sspos = hintsspos; // myCellPos + uint2(pSrt->m_gridCellW * rand0, pSrt->m_gridCellH * rand1);

	float depthVs = GetLinearDepth(pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos], pSrt->m_depthParams);

	float probability = ProbabilityBasedOnDistance(depthVs) * 10 * pSrt->m_rootComputeVec1.x;

	float rand2 = GetRandom(pSrt->m_gameTicks, dispatchId.x, 2);  // float(((pSrt->m_gameTicks ^ 0xac31735f) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);

	if (rand2 >= pSrt->m_rootComputeVec1.x && hint.m_id == 0) // hint.m_id == 0 is for droplets from blood trails
	{
		// decided not to add
		return;
	}

	if (hint.m_id == 1)
	{
		// m_speed stores rate. we don't have any persistent data we store per spawner, but we can convert rate into probability
		// rate of 30 should be probability 1.0
		// rate of 15 0.5
		// rate of 10 0.333
		float rateProbability = saturate(hint.m_speed / 30.0f);
		if (rand2 > rateProbability || rateProbability == 0)
		{
			return;
		}
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


	topSspos = hintsspos;
	topPosNdc = hintPosNdc;
	topPosWs = hint.m_pos;

	uint stencil = pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil[topSspos];
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
	posWs += pSrt->m_altWorldOrigin;

	float3 inheritedVelocity = hint.m_scale;

	//inheritedVelocity = float3(0, 0, 0);

	ParticleStateV0 addedState;

	//NewFallingParticle(kMotionStateFreeFallCollide, topSspos, topPosNdc, topPosWs, stencil, pSrt, dispatchId, particleId, /*checkInsideFirstParticle=*/ false, /*birthTimeOffset=*/ 0.001f, /*spawnData*/ 0 * rand1 * 3.1415 * 8, /*useProvidedIndex*/ false, /*providedParticleIndex=*/ -1, groupThreadId.x, /*lifetime=*/ 20.0,
	//	inheritedVelocity,
	//	addedState);

	if (hint.m_id == 0)
	{
		int resultIndex;

		StickyParticleParams spawnParams = DefaultStickyParticleParams();
		spawnParams.allowSkin = true;

		bool spawnedNew = NewStickyParticle(kMotionStateAttached, sspos, topPosNdc, posWs,/*defaultFlags1=*/ hint.m_flags1, stencil, pSrt, dispatchId, particleId, 
			/*checkInsideFirstParticle=*/ false, /*birthTimeOffset=*/ 0.001f, /*spawn data*/ 0, /*useProvidedIndex*/ false, /*providedParticleIndex=*/-1, groupThreadId.x, 
			/*lifetime=*/ 4.0f, addedState, /*normalHalfWayWithRain=*/ false, /*inheritedVelocity=*/ float3(0, 0, 0), /*isSnow*/ false, spawnParams, /*defaultSpeed*/ 1.0f, resultIndex, /*customSize*/ 0);
	}
	else
	{
		// falling state
		// did not recalculate posWs based on screen space


		//hint.m_flags1 might tell us whether we want to spawn 3d ribbon drips
		float adjustAgeLifetime = pSrt->m_rootComputeVec3.y; // we start age at full grown

		int resultIndex;

		StickyParticleParams spawnParams = DefaultStickyParticleParams();
		spawnParams.allowSkin = true;

		bool spawnedNew = NewStickyParticle(kMotionStateFreeFallCollide, hintsspos, hintPosNdc, hint.m_pos, /*defaultFlags1=*/hint.m_flags1, stencil, pSrt, dispatchId, particleId, /*checkInsideFirstParticle=*/ false, /*birthTimeOffset=*/ -adjustAgeLifetime,
			/*spawn data*/ 0, /*useProvidedIndex*/ false, /*providedParticleIndex=*/-1, groupThreadId.x, /*lifetime=*/ 4.0f + adjustAgeLifetime, addedState, /*normalHalfWayWithRain=*/ false, /*inheritedVelocity=*/ inheritedVelocity, /*isSnow*/ false, spawnParams, /*defaultSpeed*/ 1.0f, resultIndex, /*customSize*/ 0);
	}
	

	//state.m_direction.x = 0;
	//NewStickyParticle(0, sspos, posNdc, posWs, 0, stencil, pSrt, dispatchId, particleId, /*checkInsideFirstParticle=*/ false, /*birthTimeOffset=*/ 0.001f, /*spawnData*/ -0.5f, /*useProvidedIndex*/ false, /*providedParticleIndex=*/ -1, groupThreadId.x, /*lifetime=*/ 10.0, /*defaultSpeed*/ 1.0f, /*customSize*/ 0);
}



[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeSpawnDrops(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// each group works on one ribbon

	// we only spawn new particle when the whole trail is dead, i.e. first element in the list is the sentinel

	//_SCE_BREAK();

	//if (dispatchId != 0)
	//	return;
	//
	
	uint origSize;
	pSrt->m_particleIndicesOrig.GetDimensions(origSize);

	if (dispatchId >= origSize)
		return;

	uint origParticleIndex = pSrt->m_particleIndicesOrig[dispatchId];
	ParticleInstance originalParticle = pSrt->m_particleInstancesOrig[origParticleIndex];

	originalParticle.world[3].xyz = originalParticle.world[3].xyz + pSrt->m_altWorldOrigin.xyz;

	uint gdsOffsetNew = pSrt->m_gdsOffsetOther;
	uint size, stride;
	pSrt->m_particleStatesOther.GetDimensions(size, stride);


	{
		// we use a additional buffer of particle states to write data to tell the otehr system to spawn particles. It doesn't have to be a buffer of states, but we just use it.

		uint newInfoIndex = NdAtomicIncrement(gdsOffsetNew);
		if (newInfoIndex >= size)
		{
			// decrement back
			NdAtomicDecrement(gdsOffsetNew);

			return; // can't add new information
		}

		float cone = 3.1415 / 4;
		float h = sin(cone);
		float rand0 = GetRandom(pSrt->m_gameTicks/* + float(bit_cast<uint>(pSrt))*/, dispatchId.x, 0);  // float((pSrt->m_gameTicks * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);
		float angle = rand0 * 3.1415 * 2;
		float x = cos(angle);
		float y = sin(angle);
		float rand1 = GetRandom(pSrt->m_gameTicks/* + float(bit_cast<uint>(pSrt))*/, dispatchId.x, 1);  // float((pSrt->m_gameTicks * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);

		float3 v = float3(x * rand1 * h, y * rand1 * h, 1);
		v = normalize(v);



		ParticleStateV0 infoState = ParticleStateV0(0);

		infoState.m_pos = originalParticle.world[3].xyz;

		float3 xaxis = originalParticle.world[0].xyz * originalParticle.invscale.xyz * float3(0.5f);
		float3 yaxis = originalParticle.world[2].xyz * originalParticle.invscale.xyz * float3(0.5f);
		float3 zaxis = originalParticle.world[2].xyz * originalParticle.invscale.xyz * float3(0.5f);

		infoState.m_scale = originalParticle.world[2].xyz * originalParticle.invscale.xyz * float3(0.5f);
		infoState.m_scale = xaxis * v.x + yaxis * v.y + zaxis * v.z;
		infoState.m_speed = originalParticle.color.x; // encode spawn rate. we will use this for generating probability
		infoState.m_id = 1; // means free falling

		
		bool wantGeoRibbon = (pSrt->m_features & COMPUTE_FEATURE_GENERATE_COLLISION_EVENTS_GEO_RIBBON);
		bool wantRenderTargetSplat = (pSrt->m_features & COMPUTE_FEATURE_GENERATE_BLOOD_MAP_EVENTS);
		bool wantSpawnTrackDecals = (pSrt->m_features & COMPUTE_FEATURE_GENERATE_COLLISION_EVENTS_POOL_DECAL);

		infoState.m_flags1 = (wantGeoRibbon ? kMaterialMaskSpawnChildRibbons : 0) | (wantRenderTargetSplat ? kMaterialMaskSpawnRTSplats : 0) | (wantSpawnTrackDecals ? kMaterialMaskSpawnTrackDecals : 0); // enable spawning of child ribbon drips

		pSrt->m_particleStatesOther[newInfoIndex] = infoState;
	}
}


[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeDropsUpdateCompress(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// grab my particle
	uint gdsOffsetOld = pSrt->m_gdsOffsetCounterOld;
	uint numOldParticles = NdAtomicGetValue(gdsOffsetOld);

	//if (dispatchId.x >= numOldParticles)
	//	return; // todo: dispatch only the number of threads = number of particles

	StructuredBuffer<SnowParticleState> castedBufferOld = __create_buffer< StructuredBuffer<SnowParticleState> >(__get_vsharp(pSrt->m_particleStatesOld));
	//ParticleStateV0 state = pSrt->m_particleStatesOld[dispatchId.x];

	//if (state.m_flags0 >= 100)
	//{
	//	return; // too much accumulated error
	//}
	StickyUpdateRes res;
	uint m_flags1 = 0;
	if (dispatchId.x >= numOldParticles)
	{
		res = StickyUpdateRes(0);
	}
	else
	{
		SnowParticleState state = castedBufferOld[dispatchId.x];
		uint oldFlags1 = state.m_flags1;
		uint motionState = (state.m_flags1 >> 8) & 0x000000FF;

		StickyParticleParams params = DefaultStickyParticleParams();

		res = UpdateStickyParticleMotion(dispatchId, groupThreadId, groupId, pSrt,
			/*inout ParticleStateV0 state*/ state,
			/*inout uint particleErrorState*/ state.m_flags0,
			/*uint particleIndex*/ dispatchId.x,
			/*uint prevStencil*/ state.m_flags1 & 0x000000FF,
			/*uint motionState*/ motionState,
			/*inout float frameCounter*/ state.m_id, /*const uint kNumOfFramesToRecover=*/ 10, pSrt->m_rootComputeVec4.x, 0.0, pSrt->m_rootComputeVec4.y, /*isSnow*/ false, params);



		float age = (pSrt->m_time - state.m_birthTime);


		if (motionState == kMotionStateAttached && age > pSrt->m_rootComputeVec3.x)
		{
			// go to drop state
			uint newMotionState = kMotionStateFreeFallCollide;
			state.m_flags0 = 0; // clear out error
			state.m_flags1 = (state.m_flags1 & 0xFFFF00FF) | (newMotionState << 8);
			
			//state.m_direction.xyz = float3(0, 0, 0);
			//state.m_direction.x = 0; // zero it out, is used for speed

									 //state.m_birthTime = pSrt->m_time;
		}

		//if (state.m_flags0 >= 10 && state.m_flags0 < 100)
		if (state.m_flags0 > 0 && state.m_flags0 < 10)
		{
			//return; // too much accumulated error
			uint newMotionState = kMotionStateFreeFallCollide; // kMotionStateDeadFreeFall;
			state.m_flags0 = 0; // clear out error
			state.m_flags1 = (state.m_flags1 & 0xFFFF00FF) | (newMotionState << 8);
			state.m_direction.xyz = float3(0, 0, 0);
			state.m_direction.x = 0; // zero it out, is used for speed
		}

		if (state.m_flags0 >= 10)
		{
			//return; // too much accumulated error
		}
		else
		{
			// write the state out to new buffer
			uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
			uint particleIndex = NdAtomicIncrement(gdsOffsetNew);

			RWStructuredBuffer<SnowParticleState> castedBuffer = __create_buffer< RWStructuredBuffer<SnowParticleState> >(__get_vsharp(pSrt->m_particleStates));
			castedBuffer[particleIndex] = state;
			//pSrt->m_particleStates[particleIndex] = state;
		}
		m_flags1 = state.m_flags1;
	}

	if (m_flags1 & kMaterialMaskSpawnTrackDecals)
	{
		if (res.m_haveCollision)
		{
			// todo: like below, make this search parallel instead of brute force

			// this writes blood splat
			AddCollisionFeedBackSpawn(pSrt, res.m_sspos, res.m_pos, res.m_normal, dispatchId.x, groupThreadId.x,
				/*spawnDecal=*/res.m_isFg == 0,
				/*spawnBloodMap=*/ /*res.m_isFg*/ false);
		}
	}

	if (!((m_flags1 & kMaterialMaskSpawnChildRibbons) || (m_flags1 & kMaterialMaskSpawnRTSplats)));
	{
		//res.m_haveCollision = false; // nop need to go further if we don't want either of these collision spawns
	}

	{

		ulong exec = __v_cmp_eq_u32(res.m_haveCollision, true);

		// go through all collided lanes
		while (exec)
		{
			uint first_bit = __s_ff1_i32_b64(exec);

			exec = __s_andn2_b64(exec, __s_lshl_b64(1, first_bit));

			uint2 uSSPos;
			uSSPos.x = __v_readlane_b32(res.m_sspos.x, first_bit);
			uSSPos.y = __v_readlane_b32(res.m_sspos.y, first_bit);

			float3 uPos;
			uPos.x = __v_readlane_b32(res.m_pos.x, first_bit);
			uPos.y = __v_readlane_b32(res.m_pos.y, first_bit);
			uPos.z = __v_readlane_b32(res.m_pos.z, first_bit);

			float3 uNormal;
			uNormal.x = __v_readlane_b32(res.m_normal.x, first_bit);
			uNormal.y = __v_readlane_b32(res.m_normal.y, first_bit);
			uNormal.z = __v_readlane_b32(res.m_normal.z, first_bit);

			uint uFlags1 = __v_readlane_b32(m_flags1, first_bit);

			uint uIsFg;
			uIsFg = __v_readlane_b32(res.m_isFg, first_bit);

			//if (uIsFg == 0x20)
			{
				bool wantGeoRibbon = uFlags1 & kMaterialMaskSpawnChildRibbons;
				bool wantRenderTargetSplat = uFlags1 & kMaterialMaskSpawnRTSplats;

				AddFeedBackSpawn(pSrt, uSSPos, uPos, groupThreadId.x, dispatchId.x, 0, /*bloodTrail=*/false, 0, /*bloodSplat=*/wantRenderTargetSplat, /*bloodDrip*/ wantGeoRibbon && false, /*colorIntensity=*/1.0, /*colorOpacity=*/1.0, false, float2(0));
			}
		}
	}
}




[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeDropsGenerateRenderables(const uint2 dispatchId : SV_DispatchThreadID,
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

	ParticleInstance inst = ParticleInstance(0);

	inst.world = g_identity;			// The object-to-world matrix
	inst.color = float4(pSrt->m_rootComputeVec2.xyz, 1);			// The particle's color


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
	inst.userh = float4(0.0, 0.0, 0.0, 0);			// User attributes (used to be half data type)
	inst.userf = float4(0.0, 0, 0.0, 0.0);			// User attributes
	inst.partvars = float4(0, 0, 0, 0);		// Contains age, normalized age, ribbon distance, and frame number
	inst.invscale = float4(1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f);		// Reciprocal of the particle's half-scale vector

	float age = (pSrt->m_time - state.m_birthTime);

	inst.userh.x = age; // *kFps;

	inst.color.a = 1;

	// debug
	//inst.color = float4(1, 1, 1, 1);

	//if (age > 0.5)
	//{
	//	inst.color = float4(1, 0, 0, 1);
	//}
	//if (age > 1.0)
	//{
	//	inst.color = float4(0, 1, 0, 1);
	//}
	//if (age > 1.5)
	//{
	//	inst.color = float4(0, 0, 1, 1);
	//}

	float3 toCam = normalize(pSrt->m_cameraPosWs.xyz - pos);

	float coss = cos(state.m_data * 0.5);
	float sinn = sin(state.m_data * 0.5);

	//float4x4 twist;
	//twist[0].xyzw = float4(coss, sinn, 0, 0);
	//twist[1].xyzw = float4(-sinn, coss, 0, 0);
	//twist[2].xyzw = float4(0, 0, 1, 0);
	//twist[3].xyzw = float4(0, 0, 0, 1);
	//
	//
	//
	//
	//inst.world = mul(twist, TransformFromLookAt(toCam, state.m_rotation.xyz, pos, true));

	float oneFrameYMotion = abs(state.m_scale.y * (pSrt->m_delta > 0.001 ? pSrt->m_delta : 0.0333) * 1);
	float3 oneFrameMotion = state.m_scale.xyz * (pSrt->m_delta > 0.001 ? pSrt->m_delta : 0.0333) * 1;
	float3 velocityNorm = normalize(oneFrameMotion);

	inst.world = TransformFromLookAt(toCam, state.m_rotation.xyz, pos, true);
	inst.world = TransformFromLookAt(normalize(toCam), velocityNorm, pos, false);

	// for crawlers: use this for aligning the sprite with velocity direction
	//inst.world = TransformFromLookAtYFw(state.m_scale.xyz, state.m_rotation.xyz, pos, true);

	float3 scale = float3(pSrt->m_rootComputeVec0.xyz) * 0.5;

	//if (motionState == kMotionStateAttached)
	{
		
	}
	//else
	{

	}

	
	scale.x = 0.005;
	scale.z = 1.0;
	scale.y = oneFrameYMotion;

	scale.y = (motionState == kMotionStateAttached) ? 0.01 : max(0.01, oneFrameYMotion);
	scale *= (0.0 + 1.0 * saturate(age / (pSrt->m_rootComputeVec3.y)));

	//if (motionState == kMotionStateAttached  || motionState == kMotionStateDeadFreeFall)
	//{
	//	scale *= (0.5 + 0.5 * (1.0 - age / 5.0));
	//
	//	inst.color.a *= (1.0 - age / 5.0);
	//}
	

	inst.world[0].xyz = inst.world[0].xyz * scale.x;
	inst.world[1].xyz = inst.world[1].xyz * scale.y;
	inst.world[2].xyz = inst.world[2].xyz * scale.z;

	inst.invscale = float4(1.0f / (scale.x * 0.5), 1.0f / (scale.y * 0.5), 1.0f / (scale.z * 0.5), 1.0f / 1);

	float3 renderOffset = pSrt->m_renderOffset.xyz;
	renderOffset.x = -0.25f;
	renderOffset.z = 0.5f;

	renderOffset = float3(0, 0, 0);
	float4 posOffest = mul(float4(renderOffset, 1), inst.world);

	// modify position based on camera
	inst.world[3].xyz = posOffest.xyz - pSrt->m_altWorldOrigin.xyz;

	//partMat.SetRow(3, MulPointMatrix(offset.GetVec4(), partMat));

	inst.prevWorld = inst.world;		// Last frame's object-to-world matrix
	
	inst.prevWorld[3].xyz -= state.m_scale.xyz * pSrt->m_delta * 1;

	pSrt->m_particleInstances[destinationIndex] = inst;
	pSrt->m_particleIndices[destinationIndex] = destinationIndex;
}

