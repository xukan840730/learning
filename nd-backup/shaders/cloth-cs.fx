#include "profile.fxi"

// Divide where divide by zero returns 0 instead of inf
//#define SAFE_DIVIDE(a,b) (((b) == 0.0f) ? (0.0f*a) : (a)/(b))
#define SAFE_DIVIDE(a,b) ((a)*__v_rcp_legacy_f32(b))

#ifndef _HLSL2PSSL_H_ 
// row_major is having problems compiling on Win, so hide it under Windows until we no longer support it
#define row_major
#endif

#pragma argument(scheduler=minpressure)

groupshared float g_ldsBuf[THREAD_COUNT];
groupshared uint g_subframeIndex;

#ifndef CLOTH_ENABLE_STATS
#define CLOTH_ENABLE_STATS
#endif

#ifdef CLOTH_ENABLE_STATS
groupshared float g_statsSolverLoops;
groupshared float g_statsTickStartMain;
groupshared float g_statsTickStartCol;
groupshared float g_statsTicksTotalCol;
#endif


#if THREAD_COUNT == 64
#define ClothSync()
#else
#define ClothSync() ThreadGroupMemoryBarrierSync()
#endif

struct ClothConstParams
{
	// Must match ClothConstParams in cloth-body.h
	uint m_numNodes;
	uint m_numNodeThreadgroups;
	uint m_numNodeThreads;

	uint m_numLinks;
	uint m_numLinkThreadgroups;
	uint m_numLinkThreads;

	uint m_numFaces;
	uint m_numFaceThreadgroups;
	uint m_numFaceThreads;

	uint m_numAltPoints;
	uint m_numAltPointThreadgroups;
	uint m_numAltPointThreads;

	uint m_numProtoSkinningMats;
	uint m_numProtoSkinningMatThreadgroups;
	uint m_numProtoSkinningMatThreads;

	uint m_numTotalSkinningMats;
	uint m_numTotalSkinningMatThreadgroups;
	uint m_numTotalSkinningMatThreads;

	uint m_numOutputJoints;
	uint m_numOutputJointThreadgroups;
	uint m_numOutputJointThreads;

	uint m_numFacePairs;

	uint m_numLinkGroups;
	uint m_numFacePairGroups;
	uint m_maxSkinningWeights;

	uint m_numFacePairForces;
};

struct ClothFrameParams
{
	// Must match ClothFrameParams in cloth-body.h
	row_major float4x4 m_relativeXform;
	row_major float4x4 m_modelToClothSpaceXform;
	row_major float4x4 m_prevModelToClothSpaceXform;
	row_major float4x4 m_clothToModelSpaceXform;

	float4	m_deltaTime;
	float4	m_subFrameDeltaTime;
	float4	m_singleStiffnessDeltaTime;

	float4	m_airResist;

	float4	m_charCollFriction;
	float4	m_charCollFrictionMax;
	float4	m_charCollFlypaper;

	float4	m_tau;
	float4	m_omega;
	float4	m_collOmega;

	float4	m_unstretchLimit;

	float4	m_stiffnessMul;
	float4	m_damping;
	float4	m_bendDamping;
	float4	m_bendStiffness;
	float4	m_bendStiffnessFilter;
	float4	m_conjGradResidual;

	float4	m_reduceVel;

	float4	m_gravity;

	float4	m_windVec;
	float4	m_windRandom;
	float4	m_windAirDensity;

	float4	m_skinFloor;
	float4	m_skinFilter;
	float4	m_skinBias;

	float4	m_skinMoveDistMin;
	float4	m_skinMoveDistMax;

	float4	m_disableCompressPct;

	float4	m_disableSimulationPct;

	uint	m_numSubframes;

	uint	m_numOuterIters;
	uint	m_numInnerIters;
	uint	m_numUnstretchIters;
	uint	m_conjGradIters;

	uint	m_colliderNumEdges;
	uint	m_colliderNumPlanes;

	uint	m_resetVelThisFrame;
	uint	m_resetVelLastFrame;
	uint	m_framesSinceTeleport;

	uint	m_longRangeUnstretch;
	uint	m_clothRecomputeLinkLengthsShortestPartial;
	uint	m_clothRecomputeLinkLengthsShortest;
	uint	m_clothRecomputeLinkLengthsPartial;
	uint	m_clothRecomputeLinkLengthsFull;
	uint	m_forceReboot;

	uint	m_enableSkinning;
	uint	m_enableSkinMoveDist;
	uint	m_disableBendingForces;
	uint	m_disableCollision;

	int		m_forceSkinJoint;
	float	m_forceSkinPct;
};



struct ClothStats
{
	float	m_congGradResidualItr[4];
	float	m_unstretchResidualItr[4];
	float	m_congGradResidual;
	float	m_unstretchResidual;
	uint	m_ticksTotal;
	uint	m_ticksCollide;
	uint	m_solverLoops;
	uint	m_didReboot;
};

struct ClothGroupInfo
{
	// Must match ClothGroupInfo in cloth-body.h
	uint m_linkGroupStart;
	uint m_linkGroupSize;
};

struct ClothLongRangeConstraint
{
	// Must match ClothLongRangeConstraint in cloth-collider.h
	uint m_node;
	float m_dist;
};

struct ClothColliderEdge
{
	// Must match ClothCollider::Edge in cloth-collider.h
	float m_radiusA;
	float m_radiusB;
	int m_vertA;
	int m_vertB;
};

struct ClothNodeInfo
{
	// Must match ClothNodeInfo in cloth-body.h
	uint m_offset;
	uint m_count;
};

struct ClothFace
{
	// Must match ClothPrototype::Face in cloth-prototype.h
	uint m_faceN0;
	uint m_faceN1;
	uint m_faceN2;
};

struct ClothFaceTangentWeights
{
	// Must match ClothPrototype::FaceTangentWeights in cloth-prototype.h
	float m_rua0;
	float m_rub0;
	float m_rva0;
	float m_rvb0;

	float m_rua1;
	float m_rub1;
	float m_rva1;
	float m_rvb1;

	float m_rua2;
	float m_rub2;
	float m_rva2;
	float m_rvb2;

	float m_uv00;
	float m_uv01;
	float m_uv10;
	float m_uv11;
	float m_uv20;
	float m_uv21;

	uint m_altNode0;
	uint m_altNode1;
	uint m_altNode2;
};

struct ClothPrototypeLink
{
	// Must match ClothPrototype::Link in cloth-prototype.h
	uint m_p0;
	uint m_p1;
	float m_length;
	float m_spring;
	float m_lock;
};

struct ClothPrototypeFacePair
{
	// Must match ClothPrototype::FacePair in cloth-prototype.h
	uint m_edgeNode0;
	uint m_edgeNode1;
	uint m_oppNode0;
	uint m_oppNode1;
	float m_alphaA;
	float m_alphaB;
	float m_alphaC;
	float m_alphaD;
	float m_lambdaPre;
	float m_halfYotta0;
};

struct ClothNodeFacePairOffsets
{
	// Must match ClothBody::ClothNodeFacePairOffsets in cloth-body.h
	uint m_offset;
	uint m_count;
};

struct ClothFacePairNodeOffsets
{
	// Must match ClothBody::ClothFacePairNodeOffsets in cloth-body.h
	uint m_edgeNodeOffsets0;
	uint m_edgeNodeOffsets1;
	uint m_oppNodeOffsets0;
	uint m_oppNodeOffsets1;
};

struct ClothOutputJoint
{
	// Must match ClothPrototype::OutputJoint in cloth-prototype.h
	uint m_jointInd;
	uint m_boundFace;

	float m_u;
	float m_v;

	float m_rux;
	float m_rvx;
	float m_rnx;

	float m_ruy;
	float m_rvy;
	float m_rny;

	float m_ruz;
	float m_rvz;
	float m_rnz;
};

struct ClothOutputJointsWork
{
	float4 m_normal;
	float4 m_tangentU;
	float4 m_tangentV;
};

struct ClothPlaneData
{
	float4 m_point;
	float4 m_normal;
};


struct SrtConstData
{
	ClothConstParams* m_pConst;
	ClothFrameParams* m_pFrame;
};

struct SrtBuffers
{
	StructuredBuffer<float>						m_pProtoInvMass;			// m_numNodeThreads
	StructuredBuffer<float>						m_pProtoClothSkinPct;		// m_numNodeThreads
	StructuredBuffer<float>						m_pProtoClothSkinMoveDist;	// m_numNodeThreads
	StructuredBuffer<float4>					m_pProtoInitialPos;			// m_numNodeThreads
	StructuredBuffer<float>						m_pProtoSkinWeights;		// 12*m_numNodeThreads
	StructuredBuffer<uint>						m_pProtoSkinOffsets;		// 12*m_numNodeThreads
	StructuredBuffer<float4>					m_pColliderPosOld;
	StructuredBuffer<float4>					m_pColliderPosNew;
	StructuredBuffer<ClothColliderEdge>			m_pColliderEdge;
	StructuredBuffer<ClothPlaneData>			m_pColliderPlaneOld;
	StructuredBuffer<ClothPlaneData>			m_pColliderPlaneNew;
	StructuredBuffer<ClothGroupInfo>			m_pProtoLinkGroupData;		// m_numLinkGroups
	StructuredBuffer<int>						m_pProtoLinkGroups;			// m_numLinks
	StructuredBuffer<ClothNodeInfo>				m_pProtoNodeLinkInfo;		// m_numNodeThreads
	StructuredBuffer<int>						m_pProtoNodeLinks;			// 2*m_numLinks
	StructuredBuffer<ClothPrototypeLink>		m_pProtoLink;				// m_numLinks
	StructuredBuffer<ClothLongRangeConstraint>	m_pLrcLink;					// m_numNodeThreads
	StructuredBuffer<ClothNodeInfo>				m_pProtoNodeFaceInfo;		// m_numAltPointThreads
	StructuredBuffer<uint>						m_pProtoNodeFaceLists;		// 3*numFaces
	StructuredBuffer<ClothFace>					m_pProtoFaces;				// numFaces
	StructuredBuffer<ClothFaceTangentWeights>	m_pProtoFaceTangentWeights;	// numFaces
	StructuredBuffer<ClothOutputJoint>			m_pProtoOutputJoints;		// numOutputJointThreads
	StructuredBuffer<row_major float3x4>		m_pSkelInvBindPoses;		// pJointCache->GetNumTotalJoints()
	//StructuredBuffer<ClothGroupInfo>			m_pProtoFacePairGroupData;	// m_numFacePairGroups
	//StructuredBuffer<int>						m_pProtoFacePairGroups;		// m_numFacePairs
	StructuredBuffer<ClothPrototypeFacePair>	m_pProtoFacePairs;			// m_numFacePairs
	StructuredBuffer<ClothNodeFacePairOffsets>	m_pProtoNodeFacePairOffsets;// m_numNodes
	StructuredBuffer<ClothFacePairNodeOffsets>	m_pProtoFacePairNodeOffsets;// m_numFacePairs
};

struct SrtRwBuffers
{
	RWStructuredBuffer<float4>					m_pBodyPos;					// m_numNodeThreads
	RWStructuredBuffer<float4>					m_pBodyVel;					// m_numNodeThreads
	RWStructuredBuffer<float4>					m_pBodyVelWsInv;			// m_numNodeThreads
	RWStructuredBuffer<float>					m_pBodySkinMats;			// 12*m_numTotalSkinningMats
	RWStructuredBuffer<ClothStats>				m_pClothStats;				// 4
	RWStructuredBuffer<float4>					m_pBodySkinPos;				// m_numNodeThreads
	RWStructuredBuffer<float4>					m_pBodySkinPosPrev;			// m_numNodeThreads
	RWStructuredBuffer<ClothOutputJointsWork>	m_pOutputJointsWork;		// m_numNodeThreads
	RWStructuredBuffer<float3>					m_pBkwdEulerWorkX;			// m_numNodes
	RWStructuredBuffer<float3>					m_pBkwdEulerWorkR;			// m_numNodes
	RWStructuredBuffer<float3>					m_pBkwdEulerWorkT;			// m_numNodes
	RWStructuredBuffer<float3>					m_pBkwdEulerWorkHT;			// m_numNodes
	RWStructuredBuffer<float4>					m_pScratch;					// m_numNodeThreads
	RWStructuredBuffer<float>					m_pBodyLinkLengths;			// m_numLinks
	RWStructuredBuffer<float4>					m_pBodyFacePairForces;		// m_numFacePairForces
	RWStructuredBuffer<uint>					m_pDebug;
};

struct SrtData
{
	SrtConstData*	m_pConsts;
	SrtBuffers*		m_pBuffs;
	SrtRwBuffers*	m_pRwBuffs;
};

struct SrtCopyData
{
	ClothConstParams*	m_pConst;
	SrtRwBuffers*		m_pRwBuffsSrc;
	SrtRwBuffers*		m_pRwBuffsDst;
};

[isolate]
void ClothAddLocalSpaceVelocity(SrtData srt, uint dispatchThreadId)
{
	if (dispatchThreadId >= srt.m_pConsts->m_pConst->m_numNodes)
		return;

	const row_major float1x4 posLocal = {srt.m_pRwBuffs->m_pBodyPos[dispatchThreadId].xyz, 1.0f};
	const row_major float1x4 posLocalNew = mul(posLocal, srt.m_pConsts->m_pFrame->m_relativeXform);

	srt.m_pRwBuffs->m_pBodyVelWsInv[dispatchThreadId].xyz = (posLocalNew - posLocal)._m00_m01_m02/srt.m_pConsts->m_pFrame->m_deltaTime.x;
	srt.m_pRwBuffs->m_pBodyPos[dispatchThreadId].xyz = posLocalNew._m00_m01_m02;
}

[isolate]
void ClothSetLocalSpaceVelocity(SrtData srt, uint dispatchThreadId)
{
	if (dispatchThreadId >= srt.m_pConsts->m_pConst->m_numNodes)
		return;

	const row_major float1x4 posLocal = {srt.m_pRwBuffs->m_pBodyPos[dispatchThreadId].xyz, 1.0f};
	const row_major float1x4 posLocalNew = mul(posLocal, srt.m_pConsts->m_pFrame->m_relativeXform);
		
	const row_major float1x4 addVel = ( posLocal - posLocalNew ) / srt.m_pConsts->m_pFrame->m_deltaTime.x;
		
	srt.m_pRwBuffs->m_pBodyPos[dispatchThreadId].xyz = posLocalNew._m00_m01_m02;
	srt.m_pRwBuffs->m_pBodyVel[dispatchThreadId].xyz += addVel._m00_m01_m02;
}

[isolate]
void ClothApplyWindForce(SrtData srt, uint dispatchThreadId)
{
	if (dispatchThreadId >= srt.m_pConsts->m_pConst->m_numAltPoints)
		return;

	const uint faceListOffset = srt.m_pBuffs->m_pProtoNodeFaceInfo[dispatchThreadId].m_offset;
	const uint faceListCount = srt.m_pBuffs->m_pProtoNodeFaceInfo[dispatchThreadId].m_count;

	for(uint ii=0; ii<faceListCount; ++ii)
	{
		const uint faceIndex = srt.m_pBuffs->m_pProtoNodeFaceLists[faceListOffset + ii];

		const uint n0 = srt.m_pBuffs->m_pProtoFaces[faceIndex].m_faceN0;
		const uint n1 = srt.m_pBuffs->m_pProtoFaces[faceIndex].m_faceN1;
		const uint n2 = srt.m_pBuffs->m_pProtoFaces[faceIndex].m_faceN2;

		const float3 p0 = srt.m_pRwBuffs->m_pBodyPos[n0].xyz;
		const float3 p1 = srt.m_pRwBuffs->m_pBodyPos[n1].xyz;
		const float3 p2 = srt.m_pRwBuffs->m_pBodyPos[n2].xyz;

		const float3 v0 = srt.m_pRwBuffs->m_pBodyVel[n0].xyz;
		const float3 v1 = srt.m_pRwBuffs->m_pBodyVel[n1].xyz;
		const float3 v2 = srt.m_pRwBuffs->m_pBodyVel[n2].xyz;

		const float3 centerVel = 0.3333333f * (v0 + v1 + v2);
		const float3 localWind = srt.m_pConsts->m_pFrame->m_windVec.xyz - centerVel * srt.m_pConsts->m_pFrame->m_windAirDensity.x;

		const float3 e01 = p1-p0;
		const float3 e02 = p2-p0;

		// note: intentionally NOT normalizing the normal here
		//       as this way force is proportional to the area of the triangle
		//       multiply by 10000 to scale by square cm instead of square meters, keeping the numbers more reasonable
		const float3 crossE = cross(e01, e02);
		const float surfaceForce = dot(crossE, localWind) * 10000.0f * 0.2f * (1.0f + srt.m_pConsts->m_pFrame->m_damping.x);

		// const Vector finalForce = Abs(surfaceForce) * localForce * Normalize(crossE) * Sign(surfaceForce);
		// That abs() and sign() cancel each other out - thus force is always in the direction of the wind
		const float3 finalForce = surfaceForce * normalize(crossE);

		const float3 impulse = finalForce * srt.m_pConsts->m_pFrame->m_singleStiffnessDeltaTime.x;

		srt.m_pRwBuffs->m_pBodyVel[dispatchThreadId].xyz += impulse * srt.m_pBuffs->m_pProtoInvMass[dispatchThreadId];
	}
}

[isolate]
void ClothApplyAirResistance(SrtData srt, uint dispatchThreadId)
{
	if (dispatchThreadId >= srt.m_pConsts->m_pConst->m_numAltPoints)
		return;

	const uint faceListOffset = srt.m_pBuffs->m_pProtoNodeFaceInfo[dispatchThreadId].m_offset;
	const uint faceListCount = srt.m_pBuffs->m_pProtoNodeFaceInfo[dispatchThreadId].m_count;

	float3 normal = {0,0,0};
	for(uint ii=0; ii<faceListCount; ++ii)
	{
		const uint faceIndex = srt.m_pBuffs->m_pProtoNodeFaceLists[faceListOffset + ii];

		const uint n0 = srt.m_pBuffs->m_pProtoFaces[faceIndex].m_faceN0;
		const uint n1 = srt.m_pBuffs->m_pProtoFaces[faceIndex].m_faceN1;
		const uint n2 = srt.m_pBuffs->m_pProtoFaces[faceIndex].m_faceN2;

		const float3 p0 = srt.m_pRwBuffs->m_pBodyPos[n0].xyz;
		const float3 p1 = srt.m_pRwBuffs->m_pBodyPos[n1].xyz;
		const float3 p2 = srt.m_pRwBuffs->m_pBodyPos[n2].xyz;

		normal += cross(p1 - p0, p2 - p0);
	}

	normal = normalize(normal);

	if (dispatchThreadId < srt.m_pConsts->m_pConst->m_numNodes)
	{
		const float3 vel = srt.m_pRwBuffs->m_pBodyVel[dispatchThreadId].xyz;
		const float dampForce = -dot(vel, normal) * srt.m_pConsts->m_pFrame->m_airResist.x;
		const float3 dampImpulse = dampForce * normal * srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;

		srt.m_pRwBuffs->m_pBodyVel[dispatchThreadId].xyz = vel + dampImpulse;
	}
}

void ClothUnstretchReduce(SrtData srt, uint dispatchThreadId)
{
	uint offset = THREAD_COUNT/2;
	while (offset > 32)
	{
		if (dispatchThreadId < offset)
			g_ldsBuf[dispatchThreadId] += g_ldsBuf[dispatchThreadId + offset];

		ClothSync();

		offset >>= 1;
	}

	if (dispatchThreadId < 64)
	{
		float sum = g_ldsBuf[dispatchThreadId];

		sum += LaneSwizzle(sum, 0x1f, 0x00, 0x10);
		sum += LaneSwizzle(sum, 0x1f, 0x00, 0x08);
		sum += LaneSwizzle(sum, 0x1f, 0x00, 0x04);
		sum += LaneSwizzle(sum, 0x1f, 0x00, 0x02);
		sum += LaneSwizzle(sum, 0x1f, 0x00, 0x01);

		g_ldsBuf[0] = ReadLane(sum, 0) + ReadLane(sum, 32);
	}
	
	ClothSync();
}

[isolate]
void ClothUnstretch(SrtData srt, uint dispatchThreadId)
{
	PROFILE_MARKER_START(PROFILE_MARKER_YELLOW, dispatchThreadId);

	g_ldsBuf[dispatchThreadId] = 0.0f;

	float residual = 0.0f;
	for (uint unstretchIndex=0; unstretchIndex<srt.m_pConsts->m_pFrame->m_numUnstretchIters; unstretchIndex++)
	{
		if (dispatchThreadId < srt.m_pConsts->m_pConst->m_numNodes)
		{
			srt.m_pRwBuffs->m_pScratch[dispatchThreadId] = float4(0,0,0,0);
		}
		ClothSync();

		for (uint groupIndex=0; groupIndex<srt.m_pConsts->m_pConst->m_numLinkGroups; groupIndex++)
		{
			const int linkStartOffset = srt.m_pBuffs->m_pProtoLinkGroupData[groupIndex].m_linkGroupStart;
			const int linkGroupSize = srt.m_pBuffs->m_pProtoLinkGroupData[groupIndex].m_linkGroupSize;

			if (dispatchThreadId < linkGroupSize)
			{
				uint linkIndex = srt.m_pBuffs->m_pProtoLinkGroups[linkStartOffset + dispatchThreadId];

				const uint n0 = srt.m_pBuffs->m_pProtoLink[linkIndex].m_p0;
				const uint n1 = srt.m_pBuffs->m_pProtoLink[linkIndex].m_p1;

				const float3 p0 = srt.m_pRwBuffs->m_pBodyPos[n0].xyz;
				const float3 p1 = srt.m_pRwBuffs->m_pBodyPos[n1].xyz;

				const float3 s0 = srt.m_pRwBuffs->m_pScratch[n0].xyz;
				const float3 s1 = srt.m_pRwBuffs->m_pScratch[n1].xyz;

				const float3 v0 = srt.m_pRwBuffs->m_pBodyVel[n0].xyz + s0;
				const float3 v1 = srt.m_pRwBuffs->m_pBodyVel[n1].xyz + s1;

				const float3 nextP0 = p0 + srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x * v0;
				const float3 nextP1 = p1 + srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x * v1;

				const float3 nextDiff = nextP1 - nextP0;
				const float nextLength = length(nextDiff);
				const float3 normDiff = SAFE_DIVIDE(nextDiff,  nextLength);

				const float restLength = srt.m_pRwBuffs->m_pBodyLinkLengths[linkIndex];
				const float tauLength = srt.m_pConsts->m_pFrame->m_tau.x * srt.m_pBuffs->m_pProtoLink[linkIndex].m_lock * restLength;

				const float rawStretch = nextLength - restLength;
				const float scaledStretch = (rawStretch >= 0.0f) ? rawStretch : srt.m_pConsts->m_pFrame->m_disableCompressPct.x*rawStretch;

				const float stretch = max(0.0f, abs(scaledStretch) - tauLength) * sign(scaledStretch);

				const float skin0 = srt.m_pBuffs->m_pProtoInvMass[n0];
				const float skin1 = srt.m_pBuffs->m_pProtoInvMass[n1];

				const float skinSum = skin0 + skin1;
				const float skinSumRec = SAFE_DIVIDE(srt.m_pConsts->m_pFrame->m_omega.x, skinSum);

				const float skinNorm0 = skin0 * skinSumRec;
				const float skinNorm1 = skin1 * skinSumRec;

				const float3 correction = normDiff * stretch;

				const float3 diffP0 = correction * skinNorm0;
				const float3 diffP1 = -correction * skinNorm1;

				residual += isnan(rawStretch) ? 1000000.0f : abs(stretch) / restLength;

				//const float3 diffV0 = diffP0 / srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;
				//const float3 diffV1 = diffP1 / srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;

				srt.m_pRwBuffs->m_pScratch[n0].xyz += diffP0 / srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;
				srt.m_pRwBuffs->m_pScratch[n1].xyz += diffP1 / srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;
			}
			ClothSync();
		}

		const float3 s = srt.m_pRwBuffs->m_pScratch[dispatchThreadId].xyz;
		const float len = length(s);
		const float3 diff = s * (srt.m_pConsts->m_pFrame->m_unstretchLimit.x / max(len, srt.m_pConsts->m_pFrame->m_unstretchLimit.x));

		srt.m_pRwBuffs->m_pBodyVel[dispatchThreadId].xyz += diff;
		ClothSync();
	}

	g_ldsBuf[dispatchThreadId] = residual;
	ClothSync();

	ClothUnstretchReduce(srt, dispatchThreadId);

	srt.m_pRwBuffs->m_pClothStats[0].m_unstretchResidual = g_ldsBuf[0];

#ifdef CLOTH_ENABLE_STATS
	if (g_subframeIndex < 4)
		srt.m_pRwBuffs->m_pClothStats[0].m_unstretchResidualItr[g_subframeIndex] = g_ldsBuf[0];
#endif

	PROFILE_MARKER_END(PROFILE_MARKER_YELLOW, dispatchThreadId);
}

[isolate]
void ClothCollideVertsDiscrete(SrtData srt, uint dispatchThreadId, uint subFrameNum)
{
	if (dispatchThreadId >= srt.m_pConsts->m_pConst->m_numNodes)
		return;

	PROFILE_MARKER_START(PROFILE_MARKER_ORANGE, dispatchThreadId);

	const float substep_t0 = (float)subFrameNum / (float)srt.m_pConsts->m_pFrame->m_numSubframes;
	const float substep_t1 = (float)(subFrameNum+1) / (float)srt.m_pConsts->m_pFrame->m_numSubframes;

	for(uint ii=0; ii<srt.m_pConsts->m_pFrame->m_colliderNumEdges; ++ii)
	{
		// Interpolate collider position/velocity (duplicated work in every node)
		const int colliderEdgeVertA = srt.m_pBuffs->m_pColliderEdge[ii].m_vertA;
		const int colliderEdgeVertB = srt.m_pBuffs->m_pColliderEdge[ii].m_vertB;

		const float colliderEdgeRadiusA = srt.m_pBuffs->m_pColliderEdge[ii].m_radiusA;
		const float colliderEdgeRadiusB = srt.m_pBuffs->m_pColliderEdge[ii].m_radiusB;

		const float3 pb0 = lerp( srt.m_pBuffs->m_pColliderPosOld[colliderEdgeVertA].xyz, srt.m_pBuffs->m_pColliderPosNew[colliderEdgeVertA].xyz, substep_t0);
		const float3 pb1 = lerp( srt.m_pBuffs->m_pColliderPosOld[colliderEdgeVertB].xyz, srt.m_pBuffs->m_pColliderPosNew[colliderEdgeVertB].xyz, substep_t0);

		const float3 pb_vec = pb1 - pb0;
		const float pb_len2 = dot(pb1 - pb0, pb1 - pb0);

		const float3 pb0_t1 = lerp( srt.m_pBuffs->m_pColliderPosOld[colliderEdgeVertA].xyz, srt.m_pBuffs->m_pColliderPosNew[colliderEdgeVertA].xyz, substep_t1);
		const float3 pb1_t1 = lerp( srt.m_pBuffs->m_pColliderPosOld[colliderEdgeVertB].xyz, srt.m_pBuffs->m_pColliderPosNew[colliderEdgeVertB].xyz, substep_t1);

		const float3 vb0 = (pb0_t1 - pb0) / srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;
		const float3 vb1 = (pb1_t1 - pb1) / srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;


		// Do collision for this node
		const float3 va = srt.m_pRwBuffs->m_pBodyVel[dispatchThreadId].xyz;
		const float3 pa = srt.m_pRwBuffs->m_pBodyPos[dispatchThreadId].xyz;
		const float invMass = srt.m_pBuffs->m_pProtoInvMass[dispatchThreadId];

		const float tb = clamp(SAFE_DIVIDE(dot(pa - pb0, pb_vec), pb_len2), 0.0f, 1.0f);
		const float3 pb = lerp(pb0, pb1, tb);
		const float3 vb = lerp(vb0, vb1, tb);
		const float thickness = lerp(colliderEdgeRadiusA, colliderEdgeRadiusB, tb);


		// sep points from b toward a
		const float3 sep = pa - pb;
		const float sepDist = length(sep);
		const float3 sepDir = SAFE_DIVIDE(sep, sepDist);

		// dot(vPerp, sepDir) is positive if the points are separating
		const float3 vDiff = va - vb;
		const float vPerp = dot(vDiff, sepDir);
		const float nextSepDist = sepDist + vPerp*srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;

		const float3 vTan = vDiff - vPerp*sepDir;

		// corr, wantDeltaV always > 0
		const float corr = max(0.0f, thickness - nextSepDist);
		const float wantDeltaV = srt.m_pConsts->m_pFrame->m_collOmega.x * corr / srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;

		const float impulseMag = SAFE_DIVIDE(wantDeltaV, invMass);
		const float frictionMag = SAFE_DIVIDE(max(-1.0f, SAFE_DIVIDE( -srt.m_pConsts->m_pFrame->m_charCollFriction.x * impulseMag,  length(vTan))), invMass);

		// need to take into account the possibility of invMass 0...
		srt.m_pRwBuffs->m_pBodyVel[dispatchThreadId].xyz = va + invMass * (impulseMag * sepDir + frictionMag * vTan);
	}

	for(uint ii=0; ii<srt.m_pConsts->m_pFrame->m_colliderNumPlanes; ++ii)
	{
		const float3 pb = lerp( srt.m_pBuffs->m_pColliderPlaneOld[ii].m_point.xyz, srt.m_pBuffs->m_pColliderPlaneNew[ii].m_point.xyz, substep_t0);
		const float3 pb_t1 = lerp( srt.m_pBuffs->m_pColliderPlaneOld[ii].m_point.xyz, srt.m_pBuffs->m_pColliderPlaneNew[ii].m_point.xyz, substep_t1);
		const float3 vb = (pb_t1 - pb) / srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;

		const float3 planeNormal = normalize( lerp( srt.m_pBuffs->m_pColliderPlaneOld[ii].m_normal.xyz, srt.m_pBuffs->m_pColliderPlaneNew[ii].m_normal.xyz, substep_t0) );


		const float3 va = srt.m_pRwBuffs->m_pBodyVel[dispatchThreadId].xyz;
		const float3 pa = srt.m_pRwBuffs->m_pBodyPos[dispatchThreadId].xyz;
		const float invMass = srt.m_pBuffs->m_pProtoInvMass[dispatchThreadId];

		const float3 planeToVert = pa - pb;
		const float planeDist = dot(planeNormal, planeToVert);

		const float3 vDiff = va - vb;
		const float vPerp = dot(vDiff, planeNormal);
		const float nextPlaneDist = planeDist + vPerp*srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;

		const float corr = max(0.0f, -nextPlaneDist);
		const float wantDeltaV = corr / srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;

		const float impulseMag = SAFE_DIVIDE(wantDeltaV, invMass);

		srt.m_pRwBuffs->m_pBodyVel[dispatchThreadId].xyz = va + invMass * (impulseMag * planeNormal);
	}

	PROFILE_MARKER_END(PROFILE_MARKER_ORANGE, dispatchThreadId);
}

[isolate]
void ClothUnstretchLongRangeConstraints(SrtData srt, uint dispatchThreadId)
{
	if (!srt.m_pConsts->m_pFrame->m_longRangeUnstretch || dispatchThreadId >= srt.m_pConsts->m_pConst->m_numNodes)
		return;

	const uint n0 = srt.m_pBuffs->m_pLrcLink[dispatchThreadId].m_node;
	const uint n1 = dispatchThreadId;

	const float3 p0 = srt.m_pRwBuffs->m_pBodyPos[n0].xyz;
	const float3 p1 = srt.m_pRwBuffs->m_pBodyPos[n1].xyz;

	const float3 v0 = srt.m_pRwBuffs->m_pBodyVel[n0].xyz;
	const float3 v1 = srt.m_pRwBuffs->m_pBodyVel[n1].xyz;

	const float3 nextP0 = p0 + srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x * v0;
	const float3 nextP1 = p1 + srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x * v1;

	const float3 nextDiff = nextP1 - nextP0;
	const float nextLength = length(nextDiff);
	const float3 normDiff = (nextLength > 0.0f) ? nextDiff / nextLength : nextDiff;

	const float restLength = srt.m_pBuffs->m_pLrcLink[dispatchThreadId].m_dist;
	const float stretch = max(0.0f, nextLength - restLength);

	const float3 adjustVec = -stretch*normDiff;

	const float3 adjustVel = adjustVec / srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;
	const float adjustVelLen = length(adjustVel);

	const float3 velDiff = adjustVel * (srt.m_pConsts->m_pFrame->m_unstretchLimit.x / max(adjustVelLen, srt.m_pConsts->m_pFrame->m_unstretchLimit.x));

	srt.m_pRwBuffs->m_pBodyVel[dispatchThreadId].xyz += velDiff;
}

[isolate]
void ClothUpdatePositionSubframe(SrtData srt, uint dispatchThreadId)
{
	if (dispatchThreadId >= srt.m_pConsts->m_pConst->m_numNodes)
		return;

	const float3 vel = srt.m_pRwBuffs->m_pBodyVel[dispatchThreadId].xyz;
	const float3 oldPos = srt.m_pRwBuffs->m_pBodyPos[dispatchThreadId].xyz;
	const float3 newPos = oldPos + vel * srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;
			
	srt.m_pRwBuffs->m_pBodyPos[dispatchThreadId].xyz = newPos;
}

[isolate]
void ClothEdgeGeomSkinVertices(SrtData srt, uint dispatchThreadId)
{
	if (dispatchThreadId >= srt.m_pConsts->m_pConst->m_numNodes)
		return;

	const uint startOffset = 12*dispatchThreadId;
	float M[16] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

	const uint kMaxWeights = 12;
	float weights[kMaxWeights];

	float totalWeight = 0.0f;

	#pragma loop (unroll: never)
	for (uint i=0; i<kMaxWeights; i++)
	{
		const float weight = srt.m_pBuffs->m_pProtoSkinWeights[startOffset + i];

		weights[i] = weight;
		totalWeight += weight;
	}

	const float totalWeightInv = 1.0f/totalWeight;

	// Don't unroll loop to use less VGPRs
	#pragma loop (unroll: never)
	for (uint i=0; i<kMaxWeights; i++)
	{
		const float weight = totalWeightInv * weights[i];
		const uint offset = srt.m_pBuffs->m_pProtoSkinOffsets[startOffset + i] * 12;

		M[0]  += weight * srt.m_pRwBuffs->m_pBodySkinMats[offset+0];
		M[1]  += weight * srt.m_pRwBuffs->m_pBodySkinMats[offset+5];
		M[2]  += weight * srt.m_pRwBuffs->m_pBodySkinMats[offset+10];
			
		M[4]  += weight * srt.m_pRwBuffs->m_pBodySkinMats[offset+1];
		M[5]  += weight * srt.m_pRwBuffs->m_pBodySkinMats[offset+6];
		M[6]  += weight * srt.m_pRwBuffs->m_pBodySkinMats[offset+8];
			
		M[8]  += weight * srt.m_pRwBuffs->m_pBodySkinMats[offset+2];
		M[9]  += weight * srt.m_pRwBuffs->m_pBodySkinMats[offset+4];
		M[10] += weight * srt.m_pRwBuffs->m_pBodySkinMats[offset+9];
	
		M[12] += weight * srt.m_pRwBuffs->m_pBodySkinMats[offset+3];
		M[13] += weight * srt.m_pRwBuffs->m_pBodySkinMats[offset+7];
		M[14] += weight * srt.m_pRwBuffs->m_pBodySkinMats[offset+11];
	}
	#pragma loop (unroll: default)

	M[3]  = 1.0f;
	M[7]  = 0.0f;
	M[11] = 0.0f;
	M[15] = 0.0f;


	//skin vertex
	float4 pos = srt.m_pBuffs->m_pProtoInitialPos[dispatchThreadId];
	srt.m_pRwBuffs->m_pBodySkinPos[dispatchThreadId].x = M[12] + pos.x * M[0] + pos.y * M[4] + pos.z * M[8];
	srt.m_pRwBuffs->m_pBodySkinPos[dispatchThreadId].y = M[13] + pos.y * M[1] + pos.z * M[5] + pos.x * M[9];
	srt.m_pRwBuffs->m_pBodySkinPos[dispatchThreadId].z = M[14] + pos.z * M[2] + pos.x * M[6] + pos.y * M[10];
	srt.m_pRwBuffs->m_pBodySkinPos[dispatchThreadId].w = M[15] + pos.w * M[3] + pos.w * M[7] + pos.w * M[11];

	srt.m_pRwBuffs->m_pBodySkinPos[dispatchThreadId].xyz = mul(float4( srt.m_pRwBuffs->m_pBodySkinPos[dispatchThreadId].xyz, 1), srt.m_pConsts->m_pFrame->m_modelToClothSpaceXform).xyz;
}

[isolate]
void ClothComputeSimPct(SrtData srt, uint dispatchThreadId)
{
	if (dispatchThreadId >= srt.m_pConsts->m_pConst->m_numNodes)
		return;

	const float allowSimPct = clamp(srt.m_pConsts->m_pFrame->m_framesSinceTeleport/15.0f, 0.0f, 1.0f);	// Quick blend in from fully skinned position after a teleport
	const float skinFloor = lerp(1.0f,srt.m_pConsts->m_pFrame->m_skinFloor.x, allowSimPct);

	const float simProto = 1.0f - srt.m_pBuffs->m_pProtoClothSkinPct[dispatchThreadId];
	//const float simCeil = 1.0f - skinFloor;
	const float simCeil = (dispatchThreadId == srt.m_pConsts->m_pFrame->m_forceSkinJoint) ? lerp(1.0f - skinFloor, 0.0, srt.m_pConsts->m_pFrame->m_forceSkinPct) : (1.0f - skinFloor);


	const float simFilterLevel = 1.0f - srt.m_pConsts->m_pFrame->m_skinFilter.x;
	const float simFilter = (simProto > simFilterLevel) ? 1.0f : simProto;
	const float simFinal = min(simFilter, simCeil);

	g_ldsBuf[dispatchThreadId] = simFinal;
}

[isolate]
void ClothRecomputeLinkLengths(SrtData srt, uint dispatchThreadId)
{
	if (srt.m_pConsts->m_pFrame->m_clothRecomputeLinkLengthsShortestPartial)
	{
		for(uint ii=0; ii<srt.m_pConsts->m_pConst->m_numLinks; ii+=THREAD_COUNT)
		{
			const uint index = ii + dispatchThreadId;

			if (index < srt.m_pConsts->m_pConst->m_numLinks)
			{
				const int n0 = srt.m_pBuffs->m_pProtoLink[index].m_p0;
				const int n1 = srt.m_pBuffs->m_pProtoLink[index].m_p1;

				const float3 p0 = srt.m_pRwBuffs->m_pBodySkinPos[n0].xyz;
				const float3 p1 = srt.m_pRwBuffs->m_pBodySkinPos[n1].xyz;
	
				const float currSkinnedLength = srt.m_pRwBuffs->m_pBodyLinkLengths[index] = length(p0-p1);
				const float bindposeSkinnedLength = srt.m_pBuffs->m_pProtoLink[index].m_length;
				const float shortestLength = min(currSkinnedLength, bindposeSkinnedLength);

				const float sim = 0.5f*(g_ldsBuf[n0] + g_ldsBuf[n1]);	// Sim pct in g_ldsBuf computed in ClothComputeSimPct()
				srt.m_pRwBuffs->m_pBodyLinkLengths[index] = lerp(currSkinnedLength, shortestLength, sim);
			}
		}
	}
	else if (srt.m_pConsts->m_pFrame->m_clothRecomputeLinkLengthsShortest)
	{
		for(uint ii=0; ii<srt.m_pConsts->m_pConst->m_numLinks; ii+=THREAD_COUNT)
		{
			const uint index = ii + dispatchThreadId;

			if (index < srt.m_pConsts->m_pConst->m_numLinks)
			{
				const int n0 = srt.m_pBuffs->m_pProtoLink[index].m_p0;
				const int n1 = srt.m_pBuffs->m_pProtoLink[index].m_p1;

				const float3 p0 = srt.m_pRwBuffs->m_pBodySkinPos[n0].xyz;
				const float3 p1 = srt.m_pRwBuffs->m_pBodySkinPos[n1].xyz;
	
				const float currSkinnedLength = srt.m_pRwBuffs->m_pBodyLinkLengths[index] = length(p0-p1);
				const float bindposeSkinnedLength = srt.m_pBuffs->m_pProtoLink[index].m_length;

				srt.m_pRwBuffs->m_pBodyLinkLengths[index] = min(currSkinnedLength, bindposeSkinnedLength);
			}
		}
	}
	else if (srt.m_pConsts->m_pFrame->m_clothRecomputeLinkLengthsPartial)
	{
		for(uint ii=0; ii<srt.m_pConsts->m_pConst->m_numLinks; ii+=THREAD_COUNT)
		{
			const uint index = ii + dispatchThreadId;

			if (index < srt.m_pConsts->m_pConst->m_numLinks)
			{
				const int n0 = srt.m_pBuffs->m_pProtoLink[index].m_p0;
				const int n1 = srt.m_pBuffs->m_pProtoLink[index].m_p1;

				const float3 p0 = srt.m_pRwBuffs->m_pBodySkinPos[n0].xyz;
				const float3 p1 = srt.m_pRwBuffs->m_pBodySkinPos[n1].xyz;
	
				const float currSkinnedLength = srt.m_pRwBuffs->m_pBodyLinkLengths[index] = length(p0-p1);
				const float bindposeSkinnedLength = srt.m_pBuffs->m_pProtoLink[index].m_length;

				const float sim = 0.5f*(g_ldsBuf[n0] + g_ldsBuf[n1]);	// Sim pct in g_ldsBuf computed in ClothComputeSimPct()
				srt.m_pRwBuffs->m_pBodyLinkLengths[index] = lerp(currSkinnedLength, bindposeSkinnedLength, sim);
			}
		}
	}
	else if (srt.m_pConsts->m_pFrame->m_clothRecomputeLinkLengthsFull)
	{
		for(uint ii=0; ii<srt.m_pConsts->m_pConst->m_numLinks; ii+=THREAD_COUNT)
		{
			const uint index = ii + dispatchThreadId;

			if (index < srt.m_pConsts->m_pConst->m_numLinks)
			{
				const float3 p0 = srt.m_pRwBuffs->m_pBodySkinPos[srt.m_pBuffs->m_pProtoLink[index].m_p0].xyz;
				const float3 p1 = srt.m_pRwBuffs->m_pBodySkinPos[srt.m_pBuffs->m_pProtoLink[index].m_p1].xyz;
	
				srt.m_pRwBuffs->m_pBodyLinkLengths[index] = length(p0-p1);
			}
		}
	}
	else
	{
		for(uint ii=0; ii<srt.m_pConsts->m_pConst->m_numLinks; ii+=THREAD_COUNT)
		{
			const uint index = ii + dispatchThreadId;

			if (index < srt.m_pConsts->m_pConst->m_numLinks)
				srt.m_pRwBuffs->m_pBodyLinkLengths[index] = srt.m_pBuffs->m_pProtoLink[index].m_length;
		}
	}
}

[isolate]
void ClothComputeSkinVelocity(SrtData srt, uint dispatchThreadId, float deltaTimeRemaining)
{
	if (dispatchThreadId >= srt.m_pConsts->m_pConst->m_numNodes)
		return;

	const float3 skinnedVel = (srt.m_pRwBuffs->m_pBodySkinPos[dispatchThreadId].xyz - srt.m_pRwBuffs->m_pBodyPos[dispatchThreadId].xyz) / deltaTimeRemaining;

	const float sim = g_ldsBuf[dispatchThreadId];	// Sim pct in g_ldsBuf computed in ClothComputeSimPct()
	const float simFinal = (srt.m_pConsts->m_pFrame->m_enableSkinning || srt.m_pBuffs->m_pProtoClothSkinPct[dispatchThreadId] == 1.0f) ? sim : 1.0f;

	const float3 currVel = srt.m_pRwBuffs->m_pBodyVel[dispatchThreadId].xyz;
	const float lerpVal = pow( simFinal, srt.m_pConsts->m_pFrame->m_skinBias.x / srt.m_pConsts->m_pFrame->m_numSubframes );

	srt.m_pRwBuffs->m_pBodyVel[dispatchThreadId].xyz = lerp(skinnedVel, currVel, lerpVal);
}

[isolate]
void ClothComputeSkinMoveDist(SrtData srt, uint dispatchThreadId, float deltaTimeRemaining)
{
	if (!srt.m_pConsts->m_pFrame->m_enableSkinMoveDist || dispatchThreadId >= srt.m_pConsts->m_pConst->m_numNodes)
		return;

	const float3 skinnedVel = (srt.m_pRwBuffs->m_pBodySkinPos[dispatchThreadId].xyz - srt.m_pRwBuffs->m_pBodySkinPosPrev[dispatchThreadId].xyz) / deltaTimeRemaining;
	srt.m_pRwBuffs->m_pBodySkinPosPrev[dispatchThreadId].xyz = srt.m_pRwBuffs->m_pBodySkinPos[dispatchThreadId].xyz;

	const float3 p0 = srt.m_pRwBuffs->m_pBodyPos[dispatchThreadId].xyz;
	const float3 p1 = srt.m_pRwBuffs->m_pBodySkinPos[dispatchThreadId].xyz;

	const float3 v0 = srt.m_pRwBuffs->m_pBodyVel[dispatchThreadId].xyz;
	const float3 v1 = skinnedVel;

	const float3 nextP0 = p0 + srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x * v0;
	const float3 nextP1 = p1 + srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x * v1;

	const float3 nextDiff = nextP1 - nextP0;
	const float nextLength = length(nextDiff);
	const float3 normDiff = SAFE_DIVIDE(nextDiff,  nextLength);

	const float restLength = lerp(srt.m_pConsts->m_pFrame->m_skinMoveDistMin.x, srt.m_pConsts->m_pFrame->m_skinMoveDistMax.x, srt.m_pBuffs->m_pProtoClothSkinMoveDist[dispatchThreadId]);

	const float rawStretch = nextLength - restLength;
	const float stretch = max(0.0f, abs(rawStretch) - restLength) * sign(rawStretch);

	const float3 correction = normDiff * stretch;

	const float3 diffP0 = correction * srt.m_pConsts->m_pFrame->m_omega.x;

	const float3 diffV0 = diffP0 / srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;

	srt.m_pRwBuffs->m_pBodyVel[dispatchThreadId].xyz += diffV0;
}

[isolate]
void ClothReboot(SrtData srt, uint dispatchThreadId)
{
	if (dispatchThreadId >= srt.m_pConsts->m_pConst->m_numNodes)
		return;

	srt.m_pRwBuffs->m_pBodyVel[dispatchThreadId] = float4(0,0,0,0);

	ClothEdgeGeomSkinVertices(srt, dispatchThreadId);

	srt.m_pRwBuffs->m_pBodyPos[dispatchThreadId].xyz = srt.m_pRwBuffs->m_pBodySkinPos[dispatchThreadId].xyz;
	srt.m_pRwBuffs->m_pBodySkinPosPrev[dispatchThreadId].xyz = srt.m_pRwBuffs->m_pBodySkinPos[dispatchThreadId].xyz;

#ifdef CLOTH_ENABLE_STATS
	if (dispatchThreadId == 0)
	{
		srt.m_pRwBuffs->m_pClothStats[0].m_didReboot = 1;
	}
#endif
}

[isolate]
void ClothBackwardsEulerComputeY(SrtData srt, uint nodeIndex, out float3 Y)
{
	// ComputeVelForcesAndDamping() - Gravity and damping
	float3 velLs = srt.m_pRwBuffs->m_pBodyVel[nodeIndex].xyz + srt.m_pRwBuffs->m_pBodyVelWsInv[nodeIndex].xyz;
	float3 forcesTotal = srt.m_pConsts->m_pFrame->m_gravity.xyz - srt.m_pConsts->m_pFrame->m_damping.x * velLs;

	// Combined AddBendingForces and BackwardsEulerAddDFDX bending forces
	if (!srt.m_pConsts->m_pFrame->m_disableBendingForces)
	{
		const uint numFacePairs = srt.m_pConsts->m_pConst->m_numFacePairs;

		for (uint facePairIndex=nodeIndex; facePairIndex<numFacePairs; facePairIndex+=THREAD_COUNT)
		{
			const int lA = srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_oppNode0;
			const int lB = srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_oppNode1;
			const int lC = srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_edgeNode0;
			const int lD = srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_edgeNode1;

			const float3 PA = srt.m_pRwBuffs->m_pBodyPos[lA].xyz;
			const float3 PB = srt.m_pRwBuffs->m_pBodyPos[lB].xyz;
			const float3 PC = srt.m_pRwBuffs->m_pBodyPos[lC].xyz;
			const float3 PD = srt.m_pRwBuffs->m_pBodyPos[lD].xyz;

			const float3 vA = srt.m_pRwBuffs->m_pBodyVel[lA].xyz;
			const float3 vB = srt.m_pRwBuffs->m_pBodyVel[lB].xyz;
			const float3 vC = srt.m_pRwBuffs->m_pBodyVel[lC].xyz;
			const float3 vD = srt.m_pRwBuffs->m_pBodyVel[lD].xyz;

			const float lambdaPre = min( srt.m_pConsts->m_pFrame->m_bendStiffnessFilter.x, srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_lambdaPre);
			const float lambda = lambdaPre * srt.m_pConsts->m_pFrame->m_bendStiffness.x;

			const float alphaA = srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_alphaA;
			const float alphaB = srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_alphaB;
			const float alphaC = srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_alphaC;
			const float alphaD = srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_alphaD;


			const float3 N = cross( PA-PB, PD-PC );
			const float Nlen = length(N);
			const float3 N0 = SAFE_DIVIDE(N, Nlen);
			const float3 R0 = N0 * srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_halfYotta0;

			const float3 R = alphaA*PA + alphaB*PB + alphaC*PC + alphaD*PD - R0;
			const float3 vR = alphaA*vA + alphaB*vB + alphaC*vC + alphaD*vD;

			const float3 lambdaR = lambda * R;
			const float3 dampVel = srt.m_pConsts->m_pFrame->m_bendDamping.x * vR;

			const float3 force = lambdaR + dampVel;	// AddBendingForces force
		
			const float scale = srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;
			const float3 prod = scale * lambda * (alphaA*vA + alphaB*vB + alphaC*vC + alphaD*vD);	// DFDX force

			const uint forceNodeA = srt.m_pBuffs->m_pProtoFacePairNodeOffsets[facePairIndex].m_oppNodeOffsets0;
			const uint forceNodeB = srt.m_pBuffs->m_pProtoFacePairNodeOffsets[facePairIndex].m_oppNodeOffsets1;
			const uint forceNodeC = srt.m_pBuffs->m_pProtoFacePairNodeOffsets[facePairIndex].m_edgeNodeOffsets0;
			const uint forceNodeD = srt.m_pBuffs->m_pProtoFacePairNodeOffsets[facePairIndex].m_edgeNodeOffsets1;

			const float3 forceProd = force + prod;
			srt.m_pRwBuffs->m_pBodyFacePairForces[forceNodeA].xyz = alphaA * forceProd;
			srt.m_pRwBuffs->m_pBodyFacePairForces[forceNodeB].xyz = alphaB * forceProd;
			srt.m_pRwBuffs->m_pBodyFacePairForces[forceNodeC].xyz = alphaC * forceProd;
			srt.m_pRwBuffs->m_pBodyFacePairForces[forceNodeD].xyz = alphaD * forceProd;
		}

		ClothSync();

		const uint nodeForceStart = srt.m_pBuffs->m_pProtoNodeFacePairOffsets[nodeIndex].m_offset;
		const uint nodeForceEnd = nodeForceStart + srt.m_pBuffs->m_pProtoNodeFacePairOffsets[nodeIndex].m_count;
		for (uint ii=nodeForceStart; ii<nodeForceEnd; ii++)
			forcesTotal -= srt.m_pRwBuffs->m_pBodyFacePairForces[ii].xyz;
	}

	// I can't think of any reason there needs to be a sync here, but cloth is more jittery as the bending forces go up
	// if this line is removed. Would be nice to figure this out some day... -RyanB
	ClothSync();

	if (nodeIndex < srt.m_pConsts->m_pConst->m_numNodes)
	{
		const int linkStartOffset = srt.m_pBuffs->m_pProtoNodeLinkInfo[nodeIndex].m_offset;
		const int linkCount = srt.m_pBuffs->m_pProtoNodeLinkInfo[nodeIndex].m_count;

		for (int ii=0; ii<linkCount; ii++)
		{
			// ComputeVelForcesAndDamping() - Spring forces
			const int linkOffset = linkStartOffset + ii;
			const int linkIndex = srt.m_pBuffs->m_pProtoNodeLinks[linkOffset];

			const int linkN0 = srt.m_pBuffs->m_pProtoLink[linkIndex].m_p0;
			const int linkN1 = srt.m_pBuffs->m_pProtoLink[linkIndex].m_p1;

			const int n0 = nodeIndex;
			const int n1 = (nodeIndex == linkN0) ? linkN1 : linkN0;

			const float restLength = srt.m_pRwBuffs->m_pBodyLinkLengths[linkIndex];
			const float3 p0 = srt.m_pRwBuffs->m_pBodyPos[n0].xyz;
			const float3 p1 = srt.m_pRwBuffs->m_pBodyPos[n1].xyz;
			const float3 linkVec = p1 - p0;

			const float dist = length(linkVec);

			const float diff = dist - restLength;
			const float forceMag = srt.m_pConsts->m_pFrame->m_stiffnessMul.x * srt.m_pBuffs->m_pProtoLink[linkIndex].m_spring * diff;
			const float3 springForce = forceMag * SAFE_DIVIDE(linkVec, dist);


			// BackwardsEulerAddDFDX()
			const float k = srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x * srt.m_pConsts->m_pFrame->m_stiffnessMul.x * srt.m_pBuffs->m_pProtoLink[linkIndex].m_spring;

			const float3 dFidPi_vi = srt.m_pRwBuffs->m_pBodyVel[n0].xyz;
			const float3 dFidPi_vj = srt.m_pRwBuffs->m_pBodyVel[n1].xyz;

			const float3 vAdd = dFidPi_vj - dFidPi_vi;
			const float3 dfdxForce = k * vAdd;


			forcesTotal += springForce + dfdxForce;
		}

		forcesTotal *= srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x * srt.m_pBuffs->m_pProtoInvMass[nodeIndex];

		Y = forcesTotal;
	}
	else
	{
		Y = float3(0, 0, 0);
	}
}


// All functions of BackwardsEulerMultiply combined into one shader, minus bending forces
[isolate]
void ClothBackwardsEulerMultiply(SrtData srt, uint nodeIndex, in const float3 T, out float3 HT)
{
	HT = -srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x * srt.m_pConsts->m_pFrame->m_damping.x * T;	// BackwardsEulerAddDFDV()

	if (!srt.m_pConsts->m_pFrame->m_disableBendingForces)
	{
		const uint numFacePairs = srt.m_pConsts->m_pConst->m_numFacePairs;

		for (uint facePairIndex=nodeIndex; facePairIndex<numFacePairs; facePairIndex+=THREAD_COUNT)
		{
			const int lA = srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_oppNode0;
			const int lB = srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_oppNode1;
			const int lC = srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_edgeNode0;
			const int lD = srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_edgeNode1;

			const float alphaA = srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_alphaA;
			const float alphaB = srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_alphaB;
			const float alphaC = srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_alphaC;
			const float alphaD = srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_alphaD;

			const float3 vA = srt.m_pRwBuffs->m_pBkwdEulerWorkT[lA];
			const float3 vB = srt.m_pRwBuffs->m_pBkwdEulerWorkT[lB];
			const float3 vC = srt.m_pRwBuffs->m_pBkwdEulerWorkT[lC];
			const float3 vD = srt.m_pRwBuffs->m_pBkwdEulerWorkT[lD];
			
			const float lambdaPre = min( srt.m_pConsts->m_pFrame->m_bendStiffnessFilter.x, srt.m_pBuffs->m_pProtoFacePairs[facePairIndex].m_lambdaPre);
			const float lambda = lambdaPre * srt.m_pConsts->m_pFrame->m_bendStiffness.x;

			const float scaleDFDV = srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;
			const float scaleDFDX = srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x * srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x;
			const float3 prodAlpha = (alphaA*vA + alphaB *vB + alphaC*vC + alphaD*vD);
			const float3 prodDFDV = scaleDFDV * srt.m_pConsts->m_pFrame->m_bendDamping.x * prodAlpha;	// DFDV
			const float3 prodDFDX = scaleDFDX * lambda * prodAlpha;										// DFDX

			const uint forceNodeA = srt.m_pBuffs->m_pProtoFacePairNodeOffsets[facePairIndex].m_oppNodeOffsets0;
			const uint forceNodeB = srt.m_pBuffs->m_pProtoFacePairNodeOffsets[facePairIndex].m_oppNodeOffsets1;
			const uint forceNodeC = srt.m_pBuffs->m_pProtoFacePairNodeOffsets[facePairIndex].m_edgeNodeOffsets0;
			const uint forceNodeD = srt.m_pBuffs->m_pProtoFacePairNodeOffsets[facePairIndex].m_edgeNodeOffsets1;

			const float3 prod = prodDFDV + prodDFDX;
			srt.m_pRwBuffs->m_pBodyFacePairForces[forceNodeA].xyz = alphaA * prod;
			srt.m_pRwBuffs->m_pBodyFacePairForces[forceNodeB].xyz = alphaB * prod;
			srt.m_pRwBuffs->m_pBodyFacePairForces[forceNodeC].xyz = alphaC * prod;
			srt.m_pRwBuffs->m_pBodyFacePairForces[forceNodeD].xyz = alphaD * prod;
		}

		ClothSync();

		const uint nodeForceStart = srt.m_pBuffs->m_pProtoNodeFacePairOffsets[nodeIndex].m_offset;
		const uint nodeForceEnd = nodeForceStart + srt.m_pBuffs->m_pProtoNodeFacePairOffsets[nodeIndex].m_count;
		for (uint ii=nodeForceStart; ii<nodeForceEnd; ii++)
			HT -= srt.m_pRwBuffs->m_pBodyFacePairForces[ii].xyz;
	}

	//if (nodeIndex < srt.m_pConsts->m_pConst->m_numNodes)
	{
		const int linkStartOffset = srt.m_pBuffs->m_pProtoNodeLinkInfo[nodeIndex].m_offset;
		const int linkCount = srt.m_pBuffs->m_pProtoNodeLinkInfo[nodeIndex].m_count;

		for (int ii=0; ii<linkCount; ii++)
		{
			// BackwardsEulerAddDFDX()
			const int linkOffset = linkStartOffset + ii;
			const int linkIndex = srt.m_pBuffs->m_pProtoNodeLinks[linkOffset];

			const int linkN0 = srt.m_pBuffs->m_pProtoLink[linkIndex].m_p0;
			const int linkN1 = srt.m_pBuffs->m_pProtoLink[linkIndex].m_p1;

			//const int n0 = nodeIndex;
			const int n1 = (nodeIndex == linkN0) ? linkN1 : linkN0;

			const float k = srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x * srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x * srt.m_pConsts->m_pFrame->m_stiffnessMul.x * srt.m_pBuffs->m_pProtoLink[linkIndex].m_spring;

			const float3 dFidPi_vi = T;
			const float3 dFidPi_vj = srt.m_pRwBuffs->m_pBkwdEulerWorkT[n1];

			const float3 vAdd = dFidPi_vj - dFidPi_vi;
			const float3 dfdxForce = k * vAdd;

			HT += dfdxForce;
		}

		const float invMass = srt.m_pBuffs->m_pProtoInvMass[nodeIndex];
		const float3 interm = invMass * invMass * HT;

		HT = invMass * T - interm;
	}
}

void ClothBackwardsEulerReduce(SrtData srt, uint dispatchThreadId)
{
	uint offset = THREAD_COUNT/2;
	while (offset > 32)
	{
		if (dispatchThreadId < offset && dispatchThreadId < srt.m_pConsts->m_pConst->m_numNodes)
			g_ldsBuf[dispatchThreadId] += g_ldsBuf[dispatchThreadId + offset];

		ClothSync();

		offset >>= 1;
	}

	if (dispatchThreadId < 64)
	{
		float sum = g_ldsBuf[dispatchThreadId];

		sum += LaneSwizzle(sum, 0x1f, 0x00, 0x10);
		sum += LaneSwizzle(sum, 0x1f, 0x00, 0x08);
		sum += LaneSwizzle(sum, 0x1f, 0x00, 0x04);
		sum += LaneSwizzle(sum, 0x1f, 0x00, 0x02);
		sum += LaneSwizzle(sum, 0x1f, 0x00, 0x01);

		g_ldsBuf[0] = ReadLane(sum, 0) + ReadLane(sum, 32);
	}
	
	ClothSync();
}

[isolate]
void ClothSolveBackwardsEuler(SrtData srt, uint dispatchThreadId)
{
	PROFILE_MARKER_START(PROFILE_MARKER_GREEN, dispatchThreadId);

	g_ldsBuf[dispatchThreadId] = 0.0f;

	[isolate]
	{
		srt.m_pRwBuffs->m_pBkwdEulerWorkX[dispatchThreadId] = float3(0,0,0);
		srt.m_pRwBuffs->m_pBkwdEulerWorkR[dispatchThreadId] = float3(0,0,0);
		srt.m_pRwBuffs->m_pBkwdEulerWorkT[dispatchThreadId] = float3(0,0,0);
		srt.m_pRwBuffs->m_pBkwdEulerWorkHT[dispatchThreadId] = float3(0,0,0);
	}

	ClothSync();

	ClothBackwardsEulerComputeY(srt, dispatchThreadId, srt.m_pRwBuffs->m_pBkwdEulerWorkR[dispatchThreadId]);
	ClothBackwardsEulerMultiply(srt, dispatchThreadId, srt.m_pRwBuffs->m_pBkwdEulerWorkT[dispatchThreadId], srt.m_pRwBuffs->m_pBkwdEulerWorkHT[dispatchThreadId]);
	srt.m_pRwBuffs->m_pBkwdEulerWorkR[dispatchThreadId] -= srt.m_pRwBuffs->m_pBkwdEulerWorkHT[dispatchThreadId];

	float alpha = 100.0f;
	float beta = 0.0f;
	for(uint ii=0; alpha > srt.m_pConsts->m_pFrame->m_conjGradResidual.x*srt.m_pConsts->m_pConst->m_numNodes && ii<srt.m_pConsts->m_pFrame->m_conjGradIters.x; ++ii)
	{
		PROFILE_MARKER_TICK(PROFILE_MARKER_WHITE, dispatchThreadId);

#ifdef CLOTH_ENABLE_STATS
		if (dispatchThreadId == 0)
			++g_statsSolverLoops;
#endif

		const float3 R = srt.m_pRwBuffs->m_pBkwdEulerWorkR[dispatchThreadId];
		const float sumSqR = dot(R, R);

		ClothSync();		// Need sync here before writing lds, because of lds read and end of prev loop

		g_ldsBuf[dispatchThreadId] = sumSqR;

		ClothSync();

		ClothBackwardsEulerReduce(srt, dispatchThreadId);
		alpha = g_ldsBuf[0];

		const float alphaOverBetaA = SAFE_DIVIDE(alpha, beta);
		
		srt.m_pRwBuffs->m_pBkwdEulerWorkT[dispatchThreadId] = srt.m_pRwBuffs->m_pBkwdEulerWorkR[dispatchThreadId] + alphaOverBetaA * srt.m_pRwBuffs->m_pBkwdEulerWorkT[dispatchThreadId];
		
		ClothSync();

		ClothBackwardsEulerMultiply(srt, dispatchThreadId, srt.m_pRwBuffs->m_pBkwdEulerWorkT[dispatchThreadId], srt.m_pRwBuffs->m_pBkwdEulerWorkHT[dispatchThreadId]);

		const float sumTxHT = dot(srt.m_pRwBuffs->m_pBkwdEulerWorkT[dispatchThreadId], srt.m_pRwBuffs->m_pBkwdEulerWorkHT[dispatchThreadId]);
		g_ldsBuf[dispatchThreadId] = sumTxHT;

		ClothSync();

		ClothBackwardsEulerReduce(srt, dispatchThreadId);
		beta = g_ldsBuf[0];

		const float alphaOverBetaB = SAFE_DIVIDE(alpha, beta);

		srt.m_pRwBuffs->m_pBkwdEulerWorkR[dispatchThreadId] -= alphaOverBetaB*srt.m_pRwBuffs->m_pBkwdEulerWorkHT[dispatchThreadId];
		srt.m_pRwBuffs->m_pBkwdEulerWorkX[dispatchThreadId] += alphaOverBetaB*srt.m_pRwBuffs->m_pBkwdEulerWorkT[dispatchThreadId];

		beta = alpha;
	}

#ifdef CLOTH_ENABLE_STATS
		if (dispatchThreadId == 0)
		{
			srt.m_pRwBuffs->m_pClothStats[0].m_congGradResidual = alpha;
			if (g_subframeIndex < 4)
				srt.m_pRwBuffs->m_pClothStats[0].m_congGradResidualItr[g_subframeIndex] = alpha;
		}
#endif

	srt.m_pRwBuffs->m_pBodyVel[dispatchThreadId].xyz += srt.m_pBuffs->m_pProtoInvMass[dispatchThreadId] * srt.m_pRwBuffs->m_pBkwdEulerWorkX[dispatchThreadId];

	PROFILE_MARKER_END(PROFILE_MARKER_GREEN, dispatchThreadId);
}

[isolate]
void ClothDisableSimPct(SrtData srt, uint dispatchThreadId)
{
	srt.m_pRwBuffs->m_pBodyPos[dispatchThreadId].xyz = lerp(srt.m_pRwBuffs->m_pBodyPos[dispatchThreadId].xyz,
		srt.m_pRwBuffs->m_pBodySkinPos[dispatchThreadId].xyz, srt.m_pConsts->m_pFrame->m_disableSimulationPct.x);
}

[isolate]
void ClothOutputJointsPrep(SrtData srt, uint dispatchThreadId)
{
	if (dispatchThreadId >= srt.m_pConsts->m_pConst->m_numAltPoints)
		return;

	const uint faceListOffset = srt.m_pBuffs->m_pProtoNodeFaceInfo[dispatchThreadId].m_offset;
	const uint faceListCount = srt.m_pBuffs->m_pProtoNodeFaceInfo[dispatchThreadId].m_count;

	float3 normal = {0,0,0};
	float3 tangentU = {0,0,0};
	float3 tangentV = {0,0,0};
	uint numTris = 0;

	for(uint ii=0; ii<faceListCount; ++ii)
	{
		const uint faceIndex = srt.m_pBuffs->m_pProtoNodeFaceLists[faceListOffset + ii];

		const uint ind0 = srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_altNode0;
		const uint ind1 = srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_altNode1;
		//const uint ind2 = srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_altNode2;

		const uint n0 = srt.m_pBuffs->m_pProtoFaces[faceIndex].m_faceN0;
		const uint n1 = srt.m_pBuffs->m_pProtoFaces[faceIndex].m_faceN1;
		const uint n2 = srt.m_pBuffs->m_pProtoFaces[faceIndex].m_faceN2;

		const float3 p0 = srt.m_pRwBuffs->m_pBodyPos[n0].xyz;
		const float3 p1 = srt.m_pRwBuffs->m_pBodyPos[n1].xyz;
		const float3 p2 = srt.m_pRwBuffs->m_pBodyPos[n2].xyz;

		const float3 ea0 = p1 - p0;
		const float3 eb0 = p2 - p0;

		const float3 ea1 = p2 - p1;
		const float3 eb1 = p0 - p1;

		const float3 ea2 = p0 - p2;
		const float3 eb2 = p1 - p2;

		const float3 u0 = srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_rua0 * ea0 + srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_rub0 * eb0;
		const float3 v0 = srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_rva0 * ea0 + srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_rvb0 * eb0;

		const float3 u1 = srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_rua1 * ea1 + srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_rub1 * eb1;
		const float3 v1 = srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_rva1 * ea1 + srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_rvb1 * eb1;

		const float3 u2 = srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_rua2 * ea2 + srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_rub2 * eb2;
		const float3 v2 = srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_rva2 * ea2 + srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_rvb2 * eb2;

		const float3 norm = cross(ea0, eb0);

		const float3 dispatchThreadTangentU = (dispatchThreadId == ind0) ? u0 : (dispatchThreadId == ind1) ? u1 : u2;
		const float3 dispatchThreadTangentV = (dispatchThreadId == ind0) ? v0 : (dispatchThreadId == ind1) ? v1 : v2;

		normal += norm;
		tangentU += dispatchThreadTangentU;
		tangentV += dispatchThreadTangentV;

		numTris++;
	}

	const float numTrisF = (float)numTris;

	srt.m_pRwBuffs->m_pOutputJointsWork[dispatchThreadId].m_normal.xyz = normalize(normal);
	srt.m_pRwBuffs->m_pOutputJointsWork[dispatchThreadId].m_tangentU.xyz = tangentU / numTrisF;
	srt.m_pRwBuffs->m_pOutputJointsWork[dispatchThreadId].m_tangentV.xyz = tangentV / numTrisF;
}

[isolate]
void ClothOutputJoints(SrtData srt, uint dispatchThreadId)
{
	if (dispatchThreadId >= srt.m_pConsts->m_pConst->m_numOutputJoints)
		return;

	const uint jointInd = srt.m_pBuffs->m_pProtoOutputJoints[dispatchThreadId].m_jointInd;
	const uint faceIndex = srt.m_pBuffs->m_pProtoOutputJoints[dispatchThreadId].m_boundFace;

	const uint ind0 = srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_altNode0;
	const uint ind1 = srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_altNode1;
	const uint ind2 = srt.m_pBuffs->m_pProtoFaceTangentWeights[faceIndex].m_altNode2;

	const uint n0 = srt.m_pBuffs->m_pProtoFaces[faceIndex].m_faceN0;
	const uint n1 = srt.m_pBuffs->m_pProtoFaces[faceIndex].m_faceN1;
	const uint n2 = srt.m_pBuffs->m_pProtoFaces[faceIndex].m_faceN2;

	const float3 p0 = srt.m_pRwBuffs->m_pBodyPos[n0].xyz;
	const float3 p1 = srt.m_pRwBuffs->m_pBodyPos[n1].xyz;
	const float3 p2 = srt.m_pRwBuffs->m_pBodyPos[n2].xyz;

	const float3 tanU0 = srt.m_pRwBuffs->m_pOutputJointsWork[ind0].m_tangentU.xyz;
	const float3 tanU1 = srt.m_pRwBuffs->m_pOutputJointsWork[ind1].m_tangentU.xyz;
	const float3 tanU2 = srt.m_pRwBuffs->m_pOutputJointsWork[ind2].m_tangentU.xyz;

	const float3 tanV0 = srt.m_pRwBuffs->m_pOutputJointsWork[ind0].m_tangentV.xyz;
	const float3 tanV1 = srt.m_pRwBuffs->m_pOutputJointsWork[ind1].m_tangentV.xyz;
	const float3 tanV2 = srt.m_pRwBuffs->m_pOutputJointsWork[ind2].m_tangentV.xyz;

	const float3 norm0 = srt.m_pRwBuffs->m_pOutputJointsWork[ind0].m_normal.xyz;
	const float3 norm1 = srt.m_pRwBuffs->m_pOutputJointsWork[ind1].m_normal.xyz;
	const float3 norm2 = srt.m_pRwBuffs->m_pOutputJointsWork[ind2].m_normal.xyz;
		
	const float u = srt.m_pBuffs->m_pProtoOutputJoints[dispatchThreadId].m_u;
	const float v = srt.m_pBuffs->m_pProtoOutputJoints[dispatchThreadId].m_v;
	const float w = 1.0f - u - v;

	const float3 jtPos = w*p0 + u*p1 + v*p2;

	const float3 norm = normalize(w*norm0 + u*norm1 + v*norm2);
	// OPTIMIZATION NOTE: These two Normalize() calls were added during an investigation of why scaled cloth
	// meshes were stretched in the tangential plane. Turns out they are not necessary, but they do yield
	// slightly "better" looking results, and they make sense (these are basis vectors). They slow down the
	// calculations so I'm leaving them out, but *technically* we should be normalizing here.
	//const Vector tanU = /*Normalize*/( w*tanU0 + u*tanU1 + v*tanU2 );
	//const Vector tanV = /*Normalize*/( w*tanV0 + u*tanV1 + v*tanV2 );
	const float3 tanU = w*tanU0 + u*tanU1 + v*tanU2;
	const float3 tanV = w*tanV0 + u*tanV1 + v*tanV2;

	// OPTIMIZATION NOTE: These three Normalize() calls are required for scaled cloth meshes. Without them,
	// the skin is stretched in the tangential plane (but not in the normal direction). Could potentially
	// fix this in the tools by appropriately scaling the values of m_ru{xyz} and m_rv{xyz}...
	const float3 xAxis = normalize( tanU * srt.m_pBuffs->m_pProtoOutputJoints[dispatchThreadId].m_rux + tanV * srt.m_pBuffs->m_pProtoOutputJoints[dispatchThreadId].m_rvx + norm * srt.m_pBuffs->m_pProtoOutputJoints[dispatchThreadId].m_rnx );
	const float3 yAxis = normalize( tanU * srt.m_pBuffs->m_pProtoOutputJoints[dispatchThreadId].m_ruy + tanV * srt.m_pBuffs->m_pProtoOutputJoints[dispatchThreadId].m_rvy + norm * srt.m_pBuffs->m_pProtoOutputJoints[dispatchThreadId].m_rny );
	const float3 zAxis = normalize( tanU * srt.m_pBuffs->m_pProtoOutputJoints[dispatchThreadId].m_ruz + tanV * srt.m_pBuffs->m_pProtoOutputJoints[dispatchThreadId].m_rvz + norm * srt.m_pBuffs->m_pProtoOutputJoints[dispatchThreadId].m_rnz );

	float4 row0 = {xAxis, 0.0f};
	float4 row1 = {yAxis, 0.0f};
	float4 row2 = {zAxis, 0.0f};
	float4 row3 = {jtPos, 1.0f};

	const row_major float4x4 wsJoint44 = {row0, row1, row2, row3};

	const row_major float3x4 invBind34 = srt.m_pBuffs->m_pSkelInvBindPoses[jointInd];
	const row_major float4x4 invBind44 =
	{
		invBind34[0].x, invBind34[1].x, invBind34[2].x, 0,
		invBind34[0].y, invBind34[1].y, invBind34[2].y, 0,
		invBind34[0].z, invBind34[1].z, invBind34[2].z, 0,
		invBind34[0].w, invBind34[1].w, invBind34[2].w, 1,
	};

	const row_major float4x4 modelSpace44 = mul(wsJoint44, srt.m_pConsts->m_pFrame->m_clothToModelSpaceXform);
	const row_major float4x4 skinSpace44 = mul(invBind44, modelSpace44);

	const uint startJointOffset = 12*jointInd;
	srt.m_pRwBuffs->m_pBodySkinMats[startJointOffset + 0] = skinSpace44[0][0];
	srt.m_pRwBuffs->m_pBodySkinMats[startJointOffset + 1] = skinSpace44[1][0];
	srt.m_pRwBuffs->m_pBodySkinMats[startJointOffset + 2] = skinSpace44[2][0];
	srt.m_pRwBuffs->m_pBodySkinMats[startJointOffset + 3] = skinSpace44[3][0];
	srt.m_pRwBuffs->m_pBodySkinMats[startJointOffset + 4] = skinSpace44[0][1];
	srt.m_pRwBuffs->m_pBodySkinMats[startJointOffset + 5] = skinSpace44[1][1];
	srt.m_pRwBuffs->m_pBodySkinMats[startJointOffset + 6] = skinSpace44[2][1];
	srt.m_pRwBuffs->m_pBodySkinMats[startJointOffset + 7] = skinSpace44[3][1];
	srt.m_pRwBuffs->m_pBodySkinMats[startJointOffset + 8] = skinSpace44[0][2];
	srt.m_pRwBuffs->m_pBodySkinMats[startJointOffset + 9] = skinSpace44[1][2];
	srt.m_pRwBuffs->m_pBodySkinMats[startJointOffset + 10] = skinSpace44[2][2];
	srt.m_pRwBuffs->m_pBodySkinMats[startJointOffset + 11] = skinSpace44[3][2];
}

[isolate]
void ClothApplySkinVelocity(SrtData srt, int dispatchThreadId, float deltaTimeRemaining)
{
	ClothEdgeGeomSkinVertices(srt, dispatchThreadId);
	ClothSync();

	ClothComputeSimPct(srt, dispatchThreadId);
	ClothSync();

	ClothRecomputeLinkLengths(srt, dispatchThreadId);
	ClothSync();

	ClothComputeSkinVelocity(srt, dispatchThreadId, deltaTimeRemaining);
	ClothComputeSkinMoveDist(srt, dispatchThreadId, deltaTimeRemaining);
}

[isolate]
void ClothApplySkinVelSubframe(SrtData srt, int dispatchThreadId, float deltaTimeRemaining)
{
	PROFILE_MARKER_START(PROFILE_MARKER_CYAN, dispatchThreadId);
	ClothApplyWindForce(srt, dispatchThreadId);
	ClothSync();

	ClothApplySkinVelocity(srt, dispatchThreadId, deltaTimeRemaining);
	PROFILE_MARKER_END(PROFILE_MARKER_CYAN, dispatchThreadId);
}

[isolate]
void ClothUnstretchAndCollide(SrtData srt, int dispatchThreadId, uint subFrameNum)
{
	if (!srt.m_pConsts->m_pFrame->m_disableCollision)
	{
#ifdef CLOTH_ENABLE_STATS
		if (dispatchThreadId == 0)
			g_statsTickStartCol = GetTimer().x;
#endif

		for(uint jj=0; jj<srt.m_pConsts->m_pFrame->m_numInnerIters; ++jj)
		{
			ClothCollideVertsDiscrete(srt, dispatchThreadId, subFrameNum);
			ClothSync();
		}

#ifdef CLOTH_ENABLE_STATS
		if (dispatchThreadId == 0)
			g_statsTicksTotalCol += GetTimer().x - g_statsTickStartCol;
#endif
	}

	for(uint ii=0; ii<srt.m_pConsts->m_pFrame->m_numOuterIters; ++ii)
	{
		ClothUnstretchLongRangeConstraints(srt, dispatchThreadId);
		ClothSync();

		ClothUnstretch(srt, dispatchThreadId);
		ClothSync();

#ifdef CLOTH_ENABLE_STATS
		if (dispatchThreadId == 0)
			g_statsTickStartCol = GetTimer().x;
#endif

		if (!srt.m_pConsts->m_pFrame->m_disableCollision)
		{
			for(uint jj=0; jj<srt.m_pConsts->m_pFrame->m_numInnerIters; ++jj)
			{
				ClothCollideVertsDiscrete(srt, dispatchThreadId, subFrameNum);
				ClothSync();
			}
		}

#ifdef CLOTH_ENABLE_STATS
		if (dispatchThreadId == 0)
			g_statsTicksTotalCol += GetTimer().x - g_statsTickStartCol;
#endif
	}
}

[isolate]
void ClothSimulateSubframe(SrtData srt, int dispatchThreadId, uint subFrameNum)
{
	ClothSolveBackwardsEuler(srt, dispatchThreadId);
	ClothSync();

	ClothUnstretchAndCollide(srt, dispatchThreadId, subFrameNum);
}



[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothOutputJointsOnly(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	if (srt.m_pConsts->m_pFrame->m_forceReboot)
	{
		ClothReboot(srt, dispatchThreadId);
		ClothSync();
	}

	ClothOutputJointsPrep(srt, dispatchThreadId);
	ClothSync();

	ClothOutputJoints(srt, dispatchThreadId);
}



[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothCopy(SrtCopyData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	const uint numNodes = srt.m_pConst->m_numNodes;
	const uint numLinks = srt.m_pConst->m_numLinks;

	for (uint iNode=dispatchThreadId; iNode<numNodes; iNode+=THREAD_COUNT)
	{
		srt.m_pRwBuffsDst->m_pBodyPos[iNode] = srt.m_pRwBuffsSrc->m_pBodyPos[iNode];
		srt.m_pRwBuffsDst->m_pBodyVel[iNode] = srt.m_pRwBuffsSrc->m_pBodyVel[iNode];
		srt.m_pRwBuffsDst->m_pBodyVelWsInv[iNode] = srt.m_pRwBuffsSrc->m_pBodyVelWsInv[iNode];
		srt.m_pRwBuffsDst->m_pBodySkinPos[iNode] = srt.m_pRwBuffsSrc->m_pBodySkinPos[iNode];
		srt.m_pRwBuffsDst->m_pBodySkinPosPrev[iNode] = srt.m_pRwBuffsSrc->m_pBodySkinPosPrev[iNode];
	}

	for (uint iLink=dispatchThreadId; iLink<numLinks; iLink+=THREAD_COUNT)
	{
		srt.m_pRwBuffsDst->m_pBodyLinkLengths[iLink] = srt.m_pRwBuffsSrc->m_pBodyLinkLengths[iLink];
	}

	srt.m_pRwBuffsDst->m_pClothStats[0] = srt.m_pRwBuffsSrc->m_pClothStats[0];
}


[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateDisabled(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	PROFILE_START_WF(dispatchThreadId, THREAD_COUNT)

	PROFILE_MARKER_START(PROFILE_MARKER_BLUE, dispatchThreadId);

#ifdef CLOTH_ENABLE_STATS
	g_statsSolverLoops = 0;
	g_statsTickStartCol = 0;
	g_statsTicksTotalCol = 0;
	g_statsTickStartMain = GetTimer().x;
#endif

	PROFILE_MARKER_START(PROFILE_MARKER_RED, dispatchThreadId);
	ClothReboot(srt, dispatchThreadId);
	PROFILE_MARKER_END(PROFILE_MARKER_RED, dispatchThreadId);

	ClothSync();

	PROFILE_MARKER_START(PROFILE_MARKER_BLUE, dispatchThreadId);
	ClothOutputJointsPrep(srt, dispatchThreadId);
	ClothSync();

	ClothOutputJoints(srt, dispatchThreadId);
	PROFILE_MARKER_END(PROFILE_MARKER_BLUE, dispatchThreadId);

#ifdef CLOTH_ENABLE_STATS
	ClothSync();
	if (dispatchThreadId == 0)
	{
		srt.m_pRwBuffs->m_pClothStats[0].m_ticksTotal = GetTimer().x - g_statsTickStartMain;
		srt.m_pRwBuffs->m_pClothStats[0].m_ticksCollide = g_statsTicksTotalCol;
		srt.m_pRwBuffs->m_pClothStats[0].m_solverLoops = g_statsSolverLoops;
	}
#endif

	PROFILE_END_WF(dispatchThreadId)
}

[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulate(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	PROFILE_START_WF(dispatchThreadId, THREAD_COUNT)

	PROFILE_MARKER_START(PROFILE_MARKER_BLUE, dispatchThreadId);

#ifdef CLOTH_ENABLE_STATS
	g_statsSolverLoops = 0;
	g_statsTickStartCol = 0;
	g_statsTicksTotalCol = 0;
	g_statsTickStartMain = GetTimer().x;
#endif

	if (srt.m_pConsts->m_pFrame->m_forceReboot || (srt.m_pRwBuffs->m_pClothStats[0].m_unstretchResidual > 2000.0f))
	{
		ClothReboot(srt, dispatchThreadId);
		ClothSync();
	}

	if (srt.m_pConsts->m_pFrame->m_resetVelLastFrame)
		ClothSetLocalSpaceVelocity(srt, dispatchThreadId);
	else
		ClothAddLocalSpaceVelocity(srt, dispatchThreadId);
	ClothSync();

	PROFILE_MARKER_END(PROFILE_MARKER_BLUE, dispatchThreadId);

	g_subframeIndex = 0;
	for (uint subFrame=0; subFrame<srt.m_pConsts->m_pFrame->m_numSubframes; ++subFrame)
	{
		float deltaTimeRemaining = srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x * ((float)(srt.m_pConsts->m_pFrame->m_numSubframes - subFrame));

		ClothApplySkinVelSubframe(srt, dispatchThreadId, deltaTimeRemaining);
		ClothSync();

		ClothSimulateSubframe(srt, dispatchThreadId, subFrame);
		ClothSync();

		ClothUpdatePositionSubframe(srt, dispatchThreadId);
		ClothSync();

		++g_subframeIndex;
	}

	PROFILE_MARKER_START(PROFILE_MARKER_BLUE, dispatchThreadId);

	ClothDisableSimPct(srt, dispatchThreadId);
	ClothSync();

	ClothOutputJointsPrep(srt, dispatchThreadId);
	ClothSync();

	ClothOutputJoints(srt, dispatchThreadId);
	PROFILE_MARKER_END(PROFILE_MARKER_BLUE, dispatchThreadId);

#ifdef CLOTH_ENABLE_STATS
	ClothSync();
	if (dispatchThreadId == 0)
	{
		srt.m_pRwBuffs->m_pClothStats[0].m_ticksTotal = GetTimer().x - g_statsTickStartMain;
		srt.m_pRwBuffs->m_pClothStats[0].m_ticksCollide = g_statsTicksTotalCol;
		srt.m_pRwBuffs->m_pClothStats[0].m_solverLoops = g_statsSolverLoops;
	}
#endif

	PROFILE_END_WF(dispatchThreadId)
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Define some shaders that don't get exported, but are used to assist profiling
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateStart(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	if (srt.m_pConsts->m_pFrame->m_forceReboot || (srt.m_pRwBuffs->m_pClothStats[0].m_unstretchResidual > 2000.0f))
	{
		ClothReboot(srt, dispatchThreadId);
		ClothSync();
	}

	if (srt.m_pConsts->m_pFrame->m_resetVelLastFrame)
		ClothSetLocalSpaceVelocity(srt, dispatchThreadId);
	else
		ClothAddLocalSpaceVelocity(srt, dispatchThreadId);
}

[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateStartReboot(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	ClothReboot(srt, dispatchThreadId);
}

[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateStartSkin(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	ClothEdgeGeomSkinVertices(srt, dispatchThreadId);
}

[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateStartSetLocalSpaceVelocity(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	ClothSetLocalSpaceVelocity(srt, dispatchThreadId);
}

[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateStartAddLocalSpaceVelocity(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	ClothAddLocalSpaceVelocity(srt, dispatchThreadId);
}

[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateLoop(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	for (uint subFrame=0; subFrame<srt.m_pConsts->m_pFrame->m_numSubframes; ++subFrame)
	{
		float deltaTimeRemaining = srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x * ((float)(srt.m_pConsts->m_pFrame->m_numSubframes - subFrame));

		ClothApplySkinVelSubframe(srt, dispatchThreadId, deltaTimeRemaining);
		ClothSync();

		ClothSimulateSubframe(srt, dispatchThreadId, subFrame);
		ClothSync();

		ClothUpdatePositionSubframe(srt, dispatchThreadId);
		ClothSync();
	}
}

[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateLoopApplySkinVel(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	for (uint subFrame=0; subFrame<srt.m_pConsts->m_pFrame->m_numSubframes; ++subFrame)
	{
		float deltaTimeRemaining = srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x * ((float)(srt.m_pConsts->m_pFrame->m_numSubframes - subFrame));
		ClothApplySkinVelSubframe(srt, dispatchThreadId, deltaTimeRemaining);
	}
}

[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateLoopWindForce(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	ClothApplyWindForce(srt, dispatchThreadId);
}

[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateLoopApplySkinVelInner(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	ClothApplySkinVelocity(srt, dispatchThreadId, 0.0);
}


[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateLoopSimulate(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	for (uint subFrame=0; subFrame<srt.m_pConsts->m_pFrame->m_numSubframes; ++subFrame)
	{
		float deltaTimeRemaining = srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x * ((float)(srt.m_pConsts->m_pFrame->m_numSubframes - subFrame));
		ClothSimulateSubframe(srt, dispatchThreadId, subFrame);
	}
}

[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateCollide(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	ClothCollideVertsDiscrete(srt, dispatchThreadId, 0);
}

[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateUnstretch(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	ClothUnstretch(srt, dispatchThreadId);
}

[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateUpdatePosition(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	for (uint subFrame=0; subFrame<srt.m_pConsts->m_pFrame->m_numSubframes; ++subFrame)
	{
		float deltaTimeRemaining = srt.m_pConsts->m_pFrame->m_subFrameDeltaTime.x * ((float)(srt.m_pConsts->m_pFrame->m_numSubframes - subFrame));
		ClothUpdatePositionSubframe(srt, dispatchThreadId);
	}
}

[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateEnd(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	ClothOutputJointsPrep(srt, dispatchThreadId);
	ClothSync();

	ClothOutputJoints(srt, dispatchThreadId);
}

[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateEndOutputPrep(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	ClothOutputJointsPrep(srt, dispatchThreadId);
}

[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateEndOutput(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	ClothOutputJoints(srt, dispatchThreadId);
}

[numthreads(THREAD_COUNT, 1, 1)]
void Cs_ClothSimulateEuler(SrtData srt : S_SRT_DATA, uint dispatchThreadId : SV_DispatchThreadID)
{
	ClothSolveBackwardsEuler(srt, dispatchThreadId);
}

