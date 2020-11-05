//
// Compute Shader - compute mesh normals after deformation
//

// Explicit LDS reservation to limit occupancy for reducing L2 cache thrashing
#pragma argument(reservelds=8192)

#include "global-funcs.fxi"
#include "profile.fxi"

#define ND_PSSL
#include "compressed-vsharp.fxi"

// Different modes to compute normals
// 0 - Only use adjacent vertex positions
// 1 - Compute adjacent face area
#if MESH_NORMALS_CS_MODE == 0
#endif

#define kMaxVertexBlocks	64

struct RecomputeNormalInfoBuffer
{
	uint m_numAdjVertices;   // This is the number of non-halo vertices
	uint m_numAdjacencies;   // This is the number of total indexes into the 'inputIndicesData'
	//
	uint m_useMask;
	uint m_computeNormals;
	uint m_debugOffset;
	uint m_numTotalVertices; // This is the number of vertices, including halo ones, (for debug).
};

struct RecomputeNormalBuffers
{
	uint4              globalVertexBuffer;
	CompressedVSharp   inputUv;
	Buffer<float3>     inputPositionStream;
	Buffer<float3>     inputNormalStream;
	Buffer<uint>       inputIndicesData;
	Buffer<uint>       inputValencesData;
	Buffer<uint>       inputOffsetsData;
	Buffer<uint>       inputMaskData;
	RWBuffer<float3>   outputNormalStream;
	RWBuffer<float3>   outputPositionStream;
	RWBuffer<float2>   outputStretchFactorsStream;
};

struct SrtData
{
	RecomputeNormalBuffers *m_pBufs;
	RecomputeNormalInfoBuffer *m_pConsts;
};

[numthreads(kMaxVertexBlocks, 1, 1)]
void Cs_RecomputeMeshNormals(uint3 dispatchThreadId : SV_DispatchThreadID, SrtData srt : S_SRT_DATA)
{
	PROFILE_START(dispatchThreadId.x);

	uint curVertexIdx = dispatchThreadId.x;

	// guard against reading and writing too much
	if (curVertexIdx >= srt.m_pConsts->m_numAdjVertices)
	{
		PROFILE_END(dispatchThreadId.x);
		return;
	}

	// uint debugOffset  = srt.m_pConsts->m_debugOffset;

	uint valence = srt.m_pBufs->inputValencesData[curVertexIdx];
	uint offset = srt.m_pBufs->inputOffsetsData[curVertexIdx];
	uint mask = srt.m_pBufs->inputMaskData[curVertexIdx];  // LoadIntFromByte
	uint adjIndex = srt.m_pBufs->inputIndicesData[offset]; // first index of adjacency

	float3 baseVtx = srt.m_pBufs->inputPositionStream[curVertexIdx];
	float3 adjVtx = srt.m_pBufs->inputPositionStream[adjIndex]; // adj vertices range is [1..valence]....
	                                                                       // adjIndex = indices[offset + 0]

	float3 prevEdge = adjVtx - baseVtx;

	float3 addNormal = { 0, 0, 0 };

	// float3 avg = {0,0,0};

	// By construction of the data. We need to wind on the "valence" of the vertex. This is really not the real
	// valence. For interior vertices the valences is +1, and we have an extra index to make sure that we wrap around
	if (((srt.m_pConsts->m_useMask && mask == 1) || !srt.m_pConsts->m_useMask) && srt.m_pConsts->m_computeNormals)
	{
		for (uint vtxIndex = 1; vtxIndex < valence; vtxIndex++)
		{
			uint adjPlusIndex =
			    srt.m_pBufs->inputIndicesData[offset + vtxIndex]; // adjPlusIndex = indices[offset + vtxIndex]

			// Get adjacent
			adjVtx = srt.m_pBufs->inputPositionStream[adjPlusIndex];

			float3 edge = adjVtx - baseVtx;

			// We don't normalize edges to use their lengths to adjust the normal
			addNormal = addNormal + cross(prevEdge, edge);

			prevEdge = edge;
		}
		addNormal = normalize(addNormal);
	}
	else
	{
		// pass by the normal (it's part of the input stream)
		addNormal = srt.m_pBufs->inputNormalStream[curVertexIdx];
		addNormal = (srt.m_pConsts->m_useMask) ? addNormal : float3(0,1,0);
	}

	// addNormal = 0.8 * prevEdge;
	// Positions
	srt.m_pBufs->outputPositionStream[curVertexIdx] = baseVtx;

	// store the normal buffer (offset by 3*sizeof(floats))
	srt.m_pBufs->outputNormalStream[curVertexIdx] = addNormal;

	PROFILE_END(dispatchThreadId.x);
}

[numthreads(kMaxVertexBlocks, 1, 1)]
void Cs_RecomputeMeshNormalsWithStretchFactors(uint3 dispatchThreadId : SV_DispatchThreadID, SrtData srt : S_SRT_DATA)
{
	PROFILE_START(dispatchThreadId.x);

	uint curVertexIdx = dispatchThreadId.x;

	// guard against reading and writing too much
	if (curVertexIdx >= srt.m_pConsts->m_numAdjVertices)
	{
		PROFILE_END(dispatchThreadId.x);
		return;
	}

	uint valence = srt.m_pBufs->inputValencesData[curVertexIdx];
	uint offset = srt.m_pBufs->inputOffsetsData[curVertexIdx];
	uint adjIndex = srt.m_pBufs->inputIndicesData[offset]; // first index of adjacency

	float3 baseVtxPos = srt.m_pBufs->inputPositionStream[curVertexIdx];
	float2 baseVtxUv = LoadVertexAttribute<float2, 32>(srt.m_pBufs->globalVertexBuffer, srt.m_pBufs->inputUv, curVertexIdx);

	float3 adjVtxPos = srt.m_pBufs->inputPositionStream[adjIndex];
	float2 adjVtxUv = LoadVertexAttribute<float2, 32>(srt.m_pBufs->globalVertexBuffer, srt.m_pBufs->inputUv, adjIndex);

	float3 prevEdge = adjVtxPos - baseVtxPos;
	float2 prevDelta = adjVtxUv - baseVtxUv;

	float3 t = float3(0.0f, 0.0f, 0.0f);
	float3 b = float3(0.0f, 0.0f, 0.0f);
	float3 n = float3(0.0f, 0.0f, 0.0f);
	float numContributions = 0.0f;

	for (uint vtxIndex = 1; vtxIndex < valence; vtxIndex++)
	{
		uint adjPlusIndex = srt.m_pBufs->inputIndicesData[offset + vtxIndex];

		// get adjacent vertex
		adjVtxPos = srt.m_pBufs->inputPositionStream[adjPlusIndex];
		adjVtxUv = LoadVertexAttribute<float2, 32>(srt.m_pBufs->globalVertexBuffer, srt.m_pBufs->inputUv, adjPlusIndex);

		float3 currentEdge = adjVtxPos - baseVtxPos;
		float2 currentDelta = adjVtxUv - baseVtxUv;

		n += cross(prevEdge, currentEdge);
		
		// http://www.terathon.com/code/tangent.html
		// Note: The code below will only work when vertices along UV seams are duplicated,
		// otherwise distracting artifacts can be visible along UV seams.
		float r = prevDelta.x * currentDelta.y - currentDelta.x * prevDelta.y;
		float invR = (r == 0.0f) ? 1.0f : (1.0f / r);
		t += (prevEdge * currentDelta.y - currentEdge * prevDelta.y) * invR;
		b += (currentEdge * prevDelta.x - prevEdge * currentDelta.x) * invR;
		numContributions++;

		prevEdge = currentEdge;
		prevDelta = currentDelta;
	}

	float invNumContributions = 1.0f / max(numContributions, 1.0f);
	t *= invNumContributions;
	b *= invNumContributions;

	float3 normal = normalize(n);
	float3 tangent = normalize(t - normal * dot(normal, t)); // Gram-Schmidt orthogonalization
	float handedness = (dot(cross(normal, t), b) < 0.0f) ? 1.0f : -1.0f;
	float3 bitangent = cross(normal, tangent) * handedness;

	float2 stretchFactors;
	stretchFactors.x = abs(dot(t, tangent));
	stretchFactors.y = abs(dot(b, bitangent));

	srt.m_pBufs->outputPositionStream[curVertexIdx] = baseVtxPos;
	srt.m_pBufs->outputNormalStream[curVertexIdx] = normal;
	srt.m_pBufs->outputStretchFactorsStream[curVertexIdx] = stretchFactors;

	PROFILE_END(dispatchThreadId.x);
}
