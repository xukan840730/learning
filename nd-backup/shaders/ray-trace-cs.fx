/*
* Copyright (c) 2013 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "profile.fxi"
#include "global-funcs.fxi"
#include "atomics.fxi"
#include "sphere-cast.fx"

#define ND_PSSL
#include "compressed-vsharp.fxi"
#include "compressed-tangent-frame.fxi"
#include "atomics.fxi"

// ====================================================================================================
//                                        Ray-Tracing on the GPU
// ----------------------------------------------------------------------------------------------------
//
// This compute shader calculates ray-vs-triangle intersections, using regular 16-bit vertex indexes
// and 32-bit X,Y,Z positions in object space as input.
//
// ====================================================================================================

#pragma warning (default:7203)

// Because we use the Mat34 type, the matrices need to be row major so we can transform by them by
// multiplying with the matrix on the left and the vector/point on the right!
#pragma pack_matrix (row_major)

// ====================================================================================================
//   Constants
// ----------------------------------------------------------------------------------------------------
static float kRayMinDist = 1e-4f;

// must match with constants in MeshRayCastJob
static uint kAllowBackFacingTriangles  = (1 << 0);
static uint kIgnoreFrontFacingTriangles = (1 << 1);

// must match with constants in MeshRayCastJob
static uint kHitDebug = (1 << 0);			// All geometry will have kRayHitDebug, it's mostly used by surfer-probe and debugging purpose
static uint kHitDoubleSided = (1 << 1);		// Double sided material/geometry. Ray can hit either side. WARNING: this flag is reserved for mesh. MeshRay User shouldn't set it.

#define kMaxRaysPerFrame 64 // must match with MeshRayCaster::kMaxRaysPerFrame
#define kMaxNumHits 2 // must match with MeshProbe::kMaxNumHits
#define kNumTrianglesPerBlock 64 // each block has 64 triangles.

#define kMaxSubmeshes 1024
#define kMaxSubmeshBlocks (64 * kMaxSubmeshes)

// ====================================================================================================
//   Constant Buffers
// ----------------------------------------------------------------------------------------------------
struct RayCastInstanceConstants
{
	float3x4					m_objToWorld;				// Transform from object-space to world-space
	float3x4					m_worldToObj;				// Transform from world-space to object-space
	StructuredBuffer<float4x4>	m_perInstanceObjToWorld;	// Transform from obj space to world space for each instace in the cluster.
	uint						m_rayMasks0;				// Ray masks to indicate whether ray and mesh instance can potentially intersect, lower 32 bits.
	uint						m_rayMasks1;				// Ray masks to indicate whether ray and mesh instance can potentially intersect, higher 32 bits.
	uint						m_numRays;					// Number of input rays
	int							m_levelId;					// Which level we're casting against, or -1 if foreground
	uint						m_instanceId;				// The mesh index or instance index of the object cast against
};

// Pack ray index and hit dist together into U32 so that we can easily sort by using m_rayIdxAndDist.
struct CsRayCastSortInfo
{
	uint m_rayIdxAndDist;
	uint m_hitIdx;
};

struct CsBoundingBox
{
	float3 m_min;
	float3 m_max;
};

// ====================================================================================================
//   Structures
// ----------------------------------------------------------------------------------------------------
struct CsMeshRayHit
{
	// Vertex attributes of the hit triangle.
	float3	m_vertexPos[3];
	float3	m_vertexNormal[3];
	float4	m_vertexTangent[3];
	float3	m_vertexColor0[3];
	float3	m_vertexColor1[3];
	float2	m_vertexUv0[3];
	float2	m_vertexUv3[3];

	float3	m_positionWs;			// World-space position
	float3	m_posOs;				// Object-space hit pos.

	float3	m_vertexWs0;			// Optional debug data
	float3	m_vertexWs1;			// Optional debug data
	float3	m_vertexWs2;			// Optional debug data

	float3	m_normalWs;				// World-space unit-length normal
	float3	m_normalOs;				// Object-space triangle normal

	float3	m_color0;				// Color0 value
	float3	m_color1;				// Color1 value

	float2	m_uv0;					// Texture uv coordinates
	float2	m_uv3;					// Texture uv coordinates for second uv set

	float2	m_st;					// Barycentric st coordinates
	float	m_t;					// Ray intersection t value

	uint	m_tri;					// Hit triangle index
	uint	m_instance;				// Instance in the cluster hit

	uint	m_rayLevelIndex;		// Ray index [15:0] and level index [31:16]
	uint	m_instanceMeshIndex;	// Mesh/Foreground instance index [15:0], sub-mesh index [31:16]
	uint	m_debugRayIdxAndHitIdx;
};

struct CsMeshRayMultiHits
{
	CsMeshRayHit m_hits[kMaxNumHits];
};

struct CsInputProbe
{
	float3 m_orig;				// Start position (time = 0.0)
	uint   m_behaviorMask;
	uint   m_hitMask;
	float3 m_dir;				// Normalized direction
	float  m_maxT;				// Maximum intersection distance (inverted before saving to LDS!)
	float  m_radius;			// radius of probe.
	uint3  m_pad3;
};

struct CsSubMeshInfo
{
	float3x4		m_world2ObjectMat;
	CsBoundingBox	m_bbox;
	uint2			m_rayMask;
	uint			m_rayCastTypeMask;
	uint			m_meshBehaviorMask;
	uint2			m_pad2;
};

struct CsSubMeshInstanceInfo
{
	uint m_levelAndInstanceId; // Stores the level id in the upper 8 bits.
};

struct CsValidation
{
	uint4 m_vec0;
	uint4 m_vec1;
	uint4 m_vec2;
	uint4 m_vec3;
	uint4 m_vec4;
	uint4 m_vec5;
	uint4 m_vec6;
	uint4 m_vec7;
};

// ====================================================================================================
//   Support functions
// ----------------------------------------------------------------------------------------------------
float3 pmul(in float3x4 m, in float3 p)
{
	// Transform the input point on the right by the matrix on the left,
	// with an implicit 1.0 for the point's w component.
	float3 pp;

	pp.x = (m[0].x * p.x) + (m[0].y * p.y) + (m[0].z * p.z) + m[0].w;
	pp.y = (m[1].x * p.x) + (m[1].y * p.y) + (m[1].z * p.z) + m[1].w;
	pp.z = (m[2].x * p.x) + (m[2].y * p.y) + (m[2].z * p.z) + m[2].w;

	return pp;
}

float3 vmul(in float3x4 m, in float3 v)
{
	// Transform the input vector on the right by the matrix on the left,
	// ignoring the matrix's last column (containing the translation).
	float3 vp;

	vp.x = (m[0].x * v.x) + (m[0].y * v.y) + (m[0].z * v.z);
	vp.y = (m[1].x * v.x) + (m[1].y * v.y) + (m[1].z * v.z);
	vp.z = (m[2].x * v.x) + (m[2].y * v.y) + (m[2].z * v.z);

	return vp;
}

float GetBlockMin(float x)
{
	float minX = x;
	minX = min(minX, LaneSwizzle(minX, 0x1f, 0x00, 0x01));
	minX = min(minX, LaneSwizzle(minX, 0x1f, 0x00, 0x08));
	minX = min(minX, LaneSwizzle(minX, 0x1f, 0x00, 0x02));
	minX = min(minX, LaneSwizzle(minX, 0x1f, 0x00, 0x10));
	minX = min(minX, LaneSwizzle(minX, 0x1f, 0x00, 0x04));
	return min(ReadFirstLane(minX), ReadLane(minX, 0x20));
}

float GetBlockMax(float x)
{
	float maxX = x;
	maxX = max(maxX, LaneSwizzle(maxX, 0x1f, 0x00, 0x01));
	maxX = max(maxX, LaneSwizzle(maxX, 0x1f, 0x00, 0x08));
	maxX = max(maxX, LaneSwizzle(maxX, 0x1f, 0x00, 0x02));
	maxX = max(maxX, LaneSwizzle(maxX, 0x1f, 0x00, 0x10));
	maxX = max(maxX, LaneSwizzle(maxX, 0x1f, 0x00, 0x04));
	return max(ReadFirstLane(maxX), ReadLane(maxX, 0x20));
}

uint GetBlockMinU(uint x)
{
	uint minX = x;
	minX = min(minX, LaneSwizzle(minX, 0x1f, 0x00, 0x01));
	minX = min(minX, LaneSwizzle(minX, 0x1f, 0x00, 0x08));
	minX = min(minX, LaneSwizzle(minX, 0x1f, 0x00, 0x02));
	minX = min(minX, LaneSwizzle(minX, 0x1f, 0x00, 0x10));
	minX = min(minX, LaneSwizzle(minX, 0x1f, 0x00, 0x04));
	return min(ReadFirstLane(minX), ReadLane(minX, 0x20));
}

uint SumBlockCounts(uint x)
{
	uint sumX = x;
	sumX += LaneSwizzle(sumX, 0x1f, 0x00, 0x01);
	sumX += LaneSwizzle(sumX, 0x1f, 0x00, 0x08);
	sumX += LaneSwizzle(sumX, 0x1f, 0x00, 0x02);
	sumX += LaneSwizzle(sumX, 0x1f, 0x00, 0x10);
	sumX += LaneSwizzle(sumX, 0x1f, 0x00, 0x04);
	return ReadFirstLane(sumX) + ReadLane(sumX, 0x20);
}

bool RayBoundingBoxCheck(CsBoundingBox box, float3 position, float3 direction, float rayMaxT)
{
	float3 invD = 1.0f / direction;

	float3 t0 = (box.m_min - position) * invD;
	float3 t1 = (box.m_max - position) * invD;

	float tMin = max3(min(t0.x, t1.x), min(t0.y, t1.y), min(t0.z, t1.z));
	float tMax = min3(max(t0.x, t1.x), max(t0.y, t1.y), max(t0.z, t1.z));

	return tMax >= 0.0f && tMax >= tMin;
}

// rough intersection test between sphere-cast and Aabb.
// by expanding Aabb by radius and do ray-boundingbox test.
bool SphereCastBoundingBoxCheck(CsBoundingBox box, float3 position, float3 direction, float rayMaxT, float radius)
{
	CsBoundingBox expandedBox = box;
	expandedBox.m_min -= float3(radius, radius, radius);
	expandedBox.m_max += float3(radius, radius, radius);
	return RayBoundingBoxCheck(expandedBox, position, direction, rayMaxT);
}

bool IsTriangleValid(const float3 opos[3])
{
	// avoid degenerated triangle

	bool isValid0 = all(!isnan(opos[0])) && dot(opos[0], opos[0]) < (1e10f * 1e10f);
	bool isValid1 = all(!isnan(opos[1])) && dot(opos[1], opos[1]) < (1e10f * 1e10f);
	bool isValid2 = all(!isnan(opos[2])) && dot(opos[2], opos[2]) < (1e10f * 1e10f);

	if (isValid0 && isValid1 && isValid2)
	{
		const float3 edge0 = opos[1] - opos[0];
		const float3 edge1 = opos[2] - opos[1];
		const float3 edge2 = opos[0] - opos[2];

		isValid0 = dot(edge0, edge0) > (1e-6f * 1e-6f);
		isValid1 = dot(edge1, edge1) > (1e-6f * 1e-6f);
		isValid2 = dot(edge2, edge2) > (1e-6f * 1e-6f);

		if (isValid0 && isValid1 && isValid2)
		{
			const float3 normal0 = cross(edge0, edge1);
			isValid0 = dot(normal0, normal0) > (1e-8f * 1e-8f);
			return isValid0;
		}
	}
	return false;
}

bool TestTriangleFacing(in float3 edge1, in float3 edge2, in float3 rayDir, in uint rayBehaviorMask, in uint rayCastTypeMask)
{
	if (rayCastTypeMask & kHitDoubleSided)
		return true;

	// Test which direction this triangle's normal is facing and if that matches
	// up with the flags for this ray. Since we only care about the dot product's

	const float3 normalLS = cross(edge1, edge2);
	const float ndotd = dot(normalLS, rayDir);

	// Not passing through back faces OR face must be front face (N.D must be negative)!
	bool facingValid = (rayBehaviorMask & kAllowBackFacingTriangles) || ndotd < 0.0f;
	// Not passing through front faces OR face must be back face (N.D must be positive)!
	facingValid = facingValid && (!(rayBehaviorMask & kIgnoreFrontFacingTriangles) || ndotd >= 0.0f);
	return facingValid;
}

bool RayTriangleIntersect(out float bu, out float bv, inout float t, out float3 normalLS, in float3 pos[3], in float3 rayPos, in float3 rayDir, in float invRayMaxT)
{
	// Intersect the ray with the triangle! Calculate the intersection
	// time t, and the u and v barycentric coordinates.
	bool intersects = false;

	const float3 edge1 = pos[1] - pos[0];
	const float3 edge2 = pos[2] - pos[0];

	float3 pvec     = cross(rayDir, edge2);
	float  det      = dot(edge1, pvec);

	normalLS = cross(edge1, edge2); // always calculate the normal incase user needs it even if ray and triangle don't intersect

	if (det != 0.0f && abs(det) < 1e+10f)	// just in case we hit a bad triangle
	{
		intersects = true;

		float  inv_det = 1.0f / det;
		float3 svec = rayPos - pos[0];

		bu = dot(svec, pvec) * inv_det;
		if (bu < 0.0f || bu >= 1.0f)
			intersects = false;

		float3 qvec = cross(svec, edge1);

		bv = dot(rayDir, qvec) * inv_det;
		if (bv < 0.0f || bu + bv >= 1.0f)
			intersects = false;

		// Calculate the intersection distance, in world units
		float dist = dot(edge2, qvec) * inv_det;

		// Normalize the intersection distance by dividing by maxT
		t = dist * invRayMaxT;

		// Use a small minimum to avoid precision issues when the ray hits a triangle
		// very close to its origin.
		if (isnan(t) || dist < kRayMinDist || t >= 1.0f)
			intersects = false;
	}

	return intersects;
}

uint PowerOfTwoHigher(in uint v)
{
	uint fsbh = FirstSetBit_Hi(v);	// Find first set bit (starting from MSB)
	uint fsbl = FirstSetBit_Lo(v);	// Find first set bit (starting from LSB)

	return (fsbh == fsbl) ? v : (1 << (fsbh + 1));
}

void swap(inout CsRayCastSortInfo x, inout CsRayCastSortInfo y)
{
	CsRayCastSortInfo tmp = y;
	y = x;
	x = tmp;
}

bool IsRayMaskValid(uint2 rayMask, uint rayIndex)
{
	bool maskIsValid = false;
	if (rayIndex < 32)
		maskIsValid = (rayMask.x & (1 << rayIndex));
	else
		maskIsValid = (rayMask.y & (1 << (rayIndex - 32)));

	return maskIsValid;
}

bool IsTypeMatched(uint rayHitMask, uint rayBehaviorMask, uint meshRayCastTypeMask, uint meshBehaviorMask)
{
	if ((rayHitMask & meshRayCastTypeMask) != 0)
	{
		if (meshBehaviorMask == 0 || (rayBehaviorMask & meshBehaviorMask) != 0)
			return true;
	}
	return false;
}

bool IsTypeMatched(in CsInputProbe inputProbe, in CsSubMeshInfo subMeshInfo)
{
	return IsTypeMatched(inputProbe.m_hitMask, inputProbe.m_behaviorMask, subMeshInfo.m_rayCastTypeMask, subMeshInfo.m_meshBehaviorMask);
}

bool IsInstancedStream(Buffer<float3> stream, uint numVertices)
{
#if HARDWARE_INSTANCED
	uint size = 0;
	stream.GetDimensions(size);
	return size > numVertices;
#else
	return false;
#endif
}

bool IsInstancedStream(CompressedVSharp stream, uint numVertices)
{
#if HARDWARE_INSTANCED
	if (IsCompressedVSharpCompressed(stream))
	{
		return GetCompressedVSharpGetNumElements(stream) > numVertices;
	}
	else
	{
		uint size = 0;
		GetCompressedVSharpUncompressed(stream).GetDimensions(size);
		return size > numVertices;
	}
#else
	return false;
#endif
}

float3x4 GetObjToCluster(RayCastInstanceConstants *pInstanceConsts, uint instanceId)
{
	// If we're instanced we do the intersection is cluster space.
#if HARDWARE_INSTANCED
	float4x4 worldToObj = float4x4(pInstanceConsts->m_worldToObj,
								   float4(0.0f, 0.0f, 0.0f, 1.0f));

	float4x4 result = mul(worldToObj, pInstanceConsts->m_perInstanceObjToWorld[instanceId]);
	return float3x4(result[0], result[1], result[2]);
#else
	return float4x4(float4(1.0f, 0.0f, 0.0f, 0.0f),
					float4(0.0f, 1.0f, 0.0f, 0.0f),
					float4(0.0f, 0.0f, 1.0f, 0.0f),
					float4(0.0f, 0.0f, 0.0f, 1.0f));
#endif
}

struct CsMeshProbeInfo
{
	float3 m_posLS;
	float m_maxT;
	float3 m_dirLS;
	float m_radius;
	uint m_globalRayIndex;
	uint m_rayBehaviorMask;
	uint m_rayCastTypeMask; // the same as mesh::m_rayCastTypeMask
	uint m_validation;
};

struct CreateRayMeshInfoBufferSrt
{
	uint								m_numRays;
	uint								m_numSubMeshes;
	StructuredBuffer<CsInputProbe>		m_probeListBuffer;
	StructuredBuffer<CsSubMeshInfo>		m_subMeshInfoBuffer;
	RWByteAddressBuffer					m_rwMeshRayCounter;
	RWStructuredBuffer<CsMeshProbeInfo> m_rwMeshProbeInfoBuffer;
};

[numthreads(64, 1, 1)]
void Cs_CreateRayMeshInfoBuffer(int2 dispatchThreadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex, int3 groupId : SV_GroupID, CreateRayMeshInfoBufferSrt* pSrt  : S_SRT_DATA)
{
	PROFILE_START(groupIndex);

	const uint globalRayIndex = groupId.y;
	const uint submeshIndex = dispatchThreadId.x;
	const uint probeInfoOffset = submeshIndex * kMaxRaysPerFrame;

	CsInputProbe castProbe = pSrt->m_probeListBuffer[globalRayIndex];
	if (submeshIndex < pSrt->m_numSubMeshes)
	{
		CsSubMeshInfo subMeshInfo = pSrt->m_subMeshInfoBuffer[submeshIndex];

		if (IsRayMaskValid(subMeshInfo.m_rayMask.xy, globalRayIndex) && IsTypeMatched(castProbe, subMeshInfo))
		{
			float3 orgOS = pmul(subMeshInfo.m_world2ObjectMat, castProbe.m_orig);
			float3 dirOS = vmul(subMeshInfo.m_world2ObjectMat, castProbe.m_dir);

			bool intersect = false;
			if (castProbe.m_radius > 0.0f)
				intersect = SphereCastBoundingBoxCheck(subMeshInfo.m_bbox, orgOS, dirOS, castProbe.m_maxT, castProbe.m_radius);
			else
				intersect = RayBoundingBoxCheck(subMeshInfo.m_bbox, orgOS, dirOS, castProbe.m_maxT);

			if (intersect)
			{
				uint meshRayBufferIdx;
				pSrt->m_rwMeshRayCounter.InterlockedAdd(submeshIndex * 4, 1, meshRayBufferIdx);

				CsMeshProbeInfo meshProbeInfo;
				meshProbeInfo.m_posLS = orgOS;
				meshProbeInfo.m_maxT = castProbe.m_maxT;
				meshProbeInfo.m_dirLS = dirOS;
				meshProbeInfo.m_radius = castProbe.m_radius;
				meshProbeInfo.m_globalRayIndex = globalRayIndex;
				meshProbeInfo.m_rayBehaviorMask = castProbe.m_behaviorMask;
				meshProbeInfo.m_rayCastTypeMask = subMeshInfo.m_rayCastTypeMask;
				meshProbeInfo.m_validation = 0xdeadbeaf;

				pSrt->m_rwMeshProbeInfoBuffer[probeInfoOffset + meshRayBufferIdx] = meshProbeInfo;
			}
		}
	}

	PROFILE_END(groupIndex);
}

struct FillCreateBlockBoundingBoxIndirectArgsSrt
{
	uint						m_numSubMeshes;
	StructuredBuffer<uint>		m_meshRayCounter;
	RWStructuredBuffer<uint4>	m_rwArgsBuffer;
	RWStructuredBuffer<uint4>	m_rwBlockArgsBuffer;
};

[numthreads(64, 1, 1)]
void Cs_FillCreateBlockBoundingBoxIndirectArgs(int dispatchThreadId : SV_DispatchThreadID, FillCreateBlockBoundingBoxIndirectArgsSrt* pSrt : S_SRT_DATA)
{
	const uint submeshIdx = dispatchThreadId;
	if (submeshIdx < pSrt->m_numSubMeshes)
	{
		if (pSrt->m_meshRayCounter[submeshIdx] == 0)
		{
			pSrt->m_rwArgsBuffer[submeshIdx] = uint4(0, 1, 1, 1);
		}

		// Clear the block indirect args used by indirect calls to Cs_CheckRayPolygonHit. These args will be updated later in Cs_CullingBlockMesh.
		// This clear is not strictly necassary but since this buffer is allocated and cleared entirely by the CPU the block indirect args would
		// constantly increase when you run a razor capture (since we update the args using atomic increments). 
		pSrt->m_rwBlockArgsBuffer[submeshIdx] = uint4(0, 1, 1, 1);
	}
}

struct CreateBlockBoundingBoxSrt
{
	RayCastInstanceConstants*			m_pInstanceConsts;
	uint4								m_globalVertBuffer;
	Buffer<uint>    					m_indexStream;
	Buffer<float3>						m_posOffsetStream;
	CompressedVSharp   					m_posStream;
	Buffer<uint>    					m_subMeshBlockIdx;
	Buffer<uint>						m_subMeshTriangleIdx;
	RWStructuredBuffer<CsBoundingBox>	m_rwBlockBounds;
	uint								m_numVertices;
	uint								m_numIndices;
	uint								m_numTriangles;
	uint								m_meshIndex;
};

groupshared uint gs_indexStream[kNumTrianglesPerBlock * 3]; // each triangle has 3 vertices
groupshared uint gs_numTriangles;

[numthreads(kNumTrianglesPerBlock, 1, 1)]
void Cs_CreateBlockBoundingBox(int dispatchThreadId : SV_DispatchThreadID, uint3 groupId : SV_GroupID, int groupThreadId : SV_GroupThreadId, CreateBlockBoundingBoxSrt* pSrt : S_SRT_DATA)
{
	PROFILE_START(dispatchThreadId);

	const uint subMeshBlockStartIdx = pSrt->m_subMeshBlockIdx[pSrt->m_meshIndex];
	const uint blockIdx = subMeshBlockStartIdx + groupId.x;

	// load index stream into shared memory
	{
		if (groupThreadId == 0)
		{
			gs_numTriangles = pSrt->m_numTriangles;
		}

		const bool posInstanced = IsInstancedStream(pSrt->m_posStream, pSrt->m_numVertices);

		GroupMemoryBarrierWithGroupSync();

		// 3 phases will copy the whole block's index-buffer into shared memory.
		for (int ph = 0; ph < 3; ph++)
		{
			int sharedMemIndex = kNumTrianglesPerBlock * ph + groupThreadId;
			int globalIndex = kNumTrianglesPerBlock * (3 * groupId.x + ph) + groupThreadId;

			if (globalIndex < gs_numTriangles * 3)
			{
				#if HARDWARE_INSTANCED
					uint iIndex = globalIndex % pSrt->m_numIndices;
					uint instanceId = globalIndex / pSrt->m_numIndices;
					uint offset = posInstanced * instanceId * pSrt->m_numVertices;
				#else
					uint iIndex = globalIndex;
					uint offset = 0;
				#endif

				gs_indexStream[sharedMemIndex] = pSrt->m_indexStream[iIndex] + offset;
			}
		}

		GroupMemoryBarrierWithGroupSync();
	}

	float3 triangleBboxMin = 1e30, triangleBboxMax = -1e30;

	// Triangles are indexed along the X axis of each thread group.
	uint iTriangle = dispatchThreadId;
	if (iTriangle < gs_numTriangles)
	{
		uint iIndex = groupThreadId * 3;
		uint3 vidx;
		vidx.x = gs_indexStream[iIndex + 0];
		vidx.y = gs_indexStream[iIndex + 1];
		vidx.z = gs_indexStream[iIndex + 2];

		uint instanceId = (iTriangle * 3) / pSrt->m_numIndices;
		float3x4 objToCluster = GetObjToCluster(pSrt->m_pInstanceConsts, instanceId);

		uint3 baseIdx = vidx % pSrt->m_numVertices;
		uint3 posOffsetIdx = baseIdx + uint3(IsInstancedStream(pSrt->m_posOffsetStream, pSrt->m_numVertices) * instanceId * pSrt->m_numVertices);

		// vertex stream is random access pattern, it's hard to move it into shared memory.
		float3 v[3];
		v[0] = pmul(objToCluster, LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_posStream, vidx.x) + pSrt->m_posOffsetStream[posOffsetIdx.x]);
		v[1] = pmul(objToCluster, LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_posStream, vidx.y) + pSrt->m_posOffsetStream[posOffsetIdx.y]);
		v[2] = pmul(objToCluster, LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_posStream, vidx.z) + pSrt->m_posOffsetStream[posOffsetIdx.z]);

		if (IsTriangleValid(v))
		{
			triangleBboxMin = min3(v[0], v[1], v[2]);
			triangleBboxMax = max3(v[0], v[1], v[2]);
		}
	}

	float3 blockBboxMin = float3(GetBlockMin(triangleBboxMin.x), GetBlockMin(triangleBboxMin.y), GetBlockMin(triangleBboxMin.z));
	float3 blockBboxMax = float3(GetBlockMax(triangleBboxMax.x), GetBlockMax(triangleBboxMax.y), GetBlockMax(triangleBboxMax.z));

	blockBboxMin -= abs(blockBboxMin) / 1000.0f;
	blockBboxMax += abs(blockBboxMax) / 1000.0f;

	if (groupThreadId == 0)
	{
		pSrt->m_rwBlockBounds[blockIdx].m_min = blockBboxMin;
		pSrt->m_rwBlockBounds[blockIdx].m_max = blockBboxMax;
	}

	PROFILE_END(dispatchThreadId);
}

struct CsDispatchBlock
{
	uint m_rayMeshBufferOffset;
	uint m_localBlockIdx; // block index in submesh, ranged from [0 : numBlocksInThisSubMesh]
	uint2 m_potentialRayHitMask;
	uint m_validation;
	uint m_submeshIdx;
	uint2 m_pad;
};

struct CullingBlockMeshSrt
{
	Buffer<uint>    					m_blockSubmeshIdxBuffer;
	Buffer<uint>    					m_subMeshBlockIdxBuffer;
	StructuredBuffer<uint>				m_meshRayCount1Buffer;
	StructuredBuffer<CsMeshProbeInfo>	m_meshProbeInfoBuffer;
	StructuredBuffer<CsBoundingBox>		m_blockBoundsBuffer;
	RWByteAddressBuffer					m_rwBlockArgsBuffer;
	RWStructuredBuffer<CsDispatchBlock> m_rwDispatchBlockInfo;
	RWStructuredBuffer<CsValidation>	m_rwValidation;
	uint								m_numTotalBlocks;
};

[numthreads(64, 1, 1)]
void Cs_CullingBlockMesh(int dispatchThreadId : SV_DispatchThreadID, int groupId : SV_GroupID, int groupThreadId : SV_GroupThreadId, CullingBlockMeshSrt* pSrt : S_SRT_DATA)
{
	PROFILE_START(dispatchThreadId);

	if (dispatchThreadId < pSrt->m_numTotalBlocks)
	{
		const uint submeshIdx = pSrt->m_blockSubmeshIdxBuffer[dispatchThreadId] >> 16;
		const uint localBlockIdx = pSrt->m_blockSubmeshIdxBuffer[dispatchThreadId] & 0xffff;
		const uint rayMeshBufferOffset = submeshIdx * kMaxRaysPerFrame;
		const uint rayMeshBufferCount = pSrt->m_meshRayCount1Buffer[submeshIdx];

		uint2 potentialRayHitMask = 0;
		if (rayMeshBufferCount > 0) // has rays.
		{
			for (int i = 0; i < rayMeshBufferCount; i++)
			{
				CsMeshProbeInfo castProbeLS = pSrt->m_meshProbeInfoBuffer[rayMeshBufferOffset + i];
				const CsBoundingBox blockBox = pSrt->m_blockBoundsBuffer[dispatchThreadId];

				bool intersect = false;
				if (castProbeLS.m_radius > 0.0f)
					intersect = SphereCastBoundingBoxCheck(blockBox, castProbeLS.m_posLS, castProbeLS.m_dirLS, castProbeLS.m_maxT, castProbeLS.m_radius);
				else
					intersect = RayBoundingBoxCheck(blockBox, castProbeLS.m_posLS, castProbeLS.m_dirLS, castProbeLS.m_maxT);
				if (intersect)
				{
					if (i < 32)
						potentialRayHitMask.x |= (1 << i);
					else
						potentialRayHitMask.y |= (1 << (i % 32));
				}
			}
		}

		if ((potentialRayHitMask.x | potentialRayHitMask.y) != 0)
		{
			uint validIdx;
			pSrt->m_rwBlockArgsBuffer.InterlockedAdd(submeshIdx * 16, 1, validIdx);
			const uint subMeshBlockIdx = pSrt->m_subMeshBlockIdxBuffer[submeshIdx];

			CsDispatchBlock newBlock;
			newBlock.m_rayMeshBufferOffset = rayMeshBufferOffset;
			newBlock.m_localBlockIdx = localBlockIdx;
			newBlock.m_potentialRayHitMask = potentialRayHitMask;
			newBlock.m_validation = 0xdeadbeaf;
			newBlock.m_submeshIdx = submeshIdx;
			newBlock.m_pad = 0;

			if (subMeshBlockIdx + validIdx < kMaxSubmeshBlocks)
				pSrt->m_rwDispatchBlockInfo[subMeshBlockIdx + validIdx] = newBlock;
			else
				pSrt->m_rwValidation[0].m_vec0.y = 1; // validation failed!
		}
	}

	PROFILE_END(dispatchThreadId);
}

struct CsRayHitInfo
{
	float m_tt;
	uint m_hitIndexByRay;
	uint m_triangleIndex;
	uint m_submeshIndex;
};

struct CsRayHitInfo2
{
	float m_tt;
	uint m_globalRayIndex;
	uint m_triangleIndex;
	uint m_submeshIndex;
	float m_bu; // only valid for ray-cast
	float m_bv; // only valid for ray-cast
	float3 m_contactLs;
	float3 m_normalLs;
};

// we hit the limit of groupshared memory, so use a compact data structure
struct CsSphereHitInfo
{
	float m_tt;
	uint m_triangleIndex;
	uint m_submeshIndex;
};

struct CheckRayPolygonHitSrt
{
	RayCastInstanceConstants*			m_pInstanceConsts;
	uint4								m_globalVertBuffer;
	Buffer<uint>    					m_indexStream;
	Buffer<float3>						m_posOffsetStream;
	CompressedVSharp   					m_posStream;
	StructuredBuffer<CsDispatchBlock>	m_dispatchBlockInfo;
	StructuredBuffer<CsMeshProbeInfo>	m_meshProbeInfoBuffer;
	RWStructuredBuffer<ulong>			m_rwClosestHit;
	RWStructuredBuffer<CsValidation>	m_rwValidation;
	uint								m_numVertices;
	uint								m_numIndices;
	uint								m_numTriangles;
	uint								m_blockInfoOffset;
	uint								m_subMeshIndex;
};

ulong PackRayPolygonHit(float t, uint triIdx, uint subMeshIdx)
{
	uint lo = (triIdx << 16) | (subMeshIdx & 0xffff);
	uint hi = asuint(t);
	return ((ulong)hi << 32) | (ulong)lo;
}

void UnpackRayPolygonHit(ulong p, out float t, out uint triIdx, out uint subMeshIdx)
{
	uint lo = (uint)p;
	uint hi = (uint)(p >> 32);

	triIdx = lo >> 16;
	subMeshIdx = lo & 0xffff;
	t = asfloat(hi);
}

ulong PackRayPolygonHitPayload(float t, float p)
{
	uint lo = asuint(p);
	uint hi = asuint(t);
	return ((ulong)hi << 32) | (ulong)lo;
}

float UnpackRayPolygonHitPayload(ulong p)
{
	uint lo = (uint)p;
	return asfloat(lo);
}

void AddRayPolygonHit(CheckRayPolygonHitSrt* pSrt, uint rayIdx, float t, uint triIdx, float3 data)
{
	ulong p0 = PackRayPolygonHit(t, triIdx, pSrt->m_subMeshIndex);
	ulong p1 = PackRayPolygonHitPayload(t, data.x);
	ulong p2 = PackRayPolygonHitPayload(t, data.y);
	ulong p3 = PackRayPolygonHitPayload(t, data.z);

	uint index = 4 * (rayIdx * kMaxSubmeshes + pSrt->m_subMeshIndex);
	NdBufferAtomicMin(p0, index, pSrt->m_rwClosestHit);
	NdBufferAtomicMin(p1, index + 1, pSrt->m_rwClosestHit);
	NdBufferAtomicMin(p2, index + 2, pSrt->m_rwClosestHit);
	NdBufferAtomicMin(p3, index + 3, pSrt->m_rwClosestHit);
}

[numthreads(kNumTrianglesPerBlock, 1, 1)] // 64 triangles per block (thread-group), also 64 rays per group!
void Cs_CheckRayPolygonHit(uint groupIndex : SV_GroupIndex, int3 groupId : SV_GroupID, int groupThreadId : SV_GroupThreadId, CheckRayPolygonHitSrt* pSrt : S_SRT_DATA)
{
	PROFILE_START_TG(groupIndex, groupId.x, 64);

	const CsDispatchBlock blockInfo = pSrt->m_dispatchBlockInfo[pSrt->m_blockInfoOffset + groupId];

	if (blockInfo.m_validation == 0xdeadbeaf)
	{
		PROFILE_MARKER_TICK(PROFILE_MARKER_GREEN, groupIndex);

		const bool posInstanced = IsInstancedStream(pSrt->m_posStream, pSrt->m_numVertices);
		const bool posOffsetInstanced = IsInstancedStream(pSrt->m_posOffsetStream, pSrt->m_numVertices);

		const uint submeshBlockIndex = blockInfo.m_localBlockIdx;
		int triIdx = submeshBlockIndex * kNumTrianglesPerBlock + groupThreadId;

		if (triIdx < pSrt->m_numTriangles)
		{
			uint instanceId = 0;
			uint offset = 0;
			uint3 vidx;
			{
				uint iIndex = mul24(triIdx, 3);
				#if HARDWARE_INSTANCED
					instanceId = iIndex / pSrt->m_numIndices;
					iIndex = iIndex % pSrt->m_numIndices;
					offset = instanceId * pSrt->m_numVertices;
				#endif

				vidx.x = pSrt->m_indexStream[iIndex + 0];
				vidx.y = pSrt->m_indexStream[iIndex + 1];
				vidx.z = pSrt->m_indexStream[iIndex + 2];
			}

			uint3 posIdx = vidx + uint3(posInstanced * offset);
			uint3 posOffsetIdx = vidx + uint3(posOffsetInstanced * offset);

			float3x4 objToCluster = GetObjToCluster(pSrt->m_pInstanceConsts, instanceId);
			const float3 opos[3] =
			{
				LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_posStream, posIdx.x) + pSrt->m_posOffsetStream[posOffsetIdx.x],
				LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_posStream, posIdx.y) + pSrt->m_posOffsetStream[posOffsetIdx.y],
				LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_posStream, posIdx.z) + pSrt->m_posOffsetStream[posOffsetIdx.z],
			};

			// The position in cluster space
			const float3 posCs[3] =
			{
				pmul(objToCluster, opos[0]),
				pmul(objToCluster, opos[1]),
				pmul(objToCluster, opos[2]),
			};

			const float3 posWs[3] =
			{
				pmul(pSrt->m_pInstanceConsts->m_objToWorld, posCs[0]),
				pmul(pSrt->m_pInstanceConsts->m_objToWorld, posCs[1]),
				pmul(pSrt->m_pInstanceConsts->m_objToWorld, posCs[2]),
			};

			if (IsTriangleValid(posCs))
			{
				uint rayMeshBufferOffset = blockInfo.m_rayMeshBufferOffset;
				uint rayIndexOffset = 0;
				uint potentialRayHitMask = blockInfo.m_potentialRayHitMask.x;
				if (potentialRayHitMask == 0)
				{
					potentialRayHitMask = blockInfo.m_potentialRayHitMask.y;
					rayIndexOffset = 32;
				}

				const float3 edge1 = posCs[1] - posCs[0];
				const float3 edge2 = posCs[2] - posCs[0];

				while (potentialRayHitMask != 0)
				{
					PROFILE_MARKER_TICK(PROFILE_MARKER_WHITE, groupIndex);

					int rayIndex = FirstSetBit_Lo(potentialRayHitMask);
					potentialRayHitMask &= ~(1 << rayIndex);

					float bu = 0.0f, bv = 0.0f;
					float t = 0.0f;

					const uint meshProbeIndex = rayMeshBufferOffset + rayIndex + rayIndexOffset;
					if (meshProbeIndex < kMaxSubmeshes * kMaxRaysPerFrame)
					{
						const CsMeshProbeInfo meshProbeInfo = pSrt->m_meshProbeInfoBuffer[meshProbeIndex];
						if (meshProbeInfo.m_validation == 0xdeadbeaf)
						{
							if (TestTriangleFacing(edge1, edge2, meshProbeInfo.m_dirLS, meshProbeInfo.m_rayBehaviorMask, meshProbeInfo.m_rayCastTypeMask))
							{
								if (meshProbeInfo.m_radius > 0.f)
								{
									float3 contactWs = 0;
									float3 normalWs = 0;

									const float3 probePosWs = pmul(pSrt->m_pInstanceConsts->m_objToWorld, meshProbeInfo.m_posLS);
									const float3 probeDirWs = vmul(pSrt->m_pInstanceConsts->m_objToWorld, meshProbeInfo.m_dirLS);

									bool intersects = SphereTriIntersect(posWs, probePosWs, probeDirWs, meshProbeInfo.m_maxT, meshProbeInfo.m_radius, t, contactWs, normalWs);
									if (intersects)
									{
										AddRayPolygonHit(pSrt, meshProbeInfo.m_globalRayIndex, t, triIdx, contactWs);
									}
								}
								else
								{
									float3 normalLS = 0;
									bool intersects = RayTriangleIntersect(bu, bv, t, normalLS, posCs, meshProbeInfo.m_posLS, meshProbeInfo.m_dirLS, 1.0f / meshProbeInfo.m_maxT);
									if (intersects)
									{
										AddRayPolygonHit(pSrt, meshProbeInfo.m_globalRayIndex, t, triIdx, float3(bu, bv, 0.0f));
									}
								}
							}
						}
						else
						{
							pSrt->m_rwValidation[0].m_vec1.x = 1; // validation failed!
							if (pSrt->m_rwValidation[0].m_vec1.y == 0)
							{
								pSrt->m_rwValidation[0].m_vec1.y = rayMeshBufferOffset;
								pSrt->m_rwValidation[0].m_vec1.z = rayIndex;
								pSrt->m_rwValidation[0].m_vec1.w = rayIndexOffset;
							}
						}
					}
					else
					{
						pSrt->m_rwValidation[0].m_vec3.x = 1; // validation failed!
					}

					if (potentialRayHitMask == 0 && rayIndexOffset == 0)
					{
						potentialRayHitMask = blockInfo.m_potentialRayHitMask.y;
						rayIndexOffset = 32;
					}
				}
			}
		}
	}
	else
	{
		PROFILE_MARKER_TICK(PROFILE_MARKER_RED, groupIndex);
		pSrt->m_rwValidation[0].m_vec2.x = 1; // validation failed!

		if (pSrt->m_rwValidation[0].m_vec2.y == 0)
		{
			pSrt->m_rwValidation[0].m_vec2.y = pSrt->m_blockInfoOffset;
			pSrt->m_rwValidation[0].m_vec2.z = groupId;
		}
	}

	PROFILE_END_TG(groupIndex, groupId.x, 64);
}

//-------------------------------------------------------------------------------------------------------------------//
ulong PackMultiRayPolygonHit(ulong p, uint rayIdx, uint multiHitIdx, out uint subMeshIdx)
{
	float t = 0.0f;
	uint triIdx = 0;
	UnpackRayPolygonHit(p, t, triIdx, subMeshIdx);

	uint lo = (triIdx << 16) | ((rayIdx & 0xff) << 8) | (multiHitIdx & 0xff);
	uint hi = asuint(t);

	return ((ulong)hi << 32) | (ulong)lo;
}

void UnpackMultiRayPolygonHit(ulong p, out float t, out uint triIdx, out uint rayIdx, out uint multiHitIdx)
{
	uint lo = (uint)p;
	uint hi = (uint)(p >> 32);

	triIdx = lo >> 16;
	rayIdx = (lo >> 8) & 0xff;
	multiHitIdx = lo & 0xff;
	t = asfloat(hi);
}

ulong2 PackMultiRayPolygonHitPayload(float3 payload)
{
	uint lo = asuint(payload.x);
	uint hi = asuint(payload.y);
	return ulong2(((ulong)hi << 32) | (ulong)lo, (ulong)asuint(payload.z));
}

float3 UnpackMultiRayPolygonHitPayload(ulong p0, ulong p1)
{
	uint lo = (uint)p0;
	uint hi = (uint)(p0 >> 32);

	return float3(asfloat(lo), asfloat(hi), asfloat((uint)p1));
}

struct SortingHitPositionAlongRaySrt
{
	StructuredBuffer<ulong>					m_closestHit;
	StructuredBuffer<CsSubMeshInstanceInfo> m_subMeshInstanceInfo;
	RWStructuredBuffer<ulong>				m_rwHits;
	RWByteAddressBuffer						m_rwMeshRayCount2;

	uint									m_numRays;
	uint									m_numSubMeshes;

	ulong									m_rayRequestsMultipleHits;
};

#define kInvalidMeshId 0xffffffff
#define kInvalidPackedRayPolygonHit 0xffffffffffffffff

template<class T>
void Swap(out T a, out T b)
{
	T tmp = a;
	a = b;
	b = tmp;
}

[numthreads(64, 1, 1)]
void Cs_SortingHitPositionAlongRay(uint groupIndex : SV_GroupIndex, int dispatchThreadId : SV_DispatchThreadID, SortingHitPositionAlongRaySrt* pSrt : S_SRT_DATA)
{
	PROFILE_START(groupIndex);

	if (dispatchThreadId < pSrt->m_numRays)
	{
		// Locally find the closest hits per ray and ensure that each hit is from a different mesh
		uint meshId[kMaxNumHits] = { kInvalidMeshId, kInvalidMeshId };
		ulong closest[kMaxNumHits] = { kInvalidPackedRayPolygonHit, kInvalidPackedRayPolygonHit };
		float3 payload[kMaxNumHits] = { float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f) };

		for (uint m = 0; m < pSrt->m_numSubMeshes; m++)
		{
			uint hitIndex = 4 * (dispatchThreadId * kMaxSubmeshes + m);

			uint id = pSrt->m_subMeshInstanceInfo[m].m_levelAndInstanceId;
			ulong hit = pSrt->m_closestHit[hitIndex];
			float3 hitPayload = float3(UnpackRayPolygonHitPayload(pSrt->m_closestHit[hitIndex + 1]),
									   UnpackRayPolygonHitPayload(pSrt->m_closestHit[hitIndex + 2]),
									   UnpackRayPolygonHitPayload(pSrt->m_closestHit[hitIndex + 3]));

			for (uint i = 0; i < kMaxNumHits; i++)
			{
				if (hit < closest[i])
				{
					Swap(closest[i], hit);
					Swap(payload[i], hitPayload);
					Swap(meshId[i], id);
				}

				if (id == meshId[i])
				{
					break;
				}
			}
		}

		// Append the hits to the list of hits per submesh
		uint maxNumHitsToOutput = (pSrt->m_rayRequestsMultipleHits & (1L << dispatchThreadId)) == 0 ? 1 : kMaxNumHits;
		for (uint i = 0; i < maxNumHitsToOutput; i++)
		{
			if (closest[i] == kInvalidPackedRayPolygonHit)
			{
				break;
			}

			uint subMeshIdx = 0;
			ulong p0 = PackMultiRayPolygonHit(closest[i], dispatchThreadId, i, subMeshIdx);
			ulong2 p1 = PackMultiRayPolygonHitPayload(payload[i]);

			uint offset = 0;
			pSrt->m_rwMeshRayCount2.InterlockedAdd(subMeshIdx * 4, 1, offset);

			uint hitIndex = 3 * (subMeshIdx * kMaxRaysPerFrame * kMaxNumHits + offset);
			pSrt->m_rwHits[hitIndex] = p0;
			pSrt->m_rwHits[hitIndex + 1] = p1.x;
			pSrt->m_rwHits[hitIndex + 2] = p1.y;
		}
	}

	PROFILE_END(groupIndex);
}

struct FillIndirectDispatchArgsSrt
{
	StructuredBuffer<uint>		m_meshRayCount2;
	RWStructuredBuffer<uint4>	m_rwArgsBuffer;
	uint						m_numSubMeshes;
};

[numthreads(64, 1, 1)]
void Cs_FillIndirectDispatchArgs(int dispatchThreadId : SV_DispatchThreadID, FillIndirectDispatchArgsSrt* pSrt : S_SRT_DATA)
{
	PROFILE_START(dispatchThreadId);

	if (dispatchThreadId < pSrt->m_numSubMeshes)
	{
		uint groupCount = (pSrt->m_meshRayCount2[dispatchThreadId] + 63) / 64;
		pSrt->m_rwArgsBuffer[dispatchThreadId] = uint4(groupCount, 1, 1, 1);
	}

	PROFILE_END(dispatchThreadId);
}

struct GenerateRayHitPolygonInfoSrt
{
	RayCastInstanceConstants*				m_pInstanceConsts;
	uint4									m_globalVertBuffer;
	Buffer<uint>    						m_indexStream;
	Buffer<float3>							m_posOffsetStream;
	Buffer<float3>							m_normalStream;
	Buffer<float4>							m_tangentStream;
	CompressedVSharp   						m_posStream;
	CompressedVSharp   						m_uv0Stream;
	CompressedVSharp   						m_uv3Stream;
	CompressedVSharp   						m_clr0Stream;
	CompressedVSharp   						m_clr1Stream;
	StructuredBuffer<CsInputProbe>			m_probeListBuffer;
	StructuredBuffer<uint>					m_meshRayCount2;
	StructuredBuffer<ulong>					m_hits;
	RWStructuredBuffer<CsMeshRayMultiHits>	m_rwRayResult;
	RWStructuredBuffer<CsValidation>		m_rwValidation;
	uint									m_numVertices;
	uint									m_numIndices;
	uint									m_numTriangles;
	uint									m_subMeshId;
	uint									m_subMeshIndex;
	uint									m_subMeshAttrFlags;
};

float3 GetTriangleNormalSafe(float3 p0, float3 p1, float3 p2)
{
	float3 e0 = p1 - p0;
	float3 e1 = p2 - p0;
	float3 n = normalize(cross(e0, e1));

	// Make sure we always return a reasonable normal
	if (any(isnan(n) || isinf(n)))
	{
		return float3(0.0f, 1.0f, 0.0f);
	}

	return n;
}

[numthreads(64, 1, 1)]
void Cs_GenerateRayHitPolygonInfo(int dispatchThreadId : SV_DispatchThreadID, int groupId : SV_GroupID, int groupThreadId : SV_GroupThreadId, GenerateRayHitPolygonInfoSrt* pSrt : S_SRT_DATA)
{
	PROFILE_START(dispatchThreadId);

	if (dispatchThreadId >= pSrt->m_meshRayCount2[pSrt->m_subMeshIndex])
	{
		PROFILE_END(dispatchThreadId);
		return;
	}

	uint hitIndex = 3 * (pSrt->m_subMeshIndex * kMaxRaysPerFrame * kMaxNumHits + dispatchThreadId);

	float t = 0.0f;
	uint sortIdx = 0;
	uint triangleIdx = 0;
	uint globalRayIdx = 0;
	UnpackMultiRayPolygonHit(pSrt->m_hits[hitIndex], t, triangleIdx, globalRayIdx, sortIdx);

	float3 hitPayload = UnpackMultiRayPolygonHitPayload(pSrt->m_hits[hitIndex + 1], pSrt->m_hits[hitIndex + 2]);

	uint instanceId = 0;
	uint instanceOffset = 0;
	uint3 vidx;
	{
		uint iIndex = mul24(triangleIdx, 3);

	#if HARDWARE_INSTANCED
		instanceId = iIndex / pSrt->m_numIndices;
		instanceOffset = instanceId * pSrt->m_numVertices;

		iIndex = iIndex % pSrt->m_numIndices;
		triangleIdx = iIndex / 3;
	#endif

		vidx.x = pSrt->m_indexStream[iIndex + 0];
		vidx.y = pSrt->m_indexStream[iIndex + 1];
		vidx.z = pSrt->m_indexStream[iIndex + 2];
	}

	float3x4 objToCluster = GetObjToCluster(pSrt->m_pInstanceConsts, instanceId);

	const uint3 posIdx = vidx + uint3(IsInstancedStream(pSrt->m_posStream, pSrt->m_numVertices) * instanceOffset);
	const uint3 posOffsetIdx = vidx + uint3(IsInstancedStream(pSrt->m_posOffsetStream, pSrt->m_numVertices) * instanceOffset);

	const float3 opos[3] =
	{
		LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_posStream, posIdx.x) + pSrt->m_posOffsetStream[posOffsetIdx.x],
		LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_posStream, posIdx.y) + pSrt->m_posOffsetStream[posOffsetIdx.y],
		LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_posStream, posIdx.z) + pSrt->m_posOffsetStream[posOffsetIdx.z],
	};

	// The position in cluster space
	const float3 posCs[3] =
	{
		pmul(objToCluster, opos[0]),
		pmul(objToCluster, opos[1]),
		pmul(objToCluster, opos[2]),
	};

	const float3 posWs[3] =
	{
		pmul(pSrt->m_pInstanceConsts->m_objToWorld, posCs[0]),
		pmul(pSrt->m_pInstanceConsts->m_objToWorld, posCs[1]),
		pmul(pSrt->m_pInstanceConsts->m_objToWorld, posCs[2]),
	};

	float bu = 0.0f;
	float bv = 0.0f;

	// We have reserved space in the output hit accumulation buffer! Write out
	// our hit information, making sure we transform the hit to world space!

	// Also note that to keep register usage down, we should be writing out the
	// data we're calculating as soon as we possibly can, rather than keeping it
	// around for any longer than is necessary. We should also make an effort to
	// be writing out in 4 dword chunks when possible, to minimize the number of
	// separate writes to main memory that won't cache as well.
	CsMeshRayHit meshRayHit;

	const CsInputProbe castProbe = pSrt->m_probeListBuffer[globalRayIdx];
	if (castProbe.m_radius > 0.0f)
	{
		float3 contactWs = hitPayload;

		meshRayHit.m_positionWs = contactWs;
		meshRayHit.m_posOs = pmul(pSrt->m_pInstanceConsts->m_worldToObj, contactWs);
	}
	else
	{
		bu = hitPayload.x;
		bv = hitPayload.y;

		float bw = 1.0f - bu - bv;

		float3 positionLS = bw * posCs[0] + bu * posCs[1] + bv * posCs[2];
		float3 positionWS = pmul(pSrt->m_pInstanceConsts->m_objToWorld, positionLS);

		meshRayHit.m_posOs = positionLS;
		meshRayHit.m_positionWs = positionWS;
	}

	// Shift the barycentric coordinates a little bit so that the hit point is
	// always inside the triangle. Necessary for surface type queries because of
	// GPU rasterizing rules.
	// Disable this for now. We don't use GPU rasterization anymore so this shouldn't be needed.
	/*const float kTolerance = 0.01f;
	bu = clamp(bu, kTolerance, 1.0f - kTolerance);
	bv = clamp(bv, kTolerance, 1.0f - kTolerance);

	if (bu + bv >= 1.0f - kTolerance)
	{
		if (bu >= bv)
		{
			bu -= kTolerance;
		}
		else
		{
			bv -= kTolerance;
		}
	}
	*/

	float bw = 1.0f - bu - bv;

	// Use the triangle's calculated face normal, rather than interpolating the
	// per-vertex normals. It's highly unlikely that we'll actually need to be
	// using the "real" interpolated normal here. In order to properly account
	// for non-uniform scale, we must transform the object-space normal vector
	// by the inverse transpose of the object-to-world-space matrix!
	float3 normalOs = GetTriangleNormalSafe(posCs[0], posCs[1], posCs[2]);
	meshRayHit.m_normalOs = normalOs;
	meshRayHit.m_normalWs = normalize(vmul(pSrt->m_pInstanceConsts->m_objToWorld, normalOs));

	meshRayHit.m_t = t;
	meshRayHit.m_tri = triangleIdx;
	meshRayHit.m_instance = instanceId;
	meshRayHit.m_rayLevelIndex = PackUInt2ToUInt(globalRayIdx, pSrt->m_pInstanceConsts->m_levelId);
	meshRayHit.m_instanceMeshIndex = PackUInt2ToUInt(pSrt->m_pInstanceConsts->m_instanceId, pSrt->m_subMeshId);

	// Now sample the first vertex color values, for each vert of this triangle,
	// and then interpolate it the same way we interpolate everything else. Note
	// that currently, the tools allow values for the color channels to be passed
	// through that are outside of the [0, 1] range, so in order to match what the
	// material shaders do we must clamp the color values we sample from each vert!
	meshRayHit.m_color0 = float3(0.0f, 0.0f, 0.0f);

	meshRayHit.m_vertexColor0[0] = float3(0.0f, 0.0f, 0.0f);
	meshRayHit.m_vertexColor0[1] = float3(0.0f, 0.0f, 0.0f);
	meshRayHit.m_vertexColor0[2] = float3(0.0f, 0.0f, 0.0f);

	if (pSrt->m_subMeshAttrFlags & 1)
	{
		meshRayHit.m_vertexColor0[0] = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_clr0Stream, vidx.x, instanceId, pSrt->m_numVertices);
		meshRayHit.m_vertexColor0[1] = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_clr0Stream, vidx.y, instanceId, pSrt->m_numVertices);
		meshRayHit.m_vertexColor0[2] = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_clr0Stream, vidx.z, instanceId, pSrt->m_numVertices);

		meshRayHit.m_color0 = bw * meshRayHit.m_vertexColor0[0] + bu * meshRayHit.m_vertexColor0[1] + bv * meshRayHit.m_vertexColor0[2];
	}

	// And now the second vertex color stream...
	meshRayHit.m_color1 = float3(0.0f, 0.0f, 0.0f);

	meshRayHit.m_vertexColor1[0] = float3(0.0f, 0.0f, 0.0f);
	meshRayHit.m_vertexColor1[1] = float3(0.0f, 0.0f, 0.0f);
	meshRayHit.m_vertexColor1[2] = float3(0.0f, 0.0f, 0.0f);

	if (pSrt->m_subMeshAttrFlags & 2)
	{
		meshRayHit.m_vertexColor1[0] = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_clr1Stream, vidx.x, instanceId, pSrt->m_numVertices);
		meshRayHit.m_vertexColor1[1] = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_clr1Stream, vidx.y, instanceId, pSrt->m_numVertices);
		meshRayHit.m_vertexColor1[2] = LoadVertexAttribute<float3, 72>(pSrt->m_globalVertBuffer, pSrt->m_clr1Stream, vidx.z, instanceId, pSrt->m_numVertices);

		meshRayHit.m_color1 = bw * meshRayHit.m_vertexColor1[0] + bu * meshRayHit.m_vertexColor1[1] + bv * meshRayHit.m_vertexColor1[2];
	}

	// Finally, the barycentric st coordinates and the interpolated uv coordinates!
	meshRayHit.m_uv0 = float2(0.0f, 0.0f);
	meshRayHit.m_uv3 = float2(-100.0f, -100.0f);

	meshRayHit.m_vertexUv0[0] = float2(0.0f, 0.0f);
	meshRayHit.m_vertexUv0[1] = float2(0.0f, 0.0f);
	meshRayHit.m_vertexUv0[2] = float2(0.0f, 0.0f);

	meshRayHit.m_vertexUv3[0] = float2(-100.0f, -100.0f);
	meshRayHit.m_vertexUv3[1] = float2(-100.0f, -100.0f);
	meshRayHit.m_vertexUv3[2] = float2(-100.0f, -100.0f);

	if (pSrt->m_subMeshAttrFlags & 4)
	{
		meshRayHit.m_vertexUv0[0] = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_uv0Stream, vidx.x, instanceId, pSrt->m_numVertices);
		meshRayHit.m_vertexUv0[1] = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_uv0Stream, vidx.y, instanceId, pSrt->m_numVertices);
		meshRayHit.m_vertexUv0[2] = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_uv0Stream, vidx.z, instanceId, pSrt->m_numVertices);

		meshRayHit.m_uv0 = bw * meshRayHit.m_vertexUv0[0] + bu * meshRayHit.m_vertexUv0[1] + bv * meshRayHit.m_vertexUv0[2];
	}

	if (pSrt->m_subMeshAttrFlags & 8)
	{
		meshRayHit.m_vertexUv3[0] = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_uv3Stream, vidx.x, instanceId, pSrt->m_numVertices);
		meshRayHit.m_vertexUv3[1] = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_uv3Stream, vidx.y, instanceId, pSrt->m_numVertices);
		meshRayHit.m_vertexUv3[2] = LoadVertexAttribute<float2, 32>(pSrt->m_globalVertBuffer, pSrt->m_uv3Stream, vidx.z, instanceId, pSrt->m_numVertices);

		meshRayHit.m_uv3 = bw * meshRayHit.m_vertexUv3[0] + bu * meshRayHit.m_vertexUv3[1] + bv * meshRayHit.m_vertexUv3[2];
	}

	// Write out position, normals, and tangent attributes
	meshRayHit.m_vertexPos[0] = opos[0];
	meshRayHit.m_vertexPos[1] = opos[1];
	meshRayHit.m_vertexPos[2] = opos[2];

	meshRayHit.m_vertexNormal[0] = float3(0.0f, 0.0f, 0.0f);
	meshRayHit.m_vertexNormal[1] = float3(0.0f, 0.0f, 0.0f);
	meshRayHit.m_vertexNormal[2] = float3(0.0f, 0.0f, 0.0f);

	meshRayHit.m_vertexTangent[0] = float4(0.0f, 0.0f, 0.0f, 0.0f);
	meshRayHit.m_vertexTangent[1] = float4(0.0f, 0.0f, 0.0f, 0.0f);
	meshRayHit.m_vertexTangent[2] = float4(0.0f, 0.0f, 0.0f, 0.0f);

	if (pSrt->m_subMeshAttrFlags & 48)
	{
		uint3 normalIdx = vidx;
		if (!IsCompressedTangentFrame(pSrt->m_normalStream))
		{
			normalIdx += uint3(IsInstancedStream(pSrt->m_normalStream, pSrt->m_numVertices) * instanceOffset);
		}

		LoadCompressedTangentFrameNT(pSrt->m_normalStream, pSrt->m_tangentStream, meshRayHit.m_vertexNormal[0], meshRayHit.m_vertexTangent[0], normalIdx.x);
		LoadCompressedTangentFrameNT(pSrt->m_normalStream, pSrt->m_tangentStream, meshRayHit.m_vertexNormal[1], meshRayHit.m_vertexTangent[1], normalIdx.y);
		LoadCompressedTangentFrameNT(pSrt->m_normalStream, pSrt->m_tangentStream, meshRayHit.m_vertexNormal[2], meshRayHit.m_vertexTangent[2], normalIdx.z);
	}

	meshRayHit.m_st = float2(bu, bv);

	meshRayHit.m_debugRayIdxAndHitIdx = (globalRayIdx | (sortIdx << 8));

	meshRayHit.m_vertexWs0 = posWs[0];
	meshRayHit.m_vertexWs1 = posWs[1];
	meshRayHit.m_vertexWs2 = posWs[2];

	pSrt->m_rwRayResult[globalRayIdx].m_hits[sortIdx] = meshRayHit;

	PROFILE_END(dispatchThreadId);
}
