//
// Compute blend-shapes
//
// Blend shapes are simple deltas from the vertex input data. 
// The data is given as pairs of patch indexes, corresponding to the vertex index,
// as well as the delta vertex stream.
//
// For each blend-shape, there is a corresponding weight, thus the calculation should
// be for positions:
//
//    vtx_data := copy vertex data from base mesh
//    foreach blend_shape:
//       w := blend_shape.weight
//       foreach delta in blend_shape.deltas:
//           vtx_data[delta.idx] += weight * delta.vtx_delta
//       done
//    done
//
// We will support from 1 up to N blend-shapes, so if there are more than that many
// blends, it will have to be repeated more times.
//
// 

#include "global-funcs.fxi"
#include "profile.fxi"

#define BS_NUM_ATTRIBUTES ((BS_VERSION & 3) + 1)

//#define BS_FMT_FP32 0
//#define BS_FMT_FP16 1

//#define BS_NUM_ATTR0      (((BS_VERSION >>  2) & 3) + 1)
//#define BS_FMT_ATTR0      (((BS_VERSION >>  4) & 1))
//#define BS_NUM_ATTR1      (((BS_VERSION >>  5) & 3) + 1)
//#define BS_FMT_ATTR1      (((BS_VERSION >>  7) & 1))
//#define BS_NUM_ATTR2      (((BS_VERSION >>  8) & 3) + 1)
//#define BS_FMT_ATTR2      (((BS_VERSION >> 10) & 1))
//#define BS_NUM_ATTR3      (((BS_VERSION >> 11) & 3) + 1)
//#define BS_FMT_ATTR3      (((BS_VERSION >> 13) & 1))

static const int kMaxNumBlendShapes = 4;

struct VertexLayout
{
	uint m_offset;
	uint m_stride;
};

struct BlendShapeConsts
{
	// qword 0: globals
	uint  m_numBlends;
	uint  m_numVertexes;
	uint  m_doCopy;
	uint  pad;
	// Input layouts
	uint4 m_inputOffset;
	uint4 m_inputStride;
	// Blend data
	uint4 m_blendOffset;
	uint4 m_blendStride;
	uint4 m_blendVertexes;
	// Shared between input, blend and output
	uint4 m_weightIndex;
	uint4 m_elementCount;
	// Output layout
	uint4 m_outputOffset;	
	uint4 m_outputStride;	
};

struct BlendShapePass
{
	ByteAddressBuffer   b_input;
	ByteAddressBuffer   b_delta;
	ByteAddressBuffer   b_index;
};

struct BlendShapeBuffers
{
	RWByteAddressBuffer   rw_output;
	Buffer<float>         b_weight;
	BlendShapePass        m_pass[BS_NUM_ATTRIBUTES];
};

struct SrtData
{
	BlendShapeBuffers *m_pBufs;
	BlendShapeConsts *m_pConsts;
};

#define kNumThreadsInGroup 1024
// #define kNumThreadsInGroup 64

float3 LoadFp32x3(ByteAddressBuffer buffer, VertexLayout layout, uint vertexIdx)
{
	float3 pos = LoadAsFloat3(buffer, vertexIdx * layout.m_stride + layout.m_offset);
	return pos;
}

float3 LoadFp32x3(RWByteAddressBuffer buffer, VertexLayout layout, uint vertexIdx)
{
	float3 pos = LoadAsFloat3(buffer, vertexIdx * layout.m_stride + layout.m_offset);
	return pos;
}

void StoreFp32x3(RWByteAddressBuffer buffer, VertexLayout layout, uint vertexIdx, float3 data)
{
	StoreAsFloat3(buffer, data, vertexIdx * layout.m_stride + layout.m_offset);
}

void CopyFp32x3(uint threadId, uint numVertexes, RWByteAddressBuffer output, VertexLayout outputLayout,
                ByteAddressBuffer input, VertexLayout inputLayout)
{
	[loop] for (uint iVertex = threadId; iVertex < numVertexes; iVertex += kNumThreadsInGroup)
	{
		float3 data = LoadFp32x3(input, inputLayout, iVertex);
		StoreFp32x3(output, outputLayout, iVertex, data);
	}
}

void BlendFp32x3(uint threadId, float weight, uint numDeltas, 
                 RWByteAddressBuffer output, VertexLayout outputLayout,
                 ByteAddressBuffer deltas, VertexLayout inputLayout,
                 ByteAddressBuffer indexes)
{
	[loop] for (uint iDelta = threadId; iDelta < numDeltas; iDelta += kNumThreadsInGroup)
	{
		float3 delta = LoadFp32x3(deltas, inputLayout, iDelta);
		uint iVertex = LoadU16(indexes, iDelta);
		//
		float3 data  = LoadFp32x3(output, outputLayout, iVertex);
		float3 res   = data + weight * delta;
		StoreFp32x3(output, outputLayout, iVertex, res);
	}
}

// ---------------------------------------------------------------------------------------------------------------------

float4 LoadFp32x4(ByteAddressBuffer buffer, VertexLayout layout, uint vertexIdx)
{
	float4 pos = LoadAsFloat4(buffer, vertexIdx * layout.m_stride + layout.m_offset);
	return pos;
}

float4 LoadFp32x4(RWByteAddressBuffer buffer, VertexLayout layout, uint vertexIdx)
{
	float4 pos = LoadAsFloat4(buffer, vertexIdx * layout.m_stride + layout.m_offset);
	return pos;
}

void StoreFp32x4(RWByteAddressBuffer buffer, VertexLayout layout, uint vertexIdx, float4 data)
{
	StoreAsFloat4(buffer, data, vertexIdx * layout.m_stride + layout.m_offset);
}

void CopyFp32x4(uint threadId, uint numVertexes, RWByteAddressBuffer output, VertexLayout outputLayout, 
                ByteAddressBuffer input, VertexLayout inputLayout)
{
	[loop] for (uint iVertex = threadId; iVertex < numVertexes; iVertex += kNumThreadsInGroup)
	{
		float4 data = LoadFp32x4(input, inputLayout, iVertex);
		StoreFp32x4(output, outputLayout, iVertex, data);
	}
}

void BlendFp32x4(uint threadId, float weight, uint numDeltas, 
                 RWByteAddressBuffer output, VertexLayout outputLayout,
                 ByteAddressBuffer deltas, VertexLayout inputLayout,
                 ByteAddressBuffer indexes)
{
	[loop] for (uint iDelta = threadId; iDelta < numDeltas; iDelta += kNumThreadsInGroup)
	{
		float4 delta = LoadFp32x4(deltas, inputLayout, iDelta);
		uint iVertex = LoadU16(indexes, iDelta);
		//
		float4 data  = LoadFp32x4(output, outputLayout, iVertex);
		float4 res   = data + weight * delta;
		StoreFp32x4(output, outputLayout, iVertex, res);
	}
}

// ---------------------------------------------------------------------------------------------------------------------

void CopyInput(SrtData srt, uint dispatchThreadId)
{
	[unroll] for (int iPass = 0; iPass < BS_NUM_ATTRIBUTES; iPass++)
	{
		if (iPass >= srt.m_pConsts->m_numBlends) 
			return;

		if ((srt.m_pConsts->m_doCopy & (1<<iPass)) != 0)
		{
			VertexLayout inputLayout = { srt.m_pConsts->m_inputOffset[iPass], srt.m_pConsts->m_inputStride[iPass] };	
			VertexLayout outputLayout = { srt.m_pConsts->m_outputOffset[iPass], srt.m_pConsts->m_outputStride[iPass] };	
			
			if (srt.m_pConsts->m_elementCount[iPass] == 3)
				CopyFp32x3(dispatchThreadId.x, srt.m_pConsts->m_numVertexes, srt.m_pBufs->rw_output, outputLayout,
				           srt.m_pBufs->m_pass[iPass].b_input, inputLayout);
			else
				CopyFp32x4(dispatchThreadId.x, srt.m_pConsts->m_numVertexes, srt.m_pBufs->rw_output, outputLayout,
				           srt.m_pBufs->m_pass[iPass].b_input, inputLayout);
			
			GroupMemoryBarrierWithGroupSync();
		}
	}
}

void Blend(SrtData srt, uint dispatchThreadId)
{
	[unroll] for (int iPass = 0; iPass < BS_NUM_ATTRIBUTES; iPass++)
	{
		if (iPass >= srt.m_pConsts->m_numBlends) 
			return;

		float weight = srt.m_pBufs->b_weight[srt.m_pConsts->m_weightIndex[iPass]];

		if (weight > 0.0f)
		{
			VertexLayout blendLayout = { srt.m_pConsts->m_blendOffset[iPass], srt.m_pConsts->m_blendStride[iPass] };	
			VertexLayout outputLayout = { srt.m_pConsts->m_outputOffset[iPass], srt.m_pConsts->m_outputStride[iPass] };
			
			if (srt.m_pConsts->m_elementCount[iPass] == 3)
				BlendFp32x3(dispatchThreadId.x, weight, srt.m_pConsts->m_blendVertexes[iPass], srt.m_pBufs->rw_output,
				            outputLayout, srt.m_pBufs->m_pass[iPass].b_delta, blendLayout,
				            srt.m_pBufs->m_pass[iPass].b_index);
			else
				BlendFp32x4(dispatchThreadId.x, weight, srt.m_pConsts->m_blendVertexes[iPass], srt.m_pBufs->rw_output,
				            outputLayout, srt.m_pBufs->m_pass[iPass].b_delta, blendLayout,
				            srt.m_pBufs->m_pass[iPass].b_index);

			GroupMemoryBarrierWithGroupSync();
		}
	}
}

[numthreads(kNumThreadsInGroup, 1, 1)]
void Cs_BlendShape(uint3 dispatchThreadId : SV_DispatchThreadID, SrtData srt : S_SRT_DATA)
{
	PROFILE_START(dispatchThreadId.x)

	//
	// First task is to read the input
	//

	if (srt.m_pConsts->m_doCopy != 0)
		CopyInput(srt, dispatchThreadId.x);

	Blend(srt, dispatchThreadId.x);

	PROFILE_END(dispatchThreadId.x)
}
