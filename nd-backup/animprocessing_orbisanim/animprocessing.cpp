/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "animmetadata.h"
#include "animcompression.h"
#include "animobjectrootanims.h"
#include "smathhelpers.h"

//#pragma optimize("", off) // uncomment when debugging in release mode

namespace OrbisAnim {
namespace Tools {

extern float ComputeError_Quat(ITGEOM::Quat const &q, ITGEOM::Quat const &qRef);
extern float ComputeError_Vec3(ITGEOM::Vec3 const &v, ITGEOM::Vec3 const &vRef);

namespace AnimProcessing {

extern float g_fEpsilonScale;
extern float g_fEpsilonRotation;
extern float g_fEpsilonTranslation;
extern float g_fEpsilonFloat;

// helper operator for subtracting one set from another
template<class T>
std::set<T> operator-=(std::set<T>& lhs, const std::set<T>& rhs)	
{
	std::set<T> r;
	std::set_difference(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), std::inserter(r, r.end()));
	lhs = r;
	return lhs;
}

bool HasNonUniformScales(AnimationClipSourceData const& sourceData, unsigned iJointAnim)
{
	SampledAnim& channel = *sourceData.GetSampledAnim(kChannelTypeScale, iJointAnim);
	for (size_t iFrame = 0; iFrame < channel.GetNumSamples(); ++iFrame) {
		ITGEOM::Vec3 const& s = channel[iFrame];
		if (s.x != s.y || s.x != s.z)
			return true;
	}
	return false;
}

// Merge a parent animation minus the parent bind pose into a child animation
void MergeParentAnimation(AnimationClipSourceData& sourceData, 
						  unsigned iParentAnim, 
						  unsigned iJointAnim, 
						  Joint const& parentJoint)
{
	const bool bParentConstantScale = sourceData.IsConstant(kChannelTypeScale, iParentAnim);
	const bool bParentConstantRotation = sourceData.IsConstant(kChannelTypeRotation, iParentAnim);
	const bool bParentConstantTranslation = sourceData.IsConstant(kChannelTypeTranslation, iParentAnim);
	
	const bool bConstantScale = sourceData.IsConstant(kChannelTypeScale, iJointAnim);
	const bool bConstantRotation = sourceData.IsConstant(kChannelTypeRotation, iJointAnim);
	const bool bConstantTranslation = sourceData.IsConstant(kChannelTypeTranslation, iJointAnim);
	
	SampledAnim& parent_s = *sourceData.GetSampledAnim(kChannelTypeScale, iParentAnim);
	SampledAnim& parent_q = *sourceData.GetSampledAnim(kChannelTypeRotation, iParentAnim);
	SampledAnim& parent_t = *sourceData.GetSampledAnim(kChannelTypeTranslation, iParentAnim);
	
	SampledAnim& joint_s = *sourceData.GetSampledAnim(kChannelTypeScale, iJointAnim);
	SampledAnim& joint_q = *sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnim);
	SampledAnim& joint_t = *sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnim);
	
	size_t numFrames = 0;

	ITGEOM::Vec3 vParentBindScaleInv = 1.0f / parentJoint.m_jointScale;
	ITGEOM::Vec3 vParentBindTransInv = - parentJoint.m_jointTranslation;
	ITGEOM::Quat qParentBindQuatInv = parentJoint.m_jointQuat.Conjugate();

	bool bParentConstantIdentity = bParentConstantScale && bParentConstantRotation && bParentConstantTranslation;
	if (!bParentConstantScale) {
		if (!numFrames) { numFrames = parent_s.GetNumSamples(); } else { ITASSERT(parent_s.GetNumSamples() == numFrames); }
	} else {
		bParentConstantIdentity = bParentConstantIdentity && (parent_s[0] * vParentBindScaleInv == kIdentityScale);
	}
	if (!bParentConstantRotation) {
		if (!numFrames) { numFrames = parent_q.GetNumSamples(); } else { ITASSERT(parent_q.GetNumSamples() == numFrames); }
	} else {
		bParentConstantIdentity = bParentConstantIdentity && kIdentityRotation.IsEquiv2(qParentBindQuatInv * (ITGEOM::Quat&)parent_q[0], 0.0f);
	}
	if (!bParentConstantTranslation) {
		if (!numFrames) { numFrames = parent_t.GetNumSamples(); } else { ITASSERT(parent_t.GetNumSamples() == numFrames); }
	} else {
		bParentConstantIdentity = bParentConstantIdentity && ((ITGEOM::Vec3&)parent_t[0] + vParentBindTransInv == kIdentityTranslation);
	}
	if (bParentConstantIdentity)
		return;		// nothing to do if our parent pose is constant and identity
	if (!bConstantScale)
		if (!numFrames) { numFrames = joint_s.GetNumSamples(); } else {	ITASSERT(joint_s.GetNumSamples() == numFrames); }
	if (!bConstantRotation)
		if (!numFrames) { numFrames = joint_q.GetNumSamples(); } else {	ITASSERT(joint_q.GetNumSamples() == numFrames); }
	if (!bConstantTranslation)
		if (!numFrames) { numFrames = joint_t.GetNumSamples(); } else {	ITASSERT(joint_t.GetNumSamples() == numFrames); }
	if (!numFrames)
		numFrames = 1;

	if (bConstantTranslation) {
		if (bParentConstantScale && bParentConstantRotation && bParentConstantTranslation) {
			joint_t[0] = (qParentBindQuatInv * (ITGEOM::Quat&)parent_q[0]).RotateVector( joint_t[0] ).Multiply( parent_s[0] * vParentBindScaleInv ) + parent_t[0] + vParentBindTransInv;
		} else {
			ITGEOM::Vec3 t0 = joint_t[0];
			joint_t.SetNumSamples(numFrames);
			ITGEOM::Vec3 parent_s_i = parent_s[0] * vParentBindScaleInv;
			ITGEOM::Quat parent_q_i = qParentBindQuatInv * (ITGEOM::Quat&)parent_q[0];
			ITGEOM::Vec3 parent_t_i = (ITGEOM::Vec3&)parent_t[0] + vParentBindTransInv;
			for (size_t iFrame = 0; iFrame < numFrames; iFrame++) {
				if (!bParentConstantScale)			parent_s_i = parent_s[iFrame] * vParentBindScaleInv;
				if (!bParentConstantRotation)		parent_q_i = qParentBindQuatInv * (ITGEOM::Quat&)parent_q[iFrame];
				if (!bParentConstantTranslation)	parent_t_i = (ITGEOM::Vec3&)parent_t[iFrame] + vParentBindTransInv;
				joint_t[iFrame] = parent_q_i.RotateVector( t0 ).Multiply(parent_s_i) + parent_t_i;
			}
		}
	} else {
		if (bParentConstantScale && bParentConstantRotation && bParentConstantTranslation) {
			ITGEOM::Vec3 parent_s0 = parent_s[0] * vParentBindScaleInv;
			ITGEOM::Quat parent_q0 = qParentBindQuatInv * (ITGEOM::Quat&)parent_q[0];
			ITGEOM::Vec3 parent_t0 = (ITGEOM::Vec3&)parent_t[0] + vParentBindTransInv;
			for (size_t iFrame = 0; iFrame < numFrames; iFrame++)
				joint_t[iFrame] = parent_q0.RotateVector( joint_t[iFrame] ).Multiply( parent_s0 ) + parent_t0;
		} else {
			ITGEOM::Vec3 parent_s_i = parent_s[0] * vParentBindScaleInv;
			ITGEOM::Quat parent_q_i = qParentBindQuatInv * (ITGEOM::Quat&)parent_q[0];
			ITGEOM::Vec3 parent_t_i = (ITGEOM::Vec3&)parent_t[0] + vParentBindTransInv;
			for (size_t iFrame = 0; iFrame < numFrames; iFrame++) {
				if (!bParentConstantScale)			parent_s_i = parent_s[iFrame] * vParentBindScaleInv;
				if (!bParentConstantRotation)		parent_q_i = qParentBindQuatInv * (ITGEOM::Quat&)parent_q[iFrame];
				if (!bParentConstantTranslation)	parent_t_i = (ITGEOM::Vec3&)parent_t[iFrame] + vParentBindTransInv;
				joint_t[iFrame] = parent_q_i.RotateVector( joint_t[iFrame] ).Multiply(parent_s_i) + parent_t_i;
			}
		}
	}
	if (bConstantScale) {
		if (bParentConstantScale) {
			((ITGEOM::Vec3&)joint_s[0]).Multiply( parent_s[0]*vParentBindScaleInv );
		} else {
			ITGEOM::Vec3 const s0 = joint_s[0] * vParentBindScaleInv;
			joint_s.SetNumSamples(numFrames);
			for (size_t iFrame = 0; iFrame < numFrames; iFrame++)
				joint_s[iFrame] = s0 * (ITGEOM::Vec3&)parent_s[iFrame];
		}
	} else {
		if (bParentConstantScale) {
			ITGEOM::Vec3 parent_s0 = parent_s[0] * vParentBindScaleInv;
			if (parent_s0 != kIdentityScale) {
				for (size_t iFrame = 0; iFrame < numFrames; iFrame++)
					((ITGEOM::Vec3&)joint_s[iFrame]).Multiply( parent_s0 );
			}
		} else {
			for (size_t iFrame = 0; iFrame < numFrames; iFrame++)
				((ITGEOM::Vec3&)joint_s[iFrame]).Multiply( parent_s[iFrame] * vParentBindScaleInv );
		}
	}
	if (bConstantRotation) {
		if (bParentConstantRotation) {
			joint_q[0] = (qParentBindQuatInv * (ITGEOM::Quat&)parent_q[0]) * (ITGEOM::Quat&)joint_q[0];
		} else {
			ITGEOM::Quat const q0 = joint_q[0];
			joint_q.SetNumSamples(numFrames);
			for (size_t iFrame = 0; iFrame < numFrames; iFrame++)
				joint_q[iFrame] = (qParentBindQuatInv * (ITGEOM::Quat&)parent_q[iFrame]) * q0;
		}
	} else {
		if (bParentConstantRotation) {
			ITGEOM::Quat parent_q0 = qParentBindQuatInv * (ITGEOM::Quat&)parent_q[0];
			if (!kIdentityRotation.IsEquiv2(parent_q0, 0.0f)) {
				for (size_t iFrame = 0; iFrame < numFrames; iFrame++)
					joint_q[iFrame] = parent_q0 * (ITGEOM::Quat&)joint_q[iFrame];
			}
		} else {
			for (size_t iFrame = 0; iFrame < numFrames; iFrame++)
				joint_q[iFrame] = (qParentBindQuatInv * (ITGEOM::Quat&)parent_q[iFrame]) * (ITGEOM::Quat&)joint_q[iFrame];
		}
	}
	parent_s.SetToConstant(kIdentityScale);
	parent_q.SetToConstant(kIdentityRotation);
	parent_t.SetToConstant(kIdentityTranslation);
}

bool ExtractObjectRootAnim(ExtractRootAnimFunction pExtractRootAnimFunction, 
						   AnimationClipSourceData& sourceData, 
						   AnimationClipProperties& clipProperties, 
						   AnimationHierarchyDesc const& hierarchyDesc, 
						   AnimationBinding const& binding)
{
	ITASSERT(	sourceData.GetNumAnims(kChannelTypeScale) == binding.m_jointAnimIdToJointId.size() &&
				sourceData.GetNumAnims(kChannelTypeRotation) == binding.m_jointAnimIdToJointId.size() &&
				sourceData.GetNumAnims(kChannelTypeTranslation) == binding.m_jointAnimIdToJointId.size() &&
				sourceData.GetNumAnims(kChannelTypeScalar) == binding.m_floatAnimIdToFloatId.size() );
	ITASSERT(	binding.m_hierarchyId == hierarchyDesc.m_hierarchyId );

	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToJointId.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();

	clipProperties.m_objectRootAnim = kObjectRootAnimNone;

	if (sourceData.m_numFrames < 2) {
		IWARN("Animation is tagged with -movementAnim, but has only one frame; can't extract movement from a pose!\n");
		return false;
	}
	int iObjectRootJoint = hierarchyDesc.m_objectRootJoint;
	if (iObjectRootJoint < 0) {
		IWARN("Animation is tagged with -movementAnim, but no joint in the bindpose has the ObjectRoot attribute!\n");
		return false;
	} else if ((unsigned)iObjectRootJoint > hierarchyDesc.m_numAnimatedJoints) {
		IWARN("Object root joint (%d) is not an animation driven joint in the bind pose!\n", iObjectRootJoint);
		return false;
	}
	Joint const& joint = hierarchyDesc.m_joints[iObjectRootJoint];
	int iJointAnimIndex = binding.m_jointIdToJointAnimId[iObjectRootJoint];
	if (iJointAnimIndex < 0) {
		IWARN("ObjectRoot joint (%s) has no defined animation.\n", joint.m_name.c_str());
		return false;
	}
	std::vector<unsigned> parentAnims;
	std::string const& name = joint.m_name;
	for (int iParent = joint.m_parent; iParent != -1; iParent = hierarchyDesc.m_joints[iParent].m_parent) {
		Joint const& parent = hierarchyDesc.m_joints[iParent];
		std::string const& parentName = parent.m_name;
		if (!(parent.m_flags & kJointIsAnimated)) {
			IWARN("Joint parent '%s' of object root '%s' is procedural!\n", parentName.c_str(), name.c_str());
			return false;
		}
		int iParentAnimIndex = binding.m_jointIdToJointAnimId[iParent];
		if (iParentAnimIndex < 0)
			continue;
		if (parentAnims.empty() && HasNonUniformScales(sourceData, iJointAnimIndex)) {
			IWARN("ObjectRoot '%s' has non-uniform scales : can't compose for linear root anim\n", name.c_str());
			return false;
		}
		if (HasNonUniformScales(sourceData, iParentAnimIndex)) {
			IWARN("ObjectRoot '%s' parent '%s' has non-uniform scales : can't compose for linear root anim\n", name.c_str(), parentName.c_str());
			return false;
		}
		IWARN("ObjectRoot '%s' parent '%s' is animated - merging into object root anim.\n", name.c_str(), parentName.c_str());
		parentAnims.push_back((unsigned)iParentAnimIndex);
	}
	for (size_t i = 0; i < parentAnims.size(); ++i) {
		int iParentJoint = binding.m_jointAnimIdToJointId[parentAnims[i]];
		MergeParentAnimation(sourceData, parentAnims[i], (unsigned)iJointAnimIndex, hierarchyDesc.m_joints[iParentJoint]);
	}

	// Now we can extract the movement of the object root joint, leaving an animation
	// for which the object root loops back to it's initial value.
	if ((*pExtractRootAnimFunction)( sourceData, iJointAnimIndex )) {
		clipProperties.m_objectRootAnim = sourceData.m_pRootAnim->GetType();
		INOTE(IMsg::kDebug, "Extracted object root '%s' animation:\n", name.c_str());
		sourceData.m_pRootAnim->Dump();
	}

	// If this is a looping animation, all of our joints now loop, and we can drop our now-redundant final frame.
	if (sourceData.m_flags & kClipLooping) {
		unsigned numFrames = sourceData.m_numFrames-1;
		for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; ++iJointAnimIndex) {
			int iJoint = binding.m_jointAnimIdToJointId[iJointAnimIndex];
			if (iJoint < 0) {
				continue;
			}
			SampledAnim& scaleChannel = *sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex);
			if (!scaleChannel.IsConstant()) {
				ITGEOM::Vec3 firstSample = scaleChannel[0];
				ITGEOM::Vec3 lastSample = scaleChannel[numFrames];
				if (!lastSample.IsEquiv(firstSample, g_fEpsilonScale)) {
					IWARN("Looping movement anim joint '%s' scale does not loop perfectly\n", hierarchyDesc.m_joints[iJoint].m_name.c_str());
				}
				scaleChannel.SetNumSamples(numFrames);
			}
			SampledAnim& rotationChannel = *sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex);
			if (!rotationChannel.IsConstant()) {
				ITGEOM::Quat firstSample = rotationChannel[0];
				ITGEOM::Quat lastSample = rotationChannel[numFrames];
				if (!lastSample.IsEquiv2(firstSample, g_fEpsilonRotation)) {
					IWARN("Looping movement anim joint '%s' rotation does not loop perfectly\n", hierarchyDesc.m_joints[iJoint].m_name.c_str());
				}
				rotationChannel.SetNumSamples(numFrames);
			}
			SampledAnim& translationChannel = *sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex);
			if (!translationChannel.IsConstant()) {
				ITGEOM::Vec3 firstSample = translationChannel[0];
				ITGEOM::Vec3 lastSample = translationChannel[numFrames];
				if (!lastSample.IsEquiv(firstSample, g_fEpsilonTranslation)) {
					IWARN("Looping movement anim joint '%s' translation does not loop perfectly\n", hierarchyDesc.m_joints[iJoint].m_name.c_str());
				}
				translationChannel.SetNumSamples(numFrames);
			}
		}
		for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < numFloatAnims; ++iFloatAnimIndex) {
			int iFloatChannel = binding.m_floatAnimIdToFloatId[iFloatAnimIndex];
			if (iFloatChannel < 0) {
				continue;
			}
			SampledAnim& floatChannel = *sourceData.GetSampledAnim(kChannelTypeScalar, iFloatAnimIndex);
			if (!floatChannel.IsConstant()) {
				float firstSample = floatChannel[0];
				float lastSample = floatChannel[numFrames];
				if (fabsf(lastSample - firstSample) > g_fEpsilonFloat) {
					IWARN("Looping movement anim float channel %d does not loop perfectly\n", iFloatChannel);
				}
				floatChannel.SetNumSamples(numFrames);
			}
		}
		sourceData.m_numFrames = numFrames;
	}
	return true;
}

unsigned ResolveFrame(float fFrame, unsigned numFrames, bool bLooping, float& fTweenFactor)
{
	if (numFrames == 0) {
		fTweenFactor = 0.0f;
		return 0;
	}
	float fNumFrames = (float)numFrames;
	if (bLooping) {
		if (fFrame < 0.0f)
			fFrame = fNumFrames - fmodf(-fFrame, fNumFrames);
		else if (fFrame >= fNumFrames)
			fFrame = fmodf(fFrame, fNumFrames);
	} else {
		if (fFrame <= 0.0f) {
			fTweenFactor = 0.0f;
			return 0;
		} else if (fFrame >= fNumFrames - 1.0f) {
			fTweenFactor = 0.0f;
			return numFrames-1;
		}
	}
	unsigned iFrame = (unsigned)fFrame;
	fTweenFactor = fFrame - (float)iFrame;
	if (fTweenFactor > 0.999f) {
		fTweenFactor = 0.0f;
		++iFrame;
	} else if (fTweenFactor < 0.001f)
		fTweenFactor = 0.0f;
	return iFrame;
}

void SubtractAnimation(AnimationClipSourceData& sourceData, 
					   AnimationBinding& binding, 
					   AnimationClipSourceData const& left, 
					   AnimationBinding const& leftBinding, 
					   AnimationClipSourceData const& right, 
					   AnimationBinding const& rightBinding, 
					   float fRightFrameStart, 
					   float fRightFrameStep)
{
	ITASSERT(	left.GetNumAnims(kChannelTypeScale) == leftBinding.m_jointAnimIdToJointId.size() &&
				left.GetNumAnims(kChannelTypeRotation) == leftBinding.m_jointAnimIdToJointId.size() &&
				left.GetNumAnims(kChannelTypeTranslation) == leftBinding.m_jointAnimIdToJointId.size() &&
				left.GetNumAnims(kChannelTypeScalar) == leftBinding.m_floatAnimIdToFloatId.size() );
	ITASSERT(	right.GetNumAnims(kChannelTypeScale) == rightBinding.m_jointAnimIdToJointId.size() &&
				right.GetNumAnims(kChannelTypeRotation) == rightBinding.m_jointAnimIdToJointId.size() &&
				right.GetNumAnims(kChannelTypeTranslation) == rightBinding.m_jointAnimIdToJointId.size() &&
				right.GetNumAnims(kChannelTypeScalar) == rightBinding.m_floatAnimIdToFloatId.size() );

	unsigned const numLeftJointAnims = (unsigned)leftBinding.m_jointAnimIdToJointId.size();
	unsigned const numLeftFloatAnims = (unsigned)leftBinding.m_floatAnimIdToFloatId.size();

	if (leftBinding.m_jointIdToJointAnimId.size() != rightBinding.m_jointIdToJointAnimId.size() ||
		leftBinding.m_floatIdToFloatAnimId.size() != rightBinding.m_floatIdToFloatAnimId.size()) {
		ITASSERTF(0, ("Can't subtract animations from different hierarchies - number of joints and number of float channels must match!\n"));
		return;
	}
	if ((left.m_numFrames != 1) && (left.m_flags & kClipLooping) && (right.m_numFrames != 1) && (fRightFrameStep != 0.0f)) {
		if (!(right.m_flags & kClipLooping)) {
			IWARN("SubtractAnimation: subtracting a non-looping animation may compromise looping of output animation!\n");
		} else {
			float fRightNumLoops = left.m_numFrames * fabsf(fRightFrameStep) / (float)right.m_numFrames;
			if ( fabsf( fRightNumLoops - floorf(fRightNumLoops + 0.5f) ) * (float)right.m_numFrames > 0.001f ) {
				IWARN("SubtractAnimation: subtracting a non-looping part of an animation may compromise looping of output animation!\n");
			}
		}
	}
	bool bRightLooping = (right.m_flags & kClipLooping) != 0;
	unsigned rightNumFrames = right.m_numFrames;
	if (right.m_numFrames <= 1) {
		rightNumFrames = 1;
		bRightLooping = false;
		fRightFrameStart = fRightFrameStep = 0.0f;
	} else if (left.m_numFrames <= 1) {
		fRightFrameStep = 0.0f;
		bRightLooping = false;
	}

	AnimationClipSourceData out;
	out.m_flags = sourceData.m_flags | kClipAdditive;
	out.m_numFrames = sourceData.m_numFrames;

	AnimationBinding outBinding;
	outBinding.m_hierarchyId = leftBinding.m_hierarchyId;
	outBinding.m_animatedJointIndexToJointAnimId.resize(leftBinding.m_animatedJointIndexToJointAnimId.size(), -1);
	outBinding.m_jointIdToJointAnimId.resize(leftBinding.m_jointIdToJointAnimId.size(), -1);
	outBinding.m_floatIdToFloatAnimId.resize(leftBinding.m_floatIdToFloatAnimId.size(), -1);

	unsigned iOutJointAnimIndex = 0;
	for (unsigned iLeftJointAnimIndex = 0; iLeftJointAnimIndex < numLeftJointAnims; ++iLeftJointAnimIndex) {
		int iJoint = leftBinding.m_jointAnimIdToJointId[iLeftJointAnimIndex];
		int iAnimatedJoint = leftBinding.m_jointAnimIdToAnimatedJointIndex[iLeftJointAnimIndex];
		if (iJoint < 0 || iAnimatedJoint < 0)
			continue;
		ITASSERT((unsigned)iJoint < (unsigned)leftBinding.m_jointIdToJointAnimId.size());
		ITASSERT((unsigned)iAnimatedJoint < (unsigned)leftBinding.m_animatedJointIndexToJointAnimId.size());
		int iRightJointAnimIndex = rightBinding.m_jointIdToJointAnimId[iJoint];
		if ((unsigned)iRightJointAnimIndex >= (unsigned)rightBinding.m_jointAnimIdToJointId.size()) {
			IWARN("SubtractAnimation: Right animation is not defined for ajoint %u; discarding channel\n", iAnimatedJoint);
			continue;
		}
		bool const bRightConstantScale = right.IsConstant(kChannelTypeScale, iRightJointAnimIndex);
		bool const bRightConstantRotation = right.IsConstant(kChannelTypeRotation, iRightJointAnimIndex);
		bool const bRightConstantTranslation = right.IsConstant(kChannelTypeTranslation, iRightJointAnimIndex);
		bool const bLeftConstantScale = left.IsConstant(kChannelTypeScale, iLeftJointAnimIndex);
		bool const bLeftConstantRotation = left.IsConstant(kChannelTypeRotation, iLeftJointAnimIndex);
		bool const bLeftConstantTranslation = left.IsConstant(kChannelTypeTranslation, iLeftJointAnimIndex);

		bool bIdentity = true, bConstantScale = true, bConstantRotation = true, bConstantTranslation = true;
		ITGEOM::Vec3Array outScales, outTranslations;
		ITGEOM::QuatArray outRotations;

		SampledAnim const& leftScales = *left.GetSampledAnim(kChannelTypeScale, iLeftJointAnimIndex);
		SampledAnim const& rightScales = *right.GetSampledAnim(kChannelTypeScale, iRightJointAnimIndex);
		ITASSERT(bLeftConstantScale || (leftScales.GetNumSamples() == out.m_numFrames));
		ITASSERT(bRightConstantScale || (rightScales.GetNumSamples() == rightNumFrames));

		float fRightTweenFactor;
		ITGEOM::Vec3 rightScale;
		if (bRightConstantScale) {
			rightScale = rightScales[0];
		} else {
			unsigned iRightFrame = ResolveFrame(fRightFrameStart, rightNumFrames, bRightLooping, fRightTweenFactor);
			if (fRightTweenFactor <= 0.0f)
			{
				rightScale = rightScales[iRightFrame].operator ITGEOM::Vec3();
			}
			else
			{
				rightScale = ITGEOM::Lerp(
					rightScales[iRightFrame].operator ITGEOM::Vec3(),
					rightScales[(iRightFrame + 1) % rightNumFrames].operator ITGEOM::Vec3(),
					fRightTweenFactor);
			}
		}
		if (bLeftConstantScale && (bRightConstantScale || (fRightFrameStep == 0.0f))) {
			ITGEOM::Vec3 leftScale = leftScales[0];
			ITGEOM::Vec3 outScale = leftScale / rightScale;
			outScales.push_back( outScale );
			bIdentity = bIdentity && outScale.IsEquiv(kIdentityScale, g_fEpsilonScale);
		} else {
			if (bLeftConstantScale) {
				ITGEOM::Vec3 leftScale = leftScales[0];
				ITGEOM::Vec3 outScale0 = leftScale / rightScale;
				outScales.push_back( outScale0 );
				bIdentity = bIdentity && outScale0.IsEquiv(kIdentityScale, g_fEpsilonScale);
				for (unsigned iFrame = 1; iFrame < out.m_numFrames; ++iFrame) {
					unsigned iRightFrame = ResolveFrame(fRightFrameStart + iFrame * fRightFrameStep, rightNumFrames, bRightLooping, fRightTweenFactor);
					if (fRightTweenFactor <= 0.0f)
					{
						rightScale = rightScales[iRightFrame].operator ITGEOM::Vec3();
					}
					else
					{
						rightScale = ITGEOM::Lerp(
							rightScales[iRightFrame].operator ITGEOM::Vec3(), 
							rightScales[(iRightFrame+1)%rightNumFrames].operator ITGEOM::Vec3(), 
							fRightTweenFactor);
					}
					ITGEOM::Vec3 outScale = leftScale / rightScale;
					outScales.push_back( outScale );
					bConstantScale = bConstantScale && outScale.IsEquiv(outScale0, g_fEpsilonScale);
					bIdentity = bIdentity && outScale.IsEquiv(kIdentityScale, g_fEpsilonScale);
				}
			} else if (bRightConstantScale || (fRightFrameStep == 0.0f)) {
				ITGEOM::Vec3 rightScaleInv = 1.0f / rightScale;
				ITGEOM::Vec3 leftScale = leftScales[0];
				ITGEOM::Vec3 outScale0 = leftScale * rightScaleInv;
				outScales.push_back( outScale0 );
				bIdentity = bIdentity && outScale0.IsEquiv(kIdentityScale, g_fEpsilonScale);
				for (unsigned iFrame = 1; iFrame < out.m_numFrames; ++iFrame) {
					leftScale = leftScales[iFrame];
					ITGEOM::Vec3 outScale = leftScale * rightScaleInv;
					outScales.push_back( outScale );
					bConstantScale = bConstantScale && outScale.IsEquiv(outScale0, g_fEpsilonScale);
					bIdentity = bIdentity && outScale.IsEquiv(kIdentityScale, g_fEpsilonScale);
				}
			} else {
				ITGEOM::Vec3 leftScale = leftScales[0];
				ITGEOM::Vec3 outScale0 = leftScale / rightScale;
				outScales.push_back( outScale0 );
				bIdentity = bIdentity && outScale0.IsEquiv(kIdentityScale, g_fEpsilonScale);
				for (unsigned iFrame = 1; iFrame < out.m_numFrames; ++iFrame) {
					unsigned iRightFrame = ResolveFrame(fRightFrameStart + iFrame * fRightFrameStep, rightNumFrames, bRightLooping, fRightTweenFactor);
					if (fRightTweenFactor <= 0.0f)
					{
						rightScale = rightScales[iRightFrame];
					}
					else
					{
						rightScale = ITGEOM::Lerp(
							rightScales[iRightFrame].operator ITGEOM::Vec3(), 
							rightScales[(iRightFrame + 1) % rightNumFrames].operator ITGEOM::Vec3(), 
							fRightTweenFactor);
					}
					leftScale = leftScales[iFrame];
					ITGEOM::Vec3 outScale = leftScale / rightScale;
					outScales.push_back( outScale );
					bConstantScale = bConstantScale && outScale.IsEquiv(outScale0, g_fEpsilonScale);
					bIdentity = bIdentity && outScale.IsEquiv(kIdentityScale, g_fEpsilonScale);
				}
			}
			if (bConstantScale) {
				outScales.resize(1, outScales[0]);
			} else {
//				ITASSERT(!bIdentity);
			}
		}

		SampledAnim const& leftRotations = *left.GetSampledAnim(kChannelTypeRotation, iLeftJointAnimIndex);
		SampledAnim const& rightRotations = *right.GetSampledAnim(kChannelTypeRotation, iRightJointAnimIndex);
		ITASSERT(bLeftConstantRotation || (leftRotations.GetNumSamples() == out.m_numFrames));
		ITASSERT(bRightConstantRotation || (rightRotations.GetNumSamples() == rightNumFrames));
		ITGEOM::Quat rightRotation;
		if (bRightConstantRotation) {
			rightRotation = rightRotations[0];
		} else {
			unsigned iRightFrame = ResolveFrame(fRightFrameStart, rightNumFrames, bRightLooping, fRightTweenFactor);
			if (fRightTweenFactor <= 0.0f)
			{
				rightRotation = rightRotations[iRightFrame].operator ITGEOM::Quat();
			}
			else
			{
				rightRotation = ITGEOM::Slerp(
					rightRotations[iRightFrame].operator ITGEOM::Quat(), 
					rightRotations[(iRightFrame+1)%rightNumFrames].operator ITGEOM::Quat(), 
					fRightTweenFactor);
			}
		}
		if (bLeftConstantRotation && (bRightConstantRotation || (fRightFrameStep == 0.0f))) {
			ITGEOM::Quat outRotation = rightRotation.Conjugate() * (ITGEOM::Quat&)leftRotations[0];
			if (outRotation.w < 0.0f) outRotation *= -1.0f;
			outRotations.push_back( outRotation );
			bIdentity = bIdentity && outRotation.IsEquiv(kIdentityRotation, g_fEpsilonRotation);
		} else {
			if (bLeftConstantRotation) {
				ITGEOM::Quat leftRotation = leftRotations[0];
				ITGEOM::Quat outRotation0 = rightRotation.Conjugate() * leftRotation;
				if (outRotation0.w < 0.0f) outRotation0 *= -1.0f;
				outRotations.push_back( outRotation0 );
				bIdentity = bIdentity && outRotation0.IsEquiv(kIdentityRotation, g_fEpsilonRotation);
				for (unsigned iFrame = 1; iFrame < out.m_numFrames; ++iFrame) {
					unsigned iRightFrame = ResolveFrame(fRightFrameStart + iFrame * fRightFrameStep, rightNumFrames, bRightLooping, fRightTweenFactor);
					if (fRightTweenFactor <= 0.0f)
					{
						rightRotation = rightRotations[iRightFrame].operator ITGEOM::Quat();
					}
					else
					{
						rightRotation = ITGEOM::Slerp(
							rightRotations[iRightFrame].operator ITGEOM::Quat(), 
							rightRotations[(iRightFrame+1)%rightNumFrames].operator ITGEOM::Quat(), 
							fRightTweenFactor);
					}
					ITGEOM::Quat outRotation = rightRotation.Conjugate() * leftRotation;
					if (outRotation.w < 0.0f) outRotation *= -1.0f;
					outRotations.push_back( outRotation );
					bConstantRotation = bConstantRotation && outRotation.IsEquiv(outRotation0, g_fEpsilonRotation);
					bIdentity = bIdentity && outRotation.IsEquiv(kIdentityRotation, g_fEpsilonRotation);
				}
			} else if (bRightConstantRotation || (fRightFrameStep == 0.0f)) {
				ITGEOM::Quat rightRotationInv = rightRotation.Conjugate();
				ITGEOM::Quat outRotation0 = rightRotationInv * (ITGEOM::Quat&)leftRotations[0];
				if (outRotation0.w < 0.0f) outRotation0 *= -1.0f;
				outRotations.push_back( outRotation0 );
				bIdentity = bIdentity && outRotation0.IsEquiv(kIdentityRotation, g_fEpsilonRotation);
				for (unsigned iFrame = 1; iFrame < out.m_numFrames; ++iFrame) {
					ITGEOM::Quat outRotation = rightRotationInv * (ITGEOM::Quat&)leftRotations[iFrame];
					if (outRotation.w < 0.0f) outRotation *= -1.0f;
					outRotations.push_back( outRotation );
					bConstantRotation = bConstantRotation && outRotation.IsEquiv(outRotation0, g_fEpsilonRotation);
					bIdentity = bIdentity && outRotation.IsEquiv(kIdentityRotation, g_fEpsilonRotation);
				}
			} else {
				ITGEOM::Quat outRotation0 = rightRotation.Conjugate() * (ITGEOM::Quat&)leftRotations[0];
				if (outRotation0.w < 0.0f) outRotation0 *= -1.0f;
				outRotations.push_back( outRotation0 );
				bIdentity = bIdentity && outRotation0.IsEquiv(kIdentityRotation, g_fEpsilonRotation);
				for (unsigned iFrame = 1; iFrame < out.m_numFrames; ++iFrame) {
					unsigned iRightFrame = ResolveFrame(fRightFrameStart + iFrame * fRightFrameStep, rightNumFrames, bRightLooping, fRightTweenFactor);
					if (fRightTweenFactor <= 0.0f)
					{
						rightRotation = rightRotations[iRightFrame].operator ITGEOM::Quat();

					}
					else
					{
						rightRotation = ITGEOM::Slerp(
							rightRotations[iRightFrame].operator ITGEOM::Quat(), 
							rightRotations[(iRightFrame + 1) % rightNumFrames].operator ITGEOM::Quat(),
							fRightTweenFactor);
					}
					ITGEOM::Quat outRotation = rightRotation.Conjugate() * (ITGEOM::Quat&)leftRotations[iFrame];
					if (outRotation.w < 0.0f) outRotation *= -1.0f;
					outRotations.push_back( outRotation );
					bConstantRotation = bConstantRotation && outRotation.IsEquiv(outRotation0, g_fEpsilonRotation);
					bIdentity = bIdentity && outRotation.IsEquiv(kIdentityRotation, g_fEpsilonRotation);
				}
			}
			if (bConstantRotation) {
				outRotations.resize(1, outRotations[0]);
			} /*else {
//				ITASSERT(!bIdentity);
			}*/
		}

		SampledAnim const& leftTranslations = *left.GetSampledAnim(kChannelTypeTranslation, iLeftJointAnimIndex);
		SampledAnim const& rightTranslations = *right.GetSampledAnim(kChannelTypeTranslation, iRightJointAnimIndex);
		ITASSERT(bLeftConstantTranslation || (leftTranslations.GetNumSamples() == out.m_numFrames));
		ITASSERT(bRightConstantTranslation || (rightTranslations.GetNumSamples() == rightNumFrames));
		ITGEOM::Vec3 rightTranslation;
		{
			unsigned iRightFrame = ResolveFrame(fRightFrameStart, rightNumFrames, bRightLooping, fRightTweenFactor);
			rightScale = rightScales[0];
			rightRotation = rightRotations[0];
			rightTranslation = rightTranslations[0];
			if (fRightTweenFactor <= 0.0f) {
				if (!bRightConstantScale)
					rightScale = rightScales[iRightFrame];
				if (!bRightConstantRotation)
					rightRotation = rightRotations[iRightFrame];
				if (!bRightConstantTranslation)
					rightTranslation = rightTranslations[iRightFrame];
			} else {
				unsigned iRightFrameNext = (iRightFrame+1)%rightNumFrames;
				if (!bRightConstantScale)
					rightScale = ITGEOM::Lerp((ITGEOM::Vec3&)rightScales[iRightFrame], rightScales[iRightFrameNext], fRightTweenFactor);
				if (!bRightConstantRotation)
					rightRotation = ITGEOM::Slerp(rightRotations[iRightFrame], rightRotations[iRightFrameNext], fRightTweenFactor);
				if (!bRightConstantTranslation)
					rightTranslation = ITGEOM::Lerp((ITGEOM::Vec3&)rightTranslations[iRightFrame], rightTranslations[iRightFrameNext], fRightTweenFactor);
			}
		}
		if (bLeftConstantTranslation && ((fRightFrameStep == 0.0f) || bRightConstantTranslation)) {
			if (((ITGEOM::Vec3&)leftTranslations[0]).IsEquiv(rightTranslation, g_fEpsilonTranslation)) {
				// if our left and right animations have constant identical translations, we know the output is
				// identity, even if our reference rotations or scales are animated:
				outTranslations.push_back( kIdentityTranslation );
			} else if ((fRightFrameStep == 0.0f) || (bRightConstantScale && bRightConstantRotation)) {
				// else if our reference animation also has constant scale and rotation, we'll still have a
				// constant output translation:
				ITGEOM::Vec3 const& s = rightScale;
				ITGEOM::Quat const& q = rightRotation;
				ITGEOM::Vec3 outTranslation = (ITGEOM::Vec3&)leftTranslations[0] - rightTranslation;
				outTranslation = q.Conjugate().RotateVector(outTranslation) / s;
				outTranslations.push_back( outTranslation );
				bIdentity = bIdentity && outTranslation.IsEquiv(kIdentityTranslation, g_fEpsilonTranslation);
			} else {
				// otherwise, the reference animation scale or rotation animation may force us to animate
				// our output translation to compensate:
				bConstantTranslation = false;
			}
		} else {
			// if our left translation is animated, we need to do the subtraction
			bConstantTranslation = false;
		}
		if (!bConstantTranslation) {
			bConstantTranslation = true;
			if (bLeftConstantTranslation) {
				ITGEOM::Vec3 const leftTranslation = leftTranslations[0];
				ITGEOM::Vec3 sInv = 1.0f / rightScale;
				ITGEOM::Quat qInv = rightRotation.Conjugate();
				ITGEOM::Vec3 outTranslation0 = leftTranslation - rightTranslation;
				outTranslation0 = qInv.RotateVector(outTranslation0) * sInv;
				outTranslations.push_back( outTranslation0 );
				bIdentity = bIdentity && outTranslation0.IsEquiv(kIdentityTranslation, g_fEpsilonTranslation);
				bool const bRightAnimateScale = !(fRightFrameStep == 0.0f) && !bRightConstantScale;
				bool const bRightAnimateRotation = !(fRightFrameStep == 0.0f) && !bRightConstantRotation;
				for (unsigned iFrame = 1; iFrame < out.m_numFrames; ++iFrame) {
					unsigned iRightFrame = ResolveFrame(fRightFrameStart + iFrame * fRightFrameStep, rightNumFrames, bRightLooping, fRightTweenFactor);
					if (fRightTweenFactor <= 0.0f) {
						if (bRightAnimateScale)
							sInv = 1.0f / (ITGEOM::Vec3&)rightScales[iRightFrame];
						if (bRightAnimateRotation)
							qInv = ((ITGEOM::Quat&)rightRotations[iRightFrame]).Conjugate();
						rightTranslation = rightTranslations[iRightFrame];
					} else {
						unsigned iRightFrameNext = (iRightFrame + 1)%right.m_numFrames;
						if (bRightAnimateScale)
							sInv = 1.0f / ITGEOM::Lerp((ITGEOM::Vec3&)rightScales[iRightFrame], rightScales[iRightFrameNext], fRightTweenFactor);
						if (bRightAnimateRotation)
							qInv = ITGEOM::Slerp(rightRotations[iRightFrame], rightRotations[iRightFrameNext], fRightTweenFactor).Conjugate();
						rightTranslation = ITGEOM::Lerp((ITGEOM::Vec3&)rightTranslations[iRightFrame], rightTranslations[iRightFrameNext], fRightTweenFactor);
					}
					ITGEOM::Vec3 outTranslation = leftTranslation - rightTranslation;
					outTranslation = qInv.RotateVector(outTranslation) * sInv;
					outTranslations.push_back( outTranslation );
					bConstantTranslation = bConstantTranslation && outTranslation.IsEquiv(outTranslation0, g_fEpsilonTranslation);
					bIdentity = bIdentity && outTranslation.IsEquiv(kIdentityTranslation, g_fEpsilonTranslation);
				}
			} else if ((fRightFrameStep == 0.0f) || (bRightConstantScale && bRightConstantRotation && bRightConstantTranslation)) {
				ITGEOM::Vec3 sInv = 1.0f / rightScale;
				ITGEOM::Quat qInv = rightRotation.Conjugate();
				ITGEOM::Vec3 outTranslation0 = (ITGEOM::Vec3&)leftTranslations[0] - rightTranslation;
				outTranslation0 = qInv.RotateVector(outTranslation0) * sInv;
				outTranslations.push_back( outTranslation0 );
				bIdentity = bIdentity && outTranslation0.IsEquiv(kIdentityTranslation, g_fEpsilonTranslation);
				for (unsigned iFrame = 1; iFrame < out.m_numFrames; ++iFrame) {
					ITGEOM::Vec3 outTranslation = (ITGEOM::Vec3&)leftTranslations[iFrame] - rightTranslation;
					outTranslation = qInv.RotateVector(outTranslation) * sInv;
					outTranslations.push_back( outTranslation );
					bConstantTranslation = bConstantTranslation && outTranslation.IsEquiv(outTranslation0, g_fEpsilonTranslation);
					bIdentity = bIdentity && outTranslation.IsEquiv(kIdentityTranslation, g_fEpsilonTranslation);
				}
			} else {
				ITGEOM::Vec3 sInv = 1.0f / rightScale;
				ITGEOM::Quat qInv = rightRotation.Conjugate();
				ITGEOM::Vec3 outTranslation0 = (ITGEOM::Vec3&)leftTranslations[0] - rightTranslation;
				outTranslation0 = qInv.RotateVector(outTranslation0) * sInv;
				outTranslations.push_back( outTranslation0 );
				bIdentity = bIdentity && outTranslation0.IsEquiv(kIdentityTranslation, g_fEpsilonTranslation);
				bool const bRightAnimateScale = !(fRightFrameStep == 0.0f) && !bRightConstantScale;
				bool const bRightAnimateRotation = !(fRightFrameStep == 0.0f) && !bRightConstantRotation;
				for (unsigned iFrame = 1; iFrame < out.m_numFrames; ++iFrame) {
					unsigned iRightFrame = ResolveFrame(fRightFrameStart + iFrame * fRightFrameStep, rightNumFrames, bRightLooping, fRightTweenFactor);
					if (fRightTweenFactor <= 0.0f) {
						if (bRightAnimateScale)
							sInv = 1.0f / (ITGEOM::Vec3&)rightScales[iRightFrame];
						if (bRightAnimateRotation)
							qInv = ((ITGEOM::Quat&)rightRotations[iRightFrame]).Conjugate();
						rightTranslation = rightTranslations[iRightFrame];
					} else {
						unsigned iRightFrameNext = (iRightFrame + 1)%right.m_numFrames;
						if (bRightAnimateScale)
							sInv = 1.0f / ITGEOM::Lerp((ITGEOM::Vec3&)rightScales[iRightFrame], rightScales[iRightFrameNext], fRightTweenFactor);
						if (bRightAnimateRotation)
							qInv = ITGEOM::Slerp(rightRotations[iRightFrame], rightRotations[iRightFrameNext], fRightTweenFactor).Conjugate();
						rightTranslation = ITGEOM::Lerp((ITGEOM::Vec3&)rightTranslations[iRightFrame], rightTranslations[iRightFrameNext], fRightTweenFactor);
					}
					ITGEOM::Vec3 outTranslation = (ITGEOM::Vec3&)leftTranslations[iFrame] - rightTranslation;
					outTranslation = qInv.RotateVector(outTranslation) * sInv;
					outTranslations.push_back( outTranslation );
					bConstantTranslation = bConstantTranslation && outTranslation.IsEquiv(outTranslation0, g_fEpsilonTranslation);
					bIdentity = bIdentity && outTranslation.IsEquiv(kIdentityTranslation, g_fEpsilonTranslation);
				}
			}
			if (bConstantTranslation) {
				outTranslations.resize(1, outTranslations[0]);
			} else {
//				ITASSERT(!bIdentity);
			}
		}
// 		if (bIdentity) {
// 			INOTE(IMsg::kVerbose, "SubtractAnimation: Left and right are identical for ajoint %u; discarding identity channel\n", iAnimatedJoint);
// 			continue;
// 		}
		out.AddSampledAnim(kChannelTypeScale, outScales);
		out.AddSampledAnim(kChannelTypeRotation, outRotations );
		out.AddSampledAnim(kChannelTypeTranslation, outTranslations );

		outBinding.m_jointAnimIdToAnimatedJointIndex.push_back(iAnimatedJoint);
		outBinding.m_jointAnimIdToJointId.push_back(iJoint);
		outBinding.m_animatedJointIndexToJointAnimId[iAnimatedJoint] = iOutJointAnimIndex;
		outBinding.m_jointIdToJointAnimId[iJoint] = iOutJointAnimIndex;
		++iOutJointAnimIndex;
	}

	unsigned iOutFloatAnimIndex = 0;
	for (unsigned iLeftFloatAnimIndex = 0; iLeftFloatAnimIndex < numLeftFloatAnims; ++iLeftFloatAnimIndex) {
		int iFloat = leftBinding.m_floatAnimIdToFloatId[iLeftFloatAnimIndex];
		if (iFloat < 0)
			continue;
		ITASSERT((unsigned)iFloat < (unsigned)leftBinding.m_floatIdToFloatAnimId.size());
		int iRightFloatAnimIndex = rightBinding.m_floatIdToFloatAnimId[iFloat];
		if ((unsigned)iRightFloatAnimIndex >= (unsigned)rightBinding.m_floatAnimIdToFloatId.size()) {
			IWARN("SubtractAnimation: Right animation is not defined for float channel %d; discarding channel\n", iFloat);
			continue;
		}
		bool const bRightConstantFloat = right.IsConstant(kChannelTypeScalar, iRightFloatAnimIndex);
		bool const bLeftConstantFloat = left.IsConstant(kChannelTypeScalar, iLeftFloatAnimIndex);

		bool bIdentity = true, bConstantFloat = true;
		std::vector<float> outFloats;

		SampledAnim const& leftFloats = *left.GetSampledAnim(kChannelTypeScalar, iLeftFloatAnimIndex);
		SampledAnim const& rightFloats = *right.GetSampledAnim(kChannelTypeScalar, iRightFloatAnimIndex);
		ITASSERT(bLeftConstantFloat || (leftFloats.GetNumSamples() == out.m_numFrames));
		ITASSERT(bRightConstantFloat || (rightFloats.GetNumSamples() == rightNumFrames));
		float fRightTweenFactor;
		float fRightFloat;
		if (bRightConstantFloat) {
			fRightFloat = rightFloats[0];
		} else {
			unsigned iRightFrame = ResolveFrame(fRightFrameStart, rightNumFrames, bRightLooping, fRightTweenFactor);
			if (fRightTweenFactor <= 0.0f)
			{
				fRightFloat = rightFloats[iRightFrame].operator float();
			}
			else
			{
				fRightFloat = 
					rightFloats[iRightFrame].operator float() * (1.0f - fRightTweenFactor) + 
					rightFloats[(iRightFrame+1)%rightNumFrames].operator float() * fRightTweenFactor;
			}
		}
		if (bLeftConstantFloat && (bRightConstantFloat || (fRightFrameStep == 0.0f))) {
			float outFloat = leftFloats[0] - fRightFloat;
			outFloats.push_back( outFloat );
			bIdentity = bIdentity && fabsf(outFloat) <= g_fEpsilonFloat;
		} else {
			if (bLeftConstantFloat) {
				float leftFloat = leftFloats[0];
				float outFloat0 = leftFloat - fRightFloat;
				outFloats.push_back( outFloat0 );
				bIdentity = bIdentity && fabsf(outFloat0) <= g_fEpsilonFloat;
				for (unsigned iFrame = 1; iFrame < out.m_numFrames; ++iFrame) {
					unsigned iRightFrame = ResolveFrame(fRightFrameStart + iFrame * fRightFrameStep, rightNumFrames, bRightLooping, fRightTweenFactor);
					if (fRightTweenFactor <= 0.0f)
					{
						fRightFloat = rightFloats[iRightFrame].operator float();
					}
					else
					{
						fRightFloat = 
							rightFloats[iRightFrame].operator float() * (1.0f - fRightTweenFactor) + 
							rightFloats[(iRightFrame+1)%rightNumFrames].operator float() * fRightTweenFactor;
					}
					float outFloat = leftFloat - fRightFloat;
					outFloats.push_back( outFloat );
					bConstantFloat = bConstantFloat && fabsf(outFloat - outFloat0) <= g_fEpsilonFloat;
					bIdentity = bIdentity && fabsf(outFloat) <= g_fEpsilonFloat;
				}
			} else if (bRightConstantFloat || (fRightFrameStep == 0.0f)) {
				float outFloat0 = leftFloats[0] - fRightFloat;
				outFloats.push_back( outFloat0 );
				bIdentity = bIdentity && fabsf(outFloat0) <= g_fEpsilonFloat;
				for (unsigned iFrame = 1; iFrame < out.m_numFrames; ++iFrame) {
					float outFloat = leftFloats[iFrame] - fRightFloat;
					outFloats.push_back( outFloat );
					bConstantFloat = bConstantFloat && fabsf(outFloat - outFloat0) <= g_fEpsilonFloat;
					bIdentity = bIdentity && fabsf(outFloat) <= g_fEpsilonFloat;
				}
			} else {
				float outFloat0 = leftFloats[0] - fRightFloat;
				outFloats.push_back( outFloat0 );
				bIdentity = bIdentity && fabsf(outFloat0) <= g_fEpsilonFloat;
				for (unsigned iFrame = 1; iFrame < out.m_numFrames; ++iFrame) {
					unsigned iRightFrame = ResolveFrame(fRightFrameStart + iFrame * fRightFrameStep, rightNumFrames, bRightLooping, fRightTweenFactor);
					if (fRightTweenFactor <= 0.0f)
					{
						fRightFloat = rightFloats[iRightFrame].operator float();
					}
					else
					{
						fRightFloat = 
							rightFloats[iRightFrame].operator float() * (1.0f - fRightTweenFactor) + 
							rightFloats[(iRightFrame+1)%rightNumFrames].operator float() * fRightTweenFactor;
					}
					float outFloat = leftFloats[iFrame] - fRightFloat;
					outFloats.push_back( outFloat );
					bConstantFloat = bConstantFloat && fabsf(outFloat - outFloat0) <= g_fEpsilonFloat;
					bIdentity = bIdentity && fabsf(outFloat) <= g_fEpsilonFloat;
				}
			}
			if (bConstantFloat) {
				outFloats.resize(1, outFloats[0]);
			} else {
//				ITASSERT(!bIdentity);
			}
		}
// 		if (bIdentity) {
// 			INOTE("SubtractAnimation: Left and right are identical for float channel %d; discarding identity channel\n", iFloat);
// 			continue;
// 		}
		out.AddSampledAnim(kChannelTypeScalar, outFloats);
		outBinding.m_floatAnimIdToFloatId.push_back(iFloat);
		outBinding.m_floatIdToFloatAnimId[iFloat] = iOutFloatAnimIndex++;
	}

	// need to count the number of frames in the output anim since we might have removed some animations due to them being constant or identity
	out.SetNumFrames();

	binding = outBinding;
	sourceData = out;
}

void CollectRootSpaceErrorParams(std::vector<JointRootSpaceErrorTarget>& targetJoints,
								 std::vector<int>& animatedJointIndexToMetadataId,
								 AnimMetaData const& metaData, 
								 AnimationHierarchyDesc const& hierarchyDesc,
								 AnimationBinding const& binding)
{
	// defaults for joints
	AnimMetaDataJointCompressionMethod const& jc = metaData.m_defaultCompression;
	AnimMetaDataTrackRootSpaceError const& s = jc.m_scale.m_rootSpaceError;
	AnimMetaDataTrackRootSpaceError const& r = jc.m_rotation.m_rootSpaceError;
	AnimMetaDataTrackRootSpaceError const& t = jc.m_translation.m_rootSpaceError;
	JointRootSpaceErrorTarget targetJoint_default =
	{	kChannelIndexDefault, 
		{	{ s.m_fErrorTolerance,	s.m_fErrorDeltaFactor,	s.m_fErrorSpeedFactor	},
			{ r.m_fErrorTolerance,	r.m_fErrorDeltaFactor,	r.m_fErrorSpeedFactor	},
			{ t.m_fErrorTolerance,	t.m_fErrorDeltaFactor,	0.0f					} 
		}
	};
	targetJoints.push_back(targetJoint_default);

	// overrides for named joints
	if (!metaData.m_jointNames.empty()) {
		unsigned const numJoints = (unsigned)hierarchyDesc.m_joints.size();
		unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
		for (unsigned iJoint = 0; iJoint < numJoints; ++iJoint) {
			int i = FindAnimMetaDataIndexForName( hierarchyDesc.m_joints[iJoint].m_name, metaData.m_jointNames );
			if (i < 0) {
				continue; 
			}
			int iAnimatedJoint = JointIdToAnimatedJointIndex(hierarchyDesc, iJoint);
			if ((unsigned)iAnimatedJoint < numAnimatedJoints) {
				animatedJointIndexToMetadataId[iAnimatedJoint] = i;
			}
			AnimMetaDataJointCompressionMethod const& jc = metaData.m_jointCompression[i];
			AnimMetaDataTrackRootSpaceError const& s = jc.m_scale.m_rootSpaceError;
			AnimMetaDataTrackRootSpaceError const& r = jc.m_rotation.m_rootSpaceError;
			AnimMetaDataTrackRootSpaceError const& t = jc.m_translation.m_rootSpaceError;
			JointRootSpaceErrorTarget targetJoint =
			{	iJoint,
				{	{ s.m_fErrorTolerance, s.m_fErrorDeltaFactor, s.m_fErrorSpeedFactor },
					{ r.m_fErrorTolerance, r.m_fErrorDeltaFactor, r.m_fErrorSpeedFactor },
					{ t.m_fErrorTolerance, t.m_fErrorDeltaFactor, 0.0f					} 
				}
			};
			targetJoints.push_back(targetJoint);
		}
	}
}

void CollectRootSpaceErrorParams(std::vector<FloatRootSpaceErrorTarget>& targetFloatChannels,
								 std::vector<int>& floatIdToMetadataId,
								 AnimMetaData const& metaData,
								 AnimationHierarchyDesc const& hierarchyDesc,
								 AnimationBinding const& binding)
{
	// defaults for floats
	AnimMetaDataTrackRootSpaceError const& f = metaData.m_defaultCompressionFloat.m_rootSpaceError;
	FloatRootSpaceErrorTarget targetFloatChannel_default = 
	{	kChannelIndexDefault, 
		{ f.m_fErrorTolerance, f.m_fErrorDeltaFactor, 0.0f } 
	};
	targetFloatChannels.push_back(targetFloatChannel_default);
	
	// overrides for named floats
	if (!metaData.m_floatNames.empty()) {
		unsigned const numFloatChannels = (unsigned)binding.m_floatIdToFloatAnimId.size();
		for (unsigned iFloatChannel = 0; iFloatChannel < numFloatChannels; ++iFloatChannel) {
			int i = FindAnimMetaDataIndexForName( hierarchyDesc.m_floatChannels[iFloatChannel].m_name, metaData.m_floatNames );
			if (i < 0) {
				continue;
			}
			floatIdToMetadataId[iFloatChannel] = i;
			AnimMetaDataTrackRootSpaceError const& f = metaData.m_floatCompression[i].m_rootSpaceError;
			FloatRootSpaceErrorTarget targetFloatChannel =
			{	iFloatChannel,
				{ f.m_fErrorTolerance, f.m_fErrorDeltaFactor, 0.0f },
			};
			targetFloatChannels.push_back(targetFloatChannel);
		}
	}
}

void CollectLocalSpaceErrorParams(std::vector<JointLocalSpaceErrorTarget>& targetJoints,
								  std::vector<int>& animatedJointIndexToMetadataId,
								  AnimMetaData const& metaData,
								  AnimationHierarchyDesc const& hierarchyDesc, 
								  AnimationBinding const& binding)
{
	// defaults for joints 
	AnimMetaDataJointCompressionMethod const& jc = metaData.m_defaultCompression;
	JointLocalSpaceErrorTarget targetJoint_default =
	{	kChannelIndexDefault,
		{	{	jc.m_scale.m_format.m_fErrorTolerance,			jc.m_scale.m_keyFrames.m_fErrorTolerance		},
			{	jc.m_rotation.m_format.m_fErrorTolerance,		jc.m_rotation.m_keyFrames.m_fErrorTolerance		},
			{	jc.m_translation.m_format.m_fErrorTolerance,	jc.m_translation.m_keyFrames.m_fErrorTolerance	}
		}
	};
	targetJoints.push_back(targetJoint_default);

	// overrides for named joints
	if (!metaData.m_jointNames.empty()) {
		unsigned const numJoints = (unsigned)hierarchyDesc.m_joints.size();
		unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
		for (unsigned iJoint = 0; iJoint < numJoints; ++iJoint) {
			int i = FindAnimMetaDataIndexForName( hierarchyDesc.m_joints[iJoint].m_name, metaData.m_jointNames );
			if (i < 0) {
				continue;
			}
			int iAnimatedJoint = JointIdToAnimatedJointIndex(hierarchyDesc, iJoint);
			if ((unsigned)iAnimatedJoint < numAnimatedJoints) {
				animatedJointIndexToMetadataId[iAnimatedJoint] = i;
			}
			AnimMetaDataJointCompressionMethod const& jc = metaData.m_jointCompression[i];
			JointLocalSpaceErrorTarget targetJoint =
			{	iJoint,
				{	{	jc.m_scale.m_format.m_fErrorTolerance,			jc.m_scale.m_keyFrames.m_fErrorTolerance		},
					{	jc.m_rotation.m_format.m_fErrorTolerance,		jc.m_rotation.m_keyFrames.m_fErrorTolerance		},
					{	jc.m_translation.m_format.m_fErrorTolerance,	jc.m_translation.m_keyFrames.m_fErrorTolerance	}
				}
			};
			targetJoints.push_back(targetJoint);
		}
	}
}

void CollectLocalSpaceErrorParams(std::vector<FloatLocalSpaceErrorTarget>& targetFloatChannels,	// out
							 	  std::vector<int>& floatIdToMetadataId, // out
								  AnimMetaData const& metaData,
								  AnimationHierarchyDesc const& hierarchyDesc,
								  AnimationBinding const& binding)
{
	// defaults for floats	
	AnimMetaDataTrackCompressionMethod const& f = metaData.m_defaultCompressionFloat;
	FloatLocalSpaceErrorTarget targetFloatChannel_default =
	{	kChannelIndexDefault,
		{ f.m_format.m_fErrorTolerance, f.m_keyFrames.m_fErrorTolerance },
	};
	targetFloatChannels.push_back(targetFloatChannel_default);

	// overrides for named floats
	if (!metaData.m_floatNames.empty()) {
		unsigned const numFloatChannels = (unsigned)binding.m_floatIdToFloatAnimId.size();
		for (unsigned iFloatChannel = 0; iFloatChannel < numFloatChannels; ++iFloatChannel) {
			int i = FindAnimMetaDataIndexForName( hierarchyDesc.m_floatChannels[iFloatChannel].m_name, metaData.m_floatNames );
			if (i < 0) {
				continue;
			}
			floatIdToMetadataId[iFloatChannel] = i;
			AnimMetaDataTrackCompressionMethod const& f = metaData.m_floatCompression[i];
			FloatLocalSpaceErrorTarget targetFloatChannel =
			{	iFloatChannel,
				{ f.m_format.m_fErrorTolerance, f.m_keyFrames.m_fErrorTolerance },
			};
			targetFloatChannels.push_back(targetFloatChannel);
		}
	}
}

// build local space error tolerances from metadata
void BuildLocalSpaceErrors(	ClipLocalSpaceErrors&			localSpaceErrors,				// out
							std::vector<int>&				animatedJointIndexToMetadataId,	// out
							std::vector<int>&				floatIdToMetadataId,			// out
							AnimMetaData const&				metaData, 
							AnimationHierarchyDesc const&	hierarchyDesc, 
							AnimationBinding const&			binding, 
							AnimationClipSourceData const&	sourceData )
{
	bool const bRootSpaceErrors = !!(metaData.m_flags & kAnimMetaDataFlagRootSpaceErrors);
	if (bRootSpaceErrors) {	
		// collect root space error tolerance parameters for joint channels from metadata
		std::vector<JointRootSpaceErrorTarget> targetJoints;
		CollectRootSpaceErrorParams(targetJoints, animatedJointIndexToMetadataId, metaData, hierarchyDesc, binding);
		// collect root space error tolerance parameters for float channels from metadata
		std::vector<FloatRootSpaceErrorTarget> targetFloatChannels;	
		CollectRootSpaceErrorParams(targetFloatChannels, floatIdToMetadataId, metaData, hierarchyDesc, binding);
		// generate local space errors structure
		bool const bDeltaOptimizedErrors = !!(metaData.m_flags & kAnimMetaDataFlagDeltaOptimization);
		AnimClipCompression_GenerateLocalSpaceErrors(
			localSpaceErrors,
			binding,
			hierarchyDesc,
			sourceData,
			targetJoints,
			targetFloatChannels,
			bDeltaOptimizedErrors);
	} else {
		// collect local space error tolerance parameters for joint channels from metadata
		std::vector<JointLocalSpaceErrorTarget> targetJoints;
		CollectLocalSpaceErrorParams(targetJoints, animatedJointIndexToMetadataId, metaData, hierarchyDesc, binding);
		// collect local space error tolerance parameters for float channels from metadata
		std::vector<FloatLocalSpaceErrorTarget> targetFloatChannels;
		CollectLocalSpaceErrorParams(targetFloatChannels, floatIdToMetadataId, metaData, hierarchyDesc, binding);
		// generate local space errors structure
		AnimClipCompression_GenerateLocalSpaceErrors(
			localSpaceErrors,
			binding,
			sourceData,
			targetJoints,
			targetFloatChannels);
	}

	// expand const tolerances from metadata into localSpaceErrors (needs local space tolerances so must be called after the above)
	BindAnimMetaDataConstantErrorTolerancesToClip(localSpaceErrors, metaData, hierarchyDesc, binding);
}

struct GenerateBitFormatCall 
{
	AnimChannelType				m_channelType;
	AnimChannelCompressionType	m_compressionType;
	int							m_sharedGroup;	// -1 if not shared
	std::set<size_t>			m_channelSet;	// set of joints or float channels to compress
	std::set<size_t>			m_channelSet2;	// used to hold translation joint set if vector type

	inline bool IsEqual(AnimChannelType chanType, AnimChannelCompressionType compType, int group) const 
	{
		return m_channelType == chanType &&	m_compressionType == compType && m_sharedGroup == group;
	}
};

typedef std::vector<GenerateBitFormatCall> GenerateBitFormatCalls;

GenerateBitFormatCall& GetGenerateBitFormatCall(GenerateBitFormatCalls& calls, 
												AnimChannelType channelType, 
												AnimChannelCompressionType compressionType, 
												int sharedGroup)
{
	// return matching call if one exists
	for (auto it = calls.begin(); it != calls.end(); ++it) {
		if (it->IsEqual(channelType, compressionType, sharedGroup)) {
			return *it;
		}
	}
	// otherwise add a new one
	GenerateBitFormatCall call;
	call.m_channelType = channelType;
	call.m_compressionType = compressionType;
	call.m_sharedGroup = sharedGroup;
	calls.push_back(call);
	return calls.back();
}

// determines whether to generate bit packing format & skip frames for an animated channel (joint or float channels)
void CollectBitFormatAndSkipFrameWorkChannel(GenerateBitFormatCalls& generateBitFormatCalls, 
											 GenerateSkipFrameIndices& genSkipFrames,
											 ClipCompressionDesc& compression, 
											 ChannelType const chanType,
											 AnimMetaDataTrackCompressionMethod const& metaChanCompMethod,
											 AnimationClipSourceData const& sourceData, 
											 size_t const iAnimatedChan, 
											 size_t const iChanAnimIndex,
											 unsigned const keyCompression)
{
	// channel anims might have been removed by the constant vs bind pose check 
	// so early out here if it does not exist
	if (!sourceData.Exists(chanType, iChanAnimIndex)) {
		return;
	}
	AnimMetaDataTrackBitFormat const& metaChanFormat = metaChanCompMethod.m_format;
	ChannelCompressionDesc& chanCompDesc = compression.m_format[chanType][iAnimatedChan];
	// FIXME - store both const & non-const bit format info for each channel - ignoring whether or not it's constant
	if (sourceData.IsConstant(chanType, iChanAnimIndex)) {
		chanCompDesc.m_compressionType = metaChanFormat.m_constCompressionType;
		chanCompDesc.m_bitFormat = GetConstCompressionBitFormat(metaChanFormat.m_constCompressionType);
		chanCompDesc.m_flags = kFormatFlagConstant | kFormatFlagBitFormatDefined;
		if (keyCompression == kClipKeysUnshared) {
			chanCompDesc.m_flags |= kFormatFlagKeyFramesDefined;
		}
	} else {
		if (IsAuto(metaChanFormat.m_compressionType) ||	
			((metaChanFormat.m_flags & kAnimMetaDataFlagGenerateBitFormat) && IsVariableBitPackedFormat(metaChanFormat.m_compressionType))) 
		{
			chanCompDesc.m_flags = 0;
			int sharedGroup = -1;
			if (metaChanFormat.m_flags & kAnimMetaDataFlagGenerateBitFormatShared) {
				sharedGroup = (int)(metaChanFormat.m_flags & kAnimMetaDataMaskFormatLabelIndex);
			}
			// map from channel type to compression channel type - TODO: merge these enums
			static AnimChannelType const animChanTypeMap[] = { kActVec3, kActQuat, kActVec3, kActFloat };
			AnimChannelType const animChanType = animChanTypeMap[(size_t)chanType];
			GenerateBitFormatCall& call = GetGenerateBitFormatCall(generateBitFormatCalls, animChanType, metaChanFormat.m_compressionType, sharedGroup);
			if (chanType==kChannelTypeTranslation) {
				call.m_channelSet2.emplace(iAnimatedChan);
			} else {
				call.m_channelSet.emplace(iAnimatedChan);
			}
		} else {
			chanCompDesc.m_compressionType = metaChanFormat.m_compressionType;
			chanCompDesc.m_bitFormat = metaChanFormat.m_bitFormat;
			chanCompDesc.m_flags = kFormatFlagBitFormatDefined;
		}
		if ((keyCompression == kClipKeysUnshared) && (metaChanCompMethod.m_keyFrames.m_flags & kAnimMetaDataFlagGenerateSkipList)) {
			genSkipFrames.add( chanType, iAnimatedChan );
		}
	}
}

void CollectBitFormatAndSkipFrameWorkJoints(GenerateBitFormatCalls& bitFormatCalls, 
											GenerateSkipFrameIndices& genSkipFrames,
											ClipCompressionDesc& compression, 
											AnimMetaData const& metaData, 
											AnimationBinding const& binding, 
											AnimationClipSourceData const& sourceData, 
											std::vector<int> const& jointToMetaIndex)
{
	unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToJointId.size();
	unsigned const keyCompression = (compression.m_flags & kClipKeyCompressionMask);

	// initialise set of animated joints that will use the default bit packing format
	std::set<size_t> defaultAnimatedJoints;
	for (size_t iJoint = 0; iJoint < numAnimatedJoints; iJoint++) {
		defaultAnimatedJoints.emplace(iJoint);
	}

	// joint channels that have formats specified in the metadata
	if (!metaData.m_jointNames.empty())	{	
		for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; ++iJointAnimIndex) {
			unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
			if (iAnimatedJoint > numAnimatedJoints) {
				continue;
			}
			int iMetaData = jointToMetaIndex[iAnimatedJoint];
			if (iMetaData < 0) {
				continue;
			}
			defaultAnimatedJoints.erase(iAnimatedJoint);
			AnimMetaDataJointCompressionMethod const& method = metaData.m_jointCompression[iMetaData];
			CollectBitFormatAndSkipFrameWorkChannel(bitFormatCalls, genSkipFrames, compression, kChannelTypeScale, method.m_scale, sourceData, iAnimatedJoint, iJointAnimIndex, keyCompression );
			CollectBitFormatAndSkipFrameWorkChannel(bitFormatCalls, genSkipFrames, compression, kChannelTypeRotation, method.m_rotation, sourceData, iAnimatedJoint, iJointAnimIndex, keyCompression );
			CollectBitFormatAndSkipFrameWorkChannel(bitFormatCalls, genSkipFrames, compression, kChannelTypeTranslation, method.m_translation, sourceData, iAnimatedJoint, iJointAnimIndex, keyCompression );
		}
	}

	// use defaults in metadata for the remaining joints
	for (auto it = defaultAnimatedJoints.begin(); it != defaultAnimatedJoints.end(); it++) {
		size_t iAnimatedJoint = *it;
		if (iAnimatedJoint >= numAnimatedJoints) {
			break;
		}
		unsigned iJointAnimIndex = (unsigned)binding.m_animatedJointIndexToJointAnimId[iAnimatedJoint];
		if (iJointAnimIndex >= numJointAnims) {
			continue;
		}
		AnimMetaDataJointCompressionMethod const& method = metaData.m_defaultCompression;
		CollectBitFormatAndSkipFrameWorkChannel(bitFormatCalls, genSkipFrames, compression, kChannelTypeScale, method.m_scale, sourceData, iAnimatedJoint, iJointAnimIndex, keyCompression );
		CollectBitFormatAndSkipFrameWorkChannel(bitFormatCalls, genSkipFrames, compression, kChannelTypeRotation, method.m_rotation, sourceData, iAnimatedJoint, iJointAnimIndex, keyCompression );
		CollectBitFormatAndSkipFrameWorkChannel(bitFormatCalls, genSkipFrames, compression, kChannelTypeTranslation, method.m_translation, sourceData, iAnimatedJoint, iJointAnimIndex, keyCompression );
	}
}

void CollectBitFormatAndSkipFrameWorkFloats(GenerateBitFormatCalls& bitFormatCalls, 
											GenerateSkipFrameIndices& genSkipFrames, 
											ClipCompressionDesc& compression, 
											AnimMetaData const& metaData, 
											AnimationBinding const& binding, 
											AnimationClipSourceData const& sourceData, 
											std::vector<int> const& floatToMetaIndex)
{
	unsigned const numFloatChannels = (unsigned)binding.m_floatIdToFloatAnimId.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();
	unsigned const keyCompression = (compression.m_flags & kClipKeyCompressionMask);

	// initialise set of float channels that will use the default bit packing format
	std::set<size_t> defaultFloatChannels;
	for (size_t iFloat = 0; iFloat < numFloatChannels; iFloat++) {
		defaultFloatChannels.emplace(iFloat);
	}

	// float channels that have formats specified in the metadata
	if (!metaData.m_floatNames.empty()) {
		for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < numFloatAnims; ++iFloatAnimIndex) {
			unsigned iFloatChannel = (unsigned)binding.m_floatAnimIdToFloatId[iFloatAnimIndex];
			if (iFloatChannel >= numFloatChannels) {
				continue;
			}
			int iMetaData = floatToMetaIndex[iFloatChannel];
			if (iMetaData < 0) {
				continue;
			}
			defaultFloatChannels.erase(iFloatChannel);
			AnimMetaDataTrackCompressionMethod const& method = metaData.m_floatCompression[iMetaData];
			CollectBitFormatAndSkipFrameWorkChannel(bitFormatCalls, genSkipFrames, compression, kChannelTypeScalar, method, sourceData, iFloatChannel, iFloatAnimIndex, keyCompression );
		}
	}

	// use default format in metadata for the remaining floats
	for (auto it = defaultFloatChannels.begin(); it != defaultFloatChannels.end(); it++) {
		size_t iFloatChannel = *it;
		if (iFloatChannel >= numFloatChannels) {
			break;
		}
		unsigned iFloatAnimIndex = (unsigned)binding.m_floatIdToFloatAnimId[iFloatChannel];
		if (iFloatAnimIndex >= numFloatAnims) {
			continue;
		}
		AnimMetaDataTrackCompressionMethod const& method = metaData.m_defaultCompressionFloat;
		CollectBitFormatAndSkipFrameWorkChannel(bitFormatCalls, genSkipFrames, compression, kChannelTypeScalar, method, sourceData, iFloatChannel, iFloatAnimIndex, keyCompression );
	}
}

bool BindAnimMetaDataToClip(ClipCompressionDesc& compression, 
							AnimMetaData const& metaData, 
							AnimationHierarchyDesc const& hierarchyDesc, 
							AnimationBinding const& binding, 
							AnimationClipSourceData& sourceData, 
							AnimationClipCompressionStats*	pStats)
{
	ITASSERT( binding.m_hierarchyId == hierarchyDesc.m_hierarchyId);
	ITASSERT( sourceData.GetNumAnims(kChannelTypeScale) == binding.m_jointAnimIdToAnimatedJointIndex.size() &&
			  sourceData.GetNumAnims(kChannelTypeRotation) == binding.m_jointAnimIdToAnimatedJointIndex.size() &&
			  sourceData.GetNumAnims(kChannelTypeTranslation) == binding.m_jointAnimIdToAnimatedJointIndex.size() &&
			  sourceData.GetNumAnims(kChannelTypeScalar) == binding.m_floatAnimIdToFloatId.size() );

	size_t const numAnimatedJoints = binding.m_animatedJointIndexToJointAnimId.size();
	size_t const numJointAnims = binding.m_jointAnimIdToJointId.size();
	size_t const numFloatChannels = binding.m_floatIdToFloatAnimId.size();
	size_t const numFloatAnims = binding.m_floatAnimIdToFloatId.size();

	// build local space error tolerances from metadata
	ClipLocalSpaceErrors tolerances;
	std::vector<int> floatToMetaIndex(numFloatChannels, -1);
	std::vector<int> jointToMetaIndex(numAnimatedJoints, -1);
	BuildLocalSpaceErrors(tolerances, jointToMetaIndex, floatToMetaIndex, metaData, hierarchyDesc, binding, sourceData);

	// determine which channels are constant using tolerances
	sourceData.DetermineConstantChannels(tolerances);

	// remove constant channels that are equal to the default pose
	if (metaData.m_flags & kAnimMetaDataFlagRemoveConstChannels) {
		sourceData.RemoveConstantDefaultPoseChannels(tolerances, hierarchyDesc, binding);
	}

	// convert to an AnimClipCompressionDesc
	AnimClipCompression_Init( compression, sourceData.m_flags, metaData.m_flags );
	unsigned const numOutputFrames = (sourceData.m_flags & kClipLooping) ? sourceData.m_numFrames+1 : sourceData.m_numFrames;
	if (numOutputFrames <= 2) {
		compression.m_flags = (compression.m_flags &~ kClipKeyCompressionMask) | kClipKeysUniform;
	}

	static const ChannelCompressionDesc kCompressionInvalid = { kAcctInvalid, 0, kVbpfNone };

	compression.m_format[kChannelTypeScale].resize(numAnimatedJoints, kCompressionInvalid);
	compression.m_format[kChannelTypeRotation].resize(numAnimatedJoints, kCompressionInvalid);
	compression.m_format[kChannelTypeTranslation].resize(numAnimatedJoints, kCompressionInvalid);
	compression.m_format[kChannelTypeScalar].resize(numFloatChannels, kCompressionInvalid);

	unsigned const keyCompression = (compression.m_flags & kClipKeyCompressionMask);
	if (keyCompression == kClipKeysUnshared) {
		compression.m_unsharedSkipFrames[kChannelTypeScale].resize(numAnimatedJoints);
		compression.m_unsharedSkipFrames[kChannelTypeRotation].resize(numAnimatedJoints);
		compression.m_unsharedSkipFrames[kChannelTypeTranslation].resize(numAnimatedJoints);
		compression.m_unsharedSkipFrames[kChannelTypeScalar].resize(numFloatChannels);
	}

	// collect defined bit formats and keyframe skip lists from metadata
	// and determine which channels need bit formats and skip frames generated
	GenerateSkipFrameIndices genSkipFrames;
	GenerateBitFormatCalls generateBitFormatCalls;
	CollectBitFormatAndSkipFrameWorkJoints(	generateBitFormatCalls, genSkipFrames, compression, metaData, binding, sourceData, jointToMetaIndex );
	CollectBitFormatAndSkipFrameWorkFloats(	generateBitFormatCalls, genSkipFrames, compression, metaData, binding, sourceData, floatToMetaIndex );

	// generate the bit format for those channels we determined above
	for (auto it = generateBitFormatCalls.begin(); it != generateBitFormatCalls.end(); ++it) {
		switch (it->m_channelType) {
		case kActVec3:
			AnimClipCompression_GenerateBitFormats_Vector(
				compression, hierarchyDesc, binding, sourceData, tolerances,
				it->m_compressionType, it->m_channelSet, it->m_channelSet2, it->m_sharedGroup != -1);
			break;
		case kActQuat:
			AnimClipCompression_GenerateBitFormats_Rotation(
				compression, hierarchyDesc, binding, sourceData, tolerances,
				it->m_compressionType, it->m_channelSet, it->m_sharedGroup != -1);
			break;
		case kActFloat:
			AnimClipCompression_GenerateBitFormats_Float(
				compression, hierarchyDesc, binding, sourceData, tolerances,
				it->m_compressionType, it->m_channelSet, it->m_sharedGroup != -1);
			break;
		default:
			ITASSERT(0);
			break;
		}
	}

	// Execute GenerateSkipFrames calls
	if (keyCompression == kClipKeysUnshared) {
		compression.m_maxKeysPerBlock = metaData.m_maxKeysPerBlock;
		compression.m_maxBlockSize = metaData.m_maxBlockSize;

		AnimClipCompression_GenerateSkipFrames_Unshared(
			compression, 
			hierarchyDesc, 
			binding, 
			sourceData, 
			tolerances, 
			genSkipFrames);
		
		bool bFramesSkipped = false;
		// finalize compression.m_skipFrames* by adding metaData m_skipFrames to and removing m_keepFrames from each generated list:
		for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; ++iJointAnimIndex) {
			unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
			if (iAnimatedJoint >= numAnimatedJoints) {
				continue;
			}
			int iMeta = jointToMetaIndex[iAnimatedJoint];
			AnimMetaDataJointCompressionMethod const& jointCompression = (iMeta >= 0) ? metaData.m_jointCompression[iMeta] : metaData.m_defaultCompression;
			if (sourceData.Exists(kChannelTypeScale, iJointAnimIndex)) {
				if (!sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
					AnimMetaDataTrackKeyFrames const& scale_keyframes = jointCompression.m_scale.m_keyFrames;
					if (genSkipFrames.has(kChannelTypeScale, iAnimatedJoint)) {
						// merge supplied skip frames from metadata with generated skip frames
						compression.m_unsharedSkipFrames[kChannelTypeScale][iAnimatedJoint].insert(scale_keyframes.m_skipFrames.begin(), scale_keyframes.m_skipFrames.end());
					} else {
						compression.m_unsharedSkipFrames[kChannelTypeScale][iAnimatedJoint] = scale_keyframes.m_skipFrames;
					}
					compression.m_unsharedSkipFrames[kChannelTypeScale][iAnimatedJoint] -= scale_keyframes.m_keepFrames;
					compression.m_format[kChannelTypeScale][iAnimatedJoint].m_flags |= kFormatFlagKeyFramesDefined;
					bFramesSkipped = bFramesSkipped || !compression.m_unsharedSkipFrames[kChannelTypeScale][iAnimatedJoint].empty();
				}
			}
			if (sourceData.Exists(kChannelTypeRotation, iJointAnimIndex)) {
				if (!sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
					AnimMetaDataTrackKeyFrames const& rotation_keyframes = jointCompression.m_rotation.m_keyFrames;
					if (genSkipFrames.has(kChannelTypeRotation, iAnimatedJoint)) {
						// merge supplied skip frames from metadata with generated skip frames
						compression.m_unsharedSkipFrames[kChannelTypeRotation][iAnimatedJoint].insert(rotation_keyframes.m_skipFrames.begin(), rotation_keyframes.m_skipFrames.end());
					} else {
						compression.m_unsharedSkipFrames[kChannelTypeRotation][iAnimatedJoint] = rotation_keyframes.m_skipFrames;
					}
					compression.m_unsharedSkipFrames[kChannelTypeRotation][iAnimatedJoint] -= rotation_keyframes.m_keepFrames;
					compression.m_format[kChannelTypeRotation][iAnimatedJoint].m_flags |= kFormatFlagKeyFramesDefined;
					bFramesSkipped = bFramesSkipped || !compression.m_unsharedSkipFrames[kChannelTypeRotation][iAnimatedJoint].empty();
				}
			}
			if (sourceData.Exists(kChannelTypeTranslation, iJointAnimIndex)) {
				if (!sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
					AnimMetaDataTrackKeyFrames const& translation_keyframes = jointCompression.m_translation.m_keyFrames;
					if (genSkipFrames.has(kChannelTypeTranslation, iAnimatedJoint)) {
						// merge supplied skip frames from metadata with generated skip frames
						compression.m_unsharedSkipFrames[kChannelTypeTranslation][iAnimatedJoint].insert(translation_keyframes.m_skipFrames.begin(), translation_keyframes.m_skipFrames.end());
					} else {
						compression.m_unsharedSkipFrames[kChannelTypeTranslation][iAnimatedJoint] = translation_keyframes.m_skipFrames;
					}
					compression.m_unsharedSkipFrames[kChannelTypeTranslation][iAnimatedJoint] -= translation_keyframes.m_keepFrames;
					compression.m_format[kChannelTypeTranslation][iAnimatedJoint].m_flags |= kFormatFlagKeyFramesDefined;
					bFramesSkipped = bFramesSkipped || !compression.m_unsharedSkipFrames[kChannelTypeTranslation][iAnimatedJoint].empty();
				}
			}
		}
		for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < numFloatAnims; ++iFloatAnimIndex) {
			unsigned iFloatChannel = (unsigned)binding.m_floatAnimIdToFloatId[iFloatAnimIndex];
			if (iFloatChannel >= numFloatChannels) {
				continue;
			}
			if (!sourceData.Exists(kChannelTypeScalar, iFloatAnimIndex)) {
				continue;
			}
			if (sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex)) {
				continue;
			}
			int iMeta = floatToMetaIndex[iFloatChannel];
			AnimMetaDataTrackCompressionMethod const& floatCompression = (iMeta >= 0) ? metaData.m_floatCompression[iMeta] : metaData.m_defaultCompressionFloat;
			AnimMetaDataTrackKeyFrames const& float_keyframes = floatCompression.m_keyFrames;
			if (genSkipFrames.has(kChannelTypeScalar, iFloatChannel)) {
				// merge supplied skip frames from metadata with generated skip frames
				compression.m_unsharedSkipFrames[kChannelTypeScalar][iFloatChannel].insert(float_keyframes.m_skipFrames.begin(), float_keyframes.m_skipFrames.end());
			} else {
				compression.m_unsharedSkipFrames[kChannelTypeScalar][iFloatChannel] = float_keyframes.m_skipFrames;
			}
			compression.m_unsharedSkipFrames[kChannelTypeScalar][iFloatChannel] -= float_keyframes.m_keepFrames;
			compression.m_format[kChannelTypeScalar][iFloatChannel].m_flags |= kFormatFlagKeyFramesDefined;
			bFramesSkipped = bFramesSkipped || !compression.m_unsharedSkipFrames[kChannelTypeScalar][iFloatChannel].empty();
		}

		if (!bFramesSkipped) {
			IWARN("BindAnimMetaDataToClip: unshared skip frame generation + skipframes - keepframes results in no frames skipped for any animation; using uniform keys...\n");
			compression.m_flags = (compression.m_flags &~ kClipKeyCompressionMask) | kClipKeysUniform;
			for (unsigned iAnimatedJoint = 0; iAnimatedJoint < numAnimatedJoints; ++iAnimatedJoint) {
				compression.m_unsharedSkipFrames[kChannelTypeScale][iAnimatedJoint].clear();
				compression.m_unsharedSkipFrames[kChannelTypeRotation][iAnimatedJoint].clear();
				compression.m_unsharedSkipFrames[kChannelTypeTranslation][iAnimatedJoint].clear();
			}
			for (unsigned iFloatChannel = 0; iFloatChannel < numFloatChannels; ++iFloatChannel) {
				compression.m_unsharedSkipFrames[kChannelTypeScalar][iFloatChannel].clear();
			}
		}
	} else if (keyCompression == kClipKeysShared) {
		if (metaData.m_sharedKeyFrames.m_flags & kAnimMetaDataFlagGenerateSkipList) {
			// attempt to create skip frames using the local space errors
			AnimClipCompression_GenerateSkipFrames_Shared(compression, hierarchyDesc, binding, sourceData, tolerances);
			compression.m_sharedSkipFrames.insert(metaData.m_sharedKeyFrames.m_skipFrames.begin(), metaData.m_sharedKeyFrames.m_skipFrames.end());
			compression.m_sharedSkipFrames -= metaData.m_sharedKeyFrames.m_keepFrames;
			if (compression.m_sharedSkipFrames.empty()) {
				IWARN("BindAnimMetaDataToClip: shared skip frame generation + skipframes - keepframes results in no frames skipped; using uniform keys...\n");
				compression.m_flags = (compression.m_flags &~ kClipKeyCompressionMask) | kClipKeysUniform;
			}
		} else {
			compression.m_sharedSkipFrames = metaData.m_sharedKeyFrames.m_skipFrames;
			compression.m_sharedSkipFrames -= metaData.m_sharedKeyFrames.m_keepFrames;
			if (compression.m_sharedSkipFrames.empty()) {
				IWARN("BindAnimMetaDataToClip: skipframes - keepframes results in no frames skipped; using uniform keys...\n");
				compression.m_flags = (compression.m_flags &~ kClipKeyCompressionMask) | kClipKeysUniform;
			}
		}
	}

	// generate bit formats for all constant channels
	if (!AnimClipCompression_FinalizeConstFormats(compression, hierarchyDesc, binding, sourceData, tolerances)) {
		return false;
	}

	// finish filling out compression (maxKeyPerBlock & maxBlockSize) & optionally generate stats into pStats, if not NULL.
	if (!AnimClipCompression_Finalize(compression, hierarchyDesc, binding, sourceData, pStats)) {
		return false;
	}

	return true;
}

void BuildAnimationClipSharedKeyFrameArray(FrameArray& keyFrames, 
										   ClipCompressionDesc const& compression, 
										   AnimationClipSourceData const& sourceData)
{
	keyFrames.resize(0);
	keyFrames.push_back(0);
	for (unsigned iFrame = 1; iFrame < sourceData.m_numFrames; iFrame++) {
		if (!compression.m_sharedSkipFrames.count(iFrame)) {
			ITASSERT( iFrame <= 0xFFFF );
			keyFrames.push_back((U16)iFrame);
		}
	}
	if (sourceData.m_flags & kClipLooping) {
		ITASSERT( sourceData.m_numFrames <= 0xFFFF );
		keyFrames.push_back((U16)sourceData.m_numFrames);
	}
}

void BuildAnimationClipUnsharedKeyFrameBlocks(AnimationClipUnsharedKeyFrameBlockDesc& keyFrames, 
											  ClipCompressionDesc const& compression, 
											  AnimationBinding const& binding, 
											  AnimationClipSourceData const& sourceData, 
											  AnimationHierarchyDesc const& hierarchyDesc)
{
	ITASSERT(	sourceData.GetNumAnims(kChannelTypeScale) == binding.m_jointAnimIdToAnimatedJointIndex.size() &&
				sourceData.GetNumAnims(kChannelTypeRotation) == binding.m_jointAnimIdToAnimatedJointIndex.size() &&
				sourceData.GetNumAnims(kChannelTypeTranslation) == binding.m_jointAnimIdToAnimatedJointIndex.size() &&
				sourceData.GetNumAnims(kChannelTypeScalar) == binding.m_floatAnimIdToFloatId.size() );
	ITASSERT(	binding.m_hierarchyId == hierarchyDesc.m_hierarchyId );

	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();
	unsigned const numOutputFrames = (sourceData.m_flags & kClipLooping) ? sourceData.m_numFrames+1 : sourceData.m_numFrames;

	// For each group, split data into blocks based on compression.m_maxBlockSize, compression.m_maxKeysPerBlock,
	// with keys duplicated across block edges as needed.
	unsigned const numGroups = (unsigned)hierarchyDesc.m_channelGroups.size();
	keyFrames.m_blocksInGroup.resize(numGroups);
	for (unsigned iGroup = 0; iGroup < numGroups; ++iGroup) {
		AnimationClipUnsharedKeyFrameBlockDesc::BlockArray& blockArray = keyFrames.m_blocksInGroup[iGroup];
		unsigned bitsizeofFullKey = 0;
		// Get total size of a key across all non-constant joint and float channel params
		unsigned const firstAnimatedJoint = hierarchyDesc.m_channelGroups[iGroup].m_firstAnimatedJoint;
		unsigned const numAnimatedJointsInGroup = hierarchyDesc.m_channelGroups[iGroup].m_numAnimatedJoints;
		std::vector<unsigned> bitsizeofKeyScale(numAnimatedJointsInGroup, 0), bitsizeofKeyQuat(numAnimatedJointsInGroup, 0), bitsizeofKeyTrans(numAnimatedJointsInGroup, 0);
		if (numAnimatedJointsInGroup > 0) {
			unsigned const maxAnimatedJointInGroup = firstAnimatedJoint + numAnimatedJointsInGroup;
			for (unsigned iAnimatedJoint = firstAnimatedJoint; iAnimatedJoint < maxAnimatedJointInGroup; ++iAnimatedJoint) {
				unsigned iJointAnimIndex = (unsigned)binding.m_animatedJointIndexToJointAnimId[iAnimatedJoint];
				if (iJointAnimIndex >= numJointAnims) {
					continue;
				}
				unsigned iLocalIndex = (unsigned)(iAnimatedJoint - firstAnimatedJoint);
				bitsizeofKeyScale[iLocalIndex] = 0;
				bitsizeofKeyQuat[iLocalIndex] = 0;
				bitsizeofKeyTrans[iLocalIndex] = 0;
				if (!sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
					Vec3BitPackingFormat format = compression.m_format[kChannelTypeScale][iAnimatedJoint].m_bitFormat;
					ITASSERTF(format.m_numBitsTotal > 0 && format.m_numBitsTotal >= format.m_numBitsX + format.m_numBitsY + format.m_numBitsZ, ("ajoint %u anim %d has invalid scale format numBitsTotal:%d (x:%d, y:%d, z:%d)", iAnimatedJoint, iJointAnimIndex, format.m_numBitsTotal, format.m_numBitsX, format.m_numBitsY, format.m_numBitsZ));
					bitsizeofKeyScale[iLocalIndex] = format.m_numBitsTotal;
				}
				if (!sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
					Vec3BitPackingFormat format = compression.m_format[kChannelTypeRotation][iAnimatedJoint].m_bitFormat;
					ITASSERTF(format.m_numBitsTotal > 0 && format.m_numBitsTotal >= format.m_numBitsX + format.m_numBitsY + format.m_numBitsZ, ("ajoint %u anim %d has invalid rotation format numBitsTotal:%d (x:%d, y:%d, z:%d)", iAnimatedJoint, iJointAnimIndex, format.m_numBitsTotal, format.m_numBitsX, format.m_numBitsY, format.m_numBitsZ));
					bitsizeofKeyQuat[iLocalIndex] = format.m_numBitsTotal;
				}
				if (!sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
					Vec3BitPackingFormat format = compression.m_format[kChannelTypeTranslation][iAnimatedJoint].m_bitFormat;
					ITASSERTF(format.m_numBitsTotal > 0 && format.m_numBitsTotal >= format.m_numBitsX + format.m_numBitsY + format.m_numBitsZ, ("ajoint %u anim %d has invalid translation format numBitsTotal:%d (x:%d, y:%d, z:%d)", iAnimatedJoint, iJointAnimIndex, format.m_numBitsTotal, format.m_numBitsX, format.m_numBitsY, format.m_numBitsZ));
					bitsizeofKeyTrans[iLocalIndex] = format.m_numBitsTotal;
				}
				bitsizeofFullKey += bitsizeofKeyScale[iLocalIndex] + bitsizeofKeyQuat[iLocalIndex] + bitsizeofKeyTrans[iLocalIndex];
			}
		}
		unsigned const firstFloatChannel = hierarchyDesc.m_channelGroups[iGroup].m_firstFloatChannel;
		unsigned const numFloatChannelsInGroup = hierarchyDesc.m_channelGroups[iGroup].m_numFloatChannels;
		std::vector<unsigned> bitsizeofKeyFloat(numFloatChannelsInGroup, 0);
		if (numFloatChannelsInGroup > 0) {
			unsigned const maxFloatChannelInGroup = firstFloatChannel + numFloatChannelsInGroup;
			for (unsigned iFloatChannel = firstFloatChannel; iFloatChannel < maxFloatChannelInGroup; ++iFloatChannel) {
				unsigned iFloatAnimIndex = (unsigned)binding.m_floatIdToFloatAnimId[iFloatChannel];
				if (iFloatAnimIndex >= numFloatAnims) {
					continue;
				}
				unsigned iLocalIndex = (unsigned)(iFloatChannel - firstFloatChannel);
				if (sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex)) {
					bitsizeofKeyFloat[iLocalIndex] = 0;
				} else {
					Vec3BitPackingFormat format = compression.m_format[kChannelTypeScalar][iFloatChannel].m_bitFormat;
					ITASSERTF(format.m_numBitsTotal > 0 && format.m_numBitsTotal == format.m_numBitsX, ("float channel %d anim %d has invalid format numBitsTotal:%d (x:%d, y:%d, z:%d)", iFloatChannel, iFloatAnimIndex, format.m_numBitsTotal, format.m_numBitsX, format.m_numBitsY, format.m_numBitsZ));
					bitsizeofKeyFloat[iLocalIndex] = format.m_numBitsTotal;
					bitsizeofFullKey += bitsizeofKeyFloat[iLocalIndex];
				}
			}
		}

		unsigned sizeofFullKey = (bitsizeofFullKey + 7)/8;
		unsigned maxBlockBitsize = compression.m_maxBlockSize * 8;
		if (maxBlockBitsize < 4*sizeofFullKey*8) {
			maxBlockBitsize = 4*sizeofFullKey*8;
		}
		blockArray.resize(0);
		AnimationClipUnsharedKeyBlock* block;
		unsigned bitsizeofBlock;
		unsigned frame = 0;
		do {
			// start a new block : add an initial full key with frame offset 0 to all channels
			blockArray.resize(blockArray.size()+1);
			block = &blockArray.back();
			block->m_firstFrame = frame;
			block->m_numFrames = 0;
			block->m_frameOffsets[kChannelTypeScale].resize(numAnimatedJointsInGroup);
			block->m_frameOffsets[kChannelTypeRotation].resize(numAnimatedJointsInGroup);
			block->m_frameOffsets[kChannelTypeTranslation].resize(numAnimatedJointsInGroup);
			block->m_frameOffsets[kChannelTypeScalar].resize(numFloatChannelsInGroup);
			for (unsigned iLocalIndex = 0; iLocalIndex < numAnimatedJointsInGroup; iLocalIndex++) {
				if (bitsizeofKeyScale[iLocalIndex]) {
					block->m_frameOffsets[kChannelTypeScale][iLocalIndex].push_back(0);
				}
				if (bitsizeofKeyQuat[iLocalIndex]) {
					block->m_frameOffsets[kChannelTypeRotation][iLocalIndex].push_back(0);
				}
				if (bitsizeofKeyTrans[iLocalIndex]) {
					block->m_frameOffsets[kChannelTypeTranslation][iLocalIndex].push_back(0);
				}
			}
			for (unsigned iLocalIndex = 0; iLocalIndex < numFloatChannelsInGroup; iLocalIndex++) {
				if (bitsizeofKeyFloat[iLocalIndex]) {
					block->m_frameOffsets[kChannelTypeScalar][iLocalIndex].resize(1, 0);
				}
			}
			bitsizeofBlock = 2*bitsizeofFullKey;

			// add frames to this block until there is some reason we can't add another:
			// break out to start a new block if we won't be able to store the next local frame index in a key mask:
			unsigned endFrame = block->m_firstFrame + compression.m_maxKeysPerBlock-1;
			// break out to end our last block if we hit the last frame:
			if (endFrame > numOutputFrames-1)
				endFrame = numOutputFrames-1;
			for (++frame; frame < endFrame; ++frame) {
				unsigned bitsizeofPartialKey = 0;
				for (unsigned iLocalIndex = 0; iLocalIndex < numAnimatedJointsInGroup; iLocalIndex++) {
					unsigned iAnimatedJoint = firstAnimatedJoint + iLocalIndex;
					if (bitsizeofKeyScale[iLocalIndex] && !compression.m_unsharedSkipFrames[kChannelTypeScale][iAnimatedJoint].count(frame))
						bitsizeofPartialKey += bitsizeofKeyScale[iLocalIndex];
					if (bitsizeofKeyQuat[iLocalIndex] && !compression.m_unsharedSkipFrames[kChannelTypeRotation][iAnimatedJoint].count(frame))
						bitsizeofPartialKey += bitsizeofKeyQuat[iLocalIndex];
					if (bitsizeofKeyTrans[iLocalIndex] && !compression.m_unsharedSkipFrames[kChannelTypeTranslation][iAnimatedJoint].count(frame))
						bitsizeofPartialKey += bitsizeofKeyTrans[iLocalIndex];
				}
				for (unsigned iLocalIndex = 0; iLocalIndex < numFloatChannelsInGroup; iLocalIndex++) {
					unsigned iFloatChannel = firstFloatChannel + iLocalIndex;
					if (bitsizeofKeyFloat[iLocalIndex] && !compression.m_unsharedSkipFrames[kChannelTypeScalar][iFloatChannel].count(frame))
						bitsizeofPartialKey += bitsizeofKeyFloat[iLocalIndex];
				}
				// break out to start a new block if we run out of space:
				if (bitsizeofBlock + bitsizeofPartialKey > maxBlockBitsize)
					break;
				// add a partial key to the current block
				U8 frameOffset = (U8)(frame - block->m_firstFrame);
				for (unsigned iLocalIndex = 0; iLocalIndex < numAnimatedJointsInGroup; iLocalIndex++) {
					unsigned iAnimatedJoint = firstAnimatedJoint + iLocalIndex;
					if (bitsizeofKeyScale[iLocalIndex] && !compression.m_unsharedSkipFrames[kChannelTypeScale][iAnimatedJoint].count(frame)) {
						block->m_frameOffsets[kChannelTypeScale][iLocalIndex].push_back(frameOffset);
					}
					if (bitsizeofKeyQuat[iLocalIndex] && !compression.m_unsharedSkipFrames[kChannelTypeRotation][iAnimatedJoint].count(frame)) {
						block->m_frameOffsets[kChannelTypeRotation][iLocalIndex].push_back(frameOffset);
					}
					if (bitsizeofKeyTrans[iLocalIndex] && !compression.m_unsharedSkipFrames[kChannelTypeTranslation][iAnimatedJoint].count(frame)) {
						block->m_frameOffsets[kChannelTypeTranslation][iLocalIndex].push_back(frameOffset);
					}
				}
				for (unsigned iLocalIndex = 0; iLocalIndex < numFloatChannelsInGroup; iLocalIndex++) {
					unsigned iFloatChannel = firstFloatChannel + iLocalIndex;
					if (bitsizeofKeyFloat[iLocalIndex] && !compression.m_unsharedSkipFrames[kChannelTypeScalar][iFloatChannel].count(frame)) {
						block->m_frameOffsets[kChannelTypeScalar][iLocalIndex].push_back(frameOffset);
					}
				}
				bitsizeofBlock += bitsizeofPartialKey;
			}

			// add a final full key to the current block
			U8 frameOffset = (U8)(frame - block->m_firstFrame);
			for (unsigned iLocalIndex = 0; iLocalIndex < numAnimatedJointsInGroup; iLocalIndex++) {
				if (bitsizeofKeyScale[iLocalIndex]) {
					block->m_frameOffsets[kChannelTypeScale][iLocalIndex].push_back(frameOffset);
				}
				if (bitsizeofKeyQuat[iLocalIndex]) {
					block->m_frameOffsets[kChannelTypeRotation][iLocalIndex].push_back(frameOffset);
				}
				if (bitsizeofKeyTrans[iLocalIndex]) {
					block->m_frameOffsets[kChannelTypeTranslation][iLocalIndex].push_back(frameOffset);
				}
			}
			for (unsigned iLocalIndex = 0; iLocalIndex < numFloatChannelsInGroup; iLocalIndex++) {
				if (bitsizeofKeyFloat[iLocalIndex]) {
					block->m_frameOffsets[kChannelTypeScalar][iLocalIndex].push_back(frameOffset);
				}
			}
		
			block->m_numFrames = (unsigned)frameOffset+1;
		
		} while (frame < numOutputFrames-1);
	}
}

void BuildAnimationClipCompressedData(AnimationClipCompressedData& compressedData, 
									  AnimationBinding const& binding, 
									  AnimationClipSourceData const& sourceData, 
									  ClipCompressionDesc const& compression, 
									  KeyExtractor const& keyExtractor)
{
	ITASSERT(	sourceData.GetNumAnims(kChannelTypeScale) == binding.m_jointAnimIdToAnimatedJointIndex.size() &&
				sourceData.GetNumAnims(kChannelTypeRotation) == binding.m_jointAnimIdToAnimatedJointIndex.size() &&
				sourceData.GetNumAnims(kChannelTypeTranslation) == binding.m_jointAnimIdToAnimatedJointIndex.size() &&
				sourceData.GetNumAnims(kChannelTypeScalar) == binding.m_floatAnimIdToFloatId.size() );

	unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
	unsigned const numFloatChannels = (unsigned)binding.m_floatIdToFloatAnimId.size();
	compressedData.m_hierarchyId = binding.m_hierarchyId;
	compressedData.m_pRootAnim = NULL;
	if (numAnimatedJoints <= 0 && numFloatChannels <= 0) {
		compressedData.m_flags = 0;
		compressedData.m_numFrames = 0;
		ITASSERTF(numAnimatedJoints>0 || numFloatChannels>0, ("Warning: number of animated joints and float channels in bindpose is 0.\n"));
		return;
	}

	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();

	std::vector< ITGEOM::Vec3Array > constScaleData;
	std::vector< ITGEOM::QuatArray > constRotationData;
	std::vector< ITGEOM::Vec3Array > constTranslationData;
	std::vector< std::vector<float> > constFloatData;

	std::vector< AnimChannelCompressionType > constScaleCompressionType;
	std::vector< AnimChannelCompressionType > constRotationCompressionType;
	std::vector< AnimChannelCompressionType > constTranslationCompressionType;
	std::vector< AnimChannelCompressionType > constFloatCompressionType;
	
	// FIXME - is this necessary or even correct (it won't delete the channels)?
	compressedData.Clear();

	compressedData.m_flags = (sourceData.m_flags & ~kClipCompressionMask) | (compression.m_flags & kClipCompressionMask);
	compressedData.m_numFrames = (sourceData.m_flags & kClipLooping) ? sourceData.m_numFrames+1 : sourceData.m_numFrames;
	if (sourceData.m_pRootAnim) {
		compressedData.m_pRootAnim = sourceData.m_pRootAnim->ConstructCopy();
	}

	std::vector<ChannelCompressionDesc> const& formatScale = compression.m_format[kChannelTypeScale];
	std::vector<ChannelCompressionDesc> const& formatQuat = compression.m_format[kChannelTypeRotation];
	std::vector<ChannelCompressionDesc> const& formatTrans = compression.m_format[kChannelTypeTranslation];

	// first compress quat log & log pca channels, adding them to the set of crossCorrelatedRotations (so we can skip them later)
	std::set<size_t> crossCorrelatedRotations;
	for (unsigned iType = 0; iType < 2; ++iType) {
		AnimChannelCompressionType compressionType = (iType == 0) ? kAcctQuatLog : kAcctQuatLogPca;
		std::vector<unsigned> const& jointAnimOrder = (compressionType == kAcctQuatLog) ? compression.m_jointAnimOrderQuatLog : compression.m_jointAnimOrderQuatLogPca;

		ITGEOM::Vec3Array avPrevOutputLogValues;
		for (unsigned i = 0, count = (unsigned)jointAnimOrder.size(); i < count; ++i)
		{
			unsigned iJointAnimIndex = jointAnimOrder[i];
			ITASSERT(iJointAnimIndex < numJointAnims);
			ITASSERT(!sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex));
			crossCorrelatedRotations.emplace(iJointAnimIndex);

			int iAnimatedJoint = binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
			ITASSERT((unsigned)iAnimatedJoint < numAnimatedJoints);

			ChannelCompressionDesc const& format = formatQuat[iAnimatedJoint];
			ITASSERT(format.m_compressionType == compressionType);

			ITGEOM::QuatArray values;
			keyExtractor.GetRotationValues( iJointAnimIndex, values );

			IBitCompressedQuatArray *pCompressedData = BitCompressQuatArray( values, format.m_compressionType, format.m_bitFormat, &format.m_contextData, &avPrevOutputLogValues );

			AnimationClipCompressedData::AnimatedData animChannel = { pCompressedData, iAnimatedJoint };
			compressedData.m_anims[kChannelTypeRotation].push_back( animChannel );
		}
	}

	// simply compress each of our animated joint parameters independently
	for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; ++iJointAnimIndex) {
		int iAnimatedJoint = binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
		if (iAnimatedJoint < 0) {
			continue;
		}
		ITASSERT((unsigned)iAnimatedJoint < numAnimatedJoints);
		// scale 
		if (sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex)) {
			if (!sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
				ITGEOM::Vec3Array values;	// non-const scale channel
				keyExtractor.GetScaleValues(iJointAnimIndex, values);
				AnimationClipCompressedData::AnimatedData animChannel = {
					BitCompressVec3Array(values, formatScale[iAnimatedJoint].m_compressionType, formatScale[iAnimatedJoint].m_bitFormat, &formatScale[iAnimatedJoint].m_contextData),
					iAnimatedJoint
				};
				compressedData.m_anims[kChannelTypeScale].push_back(animChannel);
			} else {
				// const channel
				AnimChannelCompressionType compressionType = formatScale[iAnimatedJoint].m_compressionType;
				unsigned iType;
				// look up to see if we already have data for this type
				for (iType = 0; iType < constScaleCompressionType.size(); ++iType) {
					if (compressionType == constScaleCompressionType[iType]) {
						break;
					}
				}
				if (iType == constScaleCompressionType.size()) {
					// if not, add a new array for this type & add this channel to it
					constScaleCompressionType.push_back(compressionType);
					constScaleData.push_back(ITGEOM::Vec3Array(1, (*sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex))[0]));
					AnimationClipCompressedData::ConstantData constChannel = {
						NULL, std::vector<size_t>(1, iAnimatedJoint)
					};
					compressedData.m_const[kChannelTypeScale].push_back(constChannel);
				} else {
					// otherwise, push this channel to the existing array for this compression type
					constScaleData[iType].push_back((*sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex))[0]);
					compressedData.m_const[kChannelTypeScale][iType].m_ids.push_back(iAnimatedJoint);
				}
			}
		}
		// rotation
		if (crossCorrelatedRotations.count(iJointAnimIndex)) {
			// skip this joint anim if we already compressed it earlier (as kAcctQuatLog or kAcctQuatLogPca)
		} else if (sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex)) {
			if (!sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
				// non-const channel
				ITGEOM::QuatArray values;
				keyExtractor.GetRotationValues( iJointAnimIndex, values );
				AnimationClipCompressedData::AnimatedData animChannel = {
					BitCompressQuatArray( values, formatQuat[iAnimatedJoint].m_compressionType, formatQuat[iAnimatedJoint].m_bitFormat, &formatQuat[iAnimatedJoint].m_contextData ),
					iAnimatedJoint
				};
				compressedData.m_anims[kChannelTypeRotation].push_back( animChannel );
			} else {
				// const channel
				AnimChannelCompressionType compressionType = formatQuat[iAnimatedJoint].m_compressionType;
				unsigned iType;
				for (iType = 0; iType < constRotationCompressionType.size(); ++iType) {
					if (compressionType == constRotationCompressionType[iType]) {
						break;
					}
				}
				if (iType == constRotationCompressionType.size()) {
					constRotationCompressionType.push_back(compressionType);
					constRotationData.push_back( ITGEOM::QuatArray(1, (*sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex))[0]) );
					AnimationClipCompressedData::ConstantData constChannel = {
						NULL, std::vector<size_t>(1, iAnimatedJoint)
					};
					compressedData.m_const[kChannelTypeRotation].push_back(constChannel);
				} else {
					constRotationData[iType].push_back( (*sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex))[0] );
					compressedData.m_const[kChannelTypeRotation][iType].m_ids.push_back(iAnimatedJoint);
				}
			}
		}
		// translation
		if (sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex)) {
			if (!sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
				// non-const channel
				ITGEOM::Vec3Array values;
				keyExtractor.GetTranslationValues(iJointAnimIndex, values);
				AnimationClipCompressedData::AnimatedData animChannel = {
					BitCompressVec3Array(values, formatTrans[iAnimatedJoint].m_compressionType, formatTrans[iAnimatedJoint].m_bitFormat, &formatTrans[iAnimatedJoint].m_contextData),
					iAnimatedJoint
				};
				compressedData.m_anims[kChannelTypeTranslation].push_back(animChannel);
			} else {
				// const channel
				AnimChannelCompressionType compressionType = formatTrans[iAnimatedJoint].m_compressionType;
				unsigned iType;
				for (iType = 0; iType < constTranslationCompressionType.size(); ++iType) {
					if (compressionType == constTranslationCompressionType[iType]) {
						break;
					}
				}
				if (iType == constTranslationCompressionType.size()) {
					constTranslationCompressionType.push_back(compressionType);
					constTranslationData.push_back(ITGEOM::Vec3Array(1, (*sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex))[0]));
					AnimationClipCompressedData::ConstantData constChannel = {
						NULL, std::vector<size_t>(1, iAnimatedJoint)
					};
					compressedData.m_const[kChannelTypeTranslation].push_back(constChannel);
				} else {
					constTranslationData[iType].push_back((*sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex))[0]);
					compressedData.m_const[kChannelTypeTranslation][iType].m_ids.push_back(iAnimatedJoint);
				}
			}
		}
	}

	// compress each constant channel we found during above loop
	{	
		for (unsigned iType = 0; iType < constScaleCompressionType.size(); ++iType) {
			compressedData.m_const[kChannelTypeScale][iType].m_pCompressedArray = BitCompressVec3Array( constScaleData[iType], constScaleCompressionType[iType] );
		}
		for (unsigned iType = 0; iType < constRotationCompressionType.size(); ++iType) {
			compressedData.m_const[kChannelTypeRotation][iType].m_pCompressedArray = BitCompressQuatArray( constRotationData[iType], constRotationCompressionType[iType] );
		}
		for (unsigned iType = 0; iType < constTranslationCompressionType.size(); ++iType) {
			compressedData.m_const[kChannelTypeTranslation][iType].m_pCompressedArray = BitCompressVec3Array( constTranslationData[iType], constTranslationCompressionType[iType] );
		}
	}

	// Collect and compress float animation data in increasing order of float channel index
	std::vector<ChannelCompressionDesc> const& formatFloat = compression.m_format[kChannelTypeScalar];
	for (unsigned iFloat = 0; iFloat < numFloatChannels; ++iFloat) {
		unsigned iFloatAnimIndex = binding.m_floatIdToFloatAnimId[iFloat];
		if (iFloatAnimIndex >= numFloatAnims) {
			continue;
		}
		if (sourceData.GetSampledAnim(kChannelTypeScalar, iFloatAnimIndex)) {
			if (!sourceData.IsConstant(kChannelTypeScalar, iFloatAnimIndex)) {
				std::vector<float> values;
				keyExtractor.GetFloatValues(iFloatAnimIndex, values);
				AnimationClipCompressedData::AnimatedData animChannel = {
					BitCompressFloatArray(values, formatFloat[iFloat].m_compressionType),
					iFloat
				};
				compressedData.m_anims[kChannelTypeScalar].push_back(animChannel);
			} else {
				AnimChannelCompressionType compressionType = formatFloat[iFloat].m_compressionType;
				unsigned iType;
				for (iType = 0; iType < constFloatCompressionType.size(); ++iType) {
					if (compressionType == constFloatCompressionType[iType]) {
						break;
					}
				}
				if (iType == constFloatCompressionType.size()) {
					constFloatCompressionType.push_back(compressionType);
					constFloatData.push_back(std::vector<float>(1, (*sourceData.GetSampledAnim(kChannelTypeScalar, iFloatAnimIndex))[0]));
					AnimationClipCompressedData::ConstantData constChannel = {
						NULL, std::vector<size_t>(1, iFloat)
					};
					compressedData.m_const[kChannelTypeScalar].push_back(constChannel);
				} else {
					constFloatData[iType].push_back((*sourceData.GetSampledAnim(kChannelTypeScalar, iFloatAnimIndex))[0]);
					compressedData.m_const[kChannelTypeScalar][iType].m_ids.push_back(iFloat);
				}
			}
		}
	}
	
	// compress constant float channels
	{	
		for (unsigned iType = 0; iType < constFloatCompressionType.size(); ++iType) {
			compressedData.m_const[kChannelTypeScalar][iType].m_pCompressedArray = BitCompressFloatArray( constFloatData[iType], constFloatCompressionType[iType] );
		}
	}
}


#define COMPARE_TO_LOCAL_COMPRESSION 0

struct JointChildTranslationInfo 
{
	unsigned m_iJointAnimIndex;		// if (iJointAnimIndex == (unsinged)-1) m_matrix.GetRow(0) is the fixed translation,
	ITGEOM::Matrix4x4 m_matrix;		// otherwise, we must calculate m_matrix * jointChannelTable[iJointAnimIndex][iFrame].m_t
};

struct JointAnimInfo 
{
	unsigned m_iJointIndex;
	unsigned m_iJointAnimIndex;
	std::vector<JointAnimInfo*> m_children;
	std::vector<JointChildTranslationInfo> m_childTranslations;
};

typedef std::vector<JointAnimInfo*> JointAnimInfoList;

struct JointContext 
{
	JointAnimInfo const*			m_pJointAnimInfoParent;
	AlignedArray<SMath::Transform>	m_exactParentTransform;					// exact parent transform w/o parent scale [numDataFrames]
	AlignedArray<SMath::Vec4>		m_exactParentScale;						// exact parent scale [numDataFrames]
	AlignedArray<SMath::Transform>	m_decompressedParentTransform;			// decompressed parent transform w/o parent scale [numDataFrames]
	AlignedArray<SMath::Vec4>		m_decompressedParentScale;				// decompressed parent scale [numDataFrames]
#if COMPARE_TO_LOCAL_COMPRESSION
	AlignedArray<SMath::Transform>	m_localDecompressedParentTransform;		// non-globally optimized decompressed parent transform w/o parent scale [numDataFrames]
	AlignedArray<SMath::Vec4>		m_localDecompressedParentScale;			// non-globally optimized decompressed parent scale [numDataFrames]
#endif

	JointContext(JointAnimInfo const* pJointAnimInfoParent, unsigned numDataFrames)
	{
		m_pJointAnimInfoParent = pJointAnimInfoParent;
		m_exactParentTransform.resize(numDataFrames);
		m_exactParentScale.resize(numDataFrames);
		m_decompressedParentTransform.resize(numDataFrames);
		m_decompressedParentScale.resize(numDataFrames);
#if COMPARE_TO_LOCAL_COMPRESSION
		m_localDecompressedParentTransform.resize(numDataFrames);
		m_localDecompressedParentScale.resize(numDataFrames);
#endif
	}
	~JointContext()
	{
	}
};

ITGEOM::Vec3 GetCorrectedScale(ITGEOM::Vec3 const& vScale, 
							   SMath::Transform const& mCorrectionSkew)
{
	return ITGEOM::Vec3( vScale.x * SMath::Length( mCorrectionSkew.GetXAxis() ),
						 vScale.y * SMath::Length( mCorrectionSkew.GetYAxis() ),
						 vScale.z * SMath::Length( mCorrectionSkew.GetZAxis() ) );
}

ITGEOM::Vec3 GetCorrectedTranslation(ITGEOM::Vec3 const& vTranslation, 
									 ITGEOM::Vec3 const& vCorrectionTranslation, 
									 ITGEOM::Quat const& qCorrectionRotation, 
									 bool bCorrectForRotation )
{
	if (bCorrectForRotation)
		return qCorrectionRotation.RotateVector(vTranslation) + vCorrectionTranslation;
	else
		return vTranslation + vCorrectionTranslation;
}

ITGEOM::Quat GetCorrectedRotation(ITGEOM::Quat const& qRotation, 
								  ITGEOM::Quat const& qCorrectionRotation, 
								  ITGEOM::Vec3 const& vCorrectionTranslation, 
								  unsigned iFrame, 
								  JointAnimInfo const& jointAnimInfo, 
								  AnimationClipSourceData const& sourceData )
{
	// calculate correction to fix parent rotation error:
	if (jointAnimInfo.m_children.empty())
		return qCorrectionRotation * qRotation;

	// calculate additional correction to fix child translations:
	SMath::Transform mCorrection( SMath::BuildTransform( SMathQuat(qCorrectionRotation), SMathVec4( vCorrectionTranslation, 1.0f ) ) );
	SMath::Transform mCorrectionInv = SMath::OrthogonalInverse( mCorrection );
	unsigned iUseFrame = iFrame;
	if (sourceData.IsConstant(kChannelTypeTranslation, jointAnimInfo.m_iJointAnimIndex) || sourceData.m_flags & kClipLooping)
		iUseFrame = 0;
	SMath::Vector vTranslation = SMathVector( (*sourceData.GetSampledAnim(kChannelTypeTranslation, jointAnimInfo.m_iJointAnimIndex))[iUseFrame] );
	ITGEOM::Vec3 vLocalCorrection = qRotation.Conjugate().RotateVector( ITGEOMVec3( vTranslation * mCorrectionInv - vTranslation ) );

	ITGEOM::Vec4 vChildCorrection(0.0f, 0.0f, 0.0f, 0.0f);
	for (unsigned i = 0; i < jointAnimInfo.m_childTranslations.size(); ++i) {
		JointChildTranslationInfo const& childTranslation = jointAnimInfo.m_childTranslations[i];
		ITGEOM::Vec3 vChildTranslation;
		if ((unsigned)childTranslation.m_iJointAnimIndex < sourceData.GetNumAnims(kChannelTypeTranslation)) {
			vChildTranslation = childTranslation.m_matrix * ITGEOMVec4( (*sourceData.GetSampledAnim(kChannelTypeTranslation, childTranslation.m_iJointAnimIndex))[ iUseFrame ], 1.0f );
		} else {
			vChildTranslation = childTranslation.m_matrix.GetRow(0);
		}
		float fTransLenSqr = vChildTranslation.x*vChildTranslation.x + vChildTranslation.y*vChildTranslation.y + vChildTranslation.z*vChildTranslation.z;
		ITGEOM::Vec3 vAxis = ITGEOM::CrossProduct3( vChildTranslation, vLocalCorrection );
		float fAxisLenSqr = vAxis.x*vAxis.x + vAxis.y*vAxis.y + vAxis.z*vAxis.z;
		if (fAxisLenSqr < 0.00001f * fTransLenSqr) {
			vChildCorrection += ITGEOM::Vec4(0.0f, 0.0f, 0.0f, fTransLenSqr);		// weight by squared length of child translation
		} else {
			float fAxisLen = sqrtf(fAxisLenSqr);
			ITGEOM::Quat qChildCorrection_i( vAxis / fAxisLen, fAxisLen / fTransLenSqr );
			vChildCorrection += ITGEOM::Vec4( qChildCorrection_i.x, qChildCorrection_i.y, qChildCorrection_i.z, qChildCorrection_i.w ) * fTransLenSqr;	// weight by squared length of child translation
		}
	}
	vChildCorrection.Normalize();
	ITGEOM::Quat qChildCorrection(vChildCorrection.x, vChildCorrection.y, vChildCorrection.z, vChildCorrection.w);

	return qChildCorrection * qCorrectionRotation * qRotation;
}

void GetChildTranslations_DepthFirst(JointAnimInfo *pJointAnimInfo, 
									 JointAnimInfo *pParentJointAnimInfo, 
									 ITGEOM::Matrix4x4 const& mParent, 
									 ITGEOM::Vec3 const& vParentScale, 
									 AnimationClipSourceData const& sourceData, 
									 std::vector<Joint> const& joints )
{
	unsigned const numJointAnims = (unsigned)sourceData.GetNumAnims(kChannelTypeScale);
	Joint const* pJoint = &joints[ pJointAnimInfo->m_iJointIndex ];
	if (pParentJointAnimInfo) {
		// Calculate this joint's bind-pose / constant translation in the current parent space and add it to the parent's childTranslation list:
		JointChildTranslationInfo childTranslation;
		ITGEOM::Vec3 vChildTranslation;
		if ((unsigned)pJointAnimInfo->m_iJointAnimIndex < numJointAnims) {
			if (!sourceData.IsConstant(kChannelTypeTranslation, pJointAnimInfo->m_iJointAnimIndex)) {
				ITGEOM::Vec4 vParentScale4( vParentScale.x, vParentScale.y, vParentScale.z, 1.0f );
				childTranslation.m_iJointAnimIndex = pJointAnimInfo->m_iJointAnimIndex;
				childTranslation.m_matrix.SetRow(0, mParent.GetRow(0) * vParentScale4 );
				childTranslation.m_matrix.SetRow(1, mParent.GetRow(1) * vParentScale4 );
				childTranslation.m_matrix.SetRow(2, mParent.GetRow(2) * vParentScale4 );
				childTranslation.m_matrix.SetRow(3, ITGEOM::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
			} else {
				vChildTranslation = (*sourceData.GetSampledAnim(kChannelTypeTranslation, pJointAnimInfo->m_iJointAnimIndex))[0] * vParentScale;
				childTranslation.m_iJointAnimIndex = (unsigned)-1;
				childTranslation.m_matrix.SetRow(0, mParent * ITGEOMVec4(vChildTranslation, 1.0f));
			}
		} else {
			vChildTranslation = pJoint->m_jointTranslation * vParentScale;
			childTranslation.m_iJointAnimIndex = (unsigned)-1;
			childTranslation.m_matrix.SetRow(0, mParent * ITGEOMVec4(vChildTranslation, 1.0f));
		}
		pParentJointAnimInfo->m_childTranslations.push_back( childTranslation );
	}

	ITGEOM::Vec3 vScale, vTranslation;
	ITGEOM::Matrix3x3 mRotation;
	if ((unsigned)pJointAnimInfo->m_iJointAnimIndex < numJointAnims) {
		bool const bConstantScale = !!sourceData.IsConstant(kChannelTypeScale, pJointAnimInfo->m_iJointAnimIndex);
		bool const bConstantRotation = !!sourceData.IsConstant(kChannelTypeRotation, pJointAnimInfo->m_iJointAnimIndex);
		bool const bConstantTranslation = !!sourceData.IsConstant(kChannelTypeTranslation, pJointAnimInfo->m_iJointAnimIndex);
		if (!bConstantScale || !bConstantRotation || !bConstantTranslation) {
			// if this joint has a non-constant animation, make it the new parent and traverse its children:
			ITGEOM::Matrix4x4 mParent;
			ITGEOM::Vec3 vParentScale(1.0f, 1.0f, 1.0f);
			JointAnimInfoList::iterator it = pJointAnimInfo->m_children.begin(), itEnd = pJointAnimInfo->m_children.end();
			for (; it != itEnd; ++it)
				GetChildTranslations_DepthFirst(*it, pJointAnimInfo, mParent, vParentScale, sourceData, joints);
			return;
		}
		// if this joint is constant, continue traversing with original parent, multiplying in the constant local matrix
		vScale = (*sourceData.GetSampledAnim(kChannelTypeScale, pJointAnimInfo->m_iJointAnimIndex))[0];
		vTranslation = (*sourceData.GetSampledAnim(kChannelTypeTranslation, pJointAnimInfo->m_iJointAnimIndex))[0];
		ITGEOM::Quat q = (*sourceData.GetSampledAnim(kChannelTypeRotation, pJointAnimInfo->m_iJointAnimIndex))[0];
		ITGEOM::Matrix3x3FromQuat(&mRotation, &q);
	} else {
		// if this joint has no animation, continue traversing with original parent, multiplying in the constant local bind matrix
		vScale = pJoint->m_jointScale;
		vTranslation = pJoint->m_jointTranslation;
		ITGEOM::Matrix3x3FromQuat(&mRotation, &pJoint->m_jointQuat);
	}
	// if this joint is constant, continue traversing with original parent:
	ITGEOM::Matrix4x4 mChild(	ITGEOMVec4(mRotation.GetRow(0), vTranslation.x),
								ITGEOMVec4(mRotation.GetRow(1), vTranslation.y),
								ITGEOMVec4(mRotation.GetRow(2), vTranslation.z),
								ITGEOM::Vec4(0.0f, 0.0f, 0.0f, 1.0f) );
	ITGEOM::Matrix4x4 mJoint;
	if (!(pJoint->m_flags & kJointSegmentScaleCompensate)) {
		ITGEOM::Vec4 vParentScale4 = ITGEOMVec4( vParentScale, 1.0f );
		mJoint.SetRow(0, mParent.GetRow(0) * vParentScale4);
		mJoint.SetRow(1, mParent.GetRow(1) * vParentScale4);
		mJoint.SetRow(2, mParent.GetRow(2) * vParentScale4);
		mJoint.SetRow(3, ITGEOM::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
	} else {
		mChild.m_r[0].w *= vParentScale.x;
		mChild.m_r[1].w *= vParentScale.y;
		mChild.m_r[2].w *= vParentScale.z;
		mJoint = mParent;
	}
	mJoint = mJoint * mChild;
	JointAnimInfoList::iterator it = pJointAnimInfo->m_children.begin(), itEnd = pJointAnimInfo->m_children.end();
	for (; it != itEnd; ++it)
		GetChildTranslations_DepthFirst(*it, pParentJointAnimInfo, mJoint, vScale, sourceData, joints);
}

void CompressAnimatedData_DepthFirst(JointAnimInfo const* pJointAnimInfo,
									 JointContext const* pParentContext, 
									 AnimationClipCompressedData& compressedData, 
									 ClipCompressionDesc const &compression, 
									 AnimationBinding const& binding, 
									 AnimationClipSourceData const& sourceData, 
									 std::vector<Joint> const& joints, 
									 KeyExtractor const& keyExtractor, 
									 std::set<size_t> const& crossCorrelatedRotations)
{
	unsigned const numOutputFrames = (sourceData.m_flags & kClipLooping) ? sourceData.m_numFrames+1 : sourceData.m_numFrames;
	unsigned const numDataFrames = sourceData.m_numFrames;
	unsigned iJoint = pJointAnimInfo->m_iJointIndex;
	Joint const* pJoint = &joints[ iJoint ];
	bool bInheritParentScale = !(pJoint->m_flags & kJointSegmentScaleCompensate);

	JointContext jointContext(pJointAnimInfo, numDataFrames);
	const unsigned numJointAnims = (unsigned)binding.m_jointAnimIdToJointId.size();

#if COMPARE_TO_LOCAL_COMPRESSION
	struct CompressedJointAnims {
		IBitCompressedVec3Array *scale;
		IBitCompressedQuatArray *rotation;
		IBitCompressedVec3Array *translation;
	};
	typedef std::vector<CompressedJointAnims> CompressedJointAnimsList;
	CompressedJointAnims cjaNULL = {NULL, NULL, NULL};
	CompressedJointAnimsList localCompressedJointAnims(numJointAnims, cjaNULL);
#endif
	unsigned const iJointAnimIndex = pJointAnimInfo->m_iJointAnimIndex;
	bool const bConstantScale = !!sourceData.IsConstant(kChannelTypeScale, pJointAnimInfo->m_iJointAnimIndex);
	bool const bConstantRotation = !!sourceData.IsConstant(kChannelTypeRotation, pJointAnimInfo->m_iJointAnimIndex);
	bool const bConstantTranslation = !!sourceData.IsConstant(kChannelTypeTranslation, pJointAnimInfo->m_iJointAnimIndex);
	IBitCompressedQuatArray const* pCompressedArrayQuat = NULL;

	std::vector<ChannelCompressionDesc> const& formatScale = compression.m_format[kChannelTypeScale];
	std::vector<ChannelCompressionDesc> const& formatQuat = compression.m_format[kChannelTypeRotation];
	std::vector<ChannelCompressionDesc> const& formatTrans = compression.m_format[kChannelTypeTranslation];

	if (iJointAnimIndex < numJointAnims && binding.m_jointAnimIdToJointId[iJointAnimIndex] >= 0) {
		unsigned iAnimatedJoint = (unsigned)binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
		ITASSERT(iAnimatedJoint < (unsigned)binding.m_animatedJointIndexToJointAnimId.size());
		if (!bConstantScale || !bConstantRotation || !bConstantTranslation) {
			// Compress our joint parameters:
			std::vector<ITGEOM::Vec3> vCorrectionScale;
			std::vector<ITGEOM::Quat> qCorrectionRotation;
			std::vector<ITGEOM::Vec3> vCorrectionTranslation;
			if (pParentContext) {
				vCorrectionScale.resize(numDataFrames);
				qCorrectionRotation.resize(numDataFrames);
				vCorrectionTranslation.resize(numDataFrames);
				for (unsigned iFrame = 0; iFrame < numDataFrames; ++iFrame) {
					SMath::Transform mScale, mCorrection;
					mScale.SetScale( SMath::Vector( pParentContext->m_exactParentScale[iFrame] ) );
					SMath::Transform mExact = mScale * pParentContext->m_exactParentTransform[iFrame];
					mScale.SetScale( SMath::Vector( pParentContext->m_decompressedParentScale[iFrame] ) );
					SMath::Transform mDecompressed = mScale * pParentContext->m_decompressedParentTransform[iFrame];
					mCorrection = mExact * SMath::Inverse(mDecompressed);

					SMath::Vector vScale;
					SMath::Quat qRotation;
					SMath::Point vTranslation;
					SMathMat44_DecomposeAffine(mCorrection.GetMat44(), &vScale, &qRotation, &vTranslation);
					vCorrectionScale[iFrame] = ITGEOMVec3( vScale );
					qCorrectionRotation[iFrame] = ITGEOMQuat( qRotation );
					vCorrectionTranslation[iFrame] = ITGEOMVec3( vTranslation );
				}
				if (sourceData.m_flags & kClipLooping)
				{
					vCorrectionScale.push_back(vCorrectionScale[0]);
					qCorrectionRotation.push_back(qCorrectionRotation[0]);
					vCorrectionTranslation.push_back(vCorrectionTranslation[0]);
				}
			}

			if (!bConstantScale) {
				ITGEOM::Vec3Array values;
				if (pParentContext) {
					FrameArray frames;
					keyExtractor.GetScaleValues(iJointAnimIndex, values, frames);
					for (unsigned iKey = 0; iKey < frames.size(); ++iKey)
						values[iKey] = values[iKey] * vCorrectionScale[frames[iKey]];
				} else
					keyExtractor.GetScaleValues(iJointAnimIndex, values);
				AnimationClipCompressedData::AnimatedData animChannel = {
					BitCompressVec3Array( values, formatScale[iAnimatedJoint].m_compressionType, formatScale[iAnimatedJoint].m_bitFormat, &formatScale[iAnimatedJoint].m_contextData ),
					iAnimatedJoint
				};
				compressedData.m_anims[kChannelTypeScale].push_back( animChannel );
			}
			if (!bConstantRotation) {
				if (crossCorrelatedRotations.count(iJointAnimIndex)) {
					// If this joint rotation used cross correlation compression, just look up the previously compressed values.
					// We may not be able to correct it's target values, as it may be correlated to a joint rotation below it in the hierarchy,
					// and, even if this is not the case, any joint rotation correlated to this one would be distorted by correcting this animations values.
					for (auto it = compressedData.m_anims[kChannelTypeRotation].begin(); it != compressedData.m_anims[kChannelTypeRotation].end(); ++it) {
						if (it->m_id == iAnimatedJoint) {
							pCompressedArrayQuat = dynamic_cast<IBitCompressedQuatArray const*>(it->m_pCompressedArray);
							break;
						}
					}
					ITASSERT(pCompressedArrayQuat != NULL);
				} else {
					ITGEOM::QuatArray values;
					if (pParentContext) {
						FrameArray frames;
						keyExtractor.GetRotationValues(iJointAnimIndex, values, frames);
						for (unsigned iKey = 0; iKey < frames.size(); ++iKey)
							values[iKey] = GetCorrectedRotation(values[iKey], qCorrectionRotation[frames[iKey]], vCorrectionTranslation[frames[iKey]], frames[iKey], *pJointAnimInfo, sourceData);
					} else
						keyExtractor.GetRotationValues(iJointAnimIndex, values);
					AnimationClipCompressedData::AnimatedData animChannel = {
						BitCompressQuatArray( values, formatQuat[iAnimatedJoint].m_compressionType, formatQuat[iAnimatedJoint].m_bitFormat, &formatQuat[iAnimatedJoint].m_contextData ),
						iAnimatedJoint
					};
					compressedData.m_anims[kChannelTypeRotation].push_back( animChannel );
					pCompressedArrayQuat = dynamic_cast<IBitCompressedQuatArray const*>(animChannel.m_pCompressedArray);
				}
			}
			if (!bConstantTranslation) {
				ITGEOM::Vec3Array values;
				if (pParentContext) {
					FrameArray frames;
					keyExtractor.GetTranslationValues(iJointAnimIndex, values, frames);
					for (unsigned iKey = 0; iKey < frames.size(); ++iKey)
						values[iKey] = GetCorrectedTranslation(values[iKey], vCorrectionTranslation[frames[iKey]], qCorrectionRotation[frames[iKey]], bConstantRotation);
				} else
					keyExtractor.GetScaleValues(iJointAnimIndex, values);
				AnimationClipCompressedData::AnimatedData animChannel = {
					BitCompressVec3Array( values, formatTrans[iAnimatedJoint].m_compressionType, formatTrans[iAnimatedJoint].m_bitFormat, &formatTrans[iAnimatedJoint].m_contextData ),
					iAnimatedJoint
				};
				compressedData.m_anims[kChannelTypeTranslation].push_back( animChannel );
			}
#if COMPARE_TO_LOCAL_COMPRESSION
			if (!bConstantScale) {
				ITGEOM::Vec3Array values;
				keyExtractor.GetScaleValues(iJointAnimIndex, values);
				localCompressedJointAnims[iJointAnimIndex].scale = BitCompressVec3Array( values, formatScale[iAnimatedJoint].m_compressionType, formatScale[iAnimatedJoint].m_bitFormat, &formatScale[iAnimatedJoint].m_contextData );
			}
			if (!bConstantRotation && !crossCorrelatedRotations.count(iJointAnimIndex)) {
				ITGEOM::QuatArray values;
				keyExtractor.GetRotationValues(iJointAnimIndex, values);
				localCompressedJointAnims[iJointAnimIndex].rotation = BitCompressQuatArray( values, formatQuat[iAnimatedJoint].m_compressionType, formatQuat[iAnimatedJoint].m_bitFormat, &formatQuat[iAnimatedJoint].m_contextData );
			}
			if (!bConstantTranslation) {
				ITGEOM::Vec3Array values;
				keyExtractor.GetTranslationValues(iJointAnimIndex, values);
				localCompressedJointAnims[iJointAnimIndex].translation = BitCompressVec3Array( values, formatTrans[iAnimatedJoint].m_compressionType, formatTrans[iAnimatedJoint].m_bitFormat, &formatTrans[iAnimatedJoint].m_contextData );
			}
#endif
		}

		// multiply our transform into each frame:
		ITGEOM::Vec3Array decompressed_s, decompressed_t;
		ITGEOM::QuatArray decompressed_q;
		SMath::Transform mRotate, mRotateD;
		if (bConstantScale) {
			ITGEOM::Vec3 sConst = CalculateDecompressedConstVec3( formatScale[ iAnimatedJoint ].m_compressionType, (*sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex))[0] );
			decompressed_s.resize( numOutputFrames, sConst );
		} else {
			IBitCompressedVec3Array const* pCompressedS = 
				dynamic_cast<IBitCompressedVec3Array const*>(compressedData.m_anims[kChannelTypeScale].back().m_pCompressedArray);
			pCompressedS->GetDecompressedSamples(decompressed_s);
		}
		if (bConstantRotation) {
			mRotate = SMath::Transform( SMath::BuildTransform( SMathQuat((*sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex))[0]), SMath::Vec4(0.0f, 0.0f, 0.0f, 1.0f) ) );
			ITGEOM::Quat qConst = CalculateDecompressedConstQuat( formatQuat[ iAnimatedJoint ].m_compressionType, (*sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex))[0] );
			decompressed_q.resize( numOutputFrames, qConst);
			mRotateD = SMath::Transform( SMath::BuildTransform( SMathQuat(qConst), SMath::Vec4(0.0f, 0.0f, 0.0f, 1.0f) ) );
		} else {
			IBitCompressedQuatArray const *pCompressedQ = pCompressedArrayQuat;
			pCompressedQ->GetDecompressedSamples(decompressed_q);
		}
		if (bConstantTranslation) {
			ITGEOM::Vec3 tConst = CalculateDecompressedConstVec3( formatTrans[ iAnimatedJoint ].m_compressionType, (*sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex))[0] );
			decompressed_t.resize( numOutputFrames, tConst );
		} else {
			IBitCompressedVec3Array const* pCompressedT = 
				dynamic_cast<IBitCompressedVec3Array const*>(compressedData.m_anims[kChannelTypeTranslation].back().m_pCompressedArray);
			pCompressedT->GetDecompressedSamples(decompressed_t);
		}
#if COMPARE_TO_LOCAL_COMPRESSION
		ITGEOM::Vec3Array local_decompressed_s, local_decompressed_t;
		ITGEOM::QuatArray local_decompressed_q;
		SMath::Transform mRotateLocalD;
		if (bConstantScale) {
			local_decompressed_s.resize( numOutputFrames, decompressed_s[0] );
		} else {
			IBitCompressedVec3Array const *pCompressedS = localCompressedJointAnims[iJointAnimIndex].scale;
			pCompressedS->GetDecompressedSamples(local_decompressed_s);
		}
		if (bConstantRotation) {
			mRotateLocalD = mRotateD;
		} else if (!crossCorrelatedRotations.count(iJointAnimIndex)) {
			IBitCompressedQuatArray const *pCompressedQ = localCompressedJointAnims[iJointAnimIndex].rotation;
			pCompressedQ->GetDecompressedSamples(local_decompressed_q);
		}
		if (bConstantTranslation) {
			local_decompressed_t.resize( numOutputFrames, decompressed_t[0] );
		} else {
			IBitCompressedVec3Array const *pCompressedT = localCompressedJointAnims[iJointAnimIndex].translation;
			pCompressedT->GetDecompressedSamples(local_decompressed_t);
		}
#endif
		if (pParentContext) {
			if (bInheritParentScale) {
				SMath::Transform mScale;
				for (unsigned iFrame = 0; iFrame < numDataFrames; ++iFrame) {
					unsigned translationFrame = bConstantTranslation ? 0 : iFrame;
					if (!bConstantRotation) {
						mRotate = SMath::Transform( SMath::BuildTransform( 
							SMathQuat((*sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex))[iFrame]), 
							SMathVec4((*sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex))[translationFrame], 1.0f) ) );
						mRotateD = SMath::Transform( SMath::BuildTransform( SMathQuat(decompressed_q[iFrame]), SMathVec4(decompressed_t[iFrame], 1.0f) ) );
					} else {
						mRotate.SetTranslation( SMathPoint((*sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex))[translationFrame]) );
						mRotateD.SetTranslation( SMathPoint(decompressed_t[iFrame]) );
					}
					mScale.SetScale( SMath::Vector(pParentContext->m_exactParentScale[ iFrame ]) );
					jointContext.m_exactParentTransform[iFrame] = mRotate * mScale * pParentContext->m_exactParentTransform[ iFrame ];
					jointContext.m_exactParentScale[iFrame] = SMathVec4( (*sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex))[bConstantScale ? 0 : iFrame], 1.0f );
					mScale.SetScale( SMath::Vector(pParentContext->m_decompressedParentScale[ iFrame ]) );
					jointContext.m_decompressedParentTransform[iFrame] = mRotateD * mScale * pParentContext->m_decompressedParentTransform[ iFrame ];
					jointContext.m_decompressedParentScale[iFrame] = SMathVec4( decompressed_s[iFrame], 1.0f );

					ITGEOM::Quat qWorldE = ITGEOMQuat( SMath::Quat( jointContext.m_exactParentTransform[iFrame].GetRawMat44() ) );
					ITGEOM::Vec3 vWorldE = ITGEOMVec3( jointContext.m_exactParentTransform[iFrame].GetTranslation() );
					ITGEOM::Quat qWorldD = ITGEOMQuat( SMath::Quat( jointContext.m_decompressedParentTransform[iFrame].GetRawMat44() ) );
					ITGEOM::Vec3 vWorldD = ITGEOMVec3( jointContext.m_decompressedParentTransform[iFrame].GetTranslation() );
					float fRotError = ComputeError_Quat( qWorldD, qWorldE );
					float fTransError = ComputeError_Vec3( vWorldD, vWorldE );
					(void)fRotError; (void)fTransError;
#if COMPARE_TO_LOCAL_COMPRESSION
					if (!channelsConstant.m_quat && !crossCorrelatedRotations.count(iJointAnimIndex)) {
						mRotateLocalD = SMath::Transform( SMath::BuildTransform( SMathQuat(local_decompressed_q[iFrame]), SMathVec4(local_decompressed_t[iFrame], 1.0f) ) );
					} else {
						mRotateLocalD.SetTranslation( SMathPoint(local_decompressed_t[iFrame]) );
					}
					mScale.SetScale(pParentContext->m_localDecompressedParentScale[iFrame]);
					jointContext.m_localDecompressedParentTransform[iFrame] = mRotateLocalD * mScale * pParentContext->m_localDecompressedParentTransform[ iFrame ];
					jointContext.m_localDecompressedParentScale[iFrame] = SMathVec4( local_decompressed_s[iFrame], 1.0f );
					ITGEOM::Quat qWorldLocalD = ITGEOMQuat( SMath::Quat( jointContext.m_localDecompressedParentTransform[iFrame].GetRawMat44() ) );
					float fLocalRotError = ComputeError_Quat( qWorldLocalD, qWorldE );
					ITGEOM::Vec3 vWorldLocalD = ITGEOMVec3( jointContext.m_localDecompressedParentTransform[iFrame].GetTranslation() );
					float fLocalTransError = ComputeError_Vec3( vWorldLocalD, vWorldE );
					(void)fLocalRotError; (void)fLocalTransError;
#endif
				}
			} else {
				for (unsigned iFrame = 0; iFrame < numDataFrames; ++iFrame) {
					unsigned translationFrame = bConstantTranslation ? 0 : iFrame;
					if (!bConstantRotation) {
						mRotate = SMath::Transform( SMath::BuildTransform( 
							SMathQuat((*sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex))[iFrame]), 
							SMath::Vec4(pParentContext->m_exactParentScale[ iFrame ]) * SMathVec4((*sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex))[translationFrame], 1.0f) ) );
						mRotateD = SMath::Transform( SMath::BuildTransform( SMathQuat(decompressed_q[iFrame]), SMath::Vec4(pParentContext->m_decompressedParentScale[ iFrame ]) * SMathVec4(decompressed_t[iFrame], 1.0f) ) );
					} else {
						mRotate.SetTranslation( SMath::Point(pParentContext->m_exactParentScale[ iFrame ] * SMathVec4((*sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex))[translationFrame], 1.0f)) );
						mRotateD.SetTranslation( SMath::Point(pParentContext->m_decompressedParentScale[ iFrame ] * SMathVec4(decompressed_t[iFrame], 1.0f)) );
					}
					jointContext.m_exactParentTransform[iFrame] = mRotate * pParentContext->m_exactParentTransform[ iFrame ];
					jointContext.m_exactParentScale[iFrame] = SMathVec4( (*sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex))[bConstantScale ? 0 : iFrame], 1.0f );
					jointContext.m_decompressedParentTransform[iFrame] = mRotateD * pParentContext->m_decompressedParentTransform[ iFrame ];
					jointContext.m_decompressedParentScale[iFrame] = SMathVec4( decompressed_s[iFrame], 1.0f );

					ITGEOM::Quat qWorldE = ITGEOMQuat( SMath::Quat( jointContext.m_exactParentTransform[iFrame].GetRawMat44() ) );
					ITGEOM::Vec3 vWorldE = ITGEOMVec3( jointContext.m_exactParentTransform[iFrame].GetTranslation() );
					ITGEOM::Quat qWorldD = ITGEOMQuat( SMath::Quat( jointContext.m_decompressedParentTransform[iFrame].GetRawMat44() ) );
					ITGEOM::Vec3 vWorldD = ITGEOMVec3( jointContext.m_decompressedParentTransform[iFrame].GetTranslation() );
					float fRotError = ComputeError_Quat( qWorldD, qWorldE );
					float fTransError = ComputeError_Vec3( vWorldD, vWorldE );
					(void)fRotError; (void)fTransError;
#if COMPARE_TO_LOCAL_COMPRESSION
					if (!bConstantRotation && !crossCorrelatedRotations.count(iJointAnimIndex)) {
						mRotateLocalD = SMath::Transform( SMath::BuildTransform( SMathQuat(local_decompressed_q[iFrame]), SMath::Vec4(pParentContext->m_decompressedParentScale[ iFrame ]) * SMathVec4(local_decompressed_t[iFrame], 1.0f) ) );
					} else {
						mRotateLocalD.SetTranslation( SMath::Point(pParentContext->m_localDecompressedParentScale[ iFrame ] * SMathVec4(local_decompressed_t[iFrame], 1.0f)) );
					}
					jointContext.m_localDecompressedParentTransform[iFrame] = mRotateLocalD * pParentContext->m_localDecompressedParentTransform[ iFrame ];
					jointContext.m_localDecompressedParentScale[iFrame] = SMathVec4( local_decompressed_s[iFrame], 1.0f );
					ITGEOM::Quat qWorldLocalD = ITGEOMQuat( SMath::Quat( jointContext.m_localDecompressedParentTransform[iFrame].GetRawMat44() ) );
					float fLocalRotError = ComputeError_Quat( qWorldLocalD, qWorldE );
					ITGEOM::Vec3 vWorldLocalD = ITGEOMVec3( jointContext.m_localDecompressedParentTransform[iFrame].GetTranslation() );
					float fLocalTransError = ComputeError_Vec3( vWorldLocalD, vWorldE );
					(void)fLocalRotError; (void)fLocalTransError;
					if (fTransError > fLocalTransError) {
						int q = 0; (void)q;
					}
#endif
				}
			}
		} else {
			for (unsigned iFrame = 0; iFrame < numDataFrames; ++iFrame) {
				unsigned translationFrame = bConstantTranslation ? 0 : iFrame;
				if (!bConstantRotation) {
					mRotate = SMath::Transform( SMath::BuildTransform( 
						SMathQuat((*sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex))[iFrame]), 
						SMathVec4((*sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex))[translationFrame], 1.0f) ) );
					mRotateD = SMath::Transform( SMath::BuildTransform( SMathQuat(decompressed_q[iFrame]), SMathVec4(decompressed_t[iFrame], 1.0f) ) );
				} else {
					mRotate.SetTranslation( SMathPoint((*sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex))[translationFrame]) );
					mRotateD.SetTranslation( SMathPoint(decompressed_t[iFrame]) );
				}
				jointContext.m_exactParentTransform[iFrame] = mRotate;
				jointContext.m_exactParentScale[iFrame] = SMathVec4( (*sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex))[bConstantScale ? 0 : iFrame], 1.0f  );
				jointContext.m_decompressedParentTransform[iFrame] = mRotateD;
				jointContext.m_decompressedParentScale[iFrame] = SMathVec4( decompressed_s[iFrame], 1.0f  );

				ITGEOM::Quat qWorldE = ITGEOMQuat( SMath::Quat( jointContext.m_exactParentTransform[iFrame].GetRawMat44() ) );
				ITGEOM::Vec3 vWorldE = ITGEOMVec3( jointContext.m_exactParentTransform[iFrame].GetTranslation() );
				ITGEOM::Quat qWorldD = ITGEOMQuat( SMath::Quat( jointContext.m_decompressedParentTransform[iFrame].GetRawMat44() ) );
				ITGEOM::Vec3 vWorldD = ITGEOMVec3( jointContext.m_decompressedParentTransform[iFrame].GetTranslation() );
				float fRotError = ComputeError_Quat( qWorldD, qWorldE );
				float fTransError = ComputeError_Vec3( vWorldD, vWorldE );
				(void)fRotError; (void)fTransError;
#if COMPARE_TO_LOCAL_COMPRESSION
				if (!bConstantRotation && !crossCorrelatedRotations.count(iJointAnimIndex)) {
					mRotateLocalD = SMath::Transform( SMath::BuildTransform( SMathQuat(local_decompressed_q[iFrame]), SMathVec4(local_decompressed_t[iFrame], 1.0f) ) );
				} else {
					mRotateLocalD.SetTranslation( SMathPoint(local_decompressed_t[iFrame]) );
				}
				jointContext.m_localDecompressedParentTransform[iFrame] = mRotateLocalD;
				jointContext.m_localDecompressedParentScale[iFrame] = SMathVec4( local_decompressed_s[iFrame], 1.0f );
				ITGEOM::Quat qWorldLocalD = ITGEOMQuat( SMath::Quat( jointContext.m_localDecompressedParentTransform[iFrame].GetRawMat44() ) );
				float fLocalRotError = ComputeError_Quat( qWorldLocalD, qWorldE );
				ITGEOM::Vec3 vWorldLocalD = ITGEOMVec3( jointContext.m_localDecompressedParentTransform[iFrame].GetTranslation() );
				float fLocalTransError = ComputeError_Vec3( vWorldLocalD, vWorldE );
				(void)fLocalRotError; (void)fLocalTransError;
#endif
			}
		}
	} else {
		// multiply our bind pose into each frame:
		SMath::Vec4 const t = SMathVec4(pJoint->m_jointTranslation, 1.0f);
		SMath::Transform mRotate = SMath::Transform( SMath::BuildTransform( SMathQuat(pJoint->m_jointQuat), t ) );
		SMath::Vec4 const s = SMathVec4(pJoint->m_jointScale, 1.0f);
		if (pParentContext) {
			if (bInheritParentScale) {
				SMath::Transform mScale;
				for (unsigned iFrame = 0; iFrame < numDataFrames; ++iFrame) {
					mScale.SetScale( SMath::Vector(pParentContext->m_exactParentScale[ iFrame ]) );
					jointContext.m_exactParentTransform[iFrame] = mRotate * mScale * pParentContext->m_exactParentTransform[ iFrame ];
					mScale.SetScale( SMath::Vector(pParentContext->m_decompressedParentScale[ iFrame ]) );
					jointContext.m_decompressedParentTransform[iFrame] = mRotate * mScale * pParentContext->m_decompressedParentTransform[ iFrame ];
					jointContext.m_exactParentScale[iFrame] = jointContext.m_decompressedParentScale[iFrame] = s;
#if COMPARE_TO_LOCAL_COMPRESSION
					mScale.SetScale(pParentContext->m_localDecompressedParentScale[ iFrame ]);
					jointContext.m_localDecompressedParentTransform[iFrame] = mRotate * mScale * pParentContext->m_localDecompressedParentTransform[ iFrame ];
					jointContext.m_localDecompressedParentScale[iFrame] = s;
#endif
				}
			} else {
				for (unsigned iFrame = 0; iFrame < numDataFrames; ++iFrame) {
					mRotate.SetTranslation( SMath::Point(pParentContext->m_exactParentScale[ iFrame ] * t) );
					jointContext.m_exactParentTransform[iFrame] = mRotate * pParentContext->m_exactParentTransform[ iFrame ];
					mRotate.SetTranslation( SMath::Point(pParentContext->m_decompressedParentScale[ iFrame ] * t) );
					jointContext.m_decompressedParentTransform[iFrame] = mRotate * pParentContext->m_decompressedParentTransform[ iFrame ];
					jointContext.m_exactParentScale[iFrame] = jointContext.m_decompressedParentScale[iFrame] = s;
#if COMPARE_TO_LOCAL_COMPRESSION
					mRotate.SetTranslation( SMath::Point(pParentContext->m_localDecompressedParentScale[ iFrame ] * t) );
					jointContext.m_localDecompressedParentTransform[iFrame] = mRotate * pParentContext->m_localDecompressedParentTransform[ iFrame ];
					jointContext.m_localDecompressedParentScale[iFrame] = s;
#endif
				}
			}
		} else {
			for (unsigned iFrame = 0; iFrame < numDataFrames; ++iFrame) {
				jointContext.m_exactParentTransform[ iFrame ] = jointContext.m_decompressedParentTransform[ iFrame ] = mRotate;
				jointContext.m_exactParentScale[iFrame] = jointContext.m_decompressedParentScale[iFrame] = s;
#if COMPARE_TO_LOCAL_COMPRESSION
				jointContext.m_localDecompressedParentTransform[iFrame] = mRotate;
				jointContext.m_localDecompressedParentScale[iFrame] = s;
#endif
			}
		}
	}

	// Now compress children using our compressed data as a reference
	JointAnimInfoList::const_iterator itChild = pJointAnimInfo->m_children.begin(), itChildEnd = pJointAnimInfo->m_children.end();
	for (; itChild != itChildEnd; ++itChild)
		CompressAnimatedData_DepthFirst(*itChild, &jointContext, compressedData, compression, binding, sourceData, joints, keyExtractor, crossCorrelatedRotations);
}

void BuildAnimationClipCompressedDataOptimizedGlobally(AnimationClipCompressedData& compressedData, 
													   AnimationBinding const& binding, 
													   AnimationClipSourceData const& sourceData, 
													   ClipCompressionDesc const& compression, 
													   KeyExtractor const& keyExtractor, 
													   std::vector<Joint> const& joints)
{
	ITASSERT(	sourceData.GetNumAnims(kChannelTypeScale) == binding.m_jointAnimIdToAnimatedJointIndex.size() &&
				sourceData.GetNumAnims(kChannelTypeRotation) == binding.m_jointAnimIdToAnimatedJointIndex.size() &&
				sourceData.GetNumAnims(kChannelTypeTranslation) == binding.m_jointAnimIdToAnimatedJointIndex.size() &&
				sourceData.GetNumAnims(kChannelTypeScalar) == binding.m_floatAnimIdToFloatId.size() );

	unsigned const numAnimatedJoints = (unsigned)binding.m_animatedJointIndexToJointAnimId.size();
	unsigned const numFloatChannels = (unsigned)binding.m_floatIdToFloatAnimId.size();
	compressedData.m_hierarchyId = binding.m_hierarchyId;
	compressedData.m_pRootAnim = NULL;
	if (numAnimatedJoints <= 0 && numFloatChannels <= 0) {
		compressedData.m_flags = 0;
		compressedData.m_numFrames = 0;
		ITASSERTF(numAnimatedJoints>0 || numFloatChannels>0, ("Warning: number of animated joints and float channels in bindpose is 0.\n"));
		return;
	}

	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToJointId.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();
	
	// FIXME - is this necessary or even correct (it won't delete the channels)?
	compressedData.Clear();

	compressedData.m_flags = (sourceData.m_flags & ~kClipCompressionMask) | (compression.m_flags & kClipCompressionMask);
	compressedData.m_numFrames = (sourceData.m_flags & kClipLooping) ? sourceData.m_numFrames+1 : sourceData.m_numFrames;
	if (sourceData.m_pRootAnim)
		compressedData.m_pRootAnim = sourceData.m_pRootAnim->ConstructCopy();

	std::vector<ChannelCompressionDesc> const& formatScale = compression.m_format[kChannelTypeScale];
	std::vector<ChannelCompressionDesc> const& formatQuat = compression.m_format[kChannelTypeRotation];
	std::vector<ChannelCompressionDesc> const& formatTrans = compression.m_format[kChannelTypeTranslation];

	std::set<size_t> crossCorrelatedRotations;
	if (!compression.m_jointAnimOrderQuatLog.empty() || !compression.m_jointAnimOrderQuatLogPca.empty()) {
		INOTE(IMsg::kBrief, "Global optimization can not be applied to cross correlated log or logpca compressed quaternions...\n");
		for (unsigned iType = 0; iType < 2; ++iType) {
			AnimChannelCompressionType compressionType = (iType == 0) ? kAcctQuatLog : kAcctQuatLogPca;
			std::vector<unsigned> const& jointAnimOrder = (compressionType == kAcctQuatLog) ? compression.m_jointAnimOrderQuatLog : compression.m_jointAnimOrderQuatLogPca;

			ITGEOM::Vec3Array avPrevOutputLogValues;
			for (unsigned i = 0, count = (unsigned)jointAnimOrder.size(); i < count; ++i)
			{
				unsigned iJointAnimIndex = jointAnimOrder[i];
				ITASSERT(iJointAnimIndex < numJointAnims);
				ITASSERT(!sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex));
				crossCorrelatedRotations.emplace(iJointAnimIndex);

				int iAnimatedJoint = binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
				ITASSERT((unsigned)iAnimatedJoint < numAnimatedJoints);
				
				ChannelCompressionDesc const& format = formatQuat[iAnimatedJoint];
				ITASSERT(format.m_compressionType == compressionType);

				ITGEOM::QuatArray values;
				keyExtractor.GetRotationValues( iJointAnimIndex, values );

				IBitCompressedQuatArray *pCompressedData = BitCompressQuatArray( values, format.m_compressionType, format.m_bitFormat, &format.m_contextData, &avPrevOutputLogValues );

				AnimationClipCompressedData::AnimatedData animChannel = { pCompressedData, iAnimatedJoint };
				compressedData.m_anims[kChannelTypeRotation].push_back( animChannel );
			}
		}
	}

	{
		// We must traverse joints in hierarchical order in order to adjust errors globally.
		// Collect joint parenting info:
		std::vector<JointAnimInfo> jointAnimInfo( joints.size() );
		JointAnimInfoList roots;
		for (unsigned iJoint = 0; iJoint < joints.size(); ++iJoint) {
			Joint const *pJoint = &joints[iJoint];
			jointAnimInfo[iJoint].m_iJointIndex = iJoint;
			if (pJoint->m_flags & kJointIsAnimated) {
				jointAnimInfo[iJoint].m_iJointAnimIndex = (unsigned)binding.m_jointIdToJointAnimId[iJoint];
			} else {
				jointAnimInfo[iJoint].m_iJointAnimIndex = (unsigned)-1;
			}

			int iParentIndex = pJoint->m_parent;
			if (iParentIndex >= 0)
				jointAnimInfo[iParentIndex].m_children.push_back( &jointAnimInfo[iJoint] );
			else
				roots.push_back( &jointAnimInfo[iJoint] );
		}
		// Find translations to optimize rotations for each joint:
		// For each joint, we traverse children until we hit non-constant children or leaf children,
		// and record their positions as translations to optimize:
		ITGEOM::Matrix4x4 mParent;
		ITGEOM::Vec3 vParentScale(1.0f, 1.0f, 1.0f);
		JointAnimInfoList::const_iterator itRoot = roots.begin(), itRootEnd = roots.end();
		for (; itRoot != itRootEnd; ++itRoot)
			GetChildTranslations_DepthFirst( *itRoot, NULL, mParent, vParentScale, sourceData, joints );

		// traverse joints in hierarchical order, compressing animations:
		for (itRoot = roots.begin(), itRootEnd = roots.end(); itRoot != itRootEnd; ++itRoot)
			CompressAnimatedData_DepthFirst( *itRoot, NULL, compressedData, compression, binding, sourceData, joints, keyExtractor, crossCorrelatedRotations );
	}

	// collect constant data
	std::vector< ITGEOM::Vec3Array > constScaleData;
	std::vector< ITGEOM::QuatArray > constRotationData;
	std::vector< ITGEOM::Vec3Array > constTranslationData;
	std::vector< std::vector<float> > constFloatData;
	std::vector< AnimChannelCompressionType > constScaleCompressionType;
	std::vector< AnimChannelCompressionType > constRotationCompressionType;
	std::vector< AnimChannelCompressionType > constTranslationCompressionType;
	std::vector< AnimChannelCompressionType > constFloatCompressionType;

	for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; ++iJointAnimIndex) {
		unsigned iAnimatedJoint = binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex];
		if (iAnimatedJoint >= numAnimatedJoints)
			continue;
		if (sourceData.IsConstant(kChannelTypeScale, iJointAnimIndex)) {
			AnimChannelCompressionType compressionType = formatScale[iAnimatedJoint].m_compressionType;
			unsigned iType;
			for (iType = 0; iType < constScaleCompressionType.size(); ++iType)
				if (compressionType == constScaleCompressionType[iType])
					break;
			if (iType == constScaleCompressionType.size()) {
				constScaleCompressionType.push_back(compressionType);
				constScaleData.push_back( ITGEOM::Vec3Array(1, (*sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex))[0]) );
				AnimationClipCompressedData::ConstantData constChannel = {
					NULL, std::vector<size_t>(1, iAnimatedJoint)
				};
				compressedData.m_const[kChannelTypeScale].push_back(constChannel);
			} else {
				constScaleData[iType].push_back( (*sourceData.GetSampledAnim(kChannelTypeScale, iJointAnimIndex))[0] );
				compressedData.m_const[kChannelTypeScale][iType].m_ids.push_back(iAnimatedJoint);
			}
		}
		if (sourceData.IsConstant(kChannelTypeRotation, iJointAnimIndex)) {
			AnimChannelCompressionType compressionType = formatQuat[iAnimatedJoint].m_compressionType;
			unsigned iType;
			for (iType = 0; iType < constRotationCompressionType.size(); ++iType)
				if (compressionType == constRotationCompressionType[iType])
					break;
			if (iType == constRotationCompressionType.size()) {
				constRotationCompressionType.push_back(compressionType);
				constRotationData.push_back( ITGEOM::QuatArray(1, (*sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex))[0]) );
				AnimationClipCompressedData::ConstantData constChannel = {
					NULL, std::vector<size_t>(1, iAnimatedJoint)
				};
				compressedData.m_const[kChannelTypeRotation].push_back(constChannel);
			} else {
				constRotationData[iType].push_back( (*sourceData.GetSampledAnim(kChannelTypeRotation, iJointAnimIndex))[0] );
				compressedData.m_const[kChannelTypeRotation][iType].m_ids.push_back(iAnimatedJoint);
			}
		}
		if (sourceData.IsConstant(kChannelTypeTranslation, iJointAnimIndex)) {
			AnimChannelCompressionType compressionType = formatTrans[iAnimatedJoint].m_compressionType;
			unsigned iType;
			for (iType = 0; iType < constTranslationCompressionType.size(); ++iType)
				if (compressionType == constTranslationCompressionType[iType])
					break;
			if (iType == constTranslationCompressionType.size()) {
				constTranslationCompressionType.push_back(compressionType);
				constTranslationData.push_back( ITGEOM::Vec3Array(1, (*sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex))[0]) );
				AnimationClipCompressedData::ConstantData constChannel = {
					NULL, std::vector<size_t>(1, iAnimatedJoint)
				};
				compressedData.m_const[kChannelTypeTranslation].push_back(constChannel);
			} else {
				constTranslationData[iType].push_back( (*sourceData.GetSampledAnim(kChannelTypeTranslation, iJointAnimIndex))[0] );
				compressedData.m_const[kChannelTypeTranslation][iType].m_ids.push_back(iAnimatedJoint);
			}
		}
	}
	{	// compress constant data
		unsigned iType;
		for (iType = 0; iType < constScaleCompressionType.size(); ++iType)
			compressedData.m_const[kChannelTypeScale][iType].m_pCompressedArray = BitCompressVec3Array( constScaleData[iType], constScaleCompressionType[iType] );
		for (iType = 0; iType < constRotationCompressionType.size(); ++iType)
			compressedData.m_const[kChannelTypeRotation][iType].m_pCompressedArray = BitCompressQuatArray( constRotationData[iType], constRotationCompressionType[iType] );
		for (iType = 0; iType < constTranslationCompressionType.size(); ++iType)
			compressedData.m_const[kChannelTypeTranslation][iType].m_pCompressedArray = BitCompressVec3Array( constTranslationData[iType], constTranslationCompressionType[iType] );
	}

	// Collect and compress float animation data in increasing order of float channel index
	std::vector<ChannelCompressionDesc> const& formatFloat = compression.m_format[kChannelTypeScalar];
	for (unsigned iFloat = 0; iFloat < numFloatChannels; ++iFloat) {
		unsigned iFloatAnimIndex = binding.m_floatIdToFloatAnimId[iFloat];
		if (iFloatAnimIndex >= numFloatAnims) {
			continue;
		}
		SampledAnim const& floatAnim = *sourceData.GetSampledAnim(kChannelTypeScalar, iFloatAnimIndex);
		if (!floatAnim.IsConstant()) {
			std::vector<float> values;
			keyExtractor.GetFloatValues( iFloatAnimIndex, values );
			AnimationClipCompressedData::AnimatedData animChannel = {
				BitCompressFloatArray( values, formatFloat[iFloat].m_compressionType ),
				iFloat
			};
			compressedData.m_anims[kChannelTypeScalar].push_back( animChannel );
		} else {
			AnimChannelCompressionType compressionType = formatFloat[iFloat].m_compressionType;
			unsigned iType;
			for (iType = 0; iType < constFloatCompressionType.size(); ++iType)
				if (compressionType == constFloatCompressionType[iType])
					break;
			if (iType == constFloatCompressionType.size()) {
				constFloatCompressionType.push_back(compressionType);
				constFloatData.push_back( std::vector<float>(1, floatAnim[0]) );
				AnimationClipCompressedData::ConstantData constChannel = {
					NULL, std::vector<size_t>(1, iFloat)
				};
				compressedData.m_const[kChannelTypeScalar].push_back(constChannel);
			} else {
				constFloatData[iType].push_back( floatAnim[0] );
				compressedData.m_const[kChannelTypeScalar][iType].m_ids.push_back(iFloat);
			}
		}
	}
	{	// compress constant data
		for (unsigned iType = 0; iType < constFloatCompressionType.size(); ++iType)
			compressedData.m_const[kChannelTypeScalar][iType].m_pCompressedArray = BitCompressFloatArray( constFloatData[iType], constFloatCompressionType[iType] );
	}
}

}	//namespace AnimProcessing
}	//namespace Tools
}	//namespace OrbisAnim
