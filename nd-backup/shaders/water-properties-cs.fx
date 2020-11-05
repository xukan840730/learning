
struct FlowWaterPropertiesSrt
{
	float4				timeRates;		// Contains (delta time, foam elimination rate, churn elimination rate, diffusion rate)
	float4				depthRates;		// Contains (depth decay, algae decay, 0 ,0)
	float4				info;			// Contains (camera X delta, camera Z delta, tex scale, 1/size)
	Texture2D<float2>	txFlow;			// The flow texture
	Texture2D<float3>	txSrcFoamChurnAlgae;	// The source foam and churn texture
	RWTexture2D<float3>	txDstFoamChurnAlgae;	// The destination foam and churn texture
	Texture2D<float>	txSrcHeight;	// The source water height texture
	RWTexture2D<float>	txDstHeight;	// The destination water height texture
	SamplerState		smLinear;		// Linear texture sampler
};

[numthreads(8, 8, 1)]
void CS_FlowWaterProperties(uint3 dispatchThreadId : SV_DispatchThreadID, FlowWaterPropertiesSrt *pSrt : S_SRT_DATA)
{
	// This compute shader is executed in wavefronts of 8x8 pixels.
	// If you need to access the destination texture after the source has been "flowed"
	// into it, you must add a GroupMemoryBarrierWithGroupSync() before doing so to make
	// sure that all wavefronts have completed their work on the destination texture.
	uint2 pixcoord = dispatchThreadId.xy;
	float dt = pSrt->timeRates.x;
	float texScale = pSrt->info.z;
	float invTexSize = pSrt->info.w;
	float diffuseRate = pSrt->timeRates.w;
	float depthRate  = pSrt->depthRates.x;
	float algaeRate = pSrt->depthRates.y;

	float texSize = 1.0f / invTexSize;
	float texelsPerMeter = texSize / (2.0f * texScale);

	// First, calculate the "flow" from the camera's movement delta
	// Camera delta (worldspace) / texture scale.  ( * 0.5 because world units are (-1 - 1), while texels are (0 - 1) )
	float2 camDeltaMeters = pSrt->info.xy;
	float2 camDeltaTexels = camDeltaMeters * texelsPerMeter;
	float2 camDeltaUv = camDeltaTexels * invTexSize;

	// The flow texture contains where each pixel should move to, and we negate it
	// to figure out where to pull the data for this pixel from.
	float2 flow = pSrt->txFlow[pixcoord] * dt / texScale;


	float2 baseUv = (((float2)pixcoord + 0.5) * invTexSize);
	float2 srcUv = baseUv - flow - camDeltaUv;

	// And now we read the foam and churn from the source texture
	float3 foamChurnAlgae = pSrt->txSrcFoamChurnAlgae.SampleLevel(pSrt->smLinear, srcUv, 0);
	
	// pass throu the water height. no decay
	float height = pSrt->txSrcHeight.SampleLevel(pSrt->smLinear, baseUv, 0);

	// Blur churn buffer 
	if (diffuseRate > 0.01 && dt > 0.001)
	{
		texelsPerMeter *= diffuseRate / texSize;

		float churnDiffusion = pSrt->txSrcFoamChurnAlgae.SampleLevel(pSrt->smLinear, srcUv + float2(-texelsPerMeter,-texelsPerMeter), 0).y;
		churnDiffusion += pSrt->txSrcFoamChurnAlgae.SampleLevel(pSrt->smLinear, srcUv + float2(0,-texelsPerMeter), 0).y;
		churnDiffusion += pSrt->txSrcFoamChurnAlgae.SampleLevel(pSrt->smLinear, srcUv + float2(texelsPerMeter,-texelsPerMeter), 0).y;

		churnDiffusion += pSrt->txSrcFoamChurnAlgae.SampleLevel(pSrt->smLinear, srcUv + float2(-texelsPerMeter,0), 0).y;
		churnDiffusion += pSrt->txSrcFoamChurnAlgae.SampleLevel(pSrt->smLinear, srcUv + float2(texelsPerMeter,0), 0).y;

		churnDiffusion += pSrt->txSrcFoamChurnAlgae.SampleLevel(pSrt->smLinear, srcUv + float2(-texelsPerMeter,texelsPerMeter), 0).y;
		churnDiffusion += pSrt->txSrcFoamChurnAlgae.SampleLevel(pSrt->smLinear, srcUv + float2(0,texelsPerMeter), 0).y;
		churnDiffusion += pSrt->txSrcFoamChurnAlgae.SampleLevel(pSrt->smLinear, srcUv + float2(texelsPerMeter,texelsPerMeter), 0).y;

		churnDiffusion /= 8.0;

		foamChurnAlgae.y = churnDiffusion;
	}
	// Use the elimination rates to eliminate foam and churn values
	float foamRate = pSrt->timeRates.y;
	float churnRate = pSrt->timeRates.z;

	float3 rates = float3( pSrt->timeRates.y,  pSrt->timeRates.z, pSrt->depthRates.y);
	foamChurnAlgae.xyz *= 1 - (rates * 6 * dt);


	// And write the final value!
	pSrt->txDstFoamChurnAlgae[pixcoord] = saturate(foamChurnAlgae.xyz);
	pSrt->txDstHeight[pixcoord] = height;
}
