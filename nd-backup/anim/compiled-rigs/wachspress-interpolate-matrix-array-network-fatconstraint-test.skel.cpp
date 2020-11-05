#include <orbisanim/structs.h>
#include <orbisanim/joints.h>
#include <orbisanim/anim_perf.h>
#include <orbisanim/animhierarchy.h>
#include <orbisanim/commands.h>
#include <vectormath.h>
#include "ndlib/anim/rig-nodes/rig-nodes.h"

using namespace OrbisAnim;

namespace sceMath = sce::Vectormath::Simd;

#define max(a,b)	(((a) > (b)) ? (a) : (b))
#define min(a,b)	(((a) < (b)) ? (a) : (b))

extern "C" {
	void OrbisAnim_Parenting(uint16_t numJointQuads, OrbisAnim::HierarchyHeader const* const pHierarchyHeader, OrbisAnim::JointParentingQuad const* const pIndexTable);
}

static sceMath::Aos::Matrix3 Orthonormalize(sceMath::Aos::Transform3_arg mtx)
{
	sceMath::Aos::Vector3 vX = mtx.getCol0(), vY = mtx.getCol1(), vZ;
	vX = normalize(vX);
	vZ = normalize(cross(vX, vY));
	vY = cross(vZ, vX);	// should be normalized
	return sceMath::Aos::Matrix3(vX, vY, vZ);
}

static sceMath::Soa::Matrix3 Orthonormalize(sceMath::Soa::Transform3_arg mtx)
{
	sceMath::Soa::Vector3 vX = mtx.getCol0(), vY = mtx.getCol1(), vZ;
	vX = normalize(vX);
	vZ = normalize(cross(vX, vY));
	vY = cross(vZ, vX);	// should be normalized
	return sceMath::Soa::Matrix3(vX, vY, vZ);
}

static SMath::Quat QuaternionFromMatrix(SMath::Mat44 const &mtx)
{
	float trace = mtx[0][0] + mtx[1][1] + mtx[2][2];
	if (trace > 0.0f) {
		float s = 0.5f / sqrtf(1.0f + trace);
		return SMath::Quat(
			s * (mtx[1][2] - mtx[2][1]),
			s * (mtx[2][0] - mtx[0][2]),
			s * (mtx[0][1] - mtx[1][0]),
			0.25f / s);
	}
	else if (mtx[0][0] > mtx[1][1] && mtx[0][0] > mtx[2][2]) {
		float s = 0.5f / sqrtf(1.0f + mtx[0][0] - mtx[1][1] - mtx[2][2]);
		return SMath::Quat(
			0.25f / s,
			s * (mtx[0][1] + mtx[1][0]),
			s * (mtx[2][0] + mtx[0][2]),
			s * (mtx[1][2] - mtx[2][1]));
	}
	else if (mtx[1][1] > mtx[2][2]) {
		float s = 0.5f / sqrtf(1.0f + mtx[1][1] - mtx[0][0] - mtx[2][2]);
		return SMath::Quat(
			s * (mtx[0][1] + mtx[1][0]),
			0.25f / s,
			s * (mtx[1][2] + mtx[2][1]),
			s * (mtx[2][0] - mtx[0][2]));
	}
	else {
		float s = 0.5f / sqrtf(1.0f + mtx[2][2] - mtx[0][0] - mtx[1][1]);
		return SMath::Quat(
			s * (mtx[2][0] + mtx[0][2]),
			s * (mtx[1][2] + mtx[2][1]),
			0.25f / s,
			s * (mtx[0][1] - mtx[1][0]));
	}
}

static SMath::Quat AimQuaternion(SMath::Vec4 const& aim, SMath::Vec4 const& up)
{
	const float kEpsilon = 0.00000011921f;
	float aim_sqr = Dot3(aim, aim);
	SMath::Vec4 a = (aim_sqr == 0.0f) ? SMath::Vec4(SMath::kZero) : aim / sqrtf(aim_sqr);
	float a_dot_up = Dot3(a, up);
	float up_sqr = Dot3(up, up);
	float up_cross_a_sqr = up_sqr - a_dot_up * a_dot_up;

	if (up_cross_a_sqr * aim_sqr <= kEpsilon * up_sqr * aim_sqr) {
		//If aim cross up is 0 (if aim and up are parallel or either is 0), return the shortest rotation quaternion:
		SMath::Quat qAim0(0.0f, -a.Z(), a.Y(), 1.0f + a.X()); // shortest rotation quaternion
		return Normalize(qAim0);
	}
	SMath::Vec4 u = (up - a_dot_up * a) / sqrtf(up_cross_a_sqr);
	SMath::Mat44 mAim(a, u, Cross(a, u), SMath::Vec4(0, 0, 0, 1));
	return QuaternionFromMatrix(mAim);	// shouldn't need normalization, since we're passing an orthonormal matrix
}

// Joint[0]: |root
// Joint[1]: |root|input
// Joint[2]: |root|stateM1
// Joint[3]: |root|stateM2
// Joint[4]: |root|stateM3
// Joint[5]: |root|stateM4
// Joint[6]: |root|stateM5
// Joint[7]: |root|stateM6
// Joint[8]: |root|stateM7
// Joint[9]: |root|stateM8
// Joint[10]: |root|stateM9
// Joint[11]: |root_helper
// Joint[12]: |root_helper|output_space
// Joint[13]: |root_helper|output_space|output
// Joint[14]: |root_helper|output_driver

static void HierarchyConstantCommand_Seg0Cmd0(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const char* __restrict locJointParams = (const char*)pContext->m_locJointParams;
	const char* __restrict locSdkScalarTable = (const char*)pContext->m_locSdkScalarTable;

	ORBISANIM_MARKER_SCOPED("Const", OrbisAnim::Perf::kBlue);
	*(vec_float4*)(locJointParams + 0x0210) = sce_vectormath_hexfloat4i(0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000);	// (1.000000f, 1.000000f, 1.000000f, 1.000000f)
	*(vec_float4*)(locJointParams + 0x0240) = sce_vectormath_hexfloat4i(0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000);	// (1.000000f, 1.000000f, 1.000000f, 1.000000f)
	*(vec_float4*)(locJointParams + 0x0230) = sce_vectormath_hexfloat4i(0, 0, 0, 0x3f800000);	// (0.000000f, 0.000000f, 0.000000f, 1.000000f)
	*(vec_float4*)(locJointParams + 0x0250) = sce_vectormath_hexfloat4i(0, 0, 0, 0x3f800000);	// (0.000000f, 0.000000f, 0.000000f, 1.000000f)
	*(vec_float4*)(locJointParams + 0x0260) = sce_vectormath_hexfloat4i(0, 0, 0, 0x3f800000);	// (0.000000f, 0.000000f, 0.000000f, 1.000000f)
	*(vec_float4*)(locJointParams + 0x0270) = sce_vectormath_hexfloat4i(0, 0, 0, 0x3f800000);	// (0.000000f, 0.000000f, 0.000000f, 1.000000f)
	*(vec_float4*)(locJointParams + 0x0290) = sce_vectormath_hexfloat4i(0, 0, 0, 0x3f800000);	// (0.000000f, 0.000000f, 0.000000f, 1.000000f)
	*(vec_float4*)(locJointParams + 0x02A0) = sce_vectormath_hexfloat4i(0, 0, 0, 0x3f800000);	// (0.000000f, 0.000000f, 0.000000f, 1.000000f)
	*(vec_float4*)(locJointParams + 0x02C0) = sce_vectormath_hexfloat4i(0, 0, 0, 0x3f800000);	// (0.000000f, 0.000000f, 0.000000f, 1.000000f)
	*(vec_float4*)(locSdkScalarTable + 0x01C0) = sce_vectormath_hexfloat4i(0, 0, 0, 0);	// (0.000000f, 0.000000f, 0.000000f, 0.000000f)
	*(vec_float4*)(locSdkScalarTable + 0x01D0) = sce_vectormath_hexfloat4i(0, 0, 0, 0);	// (0.000000f, 0.000000f, 0.000000f, 0.000000f)
	*(vec_float4*)(locSdkScalarTable + 0x01E0) = sce_vectormath_hexfloat4i(0, 0, 0, 0);	// (0.000000f, 0.000000f, 0.000000f, 0.000000f)
	*(vec_float4*)(locSdkScalarTable + 0x01F0) = sce_vectormath_hexfloat4i(0, 0, 0, 0);	// (0.000000f, 0.000000f, 0.000000f, 0.000000f)
	*(vec_float4*)(locSdkScalarTable + 0x0200) = sce_vectormath_hexfloat4i(0, 0, 0, 0);	// (0.000000f, 0.000000f, 0.000000f, 0.000000f)
	*(vec_float4*)(locSdkScalarTable + 0x0210) = sce_vectormath_hexfloat4i(0, 0, 0, 0);	// (0.000000f, 0.000000f, 0.000000f, 0.000000f)
	*(vec_float4*)(locSdkScalarTable + 0x0220) = sce_vectormath_hexfloat4i(0, 0, 0, 0);	// (0.000000f, 0.000000f, 0.000000f, 0.000000f)
	*(vec_float4*)(locSdkScalarTable + 0x0230) = sce_vectormath_hexfloat4i(0, 0, 0, 0);	// (0.000000f, 0.000000f, 0.000000f, 0.000000f)
	*(vec_float4*)(locSdkScalarTable + 0x0240) = sce_vectormath_hexfloat4i(0, 0, 0, 0);	// (0.000000f, 0.000000f, 0.000000f, 0.000000f)
	*(vec_float4*)(locSdkScalarTable + 0x0240) = sce_vectormath_hexfloat4i(0, 0, 0, 0);	// (0.000000f, 0.000000f, 0.000000f, 0.000000f)
	*(vec_float4*)(locSdkScalarTable + 0x0240) = sce_vectormath_hexfloat4i(0, 0, 0, 0);	// (0.000000f, 0.000000f, 0.000000f, 0.000000f)
	*(uint32_t*)(locSdkScalarTable + 0x018C) = 0x3f800000;
	*(uint32_t*)(locSdkScalarTable + 0x0190) = 0;
	*(uint32_t*)(locSdkScalarTable + 0x0194) = 0;
	*(uint32_t*)(locSdkScalarTable + 0x0198) = 0;
	*(uint32_t*)(locSdkScalarTable + 0x01B4) = 0;
	*(uint32_t*)(locSdkScalarTable + 0x01B8) = 0;
	*(uint32_t*)(locSdkScalarTable + 0x01BC) = 0;
	*(uint32_t*)(locSdkScalarTable + 0x0250) = 0;
	*(uint32_t*)(locSdkScalarTable + 0x0254) = 0;
	*(uint32_t*)(locSdkScalarTable + 0x0254) = 0;
	*(uint32_t*)(locSdkScalarTable + 0x0254) = 0;
	*(uint32_t*)(locSdkScalarTable + 0x0254) = 0;
}

static void HierarchyRotConstraintCommand_Seg0Cmd1(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const char* __restrict locJointParams = (const char*)pContext->m_locJointParams;
	const char* __restrict locSdkScalarTable = (const char*)pContext->m_locSdkScalarTable;
	const char* __restrict locJointTransforms = (const char*)pContext->m_locJointTransforms;

	// RotateConstraintOperation
	// numSrcs: 1
	// numDst:  1
	{
		ORBISANIM_MARKER_SCOPED("RotConstraint", OrbisAnim::Perf::kGreen);
		sceMath::Soa::Matrix4 _mSrcParent(
			*(sceMath::Aos::Matrix4 const*)(locJointTransforms + 0x0000),
			*(sceMath::Aos::Matrix4 const*)(locJointTransforms + 0x0000),
			*(sceMath::Aos::Matrix4 const*)(locJointTransforms + 0x0000),
			*(sceMath::Aos::Matrix4 const*)(locJointTransforms + 0x0000));
		_mSrcParent = transpose(_mSrcParent);
		sceMath::Soa::Transform3 mSrcParent(_mSrcParent.getUpper3x3(), _mSrcParent.getTranslation());

		sceMath::Soa::Matrix4 _mDstParent(
			*(sceMath::Aos::Matrix4 const*)(locJointTransforms + 0x0000),
			*(sceMath::Aos::Matrix4 const*)(locJointTransforms + 0x0000),
			*(sceMath::Aos::Matrix4 const*)(locJointTransforms + 0x0000),
			*(sceMath::Aos::Matrix4 const*)(locJointTransforms + 0x0000));
		_mDstParent = transpose(_mDstParent);
		sceMath::Soa::Transform3 mDstParent(_mDstParent.getUpper3x3(), _mDstParent.getTranslation());
		mDstParent = prependScale(_mDstParent.getRow(3).getXYZ(), mDstParent);

		sceMath::Soa::Transform3 mSrcToDst = inverse(mDstParent) * mSrcParent;
		sceMath::Soa::Quat qSrcToDst(Orthonormalize(mSrcToDst));
		qSrcToDst = normalize(qSrcToDst);

		sceMath::Soa::Quat qSrc(
			*(sceMath::Aos::Quat const*)(locJointParams + 0x0010),
			*(sceMath::Aos::Quat const*)(locJointParams + 0x0010),
			*(sceMath::Aos::Quat const*)(locJointParams + 0x0010),
			*(sceMath::Aos::Quat const*)(locJointParams + 0x0010));

		sceMath::Soa::Quat qSourceOffset(
			sce_vectormath_hexfloat4i(0, 0, 0, 0),
			sce_vectormath_hexfloat4i(0, 0, 0, 0),
			sce_vectormath_hexfloat4i(0, 0, 0, 0),
			sce_vectormath_hexfloat4i(0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000));

		sceMath::Soa::Quat qOffsetSrc = qSrc * qSourceOffset;
		sceMath::Soa::Quat qDst = qSrcToDst * qOffsetSrc;

		sceMath::Aos::Vector4 fWeight(
			*(float const*)(locSdkScalarTable + 0x018C),
			*(float const*)(locSdkScalarTable + 0x018C),
			*(float const*)(locSdkScalarTable + 0x018C),
			*(float const*)(locSdkScalarTable + 0x018C));
		sceMath::Soa::Quat qSum(
			sceMath::Aos::Quat::zero(),
			sceMath::Aos::Quat::zero(),
			sceMath::Aos::Quat::zero(),
			sceMath::Aos::Quat::zero());
		qSum *= sce_vectormath_copysign(sce_vectormath_one_f4(), dot(qDst, qSum));
		sceMath::Soa::Quat qResult = qSum + qDst * fWeight.get128();
		qResult.get4Aos(
			*(sceMath::Aos::Quat*)(locJointParams + 0x0220),
			*(sceMath::Aos::Quat*)(locJointParams + 0x0220),
			*(sceMath::Aos::Quat*)(locJointParams + 0x0220),
			*(sceMath::Aos::Quat*)(locJointParams + 0x0220));
	}
	{
		sceMath::Soa::Quat finalRot(
			*(sceMath::Aos::Quat const*)(locJointParams + 0x0220),
			*(sceMath::Aos::Quat const*)(locJointParams + 0x0220),
			*(sceMath::Aos::Quat const*)(locJointParams + 0x0220),
			*(sceMath::Aos::Quat const*)(locJointParams + 0x0220));

		sceMath::Soa::Quat qRotAxis(
			sce_vectormath_hexfloat4i(0, 0, 0, 0),
			sce_vectormath_hexfloat4i(0, 0, 0, 0),
			sce_vectormath_hexfloat4i(0, 0, 0, 0),
			sce_vectormath_hexfloat4i(0x3f800000, 0x3f800000, 0x3f800000, 0x3f800000));

		finalRot = normalize(finalRot);
		finalRot = finalRot * qRotAxis;
		finalRot.get4Aos(
			*(sceMath::Aos::Quat*)(locJointParams + 0x0220),
			*(sceMath::Aos::Quat*)(locJointParams + 0x0220),
			*(sceMath::Aos::Quat*)(locJointParams + 0x0220),
			*(sceMath::Aos::Quat*)(locJointParams + 0x0220));

	}
}

// ndiNormalizeRangeX
static void NdiNormalizeRangeCommand_Seg0Cmd2(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{

	static CONST_EXPR OrbisAnim::CommandBlock::NormalizeRangeParams params = {
		0x498dfb40,
		0.000000f, 2.000000f,
		0.000000f, 2.000000f,
		0,
		0,
		OrbisAnim::Location(0x0050),
		OrbisAnim::Location(0x021E)
	};

	OrbisAnim::CommandBlock::ExecuteCommandNormalizeRangeImpl(&hierarchyHeader, &params);
}

// ndiNormalizeRangeY
static void NdiNormalizeRangeCommand_Seg0Cmd3(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{

	static CONST_EXPR OrbisAnim::CommandBlock::NormalizeRangeParams params = {
		0x498dfb40,
		0.000000f, 2.000000f,
		0.000000f, 2.000000f,
		0,
		0,
		OrbisAnim::Location(0x0054),
		OrbisAnim::Location(0x0222)
	};

	OrbisAnim::CommandBlock::ExecuteCommandNormalizeRangeImpl(&hierarchyHeader, &params);
}

// ndiNormalizeRangeZ
static void NdiNormalizeRangeCommand_Seg0Cmd4(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{

	static CONST_EXPR OrbisAnim::CommandBlock::NormalizeRangeParams params = {
		0x498dfb40,
		0.000000f, 2.000000f,
		0.000000f, 2.000000f,
		0,
		0,
		OrbisAnim::Location(0x0058),
		OrbisAnim::Location(0x0226)
	};

	OrbisAnim::CommandBlock::ExecuteCommandNormalizeRangeImpl(&hierarchyHeader, &params);
}

static void HierarchyParentingCommand_Seg0Cmd5(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{

	ORBISANIM_MARKER_SCOPED("Parent", OrbisAnim::Perf::kGray);
	OrbisAnim::JointParentingQuad::Element const parentingTable[] = {
		{ 0x0010, 0x0000 + 1 },	// Child[0]: root, Parent[-1]: <none>
		{ 0x0010, 0x0000 + 1 },	// Child[0]: root, Parent[-1]: <none>
		{ 0x0010, 0x0000 + 1 },	// Child[0]: root, Parent[-1]: <none>
		{ 0x0010, 0x0000 + 1 }	// Child[0]: root, Parent[-1]: <none>
	};

	OrbisAnim_Parenting(1, &hierarchyHeader, (OrbisAnim::JointParentingQuad const*)parentingTable);
}

static void HierarchyParentPosConstraintCommand_Seg0Cmd6(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const char* __restrict locJointParams = (const char*)pContext->m_locJointParams;
	const char* __restrict locSdkScalarTable = (const char*)pContext->m_locSdkScalarTable;
	const char* __restrict locJointTransforms = (const char*)pContext->m_locJointTransforms;

	// ParentPosConstraintOperation
	{
		ORBISANIM_MARKER_SCOPED("ParentPosConstraint", OrbisAnim::Perf::kPurple);
		SMath::Vec4 p(
			*(float const*)(locSdkScalarTable + 0x0190),
			*(float const*)(locSdkScalarTable + 0x0194),
			*(float const*)(locSdkScalarTable + 0x0198), 1.0f);

		float fWeight = *(float*)(locSdkScalarTable + 0x018C);
		const JointTransform* pJtSrcParent = (const JointTransform*)(locJointTransforms + 0x0040);
		SMath::Vec4 vWeightedTranslation = fWeight * pJtSrcParent->ApplyPoint(p);
		*(SMath::Vec4*)(locJointParams + 0x0230) = vWeightedTranslation;
	}
	{
		SMath::Vec4* pDst0 = (SMath::Vec4*)(locJointParams + 0x0230);
		SMath::Vec4* pDst1 = (SMath::Vec4*)(locJointParams + 0x0230);
		SMath::Vec4* pDst2 = (SMath::Vec4*)(locJointParams + 0x0230);
		SMath::Vec4* pDst3 = (SMath::Vec4*)(locJointParams + 0x0230);
		SMath::Vec4* pDst4 = (SMath::Vec4*)(locJointParams + 0x0230);
		SMath::Vec4* pDst5 = (SMath::Vec4*)(locJointParams + 0x0230);
		SMath::Vec4* pDst6 = (SMath::Vec4*)(locJointParams + 0x0230);
		SMath::Vec4* pDst7 = (SMath::Vec4*)(locJointParams + 0x0230);
		SMath::Vec4 vOutput0 = *pDst0;
		SMath::Vec4 vOutput1 = *pDst1;
		SMath::Vec4 vOutput2 = *pDst2;
		SMath::Vec4 vOutput3 = *pDst3;
		SMath::Vec4 vOutput4 = *pDst4;
		SMath::Vec4 vOutput5 = *pDst5;
		SMath::Vec4 vOutput6 = *pDst6;
		SMath::Vec4 vOutput7 = *pDst7;
		SMath::Vec4 vOutputW03(vOutput0.W(), vOutput1.W(), vOutput2.W(), vOutput3.W());
		SMath::Vec4 vOutputW45(vOutput4.W(), vOutput5.W(), vOutput6.W(), vOutput7.W());
		vOutputW03 = Recip(vOutputW03);
		vOutputW45 = Recip(vOutputW45);
		vOutput0 *= vOutputW03.X();
		vOutput1 *= vOutputW03.Y();
		vOutput2 *= vOutputW03.Z();
		vOutput3 *= vOutputW03.W();
		vOutput4 *= vOutputW45.X();
		vOutput5 *= vOutputW45.Y();
		vOutput6 *= vOutputW45.Z();
		vOutput7 *= vOutputW45.W();
		*pDst0 = vOutput0;
		*pDst1 = vOutput1;
		*pDst2 = vOutput2;
		*pDst3 = vOutput3;
		*pDst4 = vOutput4;
		*pDst5 = vOutput5;
		*pDst6 = vOutput6;
		*pDst7 = vOutput7;
	}
}

// ndiWachspress5
static void NdiWachspressCommand_Seg0Cmd7(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{

	static CONST_EXPR OrbisAnim::CommandBlock::WachspressParams params = {
		0x11BF70,
		5
	};

	static CONST_EXPR OrbisAnim::CommandBlock::WachspressParams::Entry s_entries[] = {
		{
			{
				{ 0.000000f, 0.000000f, 0.000000f },
				{ 2.000000f, 0.000000f, 0.000000f },
				{ 2.000000f, 2.000000f, 0.000000f },
				{ 0.000000f, 2.000000f, 0.000000f },
			},
			{ 0.000000f, 0.000000f, 0.000000f},
			{ OrbisAnim::Location(0x021E), OrbisAnim::Location(0x0222), OrbisAnim::Location(0x0226)},
			{ OrbisAnim::Location(0x01FE), OrbisAnim::Location(0x0202), OrbisAnim::Location(0x0206), OrbisAnim::Location(0x020A)},
		},
		{
			{
				{ 0.000000f, 0.000000f, 0.000000f },
				{ 1.000000f, 0.000000f, 0.000000f },
				{ 1.000000f, 1.000000f, 0.000000f },
				{ 0.000000f, 1.000000f, 0.000000f },
			},
			{ 0.000000f, 0.000000f, 0.000000f},
			{ OrbisAnim::Location(0x021E), OrbisAnim::Location(0x0222), OrbisAnim::Location(0x0226)},
			{ OrbisAnim::Location(0x020E), OrbisAnim::Location(0x0212), OrbisAnim::Location(0x0216), OrbisAnim::Location(0x021A)},
		},
		{
			{
				{ 1.000000f, 0.000000f, 0.000000f },
				{ 2.000000f, 0.000000f, 0.000000f },
				{ 2.000000f, 1.000000f, 0.000000f },
				{ 1.000000f, 1.000000f, 0.000000f },
			},
			{ 0.000000f, 0.000000f, 0.000000f},
			{ OrbisAnim::Location(0x021E), OrbisAnim::Location(0x0222), OrbisAnim::Location(0x0226)},
			{ OrbisAnim::Location(0x022A), OrbisAnim::Location(0x022E), OrbisAnim::Location(0x0232), OrbisAnim::Location(0x0236)},
		},
		{
			{
				{ 1.000000f, 1.000000f, 0.000000f },
				{ 2.000000f, 1.000000f, 0.000000f },
				{ 2.000000f, 2.000000f, 0.000000f },
				{ 1.000000f, 2.000000f, 0.000000f },
			},
			{ 0.000000f, 0.000000f, 0.000000f},
			{ OrbisAnim::Location(0x021E), OrbisAnim::Location(0x0222), OrbisAnim::Location(0x0226)},
			{ OrbisAnim::Location(0x023A), OrbisAnim::Location(0x023E), OrbisAnim::Location(0x0242), OrbisAnim::Location(0x0246)},
		},
		{
			{
				{ 0.000000f, 1.000000f, 0.000000f },
				{ 1.000000f, 1.000000f, 0.000000f },
				{ 1.000000f, 2.000000f, 0.000000f },
				{ 0.000000f, 2.000000f, 0.000000f },
			},
			{ 0.000000f, 0.000000f, 0.000000f},
			{ OrbisAnim::Location(0x021E), OrbisAnim::Location(0x0222), OrbisAnim::Location(0x0226)},
			{ OrbisAnim::Location(0x024A), OrbisAnim::Location(0x024E), OrbisAnim::Location(0x0252), OrbisAnim::Location(0x0256)},
		},
	};

	//const register U64 eip = asm("%eip");
	//Memory::PrefetchForLoad(eip)

	OrbisAnim::CommandBlock::ExecuteCommandWachspressImpl(&hierarchyHeader, &params, s_entries);
}

static void HierarchyParentingCommand_Seg0Cmd8(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{

	ORBISANIM_MARKER_SCOPED("Parent", OrbisAnim::Perf::kGray);
	OrbisAnim::JointParentingQuad::Element const parentingTable[] = {
		{ 0x0020, 0x0040 + 1 },	// Child[1]: input, Parent[0]: root
		{ 0x0030, 0x0040 + 1 },	// Child[2]: stateM1, Parent[0]: root
		{ 0x0040, 0x0040 + 1 },	// Child[3]: stateM2, Parent[0]: root
		{ 0x0050, 0x0040 + 1 },	// Child[4]: stateM3, Parent[0]: root
		{ 0x0060, 0x0040 + 1 },	// Child[5]: stateM4, Parent[0]: root
		{ 0x0070, 0x0040 + 1 },	// Child[6]: stateM5, Parent[0]: root
		{ 0x0080, 0x0040 + 1 },	// Child[7]: stateM6, Parent[0]: root
		{ 0x0090, 0x0040 + 1 },	// Child[8]: stateM7, Parent[0]: root
		{ 0x00A0, 0x0040 + 1 },	// Child[9]: stateM8, Parent[0]: root
		{ 0x00B0, 0x0040 + 1 },	// Child[10]: stateM9, Parent[0]: root
		{ 0x00B0, 0x0040 + 1 },	// Child[10]: stateM9, Parent[0]: root
		{ 0x00B0, 0x0040 + 1 }	// Child[10]: stateM9, Parent[0]: root
	};

	OrbisAnim_Parenting(3, &hierarchyHeader, (OrbisAnim::JointParentingQuad const*)parentingTable);
}

// ndiInterpolateMatrixArray1
static void NdiInterpolateMatrixArrayCommand_Seg0Cmd9(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{

	static CONST_EXPR OrbisAnim::CommandBlock::InterpolateMatrixArrayParams params = {
		0x498dfb30,
		4,
		1,
	};

	static CONST_EXPR OrbisAnim::CommandBlock::InterpolateMatrixArrayParams::Entry s_entries[] = {
		{
			4,
			OrbisAnim::CommandBlock::InterpolateMatrixArrayParams::Entry::EFlags(1),
			0,
			0,
			0,
			OrbisAnim::Location(0x0082)
		},
		{
			4,
			OrbisAnim::CommandBlock::InterpolateMatrixArrayParams::Entry::EFlags(1),
			8,
			4,
			4,
			OrbisAnim::Location(0x00C2)
		},
		{
			4,
			OrbisAnim::CommandBlock::InterpolateMatrixArrayParams::Entry::EFlags(1),
			16,
			8,
			8,
			OrbisAnim::Location(0x0102)
		},
		{
			4,
			OrbisAnim::CommandBlock::InterpolateMatrixArrayParams::Entry::EFlags(1),
			24,
			12,
			12,
			OrbisAnim::Location(0x0142)
		},
	};

	static CONST_EXPR ALIGNED(16) float s_matrixArray[] = {
		0.000000f, 0.000000f, 0.000000f, 0.000000f, // m_pos[0]
		1.000000f, 0.000000f, 0.000000f, 0.000000f, // m_pos[1]
		1.000000f, 1.000000f, 0.000000f, 0.000000f, // m_pos[2]
		0.000000f, 1.000000f, 0.000000f, 0.000000f, // m_pos[3]
		0.000000f, 0.000000f, 0.000000f, 1.000000f, // m_quats[0]
		-0.227073f, -0.217882f, -0.015670f, 0.949062f, // m_quats[1]
		0.158635f, -0.201534f, 0.024182f, 0.966247f, // m_quats[2]
		0.000000f, 0.000000f, 0.000000f, 1.000000f, // m_quats[3]
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // m_pad[0]
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // m_pad[1]
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // m_pad[2]
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // m_pad[3]
		1.000000f, 0.000000f, 0.000000f, 0.000000f, // m_pos[0]
		2.000000f, 0.000000f, 0.000000f, 0.000000f, // m_pos[1]
		2.000000f, 1.000000f, 0.000000f, 0.000000f, // m_pos[2]
		1.000000f, 1.000000f, 0.000000f, 0.000000f, // m_pos[3]
		-0.227073f, -0.217882f, -0.015670f, 0.949062f, // m_quats[0]
		-0.094252f, -0.300253f, -0.470311f, 0.824483f, // m_quats[1]
		0.157427f, -0.187070f, -0.491281f, 0.835981f, // m_quats[2]
		0.158635f, -0.201534f, 0.024182f, 0.966247f, // m_quats[3]
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // m_pad[0]
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // m_pad[1]
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // m_pad[2]
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // m_pad[3]
		1.000000f, 1.000000f, 0.000000f, 0.000000f, // m_pos[0]
		2.000000f, 1.000000f, 0.000000f, 0.000000f, // m_pos[1]
		2.000000f, 2.000000f, 0.000000f, 0.000000f, // m_pos[2]
		1.000000f, 2.000000f, 0.000000f, 0.000000f, // m_pos[3]
		0.158635f, -0.201534f, 0.024182f, 0.966247f, // m_quats[0]
		0.157427f, -0.187070f, -0.491281f, 0.835981f, // m_quats[1]
		-0.037210f, -0.118738f, -0.294616f, 0.947480f, // m_quats[2]
		0.188275f, -0.341465f, -0.049590f, 0.919508f, // m_quats[3]
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // m_pad[0]
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // m_pad[1]
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // m_pad[2]
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // m_pad[3]
		0.000000f, 1.000000f, 0.000000f, 0.000000f, // m_pos[0]
		1.000000f, 1.000000f, 0.000000f, 0.000000f, // m_pos[1]
		1.000000f, 2.000000f, 0.000000f, 0.000000f, // m_pos[2]
		0.000000f, 2.000000f, 0.000000f, 0.000000f, // m_pos[3]
		0.000000f, 0.000000f, 0.000000f, 1.000000f, // m_quats[0]
		0.158635f, -0.201534f, 0.024182f, 0.966247f, // m_quats[1]
		0.188275f, -0.341465f, -0.049590f, 0.919508f, // m_quats[2]
		0.000000f, 0.000000f, 0.000000f, 1.000000f, // m_quats[3]
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // m_pad[0]
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // m_pad[1]
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // m_pad[2]
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, // m_pad[3]
	};

	static CONST_EXPR float s_weightArray[] = {
		1.000000f, 0.000000f, 0.000000f, 0.000000f,
		0.666667f, 0.333333f, 0.000000f, 0.000000f,
		0.444444f, 0.222222f, 0.111111f, 0.222222f,
		0.666667f, 0.000000f, 0.000000f, 0.333333f,
	};

	static CONST_EXPR OrbisAnim::Location s_inputArray[] = {
		OrbisAnim::Location(0xFFFFFFFF), OrbisAnim::Location(0xFFFFFFFF), OrbisAnim::Location(0xFFFFFFFF), OrbisAnim::Location(0xFFFFFFFF), OrbisAnim::Location(0x020E), OrbisAnim::Location(0x0212), OrbisAnim::Location(0x0216), OrbisAnim::Location(0x021A),
		OrbisAnim::Location(0xFFFFFFFF), OrbisAnim::Location(0xFFFFFFFF), OrbisAnim::Location(0xFFFFFFFF), OrbisAnim::Location(0xFFFFFFFF), OrbisAnim::Location(0x022A), OrbisAnim::Location(0x022E), OrbisAnim::Location(0x0232), OrbisAnim::Location(0x0236),
		OrbisAnim::Location(0xFFFFFFFF), OrbisAnim::Location(0xFFFFFFFF), OrbisAnim::Location(0xFFFFFFFF), OrbisAnim::Location(0xFFFFFFFF), OrbisAnim::Location(0x023A), OrbisAnim::Location(0x023E), OrbisAnim::Location(0x0242), OrbisAnim::Location(0x0246),
		OrbisAnim::Location(0xFFFFFFFF), OrbisAnim::Location(0xFFFFFFFF), OrbisAnim::Location(0xFFFFFFFF), OrbisAnim::Location(0xFFFFFFFF), OrbisAnim::Location(0x024A), OrbisAnim::Location(0x024E), OrbisAnim::Location(0x0252), OrbisAnim::Location(0x0256),
	};

	OrbisAnim::CommandBlock::ExecuteCommandInterpolateMatrixArrayImpl(&hierarchyHeader, &params, s_entries, (const Transform*)s_matrixArray, s_weightArray, s_inputArray);
}

static void HierarchyParentingCommand_Seg0Cmd10(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{

	ORBISANIM_MARKER_SCOPED("Parent", OrbisAnim::Perf::kGray);
	OrbisAnim::JointParentingQuad::Element const parentingTable[] = {
		{ 0x00C0, 0x0000 + 1 },	// Child[11]: root_helper, Parent[-1]: <none>
		{ 0x00C0, 0x0000 + 1 },	// Child[11]: root_helper, Parent[-1]: <none>
		{ 0x00C0, 0x0000 + 1 },	// Child[11]: root_helper, Parent[-1]: <none>
		{ 0x00C0, 0x0000 + 1 }	// Child[11]: root_helper, Parent[-1]: <none>
	};

	OrbisAnim_Parenting(1, &hierarchyHeader, (OrbisAnim::JointParentingQuad const*)parentingTable);
}

// ndiInterpolateMatrixArray5
static void NdiInterpolateMatrixArrayCommand_Seg0Cmd11(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{

	static CONST_EXPR OrbisAnim::CommandBlock::InterpolateMatrixArrayParams params = {
		0x498dfb30,
		1,
		1,
	};

	static CONST_EXPR OrbisAnim::CommandBlock::InterpolateMatrixArrayParams::Entry s_entries[] = {
		{
			4,
			OrbisAnim::CommandBlock::InterpolateMatrixArrayParams::Entry::EFlags(0),
			0,
			0,
			0,
			OrbisAnim::Location(0x0042)
		},
	};

	static CONST_EXPR ALIGNED(16) float s_matrixArray[] = {
		1.000000f, 0.000000f, 0.000000f, 0.000000f, 0.000000f, 1.000000f, 0.000000f, 0.000000f, 0.000000f, 0.000000f, 1.000000f, 0.000000f, 0.000000f, 0.000000f, 0.000000f, 1.000000f,
		0.812140f, -0.228987f, 0.536650f, 0.000000f, 0.418599f, 0.869402f, -0.262517f, 0.000000f, -0.406452f, 0.437842f, 0.801930f, 0.000000f, 1.333333f, 0.000000f, 0.000000f, 1.000000f,
		0.853944f, -0.347966f, 0.386909f, 0.000000f, 0.214632f, 0.912871f, 0.347275f, 0.000000f, -0.474038f, -0.213510f, 0.854226f, 0.000000f, 1.333333f, 1.333333f, 0.000000f, 1.000000f,
		1.000000f, 0.000000f, 0.000000f, 0.000000f, 0.000000f, 1.000000f, 0.000000f, 0.000000f, 0.000000f, 0.000000f, 1.000000f, 0.000000f, 0.000000f, 1.333333f, 0.000000f, 1.000000f,
	};

	static CONST_EXPR float s_weightArray[] = {
		1.000000f, 0.000000f, 0.000000f, 0.000000f,
	};

	static CONST_EXPR OrbisAnim::Location s_inputArray[] = {
		OrbisAnim::Location(0x0082), OrbisAnim::Location(0x00C2), OrbisAnim::Location(0x0102), OrbisAnim::Location(0x0142), OrbisAnim::Location(0x01FE), OrbisAnim::Location(0x0202), OrbisAnim::Location(0x0206), OrbisAnim::Location(0x020A),
	};

	OrbisAnim::CommandBlock::ExecuteCommandInterpolateMatrixArrayImpl(&hierarchyHeader, &params, s_entries, (const Transform*)s_matrixArray, s_weightArray, s_inputArray);
}

static void HierarchyParentingCommand_Seg0Cmd12(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{

	ORBISANIM_MARKER_SCOPED("Parent", OrbisAnim::Perf::kGray);
	OrbisAnim::JointParentingQuad::Element const parentingTable[] = {
		{ 0x00D0, 0x0300 + 1 },	// Child[12]: output_space, Parent[11]: root_helper
		{ 0x00D0, 0x0300 + 1 },	// Child[12]: output_space, Parent[11]: root_helper
		{ 0x00D0, 0x0300 + 1 },	// Child[12]: output_space, Parent[11]: root_helper
		{ 0x00D0, 0x0300 + 1 }	// Child[12]: output_space, Parent[11]: root_helper
	};

	OrbisAnim_Parenting(1, &hierarchyHeader, (OrbisAnim::JointParentingQuad const*)parentingTable);
}

// ndiMultMatrix1
static void NdiMultMatrixCommand_Seg0Cmd13(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{

	static CONST_EXPR OrbisAnim::CommandBlock::MultMatrixParams params = {
		0x11BF67,
		1,
		0,
		OrbisAnim::Location(0x0002)
	};

	static CONST_EXPR float defaultValues[] = {
		1.000000f, 0.000000f, 0.000000f, 0.000000f,
		0.000000f, 1.000000f, 0.000000f, 0.000000f,
		0.000000f, 0.000000f, 1.000000f, 0.000000f,
		0.000000f, 0.000000f, 0.000000f, 1.000000f,
	};

	static CONST_EXPR OrbisAnim::Location inputLocs[] = {
		OrbisAnim::Location(0x0042),
	};

	static CONST_EXPR U8 invertMat[] = {
		0,
	};

	static CONST_EXPR U8 dataFormat[] = {
		3,
	};

	OrbisAnim::CommandBlock::ExecuteCommandMultMatrixImpl(&hierarchyHeader, &params, defaultValues, inputLocs, invertMat, dataFormat);
}

// ndiDecomposeMatrix1
static void NdiDecomposeMatrixCommand_Seg0Cmd14(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{

	static CONST_EXPR OrbisAnim::CommandBlock::DecomposeMatrixParams params = {
		0x11BF71,
		0,
		{ 0.000000f, 0.000000f, 0.000000f },
		OrbisAnim::Location(0x0002),
		{
			OrbisAnim::Location(0x01DA),
			OrbisAnim::Location(0x01DE),
			OrbisAnim::Location(0x01E2),
		},
		{
			OrbisAnim::Location(0x01E6),
			OrbisAnim::Location(0x01EA),
			OrbisAnim::Location(0x01EE),
		},
		{
			OrbisAnim::Location(0x01F2),
			OrbisAnim::Location(0x01F6),
			OrbisAnim::Location(0x01FA)
		}
	};

	OrbisAnim::CommandBlock::ExecuteCommandDecomposeMatrixImpl(&hierarchyHeader, &params);
}

static void HierarchyCopyCommand_Seg0Cmd15(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const char* __restrict locJointParams = (const char*)pContext->m_locJointParams;
	const char* __restrict locSdkScalarTable = (const char*)pContext->m_locSdkScalarTable;

	ORBISANIM_MARKER_SCOPED("Copy", OrbisAnim::Perf::kYellow);
	*(float*)(locJointParams + 0x02C0) = *(float*)(locSdkScalarTable + 0x01D8);
	*(float*)(locJointParams + 0x02C4) = *(float*)(locSdkScalarTable + 0x01DC);
	*(float*)(locJointParams + 0x02C8) = *(float*)(locSdkScalarTable + 0x01E0);
	*(float*)(locSdkScalarTable + 0x01A0) = *(float*)(locSdkScalarTable + 0x01E4);
	*(float*)(locSdkScalarTable + 0x01A8) = *(float*)(locSdkScalarTable + 0x01E8);
	*(float*)(locSdkScalarTable + 0x01B0) = *(float*)(locSdkScalarTable + 0x01EC);
	*(float*)(locJointParams + 0x02A0) = *(float*)(locSdkScalarTable + 0x01F0);
	*(float*)(locJointParams + 0x02A4) = *(float*)(locSdkScalarTable + 0x01F4);
	*(float*)(locJointParams + 0x02A8) = *(float*)(locSdkScalarTable + 0x01F8);
	*(float*)(locJointParams + 0x02A8) = *(float*)(locSdkScalarTable + 0x01F8);
	*(float*)(locJointParams + 0x02A8) = *(float*)(locSdkScalarTable + 0x01F8);
	*(float*)(locJointParams + 0x02A8) = *(float*)(locSdkScalarTable + 0x01F8);
}

static void HierarchySdkDrivenRotCommand_Seg0Cmd16(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const char* __restrict locJointParams = (const char*)pContext->m_locJointParams;
	const char* __restrict locSdkScalarTable = (const char*)pContext->m_locSdkScalarTable;

	// SdkDrivenRotOperation (EulerToQuat)
	{
		ORBISANIM_MARKER_SCOPED("Euler2Quat", OrbisAnim::Perf::kMagenta);
		sceMath::Aos::Vector3 euler(
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01A0)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01A8)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01B0)));
		sceMath::Aos::Quat q = sceMath::Aos::Quat::rotation(euler, sceMath::kXYZ);
		*(sceMath::Aos::Quat*)(locJointParams + 0x02B0) = q;
	}
	// SdkDrivenRotOperation (EulerToQuat)
	{
		ORBISANIM_MARKER_SCOPED("Euler2Quat", OrbisAnim::Perf::kMagenta);
		sceMath::Aos::Vector3 euler(
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01A0)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01A8)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01B0)));
		sceMath::Aos::Quat q = sceMath::Aos::Quat::rotation(euler, sceMath::kXYZ);
		*(sceMath::Aos::Quat*)(locJointParams + 0x02B0) = q;
	}
	// SdkDrivenRotOperation (EulerToQuat)
	{
		ORBISANIM_MARKER_SCOPED("Euler2Quat", OrbisAnim::Perf::kMagenta);
		sceMath::Aos::Vector3 euler(
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01A0)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01A8)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01B0)));
		sceMath::Aos::Quat q = sceMath::Aos::Quat::rotation(euler, sceMath::kXYZ);
		*(sceMath::Aos::Quat*)(locJointParams + 0x02B0) = q;
	}
	// SdkDrivenRotOperation (EulerToQuat)
	{
		ORBISANIM_MARKER_SCOPED("Euler2Quat", OrbisAnim::Perf::kMagenta);
		sceMath::Aos::Vector3 euler(
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01A0)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01A8)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01B0)));
		sceMath::Aos::Quat q = sceMath::Aos::Quat::rotation(euler, sceMath::kXYZ);
		*(sceMath::Aos::Quat*)(locJointParams + 0x02B0) = q;
	}
}

static void HierarchyParentingCommand_Seg0Cmd17(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{

	ORBISANIM_MARKER_SCOPED("Parent", OrbisAnim::Perf::kGray);
	OrbisAnim::JointParentingQuad::Element const parentingTable[] = {
		{ 0x00F0, 0x0300 + 1 },	// Child[14]: output_driver, Parent[11]: root_helper
		{ 0x00F0, 0x0300 + 1 },	// Child[14]: output_driver, Parent[11]: root_helper
		{ 0x00F0, 0x0300 + 1 },	// Child[14]: output_driver, Parent[11]: root_helper
		{ 0x00F0, 0x0300 + 1 }	// Child[14]: output_driver, Parent[11]: root_helper
	};

	OrbisAnim_Parenting(1, &hierarchyHeader, (OrbisAnim::JointParentingQuad const*)parentingTable);
}

// |root_helper|output_space|output|output_fatConst
static void NdiFatConstraintCommand_Seg0Cmd18(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{

	static CONST_EXPR OrbisAnim::CommandBlock::FatConstraintParams params = {
		0xD6847,
		OrbisAnim::Location(0x03C1),
		OrbisAnim::Location(0x0301),
		OrbisAnim::Location(0x03C1),
		OrbisAnim::Location(0x0341),
		OrbisAnim::Location(0x0182),
		OrbisAnim::Location(0x0186),
		OrbisAnim::Location(0x018A),
		{ 1.000000f, 0.000000f, 0.000000f, 0.000000f, 0.000000f, 1.000000f, 0.000000f, 0.000000f, 0.000000f, 0.000000f, 1.000000f, 0.000000f, 0.000000f, 0.000000f, 0.000000f, 1.000000f, },
		1.000000f,
		1.000000f,
		1.000000f,

		1.000000f,
		0.900000f,
		0.200000f,

		{ 0.100000f, 0.100000f, 0.100000f },
		{ -0.100000f, -0.100000f, -0.100000f },
		1.000000f,
		0x0000,

		{
			OrbisAnim::Location(0x01C2),
			OrbisAnim::Location(0x01C6),
			OrbisAnim::Location(0x01CA),
			OrbisAnim::Location(0x01CE),
			OrbisAnim::Location(0x01D2),
			OrbisAnim::Location(0x01D6),
			OrbisAnim::Location(0x01B6),
			OrbisAnim::Location(0x01BA),
			OrbisAnim::Location(0x01BE)
		},

		0x0000,
		SID("|root_helper|output_space|output|output_fatConst")
	};

	OrbisAnim::CommandBlock::ExecuteCommandFatConstraintImpl(&hierarchyHeader, &params, pContext);
}

static void HierarchyCopyCommand_Seg0Cmd19(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const char* __restrict locJointParams = (const char*)pContext->m_locJointParams;
	const char* __restrict locSdkScalarTable = (const char*)pContext->m_locSdkScalarTable;

	ORBISANIM_MARKER_SCOPED("Copy", OrbisAnim::Perf::kYellow);
	*(float*)(locJointParams + 0x0290) = *(float*)(locSdkScalarTable + 0x01B4);
	*(float*)(locJointParams + 0x0294) = *(float*)(locSdkScalarTable + 0x01B8);
	*(float*)(locJointParams + 0x0298) = *(float*)(locSdkScalarTable + 0x01BC);
	*(float*)(locSdkScalarTable + 0x019C) = *(float*)(locSdkScalarTable + 0x01C0);
	*(float*)(locSdkScalarTable + 0x01A4) = *(float*)(locSdkScalarTable + 0x01C4);
	*(float*)(locSdkScalarTable + 0x01AC) = *(float*)(locSdkScalarTable + 0x01C8);
	*(float*)(locJointParams + 0x0270) = *(float*)(locSdkScalarTable + 0x01CC);
	*(float*)(locJointParams + 0x0274) = *(float*)(locSdkScalarTable + 0x01D0);
	*(float*)(locJointParams + 0x0278) = *(float*)(locSdkScalarTable + 0x01D4);
	*(float*)(locJointParams + 0x0278) = *(float*)(locSdkScalarTable + 0x01D4);
	*(float*)(locJointParams + 0x0278) = *(float*)(locSdkScalarTable + 0x01D4);
	*(float*)(locJointParams + 0x0278) = *(float*)(locSdkScalarTable + 0x01D4);
}

static void HierarchySdkDrivenRotCommand_Seg0Cmd20(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const char* __restrict locJointParams = (const char*)pContext->m_locJointParams;
	const char* __restrict locSdkScalarTable = (const char*)pContext->m_locSdkScalarTable;

	// SdkDrivenRotOperation (EulerToQuat)
	{
		ORBISANIM_MARKER_SCOPED("Euler2Quat", OrbisAnim::Perf::kMagenta);
		sceMath::Aos::Vector3 euler(
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x019C)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01A4)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01AC)));
		sceMath::Aos::Quat q = sceMath::Aos::Quat::rotation(euler, sceMath::kXYZ);
		*(sceMath::Aos::Quat*)(locJointParams + 0x0280) = q;
	}
	// SdkDrivenRotOperation (EulerToQuat)
	{
		ORBISANIM_MARKER_SCOPED("Euler2Quat", OrbisAnim::Perf::kMagenta);
		sceMath::Aos::Vector3 euler(
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x019C)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01A4)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01AC)));
		sceMath::Aos::Quat q = sceMath::Aos::Quat::rotation(euler, sceMath::kXYZ);
		*(sceMath::Aos::Quat*)(locJointParams + 0x0280) = q;
	}
	// SdkDrivenRotOperation (EulerToQuat)
	{
		ORBISANIM_MARKER_SCOPED("Euler2Quat", OrbisAnim::Perf::kMagenta);
		sceMath::Aos::Vector3 euler(
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x019C)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01A4)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01AC)));
		sceMath::Aos::Quat q = sceMath::Aos::Quat::rotation(euler, sceMath::kXYZ);
		*(sceMath::Aos::Quat*)(locJointParams + 0x0280) = q;
	}
	// SdkDrivenRotOperation (EulerToQuat)
	{
		ORBISANIM_MARKER_SCOPED("Euler2Quat", OrbisAnim::Perf::kMagenta);
		sceMath::Aos::Vector3 euler(
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x019C)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01A4)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x01AC)));
		sceMath::Aos::Quat q = sceMath::Aos::Quat::rotation(euler, sceMath::kXYZ);
		*(sceMath::Aos::Quat*)(locJointParams + 0x0280) = q;
	}
}

static void HierarchyParentingCommand_Seg0Cmd21(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{

	ORBISANIM_MARKER_SCOPED("Parent", OrbisAnim::Perf::kGray);
	OrbisAnim::JointParentingQuad::Element const parentingTable[] = {
		{ 0x00E0, 0x0340 + 1 },	// Child[13]: output, Parent[12]: output_space
		{ 0x00E0, 0x0340 + 1 },	// Child[13]: output, Parent[12]: output_space
		{ 0x00E0, 0x0340 + 1 },	// Child[13]: output, Parent[12]: output_space
		{ 0x00E0, 0x0340 + 1 }	// Child[13]: output, Parent[12]: output_space
	};

	OrbisAnim_Parenting(1, &hierarchyHeader, (OrbisAnim::JointParentingQuad const*)parentingTable);
}

static void DebugPrint1(const SegmentContext* pContext)
{
}

static void DebugPrint2(const SegmentContext* pContext)
{

}

static Status EvaluateRigSeg0(SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	HierarchyConstantCommand_Seg0Cmd0(pContext, hierarchyHeader);
	HierarchyRotConstraintCommand_Seg0Cmd1(pContext, hierarchyHeader);
	NdiNormalizeRangeCommand_Seg0Cmd2(pContext, hierarchyHeader);
	NdiNormalizeRangeCommand_Seg0Cmd3(pContext, hierarchyHeader);
	NdiNormalizeRangeCommand_Seg0Cmd4(pContext, hierarchyHeader);
	HierarchyParentingCommand_Seg0Cmd5(pContext, hierarchyHeader);
	HierarchyParentPosConstraintCommand_Seg0Cmd6(pContext, hierarchyHeader);
	NdiWachspressCommand_Seg0Cmd7(pContext, hierarchyHeader);
	DebugPrint1(pContext);
	HierarchyParentingCommand_Seg0Cmd8(pContext, hierarchyHeader);
	DebugPrint1(pContext);
	NdiInterpolateMatrixArrayCommand_Seg0Cmd9(pContext, hierarchyHeader);
	HierarchyParentingCommand_Seg0Cmd10(pContext, hierarchyHeader);
	NdiInterpolateMatrixArrayCommand_Seg0Cmd11(pContext, hierarchyHeader);
	HierarchyParentingCommand_Seg0Cmd12(pContext, hierarchyHeader);
	NdiMultMatrixCommand_Seg0Cmd13(pContext, hierarchyHeader);
	NdiDecomposeMatrixCommand_Seg0Cmd14(pContext, hierarchyHeader);
	HierarchyCopyCommand_Seg0Cmd15(pContext, hierarchyHeader);
	HierarchySdkDrivenRotCommand_Seg0Cmd16(pContext, hierarchyHeader);
	HierarchyParentingCommand_Seg0Cmd17(pContext, hierarchyHeader);
	NdiFatConstraintCommand_Seg0Cmd18(pContext, hierarchyHeader);
	HierarchyCopyCommand_Seg0Cmd19(pContext, hierarchyHeader);
	HierarchySdkDrivenRotCommand_Seg0Cmd20(pContext, hierarchyHeader);
	HierarchyParentingCommand_Seg0Cmd21(pContext, hierarchyHeader);

	return kSuccess;
}

Status EvaluateWachspressInterpolateMatrixArrayNetworkFatconstraintTestRig(U32 hierarchyId, SegmentContext* pContext)
{
	if (0xea166910 != hierarchyId)
		return kFatalErrorMismatch;

	const uint32_t kNumSegments = 1;
	typedef Status(*RigEvalFn)(SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader);
	static RigEvalFn rigEvalFns[kNumSegments] =
	{
		EvaluateRigSeg0,
	};

	OrbisAnim::HierarchyHeader hierarchyHeader;
	hierarchyHeader.m_pJointParams = (void*)pContext->m_locJointParams;
	hierarchyHeader.m_pJointTransforms = (void*)pContext->m_locJointTransforms;
	hierarchyHeader.m_pScalarTable = (float*)pContext->m_locSdkScalarTable;
	hierarchyHeader.m_pDependencyTable = (void*)pContext->m_pDependencyTable;

	return rigEvalFns[pContext->m_iSegment](pContext, hierarchyHeader);
}

