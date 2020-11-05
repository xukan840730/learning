

struct Srt_SimpleMemCopy
{
	RegularBuffer<uint>			m_input;
	RW_RegularBuffer<uint>		m_output;
	uint						m_multiplier;
};

[numthreads(64, 1, 1)]
void Cs_UnittestSimpleMemCopy(Srt_SimpleMemCopy* srt : S_SRT_DATA, uint threadIndexInGroup : S_DISPATCH_THREAD_ID)
{
	srt->m_output[threadIndexInGroup] = srt->m_input[threadIndexInGroup] * srt->m_multiplier;
}


struct Srt_GetTimer
{
	RW_RegularBuffer<uint2>		m_timerOutput;
};


[numthreads(64, 1, 1)]
void Cs_UnittestGetTimer(Srt_GetTimer* srt : S_SRT_DATA, uint threadIndexInGroup : S_DISPATCH_THREAD_ID)
{
	if (threadIndexInGroup == 0)
	{
		srt->m_timerOutput[0].x = GetShaderEngineID();
		srt->m_timerOutput[0].y = GetCUID();
		srt->m_timerOutput[1] = GetTimer();
	}
}


struct Srt_ThreadIndices
{
	RW_RegularBuffer<uint4>		m_output;
};

[numthreads(32, 8, 1)]
void Cs_UnittestThreadIndices(Srt_ThreadIndices* srt : S_SRT_DATA, uint threadIndexInGroup : S_DISPATCH_THREAD_ID, uint3 threadIndexInGroup3 : S_DISPATCH_THREAD_ID,
	uint groupIndex : S_GROUP_INDEX, uint3 groupId : S_GROUP_ID, uint3 groupThreadId : S_GROUP_THREAD_ID)
{
	const int threadGroupIndex = 3*groupId.y + groupId.x;
	const int index = threadGroupIndex*32*8 + groupIndex;
	const int offsetA = 3*index;
	const int offsetB = 3*index + 1;
	const int offsetC = 3*index + 2;
	srt->m_output[offsetA].x = 32*threadIndexInGroup3.y + threadIndexInGroup3.x;
	srt->m_output[offsetA].y = threadIndexInGroup3.x;
	srt->m_output[offsetA].z = threadIndexInGroup3.y;
	srt->m_output[offsetA].w = threadIndexInGroup3.z;
	srt->m_output[offsetB].x = groupId.x;
	srt->m_output[offsetB].y = groupId.y;
	srt->m_output[offsetB].z = groupId.z;
	srt->m_output[offsetB].w = 0xA0B1C2D3;
	srt->m_output[offsetC].x = groupIndex;
	srt->m_output[offsetC].y = groupThreadId.x;
	srt->m_output[offsetC].z = groupThreadId.y;
	srt->m_output[offsetC].w = groupThreadId.z;
}




struct SwizzleTestData
{
	uint	a;
	uint	b;
	uint	c;
	uint	d;
};

struct Srt_SwizzleTest
{
	RW_RegularBuffer<SwizzleTestData>		m_data;
};

[numthreads(64, 1, 1)]
void Cs_UnittestSwizzleA(Srt_SwizzleTest* srt : S_SRT_DATA, uint threadIndexInGroup : S_DISPATCH_THREAD_ID)
{
    srt->m_data[threadIndexInGroup].a = 10000 + threadIndexInGroup;
    srt->m_data[threadIndexInGroup].b = 20000 + threadIndexInGroup;
    srt->m_data[threadIndexInGroup].c = 30000 + threadIndexInGroup;
    srt->m_data[threadIndexInGroup].d = 40000 + threadIndexInGroup;
}

[numthreads(64, 1, 1)]
void Cs_UnittestSwizzleB(Srt_SwizzleTest* srt : S_SRT_DATA, uint threadIndexInGroup : S_DISPATCH_THREAD_ID)
{
    [isolate]
    {
        srt->m_data[threadIndexInGroup].a = 10000 + threadIndexInGroup;
    }
    [isolate]
    {
        srt->m_data[threadIndexInGroup].b = 20000 + threadIndexInGroup;
    }
    [isolate]
    {
        srt->m_data[threadIndexInGroup].c = 30000 + threadIndexInGroup;
    }
    [isolate]
    {
        srt->m_data[threadIndexInGroup].d = 40000 + threadIndexInGroup;
    }
}




#if 0

cbuffer ConstData	: register(b0)
{
	uint arrayCount;
	uint threadgroupCount;
}

RWStructuredBuffer<int4>	mem		: register(u0);
RWStructuredBuffer<int>		memS	: register(u0);
RWStructuredBuffer<int>		accum	: register(u1);
RWStructuredBuffer<int>		debug	: register(u2);


groupshared int g_reduceBuf64[64];
groupshared int g_reduceBuf512[512];


[numthreads(64, 1, 1)]
void Cs_UnittestMemRead64(uint dispatchThreadId : SV_DispatchThreadID)
{
	const uint totalThreadCount = threadgroupCount*64;
	const int loopCount = arrayCount/totalThreadCount/4;
	
	int a = 0;
	for (int i=0; i<loopCount; i++)
	{
		const uint index0 = totalThreadCount*i + dispatchThreadId;
		a += mem[index0].x;
		a += mem[index0].y;
		a += mem[index0].z;
		a += mem[index0].w;
	}
	
	accum[dispatchThreadId] = a;
}

[numthreads(512, 1, 1)]
void Cs_UnittestMemRead512(uint dispatchThreadId : SV_DispatchThreadID)
{
	const uint totalThreadCount = threadgroupCount*512;
	const int loopCount = arrayCount/totalThreadCount/4;
	
	int a = 0;
	for (int i=0; i<loopCount; i++)
	{
		const uint index0 = totalThreadCount*i + dispatchThreadId;
		a += mem[index0].x;
		a += mem[index0].y;
		a += mem[index0].z;
		a += mem[index0].w;
	}

	accum[dispatchThreadId] = a;
}



[numthreads(64, 1, 1)]
void Cs_UnittestMemWrite64(uint dispatchThreadId : SV_DispatchThreadID)
{
	const uint totalThreadCount = threadgroupCount*64;
	const int loopCount = arrayCount/totalThreadCount/4;
	
	for (int i=0; i<loopCount; i++)
	{
		const uint index = totalThreadCount*i + dispatchThreadId;
		mem[index].x = dispatchThreadId;
		mem[index].y = dispatchThreadId;
		mem[index].z = dispatchThreadId;
		mem[index].w = dispatchThreadId;
	}
}

[numthreads(512, 1, 1)]
void Cs_UnittestMemWrite512(uint dispatchThreadId : SV_DispatchThreadID)
{
	const uint totalThreadCount = threadgroupCount*512;
	const int loopCount = arrayCount/totalThreadCount/4;
	
	for (int i=0; i<loopCount; i++)
	{
		const uint index = totalThreadCount*i + dispatchThreadId;
		mem[index].x = dispatchThreadId;
		mem[index].y = dispatchThreadId;
		mem[index].z = dispatchThreadId;
		mem[index].w = dispatchThreadId;
	}
}

[numthreads(64, 1, 1)]
void Cs_UnittestMemAdd1024x64(uint dispatchThreadId : SV_DispatchThreadID)
{
	const uint totalThreadCount = threadgroupCount*64;
	const int loopCount = arrayCount/totalThreadCount/4;
	const int outerLoopCount = 1024;

	for (int j=0; j<outerLoopCount; j++)
	{
		for (int i=0; i<loopCount; i++)
		{
			const uint index = totalThreadCount*i + dispatchThreadId;
			mem[index].x += 1;
			mem[index].y += 1;
			mem[index].z += 1;
			mem[index].w += 1;
		}
	}
}

[numthreads(512, 1, 1)]
void Cs_UnittestMemAdd1x512(uint dispatchThreadId : SV_DispatchThreadID)
{
	const uint totalThreadCount = threadgroupCount*512;
	const int loopCount = arrayCount/totalThreadCount/4;
	
	for (int i=0; i<loopCount; i++)
	{
		const uint index = totalThreadCount*i + dispatchThreadId;
		mem[index].x += 1;
		mem[index].y += 1;
		mem[index].z += 1;
		mem[index].w += 1;
	}
}

[numthreads(64, 1, 1)]
void Cs_UnittestReduce64(uint dispatchThreadId : SV_DispatchThreadID)
{
	const uint loopCount = (arrayCount+63)/64;

	int sum = 0;
	for (int i=0; i<loopCount; i++)
		sum += memS[dispatchThreadId + i*64];

	g_reduceBuf64[dispatchThreadId] = sum;

	if (dispatchThreadId < 32)		g_reduceBuf64[dispatchThreadId] += g_reduceBuf64[dispatchThreadId + 32];
	if (dispatchThreadId < 16)		g_reduceBuf64[dispatchThreadId] += g_reduceBuf64[dispatchThreadId + 16];
	if (dispatchThreadId < 8)		g_reduceBuf64[dispatchThreadId] += g_reduceBuf64[dispatchThreadId + 8];
	if (dispatchThreadId < 4)		g_reduceBuf64[dispatchThreadId] += g_reduceBuf64[dispatchThreadId + 4];
	if (dispatchThreadId < 2)		g_reduceBuf64[dispatchThreadId] += g_reduceBuf64[dispatchThreadId + 2];

	if (dispatchThreadId < 1)
		accum[0] = g_reduceBuf64[0] + g_reduceBuf64[1];
}

[numthreads(512, 1, 1)]
void Cs_UnittestReduce512(uint dispatchThreadId : SV_DispatchThreadID)
{
	const uint loopCount = (arrayCount+511)/512;

	int sum = 0;
	for (int i=0; i<loopCount; i++)
		sum += memS[dispatchThreadId + i*512];

	g_reduceBuf512[dispatchThreadId] = sum;
	GroupMemoryBarrierWithGroupSync();

	if (dispatchThreadId < 256)		g_reduceBuf512[dispatchThreadId] += g_reduceBuf512[dispatchThreadId + 256];
	GroupMemoryBarrierWithGroupSync();

	if (dispatchThreadId < 128)		g_reduceBuf512[dispatchThreadId] += g_reduceBuf512[dispatchThreadId + 128];
	GroupMemoryBarrierWithGroupSync();

	if (dispatchThreadId < 64)		g_reduceBuf512[dispatchThreadId] += g_reduceBuf512[dispatchThreadId + 64];
	GroupMemoryBarrierWithGroupSync();

	if (dispatchThreadId < 32)		g_reduceBuf512[dispatchThreadId] += g_reduceBuf512[dispatchThreadId + 32];
	GroupMemoryBarrierWithGroupSync();

	if (dispatchThreadId < 16)		g_reduceBuf512[dispatchThreadId] += g_reduceBuf512[dispatchThreadId + 16];
	GroupMemoryBarrierWithGroupSync();

	if (dispatchThreadId < 8)		g_reduceBuf512[dispatchThreadId] += g_reduceBuf512[dispatchThreadId + 8];
	GroupMemoryBarrierWithGroupSync();

	if (dispatchThreadId < 4)		g_reduceBuf512[dispatchThreadId] += g_reduceBuf512[dispatchThreadId + 4];
	GroupMemoryBarrierWithGroupSync();

	if (dispatchThreadId < 2)		g_reduceBuf512[dispatchThreadId] += g_reduceBuf512[dispatchThreadId + 2];
	GroupMemoryBarrierWithGroupSync();

	if (dispatchThreadId < 1)
		accum[0] = g_reduceBuf512[0] + g_reduceBuf512[1];
}




// Thread group sync test
cbuffer ConstData	: register(b0)
{
	uint syncTestArrayCount;
}

RWStructuredBuffer<int>		bufMem		: register(u0);
RWStructuredBuffer<int>		bufOutput	: register(u1);

void UnittestSyncThreadLoop(uint dispatchThreadId)
{
	if (dispatchThreadId >= 128)
		return;

	for (uint count=0; count<4; count++)
	{
		for (uint i=0; i<syncTestArrayCount; i+=128)
			bufMem[i+dispatchThreadId] += 1;

		//GroupMemoryBarrierWithGroupSync();
	}
}

[numthreads(256, 1, 1)]
void Cs_UnittestSyncThread(uint dispatchThreadId : SV_DispatchThreadID)
{
	UnittestSyncThreadLoop(dispatchThreadId);
	GroupMemoryBarrierWithGroupSync();

	for (uint i=0; i<syncTestArrayCount; i+=256)
		bufOutput[i+dispatchThreadId] = bufMem[i+dispatchThreadId];
}

#endif