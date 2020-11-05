#include "packing.fxi"
#include "global-funcs.fxi"
#include "post-globals.fxi"
#include "global-const-buffers.fxi"
#include "tile-util.fxi"
#include "atomics.fxi"
#include "particle-cs.fxi"
#include "quat.fxi"

#define kPi_TIMES_2 6.28318530718;
#define kPi_DIV_2 1.5707963226;

#define kParticleFlags0Frozen (1 << 0)
#define kParticleFlags0WaterStick (1 << 1)
#define kParticleFlags0CollisionCorrected (1 << 2)
#define kParticleFlags0Kill (1 << 3)
#define kParticleFlags0IsFG (1 << 4)
#define kParticleFlags0Active (1 << 5)

#define kCollisionDepthTolerance 0.3
#define kCollisionFreezeTolerance 0.2
#define kCollisionUnFreezeTolerance 0.1

#define ParticleSpriteScaleXYZ pSrt->m_pComputeCustomData->m_vec0.xyz
#define ParticleRandomScale pSrt->m_pComputeCustomData->m_vec0.w

#define ParticleLifeSpanBase pSrt->m_pComputeCustomData->m_vec1.x
#define ParticleLifeSpanRandom pSrt->m_pComputeCustomData->m_vec1.y
#define ParticleBounceEntropy pSrt->m_pComputeCustomData->m_vec1.z
#define ParticleSlideDrag pSrt->m_pComputeCustomData->m_vec1.w

#define ParticleFlipbookFramerate pSrt->m_pComputeCustomData->m_vec2.x
#define ParticleOpacityFadeInDuration pSrt->m_pComputeCustomData->m_vec2.y
#define ParticleOpacityFadeOutDuration pSrt->m_pComputeCustomData->m_vec2.z
#define ParticleCollisionVariation pSrt->m_pComputeCustomData->m_vec2.w

#define ParticleDistanceFadeInStart pSrt->m_pComputeCustomData->m_vec3.x
#define ParticleDistanceFadeInEnd pSrt->m_pComputeCustomData->m_vec3.y
#define ParticleDistanceFadeOutStart pSrt->m_pComputeCustomData->m_vec3.z
#define ParticleDistanceFadeOutEnd pSrt->m_pComputeCustomData->m_vec3.w

#define HintEmitterDistanceFadeOutStart pSrt->m_pComputeCustomData->m_vec4.x
#define HintEmitterDistanceFadeOutEnd pSrt->m_pComputeCustomData->m_vec4.y
#define ParticleInheritVelocityMult pSrt->m_pComputeCustomData->m_vec4.z
#define ParticleInheritVelocityVariation pSrt->m_pComputeCustomData->m_vec4.w

#define ParticleDistScaleMin pSrt->m_pComputeCustomData->m_vec5.x
#define ParticleDistScaleMax pSrt->m_pComputeCustomData->m_vec5.y
#define ParticleVelocityStretch pSrt->m_pComputeCustomData->m_vec5.z
#define ParticleAccelerationBend pSrt->m_pComputeCustomData->m_vec5.w

#define ParticleTint pSrt->m_pComputeCustomData->m_vec6.xyz
#define ParticleCollisionBufferChance pSrt->m_pComputeCustomData->m_vec6.w

#define ParticleScaleFadeInDuration pSrt->m_pComputeCustomData->m_vec7.x
#define ParticleScaleFadeOutDuration pSrt->m_pComputeCustomData->m_vec7.y

#define ParticleOrientAtAxis  pSrt->m_pComputeCustomData->m_vec7.z
#define ParticleOrientAlignAxis  pSrt->m_pComputeCustomData->m_vec7.w


#define _Lower(x) f16tof32(x)
#define _Upper(x) f16tof32(x >> 16)
#define _Flower(x) f16tof32(asuint(x))
#define _Fupper(x) f16tof32(asuint(x) >> 16)

SamplerState SetWrapSampleMode(SamplerState inSampler)
{
	uint4 ssharp = __get_ssharp(inSampler);
	ssharp.x &= 0xfffffe00; // turn off 9 first bits to make xyz wrap mode
	return __create_sampler_state(ssharp);
}

void ForceGravity(FieldState fs, inout float3 velocity,  const float timeIncrement)
{
	float g = fs.magnitude * timeIncrement;
	velocity += float3(0,-g,0);
}

void ForceDrag(FieldState fs, inout float3 velocity,  const float timeIncrement)
{
	float magnitude = fs.magnitude * timeIncrement; //replace with magnitudefrom particle
	float3 dragForce = velocity * -1 * magnitude;
	velocity += dragForce;
}

void ForceAir(FieldState fs, inout float3 velocity, const float timeIncrement)
{
	float alpha = fs.speed * timeIncrement;
	velocity *= saturate(1 - alpha);
	velocity += fs.direction.xyz * alpha * fs.magnitude;
}

void ForceRadial(FieldState fs, inout float3 velocity, const float3 position, const float timeIncrement)
{
	float3 direction = normalize(position - fs.fieldPos.xyz);
	velocity += direction * fs.magnitude * timeIncrement;
} 

void ForceTurbulence(ParticleComputeJobSrt *pSrt, FieldState fs, inout float3 velocity, const float3 position, const float timeIncrement)
{

	float3 samplePos = (position * fs.freq) + fs.phase.xyz;

	float3 direction = Tex2DAs3D(pSrt->m_materialTexture0).SampleLevel(SetWrapSampleMode(pSrt->m_linearSampler), samplePos.xyz, 0).xyz;	

	float3 noiseBias = float3(-0.225,-0.235,-0.225); //removal of average bias in each axis

	direction += noiseBias;

	velocity += direction * 2 * fs.magnitude * timeIncrement;
}

void ForceVolumeAxis(FieldState fs, inout float3 velocity, const float3 position, const float timeIncrement)
{
	float3 axis = fs.axis.xyz;
	float3 acVec = normalize(position - fs.fieldPos.xyz);
	float3 aaVec = float3(acVec.x, 0.0, acVec.z );
	float3 araVec = cross(axis, acVec);

	float3 force = fs.direction.xyz * fs.dirSpeed;
	force += acVec * fs.awayCenter;
	force += aaVec * fs.awayAxis;
	force += axis * fs.alongAxis;
	force += araVec * fs.aroundAxis;

	velocity += force * fs.magnitude * timeIncrement;
}

Setup SampleGBuffer(ParticleComputeJobSrt *pSrt, uint2 screenPos)
{
	const uint4 sample0 = pSrt->m_pPassDataSrt->m_gbuffer0[screenPos];
	const uint4 sample1 = pSrt->m_pPassDataSrt->m_gbuffer1[screenPos];

	BrdfParameters brdfParameters = (BrdfParameters)0;
	Setup setup = (Setup)0;
	UnpackGBuffer(sample0, sample1, brdfParameters, setup);

	return setup;
}

uint2 NdcToScreenSpacePos(ParticleComputeJobSrt *pSrt, float2 ndc)
{
	uint2 sspos = uint2(floor(float2((ndc.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, (1.0f - (ndc.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y)));

	return sspos;
}

float GetLinearDepthError(float z, float2 params)
{
	float lin0 = GetLinearDepth(z, params);

	// flip last matissa bit 
	float z1 = asfloat(asuint(z) ^ 0x00000001);

	float lin1 = GetLinearDepth(z1, params);

	return abs(lin0 - lin1);
}

bool trackingErrorCheck(inout GenericParticleState state)
{
	state.m_flags2 += 1;
	if (state.m_flags2 > 3)
	{
		state.m_flags2 = 0;
		state.m_flags0 &= ~kParticleFlags0Frozen;
		state.m_flags0 &= ~kParticleFlags0WaterStick;
		state.m_flags0 &= ~kParticleFlags0IsFG;

		state.m_velocity = 0;;
		return false;
	}

	return true;
}

bool isOutsideNDC(float3 posNDC)
{	
	return (abs(posNDC.x) > 1 || abs(posNDC.y) > 1);
}

bool errorCheck(inout GenericParticleState state)
{
	state.m_flags2 += 1;
	bool kill = false;
	if (state.m_flags2 > 4)
	{
		// kill, depth diff is too big
		state.m_flags0 = state.m_flags0 | kParticleFlags0Kill;
		kill = true;
	}
	return kill;
}
void AddCollisionToBuffer( GenericParticleState state, ParticleComputeJobSrt *pSrt, uint dispatchId)
{
	uint gdsOffsetNew = pSrt->m_gdsOffsetOther;

	if (gdsOffsetNew == 0)
	{
		// buffer not provided
		return;
	}

	uint size, stride;
	pSrt->m_particleStatesOther.GetDimensions(size, stride);

	uint newInfoIndex = NdAtomicIncrement(gdsOffsetNew);
	if (newInfoIndex >= size)
	{
		// decrement back
		NdAtomicDecrement(gdsOffsetNew);
		return; // can't add new information
	}

	RWStructuredBuffer<GenericParticleHint> destHintBuffer = __create_buffer<RWStructuredBuffer<GenericParticleHint> >(__get_vsharp(pSrt->m_particleStatesOther));
	GenericParticleHint infoState = GenericParticleHint(0);

	infoState.m_pos = state.m_pos;
	infoState.m_velocity = state.m_velocity;
	infoState.m_alpha = _Fupper(state.m_lifeSpan);

	destHintBuffer[newInfoIndex] = infoState;

}

void ApplyForces(inout GenericParticleState state, ParticleComputeJobSrt *pSrt, float timestep)
{
	if (state.m_flags0 & kParticleFlags0Frozen)
	{
		return;
	}
	// TODO: Transform field state into world space via root transform (Emitters have same issue)
	float3 prevVel = state.m_velocity;

	for (int fid = 0; fid < pSrt->m_numFields; ++fid)
	{
		FieldState fs = pSrt->m_fieldStates[fid];
		uint nodeId = fs.nodeId_fieldClass & 0x0000FFFF;
		float globalForceStrength = 1.0;
		switch(nodeId)
		{
			case kDragField:
				ForceDrag(fs, state.m_velocity, globalForceStrength * timestep);
				break;
			case kGravityField:
				ForceGravity(fs, state.m_velocity, globalForceStrength * timestep);
				break;
			case kAirField:
				ForceAir(fs, state.m_velocity, globalForceStrength * timestep);
				break;
			case kRadialField:
				ForceRadial(fs, state.m_velocity, state.m_pos, globalForceStrength * timestep);
				break;
			case kVolumeAxisField:
				ForceVolumeAxis(fs, state.m_velocity, state.m_pos, globalForceStrength * timestep);
			case kTurbulenceField:
				ForceTurbulence(pSrt, fs, state.m_velocity, state.m_pos, globalForceStrength * timestep);
				break;
		}
	}

	if(abs(ParticleAccelerationBend) > 0)
	{
		float3 accel = state.m_velocity - prevVel;
		float bend = dot(cross(normalize(state.m_velocity+kEpsilon), pSrt->m_cameraDirWs.xyz), accel) * ParticleAccelerationBend;
		state.m_data = PackFloat2ToUInt( _Lower(state.m_data), bend);
	}
}

bool isActive(GenericParticleState state)
{
	return (state.m_flags0 & kParticleFlags0Active);
}

struct CrawlerEventInfo
{
	bool validEvent;
	float3 eventPos;
	float dist;
	float3 dir;
	float influence;
};

CrawlerEventInfo GatherCrawlerEventInfo(GenericParticleState state, ParticleComputeJobSrt *pSrt)
{
	CrawlerEventInfo event = CrawlerEventInfo(0);

	if (pSrt->m_gdsOffsetOther == 0)
	{
		return event;
	}

	uint gdsOffsetOther = pSrt->m_gdsOffsetOther;
	uint numEmitterHints = NdAtomicGetValue(gdsOffsetOther);
	if (numEmitterHints < 1)
	{	
		return event;
	}

	event.validEvent = true;
	event.eventPos = pSrt->m_particleStatesOther[0].m_pos;

	float3 posCS = state.m_pos - event.eventPos;
	event.dist = length(posCS);
	event.dir = normalize(posCS);
	event.influence = 1-LinStep(1.5,3, event.dist);

	event.validEvent = event.influence > kEpsilon;

	return event;
}

void CrawlerUpdate(inout GenericParticleState state, ParticleComputeJobSrt *pSrt)
{

	float3 velocityStep = state.m_velocity * pSrt->m_delta;
	DepthPosInfo current = DPISetup(state.m_pos + velocityStep, pSrt);

	bool offScreen = isOutsideNDC(current.posNDC) || current.posH.w < 0;
	bool hasError = false;

	if (offScreen)
	{
		if (isActive(state))
		{
			hasError = true;
		}
		else
		{
			state.m_birthTime = pSrt->m_time;
			return;
		}
	}

	DPISampleDepth( current , pSrt, false);

	float3 posCS = state.m_pos - pSrt->m_cameraPosWs.xyz;
	float3 cameraForceDir = normalize(posCS);
	float cameraForceMag = 1 - saturate((length(posCS) - 1.5)/3.0);

	bool cameraPush = (cameraForceMag > 0.1);

	float activeRange = 1-saturate(length(current.posNDC.xy)*2.25);
	activeRange *= 1 - saturate(current.posH.w - 14);
	activeRange *= bool(pSrt->m_isPlayerFlashlightOn);
	activeRange *= (current.depthDiff - kCollisionFreezeTolerance <= 0);

	if (activeRange > 0.1 && !hasError)
	{
		state.m_flags0 = state.m_flags0 | kParticleFlags0Active;
	}

	CrawlerEventInfo eventInfo = GatherCrawlerEventInfo(state, pSrt);

	if (eventInfo.validEvent)
	{
		state.m_flags0 = state.m_flags0 | kParticleFlags0Active;
	}

	bool active = isActive(state);

	if (!active)
	{
		state.m_birthTime = pSrt->m_time;
	}

	if (current.depthDiff < (-1 * kCollisionFreezeTolerance))
	{		
		// hovering in front of depth pos
		hasError = true;
	}

	if (current.depthDiff > kCollisionFreezeTolerance)
	{
		if (active || cameraPush)
		{
			hasError = true;
		}
		else
		{
			return;
		}
	}

	if (!active && hasError && !cameraPush)
	{
		// inactive and invalid, do nothing
		return;
	}

	float timestep = pSrt->m_delta;

	if (hasError)
	{
		// active but invalid, keep moving,hoping to get fixed
		state.m_flags2 += 1;
		state.m_flags0 = (state.m_flags2 > 4) ? state.m_flags0 = state.m_flags0 | kParticleFlags0Kill : state.m_flags0;
		state.m_pos += state.m_velocity * timestep;
		return;
	}


	// random occasional motion of inactive crawlers
	float randValue2 = _Upper(state.m_flags1);
	bool stochasticMotion = (fmod(pSrt->m_time, (randValue2 * 6)) < 0.2)? 1 : 0;
	stochasticMotion = stochasticMotion && (_Lower(state.m_flags1) > 0.5);

	if (active || stochasticMotion || cameraForceMag > kEpsilon)
	{

		if (eventInfo.validEvent)
		{
			state.m_velocity = lerp(state.m_velocity, eventInfo.dir, eventInfo.influence*randValue2*randValue2);
		}

		state.m_flags2 = 0;
		DPIGetDepthPos( current, pSrt);		
		state.m_pos = current.depthPosWS;
		//Setup setup = SampleGBuffer(pSrt, current.posSS);
		float3 depthNormalWS = CalculateDepthNormal(pSrt, current.posSS, true);

		float3 surfaceTangent = cross(depthNormalWS, normalize(state.m_velocity));
		float3 direction = -1 * cross(depthNormalWS, surfaceTangent);

		float speed = length(state.m_velocity);
		if (speed > kEpsilon)
		{
			state.m_velocity = direction * speed;
		}

		state.m_rotation = lerp(state.m_rotation, depthNormalWS, 4.0/30);

		state.m_pos += state.m_velocity * timestep;

		state.m_velocity += cameraForceDir * cameraForceMag * (randValue2*0.7 + 0.3) * timestep; 

		if(bool(pSrt->m_isPlayerFlashlightOn))
		{	
			DepthPosInfo center = DepthPosInfo(0);
			center.posNDC = float3(0,0,kEpsilon);
			center.posSS = uint2(floor(float2((center.posNDC.x / 2.0f + 0.5f) * pSrt->m_screenResolution.x, 
								(1.0f - (center.posNDC.y / 2.0f + 0.5f)) * pSrt->m_screenResolution.y)));		
			center.rawDepth = pSrt->m_pPassDataSrt->m_opaquePlusAlphaDepthTexture[center.posSS];
			DPIGetDepthPos(center, pSrt);

			float3 lightForce = normalize(state.m_pos - center.depthPosWS);
			lightForce *= 1 - saturate(length(state.m_pos - center.depthPosWS)-1)*0.9;
			state.m_velocity += lightForce * 1.0 * timestep; 
		}


		timestep *= active ? 1 : 0.1;

		ApplyForces(state, pSrt, timestep);	
	}

}

void GeneralScreenInteraction(inout GenericParticleState state, ParticleComputeJobSrt *pSrt, uint idx)
{
	if (!(pSrt->m_features & COMPUTE_FEATURE_COLLISION))
	{
		return;
	}
	state.m_flags0 &= ~kParticleFlags0CollisionCorrected;

	// collision checks against the projected position
	// frozen particles (maybe tracking) shouldnt be projected (they dont have a real velocity)
	float3 velocityStep = state.m_velocity * pSrt->m_delta;
	float3 offset = (state.m_flags0 & kParticleFlags0Frozen)? 0 : velocityStep;

	DepthPosInfo current = DPISetup(state.m_pos + offset, pSrt);

	// Ignore particles outside screen
	if (isOutsideNDC(current.posNDC) || current.posH.w < 0)
	{
		return;
	}

	uint stencil = pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil[current.posSS];

	if ((pSrt->m_features & COMPUTE_FEATURE_SPAWN_FILTER_ON_WATER) && (stencil & 0x02))
	{
		state.m_flags2 += 1;
		return;
	}

	bool notBg = (stencil & 0x20) || (stencil & 0x40) || (stencil & 0x080);
	if ((pSrt->m_features & COMPUTE_FEATURE_BG_ONLY) && notBg)
	{
		state.m_flags2 += 1;
		return;
	}


	DPISampleDepth( current , pSrt, false);
	
	if (state.m_flags0 & kParticleFlags0Frozen)
	{
		#if defined DO_MOTION_TRACKING
		if (pSrt->m_features & COMPUTE_FEATURE_SCREEN_MOTION_FORCE && (state.m_flags0 & kParticleFlags0IsFG))
		{	
			float depthBufferError = GetLinearDepthError(current.posNDC.z, pSrt->m_depthParams);
			float tolerance = depthBufferError + 0.1; // pad depth tolerance

			if(abs(current.depthDiff) > tolerance)
			{
				trackingErrorCheck(state);
				return;				
			}

			DPIGetDepthPos( current, pSrt);
			state.m_pos = current.depthPosWS;
			// project backwards along screen motion vector 
			float2 motionVectorSample = pSrt->m_pPassDataSrt->m_motionVector[current.posSS];			
			int2 motionVectorSS = int2(motionVectorSample * pSrt->m_screenResolution);
			int2 lastFrameSSpos = int2(float2(current.posSS)+motionVectorSS);

			float prevDepth = pSrt->m_pPassDataSrt->m_lastFrameOpaquePlusAlphaDepthTexture[lastFrameSSpos];
			float linearPrevDepth = GetLinearDepth(prevDepth, pSrt->m_depthParams);
			uint prevStencil = pSrt->m_pPassDataSrt->m_lastFrameOpaquePlusAlphaStencil[lastFrameSSpos];

			// if depth is too off or stencil changes, skip
			if (abs(current.screenDepth - linearPrevDepth ) > tolerance || prevStencil != stencil)
			{	
				trackingErrorCheck(state);
				return;				
			}

			float3 lastFramePosNdc;
			lastFramePosNdc.x = (current.depthPosNDC.x - pSrt->m_projectionJitterOffsets.x) + (motionVectorSample.x * 2) + pSrt->m_projectionJitterOffsets.z;
			lastFramePosNdc.y = (current.depthPosNDC.y - pSrt->m_projectionJitterOffsets.y) - (motionVectorSample.y * 2) + pSrt->m_projectionJitterOffsets.w;
			lastFramePosNdc.z = prevDepth;

			float4 lastFramePosH = mul(float4(lastFramePosNdc, 1), pSrt->m_pPassDataSrt->m_mLastFrameAltVPInv);
			float3 lastFramePosWs = lastFramePosH.xyz / lastFramePosH.w;
			lastFramePosWs += pSrt->m_lastFrameAltWorldOrigin.xyz;

			// correct position based on the difference between the past projections motion vectors and velocity 
			//if(0)
			{
				// project backwards along velocity vector
				DepthPosInfo pastProjection = DPISetup(state.m_pos - velocityStep, pSrt );

				if (pSrt->m_pPassDataSrt->m_lastFrameOpaquePlusAlphaStencil[pastProjection.posSS] != stencil)
	 			{
	 				trackingErrorCheck(state);
	 				return;
	 			}

	 			DPISampleDepthPrev( pastProjection, pSrt, true);

	 			// only adjust if depth diff is small
				if(abs(pastProjection.screenDepth - current.screenDepth) < tolerance)
				{ 
					DPIGetDepthPos( pastProjection, pSrt);

					float3 adjustedPos = state.m_pos + pastProjection.depthPosWS - lastFramePosWs;				
					DepthPosInfo adjusted = DPISetup(adjustedPos, pSrt);
					DPISampleDepth(adjusted, pSrt, false);
					// make sure the adjustment doesnt worsen the depth diff
					if ( abs(adjusted.depthDiff) < tolerance)
					{
						state.m_pos += pastProjection.depthPosWS - lastFramePosWs;				
					}
				}
				
 			}

 			// velocity equates to the estimated change in position based on motion vectors
			float3 delta = (current.depthPosWS - lastFramePosWs) / pSrt->m_delta;
			state.m_velocity = delta;
			state.m_flags2 = 0;
			//state.m_flags0 = state.m_flags0 | kParticleFlags0CollisionCorrected;
			//state.m_pos += delta;
			return;

		}
		else
		#endif
		if (current.depthDiff > kCollisionUnFreezeTolerance)
		{		

			// lets do nothing if its occluded

			/*
			// store occlusion frame count
			state.m_flags2 += 1;
			*/
			state.m_flags2 = 0;
			return;
		}
		else if(current.depthDiff < -1 * kCollisionDepthTolerance)
		{	
			//particle is floating, maybe unfreeze
			state.m_flags2 += 1;
		}
		else
		{
			state.m_flags2 = 0;
		}
	}
	// Ignore particles infront of depth && ignore depth not near the particle
	if(current.depthDiff < 0 || current.screenDepth < 0.001)
	{
		state.m_flags2 = 0;
		return;
	}

	Setup setup = SampleGBuffer(pSrt, current.posSS);

	// dont collide if facing the wrong way
	if(dot(state.m_velocity, setup.normalWS) > 0)
	{
		return;
	}

	// Check if current position isnt occluded
	DepthPosInfo preupdate = DPISetup(state.m_pos, pSrt);
	DPISampleDepth( preupdate, pSrt, false);

	if (preupdate.depthDiff > 0)
	{
		state.m_flags2 += 1;
		return;	
	}

	float speedStep = length(velocityStep);
	// find approximate contact point
	{	
		float searchDist = 0.5;
		float searchLength = 0.5;
		DepthPosInfo midpoint = DepthPosInfo(0);
		for (int i=0; i < 3; i++)
		{
			midpoint = DPISetup(state.m_pos + velocityStep * searchLength, pSrt);
			DPISampleDepth( midpoint, pSrt, false);
			searchDist *= (midpoint.depthDiff < 0)? 0.5 : -0.5;
			searchLength += searchDist;
		}
		// check if too far beneath surface
		if (abs(midpoint.depthDiff) > max(speedStep * 2, kCollisionDepthTolerance))
		{
			state.m_flags2 += 1;
			return;	
		}
		// assign approximate contact position
		current = midpoint;
	}

	if (pSrt->m_features & COMPUTE_FEATURE_COLLISION && !(state.m_flags0 & kParticleFlags0Frozen))
	{
		state.m_flags2 = 0;

		// apply depth position and disable position update
		DPIGetDepthPos( current, pSrt);		
		state.m_pos = current.depthPosWS;
		state.m_pos += setup.normalWS * 0.001;
		state.m_flags0 = state.m_flags0 | kParticleFlags0CollisionCorrected;
		float randValue1 = _Lower(state.m_flags1);

		if (pSrt->m_features & COMPUTE_FEATURE_HINTS_FROM_COLLISION && !(ParticleCollisionBufferChance > kEpsilon && randValue1 > ParticleCollisionBufferChance/100.0))
		{
			AddCollisionToBuffer(state, pSrt, idx);
		}

		if (pSrt->m_features & COMPUTE_KILL_ON_COLLISION)
		{
			state.m_flags0 = state.m_flags0 | kParticleFlags0Kill;
			return;
		}

		// Orient on collision
		if(pSrt->m_features & COMPUTE_FEATURE_VELOCITY_FACING || pSrt->m_features & COMPUTE_FEATURE_VELOCITY_FACING_2D)
		{
			state.m_rotation = lerp(state.m_rotation, setup.normalWS, 0.5);
		}

		float randValue2 = _Upper(state.m_flags1);

		// Sliding behavior
		if (pSrt->m_features & COMPUTE_FEATURE_SLIDE_ON_COLLISION)
		{
			float3 surfaceTangent = cross(setup.normalWS, normalize(state.m_velocity));
			float3 direction = -1 * cross(setup.normalWS, surfaceTangent);
			float speed = length(state.m_velocity);
			speed *= 1 - (max(0,(ParticleSlideDrag - (randValue2 * ParticleCollisionVariation))) * pSrt->m_delta);
			state.m_velocity = direction * speed;// * dot(direction,state.m_velocity);
		}
		else // Bounce Behavior
		{
			float3 nvel = dot(state.m_velocity, setup.normalWS) * setup.normalWS;
			float3 pvel = state.m_velocity - nvel;
			float strength = max(0, ParticleBounceEntropy - (randValue2 * ParticleCollisionVariation));
			float drag = max(0, ParticleBounceEntropy - (randValue1 * ParticleCollisionVariation));
			state.m_velocity = (pvel * drag) - nvel * strength;
		}
		// Freeze after small collision
		if(length(state.m_velocity) < kCollisionFreezeTolerance )
		{
			state.m_velocity = 0;
			state.m_flags0 = state.m_flags0 | kParticleFlags0Frozen;
			uint matMaskSample = pSrt->m_pPassDataSrt->m_materialMaskBuffer[current.posSS];

			#if defined DO_MOTION_TRACKING
				if(stencil & 0x20 || matMaskSample & (1 << 2) /*translucency*/)
				{
					state.m_flags0 = state.m_flags0 | kParticleFlags0IsFG;
				}
			#endif

			if(pSrt->m_features & COMPUTE_FEATURE_SURFACE_SNAP)
			{
				state.m_rotation = setup.normalWS * float3(1,1,-1);
			}
			return;
		}
	}
}



[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeGenericUpdate(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// grab my particle
	uint gdsOffsetOld = pSrt->m_gdsOffsetCounterOld;
	uint numOldParticles = NdAtomicGetValue(gdsOffsetOld);

	if (dispatchId.x >= numOldParticles)
		return; // todo: dispatch only the number of threads = number of particles

	StructuredBuffer<GenericParticleState> castedBufferOld = __create_buffer< StructuredBuffer<GenericParticleState> >(__get_vsharp(pSrt->m_particleStatesOld));
	GenericParticleState state = castedBufferOld[dispatchId.x];

	float age = (pSrt->m_time - state.m_birthTime);
	if (age > _Flower(state.m_lifeSpan)) // Has lived its lifespan. No need to add it to the buffer of living particles.
	{
		return;
	}

	float subFrameDelta = max(0, min(pSrt->m_delta, age - pSrt->m_delta));

	#if defined IS_CRAWLER
		CrawlerUpdate(state, pSrt);
	#else
		GeneralScreenInteraction(state, pSrt, dispatchId.x);

		if (state.m_flags2 > 4){
			state.m_flags0 = state.m_flags0 | kParticleFlags0Kill;
		}

		if ((pSrt->m_features & COMPUTE_FEATURE_VELOCITY_FACING || pSrt->m_features & COMPUTE_FEATURE_VELOCITY_FACING_2D) && !(state.m_flags0 & kParticleFlags0Frozen))
		{
			state.m_rotation = lerp( state.m_rotation, normalize(state.m_velocity + float3(0,kEpsilon,0)), subFrameDelta);
		}

	 	//Integrate particle positions
	 	if (!(state.m_flags0 & kParticleFlags0CollisionCorrected))
	 	{
			state.m_pos += state.m_velocity * subFrameDelta;
	 	}
		ApplyForces(state, pSrt, subFrameDelta);
	#endif

	if (state.m_flags0 & kParticleFlags0Kill)
	{
		return;
	}

	// write the state out to new buffer
	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint particleIndex = NdAtomicIncrement(gdsOffsetNew);
		
	// Cast to internal struct
	RWStructuredBuffer<GenericParticleState> castedBuffer = __create_buffer< RWStructuredBuffer<GenericParticleState> >(__get_vsharp(pSrt->m_particleStates));
	castedBuffer[particleIndex] = state;
}

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeGenericCopyState(const uint2 dispatchId : SV_DispatchThreadID,
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

bool NewGenericParticle(float3 posWs, float3 velocityWs, float3 upVector, ParticleComputeJobSrt *pSrt, uint2 dispatchId, 
	uint particleId, float birthTimeOffset, float spriteTwist, float lifetime, int randValues, float alpha)
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
	GenericParticleState state = GenericParticleState(0);
	state.m_birthTime = pSrt->m_time + birthTimeOffset;
	state.m_flags1 = randValues;
	state.m_pos = posWs;
	state.m_velocity = velocityWs;
	state.m_flags2 = 0;
	state.m_speed = 1;
	state.m_lifeSpan = asfloat(PackFloat2ToUInt(lifetime, alpha)); // 1

	state.m_rotation = SafeNormalize(upVector);// + float3(0,kEpsilon,0)));
	state.m_data = PackFloat2ToUInt(spriteTwist,0.0); // 0 or -0.5 means just spawned

	RWStructuredBuffer<GenericParticleState> castedBuffer = __create_buffer< RWStructuredBuffer<GenericParticleState> >(__get_vsharp(pSrt->m_particleStates));
	castedBuffer[particleIndex] = state; //Here's where the new particle's state is added to the state buffer.
	return true;
}

void PointEmitterSpawn(CompleteEmitterState cestate, const float4x4 emitterTransform, float4 rands, inout float3 position, inout float3 velocity)
{
	EmitterState estate = cestate.m_computeEmitterState;

	float3 positionLS = float3(0,0,0);

	float azimuth = rands.x * 6.28318530718;
	float elevation;

	uint mode = cestate.m_pPointEmitter->m_type;
	if (mode == kPointEmitterTypeOmni)
	{
		elevation = (rands.y * kPi ) - kPi_DIV_2;
	}
	else // Directional with spread
	{
		elevation = 1.5707963226 - rands.y * estate.spread * 1.5707963226;
	}

	float sina = sin(azimuth);
	float sine = sin(elevation);
	float cosa = cos(azimuth);
	float cose = cos(elevation);
	float4 spreadDir = float4(cosa*cose, sina*cose, sine, 0.0);
	velocity = mul(spreadDir,  TransformFromLookAt( SafeNormalize(estate.direction), float3(0, 1, 0), 0, true) ).xyz; 
	positionLS += velocity * (estate.minDist + rands.z * (estate.maxDist - estate.minDist));
	float speed = estate.speedBase + estate.speedRandom * (rands.w);
	velocity *= speed;
	

	velocity = mul(float4(velocity, 0.0), emitterTransform).xyz;
	position = mul(float4(positionLS, 1.0), emitterTransform).xyz; //Convert position from local space to world space	
}

void VolumeEmitterSpawn(CompleteEmitterState cestate, const float4x4 emitterTransform, float4 rands, inout float3 position, inout float3 velocity)
{

	EmitterState estate = cestate.m_computeEmitterState;
	// todo: implement spawning on surface
	// bool emitFromSurface = pi.edesc->m_flags & DC::kPartEffectFlagsEmitFromSurface;
	float3 positionLS = 0;
	float azimuth;
	float elevation;
	float sina, sine;
	float cosa, cose;
	float height;

	uint shape = cestate.m_pVolumeEmitter->m_shape;
	switch(shape)
	{
		case kVolumeEmitterShapeCube:
			positionLS = 2*float3(rands.x, rands.y, rands.z)-1; 
			break;
		case kVolumeEmitterShapeSphere:
			azimuth = rands.x * cestate.m_pVolumeEmitter->m_sweep;
			elevation = (rands.y * kPi) - kPi_DIV_2;
			sina = sin(azimuth);
			sine = sin(elevation);
			cosa = cos(azimuth);
			cose = cos(elevation);
			positionLS = float3(rands.z*cosa*cose, rands.z*sine, rands.z*sina*cose);
			break;
		case kVolumeEmitterShapeCylinder:
			azimuth = rands.x * cestate.m_pVolumeEmitter->m_sweep;
			positionLS = float3(rands.z * cos(azimuth),  2 * rands.y - 1, rands.z * sin(azimuth));
			break;
		case kVolumeEmitterShapeCone:
			azimuth = rands.x * cestate.m_pVolumeEmitter->m_sweep;
			positionLS = float3(rands.z * rands.y * cos(azimuth), rands.y, rands.z * rands.y * sin(azimuth));
			break;
	}

	positionLS *= estate.spawnScale * 1;


	velocity = float3(0,0,0);
	velocity += SafeNormalize(positionLS) * estate.awayCenterSpeed; //awayFromCenter
	velocity += float3(0,1,0) * estate.alongAxisSpeed; //alongAxis  

	velocity += SafeNormalize(estate.direction) * (estate.dirSpeed + (estate.randomDirSpeed * rands.w)); //directionalSpeed 

	velocity = mul(float4(velocity, 0.0), emitterTransform).xyz;
	position = mul(float4(positionLS, 1.0), emitterTransform).xyz; //Convert position from local space to world space	


}

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeGenericSpawn(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	if (pSrt->m_numEmitterStates == 0)
		return;

	int emitterId = int(dispatchId % pSrt->m_numEmitterStates);
	int emitterMode = int(pSrt->m_emitterStates[emitterId]->m_updateData.m_poolIdx_m_spawnMode & 0x0000FFFF);
	float rate = pSrt->m_emitterStates[emitterId]->m_computeEmitterState.spawnRate;

	
	float fixedTime = pSrt->m_time + 1.0; //make sure time > 0
	// Update spawnTime for burst emitters
	if (dispatchId < pSrt->m_numEmitterStates)
	{
		float hasSpawned = pSrt->m_dataBuffer[emitterId].m_data0.x;

		if (rate > 0 && hasSpawned < kEpsilon) // if havent spawned
		{
			pSrt->m_dataBuffer[emitterId].m_data0.x = fixedTime;
		}
		else if( rate < kEpsilon && hasSpawned > 0)
		{
			pSrt->m_dataBuffer[emitterId].m_data0.x = 0;
		}
	}

	// Modify rate based on groupId
	rate = max(0, rate - (groupId.x * 64 / pSrt->m_numEmitterStates));

	int numPartsToSpawnThisFrame;
	if ((pSrt->m_emitterStates[emitterId]->m_updateData.m_poolIdx_m_spawnMode >> 16) == kPartSpawnModeBurst)
	//if(true)
	{
		int doSpawn = ((fixedTime - pSrt->m_dataBuffer[emitterId].m_data0.x) < kEpsilon);
		numPartsToSpawnThisFrame = rate * doSpawn;
	}
	else //Normal spawning
	{
		float interval = 1/rate;
		float modTime = pSrt->m_time % interval;
		float modTimeMinusDelta = modTime - pSrt->m_delta;
		numPartsToSpawnThisFrame = (modTimeMinusDelta < 0) ? 1 + floor(abs(modTimeMinusDelta)/interval) : 0;
	}

	if (float(dispatchId)/pSrt->m_numEmitterStates >= numPartsToSpawnThisFrame)
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

	float4x4 emitterTransform;
	emitterTransform = getMatrixFromAngles(pSrt->m_emitterStates[emitterId]->m_computeEmitterState.spawnRot);
	emitterTransform[3].xyz = pSrt->m_emitterStates[emitterId]->m_computeEmitterState.spawnPos;

	emitterTransform[0].x *= pSrt->m_rootSpawnerScaleMatrix[0].x;
	emitterTransform[1].y *= pSrt->m_rootSpawnerScaleMatrix[1].y;
	emitterTransform[2].z *= pSrt->m_rootSpawnerScaleMatrix[2].z;


	emitterTransform = mul(emitterTransform, pSrt->m_rootMatrixWs);

	float birthOffset = GetRandom(pSrt->m_gameTicks, dispatchId.x*(2+groupThreadId), 14);
	float3 inheritedVel = 0;
	float inheritRandom = GetRandom(pSrt->m_gameTicks, dispatchId.x*(4+groupThreadId), 12);
	float inheritStr = lerp(1, inheritRandom, ParticleInheritVelocityVariation) * ParticleInheritVelocityMult;

	// Spawn Particles from CPU Particles
	if(pSrt->m_features & COMPUTE_FEATURE_SPAWN_FROM_PARTICLES)
	{
		uint dsize;
		pSrt->m_particleIndicesOther.GetDimensions(dsize);

		if (dsize == 0)
			return;

		int randParticleId = (rands.x * dsize) % dsize;

		uint origParticleIndex = pSrt->m_particleIndicesOther[randParticleId];
		ParticleInstance originalParticle = pSrt->m_particleInstancesOther[origParticleIndex];

		if (length(originalParticle.world[3].xyz) < kEpsilon)
			return;

		float3 delta = originalParticle.world[3].xyz - originalParticle.prevWorld[3].xyz;
		originalParticle.world[3].xyz = originalParticle.world[3].xyz + pSrt->m_altWorldOrigin.xyz + delta * birthOffset;

		originalParticle.world[0].x = 1.0; // ignore particle scale when transforming to particle space
		originalParticle.world[1].y = 1.0;
		originalParticle.world[2].z = 1.0;


		emitterTransform = originalParticle.world;

		inheritedVel = delta * inheritStr / pSrt->m_delta ;
	}

	uint emitterType = pSrt->m_emitterStates[emitterId]->m_emitterNodeData.m_nodeId;
	switch(emitterType)
	{
		case kPointEmitter:
			PointEmitterSpawn(*pSrt->m_emitterStates[emitterId], emitterTransform, rands, position, velocity);
			break;
		case kVolumeEmitter:
			VolumeEmitterSpawn(*pSrt->m_emitterStates[emitterId], emitterTransform, rands, position, velocity);
			break;
	}

	velocity += inheritedVel;

	uint gdsOffsetId = pSrt->m_gdsOffsetIdCounter;
	uint particleId = uint(NdAtomicIncrement(gdsOffsetId)); // we rely on ribbons to die before we can spawn them again. Todo: use the data buffer to read how many particles we have in ribbon already

	float lifeSpan = ParticleLifeSpanBase;
	lifeSpan += (GetRandom(pSrt->m_gameTicks, dispatchId.x, 6) - 0.5) * ParticleLifeSpanRandom;

	float birthTimeOffset = pSrt->m_delta * birthOffset;

	uint randValues = PackFloat2ToUInt(GetRandom(pSrt->m_gameTicks, dispatchId.x, 7), GetRandom(pSrt->m_gameTicks, dispatchId.x, 8));
	
	// Adds particle to the atomic buffer or retrun not doing anything
	NewGenericParticle( position, velocity, velocity, pSrt, dispatchId, particleId, /*birthTimeOffset=*/ birthTimeOffset, /*spriteTwist*/ rands.y * 3.1415 * 8, /*lifetime=*/ lifeSpan, randValues, 1.0);
}

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeGenericGenerateRenderables(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{
	// grab my particle

	uint gdsOffsetNew = pSrt->m_gdsOffsetCounterNew;
	uint numParticles = NdAtomicGetValue(gdsOffsetNew);

	if (dispatchId.x >= numParticles)// || dispatchId.x > 30)
		return; // todo: dispatch only the number of threads = number fo particles

	StructuredBuffer<GenericParticleState> castedBufferOld = __create_buffer< StructuredBuffer<GenericParticleState> >(__get_vsharp(pSrt->m_particleStates));
	GenericParticleState state = castedBufferOld[dispatchId.x];
	float3 pos = state.m_pos;

	DepthPosInfo dpi = DPISetup(pos, pSrt);

// culling
#if defined DO_RENDER_CULLING
	float bboxScale = max(ParticleSpriteScaleXYZ.x, max(ParticleSpriteScaleXYZ.y,ParticleSpriteScaleXYZ.z)) * 0.5;
	bboxScale *= pSrt->m_rootSpriteScale.x;
	bboxScale /= dpi.posH.w;

	uint pixelWidth = bboxScale * pSrt->m_screenResolution.x;
	if (!(pSrt->m_features & COMPUTE_FEATURE_DISABLE_SCALE_CULL) && pixelWidth < 4)
	{
		return;
	}

	float adjustedBoundary = 1 + bboxScale;

	bool offScreen = abs(dpi.posNDC.x) > adjustedBoundary || abs(dpi.posNDC.y) > adjustedBoundary || dpi.posH.w < 0;
	if(offScreen)
	{
		return;
	}

	bool occluded = false;
	if (!(pSrt->m_features & COMPUTE_FEATURE_DISABLE_OCCLUSION_CULL))
	{
		pixelWidth *= 0.5;
		uint pixelHeight = bboxScale * pSrt->m_screenResolution.y * 0.5;

		uint2 centerSS = dpi.posSS;
		DPISampleDepth(dpi, pSrt, false);
		bool center = (dpi.depthDiff < 0.2);

		dpi.posSS = centerSS + uint2(-pixelWidth, -pixelHeight);
		DPISampleDepth(dpi, pSrt, false);
		bool topLeft = (dpi.depthDiff < 0.2);
		
		dpi.posSS = centerSS + uint2(pixelWidth, -pixelHeight);
		DPISampleDepth(dpi, pSrt, false);
		bool topRight = (dpi.depthDiff < 0.2);

		dpi.posSS = centerSS + uint2(-pixelWidth, pixelHeight);
		DPISampleDepth(dpi, pSrt, false);
		bool bottomLeft = (dpi.depthDiff < 0.2);

		dpi.posSS = centerSS + uint2(pixelWidth, pixelHeight);
		DPISampleDepth(dpi, pSrt, false);
		bool bottomRight = (dpi.depthDiff < 0.2);
		occluded = !(center || topLeft || topRight || bottomLeft || bottomRight);
	}

	if (occluded)
	{
		return;
	}
#endif

	float4x4	g_identity = { { 1, 0, 0, 0 },{ 0, 1, 0, 0 },{ 0, 0, 1, 0 },{ 0, 0, 0, 1 } };
	float3 kUnitYAxis = { 0.0f, 1.0f, 0.0f };

	ParticleInstance inst = ParticleInstance(0);

	inst.world = g_identity;						// The object-to-world matrix
	inst.color = float4(1.0, 1.0, 1.0, 1.0);			// The particle's color

#if 0
	if (state.m_flags0 & kParticleFlags0CollisionCorrected)
		inst.color.rgb = float3(0,0,1);
	else if (state.m_flags0 & kParticleFlags0Frozen)
		inst.color.rgb = float3(0,1,0);
	else if ( state.m_flags2 > 0)
		inst.color.rgb = float3(1,0,0);
#endif
	inst.texcoord = float4(1, 1, 0, 0);					// Texture coordinate scale and bias (uv = uv0 * texcoord.xy + texcoord.zw)
	inst.userh = float4(0.0, 0.0, 0.0, 0.0);			// User attributes (used to be half data type)
	inst.userf = float4(0.0, 0.0, 0.0, 0.0);			// User attributes
	inst.partvars = float4(0, 0, 0, 0);					// Contains age, normalized age, ribbon distance, and frame number
	inst.invscale = float4(1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f, 1.0f / 0.5f);		// Reciprocal of the particle's half-scale vector

	// Orientation

	float3 camDirZ 		= pSrt->m_cameraDirWs.xyz * -1;
	float3 partToCamera = pSrt->m_cameraPosWs.xyz - pos;
	float3 partToCamDir = normalize(partToCamera);
	float  distToCamera = length(partToCamera);
	float3 velocityDir 	= normalize(state.m_velocity+float3(0,kEpsilon,0));
	float3 partOrient = state.m_rotation;

	const float3 xAxis = float3(1,0,0);
	const float3 yAxis = float3(0,1,0);
	const float3 zAxis = float3(0,0,1);

	bool isCrawler = pSrt->m_features & COMPUTE_FEATURE_CRAWLER_BEHAVIOR;
	bool isVelBlend = pSrt->m_features & COMPUTE_FEATURE_VELOCITY_ORIENT_BLEND;
	bool isVel2D = pSrt->m_features & COMPUTE_FEATURE_VELOCITY_FACING_2D;
	bool isVel3D = pSrt->m_features & COMPUTE_FEATURE_VELOCITY_FACING ;
	bool isVelFacing = isVel2D || isVel3D;

	int orientAt = int(ParticleOrientAtAxis);
	int orientAlign = int(ParticleOrientAlignAxis);
	bool preserveAt = true;

	if (isCrawler)
	{
		orientAt = 6;
		orientAlign = 11;
		preserveAt = false;
		//orientation = TransformFromLookAt(velNrm, state.m_rotation , pos, false);
	}
	else
	{
		if (isVelFacing)
		{
			if (isVelBlend)
			{
				if (isVel3D)
				{
					orientAt = 11;
					orientAlign = 0;
					//orientation = TransformFromLookAt(state.m_rotation, float3(0,1,0), pos, true);
				}
				else
				{
					orientAt = 0;
					orientAlign = 11;
					preserveAt = isVel2D;
					//orientation = TransformFromLookAt(toCam, state.m_rotation, pos, isVel2D);	//at cam, align vel
				}
			}
			else
			{
				orientAt = 0;
				orientAlign = 6;
				preserveAt = isVel2D;
				//orientation = TransformFromLookAt(toCam, velNrm, pos, isVel2D);	//at cam, align vel					
			}	
		}		
	}

	float3 atAxis;
	switch (orientAt)
	{
		//case 1: atAxis = camDirZ;		break;
		case 2: atAxis = partToCamDir;	break;
		case 3: atAxis = xAxis; 		break;
		case 4: atAxis = yAxis;			break;
		case 5: atAxis = zAxis;			break;
		case 6: atAxis = velocityDir; 	break;
		case 11: atAxis = partOrient;	break;
		default: atAxis = camDirZ;		break;
	}

	float3 alignAxis;
	switch (orientAlign)
	{
		case 1: alignAxis = camDirZ;		break;
		case 2: alignAxis = partToCamDir;	break;
		case 3: alignAxis = xAxis; 			break;
		//case 4: alignAxis = yAxis;		break;
		case 5: alignAxis = zAxis;			break;
		case 6: alignAxis = velocityDir; 	break;
		case 11: alignAxis = partOrient;	break;
		default: alignAxis = yAxis;			break;
	}	

	float4x4 orientation;
	orientation = TransformFromLookAt(atAxis, alignAxis, pos, preserveAt);

	float twist_amount = _Lower(state.m_data);
	float acceleration_bend = _Upper(state.m_data);

	// Twist
	if (pSrt->m_features & COMPUTE_FEATURE_RANDOM_SPRITE_TWIST)
	{
		float coss = cos(twist_amount * 0.5);
		float sinn = sin(twist_amount * 0.5);
		float4x4 twist;
		twist[0].xyzw = float4(coss, sinn, 0, 0);
		twist[1].xyzw = float4(-sinn, coss, 0, 0);
		twist[2].xyzw = float4(0, 0, 1, 0);
		twist[3].xyzw = float4(0, 0, 0, 1);
		inst.world = mul(twist, orientation);
	}
	else
	{
		inst.world = orientation;
	}

	float randValue1 = _Lower(state.m_flags1);
	float randValue2 = _Upper(state.m_flags1);

	// uservars
	float age = (pSrt->m_time - state.m_birthTime);
	inst.userh.x = age + randValue1*100; // userh 1: age
	inst.userh.y = age * ParticleFlipbookFramerate; // userh 2: index


	float lifeSpan = _Flower(state.m_lifeSpan);

	inst.partvars.x = age;
	inst.partvars.y = age / lifeSpan;

	if (pSrt->m_features & COMPUTE_FEATURE_RANDOM_START_INDEX)
	{
		inst.userh.y += randValue2*kMaxParticleCount;
	}
	if (pSrt->m_features & COMPUTE_FEATURE_RANDOM_CHANNEL_PICK)
	{	
		inst.userh.z = (int((randValue1 + randValue2) * 0.5 * kMaxParticleCount)) % 3; // userh 3: channel pick
	}	
	
	inst.userh.w = acceleration_bend * -1; // userh 4: acceleration bend

	float opacity = 1;

	opacity *= _Fupper(state.m_lifeSpan); // stored alpha

	// Opacity Fade
	opacity *= LinStep(0, ParticleOpacityFadeInDuration, age);
	opacity *= 1 - LinStep(lifeSpan - ParticleOpacityFadeOutDuration, lifeSpan, age);

	// Distance Fade
	opacity *= LinStep( ParticleDistanceFadeInStart, ParticleDistanceFadeInEnd, distToCamera);
	opacity *= LinStep(ParticleDistanceFadeOutEnd, ParticleDistanceFadeOutStart, distToCamera);

	inst.color.a = opacity;

	inst.color *= pSrt->m_rootColor;

	if(any(ParticleTint > 0.0f))
	{
		inst.color.rgb *= ParticleTint;
	}
	// Scale
	float3 scale = ParticleSpriteScaleXYZ * 0.5;
	scale -= (randValue1-0.5) * ParticleRandomScale;

	scale *= pSrt->m_rootSpriteScale.xyz;

	// fadein/out
	scale *= LinStep(0, ParticleScaleFadeInDuration, age);
	scale *= 1 - LinStep(lifeSpan - ParticleScaleFadeOutDuration, lifeSpan, age);


	if (ParticleDistScaleMin || ParticleDistScaleMax)
	{
		float fScale = 1;//pSrt->m_fovScale;  // These cause artifacts and need to be debugged
		float fInvScale = 1;//pSrt->m_invFovScale;
		//float screenW = mul(float4(pos, 1.0), pSrt->m_pPassDataSrt->g_mVP).w;
		float distScale = fScale / dpi.posH.w;

		distScale = (distScale > ParticleDistScaleMin) ? distScale : ParticleDistScaleMin;
		distScale = (!ParticleDistScaleMax || (distScale < ParticleDistScaleMax)) ? distScale : ParticleDistScaleMax;

		scale *= distScale * dpi.posH.w * fInvScale;
	}

	scale *= 1+kEpsilon-(float(min(0,state.m_flags2-1)) / 3); // shrink based on invalid states

	float vDotCam = 0;
	if (pSrt->m_features & COMPUTE_FEATURE_VELOCITY_FACING_2D)
	{
		vDotCam = saturate(abs(dot(state.m_velocity.xyz, pSrt->m_cameraDirWs.xyz)));
	}
	if (ParticleVelocityStretch > 0) // velocity stretch
	{
		float stretch =  length(state.m_velocity) * (1.0/30) * ParticleVelocityStretch;
		scale.y *= 1 + stretch * (1 - vDotCam);
	}


	inst.world[0].xyz = inst.world[0].xyz * scale.x;
	inst.world[1].xyz = inst.world[1].xyz * scale.y;
	inst.world[2].xyz = inst.world[2].xyz * scale.z;

	inst.invscale = float4(1.0f / (scale.x * 0.5), 1.0f / (scale.y * 0.5), 1.0f / (scale.z * 0.5), 1.0f / 1);

	float3 renderOffset = pSrt->m_renderOffset.xyz;
	float4 posOffest = mul(float4(renderOffset, 1), inst.world);

	// modify position based on camera
	inst.world[3].xyz = posOffest.xyz - pSrt->m_altWorldOrigin.xyz;

	inst.prevWorld = inst.world;		// Last frame's object-to-world matrix
	inst.prevWorld[3].xyz -= state.m_velocity.xyz * pSrt->m_delta;

#ifdef DO_RENDER_CULLING
	uint gdsOffsetOld = pSrt->m_gdsOffsetCounterOld;
	uint destinationIndex = NdAtomicIncrement(gdsOffsetOld);
#else
	uint destinationIndex = dispatchId.x;
#endif
	
	pSrt->m_particleInstances[destinationIndex] = inst;
	pSrt->m_particleIndices[destinationIndex] = destinationIndex;
}

GenericEmitterHint HintFromParticle(ParticleInstance part, float rate)
{
	GenericEmitterHint hint = GenericEmitterHint(0);
	hint.m_pos = part.world[3].xyz;
	float scaleX = 2.0f / part.invscale.x;
	float scaleY = 2.0f / part.invscale.y;
	float scaleZ = 2.0f / part.invscale.z;
	hint.m_scale = float3(scaleX, scaleY, scaleZ);

	float3x3 rot;
	rot[0] = part.world[0].xyz * part.invscale.x * 0.5;
	rot[1] = part.world[1].xyz * part.invscale.y * 0.5;
	rot[2] = part.world[2].xyz * part.invscale.z * 0.5;

	//uint
	hint.m_rotation = EncodeTbnQuatCayley(rot, 1.0);

	//float4
	hint.m_data0 = part.userh;
	hint.m_data0.x = rate;

	//float4
	hint.m_data1 = part.userf;
	hint.m_data1.x = asfloat(PackFloat2ToUInt(0, part.userf.x));
	hint.m_data1.y = asfloat(PackFloat2ToUInt(1.0, part.userf.y));

	return hint;
}

GenericEmitterHint HintFromCpuEmitter(ParticleEmitterEntry emitter, float rate)
{
	GenericEmitterHint hint = GenericEmitterHint(0);
	hint.m_pos = emitter.m_pos.xyz;

	float3 offsetES = mul(float4(emitter.m_offsetVector,0.0), emitter.m_rot).xyz;
	hint.m_pos += offsetES;

	float scaleX = emitter.m_scale.x;
	float scaleY = emitter.m_scale.y;
	float scaleZ = emitter.m_scale.z;
	hint.m_scale = float3(scaleX, scaleY, scaleZ);

	//hint.m_scale *= 0.01f;

	float3x3 rot;
	rot[0] = emitter.m_rot[0].xyz;
	rot[1] = emitter.m_rot[1].xyz;
	rot[2] = emitter.m_rot[2].xyz; 

	//uint
	hint.m_rotation = EncodeTbnQuatCayley(rot, 1.0);

	//float4
	hint.m_data0 = float4(0, 0, 1, 0);
	hint.m_data0.x = rate;

	//float4
	hint.m_data1 = float4(0, 0, 0, 0);
	hint.m_data1.x = asfloat(PackFloat2ToUInt(emitter.m_lifespan, 0.0));
	hint.m_data1.y = asfloat(PackFloat2ToUInt(emitter.m_alpha, 0.0));

	return hint;
}

HintEmitterInfo EmitterFromHint(GenericEmitterHint hint)
{
	HintEmitterInfo info = HintEmitterInfo(0);
	info.pos = hint.m_pos;
	info.scale = hint.m_scale;

	info.rotation = QuatToTbn(DecodeTbnQuatCayley(hint.m_rotation));
	info.rotation[0] *= info.scale.x;
	info.rotation[1] *= info.scale.y;
	info.rotation[2] *= info.scale.z;	
	info.rate = hint.m_data0.x;

	info.direction = float3(hint.m_data0.y, hint.m_data0.z, hint.m_data0.w);

	info.spread = _Fupper(hint.m_data1.x);
	info.speed = _Fupper(hint.m_data1.y);
	info.speedRandom = hint.m_data1.z;
	info.awayFromCenter = hint.m_data1.w;

	info.lifespan = _Flower(hint.m_data1.x);
	info.alpha = _Flower(hint.m_data1.y);

	return info;
}

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeGenericEmitterHint(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{

	if (dispatchId != 0)
		return;

	uint gdsOffsetNew = pSrt->m_gdsOffsetOther;
	if (gdsOffsetNew == 0)
		return;

	uint origSize;
	pSrt->m_particleIndicesOrig.GetDimensions(origSize);

	if (dispatchId >= origSize)
		return;

	uint size, stride;
	pSrt->m_particleStatesOther.GetDimensions(size, stride);

	uint origParticleIndex = pSrt->m_particleIndicesOrig[dispatchId];
	ParticleInstance originalParticle = pSrt->m_particleInstancesOrig[origParticleIndex];

	originalParticle.world[3].xyz = originalParticle.world[3].xyz + pSrt->m_altWorldOrigin.xyz;

	float distToCamera = length(originalParticle.world[3].xyz - pSrt->m_cameraPosWs.xyz);
	float rate = originalParticle.userh.x;
	rate *= LinStep(pSrt->m_pComputeCustomData->m_vec0.y, pSrt->m_pComputeCustomData->m_vec0.x, distToCamera);

	if ( rate < 0.001 )
		return;

	{
		// we use a additional buffer of particle states to write data to tell the otehr system to spawn particles. It doesn't have to be a buffer of states, but we just use it.
		uint newInfoIndex = NdAtomicIncrement(gdsOffsetNew);
		if (newInfoIndex >= size)
		{
			// decrement back
			NdAtomicDecrement(gdsOffsetNew);
			return; // can't add new information
		}

		GenericEmitterHint info = HintFromParticle(originalParticle, rate);
		//info.m_pos = float3(-172, -20, 644);
		RWStructuredBuffer<GenericEmitterHint> destHintBuffer = __create_buffer<RWStructuredBuffer<GenericEmitterHint> >(__get_vsharp(pSrt->m_particleStatesOther));
		destHintBuffer[newInfoIndex] = info;
	}
}




[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeGenericEmitterHintFromSpawnerGroup(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{

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

	float rate = emitterEntry.m_rate;

	rate *= LinStep(pSrt->m_pCpuEmitterComputeCustomData->m_vec0.y, pSrt->m_pCpuEmitterComputeCustomData->m_vec0.x, distToCamera);

	if (rate < 0.001)
		return;

	{
		// we use a additional buffer of particle states to write data to tell the otehr system to spawn particles. It doesn't have to be a buffer of states, but we just use it.
		uint newInfoIndex = NdAtomicIncrement(gdsOffsetNew);
		if (newInfoIndex >= size)
		{
			// decrement back
			NdAtomicDecrement(gdsOffsetNew);
			return; // can't add new information
		}

		GenericEmitterHint info = HintFromCpuEmitter(emitterEntry, rate);
		RWStructuredBuffer<GenericEmitterHint> destHintBuffer = __create_buffer<RWStructuredBuffer<GenericEmitterHint> >(__get_vsharp(pSrt->m_particleStatesOther));
		destHintBuffer[newInfoIndex] = info;
	}
}

[NUM_THREADS(64, 1, 1)]
void CS_ParticleComputeGenericSpawnFromHints(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleComputeJobSrt *pSrt : S_SRT_DATA)
{

	float4 rands;
	rands.x = GetRandom(pSrt->m_gameTicks, dispatchId.x, 1);
	rands.y = GetRandom(pSrt->m_gameTicks, dispatchId.x, 2);
	rands.z = GetRandom(pSrt->m_gameTicks, dispatchId.x, 3);
	rands.w = GetRandom(pSrt->m_gameTicks, dispatchId.x, 4);

	float3 position = 0;
	float3 velocity = 0;

	float4x4 emitterTransform = 0;


	float3 upVector = float3(0, 1, 0);

	float lifeSpan = ParticleLifeSpanBase;
	float alpha = 1;

	// emitter hints
	if (pSrt->m_features & COMPUTE_FEATURE_SPAWN_FROM_EMITTER_HINTS)
	{

		uint gdsOffsetOther = pSrt->m_gdsOffsetOther;
		if (gdsOffsetOther == 0)
			return;

		uint numEmitterHints = NdAtomicGetValue(gdsOffsetOther);

		if (numEmitterHints == 0)
			return;

		int emitterId = int(dispatchId % int(numEmitterHints));

		RWStructuredBuffer<GenericEmitterHint> destHintBuffer = __create_buffer<RWStructuredBuffer<GenericEmitterHint> >(__get_vsharp(pSrt->m_particleStatesOther));
		GenericEmitterHint hint = destHintBuffer[emitterId];
		float rate = hint.m_data0.x;
		
		// Modify rate based on groupId
		rate = max(0, rate - (groupId.x * 64 / numEmitterHints));

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


		{
			position = float3(rands.x, rands.y, rands.z)-0.5; 

			HintEmitterInfo info = EmitterFromHint(hint);
			emitterTransform[0].xyz = info.rotation[0];
			emitterTransform[1].xyz = info.rotation[1];
			emitterTransform[2].xyz = info.rotation[2];
			emitterTransform[3] = float4(info.pos, 1.0);

			upVector = info.rotation[1];

			float randx = GetRandom(pSrt->m_gameTicks, dispatchId.x, 10);
			float randy = GetRandom(pSrt->m_gameTicks, dispatchId.x, 11);
			float randz = GetRandom(pSrt->m_gameTicks, dispatchId.x, 12);

			float azimuth = randy * 6.28318530718;
			float elevation = 1.5707963226 - randx * info.spread * 1.5707963226;
			float cose = cos(elevation);
			float4 spreadDir = float4(cos(azimuth)*cose, sin(azimuth)*cose, sin(elevation), 0.0);

			velocity = mul(spreadDir,  TransformFromLookAt( SafeNormalize(info.direction), float3(0, 1, 0), 0, true) ).xyz; 
			velocity += SafeNormalize(position) * info.awayFromCenter;
			velocity = normalize(mul(float4(velocity,0.0), emitterTransform).xyz);
			velocity *= info.speed + info.speedRandom * randz;
			
			position = mul(float4(position, 1.0), emitterTransform).xyz; 

			lifeSpan = (info.lifespan > kEpsilon)? info.lifespan : lifeSpan;
			alpha = info.alpha;

		}

	}
	else
	{   //particle hints
		uint gdsOffsetOther = pSrt->m_gdsOffsetOther;
		if (gdsOffsetOther == 0)
			return;

		uint numHints = NdAtomicGetValue(gdsOffsetOther);

		if (dispatchId.x >= numHints)
			return;

		RWStructuredBuffer<GenericParticleHint> destHintBuffer = __create_buffer<RWStructuredBuffer<GenericParticleHint> >(__get_vsharp(pSrt->m_particleStatesOther));

		GenericParticleHint hint = destHintBuffer[dispatchId.x];

		position = hint.m_pos;
		alpha = hint.m_alpha;
		
	}

	DepthPosInfo depthPos = DPISetup(position, pSrt);
	uint stencil = pSrt->m_pPassDataSrt->m_opaquePlusAlphaStencil[depthPos.posSS];
	
	if ((pSrt->m_features & COMPUTE_FEATURE_SPAWN_FILTER_ON_WATER) && (stencil & 0x02))
	{
		return;
	}

	bool notBg = (stencil & 0x20) || (stencil & 0x40) || (stencil & 0x080);
	if ((pSrt->m_features & COMPUTE_FEATURE_BG_ONLY) && notBg)
	{
		return;
	}

	if ((pSrt->m_features & COMPUTE_FEATURE_FG_ONLY) && !notBg )
	{
		return;
	}

	uint gdsOffsetId = pSrt->m_gdsOffsetIdCounter;
	uint particleId = uint(NdAtomicIncrement(gdsOffsetId)); // we rely on ribbons to die before we can spawn them again. Todo: use the data buffer to read how many particles we have in ribbon already

	lifeSpan += (GetRandom(pSrt->m_gameTicks, dispatchId.x, 6) - 0.5) * ParticleLifeSpanRandom;

	float birthOffset = GetRandom(pSrt->m_gameTicks, dispatchId.x, 0);
	float birthTimeOffset = pSrt->m_delta * birthOffset;

	uint randValues = PackFloat2ToUInt(GetRandom(pSrt->m_gameTicks, dispatchId.x, 7), GetRandom(pSrt->m_gameTicks, dispatchId.x, 8));
	// Adds particle to the atomic buffer or retrun not doing anything
	bool spawned = NewGenericParticle( position, velocity, upVector, pSrt, dispatchId, particleId, /*birthTimeOffset=*/ birthTimeOffset, /*spriteTwist*/ rands.y * 3.1415 * 8, /*lifetime=*/ lifeSpan, randValues, alpha);

	
	float3 vecToCam = position - pSrt->m_cameraPosWs;
	float d2 = dot(vecToCam, vecToCam);

	if (pSrt->m_features & COMPUTE_FEATURE_SEND_SOUND_EVENTS)
	{
		if (spawned && d2 < 15.0 * 15.0)
		{
			SpawnSoundBasedOnOllision(pSrt->m_particleFeedBackHeaderData, pSrt->m_particleFeedBackData, position, velocity, -6.0f, lifeSpan, pSrt->m_time);
		}
	}

	bool isPlayer = (stencil & 0x40);
	if ((pSrt->m_features & COMPUTE_FEATURE_SEND_PLAYER_SPLASH_EVENT) && isPlayer)
	{
		if (!(pSrt->m_features & COMPUTE_FEATURE_SPAWN_FROM_EMITTER_HINTS))
		{
			if (spawned)
			{
				float rate = 1;
				SetSplashSpawnedOnPlayer(pSrt->m_particleFeedBackHeaderData, rate);
			}
		}
	}
}

