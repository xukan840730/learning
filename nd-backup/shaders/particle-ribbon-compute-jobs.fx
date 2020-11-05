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

#ifndef TRACK_BIND_POSE
#define TRACK_BIND_POSE 1
#endif

#define kNumPartsInRibbon 64

//#define RIBBON_ROOT_VAR_SPEED(ribbonType) 0.1
#define RIBBON_ROOT_VAR_SPEED(ribbonType) ((ribbonType) ? (pSrt->m_rootComputeVec4.y * 2) : (pSrt->m_rootComputeVec4.x * 2))

//#define RIBBON_ROOT_VAR_LIFETIME 2.0
#define RIBBON_ROOT_VAR_LIFETIME (pSrt->m_rootComputeVec1.z * 0.2)

#define RIBBON_ROOT_VAR_SPAWN_PROBABILITY (pSrt->m_rootComputeVec1.x)

#define RIBBON_ROOT_VAR_HARD_RATE (pSrt->m_rootComputeVec3.x)
#define RIBBON_ROOT_VAR_SOFT_RATE (pSrt->m_rootComputeVec3.y)


#define BLOOD_RIBBON_SPAWN_BLOOD_MAP_THRESHOLD (0.75f)

#ifndef TRACK_INDICES
#define TRACK_INDICES 1
#endif


// spawns ribbons based purely on screen space analysis. we usually actually not use this shader because we need better control over where to spawn
[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeRibbonSpawnNew(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// each group works on one ribbon

	// we only spawn new particle when the whole trail is dead, i.e. first element in the list is the sentinel

	//_SCE_BREAK();

	bool isDynamicTrail = (groupId.x % 2) == 0;

	if (!isDynamicTrail)
		return;  // either all threads exit or none

	//if (groupThreadId.x != 0)
	//	return;
	
	
	//{
	//	if (groupThreadId.x != 0)
	//		return;
	//	if (groupThreadId.x == 0)
	//		return;
	//}

	//if (groupId.x > 0)
	//	return;

	int iStateOffset = groupId.x * kNumPartsInRibbon;

	ParticleStateV0 state = pSrt->m_particleStates[iStateOffset]; // the state has already been updated, so we look at current state

	if (state.m_id == 0)
	{
		// is sentinel, we can create new particle for this ribbon
	}
	else
	{
		// we have at least one particle in this ribbon. they will be updated and spawn bleeding edge when needed
		return;  // either all threads exit or none
	}

	uint myCellIndex = (pSrt->m_screenResolution.x / pSrt->m_gridCellW) * groupId.y + groupId.x;

	float probability = pSrt->m_dataBuffer[myCellIndex].m_data0.x;

	// check if match the texture
	// each group is ran per screen space quad

	uint2 myCellPos = groupId * uint2(pSrt->m_gridCellW, pSrt->m_gridCellH);

	// do 64 random samples in the cell to generate data about this cell. then use this data for spawning splashes in this cell
	float rand0 = /*0.1f + (groupId.x % 8) * 0.1f;*/  GetRandom(pSrt->m_gameTicks, dispatchId.x, 30);  // float((pSrt->m_gameTicks * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);
	float rand1 = /*0.1f + (groupId.x / 8) * 0.2f;*/  GetRandom(pSrt->m_gameTicks, dispatchId.x, 31);  // float(((pSrt->m_gameTicks ^ 0x735fac31) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);

	uint rotatedThreadId = (groupThreadId.x + pSrt->m_gameTicks) % 64;

	uint quadrantX = (rotatedThreadId % 8) * (pSrt->m_screenResolution.x / 8);
	uint quadrantY = (rotatedThreadId / 8) * (pSrt->m_screenResolution.y / 8);

	uint2 sspos = uint2(quadrantX, quadrantY) + uint2(pSrt->m_screenResolution / uint2(8,8) * float2(rand0, rand1));


	float opaqueNdcDepth = pSrt->m_pPassDataSrt->m_primaryDepthTexture[sspos];
	float opaquePlusAlphaNdcDepth = pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos];

	


	float depthVs = GetLinearDepth(pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos], pSrt->m_depthParams);

	if (RIBBON_ROOT_VAR_SPAWN_PROBABILITY < 0.01f)
		return;  // either all threads exit or none

	probability = depthVs > 5.0 ? 0 : ProbabilityBasedOnDistance(depthVs) * RIBBON_ROOT_VAR_SPAWN_PROBABILITY;

	float rand2 = GetRandom(pSrt->m_gameTicks, groupId.x * 64, 2);  // float(((pSrt->m_gameTicks ^ 0xac31735f) * dispatchId.x) & 0x0000FFFF) / float(0x0000FFFF);

	if (rand2 >= probability)
	{
		// decided not to add
		return;
	}

	uint stencil = pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil[sspos];

	bool fgStencil = stencil & 0x20;

	int threadSucceess = 1;

	// still have all 64 threads running


	if ((pSrt->m_features & COMPUTE_FEATURE_FG_ONLY) && !fgStencil)
	{
		threadSucceess = 0; // filter out bg
	}

	bool bgOnly = (pSrt->m_features & COMPUTE_FEATURE_BG_ONLY);
	if (bgOnly && fgStencil)
	{
		threadSucceess = 0; // filter out fg
	}

	Setup setup = (Setup)0;

	uint materialMaskSample = pSrt->m_pPassDataSrt->m_materialMaskBuffer[sspos];
	const uint4 sample1 = pSrt->m_pPassDataSrt->m_gbuffer1[sspos];

	UnpackMaterialMask(materialMaskSample, sample1, setup.materialMask);

	if ((pSrt->m_features & COMPUTE_FEATURE_ONLY_ON_CHARACTERS) && !setup.materialMask.isCharacter)
	{
		threadSucceess = 0;
	}

	if ((pSrt->m_features & COMPUTE_FEATURE_EXCLUDE_CHARACTERS) && setup.materialMask.isCharacter)
	{
		threadSucceess = 0;
	}


	// say all probability and other tests succeeded, grab the first thread that is successful

	if (groupThreadId.x == ReadFirstLane(groupThreadId.x))
	{
		// this is the first active thread. it can create a particle
	}

	// find if any thread has succeded and use its results
	ulong succeessmask = __v_cmp_eq_u32(threadSucceess, 1);

	int goodThread = __s_ff1_i32_b64(succeessmask);

	if (goodThread == -1)
	{
		// no threads succeeded, bail

		return;
	}

	int trackingOnOpaquePlusAlpha = 0;
	if (opaqueNdcDepth != opaquePlusAlphaNdcDepth)
	{
		trackingOnOpaquePlusAlpha = 1;
	}

	// now we need to get back into non-diverged state
	//posWs.x = ReadLane(posWs.x, uint(goodThread));
	//posWs.y = ReadLane(posWs.y, uint(goodThread));
	//posWs.z = ReadLane(posWs.z, uint(goodThread));
	//posNdc.x = ReadLane(posNdc.x, uint(goodThread));
	//posNdc.y = ReadLane(posNdc.y, uint(goodThread));
	//posNdc.z = ReadLane(posNdc.z, uint(goodThread));
	sspos.x = ReadLane(sspos.x, uint(goodThread));
	sspos.y = ReadLane(sspos.y, uint(goodThread));
	trackingOnOpaquePlusAlpha = ReadLane(trackingOnOpaquePlusAlpha, uint(goodThread));
	stencil = ReadLane(stencil, uint(goodThread));


	{
		// will add particle to the atomic buffer or retrun not doing anything

		//float3 posNdc = float3(sspos.x / pSrt->m_screenResolution.x * 2 - 1, -(sspos.y / pSrt->m_screenResolution.y * 2 - 1), pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos]);
		float3 posNdc = ScreenPosToNdc(pSrt, sspos, pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos]);

		float4 posH = mul(float4(posNdc, 1), pSrt->m_pPassDataSrt->m_mAltVPInv);
		float3 posWs = posH.xyz / posH.w;
		posWs += pSrt->m_altWorldOrigin;



		//AddFeedBackSpawn(pSrt, sspos, posWs, groupThreadId.x, /*colorIntensity=*/1.0);


		ParticleStateV0 addedState;

		

		uint defaultFlags1 = trackingOnOpaquePlusAlpha ? kMaterialMaskAlphaDepth : 0;

		// also check that we are spawning on a character that is tracked and we can find bind pose

		float3 bindPoseLs = float3(0, 0, 0);
		int addedStateValid = bgOnly;
		FindProcessResults findProc = FindProcessMeshTriangleBaricentrics(pSrt, sspos, posWs, groupThreadId.x);

		uint checkBlood = (pSrt->m_features & COMPUTE_FEATURE_REQUIRE_BLOOD_MAP_AT_SPAWN_LOCATION);
		
		uint ribbonType = 0;
		bool allowSkin = ribbonType == 0; // only hard ribbon can be on skin
		bool allowHead = ribbonType == 0; // only allow head with hard ribbons

		// only one thread potentially succeeds
		if (findProc.m_foundIndex != -1)
		{
			if (checkBlood)
			{
				int bodyPart = findProc.m_bodyPart;
				if (allowHead || bodyPart != kLook2BodyPartHead)
				{
					// only allow to spawn where there is blood
					if (findProc.m_rtId != 255)
					{
						// check particle render target and pick up color
						int rtId = ReadFirstLane(findProc.m_rtId);
						float textureBloodMapValue = pSrt->m_pParticleRTs->m_textures[rtId].SampleLevel(pSrt->m_linearSampler, findProc.m_uv, 0).x;
			
						if (textureBloodMapValue > BLOOD_RIBBON_SPAWN_BLOOD_MAP_THRESHOLD)
						{
							addedStateValid = 1;
						}
					}
				}
			}
			else
			{
				addedStateValid = 1;
			}
		}

		addedStateValid = __v_cmp_eq_u32(addedStateValid, 1) != 0; // succeed if one thread succeeded

#if TRACK_BIND_POSE
		if (!addedStateValid)
		{
			// one thread adds debug, since we were only processing one result anyway

			// check if we found mapped object at all

			if (__v_cmp_ne_u32(findProc.m_foundIndex, -1) == 0)
			{
				// no object found
				if (groupThreadId.x == 0)
				{
					AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, posWs, posWs, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), 0, 0, dispatchId.x, -1, ParticleFailReason_SpawnFailProcess);
				}
			}
			// we are not checking blood map
			//else
			//{
			//	// object was found but blood map or body part failed
			//
			//	if (findProc.m_foundIndex != -1)
			//	{
			//		// this thread found object
			//		AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, posWs, posWs, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), findProc.m_rtId, 0, dispatchId.x, hintSpawnerId, ParticleFailReason_SpawnFailNoBloodBadBodyPart);
			//	}
			//}



			return;
		}
#endif
		// all threads should be doing the same thing so this should be scalar and done once

		uint hintSpawnerId = 0;
		int resultIndex;

		StickyParticleParams spawnParams = DefaultStickyParticleParams();
		spawnParams.allowSkin = true;

		bool added = NewStickyParticle(kMotionStateAttached, // will spawn particle based on depth buffer and do checks with motion vector to make sure it is a good place
			sspos, posNdc, posWs,
			defaultFlags1,
			/*state->m_flags1 = stencil | (motionState << 8) */ stencil,
			pSrt, dispatchId,
			/*particleId -> state.m_id*/ ((pSrt->m_frameNumber << 22) | ((hintSpawnerId & 0x0000007F) << 15) | (ribbonType << 14) | ((groupId.x & 0x3F) << 8) | 0x01), // we start at 1! because 0 means sentinel
			/*checkInsideFirstParticle=*/ true,
			/*birthTimeOffset=*/ 0.001f,
			/*spawnData -> state.m_data*/ 0.0f, /*useProvidedIndex*/ true, /*providedParticleIndex*/iStateOffset, groupThreadId.x,
			/*lifetime -> state.m_lifetime*/ asfloat(f32tof16(RIBBON_ROOT_VAR_LIFETIME)), addedState, /*normalHalfWayWithRain=*/ false, /*inheritedVelocity=*/ float3(0, 0, 0), /*isSnow*/ false, spawnParams, /*defaultSpeed*/ 0.0f, resultIndex, /*customSize*/ 0);



		// write sentinel
		if (added)
		{
			pSrt->m_particleStates[iStateOffset + 1].m_id = 0;
			pSrt->m_particleStates[iStateOffset + 1].m_scale.x = 0;
			pSrt->m_particleStates[iStateOffset + 63].m_scale.y = 0; // store current length of ribbon

			// also clear out the non dynamic trail

			pSrt->m_particleStates[iStateOffset + 64].m_id = 0;
			pSrt->m_particleStates[iStateOffset + 64].m_scale.x = 0;

		
#if TRACK_BIND_POSE
			if (findProc.m_foundIndex != -1) // get data from thread that succeeded (should be only one)
			{
				pSrt->m_particleStates[iStateOffset].m_rotation = EncodeRotationFromBindPose(findProc.m_bindPosLs, findProc.m_baricentrics, findProc.m_meshId);

				#if TRACK_INDICES
				pSrt->m_particleStates[iStateOffset].m_speed = asfloat((findProc.m_indices.x & 0x0000FFFF) | (findProc.m_indices.y << 16));
				pSrt->m_particleStates[iStateOffset].m_lifeTime = asfloat(asuint(pSrt->m_particleStates[iStateOffset].m_lifeTime) | (findProc.m_indices.z << 16));
				#endif

				#if ADVANCE_UV_CAPTURE
				SetHaveUvs(pSrt->m_particleStates[iStateOffset]);
				#endif

				#if ADVANCE_UV_CAPTURE && !TRACK_INDICES
				pSrt->m_particleStates[iStateOffset].m_speed = asfloat(PackFloat2ToUInt(findProc.m_uv.x, findProc.m_uv.y));
				#endif

				AddDebugNewRibbonStickyParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, addedState.m_pos, posWs, 0.01f, float4(findProc.m_bindPosLs, 1.0f), GetFlags0(addedState), addedState.m_flags1, dispatchId.x, addedState.m_id, addedState.m_birthTime);
			}
#else
			if (groupThreadId.x == 0)
			{
				AddDebugNewRibbonStickyParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, addedState.m_pos, posWs, 0.01f, float4(-0.123f, -0.456f, -0.789f, 1.0f), GetFlags0(addedState), addedState.m_flags1, dispatchId.x, addedState.m_id, addedState.m_birthTime);
			}
#endif
			

		}
	}
}



float3 ConvertRotationToEncodedRotationAndBindPosLs(float3 rotation, float3 bindPosLs)
{
	float3 result;

	result.x = asfloat(PackFloat2ToUInt(rotation.x, rotation.y));
	result.y = asfloat(PackFloat2ToUInt(rotation.z, bindPosLs.x));
	result.z = asfloat(PackFloat2ToUInt(bindPosLs.y, bindPosLs.z));

	return result;
}
 



RibbonSpawnHint RibbonSpawnHintFromCpuEmitter(ParticleEmitterEntry emitter, float rate)
{
	
	float3 pos = emitter.m_pos.xyz;

	float3 offsetES = mul(float4(emitter.m_offsetVector, 0.0), emitter.m_rot).xyz;
	pos += offsetES;

	float scaleX = emitter.m_scale.x;
	float scaleY = emitter.m_scale.y;
	float scaleZ = emitter.m_scale.z;
	float3 scale = float3(scaleX, scaleY, scaleZ);

	//hint.m_scale *= 0.01f;

	float3x3 rot;
	rot[0] = emitter.m_rot[0].xyz;
	rot[1] = emitter.m_rot[1].xyz;
	rot[2] = emitter.m_rot[2].xyz;

	
	return CreateRibbonSpawnHint(pos, scale, rot, float4(1,1,1,1), 0, 0, 0, 0.0f);
}

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeRibbonGatherHintsFromSimpleEmitters(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// will add hints from simple emitters into the hint buffer
	// also will filter out simple emitters that are far away


	uint cpuEmitterListSize, cpuEmitterListStride;
	pSrt->m_cpuEmitterList.GetDimensions(cpuEmitterListSize, cpuEmitterListStride);

	if (dispatchId >= cpuEmitterListSize)
		return;

	uint gdsOffsetNew = pSrt->m_gdsOffsetOther;
	if (gdsOffsetNew == 0) // dont have a counter for "other" buffer, the one we want to write to
		return;



	uint size, stride;
	pSrt->m_particleStatesOther.GetDimensions(size, stride);

	ParticleEmitterEntry emitterEntry = pSrt->m_cpuEmitterList[dispatchId];

	float distToCamera = length(emitterEntry.m_pos - pSrt->m_cameraPosWs.xyz);

	if (distToCamera > 7.0f)
	{
		return; // too far
	}

	float rate = emitterEntry.m_rate;

	//rate *= LinStep(pSrt->m_pCpuEmitterComputeCustomData->m_vec0.y, pSrt->m_pCpuEmitterComputeCustomData->m_vec0.x, distToCamera);

	//if (rate < 0.001)
	//	return;

	{
		// we use a additional buffer of particle states to write data to tell the otehr system to spawn particles. It doesn't have to be a buffer of states, but we just use it.
		uint newInfoIndex = NdAtomicIncrement(gdsOffsetNew);
		if (newInfoIndex >= size)
		{
			// decrement back
			NdAtomicDecrement(gdsOffsetNew);
			return; // can't add new information
		}


		RibbonSpawnHint info = RibbonSpawnHintFromCpuEmitter(emitterEntry, rate);
		RWStructuredBuffer<RibbonSpawnHint> destHintBuffer = __create_buffer<RWStructuredBuffer<RibbonSpawnHint> >(__get_vsharp(pSrt->m_particleStatesOther));
		destHintBuffer[512 + newInfoIndex] = info;
	}
}

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeRibbonGatherHintInfo(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{

	// go through each hint and gather how many active ribbons we have per hint
	uint gdsOffsetOther = pSrt->m_gdsOffsetOther;
	//_SCE_BREAK();
	uint numHints = NdAtomicGetValue(gdsOffsetOther);
	numHints = ReadFirstLane(numHints);
	if (numHints > 0 && numHints <= 512)
	{
		// get unique id of the hint
		
		bool useHighPrioOnly = false;
	
		RWStructuredBuffer<ParticleDataRibbonHints> destDataArrBuffer = __create_buffer<RWStructuredBuffer<ParticleDataRibbonHints> >(__get_vsharp(pSrt->m_dataBuffer));

		{
			uint nextHintSlot = 0;

			for (uint iHInt = 0; iHInt < min(numHints, 512); ++iHInt)
			{
				RWStructuredBuffer<RibbonSpawnHint> destHintBuffer = __create_buffer<RWStructuredBuffer<RibbonSpawnHint> >(__get_vsharp(pSrt->m_particleStatesOther)); // note this is RW buffer, so we can modify it in this job and read again in next without cache issues. all in L2

				RibbonSpawnHint hint = destHintBuffer[iHInt];

				float4 hintColor = float4(f16tof32(hint.m_packedColor.x), f16tof32(hint.m_packedColor.x >> 16), f16tof32(hint.m_packedColor.y), f16tof32(hint.m_packedColor.y >> 16));

				float spawnRateMultiplier = hintColor.r;

				int hintSpawnerId = hint.m_type1_spawnerId16 & (0x0000FFFF); // max 255, other ones are coming from dynamic compute spawner like particle tracker

				RibbonHintData hintData = destDataArrBuffer[hintSpawnerId / 8].m_datas[hintSpawnerId % 8];
			
				if (hint.m_uniqueId != hintData.m_uniqueId)
				{
					// new hint. reset the data
					hintData.m_uniqueId = hint.m_uniqueId;
					hintData.m_hardSpawnTime = 0;
					hintData.m_softSpawnTime = 0;

					destDataArrBuffer[hintSpawnerId / 8].m_datas[hintSpawnerId % 8] = hintData;
				}

				bool fastSpawn = hint.m_age < 3.0 && hintColor.b > 0;

				if (!fastSpawn && useHighPrioOnly)
					continue;
			
				uint allowDifferentRibbons = (pSrt->m_features & COMPUTE_FEATURE_ALLOW_DIFFERENT_RIBBON_TYPES);

				uint ribbonTypeCheck = (fastSpawn || hintColor.y < 0.0001) ? 0 : (pSrt->m_frameNumber % 2); // only hard ones first

				ribbonTypeCheck = allowDifferentRibbons ? ribbonTypeCheck : 0; // 0 type ribbon if we don't allow different ribbons

				float lastSpawnTime = ribbonTypeCheck ? hintData.m_softSpawnTime : hintData.m_hardSpawnTime;

				float spawnRate = (ribbonTypeCheck ? RIBBON_ROOT_VAR_SOFT_RATE : RIBBON_ROOT_VAR_HARD_RATE);

				spawnRate *= spawnRateMultiplier;

				float secondsToRespawn = fastSpawn ? 0 : (1.0 / spawnRate); // first second spawn every frame.. max we can do

				float framesToRespawn = 30 * secondsToRespawn; // spawn new one every 2 seconds

				if (fastSpawn)
				{
					if (!useHighPrioOnly)
					{
						// first high priority one
						nextHintSlot = 0;
						useHighPrioOnly = 1;
					}
				}

				{
					
					if (pSrt->m_time - lastSpawnTime > secondsToRespawn)
					{
						//hint.m_numActive = numRibbonsActive;
						hint.m_type1_spawnerId16 = hint.m_type1_spawnerId16 | (ribbonTypeCheck << 16);;

						destHintBuffer[512 + nextHintSlot] = hint;

						nextHintSlot += 1;

						if (fastSpawn)
						{
							destHintBuffer[512 + nextHintSlot] = hint;
							nextHintSlot += 1;

							destHintBuffer[512 + nextHintSlot] = hint;
							nextHintSlot += 1;
						
							destHintBuffer[512 + nextHintSlot] = hint;
							nextHintSlot += 1;

						
							//destHintBuffer[512 + nextHintSlot] = hint;
							//nextHintSlot += 1;
						}
					}
				}
			}

			NdAtomicSetValue(ReadFirstLane(nextHintSlot), gdsOffsetOther);
		}
	}
}


// this spawns new ribbons from hint list
[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeRibbonSpawnNewFromList(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	//state.m_direction.x = 0;
	//NewStickyParticle(0, sspos, posNdc, posWs, 0, 0, stencil, pSrt, dispatchId, particleId, /*checkInsideFirstParticle=*/ false, /*birthTimeOffset=*/ 0.001f, /*spawnData*/ -0.5f, /*useProvidedIndex*/ false, /*providedParticleIndex=*/ -1, groupThreadId.x, /*lifetime=*/ 10.0, /*defaultSpeed*/ 0.0f, /*customSize*/ 0);


	// each group works on one ribbon

	// we only spawn new particle when the whole trail is dead, i.e. first element in the list is the sentinel
	
	if (groupId.x == 0)
	{
		//_SCE_BREAK();
	}

	bool isDynamicTrail = (groupId.x % 2) == 0;

	if (!isDynamicTrail)
		return;  // either all threads exit or none

	//spawn only one
	//if (groupId.x != 0)
	//	return;


	//_SCE_BREAK();

	int iStateOffset = groupId.x * kNumPartsInRibbon;

	ParticleStateV0 state = pSrt->m_particleStates[iStateOffset]; // the state has already been updated, so we look at current state
	ParticleStateV0 staticCopyState = pSrt->m_particleStates[iStateOffset + 64];

	if (state.m_id == 0 && staticCopyState.m_id == 0)
	{
		// is sentinel, we can create new particle for this ribbon
	}
	else
	{
		// we have at least one particle in this ribbon. they will be updated and spawn bleeding edge when needed
		return;  // either all threads exit or none
	}

	uint gdsOffsetOther = pSrt->m_gdsOffsetOther;
	int iHInt = 0;
	
	uint numHints = NdAtomicGetValue(gdsOffsetOther);
	
	if (groupThreadId.x == 0)
	{
		iHInt = NdAtomicDecrement(gdsOffsetOther);
	}
	iHInt = ReadFirstLane(iHInt);
	if (iHInt <= 0)
		return; // either all threads exit or none

	//if (numHints == 0)
	//	return;
	//iHInt = 1 + (groupId.x + pSrt->m_frameNumber) % numHints;



	// still have 64 threads running. each will grab a position

	RWStructuredBuffer<RibbonSpawnHint> destHintBuffer = __create_buffer<RWStructuredBuffer<RibbonSpawnHint> >(__get_vsharp(pSrt->m_particleStatesOther));

	RibbonSpawnHint hint = destHintBuffer[512 + iHInt-1];


	//if (hint.m_numActive > 0)
	//	return;

	// build a position within a spawner particle
	float4x4	g_identity = { { 1, 0, 0, 0 },{ 0, 1, 0, 0 },{ 0, 0, 1, 0 },{ 0, 0, 0, 1 } };

	float4x4 world = g_identity;

	world[3].xyz = hint.m_pos;

	float3 hintM0 = float3(f16tof32(hint.m_packedm0xyzm1xyz.x), f16tof32(hint.m_packedm0xyzm1xyz.x >> 16), f16tof32(hint.m_packedm0xyzm1xyz.y));
	float3 hintM1 = float3(f16tof32(hint.m_packedm0xyzm1xyz.y >> 16), f16tof32(hint.m_packedm0xyzm1xyz.z), f16tof32(hint.m_packedm0xyzm1xyz.z >> 16));
	float3 hintM2 = float3(f16tof32(hint.m_packedm2xyzsxyz.x), f16tof32(hint.m_packedm2xyzsxyz.x >> 16), f16tof32(hint.m_packedm2xyzsxyz.y));
	float3 hintS = float3(f16tof32(hint.m_packedm2xyzsxyz.y >> 16), f16tof32(hint.m_packedm2xyzsxyz.z), f16tof32(hint.m_packedm2xyzsxyz.z >> 16));


	float4 hintColor = float4(f16tof32(hint.m_packedColor.x), f16tof32(hint.m_packedColor.x >> 16), f16tof32(hint.m_packedColor.y), f16tof32(hint.m_packedColor.y >> 16));


	world[0].xyz = hintM0; // hint.m_partMat3x3[0];
	world[1].xyz = hintM1; // hint.m_partMat3x3[1];
	world[2].xyz = hintM2; // hint.m_partMat3x3[2];


	int kNumTries = 1;
	while (kNumTries > 0)
	{
		kNumTries -= 1;
		float perGroupRand0 = GetRandom(pSrt->m_gameTicks, dispatchId.x + pSrt->m_frameNumber, 0 + kNumTries);
		float perGroupRand1 = GetRandom(pSrt->m_gameTicks, dispatchId.x + pSrt->m_frameNumber, 1 + kNumTries);
		float perGroupRand2 = GetRandom(pSrt->m_gameTicks, dispatchId.x + pSrt->m_frameNumber, 2 + kNumTries);

		float3 randHintPos = mul(float4(perGroupRand0 - 0.5, perGroupRand1 - 0.5, perGroupRand2 - 0.5, 1.0), world).xyz;


		float3 hintPos = randHintPos;

		float3x3 partInv = { { 1, 0, 0 },{ 0, 1, 0 },{ 0, 0, 1 } };

		partInv[0].xyz = world[0].xyz / hintS.x / hintS.x;
		partInv[1].xyz = world[1].xyz / hintS.y / hintS.y;// *originalParticle.invscale.y * 0.5f;
		partInv[2].xyz = world[2].xyz / hintS.z / hintS.z;

		uint hintSpawnerId = hint.m_type1_spawnerId16 & 0x0000FFFF; // 0..127 are valid values. the other ones are coming from dynamic compute spawners like water tracker
		uint ribbonType = (hint.m_type1_spawnerId16 >> 16) & 1;

		/*



		float4 topPosH = mul(float4(topPosWs, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 topPosNdc = topPosH.xyz / topPosH.w;

		uint2 topSspos = uint2((topPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (topPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);

		*/




		// project the hint into screen space and from there just do the normal spawning


		float4 hintPosH = mul(float4(hintPos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 hintPosNdc = hintPosH.xyz / hintPosH.w;

		int threadSucceess = 1;

		if (abs(hintPosNdc.x) > 1.0 || abs(hintPosNdc.x) > 1.0)
		{
			threadSucceess = 0;
		}

		// get screen space position of the hint
		//uint2 hintsspos = uint2((hintPosNdc.x * 0.5 + 0.5) * pSrt->m_screenResolution.x, (1.0 - (hintPosNdc.y * 0.5 + 0.5)) * pSrt->m_screenResolution.y);
		uint2 hintsspos = NdcToScreenSpacePos(pSrt, hintPosNdc.xy);


		// check if match the texture
		// each group is ran per screen space quad

		uint2 sspos = hintsspos;

		float depthVs = GetLinearDepth(pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos], pSrt->m_depthParams);

		float opaqueNdcDepth = pSrt->m_pPassDataSrt->m_primaryDepthTexture[sspos];
		float opaquePlusAlphaNdcDepth = pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos];

		int trackingOnOpaquePlusAlpha = 0;
		if (opaqueNdcDepth != opaquePlusAlphaNdcDepth)
		{
			trackingOnOpaquePlusAlpha = 1;
		}



		uint stencil = pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil[sspos];

		float3 posNdc = ScreenPosToNdc(pSrt, sspos, pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos]);

		float4 posH = mul(float4(posNdc, 1), pSrt->m_pPassDataSrt->m_mAltVPInv);
		float3 posWs = posH.xyz / posH.w;
		posWs += pSrt->m_altWorldOrigin;


		//AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, posWs, posWs, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), 0, 0, dispatchId.x, hintSpawnerId, ParticleFailReason_SpawnFailNoBloodBadBodyPart);

		bool fgStencil = (stencil & 0x20) && !(stencil & 0x2);

		if ((pSrt->m_features & COMPUTE_FEATURE_FG_ONLY) && !fgStencil)
		{
			threadSucceess = 0; // filter out bg

			AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, posWs, posWs, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), 0, 0, dispatchId.x, hintSpawnerId, ParticleFailReason_SpawnFailNoBloodBadBodyPart);

		}

		bool bgOnly = (pSrt->m_features & COMPUTE_FEATURE_BG_ONLY);

		#if TRACK_BIND_POSE
		bgOnly = false;
		#endif

		if (bgOnly && fgStencil)
		{
			threadSucceess = 0; // filter out fg

			AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, posWs, posWs, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), 0, 0, dispatchId.x, hintSpawnerId, ParticleFailReason_SpawnFailNoBloodBadBodyPart);

		}

		Setup setup = (Setup)0;

		uint materialMaskSample = pSrt->m_pPassDataSrt->m_materialMaskBuffer[sspos];
		const uint4 sample1 = pSrt->m_pPassDataSrt->m_gbuffer1[sspos];

		UnpackMaterialMask(materialMaskSample, sample1, setup.materialMask);

		if (bgOnly && !setup.materialMask.hasSpecularNormal)
		{
			threadSucceess = 0; // filter out "dry" pixels

			AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, posWs, posWs, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), 0, 0, dispatchId.x, hintSpawnerId, ParticleFailReason_SpawnFailNoBloodBadBodyPart);

		}

		// might have less than 64 threads at this point

		// say all probability and other tests succeeded, grab the first thread that is successful

		if (groupThreadId.x == ReadFirstLane(groupThreadId.x))
		{
			// this is the first active thread. it can create a particle
		}

		// will add particle to the atomic buffer or retrun not doing anything

		//float3 posNdc = float3(sspos.x / pSrt->m_screenResolution.x * 2 - 1, -(sspos.y / pSrt->m_screenResolution.y * 2 - 1), pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[sspos]);



		float3 posWsParticleSpace = posWs - world[3].xyz;
		posWsParticleSpace = mul(partInv, posWsParticleSpace).xyz;

		//if (length(posWs - hintPos) > 0.05)
		//	return;

		// not inside of volume anymore
		if (posWsParticleSpace.x > 0.5 || posWsParticleSpace.x < -0.5 || posWsParticleSpace.y > 0.5 || posWsParticleSpace.y < -0.5 || posWsParticleSpace.z > 0.5 || posWsParticleSpace.z < -0.5)
		{
			threadSucceess = 0;

			AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, posWs, posWs, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), 0, 0, dispatchId.x, hintSpawnerId, ParticleFailReason_SpawnFailNoBloodBadBodyPart);
			
		}



		// find if any thread has succeded and use its results
		ulong succeessmask = __v_cmp_eq_u32(threadSucceess, 1);

		int goodThread = __s_ff1_i32_b64(succeessmask);

		if (goodThread == -1)
		{
			// no threads succeeded, bail
			AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, posWs, posWs, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), 0, 0, dispatchId.x, hintSpawnerId, ParticleFailReason_SpawnFailNoBloodBadBodyPart);

			continue;
		}


		// all threads active
		// todo: check if position still within particle
		bool allowSkin = ribbonType == 0; // only hard ribbon can be on skin
		bool allowHead = ribbonType == 0; // only allow head with hard ribbons
		uint checkBlood = (pSrt->m_features & COMPUTE_FEATURE_REQUIRE_BLOOD_MAP_AT_SPAWN_LOCATION);

		FindProcessResults findProc = 0;
		
		if (!bgOnly)
		{

			//AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, posWs, posWs, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), 0, 0, dispatchId.x, hintSpawnerId, ParticleFailReason_SpawnFailNoBloodBadBodyPart);


			findProc = FindProcessMeshTriangleBaricentricsDivergent(pSrt, threadSucceess, sspos, posWs, groupThreadId.x, checkBlood, BLOOD_RIBBON_SPAWN_BLOOD_MAP_THRESHOLD, allowHead);
			threadSucceess = findProc.m_foundIndex != -1;
		}

		// each thread has its own result

		// now we can choose one
		#if TRACK_BIND_POSE || 1
		if (!threadSucceess)
		{
			AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, posWs, posWs, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), 0, 0, dispatchId.x, hintSpawnerId, ParticleFailReason_UnmappedMesh);
		}
		#endif

		succeessmask = __v_cmp_eq_u32(threadSucceess, 1);

		goodThread = __s_ff1_i32_b64(succeessmask);

		if (goodThread == -1)
		{
			// no threads succeeded, bail

			continue;
		}

		//_SCE_BREAK();




		if (groupThreadId.x == goodThread)
		{
			// succeeding thread will add particle
			ParticleStateV0 addedState;
			uint defaultFlags1 = trackingOnOpaquePlusAlpha ? kMaterialMaskAlphaDepth : 0;

			//AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, posWs, posWs, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), 0, 0, dispatchId.x, hintSpawnerId, ParticleFailReason_SpawnFailNoBloodBadBodyPart);
			int resultIndex;

			StickyParticleParams spawnParams = DefaultStickyParticleParams();
			spawnParams.allowSkin = allowSkin;

			bool added = NewStickyParticle(kMotionStateAttached, // will spawn particle based on depth buffer and do checks with motion vector to make sure it is a good place
				sspos, posNdc, posWs,
				defaultFlags1,
				/*steancil->m_flags1 = stencil | (motionState << 8) */ stencil,
				pSrt, dispatchId,

				// we store 10 bits for frame number, ~30 seconds loop, 7 bits for hint id 1 bit for type 6 bits for group id (ribbonId) 8 bits for particle index
				/*particleId -> state.m_id*/ ((pSrt->m_frameNumber << 22) | ((hintSpawnerId & 0x0000007F) << 15) | (ribbonType << 14) | ((groupId.x & 0x3F) << 8) | 0x01), // wwe start at 1! because 0 means sentinel
				/*checkInsideFirstParticle=*/ true,
				/*birthTimeOffset=*/ 0.001f,
				/*spawnData -> state.m_data*/ 0.0f, /*useProvidedIndex*/ true, /*providedParticleIndex*/iStateOffset, groupThreadId.x,
				/*lifetime -> state.m_lifetime*/ asfloat(f32tof16(RIBBON_ROOT_VAR_LIFETIME)), addedState, /*normalHalfWayWithRain=*/ false, /*inheritedVelocity=*/ float3(0, 0, 0), /*isSnow*/ false, spawnParams, /*defaultSpeed*/ 0.0f, resultIndex, /*customSize*/ 0);

			// write sentinel
			if (added)
			{
				pSrt->m_particleStates[iStateOffset + 1].m_id = 0;
				pSrt->m_particleStates[iStateOffset + 1].m_scale.x = 0;
				pSrt->m_particleStates[iStateOffset + 63].m_scale.y = 0; // store current length of ribbon

				pSrt->m_particleStates[iStateOffset + 64].m_id = 0;
				pSrt->m_particleStates[iStateOffset + 64].m_scale.x = 0;

				// test initialize
				//pSrt->m_particleStates[iStateOffset].m_data = asfloat(f32tof16(1.0) << 16);

				#if TRACK_BIND_POSE
				{
					pSrt->m_particleStates[iStateOffset].m_rotation = EncodeRotationFromBindPose(findProc.m_bindPosLs, findProc.m_baricentrics, findProc.m_meshId);

					#if TRACK_INDICES
					pSrt->m_particleStates[iStateOffset].m_speed = asfloat((findProc.m_indices.x & 0x0000FFFF) | (findProc.m_indices.y << 16));
					pSrt->m_particleStates[iStateOffset].m_lifeTime = asfloat(asuint(pSrt->m_particleStates[iStateOffset].m_lifeTime) | (findProc.m_indices.z << 16));
					
					#if !ADVANCE_UV_CAPTURE
					SetStartedAtHead(pSrt->m_particleStates[iStateOffset]);
					SetUintNorm(pSrt->m_particleStates[iStateOffset], findProc.m_uNorm);
					#endif
					
					#endif
					
					#if ADVANCE_UV_CAPTURE
					SetHaveUvs(pSrt->m_particleStates[iStateOffset]);

					if (findProc.m_procType == kProcTypeInfected)
					{
						SetIsInfected(pSrt->m_particleStates[iStateOffset]);
					}
					#endif

					#if ADVANCE_UV_CAPTURE && !TRACK_INDICES
					pSrt->m_particleStates[iStateOffset].m_speed = asfloat(PackFloat2ToUInt(findProc.m_uv.x, findProc.m_uv.y));
					#endif

					AddDebugNewRibbonStickyParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, addedState.m_pos, posWs, 0.01f, float4(findProc.m_bindPosLs, 1.0f), GetFlags0(addedState), addedState.m_flags1, dispatchId.x, addedState.m_id, addedState.m_birthTime);

					AddDebugNewStickyBaryDataParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, findProc.m_posWs, addedState.m_pos, dispatchId.x, addedState.m_id, findProc.m_meshId, findProc.m_indices, findProc.m_baricentrics.xy);

				}
				#else
				{
					AddDebugNewRibbonStickyParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, addedState.m_pos, posWs, 0.01f, float4(-0.123f, -0.456f, -0.789f, 1.0f), GetFlags0(addedState), addedState.m_flags1, dispatchId.x, addedState.m_id, addedState.m_birthTime);
				}
				#endif

				RWStructuredBuffer<ParticleDataRibbonHints> destDataArrBuffer = __create_buffer<RWStructuredBuffer<ParticleDataRibbonHints> >(__get_vsharp(pSrt->m_dataBuffer));

				int dataIndex = hintSpawnerId;

				if (ribbonType)
				{
					destDataArrBuffer[dataIndex / 8].m_datas[dataIndex % 8].m_softSpawnTime = pSrt->m_time;
				}
				else
				{
					destDataArrBuffer[dataIndex / 8].m_datas[dataIndex % 8].m_hardSpawnTime = pSrt->m_time;
				}

				break;
			} // if added
		} // if good thread


		// still have 64 threads active

		
		// also check that we are spawning on a character that is tracked and we can find bind pose


		//int addedStateValid = bgOnly;
		
	} // while kNumTries
	
}

// this job adds hints for droplets
// we look at the edge of ribbon and add hints based on that
// we also add blood map and blood wash out events
[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeRibbonSpawnDrips(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// each group works on one ribbon

	// we only spawn new particle when the whole trail is dead, i.e. first element in the list is the sentinel

	//_SCE_BREAK();

	int iStateOffset = groupId.x * kNumPartsInRibbon;

	ParticleStateV0 firstState = pSrt->m_particleStates[iStateOffset]; // the state has already been updated, so we look at current state

	if (firstState.m_id == 0)
	{
		// is sentinel, no trail here

		return;
	}

	bool isDynamicTrail = (groupId.x % 2) == 0;

	if (!isDynamicTrail)
		return;


	ParticleStateV0 state = pSrt->m_particleStates[iStateOffset + groupThreadId.x];

	bool isSentinel = state.m_id == 0;


	bool haveUvs = false;
	#if ADVANCE_UV_CAPTURE
	haveUvs = GetHaveUvs(state);
	#endif

	ulong haveUvs_mask = __v_cmp_eq_u32(haveUvs, true);

	// We create a mask which is 1 for all lanes that contain this value
	ulong sentinel_mask = __v_cmp_eq_u32(isSentinel, true);

	// we need to find the earliest sentinel
	int first_bit = __s_ff1_i32_b64(sentinel_mask);  

	int first_uvBit = __s_ff1_i32_b64(haveUvs_mask); 

	bool isOutoFBounds = groupThreadId.x >= first_bit;


	if (groupThreadId.x == 0)
	{
		AddDebugStickyParticlePostUpdate(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, groupId.x, sentinel_mask, haveUvs_mask);
	}

	// spawn blood map on last one
	ParticleStateV0 endState = pSrt->m_particleStates[iStateOffset + 63];

	// find screen space pos too
	#if ADVANCE_UV_CAPTURE
	if (first_uvBit < first_bit && first_uvBit >= 0)
	{
		ParticleStateV0 lastState = pSrt->m_particleStates[iStateOffset + first_uvBit /*first_bit - 1*/];

		SetNoUvs(pSrt->m_particleStates[iStateOffset + first_uvBit /*first_bit  - 1*/]); // clear out uvs.

	#else
	ParticleStateV0 lastState = pSrt->m_particleStates[iStateOffset + first_bit - 1];
	if (GetFlags0(lastState) == 0)
	{
	#endif

		float4 newPosH = mul(float4(lastState.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 newPosNdc = newPosH.xyz / newPosH.w;
		//uint2 sspos = uint2((newPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (newPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);
		uint2 sspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);


		bool skinOrHair = lastState.m_flags1 & (kMaterialMaskSkin | kMaterialMaskHair);


		if ((pSrt->m_features & COMPUTE_FEATURE_GENERATE_BLOOD_MAP_EVENTS))
		{
			float2 uv = float2(0);
			uint3 indices = uint3(0);
			#if TRACK_INDICES
			indices.x = asuint(lastState.m_speed) & 0x0000FFFF;
			indices.y = asuint(lastState.m_speed) >> 16;
			indices.z = asuint(lastState.m_lifeTime) >> 16;
			#else
			uv.x = f16tof32(asuint(lastState.m_speed));
			uv.y = f16tof32(asuint(lastState.m_speed) >> 16);
			#endif
			uint meshId24 = DecodeMeshId24FromRotation(lastState.m_rotation);
			float2 barys = DecodeBarysFromRotation(lastState.m_rotation);	

			#if ADVANCE_UV_CAPTURE

			bool isInfected = GetIsInfected(lastState);

			float rand0 = GetRandom(groupId.x, groupId.x, groupId.x);
			
			uint index = lastState.m_id & 0x000000FF;

			float fpsScale = saturate(0.03333 / pSrt->m_delta);

			// everything happens faster when we are at lower fps

			float numFramesThick = rand0 * 32 * fpsScale;

			float fromIndex = saturate((numFramesThick + 24.0 * fpsScale - index) / (16.0 * fpsScale));

			//fromIndex *= fromIndex;

			uint isSkin = (lastState.m_flags1 & kMaterialMaskSkin) != 0;

			rand0 = 1.0 - fromIndex;
			rand0 *= rand0;
			float alpha = 0.7 * pow(rand0, 1) + 0.3f;
			float scaleX = 0.75 + (1.0 - rand0) * 4.5;

			if (isInfected)
				alpha = 1;
			//scaleX = 10 * fromIndex;

			if (isSkin && !isInfected)
			{
				fromIndex = saturate((numFramesThick * fpsScale / 2 + 8.0 * fpsScale - index) / (8.0 * fpsScale));
				rand0 = 1.0 - fromIndex;
				rand0 *= rand0;

				scaleX = 1.0 + (rand0) * 1.0;
				alpha = 1.0 - pow(rand0, 1) * 0.3f;;

				//alpha = 0;
			}



			AddBakedFeedBackSpawn(pSrt, sspos, lastState.m_pos, groupThreadId.x, dispatchId.x, lastState.m_id, /*ribbonLength*/endState.m_scale.y, /*colorIntensity=*/alpha, /*colorOpacity=*/1.0, scaleX, uv, indices, barys.x, barys.y, TRACK_INDICES, meshId24);
			#else
			AddFeedBackSpawn(pSrt, sspos, lastState.m_pos, groupThreadId.x, dispatchId.x, lastState.m_id, /*bloodMapTrail=*/ true, endState.m_scale.y, false, /*bloodDrip*/ false, /*colorIntensity=*/1.0, /*colorOpacity=*/1.0, false, float2(0)

			);
			#endif
		}

		else if ((pSrt->m_features & COMPUTE_FEATURE_BLOOD_MAP_WASH_OUT))
		{
			AddFeedBackSpawn(pSrt, sspos, lastState.m_pos, groupThreadId.x, dispatchId.x, lastState.m_id, /*bloodMapTrail=*/ true, endState.m_scale.y, false, /*bloodDrip*/ false, /*colorIntensity=*/0.0, /*colorOpacity=*/(skinOrHair ? 0.25 : 0.01f), false, float2(0));
		}

		//FindProcessResults findRes = FindProcessMeshTriangleBaricentrics(pSrt, sspos, lastState.m_pos, groupThreadId.x);
		//// only one thread potentially succeeded
		//if (findRes.m_foundIndex != -1 && findRes.m_rtId != 255)
		//{
		//	// check particle render target and pick up color
		//	int rtId = ReadFirstLane(findRes.m_rtId);
		//	//float bloodMapValue = pSrt->m_pParticleRTs->m_textures[rtId].SampleLevel(pSrt->m_linearSampler, findRes.m_uv, 0).x;
		//
		//	// write it into sentinel
		//
		//	//pSrt->m_particleStates[iStateOffset + first_bit].m_scale.x = bloodMapValue;
		//}
	}


	


	if (isOutoFBounds)
		return;
	
	uint gdsOffsetNew = pSrt->m_gdsOffsetOther;
	uint size, stride;
	pSrt->m_particleStatesOther.GetDimensions(size, stride);





	uint renderState = GetRenderState(state);


	if (renderState == kRenderStateBlue && GetFlags0(state) == 0)
	{
		// we use a additional buffer of particle states to write data to tell the other system to spawn particles. It doesn't have to be a buffer of states, but we just use it.

		uint newInfoIndex = NdAtomicIncrement(gdsOffsetNew);
		if (newInfoIndex >= size)
		{
			// decrement back
			NdAtomicDecrement(gdsOffsetNew);

			return; // can't add new information
		}


		ParticleStateV0 infoState = ParticleStateV0(0);

		infoState.m_pos = state.m_pos;
		infoState.m_scale = state.m_scale;
		infoState.m_id = 0;

		// we are creating hint list for droplets. we have control over whether those droplets will generate blood map splats and generate new drips

		bool wantGeoRibbon = (pSrt->m_features & COMPUTE_FEATURE_GENERATE_COLLISION_EVENTS_GEO_RIBBON);
		bool wantRenderTargetSplat = (pSrt->m_features & COMPUTE_FEATURE_GENERATE_BLOOD_MAP_EVENTS);
		bool wantSpawnTrackDecals = (pSrt->m_features & COMPUTE_FEATURE_GENERATE_COLLISION_EVENTS_POOL_DECAL);

		infoState.m_flags1 = (wantGeoRibbon ? kMaterialMaskSpawnChildRibbons : 0) | (wantRenderTargetSplat ? kMaterialMaskSpawnRTSplats : 0) | (wantSpawnTrackDecals ? kMaterialMaskSpawnTrackDecals : 0); // enable spawning of child ribbon drips


		pSrt->m_particleStatesOther[newInfoIndex] = infoState;
	}
}

// this function is called by ribbon spawners to start ribbons
[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeSpawnTrails(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// each group works on one ribbon

	// we only spawn new particle when the whole trail is dead, i.e. first element in the list is the sentinel

	//_SCE_BREAK();

	if (dispatchId.x != 0)
		return;

	uint gdsOffsetNew = pSrt->m_gdsOffsetOther;

	uint origSize;
	pSrt->m_particleIndicesOrig.GetDimensions(origSize);

	if (dispatchId.x >= origSize)
		return;

	uint size, stride;
	pSrt->m_particleStatesOther.GetDimensions(size, stride);

	uint origParticleIndex = pSrt->m_particleIndicesOrig[dispatchId.x];
	ParticleInstance originalParticle = pSrt->m_particleInstancesOrig[origParticleIndex];

	originalParticle.world[3].xyz = originalParticle.world[3].xyz + pSrt->m_altWorldOrigin.xyz;

	//uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), __get_vsharp(pSrt->m_particleFeedBackHeaderData), 0, 0);
	//if (prevIndex < 512)
	//{
	//	// can add new data
	//	pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, 0, groupThreadId.x, 0 /*ribbonId*/);
	//	pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(99999999, 99999999, 99999999, 99999999);
	//	pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(originalParticle.world[3].xyz, 0);
	//	pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(1, 1, 1, 0.0f);
	//}

	{
		// we use a additional buffer of particle states to write data to tell the otehr system to spawn particles. It doesn't have to be a buffer of states, but we just use it.

		uint newInfoIndex = NdAtomicIncrement(gdsOffsetNew);
		if (newInfoIndex >= size)
		{
			// decrement back
			NdAtomicDecrement(gdsOffsetNew);

			return; // can't add new information
		}


		RWStructuredBuffer<RibbonSpawnHint> destHintBuffer = __create_buffer<RWStructuredBuffer<RibbonSpawnHint> >(__get_vsharp(pSrt->m_particleStatesOther));


		RibbonSpawnHint infoState = RibbonSpawnHint(0);

		infoState.m_pos = originalParticle.world[3].xyz;
		//infoState.m_scale = state.m_scale;
		//infoState.m_flags0 = pSrt->m_rootId;
		//infoState.m_flags1 = pSrt->m_rootUniqueid;


		//infoState.m_partMat3x3[0] = originalParticle.world[0].xyz;
		//infoState.m_partMat3x3[1] = originalParticle.world[1].xyz;
		//infoState.m_partMat3x3[2] = originalParticle.world[2].xyz;


		// compress the data so that we can fit more

		float3 mat0 = originalParticle.world[0].xyz;
		float3 mat1 = originalParticle.world[1].xyz;
		float3 mat2 = originalParticle.world[2].xyz;



		float scaleX = 2.0f / originalParticle.invscale.x;
		float scaleY = 2.0f / originalParticle.invscale.y;
		float scaleZ = 2.0f / originalParticle.invscale.z;

		uint3 packedm0xyzm1xyz = uint3(PackFloat2ToUInt(mat0.x, mat0.y), PackFloat2ToUInt(mat0.z, mat1.x), PackFloat2ToUInt(mat1.y, mat1.z));

		uint3 packedm2xyzsxyz = uint3(PackFloat2ToUInt(mat2.x, mat2.y), PackFloat2ToUInt(mat2.z, scaleX), PackFloat2ToUInt(scaleY, scaleZ));
		
		
		//infoState.m_scale = float3(scaleX, scaleY, scaleZ);
		infoState.m_packedm0xyzm1xyz = packedm0xyzm1xyz;
		infoState.m_packedm2xyzsxyz = packedm2xyzsxyz;


		infoState.m_packedColor = uint2(PackFloat2ToUInt(originalParticle.color.x, originalParticle.color.y), PackFloat2ToUInt(originalParticle.color.z, originalParticle.color.w));
		infoState.m_type1_spawnerId16 = (asuint(originalParticle.partvars.z) & 0x0000FFFF); // at this point type is not determined
		infoState.m_uniqueId = asuint(originalParticle.partvars.w);
		infoState.m_age = originalParticle.partvars.x;
		destHintBuffer[newInfoIndex] = infoState;
	}
}


float3 CalculateMoveDir(ParticleComputeJobSrt *pSrt, uint2 sspos, out float3 outNormal, bool useOpaquePlusAlphaDepth)
{
	// lookup the normal
	const uint4 sample0 = pSrt->m_pPassDataSrt->m_gbuffer0[sspos];

	BrdfParameters brdfParameters = (BrdfParameters)0;
	Setup setup = (Setup)0;
	UnpackGBuffer(sample0, 0, brdfParameters, setup);

	//float3 flatX = cross(setup.normalWS, /*pSrt->m_rainDir*/ float3(0, -1, 0));
	//float3 flatZ = cross(flatX, setup.normalWS);
	//float3 moveDir = normalize(flatZ);

	float3 depthNormal = CalculateDepthNormal(pSrt, sspos, useOpaquePlusAlphaDepth);
	float3 gBufferNormal = setup.normalWS;

	float3 surfaceNormal = depthNormal;

	float3 desiredDir = float3(0, -1, 0);
	float d = dot(desiredDir, surfaceNormal);
	desiredDir -= d * surfaceNormal;
	float3 moveDir = normalize(desiredDir);

	outNormal = surfaceNormal;

	return moveDir;
}


void CalculateNewPotentialPos(ParticleComputeJobSrt *pSrt, uint ribbonType, float3 pos, uint2 sspos, float depthLinear, float depthLinearX, float depthLinearY, out uint2 newSSPos, out float3 newPartPosNdc, out bool tooHorizontal, bool useOpaquePlusAlpha)
{
	float3 normal;
	float3 moveDir = CalculateMoveDir(pSrt, sspos, normal, useOpaquePlusAlpha);

	tooHorizontal = (normal.y < -0.1f) && abs(moveDir.y) < 0.5f;

	uint objIdOld = GetObjectId(pSrt, sspos);

	
	// we move along the direction to calculate new position
	float3 newPartPosWs = pos + moveDir * RIBBON_ROOT_VAR_SPEED(ribbonType) * pSrt->m_delta; // 0.3 = move 30 cm along surface per second

	 // calculate expected depth
	float4 newPartPosH = mul(float4(newPartPosWs, 1), pSrt->m_pPassDataSrt->g_mVP);
	newPartPosNdc = newPartPosH.xyz / newPartPosH.w;

	// sample depth buffer
	uint2 newPartSSPos = uint2(floor(float2((newPartPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (newPartPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y)));
	
	//uint2 newPartSSPos = sspos + uint2(0, 4);

	uint objIdNew = GetObjectId(pSrt, newPartSSPos);

	
	uint matMaskSampleOld = pSrt->m_pPassDataSrt->m_materialMaskBuffer[sspos];
	uint4 sample1Old = pSrt->m_pPassDataSrt->m_gbuffer1[sspos];

	uint matMaskSampleNew = pSrt->m_pPassDataSrt->m_materialMaskBuffer[newPartSSPos];
	uint4 sample1New = pSrt->m_pPassDataSrt->m_gbuffer1[sspos];

	uint oldIsHair = matMaskSampleOld & (MASK_BIT_HAIR); // && sample1.w & MASK_BIT_EXTRA_CHARACTER)

	uint oldIsSkin = matMaskSampleOld & MASK_BIT_SKIN;
	uint newIsSkin = matMaskSampleNew & MASK_BIT_SKIN;

	
	bool badBoundary = false; // !oldIsHair && (objIdOld != objIdNew); // we allow crossing object id boundary from hair
	
	//if (objIdOld != objIdNew)
	//{
	//	// we might want to stop
	//	if (!oldIsHair && !(oldIsSkin && newIsSkin))
	//	{
	//		badBoundary = true;
	//	}
	//}
	//

	tooHorizontal = tooHorizontal || badBoundary;

	//tooHorizontal = false;

	newSSPos = newPartSSPos;
}

bool NewStickyParticleDepthReadjust(ParticleComputeJobSrt *pSrt, uint2 dispatchId, int threadIdInLane, ParticleStateV0 state, uint particleIndex, uint2 newPartSSPos, bool trackinAlphaDepth, uint newPartStencil, out ParticleStateV0 addedState, bool allowSkin, bool useOpaquePlusAlphaDepth)
{
	Texture2D<float> depthBufferToUse = useOpaquePlusAlphaDepth ? pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture : pSrt->m_pPassDataSrt->m_primaryDepthTexture;

	//float3 posNdcNew = float3((newPartSSPos.x) / pSrt->m_screenResolution.x * 2 - 1, -((newPartSSPos.y) / pSrt->m_screenResolution.y * 2 - 1), depthBufferToUse[newPartSSPos]);
	float3 posNdcNew = ScreenPosToNdc(pSrt, newPartSSPos, depthBufferToUse[newPartSSPos]);

	float4 posHNew = mul(float4(posNdcNew, 1), pSrt->m_pPassDataSrt->m_mAltVPInv);
	float3 posWsNew = posHNew.xyz / posHNew.w;
	posWsNew += pSrt->m_altWorldOrigin;

	uint defaultFlags1 = trackinAlphaDepth ? kMaterialMaskAlphaDepth : 0;
	int resultIndex;

	StickyParticleParams spawnParams = DefaultStickyParticleParams();
	spawnParams.allowSkin = allowSkin;
	spawnParams.opaqueAlphaDepthStencil = useOpaquePlusAlphaDepth;

	bool added = NewStickyParticle(kMotionStateAttached,
		newPartSSPos, posNdcNew, posWsNew, defaultFlags1, newPartStencil, pSrt, dispatchId,
		/*state.m_id <- */ state.m_id + 1,
		/*checkInsideFirstParticle=*/ true, /*birthTimeOffset=*/ 0.0f, /*spawn data*/ 0.0f,
		/*useProvidedIndex*/ true, /*providedParticleIndex=*/particleIndex, threadIdInLane,
		/*lifetime->m_lifeTime*/ asfloat(f32tof16(RIBBON_ROOT_VAR_LIFETIME)), addedState, /*normalHalfWayWithRain=*/ false, /*inheritedVelocity=*/ float3(0, 0, 0), /*isSnow*/ false, spawnParams, /*defaultSpeed*/ 0.0f, resultIndex, /*customSize*/ 0);



	return added;
}

#define kNumOfFramesToRecover 10

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeRibbonUpdateCompress(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{

	if (groupId.x == 0)
	{
		//_SCE_BREAK();
	}


	// each group works on one ribbon

	// we only spawn new particle when the whole trail is dead, i.e. first element in the list is the sentinel

	int iStateOffset = groupId.x * kNumPartsInRibbon;

	int particleIndex = iStateOffset + groupThreadId.x;

	ParticleStateV0 state = pSrt->m_particleStatesOld[particleIndex];

	bool isDynamicTrail = (groupId.x % 2) == 0;

	bool isSentinel = state.m_id == 0;

	// We create a mask which is 1 for all lanes that contain this value
	ulong sentinel_mask = __v_cmp_eq_u32(isSentinel, true);

	// we need to find the earliest sentinel
	uint first_bit = __s_ff1_i32_b64(sentinel_mask);
	
	uint numUpdatingStates = first_bit; // valid states

	bool isOutoFBounds = groupThreadId.x >= first_bit;

	uint startNumLanes = __s_bcnt1_i32_b64(__s_read_exec());

	float sentinelRibbonLength = pSrt->m_particleStatesOld[iStateOffset + 63].m_scale.y; // stores ribbon length

	//bool alphaDepthOnly = state.m_flags1 & kFlagAlphaDepthOnly;

	bool trackingAlphaDepth = (state.m_flags1 & kMaterialMaskAlphaDepth) != 0;

	float age = (pSrt->m_time - state.m_birthTime);

	int numNewStates = 0;
	int endNumLanes = 0;
	int isActiveThread = 0;
	int isAddingThread = 0;
	uint numLanes = 0;
	int2 lastFrameSSpos = int2(0, 0);

	bool useOpaquePlusAlpha = true;

	//pSrt->m_pPassDataSrt->m_primaryStencil
	Texture2D<uint> stencilTextureToUse = useOpaquePlusAlpha ? pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil : pSrt->m_pPassDataSrt->m_primaryStencil;
	//m_primaryDepthTexture
	Texture2D<float> depthBufferToUse = useOpaquePlusAlpha ? pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture : pSrt->m_pPassDataSrt->m_primaryDepthTexture;

	float3 preUpdatePos = state.m_pos;

	float lifeTime = f16tof32(asuint(state.m_lifeTime));

	bool haveUvs = false;

	#if ADVANCE_UV_CAPTURE
	haveUvs = GetHaveUvs(state);
	#endif


	if (pSrt->m_delta < 0.00001)
	{
		// paused frame just copy over the data

		// write the state out to new buffer
		// here we actuallyw rite all 64 lanes, if the sentinel was there before, it will be there now, along with garbage in post sentinel states
		pSrt->m_particleStates[particleIndex] = state;

		// always write sentinel
		//ulong laneMask = __s_read_exec();
		//uint numLanes = __s_bcnt1_i32_b64(laneMask);
		//pSrt->m_particleStates[iStateOffset + numLanes].m_id = 0;

		return;
	}
	
	uint needsUpdate = 1;
	uint positionComputed = 0;
	if ((age > lifeTime && !haveUvs) || isOutoFBounds)
	{
		// too old. discard
		needsUpdate = 0;
		numLanes = __s_bcnt1_i32_b64(__s_read_exec());

		if (!isOutoFBounds)
		{
			AddDebugDieStickyParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, state.m_pos, state.m_pos, 0.01f, float4(0.0f, 1.0f, 1.0f, 1.0f), GetFlags0(state), state.m_flags1, dispatchId.x, state.m_id, state.m_birthTime);
		}

		if (startNumLanes == numLanes)
		{
			// if all thread are here, we need to write the sentinel, because no other thread will
			pSrt->m_particleStates[iStateOffset].m_id = 0;
			pSrt->m_particleStates[iStateOffset].m_scale.x = 0;

			return;
		}
	}

	// for those who need update, we compute position based on stored barycentric coords
	
	FindProcessResults findProcOrig = 0;
	
	uint3 indices = uint3(0);
	#if TRACK_INDICES
	indices.x = asuint(state.m_speed) & 0x0000FFFF;
	indices.y = asuint(state.m_speed) >> 16;
	indices.z = asuint(state.m_lifeTime) >> 16;
	uint meshId24 = DecodeMeshId24FromRotation(state.m_rotation);
	float2 barys = DecodeBarysFromRotation(state.m_rotation);

	{
		findProcOrig = RecomputeMeshTriangleBaricentricsDivergent(pSrt, needsUpdate, groupThreadId.x, meshId24, indices, barys);
		positionComputed = findProcOrig.m_foundIndex != -1;
		
		if (positionComputed)
		{
			#if !ADVANCE_UV_CAPTURE
			SetStartedAtHead(state);
			SetUintNorm(state, findProcOrig.m_uNorm);
			#endif
			AddDebugStickyParticleRecomputed(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, findProcOrig.m_posWs, state.m_pos, dispatchId.x, state.m_id, meshId24, indices, barys, true);
		}
		else
		{
			#if !ADVANCE_UV_CAPTURE
			SetNotStartedAtHead(state);
			#endif

			if (needsUpdate)
			{
				// failed recompute
				_SCE_BREAK();
				AddDebugStickyParticleRecomputed(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, findProcOrig.m_posWs, state.m_pos, dispatchId.x, state.m_id, meshId24, indices, barys, false);
			}
		}

		//positionComputed = false; // turn off while debugging
	}
	#endif
	

	uint myIndex = 0; // final index of this state. could be different since we compress everything down

	if (needsUpdate)
	{

		int trackingResult = 0;
		float depthDifRightAway = 0;
		float unexpectedOffsetLen = 0;

#define kTrackingOk 0
#define kTrackingBadFlagsRightAway 1
#define kTrackingStencilMismatchRightAway (1 << 1)
#define kTrackingDepthMismatchRightAway (1 << 2)
#define kTrackingStencilMismatchUsingOldPosition (1 << 3)
#define kTrackingDepthMismatchUsingOldPosition (1 << 4)
#define kTrackingDepthUseReadjustedPosition (1 << 5)
#define kTrackingDepthDontReadjustPosition (1 << 6)
#define kTrackingDepthReadjustDidntWorkButTooMuchUnexpectedMovement (1 << 7)

		// continue life. run update:
		float3 prevPos = state.m_pos;

		state.m_pos = state.m_pos + state.m_scale * pSrt->m_delta; //  we encode direction in scale

		if (GetFlags0(state) < kNumOfFramesToRecover && positionComputed)
		{
			// we have computed new position because the mesh data is available

			// we now need to see if we can generate screen space tracking info. best case scenario, this position is still in screen space, in which case we just update point velocity and we're done
			// in case we are not in screen space we just set the velocity to 0

			state.m_pos = findProcOrig.m_posWs;
			SetFlags0(state, 0);
		}

		float3 origNewPos = state.m_pos;

		if (GetFlags0(state) != 0)
		{
			// keep them around, different color though
			state.m_pos = prevPos;
			//return;
		}

		// calculate expected depth
		float4 newPosH = mul(float4(state.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 newPosNdc = newPosH.xyz / newPosH.w;

		float depthBufferError = GetLinearDepthError(newPosNdc.z, pSrt->m_depthParams);

		float allowedDepthBufferDifference = pSrt->m_rootComputeVec2.x + depthBufferError * pSrt->m_rootComputeVec2.y * 2;
		// sample depth buffer
		//uint2 sspos = uint2((newPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (newPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);
		uint2 sspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);

		
		if (abs(newPosNdc.x) >= 1.0 || abs(newPosNdc.y) >= 1.0)
		{
			// expected position of this particle is off screen, so we just kill it
			// we do it only for particles that are purely screen tracked
			SetFlags0(state, kNumOfFramesToRecover);
		}

		uint prevStencil = state.m_flags1 & 0x000000FF;
		bool prevWasOnHair = (state.m_flags1 & kMaterialMaskHair) != 0;

		// note we allow to try to recover for certain amount of frames
		uint origFlags = 0;
		if (GetFlags0(state) < kNumOfFramesToRecover)
		{
			SetFlags0(state, 0);
			origFlags = GetFlags0(state);
		}

		
		if (GetFlags0(state) == 0)
		{
			//state.m_birthTime = pSrt->m_time; // prolongue the life

			//uint4 sample1 = pSrt->m_pPassDataSrt->m_gbuffer1[sspos];
			//uint materialMaskSample = pSrt->m_pPassDataSrt->m_materialMaskBuffer[sspos];
			//UnpackMaterialMask(materialMaskSample, sample1, setup.materialMask);


			if (prevStencil != stencilTextureToUse[sspos])
			{
				// completely jumped off to something different
				if (!positionComputed)
					state.m_pos = prevPos; // move back, redo depth at position from before
				SetFlags0(state, 1);

				trackingResult = trackingResult | kTrackingStencilMismatchRightAway;

				// will try again with old position and see if we can recover
				//return;
			}

			float depthNdc = depthBufferToUse[sspos];
			float expectedDepthNdc = newPosNdc.z;

			float expectedDepthLinear = GetLinearDepth(expectedDepthNdc, pSrt->m_depthParams);
			float depthLinear = GetLinearDepth(depthNdc, pSrt->m_depthParams);

			// since we reapply additional offset below we check distance for just very big displacement
			depthDifRightAway = abs(depthLinear - expectedDepthLinear);
			if (depthDifRightAway > allowedDepthBufferDifference)
			{
				// completely jumped off to something different (same as stencil)
				if (!positionComputed)
					state.m_pos = prevPos; // move back, redo depth at position from before
				SetFlags0(state, 1);

				trackingResult = trackingResult | kTrackingDepthMismatchRightAway;

				// will try again with old position and see if we can recover
				//return;
			}

			// allow to recover
			if (GetFlags0(state) != 0 && !positionComputed)
			{
				// we just jumped off somewhere bad, try using old position and redo the tests
				// calculate expected depth
				newPosH = mul(float4(state.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
				newPosNdc = newPosH.xyz / newPosH.w;

				// sample stencil and depth buffer
				//sspos = uint2((newPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (newPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);
				sspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);

				bool ok = true;
				if (prevStencil != stencilTextureToUse[sspos] || abs(newPosNdc.x) > 1.0 || abs(newPosNdc.y) > 1.0)
				{
					// completely jumped off to something different, this is fatal
					// we don't change the state
					ok = false;

					trackingResult = trackingResult | kTrackingStencilMismatchUsingOldPosition;

				}

				depthNdc = depthBufferToUse[sspos];
				expectedDepthNdc = newPosNdc.z;

				expectedDepthLinear = GetLinearDepth(expectedDepthNdc, pSrt->m_depthParams);
				depthLinear = GetLinearDepth(depthNdc, pSrt->m_depthParams);

				if (abs(depthLinear - expectedDepthLinear) > allowedDepthBufferDifference)
				{
					// completely jumped off to something different, this is fatal
					// we don't change the state
					ok = false;

					trackingResult = trackingResult | kTrackingDepthMismatchUsingOldPosition;
				}

				if (ok)
				{
					// prev position is fine, just stay there
					SetFlags0(state, 0);
					//origFlags = 0;
				}
			}

			// if we were at bad pixel but computed position (computed position is offscreen but still comptable) state.m_flags0 != 0

			if (GetFlags0(state) == 0)
			{
				// good, readjust position to snap to background

				newPosNdc.z = depthNdc;

				newPosH = mul(float4(newPosNdc, 1), pSrt->m_pPassDataSrt->m_mAltVPInv);
				
				if (!positionComputed)
				{
					// we do this only if we havent already computed the new position
					state.m_pos = newPosH.xyz / newPosH.w;
					state.m_pos += pSrt->m_altWorldOrigin;
				}

				// recalculate velocity based on motion vector
				float2 motionVectorSNorm = pSrt->m_pPassDataSrt->m_motionVector[sspos];
				int2 motionVector = int2(motionVectorSNorm * pSrt->m_screenResolution);
				lastFrameSSpos = int2(float2(sspos)+motionVector);

				float3 posNdcUnjittered = newPosNdc;
				posNdcUnjittered.xy -= pSrt->m_projectionJitterOffsets.xy;

				float3 lastFramePosNdcUnjittered = float3(posNdcUnjittered.x + motionVectorSNorm.x * 2, posNdcUnjittered.y - motionVectorSNorm.y * 2, pSrt->m_pPassDataSrt->m_lastFrameOpaquePlusAlphaDepthTexture[lastFrameSSpos]);
				float3 lastFramePosNdc = lastFramePosNdcUnjittered;
				lastFramePosNdc.xy += pSrt->m_projectionJitterOffsets.zw;

				float4 lastFramePosH = mul(float4(lastFramePosNdc, 1), pSrt->m_pPassDataSrt->m_mLastFrameAltVPInv);
				float3 lastFramePosWs = lastFramePosH.xyz / lastFramePosH.w;
				lastFramePosWs += pSrt->m_lastFrameAltWorldOrigin;

				if (trackingAlphaDepth)
				{
					// we can't look at motion vectors. we assume last frame's position was the same as particle's position
					lastFramePosWs = prevPos;
				}


				#if 0
				// readjust the speed and let the particle live longer

				state.m_scale = (state.m_pos - lastFramePosWs) / pSrt->m_delta; // we store velocity in scale
				#else
				if (positionComputed)
				{
					state.m_scale = (state.m_pos - lastFramePosWs) / pSrt->m_delta; // we store velocity in scale
				}
				else
				{
					// The code below is either
					// 1) stop any more processing, use current position as final position and recompute the new velocity
					// 2) Compare previous position of current pixel with expected previous position (previous position of particle)
					//    and apply the difference and re compute the new current position with difference applied, then choose the new position vs recomputed new position
					//    this recomputing allows for tracking on objects that accelerate or decelerate

					// this is the code to accomodate acceleration
					// 
					if (origFlags)
					{
						// if we had several frames of error, but below death count
						prevPos = lastFramePosWs;
					}

					// now we compare previous position of this pixel with where this particle was last frame
					// if all is good, this offset should be 0
					// if it is not 0 means that the object moved at a different speed than we predicted
					//M1:
					float3 lastFramePosUnexpectedOffset = prevPos - lastFramePosWs;
					//M2
					//float3 lastFramePosUnexpectedOffset = (state.m_scale * pSrt->m_delta) - (state.m_pos - lastFramePosWs);

					unexpectedOffsetLen = length(lastFramePosUnexpectedOffset);

					//M1
					prevPos = state.m_pos;
					//M2
					//prevPos = preUpdatePos;


					// we can now readjust our position by that offset and redo everything
					//M1
					state.m_pos = state.m_pos + lastFramePosUnexpectedOffset;
					//M2
					//state.m_pos = preUpdatePos + (state.m_pos - lastFramePosWs);

					newPosH = mul(float4(state.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
					newPosNdc = newPosH.xyz / newPosH.w;

					// sample depth buffer
					//sspos = uint2((newPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (newPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);
					uint2 prevSSPos = sspos;
					sspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);

					depthNdc = depthBufferToUse[sspos];
					expectedDepthNdc = newPosNdc.z;

					expectedDepthLinear = GetLinearDepth(expectedDepthNdc, pSrt->m_depthParams);
					depthLinear = GetLinearDepth(depthNdc, pSrt->m_depthParams);

					// we HAVE to apply unexpected offset to deal with acceleration of underlying surfaces
					// but if applying that offset lands on a pixel that still doesn't match our previous position then that offset is bad


					// recalculate velocity based on motion vector
					motionVectorSNorm = pSrt->m_pPassDataSrt->m_motionVector[sspos];
					motionVector = int2(motionVectorSNorm * pSrt->m_screenResolution);
					lastFrameSSpos = int2(float2(sspos)+motionVector);

					posNdcUnjittered = newPosNdc;
					posNdcUnjittered.xy -= pSrt->m_projectionJitterOffsets.xy;

					lastFramePosNdcUnjittered = float3(posNdcUnjittered.x + motionVectorSNorm.x * 2, posNdcUnjittered.y - motionVectorSNorm.y * 2, pSrt->m_pPassDataSrt->m_lastFrameOpaquePlusAlphaDepthTexture[lastFrameSSpos]);
					lastFramePosNdc = lastFramePosNdcUnjittered;
					lastFramePosNdc.xy += pSrt->m_projectionJitterOffsets.zw;

					lastFramePosH = mul(float4(lastFramePosNdc, 1), pSrt->m_pPassDataSrt->m_mLastFrameAltVPInv);
					lastFramePosWs = lastFramePosH.xyz / lastFramePosH.w;
					lastFramePosWs += pSrt->m_lastFrameAltWorldOrigin;

				
					// in case we lost tracking this frame by jumping based on our velocity and used just previous frames data as starting point
					// we can also retry old position + old velocity + unexpected offset

					{
						float3 origPosAdjusted = origNewPos + lastFramePosUnexpectedOffset;
						float4 origPosAdjustedH = mul(float4(origPosAdjusted, 1), pSrt->m_pPassDataSrt->g_mVP);
						float3 origPosAdjustedNdc = origPosAdjustedH.xyz / origPosAdjustedH.w;


						uint2 origAdjustedSSpos = NdcToScreenSpacePos(pSrt, origPosAdjustedNdc.xy);

						float origAdjustedDepthNdc = depthBufferToUse[origAdjustedSSpos];

						//expectedDepthNdc = newPosNdc.z;

						float origAdjustedDepthLinear = GetLinearDepth(origAdjustedDepthNdc, pSrt->m_depthParams);


						float2 origAdjustedMotionVectorSNorm = pSrt->m_pPassDataSrt->m_motionVector[origAdjustedSSpos];
						int2 origAdjustedotionVector = int2(origAdjustedMotionVectorSNorm * pSrt->m_screenResolution);
						int2 origAdjustedLastFrameSSpos = int2(float2(origAdjustedSSpos)+origAdjustedotionVector);

						//posNdcUnjittered = newPosNdc;
						//posNdcUnjittered.xy -= pSrt->m_projectionJitterOffsets.xy;
						//
						//lastFramePosNdcUnjittered = float3(posNdcUnjittered.x + motionVectorSNorm.x * 2, posNdcUnjittered.y - motionVectorSNorm.y * 2, pSrt->m_pPassDataSrt->m_lastFrameOpaquePlusAlphaDepthTexture[lastFrameSSpos]);
						//lastFramePosNdc = lastFramePosNdcUnjittered;
						//lastFramePosNdc.xy += pSrt->m_projectionJitterOffsets.zw;
						//
						//lastFramePosH = mul(float4(lastFramePosNdc, 1), pSrt->m_pPassDataSrt->m_mLastFrameAltVPInv);
						//lastFramePosWs = lastFramePosH.xyz / lastFramePosH.w;
						//lastFramePosWs += pSrt->m_lastFrameAltWorldOrigin;

					}


					if (abs(depthLinear - expectedDepthLinear) > 0.2f || prevStencil != stencilTextureToUse[sspos])
					{
						// this result is completely un acceptable
					}
					else
					{
						// this result is potentially good. lets compare it to non-unexpected-adjusted position
					}

					if (abs(depthLinear - expectedDepthLinear) > 0.2f || prevStencil != stencilTextureToUse[sspos] || length(lastFramePosWs-prevPos) > 0.03)
					{
						// readjusting made it much worse,  stay as before (first version was successful, to a point. let's see how much of the unexpected offset do we have)

						if (length(lastFramePosUnexpectedOffset) > 0.03)
						{
							// we have too much unexpected offsetm, and adjusting by it didn't help. we have to bail
							SetFlags0(state, 1);

							trackingResult = trackingResult | kTrackingDepthReadjustDidntWorkButTooMuchUnexpectedMovement;
						}
						else
						{
							//we had some unexpected offset, we couldn't readjust but choose to still stick around
							state.m_pos = prevPos;
							state.m_scale = (state.m_pos - prevPos) / pSrt->m_delta; // we store velocity in scale
							sspos = prevSSPos;

							trackingResult = trackingResult | kTrackingDepthDontReadjustPosition;
						}
					}
					else
					{

						trackingResult = trackingResult | kTrackingDepthUseReadjustedPosition;

						// good, readjust position

						newPosNdc.z = depthNdc;

						newPosH = mul(float4(newPosNdc, 1), pSrt->m_pPassDataSrt->m_mAltVPInv);
						state.m_pos = newPosH.xyz / newPosH.w;
						state.m_pos += pSrt->m_altWorldOrigin;


					


						state.m_scale = (state.m_pos - lastFramePosWs) / pSrt->m_delta; // we store velocity in scale


						if (trackingAlphaDepth)
						{
							// we can't look at motion vectors. we assume last frame's position was the same as particle's position
							state.m_scale = float3(0, 0, 0);
						}

					}
				// so we found what seems to be a good position. do some other tests.
				// for example we could be on hair now
				} // if !positionComputed
				#endif
			}
			else
			{
				if (positionComputed)
				{
					// this means we have a good position, but it is not visible
					// we will clear the error, so we can use this state, but we will have to clear out the velocity since we have no screen space data
					SetFlags0(state, 0);
					state.m_scale = float3(0, 0, 0);
				}
			}
		}
		else
		{
			trackingResult = kTrackingBadFlagsRightAway;
		}


		if (GetFlags0(state) != 0) // if != we either are on first frame of error or we continuing error state
		{
			SetFlags0(state, GetFlags0(state) + origFlags); // accumulate error frames

			// very harsh condition, basically we force to die immediately when we loose tracking
			SetFlags0(state, kNumOfFramesToRecover);

		}

		//bool wasNewParticle = state.m_data <= 0;

		//state.m_data += 1.0f; // increment with each life frame

		// write the state out to new buffer

		// at this point we have some threads that have succeeded and some that have died from lifetime
		// each active thread will grab its new index
		// note we have invlaid states here that will not be rendered. ideally we could remove them and mark the preceding invisible

		// if state is invisible it has state.m_flags0 != 0

		

		AddDebugRibbonStickyParticleTracking(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, state.m_pos, preUpdatePos, 0.01f, trackingResult, depthDifRightAway, unexpectedOffsetLen);

		AddDebugStickyParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, state.m_pos, preUpdatePos, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), GetFlags0(state), state.m_flags1, dispatchId.x, state.m_id, state.m_birthTime);

		
#if 1
		ulong laneMask = __s_read_exec();
		numLanes = __s_bcnt1_i32_b64(laneMask);


		uint laneMask_lo = (uint)laneMask;
		uint laneMask_hi = (uint)(laneMask >> 32);
		myIndex = __v_mbcnt_hi_u32_b32(laneMask_hi, myIndex);
		myIndex = __v_mbcnt_lo_u32_b32(laneMask_lo, myIndex);
#endif
		// now particle index is the new compressed location

		particleIndex = iStateOffset + myIndex;
		uint renderState = GetRenderState(state);

		Setup setup = (Setup)0;
		uint4 sample1 = pSrt->m_pPassDataSrt->m_gbuffer1[sspos];
		uint materialMaskSample = pSrt->m_pPassDataSrt->m_materialMaskBuffer[sspos];
		UnpackMaterialMask(materialMaskSample, sample1, setup.materialMask);

		// we might be now on hair. mark the state as such. note we never clear it, because once on hair, we likely have bad tracking and we just dont render particles that are on hair
		state.m_flags1 = state.m_flags1 | (setup.materialMask.hasHair ? kMaterialMaskHair : 0);


		pSrt->m_particleStates[particleIndex] = state;

		float allowedDepthError = pSrt->m_rootComputeVec2.x + depthBufferError * pSrt->m_rootComputeVec2.y * 2;

		isActiveThread = true;


#define ADD_STATES_LATER 0



		if (myIndex == numLanes - 1)
		{
#if 1
			// also we can move this position and generate new particle that is the bleeding edge of a trail
			if ((numLanes < 62) && isDynamicTrail && GetFlags0(state) == 0 && renderState != kRenderStateBlue /* && ((state.m_id & 0x00FF) < 128)*/)
			{
				isAddingThread = 1;
				#if !ADD_STATES_LATER

				// this is the last particle. it can try spawning new particle
				
				uint ribbonType = (state.m_id >> (8 + 6)) & 1;
				bool allowSkin = ribbonType == 0; // onl;y hard ribbons can be on skin
				bool added = false;
				ParticleStateV0 addedState = ParticleStateV0(0);
				uint2 addedPartSSPos;
				{
					bool tooHorizontal;
					uint2 newPartSSPos;
					float3 newPartPosNdc;


					//float depthNdc = depthBufferToUse[sspos + uint2(0, 0)];
					//float depthLinear = GetLinearDepth(depthNdc, pSrt->m_depthParams);
					//float depthNdcX = depthBufferToUse[sspos + uint2(1, 0)];
					//float depthLinearX = GetLinearDepth(depthNdcX, pSrt->m_depthParams);
					//float depthNdcY = depthBufferToUse[sspos + uint2(0, 1)];
					//float depthLinearY = GetLinearDepth(depthNdcY, pSrt->m_depthParams);

					
					CalculateNewPotentialPos(pSrt, ribbonType, state.m_pos, sspos, 0, 0, 0, newPartSSPos, newPartPosNdc, tooHorizontal, useOpaquePlusAlpha);

					addedPartSSPos = newPartSSPos;

					if (newPartSSPos.x < pSrt->m_screenResolution.x && newPartSSPos.y < pSrt->m_screenResolution.y)
					{
						uint newPartStencil = stencilTextureToUse[newPartSSPos];
						float expectedDepthLinear = GetLinearDepth(newPartPosNdc.z, pSrt->m_depthParams);
						float depthLinear = GetLinearDepth(depthBufferToUse[newPartSSPos], pSrt->m_depthParams);
						bool newTrackingAlphaDepth = depthBufferToUse[newPartSSPos] != pSrt->m_pPassDataSrt->m_primaryDepthTexture[newPartSSPos];

						prevStencil = newPartStencil;

						if ((newPartStencil == prevStencil) && (abs(depthLinear - expectedDepthLinear) < allowedDepthError) && !tooHorizontal)
						{
							// create new particle at slightly different location
							ParticleStateV0 newState = ParticleStateV0(0);
							added = NewStickyParticleDepthReadjust(pSrt, dispatchId, groupThreadId.x, state, iStateOffset + numLanes, newPartSSPos, newTrackingAlphaDepth, newPartStencil, newState, allowSkin, useOpaquePlusAlpha);
							addedState = newState;

							if (added)
							{
								// low bits store distance to next element
								uint oldVal = asuint(pSrt->m_particleStates[iStateOffset + numLanes - 1].m_data);
								oldVal |= f32tof16(length(state.m_pos - addedState.m_pos));
								pSrt->m_particleStates[iStateOffset + numLanes - 1].m_data = asfloat(oldVal);

								numLanes++;
								numNewStates++;

								AddDebugNewStickyParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, addedState.m_pos, state.m_pos, 0.01f, float4(1.0f, 1.0f, 0.0f, 1.0f), GetFlags0(addedState), addedState.m_flags1, dispatchId.x, addedState.m_id, addedState.m_birthTime);


								//sentinelRibbonLength += sqrt(distSqr);

								//uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), __get_vsharp(pSrt->m_particleFeedBackHeaderData), 0, 0);
								//if (prevIndex < 512)
								//{
								//	// can add new data
								//	pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, 0, groupThreadId.x, 0 /*ribbonId*/);
								//	pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(0, 0, 0, 0);
								//	pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(addedState.m_pos, 0);
								//	pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(0, 1, 1, 0.0f);
								//}
							}
							else
							{
								// failed because something mismatched in pixel
								AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, state.m_pos, preUpdatePos, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), GetFlags0(state), state.m_flags1, dispatchId.x, state.m_id, ParticleFailReason_AddFail);
							}
						}
						else
						{
							// couldn't create the particle. Are we on a curve?
							// for now just set the state
							SetRenderState(state, kRenderStateBlue);
							pSrt->m_particleStates[iStateOffset + numLanes - 1].m_flags1 = state.m_flags1;

							// failed because something mismatched
							AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, state.m_pos, preUpdatePos, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), GetFlags0(state), state.m_flags1, dispatchId.x, state.m_id, ParticleFailReason_StencilHorizontalDepth);
						}
					}
				}

				// can try adding another one
				#if ADVANCE_UV_CAPTURE
				int kNumTries = 16; // 8;

				while (added && kNumTries > 0 && numLanes < 62)
				{
					kNumTries -= 1;

					bool tooHorizontal;
					uint2 newPartSSPos;
					float3 newPartPosNdc;
					CalculateNewPotentialPos(pSrt, ribbonType, addedState.m_pos, addedPartSSPos, 0, 0, 0, newPartSSPos, newPartPosNdc, tooHorizontal, useOpaquePlusAlpha);
					added = false;
					if (/*impossible condition && renderState != kRenderStateBlue*/ newPartSSPos.x < pSrt->m_screenResolution.x && newPartSSPos.y < pSrt->m_screenResolution.y)
					{
						uint newPartStencil = stencilTextureToUse[newPartSSPos];
						float expectedDepthLinear = GetLinearDepth(newPartPosNdc.z, pSrt->m_depthParams);
						float depthLinear = GetLinearDepth(depthBufferToUse[newPartSSPos], pSrt->m_depthParams);
						bool newTrackingAlphaDepth = depthBufferToUse[newPartSSPos] != pSrt->m_pPassDataSrt->m_primaryDepthTexture[newPartSSPos];

						if ((newPartStencil == prevStencil) && (abs(depthLinear - expectedDepthLinear) < allowedDepthError) && !tooHorizontal)
						{
							// create new particle at slightly different location
							ParticleStateV0 newState = ParticleStateV0(0);
							added = NewStickyParticleDepthReadjust(pSrt, dispatchId, groupThreadId.x, addedState, iStateOffset + numLanes, newPartSSPos, newTrackingAlphaDepth, newPartStencil, newState, allowSkin, useOpaquePlusAlpha);

							if (added)
							{
								// low bits store distance to next element
								uint oldVal = asuint(pSrt->m_particleStates[iStateOffset + numLanes - 1].m_data);
								oldVal |= f32tof16(length(addedState.m_pos - newState.m_pos));
								pSrt->m_particleStates[iStateOffset + numLanes - 1].m_data = asfloat(oldVal);


								numLanes++;
								numNewStates++;

								AddDebugNewStickyParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, newState.m_pos, addedState.m_pos, 0.01f, float4(1.0f, 1.0f, 0.0f, 1.0f), GetFlags0(newState), newState.m_flags1, dispatchId.x, newState.m_id, newState.m_birthTime);

								//sentinelRibbonLength += sqrt(distSqr);

								//uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), __get_vsharp(pSrt->m_particleFeedBackHeaderData), 0, 0);
								//if (prevIndex < 512)
								//{
								//	// can add new data
								//	pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, 0, groupThreadId.x, 0 /*ribbonId*/);
								//	pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(0, 0, 0, 0);
								//	pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(newState.m_pos, 0);
								//	pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(0, 1, 1, 0.0f);
								//}

								addedState = newState;
								addedPartSSPos = newPartSSPos;
							}
						}
						else
						{
							// couldn't create the particle. Are we on a curve?
							// for now just set the state
							SetRenderState(state, kRenderStateBlue);
							pSrt->m_particleStates[iStateOffset + numLanes - 1].m_flags1 = state.m_flags1;
						}
					}
				}
				#endif // if ADVANCE_UV_CAPTURE
				#endif // if !ADD_STATES_LATER

			} // if trying to add a new particle
#endif
			// also write the sentinel
			// we write the sentinel after the last good state.
			pSrt->m_particleStates[iStateOffset + numLanes].m_id = 0;
			pSrt->m_particleStates[iStateOffset + numLanes].m_scale.x = 0;
			pSrt->m_particleStates[iStateOffset + 63].m_scale.y = sentinelRibbonLength; // this is mainly to handle the case when we were not adding anything
		} // if last thread

	} // if good life time (if needs update)

	#if TRACK_INDICES
	if (true)
	{
		bool needRemap = needsUpdate && !positionComputed;

		ParticleStateV0 addedState = pSrt->m_particleStates[iStateOffset + myIndex]; // this is sbuffer read. but this state should not be in k-cache, we wrote it to L2, and now will read into k-cache

		needRemap = needRemap && (GetFlags0(addedState) == 0); // we only remap the particles we know have succeeded the update well

		int addedStateValid = 0;

		// check if too far in bind pose space

		float4 newPosH = mul(float4(addedState.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 newPosNdc = newPosH.xyz / newPosH.w;

		uint2 addedSspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);


		FindProcessResults findProc = FindProcessMeshTriangleBaricentricsDivergent(pSrt, needRemap, addedSspos, addedState.m_pos, groupThreadId.x, false, BLOOD_RIBBON_SPAWN_BLOOD_MAP_THRESHOLD, true);


		if (findProc.m_foundIndex != -1)
		{
			pSrt->m_particleStates[iStateOffset + myIndex].m_rotation = EncodeRotationFromBindPose(findProc.m_bindPosLs, findProc.m_baricentrics, findProc.m_meshId);
			pSrt->m_particleStates[iStateOffset + myIndex].m_speed = asfloat((findProc.m_indices.x & 0x0000FFFF) | (findProc.m_indices.y << 16));
			pSrt->m_particleStates[iStateOffset + myIndex].m_lifeTime = asfloat((asuint(pSrt->m_particleStates[iStateOffset + myIndex].m_lifeTime) & 0x0000FFFF) | (findProc.m_indices.z << 16));

			#if !ADVANCE_UV_CAPTURE
			SetStartedAtHead(pSrt->m_particleStates[iStateOffset + myIndex]);
			SetUintNorm(pSrt->m_particleStates[iStateOffset + myIndex], findProc.m_uNorm);
			#endif

			AddDebugStickyBaryRemapParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, findProc.m_posWs, addedState.m_pos, dispatchId.x, addedState.m_id, findProc.m_meshId, findProc.m_indices, findProc.m_baricentrics.xy);
		}
	}
	#endif



	__s_dcache_inv();

	// now we are back to all threads activated
	// go trhough all new particles and check for blood

	// by now the states have been compacted
	ulong laneMask = __v_cmp_eq_u32(isActiveThread, 1); // active threads here are not compacted. i.e. they dont start at 0. the written states are compacted. so this mask could be 0000111000, and states written are 0, 1, 2
	int numActiveStates = __s_bcnt1_i32_b64(laneMask); // should be guaranteed to never be 0



	ulong addingMask = __v_cmp_eq_u32(isAddingThread, 1);

	if (numActiveStates <= 0)
		return;

	int addingThread = __s_ff1_i32_b64(addingMask); // should be the last active thread that corresponded to last state. note, again this is not compacted. this id could be 5, but was written into state 3.
	                                                // also this thread should have written the last state (or there is a bug)

	if (addingThread == -1)
		return;

	// adding thread was marked because it has good state and is last
	numNewStates = ReadLane(numNewStates, asuint(addingThread)); // this thread knows the correct value of number of new states

	numLanes = ReadLane(numLanes, asuint(addingThread));

	ParticleStateV0 precedingState = pSrt->m_particleStates[iStateOffset + numActiveStates - 1];
	
	float3 precedingBindPose = DecodeBindPosFromRotation(precedingState.m_rotation);

	// if new particles were added, the last state was guaranteed to have valid positions, so that thread has valid lastFrameSSpos
	lastFrameSSpos.x = ReadLane(lastFrameSSpos.x, asuint(addingThread)); // TODO: maybe just recalculate to save on registers
	lastFrameSSpos.y = ReadLane(lastFrameSSpos.y, asuint(addingThread));


	

	// lookup data from preceding state. we technically could have it stored in the state too
	uint preceedingMeshId = -1;
	uint preceedingBodyPart = 255;
	FindProcessResults findPreceedingProc;
	{
		float4 precPosH = mul(float4(precedingState.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 precPosNdc = precPosH.xyz / precPosH.w;

		uint2 precSspos = NdcToScreenSpacePos(pSrt, precPosNdc.xy);

		findPreceedingProc = FindProcessMeshTriangleBaricentrics(pSrt, precSspos, precedingState.m_pos, groupThreadId.x);
		// one thread potentially succeeds
		if (findPreceedingProc.m_foundIndex != -1)
		{
			preceedingMeshId = findPreceedingProc.m_meshId;
			preceedingBodyPart = findPreceedingProc.m_bodyPart;
		}
	}

	uint ribbonType = (precedingState.m_id >> (8 + 6)) & 1;
	bool allowHead = ribbonType == 0; // don't allow head on soft ribbons

	ulong lookupStateValid = __v_cmp_ne_u32(findPreceedingProc.m_foundIndex, uint(-1));
	int lookupThread = __s_ff1_i32_b64(lookupStateValid); // which thread succeeded

	if (lookupThread != -1)
	{
		preceedingMeshId = ReadLane(preceedingMeshId, uint(lookupThread));
		preceedingBodyPart = ReadLane(preceedingBodyPart, uint(lookupThread));	
	}
	else
	{
		// stays -1, will fail if we find a valid mesh below
	}

	// go through added states and check the distances in bind pose and potentially discard the new states

	if (dispatchId.x == ReadFirstLane(dispatchId.x))
	{
		AddDebugSetSentinelEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, precedingState.m_pos, precedingState.m_pos, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), GetFlags0(precedingState), precedingState.m_flags1, dispatchId.x, precedingState.m_id, numNewStates);
	}

	#if TRACK_BIND_POSE
	for (int iNew = 0; iNew < numNewStates; ++iNew)
	{
		//precedingBloodAmount = ReadLane(precedingBloodAmount, asuint(addingThread));

		isAddingThread = 0;
		uint localIndex = numActiveStates - 1 + iNew + 1;
		uint stateIndex = iStateOffset + localIndex;
		ParticleStateV0 addedState = pSrt->m_particleStates[stateIndex]; // this is sbuffer read. but this state should not be in k-cache, we wrote it to L2, and now will read into k-cache


		int addedStateValid = 0;

		// check if too far in bind pose space

		float4 newPosH = mul(float4(addedState.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 newPosNdc = newPosH.xyz / newPosH.w;

		uint2 addedSspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);

	
		// all threads must be active for this to work
		FindProcessResults findProc = FindProcessMeshTriangleBaricentrics(pSrt, addedSspos, addedState.m_pos, groupThreadId.x);
		// one thread potentially succeeds
		float travelDist = 0;
		if (findProc.m_foundIndex != -1)
		{
			int bodyPart = findProc.m_bodyPart;
			if (allowHead || bodyPart != kLook2BodyPartHead)
			{
				pSrt->m_particleStates[stateIndex].m_rotation = EncodeRotationFromBindPose(findProc.m_bindPosLs, findProc.m_baricentrics, findProc.m_meshId); // 32 x, 16bits y
				#if TRACK_INDICES
				pSrt->m_particleStates[stateIndex].m_speed = asfloat((findProc.m_indices.x & 0x0000FFFF) | (findProc.m_indices.y << 16));
				pSrt->m_particleStates[stateIndex].m_lifeTime = asfloat(asuint(pSrt->m_particleStates[stateIndex].m_lifeTime) | (findProc.m_indices.z << 16));

				#if !ADVANCE_UV_CAPTURE
				SetStartedAtHead(pSrt->m_particleStates[stateIndex]);
				SetUintNorm(pSrt->m_particleStates[stateIndex], findProc.m_uNorm);
				#endif
				#endif

				#if ADVANCE_UV_CAPTURE
				SetHaveUvs(pSrt->m_particleStates[stateIndex]);

				if (findProc.m_procType == 0x1)
				{
					SetIsInfected(pSrt->m_particleStates[stateIndex]);
				}

				#endif
				#if ADVANCE_UV_CAPTURE && !TRACK_INDICES
				pSrt->m_particleStates[stateIndex].m_speed = asfloat(PackFloat2ToUInt(findProc.m_uv.x, findProc.m_uv.y));
				#endif

				float3 distVec = findProc.m_bindPosLs - precedingBindPose;

				AddDebugNewStickyBaryDataParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, findProc.m_posWs, addedState.m_pos, dispatchId.x, addedState.m_id, findProc.m_meshId, findProc.m_indices, findProc.m_baricentrics.xy);
				
				float distSqr = dot(distVec, distVec);

				float expectedMoveDist = RIBBON_ROOT_VAR_SPEED(ribbonType) * pSrt->m_delta;

				float allowedMoveDistInBindPose = max(expectedMoveDist * 3.0f, 0.03); // allow a little extra to accomodate stretching of skinned vs bindpose. we always allow a couple of centimeter motion because of imprecisions of going from one pixel to the other
				float allowedMoveDistInBindPoseSqr = allowedMoveDistInBindPose * allowedMoveDistInBindPose;

				bool distanceOk = distSqr < allowedMoveDistInBindPoseSqr;
				bool meshIdOk = findProc.m_meshId == preceedingMeshId;
				bool bodyPartOk = findProc.m_bodyPart == preceedingBodyPart;
				if (distanceOk)//  && bodyPartOk)
				{
					addedStateValid = 1;

					travelDist = sqrt(distSqr);
				}
				else
				{
					// failed because of bind pose distance check
					if (!distanceOk)
					{
						AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, addedState.m_pos, /*prevPos*/findProc.m_bindPosLs, expectedMoveDist, float4(precedingBindPose, 1.0f), GetFlags0(addedState), asuint(distSqr), dispatchId.x, addedState.m_id, ParticleFailReason_TooMuchBindPoseMotion);
					}

					if (!bodyPartOk)
					{
						AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, addedState.m_pos, float3(asfloat(findProc.m_bodyPart), asfloat(preceedingBodyPart), 0), 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), GetFlags0(addedState), addedState.m_flags1, dispatchId.x, addedState.m_id, ParticleFailReason_BodyPartMismatch);
					}
				}

				precedingBindPose = findProc.m_bindPosLs;
			}
			else
			{
				// todo: add fail event
				AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, addedState.m_pos, float3(asfloat(findProc.m_bodyPart), asfloat(preceedingBodyPart), 0), 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), GetFlags0(addedState), addedState.m_flags1, dispatchId.x, addedState.m_id, ParticleFailReason_BodyPartHeadCheck);
			}
		}

		if (__v_cmp_ne_u32(findProc.m_foundIndex, uint(-1)) == 0)
		{
			// no thread found it
			if (dispatchId.x == ReadFirstLane(dispatchId.x))
			{
				// one thread writes debug
				AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, precedingState.m_pos, float3(0, 0, 0), 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), GetFlags0(precedingState), precedingState.m_flags1, dispatchId.x, precedingState.m_id, ParticleFailReason_UnmappedMesh);
			}
		}
		ulong theadSuccessMask = __v_cmp_eq_u32(addedStateValid, 1);
		addedStateValid = theadSuccessMask != 0; // succeed if one thread succeeded

		if (!addedStateValid)
		{
			// just write sentinel over this state and bail

			pSrt->m_particleStates[stateIndex].m_id = 0;
			pSrt->m_particleStates[stateIndex].m_scale.x = 0;
			//pSrt->m_particleStates[stateIndex].m_scale.y = sentinelRibbonLength;
			numNewStates = iNew;
			break;
		}
		else
		{
			// this thread succeeded
			travelDist = ReadLane(travelDist, __s_ff1_i32_b64(theadSuccessMask));
			sentinelRibbonLength += travelDist;

			if (iNew == numNewStates)
			{
				// last state successful, update sentinel
				//pSrt->m_particleStates[stateIndex + 1].m_scale.y = sentinelRibbonLength;
			}
		}

	} // for num new states
	pSrt->m_particleStates[iStateOffset + 63].m_scale.y = sentinelRibbonLength;
	#endif


	#if ADD_STATES_LATER

	// try add new particles
	bool added = false;
	ParticleStateV0 addedState = ParticleStateV0(0);
	uint2 addedPartSSPos;
	{
		bool tooHorizontal;
		uint2 newPartSSPos;
		float3 newPartPosNdc;


		//float depthNdc = depthBufferToUse[sspos + uint2(0, 0)];
		//float depthLinear = GetLinearDepth(depthNdc, pSrt->m_depthParams);
		//float depthNdcX = depthBufferToUse[sspos + uint2(1, 0)];
		//float depthLinearX = GetLinearDepth(depthNdcX, pSrt->m_depthParams);
		//float depthNdcY = depthBufferToUse[sspos + uint2(0, 1)];
		//float depthLinearY = GetLinearDepth(depthNdcY, pSrt->m_depthParams);


		ParticleStateV0 state = pSrt->m_particleStates[iStateOffset + numLanes - 1]; // todo: can grab it with ReadFirstLane?  that migth actually be bad because it will force vgprs storing the whole state
		float4 newPosH = mul(float4(state.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 newPosNdc = newPosH.xyz / newPosH.w;

		uint2 sspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);


		CalculateNewPotentialPos(pSrt, state.m_pos, sspos, 0, 0, 0, newPartSSPos, newPartPosNdc, tooHorizontal, useOpaquePlusAlpha);

		addedPartSSPos = newPartSSPos;
		
		numNewStates = 0;

		float depthBufferError = GetLinearDepthError(newPosNdc.z, pSrt->m_depthParams);

		float allowedDepthError = pSrt->m_rootComputeVec2.x + depthBufferError * pSrt->m_rootComputeVec2.y * 2;


		if (newPartSSPos.x < pSrt->m_screenResolution.x && newPartSSPos.y < pSrt->m_screenResolution.y)
		{
			uint newPartStencil = stencilTextureToUse[newPartSSPos];
			float expectedDepthLinear = GetLinearDepth(newPartPosNdc.z, pSrt->m_depthParams);
			float depthLinear = GetLinearDepth(depthBufferToUse[newPartSSPos], pSrt->m_depthParams);
			bool newTrackingAlphaDepth = depthBufferToUse[newPartSSPos] != pSrt->m_pPassDataSrt->m_primaryDepthTexture[newPartSSPos];

			prevStencil = newPartStencil;

			if ((newPartStencil == prevStencil) && (abs(depthLinear - expectedDepthLinear) < allowedDepthError) && !tooHorizontal)
			{
				// create new particle at slightly different location
				ParticleStateV0 newState = ParticleStateV0(0);
				added = NewStickyParticleDepthReadjust(pSrt, dispatchId, groupThreadId.x, state, iStateOffset + numLanes, newPartSSPos, newTrackingAlphaDepth, newPartStencil, newState);
				addedState = newState;

				if (added)
				{
					// low bits store distance to next element
					uint oldVal = asuint(pSrt->m_particleStates[iStateOffset + numLanes - 1].m_data);
					oldVal |= f32tof16(length(state.m_pos - addedState.m_pos));
					pSrt->m_particleStates[iStateOffset + numLanes - 1].m_data = asfloat(oldVal);

					numLanes++;
					numNewStates++;

					AddDebugNewStickyParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, addedState.m_pos, state.m_pos, 0.01f, float4(1.0f, 1.0f, 0.0f, 1.0f), GetFlags0(addedState), addedState.m_flags1, dispatchId.x, addedState.m_id, addedState.m_birthTime);



					//uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), __get_vsharp(pSrt->m_particleFeedBackHeaderData), 0, 0);
					//if (prevIndex < 512)
					//{
					//	// can add new data
					//	pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, 0, groupThreadId.x, 0 /*ribbonId*/);
					//	pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(0, 0, 0, 0);
					//	pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(addedState.m_pos, 0);
					//	pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(0, 1, 1, 0.0f);
					//}
				}
			}
			else
			{
				// couldn't create the particle. Are we on a curve?
				// for now just set the state
				SetRenderState(state, kRenderStateBlue);
				pSrt->m_particleStates[iStateOffset + numLanes - 1].m_flags1 = state.m_flags1;
			}
		}
	}

	// in case we added some states, write sentinel
	pSrt->m_particleStates[iStateOffset + numLanes].m_id = 0;
	pSrt->m_particleStates[iStateOffset + numLanes].m_scale.x = 0;

	#endif

#if 0



	
	// since we know last frame's position, we could sample the color at that location
	float4 lastFrameColor = pSrt->m_pPassDataSrt->m_lastFramePrimaryFloat[lastFrameSSpos];

	
	if (groupThreadId.x == 0)
	{
		uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), __get_vsharp(pSrt->m_particleFeedBackHeaderData), 0, 0);
		if (prevIndex < 512)
		{
			// can add new data
			pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, 0, groupThreadId.x, 0 /*ribbonId*/);
			pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(numActiveStates - 1, iStateOffset, 0, 0);
			pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(precedingState.m_pos, 0);
			pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(lastFrameColor.xyz, 0.0f);
		}
	}

	

	

	float precedingBloodAmount = f16tof32(asuint(precedingState.m_data) >> 16);

	
	isAddingThread = 1;
	for (int iNew = 0; iNew < numNewStates; ++iNew)
	{
		addingMask = __v_cmp_eq_u32(isAddingThread, 1);

		addingThread = __s_ff1_i32_b64(addingMask);

		//precedingBloodAmount = ReadLane(precedingBloodAmount, asuint(addingThread));

		isAddingThread = 0;
		uint localIndex = numActiveStates - 1 + iNew + 1;
		uint stateIndex = iStateOffset + localIndex;
		ParticleStateV0 lastState = pSrt->m_particleStates[stateIndex]; // this is sbuffer read. but this state should not be in k-cache, we wrote it to L2, and now will read into k-cache



		if (groupThreadId.x == 0)
		{
			//uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), __get_vsharp(pSrt->m_particleFeedBackHeaderData), 0, 0);
			//if (prevIndex < 512)
			//{
			//	// can add new data
			//	pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, 0, groupThreadId.x, 0 /*ribbonId*/);
			//	pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(0, 0, 0, 0);
			//	pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(lastState.m_pos, 0);
			//	pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(0, 1, 1, 0.0f);
			//}
		}


		


		float4 newPosH = mul(float4(lastState.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 newPosNdc = newPosH.xyz / newPosH.w;
		//uint2 sspos = uint2((newPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (newPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);
		uint2 sspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);


		if (!(pSrt->m_features & COMPUTE_FEATURE_GENERATE_BLOOD_MAP_EVENTS))
		{
			FindProcessResults findRes = FindProcessMeshTriangleBaricentrics(pSrt, sspos, lastState.m_pos, groupThreadId.x);
			// only one thread potentially succeeded
			if (findRes.m_foundIndex != -1 && findRes.m_rtId != 255)
			{
			


				// check particle render target and pick up color
				int rtId = ReadFirstLane(findRes.m_rtId);
				float textureBloodMapValue = pSrt->m_pParticleRTs->m_textures[rtId].SampleLevel(pSrt->m_linearSampler, findRes.m_uv, 0).x;
				float bloodMapValue = precedingBloodAmount + textureBloodMapValue;

				bloodMapValue = saturate(bloodMapValue);
				// write it 

				uint oldVal = asuint(lastState.m_data);
				oldVal |= (f32tof16(bloodMapValue) << 16);
				pSrt->m_particleStates[stateIndex].m_data = asfloat(oldVal);

				//precedingBloodAmount = bloodMapValue;

				isAddingThread = 1;
				// write the value to lane0, since we know all threads are executing afterwards
				//WriteLane(precedingBloodAmount, precedingBloodAmount, 0);
				//__v_writelane_b32(precedingBloodAmount, iNew, 1.0/* ReadFirstLane(bloodMapValue)*/);
				//pSrt->m_particleStates[iStateOffset + first_bit].m_scale.x = bloodMapValue;

				ulong exec = __s_read_exec();

				//if (threadId == 0)
				{
					//uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), __get_vsharp(pSrt->m_particleFeedBackHeaderData), 0, 0);
					//if (prevIndex < 512)
					//{
					//	// can add new data
					//	pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, rtId, groupThreadId.x, 0 /*ribbonId*/);
					//	pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(localIndex, iStateOffset, numUpdatingStates, numActiveStates);
					//	pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(lastState.m_pos, numNewStates);
					//	pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(textureBloodMapValue, __s_bcnt1_i32_b64(exec) - 1/*should be 1!*/, 0.0f, 0.0f);
					//}
				}

			}
			else
			{

				//if (groupThreadId.x == 0 && (__s_read_exec() == 0xFFFFFFFFFFFFFFFFLLU))
				//{
				//	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), __get_vsharp(pSrt->m_particleFeedBackHeaderData), 0, 0);
				//	if (prevIndex < 512)
				//	{
				//		// can add new data
				//		pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, 0, groupThreadId.x, 0 /*ribbonId*/);
				//		pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(localIndex, iStateOffset, numUpdatingStates, numActiveStates);
				//		pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(lastState.m_pos, numNewStates);
				//		pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(0, 1, 1, 0.0f);
				//	}
				//}
			}
		}
	}

#endif
}


[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeRibbonCopyToOthers(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// go through groups and decide whether to copy trail parts from dynamic trails into stationary trails

	uint iDynamicTrail = groupId.x * 2;
	uint iStaticTrail = groupId.x * 2 + 1;

	// we read both trail states and then see if we can copy some stuff over.

	// first read the source states
	
	int iSrcStateOffset = iDynamicTrail * kNumPartsInRibbon;
	int srcParticleIndex = iSrcStateOffset + groupThreadId.x;

	int iDestStateOffset = iStaticTrail * kNumPartsInRibbon;
	int destParticleIndex = iDestStateOffset + groupThreadId.x;

	ParticleStateV0 srcState = pSrt->m_particleStates[srcParticleIndex];
	ParticleStateV0 destState = pSrt->m_particleStates[destParticleIndex];


	// use thisd opportunity to clean up the ribbons that are all in bad state

	bool isSrcBad = GetFlags0(srcState) >= kNumOfFramesToRecover;
	ulong badMask = __v_cmp_eq_u32(isSrcBad, true);


	bool isSrcSentinel = srcState.m_id == 0;
	bool isDestSentinel = destState.m_id == 0;

	ulong laneMask = __v_cmp_eq_u32(isSrcSentinel, true);
	int firstSrcSentinel = __s_ff1_i32_b64(laneMask); // should be guaranteed to never be -1

	if (firstSrcSentinel < 1)
		return;
	#if ADVANCE_UV_CAPTURE || 1 // disabling it for all ribbons to see if it fixes the crash.
	// todo: do we want to enable this? or alternatively just have twice as many blood drips possible
	else
		return;
	#endif

	// now check if all states preceding sentinel are bad

	ulong srcStateMask = __s_lshl_b64(1, firstSrcSentinel) - 1;

	if (__s_and_b64(srcStateMask, badMask) == srcStateMask)
	{
		// all bad. write first one as sentinel
		pSrt->m_particleStates[iSrcStateOffset].m_id = 0;
		pSrt->m_particleStates[iSrcStateOffset].m_scale.x = 0;
		// pSrt->m_particleStates[iDestStateOffset].m_id = 0;
		return;
	}

	bool haveUvs = false;
	#if ADVANCE_UV_CAPTURE
	haveUvs = GetHaveUvs(srcState);
	#endif

	ulong haveUvsLaneMask = __v_cmp_eq_u32(haveUvs, true);
	int firstStateWithUvs = __s_ff1_i32_b64(haveUvsLaneMask); // could be -1, could be beyond sentinel

	laneMask = __v_cmp_eq_u32(isDestSentinel, true);
	int firstDestSentinel = __s_ff1_i32_b64(laneMask); // should be guaranteed to never be -1

	int numExistingSrcStates = firstStateWithUvs >= 0 ? min(firstSrcSentinel, firstStateWithUvs) : firstSrcSentinel; // we don't want to move particles that still have not sent their uv
	int numExistingDestStates = firstDestSentinel;

	int spaceLeftInDest = 63 - numExistingDestStates;





	// now find which parts we can copy over. we use invisible render mode to determine teh end of sequence that we can split at

	uint srcRenderState = GetRenderState(srcState);

	bool isSequenceBreak = srcRenderState == kRenderStateInvisible;

	laneMask = __v_cmp_eq_u32(isSequenceBreak, true);

	int firstBreak = __s_ff1_i32_b64(laneMask); // should always be -1, since we only set kRenderStateInvisible on states that go into static copied states

	if (firstBreak >= numExistingSrcStates)
		return; // no breaks in trail

#define USE_V_2 1

	float rand0 =  GetRandom(pSrt->m_gameTicks, groupId.x, 1);

	int desired = 5 + rand0 * 20;

	#if USE_V_2
	if (numExistingSrcStates > desired + 2)
	{
		firstBreak = desired;
	}
	else
	{
		return;
	}
	#endif

	// can copy and remove [0..firstBreak] states
	int numWantToCopy = firstBreak + 2;

	if (numWantToCopy > spaceLeftInDest)
		return; // not enough space in destination

	// now perfrom the copy
	if (groupThreadId.x < numWantToCopy)
	{
		#if USE_V_2
		// we also make last one invisible (we will end at it)
		if (groupThreadId.x == numWantToCopy - 2)
		{
			SetRenderState(srcState, kRenderStateInvisible);
		}
		if (groupThreadId.x == numWantToCopy - 1)
		{
			SetRenderState(srcState, kRenderStateInvisible);
		}
		#endif

		// set lifetime of this chunk

		float rand1 = GetRandom(pSrt->m_gameTicks, groupId.x, 2);

		//srcState.m_lifeTime = pSrt->m_rootComputeVec1.z * 2 + pSrt->m_rootComputeVec1.z * 2 * rand1;

		//srcState.m_birthTime = pSrt->m_time;

		pSrt->m_particleStates[iDestStateOffset + numExistingDestStates + groupThreadId.x] = srcState;


		AddDebugCopyToOtherStickyParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, srcState.m_pos, iDestStateOffset + numExistingDestStates + groupThreadId.x, 0.01f, float4(1.0f, 1.0f, 0.0f, 1.0f), GetFlags0(srcState), srcState.m_flags1, dispatchId.x, srcState.m_id);



		// and mark the src states invalid, which will cause compression in next update
		#if USE_V_2
		// except in V2 we actually keep the last element, so that we don't have a gap
		if (groupThreadId.x < numWantToCopy - 2)
		#endif
		{
			MarkStateInvalid(pSrt->m_particleStates[srcParticleIndex]);
			pSrt->m_particleStates[srcParticleIndex].m_birthTime = -100000.0f; // will die immediately on next compress

			AddDebugKillStickyParticleEventAfterCopy(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, pSrt->m_particleStates[srcParticleIndex].m_pos, (uint)(srcParticleIndex), 0.01f, float4(1.0f, 1.0f, 0.0f, 1.0f), GetFlags0(pSrt->m_particleStates[srcParticleIndex]), pSrt->m_particleStates[srcParticleIndex].m_flags1, dispatchId.x, pSrt->m_particleStates[srcParticleIndex].m_id);

		}
	}

	// and finally write new sentinel for the destination
	pSrt->m_particleStates[iDestStateOffset + numExistingDestStates + numWantToCopy].m_id = 0;
	pSrt->m_particleStates[iDestStateOffset + numExistingDestStates + numWantToCopy].m_scale.x = 0;
}

bool IsBadRibbonState(ParticleStateV0 state)
{
	bool badState = GetFlags0(state) != 0;
	uint renderState = GetRenderState(state);

	//badState = badState || (renderState == kRenderStateInvisible);

	//badState = false;
	return badState;
}


[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeRibbonGenerateRenderables(const uint2 dispatchId : SV_DispatchThreadID,
	const int3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	//__s_dcache_inv();
	//__buffer_wbinvl1();
	// grab my particle
	bool isDynamicTrail = (groupId.x % 2) == 0;

	int iStateOffset = groupId.x * kNumPartsInRibbon;

	int particleIndex = iStateOffset + groupThreadId.x;

	ParticleStateV0 state = pSrt->m_particleStates[particleIndex];
	ParticleStateV0 nextState = pSrt->m_particleStates[particleIndex + 1];

	bool isSentinel = state.m_id == 0;

	// We create a mask which is 1 for all lanes that contain this value
	ulong sentinel_mask = __v_cmp_eq_u32(isSentinel, true);

	// we need to find the earliest sentinel
	int first_bit = __s_ff1_i32_b64(sentinel_mask);

	bool wantWriteData = true;
	if (first_bit == -1)
	{
		wantWriteData = false;
		// return;
	}

	int numStates = first_bit; // num valid states

	if (numStates < 2) // would mean sentinel is at 2 and we only have 2 particles, that's not enough, we need at least 3 particles to produce one quad
	{
		wantWriteData = false;
		//return; // empty
	}

	int numRenderIndicesToWrite = 0; // how many indices whole group wants to write. scalar value

	WriteLane(numRenderIndicesToWrite, numRenderIndicesToWrite, 0);

	int renderIndexToWrite = -1; // index that this thread wants to write
	//_SCE_BREAK();


	int numQuads = int(numStates) - 1; // this is how many quads we will end up having

	float uvStep = 1.0f / numQuads;
	float uvOffset = uvStep * groupThreadId.x;
	int particleBrithIndex = int(state.m_id & 0x000000FF);

	float globalV = particleBrithIndex / 255.0f;
	
	bool badState = IsBadRibbonState(state); // this test has to EXACTLY match one below for next bad state

	uint renderState = GetRenderState(state);

	uint nextRenderState = GetRenderState(nextState);

	bool kUseGlobalVOffset = true;


	// note we don't do anything with last particle at all, it just is used for direction for last quad
	// we also completely skip bad state particles
	// we will also not draw particles that have next one as bad
	if ((groupThreadId.x < numStates) && !badState && wantWriteData)
	{
		// we are before sentinel

		uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
		uint gdsOffsetOld = pSrt->m_gdsOffsetCounterOld;

		uint size, stride;
		pSrt->m_particleStates.GetDimensions(size, stride);

		

		// todo: should check this by reading preceding lane?
		bool nextIsBad = IsBadRibbonState(nextState); // IMPORTANT: this test has to match the test of isBade above entering this if statement, because we have to guarantee next thread enters this if statement
													  // it at least has to be as strict. so we can still set it to true, but we can't have it false while previous thread evaluated it to true

													  //nextIsBad = nextIsBad || (distToNext > allowedDistance) || !depthTestOk; // we could also store this as result in state flags

													  // I myself am valid but I will not render because next one is bad or maybe ity is not bad we just have artificial break
		bool lastInSequence = (groupThreadId.x >= numQuads || nextIsBad || (renderState == kRenderStateInvisible));


		ulong lastInSequenceLaneMask = __v_cmp_eq_u32(lastInSequence, true);

		bool isDummyParticle = groupThreadId.x > 0 ? (__v_lshl_b64(1, groupThreadId.x - 1) & lastInSequenceLaneMask) != 0 : 0; // dummy particle means preceding state is also last in sequence. so we can use this just for orienting preceding particle
		
		isDummyParticle = lastInSequence && isDummyParticle;

		int destinationInstanceIndex = isDummyParticle ? -1 : NdAtomicIncrement(gdsOffsetOld); // states are tracked by old counter since we need a different counter for states vs rendrables

		ulong isDummyLaneMask = __v_cmp_eq_u32(isDummyParticle, true);
		bool nextIsDummy = groupThreadId.x < 63 ? (__v_lshl_b64(1, groupThreadId.x + 1) & isDummyLaneMask) != 0 : 0; // it will be false if next didnt enter the if statement (bad particle) or if next one is not an invisible particle

		// we want to find how many contiguous bits we have in this sequence

		//ulong laneMask = __s_read_exec();
		ulong laneMask = __v_cmp_eq_u32(lastInSequence, false);


		// we will shift right up to current location and look for first 0. that will be the end of current sequence
		ulong maskShr = __v_lshr_b64(laneMask, groupThreadId.x+1);

		// now find first 0
		uint maskShrLo = uint(maskShr);
		uint maskShrHi = uint(maskShr >> 32);

		// flip to find first 1
		maskShrLo = ~maskShrLo;
		maskShrHi = ~maskShrHi;

		int firstSetLo = __v_ffbl_b32(maskShrLo);
		int bitsAhead = 0; // zero means we are last active bit in this sequence

		if (firstSetLo == -1)
		{
			// current sequence continues past lo bit
			bitsAhead += 32;

			int firstSetHi = __v_ffbl_b32(maskShrHi);

			// we should be guaranteed to never get -1, since this sequence has to end, because we always have sentinel
			bitsAhead += firstSetHi;
			bitsAhead += firstSetHi;
		}
		else
		{
			bitsAhead = firstSetLo;// zero means we are last active bit in this sequence, otewrwise stores how many more bits we have
		}

		// now calculate bits before
		// shift left and find first 0 from left to right (most significant)

		ulong maskShl = groupThreadId.x ? __v_lshl_b64(laneMask, 64 - groupThreadId.x) : 0;

		uint maskShlLo = uint(maskShl);
		uint maskShlHi = uint(maskShl >> 32);

		// flip to find first 1
		maskShlLo = ~maskShlLo;
		maskShlHi = ~maskShlHi;

		int firstSetHi = __v_ffbh_u32(maskShlHi);

		int bitsBefore = 0;

		if (firstSetHi == -1)
		{
			// the sequence continues into the next lo dword

			bitsBefore += 32;

			firstSetLo = __v_ffbh_u32(maskShlLo);

			bitsBefore += firstSetLo;
		}
		else
		{
			bitsBefore = firstSetHi;
		}

		int bitsTotal =  bitsBefore +  1 + bitsAhead;

		int numQuadsInSequence = bitsTotal; // not (bitsTotal - 1); because we already excluded last particle
		uvStep = 1.0f / numQuadsInSequence;
		uvOffset = uvStep * bitsBefore;


		if (lastInSequence)
		{
			uvOffset = 1.0;
		}

		if (kUseGlobalVOffset)
		{
			uvOffset = globalV;
		}

		//uvOffset = 1.0/64 * bitsBefore;


		//uvStep = 1.0f / (numQuads);
		//uvOffset = uvStep * groupThreadId.x;


		// we are just going to copy paste the particles
		// this cant happen with ribbons
		///if (destinationInstanceIndex >= size) // we can't do this test without thinking more, since it will produce bad quads. || bitsTotal < 2)
		///{
		///	// decrement back
		///	NdAtomicDecrement(gdsOffsetNew);
		///	// can't add new particles
		///	return;
		///}


		


		float4x4	g_identity = { { 1, 0, 0, 0 },{ 0, 1, 0, 0 },{ 0, 0, 1, 0 },{ 0, 0, 0, 1 } };
		float3 kUnitYAxis = { 0.0f, 1.0f, 0.0f };


		float3 pos = state.m_pos;

		ParticleInstance inst = ParticleInstance(0);

		inst.world = g_identity;			// The object-to-world matrix
		inst.color = float4(1, 1, 1, 1);			// The particle's color

		if (GetFlags0(state) > 10)
		{
			inst.color = float4(1, 0, 0, 1);
		}
		else if (GetFlags0(state) > 0)
		{
			inst.color = float4(1, 1, 0, 1);
		}
		
		// kRenderStateBlue is when we can't create new particle
		if (renderState == kRenderStateBlue || nextRenderState == kRenderStateBlue)
		{
			inst.color = float4(0, 0, 1, 1);
		}


		inst.texcoord = float4(1, 1, 0, 0);		// Texture coordinate scale and bias (uv = uv0 * texcoord.xy + texcoord.zw)
												//inst.texcoord.x = 0.5f;
		inst.texcoord.w = uvOffset;


		//inst.color = float4(0, isDynamicTrail ? uvOffset * uvOffset : 0, !isDynamicTrail ? uvOffset * uvOffset : 0, 1);
		//inst.color = float4(0, isDynamicTrail ? 1 : 0, !isDynamicTrail ? 1 : 0, 1);


		inst.userh = float4(0, 0, 0, 0);			// User attributes (used to be half data type)
		inst.userf = float4(0, 0, 0, 0);			// User attributes
		inst.partvars = float4(0, 0, 0, 0);		// Contains age, normalized age, ribbon distance, and frame number
		inst.invscale = float4(1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f);		// Reciprocal of the particle's half-scale vector

		float age = (pSrt->m_time - state.m_birthTime);

		inst.userh.x = age * kFps;
		inst.userh.y = numQuadsInSequence;

		if (kUseGlobalVOffset)
		{
			inst.userh.y = 128.0;
		}

		inst.userh.z = (groupId.x / 2) / 16.0; // same value for both static and dynamic string


		float3 toCam = normalize(pSrt->m_cameraPosWs.xyz - pos);

		float distToNext = length(nextState.m_pos - pos);


		

		// based on velocity
		uint m_data = asuint(state.m_data);
		float expectedDistance = f16tof32(m_data); //   pSrt->m_rootComputeVec1.y * (pSrt->m_delta > 0.0001 ? pSrt->m_delta : 0.0333f); // assuming framerate is pretty stable and is close to 30 fps for when we are paused
		float bloodAmount = f16tof32(m_data >> 16);

		float allowedDistance = expectedDistance * 2 + 0.02; // give a little slack

		float allowedDepthDifference = allowedDistance / 3.0; // we only allow fraction of speed to differ in depth

		float distToCamera0 = length(pos - pSrt->m_altWorldOrigin.xyz);
		float distToCamera1 = length(nextState.m_pos - pSrt->m_altWorldOrigin.xyz);

		float depthTestOk = abs(distToCamera0 - distToCamera1) < allowedDepthDifference;


		float3 toNextParticle = normalize(nextState.m_pos - pos);

		if (lastInSequence && !nextIsDummy)
		{
			// predict by looking back
			toNextParticle = normalize(pos - pSrt->m_particleStates[particleIndex-1].m_pos);
			distToNext = length(pos - pSrt->m_particleStates[particleIndex - 1].m_pos);
		}

		//inst.world = TransformFromLookAt(toCam, state.m_rotation.xyz, pos, true);

		float4 newPosH = mul(float4(state.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 newPosNdc = newPosH.xyz / newPosH.w;

		uint2 sspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);


		float3 depthNormal = CalculateDepthNormal(pSrt, sspos, /*useOpaquePlusAlpha*/ true);

		if (GetStartedAtHead(state))
		{
			// we have a packed normal
			uint uintNorm = GetUintNorm(state);
			float3 unpackedNormal = UnpackUintNorm(uintNorm);
			//if (dot(unpackedNormal, depthNormal) > 0)
			//	inst.color = float4(0, 1, 0, 1);
			//else
			//	inst.color = float4(1, 0, 0, 1);
			depthNormal = unpackedNormal;
		}

		inst.world = TransformFromLookAtRibbon(depthNormal /*state.m_rotation.xyz*/, toNextParticle, pos, true); // this is totally broken because of tracking bind pose position in m_rotation

		// for crawlers: use this for aligning the sprite with velocity direction
		//inst.world = TransformFromLookAtYFw(state.m_scale.xyz, state.m_rotation.xyz, pos, true);
		 
		float3 scale = float3(pSrt->m_rootComputeVec0.xyz) * 0.5;

		inst.userf.x = bloodAmount * 0.5f;

		if (bloodAmount > 0.01f)
		{
		//	scale.x *= 10;
		}

		if (lastInSequence || (bitsBefore == 0))
		{
			//scale.x = 0.01f;
		}
		//scale.x = max(scale.x * 0.5, scale.x + bitsBefore * -0.002);

		inst.world[0].xyz = inst.world[0].xyz * scale.x;
		inst.world[1].xyz = inst.world[1].xyz * scale.y;
		inst.world[2].xyz = inst.world[2].xyz * scale.z;

		inst.invscale = float4(1.0f / (scale.x * 0.5), 1.0f / (scale.y * 0.5), 1.0f / (scale.z * 0.5), 1.0f / 1);

		float3 renderOffset = pSrt->m_renderOffset.xyz;

		//renderOffset.x = -0.25f;
		//renderOffset.z = 0.5f;
		// in this case y axis of particle is up along the normal

		renderOffset = float3(0, 0.2, 0); // but offsetting along the normal doesnt seem to produce good results, try moving it towards a camera a little

		renderOffset = float3(0, 0, 0);

		float4 posOffest = mul(float4(renderOffset, 1), inst.world);

		// modify position based on camera
		inst.world[3].xyz = posOffest.xyz - pSrt->m_altWorldOrigin.xyz + toCam * 0.005;

		//partMat.SetRow(3, MulPointMatrix(offset.GetVec4(), partMat));

		inst.prevWorld = inst.world;		// Last frame's object-to-world matrix
		
		if (lastInSequence)
		{
			// we know next particle is bad or doesnt exist. this means distance to next was predicted by using distance from previous particle to current
			inst.color = float4(1, 0, 1, 1); // magenta
		
		}
		else if (distToNext > allowedDistance)
		{
			inst.color = float4(0, 1, 1, 1); // cyan

			lastInSequence = true;
		}
		
		float blendTime = RIBBON_ROOT_VAR_LIFETIME * 0.7; // last 70% of lifetime is blending out
		
		float lifeTime = f16tof32(asuint(state.m_lifeTime));

		//if (!isDynamicTrail)
		{
			inst.color.a = saturate((lifeTime - age) / blendTime);
			
			inst.userh.w = saturate(((age) / lifeTime) * (1.0 / 0.2)); // 0 -> 1 over  0.5 lifetime

		}

		inst.userf.y = saturate(age / lifeTime); // 0 -> 1 over life

		bool skinOrHair = state.m_flags1 & (kMaterialMaskSkin | kMaterialMaskHair);
		bool isHair = state.m_flags1 & (kMaterialMaskHair);

		if (!skinOrHair)
		{
			//inst.color.a *= 0.5f;
		}

		if (isHair)
		{
			inst.color.a *= 0.0f;
		}


		//particleBrithIndex % 4

		if (lastInSequence || (bitsBefore == 0))
		{
			inst.color.a = 0.0f;
		}

		//inst.color = float4(1, 1, 1, 1.0f);


		//inst.color.a = 0;
		if (!isDummyParticle)
		{
			pSrt->m_particleInstances[destinationInstanceIndex] = inst;
		}


		//bool nextIsBad = IsBadRibbonState(nextState); // IMPORTANT: this test has to match the test of isBade above entering this if statement, because we have to guarantee next thread enters this if statement
													  // it at least has to be as strict. so we can still set it to true, but we can't have it false while previous thread evaluated it to true

		////nextIsBad = nextIsBad || (distToNext > allowedDistance) || !depthTestOk; // we could also store this as result in state flags


		//if (groupThreadId.x < numQuads && !nextIsBad && (renderState != kRenderStateInvisible))
		if (!lastInSequence && groupThreadId.x < 63 && pSrt->m_rootComputeVec2.z >= 0) // CRASH_DEBUG_TODO: remove check for 63
		{
			numRenderIndicesToWrite = __s_bcnt1_i32_b64(__s_read_exec()); // how many indices whole group wants to write. scalar value, but can diverge since not all threads are here

			
			// note we don't add renerable index for last element because it only server for lookup not rendering
			int renderIndex = NdAtomicIncrement(gdsOffsetNew); // and this counter is the one used for dispatching rendering
			#if RIBBONS_WRITE_TEMP_INDICES
				pSrt->m_particleTempIndices[renderIndex] = destinationInstanceIndex;
				// we will use this data to sort into real renderable buffer
				pSrt->m_particleTempSortData[groupId.x].m_offset = ReadFirstLane(renderIndex);
				//pSrt->m_particleTempSortData[groupId.x].m_numInstances = numRenderIndicesToWrite;
			#else
				pSrt->m_particleIndices[renderIndex] = destinationInstanceIndex;
			#endif
			renderIndexToWrite = destinationInstanceIndex; // index that this thread wants to write
		}
	}

	// back to 64 threads here
	ulong succeessmask = __v_cmp_ne_u32(renderIndexToWrite, -1);
	uint goodThread = __s_ff1_i32_b64(succeessmask);

	// make sure each group writes a 0 if not rendering
	#if RIBBONS_WRITE_TEMP_INDICES
		pSrt->m_particleTempSortData[groupId.x].m_numInstances = ReadLane(numRenderIndicesToWrite, goodThread);
	#endif
}



[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeRibbonSortRenderables(const uint2 dispatchId : SV_DispatchThreadID,
	const int3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	int numRenderIndicesBefore = 0;
	for (int i = 0; i < groupId.x; ++i)
	{
		numRenderIndicesBefore += pSrt->m_particleTempSortData[i].m_numInstances;
	}

	int numIndices = pSrt->m_particleTempSortData[groupId.x].m_numInstances;
	
	if (groupThreadId.x < numIndices)
	{
		// copy the indices over
		pSrt->m_particleIndices[numRenderIndicesBefore + groupThreadId.x] = pSrt->m_particleTempIndices[pSrt->m_particleTempSortData[groupId.x].m_offset + groupThreadId.x];
	}
}

float4 InterpVec4(float4 inst0, float4 inst1, float4 inst2, float3 tvec)
{
	float x = dot(tvec, float3(inst0.x, inst1.x, inst2.x));
	float y = dot(tvec, float3(inst0.y, inst1.y, inst2.y));
	float z = dot(tvec, float3(inst0.z, inst1.z, inst2.z));
	float w = dot(tvec, float3(inst0.w, inst1.w, inst2.w));

	return float4(x, y, z, w);
}

float3 InterpVec3(float3 inst0, float3 inst1, float3 inst2, float3 tvec)
{
	float x = dot(tvec, float3(inst0.x, inst1.x, inst2.x));
	float y = dot(tvec, float3(inst0.y, inst1.y, inst2.y));
	float z = dot(tvec, float3(inst0.z, inst1.z, inst2.z));

	return float3(x, y, z);
}

float2 InterpVec2(float2 inst0, float2 inst1, float2 inst2, float3 tvec)
{
	float x = dot(tvec, float3(inst0.x, inst1.x, inst2.x));
	float y = dot(tvec, float3(inst0.y, inst1.y, inst2.y));

	return float2(x, y);
}

float1 InterpVec1(float inst0, float inst1, float inst2, float3 tvec)
{
	float x = dot(tvec, float3(inst0.x, inst1.x, inst2.x));
	
	return x;
}


ParticleInstance InterpolateParticleInstance(ParticleInstance inst0, ParticleInstance inst1, ParticleInstance inst2, float3 tvec, out float3 scale)
{
	ParticleInstance newInst = inst1;

	newInst.world[3].xyz = InterpVec3(inst0.world[3], inst1.world[3], inst2.world[3], tvec);
	newInst.prevWorld[3].xyz = InterpVec3(inst0.prevWorld[3], inst1.prevWorld[3], inst2.prevWorld[3], tvec);

	newInst.texcoord = InterpVec4(inst0.texcoord, inst1.texcoord, inst2.texcoord, tvec);
	
	newInst.userh = InterpVec4(inst0.userh, inst1.userh, inst2.userh, tvec);
	newInst.userf = InterpVec4(inst0.userf, inst1.userf, inst2.userf, tvec);

	float3 scale0 = float3(1, 1, 1) / inst0.invscale.xyz * 2;
	float3 scale1 = float3(1, 1, 1) / inst1.invscale.xyz * 2;
	float3 scale2 = float3(1, 1, 1) / inst2.invscale.xyz * 2;

	scale = InterpVec3(scale0, scale1, scale2, tvec);

	return newInst;
}


float4x4 TransformFromLookAtRegularRibbon(float3 fwdDir, float3 upDir, float3 pos, bool preserveFwd)
{
	float3 fVec = fwdDir;
	float3 uVec = upDir;

	float3 kUnitXAxis = { 1.0f, 0.0f, 0.0f };
	float3 kUnitYAxis = { 0.0f, 1.0f, 0.0f };

	bool parallel = abs(dot(fVec, kUnitXAxis)) >= 0.999f;
	float3 xAxis = !parallel ? kUnitXAxis : kUnitYAxis;

	float3 rVec = normalize(cross(uVec, fVec));


	uVec = preserveFwd ? (cross(fVec, rVec)) : uVec;
	fVec = preserveFwd ? fVec : (cross(rVec, uVec));

	//uVec = normalize(cross(fVec, rVec));

	float4x4 mat;
	mat[0].xyzw = float4(rVec, 0);
	mat[2].xyzw = float4(uVec, 0);
	mat[1].xyzw = float4(fVec, 0);
	mat[3].xyzw = float4(pos, 1);

	return mat;
}


[NUM_THREADS(64, 1, 1)]
void CS_ParticleTesselateRibbon(const uint2 dispatchId : SV_DispatchThreadID,
	const int3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{

	//pSrt->m_particleInstances
	//pSrt->m_particleIndices

	int particleIndex = dispatchId.x;

	uint numRenderedParticles;
	pSrt->m_particleIndicesOrig.GetDimensions(numRenderedParticles);

	uint numOriginalParticleStates;
	uint origStride;
	pSrt->m_particleInstancesOrig.GetDimensions(numOriginalParticleStates, origStride);

	uint numDestinationParticleStates;
	pSrt->m_particleInstances.GetDimensions(numDestinationParticleStates, origStride);

	int numComputeParticlesFactor = numDestinationParticleStates / numOriginalParticleStates;



	if (dispatchId.x >= numOriginalParticleStates)
		return;

	uint prevInstIndex = dispatchId.x > 0 ? dispatchId.x - 1 : 0;
	uint nextInstIndex = dispatchId.x < numOriginalParticleStates - 1 ? dispatchId.x + 1 : dispatchId.x;
	uint nextNextInstIndex = dispatchId.x < numOriginalParticleStates - 2 ? dispatchId.x + 2 : dispatchId.x;

	ParticleInstance prevInst = pSrt->m_particleInstancesOrig[prevInstIndex];
	ParticleInstance curInst = pSrt->m_particleInstancesOrig[dispatchId.x];
	ParticleInstance nextInst = pSrt->m_particleInstancesOrig[nextInstIndex];
	ParticleInstance nextNextInst = pSrt->m_particleInstancesOrig[nextNextInstIndex];

	

	ParticleInstance nextInterp;
	float3 nextScale;
	{
		float t = 0.5;

		float3 tvec = float3(1, t, t*t);
		float tmx = dot(tvec, 0.5f * float3(1.0f, -2.0f, 1.0f));
		float tmy = dot(tvec, 0.5f * float3(1.0f, 2.0f, -2.0f));
		float tmz = dot(tvec, 0.5f * float3(0.0f, 0.0f, 1.0f));

		tvec = float3(tmx, tmy, tmz);

		
		nextInterp = InterpolateParticleInstance(curInst, nextInst, nextNextInst, tvec, nextScale);
	}


	// for each particle we will generate a path coonecting midway from preciding to midway to next
	ParticleInstance newInst = curInst;

	for (int i = numComputeParticlesFactor/2 - 1; i >= 0; --i)
	{
		float t = i / float(numComputeParticlesFactor);


		float3 tvec = float3(1, t, t*t);
		float tmx = dot(tvec, 0.5f * float3(1.0f, -2.0f, 1.0f));
		float tmy = dot(tvec, 0.5f * float3(1.0f, 2.0f, -2.0f));
		float tmz = dot(tvec, 0.5f * float3(0.0f, 0.0f, 1.0f));

		tvec = float3(tmx, tmy, tmz);

		float3 scale;

		newInst = InterpolateParticleInstance(curInst, nextInst, nextNextInst, tvec, scale);

		float3 camPos = float3(0, 0, 0);
		float3 camDir = pSrt->m_cameraDirWs;
		float3 toNext = normalize(nextInterp.world[3].xyz - newInst.world[3].xyz);
		float3 toCam = normalize(camPos - newInst.world[3].xyz);

		float3 toNextPrev = normalize(nextInterp.prevWorld[3].xyz - newInst.prevWorld[3].xyz);
		float3 toCamPrev = normalize(camPos - newInst.prevWorld[3].xyz);

		newInst.world = TransformFromLookAtRegularRibbon(toNext, toCam, newInst.world[3].xyz, true);
		newInst.prevWorld = TransformFromLookAtRegularRibbon(toNextPrev, toCamPrev, newInst.prevWorld[3].xyz, true);

		newInst.world[0].xyz = newInst.world[0].xyz * scale.x;
		newInst.world[1].xyz = newInst.world[1].xyz * scale.y;
		newInst.world[2].xyz = newInst.world[2].xyz * scale.z;

		newInst.prevWorld[0].xyz = newInst.prevWorld[0].xyz * scale.x;
		newInst.prevWorld[1].xyz = newInst.prevWorld[1].xyz * scale.y;
		newInst.prevWorld[2].xyz = newInst.prevWorld[2].xyz * scale.z;


		pSrt->m_particleInstances[dispatchId.x * numComputeParticlesFactor + numComputeParticlesFactor / 2 + i] = newInst;


		nextInterp = newInst;
	}

	for (int i = numComputeParticlesFactor/2 - 1; i >= 0; --i)
	{
		float t = 0.5f + i / float(numComputeParticlesFactor);
	
	
		float3 tvec = float3(1, t, t*t);
		float tmx = dot(tvec, 0.5f * float3(1.0f, -2.0f, 1.0f));
		float tmy = dot(tvec, 0.5f * float3(1.0f, 2.0f, -2.0f));
		float tmz = dot(tvec, 0.5f * float3(0.0f, 0.0f, 1.0f));
	
		tvec = float3(tmx, tmy, tmz);
	
		float3 scale;

		newInst = InterpolateParticleInstance(prevInst, curInst, nextInst, tvec, scale);
	
		float3 camPos = float3(0, 0, 0);
		float3 camDir = pSrt->m_cameraDirWs;
		float3 toNext = normalize(nextInterp.world[3].xyz - newInst.world[3].xyz);
		float3 toCam = normalize(camPos - newInst.world[3].xyz);

		float3 toNextPrev = normalize(nextInterp.prevWorld[3].xyz - newInst.prevWorld[3].xyz);
		float3 toCamPrev = normalize(camPos - newInst.prevWorld[3].xyz);

		newInst.world = TransformFromLookAtRegularRibbon(toNext, toCam, newInst.world[3].xyz, true);
		newInst.prevWorld = TransformFromLookAtRegularRibbon(toNextPrev, toCamPrev, newInst.prevWorld[3].xyz, true);

		newInst.world[0].xyz = newInst.world[0].xyz * scale.x;
		newInst.world[1].xyz = newInst.world[1].xyz * scale.y;
		newInst.world[2].xyz = newInst.world[2].xyz * scale.z;
		
		newInst.prevWorld[0].xyz = newInst.prevWorld[0].xyz * scale.x;
		newInst.prevWorld[1].xyz = newInst.prevWorld[1].xyz * scale.y;
		newInst.prevWorld[2].xyz = newInst.prevWorld[2].xyz * scale.z;


		pSrt->m_particleInstances[dispatchId.x * numComputeParticlesFactor + i] = newInst;
	

		nextInterp = newInst;
	}



	//pSrt->m_particleInstances[dispatchId.x] = pSrt->m_particleInstancesOrig[dispatchId.x];


	if (dispatchId.x >= numRenderedParticles)
		return;

	for (int i = 0; i < numComputeParticlesFactor; ++i)
	{
		pSrt->m_particleIndices[dispatchId.x * numComputeParticlesFactor + i] = pSrt->m_particleIndicesOrig[dispatchId.x] * numComputeParticlesFactor + (numComputeParticlesFactor - 1 - i);
	}

	//pSrt->m_particleIndices[dispatchId.x] = pSrt->m_particleIndicesOrig[dispatchId.x];

#if 0

	int particleIndex = dispatchId.x;

	uint origSize;
	pSrt->m_particleIndicesOrig.GetDimensions(origSize);

	if (particleIndex >= origSize)
		return;

	uint rootParticleIndex = (particleIndex / 4) * 4;

	uint origRootParticleIndex = pSrt->m_particleIndicesOrig[rootParticleIndex];
	ParticleInstance rootParticle = pSrt->m_destParticleInstancesOrig[origRootParticleIndex];



	uint nextRootParticleIndex = rootParticleIndex + 4;

	uint nextOrigParticleIndex = origRootParticleIndex + 4; // pSrt->m_particleIndicesOrig[nextRootParticleIndex];
	ParticleInstance nextRootParticle = pSrt->m_destParticleInstancesOrig[nextOrigParticleIndex];

	uint indexInTesselatedSegment = particleIndex % 4;

	if (indexInTesselatedSegment == 0)
	{
		return;
	}

	float interpolationFactor = indexInTesselatedSegment / 4.0;



	ParticleInstance modifiedInstance = rootParticle;


	float3 rootParticleOffset = (rootParticleIndex / 4) % 2 ? float3(0, 0.1, 0) : float3(0, 0, 0);
	
	rootParticle.world[3].xyz += rootParticleOffset;

	float3 nextRootParticleOffset = (nextRootParticleIndex / 4) % 2 ? float3(0, 0.1, 0) : float3(0, 0, 0);

	nextRootParticle.world[3].xyz += nextRootParticleOffset;

	float3 startPos = rootParticle.world[3].xyz;

	float3 endPos = nextRootParticle.world[3].xyz;

	float3 interpPos = lerp(startPos, endPos, interpolationFactor);
	
	float3 offset = float3(0, 0, 0); // float3(0, 0.1, 0) * (1.0 - abs(interpolationFactor - 0.5f) * 2);
	interpPos += offset;

	modifiedInstance.world[3].xyz = interpPos;

	modifiedInstance.prevWorld[3].xyz = lerp(rootParticle.prevWorld[3].xyz, nextRootParticle.prevWorld[3].xyz, interpolationFactor);;
	modifiedInstance.prevWorld[3].xyz += offset;	

	modifiedInstance.texcoord = lerp(rootParticle.texcoord, nextRootParticle.texcoord, interpolationFactor);;
	modifiedInstance.userh = lerp(rootParticle.userh, nextRootParticle.userh, interpolationFactor);;
	modifiedInstance.userf = lerp(rootParticle.userf, nextRootParticle.userf, interpolationFactor);;

	//float4   partvars;		// Contains age, normalized age, ribbon distance, and frame number

	uint origParticleIndex = pSrt->m_particleIndicesOrig[particleIndex];

	pSrt->m_destParticleInstancesOrig[origParticleIndex] = modifiedInstance;
#endif
}


[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeRibbonUpdateCompressBloodPickup(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{

	if (groupId.x == 0)
	{
		//_SCE_BREAK();
	}


	// each group works on one ribbon

	// we only spawn new particle when the whole trail is dead, i.e. first element in the list is the sentinel

	int iStateOffset = groupId.x * kNumPartsInRibbon;

	int particleIndex = iStateOffset + groupThreadId.x;

	ParticleStateV0 state = pSrt->m_particleStatesOld[particleIndex];

	bool isDynamicTrail = (groupId.x % 2) == 0;

	bool isSentinel = state.m_id == 0;

	// We create a mask which is 1 for all lanes that contain this value
	ulong sentinel_mask = __v_cmp_eq_u32(isSentinel, true);

	// we need to find the earliest sentinel
	uint first_bit = __s_ff1_i32_b64(sentinel_mask);
	
	uint numUpdatingStates = first_bit; // valid states

	bool isOutoFBounds = groupThreadId.x >= first_bit;

	uint startNumLanes = __s_bcnt1_i32_b64(__s_read_exec());

	float sentinelRibbonLength = pSrt->m_particleStatesOld[iStateOffset + 63].m_scale.y; // stores ribbon length

	//bool alphaDepthOnly = state.m_flags1 & kFlagAlphaDepthOnly;

	bool trackingAlphaDepth = (state.m_flags1 & kMaterialMaskAlphaDepth) != 0;

	float age = (pSrt->m_time - state.m_birthTime);

	int numNewStates = 0;
	int endNumLanes = 0;
	int isActiveThread = 0;
	int isAddingThread = 0;
	uint numLanes = 0;
	int2 lastFrameSSpos = int2(0, 0);

	bool useOpaquePlusAlpha = true;

	//pSrt->m_pPassDataSrt->m_primaryStencil
	Texture2D<uint> stencilTextureToUse = useOpaquePlusAlpha ? pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil : pSrt->m_pPassDataSrt->m_primaryStencil;
	//m_primaryDepthTexture
	Texture2D<float> depthBufferToUse = useOpaquePlusAlpha ? pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture : pSrt->m_pPassDataSrt->m_primaryDepthTexture;

	float3 preUpdatePos = state.m_pos;

	float lifeTime = f16tof32(asuint(state.m_lifeTime));

	bool haveUvs = false;

	#if ADVANCE_UV_CAPTURE
	haveUvs = GetHaveUvs(state);
	#endif


	if (pSrt->m_delta < 0.00001)
	{
		// paused frame just copy over the data

		// write the state out to new buffer
		// here we actuallyw rite all 64 lanes, if the sentinel was there before, it will be there now, along with garbage in post sentinel states
		pSrt->m_particleStates[particleIndex] = state;

		// always write sentinel
		//ulong laneMask = __s_read_exec();
		//uint numLanes = __s_bcnt1_i32_b64(laneMask);
		//pSrt->m_particleStates[iStateOffset + numLanes].m_id = 0;

		return;
	}
	
	uint needsUpdate = 1;
	uint positionComputed = 0;
	if ((age > lifeTime && !haveUvs) || isOutoFBounds)
	{
		// too old. discard
		needsUpdate = 0;
		numLanes = __s_bcnt1_i32_b64(__s_read_exec());

		if (!isOutoFBounds)
		{
			AddDebugDieStickyParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, state.m_pos, state.m_pos, 0.01f, float4(0.0f, 1.0f, 1.0f, 1.0f), GetFlags0(state), state.m_flags1, dispatchId.x, state.m_id, state.m_birthTime);
		}

		if (startNumLanes == numLanes)
		{
			// if all thread are here, we need to write the sentinel, because no other thread will
			pSrt->m_particleStates[iStateOffset].m_id = 0;
			pSrt->m_particleStates[iStateOffset].m_scale.x = 0;

			return;
		}
	}

	// for those who need update, we compute position based on stored barycentric coords
	
	FindProcessResults findProcOrig = 0;
	
	uint3 indices = uint3(0);
	#if TRACK_INDICES
	indices.x = asuint(state.m_speed) & 0x0000FFFF;
	indices.y = asuint(state.m_speed) >> 16;
	indices.z = asuint(state.m_lifeTime) >> 16;
	uint meshId24 = DecodeMeshId24FromRotation(state.m_rotation);
	float2 barys = DecodeBarysFromRotation(state.m_rotation);

	{
		findProcOrig = RecomputeMeshTriangleBaricentricsDivergent(pSrt, needsUpdate, groupThreadId.x, meshId24, indices, barys);
		positionComputed = findProcOrig.m_foundIndex != -1;
		
		if (positionComputed)
		{
			#if !ADVANCE_UV_CAPTURE
			SetStartedAtHead(state);
			SetUintNorm(state, findProcOrig.m_uNorm);
			#endif
			AddDebugStickyParticleRecomputed(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, findProcOrig.m_posWs, state.m_pos, dispatchId.x, state.m_id, meshId24, indices, barys, true);
		}
		else
		{
			#if !ADVANCE_UV_CAPTURE
			SetNotStartedAtHead(state);
			#endif

			if (needsUpdate)
			{
				// failed recompute
				_SCE_BREAK();
				AddDebugStickyParticleRecomputed(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, findProcOrig.m_posWs, state.m_pos, dispatchId.x, state.m_id, meshId24, indices, barys, false);
			}
		}

		//positionComputed = false; // turn off while debugging
	}
	#endif
	

	uint myIndex = 0; // final index of this state. could be different since we compress everything down

	if (needsUpdate)
	{

		int trackingResult = 0;
		float depthDifRightAway = 0;
		float unexpectedOffsetLen = 0;

#define kTrackingOk 0
#define kTrackingBadFlagsRightAway 1
#define kTrackingStencilMismatchRightAway (1 << 1)
#define kTrackingDepthMismatchRightAway (1 << 2)
#define kTrackingStencilMismatchUsingOldPosition (1 << 3)
#define kTrackingDepthMismatchUsingOldPosition (1 << 4)
#define kTrackingDepthUseReadjustedPosition (1 << 5)
#define kTrackingDepthDontReadjustPosition (1 << 6)
#define kTrackingDepthReadjustDidntWorkButTooMuchUnexpectedMovement (1 << 7)

		// continue life. run update:
		float3 prevPos = state.m_pos;

		state.m_pos = state.m_pos + state.m_scale * pSrt->m_delta; //  we encode direction in scale

		if (GetFlags0(state) < kNumOfFramesToRecover && positionComputed)
		{
			// we have computed new position because the mesh data is available

			// we now need to see if we can generate screen space tracking info. best case scenario, this position is still in screen space, in which case we just update point velocity and we're done
			// in case we are not in screen space we just set the velocity to 0

			state.m_pos = findProcOrig.m_posWs;
			SetFlags0(state, 0);
		}

		float3 origNewPos = state.m_pos;

		if (GetFlags0(state) != 0)
		{
			// keep them around, different color though
			state.m_pos = prevPos;
			//return;
		}

		// calculate expected depth
		float4 newPosH = mul(float4(state.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 newPosNdc = newPosH.xyz / newPosH.w;

		float depthBufferError = GetLinearDepthError(newPosNdc.z, pSrt->m_depthParams);

		float allowedDepthBufferDifference = pSrt->m_rootComputeVec2.x + depthBufferError * pSrt->m_rootComputeVec2.y * 2;
		// sample depth buffer
		//uint2 sspos = uint2((newPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (newPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);
		uint2 sspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);

		
		if (abs(newPosNdc.x) >= 1.0 || abs(newPosNdc.y) >= 1.0)
		{
			// expected position of this particle is off screen, so we just kill it
			// we do it only for particles that are purely screen tracked
			SetFlags0(state, kNumOfFramesToRecover);
		}

		uint prevStencil = state.m_flags1 & 0x000000FF;
		bool prevWasOnHair = (state.m_flags1 & kMaterialMaskHair) != 0;

		// note we allow to try to recover for certain amount of frames
		uint origFlags = 0;
		if (GetFlags0(state) < kNumOfFramesToRecover)
		{
			SetFlags0(state, 0);
			origFlags = GetFlags0(state);
		}

		
		if (GetFlags0(state) == 0)
		{
			//state.m_birthTime = pSrt->m_time; // prolongue the life

			//uint4 sample1 = pSrt->m_pPassDataSrt->m_gbuffer1[sspos];
			//uint materialMaskSample = pSrt->m_pPassDataSrt->m_materialMaskBuffer[sspos];
			//UnpackMaterialMask(materialMaskSample, sample1, setup.materialMask);


			if (prevStencil != stencilTextureToUse[sspos])
			{
				// completely jumped off to something different
				if (!positionComputed)
					state.m_pos = prevPos; // move back, redo depth at position from before
				SetFlags0(state, 1);

				trackingResult = trackingResult | kTrackingStencilMismatchRightAway;

				// will try again with old position and see if we can recover
				//return;
			}

			float depthNdc = depthBufferToUse[sspos];
			float expectedDepthNdc = newPosNdc.z;

			float expectedDepthLinear = GetLinearDepth(expectedDepthNdc, pSrt->m_depthParams);
			float depthLinear = GetLinearDepth(depthNdc, pSrt->m_depthParams);

			// since we reapply additional offset below we check distance for just very big displacement
			depthDifRightAway = abs(depthLinear - expectedDepthLinear);
			if (depthDifRightAway > allowedDepthBufferDifference)
			{
				// completely jumped off to something different (same as stencil)
				if (!positionComputed)
					state.m_pos = prevPos; // move back, redo depth at position from before
				SetFlags0(state, 1);

				trackingResult = trackingResult | kTrackingDepthMismatchRightAway;

				// will try again with old position and see if we can recover
				//return;
			}

			// allow to recover
			if (GetFlags0(state) != 0 && !positionComputed)
			{
				// we just jumped off somewhere bad, try using old position and redo the tests
				// calculate expected depth
				newPosH = mul(float4(state.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
				newPosNdc = newPosH.xyz / newPosH.w;

				// sample stencil and depth buffer
				//sspos = uint2((newPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (newPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);
				sspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);

				bool ok = true;
				if (prevStencil != stencilTextureToUse[sspos] || abs(newPosNdc.x) > 1.0 || abs(newPosNdc.y) > 1.0)
				{
					// completely jumped off to something different, this is fatal
					// we don't change the state
					ok = false;

					trackingResult = trackingResult | kTrackingStencilMismatchUsingOldPosition;

				}

				depthNdc = depthBufferToUse[sspos];
				expectedDepthNdc = newPosNdc.z;

				expectedDepthLinear = GetLinearDepth(expectedDepthNdc, pSrt->m_depthParams);
				depthLinear = GetLinearDepth(depthNdc, pSrt->m_depthParams);

				if (abs(depthLinear - expectedDepthLinear) > allowedDepthBufferDifference)
				{
					// completely jumped off to something different, this is fatal
					// we don't change the state
					ok = false;

					trackingResult = trackingResult | kTrackingDepthMismatchUsingOldPosition;
				}

				if (ok)
				{
					// prev position is fine, just stay there
					SetFlags0(state, 0);
					//origFlags = 0;
				}
			}

			// if we were at bad pixel but computed position (computed position is offscreen but still comptable) state.m_flags0 != 0

			if (GetFlags0(state) == 0)
			{
				// good, readjust position to snap to background

				newPosNdc.z = depthNdc;

				newPosH = mul(float4(newPosNdc, 1), pSrt->m_pPassDataSrt->m_mAltVPInv);
				
				if (!positionComputed)
				{
					// we do this only if we havent already computed the new position
					state.m_pos = newPosH.xyz / newPosH.w;
					state.m_pos += pSrt->m_altWorldOrigin;
				}

				// recalculate velocity based on motion vector
				float2 motionVectorSNorm = pSrt->m_pPassDataSrt->m_motionVector[sspos];
				int2 motionVector = int2(motionVectorSNorm * pSrt->m_screenResolution);
				lastFrameSSpos = int2(float2(sspos)+motionVector);

				float3 posNdcUnjittered = newPosNdc;
				posNdcUnjittered.xy -= pSrt->m_projectionJitterOffsets.xy;

				float3 lastFramePosNdcUnjittered = float3(posNdcUnjittered.x + motionVectorSNorm.x * 2, posNdcUnjittered.y - motionVectorSNorm.y * 2, pSrt->m_pPassDataSrt->m_lastFrameOpaquePlusAlphaDepthTexture[lastFrameSSpos]);
				float3 lastFramePosNdc = lastFramePosNdcUnjittered;
				lastFramePosNdc.xy += pSrt->m_projectionJitterOffsets.zw;

				float4 lastFramePosH = mul(float4(lastFramePosNdc, 1), pSrt->m_pPassDataSrt->m_mLastFrameAltVPInv);
				float3 lastFramePosWs = lastFramePosH.xyz / lastFramePosH.w;
				lastFramePosWs += pSrt->m_lastFrameAltWorldOrigin;

				if (trackingAlphaDepth)
				{
					// we can't look at motion vectors. we assume last frame's position was the same as particle's position
					lastFramePosWs = prevPos;
				}


				#if 0
				// readjust the speed and let the particle live longer

				state.m_scale = (state.m_pos - lastFramePosWs) / pSrt->m_delta; // we store velocity in scale
				#else
				if (positionComputed)
				{
					state.m_scale = (state.m_pos - lastFramePosWs) / pSrt->m_delta; // we store velocity in scale
				}
				else
				{
					// The code below is either
					// 1) stop any more processing, use current position as final position and recompute the new velocity
					// 2) Compare previous position of current pixel with expected previous position (previous position of particle)
					//    and apply the difference and re compute the new current position with difference applied, then choose the new position vs recomputed new position
					//    this recomputing allows for tracking on objects that accelerate or decelerate

					// this is the code to accomodate acceleration
					// 
					if (origFlags)
					{
						// if we had several frames of error, but below death count
						prevPos = lastFramePosWs;
					}

					// now we compare previous position of this pixel with where this particle was last frame
					// if all is good, this offset should be 0
					// if it is not 0 means that the object moved at a different speed than we predicted
					//M1:
					float3 lastFramePosUnexpectedOffset = prevPos - lastFramePosWs;
					//M2
					//float3 lastFramePosUnexpectedOffset = (state.m_scale * pSrt->m_delta) - (state.m_pos - lastFramePosWs);

					unexpectedOffsetLen = length(lastFramePosUnexpectedOffset);

					//M1
					prevPos = state.m_pos;
					//M2
					//prevPos = preUpdatePos;


					// we can now readjust our position by that offset and redo everything
					//M1
					state.m_pos = state.m_pos + lastFramePosUnexpectedOffset;
					//M2
					//state.m_pos = preUpdatePos + (state.m_pos - lastFramePosWs);

					newPosH = mul(float4(state.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
					newPosNdc = newPosH.xyz / newPosH.w;

					// sample depth buffer
					//sspos = uint2((newPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (newPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);
					uint2 prevSSPos = sspos;
					sspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);

					depthNdc = depthBufferToUse[sspos];
					expectedDepthNdc = newPosNdc.z;

					expectedDepthLinear = GetLinearDepth(expectedDepthNdc, pSrt->m_depthParams);
					depthLinear = GetLinearDepth(depthNdc, pSrt->m_depthParams);

					// we HAVE to apply unexpected offset to deal with acceleration of underlying surfaces
					// but if applying that offset lands on a pixel that still doesn't match our previous position then that offset is bad


					// recalculate velocity based on motion vector
					motionVectorSNorm = pSrt->m_pPassDataSrt->m_motionVector[sspos];
					motionVector = int2(motionVectorSNorm * pSrt->m_screenResolution);
					lastFrameSSpos = int2(float2(sspos)+motionVector);

					posNdcUnjittered = newPosNdc;
					posNdcUnjittered.xy -= pSrt->m_projectionJitterOffsets.xy;

					lastFramePosNdcUnjittered = float3(posNdcUnjittered.x + motionVectorSNorm.x * 2, posNdcUnjittered.y - motionVectorSNorm.y * 2, pSrt->m_pPassDataSrt->m_lastFrameOpaquePlusAlphaDepthTexture[lastFrameSSpos]);
					lastFramePosNdc = lastFramePosNdcUnjittered;
					lastFramePosNdc.xy += pSrt->m_projectionJitterOffsets.zw;

					lastFramePosH = mul(float4(lastFramePosNdc, 1), pSrt->m_pPassDataSrt->m_mLastFrameAltVPInv);
					lastFramePosWs = lastFramePosH.xyz / lastFramePosH.w;
					lastFramePosWs += pSrt->m_lastFrameAltWorldOrigin;

				
					// in case we lost tracking this frame by jumping based on our velocity and used just previous frames data as starting point
					// we can also retry old position + old velocity + unexpected offset

					{
						float3 origPosAdjusted = origNewPos + lastFramePosUnexpectedOffset;
						float4 origPosAdjustedH = mul(float4(origPosAdjusted, 1), pSrt->m_pPassDataSrt->g_mVP);
						float3 origPosAdjustedNdc = origPosAdjustedH.xyz / origPosAdjustedH.w;


						uint2 origAdjustedSSpos = NdcToScreenSpacePos(pSrt, origPosAdjustedNdc.xy);

						float origAdjustedDepthNdc = depthBufferToUse[origAdjustedSSpos];

						//expectedDepthNdc = newPosNdc.z;

						float origAdjustedDepthLinear = GetLinearDepth(origAdjustedDepthNdc, pSrt->m_depthParams);


						float2 origAdjustedMotionVectorSNorm = pSrt->m_pPassDataSrt->m_motionVector[origAdjustedSSpos];
						int2 origAdjustedotionVector = int2(origAdjustedMotionVectorSNorm * pSrt->m_screenResolution);
						int2 origAdjustedLastFrameSSpos = int2(float2(origAdjustedSSpos)+origAdjustedotionVector);

						//posNdcUnjittered = newPosNdc;
						//posNdcUnjittered.xy -= pSrt->m_projectionJitterOffsets.xy;
						//
						//lastFramePosNdcUnjittered = float3(posNdcUnjittered.x + motionVectorSNorm.x * 2, posNdcUnjittered.y - motionVectorSNorm.y * 2, pSrt->m_pPassDataSrt->m_lastFrameOpaquePlusAlphaDepthTexture[lastFrameSSpos]);
						//lastFramePosNdc = lastFramePosNdcUnjittered;
						//lastFramePosNdc.xy += pSrt->m_projectionJitterOffsets.zw;
						//
						//lastFramePosH = mul(float4(lastFramePosNdc, 1), pSrt->m_pPassDataSrt->m_mLastFrameAltVPInv);
						//lastFramePosWs = lastFramePosH.xyz / lastFramePosH.w;
						//lastFramePosWs += pSrt->m_lastFrameAltWorldOrigin;

					}


					if (abs(depthLinear - expectedDepthLinear) > 0.2f || prevStencil != stencilTextureToUse[sspos])
					{
						// this result is completely un acceptable
					}
					else
					{
						// this result is potentially good. lets compare it to non-unexpected-adjusted position
					}

					if (abs(depthLinear - expectedDepthLinear) > 0.2f || prevStencil != stencilTextureToUse[sspos] || length(lastFramePosWs-prevPos) > 0.03)
					{
						// readjusting made it much worse,  stay as before (first version was successful, to a point. let's see how much of the unexpected offset do we have)

						if (length(lastFramePosUnexpectedOffset) > 0.03)
						{
							// we have too much unexpected offsetm, and adjusting by it didn't help. we have to bail
							SetFlags0(state, 1);

							trackingResult = trackingResult | kTrackingDepthReadjustDidntWorkButTooMuchUnexpectedMovement;
						}
						else
						{
							//we had some unexpected offset, we couldn't readjust but choose to still stick around
							state.m_pos = prevPos;
							state.m_scale = (state.m_pos - prevPos) / pSrt->m_delta; // we store velocity in scale
							sspos = prevSSPos;

							trackingResult = trackingResult | kTrackingDepthDontReadjustPosition;
						}
					}
					else
					{

						trackingResult = trackingResult | kTrackingDepthUseReadjustedPosition;

						// good, readjust position

						newPosNdc.z = depthNdc;

						newPosH = mul(float4(newPosNdc, 1), pSrt->m_pPassDataSrt->m_mAltVPInv);
						state.m_pos = newPosH.xyz / newPosH.w;
						state.m_pos += pSrt->m_altWorldOrigin;


					


						state.m_scale = (state.m_pos - lastFramePosWs) / pSrt->m_delta; // we store velocity in scale


						if (trackingAlphaDepth)
						{
							// we can't look at motion vectors. we assume last frame's position was the same as particle's position
							state.m_scale = float3(0, 0, 0);
						}

					}
				// so we found what seems to be a good position. do some other tests.
				// for example we could be on hair now
				} // if !positionComputed
				#endif
			}
			else
			{
				if (positionComputed)
				{
					// this means we have a good position, but it is not visible
					// we will clear the error, so we can use this state, but we will have to clear out the velocity since we have no screen space data
					SetFlags0(state, 0);
					state.m_scale = float3(0, 0, 0);
				}
			}
		}
		else
		{
			trackingResult = kTrackingBadFlagsRightAway;
		}


		if (GetFlags0(state) != 0) // if != we either are on first frame of error or we continuing error state
		{
			SetFlags0(state, GetFlags0(state) + origFlags); // accumulate error frames

			// very harsh condition, basically we force to die immediately when we loose tracking
			SetFlags0(state, kNumOfFramesToRecover);

		}

		//bool wasNewParticle = state.m_data <= 0;

		//state.m_data += 1.0f; // increment with each life frame

		// write the state out to new buffer

		// at this point we have some threads that have succeeded and some that have died from lifetime
		// each active thread will grab its new index
		// note we have invlaid states here that will not be rendered. ideally we could remove them and mark the preceding invisible

		// if state is invisible it has state.m_flags0 != 0

		

		AddDebugRibbonStickyParticleTracking(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, state.m_pos, preUpdatePos, 0.01f, trackingResult, depthDifRightAway, unexpectedOffsetLen);

		AddDebugStickyParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, state.m_pos, preUpdatePos, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), GetFlags0(state), state.m_flags1, dispatchId.x, state.m_id, state.m_birthTime);

		
#if 1
		ulong laneMask = __s_read_exec();
		numLanes = __s_bcnt1_i32_b64(laneMask);


		uint laneMask_lo = (uint)laneMask;
		uint laneMask_hi = (uint)(laneMask >> 32);
		myIndex = __v_mbcnt_hi_u32_b32(laneMask_hi, myIndex);
		myIndex = __v_mbcnt_lo_u32_b32(laneMask_lo, myIndex);
#endif
		// now particle index is the new compressed location

		particleIndex = iStateOffset + myIndex;
		uint renderState = GetRenderState(state);

		Setup setup = (Setup)0;
		uint4 sample1 = pSrt->m_pPassDataSrt->m_gbuffer1[sspos];
		uint materialMaskSample = pSrt->m_pPassDataSrt->m_materialMaskBuffer[sspos];
		UnpackMaterialMask(materialMaskSample, sample1, setup.materialMask);

		// we might be now on hair. mark the state as such. note we never clear it, because once on hair, we likely have bad tracking and we just dont render particles that are on hair
		state.m_flags1 = state.m_flags1 | (setup.materialMask.hasHair ? kMaterialMaskHair : 0);


		pSrt->m_particleStates[particleIndex] = state;

		float allowedDepthError = pSrt->m_rootComputeVec2.x + depthBufferError * pSrt->m_rootComputeVec2.y * 2;

		isActiveThread = true;


#define ADD_STATES_LATER 0



		if (myIndex == numLanes - 1)
		{
#if 1
			// also we can move this position and generate new particle that is the bleeding edge of a trail
			if ((numLanes < 62) && isDynamicTrail && GetFlags0(state) == 0 && renderState != kRenderStateBlue /* && ((state.m_id & 0x00FF) < 128)*/)
			{
				isAddingThread = 1;
				#if !ADD_STATES_LATER

				// this is the last particle. it can try spawning new particle
				
				uint ribbonType = (state.m_id >> (8 + 6)) & 1;
				bool allowSkin = ribbonType == 0; // onl;y hard ribbons can be on skin
				bool added = false;
				ParticleStateV0 addedState = ParticleStateV0(0);
				uint2 addedPartSSPos;
				{
					bool tooHorizontal;
					uint2 newPartSSPos;
					float3 newPartPosNdc;


					//float depthNdc = depthBufferToUse[sspos + uint2(0, 0)];
					//float depthLinear = GetLinearDepth(depthNdc, pSrt->m_depthParams);
					//float depthNdcX = depthBufferToUse[sspos + uint2(1, 0)];
					//float depthLinearX = GetLinearDepth(depthNdcX, pSrt->m_depthParams);
					//float depthNdcY = depthBufferToUse[sspos + uint2(0, 1)];
					//float depthLinearY = GetLinearDepth(depthNdcY, pSrt->m_depthParams);

					
					CalculateNewPotentialPos(pSrt, ribbonType, state.m_pos, sspos, 0, 0, 0, newPartSSPos, newPartPosNdc, tooHorizontal, useOpaquePlusAlpha);

					addedPartSSPos = newPartSSPos;

					if (newPartSSPos.x < pSrt->m_screenResolution.x && newPartSSPos.y < pSrt->m_screenResolution.y)
					{
						uint newPartStencil = stencilTextureToUse[newPartSSPos];
						float expectedDepthLinear = GetLinearDepth(newPartPosNdc.z, pSrt->m_depthParams);
						float depthLinear = GetLinearDepth(depthBufferToUse[newPartSSPos], pSrt->m_depthParams);
						bool newTrackingAlphaDepth = depthBufferToUse[newPartSSPos] != pSrt->m_pPassDataSrt->m_primaryDepthTexture[newPartSSPos];

						prevStencil = newPartStencil;

						if ((newPartStencil == prevStencil) && (abs(depthLinear - expectedDepthLinear) < allowedDepthError) && !tooHorizontal)
						{
							// create new particle at slightly different location
							ParticleStateV0 newState = ParticleStateV0(0);
							added = NewStickyParticleDepthReadjust(pSrt, dispatchId, groupThreadId.x, state, iStateOffset + numLanes, newPartSSPos, newTrackingAlphaDepth, newPartStencil, newState, allowSkin, useOpaquePlusAlpha);
							addedState = newState;

							if (added)
							{
								// low bits store distance to next element
								uint oldVal = asuint(pSrt->m_particleStates[iStateOffset + numLanes - 1].m_data);
								oldVal |= f32tof16(length(state.m_pos - addedState.m_pos));
								pSrt->m_particleStates[iStateOffset + numLanes - 1].m_data = asfloat(oldVal);

								numLanes++;
								numNewStates++;

								AddDebugNewStickyParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, addedState.m_pos, state.m_pos, 0.01f, float4(1.0f, 1.0f, 0.0f, 1.0f), GetFlags0(addedState), addedState.m_flags1, dispatchId.x, addedState.m_id, addedState.m_birthTime);


								//sentinelRibbonLength += sqrt(distSqr);

								//uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), __get_vsharp(pSrt->m_particleFeedBackHeaderData), 0, 0);
								//if (prevIndex < 512)
								//{
								//	// can add new data
								//	pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, 0, groupThreadId.x, 0 /*ribbonId*/);
								//	pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(0, 0, 0, 0);
								//	pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(addedState.m_pos, 0);
								//	pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(0, 1, 1, 0.0f);
								//}
							}
							else
							{
								// failed because something mismatched in pixel
								AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, state.m_pos, preUpdatePos, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), GetFlags0(state), state.m_flags1, dispatchId.x, state.m_id, ParticleFailReason_AddFail);
							}
						}
						else
						{
							// couldn't create the particle. Are we on a curve?
							// for now just set the state
							SetRenderState(state, kRenderStateBlue);
							pSrt->m_particleStates[iStateOffset + numLanes - 1].m_flags1 = state.m_flags1;

							// failed because something mismatched
							AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, state.m_pos, preUpdatePos, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), GetFlags0(state), state.m_flags1, dispatchId.x, state.m_id, ParticleFailReason_StencilHorizontalDepth);
						}
					}
				}

				// can try adding another one
				#if ADVANCE_UV_CAPTURE
				int kNumTries = 16; // 8;

				while (added && kNumTries > 0 && numLanes < 62)
				{
					kNumTries -= 1;

					bool tooHorizontal;
					uint2 newPartSSPos;
					float3 newPartPosNdc;
					CalculateNewPotentialPos(pSrt, ribbonType, addedState.m_pos, addedPartSSPos, 0, 0, 0, newPartSSPos, newPartPosNdc, tooHorizontal, useOpaquePlusAlpha);
					added = false;
					if (/*impossible condition && renderState != kRenderStateBlue*/ newPartSSPos.x < pSrt->m_screenResolution.x && newPartSSPos.y < pSrt->m_screenResolution.y)
					{
						uint newPartStencil = stencilTextureToUse[newPartSSPos];
						float expectedDepthLinear = GetLinearDepth(newPartPosNdc.z, pSrt->m_depthParams);
						float depthLinear = GetLinearDepth(depthBufferToUse[newPartSSPos], pSrt->m_depthParams);
						bool newTrackingAlphaDepth = depthBufferToUse[newPartSSPos] != pSrt->m_pPassDataSrt->m_primaryDepthTexture[newPartSSPos];

						if ((newPartStencil == prevStencil) && (abs(depthLinear - expectedDepthLinear) < allowedDepthError) && !tooHorizontal)
						{
							// create new particle at slightly different location
							ParticleStateV0 newState = ParticleStateV0(0);
							added = NewStickyParticleDepthReadjust(pSrt, dispatchId, groupThreadId.x, addedState, iStateOffset + numLanes, newPartSSPos, newTrackingAlphaDepth, newPartStencil, newState, allowSkin, useOpaquePlusAlpha);

							if (added)
							{
								// low bits store distance to next element
								uint oldVal = asuint(pSrt->m_particleStates[iStateOffset + numLanes - 1].m_data);
								oldVal |= f32tof16(length(addedState.m_pos - newState.m_pos));
								pSrt->m_particleStates[iStateOffset + numLanes - 1].m_data = asfloat(oldVal);


								numLanes++;
								numNewStates++;

								AddDebugNewStickyParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, newState.m_pos, addedState.m_pos, 0.01f, float4(1.0f, 1.0f, 0.0f, 1.0f), GetFlags0(newState), newState.m_flags1, dispatchId.x, newState.m_id, newState.m_birthTime);

								//sentinelRibbonLength += sqrt(distSqr);

								//uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), __get_vsharp(pSrt->m_particleFeedBackHeaderData), 0, 0);
								//if (prevIndex < 512)
								//{
								//	// can add new data
								//	pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, 0, groupThreadId.x, 0 /*ribbonId*/);
								//	pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(0, 0, 0, 0);
								//	pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(newState.m_pos, 0);
								//	pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(0, 1, 1, 0.0f);
								//}

								addedState = newState;
								addedPartSSPos = newPartSSPos;
							}
						}
						else
						{
							// couldn't create the particle. Are we on a curve?
							// for now just set the state
							SetRenderState(state, kRenderStateBlue);
							pSrt->m_particleStates[iStateOffset + numLanes - 1].m_flags1 = state.m_flags1;
						}
					}
				}
				#endif // if ADVANCE_UV_CAPTURE
				#endif // if !ADD_STATES_LATER

			} // if trying to add a new particle
#endif
			// also write the sentinel
			// we write the sentinel after the last good state.
			pSrt->m_particleStates[iStateOffset + numLanes].m_id = 0;
			pSrt->m_particleStates[iStateOffset + numLanes].m_scale.x = 0;
			pSrt->m_particleStates[iStateOffset + 63].m_scale.y = sentinelRibbonLength; // this is mainly to handle the case when we were not adding anything
		} // if last thread

	} // if good life time (if needs update)

	#if TRACK_INDICES
	if (true)
	{
		bool needRemap = needsUpdate && !positionComputed;

		ParticleStateV0 addedState = pSrt->m_particleStates[iStateOffset + myIndex]; // this is sbuffer read. but this state should not be in k-cache, we wrote it to L2, and now will read into k-cache

		needRemap = needRemap && (GetFlags0(addedState) == 0); // we only remap the particles we know have succeeded the update well

		int addedStateValid = 0;

		// check if too far in bind pose space

		float4 newPosH = mul(float4(addedState.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 newPosNdc = newPosH.xyz / newPosH.w;

		uint2 addedSspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);


		FindProcessResults findProc = FindProcessMeshTriangleBaricentricsDivergent(pSrt, needRemap, addedSspos, addedState.m_pos, groupThreadId.x, false, BLOOD_RIBBON_SPAWN_BLOOD_MAP_THRESHOLD, true);


		if (findProc.m_foundIndex != -1)
		{
			pSrt->m_particleStates[iStateOffset + myIndex].m_rotation = EncodeRotationFromBindPose(findProc.m_bindPosLs, findProc.m_baricentrics, findProc.m_meshId);
			pSrt->m_particleStates[iStateOffset + myIndex].m_speed = asfloat((findProc.m_indices.x & 0x0000FFFF) | (findProc.m_indices.y << 16));
			pSrt->m_particleStates[iStateOffset + myIndex].m_lifeTime = asfloat((asuint(pSrt->m_particleStates[iStateOffset + myIndex].m_lifeTime) & 0x0000FFFF) | (findProc.m_indices.z << 16));

			#if !ADVANCE_UV_CAPTURE
			SetStartedAtHead(pSrt->m_particleStates[iStateOffset + myIndex]);
			SetUintNorm(pSrt->m_particleStates[iStateOffset + myIndex], findProc.m_uNorm);
			#endif

			AddDebugStickyBaryRemapParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, findProc.m_posWs, addedState.m_pos, dispatchId.x, addedState.m_id, findProc.m_meshId, findProc.m_indices, findProc.m_baricentrics.xy);
		}
	}
	#endif



	__s_dcache_inv();

	// now we are back to all threads activated
	// go trhough all new particles and check for blood

	// by now the states have been compacted
	ulong laneMask = __v_cmp_eq_u32(isActiveThread, 1); // active threads here are not compacted. i.e. they dont start at 0. the written states are compacted. so this mask could be 0000111000, and states written are 0, 1, 2
	int numActiveStates = __s_bcnt1_i32_b64(laneMask); // should be guaranteed to never be 0



	ulong addingMask = __v_cmp_eq_u32(isAddingThread, 1);

	if (numActiveStates <= 0)
		return;

	int addingThread = __s_ff1_i32_b64(addingMask); // should be the last active thread that corresponded to last state. note, again this is not compacted. this id could be 5, but was written into state 3.
	                                                // also this thread should have written the last state (or there is a bug)

	if (addingThread == -1)
		return;

	// adding thread was marked because it has good state and is last
	numNewStates = ReadLane(numNewStates, asuint(addingThread)); // this thread knows the correct value of number of new states

	numLanes = ReadLane(numLanes, asuint(addingThread));

	ParticleStateV0 precedingState = pSrt->m_particleStates[iStateOffset + numActiveStates - 1];
	
	float3 precedingBindPose = DecodeBindPosFromRotation(precedingState.m_rotation);

	// if new particles were added, the last state was guaranteed to have valid positions, so that thread has valid lastFrameSSpos
	lastFrameSSpos.x = ReadLane(lastFrameSSpos.x, asuint(addingThread)); // TODO: maybe just recalculate to save on registers
	lastFrameSSpos.y = ReadLane(lastFrameSSpos.y, asuint(addingThread));


	

	// lookup data from preceding state. we technically could have it stored in the state too
	uint preceedingMeshId = -1;
	uint preceedingBodyPart = 255;
	FindProcessResults findPreceedingProc;
	{
		float4 precPosH = mul(float4(precedingState.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 precPosNdc = precPosH.xyz / precPosH.w;

		uint2 precSspos = NdcToScreenSpacePos(pSrt, precPosNdc.xy);

		findPreceedingProc = FindProcessMeshTriangleBaricentrics(pSrt, precSspos, precedingState.m_pos, groupThreadId.x);
		// one thread potentially succeeds
		if (findPreceedingProc.m_foundIndex != -1)
		{
			preceedingMeshId = findPreceedingProc.m_meshId;
			preceedingBodyPart = findPreceedingProc.m_bodyPart;
		}
	}

	uint ribbonType = (precedingState.m_id >> (8 + 6)) & 1;
	bool allowHead = ribbonType == 0; // don't allow head on soft ribbons

	ulong lookupStateValid = __v_cmp_ne_u32(findPreceedingProc.m_foundIndex, uint(-1));
	int lookupThread = __s_ff1_i32_b64(lookupStateValid); // which thread succeeded

	if (lookupThread != -1)
	{
		preceedingMeshId = ReadLane(preceedingMeshId, uint(lookupThread));
		preceedingBodyPart = ReadLane(preceedingBodyPart, uint(lookupThread));	
	}
	else
	{
		// stays -1, will fail if we find a valid mesh below
	}

	// go through added states and check the distances in bind pose and potentially discard the new states

	if (dispatchId.x == ReadFirstLane(dispatchId.x))
	{
		AddDebugSetSentinelEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, precedingState.m_pos, precedingState.m_pos, 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), GetFlags0(precedingState), precedingState.m_flags1, dispatchId.x, precedingState.m_id, numNewStates);
	}

	float precedingBloodAmount = 0; // todo: read from adding thread. the last existing state

	uint dataUint = asuint(precedingState.m_data);
	precedingBloodAmount = f16tof32(dataUint >> 16);

	#if TRACK_BIND_POSE
	for (int iNew = 0; iNew < numNewStates; ++iNew)
	{
		//precedingBloodAmount = ReadLane(precedingBloodAmount, asuint(addingThread));

		isAddingThread = 0;
		uint localIndex = numActiveStates - 1 + iNew + 1;
		uint stateIndex = iStateOffset + localIndex;
		ParticleStateV0 addedState = pSrt->m_particleStates[stateIndex]; // this is sbuffer read. but this state should not be in k-cache, we wrote it to L2, and now will read into k-cache


		int addedStateValid = 0;

		// check if too far in bind pose space

		float4 newPosH = mul(float4(addedState.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 newPosNdc = newPosH.xyz / newPosH.w;

		uint2 addedSspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);

	
		// all threads must be active for this to work
		FindProcessResults findProc = FindProcessMeshTriangleBaricentrics(pSrt, addedSspos, addedState.m_pos, groupThreadId.x);
		// one thread potentially succeeds
		float travelDist = 0;
		if (findProc.m_foundIndex != -1)
		{
			int bodyPart = findProc.m_bodyPart;
			if (allowHead || bodyPart != kLook2BodyPartHead)
			{
				pSrt->m_particleStates[stateIndex].m_rotation = EncodeRotationFromBindPose(findProc.m_bindPosLs, findProc.m_baricentrics, findProc.m_meshId); // 32 x, 16bits y
				#if TRACK_INDICES
				pSrt->m_particleStates[stateIndex].m_speed = asfloat((findProc.m_indices.x & 0x0000FFFF) | (findProc.m_indices.y << 16));
				pSrt->m_particleStates[stateIndex].m_lifeTime = asfloat(asuint(pSrt->m_particleStates[stateIndex].m_lifeTime) | (findProc.m_indices.z << 16));

				#if !ADVANCE_UV_CAPTURE
				SetStartedAtHead(pSrt->m_particleStates[stateIndex]);
				SetUintNorm(pSrt->m_particleStates[stateIndex], findProc.m_uNorm);
				#endif
				#endif

				#if ADVANCE_UV_CAPTURE
				SetHaveUvs(pSrt->m_particleStates[stateIndex]);

				if (findProc.m_procType == 0x1)
				{
					SetIsInfected(pSrt->m_particleStates[stateIndex]);
				}

				#endif
				#if ADVANCE_UV_CAPTURE && !TRACK_INDICES
				pSrt->m_particleStates[stateIndex].m_speed = asfloat(PackFloat2ToUInt(findProc.m_uv.x, findProc.m_uv.y));
				#endif

				float3 distVec = findProc.m_bindPosLs - precedingBindPose;

				AddDebugNewStickyBaryDataParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, findProc.m_posWs, addedState.m_pos, dispatchId.x, addedState.m_id, findProc.m_meshId, findProc.m_indices, findProc.m_baricentrics.xy);
				
				float distSqr = dot(distVec, distVec);

				float expectedMoveDist = RIBBON_ROOT_VAR_SPEED(ribbonType) * pSrt->m_delta;

				float allowedMoveDistInBindPose = max(expectedMoveDist * 3.0f, 0.03); // allow a little extra to accomodate stretching of skinned vs bindpose. we always allow a couple of centimeter motion because of imprecisions of going from one pixel to the other
				float allowedMoveDistInBindPoseSqr = allowedMoveDistInBindPose * allowedMoveDistInBindPose;

				// check particle render target and pick up color
				int rtId = ReadFirstLane(findProc.m_rtId);
				float textureBloodMapValue = findProc.m_rtId != 255 ? pSrt->m_pParticleRTs->m_textures[rtId].SampleLevel(pSrt->m_linearSampler, findProc.m_uv, 0).x : 0;
				precedingBloodAmount = precedingBloodAmount * 0.9 + textureBloodMapValue;
				
				precedingBloodAmount = saturate(precedingBloodAmount);

				textureBloodMapValue = precedingBloodAmount;
				if (0.23 < findProc.m_uv.x && findProc.m_uv.x < 0.73 && 0.7 < findProc.m_uv.y && findProc.m_uv.y < 0.9999)
				{
					// head
					textureBloodMapValue *= 0.6;
				}

				uint oldVal = asuint(pSrt->m_particleStates[stateIndex].m_data);
				oldVal |= (f32tof16(textureBloodMapValue) << 16);
				pSrt->m_particleStates[stateIndex].m_data = asfloat(oldVal);



				bool distanceOk = distSqr < allowedMoveDistInBindPoseSqr;
				bool meshIdOk = findProc.m_meshId == preceedingMeshId;
				bool bodyPartOk = findProc.m_bodyPart == preceedingBodyPart;
				if (distanceOk)//  && bodyPartOk)
				{
					addedStateValid = 1;

					travelDist = sqrt(distSqr);
				}
				else
				{
					// failed because of bind pose distance check
					if (!distanceOk)
					{
						AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, addedState.m_pos, /*prevPos*/findProc.m_bindPosLs, expectedMoveDist, float4(precedingBindPose, 1.0f), GetFlags0(addedState), asuint(distSqr), dispatchId.x, addedState.m_id, ParticleFailReason_TooMuchBindPoseMotion);
					}

					if (!bodyPartOk)
					{
						AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, addedState.m_pos, float3(asfloat(findProc.m_bodyPart), asfloat(preceedingBodyPart), 0), 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), GetFlags0(addedState), addedState.m_flags1, dispatchId.x, addedState.m_id, ParticleFailReason_BodyPartMismatch);
					}
				}

				precedingBindPose = findProc.m_bindPosLs;
			}
			else
			{
				// todo: add fail event
				AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, addedState.m_pos, float3(asfloat(findProc.m_bodyPart), asfloat(preceedingBodyPart), 0), 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), GetFlags0(addedState), addedState.m_flags1, dispatchId.x, addedState.m_id, ParticleFailReason_BodyPartHeadCheck);
			}
		}

		if (__v_cmp_ne_u32(findProc.m_foundIndex, uint(-1)) == 0)
		{
			// no thread found it
			if (dispatchId.x == ReadFirstLane(dispatchId.x))
			{
				// one thread writes debug
				AddDebugStickyParticleFailReasonEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, precedingState.m_pos, float3(0, 0, 0), 0.01f, float4(1.0f, 0.0f, 0.0f, 1.0f), GetFlags0(precedingState), precedingState.m_flags1, dispatchId.x, precedingState.m_id, ParticleFailReason_UnmappedMesh);
			}
		}
		ulong theadSuccessMask = __v_cmp_eq_u32(addedStateValid, 1);
		addedStateValid = theadSuccessMask != 0; // succeed if one thread succeeded

		if (!addedStateValid)
		{
			// just write sentinel over this state and bail

			pSrt->m_particleStates[stateIndex].m_id = 0;
			pSrt->m_particleStates[stateIndex].m_scale.x = 0;
			//pSrt->m_particleStates[stateIndex].m_scale.y = sentinelRibbonLength;
			numNewStates = iNew;
			break;
		}
		else
		{
			// this thread succeeded
			travelDist = ReadLane(travelDist, __s_ff1_i32_b64(theadSuccessMask));
			sentinelRibbonLength += travelDist;

			// make sure all threads getsucceeded threads precedingBloodAmount
			precedingBloodAmount = ReadLane(precedingBloodAmount, __s_ff1_i32_b64(theadSuccessMask));

			if (iNew == numNewStates)
			{
				// last state successful, update sentinel
				//pSrt->m_particleStates[stateIndex + 1].m_scale.y = sentinelRibbonLength;
			}
		}

	} // for num new states
	pSrt->m_particleStates[iStateOffset + 63].m_scale.y = sentinelRibbonLength;
	#endif


	#if ADD_STATES_LATER

	// try add new particles
	bool added = false;
	ParticleStateV0 addedState = ParticleStateV0(0);
	uint2 addedPartSSPos;
	{
		bool tooHorizontal;
		uint2 newPartSSPos;
		float3 newPartPosNdc;


		//float depthNdc = depthBufferToUse[sspos + uint2(0, 0)];
		//float depthLinear = GetLinearDepth(depthNdc, pSrt->m_depthParams);
		//float depthNdcX = depthBufferToUse[sspos + uint2(1, 0)];
		//float depthLinearX = GetLinearDepth(depthNdcX, pSrt->m_depthParams);
		//float depthNdcY = depthBufferToUse[sspos + uint2(0, 1)];
		//float depthLinearY = GetLinearDepth(depthNdcY, pSrt->m_depthParams);


		ParticleStateV0 state = pSrt->m_particleStates[iStateOffset + numLanes - 1]; // todo: can grab it with ReadFirstLane?  that migth actually be bad because it will force vgprs storing the whole state
		float4 newPosH = mul(float4(state.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 newPosNdc = newPosH.xyz / newPosH.w;

		uint2 sspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);


		CalculateNewPotentialPos(pSrt, state.m_pos, sspos, 0, 0, 0, newPartSSPos, newPartPosNdc, tooHorizontal, useOpaquePlusAlpha);

		addedPartSSPos = newPartSSPos;
		
		numNewStates = 0;

		float depthBufferError = GetLinearDepthError(newPosNdc.z, pSrt->m_depthParams);

		float allowedDepthError = pSrt->m_rootComputeVec2.x + depthBufferError * pSrt->m_rootComputeVec2.y * 2;


		if (newPartSSPos.x < pSrt->m_screenResolution.x && newPartSSPos.y < pSrt->m_screenResolution.y)
		{
			uint newPartStencil = stencilTextureToUse[newPartSSPos];
			float expectedDepthLinear = GetLinearDepth(newPartPosNdc.z, pSrt->m_depthParams);
			float depthLinear = GetLinearDepth(depthBufferToUse[newPartSSPos], pSrt->m_depthParams);
			bool newTrackingAlphaDepth = depthBufferToUse[newPartSSPos] != pSrt->m_pPassDataSrt->m_primaryDepthTexture[newPartSSPos];

			prevStencil = newPartStencil;

			if ((newPartStencil == prevStencil) && (abs(depthLinear - expectedDepthLinear) < allowedDepthError) && !tooHorizontal)
			{
				// create new particle at slightly different location
				ParticleStateV0 newState = ParticleStateV0(0);
				added = NewStickyParticleDepthReadjust(pSrt, dispatchId, groupThreadId.x, state, iStateOffset + numLanes, newPartSSPos, newTrackingAlphaDepth, newPartStencil, newState);
				addedState = newState;

				if (added)
				{
					// low bits store distance to next element
					uint oldVal = asuint(pSrt->m_particleStates[iStateOffset + numLanes - 1].m_data);
					oldVal |= f32tof16(length(state.m_pos - addedState.m_pos));
					pSrt->m_particleStates[iStateOffset + numLanes - 1].m_data = asfloat(oldVal);

					numLanes++;
					numNewStates++;

					AddDebugNewStickyParticleEvent(__get_vsharp(pSrt->m_particleFeedBackHeaderData), pSrt->m_particleFeedBackData, addedState.m_pos, state.m_pos, 0.01f, float4(1.0f, 1.0f, 0.0f, 1.0f), GetFlags0(addedState), addedState.m_flags1, dispatchId.x, addedState.m_id, addedState.m_birthTime);



					//uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), __get_vsharp(pSrt->m_particleFeedBackHeaderData), 0, 0);
					//if (prevIndex < 512)
					//{
					//	// can add new data
					//	pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, 0, groupThreadId.x, 0 /*ribbonId*/);
					//	pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(0, 0, 0, 0);
					//	pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(addedState.m_pos, 0);
					//	pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(0, 1, 1, 0.0f);
					//}
				}
			}
			else
			{
				// couldn't create the particle. Are we on a curve?
				// for now just set the state
				SetRenderState(state, kRenderStateBlue);
				pSrt->m_particleStates[iStateOffset + numLanes - 1].m_flags1 = state.m_flags1;
			}
		}
	}

	// in case we added some states, write sentinel
	pSrt->m_particleStates[iStateOffset + numLanes].m_id = 0;
	pSrt->m_particleStates[iStateOffset + numLanes].m_scale.x = 0;

	#endif

#if 0



	
	// since we know last frame's position, we could sample the color at that location
	float4 lastFrameColor = pSrt->m_pPassDataSrt->m_lastFramePrimaryFloat[lastFrameSSpos];

	
	if (groupThreadId.x == 0)
	{
		uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), __get_vsharp(pSrt->m_particleFeedBackHeaderData), 0, 0);
		if (prevIndex < 512)
		{
			// can add new data
			pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, 0, groupThreadId.x, 0 /*ribbonId*/);
			pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(numActiveStates - 1, iStateOffset, 0, 0);
			pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(precedingState.m_pos, 0);
			pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(lastFrameColor.xyz, 0.0f);
		}
	}

	

	

	float precedingBloodAmount = f16tof32(asuint(precedingState.m_data) >> 16);

	
	isAddingThread = 1;
	for (int iNew = 0; iNew < numNewStates; ++iNew)
	{
		addingMask = __v_cmp_eq_u32(isAddingThread, 1);

		addingThread = __s_ff1_i32_b64(addingMask);

		//precedingBloodAmount = ReadLane(precedingBloodAmount, asuint(addingThread));

		isAddingThread = 0;
		uint localIndex = numActiveStates - 1 + iNew + 1;
		uint stateIndex = iStateOffset + localIndex;
		ParticleStateV0 lastState = pSrt->m_particleStates[stateIndex]; // this is sbuffer read. but this state should not be in k-cache, we wrote it to L2, and now will read into k-cache



		if (groupThreadId.x == 0)
		{
			//uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), __get_vsharp(pSrt->m_particleFeedBackHeaderData), 0, 0);
			//if (prevIndex < 512)
			//{
			//	// can add new data
			//	pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, 0, groupThreadId.x, 0 /*ribbonId*/);
			//	pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(0, 0, 0, 0);
			//	pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(lastState.m_pos, 0);
			//	pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(0, 1, 1, 0.0f);
			//}
		}


		


		float4 newPosH = mul(float4(lastState.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 newPosNdc = newPosH.xyz / newPosH.w;
		//uint2 sspos = uint2((newPosNdc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (newPosNdc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y);
		uint2 sspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);


		if (!(pSrt->m_features & COMPUTE_FEATURE_GENERATE_BLOOD_MAP_EVENTS))
		{
			FindProcessResults findRes = FindProcessMeshTriangleBaricentrics(pSrt, sspos, lastState.m_pos, groupThreadId.x);
			// only one thread potentially succeeded
			if (findRes.m_foundIndex != -1 && findRes.m_rtId != 255)
			{
			


				// check particle render target and pick up color
				int rtId = ReadFirstLane(findRes.m_rtId);
				float textureBloodMapValue = pSrt->m_pParticleRTs->m_textures[rtId].SampleLevel(pSrt->m_linearSampler, findRes.m_uv, 0).x;
				float bloodMapValue = precedingBloodAmount + textureBloodMapValue;

				bloodMapValue = saturate(bloodMapValue);
				// write it 

				uint oldVal = asuint(lastState.m_data);
				oldVal |= (f32tof16(bloodMapValue) << 16);
				pSrt->m_particleStates[stateIndex].m_data = asfloat(oldVal);

				//precedingBloodAmount = bloodMapValue;

				isAddingThread = 1;
				// write the value to lane0, since we know all threads are executing afterwards
				//WriteLane(precedingBloodAmount, precedingBloodAmount, 0);
				//__v_writelane_b32(precedingBloodAmount, iNew, 1.0/* ReadFirstLane(bloodMapValue)*/);
				//pSrt->m_particleStates[iStateOffset + first_bit].m_scale.x = bloodMapValue;

				ulong exec = __s_read_exec();

				//if (threadId == 0)
				{
					//uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), __get_vsharp(pSrt->m_particleFeedBackHeaderData), 0, 0);
					//if (prevIndex < 512)
					//{
					//	// can add new data
					//	pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, rtId, groupThreadId.x, 0 /*ribbonId*/);
					//	pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(localIndex, iStateOffset, numUpdatingStates, numActiveStates);
					//	pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(lastState.m_pos, numNewStates);
					//	pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(textureBloodMapValue, __s_bcnt1_i32_b64(exec) - 1/*should be 1!*/, 0.0f, 0.0f);
					//}
				}

			}
			else
			{

				//if (groupThreadId.x == 0 && (__s_read_exec() == 0xFFFFFFFFFFFFFFFFLLU))
				//{
				//	uint prevIndex = __buffer_atomic_add(1, uint2(0, 0), __get_vsharp(pSrt->m_particleFeedBackHeaderData), 0, 0);
				//	if (prevIndex < 512)
				//	{
				//		// can add new data
				//		pSrt->m_particleFeedBackData[prevIndex].m_data0 = uint4(kFeedBackTypeDebug, 0, groupThreadId.x, 0 /*ribbonId*/);
				//		pSrt->m_particleFeedBackData[prevIndex].m_data1 = float4(localIndex, iStateOffset, numUpdatingStates, numActiveStates);
				//		pSrt->m_particleFeedBackData[prevIndex].m_data2 = float4(lastState.m_pos, numNewStates);
				//		pSrt->m_particleFeedBackData[prevIndex].m_data3 = float4(0, 1, 1, 0.0f);
				//	}
				//}
			}
		}
	}

#endif
}


[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeRibbonGenerateRenderablesBloodPickup(const uint2 dispatchId : SV_DispatchThreadID,
	const int3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	//__s_dcache_inv();
	//__buffer_wbinvl1();
	// grab my particle
	bool isDynamicTrail = (groupId.x % 2) == 0;

	int iStateOffset = groupId.x * kNumPartsInRibbon;

	int particleIndex = iStateOffset + groupThreadId.x;

	ParticleStateV0 state = pSrt->m_particleStates[particleIndex];
	ParticleStateV0 nextState = pSrt->m_particleStates[particleIndex + 1];

	bool isSentinel = state.m_id == 0;

	// We create a mask which is 1 for all lanes that contain this value
	ulong sentinel_mask = __v_cmp_eq_u32(isSentinel, true);

	// we need to find the earliest sentinel
	int first_bit = __s_ff1_i32_b64(sentinel_mask);

	bool wantWriteData = true;
	if (first_bit == -1)
	{
		wantWriteData = false;
		// return;
	}

	int numStates = first_bit; // num valid states

	if (numStates < 2) // would mean sentinel is at 2 and we only have 2 particles, that's not enough, we need at least 3 particles to produce one quad
	{
		wantWriteData = false;
		//return; // empty
	}

	int numRenderIndicesToWrite = 0; // how many indices whole group wants to write. scalar value

	WriteLane(numRenderIndicesToWrite, numRenderIndicesToWrite, 0);

	int renderIndexToWrite = -1; // index that this thread wants to write
	//_SCE_BREAK();


	int numQuads = int(numStates) - 1; // this is how many quads we will end up having

	float uvStep = 1.0f / numQuads;
	float uvOffset = uvStep * groupThreadId.x;
	int particleBrithIndex = int(state.m_id & 0x000000FF);

	float globalV = particleBrithIndex / 255.0f;

	bool badState = IsBadRibbonState(state); // this test has to EXACTLY match one below for next bad state

	uint renderState = GetRenderState(state);

	uint nextRenderState = GetRenderState(nextState);

	bool kUseGlobalVOffset = true;


	// note we don't do anything with last particle at all, it just is used for direction for last quad
	// we also completely skip bad state particles
	// we will also not draw particles that have next one as bad
	if ((groupThreadId.x < numStates) && !badState && wantWriteData)
	{
		// we are before sentinel

		uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
		uint gdsOffsetOld = pSrt->m_gdsOffsetCounterOld;

		uint size, stride;
		pSrt->m_particleStates.GetDimensions(size, stride);



		// todo: should check this by reading preceding lane?
		bool nextIsBad = IsBadRibbonState(nextState); // IMPORTANT: this test has to match the test of isBade above entering this if statement, because we have to guarantee next thread enters this if statement
													  // it at least has to be as strict. so we can still set it to true, but we can't have it false while previous thread evaluated it to true

													  //nextIsBad = nextIsBad || (distToNext > allowedDistance) || !depthTestOk; // we could also store this as result in state flags

													  // I myself am valid but I will not render because next one is bad or maybe ity is not bad we just have artificial break
		bool lastInSequence = (groupThreadId.x >= numQuads || nextIsBad || (renderState == kRenderStateInvisible));


		ulong lastInSequenceLaneMask = __v_cmp_eq_u32(lastInSequence, true);

		bool isDummyParticle = groupThreadId.x > 0 ? (__v_lshl_b64(1, groupThreadId.x - 1) & lastInSequenceLaneMask) != 0 : 0; // dummy particle means preceding state is also last in sequence. so we can use this just for orienting preceding particle

		isDummyParticle = lastInSequence && isDummyParticle;

		int destinationInstanceIndex = isDummyParticle ? -1 : NdAtomicIncrement(gdsOffsetOld); // states are tracked by old counter since we need a different counter for states vs rendrables

		ulong isDummyLaneMask = __v_cmp_eq_u32(isDummyParticle, true);
		bool nextIsDummy = groupThreadId.x < 63 ? (__v_lshl_b64(1, groupThreadId.x + 1) & isDummyLaneMask) != 0 : 0; // it will be false if next didnt enter the if statement (bad particle) or if next one is not an invisible particle

		// we want to find how many contiguous bits we have in this sequence

		//ulong laneMask = __s_read_exec();
		ulong laneMask = __v_cmp_eq_u32(lastInSequence, false);


		// we will shift right up to current location and look for first 0. that will be the end of current sequence
		ulong maskShr = __v_lshr_b64(laneMask, groupThreadId.x + 1);

		// now find first 0
		uint maskShrLo = uint(maskShr);
		uint maskShrHi = uint(maskShr >> 32);

		// flip to find first 1
		maskShrLo = ~maskShrLo;
		maskShrHi = ~maskShrHi;

		int firstSetLo = __v_ffbl_b32(maskShrLo);
		int bitsAhead = 0; // zero means we are last active bit in this sequence

		if (firstSetLo == -1)
		{
			// current sequence continues past lo bit
			bitsAhead += 32;

			int firstSetHi = __v_ffbl_b32(maskShrHi);

			// we should be guaranteed to never get -1, since this sequence has to end, because we always have sentinel
			bitsAhead += firstSetHi;
			bitsAhead += firstSetHi;
		}
		else
		{
			bitsAhead = firstSetLo;// zero means we are last active bit in this sequence, otewrwise stores how many more bits we have
		}

		// now calculate bits before
		// shift left and find first 0 from left to right (most significant)

		ulong maskShl = groupThreadId.x ? __v_lshl_b64(laneMask, 64 - groupThreadId.x) : 0;

		uint maskShlLo = uint(maskShl);
		uint maskShlHi = uint(maskShl >> 32);

		// flip to find first 1
		maskShlLo = ~maskShlLo;
		maskShlHi = ~maskShlHi;

		int firstSetHi = __v_ffbh_u32(maskShlHi);

		int bitsBefore = 0;

		if (firstSetHi == -1)
		{
			// the sequence continues into the next lo dword

			bitsBefore += 32;

			firstSetLo = __v_ffbh_u32(maskShlLo);

			bitsBefore += firstSetLo;
		}
		else
		{
			bitsBefore = firstSetHi;
		}

		int bitsTotal = bitsBefore + 1 + bitsAhead;

		int numQuadsInSequence = bitsTotal; // not (bitsTotal - 1); because we already excluded last particle
		uvStep = 1.0f / numQuadsInSequence;
		uvOffset = uvStep * bitsBefore;


		if (lastInSequence)
		{
			uvOffset = 1.0;
		}

		if (kUseGlobalVOffset)
		{
			uvOffset = globalV;
		}

		//uvOffset = 1.0/64 * bitsBefore;


		//uvStep = 1.0f / (numQuads);
		//uvOffset = uvStep * groupThreadId.x;


		// we are just going to copy paste the particles
		// this cant happen with ribbons
		///if (destinationInstanceIndex >= size) // we can't do this test without thinking more, since it will produce bad quads. || bitsTotal < 2)
		///{
		///	// decrement back
		///	NdAtomicDecrement(gdsOffsetNew);
		///	// can't add new particles
		///	return;
		///}





		float4x4	g_identity = { { 1, 0, 0, 0 },{ 0, 1, 0, 0 },{ 0, 0, 1, 0 },{ 0, 0, 0, 1 } };
		float3 kUnitYAxis = { 0.0f, 1.0f, 0.0f };


		float3 pos = state.m_pos;

		ParticleInstance inst = ParticleInstance(0);

		inst.world = g_identity;			// The object-to-world matrix
		inst.color = float4(1, 1, 1, 1);			// The particle's color

		if (GetFlags0(state) > 10)
		{
			inst.color = float4(1, 0, 0, 1);
		}
		else if (GetFlags0(state) > 0)
		{
			inst.color = float4(1, 1, 0, 1);
		}

		// kRenderStateBlue is when we can't create new particle
		if (renderState == kRenderStateBlue || nextRenderState == kRenderStateBlue)
		{
			inst.color = float4(0, 0, 1, 1);
		}


		inst.texcoord = float4(1, 1, 0, 0);		// Texture coordinate scale and bias (uv = uv0 * texcoord.xy + texcoord.zw)
												//inst.texcoord.x = 0.5f;
		inst.texcoord.w = uvOffset;


		//inst.color = float4(0, isDynamicTrail ? uvOffset * uvOffset : 0, !isDynamicTrail ? uvOffset * uvOffset : 0, 1);
		//inst.color = float4(0, isDynamicTrail ? 1 : 0, !isDynamicTrail ? 1 : 0, 1);


		inst.userh = float4(0, 0, 0, 0);			// User attributes (used to be half data type)
		inst.userf = float4(0, 0, 0, 0);			// User attributes
		inst.partvars = float4(0, 0, 0, 0);		// Contains age, normalized age, ribbon distance, and frame number
		inst.invscale = float4(1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f);		// Reciprocal of the particle's half-scale vector

		float age = (pSrt->m_time - state.m_birthTime);

		inst.userh.x = age * kFps;
		inst.userh.y = numQuadsInSequence;

		if (kUseGlobalVOffset)
		{
			inst.userh.y = 128.0;
		}

		inst.userh.z = (groupId.x / 2) / 16.0; // same value for both static and dynamic string


		float3 toCam = normalize(pSrt->m_cameraPosWs.xyz - pos);

		float distToNext = length(nextState.m_pos - pos);




		// based on velocity
		uint m_data = asuint(state.m_data);
		float expectedDistance = f16tof32(m_data); //   pSrt->m_rootComputeVec1.y * (pSrt->m_delta > 0.0001 ? pSrt->m_delta : 0.0333f); // assuming framerate is pretty stable and is close to 30 fps for when we are paused
		float bloodAmount = f16tof32(m_data >> 16);

		float allowedDistance = expectedDistance * 2 + 0.02; // give a little slack

		float allowedDepthDifference = allowedDistance / 3.0; // we only allow fraction of speed to differ in depth

		float distToCamera0 = length(pos - pSrt->m_altWorldOrigin.xyz);
		float distToCamera1 = length(nextState.m_pos - pSrt->m_altWorldOrigin.xyz);

		float depthTestOk = abs(distToCamera0 - distToCamera1) < allowedDepthDifference;


		float3 toNextParticle = normalize(nextState.m_pos - pos);

		if (lastInSequence && !nextIsDummy)
		{
			// predict by looking back
			toNextParticle = normalize(pos - pSrt->m_particleStates[particleIndex - 1].m_pos);
			distToNext = length(pos - pSrt->m_particleStates[particleIndex - 1].m_pos);
		}

		//inst.world = TransformFromLookAt(toCam, state.m_rotation.xyz, pos, true);

		float4 newPosH = mul(float4(state.m_pos, 1), pSrt->m_pPassDataSrt->g_mVP);
		float3 newPosNdc = newPosH.xyz / newPosH.w;

		uint2 sspos = NdcToScreenSpacePos(pSrt, newPosNdc.xy);


		float3 depthNormal = CalculateDepthNormal(pSrt, sspos, /*useOpaquePlusAlpha*/ true);

		if (GetStartedAtHead(state))
		{
			// we have a packed normal
			uint uintNorm = GetUintNorm(state);
			float3 unpackedNormal = UnpackUintNorm(uintNorm);
			//if (dot(unpackedNormal, depthNormal) > 0)
			//	inst.color = float4(0, 1, 0, 1);
			//else
			//	inst.color = float4(1, 0, 0, 1);
			depthNormal = unpackedNormal;
		}

		inst.world = TransformFromLookAtRibbon(depthNormal /*state.m_rotation.xyz*/, toNextParticle, pos, true); // this is totally broken because of tracking bind pose position in m_rotation

		// for crawlers: use this for aligning the sprite with velocity direction
		//inst.world = TransformFromLookAtYFw(state.m_scale.xyz, state.m_rotation.xyz, pos, true);

		float3 scale = float3(pSrt->m_rootComputeVec0.xyz) * 0.5;

		inst.userf.x = bloodAmount * 0.5f;

		if (bloodAmount > 0.01f)
		{
			//	scale.x *= 10;
		}

		if (lastInSequence || (bitsBefore == 0))
		{
			//scale.x = 0.01f;
		}
		//scale.x = max(scale.x * 0.5, scale.x + bitsBefore * -0.002);

		inst.world[0].xyz = inst.world[0].xyz * scale.x;
		inst.world[1].xyz = inst.world[1].xyz * scale.y;
		inst.world[2].xyz = inst.world[2].xyz * scale.z;

		inst.invscale = float4(1.0f / (scale.x * 0.5), 1.0f / (scale.y * 0.5), 1.0f / (scale.z * 0.5), 1.0f / 1);

		float3 renderOffset = pSrt->m_renderOffset.xyz;

		//renderOffset.x = -0.25f;
		//renderOffset.z = 0.5f;
		// in this case y axis of particle is up along the normal

		renderOffset = float3(0, 0.2, 0); // but offsetting along the normal doesnt seem to produce good results, try moving it towards a camera a little

		renderOffset = float3(0, 0, 0);

		float4 posOffest = mul(float4(renderOffset, 1), inst.world);

		// modify position based on camera
		inst.world[3].xyz = posOffest.xyz - pSrt->m_altWorldOrigin.xyz + toCam * 0.005;

		//partMat.SetRow(3, MulPointMatrix(offset.GetVec4(), partMat));

		inst.prevWorld = inst.world;		// Last frame's object-to-world matrix

		if (lastInSequence)
		{
			// we know next particle is bad or doesnt exist. this means distance to next was predicted by using distance from previous particle to current
			inst.color = float4(1, 0, 1, 1); // magenta

		}
		else if (distToNext > allowedDistance)
		{
			inst.color = float4(0, 1, 1, 1); // cyan

			lastInSequence = true;
		}

		float blendTime = RIBBON_ROOT_VAR_LIFETIME * 0.7; // last 70% of lifetime is blending out

		float lifeTime = f16tof32(asuint(state.m_lifeTime));

		//if (!isDynamicTrail)
		{
			inst.color.a = saturate((lifeTime - age) / blendTime);

			inst.userh.w = saturate(((age) / lifeTime) * (1.0 / 0.2)); // 0 -> 1 over  0.5 lifetime

		}


		inst.userf.y = saturate(age / lifeTime); // 0 -> 1 over life
		bool skin = state.m_flags1 & kMaterialMaskSkin;
		bool skinOrHair = state.m_flags1 & (kMaterialMaskSkin | kMaterialMaskHair);
		bool isHair = state.m_flags1 & (kMaterialMaskHair);
		inst.color.r = skin ? bloodAmount : bloodAmount * 0.25; // controls blood blend

		if (!skinOrHair)
		{
			//inst.color.a *= 0.5f;
		}

		if (isHair)
		{
			inst.color.a *= 0.0f;
		}


		//particleBrithIndex % 4

		if (lastInSequence || (bitsBefore == 0))
		{
			inst.color.a = 0.0f;
		}

		//inst.color = float4(1, 1, 1, 1.0f);


		//inst.color.a = 0;
		if (!isDummyParticle)
		{
			pSrt->m_particleInstances[destinationInstanceIndex] = inst;
		}


		//bool nextIsBad = IsBadRibbonState(nextState); // IMPORTANT: this test has to match the test of isBade above entering this if statement, because we have to guarantee next thread enters this if statement
													  // it at least has to be as strict. so we can still set it to true, but we can't have it false while previous thread evaluated it to true

		////nextIsBad = nextIsBad || (distToNext > allowedDistance) || !depthTestOk; // we could also store this as result in state flags


		//if (groupThreadId.x < numQuads && !nextIsBad && (renderState != kRenderStateInvisible))
		if (!lastInSequence && groupThreadId.x < 63 && pSrt->m_rootComputeVec2.z >= 0) // CRASH_DEBUG_TODO: remove check for 63
		{
			numRenderIndicesToWrite = __s_bcnt1_i32_b64(__s_read_exec()); // how many indices whole group wants to write. scalar value, but can diverge since not all threads are here


			// note we don't add renerable index for last element because it only server for lookup not rendering
			int renderIndex = NdAtomicIncrement(gdsOffsetNew); // and this counter is the one used for dispatching rendering
#if RIBBONS_WRITE_TEMP_INDICES
			pSrt->m_particleTempIndices[renderIndex] = destinationInstanceIndex;
			// we will use this data to sort into real renderable buffer
			pSrt->m_particleTempSortData[groupId.x].m_offset = ReadFirstLane(renderIndex);
			//pSrt->m_particleTempSortData[groupId.x].m_numInstances = numRenderIndicesToWrite;
#else
			pSrt->m_particleIndices[renderIndex] = destinationInstanceIndex;
#endif
			renderIndexToWrite = destinationInstanceIndex; // index that this thread wants to write
		}
	}

	// back to 64 threads here
	ulong succeessmask = __v_cmp_ne_u32(renderIndexToWrite, -1);
	uint goodThread = __s_ff1_i32_b64(succeessmask);

	// make sure each group writes a 0 if not rendering
#if RIBBONS_WRITE_TEMP_INDICES
	pSrt->m_particleTempSortData[groupId.x].m_numInstances = ReadLane(numRenderIndicesToWrite, goodThread);
#endif
}


