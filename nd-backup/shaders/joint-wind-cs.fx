//
// Compute Shader - compute mesh normals after deformation
//

#include "global-funcs.fxi"
#include "snoise.fxi"

#define THREAD_COUNT 64

float3 m34MulPoint(in row_major float3x4 m, in float3 p)
{
	float3 pp;

	pp.x = (m[0].x * p.x) + (m[0].y * p.y) + (m[0].z * p.z) + m[0].w;
	pp.y = (m[1].x * p.x) + (m[1].y * p.y) + (m[1].z * p.z) + m[1].w;
	pp.z = (m[2].x * p.x) + (m[2].y * p.y) + (m[2].z * p.z) + m[2].w;

	return pp;
}

float3 m34MulVec(in row_major float3x4 m, in float3 v)
{
	float3 vp;

	vp.x = (m[0].x * v.x) + (m[0].y * v.y) + (m[0].z * v.z);
	vp.y = (m[1].x * v.x) + (m[1].y * v.y) + (m[1].z * v.z);
	vp.z = (m[2].x * v.x) + (m[2].y * v.y) + (m[2].z * v.z);

	return vp;
}

float3 m34GetTrans(in row_major float3x4 m)
{
	return float3(m[0].w, m[1].w, m[2].w);
}

row_major float3x4 mul34x34(in row_major float3x4 mA, in row_major float3x4 mB)
{
	row_major float3x4 m;

	m[0].x = mA[0].x * mB[0].x + mA[0].y * mB[1].x + mA[0].z * mB[2].x;
	m[0].y = mA[0].x * mB[0].y + mA[0].y * mB[1].y + mA[0].z * mB[2].y;
	m[0].z = mA[0].x * mB[0].z + mA[0].y * mB[1].z + mA[0].z * mB[2].z;

	m[1].x = mA[1].x * mB[0].x + mA[1].y * mB[1].x + mA[1].z * mB[2].x;
	m[1].y = mA[1].x * mB[0].y + mA[1].y * mB[1].y + mA[1].z * mB[2].y;
	m[1].z = mA[1].x * mB[0].z + mA[1].y * mB[1].z + mA[1].z * mB[2].z;

	m[2].x = mA[2].x * mB[0].x + mA[2].y * mB[1].x + mA[2].z * mB[2].x;
	m[2].y = mA[2].x * mB[0].y + mA[2].y * mB[1].y + mA[2].z * mB[2].y;
	m[2].z = mA[2].x * mB[0].z + mA[2].y * mB[1].z + mA[2].z * mB[2].z;

	float3 t = m34MulPoint(mA, m34GetTrans(mB));
	m[0].w = t.x;
	m[1].w = t.y;
	m[2].w = t.z;

	return m;
}

row_major float3x4 m34Inverse(in row_major float3x4 m)
{
	row_major float3x4 mp;

	mp[0].x = m[0].x;
	mp[0].y = m[1].x;
	mp[0].z = m[2].x;

	mp[1].x = m[0].y;
	mp[1].y = m[1].y;
	mp[1].z = m[2].y;

	mp[2].x = m[0].z;
	mp[2].y = m[1].z;
	mp[2].z = m[2].z;

	float3 t = m34MulVec(mp, -m34GetTrans(m));

	mp[0].w = t.x;
	mp[1].w = t.y;
	mp[2].w = t.z;

	return mp;
}

row_major float3x4 m34From44(in row_major float4x4 m)
{
	row_major float3x4 mp;

	mp[0].x = m[0].x;
	mp[0].y = m[1].x;
	mp[0].z = m[2].x;

	mp[1].x = m[0].y;
	mp[1].y = m[1].y;
	mp[1].z = m[2].y;

	mp[2].x = m[0].z;
	mp[2].y = m[1].z;
	mp[2].z = m[2].z;

	mp[0].w = m[3].x;
	mp[1].w = m[3].y;
	mp[2].w = m[3].z;

	return mp;
}

row_major float3x3 m33FromAxisAngle(in float3 axis, in float angle)
{
	float sinA, cosA;
	sincos(angle, sinA, cosA);

	float3 sAxis = sinA * axis;
	float3 cAxis = (1.0-cosA) * axis;

	float3 cAxisXX_YY_ZZ = cAxis * axis;
	float3 cAxisXY_YZ_ZX = cAxis * axis.yzx;

	row_major float3x3 m;
	m[0].x = cAxisXX_YY_ZZ.x + cosA;
	m[0].y = cAxisXY_YZ_ZX.x + sAxis.z;
	m[0].z = cAxisXY_YZ_ZX.z - sAxis.y;
	m[1].x = cAxisXY_YZ_ZX.x - sAxis.z;
	m[1].y = cAxisXX_YY_ZZ.y + cosA;
	m[1].z = cAxisXY_YZ_ZX.y + sAxis.x;
	m[2].x = cAxisXY_YZ_ZX.z + sAxis.y;
	m[2].y = cAxisXY_YZ_ZX.y - sAxis.x;
	m[2].z = cAxisXX_YY_ZZ.z + cosA;

	return m;
}

row_major float3x4 mul33x34(in row_major float3x3 mA, in row_major float3x4 mB)
{
	float3x4 m;

	m[0].x = mA[0].x * mB[0].x + mA[1].x * mB[1].x + mA[2].x * mB[2].x;
	m[0].y = mA[0].x * mB[0].y + mA[1].x * mB[1].y + mA[2].x * mB[2].y;
	m[0].z = mA[0].x * mB[0].z + mA[1].x * mB[1].z + mA[2].x * mB[2].z;

	m[1].x = mA[0].y * mB[0].x + mA[1].y * mB[1].x + mA[2].y * mB[2].x;
	m[1].y = mA[0].y * mB[0].y + mA[1].y * mB[1].y + mA[2].y * mB[2].y;
	m[1].z = mA[0].y * mB[0].z + mA[1].y * mB[1].z + mA[2].y * mB[2].z;

	m[2].x = mA[0].z * mB[0].x + mA[1].z * mB[1].x + mA[2].z * mB[2].x;
	m[2].y = mA[0].z * mB[0].y + mA[1].z * mB[1].y + mA[2].z * mB[2].y;
	m[2].z = mA[0].z * mB[0].z + mA[1].z * mB[1].z + mA[2].z * mB[2].z;

	m[0].w = mB[0].w;
	m[1].w = mB[1].w;
	m[2].w = mB[2].w;

	return m;
}

void m34setIdentity(row_major float3x4& m)
{
	m[0].x = 1;
	m[0].y = 0;
	m[0].z = 0;
	m[0].w = 0;
	m[1].x = 0;
	m[1].y = 1;
	m[1].z = 0;
	m[1].w = 0;
	m[2].x = 0;
	m[2].y = 0;
	m[2].z = 1;
	m[2].w = 0;
}

////////////////////////////////////////////////////////////////////
// Fg joint wind

struct JointWindConst
{
	float3 m_windDirection;
	float m_windSpeed;
	float m_turbulence;
	float m_windMultiplier;
	float m_time;
	uint m_numJoints;
};

struct JointWindDataIn
{
	float3 m_pos;
	float m_stiffness;
	uint m_flags;
};

struct JointWindDataOut
{
	float4 m_quat;
	uint m_flags;
};

void CalculateModalWindRotation(float time, float3 windDirection, float windSpeed, float turbulence, float windMultiplier, float3 pivotWS, 
	float stiffness, out float3 windRotationAxis, out float windRotationAngle)
{
	float windSpeed01 = windSpeed / 40.0;

	float stiffFactorLo   = saturate(windSpeed/20.0);
	float stiffFactorHi   = windSpeed01;

	float intensityBlend  = lerp(stiffFactorLo, stiffFactorHi, saturate( stiffness * 10));

	//precision issues when g_time is huge 
	float mod_time			= time;//fmod(float(double(motionConstants.time)), 86400.0);

	float3 objectUpAxis		= float3(0,1,0);
	windRotationAxis		= normalize(cross(objectUpAxis, windDirection));

	float baseOffset		= dot(windDirection, pivotWS);

	float variance			= cos( 1 * mod_time * 0.3 -  baseOffset * 0.1) * 0.5 + 0.5;

	float baseFreq          = lerp( lerp(5,3, saturate(stiffness*4)) , 1, stiffness);
	float freq2 			= baseFreq * (1 + stiffness);

	baseOffset				*= 1 + turbulence;
	float modeLo			= cos( baseOffset - mod_time * baseFreq);
	float modeHi			= cos( baseOffset - mod_time * freq2);

	float wind 				= lerp(modeLo, modeHi, variance);

	wind                    *= lerp(lerp(variance, 1, windSpeed01), 1, saturate(turbulence)*0.75);
	wind 					*= (1 - stiffness) * windSpeed01;

	float blendLo			= lerp(wind, wind*0.5, smoothstep(wind, 1,0));
	float blendHi           = wind * 0.5 + 0.5;

	windRotationAngle       =  lerp(blendLo, blendHi, intensityBlend * intensityBlend * (1- stiffness)) * windMultiplier;
}

float4 JointWind(float time, float3 windDirection, float windSpeed, float turbulence, float windMultiplier, float3 pivotWS, float stiffness)
{
	float3 windRotationAxis;
	float windRotationAngle;
	CalculateModalWindRotation(time, windDirection, windSpeed, turbulence, windMultiplier, pivotWS, stiffness, windRotationAxis, windRotationAngle);

	// AxisAngle rotation to quaternion
	float sinHalf, cosHalf;
	sincos(0.5*windRotationAngle, sinHalf, cosHalf);
	float4 quat = windRotationAxis.xyzz * sinHalf;
	quat.w = cosHalf;
	return normalize(quat);
}

struct JointWindBuffers
{
	StructuredBuffer<JointWindDataIn>	 m_in;
	RWStructuredBuffer<JointWindDataOut> m_out;
};

struct SrtData
{
	JointWindConst*		 m_pConsts;
	JointWindBuffers*	 m_pBuffs;	
};

[numthreads(THREAD_COUNT, 1, 1)]
void CS_JointWind(uint dispatchThreadId : SV_DispatchThreadID, SrtData srt : S_SRT_DATA)
{
	uint jointId = dispatchThreadId;
	if (jointId >= srt.m_pConsts->m_numJoints)
		return;

	JointWindConst* pCData = srt.m_pConsts;
	JointWindBuffers* pBuffs = srt.m_pBuffs;

	pBuffs->m_out[jointId].m_quat = JointWind(
		pCData->m_time,
		pCData->m_windDirection, 
		pCData->m_windSpeed,
		pCData->m_turbulence,
		pCData->m_windMultiplier,
		pBuffs->m_in[jointId].m_pos, 
		pBuffs->m_in[jointId].m_stiffness);
}


////////////////////////////////////////////////////////////////////
// Bg joint wind

struct BgJointWindConst
{
	float3 m_windDirection;
	float m_windSpeed;
	float m_turbulence;
	float m_windMultiplier;
	float m_time;
	float m_prevTime;
	uint m_numJoints;
	uint m_numInstances;
};

struct BgJointWindDataIn
{
	float m_stiffness;
	int m_parentIndex;
};

struct BgJointWindBuffers
{
	StructuredBuffer<BgJointWindDataIn>	 m_in;
	StructuredBuffer<row_major float4x4> m_instanceXfm;
	StructuredBuffer<row_major float3x4> m_invBindPose;
	RWStructuredBuffer<row_major float3x4> m_skinningOut;
	RWStructuredBuffer<row_major float3x4> m_prevSkinningOut;
};

struct BgSrtData
{
	BgJointWindConst*	 m_pConsts;
	BgJointWindBuffers*	 m_pBuffs;	
};

[numthreads(THREAD_COUNT, 1, 1)]
void CS_BgJointWind(uint dispatchThreadId : SV_DispatchThreadID, BgSrtData srt : S_SRT_DATA)
{
	uint iInstance = dispatchThreadId;
	if (iInstance >= srt.m_pConsts->m_numInstances)
		return;

	BgJointWindConst* pCData = srt.m_pConsts;
	BgJointWindBuffers* pBuffs = srt.m_pBuffs;

	row_major float3x4 objXfm = m34From44(pBuffs->m_instanceXfm[iInstance]);
	
	row_major float3x4 objXfmNoScale;
	objXfmNoScale[0].xyz = normalize(objXfm[0].xyz);
	objXfmNoScale[1].xyz = normalize(objXfm[1].xyz);
	objXfmNoScale[2].xyz = normalize(objXfm[2].xyz);
	objXfmNoScale[0].w = objXfm[0].w;
	objXfmNoScale[1].w = objXfm[1].w;
	objXfmNoScale[2].w = objXfm[2].w;

	row_major float3x4 objXfmInvNoScale = m34Inverse(objXfmNoScale);

	uint jointBase = iInstance * pCData->m_numJoints;

	for (uint iJoint = 0; iJoint<pCData->m_numJoints; iJoint++)
	{
		row_major float3x4 bindMtx = m34Inverse(pBuffs->m_invBindPose[iJoint]);

		// Joint Ws position before any wind deformation
		float3 posWs0 = m34MulPoint(objXfm, m34GetTrans(bindMtx));

		row_major float3x4 jointWsMtx;
		row_major float3x4 jointPrevWsMtx;

		int iParentJoint = pBuffs->m_in[iJoint].m_parentIndex;
		if (iParentJoint >= 0)
		{
			row_major float3x4 jointLsMtx = mul34x34(pBuffs->m_invBindPose[iParentJoint], bindMtx);
			jointWsMtx = mul34x34(pBuffs->m_skinningOut[jointBase + iParentJoint], jointLsMtx); // m_skinningOut stores jointWsMtx at this point
			jointPrevWsMtx = mul34x34(pBuffs->m_prevSkinningOut[jointBase + iParentJoint], jointLsMtx); // m_skinningOut stores jointWsMtx at this point
		}
		else
		{
			row_major float3x4 jointLsMtx = bindMtx;
			jointWsMtx = mul34x34(objXfmNoScale, jointLsMtx); 
			jointPrevWsMtx = jointWsMtx; 
		}

		if (pBuffs->m_in[iJoint].m_stiffness < 1.0)
		{
			{
				// Wind rotation
				float3 windRotationAxis;
				float windRotationAngle;
				CalculateModalWindRotation(
					pCData->m_time,
					pCData->m_windDirection, 
					pCData->m_windSpeed,
					pCData->m_turbulence,
					pCData->m_windMultiplier,
					posWs0, 
					pBuffs->m_in[iJoint].m_stiffness,
					windRotationAxis, 
					windRotationAngle);

				row_major float3x3 jointRotWs = m33FromAxisAngle(windRotationAxis, windRotationAngle);
				jointWsMtx = mul33x34(jointRotWs, jointWsMtx);
			}

			{
				// Prev Wind rotation
				float3 windRotationAxis;
				float windRotationAngle;
				CalculateModalWindRotation(
					pCData->m_prevTime,
					pCData->m_windDirection, 
					pCData->m_windSpeed,
					pCData->m_turbulence,
					pCData->m_windMultiplier,
					posWs0, 
					pBuffs->m_in[iJoint].m_stiffness,
					windRotationAxis, 
					windRotationAngle);

				row_major float3x3 jointRotWs = m33FromAxisAngle(windRotationAxis, windRotationAngle);
				jointPrevWsMtx = mul33x34(jointRotWs, jointPrevWsMtx);
			}
		}

		pBuffs->m_skinningOut[jointBase + iJoint] = jointWsMtx;
		pBuffs->m_prevSkinningOut[jointBase + iJoint] = jointPrevWsMtx;
	}

	// Convert jointWs into skinning mtx
	for (uint iJoint = 0; iJoint<pCData->m_numJoints; iJoint++)
	{
		{
			row_major float3x4 skinMtx = mul34x34(pBuffs->m_skinningOut[jointBase + iJoint], pBuffs->m_invBindPose[iJoint]);
			skinMtx = mul34x34(objXfmInvNoScale, skinMtx);
			pBuffs->m_skinningOut[jointBase + iJoint] = skinMtx;
		}

		{
			row_major float3x4 skinMtx = mul34x34(pBuffs->m_prevSkinningOut[jointBase + iJoint], pBuffs->m_invBindPose[iJoint]);
			skinMtx = mul34x34(objXfmInvNoScale, skinMtx);
			pBuffs->m_prevSkinningOut[jointBase + iJoint] = skinMtx;
		}
	}
}
