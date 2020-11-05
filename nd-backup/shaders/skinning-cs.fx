#pragma argument (scheduler=latency)

#include "global-funcs.fxi"
#include "profile.fxi"

#define ND_PSSL
#include "compressed-vsharp.fxi"
#include "compressed-tangent-frame.fxi"

#if SKINNING_CS_MODE >= 0 && SKINNING_CS_MODE < 8
#define SKINNING_INFLUENCE_FORMAT        ((SKINNING_CS_MODE & 1) != 0 ? 32 : 64)
#define SKINNING_RIGID_ONLY              0 /*((SKINNING_CS_MODE & 2) != 0)*/
#define SKINNING_TPOSE_MODE              ((SKINNING_CS_MODE & 2) != 0)
#else
#error "Illegal skinning computer shader mode"
#endif

// The "correct" normal matrix should use the inverse transpose of the bone matrix, but this is slower.
#define SKINNING_USE_CORRECT_NORMAL_MATRIX  0

#define kNumThreadsInGroup	   64

#define kSkinPosition			0x01
#define kSkinPrevPosition		0x02
#define kSkinNormal				0x04
#define kSkinExtraNormal		0x08
#define kSkinTangent			0x10

// SKINNING_INFLUENCE_FORMAT, SKINNING_USE_CORRECT_NORMAL_MATRIX and kNumThreadsInGroup
// need to be defined before including skinning.fxi
#include "skinning.fxi"

struct SkinningInfoBuffer
{
    uint			m_numVertexes;
	uint			m_numBones;
	uint2			m_pad;
};

struct SkinningInputBuffers
{
	RWBuffer<float3>                     m_outPos;
	RWBuffer<float3>                     m_outNrm;
	RWBuffer<float4>                     m_outTan;
	RWBuffer<float3>                     m_outPosLastFrame;
	RWBuffer<float3>                     m_outExtraNrm;

	StructuredBuffer<SkinningVertexInfo> m_info;

	ByteAddressBuffer                    m_infl;
	ByteAddressBuffer                    m_bone;
	ByteAddressBuffer                    m_prev;

	CompressedVSharp                     m_pos;
	DataBuffer<float3>                   m_nrm;
	DataBuffer<float4>                   m_tan;
	CompressedVSharp                     m_enrm;
};

struct SkinningSrtData
{
	uint4								 m_globalVertBuffer; // This must be first so the compiler does not dead strip it on simpler versions of the skinning shader.
	SkinningInputBuffers *               m_bufs;	
	SkinningInfoBuffer *                 m_pInfo;
};

#if !SKINNING_RIGID_ONLY

void Skin(uint mode, uint3 groupThreadId, uint3 dispatchThreadId, SkinningSrtData srt)
{
	PROFILE_START(groupThreadId.x)

	uint numVertexes = srt.m_pInfo->m_numVertexes;
	uint numBones = srt.m_pInfo->m_numBones;

	uint vertexId = dispatchThreadId.x;
	if (vertexId < numVertexes)
	{
		SkinningVertexInfo vertexInfo = srt.m_bufs->m_info[vertexId];
		uint vertexDataOffset = vertexInfo.m_bufOffset;
		uint numInfluences = vertexInfo.m_numInfluences;

		float3 inputPos = LoadVertexAttribute<float3, 72>(srt.m_globalVertBuffer, srt.m_bufs->m_pos, vertexId);
		float3 inputENrm = LoadVertexAttribute<float3, 64>(srt.m_globalVertBuffer, srt.m_bufs->m_enrm, vertexId);

		float3 inputNrm;
		float4 inputTan;
		LoadCompressedTangentFrameNT(srt.m_bufs->m_nrm, srt.m_bufs->m_tan, inputNrm, inputTan, vertexId);

#if SKINNING_TPOSE_MODE
		float3 skinnedPos = inputPos;
		float3 skinnedNrm = inputNrm;
		float3 skinnedENrm = inputENrm;
		float4 skinnedTan = inputTan;
		float3 prevSkinnedPos = inputPos;
#else
		float3 skinnedPos = float3(0, 0, 0);
		float3 skinnedNrm = float3(0, 0, 0);
		float3 skinnedENrm = float3(0, 0, 0);
		float4 skinnedTan = float4(0, 0, 0, inputTan.w);
		uint skinnedDataOffset = vertexDataOffset;

		float3 prevSkinnedPos = float3(0, 0, 0);
		[loop]
		for (uint iInfluences = 0; iInfluences < numInfluences; iInfluences++)
		{
			uint influenceIdx;
			float influenceWeight;

			skinnedDataOffset = UnpackBoneInfluenceData(srt.m_bufs->m_infl, influenceIdx, influenceWeight, skinnedDataOffset);

			AddSkinnedInfluence(srt.m_bufs->m_bone, srt.m_bufs->m_prev, influenceIdx, influenceWeight,
								inputPos, skinnedPos, prevSkinnedPos,
								inputNrm, skinnedNrm,
								inputENrm, skinnedENrm,
								inputTan.xyz, skinnedTan);
		}

		skinnedNrm = normalize(skinnedNrm);
		skinnedENrm = normalize(skinnedENrm);

		float3 skinnedBinormal = cross(skinnedNrm, skinnedTan.xyz);
		skinnedTan.xyz = normalize(cross(skinnedBinormal, skinnedNrm));
#endif

		//
		// When mode contains position, prev position, normal, and tangent:
		// 32 / gcd(32, 10) = 32 / 2 = 16, half efficiency.
		//
		if (mode & kSkinPosition)
			srt.m_bufs->m_outPos[vertexId] = skinnedPos;
		if (mode & kSkinNormal)
			srt.m_bufs->m_outNrm[vertexId] = skinnedNrm;
		if (mode & kSkinTangent)
			srt.m_bufs->m_outTan[vertexId] = skinnedTan;
		if (mode & kSkinPrevPosition)
			srt.m_bufs->m_outPosLastFrame[vertexId] = prevSkinnedPos;
		if (mode & kSkinExtraNormal)
			srt.m_bufs->m_outExtraNrm[vertexId] = skinnedENrm;
	}

	PROFILE_END(groupThreadId.x)
}

#else // SKINNING_RIGID_ONLY

void Skin(uint mode, uint3 groupThreadId, uint3 dispatchThreadId, SkinningSrtData srt)
{
	PROFILE_START(groupThreadId.x)

	uint numVertexes = srt.m_pInfo->m_numVertexes;
	uint numBones = srt.m_pInfo->m_numBones;

	uint vertexId = dispatchThreadId.x;
	if (vertexId < numVertexes)
	{
		SkinningVertexInfo vertexInfo = srt.m_bufs->m_info[vertexId];
		uint skinnedDataOffset = vertexInfo.m_bufOffset;

		float3 inputPos = LoadVertexAttribute<float3, 72>(srt.m_globalVertBuffer, srt.m_bufs->m_pos, vertexId);
		float3 inputENrm = LoadVertexAttribute<float3, 64>(srt.m_globalVertBuffer,srt.m_bufs->m_enrm, vertexId);

		float3 inputNrm;
		float4 inputTan;
		LoadCompressedTangentFrameNT(srt.m_bufs->m_nrm, srt.m_bufs->m_tan, inputNrm, inputTan, vertexId);

		uint influenceIdx;
		float influenceWeight;
		UnpackBoneInfluenceData(srt.m_bufs->m_infl, influenceIdx, influenceWeight, skinnedDataOffset);

		float3 skinnedPos     = float3(0.0f, 0.0f, 0.0f);
		float3 prevSkinnedPos = float3(0.0f, 0.0f, 0.0f);
		float3 skinnedNrm     = float3(0.0f, 0.0f, 0.0f);
		float3 skinnedENrm    = float3(0.0f, 0.0f, 0.0f);
		float4 skinnedTan     = float4(0.0f, 0.0f, 0.0f, inputTan.w);

		AddSkinnedInfluence(srt.m_bufs->m_bone, srt.m_bufs->m_prev, influenceIdx, 1.0f,
							inputPos, skinnedPos, prevSkinnedPos,
							inputNrm, skinnedNrm,
							inputENrm, skinnedENrm,
							inputTan.xyz, skinnedTan);

		skinnedNrm = normalize(skinnedNrm);
		skinnedENrm = normalize(skinnedENrm);

		float3 skinnedBinormal = cross(skinnedNrm, skinnedTan.xyz);
		skinnedTan.xyz = normalize(cross(skinnedBinormal, skinnedNrm));

		if (mode & kSkinPosition)
			srt.m_bufs->m_outPos[vertexId] = skinnedPos;
		if (mode & kSkinNormal)
			srt.m_bufs->m_outNrm[vertexId] = skinnedNrm;
		if (mode & kSkinTangent)
			srt.m_bufs->m_outTan[vertexId] = skinnedTan;
		if (mode & kSkinPrevPosition)
			srt.m_bufs->m_outPosLastFrame[vertexId] = prevSkinnedPos;
		if (mode & kSkinExtraNormal)
			srt.m_bufs->m_outExtraNrm[vertexId] = skinnedENrm;
	}

	PROFILE_END(groupThreadId.x)
}

#endif // !SKINNING_RIGID_ONLY

[numthreads(kNumThreadsInGroup, 1, 1)]
void Cs_SkinningPosOnly(uint3 groupThreadId : SV_GroupThreadId,
						uint3 dispatchThreadId : SV_DispatchThreadId,
						SkinningSrtData srt : S_SRT_DATA)
{
	Skin(kSkinPosition, groupThreadId, dispatchThreadId, srt);
}

[numthreads(kNumThreadsInGroup, 1, 1)]
void Cs_SkinningPosNormal(uint3 groupThreadId : SV_GroupThreadId,
						  uint3 dispatchThreadId : SV_DispatchThreadId,
						  SkinningSrtData srt : S_SRT_DATA)
{
	Skin(kSkinPosition | kSkinPrevPosition | kSkinNormal,
		 groupThreadId, dispatchThreadId, srt);
}

[numthreads(kNumThreadsInGroup, 1, 1)]
void Cs_SkinningPosNormalTangent(uint3 groupThreadId : SV_GroupThreadId,
								 uint3 dispatchThreadId : SV_DispatchThreadId,
								 SkinningSrtData srt : S_SRT_DATA)
{
	Skin(kSkinPosition | kSkinPrevPosition | kSkinNormal | kSkinTangent,
		 groupThreadId, dispatchThreadId, srt);
}

[numthreads(kNumThreadsInGroup, 1, 1)]
void Cs_SkinningPosNormalExtraNormal(uint3 groupThreadId : SV_GroupThreadId,
									 uint3 dispatchThreadId : SV_DispatchThreadId,
									 SkinningSrtData srt : S_SRT_DATA)
{
	Skin(kSkinPosition | kSkinPrevPosition | kSkinNormal | kSkinExtraNormal,
		 groupThreadId, dispatchThreadId, srt);
}

[numthreads(kNumThreadsInGroup, 1, 1)]
void Cs_SkinningPosNormalTangentExtraNormal(uint3 groupThreadId : SV_GroupThreadId,
											uint3 dispatchThreadId : SV_DispatchThreadId,
											SkinningSrtData srt : S_SRT_DATA)
{
	Skin(kSkinPosition | kSkinPrevPosition | kSkinNormal | kSkinExtraNormal | kSkinTangent,
		 groupThreadId, dispatchThreadId, srt);
}

