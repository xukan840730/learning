//
// Vertex Shader - Computes a Rain Cell Vertex
//
#include "global-funcs.fxi"
#include "rain.fxi"
#include "rain-impl.fxi"


struct VertexInput
{
	float4		position		: ATTR0;
};





// This will be replaced by one of our surfer shaders
float4 Ps_Rain(RainVertexShaderOutput IN) : SV_Target
{
	return IN.color;
}

RainVertexShaderOutput Vs_RainTest(uint vertexId, uint instanceId, RainSrtData srt)
{
	RainVertexShaderOutput OUT;

	CellInfoBuffer cell = srt.m_cells[instanceId];
	RainInfoBuffer *rainInfo = srt.m_consts;

	// We assume that the primitive will be an indexed triangle list
	// However, we only care about vertices in groups of 4

	// We have an issue depending of what geometric type we are generating
	//
	// If we have triangle index list, we need to know which triangle we are dealing with
	// to know which vertex index
	
	// So if primitiveID is even (0,2,3)

	//   0--1	   0    
	//    \ |	   |\   
	//     \|	   | \  
	//      2	   3--2 
	//  Prim Even  Prim Odd

	float4 position = float4(0,0,0,1);
	float  z = vertexId / 4;
	 
	float x = cell.m_cellPosition.x;
	float2 uv=0;

	float3 color = 0;

	if ((vertexId % 4) == 0) {
		position = float4(x+0,0,z,1);
		uv = float2(0,0);
		color = float3(0,0,0);
	} 
	else if ((vertexId % 4) == 1) {
		position = float4(x+1,0,z,1);
		uv = float2(1,0);
		color = float3(1,0,0);
	} 
	else if ((vertexId % 4) == 2) {
		position = float4(x+1,1,z,1);
		uv = float2(1,1);
		color = float3(1,1,0);
	} 
	else if ((vertexId % 4) == 3) {
		position = float4(x+0,1,z,1);
		uv = float2(0,1);
		color = float3(0,1,0);
	}

	color.rgb = rainInfo->m_ambientOverrideColor * rainInfo->m_rainAmbientColor.rgb;
	
	if (rainInfo->m_numLights > 0) {
		color += CalculateLightingPtr(position.xyz, rainInfo, srt.m_texturesAndSamplers);
	}

	OUT.hPosition.xyzw = posToProj4(position.xyz, rainInfo->m_mVP);
	OUT.wPosition	   	= position;
	OUT.uv			   	= uv;
	OUT.color	   	    = float4(color.rgb, rainInfo->m_alphaMult * (rainInfo->m_motionBlurAlphaActive)? 1.f : rainInfo->m_motionBlurAlpha);
	OUT.rainMisc		= 0.0f;

	return OUT;
}

RainVertexShaderOutput Vs_Rain2DMotionBlur(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID, RainSrtData srt : S_SRT_DATA)
{
	RainVertexShaderOutput OUT;

	CellInfoBuffer cell = srt.m_cells[instanceId];
	RainInfoBuffer *rainInfo = srt.m_consts;


	float4 right = rainInfo->m_width * rainInfo->m_cameraRight;
	float4 up = rainInfo->m_height  * float4(cross(rainInfo->m_cameraRight.xyz, rainInfo->m_cameraDir.xyz), 0);

	float maxViewStretch  = rainInfo->m_motionBlurCell;
	float maxViewStretch2 = 4.f * rainInfo->m_motionBlurCell / 5.f;

	float blurDistanceMin = 0.75f * rainInfo->m_motionBlurDistance;

	uint mult 	= 16807;
	uint mod  	= 2147483647 -1 ;             // 2^31 - 1
	float scale = float(mod - 1);

	float numSpans = 32.0f; // kSnowNumSpans;

	uint quadId = vertexId / 4; // infer the quad from the vertex index

	// Seed the random value by the position of the cell and
	uint rand =  (uint) cell.m_cellRandId + 133 * (quadId + 1);

	rand = (rand * mult) % mod;
	float magtime = 27.3729f * rainInfo->m_randomScale * numSpans * ((float) rand / scale);
	float gtime   = rainInfo->m_ptime + magtime;

	// Base point of flake
	float4 base = rainInfo->m_csize * EvalCurvePtr(rainInfo, gtime);
	base.xyz = (quadId < 20) ? base.xyz : ((quadId < 40) ? base.yzx : ((quadId < 60) ? base.zxy : base.xzy));

	base += cell.m_cellPosition;

	// Position of flake in curve
	rand = (rand * mult) % mod;		float volX = (((float) rand) / scale) - .5f;
	rand = (rand * mult) % mod;		float volY = (((float) rand) / scale) - .5f;
	rand = (rand * mult) % mod;		float volZ = (((float) rand) / scale) - .5f;

	float4 inCurveScale = rainInfo->m_inCurveScale * float4(volX, volY, volZ, 0.0);

	base += inCurveScale;
		
	//
	// 2D Motion blur
	// 
	float  pgtime   = rainInfo->m_pptime + magtime;
	float4 basePrev = rainInfo->m_csize * EvalCurvePtr(rainInfo, pgtime);

	basePrev.xyz = (quadId < 20) ? basePrev.xyz : ((quadId < 40) ? basePrev.yzx : ((quadId < 60) ? basePrev.zxy : basePrev.xzy));

	basePrev += cell.m_cellPrevPos; 
	basePrev += inCurveScale;

	// Blur only if flake is closer than 'motionDistance'
	float distanceToCamera = length(base.xyz - rainInfo->m_cameraPos.xyz);
	// Blur = 0 if distance > motionBlurDistance


	// THIS IS WRONG!!!!
	// The computation of the blur factors is set for T1 but it should be using 
	// the StaticLinStep2 instead!!!
	float blur0, blur1;
	blur0 = 1.0f / (blurDistanceMin - rainInfo->m_motionBlurDistance);
	blur1 = -rainInfo->m_motionBlurDistance * blur0;
	float blur = rainInfo->m_motionBlurActive * StaticLinStep(distanceToCamera, blur0, blur1);
		//StaticLinStep(distanceToCamera, 0.75f * rainInfo->m_motionBlurDistance, rainInfo->m_motionBlurDistance);

	// Trick!
	// Transform the previous position (in time) of the flake into the last camera view
	// then transform that view position into the new camera world and use that position as a blur
	// The position is wrong! but it does give a nicer feeling for the blur
		
	// previous position in view space.  world to view (previous camera)
	basePrev.w = 1.0;
	basePrev = mul(basePrev, transpose(rainInfo->m_worldToViewPrev));
		
	base.w = 1.0;
	// new position in view space
	float4 baseView = mul(base, transpose(rainInfo->m_worldToView));
				
	// Compute distance in view space from previous position to current position
	// Limit the amount of stretch based in 
	float4 diffView = basePrev - baseView;
	float distanceInView = length(diffView.xyz);

	distanceInView = min(distanceInView, maxViewStretch);
	diffView.xyz = normalize(diffView.xyz);

	basePrev = baseView + distanceInView * diffView;

	basePrev.w = 1.0;
	// previous position in current camera (world space)
	basePrev = mul(basePrev, transpose(rainInfo->m_viewToWorld));

	// Stretch even more the motion blur
	float4 diff = basePrev - base;

	float newHeight = length(diff);
	float4 diffN = normalize(diff);
		
	// Check that there is some minimum height
	// newHeight might be small if there is little motion, so want to force a particle size (m_height)
	float minHeight = max(newHeight, rainInfo->m_height);
	basePrev = base + (rainInfo->m_motionBlurCurve * minHeight ) * diffN;
	//basePrev = base + rainInfo->m_motionBlurCurve * diff;
		
	float4 dirTravel = normalize(float4(basePrev.xyz - base.xyz, 0.0));

	// Limit the amount of motion blur for particles that are facing the camera!
	// Uses a dot product limit!
	basePrev = lerp( base + up, basePrev, blur);
	float cameraDistance = abs(dot(dirTravel.xyz, rainInfo->m_cameraDir.xyz));
	basePrev = lerp( basePrev, base + up, RainLinStep( cameraDistance, rainInfo->m_motionBlurThreshold0, rainInfo->m_motionBlurThreshold1));

	float4 newRight = rainInfo->m_width * float4(normalize(cross(basePrev.xyz - base.xyz, rainInfo->m_cameraDir.xyz)), 0); //  base - cameraPos
	newRight = lerp(right, newRight, blur);

	// Alpha

	float alpha00,  alpha10,  alpha11,  alpha01 = 1;
	alpha00 = alpha10 = alpha11 = alpha01 = 1;

	alpha11 = (rainInfo->m_motionBlurAlphaActive)? 1.f : rainInfo->m_motionBlurAlpha;
	alpha01 = (rainInfo->m_motionBlurAlphaActive)? 1.f : rainInfo->m_motionBlurAlpha;

	float iblur = 1.f - blur; 
	alpha00 = (rainInfo->m_motionBlurAlphaActive)? iblur : rainInfo->m_motionBlurAlpha;
	alpha10 = (rainInfo->m_motionBlurAlphaActive)? iblur : rainInfo->m_motionBlurAlpha;

	float4 position = float4(0,0,0,1);
	float2 uv=0;
	float  alpha=1;
	float3 color=0;

	/// Texture selection
	float  texQuadId = (float) (quadId % (uint) rainInfo->m_numTextureSlots); // (quadId + cell.m_cellPeriodId) % (uint) rainInfo->m_numTextureSlots;

	float uoff0 = texQuadId * rainInfo->m_numTextureSlotsInv; 
	float uoff1 = uoff0 + rainInfo->m_numTextureSlotsInv;


	if ((vertexId % 4) == 0) {
		position = float4(base.xyz, 1.0);
		uv = float2(uoff0, 0);
		alpha = alpha00;
	} 
	else if ((vertexId % 4) == 1) {
		position = float4(base.xyz + newRight.xyz, 1.0);
		uv = float2(uoff1, 0);
		alpha = alpha10;
	} 
	else if ((vertexId % 4) == 2) {
		position = float4(basePrev.xyz + newRight.xyz, 1.0);
		uv = float2(uoff1, 1);
		alpha = alpha11;
	} 
	else if ((vertexId % 4) == 3) {
		position = float4(basePrev.xyz, 1.0);
		uv = float2(uoff0, 1);
		alpha = alpha01;
	}

	color.rgb = rainInfo->m_ambientOverrideColor * rainInfo->m_rainAmbientColor.rgb;
	
 	if (rainInfo->m_numLights > 0) {
		color += CalculateLightingPtr(position.xyz, rainInfo, srt.m_texturesAndSamplers);
	}

	alpha *= rainInfo->m_alphaMult;
	alpha *= smoothstep(rainInfo->m_alphaFadeInStart, rainInfo->m_alphaFadeInStart + rainInfo->m_alphaFadeInDistance, distanceToCamera);
	alpha *= smoothstep(rainInfo->m_alphaFadeOutStart, rainInfo->m_alphaFadeOutStart - rainInfo->m_alphaFadeOutDistance, distanceToCamera);
	
	color.rgb = lerp(color.rgb, rainInfo->m_rainColor.rgb, rainInfo->m_overrideColor);

	//=================//
	// Output Position //
	//=================//
	OUT.hPosition.xyzw = posToProj4(position.xyz, rainInfo->m_mVP);
	OUT.wPosition	   	= position;
	OUT.uv			   	= uv;
	OUT.color		   	= float4(color, alpha);
	OUT.rainMisc		= 0.0f;

	return OUT;
}


RainVertexShaderOutput Vs_Rain3DMotionBlur(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID, RainSrtData srt : S_SRT_DATA)
{
	RainVertexShaderOutput OUT;

	CellInfoBuffer cell = srt.m_cells[instanceId];
	RainInfoBuffer *rainInfo = srt.m_consts;


	float4 right = rainInfo->m_width * rainInfo->m_cameraRight;
	float4 up = rainInfo->m_height  * float4(cross(rainInfo->m_cameraRight.xyz, rainInfo->m_cameraDir.xyz), 0);

	float maxViewStretch  = rainInfo->m_motionBlurCell;
	float maxViewStretch2 = 4.f * rainInfo->m_motionBlurCell / 5.f;

	float blurDistanceMin = 0.75f * rainInfo->m_motionBlurDistance;

	uint mult 	= 16807;
	uint mod  	= 2147483647 -1 ;             // 2^31 - 1
	float scale = float(mod - 1);
	//	float ptime = rainInfo->m_time * rainInfo->m_period;

	// Blend between prev and current time
	//	float pptime = (rainInfo->m_time - rainInfo->m_deltaTime) * rainInfo->m_period;  
	//	float csize   = rainInfo->m_cellSpacing * rainInfo->m_cellSize;
	//	float quadMax	= max(rainInfo->m_width, rainInfo->m_height;


	float numSpans = 32.0f; // kSnowNumSpans;

	uint quadId = vertexId / 4; // infer the quad from the vertex index

	// Seed the random value by the position of the cell and
	uint rand =  (uint) cell.m_cellRandId + 133 * (quadId + 1);

	rand = (rand * mult) % mod;
	float magtime = 27.3729f * rainInfo->m_randomScale * numSpans * ((float) rand / scale);
	float gtime   = rainInfo->m_ptime + magtime;

	// Base point of flake
	float4 base = rainInfo->m_csize * EvalCurvePtr(rainInfo, gtime);
	base.xyz = (quadId < 20) ? base.xyz : ((quadId < 40) ? base.yzx : ((quadId < 60) ? base.zxy : base.xzy));

	base += cell.m_cellPosition;

	// Position of flake in curve
	rand = (rand * mult) % mod;		float volX = (((float) rand) / scale) - .5f;
	rand = (rand * mult) % mod;		float volY = (((float) rand) / scale) - .5f;
	rand = (rand * mult) % mod;		float volZ = (((float) rand) / scale) - .5f;

	float4 inCurveScale = rainInfo->m_inCurveScale * float4(volX, volY, volZ, 0.0);


	base += inCurveScale;
		
	//
	// 2D Motion blur
	// 
	float  pgtime   = rainInfo->m_pptime + magtime;
	float4 basePrev = rainInfo->m_csize * EvalCurvePtr(rainInfo, pgtime);

	basePrev.xyz = (quadId < 20) ? basePrev.xyz : ((quadId < 40) ? basePrev.yzx : ((quadId < 60) ? basePrev.zxy : basePrev.xzy));

	basePrev += cell.m_cellPrevPos; 
	basePrev += inCurveScale;

	// Blur only if flake is closer than 'motionDistance'
	float distanceToCamera = length(base.xyz - rainInfo->m_cameraPos.xyz);
	// Blur = 0 if distance > motionBlurDistance

	// THIS IS WRONG!!!!
	// The computation of the blur factors is set for T1 but it should be using 
	// the StaticLinStep2 instead!!!
	float blur0, blur1;
	blur0 = 1.0f / (blurDistanceMin - rainInfo->m_motionBlurDistance);
	blur1 = -rainInfo->m_motionBlurDistance * blur0;
	float blur = rainInfo->m_motionBlurActive * StaticLinStep(distanceToCamera, blur0, blur1);
		//StaticLinStep(distanceToCamera, 0.75f * rainInfo->m_motionBlurDistance, rainInfo->m_motionBlurDistance);

	// Stretch even more the motion blur
	float4 diff = basePrev - base;

	float newHeight = length(diff);
	float4 diffN = normalize(diff);
		
	// Check that there is some minimum height
	// newHeight might be small if there is little motion, so want to force a particle size (m_height)
	float minHeight = max(newHeight, rainInfo->m_height);
	basePrev = base + (rainInfo->m_motionBlurCurve * minHeight ) * diffN;
		
	float4 dirTravel = normalize(float4(basePrev.xyz - base.xyz, 0.0));

	// Limit the amount of motion blur for particles that are facing the camera!
	// Uses a dot product limit!
	basePrev = lerp( base + up, basePrev, blur);

	basePrev = lerp( basePrev, base + up, RainLinStep( abs(dot(dirTravel.xyz, rainInfo->m_cameraDir.xyz)), rainInfo->m_motionBlurThreshold0, rainInfo->m_motionBlurThreshold1));

	float4 newRight = rainInfo->m_width * float4(normalize(cross(basePrev.xyz - base.xyz, rainInfo->m_cameraDir.xyz)), 0); //  base - cameraPos
	newRight = lerp(right, newRight, blur);

	// Alpha

	float alpha00,  alpha10,  alpha11,  alpha01 = 1;
	alpha00 = alpha10 = alpha11 = alpha01 = 1;

	alpha11 = (rainInfo->m_motionBlurAlphaActive)? 1.f : rainInfo->m_motionBlurAlpha;
	alpha01 = (rainInfo->m_motionBlurAlphaActive)? 1.f : rainInfo->m_motionBlurAlpha;

	float iblur = 1.f - blur; 
	alpha00 = (rainInfo->m_motionBlurAlphaActive)? iblur : rainInfo->m_motionBlurAlpha;
	alpha10 = (rainInfo->m_motionBlurAlphaActive)? iblur : rainInfo->m_motionBlurAlpha;

	float4 position = float4(0,0,0,1);
	float2 uv=0;
	float  alpha=1;
	float3 color=0;

	/// Texture selection
	float  texQuadId = (float) (quadId % (uint) rainInfo->m_numTextureSlots); // (quadId + cell.m_cellPeriodId) % (uint) rainInfo->m_numTextureSlots;

	float uoff0 = texQuadId * rainInfo->m_numTextureSlotsInv; 
	float uoff1 = uoff0 + rainInfo->m_numTextureSlotsInv;

	
	if ((vertexId % 4) == 0) {
		position = float4(base.xyz, 1.0);
		uv = float2(uoff0, 0);
		alpha = alpha00;
	} 
	else if ((vertexId % 4) == 1) {
		position = float4(base.xyz + newRight.xyz, 1.0);
		uv = float2(uoff1, 0);
		alpha = alpha10;
	} 
	else if ((vertexId % 4) == 2) {
		position = float4(basePrev.xyz + newRight.xyz, 1.0);
		uv = float2(uoff1, 1);
		alpha = alpha11;
	} 
	else if ((vertexId % 4) == 3) {
		position = float4(basePrev.xyz, 1.0);
		uv = float2(uoff0, 1);
		alpha = alpha01;
	}

	color.rgb = rainInfo->m_ambientOverrideColor * rainInfo->m_rainAmbientColor.rgb;
	
 	if (rainInfo->m_numLights > 0) {
		color += CalculateLightingPtr(position.xyz, rainInfo, srt.m_texturesAndSamplers);
	}

	alpha *= rainInfo->m_alphaMult;
	alpha *= smoothstep(rainInfo->m_alphaFadeInStart, rainInfo->m_alphaFadeInStart + rainInfo->m_alphaFadeInDistance, distanceToCamera);
	alpha *= smoothstep(rainInfo->m_alphaFadeOutStart, rainInfo->m_alphaFadeOutStart - rainInfo->m_alphaFadeOutDistance, distanceToCamera);
	
	color.rgb = lerp(color.rgb, rainInfo->m_rainColor.rgb, rainInfo->m_overrideColor);

	//=================//
	// Output Position //
	//=================//
	OUT.hPosition.xyzw = posToProj4(position.xyz, rainInfo->m_mVP);
	OUT.wPosition	   	= position;
	OUT.uv			   	= uv;
	OUT.color		   	= float4(color, alpha);
	OUT.rainMisc		= 0.0f;

	return OUT;
}


RainVertexShaderOutput Vs_Rain3DMotionBlurWithCameraMotion(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID, RainSrtData srt : S_SRT_DATA)
{
	RainVertexShaderOutput OUT;
	RainInfoBuffer rainInfo = *srt.m_consts;
	CellInfoBuffer cell = srt.m_cells[instanceId];

	OUT = Vs_Rain3DMotionBlurWithCameraMotionImpl(vertexId, instanceId, srt, rainInfo, cell, *srt.m_texturesAndSamplers);
	
	return OUT;
}


RainVertexShaderOutput Vs_Rain2DMotionBlurWithCameraMotion(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID, RainSrtData srt : S_SRT_DATA)
{
	RainVertexShaderOutput OUT;

	CellInfoBuffer cell = srt.m_cells[instanceId];
	RainInfoBuffer *rainInfo = srt.m_consts;


	float4 right = rainInfo->m_width * rainInfo->m_cameraRight;
	float4 up = rainInfo->m_height  * float4(cross(rainInfo->m_cameraRight.xyz, rainInfo->m_cameraDir.xyz), 0);

	float4 cameraDiff = rainInfo->m_cameraPrevPos - rainInfo->m_cameraPos;

	float maxViewStretch  = rainInfo->m_motionBlurCell;
	float maxViewStretch2 = 4.f * rainInfo->m_motionBlurCell / 5.f;

	float blurDistanceMin = 0.75f * rainInfo->m_motionBlurDistance;

	// MakeStaticFactors
	// float blur0, blur1;
	// blur0 = 1.0f / (blurDistanceMin - rainInfo->m_motionBlurDistance);
	// blur1 = -rainInfo->m_motionBlurDistance * blur0;

	//	cspace  = rainInfo->m_cellSpacing;
	//	cuspace = U32(rainInfo->m_cellSpacing);

	//	float textureSlotOffset = 1.f / rainInfo->m_numTextureSlots;

	uint mult 	= 16807;
	uint mod  	= 2147483647 -1 ;             // 2^31 - 1
	float scale = float(mod - 1);
	//	float ptime = rainInfo->m_time * rainInfo->m_period;

	// Blend between prev and current time
	//	float pptime = (rainInfo->m_time - rainInfo->m_deltaTime) * rainInfo->m_period;  
	//	float csize   = rainInfo->m_cellSpacing * rainInfo->m_cellSize;
	//	float quadMax	= max(rainInfo->m_width, rainInfo->m_height);

	float numSpans = 32.0f; // kSnowNumSpans;

	uint quadId = vertexId / 4; // infer the quad from the vertex index



	// Seed the random value by the position of the cell and
	uint rand =  (uint) cell.m_cellRandId + 133 * (quadId + 1);

	rand = (rand * mult) % mod;
	float magtime = 27.3729f * rainInfo->m_randomScale * numSpans * ((float) rand / scale);
	float gtime   = rainInfo->m_ptime + magtime;

	// Base point of flake
	float4 base = rainInfo->m_csize * EvalCurvePtr(rainInfo, gtime);
	base.xyz = (quadId < 20) ? base.xyz : ((quadId < 40) ? base.yzx : ((quadId < 60) ? base.zxy : base.xzy));

	base += cell.m_cellPosition;

	// Position of flake in curve
	rand = (rand * mult) % mod;		float volX = (((float) rand) / scale) - .5f;
	rand = (rand * mult) % mod;		float volY = (((float) rand) / scale) - .5f;
	rand = (rand * mult) % mod;		float volZ = (((float) rand) / scale) - .5f;

	float4 inCurveScale = rainInfo->m_inCurveScale * float4(volX, volY, volZ, 0.0);


	base += inCurveScale;
		
	//
	// 2D Motion blur
	// 
	float  pgtime   = rainInfo->m_pptime + magtime;
	float4 basePrev = rainInfo->m_csize * EvalCurvePtr(rainInfo, pgtime);

	basePrev.xyz = (quadId < 20) ? basePrev.xyz : ((quadId < 40) ? basePrev.yzx : ((quadId < 60) ? basePrev.zxy : basePrev.xzy));

	basePrev += cell.m_cellPrevPos; 
	basePrev += inCurveScale + rainInfo->m_cameraBlurMult * cameraDiff;

	// Blur only if flake is closer than 'motionDistance'
	float distanceToCamera = length(base.xyz - rainInfo->m_cameraPos.xyz);
	// Blur = 0 if distance > motionBlurDistance

	// THIS IS WRONG!!!!
	// The computation of the blur factors is set for T1 but it should be using 
	// the StaticLinStep2 instead!!!
	float blur0, blur1;
	blur0 = 1.0f / (blurDistanceMin - rainInfo->m_motionBlurDistance);
	blur1 = -rainInfo->m_motionBlurDistance * blur0;
	float blur = rainInfo->m_motionBlurActive * StaticLinStep(distanceToCamera, blur0, blur1);
		//StaticLinStep(distanceToCamera, 0.75f * rainInfo->m_motionBlurDistance, rainInfo->m_motionBlurDistance);

	// Trick!
	// Transform the previous position (in time) of the flake into the last camera view
	// then transform that view position into the new camera world and use that position as a blur
	// The position is wrong! but it does give a nicer feeling for the blur
		
	// previous position in view space.  world to view (previous camera)
	basePrev.w = 1.0;
	basePrev = mul(basePrev, transpose(rainInfo->m_worldToViewPrev));
		
	base.w = 1.0;
	// new position in view space
	float4 baseView = mul(base, transpose(rainInfo->m_worldToView));
				
	// Compute distance in view space from previous position to current position
	// Limit the amount of stretch based in 
	float4 diffView = basePrev - baseView;
	float distanceInView = length(diffView.xyz);

	distanceInView = min(distanceInView, maxViewStretch);
	diffView.xyz = normalize(diffView.xyz);

	basePrev = baseView + distanceInView * diffView;

	basePrev.w = 1.0;
	// previous position in current camera (world space)
	basePrev = mul(basePrev, transpose(rainInfo->m_viewToWorld));

	// Stretch even more the motion blur
	float4 diff = basePrev - base;

	float newHeight = length(diff);
	float4 diffN = normalize(diff);
		
	// Check that there is some minimum height
	// newHeight might be small if there is little motion, so want to force a particle size (m_height)
	float minHeight = max(newHeight, rainInfo->m_height);
	basePrev = base + (rainInfo->m_motionBlurCurve * minHeight ) * diffN;
	//basePrev = base + rainInfo->m_motionBlurCurve * diff;
		
	float4 dirTravel = normalize(float4(basePrev.xyz - base.xyz, 0.0));

	// Limit the amount of motion blur for particles that are facing the camera!
	// Uses a dot product limit!
	basePrev = lerp( base + up, basePrev, blur);
	float cameraDistance = abs(dot(dirTravel.xyz, rainInfo->m_cameraDir.xyz));
	basePrev = lerp( basePrev, base + up, RainLinStep( cameraDistance, rainInfo->m_motionBlurThreshold0, rainInfo->m_motionBlurThreshold1));

	float4 newRight = rainInfo->m_width * float4(normalize(cross(basePrev.xyz - base.xyz, rainInfo->m_cameraDir.xyz)), 0); //  base - cameraPos
	newRight = lerp(right, newRight, blur);

	// Alpha
	float alpha00,  alpha10,  alpha11,  alpha01 = 1;

	alpha00 = alpha10 = alpha11 = alpha01 = 1;

	alpha11 = (rainInfo->m_motionBlurAlphaActive)? 1.f : rainInfo->m_motionBlurAlpha;
	alpha01 = (rainInfo->m_motionBlurAlphaActive)? 1.f : rainInfo->m_motionBlurAlpha;

	float iblur = 1.f - blur; 
	alpha00 = (rainInfo->m_motionBlurAlphaActive)? iblur : rainInfo->m_motionBlurAlpha;
	alpha10 = (rainInfo->m_motionBlurAlphaActive)? iblur : rainInfo->m_motionBlurAlpha;

	float4 position = float4(0,0,0,1);
	float2 uv=0;
	float  alpha=1;
	float3 color=0;

	/// Texture selection
	float  texQuadId = (float) (quadId % (uint) rainInfo->m_numTextureSlots); // (quadId + cell.m_cellPeriodId) % (uint) rainInfo->m_numTextureSlots;

	float uoff0 = texQuadId * rainInfo->m_numTextureSlotsInv; 
	float uoff1 = uoff0 + rainInfo->m_numTextureSlotsInv;


	if ((vertexId % 4) == 0) {
		position = float4(base.xyz, 1.0);
		uv = float2(uoff0, 0);
		alpha = alpha00;
	} 
	else if ((vertexId % 4) == 1) {
		position = float4(base.xyz + newRight.xyz, 1.0);
		uv = float2(uoff1, 0);
		alpha = alpha10;
	} 
	else if ((vertexId % 4) == 2) {
		position = float4(basePrev.xyz + newRight.xyz, 1.0);
		uv = float2(uoff1, 1);
		alpha = alpha11;
	} 
	else if ((vertexId % 4) == 3) {
		position = float4(basePrev.xyz, 1.0);
		uv = float2(uoff0, 1);
		alpha = alpha01;
	}

	color.rgb = rainInfo->m_ambientOverrideColor * rainInfo->m_rainAmbientColor.rgb;
	
 	if (rainInfo->m_numLights > 0) {
		color += CalculateLightingPtr(position.xyz, rainInfo, srt.m_texturesAndSamplers);
	}

	alpha *= rainInfo->m_alphaMult;
	alpha *= smoothstep(rainInfo->m_alphaFadeInStart, rainInfo->m_alphaFadeInStart + rainInfo->m_alphaFadeInDistance, distanceToCamera);
	alpha *= smoothstep(rainInfo->m_alphaFadeOutStart, rainInfo->m_alphaFadeOutStart - rainInfo->m_alphaFadeOutDistance, distanceToCamera);
	
	color.rgb = lerp(color.rgb, rainInfo->m_rainColor.rgb, rainInfo->m_overrideColor);

	//=================//
	// Output Position //
	//=================//
	OUT.hPosition.xyzw = posToProj4(position.xyz, rainInfo->m_mVP);
	OUT.wPosition	   	= position;
	OUT.uv			   	= uv;
	OUT.color		   	= float4(color, alpha);
	OUT.rainMisc		= 0.0f;

	return OUT;
}

//
// TEST!!!
//
// 

RainVertexShaderOutput Vs_Rain3DMotionBlurTest(uint vertexId, uint instanceId, RainSrtData srt)
{
	RainVertexShaderOutput OUT;

	CellInfoBuffer cell = srt.m_cells[instanceId];
	RainInfoBuffer *rainInfo = srt.m_consts;


	float4 right = rainInfo->m_width * rainInfo->m_cameraRight;
	float4 up = rainInfo->m_height  * float4(cross(rainInfo->m_cameraRight.xyz, rainInfo->m_cameraDir.xyz), 0);

	float maxViewStretch  = rainInfo->m_motionBlurCell;
	float maxViewStretch2 = 4.f * rainInfo->m_motionBlurCell / 5.f;

	float blurDistanceMin = 0.75f * rainInfo->m_motionBlurDistance;

	uint mult 	= 16807;
	uint mod  	= 2147483647 -1 ;             // 2^31 - 1
	float scale = float(mod - 1);
	//	float ptime = rainInfo->m_time * rainInfo->m_period;

	// Blend between prev and current time
	//	float pptime = (rainInfo->m_time - rainInfo->m_deltaTime) * rainInfo->m_period;  
	//	float csize   = rainInfo->m_cellSpacing * rainInfo->m_cellSize;
	//	float quadMax	= max(rainInfo->m_width, rainInfo->m_height;


	float numSpans = 32.0f; // kSnowNumSpans;

	uint quadId = vertexId / 4; // infer the quad from the vertex index

	// Seed the random value by the position of the cell and
	uint rand =  (uint) cell.m_cellRandId + 133 * (quadId + 1);

	rand = (rand * mult) % mod;
	float magtime = 27.3729f * rainInfo->m_randomScale * numSpans * ((float) rand / scale);
	float gtime   = rainInfo->m_ptime + magtime;

	// Base point of flake
	float4 base = rainInfo->m_csize * EvalCurvePtr(rainInfo, gtime);
	base.xyz = (quadId < 20) ? base.xyz : ((quadId < 40) ? base.yzx : ((quadId < 60) ? base.zxy : base.xzy));

	base += cell.m_cellPosition;

	// Position of flake in curve
	rand = (rand * mult) % mod;		float volX = (((float) rand) / scale) - .5f;
	rand = (rand * mult) % mod;		float volY = (((float) rand) / scale) - .5f;
	rand = (rand * mult) % mod;		float volZ = (((float) rand) / scale) - .5f;

	float4 inCurveScale = rainInfo->m_inCurveScale * float4(volX, volY, volZ, 0.0);


	base += inCurveScale;
		
	//
	// 2D Motion blur
	// 
	float  pgtime   = rainInfo->m_pptime + magtime;
	float4 basePrev = rainInfo->m_csize * EvalCurvePtr(rainInfo, pgtime);

	basePrev.xyz = (quadId < 20) ? basePrev.xyz : ((quadId < 40) ? basePrev.yzx : ((quadId < 60) ? basePrev.zxy : basePrev.xzy));

	basePrev += cell.m_cellPrevPos; 
	basePrev += inCurveScale;

	// Blur only if flake is closer than 'motionDistance'
	float distanceToCamera = length(base.xyz - rainInfo->m_cameraPos.xyz);
	// Blur = 0 if distance > motionBlurDistance

	// THIS IS WRONG!!!!
	// The computation of the blur factors is set for T1 but it should be using 
	// the StaticLinStep2 instead!!!
	float blur0, blur1;
	blur0 = 1.0f / (blurDistanceMin - rainInfo->m_motionBlurDistance);
	blur1 = -rainInfo->m_motionBlurDistance * blur0;
	float blur = rainInfo->m_motionBlurActive * StaticLinStep(distanceToCamera, blur0, blur1);
		//StaticLinStep(distanceToCamera, 0.75f * rainInfo->m_motionBlurDistance, rainInfo->m_motionBlurDistance);

	// Stretch even more the motion blur
	float4 diff = basePrev - base;

	float newHeight = length(diff);
	float4 diffN = normalize(diff);
		
	// Check that there is some minimum height
	// newHeight might be small if there is little motion, so want to force a particle size (m_height)
	float minHeight = max(newHeight, rainInfo->m_height);
	basePrev = base + (rainInfo->m_motionBlurCurve * minHeight ) * diffN;
		
	float4 dirTravel = normalize(float4(basePrev.xyz - base.xyz, 0.0));

	// Limit the amount of motion blur for particles that are facing the camera!
	// Uses a dot product limit!
	basePrev = lerp( base + up, basePrev, blur);

	basePrev = lerp( basePrev, base + up, RainLinStep( abs(dot(dirTravel.xyz, rainInfo->m_cameraDir.xyz)), rainInfo->m_motionBlurThreshold0, rainInfo->m_motionBlurThreshold1));

	float4 newRight = rainInfo->m_width * float4(normalize(cross(basePrev.xyz - base.xyz, rainInfo->m_cameraDir.xyz)), 0); //  base - cameraPos
	newRight = lerp(right, newRight, blur);

	// Alpha
	float alpha00,  alpha10,  alpha11,  alpha01 = 1;

	alpha00 = alpha10 = alpha11 = alpha01 = 1;

	alpha11 = (rainInfo->m_motionBlurAlphaActive)? 1.f : rainInfo->m_motionBlurAlpha;
	alpha01 = (rainInfo->m_motionBlurAlphaActive)? 1.f : rainInfo->m_motionBlurAlpha;

	float iblur = 1.f - blur; 
	alpha00 = (rainInfo->m_motionBlurAlphaActive)? iblur : rainInfo->m_motionBlurAlpha;
	alpha10 = (rainInfo->m_motionBlurAlphaActive)? iblur : rainInfo->m_motionBlurAlpha;

	float4 position = float4(0,0,0,1);
	float2 uv=0;

	float  alpha=(rainInfo->m_motionBlurAlphaActive)? 1.f : rainInfo->m_motionBlurAlpha;

	/// Texture selection
	float  texQuadId = (float) (quadId % (uint) rainInfo->m_numTextureSlots); // (quadId + cell.m_cellPeriodId) % (uint) rainInfo->m_numTextureSlots;

	float uoff0 = texQuadId * rainInfo->m_numTextureSlotsInv; 
	float uoff1 = uoff0 + rainInfo->m_numTextureSlotsInv;

	float3 color=0;

	if ((vertexId % 4) == 0) {
		position = float4(base.xyz, 1.0);
		uv = float2(uoff0, 0);
		//alpha = alpha00;
		color = alpha00; // alpha = 1.0; // float3(0,0,0);
	} 
	else if ((vertexId % 4) == 1) {
		position = float4(base.xyz + newRight.xyz, 1.0);
		uv = float2(uoff1, 0);
		//alpha = alpha10;
		color = alpha10; // alpha = 1.0; // float3(1,0,0);
	} 
	else if ((vertexId % 4) == 2) {
		position = float4(basePrev.xyz + newRight.xyz, 1.0);
		uv = float2(uoff1, 1);
		//alpha = alpha11;
		color = alpha11; // alpha = 1.0; // float3(1,1,0);
	} 
	else if ((vertexId % 4) == 3) {
		position = float4(basePrev.xyz, 1.0);
		uv = float2(uoff0, 1);
		//alpha = alpha01;
		color = alpha01; // alpha = 1.0; // float3(0,1,0);
	}

	color.rgb = rainInfo->m_ambientOverrideColor * rainInfo->m_rainAmbientColor.rgb;

	if (rainInfo->m_numLights > 0) {
		// THIS IS FOR TESTING. We are adding the lighting to the alpha
		color += CalculateLightingPtr(position.xyz, rainInfo, srt.m_texturesAndSamplers);
	}
	color.b = .5;

	alpha *= rainInfo->m_alphaMult;
	alpha *= smoothstep(rainInfo->m_alphaFadeInStart, rainInfo->m_alphaFadeInStart + rainInfo->m_alphaFadeInDistance, distanceToCamera);
	alpha *= smoothstep(rainInfo->m_alphaFadeOutStart, rainInfo->m_alphaFadeOutStart - rainInfo->m_alphaFadeOutDistance, distanceToCamera);
	
	color.rgb = lerp(color.rgb, rainInfo->m_rainColor.rgb, rainInfo->m_overrideColor);

	//=================//
	// Output Position //
	//=================//
	OUT.hPosition.xyzw = posToProj4(position.xyz, rainInfo->m_mVP);
	OUT.wPosition	   	= position;
	OUT.uv			   	= uv;
	OUT.color		   	= float4(color, alpha);
	OUT.rainMisc		= 0.0f;

	return OUT;
}

RainVertexShaderOutput Vs_Rain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID, RainSrtData srt : S_SRT_DATA)
{
	return Vs_Rain3DMotionBlurTest(vertexId, instanceId, srt);
	// Vs_RainTest
	//Vs_RainTest
}

