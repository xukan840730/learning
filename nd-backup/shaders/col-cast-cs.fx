/*
* Copyright (c) 2013 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#include "global-funcs.fxi"
#include "atomics.fxi"
#include "sphere-cast.fx"

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

int maskToIndex(float3 v)
{
	int res = 0;
	const int maskToIndexLut[] = { -1,  0, 1, 2-8, 2, 1-8, 0-8, -1 };

	if (v.x > 0.0f)
		res |= 0x00000001;
	if (v.y > 0.0f)
		res |= 0x00000002;
	if (v.z > 0.0f)
		res |= 0x00000004;


	return maskToIndexLut[res];
}

// ====================================================================================================
//   Macros
// ----------------------------------------------------------------------------------------------------
#define DEBUG_OUTPUT_TRIANGLE_VERTICES	0
static float kRayMinDist = 1e-4f;

bool TriIntersect(in float3 opos[3], in float3 rayStart, in float3 rayDirNorm, in float rayMaxT, out float time, out float3 normal)
{
	bool intersects = true;
	float bu, bv;
	float t;
	float3 nrmOs;
	time = 0.0f;
	normal = float3(0,1,0);
	

	// Intersect the ray with the triangle! Calculate the intersection
	// time t, and the u and v barycentric coordinates.
	{
		float3 edge1 = opos[1] - opos[0];
			float3 edge2 = opos[2] - opos[0];
			float3 dir = rayDirNorm;
			float3 pvec = cross(dir, edge2);
			float  det = dot(edge1, pvec);
		float  inv_det = 1.0f / det;
		float3 orig = rayStart;
			float3 svec = orig - opos[0];

			bu = dot(svec, pvec) * inv_det;
		if (bu < 0.0f || bu >= 1.0f)
			intersects = false;

		float3 qvec = cross(svec, edge1);

			bv = dot(dir, qvec) * inv_det;
		if (bv < 0.0f || bu + bv >= 1.0f)
			intersects = false;

		// Calculate the intersection distance, in world units
		float dist = dot(edge2, qvec) * inv_det;

		// Normalize the intersection distance by dividing by maxT
		float inv_maxT = 1.0 / rayMaxT;
		t = dist * inv_maxT;

		// Use a small minimum to avoid precision issues when the ray hits a triangle
		// very close to its origin.
		if (dist < kRayMinDist || t >= 1.0f)
			intersects = false;

		// Test which direction this triangle's normal is facing and if that matches
		// up with the flags for this ray. Since we only care about the dot product's
		// sign, we do not need to normalize yet.

		{
			nrmOs = cross(edge1, edge2);
#if 0	
			float ndotd = dot(nrmOs, dir);

			uint flags = ray.m_typeMaskFlags;

			// Not passing through back faces OR face must be front face (N.D must be negative)!
			intersects = intersects && (!(flags & kIgnoreBackFacingTriangles) || ndotd  < 0.0f);
			// Not passing through front faces OR face must be back face (N.D must be positive)!
			intersects = intersects && (!(flags & kIgnoreFrontFacingTriangles) || ndotd >= 0.0f);
#endif		
		}

	}

	// We now write *EVERY* hit to our output ray hit accumulation buffer. This way,
	// there is no need to employ complicated locking mechanisms or the tricky atomic
	// minimum we were using before; instead, we write out all the hits and then go
	// through them in a second compute shader to figure out the closest hits for each
	// ray, which are then written out all at once!
	if (intersects)
	{
		float bw = 1.0f - bu - bv;

		// Also note that to keep register usage down, we should be writing out the
		// data we're calculating as soon as we possibly can, rather than keeping it
		// around for any longer than is necessary. We should also make an effort to
		// be writing out in 4 dword chunks when possible, to minimize the number of
		// separate writes to main memory that won't cache as well.

		// Calculate the object-space interpolated position, then transform it back
		// into world-space
		//float3 posOs = bw * opos[0] + bu * opos[1] + bv * opos[2];
		//float3 posWs = pmul(srt.pInstanceConsts->m_objToWorld, posOs);

		// Gotta normalize the normal now, since we did not normalize before!
		// TODO: We only need to normalize this if it's the best hit for this ray,
		// which can be done in the gather kernel!
		nrmOs = normalize(nrmOs);

		normal = nrmOs;
		time = t;

#if DEBUG_OUTPUT_TRIANGLE_VERTICES
		// For debugging purposes, we can also output the world-space positions of each
		// vertex of the hit triangle, but normally 
		srt.pInstanceBuffers->rw_rayHit[hitIndex].m_debug0 = float4(pmul(srt.pInstanceConsts->m_objToWorld, opos[0]), 0.0f);
		srt.pInstanceBuffers->rw_rayHit[hitIndex].m_debug1 = float4(pmul(srt.pInstanceConsts->m_objToWorld, opos[1]), 0.0f);
		srt.pInstanceBuffers->rw_rayHit[hitIndex].m_debug2 = float4(pmul(srt.pInstanceConsts->m_objToWorld, opos[2]), 0.0f);
#endif

		return true;
	}
	return false;
}

// ====================================================================================================
//   Constants
// ----------------------------------------------------------------------------------------------------


// These are shifted up an extra 16 bits so we can just use the typeFlags as-is
static uint kIgnoreBackFacingTriangles  = (1 << 0) << 16;
static uint kIgnoreFrontFacingTriangles = (1 << 1) << 16;


// ====================================================================================================
//   Constant Buffers
// ----------------------------------------------------------------------------------------------------
struct CollCastInstanceConstants //  : register(b0)
{
	float3x4 m_objToWorld;		// Transform from object-space to world-space
	float3x4 m_worldToObj;		// Transform from world-space to object-space
	float3x4 m_objToWorldIT;	// Inverse transpose of the objToWorld matrix, for transforming vectors
	uint     m_numRays;			// Number of input rays
	int      m_levelId;			// Which level we're casting against, or -1 if foreground
	uint     m_instanceId;		// The mesh index or instance index of the object cast against
	uint     m_gdsOffset;       // The global data-store address of the atomic counter
	uint     m_numHits; // written by first pass
};

struct CollCastSubMeshConstants // : register(b1)
{
	uint     m_levelIdsubMeshId;
	uint     m_collisionFilter; // this is filter info for the broad phase hk mesh
	uint     m_numTriangles;
	uint     m_flags;           
};

// ====================================================================================================
//   Structures
// ----------------------------------------------------------------------------------------------------
struct CollRayHit
{
	uint   m_triRay;			// Ray index [15:0] and tri index [31:16]
	uint m_levelIdsubmeshId;
	uint m_patLow;
	uint m_patHigh;
	uint m_patExcludeLow; // debug
	uint m_patExcludeHigh; // debug
	uint m_patIndex; // debug
	uint m_pad; // 
	float  m_t;					// Ray intersection t value
	float3 m_positionWs;		// World-space position
	float3 m_normalWs;			// World-space unit-length normal

#if DEBUG_OUTPUT_TRIANGLE_VERTICES

	uint   m_instanceMesh;		// Mesh/Foreground instance index [15:0], sub-mesh index [31:16]
	float3 m_color0;			// Color0 value
	float3 m_color1;			// Color1 value
	float2 m_st;				// Barycentric st coordinates
	float2 m_uv;				// Texture uv coordinates

	float4 m_debug0;			// Optional debug data
	float4 m_debug1;			// Optional debug data
	float4 m_debug2;			// Optional debug data
#endif
};

#define RAY_INPUT_FLAG_SINGLE_SIDED 0x00000001
struct Ray
{
	float3 m_orig;				// Start position (time = 0.0)
	uint   m_layerIncludeLow;
	uint   m_layerIncludeHigh;
	uint   m_ignoreSystem;
	uint m_patExcludeLow;
	uint m_patExcludeHigh;
	uint  m_patExcludeSurfaceLow;
	uint  m_patExcludeSurfaceHigh;
	uint m_flags; // can store things like single sided vs double sided collision, etc
	float3 m_dir;				// Normalized direction
	float  m_maxT;				// Maximum intersection distance (inverted before saving to LDS!)
	float m_r;
};



struct Bounds
{
	float3 m_min;
	float  m_pad0;
	float3 m_max;
	float  m_pad1;
};

// ====================================================================================================
//   Tweakables!
// ----------------------------------------------------------------------------------------------------
#define kMaxRaysTotal			256		
#define kMaxHits			(kMaxRaysTotal * 32)		// Size of the hit accumulation array buffer

// Hit Sort Info 32bit layout:
//	31   24 23         8 7     0
//	[ Ray ] [ Distance ] [ Hit ]
#define kSortInfoDistBits	16		// Number of bits for the hit distance in the hit sort info
#define kSortInfoDistShift	8		// How far to shift the distance value into the hit sort info
#define kSortInfoDistMask	((1 << kSortInfoDistBits) - 1)
#define kSortInfoRayBits	8		// Number of bits reserved for the ray index in the hit sort info
#define kSortInfoRayShift	24		// Shift to apply to move the ray index into place
#define kSortInfoRayMask	((1 << kSortInfoRayBits) - 1)
#define kSortInfoHitBits	8		// Number of bits reserved for the hit index in the hit sort info
#define kSortInfoHitShift	0		// Shift to apply to move the hit index into place
#define kSortInfoHitMask	((1 << kSortInfoHitBits) - 1)

#define kSortInfoInvalid	0xFFFFFFFF

//#define kCounterIndex 1

// ====================================================================================================
//   SubMesh Buffers
// ----------------------------------------------------------------------------------------------------

struct RayCastSubMeshBuffers
{
	Buffer<uint>    			t_patLayerStream; //	: register(t1);
	Buffer<float3>   			t_posStream; //			: register(t2);
	Buffer<uint>    			t_patPalette;
};

// ====================================================================================================
//   Instance Buffers
// ----------------------------------------------------------------------------------------------------

struct RayCastInstanceBuffers
{
	RWStructuredBuffer<CollRayHit>	rw_rayHit; //			: register(u0);
	RWStructuredBuffer<CollRayHit>	rw_minRayHits; //	: register(u1);
	StructuredBuffer<Ray>			t_inputRays; //			: register(t0);
	RWStructuredBuffer<uint>		rw_bboxHits;
	
};

struct CollRayCastSrt
{
	CollCastInstanceConstants *pInstanceConsts;
	RayCastInstanceBuffers *pInstanceBuffers;
	CollCastSubMeshConstants *pSubMeshConsts;
	RayCastSubMeshBuffers *pSubMeshBuffers;
};

struct CollRayCastReduceSrt
{
	CollCastInstanceConstants *pInstanceConsts;
	RayCastInstanceBuffers *pInstanceBuffers;
};

// ====================================================================================================
//   Local Data Store (LDS) Buffers
// ----------------------------------------------------------------------------------------------------
#if 0
groupshared float3			l_triVerts[kMaxTriangles*3];	// Each thread loads a single triangle
groupshared uint3			l_vertIdx[kMaxTriangles];		// Keep track of the vertex indices
groupshared Ray				l_inputRays[kMaxRays];			// In object space
groupshared bool			l_rayAabbTest[kMaxRays];		// True if the ray intersected the AABB
groupshared bool			l_anyHits;						// Whether or not any rays hit the AABBs

groupshared uint			l_hits[kMaxHits];				// Local memory for sorted hit info array


#endif

groupshared uint			l_laneHitTimes[64];

// ====================================================================================================
//   Support functions
// ----------------------------------------------------------------------------------------------------

//uint PackBgLevelSubMeshRay(

void SphereTriIntersectMain(in uint iTriangle, in float3 opos[3], in CollRayCastSrt srt, uint iRay, out float time, out float3 normal)
{
	float QQ, RR, QR;
	SetupClosestPointTriangleCache( opos, QQ, RR, QR );

	Ray ray = srt.pInstanceBuffers->t_inputRays[iRay];

	const float cachedPathLength = ray.m_maxT;
	const float3 inPath = ray.m_dir * ray.m_maxT;
	const int iterativeLinearCastMaxIterations = 20;
	const float r = ray.m_r;

	float3 contact;

	bool hit = SphereTriLinearCastPostCache(ray.m_orig, r, inPath, opos, QQ, RR, QR,
	cachedPathLength,
	iterativeLinearCastMaxIterations,
	//, void* startCollector
	contact, normal, time);

	if (hit)
	{
		uint hitIndex = NdAtomicIncrement(srt.pInstanceConsts->m_gdsOffset);
		if (hitIndex < kMaxHits)
		{
			srt.pInstanceBuffers->rw_rayHit[hitIndex].m_triRay     = PackUInt2ToUInt(iRay, iTriangle);
			srt.pInstanceBuffers->rw_rayHit[hitIndex].m_levelIdsubmeshId = srt.pSubMeshConsts->m_levelIdsubMeshId;
			srt.pInstanceBuffers->rw_rayHit[hitIndex].m_t = time;
			srt.pInstanceBuffers->rw_rayHit[hitIndex].m_positionWs = opos[0];
			srt.pInstanceBuffers->rw_rayHit[hitIndex].m_normalWs = normal;
			
			//debug
			#if DEBUG_OUTPUT_TRIANGLE_VERTICES
			srt.pInstanceBuffers->rw_rayHit[hitIndex].m_positionWs = opos[0];
			srt.pInstanceBuffers->rw_rayHit[hitIndex].m_normalWs = opos[1];
			srt.pInstanceBuffers->rw_rayHit[hitIndex].m_uv.y = opos[2].x;
			srt.pInstanceBuffers->rw_rayHit[hitIndex].m_st = opos[2].yz;

			srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color0 = srt.pInstanceBuffers->t_inputRays[iRay].m_orig;
			srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color1 = srt.pInstanceBuffers->t_inputRays[iRay].m_dir;
			srt.pInstanceBuffers->rw_rayHit[hitIndex].m_uv.x = srt.pInstanceBuffers->t_inputRays[iRay].m_maxT;
			#endif
		}
		else
		{
			NdAtomicDecrement(srt.pInstanceConsts->m_gdsOffset);
		}
	}
}

void SphereTriIntersectMain_16Rays(in uint iTriangle, in float3 opos[3], in CollRayCastSrt srt, uint iRayStart, out float time, out float3 normal)
{
	float QQ, RR, QR;
	SetupClosestPointTriangleCache( opos, QQ, RR, QR );

	uint lastRay = min(iRayStart + 16, srt.pInstanceConsts->m_numRays);

	for (uint iRay = iRayStart; iRay < lastRay; ++iRay)
	{
		Ray ray = srt.pInstanceBuffers->t_inputRays[iRay];
	
		const float cachedPathLength = ray.m_maxT;
		const float3 inPath = ray.m_dir * ray.m_maxT;
		const int iterativeLinearCastMaxIterations = 20;
		const float r = 0.1;

		float3 contact;
	
		bool hit = SphereTriLinearCastPostCache(ray.m_orig, r, inPath, opos, QQ, RR, QR,
		cachedPathLength,
		iterativeLinearCastMaxIterations,
		//, void* startCollector
		contact, normal, time);
	
		if (hit)
		{
			uint hitIndex = NdAtomicIncrement(srt.pInstanceConsts->m_gdsOffset);
			if (hitIndex < kMaxHits)
			{
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_triRay     = PackUInt2ToUInt(iRay, iTriangle);
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_levelIdsubmeshId = srt.pSubMeshConsts->m_levelIdsubMeshId;
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_t = time;
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_positionWs = opos[0];
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_normalWs = normal;
				
				#if DEBUG_OUTPUT_TRIANGLE_VERTICES
				//debug
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_positionWs = opos[0];
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_normalWs = opos[1];
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_uv.y = opos[2].x;
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_st = opos[2].yz;
	
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color0 = srt.pInstanceBuffers->t_inputRays[iRay].m_orig;
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color1 = srt.pInstanceBuffers->t_inputRays[iRay].m_dir;
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_uv.x = srt.pInstanceBuffers->t_inputRays[iRay].m_maxT;
				#endif
			}
			else
			{
				NdAtomicDecrement(srt.pInstanceConsts->m_gdsOffset);
			}
		}
	}
}

bool BBoxIntersect(float3 a0, float3 a1, float3 b0, float3 b1)
{
	if (a1.x < b0.x) return false;
	if (a1.y < b0.y) return false;
	if (a1.z < b0.z) return false;
	if (a0.x > b1.x) return false;
	if (a0.y > b1.y) return false;
	if (a0.z > b1.z) return false;
	return true;
}

void SphereTriIntersectMain_16Rays_BBox(in uint iTriangle, in float3 opos[3], in CollRayCastSrt srt, uint iRayStart)
{
	float QQ, RR, QR;
	SetupClosestPointTriangleCache( opos, QQ, RR, QR );

	float3 minP = min3(opos[0], opos[1], opos[2]);
	float3 maxP = max3(opos[0], opos[1], opos[2]);

	uint lastRay = min(iRayStart + 16, srt.pInstanceConsts->m_numRays);

	for (uint iRay = iRayStart; iRay < lastRay; ++iRay)
	{
		Ray ray = srt.pInstanceBuffers->t_inputRays[iRay];
	
		const float cachedPathLength = ray.m_maxT;
		const float3 inPath = ray.m_dir * ray.m_maxT;
		const int iterativeLinearCastMaxIterations = 20;
		const float r = ray.m_r;

		float3 rMinP = min(ray.m_orig, ray.m_orig + inPath) - float3(r, r, r);
		float3 rMaxP = max(ray.m_orig, ray.m_orig + inPath) + float3(r, r, r);

		if (BBoxIntersect(minP, maxP, rMinP, rMaxP))
		{
			float3 contact;
			float3 normal;
			float time;
		
			bool hit = SphereTriLinearCastPostCache(ray.m_orig, r, inPath, opos, QQ, RR, QR,
			cachedPathLength,
			iterativeLinearCastMaxIterations,
			//, void* startCollector
			contact, normal, time);
	
			if (hit)
			{
				uint hitIndex = NdAtomicIncrement(srt.pInstanceConsts->m_gdsOffset);
				if (hitIndex < kMaxHits)
				{
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_triRay     = PackUInt2ToUInt(iRay, iTriangle);
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_levelIdsubmeshId = srt.pSubMeshConsts->m_levelIdsubMeshId;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_t = time;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_positionWs = opos[0];
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_normalWs = normal;
					
					#if DEBUG_OUTPUT_TRIANGLE_VERTICES
					//debug
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_positionWs = opos[0];
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_normalWs = opos[1];
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_uv.y = opos[2].x;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_st = opos[2].yz;
	
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color0 = srt.pInstanceBuffers->t_inputRays[iRay].m_orig;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color1 = srt.pInstanceBuffers->t_inputRays[iRay].m_dir;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_uv.x = srt.pInstanceBuffers->t_inputRays[iRay].m_maxT;
					#endif
				}
				else
				{
					NdAtomicDecrement(srt.pInstanceConsts->m_gdsOffset);
				}
			}
		}
	}
}


void RayTriIntersectMain_16Rays_BBox(in uint iTriangle, in float3 opos[3], in CollRayCastSrt srt, uint iRayStart)
{
	float3 minP = min3(opos[0], opos[1], opos[2]);
	float3 maxP = max3(opos[0], opos[1], opos[2]);

	uint lastRay = min(iRayStart + 16, srt.pInstanceConsts->m_numRays);

	for (uint iRay = iRayStart; iRay < lastRay; ++iRay)
	{
		Ray ray = srt.pInstanceBuffers->t_inputRays[iRay];

		const float cachedPathLength = ray.m_maxT;
		const float3 inPath = ray.m_dir * ray.m_maxT;
		const int iterativeLinearCastMaxIterations = 20;
		//const float r = ray.m_r;

		float3 rMinP = min(ray.m_orig, ray.m_orig + inPath);// -float3(r, r, r);
		float3 rMaxP = max(ray.m_orig, ray.m_orig + inPath);// +float3(r, r, r);

		if (BBoxIntersect(minP, maxP, rMinP, rMaxP))
		{
			float3 normal;
			float time = 0.0f;

			bool hit = TriIntersect(opos, ray.m_orig, ray.m_dir, ray.m_maxT, time, normal);

			uint layerIncludeLow = srt.pInstanceBuffers->t_inputRays[iRay].m_layerIncludeLow;
			uint layerIncludeHigh = srt.pInstanceBuffers->t_inputRays[iRay].m_layerIncludeHigh;

			// check broad phase first. get collision info of the mesh
			uint collisionInfo = srt.pSubMeshConsts->m_collisionFilter;
			uint meshLayer = collisionInfo & 0x0000003F; // 6 bits
			if (meshLayer > 31)
			{
				if ((layerIncludeHigh & (1 << (meshLayer - 32))) == 0)
					return;
			}
			else
			{
				if ((layerIncludeLow & (1 << meshLayer)) == 0)
					return;
			}

			uint system = collisionInfo >> 20;

			if (srt.pInstanceBuffers->t_inputRays[iRay].m_ignoreSystem && srt.pInstanceBuffers->t_inputRays[iRay].m_ignoreSystem == system)
				return;

			// done with broad phase of checking ray filter and mesh collision info

			// check layer and pat bits before doing any computation. Note, might want to do it after
			uint patLayer = srt.pSubMeshBuffers->t_patLayerStream[iTriangle];
			
			uint triLayer = patLayer & 0x000000FF;
			uint triPatIndex = patLayer >> 8;

			if (triLayer > 31)
			{
				if ((layerIncludeHigh & (1 << (triLayer - 32))) == 0)
					return;
			}
			else
			{
				if ((layerIncludeLow & (1 << triLayer)) == 0)
					return;
			}
			
			
			uint triPatLow = srt.pSubMeshBuffers->t_patPalette[triPatIndex * 2];
			uint triPatHigh = srt.pSubMeshBuffers->t_patPalette[triPatIndex * 2 + 1];

			uint rayPatExcludeLow = srt.pInstanceBuffers->t_inputRays[iRay].m_patExcludeLow;
			uint rayPatExcludeHigh = srt.pInstanceBuffers->t_inputRays[iRay].m_patExcludeHigh;

			if ((triPatLow & rayPatExcludeLow) != 0)
			{
				return;
			}

			if ((triPatHigh & rayPatExcludeHigh) != 0)
			{
				return;
			}

			uint rayExcludeSurfacesLow = srt.pInstanceBuffers->t_inputRays[iRay].m_patExcludeSurfaceLow;
			uint rayExcludeSurfacesHigh = srt.pInstanceBuffers->t_inputRays[iRay].m_patExcludeSurfaceHigh;

			uint surfaceFromPat = triPatLow & 63;

			if (surfaceFromPat > 31)
			{
				if ((rayExcludeSurfacesHigh & (1 << (surfaceFromPat - 32))) != 0)
					return;
			}
			else
			{
				if ((rayExcludeSurfacesLow & (1 << surfaceFromPat)) != 0)
					return;
			}

			

			//bool hit = SphereTriLinearCastPostCache(ray.m_orig, r, inPath, opos, QQ, RR, QR,
			//	cachedPathLength,
			//	iterativeLinearCastMaxIterations,
			//	//, void* startCollector
			//	normal,
			//	time);

			if (hit)
			{
				if (srt.pInstanceBuffers->t_inputRays[iRay].m_flags & RAY_INPUT_FLAG_SINGLE_SIDED)
				{
					// check for single sided..
					float3 triNormal = cross((opos[1] - opos[0]), (opos[2] - opos[1]));

					if (dot(triNormal, normal) < 0)
						return;
				}

				uint hitIndex = NdAtomicIncrement(srt.pInstanceConsts->m_gdsOffset);
				if (hitIndex < kMaxHits)
				{
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_triRay = PackUInt2ToUInt(iRay, iTriangle);
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_levelIdsubmeshId = srt.pSubMeshConsts->m_levelIdsubMeshId;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_patLow = triPatLow;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_patHigh = triPatHigh;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_patExcludeLow = rayPatExcludeLow;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_patExcludeHigh = rayPatExcludeHigh;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_patIndex = triPatIndex;
					//srt.pInstanceBuffers->rw_rayHit[hitIndex].m_patExcludeHigh = rayPatExcludeHigh;

					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_t = time;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_positionWs = opos[0];
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_normalWs = normal;

#if DEBUG_OUTPUT_TRIANGLE_VERTICES
					//debug
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_positionWs = opos[0];
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_normalWs = opos[1];
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_uv.y = opos[2].x;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_st = opos[2].yz;

					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color0 = srt.pInstanceBuffers->t_inputRays[iRay].m_orig;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color1 = srt.pInstanceBuffers->t_inputRays[iRay].m_dir;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_uv.x = srt.pInstanceBuffers->t_inputRays[iRay].m_maxT;
#endif
				}
				else
				{
					NdAtomicDecrement(srt.pInstanceConsts->m_gdsOffset);
				}
			}
		}
	}
}


void SphereTriIntersectMain_16Rays_BBox_Check(in uint iTriangle, in float3 opos[3], in CollRayCastSrt srt, uint iRayStart, out float time, out float3 normal)
{
	


	float QQ, RR, QR;
	SetupClosestPointTriangleCache( opos, QQ, RR, QR );

	uint lastRay = min(iRayStart + 16, srt.pInstanceConsts->m_numRays);

	for (uint iRay = iRayStart; iRay < lastRay; ++iRay)
	{
		Ray ray = srt.pInstanceBuffers->t_inputRays[iRay];
	
		const float cachedPathLength = ray.m_maxT;
		const float3 inPath = ray.m_dir * ray.m_maxT;
		const int iterativeLinearCastMaxIterations = 20;
		const float r = ray.m_r;

		{
			float3 contact;
			bool hit = SphereTriLinearCastPostCache(ray.m_orig, r, inPath, opos, QQ, RR, QR,
			cachedPathLength,
			iterativeLinearCastMaxIterations,
			//, void* startCollector
			contact, normal, time);
	
			if (hit)
			{
				uint hitIndex = NdAtomicIncrement(srt.pInstanceConsts->m_gdsOffset);
				if (hitIndex < kMaxHits)
				{
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_triRay     = PackUInt2ToUInt(iRay, iTriangle);
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_levelIdsubmeshId = srt.pSubMeshConsts->m_levelIdsubMeshId;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_t = time;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_positionWs = opos[0];
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_normalWs = normal;
					
					//debug
					#if DEBUG_OUTPUT_TRIANGLE_VERTICES
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_positionWs = opos[0];
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_normalWs = opos[1];
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_uv.y = opos[2].x;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_st = opos[2].yz;
	
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color0 = srt.pInstanceBuffers->t_inputRays[iRay].m_orig;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color1 = srt.pInstanceBuffers->t_inputRays[iRay].m_dir;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_uv.x = srt.pInstanceBuffers->t_inputRays[iRay].m_maxT;
					#endif
				}
				else
				{
					NdAtomicDecrement(srt.pInstanceConsts->m_gdsOffset);
				}
			}
		}
	}
}

void SphereTriIntersectMain_256Rays_BBox(in uint threadX, in uint threadY, in uint iTriangle, in float3 opos[3], in CollRayCastSrt srt, uint iRayStart)
{
	float3 minP = min(opos[0], min(opos[1], opos[2]));
	float3 maxP = max(opos[0], max(opos[1], opos[2]));

	uint lastRay = min(iRayStart + 256, srt.pInstanceConsts->m_numRays);
	uint triHitsFor32Tris0 = 0;
	uint triHitsFor32Tris1 = 0;


	for (uint iRay = iRayStart; iRay < lastRay; ++iRay)
	{
		Ray ray = srt.pInstanceBuffers->t_inputRays[iRay];
		float3 inPath = ray.m_dir * ray.m_maxT;
		float r = ray.m_r;
		float3 orig = ray.m_orig;

		float3 rMinP = min(orig, orig + inPath) - float3(r, r, r);
		float3 rMaxP = max(orig, orig + inPath) + float3(r, r, r);

		if (BBoxIntersect(minP, maxP, rMinP, rMaxP))
		{
			// all we care about if this triangle bbox is hit at least once
			if (threadX < 32)
			{
				triHitsFor32Tris0 = triHitsFor32Tris0 | (1 << (threadX));
			}
			else
			{
				triHitsFor32Tris1 = triHitsFor32Tris1 | (1 << (threadX-32));
			}

			break;
		}
	}

	
	//triHitsFor32Tris0 = triHitsFor32Tris0 | LaneSwizzle(triHitsFor32Tris0, 0x1FU, 0x00U, 0x20U);
	triHitsFor32Tris0 = triHitsFor32Tris0 | LaneSwizzle(triHitsFor32Tris0, 0x1FU, 0x00U, 0x10U);
	triHitsFor32Tris0 = triHitsFor32Tris0 | LaneSwizzle(triHitsFor32Tris0, 0x1FU, 0x00U, 0x08U);
	triHitsFor32Tris0 = triHitsFor32Tris0 | LaneSwizzle(triHitsFor32Tris0, 0x1FU, 0x00U, 0x04U);
	triHitsFor32Tris0 = triHitsFor32Tris0 | LaneSwizzle(triHitsFor32Tris0, 0x1FU, 0x00U, 0x02U);
	triHitsFor32Tris0 = triHitsFor32Tris0 | LaneSwizzle(triHitsFor32Tris0, 0x1FU, 0x00U, 0x01U);

	//triHitsFor32Tris1 = triHitsFor32Tris1 | LaneSwizzle(triHitsFor32Tris1, 0x1FU, 0x00U, 0x20U);
	triHitsFor32Tris1 = triHitsFor32Tris1 | LaneSwizzle(triHitsFor32Tris1, 0x1FU, 0x00U, 0x10U);
	triHitsFor32Tris1 = triHitsFor32Tris1 | LaneSwizzle(triHitsFor32Tris1, 0x1FU, 0x00U, 0x08U);
	triHitsFor32Tris1 = triHitsFor32Tris1 | LaneSwizzle(triHitsFor32Tris1, 0x1FU, 0x00U, 0x04U);
	triHitsFor32Tris1 = triHitsFor32Tris1 | LaneSwizzle(triHitsFor32Tris1, 0x1FU, 0x00U, 0x02U);
	triHitsFor32Tris1 = triHitsFor32Tris1 | LaneSwizzle(triHitsFor32Tris1, 0x1FU, 0x00U, 0x01U);


	triHitsFor32Tris1 = ReadLane(triHitsFor32Tris1, 32);
	if (threadX == 0)
	{
		srt.pInstanceBuffers->rw_bboxHits[iTriangle/32] = triHitsFor32Tris0;
		srt.pInstanceBuffers->rw_bboxHits[iTriangle/32+1] = triHitsFor32Tris1;
	}
}

#define MaxUintTime ((1 << 16) - 1) // 0x0000FFFF

#define MaxTime16 (MaxUintTime << 16)

void SphereTriIntersectMain_16Tris_BBox_Sort(in uint iTriangleStart, in CollRayCastSrt srt, uint iRay, out float resTime, out float3 normal)
{
	Ray ray = srt.pInstanceBuffers->t_inputRays[iRay];
	
	const float cachedPathLength = ray.m_maxT;
	const float3 inPath = ray.m_dir * ray.m_maxT;
	const int iterativeLinearCastMaxIterations = 20;
	const float r = ray.m_r;

	float3 rMinP = min(ray.m_orig, ray.m_orig + inPath) - float3(r, r, r);
	float3 rMaxP = max(ray.m_orig, ray.m_orig + inPath) + float3(r, r, r);


	
	uint lastTri = min(iTriangleStart + 16, srt.pSubMeshConsts->m_numTriangles);

	uint bestTime = MaxTime16;
	
	for (uint iTri = iTriangleStart; iTri < lastTri; ++iTri)
	{

		uint v0Index = mul24(iTri, 3);
		uint3 vidx;
		vidx.x = v0Index;
		vidx.y = v0Index + 1;
		vidx.z = v0Index + 2;

		float3 opos[3];
		opos[0] = srt.pSubMeshBuffers->t_posStream[vidx.x];
		opos[1] = srt.pSubMeshBuffers->t_posStream[vidx.y];
		opos[2] = srt.pSubMeshBuffers->t_posStream[vidx.z];

		float3 minP = min(opos[0], min(opos[1], opos[2]));
		float3 maxP = max(opos[0], max(opos[1], opos[2]));

		if (BBoxIntersect(minP, maxP, rMinP, rMaxP))
		{
			float QQ, RR, QR;
			SetupClosestPointTriangleCache( opos, QQ, RR, QR );
	
			float time = 1.0;
			float3 contact;
			bool hit = SphereTriLinearCastPostCache(ray.m_orig, r, inPath, opos, QQ, RR, QR,
			cachedPathLength,
			iterativeLinearCastMaxIterations,
			//, void* startCollector
			contact, normal, time);
			
			if (hit)
			{
				uint utime = ((uint)(time * (float)(MaxUintTime)) << 16) | (iTri - iTriangleStart) ;
	
				if (utime < bestTime)
				{
					bestTime = utime;
				}
			}
		}
	}

	if (bestTime < MaxTime16)
	{
		resTime = (bestTime >> 16) / float(MaxUintTime);

		uint iTri = (bestTime & 0x000000FF);
		uint iTriangle = iTriangleStart + iTri;

		uint hitIndex = NdAtomicIncrement(srt.pInstanceConsts->m_gdsOffset);
		if (hitIndex < kMaxHits)
		{
			srt.pInstanceBuffers->rw_rayHit[hitIndex].m_triRay     = PackUInt2ToUInt(iRay, iTriangle);
			srt.pInstanceBuffers->rw_rayHit[hitIndex].m_levelIdsubmeshId = srt.pSubMeshConsts->m_levelIdsubMeshId;
			srt.pInstanceBuffers->rw_rayHit[hitIndex].m_t = resTime;
				
			//debug
			#if DEBUG_OUTPUT_TRIANGLE_VERTICES
			//srt.pInstanceBuffers->rw_rayHit[hitIndex].m_positionWs = opos[0];
			//srt.pInstanceBuffers->rw_rayHit[hitIndex].m_normalWs = opos[1];
			//srt.pInstanceBuffers->rw_rayHit[hitIndex].m_uv.y = opos[2].x;
			//srt.pInstanceBuffers->rw_rayHit[hitIndex].m_st = opos[2].yz;
	
			//srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color0 = srt.pInstanceBuffers->t_inputRays[iRay].m_orig;
			//srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color1 = srt.pInstanceBuffers->t_inputRays[iRay].m_dir;
			//srt.pInstanceBuffers->rw_rayHit[hitIndex].m_uv.x = srt.pInstanceBuffers->t_inputRays[iRay].m_maxT;
			#endif
		}
		else
		{
			NdAtomicDecrement(srt.pInstanceConsts->m_gdsOffset);
		}
	}
}


void SphereTriIntersectMain_16Rays_BBox_InLane(in uint iThreadX, in uint iSrcTriangle, in float3 opos[3], in CollRayCastSrt srt, uint iRayStart, out float resTime, out float3 normal)
{
	float QQ, RR, QR;
	SetupClosestPointTriangleCache( opos, QQ, RR, QR );

	float3 minP = min(opos[0], min(opos[1], opos[2]));
	float3 maxP = max(opos[0], max(opos[1], opos[2]));


	uint lastRay = min(iRayStart + 16, srt.pInstanceConsts->m_numRays);
	float bestTime = 1.0;
	uint bestRay = 0;

	for (uint iRay = iRayStart; iRay < lastRay; ++iRay)
	{
		Ray ray = srt.pInstanceBuffers->t_inputRays[iRay];
	
		const float cachedPathLength = ray.m_maxT;
		const float3 inPath = ray.m_dir * ray.m_maxT;
		const int iterativeLinearCastMaxIterations = 20;
		const float r = 0.1;

		float3 rMinP = min(ray.m_orig, ray.m_orig + inPath) - float3(r, r, r);
		float3 rMaxP = max(ray.m_orig, ray.m_orig + inPath) + float3(r, r, r);

		if (BBoxIntersect(minP, maxP, rMinP, rMaxP))
		{
			float time = 1.0;
			float3 contact;
			bool hit = SphereTriLinearCastPostCache(ray.m_orig, r, inPath, opos, QQ, RR, QR,
			cachedPathLength,
			iterativeLinearCastMaxIterations,
			//, void* startCollector
			contact, normal, time);

			if (time < bestTime)
			{
				bestTime = time;
				bestRay = iRay;
			}
		}
	}

	l_laneHitTimes[iThreadX] = (((uint)(bestTime * (float)(MaxUintTime))) << 16) | (iThreadX < 8) | bestRay;

	if (iThreadX < 32)
	{
		l_laneHitTimes[iThreadX] = min(l_laneHitTimes[iThreadX], l_laneHitTimes[iThreadX+32]);
	}

	if (iThreadX < 16)
	{
		l_laneHitTimes[iThreadX] = min(l_laneHitTimes[iThreadX], l_laneHitTimes[iThreadX+16]);
	}

	if (iThreadX < 8)
	{
		l_laneHitTimes[iThreadX] = min(l_laneHitTimes[iThreadX], l_laneHitTimes[iThreadX+8]);
	}

	if (iThreadX < 4)
	{
		l_laneHitTimes[iThreadX] = min(l_laneHitTimes[iThreadX], l_laneHitTimes[iThreadX+4]);
	}

	if (iThreadX < 2)
	{
		l_laneHitTimes[iThreadX] = min(l_laneHitTimes[iThreadX], l_laneHitTimes[iThreadX+2]);
	}

	if (iThreadX < 1)
	{
		l_laneHitTimes[iThreadX] = min(l_laneHitTimes[iThreadX], l_laneHitTimes[iThreadX+1]);

		resTime = (l_laneHitTimes[0] >> 16) / float(MaxUintTime);

		if (resTime < 1.0)
		{
			uint iTriangle = iSrcTriangle + (l_laneHitTimes[0] & 0x0000FF00) >> 8;
			uint iRay = (l_laneHitTimes[0] & 0x000000FF);

			uint hitIndex = NdAtomicIncrement(srt.pInstanceConsts->m_gdsOffset);
			if (hitIndex < kMaxHits)
			{
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_triRay     = PackUInt2ToUInt(iRay, iTriangle);
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_levelIdsubmeshId = srt.pSubMeshConsts->m_levelIdsubMeshId;
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_t = resTime;
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_positionWs = opos[0];
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_normalWs = normal;
				
				//debug
				#if DEBUG_OUTPUT_TRIANGLE_VERTICES
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_positionWs = opos[0];
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_normalWs = opos[1];
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_uv.y = opos[2].x;
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_st = opos[2].yz;
				
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color0 = srt.pInstanceBuffers->t_inputRays[iRay].m_orig;
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color1 = srt.pInstanceBuffers->t_inputRays[iRay].m_dir;
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_uv.x = srt.pInstanceBuffers->t_inputRays[iRay].m_maxT;
				#endif
			}
			else
			{
				NdAtomicDecrement(srt.pInstanceConsts->m_gdsOffset);
			}
		}
	}
}


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

float3 saturate_med3(in float3 v)
{
	// This instruction returns the last argument if any of the inputs are NaNs!
	return med3(v, 1.0f, 0.0f);
}

uint PackHitSortInfo(in uint rayIdx, in float hitDist, in uint hitIdx)
{
	// The sort info is packed into 32 bits with the hit's t value (scaled up to
	// the [0, 65535] range) in the middle 16 bits, followed by the hit index in
	// the lower 8 bits, and finally the ray index in the upper 8 bits of the dword.
	// NOTE: If the maximum number of hits or rays changes, make SURE that there
	// are still enough bits assigned to them!
	uint distScaled = (uint)(hitDist * (float)kSortInfoDistMask);
	uint hitSortInfo = (rayIdx << kSortInfoRayShift) | (distScaled << kSortInfoDistShift) | (hitIdx << kSortInfoHitShift);

	return hitSortInfo;
}

uint UnpackHitSortInfoRay(in uint hitSortInfo)
{
	return (hitSortInfo >> kSortInfoRayShift) & kSortInfoRayMask;
}

float UnpackHitSortInfoDistance(in uint hitSortInfo)
{
	const float kDistScale = 1.0f / (float)kSortInfoDistMask;
	return ((hitSortInfo >> kSortInfoDistShift) & kSortInfoDistMask) * kDistScale;
}

uint UnpackHitSortInfoHit(in uint hitSortInfo)
{
	return (hitSortInfo >> kSortInfoHitShift) & kSortInfoHitMask;
}

void UnpackHitSortInfo(in uint hitSortInfo, out uint rayIdx, out float hitDist, out uint hitIdx)
{
	rayIdx = UnpackHitSortInfoRay(hitSortInfo);
	hitDist = UnpackHitSortInfoDistance(hitSortInfo);
	hitIdx = UnpackHitSortInfoHit(hitSortInfo);
}

uint PowerOfTwoHigher(in uint v)
{
	uint fsbh = FirstSetBit_Hi(v);	// Find first set bit (starting from MSB)
	uint fsbl = FirstSetBit_Lo(v);	// Find first set bit (starting from LSB)

	return (fsbh == fsbl) ? v : (1 << (fsbh + 1));
}

void swap(inout uint x, inout uint y)
{
	uint tmp = y;
	y = x;
	x = tmp;
}

bool PointsClose(float3 v0, float3 v1)
{
	float3 dif = v1-v0;
	return length(dif) < 0.001f;
}


// ====================================================================================================
//		Main Entry Function
// ----------------------------------------------------------------------------------------------------
//	Perform a ray-trace using the 16-bit vertex indexes against m_numRays rays.
//	
//	We are going to launch multiple threadgroups (m_numTriangles threads along the X axis) of
//	[kMaxTriangles, kMaxRays, 1] threads. Each threadgroup is going to collide each ray (along the Y
//	axis) with kMaxTriangles triangles from the mesh (along the X axis). We have two simple steps that
//	each thread performs:
//		1) Setup
//			* First row of threads (0th ray) loads triangle verts into groupshared memory (GSM)
//			* First m_numRays threads of first row loads input rays into GSM, and transforms them into
//				object space
//			* Also initializes "best hit" results for each ray, again in GSM
//		2) Ray/Triangle Intersection Test
//			* Each thread in the threadgroup tests its ray against its corresponding triangle
//			* All hits are written out to the hit accumulation, which uses a hidden, atomic counter to
//				make sure that each hit has a unique index into the buffer.
// ====================================================================================================
void Cs_Common_CollRayTrace_16Rays(in CollRayCastSrt srt, uint iTriangle, uint iRay)
{
	if (iTriangle < srt.pSubMeshConsts->m_numTriangles && iRay < srt.pInstanceConsts->m_numRays)
	{
		// Load the vertices for the triangle this thread corresponds to into shared memory so
		// each ray doesn't have to load the same repeated data from global memory
		uint v0Index = mul24(iTriangle, 3);
		uint3 vidx;
		vidx.x = v0Index;
		vidx.y = v0Index + 1;
		vidx.z = v0Index + 2;

// 		vidx.x = srt.pSubMeshBuffers->t_indexStream[iIndex + 0];
// 		vidx.y = srt.pSubMeshBuffers->t_indexStream[iIndex + 1];
// 		vidx.z = srt.pSubMeshBuffers->t_indexStream[iIndex + 2];

		float3 v[3];
		v[0] = srt.pSubMeshBuffers->t_posStream[vidx.x];
		v[1] = srt.pSubMeshBuffers->t_posStream[vidx.y];
		v[2] = srt.pSubMeshBuffers->t_posStream[vidx.z];

		
		//bool TriIntersect(in float3 opos[3], in Ray ray, out float time, out float3 normal)

		float3 normal;
		float time = 0.0f;
		
		SphereTriIntersectMain_16Rays(iTriangle, v, srt, iRay, time, normal);
	}
}

void Cs_Common_CollSphereCast_16Rays_BBox(in CollRayCastSrt srt, uint iTriangle, uint iRay)
{
	if (iTriangle < srt.pSubMeshConsts->m_numTriangles && iRay < srt.pInstanceConsts->m_numRays)
	{
		// Load the vertices for the triangle this thread corresponds to into shared memory so
		// each ray doesn't have to load the same repeated data from global memory
		uint v0Index = mul24(iTriangle, 3);
		uint3 vidx;
		vidx.x = v0Index;
		vidx.y = v0Index + 1;
		vidx.z = v0Index + 2;

// 		vidx.x = srt.pSubMeshBuffers->t_indexStream[iIndex + 0];
// 		vidx.y = srt.pSubMeshBuffers->t_indexStream[iIndex + 1];
// 		vidx.z = srt.pSubMeshBuffers->t_indexStream[iIndex + 2];

		float3 v[3];
		v[0] = srt.pSubMeshBuffers->t_posStream[vidx.x];
		v[1] = srt.pSubMeshBuffers->t_posStream[vidx.y];
		v[2] = srt.pSubMeshBuffers->t_posStream[vidx.z];

		
		//bool TriIntersect(in float3 opos[3], in Ray ray, out float time, out float3 normal)

		SphereTriIntersectMain_16Rays_BBox(iTriangle, v, srt, iRay);
	}
}


void Cs_Common_CollRayCast_16Rays_BBox(in CollRayCastSrt srt, uint iTriangle, uint iRay)
{
	if (iTriangle < srt.pSubMeshConsts->m_numTriangles && iRay < srt.pInstanceConsts->m_numRays)
	{
		// check layer and pat bits before doing any computation. Note, might want to do it after
		uint patLayer = srt.pSubMeshBuffers->t_patLayerStream[iTriangle];
		
		uint triLayer = patLayer & 0x000000FF;
		uint triPatIndex = patLayer >> 8;
		
		//if ((layerMask & (1 << triLayer)) == 0)
		//	return;

		uint triPatLow = srt.pSubMeshBuffers->t_patPalette[triPatIndex * 2];
		uint triPatHigh = srt.pSubMeshBuffers->t_patPalette[triPatIndex * 2 + 1];

		uint rayPatExcludeLow = srt.pInstanceBuffers->t_inputRays[iRay].m_patExcludeLow;
		uint rayPatExcludeHigh = srt.pInstanceBuffers->t_inputRays[iRay].m_patExcludeHigh;


		// Load the vertices for the triangle this thread corresponds to into shared memory so
		// each ray doesn't have to load the same repeated data from global memory
		uint v0Index = mul24(iTriangle, 3);
		uint3 vidx;
		vidx.x = v0Index;
		vidx.y = v0Index + 1;
		vidx.z = v0Index + 2;

		// 		vidx.x = srt.pSubMeshBuffers->t_indexStream[iIndex + 0];
		// 		vidx.y = srt.pSubMeshBuffers->t_indexStream[iIndex + 1];
		// 		vidx.z = srt.pSubMeshBuffers->t_indexStream[iIndex + 2];

		float3 v[3];
		v[0] = srt.pSubMeshBuffers->t_posStream[vidx.x];
		v[1] = srt.pSubMeshBuffers->t_posStream[vidx.y];
		v[2] = srt.pSubMeshBuffers->t_posStream[vidx.z];


		//bool TriIntersect(in float3 opos[3], in Ray ray, out float time, out float3 normal)

		RayTriIntersectMain_16Rays_BBox(iTriangle, v, srt, iRay);
	}
}

void Cs_Common_CollRayTrace_16Rays_BBox_Check(in CollRayCastSrt srt, in uint threadX, uint iTriangle, uint iRay)
{
	if (iTriangle < srt.pSubMeshConsts->m_numTriangles && iRay < srt.pInstanceConsts->m_numRays)
	{
		uint triangleIndex = iTriangle / 32;
		uint mask = srt.pInstanceBuffers->rw_bboxHits[iTriangle/32];

		uint anyHits = 0;
		if (threadX < 32)
		{
			anyHits = mask & (1 << threadX);
		}
		else
		{
			anyHits = mask & (1 << (threadX - 32));
		}

		if (anyHits == 0)
			return;

		// Load the vertices for the triangle this thread corresponds to into shared memory so
		// each ray doesn't have to load the same repeated data from global memory
		uint v0Index = mul24(iTriangle, 3);
		uint3 vidx;
		vidx.x = v0Index;
		vidx.y = v0Index + 1;
		vidx.z = v0Index + 2;

// 		vidx.x = srt.pSubMeshBuffers->t_indexStream[iIndex + 0];
// 		vidx.y = srt.pSubMeshBuffers->t_indexStream[iIndex + 1];
// 		vidx.z = srt.pSubMeshBuffers->t_indexStream[iIndex + 2];

		float3 v[3];
		v[0] = srt.pSubMeshBuffers->t_posStream[vidx.x];
		v[1] = srt.pSubMeshBuffers->t_posStream[vidx.y];
		v[2] = srt.pSubMeshBuffers->t_posStream[vidx.z];

		
		//bool TriIntersect(in float3 opos[3], in Ray ray, out float time, out float3 normal)

		
		SphereTriIntersectMain_16Rays_BBox(iTriangle, v, srt, iRay);
	}
}
void Cs_Common_CollRayTrace_256Rays_BBox_Only(in CollRayCastSrt srt, uint threadX, uint threadY, uint iTriangle, uint iRay)
{
	//if (iTriangle < srt.pSubMeshConsts->m_numTriangles && iRay < srt.pInstanceConsts->m_numRays)
	{
		// Load the vertices for the triangle this thread corresponds to into shared memory so
		// each ray doesn't have to load the same repeated data from global memory
		uint v0Index = mul24(iTriangle, 3);
		uint3 vidx;
		vidx.x = v0Index;
		vidx.y = v0Index + 1;
		vidx.z = v0Index + 2;

// 		vidx.x = srt.pSubMeshBuffers->t_indexStream[iIndex + 0];
// 		vidx.y = srt.pSubMeshBuffers->t_indexStream[iIndex + 1];
// 		vidx.z = srt.pSubMeshBuffers->t_indexStream[iIndex + 2];

		float3 v[3];
		v[0] = srt.pSubMeshBuffers->t_posStream[vidx.x];
		v[1] = srt.pSubMeshBuffers->t_posStream[vidx.y];
		v[2] = srt.pSubMeshBuffers->t_posStream[vidx.z];

		
		//bool TriIntersect(in float3 opos[3], in Ray ray, out float time, out float3 normal)
		
		SphereTriIntersectMain_256Rays_BBox(threadX, threadY, iTriangle, v, srt, iRay);
	}
}


void Cs_Common_CollRayTrace_16Tris_BBox_Sort(in CollRayCastSrt srt, uint iTriangle, uint iRay)
{
	if (iTriangle < srt.pSubMeshConsts->m_numTriangles && iRay < srt.pInstanceConsts->m_numRays)
	{
		// Load the vertices for the triangle this thread corresponds to into shared memory so
		// each ray doesn't have to load the same repeated data from global memory

		
		//bool TriIntersect(in float3 opos[3], in Ray ray, out float time, out float3 normal)

		float3 normal;
		float time = 0.0f;
		
		SphereTriIntersectMain_16Tris_BBox_Sort(iTriangle, srt, iRay, time, normal);
	}
}

void Cs_Common_CollRayTrace_16Rays_BBox_InLaneSort(in CollRayCastSrt srt, uint iThreadX, uint iTriangle, uint iRay)
{
	if (iTriangle < srt.pSubMeshConsts->m_numTriangles && iRay < srt.pInstanceConsts->m_numRays)
	{
		// Load the vertices for the triangle this thread corresponds to into shared memory so
		// each ray doesn't have to load the same repeated data from global memory
		uint v0Index = mul24(iTriangle, 3);
		uint3 vidx;
		vidx.x = v0Index;
		vidx.y = v0Index + 1;
		vidx.z = v0Index + 2;

// 		vidx.x = srt.pSubMeshBuffers->t_indexStream[iIndex + 0];
// 		vidx.y = srt.pSubMeshBuffers->t_indexStream[iIndex + 1];
// 		vidx.z = srt.pSubMeshBuffers->t_indexStream[iIndex + 2];

		float3 v[3];
		v[0] = srt.pSubMeshBuffers->t_posStream[vidx.x];
		v[1] = srt.pSubMeshBuffers->t_posStream[vidx.y];
		v[2] = srt.pSubMeshBuffers->t_posStream[vidx.z];

		
		//bool TriIntersect(in float3 opos[3], in Ray ray, out float time, out float3 normal)

		float3 normal;
		float time = 0.0f;
		
		SphereTriIntersectMain_16Rays_BBox_InLane(iThreadX, iTriangle, v, srt, iRay, time, normal);
	}
}


void Cs_Common_CollRayTrace(in CollRayCastSrt srt, uint iTriangle, uint iRay)
{
	if (iTriangle < srt.pSubMeshConsts->m_numTriangles && iRay < srt.pInstanceConsts->m_numRays)
	{
		// Load the vertices for the triangle this thread corresponds to into shared memory so
		// each ray doesn't have to load the same repeated data from global memory
		uint v0Index = mul24(iTriangle, 3);
		uint3 vidx;
		vidx.x = v0Index;
		vidx.y = v0Index + 1;
		vidx.z = v0Index + 2;

// 		vidx.x = srt.pSubMeshBuffers->t_indexStream[iIndex + 0];
// 		vidx.y = srt.pSubMeshBuffers->t_indexStream[iIndex + 1];
// 		vidx.z = srt.pSubMeshBuffers->t_indexStream[iIndex + 2];

		float3 v[3];
		v[0] = srt.pSubMeshBuffers->t_posStream[vidx.x];
		v[1] = srt.pSubMeshBuffers->t_posStream[vidx.y];
		v[2] = srt.pSubMeshBuffers->t_posStream[vidx.z];

		
		//bool TriIntersect(in float3 opos[3], in Ray ray, out float time, out float3 normal)

		float3 normal;
		float time = 0.0f;
		
		SphereTriIntersectMain(iTriangle, v, srt, iRay, time, normal);
		

		#if 0
		if (
		//TriIntersect(v, srt.pInstanceBuffers->t_inputRays[iRay], time, normal)
		SphereTriIntersect(v, srt, iRay, time, normal)
		
		//|| 
		//(iTriangle == 0)
		)
		{
			uint hitIndex = NdAtomicIncrement(srt.pInstanceConsts->m_gdsOffset);
			if (hitIndex < kMaxHits)
			{
			
				
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_tri     = PackUInt2ToUInt(0, iTriangle);
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_t = time;
				//srt.pInstanceBuffers->rw_rayHit[hitIndex0].m_t = -11.1f;
			
				//debug
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_positionWs = v[0];
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_normalWs = v[1];
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_uv.y = v[2].x;
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_st = v[2].yz;

				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_rayLevel = iRay; // PackUInt2ToUInt(iRay, srt.pInstanceConsts->m_levelId); //srt.pInstanceConsts->m_numRays;
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color0 = srt.pInstanceBuffers->t_inputRays[iRay].m_orig;
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color1 = srt.pInstanceBuffers->t_inputRays[iRay].m_dir;
				srt.pInstanceBuffers->rw_rayHit[hitIndex].m_uv.x = srt.pInstanceBuffers->t_inputRays[iRay].m_maxT;
			}
			else
			{
				NdAtomicDecrement(srt.pInstanceConsts->m_gdsOffset);
			}
		}
		#endif

		// store it into shared memory that has to be per group. would need to do this in first threads only (i.e. raycast num == 0)
// 		uint iLocalTriangle = groupThreadId.x;
// 		uint iFirstVertex = mul24(iLocalTriangle, 3);
// 
// 		l_triVerts[iFirstVertex + 0] = srt.pSubMeshBuffers->t_posStream[vidx.x];
// 		l_triVerts[iFirstVertex + 1] = srt.pSubMeshBuffers->t_posStream[vidx.y];
// 		l_triVerts[iFirstVertex + 2] = srt.pSubMeshBuffers->t_posStream[vidx.z];
// 		l_vertIdx[iLocalTriangle] = vidx;				
	}
	return;
	#if 0

	//
	// Setup
	//
	{
		// First row of threads in this group does most of the setup
		if (groupThreadId.y == 0)
		{
			// First we test all of the rays against this mesh's AABB. If none of them hit,
			// then we don't need to bother collecting all of the triangles and we can just bail!

			// The first srt.pInstanceConsts->m_numRays threads in this row transform the input rays into object space and
			// also saves them into shared memory
			uint iRay = groupThreadId.x;
			if (iRay < srt.pInstanceConsts->m_numRays)
			{
				// Test this ray against the AABB of the mesh; if it fails, do not test it against any triangles!

				// The bounding box is in world space, so we don't want to transform the input rays
				// before testing against it! We do, however, want to make sure that we've transformed
				// the ray and box to be in the positive octant of the world, so subtract the box's min
				// coordinate from the origin of the ray.
				float3 dirWs = srt.pInstanceBuffers->t_inputRays[iRay].m_dir;
				float3 orgWs = srt.pInstanceBuffers->t_inputRays[iRay].m_orig;
				float3 dirOs = vmul(srt.pInstanceConsts->m_worldToObj, dirWs);
				float3 orgOs = pmul(srt.pInstanceConsts->m_worldToObj, orgWs);

				float3 invD = 1.0f / dirOs;

				// AABB of the mesh the triangles make up, in object space!
				float3 t0 = (srt.pSubMeshBuffers->t_bounds[0].m_min - orgOs) * invD;
				float3 t1 = (srt.pSubMeshBuffers->t_bounds[0].m_max - orgOs) * invD;

				float  minT = kRayMinDist;
				float  maxT = srt.pInstanceBuffers->t_inputRays[iRay].m_maxT;

				float tMinBox = max(minT, max3(min(t0.x, t1.x), min(t0.y, t1.y), min(t0.z, t1.z)));
				float tMaxBox = min(maxT, min3(max(t0.x, t1.x), max(t0.y, t1.y), max(t0.z, t1.z)));

				bool intersects = (tMinBox <= tMaxBox);

				// Test the flags - 
				// There must be a common flag in low 16 (mesh type)
				// and either upper 16 (behavior) is zero or there must be some common flag
				uint typeMaskFlags = srt.pInstanceBuffers->t_inputRays[iRay].m_typeMaskFlags;
				uint commonFlags = typeMaskFlags & srt.pSubMeshConsts->m_collideMask;
				bool typesMatch = (commonFlags & 0xFFFF) != 0 && ((srt.pSubMeshConsts->m_collideMask >> 16) == 0 || (commonFlags >> 16) != 0);

				bool useRay = typesMatch && intersects;

				l_rayAabbTest[iRay] = useRay;

				// If we intersected, write out the local version of the ray
				if (useRay)
				{
					// Input rays, in object space for intersection testing
					l_inputRays[iRay].m_orig          = orgOs;
					l_inputRays[iRay].m_typeMaskFlags = typeMaskFlags;
					l_inputRays[iRay].m_dir           = dirOs;
					l_inputRays[iRay].m_maxT          = 1.0f / maxT;
				}

				// Use ballot() to figure out if any rays are valid, and have the first thread
				// write that status out to l_anyHits! This lets us avoid having to sync after
				// initializing the LDS variable, and also prevents multiple threads from all
				// writing to it simultaneously.
				bool anyRaysValid = ballot(useRay) != 0;
				if (groupThreadId.x == 0)
				{
					l_anyHits = anyRaysValid;
				}
			}
		}

		// NOTE: No sync SHOULD be necessary unless we have more rays than fit in a single warp!
		GroupMemoryBarrierWithGroupSync();

		// If we have no rays that hit this mesh's bounding box, we're done before we even began!
		if (!l_anyHits)
			return;

		if (groupThreadId.y == 0)
		{
			// Triangles are indexed along the X axis of each thread group.
			uint iTriangle = dispatchThreadId.x;
			if (iTriangle < srt.pSubMeshConsts->m_numTriangles)
			{
				// Load the vertices for the triangle this thread corresponds to into shared memory so
				// each ray doesn't have to load the same repeated data from global memory
				uint iIndex = mul24(iTriangle, 3);
				uint3 vidx;
				vidx.x = srt.pSubMeshBuffers->t_indexStream[iIndex + 0];
				vidx.y = srt.pSubMeshBuffers->t_indexStream[iIndex + 1];
				vidx.z = srt.pSubMeshBuffers->t_indexStream[iIndex + 2];
				uint iLocalTriangle = groupThreadId.x;
				uint iFirstVertex = mul24(iLocalTriangle, 3);

				l_triVerts[iFirstVertex + 0] = srt.pSubMeshBuffers->t_posStream[vidx.x];
				l_triVerts[iFirstVertex + 1] = srt.pSubMeshBuffers->t_posStream[vidx.y];
				l_triVerts[iFirstVertex + 2] = srt.pSubMeshBuffers->t_posStream[vidx.z];
				l_vertIdx[iLocalTriangle] = vidx;				
			}
		}

		// Threadgroup sync point! Make sure the GSM is all set up before moving on.
		GroupMemoryBarrierWithGroupSync();
	}

	//
	// Ray/Triangle Intersection
	//
	{
		// Indices based on our position in the threadgroup
		uint iRay      = groupThreadId.y;
		uint iTriangle = dispatchThreadId.x;

		if (iTriangle < srt.pSubMeshConsts->m_numTriangles && iRay < srt.pInstanceConsts->m_numRays && l_rayAabbTest[iRay])
		{
			// Load this triangle's verts and this ray's info from GSM
			float3 opos[3];

			uint iLocalTriangle = groupThreadId.x;
			uint iFirstVertex = mul24(iLocalTriangle, 3);
			opos[0] = l_triVerts[iFirstVertex + 0];
			opos[1] = l_triVerts[iFirstVertex + 1];
			opos[2] = l_triVerts[iFirstVertex + 2];

			bool intersects = true;
			float bu, bv;
			float t;
			float3 nrmOs;

			// Intersect the ray with the triangle! Calculate the intersection
			// time t, and the u and v barycentric coordinates.
			{
				float3 edge1    = opos[1] - opos[0];
				float3 edge2    = opos[2] - opos[0];
				float3 dir      = l_inputRays[iRay].m_dir;
				float3 pvec     = cross(dir, edge2);
				float  det      = dot(edge1, pvec);
				float  inv_det  = 1.0f / det;
				float3 orig     = l_inputRays[iRay].m_orig;
				float3 svec     = orig - opos[0];

				bu = dot(svec, pvec) * inv_det;
				if (bu < 0.0f || bu >= 1.0f)
					intersects = false;

				float3 qvec = cross(svec, edge1);

				bv = dot(dir, qvec) * inv_det;
				if (bv < 0.0f || bu + bv >= 1.0f)
					intersects = false;

				// Calculate the intersection distance, in world units
				float dist = dot(edge2, qvec) * inv_det;

				// Normalize the intersection distance by dividing by maxT
				float inv_maxT = l_inputRays[iRay].m_maxT;
				t = dist * inv_maxT;

				// Use a small minimum to avoid precision issues when the ray hits a triangle
				// very close to its origin.
				if (dist < kRayMinDist || t >= 1.0f)
					intersects = false;

				// Test which direction this triangle's normal is facing and if that matches
				// up with the flags for this ray. Since we only care about the dot product's
				// sign, we do not need to normalize yet.
				{
					nrmOs = cross(edge1, edge2);
					float ndotd = dot(nrmOs, dir);

					uint flags = l_inputRays[iRay].m_typeMaskFlags;

					// Not passing through back faces OR face must be front face (N.D must be negative)!
					intersects = intersects && (!(flags & kIgnoreBackFacingTriangles ) || ndotd  < 0.0f);
					// Not passing through front faces OR face must be back face (N.D must be positive)!
					intersects = intersects && (!(flags & kIgnoreFrontFacingTriangles) || ndotd >= 0.0f);
				}
			}

			// We now write *EVERY* hit to our output ray hit accumulation buffer. This way,
			// there is no need to employ complicated locking mechanisms or the tricky atomic
			// minimum we were using before; instead, we write out all the hits and then go
			// through them in a second compute shader to figure out the closest hits for each
			// ray, which are then written out all at once!
			if (intersects)
			{
				// DBTD: ACHTUNG! The IncrementCounter() and DecrementCounter() functions on
				// RW_RegularBuffer objects emit the ds_append and ds_consume instructions,
				// without doing any sort of bounds checking on the atomic counter to make sure
				// that it's not over- or under-flowing the size of the array. This means that
				// we must handle this ourselves; we increment the counter for every thread that
				// intersects its triangle, and we then check to see if each thread's index is
				// out of range. The threads that are out of range simply decrement the atomic
				// counter, and we can be sure that we will never write to an index that is out
				// of bounds OR end up with a counter value that is greater than the maximum!
				uint hitIndex = srt.pInstanceBuffers->rw_rayHit.IncrementCounter(0);
				if (hitIndex < kMaxHits)
				{
					// We have reserved space in the output hit accumulation buffer! Write out
					// our hit information, making sure we transform the hit to world space!
					float bw = 1.0f - bu - bv;

					// Also note that to keep register usage down, we should be writing out the
					// data we're calculating as soon as we possibly can, rather than keeping it
					// around for any longer than is necessary. We should also make an effort to
					// be writing out in 4 dword chunks when possible, to minimize the number of
					// separate writes to main memory that won't cache as well.

					// Calculate the object-space interpolated position, then transform it back
					// into world-space
					float3 posOs = bw * opos[0] + bu * opos[1] + bv * opos[2];
					float3 posWs = pmul(srt.pInstanceConsts->m_objToWorld, posOs);

					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_rayLevel     = PackUInt2ToUInt(iRay, srt.pInstanceConsts->m_levelId);
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_positionWs   = posWs;

					// Use the triangle's calculated face normal, rather than interpolating the
					// per-vertex normals. It's highly unlikely that we'll actually need to be
					// using the "real" interpolated normal here. In order to properly account
					// for non-uniform scale, we must transform the object-space normal vector
					// by the inverse transpose of the object-to-world-space matrix!
					float3 nrmWs = vmul(srt.pInstanceConsts->m_objToWorldIT, nrmOs);

					// Gotta normalize the normal now, since we did not normalize before!
					// TODO: We only need to normalize this if it's the best hit for this ray,
					// which can be done in the gather kernel!
					nrmWs = normalize(nrmWs);

					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_instanceMesh = PackUInt2ToUInt(srt.pInstanceConsts->m_instanceId, srt.pSubMeshConsts->m_subMeshId);
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_normalWs     = nrmWs;

					// Grab this triangle's vertex indices from LDS, so we know where in each
					// vertex data stream to be sampling from.
					uint3 vidx = l_vertIdx[iLocalTriangle];

					// Now sample the first vertex color values, for each vert of this triangle,
					// and then interpolate it the same way we interpolate everything else. Note
					// that currently, the tools allow values for the color channels to be passed
					// through that are outside of the [0, 1] range, so in order to match what the
					// material shaders do we must clamp the color values we sample from each vert!
					float3 color0 = float3(0.0f, 0.0f, 0.0f);
					
					if (srt.pSubMeshConsts->m_flags & 1)
					{
						float3 vcolor0[3];

						vcolor0[0] = saturate(srt.pSubMeshBuffers->t_clr0Stream[vidx.x]);
						vcolor0[1] = saturate(srt.pSubMeshBuffers->t_clr0Stream[vidx.y]);
						vcolor0[2] = saturate(srt.pSubMeshBuffers->t_clr0Stream[vidx.z]);

						color0 = bw * vcolor0[0] + bu * vcolor0[1] + bv * vcolor0[2];
					}

					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_tri          = iTriangle;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color0       = color0;

					// And now the second vertex color stream...
					float3 color1 = float3(0.0f, 0.0f, 0.0f);
					
					if (srt.pSubMeshConsts->m_flags & 2)
					{
						float3 vcolor1[3];

						vcolor1[0] = saturate(srt.pSubMeshBuffers->t_clr1Stream[vidx.x]);
						vcolor1[1] = saturate(srt.pSubMeshBuffers->t_clr1Stream[vidx.y]);
						vcolor1[2] = saturate(srt.pSubMeshBuffers->t_clr1Stream[vidx.z]);

						color1 = bw * vcolor1[0] + bu * vcolor1[1] + bv * vcolor1[2];
					}

					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_t            = t;
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_color1       = color1;

					// Finally, the barycentric st coordinates and the interpolated uv coordinates!
					float2 uv = float2(0.0f, 0.0f);

					if (srt.pSubMeshConsts->m_flags & 4)
					{
						float2 vuv[3];

						vuv[0] = srt.pSubMeshBuffers->t_uvStream[vidx.x];
						vuv[1] = srt.pSubMeshBuffers->t_uvStream[vidx.y];
						vuv[2] = srt.pSubMeshBuffers->t_uvStream[vidx.z];

						uv = bw * vuv[0] + bu * vuv[1] + bv * vuv[2];
					}

					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_st           = float2(bu, bv);
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_uv           = uv;
					
#if DEBUG_OUTPUT_TRIANGLE_VERTICES
					// For debugging purposes, we can also output the world-space positions of each
					// vertex of the hit triangle, but normally 
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_debug0 = float4(pmul(srt.pInstanceConsts->m_objToWorld, opos[0]), 0.0f);
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_debug1 = float4(pmul(srt.pInstanceConsts->m_objToWorld, opos[1]), 0.0f);
					srt.pInstanceBuffers->rw_rayHit[hitIndex].m_debug2 = float4(pmul(srt.pInstanceConsts->m_objToWorld, opos[2]), 0.0f);			
#endif

					// The gather step does not require any data from the list of hits other than the
					// ray index, the hit index, and the hit distance. We can easily store all of that
					// information in 32 bits, putting distance in the high 16 bits, followed by ray
					// index, then the hit index which is only useful when we have chosen the best
					// hit for a ray and we need to get the rest of its data into the output buffer!
					srt.pInstanceBuffers->rw_rayHitSortInfo[hitIndex] = PackHitSortInfo(iRay, t, hitIndex);
				}
				else
				{
					// We ran out of room in the buffer! Make sure we decrement its counter so
					// other code doesn't come by later thinking it contains more elements than
					// it was allocated to hold!
					srt.pInstanceBuffers->rw_rayHit.DecrementCounter(0);
				}
			}
		}
	}
	#endif
}



#define kMaxRays			16		// Maximum number of rays that can be cast by a single dispatch
#define kMaxTriangles		64		// Maximum number of triangles to intersect per dispatch

[numthreads(kMaxTriangles, kMaxRays, 1)]
void Cs_CollRayTrace_ThreadPerTri_WFPerRay_64Tris_16Rays(uint3 dispatchThreadId	: SV_DispatchThreadID, // global big index
				 uint3 groupThreadId	: SV_GroupThreadId, // index of thread in group
				 CollRayCastSrt srt : S_SRT_DATA
                 )
{
	
	uint iTriangle = dispatchThreadId.x;
	uint iRay      = dispatchThreadId.y;

	Cs_Common_CollRayTrace(srt, iTriangle, iRay);
}

[numthreads(kMaxTriangles, 1, 1)]
void Cs_CollRayTrace_ThreadPerTri_WFPerRay_64Tris_1Rays(uint3 dispatchThreadId	: SV_DispatchThreadID, // global big index
				 uint3 groupThreadId	: SV_GroupThreadId, // index of thread in group
				 CollRayCastSrt srt : S_SRT_DATA
                 )
{
	
	uint iTriangle = dispatchThreadId.x;
	uint iRay      = dispatchThreadId.y;

	Cs_Common_CollRayTrace(srt, iTriangle, iRay);
}

[numthreads(kMaxTriangles, 1, 1)]
void Cs_CollRayTrace_ThreadPerTri_WFPer16Rays_64Tris_1RayBacthes(uint3 dispatchThreadId	: SV_DispatchThreadID, // global big index
				 uint3 groupThreadId	: SV_GroupThreadId, // index of thread in group
				 CollRayCastSrt srt : S_SRT_DATA
                 )
{
	
	uint iTriangle = dispatchThreadId.x;
	uint iRay      = dispatchThreadId.y * 16;

	Cs_Common_CollRayTrace_16Rays(srt, iTriangle, iRay);
}

[numthreads(kMaxTriangles, 1, 1)]
void Cs_CollRayTrace_ThreadPerTri_WFPer16Rays_64Tris_1RayBacthes_BBox(uint3 dispatchThreadId	: SV_DispatchThreadID, // global big index
				 uint3 groupThreadId	: SV_GroupThreadId, // index of thread in group
				 CollRayCastSrt srt : S_SRT_DATA
                 )
{
	
	uint iTriangle = dispatchThreadId.x;
	uint iRay      = dispatchThreadId.y * 16;

	Cs_Common_CollSphereCast_16Rays_BBox(srt, iTriangle, iRay);
}

[numthreads(kMaxTriangles, 1, 1)]
void Cs_CollRayCast_ThreadPerTri_WFPer16Rays_64Tris_1RayBacthes_BBox(uint3 dispatchThreadId	: SV_DispatchThreadID, // global big index
	uint3 groupThreadId : SV_GroupThreadId, // index of thread in group
	CollRayCastSrt srt : S_SRT_DATA
	)
{

	uint iTriangle = dispatchThreadId.x;
	uint iRay = dispatchThreadId.y * 16;

	Cs_Common_CollRayCast_16Rays_BBox(srt, iTriangle, iRay);
}

[numthreads(512, 1, 1)]
void Cs_CollCast_FindMinTimes(uint3 dispatchThreadId	: SV_DispatchThreadID, // global big index
	uint3 groupThreadId : SV_GroupThreadId, // index of thread in group
	CollRayCastReduceSrt srt : S_SRT_DATA
	)
{
	srt.pInstanceBuffers->rw_minRayHits[dispatchThreadId.x].m_t = -1.0f;

	if (dispatchThreadId.x >= srt.pInstanceConsts->m_numRays)
		return;

	float minTime = 1.0f;
	int minIndex = -1;
	int numHits = srt.pInstanceConsts->m_numHits;
	for (int i = 0; i < srt.pInstanceConsts->m_numHits; ++i)
	{
		CollRayHit hit = srt.pInstanceBuffers->rw_rayHit[i];
		uint rayIndex = hit.m_triRay & 0x0000FFFF;
		if (rayIndex == dispatchThreadId.x)
		{
			if (hit.m_t < minTime)
			{
				minIndex = i;
				minTime = hit.m_t;
			}
		}
	}
	if (minIndex >= 0)
	{
		// found it
		srt.pInstanceBuffers->rw_minRayHits[dispatchThreadId.x] = srt.pInstanceBuffers->rw_rayHit[minIndex];
	}
	else
	{
		srt.pInstanceBuffers->rw_minRayHits[dispatchThreadId.x].m_t = 1.1f; // > 1.0 means no hit
	}

	srt.pInstanceBuffers->rw_minRayHits[dispatchThreadId.x].m_pad = numHits;

}


[numthreads(kMaxTriangles, 1, 1)]
void Cs_CollRayTrace_ThreadPerTri_WFPer16Rays_64Tris_1RayBacthes_BBox_Check(uint3 dispatchThreadId	: SV_DispatchThreadID, // global big index
				 uint3 groupThreadId	: SV_GroupThreadId, // index of thread in group
				 CollRayCastSrt srt : S_SRT_DATA
                 )
{
	
	uint iTriangle = dispatchThreadId.x;
	uint iRay      = dispatchThreadId.y * 16;

	Cs_Common_CollRayTrace_16Rays_BBox_Check(srt, groupThreadId.x, iTriangle, iRay);
}

[numthreads(64, 1, 1)]
void Cs_CollRayTrace_ThreadPerTri_WFPer256Rays_64Tris_BBox(uint3 dispatchThreadId	: SV_DispatchThreadID, // global big index
				 uint3 groupThreadId	: SV_GroupThreadId, // index of thread in group
				 CollRayCastSrt srt : S_SRT_DATA
                 )
{
	
	uint iTriangle = dispatchThreadId.x;
	uint iRay      = dispatchThreadId.y * 256;

	Cs_Common_CollRayTrace_256Rays_BBox_Only(srt, groupThreadId.x, groupThreadId.y, iTriangle, iRay);
}


[numthreads(kMaxTriangles, 1, 1)]
void Cs_CollRayTrace_ThreadPerTri_WFPer16Tris_64Rays_BBox_Sort(uint3 dispatchThreadId	: SV_DispatchThreadID, // global big index
				 uint3 groupThreadId	: SV_GroupThreadId, // index of thread in group
				 CollRayCastSrt srt : S_SRT_DATA
                 )
{
	
	uint iTriangle = dispatchThreadId.y * 16;
	uint iRay      = dispatchThreadId.x;

	Cs_Common_CollRayTrace_16Tris_BBox_Sort(srt, iTriangle, iRay);
}




[numthreads(kMaxTriangles, 1, 1)]
void Cs_CollRayTrace_ThreadPerTri_WFPer16Rays_64Tris_1RayBacthes_BBox_InLane(uint3 dispatchThreadId	: SV_DispatchThreadID, // global big index
				 uint3 groupThreadId	: SV_GroupThreadId, // index of thread in group
				 CollRayCastSrt srt : S_SRT_DATA
                 )
{
	
	uint iTriangle = dispatchThreadId.x;
	uint iRay      = dispatchThreadId.y * 16;

	Cs_Common_CollRayTrace_16Rays_BBox_InLaneSort(srt, groupThreadId.x, iTriangle, iRay);
}

[numthreads(16, 64, 1)]
void Cs_CollRayTrace_ThreadPerTri_WFPerRay_16Tris_64Rays(uint3 dispatchThreadId	: SV_DispatchThreadID, // global big index
				 uint3 groupThreadId	: SV_GroupThreadId, // index of thread in group
				 CollRayCastSrt srt : S_SRT_DATA
                 )
{
	
	uint iTriangle = dispatchThreadId.x;
	uint iRay      = dispatchThreadId.y;

	Cs_Common_CollRayTrace(srt, iTriangle, iRay);
}

[numthreads(64, 16, 1)]
void Cs_CollRayTrace_ThreadPerTri_WFPerTri_16Tris_64Rays(uint3 dispatchThreadId	: SV_DispatchThreadID, // global big index
				 uint3 groupThreadId	: SV_GroupThreadId, // index of thread in group
				 CollRayCastSrt srt : S_SRT_DATA
                 )
{
	
	uint iTriangle = dispatchThreadId.y;
	uint iRay      = dispatchThreadId.x;

	Cs_Common_CollRayTrace(srt, iTriangle, iRay);
}




#if 0
struct RayCastGatherConstants // : register(b0)
{
	uint    m_maxHitCount;
	uint    m_hitCount;
};

struct RayCastGatherBuffers
{
	RWStructuredBuffer<CollRayHit>	rw_rayHit; //			: register(u0);
	StructuredBuffer<CollRayHit>	t_rayHit; //			: register(t0);
	StructuredBuffer<uint>		t_rayHitSortInfo; //	: register(t1);
};

struct RayCastGatherSrt
{
	RayCastGatherConstants *pConsts;
	RayCastGatherBuffers *pBuffers;
};

// ====================================================================================================
//		Result Reduction Kernel
// ----------------------------------------------------------------------------------------------------
//	Figure out the best hits from the result buffer, reducing them down in a parallel fashion.
// ====================================================================================================
[numthreads(kMaxHits, 1, 1)] 
void Cs_RayTraceGatherHits(uint3 dispatchThreadId : SV_DispatchThreadID,
                           RayCastGatherSrt srt   : S_SRT_DATA)
{
	// This is actually a pretty simple algorithm! All we're doing is sorting the input array
	// of 32bit packed hit data, and outputting the first instance of each ray we encounter.
	// The packed data has the ray index as the high 8 bits, followed by the distance packed
	// into 16bits; we don't even have to worry about the low 8 bits, we can just sort the
	// values as-is! After they're sorted, each thread reads the ray index of its hit in the
	// array and its neighbor's, and if they are different, writes out that hit as the best
	// one for the ray index in question!

	uint hidx = dispatchThreadId.x;

	// Calculate the next higher power-of-two above the hit count
	uint hitCountPot = PowerOfTwoHigher(srt.pConsts->m_hitCount);

	// First, copy the input sort info array to local memory
	l_hits[hidx] = (hidx < srt.pConsts->m_hitCount) ? srt.pBuffers->t_rayHitSortInfo[hidx] : kSortInfoInvalid;

	// Make sure the whole array is written before continuing on
	GroupMemoryBarrierWithGroupSync();

	// Bitonic sort to sort the array in place by first ray index and by hit distance for
	// hits from the same ray. We could unroll this fairly easily, but I'm not sure how
	// much that would help. I will have to check it out later!
	// See http://en.wikipedia.org/wiki/Bitonic_sorter for details on the Bitonic Sort.
	for (uint k = 2; k <= hitCountPot; k <<= 1)
	{
		for (uint j = (k >> 1); j > 0; j >>= 1)
		{
			// Only the lower half of the threads do the comparison and value swapping
			uint oidx = hidx ^ j;
			if (oidx > hidx)
			{
				uint hit_hidx = l_hits[hidx];
				uint hit_oidx = l_hits[oidx];

				bool gt = hit_hidx > hit_oidx;
				bool lt = hit_hidx < hit_oidx;
				bool ascending = !(hidx & k);	// First half is sorted ascending

				if (ascending ? gt : lt)
				{
					swap(l_hits[hidx], l_hits[oidx]);
				}
			}

			// Must sync after each step!
			GroupMemoryBarrierWithGroupSync();
		}
	}

	// Now, for each hit whose ray index differs from the previous element's, write it out!
	{
		uint hit = l_hits[hidx];
		uint prevHit = hidx ? l_hits[hidx - 1] : kSortInfoInvalid;

		uint rayIdx = UnpackHitSortInfoRay(hit);
		uint prevRayIdx = UnpackHitSortInfoRay(prevHit);
		if (rayIdx != prevRayIdx)
		{
			// Got one! Now we need to write the full data to the output array! We have all
			// the indices we need packed in the sort info!
			uint hitIdx = UnpackHitSortInfoHit(hit);

			// This is a workaround for a bug in the current version of the wave compiler
			//srt.pBuffers->rw_rayHit[rayIdx] = srt.pBuffers->t_rayHit[hitIdx];
			RayHit rayHit = srt.pBuffers->t_rayHit[hitIdx];
			srt.pBuffers->rw_rayHit[rayIdx] = rayHit;
		}
	}
}

#endif
