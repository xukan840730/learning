/*
 * Copyright (c) 2016 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "packing.fxi"
#include "global-funcs.fxi"
#include "post-globals.fxi"
#include "global-const-buffers.fxi"

//------------------------------------------------------------------------------------------------------------
// 2D Vector Field Visualization, shamelessly stolen from Morgan McGuire at https://www.shadertoy.com/view/4s23DG

struct VectorFieldVisualizationSrt
{
	Texture2D<float4>	fieldTex;
	SamplerState		linearSampler;
	float4				screenParams;
	float4				arrowParams;
};

float AnalyticArrow(float2 p, float2 v, float tileSize, float headAngle, float headLength, float shaftThickness)
{
    float mag_v = length(v);
	float mag_p = length(p);
	
	if (mag_v > 0.0)
	{
		float2 dir_p = p / mag_p, dir_v = v / mag_v;

		mag_v = clamp(mag_v, 5.0, tileSize / 2.0);
		v = dir_v * mag_v;

		float dist = 
				max(
					// Shaft
					shaftThickness / 4.0 - 
						max(abs(dot(p, float2(dir_v.y, -dir_v.x))),								// Width
						    abs(dot(p, dir_v)) - mag_v + headLength / 2.0),						// Length
						
   			         // Arrow head
					 min(0.0, dot(v - p, dir_v) - cos(headAngle / 2.0) * length(v - p)) * 2.0 + // Front sides
					 min(0.0, dot(p, dir_v) + headLength - mag_v));								// Back

		return saturate(1.0 + dist);
	}
	else
	{
		return max(0.0, 1.2 - mag_p);
	}
}

float2 GetMotionVector(VectorFieldVisualizationSrt* pSrt, float2 uvCoord)
{
	float2 motionVector = pSrt->fieldTex.Sample(pSrt->linearSampler, uvCoord).xy;
	return motionVector * float2(2.0, -2.0) * pSrt->screenParams.xy;
}

float4 PS_VectorFieldVisualization(VectorFieldVisualizationSrt* pSrt : S_SRT_DATA, PS_PosTex input) : SV_Target
{
	float2 screenSize     = pSrt->screenParams.xy;
	float tileSize        = pSrt->arrowParams.x;
	float arrowHeadAngle  = pSrt->arrowParams.y;
	float arrowHeadLength = pSrt->arrowParams.z;
	float arrowThickness  = pSrt->arrowParams.w;

	float2 pixCoord = input.Tex * screenSize;
	float2 tileCenter = (floor(pixCoord / tileSize) + 0.5) * tileSize;

	float2 tileSample  = GetMotionVector(pSrt, tileCenter / screenSize);
	float2 fieldSample = GetMotionVector(pSrt, input.Tex);

	float2 tilePos = pixCoord - tileCenter;

	float arrow = AnalyticArrow(tilePos, tileSample * tileSize * 0.4, tileSize, arrowHeadAngle, arrowHeadLength, arrowThickness);

	return float4((1.0 - arrow) * float3(abs(fieldSample) / 10.0f, 0.0), 1.0);
}
