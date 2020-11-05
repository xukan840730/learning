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
// Joint[1]: |root|hip
// Joint[2]: |root|hip|tier1_base
// Joint[3]: |root|hip|tier1_base|tier1_l_elbow
// Joint[4]: |root|hip|tier1_base|tier1_r_elbow
// Joint[5]: |root|hip|tier1_base|tier1_l_elbow|tier1_l_hand
// Joint[6]: |root|hip|tier1_base|tier1_r_elbow|tier1_r_hand
// Joint[7]: |root|hip|tier1_base|tier2_base
// Joint[8]: |root|hip|tier1_base|tier2_base|tier2_neck
// Joint[9]: |root|hip|tier1_base|tier2_base|tier2_neck|tier2_head
// Joint[10]: |root|hip|tier1_base|tier2_base|tier2_b_elbow
// Joint[11]: |root|hip|tier1_base|tier2_base|tier2_b_elbow|tier2_b_hand
// Joint[12]: |root|hip|tier1_base|tier2_base|tier2_f_elbow
// Joint[13]: |root|hip|tier1_base|tier2_base|tier2_f_elbow|tier2_f_hand

static void HierarchyParentingCommand_Seg0Cmd0(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
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
		{ 0x0010, 0x0000 + 1 },	// Child[0]: root, Parent[-1]: <none>
		{ 0x0020, 0x0040 + 1 },	// Child[1]: hip, Parent[0]: root
		{ 0x0020, 0x0040 + 1 },	// Child[1]: hip, Parent[0]: root
		{ 0x0020, 0x0040 + 1 },	// Child[1]: hip, Parent[0]: root
		{ 0x0020, 0x0040 + 1 },	// Child[1]: hip, Parent[0]: root
		{ 0x0030, 0x0080 + 1 },	// Child[2]: tier1_base, Parent[1]: hip
		{ 0x0030, 0x0080 + 1 },	// Child[2]: tier1_base, Parent[1]: hip
		{ 0x0030, 0x0080 + 1 },	// Child[2]: tier1_base, Parent[1]: hip
		{ 0x0030, 0x0080 + 1 },	// Child[2]: tier1_base, Parent[1]: hip
		{ 0x0040, 0x00C0 + 1 },	// Child[3]: tier1_l_elbow, Parent[2]: tier1_base
		{ 0x0050, 0x00C0 + 1 },	// Child[4]: tier1_r_elbow, Parent[2]: tier1_base
		{ 0x0050, 0x00C0 + 1 },	// Child[4]: tier1_r_elbow, Parent[2]: tier1_base
		{ 0x0050, 0x00C0 + 1 },	// Child[4]: tier1_r_elbow, Parent[2]: tier1_base
		{ 0x0060, 0x0100 + 1 },	// Child[5]: tier1_l_hand, Parent[3]: tier1_l_elbow
		{ 0x0070, 0x0140 + 1 },	// Child[6]: tier1_r_hand, Parent[4]: tier1_r_elbow
		{ 0x0070, 0x0140 + 1 },	// Child[6]: tier1_r_hand, Parent[4]: tier1_r_elbow
		{ 0x0070, 0x0140 + 1 }	// Child[6]: tier1_r_hand, Parent[4]: tier1_r_elbow
	};

	OrbisAnim_Parenting(5, &hierarchyHeader, (OrbisAnim::JointParentingQuad const*)parentingTable);
}

static void HierarchyCopyCommand_Seg0Cmd1(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const uintptr_t locJointParams = (uintptr_t)pContext->m_locJointParams; (void)locJointParams;
	const uintptr_t locSdkScalarTable = (uintptr_t)pContext->m_locSdkScalarTable; (void)locSdkScalarTable;
	const uintptr_t locJointTransforms = (uintptr_t)pContext->m_locJointTransforms; (void)locJointTransforms;
	const uintptr_t pDependencyTable = (uintptr_t)pContext->m_pDependencyTable; (void)pDependencyTable;

	ORBISANIM_MARKER_SCOPED("Copy", OrbisAnim::Perf::kYellow);
	*(SMath::Vec4*)(pDependencyTable + 0x0000) = *(SMath::Vec4*)(locJointTransforms + 0x00C0);
	*(SMath::Vec4*)(pDependencyTable + 0x0010) = *(SMath::Vec4*)(locJointTransforms + 0x00D0);
	*(SMath::Vec4*)(pDependencyTable + 0x0020) = *(SMath::Vec4*)(locJointTransforms + 0x00E0);
	*(SMath::Vec4*)(pDependencyTable + 0x0030) = *(SMath::Vec4*)(locJointTransforms + 0x00F0);
}

static void HierarchyParentingCommand_Seg1Cmd0(const SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	const uintptr_t locJointParams = (uintptr_t)pContext->m_locJointParams; (void)locJointParams;
	const uintptr_t locSdkScalarTable = (uintptr_t)pContext->m_locSdkScalarTable; (void)locSdkScalarTable;
	const uintptr_t locJointTransforms = (uintptr_t)pContext->m_locJointTransforms; (void)locJointTransforms;
	const uintptr_t pDependencyTable = (uintptr_t)pContext->m_pDependencyTable; (void)pDependencyTable;

	ORBISANIM_MARKER_SCOPED("Parent", OrbisAnim::Perf::kGray);
	OrbisAnim::JointParentingQuad::Element const parentingTable[] = {
		{ 0x0010, 0x0000 + 3 },	// Child[7]: tier2_base, Parent[2]: tier1_base
		{ 0x0010, 0x0000 + 3 },	// Child[7]: tier2_base, Parent[2]: tier1_base
		{ 0x0010, 0x0000 + 3 },	// Child[7]: tier2_base, Parent[2]: tier1_base
		{ 0x0010, 0x0000 + 3 },	// Child[7]: tier2_base, Parent[2]: tier1_base
		{ 0x0020, 0x0040 + 1 },	// Child[8]: tier2_neck, Parent[7]: tier2_base
		{ 0x0040, 0x0040 + 1 },	// Child[10]: tier2_b_elbow, Parent[7]: tier2_base
		{ 0x0060, 0x0040 + 1 },	// Child[12]: tier2_f_elbow, Parent[7]: tier2_base
		{ 0x0060, 0x0040 + 1 },	// Child[12]: tier2_f_elbow, Parent[7]: tier2_base
		{ 0x0030, 0x0080 + 1 },	// Child[9]: tier2_head, Parent[8]: tier2_neck
		{ 0x0050, 0x0100 + 1 },	// Child[11]: tier2_b_hand, Parent[10]: tier2_b_elbow
		{ 0x0070, 0x0180 + 1 },	// Child[13]: tier2_f_hand, Parent[12]: tier2_f_elbow
		{ 0x0070, 0x0180 + 1 }	// Child[13]: tier2_f_hand, Parent[12]: tier2_f_elbow
	};

	OrbisAnim_Parenting(3, &hierarchyHeader, (OrbisAnim::JointParentingQuad const*)parentingTable);
}

static Status EvaluateRigSeg0(SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	HierarchyParentingCommand_Seg0Cmd0(pContext, hierarchyHeader);
	HierarchyCopyCommand_Seg0Cmd1(pContext, hierarchyHeader);

	return kSuccess;
}

static Status EvaluateRigSeg1(SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader)
{
	HierarchyParentingCommand_Seg1Cmd0(pContext, hierarchyHeader);

	return kSuccess;
}

Status EvaluateFlipTestRig(U32 hierarchyId, SegmentContext* pContext)
{
	if (0x5c97c207 != hierarchyId)
		return kFatalErrorMismatch;

	const uint32_t kNumSegments = 2;
	typedef Status(*RigEvalFn)(SegmentContext* pContext, const OrbisAnim::HierarchyHeader& hierarchyHeader);
	static RigEvalFn rigEvalFns[kNumSegments] =
	{
		EvaluateRigSeg0,
		EvaluateRigSeg1,
	};

	OrbisAnim::HierarchyHeader hierarchyHeader;
	hierarchyHeader.m_pJointParams = (void*)pContext->m_locJointParams;
	hierarchyHeader.m_pJointTransforms = (void*)pContext->m_locJointTransforms;
	hierarchyHeader.m_pScalarTable = (float*)pContext->m_locSdkScalarTable;
	hierarchyHeader.m_pDependencyTable = (void*)pContext->m_pDependencyTable;

	return rigEvalFns[pContext->m_iSegment](pContext, hierarchyHeader);
}

