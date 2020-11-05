struct ShapeInfoParams
{
	uint				m_numVertex;
	uint				m_numEdges;
	uint				m_numPolygons;
	int					m_shapeModifier;
	int					m_vsOffset;
	int					m_isOffset;
	int					m_outputOffset;
	float				m_coneAngle;
	float4				m_transformMat[3];
	float4				m_clipPlane;
};

struct RectShapeModifier
{
	float4				m_verts[8];
};

struct ShapeInputBuffers
{
	ByteAddressBuffer		tShapeVerexData; //				: register(t0);
	ByteAddressBuffer		tShapeLineData; //				: register(t1);
	ByteAddressBuffer		tShapePolygonData; //			: register(t2);
};

struct ShapeOutputBuffers
{
	RWByteAddressBuffer		uVertexStreamBuffer; //			: register(u0);
	RWByteAddressBuffer		uIndexStreamBuffer; //			: register(u1);
	RWByteAddressBuffer		uDrawCallArgsBuffer; //			: register(u2);
};

struct ShapeSrt
{
	ShapeInfoParams *pShapeInfo;
	RectShapeModifier *pShapeModifier;
	ShapeInputBuffers *pInput;
	ShapeOutputBuffers *pOutput;
};

#define kTempBufferSize 7136

groupshared uint		g_wsPos_indices[kTempBufferSize];
groupshared uint		g_linesInfo[1024];
groupshared uint		g_numVertex;
groupshared uint		g_numIndices;
groupshared int			g_numNewEdges;

void CreatePolygonIndexList(inout uint polygonInfo[5], inout uint numIndices, inout uint newEdgeInfo, 
                            uint edgeInfo, uint orgNumVertex)
{
	if (edgeInfo & 0x00200400) // this line will be insert to buffer.
	{
		if (edgeInfo & 0x80000000) // insert vertex is needed.
		{
			uint newVertexIdx = ((edgeInfo >> 22) & 0x1ff) + orgNumVertex;
			polygonInfo[numIndices++] = newVertexIdx;
			newEdgeInfo |= newVertexIdx << ((edgeInfo & 0x00000400) != 0 ? 0 : 11);
		}
			
		if (edgeInfo & 0x00200000) // end vertex is needed.
			polygonInfo[numIndices++] = ((edgeInfo >> 11) & 0x3ff);
	}
}

[numthreads(1024, 1, 1)]
void Cs_ScissorGivenShapes(uint3 dispatchThreadId : SV_DispatchThreadID,
                           ShapeSrt srt : S_SRT_DATA)
{
	uint orgNumVertex = srt.pShapeInfo->m_numVertex;
	if (dispatchThreadId.x == 0)
	{
		g_numVertex = orgNumVertex;
		g_numIndices = 0;
		g_numNewEdges = kTempBufferSize -1;
	}

	uint vdOffset = dispatchThreadId.x * 4;
	uint idOffset = dispatchThreadId.x * 3;
	uint vsBufferStart = srt.pShapeInfo->m_vsOffset * 12;
	uint isBufferStart = srt.pShapeInfo->m_isOffset * 4;

	// convert all the vertex to world space.
    if (dispatchThreadId.x < orgNumVertex)
	{
		uint3 vertexData = srt.pInput->tShapeVerexData.Load3(dispatchThreadId.x * 12);
		float3 currentVertex = float3(asfloat(vertexData.x), asfloat(vertexData.y), asfloat(vertexData.z));
		float4 wsPos;

		if (srt.pShapeInfo->m_shapeModifier != 0 && orgNumVertex == 8)
		{
			currentVertex = srt.pShapeModifier->m_verts[dispatchThreadId.x].xyz;
		}
		else
		if (srt.pShapeInfo->m_coneAngle > 0.0f && dispatchThreadId.x >= 2)
		{
			float orgAngle = acos(currentVertex.y);
			float angleRatio = (srt.pShapeInfo->m_coneAngle / 2.0f) / (3.1415926f / 4.0f);
			float sinAngle, cosAngle;
			sincos(orgAngle * angleRatio, sinAngle, cosAngle);

			currentVertex.y = cosAngle;
			currentVertex.xz *= (sinAngle / sin(orgAngle));
		}

		wsPos.x = dot(float4(currentVertex, 1.0f), srt.pShapeInfo->m_transformMat[0]);
		wsPos.y = dot(float4(currentVertex, 1.0f), srt.pShapeInfo->m_transformMat[1]);
		wsPos.z = dot(float4(currentVertex, 1.0f), srt.pShapeInfo->m_transformMat[2]);
		wsPos.w = dot(float4(wsPos.xyz, 1.0f), srt.pShapeInfo->m_clipPlane);

		g_wsPos_indices[vdOffset] = asuint(wsPos.x);
		g_wsPos_indices[vdOffset + 1] = asuint(wsPos.y);
		g_wsPos_indices[vdOffset + 2] = asuint(wsPos.z);
		g_wsPos_indices[vdOffset + 3] = asuint(wsPos.w);
	}

	GroupMemoryBarrierWithGroupSync();

	// do intersect dect of lines.
	if (dispatchThreadId.x < srt.pShapeInfo->m_numEdges)
	{
		uint edgeData = srt.pInput->tShapeLineData.Load(dispatchThreadId.x * 4);
		uint2 vertexIdx = uint2(edgeData, edgeData >> 11) & 0x3ff;

		float4 wsPos0 = float4(asfloat(g_wsPos_indices[vertexIdx.x*4]), 
							   asfloat(g_wsPos_indices[vertexIdx.x*4+1]), 
							   asfloat(g_wsPos_indices[vertexIdx.x*4+2]), 
							   asfloat(g_wsPos_indices[vertexIdx.x*4+3]));
		float4 wsPos1 = float4(asfloat(g_wsPos_indices[vertexIdx.y*4]), 
							   asfloat(g_wsPos_indices[vertexIdx.y*4+1]), 
							   asfloat(g_wsPos_indices[vertexIdx.y*4+2]), 
							   asfloat(g_wsPos_indices[vertexIdx.y*4+3]));

		uint finalBits = 0;
		if (wsPos0.w > 0)
			finalBits |= 0x00000400;
		if (wsPos1.w > 0)
			finalBits |= 0x00200000;

		if (finalBits == 0x00000400 || finalBits == 0x00200000) // intersect with ClipPlane
		{
			float4 stepVec = wsPos1 - wsPos0;
			float4 interVertex = stepVec / stepVec.w * (-wsPos0.w) + wsPos0;

			uint newVertexIdx;
			InterlockedAdd(g_numVertex, 1u, newVertexIdx);
			g_wsPos_indices[newVertexIdx * 4] = asuint(interVertex.x);
			g_wsPos_indices[newVertexIdx * 4 + 1] = asuint(interVertex.y);
			g_wsPos_indices[newVertexIdx * 4 + 2] = asuint(interVertex.z);
			g_wsPos_indices[newVertexIdx * 4 + 3] = asuint(interVertex.w);
			finalBits |= (((newVertexIdx - orgNumVertex) << 22) | 0x80000000);
		}

		g_linesInfo[dispatchThreadId.x] = finalBits | edgeData;
	}

	GroupMemoryBarrierWithGroupSync();

	if (dispatchThreadId.x < g_numVertex)
	{
		uint3 vertexData = uint3(g_wsPos_indices[vdOffset], g_wsPos_indices[vdOffset + 1], g_wsPos_indices[vdOffset + 2]);
		srt.pOutput->uVertexStreamBuffer.Store3(vsBufferStart + vdOffset * 3, vertexData);
	}

	if (dispatchThreadId.x + 1024 < g_numVertex)
	{
		vdOffset += 4096;
		uint3 vertexData = uint3(g_wsPos_indices[vdOffset], g_wsPos_indices[vdOffset + 1], g_wsPos_indices[vdOffset + 2]);
		srt.pOutput->uVertexStreamBuffer.Store3(vsBufferStart + vdOffset * 3, vertexData);
	}

	GroupMemoryBarrierWithGroupSync();

	// scissor triangles.
	if (dispatchThreadId.x < srt.pShapeInfo->m_numPolygons)
	{
		uint2 polygonInfo = srt.pInput->tShapePolygonData.Load2(dispatchThreadId.x * 8);
		uint numVertex = polygonInfo.x >> 24;
		uint4 edgeIndex;
		edgeIndex.x = polygonInfo.x;
		edgeIndex.y = polygonInfo.x >> 12;
		edgeIndex.z = polygonInfo.y;
		edgeIndex.w = polygonInfo.y >> 12;

		bool4 bInverseOrder = (edgeIndex & 0x800) != 0;
		edgeIndex = edgeIndex & 0x7ff;

		uint4 edgeInfo = uint4(g_linesInfo[edgeIndex.x], g_linesInfo[edgeIndex.y], g_linesInfo[edgeIndex.z], numVertex > 3 ? g_linesInfo[edgeIndex.w] : 0);
		uint4 inverseEdgeInfo = (edgeInfo & 0xffc00000) | ((edgeInfo & 0x7ff) << 11) | ((edgeInfo >> 11) & 0x7ff);
		edgeInfo = bInverseOrder ? inverseEdgeInfo : edgeInfo;

		uint numIndices = 0;
		uint aIndex[5] = { 0, 0, 0, 0, 0 };

		uint newEdgeInfo = 0;
		CreatePolygonIndexList(aIndex, numIndices, newEdgeInfo, edgeInfo.x, orgNumVertex);
		CreatePolygonIndexList(aIndex, numIndices, newEdgeInfo, edgeInfo.y, orgNumVertex);
		CreatePolygonIndexList(aIndex, numIndices, newEdgeInfo, edgeInfo.z, orgNumVertex);
		CreatePolygonIndexList(aIndex, numIndices, newEdgeInfo, edgeInfo.w, orgNumVertex);

		if (newEdgeInfo != 0)
		{
			int newEdgeIdx;
			InterlockedAdd(g_numNewEdges, -1, newEdgeIdx);
			g_wsPos_indices[newEdgeIdx] = newEdgeInfo;
		}

		if (numIndices >= 3)
		{
			uint newIndexIdx;
			InterlockedAdd(g_numIndices, (numIndices - 2) * 3, newIndexIdx);

			g_wsPos_indices[newIndexIdx] = aIndex[0];
			g_wsPos_indices[newIndexIdx+1] = aIndex[1];
			g_wsPos_indices[newIndexIdx+2] = aIndex[2];

			if (numIndices > 3)
			{
				g_wsPos_indices[newIndexIdx+3] = aIndex[0];
				g_wsPos_indices[newIndexIdx+4] = aIndex[2];
				g_wsPos_indices[newIndexIdx+5] = aIndex[3];
			}

			if (numIndices == 5)
			{
				g_wsPos_indices[newIndexIdx+6] = aIndex[0];
				g_wsPos_indices[newIndexIdx+7] = aIndex[3];
				g_wsPos_indices[newIndexIdx+8] = aIndex[4];
			}
		}
	}

	GroupMemoryBarrierWithGroupSync();

	uint numNewEdges = kTempBufferSize -1 - g_numNewEdges;
	uint baseNewEdgeIdx = g_numNewEdges + 1;
	uint fanCenterVertexIdx = g_wsPos_indices[baseNewEdgeIdx] & 0x7ff;

	if (dispatchThreadId.x < numNewEdges)
	{
		uint2 edgeVertexIdx;
		edgeVertexIdx.x = g_wsPos_indices[baseNewEdgeIdx + dispatchThreadId.x];
		edgeVertexIdx.y = edgeVertexIdx.x >> 11;

		edgeVertexIdx = edgeVertexIdx & 0x7ff;

		if (edgeVertexIdx.x != fanCenterVertexIdx && edgeVertexIdx.y != fanCenterVertexIdx) // valid Edge.
		{
			uint newIndexIdx;
			InterlockedAdd(g_numIndices, 3, newIndexIdx);

			g_wsPos_indices[newIndexIdx] = fanCenterVertexIdx;
			g_wsPos_indices[newIndexIdx+1] = edgeVertexIdx.y;
			g_wsPos_indices[newIndexIdx+2] = edgeVertexIdx.x;
		}
	}

	GroupMemoryBarrierWithGroupSync();

	uint indexCountPerInstance = g_numIndices;
	uint instanceCount = 1;
	uint startInstanceLocation = 0;
	uint isOffset = srt.pShapeInfo->m_isOffset;
	uint vsOffset = srt.pShapeInfo->m_vsOffset;

	if (idOffset < g_numIndices)
	{
		uint3 indiceData = uint3(g_wsPos_indices[idOffset], g_wsPos_indices[idOffset+1], g_wsPos_indices[idOffset+2]);
		srt.pOutput->uIndexStreamBuffer.Store3(isBufferStart + idOffset * 4, indiceData);
	}

	idOffset += 3072;
	if (idOffset < g_numIndices)
	{
		uint3 indiceData = uint3(g_wsPos_indices[idOffset], g_wsPos_indices[idOffset+1], g_wsPos_indices[idOffset+2]);
		srt.pOutput->uIndexStreamBuffer.Store3(isBufferStart + idOffset * 4, indiceData);
	}

	uint outputOffset = srt.pShapeInfo->m_outputOffset;

	uint4 params0 = uint4(indexCountPerInstance, instanceCount, 0, 0);

	srt.pOutput->uDrawCallArgsBuffer.Store4(outputOffset, params0);
	srt.pOutput->uDrawCallArgsBuffer.Store(outputOffset + 16, startInstanceLocation);
}

