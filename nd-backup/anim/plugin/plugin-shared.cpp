/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "ndlib/anim/plugin/plugin-shared.h"

#ifdef ANIM_DEBUG
/// --------------------------------------------------------------------------------------------------------------- ///
void ValidateObjectTransform(const Transform* pObjXform)
{
	const Transform curXform(*pObjXform);
	ANIM_ASSERT(IsOrthogonal(&curXform));
	const Point objWsPos = curXform.GetTranslation();
	ANIM_ASSERT(Length(objWsPos) < 100000.0f);
	ANIM_ASSERT(IsFinite(objWsPos));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ValidateJointTransforms(const OrbisAnim::JointTransform* pJointTransforms, U32 numJoints)
{
	ANIM_ASSERT(numJoints > 0 && numJoints < 1000);

	for (U32F i = 0; i < numJoints; ++i)
	{
		const Transform xform = Transform(pJointTransforms[i].GetTransform());
		const Point pos = xform.GetTranslation();
		const Quat rot = Quat(xform.GetMat44());

		ANIM_ASSERT(Length(pos) < 100000.0f);
		ANIM_ASSERT(IsFinite(pos));
		ANIM_ASSERT(IsFinite(rot));
	}
}
#endif

#ifdef _MSC_VER
void _LNK4221_avoidance_plugin_shared() {}
#endif
