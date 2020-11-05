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



struct ParticleDebugSrt
{
	RWTexture2D<float4>		m_destTexture0;
	RWTexture2D<float2>		m_destTexture1;
	Texture2D<float>		m_depth_buffer;
	StructuredBuffer<ParticleInstance> m_particleInstances;
	SamplerState			m_linearSampler;
	float4x4				g_mVP;
	float3					m_altWorldOrigin;
	float					m_alphaThreshold;
	uint					m_numRts;
	uint					m_numParts;
	int						m_mipLevel;
	uint					m_flags;
	float					m_time;
};

void SetPixel(uint2 xy, float4 v, int it, ParticleDebugSrt *pSrt)
{
	if (it == 0)
	{
		pSrt->m_destTexture0[xy] = v;
	}
	else
	{
		pSrt->m_destTexture1[xy] = v.rg;
	}
}

uint2 GetDim(int it, ParticleDebugSrt *pSrt)
{
	uint2 texDim0;

	if (it == 0)
	{
		pSrt->m_destTexture0.GetDimensions(texDim0.x, texDim0.y);

	}
	else
	{
		pSrt->m_destTexture1.GetDimensions(texDim0.x, texDim0.y);

	}

	return texDim0;
}

[NUM_THREADS(64, 1, 1)]
void CS_DebugParticles(const uint2 dispatchId : SV_DispatchThreadID,
	const uint3 groupThreadId : SV_GroupThreadID,
	const uint2 groupId : SV_GroupID,
	ParticleDebugSrt *pSrt : S_SRT_DATA)
{
	int iPart = dispatchId.x;

	uint2 depthDim;


	pSrt->m_depth_buffer.GetDimensions(depthDim.x, depthDim.y);

	uint DW = depthDim.x;
	uint DH = depthDim.y;


	if (iPart < pSrt->m_numParts)
	{
		ParticleInstance inst = pSrt->m_particleInstances[pSrt->m_numParts - iPart - 1];

		// check if this whole block intersects this particle
		float4 p0 = float4(0.5, 0.5, 0, 1);
		float4 p1 = float4(0.5, -0.5, 0, 1);
		float4 p2 = float4(-0.5, -0.5, 0, 1);
		float4 p3 = float4(-0.5, 0.5, 0, 1);


		p0 = float4((mul(p0, inst.world).xyz + pSrt->m_altWorldOrigin), 1.0);
		p1 = float4((mul(p1, inst.world).xyz + pSrt->m_altWorldOrigin), 1.0);
		p2 = float4((mul(p2, inst.world).xyz + pSrt->m_altWorldOrigin), 1.0);
		p3 = float4((mul(p3, inst.world).xyz + pSrt->m_altWorldOrigin), 1.0);


		//p0 = float4((inst.world[3].xyz + pSrt->m_altWorldOrigin), 1.0);
		//p1 = float4((inst.world[3].xyz + pSrt->m_altWorldOrigin), 1.0);
		//p2 = float4((inst.world[3].xyz + pSrt->m_altWorldOrigin), 1.0);
		//p3 = float4((inst.world[3].xyz + pSrt->m_altWorldOrigin), 1.0);

		p0 = mul(p0, (pSrt->g_mVP));
		p1 = mul(p1, (pSrt->g_mVP));
		p2 = mul(p2, (pSrt->g_mVP));
		p3 = mul(p3, (pSrt->g_mVP));

		p0.x = (p0.x / p0.w) * 0.5f + 0.5f; // -1 .. 1 -> 0 .. 1
		p0.y = (-p0.y / p0.w) * 0.5f + 0.5f;

		p1.x = (p1.x / p1.w) * 0.5f + 0.5f;
		p1.y = (-p1.y / p1.w) * 0.5f + 0.5f;

		p2.x = (p2.x / p2.w) * 0.5f + 0.5f;
		p2.y = (-p2.y / p2.w) * 0.5f + 0.5f;

		p3.x = (p3.x / p3.w) * 0.5f + 0.5f;
		p3.y = (-p3.y / p3.w) * 0.5f + 0.5f;

		// find bbox
		float2 fminP = min(p0.xy, min(p1.xy, min(p2.xy, p3.xy)));
		float2 fmaxP = max(p0.xy, max(p1.xy, max(p2.xy, p3.xy)));

		// find barycentric coordinates for each corner of the bbox
		float2 p0uv = float2(1, 1);
		float2 p1uv = float2(1, 0);
		float2 p2uv = float2(0, 0);


		float2 minUv;
		float2 dUvAlongX;
		float2 dUvAlongY;


		float4 bary = FindBarycentric(p0, p1, p2, fminP);
			float2 uv = p0uv * bary.x + p1uv * bary.y + p2uv * bary.z;
			minUv = uv;

		bary = FindBarycentric(p0, p1, p2, float2(fmaxP.x, fminP.y));
		float2 uv10 = p0uv * bary.x + p1uv * bary.y + p2uv * bary.z;
			dUvAlongX = (uv10 - minUv) / (fmaxP.x - fminP.x);

		bary = FindBarycentric(p0, p1, p2, float2(fminP.x, fmaxP.y));
		float2 uv01 = p0uv * bary.x + p1uv * bary.y + p2uv * bary.z;
			dUvAlongY = (uv01 - minUv) / (fmaxP.y - fminP.y);


		for (int it = 0; it < pSrt->m_numRts; ++it)
		{
			uint2 texDim0 = GetDim(it, pSrt);

				float W = texDim0.x;
			float H = texDim0.y;

			int2 minP = int2(fminP * texDim0);
				int2 maxP = int2(fmaxP * texDim0);

				for (int y = minP.y; y <= maxP.y; ++y)
				{
					for (int x = minP.x; x <= maxP.x; ++x)
					{
						float2 thisUv = minUv + (x - minP.x)  * dUvAlongX / W + (y - minP.y) * dUvAlongY / H; // could premultiply earlier if all rts are same resolution
							float4 c = float4(thisUv.xy, 0.0, 1.0);

							SetPixel(int2(x, y), c, it, pSrt);
					}
				}


			float2 sp0 = float2(p0.x * W, p0.y * H);
				float2 sp1 = float2(p1.x * W, p1.y * H);
				float2 sp2 = float2(p2.x * W, p2.y * H);
				float2 sp3 = float2(p3.x * W, p3.y * H);

				float2 dsp0 = float2(p0.x * DW, p0.y * DH);
				float2 dsp1 = float2(p1.x * DW, p1.y * DH);
				float2 dsp2 = float2(p2.x * DW, p2.y * DH);
				float2 dsp3 = float2(p3.x * DW, p3.y * DH);

				float d = pSrt->m_depth_buffer[int2(dsp0)];

			for (int i = 0; i <= 100; ++i)
			{
				float f = float(i) / 100.0f;
				float4 c = float4(1.0f, 1.0f, 1.0f, 1.0f) * i / 100.0f;



					SetPixel(int2(lerp(sp0.xy, sp1.xy, f)), c, it, pSrt);
				SetPixel(int2(lerp(sp1.xy, sp2.xy, f)), c, it, pSrt);
				SetPixel(int2(lerp(sp2.xy, sp3.xy, f)), c, it, pSrt);
				SetPixel(int2(lerp(sp3.xy, sp0.xy, f)), c, it, pSrt);
				//SetPixel(int2(lerp(sp0.xy, sp2.xy, f)), c, it, pSrt);
				SetPixel(int2(lerp(sp1.xy, sp3.xy, f)), c, it, pSrt);
			}
		}
	}
}

