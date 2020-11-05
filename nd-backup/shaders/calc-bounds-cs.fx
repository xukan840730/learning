#include "global-funcs.fxi"

#define kNumThreadsInGroup	64

struct Bounds
{
	float4 m_min; 
	float4 m_max; 
};

struct SrtData
{
	Buffer<float3> t_inputPos;
	RWStructuredBuffer<Bounds> b_bounds;
};

groupshared float3 g_minObjectPos[kNumThreadsInGroup];
groupshared float3 g_maxObjectPos[kNumThreadsInGroup];

void InitMinMaxPos(inout float3 minPos, inout float3 maxPos, in uint threadId)
{
	float maxDist = 1e30f;
	float minDist = -1e30f;
	minPos = float3(maxDist, maxDist, maxDist);
	maxPos = float3(minDist, minDist, minDist);
}

void MinMaxPos(inout float3 minPos, inout float3 maxPos, in float3 pos, in uint threadId)
{
	minPos = min(minPos, pos);
	maxPos = max(maxPos, pos);
}

void ReduceMinMax(in float3 minPos, in float3 maxPos, in uint threadId)
{
	g_maxObjectPos[threadId] = maxPos;
	g_minObjectPos[threadId] = minPos;

	[unroll]
	for (uint sz = kNumThreadsInGroup / 2; sz > 0; sz >>= 1)
	{
		if (threadId < sz) 
		{
			g_maxObjectPos[threadId] = min(g_minObjectPos[2 * threadId], g_minObjectPos[2 * threadId + 1]);
			g_maxObjectPos[threadId] = max(g_maxObjectPos[2 * threadId], g_maxObjectPos[2 * threadId + 1]);
		}
	}	
}

[numthreads(kNumThreadsInGroup, 1, 1)]
void Cs_CalcBounds(uint3 dispatchThreadId : SV_DispatchThreadID, SrtData srt : S_SRT_DATA)
{
	uint threadId = dispatchThreadId.x;
	uint numVertexes; 
	srt.t_inputPos.GetDimensions(numVertexes);
	float3 minPos;
	float3 maxPos;

	InitMinMaxPos(minPos, maxPos, threadId);

    for (uint iVertex = threadId; iVertex < numVertexes; iVertex += kNumThreadsInGroup)
    {
	    MinMaxPos(minPos, maxPos, srt.t_inputPos[iVertex], threadId);		
    }

	ReduceMinMax(minPos, maxPos, dispatchThreadId.x);

	if (threadId == 0)
	{
		srt.b_bounds[0].m_min = float4(g_minObjectPos[0], 1.0f);
		srt.b_bounds[0].m_max = float4(g_maxObjectPos[0], 1.0f);
	}
}
