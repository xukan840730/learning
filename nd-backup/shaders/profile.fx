
struct Srt_GetTimer
{
	RW_RegularBuffer<uint2>		m_timerOutput;
};


[numthreads(64, 1, 1)]
void Cs_ProfileGetTimer(Srt_GetTimer* srt : S_SRT_DATA, uint threadIndexInGroup : S_DISPATCH_THREAD_ID)
{
	if (threadIndexInGroup == 0)
	{
		srt->m_timerOutput[0].x = GetShaderEngineID();
		srt->m_timerOutput[0].y = GetCUID();
		srt->m_timerOutput[1] = GetTimer();
	}
}

struct Srt_IndirectDispatchArgs
{
	RegularBuffer<uint>		m_argsInput;
	RW_RegularBuffer<uint>	m_argsOutput;
};

[numthreads(64, 1, 1)]
void Cs_ProfileIndirectDistpatchArgs(Srt_IndirectDispatchArgs* srt : S_SRT_DATA, uint threadIndexInGroup : S_DISPATCH_THREAD_ID)
{
	if (threadIndexInGroup < 3)
	{
		srt->m_argsOutput[threadIndexInGroup] = srt->m_argsInput[threadIndexInGroup];
	}

	srt->m_argsOutput[3] = 0xDCBA4321;
}
