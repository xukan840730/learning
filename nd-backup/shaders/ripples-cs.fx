#include "global-funcs.fxi"
//
// Compute Shader - water ripples
//

//#define kMaxVertexBlocks	1024
#define kMaxThreadX 32
#define kMaxThreadY 32

struct RipplesConsts
{
	uint   m_active; 
	uint   m_rippleType;
	float m_rippleScale;
	uint  m_setGridToZero;

	int   m_dux;        // movement
	int   m_duz;
	uint  m_num;        // num of grid elements per side
	uint  m_size;       // total size of elements in grid

	float m_kA;  // constant A
	float m_kC;  // constant C
	float m_bumpStrength;
	float m_heightStrength; 

	float m_clampHeight;
	float m_num2;
	float m_scale2;
	float m_scalePerUnit;
	
	uint  m_numImpulses;
	uint  m_numLineImpulses;
	float m_lineWidth;
	float m_paused; // not used

	float m_fpx; // fix_position.x
	float m_fpz; // fix_position.x
	float m_deltaTime;
	float m_deltaFactor;

	float4  m_impulseIndex[40];
	float4  m_lineImpulse[40];
	float4  m_impulseForce[40]; // shared between (.x) single and (.y) line impulses 
};

struct RipplesBufs 
{
	StructuredBuffer<float>      inputGridT1Data; // 	: register(t0);
	StructuredBuffer<float>      inputGridT2Data; // 	: register(t1);
	RWStructuredBuffer<float>    gridT0Buffer; // 	: register(u0);
};

struct RipplesSrt
{
	RipplesConsts *pConsts;
	RipplesBufs *pBufs;
};
	
[numthreads(kMaxThreadX, kMaxThreadY, 1)] 
void Cs_Ripples(uint2 dispatchThreadId : SV_DispatchThreadID,
                RipplesSrt srt : S_SRT_DATA)
{
	int r1 = dispatchThreadId.x; // current row  
	int c1 = dispatchThreadId.y; // current col

	float2 ptRC2 = float2(float(c1), float(r1)) * srt.pConsts->m_scalePerUnit +
	               float2(srt.pConsts->m_fpx, srt.pConsts->m_fpz) +
	               float2(-srt.pConsts->m_scale2, -srt.pConsts->m_scale2);

	float kA = srt.pConsts->m_kA;
	float kC = srt.pConsts->m_kC * srt.pConsts->m_deltaFactor * srt.pConsts->m_deltaTime;

	float kC2 = kC*kC / 2.0f;

	int r0 = (r1 - 1 );
	int r2 = (r1 + 1 );

	
	int c0 = (c1 - 1 );
	int c2 = (c1 + 1 );

	int sindex  = ((r1) * srt.pConsts->m_num) + c1;  // center
	int sindex0 = ((r0) * srt.pConsts->m_num) + c1;  // up
	int sindex1 = ((r1) * srt.pConsts->m_num) + c0;  // left
	int sindex2 = ((r1) * srt.pConsts->m_num) + c2;  // right
	int sindex3 = ((r2) * srt.pConsts->m_num) + c1;  // down

	int sindex00 = ((r0) * srt.pConsts->m_num) + c0;  // up left
	int sindex02 = ((r0) * srt.pConsts->m_num) + c2;  // up right
	int sindex20 = ((r2) * srt.pConsts->m_num) + c0;  // low left
	int sindex22 = ((r2) * srt.pConsts->m_num) + c2;  // low right


	bool vr0 = (r0 >=0 && r0 < srt.pConsts->m_num);		
	bool vr1 = (r1 >=0 && r1 < srt.pConsts->m_num);
	bool vr2 = (r2 >=0 && r2 < srt.pConsts->m_num);

	bool vc0 = (c0 >=0 && c0 < srt.pConsts->m_num);
	bool vc1 = (c1 >=0 && c1 < srt.pConsts->m_num);
	bool vc2 = (c2 >=0 && c2 < srt.pConsts->m_num);

	bool valid  = (vr1 && vc1);
	bool valid0 = (vr0 && vc1);
	bool valid1 = (vr1 && vc0);
	bool valid2 = (vr1 && vc2);
	bool valid3 = (vr2 && vc1);

	uint index  = valid  ? sindex  : 0;
	uint index0 = valid0 ? sindex0 : 0;
	uint index1 = valid1 ? sindex1 : 0;
	uint index2 = valid2 ? sindex2 : 0;
	uint index3 = valid3 ? sindex3 : 0;


	bool valid00 = (vr0 && vc0); // up left
	bool valid02 = (vr0 && vc2); // up right
	bool valid20 = (vr2 && vc0); // low left
	bool valid22 = (vr2 && vc2); // low right

	uint index00 = valid00 ? sindex00 : 0;
	uint index02 = valid02 ? sindex02 : 0;
	uint index20 = valid20 ? sindex20 : 0;
	uint index22 = valid22 ? sindex22 : 0;

	float gt1   = valid  ? srt.pBufs->inputGridT1Data[index] : 0.0;    
	float gt2   = valid  ? srt.pBufs->inputGridT2Data[index] : 0.0;    
	float gt1p0 = valid0 ? srt.pBufs->inputGridT1Data[index0] : 0.0; 
	float gt1p1 = valid1 ? srt.pBufs->inputGridT1Data[index1] : 0.0; 
	float gt1p2 = valid2 ? srt.pBufs->inputGridT1Data[index2] : 0.0; 
	float gt1p3 = valid3 ? srt.pBufs->inputGridT1Data[index3] : 0.0; 

	float gt1p00 = valid00 ? srt.pBufs->inputGridT1Data[index00] : 0.0; 
	float gt1p02 = valid02 ? srt.pBufs->inputGridT1Data[index02] : 0.0; 
	float gt1p20 = valid20 ? srt.pBufs->inputGridT1Data[index20] : 0.0; 
	float gt1p22 = valid22 ? srt.pBufs->inputGridT1Data[index22] : 0.0; 

	// float value = kA * (2.0 * gt1 - gt2) +  kC2 * ( gt1p0 + gt1p1 + gt1p2 + gt1p3 - 4.0f * gt1);
	float value = kA * (2.0 * gt1 - gt2) +
	              kC2 * (gt1p0 + gt1p1 + gt1p2 + gt1p3 + 1.41421356237310f * (gt1p00 + gt1p02 + gt1p20 + gt1p22) -
	                     9.65685424949238f * gt1);

	uint i;
	for (i = 0; i < srt.pConsts->m_numImpulses; i++)
	{
		float len = length(float2(srt.pConsts->m_impulseIndex[i].x, srt.pConsts->m_impulseIndex[i].y) - ptRC2);
		value = (len <= srt.pConsts->m_lineWidth) ? srt.pConsts->m_impulseForce[i].x : value;
	}

	for (i=0; i < srt.pConsts->m_numLineImpulses; i++)  {
		// value = ((srt.pConsts->m_lineImpulse[i].x <= c1) && (c1 <= srt.pConsts->m_lineImpulse[i].y) && (srt.pConsts->m_lineImpulse[i].z <= r1) && (r1 <= srt.pConsts->m_lineImpulse[i].w)) ? 1.0 : value;
		float2 p0 = float2(srt.pConsts->m_lineImpulse[i].x, srt.pConsts->m_lineImpulse[i].y);
		float2 p1 = float2(srt.pConsts->m_lineImpulse[i].z, srt.pConsts->m_lineImpulse[i].w);
		float2 v = (p1 - p0);
		float  n = length(v);
		v = normalize(v);
		float2 q = ptRC2; //float2(c1, r1);
		float2 w = q - p0;
		float  d = dot(v,w);
		float2 r = p0 + v * d;
		float  dr = length(q - r);
		if ((d >=0) && (d <= n) && (dr < srt.pConsts->m_lineWidth)) {
		 	value = srt.pConsts->m_impulseForce[i].y;
		}
	}

	srt.pBufs->gridT0Buffer[index] = clamp(value, -1.0f, 1.0f);
}

// Simulate ripples, does wrap
// Does not move grid
// This is for rain
// Uses a different logic for the impulses. Using indices
[numthreads(kMaxThreadX, kMaxThreadY, 1)]
void Cs_RipplesWrap(uint2 dispatchThreadId : SV_DispatchThreadID,
                    RipplesSrt srt : S_SRT_DATA)
{
	int r1 = dispatchThreadId.x; // current row  
	int c1 = dispatchThreadId.y; // current col

	float2 ptRC2 = float2(float(c1), float(r1)) * srt.pConsts->m_scalePerUnit +
	               float2(srt.pConsts->m_fpx, srt.pConsts->m_fpz) +
	               float2(-srt.pConsts->m_scale2, -srt.pConsts->m_scale2);

	float kA = srt.pConsts->m_kA;
	float kC = srt.pConsts->m_kC * srt.pConsts->m_deltaFactor * srt.pConsts->m_deltaTime;

	float kC2 = kC*kC / 2.0f;

	int r0 = (r1 + srt.pConsts->m_num - 1 ) % srt.pConsts->m_num;
	int r2 = (r1 + 1 ) % srt.pConsts->m_num;


	//	for (int c1=0; c1 < srt.pConsts->m_num ; c1++) 
	
	int c0 = (c1 + srt.pConsts->m_num - 1 ) % srt.pConsts->m_num;
	int c2 = (c1 + 1  ) % srt.pConsts->m_num;

	uint index = (r1 * srt.pConsts->m_num) + c1;  // up
	uint index0 = (r0 * srt.pConsts->m_num) + c1;  // up
	uint index1 = (r1 * srt.pConsts->m_num) + c0;  // left
	uint index2 = (r1 * srt.pConsts->m_num) + c2;  // right
	uint index3 = (r2 * srt.pConsts->m_num) + c1;  // down

	float gt1   = srt.pBufs->inputGridT1Data[index];
	float gt2   = srt.pBufs->inputGridT2Data[index];
	float gt1p0 = srt.pBufs->inputGridT1Data[index0];
	float gt1p1 = srt.pBufs->inputGridT1Data[index1];
	float gt1p2 = srt.pBufs->inputGridT1Data[index2];
	float gt1p3 = srt.pBufs->inputGridT1Data[index3];
		
	float value = kA * (2.0 * gt1 - gt2) +  kC2 * ( gt1p0 + gt1p1 + gt1p2 + gt1p3 - 4.0f * gt1);
	uint i;

	for (i = 0; i < srt.pConsts->m_numImpulses; i++)
	{
		value =
		    (((uint)srt.pConsts->m_impulseIndex[i].x) == index || ((uint)srt.pConsts->m_impulseIndex[i].y) == index ||
		     ((uint)srt.pConsts->m_impulseIndex[i].z) == index || ((uint)srt.pConsts->m_impulseIndex[i].w) == index)
		        ? srt.pConsts->m_impulseForce[i].x
		        : value;
	}

	// We have an extra function to reset
	// value = (srt.pConsts->m_reset == 1)? 0 : value;

	srt.pBufs->gridT0Buffer[index] = clamp(value, -1.0f, 1.0f);

}

[numthreads(kMaxThreadX, kMaxThreadY, 1)]
void Cs_RipplesClamp(uint2 dispatchThreadId : SV_DispatchThreadID,
                     RipplesSrt srt : S_SRT_DATA)
{
	int r1 = dispatchThreadId.x; // current row  
	int c1 = dispatchThreadId.y; // current col 

	float kA = srt.pConsts->m_kA;
	float kC = srt.pConsts->m_kC * srt.pConsts->m_deltaFactor * srt.pConsts->m_deltaTime;

	float2 ptRC2 = float2(float(c1), float(r1)) * srt.pConsts->m_scalePerUnit +
	               float2(srt.pConsts->m_fpx, srt.pConsts->m_fpz) +
	               float2(-srt.pConsts->m_scale2, -srt.pConsts->m_scale2);

	float kC2 = kC*kC / 2.0f;

	int r0 = (r1 - 1 );
	int r2 = (r1 + 1 );

	
	int c0 = (c1 - 1 );
	int c2 = (c1 + 1 );

	int sindex  = ((r1) * srt.pConsts->m_num) + c1;  // center
	int sindex0 = ((r0) * srt.pConsts->m_num) + c1;  // up
	int sindex1 = ((r1) * srt.pConsts->m_num) + c0;  // left
	int sindex2 = ((r1) * srt.pConsts->m_num) + c2;  // right
	int sindex3 = ((r2) * srt.pConsts->m_num) + c1;  // down

	int sindex00 = ((r0) * srt.pConsts->m_num) + c0;  // up left
	int sindex02 = ((r0) * srt.pConsts->m_num) + c2;  // up right
	int sindex20 = ((r2) * srt.pConsts->m_num) + c0;  // low left
	int sindex22 = ((r2) * srt.pConsts->m_num) + c2;  // low right

	if (r1 == 0 || r1 == (srt.pConsts->m_num -1) || 
		c1 == 0 || c1 == (srt.pConsts->m_num -1)) {
		srt.pBufs->gridT0Buffer[sindex] = 0;
		return;
	}

	bool vr0 = (r0 >=0 && r0 < srt.pConsts->m_num);		
	bool vr1 = (r1 >=0 && r1 < srt.pConsts->m_num);
	bool vr2 = (r2 >=0 && r2 < srt.pConsts->m_num);

	bool vc0 = (c0 >=0 && c0 < srt.pConsts->m_num);
	bool vc1 = (c1 >=0 && c1 < srt.pConsts->m_num);
	bool vc2 = (c2 >=0 && c2 < srt.pConsts->m_num);

	bool valid  = (vr1 && vc1);
	bool valid0 = (vr0 && vc1);
	bool valid1 = (vr1 && vc0);
	bool valid2 = (vr1 && vc2);
	bool valid3 = (vr2 && vc1);

	uint index  = sindex;
	uint index0 = valid0 ? sindex0 : 0;
	uint index1 = valid1 ? sindex1 : 0;
	uint index2 = valid2 ? sindex2 : 0;
	uint index3 = valid3 ? sindex3 : 0;


	bool valid00 = (vr0 && vc0); // up left
	bool valid02 = (vr0 && vc2); // up right
	bool valid20 = (vr2 && vc0); // low left
	bool valid22 = (vr2 && vc2); // low right

	uint index00 = valid00 ? sindex00 : 0;
	uint index02 = valid02 ? sindex02 : 0;
	uint index20 = valid20 ? sindex20 : 0;
	uint index22 = valid22 ? sindex22 : 0;

	float gt1   = valid  ? srt.pBufs->inputGridT1Data[index] : 0.0;    
	float gt2   = valid  ? srt.pBufs->inputGridT2Data[index] : 0.0;    
	float gt1p0 = valid0 ? srt.pBufs->inputGridT1Data[index0] : 0.0; 
	float gt1p1 = valid1 ? srt.pBufs->inputGridT1Data[index1] : 0.0; 
	float gt1p2 = valid2 ? srt.pBufs->inputGridT1Data[index2] : 0.0; 
	float gt1p3 = valid3 ? srt.pBufs->inputGridT1Data[index3] : 0.0; 

	float gt1p00 = valid00 ? srt.pBufs->inputGridT1Data[index00] : 0.0; 
	float gt1p02 = valid02 ? srt.pBufs->inputGridT1Data[index02] : 0.0; 
	float gt1p20 = valid20 ? srt.pBufs->inputGridT1Data[index20] : 0.0; 
	float gt1p22 = valid22 ? srt.pBufs->inputGridT1Data[index22] : 0.0; 

	// float value = kA * (2.0 * gt1 - gt2) +  kC2 * ( gt1p0 + gt1p1 + gt1p2 + gt1p3 - 4.0f * gt1);
	float value = kA * (2.0 * gt1 - gt2) +
	              kC2 * (gt1p0 + gt1p1 + gt1p2 + gt1p3 + 1.41421356237310f * (gt1p00 + gt1p02 + gt1p20 + gt1p22) -
	                     9.65685424949238f * gt1);

	uint i;
	for (i = 0; i < srt.pConsts->m_numImpulses; i++)
	{
		float len = length(float2(srt.pConsts->m_impulseIndex[i].x, srt.pConsts->m_impulseIndex[i].y) - ptRC2);
		value = (len <= srt.pConsts->m_lineWidth) ? srt.pConsts->m_impulseForce[i].x : value;
	}

	for (i=0; i < srt.pConsts->m_numLineImpulses; i++)  {
		float2 p0 = float2(srt.pConsts->m_lineImpulse[i].x, srt.pConsts->m_lineImpulse[i].y);
		float2 p1 = float2(srt.pConsts->m_lineImpulse[i].z, srt.pConsts->m_lineImpulse[i].w);
		float2 v = (p1 - p0);
		float  n = length(v);
		v = normalize(v);
		float2 q = ptRC2; //float2(c1, r1);
		float2 w = q - p0;
		float  d = dot(v,w);
		float2 r = p0 + v * d;
		float  dr = length(q - r);
		if ((d >=0) && (d <= n) && (dr < srt.pConsts->m_lineWidth)) {
		 	value = srt.pConsts->m_impulseForce[i].y;
		}
	}
	
	srt.pBufs->gridT0Buffer[index] = clamp(value, -1.0f, 1.0f);
}

struct RipplesImpulsesBufs 
{
	RWStructuredBuffer<float>    gridT0Buffer; // 	: register(u0);
};

struct RipplesImpulsesSrt
{
	RipplesConsts *pConsts;
	RipplesImpulsesBufs *pBufs;
};

[numthreads(kMaxThreadX, kMaxThreadY, 1)]
void Cs_RipplesImpulses(uint2 dispatchThreadId : SV_DispatchThreadID,
                        RipplesImpulsesSrt srt : S_SRT_DATA)
{
	int r1 = dispatchThreadId.x; // current row  
	int c1 = dispatchThreadId.y; // current col

	float2 ptRC2 = float2(float(c1), float(r1)) * srt.pConsts->m_scalePerUnit +
	               float2(srt.pConsts->m_fpx, srt.pConsts->m_fpz) +
	               float2(-srt.pConsts->m_scale2, -srt.pConsts->m_scale2);

	uint index  = ((r1) * srt.pConsts->m_num) + c1;  // center

	float value = srt.pBufs->gridT0Buffer[index];

	uint i;
	for (i = 0; i < srt.pConsts->m_numImpulses; i++)
	{
		float len = length(float2(srt.pConsts->m_impulseIndex[i].x, srt.pConsts->m_impulseIndex[i].y) - ptRC2);
		value = (len <= srt.pConsts->m_lineWidth) ? srt.pConsts->m_impulseForce[i].x : value;
	}

	for (i=0; i < srt.pConsts->m_numLineImpulses; i++)  {
		float2 p0 = float2(srt.pConsts->m_lineImpulse[i].x, srt.pConsts->m_lineImpulse[i].y);
		float2 p1 = float2(srt.pConsts->m_lineImpulse[i].z, srt.pConsts->m_lineImpulse[i].w);
		float2 v = (p1 - p0);
		float  n = length(v);
		v = normalize(v);
		float2 q = ptRC2; //float2(c1, r1);
		float2 w = q - p0;
		float  d = dot(v,w);
		float2 r = p0 + v * d;
		float  dr = length(q - r);
		if ((d >=0) && (d <= n) && (dr < srt.pConsts->m_lineWidth)) {
		 	value = srt.pConsts->m_impulseForce[i].y;
		}
	}
	
	srt.pBufs->gridT0Buffer[index] = clamp(value, -1.0f, 1.0f);
}

struct RipplesResetRwBufs
{
	RWStructuredBuffer<float>    gridT0Buffer; // 	: register(u0);
	RWStructuredBuffer<float>    gridT1Buffer; // 	: register(u1);	
	RWStructuredBuffer<float>    gridT2Buffer; // 	: register(u2);
};

struct RipplesResetSrt
{
	RipplesConsts *pConsts;
	RipplesResetRwBufs *pRwBufs;
};

[numthreads(kMaxThreadX, kMaxThreadY, 1)]
void Cs_RipplesReset(uint3 dispatchThreadId : SV_DispatchThreadID,
                     RipplesResetSrt srt : S_SRT_DATA) 
{
	int r1 = dispatchThreadId.x; // current row
	int c1 = dispatchThreadId.y; // current col

	uint index = (r1 * srt.pConsts->m_num) + c1;  // up
	
	float value = 0;

	srt.pRwBufs->gridT0Buffer[index] = value;
	srt.pRwBufs->gridT1Buffer[index] = value;
	srt.pRwBufs->gridT2Buffer[index] = value;
}


struct RipplesMoveRwBufs
{
	RWStructuredBuffer<float>    gridT0Buffer; // 	: register(u0);
	RWStructuredBuffer<float>    gridT1Buffer; // 	: register(u1);	
};

struct RipplesMoveSrt
{
	RipplesConsts *pConsts;
	RipplesMoveRwBufs *pRwBufs;
};

// Move the grid and copy
[numthreads(kMaxThreadX, kMaxThreadY, 1)]
void Cs_RipplesMove(uint3 dispatchThreadId : SV_DispatchThreadID,
                    RipplesMoveSrt srt : S_SRT_DATA) 
{

	int rDst = dispatchThreadId.x; // current row
	int cDst = dispatchThreadId.y; // current col

	int rSrc = rDst - srt.pConsts->m_duz;
	int cSrc = cDst - srt.pConsts->m_dux;

	uint indexDst  = (rDst * srt.pConsts->m_num) + cDst;
	uint indexSrc  = (rSrc * srt.pConsts->m_num) + cSrc;  

	if (rSrc < 0 || rSrc >= srt.pConsts->m_num || cSrc < 0 || cSrc >= srt.pConsts->m_num) {
		srt.pRwBufs->gridT1Buffer[indexDst] = 0;
	} else {
		srt.pRwBufs->gridT1Buffer[indexDst] = srt.pRwBufs->gridT0Buffer[indexSrc];
	}
}

#if 0
		for (uint i=0; i < srt.pConsts->m_numLineImpulses; i++)  {

			float2 p0 = float2(srt.pConsts->m_lineImpulseStartX[i], srt.pConsts->m_lineImpulseStartZ[i]);
			float2 p1 = float2(srt.pConsts->m_lineImpulseEndX[i],   srt.pConsts->m_lineImpulseEndZ[i]);
			float2 v = p1 - p0;
			float2 q = float2(c1, r1);
			float2 w = q - p0;
			float  n = length(v);
			float  d = dot(v,w) / n;
			float2 r = p0 + v * d / n;
			float  dr = length(r);
			if (d >=0 && d <= 1 && dr < 5.0f) {
				value = 1.0;
			}
		}
#endif

struct RipplesGuassianBlurMipSrt
{
	Texture2D<float4> m_src;
	RWTexture2D<float4> m_dst;
	SamplerState m_samplerLinear;

	float2 m_blurRadius;
	float2 m_invDstSize;
};

// This is a direct port of VS_GaussianFilter and PS_GaussianFilter from helper-functions.fx to compute.
[numthreads(8, 8, 1)]
void Cs_RipplesGuassianBlurMip(uint2 dispatchThreadId : SV_DispatchThreadID, RipplesGuassianBlurMipSrt *pSrt : S_SRT_DATA)
{
	float2 uv = (dispatchThreadId.xy + 0.5f) * pSrt->m_invDstSize;

	float4 sample0  = pSrt->m_src.SampleLevel(pSrt->m_samplerLinear, uv + pSrt->m_blurRadius * float2( 0,  2), 0);
	float4 sample1  = pSrt->m_src.SampleLevel(pSrt->m_samplerLinear, uv + pSrt->m_blurRadius * float2(-1,  1), 0);
	float4 sample2  = pSrt->m_src.SampleLevel(pSrt->m_samplerLinear, uv + pSrt->m_blurRadius * float2( 0,  1), 0);
	float4 sample3  = pSrt->m_src.SampleLevel(pSrt->m_samplerLinear, uv + pSrt->m_blurRadius * float2( 1,  1), 0);
	float4 sample4  = pSrt->m_src.SampleLevel(pSrt->m_samplerLinear, uv + pSrt->m_blurRadius * float2(-2,  0), 0);
	float4 sample5  = pSrt->m_src.SampleLevel(pSrt->m_samplerLinear, uv + pSrt->m_blurRadius * float2(-1,  0), 0);
	float4 sample6  = pSrt->m_src.SampleLevel(pSrt->m_samplerLinear, uv + pSrt->m_blurRadius * float2( 1,  0), 0);
	float4 sample7  = pSrt->m_src.SampleLevel(pSrt->m_samplerLinear, uv + pSrt->m_blurRadius * float2( 2,  0), 0);
	float4 sample8  = pSrt->m_src.SampleLevel(pSrt->m_samplerLinear, uv + pSrt->m_blurRadius * float2(-1, -1), 0);
	float4 sample9  = pSrt->m_src.SampleLevel(pSrt->m_samplerLinear, uv + pSrt->m_blurRadius * float2( 0, -1), 0);
	float4 sample10 = pSrt->m_src.SampleLevel(pSrt->m_samplerLinear, uv + pSrt->m_blurRadius * float2( 1, -1), 0);
	float4 sample11 = pSrt->m_src.SampleLevel(pSrt->m_samplerLinear, uv + pSrt->m_blurRadius * float2( 0, -2), 0);
	float4 sample12 = pSrt->m_src.SampleLevel(pSrt->m_samplerLinear, uv, 0);

	float4 result = (sample0 + sample4 + sample7 + sample11) * 0.024882 +
					(sample1 + sample3 + sample8 + sample10) * 0.067638 +
					(sample2 + sample5 + sample6 + sample9) * 0.111515 +
					sample12 * 0.183858;

	pSrt->m_dst[dispatchThreadId.xy] = result;
}

struct GfxSubRegionData
{
	uint m_numPlanes;
	uint m_internalPlaneBits;
	float4 m_bsphere;
	uint m_startPlaneIndex;
	uint m_pad;
};

struct GfxRegionSrt
{
	Texture2D<uint4> m_spacialHash;
	StructuredBuffer<float4>			m_regionPlanes;
	StructuredBuffer<GfxSubRegionData>	m_gfxSubRegionData;
	int					m_numSubRegions;
	float2				m_offset;
	float2				m_scale;
	uint2				m_dim;
	float3				m_bsphereCenter;

	float			m_bsphereR;
	float			m_pad;
};

struct GfxCullRegionSrt
{
	float2				m_start;
	float2				m_scale;
	uint2				m_dim;
	uint2				m_pad;

	RWTexture2D<uint4> m_dst;
	GfxRegionSrt		*m_pRegionSrt;
	
};

uint4 FindPotentialRegions(GfxRegionSrt *pSrt, float3 posWs, float radius)
{
	int iGlobalPlane = 0;
	int probeRegionPlaneOffset = 0;

	float kFarFromRegion = 1.0; // 6.0f;

	uint4 resBits = uint4(0,0,0,0);

	bool deepInside = false;

	[isolate]
	{
		float minRegionAwayDist = 10000000.0f;
		int numSubRegions = pSrt->m_numSubRegions;
		for (int iSubRegion = 0; (iSubRegion < numSubRegions) /* && (!fullyInside)*/; ++iSubRegion)
		{
			int numPlanes = pSrt->m_gfxSubRegionData[iSubRegion].m_numPlanes;
			uint internalPlaneBits = pSrt->m_gfxSubRegionData[iSubRegion].m_internalPlaneBits;
			float blendOut = 0.1;
			bool inside = true;
			//fullyInside = true;
			//float regionAwayDist = 0.0f;
			float regionAwayDist = -blendOut;

			deepInside = true;
			bool farOutside = false;

			for (int iPlane = 0; iPlane < numPlanes; ++iPlane)
			{
				float4 planeEq = pSrt->m_regionPlanes[iGlobalPlane + iPlane];
				float d = dot(planeEq.xyz, posWs) + planeEq.w;
				//if (abs(dot(planeEq.xyz, float3(0, 1, 0))) > 0.9)
				//{
				//	// we don't care about horizontal planes, for now at least. because tiles are flat
				//	continue;
				//}

				bool isInternal = internalPlaneBits & (1 << iPlane);

				if (d > 0)
				{
					deepInside = false;
					inside = false; // fully outside, no need to iterate more
									//fullyInside = false;

					// but we might be still close enough

					if (d > radius)
					{
						// ok we can break out of the loop now. we are definitely far from region
						farOutside = true;
						break;
					}
				}
				else
				{
					// we are inside, but if all planes are deep inside we also don't need to check
					if (d > -radius)
					{
						deepInside = false;
					}
					// todo: we could use this optimization, just need a way to store that (maybe all 1111111s as a special value, or first bit offset? )
				}
			}
			
			
			if (deepInside)
			{
				// we are fully inside one region. this is special optimized case
				break;
			}
			else if (!farOutside)
			{
				// we should check this potential region when checking position sinside of this sphere
				uint bitToSet = iSubRegion + 1; // 0 bit is reserved

				if (bitToSet < 32)
				{
					resBits.x = resBits.x | (1 << bitToSet);
				}
				else if (bitToSet < 64)
				{
					resBits.y = resBits.y | (1 << (bitToSet - 32));
				}
				else if (bitToSet < 96)
				{
					resBits.z = resBits.z | (1 << (bitToSet - 64));
				}
				else
				{
					resBits.w = resBits.w | (1 << (bitToSet - 96));
				}
			}
			

			iGlobalPlane += numPlanes;
		}
	}

	if (deepInside)
	{
		// or if we are fully inside
		resBits.y = 0xffffffff;
		resBits.x = 0x00000003; // we only care about first two bits being set, but we set the whole thing to easier see it in debugger
	}
	else if (resBits.x == 0 && resBits.y == 0 && resBits.z == 0 && resBits.w == 0)
	{
		// tile is not intersecting with this quad. special case
		resBits.z = 0xffffffff;
		resBits.x = 0x00000001;// we only care about first two bits being set, but we set the whole thing to easier see it in debugger
	}
	
	return resBits;
}


[numthreads(kMaxThreadX, kMaxThreadY, 1)]
void Cs_GenerateGfxRegionMap(uint2 dispatchThreadId : SV_DispatchThreadID,
	GfxCullRegionSrt *pSrt : S_SRT_DATA)
{
	float2 centerXZ = pSrt->m_pRegionSrt->m_offset + pSrt->m_pRegionSrt->m_scale * (dispatchThreadId.xy + float2(0.5f, 0.5f));

	float radius = pSrt->m_pRegionSrt->m_scale * sqrt(2.0);

	float3 center = float3(centerXZ.x, pSrt->m_pRegionSrt->m_bsphereCenter.y, centerXZ.y); // we use bsphere center as we assume we mostly care about culling in xz

	uint4 potentialBits = FindPotentialRegions(pSrt->m_pRegionSrt, center, radius);

	// now go theough all regions, and find which ones the bbox is within radius of

	pSrt->m_dst[dispatchThreadId.xy] = potentialBits;
}


