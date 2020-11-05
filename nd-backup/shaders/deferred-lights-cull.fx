#pragma pack_matrix(row_major)

#include "hlsl2pssl.fxi"
#define IS_COMPUTE_SHADER
#include "deferred-include.fxi"
#include "tile-util.fxi"
#include "culling.fxi"

#define NUM_TOTAL_THREADS (TILE_SIZE * TILE_SIZE)
#define NUM_WAVEFRONTS (NUM_TOTAL_THREADS / 64)

#if TILE_SIZE > 8
groupshared float2 s_minMaxDepth[NUM_WAVEFRONTS];
#endif

/* 
 * If we cull using the same job distribution as rendering (16x16), we would be culling
 * 16x16=256 each iteration per block. Obviously this is a complete waste of resources
 * since we never have more than a couple dozen lights per scene
 * So we re-arrange the tiles so that there are kRenderTilesPerTileDim X kRenderTilesPerTileDim
 * render tiles per culling tile. Right now that number is 4x4. So each culling threadgroup
 * culls the lights for 16 rendering tiles.
*/

#define CULL_TILE_SIZE 8

// With CULL_TILE_SIZE being 8, we only get 4x4 rendering tiles with each cull tile.
// CULL_TILE_SPAN is a multiplier on that number. So 2 means we get 4x4 rendering tiles with each cull tile
#define CULL_TILE_SPAN 2

#define kRenderTilesPerTileDim ((TILE_SIZE/CULL_TILE_SIZE)*CULL_TILE_SPAN) // 4
#define kTileFactor (CULL_TILE_SIZE / kRenderTilesPerTileDim) // 2

groupshared uint numTileVisibleLights[kTileFactor];

struct CullSrt
{
	Texture2D<float2> depthMinMax;
	
	RegularBuffer<LightDesc> lights;
	RW_RegularBuffer<uint> lightsPerTile;
	RW_RegularBuffer<uint> numLightsPerTile;
	uint numLights;

	float4x4 screenToView;
	
	float3 topLeftCornerVS;
	float2 tileSizeVS;
	
	float2 screenViewParameter;
	uint2 screenSize;

	uint disableNearPlaneCulling;
	uint m_frameNumber;
	float m_minDepth;
};

void FindMinMaxDepth(uint threadGroupIndex, inout float minDepth, inout float maxDepth)
{
	// Do a simple reduction using lane swizzling
	
	// Step 1: Get 16 (flip the 5th bit)
	float reduceMin = LaneSwizzle(minDepth, 0x1F, 0x00, 0x10);
	float reduceMax = LaneSwizzle(maxDepth, 0x1F, 0x00, 0x10);
	minDepth = min(minDepth, reduceMin);
	maxDepth = max(maxDepth, reduceMax);

	// Step 2: Get the min/max depth of the 8th lane
	reduceMin = LaneSwizzle(minDepth, 0x1F, 0x00, 0x8);
	reduceMax = LaneSwizzle(maxDepth, 0x1F, 0x00, 0x8);
	minDepth = min(minDepth, reduceMin);
	maxDepth = max(maxDepth, reduceMax);

	// Step 3: Get the min/max of the 4th lane
	reduceMin = LaneSwizzle(minDepth, 0x1F, 0x00, 0x4);
	reduceMax = LaneSwizzle(maxDepth, 0x1F, 0x00, 0x4);
	minDepth = min(minDepth, reduceMin);
	maxDepth = max(maxDepth, reduceMax);

	// Step 4: Get the min/max of the 2nd lane
	reduceMin = LaneSwizzle(minDepth, 0x1F, 0x00, 0x2);
	reduceMax = LaneSwizzle(maxDepth, 0x1F, 0x00, 0x2);
	minDepth = min(minDepth, reduceMin);
	maxDepth = max(maxDepth, reduceMax);

	// Step 5: Get the min/max of the 1st lane
	reduceMin = LaneSwizzle(minDepth, 0x1F, 0x00, 0x1);
	reduceMax = LaneSwizzle(maxDepth, 0x1F, 0x00, 0x1);
	minDepth = min(minDepth, reduceMin);
	maxDepth = max(maxDepth, reduceMax);

	// Finally, get the minmax of the other group of 32 registers
	minDepth = min(ReadLane(minDepth, 0), ReadLane(minDepth, 32));
	maxDepth = max(ReadLane(maxDepth, 0), ReadLane(maxDepth, 32));

	#if TILE_SIZE >= 16
	uint waveFront = threadGroupIndex / 64;
	s_minMaxDepth[waveFront] = float2(minDepth, maxDepth);
	ThreadGroupMemoryBarrierSync();

	for (uint s = NUM_WAVEFRONTS/2; s > 0; s /= 2)
	{
		if (waveFront < s)
		{
			float2 neighbor = s_minMaxDepth[waveFront + s];
			float2 current = s_minMaxDepth[waveFront];
			s_minMaxDepth[waveFront] = float2(min(current.x, neighbor.x),
				max(current.y, neighbor.y));
		}
		ThreadGroupMemoryBarrierSync();
	}

	float2 current = s_minMaxDepth[0];
	minDepth = current.x;
	maxDepth = current.y;	
	#endif
}

void GenerateFrustumVertices(uint2 groupId, float minDepth, float maxDepth, float minDepthView,
							 float maxDepthView, CullSrt srt, out float3 verts[8],
							 out float3 bboxMin, out float3 bboxMax)
{
	// Generate the points in NDC near plane
	float2 tileSizeNDC = (TILE_SIZE * 2.f).xx / float2(srt.screenSize);
	tileSizeNDC.y *= -1.f;

	float2 topLeftNDC = float2(-1, 1) + tileSizeNDC * groupId;
	float2 bottomRightNDC = topLeftNDC + tileSizeNDC;

	// Start with clockwise calculation of the near plane points
	verts[0] = float3(topLeftNDC.xy, 0);
	verts[1] = float3(bottomRightNDC.x, topLeftNDC.y, 0);
	verts[2] = float3(bottomRightNDC.xy, 0);
	verts[3] = float3(topLeftNDC.x, bottomRightNDC.y, 0);
	verts[4] = float3(topLeftNDC.xy, 0);
	verts[5] = float3(bottomRightNDC.x, topLeftNDC.y, 0);
	verts[6] = float3(bottomRightNDC.xy, 0);
	verts[7] = float3(topLeftNDC.x, bottomRightNDC.y, 0);

	// Transfrom them from NDC to view space
	// TODO: Investigate saving float2s instead of float3 since we always know depth
	bboxMin = bboxMax = verts[0];
	for (uint i = 0; i < 8; ++i)
	{
		float z = i < 4? minDepth : maxDepth;

		float4 viewVert = mul(float4(verts[i].xy, z, 1.f), srt.screenToView); // TODO: Doesn't need to be matrix multiply. mostly diagonal
		float viewZ = i < 4? minDepthView : maxDepthView;

		verts[i] = (float3(viewVert.xy/viewVert.w, viewZ));
		bboxMin.xy = float2(min(bboxMin.x, verts[i].x), min(bboxMin.y, verts[i].y));
		bboxMax.xy = float2(max(bboxMax.x, verts[i].x), max(bboxMax.y, verts[i].y));
	}
	bboxMin.z = verts[0].z;
	bboxMax.z = verts[4].z;
}

void GenerateTileFrustum(uint2 groupId, float minDepth, float maxDepth, CullSrt srt, out float4 planes[6])
{
	// Get the VS position on the near plane of the tile top left corner
	float3 tileStartVS = srt.topLeftCornerVS + float3(groupId*srt.tileSizeVS, 0);
	float3 tileEndVS = tileStartVS + float3(srt.tileSizeVS, 0);

	planes[0] = (float4(normalize(cross(tileStartVS, float3(0,1,0))), 0));
	planes[1] = (float4(normalize(cross(tileEndVS, float3(0,-1,0))), 0));
	planes[2] = (float4(normalize(cross(tileStartVS, float3(-1,0,0))), 0));
	planes[3] = (float4(normalize(cross(tileEndVS, float3(1,0,0))), 0));
	// The compiler is smart enough to optimize all the unnecessary math by having 0 components
	// below. This simplifying the code and removing the need to specially handle the near and far planes
	planes[4] = float4(0, 0, 1, minDepth);
	planes[5] = float4(0, 0, -1, -maxDepth);
}

bool IntersectConeWithFrustum(const LightDesc light, const float3 verts[8], const float4 planes[6])
{
	const float3 coneTip = light.m_viewPos;
	const float3 coneAxis = light.m_viewDir;
	const float coneTheta = f16tof32(light.m_cosConeThetaAndRadius & 0xFFFF);
	const float coneThetaSqr = coneTheta * coneTheta;// todo precompute
	const bool disableFarPlaneCull = light.m_typeData & LIGHT_DISABLE_FAR_PLANE_CULL_BITS;
	
	// set height to be insanely large if we are disabling far plane culling
	const float height = disableFarPlaneCull ? 1.0e+38f : light.m_radius;

	float vertOrientations[8];

	bool allVertsInfront = true;
	bool allVertsBehind = true;

	// Check if any of the frustum vertices are inside the cone
	for (uint i = 0; i < 8; ++i)
	{
		float3 tipToVert = verts[i] - coneTip;
		float vertSide = dot(tipToVert, coneAxis);
		vertOrientations[i] = vertSide;
		// Only look at points on the cone side of the plane
		// and that are within the cone height
		if (vertSide >= 0)
		{
			allVertsBehind = false;
			if (vertSide <= height)
			{
				allVertsInfront = false;
				// Now test if the vertex is inside the cone or not
				float lenSqr = dot(tipToVert, tipToVert);
				if (vertSide*vertSide >= coneThetaSqr * lenSqr)
				{
					// The vertex is inside the cone
					return true;
				}
			}
		}
		else
		{
			allVertsInfront = false;
		}
	}

	// First early-exit we have - if all vertices are either behind the cone
	// or past the cone height
	if (allVertsInfront || allVertsBehind)
	{
		return false;
	}

	// None of the vertices are inside the cone. Now test edges
	// Start with the edges that go from near to far - they are more likely to hit the cone
	const uint2 edges[12] = {
		{0,4}, {1,5}, {2,6}, {3,7},
		{0,1}, {1,2}, {2,3}, {3,0},
		{4,5}, {5,6}, {6,7}, {7,4}
	};

	for (uint i = 0; i < 12; ++i)
	{
		const uint2 edge = edges[i];
		const float2 orients = float2(vertOrientations[edge.x], vertOrientations[edge.y]);
		const float3 pos[2] = { verts[edge.x], verts[edge.y] };
		// If both vertices are below the cone plane, then we don't need to consider it
		float orientSigns = orients.x * orients.y;

		if (orientSigns >= 0 && orients.x < 0)
		{
			// We can't intersect this edge.
			continue;
		}

		float3 PmV = pos[0] - coneTip;

		// TODO: There is a possible avenue of optimization here
		// a) don't normalize the edge vector E
		//		instead, define c2 = DdU*DdU - coneThetaSqr*dot(e,e)
		//		and test t within [0,1]. Only question remaining is the height clamping
		// b) Don't compute t directly - we don't care what it is - instead use inequalities

		float3 e = pos[1] - pos[0];
		float lenSeg = length(e);
		e /= lenSeg;

		float DdU = dot(coneAxis, e);
		float DdPmV = dot(coneAxis, PmV);
		float UdPmV = dot(e, PmV);
		float PmVdPmV = dot(PmV, PmV);

		float c2 = DdU * DdU - coneThetaSqr;
		float c1 = DdU * DdPmV - coneThetaSqr * UdPmV;
		float c0 = DdPmV * DdPmV - coneThetaSqr * PmVdPmV;

		if (c2 != 0)
		{
			float discr = c1 * c1 - c0 * c2;

			if (discr > 0)
			{
				float root = sqrt(discr);
				float invC2 = rcp(c2);

				float mu0 = -DdPmV / DdU;
				float mu1 = (height - DdPmV) / DdU;

				float h0 = min(mu0, mu1);
				float h1 = max(mu0, mu1);

				float t = (-c1 - root) * invC2;
				if (DdU * t + DdPmV >= 0 && t < lenSeg && t > 0 && t > h0 && t < h1)
				{
					return true;
				}

				t = (-c1 + root) * invC2;
				if (DdU * t + DdPmV >= 0 && t < lenSeg && t > 0 && t > h0 && t < h1)
				{
					return true;
				}
			}
		}
	}

	// Finally, as a last test, check if the cone intersects any of the frustum planes
	// It's pretty rare to reach this point - a tiny minority of frusta will miss
	// all vertices and all edges
	// This is more for robustness than anything

	for (uint i = 0; i < 6; ++i)
	{
		// First, test the ray from the cone against the plane
		float t = (planes[i].w - dot(planes[i].xyz, coneTip)) / dot(planes[i].xyz, coneAxis);

		if (t > 0 && t < height)
		{
			// Is this point in this plane?
			float3 p = coneTip + t * coneAxis;

			bool isInsideFrustum = true;
			for (uint j = 0; j < 6; ++j)
			{
				// Only test planes on the other coordinate axis
				if (j / 2 != i / 2)
				{
					if (dot(planes[j].xyz, p) < planes[j].w)
					{
						isInsideFrustum = false;
						break;
					}
				}
			}

			if (isInsideFrustum)
			{
				// We have a candidate point!
				return true;
			}
		}
	}

	return false;
}

bool IntersectConeNoLightAngleWithFrustum(const LightDesc light, const float3 verts[8], const float4 planes[6])
{
	const float3 coneTip = light.m_viewPos;
	const float3 coneAxis = light.m_viewDir;
	const float coneTheta = light.m_cosNoLightHalfAngleRadians;
	const float coneThetaSqr = coneTheta * coneTheta;// todo precompute
	const float height = light.m_radius;

	float vertOrientations[8];

	bool allVertsInfront = true;
	bool allVertsBehind = true;

	// Check if any of the frustum vertices are inside the cone
	for (uint i = 0; i < 8; ++i)
	{
		float3 tipToVert = verts[i] - coneTip;
		float vertSide = dot(tipToVert, coneAxis);
		vertOrientations[i] = vertSide;
		// Only look at points on the cone side of the plane
		// and that are within the cone height
		if (vertSide >= 0)
		{
			allVertsBehind = false;
			if (vertSide <= height)
			{
				allVertsInfront = false;
				
				// Now test if the vertex is inside the cone or not
				float lenSqr = dot(tipToVert, tipToVert);
				if (vertSide*vertSide < coneThetaSqr*lenSqr)
				{
					// The vertex is outside the cone
					return false;
				}
			}
		}
		else
		{
			allVertsInfront = false;
		}
	}

	// First early-exit we have - if all vertices are either behind the cone
	// or past the cone height
	if (allVertsInfront || allVertsBehind)
	{
		return false;
	}

	//return true;

	// None of the vertices are inside the cone. Now test edges
	// Start with the edges that go from near to far - they are more likely to hit the cone
	const uint2 edges[12] = {
		{0,4}, {1,5}, {2,6}, {3,7},
		{0,1}, {1,2}, {2,3}, {3,0},
		{4,5}, {5,6}, {6,7}, {7,4}
	};

	for (uint i = 0; i < 12; ++i)
	{
		const uint2 edge = edges[i];
		const float2 orients = float2(vertOrientations[edge.x], vertOrientations[edge.y]);
		const float3 pos[2] = { verts[edge.x], verts[edge.y] };
		// If both vertices are below the cone plane, then we don't need to consider it
		float orientSigns = orients.x * orients.y;

		if (orientSigns >= 0 && orients.x < 0)
		{
			// We can't intersect this edge.
			continue;
		}

		float3 PmV = pos[0] - coneTip;

		// TODO: There is a possible avenue of optimization here
		// a) don't normalize the edge vector E
		//		instead, define c2 = DdU*DdU - coneThetaSqr*dot(e,e)
		//		and test t within [0,1]. Only question remaining is the height clamping
		// b) Don't compute t directly - we don't care what it is - instead use inequalities

		float3 e = pos[1] - pos[0];
		float lenSeg = length(e);
		e /= lenSeg;

		float DdU = dot(coneAxis, e);
		float DdPmV = dot(coneAxis, PmV);
		float UdPmV = dot(e, PmV);
		float PmVdPmV = dot(PmV, PmV);

		float c2 = DdU * DdU - coneThetaSqr;
		float c1 = DdU * DdPmV - coneThetaSqr * UdPmV;
		float c0 = DdPmV * DdPmV - coneThetaSqr * PmVdPmV;

		if (c2 != 0)
		{
			float discr = c1 * c1 - c0 * c2;

			if (discr > 0)
			{
				float root = sqrt(discr);
				float invC2 = rcp(c2);

				float mu0 = -DdPmV / DdU;
				float mu1 = (height - DdPmV) / DdU;

				float h0 = min(mu0, mu1);
				float h1 = max(mu0, mu1);

				float t = (-c1 - root) * invC2;
				if (DdU * t + DdPmV >= 0 && t < lenSeg && t > 0 && t > h0 && t < h1)
				{
					return false;
				}

				t = (-c1 + root) * invC2;
				if (DdU * t + DdPmV >= 0 && t < lenSeg && t > 0 && t > h0 && t < h1)
				{
					return false;
				}
			}
		}
	}

	// Finally, as a last test, check if the cone intersects any of the frustum planes
	// It's pretty rare to reach this point - a tiny minority of frusta will miss
	// all vertices and all edges
	// This is more for robustness than anything

	//for (uint i = 0; i < 6; ++i)
	//{
	//	// First, test the ray from the cone against the plane
	//	float t = (planes[i].w - dot(planes[i].xyz, coneTip)) / dot(planes[i].xyz, coneAxis);

	//	if (t > 0 && t < height)
	//	{
	//		// Is this point in this plane?
	//		float3 p = coneTip + t * coneAxis;

	//		bool isInsideFrustum = true;
	//		for (uint j = 0; j < 6; ++j)
	//		{
	//			// Only test planes on the other coordinate axis
	//			if (j / 2 != i / 2)
	//			{
	//				if (dot(planes[j].xyz, p) < planes[j].w)
	//				{
	//					isInsideFrustum = false;
	//					break;
	//				}
	//			}
	//		}

	//		if (isInsideFrustum)
	//		{
	//			// We have a candidate point!
	//			return false;
	//		}
	//	}
	//}

	return true;
}

void CullLightsWithTile(float minDepthZ, float maxDepthZ, const uint2 groupId, const uint threadGroupIndex,
						const uint renderGroupIndex, const bool useNoLightAngle, CullSrt srt)
{
	uint numLights = srt.numLights;

	if (srt.disableNearPlaneCulling)
	{
		// In some cinematics, we have windows that don't write depth.
		// We still want runtime lights to affect them
		// So we just disable near-z culling. This hurts performance
		minDepthZ = 0;
	}
	
	// Convert depth to view space
	float minDepthView = srt.screenViewParameter.y / (minDepthZ - srt.screenViewParameter.x);
	float maxDepthView = srt.screenViewParameter.y / (maxDepthZ - srt.screenViewParameter.x);

	float3 verts[8];
	float3 bboxMin, bboxMax;
	GenerateFrustumVertices(groupId, minDepthZ, maxDepthZ, minDepthView, maxDepthView, srt, verts, bboxMin, bboxMax);

	float4 planes[6];
	GenerateTileFrustum(groupId, minDepthView, maxDepthView, srt, planes);

	if (threadGroupIndex == 0)
	{
		numTileVisibleLights[renderGroupIndex] = 0;
	}
	
	#if CULL_TILE_SIZE == 8
		ThreadGroupMemoryBarrier();
	#else
		ThreadGroupMemoryBarrierSync();
	#endif

	// Figure out how many iterations we need to do (given that we can do kTileFactor*kTileFactor lights in parallel)
	const uint NUM_TOTAL_INVOLVED_THREADS = kTileFactor * kTileFactor;

	const uint iterations = (numLights + NUM_TOTAL_INVOLVED_THREADS - 1) / NUM_TOTAL_INVOLVED_THREADS;
	const uint tileIndex = (groupId.y*(srt.screenSize.x/TILE_SIZE) + groupId.x);
	const uint tileLightsStart = tileIndex * MAX_LIGHTS_PER_TILE;

	// Do all spotlights first - the code is pretty complicated
	// and it allows us to reuse registers
	// TOOD: Since spotlights are so expensive to cull, move
	// spotlight culling in its own dispatch - this allows the rest
	// of the lights to be culled super quickly

	for (uint iteration = 0; iteration < iterations; ++iteration)
	{
		uint lightIndex = iteration*NUM_TOTAL_INVOLVED_THREADS + threadGroupIndex;

		if (lightIndex < numLights)
		{
			const LightDesc lightDesc = srt.lights[lightIndex];

			uint lightType = lightDesc.m_typeData & LIGHT_TYPE_BITS;

			bool intersects = false;

			if (lightType == kSpotLight || (lightType == kProjectorLight && !(lightDesc.m_typeData & LIGHT_IS_ORTHO_BITS)))
			{
				intersects = IntersectConeWithFrustum(lightDesc, verts, planes);

				// Certain lights, like the extra-light for fake bounce lighting, can use a "no light" inner cone where it does NOT light anything
				// If the frustum is COMPLETELY inside of the no-light-cone then we cull it
				if (useNoLightAngle && lightType == kSpotLight && (lightDesc.m_typeData & LIGHT_USE_NO_LIGHT_ANGLE_BITS))
				{
					if (IntersectConeNoLightAngleWithFrustum(lightDesc, verts, planes))
					{
						intersects = false;
					}
				}
			}

			// If intersects, append light to buffer
			if (intersects)
			{
				uint index;
				AtomicAdd(numTileVisibleLights[renderGroupIndex], 1, index);
				if (index < MAX_LIGHTS_PER_TILE)
				{
					srt.lightsPerTile[tileLightsStart + index] = lightIndex;
				}
				else
				{
					break;
				}
			}
		}
	}

	for (uint iteration = 0; iteration < iterations; ++iteration)
	{
		uint lightIndex = iteration*NUM_TOTAL_INVOLVED_THREADS + threadGroupIndex;

		if (lightIndex < numLights)
		{
			const LightDesc lightDesc = srt.lights[lightIndex];

			uint lightType = lightDesc.m_typeData & LIGHT_TYPE_BITS;
			uint isOrtho = lightDesc.m_typeData & LIGHT_IS_ORTHO_BITS;

			bool intersects = true;

			switch (lightType)
			{
			case kSpotLight:
				intersects = false;
				break;
			case kPointLight:
				intersects = FroxelSphereOverlap(bboxMin, bboxMax, planes, lightDesc.m_viewPos, lightDesc.m_radius);
				break;
			case kProjectorLight:
				{
					if (isOrtho)	// rectangular parallelepiped
					{
						for (uint i = 0; i < 6; ++i)
						{
							bool bAllOutside = true;
							for (uint j = 0; j < 8; ++j)
							{
								float distanceToPlane = dot(planes[i].xyz, lightDesc.m_verticesVS[j]) - planes[i].w;
								if (distanceToPlane > 0.f)
								{
									bAllOutside = false;
									break;
								}
							}
							if (bAllOutside)
							{
								intersects = false;
								break;
							}
						}
					}
					else
					{
						intersects = false;
					}
				} break;
			}
			
			// If intersects, append light to buffer
			if (intersects)
			{
				uint index;
				AtomicAdd(numTileVisibleLights[renderGroupIndex], 1, index);
				if (index < MAX_LIGHTS_PER_TILE)
				{
					srt.lightsPerTile[tileLightsStart + index] = lightIndex; // TODO: Only do this for tiles that need Forward+!
				}
				else
				{
					break;
				}
			}
		}
	}

	#if CULL_TILE_SIZE == 8
		ThreadGroupMemoryBarrier();
	#else
		ThreadGroupMemoryBarrierSync();
	#endif
	
	if (threadGroupIndex == 0)
	{
		srt.numLightsPerTile[tileIndex] = min(numTileVisibleLights[renderGroupIndex], MAX_LIGHTS_PER_TILE);
	}
}

struct ReduceSrt
{
	Texture2D<float> depth;
	Texture2D<float> opaquePlusAlphaDepth;
	RWTexture2D<float2> minMaxDepth;
};

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void CS_DepthMinMax(uint3 dispatchId : S_DISPATCH_THREAD_ID, uint3 groupId : S_GROUP_ID,
					uint threadGroupIndex : S_GROUP_INDEX, ReduceSrt *srt : S_SRT_DATA)
{
	const uint2 screenCoord = dispatchId.xy;

	float maxDepth = srt->depth.Load(int3(screenCoord, 0));
	float minDepth = srt->opaquePlusAlphaDepth.Load(int3(screenCoord, 0));

	// Get the min/max depth of the tile
	FindMinMaxDepth(threadGroupIndex, minDepth, maxDepth);

	float2 minMaxDepth = float2(minDepth, maxDepth);

	if (threadGroupIndex == 0)
	{
		srt->minMaxDepth[groupId.xy] = minMaxDepth;
	}
}

[numthreads(CULL_TILE_SIZE, CULL_TILE_SIZE, 1)]
void CS_DeferredLightCull(uint3 dispatchId : S_DISPATCH_THREAD_ID, uint3 groupId : S_GROUP_ID,
						  uint3 threadGroupId : S_GROUP_THREAD_ID, uint threadGroupIndex : S_GROUP_INDEX,
						  CullSrt *srt : S_SRT_DATA)
{
	// The ratio of culling tile to rendering tile
	// Currently, each tile group contains 4x4 rendering tiles
	// (TILE_SIZE/CULL_TILE_SIZE)*CULL_TILE_SPAN = 4

	const uint2 renderGroupLocalTileId = threadGroupId.xy / kTileFactor;
	const uint renderGroupLocalTileIndex = renderGroupLocalTileId.x + renderGroupLocalTileId.y * kRenderTilesPerTileDim;

	const uint2 renderGroupId = groupId.xy * kRenderTilesPerTileDim + renderGroupLocalTileId;
	
	// Index of each kTileFactorxkTileFactor group
	const uint2 renderGroupLocalThreadId = threadGroupId.xy % (kTileFactor);
	const uint renderGroupLocalThreadIndex = renderGroupLocalThreadId.x + renderGroupLocalThreadId.y * kTileFactor;

	float2 depthMinMax = srt->depthMinMax.Load(int3(renderGroupId, 0));

	CullLightsWithTile(depthMinMax.x, depthMinMax.y, renderGroupId, renderGroupLocalThreadIndex, renderGroupLocalTileIndex, /*useNoLightAngle*/ true, *srt);
}

[numthreads(CULL_TILE_SIZE, CULL_TILE_SIZE, 1)]
void CS_DeferredVolLightCull(uint3 dispatchId : S_DISPATCH_THREAD_ID, uint3 groupId : S_GROUP_ID,
							 uint3 threadGroupId : S_GROUP_THREAD_ID, uint threadGroupIndex : S_GROUP_INDEX,
							 CullSrt *srt : S_SRT_DATA)
{
	// The ratio of culling tile to rendering tile
	// Currently, each tile group contains 4x4 rendering tiles
	// (TILE_SIZE/CULL_TILE_SIZE)*CULL_TILE_SPAN = 4

	const uint2 renderGroupLocalTileId = threadGroupId.xy / kTileFactor;
	const uint renderGroupLocalTileIndex = renderGroupLocalTileId.x + renderGroupLocalTileId.y * kRenderTilesPerTileDim;

	const uint2 renderGroupId = groupId.xy * kRenderTilesPerTileDim + renderGroupLocalTileId;
	
	// Index of each kTileFactorxkTileFactor group
	const uint2 renderGroupLocalThreadId = threadGroupId.xy % (kTileFactor);
	const uint renderGroupLocalThreadIndex = renderGroupLocalThreadId.x + renderGroupLocalThreadId.y * kTileFactor;

	float2 depthMinMax = srt->depthMinMax.Load(int3(renderGroupId, 0));

	float maxDepth = depthMinMax.y;

#if DEFERRED_VOL_LIGHT_CULL_MIN_DEPTH
	maxDepth = max(maxDepth, srt->m_minDepth);
#endif

	CullLightsWithTile(0/*depthMinMax.x*/, maxDepth, renderGroupId, renderGroupLocalThreadIndex, renderGroupLocalTileIndex, /*useNoLightAngle*/ false, *srt);
}

[numthreads(CULL_TILE_SIZE, CULL_TILE_SIZE, 1)] // 8 x 8
void CS_DeferredVolLightCullNewVolumetrics(uint3 dispatchId : S_DISPATCH_THREAD_ID, uint3 groupId : S_GROUP_ID,
										   uint3 threadGroupId : S_GROUP_THREAD_ID, uint threadGroupIndex : S_GROUP_INDEX,
										   CullSrt *srt : S_SRT_DATA)
{
	// The ratio of culling tile to rendering tile
	// Currently, each tile group contains 4x4 rendering tiles
	// Each wavefront contains 2x2 rendering tiles
	// (TILE_SIZE/CULL_TILE_SIZE)*CULL_TILE_SPAN = 4

	const uint2 renderGroupLocalTileId = threadGroupId.xy / kTileFactor;
	const uint renderGroupLocalTileIndex = renderGroupLocalTileId.x + renderGroupLocalTileId.y * kRenderTilesPerTileDim;

	const uint2 renderGroupId = groupId.xy * kRenderTilesPerTileDim + renderGroupLocalTileId;

	// Index of each kTileFactorxkTileFactor group
	const uint2 renderGroupLocalThreadId = threadGroupId.xy % (kTileFactor);
	const uint renderGroupLocalThreadIndex = renderGroupLocalThreadId.x + renderGroupLocalThreadId.y * kTileFactor;

	float2 depthMinMax = srt->depthMinMax.Load(int3(renderGroupId, 0));

	// in case of new volumetrics and refresh behind geo, we sometimes want to get all volumetric lights even behind geo
	// CULL_TILE_SIZE
	// the logic works based off render frame number

	// m_frameNumber

	// 3 render groups

	//uint froxelGroupId 
	//uint xMod4 = groupId.x % 4;
	//uint yMod4 = groupId.y % 4;

	//uint groupIdMod16 = yMod4 * 4 + xMod4;
	
	// each group covers 

	uint2 pos = TILE_SIZE * renderGroupId; // we have 4 position for one tile. 
	// we have a tile that covers 16x16 area. we need to know whether this tile touches froxels that are updated behind geo

	uint frameMod16 = (srt->m_frameNumber % 16);

	uint targetFroxelXmod4 = frameMod16 % 4;
	uint targetFroxelYdiv4 = frameMod16 / 4;

#ifdef FogFroxelGridSizeNativeRes
	uint2 froxelStartIndex = pos / uint2(FogFroxelGridSizeNativeRes, FogFroxelGridSizeNativeRes);
	uint2 froxelEndIndex = (pos + uint2(TILE_SIZE - 1, TILE_SIZE - 1)) / (FogFroxelGridSizeNativeRes, FogFroxelGridSizeNativeRes);
	uint froxelX0Mod4 = (froxelStartIndex.x / 8) % 4;
	uint froxelY0Mod4 = ((froxelStartIndex.y / 8)) % 4;
#endif

#if FogFroxelGridSizeNativeRes == 12

	uint froxelX1Mod4 = ((froxelStartIndex.x + 1) / 8) % 4;
	uint froxelY1Mod4 = ((froxelStartIndex.y + 1) / 8) % 4;

	if ((froxelX0Mod4 == targetFroxelXmod4 || froxelX1Mod4 == targetFroxelXmod4) &&
		(froxelY0Mod4 == targetFroxelYdiv4 || froxelY1Mod4 == targetFroxelYdiv4))
#elif FogFroxelGridSizeNativeRes == 16
	if ((froxelX0Mod4 == targetFroxelXmod4 && froxelY0Mod4 == targetFroxelYdiv4))
#elif DeferredVolLightCullAllowAll
	if (true)
#else
	if (false)
#endif
	{
		// allow to see lights behind geo
		CullLightsWithTile(0/*depthMinMax.x*/, 1.0, renderGroupId, renderGroupLocalThreadIndex, renderGroupLocalTileIndex, /*useNoLightAngle*/ false, *srt);
	}
	else
	{
		CullLightsWithTile(0/*depthMinMax.x*/, depthMinMax.y, renderGroupId, renderGroupLocalThreadIndex, renderGroupLocalTileIndex, /*useNoLightAngle*/ false, *srt);
	}
}

[numthreads(CULL_TILE_SIZE, CULL_TILE_SIZE, 1)]
void CS_DeferredParticleLightCull(uint3 dispatchId : S_DISPATCH_THREAD_ID, uint3 groupId : S_GROUP_ID,
								  uint3 threadGroupId : S_GROUP_THREAD_ID, uint threadGroupIndex : S_GROUP_INDEX,
								  CullSrt *srt : S_SRT_DATA)
{
	// The ratio of culling tile to rendering tile
	// Currently, each tile group contains 4x4 rendering tiles
	// (TILE_SIZE/CULL_TILE_SIZE)*CULL_TILE_SPAN = 4

	const uint2 renderGroupLocalTileId = threadGroupId.xy / kTileFactor;
	const uint renderGroupLocalTileIndex = renderGroupLocalTileId.x + renderGroupLocalTileId.y * kRenderTilesPerTileDim;

	const uint2 renderGroupId = groupId.xy * kRenderTilesPerTileDim + renderGroupLocalTileId;
	
	// Index of each kTileFactorxkTileFactor group
	const uint2 renderGroupLocalThreadId = threadGroupId.xy % (kTileFactor);
	const uint renderGroupLocalThreadIndex = renderGroupLocalThreadId.x + renderGroupLocalThreadId.y * kTileFactor;

	float2 depthMinMax = srt->depthMinMax.Load(int3(renderGroupId, 0));

	CullLightsWithTile(0/*depthMinMax.x*/, depthMinMax.y, renderGroupId, renderGroupLocalThreadIndex, renderGroupLocalTileIndex, /*useNoLightAngle*/ false, *srt);
}
