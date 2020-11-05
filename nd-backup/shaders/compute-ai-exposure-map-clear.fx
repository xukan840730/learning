/*
* Copyright (c) 2018 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited.
*/

struct Srt_ComputeAiExposureMapClear
{
	RW_Texture2D<uint>				m_tex;
};

[numthreads(64, 1, 1)]
void Cs_ComputeAiExposureMapClear(Srt_ComputeAiExposureMapClear srt : S_SRT_DATA, uint2 threadID : S_DISPATCH_THREAD_ID)
{
	const uint x = threadID.x;
	const uint z = threadID.y << 2;

	srt.m_tex[int2(x, z    )] = 0;
	srt.m_tex[int2(x, z + 1)] = 0;
	srt.m_tex[int2(x, z + 2)] = 0;
	srt.m_tex[int2(x, z + 3)] = 0;
}
