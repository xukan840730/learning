#pragma pack_matrix(row_major)

#include "hlsl2pssl.fxi"
#include "deferred-include.fxi"
#include "clustered-lighting-utils.fxi"

/// --------------------------------------------------------------------------------------------------------------- ///
// 0 if the cone intersects (or is on the plane), > 0 if infront, < 1 if entirely behind. 
float ClassifyConeAgainstPlane(float3 coneTip, float3 coneEnd, float3 coneDir, float height, float radius, float4 plane)
{
	// Check if the ray from the cone tip intersects the plane.
	{
		float t = -dot(float4(coneTip, 1.0f), plane) / dot(coneDir, plane.xyz);
		if (t >= 0.0f && t <= height)
		{
			return 0.0f;
		}
	}

	// Find a ray that connects two points on the rim of the cone that are closeset and furthest from the plane.
	{
		float3 v0 = cross(plane.xyz, coneDir); // Vector perpendicular to the plane and the cone direction.
		float3 v = cross(v0, coneDir); // The direction vector from cone end to the closest (or furthest) point on the rim of the cone.

		// Intersect the ray with the plane.
		float t = -dot(float4(coneEnd + radius * v, 1.0f), plane) / dot(-v, plane.xyz);
		if (t >= 0.0f && t <= 2.0f * radius)
		{
			return 0.0f;
		}
	}

	// There was no intersection so see which side we're on.
	return dot(float4(coneTip, 1.0f), plane);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool DoesConeIntersectSlab(float3 coneTip, float3 coneDir, float height, float radius, float3 planeNormal, float near, float far)
{
	float4 farPlane = float4(planeNormal, -far);
	float4 nearPlane = float4(planeNormal, -near);

	float3 coneEnd = coneTip + coneDir * height;

	float cf = ClassifyConeAgainstPlane(coneTip, coneEnd, coneDir, height, radius, farPlane);
	if (cf > 0.0f)
	{
		return false;
	}

	float cn = ClassifyConeAgainstPlane(coneTip, coneEnd, coneDir, height, radius, nearPlane);
	return cn >= 0.0f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct FillZBinsSrt
{
	RegularBuffer<LightDesc> m_lights;
	RWBuffer<uint2> m_zBins;

	ClusteredLightingSettings m_settings;

	uint m_numZBins;
	uint m_numLights;
};

/// --------------------------------------------------------------------------------------------------------------- ///
bool IntersectLightWithSlab(const LightDesc light, float near, float far)
{
	float3 planeNormal = float3(0.0f, 0.0f, 1.0f);

	uint type = light.m_typeData & LIGHT_TYPE_BITS;
	if (type == kProjectorLight && (light.m_typeData & LIGHT_IS_ORTHO_BITS) == 0)
	{
		type = kSpotLight;
	}

	bool result = false;
	switch (type)
	{
	case kSpotLight:
		{
			float radius = f16tof32((light.m_cosConeThetaAndRadius >> 16) & 0xFFFF);
			result = DoesConeIntersectSlab(light.m_viewPos, light.m_viewDir, light.m_radius,
										   radius, planeNormal, near, far);
		}
		break;
	case kPointLight:
		{
			float dn = dot(planeNormal, light.m_viewPos) - near;
			float df = dot(planeNormal, light.m_viewPos) - far;
			result = dn >= -light.m_radius && df < light.m_radius;
		}
		break;
	case kProjectorLight:
		{
			// rectangular parallelepiped
			bool bAllOutside = true;
			for (uint i = 0; i < 8; i++)
			{
				float d = dot(planeNormal, light.m_verticesVS[i]);
				if (d >= near && d < far)
				{
					result = true;
					break;
				}
			}
		}
		break;
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
[numthreads(64, 1, 1)]
void CS_ClusteredLightCullFillZBins(uint dispatchId : S_DISPATCH_THREAD_ID, FillZBinsSrt *pSrt : S_SRT_DATA)
{
	float near = GetZBinNear(pSrt->m_settings, dispatchId);
	float far  = GetZBinNear(pSrt->m_settings, dispatchId + 1);

	uint minIndex = 0xffff;
	uint maxIndex = 0;

	for (uint i = 0; i < pSrt->m_numLights; i++)
	{
		if (IntersectLightWithSlab(pSrt->m_lights[i], near, far))
		{
			minIndex = min(minIndex, i);
			maxIndex = max(maxIndex, i);
		}
	}

	pSrt->m_zBins[dispatchId] = uint2(minIndex, maxIndex);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void GenerateFrustumVertices(uint2 dispatchId, float near, float far, float nearNdc, float farNdc,
							 uint2 screenSize, float4x4 screenToView, out float3 verts[8])
{
	// Generate the points in NDC near plane
	float2 tileSizeNdc = (TILE_SIZE * 2.0f).xx / float2(screenSize);
	tileSizeNdc.y *= -1.f;

	float2 topLeftNdc = float2(-1.0f, 1.0f) + tileSizeNdc * dispatchId;
	float2 bottomRightNdc = topLeftNdc + tileSizeNdc;

	// Start with clockwise calculation of the near plane points
	verts[0] = float3(topLeftNdc.xy, 0.0f);
	verts[1] = float3(bottomRightNdc.x, topLeftNdc.y, 0.0f);
	verts[2] = float3(bottomRightNdc.xy, 0.0f);
	verts[3] = float3(topLeftNdc.x, bottomRightNdc.y, 0.0f);
	verts[4] = float3(topLeftNdc.xy, 0.0f);
	verts[5] = float3(bottomRightNdc.x, topLeftNdc.y, 0.0f);
	verts[6] = float3(bottomRightNdc.xy, 0.0f);
	verts[7] = float3(topLeftNdc.x, bottomRightNdc.y, 0.0f);

	// Transfrom them from NDC to view space
	// TODO: Investigate saving float2s instead of float3 since we always know depth
	for (uint i = 0; i < 8; ++i)
	{
		float z = i < 4 ? nearNdc : farNdc;

		float4 viewVert = mul(float4(verts[i].xy, z, 1.0f), screenToView); // TODO: Doesn't need to be matrix multiply. mostly diagonal
		float viewZ = i < 4 ? near : far;

		verts[i] = float3(viewVert.xy / viewVert.w, viewZ);
	}
}

bool IntersectConeWithFrustum(const LightDesc light, const float3 verts[8], const float4 planes[6])
{
	const float3 coneTip = light.m_viewPos;
	const float3 coneAxis = light.m_viewDir;
	const float coneTheta = f16tof32(light.m_cosConeThetaAndRadius & 0xFFFF);
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
	const uint2 edges[12] =
	{
		{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
		{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
		{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 }
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

/// --------------------------------------------------------------------------------------------------------------- ///
struct FillTileBitsSrt
{
	Texture2D<float2> m_depthMinMax;

	RegularBuffer<LightDesc> m_lights;
	RWBuffer<uint> m_tileBits;

	ClusteredLightingSettings m_settings;

	float4x4 m_screenToView;
	uint2 m_screenSize;
	float2 m_screenViewParameter;

	uint2 m_numTiles;
	uint m_numLights;

	float3 m_topLeftCorner;
	float2 m_tileSize;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void ClusteredLightCullFillTileBits(uint2 dispatchId, float near, float far, float nearNdc, float farNdc, FillTileBitsSrt *pSrt)
{
	uint numBlocks = DivRoundUp(pSrt->m_numLights, kClusteredLightingZBinTileBlockSizeInBits);
	uint offset = dispatchId.y * pSrt->m_numTiles.x * numBlocks + dispatchId.x * numBlocks;

	// Get the VS position on the near plane of the tile top left corner
	float3 tileStart = pSrt->m_topLeftCorner + float3(dispatchId * pSrt->m_tileSize, 0.0f);
	float3 tileEnd = tileStart + float3(pSrt->m_tileSize, 0.0f);

	float4 planes[6] =
	{
		float4(normalize(cross(tileStart, float3( 0.0f,  1.0f, 0.0f))), 0.0f),
		float4(normalize(cross(tileEnd,   float3( 0.0f, -1.0f, 0.0f))), 0.0f),
		float4(normalize(cross(tileStart, float3(-1.0f,  0.0f, 0.0f))), 0.0f),
		float4(normalize(cross(tileEnd,   float3( 1.0f,  0.0f, 0.0f))), 0.0f),
		float4(0.0f, 0.0f,  1.0f,  near),
		float4(0.0f, 0.0f, -1.0f, -far),
	};

	float3 points[8];
	GenerateFrustumVertices(dispatchId, near, far, nearNdc, farNdc, pSrt->m_screenSize,
							pSrt->m_screenToView, points);

	for (uint b = 0; b < numBlocks; b++)
	{
		uint block = 0;
		uint start = kClusteredLightingZBinTileBlockSizeInBits * b;
		uint end = min(pSrt->m_numLights, kClusteredLightingZBinTileBlockSizeInBits * (b + 1));
		for (uint i = start; i < end; i++)
		{
			const LightDesc light = pSrt->m_lights[i];

			uint type = light.m_typeData & LIGHT_TYPE_BITS;
			if (type == kProjectorLight && (light.m_typeData & LIGHT_IS_ORTHO_BITS) == 0)
			{
				type = kSpotLight;
			}

			uint binBit = 1 << (i % kClusteredLightingZBinTileBlockSizeInBits);

			switch (type)
			{
			case kSpotLight:
				{
					if (!IntersectConeWithFrustum(light, points, planes))
					{
						binBit = 0;
					}

					// Certain lights, like the extra-light for fake bounce lighting, can use a "no light" inner cone where it does NOT light anything
					// If the frustum is COMPLETELY inside of the no-light-cone then we cull it
					if (light.m_typeData & LIGHT_USE_NO_LIGHT_ANGLE_BITS)
					{
						if (IntersectConeNoLightAngleWithFrustum(light, points, planes))
						{
							binBit = 0;
						}
					}
				}
				break;
			case kPointLight:
				{
					// Simple frustum sphere intersection
					for (uint p = 0; p < 6; p++)
					{
						float d = dot(planes[p].xyz, light.m_viewPos) - planes[p].w;
						if (d < -light.m_radius)
						{
							binBit = 0;
							break;
						}
					}
				}
				break;
			case kProjectorLight:
				{
					// rectangular parallelepiped
					for (uint p = 0; p < 6; p++)
					{
						bool bAllOutside = true;
						for (uint j = 0; j < 8; j++)
						{
							float d = dot(planes[p].xyz, light.m_verticesVS[j]) - planes[p].w;
							if (d > 0.0f)
							{
								bAllOutside = false;
								break;
							}
						}
						if (bAllOutside)
						{
							binBit = 0;
							break;
						}
					}
				}
				break;
			}

			block |= binBit;
		}

		pSrt->m_tileBits[offset + b] = block;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
[numthreads(8, 8, 1)]
void CS_ClusteredLightCullFillTileBits(uint2 dispatchId : S_DISPATCH_THREAD_ID, FillTileBitsSrt *pSrt : S_SRT_DATA)
{
	ClusteredLightCullFillTileBits(dispatchId, pSrt->m_settings.m_near, pSrt->m_settings.m_far, 0.0f, 1.0f, pSrt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
[numthreads(8, 8, 1)]
void CS_ClusteredLightCullFillTileBitsDepthMinMax(uint2 dispatchId : S_DISPATCH_THREAD_ID, FillTileBitsSrt *pSrt : S_SRT_DATA)
{
	float2 depthMinMax = pSrt->m_depthMinMax.Load(int3(dispatchId, 0));

	// Convert depth to view space
	float near = pSrt->m_screenViewParameter.y / (depthMinMax.x - pSrt->m_screenViewParameter.x);
	float far = pSrt->m_screenViewParameter.y / (depthMinMax.y - pSrt->m_screenViewParameter.x);

	ClusteredLightCullFillTileBits(dispatchId, near, far, depthMinMax.x, depthMinMax.y, pSrt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct FillTileLightListsSrt
{
	Texture2D<float2> m_depthMinMax;

	RegularBuffer<LightDesc> m_lights;
	RWBuffer<uint> m_lightLists;
	RWTexture3D<uint> m_lightCounts;

	ClusteredLightingSettings m_settings;

	float4x4 m_screenToView;
	uint2 m_screenSize;
	float2 m_screenViewParameter;

	uint2 m_numTiles;
	uint m_numLights;

	float3 m_topLeftCorner;
	float2 m_tileSize;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void ClusteredLightCullFillLightLists(uint3 dispatchId, float near, float far, float nearNdc, float farNdc, FillTileLightListsSrt *pSrt)
{
	// Get the VS position on the near plane of the tile top left corner
	float3 tileStart = pSrt->m_topLeftCorner + float3(dispatchId.xy * pSrt->m_tileSize, 0.0f);
	float3 tileEnd = tileStart + float3(pSrt->m_tileSize, 0.0f);

	float clusterNear = GetClusterNear(pSrt->m_settings, dispatchId.z);
	float clusterFar = GetClusterNear(pSrt->m_settings, dispatchId.z + 1);

	near = max(near, clusterNear);
	far = min(far, clusterFar);

	uint count = 0;
	if (near < far)
	{
		float4 planes[6] =
		{
			float4(normalize(cross(tileStart, float3( 0.0f,  1.0f, 0.0f))), 0.0f),
			float4(normalize(cross(tileEnd,   float3( 0.0f, -1.0f, 0.0f))), 0.0f),
			float4(normalize(cross(tileStart, float3(-1.0f,  0.0f, 0.0f))), 0.0f),
			float4(normalize(cross(tileEnd,   float3( 1.0f,  0.0f, 0.0f))), 0.0f),
			float4(0.0f, 0.0f,  1.0f,  near),
			float4(0.0f, 0.0f, -1.0f, -far),
		};

		float3 points[8];
		GenerateFrustumVertices(dispatchId, near, far, nearNdc, farNdc, pSrt->m_screenSize,
								pSrt->m_screenToView, points);

		bool intersects = true;
		for (uint i = 0; i < pSrt->m_numLights; i++)
		{
			const LightDesc light = pSrt->m_lights[i];

			uint type = light.m_typeData & LIGHT_TYPE_BITS;
			if (type == kProjectorLight && (light.m_typeData & LIGHT_IS_ORTHO_BITS) == 0)
			{
				type = kSpotLight;
			}

			uint binBit = 1 << (i % kClusteredLightingZBinTileBlockSizeInBits);

			switch (type)
			{
			case kSpotLight:
			{
				intersects = IntersectConeWithFrustum(light, points, planes);
				
				// Certain lights, like the extra-light for fake bounce lighting, can use a "no light" inner cone where it does NOT light anything
				// If the frustum is COMPLETELY inside of the no-light-cone then we cull it
				if (light.m_typeData & LIGHT_USE_NO_LIGHT_ANGLE_BITS)
				{
					if (IntersectConeNoLightAngleWithFrustum(light, points, planes))
					{
						intersects = false;
					}
				}
			}
			break;
			case kPointLight:
			{
				// Simple frustum sphere intersection
				for (uint p = 0; p < 6; p++)
				{
					float d = dot(planes[p].xyz, light.m_viewPos) - planes[p].w;
					if (d < -light.m_radius)
					{
						intersects = false;
						break;
					}
				}
			}
			break;
			case kProjectorLight:
			{
				// rectangular parallelepiped
				for (uint p = 0; p < 6; p++)
				{
					for (uint j = 0; j < 8; j++)
					{
						float d = dot(planes[p].xyz, light.m_verticesVS[j]) - planes[p].w;
						if (d > 0.0f)
						{
							intersects = false;
							break;
						}
					}
					if (!intersects)
					{
						break;
					}
				}
			}
			break;
			}

			if (intersects && count < MAX_LIGHTS_PER_TILE)
			{
				uint offset = pSrt->m_numTiles[0] * pSrt->m_numTiles[1] * MAX_LIGHTS_PER_TILE * dispatchId.z + // Slice
							  pSrt->m_numTiles[0] * MAX_LIGHTS_PER_TILE * dispatchId.y + // Row
							  MAX_LIGHTS_PER_TILE * dispatchId.x; // Column
				pSrt->m_lightLists[offset + count] = i;
				count += 1;
			}
		}
	}

	pSrt->m_lightCounts[dispatchId] = count;
}

/// --------------------------------------------------------------------------------------------------------------- ///
[numthreads(4, 4, 4)]
void CS_ClusteredLightCullFillLightLists(uint3 dispatchId : S_DISPATCH_THREAD_ID, FillTileLightListsSrt *pSrt : S_SRT_DATA)
{
	ClusteredLightCullFillLightLists(dispatchId, pSrt->m_settings.m_near, pSrt->m_settings.m_far, 0.0f, 1.0f, pSrt);
}

/// --------------------------------------------------------------------------------------------------------------- ///
[numthreads(4, 4, 4)]
void CS_ClusteredLightCullFillLightListsDepthMinMax(uint3 dispatchId : S_DISPATCH_THREAD_ID, FillTileLightListsSrt *pSrt : S_SRT_DATA)
{
	float2 depthMinMax = pSrt->m_depthMinMax.Load(int3(dispatchId.xy, 0));

	// Convert depth to view space
	float near = pSrt->m_screenViewParameter.y / (depthMinMax.x - pSrt->m_screenViewParameter.x);
	float far = pSrt->m_screenViewParameter.y / (depthMinMax.y - pSrt->m_screenViewParameter.x);

	ClusteredLightCullFillLightLists(dispatchId, near, far, depthMinMax.x, depthMinMax.y, pSrt);
}
