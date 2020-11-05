/*
 * Copyright (c) 2013 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#ifndef ND_ANIM_PLUGINS_H
#define ND_ANIM_PLUGINS_H

#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/ik/jacobian-ik.h"
#include "ndlib/anim/skel-table.h"

class ArtItemSkeleton;
class JointSet;
namespace OrbisAnim {
class WorkBuffer;
struct ProcessingGroupContext;
struct SegmentContext;
}  // namespace OrbisAnim
namespace ndanim {
struct ClipData;
}  // namespace ndanim
struct JointOverrideData;

namespace ndanim
{
	struct JointParams;
	struct ValidBits;
}

struct AnimExecutionContext;
class JointModifierData;
struct BoundingData;

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(8) EvaluateJointOverridePluginData
{
	JointOverrideData* m_pOverrideData;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(8) EvaluateJointConversionPluginData
{
	Transform* m_pOutputTransforms;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(8) EvaluateJointToGridPluginData
{
	Transform* m_pObjXform;
	void* m_grid;
	void* m_boundingBox;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(8) EvaluateRetargetAnimPluginData
{
	const SkelTable::RetargetEntry* m_pRetargetEntry;
	U32 m_tgtSegmentIndex;
	const ArtItemSkeleton* m_pSrcSkel;
	const ArtItemSkeleton* m_pDestSkel;
	const ndanim::ValidBits* m_pSrcValidBitsArray;
	bool m_jointDataIsAdditive;
	ndanim::AnimatedJointPose* m_pOutPose;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct ALIGNED(16) JacobianIkPluginData
{
	JointSet*			m_pJointSet;
	JacobianMap*		m_pIkJacobianMap;
	JacobianIkInstance	m_ikInstance;
	U32					m_inputOutputInstance;
	float				m_ikFade;
	float				m_rootScale;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct JointOverrideAnimPluginData
{
	void* m_locJointParams;
	const JointOverrideData* m_pOverrideData;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct BoundingSphereAnimPluginData
{
	const Transform* m_pObjXform;
	StringId64 m_skeletonNameId;
	U16 m_numTotalJoints;
	void* m_locJointTransforms;
	I16 m_visSphereJointIndex;
	const Vec4* m_pVisSphere;
	const Aabb* m_pVisAabb;
	BoundingData* m_pBoundingInfo;
	I16 m_boundingSphereExcludeJoints[2];
	F32 m_clothBoundingBoxMult;
	F32 m_dynamicPaddingRadius;
	bool m_useBoundingBox;
};


/// --------------------------------------------------------------------------------------------------------------- ///
void NdProcessAnimPhasePluginFunc(OrbisAnim::WorkBuffer* pWorkBuffer,
									   OrbisAnim::SegmentContext* pSegmentContext,
									   OrbisAnim::ProcessingGroupContext* pGroupContext,
									   const AnimExecutionContext* pContext,
									   StringId64 pluginName,
									   const void* pPluginData,
									   JointSet* pPluginJointSet);

/// --------------------------------------------------------------------------------------------------------------- ///
void NdProcessPostAnimPhasePluginFunc(OrbisAnim::WorkBuffer* pWorkBuffer,
									   OrbisAnim::SegmentContext* pSegmentContext,
									   const AnimExecutionContext* pContext,
									   StringId64 pluginName,
									   const void* pPluginData,
									   JointSet* pPluginJointSet);

/// --------------------------------------------------------------------------------------------------------------- ///
void EvaluateJointModifierPlugin(OrbisAnim::SegmentContext* pSegmentContext,
								 U32 ikType,
								 JointModifierData* pData,
								 const Transform* pObjXform,
								 JointSet* pJointSet);


#endif // ND_ANIM_PLUGINS_H

