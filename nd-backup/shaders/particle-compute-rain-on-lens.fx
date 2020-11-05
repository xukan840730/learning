#include "packing.fxi"
#include "global-funcs.fxi"
#include "post-globals.fxi"
#include "global-const-buffers.fxi"
#include "tile-util.fxi"
#include "atomics.fxi"
#include "particle-cs.fxi"
#include "quat.fxi"

#if 1
	// get controls from rootvars 
	#define ParticleSpawnRate pSrt->m_rootComputeVec0.x
	#define ParticleLifeSpanBase pSrt->m_rootComputeVec0.y
	#define ParticleLifeSpanRandom pSrt->m_rootComputeVec0.z

	#define ParticleSpriteScaleXYZ pSrt->m_rootComputeVec1.x
	#define ParticleRandomScale pSrt->m_rootComputeVec1.y

	#define ParticleOpacityFadeInDuration pSrt->m_rootComputeVec2.x
	#define ParticleOpacityFadeOutDuration pSrt->m_rootComputeVec2.y
	#define ParticleDistortionSpeed pSrt->m_rootComputeVec2.z
	
#else
	#define ParticleSpawnRate 4
	#define ParticleLifeSpanBase 1.0
	#define ParticleLifeSpanRandom 0.5
	#define ParticleSpriteScaleXYZ 0.1
	#define ParticleRandomScale 0.05
	#define ParticleOpacityFadeInDuration 0.2
	#define ParticleOpacityFadeOutDuration 3.0
#endif



[NUM_THREADS(64, 1, 1)]
void CS_ParticleLensRainUpdate(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// grab my particle

	if (bool(pSrt->m_isCameraUnderwater))
	{
		return;
	}

	uint gdsOffsetOld = pSrt->m_gdsOffsetCounterOld;
	uint numOldParticles = NdAtomicGetValue(gdsOffsetOld);

	if (dispatchId.x >= numOldParticles)
		return; // todo: dispatch only the number of threads = number of particles

	// cast buffer to GenericParticleState array for clearer reading
	StructuredBuffer<GenericParticleState> castedBufferOld = __create_buffer< StructuredBuffer<GenericParticleState> >(__get_vsharp(pSrt->m_particleStatesOld));
	GenericParticleState state = castedBufferOld[dispatchId.x];

	float age = (pSrt->m_time - state.m_birthTime);
	if (age > state.m_lifeSpan) // Has lived its lifespan. No need to add it to the buffer of living particles.
	{
		return;
	}

	// Do StructuredBuffer
	
	float camUpAbs = abs(pSrt->m_cameraDirWs.y) - 1 ;
	
	float randValue1 = f16tof32(state.m_flags1);
	float randValue2 = f16tof32(state.m_flags1 >> 16);

	float moveMe = ( randValue1 > 0.7 )? 1 : 0;
	float speedFlip = ( randValue2 > 0.5 )? 0 : 1;

	float moveTime = (age < 3)? 0 : 1;

	float linIn = LinStep( state.m_lifeSpan, state.m_lifeSpan - 6, age);
	float linOut = LinStep(state.m_lifeSpan - 6, state.m_lifeSpan, age);
	float lin = (speedFlip > 0.5)? linIn : linOut;

	float moveSpeedY = (((camUpAbs * 0.017) * lin ) * moveTime) * moveMe; 
	float moveSpeedX = ((((camUpAbs * 0.005) * lin ) * ( cos( age * 5 ))) * moveMe) * moveTime;
		
	state.m_pos += (float3( moveSpeedX, moveSpeedY, 0) * pSrt->m_delta);

	state.m_rotation += (moveSpeedY * 7) * ((randValue1 * 0.5) + 0.5);

	// write the state out to new buffer
	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint particleIndex = NdAtomicIncrement(gdsOffsetNew);
		
	// Cast to internal struct
	RWStructuredBuffer<GenericParticleState> castedBuffer = __create_buffer< RWStructuredBuffer<GenericParticleState> >(__get_vsharp(pSrt->m_particleStates));
	castedBuffer[particleIndex] = state;
}











[NUM_THREADS(64, 1, 1)]
void CS_ParticleLensRainStaticUpdate(const uint2 dispatchId : SV_DispatchThreadID,
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
void CS_ParticleLensRainSpawn(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	//float rate = ParticleSpawnRate;

	//float camWindDot =  saturate(dot(pSrt->m_rainDir.xyz, pSrt->m_cameraDirWs.xyz));
	//float camWind = saturate(camWindDot + 0.9);
	//float camUp = saturate((pSrt->m_rainDir.y) + 0.9);

	if (groupThreadId.x == 0)
	{
		//_SCE_BREAK();
	}

	float camWindDot =  (dot(pSrt->m_rainDir.xyz, pSrt->m_cameraDirWs.xyz));
	float camWind = camWindDot + 1.5;
	float camUp = pSrt->m_cameraDirWs.y + 1;
	float camDirRate = camUp * camWind;

	float rate = ParticleSpawnRate * camUp ;

	int numPartsToSpawnThisFrame;
	{
		float interval = 1/rate;
		float modTime = pSrt->m_time % interval;
		float modTimeMinusDelta = modTime - pSrt->m_delta;
		numPartsToSpawnThisFrame = (modTimeMinusDelta < 0) ? 1 + floor(abs(modTimeMinusDelta)/interval) : 0;
	}


	float4 samplePos = float4(pSrt->m_cameraPosWs.xyz + pSrt->m_cameraDirWs.xyz * 2, 1.0);



	float3 rainPos;
	rainPos.x = dot(pSrt->m_pCommonParticleComputeSrt->m_occlusionParameters[0], samplePos);
	rainPos.y = dot(pSrt->m_pCommonParticleComputeSrt->m_occlusionParameters[1], samplePos);
	rainPos.z = dot(pSrt->m_pCommonParticleComputeSrt->m_occlusionParameters[2], samplePos);
	float occlusion = pSrt->m_pPassDataSrt->m_rainOccluders.SampleCmp(pSrt->m_pCommonParticleComputeSrt->m_shadowSampler, rainPos.xy, rainPos.z);
	//int occlusion = (pSrt->m_pPassDataSrt->m_rainOccluders.Sample(pSrt->m_linearSampler, rainPos.xy).r < rainPos.z) ? 0 : 1;

	numPartsToSpawnThisFrame *= occlusion;


	if (dispatchId >= numPartsToSpawnThisFrame)
	{
		return;
	}

	float4 rands;
	rands.x = GetRandom(pSrt->m_gameTicks, dispatchId.x*(1+groupThreadId), 1);
	rands.y = GetRandom(pSrt->m_gameTicks, dispatchId.x*(2+groupThreadId), 2);
	rands.z = GetRandom(pSrt->m_gameTicks, dispatchId.x*(3+groupThreadId), 3);
	rands.w = GetRandom(pSrt->m_gameTicks, dispatchId.x*(4+groupThreadId), 4);

	float3 position = float3(0,0,0);
	float3 velocity = float3(0,0,0);

	// random position
	position.x = (rands.x - 0.5);// * 1.9) * 0.15; //aspect ratio
	position.y = (rands.y - 0.5);// * 0.15 ;
	position.z = 0.15;

	float2 dirx = normalize(position.xy);
	position.xy += dirx * (1 - LinStep(0, .36, length(position.xy))) * 0.36;

	position.xy *= float2(1.9, 1.0) * 0.15;

	float lifeSpan = ParticleLifeSpanBase;
	lifeSpan += (GetRandom(pSrt->m_gameTicks, dispatchId.x, 6) - 0.5) * ParticleLifeSpanRandom;

	float birthOffset = GetRandom(pSrt->m_gameTicks, dispatchId.x*(2+groupThreadId), 14);
	float birthTimeOffset = pSrt->m_delta * birthOffset;

	uint randValues = PackFloat2ToUInt(GetRandom(pSrt->m_gameTicks, dispatchId.x, 7), GetRandom(pSrt->m_gameTicks, dispatchId.x, 8));

	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint size, stride;
	pSrt->m_particleStates.GetDimensions(size, stride);

	//Increment the index so we can write a new particle. Then see if we've actually tried to create one too many particles.
	uint particleIndex;
	particleIndex = NdAtomicIncrement(gdsOffsetNew); 
	if (particleIndex >= size)
	{
		// decrement back
		NdAtomicDecrement(gdsOffsetNew);
		return; // can't add new particles
	}

	ParticleStateV0 state = ParticleStateV0(0);
	state.m_birthTime = pSrt->m_time + birthTimeOffset;
	state.m_flags1 = randValues;
	state.m_pos = position;
	state.m_scale = velocity;
	state.m_id = 0;
	state.m_speed = 1;
	state.m_lifeTime = lifeSpan;
	state.m_rotation = float3(0,0,1);
	state.m_data = 1;

	pSrt->m_particleStates[particleIndex] = state; //Here's where the new particle's state is added to the state buffer.

}










[NUM_THREADS(64, 1, 1)]
void CS_ParticleLensRainGenerateRenderables(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	if (bool(pSrt->m_isCameraUnderwater))
	{
		return;
	}
	
	// grab my particle
	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint numParticles = NdAtomicGetValue(gdsOffsetNew);

	if (dispatchId.x >= numParticles)// || dispatchId.x > 30)
		return; // todo: dispatch only the number of threads = number fo particles

	ParticleStateV0 state = pSrt->m_particleStates[dispatchId.x];
	float3 pos = state.m_pos;

	uint destinationIndex = dispatchId.x;

	ParticleInstance inst = ParticleInstance(0);

	inst.color = float4(1.0, 1.0, 1.0, 1.0);			// The particle's color
	inst.texcoord = float4(1, 1, 0, 0);					// Texture coordinate scale and bias (uv = uv0 * texcoord.xy + texcoord.zw)
	inst.userh = float4(0.0, 0.0, 0.0, 0.0);			// User attributes (used to be half data type)
	inst.userf = float4(0.0, 0.0, 0.0, 0.0);			// User attributes
	inst.partvars = float4(0, 0, 0, 0);					// Contains age, normalized age, ribbon distance, and frame number
	inst.invscale = float4(1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f);		// Reciprocal of the particle's half-scale vector


	// Transform
	float4x4 orientation;

	orientation[0] = float4(1,0,0,0);
	orientation[1] = float4(0,1,0,0);
	orientation[2] = float4(0,0,1,0);
	orientation[3] = float4(pos,1);

	float randValue1 = f16tof32(state.m_flags1);
	float randValue2 = f16tof32(state.m_flags1 >> 16);
	float age = (pSrt->m_time - state.m_birthTime);

	// "sprite twist"

	float coss = cos(randValue1 * 360);
	float sinn = sin(randValue1 * 360);

	orientation[0] = float4(coss, sinn, 0, 0);
	orientation[1] = float4(-sinn, coss, 0, 0);
	

	inst.world = orientation;


	inst.userh.x =  randValue1 * 3; // userh 1: index


	inst.userh.y = state.m_rotation; // userh 2: time

#if 0 
	// setup for user vars
	inst.userh.x = age; // userh 1: age
	inst.userh.y = age * 30.0; // userh 2: index

	inst.partvars.x = age;
	inst.partvars.y = age / state.m_lifeTime;

	if (0) // random offset to flipbook index
	{
		inst.userh.y += randValue2*kMaxParticleCount;
	}
	if (0) // random channel pick
	{	
		inst.userh.z = (int((randValue1 + randValue2) * 0.5 * kMaxParticleCount)) % 3; // userh 3: channel pick
	}	
#endif

	//float opacity = (1 * randValue1) + 0.1 ;
	float opacity = 1 ;
	// Opacity Fade
	opacity *= LinStep(0, ParticleOpacityFadeInDuration, age);
	opacity *= 1 - LinStep(state.m_lifeTime - ParticleOpacityFadeOutDuration, state.m_lifeTime, age);

	inst.color.a = opacity;

	inst.color *= pSrt->m_rootColor;

	uint origParticleIndex = pSrt->m_particleIndicesOrig[0];
	ParticleInstance originalParticle = pSrt->m_particleInstancesOrig[origParticleIndex];
	
	inst.color *= originalParticle.color;
	
	// Scale
	float3 scale = ParticleSpriteScaleXYZ * 0.5;
	scale -= (randValue1-0.5) * ParticleRandomScale;

	scale *= pSrt->m_rootSpriteScale.xyz;

	inst.world[0].xyz = inst.world[0].xyz * scale.x;
	inst.world[1].xyz = inst.world[1].xyz * scale.y;
	inst.world[2].xyz = inst.world[2].xyz * scale.z;

	inst.invscale = float4(1.0f / (scale.x * 0.5), 1.0f / (scale.y * 0.5), 1.0f / (scale.z * 0.5), 1.0f / 1);

	float3 renderOffset = pSrt->m_renderOffset.xyz;
	float4 posOffest = mul(float4(renderOffset, 1), inst.world);

	// modify position based on camera
	inst.world[3].xyz = posOffest.xyz - pSrt->m_altWorldOrigin.xyz;

	inst.prevWorld = inst.world;		// Last frame's object-to-world matrix
	inst.prevWorld[3].xyz -= state.m_scale.xyz * pSrt->m_delta;

	pSrt->m_particleInstances[destinationIndex] = inst;
	pSrt->m_particleIndices[destinationIndex] = destinationIndex;
}