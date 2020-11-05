/*
* Copyright (c) 2018 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited.
*/

struct Srt_ComputeAiExposureMapReduce
{
	Texture2D<uint>				m_in;
	RW_RegularBuffer<uint>		m_out;
	uint						m_mask;
	uint						m_pitch;
};

[numthreads(1, 64, 1)]
void Cs_ComputeAiExposureMapReduce(Srt_ComputeAiExposureMapReduce srt : S_SRT_DATA, uint2 threadID : S_DISPATCH_THREAD_ID)
{
	const uint x = threadID.x << 5;
	const uint z = threadID.y;

	// for each element from (x, z) to (x + 31, z) inclusive, set bit in output U32 to (that == srt.m_mask)
	uint res = 0;
	for (int i = 0; i < 32; ++i)
	{
		res |= uint(srt.m_in[int2(x + i, z)] == srt.m_mask) << i;
	}

	const uint index = z * srt.m_pitch + x;
	srt.m_out[index >> 5] |= res;
}
