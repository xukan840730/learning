/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "animobjectrootanims.h"

namespace OrbisAnim {
namespace Tools {
namespace AnimProcessing {

extern float g_fEpsilonScale;
extern float g_fEpsilonRotation;
extern float g_fEpsilonTranslation;
extern float g_fEpsilonFloat;

AnimationClipLinearObjectRootAnim::AnimationClipLinearObjectRootAnim() :
	m_rootLinearScale(1.0f, 1.0f, 1.0f),
	m_rootLinearRotation(0.0f, 0.0f, 0.0f, 1.0f),
	m_rootLinearTranslation(0.0f, 0.0f, 0.0f),
	m_iObjectRootAnimIndex(0),
	m_iNumFrames(0),
	m_fPhasePerFrame(0.0f)
{
}

ICETOOLS::Location AnimationClipLinearObjectRootAnim::Write( ICETOOLS::StreamWriter& streamWriter ) const
{
	streamWriter.Align(16);
	ICETOOLS::Location locStart = streamWriter.CreatePosition();
	if (m_iNumFrames > 0) {
		streamWriter.WriteF( m_rootLinearScale.x );
		streamWriter.WriteF( m_rootLinearScale.y );
		streamWriter.WriteF( m_rootLinearScale.z );
		streamWriter.WriteF( 1.0f );
		
		streamWriter.WriteF( m_rootLinearRotation.x );
		streamWriter.WriteF( m_rootLinearRotation.y );
		streamWriter.WriteF( m_rootLinearRotation.z );
		streamWriter.WriteF( m_rootLinearRotation.w );
		
		streamWriter.WriteF( m_rootLinearTranslation.x );
		streamWriter.WriteF( m_rootLinearTranslation.y );
		streamWriter.WriteF( m_rootLinearTranslation.z );
		streamWriter.WriteF( 1.0f );
	} else {
		streamWriter.WriteF( 1.0f );
		streamWriter.WriteF( 1.0f );
		streamWriter.WriteF( 1.0f );
		streamWriter.WriteF( 1.0f );

		streamWriter.WriteF( 0.0f );
		streamWriter.WriteF( 0.0f );
		streamWriter.WriteF( 0.0f );
		streamWriter.WriteF( 1.0f );
		
		streamWriter.WriteF( 0.0f );
		streamWriter.WriteF( 0.0f );
		streamWriter.WriteF( 0.0f );
		streamWriter.WriteF( 1.0f );
	}
	return locStart;
}

void AnimationClipLinearObjectRootAnim::Dump() const
{
	INOTE(IMsg::kDebug, "\tObject Root Anim %d;  extracted linear key at frame %d:\n", m_iObjectRootAnimIndex, m_iNumFrames-1);
	INOTE(IMsg::kDebug, "\tscale:       (%8.3f, %8.3f, %8.3f)\n", m_rootLinearScale.x, m_rootLinearScale.y, m_rootLinearScale.z);
	INOTE(IMsg::kDebug, "\trotation:    (%7.4f, %7.4f, %7.4f, %7.4f)\n", m_rootLinearRotation.x, m_rootLinearRotation.y, m_rootLinearRotation.z, m_rootLinearRotation.w);
	INOTE(IMsg::kDebug, "\ttranslation: (%8.3f, %8.3f, %8.3f)\n", m_rootLinearTranslation.x, m_rootLinearTranslation.y, m_rootLinearTranslation.z);
}

bool ExtractLinearRootAnim( AnimationClipSourceData& sourceData, unsigned iObjectRootAnim )
{
	ITASSERT(sourceData.m_pRootAnim == NULL);
	if (sourceData.m_numFrames <= 1 || iObjectRootAnim > sourceData.GetNumAnims(kChannelTypeScale))
		return false;

	ITGEOM::Vec3 rootLinearScale(1.0f, 1.0f, 1.0f);
	ITGEOM::Quat rootLinearRotation(0.0f, 0.0f, 0.0f, 1.0f);
	ITGEOM::Vec3 rootLinearTranslation(0.0f, 0.0f, 0.0f);
	bool bLinearComponent = false;
	unsigned iNumFrames = sourceData.m_numFrames;
	float fPhasePerFrame = 1.0f / (float)(iNumFrames - 1);

	if (!sourceData.IsConstant(kChannelTypeScale, iObjectRootAnim)) {
		SampledAnim& joint_s = *sourceData.GetSampledAnim(kChannelTypeScale, iObjectRootAnim);
		const size_t numFrames = joint_s.GetNumSamples();
		ITASSERT(numFrames == iNumFrames);
		ITGEOM::Vec3 v0 = joint_s[0];
		ITGEOM::Vec3 v1 = joint_s[numFrames-1];
		if (!v0.IsEquiv(v1, 0.0f)) {
			rootLinearScale = v1 / v0;
			bLinearComponent = true;
			// FIXME - this constant channel check doesn't use same tolerance as others
			bool bConstant = true;
			joint_s[0] = joint_s[numFrames-1] = v0;
			for (size_t iFrame = 1; iFrame < numFrames-1; iFrame++) {
				ITGEOM::Vec3 vLerp = ITGEOM::Lerp(kIdentityScale, rootLinearScale, fPhasePerFrame*(float)iFrame);
				ITGEOM::Vec3 scale = joint_s[iFrame];
				scale /= vLerp;
				bConstant = bConstant && scale.IsEquiv(v0, g_fEpsilonScale);
				joint_s[iFrame] = scale;
			}
			if (bConstant) {
				joint_s.SetToConstant(v0);
			}
		}
	}
	if (!sourceData.IsConstant(kChannelTypeRotation, iObjectRootAnim)) {
		SampledAnim& joint_q = *sourceData.GetSampledAnim(kChannelTypeRotation, iObjectRootAnim);
		const size_t numFrames = joint_q.GetNumSamples();
		ITASSERT(numFrames == iNumFrames);
		ITGEOM::Quat q0 = joint_q[0];
		ITGEOM::Quat q1 = joint_q[numFrames-1];
		if (numFrames > 1 && !q0.IsEquiv2(q1, 0.0f)) {
			rootLinearRotation = q1 * q0.Conjugate();
			bLinearComponent = true;
			// FIXME - this constant channel check doesn't use same tolerance as others
			bool bConstant = true;
			joint_q[0] = joint_q[numFrames-1] = q0;
			for (size_t iFrame = 1; iFrame < numFrames-1; iFrame++) {
				ITGEOM::Quat qSlerp = ITGEOM::Slerp(kIdentityRotation, rootLinearRotation, fPhasePerFrame*(float)iFrame);
				ITGEOM::Quat rotation = joint_q[iFrame];
				rotation *= qSlerp.Conjugate();
				bConstant = bConstant && rotation.IsEquiv2(q0, g_fEpsilonRotation);
				joint_q[iFrame] = rotation;
			}
			if (bConstant) {
				joint_q.SetToConstant(q0);
			}
		}
	}
	if (!sourceData.IsConstant(kChannelTypeTranslation, iObjectRootAnim)) {
		SampledAnim& joint_t = *sourceData.GetSampledAnim(kChannelTypeTranslation, iObjectRootAnim);
		const size_t numFrames = joint_t.GetNumSamples();
		ITASSERT(numFrames == iNumFrames);
		ITGEOM::Vec3 v0 = joint_t[0];
		ITGEOM::Vec3 v1 = joint_t[numFrames-1];
		if (numFrames > 1 && !v0.IsEquiv(v1, 0.0f)) {
			rootLinearTranslation = v1 - v0;
			bLinearComponent = true;
			// FIXME - this constant channel check doesn't use same tolerance as others
			bool bConstant = true;
			joint_t[0] = joint_t[numFrames-1] = v0;
			for (size_t iFrame = 1; iFrame < numFrames-1; iFrame++) {
				ITGEOM::Vec3 vLerp = rootLinearTranslation * (fPhasePerFrame*(float)iFrame);
				ITGEOM::Vec3 translation = joint_t[iFrame];
				translation -= vLerp;
				bConstant = bConstant && translation.IsEquiv(v0, g_fEpsilonTranslation);
			}
			if (bConstant) {
				joint_t.SetToConstant(v0);
			}
		}
	}
	if (bLinearComponent) {
		sourceData.m_pRootAnim = new AnimationClipLinearObjectRootAnim(iObjectRootAnim, iNumFrames, fPhasePerFrame, rootLinearScale, rootLinearRotation, rootLinearTranslation);
	}
	return bLinearComponent;
}

}	//namespace AnimProcessing
}	//namespace Tools
}	//namespace OrbisAnim
