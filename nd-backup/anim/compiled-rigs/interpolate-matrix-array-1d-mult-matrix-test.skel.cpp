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
// Joint[1]: |root|driver
// Joint[2]: |root|state0
// Joint[3]: |root|state1
// Joint[4]: |root|state2
// Joint[5]: |root|state3
// Joint[6]: |driven

static void HierarchyConstantCommand_Seg0Cmd0(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const uintptr_t locJointParams = (uintptr_t)pContext->m_locJointParams; (void)locJointParams;
	const uintptr_t locSdkScalarTable = (uintptr_t)pContext->m_locSdkScalarTable; (void)locSdkScalarTable;
	const uintptr_t locJointTransforms = (uintptr_t)pContext->m_locJointTransforms; (void)locJointTransforms;
	const uintptr_t pDependencyTable = (uintptr_t)pContext->m_pDependencyTable; (void)pDependencyTable;

	ORBISANIM_MARKER_SCOPED("Const", OrbisAnim::Perf::kBlue);
	*(vec_float4*)(locJointParams + 0x0120) = sce_vectormath_hexfloat4i(0, 0, 0, 0x3f800000);	// (0, 0, 0, 1)
	*(vec_float4*)(locJointParams + 0x0140) = sce_vectormath_hexfloat4i(0, 0, 0, 0x3f800000);	// (0, 0, 0, 1)
	*(vec_float4*)(locSdkScalarTable + 0x0090) = sce_vectormath_hexfloat4i(0, 0, 0, 0);	// (0, 0, 0, 0)
	*(vec_float4*)(locSdkScalarTable + 0x00A0) = sce_vectormath_hexfloat4i(0, 0, 0, 0);	// (0, 0, 0, 0)
	*(uint32_t*)(locSdkScalarTable + 0x008C) = 0;
	*(uint32_t*)(locSdkScalarTable + 0x008C) = 0;
	*(uint32_t*)(locSdkScalarTable + 0x008C) = 0;
	*(uint32_t*)(locSdkScalarTable + 0x008C) = 0;
}

// ndiInterpolateMatrix1D1
static void NdiInterpolateMatrix1DCommand_Seg0Cmd1(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const uintptr_t locJointParams = (uintptr_t)pContext->m_locJointParams; (void)locJointParams;
	const uintptr_t locSdkScalarTable = (uintptr_t)pContext->m_locSdkScalarTable; (void)locSdkScalarTable;
	const uintptr_t locJointTransforms = (uintptr_t)pContext->m_locJointTransforms; (void)locJointTransforms;
	const uintptr_t pDependencyTable = (uintptr_t)pContext->m_pDependencyTable; (void)pDependencyTable;

	OrbisAnim::CommandBlock::InterpolateMatrix1DParams params;
	params.m_mayaNodeTypeId = 0x11BF64;
	params.m_numEntries = 1;
	static CONST_EXPR OrbisAnim::CommandBlock::InterpolateMatrix1DParams::Entry s_entries[] = {
		{ 0, 0, 0, 0, 4, 0, OrbisAnim::Location(0x0054), OrbisAnim::Location(0x0042) },
	};
	static CONST_EXPR OrbisAnim::CommandBlock::InterpolateMatrix1DState s_stateData[] = {
		{ -1, { 0.605326, -0.719289, 0.340887, 0, 0.202611, 0.553392, 0.807902, 0, -0.769759, -0.419977, 0.480719, 0, 0, -1, 0, 1 } },
		{ 0, { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 } },
		{ 2, { 0.745519, 0.0549673, -0.664214, 0, 0.529456, 0.556481, 0.640317, 0, 0.404819, -0.82904, 0.385764, 0, 0, 2, 0, 1 } },
		{ 4, { 0.280647, 0.201004, -0.938528, 0, -0.39505, 0.91535, 0.0779088, 0, 0.874742, 0.3489, 0.336297, 0, 0, 4, 0, 1 } },
	};
	OrbisAnim::CommandBlock::ExecuteCommandInterpolateMatrix1DImpl(&hierarchyHeader, &params, s_entries, s_stateData);
}

static void HierarchyParentingCommand_Seg0Cmd2(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const uintptr_t locJointParams = (uintptr_t)pContext->m_locJointParams; (void)locJointParams;
	const uintptr_t locSdkScalarTable = (uintptr_t)pContext->m_locSdkScalarTable; (void)locSdkScalarTable;
	const uintptr_t locJointTransforms = (uintptr_t)pContext->m_locJointTransforms; (void)locJointTransforms;
	const uintptr_t pDependencyTable = (uintptr_t)pContext->m_pDependencyTable; (void)pDependencyTable;

	ORBISANIM_MARKER_SCOPED("Parent", OrbisAnim::Perf::kGray);
	OrbisAnim::JointParentingQuad::Element const parentingTable[] = {
		{ 0x0010, 0x0000 + 1 },	// Child[0]: root, Parent[-1]: <none>
		{ 0x0010, 0x0000 + 1 },	// Child[0]: root, Parent[-1]: <none>
		{ 0x0010, 0x0000 + 1 },	// Child[0]: root, Parent[-1]: <none>
		{ 0x0010, 0x0000 + 1 }	// Child[0]: root, Parent[-1]: <none>
	};

	OrbisAnim_Parenting(1, &hierarchyHeader, (OrbisAnim::JointParentingQuad const*)parentingTable);
}

// ndiMultMatrix1
static void NdiMultMatrixCommand_Seg0Cmd3(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const uintptr_t locJointParams = (uintptr_t)pContext->m_locJointParams; (void)locJointParams;
	const uintptr_t locSdkScalarTable = (uintptr_t)pContext->m_locSdkScalarTable; (void)locSdkScalarTable;
	const uintptr_t locJointTransforms = (uintptr_t)pContext->m_locJointTransforms; (void)locJointTransforms;
	const uintptr_t pDependencyTable = (uintptr_t)pContext->m_pDependencyTable; (void)pDependencyTable;

	OrbisAnim::CommandBlock::MultMatrixParams params;
	params.m_mayaNodeTypeId = 0x11BF67;
	params.m_numInputs = 1;
	params.m_inputArrayOffset = 0;
	params.m_outputMatrixLoc = OrbisAnim::Location(0x0002);

	static CONST_EXPR float defaultValues[] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1,
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

static void HierarchyParentingCommand_Seg0Cmd4(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const uintptr_t locJointParams = (uintptr_t)pContext->m_locJointParams; (void)locJointParams;
	const uintptr_t locSdkScalarTable = (uintptr_t)pContext->m_locSdkScalarTable; (void)locSdkScalarTable;
	const uintptr_t locJointTransforms = (uintptr_t)pContext->m_locJointTransforms; (void)locJointTransforms;
	const uintptr_t pDependencyTable = (uintptr_t)pContext->m_pDependencyTable; (void)pDependencyTable;

	ORBISANIM_MARKER_SCOPED("Parent", OrbisAnim::Perf::kGray);
	OrbisAnim::JointParentingQuad::Element const parentingTable[] = {
		{ 0x0020, 0x0040 + 1 },	// Child[1]: driver, Parent[0]: root
		{ 0x0030, 0x0040 + 1 },	// Child[2]: state0, Parent[0]: root
		{ 0x0040, 0x0040 + 1 },	// Child[3]: state1, Parent[0]: root
		{ 0x0050, 0x0040 + 1 },	// Child[4]: state2, Parent[0]: root
		{ 0x0060, 0x0040 + 1 },	// Child[5]: state3, Parent[0]: root
		{ 0x0060, 0x0040 + 1 },	// Child[5]: state3, Parent[0]: root
		{ 0x0060, 0x0040 + 1 },	// Child[5]: state3, Parent[0]: root
		{ 0x0060, 0x0040 + 1 }	// Child[5]: state3, Parent[0]: root
	};

	OrbisAnim_Parenting(2, &hierarchyHeader, (OrbisAnim::JointParentingQuad const*)parentingTable);
}

// ndiDecomposeMatrix1
static void NdiDecomposeMatrixCommand_Seg0Cmd5(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const uintptr_t locJointParams = (uintptr_t)pContext->m_locJointParams; (void)locJointParams;
	const uintptr_t locSdkScalarTable = (uintptr_t)pContext->m_locSdkScalarTable; (void)locSdkScalarTable;
	const uintptr_t locJointTransforms = (uintptr_t)pContext->m_locJointTransforms; (void)locJointTransforms;
	const uintptr_t pDependencyTable = (uintptr_t)pContext->m_pDependencyTable; (void)pDependencyTable;

	OrbisAnim::CommandBlock::DecomposeMatrixParams params;
	params.m_mayaNodeTypeId = 0x11BF71;
	params.m_rotateOrder = 0;
	params.m_jointOrient[0] = 0;
	params.m_jointOrient[1] = 0;
	params.m_jointOrient[2] = 0;
	params.m_inputMatLoc = OrbisAnim::Location(0x0002);
	params.m_outputTransLoc[0] = OrbisAnim::Location(0x008E);
	params.m_outputTransLoc[1] = OrbisAnim::Location(0x0092);
	params.m_outputTransLoc[2] = OrbisAnim::Location(0x0096);
	params.m_outputRotLoc[0] = OrbisAnim::Location(0x009A);
	params.m_outputRotLoc[1] = OrbisAnim::Location(0x009E);
	params.m_outputRotLoc[2] = OrbisAnim::Location(0x00A2);
	params.m_outputScaleLoc[0] = OrbisAnim::Location(0x00A6);
	params.m_outputScaleLoc[1] = OrbisAnim::Location(0x00AA);
	params.m_outputScaleLoc[2] = OrbisAnim::Location(0x00AE);

	OrbisAnim::CommandBlock::ExecuteCommandDecomposeMatrixImpl(&hierarchyHeader, &params);
}

static void HierarchyCopyCommand_Seg0Cmd6(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const uintptr_t locJointParams = (uintptr_t)pContext->m_locJointParams; (void)locJointParams;
	const uintptr_t locSdkScalarTable = (uintptr_t)pContext->m_locSdkScalarTable; (void)locSdkScalarTable;
	const uintptr_t locJointTransforms = (uintptr_t)pContext->m_locJointTransforms; (void)locJointTransforms;
	const uintptr_t pDependencyTable = (uintptr_t)pContext->m_pDependencyTable; (void)pDependencyTable;

	ORBISANIM_MARKER_SCOPED("Copy", OrbisAnim::Perf::kYellow);
	*(float*)(locJointParams + 0x0140) = *(float*)(locSdkScalarTable + 0x008C);
	*(float*)(locJointParams + 0x0144) = *(float*)(locSdkScalarTable + 0x0090);
	*(float*)(locJointParams + 0x0148) = *(float*)(locSdkScalarTable + 0x0094);
	*(float*)(locSdkScalarTable + 0x0080) = *(float*)(locSdkScalarTable + 0x0098);
	*(float*)(locSdkScalarTable + 0x0084) = *(float*)(locSdkScalarTable + 0x009C);
	*(float*)(locSdkScalarTable + 0x0088) = *(float*)(locSdkScalarTable + 0x00A0);
	*(float*)(locJointParams + 0x0120) = *(float*)(locSdkScalarTable + 0x00A4);
	*(float*)(locJointParams + 0x0124) = *(float*)(locSdkScalarTable + 0x00A8);
	*(float*)(locJointParams + 0x0128) = *(float*)(locSdkScalarTable + 0x00AC);
	*(float*)(locJointParams + 0x0128) = *(float*)(locSdkScalarTable + 0x00AC);
	*(float*)(locJointParams + 0x0128) = *(float*)(locSdkScalarTable + 0x00AC);
	*(float*)(locJointParams + 0x0128) = *(float*)(locSdkScalarTable + 0x00AC);
}

static void HierarchySdkDrivenRotCommand_Seg0Cmd7(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const uintptr_t locJointParams = (uintptr_t)pContext->m_locJointParams; (void)locJointParams;
	const uintptr_t locSdkScalarTable = (uintptr_t)pContext->m_locSdkScalarTable; (void)locSdkScalarTable;
	const uintptr_t locJointTransforms = (uintptr_t)pContext->m_locJointTransforms; (void)locJointTransforms;
	const uintptr_t pDependencyTable = (uintptr_t)pContext->m_pDependencyTable; (void)pDependencyTable;

	// SdkDrivenRotOperation (EulerToQuat)
	{
		ORBISANIM_MARKER_SCOPED("Euler2Quat", OrbisAnim::Perf::kMagenta);
		sceMath::Aos::Vector3 euler(
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x0080)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x0084)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x0088)));
		sceMath::Aos::Quat q = sceMath::Aos::Quat::rotation(euler, sceMath::kXYZ);
		*(sceMath::Aos::Quat*)(locJointParams + 0x0130) = q;
	}
	// SdkDrivenRotOperation (EulerToQuat)
	{
		ORBISANIM_MARKER_SCOPED("Euler2Quat", OrbisAnim::Perf::kMagenta);
		sceMath::Aos::Vector3 euler(
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x0080)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x0084)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x0088)));
		sceMath::Aos::Quat q = sceMath::Aos::Quat::rotation(euler, sceMath::kXYZ);
		*(sceMath::Aos::Quat*)(locJointParams + 0x0130) = q;
	}
	// SdkDrivenRotOperation (EulerToQuat)
	{
		ORBISANIM_MARKER_SCOPED("Euler2Quat", OrbisAnim::Perf::kMagenta);
		sceMath::Aos::Vector3 euler(
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x0080)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x0084)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x0088)));
		sceMath::Aos::Quat q = sceMath::Aos::Quat::rotation(euler, sceMath::kXYZ);
		*(sceMath::Aos::Quat*)(locJointParams + 0x0130) = q;
	}
	// SdkDrivenRotOperation (EulerToQuat)
	{
		ORBISANIM_MARKER_SCOPED("Euler2Quat", OrbisAnim::Perf::kMagenta);
		sceMath::Aos::Vector3 euler(
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x0080)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x0084)),
			DEGREES_TO_RADIANS(*(float*)(locSdkScalarTable + 0x0088)));
		sceMath::Aos::Quat q = sceMath::Aos::Quat::rotation(euler, sceMath::kXYZ);
		*(sceMath::Aos::Quat*)(locJointParams + 0x0130) = q;
	}
}

static void HierarchyParentingCommand_Seg0Cmd8(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const uintptr_t locJointParams = (uintptr_t)pContext->m_locJointParams; (void)locJointParams;
	const uintptr_t locSdkScalarTable = (uintptr_t)pContext->m_locSdkScalarTable; (void)locSdkScalarTable;
	const uintptr_t locJointTransforms = (uintptr_t)pContext->m_locJointTransforms; (void)locJointTransforms;
	const uintptr_t pDependencyTable = (uintptr_t)pContext->m_pDependencyTable; (void)pDependencyTable;

	ORBISANIM_MARKER_SCOPED("Parent", OrbisAnim::Perf::kGray);
	OrbisAnim::JointParentingQuad::Element const parentingTable[] = {
		{ 0x0070, 0x0000 + 1 },	// Child[6]: driven, Parent[-1]: <none>
		{ 0x0070, 0x0000 + 1 },	// Child[6]: driven, Parent[-1]: <none>
		{ 0x0070, 0x0000 + 1 },	// Child[6]: driven, Parent[-1]: <none>
		{ 0x0070, 0x0000 + 1 }	// Child[6]: driven, Parent[-1]: <none>
	};

	OrbisAnim_Parenting(1, &hierarchyHeader, (OrbisAnim::JointParentingQuad const*)parentingTable);
}

static Status EvaluateRigSeg0(SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	HierarchyConstantCommand_Seg0Cmd0(pContext, hierarchyHeader);
	NdiInterpolateMatrix1DCommand_Seg0Cmd1(pContext, hierarchyHeader);
	HierarchyParentingCommand_Seg0Cmd2(pContext, hierarchyHeader);
	NdiMultMatrixCommand_Seg0Cmd3(pContext, hierarchyHeader);
	HierarchyParentingCommand_Seg0Cmd4(pContext, hierarchyHeader);
	NdiDecomposeMatrixCommand_Seg0Cmd5(pContext, hierarchyHeader);
	HierarchyCopyCommand_Seg0Cmd6(pContext, hierarchyHeader);
	HierarchySdkDrivenRotCommand_Seg0Cmd7(pContext, hierarchyHeader);
	HierarchyParentingCommand_Seg0Cmd8(pContext, hierarchyHeader);

	return kSuccess;
}

Status EvaluateInterpolateMatrixArray1dMultMatrixTestRig(U32 hierarchyId, SegmentContext* pContext)
{
	if (0x734b01e2 != hierarchyId)
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

